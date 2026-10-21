#include "rendering/trajectory.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace iceberg::rendering {

namespace {

// Path angles in 0.01 deg, appended times in ms: what the panel shows.
double round_angle(double deg) { return std::round(deg * 100.0) / 100.0; }
double round_time(double s) { return std::round(s * 1000.0) / 1000.0; }

void check_times(const std::vector<TrajectoryPoint>& points) {
    for (size_t i = 1; i < points.size(); ++i) {
        if (!(points[i].time_s > points[i - 1].time_s)) {
            std::ostringstream msg;
            msg << "Times must increase: " << points[i - 1].time_s << " s is followed by "
                << points[i].time_s << " s.";
            throw std::invalid_argument(msg.str());
        }
    }
}

// The shortest plain decimal (no exponent: "90", not "9e+01") that reads
// back to the same double.
std::string shortest(double v) {
    for (int digits = 1; digits <= 17; ++digits) {
        std::ostringstream s;
        s.precision(digits);
        s << v;
        const std::string text = s.str();
        if (text.find('e') == std::string::npos && std::strtod(text.c_str(), nullptr) == v) {
            return text;
        }
    }
    std::ostringstream s;
    s.precision(17);
    s << v;
    return s.str();
}

}  // namespace

double trajectory_angle(const std::vector<TrajectoryPoint>& points, double time_s) {
    if (points.empty()) throw std::invalid_argument("trajectory_angle: no points");
    check_times(points);
    if (time_s <= points.front().time_s) return points.front().angle_deg;
    if (time_s >= points.back().time_s) return points.back().angle_deg;
    size_t i = 1;
    while (points[i].time_s < time_s) ++i;
    const TrajectoryPoint& a = points[i - 1];
    const TrajectoryPoint& b = points[i];
    const double f = (time_s - a.time_s) / (b.time_s - a.time_s);
    return a.angle_deg + (b.angle_deg - a.angle_deg) * f;
}

std::vector<TrajectoryPoint> parse_trajectory(const std::string& text) {
    std::vector<TrajectoryPoint> points;
    std::string token;
    const auto flush = [&] {
        if (token.empty()) return;
        const auto colon = token.find(':');
        if (colon == std::string::npos || token.find(':', colon + 1) != std::string::npos) {
            throw std::invalid_argument("'" + token + "' is not time:angle (seconds:degrees).");
        }
        const std::string t = token.substr(0, colon), a = token.substr(colon + 1);
        char* end_t = nullptr;
        char* end_a = nullptr;
        const double time_s = std::strtod(t.c_str(), &end_t);
        const double angle = std::strtod(a.c_str(), &end_a);
        if (t.empty() || a.empty() || *end_t != '\0' || *end_a != '\0' || !std::isfinite(time_s) ||
            !std::isfinite(angle)) {
            throw std::invalid_argument("'" + token + "' is not time:angle (seconds:degrees).");
        }
        if (time_s < 0.0) throw std::invalid_argument("'" + token + "': time cannot be negative.");
        points.push_back({time_s, round_angle(angle)});
        token.clear();
    };
    for (const char ch : text) {
        if (ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
            flush();
        } else {
            token += ch;
        }
    }
    flush();
    check_times(points);
    return points;
}

std::vector<TrajectoryPoint> append_trajectory_point(std::vector<TrajectoryPoint> points,
                                                     double angle_deg, double move_s,
                                                     double hold_s) {
    if (!(move_s > 0.0) || !(hold_s >= 0.0) || !std::isfinite(angle_deg)) {
        throw std::invalid_argument("append_trajectory_point: needs move > 0 and hold >= 0");
    }
    double time_s = 0.0;
    double angle = angle_deg;
    if (!points.empty()) {
        const TrajectoryPoint& last = points.back();
        time_s = last.time_s + move_s;
        // The representative of angle_deg nearest the last angle: [-180, 180) away.
        double delta = std::fmod(angle_deg - last.angle_deg + 180.0, 360.0);
        if (delta < 0.0) delta += 360.0;
        angle = last.angle_deg + (delta - 180.0);
    }
    angle = round_angle(angle);
    time_s = round_time(time_s);
    points.push_back({time_s, angle});
    if (hold_s > 0.0) points.push_back({round_time(time_s + hold_s), angle});
    return points;
}

std::string format_trajectory(const std::vector<TrajectoryPoint>& points) {
    std::string out;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i) out += ", ";
        out += shortest(points[i].time_s) + ":" + shortest(points[i].angle_deg);
    }
    return out;
}

}  // namespace iceberg::rendering
