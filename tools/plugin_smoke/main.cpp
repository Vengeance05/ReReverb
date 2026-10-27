// Loads a built VST3, processes audio and round-trips state. Prints PASS/FAIL lines.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <thread>

static int failures = 0;
static void check(bool ok, const juce::String& what) { std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (!ok) ++failures; }

static double runAudio(juce::AudioPluginInstance& p, double seconds, double fs, int block, double* tailEnergy = nullptr, bool impulse = false) {
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi; juce::Random rng(7);
    double e = 0, tail = 0; const int total = int(seconds * fs);
    for (int pos = 0; pos < total; pos += block) {
        for (int i = 0; i < block; ++i) {
            const bool on = impulse ? (pos + i == 0) : (pos + i) < int(0.3 * fs);
            const float v = impulse ? (on ? 1.0f : 0.0f) : (on ? (rng.nextFloat() * 2 - 1) * 0.3f : 0.0f);
            buf.setSample(0, i, v); buf.setSample(1, i, impulse ? v : (on ? (rng.nextFloat() * 2 - 1) * 0.3f : 0.0f));
        }
        p.processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < block; ++i) {
            const float s = buf.getSample(ch, i);
            if (!std::isfinite(s)) return -1;
            e += s * s; if (pos > int(0.5 * fs)) tail += s * s;
        }
    }
    if (tailEnergy) *tailEnergy = tail;
    return e;
}

static void setParam(juce::AudioPluginInstance& p, const juce::String& name, float norm) {
    for (auto* prm : p.getParameters()) if (prm->getName(64) == name) { prm->setValueNotifyingHost(norm); return; }
    std::printf("  (param %s not found)\n", name.toRawUTF8());
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) return 2;
    juce::VST3PluginFormat fmt; juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile(types, argv[1]);
    check(types.size() == 1, "found plugin type in " + juce::String(argv[1]));
    if (types.isEmpty()) return 1;
    const auto desc = *types[0];
    std::printf("  name=%s manufacturer=%s category=%s\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(), desc.category.toRawUTF8());
    const bool septima = desc.name == "Septima";
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (int block : {64, 512, 1024}) {
            juce::String err;
            auto p = fmt.createInstanceFromDescription(desc, fs, block, err);
            if (!p) { check(false, "instantiate: " + err); return 1; }
            p->setPlayConfigDetails(2, 2, fs, block);
            p->prepareToPlay(fs, block);
            if (septima) {
                // Give the worker time to scan the library and prepare the IR.
                for (int i = 0; i < 60 && runAudio(*p, 0.05, fs, block) >= 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            double tail = 0; const double e = runAudio(*p, 2.0, fs, block, &tail);
            check(e > 0 && tail > 1e-6, juce::String::formatted("audio @%.0f/%d energy %.3f tail %.4f", fs, block, e, tail));
            p->releaseResources();
        }
    }
    // State round trip.
    juce::String err;
    auto a = fmt.createInstanceFromDescription(desc, 48000, 512, err);
    a->prepareToPlay(48000, 512);
    int changed = 0;
    for (auto* prm : a->getParameters()) { if (++changed % 3 == 0) prm->setValueNotifyingHost(0.37f); }
    juce::MemoryBlock mb; a->getStateInformation(mb);
    auto b = fmt.createInstanceFromDescription(desc, 48000, 512, err);
    b->setStateInformation(mb.getData(), int(mb.getSize()));
    int mismatches = 0;
    for (int i = 0; i < a->getParameters().size(); ++i)
        if (std::abs(a->getParameters()[i]->getValue() - b->getParameters()[i]->getValue()) > 1e-4f) ++mismatches;
    check(mismatches == 0, "state round trip (" + juce::String(a->getParameters().size()) + " params, " + juce::String(mismatches) + " mismatches)");
    // Extremes: all parameters to 0 then 1 must stay finite.
    for (float v : {0.0f, 1.0f}) {
        auto x = fmt.createInstanceFromDescription(desc, 48000, 256, err);
        x->prepareToPlay(48000, 256);
        for (auto* prm : x->getParameters()) prm->setValueNotifyingHost(v);
        if (septima) std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        const double e = runAudio(*x, 3.0, 48000, 256);
        check(e >= 0 && e < 1e7, juce::String::formatted("all params at %.0f stay finite (energy %.2f)", v, e));
    }
    // Impulse response render for listening/inspection.
    if (argc > 2) {
        auto x = fmt.createInstanceFromDescription(desc, 48000, 512, err);
        x->prepareToPlay(48000, 512);
        if (septima) { for (int i = 0; i < 40; ++i) { runAudio(*x, 0.05, 48000, 512); std::this_thread::sleep_for(std::chrono::milliseconds(50)); } x->reset(); }
        juce::AudioBuffer<float> out(2, 48000 * 6); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += 512) {
            buf.clear(); if (pos == 0) { buf.setSample(0, 0, 1.0f); buf.setSample(1, 0, 1.0f); }
            x->processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, pos, buf, ch, 0, 512);
        }
        juce::WavAudioFormat wav; juce::File f(argv[2]); f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), 48000, 2, 32, {}, 0));
        w->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
        std::printf("  wrote %s\n", argv[2]);
    }
    std::printf("%s: %d failures\n", desc.name.toRawUTF8(), failures);
    return failures ? 1 : 0;
}
