// Tests the handling of the Ambisonic room impulse response before rendering: the
// conversion of AmbiX, N3D and higher-order inputs to first-order FuMa, the direct-sound
// ratio that reveals a mis-declared format, and the clockwise azimuth convention.

#include "rendering/input_handling.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>
#include <stdexcept>

namespace {

using iceberg::rendering::AmbisonicFormat;
using iceberg::test::record_measure;

// A first-order FuMa plane wave from azimuth `deg` (counter-clockwise,
// elevation 0): W = s/sqrt(2), X = s cos, Y = s sin, Z = 0.
Eigen::MatrixXf fuma_plane_wave(double deg, int n = 64) {
    const double a = deg * 3.14159265358979323846 / 180.0;
    Eigen::MatrixXf m = Eigen::MatrixXf::Zero(n, 4);
    for (int i = 0; i < n; ++i) {
        const double s = std::exp(-i / 4.0);
        m(i, 0) = static_cast<float>(s / std::sqrt(2.0));
        m(i, 1) = static_cast<float>(s * std::cos(a));
        m(i, 2) = static_cast<float>(s * std::sin(a));
    }
    return m;
}

// Checks that a first-order FuMa input (a synthetic plane wave from 30 deg) leaves
// bformat_to_fuma bit for bit unchanged, with its order reported as 1.
TEST(InputHandling, FumaPassesThrough) {
    const auto f = fuma_plane_wave(30.0);
    int order = 0;
    EXPECT_TRUE(iceberg::rendering::bformat_to_fuma(f, AmbisonicFormat::FuMa, &order) == f);
    EXPECT_EQ(order, 1);
}

// Checks the conversion to FuMa of a synthetic plane wave from 120 deg written in AmbiX
// (ACN order W Y Z X, SN3D) and in N3D: both carry the same field, so each result must
// equal the FuMa channels within 1e-7, the float rounding of the sqrt(2) and sqrt(3)
// scalings.
TEST(InputHandling, AmbixAndN3dConvertToFuma) {
    const auto f = fuma_plane_wave(120.0);
    Eigen::MatrixXf ambix(f.rows(), 4), n3d(f.rows(), 4);
    ambix << f.col(0) * std::sqrt(2.0f), f.col(2), f.col(3), f.col(1);
    n3d << f.col(0) * std::sqrt(2.0f), f.col(2) * std::sqrt(3.0f), f.col(3) * std::sqrt(3.0f),
        f.col(1) * std::sqrt(3.0f);
    const auto from_ambix =
        iceberg::rendering::bformat_to_fuma(ambix, AmbisonicFormat::AmbiX, nullptr);
    const auto from_n3d = iceberg::rendering::bformat_to_fuma(n3d, AmbisonicFormat::N3D, nullptr);
    record_measure("AmbiX to FuMa", (from_ambix - f).cwiseAbs().maxCoeff(), 1e-7f, "abs");
    record_measure("N3D to FuMa", (from_n3d - f).cwiseAbs().maxCoeff(), 1e-7f, "abs");
    EXPECT_LT((from_ambix - f).cwiseAbs().maxCoeff(), 1e-7f);
    EXPECT_LT((from_n3d - f).cwiseAbs().maxCoeff(), 1e-7f);
}

// Checks the order reduction of a third-order input (16 random channels): the order is
// reported as 3, and the output is exactly the first four channels, which hold the
// first-order components.
TEST(InputHandling, HigherOrderKeepsTheFirstFourChannels) {
    Eigen::MatrixXf hoa = Eigen::MatrixXf::Random(32, 16);   // third order
    int order = 0;
    const auto f = iceberg::rendering::bformat_to_fuma(hoa, AmbisonicFormat::FuMa, &order);
    EXPECT_EQ(order, 3);
    EXPECT_TRUE(f == hoa.leftCols(4));
}

// Checks that bformat_to_fuma rejects, with std::invalid_argument, channel counts that
// are not (N+1)^2 for an order N >= 1: 1, 2, 3, 5, 8 and 10 channels.
TEST(InputHandling, RejectsNonSquareChannelCounts) {
    for (const int ch : {1, 2, 3, 5, 8, 10}) {
        EXPECT_THROW(iceberg::rendering::bformat_to_fuma(
                         Eigen::MatrixXf::Zero(8, ch), AmbisonicFormat::FuMa, nullptr),
                     std::invalid_argument)
            << ch;
    }
}

// Checks the direct-sound ratio W/|(X, Y, Z)| around the peak: a FuMa plane wave gives
// the analytic 1/sqrt(2), the same wave with W scaled by sqrt(2) (AmbiX read as FuMa)
// gives 1, both within 1e-6, and an input with W alone, without directional energy,
// gives NaN.
TEST(InputHandling, DirectSoundRatio) {
    const auto f = fuma_plane_wave(45.0);
    EXPECT_NEAR(iceberg::rendering::direct_sound_ratio(f, 44100.0), 1.0 / std::sqrt(2.0), 1e-6);
    Eigen::MatrixXf w_scaled = f;
    w_scaled.col(0) *= std::sqrt(2.0f);
    EXPECT_NEAR(iceberg::rendering::direct_sound_ratio(w_scaled, 44100.0), 1.0, 1e-6);
    record_measure(
        "FuMa plane wave vs 1/sqrt(2)",
        std::abs(iceberg::rendering::direct_sound_ratio(f, 44100.0) - 1.0 / std::sqrt(2.0)), 1e-6,
        "abs");
    record_measure("W scaled by sqrt(2) vs 1",
                   std::abs(iceberg::rendering::direct_sound_ratio(w_scaled, 44100.0) - 1.0), 1e-6,
                   "abs");

    Eigen::MatrixXf omni = Eigen::MatrixXf::Zero(64, 4);
    omni.col(0) = f.col(0);
    EXPECT_TRUE(std::isnan(iceberg::rendering::direct_sound_ratio(omni, 44100.0)));
}

// Checks prepare_inputs with the clockwise azimuth convention declared, on a FuMa plane
// wave from 60 deg: the Y channel is exactly negated, the X channel is unchanged, and no
// notice is reported.
TEST(InputHandling, ClockwiseMirrorsY) {
    iceberg::rendering::ConfigSetup cfg;
    cfg.ambisonic_format = AmbisonicFormat::FuMa;
    cfg.azimuth_convention = iceberg::rendering::AzimuthConvention::Clockwise;
    iceberg::Signal sig{Eigen::MatrixXf::Ones(16, 1), 44100.0};
    iceberg::Signal ir{fuma_plane_wave(60.0), 44100.0};
    const auto p = iceberg::rendering::prepare_inputs(sig, ir, cfg);
    EXPECT_TRUE(p.ir.samples.col(2) == -ir.samples.col(2));
    EXPECT_TRUE(p.ir.samples.col(1) == ir.samples.col(1));
    EXPECT_TRUE(p.notices.empty());
}

}  // namespace
