#include "VistaEditor.h"

namespace Colours = rrui::Colours;
using rrui::KnobSize;
using rrui::Weight;

namespace {
const juce::Colour kAccent{0xff4cc9c0}; // teal
constexpr int kWidth = 1060, kHeight = 720;

class GroupCaptions : public juce::Component {
public:
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> groups;
    GroupCaptions() { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        for (const auto& [r, t] : groups) rrui::paintGroupLabel(g, r, t);
    }
};
} // namespace

// ============================================================================
VistaDisplay::VistaDisplay(juce::AudioProcessorValueTreeState& s, juce::Colour accent) : state_(s), accent_(accent) {
    startTimerHz(20);
}

void VistaDisplay::timerCallback() {
    recomputeTaps();
    repaint();
}

void VistaDisplay::recomputeTaps() {
    auto v = [this](const char* id) { return state_.getRawParameterValue(id)->load(); };
    rr::ErSettings er;
    er.pattern = static_cast<rr::ErPattern>(static_cast<int>(v("refl_pattern")));
    er.size = v("refl_size") * 0.01f;
    er.proximity = v("refl_proximity") * 0.01f;
    er.reflectivity = v("refl_reflectivity") * 0.01f;
    er.crossfeed = v("refl_xfeed") * 0.01f;
    er.crossfeedDelayMs = v("refl_xfeed_delay");
    if (first_ || !(er == lastEr_)) {
        rr::EarlyReflections::previewTaps(er, 48000.0, taps_);
        lastEr_ = er;
        first_ = false;
    }
}

