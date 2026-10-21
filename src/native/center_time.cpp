#include "native/center_time.h"

#include "native/start_ir.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace iceberg::native {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kRealmin = std::numeric_limits<double>::min();

double db(const double x) { return 10.0 * std::log10(x); }

// ISO 3382-1 onset of a single column, 0-based.
Eigen::Index onset(const Eigen::VectorXd& x) {
    const Eigen::MatrixXd column(x);
    return start_ir(column, 20.0)(0);
}

// Element r moves to (r + k) mod n, so a negative k moves the signal earlier.
// Requires a non-empty `x`.
Eigen::VectorXd circular_shift(const Eigen::VectorXd& x, const Eigen::Index k) {
    const Eigen::Index n = x.size();
    const Eigen::Index shift = ((k % n) + n) % n;
    Eigen::VectorXd out(n);
    for (Eigen::Index r = 0; r < n; ++r) {
        out[(r + shift) % n] = x[r];
    }
    return out;
}

// Block length from a real-valued estimate. Checked as a double, before the
// conversion, so a NaN or a value below one sample never reaches the integer
// division in `average`.
Eigen::Index block_length(const double samples) {
    const double length = std::round(samples);
    if (!(length >= 1.0)) {
        throw std::runtime_error("center_time: block length below one sample");
    }
    return static_cast<Eigen::Index>(length);
}

// Mean energy of the complete blocks of `length` samples within the first
// `usable` samples; incomplete trailing samples are ignored.
struct Blocks {
    Eigen::Index length = 0;
    Eigen::Index count = 0;
    Eigen::VectorXd w;      // mean energy per block
    Eigen::VectorXd level;  // dB(w)
    Eigen::VectorXd time;   // start time of each block, s
};

Blocks average(const Eigen::VectorXd& energy, const Eigen::Index usable,
               const Eigen::Index length, const double fs) {
    Blocks blocks;
    blocks.length = length;
    blocks.count = usable / length;
    blocks.w.resize(blocks.count);
    blocks.level.resize(blocks.count);
    blocks.time.resize(blocks.count);
    for (Eigen::Index j = 0; j < blocks.count; ++j) {
        const Eigen::Index begin = j * length;
        double sum = 0.0;
        for (Eigen::Index i = begin; i < begin + length; ++i) {
            sum += energy[i];
        }
        blocks.w[j] = sum / static_cast<double>(length);
        blocks.level[j] = db(blocks.w[j]);
        blocks.time[j] = static_cast<double>(j) * static_cast<double>(length) / fs;
    }
    return blocks;
}

// Strict `>` keeps the earliest index among equal maxima.
Eigen::Index first_max(const Eigen::VectorXd& w) {
    Eigen::Index best = 0;
    for (Eigen::Index k = 1; k < w.size(); ++k) {
        if (w[k] > w[best]) {
            best = k;
        }
    }
    return best;
}

// Mean of the 1-based blocks `first`..count, plus realmin so that the noise
// level in dB stays finite on an all-zero tail.
double noise_energy(const Blocks& blocks, const Eigen::Index first) {
    double sum = 0.0;
    // 1-based first -> 0-based first - 1
    for (Eigen::Index j = first - 1; j < blocks.count; ++j) {
        sum += blocks.w[j];
    }
    return sum / static_cast<double>(blocks.count - first + 1) + kRealmin;
}

struct Line {
    double c0 = kNaN;  // level at t = 0, dB
    double c1 = kNaN;  // slope, dB/s
};

// Least-squares line through (time, level) of blocks first..last inclusive.
// Column-pivoting QR is what MATLAB's backslash uses for this tall system,
// so the coefficients round the same way.
Line fit(const Blocks& blocks, const Eigen::Index first, const Eigen::Index last) {
    const Eigen::Index count = last - first + 1;
    Eigen::MatrixXd a(count, 2);
    a.col(0).setOnes();
    a.col(1) = blocks.time.segment(first, count);
    const Eigen::VectorXd y = blocks.level.segment(first, count);
    const Eigen::VectorXd c = a.colPivHouseholderQr().solve(y);
    return {c[0], c[1]};
}

