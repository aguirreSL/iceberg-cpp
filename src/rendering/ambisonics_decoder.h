#pragma once

#include "iceberg_signal.h"

#include <Eigen/Dense>

namespace iceberg::rendering {

/// SAD (Sampling Ambisonic Decoder) matrix with max-rE weighting,
/// 1st-order, mirroring `polarch::ambiDecoder(ls_dirs, 'SAD', 1, 1)`.
///
/// Algorithm: `D = (4π / n_ls) * Y_ls.transpose() * diag(a_n)`, where
/// `Y_ls = getRSH(1, ls_dirs)` is the (4 × n_ls) real-SH encoding
/// matrix in ACN order with N3D normalisation, and `a_n` are the
/// max-rE order weights.
///
/// `ls_dirs` is `(n_ls × 2)` with columns `[azimuth_deg, elevation_deg]`.
/// Returns `(n_ls × 4)` decoder in ACN order `[W, Y, Z, X]`. For 2D ring
/// layouts (elev = 0 for all LSes), the Z column is exactly zero.
///
/// Throws `std::invalid_argument` if `ls_dirs.cols() < 2` or
/// `ls_dirs.rows() < 1`.
Eigen::MatrixXd sad_decoder(const Eigen::MatrixXd& ls_dirs);

/// Decode FuMa B-Format to physical loudspeaker signals.
///
/// Mirrors `polarch::decodeBformat(BFsig, M_dec)`: converts the FuMa
/// input to N3D / ACN internally (channel reorder
/// `[W, X, Y, Z] → [W, Y, Z, X]` + per-channel scale by
/// `[√2, √3, √3, √3]`) before the matrix multiply.
///
/// `bf_signal.samples` must be `(nSamples × 4)` in FuMa order
/// `[W, X, Y, Z]`. `decoder` is `(n_ls × 4)` ACN-ordered, typically
/// from `sad_decoder()`. Output is `(nSamples × n_ls)`.
///
/// Throws `std::invalid_argument` on shape mismatch.
iceberg::Signal decode_bformat(const iceberg::Signal& bf_signal,
                               const Eigen::MatrixXd& decoder);

}  // namespace iceberg::rendering
