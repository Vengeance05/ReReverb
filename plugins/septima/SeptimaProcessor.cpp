#include "SeptimaProcessor.h"
#include "SeptimaEditor.h"

using rrui::Unit;

namespace {

const juce::StringArray kEqLowHighTypes{"Shelf", "Bell"};

juce::StringArray erSetChoices() {
    juce::StringArray c{"Sampled"};
    for (int i = 0; i < 32; ++i) c.add("Set " + juce::String(i));
    return c;
}

const char* const kReshapeParams[] = {"decay", "low_mult", "low_xover", "high_mult", "high_xover"};

// Serialises heavy IR rebuilds across all plugin instances so a template with
// many instances does not spike memory/CPU on session load.
std::mutex& rebuildMutex() {
    static std::mutex m;
    return m;
}

bool readAudioFile(juce::AudioFormatManager& fm, const juce::File& f, juce::AudioBuffer<float>& out, double& fs) {
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
    if (reader == nullptr) return false;
    const auto maxLen = static_cast<juce::int64>(reader->sampleRate * 40.0);
    const int len = static_cast<int>(std::min(reader->lengthInSamples, maxLen));
    const int ch = static_cast<int>(std::min<unsigned int>(reader->numChannels, 4));
    if (len <= 0 || ch <= 0) return false;
    out.setSize(ch, len);
    reader->read(&out, 0, len, 0, true, true);
    fs = reader->sampleRate;
    return true;
}

std::vector<float> channelVec(const juce::AudioBuffer<float>& b, int ch) {
    if (ch >= b.getNumChannels()) return std::vector<float>(static_cast<std::size_t>(b.getNumSamples()), 0.0f);
    return {b.getReadPointer(ch), b.getReadPointer(ch) + b.getNumSamples()};
}

// A decorrelated version of the four late channels with the same time/frequency
// envelope: STFT, rotate every bin's phase by a random angle (the same angle for
// all four channels, so their mutual phase relations - stereo image - survive),
// inverse STFT. Only the part from `start` on is used (the tail).
std::array<std::vector<float>, 4> makeTailVariant(const std::array<std::vector<float>, 4>& late, std::uint32_t seed, int start) {
    constexpr int order = 11, N = 1 << order, hop = N / 4;
    juce::dsp::FFT fft(order);
    std::array<std::vector<float>, 4> out;
    std::size_t len = 0;
    for (const auto& c : late) len = std::max(len, c.size());
    std::vector<float> win(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) win[static_cast<std::size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * (static_cast<float>(i) + 0.5f) / N));
    for (auto& c : out) c.assign(len, 0.0f);
    std::vector<float> buf(static_cast<std::size_t>(2 * N));
    juce::Random rng(static_cast<juce::int64>(seed));
    const int firstFrame = std::max(0, start - 2 * N) / hop;
    std::vector<std::complex<float>> rot(static_cast<std::size_t>(N / 2 + 1));
    for (long long f0 = static_cast<long long>(firstFrame) * hop; f0 < static_cast<long long>(len); f0 += hop) {
        for (auto& r : rot) { const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi; r = {std::cos(a), std::sin(a)}; }
        rot.front() = 1.0f; rot.back() = 1.0f;
        for (int c = 0; c < 4; ++c) {
            const auto& x = late[static_cast<std::size_t>(c)];
            std::fill(buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i) {
                const auto idx = static_cast<std::size_t>(f0 + i);
                if (idx < x.size()) buf[static_cast<std::size_t>(i)] = x[idx] * win[static_cast<std::size_t>(i)];
            }
            fft.performRealOnlyForwardTransform(buf.data(), true);
            for (int b = 0; b <= N / 2; ++b) {
                const std::complex<float> v{buf[static_cast<std::size_t>(2 * b)], buf[static_cast<std::size_t>(2 * b + 1)]};
                const auto r = v * rot[static_cast<std::size_t>(b)];
                buf[static_cast<std::size_t>(2 * b)] = r.real();
                buf[static_cast<std::size_t>(2 * b + 1)] = r.imag();
            }
            fft.performRealOnlyInverseTransform(buf.data());
            auto& y = out[static_cast<std::size_t>(c)];
            for (int i = 0; i < N; ++i) {
                const auto idx = static_cast<std::size_t>(f0 + i);
                if (idx < y.size()) y[idx] += buf[static_cast<std::size_t>(i)] * win[static_cast<std::size_t>(i)];
            }
        }
    }
    // Random-phase frames add in power, not amplitude: match each channel's tail
    // energy to the original in 50 ms windows so the envelope is preserved exactly.
    const int w = 2400;
    for (int c = 0; c < 4; ++c) {
        const auto& x = late[static_cast<std::size_t>(c)];
        auto& y = out[static_cast<std::size_t>(c)];
        std::vector<float> gains;
        for (std::size_t a = 0; a < y.size(); a += static_cast<std::size_t>(w)) {
            double ex = 0.0, ey = 0.0;
            for (std::size_t i = a; i < std::min(y.size(), a + static_cast<std::size_t>(w)); ++i) {
                ex += i < x.size() ? static_cast<double>(x[i]) * x[i] : 0.0;
                ey += static_cast<double>(y[i]) * y[i];
            }
            gains.push_back(ey > 1.0e-30 ? static_cast<float>(std::sqrt(ex / ey)) : 0.0f);
        }
        for (std::size_t i = 0; i < y.size(); ++i) {
            const double pos = static_cast<double>(i) / w - 0.5;
            const auto k = static_cast<std::size_t>(std::clamp(pos, 0.0, static_cast<double>(gains.size() - 1)));
            const auto k1 = std::min(gains.size() - 1, k + 1);
            const float f = static_cast<float>(std::clamp(pos - static_cast<double>(k), 0.0, 1.0));
            y[i] *= gains[k] + f * (gains[k1] - gains[k]);
        }
    }
    return out;
}

