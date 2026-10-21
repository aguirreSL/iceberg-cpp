#include "calibration/measure.h"

#include "calibration/bandpass.h"
#include "calibration/signals.h"
#include "native/fft.h"
#include "native/linspace.h"
#include "native/pchip.h"
#include "native/resample.h"

#include <cmath>
#include <algorithm>
#include <complex>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace iceberg::calibration {

namespace {

constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

std::string format(const char* fmt, double a, double b = 0.0, double c = 0.0, double d = 0.0,
                   double e = 0.0) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c, d, e);
    return buf;
}

// matlab: real(ifft(fft(x) .* [H; conj(flipud(H(2:end-1)))])) for a real
// one-sided H.
Eigen::VectorXd apply_one_sided(const Eigen::VectorXd& x, const Eigen::VectorXd& h) {
    Eigen::VectorXcd spectrum = iceberg::native::rfft(x);
    spectrum = spectrum.cwiseProduct(h.cast<cd>());
    return iceberg::native::irfft(spectrum, static_cast<int>(x.size()));
}

Eigen::VectorXd bins(Eigen::Index n, double fs) {
    const Eigen::Index n_bins = n / 2 + 1;
    Eigen::VectorXd f(n_bins);
    for (Eigen::Index i = 0; i < n_bins; ++i) {
        f(i) = static_cast<double>(i) * (fs / static_cast<double>(n));
    }
    return f;
}

// The fade both getLevel ends get: 50 ms of Hann at both ends.
Eigen::VectorXd fade_and_band(const Eigen::VectorXd& x, const LevelConfig& c) {
    const Eigen::VectorXd faded = time_window(x, c.fs, {0.05, 0.0}, RangeUnit::Seconds,
                                              iceberg::native::WindowType::Hann, true);
    return filter_bandpass(faded, c.fs, c.lower_freq, c.upper_freq);
}

// One curve of response_check: the band levels in dB about their mean over
// [kCheckLow, kCheckHigh], and half their range there.
constexpr double kCheckLow = 100.0;
constexpr double kCheckHigh = 16000.0;

Eigen::VectorXd relative_band_db(const Eigen::VectorXd& rir, double fs, Eigen::VectorXd* centres,
                                 double* spread) {
    const BandSpectrum b = spk2frequencybands(one_sided_spectrum(rir), rir.size(), fs, 3, 50.0,
                                              20000.0, BandMethod::Averaged);
    Eigen::VectorXd db(b.freq.size());
    double sum = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    int count = 0;
    for (Eigen::Index i = 0; i < b.freq.size(); ++i) {
        const double v = b.freq(i);
        db(i) = (std::isfinite(v) && v > 0.0) ? 20.0 * std::log10(v)
                                              : std::numeric_limits<double>::quiet_NaN();
        const double f = b.freq_vector(i);
        if (f < kCheckLow || f > kCheckHigh || !std::isfinite(db(i))) continue;
        sum += db(i);
        lo = count == 0 ? db(i) : std::min(lo, db(i));
        hi = count == 0 ? db(i) : std::max(hi, db(i));
        ++count;
    }
    if (count == 0) {
        throw std::invalid_argument("response_check: no band between 100 Hz and 16 kHz");
    }
    db.array() -= sum / count;
    *centres = b.freq_vector;
    *spread = (hi - lo) / 2.0;
    return db;
}

}  // namespace

