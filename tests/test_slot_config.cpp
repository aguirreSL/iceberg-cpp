// Tests for experiment/slot_config: how an experiment slot becomes a render
// configuration (the loudspeaker layout, the calibration and the Iceberg
// v1.0.0 options), and how a slot is prepared for the live engine.
#include "experiment/slot_config.h"
#include "io/wav_loader.h"
#include "rendering/iceberg.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace exp = iceberg::experiment;
using iceberg::rendering::AmbisonicFormat;
using iceberg::rendering::AzimuthConvention;
using iceberg::rendering::DserFactor;

std::string write_tmp_json(const std::string& name, const std::string& content) {
    const auto dir = std::filesystem::temp_directory_path() / "iceberg_slot_config_tests";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / name).string();
    std::ofstream(path) << content;
    return path;
}

void expect_layout(const iceberg::rendering::ConfigSetup& cfg,
                   const std::vector<int>& ls_array, const std::vector<int>& active,
                   const std::vector<double>& ls_dir_azimuths) {
    ASSERT_EQ(cfg.ls_array.size(), static_cast<Eigen::Index>(ls_array.size()));
    for (size_t i = 0; i < ls_array.size(); ++i) {
        EXPECT_EQ(cfg.ls_array(static_cast<Eigen::Index>(i)), ls_array[i]) << i;
    }
    ASSERT_EQ(cfg.active_ls_numbers.size(), static_cast<Eigen::Index>(active.size()));
    for (size_t i = 0; i < active.size(); ++i) {
        EXPECT_EQ(cfg.active_ls_numbers(static_cast<Eigen::Index>(i)), active[i]) << i;
    }
    ASSERT_EQ(cfg.ls_dir.rows(), static_cast<Eigen::Index>(ls_dir_azimuths.size()));
    ASSERT_EQ(cfg.ls_dir.cols(), 2);
    for (size_t i = 0; i < ls_dir_azimuths.size(); ++i) {
        EXPECT_DOUBLE_EQ(cfg.ls_dir(static_cast<Eigen::Index>(i), 0), ls_dir_azimuths[i]) << i;
        EXPECT_DOUBLE_EQ(cfg.ls_dir(static_cast<Eigen::Index>(i), 1), 0.0) << i;
    }
}

// === slot_layout ===========================================================

// A slot with no layout file gets the four default loudspeakers: azimuths 0,
// 90, 180 and 270 degrees on output channels 1 to 4, all active and all at
// zero elevation.
TEST(SlotLayout, NoLayoutFileGivesTheFourDefaultLoudspeakers) {
    const exp::Slot slot;
    expect_layout(exp::slot_layout(slot), {0, 90, 180, 270}, {1, 2, 3, 4},
                  {0, 90, 180, 270});
}

// With no layout file, entry i of the slot's loudspeaker order is the azimuth
// of output channel i + 1: the order 180, 270, 0, 90 must appear as given in
// the azimuth array and the direction matrix, with all four channels active.
TEST(SlotLayout, DefaultOrderIsPerOutputChannel) {
    exp::Slot slot;
    slot.default_ls_order = {180, 270, 0, 90};
    expect_layout(exp::slot_layout(slot), {180, 270, 0, 90}, {1, 2, 3, 4},
                  {180, 270, 0, 90});
}

// A loudspeaker order that repeats an azimuth (0, 90, 90, 270) is not a
// permutation of 0, 90, 180, 270 and must make slot_layout throw
// std::runtime_error.
TEST(SlotLayout, RejectsAnOrderThatIsNotAPermutation) {
    exp::Slot slot;
    slot.default_ls_order = {0, 90, 90, 270};
    EXPECT_THROW(exp::slot_layout(slot), std::runtime_error);
}

// A layout file in the speakers format (eight loudspeakers every 45 degrees)
// is used whole: all eight are active in file order, and the slot's default
// loudspeaker order, set here to 180, 270, 0, 90, is ignored.
TEST(SlotLayout, SpeakersFileIsActiveWhole) {
    exp::Slot slot;
    slot.default_ls_order = {180, 270, 0, 90};
    slot.layout_path = write_tmp_json(
        "eight.json",
        R"({"speakers":[{"azimuth":0,"elevation":0},{"azimuth":45,"elevation":0},)"
        R"({"azimuth":90,"elevation":0},{"azimuth":135,"elevation":0},)"
        R"({"azimuth":180,"elevation":0},{"azimuth":225,"elevation":0},)"
        R"({"azimuth":270,"elevation":0},{"azimuth":315,"elevation":0}]})");
    expect_layout(exp::slot_layout(slot), {0, 45, 90, 135, 180, 225, 270, 315},
                  {1, 2, 3, 4, 5, 6, 7, 8}, {0, 45, 90, 135, 180, 225, 270, 315});
}

