#include "rr/septima_engine.h"

#include <algorithm>
#include <cmath>

namespace rr {

void SeptimaEngine::prepare(double sampleRate, int maxBlockSize) {
    fs_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    synthStep_ = static_cast<float>(1.0 / std::max(1.0, 0.03 * fs_));
    for (auto& d : feed_) d.prepare(static_cast<std::size_t>((kMaxPreDelay + kMaxDelay + 0.01) * fs_));
    for (auto& d : modLine_) d.prepare(static_cast<std::size_t>(0.006 * fs_) + 8);
    for (auto& d : earlyAlign_) d.prepare(static_cast<std::size_t>(0.003 * fs_) + 8);
    modLfo_[0].prepare(fs_, 0.41f, 9001u);
    modLfo_[1].prepare(fs_, 0.53f, 9002u);
    preDelay_.prepare(fs_, 60.0);
    delayTime_.prepare(fs_, 60.0);
    for (auto* g : {&delayLevel_, &earlyGain_, &lateGain_, &vlfGain_, &dryGain_, &wetGain_, &outGain_})
        g->prepare(fs_, 20.0);
    synthEr_.prepare(fs_, maxBlock_);
    eq_.prepare(fs_);
    for (auto* v : {&erL_, &erR_, &wetL_, &wetR_}) v->assign(static_cast<std::size_t>(maxBlock_), 0.0f);
    setSettings(s_);
    for (auto* g : {&preDelay_, &delayTime_, &delayLevel_, &earlyGain_, &lateGain_, &vlfGain_,
                    &dryGain_, &wetGain_, &outGain_})
        g->snap(g->target());
    reset();
}

void SeptimaEngine::reset() noexcept {
    synthMix_ = s_.erSet > 0 ? 1.0f : 0.0f;
    synthRunning_ = false; synthWarmup_ = 0;
    sampledActive_ = true; sampledWarmup_ = 0;
    for (auto& d : feed_) d.reset();
    for (auto& d : modLine_) d.reset();
    for (auto& d : earlyAlign_) d.reset();
    synthEr_.reset();
    eq_.reset();
    for (auto* arr : {&earlyRoll_, &lateRoll_, &vlfRoll_}) for (auto& f : *arr) f.reset();
}

void SeptimaEngine::setSettings(const SeptimaSettings& s) noexcept {
    s_ = s;
    preDelay_.setTarget(std::clamp(s.preDelayMs, 0.0f, 500.0f) * 0.001f * static_cast<float>(fs_));
    delayTime_.setTarget(std::clamp(s.delayMs, 0.0f, 1000.0f) * 0.001f * static_cast<float>(fs_));
    delayLevel_.setTarget(std::clamp(s.delayLevel, 0.0f, 1.0f));
    if (s.erSet > 0) {
        auto er = erSetSettings(s.erSet - 1);
        er.modDepth = 0.1f + 0.5f * std::clamp(s.modulation, 0.0f, 1.0f);
        er.chorus = ChorusMode::Fine;
        synthEr_.setSettings(er);
    }
    for (auto& f : earlyRoll_) f.setup(fs_, s.earlyRolloffHz, 12);
    for (auto& f : lateRoll_) f.setup(fs_, s.lateRolloffHz, 12);
    for (auto& f : vlfRoll_) f.setup(fs_, std::max(s.lateRolloffHz, 400.0f), 12);
    eq_.setEnabled(s.eqOn);
    for (std::size_t b = 0; b < 4; ++b) eq_.setBand(b, s.eq[b]);
    const float m = std::clamp(s.earlyLate, 0.0f, 1.0f);
    earlyGain_.setTarget(std::min(1.0f, 2.0f * (1.0f - m)));
    lateGain_.setTarget(std::min(1.0f, 2.0f * m));
    vlfGain_.setTarget(std::clamp(s.vlfLevel, 0.0f, 2.0f));
    const float mix = std::clamp(s.mix, 0.0f, 1.0f);
    dryGain_.setTarget(std::cos(mix * 0.5f * static_cast<float>(kPi)));
    wetGain_.setTarget(std::sin(mix * 0.5f * static_cast<float>(kPi)));
    outGain_.setTarget(dbToGain(s.gainDb));
}

void SeptimaEngine::makeFeeds(const float* inL, const float* inR, float* lateL, float* lateR,
                              float* vlfMono, int n) noexcept {
    for (int i = 0; i < n; ++i) {
        feed_[0].push(inL[i]);
        feed_[1].push(inR[i]);
        const float pd = preDelay_.next();
        const float dt = pd + delayTime_.next();
        const float dl = delayLevel_.next();
        float l = pd < 0.5f ? inL[i] : feed_[0].readFrac(pd);
        float r = pd < 0.5f ? inR[i] : feed_[1].readFrac(pd);
        if (dl > 0.0f) {
            l += dl * feed_[0].readFrac(std::max(1.0f, dt));
            r += dl * feed_[1].readFrac(std::max(1.0f, dt));
        }
        lateL[i] = l;
        lateR[i] = r;
        vlfMono[i] = 0.5f * (l + r);
    }
    float p = 0.0f;
    for (int i = 0; i < n; ++i) p = std::max({p, std::abs(inL[i]), std::abs(inR[i])});
    mIn_ = p;
}

void SeptimaEngine::finish(float* left, float* right, const float* earlyL, const float* earlyR,
                           const float* lateL, const float* lateR, const float* vlfL,
                           const float* vlfR, int numSamples) noexcept {
    const float depth = std::clamp(s_.modulation, 0.0f, 1.0f) * 0.0005f * static_cast<float>(fs_);
    // The tail's modulation delay is centred on a fixed 2 ms; the early path is
    // delayed by exactly the same amount so the early/late crossover stays aligned.
    const auto baseSamples = static_cast<std::size_t>(std::lround(0.002 * fs_));
    const float base = static_cast<float>(baseSamples);
    float pe = 0, pl = 0, pv = 0, po = 0;
    int pos = 0;
    while (pos < numSamples) {
        const int n = std::min(maxBlock_, numSamples - pos);
        float* L = left + pos;
        float* R = right + pos;
        // Switching between the sampled early IR and a synthetic ER set
        // crossfades (~30 ms); the synthetic engine starts clean when it fades in.
        // When it starts from cold, the synthetic engine runs silently until its
        // delay lines hold real signal (otherwise a sustained input would "start"
        // abruptly at every tap), then fades in.
        const float synthTarget = s_.erSet > 0 ? 1.0f : 0.0f;
        if (synthTarget > 0.0f && synthMix_ >= 1.0f && synthWarmup_ <= 0) {
            sampledActive_ = false;                       // the host may stop the early convolution
        } else if (synthTarget <= 0.0f && !sampledActive_) {
            sampledActive_ = true;                        // restart it, hold the synthetic set meanwhile
            sampledWarmup_ = static_cast<int>(0.16 * fs_);
        }
        const bool synthRun = synthMix_ > 0.0f || synthTarget > 0.0f;
        if (synthRun) {
            if (!synthRunning_) {
                synthEr_.reset();
                synthRunning_ = true;
                synthWarmup_ = static_cast<int>((std::min(EarlyReflections::kMaxErSeconds, synthEr_.longestTapSeconds()) + 0.03) * fs_);
            }
            synthEr_.process(L, R, erL_.data(), erR_.data(), n);
        } else {
            synthRunning_ = false;
        }
        for (int i = 0; i < n; ++i) {
            const auto si = static_cast<std::size_t>(i);
            const int j = pos + i;
            float eL = earlyL[j], eR = earlyR[j];
            if (synthRun) {
                if (synthWarmup_ > 0 && synthTarget > 0.0f) --synthWarmup_;
                else if (sampledWarmup_ > 0 && synthTarget <= 0.0f) --sampledWarmup_;
                else if (synthMix_ != synthTarget)
                    synthMix_ = synthTarget > synthMix_ ? std::min(1.0f, synthMix_ + synthStep_) : std::max(0.0f, synthMix_ - synthStep_);
                const float g = s_.syntheticErGain;
                eL += synthMix_ * (erL_[si] * g - eL);
                eR += synthMix_ * (erR_[si] * g - eR);
            }
            eL = earlyRoll_[0].process(eL);
            eR = earlyRoll_[1].process(eR);
            earlyAlign_[0].push(eL);
            earlyAlign_[1].push(eR);
            eL = earlyAlign_[0].read(baseSamples);
            eR = earlyAlign_[1].read(baseSamples);
            const float vg = vlfGain_.next();
            const float vL = vlfRoll_[0].process(vlfL[j]) * vg;
            const float vR = vlfRoll_[1].process(vlfR[j]) * vg;
            float tL = lateRoll_[0].process(lateL[j]) + vL;
            float tR = lateRoll_[1].process(lateR[j]) + vR;
            // Version-2 style tail modulation: slow random vibrato on the tail.
            modLine_[0].push(tL);
            modLine_[1].push(tR);
            tL = modLine_[0].readFrac(base + depth * modLfo_[0].next());
            tR = modLine_[1].readFrac(base + depth * modLfo_[1].next());
            const float eg = earlyGain_.next(), lg = lateGain_.next();
            eL *= eg; eR *= eg; tL *= lg; tR *= lg;
            pe = std::max({pe, std::abs(eL), std::abs(eR)});
            pl = std::max({pl, std::abs(tL), std::abs(tR)});
            pv = std::max({pv, std::abs(vL), std::abs(vR)});
            wetL_[si] = eL + tL;
            wetR_[si] = eR + tR;
        }
        eq_.process(wetL_.data(), wetR_.data(), n);
        for (int i = 0; i < n; ++i) {
            const auto si = static_cast<std::size_t>(i);
            const float dg = dryGain_.next(), wg = wetGain_.next(), og = outGain_.next();
            L[i] = (L[i] * dg + wetL_[si] * wg) * og;
            R[i] = (R[i] * dg + wetR_[si] * wg) * og;
            if (!std::isfinite(L[i])) L[i] = 0.0f;
            if (!std::isfinite(R[i])) R[i] = 0.0f;
            po = std::max({po, std::abs(L[i]), std::abs(R[i])});
        }
        pos += n;
    }
    mEarly_ = pe; mLate_ = pl; mVlf_ = pv; mOut_ = po;
}

} // namespace rr
