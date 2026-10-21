#include "DevicePlayRecord.h"

#include <cmath>
#include <string>

namespace iceberg::juce_app::room_calibration {

namespace {

// Position of hardware channel `channel` in the callback's buffer array,
// which holds the active channels only, in ascending order; -1 if inactive.
int bufferIndex(const juce::BigInteger& active, int channel) {
    if (channel < 0 || !active[channel]) return -1;
    int index = 0;
    for (int c = 0; c < channel; ++c) {
        if (active[c]) ++index;
    }
    return index;
}

}  // namespace

DevicePlayRecord::DevicePlayRecord(juce::AudioDeviceManager& device_manager)
    : device_manager_(device_manager) {
    device_manager_.addAudioCallback(this);
}

DevicePlayRecord::~DevicePlayRecord() {
    device_manager_.removeAudioCallback(this);
}

Eigen::VectorXd DevicePlayRecord::playRecord(const Eigen::MatrixXd& page,
                                             const std::vector<int>& play_channels,
                                             int rec_channel, double fs) {
    if (cancel_.load()) throw Cancelled();
    auto* device = device_manager_.getCurrentAudioDevice();
    if (device == nullptr) {
        throw std::runtime_error("No audio device is open: choose one in Audio setup.");
    }
    if (std::abs(device->getCurrentSampleRate() - fs) > 0.5) {
        throw std::runtime_error(
            "The device runs at " + std::to_string(static_cast<int>(device->getCurrentSampleRate())) +
            " Hz; the calibration measures at " + std::to_string(static_cast<int>(fs)) +
            " Hz. Set the sample rate in Audio setup.");
    }
    const juce::BigInteger outs = device->getActiveOutputChannels();
    const juce::BigInteger ins = device->getActiveInputChannels();
    std::vector<int> page_outputs;
    for (const int c : play_channels) {
        const int index = bufferIndex(outs, c);
        if (index < 0) {
            throw std::runtime_error("Output channel " + std::to_string(c + 1) +
                                     " is not active: enable it in Audio setup.");
        }
        page_outputs.push_back(index);
    }
    const int input_index = bufferIndex(ins, rec_channel);
    if (input_index < 0) {
        throw std::runtime_error("Input channel " + std::to_string(rec_channel + 1) +
                                 " is not active: enable it in Audio setup.");
    }

    Eigen::VectorXd recording = Eigen::VectorXd::Zero(page.rows());
    {
        const juce::SpinLock::ScopedLockType lock(job_lock_);
        page_ = &page;
        page_outputs_ = std::move(page_outputs);
        input_index_ = input_index;
        recording_.swap(recording);
        position_ = 0;
        done_.store(false);
        active_ = true;
    }

    // The page's duration plus a margin for the device to start.
    const double seconds = static_cast<double>(page.rows()) / fs + 5.0;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    bool finished = false;
    while (!(finished = done_.load())) {
        if (cancel_.load() || juce::Time::getMillisecondCounterHiRes() > deadline) break;
        juce::Thread::sleep(20);
    }

    {
        const juce::SpinLock::ScopedLockType lock(job_lock_);
        active_ = false;
        page_ = nullptr;
        recording_.swap(recording);
    }
    if (!finished) {
        if (cancel_.load()) throw Cancelled();
        throw std::runtime_error("The audio device stopped delivering audio: check it in Audio "
                                 "setup.");
    }
    return recording;
}

void DevicePlayRecord::audioDeviceIOCallbackWithContext(const float* const* input, int num_inputs,
                                                        float* const* output, int num_outputs,
                                                        int num_samples,
                                                        const juce::AudioIODeviceCallbackContext&) {
    for (int c = 0; c < num_outputs; ++c) {
        if (output[c] != nullptr) juce::FloatVectorOperations::clear(output[c], num_samples);
    }
    const juce::SpinLock::ScopedTryLockType lock(job_lock_);
    if (!lock.isLocked() || !active_ || page_ == nullptr) return;

    const Eigen::Index n = page_->rows();
    int i = 0;
    for (; i < num_samples && position_ < n; ++i, ++position_) {
        for (size_t k = 0; k < page_outputs_.size(); ++k) {
            const int o = page_outputs_[k];
            if (o < num_outputs && output[o] != nullptr) {
                output[o][i] = static_cast<float>((*page_)(position_, static_cast<Eigen::Index>(k)));
            }
        }
        if (input_index_ < num_inputs && input[input_index_] != nullptr) {
            recording_(position_) = static_cast<double>(input[input_index_][i]);
        }
    }
    if (position_ >= n) {
        active_ = false;
        done_.store(true);
    }
}

}  // namespace iceberg::juce_app::room_calibration
