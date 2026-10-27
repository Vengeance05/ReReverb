#include "VistaProcessor.h"
#include "VistaEditor.h"

using rrui::Unit;

namespace {

const juce::StringArray kPatterns{"Room", "Studio", "Chamber", "Hall", "Stage", "Cathedral", "Scattered", "Nonlin U"};
const juce::StringArray kSlopes{"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct"};
const juce::StringArray kReflChorus{"Off", "Fine", "Light", "Moderate"};
const juce::StringArray kRevChorus{"Light", "Moderate", "Ease-in"};
const juce::StringArray kDynModes{"Off", "Duck Reverb", "Duck Wet", "Compress Wet"};

int slopeFromIndex(int i) { return (i + 1) * 6; }

using P = std::pair<juce::String, float>;
using F = rrui::PresetManager::Factory;

// Factory presets (generated table).
#include "VistaFactoryPresets.inc"

std::vector<F> factoryPresets() {
    std::vector<F> out;
    out.reserve(std::size(kFactoryEntries) + 1);
    out.push_back({"Init", "Basics", {}, {}});
    for (const auto& e : kFactoryEntries) {
        F f{e.name, e.category, {}, {}};
        f.values.reserve(static_cast<std::size_t>(e.count));
        for (int i = 0; i < e.count; ++i) {
            const auto& v = kFactoryValues[e.first + i];
            f.values.emplace_back(v.id, v.value);
        }
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout VistaProcessor::createLayout() {
    rrui::ParamList l;
    // Reflections
    rrui::addFloat(l, "refl_proximity", "Refl Proximity", 0, 100, 40, Unit::Percent);
    rrui::addFloat(l, "refl_reflectivity", "Refl Reflectivity", 0, 100, 60, Unit::Percent);
    rrui::addFloat(l, "refl_size", "Refl Size", 0, 100, 50, Unit::Percent);
    rrui::addChoice(l, "refl_pattern", "Refl Pattern", kPatterns, 0);
    rrui::addFloat(l, "refl_diffusion", "Refl Diffusion", 0, 100, 50, Unit::Percent);
    rrui::addFloat(l, "refl_rolloff", "Refl Roll-off", 1000, 20000, 10000, Unit::Hz, 6000);
    rrui::addChoice(l, "refl_slope", "Refl Roll-off Slope", kSlopes, 0);
    rrui::addFloat(l, "refl_mod", "Refl Modulation", 0, 100, 20, Unit::Percent);
    rrui::addChoice(l, "refl_chorus", "Refl Chorus", kReflChorus, 0);
    rrui::addFloat(l, "refl_xfeed", "Refl X-Feed", 0, 100, 50, Unit::Percent);
    rrui::addFloat(l, "refl_xfeed_delay", "Refl X-Feed Delay", 0, 100, 0, Unit::Ms, 20);
    rrui::addFloat(l, "refl_xfeed_rolloff", "Refl X-Feed Roll-off", 1000, 20000, 20000, Unit::Hz, 6000);
    // Reverb
    rrui::addFloat(l, "rev_time", "Reverb Time", 0.2f, 45.0f, 2.5f, Unit::Seconds, 3.0f);
    rrui::addBool(l, "rev_infinite", "Reverb Infinite", false);
    // Frequency-dependent decay (reverb time multipliers below / above two crossovers).
    rrui::addFloat(l, "rev_bass_mult", "Reverb Bass Multiply", 0.25f, 4.0f, 1.0f, Unit::Multiplier, 1.0f);
    rrui::addFloat(l, "rev_bass_freq", "Reverb Bass Crossover", 50, 2000, 500, Unit::Hz, 400);
    rrui::addFloat(l, "rev_treble_mult", "Reverb Treble Multiply", 0.1f, 2.0f, 0.85f, Unit::Multiplier, 0.8f);
    rrui::addFloat(l, "rev_treble_freq", "Reverb Treble Crossover", 1000, 16000, 6000, Unit::Hz, 5000);
    rrui::addFloat(l, "rev_lowboost", "Reverb Low Boost", 0, 12, 0, Unit::Db);
    rrui::addFloat(l, "rev_predelay", "Reverb Pre-delay", 0, 500, 20, Unit::Ms, 80);
    rrui::addChoice(l, "rev_predelay_sync", "Pre-delay Sync", rrui::syncChoices(), 0);
    rrui::addFloat(l, "rev_bloom", "Reverb Bloom", 0, 100, 30, Unit::Percent);
    rrui::addFloat(l, "rev_undulation", "Reverb Undulation", 0, 100, 0, Unit::Percent);
    rrui::addFloat(l, "rev_size", "Reverb Size", 0, 100, 50, Unit::Percent);
    rrui::addFloat(l, "rev_rolloff", "Reverb Roll-off", 1000, 20000, 9000, Unit::Hz, 6000);
    rrui::addChoice(l, "rev_slope", "Reverb Roll-off Slope", kSlopes, 1);
    rrui::addFloat(l, "rev_mod", "Reverb Modulation", 0, 100, 30, Unit::Percent);
    rrui::addChoice(l, "rev_chorus", "Reverb Chorus", kRevChorus, 0);
    rrui::addFloat(l, "rev_xfeed", "Reverb X-Feed", 0, 100, 70, Unit::Percent);
    rrui::addFloat(l, "rev_xfeed_delay", "Reverb X-Feed Delay", 0, 100, 0, Unit::Ms, 20);
    rrui::addFloat(l, "rev_xfeed_rolloff", "Reverb X-Feed Roll-off", 1000, 20000, 20000, Unit::Hz, 6000);
    rrui::addFloat(l, "rev_echo_time", "Echo Time", 100, 2000, 250, Unit::Ms, 500);
    rrui::addChoice(l, "rev_echo_sync", "Echo Sync", rrui::syncChoices(), 0);
    rrui::addFloat(l, "rev_echo_level", "Echo Level", 0, 100, 0, Unit::Percent);
    rrui::addFloat(l, "rev_echo_rolloff", "Echo Roll-off", 1000, 20000, 8000, Unit::Hz, 6000);
    // Global
    rrui::addFloat(l, "width", "Width", 0, 100, 100, Unit::Percent);
    rrui::addFloat(l, "balance", "Reflections / Reverb", 0, 100, 50, Unit::Percent);
    rrui::addFloat(l, "mix", "Mix", 0, 100, 100, Unit::Percent);
    rrui::addFloat(l, "gain", "Gain", -24, 12, 0, Unit::Db);
    // EQ
    rrui::addBool(l, "eq_on", "EQ On", true);
    rrui::addFloat(l, "eq_lowcut", "EQ Low Cut", 20, 500, 20, Unit::Hz, 100);
    rrui::addFloat(l, "eq_ls_freq", "EQ Low Shelf Freq", 40, 1000, 200, Unit::Hz, 200);
    rrui::addFloat(l, "eq_ls_gain", "EQ Low Shelf Gain", -18, 18, 0, Unit::Db);
    rrui::addFloat(l, "eq_ls_q", "EQ Low Shelf Q", 0.3f, 2.0f, 0.707f, Unit::None);
    rrui::addFloat(l, "eq_mid_freq", "EQ Mid Freq", 100, 10000, 1000, Unit::Hz, 1000);
    rrui::addFloat(l, "eq_mid_gain", "EQ Mid Gain", -18, 18, 0, Unit::Db);
    rrui::addFloat(l, "eq_mid_q", "EQ Mid Q", 0.3f, 8.0f, 0.707f, Unit::None, 1.5f);
    rrui::addFloat(l, "eq_hs_freq", "EQ High Shelf Freq", 1000, 16000, 6000, Unit::Hz, 5000);
    rrui::addFloat(l, "eq_hs_gain", "EQ High Shelf Gain", -18, 18, 0, Unit::Db);
    rrui::addFloat(l, "eq_hs_q", "EQ High Shelf Q", 0.3f, 2.0f, 0.707f, Unit::None);
    rrui::addFloat(l, "eq_rolloff", "EQ Roll-off", 1000, 20000, 20000, Unit::Hz, 6000);
    // Dynamics
    rrui::addChoice(l, "dyn_mode", "Dynamics Mode", kDynModes, 0);
    rrui::addFloat(l, "dyn_threshold", "Dyn Threshold", -60, 0, -24, Unit::Db);
    rrui::addFloat(l, "dyn_ratio", "Dyn Ratio", 1, 20, 4, Unit::Multiplier, 4);
    rrui::addFloat(l, "dyn_knee", "Dyn Knee", 0, 12, 6, Unit::Db);
    rrui::addFloat(l, "dyn_attack", "Dyn Attack", 0.1f, 200, 10, Unit::Ms, 15);
    rrui::addFloat(l, "dyn_release", "Dyn Release", 10, 2000, 250, Unit::Ms, 250);
    rrui::addFloat(l, "dyn_trim", "Dyn Trim", -12, 12, 0, Unit::Db);
    rrui::addBool(l, "gate_on", "Gate On", false);
    rrui::addFloat(l, "gate_threshold", "Gate Threshold", -60, 0, -20, Unit::Db);
    rrui::addFloat(l, "gate_hold", "Gate Hold", 0, 500, 50, Unit::Ms, 100);
    rrui::addFloat(l, "gate_range", "Gate Range", 0, 60, 25, Unit::Db);
    rrui::addFloat(l, "gate_close", "Gate Close", 10, 2000, 200, Unit::Ms, 250);
    rrui::addFloat(l, "gate_open", "Gate Open", 0.1f, 200, 10, Unit::Ms, 15);
    rrui::addFloat(l, "gate_hysteresis", "Gate Hysteresis", 0, 12, 3, Unit::Db);
    return {l.begin(), l.end()};
}

VistaProcessor::VistaProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "VistaRooms", createLayout()) {
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            ids_.push_back(rp->paramID);
            state_.addParameterListener(rp->paramID, this);
        }
    presets_ = std::make_unique<rrui::PresetManager>(state_, "Vista Rooms", factoryPresets());
}

VistaProcessor::~VistaProcessor() {
    for (const auto& id : ids_) state_.removeParameterListener(id, this);
}

bool VistaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

float VistaProcessor::p(const char* id) const {
    return state_.getRawParameterValue(id)->load();
}

rr::VistaSettings VistaProcessor::buildSettings(double bpm) const {
    rr::VistaSettings s;
    auto pct = [this](const char* id) { return p(id) * 0.01f; };
    auto& er = s.er;
    er.pattern = static_cast<rr::ErPattern>(static_cast<int>(p("refl_pattern")));
    er.size = pct("refl_size");
    er.proximity = pct("refl_proximity");
    er.reflectivity = pct("refl_reflectivity");
    er.diffusion = pct("refl_diffusion");
    er.modDepth = pct("refl_mod");
    er.chorus = static_cast<rr::ChorusMode>(static_cast<int>(p("refl_chorus")));
    er.crossfeed = pct("refl_xfeed");
    er.crossfeedDelayMs = p("refl_xfeed_delay");
    er.crossfeedRolloffHz = p("refl_xfeed_rolloff");
    er.width = pct("width");
    er.rolloffHz = p("refl_rolloff");
    er.rolloffSlope = slopeFromIndex(static_cast<int>(p("refl_slope")));

    auto& lt = s.late;
    lt.rt60 = p("rev_time");
    lt.infinite = p("rev_infinite") > 0.5f;
    lt.bassMult = p("rev_bass_mult");
    lt.bassHz = p("rev_bass_freq");
    lt.trebleMult = p("rev_treble_mult");
    lt.trebleHz = p("rev_treble_freq");
    lt.lowBoostDb = p("rev_lowboost");
    const double preSync = rrui::syncSeconds(static_cast<int>(p("rev_predelay_sync")), bpm);
    lt.preDelayMs = preSync > 0 ? static_cast<float>(std::min(500.0, preSync * 1000.0)) : p("rev_predelay");
    lt.bloom = pct("rev_bloom");
    lt.undulation = pct("rev_undulation");
    lt.size = pct("rev_size");
    lt.modDepth = pct("rev_mod");
    lt.chorus = static_cast<rr::ChorusMode>(2 + static_cast<int>(p("rev_chorus")));
    lt.crossfeed = pct("rev_xfeed");
    lt.crossfeedDelayMs = p("rev_xfeed_delay");
    lt.crossfeedRolloffHz = p("rev_xfeed_rolloff");
    lt.width = pct("width");
    lt.rolloffHz = p("rev_rolloff");
    lt.rolloffSlope = slopeFromIndex(static_cast<int>(p("rev_slope")));
    const double echoSync = rrui::syncSeconds(static_cast<int>(p("rev_echo_sync")), bpm);
    lt.echoMs = echoSync > 0 ? static_cast<float>(std::clamp(echoSync * 1000.0, 100.0, 2000.0)) : p("rev_echo_time");
    lt.echoLevel = pct("rev_echo_level");
    lt.echoRolloffHz = p("rev_echo_rolloff");

    s.reflReverbMix = pct("balance");
    s.mix = pct("mix");
    s.gainDb = p("gain");

    s.eqOn = p("eq_on") > 0.5f;
    s.eq[0] = {p("eq_lowcut") <= 20.5f ? rr::EqType::Off : rr::EqType::LowCut, p("eq_lowcut"), 0.0f, 0.707f};
    s.eq[1] = {rr::EqType::LowShelf, p("eq_ls_freq"), p("eq_ls_gain"), p("eq_ls_q")};
    s.eq[2] = {rr::EqType::Peak, p("eq_mid_freq"), p("eq_mid_gain"), p("eq_mid_q")};
    s.eq[3] = {rr::EqType::HighShelf, p("eq_hs_freq"), p("eq_hs_gain"), p("eq_hs_q")};
    s.eq[4] = {rr::EqType::HighCut, p("eq_rolloff"), 0.0f, 0.707f};

    auto& d = s.dynamics;
    d.mode = static_cast<rr::DynamicsMode>(static_cast<int>(p("dyn_mode")));
    d.thresholdDb = p("dyn_threshold");
    d.ratio = p("dyn_ratio");
    d.kneeDb = p("dyn_knee");
    d.attackMs = p("dyn_attack");
    d.releaseMs = p("dyn_release");
    d.makeupDb = p("dyn_trim");
    d.gateOn = p("gate_on") > 0.5f;
    d.gateThresholdDb = p("gate_threshold");
    d.gateHoldMs = p("gate_hold");
    d.gateRangeDb = p("gate_range");
    d.gateCloseMs = p("gate_close");
    d.gateOpenMs = p("gate_open");
    d.gateHysteresisDb = p("gate_hysteresis");
    return s;
}

void VistaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.setSettings(buildSettings(lastBpm_));
    engine_.prepare(sampleRate, std::max(32, samplesPerBlock));
    dirty_ = false;
}