void VistaDisplay::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    rrui::paintDisplayGlass(g, r, accent_);
    auto v = [this](const char* id) { return state_.getRawParameterValue(id)->load(); };

    // ---- Graph: sqrt time axis so reflections and long tails are both readable.
    auto area = r.reduced(18.0f, 14.0f).withWidth(r.getWidth() - 360.0f);
    g.setColour(Colours::textDim);
    g.setFont(rrui::capsFont(9.0f));
    g.drawText("REFLECTIONS  +  REVERB ENVELOPE", area.removeFromTop(14), juce::Justification::left, false);
    area.removeFromTop(4);
    auto plot = area.withTrimmedBottom(12).withTrimmedLeft(28);
    const bool infinite = v("rev_infinite") > 0.5f;
    const float rt = v("rev_time");
    const bool synced = v("rev_predelay_sync") > 0.5f;
    const float pre = (synced ? static_cast<float>(rrui::syncSeconds(juce::roundToInt(v("rev_predelay_sync")), 120.0)) : v("rev_predelay") * 0.001f);
    const float tMax = juce::jlimit(0.4f, 14.0f, pre + (infinite ? 6.0f : rt) * 1.05f + 0.05f);
    auto xFor = [&](float t) { return plot.getX() + std::sqrt(juce::jlimit(0.0f, 1.0f, t / tMax)) * plot.getWidth(); };
    auto yFor = [&](float db) { return plot.getY() + juce::jlimit(0.0f, 1.0f, -db / 66.0f) * plot.getHeight(); };

    g.setFont(rrui::uiFont(9.0f));
    for (float db : {-12.0f, -24.0f, -36.0f, -48.0f, -60.0f}) {
        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.drawHorizontalLine(juce::roundToInt(yFor(db)), plot.getX(), plot.getRight());
        g.setColour(Colours::textDim);
        g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(plot.getX() - 30, yFor(db) - 6, 26, 12), juce::Justification::right, false);
    }
    for (float t : {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f}) {
        if (t > tMax) break;
        g.setColour(juce::Colours::white.withAlpha(0.045f));
        g.drawVerticalLine(juce::roundToInt(xFor(t)), plot.getY(), plot.getBottom());
        g.setColour(Colours::textDim);
        const auto label = t < 1.0f ? juce::String(juce::roundToInt(t * 1000)) + "ms" : juce::String(juce::roundToInt(t)) + "s";
        g.drawText(label, juce::Rectangle<float>(xFor(t) - 20, plot.getBottom() + 1, 40, 11), juce::Justification::centred, false);
    }

    // Late envelope (mid band and, dimmer, the contour-shaped high band).
    const float bal = v("balance") * 0.01f;
    const float lateDb = 20.0f * std::log10(std::max(0.02f, std::min(1.0f, 2.0f * bal)));
    const float erDb = 20.0f * std::log10(std::max(0.02f, std::min(1.0f, 2.0f * (1.0f - bal))));
    const float bloomT = 0.02f + v("rev_bloom") * 0.01f * 0.25f;
    const float mHigh = v("rev_treble_mult");
    auto envelope = [&](float rtBand, bool closed) {
        juce::Path p;
        const float startDb = -66.0f, peakDb = lateDb - 6.0f;
        p.startNewSubPath(xFor(pre), yFor(startDb));
        for (int i = 0; i <= 120; ++i) {
            const float t = pre + (tMax - pre) * static_cast<float>(i) / 120.0f;
            const float age = t - pre;
            float db = age < bloomT ? startDb + (peakDb - startDb) * std::sqrt(age / bloomT)
                                    : peakDb - (infinite ? 0.0f : 60.0f * (age - bloomT) / std::max(0.05f, rtBand));
            p.lineTo(xFor(t), yFor(std::max(db, -66.0f)));
        }
        if (closed) { p.lineTo(xFor(tMax), yFor(-66.0f)); p.closeSubPath(); }
        return p;
    };
    juce::ColourGradient fill(accent_.withAlpha(0.32f), 0, plot.getY(), accent_.withAlpha(0.03f), 0, plot.getBottom(), false);
    g.setGradientFill(fill);
    g.fillPath(envelope(rt, true));
    g.setColour(accent_);
    g.strokePath(envelope(rt, false), juce::PathStrokeType(1.6f));
    if (std::abs(mHigh - 1.0f) > 0.02f && !infinite) {
        g.setColour(accent_.withAlpha(0.45f));
        const float dashes[] = {3.0f, 3.0f};
        juce::Path d;
        juce::PathStrokeType(1.0f).createDashedStroke(d, envelope(rt * mHigh, false), dashes, 2);
        g.fillPath(d);
    }

    // Reflection stems
    float gmax = 1e-6f;
    for (const auto& [t, gain] : taps_) gmax = std::max(gmax, std::abs(gain));
    for (const auto& [t, gain] : taps_) {
        const float db = erDb + 20.0f * std::log10(std::max(1e-4f, std::abs(gain) / gmax)) - 3.0f;
        if (db < -64.0f) continue;
        const float x = xFor(t);
        g.setColour(Colours::textMid.withAlpha(0.35f));
        g.drawLine(x, yFor(-66.0f), x, yFor(db), 1.0f);
        g.setColour(Colours::text.withAlpha(0.8f));
        g.fillEllipse(x - 1.5f, yFor(db) - 1.5f, 3.0f, 3.0f);
    }

    // ---- Readouts
    const float rx = r.getRight() - 330.0f;
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawVerticalLine(juce::roundToInt(rx - 14), 16.0f, r.getBottom() - 16.0f);
    auto readout = [&](float x, float y, const char* label, const juce::String& value, float size, juce::Colour col) {
        g.setColour(Colours::textDim);
        g.setFont(rrui::capsFont(9.5f));
        g.drawText(label, juce::Rectangle<float>(x, y, 160, 12), juce::Justification::left, false);
        g.setColour(col);
        g.setFont(rrui::uiFont(size, Weight::Medium));
        g.drawText(value, juce::Rectangle<float>(x, y + 13, 170, size + 6), juce::Justification::left, false);
    };
    readout(rx, 16, "REVERB TIME", infinite ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x9e")) : rrui::formatValue(rt, rrui::Unit::Seconds), 30.0f, accent_);
    readout(rx + 170, 16, "PRE-DELAY", synced ? rrui::syncChoices()[juce::roundToInt(v("rev_predelay_sync"))]
                                               : rrui::formatValue(v("rev_predelay"), rrui::Unit::Ms), 17.0f, Colours::text);
    readout(rx, 74, "PATTERN", rr::erPatternName(static_cast<rr::ErPattern>(static_cast<int>(v("refl_pattern")))), 15.0f, Colours::text);
    readout(rx + 170, 74, "REFL / REVERB", juce::String(juce::roundToInt(100 - v("balance"))) + " / " + juce::String(juce::roundToInt(v("balance"))), 15.0f, Colours::text);
}

