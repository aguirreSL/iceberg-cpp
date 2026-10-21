#include "native/pchip.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace iceberg::native {

namespace {

inline int sign_of(double x) {
    return (x > 0.0) - (x < 0.0);
}

// Fritsch–Carlson endpoint derivative + sign / 3·slope clamps.
double endpoint_derivative(double h_near, double h_far,
                           double m_near, double m_far) {
    double d = ((2.0 * h_near + h_far) * m_near - h_near * m_far) /
               (h_near + h_far);
    if (sign_of(d) != sign_of(m_near)) {
        d = 0.0;
    } else if ((sign_of(m_near) != sign_of(m_far)) &&
               (std::abs(d) > 3.0 * std::abs(m_near))) {
        d = 3.0 * m_near;
    }
    return d;
}

}  // namespace

Eigen::VectorXd pchip(const Eigen::VectorXd& x,
                      const Eigen::VectorXd& y,
                      const Eigen::VectorXd& xq) {
    if (x.size() != y.size()) {
        throw std::invalid_argument("pchip: x and y size mismatch");
    }
    const Eigen::Index n = x.size();
    if (n < 2) {
        throw std::invalid_argument("pchip: need at least 2 nodes");
    }

    Eigen::VectorXd h(n - 1);  // interval widths
    Eigen::VectorXd m(n - 1);  // secant slopes
    for (Eigen::Index k = 0; k < n - 1; ++k) {
        h(k) = x(k + 1) - x(k);
        if (h(k) <= 0.0) {
            throw std::invalid_argument(
                "pchip: x must be strictly increasing");
        }
        m(k) = (y(k + 1) - y(k)) / h(k);
    }

    Eigen::VectorXd d(n);  // node derivatives

    if (n == 2) {
        d(0) = m(0);
        d(1) = m(0);
    } else {
        d(0) = endpoint_derivative(h(0), h(1), m(0), m(1));
        for (Eigen::Index k = 1; k < n - 1; ++k) {
            if (m(k - 1) * m(k) <= 0.0) {
                d(k) = 0.0;
            } else {
                const double w1 = 2.0 * h(k) + h(k - 1);
                const double w2 = h(k) + 2.0 * h(k - 1);
                d(k) = (w1 + w2) / (w1 / m(k - 1) + w2 / m(k));
            }
        }
        d(n - 1) = endpoint_derivative(h(n - 2), h(n - 3),
                                       m(n - 2), m(n - 3));
    }

    Eigen::VectorXd yq(xq.size());
    for (Eigen::Index i = 0; i < xq.size(); ++i) {
        const double xi = xq(i);

        // Locate interval k such that x(k) <= xi < x(k+1).
        Eigen::Index k;
        if (xi <= x(0)) {
            k = 0;  // extrapolate using leftmost cubic
        } else if (xi >= x(n - 1)) {
            k = n - 2;  // extrapolate using rightmost cubic
        } else {
            const auto it =
                std::upper_bound(x.data(), x.data() + n, xi);
            k = static_cast<Eigen::Index>((it - x.data()) - 1);
            // Clamp defensively (upper_bound on equal-to-x(0) edge etc.)
            if (k < 0) k = 0;
            if (k > n - 2) k = n - 2;
        }

        const double t = (xi - x(k)) / h(k);
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;
        yq(i) = h00 * y(k) +
                h10 * h(k) * d(k) +
                h01 * y(k + 1) +
                h11 * h(k) * d(k + 1);
    }

    return yq;
}

}  // namespace iceberg::native
