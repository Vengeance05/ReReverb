// Standalone tests for the rereverb DSP core. Exit code 0 = all passed.
#include "rr/ir_reshaper.h"
#include "rr/m7_library.h"
#include "rr/septima_engine.h"
#include "rr/vista_engine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace rr;

namespace {
int failures = 0;
int checks = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what.c_str()); }
}
void checkNear(double v, double target, double tolFrac, const std::string& what) {
    const bool ok = std::isfinite(v) && std::abs(v - target) <= tolFrac * std::abs(target);
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s (got %.4f, want %.4f +/- %.0f%%)\n", what.c_str(), v, target, tolFrac * 100); }
    else std::printf("  ok: %s = %.3f (target %.3f)\n", what.c_str(), v, target);
}

// Broadband T30 (or T20) via Schroeder integration of a signal.
double measureRt(const std::vector<float>& x, double fs) {
    std::vector<double> edc(x.size());
    double acc = 0;
    for (std::size_t i = x.size(); i-- > 0;) { acc += static_cast<double>(x[i]) * x[i]; edc[i] = acc; }
    if (acc <= 0) return 0;
    std::size_t peak = 0;
    for (std::size_t i = 0; i < x.size(); ++i) if (std::abs(x[i]) > std::abs(x[peak])) peak = i;
    auto db = [&](std::size_t i) { return 10 * std::log10(std::max(edc[i], 1e-300) / edc[peak]); };
    std::size_t a = peak, b;
    while (a < x.size() && db(a) > -5) ++a;
    b = a;
    while (b < x.size() && db(b) > -35) ++b;
    if (b >= x.size()) { b = a; while (b < x.size() && db(b) > -25) ++b; }
    double sx = 0, sy = 0, sxx = 0, sxy = 0; double n = 0;
    for (std::size_t i = a; i < b; i += 16) {
        const double t = i / fs, y = db(i);
        sx += t; sy += y; sxx += t * t; sxy += t * y; n += 1;
    }
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return -60.0 / slope;
}

