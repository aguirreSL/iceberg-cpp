// Tests native::spline, the not-a-knot cubic spline used in the fractional-octave
// smoothing of spectra: agreement with MATLAB interp1(..., 'spline'), exact
// reproduction of a cubic, and the rejection of invalid knots.

#include "native/spline.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using iceberg::test::record_measure;

Eigen::VectorXd vec(const std::vector<double>& v) {
    return Eigen::Map<const Eigen::VectorXd>(v.data(), static_cast<Eigen::Index>(v.size()));
}

// Checks spline on three knot sets (6, 3 and 2 knots) against interp1(x, y, xq, 'spline')
// printed by MATLAB R2026b (26.2.0.3386108, macOS arm64) with %.17g: queries before, on
// and between the knots, and after them (extrapolated by the end pieces). Every query
// must agree within 1e-14 relative to max(1, |reference|).
TEST(NativeSpline, MatchesMatlabInterp1Spline) {
    struct Case {
        std::vector<double> x, y, xq, expected;
    };
    const std::vector<Case> cases = {
        {{0, 0.7, 1.1, 2.5, 3, 4.2},
         {1, -0.3, 0.8, 2.2, -1.4, 0.5},
         {-0.5, 0, 0.35, 0.7, 1.9, 3, 4.2, 5.1},
         {6.5731832938085084, 1, -0.36707134150617637, -0.29999999999999999, 3.1543407736735354,
          -1.3999999999999999, 0.50000000000000111, 38.780398874254075}},
        {{0, 1, 2.5}, {2, -1, 0.5}, {-1, 0.5, 1, 2, 3},
         {8.1999999999999993, 0.10000000000000009, -1, -0.79999999999999982, 2.6000000000000005}},
        {{1, 3}, {0.5, -1.5}, {0, 2, 3, 4}, {1.5, -0.5, -1.5, -2.5}},
    };
    double worst = 0.0;
    for (const auto& c : cases) {
        const Eigen::VectorXd got = iceberg::native::spline(vec(c.x), vec(c.y), vec(c.xq));
        for (size_t i = 0; i < c.expected.size(); ++i) {
            EXPECT_NEAR(got(static_cast<Eigen::Index>(i)), c.expected[i],
                        1e-14 * std::max(1.0, std::abs(c.expected[i])))
                << c.x.size() << " knots, xq = " << c.xq[i];
            worst = std::max(worst, std::abs(got(static_cast<Eigen::Index>(i)) - c.expected[i]) /
                                        std::max(1.0, std::abs(c.expected[i])));
        }
    }
    record_measure("vs interp1 'spline', worst query (relative to max(1, |ref|))", worst, 1e-14,
                   "rel");
}

// Checks that the not-a-knot spline reproduces a cubic polynomial (its third derivative
// is continuous everywhere): on seven irregular knots, seven queries inside and outside
// the knot range must equal the polynomial within 1e-11.
TEST(NativeSpline, ReproducesACubic) {
    const auto f = [](double t) { return 0.5 * t * t * t - 2.0 * t * t + t - 3.0; };
    std::vector<double> x = {-1.0, -0.2, 0.5, 1.3, 2.0, 3.7, 4.1};
    std::vector<double> y;
    for (double t : x) y.push_back(f(t));
    const std::vector<double> q = {-2.0, -0.6, 0.0, 0.9, 2.5, 4.0, 5.0};
    const Eigen::VectorXd got = iceberg::native::spline(vec(x), vec(y), vec(q));
    double worst = 0.0;
    for (size_t i = 0; i < q.size(); ++i) {
        EXPECT_NEAR(got(static_cast<Eigen::Index>(i)), f(q[i]), 1e-11) << q[i];
        worst = std::max(worst, std::abs(got(static_cast<Eigen::Index>(i)) - f(q[i])));
    }
    record_measure("vs the cubic, worst query", worst, 1e-11, "abs");
}

// Checks that spline rejects invalid knots with std::invalid_argument: a single knot,
// a repeated knot position, and x and y of different lengths.
TEST(NativeSpline, RejectsBadKnots) {
    EXPECT_THROW(iceberg::native::spline(vec({0.0}), vec({1.0}), vec({0.0})),
                 std::invalid_argument);
    EXPECT_THROW(iceberg::native::spline(vec({0.0, 1.0, 1.0}), vec({0.0, 1.0, 2.0}), vec({0.5})),
                 std::invalid_argument);
    EXPECT_THROW(iceberg::native::spline(vec({0.0, 1.0}), vec({0.0}), vec({0.5})),
                 std::invalid_argument);
}

}  // namespace
