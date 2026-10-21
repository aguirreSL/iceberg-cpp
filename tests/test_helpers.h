#pragma once

#include "io/calibration_loader.h"
#include "io/layout_loader.h"
#include "io/wav_loader.h"
#include "rendering/calibrate_ambisonics.h"  // ConfigSetup
#include "iceberg_signal.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef ICEBERG_FIXTURES_DIR
#error "ICEBERG_FIXTURES_DIR must be defined by CMake"
#endif

namespace iceberg::test {

/// Returns true if the fixtures directory is reachable on disk. Used
/// to gate fixture-dependent tests at runtime: when CI builds with
/// `-DICEBERG_REQUIRE_FIXTURES=OFF`, tests call `GTEST_SKIP()` via
/// the `ICEBERG_SKIP_IF_NO_FIXTURES()` macro below; local builds
/// (default `ON`) leave the macro inert so tests fail loudly when
/// fixtures are absent.
inline bool fixtures_present() {
    std::error_code ec;
    return std::filesystem::exists(
        std::string(ICEBERG_FIXTURES_DIR), ec);
}

#ifdef ICEBERG_REQUIRE_FIXTURES
#  define ICEBERG_SKIP_IF_NO_FIXTURES() ((void)0)
#else
#  define ICEBERG_SKIP_IF_NO_FIXTURES()                              \
      do {                                                           \
          if (!::iceberg::test::fixtures_present()) {                \
              GTEST_SKIP() << "Fixtures dir missing ("               \
                           << ICEBERG_FIXTURES_DIR                   \
                           << "); skipping fixture-dependent test."; \
          }                                                          \
      } while (0)
#endif

/// Path of a file in the v1.0.0 fixture layout, relative to its root
/// (see the fixtures README: `sweep/`, `stages/`, `inputs/`, ...).
inline std::string fixture_file(const std::string& relative) {
    return std::string(ICEBERG_FIXTURES_DIR) + "/" + relative;
}

inline nlohmann::json load_fixture_json(const std::string& relative) {
    const std::string path = fixture_file(relative);
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("load_fixture_json: cannot open " + path);
    }
    nlohmann::json j;
    in >> j;
    return j;
}

/// Layout and calibration of a v1.0.0 case: `<case_dir>/config.json`
/// plus the calibration file it names (the shared `calibration.json`,
/// or a case's own, e.g. `inputs/four_alone/calibration_4ls.json`).
inline iceberg::rendering::ConfigSetup load_case_setup(
    const std::string& case_dir) {
    auto cfg = iceberg::io::load_layout_json(
        fixture_file(case_dir + "/config.json"));
    const auto j = load_fixture_json(case_dir + "/config.json");
    cfg.calibration = iceberg::io::load_calibration(
        fixture_file(j.at("calibration").get<std::string>()));

    using Kind = iceberg::rendering::DserFactor::Kind;
    const auto& d = j.at("dserFactor");
    if (d.is_string()) {
        const auto s = d.get<std::string>();
        if (s == "unity") {
            cfg.dser_factor.kind = Kind::Unity;
        } else if (s == "thesis") {
            cfg.dser_factor.kind = Kind::Thesis;
        } else {
            throw std::runtime_error("load_case_setup: unknown dserFactor " + s);
        }
    } else {
        cfg.dser_factor.kind = Kind::Numeric;
        cfg.dser_factor.value = d.get<double>();
    }
    cfg.anechoic_special_case = j.at("anechoicSpecialCase").get<bool>();

    using Format = iceberg::rendering::AmbisonicFormat;
    const auto format = j.at("ambisonicFormat").get<std::string>();
    if (format == "fuma") {
        cfg.ambisonic_format = Format::FuMa;
    } else if (format == "ambix") {
        cfg.ambisonic_format = Format::AmbiX;
    } else if (format == "n3d") {
        cfg.ambisonic_format = Format::N3D;
    } else {
        throw std::runtime_error("load_case_setup: unknown ambisonicFormat " + format);
    }
    using Convention = iceberg::rendering::AzimuthConvention;
    const auto convention = j.at("azimuthConvention").get<std::string>();
    if (convention == "counterclockwise") {
        cfg.azimuth_convention = Convention::CounterClockwise;
    } else if (convention == "clockwise") {
        cfg.azimuth_convention = Convention::Clockwise;
    } else {
        throw std::runtime_error("load_case_setup: unknown azimuthConvention " + convention);
    }
    return cfg;
}