std::vector<float> decayingNoise(double fs, double rt, double seconds, std::uint32_t seed, double floorDb = -90) {
    Random r(seed);
    std::vector<float> x(static_cast<std::size_t>(seconds * fs));
    const double floor = std::pow(10.0, floorDb / 20.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = static_cast<float>(r.bipolar() * (std::exp(-kLn1000 * (i / fs) / rt) + floor));
    return x;
}

// Lowpass/highpass mix: lows decay with rtLow, highs with rtHigh.
std::vector<float> twoBandNoise(double fs, double rtLow, double rtHigh, double seconds) {
    auto lo = decayingNoise(fs, rtLow, seconds, 11);
    auto hi = decayingNoise(fs, rtHigh, seconds, 23);
    Biquad lp, hp;
    lp.setCoeffs(design::lowpass(fs, 300, 0.707));
    hp.setCoeffs(design::highpass(fs, 3000, 0.707));
    Biquad lp2 = lp, hp2 = hp;
    for (std::size_t i = 0; i < lo.size(); ++i) {
        lo[i] = lp2.process(lp.process(lo[i]));
        hi[i] = hp2.process(hp.process(hi[i]));
        lo[i] += hi[i];
    }
    return lo;
}

void testFilters() {
    std::printf("[filters]\n");
    const double fs = 48000;
    RollOff r;
    r.setup(fs, 1000, 24);
    double eIn = 0, eOut = 0;
    for (int i = 0; i < 48000; ++i) {
        const float x = static_cast<float>(std::sin(kTwoPi * 8000 * i / fs));
        const float y = r.process(x);
        if (i > 4800) { eIn += x * x; eOut += y * y; }
    }
    check(10 * std::log10(eOut / eIn) < -60, "24 dB/oct roll-off attenuates 3 octaves above cutoff by > 60 dB");
    MasterEq<5> eq;
    eq.prepare(fs);
    eq.setBand(0, {EqType::Peak, 1000, 0, 1});
    std::vector<float> l(512, 0.25f), rr(512, 0.25f);
    eq.process(l.data(), rr.data(), 512);
    check(std::abs(l[511] - 0.25f) < 1e-3f, "flat EQ is transparent");
}

void testEarlyReflections() {
    std::printf("[early reflections]\n");
    for (int p = 0; p < static_cast<int>(ErPattern::Count); ++p) {
        EarlyReflections er;
        ErSettings s;
        s.pattern = static_cast<ErPattern>(p);
        er.setSettings(s);
        er.prepare(48000, 256);
        check(er.tapCount(0, 0, false) > 8, std::string("pattern has taps: ") + erPatternName(s.pattern));
        std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
        l[0] = 1.0f;
        for (int i = 0; i < 48000; i += 256) er.process(&l[i], &r[i], &l[i], &r[i], std::min(256, 48000 - i));
        double e = 0;
        bool finite = true;
        for (std::size_t i = 0; i < l.size(); ++i) { e += l[i] * l[i] + r[i] * r[i]; finite &= std::isfinite(l[i]) && std::isfinite(r[i]); }
        check(finite && e > 0.05 && e < 3.0, std::string("bounded ER energy: ") + erPatternName(s.pattern));
    }
    // Crossfeed 0: left input must not reach the right output (before width mixing).
    EarlyReflections er;
    ErSettings s;
    s.crossfeed = 0.0f;
    s.width = 1.0f;
    er.setSettings(s);
    er.prepare(48000, 256);
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    l[0] = 1.0f;
    for (int i = 0; i < 24000; i += 256) er.process(&l[i], &r[i], &l[i], &r[i], std::min(256, 24000 - i));
    double eL = 0, eR = 0;
    for (std::size_t i = 0; i < l.size(); ++i) { eL += l[i] * l[i]; eR += r[i] * r[i]; }
    check(eR < 1e-6 * eL, "zero reflection crossfeed isolates channels");
    for (int set = 0; set < 32; ++set) {
        const auto st = erSetSettings(set);
        check(st.size >= 0.0f && st.size <= 1.0f, "ER set size in range");
    }
}

std::vector<float> lateImpulse(const LateSettings& ls, double fs, double seconds) {
    LateReverb late;
    late.setSettings(ls);
    late.prepare(fs, 256);
    const auto n = static_cast<std::size_t>(seconds * fs);
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    l[0] = 1.0f; r[0] = 1.0f;
    for (std::size_t i = 0; i < n; i += 256)
        late.process(&l[i], &r[i], &l[i], &r[i], static_cast<int>(std::min<std::size_t>(256, n - i)));
    for (std::size_t i = 0; i < n; ++i) l[i] += r[i];
    return l;
}

void testLateReverb() {
    std::printf("[late reverb]\n");
    const double fs = 48000;
    for (float rt : {0.8f, 2.5f, 6.0f}) {
        LateSettings ls;
        ls.rt60 = rt;
        ls.trebleMult = 1.0f;
        ls.preDelayMs = 0.0f;
        ls.bloom = 0.0f;
        auto ir = lateImpulse(ls, fs, rt * 1.4 + 0.5);
        checkNear(measureRt(ir, fs), rt, 0.12, "late RT60 at " + std::to_string(rt) + " s");
    }
    LateSettings x;
    x.rt60 = 45; x.size = 1; x.bloom = 1; x.modDepth = 1; x.chorus = ChorusMode::Moderate;
    x.lowBoostDb = 12; x.undulation = 1; x.echoLevel = 1; x.bassMult = 0.5f;
    auto ir = lateImpulse(x, fs, 6);
    bool finite = true;
    float peak = 0;
    for (float v : ir) { finite &= std::isfinite(v); peak = std::max(peak, std::abs(v)); }
    check(finite && peak < 4.0f, "extreme settings stay finite and bounded");
    // 44.1 / 96 kHz decay accuracy.
    for (double rate : {44100.0, 96000.0}) {
        LateSettings ls; ls.rt60 = 2.0f; ls.trebleMult = 1.0f; ls.preDelayMs = 0; ls.bloom = 0;
        checkNear(measureRt(lateImpulse(ls, rate, 3.2), rate), 2.0, 0.12, "late RT60 at fs " + std::to_string(static_cast<int>(rate)));
    }
}

void testVista() {
    std::printf("[vista engine]\n");
    VistaEngine e;
    VistaSettings s;
    s.mix = 0.0f;
    e.setSettings(s);
    e.prepare(48000, 512);
    std::vector<float> l(512), r(512);
    for (int i = 0; i < 512; ++i) { l[static_cast<std::size_t>(i)] = std::sin(i * 0.1f); r[static_cast<std::size_t>(i)] = std::cos(i * 0.1f); }
    auto l0 = l;
    e.process(l.data(), r.data(), 512);
    double err = 0;
    for (int i = 0; i < 512; ++i) err = std::max(err, static_cast<double>(std::abs(l[static_cast<std::size_t>(i)] - l0[static_cast<std::size_t>(i)])));
    check(err < 1e-5, "Vista mix 0 passes dry signal");
    // Odd block sizes larger than prepared maximum.
    VistaEngine e2;
    e2.prepare(48000, 64);
    std::vector<float> big(1000, 0.1f), big2(1000, 0.1f);
    e2.process(big.data(), big2.data(), 1000);
    bool finite = true;
    for (float v : big) finite &= std::isfinite(v);
    check(finite, "Vista handles blocks larger than prepared size");
}

void testReshapeExactAndShape() {
    std::printf("[reshape exactness / envelope shape]\n");
    const double fs = 48000;
    auto l = decayingNoise(fs, 1.5, 2.4, 7, -80);
    auto r = decayingNoise(fs, 1.5, 2.4, 8, -80);
    auto ir = makeStereo(l, r, fs);
    auto a = analyseIr(ir);
    // Untouched controls: the reshaped program must reproduce the capture.
    {
        ReshapeParams p;
        auto rs = reshapeIr(ir, a, p);
        double err = 0.0, ref = 0.0;
        for (std::size_t i = 0; i < l.size(); ++i) {
            double y = rs.late[0][i] + rs.vlf[0][i] + (i < rs.early[0].size() ? rs.early[0][i] : 0.0f);
            const double x = l[i] * a.normalisationGain;
            err += (y - x) * (y - x); ref += x * x;
        }
        check(10.0 * std::log10(err / ref + 1e-30) < -100.0, "default reshape is exact (< -100 dB)");
    }
    // A gated shape (flat, then a fast fall) keeps its shape when stretched 2x.
    {
        Random rng(99);
        std::vector<float> g(static_cast<std::size_t>(1.5 * fs));
        for (std::size_t i = 0; i < g.size(); ++i) {
            const double t = i / fs;
            const double env = t < 0.35 ? std::exp(-kLn1000 * t / 3.0) : std::exp(-kLn1000 * 0.35 / 3.0) * std::exp(-kLn1000 * (t - 0.35) / 0.15);
            g[i] = static_cast<float>(rng.bipolar() * (env + 1.0e-5));
        }
        auto gir = makeStereo(g, g, fs);
        auto ga = analyseIr(gir);
        ReshapeParams p;
        p.decaySeconds = 2.0f * ga.midRt;
        auto rs = reshapeIr(gir, ga, p);
        auto level = [&](double t0, double t1) {
            double e = 0.0; int n = 0;
            for (auto i = static_cast<std::size_t>(t0 * fs); i < static_cast<std::size_t>(t1 * fs) && i < rs.late[0].size(); ++i) {
                const double v = rs.late[0][i] + rs.vlf[0][i]; e += v * v; ++n;
            }
            return 10.0 * std::log10(e / std::max(1, n) + 1e-30);
        };
        const double plateau = level(0.15, 0.25), held = level(0.50, 0.58), after = level(0.95, 1.05);
        check(plateau - held < 6.0, "stretched gate holds its plateau (" + std::to_string(plateau - held) + " dB)");
        check(held - after > 25.0, "stretched gate still falls away (" + std::to_string(held - after) + " dB)");
    }
}

void testBandSplit() {
    std::printf("[band split]\n");
    auto x = decayingNoise(48000, 1.0, 1.0, 5);
    auto bands = splitBands(x, 48000);
    check(static_cast<int>(bands.size()) == kDecayBands, "band count");
    double maxErr = 0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        double s = 0;
        for (const auto& b : bands) s += b[i];
        maxErr = std::max(maxErr, std::abs(s - x[i]));
    }
    check(maxErr < 1e-5, "bands reconstruct the input exactly");
}

