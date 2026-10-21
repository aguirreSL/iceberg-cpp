#pragma once

#include "iceberg_signal.h"

namespace iceberg::native {

/// Centre time `Ts` (ISO 3382-1) of channel 0 of `ir` (the W channel of a
/// B-format IR), in seconds, computed in double.
///
/// The channel is aligned to its ISO 3382-1 onset and its trailing zeros are
/// trimmed. The energy is then integrated up to the point where the decay
/// meets the background noise, found by the iterative method of Lundeby et
/// al. (1995), and the truncated tail is added back as an exponential decay.
/// When that method finds no crossing point, the plain energy centroid of the
/// aligned channel is returned instead.
///
/// Throws `std::invalid_argument` when `ir` has no rows or no columns, and
/// `std::runtime_error` when the aligned channel is empty (all zeros) or the
/// crossing-point iteration cannot continue (block length below one sample,
/// no complete block, empty noise window).
double center_time(const Signal& ir);

}  // namespace iceberg::native
