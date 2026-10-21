#include "RenderOrchestrator.h"
#include "audio/SignalFile.h"

#include "experiment/slot_config.h"
#include "rendering/live.h"
#include "io/wav_loader.h"
#include "native/center_time.h"
#include "rendering/iceberg.h"

#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace iceberg::juce_app {

namespace {

bool file_exists(const std::string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

}  // namespace

RenderOrchestrator::RenderOrchestrator() {
    worker_ = std::thread([this] { workerLoop(); });
}

RenderOrchestrator::~RenderOrchestrator() {
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        shutdown_.store(true);
        cancel_requested_.store(true);
        std::queue<int> empty;
        std::swap(render_queue_, empty);
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void RenderOrchestrator::setExperiment(iceberg::experiment::Experiment* exp) {
    experiment_ = exp;
    resyncSlotCount();
}

void RenderOrchestrator::resyncSlotCount() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const size_t n = experiment_ ? experiment_->slots.size() : 0u;
    slot_states_.resize(n, SlotRenderState::Idle);
    // Re-derive states from slot fields so reorder/duplicate keep
    // the visible badge consistent.
    for (size_t i = 0; i < n; ++i) {
        const auto& s = experiment_->slots[i];
        if (s.is_rendered && !s.is_stale)      slot_states_[i] = SlotRenderState::Rendered;
        else if (s.is_rendered && s.is_stale)  slot_states_[i] = SlotRenderState::Stale;
        else                                   slot_states_[i] = SlotRenderState::Idle;
    }
}

void RenderOrchestrator::renderSlot(int slot_index) {
    if (!experiment_) return;
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) return;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        render_queue_.push(slot_index);
    }
    postStateChange(slot_index, SlotRenderState::Queued);
    queue_cv_.notify_one();
}

void RenderOrchestrator::renderAllStale() {
    if (!experiment_) return;
    for (size_t i = 0; i < experiment_->slots.size(); ++i) {
        const auto& s = experiment_->slots[i];
        if (s.is_stale) renderSlot(static_cast<int>(i));
    }
}

void RenderOrchestrator::cancelAll() {
    std::queue<int> drained;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        std::swap(render_queue_, drained);
        cancel_requested_.store(true);
    }
    queue_cv_.notify_all();
    while (!drained.empty()) {
        postStateChange(drained.front(), SlotRenderState::Cancelled);
        drained.pop();
    }
    // The in-flight render is allowed to finish (mid-render cancel is
    // not supported: the lib has no cancel token).
}

SlotRenderState RenderOrchestrator::getSlotState(int slot_index) const {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(slot_states_.size())) {
        return SlotRenderState::Idle;
    }
    return slot_states_[static_cast<size_t>(slot_index)];
}

bool RenderOrchestrator::isAnythingRendering() const {
    if (in_flight_index_.load() >= 0) return true;
    std::lock_guard<std::mutex> lk(queue_mutex_);
    return !render_queue_.empty();
}

int RenderOrchestrator::getStaleCount() const {
    if (!experiment_) return 0;
    int n = 0;
    for (const auto& s : experiment_->slots) if (s.is_stale) ++n;
    return n;
}

void RenderOrchestrator::clearSlotBuffer(int slot_index) {
    if (!experiment_) return;
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) return;
    auto& s = experiment_->slots[static_cast<size_t>(slot_index)];
    s.output_buffer.reset();
    s.is_rendered = false;
    s.is_stale = true;
    s.cached_metrics.reset();
    s.render_notices.clear();
    s.trajectory_buffer.reset();
    postStateChange(slot_index, SlotRenderState::Idle);
}

size_t RenderOrchestrator::getCurrentBufferRamBytes() const {
    if (!experiment_) return 0;
    size_t total = 0;
    for (const auto& s : experiment_->slots) {
        if (s.output_buffer) {
            total += static_cast<size_t>(s.output_buffer->samples.size()) *
                     sizeof(float);
        }
        // The trajectory render and the live preparation.
        if (s.trajectory_buffer) {
            total += static_cast<size_t>(s.trajectory_buffer->samples.size()) * sizeof(float);
        }
        if (s.live_render) {
            total += static_cast<size_t>(s.live_render->direct.size() +
                                         s.live_render->late_dry.size()) * sizeof(float);
        }
    }
    return total;
}

void RenderOrchestrator::setMemoryBudgetGB(double gb) {
    memory_budget_gb_ = gb;
}

