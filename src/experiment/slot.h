#pragma once

#include "iceberg_signal.h"
#include "rendering/calibrate_ambisonics.h"  // DserFactor, AmbisonicFormat, AzimuthConvention
#include "rendering/input_handling.h"        // RenderNotice
#include "rendering/trajectory.h"            // TrajectoryPoint

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace iceberg::rendering {
struct LiveRender;
}

namespace iceberg::experiment {

/// One pre-rendered stimulus configuration in a slots-based experiment.
///
/// Persisted fields (top block) are read from / written to JSON.
/// Runtime fields (`is_rendered`, `is_stale`, `output_buffer`,
/// `error_message`, `render_notices`) are NOT persisted; they describe in-memory state
/// of the render pipeline. After load, runtime fields are reset:
/// `is_stale = true`, `is_rendered = false`, `output_buffer = nullptr`,
/// `error_message = nullopt`.
///
/// `cached_metrics` IS persisted (optional) so a freshly loaded
/// experiment can show level / peak / cTime in the GUI without
/// re-rendering. Updated in place after every successful render.
struct Slot {
    // ---- Persisted ----------------------------------------------------
    std::string name;                          ///< user-editable; default "Slot N"
    std::string ir_path;
    /// Empty = the 4 default loudspeakers (`default_ls_order`). Required
    /// before schema 1.3.
    std::string layout_path;
    std::optional<std::string> cal_path;       ///< nullopt = use neutral
    std::string signal_path;
    double source_angle_deg = 0.0;             ///< [0, 360)
    /// Range depends on `use_full_scale`: with cal loaded (false)
    /// -> dB SPL in [0, 95]; with full-scale mode (true) -> dBFS in
    /// [-60, 0]. The GUI slider switches semantics; the lib treats
    /// it uniformly as the `level_db` argument to
    /// `iceberg::rendering::iceberg(...)`.
    double level_db = -20.0;

    /// Schema 1.2: toggle persisted per slot
    /// instead of derived from `cal_path` so the UI stays consistent
    /// when the user clicks "Use full scale" OFF without immediately
    /// browsing a calibration. Render path uses this:
    ///   true  -> `neutral_calibration` (cal_path ignored).
    ///   false -> requires `cal_path` set; render fails otherwise.
    bool use_full_scale = true;

    /// Schema 1.3: with no `layout_path` the slot renders on 4
    /// loudspeakers, all active; entry i is the azimuth of output channel
    /// i + 1. Always a permutation of 0, 90, 180, 270. Ignored while a
    /// layout file is set.
    std::array<int, 4> default_ls_order = {0, 90, 180, 270};

    /// Schema 1.3: Iceberg v1.0.0's options, with its defaults. The IR
    /// format and the angle convention are always declared from the app,
    /// so the library's "assumed" notices never fire.
    iceberg::rendering::DserFactor dser_factor;
    iceberg::rendering::AmbisonicFormat ambisonic_format =
        iceberg::rendering::AmbisonicFormat::FuMa;
    iceberg::rendering::AzimuthConvention azimuth_convention =
        iceberg::rendering::AzimuthConvention::CounterClockwise;
    bool anechoic_special_case = false;

    /// Schema 1.4: a programmed source path, rendered by its own action
    /// ("Render trajectory") into `trajectory_buffer`; the slot's render
    /// stays static at `source_angle_deg`. Empty = no path.
    std::vector<iceberg::rendering::TrajectoryPoint> trajectory;

    /// Cached metrics from the last successful render. Persisted as
    /// optional so a fresh experiment without renders still parses.
    struct CachedMetrics {
        double peak_abs = 0.0;
        double rms = 0.0;
        int n_samples = 0;
        double c_time = 0.0;
    };
    std::optional<CachedMetrics> cached_metrics;

    // ---- Runtime (not serialised) -------------------------------------
    bool is_rendered = false;
    bool is_stale = true;
    std::shared_ptr<iceberg::Signal> output_buffer;
    std::optional<std::string> error_message;
    /// What `iceberg()` reported about the last render (MATLAB's warnings).
    std::vector<iceberg::rendering::RenderNotice> render_notices;
    /// The slot prepared for live playback, and the
    /// `live_inputs_key` it was prepared from. Everything but the angle
    /// goes into it, so moving the source keeps it valid.
    std::shared_ptr<const iceberg::rendering::LiveRender> live_render;
    std::string live_key;
    /// The slot rendered along `trajectory`, and the key it was
    /// rendered from (`live_inputs_key` plus the path).
    std::shared_ptr<iceberg::Signal> trajectory_buffer;
    std::string trajectory_key;
};

}  // namespace iceberg::experiment
