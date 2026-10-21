// Tests the 2D VBAP panning of the direct sound (vbap_render and its gain helpers),
// mostly on a ring of four loudspeakers at 0, 90, 180 and 270 deg: output shape, the
// active pair, the block overlap-add, gain invariants, and agreement with the pan
// stage computed by Iceberg MATLAB v1.0.0.

#include "io/wav_loader.h"
#include "rendering/vbap_internals.h"
#include "rendering/vbap_render.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>

namespace {

constexpr double kFs = 44100.0;

using iceberg::test::record_measure;

Eigen::MatrixXd four_cardinal_ls_dir() {
    // ls_dir: [180, 270, 0, 90] with elevation 0.
    Eigen::MatrixXd m(4, 2);
    m << 180, 0,
         270, 0,
           0, 0,
          90, 0;
    return m;
}

iceberg::Signal mono_white_noise(int n, unsigned seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf(n, 1);
    for (int i = 0; i < n; ++i) s.samples(i, 0) = dist(rng);
    s.sample_rate = kFs;
    return s;
}

iceberg::Signal mono_constant(int n, float value = 1.0f) {
    iceberg::Signal s;
    s.samples = Eigen::MatrixXf::Constant(n, 1, value);
    s.sample_rate = kFs;
    return s;
}

// === 1. Single-block sanity: shape + nearest-LS gains pattern ============

// Checks vbap_render on 2410 samples of white noise (one 2400-sample block and a tail)
// panned to 45 deg: the output keeps the input length and sample rate with one column
// per loudspeaker; the loudspeakers at 0 and 90 deg carry signal, and those at 180 and
// 270 deg stay exactly zero.
TEST(VbapRender, SingleBlockSanity) {
    // 2410 samples > blocksize (2400), exercises one full block + tail.
    const auto signal = mono_white_noise(2410);
    const auto out = iceberg::rendering::vbap_render(
        signal, /*angle=*/45.0, 0.0, four_cardinal_ls_dir());

    ASSERT_EQ(out.samples.rows(), 2410);
    ASSERT_EQ(out.samples.cols(), 4);
    EXPECT_DOUBLE_EQ(out.sample_rate, kFs);

    // Source at 45° picks LS at 0° (col 2) and 90° (col 3). Cols 0, 1
    // (180°, 270°) must be all-zero — they're not in the active pair.
    EXPECT_EQ(out.samples.col(0).cwiseAbs().maxCoeff(), 0.0f);
    EXPECT_EQ(out.samples.col(1).cwiseAbs().maxCoeff(), 0.0f);
    EXPECT_GT(out.samples.col(2).cwiseAbs().maxCoeff(), 0.0f);
    EXPECT_GT(out.samples.col(3).cwiseAbs().maxCoeff(), 0.0f);
}

// === 2. Approximate constant-overlap-add ===============================

// Checks the block overlap-add of vbap_render on a constant signal of 24000 samples (ten
// 2400-sample blocks) panned to 45 deg: away from the first and last two blocks, the sum
// of the two active channels must vary by less than 1e-3 peak to trough. The Hanning
// windows at 50 % overlap leave a ripple of about 9e-4 there; a symmetric Hann window
// would leave about the same, so the window type is checked in test_time_window.
TEST(VbapRender, ConstantSignalApproxColaInMiddle) {
    // A constant input of ten blocks at 45 deg: the two active channels should sum to a
    // nearly constant value in the steady-state middle.
    const long n = 24000;  // 10 × blocksize
    const auto signal = mono_constant(static_cast<int>(n), 1.0f);
    const auto out = iceberg::rendering::vbap_render(
        signal, 45.0, 0.0, four_cardinal_ls_dir());

    // Steady-state middle: skip the first 2 blocks and last 2 blocks.
    const long mid_start = 2 * 2400;
    const long mid_end   = n - 2 * 2400;

    // Per-sample sum across active channels (ch2 + ch3).
    double mid_min = std::numeric_limits<double>::infinity();
    double mid_max = -std::numeric_limits<double>::infinity();
    for (long i = mid_start; i < mid_end; ++i) {
        const double s =
            static_cast<double>(out.samples(i, 2)) +
            static_cast<double>(out.samples(i, 3));
        mid_min = std::min(mid_min, s);
        mid_max = std::max(mid_max, s);
    }
    // Constant-overlap-add: hanning(2400) at a hop of 1200 leaves a peak-to-trough ripple
    // of about 6.5e-4 in the window sum, about 9e-4 after the pan gains.
    record_measure("steady-state peak-to-trough", mid_max - mid_min, 1e-3, "abs");
    EXPECT_LT(mid_max - mid_min, 1e-3)
        << "min=" << mid_min << " max=" << mid_max
        << ": the overlap-add of the blocks is not close to constant";
}

// === 3. Angle vs gains in `vbap_2d` directly =============================

// Checks the VBAP gain vector of vbap_2d on the four-loudspeaker ring: a source at 0 or
// 90 deg has its largest gain on the loudspeaker at that angle, and a source at 45 deg
// gets equal gains of sqrt(1/2) on the loudspeakers at 0 and 90 deg, within 1e-12.
TEST(VbapRender, GainsAtCardinalAngles) {
    const Eigen::MatrixXd ls_dir = four_cardinal_ls_dir();
    const Eigen::VectorXd ls_angles = ls_dir.col(0);
    const Eigen::MatrixXi groups =
        iceberg::rendering::detail::find_ls_pairs(ls_angles);
    const Eigen::MatrixXd inv_mtx =
        iceberg::rendering::detail::invert_ls_mtx(ls_angles, groups);

    {  // Source 0°: gain max at LS-0° (idx 2).
        const auto g = iceberg::rendering::detail::vbap_2d(
            0.0, ls_angles, groups, inv_mtx);
        Eigen::Index dom; g.maxCoeff(&dom);
        EXPECT_EQ(dom, 2);
    }
    {  // Source 90°: gain max at LS-90° (idx 3).
        const auto g = iceberg::rendering::detail::vbap_2d(
            90.0, ls_angles, groups, inv_mtx);
        Eigen::Index dom; g.maxCoeff(&dom);
        EXPECT_EQ(dom, 3);
    }
    {  // Source 45°: ch2 == ch3 (equal pan).
        const auto g = iceberg::rendering::detail::vbap_2d(
            45.0, ls_angles, groups, inv_mtx);
        EXPECT_NEAR(g(2), g(3), 1e-12);
        EXPECT_NEAR(g(2), std::sqrt(0.5), 1e-12);
        record_measure("45 deg: gain at 0 deg vs gain at 90 deg", std::abs(g(2) - g(3)), 1e-12,
                       "abs");
        record_measure("45 deg: gain vs sqrt(1/2)", std::abs(g(2) - std::sqrt(0.5)), 1e-12, "abs");
    }
}

// === 4. Mathematical invariant: VBAP constant-power per block ============

// Checks the constant-power invariant of the VBAP gains on the four-loudspeaker ring:
// for 13 source angles, including -1 and 720 deg outside the range 0 to 360 deg, the
// squared gains sum to 1 within 1e-12.
TEST(VbapRender, GainsHaveConstantPower) {
    // For 2D VBAP with cos² + sin² = 1 normalisation, the gain vector
    // for ANY source angle on the ring satisfies Σ g² = 1. This is the
    // property that justifies VBAP as a constant-power pan law. Sample-
    // by-sample MATLAB cross-check does not protect against a future
    // refactor that breaks the invariant within float tolerance.
    const Eigen::MatrixXd ls_dir = four_cardinal_ls_dir();
    const Eigen::VectorXd ls_angles = ls_dir.col(0);
    const Eigen::MatrixXi groups =
        iceberg::rendering::detail::find_ls_pairs(ls_angles);
    const Eigen::MatrixXd inv_mtx =
        iceberg::rendering::detail::invert_ls_mtx(ls_angles, groups);

    double worst = 0.0;
    for (const double angle :
         {0.0, 22.5, 45.0, 67.5, 90.0, 135.0, 180.0, 225.0, 270.0,
          315.0, 359.0, -1.0, 720.0}) {
        const auto g = iceberg::rendering::detail::vbap_2d(
            angle, ls_angles, groups, inv_mtx);
        const double power = g.array().square().sum();
        EXPECT_NEAR(power, 1.0, 1e-12) << "angle = " << angle;
        worst = std::max(worst, std::abs(power - 1.0));
    }
    record_measure("sum of g^2 vs 1, worst angle", worst, 1e-12, "abs");
}

// === 5. Multichannel zeros: opposite LS stay silent ======================

// Checks that vbap_render leaves the loudspeakers outside the active pair silent: with
// 5000 samples of white noise panned to 45 deg, the outputs of the loudspeakers at 180
// and 270 deg are exactly zero.
TEST(VbapRender, OppositeLsAreSilent) {
    const auto signal = mono_white_noise(5000, /*seed=*/7);
    const auto out = iceberg::rendering::vbap_render(
        signal, 45.0, 0.0, four_cardinal_ls_dir());
    EXPECT_EQ(out.samples.col(0).cwiseAbs().maxCoeff(), 0.0f);  // 180°
    EXPECT_EQ(out.samples.col(1).cwiseAbs().maxCoeff(), 0.0f);  // 270°
}

// Checks vbap_render on a signal shorter than one block (100 samples of white noise
// against 2400-sample blocks, mostly zero padding) panned to 45 deg: the output has 100
// samples and four channels, holds no NaN or infinite value, and the loudspeakers at
// 180 and 270 deg stay silent.
TEST(VbapRender, ShortSignalShorterThanBlocksize) {
    // signal length 100 << blocksize (2400). Nhop = ceil(100/1200)+2 = 3,
    // loop runs Nhop - 1 = 2 iterations with heavy zero-padding. Output
    // shape must still match input length and be finite. Defensive
    // coverage; pipeline real always feeds ~132 300 samples.
    const auto signal = mono_white_noise(100, /*seed=*/3);
    const auto out = iceberg::rendering::vbap_render(
        signal, 45.0, 0.0, four_cardinal_ls_dir());

    ASSERT_EQ(out.samples.rows(), 100);
    ASSERT_EQ(out.samples.cols(), 4);
    EXPECT_FALSE(out.samples.array().isNaN().any());
    EXPECT_FALSE(out.samples.array().isInf().any());
    // Source 45° → ch2 and ch3 active; ch0 / ch1 silent regardless of
    // signal length.
    EXPECT_EQ(out.samples.col(0).cwiseAbs().maxCoeff(), 0.0f);
    EXPECT_EQ(out.samples.col(1).cwiseAbs().maxCoeff(), 0.0f);
}

// === 6. Against MATLAB: stages/rt_05, the convolved DSER in, the pan out ===

// Checks vbap_render against the Iceberg MATLAB v1.0.0 pan stage for the rt_05 room: the
// signal convolved with the direct sound and early reflections is panned to 30, 100 and
// 225 deg on the default layout (0, 90, 180, 270 deg). The limit, 2e-7 of each channel's
// peak, covers the float32 rounding of the stored input (MATLAB panned it in double).
TEST(VbapRender, StagesMatchMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto convolved = iceberg::io::load_wav(
        iceberg::test::fixture_file("stages/rt_05/vbap_convolved.wav"));
    const auto ls_dir = iceberg::test::load_case_setup(std::string("sweep/rt_05")).ls_dir;
    double worst_rel = 0.0;
    for (const int angle : {30, 100, 225}) {
        char name[48];
        std::snprintf(name, sizeof(name), "stages/rt_05/vbap_panned_az_%03d.wav", angle);
        const auto expected = iceberg::io::load_wav(iceberg::test::fixture_file(name));
        const auto out = iceberg::rendering::vbap_render(convolved, angle, 0.0, ls_dir);
        ASSERT_EQ(out.samples.rows(), expected.samples.rows());
        ASSERT_EQ(out.samples.cols(), expected.samples.cols());
        const auto d = iceberg::test::diff_stats(out.samples, expected.samples);
        // Floor: the float32 rounding of the input (half an ulp, ~6e-8
        // relative), observed 7.6e-8 .. 9.5e-8 relative, 4.6e-9 .. 6.6e-9 RMS.
        EXPECT_LT(d.worst_rel_peak, 2e-7) << angle;
        worst_rel = std::max(worst_rel, d.worst_rel_peak);
    }
    record_measure("stages/rt_05 VBAP pan, worst angle (az 30, 100, 225)", worst_rel, 2e-7,
                   "rel");
}

}  // namespace
