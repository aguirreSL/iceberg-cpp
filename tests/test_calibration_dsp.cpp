#include "calibration/bandpass.h"
#include "calibration/signals.h"
#include "calibration/spectrum.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>
#include <complex>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Tests the signal-processing primitives of the room calibration (time windows,
// excitation signals, band-pass filter, band levels, microphone factor, calibrator
// check, Kirkeby inverse, getTF filter chain) against 64-bit reference data generated
// by Iceberg MATLAB v1.0.0 (calibration/); the usual limit is 1e-10 of the reference peak.

namespace {

namespace cal = iceberg::calibration;
using iceberg::test::fixture_file;
using iceberg::test::load_wav_f64;
using iceberg::test::record_measure;
using iceberg::test::rel_peak_diff;

constexpr double kFs = 48000.0;
constexpr double kTol = 1e-10;

const nlohmann::json& refs() {
    static const nlohmann::json j = iceberg::test::load_fixture_json("calibration/references.json");
    return j;
}

Eigen::VectorXd mono(const std::string& name) {
    return load_wav_f64("calibration/" + name).col(0);
}

// A (re, im) two-column reference as a complex vector.
Eigen::VectorXcd complex_ref(const std::string& name) {
    const Eigen::MatrixXd m = load_wav_f64("calibration/" + name);
    Eigen::VectorXcd v(m.rows());
    for (Eigen::Index i = 0; i < m.rows(); ++i) v(i) = {m(i, 0), m(i, 1)};
    return v;
}

double rel_peak_diff_c(const Eigen::VectorXcd& out, const Eigen::VectorXcd& ref) {
    if (out.size() != ref.size()) return std::numeric_limits<double>::infinity();
    return (out - ref).cwiseAbs().maxCoeff() / ref.cwiseAbs().maxCoeff();
}

// Checks time_window on the reference noise for four settings given in seconds
// (fade-ins, a fade-in combined with a fade-out, and a symmetric Hann fade at both
// ends). Each output is compared with the Iceberg MATLAB v1.0.0 reference and must
// agree within 1e-15 of the reference peak, that is, to double-precision rounding.
TEST(CalibrationDsp, TimeWindowsMatchMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const Eigen::VectorXd x = mono("noise.wav");
    for (const auto& w : refs().at("windows")) {
        const auto range = w.at("range").get<std::vector<double>>();
        const bool symmetric = !w.at("options").empty();   // {'windowType','hann','symmetric',true}
        const Eigen::VectorXd out = cal::time_window(x, kFs, range, cal::RangeUnit::Seconds,
                                                     iceberg::native::WindowType::Hann, symmetric);
        const double d = rel_peak_diff(out, mono(w.at("file").get<std::string>()));
        record_measure(w.at("file").get<std::string>(), d, 1e-15, "rel");
        EXPECT_LT(d, 1e-15) << w.at("file");
    }
}

// Checks the excitation signals of the calibration: the getLevel sweep (2^16
// samples), the getTF sweep (2^15 samples) and pink noise shaped from a stored
// white-noise draw. Each is compared with the Iceberg MATLAB v1.0.0 reference and
// must agree within 1e-10 of the reference peak.
TEST(CalibrationDsp, ExcitationsMatchMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const double d_level = rel_peak_diff(cal::sweep(0.1, kFs, 16), mono("sweep_getlevel.wav"));
    const double d_tf = rel_peak_diff(cal::sweep(0.1, kFs, 15), mono("sweep_gettf.wav"));
    const double d_pink = rel_peak_diff(cal::pink_noise(mono("pink_white.wav"), kFs), mono("pink.wav"));
    record_measure("sweep getLevel (2^16)", d_level, kTol, "rel");
    record_measure("sweep getTF (2^15)", d_tf, kTol, "rel");
    record_measure("pink noise", d_pink, kTol, "rel");
    EXPECT_LT(d_level, kTol);
    EXPECT_LT(d_tf, kTol);
    EXPECT_LT(d_pink, kTol);
}

// Checks that sweep() rejects a stop margin of 1 s for a 2^15-sample sweep at
// 48 kHz (0.68 s long), for which the final group delay would fall below 30 ms:
// std::invalid_argument is expected.
TEST(CalibrationDsp, SweepRejectsATooLargeStopMargin) {
    EXPECT_THROW(cal::sweep(1.0, kFs, 15), std::invalid_argument);   // 2^15/48k = 0.68 s
}

// Checks the 10th-order Butterworth band-pass on three bands (60 Hz to 20 kHz, 20 Hz
// to 22.6 kHz, 50 Hz to 16 kHz): the magnitude response of its sections, in 5 % steps
// from 5 Hz to fs/2, must match MATLAB's zp2sos(butter(...)) within 1e-10, and the
// zero-phase filtered noise the Iceberg MATLAB v1.0.0 output within 1e-10 of its peak.
TEST(CalibrationDsp, BandpassMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const Eigen::VectorXd x = mono("noise.wav");
    for (const auto& b : refs().at("bandpass")) {
        const double lo = b.at("lower").get<double>();
        const double hi = b.at("upper").get<double>();
        const Eigen::MatrixXd sos = cal::butterworth_bandpass_sos(10, lo, hi, kFs);
        const auto ref_sos = b.at("sos").get<std::vector<std::vector<double>>>();
        ASSERT_EQ(sos.rows(), static_cast<Eigen::Index>(ref_sos.size()));
        double worst_h = 0.0;
        for (double f = 5.0; f < kFs / 2.0; f *= 1.05) {
            const std::complex<double> z = std::polar(1.0, -2.0 * 3.14159265358979323846 * f / kFs);
            std::complex<double> h_out = 1.0, h_ref = 1.0;
            for (Eigen::Index r = 0; r < sos.rows(); ++r) {
                const auto& q = ref_sos[static_cast<size_t>(r)];
                h_out *= (sos(r, 0) + sos(r, 1) * z + sos(r, 2) * z * z) /
                         (sos(r, 3) + sos(r, 4) * z + sos(r, 5) * z * z);
                h_ref *= (q[0] + q[1] * z + q[2] * z * z) / (q[3] + q[4] * z + q[5] * z * z);
            }
            worst_h = std::max(worst_h, std::abs(std::abs(h_out) - std::abs(h_ref)));
        }
        const double d = rel_peak_diff(cal::filter_bandpass(x, kFs, lo, hi),
                                       mono(b.at("file").get<std::string>()));
        record_measure(b.at("file").get<std::string>() + " |H|", worst_h, kTol, "abs");
        record_measure(b.at("file").get<std::string>(), d, kTol, "rel");
        EXPECT_LT(worst_h, kTol) << b.at("file");
        EXPECT_LT(d, kTol) << b.at("file");
    }
}

// Checks the length precondition of filter_bandpass: a 1000-sample signal (shorter
// than 2^15 samples and not a power of two) is rejected with std::invalid_argument,
// while a 1024-sample signal is accepted.
TEST(CalibrationDsp, BandpassRejectsAShortNonPowerOfTwoSignal) {
    EXPECT_THROW(cal::filter_bandpass(Eigen::VectorXd::Ones(1000), kFs, 60.0, 20000.0),
                 std::invalid_argument);
    EXPECT_NO_THROW(cal::filter_bandpass(Eigen::VectorXd::Ones(1024), kFs, 60.0, 20000.0));
}

// Checks getLevel's second excitation built from its parts: the reference pink noise
// with a 20 ms Hann fade-in, band-passed from 60 Hz to 20 kHz. The result is compared
// with the Iceberg MATLAB v1.0.0 reference and must agree within 1e-10 of its peak.
TEST(CalibrationDsp, GetLevelPinkMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const Eigen::VectorXd faded = cal::time_window(mono("pink.wav"), kFs, {0.02, 0.0},
                                                   cal::RangeUnit::Seconds);
    const double d = rel_peak_diff(cal::filter_bandpass(faded, kFs, 60.0, 20000.0),
                                   mono("pink_getlevel.wav"));
    record_measure("pink_getlevel", d, kTol, "rel");
    EXPECT_LT(d, kTol);
}

// Checks spk2level (the RMS level from the one-sided spectrum, bin powers summed or
// averaged) on the reference noise within 1e-13 relative, and the 1/3-octave band
// centres from 50 Hz to 22627 Hz at 48 kHz against Iceberg MATLAB v1.0.0: nominal
// labels equal to the reference, exact centres within 1e-12 relative.
TEST(CalibrationDsp, LevelsAndBandCentresMatchMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const Eigen::VectorXd x = mono("noise.wav");
    const auto& lvl = refs().at("spk2level");
    const double ref_added = lvl.at("added").get<double>();
    const double ref_averaged = lvl.at("averaged").get<double>();
    record_measure("spk2level added",
                   std::abs(cal::spk2level(x, cal::BandMethod::Added) - ref_added) / ref_added,
                   1e-13, "rel");
    record_measure(
        "spk2level averaged",
        std::abs(cal::spk2level(x, cal::BandMethod::Averaged) - ref_averaged) / ref_averaged,
        1e-13, "rel");
    EXPECT_NEAR(cal::spk2level(x, cal::BandMethod::Added), lvl.at("added").get<double>(),
                1e-13 * lvl.at("added").get<double>());
    EXPECT_NEAR(cal::spk2level(x, cal::BandMethod::Averaged), lvl.at("averaged").get<double>(),
                1e-13 * lvl.at("averaged").get<double>());

    const auto& ansi = refs().at("ansi");
    const auto lim = ansi.at("limits").get<std::vector<double>>();
    const auto c = cal::ansi_center_frequencies(lim[0], lim[1], ansi.at("bandsPerOctave").get<int>(),
                                                kFs);
    const auto nominal = ansi.at("nominal").get<std::vector<double>>();
    const auto exact = ansi.at("exact").get<std::vector<double>>();
    ASSERT_EQ(c.nominal.size(), static_cast<Eigen::Index>(nominal.size()));
    double worst_exact = 0.0;
    for (size_t i = 0; i < nominal.size(); ++i) {
        EXPECT_DOUBLE_EQ(c.nominal(static_cast<Eigen::Index>(i)), nominal[i]) << i;
        EXPECT_NEAR(c.exact(static_cast<Eigen::Index>(i)), exact[i], 1e-12 * exact[i]) << i;
        worst_exact = std::max(
            worst_exact, std::abs(c.exact(static_cast<Eigen::Index>(i)) - exact[i]) / exact[i]);
    }
    record_measure("exact band centres, worst", worst_exact, 1e-12, "rel");
}

// Checks the microphone calibration factor from the reference 1 kHz calibrator tone:
// the tone RMS (v1k) must match Iceberg MATLAB v1.0.0 within 1e-14 and iFactor (1/v1k,
// pascal per digital unit) within 1e-11, both absolute. A reference frequency of
// 30 kHz, above fs/2, must be rejected with std::invalid_argument.
TEST(CalibrationDsp, MicFactorMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto& mic = refs().at("mic");
    const auto m = cal::mic_calibration_factor(mono(mic.at("file").get<std::string>()), kFs,
                                               mic.at("fRef").get<double>());
    record_measure("v1k", std::abs(m.v1k - mic.at("v1k").get<double>()), 1e-14, "abs");
    record_measure("iFactor", std::abs(m.i_factor - mic.at("iFactor").get<double>()), 1e-11, "abs");
    EXPECT_NEAR(m.v1k, mic.at("v1k").get<double>(), 1e-14);
    EXPECT_NEAR(m.i_factor, mic.at("iFactor").get<double>(), 1e-11);
    EXPECT_THROW(cal::mic_calibration_factor(Eigen::VectorXd::Ones(64), kFs, 30000.0),
                 std::invalid_argument);
}

// Checks the microphone factor for a calibrator of a given level, with v1k = 0.05:
// at 94 dB SPL it equals 1/v1k exactly, as Iceberg MATLAB computes it, and at
// 114 dB SPL it equals 10/v1k (20 dB more) within 1e-12.
TEST(CalibrationDsp, MicFactorForOtherCalibrators) {
    EXPECT_EQ(cal::mic_factor_for_calibrator(0.05, 94.0), 1.0 / 0.05);   // MATLAB's, exactly
    record_measure("114 dB calibrator",
                   std::abs(cal::mic_factor_for_calibrator(0.05, 114.0) - 10.0 / 0.05), 1e-12,
                   "abs");
    EXPECT_NEAR(cal::mic_factor_for_calibrator(0.05, 114.0), 10.0 / 0.05, 1e-12);
}

// A 10 s calibrator recording at 48 kHz: 1 kHz at -20 dBFS RMS.
Eigen::VectorXd calibrator_tone(double hz = 1000.0, double rms = 0.1) {
    Eigen::VectorXd t(480000);
    for (Eigen::Index i = 0; i < t.size(); ++i) {
        t(i) = rms * std::sqrt(2.0) * std::sin(2.0 * 3.14159265358979323846 * hz *
                                               static_cast<double>(i) / kFs);
    }
    return t;
}

bool has_warning(const cal::CalibratorCheck& c, const std::string& fragment) {
    for (const auto& w : c.warnings) {
        if (w.find(fragment) != std::string::npos) return true;
    }
    return false;
}

// Checks the calibrator-recording check on a synthetic 10 s, 1 kHz sine at -20 dBFS
// RMS: no warning is raised, the RMS is -20 dBFS within 1e-6 dB, the strongest line is
// at 1 kHz within 0.1 Hz, more than 99.9 % of the power lies near 1 kHz, and the level
// varies by less than 1e-6 dB over the recording.
TEST(CalibrationDsp, CalibratorCheckPassesACleanTone) {
    const auto c = cal::check_calibrator_recording(calibrator_tone(), kFs);
    EXPECT_TRUE(c.warnings.empty()) << c.warnings.front();
    record_measure("RMS level vs -20 dBFS", std::abs(c.rms_dbfs + 20.0), 1e-6, "dB");
    record_measure("strongest frequency vs 1 kHz", std::abs(c.strongest_hz - 1000.0), 0.1, "abs");
    record_measure("energy outside the tone", 1.0 - c.tone_fraction, 1.0 - 0.999, "rel");
    record_measure("level spread", c.level_spread_db, 1e-6, "dB");
    EXPECT_NEAR(c.rms_dbfs, -20.0, 1e-6);
    EXPECT_NEAR(c.strongest_hz, 1000.0, 0.1);
    EXPECT_GT(c.tone_fraction, 0.999);
    EXPECT_LT(c.level_spread_db, 1e-6);
}

// Checks that the calibrator-recording check warns on each synthetic defect of a tone:
// clipping at full scale, a very low level (-80 dBFS), a 250 Hz tone instead of 1 kHz,
// added noise at about 6 dB SNR (too little power near 1 kHz, while the strongest line
// is still found at 1 kHz), and a level step of 1.6 dB halfway through.
TEST(CalibrationDsp, CalibratorCheckWarnsOnEachDefect) {
    Eigen::VectorXd clipped = calibrator_tone(1000.0, 0.9).cwiseMax(-1.0).cwiseMin(1.0);
    EXPECT_TRUE(has_warning(cal::check_calibrator_recording(clipped, kFs), "clipping"));

    EXPECT_TRUE(has_warning(cal::check_calibrator_recording(calibrator_tone(1000.0, 1e-4), kFs),
                            "very low"));
    EXPECT_TRUE(has_warning(cal::check_calibrator_recording(calibrator_tone(250.0), kFs),
                            "strongest frequency is 250 Hz"));

    Eigen::VectorXd noisy = calibrator_tone();
    std::mt19937 rng(3);
    std::normal_distribution<double> n01(0.0, 1.0);
    for (Eigen::Index i = 0; i < noisy.size(); ++i) noisy(i) += 0.05 * n01(rng);   // ~6 dB SNR
    const auto c_noisy = cal::check_calibrator_recording(noisy, kFs);
    EXPECT_TRUE(has_warning(c_noisy, "near 1000 Hz")) << c_noisy.tone_fraction;
    EXPECT_FALSE(has_warning(c_noisy, "strongest frequency"));

    Eigen::VectorXd step = calibrator_tone();
    step.tail(step.size() / 2) *= 1.2;   // +1.6 dB halfway
    EXPECT_TRUE(has_warning(cal::check_calibrator_recording(step, kFs), "changed by 1.6 dB"));
}

// Checks the regularised (Kirkeby) inverse of the reference getTF sweep over 20 Hz to
// 24 kHz: the result is compared with the Iceberg MATLAB v1.0.0 reference and must
// agree within 1e-10 of the reference peak.
TEST(CalibrationDsp, KirkebyInverseMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto& k = refs().at("kirkeby");
    const auto range = k.at("freqRange").get<std::vector<double>>();
    const double d = rel_peak_diff(
        cal::invert_spk_regularization(mono(k.at("input").get<std::string>()), kFs, range[0],
                                       range[1]),
        mono(k.at("file").get<std::string>()));
    record_measure("kirkeby", d, kTol, "rel");
    EXPECT_LT(d, kTol);
}

// Checks getTF's filter chain for loudspeaker 1: band-pass, one-sided spectrum,
// one-octave smoothing, normalisation and the 1/3-octave inverse filter, each stage fed
// with the Iceberg MATLAB v1.0.0 input to it, then the whole chain from the measured
// RIR. Every output must agree with the reference within 1e-10 of its peak.
TEST(CalibrationDsp, GetTFChainMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const Eigen::VectorXd rir = mono("tf_rir_ls1.wav");
    const Eigen::Index n = rir.size();
    const Eigen::VectorXd bandpassed = mono("tf_chain_bandpassed.wav");
    const Eigen::VectorXcd freqdata = complex_ref("tf_chain_freqdata.wav");
    const Eigen::VectorXcd smoothed = complex_ref("tf_chain_smoothed.wav");
    const Eigen::VectorXcd normalized = complex_ref("tf_chain_normalized.wav");

    const double d_bp = rel_peak_diff(cal::filter_bandpass(rir, kFs, 20.0, 22627.0), bandpassed);
    const double d_fd = rel_peak_diff_c(cal::one_sided_spectrum(bandpassed), freqdata);
    const double d_sm = rel_peak_diff_c(cal::smooth_frequency(freqdata, n, kFs, 1.0), smoothed);
    const double d_nm = rel_peak_diff_c(cal::normalize_spk(smoothed), normalized);
    record_measure("getTF chain: bandpass", d_bp, kTol, "rel");
    record_measure("getTF chain: one-sided spectrum", d_fd, kTol, "rel");
    record_measure("getTF chain: smoothing", d_sm, kTol, "rel");
    record_measure("getTF chain: normalisation", d_nm, kTol, "rel");
    EXPECT_LT(d_bp, kTol);
    EXPECT_LT(d_fd, kTol);
    EXPECT_LT(d_sm, kTol);
    EXPECT_LT(d_nm, kTol);

    // The 1/3-octave filter: the inverse of the normalised spectrum (0
    // where it is not finite), band-averaged, last band patched (as getTF).
    const auto filter_from = [&](const Eigen::VectorXcd& norm) {
        Eigen::VectorXcd inv(norm.size());
        for (Eigen::Index i = 0; i < norm.size(); ++i) {
            const std::complex<double> v = 1.0 / norm(i);
            inv(i) = std::isfinite(v.real()) && std::isfinite(v.imag()) ? v : 0.0;
        }
        auto bands = cal::spk2frequencybands(inv, n, kFs, 3, 50.0, 22627.0,
                                             cal::BandMethod::Averaged);
        bands.freq(bands.freq.size() - 1) = bands.freq(bands.freq.size() - 2);
        return bands;
    };
    const auto& ref = refs().at("getTF").at("filters").at(0);
    const auto ref_f = ref.at("freq").get<std::vector<double>>();
    const auto ref_v = ref.at("freqVector").get<std::vector<double>>();
    const Eigen::VectorXd ref_freq =
        Eigen::Map<const Eigen::VectorXd>(ref_f.data(), static_cast<Eigen::Index>(ref_f.size()));

    const auto stage = filter_from(normalized);
    const auto chained = filter_from(cal::normalize_spk(cal::smooth_frequency(
        cal::one_sided_spectrum(cal::filter_bandpass(rir, kFs, 20.0, 22627.0)), n, kFs, 1.0)));
    ASSERT_EQ(stage.freq_vector.size(), static_cast<Eigen::Index>(ref_v.size()));
    for (size_t i = 0; i < ref_v.size(); ++i) {
        EXPECT_DOUBLE_EQ(stage.freq_vector(static_cast<Eigen::Index>(i)), ref_v[i]) << i;
    }
    const double d_bands = rel_peak_diff(stage.freq, ref_freq);
    const double d_chain = rel_peak_diff(chained.freq, ref_freq);
    record_measure("getTF chain: 1/3-octave filter from MATLAB's normalised spectrum", d_bands,
                   kTol, "rel");
    record_measure("getTF chain: 1/3-octave filter, whole chain from the RIR", d_chain, kTol,
                   "rel");
    EXPECT_LT(d_bands, kTol);
    EXPECT_LT(d_chain, kTol);
}

}  // namespace
