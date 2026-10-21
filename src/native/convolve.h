#pragma once

#include "iceberg_signal.h"

namespace iceberg::native {

/// How `convolve` computes each channel's linear convolution.
enum class ConvolveMethod {
    FFT,     ///< Zero-padded real FFT in double, 2-3-5 transform size. Default.
    Direct,  ///< Time-domain sum in float, terms in increasing signal index.
};

/// Channel-wise linear convolution of `signal` with `ir`.
///
/// With `na`, `nb` samples and `cs`, `ci` channels, output channel `k`
/// is signal column `min(k, cs - 1)` convolved with IR column
/// `min(k, ci - 1)`: the operand with fewer channels reuses its last
/// column. The output has `max(cs, ci)` columns and the sample rate of
/// `signal`.
///
/// Length: the linear length is `nl = na + nb - 1`. The output has `nl`
/// rows rounded up to an even count (an odd `nl` gets one trailing row
/// that stays 0), or 0 rows when `nl <= 0`.
///
/// Edge cases: if `na`, `nb`, `cs` or `ci` is 0, the result is that
/// all-zero output. Throws `std::invalid_argument` when the sample rates
/// differ (exact comparison, checked before anything else) and
/// `std::length_error` when the FFT size does not fit in an `int`.
Signal convolve(const Signal& signal, const Signal& ir,
                ConvolveMethod method = ConvolveMethod::FFT);

}  // namespace iceberg::native
