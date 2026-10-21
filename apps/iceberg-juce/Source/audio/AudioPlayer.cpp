#include "AudioPlayer.h"

#include "experiment/slot_config.h"
#include "rendering/vbap_active_pair.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace iceberg::juce_app::audio {

AudioPlayer::AudioPlayer() {
    // Caller may restore device state from saved XML via
    // device_manager_.initialise(...) before / instead of this.
    // Default path: 0 inputs, up to kMaxOutputChannels outputs.
    const auto err = device_manager_.initialiseWithDefaultDevices(
        0, kMaxOutputChannels);
    if (err.isNotEmpty() && onError) {
        onError(("AudioPlayer init: " + err).toStdString());
    }
    device_manager_.addAudioCallback(&source_player_);
}

AudioPlayer::~AudioPlayer() {
    stopTimer();
    source_player_.setSource(nullptr);
    device_manager_.removeAudioCallback(&source_player_);
    releaseSource();
}

void AudioPlayer::setExperiment(iceberg::experiment::Experiment* exp) {
    experiment_ = exp;
}

void AudioPlayer::playSlot(int slot_index) {
    stop();
    playing_sequence_ = false;
    sequence_queue_.clear();
    startOrCoerceSlot(slot_index);
}

void AudioPlayer::playSequence() {
    stop();
    playing_sequence_ = true;
    sequence_queue_.clear();
    if (!experiment_) return;
    for (size_t i = 0; i < experiment_->slots.size(); ++i) {
        const auto& s = experiment_->slots[i];
        if (s.is_rendered && s.output_buffer) {
            sequence_queue_.push_back(static_cast<int>(i));
        }
    }
    if (sequence_queue_.empty()) {
        if (onError) onError("No rendered slots in experiment.");
        return;
    }
    advanceSequence();
}

void AudioPlayer::advanceSequence() {
    if (sequence_queue_.empty()) {
        playing_sequence_ = false;
        if (onSequenceFinished) onSequenceFinished();
        return;
    }
    const int next_idx = sequence_queue_.front();
    sequence_queue_.erase(sequence_queue_.begin());
    startOrCoerceSlot(next_idx);
}

void AudioPlayer::stop() {
    stopTimer();
    source_player_.setSource(nullptr);
    releaseSource();
    current_slot_index_   = -1;
    waiting_for_next_     = false;
    wait_until_ms_        = 0;
    playing_sequence_     = false;
    sequence_queue_.clear();
}

void AudioPlayer::next() {
    if (!waiting_for_next_) return;
    waiting_for_next_ = false;
    advanceSequence();
}

bool AudioPlayer::isPlaying() const {
    return current_source_ != nullptr && !waiting_for_next_;
}

double AudioPlayer::currentSlotPositionSec() const {
    if (!current_source_) return 0.0;
    const double fs = current_source_->getBufferSampleRate();
    if (fs <= 0.0) return 0.0;
    return static_cast<double>(current_source_->getNextReadPosition()) / fs;
}

double AudioPlayer::currentSlotDurationSec() const {
    if (!current_source_) return 0.0;
    const double fs = current_source_->getBufferSampleRate();
    if (fs <= 0.0) return 0.0;
    return static_cast<double>(current_source_->getBufferLengthSamples()) / fs;
}

void AudioPlayer::releaseSource() {
    live_source_ = nullptr;
    current_source_.reset();
}

void AudioPlayer::startOrCoerceSlot(int slot_index) {
    // Validate experiment + slot before any UI side effect.
    if (!experiment_) { if (onError) onError("No experiment bound."); return; }
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) {
        if (onError) onError("Slot index out of range.");
        return;
    }
    auto& slot = experiment_->slots[static_cast<size_t>(slot_index)];
    if (!slot.is_rendered || !slot.output_buffer) {
        if (onError) onError("Slot has no rendered buffer.");
        return;
    }
    coerceRateThen(slot.output_buffer->sample_rate,
                   [this, slot_index] { actuallyStartPlayback(slot_index); });
}

