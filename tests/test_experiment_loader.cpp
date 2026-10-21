// Tests for the .iceberg-exp experiment file (experiment/experiment_loader):
// save and load round trips of an experiment and its slots, the defaults given
// to files of an older schema version, and the rejection of missing, malformed
// or out-of-range content.
#include "experiment/experiment_loader.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace exp = iceberg::experiment;

// Per-test temp file path. Use the test's full name to avoid collisions
// when ctest parallelizes.
std::string tmp_path(const ::testing::TestInfo* info,
                     const std::string& suffix = ".iceberg-exp") {
    const auto dir = std::filesystem::temp_directory_path() /
                     "iceberg_exp_tests";
    std::filesystem::create_directories(dir);
    const std::string fname = std::string(info->test_suite_name()) + "_" +
                              info->name() + suffix;
    return (dir / fname).string();
}

class ExperimentLoaderTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Best-effort cleanup; don't fail the test if it's already gone.
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(path_ + ".tmp", ec);
    }
    std::string path_ = tmp_path(
        ::testing::UnitTest::GetInstance()->current_test_info());
};

exp::Slot make_slot(const std::string& name, double angle, double level,
                    bool with_cal = true) {
    exp::Slot s;
    s.name = name;
    s.ir_path = "/abs/ir/" + name + ".wav";
    s.layout_path = "/abs/layout/" + name + ".json";
    s.signal_path = "/abs/sig/" + name + ".wav";
    if (with_cal) s.cal_path = "/abs/cal/" + name + ".json";
    s.source_angle_deg = angle;
    s.level_db = level;
    return s;
}

void expect_slots_persist_eq(const exp::Slot& a, const exp::Slot& b) {
    EXPECT_EQ(a.name,             b.name);
    EXPECT_EQ(a.ir_path,          b.ir_path);
    EXPECT_EQ(a.layout_path,      b.layout_path);
    EXPECT_EQ(a.cal_path,         b.cal_path);
    EXPECT_EQ(a.signal_path,      b.signal_path);
    EXPECT_DOUBLE_EQ(a.source_angle_deg, b.source_angle_deg);
    EXPECT_DOUBLE_EQ(a.level_db,         b.level_db);
    EXPECT_EQ(a.cached_metrics.has_value(),
              b.cached_metrics.has_value());
    if (a.cached_metrics && b.cached_metrics) {
        EXPECT_DOUBLE_EQ(a.cached_metrics->peak_abs,
                         b.cached_metrics->peak_abs);
        EXPECT_DOUBLE_EQ(a.cached_metrics->rms,
                         b.cached_metrics->rms);
        EXPECT_EQ(a.cached_metrics->n_samples,
                  b.cached_metrics->n_samples);
        EXPECT_DOUBLE_EQ(a.cached_metrics->c_time,
                         b.cached_metrics->c_time);
    }
}

}  // namespace

// An experiment with a name and no slots is saved and loaded back. The name,
// the empty slot list, the current schema version, the creation and
// modification timestamps that saving fills in, and the default play options
// (wait between slots, 0 s of wait) are expected after the load.
TEST_F(ExperimentLoaderTest, RoundTripEmpty) {
    exp::Experiment original;
    original.name = "Empty";
    exp::save_experiment(path_, original);

    const auto loaded = exp::load_experiment(path_);
    EXPECT_EQ(loaded.name, "Empty");
    EXPECT_TRUE(loaded.slots.empty());
    EXPECT_EQ(loaded.version, exp::kExperimentVersion);
    // save_experiment populates timestamps automatically.
    EXPECT_FALSE(loaded.created_iso8601.empty());
    EXPECT_FALSE(loaded.modified_iso8601.empty());
    EXPECT_EQ(loaded.play_options.wait_between_slots, true);
    EXPECT_DOUBLE_EQ(loaded.play_options.wait_seconds, 0.0);
}

