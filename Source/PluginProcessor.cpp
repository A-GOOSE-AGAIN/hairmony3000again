#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace hm;

//==============================================================================
HairmonyProcessor::HairmonyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pGain   = apvts.getRawParameterValue (ParamIDs::gain);
    pLoCut  = apvts.getRawParameterValue (ParamIDs::loCut);
    pBright = apvts.getRawParameterValue (ParamIDs::brightness);
    pReverb = apvts.getRawParameterValue (ParamIDs::reverb);
    pFollow = apvts.getRawParameterValue (ParamIDs::follow);
    for (auto& m : mutes) m.store (false);
    tempFolder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HAIRMONY 3000");
    sessionId = juce::Uuid().toString().substring (0, 8);
    postFx.prepare (48000.0);
    startTimerHz (15);
}

HairmonyProcessor::~HairmonyProcessor()
{
    stopTimer();
    captureState.store (captureIdle);
    cancelPendingUpdate();
    if (job) { job->requestCancel(); job.reset(); }
}

juce::AudioProcessorValueTreeState::ParameterLayout HairmonyProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto toArray = [] (const std::vector<std::string>& v) { StringArray a; for (auto& s : v) a.add (s); return a; };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::choir, 1 }, "Choir", toArray (choirNames()), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mic, 1 }, "Microphone", toArray (micNames()), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::octUp, 1 }, "Octave Up", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::octDown, 1 }, "Octave Down", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::key, 1 }, "Key", toArray (keyNames()), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scale, 1 }, "Scale", toArray (scaleNames()), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::third, 1 }, "Third Above", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::fifth, 1 }, "Fifth Above", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::below, 1 }, "Below", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::stereo, 1 }, "Output Format", true,
                    AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return v ? String ("Stereo") : String ("Mono"); })
                                                  .withValueFromStringFunction ([] (const String& s) { return ! s.trim().equalsIgnoreCase ("mono"); })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::voicing, 1 }, "Voicing", toArray (voicingNames()), 2));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::follow, 1 }, "Play With DAW", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::gain, 1 }, "Gain",
                    NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::loCut, 1 }, "Lo-Cut",
                    NormalisableRange<float> (20.0f, 800.0f, 1.0f, 0.4f), 20.0f,
                    AudioParameterFloatAttributes().withLabel ("Hz")
                        .withStringFromValueFunction ([] (float v, int) { return v <= 20.5f ? String ("OFF") : String (roundToInt (v)) + " Hz"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::brightness, 1 }, "Brightness",
                    NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::reverb, 1 }, "Reverb",
                    NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
    return layout;
}

//==============================================================================
void HairmonyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    postFx.prepare (hostRate);
    preview.setSize (2, juce::jmax (512, samplesPerBlock));
    gainSmoothed.reset (hostRate, 0.05);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));

    const juce::ScopedLock sl (sessionLock);
    if (tracks.empty() && outputs.empty()) workRate = hostRate;
}

void HairmonyProcessor::releaseResources() {}

