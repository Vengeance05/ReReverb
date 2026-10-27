// Shared low-level DSP building blocks for the rereverb plugins.
// Header-only, allocation happens only in prepare()/resize calls.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rr {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
// ln(1000): amplitude falls by 60 dB when exp(-kLn1000 * t / rt60).
constexpr double kLn1000 = 6.907755278982137;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) noexcept { return 20.0f * std::log10(std::max(g, 1.0e-12f)); }

// Per-sample gain that gives a 60 dB decay in rt60 seconds over `delaySeconds`.
inline double decayGainForDelay(double delaySeconds, double rt60) noexcept {
    if (rt60 <= 0.0) return 0.0;
    return std::exp(-kLn1000 * delaySeconds / rt60);
}

// Prevents denormal build-up in long feedback paths.
inline float flushDenormal(float x) noexcept {
    return std::abs(x) < 1.0e-20f ? 0.0f : x;
}

// ---------------------------------------------------------------------------
// Deterministic PRNG (xorshift32) for repeatable patterns and modulation.
class Random {
public:
    explicit Random(std::uint32_t seed = 0x9E3779B9u) noexcept : state_(seed ? seed : 1u) {}
    void seed(std::uint32_t s) noexcept { state_ = s ? s : 1u; }
    std::uint32_t nextU32() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    // Uniform in [0, 1)
    float uniform() noexcept { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }
    // Uniform in [-1, 1)
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }
private:
    std::uint32_t state_;
};

