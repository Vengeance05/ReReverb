// Session-compatibility guard for Septima's DSP (no JUCE, no IR files needed):
//   rr_golden_core check  tests/golden/septima_core.txt
//   rr_golden_core update tests/golden/septima_core.txt   (intended changes only)
// Fingerprints the IR reshaper (decay / band multipliers on a synthetic IR) and
// the engine (feeds, early reflections, pre-delay, delay, roll-offs, EQ) as RMS
// levels in dB per time window. Any change to how existing settings sound fails.
#include "rr/ir_reshaper.h"
#include "rr/septima_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace rr;

namespace {
constexpr double kFs = 48000.0;
constexpr float kFloorDb = -100.0f, kToleranceDb = 0.1f;

struct Lcg {
    std::uint32_t s;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 8388608.0f - 1.0f; }
};

std::vector<float> windows(const std::vector<float>& x, int win, int count) {
    std::vector<float> out;
    for (int w = 0; w < count; ++w) {
        double e = 0.0;
        for (int i = w * win; i < (w + 1) * win; ++i)
            if (i < static_cast<int>(x.size())) e += static_cast<double>(x[static_cast<std::size_t>(i)]) * x[static_cast<std::size_t>(i)];
        out.push_back(std::max(kFloorDb, static_cast<float>(10.0 * std::log10(e / win + 1e-30))));
    }
    return out;
}

