// HAIRMONY 3000 - engine test suite (no JUCE required).
#include "../Source/core/ChoirEngine.h"
#include "../Source/core/Fft.h"
#include "../Source/core/PitchTracker.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>

using namespace hm;

static int g_failures = 0, g_checks = 0;
static std::string g_current;

#define CHECK(cond, ...) do { ++g_checks; if (! (cond)) { ++g_failures; \
    std::printf ("  FAIL [%s] %s:%d: %s -- ", g_current.c_str(), __FILE__, __LINE__, #cond); \
    std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double PI = 3.14159265358979323846;

//==============================================================================
// Test signals
static std::vector<float> sine (float hz, double sr, double seconds, float amp = 0.5f)
{
    std::vector<float> v ((size_t) (sr * seconds));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float) std::sin (2.0 * PI * hz * (double) i / sr);
    return v;
}

// Formant envelope for an /a/-like vowel (sum of resonance bumps), linear magnitude.
static float vowelEnv (float hz, float shift = 1.0f)
{
    const float F[3] = { 700.0f * shift, 1220.0f * shift, 2600.0f * shift };
    const float B[3] = { 110.0f * shift, 160.0f * shift, 260.0f * shift };
    const float G[3] = { 1.0f, 0.5f, 0.25f };
    float e = 0.02f;
    for (int i = 0; i < 3; ++i) { const float d = (hz - F[i]) / B[i]; e += G[i] / (1.0f + d * d); }
    return e;
}

struct Note { float midi; double seconds; };

// Additive "sung" vowel melody with harmonics shaped by vowelEnv. Optional gaps between notes.
static std::vector<float> sungMelody (const std::vector<Note>& notes, double sr, double gap = 0.0, float amp = 0.3f)
{
    std::vector<float> out;
    for (auto& n : notes)
    {
        const float f0 = midiToHz (n.midi);
        const size_t len = (size_t) (n.seconds * sr);
        const int numH = (int) (std::min (sr * 0.45, 9000.0) / f0);
        double norm = 0.0; for (int h = 1; h <= numH; ++h) norm += vowelEnv (f0 * h);
        const size_t fade = (size_t) (0.01 * sr);
        for (size_t i = 0; i < len; ++i)
        {
            const double t = (double) i / sr;
            double s = 0.0;
            for (int h = 1; h <= numH; ++h) s += vowelEnv (f0 * h) * std::sin (2.0 * PI * f0 * h * t + h * 0.7);
            float env = 1.0f;
            if (i < fade) env = (float) i / fade;
            if (len - i < fade) env = (float) (len - i) / fade;
            out.push_back ((float) (s / norm) * amp * 3.0f * env);
        }
        out.insert (out.end(), (size_t) (gap * sr), 0.0f);
    }
    return out;
}

static float rms (const std::vector<float>& v, size_t a = 0, size_t b = (size_t) -1)
{
    b = std::min (b, v.size()); if (b <= a) return 0.0f;
    double e = 0.0; for (size_t i = a; i < b; ++i) e += (double) v[i] * v[i];
    return (float) std::sqrt (e / (double) (b - a));
}

static bool allFinite (const Stem& s)
{
    for (auto& ch : s.channels) for (float v : ch) if (! std::isfinite (v)) return false;
    return true;
}

static std::vector<float> monoOf (const Stem& s)
{
    std::vector<float> m ((size_t) s.numSamples(), 0.0f);
    for (auto& ch : s.channels) for (size_t i = 0; i < ch.size(); ++i) m[i] += ch[i] / (float) s.channels.size();
    return m;
}

// Median detected f0 over a sample range.
static float medianF0 (const std::vector<float>& x, double sr, size_t a, size_t b)
{
    const auto tr = trackPitch (x, sr);
    std::vector<float> vals;
    for (size_t i = a / (size_t) tr.hop; i < std::min (tr.f0.size(), b / (size_t) tr.hop); ++i)
        if (tr.f0[i] > 0.0f) vals.push_back (tr.f0[i]);
    if (vals.empty()) return 0.0f;
    std::sort (vals.begin(), vals.end());
    return vals[vals.size() / 2];
}

// Frequency of the strongest smoothed-spectrum bump between 300 and 1100 Hz (first formant).
static float firstFormant (const std::vector<float>& x, double sr, size_t a, size_t b)
{
    const int N = 8192; RealFft fft (N);
    std::vector<double> avg (N / 2 + 1, 0.0);
    std::vector<float> fr (N); std::vector<std::complex<float>> X (N / 2 + 1);
    int frames = 0;
    for (size_t s = a; s + N <= b; s += N / 2)
    {
        for (int i = 0; i < N; ++i) fr[i] = x[s + i] * (float) (0.5 - 0.5 * std::cos (2 * PI * i / N));
        fft.forward (fr.data(), X.data());
        for (int k = 0; k <= N / 2; ++k) avg[k] += std::abs (X[k]);
        ++frames;
    }
    if (frames == 0) return 0.0f;
    // Smooth across harmonics with a wide moving average (~400 Hz).
    const double binHz = sr / N; const int w = (int) (200.0 / binHz);
    std::vector<double> sm (avg.size(), 0.0);
    for (int k = w; k + w < (int) avg.size(); ++k) { double s = 0; for (int j = -w; j <= w; ++j) s += avg[k + j]; sm[k] = s; }
    int best = 0; double bv = -1;
    for (int k = (int) (300 / binHz); k < (int) (1100 / binHz); ++k) if (sm[k] > bv) { bv = sm[k]; best = k; }
    return (float) (best * binHz);
}

// Spectral centroid of the long-term average spectrum (formant-shift indicator).
static float spectralCentroid (const std::vector<float>& x, double sr, size_t a, size_t b)
{
    const int N = 4096; RealFft fft (N);
    std::vector<float> fr (N); std::vector<std::complex<float>> X (N / 2 + 1);
    double num = 0, den = 0;
    for (size_t s = a; s + N <= b; s += N / 2)
    {
        for (int i = 0; i < N; ++i) fr[i] = x[s + i] * (float) (0.5 - 0.5 * std::cos (2 * PI * i / N));
        fft.forward (fr.data(), X.data());
        for (int k = 1; k <= N / 2; ++k) { const double m = std::abs (X[k]); const double hz = k * sr / N; if (hz < 6000) { num += m * hz; den += m; } }
    }
    return den > 0 ? (float) (num / den) : 0.0f;
}

