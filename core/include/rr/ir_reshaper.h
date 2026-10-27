// Offline impulse-response analysis and reshaping for Septima.
//
// A captured response is held as true stereo (LL, LR, RL, RR: input->output).
// Reshaping splits each channel into zero-phase complementary bands, measures
// each band's RT60 (Schroeder integration with noise-floor compensation),
// retimes each band's decay with an exponential envelope, and replaces the
// noise floor with band-limited decaying noise so the tail can be lengthened
// cleanly. The result is split into early, late and VLF components.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rr {

enum class IrFormat { MonoToStereo, Stereo, TrueStereo };

struct TrueStereoIr {
    double sampleRate = 48000.0;
    IrFormat format = IrFormat::TrueStereo;
    // [0]=L->L, [1]=L->R, [2]=R->L, [3]=R->R
    std::array<std::vector<float>, 4> ch;
    std::size_t length() const noexcept { return ch[0].size(); }
};

// Builders for the common capture layouts.
TrueStereoIr makeMonoToStereo(std::vector<float> left, std::vector<float> right, double fs);
TrueStereoIr makeStereo(std::vector<float> left, std::vector<float> right, double fs);
TrueStereoIr makeTrueStereo(std::vector<float> ll, std::vector<float> lr,
                            std::vector<float> rl, std::vector<float> rr, double fs);

constexpr int kDecayBands = 9;
// Band edges (Hz) between the kDecayBands bands.
constexpr std::array<float, kDecayBands - 1> kDecayBandEdges{90.f, 180.f, 355.f, 710.f, 1400.f,
                                                             2800.f, 5600.f, 11200.f};
float decayBandCentre(int band) noexcept;

struct BandDecay {
    float rt60 = 0.0f;        // seconds; 0 if not measurable
    float extendSeconds = 0;  // where measured decay hands over to synthesised decay
    float levelAtExtend = 0;  // RMS of the band at extendSeconds
};

struct IrAnalysis {
    std::array<std::array<BandDecay, kDecayBands>, 4> channel{};
    std::array<float, kDecayBands> bandRt{};   // averaged across channels
    float midRt = 0.0f;                         // mean of the 500 Hz-1.4 kHz bands
    float normalisationGain = 1.0f;
    float earlyEnergy = 0.0f;                   // per-input energy of early part (normalised)
    double sampleRate = 48000.0;
};

// Trims leading silence consistently across channels (keeps 1 ms margin).
void trimOnset(TrueStereoIr& ir, float thresholdDb = -50.0f);

IrAnalysis analyseIr(const TrueStereoIr& ir);

struct ReshapeParams {
    float decaySeconds = 0.0f;     // target mid-band RT60; <= 0 keeps the sampled decay
    float lowMultiplier = 1.0f;    // decay multiplier below lowCrossoverHz
    float lowCrossoverHz = 250.0f;
    float highMultiplier = 1.0f;   // decay multiplier above highCrossoverHz
    float highCrossoverHz = 4000.0f;
    float earlyEndSeconds = 0.07f; // centre of the early/late crossover
    float earlyCrossfadeSeconds = 0.06f;
    float vlfHz = 200.0f;
    float maxSeconds = 32.0f;
    std::uint32_t seed = 12345;
};

struct ReshapedIr {
    double sampleRate = 48000.0;
    std::array<std::vector<float>, 4> early;  // true stereo
    std::array<std::vector<float>, 4> late;   // true stereo, VLF band removed
    std::array<std::vector<float>, 4> vlf;    // true stereo, below vlfHz
    std::array<float, kDecayBands> targetRt{};
};

// Decay-time multiplier applied at frequency f by the band multipliers.
float decayMultiplierAt(float f, const ReshapeParams& p) noexcept;

ReshapedIr reshapeIr(const TrueStereoIr& ir, const IrAnalysis& analysis, const ReshapeParams& params);

// Zero-phase complementary band split (exposed for tests): bands sum to input.
std::vector<std::vector<float>> splitBands(const std::vector<float>& x, double fs);

// Windowed-sinc resampler.
std::vector<float> resample(const std::vector<float>& input, double sourceRate, double destRate);

} // namespace rr