// Three slots with distinct names, file paths, angles and levels, and
// non-default play options, are saved and loaded back. Every saved slot field
// and both play options must match the original, and the runtime fields
// (rendered, stale, output buffer, error message) must load in their reset state.
TEST_F(ExperimentLoaderTest, RoundTripWithSlots) {
    exp::Experiment original;
    original.name = "With Slots";
    original.slots.push_back(make_slot("Stim 1", 0.0,   80.0));
    original.slots.push_back(make_slot("Stim 2", 45.5,  72.5));
    original.slots.push_back(make_slot("Stim 3", 270.0, 95.0));
    original.play_options.wait_between_slots = false;
    original.play_options.wait_seconds = 1.5;

    exp::save_experiment(path_, original);
    const auto loaded = exp::load_experiment(path_);

    ASSERT_EQ(loaded.slots.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        expect_slots_persist_eq(original.slots[i], loaded.slots[i]);
        // Runtime fields must come back zeroed.
        EXPECT_FALSE(loaded.slots[i].is_rendered);
        EXPECT_TRUE (loaded.slots[i].is_stale);
        EXPECT_EQ(loaded.slots[i].output_buffer, nullptr);
        EXPECT_FALSE(loaded.slots[i].error_message.has_value());
    }
    EXPECT_FALSE(loaded.play_options.wait_between_slots);
    EXPECT_DOUBLE_EQ(loaded.play_options.wait_seconds, 1.5);
}

// A slot carrying cached render metrics (peak, RMS, sample count, centre time)
// is saved and loaded back; the metrics must be present after the load and
// match the values written, as must every other saved slot field.
TEST_F(ExperimentLoaderTest, RoundTripWithCachedMetrics) {
    exp::Experiment original;
    auto s = make_slot("With Metrics", 90.0, 80.0);
    s.cached_metrics = exp::Slot::CachedMetrics{
        /*peak_abs=*/0.812345,
        /*rms=*/0.123456,
        /*n_samples=*/132300,
        /*c_time=*/0.0524,
    };
    original.slots.push_back(s);

    exp::save_experiment(path_, original);
    const auto loaded = exp::load_experiment(path_);

    ASSERT_EQ(loaded.slots.size(), 1u);
    ASSERT_TRUE(loaded.slots[0].cached_metrics.has_value());
    expect_slots_persist_eq(original.slots[0], loaded.slots[0]);
}

// The calibration path of a slot is optional. Of two saved slots, the one with
// a calibration file must load with its path set, and the one without (written
// as JSON null) must load with the path unset.
TEST_F(ExperimentLoaderTest, OptionalCalPathPreservesNullopt) {
    exp::Experiment original;
    original.slots.push_back(make_slot("HasCal", 0.0, 80.0, /*with_cal=*/true));
    original.slots.push_back(make_slot("Neutral", 0.0, 80.0, /*with_cal=*/false));

    exp::save_experiment(path_, original);
    const auto loaded = exp::load_experiment(path_);

    ASSERT_EQ(loaded.slots.size(), 2u);
    EXPECT_TRUE(loaded.slots[0].cal_path.has_value());
    EXPECT_FALSE(loaded.slots[1].cal_path.has_value());
}

// Loading a path that does not exist must throw std::runtime_error.
TEST_F(ExperimentLoaderTest, ThrowsOnMissingFile) {
    EXPECT_THROW(
        exp::load_experiment("/nonexistent/path/no.iceberg-exp"),
        std::runtime_error);
}

// A file whose content is not valid JSON must make the loader throw
// std::runtime_error.
TEST_F(ExperimentLoaderTest, ThrowsOnMalformedJson) {
    {
        std::ofstream out(path_);
        out << "{ this is : not valid json , slots [ ";
    }
    EXPECT_THROW(exp::load_experiment(path_), std::runtime_error);
}

