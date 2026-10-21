#include "calibration/spectrum.h"

#include "native/fft.h"
#include "native/hann.h"
#include "native/spline.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace iceberg::calibration {

namespace {

using cd = std::complex<double>;

// matlab: (0:nBins-1)' * (fs/n)
Eigen::VectorXd bin_frequencies(Eigen::Index n_bins, Eigen::Index n, double fs) {
    Eigen::VectorXd f(n_bins);
    for (Eigen::Index i = 0; i < n_bins; ++i) {
        f(i) = static_cast<double>(i) * (fs / static_cast<double>(n));
    }
    return f;
}

// MATLAB's builtin interp1 'linear': t = (q - x0)/h, y0*(1 - t) + y1*t;
// NaN outside [x(1), x(end)].
Eigen::VectorXd interp1_linear(const Eigen::VectorXd& x, const Eigen::VectorXd& y,
                               const Eigen::VectorXd& xq) {
    const double* b = x.data();
    const Eigen::Index n = x.size();
    Eigen::VectorXd out(xq.size());
    for (Eigen::Index q = 0; q < xq.size(); ++q) {
        const double v = xq(q);
        if (!(v >= b[0] && v <= b[n - 1])) {
            out(q) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        Eigen::Index k = std::upper_bound(b, b + n, v) - b - 1;
        k = std::min(k, n - 2);
        const double t = (v - b[k]) / (b[k + 1] - b[k]);
        out(q) = y(k) * (1.0 - t) + y(k + 1) * t;
    }
    return out;
}

// Three significant digits of `a` on a grid `m` times coarser than its
// third digit (m = 5: steps of 5 in that digit; m = 100: the leading digit).
double round_significant(double a, double m) {
    if (a == 0.0) return 0.0;
    double d = std::ceil(std::log10(std::abs(a)));
    // ceil(log10) of an exact power of ten is one digit short.
    if (std::abs(a) - std::pow(10.0, d) == 0.0) d += 1.0;
    const double u = std::pow(10.0, 3.0 - d) / m;
    return 1.0 / u * std::round(u * a);
}

// `a` up to `f0`, `b` from `f1`, Hann-blended in between, on the bins `f`
// (spacing f(1) - f(0)).
Eigen::VectorXd crossfade(const Eigen::VectorXd& a, const Eigen::VectorXd& b, double f0,
                          double f1, const Eigen::VectorXd& f) {
    const Eigen::Index n_bins = f.size();
    const double f1_clipped = std::min(f1, f(n_bins - 1));
    const double d = f(1) - f(0);
    const double r0 = std::round(f0 / d);
    const double r1 = std::round(f1_clipped / d);
    // Checked on the doubles so a NaN or out-of-range edge never reaches the
    // integer cast or the indexing below.
    if (!(r0 >= 0.0 && r1 >= r0 && r1 <= static_cast<double>(n_bins - 1))) {
        throw std::invalid_argument("invert_spk_regularization: a crossfade falls outside "
                                    "the spectrum");
    }
    const Eigen::Index j0 = static_cast<Eigen::Index>(r0);
    const Eigen::Index j1 = static_cast<Eigen::Index>(r1);
    const Eigen::Index m = j1 - j0;

    Eigen::ArrayXd up = Eigen::ArrayXd::Constant(1, 0.5);
    Eigen::ArrayXd down = up;
    if (m != 0) {
        const Eigen::ArrayXd w = iceberg::native::hann(static_cast<int>(2 * m + 1));
        up = w.head(m + 1);
        down = w.tail(m + 1);
    }

    Eigen::VectorXd fa = a;
    for (Eigen::Index k = 0; k <= m; ++k) fa(j0 + k) *= down(k);
    for (Eigen::Index i = j1 + 1; i < n_bins; ++i) fa(i) = 0.0;
    Eigen::VectorXd fb = b;
    for (Eigen::Index i = 0; i < j0; ++i) fb(i) = 0.0;
    for (Eigen::Index k = 0; k <= m; ++k) fb(j0 + k) *= up(k);
    return fa + fb;
}

// Centred moving average over an odd `w` points; the window shrinks
// symmetrically at both ends (1, 3, 5, ... points) so the length is kept.
// Each sum runs in the order the reference adds its terms.
Eigen::VectorXd moving_average(const Eigen::VectorXd& y, Eigen::Index w) {
    if (w == 1) return y;
    const Eigen::Index len = y.size();
    Eigen::VectorXd out(len);
    Eigen::Index o = 0;

    double r = 0.0;
    for (Eigen::Index i = 0; i <= w - 3; ++i) {
        r += y(i);
        if (i % 2 == 0) out(o++) = r / static_cast<double>(i + 1);
    }

    const double q = 1.0 / static_cast<double>(w);
    for (Eigen::Index k = w - 1; k <= len - 1; ++k) {
        double z = q * y(k - w + 1);
        for (Eigen::Index j = w - 2; j >= 0; --j) z = q * y(k - j) + z;
        out(o++) = z;
    }

    Eigen::VectorXd t(w - 2);
    double s = 0.0;
    for (Eigen::Index i = 0; i <= w - 3; ++i) {
        s += y(len - 1 - i);
        t(i) = s;
    }
    for (Eigen::Index i = w - 3; i >= 0; i -= 2) out(o++) = t(i) / static_cast<double>(i + 1);
    return out;
}

}  // namespace

MicFactor mic_calibration_factor(const Eigen::VectorXd& reference, double fs, double f_ref) {
    const Eigen::Index n = reference.size();
    if (n < 2) {
        throw std::invalid_argument("mic_calibration_factor: the recording is too short");
    }
    if (!(f_ref >= 0.0 && f_ref <= fs / 2.0)) {
        throw std::invalid_argument("mic_calibration_factor: f_ref outside [0, fs/2]");
    }
    const Eigen::VectorXcd x = iceberg::native::rfft(reference);
    const Eigen::VectorXd fv = bin_frequencies(x.size(), n, fs);
    // matlab: abs(X1(1:nB)) * sqrt(2)/n
    const Eigen::VectorXd mag = (x.cwiseAbs() * std::sqrt(2.0)) / static_cast<double>(n);
    const double v1k = interp1_linear(fv, mag, Eigen::VectorXd::Constant(1, f_ref))(0);
    return {1.0 / v1k, v1k};
}

double mic_factor_for_calibrator(double v1k, double calibrator_db) {
    return std::pow(10.0, (calibrator_db - 94.0) / 20.0) / v1k;
}

CalibratorCheck check_calibrator_recording(const Eigen::VectorXd& recording, double fs,
                                           double f_ref) {
    constexpr double kClip = 0.999;
    constexpr double kLowDbfs = -60.0;
    constexpr double kFreqTolerance = 0.02;
    constexpr double kToneBand = 0.05;
    constexpr double kMinToneFraction = 0.9;
    constexpr double kMaxSpreadDb = 0.5;
    constexpr double kWindowSec = 0.5;

    CalibratorCheck c;
    const Eigen::Index n = recording.size();
    if (n < 2) {
        c.warnings.push_back("The recording is empty.");
        return c;
    }
    const auto db = [](double v) { return 20.0 * std::log10(std::max(v, 1e-300)); };
    const double peak = recording.cwiseAbs().maxCoeff();
    const double rms = std::sqrt(recording.squaredNorm() / static_cast<double>(n));
    c.peak_dbfs = db(peak);
    c.rms_dbfs = db(rms);

    const Eigen::VectorXcd x = iceberg::native::rfft(recording);
    const Eigen::VectorXd f = bin_frequencies(x.size(), n, fs);
    double total = 0.0, tone = 0.0, strongest = -1.0;
    for (Eigen::Index i = 1; i < x.size(); ++i) {
        const double p = std::norm(x(i));
        total += p;
        if (std::abs(f(i) - f_ref) <= kToneBand * f_ref) tone += p;
        if (p > strongest) {
            strongest = p;
            c.strongest_hz = f(i);
        }
    }
    c.tone_fraction = total > 0.0 ? tone / total : 0.0;

    const Eigen::Index w = std::max<Eigen::Index>(1, static_cast<Eigen::Index>(kWindowSec * fs));
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (Eigen::Index s = 0; s + w <= n; s += w) {
        const double l = db(std::sqrt(recording.segment(s, w).squaredNorm() / static_cast<double>(w)));
        lo = std::min(lo, l);
        hi = std::max(hi, l);
    }
    c.level_spread_db = hi >= lo ? hi - lo : 0.0;

    char buf[256];
    if (peak >= kClip) {
        c.warnings.push_back("The recording reaches full scale (clipping): lower the input gain "
                             "and record again.");
    }
    if (c.rms_dbfs < kLowDbfs) {
        std::snprintf(buf, sizeof(buf),
                      "The recording is very low (%.1f dBFS RMS): raise the input gain, or check "
                      "that the microphone sits in the calibrator.", c.rms_dbfs);
        c.warnings.push_back(buf);
    }
    if (std::abs(c.strongest_hz - f_ref) > kFreqTolerance * f_ref) {
        std::snprintf(buf, sizeof(buf),
                      "The strongest frequency is %.0f Hz, not %.0f Hz: check the calibrator and "
                      "the microphone input channel.", c.strongest_hz, f_ref);
        c.warnings.push_back(buf);
    }
    if (c.tone_fraction < kMinToneFraction) {
        std::snprintf(buf, sizeof(buf),
                      "Only %.0f %% of the recording's power is near %.0f Hz: background noise or "
                      "another sound can bias the factor.", 100.0 * c.tone_fraction, f_ref);
        c.warnings.push_back(buf);
    }
    if (c.level_spread_db > kMaxSpreadDb) {
        std::snprintf(buf, sizeof(buf),
                      "The level changed by %.1f dB during the recording: keep the calibrator "
                      "still and switched on, then record again.", c.level_spread_db);
        c.warnings.push_back(buf);
    }
    return c;
}

Eigen::VectorXd one_sided_scale(Eigen::Index n) {
    const double nd = static_cast<double>(n);
    const Eigen::Index n_bins = n / 2 + 1;
    Eigen::VectorXd s = Eigen::VectorXd::Constant(n_bins, std::sqrt(2.0) / nd);
    s(0) = 1.0 / nd;
    // Last, so that a single bin takes this value rather than DC's.
    s(n_bins - 1) = n % 2 == 0 ? std::sqrt(2.0) / (2.0 * nd) : std::sqrt(2.0) / nd;
    return s;
}

Eigen::VectorXcd one_sided_spectrum(const Eigen::VectorXd& x) {
    Eigen::VectorXcd spk = iceberg::native::rfft(x);
    const Eigen::VectorXd s = one_sided_scale(x.size());
    for (Eigen::Index i = 0; i < spk.size(); ++i) spk(i) = spk(i) * cd(s(i), 0.0);
    return spk;
}

double spk2level(const Eigen::VectorXd& x, BandMethod method) {
    const Eigen::VectorXcd spk = one_sided_spectrum(x);
    double p = 0.0;
    for (Eigen::Index i = 0; i < spk.size(); ++i) p += std::norm(spk(i));
    if (method == BandMethod::Added) return std::sqrt(p);
    return std::sqrt(p / static_cast<double>(spk.size()));
}

CenterFrequencies ansi_center_frequencies(double f_low, double f_high, int bands_per_octave,
                                          double fs) {
    if (bands_per_octave < 1) {
        throw std::invalid_argument("ansi_center_frequencies: bands_per_octave must be >= 1");
    }
    const double b = static_cast<double>(bands_per_octave);
    const double hi = std::min(f_high, fs / 2.0 * std::pow(2.0, -1.0 / b / 2.0));
    const double kmax_d = b * std::log2(hi / 1000.0) + 1.0;
    const double kmin_d = b * std::log2(1000.0 / f_low) + 1.0;
    // A zero or negative limit makes these infinite or NaN, and the band
    // loop below would never end.
    if (!std::isfinite(kmax_d) || !std::isfinite(kmin_d)) {
        throw std::invalid_argument("ansi_center_frequencies: the limits must be positive");
    }
    const long kmax = std::lround(kmax_d);
    const long kmin = std::lround(kmin_d);
    const double lower_limit = f_low * std::pow(2.0, -0.5 / b);
    const double upper_limit = hi * std::pow(2.0, 0.5 / b);

    std::vector<double> nominal;
    std::vector<double> exact;
    for (long k = -kmin; k <= kmax; ++k) {
        const double ex = 1000.0 * std::pow(2.0, static_cast<double>(k) / b);
        if (!(ex > lower_limit && ex < upper_limit)) continue;
        const double g1 = round_significant(ex, 5.0);
        const double g2 = round_significant(ex, 100.0);
        nominal.push_back(std::abs(100.0 * (1.0 - g1 / g2)) < 4.0 ? g2 : g1);
        exact.push_back(ex);
    }

    CenterFrequencies c;
    const Eigen::Index count = static_cast<Eigen::Index>(exact.size());
    c.nominal = Eigen::Map<const Eigen::VectorXd>(nominal.data(), count);
    c.exact = Eigen::Map<const Eigen::VectorXd>(exact.data(), count);
    return c;
}

Eigen::VectorXd invert_spk_regularization(const Eigen::VectorXd& x, double fs, double f_low,
                                          double f_high, double beta) {
    const Eigen::Index n = x.size();
    if (n < 2) {
        throw std::invalid_argument("invert_spk_regularization: fewer than 2 samples");
    }
    const Eigen::VectorXcd spk = iceberg::native::rfft(x);
    const Eigen::Index n_bins = spk.size();
    const Eigen::VectorXd f = bin_frequencies(n_bins, n, fs);
    const Eigen::VectorXd ones = Eigen::VectorXd::Ones(n_bins);

    Eigen::VectorXd e = crossfade(ones, beta * ones, f_low / std::sqrt(2.0), f_low, f);
    const double top = std::min(f_high * std::sqrt(2.0), fs / 2.0);
    if (f_high < top) e = crossfade(e, ones, f_high, top, f);

    double p = 0.0;
    for (Eigen::Index i = 0; i < n_bins; ++i) p = std::max(p, std::abs(spk(i)));
    const double half_peak_power = p * p * 50.0 / 100.0;
    for (Eigen::Index i = 0; i < n_bins; ++i) e(i) = e(i) * e(i) * half_peak_power;
    if (e(0) > 0.0) e(0) = e(0) * 2.0;

    const Eigen::VectorXd s = one_sided_scale(n);
    Eigen::VectorXcd inv(n_bins);
    for (Eigen::Index i = 0; i < n_bins; ++i) {
        inv(i) = (std::conj(spk(i)) / (std::norm(spk(i)) + e(i))) / s(i);
    }
    return iceberg::native::irfft(inv, static_cast<int>(n));
}

Eigen::VectorXcd normalize_spk(const Eigen::VectorXcd& freq_data) {
    double peak = 0.0;
    for (Eigen::Index i = 0; i < freq_data.size(); ++i) {
        peak = std::max(peak, std::abs(freq_data(i)));
    }
    Eigen::VectorXcd out(freq_data.size());
    for (Eigen::Index i = 0; i < freq_data.size(); ++i) out(i) = freq_data(i) / peak;
    return out;
}

BandSpectrum spk2frequencybands(const Eigen::VectorXcd& freq_data, Eigen::Index n, double fs,
                                int bands_per_octave, double f_low, double f_high,
                                BandMethod method) {
    const Eigen::Index n_bins = n / 2 + 1;
    if (n < 0 || freq_data.size() != n_bins) {
        throw std::invalid_argument("spk2frequencybands: freq_data must hold n/2 + 1 bins");
    }
    const Eigen::VectorXd f = bin_frequencies(n_bins, n, fs);
    const double lo = std::max(f_low, f(0));
    const double hi = std::min(f_high, f(n_bins - 1));
    const CenterFrequencies c = ansi_center_frequencies(lo, hi, bands_per_octave, fs);
    const double e = std::pow(2.0, 1.0 / (2.0 * static_cast<double>(bands_per_octave)));

    BandSpectrum out;
    out.freq_vector = c.nominal;
    out.freq.resize(c.exact.size());
    const double* fb = f.data();
    const double* fe = fb + n_bins;
    for (Eigen::Index band = 0; band < c.exact.size(); ++band) {
        const double lower = c.exact(band) / e;
        const double upper = c.exact(band) * e;
        // f rises with the index: the first bin >= lower, the last bin < upper.
        const Eigen::Index first = std::lower_bound(fb, fe, lower) - fb;
        const Eigen::Index last = (std::lower_bound(fb, fe, upper) - fb) - 1;
        if (last < first) {
            out.freq(band) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        double p = 0.0;
        for (Eigen::Index i = first; i <= last; ++i) p += std::norm(freq_data(i));
        if (method == BandMethod::Averaged) p /= static_cast<double>(last - first + 1);
        out.freq(band) = std::sqrt(p);
    }
    return out;
}

Eigen::VectorXcd smooth_frequency(const Eigen::VectorXcd& freq_data, Eigen::Index n, double fs,
                                  double window_width) {
    const Eigen::Index n_bins = n / 2 + 1;
    if (n < 0 || freq_data.size() != n_bins) {
        throw std::invalid_argument("smooth_frequency: freq_data must hold n/2 + 1 bins");
    }
    const Eigen::VectorXd f = bin_frequencies(n_bins, n, fs);
    Eigen::VectorXd mag(n_bins);
    for (Eigen::Index i = 0; i < n_bins; ++i) mag(i) = std::abs(freq_data(i));

    constexpr Eigen::Index kGridPoints = 24001;   // one per cent over 20 octaves
    const double fmin = (fs / 2.0) * std::pow(2.0, -20.0);
    Eigen::VectorXd g(kGridPoints);
    for (Eigen::Index c = 0; c < kGridPoints; ++c) {
        g(c) = fmin * std::pow(2.0, static_cast<double>(c) / 1200.0);
    }
    const Eigen::VectorXd y = interp1_linear(f, mag, g);

    const double w0_d = std::round(window_width * 1200.0);
    if (!(w0_d >= 1.0)) {
        throw std::invalid_argument("smooth_frequency: window_width is below one grid point");
    }
    // Clamped before the cast only to keep it in range: every width from
    // the grid length up ends as the grid length (odd) anyway.
    const Eigen::Index w0 = w0_d >= static_cast<double>(kGridPoints)
                                ? kGridPoints
                                : static_cast<Eigen::Index>(w0_d);
    const Eigen::Index w = std::min(w0 - 1 + (w0 % 2), y.size());
    const Eigen::VectorXd smoothed = moving_average(y, w);

    Eigen::VectorXd out_mag(n_bins);
    out_mag(0) = mag(0);
    out_mag.tail(n_bins - 1) = iceberg::native::spline(
        g.tail(kGridPoints - 1), smoothed.tail(kGridPoints - 1), f.tail(n_bins - 1));

    Eigen::VectorXcd out(n_bins);
    for (Eigen::Index i = 0; i < n_bins; ++i) {
        out(i) = out_mag(i) * std::exp(cd(0.0, std::arg(freq_data(i))));
    }
    return out;
}

}  // namespace iceberg::calibration
