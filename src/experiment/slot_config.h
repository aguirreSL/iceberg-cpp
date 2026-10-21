#pragma once

#include "experiment/slot.h"
#include "iceberg_signal.h"
#include "rendering/calibrate_ambisonics.h"  // ConfigSetup
#include "rendering/live.h"

#include <memory>
#include <string>
#include <vector>

namespace iceberg::experiment {

/// The loudspeakers a slot renders on, calibration left empty.
///
/// With a layout file: the file as `io::load_layout_json` reads it, so a
/// fixture-format file keeps its active subset and a speakers-format file
/// is active whole. Without one: the 4 default loudspeakers, output
/// channel i + 1 at `default_ls_order[i]`, all active.
///
/// The render, the position pad and the preparation mode all take the
/// layout from here, so the pair drawn on screen is the pair that plays.
///
/// Throws `std::runtime_error` when the layout file does not load or
/// `default_ls_order` is not a permutation of 0, 90, 180, 270.
iceberg::rendering::ConfigSetup slot_layout(const Slot& slot);

/// `slot_layout` plus the calibration and the slot's Iceberg v1.0.0
/// options: the `ConfigSetup` the app hands to `rendering::iceberg`.
///
/// Calibration: neutral, sized to the layout, in full-scale mode; else
/// `cal_path`. The IR format and the angle convention are always set.
///
/// Throws `std::runtime_error` when full scale is off and `cal_path` is
/// unset, or when the calibration and the layout count different
/// loudspeakers (a calibration covers every position of the layout it
/// was measured with).
iceberg::rendering::ConfigSetup slot_render_config(const Slot& slot);

/// Everything `prepare_slot_live` depends on, as one string: the files,
/// the level and calibration mode, the loudspeaker order and the v1.0.0
/// options. Not the source angle, which the live engine takes per block.
std::string live_inputs_key(const Slot& slot);

/// What a slot's `trajectory_buffer` was rendered from: `live_inputs_key`
/// plus the path. The buffer is current while this matches.
std::string trajectory_key(const Slot& slot);

/// The slot ready for `rendering::LiveEngine`: its signal (first channel
/// if multichannel, as the render takes it) and IR loaded, its
/// `slot_render_config`, `rendering::prepare_live` at its level. Throws
/// what those throw (missing files, a calibration that does not cover the
/// layout, ...).
std::shared_ptr<const iceberg::rendering::LiveRender> prepare_slot_live(
    const Slot& slot, std::vector<iceberg::rendering::RenderNotice>* notices = nullptr);

/// The same with the signal already read, for a signal file in a format
/// `io::load_wav` does not read; `slot.signal_path` is not opened.
std::shared_ptr<const iceberg::rendering::LiveRender> prepare_slot_live(
    const Slot& slot, iceberg::Signal signal,
    std::vector<iceberg::rendering::RenderNotice>* notices = nullptr);

}  // namespace iceberg::experiment