bool HairmonyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void HairmonyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (numIn == 1 && numOut > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n); // mono in -> both sides

    // ---- Host transport
    bool hostIsPlaying = false, hasTransport = false;
    juce::int64 hostPos = -1;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostIsPlaying = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples()) { hostPos = *t; hasTransport = true; }
        }
    }
    hostPlaying.store (hostIsPlaying && hasTransport);

    // ---- Record from track (the dry input, before any playback is mixed in)
    captureBusy.store (true);
    {
        const int cs = captureState.load();
        if (cs != captureIdle && n > 0 && numIn > 0)
        {
            const int inCh = juce::jmin (numIn, buffer.getNumChannels());
            float pk = 0.0f;
            for (int ch = 0; ch < inCh; ++ch) pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, n));
            captureLevel.store (juce::jmax (pk, captureLevel.load() * 0.85f));

            if (cs == captureArmed && (! hasTransport || hostIsPlaying))
            {
                captureWrite.store (0);
                captureHostStart.store (hasTransport ? hostPos : -1);
                captureExpectedPos.store (hasTransport ? hostPos : -1);
                captureState.store (captureRecording);
            }

            if (captureState.load() == captureRecording)
            {
                bool stop = false;
                if (hasTransport)
                {
                    const auto expected = captureExpectedPos.load();
                    if (! hostIsPlaying) stop = true;                                     // DAW stopped
                    else if (expected >= 0 && std::llabs (hostPos - expected) > (juce::int64) juce::jmax (n, 64)) stop = true; // loop / jump
                }
                if (! stop)
                {
                    const int w = captureWrite.load();
                    const int room = (int) captureBuf.size() - w;
                    const int m = juce::jmin (n, juce::jmax (0, room));
                    const float scale = 1.0f / (float) inCh;
                    for (int i = 0; i < m; ++i)
                    {
                        float v = 0.0f;
                        for (int ch = 0; ch < inCh; ++ch) v += buffer.getSample (ch, i);
                        captureBuf[(size_t) (w + i)] = v * scale;
                    }
                    captureWrite.store (w + m);
                    if (hasTransport) captureExpectedPos.store (hostPos + n);
                    if (m < n) stop = true;                                                 // buffer full
                }
                if (stop) finishCaptureAudioThread();
            }
        }
    }
    captureBusy.store (false);

    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    postFx.setParams (pLoCut->load(), pBright->load(), pReverb->load());

    std::shared_ptr<const PlaybackSet> set;
    {
        const juce::SpinLock::ScopedTryLockType tl (setLock);
        if (tl.isLocked()) set = current;
    }

    // ---- Choir playing along with the DAW at the position the take was recorded from
    const bool synced = set != nullptr && set->anySynced && set->applyFx && pFollow->load() > 0.5f
                        && hasTransport && hostIsPlaying && hostPos >= 0;
    syncedNow.store (synced);
    if (synced && playing.load()) { playing.store (false); position.store (0.0); }

    if ((! synced && ! playing.load()) || n == 0 || numOut == 0 || set == nullptr || set->length <= 0)
    {
        gainSmoothed.skip (n);
        return;
    }

    if (resetFx.exchange (false)) postFx.reset();

    const int soloIdx = synced ? -1 : solo.load();
    const int numTracks = (int) juce::jmin<size_t> (set->tracks.size(), (size_t) maxTracks);
    int active = 0;
    for (int t = 0; t < numTracks; ++t)
        if (! mutes[t].load() && (soloIdx < 0 || soloIdx == t) && (! synced || set->hostStarts[(size_t) t] >= 0)) ++active;
    // Summing many stems gets loud fast; keep the audition mix near unity.
    const float mixScale = 1.0f / std::sqrt ((float) juce::jmax (1, active));

    const double hr = hostRate > 0.0 ? hostRate : set->sampleRate;
    const double inc = set->sampleRate / hr;
    double pos = position.load();
    bool finished = false;
    const int chunkMax = preview.getNumSamples();

    auto addTrack = [&] (int t, double p, float& l, float& r)
    {
        const auto& chans = set->tracks[(size_t) t]->channels;
        const int len = (int) chans[0].size();
        if (p < 0.0) return;
        const int i0 = (int) p;
        if (i0 >= len) return;
        const int i1 = juce::jmin (i0 + 1, len - 1);
        const float fr = (float) (p - (double) i0);
        const float a = chans[0][(size_t) i0] + (chans[0][(size_t) i1] - chans[0][(size_t) i0]) * fr;
        const float b = chans.size() > 1 ? chans[1][(size_t) i0] + (chans[1][(size_t) i1] - chans[1][(size_t) i0]) * fr : a;
        l += a * mixScale; r += b * mixScale;
    };

    for (int start = 0; start < n && ! finished; start += chunkMax)
    {
        const int m = juce::jmin (chunkMax, n - start);
        preview.clear (0, m);
        float* L = preview.getWritePointer (0);
        float* R = preview.getWritePointer (1);

        if (synced)
        {
            for (int i = 0; i < m; ++i)
            {
                const juce::int64 hp = hostPos + start + i;
                for (int t = 0; t < numTracks; ++t)
                {
                    const auto hs = set->hostStarts[(size_t) t];
                    if (hs < 0 || mutes[t].load (std::memory_order_relaxed)) continue;
                    addTrack (t, (double) (hp - hs) * inc, L[i], R[i]);
                }
            }
            for (int t = 0; t < numTracks; ++t)
                if (set->hostStarts[(size_t) t] >= 0) { position.store (juce::jmax (0.0, (double) (hostPos + start + m - set->hostStarts[(size_t) t]) * inc)); break; }
        }
        else
        {
            int produced = 0;
            for (; produced < m; ++produced)
            {
                if ((int) pos >= set->length) { finished = true; break; }
                for (int t = 0; t < numTracks; ++t)
                {
                    if (mutes[t].load (std::memory_order_relaxed) || (soloIdx >= 0 && soloIdx != t)) continue;
                    addTrack (t, pos, L[produced], R[produced]);
                }
                pos += inc;
            }
        }

        if (set->applyFx) postFx.process (L, R, m); // keep running to the end of the chunk for the reverb tail

        for (int i = 0; i < m; ++i)
        {
            const float g = gainSmoothed.getNextValue();
            L[i] *= g; R[i] *= g;
        }
        if (numOut >= 2)
        {
            buffer.addFrom (0, start, L, m);
            buffer.addFrom (1, start, R, m);
        }
        else
        {
            buffer.addFrom (0, start, L, m, 0.5f);
            buffer.addFrom (0, start, R, m, 0.5f);
        }
    }

    if (! synced)
    {
        if (finished) { playing.store (false); position.store (0.0); }
        else position.store (pos);
    }
}

