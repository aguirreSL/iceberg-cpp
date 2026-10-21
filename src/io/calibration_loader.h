#pragma once

#include <Eigen/Dense>

#include <string>
#include <vector>

namespace iceberg::io {

struct CalibrationData {
    /// Voltage-to-Pa scaling factor for SPL → digital level math.
    double i_factor;

    /// Per-LS gain factors (one entry per loudspeaker, ordered to
    /// match `loudspeakers[i].index`). Size matches `loudspeakers`.
    Eigen::VectorXd new_level_factor;

    struct LoudspeakerFilter {
        /// 1-based index from MATLAB convention (matches JSON
        /// `loudspeakers[].index`). Treat as metadata for debug /
        /// display only. To index a C++ array (e.g. output channels),
        /// use the position in the `loudspeakers` vector (0-based) OR
        /// convert with the comment idiom:
        ///   // matlab: ls.index → c++: ls.index - 1
        int index;

        /// Frequency grid (Hz), 27 entries in the 24-loudspeaker calibration.
        Eigen::VectorXd freq_vector;

        /// Magnitude curve at each `freq_vector` point. Real-valued —
        /// the MATLAB exporter takes `abs(complex)` upstream.
        Eigen::VectorXd freq_magnitude;
    };

    /// Loudspeaker calibration filters, ordered by JSON appearance.
    /// Minimum 4 entries (algorithm constraint: VBAP pairs wider than
    /// 90 deg break localisation). No hard maximum; the calibration
    /// fixture used in this repo has 24.
    std::vector<LoudspeakerFilter> loudspeakers;
};

/// Load + validate the JSON file produced by
/// `Iceberg/tools/export_calibration.m`.
///
/// Throws `std::runtime_error` with a clear message when:
///   - the file cannot be opened or parsed
///   - any of `iFactor`, `newLevelFactor`, `loudspeakers` is missing
///   - `newLevelFactor.size() != loudspeakers.size()` (consistency)
///   - `loudspeakers.size() < 4` (algorithm minimum)
///   - any LS lacks `index`, `freqVector`, or `freqMagnitude`
///   - any LS has `freqVector.size() != freqMagnitude.size()`
CalibrationData load_calibration(const std::string& json_path);

/// Write `cal` as the JSON `load_calibration` reads (the keys and layout
/// of the MATLAB export: `iFactor`, `newLevelFactor`, `loudspeakers[]` with
/// `index`, `freqVector`, `freqMagnitude`), atomically: a `.tmp` file
/// renamed into place. Doubles keep full precision, so a saved file loads
/// back bit for bit.
///
/// Throws `std::invalid_argument` on data `load_calibration` would reject
/// (fewer than 4 loudspeakers, level factors not one per loudspeaker, a
/// curve whose two vectors differ in length) and `std::runtime_error` on
/// an I/O failure.
void save_calibration(const std::string& json_path, const CalibrationData& cal);

/// Build a "neutral" `CalibrationData` with identity spectral filter
/// (magnitude 1.0 at every band) and unity level factors. Useful for
/// running the pipeline without room compensation — labs without
/// dedicated calibration data, algorithm-only studies, regression
/// fixtures, etc.
///
///   - `i_factor = 2e-5`. With the SPL math
///     `lsdBperVolt = 20·log10(i_factor / 2e-5)`, this yields
///     `lsdBperVolt = 0` so the SPL multiplier collapses to
///     `10^(level / 20)` (raw digital gain).
///   - `new_level_factor = ones(n_ls)`.
///   - Each loudspeaker carries the 27-band 1/3-octave-ish frequency
///     grid from the 24-loudspeaker calibration (50 Hz … 20 kHz)
///     with `freqMagnitude = ones(27)`. The grid points themselves
///     don't matter mathematically (pchip on a flat-1 curve is
///     flat-1 everywhere), but they keep the shape consistent with
///     loaded fixtures.
///
/// Throws `std::invalid_argument` if `n_ls < 4` (algorithm minimum).
CalibrationData neutral_calibration(int n_ls);

}  // namespace iceberg::io
