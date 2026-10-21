#pragma once

#include <Eigen/Dense>

#include <complex>

namespace iceberg::native {

/// Real-input forward FFT (rfft).
///
/// Returns the first `N/2 + 1` complex bins for an `N`-sample real input.
/// Normalisation matches MATLAB `fft`: no scale on the forward transform.
///
/// Backed by pocketfft (C++ header-only).
Eigen::VectorXcd rfft(const Eigen::VectorXd& time);

/// Inverse rfft.
///
/// Reconstructs an `n_time`-sample real signal from `N/2 + 1` complex
/// bins. Normalisation matches MATLAB `ifft`: divides by `n_time` so
/// that `irfft(rfft(x), x.size()) == x` numerically.
///
/// `freq.size()` must equal `n_time / 2 + 1`. Throws
/// `std::invalid_argument` otherwise.
Eigen::VectorXd irfft(const Eigen::VectorXcd& freq, int n_time);

}  // namespace iceberg::native