juce::AudioBuffer<float> toBuffer(const std::vector<float>& a, const std::vector<float>& b) {
    const int n = static_cast<int>(std::max<std::size_t>(1, std::max(a.size(), b.size())));
    juce::AudioBuffer<float> buf(2, n);
    buf.clear();
    if (!a.empty()) buf.copyFrom(0, 0, a.data(), static_cast<int>(a.size()));
    if (!b.empty()) buf.copyFrom(1, 0, b.data(), static_cast<int>(b.size()));
    return buf;
}


using SP = std::pair<juce::String, float>;
using SF = rrui::PresetManager::Factory;

juce::String progKey(const char* category, int number, const char* name) {
    return (juce::String(category) + "/" + juce::String(number).paddedLeft('0', 2) + " " + name).toLowerCase();
}

#include "SeptimaFactoryPresets.inc"

// One factory preset per library program, named and grouped like the original
// unit's banks. No value overrides: each loads at its sampled decay with every
// other control at its neutral default, i.e. exactly the captured program.
std::vector<SF> septimaFactory() {
    std::vector<SF> out;
    out.reserve(std::size(kProgramPresets));
    for (const auto& e : kProgramPresets)
        out.push_back({e.name, e.bank, {}, progKey(e.bank, e.number, e.name)});
    return out;
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SeptimaProcessor::createLayout() {
    rrui::ParamList l;
    rrui::addFloat(l, "decay", "Decay Time", 0.2f, 30.0f, 2.5f, Unit::Seconds, 3.0f);
    rrui::addFloat(l, "predelay", "Pre-delay", 0, 500, 0, Unit::Ms, 80);
    rrui::addChoice(l, "predelay_sync", "Pre-delay Sync", rrui::syncChoices(), 0);
    rrui::addFloat(l, "delay_time", "Delay Time", 0, 1000, 250, Unit::Ms, 250);
    rrui::addChoice(l, "delay_sync", "Delay Sync", rrui::syncChoices(), 0);
    rrui::addFloat(l, "delay_level", "Delay Level", 0, 100, 0, Unit::Percent);
    rrui::addChoice(l, "er_set", "Early Reflections", erSetChoices(), 0);
    rrui::addFloat(l, "early_late", "Early / Late", 0, 100, 50, Unit::Percent);
    rrui::addFloat(l, "vlf", "VLF", 0, 200, 100, Unit::Percent);
    rrui::addFloat(l, "early_rolloff", "Early Roll-off", 1000, 20000, 20000, Unit::Hz, 6000);
    rrui::addFloat(l, "late_rolloff", "Late Roll-off", 1000, 20000, 20000, Unit::Hz, 6000);
    rrui::addFloat(l, "low_mult", "Low Decay Multiply", 0.25f, 4.0f, 1.0f, Unit::Multiplier, 1.0f);
    rrui::addFloat(l, "low_xover", "Low Crossover", 50, 1000, 250, Unit::Hz, 250);
    rrui::addFloat(l, "high_mult", "High Decay Multiply", 0.25f, 4.0f, 1.0f, Unit::Multiplier, 1.0f);
    rrui::addFloat(l, "high_xover", "High Crossover", 1000, 16000, 4000, Unit::Hz, 4000);
    rrui::addFloat(l, "modulation", "Modulation", 0, 100, 5, Unit::Percent);
    // Slowly evolving tail (blends decorrelated versions of the late tail). 0 = the static capture.
    rrui::addFloat(l, "motion", "Tail Motion", 0, 100, 5, Unit::Percent);
    rrui::addBool(l, "eq_on", "EQ On", true);
    rrui::addChoice(l, "eq_low_type", "EQ Low Type", kEqLowHighTypes, 0);
    rrui::addFloat(l, "eq_low_freq", "EQ Low Freq", 30, 1000, 150, Unit::Hz, 200);
    rrui::addFloat(l, "eq_low_gain", "EQ Low Gain", -18, 18, 0, Unit::Db);
    rrui::addFloat(l, "eq_low_q", "EQ Low Q", 0.3f, 4.0f, 0.707f, Unit::None, 1.0f);
    rrui::addFloat(l, "eq_mid_freq", "EQ Mid Freq", 100, 10000, 1000, Unit::Hz, 1000);
    rrui::addFloat(l, "eq_mid_gain", "EQ Mid Gain", -18, 18, 0, Unit::Db);
    rrui::addFloat(l, "eq_mid_q", "EQ Mid Q", 0.3f, 8.0f, 0.707f, Unit::None, 1.5f);
    rrui::addChoice(l, "eq_high_type", "EQ High Type", kEqLowHighTypes, 0);
    rrui::addFloat(l, "eq_high_freq", "EQ High Freq", 1000, 16000, 6000, Unit::Hz, 5000);
    rrui::addFloat(l, "eq_high_gain", "EQ High Gain", -18, 18, 0, Unit::Db);
    rrui::addFloat(l, "eq_high_q", "EQ High Q", 0.3f, 4.0f, 0.707f, Unit::None, 1.0f);
    rrui::addFloat(l, "eq_highcut", "EQ High Cut", 1000, 20000, 20000, Unit::Hz, 6000);
    rrui::addFloat(l, "mix", "Mix", 0, 100, 100, Unit::Percent);
    rrui::addFloat(l, "gain", "Gain", -24, 12, 0, Unit::Db);
    return {l.begin(), l.end()};
}

SeptimaProcessor::SeptimaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Septima IR worker"),
      state_(*this, nullptr, "Septima", createLayout()) {
    formats_.registerBasicFormats();
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            ids_.push_back(rp->paramID);
            state_.addParameterListener(rp->paramID, this);
        }
    presets_ = std::make_unique<rrui::PresetManager>(state_, "Septima", septimaFactory());
    presets_->applyFactoryExtra = [this](const rrui::PresetManager::Factory& f) {
        bool hasDecay = false;
        for (const auto& v : f.values) hasDecay |= v.first == "decay";
        if (f.extra.isNotEmpty()) selectProgram(f.extra, !hasDecay);
    };
    presets_->writeExtra = [this](juce::XmlElement& x) { x.setAttribute("programKey", currentProgramKey()); };
    presets_->readExtra = [this](const juce::XmlElement& x) {
        const auto key = x.getStringAttribute("programKey");
        if (key.isNotEmpty()) selectProgram(key, false);
    };

    // Default library: the folder last chosen in any Septima instance, else
    // the IR set installed by the Rereverb package.
    juce::File folder;
    if (auto xml = juce::XmlDocument::parse(globalSettingsFile()))
        folder = juce::File(xml->getStringAttribute("libraryFolder"));
    if (!folder.isDirectory()) folder = bundledLibraryFolder();
    if (folder.isDirectory()) {
        std::lock_guard lock(mutex_);
        libraryFolder_ = folder;
        rescanRequested_ = true;
    }
    status_.text = "No IRs found: click IR Library... and choose your IR folder";
    startThread(juce::Thread::Priority::low);
}

