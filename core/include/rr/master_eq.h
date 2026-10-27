// Configurable stereo-linked master EQ used by both plugins.
#pragma once

#include "rr/dsp.h"

#include <array>

namespace rr {

enum class EqType { Off = 0, LowCut, LowShelf, Peak, HighShelf, HighCut };

struct EqBandSettings {
    EqType type = EqType::Off;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
};

template <std::size_t Bands>
class MasterEq {
public:
    void prepare(double sampleRate) noexcept {
        fs_ = sampleRate;
        for (std::size_t b = 0; b < Bands; ++b) {
            freq_[b].prepare(fs_, 30.0); gain_[b].prepare(fs_, 30.0); q_[b].prepare(fs_, 30.0);
            freq_[b].snap(std::log(target_[b].freqHz));
            gain_[b].snap(target_[b].gainDb);
            q_[b].snap(target_[b].q);
        }
        fadeStep_ = static_cast<float>(1.0 / std::max(1.0, 0.015 * fs_));
        updateCoefficients(0);
        reset();
        for (std::size_t b = 0; b < Bands; ++b) mix_[b] = enabled_ && !isTransparent(b) ? 1.0f : 0.0f;
    }
    void reset() noexcept {
        for (auto& ch : filters_) for (auto& f : ch) f.reset();
    }
    void setBand(std::size_t band, const EqBandSettings& s) noexcept {
        if (band >= Bands) return;
        target_[band] = s;
        freq_[band].setTarget(std::log(std::clamp(s.freqHz, 10.0f, 22000.0f)));
        gain_[band].setTarget(s.gainDb);
        q_[band].setTarget(std::clamp(s.q, 0.1f, 18.0f));
    }
    void setEnabled(bool e) noexcept { enabled_ = e; }

    // Filters run continuously; a band that becomes inaudible (off, a cut at
    // the frequency extremes, a flat shelf/peak) or the whole EQ being switched
    // off crossfades out over ~15 ms, and once faded the signal passes exactly
    // untouched. Switching back on fades in from a warm filter, so nothing clicks.
    void process(float* left, float* right, int numSamples) noexcept {
        int pos = 0;
        while (pos < numSamples) {
            const int chunk = std::min(kControlBlock, numSamples - pos);
            updateCoefficients(chunk);
            for (std::size_t b = 0; b < Bands; ++b) {
                const float target = enabled_ && !isTransparent(b) ? 1.0f : 0.0f;
                auto& fl = filters_[0][b];
                auto& fr = filters_[1][b];
                float m = mix_[b];
                if (m == 0.0f && target == 0.0f) {
                    // Keep the filter warm without touching the signal.
                    for (int i = pos; i < pos + chunk; ++i) { fl.process(left[i]); fr.process(right[i]); }
                } else {
                    for (int i = pos; i < pos + chunk; ++i) {
                        if (m != target) m = target > m ? std::min(target, m + fadeStep_) : std::max(target, m - fadeStep_);
                        const float yl = fl.process(left[i]), yr = fr.process(right[i]);
                        left[i] += m * (yl - left[i]);
                        right[i] += m * (yr - right[i]);
                    }
                    mix_[b] = m;
                }
                fl.flush(); fr.flush();
            }
            pos += chunk;
        }
    }

private:
    static constexpr int kControlBlock = 32;

    // A band is skipped (bit-exact passthrough) when it cannot affect the sound:
    // switched off, a cut at/above 20 kHz, or a shelf/peak whose gain is flat
    // and has finished smoothing.
    bool isTransparent(std::size_t b) const noexcept {
        const auto& t = target_[b];
        switch (t.type) {
            case EqType::Off: return true;
            case EqType::HighCut: return t.freqHz >= 19999.0f && std::exp(freq_[b].value()) >= 19990.0;
            case EqType::LowCut: return t.freqHz <= 10.5f;
            case EqType::LowShelf: case EqType::Peak: case EqType::HighShelf:
                return std::abs(t.gainDb) < 0.01f && std::abs(gain_[b].value()) < 0.01f;
        }
        return false;
    }
    double fs_ = 48000.0;
    bool enabled_ = true;
    std::array<float, Bands> mix_{};
    float fadeStep_ = 0.0015f;
    std::array<EqBandSettings, Bands> target_{};
    std::array<Smoother, Bands> freq_{}, gain_{}, q_{};
    std::array<std::array<Biquad, Bands>, 2> filters_{};

    void updateCoefficients(int advance) noexcept {
        for (std::size_t b = 0; b < Bands; ++b) {
            const double f = std::exp(advance > 0 ? freq_[b].advance(advance) : freq_[b].value());
            const double g = advance > 0 ? gain_[b].advance(advance) : gain_[b].value();
            const double q = advance > 0 ? q_[b].advance(advance) : q_[b].value();
            BiquadCoeffs c;
            switch (target_[b].type) {
                case EqType::LowCut: c = design::highpass(fs_, f, 0.7071); break;
                case EqType::LowShelf: c = design::lowShelf(fs_, f, q, g); break;
                case EqType::Peak: c = design::peak(fs_, f, q, g); break;
                case EqType::HighShelf: c = design::highShelf(fs_, f, q, g); break;
                case EqType::HighCut:
                    c = design::lowpass(fs_, std::min(f, 0.45 * fs_), 0.7071);
                    break;
                case EqType::Off: default: continue;   // keep the last response for the fade-out
            }
            filters_[0][b].setCoeffs(c);
            filters_[1][b].setCoeffs(c);
        }
    }
};

} // namespace rr
