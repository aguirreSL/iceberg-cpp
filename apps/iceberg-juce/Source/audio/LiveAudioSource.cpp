#include "LiveAudioSource.h"

#include <algorithm>

namespace iceberg::juce_app::audio {

LiveAudioSource::LiveAudioSource(std::shared_ptr<const iceberg::rendering::LiveRender> render,
                                 double initial_angle_deg, bool stereo_fold_down, bool clockwise)
    : engine_(render, initial_angle_deg),
      stereo_(stereo_fold_down),
      sample_rate_(render->sample_rate),
      length_(static_cast<juce::int64>(render->length)),
      fold_(stereo_fold(render->ls_dir, render->active_ls_numbers, render->n_master, clockwise)) {
    const auto n = static_cast<size_t>(render->n_master);
    scratch_.assign(n, {});
    scratch_ptrs_.assign(n, nullptr);
}

void LiveAudioSource::prepareToPlay(int samples_per_block, double /*sample_rate*/) {
    const auto frames = static_cast<size_t>(std::max(samples_per_block, 64));
    for (size_t c = 0; c < scratch_.size(); ++c) {
        scratch_[c].assign(frames, 0.0f);
        scratch_ptrs_[c] = scratch_[c].data();
    }
}

void LiveAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    auto* dest = info.buffer;
    const int n_dest = dest->getNumChannels();
    const int frames = info.numSamples;
    if (!stereo_) {
        // Engine channel c to device channel c; the engine zeroes channels
        // past its own.
        float* ptrs[64];
        const int n = std::min(n_dest, 64);
        for (int c = 0; c < n; ++c) ptrs[c] = dest->getWritePointer(c, info.startSample);
        engine_.render(ptrs, n, frames);
        for (int c = n; c < n_dest; ++c) dest->clear(c, info.startSample, frames);
    } else {
        info.clearActiveBufferRegion();
        const int chunk_max = scratch_.empty() ? 0 : static_cast<int>(scratch_[0].size());
        const int n_engine = static_cast<int>(scratch_.size());
        for (int done = 0; done < frames && chunk_max > 0;) {
            const int chunk = std::min(chunk_max, frames - done);
            engine_.render(scratch_ptrs_.data(), n_engine, chunk);
            for (int out = 0; out < std::min(n_dest, 2); ++out) {
                float* d = dest->getWritePointer(out, info.startSample + done);
                const std::vector<float>& gain = out == 0 ? fold_.left : fold_.right;
                for (int c = 0; c < n_engine; ++c) {
                    const float g = gain[static_cast<size_t>(c)];
                    if (g == 0.0f) continue;
                    const float* s = scratch_ptrs_[static_cast<size_t>(c)];
                    for (int i = 0; i < chunk; ++i) d[i] += g * s[i];
                }
            }
            done += chunk;
        }
    }
    const juce::int64 served = served_.load() + frames;
    served_.store(std::min(served, length_));
    if (served >= length_) finished_.store(true);
}

}  // namespace iceberg::juce_app::audio
