// Session-compatibility guard ("golden renders").
//
//   <exe> check  <golden.txt>   render a fixed set of presets and compare with the file
//   <exe> update <golden.txt>   rewrite the file (only when a sound change is intended
//                               AND old sessions are protected by an engine version)
//
// Renders a deterministic test signal through the plugin (linked in-process) for
// several factory presets plus the default state, and fingerprints the output as
// RMS levels (dB) over consecutive 250 ms windows per channel. A future change that
// alters how existing settings sound fails the check, so released sessions keep
// their sound.
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 256, kSeconds = 4, kWindow = 12000;
constexpr float kToleranceDb = 0.1f, kFloorDb = -100.0f;

// Noise burst, then a short two-note phrase, then silence (the tail).
void makeInput(juce::AudioBuffer<float>& b) {
    std::uint32_t s = 22222u;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 8388608.0f - 1.0f; };
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / kFs;
        float l = 0.0f, r = 0.0f;
        if (t < 0.02) { l = 0.5f * rnd(); r = 0.5f * rnd(); }
        else if (t >= 0.5 && t < 1.2) {
            const float env = static_cast<float>(std::min(1.0, (t - 0.5) * 50.0) * std::min(1.0, (1.2 - t) * 50.0));
            l = env * 0.25f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t));
            r = env * 0.25f * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 330.0 * t));
        }
        b.setSample(0, i, l);
        b.setSample(1, i, r);
    }
}

std::vector<float> render(juce::AudioProcessor& p) {
    juce::AudioBuffer<float> io(2, kSeconds * static_cast<int>(kFs));
    makeInput(io);
    juce::MidiBuffer midi;
    // Let any preset-change transitions finish before measuring, so the check
    // compares steady-state sound, not crossfade shapes. The settle signal is an
    // inaudible -120 dB tone: true silence would let the engine idle and defer
    // the change to the first measured block.
    {
        juce::AudioBuffer<float> settle(2, kBlock);
        for (int i = 0; i < static_cast<int>(kFs) * 2 / kBlock; ++i) {
            for (int k = 0; k < kBlock; ++k) {
                const float v = 1.0e-6f * static_cast<float>(std::sin(0.05 * (i * kBlock + k)));
                settle.setSample(0, k, v); settle.setSample(1, k, v);
            }
            p.processBlock(settle, midi);
        }
    }
    for (int pos = 0; pos < io.getNumSamples(); pos += kBlock) {
        const int n = std::min(kBlock, io.getNumSamples() - pos);
        juce::AudioBuffer<float> view(io.getArrayOfWritePointers(), 2, pos, n);
        p.processBlock(view, midi);
    }
    std::vector<float> fp;
    for (int ch = 0; ch < 2; ++ch)
        for (int w = 0; w + kWindow <= io.getNumSamples(); w += kWindow) {
            double e = 0.0;
            for (int i = w; i < w + kWindow; ++i) e += static_cast<double>(io.getSample(ch, i)) * io.getSample(ch, i);
            fp.push_back(std::max(kFloorDb, static_cast<float>(10.0 * std::log10(e / kWindow + 1e-30))));
        }
    return fp;
}
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::printf("usage: <check|update> <golden.txt>\n"); return 2; }
    const juce::String mode(argv[1]);
    juce::PluginHostType::jucePlugInClientCurrentWrapperType = juce::AudioProcessor::wrapperType_Standalone;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    p->setPlayConfigDetails(2, 2, kFs, kBlock);
    p->setNonRealtime(true);
    p->prepareToPlay(kFs, kBlock);

    // Default state, then every 25th factory program.
    std::map<juce::String, std::vector<float>> results;
    results["<default>"] = render(*p);
    for (int i = 0; i < p->getNumPrograms(); i += 25) {
        p->setCurrentProgram(i);
        results[juce::String(i) + " " + p->getProgramName(i)] = render(*p);
    }

    if (mode == "update") {
        std::ofstream out(argv[2]);
        out << "# Golden renders: preset | RMS dB per 250 ms window (L then R). Regenerate only for intended changes.\n";
        for (auto& [name, fp] : results) {
            out << name.toStdString() << " |";
            for (float v : fp) out << ' ' << juce::String(v, 2).toStdString();
            out << '\n';
        }
        std::printf("wrote %zu golden renders to %s\n", results.size(), argv[2]);
        return 0;
    }

    std::ifstream in(argv[2]);
    if (!in) { std::printf("cannot read %s\n", argv[2]); return 1; }
    int failures = 0, checked = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto bar = line.find(" |");
        const juce::String name(line.substr(0, bar));
        std::istringstream vals(line.substr(bar + 2));
        std::vector<float> want;
        for (float v; vals >> v;) want.push_back(v);
        auto it = results.find(name);
        if (it == results.end()) { std::printf("MISSING  %s (preset removed or renamed?)\n", name.toRawUTF8()); ++failures; continue; }
        ++checked;
        float worst = 0.0f;
        for (std::size_t k = 0; k < want.size() && k < it->second.size(); ++k)
            if (want[k] > kFloorDb + 1.0f || it->second[k] > kFloorDb + 1.0f)
                worst = std::max(worst, std::abs(want[k] - it->second[k]));
        if (worst > kToleranceDb || want.size() != it->second.size()) {
            std::printf("CHANGED  %s (max %.2f dB)\n", name.toRawUTF8(), worst);
            ++failures;
        }
    }
    std::printf("golden renders: %d checked, %d changed\n", checked, failures);
    p->releaseResources();
    return failures == 0 && checked > 0 ? 0 : 1;
}
