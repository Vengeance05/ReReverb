#include "grand_hall/reverb_engine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grand_hall {

void ReverbEngine::prepare(const double sampleRate, const std::size_t maximumBlockSize,
                           const std::size_t irLength) {
    if (sampleRate <= 0.0 || maximumBlockSize == 0 || irLength == 0)
        throw std::invalid_argument("Invalid reverb preparation parameters");
    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
    earlyLeftConvolver_.prepare(maximumBlockSize, irLength);
    lateLeftConvolver_.prepare(maximumBlockSize, irLength);
    earlyRightConvolver_.prepare(maximumBlockSize, irLength);
    lateRightConvolver_.prepare(maximumBlockSize, irLength);
    earlyWetLeft_.assign(maximumBlockSize, 0.0f);
    lateWetLeft_.assign(maximumBlockSize, 0.0f);
    earlyWetRight_.assign(maximumBlockSize, 0.0f);
    lateWetRight_.assign(maximumBlockSize, 0.0f);
    const auto delayCapacity = std::max<std::size_t>(
        maximumBlockSize * 2,
        static_cast<std::size_t>(sampleRate * 0.25) + 1);
    delayLeft_.assign(delayCapacity, 0.0f);
    delayRight_.assign(delayCapacity, 0.0f);
    delayPosition_ = 0;
}

void ReverbEngine::setImpulseResponse(const std::span<const float> left,
                                      const std::span<const float> right) {
    if (left.size() != right.size() || left.empty())
        throw std::invalid_argument("Stereo impulse responses must have equal non-zero lengths");
    std::vector<float> empty(left.size(), 0.0f);
    setImpulseResponseComponents(empty, left, empty, right);
}

void ReverbEngine::setImpulseResponseComponents(
    const std::span<const float> earlyLeft, const std::span<const float> lateLeft,
    const std::span<const float> earlyRight, const std::span<const float> lateRight) {
    if (earlyLeft.size() != lateLeft.size() || earlyRight.size() != lateRight.size()
        || earlyLeft.size() != earlyRight.size() || earlyLeft.empty())
        throw std::invalid_argument("Stereo early/late impulse responses must be equal and non-zero");
    earlyLeftConvolver_.setImpulseResponse(earlyLeft);
    lateLeftConvolver_.setImpulseResponse(lateLeft);
    earlyRightConvolver_.setImpulseResponse(earlyRight);
    lateRightConvolver_.setImpulseResponse(lateRight);
}

void ReverbEngine::reset() noexcept {
    earlyLeftConvolver_.reset();
    lateLeftConvolver_.reset();
    earlyRightConvolver_.reset();
    lateRightConvolver_.reset();
    std::fill(earlyWetLeft_.begin(), earlyWetLeft_.end(), 0.0f);
    std::fill(lateWetLeft_.begin(), lateWetLeft_.end(), 0.0f);
    std::fill(earlyWetRight_.begin(), earlyWetRight_.end(), 0.0f);
    std::fill(lateWetRight_.begin(), lateWetRight_.end(), 0.0f);
    std::fill(delayLeft_.begin(), delayLeft_.end(), 0.0f);
    std::fill(delayRight_.begin(), delayRight_.end(), 0.0f);
    delayPosition_ = 0;
}

void ReverbEngine::process(const std::span<float> left, const std::span<float> right) {
    if (left.size() != right.size() || left.size() > maximumBlockSize_)
        throw std::invalid_argument("Unsupported reverb block");
    earlyLeftConvolver_.process(left, {earlyWetLeft_.data(), left.size()});
    lateLeftConvolver_.process(left, {lateWetLeft_.data(), left.size()});
    earlyRightConvolver_.process(right, {earlyWetRight_.data(), right.size()});
    lateRightConvolver_.process(right, {lateWetRight_.data(), right.size()});
    const auto delay = std::min<std::size_t>(
        delayLeft_.size() - 1,
        static_cast<std::size_t>(std::max(0.0f, parameters_.preDelayMs) * sampleRate_ / 1000.0));
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto balance = std::clamp(parameters_.earlyLateBalance, 0.0f, 1.0f);
        const auto earlyGain = 2.0f * parameters_.earlyLevel * (1.0f - balance);
        const auto lateGain = 2.0f * parameters_.lateLevel * balance;
        delayLeft_[delayPosition_] = earlyWetLeft_[i] * earlyGain
                                   + lateWetLeft_[i] * lateGain;
        delayRight_[delayPosition_] = earlyWetRight_[i] * earlyGain
                                    + lateWetRight_[i] * lateGain;
        const auto delayed = (delayPosition_ + delayLeft_.size() - delay) % delayLeft_.size();
        const auto mid = 0.5f * (delayLeft_[delayed] + delayRight_[delayed]);
        const auto side = 0.5f * (delayLeft_[delayed] - delayRight_[delayed]) * parameters_.width;
        const auto wetL = (mid + side) * parameters_.wet;
        const auto wetR = (mid - side) * parameters_.wet;
        const auto dryL = left[i] * parameters_.dry;
        const auto dryR = right[i] * parameters_.dry;
        const auto gain = parameters_.outputGain * parameters_.inputGain;
        left[i] = (dryL + wetL) * gain;
        right[i] = (dryR + wetR) * gain;
        delayPosition_ = (delayPosition_ + 1) % delayLeft_.size();
    }
}

} // namespace grand_hall
