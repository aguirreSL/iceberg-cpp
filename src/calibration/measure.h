#pragma once

#include "calibration/spectrum.h"   // BandSpectrum
#include "io/calibration_loader.h"   // CalibrationData

#include <Eigen/Dense>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace iceberg::calibration {

/// The audio device, as Iceberg v1.0.0's `ioFun`: plays `page` (samples x
/// channels, column k on output channel `play_channels[k]`) and returns the
/// recording of input channel `rec_channel`. Channels are 0-based. The app
/// backs it with the sound card; tests with a simulated device. It may
/// throw to stop a measurement (e.g. the user cancelled).
using PlayRecord = std::function<Eigen::VectorXd(const Eigen::MatrixXd& page,
                                                 const std::vector<int>& play_channels,
                                                 int rec_channel, double fs)>;

/// Progress lines, as MATLAB prints them ("Measuring loudspeaker 2", the
/// SPL of each repeat).
using Progress = std::function<void(const std::string&)>;

/// A measurement refused or abandoned for safety. `Clipping`: the stimulus
/// would exceed digital full scale, so nothing was played.
/// `NoConvergence`: a loudspeaker did not reach the target within the
/// allowed corrections. Both usually mean the target is unreachable or the
/// microphone does not pick up the loudspeaker.
class MeasurementError : public std::runtime_error {
public:
    enum class Kind { Clipping, NoConvergence };
    MeasurementError(Kind kind, int loudspeaker, const std::string& message)
        : std::runtime_error(message), kind_(kind), loudspeaker_(loudspeaker) {}
    Kind kind() const { return kind_; }
    int loudspeaker() const { return loudspeaker_; }   ///< 0-based
private:
    Kind kind_;
    int loudspeaker_;
};

/// A swept-sine measurement of the transfer function between `play`'s
/// output and its recording, as an impulse response of `excitation.size()`
/// samples at `fs`. `play` gets the stimulus (`excitation`, or, when `eq`
/// is given, `excitation` pre-equalised by `eq` interpolated onto the bins
/// with `pchip`) `averages` times; the recordings are averaged and
/// deconvolved by the regularised inverse of `excitation` over
/// `[f_low, f_high]`, so a measurement through `eq` keeps the EQ in its
/// result.
///
/// Throws `std::runtime_error` when a recording's length differs from the
/// excitation's. Whatever `play` throws passes through (the app cancels a
/// measurement that way).
Eigen::VectorXd measure_tf(const Eigen::VectorXd& excitation, double fs, double f_low,
                           double f_high, int averages,
                           const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& play,
                           const BandSpectrum* eq = nullptr);

struct TfConfig {
    int n_loudspeakers = 4;           ///< output channels 0 .. n-1
    int input_channel = 0;
    double fs = 48000.0;
    int averages = 3;
    int fft_degree = 15;              ///< sweep of 2^fft_degree samples
    double output_amplification_db = -22.0;   ///< sweep level, as Iceberg MATLAB
};

struct TfResult {
    std::vector<BandSpectrum> filters;        ///< 1/3-octave EQ per loudspeaker, 50 Hz .. 20 kHz
    std::vector<Eigen::VectorXd> rir;         ///< measured impulse responses
    std::vector<Eigen::VectorXd> rir_adjusted;   ///< measured again through the EQ
};

/// `getTF`: per loudspeaker, the sweep at `output_amplification_db`
/// (refused with `MeasurementError::Clipping` if it would clip), the
/// impulse response, then bandpass 20 Hz .. 22.6 kHz, one-octave
/// smoothing, normalisation, inversion and 1/3-octave averaging into the
/// EQ (last band copied from the one before, as Iceberg MATLAB does), and a check
/// measurement through that EQ. MATLAB's 1.5 s pause between loudspeakers
/// is the device's business.
TfResult get_tf(const TfConfig& config, const PlayRecord& io, const Progress& progress = {});