// ============================================================================
VistaEditor::VistaEditor(VistaProcessor& p)
    : AudioProcessorEditor(p), proc_(p), lnf_(kAccent),
      reflPanel_("Reflections", kAccent), revPanel_("Reverb", kAccent), outPanel_("Output", kAccent),
      masterPanel_("Master", kAccent), presetBar_(p.presets(), kAccent), display_(p.state(), kAccent),
      meters_({"IN", "REFL", "REV", "OUT"}, kAccent),
      eqCurve_(kAccent,
               [this] {
                   auto v = [this](const char* id) { return proc_.state().getRawParameterValue(id)->load(); };
                   const double fs = 48000.0;
                   std::vector<rrui::CurveStage> st;
                   auto add = [&](const rr::BiquadCoeffs& c) { st.push_back({c.b0, c.b1, c.b2, c.a1, c.a2}); };
                   if (v("eq_lowcut") > 20.5f) add(rr::design::highpass(fs, v("eq_lowcut"), 0.7071));
                   add(rr::design::lowShelf(fs, v("eq_ls_freq"), v("eq_ls_q"), v("eq_ls_gain")));
                   add(rr::design::peak(fs, v("eq_mid_freq"), v("eq_mid_q"), v("eq_mid_gain")));
                   add(rr::design::highShelf(fs, v("eq_hs_freq"), v("eq_hs_q"), v("eq_hs_gain")));
                   if (v("eq_rolloff") < 19999.0f) add(rr::design::lowpass(fs, v("eq_rolloff"), 0.7071));
                   return st;
               },
               [this] { return proc_.state().getRawParameterValue("eq_on")->load() > 0.5f; }) {
    setLookAndFeel(&lnf_);
    for (auto* pnl : {&reflPanel_, &revPanel_, &outPanel_, &masterPanel_}) addAndMakeVisible(*pnl);
    addAndMakeVisible(presetBar_);
    addAndMakeVisible(display_);
    p.presets().onPresetLoaded = [this] { presetBar_.refreshMenu(); };

    // Reflections: primary row + tabbed pages
    knob(reflPanel_, "refl_proximity", "Proximity", KnobSize::Large);
    knob(reflPanel_, "refl_reflectivity", "Reflectivity", KnobSize::Large);
    knob(reflPanel_, "refl_size", "Size", KnobSize::Large);
    choice(reflPanel_, "refl_pattern", "Pattern");
    {
        auto& pg = reflPanel_.addPage("Character");
        knob(pg, "refl_diffusion", "Diffusion", KnobSize::Medium);
        knob(pg, "refl_mod", "Modulation", KnobSize::Medium);
        choice(pg, "refl_chorus", "Chorus");
        pg.layout = [this](juce::Rectangle<int> r) {
            rrui::placeFlow(r, {{c("refl_diffusion"), 74}, {c("refl_mod"), 74}}, 4);
            c("refl_chorus")->setBounds(r.getX() + 166, r.getY() + 20, 100, 40);
        };
    }
    {
        auto& pg = reflPanel_.addPage("Tone");
        knob(pg, "refl_rolloff", "Roll-off", KnobSize::Medium);
        choice(pg, "refl_slope", "Slope");
        knob(pg, "width", "Width", KnobSize::Medium);
        pg.layout = [this](juce::Rectangle<int> r) {
            c("refl_rolloff")->setBounds(r.getX(), r.getY(), 74, r.getHeight());
            c("refl_slope")->setBounds(r.getX() + 84, r.getY() + 20, 100, 40);
            c("width")->setBounds(r.getX() + 196, r.getY(), 74, r.getHeight());
        };
    }
    {
        auto& pg = reflPanel_.addPage("X-Feed");
        knob(pg, "refl_xfeed", "Level", KnobSize::Medium);
        knob(pg, "refl_xfeed_delay", "Delay", KnobSize::Medium);
        knob(pg, "refl_xfeed_rolloff", "Roll-off", KnobSize::Medium);
        pg.layout = [this](juce::Rectangle<int> r) {
            rrui::placeFlow(r, {{c("refl_xfeed"), 74}, {c("refl_xfeed_delay"), 74}, {c("refl_xfeed_rolloff"), 74}}, 4);
        };
    }

    // Reverb
    knob(revPanel_, "rev_time", "Time", KnobSize::Large);
    knob(revPanel_, "rev_predelay", "Pre-delay", KnobSize::Large);
    knob(revPanel_, "rev_bloom", "Bloom", KnobSize::Large);
    knob(revPanel_, "rev_size", "Size", KnobSize::Large);
    {
        auto& pg = revPanel_.addPage("Shape");
        knob(pg, "rev_undulation", "Undulation", KnobSize::Medium);
        choice(pg, "rev_predelay_sync", "Pre-delay Sync");
        toggle(pg, "rev_infinite", "Infinite");
        pg.layout = [this](juce::Rectangle<int> r) {
            c("rev_undulation")->setBounds(r.getX(), r.getY(), 74, r.getHeight());
            c("rev_predelay_sync")->setBounds(r.getX() + 90, r.getY() + 20, 110, 40);
            c("rev_infinite")->setBounds(r.getX() + 216, r.getY() + 36, 100, 22);
        };
    }
    {
        auto& pg = revPanel_.addPage("Decay");
        knob(pg, "rev_bass_mult", "Bass x", KnobSize::Medium);
        knob(pg, "rev_bass_freq", "Bass Freq", KnobSize::Medium);
        knob(pg, "rev_treble_mult", "Treble x", KnobSize::Medium);
        knob(pg, "rev_treble_freq", "Treble Freq", KnobSize::Medium);
        pg.layout = [this](juce::Rectangle<int> r) {
            rrui::placeFlow(r, {{c("rev_bass_mult"), 70}, {c("rev_bass_freq"), 70}, {c("rev_treble_mult"), 70}, {c("rev_treble_freq"), 70}}, 2);
        };
    }
    {
        auto& pg = revPanel_.addPage("Tone");
        knob(pg, "rev_lowboost", "Low Boost", KnobSize::Medium);
        knob(pg, "rev_rolloff", "Roll-off", KnobSize::Medium);
        choice(pg, "rev_slope", "Slope");
        pg.layout = [this](juce::Rectangle<int> r) {
            rrui::placeFlow(r, {{c("rev_lowboost"), 70}, {c("rev_rolloff"), 70}}, 2);
            c("rev_slope")->setBounds(r.getX() + 160, r.getY() + 20, 88, 40);
        };
    }
    {
        auto& pg = revPanel_.addPage("Motion");
        knob(pg, "rev_mod", "Modulation", KnobSize::Medium);
        choice(pg, "rev_chorus", "Chorus");
        knob(pg, "rev_xfeed", "X-Feed", KnobSize::Medium);
        knob(pg, "rev_xfeed_delay", "X-Delay", KnobSize::Medium);
        knob(pg, "rev_xfeed_rolloff", "X-Roll-off", KnobSize::Medium);
        pg.layout = [this](juce::Rectangle<int> r) {
            c("rev_mod")->setBounds(r.getX(), r.getY(), 74, r.getHeight());
            c("rev_chorus")->setBounds(r.getX() + 80, r.getY() + 20, 92, 40);
            rrui::placeFlow(r.withTrimmedLeft(180), {{c("rev_xfeed"), 70}, {c("rev_xfeed_delay"), 70}, {c("rev_xfeed_rolloff"), 70}}, 2);
        };
    }
    {
        auto& pg = revPanel_.addPage("Echo");
        knob(pg, "rev_echo_time", "Time", KnobSize::Medium);
        choice(pg, "rev_echo_sync", "Sync");
        knob(pg, "rev_echo_level", "Level", KnobSize::Medium);
        knob(pg, "rev_echo_rolloff", "Roll-off", KnobSize::Medium);
        pg.layout = [this](juce::Rectangle<int> r) {
            c("rev_echo_time")->setBounds(r.getX(), r.getY(), 74, r.getHeight());
            c("rev_echo_sync")->setBounds(r.getX() + 82, r.getY() + 20, 90, 40);
            rrui::placeFlow(r.withTrimmedLeft(184), {{c("rev_echo_level"), 74}, {c("rev_echo_rolloff"), 74}}, 4);
        };
    }

    // Output
    knob(outPanel_, "balance", "Refl / Rev", KnobSize::Medium, true);
    knob(outPanel_, "mix", "Mix", KnobSize::Medium);
    knob(outPanel_, "gain", "Gain", KnobSize::Medium, true);
    outPanel_.addAndMakeVisible(meters_);

    // Master: EQ + dynamics pages
    {
        auto& pg = masterPanel_.addPage("Equaliser");
        auto* caps = new GroupCaptions();
        extras_.emplace_back(caps);
        pg.addAndMakeVisible(caps);
        pg.addAndMakeVisible(eqCurve_);
        toggle(pg, "eq_on", "EQ On");
        static const char* ids[] = {"eq_lowcut", "eq_ls_freq", "eq_ls_gain", "eq_ls_q", "eq_mid_freq", "eq_mid_gain",
                                    "eq_mid_q", "eq_hs_freq", "eq_hs_gain", "eq_hs_q", "eq_rolloff"};
        static const char* names[] = {"Freq", "Freq", "Gain", "Q", "Freq", "Gain", "Q", "Freq", "Gain", "Q", "Freq"};
        for (int i = 0; i < 11; ++i) knob(pg, ids[i], names[i], KnobSize::Small, juce::String(ids[i]).endsWith("gain"));
        pg.layout = [this, caps](juce::Rectangle<int> r) {
            const auto r0 = r;
            caps->setBounds(r);
            auto left = r.removeFromLeft(184);
            c("eq_on")->setBounds(left.getX(), left.getY(), 78, 22);
            eqCurve_.setBounds(left.withTrimmedTop(28));
            r.removeFromLeft(18);
            const int cw = 48, gap = 12, ky = r.getY() + 16, kh = r.getHeight() - 16;
            int x = r.getX();
            caps->groups.clear();
            auto group = [&](const char* title, std::initializer_list<const char*> items) {
                const int w = static_cast<int>(items.size()) * cw;
                caps->groups.push_back({juce::Rectangle<int>(x - r0.getX(), 0, w, 12), title});
                int cx = x;
                for (auto* id : items) { c(id)->setBounds(cx, ky, cw, kh); cx += cw; }
                x += w + gap;
            };
            group("Low Cut", {"eq_lowcut"});
            group("Low Shelf", {"eq_ls_freq", "eq_ls_gain", "eq_ls_q"});
            group("Mid", {"eq_mid_freq", "eq_mid_gain", "eq_mid_q"});
            group("High Shelf", {"eq_hs_freq", "eq_hs_gain", "eq_hs_q"});
            group("Roll-off", {"eq_rolloff"});
            caps->repaint();
        };
    }
    {
        auto& pg = masterPanel_.addPage("Dynamics");
        auto* caps = new GroupCaptions();
        extras_.emplace_back(caps);
        pg.addAndMakeVisible(caps);
        choice(pg, "dyn_mode", "Mode");
        toggle(pg, "gate_on", "Gate On");
        static const char* ids[] = {"dyn_threshold", "dyn_ratio", "dyn_knee", "dyn_attack", "dyn_release", "dyn_trim",
                                    "gate_threshold", "gate_hold", "gate_range", "gate_close", "gate_open", "gate_hysteresis"};
        static const char* names[] = {"Thresh", "Ratio", "Knee", "Attack", "Release", "Trim",
                                      "Thresh", "Hold", "Range", "Close", "Open", "Hyst"};
        for (int i = 0; i < 12; ++i) knob(pg, ids[i], names[i], KnobSize::Small, juce::String(ids[i]) == "dyn_trim");
        pg.layout = [this, caps](juce::Rectangle<int> r) {
            const auto r0 = r;
            caps->setBounds(r);
            caps->groups.clear();
            const int cw = 50, ky = r.getY() + 16, kh = r.getHeight() - 16;
            auto left = r.removeFromLeft(r.getWidth() / 2 - 8);
            r.removeFromLeft(16);
            caps->groups.push_back({{left.getX() - r0.getX(), 0, left.getWidth(), 12}, "Ducking / Compression"});
            caps->groups.push_back({{r.getX() - r0.getX(), 0, r.getWidth(), 12}, "Tail Gate"});
            c("dyn_mode")->setBounds(left.getX(), ky + 14, 92, 40);
            int x = left.getX() + 98;
            for (int i = 0; i < 6; ++i) { c(ids[i])->setBounds(x, ky, cw, kh); x += cw; }
            c("gate_on")->setBounds(r.getX(), ky + 30, 84, 22);
            x = r.getX() + 92;
            for (int i = 6; i < 12; ++i) { c(ids[i])->setBounds(x, ky, cw, kh); x += cw; }
            caps->repaint();
        };
    }

    setSize(kWidth, kHeight);
    startTimerHz(30);
}

