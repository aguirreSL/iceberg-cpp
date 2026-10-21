#pragma once

#include "experiment/slot.h"

#include <string>
#include <vector>

namespace iceberg::experiment {

/// Schema version this binary writes. 1.3: per-slot
/// `default_ls_order` and the Iceberg v1.0.0 options; `layout_path` may
/// be empty. 1.4: per-slot `trajectory`.
inline constexpr const char* kExperimentVersion = "1.4";

/// Inter-slot playback behaviour. `wait_seconds == 0` (with
/// `wait_between_slots == true`) means the operator advances manually
/// between stimuli (key press in the GUI). Positive `wait_seconds` is
/// an automatic delay.
struct PlayOptions {
    bool wait_between_slots = true;
    double wait_seconds = 0.0;
    /// Stereo preview using the slot's active VBAP pair
    /// (s1 -> L, s2 -> R). Added in schema version 1.1; older 1.0
    /// files load with default `false`.
    bool preparation_mode = false;
};

/// Top-level experiment container. Persisted as `.iceberg-exp` JSON.
///
/// Versioning is semver. Bump **major** on a JSON shape break (field
/// removed / renamed / type changed). Bump **minor** on additive
/// fields (loader ignores unknown new fields when reading older
/// majors). The loader rejects mismatched majors with a clear error.
struct Experiment {
    std::string name = "Untitled Experiment";
    std::string created_iso8601;          ///< ISO 8601 UTC ("...Z")
    std::string modified_iso8601;
    std::vector<Slot> slots;
    PlayOptions play_options;
    /// Written as `kExperimentVersion` on save; holds the file's own
    /// version after a load.
    std::string version = kExperimentVersion;
};

}  // namespace iceberg::experiment
