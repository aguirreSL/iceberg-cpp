#include "rendering/input_handling.h"

#include "native/resample.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace iceberg::rendering {

namespace {

const char* format_name(AmbisonicFormat f) {
    switch (f) {
        case AmbisonicFormat::FuMa:  return "fuma";
        case AmbisonicFormat::AmbiX: return "ambix";
        case AmbisonicFormat::N3D:   return "n3d";
    }
    return "?";
}

template <typename... Args>
std::string format_text(const char* fmt, Args... args) {
    const int n = std::snprintf(nullptr, 0, fmt, args...);
    std::string s(static_cast<size_t>(n), '\0');
    std::snprintf(s.data(), s.size() + 1, fmt, args...);
    return s;
}

}  // namespace

Eigen::MatrixXf bformat_to_fuma(const Eigen::MatrixXf& bformat,
                                AmbisonicFormat format, int* order) {
    const Eigen::Index n_ch = bformat.cols();
    const double n_order = std::sqrt(static_cast<double>(n_ch)) - 1.0;
    if (n_ch < 4 || n_order != std::round(n_order)) {
        throw std::invalid_argument(format_text(
            "Ambisonics B-format needs (N+1)^2 channels (4 for first order, 9 for second, 16 "
            "for third); got %ld. Mixed-order and horizontal-only layouts are not supported: "
            "convert the file to full-sphere FuMa or ACN first.",
            static_cast<long>(n_ch)));
    }
    if (order) *order = static_cast<int>(n_order);

    // The first-order components are the first four channels in any
    // order: ACN W Y Z X, FuMa W X Y Z.
    const Eigen::MatrixXd b = bformat.leftCols(4).cast<double>();
    Eigen::MatrixXd fuma(b.rows(), 4);
    switch (format) {
        case AmbisonicFormat::FuMa:
            fuma = b;
            break;
        case AmbisonicFormat::AmbiX:
            // matlab: [W/sqrt(2), X = ch 4, Y = ch 2, Z = ch 3]
            fuma.col(0) = b.col(0) / std::sqrt(2.0);
            fuma.col(1) = b.col(3);
            fuma.col(2) = b.col(1);
            fuma.col(3) = b.col(2);
            break;
        case AmbisonicFormat::N3D:
            // matlab: [W/sqrt(2), [X Y Z] = ch [4 2 3] / sqrt(3)]
            fuma.col(0) = b.col(0) / std::sqrt(2.0);
            fuma.col(1) = b.col(3) / std::sqrt(3.0);
            fuma.col(2) = b.col(1) / std::sqrt(3.0);
            fuma.col(3) = b.col(2) / std::sqrt(3.0);
            break;
    }
    return fuma.cast<float>();
}

double direct_sound_ratio(const Eigen::MatrixXf& fuma, double fs) {
    const Eigen::Index n = fuma.rows();
    if (n == 0) return std::numeric_limits<double>::quiet_NaN();
    // matlab: [~, pk] = max(abs(fuma(:, 1))) keeps the first maximum
    Eigen::Index pk = 0;
    for (Eigen::Index i = 1; i < n; ++i) {
        if (std::abs(fuma(i, 0)) > std::abs(fuma(pk, 0))) pk = i;
    }
    const Eigen::Index half =
        std::max<Eigen::Index>(1, static_cast<Eigen::Index>(std::round(0.25e-3 * fs)));
    // matlab: idx = max(1, pk-half):min(n, pk+half) (1-based)
    //   → c++: rows max(0, pk0-half) .. min(n-1, pk0+half)
    const Eigen::Index from = std::max<Eigen::Index>(0, pk - half);
    const Eigen::Index to = std::min<Eigen::Index>(n - 1, pk + half);
    const Eigen::MatrixXd win = fuma.middleRows(from, to - from + 1).cast<double>();
    const double dipoles =
        std::sqrt(win.rightCols(3).array().square().rowwise().sum().mean());
    if (dipoles == 0.0) return std::numeric_limits<double>::quiet_NaN();
    return std::sqrt(win.col(0).array().square().mean()) / dipoles;
}

