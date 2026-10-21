#pragma once

#include "iceberg_signal.h"
#include "native/partitioned_convolver.h"
#include "rendering/calibrate_ambisonics.h"   // ConfigSetup
#include "rendering/input_handling.h"         // RenderNotice
#include "rendering/trajectory.h"

#include <Eigen/Dense>

#include <atomic>
#include <memory>
#include <vector>

namespace iceberg::rendering {

/// What a live render needs, computed once per slot: the parts
/// of `iceberg()` that do not depend on the source angle.
///
/// In Iceberg v1.0.0 the angle enters only in two places:
///   - the VBAP gains: the direct branch at angle a is, per active
///     loudspeaker k, `g_k(a)` times one angle-independent calibrated
///     stream (`direct.col(k)`), because the single level stage's scaler
///     is the panned RMS and `||g|| = 1`;
///   - the nearest loudspeaker, whose curve calibrates the dry signal
///     before the late convolution: one dry stream per loudspeaker
///     (`late_dry.col(k)`), convolved live with the IR tail.
/// With the angle fixed, `LiveEngine` reproduces `iceberg()` to the
/// float32 floor (`LiveEngine.FrozenAngle*` tests, against the MATLAB
/// references as well).
struct LiveRender {
    double sample_rate = 0.0;
    Eigen::Index length = 0;               ///< output frames, as iceberg() returns
    Eigen::VectorXi active_ls_numbers;     ///< 1-based positions in ls_array
    Eigen::Index n_master = 0;             ///< output channels (ls_array size)
    Eigen::MatrixXd ls_dir;                ///< active loudspeakers, [azimuth 0]
    Eigen::MatrixXf direct;                ///< (length x n_active) calibrated direct streams
    Eigen::MatrixXf late_dry;              ///< (signal x n_active) dry signal per nearest LS
    Eigen::MatrixXd ir_late;               ///< (taps x 4) first-order FuMa IR tail
    Eigen::MatrixXd decoder;               ///< (n_active x 4) SAD max-rE
};

/// The angle-independent part of `iceberg(signal, ir, angle, level_db,
/// config)`: input handling, the DSER / late split, the direct streams
/// (convolution, unit-gain overlap-add, VBAP calibration, dserFactor) and
/// the per-loudspeaker dry streams (Ambisonics calibration). Same
/// preconditions and notices as `iceberg()`.
LiveRender prepare_live(const iceberg::Signal& signal, const iceberg::Signal& ir,
                        double level_db, const ConfigSetup& config,
                        std::vector<RenderNotice>* notices = nullptr);

/// Plays a `LiveRender` block by block while the source angle changes.
///
/// Per block of `block()` frames: the VBAP gains of the current angle,
/// ramped linearly from the previous block's (no zipper noise); the dry
/// stream of the nearest loudspeaker into the IR tail, cross-faded over
/// `kLateFadeFrames` when the nearest loudspeaker changes (a new change
/// waits for the fade to end); the tail decoded to the loudspeakers.
/// Moving sources have no MATLAB counterpart: the direct gain is applied
/// after each loudspeaker's EQ, and the late tail keeps the colour it had
/// while it decays.
///
/// `set_angle` may be called from any thread; `render` belongs to one
/// (the audio) thread and never allocates or locks.
class LiveEngine {
public:
    static constexpr int kLateFadeFrames = 2048;

    /// `block`: a power of 2 (frames per internal block; the angle is
    /// sampled once per block). Throws `std::invalid_argument` otherwise.
    LiveEngine(std::shared_ptr<const LiveRender> render, double initial_angle_deg,
               int block = 512);

    void set_angle(double degrees) { angle_.store(degrees); }

    /// Writes the next `frames` frames of every output channel
    /// (`out[ch]`, `channels` of them, up to the render's `n_master`; extra
    /// channels get zeros). Past the end: zeros.
    void render(float* const* out, int channels, int frames);

    bool finished() const { return position_ >= render_->length && served_ == filled_; }

private:
    void next_block();

    std::shared_ptr<const LiveRender> render_;
    int block_;
    std::atomic<double> angle_;

    Eigen::MatrixXi pairs_;
    Eigen::MatrixXd inverse_;
    Eigen::VectorXd gains_, previous_gains_;
    Eigen::Index late_from_ = 0, late_to_ = 0;
    int fade_done_ = kLateFadeFrames;     // >= kLateFadeFrames: not fading

    iceberg::native::PartitionedConvolver tail_;
    std::vector<double> dry_;
    std::vector<std::vector<double>> bformat_;
    std::vector<double*> bformat_ptrs_;
    Eigen::MatrixXf block_out_;           // (block x n_master)
    int filled_ = 0, served_ = 0;
    Eigen::Index position_ = 0;           // frames computed so far
};

/// The whole slot rendered with the source following `points`: the live
/// engine run offline, the angle taken at the start of every block, so
/// the result is the same, bit for bit, every time. A path that holds
/// still gives `iceberg()` at that angle (to the float32 floor). Output:
/// (length x n_master), at the render's sample rate.
iceberg::Signal render_trajectory(std::shared_ptr<const LiveRender> render,
                                  const std::vector<TrajectoryPoint>& points, int block = 512);

}  // namespace iceberg::rendering