static int bestLag (const std::vector<float>& a, const std::vector<float>& b, int maxLag, size_t from, size_t to)
{
    int best = 0; double bv = -1e30;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (size_t i = from; i < to; ++i)
        {
            const long j = (long) i + lag;
            if (j >= 0 && j < (long) b.size()) s += (double) a[i] * b[(size_t) j];
        }
        if (s > bv) { bv = s; best = lag; }
    }
    return best;
}

static float bandLevel (const std::vector<float>& x, double sr, float hz, size_t a, size_t b)
{
    // Goertzel-ish correlation with a sine/cosine at hz.
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) { const double ph = 2 * PI * hz * i / sr; re += x[i] * std::cos (ph); im += x[i] * std::sin (ph); }
    return (float) (2.0 * std::sqrt (re * re + im * im) / (double) (b - a));
}

static SingerParams plainSinger()
{
    SingerParams s; s.gain = 1.0f; s.pan = 0.0f; return s;
}

static std::vector<float> shiftOnce (const std::vector<float>& x, double sr, float semis, float formant, float timingMs = 0.0f)
{
    const auto tr = trackPitch (x, sr);
    auto sp = plainSinger(); sp.formant = formant; sp.timingMs = timingMs;
    std::vector<std::vector<float>> out;
    renderSingers (x, sr, tr, [semis] (double) { return semis; }, { sp }, false, out);
    return out[0];
}

//==============================================================================
static void testFft()
{
    g_current = "fft";
    Rng r (3);
    for (int n : { 4, 8, 64, 512, 2048, 8192 })
    {
        RealFft fft (n);
        std::vector<float> x (n), y (n);
        for (auto& v : x) v = r.bipolar();
        std::vector<std::complex<float>> X (n / 2 + 1);
        fft.forward (x.data(), X.data());
        fft.inverse (X.data(), y.data());
        float err = 0; for (int i = 0; i < n; ++i) err = std::max (err, std::abs (x[i] - y[i]));
        CHECK (err < 2e-5f, "roundtrip n=%d err=%g", n, err);

        if (n <= 512)
        {
            float derr = 0;
            for (int k = 0; k <= n / 2; ++k)
            {
                std::complex<double> s = 0;
                for (int i = 0; i < n; ++i) s += (double) x[i] * std::polar (1.0, -2 * PI * k * i / n);
                derr = std::max (derr, (float) std::abs (s - std::complex<double> (X[k])));
            }
            CHECK (derr < 1e-3f, "naive DFT match n=%d err=%g", n, derr);
        }
    }
    bool threw = false;
    try { RealFft bad (100); } catch (...) { threw = true; }
    CHECK (threw, "non power of two must throw");
}

static void testResample()
{
    g_current = "resample";
    const auto x = sine (1000.0f, 44100.0, 1.0);
    const auto y = resample (x, 44100.0, 48000.0);
    CHECK (std::abs ((int) y.size() - 48000) <= 1, "length %zu", y.size());
    CHECK (std::abs (medianF0 (y, 48000.0, 4800, 43200) - 1000.0f) < 3.0f, "freq kept");
    CHECK (std::abs (rms (y, 4800, 43200) - rms (x, 4410, 39690)) < 0.01f, "level kept");
    const auto z = resample (sine (15000.0f, 48000.0, 0.5), 48000.0, 22050.0);
    CHECK (rms (z, 1000, z.size() - 1000) < 0.02f, "content above new Nyquist is filtered (rms %g)", rms (z, 1000, z.size() - 1000));
    CHECK (resample ({}, 44100, 48000).empty(), "empty in, empty out");
    CHECK (resample (x, 48000, 48000).size() == x.size(), "same rate passthrough");
}

static void testPitch()
{
    g_current = "pitch";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (float hz : { 65.0f, 82.4f, 110.0f, 220.0f, 440.0f, 880.0f, 1046.5f })
        {
            const auto x = sine (hz, sr, 0.6);
            const float f = medianF0 (x, sr, (size_t) (0.1 * sr), (size_t) (0.5 * sr));
            CHECK (std::abs (f - hz) / hz < 0.005f, "sr=%g hz=%g got %g", sr, hz, f);
        }

    const auto tr0 = trackPitch (std::vector<float> (48000, 0.0f), 48000.0);
    bool anyVoiced = false; for (float f : tr0.f0) anyVoiced |= f > 0.0f;
    CHECK (! anyVoiced, "silence is unvoiced");

    Rng r (5); std::vector<float> noise (48000); for (auto& v : noise) v = r.bipolar() * 0.3f;
    const auto trn = trackPitch (noise, 48000.0);
    int voiced = 0; for (float f : trn.f0) voiced += f > 0.0f;
    CHECK (voiced < (int) trn.f0.size() / 10, "white noise mostly unvoiced (%d/%zu)", voiced, trn.f0.size());

    const auto vowel = sungMelody ({ { 51.0f, 0.8 } }, 48000.0); // D#3 ~155.6 Hz
    const float fv = medianF0 (vowel, 48000.0, 4800, 33600);
    CHECK (std::abs (fv - midiToHz (51.0f)) < 1.0f, "vowel f0 %g", fv);

    const auto trv = trackPitch (vowel, 48000.0);
    CHECK (std::abs (trv.f0At (19200.0) - midiToHz (51.0f)) < 1.5f, "f0At interpolation");
    CHECK (trv.f0At (-1000.0) >= 0.0f && trv.f0At (1e9) >= 0.0f, "f0At out of range is safe");
    CHECK (trackPitch ({}, 48000.0).f0.empty(), "empty input");
}

