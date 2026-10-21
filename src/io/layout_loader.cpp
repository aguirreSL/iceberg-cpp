#include "io/layout_loader.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

namespace iceberg::io {

namespace {

constexpr int kMinSpeakers = 4;

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw std::runtime_error("load_layout_json(" + path + "): " + msg);
}

void require(bool cond, const std::string& path, const std::string& msg) {
    if (!cond) fail(path, msg);
}

iceberg::rendering::ConfigSetup parse_speakers_format(
    const nlohmann::json& j, const std::string& path) {
    const auto& sp = j.at("speakers");
    require(sp.is_array(), path, "'speakers' must be an array");
    const int n = static_cast<int>(sp.size());
    require(n >= kMinSpeakers, path,
            "'speakers' size " + std::to_string(n) +
            " is below algorithm minimum " +
            std::to_string(kMinSpeakers));

    iceberg::rendering::ConfigSetup cfg;
    cfg.ls_array.resize(n);
    cfg.ls_dir.resize(n, 2);
    cfg.active_ls_numbers.resize(n);

    for (int i = 0; i < n; ++i) {
        require(sp[i].contains("azimuth"), path,
                "speaker[" + std::to_string(i) +
                "] missing 'azimuth'");
        require(sp[i].contains("elevation"), path,
                "speaker[" + std::to_string(i) +
                "] missing 'elevation'");

        const double azi = sp[i].at("azimuth").get<double>();
        const double ele = sp[i].at("elevation").get<double>();
        require(ele == 0.0, path,
                "speaker[" + std::to_string(i) +
                "] elevation = " + std::to_string(ele) +
                "; port is 2D-only");

        cfg.ls_array(i) = static_cast<int>(azi);
        cfg.ls_dir(i, 0) = azi;
        cfg.ls_dir(i, 1) = ele;
        cfg.active_ls_numbers(i) = i + 1;  // 1-based MATLAB convention
    }
    return cfg;
}

iceberg::rendering::ConfigSetup parse_fixture_format(
    const nlohmann::json& j, const std::string& path) {
    require(j.contains("lsArray"), path, "missing 'lsArray'");
    require(j.contains("activeLSNumbers"), path,
            "missing 'activeLSNumbers'");
    require(j.contains("ls_dir"), path, "missing 'ls_dir'");

    iceberg::rendering::ConfigSetup cfg;

    const auto& arr = j.at("lsArray");
    cfg.ls_array.resize(static_cast<Eigen::Index>(arr.size()));
    for (size_t i = 0; i < arr.size(); ++i) {
        cfg.ls_array(static_cast<Eigen::Index>(i)) = arr[i].get<int>();
    }
    require(static_cast<int>(cfg.ls_array.size()) >= kMinSpeakers, path,
            "'lsArray' size " + std::to_string(cfg.ls_array.size()) +
            " is below algorithm minimum " +
            std::to_string(kMinSpeakers));

    const auto& act = j.at("activeLSNumbers");
    cfg.active_ls_numbers.resize(static_cast<Eigen::Index>(act.size()));
    for (size_t i = 0; i < act.size(); ++i) {
        cfg.active_ls_numbers(static_cast<Eigen::Index>(i)) =
            act[i].get<int>();
    }

    const auto& dir = j.at("ls_dir");
    cfg.ls_dir.resize(static_cast<Eigen::Index>(dir.size()), 2);
    for (size_t i = 0; i < dir.size(); ++i) {
        cfg.ls_dir(static_cast<Eigen::Index>(i), 0) =
            dir[i][0].get<double>();
        cfg.ls_dir(static_cast<Eigen::Index>(i), 1) =
            dir[i][1].get<double>();
        require(cfg.ls_dir(static_cast<Eigen::Index>(i), 1) == 0.0,
                path,
                "ls_dir[" + std::to_string(i) +
                "][1] (elevation) = " +
                std::to_string(
                    cfg.ls_dir(static_cast<Eigen::Index>(i), 1)) +
                "; port is 2D-only");
    }
    return cfg;
}

}  // namespace

iceberg::rendering::ConfigSetup load_layout_json(const std::string& path) {
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

    if (j.contains("speakers")) {
        return parse_speakers_format(j, path);
    }
    if (j.contains("lsArray")) {
        return parse_fixture_format(j, path);
    }
    fail(path,
         "JSON must contain either 'speakers' (a list of loudspeakers) "
         "or 'lsArray' (a ring, as Iceberg MATLAB's configuration)");
}

}  // namespace iceberg::io
