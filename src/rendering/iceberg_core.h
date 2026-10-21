#pragma once

#include "iceberg_signal.h"

namespace iceberg::rendering {

/// Output of `iceberg_core`: DSER (mono envelope, for VBAP path) and
/// IR_Late (full 4-ch B-Format, for Ambisonics path), both at the
/// original IR's sample rate and length.
struct IcebergCoreOutput {
    iceberg::Signal dser;     ///< mono (n × 1)
    iceberg::Signal ir_late;  ///< 4-ch B-Format (n × 4)
};

/// Split a B-Format impulse response into early (DSER, mono) and late
/// (IR_Late, 4-ch) parts using the ISO 3382 center time `Ts` computed
/// on the W (channel 0) of the IR.
///
/// Mirrors Iceberg v1.0.0 `src/core/iceberg_core.m`. Both branches use a
/// `time_shift('auto')` → process → `time_shift(abs(shift_index))`
/// resync pattern so the output stays aligned with the input timeline.
///
/// `anechoic_special_case` (MATLAB `configSetup.anechoicSpecialCase`,
/// default false) reproduces the thesis-era rt_00 behaviour: a single
/// direct peak has no meaningful centre time, so the early part is a
/// fixed 10 ms rectangle. The same 10 ms rectangle is the fallback when
/// `center_time` throws, as MATLAB's try/catch does.
///
/// Throws on empty IR (zero rows or zero columns).
IcebergCoreOutput iceberg_core(const iceberg::Signal& ir,
                               bool anechoic_special_case = false);

}  // namespace iceberg::rendering
