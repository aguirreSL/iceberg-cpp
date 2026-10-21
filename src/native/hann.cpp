#include "native/hann.h"

#include <cmath>

namespace iceberg::native {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

Eigen::ArrayXd hann(int n) {
    if (n <= 0) {
        return Eigen::ArrayXd();
    }
    Eigen::ArrayXd w(n);
    if (n == 1) {
        w(0) = 0.5;
        return w;
    }
    const double denom = static_cast<double>(n - 1);
    for (int k = 0; k < n; ++k) {
        w(k) = 0.5 *
               (1.0 - std::cos(2.0 * kPi * static_cast<double>(k) / denom));
    }
    return w;
}

Eigen::ArrayXd hanning(int n) {
    if (n <= 0) {
        return Eigen::ArrayXd();
    }
    Eigen::ArrayXd w(n);
    if (n == 1) {
        w(0) = 0.5;
        return w;
    }
    const double denom = static_cast<double>(n + 1);
    for (int k = 0; k < n; ++k) {
        w(k) = 0.5 * (1.0 - std::cos(2.0 * kPi *
                                     static_cast<double>(k + 1) / denom));
    }
    return w;
}

}  // namespace iceberg::native
