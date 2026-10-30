// HAIRMONY 3000 - audio processor: session model, background rendering and preview playback.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Session.h"
#include <thread>

namespace ParamIDs
{
    inline constexpr const char* choir      = "choir";
    inline constexpr const char* mic        = "mic";
    inline constexpr const char* octUp      = "octUp";
    inline constexpr const char* octDown    = "octDown";
    inline constexpr const char* key        = "key";
    inline constexpr const char* scale      = "scale";
    inline constexpr const char* third      = "third";
    inline constexpr const char* fifth      = "fifth";
    inline constexpr const char* below      = "below";
    inline constexpr const char* stereo     = "stereo";
    inline constexpr const char* gain       = "gain";
    inline constexpr const char* loCut      = "loCut";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* reverb     = "reverb";
    inline constexpr const char* voicing    = "voicing";
    inline constexpr const char* follow     = "follow";
}

class HairmonyProcessor : public juce::AudioProcessor,
                          private juce::AsyncUpdater,
                          private juce::Timer
{
public:
    enum Screen { importScreen = 0, craftScreen = 1, reviewScreen = 2 };
    enum CaptureState { captureIdle = 0, captureArmed = 1, captureRecording = 2 };
    static constexpr int maxTracks = 64;
    static constexpr double maxCaptureSeconds = 5.0 * 60.0;

    struct TrackInfo { juce::String name; double seconds = 0.0; bool muted = false; juce::int64 hostStart = -1; };

    HairmonyProcessor();
    ~HairmonyProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HAIRMONY 3000"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getState() { return apvts; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    int getScreen() const noexcept { return screen.load(); }
    int getSessionVersion() const noexcept { return sessionVersion.load(); }
    juce::String getLastMessage() const;

    // Screen 1 / 2: importing
    juce::StringArray importFiles (const juce::Array<juce::File>& files);
    std::vector<TrackInfo> getTrackInfos() const;
    void setTrackMuted (int index, bool muted);
    void startOver();

    // Screen 2: rendering
    hm::RenderSettings currentSettings() const;
    bool startRender();
    bool isRendering() const;
    float getRenderProgress() const;
    void cancelRender();
    bool pollRender(); // finalises a finished render on the message thread; true if it did

    // Screen 3: outputs
    std::vector<TrackInfo> getOutputInfos() const;
    void setOutputMuted (int index, bool muted);
    void backToCraft();
    // Peak envelope (max abs per bucket) of one output, for waveform drawing.
    std::vector<float> getOutputPeaks (int index, int numBuckets) const;
    juce::File createExportFile (int index);
    juce::Array<juce::File> createAllExportFiles();
    bool exportAllTo (const juce::File& folder, juce::StringArray& written);

    // Record from track: captures this channel's input while the DAW plays (or straight away in
    // hosts without a transport, e.g. the standalone app). Stopping the DAW imports the take.
    bool armCapture();
    void stopCapture();                       // finishes (imports) a take in progress, or disarms
    int getCaptureState() const noexcept { return captureState.load(); }
    double getCaptureSeconds() const;
    float getCaptureLevel() const noexcept { return captureLevel.load(); }
    bool pollCapture();                       // message thread: imports a finished take; true if it did
    bool isHostPlaying() const noexcept { return hostPlaying.load(); }

    // Transport (plays the current screen's tracks through the plugin output)
    void play (int soloIndex = -1);
    void stop();
    bool isPlaying() const noexcept { return playing.load(); }
    bool isSyncedPlaying() const noexcept { return syncedNow.load(); } // choir following the DAW right now
    int getSoloIndex() const noexcept { return solo.load(); }
    double getPlayheadSeconds() const;
    double getPlaybackLengthSeconds() const;

    // Settings
    juce::File getTempFolder() const;
    void setTempFolder (const juce::File& f);
    void clearTempFiles();
    double getWorkRate() const noexcept { return workRate; }
    juce::File sessionFolderForTests() const { return sessionFolder(); }

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void finishCaptureAudioThread();
    void importCapture();
    void publishPlayback();
    void setScreenInternal (Screen s);
    void bump() { ++sessionVersion; }
    juce::File sessionFolder() const;
    hm::Stem processedStem (int index) const;
    void restoreSession (const juce::ValueTree& s);
    void releaseOldSets();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pLoCut = nullptr;
    std::atomic<float>* pBright = nullptr;
    std::atomic<float>* pReverb = nullptr;

    // Session model (message thread; guarded by sessionLock for host-thread state calls)
    mutable juce::CriticalSection sessionLock;
    std::vector<hm::ImportedTrack> tracks;
    std::vector<hm::OutputTrack> outputs;
    double workRate = 48000.0;
    juce::File tempFolder;
    juce::String sessionId;
    juce::String lastMessage;
    std::atomic<int> screen { importScreen };
    std::atomic<int> sessionVersion { 0 };
    std::unique_ptr<hm::RenderJob> job;

    // Playback (audio thread reads)
    juce::SpinLock setLock;
    std::shared_ptr<const hm::PlaybackSet> current;
    std::vector<std::shared_ptr<const hm::PlaybackSet>> retired;
    std::atomic<bool> playing { false };
    std::atomic<int> solo { -1 };
    std::atomic<double> position { 0.0 };
    std::atomic<bool> mutes[maxTracks];
    std::atomic<bool> resetFx { false };
    std::atomic<float>* pFollow = nullptr;
    std::atomic<bool> hostPlaying { false }, syncedNow { false };

    // Capture (record from track). The buffer is only resized on the message thread while the
    // audio thread is guaranteed not to be inside the capture section (captureBusy == false).
    std::vector<float> captureBuf;
    std::atomic<int> captureState { captureIdle };
    std::atomic<int> captureWrite { 0 };
    std::atomic<juce::int64> captureHostStart { -1 };
    std::atomic<juce::int64> captureExpectedPos { -1 };
    std::atomic<bool> captureDone { false }, captureBusy { false };
    std::atomic<float> captureLevel { 0.0f };
    double captureRate = 48000.0;
    int takeCounter = 0;

    double hostRate = 0.0;
    hm::PostFx postFx;
    juce::AudioBuffer<float> preview;
    juce::SmoothedValue<float> gainSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HairmonyProcessor)
};