// ---------------------------------------------------------------------------
// One-pole parameter smoother.
class Smoother {
public:
    void prepare(double sampleRate, double timeMs) noexcept {
        const double samples = std::max(1.0, sampleRate * timeMs * 0.001);
        coeff_ = static_cast<float>(std::exp(-1.0 / samples));
    }
    void setTarget(float t) noexcept { target_ = t; }
    void snap(float v) noexcept { target_ = value_ = v; }
    float next() noexcept {
        value_ = target_ + (value_ - target_) * coeff_;
        return value_;
    }
    // Advance n samples at once (for control-rate use).
    float advance(int n) noexcept {
        value_ = target_ + (value_ - target_) * std::pow(coeff_, static_cast<float>(n));
        return value_;
    }
    float value() const noexcept { return value_; }
    float target() const noexcept { return target_; }
private:
    float coeff_ = 0.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

// Smoothstep: maps a linear 0..1 fade onto an S-curve with zero slope at both
// ends, so crossfades start and stop without a corner (no high-frequency tick).
inline float sCurve(float m) noexcept { return m * m * (3.0f - 2.0f * m); }

// 0..1 switch weight: a linear ramp of fixed duration shaped by sCurve().
// Interface matches Smoother (prepare/setTarget/snap/next/target).
class Fader {
public:
    void prepare(double sampleRate, double timeMs) noexcept {
        step_ = static_cast<float>(1.0 / std::max(1.0, sampleRate * timeMs * 0.001));
    }
    void setTarget(float t) noexcept { target_ = t; }
    void snap(float v) noexcept { target_ = ramp_ = v; }
    float next() noexcept {
        if (ramp_ != target_) ramp_ = target_ > ramp_ ? std::min(target_, ramp_ + step_) : std::max(target_, ramp_ - step_);
        return sCurve(ramp_);
    }
    float target() const noexcept { return target_; }
private:
    float step_ = 0.001f, ramp_ = 0.0f, target_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Biquad (transposed direct form II) with RBJ cookbook designs.
struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

namespace design {

inline double clampFreq(double fs, double f) noexcept {
    return std::clamp(f, 5.0, fs * 0.49);
}

inline BiquadCoeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
    BiquadCoeffs c;
    c.b0 = static_cast<float>(b0 / a0);
    c.b1 = static_cast<float>(b1 / a0);
    c.b2 = static_cast<float>(b2 / a0);
    c.a1 = static_cast<float>(a1 / a0);
    c.a2 = static_cast<float>(a2 / a0);
    return c;
}

inline BiquadCoeffs lowpass(double fs, double f, double q) noexcept {
    const double w = kTwoPi * clampFreq(fs, f) / fs, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return normalise((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
}
inline BiquadCoeffs highpass(double fs, double f, double q) noexcept {
    const double w = kTwoPi * clampFreq(fs, f) / fs, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return normalise((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
}
inline BiquadCoeffs peak(double fs, double f, double q, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = kTwoPi * clampFreq(fs, f) / fs, cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    return normalise(1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
}
inline BiquadCoeffs lowShelf(double fs, double f, double q, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = kTwoPi * clampFreq(fs, f) / fs, cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * q), sA = 2.0 * std::sqrt(A) * alpha;
    return normalise(A * ((A + 1) - (A - 1) * cw + sA), 2 * A * ((A - 1) - (A + 1) * cw),
                     A * ((A + 1) - (A - 1) * cw - sA), (A + 1) + (A - 1) * cw + sA,
                     -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sA);
}
inline BiquadCoeffs highShelf(double fs, double f, double q, double gainDb) noexcept {
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w = kTwoPi * clampFreq(fs, f) / fs, cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * q), sA = 2.0 * std::sqrt(A) * alpha;
    return normalise(A * ((A + 1) + (A - 1) * cw + sA), -2 * A * ((A - 1) + (A + 1) * cw),
                     A * ((A + 1) + (A - 1) * cw - sA), (A + 1) - (A - 1) * cw + sA,
                     2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sA);
}
// First-order low/high pass expressed as a biquad (bilinear transform).
inline BiquadCoeffs lowpass1(double fs, double f) noexcept {
    const double k = std::tan(kPi * clampFreq(fs, f) / fs);
    return normalise(k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
}
// One-pole lowpass with the pole matched to the analog one (no zero at Nyquist),
// so the top octave is not pulled down by frequency warping.
inline BiquadCoeffs onePoleLowpass(double fs, double f) noexcept {
    const double a = 1.0 - std::exp(-2.0 * kPi * clampFreq(fs, f) / fs);
    BiquadCoeffs c; c.b0 = static_cast<float>(a); c.a1 = static_cast<float>(a - 1.0); return c;
}
inline BiquadCoeffs highpass1(double fs, double f) noexcept {
    const double k = std::tan(kPi * clampFreq(fs, f) / fs);
    return normalise(1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0);
}

} // namespace design

class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    void setCoeffsFrom(const Biquad& other) noexcept { c_ = other.c_; }   // keeps this filter's state
    const BiquadCoeffs& coeffs() const noexcept { return c_; }
    void reset() noexcept { z1_ = z2_ = 0.0f; }
    float process(float x) noexcept {
        const float y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }
    void flush() noexcept { z1_ = flushDenormal(z1_); z2_ = flushDenormal(z2_); }
private:
    BiquadCoeffs c_;
    float z1_ = 0.0f, z2_ = 0.0f;
};

// First-order section (b2 = a2 = 0) using BiquadCoeffs: cheaper than a Biquad.
class FirstOrder {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { b0_ = c.b0; b1_ = c.b1; a1_ = c.a1; }
    void reset() noexcept { x1_ = y1_ = 0.0f; }
    float process(float x) noexcept {
        const float y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x; y1_ = y;
        return y;
    }
    void flush() noexcept { y1_ = flushDenormal(y1_); }
private:
    float b0_ = 1.0f, b1_ = 0.0f, a1_ = 0.0f, x1_ = 0.0f, y1_ = 0.0f;
};

// Simple one-pole lowpass (y += a * (x - y)).
class OnePoleLowpass {
public:
    void setCutoff(double fs, double f) noexcept {
        a_ = static_cast<float>(1.0 - std::exp(-kTwoPi * design::clampFreq(fs, f) / fs));
    }
    void setCoefficient(float a) noexcept { a_ = a; }
    float process(float x) noexcept { y_ += a_ * (x - y_); return y_; }
    void reset() noexcept { y_ = 0.0f; }
    float state() const noexcept { return y_; }
    void flush() noexcept { y_ = flushDenormal(y_); }
private:
    float a_ = 1.0f;
    float y_ = 0.0f;
};

// Lowpass with selectable slope: 6, 12, 18 or 24 dB/octave.
// A cutoff at or above `bypassHz` disables the filter.
// Shape::Butterworth: maximally flat, -3 dB at the cutoff.
// Shape::Cascade: 1-4 identical first-order poles at the cutoff (-3 dB per pole
// there): a soft knee that starts darkening well below the cutoff, as in the
// roll-off filters of classic hall processors.
class RollOff {
public:
    enum class Shape { Butterworth, Cascade };
    void setShape(Shape sh) noexcept {
        if (sh == shape_) return;
        shape_ = sh;
        if (cur_.slope != 0) { old_ = cur_; oldMix_ = mix_ > 0.0f ? 1.0f : 0.0f; build(cur_, curLogF_); }
    }
    // The cutoff glides (~20 ms, coefficients refreshed every 16 samples) so
    // automating it has no zipper noise. Engaging/disengaging (crossing
    // bypassHz) and slope changes crossfade over ~15 ms. Fully off, the input
    // passes untouched.
    void setup(double fs, double cutoffHz, int slopeDb, double bypassHz = 19999.0) noexcept {
        if (fs != fs_) {
            fs_ = fs;
            fadeStep_ = static_cast<float>(1.0 / std::max(1.0, 0.015 * fs));
            glide_ = static_cast<float>(std::exp(-16.0 / (0.02 * fs)));
        }
        if (cutoffHz >= bypassHz) { target_ = 0.0f; return; }   // keep the response for the fade-out
        target_ = 1.0f;
        const float logF = static_cast<float>(std::log(std::min(cutoffHz, fs * 0.45)));
        targetLogF_ = logF;
        if (mix_ <= 0.0f) {
            // Starting from bypass: clean state, jump straight to the cutoff.
            curLogF_ = logF;
            cur_ = Set{}; cur_.slope = slopeDb; build(cur_, logF);
            oldMix_ = 0.0f;
        } else if (slopeDb != cur_.slope) {
            if (oldMix_ > 0.0f) pendingSlope_ = slopeDb;            // finish the running fade first
            else startSlopeFade(slopeDb);
        } else {
            pendingSlope_ = 0;
        }
    }
    float process(float x) noexcept {
        if (mix_ <= 0.0f && target_ <= 0.0f) return x;
        if (--countdown_ <= 0) {
            countdown_ = 16;
            if (std::abs(curLogF_ - targetLogF_) > 1.0e-4f) {
                curLogF_ = targetLogF_ + (curLogF_ - targetLogF_) * glide_;
                build(cur_, curLogF_);
            }
        }
        float y = cur_.run(x);
        if (oldMix_ > 0.0f) {
            y += sCurve(oldMix_) * (old_.run(x) - y);
            oldMix_ = std::max(0.0f, oldMix_ - fadeStep_);
            if (oldMix_ <= 0.0f && pendingSlope_ != 0 && pendingSlope_ != cur_.slope) startSlopeFade(pendingSlope_);
        }
        if (mix_ != target_) mix_ = target_ > mix_ ? std::min(target_, mix_ + fadeStep_) : std::max(target_, mix_ - fadeStep_);
        return x + sCurve(mix_) * (y - x);
    }
    void reset() noexcept {
        for (auto& st : cur_.s) st.reset();
        oldMix_ = 0.0f; mix_ = target_; curLogF_ = targetLogF_;
        if (target_ > 0.0f) build(cur_, curLogF_);
    }
    bool bypassed() const noexcept { return mix_ <= 0.0f && target_ <= 0.0f; }
private:
    void startSlopeFade(int slopeDb) noexcept {
        old_ = cur_; oldMix_ = 1.0f;                                // topology change: fade old -> new
        cur_ = Set{}; cur_.slope = slopeDb; build(cur_, curLogF_);
        pendingSlope_ = 0;
    }
    int pendingSlope_ = 0;
    struct Set {
        std::array<Biquad, 4> s{};
        int stages = 0, slope = 0;
        float run(float x) noexcept { for (int i = 0; i < stages; ++i) x = s[static_cast<std::size_t>(i)].process(x); return x; }
    };
    void build(Set& set, float logF) const noexcept {
        const double f = std::exp(static_cast<double>(logF));
        if (shape_ == Shape::Cascade) {
            set.stages = std::clamp(set.slope / 6, 1, 4);
            for (int i = 0; i < set.stages; ++i) set.s[static_cast<std::size_t>(i)].setCoeffs(design::onePoleLowpass(fs_, f));
            return;
        }
        switch (set.slope) {
            case 6: set.stages = 1; set.s[0].setCoeffs(design::lowpass1(fs_, f)); break;
            case 18: set.stages = 2; set.s[0].setCoeffs(design::lowpass1(fs_, f)); set.s[1].setCoeffs(design::lowpass(fs_, f, 1.0)); break;
            case 24: set.stages = 2; set.s[0].setCoeffs(design::lowpass(fs_, f, 0.5411961)); set.s[1].setCoeffs(design::lowpass(fs_, f, 1.3065630)); break;
            case 12: default: set.stages = 1; set.s[0].setCoeffs(design::lowpass(fs_, f, 0.7071068)); break;
        }
    }
    Set cur_, old_;
    Shape shape_ = Shape::Butterworth;
    double fs_ = 0.0;
    float mix_ = 0.0f, target_ = 0.0f, oldMix_ = 0.0f, fadeStep_ = 0.001f, glide_ = 0.98f;
    float curLogF_ = 0.0f, targetLogF_ = 0.0f;
    int countdown_ = 0;
};

// ---------------------------------------------------------------------------
// Power-of-two circular delay line with integer and cubic-interpolated reads.
class DelayLine {
public:
    void prepare(std::size_t maxDelaySamples) {
        std::size_t size = 4;
        while (size < maxDelaySamples + 4) size <<= 1;
        buffer_.assign(size, 0.0f);
        mask_ = size - 1;
        write_ = 0;
    }
    void reset() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); write_ = 0; }
    void push(float x) noexcept {
        buffer_[write_] = x;
        write_ = (write_ + 1) & mask_;
    }
    // Delay of `d` samples relative to the most recently pushed sample (d >= 0).
    float read(std::size_t d) const noexcept {
        return buffer_[(write_ - 1 - d) & mask_];
    }
    // Cubic (Hermite) fractional read; d >= 1 recommended.
    float readFrac(float d) const noexcept {
        const auto di = static_cast<std::size_t>(d);
        const float f = d - static_cast<float>(di);
        const float xm1 = read(di - (di > 0 ? 1 : 0));
        const float x0 = read(di);
        const float x1 = read(di + 1);
        const float x2 = read(di + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    std::size_t capacity() const noexcept { return buffer_.empty() ? 0 : buffer_.size() - 4; }
private:
    std::vector<float> buffer_;
    std::size_t mask_ = 0;
    std::size_t write_ = 0;
};

// Schroeder allpass built on a modulatable delay line.
class Allpass {
public:
    void prepare(std::size_t maxDelaySamples) { line_.prepare(maxDelaySamples + 4); }
    void reset() noexcept { line_.reset(); }
    float process(float x, float delaySamples, float g) noexcept {
        const float delayed = line_.readFrac(std::max(1.0f, delaySamples - 1.0f));
        const float v = x + g * delayed;
        line_.push(flushDenormal(v));
        return delayed - g * v;
    }
    // Same as process() for an integer delay (>= 2), without interpolation.
    float processInt(float x, int delaySamples, float g) noexcept {
        const float delayed = line_.read(static_cast<std::size_t>(delaySamples - 1));
        const float v = x + g * delayed;
        line_.push(flushDenormal(v));
        return delayed - g * v;
    }
    // Lossy variants: the internal delay is attenuated by `a` per pass, so energy
    // circulating inside the allpass decays at the same rate per second as the
    // rest of a reverb loop (every delay element scaled as z^-1 -> gamma z^-1).
    float processLossy(float x, float delaySamples, float g, float a) noexcept {
        const float delayed = a * line_.readFrac(std::max(1.0f, delaySamples - 1.0f));
        const float v = x + g * delayed;
        line_.push(flushDenormal(v));
        return delayed - g * v;
    }
    float processIntLossy(float x, int delaySamples, float g, float a) noexcept {
        const float delayed = a * line_.read(static_cast<std::size_t>(delaySamples - 1));
        const float v = x + g * delayed;
        line_.push(flushDenormal(v));
        return delayed - g * v;
    }
private:
    DelayLine line_;
};

// ---------------------------------------------------------------------------
// Smoothed random LFO: cosine-interpolates between random targets.
// Avoids the periodic "chorus" signature of a pure sine.
class RandomLfo {
public:
    void prepare(double sampleRate, float rateHz, std::uint32_t seed) noexcept {
        sampleRate_ = sampleRate;
        rng_.seed(seed);
        setRate(rateHz);
        from_ = rng_.bipolar();
        to_ = rng_.bipolar();
        phase_ = rng_.uniform();
    }
    void setRate(float rateHz) noexcept {
        inc_ = static_cast<float>(std::max(0.01, static_cast<double>(rateHz)) / sampleRate_);
    }
    // Advance n samples at once (control-rate use); returns the new value.
    float nextBlock(int n) noexcept {
        phase_ += inc_ * static_cast<float>(n);
        while (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            from_ = to_;
            to_ = std::clamp(from_ + rng_.bipolar() * 1.2f, -1.0f, 1.0f);
        }
        const float s = phase_ * phase_ * (3.0f - 2.0f * phase_);
        return from_ + (to_ - from_) * s;
    }
    float next() noexcept {
        phase_ += inc_;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            from_ = to_;
            // Keep consecutive targets reasonably far apart for consistent motion.
            to_ = std::clamp(from_ + rng_.bipolar() * 1.2f, -1.0f, 1.0f);
        }
        // Smoothstep easing (cheaper than a cosine, same C1 continuity).
        const float s = phase_ * phase_ * (3.0f - 2.0f * phase_);
        return from_ + (to_ - from_) * s;
    }
private:
    Random rng_;
    double sampleRate_ = 48000.0;
    float inc_ = 0.0f, phase_ = 0.0f, from_ = 0.0f, to_ = 0.0f;
};

// In-place normalised fast Walsh-Hadamard transform (N power of two).
template <std::size_t N>
inline void hadamard(std::array<float, N>& v) noexcept {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    for (std::size_t h = 1; h < N; h <<= 1) {
        for (std::size_t i = 0; i < N; i += h << 1) {
            for (std::size_t j = i; j < i + h; ++j) {
                const float a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
        }
    }
    const float norm = 1.0f / std::sqrt(static_cast<float>(N));
    for (auto& x : v) x *= norm;
}

// Envelope follower (peak detector with attack/release).
class EnvelopeFollower {
public:
    void prepare(double fs, double attackMs, double releaseMs) noexcept {
        att_ = static_cast<float>(std::exp(-1.0 / std::max(1.0, fs * attackMs * 0.001)));
        rel_ = static_cast<float>(std::exp(-1.0 / std::max(1.0, fs * releaseMs * 0.001)));
    }
    float process(float x) noexcept {
        const float a = std::abs(x);
        env_ = a > env_ ? a + (env_ - a) * att_ : a + (env_ - a) * rel_;
        return env_;
    }
    float value() const noexcept { return env_; }
    void reset() noexcept { env_ = 0.0f; }
private:
    float att_ = 0.0f, rel_ = 0.0f, env_ = 0.0f;
};

} // namespace rr
