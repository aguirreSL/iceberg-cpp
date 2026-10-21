// Tests iceberg::native::add, which sums two signals after padding both with zeros to the larger
// number of samples and of channels; the render uses it to sum its VBAP and Ambisonics parts.
// The inputs are constant signals, so every expected sum is exact.

#include "native/add.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

namespace {

constexpr float kTol = 1e-6f;
constexpr double kFs = 44100.0;

using iceberg::test::record_measure;

iceberg::Signal make_signal(Eigen::Index rows, Eigen::Index cols, float fill,
                            double fs = kFs) {
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(rows, cols, fill);
    s.sample_rate = fs;
    return s;
}

// Two 1000-sample, 2-channel constant signals (0.5 and -0.25) must sum to 0.25, checked at the
// first and last samples within 1e-6 (the rounding of 32-bit floats), with the shape and the
// sample rate kept.
TEST(NativeAdd, SameShape) {
    const auto a = make_signal(1000, 2, 0.5f);
    const auto b = make_signal(1000, 2, -0.25f);

    const auto out = iceberg::native::add(a, b);

    ASSERT_EQ(out.samples.rows(), 1000);
    ASSERT_EQ(out.samples.cols(), 2);
    EXPECT_DOUBLE_EQ(out.sample_rate, kFs);
    EXPECT_NEAR(out.samples(0, 0),    0.25f, kTol);
    EXPECT_NEAR(out.samples(999, 1),  0.25f, kTol);
    record_measure("sum, worst checked sample",
                   std::max(std::abs(out.samples(0, 0) - 0.25f),
                            std::abs(out.samples(999, 1) - 0.25f)),
                   kTol, "abs");
}

// A 1000-sample signal of ones plus a 500-sample signal of twos (2 channels each): the shorter
// one is padded with zeros, so the first 500 rows must be 3 and the remaining rows 1, checked
// within 1e-6 (the rounding of 32-bit floats).
TEST(NativeAdd, DifferentLengthsZeroPadsShorter) {
    const auto a = make_signal(1000, 2, 1.0f);
    const auto b = make_signal(500, 2, 2.0f);

    const auto out = iceberg::native::add(a, b);

    ASSERT_EQ(out.samples.rows(), 1000);
    ASSERT_EQ(out.samples.cols(), 2);
    // First 500 rows: 1.0 + 2.0 = 3.0
    EXPECT_NEAR(out.samples(0,   0), 3.0f, kTol);
    EXPECT_NEAR(out.samples(499, 1), 3.0f, kTol);
    // Tail rows: only `a` contributes
    EXPECT_NEAR(out.samples(500, 0), 1.0f, kTol);
    EXPECT_NEAR(out.samples(999, 1), 1.0f, kTol);
    record_measure("sum, worst checked sample",
                   std::max({std::abs(out.samples(0, 0) - 3.0f),
                             std::abs(out.samples(499, 1) - 3.0f),
                             std::abs(out.samples(500, 0) - 1.0f),
                             std::abs(out.samples(999, 1) - 1.0f)}),
                   kTol, "abs");
}

// A 2-channel signal of ones plus a 4-channel signal of 0.5: the narrower one is padded with zero
// channels, so channels 0 and 1 must be 1.5 and channels 2 and 3 must be 0.5, checked within
// 1e-6 (the rounding of 32-bit floats).
TEST(NativeAdd, DifferentChannelsZeroPadsNarrower) {
    const auto a = make_signal(1000, 2, 1.0f);
    const auto b = make_signal(1000, 4, 0.5f);

    const auto out = iceberg::native::add(a, b);

    ASSERT_EQ(out.samples.rows(), 1000);
    ASSERT_EQ(out.samples.cols(), 4);
    // Cols 0..1: 1.0 + 0.5
    EXPECT_NEAR(out.samples(0, 0), 1.5f, kTol);
    EXPECT_NEAR(out.samples(0, 1), 1.5f, kTol);
    // Cols 2..3: only `b` contributes — must equal `b`'s value
    EXPECT_NEAR(out.samples(0,   2), 0.5f, kTol);
    EXPECT_NEAR(out.samples(999, 3), 0.5f, kTol);
    record_measure("sum, worst checked sample",
                   std::max({std::abs(out.samples(0, 0) - 1.5f),
                             std::abs(out.samples(0, 1) - 1.5f),
                             std::abs(out.samples(0, 2) - 0.5f),
                             std::abs(out.samples(999, 3) - 0.5f)}),
                   kTol, "abs");
}

// A 500-sample, 2-channel signal of ones plus a 1000-sample, 4-channel signal of 0.25: the result
// is 1000 by 4, with 1.25 where both overlap and 0.25 elsewhere, checked at five samples within
// 1e-6 (the rounding of 32-bit floats).
TEST(NativeAdd, DifferentBothLengthAndChannels) {
    const auto a = make_signal(500, 2, 1.0f);
    const auto b = make_signal(1000, 4, 0.25f);

    const auto out = iceberg::native::add(a, b);

    ASSERT_EQ(out.samples.rows(), 1000);
    ASSERT_EQ(out.samples.cols(), 4);
    // Top-left 500×2 quadrant: a + b
    EXPECT_NEAR(out.samples(0,   0), 1.25f, kTol);
    EXPECT_NEAR(out.samples(499, 1), 1.25f, kTol);
    // Below row 500, cols 0..1: only b
    EXPECT_NEAR(out.samples(500, 1), 0.25f, kTol);
    // Cols 2..3: only b for all rows
    EXPECT_NEAR(out.samples(0,   3), 0.25f, kTol);
    EXPECT_NEAR(out.samples(999, 2), 0.25f, kTol);
    record_measure("sum, worst checked sample",
                   std::max({std::abs(out.samples(0, 0) - 1.25f),
                             std::abs(out.samples(499, 1) - 1.25f),
                             std::abs(out.samples(500, 1) - 0.25f),
                             std::abs(out.samples(0, 3) - 0.25f),
                             std::abs(out.samples(999, 2) - 0.25f)}),
                   kTol, "abs");
}

// Signals at 44.1 kHz and 48 kHz must be rejected with std::invalid_argument.
TEST(NativeAdd, SampleRateMismatchThrows) {
    const auto a = make_signal(100, 1, 0.0f, 44100.0);
    const auto b = make_signal(100, 1, 0.0f, 48000.0);

    EXPECT_THROW(iceberg::native::add(a, b), std::invalid_argument);
}

// An empty operand (no samples, no channels) contributes nothing: adding it to a 1000-sample,
// 2-channel signal of 0.75, as the first or the second operand, must return that signal (shape
// checked, values within 1e-6, the rounding of 32-bit floats).
TEST(NativeAdd, EmptyOperandReturnsOther) {
    iceberg::Signal empty;
    empty.samples = Eigen::MatrixXf(0, 0);
    empty.sample_rate = kFs;

    const auto b = make_signal(1000, 2, 0.75f);

    const auto out = iceberg::native::add(empty, b);

    ASSERT_EQ(out.samples.rows(), 1000);
    ASSERT_EQ(out.samples.cols(), 2);
    EXPECT_NEAR(out.samples(0,   0), 0.75f, kTol);
    EXPECT_NEAR(out.samples(999, 1), 0.75f, kTol);

    // Symmetry: empty as second operand also works.
    const auto out2 = iceberg::native::add(b, empty);
    ASSERT_EQ(out2.samples.rows(), 1000);
    ASSERT_EQ(out2.samples.cols(), 2);
    EXPECT_NEAR(out2.samples(500, 1), 0.75f, kTol);
    record_measure("the other operand, worst checked sample",
                   std::max({std::abs(out.samples(0, 0) - 0.75f),
                             std::abs(out.samples(999, 1) - 0.75f),
                             std::abs(out2.samples(500, 1) - 0.75f)}),
                   kTol, "abs");
}

}  // namespace
