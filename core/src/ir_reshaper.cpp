#include "rr/ir_reshaper.h"

#include "rr/dsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace rr {
namespace {

// Double-precision 2nd-order lowpass section (TDF2).
struct BiquadD {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    static BiquadD lowpass(double fs, double fc, double q = 0.7071067811865476) {
        const double w = kTwoPi * std::clamp(fc, 5.0, fs * 0.45) / fs;
        const double cw = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1 + alpha;
        return {(1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0};
    }
    double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Zero-phase 8th-order Butterworth lowpass (|H|^2, run forwards and backwards).
// Because |H_lp|^2 + |H_hp|^2 = 1 for a Butterworth pair, "x - lowpass(x)" is
// exactly the matching zero-phase Butterworth highpass: the band split is
// perfectly reconstructing AND steep on both sides (-48 dB one octave away),
// so slowly decaying neighbours cannot leak into a band's decay measurement.
void filtfiltLowpass(const std::vector<float>& in, std::vector<float>& out, double fs, double fc) {
    static constexpr std::array<double, 4> kQ{0.50979558, 0.60134489, 0.89997622, 2.56291545};
    out.resize(in.size());
    std::array<BiquadD, 4> f;
    auto init = [&] { for (std::size_t k = 0; k < 4; ++k) f[k] = BiquadD::lowpass(fs, fc, kQ[k]); };
    init();
    for (std::size_t i = 0; i < in.size(); ++i) {
        double v = in[i];
        for (auto& s : f) v = s.process(v);
        out[i] = static_cast<float>(v);
    }
    init();
    for (std::size_t i = in.size(); i-- > 0;) {
        double v = out[i];
        for (auto& s : f) v = s.process(v);
        out[i] = static_cast<float>(v);
    }
}

bool allZero(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return x == 0.0f; });
}

double energy(const std::vector<float>& v) {
    double e = 0.0;
    for (float x : v) e += static_cast<double>(x) * x;
    return e;
}

constexpr double kWindowSeconds = 0.01;

// startSeconds > 0 fits the decay only from that time on (the late field),
// so a strong direct sound / early burst cannot bias the slope.
BandDecay measureBand(const std::vector<float>& band, double fs, double startSeconds = 0.0) {
    BandDecay out;
    const auto win = std::max<std::size_t>(1, static_cast<std::size_t>(kWindowSeconds * fs));
    const std::size_t windows = band.size() / win;
    if (windows < 8) return out;
    std::vector<double> e(windows, 0.0);
    for (std::size_t k = 0; k < windows; ++k) {
        double s = 0.0;
        for (std::size_t i = k * win; i < (k + 1) * win; ++i) s += static_cast<double>(band[i]) * band[i];
        e[k] = s / static_cast<double>(win);
    }
    const auto peakIt = std::max_element(e.begin(), e.end());
    const double peak = *peakIt;
    if (peak <= 1.0e-30) return out;
    const std::size_t kp = std::max(static_cast<std::size_t>(peakIt - e.begin()),
                                    std::min(windows - 8, static_cast<std::size_t>(startSeconds / kWindowSeconds)));
    // Noise floor: the larger of the final 5% (classic estimate) and the median
    // of the last 40% excluding that final stretch. Captures are often faded out
    // at the very end, which would otherwise hide a steady hiss floor and make
    // it look like an extremely slow decay.
    const std::size_t tailWindows = std::max<std::size_t>(3, windows / 20);
    double floor = 0.0;
    for (std::size_t k = windows - tailWindows; k < windows; ++k) floor += e[k];
    floor /= static_cast<double>(tailWindows);
    {
        const std::size_t a = windows - std::max<std::size_t>(tailWindows + 4, windows * 2 / 5);
        std::vector<double> late(e.begin() + static_cast<std::ptrdiff_t>(a), e.end() - static_cast<std::ptrdiff_t>(tailWindows));
        if (!late.empty()) {
            std::nth_element(late.begin(), late.begin() + static_cast<std::ptrdiff_t>(late.size() / 2), late.end());
            floor = std::max(floor, late[late.size() / 2]);
        }
    }
    floor = std::max(floor, peak * 1.0e-12); // never below -120 dB re peak

    std::size_t kt = windows - 1;
    for (std::size_t k = kp; k < windows; ++k) {
        if (e[k] < floor * 2.0) { kt = k; break; }
    }
    // Schroeder backward integration with floor compensation.
    std::vector<double> edc(kt + 1, 0.0);
    double acc = 0.0;
    for (std::size_t k = kt + 1; k-- > 0;) {
        acc += std::max(0.0, e[k] - floor);
        edc[k] = acc;
    }
    if (edc[kp] <= 0.0) return out;
    auto edcDb = [&](std::size_t k) { return 10.0 * std::log10(std::max(edc[k], 1.0e-300) / edc[kp]); };
    std::size_t k5 = kp, kEnd = kp;
    while (k5 <= kt && edcDb(k5) > -5.0) ++k5;
    const double target = edcDb(kt > 0 ? kt - 1 : kt) < -35.0 ? -35.0 : -25.0;
    kEnd = k5;
    while (kEnd <= kt && edcDb(kEnd) > target) ++kEnd;
    if (kEnd > kt) kEnd = kt;
    if (kEnd < k5 + 3) {
        // Very short decay: fall back to a peak-to-(-20 dB) estimate.
        std::size_t k20 = kp;
        while (k20 < windows && 10.0 * std::log10(e[k20] / peak) > -20.0) ++k20;
        out.rt60 = static_cast<float>(std::max(0.05, 3.0 * (k20 - kp) * kWindowSeconds));
    } else {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const double n = static_cast<double>(kEnd - k5 + 1);
        for (std::size_t k = k5; k <= kEnd; ++k) {
            const double x = static_cast<double>(k) * kWindowSeconds, y = edcDb(k);
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double slope = (n * sxy - sx * sy) / std::max(1.0e-12, n * sxx - sx * sx);
        out.rt60 = slope < -1.0e-6 ? static_cast<float>(std::clamp(-60.0 / slope, 0.05, 60.0)) : 0.0f;
    }

    const double floorDb = 10.0 * std::log10(floor), peakDb = 10.0 * std::log10(peak);
    const double extendDb = std::max(floorDb + 15.0, peakDb - 55.0);
    std::size_t kx = kp;
    for (std::size_t k = windows; k-- > kp;) {
        if (10.0 * std::log10(std::max(e[k], 1.0e-300)) >= extendDb) { kx = k; break; }
    }
    double lvl = 0.0; int cnt = 0;
    for (std::size_t k = (kx > 0 ? kx - 1 : kx); k <= std::min(windows - 1, kx + 1); ++k) { lvl += e[k]; ++cnt; }
    out.extendSeconds = static_cast<float>((static_cast<double>(kx) + 0.5) * kWindowSeconds);
    out.levelAtExtend = static_cast<float>(std::sqrt(lvl / std::max(1, cnt)));
    return out;
}

float freqMultiplierImpl(float f, const ReshapeParams& p) {
    const float wl = std::clamp(0.5f - std::log2(f / std::max(10.0f, p.lowCrossoverHz)), 0.0f, 1.0f);
    const float wh = std::clamp(0.5f + std::log2(f / std::max(10.0f, p.highCrossoverHz)), 0.0f, 1.0f);
    return std::exp(wl * std::log(std::max(0.05f, p.lowMultiplier))
                    + wh * std::log(std::max(0.05f, p.highMultiplier)));
}

} // namespace

float decayMultiplierAt(float f, const ReshapeParams& p) noexcept { return freqMultiplierImpl(f, p); }

float decayBandCentre(int band) noexcept {
    if (band <= 0) return 63.0f;
    if (band >= kDecayBands - 1) return 14000.0f;
    return std::sqrt(kDecayBandEdges[static_cast<std::size_t>(band - 1)]
                     * kDecayBandEdges[static_cast<std::size_t>(band)]);
}

TrueStereoIr makeMonoToStereo(std::vector<float> left, std::vector<float> right, double fs) {
    TrueStereoIr ir;
    ir.sampleRate = fs;
    ir.format = IrFormat::MonoToStereo;
    const float g = 0.70710678f;
    for (auto& x : left) x *= g;
    for (auto& x : right) x *= g;
    ir.ch = {left, right, left, right};
    return ir;
}

TrueStereoIr makeStereo(std::vector<float> left, std::vector<float> right, double fs) {
    TrueStereoIr ir;
    ir.sampleRate = fs;
    ir.format = IrFormat::Stereo;
    const auto n = std::max(left.size(), right.size());
    left.resize(n); right.resize(n);
    ir.ch = {std::move(left), std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f), std::move(right)};
    return ir;
}

