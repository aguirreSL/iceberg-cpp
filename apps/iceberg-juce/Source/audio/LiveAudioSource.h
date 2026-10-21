#pragma once

#include "PlaybackSource.h"
#include "StereoFold.h"

#include "rendering/live.h"

#include <atomic>
#include <memory>
#include <vector>

namespace iceberg::juce_app::audio {

/// Plays a slot through `rendering::LiveEngine`, so the source
/// angle can change while it plays (`setAngle`, any thread).
///
/// Loudspeaker output: engine channel c to device output c. Stereo
/// (preparation mode): `StereoFold`.
///
/// `getNextAudioBlock` never allocates or locks: the fold-down scratch is
/// sized in `prepareToPlay` and blocks longer than it are done in pieces.
class LiveAudioSource : public PlaybackSource {
public:
    LiveAudioSource(std::shared_ptr<const iceberg::rendering::LiveRender> render,
                    double initial_angle_deg, bool stereo_fold_down, bool clockwise);

    void setAngle(double degrees) { engine_.set_angle(degrees); }

    void prepareToPlay(int samples_per_block, double sample_rate) override;
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

    // No seeking in live playback: the engine's convolution state runs forward.
    void setNextReadPosition(juce::int64) override {}
    juce::int64 getNextReadPosition() const override { return served_.load(); }
    juce::int64 getTotalLength() const override { return length_; }
    bool isLooping() const override { return false; }

    bool isFinished() const override { return finished_.load(); }
    double getBufferSampleRate() const override { return sample_rate_; }
    juce::int64 getBufferLengthSamples() const override { return length_; }

private:
    iceberg::rendering::LiveEngine engine_;
    const bool stereo_;
    const double sample_rate_;
    const juce::int64 length_;
    StereoFold fold_;
    std::vector<std::vector<float>> scratch_;     // fold-down: engine channels
    std::vector<float*> scratch_ptrs_;
    std::atomic<juce::int64> served_ {0};
    std::atomic<bool> finished_ {false};
};

}  // namespace iceberg::juce_app::audio
