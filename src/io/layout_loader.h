#pragma once

#include "rendering/calibrate_ambisonics.h"  // ConfigSetup

#include <string>

namespace iceberg::io {

/// Load a loudspeaker layout from a JSON file. Auto-detects the
/// schema and dispatches:
///
/// **Speakers format**: a list of loudspeakers, all in use, in output
/// channel order (`examples/layouts/ring8_speakers.json`):
/// ```json
/// {
///   "name": "Cardinal 4 LS",
///   "speakers": [
///     {"label": "back",  "azimuth": 180, "elevation": 0},
///     {"label": "right", "azimuth": 270, "elevation": 0},
///     {"label": "front", "azimuth": 0,   "elevation": 0},
///     {"label": "left",  "azimuth": 90,  "elevation": 0}
///   ]
/// }
/// ```
/// All speakers are marked active (`active_ls_numbers = 1..n`,
/// `ls_dir` = all speakers). `name` and `label` fields are optional
/// and currently ignored.
///
/// **Ring format**, Iceberg MATLAB's `lsArray` / `activeLSNumbers` /
/// `ls_dir`: every position of the ring is an output channel and only the
/// listed ones play (`examples/layouts/ring24_4active.json`):
/// ```json
/// {
///   "lsArray": [180, 195, 210, ...],
///   "activeLSNumbers": [1, 7, 13, 19],
///   "ls_dir": [[180, 0], [270, 0], [0, 0], [90, 0]],
///   ...
/// }
/// ```
/// Preserves the active subset and `ls_dir` exactly as given.
///
/// **The calibration field of the returned `ConfigSetup` is left
/// empty.** The caller must populate it via
/// `iceberg::io::load_calibration(...)` or
/// `iceberg::io::neutral_calibration(...)` before passing the
/// `ConfigSetup` to render functions.
///
/// Throws `std::runtime_error` if:
///   - The file cannot be opened or parsed.
///   - Neither `"speakers"` nor `"lsArray"` key is present.
///   - Speakers / loudspeakers count is below the algorithm minimum
///     of 4.
///   - Any speaker has elevation != 0 (port is 2D-only).
iceberg::rendering::ConfigSetup load_layout_json(const std::string& path);

}  // namespace iceberg::io
