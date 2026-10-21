#pragma once

#include "iceberg_signal.h"

namespace iceberg::native {

/// How `time_shift` obtains the number of samples to shift by.
enum class ShiftMode { Auto, Samples, Time };

/// Output of `time_shift`: the shifted signal and the shift applied.
struct TimeShiftResult {
    Signal signal;
    int shift_samples = 0;   // the shift applied, signed
};

/// Circularly shifts every channel of `s` by the same number of samples k.
/// A positive k moves each sample to a later index, wrapping the end of the
/// signal around to its start: out[(r + k) mod N] = in[r].
///
/// - Auto: k = -(earliest per-channel onset from `start_ir` with a 20 dB
///   threshold), which brings that onset to sample 0. `amount` is ignored;
///   k = 0 when the signal has no rows or no columns.
/// - Samples: k = round(amount), halves away from zero.
/// - Time: k = round(amount * sample_rate), halves away from zero.
///
/// `shift_samples` is k as computed, not reduced modulo N. An empty signal
/// is returned unchanged. The sample rate is kept. Throws
/// std::invalid_argument when the rounded amount is not finite or does not
/// fit in an int.
TimeShiftResult time_shift(const Signal& s, ShiftMode mode, double amount = 0.0);

}  // namespace iceberg::native
