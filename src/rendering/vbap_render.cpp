#include "rendering/vbap_render.h"

#include "native/hann.h"
#include "rendering/vbap_internals.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iceberg::rendering {

namespace {

// Block size selection mirrors `iceberg_set_vbap.m:56-61`. All branches
// converge on ~50 ms (= 2400 samples @ 44.1 kHz / 48 kHz, 2400 @ 96 kHz).
long blocksize_for(double fs) {
    if (fs == 48000.0) {
        return static_cast<long>(fs / 20.0);
    }
    if (fs == 96000.0) {
        return static_cast<long>(fs / 40.0);
    }
    // ~54 ms rounded to an even number of samples, so the hop is whole at
    // any rate (Iceberg v1.0.0; exact, hence unchanged, at 22.05, 44.1 and
    // 88.2 kHz). matlab: blocksize = 2*round(fs/18.3750/2)
    return 2 * std::lround(fs / 18.375 / 2.0);
}

}  // namespace

iceberg::Signal vbap_render(const iceberg::Signal& signal,
                            double source_angle_deg,
                            double /*source_elevation_deg*/,
                            const Eigen::MatrixXd& ls_dir) {
    if (signal.samples.cols() != 1) {
        throw std::invalid_argument("vbap_render: signal must be mono");
    }
    const Eigen::Index n_active = ls_dir.rows();
    if (n_active < 2) {
        throw std::invalid_argument(
            "vbap_render: ls_dir must have at least 2 loudspeakers");
    }

    const double fs = signal.sample_rate;
    const long blocksize = blocksize_for(fs);
    const long hopsize = blocksize / 2;
    const Eigen::Index Lsig = signal.samples.rows();

    // Nhop = ceil(Lsig / hopsize) + 2.
    const long Nhop =
        static_cast<long>((Lsig + hopsize - 1) / hopsize) + 2;
    // padsig = [zeros(hopsize); signal; zeros(Nhop*hopsize - Lsig - hopsize)]
    const long pad_pre = hopsize;
    const long pad_post = Nhop * hopsize - Lsig - hopsize;
    const long pad_total = pad_pre + Lsig + pad_post;

    Eigen::VectorXd padsig = Eigen::VectorXd::Zero(pad_total);
    padsig.segment(pad_pre, Lsig) =
        signal.samples.col(0).cast<double>();

    // Precompute VBAP gains for the (static) source angle.
    const Eigen::VectorXd ls_angles = ls_dir.col(0);
    const Eigen::MatrixXi ls_groups = detail::find_ls_pairs(ls_angles);
    const Eigen::MatrixXd inv_mtx =
        detail::invert_ls_mtx(ls_angles, ls_groups);
    const Eigen::VectorXd gains = detail::vbap_2d(
        source_angle_deg, ls_angles, ls_groups, inv_mtx);

    // Hanning window, not hann: see native/hann.h.
    const Eigen::ArrayXd window = iceberg::native::hanning(
        static_cast<int>(blocksize));

    // Block-OLA accumulator.
    Eigen::MatrixXd pansig =
        Eigen::MatrixXd::Zero(pad_total, n_active);

    for (long idx = 0; idx <= (Nhop - 2) * hopsize; idx += hopsize) {
        const Eigen::ArrayXd block =
            padsig.segment(idx, blocksize).array();
        const Eigen::VectorXd winsig = (block * window).matrix();
        // Outer product winsig (blocksize×1) × gains^T (1×n_active)
        // → (blocksize × n_active), accumulated into pansig.
        pansig.middleRows(idx, blocksize).noalias() +=
            winsig * gains.transpose();
    }

    // Truncate output: pansig(hopsize+(1:Lsig), :).
    iceberg::Signal out;
    out.sample_rate = fs;
    out.samples = pansig.middleRows(pad_pre, Lsig).cast<float>();
    return out;
}

Eigen::VectorXd vbap_overlap_add(const iceberg::Signal& signal) {
    if (signal.samples.cols() != 1) {
        throw std::invalid_argument("vbap_overlap_add: signal must be mono");
    }
    const long blocksize = blocksize_for(signal.sample_rate);
    const long hopsize = blocksize / 2;
    const Eigen::Index Lsig = signal.samples.rows();
    // The same padding, window and hops as vbap_render.
    const long Nhop = static_cast<long>((Lsig + hopsize - 1) / hopsize) + 2;
    const long pad_pre = hopsize;
    const long pad_total = Nhop * hopsize;

    Eigen::VectorXd padsig = Eigen::VectorXd::Zero(pad_total);
    padsig.segment(pad_pre, Lsig) = signal.samples.col(0).cast<double>();
    const Eigen::ArrayXd window = iceberg::native::hanning(static_cast<int>(blocksize));

    Eigen::VectorXd acc = Eigen::VectorXd::Zero(pad_total);
    for (long idx = 0; idx <= (Nhop - 2) * hopsize; idx += hopsize) {
        acc.segment(idx, blocksize).array() += padsig.segment(idx, blocksize).array() * window;
    }
    return acc.segment(pad_pre, Lsig);
}

}  // namespace iceberg::rendering
