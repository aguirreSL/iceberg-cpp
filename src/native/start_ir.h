#pragma once

#include <Eigen/Dense>

namespace iceberg::native {

/// Onset of an impulse response, per channel (ISO 3382-1:2009, A.3.4).
///
/// `ir` is (nSamples x nChannels). Returns one 0-based sample index per
/// column: the last sample more than `|threshold_db|` dB below the peak
/// before the peak, moved earlier when energy rises back above
/// `-|threshold_db| + 6` dB before that point. Only the magnitude of
/// `threshold_db` is used.
///
/// The onset is 0 (no shift) when the column is empty, when the peak is
/// less than 20 dB above the noise level (mean energy of the last ~10 % of
/// the column), when the peak lies in that noise region, or when the peak
/// is the first sample. Never throws.
Eigen::VectorXi start_ir(const Eigen::MatrixXd& ir, double threshold_db = 20.0);

}  // namespace iceberg::native
