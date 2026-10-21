// Tests iceberg::native::time_shift, the circular shift of every channel by one number of
// samples, given in samples or in seconds, or found automatically (Auto) from the ISO 3382-1
// onset of the impulse response. Short synthetic signals with hand-computed results are used.

#include "native/time_shift.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

namespace {

constexpr float kTol = 1e-6f;

using iceberg::test::record_measure;

iceberg::Signal make_signal(const std::vector<float>& mono, double fs = 1000.0) {
    iceberg::Signal s;
    s.samples = Eigen::Map<const Eigen::VectorXf>(mono.data(),
                                                  static_cast<Eigen::Index>(
                                                      mono.size()));
    s.sample_rate = fs;
    return s;
}

// Auto mode moves the ISO 3382-1 onset to sample 0, as Iceberg MATLAB v1.0.0 computes it; the
// onset is the last sample before the peak that lies more than 20 dB below it. For a unit impulse
// at sample 3 the silent sample 2 is the onset, so the shift must be -2 and the impulse must land
// on sample 1 (within 1e-6, the rounding of 32-bit floats).
TEST(TimeShift, AutoMovesIsoOnsetToZero) {
    const auto s = make_signal({0, 0, 0, 1, 0, 0, 0, 0});
    const auto out =
        iceberg::native::time_shift(s, iceberg::native::ShiftMode::Auto);

    EXPECT_EQ(out.shift_samples, -2);
    EXPECT_NEAR(out.signal.samples(0, 0), 0.0f, kTol);
    EXPECT_NEAR(out.signal.samples(1, 0), 1.0f, kTol);
    record_measure("shifted samples, worst",
                   std::max(std::abs(out.signal.samples(0, 0)),
                            std::abs(out.signal.samples(1, 0) - 1.0f)),
                   kTol, "abs");
}

// In Auto mode, energy that rises again before the onset, more than 6 dB above the -20 dB
// threshold, belongs to the impulse response, and the onset moves back before it. With a -6 dB
// sample at 1, a -80 dB sample at 2 and the peak at 4, the onset is sample 0 and the shift is 0.
TEST(TimeShift, AutoKeepsPreOnsetOscillation) {
    std::vector<float> x(20, 0.0f);
    x[1] = 0.5f;      // -6 dB re the peak: above -20 + 6
    x[2] = 1e-4f;     // -80 dB
    x[4] = 1.0f;      // peak
    const auto out = iceberg::native::time_shift(make_signal(x),
                                                 iceberg::native::ShiftMode::Auto);
    EXPECT_EQ(out.shift_samples, 0);
}

// In Auto mode, a peak less than 20 dB above the mean energy of the last 10 % of the signal (the
// noise level) gives no shift. The signal is a constant 0.5 with a peak of 1 at sample 10, 6 dB
// above that level, and the shift must be 0.
TEST(TimeShift, AutoDoesNotShiftWithoutSnr) {
    std::vector<float> x(50, 0.5f);
    x[10] = 1.0f;
    const auto out = iceberg::native::time_shift(make_signal(x),
                                                 iceberg::native::ShiftMode::Auto);
    EXPECT_EQ(out.shift_samples, 0);
}

// A shift of +2 samples on [1 2 3 4 5] must give the circular result [4 5 1 2 3] and report a
// shift of 2. The samples are compared within 1e-6, the rounding of 32-bit floats.
TEST(TimeShift, SamplesPositive) {
    const auto s = make_signal({1, 2, 3, 4, 5});
    const auto out = iceberg::native::time_shift(
        s, iceberg::native::ShiftMode::Samples, 2.0);

    EXPECT_EQ(out.shift_samples, 2);
    const std::vector<float> expected = {4, 5, 1, 2, 3};
    float worst = 0.0f;
    for (Eigen::Index i = 0; i < 5; ++i) {
        EXPECT_NEAR(out.signal.samples(i, 0), expected[i], kTol);
        worst = std::max(worst, std::abs(out.signal.samples(i, 0) - expected[i]));
    }
    record_measure("shifted samples, worst", worst, kTol, "abs");
}

// A shift of -2 samples on [1 2 3 4 5] must give the circular result [3 4 5 1 2] and report a
// shift of -2. The samples are compared within 1e-6, the rounding of 32-bit floats.
TEST(TimeShift, SamplesNegative) {
    const auto s = make_signal({1, 2, 3, 4, 5});
    const auto out = iceberg::native::time_shift(
        s, iceberg::native::ShiftMode::Samples, -2.0);

    EXPECT_EQ(out.shift_samples, -2);
    const std::vector<float> expected = {3, 4, 5, 1, 2};
    float worst = 0.0f;
    for (Eigen::Index i = 0; i < 5; ++i) {
        EXPECT_NEAR(out.signal.samples(i, 0), expected[i], kTol);
        worst = std::max(worst, std::abs(out.signal.samples(i, 0) - expected[i]));
    }
    record_measure("shifted samples, worst", worst, kTol, "abs");
}

// A shift of 7 samples on a 5-sample signal wraps around as a shift of 2 and must give
// [4 5 1 2 3], while the reported shift stays 7. The samples are compared within 1e-6, the
// rounding of 32-bit floats.
TEST(TimeShift, SamplesGreaterThanLength) {
    const auto s = make_signal({1, 2, 3, 4, 5});
    const auto out = iceberg::native::time_shift(
        s, iceberg::native::ShiftMode::Samples, 7.0);

    // 7 mod 5 = 2 — same wrap as the +2 test.
    EXPECT_EQ(out.shift_samples, 7);
    const std::vector<float> expected = {4, 5, 1, 2, 3};
    float worst = 0.0f;
    for (Eigen::Index i = 0; i < 5; ++i) {
        EXPECT_NEAR(out.signal.samples(i, 0), expected[i], kTol);
        worst = std::max(worst, std::abs(out.signal.samples(i, 0) - expected[i]));
    }
    record_measure("shifted samples, worst", worst, kTol, "abs");
}

// A shift given in seconds is converted with the sample rate: 0.005 s at 1 kHz is 5 samples, so
// a unit impulse at sample 0 of a 20-sample signal must move to sample 5 (within 1e-6, the
// rounding of 32-bit floats) and the reported shift must be 5.
TEST(TimeShift, TimeMode) {
    iceberg::Signal s;
    s.samples = Eigen::VectorXf::Zero(20);
    s.samples(0) = 1.0f;
    s.sample_rate = 1000.0;

    const auto out =
        iceberg::native::time_shift(s, iceberg::native::ShiftMode::Time, 0.005);

    // 0.005 s * 1000 Hz = 5 samples → impulse at idx 0 moves to idx 5.
    EXPECT_EQ(out.shift_samples, 5);
    EXPECT_NEAR(out.signal.samples(5, 0), 1.0f, kTol);
    EXPECT_NEAR(out.signal.samples(0, 0), 0.0f, kTol);
    record_measure("shifted samples, worst",
                   std::max(std::abs(out.signal.samples(5, 0) - 1.0f),
                            std::abs(out.signal.samples(0, 0))),
                   kTol, "abs");
}

// A shift of +2 samples on a 3-channel signal must move every channel by the same two samples;
// the 5 by 3 result is compared with the hand-computed one within 1e-6, the rounding of 32-bit
// floats.
TEST(TimeShift, MultichannelUniformShift) {
    Eigen::MatrixXf m(5, 3);
    m << 1, 10, 100,
         2, 20, 200,
         3, 30, 300,
         4, 40, 400,
         5, 50, 500;
    iceberg::Signal s{m, 1000.0};

    const auto out = iceberg::native::time_shift(
        s, iceberg::native::ShiftMode::Samples, 2.0);

    // Each column shifted by +2 independently.
    Eigen::MatrixXf expected(5, 3);
    expected <<
        4, 40, 400,
        5, 50, 500,
        1, 10, 100,
        2, 20, 200,
        3, 30, 300;
    float worst = 0.0f;
    for (Eigen::Index i = 0; i < 5; ++i) {
        for (Eigen::Index j = 0; j < 3; ++j) {
            EXPECT_NEAR(out.signal.samples(i, j), expected(i, j), kTol);
            worst = std::max(worst, std::abs(out.signal.samples(i, j) - expected(i, j)));
        }
    }
    record_measure("shifted samples, worst", worst, kTol, "abs");
}

// In Auto mode, the earliest onset over all channels sets one shift for every channel, as
// Iceberg MATLAB computes it. With impulses at sample 6 (channel 0, onset 5) and sample 3
// (channel 1, onset 2), the shift must be -2 and the impulses must land on samples 4 and 1
// (within 1e-6, the rounding of 32-bit floats).
TEST(TimeShift, AutoUsesEarliestOnsetAcrossChannels) {
    Eigen::MatrixXf m = Eigen::MatrixXf::Zero(10, 2);
    m(6, 0) = 1.0f;   // ch0 onset 5
    m(3, 1) = 1.0f;   // ch1 onset 2: the earliest
    iceberg::Signal s{m, 1000.0};

    const auto out =
        iceberg::native::time_shift(s, iceberg::native::ShiftMode::Auto);

    EXPECT_EQ(out.shift_samples, -2);
    EXPECT_NEAR(out.signal.samples(4, 0), 1.0f, kTol);
    EXPECT_NEAR(out.signal.samples(1, 1), 1.0f, kTol);
    record_measure("shifted samples, worst",
                   std::max(std::abs(out.signal.samples(4, 0) - 1.0f),
                            std::abs(out.signal.samples(1, 1) - 1.0f)),
                   kTol, "abs");
}

// A shift of 0 samples must return a 2-channel input unchanged (within 1e-6, the rounding of
// 32-bit floats) and report a shift of 0.
TEST(TimeShift, ShiftZeroIsIdentity) {
    // Exercises the `kc == 0` short-circuit in circshift_rows. None of the
    // other tests produce a canonical shift of 0 — Auto picks a non-zero
    // peak, Samples uses ±2 / +7, Time gives 5. Without this case the
    // early-return branch is dead code under the existing suite.
    Eigen::MatrixXf m(5, 2);
    m << 1, 10,
         2, 20,
         3, 30,
         4, 40,
         5, 50;
    iceberg::Signal s{m, 1000.0};

    const auto out = iceberg::native::time_shift(
        s, iceberg::native::ShiftMode::Samples, 0.0);

    EXPECT_EQ(out.shift_samples, 0);
    float worst = 0.0f;
    for (Eigen::Index i = 0; i < 5; ++i) {
        for (Eigen::Index j = 0; j < 2; ++j) {
            EXPECT_NEAR(out.signal.samples(i, j), m(i, j), kTol);
            worst = std::max(worst, std::abs(out.signal.samples(i, j) - m(i, j)));
        }
    }
    record_measure("vs the input, worst", worst, kTol, "abs");
}

// The Auto shift on real IRs is checked against MATLAB through
// IcebergCore.StagesMatchMatlab: IR_Late comes out bit-identical there,
// which needs the same shift sample.

}  // namespace
