#include "grand_hall/ir_processing.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace grand_hall {

std::vector<float> resampleLinear(const std::span<const float> input,
                                  const double sourceRate,
                                  const double destinationRate) {
    if (input.empty() || sourceRate <= 0.0 || destinationRate <= 0.0)
        throw std::invalid_argument("Invalid impulse-response resampling input");
    if (sourceRate == destinationRate) return {input.begin(), input.end()};
    const auto outputSize = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(input.size() * destinationRate / sourceRate)));
    std::vector<float> output(outputSize);
    const auto ratio = sourceRate / destinationRate;
    const auto cutoff = std::min(1.0, destinationRate / sourceRate) * 0.94;
    constexpr int radius = 16;
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto source = i * ratio;
        const auto center = static_cast<long>(std::floor(source));
        float sum = 0.0f;
        double weightSum = 0.0;
        for (int offset = -radius + 1; offset <= radius; ++offset) {
            const auto index = center + offset;
            if (index < 0 || index >= static_cast<long>(input.size())) continue;
            const auto distance = source - static_cast<double>(index);
            const auto sincArgument = cutoff * distance;
            const auto sinc = std::abs(sincArgument) < 1.0e-12
                ? 1.0 : std::sin(pi * sincArgument) / (pi * sincArgument);
            const auto windowPosition = static_cast<double>(offset + radius - 1)
                                      / static_cast<double>(2 * radius - 1);
            const auto window = 0.5 * (1.0 - std::cos(2.0 * pi * windowPosition));
            const auto weight = cutoff * sinc * window;
            sum += input[static_cast<std::size_t>(index)] * static_cast<float>(weight);
            weightSum += weight;
        }
        output[i] = weightSum > 1.0e-12 ? static_cast<float>(sum / weightSum) : 0.0f;
    }
    return output;
}

std::vector<float> processImpulseResponse(const std::span<const float> input,
                                          const IrProcessingOptions options) {
    if (input.empty()) throw std::invalid_argument("Empty impulse response");
    std::size_t first = 0;
    std::size_t last = input.size();
    if (options.trimSilence) {
        while (first < last && std::abs(input[first]) <= options.trimThreshold) ++first;
        while (last > first && std::abs(input[last - 1]) <= options.trimThreshold) --last;
    }

    if (first == last) return {0.0f};
    std::vector<float> output(input.begin() + static_cast<std::ptrdiff_t>(first),
                              input.begin() + static_cast<std::ptrdiff_t>(last));
    if (options.peakNormalize) {
        const auto peak = *std::max_element(output.begin(), output.end(),
            [](const float a, const float b) { return std::abs(a) < std::abs(b); });
        if (std::abs(peak) > 0.0f)
            for (auto& sample : output) sample /= std::abs(peak);
    }
    return output;
}

std::vector<float> shapeDecay(const std::span<const float> input, const double sampleRate,
                              const DecayShape shape) {
    if (input.empty() || sampleRate <= 0.0 || shape.referenceSeconds <= 0.0f
        || shape.multiplier <= 0.0f || shape.lowMultiplier <= 0.0f
        || shape.highMultiplier <= 0.0f)
        throw std::invalid_argument("Invalid decay-shaping parameters");
    std::vector<float> output(input.begin(), input.end());
    const auto alpha = std::exp(-2.0 * 3.141592653589793 * shape.crossoverHz / sampleRate);
    float low = 0.0f;
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto time = static_cast<float>(i / sampleRate);
        low = static_cast<float>(alpha) * low + (1.0f - static_cast<float>(alpha)) * input[i];
        if (time <= shape.earlySeconds) continue;
        const auto tailTime = time - shape.earlySeconds;
        const auto lowTarget = shape.referenceSeconds * shape.multiplier * shape.lowMultiplier;
        const auto highTarget = shape.referenceSeconds * shape.multiplier * shape.highMultiplier;
        const auto lowGain = std::exp(-0.69314718056f * tailTime
                                      * (1.0f / lowTarget - 1.0f / shape.referenceSeconds));
        const auto highTargetWithDamping = highTarget / std::max(0.05f, 1.0f - shape.highDamping);
        const auto highGain = std::exp(-0.69314718056f * tailTime
                                       * (1.0f / highTargetWithDamping
                                          - 1.0f / shape.referenceSeconds));
        output[i] = low * static_cast<float>(lowGain)
                  + (input[i] - low) * static_cast<float>(highGain);
    }
    return output;
}

ImpulseResponseComponents splitImpulseResponse(const std::span<const float> input,
                                               const double sampleRate,
                                               const double earlyEndSeconds,
                                               const double lateStartSeconds) {
    if (input.empty() || sampleRate <= 0.0 || earlyEndSeconds < 0.0
        || lateStartSeconds <= earlyEndSeconds)
        throw std::invalid_argument("Invalid impulse-response split parameters");
    ImpulseResponseComponents result{std::vector<float>(input.size()),
                                     std::vector<float>(input.size())};
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto time = static_cast<double>(i) / sampleRate;
        float lateWeight = 0.0f;
        if (time >= lateStartSeconds) {
            lateWeight = 1.0f;
        } else if (time > earlyEndSeconds) {
            const auto position = (time - earlyEndSeconds)
                                / (lateStartSeconds - earlyEndSeconds);
            const auto smooth = position * position * (3.0 - 2.0 * position);
            lateWeight = static_cast<float>(smooth);
        }
        result.late[i] = input[i] * lateWeight;
        result.early[i] = input[i] * (1.0f - lateWeight);
    }
    return result;
}

} // namespace grand_hall
