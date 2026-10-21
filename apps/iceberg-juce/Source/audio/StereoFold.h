#pragma once

#include <Eigen/Dense>

#include <vector>

namespace iceberg::juce_app::audio {

/// Preparation mode for a moving source: every loudspeaker panned to L/R
/// by where it sits left or right of the listener (equal power, from its
/// azimuth and the slot's angle convention: +90 is left counter-clockwise,
/// right clockwise; front and back in the middle). A moving source then
/// moves smoothly on headphones, where mapping its current pair to L/R
/// would jump when the pair changes.
struct StereoFold {
    std::vector<float> left, right;   ///< per output channel (ls_array position)
};

/// Gains for the `n_master` output channels; inactive positions get 0.
StereoFold stereo_fold(const Eigen::MatrixXd& ls_dir, const Eigen::VectorXi& active_ls_numbers,
                       Eigen::Index n_master, bool clockwise);

/// A (frames x n_master) render folded down to (frames x 2).
Eigen::MatrixXf fold_to_stereo(const Eigen::MatrixXf& render, const StereoFold& fold);

}  // namespace iceberg::juce_app::audio
