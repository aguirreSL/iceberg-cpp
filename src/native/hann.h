#pragma once

#include <Eigen/Dense>

namespace iceberg::native {

/// Symmetric Hann window of length N (matches MATLAB `hann(N)`).
///
/// `w[k] = 0.5 * (1 - cos(2π·k / (N - 1)))` for `k = 0..N-1` and `N >= 2`.
/// Endpoints are exactly zero. For `N == 1` returns `[0.5]` (matching
/// MATLAB's degenerate convention). For `N <= 0` returns an empty array.
///
/// Returned as `ArrayXd` so callers stay in double precision until the
/// final cast back to `float` at sample application.
///
/// **Not** the same as `hanning(N)`, see below.
Eigen::ArrayXd hann(int n);

/// Periodic-style Hann window of length N (matches MATLAB `hanning(N)`).
///
/// `w[k] = 0.5 * (1 - cos(2π·(k+1) / (N + 1)))` for `k = 0..N-1`.
/// Endpoints are NON-zero — this is the key behavioural difference from
/// `hann(N)` (which zeros them). Used by the VBAP block-OLA in
/// `vbap_render` because the non-zero endpoints prevent click artefacts
/// at block boundaries; `hann` would zero out the boundary samples and
/// the OLA sum across blocks would dip there.
///
/// `N <= 0` returns empty; `N == 1` returns `[0.5]`.
Eigen::ArrayXd hanning(int n);

}  // namespace iceberg::native
