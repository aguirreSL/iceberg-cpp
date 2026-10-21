#include "native/time_crop.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace iceberg::native {

namespace {

// 1-based sample index of a time. std::lround is unspecified for NaN,
// infinities and values beyond the range of long, so those are rejected
// before rounding instead of producing an arbitrary index.
Eigen::Index one_based_index(const double seconds, const double fs, const char* name) {
    const double position = seconds * fs;
    constexpr double kLimit = 4.0e18;  // well inside the range of a 64-bit long
    if (!std::isfinite(position) || std::abs(position) > kLimit) {
        throw std::invalid_argument(std::string("time_crop: ") + name + " * sample_rate = " +
                                    std::to_string(position) +
                                    " cannot be rounded to a sample index");
    }
    return static_cast<Eigen::Index>(std::lround(position)) + 1;
}

}  // namespace

Signal time_crop(const Signal& s, const double start_sec, const double end_sec) {
    const Eigen::Index n = s.samples.rows();
    const Eigen::Index channels = s.samples.cols();
    const Eigen::Index i1 = one_based_index(start_sec, s.sample_rate, "start_sec");
    Eigen::Index i2 = one_based_index(end_sec, s.sample_rate, "end_sec");

    const Eigen::Index span = i2 - i1;
    if (span > 0 && span % 2 == 0) {
        --i2;
    }

    Signal out;
    out.sample_rate = s.sample_rate;

    if (i2 > i1) {
        if (i1 < 1 || i2 > n) {
            throw std::invalid_argument(
                "time_crop: range [" + std::to_string(start_sec) + ", " +
                std::to_string(end_sec) + "] s gives samples " + std::to_string(i1) + ".." +
                std::to_string(i2) + ", outside the signal's 1.." + std::to_string(n));
        }
        // 1-based samples i1..i2  ->  0-based rows i1-1 .. i2-1
        out.samples = s.samples.middleRows(i1 - 1, i2 - i1 + 1);
        return out;
    }

    if (i1 < 0) {
        throw std::invalid_argument("time_crop: inverted range starts at sample " +
                                    std::to_string(i1) + ", before the signal");
    }
    const Eigen::Index head = std::max<Eigen::Index>(i2 - 1, 0);
    if (head > n) {
        throw std::invalid_argument("time_crop: inverted range keeps samples 1.." +
                                    std::to_string(i2 - 1) + ", past the signal's " +
                                    std::to_string(n) + " samples");
    }
    const Eigen::Index tail = std::max<Eigen::Index>(n - i1, 0);

    // Head: 1-based samples 1..i2-1  ->  0-based rows 0 .. i2-2.
    // Tail: 1-based samples i1+1..N  ->  0-based rows i1 .. N-1, i.e. the last
    // N - i1 rows.
    out.samples.resize(head + tail, channels);
    out.samples.topRows(head) = s.samples.topRows(head);
    out.samples.bottomRows(tail) = s.samples.bottomRows(tail);
    return out;
}

}  // namespace iceberg::native
