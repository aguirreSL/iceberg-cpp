// Tests iceberg::native::time_crop, which cuts a time range out of a signal or, for an inverted
// range, removes the block between the bounds. The bounds are rounded to samples and a forward
// range keeps an even number of samples, as Iceberg MATLAB computes it. The inputs are ramps, so
// each sample value identifies its index.

#include "native/time_crop.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

namespace {

constexpr float kTol = 1e-6f;
constexpr double kFs = 1000.0;

using iceberg::test::record_measure;

iceberg::Signal ramp_signal(Eigen::Index n, Eigen::Index ch = 1,
                            double fs = kFs) {
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf(n, ch);
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index c = 0; c < ch; ++c) {
            s.samples(i, c) = static_cast<float>(i) +
                              static_cast<float>(c) * static_cast<float>(n);
        }
    }
    s.sample_rate = fs;
    return s;
}

// Cropping [0.2, 0.5] s from a 1000-sample ramp at 1 kHz must keep 300 samples, 0-based 200 to
// 499, with the sample rate unchanged. The ramp values are integers, so the limit of 1e-6 on the
// first and last samples detects any index error.
TEST(TimeCrop, BasicRange) {
    // 1 s @ 1000 Hz → 1000 samples. Crop [0.2, 0.5] in MATLAB:
    //   start = round(0.2*1000)+1 = 201, end = round(0.5*1000) = 500
    //   length = 500 - 201 + 1 = 300.
    const auto s = ramp_signal(1000);
    const auto out = iceberg::native::time_crop(s, 0.2, 0.5);

    ASSERT_EQ(out.samples.rows(), 300);
    ASSERT_EQ(out.samples.cols(), 1);
    EXPECT_DOUBLE_EQ(out.sample_rate, kFs);
    // First cropped sample = original sample at idx 200 (0-based).
    EXPECT_NEAR(out.samples(0, 0), 200.0f, kTol);
    EXPECT_NEAR(out.samples(299, 0), 499.0f, kTol);
    record_measure("cropped samples, worst",
                   std::max(std::abs(out.samples(0, 0) - 200.0f),
                            std::abs(out.samples(299, 0) - 499.0f)),
                   kTol, "abs");
}

// The range [0.5, 0] s is an inverted range that keeps the samples after the start bound, so the
// result begins one sample after 0.5 s. On a 1000-sample ramp at 1 kHz it must hold 499 samples,
// 0-based 501 to 999 (the first and last checked within 1e-6).
TEST(TimeCrop, EndZeroStartsOneSampleAfterStart) {
    // Crop [0.5, 0] is an inverted range: i1 = 501, i2 = 1 (1-based),
    // keep [1:0, 502:end], i.e. 0-based rows 501 .. 999 → 499 rows.
    const auto s = ramp_signal(1000);
    const auto out = iceberg::native::time_crop(s, 0.5, 0.0);

    ASSERT_EQ(out.samples.rows(), 499);
    EXPECT_NEAR(out.samples(0, 0), 501.0f, kTol);
    EXPECT_NEAR(out.samples(498, 0), 999.0f, kTol);
    record_measure("cropped samples, worst",
                   std::max(std::abs(out.samples(0, 0) - 501.0f),
                            std::abs(out.samples(498, 0) - 999.0f)),
                   kTol, "abs");
}

// A forward range keeps an even number of samples: [0.1, 0.3] s at 1 kHz spans the 1-based
// samples 101 to 301, the end moves to 300, and 200 samples remain, 0-based 100 to 299 (the
// first and last checked within 1e-6 on a ramp).
TEST(TimeCrop, EvenSpanDropsOneSample) {
    // [0.1, 0.3] @ 1000 Hz: i1 = 101, i2 = 301, span 200 is even, so i2
    // moves to 300 (an odd sample count is made even): rows 100 .. 299.
    const auto s = ramp_signal(1000);
    const auto out = iceberg::native::time_crop(s, 0.1, 0.3);

    ASSERT_EQ(out.samples.rows(), 200);
    EXPECT_NEAR(out.samples(0, 0), 100.0f, kTol);
    EXPECT_NEAR(out.samples(199, 0), 299.0f, kTol);
    record_measure("cropped samples, worst",
                   std::max(std::abs(out.samples(0, 0) - 100.0f),
                            std::abs(out.samples(199, 0) - 299.0f)),
                   kTol, "abs");
}

