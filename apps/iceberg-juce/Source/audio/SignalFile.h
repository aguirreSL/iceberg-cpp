#pragma once

#include "iceberg_signal.h"

#include <juce_core/juce_core.h>

#include <string>

namespace iceberg::juce_app::audio {

/// A slot's signal file: WAV through `io::load_wav`, the reader the
/// library and its tests use; AIFF, FLAC, Ogg Vorbis and what the system
/// decodes (MP3 and AAC on macOS, MP3 and WMA on Windows) through JUCE.
/// Samples (frames x channels) and the file's rate. Throws
/// `std::runtime_error` when the file does not open as audio.
iceberg::Signal load_signal_file(const std::string& path);

/// File-chooser pattern for the formats `load_signal_file` reads.
juce::String signal_file_wildcard();

}  // namespace iceberg::juce_app::audio
