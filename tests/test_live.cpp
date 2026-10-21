// Tests of live source tracking (the live engine and programmed trajectories) and of its
// building blocks, the real FFT and the partitioned convolver. With the angle held still
// the engine must reproduce the static render and the Iceberg MATLAB v1.0.0 reference to
// 32-bit float rounding; a moving source must not jump, and the audio path must not allocate.

#include "experiment/slot_config.h"
#include "io/wav_loader.h"
#include "native/fft.h"
#include "native/partitioned_convolver.h"
#include "native/realtime_fft.h"
#include "rendering/iceberg.h"
#include "rendering/live.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

// Counts operator new while armed (std::vector, std::function, ...; Eigen
// allocates through malloc, and the engine's Eigen objects are sized at
// construction).
namespace {
std::atomic<bool> g_count_allocations{false};
std::atomic<long> g_allocations{0};
}  // namespace

void* operator new(std::size_t n) {
    if (g_count_allocations.load()) g_allocations.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

using iceberg::rendering::LiveEngine;
using iceberg::rendering::LiveRender;
using iceberg::test::record_measure;

Eigen::VectorXd random_vector(Eigen::Index n, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> n01(0.0, 1.0);
    Eigen::VectorXd v(n);
    for (Eigen::Index i = 0; i < n; ++i) v(i) = n01(rng);
    return v;
}

// === RealFft and PartitionedConvolver =====================================

// RealFft is compared with the pocketfft real FFT on Gaussian noise for every power of 2
// from 4 to 8192: the forward spectrum within 1e-13 of the largest bin, and the inverse of
// the reference spectrum within 1e-13 of the input peak (64-bit float rounding). A size
// that is not a power of 2 (12) must throw std::invalid_argument.
TEST(RealtimeFft, MatchesPocketfft) {
    double worst_forward = 0.0, worst_inverse = 0.0;
    for (int n = 4; n <= 8192; n *= 2) {
        const Eigen::VectorXd x = random_vector(n, static_cast<unsigned>(n));
        iceberg::native::RealFft fft(n);
        Eigen::VectorXcd spectrum(n / 2 + 1);
        fft.forward(x.data(), spectrum.data());
        const Eigen::VectorXcd ref = iceberg::native::rfft(x);
        EXPECT_LT((spectrum - ref).cwiseAbs().maxCoeff() / ref.cwiseAbs().maxCoeff(), 1e-13) << n;
        worst_forward = std::max(
            worst_forward, (spectrum - ref).cwiseAbs().maxCoeff() / ref.cwiseAbs().maxCoeff());
        Eigen::VectorXd back(n);
        fft.inverse(ref.data(), back.data());
        EXPECT_LT((back - x).cwiseAbs().maxCoeff() / x.cwiseAbs().maxCoeff(), 1e-13) << n;
        worst_inverse =
            std::max(worst_inverse, (back - x).cwiseAbs().maxCoeff() / x.cwiseAbs().maxCoeff());
    }
    record_measure("forward vs pocketfft, worst size", worst_forward, 1e-13, "rel");
    record_measure("inverse round trip, worst size", worst_inverse, 1e-13, "rel");
    EXPECT_THROW(iceberg::native::RealFft(12), std::invalid_argument);
}

// The partitioned convolver, fed 3001 samples of noise and then zeros in blocks of 2, 64,
// 256 and 1024 frames, is compared with the direct linear convolution by three 777-tap
// filters: within 1e-12 of the reference peak (64-bit float rounding), and below 1e-12
// after the end of the convolution.
TEST(PartitionedConvolver, IsTheLinearConvolution) {
    const Eigen::VectorXd x = random_vector(3001, 1);
    Eigen::MatrixXd filters(777, 3);
    for (int c = 0; c < 3; ++c) filters.col(c) = random_vector(777, 10u + static_cast<unsigned>(c));
    double worst_d = 0.0, worst_tail = 0.0;
    for (const int block : {2, 64, 256, 1024}) {
        iceberg::native::PartitionedConvolver conv(filters, block);
        const Eigen::Index n_out = x.size() + filters.rows() - 1;
        const Eigen::Index n_blocks = (n_out + block - 1) / block;
        Eigen::MatrixXd out(n_blocks * block, 3);
        std::vector<double> in(static_cast<size_t>(block));
        std::vector<std::vector<double>> y(3, std::vector<double>(static_cast<size_t>(block)));
        std::vector<double*> ptrs = {y[0].data(), y[1].data(), y[2].data()};
        for (Eigen::Index b = 0; b < n_blocks; ++b) {
            for (int t = 0; t < block; ++t) {
                const Eigen::Index i = b * block + t;
                in[static_cast<size_t>(t)] = i < x.size() ? x(i) : 0.0;
            }
            conv.process(in.data(), ptrs.data());
            for (int c = 0; c < 3; ++c) {
                for (int t = 0; t < block; ++t) out(b * block + t, c) = y[static_cast<size_t>(c)][static_cast<size_t>(t)];
            }
        }
        for (int c = 0; c < 3; ++c) {
            Eigen::VectorXd ref = Eigen::VectorXd::Zero(n_out);
            for (Eigen::Index i = 0; i < x.size(); ++i) {
                ref.segment(i, filters.rows()) += x(i) * filters.col(c);
            }
            const double d = (out.col(c).head(n_out) - ref).cwiseAbs().maxCoeff() /
                             ref.cwiseAbs().maxCoeff();
            EXPECT_LT(d, 1e-12) << "block " << block << " output " << c;
            EXPECT_LT(out.col(c).tail(out.rows() - n_out).cwiseAbs().maxCoeff(), 1e-12);
            worst_d = std::max(worst_d, d);
            worst_tail = std::max(worst_tail,
                                  out.col(c).tail(out.rows() - n_out).cwiseAbs().maxCoeff());
        }
    }
    record_measure("vs direct convolution, worst block and output", worst_d, 1e-12, "rel");
    record_measure("tail after the convolution, worst", worst_tail, 1e-12, "abs");
}

// === The engine ===========================================================
Eigen::MatrixXf run_engine(const std::shared_ptr<const LiveRender>& r, double angle,
                           int chunk = 300) {
    LiveEngine engine(r, angle);
    Eigen::MatrixXf out(r->length, r->n_master);
    std::vector<std::vector<float>> buf(static_cast<size_t>(r->n_master),
                                        std::vector<float>(static_cast<size_t>(chunk)));
    std::vector<float*> ptrs;
    for (auto& b : buf) ptrs.push_back(b.data());
    for (Eigen::Index done = 0; done < r->length; done += chunk) {
        engine.render(ptrs.data(), static_cast<int>(ptrs.size()), chunk);
        const Eigen::Index n = std::min<Eigen::Index>(chunk, r->length - done);
        for (Eigen::Index c = 0; c < r->n_master; ++c) {
            for (Eigen::Index i = 0; i < n; ++i) out(done + i, c) = buf[static_cast<size_t>(c)][static_cast<size_t>(i)];
        }
    }
    EXPECT_TRUE(engine.finished());
    return out;
}

iceberg::rendering::ConfigSetup default_slot_config() {
    iceberg::experiment::Slot slot;
    slot.use_full_scale = false;
    slot.cal_path = iceberg::test::fixture_file("inputs/four_alone/calibration_4ls.json");
    return iceberg::experiment::slot_render_config(slot);
}

// The live engine held at a fixed angle is compared with the static render iceberg() for
// the app's default slot (4 loudspeakers, signal.wav, rt_05, 80 dB SPL) every 5 deg (72
// angles): within 2e-9 RMS and 5e-7 of the channel peak, the rounding of 32-bit floats.
TEST(LiveEngine, FrozenAngleIsTheStaticRender) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto cfg = default_slot_config();
    const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file("signal.wav"));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file("sweep/rt_05/input_ir.wav"));
    const auto r = std::make_shared<const LiveRender>(iceberg::rendering::prepare_live(signal, ir, 80.0, cfg));
    double worst_rms = 0.0, worst_rel = 0.0;
    for (int deg = 0; deg < 360; deg += 5) {
        const auto stat = iceberg::rendering::iceberg(signal, ir, deg, 80.0, cfg);
        ASSERT_EQ(stat.samples.rows(), r->length);
        const auto d = iceberg::test::diff_stats(run_engine(r, deg), stat.samples);
        worst_rms = std::max(worst_rms, d.worst_rms);
        worst_rel = std::max(worst_rel, d.worst_rel_peak);
    }
    record_measure("default slot held still vs iceberg(), worst RMS (72 angles)", worst_rms,
                   2e-9, "rms");
    record_measure("default slot held still vs iceberg(), worst rel peak (72 angles)", worst_rel,
                   5e-7, "rel");
    EXPECT_LT(worst_rms, 2e-9);
    EXPECT_LT(worst_rel, 5e-7);
}

