#include "ChoirEngine.h"
#include "Fft.h"
#include <algorithm>
#include <complex>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>

namespace hm
{
static constexpr double kPi = 3.14159265358979323846;

//==============================================================================
const std::vector<std::string>& choirNames()
{
    static const std::vector<std::string> n { "Gospel Choir", "Kids' Choir", "Gang Vocals", "Cyborg Choir" };
    return n;
}

const std::vector<std::string>& voicingNames()
{
    static const std::vector<std::string> n { "Unison", "Octaves", "Full Harmony" };
    return n;
}

const std::vector<std::string>& micNames()
{
    static const std::vector<std::string> n { "Studio Condenser", "Velvet Ribbon", "Stage Dynamic", "Valve Tube", "Chrome Laser" };
    return n;
}

ChoirProfile profileFor (ChoirModel m)
{
    ChoirProfile p;
    switch (m)
    {
        case ChoirModel::gospel:
            p.singers = 12; p.formantBase = 1.0f; p.formantSpread = 0.05f; p.mixedSections = true;
            p.detuneCents = 12.0f; p.vibratoCents = 18.0f; p.vibratoHz = 5.3f; p.timingMs = 24.0f; p.vibratoRemoval = 0.6f;
            p.breath = 0.05f; p.grit = 0.0f; p.presenceDb = 0.0f; p.warmthDb = 2.0f; p.hpfHz = 70.0f;
            p.ampWanderDb = 2.5f; p.tiltDb = 3.0f; break;
        case ChoirModel::kids:
            p.singers = 12; p.formantBase = 1.28f; p.formantSpread = 0.05f; p.mixedSections = false;
            p.detuneCents = 14.0f; p.vibratoCents = 10.0f; p.vibratoHz = 5.8f; p.timingMs = 28.0f; p.vibratoRemoval = 0.5f;
            p.breath = 0.14f; p.grit = 0.0f; p.presenceDb = 1.5f; p.warmthDb = -3.0f; p.hpfHz = 160.0f;
            p.ampWanderDb = 2.5f; p.tiltDb = 3.0f; break;
        case ChoirModel::gang:
            p.singers = 12; p.formantBase = 0.97f; p.formantSpread = 0.07f; p.mixedSections = false;
            p.detuneCents = 22.0f; p.vibratoCents = 4.0f; p.vibratoHz = 5.0f; p.timingMs = 40.0f; p.vibratoRemoval = 0.75f;
            p.breath = 0.05f; p.grit = 0.3f; p.presenceDb = 3.0f; p.warmthDb = 2.5f; p.hpfHz = 90.0f;
            p.formantSpread = 0.08f; p.ampWanderDb = 3.0f; p.tiltDb = 4.0f; break;
        case ChoirModel::cyborg:
        case ChoirModel::numModels:
        default:
            p.singers = 8; p.formantBase = 1.0f; p.formantSpread = 0.0f; p.mixedSections = true;
            p.detuneCents = 0.0f; p.vibratoCents = 0.0f; p.timingMs = 6.0f;
            p.breath = 0.0f; p.grit = 0.1f; p.presenceDb = 2.0f; p.warmthDb = 0.0f; p.hpfHz = 100.0f;
            p.hardTune = true; p.ampWanderDb = 0.0f; p.tiltDb = 0.0f; p.vibratoRemoval = 0.0f; break;
    }
    return p;
}

std::vector<PartSpec> partsFor (const RenderSettings& s)
{
    std::vector<PartSpec> parts;
    parts.push_back ({ "Choir", 0.0f, 0 });
    if (s.octaveUp)   parts.push_back ({ "Octave Up", 12.0f, 0 });
    if (s.octaveDown) parts.push_back ({ "Octave Down", -12.0f, 0 });
    if (s.thirdAbove)
    {
        parts.push_back ({ "3rd Above", 0.0f, 2 });
        if (s.below) parts.push_back ({ "3rd Above (Low)", -12.0f, 2 });
    }
    if (s.fifthAbove)
    {
        parts.push_back ({ "5th Above", 0.0f, 4 });
        if (s.below) parts.push_back ({ "5th Above (Low)", -12.0f, 4 });
    }
    return parts;
}

//==============================================================================
static float wrapPhase (float p)
{
    p = std::fmod (p + (float) kPi, (float) (2.0 * kPi));
    if (p < 0.0f) p += (float) (2.0 * kPi);
    return p - (float) kPi;
}

static int fftSizeFor (double sampleRate)
{
    return std::clamp (nextPowerOfTwo ((int) std::round (sampleRate * 0.04)), 512, 16384);
}

void renderSingers (const std::vector<float>& input, double sr, const PitchTrack& track,
                    const std::function<float (double)>& semitonesAt,
                    const std::vector<SingerParams>& singers, bool stereo,
                    std::vector<std::vector<float>>& out,
                    const std::function<bool (float)>& progress)
{
    const int numCh = stereo ? 2 : 1;
    const int L = (int) input.size();
    out.assign ((size_t) numCh, std::vector<float> ((size_t) L, 0.0f));
    if (L == 0 || singers.empty()) return;

    const int N = fftSizeFor (sr);
    const int half = N / 2;
    const int H = N / 8;
    const float olaNorm = 1.0f / 3.0f; // sum of Hann^2 at 87.5% overlap

    float maxOffsetMs = 0.0f;
    for (auto& s : singers) maxOffsetMs = std::max (maxOffsetMs, std::abs (s.timingMs) + std::abs (s.timingWanderMs));
    // Pad to a whole number of hops so the analysis grid always lands on the same input
    // positions, whatever the singers' timing offsets are (keeps renders consistent).
    const int rawPad = N + (int) std::ceil (maxOffsetMs * 0.001 * sr) + H;
    const int pad = ((rawPad + H - 1) / H) * H;
    const int Lp = L + 2 * pad;

    std::vector<float> xp ((size_t) Lp, 0.0f);
    std::copy (input.begin(), input.end(), xp.begin() + pad);
    std::vector<std::vector<float>> acc ((size_t) numCh, std::vector<float> ((size_t) Lp, 0.0f));

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));

    RealFft fft (N);
    std::vector<float> frame ((size_t) N), logE ((size_t) half + 1);
    std::vector<double> prefix ((size_t) half + 2);
    std::vector<int> envBins;
    std::vector<std::complex<float>> X ((size_t) half + 1), Y ((size_t) half + 1);
    std::vector<float> A ((size_t) half + 1), phi ((size_t) half + 1), prevPhi ((size_t) half + 1, 0.0f);
    std::vector<float> tf ((size_t) half + 1), E ((size_t) half + 1);
    std::vector<int> peaks, lo, hi;
    peaks.reserve ((size_t) half); lo.reserve ((size_t) half); hi.reserve ((size_t) half);

    struct SingerState
    {
        std::vector<int> prevPeaks; std::vector<float> prevPsi, prevWout;
        std::vector<int> curPeaks;  std::vector<float> curPsi, curWout;
        SmoothWander pitchWander, timeWander, ampWander;
        float vibPhase = 0.0f, staticDetune = 0.0f;
        Rng rng { 1 }, phaseRng { 2 };
        std::vector<float> tilt;
    };
    std::vector<SingerState> st (singers.size());
    for (size_t i = 0; i < singers.size(); ++i)
    {
        Rng r (mixSeed (singers[i].seed, 7));
        st[i].pitchWander = SmoothWander (r, 0.08f, 0.45f);
        st[i].timeWander  = SmoothWander (r, 0.05f, 0.25f);
        st[i].vibPhase = (float) (r.uniform() * 2.0 * kPi);
        st[i].staticDetune = r.bipolar() * 0.5f;
        st[i].ampWander = SmoothWander (r, 0.15f, 0.8f);
        st[i].rng = Rng (mixSeed (singers[i].seed, 99));
        st[i].phaseRng = Rng (mixSeed (singers[i].seed, 555));
        // Brightness curve: 0 dB below 1.5 kHz rising to tiltDb above 6 kHz.
        st[i].tilt.resize ((size_t) half + 1);
        for (int k = 0; k <= half; ++k)
        {
            const double hz = k * sr / N;
            const double t = std::clamp ((std::log2 (std::max (hz, 1.0)) - std::log2 (1500.0)) / 2.0, 0.0, 1.0);
            st[i].tilt[(size_t) k] = dbToGain ((float) (singers[i].tiltDb * t));
        }
    }

    std::vector<float> panL (singers.size()), panR (singers.size());
    for (size_t i = 0; i < singers.size(); ++i)
    {
        const float g = singers[i].gain;
        if (stereo)
        {
            const float a = (std::clamp (singers[i].pan, -1.0f, 1.0f) + 1.0f) * (float) kPi * 0.25f;
            panL[i] = std::cos (a) * g; panR[i] = std::sin (a) * g;
        }
        else { panL[i] = g; panR[i] = 0.0f; }
    }

    bool needVib = false;
    for (auto& sgr : singers) needVib |= sgr.vibratoRemoval > 0.0f;
    const std::vector<float> vib = needVib ? vibratoCurve (track) : std::vector<float>();
    auto vibAt = [&] (double pos) -> float
    {
        if (vib.empty()) return 0.0f;
        const double idx = std::clamp (pos / (double) track.hop, 0.0, (double) vib.size() - 1.0);
        const size_t i0 = (size_t) idx, i1 = std::min (i0 + 1, vib.size() - 1);
        const float fr = (float) (idx - (double) i0);
        return vib[i0] * (1.0f - fr) + vib[i1] * fr;
    };

    const int numFrames = (Lp - N) / H + 1;
    const float binHz = (float) (sr / N);
    const int breathStart = std::max (1, (int) (600.0f / binHz));
    const int breathFull  = std::max (breathStart + 1, (int) (2200.0f / binHz));
    std::vector<int> jpArr;

    for (int f = 0; f < numFrames; ++f)
    {
        const int pos = f * H;
        const double centre = (double) (pos + N / 2 - pad); // in input coordinates
        const double tSec = centre / sr;

        // ---------- analysis (shared by all singers)
        for (int i = 0; i < N; ++i) frame[(size_t) i] = xp[(size_t) (pos + i)] * win[(size_t) i];
        fft.forward (frame.data(), X.data());

        float maxA = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            A[(size_t) k] = std::abs (X[(size_t) k]);
            phi[(size_t) k] = std::arg (X[(size_t) k]);
            maxA = std::max (maxA, A[(size_t) k]);
            const float omega = (float) (2.0 * kPi * k / N);
            const float dphi = wrapPhase (phi[(size_t) k] - prevPhi[(size_t) k] - omega * (float) H);
            tf[(size_t) k] = omega + dphi / (float) H;
            prevPhi[(size_t) k] = phi[(size_t) k];
        }

        if (maxA < 1.0e-7f)
        {
            for (auto& s : st) { s.prevPeaks.clear(); s.prevPsi.clear(); s.prevWout.clear(); }
            if (progress && (f & 63) == 0 && ! progress ((float) f / (float) numFrames)) return;
            continue;
        }

        // Noisy unvoiced frames (s, t, sh, breaths) aren't pitched: pass them through untouched
        // (just humanised in timing/level) instead of pitch-shifting noise, which smears and
        // sounds metallic. Frames the tracker missed but that are tonal still get shifted.
        bool passThrough = false;
        {
            if (track.f0At (centre) <= 0.0f)
            {
                double num = 0.0, den = 0.0;
                for (int k = 1; k <= half; ++k) { const double e = (double) A[(size_t) k] * A[(size_t) k]; num += e * k; den += e; }
                const double centroidHz = den > 0.0 ? num / den * binHz : 0.0;
                passThrough = centroidHz > 2200.0;
            }
        }

        // Peaks and their regions of influence (split at the valley between neighbours).
        peaks.clear(); lo.clear(); hi.clear();
        const float floorA = maxA * 1.0e-4f;
        for (int k = 2; k <= half - 2; ++k)
        {
            const float a = A[(size_t) k];
            if (a > floorA && a > A[(size_t) k - 1] && a >= A[(size_t) k + 1] && a > A[(size_t) k - 2] && a >= A[(size_t) k + 2])
                peaks.push_back (k);
        }

        // Spectral envelope through the harmonic peaks (robust at high pitches where cepstral
        // smoothing collapses into the gaps between harmonics). When voiced, only peaks that
        // dominate +-half a harmonic spacing count, so window sidelobes don't pull it down.
        {
            const float f0 = track.f0At (centre);
            const int w = f0 > 0.0f ? std::max (2, (int) (0.5f * f0 / binHz)) : 2;
            envBins.clear();
            for (int kp : peaks)
            {
                bool dominant = true;
                for (int k = std::max (1, kp - w); k <= std::min (half, kp + w) && dominant; ++k)
                    if (A[(size_t) k] > A[(size_t) kp]) dominant = false;
                if (dominant) envBins.push_back (kp);
            }
            if (envBins.empty())
            {
                for (int k = 0; k <= half; ++k) logE[(size_t) k] = std::log (A[(size_t) k] + 1.0e-9f);
            }
            else
            {
                const float first = std::log (A[(size_t) envBins.front()] + 1.0e-9f);
                const float last  = std::log (A[(size_t) envBins.back()] + 1.0e-9f);
                for (int k = 0; k <= envBins.front(); ++k) logE[(size_t) k] = first;
                for (int k = envBins.back(); k <= half; ++k) logE[(size_t) k] = last;
                for (size_t i = 0; i + 1 < envBins.size(); ++i)
                {
                    const int k0 = envBins[i], k1 = envBins[i + 1];
                    const float l0 = std::log (A[(size_t) k0] + 1.0e-9f), l1 = std::log (A[(size_t) k1] + 1.0e-9f);
                    for (int k = k0; k <= k1; ++k) logE[(size_t) k] = l0 + (l1 - l0) * (float) (k - k0) / (float) (k1 - k0);
                }
            }
            // Light smoothing (box filter) in the log domain.
            const int sw = std::max (1, (f0 > 0.0f ? (int) (0.5f * f0 / binHz) : 2));
            double run = 0.0;
            prefix[0] = 0.0;
            for (int k = 0; k <= half; ++k) { run += logE[(size_t) k]; prefix[(size_t) k + 1] = run; }
            for (int k = 0; k <= half; ++k)
            {
                const int a0 = std::max (0, k - sw), a1 = std::min (half, k + sw);
                E[(size_t) k] = std::exp ((float) ((prefix[(size_t) a1 + 1] - prefix[(size_t) a0]) / (double) (a1 - a0 + 1)));
            }
        }
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            int l = 1, h = half;
            if (i > 0)
            {
                int m = peaks[i - 1];
                for (int k = peaks[i - 1]; k <= peaks[i]; ++k) if (A[(size_t) k] < A[(size_t) m]) m = k;
                l = m + 1;
            }
            if (i + 1 < peaks.size())
            {
                int m = peaks[i];
                for (int k = peaks[i]; k <= peaks[i + 1]; ++k) if (A[(size_t) k] < A[(size_t) m]) m = k;
                h = m;
            }
            lo.push_back (l); hi.push_back (h);
        }

        // ---------- synthesis per singer
        const float baseSemis = semitonesAt (centre);
        const float leadVibrato = vibAt (centre);
        for (size_t s = 0; s < singers.size(); ++s)
        {
            const auto& sp = singers[s];
            auto& ss = st[s];

            const float cents = sp.detuneCents * (ss.pitchWander.at (tSec) + ss.staticDetune)
                              + sp.vibratoCents * std::sin ((float) (2.0 * kPi * sp.vibratoHz * tSec) + ss.vibPhase);
            const float partSemis = sp.partSemitones ? sp.partSemitones (centre) : 0.0f;
            const float ratio = std::exp2 ((baseSemis + partSemis - sp.vibratoRemoval * leadVibrato + cents * 0.01f) / 12.0f);
            const float fmt = std::max (0.25f, sp.formant);
            const float ampGain = sp.ampWanderDb > 0.0f ? dbToGain (sp.ampWanderDb * ss.ampWander.at (tSec)) : 1.0f;
            const auto& tilt = ss.tilt;

            auto envAt = [&] (float bin)
            {
                if (bin <= 0.0f) return E[0];
                if (bin >= (float) half) return E[(size_t) half];
                const int b0 = (int) bin; const float fr = bin - (float) b0;
                return E[(size_t) b0] * (1.0f - fr) + E[(size_t) b0 + 1] * fr;
            };

            std::fill (Y.begin(), Y.end(), std::complex<float> (0.0f, 0.0f));
            ss.curPeaks.clear(); ss.curPsi.clear(); ss.curWout.clear();

            if (passThrough)
            {
                for (int k = 0; k <= half; ++k) Y[(size_t) k] = X[(size_t) k] * tilt[(size_t) k];
            }
            else
            {

            jpArr.resize (peaks.size());
            for (size_t i = 0; i < peaks.size(); ++i)
            {
                const float wout = tf[(size_t) peaks[i]] * ratio;
                jpArr[i] = (int) std::lround (wout * (float) N / (float) (2.0 * kPi));
            }

            size_t prevIdx = 0;
            for (size_t i = 0; i < peaks.size(); ++i)
            {
                const int kp = peaks[i];
                const int jp = jpArr[i];
                if (jp < 1 || jp >= half) continue;
                const float wout = tf[(size_t) kp] * ratio;

                // Track this peak to the nearest peak of the previous frame for phase continuity.
                // New partials start at a random phase per singer, so the singers don't sum
                // coherently (which sounds like one voice through a flanger, not a choir).
                float psi = phi[(size_t) kp] + (sp.randomPhase ? (float) (ss.phaseRng.uniform() * 2.0 * kPi) : 0.0f);
                while (prevIdx + 1 < ss.prevPeaks.size() && std::abs (ss.prevPeaks[prevIdx + 1] - kp) <= std::abs (ss.prevPeaks[prevIdx] - kp))
                    ++prevIdx;
                if (! ss.prevPeaks.empty() && std::abs (ss.prevPeaks[prevIdx] - kp) <= 2)
                    psi = wrapPhase (ss.prevPsi[prevIdx] + (float) H * 0.5f * (ss.prevWout[prevIdx] + wout));

                ss.curPeaks.push_back (kp); ss.curPsi.push_back (psi); ss.curWout.push_back (wout);

                const int delta = jp - kp;
                int olo = lo[i] + delta, ohi = hi[i] + delta;
                if (i > 0) olo = std::max (olo, (jpArr[i - 1] + jp) / 2 + 1);
                if (i + 1 < peaks.size()) ohi = std::min (ohi, (jp + jpArr[i + 1]) / 2);
                olo = std::max (olo, 1); ohi = std::min (ohi, half - 1);

                const float phiPeak = phi[(size_t) kp];
                for (int j = olo; j <= ohi; ++j)
                {
                    const int k = j - delta;
                    if (k < 0 || k > half) continue;
                    const float corr = std::clamp (envAt ((float) j / fmt) / std::max (E[(size_t) k], 1.0e-9f), 0.0f, 8.0f);
                    const float mag = A[(size_t) k] * corr * tilt[(size_t) j];
                    Y[(size_t) j] += std::polar (mag, psi + (phi[(size_t) k] - phiPeak));
                }
            }
            }

            if (sp.breath > 0.0f && ! passThrough)
            {
                for (int j = breathStart; j < half; ++j)
                {
                    const float shape = std::min (1.0f, (float) (j - breathStart) / (float) (breathFull - breathStart));
                    const float mag = sp.breath * 1.4f * envAt ((float) j / fmt) * shape;
                    Y[(size_t) j] += std::polar (mag, (float) (ss.rng.uniform() * 2.0 * kPi));
                }
            }

            std::swap (ss.prevPeaks, ss.curPeaks); std::swap (ss.prevPsi, ss.curPsi); std::swap (ss.prevWout, ss.curWout);

            fft.inverse (Y.data(), frame.data());

            const double offsetMs = sp.timingMs + sp.timingWanderMs * ss.timeWander.at (tSec);
            const int offset = (int) std::lround (offsetMs * 0.001 * sr);
            const int dst = pos + offset;
            const float gl = panL[s] * olaNorm * ampGain, gr = panR[s] * olaNorm * ampGain;
            for (int i = 0; i < N; ++i)
            {
                const int d = dst + i;
                if (d < 0 || d >= Lp) continue;
                const float v = frame[(size_t) i] * win[(size_t) i];
                acc[0][(size_t) d] += v * gl;
                if (numCh > 1) acc[1][(size_t) d] += v * gr;
            }
        }

        if (progress && (f & 63) == 0 && ! progress ((float) f / (float) numFrames))
            return;
    }

    for (int ch = 0; ch < numCh; ++ch)
        std::copy (acc[(size_t) ch].begin() + pad, acc[(size_t) ch].begin() + pad + L, out[(size_t) ch].begin());
}

