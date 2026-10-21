// Tests iceberg::native::resample and its helpers kaiser and rat, which convert an input signal
// to the sample rate of the impulse response. The results are compared with values computed by
// MATLAB's Signal Processing Toolbox (kaiser, rat, and resample with its default filter).

#include "native/resample.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

using iceberg::test::record_measure;

// Reference values computed with MATLAB R2026b (Signal Processing Toolbox):
// kaiser(11, 5), rat(a/b, 1e-12), resample(x, p, q) with the default
// filter, printed with 17 significant digits.

// kaiser(11, 5), the window of the resampling filter, is compared sample by sample with MATLAB's
// kaiser(11, 5) printed to 17 digits, within 1e-15 (a few units of double rounding).
TEST(Resample, KaiserMatchesMatlab) {
    const double ref[11] = {0.036710892271286669, 0.17917825081818559, 0.41490363924336671,
                            0.69020641551432982,  0.91381248386919955, 1,
                            0.91381248386919955,  0.69020641551432982, 0.41490363924336671,
                            0.17917825081818559,  0.036710892271286669};
    const auto w = iceberg::native::kaiser(11, 5.0);
    ASSERT_EQ(w.size(), 11);
    for (int i = 0; i < 11; ++i) EXPECT_NEAR(w(i), ref[i], 1e-15) << i;
    double worst = 0.0;
    for (int i = 0; i < 11; ++i) worst = std::max(worst, std::abs(w(i) - ref[i]));
    record_measure("kaiser(11, 5) vs MATLAB, worst sample", worst, 1e-15, "abs");
}

// rat(x, 1e-12) must return the same integer ratios as MATLAB's rat for five sample-rate ratios:
// 44.1/48 kHz gives 147/160, 48/44.1 gives 160/147, 44.1/32 gives 441/320, 96/44.1 gives 320/147
// and 44.1/22.05 gives 2/1 (exact comparison).
TEST(Resample, RatMatchesMatlab) {
    using P = std::pair<long long, long long>;
    EXPECT_EQ(iceberg::native::rat(44100.0 / 48000.0, 1e-12), (P{147, 160}));
    EXPECT_EQ(iceberg::native::rat(48000.0 / 44100.0, 1e-12), (P{160, 147}));
    EXPECT_EQ(iceberg::native::rat(44100.0 / 32000.0, 1e-12), (P{441, 320}));
    EXPECT_EQ(iceberg::native::rat(96000.0 / 44100.0, 1e-12), (P{320, 147}));
    EXPECT_EQ(iceberg::native::rat(44100.0 / 22050.0, 1e-12), (P{2, 1}));
}

// 100 samples of a 1 kHz sine at 48 kHz are resampled to 44.1 kHz (p/q = 147/160), which gives
// ceil(100 * 147 / 160) = 92 samples. The first 6 and last 3 samples are compared with MATLAB's
// resample within 1e-13, and the energy (sum of squares, about 44.36) within 1e-11; both limits
// allow for double rounding in the filter sums.
TEST(Resample, Sine48kTo44k1MatchesMatlab) {
    Eigen::MatrixXd x(100, 1);
    for (int i = 0; i < 100; ++i) x(i, 0) = std::sin(2.0 * kPi * 1000.0 * i / 48000.0);
    const auto y = iceberg::native::resample(x, 147, 160);

    ASSERT_EQ(y.rows(), 92);   // ceil(100 * 147 / 160)
    const double head[6] = {0.0024114590962541708, 0.14138675026202818, 0.28144159043938555,
                            0.41451481213261088,   0.53976505421052068, 0.65373908030701977};
    for (int i = 0; i < 6; ++i) EXPECT_NEAR(y(i, 0), head[i], 1e-13) << i;
    const double tail[3] = {0.10741641884609196, 0.26597030701636593, 0.35497923220179506};
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(y(89 + i, 0), tail[i], 1e-13) << i;
    EXPECT_NEAR(y.squaredNorm(), 44.356983382783447, 1e-11);
    double worst = 0.0;
    for (int i = 0; i < 6; ++i) worst = std::max(worst, std::abs(y(i, 0) - head[i]));
    for (int i = 0; i < 3; ++i) worst = std::max(worst, std::abs(y(89 + i, 0) - tail[i]));
    record_measure("first 6 and last 3 samples vs MATLAB, worst", worst, 1e-13, "abs");
    record_measure("energy vs MATLAB", std::abs(y.squaredNorm() - 44.356983382783447), 1e-11,
                   "abs");
}

// 200 samples of a decaying 300 Hz cosine at 44.1 kHz are resampled to 48 kHz (p/q = 160/147),
// which gives ceil(200 * 160 / 147) = 218 samples. The first 3 samples are compared with MATLAB's
// resample within 1e-13, and the energy (about 23.83) within 1e-11; both limits allow for double
// rounding in the filter sums.
TEST(Resample, Upsampling44k1To48kMatchesMatlab) {
    Eigen::MatrixXd x(200, 1);
    for (int i = 0; i < 200; ++i) {
        x(i, 0) = std::cos(2.0 * kPi * 300.0 * i / 44100.0) * std::exp(-i / 80.0);
    }
    const auto y = iceberg::native::resample(x, 160, 147);

    ASSERT_EQ(y.rows(), 218);   // ceil(200 * 160 / 147)
    EXPECT_NEAR(y(0, 0), 1.0006751149639157, 1e-13);
    EXPECT_NEAR(y(1, 0), 1.0131401266379894, 1e-13);
    EXPECT_NEAR(y(2, 0), 0.94630987915424469, 1e-13);
    EXPECT_NEAR(y.squaredNorm(), 23.834759485160539, 1e-11);
    record_measure("first 3 samples vs MATLAB, worst",
                   std::max({std::abs(y(0, 0) - 1.0006751149639157),
                             std::abs(y(1, 0) - 1.0131401266379894),
                             std::abs(y(2, 0) - 0.94630987915424469)}),
                   1e-13, "abs");
    record_measure("energy vs MATLAB", std::abs(y.squaredNorm() - 23.834759485160539), 1e-11,
                   "abs");
}

// Equal up and down factors (p = q = 3) must return a random 50 by 2 input unchanged (exact
// comparison).
TEST(Resample, EqualRatesReturnTheInput) {
    const Eigen::MatrixXd x = Eigen::MatrixXd::Random(50, 2);
    EXPECT_TRUE(iceberg::native::resample(x, 3, 3) == x);
}

// Every column is resampled with the same filter: for a 2-column input whose second column is
// twice the first, the second output column must equal twice the first within a relative 1e-15
// (Eigen's isApprox), that is, to double rounding.
TEST(Resample, ResamplesEveryColumn) {
    Eigen::MatrixXd x(100, 2);
    for (int i = 0; i < 100; ++i) {
        x(i, 0) = std::sin(2.0 * kPi * 1000.0 * i / 48000.0);
        x(i, 1) = 2.0 * x(i, 0);
    }
    const auto y = iceberg::native::resample(x, 147, 160);
    EXPECT_TRUE(y.col(1).isApprox(2.0 * y.col(0), 1e-15));
    record_measure("column 2 vs 2 x column 1 (Eigen isApprox norm)",
                   (y.col(1) - 2.0 * y.col(0)).norm() /
                       std::min(y.col(1).norm(), (2.0 * y.col(0)).norm()),
                   1e-15, "rel");
}

}  // namespace
