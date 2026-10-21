#pragma once

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

// Internal helpers for VBAP rendering — ports of the polarch toolbox
// functions `findLsPairs`, `invertLsMtx`, and `vbap` (2D path only) at
// `Iceberg/Toolboxes/polarch/vbap/`. Header-only, scoped to
// `iceberg::rendering::detail`. Promote to a non-detail header if a
// second consumer ever appears.

namespace iceberg::rendering::detail {

constexpr double kVbapPi = 3.14159265358979323846;

/// Build adjacent loudspeaker pairs for a 2D ring layout.
///
/// Sorts the input azimuths and returns `(N × 2)` pairs of 1-based
/// indices into `ls_dirs_deg`, with the last pair wrapping around
/// (`[idx_sorted[N-1], idx_sorted[0]]`). Mirrors
/// `findLsPairs.m` (no `OMIT_LARGE_PAIR`).
inline Eigen::MatrixXi find_ls_pairs(const Eigen::VectorXd& ls_dirs_deg) {
    const Eigen::Index n = ls_dirs_deg.size();
    if (n < 2) {
        throw std::invalid_argument(
            "find_ls_pairs: need at least 2 loudspeakers");
    }

    std::vector<Eigen::Index> idx_sorted(static_cast<size_t>(n));
    std::iota(idx_sorted.begin(), idx_sorted.end(), Eigen::Index{0});
    std::stable_sort(idx_sorted.begin(), idx_sorted.end(),
                     [&](Eigen::Index a, Eigen::Index b) {
                         return ls_dirs_deg(a) < ls_dirs_deg(b);
                     });

    Eigen::MatrixXi pairs(n, 2);  // n adjacent pairs (with wrap)
    for (Eigen::Index k = 0; k < n; ++k) {
        // matlab: idx_sorted is 1-based; c++: store as 1-based to keep
        // parity with vbap.m which uses 1-based indices throughout.
        pairs(k, 0) =
            static_cast<int>(idx_sorted[static_cast<size_t>(k)] + 1);
        pairs(k, 1) = static_cast<int>(
            idx_sorted[static_cast<size_t>((k + 1) % n)] + 1);
    }
    return pairs;
}

/// Precompute per-pair `inv(L)` (2×2) flattened in MATLAB column-major
/// order, mirroring `invertLsMtx.m` for 2D layouts.
///
/// `ls_invMtx` is `(Npair × 4)` where each row stores
/// `[M(0,0), M(1,0), M(0,1), M(1,1)]` — i.e. `tempInv(:)` in MATLAB
/// stacks columns of the 2×2 inverse.
inline Eigen::MatrixXd invert_ls_mtx(const Eigen::VectorXd& ls_dirs_deg,
                                     const Eigen::MatrixXi& ls_groups) {
    const Eigen::Index n_group = ls_groups.rows();
    Eigen::MatrixXd inv_mtx(n_group, 4);

    for (Eigen::Index n = 0; n < n_group; ++n) {
        // matlab: ls_groups(n, :) is 1-based; c++: subtract 1.
        const Eigen::Index i0 =
            static_cast<Eigen::Index>(ls_groups(n, 0) - 1);
        const Eigen::Index i1 =
            static_cast<Eigen::Index>(ls_groups(n, 1) - 1);

        const double a0 = ls_dirs_deg(i0) * kVbapPi / 180.0;
        const double a1 = ls_dirs_deg(i1) * kVbapPi / 180.0;
        Eigen::Matrix2d L;
        L << std::cos(a0), std::sin(a0),
             std::cos(a1), std::sin(a1);

        const Eigen::Matrix2d M = L.inverse();
        // Column-major flatten to match MATLAB `tempInv(:)`.
        inv_mtx(n, 0) = M(0, 0);
        inv_mtx(n, 1) = M(1, 0);
        inv_mtx(n, 2) = M(0, 1);
        inv_mtx(n, 3) = M(1, 1);
    }
    return inv_mtx;
}

/// 2D VBAP gain vector for a single source azimuth, mirroring
/// `vbap.m::vbap2` followed by the outer `max(0, ...)` clamp.
///
/// In a well-formed 2D ring layout, exactly one pair satisfies the
/// `min(g1, g2) > -tol` test for any given source angle. If more than
/// one pair satisfies (a pathological layout, e.g. duplicated angles),
/// **the last pair in iteration order wins** — this replicates the
/// polarch behaviour, which simply overwrites without breaking out of
/// the loop. Don't add a `break`; that would silently diverge from
/// MATLAB.
///
/// `vbap_2d_into` writes into `gains` (sized to the loudspeaker count)
/// without allocating, for the live engine's audio thread; `vbap_2d`
/// calls it, so both give the same bits.
inline void vbap_2d_into(double azi_deg,
                         const Eigen::MatrixXi& ls_groups,
                         const Eigen::MatrixXd& ls_invMtx,
                         Eigen::Ref<Eigen::VectorXd> gains) {
    const double azi_rad = azi_deg * kVbapPi / 180.0;
    const double cos_a = std::cos(azi_rad);
    const double sin_a = std::sin(azi_rad);

    gains.setZero();
    for (Eigen::Index i = 0; i < ls_groups.rows(); ++i) {
        // ls_invMtx[i] is column-stacked inv(L_row): [M00, M10, M01, M11].
        // Indexing (1:2) and (3:4) in MATLAB picks the FIRST and SECOND
        // columns of inv(L_row), and dotting them with u_col gives the
        // row entries of inv(L_row^T) * u_col = correct VBAP gains.
        const double g1 =
            ls_invMtx(i, 0) * cos_a + ls_invMtx(i, 1) * sin_a;
        const double g2 =
            ls_invMtx(i, 2) * cos_a + ls_invMtx(i, 3) * sin_a;
        if (std::min(g1, g2) > -0.001) {
            const double n = std::sqrt(g1 * g1 + g2 * g2);
            // matlab: ls_groups(i, :) is 1-based; c++: subtract 1.
            gains(static_cast<Eigen::Index>(ls_groups(i, 0) - 1)) =
                g1 / n;
            gains(static_cast<Eigen::Index>(ls_groups(i, 1) - 1)) =
                g2 / n;
        }
    }
    // Outer wrapper from vbap.m:44.
    gains = gains.cwiseMax(0.0);
    // Final overall norm — no-op when only one pair contributed
    // (guaranteed for well-formed ring layouts).
    const double tot = gains.norm();
    if (tot > 0.0) {
        gains /= tot;
    }
}

inline Eigen::VectorXd vbap_2d(double azi_deg,
                               const Eigen::VectorXd& ls_dirs_deg,
                               const Eigen::MatrixXi& ls_groups,
                               const Eigen::MatrixXd& ls_invMtx) {
    Eigen::VectorXd gains(ls_dirs_deg.size());
    vbap_2d_into(azi_deg, ls_groups, ls_invMtx, gains);
    return gains;
}

}  // namespace iceberg::rendering::detail
