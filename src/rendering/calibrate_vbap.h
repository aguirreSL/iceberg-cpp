#pragma once

#include "rendering/calibrate_ambisonics.h"  // ConfigSetup
#include "iceberg_signal.h"

namespace iceberg::rendering {

/// Apply the per-LS frequency filter and a single SPL-aligned level stage
/// to a VBAP-rendered multi-channel signal (Iceberg v1.0.0).
///
/// Mirrors `Iceberg/src/calibration/calibrate_vbap.m` (v1.0.0) — note the
/// MATLAB tree puts this under `calibration/`; the C++ port lives under
/// `rendering/`.
///
/// One common scaler for every channel, so the VBAP gains reach the
/// loudspeakers unchanged: `||g||_2 = 1`, so the RMS of the Euclidean
/// sum across channels is the RMS the mono signal had before the pan,
/// and dividing every channel by the same number anchors the SPL
/// without touching the balance (dividing each channel by its own RMS
/// would erase the VBAP gains).
///
/// Parameters
///   `signal_to_play`     Multi-channel input. `samples.cols()` must
///                        equal `config.ls_dir.rows()`.
///   `level_db`           SPL of the rendered input. NaN
///                        (`std::numeric_limits<double>::quiet_NaN()`)
///                        is MATLAB's `'n'`: the scale stage is
///                        bypassed (no common scaler, no level gain);
///                        the per-LS filter and level factor still apply.
///   `source_angle_deg`   Ignored, kept for signature compatibility (the
///                        calibration is angle-independent by design,
///                        as in MATLAB).
///   `config`             Loudspeaker layout + calibration bundle.
///
/// v1.0.0 order, kept as is: the common scaler is computed on the raw
/// input, before NaN samples are zeroed, so a single NaN sample makes
/// the whole output NaN unless the level is bypassed.
iceberg::Signal calibrate_vbap(const iceberg::Signal& signal_to_play,
                               double level_db,
                               double source_angle_deg,
                               const ConfigSetup& config);

}  // namespace iceberg::rendering
