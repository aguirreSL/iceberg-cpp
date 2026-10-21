#include "StereoFold.h"

#include <cmath>

namespace iceberg::juce_app::audio {

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

StereoFold stereo_fold(const Eigen::MatrixXd& ls_dir, const Eigen::VectorXi& active_ls_numbers,
                       Eigen::Index n_master, bool clockwise) {
    StereoFold fold;
    fold.left.assign(static_cast<size_t>(n_master), 0.0f);
    fold.right.assign(static_cast<size_t>(n_master), 0.0f);
    for (Eigen::Index k = 0; k < ls_dir.rows(); ++k) {
        const double a = ls_dir(k, 0) * kPi / 180.0;
        const double left = clockwise ? -std::sin(a) : std::sin(a);   // +1 = all left
        const double pan = (1.0 - left) / 2.0;                        // 0 = left, 1 = right
        // matlab: activeLSNumbers(k) (1-based)  ->  c++: active_ls_numbers(k) - 1
        const auto ch = static_cast<size_t>(active_ls_numbers(k) - 1);
        fold.left[ch] = static_cast<float>(std::cos(pan * kPi / 2.0));
        fold.right[ch] = static_cast<float>(std::sin(pan * kPi / 2.0));
    }
    return fold;
}

Eigen::MatrixXf fold_to_stereo(const Eigen::MatrixXf& render, const StereoFold& fold) {
    Eigen::MatrixXf out = Eigen::MatrixXf::Zero(render.rows(), 2);
    for (Eigen::Index c = 0; c < render.cols(); ++c) {
        out.col(0) += fold.left[static_cast<size_t>(c)] * render.col(c);
        out.col(1) += fold.right[static_cast<size_t>(c)] * render.col(c);
    }
    return out;
}

}  // namespace iceberg::juce_app::audio
