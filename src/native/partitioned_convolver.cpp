#include "native/partitioned_convolver.h"

#include <algorithm>
#include <stdexcept>

namespace iceberg::native {

namespace {
using cd = std::complex<double>;
}  // namespace

PartitionedConvolver::PartitionedConvolver(const Eigen::MatrixXd& filters, int block)
    : block_(block),
      outputs_(static_cast<int>(filters.cols())),
      partitions_(0),
      bins_(block + 1),
      fft_((block >= 2 && (block & (block - 1)) == 0) ? 2 * block : 4) {
    if (block < 2 || (block & (block - 1)) != 0) {
        throw std::invalid_argument("PartitionedConvolver: block must be a power of 2, at least 2");
    }
    if (filters.rows() < 1 || filters.cols() < 1) {
        throw std::invalid_argument("PartitionedConvolver: need at least one tap and one output");
    }
    const auto taps = static_cast<int>(filters.rows());
    partitions_ = (taps + block_ - 1) / block_;

    std::vector<double> padded(static_cast<size_t>(2 * block_));
    filter_.assign(static_cast<size_t>(outputs_), {});
    for (int c = 0; c < outputs_; ++c) {
        auto& parts = filter_[static_cast<size_t>(c)];
        parts.assign(static_cast<size_t>(partitions_ * bins_), cd(0.0, 0.0));
        for (int p = 0; p < partitions_; ++p) {
            std::fill(padded.begin(), padded.end(), 0.0);
            const int first = p * block_;
            const int count = std::min(block_, taps - first);
            for (int t = 0; t < count; ++t) {
                padded[static_cast<size_t>(t)] = filters(first + t, c);
            }
            fft_.forward(padded.data(), parts.data() + static_cast<size_t>(p * bins_));
        }
    }
    delay_.assign(static_cast<size_t>(partitions_),
                  std::vector<cd>(static_cast<size_t>(bins_), cd(0.0, 0.0)));
    window_.assign(static_cast<size_t>(2 * block_), 0.0);
    accumulate_.assign(static_cast<size_t>(bins_), cd(0.0, 0.0));
    time_.assign(static_cast<size_t>(2 * block_), 0.0);
}

void PartitionedConvolver::process(const double* in, double* const* out) {
    // Slide the 2*block window and transform it into the delay line's head.
    std::copy(window_.begin() + block_, window_.end(), window_.begin());
    std::copy(in, in + block_, window_.begin() + block_);
    head_ = (head_ + partitions_ - 1) % partitions_;
    fft_.forward(window_.data(), delay_[static_cast<size_t>(head_)].data());

    for (int c = 0; c < outputs_; ++c) {
        std::fill(accumulate_.begin(), accumulate_.end(), cd(0.0, 0.0));
        const cd* parts = filter_[static_cast<size_t>(c)].data();
        for (int p = 0; p < partitions_; ++p) {
            // Partition p meets the input from p blocks ago.
            const cd* x = delay_[static_cast<size_t>((head_ + p) % partitions_)].data();
            const cd* h = parts + static_cast<size_t>(p * bins_);
            for (int k = 0; k < bins_; ++k) accumulate_[static_cast<size_t>(k)] += x[k] * h[k];
        }
        fft_.inverse(accumulate_.data(), time_.data());
        // Overlap-save: the first half wrapped around, the second is valid.
        std::copy(time_.begin() + block_, time_.end(), out[c]);
    }
}

}  // namespace iceberg::native
