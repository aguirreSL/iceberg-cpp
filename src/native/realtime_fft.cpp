#include "native/realtime_fft.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace iceberg::native {

namespace {

constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

}  // namespace

RealFft::RealFft(int n) : n_(n), half_(n / 2) {
    if (n < 4 || (n & (n - 1)) != 0) {
        throw std::invalid_argument("RealFft: size must be a power of 2, at least 4");
    }
    twiddle_.resize(static_cast<size_t>(half_ / 2 > 0 ? half_ / 2 : 1));
    for (int k = 0; k < half_ / 2; ++k) {
        twiddle_[static_cast<size_t>(k)] = std::polar(1.0, -2.0 * kPi * k / half_);
    }
    split_.resize(static_cast<size_t>(half_ + 1));
    for (int k = 0; k <= half_; ++k) {
        split_[static_cast<size_t>(k)] = std::polar(1.0, -2.0 * kPi * k / n_);
    }
    bit_reverse_.resize(static_cast<size_t>(half_));
    int bits = 0;
    while ((1 << bits) < half_) ++bits;
    for (int i = 0; i < half_; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) {
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        }
        bit_reverse_[static_cast<size_t>(i)] = r;
    }
    scratch_.resize(static_cast<size_t>(half_));
}

void RealFft::complex_fft(cd* data, bool inverse) const {
    for (int i = 0; i < half_; ++i) {
        const int r = bit_reverse_[static_cast<size_t>(i)];
        if (r > i) std::swap(data[i], data[r]);
    }
    for (int len = 2; len <= half_; len <<= 1) {
        const int step = half_ / len;
        for (int start = 0; start < half_; start += len) {
            for (int k = 0; k < len / 2; ++k) {
                cd w = twiddle_[static_cast<size_t>(k * step)];
                if (inverse) w = std::conj(w);
                const cd a = data[start + k];
                const cd b = data[start + k + len / 2] * w;
                data[start + k] = a + b;
                data[start + k + len / 2] = a - b;
            }
        }
    }
}

void RealFft::forward(const double* in, cd* out) {
    // z[m] = x[2m] + i x[2m+1]; Z = FFT_half(z).
    for (int m = 0; m < half_; ++m) scratch_[static_cast<size_t>(m)] = cd(in[2 * m], in[2 * m + 1]);
    complex_fft(scratch_.data(), false);
    // X[k] = E[k] + W^k O[k], E = (Z[k] + conj Z[h-k]) / 2, O = (Z[k] - conj Z[h-k]) / 2i.
    for (int k = 0; k <= half_; ++k) {
        const cd zk = scratch_[static_cast<size_t>(k % half_)];
        const cd zc = std::conj(scratch_[static_cast<size_t>((half_ - k) % half_)]);
        const cd e = 0.5 * (zk + zc);
        const cd o = cd(0.0, -0.5) * (zk - zc);
        out[k] = e + split_[static_cast<size_t>(k)] * o;
    }
}

void RealFft::inverse(const cd* in, double* out) {
    // E[k] = (X[k] + conj X[h-k]) / 2, O[k] = (X[k] - conj X[h-k]) / (2 W^k);
    // z = IFFT_half(E + i O) = x[2m] + i x[2m+1].
    for (int k = 0; k < half_; ++k) {
        cd xk = in[k];
        cd xc = std::conj(in[half_ - k]);
        if (k == 0) {   // DC and Nyquist are real for a real signal
            xk = cd(in[0].real(), 0.0);
            xc = cd(in[half_].real(), 0.0);
        }
        const cd e = 0.5 * (xk + xc);
        const cd o = 0.5 * (xk - xc) * std::conj(split_[static_cast<size_t>(k)]);
        scratch_[static_cast<size_t>(k)] = e + cd(0.0, 1.0) * o;
    }
    complex_fft(scratch_.data(), true);
    const double scale = 1.0 / static_cast<double>(half_);
    for (int m = 0; m < half_; ++m) {
        out[2 * m] = scratch_[static_cast<size_t>(m)].real() * scale;
        out[2 * m + 1] = scratch_[static_cast<size_t>(m)].imag() * scale;
    }
}

}  // namespace iceberg::native
