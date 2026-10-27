// Vista Rooms late-reverb engine: two 32-line modulated feedback delay
// networks (one per input channel) with decorrelated crossfeed, two-band
// decay (bass / mid / treble multipliers), bloom, undulation, low boost and echo.
#pragma once

#include "rr/dsp.h"
#include "rr/early_reflections.h" // ChorusMode

#include <array>

namespace rr {

struct LateSettings {
    float rt60 = 2.5f;            // seconds, 0.2 .. 45
    bool infinite = false;        // freeze: lossless loop, input muted
    // Frequency-dependent decay: the reverb time is multiplied by bassMult below
    // bassHz and by trebleMult above trebleHz (gentle crossovers).
    float bassMult = 1.0f;        // 0.25 .. 4
    float bassHz = 500.0f;
    float trebleMult = 0.85f;     // 0.1 .. 2
    float trebleHz = 6000.0f;
    float lowBoostDb = 0.0f;      // 0 .. 12
    float preDelayMs = 20.0f;     // 0 .. 500
    float bloom = 0.3f;           // 0 .. 1
    float undulation = 0.0f;      // 0 .. 1
    float size = 0.5f;            // 0 .. 1 line-length scale
    float modDepth = 0.3f;        // 0 .. 1
    ChorusMode chorus = ChorusMode::Light;
    float crossfeed = 0.7f;       // 0 .. 1
    float crossfeedDelayMs = 0.0f;
    float crossfeedRolloffHz = 20000.0f;
    float width = 1.0f;           // 0 .. 1 (input feed + output)
    float rolloffHz = 20000.0f;
    int rolloffSlope = 12;
    float echoMs = 250.0f;        // 100 .. 2000
    float echoLevel = 0.0f;       // 0 .. 1
    float echoRolloffHz = 8000.0f;
};

class LateReverb {
public:
    static constexpr int kLines = 32;
    static constexpr int kBloomTaps = 24;
    static constexpr double kMaxPreDelay = 0.5;
    static constexpr double kMaxEcho = 2.0;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setSettings(const LateSettings& s) noexcept;
    // Wet late output only. in/out may alias.
    void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

private:
    struct Network {
        std::array<DelayLine, kLines> lines;
        std::array<OnePoleLowpass, kLines> tiltLo;            // 1st-order bass split
        std::array<FirstOrder, kLines> tiltHi;                    // 1st-order (bilinear) treble split
        std::array<RandomLfo, kLines> lfo;
        std::array<float, kLines> baseLength{};   // samples at size scale 1
        std::array<float, kLines> gLow{}, gMid{}, gHigh{};
        std::array<float, kLines> apAtt{}, ap2Att{};
        std::array<float, kLines> fracY{};
        // Per-control-block read positions: loop read (integer + allpass coefficient)
        // and output tap (integer + linear fraction).
        std::array<int, kLines> rdN{}, tapN{};
        std::array<float, kLines> rdA{}, tapF{};   // allpass-interpolator state per line   // per-pass loss inside the loop allpasses
        std::array<float, kLines> inSign{}, ownTap{}, crossTap{};
        DelayLine bloomLine;
        std::array<float, 24> bloomDelay{}, bloomGain{};
        float bloomDirect = 1.0f;
        std::array<Allpass, 6> inputDiffuser;
        std::array<Allpass, kLines> loopAp, loopAp2;
        std::array<float, kLines> loopApLen{}, loopAp2Len{};   // samples at size scale 1
        // Current (gliding) delays of the static taps. They settle on whole samples,
        // where the cheaper integer reads give bit-identical results.
        std::array<float, kLines> apCur{}, ap2Cur{}, stagCur{};
        bool apSettled = false, stagSettled = false;
        DelayLine stagger, staggerLow;
        DelayLine crossVibrato;
        RandomLfo crossLfo;
        RollOff crossRolloff;
        OnePoleLowpass undLow1, undLow2;
    };

    void updateControl(int samples) noexcept;

    double fs_ = 48000.0;
    LateSettings settings_{};
    std::array<Network, 2> nets_{};
    std::array<DelayLine, 2> preDelay_{};
    std::array<RollOff, 2> echoRolloff_{};
    std::array<RollOff, 2> outRolloff_{};

    // Low boost: small 4-line network fed by the low band.
    std::array<DelayLine, 4> lbLines_{};
    std::array<float, 4> lbLength_{};
    std::array<float, 4> lbGain_{};
    OnePoleLowpass lbLp1_, lbLp2_;
    float lbLevel_ = 0.0f;
    bool lbRunning_ = false;

    Smoother rtLog_, sizeScale_, bassLog_, trebleLog_, inputGate_, ease_;
    // Per-sample smoothing of everything that would otherwise step (and click) when automated.
    Smoother preDelaySm_, echoSm_, echoLevelSm_, widthSm_, crossfeedSm_, crossDelaySm_, undSm_,
             lineDepthSm_, crossDepthSm_, lbLevelSm_, staggerSm_, infAmt_;
    EnvelopeFollower easeDetector_;
    float silentSeconds_ = 0.0f;
    int controlCounter_ = 0;
    float modDepthSamples_ = 0.0f;
    float lineModScale_ = 1.0f, crossModScale_ = 1.0f;
    bool easeIn_ = false;
    bool bloomActive_ = false;
    std::array<float, kLines> injGain_{};
    // Inputs the loop gains were last computed from (skip the work when unchanged).
    std::array<float, 5> gainKey_{};
    bool gainKeyValid_ = false;
    float staggerSamples_ = 0.0f;
    float effScale_ = 1.0f;
    float loopApGain_ = 0.55f;
};

} // namespace rr
