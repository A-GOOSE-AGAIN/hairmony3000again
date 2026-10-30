#include "Session.h"

namespace hm
{
juce::AudioFormatManager& formatManager()
{
    static juce::AudioFormatManager fm;
    static bool init = false;
    if (! init) { fm.registerBasicFormats(); init = true; }
    return fm;
}

juce::String supportedWildcards()
{
    return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg";
}

bool isSupportedAudioFile (const juce::File& f)
{
    const auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".wave" || ext == ".aif" || ext == ".aiff"
        || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}

bool loadAudioFileMono (const juce::File& file, double targetRate, std::vector<float>& out, juce::String& error)
{
    out.clear();
    if (! file.existsAsFile()) { error = file.getFileName() + ": file not found"; return false; }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr) { error = file.getFileName() + ": not a readable audio file"; return false; }
    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) { error = file.getFileName() + ": file is empty"; return false; }

    const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
    if (seconds > kMaxImportSeconds) { error = file.getFileName() + ": longer than 10 minutes - trim it to the section you want a choir on"; return false; }

    const int numCh = (int) juce::jlimit (1u, 8u, reader->numChannels);
    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf (numCh, len);
    if (! reader->read (&buf, 0, len, 0, true, numCh > 1)) { error = file.getFileName() + ": read error"; return false; }

    std::vector<float> mono ((size_t) len, 0.0f);
    const float scale = 1.0f / (float) numCh;
    for (int c = 0; c < numCh; ++c)
    {
        const float* d = buf.getReadPointer (c);
        for (int i = 0; i < len; ++i) mono[(size_t) i] += d[i] * scale;
    }
    for (auto& v : mono) if (! std::isfinite (v)) v = 0.0f;

    out = resample (mono, reader->sampleRate, targetRate);
    return true;
}

bool loadStem (const juce::File& file, Stem& out, double& sampleRate, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr) { error = file.getFileName() + ": cannot open"; return false; }
    const int numCh = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxImportSeconds * 400000.0));
    juce::AudioBuffer<float> buf (numCh, len);
    if (! reader->read (&buf, 0, len, 0, true, numCh > 1)) { error = file.getFileName() + ": read error"; return false; }
    out.channels.assign ((size_t) numCh, std::vector<float> ((size_t) len));
    for (int c = 0; c < numCh; ++c)
        std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + len, out.channels[(size_t) c].begin());
    sampleRate = reader->sampleRate;
    return true;
}

bool writeWav (const juce::File& file, const Stem& stem, double sampleRate, int bits, juce::String& error)
{
    if (stem.channels.empty()) { error = "nothing to write"; return false; }
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk()) { error = "cannot write " + file.getFullPathName(); return false; }

    juce::WavAudioFormat wav;
    auto opts = juce::AudioFormatWriterOptions{}
                    .withSampleRate (sampleRate)
                    .withNumChannels ((int) stem.channels.size())
                    .withBitsPerSample (bits)
                    .withSampleFormat (bits == 32 ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                  : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = wav.createWriterFor (stream, opts);
    if (writer == nullptr) { error = "cannot create WAV writer"; return false; }

    std::vector<const float*> ptrs;
    for (auto& ch : stem.channels) ptrs.push_back (ch.data());
    const bool ok = writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), stem.numSamples());
    writer.reset();
    if (! ok) { error = "write failed: " + file.getFullPathName(); return false; }
    return true;
}

juce::String safeFileName (const juce::String& s)
{
    auto r = juce::File::createLegalFileName (s).replaceCharacters ("()'", "___").trim();
    return r.isEmpty() ? juce::String ("stem") : r;
}

//==============================================================================
RenderJob::RenderJob (std::vector<Input> in, double sampleRate, RenderSettings s, juce::File folder, std::function<void()> finished)
    : juce::Thread ("HAIRMONY render"), inputs (std::move (in)), rate (sampleRate), settings (s),
      rawFolder (std::move (folder)), onFinished (std::move (finished))
{
}

RenderJob::~RenderJob()
{
    requestCancel();
    stopThread (10000);
}

void RenderJob::run()
{
    const size_t n = inputs.size();
    for (size_t i = 0; i < n && ! cancel.load() && ! threadShouldExit(); ++i)
    {
        auto prog = [this, i, n] (float p) { progress.store (((float) i + p) / (float) n * 0.97f); };
        auto stems = renderChoir (inputs[i].audio->channels[0], rate, settings, prog, &cancel);
        if (cancel.load()) break;

        for (auto& st : stems)
        {
            Result r;
            r.name = n > 1 ? inputs[i].name + " - " + juce::String (st.name) : juce::String (st.name);
            r.stem = std::move (st);
            r.hostStart = inputs[i].hostStart;
            results.push_back (std::move (r));
        }
    }

    if (! cancel.load())
    {
        // Keep the raw renders on disk so the session can be recalled with the project.
        rawFolder.createDirectory();
        for (size_t i = 0; i < results.size() && ! cancel.load(); ++i)
        {
            auto f = rawFolder.getChildFile (juce::String ((int) i + 1).paddedLeft ('0', 2) + " " + safeFileName (results[i].name) + ".wav");
            juce::String err;
            if (writeWav (f, results[i].stem, rate, 32, err)) results[i].rawFile = f;
            else error = err;
        }
    }

    progress.store (1.0f);
    done.store (true);
    if (onFinished) onFinished();
}
} // namespace hm
