// Tests iceberg::native::convolve, the channel-wise linear convolution of the render, with its
// FFT method (the default) and its direct time-domain method: hand-computed results, the output
// length (the linear length rounded up to an even number of rows), edge cases, and the
// convolution of the reference data computed by Iceberg MATLAB v1.0.0.

#include "io/wav_loader.h"
#include "native/convolve.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>

namespace {

constexpr float kTol = 1e-6f;
constexpr double kFs = 44100.0;

using iceberg::test::record_measure;

iceberg::Signal mono_signal(const std::vector<float>& v, double fs = kFs) {
    iceberg::Signal s;
    s.samples = Eigen::Map<const Eigen::VectorXf>(
        v.data(), static_cast<Eigen::Index>(v.size()));
    s.sample_rate = fs;
    return s;
}

// The convolution of [1 2 3] with [1 1] by the default method is compared with the
// hand-computed [1 3 5 3]. The linear length 4 is already even, so no row is added. The values
// are checked within 1e-6, the rounding of 32-bit floats.
TEST(NativeConvolve, MonoMonoAlgebraic) {
    // [1 2 3] * [1 1] = [1, 1+2, 2+3, 3] = [1, 3, 5, 3]. The linear length
    // 4 is already even, so the even output length adds no row.
    const auto s = mono_signal({1, 2, 3});
    const auto ir = mono_signal({1, 1});

    const auto out = iceberg::native::convolve(s, ir);

    ASSERT_EQ(out.samples.rows(), 4);
    ASSERT_EQ(out.samples.cols(), 1);
    EXPECT_NEAR(out.samples(0, 0), 1.0f, kTol);
    EXPECT_NEAR(out.samples(1, 0), 3.0f, kTol);
    EXPECT_NEAR(out.samples(2, 0), 5.0f, kTol);
    EXPECT_NEAR(out.samples(3, 0), 3.0f, kTol);
    const Eigen::Vector4f expected(1.0f, 3.0f, 5.0f, 3.0f);
    record_measure("vs [1 3 5 3]", (out.samples.col(0) - expected).cwiseAbs().maxCoeff(), kTol,
                   "abs");
}

// The output length is rounded up to an even number of rows, as Iceberg MATLAB computes it:
// [1 2 3] convolved with [1 1 1] has the odd linear length 5, so the output has 6 rows, with the
// last linear sample (3) in row 4 and a zero in row 5, checked within 1e-6 (the rounding of
// 32-bit floats).
TEST(NativeConvolve, OddLinearLengthGetsOneZeroRow) {
    // [1 2 3] * [1 1 1] has linear length 5: forced even to 6, last row 0.
    const auto s = mono_signal({1, 2, 3});
    const auto ir = mono_signal({1, 1, 1});

    const auto out = iceberg::native::convolve(s, ir);

    ASSERT_EQ(out.samples.rows(), 6);
    EXPECT_NEAR(out.samples(4, 0), 3.0f, kTol);
    EXPECT_NEAR(out.samples(5, 0), 0.0f, kTol);
    record_measure("last two rows vs [3 0]",
                   std::max(std::abs(out.samples(4, 0) - 3.0f), std::abs(out.samples(5, 0))), kTol,
                   "abs");
}

// Convolution with the one-sample impulse [1] must return the 5-sample input unchanged,
// followed by one zero row (the odd linear length 5 is rounded up to 6). The values are checked
// within 1e-6, the rounding of 32-bit floats.
TEST(NativeConvolve, IdentityIrLeavesSignalUntouched) {
    // [a b c d e] * [1] = [a b c d e] (with one trailing zero from off-by-one).
    const auto s = mono_signal({1.5f, -2.0f, 0.25f, 7.0f, -3.5f});
    const auto ir = mono_signal({1.0f});

    const auto out = iceberg::native::convolve(s, ir);

    ASSERT_EQ(out.samples.rows(), 6);  // 5 + 1
    for (Eigen::Index i = 0; i < 5; ++i) {
        EXPECT_NEAR(out.samples(i, 0), s.samples(i, 0), kTol);
    }
    EXPECT_NEAR(out.samples(5, 0), 0.0f, kTol);
    record_measure("vs the signal and a zero",
                   std::max((out.samples.col(0).head(5) - s.samples.col(0)).cwiseAbs().maxCoeff(),
                            std::abs(out.samples(5, 0))),
                   kTol, "abs");
}

// Convolution with a unit impulse at sample 2, [0 0 1 0], must delay [1 2 3 4 5] by two
// samples: the 8 output rows (an even linear length, so no row is added) are compared with
// [0 0 1 2 3 4 5 0] within 1e-6, the rounding of 32-bit floats.
TEST(NativeConvolve, DeltaShiftsSignal) {
    // Signal * [0 0 1 0] should produce signal shifted by +2 samples.
    const auto s = mono_signal({1, 2, 3, 4, 5});
    const auto ir = mono_signal({0, 0, 1, 0});

    const auto out = iceberg::native::convolve(s, ir);

    ASSERT_EQ(out.samples.rows(), 8);  // 5 + 4 - 1, already even
    EXPECT_NEAR(out.samples(0, 0), 0.0f, kTol);
    EXPECT_NEAR(out.samples(1, 0), 0.0f, kTol);
    EXPECT_NEAR(out.samples(2, 0), 1.0f, kTol);
    EXPECT_NEAR(out.samples(3, 0), 2.0f, kTol);
    EXPECT_NEAR(out.samples(4, 0), 3.0f, kTol);
    EXPECT_NEAR(out.samples(5, 0), 4.0f, kTol);
    EXPECT_NEAR(out.samples(6, 0), 5.0f, kTol);
    EXPECT_NEAR(out.samples(7, 0), 0.0f, kTol);
    Eigen::VectorXf expected(8);
    expected << 0, 0, 1, 2, 3, 4, 5, 0;
    record_measure("vs the signal shifted by 2",
                   (out.samples.col(0) - expected).cwiseAbs().maxCoeff(), kTol, "abs");
}

// Channel pairing: a mono signal convolved with a 4-channel IR gives 4 output channels, each the
// convolution of the one signal channel with one IR channel (the operand with fewer channels
// reuses its last column). The 4 by 4 output is compared with four hand-computed convolutions
// within 1e-6, the rounding of 32-bit floats.
TEST(NativeConvolve, MonoSignalTimes4ChIrClampsToCh0) {
    // signal mono * ir 4ch — for every k=0..3, signal idx clamps to 0.
    const auto s = mono_signal({1, 2, 3});
    iceberg::Signal ir;
    ir.samples = Eigen::MatrixXf(2, 4);
    ir.samples << 1, 2, 3, 4,
                  1, 1, 1, 1;
    ir.sample_rate = kFs;

    const auto out = iceberg::native::convolve(s, ir);

    ASSERT_EQ(out.samples.rows(), 4);   // 3 + 2 - 1, already even
    ASSERT_EQ(out.samples.cols(), 4);

    // ch 0: [1 2 3] * [1 1] = [1 3 5 3]
    // ch 1: [1 2 3] * [2 1] = [2 5 8 3]
    // ch 2: [1 2 3] * [3 1] = [3 7 11 3]
    // ch 3: [1 2 3] * [4 1] = [4 9 14 3]
    Eigen::MatrixXf expected(4, 4);
    expected << 1, 2, 3, 4,
                3, 5, 7, 9,
                5, 8, 11, 14,
                3, 3, 3, 3;

    for (Eigen::Index i = 0; i < 4; ++i) {
        for (Eigen::Index j = 0; j < 4; ++j) {
            EXPECT_NEAR(out.samples(i, j), expected(i, j), kTol)
                << "i=" << i << " j=" << j;
        }
    }
    record_measure("vs the 4 hand convolutions", (out.samples - expected).cwiseAbs().maxCoeff(),
                   kTol, "abs");
}

// A signal at 44.1 kHz and an IR at 48 kHz must be rejected with std::invalid_argument.
TEST(NativeConvolve, SampleRateMismatchThrows) {
    iceberg::Signal a; a.samples = Eigen::VectorXf::Ones(10); a.sample_rate = 44100.0;
    iceberg::Signal b; b.samples = Eigen::VectorXf::Ones(10); b.sample_rate = 48000.0;

    EXPECT_THROW(iceberg::native::convolve(a, b), std::invalid_argument);
}

// Edge case: a signal with no samples, convolved with a 100-sample, 4-channel IR, must give an
// all-zero output of 100 rows (the linear length 99 rounded up to an even number) and 4 channels.
TEST(NativeConvolve, EmptySignalProducesAllZeros) {
    // Replicates MATLAB: timeData = zeros(0 + n_b, max(nCh)). All zeros.
    iceberg::Signal empty;
    empty.samples = Eigen::MatrixXf(0, 1);
    empty.sample_rate = kFs;

    iceberg::Signal ir;
    ir.samples = Eigen::MatrixXf::Constant(100, 4, 0.5f);
    ir.sample_rate = kFs;

    const auto out = iceberg::native::convolve(empty, ir);

    EXPECT_EQ(out.samples.rows(), 100);  // 0 + 100
    EXPECT_EQ(out.samples.cols(), 4);
    EXPECT_TRUE(out.samples.isZero());
}

// === Against MATLAB native_convolve: stages/rt_05/vbap_convolved.wav =====

// White noise (1 s at 44.1 kHz) convolved by both methods with the direct sound and early
// reflections of the rt_05 IR is compared with the convolution by Iceberg MATLAB v1.0.0. The
// peak error relative to the reference peak must stay below 2e-7 (FFT) and 5e-7 (Direct, which
// sums in float), the 32-bit float rounding of IR and output; the added last row must be zero.
TEST(NativeConvolve, StagesMatchMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file("signal.wav"));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file("stages/rt_05/core_dser.wav"));
    const auto expected = iceberg::io::load_wav(
        iceberg::test::fixture_file(
        "stages/rt_05/vbap_convolved.wav"));

    for (const auto method : {iceberg::native::ConvolveMethod::Direct,
                              iceberg::native::ConvolveMethod::FFT}) {
        const char* name =
            method == iceberg::native::ConvolveMethod::Direct ? "Direct" : "FFT";
        const auto out = iceberg::native::convolve(signal, ir, method);
        ASSERT_EQ(out.samples.rows(), expected.samples.rows());
        ASSERT_EQ(out.samples.cols(), expected.samples.cols());

        const auto d = iceberg::test::diff_stats(out.samples, expected.samples);
        record_measure(std::string("stages/rt_05 ") + name + " rel peak", d.worst_rel_peak,
                       method == iceberg::native::ConvolveMethod::FFT ? 2e-7 : 5e-7, "rel");
        // Floor: the float32 rounding of the DSER input and of the output
        // (this signal is ~100x louder than a calibrated render, so the
        // floor shows in the relative figure, not as 1e-9 RMS).
        // FFT is the render default; Direct accumulates in float.
        EXPECT_LT(d.worst_rel_peak,
                  method == iceberg::native::ConvolveMethod::FFT ? 2e-7 : 5e-7) << name;

        // Final row must be exactly zero: 44100 + 28366 - 1 is odd, forced even.
        EXPECT_TRUE(out.samples.row(out.samples.rows() - 1).isZero()) << name;
    }
}