/// Level of a v1.0.0 case: a number, or NaN for MATLAB's `'n'` (bypass).
inline double case_level(const nlohmann::json& config) {
    const auto& l = config.at("level");
    return l.is_string() ? std::numeric_limits<double>::quiet_NaN() : l.get<double>();
}

/// Columns of a full-ring render that belong to the active loudspeakers,
/// in `ls_dir` order: the layout of every v1.0.0 render fixture.
inline Eigen::MatrixXf active_columns(const Eigen::MatrixXf& full,
                                      const Eigen::VectorXi& active_ls_numbers) {
    Eigen::MatrixXf out(full.rows(), active_ls_numbers.size());
    for (Eigen::Index i = 0; i < active_ls_numbers.size(); ++i) {
        // matlab: out.time(:, activeLSNumbers) → c++: col(active_ls_numbers(i) - 1)
        out.col(i) = full.col(active_ls_numbers(i) - 1);
    }
    return out;
}

/// Worst per-channel difference between an output and a MATLAB reference
/// of the same shape: RMS of the difference, and peak difference relative
/// to the reference channel's peak (channels whose reference is all zero
/// are skipped for the relative figure).
struct DiffStats {
    double worst_rms = 0.0;
    double worst_rel_peak = 0.0;
};

inline DiffStats diff_stats(const Eigen::MatrixXf& out, const Eigen::MatrixXf& ref) {
    DiffStats s;
    for (Eigen::Index c = 0; c < ref.cols(); ++c) {
        const Eigen::ArrayXd d =
            (out.col(c).cast<double>() - ref.col(c).cast<double>()).array();
        s.worst_rms = std::max(s.worst_rms, std::sqrt(d.square().mean()));
        const double peak = ref.col(c).cwiseAbs().maxCoeff();
        if (peak > 0.0) {
            s.worst_rel_peak = std::max(s.worst_rel_peak, d.abs().maxCoeff() / peak);
        }
    }
    return s;
}

// A fixture WAV read as double, (frames x channels). The hardware-calibration
// references are 64-bit float WAVs that back 1e-10 checks; load_wav would
// round them to float.
inline Eigen::MatrixXd load_wav_f64(const std::string& relative) {
    return iceberg::io::load_wav_double(fixture_file(relative));
}

// max|out - ref| / max|ref|: the helper-level contract (<= 1e-10).
inline double rel_peak_diff(const Eigen::VectorXd& out, const Eigen::VectorXd& ref) {
    if (out.size() != ref.size()) return std::numeric_limits<double>::infinity();
    return (out - ref).cwiseAbs().maxCoeff() / ref.cwiseAbs().maxCoeff();
}

/// One measured error and the tolerance its assertion uses, for the
/// test report: a `measure:<label>` property of the running test in the
/// `--gtest_output=xml` file ("<value> <limit> <unit>", both `%.6e`), and
/// one line on stdout. `unit` is one token: rms, rel, abs, s, ms, dB or
/// samples.
inline void record_measure(const std::string& label, double value, double limit,
                           const std::string& unit) {
    char numbers[64];
    std::snprintf(numbers, sizeof(numbers), "%.6e %.6e", value, limit);
    ::testing::Test::RecordProperty("measure:" + label,
                                    std::string(numbers) + " " + unit);
    std::cout << "[measure] " << label << ": " << value << " (limit " << limit
              << ", " << unit << ")\n";
}

}  // namespace iceberg::test