// The live engine held at each reference angle is compared with the reference data
// generated by Iceberg MATLAB v1.0.0: the 5-degree sweep with the rt_05, rt_11 and rt_00
// (anechoic) IRs and the 8- and 24-loudspeaker grids, 222 renders. Limits: 2e-9 RMS and
// 5e-7 of the channel peak, the rounding of 32-bit floats.
TEST(LiveEngine, FrozenAngleMatchesMatlab) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    double worst_rms = 0.0, worst_rel = 0.0;
    int renders = 0;
    for (const std::string case_dir : {"sweep/rt_05", "sweep/rt_11", "sweep/rt_00",
                                       "inputs/grid_8", "inputs/grid_24"}) {
        const auto cfg = iceberg::test::load_case_setup(case_dir);
        const auto j = iceberg::test::load_fixture_json(case_dir + "/config.json");
        const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file(j.at("signal").get<std::string>()));
        const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file(case_dir + "/input_ir.wav"));
        const auto r = std::make_shared<const LiveRender>(
            iceberg::rendering::prepare_live(signal, ir, iceberg::test::case_level(j), cfg));
        for (const double angle : j.at("angles").get<std::vector<double>>()) {
            char az[16];
            std::snprintf(az, sizeof(az), "/az_%03d.wav", static_cast<int>(angle));
            const auto expected = iceberg::io::load_wav(iceberg::test::fixture_file(case_dir + az));
            const Eigen::MatrixXf active =
                iceberg::test::active_columns(run_engine(r, angle), cfg.active_ls_numbers);
            const auto d = iceberg::test::diff_stats(active, expected.samples);
            EXPECT_LT(d.worst_rms, 2e-9) << case_dir << " " << angle;
            EXPECT_LT(d.worst_rel_peak, 5e-7) << case_dir << " " << angle;
            worst_rms = std::max(worst_rms, d.worst_rms);
            worst_rel = std::max(worst_rel, d.worst_rel_peak);
            ++renders;
        }
    }
    const std::string n_renders = " (" + std::to_string(renders) + " MATLAB renders)";
    record_measure("engine held still, worst RMS" + n_renders, worst_rms, 2e-9, "rms");
    record_measure("engine held still, worst rel peak" + n_renders, worst_rel, 5e-7, "rel");
}

