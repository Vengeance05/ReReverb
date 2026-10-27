// Live (real-time) part of Septima: everything around the convolutions.
//
//   dry ─┬─────────────────────────────► early convolution (true stereo) ─┐
//        │      (or synthetic ER set) ─────────────────────────────────────┤
//        └─► pre-delay + delay feed ───► late convolution (true stereo) ───┤─► early/late balance,
//                                   └──► VLF convolution (mono in) ────────┘   roll-offs, VLF level,
//                                                                              tail modulation, EQ, mix
#pragma once

#include "rr/early_reflections.h"
#include "rr/master_eq.h"

#include <vector>

namespace rr {

struct SeptimaSettings {
    float preDelayMs = 0.0f;       // 0..500, late + VLF only
    float delayMs = 250.0f;        // 0..1000 delayed copy into the late line
    float delayLevel = 0.0f;       // 0..1
    int erSet = 0;                 // 0 = sampled early reflections, 1..32 = synthetic set 0..31
    float earlyLate = 0.5f;        // 0 early only .. 0.5 both .. 1 late only
    float vlfLevel = 1.0f;         // 0..2 (1 = as sampled)
    float earlyRolloffHz = 20000.0f;
    float lateRolloffHz = 20000.0f;
    float modulation = 0.25f;      // 0..1 tail (and synthetic ER) modulation
    bool eqOn = true;
    std::array<EqBandSettings, 4> eq{{
        {EqType::LowShelf, 150.0f, 0.0f, 0.707f},
        {EqType::Peak, 1000.0f, 0.0f, 0.707f},
        {EqType::HighShelf, 6000.0f, 0.0f, 0.707f},
        {EqType::HighCut, 20000.0f, 0.0f, 0.707f},
    }};
    float mix = 1.0f;
    float gainDb = 0.0f;
    float syntheticErGain = 1.0f;  // matches synthetic ER level to the sampled early energy
};

class SeptimaEngine {
public:
    static constexpr double kMaxPreDelay = 0.5;
    static constexpr double kMaxDelay = 1.0;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setSettings(const SeptimaSettings& s) noexcept;

    // Stage 1: builds the late (stereo) and VLF (mono) convolution feeds.
    void makeFeeds(const float* inL, const float* inR, float* lateL, float* lateR,
                   float* vlfMono, int numSamples) noexcept;

    // Stage 2: `left/right` hold the dry input on entry and the output on exit.
    void finish(float* left, float* right,
                const float* earlyL, const float* earlyR,
                const float* lateL, const float* lateR,
                const float* vlfL, const float* vlfR, int numSamples) noexcept;

    // False while a synthetic ER set has fully replaced the sampled early
    // reflections: the host can skip the early convolution. When the sampled
    // early IR comes back, the synthetic set is held for one early-IR length so
    // the convolution has real history before the crossfade.
    bool wantsSampledEarly() const noexcept { return sampledActive_; }

    float meterIn() const noexcept { return mIn_; }
    float meterEarly() const noexcept { return mEarly_; }
    float meterLate() const noexcept { return mLate_; }
    float meterVlf() const noexcept { return mVlf_; }
    float meterOut() const noexcept { return mOut_; }

private:
    double fs_ = 48000.0;
    int maxBlock_ = 1;
    SeptimaSettings s_{};
    std::array<DelayLine, 2> feed_{};
    Smoother preDelay_, delayTime_, delayLevel_;
    EarlyReflections synthEr_;
    std::array<RollOff, 2> earlyRoll_{}, lateRoll_{}, vlfRoll_{};
    std::array<DelayLine, 2> modLine_{}, earlyAlign_{};
    std::array<RandomLfo, 2> modLfo_{};
    MasterEq<4> eq_;
    Smoother earlyGain_, lateGain_, vlfGain_, dryGain_, wetGain_, outGain_;
    float synthMix_ = 0.0f, synthStep_ = 0.001f;   // sampled <-> synthetic ER crossfade
    int synthWarmup_ = 0;                            // samples to run the synthetic ER before fading it in
    bool synthRunning_ = false;
    bool sampledActive_ = true;
    int sampledWarmup_ = 0;
    std::vector<float> erL_, erR_, wetL_, wetR_;
    float mIn_ = 0, mEarly_ = 0, mLate_ = 0, mVlf_ = 0, mOut_ = 0;
};

} // namespace rr