size_t RenderOrchestrator::estimateRenderBytes(int slot_index) const {
    if (!experiment_) return 0;
    if (slot_index < 0 ||
        slot_index >= static_cast<int>(experiment_->slots.size())) return 0;
    const auto& s = experiment_->slots[static_cast<size_t>(slot_index)];
    if (s.signal_path.empty() || s.ir_path.empty()) return 0;
    try {
        // Full reads: the library has no header-only path. For a
        // 3 GB estimate the cost is accepted.
        const auto signal = audio::load_signal_file(s.signal_path);
        const auto ir = iceberg::io::load_wav(s.ir_path);
        const auto cfg = iceberg::experiment::slot_layout(s);
        const auto n_ls = static_cast<size_t>(cfg.ls_array.size());
        // The signal is resampled to the IR rate before the convolution.
        const double ratio = signal.sample_rate > 0.0
                                 ? ir.sample_rate / signal.sample_rate : 1.0;
        const size_t n_out = static_cast<size_t>(
            std::ceil(static_cast<double>(signal.samples.rows()) * ratio)) +
            static_cast<size_t>(ir.samples.rows());
        return n_out * n_ls * sizeof(float);
    } catch (...) {
        return 0;
    }
}

std::optional<double> RenderOrchestrator::getExperimentSampleRate(
    const iceberg::experiment::Experiment& exp, int skip_slot) {
    // A render comes out at its IR's rate (Iceberg v1.0.0 resamples the
    // signal to it), so the IRs alone fix the experiment's rate.
    for (size_t i = 0; i < exp.slots.size(); ++i) {
        if (static_cast<int>(i) == skip_slot) continue;
        const auto& s = exp.slots[i];
        if (s.ir_path.empty() || !file_exists(s.ir_path)) continue;
        try {
            const auto ir = iceberg::io::load_wav(s.ir_path);
            if (ir.sample_rate > 0.0) return ir.sample_rate;
        } catch (...) {
            // Skip unreadable files; another slot may resolve.
        }
    }
    return std::nullopt;
}

bool RenderOrchestrator::validateSlotPaths(
    const iceberg::experiment::Slot& slot, std::string& err_out) const {
    if (slot.ir_path.empty())     { err_out = "IR path empty"; return false; }
    if (slot.signal_path.empty()) { err_out = "Signal path empty"; return false; }
    if (!file_exists(slot.ir_path))     { err_out = "IR file not found: " + slot.ir_path; return false; }
    if (!file_exists(slot.signal_path)) { err_out = "Signal file not found: " + slot.signal_path; return false; }
    // An empty layout path means the 4 default loudspeakers.
    if (!slot.layout_path.empty() && !file_exists(slot.layout_path)) {
        err_out = "Layout file not found: " + slot.layout_path;
        return false;
    }
    if (slot.cal_path && !file_exists(*slot.cal_path)) {
        err_out = "Calibration file not found: " + *slot.cal_path;
        return false;
    }
    return true;
}

void RenderOrchestrator::workerLoop() {
    while (true) {
        int next_idx = -1;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this] {
                return shutdown_.load() || !render_queue_.empty();
            });
            if (shutdown_.load()) return;
            if (render_queue_.empty()) continue;
            next_idx = render_queue_.front();
            render_queue_.pop();
        }
        if (next_idx < 0) continue;

        // Reset cancel flag at the start of each slot so cancelAll
        // during this render only affects the queue, not this slot.
        // (The cancel that drained the queue already fired its
        // Cancelled state changes for the queued items.)
        cancel_requested_.store(false);

        in_flight_index_.store(next_idx);
        postStateChange(next_idx, SlotRenderState::Rendering);

        if (onSlotProgress) {
            const int idx = next_idx;
            juce::MessageManager::callAsync([cb = onSlotProgress, idx] {
                cb(idx, 0.0);
            });
        }

        renderOne(next_idx);

        if (onSlotProgress) {
            const int idx = next_idx;
            juce::MessageManager::callAsync([cb = onSlotProgress, idx] {
                cb(idx, 1.0);
            });
        }
        in_flight_index_.store(-1);
    }
}

