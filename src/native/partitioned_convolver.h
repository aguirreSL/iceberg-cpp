#pragma once

#include "native/realtime_fft.h"

#include <Eigen/Dense>

#include <complex>
#include <vector>

namespace iceberg::native {

/// Uniformly partitioned overlap-save convolution of one input stream with
/// several filters (one per output), block by block, for the audio
/// thread: the live engine runs the late branch's IR tail (4 B-format
/// channels) through it. Fed the whole input and then zeros, it gives the
/// linear convolution `conv(input, filters.col(c))`, to rounding.
///
/// Filters are split into `block`-sample partitions, each kept as the
/// spectrum of a 2*block FFT; every block of input is transformed once,
/// kept in a frequency-domain delay line, and each output is the inverse
/// FFT of the delay line times its partitions, last half kept.
///
/// Construction allocates; `process` does not.
class PartitionedConvolver {
public:
    /// `filters`: (taps x outputs), at least one tap and one output.
    /// `block`: a power of 2, at least 2. Throws `std::invalid_argument`
    /// otherwise.
    PartitionedConvolver(const Eigen::MatrixXd& filters, int block);

    /// `in`: one block of samples; `out[c]`: one block per output.
    void process(const double* in, double* const* out);

private:
    int block_;
    int outputs_;
    int partitions_;
    int bins_;
    RealFft fft_;
    // filter_[c][p]: spectrum of partition p of output c, bins_ values.
    std::vector<std::vector<std::complex<double>>> filter_;
    // The input spectra of the last `partitions_` blocks, newest at head_.
    std::vector<std::vector<std::complex<double>>> delay_;
    int head_ = 0;
    std::vector<double> window_;                   // the last 2*block input samples
    std::vector<std::complex<double>> accumulate_;
    std::vector<double> time_;
};

}  // namespace iceberg::native