Eigen::VectorXd measure_tf(const Eigen::VectorXd& excitation, double fs, double f_low,
                           double f_high, int averages,
                           const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& play,
                           const BandSpectrum* eq) {
    const Eigen::Index n = excitation.size();
    const Eigen::VectorXd stimulus =
        eq != nullptr
            ? apply_one_sided(excitation,
                              iceberg::native::pchip(eq->freq_vector, eq->freq, bins(n, fs)))
            : excitation;
    // Inverting the plain excitation, not the stimulus, leaves the EQ's
    // effect in the result: that is what the check measurement looks at.
    const Eigen::VectorXcd compensation =
        iceberg::native::rfft(invert_spk_regularization(excitation, fs, f_low, f_high));

    Eigen::VectorXd sum = Eigen::VectorXd::Zero(n);
    for (int k = 0; k < averages; ++k) {
        const Eigen::VectorXd recording = play(stimulus);
        if (recording.size() != n) {
            throw std::runtime_error("measure_tf: the recording has " +
                                     std::to_string(recording.size()) +
                                     " samples, the stimulus " + std::to_string(n));
        }
        sum += recording;
    }
    const Eigen::VectorXd average = sum / static_cast<double>(averages);
    return iceberg::native::irfft(iceberg::native::rfft(average).cwiseProduct(compensation),
                                  static_cast<int>(n));
}

TfResult get_tf(const TfConfig& config, const PlayRecord& io, const Progress& progress) {
    const double amp_lin = std::pow(10.0, config.output_amplification_db / 20.0);
    const Eigen::VectorXd signal = sweep(0.1, config.fs, config.fft_degree);
    const Eigen::Index n = signal.size();

    TfResult out;
    for (int ls = 0; ls < config.n_loudspeakers; ++ls) {
        if (progress) progress("Measuring loudspeaker " + std::to_string(ls + 1));
        const auto play = [&](const Eigen::VectorXd& stim) {
            const Eigen::VectorXd scaled = stim * amp_lin;
            const double peak = scaled.cwiseAbs().maxCoeff();
            if (peak > 1.0) {
                throw MeasurementError(
                    MeasurementError::Kind::Clipping, ls,
                    format("Loudspeaker %.0f: the measurement stimulus would clip (peak %.2f = "
                           "%+.1f dBFS at %+.1f dB output amplification), so nothing was played. "
                           "Lower the output amplification.",
                           ls + 1.0, peak, 20.0 * std::log10(peak), 20.0 * std::log10(amp_lin)));
            }
            const Eigen::MatrixXd page = scaled;
            return Eigen::VectorXd(io(page, {ls}, config.input_channel, config.fs) / amp_lin);
        };

        const Eigen::VectorXd rir = measure_tf(signal, config.fs, 20.0, 24000.0, config.averages, play);
        Eigen::VectorXcd w = one_sided_spectrum(filter_bandpass(rir, config.fs, 20.0, 22627.0));
        w = normalize_spk(smooth_frequency(w, n, config.fs, 1.0));
        Eigen::VectorXcd inv(w.size());
        for (Eigen::Index i = 0; i < w.size(); ++i) {
            const cd v = 1.0 / w(i);
            inv(i) = (std::isfinite(v.real()) && std::isfinite(v.imag())) ? v : cd(0.0, 0.0);
        }
        BandSpectrum bands =
            spk2frequencybands(inv, n, config.fs, 3, 50.0, 22627.0, BandMethod::Averaged);
        bands.freq(bands.freq.size() - 1) = bands.freq(bands.freq.size() - 2);   // as Iceberg MATLAB

        out.rir.push_back(rir);
        out.rir_adjusted.push_back(measure_tf(signal, config.fs, 20.0, 24000.0, 1, play, &bands));
        out.filters.push_back(std::move(bands));
    }
    return out;
}

ResponseCheck response_check(const Eigen::VectorXd& rir, const Eigen::VectorXd& rir_adjusted,
                             double fs) {
    ResponseCheck r;
    Eigen::VectorXd centres_after;
    r.before_db = relative_band_db(rir, fs, &r.centres, &r.before_spread_db);
    r.after_db = relative_band_db(rir_adjusted, fs, &centres_after, &r.after_spread_db);
    return r;
}

Eigen::VectorXd level_excitation_sweep(double fs) {
    return sweep(0.1, fs, 16);
}