static void testScales()
{
    g_current = "scales";
    CHECK (snapToScale (60.8f, 0, Scale::major) == 60, "60.8 snaps to C in C major");
    CHECK (snapToScale (61.6f, 0, Scale::major) == 62, "61.6 -> D");
    CHECK (snapToScale (64.4f, 0, Scale::major) == 64, "E stays");
    CHECK (snapToScale (66.0f, 0, Scale::major) == 65 || snapToScale (66.0f, 0, Scale::major) == 67, "F# goes to neighbour");
    CHECK (snapToScale (70.0f, 0, Scale::chromatic) == 70, "chromatic keeps everything");

    // C major thirds/fifths (diatonic)
    const std::map<int, int> thirdsC { { 60, 4 }, { 62, 3 }, { 64, 3 }, { 65, 4 }, { 67, 4 }, { 69, 3 }, { 71, 3 } };
    for (auto [n, o] : thirdsC) CHECK (diatonicOffset (n, 2, 0, Scale::major) == o, "C major third above %d -> %d got %d", n, o, diatonicOffset (n, 2, 0, Scale::major));
    const std::map<int, int> fifthsC { { 60, 7 }, { 62, 7 }, { 64, 7 }, { 65, 7 }, { 67, 7 }, { 69, 7 }, { 71, 6 } };
    for (auto [n, o] : fifthsC) CHECK (diatonicOffset (n, 4, 0, Scale::major) == o, "C major fifth above %d", n);
    CHECK (diatonicOffset (57, 2, 9, Scale::naturalMinor) == 3, "A minor: A -> C");
    CHECK (diatonicOffset (64, 2, 9, Scale::harmonicMinor) == 4, "A harmonic minor: E -> G#");
    CHECK (diatonicOffset (60, 2, 0, Scale::chromatic) == 4 && diatonicOffset (60, 4, 0, Scale::chromatic) == 7, "chromatic fallback");
    CHECK (diatonicOffset (48, 2, 0, Scale::major) == 4 && diatonicOffset (84, 2, 0, Scale::major) == 4, "octave independent");
    CHECK (diatonicOffset (60, -2, 0, Scale::major) == -3, "third below C is A");

    // Every key/scale: targets land in scale and intervals are musically sane.
    for (int key = 0; key < 12; ++key)
        for (int sc = 0; sc < (int) Scale::chromatic; ++sc)
        {
            const auto scale = (Scale) sc;
            for (int note = 48; note < 72; ++note)
            {
                const int n = snapToScale ((float) note, key, scale);
                const int o3 = diatonicOffset (n, 2, key, scale), o5 = diatonicOffset (n, 4, key, scale);
                CHECK (o3 >= 2 && o3 <= 6, "third range key %d scale %d note %d -> %d", key, sc, n, o3);
                CHECK (o5 >= 5 && o5 <= 10, "fifth range key %d scale %d note %d -> %d", key, sc, n, o5);
                CHECK (snapToScale ((float) (n + o3), key, scale) == n + o3, "third target in scale");
            }
        }
    CHECK (scaleNames().size() == (size_t) Scale::numScales, "scale names");
    CHECK (keyNames().size() == 12, "key names");
}

static void testHarmonyCurve()
{
    g_current = "harmony curve";
    const double sr = 48000.0;
    // C4 D4 E4 F4 G4 A4 B4 in C major
    std::vector<Note> notes; for (int m : { 60, 62, 64, 65, 67, 69, 71 }) notes.push_back ({ (float) m, 0.4 });
    const auto x = sungMelody (notes, sr);
    const auto tr = trackPitch (x, sr);
    const auto third = harmonyCurve (tr, 2, 0, Scale::major);
    const int expect[7] = { 4, 3, 3, 4, 4, 3, 3 };
    for (int i = 0; i < 7; ++i)
    {
        const size_t mid = (size_t) ((i * 0.4 + 0.2) * sr) / (size_t) tr.hop;
        CHECK (std::abs (third[mid] - (float) expect[i]) < 0.05f, "note %d offset %g expected %d", i, third[mid], expect[i]);
    }
    const auto tune = hardTuneCurve (trackPitch (sine (midiToHz (60.3f), sr, 0.5), sr), 0, Scale::major);
    CHECK (std::abs (tune[tune.size() / 2] + 0.3f) < 0.05f, "hard tune pulls 60.3 to 60 (%g)", tune[tune.size() / 2]);
}

static void testShifterPitch()
{
    g_current = "shifter pitch";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const auto x = sungMelody ({ { 55.0f, 1.2 } }, sr); // G3 196 Hz
        for (float s : { -12.0f, -7.0f, -5.0f, 0.0f, 3.0f, 4.0f, 7.0f, 12.0f })
        {
            const auto y = shiftOnce (x, sr, s, 1.0f);
            const float expect = midiToHz (55.0f + s);
            const float got = medianF0 (y, sr, (size_t) (0.2 * sr), (size_t) (1.0 * sr));
            CHECK (std::abs (got - expect) / expect < 0.012f, "sr %g shift %g: expected %g got %g", sr, s, expect, got);
            const float lvl = gainToDb (rms (y, (size_t) (0.2 * sr), (size_t) (1.0 * sr)) / rms (x, (size_t) (0.2 * sr), (size_t) (1.0 * sr)));
            CHECK (std::abs (lvl) < 6.0f, "sr %g shift %g level change %g dB", sr, s, lvl);
        }
    }
    // Pure sine through extreme shifts stays finite and at the right pitch.
    const auto sn = sine (300.0f, 48000.0, 1.0);
    const auto up = shiftOnce (sn, 48000.0, 24.0f, 1.0f);
    CHECK (std::abs (medianF0 (up, 48000.0, 9600, 38400) - 1200.0f) < 15.0f, "two octaves up");
}

static void testFormant()
{
    g_current = "formant";
    const double sr = 48000.0;
    const auto x = sungMelody ({ { 48.0f, 1.5 } }, sr); // C3 130.8 Hz, many harmonics to read the envelope
    const size_t a = (size_t) (0.25 * sr), b = (size_t) (1.25 * sr);
    const float c0 = spectralCentroid (x, sr, a, b);
    const float f1 = firstFormant (x, sr, a, b);

    const auto same = shiftOnce (x, sr, 0.0f, 1.0f);
    CHECK (std::abs (firstFormant (same, sr, a, b) - f1) < 80.0f, "neutral keeps F1 (%g vs %g)", firstFormant (same, sr, a, b), f1);

    const auto upF = shiftOnce (x, sr, 0.0f, 1.25f);
    const float cUp = spectralCentroid (upF, sr, a, b);
    CHECK (cUp > c0 * 1.12f, "formant up raises centroid (%g -> %g)", c0, cUp);
    CHECK (std::abs (medianF0 (upF, sr, a, b) - midiToHz (48.0f)) < 2.0f, "formant shift keeps pitch");

    const auto dnF = shiftOnce (x, sr, 0.0f, 0.8f);
    CHECK (spectralCentroid (dnF, sr, a, b) < c0 * 0.92f, "formant down lowers centroid (%g -> %g)", c0, spectralCentroid (dnF, sr, a, b));

    // Pitch shifting with formants preserved: each new harmonic should sit on the original
    // vowel envelope. Compare harmonic levels (300 Hz - 3 kHz) after removing the overall offset.
    for (float semis : { 5.0f, 7.0f, -5.0f, 12.0f })
    {
        const auto sh = shiftOnce (x, sr, semis, 1.0f);
        const float f0o = midiToHz (48.0f + semis);
        std::vector<float> err;
        for (int h = 1; h * f0o < 3000.0f; ++h)
        {
            const float hz = h * f0o; if (hz < 300.0f) continue;
            err.push_back (gainToDb (bandLevel (sh, sr, hz, a, b)) - gainToDb (vowelEnv (hz)));
        }
        float mean = 0; for (float e : err) mean += e; mean /= (float) err.size();
        float mae = 0; for (float e : err) mae += std::abs (e - mean); mae /= (float) err.size();
        CHECK (mae < 3.0f, "formants preserved at %+g st: mean abs error %g dB", semis, mae);

        // ...and a naive (formant-moving) shift would be far worse, proving the test has teeth.
        if (semis == 7.0f)
        {
            float naive = 0;
            int c = 0;
            for (int h = 1; h * f0o < 3000.0f; ++h)
            {
                const float hz = h * f0o; if (hz < 300.0f) continue;
                naive += std::abs (gainToDb (vowelEnv (hz / 1.4983f)) - gainToDb (vowelEnv (hz)) - 0.0f); ++c;
            }
            CHECK (naive / c > 2.0f * mae, "naive shift error %g vs ours %g", naive / c, mae);
        }
    }
    (void) f1;
}

