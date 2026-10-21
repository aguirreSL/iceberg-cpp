#pragma once

#include "experiment/experiment.h"

#include <array>
#include <string>

namespace iceberg::experiment {

/// True when `order` holds 0, 90, 180 and 270 once each, the only valid
/// `Slot::default_ls_order`.
bool is_default_ls_permutation(const std::array<int, 4>& order);

/// Load an experiment from a `.iceberg-exp` JSON file.
///
/// Throws `std::runtime_error` with a clear message when:
///   - the file cannot be opened
///   - JSON parse fails
///   - any required top-level field is missing
///     (`version`, `name`, `slots`, `play_options`)
///   - the file's `version` major does not match the binary's
///     supported major (currently 1)
///   - any slot is missing a required field (`name`, `ir_path`,
///     `layout_path`, `signal_path`, `source_angle_deg`, `level_db`)
///   - `source_angle_deg` is outside `[0, 360)`
///   - `level_db` is outside `[-60, 95]`
///   - a schema 1.3 option is malformed: `default_ls_order` not a
///     permutation of 0, 90, 180, 270; `dser_factor` not `"unity"`,
///     `"thesis"` or a finite number >= 0; `ambisonic_format` not
///     `"fuma"`, `"ambix"` or `"n3d"`; `azimuth_convention` not
///     `"counterclockwise"` or `"clockwise"`
///   - `slots` is not a JSON array
///
/// Runtime slot state is reset: `is_stale = true`,
/// `is_rendered = false`, `output_buffer = nullptr`,
/// `error_message = nullopt`. `cached_metrics` is preserved if the
/// JSON contained it.
Experiment load_experiment(const std::string& path);

/// Save an experiment atomically to `path`.
///
/// Writes to `path + ".tmp"` first, then renames into place (POSIX
/// `rename` is atomic on the same filesystem). On the way out,
/// updates `exp.modified_iso8601` to the current UTC time, fills
/// `created_iso8601` if it is empty, and sets `exp.version` to
/// `kExperimentVersion` (the file is written in this schema).
///
/// Throws `std::runtime_error` if the temp file cannot be opened or
/// the rename fails.
void save_experiment(const std::string& path, Experiment& exp);

}  // namespace iceberg::experiment
