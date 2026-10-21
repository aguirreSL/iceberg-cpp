// Tests native::pchip, the shape-preserving piecewise cubic Hermite interpolation used
// to bring calibration filters onto frequency bins: agreement with MATLAB pchip() on a
// stored calibration curve, input validation, monotonicity, and exactness at the nodes.

#include "native/pchip.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <array>

namespace {

using iceberg::test::record_measure;

// LS 1 calibration filter from fixtures/calibration.json. Captured
// verbatim — the JSON values are float32 (note the trailing zeros that
// reveal float32 representation).
// 1/3-octave-ish band centers as stored in the calibration JSON. NOTE:
// node 6 is 155, not 160 — the calibration data uses a non-ISO band
// center here. The pchip cross-check tolerance (1e-10) catches a
// misreading of 155 as 160 instantly because a 5 Hz error at
// xq=222.7 Hz produces a ~2e-4 amplitude diff.
constexpr std::array<double, 27> kLs1FreqVector = {
    50, 62.5, 80, 100, 125, 155, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6350, 8000,
    10000, 12500, 16000, 20000,
};
constexpr std::array<double, 27> kLs1FreqMagnitude = {
    3.5122578144073486,  2.0764956474304199,  1.5668996572494507,
    1.3503295183181763,  1.2525439262390137,  1.1812783479690552,
    1.144555926322937,   1.1189794540405273,  1.0865253210067749,
    1.1037219762802124,  1.2069544792175293,  1.2969951629638672,
    1.2004158496856689,  1.1200867891311646,  1.0728346109390259,
    1.0910677909851074,  1.030931830406189,   1.0191425085067749,
    1.074174165725708,   1.0960088968276978,  1.1042088270187378,
    1.0930596590042114,  1.1617119312286377,  1.282647967338562,
    1.3831750154495239,  1.6129924058914185,  1.6129924058914185,
};

// MATLAB pchip(x, y, linspace(0, 22050, 100)), captured from MATLAB
// R2026a.
constexpr std::array<double, 100> kPchipMatlabRef = {
    -12.898380656859814,    1.1320862804501275,    1.1433892133969519,
    1.28771167412703,       1.158797749112958,     1.0908654002877995,
    1.0756172364082706,     1.0903787344320541,    1.0669772734456009,
    1.0307419401016344,     1.0230059041673794,    1.019284082133374,
    1.02737457567016,       1.0516615481297231,    1.0726294929260953,
    1.0811990133527809,     1.0877524401138914,    1.0926777483796772,
    1.0961234557399684,     1.0988512230121508,    1.1012562965783423,
    1.1030872596488053,     1.1040926954338017,    1.1039491544353792,
    1.102392287981689,      1.0999464568094675,    1.0972124784526187,
    1.0947911704450464,     1.0932833503206543,    1.0935901812629922,
    1.0976597720968886,     1.1050472260295634,    1.1149195447077398,
    1.1264437297781409,     1.1387867828874894,    1.1511157056825079,
    1.1626086520916943,     1.1744443273035776,    1.1875857050773277,
    1.2016471880736448,     1.2162431789532289,    1.2309880803767805,
    1.2454962950049997,     1.2593822254985869,    1.2722602745182425,
    1.2837486486298404,     1.2940095693678617,    1.3034461229762893,
    1.3122435330183171,     1.3205870230571395,    1.3286618166559496,
    1.3366531373779422,     1.3447462087863109,    1.3531262544442497,
    1.3619784979149525,     1.3714881627616131,    1.3818404725474258,
    1.3938208650718196,     1.4081915868043591,    1.4245215717024204,
    1.4423664943449859,     1.4612820293110369,    1.4808238511795555,
    1.5005476345295234,     1.5200090539399227,    1.5387637839897355,
    1.5563674992579428,     1.572375874323527,     1.5863445837654702,
    1.5978293021627537,     1.6063857040943594,    1.6115694641392697,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,     1.6129924058914185,    1.6129924058914185,
    1.6129924058914185,
};

// Checks pchip on the loudspeaker 1 filter of the reference calibration.json (27 band
// centres and magnitudes), queried at 100 points from 0 to 22050 Hz with extrapolation
// at both ends, against MATLAB pchip() output stored in this file; every point must
// agree within 1e-10.
TEST(NativePchip, MatchesMatlabOnLs1Filter) {
    Eigen::VectorXd x = Eigen::Map<const Eigen::VectorXd>(
        kLs1FreqVector.data(), 27);
    Eigen::VectorXd y = Eigen::Map<const Eigen::VectorXd>(
        kLs1FreqMagnitude.data(), 27);
    Eigen::VectorXd xq = Eigen::VectorXd::LinSpaced(100, 0.0, 22050.0);

    const Eigen::VectorXd yq = iceberg::native::pchip(x, y, xq);

    ASSERT_EQ(yq.size(), 100);
    double worst = 0.0;
    for (Eigen::Index i = 0; i < 100; ++i) {
        EXPECT_NEAR(yq(i), kPchipMatlabRef[i], 1e-10) << "i = " << i;
        worst = std::max(worst, std::abs(yq(i) - kPchipMatlabRef[i]));
    }
    record_measure("LS 1 filter vs MATLAB, worst query", worst, 1e-10, "abs");
}

// Checks that pchip rejects invalid nodes with std::invalid_argument: x and y of
// different lengths, a single node, and node positions that are not strictly increasing.
TEST(NativePchip, ThrowsOnInvalidInputs) {
    Eigen::VectorXd x(3); x << 0, 1, 2;
    Eigen::VectorXd y(2); y << 0, 1;
    EXPECT_THROW(
        iceberg::native::pchip(x, y, Eigen::VectorXd::Zero(1)),
        std::invalid_argument);

    Eigen::VectorXd x_short(1); x_short << 0;
    Eigen::VectorXd y_short(1); y_short << 0;
    EXPECT_THROW(
        iceberg::native::pchip(x_short, y_short,
                               Eigen::VectorXd::Zero(1)),
        std::invalid_argument);

    Eigen::VectorXd x_dup(3); x_dup << 0, 1, 1;  // not strictly increasing
    Eigen::VectorXd y_dup(3); y_dup << 0, 1, 2;
    EXPECT_THROW(
        iceberg::native::pchip(x_dup, y_dup,
                               Eigen::VectorXd::Zero(1)),
        std::invalid_argument);
}

// Checks that pchip preserves monotonicity: on increasing data (y = x^2 at six uniform
// nodes) and on decreasing data (seven log-spaced nodes, a filter-like roll-off), no
// step between consecutive interpolated values may go against the trend of the data by
// more than 1e-12.
TEST(NativePchip, MonotonicityPreservedOnMonotonicData) {
    // The whole reason Fritsch–Carlson is preferred over plain cubic
    // spline: it preserves monotonicity. If a future refactor swaps
    // pchip for any non-monotonic interpolator, this test fires even
    // when sample-by-sample diff is sub-tolerance — a non-monotonic
    // wiggle around 1e-6 would pass the 1e-10 cross-check on a
    // different node set but lose the perceptual quality the
    // calibration filter relies on (smooth roll-off).
    {
        // Strictly increasing y on uniform x.
        Eigen::VectorXd x(6);
        Eigen::VectorXd y(6);
        x << 0, 1, 2, 3, 4, 5;
        y << 0, 1, 4, 9, 16, 25;
        Eigen::VectorXd xq = Eigen::VectorXd::LinSpaced(200, 0.0, 5.0);

        const Eigen::VectorXd yq = iceberg::native::pchip(x, y, xq);

        double largest_drop = -std::numeric_limits<double>::infinity();
        for (Eigen::Index i = 0; i + 1 < yq.size(); ++i) {
            EXPECT_LE(yq(i), yq(i + 1) + 1e-12)
                << "non-monotonic increasing at i = " << i
                << "  yq(i) = " << yq(i)
                << "  yq(i+1) = " << yq(i + 1);
            largest_drop = std::max(largest_drop, yq(i) - yq(i + 1));
        }
        record_measure("increasing data, largest drop", largest_drop, 1e-12, "abs");
    }
    {
        // Strictly decreasing y on log-spaced x — closer to the actual
        // calibration filter shape at high frequencies.
        Eigen::VectorXd x(7);
        Eigen::VectorXd y(7);
        x << 100, 200, 500, 1000, 2000, 5000, 10000;
        y << 5.0, 3.0, 2.5, 2.0, 1.5, 1.2, 1.0;
        Eigen::VectorXd xq = Eigen::VectorXd::LinSpaced(500, 100.0, 10000.0);

        const Eigen::VectorXd yq = iceberg::native::pchip(x, y, xq);

        double largest_rise = -std::numeric_limits<double>::infinity();
        for (Eigen::Index i = 0; i + 1 < yq.size(); ++i) {
            EXPECT_GE(yq(i), yq(i + 1) - 1e-12)
                << "non-monotonic decreasing at i = " << i
                << "  yq(i) = " << yq(i)
                << "  yq(i+1) = " << yq(i + 1);
            largest_rise = std::max(largest_rise, yq(i + 1) - yq(i));
        }
        record_measure("decreasing data, largest rise", largest_rise, 1e-12, "abs");
    }
}

// Checks that pchip reproduces the data at its own nodes: queried at the 27 band
// centres of the loudspeaker 1 filter, it must return the stored magnitudes within 1e-13.
TEST(NativePchip, PassesThroughNodes) {
    // pchip is exact at every node by construction.
    Eigen::VectorXd x = Eigen::Map<const Eigen::VectorXd>(
        kLs1FreqVector.data(), 27);
    Eigen::VectorXd y = Eigen::Map<const Eigen::VectorXd>(
        kLs1FreqMagnitude.data(), 27);

    const Eigen::VectorXd yq = iceberg::native::pchip(x, y, x);

    double worst = 0.0;
    for (Eigen::Index i = 0; i < y.size(); ++i) {
        EXPECT_NEAR(yq(i), y(i), 1e-13) << "node " << i;
        worst = std::max(worst, std::abs(yq(i) - y(i)));
    }
    record_measure("at the nodes, worst", worst, 1e-13, "abs");
}

}  // namespace
