#include "rendering/live.h"

#include "native/convolve.h"
#include "native/normalize_dat.h"
#include "rendering/ambisonics_decoder.h"
#include "rendering/calibrate_vbap.h"
#include "rendering/iceberg_core.h"
#include "rendering/vbap_internals.h"
#include "rendering/vbap_render.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace iceberg::rendering {

LiveRender prepare_live(const iceberg::Signal& signal_in, const iceberg::Signal& ir_in,
                        double level_db, const ConfigSetup& config,
                        std::vector<RenderNotice>* notices) {
    if (auto err = validate_config(config)) {
        throw std::invalid_argument("prepare_live: " + *err);
    }
    if (signal_in.samples.cols() != 1) {
        throw std::invalid_argument("prepare_live: signal must be mono");
    }
    // The same steps as iceberg(), stopped before the angle enters.
    PreparedInputs prepared = prepare_inputs(signal_in, ir_in, config);
    if (notices) notices->insert(notices->end(), prepared.notices.begin(), prepared.notices.end());
    const auto core = iceberg_core(prepared.ir, config.anechoic_special_case);
    iceberg::Signal signal_norm;
    signal_norm.sample_rate = prepared.signal.sample_rate;
    signal_norm.samples = iceberg::native::normalize_dat(prepared.signal.samples);

    const Eigen::Index n_active = config.ls_dir.rows();
    LiveRender r;
    r.sample_rate = signal_norm.sample_rate;
    r.active_ls_numbers = config.active_ls_numbers;
    r.n_master = config.ls_array.size();
    r.ls_dir = config.ls_dir;

    // Direct branch. The pan at angle a is g(a) times the unit-gain
    // overlap-add u, and calibrate_vbap's scaler is the RMS of the panned
    // channels' Euclidean sum, ||g|| * rms(u) = rms(u): feeding it u/sqrt(N)
    // on every channel gives the same scaler, and channel k comes out as
    // the calibrated stream of loudspeaker k over sqrt(N).
    const auto dser = iceberg::native::convolve(signal_norm, core.dser);
    const Eigen::VectorXd u = vbap_overlap_add(dser);
    const double root_n = std::sqrt(static_cast<double>(n_active));
    iceberg::Signal spread;
    spread.sample_rate = dser.sample_rate;
    spread.samples = (u / root_n).cast<float>().replicate(1, n_active);
    const auto calibrated = calibrate_vbap(spread, level_db, 0.0, config);
    double direct_scale = root_n;
    switch (config.dser_factor.kind) {
        case DserFactor::Kind::Unity:
            break;
        case DserFactor::Kind::Thesis:
            // matlab: directScale = max(DSER.time(:)) (the positive peak)
            direct_scale *= static_cast<double>(core.dser.samples.maxCoeff());
            break;
        case DserFactor::Kind::Numeric:
            direct_scale *= config.dser_factor.value;
            break;
    }
    r.direct = (calibrated.samples.cast<double>() * direct_scale).cast<float>();

    // Late branch: the dry signal as calibrate_ambisonics leaves it with
    // each loudspeaker as the nearest one.
    r.late_dry.resize(signal_norm.samples.rows(), n_active);
    for (Eigen::Index k = 0; k < n_active; ++k) {
        r.late_dry.col(k) =
            calibrate_ambisonics(signal_norm, level_db, config.ls_dir(k, 0), config).samples.col(0);
    }
    r.ir_late = core.ir_late.samples.cast<double>();

    // decode_bformat converts FuMa (W X Y Z) to N3D (W*sqrt2, Y*sqrt3,
    // Z*sqrt3, X*sqrt3) before the SAD matrix (ACN columns): fold the
    // conversion in, so the engine decodes FuMa directly.
    const Eigen::MatrixXd sad = sad_decoder(config.ls_dir);
    const double s2 = std::sqrt(2.0), s3 = std::sqrt(3.0);
    r.decoder.resize(n_active, 4);
    r.decoder.col(0) = sad.col(0) * s2;   // W
    r.decoder.col(1) = sad.col(3) * s3;   // X -> ACN 3
    r.decoder.col(2) = sad.col(1) * s3;   // Y -> ACN 1
    r.decoder.col(3) = sad.col(2) * s3;   // Z -> ACN 2

    // iceberg() sums the two branches, zero-padding the shorter.
    const Eigen::Index late_length =
        2 * ((signal_norm.samples.rows() + r.ir_late.rows()) / 2);   // 2*ceil((n1+n2-1)/2)
    r.length = std::max<Eigen::Index>(r.direct.rows(), late_length);
    return r;
}

namespace {

// The IR tail, or a silent one so a null render fails with the clear
// message below instead of in the convolver.
Eigen::MatrixXd tail_of(const std::shared_ptr<const LiveRender>& render) {
    if (render) return render->ir_late;
    return Eigen::MatrixXd::Zero(1, 4);
}

}  // namespace

