// HAIRMONY 3000 - real FFT (radix-2), dependency-free so the engine can be unit tested alone.
#pragma once
#include <complex>
#include <vector>

namespace hm
{
class RealFft
{
public:
    explicit RealFft (int size);   // size must be a power of two >= 4

    int getSize() const noexcept { return n; }

    // in: n real samples. out: n/2+1 complex bins (unscaled).
    void forward (const float* in, std::complex<float>* out);

    // in: n/2+1 complex bins. out: n real samples. Exact inverse of forward().
    void inverse (const std::complex<float>* in, float* out);

private:
    void complexFft (std::complex<float>* data, bool inverseDirection);

    int n, half;
    std::vector<int> bitrev;
    std::vector<std::complex<float>> twiddle;      // for size n/2 complex fft
    std::vector<std::complex<float>> postTwiddle;  // e^{-2 pi i k / n}
    std::vector<std::complex<float>> work;
};

inline bool isPowerOfTwo (int x) { return x > 0 && (x & (x - 1)) == 0; }
inline int nextPowerOfTwo (int x) { int p = 1; while (p < x) p <<= 1; return p; }
} // namespace hm