Eigen::VectorXd level_excitation_pink(const Eigen::VectorXd& white, double fs, double f_low,
                                      double f_high) {
    const Eigen::VectorXd faded =
        time_window(pink_noise(white, fs), fs, {0.02, 0.0}, RangeUnit::Seconds);
    return filter_bandpass(faded, fs, f_low, f_high);
}

Eigen::VectorXd level_excitation_ltass(const Eigen::VectorXd& speech, double speech_fs,
                                       double fs, const Eigen::VectorXd& white) {
    const Eigen::Index n_speech = speech.size();
    // matlab: seg = 2^floor(log2(min(numel(y), round(10*yFs))))
    const double longest = std::min(static_cast<double>(n_speech), std::round(10.0 * speech_fs));
    const Eigen::Index seg = Eigen::Index{1} << static_cast<int>(std::floor(std::log2(longest)));
    const Eigen::Index n_seg = std::max<Eigen::Index>(1, n_speech / seg);
    Eigen::VectorXd w(seg);
    for (Eigen::Index i = 0; i < seg; ++i) {
        w(i) = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(seg));
    }
    Eigen::VectorXd sum = Eigen::VectorXd::Zero(seg / 2 + 1);
    for (Eigen::Index k = 0; k < n_seg; ++k) {
        const Eigen::VectorXcd y = iceberg::native::rfft(speech.segment(k * seg, seg).cwiseProduct(w));
        for (Eigen::Index i = 0; i < y.size(); ++i) {
            const double a = std::abs(y(i));
            sum(i) += a * a;
        }
    }
    Eigen::VectorXd h = (sum / static_cast<double>(n_seg)).cwiseSqrt();
    h /= h.maxCoeff();

    const Eigen::Index n = white.size();
    Eigen::VectorXd f_ref(seg / 2 + 1);
    for (Eigen::Index i = 0; i < f_ref.size(); ++i) {
        f_ref(i) = static_cast<double>(i) * (speech_fs / static_cast<double>(seg));
    }
    const Eigen::VectorXd h_grid = iceberg::native::pchip(f_ref, h, bins(n, speech_fs));
    Eigen::VectorXd shaped = normalize_peak(apply_one_sided(white, h_grid));
    if (speech_fs != fs) {
        // matlab: resample(x, targetFs, yFs), which reduces p/q first
        const auto [p, q] = iceberg::native::rat(fs / speech_fs, 1e-12);
        shaped = iceberg::native::resample(shaped, p, q).col(0);
    }
    return shaped;
}

