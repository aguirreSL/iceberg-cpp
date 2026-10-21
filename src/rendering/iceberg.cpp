#include "rendering/iceberg.h"

#include "native/add.h"
#include "native/convolve.h"
#include "native/normalize_dat.h"
#include "rendering/ambisonics_decoder.h"
#include "rendering/calibrate_ambisonics.h"
#include "rendering/calibrate_vbap.h"
#include "rendering/iceberg_core.h"
#include "rendering/input_handling.h"
#include "rendering/vbap_render.h"

#include <stdexcept>
#include <string>

namespace iceberg::rendering {

iceberg::Signal iceberg(const iceberg::Signal& signal_in,
                        const iceberg::Signal& ir_in,
                        double source_angle_deg,
                        double level_db,
                        const ConfigSetup& config,
                        std::vector<RenderNotice>* notices) {
    if (auto err = validate_config(config)) {
        throw std::invalid_argument("iceberg: " + *err);
    }
    if (signal_in.samples.cols() != 1) {
        throw std::invalid_argument("iceberg: signal must be mono");
    }

    // 0. Input handling: IR to first-order FuMa, azimuth convention,
    //    signal resampled to the IR rate.
    PreparedInputs prepared = prepare_inputs(signal_in, ir_in, config);
    if (notices) {
        notices->insert(notices->end(), prepared.notices.begin(), prepared.notices.end());
    }
    const iceberg::Signal& signal = prepared.signal;
    const iceberg::Signal& ir = prepared.ir;

    // 1. DSER / LR split.
    const auto core = iceberg_core(ir, config.anechoic_special_case);

    // Both branches start from the peak-normalised dry signal.
    iceberg::Signal signal_norm;
    signal_norm.sample_rate = signal.sample_rate;
    signal_norm.samples =
        iceberg::native::normalize_dat(signal.samples);

    // 2. VBAP path: normalize → convolve → pan → calibrate → dserFactor.
    const auto signal_dser =
        iceberg::native::convolve(signal_norm, core.dser);
    const auto vbap_panned = vbap_render(
        signal_dser, source_angle_deg, /*elevation=*/0.0, config.ls_dir);
    auto vbap_calibrated = calibrate_vbap(
        vbap_panned, level_db, source_angle_deg, config);
    switch (config.dser_factor.kind) {
        case DserFactor::Kind::Unity:
            break;
        case DserFactor::Kind::Thesis:
            // matlab: directScale = max(DSER.time(:)) (the positive peak)
            vbap_calibrated.samples *= core.dser.samples.maxCoeff();
            break;
        case DserFactor::Kind::Numeric:
            vbap_calibrated.samples *= static_cast<float>(config.dser_factor.value);
            break;
    }

    // 3. Ambi path: normalize → calibrate (dry mono) → convolve → decode.

    const auto signal_amb_input = calibrate_ambisonics(
        signal_norm, level_db, source_angle_deg, config);
    const auto signal_ambi_lr =
        iceberg::native::convolve(signal_amb_input, core.ir_late);
    const Eigen::MatrixXd decoder = sad_decoder(config.ls_dir);
    const auto ambi_decoded = decode_bformat(signal_ambi_lr, decoder);

    // 4. Distribute to the master loudspeaker array.
    //    matlab: vbapAudio.time(:, channel) = vbapDser.time(:, i)
    //    where channel = activeLSNumbers(i) (1-based).
    //    c++:    vbap_audio.col(channel - 1) = vbap_calibrated.col(i)
    const Eigen::Index n_master = config.ls_array.size();
    const Eigen::Index n_active = config.active_ls_numbers.size();
    const Eigen::Index n_out_vbap = vbap_calibrated.samples.rows();
    const Eigen::Index n_out_amb  = ambi_decoded.samples.rows();

    Eigen::MatrixXf vbap_audio =
        Eigen::MatrixXf::Zero(n_out_vbap, n_master);
    Eigen::MatrixXf amb_audio =
        Eigen::MatrixXf::Zero(n_out_amb,  n_master);

    for (Eigen::Index i = 0; i < n_active; ++i) {
        // matlab: activeLSNumbers(i)        (1-based)
        // c++:    active_ls_numbers(i) - 1  (0-based)
        const Eigen::Index ch_master =
            static_cast<Eigen::Index>(config.active_ls_numbers(i) - 1);
        if (i < vbap_calibrated.samples.cols()) {
            vbap_audio.col(ch_master) = vbap_calibrated.samples.col(i);
        }
        if (i < ambi_decoded.samples.cols()) {
            amb_audio.col(ch_master) = ambi_decoded.samples.col(i);
        }
    }

    // 5. Sum. native::add zero-pads if shapes mismatch (won't here:
    //    DSER and LR have the same length after iceberg_core's resync).
    iceberg::Signal vbap_sig{std::move(vbap_audio), signal.sample_rate};
    iceberg::Signal amb_sig{ std::move(amb_audio),  signal.sample_rate};
    return iceberg::native::add(vbap_sig, amb_sig);
}

}  // namespace iceberg::rendering
