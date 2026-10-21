#pragma once

#include <Eigen/Dense>

namespace iceberg::native {

/// MATLAB `interp1(x, y, xq, 'spline')`: the not-a-knot cubic spline,
/// extrapolated by the end pieces.
///
/// Since R2026a `interp1` does not go through `spline.m` but through the
/// builtin `griddedInterpolant`, whose results this reproduces with the
/// following order of operations: the tridiagonal slope system of
/// `spline.m` with `x(3) - x(1)` taken as `h(1) + h(2)`, the end rows written as
/// `(h2 + h2)*dd1 + (h1/(h1 + h2))*(h2*dd1 + h1*dd2)` (and its mirror),
/// Thomas elimination without pivoting, pieces
/// `[A/h, (dd - s(i))/h - A, s(i), y(i)]` with
/// `A = ((s(i) + s(i+1)) - (dd + dd))/h`, evaluated by Horner from the
/// piece's left break. A query on an interior break takes the piece that
/// starts there. Three points give the parabola, two the line.
/// Checked against MATLAB R2026b `interp1(..., 'spline')` to 1e-14 relative
/// (`NativeSpline.MatchesMatlabInterp1Spline`).
///
/// Throws `std::invalid_argument` when `x.size() != y.size()`,
/// `x.size() < 2`, or `x` is not strictly increasing.
Eigen::VectorXd spline(const Eigen::VectorXd& x,
                       const Eigen::VectorXd& y,
                       const Eigen::VectorXd& xq);

}  // namespace iceberg::native
