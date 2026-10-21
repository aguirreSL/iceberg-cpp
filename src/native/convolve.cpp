#include "native/convolve.h"

#include "native/fft.h"

#include <Eigen/Dense>

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace iceberg::native {

namespace {

/// Smallest integer `>= n` whose only prime factors are 2, 3 and 5, or 1
/// when `n <= 1`.
Eigen::Index smooth_fft_size(const Eigen::Index n) {
    if (n <= 1) {
        return 1;
    }
    // Every 2-3-5 number is (5^c * 3^b) * 2^a; for each odd part the
    // smallest candidate >= n doubles it up to n. Odd parts at or above
    // the best candidate so far cannot improve on it, which bounds both
    // loops at O(log n) steps each.
    Eigen::Index best = std::numeric_limits<Eigen::Index>::max();
    for (Eigen::Index p5 = 1; p5 < best; p5 *= 5) {
        for (Eigen::Index p35 = p5; p35 < best; p35 *= 3) {
            Eigen::Index candidate = p35;
            while (candidate < n) {
                candidate *= 2;
            }
            best = std::min(best, candidate);
        }
    }
    return best;
}

/// rfft of column `col` of `samples`, zero-padded in double to
/// `fft_size` samples (`fft_size >= samples.rows()`).
Eigen::VectorXcd column_spectrum(const Eigen::MatrixXf& samples,
                                 const Eigen::Index col,
                                 const Eigen::Index fft_size) {
    Eigen::VectorXd padded = Eigen::VectorXd::Zero(fft_size);
    padded.head(samples.rows()) = samples.col(col).cast<double>();
    return rfft(padded);
}

/// Writes rows `0 .. nl - 1` of every column of `out` by FFT. `a` and `b`
/// are non-empty and `out` has `max(a.cols(), b.cols())` columns.
void convolve_fft(const Eigen::MatrixXf& a, const Eigen::MatrixXf& b,
                  const Eigen::Index nl, Eigen::MatrixXf& out) {
    const Eigen::Index fft_size = smooth_fft_size(nl);
    if (fft_size > std::numeric_limits<int>::max()) {
        throw std::length_error("native::convolve: FFT size " +
                                std::to_string(fft_size) +
                                " does not fit in an int");
    }
    const int n_time = static_cast<int>(fft_size);

    // The paired column index never decreases with k, so keeping only the
    // latest spectrum of each operand still computes every column's
    // spectrum once, with at most two spectra held at a time.
    Eigen::Index a_col = -1;
    Eigen::Index b_col = -1;
    Eigen::VectorXcd a_spectrum;
    Eigen::VectorXcd b_spectrum;
    for (Eigen::Index k = 0; k < out.cols(); ++k) {
        const Eigen::Index ak = std::min(k, a.cols() - 1);
        const Eigen::Index bk = std::min(k, b.cols() - 1);
        if (ak != a_col) {
            a_spectrum = column_spectrum(a, ak, fft_size);
            a_col = ak;
        }
        if (bk != b_col) {
            b_spectrum = column_spectrum(b, bk, fft_size);
            b_col = bk;
        }
        const Eigen::VectorXd y = irfft(a_spectrum.cwiseProduct(b_spectrum), n_time);
        out.col(k).head(nl) = y.head(nl).cast<float>();
    }
}

/// Adds the time-domain convolution of each column pair into `out`, which
/// starts at zero. `a` and `b` are non-empty and `out` has
/// `max(a.cols(), b.cols())` columns.
void convolve_direct(const Eigen::MatrixXf& a, const Eigen::MatrixXf& b,
                     Eigen::MatrixXf& out) {
    const Eigen::Index na = a.rows();
    const Eigen::Index nb = b.rows();
    for (Eigen::Index k = 0; k < out.cols(); ++k) {
        const float* const x = a.col(std::min(k, a.cols() - 1)).data();
        const float* const h = b.col(std::min(k, b.cols() - 1)).data();
        float* const y = out.col(k).data();
        // The summation order is part of the contract: each y[n] gets its
        // float terms in increasing i, so its rounding is fixed whatever
        // the compiler does to vectorise the inner loop across j.
        for (Eigen::Index i = 0; i < na; ++i) {
            const float xi = x[i];
            for (Eigen::Index j = 0; j < nb; ++j) {
                y[i + j] += xi * h[j];
            }
        }
    }
}

}  // namespace

Signal convolve(const Signal& signal, const Signal& ir,
                const ConvolveMethod method) {
    if (signal.sample_rate != ir.sample_rate) {
        std::ostringstream message;
        // Full precision, so two rates that differ in a late digit do not
        // print alike.
        message.precision(17);
        message << "native::convolve: sample rates differ (signal "
                << signal.sample_rate << " Hz, ir " << ir.sample_rate
                << " Hz)";
        throw std::invalid_argument(message.str());
    }

    const Eigen::Index na = signal.samples.rows();
    const Eigen::Index nb = ir.samples.rows();
    const Eigen::Index cs = signal.samples.cols();
    const Eigen::Index ci = ir.samples.cols();
    const Eigen::Index n_channels = std::max(cs, ci);

    const Eigen::Index nl = na + nb - 1;
    const Eigen::Index n_out = nl <= 0 ? 0 : nl + nl % 2;

    Signal out;
    out.samples = Eigen::MatrixXf::Zero(n_out, n_channels);
    out.sample_rate = signal.sample_rate;

    if (na == 0 || nb == 0 || cs == 0 || ci == 0) {
        return out;
    }

    switch (method) {
        case ConvolveMethod::FFT:
            convolve_fft(signal.samples, ir.samples, nl, out.samples);
            return out;
        case ConvolveMethod::Direct:
            convolve_direct(signal.samples, ir.samples, out.samples);
            return out;
    }
    // Only reachable through an out-of-range cast to ConvolveMethod.
    throw std::invalid_argument("native::convolve: unknown ConvolveMethod " +
                                std::to_string(static_cast<int>(method)));
}

}  // namespace iceberg::native