// A file of valid JSON that lacks the required "slots" field must make the
// loader throw std::runtime_error.
TEST_F(ExperimentLoaderTest, ThrowsOnMissingRequiredField) {
    {
        std::ofstream out(path_);
        // Valid JSON but missing 'slots'.
        out << R"({"version":"1.0","name":"x","play_options":)"
            << R"({"wait_between_slots":true,"wait_seconds":0}})";
    }
    EXPECT_THROW(exp::load_experiment(path_), std::runtime_error);
}

// A file that declares schema version 2.0.0 must be rejected with
// std::runtime_error, since the loader reads only files of major version 1.
TEST_F(ExperimentLoaderTest, ThrowsOnIncompatibleVersionMajor) {
    {
        std::ofstream out(path_);
        out << R"({"version":"2.0.0","name":"x","slots":[],"play_options":)"
            << R"({"wait_between_slots":true,"wait_seconds":0}})";
    }
    EXPECT_THROW(exp::load_experiment(path_), std::runtime_error);
}

// A saved file is edited on disk so that the slot's source angle reads 400
// degrees. Loading it must throw std::runtime_error, since the loader accepts
// source angles in [0, 360) only.
TEST_F(ExperimentLoaderTest, ThrowsOnOutOfRangeAngle) {
    exp::Experiment original;
    auto s = make_slot("BadAngle", 0.0, 80.0);
    original.slots.push_back(s);
    exp::save_experiment(path_, original);

    // Mutate the angle on disk to an out-of-range value, then re-load.
    {
        std::ifstream in(path_);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        const auto pos = content.find("\"source_angle_deg\": 0.0");
        ASSERT_NE(pos, std::string::npos);
        content.replace(pos, 26, "\"source_angle_deg\": 400.0");
        std::ofstream out(path_);
        out << content;
    }
    EXPECT_THROW(exp::load_experiment(path_), std::runtime_error);
}

// Saving writes a temporary ".tmp" file and renames it onto the target. After a
// successful save the target must exist, the temporary file must be gone, and
// the target must load back with its one slot. An interrupted write is not
// simulated.
TEST_F(ExperimentLoaderTest, AtomicSaveLeavesNoTempOnSuccess) {
    // A programmatic mid-write crash is OS-level and harder
    // to simulate portably; we instead verify the success-path side
    // effect: temp file is gone after save.
    exp::Experiment original;
    original.name = "Atomic";
    original.slots.push_back(make_slot("Stim 1", 0.0, 80.0));

    exp::save_experiment(path_, original);

    EXPECT_TRUE(std::filesystem::exists(path_));
    EXPECT_FALSE(std::filesystem::exists(path_ + ".tmp"))
        << "Temp file should be renamed/removed on success.";

    // Sanity: file is round-trippable.
    const auto loaded = exp::load_experiment(path_);
    EXPECT_EQ(loaded.slots.size(), 1u);
}

// Saving fills in the creation and modification timestamps. The modification
// timestamp is then set in memory to a date in 1999 and the experiment is
// saved again; the second save must overwrite that value.
TEST_F(ExperimentLoaderTest, SaveUpdatesModifiedTimestamp) {
    exp::Experiment original;
    original.name = "Stamps";
    exp::save_experiment(path_, original);
    const std::string first_modified = original.modified_iso8601;
    EXPECT_FALSE(first_modified.empty());
    EXPECT_FALSE(original.created_iso8601.empty());

    // Re-save and confirm `modified` was refreshed (or stayed equal at
    // 1-second resolution). Simulate elapsed time by mutating the
    // member; save_experiment must overwrite it.
    original.modified_iso8601 = "1999-01-01T00:00:00Z";
    exp::save_experiment(path_, original);
    EXPECT_NE(original.modified_iso8601, "1999-01-01T00:00:00Z");
}

// === Schema 1.3: default loudspeaker order and the v1.0.0 options ==========