// A layout file in the MATLAB fixture format (a ring of eight positions with
// positions 1, 3, 5 and 7 active) keeps its active subset and its directions
// as written, which is how Iceberg MATLAB renders such a layout.
TEST(SlotLayout, FixtureFileKeepsItsActiveSubset) {
    exp::Slot slot;
    slot.layout_path = write_tmp_json(
        "ring8_active4.json",
        R"({"lsArray":[0,45,90,135,180,225,270,315],"activeLSNumbers":[1,3,5,7],)"
        R"("ls_dir":[[0,0],[90,0],[180,0],[270,0]]})");
    expect_layout(exp::slot_layout(slot), {0, 45, 90, 135, 180, 225, 270, 315},
                  {1, 3, 5, 7}, {0, 90, 180, 270});
}

// === slot_render_config ====================================================

// In full-scale mode the slot's calibration path is ignored and a neutral
// calibration sized to the layout is used: four loudspeakers for the default
// layout, with iFactor 2e-5 (0 dB per volt, so the level is a plain digital
// gain). The resulting configuration must pass validate_config.
TEST(SlotRenderConfig, FullScaleCalibrationIsNeutralOfTheLayoutSize) {
    exp::Slot slot;
    slot.use_full_scale = true;
    slot.cal_path = "/does/not/matter.json";   // ignored in full-scale mode
    const auto cfg = exp::slot_render_config(slot);
    ASSERT_EQ(cfg.calibration.loudspeakers.size(), 4u);
    EXPECT_DOUBLE_EQ(cfg.calibration.i_factor, 2e-5);
    EXPECT_FALSE(iceberg::rendering::validate_config(cfg).has_value());
}

// slot_render_config copies the slot's v1.0.0 options (DSER factor, anechoic
// special case, IR format, azimuth convention), checked at their defaults and
// at non-default values. The format and the convention are always set, so a
// render from a slot never reports them as assumed.
TEST(SlotRenderConfig, CopiesTheOptionsAndAlwaysDeclares) {
    exp::Slot slot;
    auto cfg = exp::slot_render_config(slot);
    EXPECT_EQ(cfg.dser_factor.kind, DserFactor::Kind::Unity);
    EXPECT_FALSE(cfg.anechoic_special_case);
    ASSERT_TRUE(cfg.ambisonic_format.has_value());
    ASSERT_TRUE(cfg.azimuth_convention.has_value());
    EXPECT_EQ(*cfg.ambisonic_format, AmbisonicFormat::FuMa);
    EXPECT_EQ(*cfg.azimuth_convention, AzimuthConvention::CounterClockwise);

    slot.dser_factor = DserFactor{DserFactor::Kind::Numeric, 0.25};
    slot.anechoic_special_case = true;
    slot.ambisonic_format = AmbisonicFormat::N3D;
    slot.azimuth_convention = AzimuthConvention::Clockwise;
    cfg = exp::slot_render_config(slot);
    EXPECT_EQ(cfg.dser_factor.kind, DserFactor::Kind::Numeric);
    EXPECT_DOUBLE_EQ(cfg.dser_factor.value, 0.25);
    EXPECT_TRUE(cfg.anechoic_special_case);
    EXPECT_EQ(*cfg.ambisonic_format, AmbisonicFormat::N3D);
    EXPECT_EQ(*cfg.azimuth_convention, AzimuthConvention::Clockwise);
}

// With full-scale mode off and no calibration file set, slot_render_config
// must throw std::runtime_error.
TEST(SlotRenderConfig, CalibrationRequiredWhenNotFullScale) {
    exp::Slot slot;
    slot.use_full_scale = false;
    EXPECT_THROW(exp::slot_render_config(slot), std::runtime_error);
}

// The 24-loudspeaker calibration of the reference data, given to a slot on the
// four default loudspeakers, must be rejected with std::runtime_error whose
// message names both counts ("24 loudspeakers" and "layout has 4").
TEST(SlotRenderConfig, CalibrationMustCoverTheLayout) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    exp::Slot slot;
    slot.use_full_scale = false;
    slot.cal_path = iceberg::test::fixture_file("calibration.json");
    try {
        exp::slot_render_config(slot);
        FAIL() << "expected a count mismatch";
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("24 loudspeakers"), std::string::npos) << msg;
        EXPECT_NE(msg.find("layout has 4"), std::string::npos) << msg;
    }
}