LevelResult get_level(const LevelConfig& config, double i_factor,
                      const std::vector<BandSpectrum>& filters,
                      const Eigen::VectorXd& initial_factor, const Eigen::VectorXd& excitation,
                      const PlayRecord& io, const Progress& progress) {
    const int n_ls = config.n_loudspeakers;
    if (static_cast<int>(filters.size()) < n_ls || initial_factor.size() < n_ls) {
        throw std::invalid_argument("get_level: need a filter and a starting factor per loudspeaker");
    }
    const double ls_db_per_volt = 20.0 * std::log10(i_factor / 2e-5);
    const Eigen::VectorXd signal = normalize_peak(excitation);
    const Eigen::Index n = signal.size();
    // matlab: freqVec = linspace(0, iFs/2, nBins)'
    const Eigen::VectorXd freq = iceberg::native::linspace(0.0, config.fs / 2.0, n / 2 + 1);

    std::vector<Eigen::VectorXd> eq(static_cast<size_t>(n_ls));
    for (int k = 0; k < n_ls; ++k) {
        const auto& f = filters[static_cast<size_t>(k)];
        eq[static_cast<size_t>(k)] = iceberg::native::pchip(f.freq_vector, f.freq, freq);
    }

    // The stimulus scale shared by every loudspeaker: matlab
    // (signal ./ (sqrt(mean(signal.^2)) * 2)) .* 10^((level - lsdBperVolt)/20)
    const double scaler = std::sqrt(signal.squaredNorm() / static_cast<double>(n)) * 2.0;
    const double gain = std::pow(10.0, (config.level - ls_db_per_volt) / 20.0);
    const Eigen::VectorXd base = (signal / scaler) * gain;

    LevelResult out;
    out.level_factor = initial_factor.head(n_ls).cwiseAbs();
    out.spl = Eigen::VectorXd::Zero(n_ls);
    out.corrections.assign(static_cast<size_t>(n_ls), 0);
    std::vector<int> channels(static_cast<size_t>(n_ls));
    for (int k = 0; k < n_ls; ++k) channels[static_cast<size_t>(k)] = k;

    const auto record = [&](int ls) {
        Eigen::MatrixXd page = Eigen::MatrixXd::Zero(n, n_ls);
        const Eigen::VectorXd stim =
            apply_one_sided(base, eq[static_cast<size_t>(ls)]) * out.level_factor(ls);
        page.col(ls) = fade_and_band(stim, config);
        const double peak = page.cwiseAbs().maxCoeff();
        if (peak > 1.0) {
            throw MeasurementError(
                MeasurementError::Kind::Clipping, ls,
                format("Loudspeaker %.0f: the stimulus would clip (peak %.3g, full scale 1), so "
                       "nothing was played. The target level is unreachable with this gain "
                       "structure, or the microphone is not picking up the loudspeaker.",
                       ls + 1.0, peak));
        }
        const Eigen::VectorXd rec = io(page, channels, config.input_channel, config.fs);
        const double level = spk2level(fade_and_band(rec, config) * i_factor, BandMethod::Added);
        return 20.0 * std::log10(level / 2e-5);
    };

    const auto measure_average = [&](int ls) {
        double sum = 0.0;
        double avg = 0.0;
        for (int r = 1; r <= config.n_average; ++r) {
            const double spl = record(ls);
            sum += spl;
            avg = sum / static_cast<double>(r);
            if (progress) {
                progress(format("  LS = %.0f n = %.0f  SPL %.2f [dB]  average SPL %.2f [dB]",
                                ls + 1.0, r, spl, avg));
            }
        }
        return avg;
    };

    for (int ls = 0; ls < n_ls; ++ls) {
        double avg = measure_average(ls);
        int corrections = 0;
        while (avg > config.level + config.tolerance_db || avg < config.level - config.tolerance_db) {
            if (corrections >= config.max_iterations) {
                throw MeasurementError(
                    MeasurementError::Kind::NoConvergence, ls,
                    format("Loudspeaker %.0f did not reach %.1f +/- %.1f dB after %.0f "
                           "corrections (last average %.2f dB). Check the microphone, the cabling "
                           "and the target level.",
                           ls + 1.0, config.level, config.tolerance_db, config.max_iterations, avg));
            }
            ++corrections;
            out.level_factor(ls) *= std::pow(10.0, (config.level - avg) / 20.0);
            avg = measure_average(ls);
        }
        out.spl(ls) = avg;
        out.corrections[static_cast<size_t>(ls)] = corrections;
    }
    return out;
}

iceberg::io::CalibrationData to_calibration_data(double i_factor,
                                                 const std::vector<BandSpectrum>& filters,
                                                 const Eigen::VectorXd& level_factor) {
    if (static_cast<Eigen::Index>(filters.size()) != level_factor.size()) {
        throw std::invalid_argument("to_calibration_data: one filter per level factor");
    }
    iceberg::io::CalibrationData cal;
    cal.i_factor = i_factor;
    cal.new_level_factor = level_factor;
    for (size_t k = 0; k < filters.size(); ++k) {
        // matlab: index = k (1-based)  →  c++: k + 1
        cal.loudspeakers.push_back(
            {static_cast<int>(k) + 1, filters[k].freq_vector, filters[k].freq});
    }
    return cal;
}

}  // namespace iceberg::calibration