void testAnalysisAndReshape() {
    std::printf("[ir analysis / reshape]\n");
    const double fs = 48000;
    auto l = decayingNoise(fs, 2.0, 4.0, 1, -80);
    auto r = decayingNoise(fs, 2.0, 4.0, 2, -80);
    auto ir = makeStereo(l, r, fs);
    auto a = analyseIr(ir);
    checkNear(a.midRt, 2.0, 0.08, "measured mid RT of 2 s noise IR");
    checkNear(a.bandRt[1], 2.0, 0.15, "measured 127 Hz band RT");
    checkNear(a.bandRt[7], 2.0, 0.10, "measured 8 kHz band RT");

    auto sum = [](const ReshapedIr& rs, int c) {
        std::vector<float> y = rs.late[static_cast<std::size_t>(c)];
        for (std::size_t i = 0; i < rs.early[static_cast<std::size_t>(c)].size(); ++i) y[i] += rs.early[static_cast<std::size_t>(c)][i];
        return y;
    };
    for (float target : {1.0f, 4.0f, 9.0f}) {
        ReshapeParams p;
        p.decaySeconds = target;
        auto rs = reshapeIr(ir, a, p);
        // Re-analyse (late + early + VLF folded back into L->L).
        auto y = sum(rs, 0);
        for (std::size_t i = 0; i < rs.vlf[0].size(); ++i) y[i] += rs.vlf[0][i];
        auto again = analyseIr(makeStereo(y, sum(rs, 3), fs));
        checkNear(again.midRt, target, 0.12, "reshaped mid RT -> " + std::to_string(target) + " s");
    }
    // Frequency-dependent decay.
    {
        ReshapeParams p;
        p.lowMultiplier = 2.0f;
        p.lowCrossoverHz = 300.0f;
        p.highMultiplier = 0.5f;
        p.highCrossoverHz = 3000.0f;
        auto rs = reshapeIr(ir, a, p);
        auto y = sum(rs, 0);
        for (std::size_t i = 0; i < rs.vlf[0].size(); ++i) y[i] += rs.vlf[0][i];
        auto again = analyseIr(makeStereo(y, y, fs));
        checkNear(again.bandRt[1], 4.0, 0.15, "low multiplier x2 at 127 Hz");
        checkNear(again.bandRt[7], 1.0, 0.15, "high multiplier x0.5 at 8 kHz");
        checkNear(again.bandRt[4], 2.0, 0.12, "1 kHz RT unchanged by band multipliers");
        checkNear(again.bandRt[3], 2.0, 0.30, "500 Hz RT ~unchanged (adjacent-band leakage allowed)");
    }
    // Neutral reshape: early + late + VLF ~ normalised original.
    {
        ReshapeParams p;
        auto rs = reshapeIr(ir, a, p);
        double e0 = 0, e1 = 0;
        const auto n = std::min<std::size_t>(static_cast<std::size_t>(1.5 * fs), l.size());
        for (std::size_t i = 0; i < n; ++i) {
            const double orig = ir.ch[0][i] * a.normalisationGain;
            double y = rs.late[0][i] + rs.vlf[0][i];
            if (i < rs.early[0].size()) y += rs.early[0][i];
            e0 += orig * orig;
            e1 += (y - orig) * (y - orig);
        }
        check(10 * std::log10(e1 / e0) < -30, "neutral reshape reconstructs the first 1.5 s (error < -30 dB)");
    }
    // Two-band source: measure lows/highs separately.
    {
        auto x = twoBandNoise(fs, 3.0, 1.0, 5.0);
        auto a2 = analyseIr(makeStereo(x, x, fs));
        checkNear(a2.bandRt[1], 3.0, 0.15, "two-band IR: low band RT");
        checkNear(a2.bandRt[7], 1.0, 0.15, "two-band IR: high band RT");
    }
    // True-stereo layout + onset trimming.
    {
        std::vector<float> pre(4800, 0.0f);
        auto ll = pre; auto t = decayingNoise(fs, 1.0, 2.0, 7); ll.insert(ll.end(), t.begin(), t.end());
        auto ts = makeTrueStereo(ll, ll, ll, ll, fs);
        trimOnset(ts);
        check(ts.length() < ll.size() - 4000, "onset trimming removes leading silence");
    }
}

