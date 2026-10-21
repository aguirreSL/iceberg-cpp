#include "native/fft.h"

#include <pocketfft_hdronly.h>

#include <stdexcept>
#include <string>

namespace iceberg::native {

Eigen::VectorXcd rfft(const Eigen::VectorXd& time) {
    const size_t n = static_cast<size_t>(time.size());
    Eigen::VectorXcd out(static_cast<Eigen::Index>(n / 2 + 1));

    if (n == 0) {
        return out;
    }

    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t stride_in{
        static_cast<ptrdiff_t>(sizeof(double))};
    const pocketfft::stride_t stride_out{
        static_cast<ptrdiff_t>(sizeof(std::complex<double>))};
    constexpr size_t kAxis = 0;
    constexpr double kFct = 1.0;  // MATLAB fft: no normalisation on forward.

    pocketfft::r2c<double>(shape, stride_in, stride_out, kAxis,
                           pocketfft::FORWARD, time.data(), out.data(), kFct);
    return out;
}

Eigen::VectorXd irfft(const Eigen::VectorXcd& freq, int n_time) {
    const Eigen::Index expected_bins =
        static_cast<Eigen::Index>(n_time) / 2 + 1;
    if (freq.size() != expected_bins) {
        throw std::invalid_argument(
            "native::irfft: freq.size() = " + std::to_string(freq.size()) +
            " does not match n_time/2+1 = " +
            std::to_string(expected_bins));
    }

    Eigen::VectorXd out(n_time);
    if (n_time == 0) {
        return out;
    }

    const pocketfft::shape_t shape{static_cast<size_t>(n_time)};
    const pocketfft::stride_t stride_in{
        static_cast<ptrdiff_t>(sizeof(std::complex<double>))};
    const pocketfft::stride_t stride_out{
        static_cast<ptrdiff_t>(sizeof(double))};
    constexpr size_t kAxis = 0;
    // MATLAB ifft: divide by N. Matches `irfft(rfft(x), x.size()) == x`.
    const double fct = 1.0 / static_cast<double>(n_time);

    pocketfft::c2r<double>(shape, stride_in, stride_out, kAxis,
                           pocketfft::BACKWARD, freq.data(), out.data(), fct);
    return out;
}

}  // namespace iceberg::native
