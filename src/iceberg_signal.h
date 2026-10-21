#pragma once

#include <Eigen/Dense>

namespace iceberg {

/// Canonical time-domain signal carried between DSP stages.
///
/// Mirrors MATLAB's `itaAudio.time` + `samplingRate`:
///   - `samples` is shaped (nSamples × nChannels), column-major (each
///     channel contiguous).
///   - `sample_rate` is a double to ease ratio math (`t = n / fs`); WAV
///     files have integer rates which fit double exactly.
///
/// Empty / degenerate signals are valid: any of `(0,0)`, `(0,N)`, `(N,0)`
/// represent MATLAB's `isempty(signal.time)` case.
struct Signal {
    Eigen::MatrixXf samples;
    double sample_rate = 0.0;
};

}  // namespace iceberg
