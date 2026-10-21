// Tests of iceberg::rendering::calibrate_vbap(), the calibration of the VBAP-panned direct
// branch: each loudspeaker's filter and level factor, and one common level stage that sets
// the SPL while keeping the VBAP gains. It is compared with the reference data generated
// by Iceberg MATLAB v1.0.0 and checked against analytic values on synthetic noise.

#include "io/calibration_loader.h"
#include "io/wav_loader.h"
#include "rendering/calibrate_ambisonics.h"  // ConfigSetup
#include "rendering/calibrate_vbap.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

namespace {

constexpr double kFs = 44100.0;
constexpr double kPi = 3.14159265358979323846;

using iceberg::test::record_measure;

// The 4 loudspeakers at 0, 90, 180, 270 with a neutral calibration
// (flat filter, unity level factors, lsdBperVolt = 0): what is left is
// the level stage itself.
iceberg::rendering::ConfigSetup neutral_setup() {
    iceberg::rendering::ConfigSetup cfg;
    cfg.ls_array = Eigen::VectorXi(4);
    cfg.ls_array << 0, 90, 180, 270;
    cfg.active_ls_numbers = Eigen::VectorXi(4);
    cfg.active_ls_numbers << 1, 2, 3, 4;
    cfg.ls_dir = Eigen::MatrixXd(4, 2);
    cfg.ls_dir << 0, 0,
                  90, 0,
                  180, 0,
                  270, 0;
    cfg.calibration = iceberg::io::neutral_calibration(4);
    return cfg;
}

// A mono noise panned with fixed gains, one column per loudspeaker.
iceberg::Signal panned_noise(int n, const Eigen::Vector4d& gains,
                             unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, 0.3);
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf(n, 4);
    for (int i = 0; i < n; ++i) {
        const double v = dist(rng);
        for (int c = 0; c < 4; ++c) {
            s.samples(i, c) = static_cast<float>(gains(c) * v);
        }
    }
    s.sample_rate = kFs;
    return s;
}

double rms_of(const Eigen::VectorXf& v) {
    return std::sqrt(v.cast<double>().array().square().mean());
}

double euclidean_rms(const Eigen::MatrixXf& m) {
    return std::sqrt(m.cast<double>().array().square().rowwise().sum().mean());
}

// === Against MATLAB: stages/rt_05, the panned signal in, the calibrated out ==

// The panned signals of the rt_05 stage files at 30, 100 and 225 deg are calibrated at the
// case level and compared with the calibrated output of Iceberg MATLAB v1.0.0, within
// 1e-9 RMS per channel. The limit is the 32-bit float rounding of the input files, since
// MATLAB calibrated the pan in double precision.
TEST(CalibrateVbap, StagesMatchMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto cfg = iceberg::test::load_case_setup("sweep/rt_05");
    const double level = iceberg::test::load_fixture_json("sweep/rt_05/config.json")
                             .at("level").get<double>();

    double worst_rms = 0.0;
    for (const int angle : {30, 100, 225}) {
        char az[16];
        std::snprintf(az, sizeof(az), "az_%03d.wav", angle);
        const auto panned = iceberg::io::load_wav(
            iceberg::test::fixture_file(std::string("stages/rt_05/vbap_panned_") + az));
        const auto expected = iceberg::io::load_wav(
            iceberg::test::fixture_file(std::string("stages/rt_05/vbap_calibrated_") + az));

        const auto out = iceberg::rendering::calibrate_vbap(panned, level, angle, cfg);
        ASSERT_EQ(out.samples.rows(), expected.samples.rows());
        ASSERT_EQ(out.samples.cols(), expected.samples.cols());

        for (Eigen::Index c = 0; c < out.samples.cols(); ++c) {
            const Eigen::ArrayXd diff = (out.samples.col(c).cast<double>() -
                                         expected.samples.col(c).cast<double>()).array();
            const double rms = std::sqrt(diff.square().mean());
            worst_rms = std::max(worst_rms, rms);
            EXPECT_LT(rms, 1e-9) << "angle " << angle << " channel " << c;
        }
    }
    record_measure("stages/rt_05 worst RMS (az 30, 100, 225)", worst_rms, 1e-9, "rms");
}

// The 4 loudspeakers with no ring around them (lsArray = ls_dir, a 4-entry calibration)
// must calibrate exactly as inside the 24-position ring: the rt_05 panned signal at
// 30 deg and 80 dB SPL gives bit-identical outputs in both layouts.
TEST(CalibrateVbap, FourLoudspeakersAloneMatchRing) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto ring  = iceberg::test::load_case_setup(std::string("sweep/rt_05"));
    const auto alone = iceberg::test::load_case_setup(std::string("inputs/four_alone"));
    ASSERT_EQ(alone.ls_array.size(), 4);
    const auto panned = iceberg::io::load_wav(
        iceberg::test::fixture_file("stages/rt_05/vbap_panned_az_030.wav"));

    const auto a = iceberg::rendering::calibrate_vbap(panned, 80.0, 30.0, ring);
    const auto b = iceberg::rendering::calibrate_vbap(panned, 80.0, 30.0, alone);
    EXPECT_TRUE(a.samples == b.samples);
}