// Result of the crossing-point search: all NaN when the method fails.
struct Decay {
    double reverberation_time = kNaN;  // T, s
    double noise = kNaN;               // P, mean block energy of the noise
    double crossing_time = kNaN;       // ti, s
};

// Lundeby crossing point, broadband (Part B of the specification).
Decay lundeby(Eigen::VectorXd q, const double fs) {
    const Decay failed;
    const Eigen::Index n = q.size();

    // B.1: inner alignment; 0 for a response already aligned in Part A.
    const Eigen::Index k2 = -onset(q);
    const double tshift = static_cast<double>(k2) / fs;
    q = circular_shift(q, k2);

    // B.2: x comes back from tshift in floating point on purpose, so the
    // usable length rounds exactly as the reference does.
    const double x = -tshift * fs;
    const double usable_real = static_cast<double>(n) - (x + std::fmod(x, 2.0));
    const Eigen::Index usable =
        usable_real >= 1.0 ? static_cast<Eigen::Index>(std::floor(usable_real)) : 0;

    // B.3
    const Eigen::VectorXd energy = q.array().square().matrix();

    // B.5: 30 ms blocks.
    const Blocks coarse = average(energy, usable, block_length(0.03 * fs), fs);
    if (coarse.count == 0) {
        return failed;
    }

    // B.6: noise from the last ~10 % of the blocks.
    // 1-based start a; noise_energy converts it to 0-based a - 1.
    const Eigen::Index a_first =
        coarse.count - static_cast<Eigen::Index>(std::round(static_cast<double>(coarse.count) / 10.0));
    double noise = noise_energy(coarse, a_first);

    // B.7: preliminary regression from the peak to 10 dB above the noise.
    const Eigen::Index i0 = first_max(coarse.w);
    const double above_noise = db(noise) + 10.0;
    Eigen::Index i1 = -1;
    for (Eigen::Index k = coarse.count - 1; k > i0; --k) {
        if (coarse.level[k] > above_noise) {
            i1 = k;
            break;
        }
    }
    if (i1 < 0) {
        return failed;
    }
    if (coarse.level[i1] - coarse.level[i0] > -5.0) {
        return failed;
    }
    Line line = fit(coarse, i0, i1);
    if (line.c1 == 0.0 || std::isnan(line.c0) || std::isnan(line.c1)) {
        return failed;
    }

    // B.8: preliminary crossing point, rejected when far past the response.
    double tc = (db(noise) - line.c0) / line.c1;
    if (tc > (static_cast<double>(n) / fs + tshift) * 2.0) {
        return failed;
    }

    // B.9: 5 intervals per 10 dB of the fitted decay, from the 30 ms blocks.
    const double intervals = (coarse.level[i1] - coarse.level[i0]) / -10.0 * 5.0;
    const Eigen::Index length =
        block_length((coarse.time[i1] - coarse.time[i0]) / intervals * fs);

    // B.10
    const Blocks blocks = average(energy, usable, length, fs);
    if (blocks.count == 0) {
        throw std::runtime_error("center_time: no complete block at the new block length");
    }
    const Eigen::Index imax = first_max(blocks.w);

    // B.11: iterate until the crossing point moves by at most 10 ms.
    // The noise-window bounds stay in double until they are known to be at
    // least 1, so a NaN or huge estimate never reaches an integer cast; and
    // std::max(1.0, NaN) is 1.0, as MATLAB's max ignores the NaN.
    double tc_old = 11.0 + tc;
    int iterations = 0;
    while (std::abs(tc_old - tc) > 0.01) {
        // B.11.1: noise window from 10 dB of decay past the crossing point,
        // capped at the last 10 % of the blocks; bounds are 1-based.
        const double a_last = std::round(static_cast<double>(blocks.count) * 0.9);
        const double a_10 = std::max(
            1.0, std::round((tc - 10.0 / line.c1) * fs / static_cast<double>(blocks.length)));
        const double a = std::min(a_last, a_10);
        if (a < 1.0) {
            throw std::runtime_error("center_time: empty noise window");
        }
        noise = noise_energy(blocks, static_cast<Eigen::Index>(a));

        // B.11.2: late decay, from 30 dB to 10 dB above the noise.
        const double regression_start = db(noise) + 10.0 + 20.0;
        Eigen::Index s0 = 0;
        for (Eigen::Index k = imax; k < blocks.count; ++k) {
            if (blocks.level[k] < regression_start) {
                s0 = k;
                break;
            }
        }
        const double regression_end = db(noise) + 10.0;
        Eigen::Index e0 = -1;
        for (Eigen::Index k = s0 + 1; k < blocks.count; ++k) {
            if (blocks.level[k] < regression_end) {
                e0 = k;
                break;
            }
        }
        if (e0 < 0) {
            return failed;
        }

        // B.11.3: a flat or rising late decay has no crossing point to refine.
        line = fit(blocks, s0, e0);
        if (line.c1 >= 0.0) {
            line.c1 = std::numeric_limits<double>::infinity();
            break;
        }

        // B.11.4
        tc_old = tc;
        tc = (db(noise) - line.c0) / line.c1;

        // B.11.5
        ++iterations;
        if (iterations > 30) {
            break;
        }
    }

    // B.12
    Decay decay;
    decay.reverberation_time = -60.0 / line.c1;
    decay.noise = noise;
    decay.crossing_time = tc - tshift;
    return decay;
}

}  // namespace

