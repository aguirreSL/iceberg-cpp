#include "experiment/experiment_loader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace iceberg::experiment {

namespace {

constexpr int kSupportedMajorVersion = 1;

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw std::runtime_error("experiment_loader(" + path + "): " + msg);
}

void require(bool cond, const std::string& path, const std::string& msg) {
    if (!cond) fail(path, msg);
}

int parse_major(const std::string& version, const std::string& path) {
    // Accept "1", "1.0", "1.0.0" and any "<major>.*" form. Negative or
    // non-numeric prefix is an error.
    if (version.empty()) fail(path, "empty 'version' field");
    size_t consumed = 0;
    int major = 0;
    try {
        major = std::stoi(version, &consumed);
    } catch (const std::exception&) {
        fail(path, "version field is not a number: '" + version + "'");
    }
    if (consumed == 0) fail(path, "version field is not a number: '" + version + "'");
    return major;
}

std::string utc_iso8601_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    char out[32];
    std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(out);
}

// ---- JSON <-> structs ------------------------------------------------

// Option strings as Iceberg v1.0.0's configSetup spells them (and the
// fixtures' config.json).
using iceberg::rendering::AmbisonicFormat;
using iceberg::rendering::AzimuthConvention;
using iceberg::rendering::DserFactor;

const char* format_to_string(AmbisonicFormat f) {
    switch (f) {
        case AmbisonicFormat::FuMa:  return "fuma";
        case AmbisonicFormat::AmbiX: return "ambix";
        case AmbisonicFormat::N3D:   return "n3d";
    }
    return "fuma";
}

const char* convention_to_string(AzimuthConvention c) {
    return c == AzimuthConvention::Clockwise ? "clockwise" : "counterclockwise";
}

nlohmann::json dser_to_json(const DserFactor& d) {
    switch (d.kind) {
        case DserFactor::Kind::Unity:   return "unity";
        case DserFactor::Kind::Thesis:  return "thesis";
        case DserFactor::Kind::Numeric: return d.value;
    }
    return "unity";
}

DserFactor dser_from_json(const nlohmann::json& j, const std::string& path,
                          const std::string& where) {
    DserFactor d;
    if (j.is_number()) {
        d.kind = DserFactor::Kind::Numeric;
        d.value = j.get<double>();
        require(std::isfinite(d.value) && d.value >= 0.0, path,
                where + " must be a finite scale >= 0");
        return d;
    }
    require(j.is_string(), path, where + " must be 'unity', 'thesis' or a number");
    const std::string v = j.get<std::string>();
    if (v == "unity")       d.kind = DserFactor::Kind::Unity;
    else if (v == "thesis") d.kind = DserFactor::Kind::Thesis;
    else fail(path, where + " '" + v + "' is not 'unity', 'thesis' or a number");
    return d;
}

AmbisonicFormat format_from_json(const nlohmann::json& j, const std::string& path,
                                 const std::string& where) {
    const std::string v = j.get<std::string>();
    if (v == "fuma")  return AmbisonicFormat::FuMa;
    if (v == "ambix") return AmbisonicFormat::AmbiX;
    if (v == "n3d")   return AmbisonicFormat::N3D;
    fail(path, where + " '" + v + "' is not 'fuma', 'ambix' or 'n3d'");
}

AzimuthConvention convention_from_json(const nlohmann::json& j, const std::string& path,
                                       const std::string& where) {
    const std::string v = j.get<std::string>();
    if (v == "counterclockwise") return AzimuthConvention::CounterClockwise;
    if (v == "clockwise")        return AzimuthConvention::Clockwise;
    fail(path, where + " '" + v + "' is not 'counterclockwise' or 'clockwise'");
}

nlohmann::json metrics_to_json(const Slot::CachedMetrics& m) {
    return {
        {"peak_abs", m.peak_abs},
        {"rms", m.rms},
        {"n_samples", m.n_samples},
        {"c_time", m.c_time},
    };
}

Slot::CachedMetrics metrics_from_json(const nlohmann::json& j,
                                      const std::string& path,
                                      const std::string& where) {
    Slot::CachedMetrics m;
    require(j.contains("peak_abs"), path, where + ".peak_abs missing");
    require(j.contains("rms"),       path, where + ".rms missing");
    require(j.contains("n_samples"), path, where + ".n_samples missing");
    require(j.contains("c_time"),    path, where + ".c_time missing");
    m.peak_abs   = j.at("peak_abs").get<double>();
    m.rms        = j.at("rms").get<double>();
    m.n_samples  = j.at("n_samples").get<int>();
    m.c_time     = j.at("c_time").get<double>();
    return m;
}

