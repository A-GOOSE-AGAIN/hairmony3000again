// HAIRMONY 3000 - small DSP building blocks (biquads, reverb, deterministic noise, resampler).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hm
{
constexpr double kTwoPi = 6.28318530717958647692;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

//==============================================================================
// Deterministic RNG (xorshift) so renders are repeatable for a given seed.
class Rng
{
public:
    explicit Rng (uint64_t seed = 0x9E3779B97F4A7C15ull) : s (seed ? seed : 1) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float uniform() { return (float) ((next() >> 11) * (1.0 / 9007199254740992.0)); } // [0,1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }
private:
    uint64_t s;
};

inline uint64_t mixSeed (uint64_t a, uint64_t b)
{
    uint64_t z = a + 0x9E3779B97F4A7C15ull * (b + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

//==============================================================================
// Smooth bounded wander in [-1, 1]: a sum of slow sines with random phases.
class SmoothWander
{
public:
    SmoothWander() = default;
    SmoothWander (Rng& rng, float minHz, float maxHz)
    {
        for (auto& p : parts)
        {
            p.freq = minHz + (maxHz - minHz) * rng.uniform();
            p.phase = (float) (rng.uniform() * kTwoPi);
        }
    }
    float at (double seconds) const
    {
        double v = 0.0;
        for (auto& p : parts) v += std::sin (kTwoPi * p.freq * seconds + p.phase);
        return (float) (v / (double) parts.size());
    }
private:
    struct Part { float freq = 0.1f, phase = 0.0f; };
    std::array<Part, 3> parts {};
};

//==============================================================================
// RBJ biquad, transposed direct form II.
class Biquad
{
public:
    enum class Type { lowPass, highPass, peak, lowShelf, highShelf };

    void set (Type type, double sampleRate, double freq, double q, double gainDb = 0.0)
    {
        freq = std::clamp (freq, 5.0, sampleRate * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = kTwoPi * freq / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case Type::lowPass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::highPass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Type::lowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case Type::highShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s; break;
            }
        }
        c0 = (float) (b0 / a0); c1 = (float) (b1 / a0); c2 = (float) (b2 / a0);
        d1 = (float) (a1 / a0); d2 = (float) (a2 / a0);
    }

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = c0 * x + z1;
        z1 = c1 * x - d1 * y + z2;
        z2 = c2 * x - d2 * y;
        return y;
    }

    void process (float* data, int n) { for (int i = 0; i < n; ++i) data[i] = process (data[i]); }

private:
    float c0 = 1, c1 = 0, c2 = 0, d1 = 0, d2 = 0, z1 = 0, z2 = 0;
};

// Gentle odd-order saturation with unity gain for small signals.
// `drive` 0..1; `ref` is the level that should start to saturate.
inline float softSat (float x, float drive, float ref = 0.25f)
{
    if (drive <= 0.0f) return x;
    const float k = (1.0f + drive * 8.0f) / ref;
    return std::tanh (x * k) / k;
}

//==============================================================================
// Freeverb-style stereo reverb (public-domain algorithm by Jezar at Dreampoint).
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        static const int combT[8]    = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int allpassT[4] = { 556, 441, 341, 225 };
        const double scale = sampleRate / 44100.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const int spread = ch == 0 ? 0 : 23;
            for (size_t i = 0; i < 8; ++i)  combs[(size_t) ch][i].init ((int) ((combT[i] + spread) * scale));
            for (size_t i = 0; i < 4; ++i)  allpasses[(size_t) ch][i].init ((int) ((allpassT[i] + spread) * scale));
        }
        setRoom (roomSize, damping);
    }

    void setRoom (float size, float damp)
    {
        roomSize = size; damping = damp;
        const float fb = 0.7f + 0.28f * std::clamp (size, 0.0f, 1.0f);
        for (auto& side : combs) for (auto& c : side) { c.feedback = fb; c.damp = std::clamp (damp, 0.0f, 1.0f) * 0.4f; }
    }

    void reset()
    {
        for (auto& side : combs) for (auto& c : side) c.clear();
        for (auto& side : allpasses) for (auto& a : side) a.clear();
    }

    // Returns wet-only signal into wetL/wetR.
    void processSample (float inL, float inR, float& wetL, float& wetR)
    {
        const float input = (inL + inR) * 0.015f;
        float out[2] = { 0, 0 };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            for (auto& c : combs[ch]) out[ch] += c.process (input);
            for (auto& a : allpasses[ch]) out[ch] = a.process (out[ch]);
        }
        wetL = out[0]; wetR = out[1];
    }

private:
    struct Comb
    {
        std::vector<float> buf; int idx = 0; float store = 0, feedback = 0.84f, damp = 0.2f;
        void init (int size) { buf.assign ((size_t) std::max (1, size), 0.0f); idx = 0; store = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); store = 0; }
        float process (float in)
        {
            const float out = buf[(size_t) idx];
            store = out * (1.0f - damp) + store * damp;
            buf[(size_t) idx] = in + store * feedback;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> buf; int idx = 0;
        void init (int size) { buf.assign ((size_t) std::max (1, size), 0.0f); idx = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        float process (float in)
        {
            const float b = buf[(size_t) idx];
            buf[(size_t) idx] = in + b * 0.5f;
            if (++idx >= (int) buf.size()) idx = 0;
            return b - in;
        }
    };

    std::array<std::array<Comb, 8>, 2> combs;
    std::array<std::array<Allpass, 4>, 2> allpasses;
    float roomSize = 0.75f, damping = 0.4f;
};

// 4x-oversampled soft saturation (keeps the added harmonics from folding back as aliasing).
void saturateOversampled (std::vector<float>& x, float drive, float ref);

//==============================================================================
// Windowed-sinc resampler for importing files at a different sample rate.
std::vector<float> resample (const std::vector<float>& in, double fromRate, double toRate);
} // namespace hm