double center_time(const Signal& ir) {
    if (ir.samples.rows() == 0 || ir.samples.cols() == 0) {
        throw std::invalid_argument("center_time: empty impulse response");
    }
    const double fs = ir.sample_rate;
    const Eigen::Index total = ir.samples.rows();
    const Eigen::VectorXd channel = ir.samples.col(0).cast<double>();

    // A.1: move the onset to index 0.
    const Eigen::Index k = -onset(channel);
    Eigen::VectorXd shifted = circular_shift(channel, k);

    // A.2: C++ `%` keeps the sign of k, which the count relies on.
    Eigen::Index m = total + k + (k % 2);
    if (m < 1) {
        throw std::runtime_error("center_time: nothing left after onset alignment");
    }

    // A.3: clear the part that wrapped around.
    // 1-based m -> 0-based m - 1
    shifted.segment(m - 1, total - m + 1).setZero();

    // A.4: trim trailing zeros, but never below 5 samples.
    if (m > 1) {
        const Eigen::Index m0 = m;
        // 1-based m - 1 -> 0-based m - 2
        while (shifted[m - 2] == 0.0) {
            --m;
            if (m == 1) {
                throw std::runtime_error("center_time: channel is empty");
            }
        }
        if (m < 5) {
            m = m0;
        }
    }

    // A.5
    const Eigen::VectorXd p = shifted.head(m);
    const Eigen::Index n = p.size();

    const Decay decay = lundeby(p, fs);

    // C.1: no crossing point, so the centroid of the whole aligned response.
    if (std::isnan(decay.reverberation_time) || std::isnan(decay.crossing_time)) {
        double weighted = 0.0;
        double total_energy = 0.0;
        for (Eigen::Index i = 0; i < n; ++i) {
            const double e = p[i] * p[i];
            const double t = static_cast<double>(i) / fs;
            weighted += e * t;
            total_energy += e;
        }
        return weighted / total_energy;
    }

    // C.2: integrate up to the sample nearest the crossing point (the
    // earliest on ties, hence the strict `<`), then add the modelled tail.
    const double ti = decay.crossing_time;
    Eigen::Index k1 = 0;
    double nearest = std::abs(0.0 / fs - ti);
    for (Eigen::Index i = 1; i < n; ++i) {
        const double distance = std::abs(static_cast<double>(i) / fs - ti);
        if (distance < nearest) {
            nearest = distance;
            k1 = i;
        }
    }

    const double noise = decay.noise;
    const double tail = noise * decay.reverberation_time / (6.0 * std::log(10.0)) * fs;
    double energy_sum = 0.0;
    double weighted_sum = 0.0;
    for (Eigen::Index i = 0; i <= k1; ++i) {
        const double e = p[i] * p[i];
        energy_sum += e;
        weighted_sum += e * (static_cast<double>(i) / fs);
    }
    const double energy = energy_sum + tail;
    const double weighted = weighted_sum + tail * tail + tail * noise * ti;
    return weighted / energy;
}

}  // namespace iceberg::native