SeptimaProcessor::~SeptimaProcessor() {
    for (const auto& id : ids_) state_.removeParameterListener(id, this);
    signalThreadShouldExit();
    notify();
    stopThread(5000);
    cancelPendingUpdate();
    delete pending_.exchange(nullptr);
    for (auto& r : retired_) delete r.exchange(nullptr);
    delete overflow_;
}

namespace {
juce::File appSupport(juce::File::SpecialLocationType where) {
#if JUCE_MAC
    return juce::File::getSpecialLocation(where).getChildFile("Application Support/Rereverb");
#else
    return juce::File::getSpecialLocation(where).getChildFile("Rereverb");
#endif
}
} // namespace

juce::File SeptimaProcessor::globalSettingsFile() {
    return appSupport(juce::File::userApplicationDataDirectory).getChildFile("Septima.settings");
}

juce::File SeptimaProcessor::bundledLibraryFolder() {
    // Prefer a default folder that actually holds IRs; else the first that exists.
    juce::File firstExisting;
    for (auto where : {juce::File::userApplicationDataDirectory, juce::File::commonApplicationDataDirectory}) {
        auto f = appSupport(where).getChildFile("Septima/IRs");
        if (!f.isDirectory()) continue;
        if (firstExisting == juce::File()) firstExisting = f;
        if (!f.findChildFiles(juce::File::findFiles, true, "*.wav;*.WAV;*.aif;*.aiff", juce::File::FollowSymlinks::no).isEmpty())
            return f;
    }
    return firstExisting;
}

juce::File SeptimaProcessor::userDefaultLibraryFolder(bool create) {
    auto f = appSupport(juce::File::userApplicationDataDirectory).getChildFile("Septima/IRs");
    if (create) f.createDirectory();
    return f;
}

void SeptimaProcessor::rescanLibrary() {
    {
        std::lock_guard lock(mutex_);
        if (!libraryFolder_.isDirectory()) libraryFolder_ = bundledLibraryFolder();
        if (!libraryFolder_.isDirectory()) return;
        rescanRequested_ = true;
    }
    notify();
}

void SeptimaProcessor::saveGlobalLibrary(const juce::File& folder) {
    juce::XmlElement x("SeptimaSettings");
    x.setAttribute("libraryFolder", folder.getFullPathName());
    auto f = globalSettingsFile();
    f.getParentDirectory().createDirectory();
    x.writeTo(f);
}

bool SeptimaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

// ---------------------------------------------------------------------------
// Library & program selection

void SeptimaProcessor::setLibraryFolder(const juce::File& folder) {
    {
        std::lock_guard lock(mutex_);
        libraryFolder_ = folder;
        rescanRequested_ = true;
    }
    saveGlobalLibrary(folder);
    notify();
}

juce::File SeptimaProcessor::libraryFolder() const {
    std::lock_guard lock(mutex_);
    return libraryFolder_;
}

std::vector<rr::IrProgram> SeptimaProcessor::programs() const {
    std::lock_guard lock(mutex_);
    return programs_;
}

juce::String SeptimaProcessor::currentProgramKey() const {
    std::lock_guard lock(mutex_);
    return requestedKey_.isNotEmpty() ? requestedKey_ : programKey_;
}

void SeptimaProcessor::selectProgram(const juce::String& key, bool applySampledDecay) {
    {
        std::lock_guard lock(mutex_);
        requestedKey_ = key;
        applySampledDecay_ = applySampledDecay;
    }
    loadRequested_ = true;
    notify();
}