//==============================================================================
static float stemRms (const Stem& s)
{
    double e = 0.0; size_t n = 0;
    for (auto& ch : s.channels) { for (float v : ch) e += (double) v * v; n += ch.size(); }
    return n ? (float) std::sqrt (e / (double) n) : 0.0f;
}

static float stemPeak (const Stem& s)
{
    float p = 0.0f;
    for (auto& ch : s.channels) for (float v : ch) p = std::max (p, std::abs (v));
    return p;
}

static void saturateStem (Stem& stem, float drive)
{
    if (drive <= 0.0f) return;
    // Saturate only the loud peaks (well above the average level), oversampled 4x so the
    // new harmonics don't alias back down into the audible band.
    const float ref = std::max (1.0e-4f, stemRms (stem) * 4.0f);
    for (auto& ch : stem.channels) saturateOversampled (ch, drive, ref);
}

void applyMicModel (Stem& stem, MicModel mic, double sr)
{
    using T = Biquad::Type;
    struct Band { T type; double f, q, g; };
    std::vector<Band> bands;
    bands.reserve (4);
    float drive = 0.0f;
    switch (mic)
    {
        case MicModel::condenser: bands = { { T::highPass, 60, 0.707, 0 }, { T::peak, 5000, 1.0, 1.5 }, { T::highShelf, 10000, 0.707, 2.5 } }; break;
        case MicModel::ribbon:    bands = { { T::highPass, 45, 0.707, 0 }, { T::lowShelf, 160, 0.707, 1.5 }, { T::highShelf, 6000, 0.707, -4.0 }, { T::lowPass, 12000, 0.707, 0 } }; break;
        case MicModel::dynamic:   bands = { { T::highPass, 110, 0.707, 0 }, { T::lowShelf, 250, 0.707, -1.5 }, { T::peak, 3500, 0.9, 4.0 }, { T::highShelf, 9000, 0.707, -3.0 } }; break;
        case MicModel::tube:      bands = { { T::highPass, 70, 0.707, 0 }, { T::lowShelf, 250, 0.707, 2.5 }, { T::peak, 1200, 0.8, 1.0 }, { T::highShelf, 9000, 0.707, 1.0 } }; drive = 0.3f; break;
        case MicModel::laser:
        case MicModel::numMics:
        default:                  bands = { { T::highPass, 140, 0.707, 0 }, { T::peak, 2800, 1.0, 3.5 }, { T::highShelf, 9000, 0.707, 5.0 } }; drive = 0.2f; break;
    }
    for (auto& ch : stem.channels)
    {
        for (auto& b : bands)
        {
            Biquad bq; bq.set (b.type, sr, b.f, b.q, b.g);
            bq.process (ch.data(), (int) ch.size());
        }
    }
    saturateStem (stem, drive);
}

