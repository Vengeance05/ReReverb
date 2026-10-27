// Complete Vista Rooms signal path (stereo):
//   input -> [early reflections] ----------------|
//         -> [late reverb] -> gate/duck(reverb) --+-> refl/reverb mix -> master EQ
//         -> duck/compress(wet) -> dry/wet mix -> master gain
#pragma once

#include "rr/dynamics.h"
#include "rr/early_reflections.h"
#include "rr/late_reverb.h"
#include "rr/master_eq.h"

#include <vector>

namespace rr {

struct VistaSettings {
    ErSettings er;
    LateSettings late;
    float reflReverbMix = 0.5f;   // 0 = reflections only, 0.5 = both, 1 = reverb only
    bool eqOn = true;
    std::array<EqBandSettings, 5> eq{{
        {EqType::LowCut, 20.0f, 0.0f, 0.707f},
        {EqType::LowShelf, 200.0f, 0.0f, 0.707f},
        {EqType::Peak, 1000.0f, 0.0f, 0.707f},
        {EqType::HighShelf, 6000.0f, 0.0f, 0.707f},
        {EqType::HighCut, 20000.0f, 0.0f, 0.707f},
    }};
    DynamicsSettings dynamics;
    float mix = 1.0f;             // 0 dry .. 1 wet
    float gainDb = 0.0f;
};

class VistaEngine {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setSettings(const VistaSettings& s) noexcept;
    void process(float* left, float* right, int numSamples) noexcept;

    // Metering (peak, linear) for the last block.
    float meterIn() const noexcept { return meterIn_; }
    float meterRefl() const noexcept { return meterRefl_; }
    float meterReverb() const noexcept { return meterReverb_; }
    float meterOut() const noexcept { return meterOut_; }
    float duckReductionDb() const noexcept { return dynamics_.currentReductionDb(); }

private:
    double fs_ = 48000.0;
    int maxBlock_ = 0;
    VistaSettings settings_{};
    EarlyReflections er_;
    LateReverb late_;
    MasterEq<5> eq_;
    Dynamics dynamics_;
    std::vector<float> dryL_, dryR_, erL_, erR_, lateL_, lateR_, duckWet_, compWeight_;
    Smoother erGain_, lateGain_, dryGain_, wetGain_, outGain_;
    Smoother duckRevW_, duckWetW_, compW_;   // where the dynamics apply (crossfaded on mode change)
    float meterIn_ = 0, meterRefl_ = 0, meterReverb_ = 0, meterOut_ = 0;
    // Idle skipping: silent input and a fully decayed wet signal (not frozen).
    int quietSamples_ = 0;
};

} // namespace rr
