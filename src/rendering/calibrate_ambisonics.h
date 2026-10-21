#pragma once

#include "io/calibration_loader.h"
#include "iceberg_signal.h"

#include <Eigen/Dense>

#include <optional>
#include <string>

namespace iceberg::rendering {

/// Loudspeaker layout + calibration bundle consumed by the rendering
/// stage.
///
/// Constraints (validated by `validate_config`):
///
///   1. `ls_array.size() == calibration.loudspeakers.size()` — the
///      master array maps one-to-one with calibration entries.
///   2. `ls_array.size() >= 4` — algorithm minimum.
///   3. Every value in `active_ls_numbers` is in
///      `{1, 2, ..., ls_array.size()}` (1-based MATLAB convention).
///   4. `ls_dir.rows() == active_ls_numbers.size()` — one row per
///      active LS.
///   5. `ls_dir.col(1)` (elevation) is zero everywhere — the C++
///      port is 2D-ring-only.
///
/// Soft constraint (NOT validated, but documented for callers):
///
///   - `ls_array` should cover the horizontal ring (gaps wider than
///     ~90° degrade VBAP image localisation; the wrap-around pair
///     created by `findLsPairs` over a wide gap will fail the
///     `min(g1, g2) > -tol` check for typical source angles, leaving
///     the gap region with all-zero gains). Irregular spacing is
///     fine; missing half the ring is not.
/// Direct-branch output scale, MATLAB `configSetup.dserFactor` (Iceberg
/// v1.0.0): `Unity` (default) plays the direct branch at the declared
/// level; `Thesis` multiplies it by `max(DSER)` (about -3.01 dB for the
/// shipped FuMa W IRs), reproducing the 2020 measurement chain; `Numeric`
/// multiplies it by `value`.
struct DserFactor {
    enum class Kind { Unity, Thesis, Numeric };
    Kind kind = Kind::Unity;
    double value = 1.0;   ///< used by `Numeric` only
};

/// Ambisonics convention of the impulse response, MATLAB
/// `configSetup.ambisonicFormat`: `FuMa` (W X Y Z, W scaled by 1/sqrt(2)),
/// `AmbiX` (ACN order W Y Z X, SN3D) or `N3D` (ACN order, N3D).
enum class AmbisonicFormat { FuMa, AmbiX, N3D };

/// How the angles (source, `ls_array`, `ls_dir`) are counted, MATLAB
/// `configSetup.azimuthConvention`: counter-clockwise (0 front, 90 left,
/// the Ambisonics / polarch / ODEON convention) or clockwise (90 right).
enum class AzimuthConvention { CounterClockwise, Clockwise };

struct ConfigSetup {
    /// Azimuths (degrees) of the full master loudspeaker ring. Size
    /// matches `calibration.loudspeakers`.
    Eigen::VectorXi ls_array;

    /// 1-based positions in `ls_array` for the active subset (kept
    /// for downstream consumers; `calibrate_ambisonics` does NOT use
    /// this field — it recomputes the mapping from `ls_dir`/`ls_array`
    /// inside, mirroring the MATLAB reference).
    Eigen::VectorXi active_ls_numbers;

    /// `(n_active × 2)` matrix of `[azimuth_deg, elevation_deg]`.
    /// Elevation column is zero — the port is 2D-only.
    Eigen::MatrixXd ls_dir;

    /// Calibration data parsed from `calibration.json`.
    iceberg::io::CalibrationData calibration;

    /// Direct-branch output scale (MATLAB `dserFactor`, default unity).
    DserFactor dser_factor;

    /// MATLAB `anechoicSpecialCase`: split the IR with a fixed 10 ms
    /// rectangle instead of the centre time (the thesis-era rt_00 case).
    bool anechoic_special_case = false;

    /// IR convention. Unset: FuMa is assumed and `iceberg()` reports it.
    std::optional<AmbisonicFormat> ambisonic_format;

    /// Angle convention. Unset: counter-clockwise is assumed and
    /// `iceberg()` reports it.
    std::optional<AzimuthConvention> azimuth_convention;
};

/// Validate a `ConfigSetup` against the constraints documented above.
/// Returns `std::nullopt` if valid; otherwise a human-readable error
/// message describing the FIRST violation (in declaration order, so a
/// failure earlier in the list shadows later ones).
///
/// Cheap to call (O(n_LS)). Intended to run at pipeline entry —
/// `iceberg()` calls this and throws if invalid.
std::optional<std::string> validate_config(const ConfigSetup& config);

/// The row of `ls_dir` nearest to `source_angle_deg` by wrap-around
/// angular distance: the loudspeaker whose calibration curve
/// `calibrate_ambisonics` applies. Tie-break: the loudspeaker reached first
/// clockwise from the source (45 -> 0, 135 -> 90, 225 -> 180, 315 -> 270 on
/// the cardinal ring); a full tie keeps the first row (MATLAB's stable
/// `sortrows`). Allocation-free, for the live engine's audio thread.
Eigen::Index nearest_loudspeaker(double source_angle_deg, const Eigen::MatrixXd& ls_dir);

/// Apply per-LS frequency filter and SPL alignment to a mono signal,
/// using the nearest physical loudspeaker as the virtual reference
/// (Nearest Speaker Pan, NSP).
///
/// Mirrors `Iceberg/src/calibration/calibrate_ambisonics.m` — note the
/// MATLAB tree puts this under `calibration/` while the C++ port lives
/// under `rendering/`, keeping the per-render DSP apart from the
/// calibration loader in `io/`.
///
/// Parameters
///   `signal`           Mono input. Throws `std::invalid_argument`
///                      otherwise.
///   `level_db`         Target SPL. Pass NaN
///                      (`std::numeric_limits<double>::quiet_NaN()`)
///                      to skip the SPL multiplication entirely
///                      (MATLAB sentinel `'n'`). Throws
///                      `std::invalid_argument` if a finite value
///                      exceeds 95 dB.
///   `source_angle_deg` Virtual source azimuth (degrees, can be any
///                      real number — wrapped onto `[0, 360)` via
///                      modulo before NSP).
///   `config`           Loudspeaker layout + calibration bundle.
iceberg::Signal calibrate_ambisonics(const iceberg::Signal& signal,
                                     double level_db,
                                     double source_angle_deg,
                                     const ConfigSetup& config);

}  // namespace iceberg::rendering
