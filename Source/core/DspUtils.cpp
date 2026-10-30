#include "DspUtils.h"

namespace hm
{
std::vector<float> resample (const std::vector<float>& in, double fromRate, double toRate)
{
    if (in.empty() || fromRate <= 0.0 || toRate <= 0.0)
        return in;
    if (std::abs (fromRate - toRate) < 1.0e-6)
        return in;

    const double ratio = toRate / fromRate;
    const size_t outLen = (size_t) std::max (1.0, std::floor ((double) in.size() * ratio));
    std::vector<float> out (outLen, 0.0f);

    // Low-pass at the lower of the two Nyquists to avoid aliasing when downsampling.
    const double cutoff = std::min (1.0, ratio) * 0.95;
    const int halfTaps = 24;
    const double step = 1.0 / ratio;
    const int inLen = (int) in.size();

    for (size_t i = 0; i < outLen; ++i)
    {
        const double pos = (double) i * step;
        const int centre = (int) std::floor (pos);
        double acc = 0.0, norm = 0.0;
        for (int t = -halfTaps + 1; t <= halfTaps; ++t)
        {
            const int idx = centre + t;
            const double x = (double) idx - pos;
            const double arg = x * cutoff;
            const double sinc = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (3.14159265358979323846 * arg) / (3.14159265358979323846 * arg);
            const double w = 0.5 + 0.5 * std::cos (3.14159265358979323846 * x / (double) halfTaps); // Hann
            const double k = sinc * w * cutoff;
            norm += k;
            if (idx >= 0 && idx < inLen)
                acc += k * in[(size_t) idx];
        }
        out[i] = (float) (norm > 1.0e-9 ? acc / norm : 0.0);
    }
    return out;
}
} // namespace hm

namespace hm
{
// File-level constants (MSVC won't let a capture-less lambda read function-local constexprs).
static constexpr int kOversample = 4;
static constexpr int kOversampleTaps = 96;      // multiple of 2*up so the delay is whole samples

void saturateOversampled (std::vector<float>& x, float drive, float ref)
{
    if (drive <= 0.0f || x.empty()) return;
    constexpr int up = kOversample;
    constexpr int taps = kOversampleTaps;
    static const std::vector<float> h = []
    {
        std::vector<float> c (kOversampleTaps);
        const double fc = 0.45 / kOversample;   // cycles per (oversampled) sample
        double sum = 0.0;
        for (int n = 0; n < kOversampleTaps; ++n)
        {
            const double m = n - (kOversampleTaps - 1) * 0.5;
            const double sinc = std::abs (m) < 1e-9 ? 2.0 * fc : std::sin (2.0 * 3.14159265358979323846 * fc * m) / (3.14159265358979323846 * m);
            const double w = 0.42 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * n / (kOversampleTaps - 1))
                                  + 0.08 * std::cos (4.0 * 3.14159265358979323846 * n / (kOversampleTaps - 1)); // Blackman
            c[(size_t) n] = (float) (sinc * w);
            sum += c[(size_t) n];
        }
        for (auto& v : c) v = (float) (v / sum);
        return c;
    }();

    const int n = (int) x.size();
    const int nUp = n * up;
    std::vector<float> u ((size_t) nUp + taps, 0.0f);

    // Upsample (polyphase: only every up-th input contributes) and saturate.
    for (int i = 0; i < nUp; ++i)
    {
        double acc = 0.0;
        // contributing input samples m satisfy (i - m*up) in [0, taps)
        int m0 = (i - taps + 1 + up - 1) / up; if (i - taps + 1 < 0) m0 = 0;
        const int m1 = std::min (n - 1, i / up);
        for (int m = m0; m <= m1; ++m) acc += (double) x[(size_t) m] * h[(size_t) (i - m * up)];
        u[(size_t) i] = softSat ((float) (acc * up), drive, ref);
    }

    // Low-pass and decimate, removing the total filter delay (taps-1 oversampled samples).
    const int delay = (taps - 1);
    for (int j = 0; j < n; ++j)
    {
        const int centre = j * up + delay;      // index into u of output j after both filters
        double acc = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const int idx = centre - k;
            if (idx >= 0 && idx < nUp) acc += (double) u[(size_t) idx] * h[(size_t) k];
        }
        x[(size_t) j] = (float) acc;
    }
}
} // namespace hm