/// A loudspeaker's response before its equalisation and through it, for
/// the user to judge the EQ: 1/3-octave band levels from 50 Hz to 20 kHz,
/// each curve in dB about its own mean over 100 Hz .. 16 kHz, and over that
/// range half the distance between the highest and the lowest band (the
/// "+-" shown). MATLAB's getTF saves the check measurement but does not show
/// it; this is the app's reading of it.
struct ResponseCheck {
    Eigen::VectorXd centres;      ///< nominal band centres, Hz
    Eigen::VectorXd before_db;    ///< NaN for a band with no spectral line
    Eigen::VectorXd after_db;
    double before_spread_db = 0.0;
    double after_spread_db = 0.0;
};

/// `rir` and `rir_adjusted` as `get_tf` returns them, at `fs`. Throws
/// `std::invalid_argument` when either holds no band between 100 Hz and
/// 16 kHz.
ResponseCheck response_check(const Eigen::VectorXd& rir, const Eigen::VectorXd& rir_adjusted,
                             double fs);

/// getLevel's excitations, before its peak normalisation.
/// 1: the 2^16-sample sweep.
Eigen::VectorXd level_excitation_sweep(double fs);
/// 2: pink noise from `white` (MATLAB: `randn(2^16, 1)`), a 20 ms fade-in,
///    bandpassed to `[f_low, f_high]`.
Eigen::VectorXd level_excitation_pink(const Eigen::VectorXd& white, double fs, double f_low,
                                      double f_high);
/// 3: `native_ltass`: `white` (MATLAB: `randn(round(seconds*speech_fs), 1)`)
///    shaped by the long-term average spectrum of `speech` (averaged
///    periodograms over up to 10 s windows, periodic Hann), peak-normalised,
///    resampled to `fs` when the speech rate differs.
Eigen::VectorXd level_excitation_ltass(const Eigen::VectorXd& speech, double speech_fs,
                                       double fs, const Eigen::VectorXd& white);

struct LevelConfig {
    double level = 70.0;            ///< target dB SPL per loudspeaker
    double tolerance_db = 0.5;
    int n_average = 3;              ///< measurements averaged per step
    int max_iterations = 10;        ///< corrections per loudspeaker before giving up
    int n_loudspeakers = 4;
    int input_channel = 0;
    double fs = 48000.0;
    double lower_freq = 60.0;
    double upper_freq = 20000.0;
};

struct LevelResult {
    Eigen::VectorXd level_factor;   ///< new factor per loudspeaker
    Eigen::VectorXd spl;            ///< last average SPL per loudspeaker [dB]
    std::vector<int> corrections;   ///< corrections each loudspeaker took
};

/// `getLevel`: per loudspeaker, the excitation scaled to the target SPL
/// (`20*log10(i_factor/2e-5)` dB per volt), equalised by its filter,
/// times its level factor, faded (50 ms, both ends) and bandpassed, played
/// alone; the recording faded and bandpassed the same way, its SPL from
/// the summed power times `i_factor`. Repeats `n_average` times and, while
/// the average is outside the tolerance, corrects the factor by the
/// difference and measures again. Starts from `|initial_factor|` (MATLAB:
/// the current calibration's `newLevelFactor`).
///
/// Throws `MeasurementError::Clipping` (nothing played) and
/// `MeasurementError::NoConvergence` (after `max_iterations` corrections);
/// `std::invalid_argument` when fewer filters or initial factors than
/// loudspeakers are given.
LevelResult get_level(const LevelConfig& config, double i_factor,
                      const std::vector<BandSpectrum>& filters,
                      const Eigen::VectorXd& initial_factor, const Eigen::VectorXd& excitation,
                      const PlayRecord& io, const Progress& progress = {});

/// The three stages as the calibration file the renderer loads:
/// loudspeaker k (output channel k, `index` k + 1) gets `filters[k]` as
/// its curve and `level_factor(k)`. Throws `std::invalid_argument` when
/// the counts differ.
iceberg::io::CalibrationData to_calibration_data(double i_factor,
                                                 const std::vector<BandSpectrum>& filters,
                                                 const Eigen::VectorXd& level_factor);

}  // namespace iceberg::calibration
