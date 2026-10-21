#include "native/time_window.h"

#include "native/hann.h"

#include <algorithm>
#include <cmath>

namespace iceberg::native {

namespace {

// 1-based sample index of time t: round(t * fs) + 1, halves away from zero.
Eigen::Index raw_bound(double t, double fs) {
    return static_cast<Eigen::Index>(std::round(t * fs)) + 1;
}

// The modulo is made non-negative so that negative odd bounds (from negative
// times) are bumped like positive ones; C++ `%` keeps the dividend's sign.
Eigen::Index bump_to_even(Eigen::Index v) {
    return (((v % 2) + 2) % 2 == 1) ? v + 1 : v;
}

}  // namespace

Signal time_window(const Signal& s, double start_sec, double end_sec,
                   WindowType wt) {
    const Eigen::Index n = s.samples.rows();
    const Eigen::Index cols = s.samples.cols();
    const double fs = s.sample_rate;

    Eigen::Index start = bump_to_even(raw_bound(start_sec, fs));
    // end_sec == 0.0 is a sentinel for "whole signal", so it is compared
    // exactly rather than with a tolerance.
    Eigen::Index end = bump_to_even(end_sec == 0.0 ? n + 1 : raw_bound(end_sec, fs));
    if (end == n + 1) {
        end -= 1;
    }
    // std::clamp is undefined when hi < lo, so an empty signal clamps to
    // [1, 1]; it then leaves through the N == 0 branch below.
    const Eigen::Index hi = std::max<Eigen::Index>(n, 1);
    start = std::clamp<Eigen::Index>(start, 1, hi);
    end = std::clamp<Eigen::Index>(end, 1, hi);

    const Eigen::Index len = end - start + 1;
    if (len <= 0 || n == 0) {
        Signal empty;
        empty.samples = Eigen::MatrixXf(0, cols);
        empty.sample_rate = s.sample_rate;
        return empty;
    }

    Eigen::ArrayXf w(len);
    if (wt == WindowType::Rectwin) {
        w.setOnes();
    } else {
        const Eigen::ArrayXd full = hann(static_cast<int>(2 * len));
        w = full.tail(len).cast<float>();
    }

    // matlab: start  →  c++: start - 1
    const Eigen::Index start0 = start - 1;

    Signal out;
    out.sample_rate = s.sample_rate;
    out.samples = Eigen::MatrixXf::Zero(n, cols);
    out.samples.topRows(start0) = s.samples.topRows(start0);
    out.samples.middleRows(start0, len) =
        (s.samples.middleRows(start0, len).array().colwise() * w).matrix();
    return out;
}

}  // namespace iceberg::native
