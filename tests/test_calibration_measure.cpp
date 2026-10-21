#include "calibration/measure.h"
#include "io/calibration_loader.h"
#include "native/fft.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <complex>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

// Tests the room-calibration measurements getTF and getLevel, run on noise-free
// simulated audio devices, and the LTASS excitation against reference data generated
// by Iceberg MATLAB v1.0.0 (calibration/), within 1e-10 of the reference peak; also the
// safety stops of a measurement and the calibration file it produces.

namespace {

namespace cal = iceberg::calibration;
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

Eigen::VectorXd vec(const std::vector<double>& v) {
    return Eigen::Map<const Eigen::VectorXd>(v.data(), static_cast<Eigen::Index>(v.size()));
}

// MATLAB conv(a, b), full length, by FFT (the devices' convolutions are
// long; the FFT's rounding is far below the 1e-10 contract).
Eigen::VectorXd conv(const Eigen::VectorXd& a, const Eigen::VectorXd& b) {
    const Eigen::Index n_out = a.size() + b.size() - 1;
    Eigen::Index n_fft = 1;
    while (n_fft < n_out) n_fft *= 2;
    Eigen::VectorXd pa = Eigen::VectorXd::Zero(n_fft), pb = Eigen::VectorXd::Zero(n_fft);
    pa.head(a.size()) = a;
    pb.head(b.size()) = b;
    Eigen::VectorXcd fa = iceberg::native::rfft(pa);
    fa = fa.cwiseProduct(iceberg::native::rfft(pb));
    return iceberg::native::irfft(fa, static_cast<int>(n_fft)).head(n_out);
}

// Checks the LTASS level excitation: white noise shaped by the long-term average
// spectrum of a stand-in for speech (two reference noise files at 6 kHz), resampled to
// 48 kHz. The result is compared with the Iceberg MATLAB v1.0.0 reference and must
// agree within 1e-10 of the reference peak.
TEST(CalibrationMeasure, LtassMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto& lt = refs().at("ltass");
    const auto parts = lt.at("speech").get<std::vector<std::string>>();
    const Eigen::VectorXd a = mono(parts[0]);
    const Eigen::VectorXd b = mono(parts[1]);
    Eigen::VectorXd speech(a.size() + b.size());
    speech << a, b;
    const Eigen::VectorXd out = cal::level_excitation_ltass(
        speech, lt.at("speechFs").get<double>(), lt.at("targetFs").get<double>(),
        mono(lt.at("white").get<std::string>()));
    const double d = rel_peak_diff(out, mono(lt.at("file").get<std::string>()));
    record_measure("ltass", d, kTol, "rel");
    EXPECT_LT(d, kTol);
}

// Runs getTF on a simulated device with four planted rooms (band-passed noise with a
// 1/3-octave tilt each), the recording being the stimulus convolved with the room.
// Per loudspeaker, the EQ filter, the RIR and the RIR through the EQ must match Iceberg
// MATLAB v1.0.0 within 1e-10 of the peak, and the EQ must reduce the response spread.
TEST(CalibrationMeasure, GetTFMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto& ref = refs().at("getTF");
    const int n_ls = static_cast<int>(ref.at("filters").size());
    std::vector<Eigen::VectorXd> rooms;
    for (int k = 1; k <= n_ls; ++k) rooms.push_back(mono("tf_room_ls" + std::to_string(k) + ".wav"));

    int calls = 0;
    const cal::PlayRecord device = [&](const Eigen::MatrixXd& page, const std::vector<int>& ch,
                                       int rec, double) {
        ++calls;
        EXPECT_EQ(page.cols(), 1);
        EXPECT_EQ(rec, 0);
        return Eigen::VectorXd(conv(page.col(0), rooms[static_cast<size_t>(ch.at(0))]).head(page.rows()));
    };
    cal::TfConfig config;
    config.n_loudspeakers = n_ls;
    config.averages = ref.at("averages").get<int>();
    config.fft_degree = ref.at("fftDegree").get<int>();
    config.output_amplification_db = ref.at("outputAmplificationDb").get<double>();
    std::vector<std::string> log;
    const cal::TfResult tf = cal::get_tf(config, device, [&](const std::string& s) { log.push_back(s); });

    EXPECT_EQ(calls, n_ls * (config.averages + 1));   // the averages, then the check through the EQ
    ASSERT_EQ(static_cast<int>(tf.filters.size()), n_ls);
    EXPECT_EQ(log.front(), "Measuring loudspeaker 1");
    for (int k = 0; k < n_ls; ++k) {
        const auto& f = ref.at("filters").at(k);
        const double d_filter = rel_peak_diff(tf.filters[static_cast<size_t>(k)].freq,
                                              vec(f.at("freq").get<std::vector<double>>()));
        const double d_rir = rel_peak_diff(tf.rir[static_cast<size_t>(k)],
                                           mono("tf_rir_ls" + std::to_string(k + 1) + ".wav"));
        const double d_adj =
            rel_peak_diff(tf.rir_adjusted[static_cast<size_t>(k)],
                          mono("tf_rir_adjusted_ls" + std::to_string(k + 1) + ".wav"));
        const cal::ResponseCheck check = cal::response_check(
            tf.rir[static_cast<size_t>(k)], tf.rir_adjusted[static_cast<size_t>(k)], kFs);
        const std::string ls = "LS " + std::to_string(k + 1);
        record_measure(ls + " filter", d_filter, kTol, "rel");
        record_measure(ls + " rir", d_rir, kTol, "rel");
        record_measure(ls + " rir through EQ", d_adj, kTol, "rel");
        record_measure(ls + " response spread, after EQ / before",
                       check.after_spread_db / check.before_spread_db, 1.0, "rel");
        EXPECT_LT(check.after_spread_db, check.before_spread_db) << k;
        EXPECT_LT(d_filter, kTol) << k;
        EXPECT_LT(d_rir, kTol) << k;
        EXPECT_LT(d_adj, kTol) << k;
        EXPECT_TRUE((tf.filters[static_cast<size_t>(k)].freq_vector.array() ==
                     vec(f.at("freqVector").get<std::vector<double>>()).array())
                        .all());
    }
}

// Checks response_check on synthetic responses: a unit impulse is flat (band levels
// 0 dB within 1e-9 dB from 100 Hz to 16 kHz, spread below 1e-9 dB), and a comb,
// x[n] + 0.5 x[n - 24], swinging between -6.0 and +3.5 dB (notches from 1 kHz at
// 48 kHz), spreads by more than 1 dB and by less than half of its 9.5 dB swing.
TEST(CalibrationMeasure, ResponseCheckFlatAndComb) {
    const Eigen::Index n = Eigen::Index{1} << 15;
    Eigen::VectorXd impulse = Eigen::VectorXd::Zero(n);
    impulse(0) = 1.0;
    Eigen::VectorXd comb = impulse;
    comb(24) = 0.5;

    const cal::ResponseCheck r = cal::response_check(comb, impulse, kFs);
    EXPECT_GT(r.before_spread_db, 1.0);
    EXPECT_LT(r.before_spread_db, 4.8);
    EXPECT_LT(r.after_spread_db, 1e-9);
    record_measure("impulse spread", r.after_spread_db, 1e-9, "dB");
    ASSERT_EQ(r.centres.size(), r.before_db.size());
    ASSERT_EQ(r.centres.size(), r.after_db.size());
    double worst_after = 0.0;
    for (Eigen::Index i = 0; i < r.centres.size(); ++i) {
        if (r.centres(i) >= 100.0 && r.centres(i) <= 16000.0) {
            EXPECT_NEAR(r.after_db(i), 0.0, 1e-9) << r.centres(i);
            worst_after = std::max(worst_after, std::abs(r.after_db(i)));
        }
    }
    record_measure("impulse band level, worst 100 Hz .. 16 kHz", worst_after, 1e-9, "dB");
}

// Runs getLevel, with the sweep and with the pink-noise excitation, on a simulated
// device (one gain per loudspeaker, then a microphone IR) and the calibration.json EQ
// curves of the default layout. The level factors must match Iceberg MATLAB v1.0.0
// within 1e-10 of the peak, and every loudspeaker must end within 0.5 dB of 70 dB SPL.
TEST(CalibrationMeasure, GetLevelMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto calib = iceberg::io::load_calibration(iceberg::test::fixture_file("calibration.json"));
    const Eigen::VectorXd mic_ir = mono("level_mic_ir.wav");

    for (const auto& r : refs().at("getLevel")) {
        const auto gains = r.at("gains").get<std::vector<double>>();
        const auto idx = r.at("calibrationIndices").get<std::vector<int>>();
        std::vector<cal::BandSpectrum> filters;
        for (const int i : idx) {
            // matlab: iLoudspeakerFreqFilter(i)  →  c++: loudspeakers[i - 1]
            const auto& ls = calib.loudspeakers.at(static_cast<size_t>(i - 1));
            filters.push_back({ls.freq_vector, ls.freq_magnitude});
        }
        const cal::PlayRecord device = [&](const Eigen::MatrixXd& page,
                                           const std::vector<int>& ch, int, double) {
            Eigen::VectorXd rec = Eigen::VectorXd::Zero(page.rows() + mic_ir.size() - 1);
            for (size_t i = 0; i < ch.size(); ++i) {
                const Eigen::VectorXd col = page.col(static_cast<Eigen::Index>(i));
                if (col.isZero(0.0)) continue;   // adds exactly 0 in MATLAB too
                rec += gains[static_cast<size_t>(ch[i])] * conv(col, mic_ir);
            }
            return rec;
        };

        cal::LevelConfig config;
        config.level = r.at("level").get<double>();
        config.tolerance_db = r.at("nTolerance").get<double>();
        config.n_average = r.at("nAverage").get<int>();
        config.max_iterations = r.at("maxIterations").get<int>();
        config.n_loudspeakers = static_cast<int>(idx.size());
        const int exc = r.at("excitation").get<int>();
        const Eigen::VectorXd excitation =
            exc == 1 ? cal::level_excitation_sweep(kFs)
                     : cal::level_excitation_pink(mono("level_pink_white.wav"), kFs,
                                                  config.lower_freq, config.upper_freq);
        const auto out = cal::get_level(config, r.at("iFactor").get<double>(), filters,
                                        vec(r.at("initialLevelFactor").get<std::vector<double>>()),
                                        excitation, device);
        const double d = rel_peak_diff(out.level_factor,
                                       vec(r.at("newLevelFactor").get<std::vector<double>>()));
        const std::string excitation_label = "excitation " + std::to_string(exc);
        record_measure(excitation_label + " level factors", d, kTol, "rel");
        EXPECT_LT(d, kTol) << "excitation " << exc;
        double worst_spl = 0.0;
        for (int k = 0; k < config.n_loudspeakers; ++k) {
            EXPECT_NEAR(out.spl(k), config.level, config.tolerance_db) << k;
            worst_spl = std::max(worst_spl, std::abs(out.spl(k) - config.level));
        }
        record_measure(excitation_label + " SPL vs target, worst loudspeaker", worst_spl,
                       config.tolerance_db, "dB");
    }
}

// === Safety: nothing is played above full scale, and a loudspeaker that
// does not converge stops the run.

// Checks that getTF refuses a sweep set to +6 dBFS: a Clipping MeasurementError is
// thrown for loudspeaker 1 before anything is played (the device is never called).
TEST(CalibrationMeasure, GetTFRefusesAClippingSweep) {
    int calls = 0;
    const cal::PlayRecord device = [&](const Eigen::MatrixXd& page, const std::vector<int>&, int,
                                       double) {
        ++calls;
        return Eigen::VectorXd(page.col(0));
    };
    cal::TfConfig config;
    config.output_amplification_db = 6.0;   // the peak-normalised sweep at +6 dBFS
    try {
        cal::get_tf(config, device);
        FAIL() << "expected a clipping refusal";
    } catch (const cal::MeasurementError& e) {
        EXPECT_EQ(e.kind(), cal::MeasurementError::Kind::Clipping);
        EXPECT_EQ(e.loudspeaker(), 0);
    }
    EXPECT_EQ(calls, 0);
}

std::vector<cal::BandSpectrum> flat_filters(int n) {
    const Eigen::VectorXd f = (Eigen::VectorXd(3) << 50.0, 1000.0, 20000.0).finished();
    return std::vector<cal::BandSpectrum>(static_cast<size_t>(n), {f, Eigen::VectorXd::Ones(3)});
}

// Checks that getLevel refuses a stimulus that would exceed full scale (initial level
// factors of 1000 on two loudspeakers): a Clipping MeasurementError is thrown before
// anything is played (the device is never called).
TEST(CalibrationMeasure, GetLevelRefusesAClippingStimulus) {
    int calls = 0;
    const cal::PlayRecord device = [&](const Eigen::MatrixXd& page, const std::vector<int>&, int,
                                       double) {
        ++calls;
        return Eigen::VectorXd(page.rowwise().sum());
    };
    cal::LevelConfig config;
    config.n_loudspeakers = 2;
    config.n_average = 1;
    try {
        cal::get_level(config, 1.0, flat_filters(2), Eigen::VectorXd::Constant(2, 1e3),
                       cal::level_excitation_sweep(kFs), device);
        FAIL() << "expected a clipping refusal";
    } catch (const cal::MeasurementError& e) {
        EXPECT_EQ(e.kind(), cal::MeasurementError::Kind::Clipping);
    }
    EXPECT_EQ(calls, 0);
}

// Checks that getLevel gives up on a loudspeaker that does not converge: the simulated
// device returns the same noise whatever is played (a microphone pointing elsewhere),
// about 5 dB under the target, so after the limit of two corrections a NoConvergence
// MeasurementError is thrown for loudspeaker 1, with three measurements made.
TEST(CalibrationMeasure, GetLevelStopsWhenItDoesNotConverge) {
    std::mt19937 rng(7);
    std::normal_distribution<double> n01(0.0, 1.0);
    Eigen::VectorXd fixed(1 << 16);
    for (Eigen::Index i = 0; i < fixed.size(); ++i) fixed(i) = 0.04 * n01(rng);   // ~65 dB SPL
    int calls = 0;
    const cal::PlayRecord device = [&](const Eigen::MatrixXd&, const std::vector<int>&, int,
                                       double) {
        ++calls;
        return fixed;
    };
    cal::LevelConfig config;
    config.n_loudspeakers = 1;
    config.n_average = 1;
    config.max_iterations = 2;
    try {
        cal::get_level(config, 1.0, flat_filters(1), Eigen::VectorXd::Constant(1, 0.5),
                       cal::level_excitation_sweep(kFs), device);
        FAIL() << "expected no convergence";
    } catch (const cal::MeasurementError& e) {
        EXPECT_EQ(e.kind(), cal::MeasurementError::Kind::NoConvergence);
        EXPECT_EQ(e.loudspeaker(), 0);
    }
    EXPECT_EQ(calls, 3);   // the first measurement and one per correction
}

// Checks that a calibration file written in the format of the MATLAB export loads back
// bit for bit: the reference calibration.json is saved and read again, and iFactor, the
// level factors and every loudspeaker's index and EQ curve must be identical.
TEST(CalibrationMeasure, CalibrationFileRoundTrips) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto original = iceberg::io::load_calibration(iceberg::test::fixture_file("calibration.json"));
    const std::string path =
        (std::filesystem::temp_directory_path() / "iceberg_calibration_roundtrip.json").string();
    iceberg::io::save_calibration(path, original);
    const auto back = iceberg::io::load_calibration(path);
    std::filesystem::remove(path);

