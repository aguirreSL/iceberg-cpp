#pragma once

#include <Eigen/Dense>

namespace iceberg::native {

/// 1-D piecewise cubic Hermite interpolation, MATLAB-compatible.
///
/// Implements the Fritsch–Carlson algorithm
/// (Fritsch & Carlson, "Monotone Piecewise Cubic Interpolation", 1980)
/// — same derivative selection as MATLAB's `pchip`. Endpoints use the
/// non-centered three-point formula with sign / 3·slope clamps.
///
/// Outside `[x.front(), x.back()]` the value is extrapolated using the
/// boundary cubic, matching MATLAB's `pchip` extrapolation behaviour.
///
/// Throws `std::invalid_argument` when `x.size() != y.size()`,
/// `x.size() < 2`, or `x` is not strictly increasing.
Eigen::VectorXd pchip(const Eigen::VectorXd& x,
                      const Eigen::VectorXd& y,
                      const Eigen::VectorXd& xq);

}  // namespace iceberg::native