LiveEngine::LiveEngine(std::shared_ptr<const LiveRender> render, double initial_angle_deg,
                       int block)
    : render_(std::move(render)),
      block_(block),
      angle_(initial_angle_deg),
      tail_(tail_of(render_), block) {
    if (!render_) throw std::invalid_argument("LiveEngine: no render");
    if (block < 2 || (block & (block - 1)) != 0) {
        throw std::invalid_argument("LiveEngine: block must be a power of 2");
    }
    const Eigen::VectorXd azimuths = render_->ls_dir.col(0);
    pairs_ = detail::find_ls_pairs(azimuths);
    inverse_ = detail::invert_ls_mtx(azimuths, pairs_);
    const Eigen::Index n_active = render_->ls_dir.rows();
    gains_.resize(n_active);
    previous_gains_.resize(n_active);
    detail::vbap_2d_into(initial_angle_deg, pairs_, inverse_, gains_);
    previous_gains_ = gains_;
    late_from_ = late_to_ = nearest_loudspeaker(initial_angle_deg, render_->ls_dir);

    dry_.assign(static_cast<size_t>(block_), 0.0);
    bformat_.assign(4, std::vector<double>(static_cast<size_t>(block_), 0.0));
    for (auto& b : bformat_) bformat_ptrs_.push_back(b.data());
    block_out_ = Eigen::MatrixXf::Zero(block_, render_->n_master);
}

void LiveEngine::next_block() {
    const LiveRender& r = *render_;
    const double angle = angle_.load(std::memory_order_relaxed);
    previous_gains_ = gains_;
    detail::vbap_2d_into(angle, pairs_, inverse_, gains_);
    if (fade_done_ >= kLateFadeFrames) {
        const Eigen::Index nearest = nearest_loudspeaker(angle, r.ls_dir);
        if (nearest != late_to_) {
            late_from_ = late_to_;
            late_to_ = nearest;
            fade_done_ = 0;
        }
    }

    const Eigen::Index n_active = r.ls_dir.rows();
    const Eigen::Index n_direct = r.direct.rows();
    const Eigen::Index n_dry = r.late_dry.rows();
    block_out_.setZero();
    for (int t = 0; t < block_; ++t) {
        const Eigen::Index frame = position_ + t;
        if (frame < n_direct) {
            // Linear ramp from the previous block's gains: exact when the
            // angle holds still (previous == current).
            const double w = static_cast<double>(t + 1) / block_;
            for (Eigen::Index k = 0; k < n_active; ++k) {
                const double g = previous_gains_(k) + (gains_(k) - previous_gains_(k)) * w;
                // matlab: activeLSNumbers(k) (1-based)  →  c++: active_ls_numbers(k) - 1
                block_out_(t, r.active_ls_numbers(k) - 1) +=
                    static_cast<float>(g * static_cast<double>(r.direct(frame, k)));
            }
        }
        double dry = 0.0;
        if (frame < n_dry) {
            dry = r.late_dry(frame, late_to_);
            if (fade_done_ < kLateFadeFrames) {
                const double f = std::min(1.0, static_cast<double>(fade_done_ + t + 1) /
                                                   kLateFadeFrames);
                dry = (1.0 - f) * r.late_dry(frame, late_from_) + f * dry;
            }
        }
        dry_[static_cast<size_t>(t)] = dry;
    }
    if (fade_done_ < kLateFadeFrames) fade_done_ += block_;

    tail_.process(dry_.data(), bformat_ptrs_.data());
    for (int t = 0; t < block_; ++t) {
        for (Eigen::Index k = 0; k < n_active; ++k) {
            double v = 0.0;
            for (int c = 0; c < 4; ++c) {
                v += r.decoder(k, c) * bformat_[static_cast<size_t>(c)][static_cast<size_t>(t)];
            }
            block_out_(t, r.active_ls_numbers(k) - 1) += static_cast<float>(v);
        }
    }

    filled_ = static_cast<int>(std::min<Eigen::Index>(block_, r.length - position_));
    served_ = 0;
    position_ += block_;
}

iceberg::Signal render_trajectory(std::shared_ptr<const LiveRender> render,
                                  const std::vector<TrajectoryPoint>& points, int block) {
    if (!render) throw std::invalid_argument("render_trajectory: no render");
    const double fs = render->sample_rate;
    LiveEngine engine(render, trajectory_angle(points, 0.0), block);
    iceberg::Signal out;
    out.sample_rate = fs;
    out.samples = Eigen::MatrixXf::Zero(render->length, render->n_master);
    // Whole blocks into a block-sized scratch, so each call starts a new
    // engine block and the angle set just before it applies to it.
    Eigen::MatrixXf scratch(block, render->n_master);
    std::vector<float*> ptrs(static_cast<size_t>(render->n_master));
    for (Eigen::Index c = 0; c < render->n_master; ++c) ptrs[static_cast<size_t>(c)] = scratch.col(c).data();
    for (Eigen::Index start = 0; start < render->length; start += block) {
        engine.set_angle(trajectory_angle(points, static_cast<double>(start) / fs));
        engine.render(ptrs.data(), static_cast<int>(ptrs.size()), block);
        const Eigen::Index n = std::min<Eigen::Index>(block, render->length - start);
        out.samples.middleRows(start, n) = scratch.topRows(n);
    }
    return out;
}

void LiveEngine::render(float* const* out, int channels, int frames) {
    const int n_master = static_cast<int>(render_->n_master);
    int written = 0;
    while (written < frames) {
        if (served_ == filled_) {
            if (position_ >= render_->length) break;
            next_block();
        }
        const int count = std::min(frames - written, filled_ - served_);
        for (int c = 0; c < channels; ++c) {
            float* dst = out[c] + written;
            if (c < n_master) {
                for (int i = 0; i < count; ++i) dst[i] = block_out_(served_ + i, c);
            } else {
                std::fill(dst, dst + count, 0.0f);
            }
        }
        served_ += count;
        written += count;
    }
    for (int c = 0; c < channels; ++c) std::fill(out[c] + written, out[c] + frames, 0.0f);
}

}  // namespace iceberg::rendering