TrueStereoIr makeTrueStereo(std::vector<float> ll, std::vector<float> lr, std::vector<float> rl,
                            std::vector<float> rr, double fs) {
    TrueStereoIr ir;
    ir.sampleRate = fs;
    ir.format = IrFormat::TrueStereo;
    const auto n = std::max({ll.size(), lr.size(), rl.size(), rr.size()});
    for (auto* v : {&ll, &lr, &rl, &rr}) v->resize(n);
    ir.ch = {std::move(ll), std::move(lr), std::move(rl), std::move(rr)};
    return ir;
}

void trimOnset(TrueStereoIr& ir, float thresholdDb) {
    float peak = 0.0f;
    for (const auto& c : ir.ch) for (float x : c) peak = std::max(peak, std::abs(x));
    if (peak <= 0.0f) return;
    const float thr = peak * dbToGain(thresholdDb);
    std::size_t first = ir.length();
    for (const auto& c : ir.ch) {
        for (std::size_t i = 0; i < c.size(); ++i)
            if (std::abs(c[i]) > thr) { first = std::min(first, i); break; }
    }
    const auto margin = static_cast<std::size_t>(0.001 * ir.sampleRate);
    first = first > margin ? first - margin : 0;
    if (first == 0) return;
    for (auto& c : ir.ch) c.erase(c.begin(), c.begin() + static_cast<std::ptrdiff_t>(std::min(first, c.size())));
}