// The engine's output does not depend on the audio callback size: the default slot at
// 30 deg, rendered in callbacks of 64 and of 1000 frames, must be bit-identical.
TEST(LiveEngine, OutputDoesNotDependOnTheCallbackSize) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto cfg = default_slot_config();
    const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file("signal.wav"));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file("sweep/rt_05/input_ir.wav"));
    const auto r = std::make_shared<const LiveRender>(iceberg::rendering::prepare_live(signal, ir, 80.0, cfg));
    const Eigen::MatrixXf a = run_engine(r, 30.0, 64);
    const Eigen::MatrixXf b = run_engine(r, 30.0, 1000);
    EXPECT_TRUE(a == b);
}

// A source moving twice round the ring during playback (default slot, 256-frame
// callbacks): its largest sample-to-sample step must stay below 1.5 times the largest
// step of static renders every 15 deg (gains are ramped, the late switch is faded), and
// render() must perform no heap allocation, counted through a replaced operator new.
TEST(LiveEngine, MovingSourceIsContinuousAndDoesNotAllocate) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto cfg = default_slot_config();
    const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file("signal.wav"));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file("sweep/rt_05/input_ir.wav"));
    const auto r = std::make_shared<const LiveRender>(iceberg::rendering::prepare_live(signal, ir, 80.0, cfg));

    double static_step = 0.0;
    for (int deg = 0; deg < 360; deg += 15) {
        const Eigen::MatrixXf s = run_engine(r, deg);
        static_step = std::max(static_step, static_cast<double>(
            (s.bottomRows(s.rows() - 1) - s.topRows(s.rows() - 1)).cwiseAbs().maxCoeff()));
    }

    LiveEngine engine(r, 0.0);
    const int chunk = 256;
    std::vector<std::vector<float>> buf(static_cast<size_t>(r->n_master),
                                        std::vector<float>(static_cast<size_t>(chunk)));
    std::vector<float*> ptrs;
    for (auto& b : buf) ptrs.push_back(b.data());
    Eigen::MatrixXf out(r->length, r->n_master);
    g_allocations.store(0);
    for (Eigen::Index done = 0; done < r->length; done += chunk) {
        engine.set_angle(720.0 * static_cast<double>(done) / static_cast<double>(r->length));
        g_count_allocations.store(true);
        engine.render(ptrs.data(), static_cast<int>(ptrs.size()), chunk);
        g_count_allocations.store(false);
        const Eigen::Index n = std::min<Eigen::Index>(chunk, r->length - done);
        for (Eigen::Index c = 0; c < r->n_master; ++c) {
            for (Eigen::Index i = 0; i < n; ++i) out(done + i, c) = buf[static_cast<size_t>(c)][static_cast<size_t>(i)];
        }
    }
    EXPECT_EQ(g_allocations.load(), 0);
    const double moving_step = (out.bottomRows(out.rows() - 1) - out.topRows(out.rows() - 1))
                                   .cwiseAbs().maxCoeff();
    record_measure("moving source's largest step / static renders' largest",
                   moving_step / static_step, 1.5, "rel");
    EXPECT_LT(moving_step, 1.5 * static_step);
}

