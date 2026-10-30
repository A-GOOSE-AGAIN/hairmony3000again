// HAIRMONY 3000 - audio file I/O helpers and the background render job.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include "core/ChoirEngine.h"

namespace hm
{
constexpr double kMaxImportSeconds = 10.0 * 60.0;

// Audio is held in shared, immutable Stems so the playback engine and the render thread can
// reference it without copying (choir stems for a long vocal can run to hundreds of MB).
using StemPtr = std::shared_ptr<const Stem>;

struct ImportedTrack
{
    juce::String name;
    juce::File source;
    StemPtr audio; // mono
    bool muted = false;
    juce::int64 hostStart = -1; // timeline sample where a recorded take began (-1 = imported file)
};

struct OutputTrack
{
    juce::String name;
    StemPtr stem;
    bool muted = false;
    juce::File rawFile;
    juce::int64 hostStart = -1; // inherited from the take it was made from
};

// Everything the audio thread needs to play one screen's tracks. Immutable once published.
struct PlaybackSet
{
    std::vector<StemPtr> tracks;
    std::vector<juce::int64> hostStarts; // per track, -1 when not tied to the DAW timeline
    double sampleRate = 48000.0;
    int length = 0;
    bool applyFx = false;
    bool anySynced = false;
};

juce::AudioFormatManager& formatManager();
juce::String supportedWildcards();
bool isSupportedAudioFile (const juce::File& f);

// Reads any supported file, mixes to mono and resamples to `targetRate`.
bool loadAudioFileMono (const juce::File& file, double targetRate, std::vector<float>& out, juce::String& error);

// Reads a (mono or stereo) file at its own rate without conversion.
bool loadStem (const juce::File& file, Stem& out, double& sampleRate, juce::String& error);

bool writeWav (const juce::File& file, const Stem& stem, double sampleRate, int bitsPerSample, juce::String& error);

juce::String safeFileName (const juce::String& s);

//==============================================================================
class RenderJob : public juce::Thread
{
public:
    struct Input { juce::String name; StemPtr audio; juce::int64 hostStart = -1; };
    struct Result { juce::String name; Stem stem; juce::File rawFile; juce::int64 hostStart = -1; };

    RenderJob (std::vector<Input> inputs, double sampleRate, RenderSettings settings,
               juce::File rawFolder, std::function<void()> onFinished);
    ~RenderJob() override;

    void run() override;

    float getProgress() const noexcept { return progress.load(); }
    bool isDone() const noexcept { return done.load(); }
    bool wasCancelled() const noexcept { return cancel.load(); }
    void requestCancel() noexcept { cancel.store (true); }

    // Only valid once isDone() is true.
    std::vector<Result>& getResults() { return results; }
    const juce::String& getError() const { return error; }
    double getSampleRate() const noexcept { return rate; }

private:
    std::vector<Input> inputs;
    double rate;
    RenderSettings settings;
    juce::File rawFolder;
    std::function<void()> onFinished;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancel { false }, done { false };
    std::vector<Result> results;
    juce::String error;
};
} // namespace hm