void HairmonyProcessor::finishCaptureAudioThread()
{
    captureState.store (captureIdle);
    captureDone.store (true);
}

//==============================================================================
bool HairmonyProcessor::armCapture()
{
    if (isRendering()) return false;
    pollCapture();
    if (captureState.load() != captureIdle) return false;
    captureRate = hostRate > 0.0 ? hostRate : 48000.0;
    try { captureBuf.assign ((size_t) (maxCaptureSeconds * captureRate), 0.0f); }
    catch (const std::bad_alloc&) { const juce::ScopedLock sl (sessionLock); lastMessage = "Not enough memory to record"; return false; }
    captureWrite.store (0);
    captureDone.store (false);
    captureLevel.store (0.0f);
    stop();
    captureState.store (captureArmed);
    {
        const juce::ScopedLock sl (sessionLock);
        lastMessage = hostRate > 0.0 ? "Armed - press play in your DAW and sing (or play the vocal track)" : "Recording...";
    }
    bump();
    return true;
}

static void waitUntilFalse (const std::atomic<bool>& flag)
{
    for (int i = 0; i < 2000 && flag.load(); ++i) std::this_thread::yield();
}

void HairmonyProcessor::stopCapture()
{
    const int prev = captureState.exchange (captureIdle);
    waitUntilFalse (captureBusy);
    if (prev == captureRecording || captureDone.exchange (false)) importCapture();
    else { std::vector<float>().swap (captureBuf); const juce::ScopedLock sl (sessionLock); lastMessage.clear(); }
    bump();
}

bool HairmonyProcessor::pollCapture()
{
    if (! captureDone.exchange (false)) return false;
    waitUntilFalse (captureBusy);
    importCapture();
    return true;
}

double HairmonyProcessor::getCaptureSeconds() const
{
    return (double) captureWrite.load() / juce::jmax (1.0, captureRate);
}