static void testAlignment()
{
    g_current = "alignment";
    const double sr = 48000.0;
    const auto x = sungMelody ({ { 57.0f, 0.3 }, { 60.0f, 0.3 }, { 64.0f, 0.3 } }, sr, 0.15);
    // A phase vocoder keeps each partial's frequency, not its absolute phase, so waveform
    // cross-correlation can lock onto a neighbouring period; compare loudness envelopes instead.
    auto envelope = [] (const std::vector<float>& v)
    {
        std::vector<float> e (v.size() / 64);
        for (size_t i = 0; i < e.size(); ++i) e[i] = rms (v, i * 64, i * 64 + 256);
        return e;
    };
    const auto y = shiftOnce (x, sr, 0.0f, 1.0f);
    CHECK (y.size() == x.size(), "same length");
    const auto ex = envelope (x);
    const int lag = bestLag (ex, envelope (y), 40, 10, ex.size() - 10) * 64;
    CHECK (std::abs (lag) <= 64, "no latency: envelope lag %d samples", lag);

    // Onset envelope alignment of a pitch-shifted copy.
    const auto up = shiftOnce (x, sr, 5.0f, 1.0f);
    auto onset = [&] (const std::vector<float>& v, size_t from)
    {
        const float thr = rms (v, 0, v.size()) * 0.5f;
        for (size_t i = from; i < v.size(); i += 32) if (rms (v, i, i + 256) > thr) return (long) i;
        return -1L;
    };
    const long o1 = onset (x, (size_t) (0.62 * sr)), o2 = onset (up, (size_t) (0.62 * sr));
    CHECK (o1 > 0 && std::abs (o1 - o2) < (long) (0.012 * sr), "shifted onset within 12 ms (%ld vs %ld)", o1, o2);

    // Same processing with a 20 ms singer offset must be the identical signal, 960 samples later.
    const auto late = shiftOnce (x, sr, 0.0f, 1.0f, 20.0f);
    const int lag2 = bestLag (y, late, 1100, 4800, x.size() - 4800);
    CHECK (lag2 == 960, "20 ms timing offset -> lag %d", lag2);
    const auto early = shiftOnce (x, sr, 0.0f, 1.0f, -15.0f);
    CHECK (bestLag (y, early, 1100, 4800, x.size() - 4800) == -720, "negative offset (singer early)");
}

static void testRenderChoir()
{
    g_current = "renderChoir";
    const double sr = 48000.0;
    const auto x = sungMelody ({ { 60.0f, 0.5 }, { 64.0f, 0.5 }, { 67.0f, 0.5 }, { 65.0f, 0.5 } }, sr, 0.05);
    const float inRms = rms (x);

    for (int m = 0; m < (int) ChoirModel::numModels; ++m)
        for (bool stereo : { true, false })
        {
            RenderSettings s; s.choir = (ChoirModel) m; s.stereo = stereo;
            s.octaveUp = true; s.octaveDown = true; s.thirdAbove = true; s.fifthAbove = true; s.below = true;
            s.mic = (MicModel) (m % (int) MicModel::numMics);
            float lastProgress = -1.0f; bool monotonic = true;
            const auto stems = renderChoir (x, sr, s, [&] (float p) { if (p + 1e-6f < lastProgress) monotonic = false; lastProgress = std::max (lastProgress, p); });
            CHECK (stems.size() == 7, "model %d: 7 stems, got %zu", m, stems.size());
            CHECK (lastProgress == 1.0f, "progress reaches 1");
            CHECK (monotonic, "progress never goes backwards");
            for (auto& st : stems)
            {
                CHECK (st.channels.size() == (stereo ? 2u : 1u), "channels for %s", st.name.c_str());
                CHECK (st.numSamples() == (int) x.size(), "length for %s", st.name.c_str());
                CHECK (allFinite (st), "finite %s", st.name.c_str());
                float pk = 0; for (auto& ch : st.channels) for (float v : ch) pk = std::max (pk, std::abs (v));
                CHECK (pk <= 0.951f, "no clipping %s (%g)", st.name.c_str(), pk);
                const float lv = gainToDb (rms (monoOf (st)) / inRms);
                CHECK (lv > -18.0f && lv < 6.0f, "model %d %s level %g dB", m, st.name.c_str(), lv);
                if (stereo && m != (int) ChoirModel::cyborg)
                {
                    double num = 0, dl = 0, dr = 0;
                    for (int i = 0; i < st.numSamples(); ++i) { num += st.channels[0][i] * st.channels[1][i]; dl += st.channels[0][i] * st.channels[0][i]; dr += st.channels[1][i] * st.channels[1][i]; }
                    const double corr = num / std::sqrt (dl * dr + 1e-20);
                    CHECK (corr < 0.97, "stereo width %s corr %g", st.name.c_str(), corr);
                }
            }
        }

    RenderSettings basic;
    auto names = [] (const RenderSettings& s) { std::string r; for (auto& p : partsFor (s)) r += p.name + "|"; return r; };
    CHECK (names (basic) == "Choir|", "default: only choir");
    basic.below = true; CHECK (names (basic) == "Choir|", "below alone adds nothing (no harmonies)");
    basic.thirdAbove = true; CHECK (names (basic) == "Choir|3rd Above|3rd Above (Low)|", "third + below");
    basic.below = false; basic.fifthAbove = true; basic.octaveUp = true;
    CHECK (names (basic) == "Choir|Octave Up|3rd Above|5th Above|", "order");
}