static void applyCharacter (Stem& stem, const ChoirProfile& p, double sr)
{
    using T = Biquad::Type;
    for (auto& ch : stem.channels)
    {
        Biquad hp; hp.set (T::highPass, sr, p.hpfHz, 0.707); hp.process (ch.data(), (int) ch.size());
        if (std::abs (p.warmthDb) > 0.01f)   { Biquad b; b.set (T::peak, sr, 220, 0.8, p.warmthDb);  b.process (ch.data(), (int) ch.size()); }
        if (std::abs (p.presenceDb) > 0.01f) { Biquad b; b.set (T::peak, sr, 3000, 0.9, p.presenceDb); b.process (ch.data(), (int) ch.size()); }
    }
    saturateStem (stem, p.grit);
}

static std::vector<SingerParams> makeSingers (const ChoirProfile& p, bool stereo, uint64_t seed)
{
    std::vector<SingerParams> v ((size_t) p.singers);
    Rng r (seed);

    // Shuffle pan slots so neighbouring singers aren't neighbours in the stereo field.
    std::vector<int> slots ((size_t) p.singers);
    std::iota (slots.begin(), slots.end(), 0);
    for (int i = p.singers - 1; i > 0; --i) std::swap (slots[(size_t) i], slots[(size_t) (r.next() % (uint64_t) (i + 1))]);

    for (int i = 0; i < p.singers; ++i)
    {
        auto& s = v[(size_t) i];
        float section = 1.0f;
        if (p.mixedSections) section = p.hardTune ? (i % 2 == 0 ? 0.85f : 1.15f) : (i % 2 == 0 ? 0.9f : 1.1f);
        s.formant = p.formantBase * section * (1.0f + p.formantSpread * r.bipolar());
        s.detuneCents = p.detuneCents * (0.6f + 0.8f * r.uniform());
        s.vibratoCents = p.vibratoCents * (0.6f + 0.8f * r.uniform());
        s.vibratoHz = p.vibratoHz + 0.6f * r.bipolar();
        s.timingMs = p.timingMs * 0.8f * r.bipolar();
        s.timingWanderMs = p.timingMs * 0.25f;
        s.breath = p.breath * (0.7f + 0.6f * r.uniform());
        s.gain = p.hardTune ? 1.0f : dbToGain (2.0f * r.bipolar());
        s.pan = stereo && p.singers > 1 ? -0.9f + 1.8f * (float) slots[(size_t) i] / (float) (p.singers - 1) : 0.0f;
        s.ampWanderDb = p.ampWanderDb * (0.6f + 0.8f * r.uniform());
        s.tiltDb = p.tiltDb * r.bipolar();
        s.randomPhase = true;
        s.vibratoRemoval = std::clamp (p.vibratoRemoval * (0.6f + 0.8f * r.uniform()), 0.0f, 1.0f);
        s.seed = mixSeed (seed, (uint64_t) i + 1000);
    }
    return v;
}