// Cropping [0, 0.3] s from a 1000-sample ramp at 1 kHz must keep the first 300 samples, 0-based
// 0 to 299 (the first and last checked within 1e-6).
TEST(TimeCrop, StartZero) {
    // Crop [0, 0.3]: start = 1, end = 300 → 300 rows.
    const auto s = ramp_signal(1000);
    const auto out = iceberg::native::time_crop(s, 0.0, 0.3);

    ASSERT_EQ(out.samples.rows(), 300);
    EXPECT_NEAR(out.samples(0, 0), 0.0f, kTol);
    EXPECT_NEAR(out.samples(299, 0), 299.0f, kTol);
    record_measure("cropped samples, worst",
                   std::max(std::abs(out.samples(0, 0)),
                            std::abs(out.samples(299, 0) - 299.0f)),
                   kTol, "abs");
}

// The inverted range [0.8, 0.2] s removes the block between the bounds: from a 4-channel,
// 1000-sample ramp at 1 kHz it must keep the 0-based samples 0 to 199 and 801 to 999 (399 rows),
// with the channels and the sample rate unchanged; the two samples that meet at the cut are
// checked within 1e-6.
TEST(TimeCrop, InvertedRangeRemovesTheBlockBetween) {
    // [0.8, 0.2]: i1 = 801, i2 = 201 → keep [1:200, 802:end] (1-based),
    // 0-based rows 0 .. 199 and 801 .. 999 → 399 rows, columns preserved.
    const auto s = ramp_signal(1000, 4);
    const auto out = iceberg::native::time_crop(s, 0.8, 0.2);

    ASSERT_EQ(out.samples.rows(), 399);
    EXPECT_EQ(out.samples.cols(), 4);
    EXPECT_DOUBLE_EQ(out.sample_rate, kFs);
    EXPECT_NEAR(out.samples(199, 0), 199.0f, kTol);
    EXPECT_NEAR(out.samples(200, 0), 801.0f, kTol);
    record_measure("cropped samples, worst",
                   std::max(std::abs(out.samples(199, 0) - 199.0f),
                            std::abs(out.samples(200, 0) - 801.0f)),
                   kTol, "abs");
}

// A range past the end of the signal, [0.5, 0.8] s on 100 samples at 1 kHz, must be rejected
// with std::invalid_argument; Iceberg MATLAB v1.0.0 also stops with an indexing error there.
TEST(TimeCrop, RangeOutsideThrows) {
    // 0.1 s @ 1000 Hz → 100 samples; [0.5, 0.8] is past the end. v1.0.0
    // indexes directly (MATLAB errors).
    const auto s = ramp_signal(100);
    EXPECT_THROW(iceberg::native::time_crop(s, 0.5, 0.8), std::invalid_argument);
}

// Cropping [0.2, 0.5] s from a 3-channel ramp (channel c offset by c * 1000) must keep the same
// 300 samples, 0-based 200 to 499, in every channel; the first and last sample of each channel
// are checked within 1e-6.
TEST(TimeCrop, MultichannelCroppedUniformly) {
    const auto s = ramp_signal(1000, 3);
    const auto out = iceberg::native::time_crop(s, 0.2, 0.5);

    ASSERT_EQ(out.samples.rows(), 300);
    ASSERT_EQ(out.samples.cols(), 3);
    // Channel c, row i in input = i + c * 1000. After crop, row 0 → input 200.
    float worst = 0.0f;
    for (Eigen::Index c = 0; c < 3; ++c) {
        EXPECT_NEAR(out.samples(0,   c), 200.0f + c * 1000.0f, kTol);
        EXPECT_NEAR(out.samples(299, c), 499.0f + c * 1000.0f, kTol);
        worst = std::max({worst, std::abs(out.samples(0, c) - (200.0f + c * 1000.0f)),
                          std::abs(out.samples(299, c) - (499.0f + c * 1000.0f))});
    }
    record_measure("cropped samples, worst channel", worst, kTol, "abs");
}

}  // namespace