static void testHarmonyStems()
{
    g_current = "harmony stems";
    const double sr = 48000.0;
    const std::vector<int> mel { 60, 64, 67, 72 }; // C E G C in C major
    std::vector<Note> notes; for (int m : mel) notes.push_back ({ (float) m, 0.6 });
    const auto x = sungMelody (notes, sr, 0.05);

    RenderSettings s; s.choir = ChoirModel::cyborg; s.stereo = false; // cyborg: no detune => clean pitch readings
    s.voicing = Voicing::unison;                                       // the main stem on the tune for this check
    s.thirdAbove = true; s.fifthAbove = true; s.octaveUp = true; s.octaveDown = true;
    s.key = 0; s.scale = Scale::major;
    const auto stems = renderChoir (x, sr, s);
    std::map<std::string, const Stem*> byName; for (auto& st : stems) byName[st.name] = &st;

    const int thirds[4] = { 64, 67, 71, 76 }, fifths[4] = { 67, 71, 74, 79 };
    for (int i = 0; i < 4; ++i)
    {
        const size_t a = (size_t) ((i * 0.65 + 0.15) * sr), b = (size_t) ((i * 0.65 + 0.5) * sr);
        auto check = [&] (const char* nm, float expMidi)
        {
            const float f = medianF0 (monoOf (*byName[nm]), sr, a, b);
            CHECK (f > 0.0f && std::abs (hzToMidi (f) - expMidi) < 0.3f, "%s note %d: expected midi %g got %g", nm, i, expMidi, f > 0 ? hzToMidi (f) : -1.0f);
        };
        check ("Choir", (float) mel[i]);
        check ("3rd Above", (float) thirds[i]);
        check ("5th Above", (float) fifths[i]);
        check ("Octave Up", (float) mel[i] + 12);
        check ("Octave Down", (float) mel[i] - 12);
    }

    // Gospel (humanised) melody stem stays on pitch on average.
    RenderSettings g; g.choir = ChoirModel::gospel; g.stereo = true; g.voicing = Voicing::unison;
    const auto gs = renderChoir (x, sr, g);
    const float f = medianF0 (monoOf (gs[0]), sr, (size_t) (0.15 * sr), (size_t) (0.5 * sr));
    CHECK (std::abs (hzToMidi (f) - 60.0f) < 0.3f, "gospel choir on pitch (%g)", hzToMidi (f));

    // Kids choir is brighter (higher formants) than gang vocals.
    RenderSettings k = g; k.choir = ChoirModel::kids; k.mic = MicModel::condenser;
    RenderSettings gg = g; gg.choir = ChoirModel::gang; gg.mic = MicModel::condenser;
    const auto ks = renderChoir (x, sr, k), gangS = renderChoir (x, sr, gg);
    const float ck = spectralCentroid (monoOf (ks[0]), sr, 0, x.size()), cg = spectralCentroid (monoOf (gangS[0]), sr, 0, x.size());
    CHECK (ck > cg * 1.1f, "kids brighter than gang (%g vs %g)", ck, cg);
}

static void testEdgeCases()
{
    g_current = "edge cases";
    const double sr = 48000.0;
    RenderSettings s; s.thirdAbove = true; s.octaveUp = true;
    CHECK (renderChoir ({}, sr, s).empty(), "empty input");
    CHECK (renderChoir (std::vector<float> (1000, 0.1f), 0.0, s).empty(), "bad sample rate");

    const auto tiny = renderChoir (std::vector<float> (100, 0.2f), sr, s);
    CHECK (tiny.size() == 3 && tiny[0].numSamples() == 100 && allFinite (tiny[0]), "100-sample input");

    const auto quiet = renderChoir (std::vector<float> ((size_t) sr * 2, 0.0f), sr, s);
    bool silent = true; for (auto& st : quiet) { silent &= allFinite (st); for (auto& ch : st.channels) for (float v : ch) silent &= std::abs (v) < 1e-6f; }
    CHECK (silent, "silence in -> silence out");

    Rng r (11); std::vector<float> loud ((size_t) sr * 2);
    for (size_t i = 0; i < loud.size(); ++i) loud[i] = (i / 120) % 2 ? 1.0f : -1.0f; // full-scale square
    for (size_t i = 0; i < loud.size() / 2; ++i) loud[i] = std::clamp (r.bipolar() * 3.0f, -1.0f, 1.0f);
    for (int m = 0; m < (int) ChoirModel::numModels; ++m)
    {
        s.choir = (ChoirModel) m;
        const auto st = renderChoir (loud, sr, s);
        bool ok = true; float pk = 0;
        for (auto& x : st) { ok &= allFinite (x); for (auto& ch : x.channels) for (float v : ch) pk = std::max (pk, std::abs (v)); }
        CHECK (ok && pk <= 0.951f, "hot input model %d finite and limited (%g)", m, pk);
    }

    std::vector<float> dc ((size_t) sr, 0.5f);
    const auto dcs = renderChoir (dc, sr, RenderSettings {});
    CHECK (allFinite (dcs[0]) && std::abs (rms (monoOf (dcs[0]), 12000, 36000)) < 0.05f, "DC is removed");

    // Determinism + seeds
    const auto x = sungMelody ({ { 62.0f, 0.5 } }, sr);
    RenderSettings d; d.seed = 7;
    const auto a1 = renderChoir (x, sr, d), a2 = renderChoir (x, sr, d);
    d.seed = 8; const auto a3 = renderChoir (x, sr, d);
    CHECK (a1[0].channels == a2[0].channels, "same seed => identical");
    CHECK (a1[0].channels != a3[0].channels, "different seed => different");
    RenderSettings one = d; one.maxThreads = 1; one.seed = 7; one.thirdAbove = true;
    RenderSettings many = one; many.maxThreads = 4;
    CHECK (renderChoir (x, sr, one)[1].channels == renderChoir (x, sr, many)[1].channels, "thread count doesn't change output");

    // Cancel
    std::atomic<bool> cancel { true };
    const auto t0 = std::chrono::steady_clock::now();
    const auto c = renderChoir (sungMelody ({ { 60.0f, 5.0 } }, sr), sr, s, {}, &cancel);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    CHECK (c.empty(), "cancelled render returns nothing");
    CHECK (ms < 2000.0, "cancel is quick (%g ms)", ms);
}

