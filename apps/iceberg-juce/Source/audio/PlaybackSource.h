#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace iceberg::juce_app::audio {

/// What `AudioPlayer` needs from whatever it plays: a slot's pre-rendered
/// buffer (`IcebergAudioSource`) or the live engine (`LiveAudioSource`).
/// The three queries are read on the message thread while the audio thread
/// plays, so implementations back them with atomics or immutable state.
class PlaybackSource : public juce::PositionableAudioSource {
public:
    /// True once the audio thread has played everything.
    virtual bool isFinished() const = 0;
    virtual double getBufferSampleRate() const = 0;
    virtual juce::int64 getBufferLengthSamples() const = 0;
};

}  // namespace iceberg::juce_app::audio