//==============================================================================
// Choir sections for the main stem. degrees = diatonic steps from the melody (negative = below),
// octave = fixed semitones, formant = vocal-tract size of that section.
struct Section { int count; int degrees; float octave; float formant; float gainDb = 0.0f; };

static std::vector<Section> sectionsFor (ChoirModel m, Voicing v)
{
    switch (v)
    {
        case Voicing::harmony:
            switch (m)
            {
                case ChoirModel::gospel: return { { 3, 0, 0, 1.06f, 1.0f }, { 3, -2, 0, 1.02f, 2.5f }, { 3, -5, 0, 0.95f, 0.5f }, { 3, 0, -12, 0.88f, -1.5f } }; // S A T B
                case ChoirModel::kids:   return { { 6, 0, 0, 1.0f, 0.0f }, { 6, -2, 0, 0.97f, 2.0f } };                         // 2-part
                case ChoirModel::gang:   return { { 5, 0, 0, 1.0f, 0.0f }, { 4, 0, -12, 0.93f, -2.0f }, { 3, -2, 0, 0.98f, 2.0f } };
                case ChoirModel::cyborg:
                case ChoirModel::numModels:
                default:                 return { { 3, 0, 0, 1.15f, 0.0f }, { 3, -2, 0, 0.85f, 1.5f }, { 2, -4, 0, 1.0f, 2.0f } };
            }
        case Voicing::octaves:
            switch (m)
            {
                case ChoirModel::kids:   return { { 7, 0, 0, 1.0f }, { 5, 0, 12, 1.04f } };
                case ChoirModel::cyborg: return { { 4, 0, 0, 1.15f }, { 4, 0, -12, 0.85f } };
                case ChoirModel::gospel: return { { 7, 0, 0, 1.06f }, { 5, 0, -12, 0.9f } };
                case ChoirModel::gang:
                case ChoirModel::numModels:
                default:                 return { { 7, 0, 0, 1.0f }, { 5, 0, -12, 0.93f } };
            }
        case Voicing::unison:
        case Voicing::numVoicings:
        default: return {};
    }
}

