#include "io/wav_writer.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace iceberg::io {

namespace {

void put16(std::vector<unsigned char>& out, std::uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>(v >> 8));
}

void put32(std::vector<unsigned char>& out, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) out.push_back(static_cast<unsigned char>((v >> (8 * k)) & 0xFF));
}

void tag(std::vector<unsigned char>& out, const char* id) {
    out.insert(out.end(), id, id + 4);
}

}  // namespace

void write_wav(const std::string& path, const Signal& signal) {
    const std::uint64_t frames = static_cast<std::uint64_t>(signal.samples.rows());
    const std::uint16_t channels = static_cast<std::uint16_t>(signal.samples.cols());
    const std::uint64_t data_size = frames * channels * 4;
    // RIFF sizes are 32-bit: the header (58 bytes) and the data must fit.
    if (data_size + 50 > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("write_wav: " + path + ": more than 4 GB of samples");
    }
    const std::uint32_t rate = static_cast<std::uint32_t>(signal.sample_rate);

    // IEEE float, 18-byte fmt (cbSize 0) and the fact chunk non-PCM data carries.
    std::vector<unsigned char> header;
    tag(header, "RIFF");
    put32(header, static_cast<std::uint32_t>(4 + 26 + 12 + 8 + data_size));
    tag(header, "WAVE");
    tag(header, "fmt ");
    put32(header, 18);
    put16(header, 3);
    put16(header, channels);
    put32(header, rate);
    put32(header, rate * channels * 4);
    put16(header, static_cast<std::uint16_t>(channels * 4));
    put16(header, 32);
    put16(header, 0);
    tag(header, "fact");
    put32(header, 4);
    put32(header, static_cast<std::uint32_t>(frames));
    tag(header, "data");
    put32(header, static_cast<std::uint32_t>(data_size));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("write_wav: cannot open " + path + " for writing");
    out.write(reinterpret_cast<const char*>(header.data()),
              static_cast<std::streamsize>(header.size()));

    // Interleaved frames, little-endian, one frame at a time.
    std::vector<unsigned char> frame(static_cast<std::size_t>(channels) * 4);
    for (std::uint64_t i = 0; i < frames; ++i) {
        for (std::uint16_t c = 0; c < channels; ++c) {
            const float v = signal.samples(static_cast<Eigen::Index>(i), c);
            std::uint32_t raw;
            std::memcpy(&raw, &v, sizeof(raw));
            for (int k = 0; k < 4; ++k) {
                frame[static_cast<std::size_t>(c) * 4 + static_cast<std::size_t>(k)] =
                    static_cast<unsigned char>((raw >> (8 * k)) & 0xFF);
            }
        }
        out.write(reinterpret_cast<const char*>(frame.data()),
                  static_cast<std::streamsize>(frame.size()));
    }
    if (!out) throw std::runtime_error("write_wav: short write on " + path);
}

}  // namespace iceberg::io
