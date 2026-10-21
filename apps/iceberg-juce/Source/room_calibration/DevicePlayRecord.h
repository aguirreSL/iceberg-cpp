#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <Eigen/Dense>

#include <atomic>
#include <stdexcept>
#include <vector>

namespace iceberg::juce_app::room_calibration {

/// Thrown by `playRecord` when `cancel()` was called.
class Cancelled : public std::runtime_error {
public:
    Cancelled() : std::runtime_error("Cancelled.") {}
};

/// The sound card as `iceberg::calibration::PlayRecord`: plays a page on
/// output channels and records one input channel, sample-aligned from the
/// same device callback, as MATLAB's playrec does (the recording has the
/// page's length; the device latency shifts it, which the measurements
/// tolerate, as in MATLAB).
///
/// `playRecord` blocks the calling (worker) thread until the page has
/// played, polling a flag the audio thread sets. The audio thread only
/// try-locks the job, so it never waits, never allocates and touches no
/// mutex. Registers itself with `device_manager` in the
/// ctor and removes itself in the dtor.
class DevicePlayRecord : private juce::AudioIODeviceCallback {
public:
    explicit DevicePlayRecord(juce::AudioDeviceManager& device_manager);
    ~DevicePlayRecord() override;

    /// Worker thread only. Channels are 0-based hardware channels.
    /// Throws `std::runtime_error` with an instruction for the user when
    /// no device is open, the rate is not `fs`, a channel is not active,
    /// or the device stops delivering audio; `Cancelled` after `cancel()`.
    Eigen::VectorXd playRecord(const Eigen::MatrixXd& page, const std::vector<int>& play_channels,
                               int rec_channel, double fs);

    /// Any thread: makes the running and every later `playRecord` throw
    /// `Cancelled` until `resetCancel()`.
    void cancel() { cancel_.store(true); }
    void resetCancel() { cancel_.store(false); }

private:
    void audioDeviceIOCallbackWithContext(const float* const* input, int num_inputs,
                                          float* const* output, int num_outputs, int num_samples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    juce::AudioDeviceManager& device_manager_;
    juce::SpinLock job_lock_;
    // The job, written by the worker under job_lock_, read by the audio
    // thread under a try-lock while `active_`.
    bool active_ = false;
    const Eigen::MatrixXd* page_ = nullptr;
    std::vector<int> page_outputs_;   // device-buffer index per page column
    int input_index_ = -1;            // device-buffer index of the recorded channel
    Eigen::VectorXd recording_;
    Eigen::Index position_ = 0;
    std::atomic<bool> done_{false};
    std::atomic<bool> cancel_{false};
};

}  // namespace iceberg::juce_app::room_calibration
