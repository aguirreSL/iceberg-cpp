#pragma once

#include "iceberg_signal.h"

#include <string>

namespace iceberg::io {

/// Write a `Signal` to a 32-bit float WAV file.
///
/// Always float32 (IEEE float WAV) to preserve magnitudes
/// outside `[-1, 1]` — pipeline output (e.g. `iceberg()` end-to-end)
/// can have peak abs > 1.0 by design. int16 / int24 would clip
/// silently.
///
/// Throws `std::runtime_error` on file I/O failure.
void write_wav(const std::string& path, const Signal& signal);

}  // namespace iceberg::io
