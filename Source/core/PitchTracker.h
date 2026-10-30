// HAIRMONY 3000 - YIN pitch tracking and key/scale harmony logic.
#pragma once
#include <cmath>
#include <string>
#include <vector>

namespace hm
{
struct PitchTrack
{
    int hop = 256;              // samples between frames
    double sampleRate = 48000.0;
    std::vector<float> f0;      // Hz, 0 when unvoiced
    std::vector<float> confidence;

    // f0 at an arbitrary sample position (nearest voiced neighbours, linear in log-frequency).
    float f0At (double samplePos) const;
    bool voicedAt (double samplePos) const;
};

// YIN (de Cheveigne & Kawahara 2002) with FFT-based difference function.
PitchTrack trackPitch (const std::vector<float>& mono, double sampleRate,
                       float minHz = 60.0f, float maxHz = 1600.0f, int hop = 256);

//==============================================================================
enum class Scale
{
    major = 0, naturalMinor, harmonicMinor, melodicMinor, dorian, phrygian,
    lydian, mixolydian, majorPentatonic, minorPentatonic, blues, chromatic,
    numScales
};

const std::vector<std::string>& scaleNames();
const std::vector<std::string>& keyNames();
const std::vector<int>& scaleIntervals (Scale s);

inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float m)  { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

// Nearest note of the scale to a (fractional) MIDI pitch.
int snapToScale (float midi, int key, Scale scale);

// Semitone offset from scale note `note` to the note `degrees` scale steps above (negative = below).
int diatonicOffset (int note, int degrees, int key, Scale scale);

// Per-frame harmony offsets in semitones (smoothed), following the sung melody in key/scale.
// degrees = 2 for a third above, 4 for a fifth above.
std::vector<float> harmonyCurve (const PitchTrack& track, int degrees, int key, Scale scale);

// Per-frame vibrato / wobble of the singer in semitones (pitch minus its vibrato-free centre),
// clamped to +-1 and zero when unvoiced. Choir singers subtract part of it and add their own.
std::vector<float> vibratoCurve (const PitchTrack& track);

// Per-frame hard-tune correction in semitones (for the robotic choir).
std::vector<float> hardTuneCurve (const PitchTrack& track, int key, Scale scale);
} // namespace hm