nlohmann::json slot_to_json(const Slot& s) {
    nlohmann::json j;
    j["name"]            = s.name;
    j["ir_path"]         = s.ir_path;
    j["layout_path"]     = s.layout_path;
    if (s.cal_path) j["cal_path"] = *s.cal_path;
    else            j["cal_path"] = nullptr;
    j["signal_path"]     = s.signal_path;
    j["source_angle_deg"] = s.source_angle_deg;
    j["level_db"]        = s.level_db;
    j["use_full_scale"]  = s.use_full_scale;
    j["default_ls_order"] = s.default_ls_order;
    j["dser_factor"]     = dser_to_json(s.dser_factor);
    j["ambisonic_format"] = format_to_string(s.ambisonic_format);
    j["azimuth_convention"] = convention_to_string(s.azimuth_convention);
    j["anechoic_special_case"] = s.anechoic_special_case;
    nlohmann::json path = nlohmann::json::array();
    for (const auto& p : s.trajectory) path.push_back({p.time_s, p.angle_deg});
    j["trajectory"] = std::move(path);
    if (s.cached_metrics) {
        j["cached_metrics"] = metrics_to_json(*s.cached_metrics);
    }
    return j;
}

Slot slot_from_json(const nlohmann::json& j, size_t idx,
                    const std::string& path) {
    const std::string where =
        "slots[" + std::to_string(idx) + "]";

    require(j.contains("name"),             path, where + ".name missing");
    require(j.contains("ir_path"),          path, where + ".ir_path missing");
    require(j.contains("layout_path"),      path, where + ".layout_path missing");
    require(j.contains("signal_path"),      path, where + ".signal_path missing");
    require(j.contains("source_angle_deg"), path, where + ".source_angle_deg missing");
    require(j.contains("level_db"),         path, where + ".level_db missing");

    Slot s;
    s.name        = j.at("name").get<std::string>();
    s.ir_path     = j.at("ir_path").get<std::string>();
    s.layout_path = j.at("layout_path").get<std::string>();
    s.signal_path = j.at("signal_path").get<std::string>();
    s.source_angle_deg = j.at("source_angle_deg").get<double>();
    s.level_db    = j.at("level_db").get<double>();

    if (j.contains("cal_path") && !j.at("cal_path").is_null()) {
        s.cal_path = j.at("cal_path").get<std::string>();
    }

    // Schema 1.2 additive: present in 1.2+, absent in 1.0/1.1 ->
    // default true (full scale). Older files load OK.
    if (j.contains("use_full_scale") && !j.at("use_full_scale").is_null()) {
        s.use_full_scale = j.at("use_full_scale").get<bool>();
    }

    // Schema 1.3 additive: absent before 1.3 -> v1.0.0's defaults and the
    // 0, 90, 180, 270 order.
    if (j.contains("default_ls_order") && !j.at("default_ls_order").is_null()) {
        const auto& o = j.at("default_ls_order");
        require(o.is_array() && o.size() == 4, path,
                where + ".default_ls_order must hold 4 azimuths");
        for (size_t k = 0; k < 4; ++k) s.default_ls_order[k] = o[k].get<int>();
        require(is_default_ls_permutation(s.default_ls_order), path,
                where + ".default_ls_order must be a permutation of 0, 90, 180, 270");
    }
    if (j.contains("dser_factor") && !j.at("dser_factor").is_null()) {
        s.dser_factor = dser_from_json(j.at("dser_factor"), path, where + ".dser_factor");
    }
    if (j.contains("ambisonic_format") && !j.at("ambisonic_format").is_null()) {
        s.ambisonic_format =
            format_from_json(j.at("ambisonic_format"), path, where + ".ambisonic_format");
    }
    if (j.contains("azimuth_convention") && !j.at("azimuth_convention").is_null()) {
        s.azimuth_convention = convention_from_json(j.at("azimuth_convention"), path,
                                                    where + ".azimuth_convention");
    }
    if (j.contains("anechoic_special_case") && !j.at("anechoic_special_case").is_null()) {
        s.anechoic_special_case = j.at("anechoic_special_case").get<bool>();
    }
    // Schema 1.4 additive: absent before 1.4 -> no path.
    if (j.contains("trajectory") && !j.at("trajectory").is_null()) {
        const auto& traj = j.at("trajectory");
        require(traj.is_array(), path, where + ".trajectory must be an array");
        for (const auto& p : traj) {
            require(p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number(),
                    path, where + ".trajectory holds [seconds, degrees] pairs");
            s.trajectory.push_back({p[0].get<double>(), p[1].get<double>()});
        }
        try {
            // The parser's own checks: finite, non-negative, increasing times.
            (void)iceberg::rendering::parse_trajectory(
                iceberg::rendering::format_trajectory(s.trajectory));
        } catch (const std::invalid_argument& e) {
            fail(path, where + ".trajectory: " + e.what());
        }
    }

    require(s.source_angle_deg >= 0.0 && s.source_angle_deg < 360.0,
            path, where + ".source_angle_deg out of range [0, 360): " +
                  std::to_string(s.source_angle_deg));
    // Range covers both slider semantics: dBFS [-60, 0] in full-scale
    // mode (cal_path nullopt) and SPL [0, 95] with a real cal. Loader
    // accepts the union; the GUI clamps per mode at edit time.
    require(s.level_db >= -60.0 && s.level_db <= 95.0,
            path, where + ".level_db out of range [-60, 95]: " +
                  std::to_string(s.level_db));

    if (j.contains("cached_metrics") && !j.at("cached_metrics").is_null()) {
        s.cached_metrics = metrics_from_json(j.at("cached_metrics"),
                                             path, where + ".cached_metrics");
    }

    // Runtime fields explicit reset (defaults already match, but
    // keep this loud in case a future refactor changes the struct).
    s.is_rendered = false;
    s.is_stale = true;
    s.output_buffer.reset();
    s.error_message.reset();
    s.render_notices.clear();
    s.trajectory_buffer.reset();
    return s;
}

