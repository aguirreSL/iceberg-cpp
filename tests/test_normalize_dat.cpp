// Tests iceberg::native::normalize_dat, which divides a multichannel buffer by its largest
// absolute sample over all channels, so that its peak becomes 1; the render applies it to the
// input signal. Small hand-made buffers with exact expected values are used.

#include "native/normalize_dat.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

namespace {

constexpr float kTol = 1e-6f;

using iceberg::test::record_measure;

// A 4 by 2 buffer whose largest absolute sample is -2.5 must be divided by 2.5: its peak must
// become 1 and the checked samples -1, 0.2 and 0.04, within 1e-6 (the rounding of 32-bit
// floats).
TEST(NormalizeDat, BasicMaxBecomesOne) {
    Eigen::MatrixXf x(4, 2);
    x << 0.5f, -0.25f,
         1.0f,  0.10f,
        -2.5f,  0.00f,   // global peak lives here at |-2.5|
         0.0f,  0.50f;

    const Eigen::MatrixXf y = iceberg::native::normalize_dat(x);

    EXPECT_NEAR(y.cwiseAbs().maxCoeff(), 1.0f, kTol);
    // Linear scale by 1 / 2.5 = 0.4
    EXPECT_NEAR(y(2, 0), -1.0f, kTol);
    EXPECT_NEAR(y(0, 0),  0.5f / 2.5f, kTol);
    EXPECT_NEAR(y(1, 1),  0.10f / 2.5f, kTol);
    record_measure("scaled by 1/2.5, worst check",
                   std::max({std::abs(y.cwiseAbs().maxCoeff() - 1.0f), std::abs(y(2, 0) + 1.0f),
                             std::abs(y(0, 0) - 0.5f / 2.5f), std::abs(y(1, 1) - 0.10f / 2.5f)}),
                   kTol, "abs");
}

// Empty buffers (0 by 0 and 0 by 4) must be returned with the same shape.
TEST(NormalizeDat, EmptyInputReturnsEmpty) {
    Eigen::MatrixXf empty(0, 0);
    const Eigen::MatrixXf y = iceberg::native::normalize_dat(empty);
    EXPECT_EQ(y.rows(), 0);
    EXPECT_EQ(y.cols(), 0);

    Eigen::MatrixXf zero_rows(0, 4);
    const Eigen::MatrixXf y2 = iceberg::native::normalize_dat(zero_rows);
    EXPECT_EQ(y2.rows(), 0);
    EXPECT_EQ(y2.cols(), 4);
}

// An all-zero 8 by 4 buffer has no peak to divide by; it must be returned with the same shape
// and every sample exactly 0.
TEST(NormalizeDat, AllZerosUnchanged) {
    Eigen::MatrixXf z = Eigen::MatrixXf::Zero(8, 4);
    const Eigen::MatrixXf y = iceberg::native::normalize_dat(z);
    ASSERT_EQ(y.rows(), z.rows());
    ASSERT_EQ(y.cols(), z.cols());
    EXPECT_EQ(y.cwiseAbs().maxCoeff(), 0.0f);
}

// The scale is the absolute value of the peak and the signs are kept: [-2.5 1 -0.5] must become
// [-1 0.4 -0.2], with a peak of 1, within 1e-6 (the rounding of 32-bit floats).
TEST(NormalizeDat, NegativeMaxNormalizedByAbs) {
    Eigen::MatrixXf x(3, 1);
    x << -2.5f, 1.0f, -0.5f;

    const Eigen::MatrixXf y = iceberg::native::normalize_dat(x);

    EXPECT_NEAR(y(0, 0), -1.0f,  kTol);
    EXPECT_NEAR(y(1, 0),  0.4f,  kTol);
    EXPECT_NEAR(y(2, 0), -0.2f,  kTol);
    EXPECT_NEAR(y.cwiseAbs().maxCoeff(), 1.0f, kTol);
    record_measure("scaled by 1/2.5, worst check",
                   std::max({std::abs(y(0, 0) + 1.0f), std::abs(y(1, 0) - 0.4f),
                             std::abs(y(2, 0) + 0.2f), std::abs(y.cwiseAbs().maxCoeff() - 1.0f)}),
                   kTol, "abs");
}

// One scale serves all channels: with channel peaks of 0.5 and 4.0, both channels are divided by
// 4.0, so channel 0 must peak at 0.125 and channel 1 at 1, within 1e-6 (the rounding of 32-bit
// floats).
TEST(NormalizeDat, GlobalPeakNotPerChannel) {
    // ch0 peak = 0.5, ch1 peak = 4.0. Global peak is 4.0 → both channels
    // scale by 1/4.0, ch0 max stays 0.125 (NOT 1.0).
    Eigen::MatrixXf x(2, 2);
    x << 0.5f, 4.0f,
         0.1f, -1.0f;

    const Eigen::MatrixXf y = iceberg::native::normalize_dat(x);

    EXPECT_NEAR(y(0, 0), 0.125f, kTol);
    EXPECT_NEAR(y(0, 1), 1.0f,   kTol);
    EXPECT_NEAR(y.col(0).cwiseAbs().maxCoeff(), 0.125f, kTol);
    record_measure("scaled by the global 1/4, worst check",
                   std::max({std::abs(y(0, 0) - 0.125f), std::abs(y(0, 1) - 1.0f),
                             std::abs(y.col(0).cwiseAbs().maxCoeff() - 0.125f)}),
                   kTol, "abs");
}

}  // namespace