void HairmonyProcessor::importCapture()
{
    const int len = juce::jlimit (0, (int) captureBuf.size(), captureWrite.load());
    std::vector<float> take (captureBuf.begin(), captureBuf.begin() + len);
    std::vector<float>().swap (captureBuf);
    captureDone.store (false);
    captureLevel.store (0.0f);
    juce::int64 hostStart = captureHostStart.load();

    // Trim silence before/after the performance (keeping the timeline position exact).
    const float thr = 0.001f; // -60 dBFS
    int first = 0, last = len;
    while (first < len && std::abs (take[(size_t) first]) < thr) ++first;
    while (last > first && std::abs (take[(size_t) last - 1]) < thr) --last;
    first = juce::jmax (0, first - (int) (0.01 * captureRate));
    last = juce::jmin (len, last + (int) (0.05 * captureRate));

    if (last - first < (int) (0.25 * captureRate))
    {
        const juce::ScopedLock sl (sessionLock);
        lastMessage = "Nothing was recorded - arm REC, then play the track with your vocal on it";
        bump();
        return;
    }
    std::vector<float> mono (take.begin() + first, take.begin() + last);
    if (hostStart >= 0) hostStart += first;

    double rate;
    {
        const juce::ScopedLock sl (sessionLock);
        if (tracks.empty() && outputs.empty()) workRate = captureRate;
        rate = workRate;
        if ((int) tracks.size() >= 16) { lastMessage = "Up to 16 vocals per session"; bump(); return; }
    }
    if (std::abs (rate - captureRate) > 0.5) mono = resample (mono, captureRate, rate);

    const auto name = juce::String ("Take ") + juce::String (++takeCounter);
    auto stem = std::make_shared<Stem>();
    stem->name = name.toStdString();
    stem->channels.push_back (std::move (mono));
    auto file = sessionFolder().getChildFile ("takes").getChildFile (name + ".wav");
    juce::String err;
    if (! writeWav (file, *stem, rate, 32, err)) file = juce::File();

    ImportedTrack t;
    t.name = name;
    t.source = file;
    t.audio = std::move (stem);
    t.hostStart = hostStart;
    const double seconds = (double) t.audio->numSamples() / rate;
    {
        const juce::ScopedLock sl (sessionLock);
        tracks.push_back (std::move (t));
        lastMessage = "Recorded " + name + " (" + juce::String (seconds, 1) + " s)"
                    + (hostStart >= 0 ? " from " + juce::String ((double) hostStart / juce::jmax (1.0, captureRate), 1) + " s on your timeline" : juce::String());
        if (screen.load() == reviewScreen) outputs.clear();
    }
    setScreenInternal (craftScreen);
}

void HairmonyProcessor::timerCallback()
{
    pollCapture();
}

//==============================================================================
juce::AudioProcessorEditor* HairmonyProcessor::createEditor() { return new HairmonyEditor (*this); }

juce::String HairmonyProcessor::getLastMessage() const
{
    const juce::ScopedLock sl (sessionLock);
    return lastMessage;
}

juce::File HairmonyProcessor::sessionFolder() const
{
    return getTempFolder().getChildFile ("session-" + sessionId);
}

juce::File HairmonyProcessor::getTempFolder() const
{
    const juce::ScopedLock sl (sessionLock);
    return tempFolder;
}

void HairmonyProcessor::setTempFolder (const juce::File& f)
{
    {
        const juce::ScopedLock sl (sessionLock);
        if (f == juce::File()) return;
        tempFolder = f;
    }
    f.createDirectory();
    bump();
}

void HairmonyProcessor::clearTempFiles()
{
    const auto keep = sessionFolder();
    const auto root = getTempFolder();
    for (auto& child : root.findChildFiles (juce::File::findDirectories, false, "session-*"))
        if (child != keep) child.deleteRecursively();
    keep.getChildFile ("export").deleteRecursively();
    const juce::ScopedLock sl (sessionLock);
    lastMessage = "Old renders cleared";
    bump();
}

void HairmonyProcessor::setScreenInternal (Screen s)
{
    screen.store (s);
    publishPlayback();
    bump();
}

//==============================================================================
juce::StringArray HairmonyProcessor::importFiles (const juce::Array<juce::File>& files)
{
    juce::StringArray errors;
    if (isRendering()) { errors.add ("Wait for the choir to finish (or cancel it) before importing"); return errors; }

    double rate;
    {
        const juce::ScopedLock sl (sessionLock);
        if (tracks.empty() && outputs.empty() && hostRate > 0.0) workRate = hostRate;
        rate = workRate;
    }

    int added = 0;
    for (auto& f : files)
    {
        if (f.isDirectory()) { errors.add (f.getFileName() + ": folders aren't supported, drop the audio files"); continue; }
        if (! isSupportedAudioFile (f)) { errors.add (f.getFileName() + ": unsupported file type"); continue; }

        {
            const juce::ScopedLock sl (sessionLock);
            if ((int) tracks.size() >= 16) { errors.add ("Up to 16 vocals per session"); break; }
        }

        std::vector<float> mono;
        juce::String err;
        if (! loadAudioFileMono (f, rate, mono, err)) { errors.add (err); continue; }

        auto stem = std::make_shared<Stem>();
        stem->name = f.getFileNameWithoutExtension().toStdString();
        stem->channels.push_back (std::move (mono));

        ImportedTrack t;
        t.name = f.getFileNameWithoutExtension();
        t.source = f;
        t.audio = std::move (stem);
        {
            const juce::ScopedLock sl (sessionLock);
            tracks.push_back (std::move (t));
        }
        ++added;
    }

    {
        const juce::ScopedLock sl (sessionLock);
        lastMessage = errors.joinIntoString ("\n");
    }
    if (added > 0)
    {
        stop();
        if (screen.load() == reviewScreen) { const juce::ScopedLock sl (sessionLock); outputs.clear(); }
        setScreenInternal (craftScreen);
    }
    else bump();
    return errors;
}