// The per-slot options of schema 1.3 survive a save and load. One slot has an
// empty layout path, the loudspeaker order 180, 270, 0, 90, a numeric DSER
// factor of 0.5, AmbiX, the clockwise convention and the anechoic special case;
// the other has the "thesis" DSER factor, N3D and the default convention.
TEST_F(ExperimentLoaderTest, RoundTripV13Options) {
    using iceberg::rendering::AmbisonicFormat;
    using iceberg::rendering::AzimuthConvention;
    using iceberg::rendering::DserFactor;
    exp::Experiment original;
    auto a = make_slot("Defaults off", 30.0, 80.0);
    a.layout_path.clear();
    a.default_ls_order = {180, 270, 0, 90};
    a.dser_factor = DserFactor{DserFactor::Kind::Numeric, 0.5};
    a.ambisonic_format = AmbisonicFormat::AmbiX;
    a.azimuth_convention = AzimuthConvention::Clockwise;
    a.anechoic_special_case = true;
    auto b = make_slot("Thesis", 30.0, 80.0);
    b.dser_factor.kind = DserFactor::Kind::Thesis;
    b.ambisonic_format = AmbisonicFormat::N3D;
    original.slots = {a, b};

    exp::save_experiment(path_, original);
    const auto loaded = exp::load_experiment(path_);
    ASSERT_EQ(loaded.slots.size(), 2u);
    const auto& la = loaded.slots[0];
    EXPECT_TRUE(la.layout_path.empty());
    EXPECT_EQ(la.default_ls_order, (std::array<int, 4>{180, 270, 0, 90}));
    EXPECT_EQ(la.dser_factor.kind, DserFactor::Kind::Numeric);
    EXPECT_DOUBLE_EQ(la.dser_factor.value, 0.5);
    EXPECT_EQ(la.ambisonic_format, AmbisonicFormat::AmbiX);
    EXPECT_EQ(la.azimuth_convention, AzimuthConvention::Clockwise);
    EXPECT_TRUE(la.anechoic_special_case);
    EXPECT_EQ(loaded.slots[1].dser_factor.kind, DserFactor::Kind::Thesis);
    EXPECT_EQ(loaded.slots[1].ambisonic_format, AmbisonicFormat::N3D);
    EXPECT_EQ(loaded.slots[1].azimuth_convention, AzimuthConvention::CounterClockwise);
}

// A hand-written schema 1.2 file, which has none of the 1.3 and 1.4 fields,
// loads with Iceberg v1.0.0's defaults (order 0, 90, 180, 270, unity DSER
// factor, FuMa, counter-clockwise, no anechoic special case) and no trajectory.
// Saving it writes the current schema version, which is also read back.
TEST_F(ExperimentLoaderTest, Version12LoadsWithDefaultsAndSavesAsCurrent) {
    {
        std::ofstream out(path_);
        out << R"({"version":"1.2","name":"old","play_options":)"
            << R"({"wait_between_slots":true,"wait_seconds":0},"slots":[)"
            << R"({"name":"s","ir_path":"/ir.wav","layout_path":"/l.json",)"
            << R"("cal_path":null,"signal_path":"/s.wav","source_angle_deg":30,)"
            << R"("level_db":-20,"use_full_scale":true}]})";
    }
    auto loaded = exp::load_experiment(path_);
    EXPECT_EQ(loaded.version, "1.2");
    ASSERT_EQ(loaded.slots.size(), 1u);
    const auto& s = loaded.slots[0];
    EXPECT_EQ(s.default_ls_order, (std::array<int, 4>{0, 90, 180, 270}));
    EXPECT_EQ(s.dser_factor.kind, iceberg::rendering::DserFactor::Kind::Unity);
    EXPECT_EQ(s.ambisonic_format, iceberg::rendering::AmbisonicFormat::FuMa);
    EXPECT_EQ(s.azimuth_convention, iceberg::rendering::AzimuthConvention::CounterClockwise);
    EXPECT_FALSE(s.anechoic_special_case);
    EXPECT_TRUE(s.trajectory.empty());

    exp::save_experiment(path_, loaded);
    EXPECT_EQ(loaded.version, exp::kExperimentVersion);
    EXPECT_EQ(exp::load_experiment(path_).version, exp::kExperimentVersion);
}

// === Schema 1.4: the programmed path =======================================