void RenderOrchestrator::renderOne(int slot_index) {
    if (!experiment_) return;

    iceberg::experiment::Slot* slot_ptr = nullptr;
    {
        // Snapshot the slot fields; do NOT keep the pointer past the
        // load step. Vector mutation on the UI thread can invalidate.
        if (slot_index < 0 ||
            slot_index >= static_cast<int>(experiment_->slots.size())) return;
        slot_ptr = &experiment_->slots[static_cast<size_t>(slot_index)];
    }
    iceberg::experiment::Slot snap = *slot_ptr;  // copy of paths/params

    auto post_error = [this, slot_index](const std::string& msg) {
        const int idx = slot_index;
        const std::string m = msg;
        juce::MessageManager::callAsync([this, idx, m] {
            if (!experiment_) return;
            if (idx < 0 || idx >= static_cast<int>(experiment_->slots.size())) return;
            auto& s = experiment_->slots[static_cast<size_t>(idx)];
            s.error_message = m;
            s.render_notices.clear();
            s.is_rendered = false;
            // is_stale stays true (default for fresh / failed slots)
        });
        postStateChange(slot_index, SlotRenderState::Error);
    };

    std::string err;
    if (!validateSlotPaths(snap, err)) {
        post_error(err);
        return;
    }

    iceberg::Signal signal_buf;
    iceberg::Signal ir_buf;
    iceberg::rendering::ConfigSetup config;
    try {
        signal_buf = audio::load_signal_file(snap.signal_path);
        ir_buf = iceberg::io::load_wav(snap.ir_path);
        // The layout (file, or the 4 default loudspeakers), the
        // calibration (neutral in full-scale mode; the pre-render check in
        // MainComponent catches a missing cal file, this enforces it for
        // Render All Stale) and the v1.0.0 options, all from the slot.
        // The pad and the preparation mode read the same slot_layout.
        config = iceberg::experiment::slot_render_config(snap);
    } catch (const std::exception& e) {
        post_error(std::string("load failed: ") + e.what());
        return;
    }

    // Browse Signal already warns the user if
    // the file is multichannel + asks consent. Here we honour that
    // consent by silently dropping to channel 0. If the user bypassed
    // the GUI flow (e.g. loaded an experiment with a multichannel
    // signal_path), we still take col(0) without erroring.
    if (signal_buf.samples.cols() > 1) {
        iceberg::Signal mono;
        mono.sample_rate = signal_buf.sample_rate;
        mono.samples = signal_buf.samples.col(0);
        signal_buf = std::move(mono);
    }
    // The IR's channel count ((N+1)^2, order N reduced to first order) and
    // a signal at another rate (resampled to the IR's) are handled by
    // iceberg() itself, which reports what it did in `notices`.
    iceberg::Signal out_signal;
    std::vector<iceberg::rendering::RenderNotice> notices;
    try {
        out_signal = iceberg::rendering::iceberg(
            signal_buf, ir_buf,
            snap.source_angle_deg,
            snap.level_db,
            config,
            &notices);
    } catch (const std::exception& e) {
        post_error(std::string("render failed: ") + e.what());
        return;
    }

    // Compute peak / rms / center-time on the worker thread. Cheap.
    double peak = 0.0;
    double sum_sq = 0.0;
    const auto& samples = out_signal.samples;
    for (Eigen::Index k = 0; k < samples.size(); ++k) {
        const float v = samples.data()[k];
        if (std::abs(v) > peak) peak = std::abs(v);
        sum_sq += static_cast<double>(v) * v;
    }
    const double rms = std::sqrt(
        sum_sq / static_cast<double>(std::max<Eigen::Index>(1, samples.size())));
    // The split iceberg() used, as iceberg_core picks it: the IR's centre
    // time (W is channel 0 in FuMa and AmbiX alike, and the scale does not
    // move it), or 10 ms in the anechoic case and where it cannot be computed.
    double c_time = 0.01;
    if (!snap.anechoic_special_case) {
        try {
            c_time = iceberg::native::center_time(ir_buf);
        } catch (const std::exception&) {
        }
    }

    const int slot_index_capture = slot_index;
    auto buf_shared = std::make_shared<iceberg::Signal>(std::move(out_signal));
    const int n_samples = static_cast<int>(buf_shared->samples.rows());

    juce::MessageManager::callAsync(
        [this, slot_index_capture, buf_shared, peak, rms, n_samples, c_time,
         slot_notices = std::move(notices)] {
            if (!experiment_) return;
            if (slot_index_capture < 0 ||
                slot_index_capture >= static_cast<int>(
                    experiment_->slots.size())) return;
            auto& s = experiment_->slots[
                static_cast<size_t>(slot_index_capture)];
            s.output_buffer = buf_shared;
            s.is_rendered = true;
            s.is_stale = false;
            s.error_message.reset();
            s.render_notices = slot_notices;
            iceberg::experiment::Slot::CachedMetrics m;
            m.peak_abs = peak;
            m.rms = rms;
            m.n_samples = n_samples;
            m.c_time = c_time;
            s.cached_metrics = m;
        });
    postStateChange(slot_index, SlotRenderState::Rendered);
}

void RenderOrchestrator::postStateChange(int slot_index,
                                         SlotRenderState new_state) {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (slot_index >= 0 &&
            slot_index < static_cast<int>(slot_states_.size())) {
            slot_states_[static_cast<size_t>(slot_index)] = new_state;
        }
    }
    if (onSlotStateChanged) {
        const int idx = slot_index;
        const auto st = new_state;
        juce::MessageManager::callAsync([cb = onSlotStateChanged, idx, st] {
            cb(idx, st);
        });
    }
}

}  // namespace iceberg::juce_app
