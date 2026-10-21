#pragma once

#include <Eigen/Dense>

namespace iceberg::calibration {

/// Digital Butterworth bandpass of total order `order` (even; MATLAB
/// `butter(order/2, [f_low f_high]/(fs/2), 'bandpass')`) as second-order
/// sections, one row `[b0 b1 b2 1 a1 a2]` per conjugate (or real) pole
/// pair, zeros at +1 and -1 in every row, the gain in the first.
///
/// Written by hand (no filter-design dependency):
/// prewarp `u = 4*tan(pi*W/2)` (MATLAB's `fs = 2`), the order/2 analog
/// prototype poles, lowpass-to-bandpass (`s^2 - p*Bw*s + W0^2 = 0`), the
/// bilinear map in MATLAB's form `(1 + s/4)/(1 - s/4)` with gain
/// `Bw^(order/2) * real(4^(order/2) / prod(4 - s))`. MATLAB finds the
/// poles as eigenvalues of a state-space matrix, so this matches its
/// filter to rounding, not bit for bit (checked against `zp2sos(butter)`
/// in the fixtures).
///
/// Throws `std::invalid_argument` unless `order` is even and positive and
/// `0 < f_low < f_high < fs/2`.
Eigen::MatrixXd butterworth_bandpass_sos(int order, double f_low, double f_high, double fs);

/// MATLAB `sosfilt(sos, x)`: the sections in cascade, each in direct form
/// II with MATLAB's order of operations,
/// `w = (x - a1*w1) - a2*w2`, `y = (b0*w + b1*w1) + b2*w2`, zero initial
/// state. Rows are `[b0 b1 b2 a0 a1 a2]` with `a0 = 1`.
Eigen::VectorXd sosfilt(const Eigen::MatrixXd& sos, const Eigen::VectorXd& x);

/// Zero-phase Butterworth band-pass of the room calibration, applied
/// through the spectrum: the magnitude of the `rfft` of the filter's
/// impulse response (`Nf = min(2^15, nextpow2(n))` samples, run through
/// `sosfilt(butterworth_bandpass_sos(order, f_low, f_high, fs), .)` and
/// zero-padded to `n`) multiplies each bin of `rfft(x)`, and `irfft` of the
/// product, `n` samples long, is returned.
///
/// Throws `std::invalid_argument` when `x` has fewer than `Nf` samples,
/// which means a length below `2^15` that is not a power of two; the
/// exceptions of `butterworth_bandpass_sos` (odd or non-positive `order`,
/// band edges outside `0 < f_low < f_high < fs/2`) pass through.
Eigen::VectorXd filter_bandpass(const Eigen::VectorXd& x, double fs, double f_low,
                                double f_high, int order = 10);

}  // namespace iceberg::calibration
