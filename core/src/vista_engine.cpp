#include "rr/vista_engine.h"

#include <algorithm>
#include <cmath>

namespace rr {
namespace {
// Level calibration so reflections and reverb sit at similar loudness at the
// neutral 50% mix (measured with tools/render on pink noise bursts).
constexpr float kErTrim = 0.85f;
constexpr float kLateTrim = 1.0f;

float peakOf(const float* a, const float* b, int n) noexcept {
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = std::max({p, std::abs(a[i]), std::abs(b[i])});
    return p;
}
} // namespace

void VistaEngine::prepare(double sampleRate, int maxBlockSize) {
    fs_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    for (auto* v : {&dryL_, &dryR_, &erL_, &erR_, &lateL_, &lateR_, &duckWet_, &compWeight_})
        v->assign(static_cast<std::size_t>(maxBlock_), 0.0f);
    er_.prepare(fs_, maxBlock_);
    late_.prepare(fs_, maxBlock_);
    eq_.prepare(fs_);
    dynamics_.prepare(fs_);
    for (auto* s : {&erGain_, &lateGain_, &dryGain_, &wetGain_, &outGain_, &duckRevW_, &duckWetW_, &compW_}) s->prepare(fs_, 20.0);
    setSettings(settings_);
    for (auto* s : {&duckRevW_, &duckWetW_, &compW_}) s->snap(s->target());
    erGain_.snap(erGain_.target());
    lateGain_.snap(lateGain_.target());
    dryGain_.snap(dryGain_.target());
    wetGain_.snap(wetGain_.target());
    outGain_.snap(outGain_.target());
}

void VistaEngine::reset() noexcept {
    quietSamples_ = 0;
    er_.reset();
    late_.reset();
    eq_.reset();
    dynamics_.reset();
}

void VistaEngine::setSettings(const VistaSettings& s) noexcept {
    settings_ = s;
    er_.setSettings(s.er);
    late_.setSettings(s.late);
    eq_.setEnabled(s.eqOn);
    for (std::size_t b = 0; b < 5; ++b) eq_.setBand(b, s.eq[b]);
    dynamics_.setSettings(s.dynamics);
    duckRevW_.setTarget(s.dynamics.mode == DynamicsMode::DuckReverb ? 1.0f : 0.0f);
    duckWetW_.setTarget(s.dynamics.mode == DynamicsMode::DuckWet ? 1.0f : 0.0f);
    compW_.setTarget(s.dynamics.mode == DynamicsMode::CompressWet ? 1.0f : 0.0f);
    const float m = std::clamp(s.reflReverbMix, 0.0f, 1.0f);
    erGain_.setTarget(std::min(1.0f, 2.0f * (1.0f - m)) * kErTrim);
    lateGain_.setTarget(std::min(1.0f, 2.0f * m) * kLateTrim);
    const float mix = std::clamp(s.mix, 0.0f, 1.0f);
    dryGain_.setTarget(std::cos(mix * 0.5f * static_cast<float>(kPi)));
    wetGain_.setTarget(std::sin(mix * 0.5f * static_cast<float>(kPi)));
    outGain_.setTarget(dbToGain(s.gainDb));
}

void VistaEngine::process(float* left, float* right, int numSamples) noexcept {
    int pos = 0;
    float mIn = 0, mRefl = 0, mRev = 0, mOut = 0;
    while (pos < numSamples) {
        const int n = std::min(maxBlock_, numSamples - pos);
        float* L = left + pos;
        float* R = right + pos;
        std::copy(L, L + n, dryL_.begin());
        std::copy(R, R + n, dryR_.begin());
        mIn = std::max(mIn, peakOf(L, R, n));

        // Once the input has been silent and the wet output has decayed below
        // -160 dBFS for half a second, the reverb engines are skipped (their
        // output would be inaudible) until signal returns. Never while frozen.
        const float inPeak = peakOf(L, R, n);
        const bool idle = !settings_.late.infinite && inPeak < 1.0e-8f && quietSamples_ > static_cast<int>(0.5 * fs_);
        if (idle) {
            std::fill(erL_.begin(), erL_.begin() + n, 0.0f);
            std::fill(erR_.begin(), erR_.begin() + n, 0.0f);
            std::fill(lateL_.begin(), lateL_.begin() + n, 0.0f);
            std::fill(lateR_.begin(), lateR_.begin() + n, 0.0f);
        } else {
            er_.process(L, R, erL_.data(), erR_.data(), n);
            late_.process(L, R, lateL_.data(), lateR_.data(), n);
            const float wetPeak = std::max(peakOf(erL_.data(), erR_.data(), n), peakOf(lateL_.data(), lateR_.data(), n));
            quietSamples_ = (inPeak < 1.0e-8f && wetPeak < 1.0e-8f) ? quietSamples_ + n : 0;
        }
        if (inPeak >= 1.0e-8f) quietSamples_ = 0;

        for (int i = 0; i < n; ++i) {
            const float key = std::max(std::abs(dryL_[static_cast<std::size_t>(i)]),
                                       std::abs(dryR_[static_cast<std::size_t>(i)]));
            // Mode changes crossfade (~20 ms) between where the dynamics apply.
            const float wDuckRev = duckRevW_.next(), wDuckWet = duckWetW_.next(), wComp = compW_.next();
            const float duck = dynamics_.duckGain(key);
            duckWet_[static_cast<std::size_t>(i)] = 1.0f + wDuckWet * (duck - 1.0f);
            compWeight_[static_cast<std::size_t>(i)] = wComp;
            float lateMul = dynamics_.gateGain(key) * (1.0f + wDuckRev * (duck - 1.0f));
            const float eg = erGain_.next(), lg = lateGain_.next() * lateMul;
            erL_[static_cast<std::size_t>(i)] *= eg;
            erR_[static_cast<std::size_t>(i)] *= eg;
            lateL_[static_cast<std::size_t>(i)] *= lg;
            lateR_[static_cast<std::size_t>(i)] *= lg;
        }
        mRefl = std::max(mRefl, peakOf(erL_.data(), erR_.data(), n));
        mRev = std::max(mRev, peakOf(lateL_.data(), lateR_.data(), n));

        for (int i = 0; i < n; ++i) {
            L[i] = erL_[static_cast<std::size_t>(i)] + lateL_[static_cast<std::size_t>(i)];
            R[i] = erR_[static_cast<std::size_t>(i)] + lateR_[static_cast<std::size_t>(i)];
        }
        eq_.process(L, R, n);

        for (int i = 0; i < n; ++i) {
            const float comp = dynamics_.compGain(std::max(std::abs(L[i]), std::abs(R[i])));
            const float wc = compWeight_[static_cast<std::size_t>(i)];
            const float wetMul = duckWet_[static_cast<std::size_t>(i)] * (1.0f + wc * (comp - 1.0f));
            const float wg = wetGain_.next() * wetMul, dg = dryGain_.next(), og = outGain_.next();
            L[i] = (dryL_[static_cast<std::size_t>(i)] * dg + L[i] * wg) * og;
            R[i] = (dryR_[static_cast<std::size_t>(i)] * dg + R[i] * wg) * og;
            if (!std::isfinite(L[i])) L[i] = 0.0f;
            if (!std::isfinite(R[i])) R[i] = 0.0f;
        }
        mOut = std::max(mOut, peakOf(L, R, n));
        pos += n;
    }
    meterIn_ = mIn; meterRefl_ = mRefl; meterReverb_ = mRev; meterOut_ = mOut;
}

} // namespace rr
