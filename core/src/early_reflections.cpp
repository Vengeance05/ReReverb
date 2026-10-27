#include "rr/early_reflections.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace rr {

// Output diffuser lengths relative to the base table. Short diffusers keep each
// reflection cluster's onset crisp (long ones smeared the start of every hit
// into a delayed "whoosh"); the dense pre-diffusion still fills them in.
static constexpr float kDiffuserLength = 0.4f;
// Fixed extra delay on all reflections.
static constexpr double kErOffsetSeconds = 0.005;
namespace {

struct Shape {
    float L, W, H;        // metres at size scale 1.0
    float hardness;       // multiplies reflection coefficient
    bool physical;
};

constexpr std::array<Shape, static_cast<int>(ErPattern::Count)> kShapes{{
    {9.0f, 7.0f, 3.2f, 1.00f, true},     // Room
    {6.0f, 5.0f, 3.0f, 0.80f, true},     // Studio
    {7.0f, 4.5f, 5.5f, 1.10f, true},     // Chamber
    {32.0f, 22.0f, 14.0f, 1.00f, true},  // Hall
    {24.0f, 30.0f, 10.0f, 0.95f, true},  // Stage
    {55.0f, 22.0f, 28.0f, 1.08f, true},  // Cathedral
    {20.0f, 20.0f, 10.0f, 1.00f, false}, // Scattered
    {20.0f, 20.0f, 10.0f, 1.00f, false}, // Nonlin U
}};

constexpr float kSpeedOfSound = 343.0f;

float sizeScale(float size) noexcept { return 0.25f * std::pow(12.0f, std::clamp(size, 0.0f, 1.0f)); }

struct Candidate { float time; float gain; int order; };

} // namespace

const char* erPatternName(ErPattern p) noexcept {
    switch (p) {
        case ErPattern::Room: return "Room";
        case ErPattern::Studio: return "Studio";
        case ErPattern::Chamber: return "Chamber";
        case ErPattern::Hall: return "Hall";
        case ErPattern::Stage: return "Stage";
        case ErPattern::Cathedral: return "Cathedral";
        case ErPattern::Scattered: return "Scattered";
        case ErPattern::NonlinU: return "Nonlin U";
        default: return "?";
    }
}

ErSettings erSetSettings(int setIndex) noexcept {
    const int idx = std::clamp(setIndex, 0, 31);
    const float t = static_cast<float>(idx) / 31.0f;
    ErSettings s;
    // Cycle through physical shapes with a growing size so neighbouring sets
    // differ in character as well as scale.
    static constexpr ErPattern cycle[] = {ErPattern::Studio, ErPattern::Room, ErPattern::Chamber,
                                          ErPattern::Room, ErPattern::Hall, ErPattern::Stage,
                                          ErPattern::Hall, ErPattern::Cathedral};
    s.pattern = idx < 6 ? (idx % 2 ? ErPattern::Room : ErPattern::Studio)
                        : cycle[idx % 8];
    // Normalise the very different base dimensions to a monotonic overall size.
    const float targetLength = 4.0f * std::pow(16.0f, t); // 4 m .. 64 m
    const float base = kShapes[static_cast<int>(s.pattern)].L;
    const float scale = targetLength / base;           // scale = 0.25*12^size
    s.size = std::clamp(std::log(scale / 0.25f) / std::log(12.0f), 0.0f, 1.0f);
    s.proximity = 0.35f + 0.25f * t;
    s.reflectivity = 0.55f + 0.2f * std::sin(static_cast<float>(idx) * 1.7f) * 0.5f + 0.1f * t;
    s.diffusion = 0.35f + 0.4f * t;
    s.crossfeed = 0.65f;
    s.modDepth = 0.15f;
    s.seed = static_cast<std::uint32_t>(idx * 7919 + 17);
    return s;
}

