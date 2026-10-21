#pragma once

#include <string>
#include <vector>

namespace iceberg::rendering {

/// One point of a programmed source path.
struct TrajectoryPoint {
    double time_s = 0.0;
    double angle_deg = 0.0;   ///< as written: not wrapped, so 0 -> 270 passes 90 and 180
};

/// The source angle at `time_s`: linear in time between two points, held
/// before the first and after the last. Angles are not wrapped, so the
/// path goes the way it is written: 0 -> 270
/// counter-clockwise through 90 and 180, 0 -> -90 clockwise, 0 -> 720
/// twice round. `points` must be non-empty with strictly increasing
/// times (`std::invalid_argument`).
double trajectory_angle(const std::vector<TrajectoryPoint>& points, double time_s);

/// The path as the slot panel writes it: `time:angle` pairs in seconds and
/// degrees, separated by commas and/or spaces ("0:0, 2:90, 3:90, 5:-90").
/// Angles are rounded to 2 decimals (0.01 deg), so the
/// text shows what is rendered. Empty text is no path. Throws `std::invalid_argument` with a message
/// for the user on a malformed pair, a number that is not finite, a
/// negative time or times that do not increase.
std::vector<TrajectoryPoint> parse_trajectory(const std::string& text);

/// The inverse of `parse_trajectory` ("0:0, 2:90, ..."), shortest decimals.
std::string format_trajectory(const std::vector<TrajectoryPoint>& points);

/// The slot panel's "Add point": the source goes to
/// `angle_deg` and stays there. On an empty path the first point is at
/// time 0; otherwise it arrives `move_s` after the last point, the short
/// way round (the angle is written within 180 degrees of the last one, so
/// 350 -> 10 passes 0). A positive `hold_s` adds the point where it
/// leaves. The angle is rounded to 2 decimals and the times to the
/// millisecond, so the text stays short. Throws `std::invalid_argument`
/// unless `move_s > 0` (times must increase) and `hold_s >= 0`.
std::vector<TrajectoryPoint> append_trajectory_point(std::vector<TrajectoryPoint> points,
                                                     double angle_deg, double move_s,
                                                     double hold_s);

}  // namespace iceberg::rendering