    EXPECT_EQ(back.i_factor, original.i_factor);
    EXPECT_TRUE(back.new_level_factor == original.new_level_factor);
    ASSERT_EQ(back.loudspeakers.size(), original.loudspeakers.size());
    for (size_t k = 0; k < back.loudspeakers.size(); ++k) {
        EXPECT_EQ(back.loudspeakers[k].index, original.loudspeakers[k].index);
        EXPECT_TRUE(back.loudspeakers[k].freq_vector == original.loudspeakers[k].freq_vector);
        EXPECT_TRUE(back.loudspeakers[k].freq_magnitude == original.loudspeakers[k].freq_magnitude);
    }
}

// Checks to_calibration_data, which joins iFactor, the EQ filters and the level factors
// into calibration data: values carried over unchanged, loudspeakers numbered from 1,
// mismatched counts rejected; save_calibration then rejects a calibration of three
// loudspeakers, below the minimum of four.
TEST(CalibrationMeasure, StagesBecomeACalibration) {
    const auto filters = flat_filters(4);
    const Eigen::VectorXd factors = (Eigen::VectorXd(4) << 0.5, 0.6, 0.7, 0.8).finished();
    const auto cal_data = cal::to_calibration_data(20.0, filters, factors);
    EXPECT_EQ(cal_data.i_factor, 20.0);
    EXPECT_TRUE(cal_data.new_level_factor == factors);
    ASSERT_EQ(cal_data.loudspeakers.size(), 4u);
    EXPECT_EQ(cal_data.loudspeakers[0].index, 1);
    EXPECT_EQ(cal_data.loudspeakers[3].index, 4);
    EXPECT_THROW(cal::to_calibration_data(20.0, flat_filters(3), factors), std::invalid_argument);
    EXPECT_THROW(iceberg::io::save_calibration("/nonexistent/dir/cal.json",
                                               cal::to_calibration_data(20.0, flat_filters(3),
                                                                        factors.head(3))),
                 std::invalid_argument);   // 3 loudspeakers: below the minimum
}

// Checks that measure_tf throws std::runtime_error when the device returns a recording
// one sample shorter than the 4096-sample excitation.
TEST(CalibrationMeasure, MeasureTfRejectsARecordingOfAnotherLength) {
    const Eigen::VectorXd excitation = Eigen::VectorXd::Random(1 << 12);
    const auto play = [](const Eigen::VectorXd& s) { return Eigen::VectorXd(s.head(s.size() - 1)); };
    EXPECT_THROW(cal::measure_tf(excitation, kFs, 20.0, 20000.0, 1, play), std::runtime_error);
}

}  // namespace
