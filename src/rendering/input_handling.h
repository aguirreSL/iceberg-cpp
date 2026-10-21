#pragma once

#include "rendering/calibrate_ambisonics.h"  // ConfigSetup, AmbisonicFormat
#include "iceberg_signal.h"

#include <Eigen/Dense>

#include <string>
#include <vector>

namespace iceberg::rendering {

/// One change `iceberg()` made to its inputs, or one check that failed.
/// `id` is the MATLAB warning identifier (e.g. `iceberg:resampledSignal`),
/// `message` the MATLAB text. MATLAB shows the two "assumed" notices once
/// per session; the library reports them on every call and leaves the
/// de-duplication to the caller.
struct RenderNotice {
    std::string id;
    std::string message;
};

/// First-order FuMa B-format (W, X, Y, Z) from full-sphere Ambisonics of
/// order N >= 1, (N+1)^2 channels, as Iceberg v1.0.0
/// `native_bformat_fuma`. The first-order components are always the
/// first four channels (ACN W Y Z X, FuMa W X Y Z), so higher orders are
/// reduced by keeping them. `order` receives N. Throws
/// `std::invalid_argument` on a channel count that is not (N+1)^2 >= 4.
Eigen::MatrixXf bformat_to_fuma(const Eigen::MatrixXf& bformat,
                                AmbisonicFormat format, int* order);

/// W / |(X, Y, Z)| (RMS) over +-0.25 ms around the peak of |W|. A FuMa
/// plane wave gives 1/sqrt(2); a mis-declared format gives 1.00 (AmbiX
/// read as FuMa), 0.58 (N3D as FuMa) and so on. NaN when the window holds
/// no directional energy.
double direct_sound_ratio(const Eigen::MatrixXf& fuma, double fs);

/// The inputs as the renderer takes them, plus what was done to them.
struct PreparedInputs {
    iceberg::Signal signal;
    iceberg::Signal ir;   ///< first-order FuMa, 4 channels
    std::vector<RenderNotice> notices;
};

/// Iceberg v1.0.0's input handling, in its order: the IR converted from
/// the declared format to first-order FuMa (FuMa assumed and reported when
/// undeclared; conversion and order reduction reported), the direct-sound
/// check on the result, the Y axis mirrored for the clockwise convention
/// (counter-clockwise assumed and reported when undeclared), and the
/// signal resampled to the IR rate when they differ (reported).
PreparedInputs prepare_inputs(const iceberg::Signal& signal,
                              const iceberg::Signal& ir,
                              const ConfigSetup& config);

}  // namespace iceberg::rendering
