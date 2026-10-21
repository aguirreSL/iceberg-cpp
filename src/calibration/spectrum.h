#pragma once

#include <Eigen/Dense>

#include <string>
#include <vector>

namespace iceberg::calibration {

enum class BandMethod { Added, Averaged };

struct CenterFrequencies {
    Eigen::VectorXd nominal;   ///< rounded, as labelled (50, 63, 80, ...)
    Eigen::VectorXd exact;     ///< 1000 * 2^(k/bands)
};

struct MicFactor {
    double i_factor;   ///< 1 / v1k: digital units to pascal for a 1 Pa calibrator
    double v1k;        ///< the tone RMS in digital units
};

/// `mic_calibration_factor(reference, fs, f_ref)`: the one-sided power
/// spectrum's magnitude at `f_ref`, linearly interpolated between bins
/// (exact on a bin; off a bin, leakage lowers it). Throws
/// `std::invalid_argument` when `f_ref` is outside `[0, fs/2]` (MATLAB
/// returns NaN) or the recording has fewer than 2 samples.
MicFactor mic_calibration_factor(const Eigen::VectorXd& reference, double fs,
                                 double f_ref = 1000.0);

/// The microphone factor for a calibrator of `calibrator_db` dB SPL, with
/// 94 dB taken as exactly 1 Pa as in Iceberg (MATLAB assumes a 94 dB
/// calibrator: `iFactor = 1/v1k`): `10^((calibrator_db - 94)/20) / v1k`.
/// At 94 dB it is `mic_calibration_factor`'s `i_factor` exactly.
double mic_factor_for_calibrator(double v1k, double calibrator_db);

/// What the app checks on a calibrator recording before taking its factor
/// (not in MATLAB, which takes any recording; the app warns, it does not
/// block). Each warning is a sentence for the user.
struct CalibratorCheck {
    double peak_dbfs = 0.0;
    double rms_dbfs = 0.0;
    double strongest_hz = 0.0;     ///< the largest spectral line above DC
    double tone_fraction = 0.0;    ///< power within +-5 % of f_ref over the total (DC excluded)
    double level_spread_db = 0.0;  ///< max - min RMS over 0.5 s windows
    std::vector<std::string> warnings;
};

/// Warns when the recording reaches full scale (peak >= 0.999), is very
/// low (RMS < -60 dBFS), peaks more than 2 % away from `f_ref`, holds
/// less than 90 % of its power near `f_ref` (about 10 dB of noise or
/// another signal), or changes level by more than 0.5 dB between 0.5 s
/// windows.
CalibratorCheck check_calibrator_recording(const Eigen::VectorXd& recording, double fs,
                                           double f_ref = 1000.0);

struct BandSpectrum {
    Eigen::VectorXd freq_vector;   ///< nominal band centres
    Eigen::VectorXd freq;          ///< root of the band power (NaN for an empty band)
};

/// The per-bin factor that turns the `n / 2 + 1` bins of `rfft` of an
/// `n`-sample signal into one-sided amplitudes of a power signal:
/// `sqrt(2) / n`, except `1 / n` at DC and, at the last bin, `sqrt(2) / (2n)`
/// for even `n` (the Nyquist bin) or `sqrt(2) / n` for odd `n`. The last-bin
/// rule wins when there is a single bin (`n` of 0 or 1), so `n == 1` gives
/// `sqrt(2)`. `n` must not be negative; `n == 0` gives infinity.
Eigen::VectorXd one_sided_scale(Eigen::Index n);

/// `rfft(x)` scaled bin by bin by `one_sided_scale(x.size())`: the one-sided
/// spectrum of a power signal, `x.size() / 2 + 1` bins.
Eigen::VectorXcd one_sided_spectrum(const Eigen::VectorXd& x);

/// The RMS level of `x` from its one-sided spectrum: the root of the summed
/// bin powers (`Added`), or of their mean over the `x.size() / 2 + 1` bins
/// (`Averaged`).
double spk2level(const Eigen::VectorXd& x, BandMethod method);

/// The base-2 fractional-octave band centres `1000 * 2^(k / bands_per_octave)`
/// (IEC 61260) whose bands lie between `f_low` and `f_high`, with the top
/// limit pulled to half a band below `fs / 2`. `exact` holds the centres,
/// `nominal` the labels: the centre rounded to its leading digit when that
/// is within 4 % of the centre rounded to steps of 5 in its third
/// significant digit, otherwise the latter. Both are empty when no band
/// fits. Throws `std::invalid_argument` when `bands_per_octave < 1` or a
/// limit is zero or negative (the band range would be unbounded).
CenterFrequencies ansi_center_frequencies(double f_low, double f_high, int bands_per_octave,
                                          double fs);

/// The regularised (Kirkeby) inverse of the impulse response `x`, same
/// length: `conj(X) / (|X|^2 + eps)`, divided by the one-sided scale, with
/// `eps` half the peak bin power times `beta^2` inside `[f_low, f_high]`
/// and half the peak bin power outside (doubled at DC), joined by squared
/// Hann crossfades over the half octave below `f_low` and above `f_high`
/// (the upper one only when `f_high < fs / 2`). Throws
/// `std::invalid_argument` when `x` has fewer than 2 samples or a crossfade
/// would fall outside the spectrum (e.g. `f_low` far above `fs / 2`).
Eigen::VectorXd invert_spk_regularization(const Eigen::VectorXd& x, double fs, double f_low,
                                          double f_high, double beta = 1e-10);

/// Every bin divided by the largest bin magnitude (NaN bins ignored when
/// finding it), so the peak has magnitude 1. An all-zero spectrum gives
/// NaN; an empty one stays empty.
Eigen::VectorXcd normalize_spk(const Eigen::VectorXcd& freq_data);

/// The fractional-octave band spectrum of the one-sided spectrum
/// `freq_data` of an `n`-sample signal at `fs`: per band of
/// `ansi_center_frequencies` within `[f_low, f_high]` (clipped to the bin
/// range), the root of the summed (`Added`) or mean (`Averaged`) power of
/// the bins in `[lower edge, upper edge)`; NaN for a band with no bin.
/// Throws `std::invalid_argument` unless `freq_data.size() == n / 2 + 1`.
BandSpectrum spk2frequencybands(const Eigen::VectorXcd& freq_data, Eigen::Index n, double fs,
                                int bands_per_octave, double f_low, double f_high,
                                BandMethod method);

/// Fractional-octave smoothing of the one-sided spectrum `freq_data` of an
/// `n`-sample signal at `fs`: the magnitude, resampled linearly on a grid
/// of one point per cent over the 20 octaves below `fs / 2`, is averaged
/// over `window_width` octaves (an odd number of points, shrinking windows
/// at both ends), then brought back to the bins by a not-a-knot spline;
/// DC keeps its magnitude and every bin keeps its phase. A grid point
/// outside the bin range (the top one, when the last bin falls just below
/// `fs / 2`) is NaN and spreads to its neighbours. Throws
/// `std::invalid_argument` unless `freq_data.size() == n / 2 + 1`, or when
/// `window_width` rounds to less than one point.
Eigen::VectorXcd smooth_frequency(const Eigen::VectorXcd& freq_data, Eigen::Index n, double fs,
                                  double window_width = 1.0);

}  // namespace iceberg::calibration
