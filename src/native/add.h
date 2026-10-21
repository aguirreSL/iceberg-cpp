#pragma once

#include "iceberg_signal.h"

namespace iceberg::native {

/// Sums two signals after padding both with zeros to a common shape.
///
/// The result has max(rows) samples, max(columns) channels and the sample
/// rate of `a`. Where both operands have a sample the result is `a + b`;
/// where only one has it, that sample is copied unchanged; elsewhere it is
/// 0. Empty operands are valid and contribute nothing.
///
/// Throws std::invalid_argument when the sample rates differ (exact
/// comparison).
Signal add(const Signal& a, const Signal& b);

}  // namespace iceberg::native
