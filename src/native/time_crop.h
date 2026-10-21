#pragma once

#include "iceberg_signal.h"

namespace iceberg::native {

/// Cuts a time range out of a signal, all channels alike.
///
/// With N samples and fs the sample rate, the 1-based bounds are
/// i1 = round(start_sec * fs) + 1 and i2 = round(end_sec * fs) + 1, halves
/// rounded away from zero. When i2 - i1 is positive and even, i2 moves one
/// sample earlier, so a forward range always keeps an even number of
/// samples.
///
/// - Forward range (i2 > i1): keeps samples i1..i2 inclusive. Throws
///   std::invalid_argument if i1 < 1 or i2 > N.
/// - Otherwise (inverted or empty range): removes a block, keeping the head
///   (samples 1..i2-1) followed by the tail (samples i1+1..N). The range
///   [t, 0] therefore keeps what follows sample i1, one sample later than
///   "from t to the end". Throws std::invalid_argument if i1 < 0 or the
///   head would be longer than N.
///
/// Also throws std::invalid_argument when start_sec * fs or end_sec * fs is
/// not finite or too large to round to an integer index. The result keeps
/// the sample rate and the channel count of `s`.
Signal time_crop(const Signal& s, double start_sec, double end_sec);

}  // namespace iceberg::native