void EarlyReflections::prepare(double sampleRate, int /*maxBlockSize*/) {
    fs_ = sampleRate;
    const auto maxDelay = static_cast<std::size_t>((kMaxErSeconds + 0.12) * fs_) + 8;
    for (auto& line : input_) line.prepare(maxDelay);
    const auto apMax = static_cast<std::size_t>(0.04 * fs_) + 64;
    for (auto& ch : diffusers_) for (auto& ap : ch) ap.prepare(apMax);
    for (auto& ch : preDiff_) for (auto& ap : ch) ap.prepare(static_cast<std::size_t>(0.02 * fs_) + 8);
    for (auto& o : scatter_) for (auto& k : o) for (auto& ap : k) ap.prepare(static_cast<std::size_t>(0.012 * fs_) + 8);
    for (int o = 0; o < 2; ++o) {
        for (int k = 0; k < 4; ++k)
            diffuserLfo_[o][k].prepare(fs_, 0.35f + 0.13f * static_cast<float>(k + 3 * o),
                                       static_cast<std::uint32_t>(101 + 31 * k + 977 * o));
        for (int c = 0; c < 2; ++c) {
            vibrato_[o][c].prepare(static_cast<std::size_t>(0.012 * fs_) + 8);
            vibratoLfo_[o][c].prepare(fs_, 0.6f + 0.21f * static_cast<float>(c + 2 * o),
                                      static_cast<std::uint32_t>(4242 + 77 * c + 313 * o));
        }
    }
    fadeLength_ = std::max(1, static_cast<int>(0.03 * fs_));
    generate(settings_, sets_[0]);
    sets_[1] = sets_[0];
    active_ = 0;
    fadeRemaining_ = 0;
    absorbPrimed_ = false;
    applyDerived(settings_);
    absorbPrimed_ = true;
    smoothCoef_ = static_cast<float>(std::exp(-1.0 / (0.025 * fs_)));
    snapSmoothed();
    reset();
}

void EarlyReflections::reset() noexcept {
    for (auto& l : input_) l.reset();
    for (auto& o : absorb_) for (auto& k : o) for (auto& f : k) f.reset();
    for (auto& f : crossRolloff_) f.reset();
    for (auto& o : diffusers_) for (auto& ap : o) ap.reset();
    for (auto& o : preDiff_) for (auto& ap : o) ap.reset();
    for (auto& o : scatter_) for (auto& k : o) for (auto& ap : k) ap.reset();
    for (auto& o : vibrato_) for (auto& v : o) v.reset();
    for (auto& f : rolloff_) f.reset();
    // A reset starts from silence, so jump straight to the latest settings.
    if (pendingDirty_) {
        generate(pendingSettings_, sets_[static_cast<std::size_t>(active_)]);
        pendingDirty_ = false;
    }
    fadeRemaining_ = 0;
    snapSmoothed();
    if (absorbPrimed_) setAbsorb(absorbTargetLog_);
}

double EarlyReflections::longestTapSeconds() const noexcept {
    int longest = 0;
    for (const auto& row : sets_[static_cast<std::size_t>(active_)].lists)
        for (const auto& list : row)
            for (int t = 0; t < list.count; ++t) longest = std::max(longest, list.taps[static_cast<std::size_t>(t)].delay);
    return longest / fs_;
}

void EarlyReflections::previewTaps(const ErSettings& s, double sampleRate, std::vector<std::pair<float, float>>& out) {
    auto er = std::make_unique<EarlyReflections>();
    er->fs_ = sampleRate;
    auto set = std::make_unique<TapSet>();
    er->generate(s, *set);
    out.clear();
    for (const auto& in : set->lists)
        for (const auto& list : in)
            for (int i = 0; i < list.count; ++i)
                out.emplace_back(static_cast<float>(list.taps[i].delay / sampleRate), list.taps[i].gain);
}

int EarlyReflections::tapCount(int input, int output, bool /*cross*/) const noexcept {
    return sets_[active_].lists[input][output].count;
}

void EarlyReflections::setSettings(const ErSettings& s) noexcept {
    const bool geometryChanged = s.pattern != settings_.pattern || s.size != settings_.size
        || s.proximity != settings_.proximity || s.reflectivity != settings_.reflectivity
        || s.crossfeed != settings_.crossfeed || s.crossfeedDelayMs != settings_.crossfeedDelayMs
        || s.seed != settings_.seed;
    if (geometryChanged) {
        pendingSettings_ = s;
        pendingDirty_ = true;
    }
    settings_ = s;
    applyDerived(s);
}

void EarlyReflections::setAbsorb(float logHz) noexcept {
    absorbCurLog_ = logHz;
    const double hz = std::exp(static_cast<double>(logHz));
    for (auto& o : absorb_) for (auto& k : o) for (auto& f : k) f.setCutoff(fs_, hz);
}

