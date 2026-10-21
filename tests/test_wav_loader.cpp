// Tests for io/wav_loader on WAV files assembled byte by byte in the test: the
// PCM (8, 16, 24 and 32 bit) and IEEE float encodings, the extensible format
// header, the skipping of other chunks, and the rejection of unreadable files.
#include "io/wav_loader.h"

#include <gtest/gtest.h>

#include <Eigen/Dense>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<unsigned char>;

void put16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xFF));
    b.push_back(static_cast<unsigned char>(v >> 8));
}

void put32(Bytes& b, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) b.push_back(static_cast<unsigned char>((v >> (8 * k)) & 0xFF));
}

void put_chunk(Bytes& b, const char* id, const Bytes& body) {
    b.insert(b.end(), id, id + 4);
    put32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1) b.push_back(0);
}

// fmt body: plain (16 bytes) or WAVE_FORMAT_EXTENSIBLE (40 bytes, the
// format code at the head of the SubFormat GUID).
Bytes fmt_body(std::uint16_t format, int channels, int bits, bool extensible) {
    Bytes f;
    put16(f, extensible ? 0xFFFE : format);
    put16(f, static_cast<std::uint16_t>(channels));
    put32(f, 48000);
    put32(f, static_cast<std::uint32_t>(48000 * channels * bits / 8));
    put16(f, static_cast<std::uint16_t>(channels * bits / 8));
    put16(f, static_cast<std::uint16_t>(bits));
    if (extensible) {
        put16(f, 22);
        put16(f, static_cast<std::uint16_t>(bits));
        put32(f, 0);
        put16(f, format);
        static const unsigned char kGuidTail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                                    0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        f.insert(f.end(), kGuidTail, kGuidTail + 14);
    }
    return f;
}

std::filesystem::path write_file(const std::string& name, const Bytes& riff_body) {
    Bytes b = {'R', 'I', 'F', 'F'};
    put32(b, static_cast<std::uint32_t>(4 + riff_body.size()));
    b.insert(b.end(), {'W', 'A', 'V', 'E'});
    b.insert(b.end(), riff_body.begin(), riff_body.end());
    const auto path =
        std::filesystem::temp_directory_path() / ("iceberg_wav_loader_test_" + name + ".wav");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return path;
}

std::filesystem::path wav(const std::string& name, std::uint16_t format, int channels, int bits,
                          const Bytes& data, bool extensible = false) {
    Bytes body;
    put_chunk(body, "fmt ", fmt_body(format, channels, bits, extensible));
    put_chunk(body, "data", data);
    return write_file(name, body);
}

// A hand-built mono 16-bit PCM file at 48 kHz with the samples 0x0000, 0x4000,
// 0x8000 and 0xC000 must load as exactly 0, 0.5, -1 and -0.5 (full scale is
// 32768), with the sample rate read from the header.
TEST(WavLoader, Pcm16) {
    Bytes d;
    for (const std::uint16_t v : {0x0000, 0x4000, 0x8000, 0xC000}) put16(d, v);
    const auto path = wav("pcm16", 1, 1, 16, d);
    const iceberg::Signal s = iceberg::io::load_wav(path.string());
    ASSERT_EQ(s.samples.rows(), 4);
    EXPECT_DOUBLE_EQ(s.sample_rate, 48000.0);
    EXPECT_EQ(s.samples(0, 0), 0.0f);
    EXPECT_EQ(s.samples(1, 0), 0.5f);
    EXPECT_EQ(s.samples(2, 0), -1.0f);
    EXPECT_EQ(s.samples(3, 0), -0.5f);
    std::filesystem::remove(path);
}

// Hand-built mono PCM files of 8, 24 and 32 bits must load to exact values:
// the unsigned 8-bit bytes 0, 128 and 192 as -1, 0 and 0.5, and for 24 and 32
// bits half and minus full scale as 0.5 and -1 (the sign taken from the top
// byte).
TEST(WavLoader, Pcm8Pcm24Pcm32) {
    const auto p8 = wav("pcm8", 1, 1, 8, Bytes{0, 128, 192});
    const iceberg::Signal s8 = iceberg::io::load_wav(p8.string());
    ASSERT_EQ(s8.samples.rows(), 3);
    EXPECT_EQ(s8.samples(0, 0), -1.0f);
    EXPECT_EQ(s8.samples(1, 0), 0.0f);
    EXPECT_EQ(s8.samples(2, 0), 0.5f);
    std::filesystem::remove(p8);

    // 0x400000 = +0.5, 0x800000 = -1 (sign bit in the third byte).
    const auto p24 = wav("pcm24", 1, 1, 24, Bytes{0x00, 0x00, 0x40, 0x00, 0x00, 0x80});
    const iceberg::Signal s24 = iceberg::io::load_wav(p24.string());
    ASSERT_EQ(s24.samples.rows(), 2);
    EXPECT_EQ(s24.samples(0, 0), 0.5f);
    EXPECT_EQ(s24.samples(1, 0), -1.0f);
    std::filesystem::remove(p24);

    Bytes d32;
    put32(d32, 0x40000000);
    put32(d32, 0x80000000);
    const auto p32 = wav("pcm32", 1, 1, 32, d32);
    const iceberg::Signal s32 = iceberg::io::load_wav(p32.string());
    ASSERT_EQ(s32.samples.rows(), 2);
    EXPECT_EQ(s32.samples(0, 0), 0.5f);
    EXPECT_EQ(s32.samples(1, 0), -1.0f);
    std::filesystem::remove(p32);
}

