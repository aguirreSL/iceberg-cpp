#include "calibration/signals.h"

#include "native/fft.h"
#include "native/hann.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace iceberg::calibration {

namespace {

constexpr double kPi = 3.14159265358979323846;

using iceberg::native::WindowType;

// One pair of 1-based bounds on `y`. The caller has clamped both to the
// length of `y`, so every segment below stays inside it.
Eigen::VectorXd apply_pair(const Eigen::VectorXd& y, const long s, const long e,
                           const WindowType type, const bool symmetric) {
    if (s < 1 || e < 1) {
        throw std::invalid_argument("calibration::time_window: bounds must be at least 1, got " +
                                    std::to_string(s) + " and " + std::to_string(e));
    }
    const Eigen::Index n = y.size();
    if (s == e && s == n) {
        return y;
    }

    const Eigen::Index s0 = s - 1;  // 1-based s -> 0-based s - 1
    const Eigen::Index e0 = e - 1;  // 1-based e -> 0-based e - 1
    Eigen::ArrayXd w = Eigen::ArrayXd::Ones(n);
    if (type == WindowType::Rectwin) {
        if (s <= e) {
            w.tail(n - e0 - 1).setZero();
        } else {
            w.head(e0).setZero();
        }
    } else {
        const long h = std::abs(e - s) + 1;
        const Eigen::ArrayXd hw = iceberg::native::hann(static_cast<int>(2 * h));
        if (s <= e) {
            w.segment(s0, h) = hw.tail(h);
            w.tail(n - e0 - 1).setZero();
        } else {
            w.head(e0).setZero();
            w.segment(e0, h) = hw.head(h);
        }
    }

    if (symmetric) {
        // The mirrored copy adds the same fade at the other end; removing the
        // minimum brings the untouched middle back to the weight of one pair.
        const Eigen::ArrayXd both = w + w.reverse();
        w = both - both.minCoeff();
    }
    return (y.array() * w).matrix();
}

// Applies 2 or 4 bounds (1-based, as doubles). With 4, the second pair goes
// first and is never symmetric.
Eigen::VectorXd apply_bounds(const Eigen::VectorXd& y, std::vector<double> bounds,
                             const WindowType type, const bool symmetric) {
    const double n = static_cast<double>(y.size());
    if (*std::max_element(bounds.begin(), bounds.end()) > n) {
        for (double& b : bounds) {
            b = std::min(b, n);
        }
    }
    const long s = static_cast<long>(bounds[0]);
    const long e = static_cast<long>(bounds[1]);
    if (bounds.size() == 4) {
        const Eigen::VectorXd inner = apply_bounds(y, {bounds[2], bounds[3]}, type, false);
        return apply_pair(inner, s, e, type, symmetric);
    }
    return apply_pair(y, s, e, type, symmetric);
}

}  // namespace

Eigen::VectorXd time_window(const Eigen::VectorXd& x, const double fs,
                            const std::vector<double>& range, const RangeUnit unit,
                            const iceberg::native::WindowType type, const bool symmetric) {
    if (range.size() != 2 && range.size() != 4) {
        throw std::invalid_argument("calibration::time_window: range must hold 2 or 4 values, got " +
                                    std::to_string(range.size()));
    }
    std::vector<double> bounds = range;
    if (unit == RangeUnit::Seconds) {
        const double n = static_cast<double>(x.size());
        for (double& v : bounds) {
            v = std::round(v * fs) + 1.0;
        }
        // An end at the very end of the signal names sample N, not N + 1.
        if (bounds[1] == n + 1.0) {
            bounds[1] -= 1.0;
        }
        for (double& v : bounds) {
            v = v + std::fmod(v, 2.0);
        }
    }
    return apply_bounds(x, bounds, type, symmetric);
}