SeptimaProcessor::Status SeptimaProcessor::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void SeptimaProcessor::setStatus(const juce::String& text, bool busy) {
    std::lock_guard lock(mutex_);
    status_.text = text;
    status_.busy = busy;
    ++status_.generation;
}

// ---------------------------------------------------------------------------
// Worker thread

void SeptimaProcessor::run() {
    while (!threadShouldExit()) {
        wait(40);
        if (threadShouldExit()) break;
        workerBusy_ = true;

        bool rescan = false;
        juce::File folder;
        {
            std::lock_guard lock(mutex_);
            rescan = std::exchange(rescanRequested_, false);
            folder = libraryFolder_;
        }
        if (rescan) {
            setStatus("Scanning library...", true);
            auto progs = rr::scanIrLibrary(folder.getFullPathName().toStdString());
            bool autoSelect = false;
            {
                std::lock_guard lock(mutex_);
                programs_ = std::move(progs);
                status_.busy = false;
                status_.text = programs_.empty() ? "No IR files found in " + folder.getFileName()
                                                 : juce::String(static_cast<int>(programs_.size())) + " programs found";
                ++status_.generation;
                if (requestedKey_.isEmpty() && programKey_.isEmpty() && !programs_.empty()) {
                    requestedKey_ = programs_.front().key;
                    applySampledDecay_ = true;
                    autoSelect = true;
                }
            }
            if (autoSelect) loadRequested_ = true;
            {
                std::lock_guard lock(mutex_);
                if (requestedKey_.isNotEmpty()) loadRequested_ = true; // request made before the scan finished
            }
        }
        if (loadRequested_.exchange(false)) {
            juce::String key;
            {
                std::lock_guard lock(mutex_);
                key = requestedKey_;
            }
            if (key.isNotEmpty()) doLoadProgram(key);
        }
        workerBusy_ = false;   // a parameter reshape alone is tracked separately
        if (reshapeRequested_ && juce::Time::currentTimeMillis() >= reshapeDue_.load()) {
            reshapeBusy_ = true;
            reshapeRequested_ = false;
            doReshape();
            reshapeBusy_ = false;
        }
        for (auto& r : retired_) delete r.exchange(nullptr);
    }
}

void SeptimaProcessor::doLoadProgram(const juce::String& key) {
    rr::IrProgram prog;
    bool found = false;
    bool applyDecay = false;
    {
        std::lock_guard lock(mutex_);
        for (const auto& pr : programs_)
            if (juce::String(pr.key) == key) { prog = pr; found = true; break; }
        applyDecay = applySampledDecay_;
    }
    if (!found) {
        // Library not scanned yet or program missing: keep the request for later.
        std::lock_guard lock(mutex_);
        if (!programs_.empty()) {
            status_.text = "Program not found in library";
            ++status_.generation;
        }
        return;
    }
    setStatus("Loading " + juce::String(prog.displayName()) + "...", true);
    reshapeRequested_ = false; // this load reshapes with the current parameters

    std::unique_ptr<rr::TrueStereoIr> ir;
    {
        std::lock_guard heavy(rebuildMutex());
        juce::AudioBuffer<float> a, b;
        double fsA = 0, fsB = 0;
        if (!readAudioFile(formats_, juce::File(prog.files[0]), a, fsA)) {
            setStatus("Could not read " + juce::File(prog.files[0]).getFileName(), false);
            return;
        }
        rr::TrueStereoIr t;
        if (prog.files.size() == 2) {
            if (!readAudioFile(formats_, juce::File(prog.files[1]), b, fsB)) {
                setStatus("Could not read " + juce::File(prog.files[1]).getFileName(), false);
                return;
            }
            t = rr::makeTrueStereo(channelVec(a, 0), channelVec(a, 1), channelVec(b, 0), channelVec(b, 1), fsA);
        } else if (a.getNumChannels() >= 4) {
            t = rr::makeTrueStereo(channelVec(a, 0), channelVec(a, 1), channelVec(a, 2), channelVec(a, 3), fsA);
        } else if (a.getNumChannels() == 2) {
            t = prog.format == rr::IrFormat::MonoToStereo
                ? rr::makeMonoToStereo(channelVec(a, 0), channelVec(a, 1), fsA)
                : rr::makeStereo(channelVec(a, 0), channelVec(a, 1), fsA);
        } else {
            t = rr::makeMonoToStereo(channelVec(a, 0), channelVec(a, 0), fsA);
        }
        // Process high-rate captures at 48 kHz (the convolver resamples to the host rate).
        if (t.sampleRate > 50000.0) {
            for (auto& c : t.ch) c = rr::resample(c, t.sampleRate, 48000.0);
            t.sampleRate = 48000.0;
        }
        rr::trimOnset(t);
        ir = std::make_unique<rr::TrueStereoIr>(std::move(t));
    }
    auto analysis = rr::analyseIr(*ir);
    {
        std::lock_guard lock(mutex_);
        ir_ = std::move(ir);
        analysis_ = analysis;
        programKey_ = key;
        if (requestedKey_ == key) requestedKey_.clear();
        status_.sampledDecay = analysis.midRt;
        status_.bandRt = analysis.bandRt;
        status_.sampleRate = ir_->sampleRate;
        status_.format = ir_->format == rr::IrFormat::TrueStereo ? "True stereo"
                       : ir_->format == rr::IrFormat::Stereo ? "Stereo" : "Mono to stereo";
        ++status_.generation;
    }
    if (applyDecay) {
        pendingDecayValue_ = juce::jlimit(0.2f, 30.0f, analysis.midRt);
        triggerAsyncUpdate();
    }
    doReshape();
}