// ====================================================================
// FFT against Direct
// ====================================================================

namespace {

iceberg::Signal random_mono_signal(Eigen::Index n, uint32_t seed,
                                   double fs = kFs) {
    // Deterministic LCG to avoid pulling in <random> machinery — the
    // tests just need reproducible noise, not statistical purity.
    iceberg::Signal s;
    s.samples = Eigen::VectorXf(n);
    uint32_t x = seed ? seed : 1u;
    for (Eigen::Index i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        s.samples(i, 0) = (static_cast<float>(x) /
                           static_cast<float>(UINT32_MAX)) * 2.0f - 1.0f;
    }
    s.sample_rate = fs;
    return s;
}

double max_abs_diff(const Eigen::MatrixXf& a, const Eigen::MatrixXf& b) {
    return (a.array() - b.array()).abs().maxCoeff();
}

double rms_diff(const Eigen::MatrixXf& a, const Eigen::MatrixXf& b) {
    const Eigen::ArrayXXf d = a.array() - b.array();
    return std::sqrt(d.square().sum() /
                     static_cast<double>(d.size()));
}

}  // namespace

// The FFT method is compared with the direct time-domain sum on pseudo-random signals and IRs
// of four size pairs (from 8 and 4 samples up to 99 and 99). The largest absolute difference
// must stay below 1e-5, a margin above the 32-bit float rounding of the two results.
TEST(ConvolveFFT, MatchesDirectOnSmallInputs) {
    // Tight algorithmic correctness check on small inputs. FFT vs Direct
    // bit-near (1e-5 floor: float -> double -> float roundtrip noise).
    struct Case { Eigen::Index n_a, n_b; uint32_t seed; };
    double worst_err = 0.0;
    for (const auto& c : {Case{8,4,1u}, Case{37,11,2u}, Case{83,23,3u},
                          Case{99,99,4u}}) {
        const auto s = random_mono_signal(c.n_a, c.seed);
        const auto ir = random_mono_signal(c.n_b, c.seed + 100);
        const auto direct = iceberg::native::convolve(
            s, ir, iceberg::native::ConvolveMethod::Direct);
        const auto fft = iceberg::native::convolve(
            s, ir, iceberg::native::ConvolveMethod::FFT);

        ASSERT_EQ(direct.samples.rows(), fft.samples.rows());
        ASSERT_EQ(direct.samples.cols(), fft.samples.cols());
        const double max_err = max_abs_diff(direct.samples, fft.samples);
        EXPECT_LT(max_err, 1e-5)
            << "n_a=" << c.n_a << " n_b=" << c.n_b
            << " max_abs_err=" << max_err;
        worst_err = std::max(worst_err, max_err);
    }
    record_measure("FFT vs Direct, worst of 4 sizes", worst_err, 1e-5, "abs");
}