nlohmann::json play_to_json(const PlayOptions& p) {
    return {
        {"wait_between_slots", p.wait_between_slots},
        {"wait_seconds", p.wait_seconds},
        {"preparation_mode", p.preparation_mode},
    };
}

PlayOptions play_from_json(const nlohmann::json& j,
                           const std::string& path) {
    PlayOptions p;
    require(j.contains("wait_between_slots"), path,
            "play_options.wait_between_slots missing");
    require(j.contains("wait_seconds"), path,
            "play_options.wait_seconds missing");
    p.wait_between_slots = j.at("wait_between_slots").get<bool>();
    p.wait_seconds = j.at("wait_seconds").get<double>();
    // Additive: present in 1.1+, absent in 1.0 -> default false.
    if (j.contains("preparation_mode") && !j.at("preparation_mode").is_null()) {
        p.preparation_mode = j.at("preparation_mode").get<bool>();
    }
    return p;
}

}  // namespace

bool is_default_ls_permutation(const std::array<int, 4>& order) {
    std::array<int, 4> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    return sorted == std::array<int, 4>{0, 90, 180, 270};
}

Experiment load_experiment(const std::string& path) {
    std::ifstream in(path);
    if (!in) fail(path, "cannot open file");

    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error& e) {
        fail(path, std::string("JSON parse error: ") + e.what());
    }

    require(j.contains("version"),      path, "missing field 'version'");
    require(j.contains("name"),         path, "missing field 'name'");
    require(j.contains("slots"),        path, "missing field 'slots'");
    require(j.contains("play_options"), path, "missing field 'play_options'");

    Experiment exp;
    exp.version = j.at("version").get<std::string>();
    const int major = parse_major(exp.version, path);
    if (major != kSupportedMajorVersion) {
        fail(path, "version major " + std::to_string(major) +
                   " not supported (this binary speaks " +
                   std::to_string(kSupportedMajorVersion) + ".x.x)");
    }

    exp.name = j.at("name").get<std::string>();
    if (j.contains("created"))  exp.created_iso8601  = j.at("created").get<std::string>();
    if (j.contains("modified")) exp.modified_iso8601 = j.at("modified").get<std::string>();

    const auto& jslots = j.at("slots");
    require(jslots.is_array(), path, "'slots' must be a JSON array");
    exp.slots.reserve(jslots.size());
    for (size_t i = 0; i < jslots.size(); ++i) {
        exp.slots.push_back(slot_from_json(jslots[i], i, path));
    }

    exp.play_options = play_from_json(j.at("play_options"), path);
    return exp;
}

void save_experiment(const std::string& path, Experiment& exp) {
    const std::string now = utc_iso8601_now();
    if (exp.created_iso8601.empty()) exp.created_iso8601 = now;
    exp.modified_iso8601 = now;
    // Every slot field of this binary's schema is written, so the file is
    // this schema whatever version it was loaded from.
    exp.version = kExperimentVersion;

    nlohmann::json j;
    j["version"]  = exp.version;
    j["name"]     = exp.name;
    j["created"]  = exp.created_iso8601;
    j["modified"] = exp.modified_iso8601;

    nlohmann::json jslots = nlohmann::json::array();
    for (const auto& s : exp.slots) jslots.push_back(slot_to_json(s));
    j["slots"] = std::move(jslots);
    j["play_options"] = play_to_json(exp.play_options);

    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream out(tmp_path);
        if (!out) {
            throw std::runtime_error(
                "save_experiment(" + path + "): cannot open temp file '" +
                tmp_path + "' for writing");
        }
        out << j.dump(2);
        out << "\n";
        if (!out) {
            throw std::runtime_error(
                "save_experiment(" + path + "): write to temp file failed");
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path);
        throw std::runtime_error(
            "save_experiment(" + path + "): rename failed: " + ec.message());
    }
}

}  // namespace iceberg::experiment
