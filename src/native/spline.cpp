#include "native/spline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace iceberg::native {

namespace {

using Piece = std::array<double, 4>;   // c3, c2, c1, c0 from the left break

// Knot slopes of the not-a-knot spline, n >= 4 knots, in the builtin's
// order of operations (see the header).
std::vector<double> not_a_knot_slopes(const std::vector<double>& h,
                                      const std::vector<double>& dd) {
    const size_t n = h.size() + 1;
    std::vector<double> lo(n, 0.0), di(n, 0.0), up(n, 0.0), r(n, 0.0);
    const double d31 = h[0] + h[1];
    const double dn = h[n - 2] + h[n - 3];
    di[0] = h[1];
    up[0] = d31;
    r[0] = (h[1] + h[1]) * dd[0] + (h[0] / d31) * (h[1] * dd[0] + h[0] * dd[1]);
    for (size_t k = 1; k + 1 < n; ++k) {
        lo[k] = h[k];
        di[k] = (h[k - 1] + h[k]) * 2.0;
        up[k] = h[k - 1];
        r[k] = (h[k] * dd[k - 1] + h[k - 1] * dd[k]) * 3.0;
    }
    lo[n - 1] = dn;
    di[n - 1] = h[n - 3];
    r[n - 1] = (h[n - 3] + h[n - 3]) * dd[n - 2] +
               (h[n - 2] / dn) * (h[n - 2] * dd[n - 3] + h[n - 3] * dd[n - 2]);

    for (size_t i = 0; i + 1 < n; ++i) {
        const double w = lo[i + 1] / di[i];
        di[i + 1] -= w * up[i];
        r[i + 1] -= w * r[i];
    }
    r[n - 1] /= di[n - 1];
    for (size_t k = n - 1; k-- > 0;) {
        r[k] = (r[k] - r[k + 1] * up[k]) / di[k];
    }
    return r;
}

}  // namespace

Eigen::VectorXd spline(const Eigen::VectorXd& x,
                       const Eigen::VectorXd& y,
                       const Eigen::VectorXd& xq) {
    if (x.size() != y.size()) {
        throw std::invalid_argument("spline: x and y differ in size");
    }
    if (x.size() < 2) {
        throw std::invalid_argument("spline: need at least 2 points");
    }
    const size_t n = static_cast<size_t>(x.size());
    for (size_t i = 0; i + 1 < n; ++i) {
        if (!(x(static_cast<Eigen::Index>(i)) < x(static_cast<Eigen::Index>(i + 1)))) {
            throw std::invalid_argument("spline: x must be strictly increasing");
        }
    }

    std::vector<double> h(n - 1), dd(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        const auto k = static_cast<Eigen::Index>(i);
        h[i] = x(k + 1) - x(k);
        dd[i] = (y(k + 1) - y(k)) / h[i];
    }

    std::vector<Piece> pieces;
    if (n == 2) {
        pieces.push_back({0.0, 0.0, dd[0], y(0)});
    } else if (n == 3) {
        // The parabola through the three points.
        const double c = (dd[1] - dd[0]) / (h[0] + h[1]);
        pieces.push_back({0.0, c, dd[0] - c * h[0], y(0)});
        pieces.push_back({0.0, c, dd[0] + c * h[0], y(1)});
    } else {
        const auto s = not_a_knot_slopes(h, dd);
        pieces.reserve(n - 1);
        for (size_t i = 0; i + 1 < n; ++i) {
            const double a = ((s[i] + s[i + 1]) - (dd[i] + dd[i])) / h[i];
            pieces.push_back({a / h[i], (dd[i] - s[i]) / h[i] - a, s[i],
                              y(static_cast<Eigen::Index>(i))});
        }
    }

    Eigen::VectorXd out(xq.size());
    const double* breaks = x.data();
    for (Eigen::Index q = 0; q < xq.size(); ++q) {
        const double v = xq(q);
        if (std::isnan(v)) {
            out(q) = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        // The piece whose left break is <= v, among the interior breaks;
        // the end pieces extrapolate.
        const size_t i = static_cast<size_t>(
            std::upper_bound(breaks + 1, breaks + n - 1, v) - (breaks + 1));
        const double t = v - breaks[i];
        const Piece& c = pieces[i];
        out(q) = ((c[0] * t + c[1]) * t + c[2]) * t + c[3];
    }
    return out;
}

}  // namespace iceberg::native
