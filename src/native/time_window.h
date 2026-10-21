#pragma once

#include "iceberg_signal.h"

namespace iceberg::native {

/// Shape of the fade applied by `time_window`.
enum class WindowType { Rectwin, Hann };

/// Keeps the head of `s`, fades `[start_sec, end_sec]` out and zeroes the tail.
///
/// Bounds are 1-based sample indices `round(t * fs) + 1` (halves away from
/// zero), each bumped up to the next even number, then clamped to `[1, N]`.
/// `end_sec == 0.0` exactly means "to the end of the signal"; an end bound
/// landing on `N + 1` is pulled back to `N` before clamping.
///
/// Within the range each sample is multiplied by `w[k]` (`k = 0` at the start
/// bound): all ones for `Rectwin`, or the last `L` values of `hann(2 * L)`
/// for `Hann`, i.e. the falling half of a symmetric Hann window, which ends
/// at exactly 0. Window values are computed in double and cast to float once.
/// Samples before the range are copied unchanged; samples after it are 0.
///
/// Returns a signal of the same shape and sample rate as `s`. If the range is
/// empty (`L = end - start + 1 <= 0`, e.g. `start_sec > end_sec` or a negative
/// `end_sec`) or `s` has no samples, returns 0 rows with the same column count
/// and sample rate.
Signal time_window(const Signal& s, double start_sec, double end_sec,
                   WindowType wt = WindowType::Hann);

}  // namespace iceberg::native
