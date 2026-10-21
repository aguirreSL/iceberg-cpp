// Tests for io/calibration_loader: reading a loudspeaker calibration JSON file
// (iFactor, one level factor and one frequency response per loudspeaker), the
// rejection of malformed files, and rendering with the example calibrations.
#include "io/calibration_loader.h"
#include "io/layout_loader.h"
#include "rendering/iceberg.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::filesystem::path tmp_json(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() /
             ("iceberg_calib_test_" + name + ".json");
    return p;
}

void write_file(const std::filesystem::path& p, const std::string& body) {
    std::ofstream out(p);
    out << body;
}

// The 24-loudspeaker calibration file of the reference data loads with a
// finite, positive iFactor, 24 level factors and 24 loudspeaker entries; each
// entry has an index in 1..24 and a non-empty frequency vector of the same
// length as its magnitude vector. A short summary of the values is printed.
TEST(CalibrationLoader, HappyPathFixture) {
    ICEBERG_SKIP_IF_NO_FIXTURES();
    const std::string path =
        std::string(ICEBERG_FIXTURES_DIR) + "/calibration.json";
    const auto cal = iceberg::io::load_calibration(path);

    EXPECT_TRUE(std::isfinite(cal.i_factor));
    EXPECT_GT(cal.i_factor, 0.0);
    ASSERT_EQ(cal.new_level_factor.size(), 24);
    ASSERT_EQ(cal.loudspeakers.size(), 24u);

    for (const auto& ls : cal.loudspeakers) {
        EXPECT_GE(ls.index, 1);
        EXPECT_LE(ls.index, 24);
        EXPECT_EQ(ls.freq_vector.size(), ls.freq_magnitude.size());
        EXPECT_GT(ls.freq_vector.size(), 0);
    }

    // Print a small shape summary.
    std::cout << "[Calibration] iFactor = " << cal.i_factor
              << "\n[Calibration] newLevelFactor[0..2] = "
              << cal.new_level_factor(0) << ", "
              << cal.new_level_factor(1) << ", "
              << cal.new_level_factor(2)
              << "\n[Calibration] LS index=" << cal.loudspeakers[0].index
              << " freqVector[0..2] = "
              << cal.loudspeakers[0].freq_vector(0) << ", "
              << cal.loudspeakers[0].freq_vector(1) << ", "
              << cal.loudspeakers[0].freq_vector(2)
              << " (size " << cal.loudspeakers[0].freq_vector.size() << ")"
              << std::endl;
}

// The four-loudspeaker example calibration with its iFactor field removed, and
// otherwise valid, must be rejected with std::runtime_error, and the message
// must name the missing field.
TEST(CalibrationLoader, ThrowsOnMissingIFactor) {
    std::ifstream in(std::string(ICEBERG_EXAMPLES_DIR) + "/calibration/four_loudspeakers.json");
    nlohmann::json j = nlohmann::json::parse(in);
    j.erase("iFactor");
    const auto p = tmp_json("missing_ifactor");
    write_file(p, j.dump());
    try {
        iceberg::io::load_calibration(p.string());
        ADD_FAILURE() << "a calibration without iFactor was accepted";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("iFactor"), std::string::npos) << e.what();
    }
    std::filesystem::remove(p);
}

// A calibration file with 23 level factors and an empty loudspeaker list must
// be rejected with std::runtime_error, since the loader requires one level
// factor per loudspeaker entry.
TEST(CalibrationLoader, ThrowsOnWrongNewLevelFactorSize) {
    const auto p = tmp_json("wrong_nlf_size");
    // 23 level factors and no loudspeaker entry: the two counts differ.
    std::string body = R"({"iFactor": 0.5, "newLevelFactor": [)";
    for (int i = 0; i < 23; ++i) body += (i ? "," : "") + std::string("1.0");
    body += R"(], "loudspeakers": []})";
    write_file(p, body);
    EXPECT_THROW(iceberg::io::load_calibration(p.string()),
                 std::runtime_error);
    std::filesystem::remove(p);
}

// A calibration file with 24 level factors and 23 loudspeaker entries must be
// rejected with std::runtime_error, since the loader requires one level factor
// per loudspeaker entry.
TEST(CalibrationLoader, ThrowsOnLoudspeakersWrongCount) {
    // 24 level factors and 23 loudspeaker entries: the same count check, reached
    // with entries present.
    const auto p = tmp_json("ls_wrong_count");
    std::string body = R"({"iFactor": 0.5, "newLevelFactor": [)";
    for (int i = 0; i < 24; ++i) body += (i ? "," : "") + std::string("1.0");
    body += R"(], "loudspeakers": [)";
    for (int i = 0; i < 23; ++i) {
        if (i) body += ",";
        body += R"({"index": )" + std::to_string(i + 1) +
                R"(, "freqVector": [50, 62.5, 80])"
                R"(, "freqMagnitude": [1.0, 1.0, 1.0]})";
    }
    body += "]}";
    write_file(p, body);
    EXPECT_THROW(iceberg::io::load_calibration(p.string()),
                 std::runtime_error);
    std::filesystem::remove(p);
}