std::vector<HairmonyProcessor::TrackInfo> HairmonyProcessor::getTrackInfos() const
{
    const juce::ScopedLock sl (sessionLock);
    std::vector<TrackInfo> v;
    for (auto& t : tracks)
        v.push_back ({ t.name, (double) t.audio->numSamples() / workRate, t.muted, t.hostStart });
    return v;
}

void HairmonyProcessor::setTrackMuted (int index, bool muted)
{
    {
        const juce::ScopedLock sl (sessionLock);
        if (! juce::isPositiveAndBelow (index, (int) tracks.size())) return;
        tracks[(size_t) index].muted = muted;
    }
    if (screen.load() == craftScreen && index < maxTracks) mutes[index].store (muted);
    bump();
}

void HairmonyProcessor::startOver()
{
    cancelRender();
    if (captureState.load() != captureIdle) { captureState.store (captureIdle); waitUntilFalse (captureBusy); std::vector<float>().swap (captureBuf); captureDone.store (false); }
    takeCounter = 0;
    stop();
    {
        const juce::ScopedLock sl (sessionLock);
        tracks.clear();
        outputs.clear();
        lastMessage.clear();
        if (hostRate > 0.0) workRate = hostRate;
    }
    sessionFolder().deleteRecursively();
    setScreenInternal (importScreen);
}

//==============================================================================
RenderSettings HairmonyProcessor::currentSettings() const
{
    auto choice = [this] (const char* id) { return juce::roundToInt (apvts.getRawParameterValue (id)->load()); };
    auto flag   = [this] (const char* id) { return apvts.getRawParameterValue (id)->load() > 0.5f; };
    RenderSettings s;
    s.choir = (ChoirModel) juce::jlimit (0, (int) ChoirModel::numModels - 1, choice (ParamIDs::choir));
    s.mic = (MicModel) juce::jlimit (0, (int) MicModel::numMics - 1, choice (ParamIDs::mic));
    s.octaveUp = flag (ParamIDs::octUp);
    s.octaveDown = flag (ParamIDs::octDown);
    s.key = juce::jlimit (0, 11, choice (ParamIDs::key));
    s.scale = (Scale) juce::jlimit (0, (int) Scale::numScales - 1, choice (ParamIDs::scale));
    s.thirdAbove = flag (ParamIDs::third);
    s.fifthAbove = flag (ParamIDs::fifth);
    s.below = flag (ParamIDs::below);
    s.stereo = flag (ParamIDs::stereo);
    s.voicing = (Voicing) juce::jlimit (0, (int) Voicing::numVoicings - 1, choice (ParamIDs::voicing));
    return s;
}

bool HairmonyProcessor::startRender()
{
    if (isRendering()) return false;

    std::vector<RenderJob::Input> inputs;
    double rate;
    {
        const juce::ScopedLock sl (sessionLock);
        for (auto& t : tracks)
            if (! t.muted) inputs.push_back ({ t.name, t.audio, t.hostStart });
        rate = workRate;
        if (inputs.empty())
        {
            lastMessage = tracks.empty() ? "Import a vocal first" : "Every vocal is muted - unmute one to build a choir on it";
            bump();
            return false;
        }
        lastMessage.clear();
    }

    stop();
    auto folder = sessionFolder().getChildFile ("render-" + juce::String (juce::Time::currentTimeMillis()));
    job = std::make_unique<RenderJob> (std::move (inputs), rate, currentSettings(), folder, [this] { triggerAsyncUpdate(); });
    job->startThread (juce::Thread::Priority::normal);
    bump();
    return true;
}