void EarlyReflections::applyDerived(const ErSettings& s) noexcept {
    const auto& shape = kShapes[static_cast<int>(s.pattern)];
    // Only a mild high-frequency loss per reflection order: measured reference
    // reflections are as bright as their roll-off filter alone allows (the
    // roll-off sets the tone; reflectivity shapes the envelope and level).
    const double absorbHz = (13000.0 + 9000.0 * static_cast<double>(s.reflectivity))
                          * std::sqrt(static_cast<double>(shape.hardness));
    // The absorption cutoff glides (per control block) so pattern/reflectivity
    // changes don't step the filter coefficients mid-signal.
    absorbTargetLog_ = static_cast<float>(std::log(absorbHz));
    if (!absorbPrimed_) setAbsorb(absorbTargetLog_);
    for (auto& f : crossRolloff_) f.setup(fs_, s.crossfeedRolloffHz, 12);
    for (auto& f : rolloff_) { f.setShape(RollOff::Shape::Cascade); f.setup(fs_, s.rolloffHz, s.rolloffSlope); }

    // Output diffusers (halved from the original lengths: with the dense pre-diffusion
    // in front, shorter diffusers keep the early energy front-loaded like real rooms).
    static constexpr float baseMs[2][4] = {{4.3f, 6.7f, 9.1f, 12.9f}, {4.9f, 7.3f, 10.3f, 13.7f}};
    const float scale = 0.6f + 0.9f * s.size;
    // Very small spaces keep the full diffuser lengths (shorter ones would comb
    // at audible frequencies); from medium rooms up they are halved.
    const float lengthScale = 1.0f - 0.5f * std::clamp((s.size - 0.15f) / 0.2f, 0.0f, 1.0f);
    for (int o = 0; o < 2; ++o)
        for (int k = 0; k < 4; ++k)
            diffuserDelay_[o][k] = kDiffuserLength * baseMs[o][k] * scale * lengthScale * 0.001f * static_cast<float>(fs_);
    // Small rooms use gentler diffusers (their ringing colours a short decay);
    // larger spaces need the full density.
    {
        const float x = std::clamp((s.size - 0.3f) / 0.2f, 0.0f, 1.0f);
        diffusionGain_ = (0.6f + 0.18f * x * x * (3.0f - 2.0f * x)) * std::clamp(s.diffusion, 0.0f, 1.0f);
    }
    static constexpr float scatterMs[4] = {1.31f, 2.17f, 2.93f, 4.07f};
    for (int o = 0; o < 2; ++o)
        for (int kind = 0; kind < 2; ++kind)
            for (int k = 0; k < 4; ++k)
                scatterDelay_[o][kind][k] = std::max(2.0f, std::round(scatterMs[k] * (1.0f + 0.11f * static_cast<float>(o) + 0.23f * static_cast<float>(kind))
                                          * scale * 0.001f * static_cast<float>(fs_)));
    // Modulation is kept subtle: a real room is time-invariant, and measured
    // reference renders stay close to their own impulse response. Strong
    // modulation made the reverb sound like an effect layered on top.
    constexpr float kEM = 0.1f;
    diffuserModSamples_ = kEM * s.modDepth * 0.00045f * static_cast<float>(fs_);

    float own = 0.0f, cross = 0.0f;
    switch (s.chorus) {
        case ChorusMode::Fine: own = 0.3f; cross = 0.3f; break;
        case ChorusMode::Light: own = 0.0f; cross = 1.0f; break;
        case ChorusMode::Moderate:
        case ChorusMode::EaseIn: own = 1.0f; cross = 1.0f; break;
        case ChorusMode::Off: default: break;
    }
    const float depth = kEM * s.modDepth * 0.0012f * static_cast<float>(fs_);
    vibratoDepth_ = {own * depth, cross * depth};
    preGain_ = std::clamp(0.42f + 0.36f * s.diffusion, 0.0f, 0.72f);
    derivedSettled_ = false;
    scatterSettled_ = false;
    controlCountdown_ = 0;
}