std::vector<Stem> renderChoir (const std::vector<float>& input, double sr, const RenderSettings& settings,
                               ProgressFn progress, const std::atomic<bool>* cancel)
{
    std::vector<Stem> stems;
    if (input.empty() || sr <= 0.0) return stems;

    auto cancelled = [&] { return cancel != nullptr && cancel->load(); };

    const auto profile = profileFor (settings.choir);
    const auto parts = partsFor (settings);
    const PitchTrack track = trackPitch (input, sr);
    if (cancelled()) return {};

    std::vector<float> tune;
    if (profile.hardTune) tune = hardTuneCurve (track, settings.key, settings.scale);
    std::vector<std::vector<float>> harmony (parts.size());
    for (size_t p = 0; p < parts.size(); ++p)
        if (parts[p].harmonyDegrees != 0)
            harmony[p] = harmonyCurve (track, parts[p].harmonyDegrees, settings.key, settings.scale);

    // Section harmony curves for the main stem (computed once, shared by the singers).
    const auto sections = sectionsFor (settings.choir, settings.voicing);
    std::map<int, std::vector<float>> sectionCurves;
    for (auto& sec : sections)
        if (sec.degrees != 0 && sectionCurves.count (sec.degrees) == 0)
            sectionCurves[sec.degrees] = harmonyCurve (track, sec.degrees, settings.key, settings.scale);

    auto curveAt = [&track] (const std::vector<float>& c, double pos) -> float
    {
        if (c.empty()) return 0.0f;
        const double idx = std::clamp (pos / (double) track.hop, 0.0, (double) c.size() - 1.0);
        const size_t i0 = (size_t) idx; const size_t i1 = std::min (i0 + 1, c.size() - 1);
        const float fr = (float) (idx - (double) i0);
        return c[i0] * (1.0f - fr) + c[i1] * fr;
    };

    stems.resize (parts.size());
    std::vector<float> partProgress (parts.size(), 0.0f);
    std::mutex progressLock;

    auto renderPart = [&] (size_t p)
    {
        const auto& part = parts[p];
        auto singers = makeSingers (profile, settings.stereo, mixSeed (settings.seed, p + 1));
        if (p == 0 && ! sections.empty())
        {
            // Split the main choir into sections singing in parts under / around the melody.
            size_t idx = 0;
            for (auto& sec : sections)
                for (int c = 0; c < sec.count && idx < singers.size(); ++c, ++idx)
                {
                    auto& sg = singers[idx];
                    const float spread = sg.formant / std::max (0.01f, profile.formantBase * (profile.mixedSections ? (profile.hardTune ? (idx % 2 == 0 ? 0.85f : 1.15f) : (idx % 2 == 0 ? 0.9f : 1.1f)) : 1.0f));
                    sg.formant = profile.formantBase * sec.formant * spread;
                    sg.gain *= dbToGain (sec.gainDb);
                    const std::vector<float>* curve = sec.degrees != 0 ? &sectionCurves.at (sec.degrees) : nullptr;
                    const float oct = sec.octave;
                    if (curve != nullptr || oct != 0.0f)
                        sg.partSemitones = [curve, oct, &curveAt] (double pos) { return oct + (curve ? curveAt (*curve, pos) : 0.0f); };
                }
        }
        auto semis = [&] (double pos)
        {
            return part.octaveSemitones + curveAt (harmony[p], pos) + curveAt (tune, pos);
        };
        Stem stem;
        stem.name = part.name;
        renderSingers (input, sr, track, semis, singers, settings.stereo, stem.channels,
                       [&] (float fr)
                       {
                           if (progress)
                           {
                               std::lock_guard<std::mutex> g (progressLock);
                               partProgress[p] = fr;
                               const float total = std::accumulate (partProgress.begin(), partProgress.end(), 0.0f) / (float) parts.size();
                               progress (0.05f + 0.9f * total);
                           }
                           return ! cancelled();
                       });
        if (cancelled()) return;
        // Summing N decorrelated voices grows level by ~sqrt(N); bring it back to one singer.
        const float norm = 1.0f / std::sqrt ((float) std::max (1, profile.singers));
        for (auto& ch : stem.channels) for (auto& v : ch) v *= norm;
        applyCharacter (stem, profile, sr);
        applyMicModel (stem, settings.mic, sr);
        stems[p] = std::move (stem);
    };

    if (progress) progress (0.05f);

    unsigned threads = settings.maxThreads > 0 ? (unsigned) settings.maxThreads : std::max (1u, std::thread::hardware_concurrency());
    threads = std::min<unsigned> (threads, (unsigned) parts.size());
    if (threads <= 1)
    {
        for (size_t p = 0; p < parts.size() && ! cancelled(); ++p) renderPart (p);
    }
    else
    {
        std::atomic<size_t> next { 0 };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back ([&] { for (size_t p; (p = next++) < parts.size() && ! cancelled();) renderPart (p); });
        for (auto& th : pool) th.join();
    }
    if (cancelled()) return {};

    // Loudness: match the main choir to the input's level, apply the same gain to every stem,
    // then make sure nothing clips.
    double inE = 0.0; for (float v : input) inE += (double) v * v;
    const float inRms = (float) std::sqrt (inE / (double) input.size());
    const float choirRms = stemRms (stems[0]);
    float gain = (choirRms > 1.0e-7f && inRms > 1.0e-7f) ? inRms / choirRms : 1.0f;
    gain = std::clamp (gain, 0.05f, 20.0f);
    float peak = 0.0f;
    for (auto& s : stems) peak = std::max (peak, stemPeak (s) * gain);
    if (peak > 0.95f) gain *= 0.95f / peak;
    for (auto& s : stems) for (auto& ch : s.channels) for (auto& v : ch) v *= gain;

    if (progress) progress (1.0f);
    return stems;
}