void AudioPlayer::coerceRateThen(double fs, std::function<void()> start) {
    auto* device = device_manager_.getCurrentAudioDevice();
    if (!device) {
        if (onError) onError("No audio device available.");
        return;
    }
    const double dev_fs = device->getCurrentSampleRate();
    if (std::abs(dev_fs - fs) <= 0.5) {
        start();
        return;
    }

    // Async dialog asks before changing system-wide device fs.
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        "Audio device sample rate mismatch",
        "Slot is at " + juce::String(fs, 0) +
            " Hz; device is at " + juce::String(dev_fs, 0) + " Hz.\n\n"
            "Switch device to " + juce::String(fs, 0) +
            " Hz? (This may affect other audio applications.)",
        "Switch", "Cancel", nullptr,
        juce::ModalCallbackFunction::create(
            [this, fs, start](int result) {
                if (result != 1) {
                    if (onError) {
                        onError("Re-render slot at device's native rate "
                                "or pick a compatible device.");
                    }
                    return;
                }
                auto setup = device_manager_.getAudioDeviceSetup();
                setup.sampleRate = fs;
                const auto err = device_manager_.setAudioDeviceSetup(setup, true);
                if (err.isNotEmpty()) {
                    if (onError) {
                        onError(("Device cannot run at " +
                                 juce::String(fs, 0) + " Hz: " + err)
                                .toStdString());
                    }
                    return;
                }
                auto* dev2 = device_manager_.getCurrentAudioDevice();
                if (!dev2 ||
                    std::abs(dev2->getCurrentSampleRate() - fs) > 0.5) {
                    if (onError) {
                        onError("Device did not honour the requested "
                                "sample rate.");
                    }
                    return;
                }
                start();
            }));
}

void AudioPlayer::playSlotLive(int slot_index) {
    stop();
    if (!experiment_) { if (onError) onError("No experiment bound."); return; }
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) {
        if (onError) onError("Slot index out of range.");
        return;
    }
    const auto& slot = experiment_->slots[static_cast<size_t>(slot_index)];
    if (!slot.live_render) {
        if (onError) onError("Slot is not prepared for live playback.");
        return;
    }
    coerceRateThen(slot.live_render->sample_rate,
                   [this, slot_index] { startLive(slot_index); });
}

void AudioPlayer::startLive(int slot_index) {
    if (!experiment_) return;
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) return;
    const auto& slot = experiment_->slots[static_cast<size_t>(slot_index)];
    const auto render = slot.live_render;
    if (!render) {
        if (onError) onError("Slot is not prepared for live playback.");
        return;
    }
    auto* device = device_manager_.getCurrentAudioDevice();
    const int dev_n_out = device ?
        device->getActiveOutputChannels().countNumberOfSetBits() : 0;
    const bool prep = experiment_->play_options.preparation_mode;
    if (prep && dev_n_out < 2) {
        if (onError) onError("Device has " + std::to_string(dev_n_out) +
                             " channel; preparation mode needs 2.");
        return;
    }
    if (!prep && dev_n_out < render->n_master) {
        if (onError) {
            onError("Audio device has " + std::to_string(dev_n_out) +
                    " output channels; live playback needs " +
                    std::to_string(render->n_master) +
                    " (one per loudspeaker). Turn Prep mode on for a stereo preview.");
        }
        return;
    }
    auto src = std::make_unique<LiveAudioSource>(
        render, slot.source_angle_deg, prep,
        slot.azimuth_convention == iceberg::rendering::AzimuthConvention::Clockwise);
    live_source_ = src.get();
    source_player_.setSource(src.get());
    current_source_     = std::move(src);
    current_slot_index_ = slot_index;
    waiting_for_next_   = false;
    startTimer(kPollIntervalMs);
    if (onSlotStarted) onSlotStarted(slot_index);
}

