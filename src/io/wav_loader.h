#pragma once

#include "iceberg_signal.h"

#include <Eigen/Dense>

#include <string>

namespace iceberg::io {

/// Load a WAV file into the canonical `iceberg::Signal`: samples (rows) by
/// channels (columns) and the file's sample rate.
///
/// Reads PCM integer (8-bit unsigned, 16, 24 and 32-bit) and IEEE float
/// (32 and 64-bit) data, plain or WAVE_FORMAT_EXTENSIBLE; other chunks are
/// skipped. Integers are scaled by 2^-(bits-1) (8-bit: (x - 128) / 128), so
/// full scale is [-1, 1). Throws `std::runtime_error` if the file cannot be
/// opened or is not a WAV file this reads.
Signal load_wav(const std::string& path);

/// The same samples in double, without the cast to float: for data stored as
/// 64-bit floats. `sample_rate`, when given, receives the file's rate.
Eigen::MatrixXd load_wav_double(const std::string& path, double* sample_rate = nullptr);

}  // namespace iceberg::io