bool HairmonyProcessor::isRendering() const { return job != nullptr && ! job->isDone(); }
float HairmonyProcessor::getRenderProgress() const { return job ? job->getProgress() : 0.0f; }

void HairmonyProcessor::cancelRender()
{
    if (job == nullptr) return;
    job->requestCancel();
    job->stopThread (10000);
    if (job->getResults().size() > 0)
        for (auto& r : job->getResults()) if (r.rawFile.existsAsFile()) r.rawFile.deleteFile();
    job.reset();
    const juce::ScopedLock sl (sessionLock);
    lastMessage = "Render cancelled";
    bump();
}

bool HairmonyProcessor::pollRender()
{
    if (job == nullptr || ! job->isDone()) return false;
    job->stopThread (2000);

    if (! job->wasCancelled())
    {
        std::vector<OutputTrack> made;
        for (auto& r : job->getResults())
        {
            OutputTrack o;
            o.name = r.name;
            o.rawFile = r.rawFile;
            o.hostStart = r.hostStart;
            o.stem = std::make_shared<const Stem> (std::move (r.stem));
            made.push_back (std::move (o));
        }
        {
            const juce::ScopedLock sl (sessionLock);
            outputs = std::move (made);
            lastMessage = job->getError();
        }
        job.reset();
        setScreenInternal (reviewScreen);
    }
    else
    {
        job.reset();
        bump();
    }
    return true;
}

void HairmonyProcessor::handleAsyncUpdate() { pollRender(); }

//==============================================================================
std::vector<HairmonyProcessor::TrackInfo> HairmonyProcessor::getOutputInfos() const
{
    const juce::ScopedLock sl (sessionLock);
    std::vector<TrackInfo> v;
    for (auto& o : outputs)
        v.push_back ({ o.name, (double) o.stem->numSamples() / workRate, o.muted, o.hostStart });
    return v;
}

void HairmonyProcessor::setOutputMuted (int index, bool muted)
{
    {
        const juce::ScopedLock sl (sessionLock);
        if (! juce::isPositiveAndBelow (index, (int) outputs.size())) return;
        outputs[(size_t) index].muted = muted;
    }
    if (screen.load() == reviewScreen && index < maxTracks) mutes[index].store (muted);
    bump();
}

void HairmonyProcessor::backToCraft()
{
    stop();
    {
        const juce::ScopedLock sl (sessionLock);
        for (auto& o : outputs) if (o.rawFile.existsAsFile()) { o.rawFile.getParentDirectory().deleteRecursively(); break; }
        outputs.clear();
    }
    setScreenInternal (tracks.empty() ? importScreen : craftScreen);
}

std::vector<float> HairmonyProcessor::getOutputPeaks (int index, int numBuckets) const
{
    StemPtr stem;
    {
        const juce::ScopedLock sl (sessionLock);
        if (! juce::isPositiveAndBelow (index, (int) outputs.size()) || numBuckets <= 0) return {};
        stem = outputs[(size_t) index].stem;
    }
    std::vector<float> peaks ((size_t) numBuckets, 0.0f);
    const int len = stem->numSamples();
    if (len == 0) return peaks;
    for (int b = 0; b < numBuckets; ++b)
    {
        const int s0 = (int) ((juce::int64) len * b / numBuckets);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) len * (b + 1) / numBuckets));
        float p = 0.0f;
        for (auto& ch : stem->channels)
            for (int i = s0; i < juce::jmin (s1, len); ++i) p = juce::jmax (p, std::abs (ch[(size_t) i]));
        peaks[(size_t) b] = p;
    }
    return peaks;
}

Stem HairmonyProcessor::processedStem (int index) const
{
    StemPtr stem; double rate;
    {
        const juce::ScopedLock sl (sessionLock);
        if (! juce::isPositiveAndBelow (index, (int) outputs.size())) return {};
        stem = outputs[(size_t) index].stem;
        rate = workRate;
    }
    auto s = processStemOffline (*stem, rate, pLoCut->load(), pBright->load(), pReverb->load());
    s.name = stem->name;
    return s;
}

