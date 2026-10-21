#include "native/start_ir.h"

#include <cmath>
#include <vector>

namespace iceberg::native {

namespace {

// Largest index in [0, last] whose level is strictly below `threshold`, or 0
// when there is none. A zero sample has level -inf, so it always qualifies.
Eigen::Index last_below(const std::vector<double>& level, const Eigen::Index last,
                        const double threshold) {
    for (Eigen::Index k = last; k >= 0; --k) {
        if (level[static_cast<std::size_t>(k)] < threshold) {
            return k;
        }
    }
    return 0;
}

Eigen::Index channel_onset(const Eigen::Ref<const Eigen::VectorXd>& x, const double threshold) {
    const Eigen::Index n = x.size();
    const Eigen::VectorXd energy = x.array().square().matrix();

    // The noise region starts one sample before index m (0-based m - 1), as
    // the specification fixes it; m >= 1 whenever n >= 1, so the start is
    // never negative.
    const Eigen::Index m = static_cast<Eigen::Index>(std::round(0.9 * static_cast<double>(n)));
    const Eigen::Index noise_begin = m - 1;
    // Plain sequential sum in double: the mean only feeds the 20 dB guard.
    double noise_sum = 0.0;
    for (Eigen::Index k = noise_begin; k < n; ++k) {
        noise_sum += energy[k];
    }
    const double noise = noise_sum / static_cast<double>(n - noise_begin);

    // Strict `>` keeps the earliest index among equal maxima.
    Eigen::Index peak_index = 0;
    for (Eigen::Index k = 1; k < n; ++k) {
        if (energy[k] > energy[peak_index]) {
            peak_index = k;
        }
    }
    const double peak = energy[peak_index];

    // No reliable onset: peak too close to the noise, peak inside the noise
    // tail, or nothing before the peak to search.
    if (peak < 100.0 * noise || peak_index + 1 > m || peak_index == 0) {
        return 0;
    }

    // Difference of the two logarithms, not log of the ratio: the threshold
    // comparisons below must see exactly these values to land on the same
    // sample as the reference.
    const double peak_db = 10.0 * std::log10(peak);
    std::vector<double> level(static_cast<std::size_t>(peak_index) + 1);
    for (Eigen::Index k = 0; k <= peak_index; ++k) {
        level[static_cast<std::size_t>(k)] = 10.0 * std::log10(energy[k]) - peak_db;
    }

    const Eigen::Index first_estimate = last_below(level, peak_index, threshold);

    // Energy that comes back more than 6 dB above the threshold before the
    // first estimate is part of the response, so the onset must precede it.
    const double rise = threshold + 6.0;
    for (Eigen::Index k = 0; k <= first_estimate; ++k) {
        if (level[static_cast<std::size_t>(k)] > rise) {
            return last_below(level, k, threshold);
        }
    }
    return first_estimate;
}

}  // namespace

Eigen::VectorXi start_ir(const Eigen::MatrixXd& ir, const double threshold_db) {
    Eigen::VectorXi onsets = Eigen::VectorXi::Zero(ir.cols());
    if (ir.rows() == 0) {
        return onsets;
    }
    const double threshold = -std::abs(threshold_db);
    for (Eigen::Index c = 0; c < ir.cols(); ++c) {
        onsets[c] = static_cast<int>(channel_onset(ir.col(c), threshold));
    }
    return onsets;
}

}  // namespace iceberg::native