// On the reference inputs (1 s of white noise and the direct sound and early reflections of the
// rt_05 IR), the FFT method is compared with the direct sum. The RMS of their difference must
// stay below 1e-6, the 32-bit float rounding of the two methods.
TEST(ConvolveFFT, MatchesDirectOnFixtureCase1) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    // signal.wav (44100) x stages/rt_05 DSER (28366), FFT vs Direct. Tolerance 1e-6 RMS
    // -- FFT introduces float-cast + bin-product noise.
    const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file("signal.wav"));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file("stages/rt_05/core_dser.wav"));

    const auto direct = iceberg::native::convolve(
        signal, ir, iceberg::native::ConvolveMethod::Direct);
    const auto fft = iceberg::native::convolve(
        signal, ir, iceberg::native::ConvolveMethod::FFT);

    ASSERT_EQ(direct.samples.rows(), fft.samples.rows());
    ASSERT_EQ(direct.samples.cols(), fft.samples.cols());

    const double rms = rms_diff(direct.samples, fft.samples);
    record_measure("FFT vs Direct", rms, 1e-6, "rms");
    EXPECT_LT(rms, 1e-6);
}

// For a 1024-sample signal and a 256-sample IR (odd linear length 1279), both methods must
// return 1280 rows, with a zero last row, the row added to make the length even.
TEST(ConvolveFFT, OffByOnePreserved) {
    // Both Direct and FFT must keep the trailing zero row.
    const auto s = random_mono_signal(1024, 7u);
    const auto ir = random_mono_signal(256, 8u);
    for (auto m : {iceberg::native::ConvolveMethod::Direct,
                   iceberg::native::ConvolveMethod::FFT}) {
        const auto out = iceberg::native::convolve(s, ir, m);
        ASSERT_EQ(out.samples.rows(), 1024 + 256);
        EXPECT_TRUE(out.samples.row(out.samples.rows() - 1).isZero())
            << "method=" << static_cast<int>(m);
    }
}

}  // namespace
