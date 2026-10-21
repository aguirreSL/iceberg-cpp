#include "io/calibration_loader.h"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace iceberg::io {

namespace {

// Minimum LS count enforced by load_calibration. Algorithm constraints:
// VBAP 2D + Ambisonics order 1 require ≥ 4 LS for stable behaviour.
// No hard maximum.
constexpr int kMinLoudspeakerCount = 4;

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw std::runtime_error("load_calibration(" + path + "): " + msg);
}

void require(bool cond, const std::string& path, const std::string& msg) {
    if (!cond) fail(path, msg);
}

Eigen::VectorXd to_vector_xd(const nlohmann::json& arr) {
    Eigen::VectorXd v(static_cast<Eigen::Index>(arr.size()));
    for (size_t i = 0; i < arr.size(); ++i) {
        v(static_cast<Eigen::Index>(i)) = arr[i].get<double>();
    }
    return v;
}

}  // namespace

CalibrationData load_calibration(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        fail(path, "cannot open file");
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error& e) {
        fail(path, std::string("JSON parse error: ") + e.what());
    }

    require(j.contains("iFactor"),         path, "missing field 'iFactor'");
    require(j.contains("newLevelFactor"),  path, "missing field 'newLevelFactor'");
    require(j.contains("loudspeakers"),    path, "missing field 'loudspeakers'");

    CalibrationData out;
    out.i_factor = j.at("iFactor").get<double>();

    const auto& nlf = j.at("newLevelFactor");
    require(nlf.is_array(), path, "'newLevelFactor' must be an array");
    const int n_levels = static_cast<int>(nlf.size());

    const auto& ls = j.at("loudspeakers");
    require(ls.is_array(), path, "'loudspeakers' must be an array");
    const int n_ls = static_cast<int>(ls.size());

    require(n_levels == n_ls, path,
            "'newLevelFactor' size " + std::to_string(n_levels) +
            " != 'loudspeakers' size " + std::to_string(n_ls));
    require(n_ls >= kMinLoudspeakerCount, path,
            "'loudspeakers' size " + std::to_string(n_ls) +
            " is below algorithm minimum " +
            std::to_string(kMinLoudspeakerCount));

    out.new_level_factor = to_vector_xd(nlf);
    out.loudspeakers.reserve(static_cast<size_t>(n_ls));
    for (size_t i = 0; i < ls.size(); ++i) {
        const auto& obj = ls[i];
        require(obj.contains("index"), path,
                "loudspeaker[" + std::to_string(i) + "] missing 'index'");
        require(obj.contains("freqVector"), path,
                "loudspeaker[" + std::to_string(i) + "] missing 'freqVector'");
        require(obj.contains("freqMagnitude"), path,
                "loudspeaker[" + std::to_string(i) + "] missing 'freqMagnitude'");

        CalibrationData::LoudspeakerFilter f;
        f.index = obj.at("index").get<int>();
        f.freq_vector = to_vector_xd(obj.at("freqVector"));
        f.freq_magnitude = to_vector_xd(obj.at("freqMagnitude"));

        require(f.freq_vector.size() == f.freq_magnitude.size(), path,
                "loudspeaker[" + std::to_string(i) + "] freqVector size " +
                std::to_string(f.freq_vector.size()) +
                " != freqMagnitude size " +
                std::to_string(f.freq_magnitude.size()));

        out.loudspeakers.push_back(std::move(f));
    }

    return out;
}

void save_calibration(const std::string& json_path, const CalibrationData& cal) {
    const size_t n = cal.loudspeakers.size();
    if (n < static_cast<size_t>(kMinLoudspeakerCount)) {
        throw std::invalid_argument("save_calibration: fewer than " +
                                    std::to_string(kMinLoudspeakerCount) + " loudspeakers");
    }
    if (static_cast<size_t>(cal.new_level_factor.size()) != n) {
        throw std::invalid_argument("save_calibration: one level factor per loudspeaker");
    }
    const auto to_array = [](const Eigen::VectorXd& v) {
        return std::vector<double>(v.data(), v.data() + v.size());
    };
    nlohmann::json loudspeakers = nlohmann::json::array();
    for (const auto& ls : cal.loudspeakers) {
        if (ls.freq_vector.size() != ls.freq_magnitude.size()) {
            throw std::invalid_argument("save_calibration: loudspeaker " +
                                        std::to_string(ls.index) +
                                        ": freqVector and freqMagnitude differ in length");
        }
        loudspeakers.push_back({{"index", ls.index},
                                {"freqVector", to_array(ls.freq_vector)},
                                {"freqMagnitude", to_array(ls.freq_magnitude)}});
    }
    const nlohmann::json j = {{"iFactor", cal.i_factor},
                              {"newLevelFactor", to_array(cal.new_level_factor)},
                              {"loudspeakers", loudspeakers}};

    const std::string tmp = json_path + ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) throw std::runtime_error("save_calibration: cannot write " + tmp);
        out << j.dump() << "\n";
        if (!out) throw std::runtime_error("save_calibration: write to " + tmp + " failed");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, json_path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("save_calibration: cannot rename into " + json_path);
    }
}

CalibrationData neutral_calibration(int n_ls) {
    if (n_ls < kMinLoudspeakerCount) {
        throw std::invalid_argument(
            "neutral_calibration: n_ls (" + std::to_string(n_ls) +
            ") is below algorithm minimum " +
            std::to_string(kMinLoudspeakerCount));
    }

    // Standard 1/3-octave-ish centers from the 24-loudspeaker calibration
    // (matches `calibration.json::loudspeakers[].freqVector`). Note
    // band 6 is 155 Hz, not the ISO standard 160 — kept for parity
    // with the fixture data so that pchip on this grid produces the
    // same intermediate node positions.
    constexpr std::array<double, 27> kFreqs = {
        50,    62.5,  80,    100,   125,   155,   200,
        250,   315,   400,   500,   630,   800,   1000,
        1250,  1600,  2000,  2500,  3150,  4000,  5000,
        6350,  8000,  10000, 12500, 16000, 20000,
    };

    CalibrationData c;
    c.i_factor = 2e-5;
    c.new_level_factor = Eigen::VectorXd::Ones(n_ls);
    c.loudspeakers.reserve(static_cast<size_t>(n_ls));
    for (int i = 0; i < n_ls; ++i) {
        CalibrationData::LoudspeakerFilter f;
        f.index = i + 1;  // 1-based MATLAB convention
        f.freq_vector = Eigen::Map<const Eigen::VectorXd>(
            kFreqs.data(), 27);
        f.freq_magnitude = Eigen::VectorXd::Ones(27);
        c.loudspeakers.push_back(std::move(f));
    }
    return c;
}

}  // namespace iceberg::io
