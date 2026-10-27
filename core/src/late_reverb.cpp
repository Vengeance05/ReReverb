#include "rr/late_reverb.h"

#include <algorithm>
#include <cmath>

namespace rr {
namespace {

// Output tap signs (balanced; own and cross orthogonal).
constexpr std::array<std::array<float, 32>, 2> kOwnTap{{{-1, 1, -1, 1, -1, 1, 1, -1, -1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, -1, -1, -1, -1, 1, -1, 1, 1, -1, -1, 1, 1}, {-1, 1, -1, -1, 1, -1, -1, -1, -1, 1, 1, -1, -1, 1, -1, -1, 1, 1, 1, 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, 1}}};
constexpr std::array<std::array<float, 32>, 2> kCrossTap{{{1, 1, 1, -1, -1, -1, 1, 1, -1, 1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1, -1, 1, 1, 1, -1, 1, -1, -1, -1, 1}, {1, -1, -1, -1, -1, 1, -1, 1, -1, 1, 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, 1, 1}}};
// Line lengths (ms at size 1.0, ~29-103 ms; 32 per network so the tail has twice
// the modal density of a 16-line design without any time variation). Chosen by search so that the lines'
// modal series never line up: evenly spaced lengths would put many lines' modes
// on the same frequencies (multiples of 1/spacing) and colour the tail.
constexpr std::array<float, 32> kBaseMsA{30.09f, 30.82f, 31.63f, 32.56f, 33.89f, 34.99f, 36.17f, 37.78f, 38.39f, 40.01f, 41.03f, 43.59f, 44.23f, 44.86f, 47.94f, 52.03f, 52.97f, 54.72f, 56.36f, 59.31f, 62.52f, 63.13f, 64.72f, 65.52f, 69.70f, 72.66f, 74.93f, 78.25f, 82.41f, 87.03f, 91.38f, 92.38f};
constexpr std::array<float, 32> kBaseMsB{29.04f, 30.95f, 32.79f, 33.48f, 35.30f, 36.05f, 37.10f, 39.28f, 40.48f, 41.39f, 42.29f, 44.26f, 46.70f, 48.62f, 49.64f, 52.68f, 55.06f, 56.67f, 57.98f, 60.46f, 62.66f, 65.90f, 67.17f, 70.41f, 71.98f, 78.46f, 80.18f, 83.65f, 86.12f, 88.71f, 95.73f, 102.88f};
// Always-on input diffusion (ms) for each network.
constexpr std::array<std::array<float, 6>, 2> kInDiffMs{{{0.71f, 1.19f, 1.83f, 2.79f, 4.13f, 6.07f},
                                                        {0.79f, 1.31f, 1.97f, 3.01f, 4.43f, 6.53f}}};
constexpr std::array<float, 6> kInDiffGain{0.72f, 0.70f, 0.68f, 0.66f, 0.64f, 0.62f};
// In-loop allpass per line (ms at size 1.0): multiplies echo density every pass.
// Long enough to add real delay (modal density) to each loop, so the tail's fine
// spectral structure is dense rather than a set of faint persistent tones.
constexpr std::array<float, 32> kLoopApMs{8.62f, 9.44f, 10.26f, 11.08f, 11.90f, 12.72f, 13.54f, 14.36f, 15.17f, 15.99f, 16.81f, 17.63f, 18.45f, 19.27f, 20.09f, 20.91f, 21.73f, 22.55f, 23.37f, 24.19f, 25.01f, 25.83f, 26.65f, 27.47f, 28.28f, 29.10f, 29.92f, 30.74f, 31.56f, 32.38f, 33.20f, 34.02f};
constexpr std::array<float, 32> kLoopAp2Ms{3.82f, 4.13f, 4.45f, 4.76f, 5.08f, 5.39f, 5.71f, 6.02f, 6.34f, 6.65f, 6.97f, 7.28f, 7.60f, 7.91f, 8.23f, 8.54f, 8.86f, 9.17f, 9.49f, 9.80f, 10.12f, 10.43f, 10.75f, 11.06f, 11.38f, 11.69f, 12.01f, 12.32f, 12.64f, 12.95f, 13.27f, 13.58f};
// Moderate gain: enough density, but a strong in-loop allpass's frequency-
// dependent group delay colours the decay (measured against reference tails).
constexpr float kLoopApGain = 0.45f;
// Irregular (golden-ratio sequence) injection offsets for Bloom: evenly spaced
// offsets would form a comb filter.
constexpr std::array<float, 32> kStaggerPos = [] {
    std::array<float, 32> a{};
    for (int i = 0; i < 32; ++i) {
        const double v = static_cast<double>(i) * 0.6180339887498949;
        a[static_cast<std::size_t>(i)] = static_cast<float>(v - static_cast<double>(static_cast<long>(v)));
    }
    return a;
}();
constexpr std::array<float, 4> kLowBoostMs{19.3f, 26.9f, 33.1f, 41.9f};
constexpr float kInjectionScale = 0.17677670f;   // 1/sqrt(lines)
constexpr float kOutputScale = 0.25f;
constexpr float kMaxSizeScale = 1.55f;
constexpr float kMaxModSeconds = 0.0012f;
// Treble damping corner relative to the treble crossover, and the matching factor
// that makes the decay at the crossover come out at the treble multiplier.
constexpr float kTrebleCornerRatio = 1.25f;
constexpr float kBassCornerRatio = 1.0f;
// Time variation is kept small: a room is time-invariant, and strong modulation
// made the reverb sound detached from the source.
constexpr float kLateModScale = 0.25f;
constexpr float kTrebleExtra = 1.0f + kTrebleCornerRatio * kTrebleCornerRatio;
// Output tap positions inside each line (fraction of its length). Reading the
// lines' ends would put every return through line i at exactly its loop time,
// so the output would correlate with itself at each loop period (a faint
// periodic flutter); irregular interior taps spread those returns out.
constexpr std::array<std::array<float, 32>, 2> kTapFrac{{{0.67f, 0.82f, 0.76f, 0.45f, 0.50f, 0.81f, 0.33f, 0.78f, 0.77f, 0.59f, 0.50f, 0.48f, 0.47f, 0.57f, 0.61f, 0.63f, 0.88f, 0.77f, 0.67f, 0.87f, 0.45f, 0.42f, 0.67f, 0.35f, 0.35f, 0.61f, 0.59f, 0.83f, 0.68f, 0.61f, 0.60f, 0.47f}, {0.34f, 0.44f, 0.71f, 0.44f, 0.53f, 0.33f, 0.79f, 0.41f, 0.48f, 0.81f, 0.61f, 0.80f, 0.68f, 0.74f, 0.38f, 0.63f, 0.61f, 0.81f, 0.53f, 0.66f, 0.36f, 0.54f, 0.51f, 0.41f, 0.78f, 0.54f, 0.87f, 0.65f, 0.66f, 0.68f, 0.70f, 0.41f}}};

inline float juce_clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float sizeToScale(float size) noexcept { return 0.72f + 0.83f * std::clamp(size, 0.0f, 1.0f); }

} // namespace

void LateReverb::prepare(double sampleRate, int /*maxBlockSize*/) {
    fs_ = sampleRate;
    gainKeyValid_ = false;
    const auto fsf = static_cast<float>(fs_);
    for (auto& p : preDelay_) p.prepare(static_cast<std::size_t>((kMaxPreDelay + kMaxEcho + 0.05) * fs_));

    for (int n = 0; n < 2; ++n) {
        auto& net = nets_[static_cast<std::size_t>(n)];
        const auto& ms = n == 0 ? kBaseMsA : kBaseMsB;
        Random rng(static_cast<std::uint32_t>(0xC0FFEE + 7 * n));
        for (int i = 0; i < kLines; ++i) {
            const auto si = static_cast<std::size_t>(i);
            net.baseLength[si] = ms[si] * 0.001f * fsf;
            net.lines[si].prepare(static_cast<std::size_t>(net.baseLength[si] * kMaxSizeScale
                                                           + kMaxModSeconds * fsf) + 16);
            net.lfo[si].prepare(fs_, 0.15f + 0.035f * static_cast<float>(i) + 0.03f * static_cast<float>(n),
                                static_cast<std::uint32_t>(1000 + 37 * i + 4099 * n));
            net.inSign[si] = rng.uniform() < 0.5f ? -1.0f : 1.0f;
            net.ownTap[si] = kOwnTap[static_cast<std::size_t>(n)][si];
            net.crossTap[si] = kCrossTap[static_cast<std::size_t>(n)][si];
        }
        net.bloomLine.prepare(static_cast<std::size_t>(0.32 * fs_));
        for (auto& ap : net.inputDiffuser) ap.prepare(static_cast<std::size_t>(0.008 * fs_));
        for (int i = 0; i < kLines; ++i) {
            // Interleave lengths so short loop allpasses pair with long lines and vice versa.
            const auto si = static_cast<std::size_t>(i);
            net.loopApLen[si] = kLoopApMs[static_cast<std::size_t>((i * 7 + n * 3) % kLines)] * 0.001f * fsf;
            net.loopAp2Len[si] = kLoopAp2Ms[static_cast<std::size_t>((i * 11 + n * 5 + 3) % kLines)] * 0.001f * fsf;
            net.loopAp[si].prepare(static_cast<std::size_t>(net.loopApLen[si] * kMaxSizeScale) + 8);
            net.loopAp2[si].prepare(static_cast<std::size_t>(net.loopAp2Len[si] * kMaxSizeScale) + 8);
        }
        net.stagger.prepare(static_cast<std::size_t>(0.32 * fs_) + 8);
        net.staggerLow.prepare(static_cast<std::size_t>(0.32 * fs_) + 8);
        net.crossVibrato.prepare(static_cast<std::size_t>((0.11 + kMaxModSeconds) * fs_) + 8);
        net.crossLfo.prepare(fs_, 0.47f + 0.11f * static_cast<float>(n), static_cast<std::uint32_t>(777 + n));
        net.undLow1.setCutoff(fs_, 200.0);
        net.undLow2.setCutoff(fs_, 200.0);
    }
    for (int j = 0; j < 4; ++j) {
        lbLength_[static_cast<std::size_t>(j)] = kLowBoostMs[static_cast<std::size_t>(j)] * 0.001f * fsf;
        lbLines_[static_cast<std::size_t>(j)].prepare(static_cast<std::size_t>(lbLength_[static_cast<std::size_t>(j)]) + 8);
    }
    lbLp1_.setCutoff(fs_, 160.0);
    lbLp2_.setCutoff(fs_, 160.0);

    rtLog_.prepare(fs_, 80.0);
    sizeScale_.prepare(fs_, 400.0);
    bassLog_.prepare(fs_, 60.0);
    trebleLog_.prepare(fs_, 60.0);
    inputGate_.prepare(fs_, 40.0);
    ease_.prepare(fs_, 120.0);
    easeDetector_.prepare(fs_, 5.0, 60.0);
    for (auto* sm : {&preDelaySm_, &echoSm_, &crossDelaySm_, &staggerSm_}) sm->prepare(fs_, 50.0);
    for (auto* sm : {&echoLevelSm_, &widthSm_, &crossfeedSm_, &undSm_, &lineDepthSm_, &crossDepthSm_, &lbLevelSm_}) sm->prepare(fs_, 20.0);
    infAmt_.prepare(fs_, 40.0);

    rtLog_.snap(std::log(std::clamp(settings_.rt60, 0.2f, 45.0f)));
    sizeScale_.snap(sizeToScale(settings_.size));
    bassLog_.snap(std::log(std::clamp(settings_.bassMult, 0.25f, 4.0f)));
    trebleLog_.snap(std::log(std::clamp(settings_.trebleMult, 0.1f, 2.0f)));
    inputGate_.snap(settings_.infinite ? 0.0f : 1.0f);
    ease_.snap(1.0f);
    setSettings(settings_);
    for (auto* sm : {&preDelaySm_, &echoSm_, &crossDelaySm_, &staggerSm_, &echoLevelSm_, &widthSm_, &crossfeedSm_,
                     &undSm_, &lineDepthSm_, &crossDepthSm_, &lbLevelSm_, &infAmt_}) sm->snap(sm->target());
    updateControl(0);
    reset();
}

void LateReverb::reset() noexcept {
    for (auto& p : preDelay_) p.reset();
    for (auto& net : nets_) {
        for (auto& l : net.lines) l.reset();
        net.fracY.fill(0.0f);
        for (auto& t : net.tiltLo) t.reset();
        for (auto& t : net.tiltHi) t.reset();
        net.bloomLine.reset();
        for (auto& ap : net.inputDiffuser) ap.reset();
        for (auto& ap : net.loopAp) ap.reset();
        for (auto& ap : net.loopAp2) ap.reset();
        net.stagger.reset();
        net.staggerLow.reset();
        net.crossVibrato.reset();
        net.crossRolloff.reset();
        net.undLow1.reset();
        net.undLow2.reset();
    }
    for (auto& f : echoRolloff_) f.reset();
    for (auto& f : outRolloff_) f.reset();
    for (auto& l : lbLines_) l.reset();
    lbLp1_.reset(); lbLp2_.reset();
    easeDetector_.reset();
    silentSeconds_ = 0.0f;
}

void LateReverb::setSettings(const LateSettings& s) noexcept {
    settings_ = s;
    rtLog_.setTarget(std::log(std::clamp(s.rt60, 0.2f, 45.0f)));
    sizeScale_.setTarget(sizeToScale(s.size));
    bassLog_.setTarget(std::log(std::clamp(s.bassMult, 0.25f, 4.0f)));
    trebleLog_.setTarget(std::log(std::clamp(s.trebleMult, 0.1f, 2.0f)));
    inputGate_.setTarget(s.infinite ? 0.0f : 1.0f);

    for (auto& net : nets_) {
        net.crossRolloff.setup(fs_, s.crossfeedRolloffHz, 12);
        const float bHz = std::clamp(s.bassHz, 40.0f, 4000.0f);
        const float tHz = std::clamp(std::max(s.trebleHz, 1.5f * bHz), 500.0f, 18000.0f);
        // Two cascaded one-poles at 1.554x the crossover give a -3 dB point at the
        // crossover and a 12 dB/octave split, so the multiplier is fully applied
        // an octave away (matches reference decay curves).
        // Bass: a broad first-order split above the crossover; the multiplier is
        // reached about an octave below it (matches reference decay curves).
        for (auto& t : net.tiltLo) t.setCutoff(fs_, std::min(kBassCornerRatio * bHz, 0.45f * static_cast<float>(fs_)));
        Biquad trebleSplit;
        trebleSplit.setCoeffs(design::lowpass1(fs_, std::min(kTrebleCornerRatio * tHz, 0.45f * static_cast<float>(fs_))));
        // Treble: a first-order split a little above the crossover, so the extra
        // damping rises gradually through the treble range and keeps rising
        // above it (the reference decays keep shortening towards 12-16 kHz).
        for (auto& t : net.tiltHi) t.setCoeffs(trebleSplit.coeffs());
    }
    for (auto& f : echoRolloff_) f.setup(fs_, s.echoRolloffHz, 12);
    for (auto& f : outRolloff_) { f.setShape(RollOff::Shape::Cascade); f.setup(fs_, s.rolloffHz, s.rolloffSlope); }

    modDepthSamples_ = kLateModScale * std::clamp(s.modDepth, 0.0f, 1.0f) * 0.0009f * static_cast<float>(fs_);
    easeIn_ = s.chorus == ChorusMode::EaseIn;
    switch (s.chorus) {
        case ChorusMode::Off: lineModScale_ = 0.35f; crossModScale_ = 0.35f; break;
        case ChorusMode::Fine: lineModScale_ = 0.5f; crossModScale_ = 0.5f; break;
        case ChorusMode::Light: lineModScale_ = 0.35f; crossModScale_ = 1.0f; break;
        case ChorusMode::Moderate:
        case ChorusMode::EaseIn: lineModScale_ = 1.0f; crossModScale_ = 1.0f; break;
    }
    {
        const float b = std::clamp(s.bloom, 0.0f, 1.0f);
        bloomActive_ = false; // swell is produced by per-line staggered injection below
        const float span = (0.01f + 0.29f * b) * static_cast<float>(fs_); // up to 300 ms
        for (int n = 0; n < 2; ++n) {
            auto& net = nets_[static_cast<std::size_t>(n)];
            Random rng(static_cast<std::uint32_t>(0xB1005 + 31 * n));
            double energy = 0.0;
            net.bloomDirect = 1.0f - 0.85f * b;
            energy += net.bloomDirect * net.bloomDirect;
            for (int k = 0; k < kBloomTaps; ++k) {
                const auto sk = static_cast<std::size_t>(k);
                double pos = static_cast<double>(k + 1) * 0.6180339887498949 + 0.37 * n;
                pos -= static_cast<double>(static_cast<long>(pos));
                pos = std::sqrt(pos); // more taps late in the swell
                net.bloomDelay[sk] = std::max(1.0f, static_cast<float>(pos) * span);
                const float rise = 0.2f + 0.8f * static_cast<float>(pos);
                net.bloomGain[sk] = b * rise * (rng.uniform() < 0.5f ? -1.0f : 1.0f) * (0.7f + 0.3f * rng.uniform());
                energy += static_cast<double>(net.bloomGain[sk]) * net.bloomGain[sk];
            }
            const float norm = static_cast<float>(1.0 / std::sqrt(std::max(energy, 1e-9)));
            net.bloomDirect *= norm;
            for (auto& g : net.bloomGain) g *= norm;
        }
    }
    staggerSamples_ = std::clamp(s.bloom, 0.0f, 1.0f) * 0.30f * static_cast<float>(fs_);
    {
        // Rising injection gain across the (irregular) stagger offsets, energy-normalised.
        const float b = std::clamp(s.bloom, 0.0f, 1.0f);
        double e = 0.0;
        for (int l = 0; l < kLines; ++l) {
            const float pos = kStaggerPos[static_cast<std::size_t>(l)];
            injGain_[static_cast<std::size_t>(l)] = 1.0f - b + b * (0.15f + 0.85f * pos);
            e += static_cast<double>(injGain_[static_cast<std::size_t>(l)]) * injGain_[static_cast<std::size_t>(l)];
        }
        const float norm = static_cast<float>(std::sqrt(static_cast<double>(kLines) / std::max(e, 1e-9)));
        for (auto& g : injGain_) g *= norm;
    }
    lbLevel_ = (dbToGain(std::clamp(s.lowBoostDb, 0.0f, 18.0f)) - 1.0f) * 0.9f;
    {
        const auto fsf = static_cast<float>(fs_);
        const float pd = std::clamp(s.preDelayMs * 0.001f, 0.0f, static_cast<float>(kMaxPreDelay)) * fsf;
        preDelaySm_.setTarget(pd);
        echoSm_.setTarget(pd + std::clamp(s.echoMs * 0.001f, 0.0f, static_cast<float>(kMaxEcho)) * fsf);
        echoLevelSm_.setTarget(std::clamp(s.echoLevel, 0.0f, 1.0f));
        widthSm_.setTarget(std::clamp(s.width, 0.0f, 1.0f));
        crossfeedSm_.setTarget(std::clamp(s.crossfeed, 0.0f, 1.0f));
        crossDelaySm_.setTarget(0.004f * fsf + std::clamp(s.crossfeedDelayMs, 0.0f, 100.0f) * 0.001f * fsf);
        undSm_.setTarget(std::clamp(s.undulation, 0.0f, 1.0f));
        lineDepthSm_.setTarget(modDepthSamples_ * lineModScale_);
        crossDepthSm_.setTarget(modDepthSamples_ * crossModScale_);
        lbLevelSm_.setTarget(lbLevel_);
        staggerSm_.setTarget(staggerSamples_);
        infAmt_.setTarget(s.infinite ? 1.0f : 0.0f);
    }
}

void LateReverb::updateControl(int samples) noexcept {
    const float rt = std::exp(samples > 0 ? rtLog_.advance(samples) : rtLog_.value());
    const float sizeScale = samples > 0 ? sizeScale_.advance(samples) : sizeScale_.value();
    const float scale = sizeScale;
    effScale_ = scale;
    loopApGain_ = kLoopApGain;
    const float mLow = std::exp(samples > 0 ? bassLog_.advance(samples) : bassLog_.value());
    const float mHigh = std::exp(samples > 0 ? trebleLog_.advance(samples) : trebleLog_.value());
    // Freeze glides the loop gains towards lossless instead of switching.
    const float inf = samples > 0 ? infAmt_.advance(samples) : infAmt_.value();
    // Static tap delays (in-loop allpasses, bloom stagger) glide towards whole-sample
    // targets at control rate; once there, the integer read path is exact and cheap.
    {
        const float sizeTarget = sizeScale_.target();
        const float apCoef = samples > 0 ? static_cast<float>(std::exp(-samples / (0.4 * fs_))) : 0.0f;
        const float stCoef = samples > 0 ? static_cast<float>(std::exp(-samples / (0.05 * fs_))) : 0.0f;
        auto glide = [](float& cur, float target, float coef) {
            cur = target + (cur - target) * coef;
            if (std::abs(cur - target) < 0.01f) cur = target;
            return cur == target;
        };
        for (int n = 0; n < 2; ++n) {
            auto& net = nets_[static_cast<std::size_t>(n)];
            bool apOk = true, stOk = true;
            for (int l = 0; l < kLines; ++l) {
                const auto sl = static_cast<std::size_t>(l);
                apOk &= glide(net.apCur[sl], std::max(2.0f, std::round(net.loopApLen[sl] * sizeTarget)), apCoef);
                apOk &= glide(net.ap2Cur[sl], std::max(2.0f, std::round(net.loopAp2Len[sl] * sizeTarget)), apCoef);
                const auto stagIdx = static_cast<std::size_t>((l + 5 * n) % kLines);
                stOk &= glide(net.stagCur[sl], std::round(staggerSamples_ * kStaggerPos[stagIdx]) + 1.0f, stCoef);
            }
            net.apSettled = apOk;
            net.stagSettled = stOk;
        }
    }
    const std::array<float, 5> key{rt, scale, mLow, mHigh, inf};
    const bool apsSettled = nets_[0].apSettled && nets_[1].apSettled;
    if (!(gainKeyValid_ && apsSettled && key == gainKey_)) {
        gainKey_ = key;
        gainKeyValid_ = apsSettled;
        for (auto& net : nets_) {
            for (int i = 0; i < kLines; ++i) {
                const auto si = static_cast<std::size_t>(i);
                // Every delay element in the loop loses energy in proportion to its
                // length, including the allpasses' internal delays. Then all modes
                // decay at the same rate whatever the allpasses' (frequency-dependent)
                // group delay, instead of modes where the group delay is long ringing
                // on as faint persistent tones.
                const double lineS = net.baseLength[si] * scale / fs_;
                const double apS = net.apCur[si] / fs_, ap2S = net.ap2Cur[si] / fs_;
                const double seconds = lineS + apS + ap2S;
                const float a1 = static_cast<float>(decayGainForDelay(apS, rt));
                const float a2 = static_cast<float>(decayGainForDelay(ap2S, rt));
                const float apComp = 1.0f / std::max(1.0e-6f, a1 * a2);
                // Bass/treble: the band gains cover the whole nominal loop; the allpasses
                // apply the mid-band loss, so the line divides it back out.
                const float gl = static_cast<float>(decayGainForDelay(seconds, rt * mLow)) * apComp;
                // Extra treble loss (relative to mid) reached far above the corner.
                const float extraHi = std::max(-0.8f, (1.0f / mHigh - 1.0f) * kTrebleExtra);
                const float gh = static_cast<float>(decayGainForDelay(seconds, rt / (1.0f + extraHi))) * apComp;
                const float gm = static_cast<float>(decayGainForDelay(lineS, rt));
                net.gLow[si] = gl + inf * (0.99999f - gl);
                net.gMid[si] = gm + inf * (0.99999f - gm);
                net.gHigh[si] = gh + inf * (0.99999f - gh);
                net.apAtt[si] = a1 + inf * (1.0f - a1);
                net.ap2Att[si] = a2 + inf * (1.0f - a2);
            }
        }
    }
    const float rtLb = std::clamp(0.4f * rt, 0.15f, 4.0f);
    for (int j = 0; j < 4; ++j)
        lbGain_[static_cast<std::size_t>(j)] = static_cast<float>(
            decayGainForDelay(lbLength_[static_cast<std::size_t>(j)] / fs_, rtLb));
}

void LateReverb::process(const float* inL, const float* inR, float* outL, float* outR,
                         int numSamples) noexcept {
    constexpr int kControl = 16;
    const auto fsf = static_cast<float>(fs_);
    for (int i = 0; i < numSamples; ++i) {
        const float preDelaySamples = preDelaySm_.next();
        const float echoSamples = echoSm_.next();
        const float echoLevel = echoLevelSm_.next();
        const float width = widthSm_.next();
        const float crossfeed = crossfeedSm_.next();
        const float crossDelay = crossDelaySm_.next();
        const float und = undSm_.next();
        const float lineDepth = lineDepthSm_.next();
        const float crossDepth = crossDepthSm_.next();
        const float lbLevel = lbLevelSm_.next();
        const bool refresh = controlCounter_ <= 0;
        if (refresh) {
            updateControl(kControl);
            controlCounter_ = kControl;
        }
        --controlCounter_;
        const float scale = effScale_;
        const float gate = inputGate_.next();

        preDelay_[0].push(inL[i]);
        preDelay_[1].push(inR[i]);
        float a = preDelay_[0].readFrac(preDelaySamples + 1.0f);
        float b = preDelay_[1].readFrac(preDelaySamples + 1.0f);
        if (echoLevel > 1.0e-6f) {
            a += echoLevel * echoRolloff_[0].process(preDelay_[0].readFrac(echoSamples + 1.0f));
            b += echoLevel * echoRolloff_[1].process(preDelay_[1].readFrac(echoSamples + 1.0f));
        }
        {
            const float m = 0.5f * (a + b), s = 0.5f * (a - b) * width;
            a = (m + s) * gate;
            b = (m - s) * gate;
        }

        // Ease-in chorus: modulation grows once the input has gone quiet.
        float easeMul;
        if (easeIn_) {
            const float env = easeDetector_.process(std::max(std::abs(inL[i]), std::abs(inR[i])));
            silentSeconds_ = env > 1.0e-3f ? 0.0f : silentSeconds_ + 1.0f / fsf;
            ease_.setTarget(std::clamp((silentSeconds_ - 0.05f) / 0.5f, 0.0f, 1.0f));
        } else {
            ease_.setTarget(1.0f);   // glide back to full depth when Ease-in is switched off
        }
        easeMul = ease_.next();

        float own[2], cross[2];
        for (int n = 0; n < 2; ++n) {
            auto& net = nets_[static_cast<std::size_t>(n)];
            float u = n == 0 ? a : b;
            if (bloomActive_) {
                // Bloom: a cloud of irregularly timed taps with rising gain gives a
                // smooth swell; the diffusers below then fill it in.
                net.bloomLine.push(u);
                float acc = net.bloomDirect * u;
                for (int k = 0; k < kBloomTaps; ++k)
                    acc += net.bloomGain[static_cast<std::size_t>(k)]
                         * net.bloomLine.read(static_cast<std::size_t>(net.bloomDelay[static_cast<std::size_t>(k)]));
                u = acc;
            }
            for (int k = 0; k < 6; ++k)
                u = net.inputDiffuser[static_cast<std::size_t>(k)].process(
                    u, kInDiffMs[static_cast<std::size_t>(n)][static_cast<std::size_t>(k)] * 0.001f * fsf,
                    k % 2 ? -kInDiffGain[static_cast<std::size_t>(k)] : kInDiffGain[static_cast<std::size_t>(k)]);
            const float lowNow = net.undLow2.process(net.undLow1.process(u));
            net.stagger.push(u - lowNow);
            net.staggerLow.push(lowNow);

            std::array<float, kLines> v{};
            float ownSum = 0.0f, crossSum = 0.0f;
            const float depth = lineDepth * easeMul;
            if (refresh) {
                // Read positions move slowly (size glide, gentle modulation), so they
                // are updated once per control block.
                const auto& tf = kTapFrac[static_cast<std::size_t>(n)];
                for (int l = 0; l < kLines; ++l) {
                    const auto sl = static_cast<std::size_t>(l);
                    const float len = std::max(2.0f, net.baseLength[sl] * scale);
                    const float d = len + depth * net.lfo[sl].nextBlock(kControl);
                    const float n0 = std::floor(d - 0.5f);
                    const float frac = d - n0;                        // 0.5 .. 1.5
                    net.rdN[sl] = static_cast<int>(n0);
                    net.rdA[sl] = (1.0f - frac) / (1.0f + frac);
                    const float tp = std::max(1.0f, len * tf[sl]);
                    const float tn = std::floor(tp);
                    net.tapN[sl] = static_cast<int>(tn);
                    net.tapF[sl] = tp - tn;
                }
            }
            for (int l = 0; l < kLines; ++l) {
                const auto sl = static_cast<std::size_t>(l);
                // First-order allpass (Thiran) interpolation: flat magnitude, so the
                // fractional read does not add high-frequency loss on every pass
                // (cubic interpolation shortened the decay above ~8 kHz).
                const auto ni = static_cast<std::size_t>(net.rdN[sl]);
                const float ac = net.rdA[sl];
                const float r = ac * net.lines[sl].read(ni) + net.lines[sl].read(ni + 1) - ac * net.fracY[sl];
                net.fracY[sl] = r;
                // Three-band loop gain: mid everywhere, corrected below the bass and
                // above the treble crossover.
                const float lo = net.tiltLo[sl].process(r);
                const float hi = r - net.tiltHi[sl].process(r);
                float f = net.gMid[sl] * r + (net.gLow[sl] - net.gMid[sl]) * lo + (net.gHigh[sl] - net.gMid[sl]) * hi;
                if (net.apSettled) {
                    f = net.loopAp[sl].processIntLossy(f, static_cast<int>(net.apCur[sl]), (l & 1) ? -loopApGain_ : loopApGain_, net.apAtt[sl]);
                    f = net.loopAp2[sl].processIntLossy(f, static_cast<int>(net.ap2Cur[sl]), (l & 1) ? loopApGain_ : -loopApGain_, net.ap2Att[sl]);
                } else {
                    f = net.loopAp[sl].processLossy(f, net.apCur[sl], (l & 1) ? -loopApGain_ : loopApGain_, net.apAtt[sl]);
                    f = net.loopAp2[sl].processLossy(f, net.ap2Cur[sl], (l & 1) ? loopApGain_ : -loopApGain_, net.ap2Att[sl]);
                }
                v[sl] = f;
                const auto tn = static_cast<std::size_t>(net.tapN[sl]);
                const float t0 = net.lines[sl].read(tn);
                const float t = t0 + net.tapF[sl] * (net.lines[sl].read(tn + 1) - t0);
                ownSum += net.ownTap[sl] * t;
                crossSum += net.crossTap[sl] * t;
            }
            hadamard(v);
            for (int l = 0; l < kLines; ++l) {
                const auto sl = static_cast<std::size_t>(l);
                const auto stagIdx = static_cast<std::size_t>((l + 5 * n) % kLines);
                float high, low;
                if (net.stagSettled) {
                    const auto d = static_cast<std::size_t>(net.stagCur[sl]);
                    high = net.stagger.read(d);
                    low = net.staggerLow.read(d);
                } else {
                    high = net.stagger.readFrac(net.stagCur[sl]);
                    low = net.staggerLow.readFrac(net.stagCur[sl]);
                }
                // Undulation concentrates the low band in two lines so fewer
                // low-frequency modes are excited, sparsening the VLF field.
                const float lowWeight = l < 2 ? 1.0f + und * (4.0f - 1.0f) : 1.0f - und;
                const float inj = net.inSign[sl] * injGain_[stagIdx] * (high + low * lowWeight) * kInjectionScale;
                net.lines[sl].push(flushDenormal(v[sl] + inj));
            }
            for (auto& t : net.tiltLo) t.flush();
            for (auto& t : net.tiltHi) t.flush();
            own[n] = ownSum * kOutputScale;
            net.crossVibrato.push(crossSum * kOutputScale);
            const float cd = crossDelay + crossDepth * easeMul * net.crossLfo.next();
            cross[n] = net.crossRolloff.process(net.crossVibrato.readFrac(std::max(1.0f, cd))) * crossfeed;
        }

        float yl = own[0] + cross[1];
        float yr = own[1] + cross[0];

        // The low-boost network runs whenever it is (or is fading) audible and keeps
        // running for its own decay, so switching it never cuts a tail.
        const bool lbRun = lbLevel > 1.0e-5f || lbLevel_ > 0.0f;
        if (lbRun && !lbRunning_) { for (auto& l : lbLines_) l.reset(); lbLp1_.reset(); lbLp2_.reset(); }
        lbRunning_ = lbRun;
        if (lbRun) {
            const float lbIn = lbLp2_.process(lbLp1_.process(0.5f * (a + b)));
            std::array<float, 4> r{};
            for (int j = 0; j < 4; ++j)
                r[static_cast<std::size_t>(j)] = lbLines_[static_cast<std::size_t>(j)].read(
                    static_cast<std::size_t>(lbLength_[static_cast<std::size_t>(j)]));
            std::array<float, 4> v{};
            for (int j = 0; j < 4; ++j) v[static_cast<std::size_t>(j)] = r[static_cast<std::size_t>(j)] * lbGain_[static_cast<std::size_t>(j)];
            hadamard(v);
            static constexpr float sgn[4] = {1.0f, -1.0f, 1.0f, -1.0f};
            for (int j = 0; j < 4; ++j)
                lbLines_[static_cast<std::size_t>(j)].push(flushDenormal(v[static_cast<std::size_t>(j)] + lbIn * sgn[j]));
            yl += lbLevel * 0.5f * (r[0] + r[1] - r[2] - r[3]);
            yr += lbLevel * 0.5f * (r[0] - r[1] + r[2] - r[3]);
        }

        const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * width;
        outL[i] = outRolloff_[0].process(m + s);
        outR[i] = outRolloff_[1].process(m - s);
    }
}

} // namespace rr