VistaEditor::~VistaEditor() {
    proc_.presets().onPresetLoaded = nullptr;
    setLookAndFeel(nullptr);
}

rrui::Knob& VistaEditor::knob(juce::Component& parent, const juce::String& id, const juce::String& label,
                              KnobSize size, bool bipolar) {
    auto k = std::make_unique<rrui::Knob>(proc_.state(), id, label, size, bipolar);
    auto& ref = *k;
    parent.addAndMakeVisible(ref);
    controls_[id] = std::move(k);
    return ref;
}

rrui::Choice& VistaEditor::choice(juce::Component& parent, const juce::String& id, const juce::String& label) {
    auto k = std::make_unique<rrui::Choice>(proc_.state(), id, label);
    auto& ref = *k;
    parent.addAndMakeVisible(ref);
    controls_[id] = std::move(k);
    return ref;
}

rrui::Switch& VistaEditor::toggle(juce::Component& parent, const juce::String& id, const juce::String& text) {
    auto k = std::make_unique<rrui::Switch>(proc_.state(), id, text);
    auto& ref = *k;
    parent.addAndMakeVisible(ref);
    controls_[id] = std::move(k);
    return ref;
}

void VistaEditor::paint(juce::Graphics& g) {
    juce::ColourGradient bg(juce::Colour(0xff111215), 0, 0, Colours::background, 0, static_cast<float>(getHeight()), false);
    g.setGradientFill(bg);
    g.fillAll();
    auto header = getLocalBounds().removeFromTop(56);
    g.setColour(Colours::header);
    g.fillRect(header);
    g.setColour(Colours::outline);
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    auto wm = header.reduced(20, 8).withWidth(280);
    g.setColour(Colours::text);
    g.setFont(rrui::uiFont(19.0f, Weight::SemiBold).withExtraKerningFactor(0.32f));
    g.drawText("VISTA ROOMS", wm.removeFromTop(wm.getHeight() / 2 + 4).withTrimmedTop(6), juce::Justification::bottomLeft, false);
    g.setColour(kAccent.withAlpha(0.85f));
    g.setFont(rrui::capsFont(8.5f).withExtraKerningFactor(0.22f));
    g.drawText("CINEMATIC ALGORITHMIC REVERB", wm.withTrimmedTop(1), juce::Justification::topLeft, false);
}

