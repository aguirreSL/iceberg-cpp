#pragma once

#include "experiment/experiment.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace iceberg::juce_app {

enum class SlotRenderState {
    Idle,        ///< never rendered, or buffer cleared
    Queued,      ///< in queue, not yet started
    Rendering,   ///< active in worker thread
    Rendered,    ///< success, output_buffer populated
    Stale,       ///< rendered then user-edited; needs re-render
    Error,       ///< exception during render; error_message populated
    Cancelled,   ///< user cancelled before completion
};

/// Owns a single background worker thread that renders slots
/// sequentially. UI thread enqueues; worker dequeues, calls
/// `iceberg::rendering::iceberg(...)`, and posts results back via
/// `juce::MessageManager::callAsync` so all `Experiment` mutation
/// happens on the message thread (the panels hold raw `Slot*` pointers
/// that a `slots` mutation invalidates).
class RenderOrchestrator {
public:
    RenderOrchestrator();
    ~RenderOrchestrator();

    void setExperiment(iceberg::experiment::Experiment* exp);

    void renderSlot(int slot_index);
    void renderAllStale();
    void cancelAll();

    SlotRenderState              getSlotState(int slot_index) const;
    bool                         isAnythingRendering() const;
    int                          getStaleCount() const;

    /// Free a slot's `output_buffer` and reset its state to `Idle`.
    /// Frees the corresponding RAM. Slot's `is_rendered` and
    /// `is_stale` are reset (Idle == "as if never rendered").
    void clearSlotBuffer(int slot_index);

    /// Sum of `output_buffer.samples` byte sizes across all slots.
    size_t getCurrentBufferRamBytes() const;
    void   setMemoryBudgetGB(double gb);
    double getMemoryBudgetGB() const { return memory_budget_gb_; }

    /// Estimate the buffer size that a render of `slot` would
    /// produce. Used by callers to pre-flight the budget warning.
    /// Returns 0 if slot paths are not populated enough to estimate.
    size_t estimateRenderBytes(int slot_index) const;

    /// Helper: returns the sample rate currently fixed by populated
    /// slots in the experiment, or `nullopt` if no slot has a loadable
    /// IR yet. A render comes out at its IR's rate (the signal is
    /// resampled to it), so only IRs count: reads the first slot whose
    /// IR path is non-empty and openable, skipping `skip_slot` (the
    /// slot whose IR is being replaced). Used by Browse-time fs
    /// validation in MainComponent.
    static std::optional<double> getExperimentSampleRate(
        const iceberg::experiment::Experiment& exp, int skip_slot = -1);

    /// Slot lookup is sized to match `experiment_->slots`. Caller
    /// must invoke this after any slot list mutation (add / remove /
    /// reorder / duplicate). Cheap.
    void resyncSlotCount();

    // ---- Callbacks (always invoked on the JUCE message thread) ----

    /// Fired when a slot's render state changes.
    std::function<void(int /*slot_index*/, SlotRenderState)> onSlotStateChanged;

    /// Fired when the in-flight slot makes progress. Reports only at
    /// start (0.0) and end (1.0); fine-grained
    /// progress would require lib-side instrumentation.
    std::function<void(int /*slot_index*/, double /*progress 0..1*/)> onSlotProgress;

    /// Fired on an unexpected fatal error (worker thread caught
    /// something the per-slot handler couldn't classify).
    std::function<void(const std::string&)> onError;

private:
    void workerLoop();
    void renderOne(int slot_index);
    bool validateSlotPaths(const iceberg::experiment::Slot& slot,
                           std::string& err_out) const;
    void postStateChange(int slot_index, SlotRenderState new_state);

    iceberg::experiment::Experiment* experiment_ = nullptr;

    mutable std::mutex          state_mutex_;
    std::vector<SlotRenderState> slot_states_;

    mutable std::mutex          queue_mutex_;
    std::condition_variable     queue_cv_;
    std::queue<int>             render_queue_;
    std::atomic<bool>           cancel_requested_ {false};
    std::atomic<bool>           shutdown_         {false};
    std::atomic<int>            in_flight_index_  {-1};
    std::thread                 worker_;

    double memory_budget_gb_ = 3.0;
};

}  // namespace iceberg::juce_app
