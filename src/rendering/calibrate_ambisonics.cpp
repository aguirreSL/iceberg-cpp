#include "rendering/calibrate_ambisonics.h"

#include "native/fft.h"
#include "native/linspace.h"
#include "native/pchip.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <string>

namespace iceberg::rendering {

namespace {

/// MATLAB `mod(x, 360)`: non-negative remainder.
double mod360(double x) {
    return std::fmod(std::fmod(x, 360.0) + 360.0, 360.0);
}

/// `abs(mod(a - b + 180, 360) - 180)`, matching MATLAB.
double angular_distance(double a, double b) {
    return std::abs(mod360(a - b + 180.0) - 180.0);
}

}  // namespace

Eigen::Index nearest_loudspeaker(double source_angle_deg, const Eigen::MatrixXd& ls_dir) {
    // matlab: sortrows([angDist, mod(iAngles - allAngles + 360, 360)]),
    // stable, so a full tie keeps the first ls_dir row.
    Eigen::Index nearest = 0;
    double min_dist = angular_distance(ls_dir(0, 0), source_angle_deg);
    double min_tie = mod360(source_angle_deg - ls_dir(0, 0) + 360.0);
    for (Eigen::Index i = 1; i < ls_dir.rows(); ++i) {
        const double d = angular_distance(ls_dir(i, 0), source_angle_deg);
        const double tie = mod360(source_angle_deg - ls_dir(i, 0) + 360.0);
        if (d < min_dist || (d == min_dist && tie < min_tie)) {
            min_dist = d;
            min_tie = tie;
            nearest = i;
        }
    }
    return nearest;
}

std::optional<std::string> validate_config(const ConfigSetup& cfg) {
    const Eigen::Index n_master =
        cfg.ls_array.size();
    const Eigen::Index n_calib =
        static_cast<Eigen::Index>(cfg.calibration.loudspeakers.size());

    // 1. Master array ↔ calibration size match.
    if (n_master != n_calib) {
        return "ls_array.size() (" + std::to_string(n_master) +
               ") != calibration.loudspeakers.size() (" +
               std::to_string(n_calib) + ")";
    }

    // 2. Minimum 4 LS.
    if (n_master < 4) {
        return "ls_array.size() (" + std::to_string(n_master) +
               ") is below algorithm minimum 4";
    }

    // 3. active_ls_numbers values in [1, n_master].
    for (Eigen::Index i = 0; i < cfg.active_ls_numbers.size(); ++i) {
        const int v = cfg.active_ls_numbers(i);
        if (v < 1 || v > static_cast<int>(n_master)) {
            return "active_ls_numbers[" + std::to_string(i) + "] = " +
                   std::to_string(v) + " is out of range [1, " +
                   std::to_string(n_master) + "]";
        }
    }

    // 4. ls_dir.rows() == active_ls_numbers.size().
    if (cfg.ls_dir.rows() != cfg.active_ls_numbers.size()) {
        return "ls_dir.rows() (" + std::to_string(cfg.ls_dir.rows()) +
               ") != active_ls_numbers.size() (" +
               std::to_string(cfg.active_ls_numbers.size()) + ")";
    }

    // 5. ls_dir.col(1) (elevation) is zero — 2D-only port.
    if (cfg.ls_dir.cols() < 2) {
        return "ls_dir must have at least 2 columns "
               "(azimuth, elevation)";
    }
    for (Eigen::Index i = 0; i < cfg.ls_dir.rows(); ++i) {
        if (cfg.ls_dir(i, 1) != 0.0) {
            return "ls_dir(" + std::to_string(i) +
                   ", 1) (elevation) = " +
                   std::to_string(cfg.ls_dir(i, 1)) +
                   " — port is 2D-only, elevation must be 0";
        }
    }

    return std::nullopt;
}

iceberg::Signal calibrate_ambisonics(const iceberg::Signal& signal,
                                     double level_db,
                                     double source_angle_deg,
                                     const ConfigSetup& config) {
    if (!std::isnan(level_db) && level_db > 95.0) {
        throw std::invalid_argument(
            "calibrate_ambisonics: level > 95 dB is forbidden");
    }
    if (signal.samples.cols() != 1) {
        throw std::invalid_argument(
            "calibrate_ambisonics: signal must be mono");
    }

    const double fs = signal.sample_rate;
    const Eigen::Index n_len = signal.samples.rows();
    const double ls_db_per_volt =
        20.0 * std::log10(config.calibration.i_factor / 2e-5);

    // 1. Map ls_dir(:, 0) angles to master-array indices (1-based, MATLAB).
    //    matlab: activeLSNumbers(ii) = find(lsArray == ls_dir(ii, 1), 1)
    //    c++:    iterate ls_array, store position+1.
    const Eigen::Index n_active = config.ls_dir.rows();
    Eigen::VectorXi active_ls_numbers(n_active);
    for (Eigen::Index ii = 0; ii < n_active; ++ii) {
        const double target = config.ls_dir(ii, 0);
        Eigen::Index found = -1;
        for (Eigen::Index j = 0; j < config.ls_array.size(); ++j) {
            if (static_cast<double>(config.ls_array(j)) == target) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            throw std::invalid_argument(
                "calibrate_ambisonics: ls_dir angle not found in ls_array");
        }
        // matlab: find(...) (1-based) → c++: found + 1
        active_ls_numbers(ii) = static_cast<int>(found + 1);
    }

    // 2. One-sided frequency grid (linspace, as Iceberg MATLAB builds it).
    const Eigen::Index n_bins = n_len / 2 + 1;
    const Eigen::VectorXd freq_vec =
        iceberg::native::linspace(0.0, fs / 2.0, n_bins);

    // 3. NSP — nearest LS by wrap-around angular distance, clockwise
    //    tie-break (see nearest_loudspeaker).
    const Eigen::Index nearest = nearest_loudspeaker(source_angle_deg, config.ls_dir);
    // matlab: iChannel = activeLSNumbers(order(1)) (1-based)
    // c++:    i_channel_0 = active_ls_numbers(nearest) - 1
    const Eigen::Index i_channel_0 =
        static_cast<Eigen::Index>(active_ls_numbers(nearest) - 1);

    // 4. Per-LS spectral filter (one-sided, length n_bins). The MATLAB
    //    code computes a sparse matrix with all active LS columns and
    //    indexes one of them; we skip the sparsity and pchip only the
    //    chosen LS — algebraically identical, lighter.
    const auto& ls_filter =
        config.calibration.loudspeakers[i_channel_0];
    const Eigen::VectorXd interpolation = iceberg::native::pchip(
        ls_filter.freq_vector, ls_filter.freq_magnitude, freq_vec);

    // 5. Pipeline: NaN→0, RMS-normalise, conditional SPL scale.
    Eigen::VectorXd ch = signal.samples.col(0).cast<double>();
    for (Eigen::Index i = 0; i < ch.size(); ++i) {
        if (std::isnan(ch(i))) ch(i) = 0.0;
    }
    const double rms = std::sqrt(ch.array().square().mean());
    const double scaler = 2.0 * rms;
    if (scaler != 0.0) {
        ch /= scaler;
    }
    if (!std::isnan(level_db)) {
        ch *= std::pow(10.0, (level_db - ls_db_per_volt) / 20.0);
    }

    // 6. Full FFT → multiply by the filter mirrored onto the full grid
    //    (Hermitian: the mirror of a real signal's spectrum) → real(ifft) → newLevelFactor.
    //    matlab: real(ifft(chFFT .* filterResp)); the mirrored filter is
    //    real and even, so the real FFT of its one-sided half is the same
    //    filtering at half the cost.
    Eigen::VectorXcd spectrum = iceberg::native::rfft(ch);
    spectrum.array() *= interpolation.cast<std::complex<double>>().array();
    const Eigen::VectorXd filtered =
        iceberg::native::irfft(spectrum, static_cast<int>(n_len));

    const double level_factor =
        config.calibration.new_level_factor(i_channel_0);
    const Eigen::VectorXd scaled = filtered * level_factor;

    // 7. Pack as Signal — float at the boundary.
    iceberg::Signal out;
    out.sample_rate = fs;
    out.samples = scaled.cast<float>();
    return out;
}

}  // namespace iceberg::rendering
