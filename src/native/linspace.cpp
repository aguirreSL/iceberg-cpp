#include "native/linspace.h"

#include <stdexcept>

namespace iceberg::native {

Eigen::VectorXd linspace(double d1, double d2, Eigen::Index n) {
    if (n < 1) {
        throw std::invalid_argument("linspace: n must be >= 1");
    }
    if (d1 == -d2 && d1 != 0.0 && n > 2) {
        throw std::invalid_argument(
            "linspace: symmetric range (d1 == -d2) is not implemented");
    }
    Eigen::VectorXd y(n);
    if (n == 1) {
        y(0) = d2;   // MATLAB: a scalar result takes the end point
        return y;
    }
    const double n1 = static_cast<double>(n - 1);
    for (Eigen::Index k = 0; k < n; ++k) {
        // matlab: y = d1 + (0:n1).*(d2 - d1)./n1
        y(k) = d1 + (static_cast<double>(k) * (d2 - d1)) / n1;
    }
    y(0) = d1;
    y(n - 1) = d2;
    return y;
}

}  // namespace iceberg::native
