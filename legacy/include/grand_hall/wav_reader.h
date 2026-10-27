#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace grand_hall {

struct StereoImpulseResponse {
    double sampleRate = 0.0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t format = 0;
    std::vector<float> left;
    std::vector<float> right;
};

StereoImpulseResponse readWavImpulseResponse(const std::string& path);

} // namespace grand_hall
