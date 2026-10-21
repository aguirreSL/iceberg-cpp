#include "experiment/slot_config.h"

#include "experiment/experiment_loader.h"  // is_default_ls_permutation
#include "io/calibration_loader.h"
#include "io/layout_loader.h"
#include "io/wav_loader.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace iceberg::experiment {

iceberg::rendering::ConfigSetup slot_layout(const Slot& slot) {
    if (!slot.layout_path.empty()) {
        return iceberg::io::load_layout_json(slot.layout_path);
    }
    if (!is_default_ls_permutation(slot.default_ls_order)) {
        throw std::runtime_error(
            "slot_layout: default_ls_order must be a permutation of 0, 90, 180, 270");
    }
    iceberg::rendering::ConfigSetup cfg;
    cfg.ls_array.resize(4);
    cfg.active_ls_numbers.resize(4);
    cfg.ls_dir = Eigen::MatrixXd::Zero(4, 2);
    for (int i = 0; i < 4; ++i) {
        cfg.ls_array(i) = slot.default_ls_order[static_cast<size_t>(i)];
        cfg.active_ls_numbers(i) = i + 1;  // matlab: 1:4  →  c++: i + 1
        cfg.ls_dir(i, 0) = static_cast<double>(cfg.ls_array(i));
    }
    return cfg;
}

iceberg::rendering::ConfigSetup slot_render_config(const Slot& slot) {
    iceberg::rendering::ConfigSetup cfg = slot_layout(slot);
    const int n_ls = static_cast<int>(cfg.ls_array.size());

    if (slot.use_full_scale) {
        cfg.calibration = iceberg::io::neutral_calibration(n_ls);
    } else {
        if (!slot.cal_path) {
            throw std::runtime_error(
                "Calibration JSON required when 'Use full scale' is OFF. "
                "Browse a cal file or switch to full scale mode.");
        }
        cfg.calibration = iceberg::io::load_calibration(*slot.cal_path);
        const int n_cal = static_cast<int>(cfg.calibration.loudspeakers.size());
        if (n_cal != n_ls) {
            throw std::runtime_error(
                "The calibration covers " + std::to_string(n_cal) +
                " loudspeakers and the layout has " + std::to_string(n_ls) +
                ". Load the layout the calibration was measured with.");
        }
    }

    cfg.dser_factor = slot.dser_factor;
    cfg.anechoic_special_case = slot.anechoic_special_case;
    cfg.ambisonic_format = slot.ambisonic_format;
    cfg.azimuth_convention = slot.azimuth_convention;
    return cfg;
}

std::string live_inputs_key(const Slot& slot) {
    std::ostringstream key;
    key.precision(17);
    key << slot.ir_path << '\n' << slot.signal_path << '\n' << slot.layout_path << '\n'
        << slot.use_full_scale << ' ' << (slot.cal_path ? *slot.cal_path : std::string("-"))
        << '\n' << slot.level_db << ' ' << slot.default_ls_order[0] << ' '
        << slot.default_ls_order[1] << ' ' << slot.default_ls_order[2] << ' '
        << slot.default_ls_order[3] << ' ' << static_cast<int>(slot.dser_factor.kind) << ' '
        << slot.dser_factor.value << ' ' << static_cast<int>(slot.ambisonic_format) << ' '
        << static_cast<int>(slot.azimuth_convention) << ' ' << slot.anechoic_special_case;
    return key.str();
}

std::string trajectory_key(const Slot& slot) {
    return live_inputs_key(slot) + "\n" + iceberg::rendering::format_trajectory(slot.trajectory);
}

std::shared_ptr<const iceberg::rendering::LiveRender> prepare_slot_live(
    const Slot& slot, std::vector<iceberg::rendering::RenderNotice>* notices) {
    return prepare_slot_live(slot, iceberg::io::load_wav(slot.signal_path), notices);
}

std::shared_ptr<const iceberg::rendering::LiveRender> prepare_slot_live(
    const Slot& slot, iceberg::Signal signal,
    std::vector<iceberg::rendering::RenderNotice>* notices) {
    if (signal.samples.cols() > 1) {
        // As the render: the first channel of a multichannel signal.
        signal.samples = signal.samples.col(0).eval();
    }
    const iceberg::Signal ir = iceberg::io::load_wav(slot.ir_path);
    return std::make_shared<const iceberg::rendering::LiveRender>(iceberg::rendering::prepare_live(
        signal, ir, slot.level_db, slot_render_config(slot), notices));
}

}  // namespace iceberg::experiment