static void testPostFx()
{
    g_current = "postfx";
    const double sr = 48000.0;
    const size_t n = (size_t) sr;
    auto run = [&] (std::vector<float> l, float lc, float br, float rv)
    {
        PostFx fx; fx.prepare (sr); fx.setParams (lc, br, rv); fx.reset();
        std::vector<float> r = l;
        for (size_t s = 0; s < l.size(); s += 512) { const int b = (int) std::min<size_t> (512, l.size() - s); fx.process (l.data() + s, r.data() + s, b); }
        return std::make_pair (l, r);
    };

    const auto low = sine (50.0f, sr, 1.0), mid = sine (2000.0f, sr, 1.0), high = sine (10000.0f, sr, 1.0);
    const auto [neutral, nr] = run (mid, 20.0f, 0.0f, 0.0f);
    float maxDiff = 0; for (size_t i = 0; i < n; ++i) maxDiff = std::max (maxDiff, std::abs (neutral[i] - mid[i]));
    CHECK (maxDiff < 1e-6f, "all-zero settings are transparent (%g)", maxDiff);

    const auto lc = run (low, 200.0f, 0.0f, 0.0f).first;
    CHECK (gainToDb (bandLevel (lc, sr, 50, 24000, n) / bandLevel (low, sr, 50, 24000, n)) < -25.0f, "lo-cut kills 50 Hz");
    const auto lcm = run (mid, 200.0f, 0.0f, 0.0f).first;
    CHECK (std::abs (gainToDb (bandLevel (lcm, sr, 2000, 24000, n) / bandLevel (mid, sr, 2000, 24000, n))) < 0.5f, "lo-cut passes 2 kHz");

    const auto br = run (high, 20.0f, 1.0f, 0.0f).first;
    CHECK (gainToDb (bandLevel (br, sr, 10000, 24000, n) / bandLevel (high, sr, 10000, 24000, n)) > 8.0f, "brightness boosts 10 kHz");
    const auto brl = run (sine (200.0f, sr, 1.0), 20.0f, 1.0f, 0.0f).first;
    CHECK (std::abs (gainToDb (bandLevel (brl, sr, 200, 24000, n) / 0.5f)) < 0.5f, "brightness leaves lows");

    std::vector<float> burst (n * 2, 0.0f); for (size_t i = 0; i < n / 4; ++i) burst[i] = mid[i];
    const auto [rvL, rvR] = run (burst, 20.0f, 0.0f, 1.0f);
    CHECK (rms (rvL, n / 4 + 4800, n) > 0.005f, "reverb leaves a tail (%g)", rms (rvL, n / 4 + 4800, n));
    double diff = 0; for (size_t i = 0; i < n; ++i) diff += std::abs (rvL[i] - rvR[i]);
    CHECK (diff > 1.0, "reverb is stereo");

    // Random automation: stays finite and bounded.
    // Reference: the loudest static setting on the same noise.
    float refPk = 0;
    {
        PostFx fx; fx.prepare (44100.0); fx.setParams (20.0f, 1.0f, 1.0f); fx.reset();
        Rng r (2); std::vector<float> l (256), rr (256);
        for (int blk = 0; blk < 2000; ++blk)
        {
            for (int i = 0; i < 256; ++i) { l[i] = r.bipolar() * 0.5f; rr[i] = r.bipolar() * 0.5f; }
            fx.process (l.data(), rr.data(), 256);
            for (int i = 0; i < 256; ++i) refPk = std::max (refPk, std::abs (l[i]));
        }
    }
    PostFx fx; fx.prepare (44100.0);
    Rng r (2); std::vector<float> l (256), rr (256); bool ok = true; float pk = 0;
    for (int blk = 0; blk < 2000; ++blk)
    {
        fx.setParams (20.0f + 780.0f * r.uniform(), r.uniform(), r.uniform());
        for (int i = 0; i < 256; ++i) { l[i] = r.bipolar() * 0.5f; rr[i] = r.bipolar() * 0.5f; }
        fx.process (l.data(), rr.data(), 256);
        for (int i = 0; i < 256; ++i) { ok &= std::isfinite (l[i]) && std::isfinite (rr[i]); pk = std::max (pk, std::abs (l[i])); }
        float mono[7] = { 0.1f, -0.2f, 0.3f, 0.0f, 0.5f, -0.5f, 0.2f };
        fx.process (mono, nullptr, 7); // mono path between stereo blocks
        for (float m : mono) ok &= std::isfinite (m);
    }
    CHECK (ok && pk < refPk * 1.5f, "random automation stable (peak %g, static max-settings peak %g)", pk, refPk);

    Stem st; st.name = "x"; st.channels = { burst, burst };
    const auto wet = processStemOffline (st, sr, 20.0f, 0.0f, 0.6f);
    CHECK (wet.numSamples() > st.numSamples() && wet.numSamples() <= st.numSamples() + (int) (3.5 * sr), "offline reverb tail %d", wet.numSamples());
    const auto dry = processStemOffline (st, sr, 100.0f, 0.3f, 0.0f);
    CHECK (dry.numSamples() == st.numSamples() && dry.name == "x", "no tail without reverb");
    CHECK (processStemOffline (Stem {}, sr, 20, 0, 1).channels.empty(), "empty stem");
}

static void testMics()
{
    g_current = "mics";
    const double sr = 48000.0;
    const auto x = sungMelody ({ { 57.0f, 1.0 } }, sr);
    CHECK (micNames().size() == (size_t) MicModel::numMics && choirNames().size() == (size_t) ChoirModel::numModels, "names");
    std::vector<float> centroids;
    for (int m = 0; m < (int) MicModel::numMics; ++m)
    {
        Stem s; s.channels = { x };
        applyMicModel (s, (MicModel) m, sr);
        CHECK (allFinite (s), "mic %d finite", m);
        const float db = gainToDb (rms (s.channels[0]) / rms (x));
        CHECK (std::abs (db) < 9.0f, "mic %d level %g dB", m, db);
        centroids.push_back (spectralCentroid (s.channels[0], sr, 0, x.size()));
    }
    CHECK (centroids[(int) MicModel::ribbon] < centroids[(int) MicModel::condenser], "ribbon darker than condenser");
    CHECK (centroids[(int) MicModel::laser] > centroids[(int) MicModel::ribbon], "laser brighter than ribbon");
}

