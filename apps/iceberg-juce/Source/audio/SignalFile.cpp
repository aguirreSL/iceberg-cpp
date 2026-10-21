#include "SignalFile.h"

#include "io/wav_loader.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <limits>
#include <memory>
#include <stdexcept>

namespace iceberg::juce_app::audio {

iceberg::Signal load_signal_file(const std::string& path) {
    const juce::File file(juce::String::fromUTF8(path.c_str()));
    if (file.hasFileExtension(".wav")) return iceberg::io::load_wav(path);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        throw std::runtime_error(path + ": not an audio file this app reads");
    }
    const juce::int64 frames = reader->lengthInSamples;
    const int channels = static_cast<int>(reader->numChannels);
    if (frames <= 0 || channels <= 0) throw std::runtime_error(path + ": no audio in the file");
    if (frames > std::numeric_limits<int>::max()) {
        throw std::runtime_error(path + ": too long to read");
    }
    juce::AudioBuffer<float> buffer(channels, static_cast<int>(frames));
    if (!reader->read(&buffer, 0, static_cast<int>(frames), 0, true, true)) {
        throw std::runtime_error(path + ": read failed");
    }

    iceberg::Signal signal;
    signal.sample_rate = reader->sampleRate;
    signal.samples.resize(static_cast<Eigen::Index>(frames), channels);
    for (int c = 0; c < channels; ++c) {
        signal.samples.col(c) =
            Eigen::Map<const Eigen::VectorXf>(buffer.getReadPointer(c), signal.samples.rows());
    }
    return signal;
}

juce::String signal_file_wildcard() {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    return formats.getWildcardForAllFormats();
}

}  // namespace iceberg::juce_app::audio