void AudioPlayer::playTrajectory(int slot_index) {
    stop();
    if (!experiment_) { if (onError) onError("No experiment bound."); return; }
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) {
        if (onError) onError("Slot index out of range.");
        return;
    }
    const auto& slot = experiment_->slots[static_cast<size_t>(slot_index)];
    if (!slot.trajectory_buffer || !slot.live_render) {
        if (onError) onError("Slot has no rendered trajectory.");
        return;
    }
    coerceRateThen(slot.trajectory_buffer->sample_rate,
                   [this, slot_index] { startTrajectory(slot_index); });
}

void AudioPlayer::startTrajectory(int slot_index) {
    if (!experiment_) return;
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) return;
    const auto& slot = experiment_->slots[static_cast<size_t>(slot_index)];
    if (!slot.trajectory_buffer || !slot.live_render) return;
    auto* device = device_manager_.getCurrentAudioDevice();
    const int dev_n_out = device ?
        device->getActiveOutputChannels().countNumberOfSetBits() : 0;
    const bool prep = experiment_->play_options.preparation_mode;
    std::shared_ptr<const iceberg::Signal> buffer = slot.trajectory_buffer;
    if (prep) {
        if (dev_n_out < 2) {
            if (onError) onError("Device has " + std::to_string(dev_n_out) +
                                 " channel; preparation mode needs 2.");
            return;
        }
        // The moving source folded down by loudspeaker position (message
        // thread; the audio thread only copies the result).
        const auto& r = *slot.live_render;
        auto folded = std::make_shared<iceberg::Signal>();
        folded->sample_rate = buffer->sample_rate;
        folded->samples = fold_to_stereo(
            buffer->samples,
            stereo_fold(r.ls_dir, r.active_ls_numbers, r.n_master,
                        slot.azimuth_convention == iceberg::rendering::AzimuthConvention::Clockwise));
        buffer = folded;
    } else if (dev_n_out < buffer->samples.cols()) {
        if (onError) {
            onError("Audio device has " + std::to_string(dev_n_out) +
                    " output channels; the trajectory needs " +
                    std::to_string(buffer->samples.cols()) +
                    ". Turn Prep mode on for a stereo preview.");
        }
        return;
    }
    auto src = std::make_unique<IcebergAudioSource>(
        buffer, prep ? 2 : dev_n_out, std::vector<int>{});
    source_player_.setSource(src.get());
    current_source_     = std::move(src);
    current_slot_index_ = slot_index;
    waiting_for_next_   = false;
    startTimer(kPollIntervalMs);
    if (onSlotStarted) onSlotStarted(slot_index);
}

void AudioPlayer::setLiveAngle(double degrees) {
    if (live_source_) live_source_->setAngle(degrees);
}