void SeptimaProcessor::doReshape() {
    rr::ReshapeParams rp;
    const float pendingDecay = pendingDecayValue_.load();
    rp.decaySeconds = pendingDecay > 0.0f ? pendingDecay : p("decay");
    rp.lowMultiplier = p("low_mult");
    rp.lowCrossoverHz = p("low_xover");
    rp.highMultiplier = p("high_mult");
    rp.highCrossoverHz = p("high_xover");

    std::unique_ptr<IrPackage> pkg;
    juce::String name;
    {
        std::unique_lock lock(mutex_);
        if (ir_ == nullptr) return;
        // Snapshot under lock; the heavy work runs on copies of const data.
        const rr::TrueStereoIr& ir = *ir_;
        const rr::IrAnalysis analysis = analysis_;
        if (std::abs(rp.decaySeconds - analysis.midRt) < 0.005f * analysis.midRt) rp.decaySeconds = 0.0f;
        status_.busy = true;
        ++status_.generation;
        lock.unlock();

        std::lock_guard heavy(rebuildMutex());
        auto shaped = rr::reshapeIr(ir, analysis, rp);
        // Resample to the host rate here (high-quality sinc) so the convolver
        // never has to; gain src/dst keeps the response level unchanged.
        const double dst = hostRate_.load();
        if (dst > 0.0 && std::abs(dst - shaped.sampleRate) > 1.0e-6) {
            const auto g = static_cast<float>(shaped.sampleRate / dst);
            auto conv = [&](std::vector<float>& v) {
                v = rr::resample(v, shaped.sampleRate, dst);
                for (auto& s : v) s *= g;
            };
            for (int c = 0; c < 4; ++c) {
                conv(shaped.early[static_cast<std::size_t>(c)]);
                conv(shaped.late[static_cast<std::size_t>(c)]);
                conv(shaped.vlf[static_cast<std::size_t>(c)]);
            }
            shaped.sampleRate = dst;
        }
        pkg = std::make_unique<IrPackage>();
        pkg->sampleRate = shaped.sampleRate;
        {
            // Late/VLF responses are split three ways: the first 2Pa samples run on the
            // audio thread, the next section on tail stage A and the rest on stage B
            // (both background threads, zero added latency).
            const int Pb = septima::TailConvolver::partitionSizeFor(shaped.sampleRate);
            const int Pa = Pb / 8;
            std::array<const std::vector<float>*, septima::TailSpectra::kPaths> irs{
                &shaped.late[0], &shaped.late[1], &shaped.vlf[0], &shaped.vlf[1],
                &shaped.late[2], &shaped.late[3], &shaped.vlf[2], &shaped.vlf[3]};
            for (int c = 0; c < 4; ++c)
                pkg->totalLength = std::max({pkg->totalLength, static_cast<int>(shaped.late[static_cast<std::size_t>(c)].size()),
                                             static_cast<int>(shaped.vlf[static_cast<std::size_t>(c)].size()),
                                             static_cast<int>(shaped.early[static_cast<std::size_t>(c)].size())});
            pkg->tailA = septima::TailSpectra::build(irs, 2 * Pa, Pa, 2 * Pb);
            pkg->tailB = septima::TailSpectra::build(irs, 2 * Pb, Pb);
            // Two decorrelated versions of the late tail for Tail Motion (late paths only).
            for (int v = 0; v < 2; ++v) {
                const auto var = makeTailVariant(shaped.late, 0x5EB7u + 977u * static_cast<std::uint32_t>(v), 2 * Pb);
                std::array<const std::vector<float>*, septima::TailSpectra::kPaths> virs{
                    &var[0], &var[1], nullptr, nullptr, &var[2], &var[3], nullptr, nullptr};
                pkg->tailB->variants[static_cast<std::size_t>(v)] = septima::TailSpectra::build(virs, 2 * Pb, Pb);
            }
            for (int c = 0; c < 4; ++c)
                for (auto* v : {&shaped.late[static_cast<std::size_t>(c)], &shaped.vlf[static_cast<std::size_t>(c)]})
                    if (static_cast<int>(v->size()) > 2 * Pa) v->resize(static_cast<std::size_t>(2 * Pa));
        }
        pkg->earlyA = toBuffer(shaped.early[0], shaped.early[1]);
        pkg->earlyB = toBuffer(shaped.early[2], shaped.early[3]);
        pkg->lateA = toBuffer(shaped.late[0], shaped.late[1]);
        pkg->lateB = toBuffer(shaped.late[2], shaped.late[3]);
        pkg->vlfA = toBuffer(shaped.vlf[0], shaped.vlf[1]);
        pkg->vlfB = toBuffer(shaped.vlf[2], shaped.vlf[3]);
        // Synthetic ER sets carry ~0.7 energy per input; match the sampled early energy.
        pkg->syntheticErGain = juce::jlimit(0.05f, 4.0f, std::sqrt(std::max(analysis.earlyEnergy, 1.0e-6f) / 0.7f));
    }
    // Note: ir_ is only replaced by this same worker thread, so the reference
    // used above stayed valid while unlocked.
    delete pending_.exchange(pkg.release());
    std::lock_guard lock(mutex_);
    status_.busy = false;
    status_.ready = true;
    for (const auto& pr : programs_)
        if (juce::String(pr.key) == programKey_) status_.text = juce::String(pr.category) + " / " + juce::String(pr.displayName());
    ++status_.generation;
}