static void testPerformance()
{
    g_current = "performance";
    const double sr = 48000.0;
    std::vector<Note> notes; for (int i = 0; i < 20; ++i) notes.push_back ({ (float) (57 + (i * 5) % 12), 0.5 });
    const auto x = sungMelody (notes, sr); // 10 s
    RenderSettings s; s.choir = ChoirModel::gang; s.octaveUp = s.octaveDown = s.thirdAbove = s.fifthAbove = s.below = true;
    const auto t0 = std::chrono::steady_clock::now();
    const auto stems = renderChoir (x, sr, s);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  info: 10 s vocal -> 7 stems x 12 singers rendered in %.2f s\n", secs);
    CHECK (stems.size() == 7, "stems");
    CHECK (secs < 60.0, "render time reasonable (%g s)", secs);
}

//==============================================================================
// Realistic voice: glottal pulses with jitter/shimmer, aspiration, vibrato, through formant resonators.
static std::vector<float> glottalVoice (float midi, double sr, double seconds, float vibCents = 40.0f, uint64_t seed = 5)
{
    Rng r (seed);
    const size_t n = (size_t) (seconds * sr);
    std::vector<float> g (n, 0.0f);
    const float f0 = midiToHz (midi);
    double ph = 0.0, prev = 0.0;
    float periodJitter = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double f = f0 * std::pow (2.0, (vibCents * std::sin (2 * PI * 5.6 * t)) / 1200.0) * (1.0 + periodJitter);
        ph += f / sr;
        if (ph >= 1.0) { ph -= 1.0; periodJitter = 0.008f * r.bipolar(); }
        const double flow = ph < 0.6 ? 0.5 - 0.5 * std::cos (PI * ph / 0.6) : std::max (0.0, std::cos (PI * (ph - 0.6) / 0.8));
        g[i] = (float) (flow - prev) * (1.0f + 0.04f * r.bipolar()) + 0.01f * r.bipolar();
        prev = flow;
    }
    // /a/ formants
    std::vector<float> y (n, 0.0f);
    for (auto [fc, bw, gain] : { std::tuple<double, double, double> { 730, 90, 1.0 }, { 1090, 110, 0.7 }, { 2440, 160, 0.4 }, { 3400, 250, 0.25 } })
    {
        const double rr = std::exp (-PI * bw / sr), th = 2 * PI * fc / sr;
        const double a1 = -2 * rr * std::cos (th), a2 = rr * rr;
        double y1 = 0, y2 = 0;
        for (size_t i = 0; i < n; ++i) { const double out = (1 - rr) * g[i] - a1 * y1 - a2 * y2; y2 = y1; y1 = out; y[i] += (float) (out * gain); }
    }
    float pk = 0; for (float s : y) pk = std::max (pk, std::abs (s));
    for (auto& s : y) s *= 0.4f / pk;
    return y;
}

static float bandPeak (const std::vector<float>& x, double sr, float hz, size_t a, size_t b, float spread = 0.03f)
{
    float m = 0.0f;
    for (float d = -hz * spread; d <= hz * spread; d += 0.5f) m = std::max (m, bandLevel (x, sr, hz + d, a, b));
    return m;
}

static void testRealisticVoice()
{
    g_current = "realistic voice";
    const double sr = 48000.0;
    // Pitch tracking survives jitter, shimmer, breath and vibrato without octave errors.
    for (float midi : { 45.0f, 52.0f, 57.0f, 60.0f, 64.0f, 69.0f, 74.0f })
    {
        const auto x = glottalVoice (midi, sr, 1.0);
        const auto tr = trackPitch (x, sr);
        int ok = 0, voiced = 0;
        for (size_t i = 20; i + 20 < tr.f0.size(); ++i)
        {
            if (tr.f0[i] <= 0.0f) continue;
            ++voiced;
            ok += std::abs (hzToMidi (tr.f0[i]) - midi) < 0.9f; // inside the +-40 cent vibrato plus margin
        }
        CHECK (voiced > (int) tr.f0.size() / 2, "midi %g mostly voiced (%d/%zu)", midi, voiced, tr.f0.size());
        CHECK (ok >= voiced * 95 / 100, "midi %g: %d of %d frames on the right note (no octave errors)", midi, ok, voiced);
    }

    // A held E4 with wide vibrato must keep one harmony note (no flicker to F's third).
    {
        const auto x = glottalVoice (64.0f, sr, 1.5, 55.0f);
        const auto tr = trackPitch (x, sr);
        const auto third = harmonyCurve (tr, 2, 0, Scale::major);
        float lo = 99, hi = -99;
        for (size_t i = 40; i + 40 < third.size(); ++i) { lo = std::min (lo, third[i]); hi = std::max (hi, third[i]); }
        CHECK (std::abs (lo - 3.0f) < 0.05f && std::abs (hi - 3.0f) < 0.05f, "held E with vibrato: third stays at +3 (range %g..%g)", lo, hi);
    }

    // Lead vibrato removal: a singer with removal=1 sings much steadier than the input.
    {
        const auto x = glottalVoice (57.0f, sr, 1.5, 50.0f);
        const auto tr = trackPitch (x, sr);
        auto sp = plainSinger(); sp.vibratoRemoval = 1.0f;
        std::vector<std::vector<float>> out;
        renderSingers (x, sr, tr, [] (double) { return 0.0f; }, { sp }, false, out);
        auto spreadCents = [&] (const std::vector<float>& y)
        {
            const auto t2 = trackPitch (y, sr);
            std::vector<float> c;
            for (size_t i = 40; i + 40 < t2.f0.size(); ++i) if (t2.f0[i] > 0.0f) c.push_back (1200.0f * std::log2 (t2.f0[i] / midiToHz (57.0f)));
            float mean = 0; for (float q : c) mean += q; mean /= (float) std::max<size_t> (1, c.size());
            float var = 0; for (float q : c) var += (q - mean) * (q - mean);
            return std::sqrt (var / (float) std::max<size_t> (1, c.size()));
        };
        const float before = spreadCents (x), after = spreadCents (out[0]);
        CHECK (after < before * 0.45f, "vibrato removed: %g -> %g cents rms", before, after);
    }
}

