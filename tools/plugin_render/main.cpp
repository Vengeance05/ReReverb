// Offline render through a VST3: PluginRender <plugin.vst3> <in.wav> <out.wav> [--preset "Name"] [--tail sec] [--block n]
//   [--sweep "Param Name" lo hi] [--list] [Param Name=value ...]
// --sweep moves a parameter (normalised lo..hi..lo, triangle) across the input once, every block.
// Values are given in the parameter's display units (as typed into the host).
// Also prints CPU load (% of one core) for the render.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <thread>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4) { std::printf("usage\n"); return 2; }
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> types;
    fmt.findAllTypesForFile(types, argv[1]);
    if (types.isEmpty()) { std::printf("no plugin\n"); return 1; }
    juce::AudioFormatManager afm; afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(afm.createReaderFor(juce::File{juce::String(argv[2])}));
    if (!rd) { std::printf("cannot read input\n"); return 1; }
    const double fs = rd->sampleRate;
    int block = 256; double tail = 4.0; juce::String preset;
    std::vector<std::pair<juce::String, juce::String>> params;
    juce::String sweepName; float sweepLo = 0, sweepHi = 1; bool list = false, realtime = false, nosettle = false;
    for (int a = 4; a < argc; ++a) {
        juce::String s(argv[a]);
        if (s == "--block") block = juce::String(argv[++a]).getIntValue();
        else if (s == "--tail") tail = juce::String(argv[++a]).getDoubleValue();
        else if (s == "--preset") preset = argv[++a];
        else if (s == "--list") list = true;
        else if (s == "--realtime") realtime = true;
        else if (s == "--nosettle") nosettle = true;
        else if (s == "--sweep") { sweepName = argv[++a]; sweepLo = juce::String(argv[++a]).getFloatValue(); sweepHi = juce::String(argv[++a]).getFloatValue(); }
        else params.push_back({s.upToFirstOccurrenceOf("=", false, false), s.fromFirstOccurrenceOf("=", false, false)});
    }
    juce::String err;
    auto p = fmt.createInstanceFromDescription(*types[0], fs, block, err);
    if (!p) { std::printf("instantiate failed\n"); return 1; }
    p->setPlayConfigDetails(2, 2, fs, block);
    p->setNonRealtime(!realtime);
    p->prepareToPlay(fs, block);
    if (list) { for (int i = 0; i < p->getNumPrograms(); ++i) std::printf("#%s\n", p->getProgramName(i).toRawUTF8());
        for (auto* prm : p->getParameters()) std::printf("%s|%d\n", prm->getName(64).toRawUTF8(), prm->getNumSteps()); return 0; }
    if (preset.isNotEmpty()) {
        int found = -1;
        for (int i = 0; i < p->getNumPrograms(); ++i)
            if (p->getProgramName(i).endsWith(" - " + preset) || p->getProgramName(i) == preset) found = i;
        if (found < 0) { std::printf("preset '%s' not found\n", preset.toRawUTF8()); return 1; }
        p->setCurrentProgram(found);
        std::printf("  program %d: %s\n", found, p->getProgramName(found).toRawUTF8());
    }
    for (auto& [name, val] : params) {
        bool found = false;
        for (auto* prm : p->getParameters())
            if (prm->getName(64) == name) { prm->setValueNotifyingHost(prm->getValueForText(val)); found = true;
                std::printf("  %s = %s\n", name.toRawUTF8(), prm->getCurrentValueAsText().toRawUTF8()); }
        if (!found) { std::printf("param '%s' not found\n", name.toRawUTF8()); return 1; }
    }
    // Let worker threads settle (Septima IR load) while feeding silence.
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    for (int i = 0; i < (nosettle ? 0 : 60); ++i) { buf.clear(); p->processBlock(buf, midi); std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
    // Re-apply parameters after asynchronous loading (Septima resets Decay to the
    // sampled value when a program loads), then let any rebuild finish.
    for (auto& [name, val] : params)
        for (auto* prm : p->getParameters())
            if (prm->getName(64) == name) {
                const float target = prm->getValueForText(val);
                prm->setValueNotifyingHost(target > 0.5f ? 0.0f : 1.0f); // force a real change
                prm->setValueNotifyingHost(target);
            }
    for (int i = 0; i < (nosettle ? 0 : 60); ++i) { buf.clear(); p->processBlock(buf, midi); std::this_thread::sleep_for(std::chrono::milliseconds(30)); }
    p->reset();
    const int inLen = static_cast<int>(rd->lengthInSamples);
    const int total = inLen + static_cast<int>(tail * fs);
    juce::AudioBuffer<float> in(2, inLen); in.clear();
    rd->read(&in, 0, inLen, 0, true, true);
    if (rd->numChannels == 1) in.copyFrom(1, 0, in, 0, 0, inLen);
    juce::AudioBuffer<float> out(2, total);
    juce::AudioProcessorParameter* sweep = nullptr;
    if (sweepName.isNotEmpty()) {
        for (auto* prm : p->getParameters()) if (prm->getName(64) == sweepName) sweep = prm;
        if (sweep == nullptr) { std::printf("sweep param not found\n"); return 1; }
        sweep->setValueNotifyingHost(sweepLo);
    }
    double cpu = 0, peak = 0;
    std::vector<double> loads;
    const std::clock_t cpu0 = std::clock();
    const auto wall0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        if (sweep != nullptr && pos < inLen) {
            const double ph = static_cast<double>(pos) / inLen;
            const double tri = ph < 0.5 ? 2.0 * ph : 2.0 - 2.0 * ph;
            sweep->setValueNotifyingHost(static_cast<float>(sweepLo + (sweepHi - sweepLo) * tri));
        }
        buf.clear();
        for (int c = 0; c < 2; ++c) if (pos < inLen) buf.copyFrom(c, 0, in, c, pos, std::min(n, inLen - pos));
        auto t0 = std::chrono::steady_clock::now();
        if (n == block) p->processBlock(buf, midi);
        else { juce::AudioBuffer<float> sb(buf.getArrayOfWritePointers(), 2, n); p->processBlock(sb, midi); }
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        cpu += dt; if (pos > 4 * block) { peak = std::max(peak, dt / (n / fs)); loads.push_back(dt / (n / fs)); }
        if (realtime) std::this_thread::sleep_until(wall0 + std::chrono::microseconds(static_cast<long long>((pos + n) / fs * 1.0e6)));
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
    }
    juce::File f{juce::String(argv[3])}; f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), fs, 2, 32, {}, 0));
    w->writeFromAudioSampleBuffer(out, 0, total);
    const double allThreads = static_cast<double>(std::clock() - cpu0) / CLOCKS_PER_SEC;
    std::sort(loads.begin(), loads.end());
    const double p999 = loads.empty() ? 0.0 : loads[std::min(loads.size() - 1, static_cast<std::size_t>(loads.size() * 0.999))];
    const double p99 = loads.empty() ? 0.0 : loads[std::min(loads.size() - 1, static_cast<std::size_t>(loads.size() * 0.99))];
    std::printf("rendered %s  audio thread %.2f%%, all threads %.2f%% of one core | block load p99 %.0f%%, p99.9 %.0f%%, max %.0f%%\n", argv[3], 100.0 * cpu / (total / fs), 100.0 * allThreads / (total / fs), 100.0 * p99, 100.0 * p999, 100.0 * peak);
    return 0;
}
