#pragma once

#include <array>
#include <span>

namespace grand_hall {

struct EqBand {
    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
};

class FiveBandEq {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setBands(const std::array<EqBand, 5>& bands) noexcept;
    void process(std::span<float> left, std::span<float> right) noexcept;

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1Left = 0.0f, z2Left = 0.0f, z1Right = 0.0f, z2Right = 0.0f;
        float processLeft(float input) noexcept;
        float processRight(float input) noexcept;
    };
    double sampleRate_ = 44100.0;
    std::array<EqBand, 5> bands_{};
    std::array<EqBand, 5> targetBands_{};
    std::array<Biquad, 5> filters_{};
    std::size_t smoothingSamples_ = 0;
    void updateCoefficients() noexcept;
};

} // namespace grand_hall