//==============================================================================
void PostFx::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    reverb.prepare (sr);
    reverb.setRoom (0.82f, 0.45f);
    for (auto& b : verbHp) b.set (Biquad::Type::highPass, sr, 180.0, 0.707);
    curLoCut = curBright = -1.0f;
    smoothLoCut = loCut; smoothBright = bright;
    updateFilters();
    reset();
}

void PostFx::reset()
{
    for (auto& s : hpf) for (auto& b : s) b.reset();
    for (auto& b : shelf) b.reset();
    for (auto& b : air) b.reset();
    for (auto& b : verbHp) b.reset();
    reverb.reset();
    smoothVerb = verb;
    smoothLoCut = loCut; smoothBright = bright;
    curLoCut = curBright = -1.0f;
    updateFilters();
}

void PostFx::setParams (float loCutHz, float brightness, float rev)
{
    loCut = std::clamp (loCutHz, 20.0f, 800.0f);
    bright = std::clamp (brightness, 0.0f, 1.0f);
    verb = std::clamp (rev, 0.0f, 1.0f);
}

void PostFx::updateFilters()
{
    if (curLoCut < 0.0f || std::abs (smoothLoCut - curLoCut) > 0.01f)
    {
        for (auto& side : hpf)
        {
            side[0].set (Biquad::Type::highPass, sr, smoothLoCut, 0.5412); // 4th-order Butterworth
            side[1].set (Biquad::Type::highPass, sr, smoothLoCut, 1.3066);
        }
        curLoCut = smoothLoCut;
    }
    if (curBright < 0.0f || std::abs (smoothBright - curBright) > 1.0e-5f)
    {
        for (auto& b : shelf) b.set (Biquad::Type::highShelf, sr, 4500.0, 0.707, smoothBright * 9.0);
        for (auto& b : air)   b.set (Biquad::Type::highShelf, sr, 12000.0, 0.707, smoothBright * 4.0);
        curBright = smoothBright;
    }
}