PreparedInputs prepare_inputs(const iceberg::Signal& signal,
                              const iceberg::Signal& ir,
                              const ConfigSetup& config) {
    PreparedInputs out;
    out.signal = signal;

    // --- Ambisonics convention and order --------------------------------
    AmbisonicFormat format = AmbisonicFormat::FuMa;
    if (config.ambisonic_format) {
        format = *config.ambisonic_format;
    } else {
        out.notices.push_back({"iceberg:ambisonicFormatAssumed",
            "configSetup.ambisonicFormat is not set: the impulse response is assumed to be FuMa "
            "(W, X, Y, Z; W scaled by 1/sqrt(2)). AmbiX files (ACN order W, Y, Z, X; SN3D) must "
            "be declared as 'ambix' and ACN/N3D files as 'n3d', otherwise directions are decoded "
            "wrongly. Declare the format to silence this notice (shown once per session)."});
    }
    const Eigen::Index n_ir_channels = ir.samples.cols();
    int ir_order = 1;
    Eigen::MatrixXf fuma = bformat_to_fuma(ir.samples, format, &ir_order);

    if (format == AmbisonicFormat::AmbiX) {
        out.notices.push_back({"iceberg:ambisonicConversion",
            "The impulse response was declared as AmbiX (ACN order W, Y, Z, X; SN3D): it was "
            "converted to FuMa (W, X, Y, Z; W scaled by 1/sqrt(2)), the convention the decoder "
            "expects."});
    } else if (format == AmbisonicFormat::N3D) {
        out.notices.push_back({"iceberg:ambisonicConversion",
            "The impulse response was declared as ACN/N3D (order W, Y, Z, X; X, Y, Z scaled by "
            "sqrt(3)): it was converted to FuMa (W, X, Y, Z; W scaled by 1/sqrt(2)), the "
            "convention the decoder expects."});
    }
    if (ir_order > 1) {
        out.notices.push_back({"iceberg:ambisonicOrderReduced", format_text(
            "The impulse response is order-%d Ambisonics (%ld channels). Iceberg decodes first "
            "order: only its first-order components (W, X, Y, Z) are used and the spatial detail "
            "of the higher orders (%ld channels) is discarded. If the file was made with "
            "order-dependent weights (e.g. max-rE), its first-order part keeps them.",
            ir_order, static_cast<long>(n_ir_channels), static_cast<long>(n_ir_channels - 4))});
    }

    const double ratio = direct_sound_ratio(fuma, ir.sample_rate);
    if (!std::isnan(ratio) && (ratio < 0.64 || ratio > 0.84)) {
        const char* hint = ratio > 0.84
            ? "W is too strong relative to X, Y, Z - typical of an AmbiX (SN3D) file declared as "
              "'fuma' (1.00) or as 'n3d' (1.22)."
            : "W is too weak relative to X, Y, Z - typical of an N3D file declared as 'fuma' "
              "(0.58) or as 'ambix' (0.41).";
        out.notices.push_back({"iceberg:ambisonicNormalizationSuspect", format_text(
            "Check configSetup.ambisonicFormat ('%s'): around the direct sound, W/|(X,Y,Z)| is "
            "%.2f after conversion, while a correctly declared impulse response gives 0.71 (a "
            "FuMa plane wave). %s A direct sound that is not a plane wave (e.g. a very close "
            "source) can also trigger this check, and a wrong channel order with the right "
            "normalization cannot be detected.",
            format_name(format), ratio, hint)});
    }

    // --- Azimuth convention ----------------------------------------------
    AzimuthConvention convention = AzimuthConvention::CounterClockwise;
    if (config.azimuth_convention) {
        convention = *config.azimuth_convention;
    } else {
        out.notices.push_back({"iceberg:azimuthConventionAssumed",
            "configSetup.azimuthConvention is not set: the angles (source, lsArray, ls_dir) are "
            "assumed counter-clockwise - 0 deg in front, 90 deg to the LEFT - the convention of "
            "Ambisonics (B-format Y points left) and of the polarch toolboxes. If your loudspeaker "
            "ring is labelled clockwise (90 deg to the RIGHT), set 'clockwise', otherwise the "
            "Ambisonics (late) part is mirrored left-right. Declare the convention to silence this "
            "notice (shown once per session)."});
    }
    if (convention == AzimuthConvention::Clockwise) {
        // Clockwise labels are mirror images of the counter-clockwise ones:
        // mirroring the IR's Y axis decodes the late field against them. VBAP
        // and the nearest-loudspeaker anchor only compare labels with labels.
        fuma.col(2) = -fuma.col(2);
    }
    out.ir.samples = std::move(fuma);
    out.ir.sample_rate = ir.sample_rate;

    // --- Sampling rate ----------------------------------------------------
    if (signal.sample_rate != ir.sample_rate) {
        out.notices.push_back({"iceberg:resampledSignal", format_text(
            "The impulse response is at %g Hz and the signal at %g Hz: the signal was resampled "
            "to %g Hz to match.", ir.sample_rate, signal.sample_rate, ir.sample_rate)});
        // matlab: [p, q] = rat(IR.samplingRate / signal.samplingRate, 1e-12)
        const auto [p, q] = iceberg::native::rat(ir.sample_rate / signal.sample_rate, 1e-12);
        out.signal.samples =
            iceberg::native::resample(signal.samples.cast<double>(), p, q).cast<float>();
        out.signal.sample_rate = ir.sample_rate;
    }
    return out;
}

}  // namespace iceberg::rendering
