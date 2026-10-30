// HAIRMONY 3000 - offline choir renderer.
// Turns one sung vocal into choir stems that follow its lyrics, melody and timing:
// every "singer" is a pitch/formant-shifted, humanised copy of the input made with a
// peak-locked phase vocoder and spectral-envelope (formant) control.
#pragma once
#include "DspUtils.h"
#include "PitchTracker.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace hm
{
//==============================================================================
enum class ChoirModel { gospel = 0, kids, gang, cyborg, numModels };
enum class MicModel   { condenser = 0, ribbon, dynamic, tube, laser, numMics };
// How the main "Choir" stem is arranged: everyone on the tune, tune + octave, or in parts.
enum class Voicing    { unison = 0, octaves, harmony, numVoicings };

const std::vector<std::string>& choirNames();
const std::vector<std::string>& micNames();
const std::vector<std::string>& voicingNames();

struct ChoirProfile
{
    int singers = 10;
    float formantBase = 1.0f;     // >1 = smaller vocal tract (kids), <1 = bigger
    float formantSpread = 0.05f;  // per-singer variation (fraction)
    bool  mixedSections = false;  // alternate lower/higher sections (gospel SATB colour)
    float detuneCents = 10.0f;    // slow pitch wander depth
    float vibratoCents = 0.0f;
    float vibratoHz = 5.2f;
    float timingMs = 18.0f;       // spread of per-singer timing
    float breath = 0.08f;         // aspiration noise level relative to envelope
    float grit = 0.0f;            // saturation amount
    float presenceDb = 0.0f;      // 3 kHz shout/presence
    float warmthDb = 0.0f;        // 220 Hz body
    float hpfHz = 70.0f;
    bool  hardTune = false;
    float ampWanderDb = 2.0f;     // slow per-singer loudness movement
    float tiltDb = 3.0f;          // per-singer brightness differences (+-)
    float vibratoRemoval = 0.6f;  // how much of the lead's own vibrato each singer drops
};

ChoirProfile profileFor (ChoirModel m);

//==============================================================================
struct RenderSettings
{
    ChoirModel choir = ChoirModel::gospel;
    MicModel mic = MicModel::condenser;
    bool octaveUp = false, octaveDown = false;
    int key = 0;
    Scale scale = Scale::major;
    bool thirdAbove = false, fifthAbove = false;
    bool below = false;           // add an octave-below copy of each harmony
    bool stereo = true;
    Voicing voicing = Voicing::harmony;
    uint64_t seed = 1984;
    int maxThreads = 0;           // 0 = hardware concurrency
};

struct Stem
{
    std::string name;
    std::vector<std::vector<float>> channels; // 1 (mono) or 2 (stereo)
    int numSamples() const { return channels.empty() ? 0 : (int) channels[0].size(); }
};

// One output part: a whole choir singing at a pitch relation to the input.
struct PartSpec
{
    std::string name;
    float octaveSemitones = 0.0f; // fixed transposition
    int harmonyDegrees = 0;       // 0 = melody, 2 = third above, 4 = fifth above
};

std::vector<PartSpec> partsFor (const RenderSettings& s);

using ProgressFn = std::function<void (float)>;

// Renders all stems for one mono input. Output stems are sample-aligned with the input.
// Returns empty if cancelled.
std::vector<Stem> renderChoir (const std::vector<float>& monoInput, double sampleRate,
                               const RenderSettings& settings,
                               ProgressFn progress = {}, const std::atomic<bool>* cancel = nullptr);

//==============================================================================
// Pitch/formant shifting voice synthesiser (exposed for tests).
struct SingerParams
{
    float formant = 1.0f;
    float detuneCents = 0.0f, vibratoCents = 0.0f, vibratoHz = 5.0f;
    float timingMs = 0.0f;        // constant offset (+ = late)
    float timingWanderMs = 0.0f;
    float breath = 0.0f;
    float gain = 1.0f, pan = 0.0f; // pan -1..1
    float ampWanderDb = 0.0f;     // slow loudness movement
    float tiltDb = 0.0f;          // brightness offset above ~2 kHz
    bool randomPhase = false;     // fresh random phase for every new partial (decorrelates singers)
    float vibratoRemoval = 0.0f;  // 0..1: subtract this much of the input's vibrato (own vibrato added instead)
    std::function<float (double)> partSemitones; // extra per-singer transposition (choir sections)
    uint64_t seed = 1;
};

// Pitch ratio for each analysis position is given by `semitonesAt(samplePos)`.
// Noisy unvoiced frames (consonants, breaths) are passed through unshifted so they stay crisp.
void renderSingers (const std::vector<float>& input, double sampleRate, const PitchTrack& track,
                    const std::function<float (double)>& semitonesAt,
                    const std::vector<SingerParams>& singers, bool stereo,
                    std::vector<std::vector<float>>& out,
                    const std::function<bool (float)>& progress = {});

//==============================================================================
// Mic model colouring (applied once per stem at render time).
void applyMicModel (Stem& stem, MicModel mic, double sampleRate);

//==============================================================================
// Review-screen effects: Lo-Cut, Brightness, Reverb. Realtime safe after prepare().
class PostFx
{
public:
    void prepare (double sampleRate);
    void reset();
    // loCutHz 20..800 (20 = off), brightness 0..1, reverb 0..1
    void setParams (float loCutHz, float brightness, float reverb);
    void process (float* left, float* right, int numSamples); // right may be nullptr (mono)
    double getSampleRate() const { return sr; }

private:
    void updateFilters();
    double sr = 48000.0;
    float loCut = 20.0f, bright = 0.0f, verb = 0.0f;
    float curLoCut = -1.0f, curBright = -1.0f;
    float smoothLoCut = 20.0f, smoothBright = 0.0f;
    float smoothVerb = 0.0f;
    Biquad hpf[2][2], shelf[2], air[2];
    Reverb reverb;
    Biquad verbHp[2];
};

// Convenience: offline-process a stem through PostFx (for drag-out/export). Adds a reverb tail.
Stem processStemOffline (const Stem& in, double sampleRate, float loCutHz, float brightness, float reverb);
} // namespace hm