// A 64-bit float file holding 0.1, which float cannot represent, must load
// through load_wav_double as exactly the double 0.1 with its 48 kHz rate, and
// through load_wav as the float nearest to 0.1.
TEST(WavLoader, Float64KeepsDoublePrecision) {
    const double v = 0.1;   // not representable in float
    std::uint64_t raw;
    std::memcpy(&raw, &v, sizeof(raw));
    Bytes d;
    put32(d, static_cast<std::uint32_t>(raw & 0xFFFFFFFFu));
    put32(d, static_cast<std::uint32_t>(raw >> 32));
    const auto path = wav("f64", 3, 1, 64, d);
    double rate = 0.0;
    const Eigen::MatrixXd m = iceberg::io::load_wav_double(path.string(), &rate);
    ASSERT_EQ(m.rows(), 1);
    EXPECT_EQ(m(0, 0), 0.1);
    EXPECT_DOUBLE_EQ(rate, 48000.0);
    EXPECT_EQ(iceberg::io::load_wav(path.string()).samples(0, 0), 0.1f);
    std::filesystem::remove(path);
}

// A stereo 16-bit file with a WAVE_FORMAT_EXTENSIBLE header (the format code
// read from the SubFormat GUID) must load as two deinterleaved channels, left
// exactly 0.25 i and right exactly -0.25 i for the frames i = 0, 1, 2.
TEST(WavLoader, ExtensibleInterleavedChannels) {
    // Three frames of two channels: left 0.25 * i, right -0.25 * i.
    Bytes d;
    for (int i = 0; i < 3; ++i) {
        put16(d, static_cast<std::uint16_t>(static_cast<std::int16_t>(8192 * i)));
        put16(d, static_cast<std::uint16_t>(static_cast<std::int16_t>(-8192 * i)));
    }
    const auto path = wav("extensible", 1, 2, 16, d, /*extensible=*/true);
    const iceberg::Signal s = iceberg::io::load_wav(path.string());
    ASSERT_EQ(s.samples.rows(), 3);
    ASSERT_EQ(s.samples.cols(), 2);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.samples(i, 0), 0.25f * static_cast<float>(i));
        EXPECT_EQ(s.samples(i, 1), -0.25f * static_cast<float>(i));
    }
    std::filesystem::remove(path);
}

// Chunks other than "fmt " and "data" are skipped, with their pad byte: an
// odd-sized LIST chunk before "fmt " and an odd-sized "junk" chunk between
// "fmt " and "data". The single 16-bit sample must load as 0.5.
TEST(WavLoader, SkipsOtherChunksAndPadding) {
    // An odd-sized chunk before fmt and another between fmt and data.
    Bytes body;
    put_chunk(body, "LIST", Bytes{'a', 'b', 'c'});
    put_chunk(body, "fmt ", fmt_body(1, 1, 16, false));
    put_chunk(body, "junk", Bytes(7, 0xEE));
    Bytes d;
    put16(d, 0x4000);
    put_chunk(body, "data", d);
    const auto path = write_file("chunks", body);
    const iceberg::Signal s = iceberg::io::load_wav(path.string());
    ASSERT_EQ(s.samples.rows(), 1);
    EXPECT_EQ(s.samples(0, 0), 0.5f);
    std::filesystem::remove(path);
}

// load_wav must throw std::runtime_error on four unreadable inputs: a missing
// file, a text file with a .wav name, an IMA ADPCM file (format 0x11, an
// encoding the loader does not decode) and a file with no "data" chunk.
TEST(WavLoader, RejectsWhatItCannotRead) {
    EXPECT_THROW(iceberg::io::load_wav("/nonexistent/dir/foo.wav"), std::runtime_error);

    const auto not_wav = std::filesystem::temp_directory_path() / "iceberg_wav_loader_test_txt.wav";
    {
        std::ofstream out(not_wav);
        out << "this is not a wav file";
    }
    EXPECT_THROW(iceberg::io::load_wav(not_wav.string()), std::runtime_error);
    std::filesystem::remove(not_wav);

    // IMA ADPCM (format 0x11) is not read.
    const auto adpcm = wav("adpcm", 0x11, 1, 4, Bytes(16, 0));
    EXPECT_THROW(iceberg::io::load_wav(adpcm.string()), std::runtime_error);
    std::filesystem::remove(adpcm);

    Bytes no_data;
    put_chunk(no_data, "fmt ", fmt_body(1, 1, 16, false));
    const auto missing = write_file("no_data", no_data);
    EXPECT_THROW(iceberg::io::load_wav(missing.string()), std::runtime_error);
    std::filesystem::remove(missing);
}

}  // namespace
