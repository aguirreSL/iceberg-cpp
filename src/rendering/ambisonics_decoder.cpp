#include "rendering/ambisonics_decoder.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace iceberg::rendering {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Real-SH encoding matrix for first-order ACN/N3D, `(4 × n_ls)`.
///
/// Mirrors `polarch::getRSH(1, dirs_deg)` algebraically simplified to
/// the closed-form order-1 expressions:
///   Y[0, ls] = 1/√(4π)
///   Y[1, ls] = √(3/(4π)) · cos(elev) · sin(azi)
///   Y[2, ls] = √(3/(4π)) · sin(elev)
///   Y[3, ls] = √(3/(4π)) · cos(elev) · cos(azi)
/// (ACN order: m = 0, then m = -1, 0, +1 within n = 1.)
Eigen::MatrixXd compute_rsh_order1(const Eigen::MatrixXd& dirs_deg) {
    const Eigen::Index n_ls = dirs_deg.rows();
    Eigen::MatrixXd Y(4, n_ls);
    const double n00 = 1.0 / std::sqrt(4.0 * kPi);
    const double n1m = std::sqrt(3.0 / (4.0 * kPi));
    for (Eigen::Index i = 0; i < n_ls; ++i) {
        const double azi = dirs_deg(i, 0) * kPi / 180.0;
        const double ele = dirs_deg(i, 1) * kPi / 180.0;
        const double cos_e = std::cos(ele);
        Y(0, i) = n00;
        Y(1, i) = n1m * cos_e * std::sin(azi);
        Y(2, i) = n1m * std::sin(ele);
        Y(3, i) = n1m * cos_e * std::cos(azi);
    }
    return Y;
}

/// max-rE order weights for order 1, mirroring
/// `polarch::getMaxREweights(1)`. Closed form:
///   a_n[0] = legendre_0(cos(137.9° / 2.51)) = 1.0
///   a_n[1..3] = legendre_1(cos(137.9° / 2.51))   (broadcast across m)
///
/// Numerical reference (MATLAB R2026a):
///   1.00000000000000000000
///   0.57443052062777699707  (× 3 for the m = -1, 0, +1 entries)
Eigen::Vector4d max_re_weights_order1() {
    constexpr double kAngleDeg = 137.9 / 2.51;
    const double x = std::cos(kAngleDeg * kPi / 180.0);
    Eigen::Vector4d a;
    a(0) = 1.0;        // legendre_0 ≡ 1
    a(1) = x;          // legendre_1(x) = x
    a(2) = x;
    a(3) = x;
    return a;
}

/// FuMa → N3D / ACN conversion for first-order B-Format.
///
/// Channel reorder `[W, X, Y, Z] → [W, Y, Z, X]` (FuMa `map_fuma2acn`
/// for order 1 = `[1, 3, 4, 2]` 1-based) followed by per-channel scale
/// `[√2, √3, √3, √3]` (`norm_fuma2n3d` for order 1).
Eigen::MatrixXd fuma_to_n3d_order1(const Eigen::MatrixXf& fuma) {
    if (fuma.cols() != 4) {
        throw std::invalid_argument(
            "fuma_to_n3d_order1: expected 4 channels, got " +
            std::to_string(fuma.cols()));
    }
    const Eigen::Index n = fuma.rows();
    Eigen::MatrixXd n3d(n, 4);
    const double s2 = std::sqrt(2.0);
    const double s3 = std::sqrt(3.0);
    n3d.col(0) = fuma.col(0).cast<double>() * s2;  // W → W·√2
    n3d.col(1) = fuma.col(2).cast<double>() * s3;  // Y_FuMa → ACN(1)·√3
    n3d.col(2) = fuma.col(3).cast<double>() * s3;  // Z_FuMa → ACN(2)·√3
    n3d.col(3) = fuma.col(1).cast<double>() * s3;  // X_FuMa → ACN(3)·√3
    return n3d;
}

}  // namespace

Eigen::MatrixXd sad_decoder(const Eigen::MatrixXd& ls_dirs) {
    if (ls_dirs.cols() < 2) {
        throw std::invalid_argument(
            "sad_decoder: ls_dirs must have at least 2 columns "
            "(azimuth_deg, elevation_deg)");
    }
    const Eigen::Index n_ls = ls_dirs.rows();
    if (n_ls < 1) {
        throw std::invalid_argument(
            "sad_decoder: ls_dirs must have at least 1 loudspeaker");
    }

    const Eigen::MatrixXd Y_ls = compute_rsh_order1(ls_dirs);  // (4 × n_ls)
    Eigen::MatrixXd D =
        (4.0 * kPi / static_cast<double>(n_ls)) *
        Y_ls.transpose();                                     // (n_ls × 4)

    const Eigen::Vector4d a_n = max_re_weights_order1();
    D = D * a_n.asDiagonal();
    return D;
}

iceberg::Signal decode_bformat(const iceberg::Signal& bf_signal,
                               const Eigen::MatrixXd& decoder) {
    if (bf_signal.samples.cols() != 4) {
        throw std::invalid_argument(
            "decode_bformat: B-Format input must have 4 channels "
            "(FuMa W X Y Z)");
    }
    if (decoder.cols() != 4) {
        throw std::invalid_argument(
            "decode_bformat: decoder must have 4 columns (ACN order)");
    }

    const Eigen::MatrixXd n3d = fuma_to_n3d_order1(bf_signal.samples);
    // n3d (n × 4)  ·  decoder.transpose() (4 × n_ls)  =  (n × n_ls)
    const Eigen::MatrixXd ls_sig = n3d * decoder.transpose();

    iceberg::Signal out;
    out.sample_rate = bf_signal.sample_rate;
    out.samples = ls_sig.cast<float>();
    return out;
}

}  // namespace iceberg::rendering
