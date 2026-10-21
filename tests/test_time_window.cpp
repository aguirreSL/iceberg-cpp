// Tests iceberg::native::time_window, which keeps the start of a signal, fades a range out with
// the falling half of a Hann window (or keeps it, for a rectangular window) and zeroes the rest,
// with the bounds rounded as Iceberg MATLAB computes them. Also tests the hann and hanning
// windows of the library against values printed by MATLAB.

#include "native/hann.h"
#include "native/time_window.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>

namespace {

constexpr float kTol = 1e-6f;
constexpr double kPi = 3.14159265358979323846;

using iceberg::test::record_measure;

iceberg::Signal constant_signal(Eigen::Index n, Eigen::Index ch = 1,
                                float value = 1.0f, double fs = 1000.0) {
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(n, ch, value);
    s.sample_rate = fs;
    return s;
}

// MATLAB hann(64), captured via `hann(64)` and printed with
// %.17g. Used to lock the formula choice (hann vs hanning) at 1e-10.
const std::array<double, 64> kHann64Matlab = {
    0,
    0.0024846123172992951, 0.0099137560757280863, 0.022213597106929606,
    0.039261894064796188,  0.060889213314885726,  0.086880612842002547,
    0.11697777844051099,   0.15088159095696357,   0.18825509907063326,
    0.22872686806712028,   0.27189467132341849,   0.31732948781680248,
    0.36457976592849739,   0.41317591116653479,   0.46263495320678782,
    0.51246534586903636,   0.56217185232374256,   0.61126046697815717,
    0.65924332512584216,   0.70564355156530567,   0.75,
    0.79187183611739487,   0.83084291879842964,   0.86652593591491311,
    0.89856625361146114,   0.9266454408160778,    0.95048443395120952,
    0.96984631039295421,   0.98453864311453898,   0.99441541311256421,
    0.99937846060946112,   0.99937846060946112,   0.99441541311256421,
    0.98453864311453898,   0.96984631039295421,   0.95048443395120952,
    0.9266454408160778,    0.89856625361146114,   0.86652593591491311,
    0.83084291879842964,   0.79187183611739487,   0.75,
    0.70564355156530567,   0.65924332512584216,   0.61126046697815717,
    0.56217185232374256,   0.51246534586903636,   0.46263495320678782,
    0.41317591116653479,   0.36457976592849739,   0.31732948781680248,
    0.27189467132341849,   0.22872686806712028,   0.18825509907063326,
    0.15088159095696357,   0.11697777844051099,   0.086880612842002547,
    0.060889213314885726,  0.039261894064796188,  0.022213597106929606,
    0.0099137560757280863, 0.0024846123172992951, 0,
};

// Captured from MATLAB hanning(64).
// Different formula from hann(64) — endpoints non-zero, denom (N+1).
const std::array<double, 64> kHanning64Matlab = {
    0.0023341826411756861, 0.0093149369302932827, 0.020877085445416887,
    0.036912675521111715,  0.057271987173395045,  0.081764930994886631,
    0.11016282296838881,   0.14220051962793945,   0.1775788936319147,
    0.21596762663442204,   0.25700829337869691,   0.3003177082171522,
    0.34549150281252627,   0.39210790161609693,   0.43973165987233837,
    0.4879181273819338,    0.53621740008088126,   0.58417852067351927,
    0.63135368909929335,   0.67730244352126778,   0.72159577279962062,
    0.76382012205306626,   0.80358125390935564,   0.84050792939339847,
    0.87425537408555054,   0.90450849718747373,   0.93098483344002458,
    0.9534371804252727,    0.97165590662887147,   0.98547090871302601,
    0.99475319972552556,   0.99941611341616321,   0.99941611341616321,
    0.99475319972552556,   0.98547090871302601,   0.97165590662887147,
    0.9534371804252727,    0.93098483344002458,   0.90450849718747373,
    0.87425537408555054,   0.84050792939339847,   0.80358125390935564,
    0.76382012205306626,   0.72159577279962062,   0.67730244352126778,
    0.63135368909929335,   0.58417852067351927,   0.53621740008088126,
    0.4879181273819338,    0.43973165987233837,   0.39210790161609693,
    0.34549150281252627,   0.3003177082171522,    0.25700829337869691,
    0.21596762663442204,   0.1775788936319147,    0.14220051962793945,
    0.11016282296838881,   0.081764930994886631,  0.057271987173395045,
    0.036912675521111715,  0.020877085445416887,  0.0093149369302932827,
    0.0023341826411756861,
};

// === Hann formula cross-check ============================================

// hanning(64), the window with non-zero endpoints of the VBAP block overlap-add, is compared
// sample by sample with MATLAB's hanning(64) printed to 17 digits. The limit 1e-10 is far above
// double rounding and far below the difference between the hann and hanning formulas, so it
// identifies the formula.
TEST(Hanning, MatchesMatlabHanning64) {
    const Eigen::ArrayXd w = iceberg::native::hanning(64);
    ASSERT_EQ(w.size(), 64);
    double worst = 0.0;
    for (int i = 0; i < 64; ++i) {
        EXPECT_NEAR(w(i), kHanning64Matlab[i], 1e-10) << "at i = " << i;
        worst = std::max(worst, std::abs(w(i) - kHanning64Matlab[i]));
    }
    record_measure("hanning(64) vs MATLAB, worst sample", worst, 1e-10, "abs");
}

// The first and last values of hanning(n), whose formula divides by n + 1, must be strictly
// positive for n = 2, 8, 31, 100 and 2400.
TEST(Hanning, EndpointsNonZero) {
    // Key behavioural difference from hann(): hanning's endpoints are
    // strictly positive (matters for OLA boundary smoothness).
    for (int n : {2, 8, 31, 100, 2400}) {
        const Eigen::ArrayXd w = iceberg::native::hanning(n);
        EXPECT_GT(w(0), 0.0);
        EXPECT_GT(w(n - 1), 0.0);
    }
}

// hann(64), the symmetric Hann window that shapes the fade-out of time_window, is compared
// sample by sample with MATLAB's hann(64) printed to 17 digits. The limit 1e-10 is far above
// double rounding and far below the difference between the hann and hanning formulas, so it
// identifies the formula.
TEST(Hann, MatchesMatlabHann64) {
    const Eigen::ArrayXd w = iceberg::native::hann(64);
    ASSERT_EQ(w.size(), 64);
    double worst = 0.0;
    for (int i = 0; i < 64; ++i) {
        EXPECT_NEAR(w(i), kHann64Matlab[i], 1e-10) << "at i = " << i;
        worst = std::max(worst, std::abs(w(i) - kHann64Matlab[i]));
    }
    record_measure("hann(64) vs MATLAB, worst sample", worst, 1e-10, "abs");
}

// The first and last values of the symmetric hann(n) must be zero within 1e-15 (double
// rounding) for n = 2, 8, 31, 100 and 1024.
TEST(Hann, EndpointsAreZero) {
    double worst = 0.0;
    for (int n : {2, 8, 31, 100, 1024}) {
        const Eigen::ArrayXd w = iceberg::native::hann(n);
        EXPECT_NEAR(w(0),     0.0, 1e-15);
        EXPECT_NEAR(w(n - 1), 0.0, 1e-15);
        worst = std::max({worst, std::abs(w(0)), std::abs(w(n - 1))});
    }
    record_measure("endpoints, worst length", worst, 1e-15, "abs");
}

// === time_window behaviour tests =========================================

// A rectangular window on [0.1, 0.3] s over 1 s of ones at 1 kHz: the bounds become the 0-based
// samples 101 and 301 (rounded, then moved up to even 1-based numbers, as Iceberg MATLAB computes
// them). Samples before and inside the range must stay 1 and samples after it must be 0, within
// 1e-6 (the rounding of 32-bit floats).
TEST(TimeWindow, RectwinThreeRegions) {
    // 1 s @ 1000 Hz, range [0.1, 0.3]:
    //   startSampleRaw = 101 → bump to 102 (1-based) → start0 = 101.
    //   endSampleRaw   = 301 → bump to 302 (1-based) → end0   = 301.
    const auto s = constant_signal(1000);
    const auto out = iceberg::native::time_window(
        s, 0.1, 0.3, iceberg::native::WindowType::Rectwin);

    ASSERT_EQ(out.samples.rows(), 1000);
    EXPECT_NEAR(out.samples(50,  0), 1.0f, kTol);  // before start: untouched
    EXPECT_NEAR(out.samples(101, 0), 1.0f, kTol);  // first windowed sample
    EXPECT_NEAR(out.samples(200, 0), 1.0f, kTol);  // mid-window: 1 * 1.0
    EXPECT_NEAR(out.samples(301, 0), 1.0f, kTol);  // last windowed sample
    EXPECT_NEAR(out.samples(302, 0), 0.0f, kTol);  // first zeroed sample
    EXPECT_NEAR(out.samples(900, 0), 0.0f, kTol);  // way past end: 0
    record_measure("checked samples, worst",
                   std::max({std::abs(out.samples(50, 0) - 1.0f),
                             std::abs(out.samples(101, 0) - 1.0f),
                             std::abs(out.samples(200, 0) - 1.0f),
                             std::abs(out.samples(301, 0) - 1.0f), std::abs(out.samples(302, 0)),
                             std::abs(out.samples(900, 0))}),
                   kTol, "abs");
}

// A Hann fade-out on [0, 0.5] s over 1 s of ones at 1 kHz applies the falling half of hann(1002)
// to samples 1 to 501. Sample 0 must stay 1, sample 1 must exceed 0.999, sample 500 must be below
// 0.001, samples 501 and 700 must be 0 (within 1e-6), and the fade must decrease monotonically
// (a rise of up to 1e-7, the rounding of 32-bit floats, is tolerated).
TEST(TimeWindow, HannEndpointsAndDecay) {
    // Range [0, 0.5] @ 1000 Hz on 1000 samples → windowLength = 501.
    // Slice = last 501 samples of hann(1002).
    //   slice[0]   = hann(1002)[501] ≈ peak (~1.0)
    //   slice[500] = hann(1002)[1001] = 0 exactly.
    const auto s = constant_signal(1000);
    const auto out = iceberg::native::time_window(
        s, 0.0, 0.5, iceberg::native::WindowType::Hann);

    // Sample at start_sec = 0.0 → idx 0. start_sec maps to startSampleRaw=1
    // which bumps to startSample=2 (1-based) → start0=1. So idx 0 is
    // before the windowed region and stays 1.0.
    EXPECT_NEAR(out.samples(0, 0), 1.0f, kTol);

    // First windowed sample (idx 1) ≈ slice[0] ≈ 1.0.
    EXPECT_GT(out.samples(1, 0), 0.999f);

    // Sample at end_sec = 0.5 → idx 500. With end0 = 501, idx 500 is at
    // slice[499] which is very close to 0 but not exactly 0.
    EXPECT_LT(out.samples(500, 0), 0.001f);

    // End of windowed region (idx 501) = slice[500] = 0.
    EXPECT_NEAR(out.samples(501, 0), 0.0f, kTol);

    // Past the windowed region: zeroed.
    EXPECT_NEAR(out.samples(700, 0), 0.0f, kTol);

    // Decay should be monotonic from slice[0] through slice[500].
    float largest_rise = -std::numeric_limits<float>::infinity();
    for (Eigen::Index i = 1; i < 500; ++i) {
        EXPECT_GE(out.samples(i, 0) + 1e-7f, out.samples(i + 1, 0))
            << "non-monotonic at i = " << i;
        largest_rise = std::max(largest_rise, out.samples(i + 1, 0) - out.samples(i, 0));
    }
    record_measure("samples 0, 501 and 700, worst",
                   std::max({std::abs(out.samples(0, 0) - 1.0f), std::abs(out.samples(501, 0)),
                             std::abs(out.samples(700, 0))}),
                   kTol, "abs");
    record_measure("first windowed sample, 1 - value", 1.0f - out.samples(1, 0), 1.0f - 0.999f,
                   "abs");
    record_measure("sample at end_sec", out.samples(500, 0), 0.001f, "abs");
    record_measure("largest rise in the decay", largest_rise, 1e-7f, "abs");
}

// The first faded sample of a Hann fade-out on [0, 0.5] s at 1 kHz must equal the analytic value
// hann(1002)[501] = 0.5 (1 - cos(2 pi 501 / 1001)) within 1e-6, the rounding of the 32-bit float
// output. This checks the slice of the window; at this index hann and hanning differ by about
// 1e-8, so the formula itself is checked by Hann.MatchesMatlabHann64.
TEST(TimeWindow, HannSliceStartMatchesFormula) {
    // Confirms the slice math: the first windowed sample equals
    // hann(2*windowLength)[windowLength] in double precision.
    // (The hann-vs-hanning discrimination lives in
    //  Hann.MatchesMatlabHann64, where the 1e-10 tolerance vs MATLAB
    //  values reliably catches the wrong formula. At this slice index
    //  the two formulas agree to ~1e-8, so they're indistinguishable
    //  here.)
    const auto s = constant_signal(1000);
    const auto out = iceberg::native::time_window(
        s, 0.0, 0.5, iceberg::native::WindowType::Hann);

    // windowLength = 501, outer length = 1002, slice[0] = hann(1002)[501].
    const double expected_hann =
        0.5 * (1.0 - std::cos(2.0 * kPi * 501.0 / 1001.0));
    record_measure("first windowed sample vs hann(1002)[501]",
                   std::abs(static_cast<double>(out.samples(1, 0)) - expected_hann), 1e-6, "abs");
    EXPECT_NEAR(static_cast<double>(out.samples(1, 0)), expected_hann,
                1e-6);
}

// On 999 ones at 1 kHz, a rectangular window on [0, 0.4] s has the odd raw end bound 401
// (1-based), which is moved up to 402, as Iceberg MATLAB computes it. Sample 401 (0-based) must
// therefore stay 1 and sample 402 must be 0, within 1e-6 (the rounding of 32-bit floats).
TEST(TimeWindow, OddIndicesBumpedToEven) {
    // nSamples = 999 (odd), range [0, 0.4] → endSampleRaw = 401 (odd).
    // After even-bump, endSample = 402 → end0 = 401. With rectwin, idx 401
    // should still be 1.0; without the bump, end0 would be 400 and idx 401
    // would be 0.
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(999, 1, 1.0f);
    s.sample_rate = 1000.0;

    const auto out = iceberg::native::time_window(
        s, 0.0, 0.4, iceberg::native::WindowType::Rectwin);

    EXPECT_NEAR(out.samples(401, 0), 1.0f, kTol);  // bumped end included
    EXPECT_NEAR(out.samples(402, 0), 0.0f, kTol);  // first zeroed sample
    record_measure("checked samples, worst",
                   std::max(std::abs(out.samples(401, 0) - 1.0f), std::abs(out.samples(402, 0))),
                   kTol, "abs");
}

// An end of 0 s means the end of the signal. On 99 ones at 1 kHz the end bound N + 1 = 100 is
// brought back to 99, and the last sample (0-based 98) of a rectangular window must stay 1
// within 1e-6 (the rounding of 32-bit floats).
TEST(TimeWindow, EndZeroPdiKeepsLastSample) {
    // nSamples = 99 (odd) → end_sec=0 produces endSampleRaw = 100 (even,
    // no bump) → endSample = 100 → PDI fix to 99. Without the fix, clamp
    // catches it; either way the last sample (idx 98) must remain.
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(99, 1, 1.0f);
    s.sample_rate = 1000.0;

    const auto out = iceberg::native::time_window(
        s, 0.0, 0.0, iceberg::native::WindowType::Rectwin);

    ASSERT_EQ(out.samples.rows(), 99);
    EXPECT_NEAR(out.samples(98, 0), 1.0f, kTol);
    record_measure("last sample", std::abs(out.samples(98, 0) - 1.0f), kTol, "abs");
}

// A start (0.5 s) after the end (0.1 s) gives a window length below 1, and the result must be a
// signal with no rows, the 3 columns of the input and its sample rate of 1000 Hz.
TEST(TimeWindow, InvertedRangeReturnsEmpty) {
    // start_sec > end_sec → after even-bump and clamp, windowLength
    // collapses negative and the function returns the empty-output
    // branch. Without this case the `windowLength <= 0` early-return
    // path (time_window.cpp:60-63) is uncovered.
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(1000, 3, 1.0f);
    s.sample_rate = 1000.0;

    const auto out = iceberg::native::time_window(
        s, 0.5, 0.1, iceberg::native::WindowType::Hann);

    EXPECT_EQ(out.samples.rows(), 0);
    EXPECT_EQ(out.samples.cols(), 3);   // column count preserved
    EXPECT_DOUBLE_EQ(out.sample_rate, 1000.0);
}

// A Hann fade-out on [0.1, 0.3] s over four channels of ones must be applied identically to
// every channel: channels 1 to 3 are compared with channel 0, sample by sample, within 1e-6
// (the rounding of 32-bit floats).
TEST(TimeWindow, MultichannelUniformWindow) {
    // Same window must be applied identically across all channels.
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(1000, 4, 1.0f);
    s.sample_rate = 1000.0;

    const auto out = iceberg::native::time_window(
        s, 0.1, 0.3, iceberg::native::WindowType::Hann);

    float worst = 0.0f;
    for (Eigen::Index i = 0; i < out.samples.rows(); ++i) {
        const float v0 = out.samples(i, 0);
        for (Eigen::Index c = 1; c < 4; ++c) {
            EXPECT_NEAR(out.samples(i, c), v0, kTol)
                << "ch " << c << " differs from ch 0 at i = " << i;
            worst = std::max(worst, std::abs(out.samples(i, c) - v0));
        }
    }
    record_measure("channels 1-3 vs channel 0, worst", worst, kTol, "abs");
}

}  // namespace