static void testChoirRealism()
{
    g_current = "choir realism";
    const double sr = 48000.0;
    const auto x = glottalVoice (60.0f, sr, 1.6, 30.0f);
    const auto tr = trackPitch (x, sr);

    // Two singers with different seeds are independent voices, not copies.
    // (Real singers drift in pitch, so their relative phase wanders - give both realistic drift.)
    auto a = plainSinger(); a.randomPhase = true; a.seed = 11; a.detuneCents = 12.0f; a.vibratoCents = 15.0f; a.vibratoHz = 5.1f;
    auto b = plainSinger(); b.randomPhase = true; b.seed = 22; b.detuneCents = 12.0f; b.vibratoCents = 15.0f; b.vibratoHz = 5.9f;
    std::vector<std::vector<float>> oa, ob, oa2;
    renderSingers (x, sr, tr, [] (double) { return 0.0f; }, { a }, false, oa);
    renderSingers (x, sr, tr, [] (double) { return 0.0f; }, { b }, false, ob);
    renderSingers (x, sr, tr, [] (double) { return 0.0f; }, { a }, false, oa2);
    double num = 0, da = 0, db = 0;
    for (size_t i = 4800; i < oa[0].size() - 4800; ++i) { num += oa[0][i] * ob[0][i]; da += oa[0][i] * oa[0][i]; db += ob[0][i] * ob[0][i]; }
    const double corr = num / std::sqrt (da * db + 1e-20);
    CHECK (std::abs (corr) < 0.35, "independent singers correlate only %g", corr);
    CHECK (oa[0] == oa2[0], "same seed => identical singer");

    // Full Harmony: the gospel choir sings soprano/alto/tenor/bass on C major chord tones under a C.
    RenderSettings s; s.choir = ChoirModel::gospel; s.stereo = false; s.voicing = Voicing::harmony;
    const auto st = renderChoir (x, sr, s);
    const auto& c = st[0].channels[0];
    const size_t a0 = (size_t) (0.3 * sr), a1 = (size_t) (1.3 * sr);
    // Energy in +-2.5% around each part's fundamental (vibrato-proof), vs whole-tone gaps.
    auto energy = [&] (const std::vector<float>& y, float hz)
    {
        double e = 0.0;
        for (float f = hz * 0.975f; f <= hz * 1.025f; f += 0.5f) { const double l = bandLevel (y, sr, f, a0, a1); e += l * l; }
        return e;
    };
    const double sop = energy (c, 261.6f);
    const double off = std::max ({ energy (c, 146.8f), energy (c, 196.0f), energy (c, 293.7f) });
    for (auto [hz, name, minOverOff] : { std::tuple<float, const char*, double> { 261.6f, "soprano C4", 10.0 }, { 220.0f, "alto A3", 10.0 },
                                         { 164.8f, "tenor E3", 10.0 }, { 130.8f, "bass C3", 8.0 } })
    {
        const double e = energy (c, hz);
        CHECK (10.0 * std::log10 (e / off) > minOverOff, "%s present (%g dB over off-chord gaps)", name, 10.0 * std::log10 (e / off));
        CHECK (10.0 * std::log10 (e / sop) > -8.0 && 10.0 * std::log10 (e / sop) < 4.0, "%s balanced with the tune (%g dB)", name, 10.0 * std::log10 (e / sop));
    }
    // ...Unison keeps everyone on the tune, Octaves adds the octave below.
    s.voicing = Voicing::unison;
    const auto su = renderChoir (x, sr, s);
    CHECK (10.0 * std::log10 (energy (su[0].channels[0], 220.0f) / energy (su[0].channels[0], 261.6f)) < -15.0, "unison has no alto part");
    s.voicing = Voicing::octaves;
    const auto so = renderChoir (x, sr, s);
    CHECK (10.0 * std::log10 (energy (so[0].channels[0], 130.8f) / energy (so[0].channels[0], 196.0f)) > 10.0, "octaves voicing adds the octave below");
    CHECK (voicingNames().size() == (size_t) Voicing::numVoicings, "voicing names");

    // Consonants pass through unshifted: a hissy 's' keeps its spectrum even in a +4 st harmony.
    Rng r (3);
    std::vector<float> hiss ((size_t) sr, 0.0f);
    Biquad hp; hp.set (Biquad::Type::highPass, sr, 4000.0, 0.707);
    for (auto& q : hiss) q = hp.process (r.bipolar() * 0.3f);
    const auto trh = trackPitch (hiss, sr);
    std::vector<std::vector<float>> oh;
    renderSingers (hiss, sr, trh, [] (double) { return 4.0f; }, { plainSinger() }, false, oh);
    const float cIn = spectralCentroid (hiss, sr, 4800, 43200), cOut = spectralCentroid (oh[0], sr, 4800, 43200);
    CHECK (std::abs (cOut - cIn) / cIn < 0.08f, "consonant not pitch-shifted (centroid %g vs %g)", cOut, cIn);
}

static void testOversampledSaturation()
{
    g_current = "saturation";
    const double sr = 48000.0;
    // A bright 5 kHz vocal-range tone driven the way the choir grit drives peaks. Its 7th and 9th
    // harmonics (35 / 45 kHz) fold to 13 kHz and 3 kHz without oversampling.
    const auto x = sine (5000.0f, sr, 0.5, 0.5f);
    auto clean = x;
    saturateOversampled (clean, 0.3f, 0.25f);
    auto naive = x;
    for (auto& q : naive) q = softSat (q, 0.3f, 0.25f);
    const float fund = bandLevel (clean, sr, 5000.0f, 2400, 21600);
    for (float alias : { 3000.0f, 13000.0f })
    {
        const float ac = bandLevel (clean, sr, alias, 2400, 21600), an = bandLevel (naive, sr, alias, 2400, 21600);
        CHECK (gainToDb (ac / fund) < -70.0f, "oversampled alias at %g Hz: %g dB", alias, gainToDb (ac / fund));
        CHECK (gainToDb (an / fund) > gainToDb (ac / fund) + 20.0f, "naive version aliases far more at %g Hz (%g dB)", alias, gainToDb (an / fund));
    }
    auto quiet = sine (1000.0f, sr, 0.2, 0.001f), q2 = quiet;
    saturateOversampled (q2, 0.8f, 0.1f);
    float err = 0; for (size_t i = 200; i < quiet.size() - 200; ++i) err = std::max (err, std::abs (q2[i] - quiet[i]));
    CHECK (err < 2.0e-5f, "transparent and aligned at low level (%g)", err);
}

int main()
{
    std::printf ("HAIRMONY 3000 engine tests\n");
    const auto t0 = std::chrono::steady_clock::now();
    testFft(); testResample(); testPitch(); testScales(); testHarmonyCurve();
    testShifterPitch(); testFormant(); testAlignment(); testRenderChoir(); testHarmonyStems();
    testEdgeCases(); testPostFx(); testMics();
    testRealisticVoice(); testChoirRealism(); testOversampledSaturation();
    testPerformance();
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("%d checks, %d failures (%.1f s)\n", g_checks, g_failures, secs);
    return g_failures == 0 ? 0 : 1;
}
