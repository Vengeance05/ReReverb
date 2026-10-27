#include "grand_hall/algorithmic_reverb.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grand_hall {
namespace {
constexpr std::array<std::size_t, 8> baseLengths{1499, 1783, 2029, 2377, 2671, 3001, 3371, 3793};
constexpr std::array<float, 12> tapTimes{0.007f, 0.011f, 0.017f, 0.023f, 0.031f, 0.041f,
                                          0.053f, 0.067f, 0.083f, 0.101f, 0.127f, 0.157f};
constexpr std::array<float, 12> tapGain{0.70f, 0.58f, 0.52f, 0.46f, 0.40f, 0.36f,
                                         0.32f, 0.28f, 0.24f, 0.20f, 0.17f, 0.14f};
constexpr std::array<float, 12> tapPan{-0.82f, 0.68f, -0.55f, 0.44f, -0.34f, 0.27f,
                                       -0.21f, 0.18f, -0.14f, 0.11f, -0.08f, 0.05f};
constexpr float oneOverSqrtEight = 0.3535533905932738f;
bool evenParity(const std::size_t value) noexcept {
    auto bits = value;
    bool parity = false;
    while (bits != 0) {
        parity = !parity;
        bits &= bits - 1;
    }
    return !parity;
}
}

void AlgorithmicReverb::prepare(const double sampleRate, const std::size_t maximumBlockSize) {
    if (sampleRate <= 0.0 || maximumBlockSize == 0)
        throw std::invalid_argument("Invalid algorithmic reverb preparation parameters");
    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
    const auto maximumLength = static_cast<std::size_t>(baseLengths.back() * 1.5) + 1;
    for (std::size_t line = 0; line < lineCount; ++line) {
        lengths_[line] = static_cast<std::size_t>(baseLengths[line] * (0.75 + 0.75 * target_.roomSize));
        delays_[line].assign(maximumLength, 0.0f);
        lineSigns_[line] = (line & 1U) == 0U ? 1.0f : -1.0f;
    }
    earlyGains_ = tapGain;
    earlyPan_ = tapPan;
    inputDelayLeft_.assign(static_cast<std::size_t>(sampleRate * 0.25) + maximumBlockSize, 0.0f);
    inputDelayRight_.assign(inputDelayLeft_.size(), 0.0f);
    parameterRampSamples_ = static_cast<std::size_t>(sampleRate * 0.02);
    derivedUpdateCounter_ = 0;
    current_ = target_;
    updateDerivedParameters();
    reset();
}

void AlgorithmicReverb::reset() noexcept {
    for (auto& delay : delays_) std::fill(delay.begin(), delay.end(), 0.0f);
    std::fill(inputDelayLeft_.begin(), inputDelayLeft_.end(), 0.0f);
    std::fill(inputDelayRight_.begin(), inputDelayRight_.end(), 0.0f);
    positions_.fill(0);
    dampingState_.fill(0.0f);
    inputPosition_ = 0;
}

void AlgorithmicReverb::setParameters(const AlgorithmicParameters& parameters) noexcept {
    target_ = parameters;
}

void AlgorithmicReverb::updateDerivedParameters() noexcept {
    const auto decay = std::clamp(current_.decaySeconds, 0.1f, 30.0f);
    const auto meanDelay = 0.5 * (lengths_.front() + lengths_.back()) / sampleRate_;
    feedbackGain_ = std::clamp(std::pow(0.001f, static_cast<float>(meanDelay / decay)),
                               0.0f, 0.98f);
    lowFeedbackGain_ = std::pow(0.001f, static_cast<float>(
        meanDelay / std::max(0.1f, decay * current_.lowDecay)));
    highFeedbackGain_ = std::pow(0.001f, static_cast<float>(
        meanDelay / std::max(0.1f, decay * current_.highDecay)));
    for (std::size_t tap = 0; tap < earlyTapCount; ++tap)
        earlyDelays_[tap] = std::min(inputDelayLeft_.size() - 1,
                                      static_cast<std::size_t>(tapTimes[tap] * sampleRate_));
}

float AlgorithmicReverb::nextDelayedSample(const std::size_t line) noexcept {
    const auto capacity = delays_[line].size();
    const auto readPosition = (positions_[line] + capacity - lengths_[line]) % capacity;
    const auto value = delays_[line][readPosition];
    return value;
}