void testParser() {
    std::printf("[library parser]\n");
    auto p = parseIrFileStem("1 Halls 01 Large & Stage, 48K M to S");
    check(p.base == "1 Halls 01 Large & Stage" && p.tag == FileChannelTag::MonoToStereo && p.sampleRateTag == 48000,
          "Samplicity style M to S: '" + p.base + "'");
    p = parseIrFileStem("2 Plates 03 Bright Plate 44K S to S L");
    check(p.base == "2 Plates 03 Bright Plate" && p.tag == FileChannelTag::Left && p.sampleRateTag == 44100,
          "true-stereo left half: '" + p.base + "'");
    p = parseIrFileStem("Vocal Chamber_R");
    check(p.base == "Vocal Chamber" && p.tag == FileChannelTag::Right, "underscore R suffix");
    p = parseIrFileStem("Studio B Far");
    check(p.base == "Studio B Far" && p.tag == FileChannelTag::None, "plain name untouched");

    auto progs = groupIrFiles({
        "/lib/Halls/1 Halls 01 Large & Stage, 48K M to S.wav",
        "/lib/Halls/1 Halls 01 Large & Stage, 44K M to S.wav",
        "/lib/Halls/1 Halls 02 Medium Hall, 48K S to S L.wav",
        "/lib/Halls/1 Halls 02 Medium Hall, 48K S to S R.wav",
        "/lib/Rooms/3 Rooms 05 Studio B Far, 48K M to S.wav",
        "/lib/Misc/Something.wav",
        "/lib/Misc/readme.txt",
    });
    check(progs.size() == 4, "grouped into 4 programs (got " + std::to_string(progs.size()) + ")");
    if (progs.size() == 4) {
        check(progs[0].category == "Halls" && progs[0].number == 1 && progs[0].name == "Large & Stage", "category/number/name parsed");
        check(progs[0].files.size() == 1 && progs[0].files[0].find("48K") != std::string::npos, "48 kHz variant preferred");
        check(progs[1].format == IrFormat::TrueStereo && progs[1].files.size() == 2, "L/R pair becomes true stereo");
        check(progs[2].category == "Rooms", "rooms category");
        check(progs[3].category == "Misc", "fallback category from folder");
    }

    // Pointing at the whole unzipped Samplicity download (several sub-folders
    // holding the same programs) must give one 48 kHz entry per program.
    const std::string top = "/dl/Samplicity - Bricasti IRs version 2023-10/";
    progs = groupIrFiles({
        top + "Samplicity - Bricasti IRs version 2023-10, quad channel files, 48 Khz/1 Halls 01 Large Hall, 48K.wav",
        top + "Samplicity - Bricasti IRs version 2023-10, quad channel files, 44.1 Khz/1 Halls 01 Large Hall, 44K.wav",
        top + "Samplicity - Bricasti IRs version 2023-10, left-right files, 48 Khz/1 Halls 01 Large Hall, 48K L.wav",
        top + "Samplicity - Bricasti IRs version 2023-10, left-right files, 48 Khz/1 Halls 01 Large Hall, 48K R.wav",
        top + "x/1 Halls 01 Large Hall, 44K L.wav",
        top + "x/1 Halls 01 Large Hall, 44K R.wav",
        top + "Samplicity - Bricasti IRs version 2023-10, quad channel files, 48 Khz/2 Plates 06 Vocal Plate, 48K.wav",
        top + "Samplicity - Bricasti IRs version 2023-10, quad channel files, 44.1 Khz/2 Plates 06 Vocal Plate, 44K.wav",
    });
    check(progs.size() == 2, "whole download de-duplicates to 2 programs (got " + std::to_string(progs.size()) + ")");
    if (progs.size() == 2) {
        check(progs[0].key == "halls/01 large hall" && progs[1].key == "plates/06 vocal plate", "folder-independent keys");
        check(progs[0].sampleRateTag == 48000 && progs[1].sampleRateTag == 48000, "48 kHz chosen from full download");
    }
}

