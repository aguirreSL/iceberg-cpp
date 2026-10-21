#include "rendering/iceberg_core.h"

#include "native/center_time.h"
#include "native/time_crop.h"
#include "native/time_shift.h"
#include "native/time_window.h"

#include <exception>
#include <stdexcept>

namespace iceberg::rendering {

IcebergCoreOutput iceberg_core(const iceberg::Signal& ir,
                               bool anechoic_special_case) {
    if (ir.samples.rows() == 0 || ir.samples.cols() == 0) {
        throw std::invalid_argument("iceberg_core: empty IR");
    }

    const Eigen::Index original_n = ir.samples.rows();
    const double fs = ir.sample_rate;

    // === Mono W path (DSER) ==============================================
    iceberg::Signal omni;
    omni.samples = ir.samples.col(0);
    omni.sample_rate = fs;

    const auto auto_shifted_omni = iceberg::native::time_shift(
        omni, iceberg::native::ShiftMode::Auto);
    const int shift_index = auto_shifted_omni.shift_samples;

    // The split point: the centre time, or a fixed 10 ms rectangle for the
    // anechoic special case (a single direct peak has no meaningful centre
    // time) and whenever the centre time cannot be computed, as MATLAB's
    // try/catch does.
    double c_time = 0.01;
    iceberg::Signal ir_early_windowed;
    bool windowed = false;
    if (!anechoic_special_case) {
        try {
            c_time = iceberg::native::center_time(omni);
            ir_early_windowed = iceberg::native::time_window(
                auto_shifted_omni.signal, /*start_sec=*/0.0, /*end_sec=*/c_time,
                iceberg::native::WindowType::Hann);
            windowed = true;
        } catch (const std::exception&) {
            c_time = 0.01;
        }
    }
    if (!windowed) {
        ir_early_windowed = iceberg::native::time_window(
            auto_shifted_omni.signal, /*start_sec=*/0.0, /*end_sec=*/0.01,
            iceberg::native::WindowType::Rectwin);
    }
    const auto dser_shifted = iceberg::native::time_shift(
        ir_early_windowed,
        iceberg::native::ShiftMode::Samples,
        static_cast<double>(std::abs(shift_index)));

    IcebergCoreOutput out;
    out.dser = dser_shifted.signal;

    // === Full 4-ch path (IR_Late) ========================================
    const auto auto_shifted_full = iceberg::native::time_shift(
        ir, iceberg::native::ShiftMode::Auto);
    const int shift_index_late = auto_shifted_full.shift_samples;

    // Crop [c_time 0]: time_crop's inverted range, everything after c_time.
    const auto ir_late_cropped = iceberg::native::time_crop(
        auto_shifted_full.signal, /*start_sec=*/c_time, /*end_sec=*/0.0);

    // Resync zero-prepend to restore the original length.
    iceberg::Signal ir_late_padded;
    ir_late_padded.sample_rate = fs;
    ir_late_padded.samples =
        Eigen::MatrixXf::Zero(original_n, ir_late_cropped.samples.cols());
    if (ir_late_cropped.samples.rows() > 0) {
        ir_late_padded.samples.bottomRows(ir_late_cropped.samples.rows()) =
            ir_late_cropped.samples;
    }

    // Trim the last 50 ms with a rectangular window.
    const double track_length =
        static_cast<double>(original_n) / fs;
    const auto ir_late_trimmed = iceberg::native::time_window(
        ir_late_padded, /*start_sec=*/0.0,
        /*end_sec=*/track_length - 0.05,
        iceberg::native::WindowType::Rectwin);

    // Shift-back to match the original timeline.
    const auto ir_late_shifted = iceberg::native::time_shift(
        ir_late_trimmed,
        iceberg::native::ShiftMode::Samples,
        static_cast<double>(std::abs(shift_index_late)));

    out.ir_late = ir_late_shifted.signal;
    return out;
}

}  // namespace iceberg::rendering
