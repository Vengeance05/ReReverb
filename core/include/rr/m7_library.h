// Scans a folder of impulse-response WAV/AIFF files (designed around the
// free Samplicity Bricasti M7 library, but tolerant of other naming) and
// groups files into programs with a category, number and channel format.
#pragma once

#include "rr/ir_reshaper.h"

#include <string>
#include <vector>

namespace rr {

enum class FileChannelTag { None, MonoToStereo, StereoToStereo, Left, Right };

struct ParsedIrName {
    std::string base;       // name without channel / sample-rate tags
    FileChannelTag tag = FileChannelTag::None;
    int sampleRateTag = 0;  // e.g. 44100, 48000, 96000; 0 if absent
};

ParsedIrName parseIrFileStem(const std::string& stem);

struct IrProgram {
    std::string category;   // e.g. "Halls"
    int categoryNumber = 99;
    int number = 0;         // program number within the category
    std::string name;       // e.g. "Large & Stage"
    std::string key;        // stable identifier (relative base path)
    IrFormat format = IrFormat::Stereo;
    int sampleRateTag = 0;
    // One file (mono, stereo, M-to-S, 4-channel) or two (true-stereo L/R).
    std::vector<std::string> files;
    std::string displayName() const;
};

// Groups already-listed file paths (relative or absolute) into programs.
std::vector<IrProgram> groupIrFiles(const std::vector<std::string>& paths);

// Recursively scans `root` for audio files and groups them.
std::vector<IrProgram> scanIrLibrary(const std::string& root);

} // namespace rr
