#include "grand_hall/wav_reader.h"

#include <cstring>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace grand_hall {
namespace {
std::uint32_t readU32(std::ifstream& file) {
    std::uint8_t bytes[4]{};
    file.read(reinterpret_cast<char*>(bytes), 4);
    if (!file) throw std::runtime_error("Unexpected end of WAV file");
    return bytes[0] | (bytes[1] << 8U) | (bytes[2] << 16U) | (bytes[3] << 24U);
}
std::uint16_t readU16(std::ifstream& file) {
    std::uint8_t bytes[2]{};
    file.read(reinterpret_cast<char*>(bytes), 2);
    if (!file) throw std::runtime_error("Unexpected end of WAV file");
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8U));
}

void seekTo(std::ifstream& file, const std::streamoff position) {
    file.clear();
    file.seekg(position, std::ios::beg);
    if (!file) throw std::runtime_error("Invalid WAV chunk boundary");
}
}

StereoImpulseResponse readWavImpulseResponse(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open WAV file: " + path);
    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    if (fileSize <= 0 || fileSize > static_cast<std::streamoff>(512 * 1024 * 1024))
        throw std::runtime_error("WAV file is empty or exceeds the 512 MiB limit");
    file.seekg(0);
    if (fileSize < 12) throw std::runtime_error("Truncated WAV header");
    char riff[4]{}, wave[4]{};
    file.read(riff, 4);
    const auto riffSize = readU32(file);
    file.read(wave, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0)
        throw std::runtime_error("Not a RIFF/WAVE file");
    if (riffSize < 4 || static_cast<std::streamoff>(riffSize) + 8 > fileSize)
        throw std::runtime_error("Truncated RIFF payload");

    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t sampleRate = 0, dataSize = 0;
    std::streamoff dataPosition = -1;
    bool hasFormat = false;
    const auto containerEnd = std::min<std::streamoff>(
        fileSize, static_cast<std::streamoff>(riffSize) + 8);
    while (file.tellg() >= 0 && file.tellg() < containerEnd) {
        const auto chunkHeader = static_cast<std::streamoff>(file.tellg());
        if (containerEnd - chunkHeader < 8)
            throw std::runtime_error("Truncated WAV chunk header");
        char id[4]{};
        file.read(id, 4);
        if (!file) throw std::runtime_error("Truncated WAV chunk header");
        const auto size = readU32(file);
        const auto payload = static_cast<std::streamoff>(file.tellg());
        const auto payloadEnd = payload + static_cast<std::streamoff>(size);
        const auto paddedEnd = payloadEnd + static_cast<std::streamoff>(size & 1U);
        if (payloadEnd < payload || paddedEnd < payloadEnd || paddedEnd > containerEnd)
            throw std::runtime_error("Truncated WAV chunk");
        if (std::strncmp(id, "fmt ", 4) == 0) {
            if (size < 16) throw std::runtime_error("Malformed WAV format chunk");
            format = readU16(file);
            channels = readU16(file);
            sampleRate = readU32(file);
            readU32(file);
            readU16(file);
            bits = readU16(file);
            hasFormat = true;
            seekTo(file, payloadEnd);
        } else if (std::strncmp(id, "data", 4) == 0) {
            dataPosition = payload;
            dataSize = size;
        }
        seekTo(file, paddedEnd);
    }
    if (!hasFormat || dataPosition < 0 || sampleRate == 0)
        throw std::runtime_error("WAV must contain valid fmt and data chunks");
    if (channels != 1 && channels != 2)
        throw std::runtime_error("WAV must be mono or stereo");
    if (format == 1) {
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
            throw std::runtime_error("Unsupported PCM bit depth");
    } else if (format == 3) {
        if (bits != 32)
            throw std::runtime_error("IEEE float WAV must be 32-bit");
    } else {
        throw std::runtime_error("Unsupported WAV encoding");
    }
    const auto bytesPerSample = static_cast<std::size_t>((bits + 7) / 8);
    const auto frameBytes = channels * bytesPerSample;
    if (frameBytes == 0 || dataSize % frameBytes != 0)
        throw std::runtime_error("WAV data is not aligned to complete frames");
    const auto frames = dataSize / frameBytes;
    if (frames == 0 || frames > static_cast<std::size_t>(sampleRate * 60.0 * 30.0))
        throw std::runtime_error("WAV impulse response must be between one sample and 30 minutes");
    StereoImpulseResponse result;
    result.sampleRate = sampleRate;
    result.channels = channels;
    result.bitsPerSample = bits;
    result.format = format;
    result.left.resize(frames);
    result.right.resize(frames);
    seekTo(file, dataPosition);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        auto readSample = [&]() {
            std::uint32_t raw = 0;
            for (std::size_t byte = 0; byte < bytesPerSample; ++byte) {
                const auto value = file.get();
                if (value == std::char_traits<char>::eof())
                    throw std::runtime_error("Truncated WAV sample data");
                raw |= static_cast<std::uint32_t>(value & 0xff) << (byte * 8);
            }
            if (format == 3 && bits == 32) {
                float value = 0.0f;
                std::memcpy(&value, &raw, sizeof(value));
                return value;
            }
            if (bits == 8) return (static_cast<float>(raw & 0xffU) - 128.0f) / 128.0f;
            const auto shift = 32U - bits;
            const auto signedValue = static_cast<std::int32_t>(raw << shift) >> shift;
            return static_cast<float>(signedValue) / static_cast<float>(std::uint64_t{1} << (bits - 1));
        };
        const auto l = readSample();
        const auto r = channels == 2 ? readSample() : l;
        if (!std::isfinite(l) || !std::isfinite(r))
            throw std::runtime_error("WAV contains a non-finite sample");
        result.left[frame] = l;
        result.right[frame] = r;
    }
    return result;
}

} // namespace grand_hall
