// Tests the real FFT pair native::rfft and native::irfft: round trips on random
// signals of even, odd and long lengths, the analytic spectra of an impulse and of a
// constant, and agreement with MATLAB fft() on a stored 64-sample randn vector.

#include "native/fft.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <array>
#include <cmath>
#include <complex>

namespace {

using iceberg::test::record_measure;

// MATLAB reference, generated with rng(42); x = randn(64, 1);
// Captured to %.17g.
constexpr std::array<double, 64> kRandnSeed42 = {
    -0.53824389372926962,   0.86723215762957329,   0.97598646347253948,
     0.33739025237921211,  -0.99609409661495396,  -0.52321403168644098,
    -1.2974474771673907,    0.91738858914593557,   0.17660162861754794,
     0.75517993567748831,  -0.59149995977194048,   1.8443896370005026,
     1.816922249179955,    -0.12383335027924647,  -1.1106013550606419,
    -0.68090588026724619,   0.014169326397636454, -0.059550610459064499,
    -0.66101101446033916,   0.30595091506898697,  -0.4090578457905143,
    -1.2818548228848252,   -0.28490282946308648,  -0.06478685589122006,
     1.0003831888389663,   -0.80132929831121413,   0.091800015251789033,
     0.27380479193770302,  -2.1334675594179631,    0.40397728054942095,
    -0.95849940934865763,  -0.3263168518663801,    2.4786956296041733,
     1.5390760520725197,    0.82701089590509713,  -0.15958722588765548,
    -0.8577217490829192,    0.39835396641254128,  -0.22480884254365807,
    -1.3146676437229412,   -0.021796865945491947, -0.88006034099445507,
     1.1086404728571893,   -0.46413826093464672,   0.66456919138124804,
    -0.53013811821816303,   0.040909329870564795,  0.15505390913724837,
    -0.32108498156234999,   2.1458146711040254,    0.80040655725138687,
     1.1962715843610494,    2.0975463826553962,    0.38771501535540298,
     1.0453182014943911,   -1.3718263433146096,   -0.95840715563049539,
    -2.6689399596406402,   -0.80268518189891847,  -0.45101594662272571,
    -0.73658154396130016,   1.1147039555125553,   -0.85335003027021894,
    -0.75875185852041416,
};

// MATLAB fft(x) bins 1..33 (1-based) = C++ bins 0..32 (0-based, the rfft
// output of length N/2+1 = 33). Stored as {real, imag} pairs.
constexpr std::array<std::pair<double, double>, 33> kFftSeed42 = {{
    {-0.43691694509995205,    0.0},
    {-1.7496445453792326,     3.3802574863345352},
    {-4.9507894146666409,    -5.8021835786948337},
    {-10.157231669218255,    -9.4477905208133706},
    {10.029856501630636,     -4.942628156155819},
    {7.8154233649267235,      8.2477206277932851},
    {13.822214995847521,      2.3405030723856899},
    {-6.6363091714406126,     6.5993474701092882},
    {-1.3466108880125023,   -10.610189778828648},
    {-10.4498025278752,       -3.4573030643892011},
    {1.9041894088724307,     -1.6625536178338753},
    {-2.1100328455197488,    -7.1899773770348787},
    {2.424499517433214,     -10.590513591353648},
    {-7.6381713575918306,    -0.49313777412328563},
    {-2.4608674866449505,    -2.7614535573003778},
    {-5.0101740939177501,     6.8480884438542491},
    {3.1711660688215595,     -1.3048796898366781},
    {4.6754358358418076,     -3.5413835794632407},
    {-4.6378296547590772,    -5.6532006623271478},
    {-3.2751854046809177,     1.7889227286523168},
    {0.42576556931082588,    -5.0183103774217157},
    {-0.1486709751052151,     0.03483408432759072},
    {7.9761755841147659,      1.2569120631197788},
    {-4.2613690705971852,     1.9758803023773177},
    {6.1150145844960377,      0.17387154336235255},
    {-5.9511667913126809,    -2.7646693339079067},
    {10.442481808731753,      9.9259464693973971},
    {-1.0500439112794142,     7.1115207258191138},
    {-7.1331004490560233,     1.5224964737163882},
    {2.3866767287160093,     -6.0058729940157578},
    {-4.1166361131788598,    -3.12532313195487},
    {-4.7107659389015835,    -6.9214773930482369},
    {-0.79968757278450486,    0.0},
}};

// Checks that irfft(rfft(x)) returns x for random signals of 16 to 1024 samples
// (powers of two); the RMS of the difference must stay below 1e-12 at every length.
TEST(NativeFft, RoundTripIdentitySmall) {
    double worst = 0.0;
    for (const int n : {16, 32, 64, 256, 1024}) {
        Eigen::VectorXd x = Eigen::VectorXd::Random(n);
        const Eigen::VectorXd y =
            iceberg::native::irfft(iceberg::native::rfft(x), n);

        ASSERT_EQ(y.size(), n);
        const double rms_diff =
            std::sqrt((x - y).array().square().sum() / n);
        EXPECT_LT(rms_diff, 1e-12)
            << "round-trip RMS diff = " << rms_diff << " at N = " << n;
        worst = std::max(worst, rms_diff);
    }
    record_measure("round trip, worst size", worst, 1e-12, "rms");
}

// Checks the spectrum of a unit impulse at sample 0 (256 samples) against the
// analytic result: rfft returns N/2 + 1 bins, each equal to 1 + 0i within 1e-12.
TEST(NativeFft, DeltaInputUnitMagnitudeAllBins) {
    // delta at idx 0 → FFT(x)[k] = 1 for all k (zero phase).
    constexpr int kN = 256;
    Eigen::VectorXd x = Eigen::VectorXd::Zero(kN);
    x(0) = 1.0;

    const Eigen::VectorXcd X = iceberg::native::rfft(x);
    ASSERT_EQ(X.size(), kN / 2 + 1);

    double worst = 0.0;
    for (Eigen::Index k = 0; k < X.size(); ++k) {
        EXPECT_NEAR(X(k).real(), 1.0, 1e-12) << "bin " << k;
        EXPECT_NEAR(X(k).imag(), 0.0, 1e-12) << "bin " << k;
        worst = std::max({worst, std::abs(X(k).real() - 1.0), std::abs(X(k).imag())});
    }
    record_measure("bins vs 1 + 0i, worst", worst, 1e-12, "abs");
}

// Checks the spectrum of a constant signal of ones (256 samples) against the analytic
// result of an unnormalised FFT: the DC bin equals N within 1e-9, and its imaginary
// part and every other bin are zero within 1e-12.
TEST(NativeFft, DcOnlyInputProducesNAtZeroBin) {
    // x = ones(N). FFT (no normalisation) → X[0] = N, rest 0.
    constexpr int kN = 256;
    Eigen::VectorXd x = Eigen::VectorXd::Ones(kN);

    const Eigen::VectorXcd X = iceberg::native::rfft(x);

    EXPECT_NEAR(X(0).real(), static_cast<double>(kN), 1e-9);
    EXPECT_NEAR(X(0).imag(), 0.0, 1e-12);
    double worst = std::abs(X(0).imag());
    for (Eigen::Index k = 1; k < X.size(); ++k) {
        EXPECT_NEAR(std::abs(X(k)), 0.0, 1e-12) << "bin " << k;
        worst = std::max(worst, std::abs(X(k)));
    }
    record_measure("DC bin vs N", std::abs(X(0).real() - static_cast<double>(kN)), 1e-9, "abs");
    record_measure("DC imaginary part and other bins, worst", worst, 1e-12, "abs");
}

// Checks odd lengths (7 to 1023 samples): rfft returns floor(N/2) + 1 bins, and
// irfft(rfft(x)) returns the random input with an RMS difference below 1e-12.
TEST(NativeFft, OddSizeRoundTrip) {
    // Every other test in this suite uses an even N. pocketfft handles
    // odd N natively, but exercises the integer-division branch in
    // `expected_bins = n_time / 2 + 1` (e.g. N=63 → 32 bins, N=65 → 33).
    // If the pchip→FFT chain in calibrate_vbap ever lands on an odd
    // length, this is the path that catches a silent off-by-one.
    double worst = 0.0;
    for (const int n : {7, 31, 63, 65, 127, 1023}) {
        Eigen::VectorXd x = Eigen::VectorXd::Random(n);
        const Eigen::VectorXcd X = iceberg::native::rfft(x);
        EXPECT_EQ(X.size(), n / 2 + 1) << "N=" << n;

        const Eigen::VectorXd y = iceberg::native::irfft(X, n);
        ASSERT_EQ(y.size(), n) << "N=" << n;
        const double rms_diff =
            std::sqrt((x - y).array().square().sum() / n);
        EXPECT_LT(rms_diff, 1e-12)
            << "round-trip RMS diff = " << rms_diff << " at N = " << n;
        worst = std::max(worst, rms_diff);
    }
    record_measure("round trip, worst odd size", worst, 1e-12, "rms");
}

// Checks the round trip irfft(rfft(x)) on a random signal of 132300 samples (3 s at
// 44.1 kHz, a length met in rendering); the RMS of the difference must stay below 1e-12.
TEST(NativeFft, RoundTripPipelineSize) {
    // Pipeline-realistic size: 132300 samples (3 s @ 44.1 kHz).
    constexpr int kN = 132300;
    Eigen::VectorXd x = Eigen::VectorXd::Random(kN);

    const Eigen::VectorXcd X = iceberg::native::rfft(x);
    const Eigen::VectorXd y = iceberg::native::irfft(X, kN);
    ASSERT_EQ(y.size(), kN);
    const double rms_diff =
        std::sqrt((x - y).array().square().sum() / kN);
    record_measure("round trip, 132300 samples", rms_diff, 1e-12, "rms");
    EXPECT_LT(rms_diff, 1e-12);
}

// Checks rfft against MATLAB fft() on a 64-sample randn vector (rng(42)) stored in this
// file to 17 significant digits: the real and imaginary parts of the 33 bins from DC to
// Nyquist must match the stored MATLAB output within 1e-10.
TEST(NativeFft, MatchesMatlabRandnSeed42) {
    Eigen::VectorXd x = Eigen::Map<const Eigen::VectorXd>(
        kRandnSeed42.data(), 64);

    const Eigen::VectorXcd X = iceberg::native::rfft(x);
    ASSERT_EQ(X.size(), 33);

    double worst = 0.0;
    for (Eigen::Index k = 0; k < 33; ++k) {
        EXPECT_NEAR(X(k).real(), kFftSeed42[k].first,  1e-10) << "bin " << k;
        EXPECT_NEAR(X(k).imag(), kFftSeed42[k].second, 1e-10) << "bin " << k;
        worst = std::max({worst, std::abs(X(k).real() - kFftSeed42[k].first),
                          std::abs(X(k).imag() - kFftSeed42[k].second)});
    }
    record_measure("fft(randn(64)) vs MATLAB, worst bin", worst, 1e-10, "abs");
}

}  // namespace
