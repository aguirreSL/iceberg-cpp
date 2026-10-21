#pragma once

#include "native/time_window.h"

#include <Eigen/Dense>

#include <vector>

/// The signals of the room-calibration measurement: mono `double` vectors,
/// not the render's multichannel `float` signals (`iceberg::Signal`).
namespace iceberg::calibration {

/// Unit of the bounds given to `time_window`.
enum class RangeUnit { Seconds, Samples };

/// Windows `x` with one or two pairs of bounds, each a fade-out
/// (`start <= end`) or a fade-in (`start > end`).
///
/// `range` holds 2 or 4 bounds. With `RangeUnit::Samples` they are 1-based
/// sample positions. With `RangeUnit::Seconds` a time `t` becomes the 1-based
/// sample `round(t * fs) + 1`, a second bound landing on `N + 1` is pulled
/// back to `N`, and every odd bound moves up to the next even one. Bounds past
/// the end are clamped to the length `N` of `x`.
///
/// A fade-out keeps the samples before `start`, weights `start .. end` with the
/// falling half of `hann(2h)` (`h = |end - start| + 1`; ones for `Rectwin`) and
/// zeroes the samples after `end`. A fade-in zeroes the samples before `end`
/// and weights the next `h` samples with the rising half (ones for `Rectwin`).
/// With 4 bounds the second pair is applied first, never symmetric, then the
/// first pair. `symmetric` mirrors the first pair's weights onto the other end
/// of the signal, so one pair fades in and out.
///
/// Throws `std::invalid_argument` if `range` does not hold 2 or 4 values, or
/// if a pair has a bound below 1 once converted and clamped.
Eigen::VectorXd time_window(const Eigen::VectorXd& x, double fs,
                            const std::vector<double>& range, RangeUnit unit,
                            iceberg::native::WindowType type = iceberg::native::WindowType::Hann,
                            bool symmetric = false);

/// Divides `x` by its largest absolute value.
///
/// Returns `x` unchanged when it is empty or has no positive peak (all zeros).
/// Does not throw.
Eigen::VectorXd normalize_peak(const Eigen::VectorXd& x);

/// The measurement sweep: `2^fft_degree` samples at `fs`, peak-normalised.
///
/// Built in the frequency domain with a flat magnitude and a group delay that
/// rises linearly from 30 ms to `2^fft_degree / fs - stop_margin`, at twice
/// the final length, then refined by alternating a fade-in / fade-out time
/// window with a projection back onto unit magnitude, and cut to its first
/// half.
///
/// Throws `std::invalid_argument` if `fft_degree` is outside `0 .. 29` (the
/// doubled length must fit an `int`), or if `stop_margin` is so large that the
/// final group delay falls below 30 ms.
Eigen::VectorXd sweep(double stop_margin, double fs, int fft_degree);

/// Pink noise from a white draw `white` sampled at `fs`, made by the caller so
/// that a test can replay a reference draw.
///
/// Divides every bin of the spectrum of `white` by `sqrt(j * 2 * pi * f)` and
/// zeroes DC; the result has the length of `white` (empty for an empty draw).
/// Does not throw.
Eigen::VectorXd pink_noise(const Eigen::VectorXd& white, double fs);

}  // namespace iceberg::calibration
