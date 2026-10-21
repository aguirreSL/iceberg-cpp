#pragma once

#include <Eigen/Dense>

namespace iceberg::native {

/// `n` evenly spaced points from `d1` to `d2`, bit-identical to MATLAB
/// `linspace(d1, d2, n)` for the non-symmetric case (`d1 != -d2`).
///
/// MATLAB computes `d1 + ((0:n-1) .* (d2 - d1)) ./ (n-1)`, multiplying
/// before dividing, and then forces both endpoints. Eigen's `LinSpaced`
/// multiplies by a precomputed step instead, which rounds differently in
/// the last bit; on the calibration frequency grid that flips pchip
/// interval lookups at ~1e-12 Hz (Iceberg v1.0.0 builds its grids with
/// `linspace` for this reason). Throws `std::invalid_argument` if `n < 1`
/// or `d1 == -d2` with `n > 2` (MATLAB's symmetric branch, not needed).
Eigen::VectorXd linspace(double d1, double d2, Eigen::Index n);

}  // namespace iceberg::native