void VistaEditor::resized() {
    presetBar_.setBounds(320, 13, 440, 30);
    display_.setBounds(16, 64, 1028, 140);

    using rrui::Knob;
    const int lw = Knob::cellWidth(KnobSize::Large), lh = Knob::cellHeight(KnobSize::Large);
    const int mw = Knob::cellWidth(KnobSize::Medium), mh = Knob::cellHeight(KnobSize::Medium);

    reflPanel_.setBounds(16, 212, 412, 278);
    rrui::placeRow({14, 34, 384, lh}, {c("refl_proximity"), c("refl_reflectivity"), c("refl_size"), c("refl_pattern")}, lw);
    c("refl_pattern")->setBounds(c("refl_pattern")->getX(), 34 + 30, lw, 40);
    reflPanel_.setTabArea({14, 34 + lh + 10, 384, 278 - (34 + lh + 10) - 10});

    revPanel_.setBounds(436, 212, 410, 278);
    rrui::placeRow({14, 34, 382, lh}, {c("rev_time"), c("rev_predelay"), c("rev_bloom"), c("rev_size")}, lw);
    revPanel_.setTabArea({14, 34 + lh + 10, 382, 278 - (34 + lh + 10) - 10});

    outPanel_.setBounds(854, 212, 190, 492);
    rrui::placeRow({12, 34, 166, mh}, {c("balance"), c("mix")}, mw);
    c("gain")->setBounds(12 + (166 - mw) / 2, 34 + mh + 8, mw, mh);
    meters_.setBounds(18, 34 + 2 * mh + 24, 156, 492 - (34 + 2 * mh + 24) - 14);

    masterPanel_.setBounds(16, 498, 830, 206);
    masterPanel_.setTabArea({14, 32, 802, 206 - 32 - 10});
}

void VistaEditor::timerCallback() {
    auto take = [](std::atomic<float>& a) { return a.exchange(0.0f); };
    meters_.setLevels({take(proc_.meters.in), take(proc_.meters.refl), take(proc_.meters.reverb), take(proc_.meters.out)});
}