// === Programmed paths =====================================================
using iceberg::rendering::TrajectoryPoint;

// trajectory_angle() on the path 0:0, 2:90, 3:90, 5:-90 is checked against exact values:
// held before the first and after the last point, linear in between, constant on a hold.
// Angles are not wrapped, so 0 -> 270 over 3 s passes 180 deg at 2 s.
TEST(Trajectory, AngleFollowsThePathAsWritten) {
    const std::vector<TrajectoryPoint> p = {{0.0, 0.0}, {2.0, 90.0}, {3.0, 90.0}, {5.0, -90.0}};
    EXPECT_DOUBLE_EQ(iceberg::rendering::trajectory_angle(p, -1.0), 0.0);    // held before
    EXPECT_DOUBLE_EQ(iceberg::rendering::trajectory_angle(p, 1.0), 45.0);
    EXPECT_DOUBLE_EQ(iceberg::rendering::trajectory_angle(p, 2.5), 90.0);    // holds
    EXPECT_DOUBLE_EQ(iceberg::rendering::trajectory_angle(p, 4.0), 0.0);     // through the front
    EXPECT_DOUBLE_EQ(iceberg::rendering::trajectory_angle(p, 9.0), -90.0);   // held after
    // Not wrapped: 0 -> 270 goes through 90 and 180.
    EXPECT_DOUBLE_EQ(iceberg::rendering::trajectory_angle({{0.0, 0.0}, {3.0, 270.0}}, 2.0), 180.0);
}

// parse_trajectory() reads the slot text of "time:angle" pairs separated by commas and/or
// spaces, format_trajectory() writes it back in canonical form, and blank text gives an
// empty path. Malformed text throws std::invalid_argument: repeated or decreasing times,
// a missing angle, a non-number, three fields, a negative time and NaN.
TEST(Trajectory, ParsesAndFormatsTheSlotText) {
    const auto p = iceberg::rendering::parse_trajectory(" 0:0, 2:90 3:90,5:-90.5 ");
    ASSERT_EQ(p.size(), 4u);
    EXPECT_DOUBLE_EQ(p[3].angle_deg, -90.5);
    EXPECT_EQ(iceberg::rendering::format_trajectory(p), "0:0, 2:90, 3:90, 5:-90.5");
    EXPECT_TRUE(iceberg::rendering::parse_trajectory("  ").empty());
    for (const std::string bad : {"0:0, 0:90", "0", "a:1", "1:2:3", "-1:0", "1:nan", "2:0, 1:0"}) {
        EXPECT_THROW(iceberg::rendering::parse_trajectory(bad), std::invalid_argument) << bad;
    }
}