// A 2.5 s synthetic hall: dense noise, brighter early, faster HF decay.
TrueStereoIr syntheticIr() {
    const int n = static_cast<int>(2.5 * kFs);
    std::array<std::vector<float>, 4> ch;
    for (int c = 0; c < 4; ++c) {
        Lcg rng{static_cast<std::uint32_t>(1234 + 77 * c)};
        ch[static_cast<std::size_t>(c)].resize(static_cast<std::size_t>(n));
        float lp = 0.0f;
        for (int i = 0; i < n; ++i) {
            const double t = i / kFs;
            const float white = rng.next();
            const float a = static_cast<float>(std::min(0.95, 0.2 + 0.3 * t));   // darker with time
            lp = a * lp + (1.0f - a) * white;
            const float env = static_cast<float>(std::exp(-6.9 * t / 1.8)) * (t < 0.003 ? static_cast<float>(t / 0.003) : 1.0f);
            ch[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = 0.3f * env * (0.5f * white + lp);
        }
    }
    return makeTrueStereo(ch[0], ch[1], ch[2], ch[3], kFs);
}

std::map<std::string, std::vector<float>> run() {
    std::map<std::string, std::vector<float>> r;
    const auto ir = syntheticIr();
    const auto an = analyseIr(ir);
    struct P { const char* name; float decay, lowMul, highMul; };
    const P sets[] = {{"reshape sampled", 0.0f, 1.0f, 1.0f}, {"reshape 4s", 4.0f, 1.0f, 1.0f},
                      {"reshape 1s dark", 1.0f, 1.0f, 0.5f}, {"reshape 2.5s warm", 2.5f, 2.0f, 1.5f}};
    for (const auto& p : sets) {
        ReshapeParams rp;
        rp.decaySeconds = p.decay;
        rp.lowMultiplier = p.lowMul;
        rp.highMultiplier = p.highMul;
        const auto out = reshapeIr(ir, an, rp);
        std::vector<float> fp;
        for (const auto* v : {&out.early[0], &out.late[0], &out.late[3], &out.vlf[0]}) {
            const auto w = windows(*v, 9600, 25);   // 200 ms windows, 5 s
            fp.insert(fp.end(), w.begin(), w.end());
        }
        r[p.name] = fp;
    }

    // Engine: dry input in, late/VLF feeds stand in for the convolution outputs.
    struct E { const char* name; SeptimaSettings s; };
    std::vector<E> engines;
    engines.push_back({"engine default", SeptimaSettings{}});
    {
        SeptimaSettings s; s.erSet = 6; s.earlyLate = 0.35f; s.preDelayMs = 40.0f; s.delayLevel = 0.3f; s.delayMs = 180.0f;
        s.earlyRolloffHz = 7000.0f; s.lateRolloffHz = 5000.0f; s.vlfLevel = 1.5f;
        engines.push_back({"engine er6 predelay delay rolloff", s});
    }
    {
        SeptimaSettings s; s.eq[0].gainDb = 4.0f; s.eq[1].gainDb = -3.0f; s.eq[2].gainDb = -6.0f; s.eq[3].freqHz = 9000.0f;
        s.modulation = 0.6f; s.mix = 0.7f; s.gainDb = -3.0f;
        engines.push_back({"engine eq mod mix", s});
    }
    for (auto& e : engines) {
        SeptimaEngine eng;
        eng.setSettings(e.s);
        eng.prepare(kFs, 512);
        const int total = static_cast<int>(3.0 * kFs);
        std::vector<float> outL, outR;
        Lcg rng{999u};
        std::vector<float> l(512), r2(512), fl(512), fr(512), fv(512), dl(512), dr(512);
        for (int pos = 0; pos < total; pos += 512) {
            for (int i = 0; i < 512; ++i) {
                const double t = (pos + i) / kFs;
                const float burst = t < 0.05 ? 0.4f * rng.next() : 0.0f;
                const float tone = (t > 0.6 && t < 1.4) ? 0.2f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 196.0 * t)) : 0.0f;
                l[static_cast<std::size_t>(i)] = burst + tone;
                r2[static_cast<std::size_t>(i)] = 0.8f * burst - tone;
            }
            eng.makeFeeds(l.data(), r2.data(), fl.data(), fr.data(), fv.data(), 512);
            std::copy(l.begin(), l.end(), dl.begin());
            std::copy(r2.begin(), r2.end(), dr.begin());
            eng.finish(dl.data(), dr.data(), l.data(), r2.data(), fl.data(), fr.data(), fv.data(), fv.data(), 512);
            outL.insert(outL.end(), dl.begin(), dl.end());
            outR.insert(outR.end(), dr.begin(), dr.end());
        }
        auto fp = windows(outL, 12000, 12);
        const auto wr = windows(outR, 12000, 12);
        fp.insert(fp.end(), wr.begin(), wr.end());
        r[e.name] = fp;
    }
    return r;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: rr_golden_core <check|update> <file>\n"); return 2; }
    const std::string mode = argv[1];
    const auto results = run();
    if (mode == "update") {
        std::ofstream out(argv[2]);
        out << "# Septima DSP golden fingerprints (RMS dB per window). Regenerate only for intended changes.\n";
        for (const auto& [name, fp] : results) {
            out << name << " |";
            char buf[32];
            for (float v : fp) { std::snprintf(buf, sizeof buf, " %.2f", v); out << buf; }
            out << '\n';
        }
        std::printf("wrote %zu fingerprints to %s\n", results.size(), argv[2]);
        return 0;
    }
    std::ifstream in(argv[2]);
    if (!in) { std::printf("cannot read %s\n", argv[2]); return 1; }
    int checked = 0, failures = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        const auto bar = line.find(" |");
        const std::string name = line.substr(0, bar);
        std::istringstream vals(line.substr(bar + 2));
        std::vector<float> want;
        for (float v; vals >> v;) want.push_back(v);
        const auto it = results.find(name);
        if (it == results.end()) { std::printf("MISSING  %s\n", name.c_str()); ++failures; continue; }
        ++checked;
        float worst = 0.0f;
        for (std::size_t k = 0; k < want.size() && k < it->second.size(); ++k)
            if (want[k] > kFloorDb + 1.0f || it->second[k] > kFloorDb + 1.0f)
                worst = std::max(worst, std::abs(want[k] - it->second[k]));
        if (worst > kToleranceDb || want.size() != it->second.size()) { std::printf("CHANGED  %s (max %.2f dB)\n", name.c_str(), worst); ++failures; }
    }
    std::printf("septima golden: %d checked, %d changed\n", checked, failures);
    return failures == 0 && checked > 0 ? 0 : 1;
}
