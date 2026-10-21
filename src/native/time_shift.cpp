#include "native/time_shift.h"

#include "native/start_ir.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace iceberg::native {

namespace {

// Converting a double outside the range of int is undefined behaviour, so an
// out-of-range shift is rejected rather than cast.
int rounded_shift(const double value) {
    const double k = std::round(value);
    if (!std::isfinite(k) || k < static_cast<double>(std::numeric_limits<int>::min()) ||
        k > static_cast<double>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("time_shift: shift of " + std::to_string(value) +
                                    " samples does not fit in an int");
    }
    return static_cast<int>(k);
}

int auto_shift(const Eigen::MatrixXf& samples) {
    if (samples.size() == 0) {
        return 0;
    }
    const Eigen::MatrixXd ir = samples.cast<double>();
    const Eigen::VectorXi onsets = start_ir(ir, 20.0);
    return -onsets.minCoeff();
}

}  // namespace

TimeShiftResult time_shift(const Signal& s, const ShiftMode mode, const double amount) {
    int k = 0;
    switch (mode) {
        case ShiftMode::Auto:
            k = auto_shift(s.samples);
            break;
        case ShiftMode::Samples:
            k = rounded_shift(amount);
            break;
        case ShiftMode::Time:
            k = rounded_shift(amount * s.sample_rate);
            break;
    }

    TimeShiftResult result;
    result.shift_samples = k;
    result.signal.sample_rate = s.sample_rate;

    const Eigen::Index n = s.samples.rows();
    if (s.samples.size() == 0) {
        result.signal.samples = s.samples;
        return result;
    }

    // Wrap k into [0, N); C++ `%` keeps the sign of k, hence the second step.
    const Eigen::Index wrapped = ((static_cast<Eigen::Index>(k) % n) + n) % n;
    // out[(r + k) mod N] = in[r]: the last `wrapped` input rows land at the
    // top, the remaining rows follow them.
    result.signal.samples.resize(n, s.samples.cols());
    result.signal.samples.topRows(wrapped) = s.samples.bottomRows(wrapped);
    result.signal.samples.bottomRows(n - wrapped) = s.samples.topRows(n - wrapped);
    return result;
}

}  // namespace iceberg::native
