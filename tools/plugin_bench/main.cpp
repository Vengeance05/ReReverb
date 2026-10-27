// Loads a VST3, applies optional parameter overrides, and reports CPU load
// (% of one core in real time) plus basic correctness checks.
// usage: PluginBench <plugin.vst3> [fs] [block] [param=value ...]
#include <juce_audio_processors/juce_audio_processors.h>
#include <chrono>
#include <thread>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) return 2;
    const double fs = argc > 2 ? std::atof(argv[2]) : 48000.0;
    const int block = argc > 3 ? std::atoi(argv[3]) : 256;
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile(types, argv[1]);
    if (types.isEmpty()) { std::printf("no plugin\n"); return 1; }
    juce::String err;
    auto p = fmt.createInstanceFromDescription(*types[0], fs, block, err);
    if (!p) { std::printf("instantiate failed: %s\n", err.toRawUTF8()); return 1; }
    p->setPlayConfigDetails(2, 2, fs, block);
    for (int a = 4; a < argc; ++a) {
        juce::String kv(argv[a]);
        const auto name = kv.upToFirstOccurrenceOf("=", false, false);
        const float val = kv.fromFirstOccurrenceOf("=", false, false).getFloatValue();
        bool found = false;
        for (auto* prm : p->getParameters())
            if (auto* ranged = dynamic_cast<juce::HostedAudioProcessorParameter*>(prm))
                if (ranged->getName(64) == name) { prm->setValueNotifyingHost(prm->getValueForText(juce::String(val))); found = true; }
        if (!found) std::printf("param '%s' not found\n", name.toRawUTF8());
    }
    p->prepareToPlay(fs, block);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    juce::Random rng(1);
    auto feed = [&](bool sound) {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i)
            buf.setSample(c, i, sound ? (rng.nextFloat() * 2 - 1) * 0.2f : 0.0f);
        p->processBlock(buf, midi);
    };
    // Warm up / let Septima load its IR.
    for (int i = 0; i < 40; ++i) { feed(true); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    auto timeIt = [&](bool sound, double seconds) {
        const int blocks = static_cast<int>(seconds * fs / block);
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) feed(sound);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return 100.0 * dt / seconds;
    };
    const double busy = timeIt(true, 10.0);
    const double tail = timeIt(false, 2.0);      // reverb tail still ringing
    const double idle = timeIt(false, 20.0);     // long silence after tail
    std::printf("%s @%.0f/%d: active %.2f%%  tail %.2f%%  silent %.2f%%  latency %d\n",
                types[0]->name.toRawUTF8(), fs, block, busy, tail, idle, p->getLatencySamples());
    return 0;
}
