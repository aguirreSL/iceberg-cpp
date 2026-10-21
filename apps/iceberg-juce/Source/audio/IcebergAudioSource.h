#pragma once

#include "PlaybackSource.h"
#include "iceberg_signal.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace iceberg::juce_app::audio {

/// Realtime-safe audio source that streams a pre-rendered slot
/// buffer to the device.
///
/// Contract:
///   * `buffer_` is set in the constructor and NEVER swapped during
///     this source's lifetime. To play a different slot, the owner
///     constructs a new `IcebergAudioSource` on the message thread.
///   * `getNextAudioBlock` is realtime-safe: no allocation, no mutex,
///     no shared_ptr swap. Just `memcpy` per channel.
///   * `position_` and `finished_` are `std::atomic` so the message
///     thread can poll progress / completion without races.
class IcebergAudioSource : public PlaybackSource {
public:
    /// `channel_map` (optional): for each device-output channel `c`,
    /// which buffer column to read. Empty = identity (copy first
    /// `min(buffer_ch, device_ch)` columns straight). Sized to
    /// `n_device_output_channels` when populated. Used for
    /// preparation mode (`{s1_idx, s2_idx}` -> stereo L/R).
    IcebergAudioSource(std::shared_ptr<const iceberg::Signal> buffer,
                       int n_device_output_channels,
                       std::vector<int> channel_map = {});
    ~IcebergAudioSource() override = default;

    // juce::AudioSource
    void prepareToPlay(int /*samples_per_block*/,
                       double /*sample_rate*/) override {}
    void releaseResources() override {}
    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& info) override;

    // juce::PositionableAudioSource
    void  setNextReadPosition(juce::int64 new_position) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;
    bool  isLooping() const override { return false; }

    /// True after the audio thread has reached the end of `buffer_`
    /// and is now padding zeros. Polled on the message thread to
    /// trigger sequence advance.
    bool isFinished() const override { return finished_.load(); }

    double getBufferSampleRate() const override { return buffer_sample_rate_; }
    juce::int64 getBufferLengthSamples() const override {
        return total_length_;
    }

private:
    std::shared_ptr<const iceberg::Signal> buffer_;  // immutable
    int n_buffer_channels_  = 0;
    int n_device_channels_  = 0;
    double buffer_sample_rate_ = 0.0;
    juce::int64 total_length_  = 0;
    std::vector<int> channel_map_;        // immutable post-ctor
    std::atomic<juce::int64> position_ {0};
    std::atomic<bool>        finished_ {false};
};

}  // namespace iceberg::juce_app::audio
