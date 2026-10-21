#pragma once

#include <Eigen/Dense>

namespace iceberg::native {

/// Scales a (nSamples x nChannels) buffer so that its largest absolute
/// sample, taken over all channels together, becomes 1. Every channel is
/// divided by the same global peak, so their relative levels are kept.
///
/// An empty buffer (no rows or no columns) and a buffer whose peak is not
/// positive (all zeros) are returned unchanged. Never throws.
Eigen::MatrixXf normalize_dat(const Eigen::MatrixXf& signal);

}  // namespace iceberg::native
