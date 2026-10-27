#pragma once

#include "grand_hall/partitioned_convolver.h"

#include <array>
#include <span>
#include <vector>

namespace grand_hall {

struct ReverbParameters {
    float wet = 1.0f;
    float dry = 0.0f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    float preDelayMs = 20.0f;
    float earlyLevel = 0.35f;
    float lateLevel = 0.75f;
    float earlyLateBalance = 0.5f;
    float width = 1.0f;
    float lowDamping = 0.0f;
    float highDamping = 0.2f;
};

class ReverbEngine {
public:
    void prepare(double sampleRate, std::size_t maximumBlockSize, std::size_t irLength);
    void setImpulseResponse(std::span<const float> left, std::span<const float> right);
    void setImpulseResponseComponents(std::span<const float> earlyLeft,
                                      std::span<const float> lateLeft,
                                      std::span<const float> earlyRight,
                                      std::span<const float> lateRight);
    void reset() noexcept;
    void setParameters(const ReverbParameters& parameters) noexcept { parameters_ = parameters; }
    void process(std::span<float> left, std::span<float> right);

private:
    double sampleRate_ = 44100.0;
    std::size_t maximumBlockSize_ = 0;
    ReverbParameters parameters_;
    PartitionedConvolver earlyLeftConvolver_;
    PartitionedConvolver lateLeftConvolver_;
    PartitionedConvolver earlyRightConvolver_;
    PartitionedConvolver lateRightConvolver_;
    std::vector<float> earlyWetLeft_;
    std::vector<float> lateWetLeft_;
    std::vector<float> earlyWetRight_;
    std::vector<float> lateWetRight_;
    std::vector<float> delayLeft_;
    std::vector<float> delayRight_;
    std::size_t delayPosition_ = 0;
};

} // namespace grand_hall
