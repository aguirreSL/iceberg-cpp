#pragma once

#include "rendering/calibrate_ambisonics.h"  // ConfigSetup, validate_config
#include "rendering/input_handling.h"       // RenderNotice
#include "iceberg_signal.h"

#include <vector>

namespace iceberg::rendering {

/// Top-level Iceberg auralisation: VBAP for direct sound + early
/// reflections, Ambisonics (SAD) for late reverberation.
///
/// Mirrors Iceberg v1.0.0 `src/core/iceberg.m`, input handling included
/// (`prepare_inputs`: format conversion, order reduction, azimuth
/// convention, resampling). Pipeline:
///
///   1. `iceberg_core(ir, config.anechoic_special_case)` →
///      `(dser, ir_late)`.
///   2. **VBAP path:** `normalize_dat(signal)` → `convolve(·, dser)` →
///      `vbap_render` → `calibrate_vbap` (single level stage on the
///      panned output) → `config.dser_factor`.
///   3. **Ambi path:** `normalize_dat(signal)` →
///      `calibrate_ambisonics` (mono, on the dry signal) →
///      `convolve(signal_calibrated, ir_late)` → `decode_bformat`
///      with `sad_decoder(ls_dir)`.
///   4. Distribute both multichannel outputs to the master array,
///      using `config.active_ls_numbers` as the master-index
///      mapping. Inactive channels stay zero.
///   5. `native::add(vbap_audio, amb_audio)`.
///
/// **Calibration is always applied in the C++ port.** The MATLAB
/// reference makes calibration optional via `numel(...) > 0` on the
/// loudspeaker filter field; the C++ `ConfigSetup` mandates a
/// calibration, validated by `validate_config`. Callers that want
/// to skip room compensation should pass
/// `iceberg::io::neutral_calibration(n_ls)` as the calibration
/// payload.
///
/// Output shape is `(2*ceil((signal.rows() + ir.rows() - 1) / 2),
/// config.ls_array.size())`: `native_convolve`'s even length.
///
/// Throws `std::invalid_argument` if the config fails validation, the
/// signal is not mono, or the IR is not (N+1)^2-channel B-Format.
/// Both convolutions (DSER and late paths) run by FFT, as Iceberg v1.0.0
/// does.
///
/// `notices` (optional) receives what the input handling did or found
/// (MATLAB's warnings: format assumed or converted, order reduced, format
/// suspect, convention assumed, signal resampled), in MATLAB's order.
iceberg::Signal iceberg(const iceberg::Signal& signal,
                        const iceberg::Signal& ir,
                        double source_angle_deg,
                        double level_db,
                        const ConfigSetup& config,
                        std::vector<RenderNotice>* notices = nullptr);

}  // namespace iceberg::rendering
