#include "grand_hall/five_band_eq.h"

#include <algorithm>
#include <cmath>

namespace grand_hall {

float FiveBandEq::Biquad::processLeft(const float input) noexcept {
    const auto output = b0 * input + z1Left;
    z1Left = b1 * input - a1 * output + z2Left;
    z2Left = b2 * input - a2 * output;
    return output;
}

float FiveBandEq::Biquad::processRight(const float input) noexcept {
    const auto output = b0 * input + z1Right;
    z1Right = b1 * input - a1 * output + z2Right;
    z2Right = b2 * input - a2 * output;
    return output;
}

void FiveBandEq::prepare(const double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    smoothingSamples_ = static_cast<std::size_t>(sampleRate * 0.02);
    targetBands_ = bands_;
    updateCoefficients();
    reset();
}

void FiveBandEq::reset() noexcept {
    for (auto& filter : filters_) {
        filter.z1Left = filter.z2Left = filter.z1Right = filter.z2Right = 0.0f;
    }
}

void FiveBandEq::setBands(const std::array<EqBand, 5>& bands) noexcept {
    targetBands_ = bands;
}

void FiveBandEq::updateCoefficients() noexcept {
    constexpr auto pi = 3.14159265358979323846;
    for (std::size_t index = 0; index < filters_.size(); ++index) {
        const auto frequency = std::clamp<double>(bands_[index].frequencyHz, 20.0, sampleRate_ * 0.45);
        const auto q = std::clamp<double>(bands_[index].q, 0.1, 10.0);
        const auto gain = std::clamp<double>(bands_[index].gainDb, -24.0, 24.0);
        const auto a = std::pow(10.0, gain / 40.0);
        const auto omega = 2.0 * pi * frequency / sampleRate_;
        const auto alpha = std::sin(omega) / (2.0 * q);
        const auto cosOmega = std::cos(omega);
        double b0, b1, b2, a0, a1, a2;
        if (index == 0) {
            const auto beta = 2.0 * std::sqrt(a) * alpha;
            b0 = a * ((a + 1.0) - (a - 1.0) * cosOmega + beta);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosOmega);
            b2 = a * ((a + 1.0) - (a - 1.0) * cosOmega - beta);
            a0 = (a + 1.0) + (a - 1.0) * cosOmega + beta;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosOmega);
            a2 = (a + 1.0) + (a - 1.0) * cosOmega - beta;
        } else if (index == 4) {
            const auto beta = 2.0 * std::sqrt(a) * alpha;
            b0 = a * ((a + 1.0) + (a - 1.0) * cosOmega + beta);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosOmega);
            b2 = a * ((a + 1.0) + (a - 1.0) * cosOmega - beta);
            a0 = (a + 1.0) - (a - 1.0) * cosOmega + beta;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosOmega);
            a2 = (a + 1.0) - (a - 1.0) * cosOmega - beta;
        } else {
            b0 = 1.0 + alpha * a;
            b1 = -2.0 * cosOmega;
            b2 = 1.0 - alpha * a;
            a0 = 1.0 + alpha / a;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha / a;
        }
        auto& filter = filters_[index];
        filter.b0 = static_cast<float>(b0 / a0);
        filter.b1 = static_cast<float>(b1 / a0);
        filter.b2 = static_cast<float>(b2 / a0);
        filter.a1 = static_cast<float>(a1 / a0);
        filter.a2 = static_cast<float>(a2 / a0);
    }
}

void FiveBandEq::process(const std::span<float> left, const std::span<float> right) noexcept {
    for (auto index = std::size_t{}; index < bands_.size(); ++index) {
        const auto step = smoothingSamples_ == 0 ? 1.0f
            : 1.0f / static_cast<float>(smoothingSamples_);
        bands_[index].frequencyHz += (targetBands_[index].frequencyHz - bands_[index].frequencyHz) * step;
        bands_[index].gainDb += (targetBands_[index].gainDb - bands_[index].gainDb) * step;
        bands_[index].q += (targetBands_[index].q - bands_[index].q) * step;
    }
    updateCoefficients();
    const auto count = std::min(left.size(), right.size());
    for (std::size_t sample = 0; sample < count; ++sample) {
        auto l = left[sample];
        auto r = right[sample];
        for (auto& filter : filters_) {
            l = filter.processLeft(l);
            r = filter.processRight(r);
        }
        left[sample] = l;
        right[sample] = r;
    }
}

} // namespace grand_hall