juce::File HairmonyProcessor::createExportFile (int index)
{
    juce::String name;
    {
        const juce::ScopedLock sl (sessionLock);
        if (! juce::isPositiveAndBelow (index, (int) outputs.size())) return {};
        name = outputs[(size_t) index].name;
    }
    const auto stem = processedStem (index);
    auto file = sessionFolder().getChildFile ("export").getChildFile ("HAIRMONY " + safeFileName (name) + ".wav");
    juce::String err;
    if (! writeWav (file, stem, workRate, 24, err))
    {
        const juce::ScopedLock sl (sessionLock);
        lastMessage = err;
        return {};
    }
    return file;
}

juce::Array<juce::File> HairmonyProcessor::createAllExportFiles()
{
    juce::Array<juce::File> files;
    const int n = (int) getOutputInfos().size();
    for (int i = 0; i < n; ++i)
        if (auto f = createExportFile (i); f.existsAsFile()) files.add (f);
    return files;
}

bool HairmonyProcessor::exportAllTo (const juce::File& folder, juce::StringArray& written)
{
    if (! folder.createDirectory()) return false;
    bool ok = true;
    for (auto& f : createAllExportFiles())
    {
        auto dest = folder.getChildFile (f.getFileName());
        if (f.copyFileTo (dest)) written.add (dest.getFullPathName());
        else ok = false;
    }
    return ok && ! written.isEmpty();
}

//==============================================================================
void HairmonyProcessor::publishPlayback()
{
    auto set = std::make_shared<PlaybackSet>();
    {
        const juce::ScopedLock sl (sessionLock);
        set->sampleRate = workRate;
        const int scr = screen.load();
        if (scr == craftScreen)
            for (auto& t : tracks) { set->tracks.push_back (t.audio); set->hostStarts.push_back (-1); }
        else if (scr == reviewScreen)
            for (auto& o : outputs) { set->tracks.push_back (o.stem); set->hostStarts.push_back (o.hostStart); set->anySynced |= o.hostStart >= 0; }
        set->applyFx = scr == reviewScreen;
        for (int i = 0; i < maxTracks; ++i)
        {
            bool m = false;
            if (scr == craftScreen && i < (int) tracks.size()) m = tracks[(size_t) i].muted;
            if (scr == reviewScreen && i < (int) outputs.size()) m = outputs[(size_t) i].muted;
            mutes[i].store (m);
        }
    }
    for (auto& t : set->tracks) set->length = juce::jmax (set->length, t->numSamples());

    std::shared_ptr<const PlaybackSet> old;
    {
        const juce::SpinLock::ScopedLockType l (setLock);
        old = std::move (current);
        current = std::move (set);
    }
    position.store (0.0);
    // Keep the previous set alive here so the audio thread never frees it mid-block.
    if (old) retired.push_back (std::move (old));
    releaseOldSets();
}

void HairmonyProcessor::releaseOldSets()
{
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& p) { return p.use_count() <= 1; }), retired.end());
}

void HairmonyProcessor::play (int soloIndex)
{
    releaseOldSets();
    std::shared_ptr<const PlaybackSet> set;
    {
        const juce::SpinLock::ScopedLockType l (setLock);
        set = current;
    }
    if (set == nullptr || set->length == 0) return;
    if (! playing.load()) { position.store (0.0); resetFx.store (true); }
    solo.store (soloIndex);
    playing.store (true);
}

void HairmonyProcessor::stop()
{
    playing.store (false);
    position.store (0.0);
    solo.store (-1);
}

double HairmonyProcessor::getPlayheadSeconds() const
{
    const juce::ScopedLock sl (sessionLock);
    return position.load() / workRate;
}

double HairmonyProcessor::getPlaybackLengthSeconds() const
{
    std::shared_ptr<const PlaybackSet> set;
    {
        const juce::SpinLock::ScopedLockType l (const_cast<juce::SpinLock&> (setLock));
        set = current;
    }
    return set ? (double) set->length / set->sampleRate : 0.0;
}