void SeptimaProcessor::handleAsyncUpdate() {
    const float v = pendingDecayValue_.load();
    if (v > 0.0f) {
        if (auto* param = state_.getParameter("decay")) param->setValueNotifyingHost(param->convertTo0to1(v));
        float expected = v;
        pendingDecayValue_.compare_exchange_strong(expected, -1.0f);
    }
}

void SeptimaProcessor::parameterChanged(const juce::String& id, float) {
    dirty_ = true;
    for (auto* rid : kReshapeParams) {
        if (id == rid) {
            if (id == "decay") {
                const float pending = pendingDecayValue_.load();
                // The program-load update itself: nothing to rebuild.
                if (pending > 0.0f && std::abs(p("decay") - pending) < 1.0e-3f * pending) return;
                // Any other change (user, automation, preset) overrides a pending sampled value.
                pendingDecayValue_ = -1.0f;
            }
            reshapeDue_ = juce::Time::currentTimeMillis() + 150;
            reshapeRequested_ = true;
            notify();
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Audio

rr::SeptimaSettings SeptimaProcessor::buildSettings(double bpm) const {
    rr::SeptimaSettings s;
    const double pre = rrui::syncSeconds(static_cast<int>(p("predelay_sync")), bpm);
    s.preDelayMs = pre > 0 ? static_cast<float>(std::min(500.0, pre * 1000.0)) : p("predelay");
    const double dly = rrui::syncSeconds(static_cast<int>(p("delay_sync")), bpm);
    s.delayMs = dly > 0 ? static_cast<float>(std::min(1000.0, dly * 1000.0)) : p("delay_time");
    s.delayLevel = p("delay_level") * 0.01f;
    s.erSet = static_cast<int>(p("er_set"));
    s.earlyLate = p("early_late") * 0.01f;
    s.vlfLevel = p("vlf") * 0.01f;
    s.earlyRolloffHz = p("early_rolloff");
    s.lateRolloffHz = p("late_rolloff");
    s.modulation = p("modulation") * 0.01f;
    s.eqOn = p("eq_on") > 0.5f;
    const bool lowBell = p("eq_low_type") > 0.5f, highBell = p("eq_high_type") > 0.5f;
    s.eq[0] = {lowBell ? rr::EqType::Peak : rr::EqType::LowShelf, p("eq_low_freq"), p("eq_low_gain"), p("eq_low_q")};
    s.eq[1] = {rr::EqType::Peak, p("eq_mid_freq"), p("eq_mid_gain"), p("eq_mid_q")};
    s.eq[2] = {highBell ? rr::EqType::Peak : rr::EqType::HighShelf, p("eq_high_freq"), p("eq_high_gain"), p("eq_high_q")};
    s.eq[3] = {rr::EqType::HighCut, p("eq_highcut"), 0.0f, 0.707f};
    s.mix = p("mix") * 0.01f;
    s.gainDb = p("gain");
    s.syntheticErGain = syntheticErGain_.load();
    return s;
}

void SeptimaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate;
    if (std::abs(hostRate_.exchange(sampleRate) - sampleRate) > 1.0e-6) {
        // Rebuild the current program at the new rate (the convolver resamples
        // the old package meanwhile).
        reshapeDue_ = juce::Time::currentTimeMillis();
        reshapeRequested_ = true;
        notify();
    }
    maxBlock_ = std::max(32, samplesPerBlock);
    const juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(maxBlock_), 2};
    for (auto* c : {&earlyA_, &earlyB_, &lateA_, &lateB_, &vlfA_, &vlfB_}) c->prepare(spec);
    {
        const int Pb = septima::TailConvolver::partitionSizeFor(sampleRate);
        tailA_.prepare(Pb / 8, sampleRate);
        tailB_.prepare(Pb, sampleRate);
    }
    for (auto* b : {&eA_, &eB_, &lA_, &lB_, &v_, &vB_}) b->setSize(2, maxBlock_);
    for (auto* b : {&feedL_, &feedR_, &feedV_}) b->setSize(1, maxBlock_);
    offlineScratch_.setSize(2, maxBlock_);
    engine_.setSettings(buildSettings(lastBpm_));
    engine_.prepare(sampleRate, maxBlock_);
    dirty_ = false;
}

double SeptimaProcessor::getTailLengthSeconds() const {
    return std::min(60.0, static_cast<double>(p("decay")) * 1.3 * std::max(1.0f, p("low_mult"))
                              + p("predelay") * 0.001 + 0.5);
}

// Accept a newly prepared impulse response (wait-free).
void SeptimaProcessor::acceptPendingPackage() {
    // Return any package we could not park last time.
    if (overflow_ != nullptr) {
        for (auto& slot : retired_) {
            IrPackage* expected = nullptr;
            if (slot.compare_exchange_strong(expected, overflow_)) { overflow_ = nullptr; break; }
        }
    }
    if (auto* pkg = overflow_ == nullptr ? pending_.exchange(nullptr) : nullptr) {
        using C = juce::dsp::Convolution;
        const bool tailMatches = pkg->tailA != nullptr && pkg->tailB != nullptr
            && pkg->tailA->partitionSize == tailA_.partitionSize() && pkg->tailB->partitionSize == tailB_.partitionSize();
        if (tailMatches) {
        expectedIrSize_ = {pkg->earlyA.getNumSamples(), pkg->earlyB.getNumSamples(), pkg->lateA.getNumSamples(),
                           pkg->lateB.getNumSamples(), pkg->vlfA.getNumSamples(), pkg->vlfB.getNumSamples()};
        previousIrSize_ = {earlyA_.getCurrentIRSize(), earlyB_.getCurrentIRSize(), lateA_.getCurrentIRSize(),
                           lateB_.getCurrentIRSize(), vlfA_.getCurrentIRSize(), vlfB_.getCurrentIRSize()};
        convLoadStartMs_ = juce::Time::getMillisecondCounterHiRes();
        convLoadPending_ = true;
        tailA_.setSpectra(pkg->tailA.release());
        tailB_.setSpectra(pkg->tailB.release());
        earlyA_.loadImpulseResponse(std::move(pkg->earlyA), pkg->sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        earlyB_.loadImpulseResponse(std::move(pkg->earlyB), pkg->sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        lateA_.loadImpulseResponse(std::move(pkg->lateA), pkg->sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        lateB_.loadImpulseResponse(std::move(pkg->lateB), pkg->sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        vlfA_.loadImpulseResponse(std::move(pkg->vlfA), pkg->sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        vlfB_.loadImpulseResponse(std::move(pkg->vlfB), pkg->sampleRate, C::Stereo::yes, C::Trim::no, C::Normalise::no);
        syntheticErGain_ = pkg->syntheticErGain;
        irLength_ = pkg->totalLength;
        silentSamples_ = 0;
        dirty_ = true;
        irReady_ = true;
        } // else: built for another sample rate; a rebuild for this rate is already queued
        bool parked = false;
        for (auto& slot : retired_) {
            IrPackage* expected = nullptr;
            if (slot.compare_exchange_strong(expected, pkg)) { parked = true; break; }
        }
        if (!parked) overflow_ = pkg; // retried next block; never deleted on the audio thread
    }

}

// Offline renders (bounces) must not start before the impulse response is in
// place, or the start of the bounce comes out dry/silent. In non-realtime mode
// the audio thread may block, so wait for the worker and for the convolvers'
// background loading to finish (bounded, so a missing library cannot hang).
void SeptimaProcessor::waitUntilReadyForOffline() {
    if (offlineGaveUp_) return;
    // Only while nothing is ringing (before any audio, or once the whole response has
    // died away): then swapping responses or priming the convolvers is inaudible.
    const long long quietFor = static_cast<long long>(irLength_)
        + static_cast<long long>((rr::SeptimaEngine::kMaxPreDelay + rr::SeptimaEngine::kMaxDelay + 0.25) * sampleRate_);
    if (irReady_.load() && silentSamples_ <= quietFor) return;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
    for (;;) {
        acceptPendingPackage();
        // Never stall or disturb the convolvers while anything is sounding (e.g. Decay
        // automated during a bounce); see the check at the top.
        const bool reshapePending = reshapeRequested_.load() || reshapeBusy_.load();
        const bool workerIdle = !workerBusy_.load() && !loadRequested_.load() && !reshapePending
                                && pending_.load() == nullptr;
        bool convReady = true;
        if (convLoadPending_) {
            // The convolvers load new responses on a background thread and swap
            // them in while processing, so keep feeding them silence until each
            // reports the new size (or, if the size did not change, briefly).
            const double waited = juce::Time::getMillisecondCounterHiRes() - convLoadStartMs_;
            const std::array<juce::dsp::Convolution*, 6> convs{&earlyA_, &earlyB_, &lateA_, &lateB_, &vlfA_, &vlfB_};
            bool all = true;
            for (std::size_t i = 0; i < convs.size(); ++i) {
                const int cur = convs[i]->getCurrentIRSize();
                const bool done = expectedIrSize_[i] != previousIrSize_[i] ? cur != previousIrSize_[i] : waited > 300.0;
                if (!done) {
                    all = false;
                    offlineScratch_.clear();
                    juce::dsp::AudioBlock<float> blk(offlineScratch_.getArrayOfWritePointers(), 2,
                                                     static_cast<size_t>(offlineScratch_.getNumSamples()));
                    convs[i]->process(juce::dsp::ProcessContextReplacing<float>(blk));
                }
            }
            if (all || waited > 5000.0) convLoadPending_ = false;
            convReady = !convLoadPending_;
        }
        bool libraryPending = false;
        {
            std::unique_lock lock(mutex_, std::try_to_lock);
            if (lock.owns_lock())
                libraryPending = rescanRequested_ || (requestedKey_.isNotEmpty() && !irReady_.load() && !programs_.empty());
            else
                libraryPending = true;
        }
        if (workerIdle && convReady && !libraryPending) return;
        if (juce::Time::getMillisecondCounterHiRes() > deadline) { offlineGaveUp_ = true; return; }
        notify();
        juce::Thread::sleep(2);
    }
}

void SeptimaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inChannels = getTotalNumInputChannels();
    if (buffer.getNumChannels() < 2) return;
    if (inChannels == 1) buffer.copyFrom(1, 0, buffer, 0, 0, total);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, total);

    if (isNonRealtime()) waitUntilReadyForOffline();
    acceptPendingPackage();

    double bpm = lastBpm_;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    const bool synced = p("predelay_sync") > 0.5f || p("delay_sync") > 0.5f;
    if (dirty_.exchange(false) || (synced && std::abs(bpm - lastBpm_) > 1e-3)) {
        lastBpm_ = bpm;
        engine_.setSettings(buildSettings(bpm));
    }

    const bool ready = irReady_.load();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    float mIn = 0, mE = 0, mL = 0, mV = 0, mO = 0;
    for (int pos = 0; pos < total; pos += maxBlock_) {
        const int n = std::min(maxBlock_, total - pos);
        float* l = L + pos;
        float* r = R + pos;
        float inPeak = 0.0f;
        for (int i = 0; i < n; ++i) inPeak = std::max({inPeak, std::abs(l[i]), std::abs(r[i])});
        silentSamples_ = inPeak > 1.0e-8f ? 0 : silentSamples_ + n;
        const long long quietFor = static_cast<long long>(irLength_)
            + static_cast<long long>((rr::SeptimaEngine::kMaxPreDelay + rr::SeptimaEngine::kMaxDelay + 0.25) * sampleRate_);
        const bool idle = silentSamples_ > quietFor;
        engine_.makeFeeds(l, r, feedL_.getWritePointer(0), feedR_.getWritePointer(0), feedV_.getWritePointer(0), n);
        if (ready && !idle) {
            auto runConv = [n](juce::dsp::Convolution& conv, juce::AudioBuffer<float>& buf, const float* inA, const float* inB) {
                buf.copyFrom(0, 0, inA, n);
                buf.copyFrom(1, 0, inB, n);
                juce::dsp::AudioBlock<float> block(buf.getArrayOfWritePointers(), 2, static_cast<size_t>(n));
                conv.process(juce::dsp::ProcessContextReplacing<float>(block));
            };
            if (engine_.wantsSampledEarly()) {
                runConv(earlyA_, eA_, l, l);
                runConv(earlyB_, eB_, r, r);
            } else {
                eA_.clear(0, n);
                eB_.clear(0, n);
            }
            runConv(lateA_, lA_, feedL_.getReadPointer(0), feedL_.getReadPointer(0));
            runConv(lateB_, lB_, feedR_.getReadPointer(0), feedR_.getReadPointer(0));
            runConv(vlfA_, v_, feedL_.getReadPointer(0), feedL_.getReadPointer(0));
            runConv(vlfB_, vB_, feedR_.getReadPointer(0), feedR_.getReadPointer(0));
            v_.addFrom(0, 0, vB_, 0, 0, n);
            v_.addFrom(1, 0, vB_, 1, 0, n);
            // Sum the true-stereo halves: output L = L->L + R->L, output R = L->R + R->R.
            eA_.addFrom(0, 0, eB_, 0, 0, n);
            eA_.addFrom(1, 0, eB_, 1, 0, n);
            lA_.addFrom(0, 0, lB_, 0, 0, n);
            lA_.addFrom(1, 0, lB_, 1, 0, n);
            // Long tail (background thread) adds into late L/R and VLF L/R.
            float* tailOut[4] = {lA_.getWritePointer(0), lA_.getWritePointer(1), v_.getWritePointer(0), v_.getWritePointer(1)};
            tailB_.setMotion(p("motion") * 0.01f);
            tailA_.process(feedL_.getReadPointer(0), feedR_.getReadPointer(0), tailOut, n);
            tailB_.process(feedL_.getReadPointer(0), feedR_.getReadPointer(0), tailOut, n);
        } else {
            for (auto* b : {&eA_, &lA_, &v_}) b->clear(0, n);
        }
        engine_.finish(l, r, eA_.getReadPointer(0), eA_.getReadPointer(1), lA_.getReadPointer(0), lA_.getReadPointer(1),
                       v_.getReadPointer(0), v_.getReadPointer(1), n);
        mIn = std::max(mIn, engine_.meterIn());
        mE = std::max(mE, engine_.meterEarly());
        mL = std::max(mL, engine_.meterLate());
        mV = std::max(mV, engine_.meterVlf());
        mO = std::max(mO, engine_.meterOut());
    }
    meters.in = std::max(meters.in.load(), mIn);
    meters.early = std::max(meters.early.load(), mE);
    meters.late = std::max(meters.late.load(), mL);
    meters.vlf = std::max(meters.vlf.load(), mV);
    meters.out = std::max(meters.out.load(), mO);
}

juce::AudioProcessorEditor* SeptimaProcessor::createEditor() { return new SeptimaEditor(*this); }

void SeptimaProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = state_.copyState();
    st.setProperty("libraryFolder", libraryFolder().getFullPathName(), nullptr);
    st.setProperty("programKey", currentProgramKey(), nullptr);
    st.setProperty("presetName", presets_->currentName(), nullptr);
    st.setProperty("engineVersion", engineVersion_.load(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}

void SeptimaProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(state_.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    const juce::File folder(tree.getProperty("libraryFolder").toString());
    const juce::String key = tree.getProperty("programKey").toString();
    presets_->setCurrentName(tree.getProperty("presetName", "Default").toString());
    // Sessions from 1.0.0 carry no version: they are engine version 1.
    engineVersion_ = juce::jlimit(1, kEngineVersion, static_cast<int>(tree.getProperty("engineVersion", 1)));
    state_.replaceState(tree);
    rrui::syncParametersToState(state_);
    {
        std::lock_guard lock(mutex_);
        if (folder.isDirectory() && folder != libraryFolder_) {
            libraryFolder_ = folder;
            rescanRequested_ = true;
        }
        requestedKey_ = key;
        applySampledDecay_ = false;
    }
    if (key.isNotEmpty()) loadRequested_ = true;
    dirty_ = true;
    notify();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SeptimaProcessor(); }

// Compiled as part of this translation unit (keeps the build file list unchanged).
#include "TailConvolver.cpp"