void EarlyReflections::generate(const ErSettings& s, TapSet& out) const noexcept {
    if (kShapes[static_cast<int>(s.pattern)].physical) generatePhysical(s, out);
    else generateProcedural(s, out);

    // Proximity also sets how the reflections build: a near source gets its
    // strongest reflections straight away, a distant one hears them swell in
    // over tens of milliseconds (measured reference: far presets peak 50-80 ms
    // in, with the first reflections ~10 dB down). Without this, drums in big
    // spaces get a hard slap right after the hit.
    {
        const float p = std::clamp(s.proximity, 0.0f, 1.0f);
        const float tPeak = 0.11f * p * p * (0.7f + 0.6f * std::clamp(s.size, 0.0f, 1.0f));
        if (tPeak > 0.001f) {
            const float peakSamples = tPeak * static_cast<float>(fs_);
            for (auto& row : out.lists)
                for (auto& list : row)
                    for (int i = 0; i < list.count; ++i) {
                        const float x = static_cast<float>(list.taps[i].delay) / peakSamples;
                        if (x >= 1.0f) continue;
                        const float sm = x * x * (3.0f - 2.0f * x);
                        list.taps[i].gain *= 0.3f + 0.7f * sm;
                    }
        }
        // Reference reflections never start before ~9 ms (ours began at ~4 ms,
        // which sounds close and slightly unnatural).
        const int offset = static_cast<int>(kErOffsetSeconds * fs_);
        for (auto& row : out.lists)
            for (auto& list : row)
                for (int i = 0; i < list.count; ++i) list.taps[i].delay += offset;
    }

    // Energy normalisation per input so that size/proximity change timing and
    // density rather than level. Reflectivity still sets overall energy.
    const float targetEnergy = 0.25f + 0.75f * std::clamp(s.reflectivity, 0.0f, 1.0f);
    for (int in = 0; in < 2; ++in) {
        float energy = 0.0f;
        for (int o = 0; o < 2; ++o) {
            const auto& list = out.lists[in][o];
            for (int i = 0; i < list.count; ++i) energy += list.taps[i].gain * list.taps[i].gain;
        }
        const float norm = energy > 1.0e-9f ? std::sqrt(targetEnergy / energy) : 0.0f;
        for (int o = 0; o < 2; ++o) {
            auto& list = out.lists[in][o];
            const bool cross = in != o;
            const float g = norm * (cross ? std::clamp(s.crossfeed, 0.0f, 1.0f) * 1.4142f : 1.0f);
            const int extra = cross ? static_cast<int>(s.crossfeedDelayMs * 0.001f * fs_) : 0;
            for (int i = 0; i < list.count; ++i) {
                list.taps[i].gain *= g;
                list.taps[i].delay += extra;
            }
        }
    }
}

static constexpr float kLatticeJitter = 0.035f;

