#include "PitchTracker.h"
#include "Fft.h"
#include <algorithm>
#include <complex>

namespace hm
{
//==============================================================================
float PitchTrack::f0At (double samplePos) const
{
    if (f0.empty() || hop <= 0) return 0.0f;
    const double idx = samplePos / (double) hop;
    const int last = (int) f0.size() - 1;
    const int i0 = std::clamp ((int) std::floor (idx), 0, last);
    const int i1 = std::min (i0 + 1, last);
    const float a = f0[(size_t) i0], b = f0[(size_t) i1];
    const float frac = (float) std::clamp (idx - (double) i0, 0.0, 1.0);
    if (a > 0.0f && b > 0.0f)
        return std::exp2 (std::log2 (a) * (1.0f - frac) + std::log2 (b) * frac);
    if (a > 0.0f && frac < 0.5f) return a;
    if (b > 0.0f && frac >= 0.5f) return b;
    return 0.0f;
}

bool PitchTrack::voicedAt (double samplePos) const { return f0At (samplePos) > 0.0f; }

//==============================================================================
static std::vector<float> median5 (const std::vector<float>& v)
{
    std::vector<float> out (v);
    const int n = (int) v.size();
    for (int i = 0; i < n; ++i)
    {
        if (v[(size_t) i] <= 0.0f) continue;
        float w[5]; int c = 0;
        for (int k = -2; k <= 2; ++k)
        {
            const int j = i + k;
            if (j >= 0 && j < n && v[(size_t) j] > 0.0f) w[c++] = v[(size_t) j];
        }
        for (int a = 1; a < c; ++a)            // tiny insertion sort (c <= 5)
            for (int b = a; b > 0 && w[b] < w[b - 1]; --b) std::swap (w[b], w[b - 1]);
        out[(size_t) i] = w[c / 2];
    }
    return out;
}

PitchTrack trackPitch (const std::vector<float>& x, double sampleRate, float minHz, float maxHz, int hop)
{
    PitchTrack track;
    track.hop = hop;
    track.sampleRate = sampleRate;
    const int len = (int) x.size();
    if (len == 0 || sampleRate <= 0.0) return track;

    const int tauMax = (int) std::ceil (sampleRate / (double) minHz);
    const int tauMin = std::max (2, (int) std::floor (sampleRate / (double) maxHz));
    const int W = tauMax;
    const int fftSize = nextPowerOfTwo (W + tauMax + 1);
    RealFft fft (fftSize);

    // Global loudness reference so quiet breaths/bleed are treated as unvoiced.
    double peakRms = 0.0;
    {
        const int blk = 1024;
        for (int s = 0; s < len; s += blk)
        {
            double e = 0.0; const int e1 = std::min (len, s + blk);
            for (int i = s; i < e1; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
            peakRms = std::max (peakRms, std::sqrt (e / std::max (1, e1 - s)));
        }
    }
    const double rmsGate = std::max (1.0e-4, peakRms * 0.0056); // ~ -45 dB below loudest block

    const int numFrames = len / hop + 1;
    track.f0.assign ((size_t) numFrames, 0.0f);
    track.confidence.assign ((size_t) numFrames, 0.0f);

    std::vector<float> a ((size_t) fftSize), b ((size_t) fftSize), corr ((size_t) fftSize);
    std::vector<std::complex<float>> A ((size_t) fftSize / 2 + 1), B ((size_t) fftSize / 2 + 1);
    std::vector<double> d ((size_t) tauMax + 1), dp ((size_t) tauMax + 1);
    std::vector<double> sq ((size_t) (W + tauMax + 1));

    auto sample = [&] (int i) -> float { return (i >= 0 && i < len) ? x[(size_t) i] : 0.0f; };

    for (int f = 0; f < numFrames; ++f)
    {
        const int start = f * hop - (W + tauMax) / 2;

        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        double frameEnergy = 0.0;
        for (int j = 0; j < W + tauMax; ++j)
        {
            const float s = sample (start + j);
            b[(size_t) j] = s;
            if (j < W) { a[(size_t) j] = s; frameEnergy += (double) s * s; }
        }
        const double rms = std::sqrt (frameEnergy / W);
        if (rms < rmsGate) continue;

        fft.forward (a.data(), A.data());
        fft.forward (b.data(), B.data());
        for (size_t k = 0; k < A.size(); ++k) A[k] = std::conj (A[k]) * B[k];
        fft.inverse (A.data(), corr.data());

        // Prefix sums of squares for the sliding energy term.
        sq[0] = 0.0;
        for (int j = 0; j < W + tauMax; ++j) sq[(size_t) j + 1] = sq[(size_t) j] + (double) b[(size_t) j] * b[(size_t) j];
        const double e0 = sq[(size_t) W];

        d[0] = 0.0; dp[0] = 1.0;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const double et = sq[(size_t) (tau + W)] - sq[(size_t) tau];
            d[(size_t) tau] = std::max (0.0, e0 + et - 2.0 * (double) corr[(size_t) tau]);
            running += d[(size_t) tau];
            dp[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
        }

        const double threshold = 0.15;
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (dp[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax && dp[(size_t) tau + 1] < dp[(size_t) tau]) ++tau;
                best = tau; break;
            }
        }
        if (best < 0)
        {
            double mn = 1.0e9;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (dp[(size_t) tau] < mn) { mn = dp[(size_t) tau]; best = tau; }
            if (mn > 0.35) best = -1;
        }
        // Sub-octave guard: breathy or jittery voices can make a multiple of the true period
        // look best. If a dip at best/2 (or best/3) is nearly as deep, the shorter period wins.
        for (int pass = 0; pass < 2 && best > 0; ++pass)
        {
            bool moved = false;
            for (int div : { 2, 3 })
            {
                const int c = best / div;
                if (c - 2 <= tauMin) continue;
                int m = c;
                for (int t = c - 2; t <= c + 2; ++t) if (dp[(size_t) t] < dp[(size_t) m]) m = t;
                const bool isDip = dp[(size_t) m] <= dp[(size_t) m - 1] && dp[(size_t) m] <= dp[(size_t) m + 1];
                if (isDip && dp[(size_t) m] < std::max (0.25, dp[(size_t) best] + 0.12)) { best = m; moved = true; break; }
            }
            if (! moved) break;
        }
        if (best <= tauMin || best >= tauMax - 1) continue;

        // Parabolic interpolation for sub-sample period.
        const double y0 = dp[(size_t) best - 1], y1 = dp[(size_t) best], y2 = dp[(size_t) best + 1];
        const double denom = y0 - 2.0 * y1 + y2;
        double refined = (double) best;
        if (std::abs (denom) > 1.0e-12) refined += std::clamp (0.5 * (y0 - y2) / denom, -1.0, 1.0);

        const float hz = (float) (sampleRate / refined);
        if (hz >= minHz * 0.9f && hz <= maxHz * 1.1f)
        {
            track.f0[(size_t) f] = hz;
            track.confidence[(size_t) f] = (float) std::clamp (1.0 - y1, 0.0, 1.0);
        }
    }

    // Remove voiced blips shorter than 3 frames, then median-smooth (also fixes lone octave jumps).
    for (int i = 0; i < numFrames;)
    {
        if (track.f0[(size_t) i] <= 0.0f) { ++i; continue; }
        int j = i;
        while (j < numFrames && track.f0[(size_t) j] > 0.0f) ++j;
        if (j - i < 3) for (int k = i; k < j; ++k) track.f0[(size_t) k] = 0.0f;
        i = j;
    }
    track.f0 = median5 (track.f0);

    // Octave consistency within each phrase: fold frames sitting an octave away from the
    // phrase's running median (~0.25 s each side) back into place. Two passes.
    for (int pass = 0; pass < 2; ++pass)
    for (int i = 0; i < numFrames;)
    {
        if (track.f0[(size_t) i] <= 0.0f) { ++i; continue; }
        int j = i;
        while (j < numFrames && track.f0[(size_t) j] > 0.0f) ++j;
        std::vector<float> logs;
        for (int k = i; k < j; ++k) logs.push_back (std::log2 (track.f0[(size_t) k]));
        std::vector<float> fixedLogs (logs);
        const int win = 48;
        for (int k = 0; k < (int) logs.size(); ++k)
        {
            std::vector<float> w;
            for (int q = std::max (0, k - win); q <= std::min ((int) logs.size() - 1, k + win); ++q) w.push_back (logs[(size_t) q]);
            std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
            const float med = w[w.size() / 2];
            const float dev = logs[(size_t) k] - med;
            if (std::abs (dev - 1.0f) < 0.15f) fixedLogs[(size_t) k] -= 1.0f;
            else if (std::abs (dev + 1.0f) < 0.15f) fixedLogs[(size_t) k] += 1.0f;
            else if (std::abs (dev - std::log2 (3.0f)) < 0.1f) fixedLogs[(size_t) k] -= std::log2 (3.0f);
            else if (std::abs (dev + std::log2 (3.0f)) < 0.1f) fixedLogs[(size_t) k] += std::log2 (3.0f);
        }
        for (int k = i; k < j; ++k) track.f0[(size_t) k] = std::exp2 (fixedLogs[(size_t) (k - i)]);
        i = j;
    }
    return track;
}

//==============================================================================
const std::vector<std::string>& scaleNames()
{
    static const std::vector<std::string> names {
        "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian",
        "Lydian", "Mixolydian", "Major Pentatonic", "Minor Pentatonic", "Blues", "Chromatic" };
    return names;
}

const std::vector<std::string>& keyNames()
{
    static const std::vector<std::string> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

const std::vector<int>& scaleIntervals (Scale s)
{
    static const std::vector<std::vector<int>> table {
        { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 2, 3, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 4, 7, 9 },
        { 0, 3, 5, 7, 10 }, { 0, 3, 5, 6, 7, 10 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    const int i = std::clamp ((int) s, 0, (int) table.size() - 1);
    return table[(size_t) i];
}

static int floorDiv (int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
static int posMod (int a, int b) { return ((a % b) + b) % b; }

static bool inScale (int note, int key, Scale scale)
{
    const int pc = posMod (note - key, 12);
    const auto& iv = scaleIntervals (scale);
    return std::find (iv.begin(), iv.end(), pc) != iv.end();
}

int snapToScale (float midi, int key, Scale scale)
{
    const int base = (int) std::floor (midi);
    int best = base; float bestDist = 1.0e9f;
    for (int n = base - 3; n <= base + 4; ++n)
    {
        if (! inScale (n, key, scale)) continue;
        const float dist = std::abs (midi - (float) n);
        if (dist < bestDist - 1.0e-6f) { bestDist = dist; best = n; }
    }
    return best;
}

int diatonicOffset (int note, int degrees, int key, Scale scale)
{
    if (scale == Scale::chromatic)
    {
        // Steps in a chromatic "scale" are meaningless for harmony - use major third / perfect fifth.
        if (degrees == 2) return 4;
        if (degrees == 4) return 7;
        if (degrees == -2) return -3;
        if (degrees == -4) return -7;
        return degrees;
    }

    const auto& iv = scaleIntervals (scale);
    const int size = (int) iv.size();
    const int rel = note - key;
    const int pc = posMod (rel, 12);
    const int octave = floorDiv (rel - pc, 12);

    int degree = 0;
    for (int i = 0; i < size; ++i) if (iv[(size_t) i] <= pc) degree = i; // nearest scale degree at/below

    const int target = degree + degrees;
    const int octShift = floorDiv (target, size);
    const int tDeg = posMod (target, size);
    const int targetNote = key + 12 * (octave + octShift) + iv[(size_t) tDeg];
    return targetNote - note;
}

//==============================================================================
// Zero-phase one-pole smoothing (forward + backward) - fine to do offline.
static void smoothZeroPhase (std::vector<float>& v, float coeff)
{
    if (v.empty()) return;
    float s = v.front();
    for (auto& x : v) { s = coeff * s + (1.0f - coeff) * x; x = s; }
    s = v.back();
    for (auto it = v.rbegin(); it != v.rend(); ++it) { s = coeff * s + (1.0f - coeff) * *it; *it = s; }
}

static void holdThroughGaps (std::vector<float>& v, const std::vector<bool>& valid)
{
    const int n = (int) v.size();
    int firstValid = -1;
    for (int i = 0; i < n; ++i) if (valid[(size_t) i]) { firstValid = i; break; }
    if (firstValid < 0) { std::fill (v.begin(), v.end(), 0.0f); return; }
    for (int i = 0; i < firstValid; ++i) v[(size_t) i] = v[(size_t) firstValid];
    float last = v[(size_t) firstValid];
    for (int i = firstValid; i < n; ++i)
    {
        if (valid[(size_t) i]) last = v[(size_t) i];
        else v[(size_t) i] = last;
    }
}

static float smoothingCoeff (const PitchTrack& track, float seconds)
{
    const double hopSec = (double) track.hop / std::max (1.0, track.sampleRate);
    return (float) std::exp (-hopSec / std::max (1.0e-4, (double) seconds));
}

static std::vector<float> vibratoFreeMidi (const PitchTrack& track)
{
    const int n = (int) track.f0.size();
    // Vibrato-free pitch: running median (~75 ms each side) of the MIDI pitch within each
    //    phrase. It ignores vibrato swings (so a held E doesn't flicker to F) but keeps real
    //    note changes sharp, unlike a low-pass.
    std::vector<float> smooth ((size_t) n, 0.0f);
    const int half = std::max (2, (int) std::lround (0.075 * track.sampleRate / std::max (1, track.hop)));
    for (int i = 0; i < n;)
    {
        if (track.f0[(size_t) i] <= 0.0f) { ++i; continue; }
        int j = i;
        while (j < n && track.f0[(size_t) j] > 0.0f) ++j;
        std::vector<float> w;
        for (int k = i; k < j; ++k)
        {
            w.clear();
            for (int q = std::max (i, k - half); q <= std::min (j - 1, k + half); ++q) w.push_back (hzToMidi (track.f0[(size_t) q]));
            std::nth_element (w.begin(), w.begin() + (long) w.size() / 2, w.end());
            smooth[(size_t) k] = w[w.size() / 2];
        }
        i = j;
    }

    return smooth;
}

std::vector<float> vibratoCurve (const PitchTrack& track)
{
    const auto centre = vibratoFreeMidi (track);
    std::vector<float> d (track.f0.size(), 0.0f);
    for (size_t i = 0; i < d.size(); ++i)
        if (track.f0[i] > 0.0f) d[i] = std::clamp (hzToMidi (track.f0[i]) - centre[i], -1.0f, 1.0f);
    smoothZeroPhase (d, smoothingCoeff (track, 0.01f));
    return d;
}

// Stable per-frame scale notes: snap each frame, then a 9-frame (non-causal) mode filter.
static std::vector<int> stableNotes (const PitchTrack& track, int key, Scale scale, std::vector<bool>& voiced)
{
    const int n = (int) track.f0.size();
    std::vector<int> raw ((size_t) n, 0);
    voiced.assign ((size_t) n, false);

    std::vector<float> smooth = vibratoFreeMidi (track);

    // 2) Hysteresis: only move to a new scale note once the smoothed pitch has clearly left the
    //    current one (more than 0.65 semitone away) for a few frames. New phrases start fresh.
    int cur = 0, pending = 0, pendingCount = 0;
    bool inPhrase = false;
    for (int i = 0; i < n; ++i)
    {
        if (track.f0[(size_t) i] <= 0.0f) { inPhrase = false; continue; }
        voiced[(size_t) i] = true;
        const float m = smooth[(size_t) i];
        const int cand = snapToScale (m, key, scale);
        if (! inPhrase) { cur = cand; pendingCount = 0; inPhrase = true; }
        else if (cand != cur && std::abs (m - (float) cur) > 0.65f)
        {
            if (cand == pending) ++pendingCount; else { pending = cand; pendingCount = 1; }
            if (pendingCount >= 4) { cur = cand; pendingCount = 0; }
        }
        else pendingCount = 0;
        raw[(size_t) i] = cur;
    }
    std::vector<int> out (raw);
    for (int i = 0; i < n; ++i)
    {
        if (! voiced[(size_t) i]) continue;
        int bestNote = raw[(size_t) i], bestCount = 0;
        for (int k = -4; k <= 4; ++k)
        {
            const int j = i + k;
            if (j < 0 || j >= n || ! voiced[(size_t) j]) continue;
            int count = 0;
            for (int m = -4; m <= 4; ++m)
            {
                const int q = i + m;
                if (q >= 0 && q < n && voiced[(size_t) q] && raw[(size_t) q] == raw[(size_t) j]) ++count;
            }
            if (count > bestCount) { bestCount = count; bestNote = raw[(size_t) j]; }
        }
        out[(size_t) i] = bestNote;
    }
    return out;
}


std::vector<float> harmonyCurve (const PitchTrack& track, int degrees, int key, Scale scale)
{
    std::vector<bool> voiced;
    const auto notes = stableNotes (track, key, scale, voiced);
    std::vector<float> off (notes.size(), 0.0f);
    for (size_t i = 0; i < notes.size(); ++i)
        if (voiced[i]) off[i] = (float) diatonicOffset (notes[i], degrees, key, scale);
    holdThroughGaps (off, voiced);
    smoothZeroPhase (off, smoothingCoeff (track, 0.012f));
    return off;
}

std::vector<float> hardTuneCurve (const PitchTrack& track, int key, Scale scale)
{
    std::vector<bool> voiced;
    const auto notes = stableNotes (track, key, scale, voiced);
    std::vector<float> corr (notes.size(), 0.0f);
    for (size_t i = 0; i < notes.size(); ++i)
        if (voiced[i]) corr[i] = (float) notes[i] - hzToMidi (track.f0[i]);
    holdThroughGaps (corr, voiced);
    smoothZeroPhase (corr, smoothingCoeff (track, 0.004f));
    return corr;
}
} // namespace hm