std::vector<std::vector<float>> splitBands(const std::vector<float>& x, double fs) {
    std::vector<std::vector<float>> bands;
    std::vector<float> rest = x, low;
    for (int b = 0; b < kDecayBands - 1; ++b) {
        filtfiltLowpass(rest, low, fs, kDecayBandEdges[static_cast<std::size_t>(b)]);
        for (std::size_t i = 0; i < rest.size(); ++i) rest[i] -= low[i];
        bands.push_back(low);
    }
    bands.push_back(rest);
    return bands;
}

IrAnalysis analyseIr(const TrueStereoIr& ir) {
    IrAnalysis a;
    a.sampleRate = ir.sampleRate;
    std::array<std::array<double, kDecayBands>, 4> bandEnergy{};
    auto work = [&](int c) {
        const auto& x = ir.ch[static_cast<std::size_t>(c)];
        if (allZero(x)) return;
        std::vector<float> rest = x, low;
        for (int b = 0; b < kDecayBands; ++b) {
            const std::vector<float>* band = &rest;
            if (b < kDecayBands - 1) {
                filtfiltLowpass(rest, low, ir.sampleRate, kDecayBandEdges[static_cast<std::size_t>(b)]);
                for (std::size_t i = 0; i < rest.size(); ++i) rest[i] -= low[i];
                band = &low;
            }
            a.channel[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)] = measureBand(*band, ir.sampleRate);
            bandEnergy[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)] = energy(*band);
        }
    };
    std::array<std::thread, 4> threads;
    for (int c = 0; c < 4; ++c) threads[static_cast<std::size_t>(c)] = std::thread(work, c);
    for (auto& t : threads) t.join();

    for (int b = 0; b < kDecayBands; ++b) {
        double num = 0, den = 0;
        for (int c = 0; c < 4; ++c) {
            const auto& d = a.channel[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)];
            const double w = bandEnergy[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)];
            if (d.rt60 > 0.0f) { num += d.rt60 * w; den += w; }
        }
        a.bandRt[static_cast<std::size_t>(b)] = den > 0 ? static_cast<float>(num / den) : 0.0f;
    }
    // Fill unmeasurable bands from neighbours.
    for (int pass = 0; pass < kDecayBands; ++pass) {
        for (int b = 0; b < kDecayBands; ++b) {
            auto& v = a.bandRt[static_cast<std::size_t>(b)];
            if (v > 0.0f) continue;
            const float l = b > 0 ? a.bandRt[static_cast<std::size_t>(b - 1)] : 0.0f;
            const float r = b < kDecayBands - 1 ? a.bandRt[static_cast<std::size_t>(b + 1)] : 0.0f;
            if (l > 0 && r > 0) v = 0.5f * (l + r);
            else if (l > 0) v = l;
            else if (r > 0) v = r;
        }
    }
    for (auto& v : a.bandRt) if (v <= 0.0f) v = 1.0f;
    a.midRt = 0.5f * (a.bandRt[3] + a.bandRt[4]);

    double total = 0.0;
    for (const auto& c : ir.ch) total += energy(c);
    // Target: each input produces wet energy 0.25 for a unit impulse.
    a.normalisationGain = total > 0.0 ? static_cast<float>(std::sqrt(0.25 / (total / 2.0))) : 1.0f;
    const auto earlyN = std::min(ir.length(), static_cast<std::size_t>(0.07 * ir.sampleRate));
    double early = 0.0;
    for (const auto& c : ir.ch)
        for (std::size_t i = 0; i < earlyN; ++i) early += static_cast<double>(c[i]) * c[i];
    a.earlyEnergy = static_cast<float>(early / 2.0) * a.normalisationGain * a.normalisationGain;
    return a;
}