double VistaProcessor::getTailLengthSeconds() const {
    if (p("rev_infinite") > 0.5f) return 60.0;
    return std::min(60.0, static_cast<double>(p("rev_time")) * 1.3 + p("rev_predelay") * 0.001 + 0.8);
}

void VistaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inChannels = getTotalNumInputChannels();
    if (inChannels == 1 && buffer.getNumChannels() > 1) buffer.copyFrom(1, 0, buffer, 0, 0, n);
    for (int ch = std::max(inChannels, 2); ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, n);

    double bpm = lastBpm_;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    const bool synced = p("rev_predelay_sync") > 0.5f || p("rev_echo_sync") > 0.5f;
    if (dirty_.exchange(false) || (synced && std::abs(bpm - lastBpm_) > 1e-3)) {
        lastBpm_ = bpm;
        engine_.setSettings(buildSettings(bpm));
    }
    if (buffer.getNumChannels() < 2) return;
    engine_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    meters.in = std::max(meters.in.load(), engine_.meterIn());
    meters.refl = std::max(meters.refl.load(), engine_.meterRefl());
    meters.reverb = std::max(meters.reverb.load(), engine_.meterReverb());
    meters.out = std::max(meters.out.load(), engine_.meterOut());
    meters.duck = engine_.duckReductionDb();
}

juce::AudioProcessorEditor* VistaProcessor::createEditor() { return new VistaEditor(*this); }

void VistaProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto st = state_.copyState();
    st.setProperty("presetName", presets_->currentName(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
}

void VistaProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (xml->hasTagName(state_.state.getType())) {
            auto tree = juce::ValueTree::fromXml(*xml);
            presets_->setCurrentName(tree.getProperty("presetName", "Default").toString());
            state_.replaceState(tree);
            rrui::syncParametersToState(state_);
            dirty_ = true;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VistaProcessor(); }
