#include "IcebergAudioSource.h"

#include <algorithm>
#include <cstring>

namespace iceberg::juce_app::audio {

IcebergAudioSource::IcebergAudioSource(
    std::shared_ptr<const iceberg::Signal> buffer,
    int n_device_output_channels,
    std::vector<int> channel_map)
    : buffer_(std::move(buffer)),
      n_device_channels_(n_device_output_channels),
      channel_map_(std::move(channel_map)) {
    if (buffer_) {
        n_buffer_channels_ =
            static_cast<int>(buffer_->samples.cols());
        buffer_sample_rate_ = buffer_->sample_rate;
        total_length_ = buffer_->samples.rows();
    }
}

void IcebergAudioSource::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& info) {
    auto* dest = info.buffer;
    const int dest_start = info.startSample;
    const int n_to_render = info.numSamples;

    // No buffer or already past end -> output silence.
    if (!buffer_ || total_length_ <= 0) {
        info.clearActiveBufferRegion();
        return;
    }

    const juce::int64 pos = position_.load(std::memory_order_acquire);
    if (pos >= total_length_) {
        info.clearActiveBufferRegion();
        finished_.store(true, std::memory_order_release);
        return;
    }

    const juce::int64 remaining = total_length_ - pos;
    const int n_copy = static_cast<int>(
        std::min<juce::int64>(remaining, n_to_render));

    // Per-channel memcpy. Eigen MatrixXf is column-major (channel
    // contiguous); juce::AudioBuffer<float> is planar (channel
    // contiguous). Layouts match -> raw memcpy is correct.
    //
    // With `channel_map_` populated, device-channel `c` reads from
    // `buffer_->samples.col(channel_map_[c])`. Identity (empty map)
    // copies the first `min(buffer_ch, device_ch)` columns straight.
    const int n_channels_to_fill = channel_map_.empty()
        ? std::min(dest->getNumChannels(),
                   std::min(n_buffer_channels_, n_device_channels_))
        : std::min(dest->getNumChannels(),
                   static_cast<int>(channel_map_.size()));

    for (int c = 0; c < n_channels_to_fill; ++c) {
        const int src_col = channel_map_.empty()
            ? c : channel_map_[static_cast<size_t>(c)];
        if (src_col < 0 || src_col >= n_buffer_channels_) {
            dest->clear(c, dest_start, n_to_render);
            continue;
        }
        const float* src = buffer_->samples.col(src_col).data() +
                           static_cast<Eigen::Index>(pos);
        float* dst = dest->getWritePointer(c, dest_start);
        std::memcpy(dst, src, sizeof(float) * static_cast<size_t>(n_copy));
    }

    // Zero any remaining device channels beyond what we filled.
    for (int c = n_channels_to_fill; c < dest->getNumChannels(); ++c) {
        dest->clear(c, dest_start, n_to_render);
    }

    // If we rendered fewer than requested, pad zeros on every
    // channel for the tail.
    if (n_copy < n_to_render) {
        for (int c = 0; c < dest->getNumChannels(); ++c) {
            dest->clear(c, dest_start + n_copy, n_to_render - n_copy);
        }
        finished_.store(true, std::memory_order_release);
    }

    position_.store(pos + n_copy, std::memory_order_release);
}

void IcebergAudioSource::setNextReadPosition(juce::int64 new_position) {
    position_.store(new_position, std::memory_order_release);
    if (new_position < total_length_) {
        finished_.store(false, std::memory_order_release);
    }
}

juce::int64 IcebergAudioSource::getNextReadPosition() const {
    return position_.load(std::memory_order_acquire);
}

juce::int64 IcebergAudioSource::getTotalLength() const {
    return total_length_;
}

}  // namespace iceberg::juce_app::audio
