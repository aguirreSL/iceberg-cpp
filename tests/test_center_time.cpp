// Tests iceberg::native::center_time, the centre time Ts of ISO 3382-1, computed on the
// onset-aligned first channel of an impulse response and truncated where the decay meets the
// noise (the method of Lundeby et al., 1995), as Iceberg MATLAB v1.0.0 computes it. The render
// splits the impulse response at this time. Synthetic responses with known values are used.

#include "native/center_time.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <array>
#include <cmath>

namespace {

// === Synthetic tests ======================================================

// A noise-free exponential decay with a reverberation time of 0.5 s (1 s at 44.1 kHz) has the
// analytic centre time 1 / (2 alpha) = T60 / (12 ln 10), about 18.09 ms. The computed value must
// lie within 5 % of it; the block averaging and the noise-floor estimate of the truncation step
// bias the result slightly.
TEST(NativeCenterTime, ExponentialDecay) {
    // p(t) = exp(-α t)  →  E(t) = exp(-2α t).
    // Center time = ∫ t·E dt / ∫ E dt = 1 / (2α).
    // For T60 = 0.5 s, decay factor over T60 is 60 dB:
    //   20 log10(exp(-α·T60)) = -60  →  α = 6 ln(10) / T60.
    // → center_time_analytical = T60 / (12 ln10) ≈ 0.018087 s.
    constexpr double kFs = 44100.0;
    constexpr double kT60 = 0.5;
    constexpr double kDuration = 1.0;  // 1 s of IR
    const long n = static_cast<long>(kDuration * kFs);
    const double alpha = 6.0 * std::log(10.0) / kT60;

    iceberg::Signal s;
    s.samples = Eigen::MatrixXf(n, 1);
    for (long i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kFs;
        s.samples(i, 0) = static_cast<float>(std::exp(-alpha * t));
    }
    s.sample_rate = kFs;

    const double cTime = iceberg::native::center_time(s);

    const double expected = 1.0 / (2.0 * alpha);  // ≈ 0.0180874 s
    // 5 % tolerance: blocking + noise-floor estimate cause a small bias
    // vs. the closed-form integral.
    iceberg::test::record_measure("vs 1 / (2 alpha)", std::abs(cTime - expected) / expected, 0.05,
                                  "rel");
    EXPECT_NEAR(cTime, expected, 0.05 * expected)
        << "cTime = " << cTime << "  expected ≈ " << expected;
}

// Impulses of 1 and 0.5 at samples 100 and 500 (44.1 kHz): the time axis starts at the onset,
// sample 99, and the trimmed response (403 samples) is shorter than one 30 ms averaging block, so
// the truncation step fails and the energy centroid (1 * 1 + 0.25 * 401) / (1.25 fs) of the
// whole response is returned. It must match within 1e-12 s, the rounding of doubles.
TEST(NativeCenterTime, ShortIrFallsBackToOnsetReferencedCentroid) {
    // v1.0.0: the time axis starts at the ISO 3382 onset, not at sample 0.
    // Deltas at 100 and 500: the onset is sample 99 (the last one 20 dB
    // below the peak), so they sit at 1 and 401 after the shift; the
    // trailing zeros are trimmed to 403 samples. Lundeby's 30 ms block
    // (1323 samples) does not fit, so Lundeby fails and the uncut centroid
    // is returned: sum(E·t) / sum(E) with E = [1, 0.25] at [1, 401].
    constexpr double kFs = 44100.0;
    constexpr long n = 1000;

    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Zero(n, 1);
    s.samples(100, 0) = 1.0f;
    s.samples(500, 0) = 0.5f;
    s.sample_rate = kFs;

    const double cTime = iceberg::native::center_time(s);

    const double expected = (1.0 * 1.0 + 0.25 * 401.0) / (1.25 * kFs);
    iceberg::test::record_measure("vs the onset-referenced centroid", std::abs(cTime - expected),
                                  1e-12, "s");
    EXPECT_NEAR(cTime, expected, 1e-12);
}

// An all-zero response (100000 samples) has no energy, and center_time must throw
// std::runtime_error, as Iceberg MATLAB stops with an error; the render catches it and splits
// the response at a fixed 10 ms instead.
TEST(NativeCenterTime, AllZeroIrThrowsLikeMatlab) {
    // The trailing-zero walk reaches sample 1 on an all-zero channel and
    // errors ('channel is empty'); iceberg_core catches it and falls back
    // to the 10 ms rectangle, as MATLAB's try/catch does.
    constexpr double kFs = 44100.0;
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Zero(100000, 1);
    s.sample_rate = kFs;

    EXPECT_THROW(iceberg::native::center_time(s), std::runtime_error);
}

// Only channel 0 (the W channel of a B-format response) enters the centre time: a 4-channel
// response whose channel 0 is a mono impulse response and whose other channels hold random,
// constant and unit values must give the same centre time as that mono response, equal to
// within 4 units in the last place of a double (EXPECT_DOUBLE_EQ).
TEST(NativeCenterTime, MultichannelUsesChannel0) {
    // Same channel-0 content, different content on channels 1..3 — must
    // not influence cTime at all.
    constexpr double kFs = 44100.0;
    constexpr long n = 100000;

    iceberg::Signal mono;
    mono.samples = Eigen::MatrixXf::Zero(n, 1);
    mono.samples(1000, 0) = 1.0f;
    mono.sample_rate = kFs;

    iceberg::Signal multi;
    multi.samples = Eigen::MatrixXf::Zero(n, 4);
    multi.samples(1000, 0) = 1.0f;       // same as mono
    multi.samples.col(1).setRandom();    // garbage on other channels
    multi.samples.col(2).setConstant(0.5f);
    multi.samples.col(3).setOnes();
    multi.sample_rate = kFs;

    const double cTime_mono = iceberg::native::center_time(mono);
    const double cTime_multi = iceberg::native::center_time(multi);

    EXPECT_DOUBLE_EQ(cTime_mono, cTime_multi);
}


}  // namespace