// A slot's programmed trajectory of four (time, angle) points is saved and
// loaded back: the point count and the last point (5.5 s, -90.25 degrees) must
// be preserved, and a second slot without a trajectory must load with none.
TEST_F(ExperimentLoaderTest, RoundTripTrajectory) {
    exp::Experiment original;
    auto s = make_slot("Moving", 0.0, 80.0);
    s.trajectory = {{0.0, 0.0}, {2.0, 90.0}, {3.0, 90.0}, {5.5, -90.25}};
    original.slots = {s, make_slot("Still", 30.0, 80.0)};
    exp::save_experiment(path_, original);
    const auto loaded = exp::load_experiment(path_);
    ASSERT_EQ(loaded.slots[0].trajectory.size(), 4u);
    EXPECT_DOUBLE_EQ(loaded.slots[0].trajectory[3].time_s, 5.5);
    EXPECT_DOUBLE_EQ(loaded.slots[0].trajectory[3].angle_deg, -90.25);
    EXPECT_TRUE(loaded.slots[1].trajectory.empty());
}

// Four malformed trajectories in a schema 1.4 file must each be rejected with
// std::runtime_error: a repeated time (0 s twice), a point of three numbers, a
// negative time, and a string in place of the array of points.
TEST_F(ExperimentLoaderTest, ThrowsOnMalformedTrajectory) {
    for (const std::string traj : {R"("trajectory":[[0,0],[0,90]])", R"("trajectory":[[1,0,2]])",
                                   R"("trajectory":[[-1,0]])", R"("trajectory":"0:0")"}) {
        {
            std::ofstream out(path_);
            out << R"({"version":"1.4","name":"x","play_options":)"
                << R"({"wait_between_slots":true,"wait_seconds":0},"slots":[)"
                << R"({"name":"s","ir_path":"","layout_path":"","cal_path":null,)"
                << R"("signal_path":"","source_angle_deg":0,"level_db":-20,)" << traj << "}]}";
        }
        EXPECT_THROW(exp::load_experiment(path_), std::runtime_error) << traj;
    }
}

// Seven invalid values of the schema 1.3 slot options must each be rejected
// with std::runtime_error: loudspeaker orders that repeat an angle, have three
// entries or contain 45 degrees, the DSER factors "max" and -1, the format
// "bformat" and the convention "cw".
TEST_F(ExperimentLoaderTest, ThrowsOnMalformedV13Option) {
    const auto write_slot_with = [this](const std::string& field) {
        std::ofstream out(path_);
        out << R"({"version":"1.3","name":"x","play_options":)"
            << R"({"wait_between_slots":true,"wait_seconds":0},"slots":[)"
            << R"({"name":"s","ir_path":"","layout_path":"","cal_path":null,)"
            << R"("signal_path":"","source_angle_deg":0,"level_db":-20,)" << field << "}]}";
    };
    for (const std::string field :
         {R"("default_ls_order":[0,0,180,270])", R"("default_ls_order":[0,90,180])",
          R"("default_ls_order":[0,45,180,270])", R"("dser_factor":"max")",
          R"("dser_factor":-1)", R"("ambisonic_format":"bformat")",
          R"("azimuth_convention":"cw")"}) {
        write_slot_with(field);
        EXPECT_THROW(exp::load_experiment(path_), std::runtime_error) << field;
    }
}

// is_default_ls_permutation accepts any ordering of 0, 90, 180 and 270 degrees
// and rejects an order with a repeated angle or one that contains 360.
TEST(ExperimentLoader, DefaultLsPermutation) {
    EXPECT_TRUE(exp::is_default_ls_permutation({0, 90, 180, 270}));
    EXPECT_TRUE(exp::is_default_ls_permutation({270, 0, 180, 90}));
    EXPECT_FALSE(exp::is_default_ls_permutation({0, 90, 90, 270}));
    EXPECT_FALSE(exp::is_default_ls_permutation({0, 90, 180, 360}));
}