namespace {
constexpr int kReshapeBands = 17;
constexpr std::array<double, kReshapeBands - 1> kReshapeEdges{
    63.0, 90.0, 125.0, 180.0, 250.0, 355.0, 500.0, 710.0, 1000.0, 1400.0,
    2000.0, 2800.0, 4000.0, 5600.0, 8000.0, 11200.0};

// Analysis-band RT at an arbitrary frequency (log-frequency interpolation).
double interpolatedBandRt(const IrAnalysis& a, double f) {
    double prevF = decayBandCentre(0), prevRt = a.bandRt[0];
    if (f <= prevF) return prevRt;
    for (int b = 1; b < kDecayBands; ++b) {
        const double cf = decayBandCentre(b), rt = a.bandRt[static_cast<std::size_t>(b)];
        if (f <= cf) {
            const double w = std::log(f / prevF) / std::log(cf / prevF);
            return prevRt + w * (rt - prevRt);
        }
        prevF = cf; prevRt = rt;
    }
    return prevRt;
}
} // namespace

ReshapedIr reshapeIr(const TrueStereoIr& ir, const IrAnalysis& analysis, const ReshapeParams& p) {
    ReshapedIr out;
    const double fs = ir.sampleRate;
    out.sampleRate = fs;
    const float ratio = p.decaySeconds > 0.0f && analysis.midRt > 0.0f ? p.decaySeconds / analysis.midRt : 1.0f;
    float maxRt = 0.0f;
    for (int b = 0; b < kDecayBands; ++b) {
        out.targetRt[static_cast<std::size_t>(b)] = analysis.bandRt[static_cast<std::size_t>(b)] * ratio
            * freqMultiplierImpl(decayBandCentre(b), p);
        maxRt = std::max(maxRt, out.targetRt[static_cast<std::size_t>(b)]);
    }
    const double t0 = std::max(0.0, static_cast<double>(p.earlyEndSeconds) - 0.5 * p.earlyCrossfadeSeconds);
    // Long enough for ~-75 dB in the slowest band; when that is close to the
    // capture's own length keep the whole capture so an unmodified program is exact.
    double lenSeconds = t0 + 1.25 * maxRt;
    const double captureSeconds = static_cast<double>(ir.length()) / fs;
    if (captureSeconds <= t0 + 1.6 * maxRt) lenSeconds = std::max(lenSeconds, captureSeconds);
    const auto outLen = static_cast<std::size_t>(std::clamp(lenSeconds, 0.3, static_cast<double>(p.maxSeconds)) * fs);
    const double xfade = 0.06;
    const auto earlyLen = static_cast<std::size_t>((p.earlyEndSeconds + 0.5 * p.earlyCrossfadeSeconds) * fs) + 1;

    std::array<std::vector<float>, 4> full;
    auto work = [&](int c) {
        const auto sc = static_cast<std::size_t>(c);
        const auto& x = ir.ch[sc];
        auto& y = full[sc];
        y.assign(outLen, 0.0f);
        if (allZero(x)) return;

        // Noise for tail synthesis, only from the earliest hand-over point.
        // Hand-over points are chosen per band below and can be as early as the
        // late section starts, so the noise covers the whole output.
        const std::size_t noiseStart = 0;
        std::vector<float> noiseRest(outLen > noiseStart ? outLen - noiseStart : 0), noiseLow;
        Random rng(p.seed + 977u * static_cast<std::uint32_t>(c + 1));
        for (auto& v : noiseRest) v = rng.bipolar();

        // The reshape itself runs on half-octave bands (finer than the analysis
        // bands) so the spectral tilt inside each band stays small: otherwise the
        // synthesised continuation would be flatter (brighter) than the capture
        // it continues, because highs decay faster than lows within one band.
        std::vector<float> rest = x, low;
        bool lengthenedAny = false;
        for (int b = 0; b < kReshapeBands; ++b) {
            const auto sb = static_cast<std::size_t>(b);
            const std::vector<float>* band = &rest;
            const std::vector<float>* noise = &noiseRest;
            if (b < kReshapeBands - 1) {
                filtfiltLowpass(rest, low, fs, kReshapeEdges[sb]);
                for (std::size_t i = 0; i < rest.size(); ++i) rest[i] -= low[i];
                band = &low;
                std::vector<float> nl;
                filtfiltLowpass(noiseRest, nl, fs, kReshapeEdges[sb]);
                for (std::size_t i = 0; i < noiseRest.size(); ++i) noiseRest[i] -= nl[i];
                noiseLow = std::move(nl);
                noise = &noiseLow;
            }
            const double centre = b == 0 ? 0.7 * kReshapeEdges[0]
                : b == kReshapeBands - 1 ? 1.25 * kReshapeEdges[sb - 1]
                : std::sqrt(kReshapeEdges[sb - 1] * kReshapeEdges[sb]);
            const BandDecay d = measureBand(*band, fs, p.earlyEndSeconds);
            const double coarseRt = interpolatedBandRt(analysis, centre);
            const double rtMeasured = d.rt60 > 0.0f ? d.rt60 : coarseRt;
            const double rtTarget = std::max(0.05, rtMeasured * ratio * freqMultiplierImpl(static_cast<float>(centre), p));
            double noiseRms = 0.0;
            if (!noise->empty()) noiseRms = std::sqrt(energy(*noise) / static_cast<double>(noise->size()));
            const double k = rtTarget / rtMeasured;   // time-stretch factor for this band
            if (d.rt60 <= 0.0f || std::abs(k - 1.0) < 1.0e-3) {
                // Unmeasurable (silent) band, or nothing to change: keep it exactly.
                for (std::size_t i = 0; i < std::min(outLen, band->size()); ++i) y[i] += (*band)[i];
                continue;
            }
            const double ext = d.extendSeconds > 0.0f ? d.extendSeconds : static_cast<double>(x.size()) / fs;

            // Stretch origin: the end of the early section (the early IR is never altered).
            const double tS = p.earlyEndSeconds;
            // Smoothed envelope of the band (dB mean square, 10 ms grid, 50 ms average),
            // continued past the hand-over point along the fitted decay.
            constexpr double kHop = 0.01;
            const auto hop = static_cast<std::size_t>(kHop * fs);
            const std::size_t nOut = outLen / hop + 2;
            const std::size_t nCap = band->size() / hop;
            const double slopeDb = 60.0 / rtMeasured;   // dB per second, measured
            std::vector<double> em(std::max(nOut, nCap + 1));
            {
                std::vector<double> e(nCap);
                for (std::size_t w = 0; w < nCap; ++w) {
                    double acc = 0.0;
                    for (std::size_t i = w * hop; i < (w + 1) * hop; ++i) acc += (*band)[i] * (*band)[i];
                    e[w] = acc / static_cast<double>(hop);
                }
                const std::size_t extW = std::min(nCap > 0 ? nCap - 1 : 0, static_cast<std::size_t>(ext / kHop));
                // Smoothing never reaches back across the stretch origin, so the
                // direct sound / early burst cannot leak into the late envelope.
                const auto w0 = static_cast<std::size_t>(tS / kHop);
                for (std::size_t w = 0; w < em.size(); ++w) {
                    if (w <= extW && nCap > 0) {
                        double acc = 0.0; int cnt = 0;
                        const std::size_t lo = w >= w0 ? std::max(w0, w >= 2 ? w - 2 : 0) : (w >= 2 ? w - 2 : 0);
                        for (std::size_t j = lo; j <= std::min(nCap - 1, w + 2); ++j) { acc += e[j]; ++cnt; }
                        em[w] = 10.0 * std::log10(std::max(acc / cnt, 1.0e-30));
                    } else {
                        em[w] = em[extW] - slopeDb * static_cast<double>(w - extW) * kHop;
                    }
                }
            }
            auto emAt = [&](double t) {
                const double wf = t / kHop - 0.5;
                if (wf <= 0.0) return em[0];
                const auto w = static_cast<std::size_t>(wf);
                if (w + 1 >= em.size()) return em.back() - slopeDb * (wf - static_cast<double>(em.size() - 1)) * kHop;
                const double f = wf - static_cast<double>(w);
                return em[w] + f * (em[w + 1] - em[w]);
            };

            // Target envelope: the captured envelope stretched in time by k from the
            // end of the early section. For an exponential decay this is exactly an RT
            // change; gated, double-slope or build-up shapes keep their character.
            std::vector<double> gain(nOut, 1.0), synthAmp(nOut, 0.0);
            double handover = 1.0e9;
            for (std::size_t w = 0; w < nOut; ++w) {
                const double t = (static_cast<double>(w) + 0.5) * kHop;
                if (t < tS) continue;
                const double target = emAt(tS + (t - tS) / k);
                const double gDb = std::clamp(target - em[w], -150.0, 60.0);
                gain[w] = std::pow(10.0, gDb / 20.0);
                synthAmp[w] = std::pow(10.0, target / 20.0);
                // Lengthening amplifies whatever in the capture is not part of the
                // decay (its noise floor above all): hand over to a synthesised
                // continuation before that gain exceeds 12 dB, or at the floor.
                if (k > 1.0 && handover > 1.0e8 && (gDb > 12.0 || t >= ext)) handover = t;
            }
            const bool synthesise = k > 1.0 && handover < 1.0e8 && noiseRms > 0.0;
            lengthenedAny = lengthenedAny || synthesise;
            const double xa = handover - 0.5 * xfade, xb = handover + 0.5 * xfade;
            const auto ia = static_cast<std::size_t>(std::max(0.0, xa) * fs);
            for (std::size_t i = 0; i < outLen; ++i) {
                const double wf = std::max(0.0, static_cast<double>(i) / static_cast<double>(hop) - 0.5);
                const auto w = std::min(nOut - 2, static_cast<std::size_t>(wf));
                const double f = std::min(1.0, wf - static_cast<double>(w));
                double v = (i < band->size() ? (*band)[i] : 0.0) * (gain[w] + f * (gain[w + 1] - gain[w]));
                if (synthesise && i >= ia && i >= noiseStart) {
                    const double t = static_cast<double>(i) / fs;
                    const double xw = t >= xb ? 1.0 : 0.5 - 0.5 * std::cos(kPi * (t - xa) / xfade);
                    const double amp = (synthAmp[w] + f * (synthAmp[w + 1] - synthAmp[w])) / noiseRms;
                    v = v * (1.0 - xw) + (*noise)[i - noiseStart] * amp * xw;
                }
                y[i] += static_cast<float>(v);
            }
        }
        // Gentle fade over the final 5% to guarantee a clean end (not needed when
        // the whole capture fits, which keeps an untouched program exact).
        const std::size_t fade = outLen >= x.size() && !lengthenedAny ? 0 : std::max<std::size_t>(1, outLen / 20);
        for (std::size_t i = 0; i < fade; ++i)
            y[outLen - 1 - i] *= static_cast<float>(0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / fade));
        for (auto& v : y) v *= analysis.normalisationGain;
    };
    {
        std::array<std::thread, 4> threads;
        for (int c = 0; c < 4; ++c) threads[static_cast<std::size_t>(c)] = std::thread(work, c);
        for (auto& t : threads) t.join();
    }

    // Early / late split with a raised-cosine crossover.
    std::array<std::vector<float>, 4> lowLate;
    for (int c = 0; c < 4; ++c) {
        const auto sc = static_cast<std::size_t>(c);
        auto& y = full[sc];
        out.early[sc].assign(std::min(earlyLen, y.size()), 0.0f);
        out.late[sc] = y;
        const double xa = p.earlyEndSeconds - 0.5 * p.earlyCrossfadeSeconds;
        for (std::size_t i = 0; i < out.early[sc].size(); ++i) {
            const double t = static_cast<double>(i) / fs;
            const double wEarly = t <= xa ? 1.0
                : std::max(0.0, 0.5 + 0.5 * std::cos(kPi * (t - xa) / p.earlyCrossfadeSeconds));
            out.early[sc][i] = static_cast<float>(y[i] * wEarly);
            out.late[sc][i] = static_cast<float>(y[i] * (1.0 - wEarly));
        }
        filtfiltLowpass(out.late[sc], lowLate[sc], fs, p.vlfHz);
        for (std::size_t i = 0; i < out.late[sc].size(); ++i) out.late[sc][i] -= lowLate[sc][i];
    }
    for (int c = 0; c < 4; ++c) out.vlf[static_cast<std::size_t>(c)] = std::move(lowLate[static_cast<std::size_t>(c)]);
    return out;
}