void AlgorithmicReverb::process(const std::span<float> left, const std::span<float> right) {
    if (left.size() != right.size() || left.size() > maximumBlockSize_)
        throw std::invalid_argument("Unsupported algorithmic reverb block");
    for (std::size_t sample = 0; sample < left.size(); ++sample) {
        const auto ramp = parameterRampSamples_ == 0 ? 1.0f
            : 1.0f / static_cast<float>(parameterRampSamples_);
        current_.roomSize += (target_.roomSize - current_.roomSize) * ramp;
        current_.decaySeconds += (target_.decaySeconds - current_.decaySeconds) * ramp;
        current_.preDelayMs += (target_.preDelayMs - current_.preDelayMs) * ramp;
        current_.earlyLevel += (target_.earlyLevel - current_.earlyLevel) * ramp;
        current_.lateLevel += (target_.lateLevel - current_.lateLevel) * ramp;
        current_.earlyLateBalance += (target_.earlyLateBalance - current_.earlyLateBalance) * ramp;
        current_.lowDecay += (target_.lowDecay - current_.lowDecay) * ramp;
        current_.highDecay += (target_.highDecay - current_.highDecay) * ramp;
        current_.highDamping += (target_.highDamping - current_.highDamping) * ramp;
        current_.width += (target_.width - current_.width) * ramp;
        current_.wet += (target_.wet - current_.wet) * ramp;
        current_.dry += (target_.dry - current_.dry) * ramp;
        current_.outputGain += (target_.outputGain - current_.outputGain) * ramp;
        if (derivedUpdateCounter_ == 0) {
            for (std::size_t line = 0; line < lineCount; ++line)
                lengths_[line] = std::clamp(
                    static_cast<std::size_t>(baseLengths[line] * (0.75 + 0.75 * current_.roomSize)),
                    std::size_t{1}, delays_[line].size() - 1);
            updateDerivedParameters();
            derivedUpdateCounter_ = 64;
        } else {
            --derivedUpdateCounter_;
        }
        const auto inputL = left[sample];
        const auto inputR = right[sample];
        inputDelayLeft_[inputPosition_] = inputL;
        inputDelayRight_[inputPosition_] = inputR;
        const auto preDelay = std::min(inputDelayLeft_.size() - 1,
            static_cast<std::size_t>(std::max(0.0f, current_.preDelayMs) * sampleRate_ / 1000.0));
        const auto delayedPosition = (inputPosition_ + inputDelayLeft_.size() - preDelay)
                                   % inputDelayLeft_.size();
        const auto lateInput = 0.5f * (inputDelayLeft_[delayedPosition] + inputDelayRight_[delayedPosition]);
        float earlyL = 0.0f;
        float earlyR = 0.0f;
        for (std::size_t tap = 0; tap < earlyTapCount; ++tap) {
            const auto position = (inputPosition_ + inputDelayLeft_.size() - earlyDelays_[tap])
                                % inputDelayLeft_.size();
            const auto mono = 0.5f * (inputDelayLeft_[position] + inputDelayRight_[position]);
            const auto pan = earlyPan_[tap];
            earlyL += mono * earlyGains_[tap] * (1.0f - pan);
            earlyR += mono * earlyGains_[tap] * (1.0f + pan);
        }
        std::array<float, lineCount> delayed{};
        for (std::size_t line = 0; line < lineCount; ++line)
            delayed[line] = nextDelayedSample(line);
        std::array<float, lineCount> mixed{};
        for (std::size_t line = 0; line < lineCount; ++line) {
            float hadamard = 0.0f;
            for (std::size_t source = 0; source < lineCount; ++source) {
                hadamard += (evenParity(line & source) ? 1.0f : -1.0f) * delayed[source];
            }
            hadamard *= oneOverSqrtEight;
            const auto damp = current_.highDamping * 0.5f;
            dampingState_[line] += (hadamard - dampingState_[line]) * (1.0f - damp);
            const auto feedbackScale = std::clamp(
                0.5f * (lowFeedbackGain_ + highFeedbackGain_), 0.0f, 0.98f);
            const auto feedback = dampingState_[line] * feedbackScale;
            mixed[line] = std::clamp(lateInput + feedback, -4.0f, 4.0f);
            delays_[line][positions_[line]] = mixed[line];
            positions_[line] = (positions_[line] + 1) % delays_[line].size();
        }
        inputPosition_ = (inputPosition_ + 1) % inputDelayLeft_.size();
        const auto lateL = (delayed[0] + delayed[2] - delayed[4] - delayed[6]) * 0.25f;
        const auto lateR = (delayed[1] + delayed[3] - delayed[5] - delayed[7]) * 0.25f;
        const auto lateMid = 0.5f * (lateL + lateR);
        const auto lateSide = 0.5f * (lateL - lateR) * current_.width;
        const auto balance = std::clamp(current_.earlyLateBalance, 0.0f, 1.0f);
        const auto wetEarly = current_.earlyLevel * (1.0f - balance);
        const auto wetLate = current_.lateLevel * balance;
        left[sample] = (inputL * current_.dry
                      + (earlyL * (1.0f - current_.width * 0.5f) * wetEarly
                         + (lateMid + lateSide) * wetLate) * current_.wet) * current_.outputGain;
        right[sample] = (inputR * current_.dry
                       + (earlyR * (1.0f - current_.width * 0.5f) * wetEarly
                          + (lateMid - lateSide) * wetLate) * current_.wet) * current_.outputGain;
        if (!std::isfinite(left[sample])) left[sample] = 0.0f;
        if (!std::isfinite(right[sample])) right[sample] = 0.0f;
    }
}

} // namespace grand_hall