//==============================================================================
void HairmonyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("HAIRMONY3000");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree s ("Session");
    {
        const juce::ScopedLock sl (sessionLock);
        s.setProperty ("screen", screen.load(), nullptr);
        s.setProperty ("workRate", workRate, nullptr);
        s.setProperty ("tempFolder", tempFolder.getFullPathName(), nullptr);
        s.setProperty ("sessionId", sessionId, nullptr);
        s.setProperty ("takeCounter", takeCounter, nullptr);
        for (auto& t : tracks)
        {
            juce::ValueTree c ("Track");
            c.setProperty ("name", t.name, nullptr);
            c.setProperty ("path", t.source.getFullPathName(), nullptr);
            c.setProperty ("muted", t.muted, nullptr);
            c.setProperty ("hostStart", (juce::int64) t.hostStart, nullptr);
            s.appendChild (c, nullptr);
        }
        for (auto& o : outputs)
        {
            juce::ValueTree c ("Output");
            c.setProperty ("name", o.name, nullptr);
            c.setProperty ("path", o.rawFile.getFullPathName(), nullptr);
            c.setProperty ("muted", o.muted, nullptr);
            c.setProperty ("hostStart", (juce::int64) o.hostStart, nullptr);
            s.appendChild (c, nullptr);
        }
    }
    root.appendChild (s, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void HairmonyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("HAIRMONY3000")) return;

    auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (params);

    auto s = root.getChildWithName ("Session");
    if (s.isValid()) restoreSession (s);
}

void HairmonyProcessor::restoreSession (const juce::ValueTree& s)
{
    if (job) { job->requestCancel(); job.reset(); }
    stop();

    std::vector<ImportedTrack> newTracks;
    std::vector<OutputTrack> newOutputs;
    juce::StringArray problems;

    const double rate = juce::jlimit (8000.0, 768000.0, (double) s.getProperty ("workRate", 48000.0));
    auto folder = juce::File (s.getProperty ("tempFolder").toString());
    if (folder == juce::File() || ! juce::File::isAbsolutePath (folder.getFullPathName()))
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("HAIRMONY 3000");

    for (const auto& c : s)
    {
        if (c.hasType ("Track"))
        {
            const juce::File f (c.getProperty ("path").toString());
            std::vector<float> mono; juce::String err;
            if (juce::File::isAbsolutePath (f.getFullPathName()) && loadAudioFileMono (f, rate, mono, err))
            {
                auto stem = std::make_shared<Stem>();
                stem->channels.push_back (std::move (mono));
                newTracks.push_back ({ c.getProperty ("name").toString(), f, std::move (stem), (bool) c.getProperty ("muted", false),
                                       (juce::int64) c.getProperty ("hostStart", -1) });
            }
            else problems.add ("Missing vocal: " + f.getFileName());
        }
        else if (c.hasType ("Output"))
        {
            const juce::File f (c.getProperty ("path").toString());
            Stem stem; double r = rate; juce::String err;
            if (juce::File::isAbsolutePath (f.getFullPathName()) && f.existsAsFile() && loadStem (f, stem, r, err))
            {
                if (std::abs (r - rate) > 0.5)
                    for (auto& ch : stem.channels) ch = resample (ch, r, rate);
                stem.name = c.getProperty ("name").toString().toStdString();
                newOutputs.push_back ({ c.getProperty ("name").toString(), std::make_shared<const Stem> (std::move (stem)),
                                        (bool) c.getProperty ("muted", false), f, (juce::int64) c.getProperty ("hostStart", -1) });
            }
        }
    }

    int wanted = (int) s.getProperty ("screen", 0);
    Screen target = importScreen;
    if (wanted >= reviewScreen && ! newOutputs.empty()) target = reviewScreen;
    else if (! newTracks.empty()) target = craftScreen;
    else if (! newOutputs.empty()) target = reviewScreen;

    {
        const juce::ScopedLock sl (sessionLock);
        workRate = rate;
        tempFolder = folder;
        const auto id = s.getProperty ("sessionId").toString();
        if (id.isNotEmpty()) sessionId = juce::File::createLegalFileName (id).substring (0, 16);
        tracks = std::move (newTracks);
        outputs = std::move (newOutputs);
        takeCounter = juce::jmax (0, (int) s.getProperty ("takeCounter", 0));
        lastMessage = problems.joinIntoString ("\n");
    }
    setScreenInternal (target);
}

//==============================================================================
#if ! defined (HAIRMONY_TEST_BUILD)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HairmonyProcessor(); }
#endif
