#pragma once

#include "grand_hall/ir_processing.h"
#include "grand_hall/reverb_engine.h"
#include "grand_hall/wav_reader.h"

#include <string>

namespace grand_hall {

struct ImpulseResponseMetadata {
    std::string name = "unknown";
    std::string source = "unknown";
    std::string licence = "unknown";
    std::string redistribution = "user-import only";
    double originalSampleRate = 0.0;
    std::uint16_t originalChannels = 0;
    double originalDurationSeconds = 0.0;
    double referenceDecaySeconds = 0.0;
    std::string notes = "unknown";
};

struct PreparedResponse {
    ReverbEngine engine;
    ImpulseResponseMetadata metadata;
};

PreparedResponse prepareWavResponse(const std::string& path,
                                    double hostSampleRate,
                                    std::size_t maximumBlockSize,
                                    DecayShape shape = {});

} // namespace grand_hall