void PostFx::process (float* left, float* right, int n)
{
    // Parameters glide (log-frequency for the cut-off) and filters are recomputed every
    // 32 samples, so fast automation can't kick the biquads into big transients.
    constexpr int sub = 32;
    if (n > sub)
    {
        for (int s = 0; s < n; s += sub)
            process (left + s, right ? right + s : nullptr, std::min (sub, n - s));
        return;
    }
    const float a = (float) std::exp (-(double) n / (0.03 * sr));
    if (curLoCut < 0.0f) { smoothLoCut = loCut; smoothBright = bright; }
    smoothLoCut = std::exp (a * std::log (smoothLoCut) + (1.0f - a) * std::log (loCut));
    smoothBright = a * smoothBright + (1.0f - a) * bright;
    if (std::abs (smoothLoCut - loCut) < 0.05f) smoothLoCut = loCut;
    if (std::abs (smoothBright - bright) < 1.0e-4f) smoothBright = bright;
    updateFilters();
    const bool loCutOn = smoothLoCut > 20.5f;
    const bool brightOn = smoothBright > 1.0e-4f;
    const int numCh = right != nullptr ? 2 : 1;
    float* chans[2] = { left, right };

    for (int c = 0; c < numCh; ++c)
    {
        float* d = chans[c];
        for (int i = 0; i < n; ++i)
        {
            float v = d[i];
            if (loCutOn) v = hpf[c][1].process (hpf[c][0].process (v));
            if (brightOn) v = air[c].process (shelf[c].process (v));
            d[i] = v;
        }
    }

    const float smoothCoeff = (float) std::exp (-1.0 / (0.02 * sr));
    for (int i = 0; i < n; ++i)
    {
        smoothVerb = smoothCoeff * smoothVerb + (1.0f - smoothCoeff) * verb;
        if (smoothVerb < 1.0e-5f && verb <= 0.0f) continue;
        const float inL = left[i], inR = right ? right[i] : left[i];
        float wl, wr;
        reverb.processSample (verbHp[0].process (inL), verbHp[1].process (inR), wl, wr);
        const float g = smoothVerb * 1.3f;
        if (right) { left[i] += wl * g; right[i] += wr * g; }
        else left[i] += (wl + wr) * 0.5f * g;
    }
}