// append_trajectory_point(), the slot panel's "Add point", against the expected path text:
// the first point at time 0, each next one move_s later and the short way round (350 then
// 10 is written 370), a hold adding the point where it leaves, angles rounded to 2
// decimals and times to 1 ms. move_s <= 0 or a negative hold throws std::invalid_argument.
TEST(Trajectory, AddPointBuildsThePath) {
    using iceberg::rendering::append_trajectory_point;
    using iceberg::rendering::format_trajectory;
    std::vector<TrajectoryPoint> p;
    p = append_trajectory_point(p, 0.0, 1.0, 1.0);
    p = append_trajectory_point(p, 90.0, 1.0, 1.5);
    p = append_trajectory_point(p, 180.0, 1.0, 0.5);
    EXPECT_EQ(format_trajectory(p), "0:0, 1:0, 2:90, 3.5:90, 4.5:180, 5:180");
    p = append_trajectory_point(p, 10.0, 2.0, 0.0);        // 180 -> 10 the short way: down to 10
    EXPECT_EQ(format_trajectory(p).substr(format_trajectory(p).rfind(',') + 2), "7:10");
    std::vector<TrajectoryPoint> q = append_trajectory_point({}, 350.0, 1.0, 0.0);
    q = append_trajectory_point(q, 10.0, 1.0, 0.0);          // 350 -> 370, through 0
    EXPECT_EQ(format_trajectory(q), "0:350, 1:370");
    q = append_trajectory_point(q, 340.0, 1.0, 0.0);         // 370 -> 340, back through 0
    EXPECT_DOUBLE_EQ(q.back().angle_deg, 340.0);
    // The pad gives any angle; the path keeps 2 decimals, times in ms.
    std::vector<TrajectoryPoint> r = append_trajectory_point({}, 87.374219, 1.0, 1.05);
    r = append_trajectory_point(r, 132.0061, 1.05, 0.0);
    EXPECT_EQ(format_trajectory(r), "0:87.37, 1.05:87.37, 2.1:132.01");
    EXPECT_EQ(format_trajectory(iceberg::rendering::parse_trajectory("0:10.126, 1:-3.004")),
              "0:10.13, 1:-3");
    EXPECT_THROW(append_trajectory_point(q, 0.0, 0.0, 1.0), std::invalid_argument);
    EXPECT_THROW(append_trajectory_point(q, 0.0, 1.0, -1.0), std::invalid_argument);
}

// render_trajectory() with a path held at 100 deg is compared with iceberg() at 100 deg
// (default slot): within 2e-9 RMS and 5e-7 of the channel peak, the rounding of 32-bit
// floats. A moving path (0, 270, -45 deg) rendered twice must be bit-identical and as
// long as the render.
TEST(Trajectory, RenderHoldsToTheStaticRenderAndRepeats) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const auto cfg = default_slot_config();
    const auto signal = iceberg::io::load_wav(iceberg::test::fixture_file("signal.wav"));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file("sweep/rt_05/input_ir.wav"));
    const auto r = std::make_shared<const LiveRender>(iceberg::rendering::prepare_live(signal, ir, 80.0, cfg));

    const auto still = iceberg::rendering::render_trajectory(r, {{0.0, 100.0}, {1.0, 100.0}});
    const auto stat = iceberg::rendering::iceberg(signal, ir, 100.0, 80.0, cfg);
    const auto d = iceberg::test::diff_stats(still.samples, stat.samples);
    record_measure("still path vs iceberg(), worst RMS", d.worst_rms, 2e-9, "rms");
    record_measure("still path vs iceberg(), worst rel peak", d.worst_rel_peak, 5e-7, "rel");
    EXPECT_LT(d.worst_rms, 2e-9);
    EXPECT_LT(d.worst_rel_peak, 5e-7);

    const std::vector<TrajectoryPoint> path = {{0.0, 0.0}, {0.5, 270.0}, {1.2, -45.0}};
    const auto a = iceberg::rendering::render_trajectory(r, path);
    const auto b = iceberg::rendering::render_trajectory(r, path);
    EXPECT_TRUE(a.samples == b.samples);
    EXPECT_EQ(a.samples.rows(), r->length);
}

// Constructing a LiveEngine without a prepared render (a null pointer) throws
// std::invalid_argument.
TEST(LiveEngine, RejectsABadBlock) {
    EXPECT_THROW(LiveEngine(nullptr, 0.0), std::invalid_argument);
}

}  // namespace
