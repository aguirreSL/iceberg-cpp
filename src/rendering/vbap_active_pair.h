#pragma once

#include "rendering/vbap_internals.h"

#include <Eigen/Dense>

namespace iceberg::rendering {

/// Active VBAP pair for a single 2D source angle.
///
/// Mirrors the inner loop of `detail::vbap_2d` but returns the
/// winning pair indices and gains instead of distributing into a
/// full `n_ls` vector. Useful for the JUCE prep-mode L/R mapping
/// where we need the pair identity directly.
///
/// `s1_idx` / `s2_idx` are 0-based row indices into `ls_dir`.
/// `g1_norm` / `g2_norm` are the VBAP gains after norm
/// (sqrt(g1^2 + g2^2) = 1). `s1` corresponds to the "earlier" LS in
/// the sorted ring order, matching `findLsPairs` ordering — handles
/// ring-wrap correctly so a source between LS_N-1 and LS_0 yields
/// `s1 = LS_N-1, s2 = LS_0`.
struct VbapActivePair {
    int s1_idx;
    int s2_idx;
    double g1_norm;
    double g2_norm;
};

/// Compute the active VBAP pair for a 2D ring layout. Throws
/// `std::invalid_argument` if `ls_dir.rows() < 2`.
///
/// `ls_dir` shape: `(n_ls, 2)` with `[azimuth_deg, elevation_deg]`.
/// Elevation is ignored (port is 2D-only).
///
/// If no pair satisfies `min(g1, g2) > -0.001` (degenerate layout),
/// returns the wrap-around pair with both gains = 0.5 as fallback.
inline VbapActivePair vbap_active_pair(double source_angle_deg,
                                       const Eigen::MatrixXd& ls_dir) {
    if (ls_dir.rows() < 2) {
        throw std::invalid_argument(
            "vbap_active_pair: need at least 2 loudspeakers");
    }
    const Eigen::VectorXd azis = ls_dir.col(0);
    const auto pairs = detail::find_ls_pairs(azis);
    const auto inv_mtx = detail::invert_ls_mtx(azis, pairs);

    const double azi_rad = source_angle_deg * detail::kVbapPi / 180.0;
    const double cos_a = std::cos(azi_rad);
    const double sin_a = std::sin(azi_rad);

    VbapActivePair out{static_cast<int>(pairs(0, 0) - 1),
                       static_cast<int>(pairs(0, 1) - 1),
                       0.5, 0.5};

    for (Eigen::Index i = 0; i < pairs.rows(); ++i) {
        const double g1 =
            inv_mtx(i, 0) * cos_a + inv_mtx(i, 1) * sin_a;
        const double g2 =
            inv_mtx(i, 2) * cos_a + inv_mtx(i, 3) * sin_a;
        if (std::min(g1, g2) > -0.001) {
            const double n = std::sqrt(g1 * g1 + g2 * g2);
            // matlab: pairs(i, :) is 1-based; c++: subtract 1.
            out.s1_idx = static_cast<int>(pairs(i, 0) - 1);
            out.s2_idx = static_cast<int>(pairs(i, 1) - 1);
            out.g1_norm = (n > 0.0) ? g1 / n : 0.5;
            out.g2_norm = (n > 0.0) ? g2 / n : 0.5;
            // No early break - replicate vbap.m semantics (the last
            // valid pair wins).
        }
    }
    return out;
}

}  // namespace iceberg::rendering