void EarlyReflections::generatePhysical(const ErSettings& s, TapSet& out) const noexcept {
    const auto& shape = kShapes[static_cast<int>(s.pattern)];
    const float scale = sizeScale(s.size);
    const float L = shape.L * scale, W = shape.W * scale, H = shape.H * scale;
    const float R = std::clamp((0.30f + 0.62f * s.reflectivity) * shape.hardness, 0.05f, 0.97f);

    const float srcX = std::max(0.8f, 0.2f * L);
    const float srcZ = std::min(1.6f, 0.4f * H);
    const float lisZ = std::min(1.5f, 0.45f * H);
    const float dist = std::clamp(std::min(1.0f, 0.1f * L) + (0.65f * L - std::min(1.0f, 0.1f * L)) * s.proximity,
                                  0.5f, L - srcX - 0.3f);
    const float lisX = srcX + dist;
    // Spaced pickup positions (like a spaced omni pair) keep the low end decorrelated.
    const float earOffset = 0.6f * std::clamp(scale, 1.0f, 3.0f);
    const float maxTime = static_cast<float>(kMaxErSeconds) - 0.11f;

    std::array<Candidate, 729> cands{};
    for (int in = 0; in < 2; ++in) {
        const float srcY = 0.5f * W + (in == 0 ? -0.15f : 0.15f) * W;
        for (int o = 0; o < 2; ++o) {
            const float earY = 0.5f * W + (o == 0 ? -earOffset : earOffset);
            const float dx0 = lisX - srcX, dy0 = earY - srcY, dz0 = lisZ - srcZ;
            const float r0 = std::sqrt(dx0 * dx0 + dy0 * dy0 + dz0 * dz0);
            int n = 0;
            for (int nx = -4; nx <= 4; ++nx) {
                for (int ny = -4; ny <= 4; ++ny) {
                    for (int nz = -4; nz <= 4; ++nz) {
                        const int fullOrder = std::abs(nx) + std::abs(ny) + std::abs(nz);
                        if (fullOrder == 0 || fullOrder > 4) continue;
                        const int order = std::min(fullOrder, 3); // absorption stages
                        auto image = [](int k, float len, float p) {
                            return (k % 2 == 0) ? static_cast<float>(k) * len + p
                                                : static_cast<float>(k + 1) * len - p;
                        };
                        const float ix = image(nx, L, srcX);
                        const float iy = image(ny, W, srcY);
                        const float iz = image(nz, H, srcZ);
                        const float dx = ix - lisX, dy = iy - earY, dz = iz - lisZ;
                        const float r = std::sqrt(dx * dx + dy * dy + dz * dz);
                        const float t = (r - r0) / kSpeedOfSound;
                        if (t <= 0.0005f || t > maxTime) continue;
                        // Head shadow: listener faces -x, left ear axis is -y.
                        const float planar = std::sqrt(dx * dx + dy * dy) + 1.0e-6f;
                        const float dot = (o == 0 ? -dy : dy) / planar;
                        const float shadow = 0.45f + 0.55f * (0.5f + 0.5f * dot);
                        float g = (r0 / r) * std::pow(R, static_cast<float>(fullOrder)) * shadow;
                        if (g < 1.0e-4f) continue;
                        // Real rooms are not perfect boxes: surfaces are uneven and
                        // furnished, so the image lattice is perturbed. A fixed
                        // per-image jitter (growing with order) breaks the lattice's
                        // regular spacing, which otherwise rings as a comb in small rooms.
                        float tj = t;
                        {
                            std::uint32_t hsh = static_cast<std::uint32_t>((nx + 8) * 361 + (ny + 8) * 19 + (nz + 8)) * 2654435761u
                                              ^ (s.seed * 40503u);
                            hsh ^= hsh >> 15; hsh *= 2246822519u; hsh ^= hsh >> 13;
                            const float u1 = static_cast<float>(hsh & 0xffff) / 32767.5f - 1.0f;
                            const float u2 = static_cast<float>(hsh >> 16) / 32767.5f - 1.0f;
                            tj = t * (1.0f + kLatticeJitter * static_cast<float>(fullOrder) * u1);
                            g *= 1.0f + 0.2f * u2;
                        }
                        if (tj <= 0.0005f || tj > maxTime) continue;
                        cands[static_cast<std::size_t>(n++)] = {tj, g, order};
                    }
                }
            }
            const int keep = std::min(n, kMaxTaps);
            std::partial_sort(cands.begin(), cands.begin() + keep, cands.begin() + n,
                              [](const Candidate& a, const Candidate& b) { return a.time < b.time; });
            auto& list = out.lists[in][o];
            list.count = keep;
            for (int i = 0; i < keep; ++i) {
                list.taps[i].delay = std::max(1, static_cast<int>(cands[i].time * fs_));
                list.taps[i].gain = cands[i].gain;
                list.taps[i].order = cands[i].order;
            }
        }
    }
}

void EarlyReflections::generateProcedural(const ErSettings& s, TapSet& out) const noexcept {
    const float maxTime = static_cast<float>(kMaxErSeconds) - 0.11f;
    for (int in = 0; in < 2; ++in) {
        for (int o = 0; o < 2; ++o) {
            Random rng(s.seed * 7919u + static_cast<std::uint32_t>(in * 2 + o) * 104729u + 3u);
            auto& list = out.lists[in][o];
            list.count = kMaxTaps;
            std::array<float, kMaxTaps> times{};
            if (s.pattern == ErPattern::NonlinU) {
                const float hold = 0.05f + 0.25f * s.proximity;
                const float tau = 0.008f + 0.09f * s.reflectivity;
                const float total = std::min(hold + 4.0f * tau, maxTime);
                for (int i = 0; i < kMaxTaps; ++i)
                    times[static_cast<std::size_t>(i)] = std::max(0.001f,
                        total * (static_cast<float>(i) + 0.5f + 0.4f * rng.bipolar()) / kMaxTaps);
                std::sort(times.begin(), times.end());
                for (int i = 0; i < kMaxTaps; ++i) {
                    const float t = times[static_cast<std::size_t>(i)];
                    const float env = t < hold ? 1.0f : std::exp(-(t - hold) / tau);
                    list.taps[i].delay = std::max(1, static_cast<int>(t * fs_));
                    list.taps[i].gain = env * (rng.uniform() < 0.5f ? -1.0f : 1.0f) * (0.75f + 0.25f * rng.uniform());
                    list.taps[i].order = 1;
                }
            } else { // Scattered
                const float span = std::min(0.015f * std::pow(16.0f, s.size), maxTime);
                const float tau = span * (0.15f + 0.85f * s.reflectivity);
                for (int i = 0; i < kMaxTaps; ++i)
                    times[static_cast<std::size_t>(i)] = std::max(0.0007f, span * std::pow(rng.uniform(), 0.6f));
                std::sort(times.begin(), times.end());
                for (int i = 0; i < kMaxTaps; ++i) {
                    const float t = times[static_cast<std::size_t>(i)];
                    list.taps[i].delay = std::max(1, static_cast<int>(t * fs_));
                    list.taps[i].gain = std::exp(-t / tau) * (rng.uniform() < 0.5f ? -1.0f : 1.0f)
                                      * (0.6f + 0.4f * rng.uniform());
                    list.taps[i].order = std::clamp(1 + static_cast<int>(3.0f * t / span), 1, 3);
                }
            }
        }
    }
}