void AudioPlayer::actuallyStartPlayback(int slot_index) {
    if (!experiment_) return;
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) return;
    auto& slot = experiment_->slots[static_cast<size_t>(slot_index)];
    auto buffer_view =
        std::shared_ptr<const iceberg::Signal>(slot.output_buffer);
    if (!buffer_view) {
        if (onError) onError("Slot buffer was cleared.");
        return;
    }
    const int slot_n_ch = static_cast<int>(buffer_view->samples.cols());
    auto* device = device_manager_.getCurrentAudioDevice();
    const int dev_n_out = device ?
        device->getActiveOutputChannels().countNumberOfSetBits() : 0;

    const bool prep = experiment_->play_options.preparation_mode;

    int n_device_use = dev_n_out;
    std::vector<int> channel_map;

    if (prep) {
        // Stereo preview: device must have at least 2 outputs; only
        // first 2 are used regardless of total device channels.
        if (dev_n_out < 2) {
            if (onError) {
                onError("Device has " + std::to_string(dev_n_out) +
                        " channel; preparation mode needs 2.");
            }
            return;
        }
        n_device_use = 2;

        // Compute the active VBAP pair from the slot's layout +
        // source angle. Layout reload here is acceptable cost
        // (cached implicitly by OS; ~ms for small JSON). slot_layout
        // is the layout the render used (file, or the 4 default
        // loudspeakers), so the pair heard is the pair on the pad.
        try {
            const auto cfg = iceberg::experiment::slot_layout(slot);
            const auto pair =
                iceberg::rendering::vbap_active_pair(
                    slot.source_angle_deg, cfg.ls_dir);
            // The slot output buffer is sized to `ls_array.size()`
            // (master ring), but the VBAP pair indices come from
            // `ls_dir` rows (active subset). Map back via
            // `active_ls_numbers` (1-based -> 0-based).
            const auto& act = cfg.active_ls_numbers;
            if (pair.s1_idx < 0 || pair.s1_idx >= act.size() ||
                pair.s2_idx < 0 || pair.s2_idx >= act.size()) {
                if (onError) {
                    onError("VBAP pair index out of range for layout.");
                }
                return;
            }
            const int s1_master =
                static_cast<int>(act(pair.s1_idx)) - 1;
            const int s2_master =
                static_cast<int>(act(pair.s2_idx)) - 1;
            if (s1_master < 0 || s1_master >= slot_n_ch ||
                s2_master < 0 || s2_master >= slot_n_ch) {
                if (onError) {
                    onError("VBAP pair master index out of range "
                            "for slot buffer.");
                }
                return;
            }
            // The pair runs from s1 to s2 in increasing azimuth
            // (findLsPairs order). Counter-clockwise (v1.0.0's
            // default) that is towards the listener's left, so s2 is
            // the left loudspeaker; clockwise, s1 is.
            const bool counter_clockwise =
                slot.azimuth_convention ==
                iceberg::rendering::AzimuthConvention::CounterClockwise;
            if (counter_clockwise) channel_map = {s2_master, s1_master};
            else                   channel_map = {s1_master, s2_master};
        } catch (const std::exception& e) {
            if (onError) {
                onError(std::string("Preparation mode setup failed: ") +
                        e.what());
            }
            return;
        }
    } else {
        if (dev_n_out < slot_n_ch) {
            if (onError) {
                onError("Audio device has " + std::to_string(dev_n_out) +
                        " output channels; slot needs " +
                        std::to_string(slot_n_ch) +
                        ". Connect a multichannel interface or reduce "
                        "loudspeaker count.");
            }
            return;
        }
    }

    auto src = std::make_unique<IcebergAudioSource>(
        buffer_view, n_device_use, std::move(channel_map));
    source_player_.setSource(src.get());
    current_source_     = std::move(src);
    current_slot_index_ = slot_index;
    waiting_for_next_   = false;

    startTimer(kPollIntervalMs);
    if (onSlotStarted) onSlotStarted(slot_index);
}

void AudioPlayer::timerCallback() {
    // Wait-deadline branch: in `wait_seconds > 0` after a slot
    // finished, we sit here until the deadline elapses.
    if (wait_until_ms_ > 0) {
        if (juce::Time::currentTimeMillis() >= wait_until_ms_) {
            wait_until_ms_ = 0;
            advanceSequence();
        }
        return;
    }

    if (!current_source_) {
        stopTimer();
        return;
    }

    if (!current_source_->isFinished()) return;  // still playing

    // Slot finished. Tear down its source.
    const int finished_idx = current_slot_index_;
    source_player_.setSource(nullptr);
    releaseSource();
    current_slot_index_ = -1;

    if (onSlotFinished) onSlotFinished(finished_idx);

    if (!playing_sequence_ || sequence_queue_.empty()) {
        stopTimer();
        playing_sequence_ = false;
        if (onSequenceFinished) onSequenceFinished();
        return;
    }

    // Sequence transition decision.
    const auto& opts = experiment_ ? experiment_->play_options
                                   : iceberg::experiment::PlayOptions{};
    if (!opts.wait_between_slots) {
        advanceSequence();
        return;
    }
    if (opts.wait_seconds > 0.0) {
        wait_until_ms_ = juce::Time::currentTimeMillis() +
                         static_cast<juce::int64>(opts.wait_seconds * 1000.0);
        return;
    }
    // wait_between=ON + wait_seconds=0: manual continue.
    waiting_for_next_ = true;
    if (onWaitingForNext) onWaitingForNext();
    stopTimer();
}

}  // namespace iceberg::juce_app::audio
