#include "Fft.h"
#include <cmath>
#include <stdexcept>

namespace hm
{
static constexpr double kPi = 3.14159265358979323846;

RealFft::RealFft (int size) : n (size), half (size / 2)
{
    if (! isPowerOfTwo (size) || size < 4)
        throw std::invalid_argument ("RealFft size must be a power of two >= 4");

    const int m = half;
    bitrev.resize ((size_t) m);
    int bits = 0;
    while ((1 << bits) < m) ++bits;
    for (int i = 0; i < m; ++i)
    {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev[(size_t) i] = r;
    }

    twiddle.resize ((size_t) (m / 2 > 0 ? m / 2 : 1));
    for (int i = 0; i < m / 2; ++i)
        twiddle[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / m));

    postTwiddle.resize ((size_t) half + 1);
    for (int k = 0; k <= half; ++k)
        postTwiddle[(size_t) k] = std::polar (1.0f, (float) (-2.0 * kPi * k / n));

    work.resize ((size_t) m);
}

void RealFft::complexFft (std::complex<float>* data, bool inverseDirection)
{
    const int m = half;
    for (int i = 0; i < m; ++i)
    {
        const int j = bitrev[(size_t) i];
        if (j > i) std::swap (data[i], data[j]);
    }

    for (int len = 2; len <= m; len <<= 1)
    {
        const int halfLen = len >> 1;
        const int step = m / len;
        for (int start = 0; start < m; start += len)
        {
            for (int k = 0; k < halfLen; ++k)
            {
                auto w = twiddle[(size_t) (k * step)];
                if (inverseDirection) w = std::conj (w);
                const auto a = data[start + k];
                const auto b = data[start + k + halfLen] * w;
                data[start + k] = a + b;
                data[start + k + halfLen] = a - b;
            }
        }
    }
}

void RealFft::forward (const float* in, std::complex<float>* out)
{
    const int m = half;
    for (int i = 0; i < m; ++i)
        work[(size_t) i] = { in[2 * i], in[2 * i + 1] };

    complexFft (work.data(), false);

    for (int k = 0; k <= m; ++k)
    {
        const auto zk  = work[(size_t) (k % m)];
        const auto zmk = std::conj (work[(size_t) ((m - k) % m)]);
        const auto even = 0.5f * (zk + zmk);
        const auto odd  = std::complex<float> (0.0f, -0.5f) * (zk - zmk);
        out[k] = even + postTwiddle[(size_t) k] * odd;
    }
}

void RealFft::inverse (const std::complex<float>* in, float* out)
{
    const int m = half;
    for (int k = 0; k < m; ++k)
    {
        const auto xk  = in[k];
        const auto xmk = std::conj (in[m - k]);
        const auto even = 0.5f * (xk + xmk);
        const auto odd  = 0.5f * (xk - xmk) * std::conj (postTwiddle[(size_t) k]);
        work[(size_t) k] = even + std::complex<float> (0.0f, 1.0f) * odd;
    }

    complexFft (work.data(), true);

    const float scale = 1.0f / (float) m;
    for (int i = 0; i < m; ++i)
    {
        out[2 * i]     = work[(size_t) i].real() * scale;
        out[2 * i + 1] = work[(size_t) i].imag() * scale;
    }
}
} // namespace hm