void EarlyReflections::process(const float* inL, const float* inR, float* outL, float* outR,
                               int numSamples) noexcept {
    if (pendingDirty_ && fadeRemaining_ == 0) {
        generate(pendingSettings_, sets_[1 - active_]);
        active_ = 1 - active_;
        fadeRemaining_ = fadeLength_;
        pendingDirty_ = false;
    }
    const float width = std::clamp(settings_.width, 0.0f, 1.0f);
    // Derived values glide per sample while they are moving (a stepped glide of a
    // delay inside an allpass would zipper); once everything has arrived the
    // glide is skipped entirely and the scatter allpasses use exact integer reads.
    const float sc = smoothCoef_, si = 1.0f - smoothCoef_;
    for (int i = 0; i < numSamples; ++i) {
        input_[0].push(inL[i]);
        input_[1].push(inR[i]);
        if (!derivedSettled_) {
            for (int o = 0; o < 2; ++o)
                for (int k = 0; k < 4; ++k)
                    diffuserDelayCur_[o][k] = sc * diffuserDelayCur_[o][k] + si * diffuserDelay_[o][k];
            diffusionGainCur_ = sc * diffusionGainCur_ + si * diffusionGain_;
            preGainCur_ = sc * preGainCur_ + si * preGain_;
            diffuserModCur_ = sc * diffuserModCur_ + si * diffuserModSamples_;
            for (int k = 0; k < 2; ++k) vibratoDepthCur_[k] = sc * vibratoDepthCur_[k] + si * vibratoDepth_[k];
            for (int o = 0; o < 2; ++o)
                for (int kind = 0; kind < 2; ++kind)
                    for (int k = 0; k < 4; ++k)
                        scatterDelayCur_[o][kind][k] = sc * scatterDelayCur_[o][kind][k] + si * scatterDelay_[o][kind][k];
        }
        if (--controlCountdown_ <= 0) {
            controlCountdown_ = 32;
            if (absorbCurLog_ != absorbTargetLog_) {
                const float g = absorbCurLog_ + (absorbTargetLog_ - absorbCurLog_) * 0.25f;
                setAbsorb(std::abs(g - absorbTargetLog_) < 1.0e-4f ? absorbTargetLog_ : g);
            }
            // Snap values that have (practically) arrived; settle when all have.
            bool settled = true, scatterOk = true;
            auto snap = [&settled](float& c, float t, float eps) { if (std::abs(c - t) < eps) c = t; else settled = false; };
            for (int o = 0; o < 2; ++o)
                for (int k = 0; k < 4; ++k) snap(diffuserDelayCur_[o][k], diffuserDelay_[o][k], 1.0e-3f);
            snap(diffusionGainCur_, diffusionGain_, 1.0e-5f);
            snap(preGainCur_, preGain_, 1.0e-5f);
            snap(diffuserModCur_, diffuserModSamples_, 1.0e-4f);
            for (int k = 0; k < 2; ++k) snap(vibratoDepthCur_[k], vibratoDepth_[k], 1.0e-4f);
            for (int o = 0; o < 2; ++o)
                for (int kind = 0; kind < 2; ++kind)
                    for (int k = 0; k < 4; ++k) {
                        float& c = scatterDelayCur_[o][kind][k];
                        const float t = scatterDelay_[o][kind][k];
                        if (std::abs(c - t) < 1.0e-3f) c = t; else { settled = false; scatterOk = false; }
                    }
            derivedSettled_ = settled;
            scatterSettled_ = scatterOk;
        }
        const float scatterGain = std::min(0.62f, 1.03f * diffusionGainCur_);

        // bus[output][own/cross][order]
        float bus[2][2][3] = {};
        const float newWeight = fadeRemaining_ > 0
            ? sCurve(1.0f - static_cast<float>(fadeRemaining_) / static_cast<float>(fadeLength_)) : 1.0f;
        auto accumulate = [&](const TapSet& set, float weight) {
            for (int o = 0; o < 2; ++o) {
                for (int kind = 0; kind < 2; ++kind) {
                    const int in = kind == 0 ? o : 1 - o;
                    const auto& list = set.lists[in][o];
                    const auto& line = input_[static_cast<std::size_t>(in)];
                    for (int t = 0; t < list.count; ++t) {
                        const auto& tap = list.taps[t];
                        bus[o][kind][tap.order - 1] += weight * tap.gain
                            * line.read(static_cast<std::size_t>(tap.delay));
                    }
                }
            }
        };
        accumulate(sets_[active_], newWeight);
        if (fadeRemaining_ > 0) {
            accumulate(sets_[1 - active_], 1.0f - newWeight);
            --fadeRemaining_;
        }

        float y[2];
        for (int o = 0; o < 2; ++o) {
            float sum = 0.0f;
            for (int kind = 0; kind < 2; ++kind) {
                auto& f = absorb_[o][kind];
                // Order-k reflections pass through k absorption stages.
                auto& sc = scatter_[o][kind];
                const auto& sd = scatterDelayCur_[o][kind];
                const float sg = scatterGain;
                float v = f[2].process(bus[o][kind][2]);
                if (scatterSettled_) {
                    v = sc[3].processInt(sc[2].processInt(v, static_cast<int>(sd[2]), sg), static_cast<int>(sd[3]), -sg);
                    v = f[1].process(bus[o][kind][1] + v);
                    v = sc[1].processInt(sc[0].processInt(v, static_cast<int>(sd[0]), -sg), static_cast<int>(sd[1]), sg);
                } else {
                    v = sc[3].process(sc[2].process(v, sd[2], sg), sd[3], -sg);
                    v = f[1].process(bus[o][kind][1] + v);
                    v = sc[1].process(sc[0].process(v, sd[0], -sg), sd[1], sg);
                }
                v = f[0].process(bus[o][kind][0] + v);
                if (kind == 1) v = crossRolloff_[o].process(v);
                auto& vib = vibrato_[o][kind];
                vib.push(v);
                const float base = 0.003f * static_cast<float>(fs_);
                const float d = base + vibratoDepthCur_[kind] * vibratoLfo_[o][kind].next();
                sum += vib.readFrac(d);
            }
            // Dense smearing right at the start of the reflection sum, so the early
            // field is diffuse within ~10 ms (as in reference rooms) instead of a
            // sparse train of discrete taps.
            {
                static constexpr float kPreMs[4] = {0.37f, 0.61f, 0.97f, 1.53f};
                // Diffusion sets how smooth, not whether: even low settings stay dense.
                const float pg = preGainCur_;
                for (int k = 0; k < 4; ++k)
                    sum = preDiff_[o][static_cast<std::size_t>(k)].processInt(sum,
                        std::max(2, static_cast<int>(kPreMs[k] * (o ? 1.083f : 1.0f) * 0.001f * static_cast<float>(fs_) + 0.5f)),
                        (k % 2 ? -1.0f : 1.0f) * pg);
            }
            for (int k = 0; k < 4; ++k) {
                const float d = diffuserDelayCur_[o][k] + diffuserModCur_ * diffuserLfo_[o][k].next();
                sum = diffusers_[o][k].process(sum, d, diffusionGainCur_ * (k % 2 ? -1.0f : 1.0f));
            }
            y[o] = rolloff_[o].process(sum);
        }
        const float mid = 0.5f * (y[0] + y[1]);
        const float side = 0.5f * (y[0] - y[1]) * width;
        outL[i] = mid + side;
        outR[i] = mid - side;
    }
    for (auto& o : absorb_) for (auto& k : o) for (auto& f : k) f.flush();
}

} // namespace rr