Eigen::VectorXd normalize_peak(const Eigen::VectorXd& x) {
    if (x.size() == 0) {
        return x;
    }
    const double p = x.cwiseAbs().maxCoeff();
    if (!(p > 0.0)) {
        return x;
    }
    // A true division per sample: a product with 1 / p can differ in the last
    // bit, and the reference divides.
    Eigen::VectorXd out(x.size());
    for (Eigen::Index i = 0; i < x.size(); ++i) {
        out(i) = x(i) / p;
    }
    return out;
}

Eigen::VectorXd sweep(const double stop_margin, const double fs, const int fft_degree) {
    // 2 * 2^fft_degree is the irfft length, an int.
    if (fft_degree < 0 || fft_degree > 29) {
        throw std::invalid_argument("calibration::sweep: fft_degree must be in 0 .. 29, got " +
                                    std::to_string(fft_degree));
    }
    const int n0 = 1 << fft_degree;
    const double gd0 = 0.03;
    const double gd1 = static_cast<double>(n0) / fs - stop_margin;
    if (gd1 < gd0) {
        throw std::invalid_argument("calibration::sweep: stop margin " + std::to_string(stop_margin) +
                                    " s leaves a group delay below 30 ms for 2^" +
                                    std::to_string(fft_degree) + " samples");
    }

    // Built at twice the length; the first half is kept at the end.
    const int n = 2 * n0;
    const int nb = n / 2 + 1;
    const double k = (gd1 - gd0) / static_cast<double>(nb);
    const double d = fs / static_cast<double>(n);

    // The phase integrates the linearly rising group delay over frequency.
    Eigen::VectorXd phi(nb);
    double r = 0.0;
    for (int i = 0; i < nb; ++i) {
        r += -(gd0 + k * static_cast<double>(i));
        phi(i) = r * d * 2.0 * kPi;
    }

    // A linear phase term that brings the last (Nyquist) bin to a multiple of
    // pi, so that bin is real, as it must be for a real signal.
    const double q = std::fmod(phi(nb - 1), kPi);
    Eigen::VectorXcd spectrum(nb);
    for (int i = 0; i < nb; ++i) {
        const double p = phi(i) - (static_cast<double>(i) / static_cast<double>(nb - 1)) * q;
        spectrum(i) = std::exp(std::complex<double>(0.0, p));
    }

    const double wend = std::min(gd1, static_cast<double>(n) / (2.0 * fs) - gd0);
    const std::vector<double> range{gd0, 0.0, wend, wend + gd0};

    // Alternating the time window with a projection back onto unit magnitude
    // pulls the signal toward one that is both flat and inside the window. The
    // last pass ends on the window, so the tail outside it is exactly zero.
    constexpr int kWindowings = 51;
    Eigen::VectorXd s = iceberg::native::irfft(spectrum, n);
    for (int pass = 1; pass <= kWindowings; ++pass) {
        s = time_window(s, fs, range, RangeUnit::Seconds);
        if (pass == kWindowings) {
            break;
        }
        Eigen::VectorXcd bins = iceberg::native::rfft(s);
        for (Eigen::Index i = 0; i < bins.size(); ++i) {
            bins(i) = std::exp(std::complex<double>(0.0, std::arg(bins(i))));
        }
        s = iceberg::native::irfft(bins, n);
    }
    return normalize_peak(s.head(n0));
}

Eigen::VectorXd pink_noise(const Eigen::VectorXd& white, const double fs) {
    const Eigen::Index n = white.size();
    if (n == 0) {
        return Eigen::VectorXd();
    }
    Eigen::VectorXcd spectrum = iceberg::native::rfft(white);
    // DC is skipped: it would divide by sqrt(0) and is set to 0 below anyway.
    for (Eigen::Index i = 1; i < spectrum.size(); ++i) {
        const double f = static_cast<double>(i) * (fs / static_cast<double>(n));
        spectrum(i) = spectrum(i) / std::sqrt(std::complex<double>(0.0, f * 2.0 * kPi));
    }
    spectrum(0) = 0.0;
    return iceberg::native::irfft(spectrum, static_cast<int>(n));
}

}  // namespace iceberg::calibration
