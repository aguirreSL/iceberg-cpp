#pragma once

#include "IcebergAudioSource.h"
#include "LiveAudioSource.h"

#include "experiment/experiment.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iceberg::juce_app::audio {

/// Owns the JUCE audio device + a single in-flight audio source.
/// Single-source-at-a-time policy: `playSlot` / `playSequence`
/// constructs a fresh `IcebergAudioSource` on the message thread,
/// hands it to `juce::AudioSourcePlayer`, and the player owns the
/// realtime stream until `stop()` or sequence completion.
///
/// Sequence transitions are driven by a `juce::Timer` polling
/// `IcebergAudioSource::isFinished()` at 50 ms - the audio thread
/// itself never invokes callbacks.
class AudioPlayer : private juce::Timer {
public:
    AudioPlayer();
    ~AudioPlayer() override;

    void setExperiment(iceberg::experiment::Experiment* exp);

    /// Accessor for the Audio Settings dialog
    /// (`juce::AudioDeviceSelectorComponent` constructor needs the
    /// device manager). MainComponent is the only intended caller.
    juce::AudioDeviceManager& getDeviceManager() { return device_manager_; }

    // Single slot. Errors via onError if not rendered, fs mismatch,
    // or insufficient device channels.
    void playSlot(int slot_index);

    // Plays every rendered slot in `experiment_->slots` order,
    // honouring `play_options.wait_between_slots` /
    // `play_options.wait_seconds`.
    void playSequence();

    // Plays the slot through the live engine (its `live_render`
    // must be prepared), the source angle following `setLiveAngle`.
    // Loudspeaker output needs one device output per loudspeaker;
    // preparation mode folds it down to stereo. Errors via onError.
    void playSlotLive(int slot_index);
    void setLiveAngle(double degrees);
    bool isLive() const { return live_source_ != nullptr; }

    // Plays the slot's `trajectory_buffer` (rendered along its
    // path). Preparation mode folds it down to stereo like the live path.
    void playTrajectory(int slot_index);

    // Halt playback and reset position.
    void stop();

    // For wait_between=ON + wait_seconds=0 (manual continue):
    // operator clicks Next to advance. No-op otherwise.
    void next();

    // ---- Query (UI thread) ----
    bool        isPlaying() const;
    bool        isWaitingForNext() const { return waiting_for_next_; }
    int         currentSlotIndex() const { return current_slot_index_; }
    double      currentSlotPositionSec() const;
    double      currentSlotDurationSec() const;

    // ---- Callbacks (always on JUCE message thread) ----
    std::function<void(int /*slot_index*/)> onSlotStarted;
    std::function<void(int /*slot_index*/)> onSlotFinished;
    std::function<void()>                   onSequenceFinished;
    std::function<void()>                   onWaitingForNext;
    std::function<void(const std::string&)> onError;

private:
    void timerCallback() override;
    void startOrCoerceSlot(int slot_index);
    void actuallyStartPlayback(int slot_index);  // assumes device fs ok
    void startLive(int slot_index);              // assumes device fs ok
    void startTrajectory(int slot_index);        // assumes device fs ok
    // Runs `start` once the device is at `fs`, asking before switching it.
    void coerceRateThen(double fs, std::function<void()> start);
    void advanceSequence();
    void releaseSource();

    iceberg::experiment::Experiment* experiment_ = nullptr;

    juce::AudioDeviceManager device_manager_;
    juce::AudioSourcePlayer  source_player_;
    std::unique_ptr<PlaybackSource> current_source_;
    LiveAudioSource* live_source_ = nullptr;   // current_source_ when live

    std::vector<int> sequence_queue_;     // slot indices remaining
    int  current_slot_index_  = -1;       // -1 = nothing playing
    bool waiting_for_next_    = false;
    bool playing_sequence_    = false;

    // Pending-wait timer state. We run a single juce::Timer at 50 ms
    // and flip flags / decrement deadline.
    juce::int64 wait_until_ms_ = 0;       // 0 = no wait pending

    static constexpr int kMaxOutputChannels = 32;
    static constexpr int kPollIntervalMs    = 50;
};

}  // namespace iceberg::juce_app::audio
