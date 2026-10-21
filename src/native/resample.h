#pragma once

#include <Eigen/Dense>

#include <utility>

namespace iceberg::native {

/// MATLAB `[N, D] = rat(x, tol)`: continued-fraction approximation of
/// `x`, stopping when `|N/D - x| <= max(tol, eps(x))`. Iceberg v1.0.0
/// calls `rat(irFs / signalFs, 1e-12)` to get the resampling ratio.
std::pair<long long, long long> rat(double x, double tol);

/// Kaiser window, MATLAB `kaiser(n, beta)`: `I0(beta*sqrt(1 - r^2)) /
/// I0(beta)` on the symmetric grid `r = (2k - (n-1)) / (n-1)`.
Eigen::VectorXd kaiser(Eigen::Index n, double beta);

/// MATLAB `resample(x, p, q)` with its default filter (n = 10, beta = 5),
/// column by column, bit-near: the same filter design and the same
/// delay compensation and length (`ceil(rows * p / q)`).
///
/// The filter is `firls(2*10*max(p,q), [0 2fc 2fc 1], [1 1 0 0])` (a
/// type I full-band design, which reduces to the truncated ideal
/// low-pass `2*fc*sinc(2*k*fc)` with `fc = 1/(2*max(p,q))`) times a
/// Kaiser window, scaled to sum to `p`; zeros are added in front so the
/// down-sampling hits the centre tap, and behind so the output reaches
/// the full length; then `upfirdn` and the delay is cut. A different
/// resampler (another filter) would differ from MATLAB by far more than
/// the port's tolerances, which is why this one is replicated rather
/// than taken from a library.
///
/// `p == q` returns `x` unchanged. Throws `std::invalid_argument` unless
/// `p, q >= 1`.
Eigen::MatrixXd resample(const Eigen::MatrixXd& x, long long p, long long q);

}  // namespace iceberg::native