Stem processStemOffline (const Stem& in, double sr, float loCutHz, float brightness, float rev)
{
    Stem out;
    out.name = in.name;
    if (in.channels.empty()) return out;
    const int tail = rev > 0.001f ? (int) (sr * 3.5) : 0;
    const int len = in.numSamples() + tail;
    out.channels.assign (in.channels.size(), std::vector<float> ((size_t) len, 0.0f));
    for (size_t c = 0; c < in.channels.size(); ++c)
        std::copy (in.channels[c].begin(), in.channels[c].end(), out.channels[c].begin());

    PostFx fx;
    fx.prepare (sr);
    fx.setParams (loCutHz, brightness, rev);
    fx.reset();
    const int block = 4096;
    for (int s = 0; s < len; s += block)
    {
        const int n = std::min (block, len - s);
        fx.process (out.channels[0].data() + s, out.channels.size() > 1 ? out.channels[1].data() + s : nullptr, n);
    }

    if (tail > 0)
    {
        // Trim the reverb tail once it has decayed below -90 dBFS.
        int last = in.numSamples();
        for (int i = len - 1; i >= in.numSamples(); --i)
        {
            bool loud = false;
            for (auto& ch : out.channels) if (std::abs (ch[(size_t) i]) > 3.0e-5f) { loud = true; break; }
            if (loud) { last = i + 1; break; }
        }
        for (auto& ch : out.channels) ch.resize ((size_t) last);
    }
    return out;
}
} // namespace hm
