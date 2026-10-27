#include "grand_hall/prepared_response.h"

#include <filesystem>
#include <stdexcept>

namespace grand_hall {

PreparedResponse prepareWavResponse(const std::string& path, const double hostSampleRate,
                                    const std::size_t maximumBlockSize,
                                    const DecayShape shape) {
    if (hostSampleRate <= 0.0 || maximumBlockSize == 0)
        throw std::invalid_argument("Invalid host preparation parameters");
    const auto source = readWavImpulseResponse(path);
    const auto left = shapeDecay(resampleLinear(source.left, source.sampleRate, hostSampleRate),
                                 hostSampleRate, shape);
    const auto right = shapeDecay(resampleLinear(source.right, source.sampleRate, hostSampleRate),
                                  hostSampleRate, shape);
    if (left.size() != right.size() || left.empty())
        throw std::runtime_error("Prepared stereo impulse response is invalid");
    PreparedResponse result;
    const auto leftComponents = splitImpulseResponse(left, hostSampleRate);
    const auto rightComponents = splitImpulseResponse(right, hostSampleRate);
    result.engine.prepare(hostSampleRate, maximumBlockSize, left.size());
    result.engine.setImpulseResponseComponents(leftComponents.early, leftComponents.late,
                                               rightComponents.early, rightComponents.late);
    result.metadata.name = std::filesystem::path(path).filename().string();
    result.metadata.source = path;
    result.metadata.originalSampleRate = source.sampleRate;
    result.metadata.originalChannels = source.channels;
    result.metadata.originalDurationSeconds = source.left.size() / source.sampleRate;
    result.metadata.referenceDecaySeconds = shape.referenceSeconds;
    result.metadata.notes = "User-imported WAV; licence and redistribution status must be verified externally.";
    return result;
}

} // namespace grand_hall
