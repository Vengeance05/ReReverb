// Minimal float32 WAV writer / PCM+float reader for tools and tests.
#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace rr::tools {

inline void writeWav(const std::string& path, const std::vector<std::vector<float>>& ch, int fs) {
    const auto channels = static_cast<std::uint16_t>(ch.size());
    const auto frames = static_cast<std::uint32_t>(ch.empty() ? 0 : ch[0].size());
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t dataBytes = frames * channels * 4;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(channels); u32(static_cast<std::uint32_t>(fs));
    u32(static_cast<std::uint32_t>(fs) * channels * 4); u16(static_cast<std::uint16_t>(channels * 4)); u16(32);
    f.write("data", 4); u32(dataBytes);
    for (std::uint32_t i = 0; i < frames; ++i)
        for (const auto& c : ch) f.write(reinterpret_cast<const char*>(&c[i]), 4);
}

} // namespace rr::tools
