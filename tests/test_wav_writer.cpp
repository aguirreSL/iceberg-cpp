// Tests for io/wav_writer: signals written as 32-bit float WAV files and read
// back with io/wav_loader must keep their samples, channel order and sample
// rate, and a path that cannot be written must raise an error.
#include "io/wav_loader.h"
#include "io/wav_writer.h"
#include "iceberg_signal.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

constexpr double kPi = 3.14159265358979323846;

std::filesystem::path tmp_wav(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("iceberg_wav_writer_test_" + name + ".wav");
}

// A two-channel signal of 1000 random samples at 44.1 kHz is written and read
// back. The shape and the sample rate must match, and every sample must equal
// the original to within 4 float ULP (EXPECT_FLOAT_EQ).
TEST(WavWriter, RoundTripPreservesSamples) {
    iceberg::Signal sig;
    sig.samples = Eigen::MatrixXf::Random(1000, 2);
    sig.sample_rate = 44100.0;

    const auto path = tmp_wav("round_trip");
    iceberg::io::write_wav(path.string(), sig);
    const auto loaded = iceberg::io::load_wav(path.string());

    ASSERT_EQ(loaded.samples.rows(), sig.samples.rows());
    ASSERT_EQ(loaded.samples.cols(), sig.samples.cols());
    EXPECT_DOUBLE_EQ(loaded.sample_rate, sig.sample_rate);

    for (Eigen::Index i = 0; i < sig.samples.rows(); ++i) {
        for (Eigen::Index c = 0; c < sig.samples.cols(); ++c) {
            EXPECT_FLOAT_EQ(loaded.samples(i, c), sig.samples(i, c))
                << "i=" << i << " c=" << c;
        }
    }
    std::filesystem::remove(path);
}

// A constant signal of 1.5, above digital full scale, is written and read back;
// the float32 file must keep the value 1.5 without clipping.
TEST(WavWriter, FloatMagnitudesAboveOnePreserved) {
    // Pipeline output (e.g. iceberg() before normalization) can
    // exceed [-1, 1]. float32 WAV must round-trip exactly.
    iceberg::Signal sig;
    sig.samples = Eigen::MatrixXf::Constant(100, 1, 1.5f);
    sig.sample_rate = 44100.0;

    const auto path = tmp_wav("above_one");
    iceberg::io::write_wav(path.string(), sig);
    const auto loaded = iceberg::io::load_wav(path.string());

    EXPECT_FLOAT_EQ(loaded.samples.cwiseAbs().maxCoeff(), 1.5f);
    EXPECT_FLOAT_EQ(loaded.samples(0, 0), 1.5f);
    std::filesystem::remove(path);
}

// A four-channel signal, channel c a sine of 100 (c + 1) Hz, is written and read
// back. Each channel must keep its position: the RMS difference per channel
// must stay below 1e-6, where a misplaced channel would differ by about 1.
TEST(WavWriter, MultichannelOrderPreserved) {
    // Each channel: distinct sine frequency = 100 * (c+1) Hz.
    // Round-trip must keep channels in their original positions.
    iceberg::Signal sig;
    sig.samples = Eigen::MatrixXf(1000, 4);
    for (int i = 0; i < 1000; ++i) {
        const double t = static_cast<double>(i) / 44100.0;
        for (int c = 0; c < 4; ++c) {
            const double f = 100.0 * static_cast<double>(c + 1);
            sig.samples(i, c) =
                static_cast<float>(std::sin(2.0 * kPi * f * t));
        }
    }
    sig.sample_rate = 44100.0;

    const auto path = tmp_wav("multichannel");
    iceberg::io::write_wav(path.string(), sig);
    const auto loaded = iceberg::io::load_wav(path.string());

    ASSERT_EQ(loaded.samples.cols(), 4);

    double worst = 0.0;
    for (Eigen::Index c = 0; c < 4; ++c) {
        const Eigen::ArrayXf diff =
            loaded.samples.col(c).array() - sig.samples.col(c).array();
        const double rms_diff =
            std::sqrt(diff.square().sum() / 1000.0);
        EXPECT_LT(rms_diff, 1e-6) << "channel " << c;
        worst = std::max(worst, rms_diff);
    }
    iceberg::test::record_measure("round trip, worst channel", worst, 1e-6, "rms");
    std::filesystem::remove(path);
}

// Writing to a path inside a directory that does not exist must throw
// std::runtime_error.
TEST(WavWriter, ThrowsOnInvalidPath) {
    iceberg::Signal sig;
    sig.samples = Eigen::MatrixXf::Zero(100, 2);
    sig.sample_rate = 44100.0;

    EXPECT_THROW(iceberg::io::write_wav(
                     "/nonexistent/dir/should/not/exist/foo.wav", sig),
                 std::runtime_error);
}

}  // namespace
