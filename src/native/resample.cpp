#include "native/resample.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace iceberg::native {

namespace {

constexpr double kPi = 3.14159265358979323846;

// MATLAB sinpi: sin(pi*x), exactly 0 at the integers. The argument is
// reduced exactly (fmod, reflections) before the single sin() call.
double sinpi(double x) {
    if (x == std::floor(x)) return 0.0;
    double r = std::fmod(x, 2.0);
    if (r < 0.0) r += 2.0;
    double sign = 1.0;
    if (r >= 1.0) {
        r -= 1.0;
        sign = -1.0;
    }
    if (r > 0.5) r = 1.0 - r;
    return sign * std::sin(kPi * r);
}

// MATLAB sinc: sinpi(x) ./ (pi*x), 1 at 0.
double sinc(double x) {
    return x == 0.0 ? 1.0 : sinpi(x) / (kPi * x);
}

// Modified Bessel function of the first kind, order 0, by its power
// series; converges to double precision well beyond the beta = 5 used here.
double bessel_i0(double x) {
    const double t = (x / 2.0) * (x / 2.0);
    double term = 1.0;
    double sum = 1.0;
    for (int k = 1; k < 1000; ++k) {
        term *= t / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term <= sum * std::numeric_limits<double>::epsilon() / 4.0) break;
    }
    return sum;
}

long long ceil_div(long long a, long long b) {   // a >= 0, b > 0
    return (a + b - 1) / b;
}

}  // namespace

std::pair<long long, long long> rat(double x, double tol) {
    if (!std::isfinite(x)) {
        throw std::invalid_argument("rat: x must be finite");
    }
    const double target = x;
    const double eps_x =
        std::nextafter(std::abs(target), std::numeric_limits<double>::infinity()) -
        std::abs(target);
    // matlab: C = [n(k) n(k-1); d(k) d(k-1)], C = [C*[d;1] C(:,1)]
    double n1 = 1.0, n0 = 0.0, d1 = 0.0, d0 = 1.0;
    for (int k = 0; k < 64; ++k) {
        const double d = std::round(x);
        x -= d;
        const double n_new = n1 * d + n0;
        const double d_new = d1 * d + d0;
        n0 = n1;
        d0 = d1;
        n1 = n_new;
        d1 = d_new;
        if (x == 0.0 || std::abs(n1 / d1 - target) <= std::max(tol, eps_x)) break;
        x = 1.0 / x;
    }
    const double sign = d1 < 0.0 ? -1.0 : 1.0;
    return {static_cast<long long>(n1 / sign), static_cast<long long>(std::abs(d1))};
}

Eigen::VectorXd kaiser(Eigen::Index n, double beta) {
    if (n < 1) throw std::invalid_argument("kaiser: n must be >= 1");
    Eigen::VectorXd w(n);
    if (n == 1) {
        w(0) = 1.0;
        return w;
    }
    // matlab (MATLAB target branch of kaiser.m):
    //   isOdd = mod(L,2) == 1;  xi = (0:(L+isOdd)/2-1)' + 0.5*(1-isOdd);
    //   w = besseli(0, beta*sqrt(1 - 4*xi.^2/(L-1)^2)) / besseli(0, beta);
    //   w = [w((L+isOdd)/2:-1:isOdd+1); w]
    const Eigen::Index is_odd = n % 2;
    const Eigen::Index m = (n + is_odd) / 2;
    const double bes = std::abs(bessel_i0(beta));
    const double denom = static_cast<double>(n - 1) * static_cast<double>(n - 1);
    Eigen::VectorXd half(m);
    for (Eigen::Index j = 0; j < m; ++j) {
        const double xi = static_cast<double>(j) + 0.5 * static_cast<double>(1 - is_odd);
        half(j) = std::abs(bessel_i0(beta * std::sqrt(1.0 - (4.0 * xi * xi) / denom)) / bes);
    }
    // c++: the left half mirrors half(m-1 .. is_odd), then half(0 .. m-1)
    Eigen::Index o = 0;
    for (Eigen::Index j = m - 1; j >= is_odd; --j) w(o++) = half(j);
    for (Eigen::Index j = 0; j < m; ++j) w(o++) = half(j);
    return w;
}

Eigen::MatrixXd resample(const Eigen::MatrixXd& x, long long p, long long q) {
    if (p < 1 || q < 1) {
        throw std::invalid_argument("resample: p and q must be >= 1");
    }
    const long long g = std::gcd(p, q);
    p /= g;
    q /= g;
    if (p == q) return x;

    // Filter: firls(L-1, [0 2fc 2fc 1], [1 1 0 0]) .* kaiser(L, 5), sum p.
    // For this type I full-band design with constant weights firls solves
    // nothing: centre 2*fc, side taps (4*(fc*sinc(2k*fc)))/2.
    constexpr long long kN = 10;
    constexpr double kBeta = 5.0;
    const long long pqmax = std::max(p, q);
    const double fc = 1.0 / 2.0 / static_cast<double>(pqmax);
    const long long L = 2 * kN * pqmax + 1;
    const long long half = (L - 1) / 2;
    Eigen::VectorXd h(L);
    h(half) = 2.0 * fc;
    for (long long k = 1; k <= half; ++k) {
        const double a = 4.0 * (fc * sinc((2.0 * static_cast<double>(k)) * fc));
        h(half - k) = a / 2.0;
        h(half + k) = a / 2.0;
    }
    h = h.cwiseProduct(kaiser(L, kBeta));
    h = (static_cast<double>(p) * h) / h.sum();

    // Delay compensation, as MATLAB's findDelayAndZeroPadFilter: zeros in
    // front so down-sampling by q hits the centre tap, zeros behind so the
    // output reaches ceil(Lx*p/q) samples.
    const long long lx = static_cast<long long>(x.rows());
    const double l_half = static_cast<double>(L - 1) / 2.0;
    const long long n_zero_begin = static_cast<long long>(
        std::floor(static_cast<double>(q) - std::fmod(l_half, static_cast<double>(q))));
    const long long delay = static_cast<long long>(
        std::floor(std::ceil(l_half + static_cast<double>(n_zero_begin)) / static_cast<double>(q)));
    const long long y_len = ceil_div(lx * p, q);
    const long long len_h = L + n_zero_begin;
    long long n_zero_end = 0;
    while (lx > 0 &&
           ceil_div((lx - 1) * p + len_h + n_zero_end, q) - delay < y_len) {
        ++n_zero_end;
    }
    Eigen::VectorXd hp = Eigen::VectorXd::Zero(len_h + n_zero_end);
    hp.segment(n_zero_begin, L) = h;
    const long long lh = hp.size();

    // upfirdn(x, hp, p, q), keeping outputs delay .. delay + y_len - 1:
    // y[m] = sum_i x[i] * hp[m*q - i*p] over 0 <= m*q - i*p < lh.
    Eigen::MatrixXd y = Eigen::MatrixXd::Zero(y_len, x.cols());
    for (long long m = 0; m < y_len; ++m) {
        const long long n = (delay + m) * q;
        const long long i_min = std::max<long long>(0, n >= lh ? ceil_div(n - lh + 1, p) : 0);
        const long long i_max = std::min<long long>(lx - 1, n / p);
        for (Eigen::Index c = 0; c < x.cols(); ++c) {
            double acc = 0.0;
            for (long long i = i_min; i <= i_max; ++i) {
                acc += x(i, c) * hp(n - i * p);
            }
            y(m, c) = acc;
        }
    }
    return y;
}

}  // namespace iceberg::native
