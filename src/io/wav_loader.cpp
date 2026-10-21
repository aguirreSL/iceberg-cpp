#include "io/wav_loader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace iceberg::io {

namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

// Little-endian fields, independent of the host's byte order.
std::uint32_t u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
    throw std::runtime_error("load_wav: " + path + ": " + msg);
}

// One sample of the data chunk, as a double.
double sample(const unsigned char* p, std::uint16_t format, int bits) {
    if (format == kFormatFloat) {
        if (bits == 32) {
            const std::uint32_t raw = u32(p);
            float f;
            std::memcpy(&f, &raw, sizeof(f));
            return static_cast<double>(f);
        }
        const std::uint64_t raw = static_cast<std::uint64_t>(u32(p)) |
                                  (static_cast<std::uint64_t>(u32(p + 4)) << 32);
        double d;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }
    switch (bits) {
        case 8:
            return (static_cast<double>(p[0]) - 128.0) / 128.0;
        case 16:
            return static_cast<double>(static_cast<std::int16_t>(u16(p))) / 32768.0;
        case 24: {
            // Sign-extend the 3 bytes through the top of a 32-bit word.
            const std::int32_t v = static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(p[0]) << 8) | (static_cast<std::uint32_t>(p[1]) << 16) |
                (static_cast<std::uint32_t>(p[2]) << 24));
            return static_cast<double>(v) / 2147483648.0;
        }
        default:
            return static_cast<double>(static_cast<std::int32_t>(u32(p))) / 2147483648.0;
    }
}

Eigen::MatrixXd read(const std::string& path, double* sample_rate) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail(path, "cannot open");
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),
                                           std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        fail(path, "not a RIFF/WAVE file");
    }

    std::uint16_t format = 0;
    int channels = 0;
    int bits = 0;
    double rate = 0.0;
    const unsigned char* data = nullptr;
    std::size_t data_size = 0;
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char* chunk = bytes.data() + pos;
        const std::size_t size = u32(chunk + 4);
        const std::size_t body = pos + 8;
        const std::size_t available = bytes.size() - body;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16 || available < 16) fail(path, "short fmt chunk");
            format = u16(chunk + 8);
            channels = u16(chunk + 10);
            rate = static_cast<double>(u32(chunk + 12));
            bits = u16(chunk + 22);
            if (format == kFormatExtensible) {
                // The SubFormat GUID starts with the format code.
                if (size < 40 || available < 40) fail(path, "short extensible fmt chunk");
                format = u16(chunk + 8 + 24);
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            data_size = std::min(size, available);   // a truncated file keeps what it has
        }
        if (data != nullptr && channels > 0) break;
        pos = body + size + (size & 1);   // chunks are word-aligned
    }
    if (channels <= 0) fail(path, "no fmt chunk");
    if (data == nullptr) fail(path, "no data chunk");
    const bool pcm = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool flt = format == kFormatFloat && (bits == 32 || bits == 64);
    if (!pcm && !flt) {
        fail(path, "unsupported encoding (format " + std::to_string(format) + ", " +
                       std::to_string(bits) + " bits)");
    }

    const std::size_t width = static_cast<std::size_t>(bits / 8);
    const std::size_t frame = width * static_cast<std::size_t>(channels);
    const Eigen::Index frames = static_cast<Eigen::Index>(data_size / frame);
    Eigen::MatrixXd samples(frames, channels);
    for (Eigen::Index i = 0; i < frames; ++i) {
        const unsigned char* f = data + static_cast<std::size_t>(i) * frame;
        for (int c = 0; c < channels; ++c) {
            samples(i, c) = sample(f + static_cast<std::size_t>(c) * width, format, bits);
        }
    }
    if (sample_rate != nullptr) *sample_rate = rate;
    return samples;
}

}  // namespace

Signal load_wav(const std::string& path) {
    double rate = 0.0;
    const Eigen::MatrixXd samples = read(path, &rate);
    return Signal{samples.cast<float>(), rate};
}

Eigen::MatrixXd load_wav_double(const std::string& path, double* sample_rate) {
    return read(path, sample_rate);
}

}  // namespace iceberg::io
