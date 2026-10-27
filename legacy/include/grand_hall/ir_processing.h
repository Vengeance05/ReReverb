#pragma once

#include <span>
#include <vector>

namespace grand_hall {

struct IrProcessingOptions {
    bool trimSilence = true;
    bool peakNormalize = false;
    float trimThreshold = 1.0e-5f;
};

struct DecayShape {
    float referenceSeconds = 2.5f;
    float multiplier = 1.0f;
    float lowMultiplier = 1.0f;
    float highMultiplier = 1.0f;
    float highDamping = 0.0f;
    float earlySeconds = 0.05f;
    float crossoverHz = 500.0f;
};

struct ImpulseResponseComponents {
    std::vector<float> early;
    std::vector<float> late;
};

std::vector<float> resampleLinear(std::span<const float> input,
                                  double sourceRate,
                                  double destinationRate);

std::vector<float> processImpulseResponse(std::span<const float> input,
                                           IrProcessingOptions options = {});

std::vector<float> shapeDecay(std::span<const float> input,
                              double sampleRate,
                              DecayShape shape = {});

ImpulseResponseComponents splitImpulseResponse(std::span<const float> input,
                                                double sampleRate,
                                                double earlyEndSeconds = 0.04,
                                                double lateStartSeconds = 0.08);

} // namespace grand_hall
