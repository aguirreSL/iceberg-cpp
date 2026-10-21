#include "native/add.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace iceberg::native {

Signal add(const Signal& a, const Signal& b) {
    if (a.sample_rate != b.sample_rate) {
        std::ostringstream msg;
        // Full precision so that two nearly equal rates still print apart.
        msg.precision(std::numeric_limits<double>::max_digits10);
        msg << "add: sample rates differ (" << a.sample_rate << " Hz and " << b.sample_rate
            << " Hz)";
        throw std::invalid_argument(msg.str());
    }

    const Eigen::Index a_rows = a.samples.rows();
    const Eigen::Index a_cols = a.samples.cols();
    const Eigen::Index b_rows = b.samples.rows();
    const Eigen::Index b_cols = b.samples.cols();

    Signal out;
    out.sample_rate = a.sample_rate;
    out.samples = Eigen::MatrixXf::Zero(std::max(a_rows, b_rows), std::max(a_cols, b_cols));

    // Copy each operand into its own region, then overwrite the overlap with
    // the sum. Copying (rather than adding to the zero padding) keeps a
    // sample present in only one operand bit-identical, -0.0f included.
    out.samples.topLeftCorner(b_rows, b_cols) = b.samples;
    out.samples.topLeftCorner(a_rows, a_cols) = a.samples;
    const Eigen::Index o_rows = std::min(a_rows, b_rows);
    const Eigen::Index o_cols = std::min(a_cols, b_cols);
    out.samples.topLeftCorner(o_rows, o_cols) =
        a.samples.topLeftCorner(o_rows, o_cols) + b.samples.topLeftCorner(o_rows, o_cols);
    return out;
}

}  // namespace iceberg::native