std::vector<float> resample(const std::vector<float>& input, double sourceRate, double destRate) {
    if (input.empty() || sourceRate <= 0.0 || destRate <= 0.0) return input;
    if (std::abs(sourceRate - destRate) < 1.0e-6) return input;
    // Kaiser-windowed sinc (beta 10, ~100 dB stopband), 2 x 48 taps at the
    // lower of the two rates, from a 1024-phase table with linear interpolation
    // between phases. The passband runs to 95% of the lower Nyquist.
    constexpr int kRadius = 48;
    constexpr int kPhases = 1024;
    constexpr double kBeta = 10.0;
    const double scale = std::min(1.0, destRate / sourceRate);   // < 1 when downsampling
    const double cutoff = 0.95 * scale;
    const int radius = static_cast<int>(std::ceil(kRadius / scale));
    auto besselI0 = [](double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    };
    const double i0Beta = besselI0(kBeta);
    // table[ph][j]: kernel at distance (j - radius + 1) - ph / kPhases
    const int taps = 2 * radius;
    std::vector<float> table(static_cast<std::size_t>((kPhases + 1) * taps));
    for (int ph = 0; ph <= kPhases; ++ph) {
        for (int j = 0; j < taps; ++j) {
            const double dist = static_cast<double>(j - radius + 1) - static_cast<double>(ph) / kPhases;
            const double arg = cutoff * dist;
            const double sinc = std::abs(arg) < 1e-12 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
            const double r = dist / static_cast<double>(radius);
            const double win = std::abs(r) >= 1.0 ? 0.0 : besselI0(kBeta * std::sqrt(1.0 - r * r)) / i0Beta;
            table[static_cast<std::size_t>(ph * taps + j)] = static_cast<float>(cutoff * sinc * win);
        }
    }
    const auto outputSize = static_cast<std::size_t>(std::ceil(static_cast<double>(input.size()) * destRate / sourceRate));
    std::vector<float> output(outputSize);
    const double step = sourceRate / destRate;
    const long n = static_cast<long>(input.size());
    for (std::size_t i = 0; i < outputSize; ++i) {
        const double src = static_cast<double>(i) * step;
        const auto base = static_cast<long>(std::floor(src));
        const double frac = (src - static_cast<double>(base)) * kPhases;
        const auto ph = static_cast<int>(frac);
        const float pf = static_cast<float>(frac - ph);
        const float* t0 = &table[static_cast<std::size_t>(ph * taps)];
        const float* t1 = t0 + taps;
        double sum = 0.0;
        // sample index idx = base + (j - radius + 1); distance = src - idx
        const long first = base - radius + 1;
        const int jStart = static_cast<int>(std::max(0L, -first));
        const int jEnd = static_cast<int>(std::min(static_cast<long>(taps), n - first));
        for (int j = jStart; j < jEnd; ++j) {
            // table distance is (j - radius + 1) - ph/kPhases; we need src - idx = frac/kPhases - (j - radius + 1),
            // i.e. the mirrored tap, which the symmetric kernel makes equal.
            const float k = t0[j] + pf * (t1[j] - t0[j]);
            sum += static_cast<double>(input[static_cast<std::size_t>(first + j)]) * k;
        }
        output[i] = static_cast<float>(sum);
    }
    return output;
}

} // namespace rr