// The level balance within the active pair follows the VBAP gains: noise panned at 30 deg
// between the loudspeakers at 0 and 90 deg (gains cos 30 and sin 30, neutral calibration,
// 70 dB SPL) keeps their 4.77 dB level ratio within 1e-5 dB, and the two other
// loudspeakers stay exactly silent.
TEST(CalibrateVbap, VbapGainsSurviveAt30Deg) {
    const auto cfg = neutral_setup();
    const Eigen::Vector4d g(std::cos(kPi / 6.0), std::sin(kPi / 6.0), 0.0, 0.0);
    const auto out = iceberg::rendering::calibrate_vbap(
        panned_noise(8192, g), 70.0, 30.0, cfg);

    const double ratio_db = 20.0 * std::log10(rms_of(out.samples.col(0)) /
                                              rms_of(out.samples.col(1)));
    record_measure("in-pair ratio vs VBAP's", std::abs(ratio_db - 20.0 * std::log10(g(0) / g(1))),
                   1e-5, "dB");
    EXPECT_NEAR(ratio_db, 20.0 * std::log10(g(0) / g(1)), 1e-5);
    EXPECT_EQ(out.samples.col(2).cwiseAbs().maxCoeff(), 0.0f);
    EXPECT_EQ(out.samples.col(3).cwiseAbs().maxCoeff(), 0.0f);
}

// One SPL anchor for every pan: with a neutral calibration, noise panned at 0, 10, 30, 45
// and 80 deg (||g||_2 = 1) and a level of 70 dB SPL, the RMS of the Euclidean norm across
// channels must equal the analytic value 10^((level - lsdBperVolt)/20) / 2 within a
// relative 1e-6.
TEST(CalibrateVbap, CommonScalerAnchorsSplForAnyPan) {
    const auto cfg = neutral_setup();
    const double level = 70.0;
    const double expected = 0.5 * std::pow(10.0, level / 20.0);   // lsdBperVolt = 0
    double worst = 0.0;
    for (const double deg : {0.0, 10.0, 30.0, 45.0, 80.0}) {
        const double t = deg * kPi / 180.0;
        const Eigen::Vector4d g(std::cos(t), std::sin(t), 0.0, 0.0);
        const auto out = iceberg::rendering::calibrate_vbap(
            panned_noise(4096, g), level, deg, cfg);
        EXPECT_NEAR(euclidean_rms(out.samples) / expected, 1.0, 1e-6) << deg << " deg";
        worst = std::max(worst, std::abs(euclidean_rms(out.samples) / expected - 1.0));
    }
    record_measure("Euclidean RMS vs the anchor, worst pan", worst, 1e-6, "rel");
}

// Level 'n' (NaN) bypasses the level stage while the filter and level factor stay: with a
// neutral calibration the output equals the input within 1e-6, and with the rt_05
// calibration the output at 80 dB SPL equals the bypassed output times the SPL gain over
// the common scaler, within a relative 1e-6.
TEST(CalibrateVbap, NaNLevelBypassesTheScaleStage) {
    const auto neutral = neutral_setup();
    const Eigen::Vector4d g(0.8, 0.6, 0.0, 0.0);
    const auto in = panned_noise(4096, g, 7);
    const auto out = iceberg::rendering::calibrate_vbap(
        in, std::numeric_limits<double>::quiet_NaN(), 0.0, neutral);
    record_measure("neutral calibration + bypass vs the input",
                   (out.samples - in.samples).cwiseAbs().maxCoeff(), 1e-6f, "abs");
    EXPECT_LT((out.samples - in.samples).cwiseAbs().maxCoeff(), 1e-6f)
        << "neutral calibration + bypass must be the identity";

    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto cfg = iceberg::test::load_case_setup("sweep/rt_05");
    const double level = 80.0;
    const double ls_db_per_volt = 20.0 * std::log10(cfg.calibration.i_factor / 2e-5);
    const double scale = std::pow(10.0, (level - ls_db_per_volt) / 20.0) /
                         (2.0 * euclidean_rms(in.samples));
    const auto bypass = iceberg::rendering::calibrate_vbap(
        in, std::numeric_limits<double>::quiet_NaN(), 0.0, cfg);
    const auto finite = iceberg::rendering::calibrate_vbap(in, level, 0.0, cfg);
    const double rel = (finite.samples.cast<double>() - bypass.samples.cast<double>() * scale)
                           .cwiseAbs().maxCoeff() /
                       finite.samples.cast<double>().cwiseAbs().maxCoeff();
    record_measure("finite level vs bypass x gain / scaler", rel, 1e-6, "rel");
    EXPECT_LT(rel, 1e-6);
}

// Order of operations as Iceberg MATLAB v1.0.0 computes it: the common scaler is computed
// before NaN samples are set to zero, so one NaN input sample makes every output sample
// NaN at 70 dB SPL; with level 'n' (bypass) the output contains no NaN.
TEST(CalibrateVbap, NaNSamplePoisonsTheScaler_DocumentsV1Order) {
    const auto cfg = neutral_setup();
    auto in = panned_noise(1024, Eigen::Vector4d(0.6, 0.8, 0.0, 0.0), 13);
    in.samples(500, 1) = std::numeric_limits<float>::quiet_NaN();

    const auto scaled = iceberg::rendering::calibrate_vbap(in, 70.0, 0.0, cfg);
    EXPECT_TRUE(scaled.samples.array().isNaN().all());

    const auto bypass = iceberg::rendering::calibrate_vbap(
        in, std::numeric_limits<double>::quiet_NaN(), 0.0, cfg);
    EXPECT_FALSE(bypass.samples.array().isNaN().any());
}

// A 3-channel input for a layout of 4 active loudspeakers is rejected with
// std::invalid_argument.
TEST(CalibrateVbap, ThrowsOnChannelCountMismatch) {
    const auto cfg = neutral_setup();
    iceberg::Signal s;
    s.sample_rate = kFs;
    s.samples = Eigen::MatrixXf::Ones(256, 3);
    EXPECT_THROW(iceberg::rendering::calibrate_vbap(s, 70.0, 0.0, cfg),
                 std::invalid_argument);
}

}  // namespace
