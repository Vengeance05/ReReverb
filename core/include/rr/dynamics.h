// Ducking / compression and a keyed reverb-tail gate.
#pragma once

#include "rr/dsp.h"

namespace rr {

enum class DynamicsMode : int { Off = 0, DuckReverb, DuckWet, CompressWet };

struct DynamicsSettings {
    DynamicsMode mode = DynamicsMode::Off;
    float thresholdDb = -24.0f;
    float ratio = 4.0f;
    float kneeDb = 6.0f;
    float attackMs = 10.0f;
    float releaseMs = 250.0f;
    float makeupDb = 0.0f;

    bool gateOn = false;
    float gateThresholdDb = -20.0f;
    float gateHoldMs = 50.0f;
    float gateRangeDb = 25.0f;
    float gateCloseMs = 200.0f;
    float gateOpenMs = 10.0f;
    float gateHysteresisDb = 3.0f;
};

// Computes per-sample gains; the caller applies them to the right signals.
class Dynamics {
public:
    void prepare(double fs) noexcept {
        fs_ = fs;
        detector_.prepare(fs, 1.0, 60.0);
        compDetector_.prepare(fs, 1.0, 60.0);
        gateDetector_.prepare(fs, 0.5, 30.0);
        reset();
        setSettings(settings_);
    }
    void reset() noexcept {
        detector_.reset(); compDetector_.reset(); gateDetector_.reset();
        grDb_ = 0.0f; compGrDb_ = 0.0f; makeupCur_ = makeup_; gateGain_ = 1.0f; gateOpen_ = true; holdCounter_ = 0;
    }
    void setSettings(const DynamicsSettings& s) noexcept {
        settings_ = s;
        att_ = coeff(s.attackMs);
        rel_ = coeff(s.releaseMs);
        gateFloor_ = dbToGain(-std::abs(s.gateRangeDb));
        // Linear-in-dB ramps: per-sample multiplicative steps.
        gateCloseStep_ = std::pow(gateFloor_, 1.0f / std::max(1.0f, s.gateCloseMs * 0.001f * static_cast<float>(fs_)));
        gateOpenStep_ = std::pow(1.0f / gateFloor_, 1.0f / std::max(1.0f, s.gateOpenMs * 0.001f * static_cast<float>(fs_)));
        holdSamples_ = static_cast<int>(s.gateHoldMs * 0.001f * static_cast<float>(fs_));
        makeup_ = dbToGain(s.makeupDb);
    }
    const DynamicsSettings& settings() const noexcept { return settings_; }

    // Gain computers run continuously (whatever the mode) so that switching mode
    // never starts from a stale state; the caller crossfades where they apply.
    // duckGain: keyed by the dry input. compGain: keyed by the wet signal.
    float duckGain(float key) noexcept { return computeGain(detector_, grDb_, key); }
    float compGain(float key) noexcept { return computeGain(compDetector_, compGrDb_, key); }
    // Kept for tests / existing callers: honours the mode.
    float compressorGain(float key) noexcept {
        if (settings_.mode == DynamicsMode::Off) return 1.0f;
        return duckGain(key);
    }

    // Keyed by the dry input; returns the tail gain.
    float gateGain(float key) noexcept {
        if (!settings_.gateOn) {
            // Release smoothly when the gate is switched off.
            gateOpen_ = true; holdCounter_ = 0;
            gateGain_ = std::min(1.0f, gateGain_ * gateOpenStep_);
            return gateGain_;
        }
        const float levelDb = gainToDb(gateDetector_.process(key));
        if (gateOpen_) {
            if (levelDb < settings_.gateThresholdDb) {
                if (++holdCounter_ > holdSamples_) gateOpen_ = false;
            } else holdCounter_ = 0;
        } else if (levelDb > settings_.gateThresholdDb + settings_.gateHysteresisDb) {
            gateOpen_ = true;
            holdCounter_ = 0;
        }
        if (gateOpen_) gateGain_ = std::min(1.0f, gateGain_ * gateOpenStep_);
        else gateGain_ = std::max(gateFloor_, gateGain_ * gateCloseStep_);
        return gateGain_;
    }
    float currentReductionDb() const noexcept { return grDb_; }

private:
    float computeGain(EnvelopeFollower& det, float& gr, float key) noexcept {
        const float levelDb = gainToDb(det.process(key));
        const float over = levelDb - settings_.thresholdDb;
        const float knee = std::max(0.0f, settings_.kneeDb);
        const float slope = 1.0f - 1.0f / std::max(1.0f, settings_.ratio);
        float target = 0.0f;
        if (2.0f * over < -knee) target = 0.0f;
        else if (knee > 0.0f && 2.0f * std::abs(over) <= knee)
            target = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        else target = slope * over;
        gr = target > gr ? target + (gr - target) * att_ : target + (gr - target) * rel_;
        makeupCur_ += (makeup_ - makeupCur_) * 0.0005f;   // ~40 ms glide at 48 kHz (two calls per sample)
        return dbToGain(-gr) * makeupCur_;
    }
    float coeff(float ms) const noexcept {
        return static_cast<float>(std::exp(-1.0 / std::max(1.0, fs_ * ms * 0.001)));
    }
    double fs_ = 48000.0;
    DynamicsSettings settings_{};
    EnvelopeFollower detector_, compDetector_, gateDetector_;
    float att_ = 0.0f, rel_ = 0.0f, grDb_ = 0.0f, compGrDb_ = 0.0f, makeup_ = 1.0f, makeupCur_ = 1.0f;
    float gateGain_ = 1.0f, gateFloor_ = 0.05f, gateCloseStep_ = 1.0f, gateOpenStep_ = 1.0f;
    bool gateOpen_ = true;
    int holdCounter_ = 0, holdSamples_ = 0;
};

} // namespace rr
