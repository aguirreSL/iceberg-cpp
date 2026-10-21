#pragma once

#include "iceberg_signal.h"

#include <Eigen/Dense>

namespace iceberg::rendering {

/// Pan a mono signal to a 2D loudspeaker ring via VBAP, using
/// block-overlap-add (block-OLA) with a `hanning` window.
///
/// Mirrors the inner `ring_VBAP` function of
/// `Iceberg/src/rendering/iceberg_set_vbap.m`. The outer wrapper that
/// also runs `native_convolve` and `calibrate_vbap` is `iceberg()`.
///
/// **Architecture note.** The block-OLA scaffolding is inherited from
/// the .m original, which supports time-varying source trajectories
/// (`azis[]` per block). In the current static-source pipeline the
/// output is mathematically equivalent to `signal × constant_gains`
/// (a single VBAP gain vector applied to the whole signal). The
/// block-OLA path is preserved for fidelity to the MATLAB pipeline
/// and to unblock dynamic trajectories without future refactor.
///
/// Parameters
///   `signal`               Mono input. Throws `std::invalid_argument`
///                          otherwise.
///   `source_angle_deg`     Source azimuth (degrees, wrapped onto
///                          `[0, 360)` by the VBAP gains math).
///   `source_elevation_deg` Source elevation (degrees). Currently
///                          ignored — the 2D ring layout discards
///                          elevation. Pass 0.0.
///   `ls_dir`               `(n_active × ≥1)` matrix; column 0 is the
///                          azimuth in degrees of each LS in the ring
///                          (must be ≥ 2 LS).
///
/// Output: `Signal` with `(n_signal_samples × n_active)` channels
/// (column 0 corresponds to `ls_dir.row(0)`, etc.). Sample rate
/// preserved.
iceberg::Signal vbap_render(const iceberg::Signal& signal,
                            double source_angle_deg,
                            double source_elevation_deg,
                            const Eigen::MatrixXd& ls_dir);

/// The block overlap-add `vbap_render` applies, with unit gain, in double:
/// for a static angle, `vbap_render(s, a)` column k is `g_k(a)` times this
/// (to rounding). The live engine's direct streams start from it.
/// Throws `std::invalid_argument` unless `signal` is mono.
Eigen::VectorXd vbap_overlap_add(const iceberg::Signal& signal);

}  // namespace iceberg::rendering
