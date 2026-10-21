#include "rendering/calibrate_vbap.h"

#include "native/fft.h"
#include "native/linspace.h"
#include "native/pchip.h"

#include <cmath>
#include <complex>
#include <stdexcept>

namespace iceberg::rendering {

iceberg::Signal calibrate_vbap(const iceberg::Signal& signal_to_play,
                               double level_db,
                               double /*source_angle_deg*/,
                               const ConfigSetup& config) {
    const double fs = signal_to_play.sample_rate;
    const Eigen::Index n_len = signal_to_play.samples.rows();
    const Eigen::Index n_active = config.ls_dir.rows();

    if (signal_to_play.samples.cols() != n_active) {
        throw std::invalid_argument(
            "calibrate_vbap: signal channel count must equal ls_dir.rows()");
    }

    const double ls_db_per_volt =
        20.0 * std::log10(config.calibration.i_factor / 2e-5);

    // 1. Map active LS angles to master-array indices (1-based MATLAB).
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
                "calibrate_vbap: ls_dir angle not found in ls_array");
        }
        // matlab: find(...) (1-based) → c++: found + 1
        active_ls_numbers(ii) = static_cast<int>(found + 1);
    }

    // 2. One-sided frequency grid (linspace, as Iceberg MATLAB builds it) and
    //    the per-active-LS spectral filter on it.
    const Eigen::Index n_bins = n_len / 2 + 1;
    const Eigen::VectorXd freq_vec =
        iceberg::native::linspace(0.0, fs / 2.0, n_bins);
    Eigen::MatrixXd interpolation(n_bins, n_active);
    for (Eigen::Index i = 0; i < n_active; ++i) {
        // matlab: ls.index (1-based) → c++: ls.index - 1
        const auto& f =
            config.calibration.loudspeakers[active_ls_numbers(i) - 1];
        interpolation.col(i) = iceberg::native::pchip(
            f.freq_vector, f.freq_magnitude, freq_vec);
    }

    // 3. Single level stage: one common scaler, one SPL anchor.
    //    matlab: ref = sqrt(mean(sum(time.^2, 2)))
    //    Computed on the raw input (NaN included), as MATLAB does.
    const Eigen::MatrixXd x = signal_to_play.samples.cast<double>();
    const double ref = std::sqrt(x.array().square().rowwise().sum().mean());
    const double scaler = 2.0 * ref;
    const bool bypass = std::isnan(level_db);   // MATLAB level 'n'
    const double gain =
        bypass ? 1.0 : std::pow(10.0, (level_db - ls_db_per_volt) / 20.0);

    // 4. Per-channel pipeline.
    Eigen::MatrixXf out(n_len, n_active);
    for (Eigen::Index idx = 0; idx < n_active; ++idx) {
        Eigen::VectorXd ch = x.col(idx);

        // NaN-replace per channel. MATLAB also fprintf-warns; we stay
        // silent.
        for (Eigen::Index i = 0; i < ch.size(); ++i) {
            if (std::isnan(ch(i))) ch(i) = 0.0;
        }

        if (scaler != 0.0 && !bypass) {
            ch = (ch / scaler) * gain;   // the VBAP gain g_i survives
        }

        // matlab: real(ifft(fft(ch) .* [H; conj(flipud(H(2:end-1)))]))
        // The mirrored filter is real and even, so the real FFT of the
        // one-sided half gives the same result at half the cost (these
        // full-length FFTs are most of a render).
        Eigen::VectorXcd spectrum = iceberg::native::rfft(ch);
        spectrum.array() *= interpolation.col(idx).cast<std::complex<double>>().array();
        const Eigen::VectorXd filtered =
            iceberg::native::irfft(spectrum, static_cast<int>(n_len));

        const double level_factor =
            config.calibration.new_level_factor(
                active_ls_numbers(idx) - 1);
        out.col(idx) = (filtered * level_factor).cast<float>();
    }

    iceberg::Signal result;
    result.sample_rate = fs;
    result.samples = std::move(out);
    return result;
}

}  // namespace iceberg::rendering