// A hand-written calibration for four loudspeakers, the minimum the renderer
// supports, loads with four level factors and four loudspeaker entries.
TEST(CalibrationLoader, AcceptsMinimumFourLs) {
    // 4-LS synthetic JSON — algorithm minimum. Verifies the
    // loader accepts layouts other than the 24-loudspeaker one.
    const auto p = tmp_json("four_ls");
    std::string body = R"({"iFactor": 0.5, "newLevelFactor": [)";
    for (int i = 0; i < 4; ++i) body += (i ? "," : "") + std::string("1.0");
    body += R"(], "loudspeakers": [)";
    for (int i = 0; i < 4; ++i) {
        if (i) body += ",";
        body += R"({"index": )" + std::to_string(i + 1) +
                R"(, "freqVector": [50, 100, 200])"
                R"(, "freqMagnitude": [1.0, 1.0, 1.0]})";
    }
    body += "]}";
    write_file(p, body);

    const auto cal = iceberg::io::load_calibration(p.string());
    EXPECT_EQ(cal.new_level_factor.size(), 4);
    EXPECT_EQ(cal.loudspeakers.size(), 4u);
    std::filesystem::remove(p);
}

// A hand-written calibration for three loudspeakers, one fewer than the
// minimum of four, must be rejected with std::runtime_error.
TEST(CalibrationLoader, ThrowsOnBelowMinimumThreeLs) {
    // 3-LS — below algorithm minimum; loader must reject.
    const auto p = tmp_json("three_ls");
    std::string body = R"({"iFactor": 0.5, "newLevelFactor": [)";
    for (int i = 0; i < 3; ++i) body += (i ? "," : "") + std::string("1.0");
    body += R"(], "loudspeakers": [)";
    for (int i = 0; i < 3; ++i) {
        if (i) body += ",";
        body += R"({"index": )" + std::to_string(i + 1) +
                R"(, "freqVector": [50, 100, 200])"
                R"(, "freqMagnitude": [1.0, 1.0, 1.0]})";
    }
    body += "]}";
    write_file(p, body);

    EXPECT_THROW(iceberg::io::load_calibration(p.string()),
                 std::runtime_error);
    std::filesystem::remove(p);
}

// A 24-loudspeaker calibration in which the first loudspeaker has 27
// frequencies and 26 magnitudes must be rejected with std::runtime_error,
// since each frequency vector must have the length of its magnitude vector.
TEST(CalibrationLoader, ThrowsOnAsymmetricLoudspeakerSizes) {
    const auto p = tmp_json("asymmetric_ls");
    // Build a JSON with 24 newLevelFactor entries and 24 loudspeakers,
    // but loudspeaker[0] has freqVector(27) and freqMagnitude(26).
    std::string body = R"({"iFactor": 0.5, "newLevelFactor": [)";
    for (int i = 0; i < 24; ++i) body += (i ? "," : "") + std::string("1.0");
    body += R"(], "loudspeakers": [)";
    for (int i = 0; i < 24; ++i) {
        if (i) body += ",";
        body += R"({"index": )" + std::to_string(i + 1) +
                R"(, "freqVector": [)";
        const int n_freq = 27;
        for (int k = 0; k < n_freq; ++k)
            body += (k ? "," : "") + std::to_string(k * 100);
        body += R"(], "freqMagnitude": [)";
        const int n_mag = (i == 0) ? 26 : 27;  // ← LS 0 mismatch
        for (int k = 0; k < n_mag; ++k)
            body += (k ? "," : "") + std::string("1.0");
        body += "]}";
    }
    body += "]}";
    write_file(p, body);
    EXPECT_THROW(iceberg::io::load_calibration(p.string()),
                 std::runtime_error);
    std::filesystem::remove(p);
}


// The calibrations in examples/calibration load with their loudspeaker counts
// (ring24.json: 24, with layouts/ring24_4active.json; four_loudspeakers.json:
// 4, at 0, 90, 180, 270 degrees on outputs 1 to 4), and each renders a sine at
// 30 degrees and 80 dB SPL to a non-silent output of one channel per position.
TEST(CalibrationLoader, ExampleCalibrationsLoadAndRender) {
    const std::string dir = std::string(ICEBERG_EXAMPLES_DIR);
    iceberg::Signal signal;
    signal.samples = Eigen::MatrixXf(1024, 1);
    for (Eigen::Index i = 0; i < 1024; ++i) {
        signal.samples(i, 0) = static_cast<float>(std::sin(0.05 * static_cast<double>(i)));
    }
    signal.sample_rate = 44100.0;
    iceberg::Signal ir;
    ir.samples = Eigen::MatrixXf::Zero(4096, 4);
    ir.samples(100, 0) = 1.0f;
    ir.samples(100, 1) = 0.5f;
    ir.sample_rate = 44100.0;

    auto ring = iceberg::io::load_layout_json(dir + "/layouts/ring24_4active.json");
    ring.calibration = iceberg::io::load_calibration(dir + "/calibration/ring24.json");
    ASSERT_EQ(ring.calibration.loudspeakers.size(), 24u);
    const auto out_ring = iceberg::rendering::iceberg(signal, ir, 30.0, 80.0, ring);
    EXPECT_EQ(out_ring.samples.cols(), 24);
    EXPECT_GT(out_ring.samples.cwiseAbs().maxCoeff(), 0.0f);

    iceberg::rendering::ConfigSetup four;
    four.ls_array.resize(4);
    four.ls_array << 0, 90, 180, 270;
    four.active_ls_numbers.resize(4);
    four.active_ls_numbers << 1, 2, 3, 4;
    four.ls_dir.resize(4, 2);
    four.ls_dir << 0, 0, 90, 0, 180, 0, 270, 0;
    four.calibration = iceberg::io::load_calibration(dir + "/calibration/four_loudspeakers.json");
    ASSERT_EQ(four.calibration.loudspeakers.size(), 4u);
    const auto out_four = iceberg::rendering::iceberg(signal, ir, 30.0, 80.0, four);
    EXPECT_EQ(out_four.samples.cols(), 4);
    EXPECT_GT(out_four.samples.cwiseAbs().maxCoeff(), 0.0f);
}

}  // namespace
