#include "calibration/bandpass.h"

#include "native/fft.h"

#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace iceberg::calibration {

namespace {

constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

}  // namespace

Eigen::MatrixXd butterworth_bandpass_sos(int order, double f_low, double f_high, double fs) {
    if (order <= 0 || order % 2 != 0) {
        throw std::invalid_argument("butterworth_bandpass_sos: order must be even and positive");
    }
    if (!(f_low > 0.0 && f_low < f_high && f_high < fs / 2.0)) {
        throw std::invalid_argument(
            "butterworth_bandpass_sos: need 0 < f_low < f_high < fs/2");
    }
    const int n = order / 2;   // MATLAB butter(n, ...) gives a bandpass of order 2n

    // Prewarped analog edges, MATLAB's fs = 2: u = 2*fs*tan(pi*Wn/fs).
    const double u1 = 4.0 * std::tan(kPi * (f_low / (fs / 2.0)) / 2.0);
    const double u2 = 4.0 * std::tan(kPi * (f_high / (fs / 2.0)) / 2.0);
    const double bw = u2 - u1;
    const double w0_sq = u1 * u2;

    // buttap(n): p = exp(1i*(pi*(1:2:n-1)/(2n) + pi/2)) and conjugates, -1
    // when n is odd. Each maps to the two roots of s^2 - p*Bw*s + W0^2.
    std::vector<cd> analog;
    for (int k = 1; k <= n; ++k) {
        // The middle pole of an odd prototype is -1 exactly, as in buttap.
        const cd p = (2 * k - 1 == n)
                         ? cd(-1.0, 0.0)
                         : std::exp(cd(0.0, kPi * (2.0 * k + n - 1.0) / (2.0 * n)));
        const cd half = p * bw / 2.0;
        const cd root = std::sqrt(half * half - w0_sq);
        analog.push_back(half + root);
        analog.push_back(half - root);
    }

    // Bilinear, MATLAB's form; analog zeros: n at 0 (-> +1), n at infinity (-> -1).
    std::vector<cd> poles;
    cd den(1.0, 0.0);
    for (const cd& s : analog) {
        poles.push_back((1.0 + s / 4.0) / (1.0 - s / 4.0));
        den *= 4.0 - s;
    }
    const double gain = std::pow(bw, n) * std::real(std::pow(4.0, n) / den);

    // One section per conjugate pair; the real poles (a wide band leaves
    // the prototype's real pole as two real ones) pair among themselves.
    Eigen::MatrixXd sos(n, 6);
    std::vector<double> reals;
    Eigen::Index row = 0;
    for (const cd& p : poles) {
        if (std::abs(p.imag()) <= 1e-12 * std::abs(p)) {
            reals.push_back(p.real());
        } else if (p.imag() > 0.0) {
            sos.row(row++) << 1.0, 0.0, -1.0, 1.0, -2.0 * p.real(), std::norm(p);
        }
    }
    for (size_t i = 0; i + 1 < reals.size(); i += 2) {
        sos.row(row++) << 1.0, 0.0, -1.0, 1.0, -(reals[i] + reals[i + 1]),
            reals[i] * reals[i + 1];
    }
    if (row != n) {
        throw std::runtime_error("butterworth_bandpass_sos: pole pairing failed");
    }
    sos.block(0, 0, 1, 3) *= gain;
    return sos;
}

Eigen::VectorXd sosfilt(const Eigen::MatrixXd& sos, const Eigen::VectorXd& x) {
    Eigen::VectorXd y = x;
    for (Eigen::Index s = 0; s < sos.rows(); ++s) {
        const double b0 = sos(s, 0), b1 = sos(s, 1), b2 = sos(s, 2);
        const double a1 = sos(s, 4), a2 = sos(s, 5);
        double w1 = 0.0, w2 = 0.0;
        for (Eigen::Index i = 0; i < y.size(); ++i) {
            const double w = (y(i) - a1 * w1) - a2 * w2;
            y(i) = (b0 * w + b1 * w1) + b2 * w2;
            w2 = w1;
            w1 = w;
        }
    }
    return y;
}

Eigen::VectorXd filter_bandpass(const Eigen::VectorXd& x, double fs, double f_low,
                                double f_high, int order) {
    const Eigen::Index n = x.size();
    Eigen::Index p = 1;
    while (p < n) {
        p *= 2;
    }
    constexpr Eigen::Index kMaxImpulse = Eigen::Index{1} << 15;
    const Eigen::Index nf = p < kMaxImpulse ? p : kMaxImpulse;
    if (n < nf) {
        throw std::invalid_argument(
            "filter_bandpass: below 2^15 samples the length must be a power of two (got " +
            std::to_string(n) + ")");
    }

    const Eigen::MatrixXd sos = butterworth_bandpass_sos(order, f_low, f_high, fs);
    Eigen::VectorXd impulse = Eigen::VectorXd::Zero(nf);
    impulse(0) = 1.0;
    Eigen::VectorXd response = Eigen::VectorXd::Zero(n);
    response.head(nf) = sosfilt(sos, impulse);

    const Eigen::VectorXcd h_spec = native::rfft(response);
    Eigen::VectorXcd spec = native::rfft(x);
    for (Eigen::Index k = 0; k < spec.size(); ++k) {
        // A complex product with a zero imaginary part, as the specification
        // states, not a real scaling: the two differ for non-finite bins.
        spec(k) = spec(k) * cd(std::abs(h_spec(k)), 0.0);
    }
    return native::irfft(spec, static_cast<int>(n));
}

}  // namespace iceberg::calibration
