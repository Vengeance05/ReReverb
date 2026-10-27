#pragma once

#include <array>
#include <span>
#include <vector>

namespace grand_hall {

struct AlgorithmicParameters {
    float roomSize = 0.65f;
    float decaySeconds = 2.5f;
    float preDelayMs = 20.0f;
    float earlyLevel = 0.35f;
    float lateLevel = 0.75f;
    float earlyLateBalance = 0.5f;
    float lowDecay = 1.0f;
    float highDecay = 1.0f;
    float highDamping = 0.2f;
    float width = 1.0f;
    float wet = 1.0f;
    float dry = 0.0f;
    float outputGain = 1.0f;
};

class AlgorithmicReverb {
public:
    void prepare(double sampleRate, std::size_t maximumBlockSize);
    void reset() noexcept;
    void setParameters(const AlgorithmicParameters& parameters) noexcept;
    void process(std::span<float> left, std::span<float> right);

private:
    static constexpr std::size_t lineCount = 8;
    static constexpr std::size_t earlyTapCount = 12;
    double sampleRate_ = 44100.0;
    std::size_t maximumBlockSize_ = 0;
    AlgorithmicParameters target_;
    AlgorithmicParameters current_;
    std::array<std::size_t, lineCount> lengths_{};
    std::array<std::vector<float>, lineCount> delays_;
    std::array<std::size_t, lineCount> positions_{};
    std::array<float, lineCount> dampingState_{};
    std::vector<float> inputDelayLeft_;
    std::vector<float> inputDelayRight_;
    std::size_t inputPosition_ = 0;
    std::array<std::size_t, earlyTapCount> earlyDelays_{};
    std::array<float, earlyTapCount> earlyGains_{};
    std::array<float, earlyTapCount> earlyPan_{};
    std::array<float, lineCount> lineSigns_{};
    float feedbackGain_ = 0.0f;
    float lowFeedbackGain_ = 0.0f;
    float highFeedbackGain_ = 0.0f;
    std::size_t parameterRampSamples_ = 0;
    std::size_t derivedUpdateCounter_ = 0;

    void updateDerivedParameters() noexcept;
    [[nodiscard]] float nextDelayedSample(std::size_t line) noexcept;
};

} // namespace grand_hall
