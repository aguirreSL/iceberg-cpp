#pragma once

#include <complex>
#include <vector>

namespace iceberg::native {

/// Real FFT of one fixed power-of-two size that never allocates after
/// construction, for the audio thread (pocketfft allocates scratch on
/// every call). A complex radix-2 FFT of half the size with the usual
/// even/odd split. Normalisation as `rfft` / `irfft`: none forward, 1/n
/// inverse.
///
/// Construction allocates (twiddles, bit reversal, scratch); `forward`
/// and `inverse` do not. One instance per thread: the scratch is shared.
class RealFft {
public:
    /// `n` a power of 2, at least 4. Throws `std::invalid_argument`
    /// otherwise.
    explicit RealFft(int n);

    /// `in`: n samples. `out`: n/2 + 1 bins.
    void forward(const double* in, std::complex<double>* out);

    /// `in`: n/2 + 1 bins of a real signal's spectrum (the imaginary parts
    /// of DC and Nyquist are ignored). `out`: n samples.
    void inverse(const std::complex<double>* in, double* out);

private:
    void complex_fft(std::complex<double>* data, bool inverse) const;

    int n_;
    int half_;
    std::vector<std::complex<double>> twiddle_;   // e^{-2 pi i k / half}, k < half/2
    std::vector<std::complex<double>> split_;     // e^{-2 pi i k / n}, k <= half
    std::vector<int> bit_reverse_;
    std::vector<std::complex<double>> scratch_;
};

}  // namespace iceberg::native
