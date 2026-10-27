// Early-reflection engine shared by Vista Rooms (reflection section) and
// Septima (synthetic ER sets). Physical patterns use a 3rd-order image-source
// model of a rectangular room; non-physical patterns (Scattered, Nonlin U)
// are generated procedurally. Tap sets are crossfaded when settings change.
#pragma once

#include "rr/dsp.h"

#include <array>
#include <atomic>
#include <vector>

namespace rr {

enum class ErPattern : int {
    Room = 0, Studio, Chamber, Hall, Stage, Cathedral, Scattered, NonlinU, Count
};

const char* erPatternName(ErPattern p) noexcept;

enum class ChorusMode : int { Off = 0, Fine, Light, Moderate, EaseIn };

struct ErSettings {
    ErPattern pattern = ErPattern::Room;
    float size = 0.5f;          // 0..1 -> 0.25x .. 3x of pattern base dimensions
    float proximity = 0.4f;     // 0 near source .. 1 far from source
    float reflectivity = 0.6f;  // 0 absorbent .. 1 hard surfaces
    float diffusion = 0.5f;     // 0 specular .. 1 very diffuse
    float modDepth = 0.2f;      // 0..1
    ChorusMode chorus = ChorusMode::Off;
    float crossfeed = 0.5f;     // 0..1 level of reflections reaching the opposite channel
    float crossfeedDelayMs = 0.0f;   // 0..100
    float crossfeedRolloffHz = 20000.0f;
    float width = 1.0f;         // 0 mono .. 1 full stereo
    float rolloffHz = 20000.0f;
    int rolloffSlope = 12;      // 6/12/18/24
    std::uint32_t seed = 1;

    bool operator==(const ErSettings&) const = default;
};

// Builds settings that approximate a numbered ER set (0..31). Low sets are
// small spaces, high sets are large spaces.
ErSettings erSetSettings(int setIndex) noexcept;

class EarlyReflections {
public:
    static constexpr int kMaxTaps = 64;
    static constexpr double kMaxErSeconds = 0.75;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setSettings(const ErSettings& s) noexcept;

    // Processes stereo input into stereo reflections (wet only). in/out may alias.
    void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    // Tap times (s) and gains for display purposes (all paths, both inputs).
    static void previewTaps(const ErSettings& s, double sampleRate, std::vector<std::pair<float, float>>& out);

    // Longest reflection delay of the current tap set (seconds).
    double longestTapSeconds() const noexcept;

    // Diagnostics for tests.
    int tapCount(int input, int output, bool cross) const noexcept;

private:
    struct Tap { int delay = 0; float gain = 0.0f; int order = 1; };
    struct TapList { std::array<Tap, kMaxTaps> taps{}; int count = 0; };
    // [input][output] tap lists; input==output is the "own" path.
    struct TapSet { std::array<std::array<TapList, 2>, 2> lists{}; };

    void generate(const ErSettings& s, TapSet& out) const noexcept;
    void generatePhysical(const ErSettings& s, TapSet& out) const noexcept;
    void generateProcedural(const ErSettings& s, TapSet& out) const noexcept;
    void applyDerived(const ErSettings& s) noexcept;
    void setAbsorb(float logHz) noexcept;
    float absorbCurLog_ = 0.0f, absorbTargetLog_ = 0.0f;
    bool absorbPrimed_ = false;
    float renderSet(const TapSet& set, int output, bool cross, int order) const noexcept;

    double fs_ = 48000.0;
    ErSettings settings_{};
    ErSettings pendingSettings_{};
    bool pendingDirty_ = false;
    std::array<TapSet, 2> sets_{};
    int active_ = 0;
    int fadeRemaining_ = 0;
    int fadeLength_ = 1;

    std::array<DelayLine, 2> input_{};
    // Absorption cascades [output][own/cross][stage]
    std::array<std::array<std::array<OnePoleLowpass, 3>, 2>, 2> absorb_{};
    std::array<RollOff, 2> crossRolloff_{};
    std::array<std::array<Allpass, 4>, 2> diffusers_{};
    std::array<std::array<Allpass, 4>, 2> preDiff_{};   // short, dense smearing of the reflection sum
    // Surface scattering: higher-order reflections are smeared by allpasses in the
    // absorption cascade ([output][own/cross][k]: k 0,1 after the 2nd-order stage,
    // k 2,3 after the 3rd-order stage), so later reflections arrive as dense
    // clusters instead of comb-filtering discrete copies.
    std::array<std::array<std::array<Allpass, 4>, 2>, 2> scatter_{};
    std::array<std::array<std::array<float, 4>, 2>, 2> scatterDelay_{}, scatterDelayCur_{};
    std::array<std::array<float, 4>, 2> diffuserDelay_{};
    std::array<std::array<RandomLfo, 4>, 2> diffuserLfo_{};
    std::array<std::array<DelayLine, 2>, 2> vibrato_{};   // [output][own/cross]
    std::array<std::array<RandomLfo, 2>, 2> vibratoLfo_{};
    std::array<RollOff, 2> rolloff_{};
    float diffusionGain_ = 0.0f;
    float diffuserModSamples_ = 0.0f;
    std::array<float, 2> vibratoDepth_{}; // own, cross
    // Smoothed copies of the derived values above (changing them abruptly clicks).
    std::array<std::array<float, 4>, 2> diffuserDelayCur_{};
    float diffusionGainCur_ = 0.0f, diffuserModCur_ = 0.0f, preGain_ = 0.6f, preGainCur_ = 0.6f;
    std::array<float, 2> vibratoDepthCur_{};
    float smoothCoef_ = 0.999f;
    int controlCountdown_ = 0;
    bool scatterSettled_ = false;
    bool derivedSettled_ = false;
    void snapSmoothed() noexcept {
        diffuserDelayCur_ = diffuserDelay_; diffusionGainCur_ = diffusionGain_; scatterDelayCur_ = scatterDelay_; preGainCur_ = preGain_;
        diffuserModCur_ = diffuserModSamples_; vibratoDepthCur_ = vibratoDepth_;
        controlCountdown_ = 0;
        derivedSettled_ = false;
    }
};

} // namespace rr