void testSeptimaEngine() {
    std::printf("[septima engine]\n");
    SeptimaEngine e;
    SeptimaSettings s;
    s.mix = 0.0f;
    e.setSettings(s);
    e.prepare(48000, 256);
    std::vector<float> l(256, 0.3f), r(256, -0.3f), lateL(256), lateR(256), vlf(256), z(256, 0.0f);
    e.makeFeeds(l.data(), r.data(), lateL.data(), lateR.data(), vlf.data(), 256);
    e.finish(l.data(), r.data(), z.data(), z.data(), z.data(), z.data(), z.data(), z.data(), 256);
    check(std::abs(l[255] - 0.3f) < 1e-4f && std::abs(r[255] + 0.3f) < 1e-4f, "Septima mix 0 passes dry");

    // Pre-delay applies to the late feed only.
    SeptimaEngine e2;
    SeptimaSettings s2;
    s2.preDelayMs = 10.0f;
    e2.setSettings(s2);
    e2.prepare(48000, 1024);
    std::vector<float> a(1024, 0.0f), b(1024, 0.0f), fl(1024), fr(1024), fv(1024);
    a[0] = 1.0f;
    e2.makeFeeds(a.data(), b.data(), fl.data(), fr.data(), fv.data(), 1024);
    std::size_t peak = 0;
    for (std::size_t i = 0; i < fl.size(); ++i) if (std::abs(fl[i]) > std::abs(fl[peak])) peak = i;
    check(std::abs(static_cast<int>(peak) - 480) <= 2, "10 ms pre-delay on late feed (peak at " + std::to_string(peak) + ")");
}

} // namespace

int main() {
    testFilters();
    testEarlyReflections();
    testLateReverb();
    testVista();
    testBandSplit();
    testAnalysisAndReshape();
    testReshapeExactAndShape();
    testParser();
    testSeptimaEngine();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