// A default slot with the four-loudspeaker calibration renders the reference
// case "four loudspeakers alone" (30 and 225 degrees, 80 dB SPL) as Iceberg
// MATLAB v1.0.0 does: on every channel the difference stays below 2e-9 RMS and
// 5e-7 of the peak (the float32 rounding floor), and no notice is raised.
TEST(SlotRenderConfig, DefaultSlotRendersAsMatlabFourAlone) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const std::string case_dir = "inputs/four_alone";
    const auto j = iceberg::test::load_fixture_json(case_dir + "/config.json");
    exp::Slot slot;
    slot.use_full_scale = false;
    slot.cal_path = iceberg::test::fixture_file(j.at("calibration").get<std::string>());
    const auto cfg = exp::slot_render_config(slot);
    const auto signal = iceberg::io::load_wav(
        iceberg::test::fixture_file(j.at("signal").get<std::string>()));
    const auto ir = iceberg::io::load_wav(iceberg::test::fixture_file(case_dir + "/input_ir.wav"));

    double worst_rms = 0.0, worst_rel = 0.0;
    for (const double angle : j.at("angles").get<std::vector<double>>()) {
        char az[16];
        std::snprintf(az, sizeof(az), "/az_%03d.wav", static_cast<int>(angle));
        const auto expected = iceberg::io::load_wav(iceberg::test::fixture_file(case_dir + az));
        std::vector<iceberg::rendering::RenderNotice> notices;
        const auto out = iceberg::rendering::iceberg(signal, ir, angle, j.at("level").get<double>(),
                                                     cfg, &notices);
        ASSERT_EQ(out.samples.rows(), expected.samples.rows()) << angle;
        ASSERT_EQ(out.samples.cols(), expected.samples.cols()) << angle;
        const auto d = iceberg::test::diff_stats(out.samples, expected.samples);
        EXPECT_LT(d.worst_rms, 2e-9) << angle;
        EXPECT_LT(d.worst_rel_peak, 5e-7) << angle;
        EXPECT_TRUE(notices.empty()) << angle << ": " << notices.front().id;
        worst_rms = std::max(worst_rms, d.worst_rms);
        worst_rel = std::max(worst_rel, d.worst_rel_peak);
    }
    iceberg::test::record_measure("inputs/four_alone worst RMS", worst_rms, 2e-9, "rms");
    iceberg::test::record_measure("inputs/four_alone worst rel peak", worst_rel, 5e-7, "rel");
}

// live_inputs_key, which tells when a slot must be prepared again for the live
// engine, is unchanged by the source angle and changes with each of six other
// inputs: the level, the loudspeaker order, the IR format, the azimuth
// convention, the calibration path and the DSER factor.
TEST(SlotLive, KeyIgnoresTheAngleOnly) {
    exp::Slot a;
    a.ir_path = "/ir.wav";
    a.signal_path = "/s.wav";
    exp::Slot b = a;
    b.source_angle_deg = 123.0;
    EXPECT_EQ(exp::live_inputs_key(a), exp::live_inputs_key(b));
    for (int field = 0; field < 6; ++field) {
        exp::Slot c = a;
        switch (field) {
            case 0: c.level_db = -19.5; break;
            case 1: c.default_ls_order = {90, 0, 180, 270}; break;
            case 2: c.ambisonic_format = AmbisonicFormat::AmbiX; break;
            case 3: c.azimuth_convention = AzimuthConvention::Clockwise; break;
            case 4: c.cal_path = "/cal.json"; break;
            case 5: c.dser_factor = DserFactor{DserFactor::Kind::Numeric, 0.7}; break;
        }
        EXPECT_NE(exp::live_inputs_key(a), exp::live_inputs_key(c)) << field;
    }
}

// prepare_slot_live on a slot that names reference files (signal, IR and a
// calibration for four loudspeakers, at 80 dB SPL) must equal prepare_live run
// directly on the same files with slot_render_config: the same length and
// bit-identical direct streams and per-loudspeaker dry streams.
TEST(SlotLive, PreparesFromTheSlotFiles) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    exp::Slot slot;
    slot.use_full_scale = false;
    slot.cal_path = iceberg::test::fixture_file("inputs/four_alone/calibration_4ls.json");
    slot.signal_path = iceberg::test::fixture_file("signal.wav");
    slot.ir_path = iceberg::test::fixture_file("sweep/rt_05/input_ir.wav");
    slot.level_db = 80.0;
    const auto live = exp::prepare_slot_live(slot);
    const auto direct = iceberg::rendering::prepare_live(
        iceberg::io::load_wav(slot.signal_path), iceberg::io::load_wav(slot.ir_path), 80.0,
        exp::slot_render_config(slot));
    EXPECT_EQ(live->length, direct.length);
    EXPECT_TRUE(live->direct == direct.direct);
    EXPECT_TRUE(live->late_dry == direct.late_dry);
}

// The overload of prepare_slot_live that takes a signal already in memory gives
// the same live render as the one that reads the signal file (same length,
// bit-identical direct and dry streams), and does not open the slot's signal
// path, which here names a file that does not exist.
TEST(SlotLive, TakesASignalAlreadyRead) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    exp::Slot slot;
    slot.signal_path = iceberg::test::fixture_file("signal.wav");
    slot.ir_path = iceberg::test::fixture_file("sweep/rt_05/input_ir.wav");
    slot.level_db = 80.0;
    const auto from_path = exp::prepare_slot_live(slot);
    const iceberg::Signal signal = iceberg::io::load_wav(slot.signal_path);
    slot.signal_path = "/not/opened.flac";
    const auto from_signal = exp::prepare_slot_live(slot, signal);
    EXPECT_EQ(from_signal->length, from_path->length);
    EXPECT_TRUE(from_signal->direct == from_path->direct);
    EXPECT_TRUE(from_signal->late_dry == from_path->late_dry);
}

}  // namespace
