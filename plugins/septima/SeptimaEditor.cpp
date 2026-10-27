#include "SeptimaEditor.h"

namespace Colours = rrui::Colours;
using rrui::KnobSize;
using rrui::Weight;

namespace {
const juce::Colour kAccent{0xffeaa64d}; // warm amber
constexpr int kWidth = 1000, kHeight = 708;

void paintWordmark(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name, const juce::String& sub, juce::Colour accent) {
    g.setColour(Colours::text);
    g.setFont(rrui::uiFont(19.0f, Weight::SemiBold).withExtraKerningFactor(0.32f));
    g.drawText(name, r.removeFromTop(r.getHeight() / 2 + 4).withTrimmedTop(6), juce::Justification::bottomLeft, false);
    g.setColour(accent.withAlpha(0.85f));
    g.setFont(rrui::capsFont(8.5f).withExtraKerningFactor(0.22f));
    g.drawText(sub, r.withTrimmedTop(1), juce::Justification::topLeft, false);
}
} // namespace

// ============================================================================
SeptimaDisplay::SeptimaDisplay(SeptimaProcessor& p, juce::Colour accent) : proc_(p), accent_(accent) {
    addAndMakeVisible(browse);
    browse.setTooltip("Browse the impulse response library");
}

void SeptimaDisplay::resized() {
    browse.setBounds(20, getHeight() - 38, 150, 24);
}

void SeptimaDisplay::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    rrui::paintDisplayGlass(g, r, accent_);
    auto v = [this](const char* id) { return proc_.state().getRawParameterValue(id)->load(); };

    // --- Program block
    juce::String category = "NO PROGRAM", name = "Choose an IR library", info;
    if (status_.ready || status_.sampledDecay > 0.0f) {
        const auto key = proc_.currentProgramKey();
        for (const auto& pr : proc_.programs())
            if (juce::String(pr.key) == key) { category = juce::String(pr.category).toUpperCase(); name = pr.displayName(); }
        info << status_.format << "   " << juce::String(status_.sampleRate / 1000.0, 1) << " kHz";
    }
    if (status_.busy) info = status_.text;
    g.setColour(accent_);
    g.setFont(rrui::capsFont(10.5f).withExtraKerningFactor(0.18f));
    g.drawText(category, 20, 18, 360, 14, juce::Justification::left, false);
    g.setColour(Colours::text);
    g.setFont(rrui::uiFont(24.0f, Weight::Medium));
    g.drawText(name, 20, 34, 360, 32, juce::Justification::left, true);
    g.setColour(Colours::textDim);
    g.setFont(rrui::uiFont(11.5f));
    g.drawText(info, 20, 68, 360, 16, juce::Justification::left, true);

    // Dividers
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawVerticalLine(392, 16.0f, r.getBottom() - 16.0f);
    g.drawVerticalLine(632, 16.0f, r.getBottom() - 16.0f);

    // --- Readouts
    auto readout = [&](int y, const char* label, const juce::String& value, float size, juce::Colour col) {
        g.setColour(Colours::textDim);
        g.setFont(rrui::capsFont(9.5f));
        g.drawText(label, 412, y, 200, 12, juce::Justification::left, false);
        g.setColour(col);
        g.setFont(rrui::uiFont(size, Weight::Medium));
        g.drawText(value, 412, y + 13, 210, juce::roundToInt(size) + 6, juce::Justification::left, false);
    };
    readout(16, "DECAY", rrui::formatValue(v("decay"), rrui::Unit::Seconds), 30.0f, accent_);
    if (status_.sampledDecay > 0.0f) {
        g.setColour(Colours::textDim);
        g.setFont(rrui::uiFont(10.5f));
        g.drawText("sampled " + rrui::formatValue(status_.sampledDecay, rrui::Unit::Seconds), 412, 63, 200, 14, juce::Justification::left, false);
    }
    const bool sync = v("predelay_sync") > 0.5f;
    readout(88, "PRE-DELAY", sync ? rrui::syncChoices()[juce::roundToInt(v("predelay_sync"))]
                                   : rrui::formatValue(v("predelay"), rrui::Unit::Ms), 17.0f, Colours::text);
    g.setColour(Colours::textDim);
    g.setFont(rrui::capsFont(9.5f));
    g.drawText("MIX", 532, 88, 80, 12, juce::Justification::left, false);
    g.setColour(Colours::text);
    g.setFont(rrui::uiFont(17.0f, Weight::Medium));
    g.drawText(rrui::formatValue(v("mix"), rrui::Unit::Percent), 532, 101, 90, 23, juce::Justification::left, false);

    paintDecayGraph(g, {652.0f, 14.0f, r.getWidth() - 668.0f, r.getHeight() - 28.0f});
}

void SeptimaDisplay::paintDecayGraph(juce::Graphics& g, juce::Rectangle<float> r) {
    auto v = [this](const char* id) { return proc_.state().getRawParameterValue(id)->load(); };
    g.setColour(Colours::textDim);
    g.setFont(rrui::capsFont(9.0f));
    g.drawText("DECAY TIME / FREQUENCY", r.removeFromTop(14), juce::Justification::left, false);
    r.removeFromTop(4);
    auto plot = r.withTrimmedBottom(12).withTrimmedLeft(26);
    const bool have = status_.sampledDecay > 0.0f;
    rr::ReshapeParams rp;
    rp.lowMultiplier = v("low_mult");
    rp.lowCrossoverHz = v("low_xover");
    rp.highMultiplier = v("high_mult");
    rp.highCrossoverHz = v("high_xover");
    const float ratio = have ? v("decay") / status_.sampledDecay : 1.0f;
    std::array<float, rr::kDecayBands> sampled{}, target{};
    float maxRt = 0.5f;
    for (int b = 0; b < rr::kDecayBands; ++b) {
        const auto sb = static_cast<std::size_t>(b);
        sampled[sb] = have ? status_.bandRt[sb] : v("decay") * (b < 3 ? 1.1f : b > 6 ? 0.7f : 1.0f);
        target[sb] = sampled[sb] * ratio * rr::decayMultiplierAt(rr::decayBandCentre(b), rp);
        maxRt = std::max({maxRt, sampled[sb], target[sb]});
    }
    maxRt *= 1.15f;
    auto xFor = [&](float f) { return plot.getX() + std::log(f / 40.0f) / std::log(16000.0f / 40.0f) * plot.getWidth(); };
    auto yFor = [&](float t) { return plot.getBottom() - t / maxRt * plot.getHeight(); };
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (float f : {100.0f, 1000.0f, 10000.0f}) g.drawVerticalLine(juce::roundToInt(xFor(f)), plot.getY(), plot.getBottom());
    const float step = maxRt > 12.0f ? 5.0f : maxRt > 5.0f ? 2.0f : maxRt > 2.0f ? 1.0f : 0.5f;
    g.setFont(rrui::uiFont(9.0f));
    for (float t = step; t < maxRt; t += step) {
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawHorizontalLine(juce::roundToInt(yFor(t)), plot.getX(), plot.getRight());
        g.setColour(Colours::textDim);
        g.drawText(juce::String(t, t < 1.0f ? 1 : 0) + "s", juce::Rectangle<float>(plot.getX() - 28, yFor(t) - 6, 24, 12), juce::Justification::right, false);
    }
    for (auto [f, t] : std::initializer_list<std::pair<float, const char*>>{{100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}})
        g.drawText(t, juce::Rectangle<float>(xFor(f) - 15, plot.getBottom() + 1, 30, 11), juce::Justification::centred, false);

    auto curve = [&](const std::array<float, rr::kDecayBands>& rt, bool closed) {
        juce::Path p;
        for (int b = 0; b < rr::kDecayBands; ++b) {
            const float x = xFor(juce::jlimit(40.0f, 16000.0f, rr::decayBandCentre(b)));
            const float y = yFor(rt[static_cast<std::size_t>(b)]);
            if (b == 0) p.startNewSubPath(plot.getX(), y);
            p.lineTo(x, y);
        }
        p.lineTo(plot.getRight(), yFor(rt.back()));
        p = p.createPathWithRoundedCorners(18.0f);
        if (closed) {
            juce::Path f = p;
            f.lineTo(plot.getRight(), plot.getBottom());
            f.lineTo(plot.getX(), plot.getBottom());
            f.closeSubPath();
            return f;
        }
        return p;
    };
    if (have) {
        juce::Path dashed;
        const float dashes[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, curve(sampled, false), dashes, 2);
        g.setColour(Colours::textDim);
        g.fillPath(dashed);
    }
    juce::ColourGradient fill(accent_.withAlpha(0.28f), 0, plot.getY(), accent_.withAlpha(0.02f), 0, plot.getBottom(), false);
    g.setGradientFill(fill);
    g.fillPath(curve(target, true));
    g.setColour(accent_);
    g.strokePath(curve(target, false), juce::PathStrokeType(1.8f));
}

// ============================================================================
ProgramBrowser::ProgramBrowser(SeptimaProcessor& p, juce::Colour accent) : proc_(p), accent_(accent) {}

void ProgramBrowser::setPrograms(std::vector<rr::IrProgram> programs, const juce::String& currentKey) {
    programs_ = std::move(programs);
    currentKey_ = currentKey;
    categories_.clear();
    for (const auto& pr : programs_) {
        const juce::String c(pr.category);
        if (std::find(categories_.begin(), categories_.end(), c) == categories_.end()) categories_.push_back(c);
        if (juce::String(pr.key) == currentKey_) category_ = c;
    }
    if (category_.isEmpty() && !categories_.empty()) category_ = categories_.front();
    layoutCells();
    repaint();
}

void ProgramBrowser::layoutCells() {
    cells_.clear();
    catRects_.clear();
    auto r = getLocalBounds().reduced(16);
    auto head = r.removeFromTop(28);
    closeRect_ = head.removeFromRight(28);
    r.removeFromTop(8);
    auto cats = r.removeFromLeft(150);
    for (int i = 0; i < static_cast<int>(categories_.size()); ++i)
        catRects_.push_back(cats.removeFromTop(32).reduced(0, 2));
    r.removeFromLeft(16);
    const int cols = 4, rowH = 30, gap = 6;
    const int colW = (r.getWidth() - gap * (cols - 1)) / cols;
    int idx = 0;
    for (int i = 0; i < static_cast<int>(programs_.size()); ++i) {
        if (juce::String(programs_[static_cast<std::size_t>(i)].category) != category_) continue;
        const int col = idx % cols, row = idx / cols;
        cells_.push_back({{r.getX() + col * (colW + gap), r.getY() + row * (rowH + 4), colW, rowH}, i});
        ++idx;
    }
}

void ProgramBrowser::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(Colours::background);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(Colours::outline);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    auto head = getLocalBounds().reduced(16).removeFromTop(28);
    g.setColour(Colours::text);
    g.setFont(rrui::capsFont(12.0f).withExtraKerningFactor(0.15f));
    g.drawText("PROGRAMS", head, juce::Justification::centredLeft, false);
    g.setColour(Colours::textDim);
    g.setFont(rrui::uiFont(11.5f));
    g.drawText(juce::String(static_cast<int>(programs_.size())) + " programs  |  " + proc_.libraryFolder().getFileName(),
               head.withTrimmedLeft(110), juce::Justification::centredLeft, true);
    // Close cross
    g.setColour(Colours::textMid);
    auto cr = closeRect_.toFloat().reduced(8.0f);
    g.drawLine(cr.getX(), cr.getY(), cr.getRight(), cr.getBottom(), 1.6f);
    g.drawLine(cr.getRight(), cr.getY(), cr.getX(), cr.getBottom(), 1.6f);

    for (int i = 0; i < static_cast<int>(catRects_.size()); ++i) {
        const auto& cr2 = catRects_[static_cast<std::size_t>(i)];
        const bool active = categories_[static_cast<std::size_t>(i)] == category_;
        if (active) {
            g.setColour(accent_.withAlpha(0.12f));
            g.fillRoundedRectangle(cr2.toFloat(), 4.0f);
            g.setColour(accent_);
            g.fillRoundedRectangle(cr2.toFloat().withWidth(3.0f).reduced(0, 6), 1.5f);
        }
        g.setColour(active ? Colours::text : Colours::textMid);
        g.setFont(rrui::capsFont(11.0f));
        g.drawText(categories_[static_cast<std::size_t>(i)].toUpperCase(), cr2.withTrimmedLeft(14), juce::Justification::centredLeft, false);
    }
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i) {
        const auto& [rect, progIndex] = cells_[static_cast<std::size_t>(i)];
        const auto& pr = programs_[static_cast<std::size_t>(progIndex)];
        const bool current = juce::String(pr.key) == currentKey_;
        auto cell = rect.toFloat();
        g.setColour(current ? accent_.withAlpha(0.18f) : i == hover_ ? Colours::panelRaised : Colours::panel);
        g.fillRoundedRectangle(cell, 4.0f);
        g.setColour(current ? accent_.withAlpha(0.8f) : Colours::outlineSoft);
        g.drawRoundedRectangle(cell.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(Colours::textDim);
        g.setFont(rrui::uiFont(10.5f, Weight::Medium));
        g.drawText(juce::String(pr.number).paddedLeft('0', 2), rect.withWidth(30), juce::Justification::centred, false);
        g.setColour(current ? Colours::text : Colours::textMid.brighter(0.2f));
        g.setFont(rrui::uiFont(12.5f, current ? Weight::Medium : Weight::Regular));
        g.drawText(pr.name, rect.withTrimmedLeft(30).withTrimmedRight(6), juce::Justification::centredLeft, true);
    }
}

void ProgramBrowser::mouseMove(const juce::MouseEvent& e) {
    int h = -1;
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i)
        if (cells_[static_cast<std::size_t>(i)].first.contains(e.getPosition())) h = i;
    if (h != hover_) { hover_ = h; repaint(); }
}

void ProgramBrowser::mouseUp(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (closeRect_.contains(pos)) { if (onClose) onClose(); return; }
    for (int i = 0; i < static_cast<int>(catRects_.size()); ++i)
        if (catRects_[static_cast<std::size_t>(i)].contains(pos)) {
            category_ = categories_[static_cast<std::size_t>(i)];
            layoutCells();
            repaint();
            return;
        }
    for (const auto& [rect, progIndex] : cells_)
        if (rect.contains(pos)) {
            const auto& pr = programs_[static_cast<std::size_t>(progIndex)];
            currentKey_ = pr.key;
            proc_.selectProgram(pr.key, true);
            repaint();
            if (onClose) onClose();
            return;
        }
}

// ============================================================================
SetupPanel::SetupPanel(juce::Colour accent) : accent_(accent) {
    for (auto* b : {&download, &choose, &openDefault, &rescan}) addAndMakeVisible(*b);
    download.setTooltip(downloadUrl());
    download.getProperties().set("primary", true);
    download.setColour(juce::TextButton::textColourOffId, accent_);
    choose.setTooltip("Pick the unzipped download folder (or any folder of WAV/AIFF impulse responses)");
    openDefault.setTooltip("Open the folder Septima checks automatically, to copy your IRs into");
    rescan.setTooltip("Look for IRs again, e.g. after copying them into the default folder");
}

void SetupPanel::resized() {
    const int y = 286, h = 32;
    download.setBounds(40, y, 210, h);
    choose.setBounds(262, y, 170, h);
    openDefault.setBounds(444, y, 190, h);
    rescan.setBounds(646, y, 100, h);
}

void SetupPanel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(Colours::background.withAlpha(0.97f));
    g.fillRoundedRectangle(r, 6.0f);
    rrui::paintDisplayGlass(g, r.reduced(1.0f), accent_);

    g.setColour(accent_);
    g.setFont(rrui::capsFont(11.0f).withExtraKerningFactor(0.2f));
    g.drawText("FIRST-TIME SETUP", 40, 36, 600, 16, juce::Justification::left, false);
    g.setColour(Colours::text);
    g.setFont(rrui::uiFont(26.0f, rrui::Weight::Medium));
    g.drawText("Septima needs its impulse responses", 40, 56, 860, 36, juce::Justification::left, false);

    g.setFont(rrui::uiFont(14.0f));
    const char* steps[] = {
        "1.   Download the free IR library (about 960 MB): \"Samplicity - Bricasti IRs version 2023-10.zip\".",
        "2.   Unzip it anywhere you like.",
        "3.   Click Choose Folder and pick the unzipped folder. The top-level folder is fine.",
        "       Or copy the WAVs into the default folder and click Rescan.",
    };
    int y = 112;
    for (auto* line : steps) {
        g.setColour(line[0] == ' ' ? Colours::textDim : Colours::textMid);
        g.drawText(line, 40, y, 900, 22, juce::Justification::left, false);
        y += 30;
    }
    g.setColour(Colours::textDim);
    g.setFont(rrui::uiFont(12.0f));
    g.drawText("The library can't be bundled with Septima because of its licence. You only need to do this once.",
               40, 336, 900, 18, juce::Justification::left, false);
    if (message_.isNotEmpty()) {
        g.setColour(accent_.withAlpha(0.9f));
        g.drawText(message_, 40, 362, 900, 18, juce::Justification::left, true);
    }
}

// ============================================================================
SeptimaEditor::SeptimaEditor(SeptimaProcessor& p)
    : AudioProcessorEditor(p), proc_(p), lnf_(kAccent),
      mainPanel_("Reverb", kAccent), outPanel_("Output", kAccent), earlyPanel_("Early", kAccent),
      latePanel_("Late", kAccent), shapePanel_("Decay Shape", kAccent), eqPanel_("Master EQ", kAccent),
      presetBar_(p.presets(), kAccent), display_(p, kAccent), browser_(p, kAccent), setup_(kAccent),
      meters_({"IN", "ER", "LATE", "VLF", "OUT"}, kAccent),
      eqCurve_(kAccent,
               [this] {
                   auto v = [this](const char* id) { return proc_.state().getRawParameterValue(id)->load(); };
                   const double fs = 48000.0;
                   std::vector<rrui::CurveStage> st;
                   auto add = [&](const rr::BiquadCoeffs& c) { st.push_back({c.b0, c.b1, c.b2, c.a1, c.a2}); };
                   add(v("eq_low_type") > 0.5f ? rr::design::peak(fs, v("eq_low_freq"), v("eq_low_q"), v("eq_low_gain"))
                                               : rr::design::lowShelf(fs, v("eq_low_freq"), v("eq_low_q"), v("eq_low_gain")));
                   add(rr::design::peak(fs, v("eq_mid_freq"), v("eq_mid_q"), v("eq_mid_gain")));
                   add(v("eq_high_type") > 0.5f ? rr::design::peak(fs, v("eq_high_freq"), v("eq_high_q"), v("eq_high_gain"))
                                                : rr::design::highShelf(fs, v("eq_high_freq"), v("eq_high_q"), v("eq_high_gain")));
                   if (v("eq_highcut") < 19999.0f) add(rr::design::lowpass(fs, v("eq_highcut"), 0.7071));
                   return st;
               },
               [this] { return proc_.state().getRawParameterValue("eq_on")->load() > 0.5f; }) {
    setLookAndFeel(&lnf_);
    for (auto* pnl : {&mainPanel_, &outPanel_, &earlyPanel_, &latePanel_, &shapePanel_, &eqPanel_}) addAndMakeVisible(*pnl);
    addAndMakeVisible(presetBar_);
    addAndMakeVisible(display_);
    addAndMakeVisible(libraryButton_);
    p.presets().onPresetLoaded = [this] { presetBar_.refreshMenu(); };

    // Main
    knob(mainPanel_, "decay", "Decay", KnobSize::Large);
    knob(mainPanel_, "predelay", "Pre-delay", KnobSize::Large);
    knob(mainPanel_, "early_late", "Early / Late", KnobSize::Large, true);
    knob(mainPanel_, "vlf", "VLF", KnobSize::Large);
    knob(mainPanel_, "modulation", "Modulation", KnobSize::Large);
    knob(mainPanel_, "motion", "Tail Motion", KnobSize::Large);
    choice(mainPanel_, "predelay_sync");
    mainPanel_.addAndMakeVisible(sampledButton_);
    sampledButton_.setTooltip("Return Decay to the program's sampled value");
    sampledButton_.onClick = [this] {
        const auto st = proc_.status();
        if (st.sampledDecay > 0.0f)
            if (auto* param = proc_.state().getParameter("decay")) param->setValueNotifyingHost(param->convertTo0to1(st.sampledDecay));
    };
    // Output
    knob(outPanel_, "mix", "Mix", KnobSize::Medium);
    knob(outPanel_, "gain", "Gain", KnobSize::Medium, true);
    outPanel_.addAndMakeVisible(meters_);
    // Early / late / shape
    choice(earlyPanel_, "er_set", "Reflections");
    knob(earlyPanel_, "early_rolloff", "Roll-off", KnobSize::Medium);
    knob(latePanel_, "delay_time", "Delay", KnobSize::Medium);
    choice(latePanel_, "delay_sync");
    knob(latePanel_, "delay_level", "Delay Level", KnobSize::Medium);
    knob(latePanel_, "late_rolloff", "Roll-off", KnobSize::Medium);
    knob(shapePanel_, "low_mult", "Bass Mult", KnobSize::Small);
    knob(shapePanel_, "low_xover", "Bass Freq", KnobSize::Small);
    knob(shapePanel_, "high_mult", "High Mult", KnobSize::Small);
    knob(shapePanel_, "high_xover", "High Freq", KnobSize::Small);
    // EQ
    eqOn_ = std::make_unique<rrui::Switch>(p.state(), "eq_on", "On");
    eqPanel_.addAndMakeVisible(*eqOn_);
    eqPanel_.addAndMakeVisible(eqCurve_);
    choice(eqPanel_, "eq_low_type");
    knob(eqPanel_, "eq_low_freq", "Freq", KnobSize::Small);
    knob(eqPanel_, "eq_low_gain", "Gain", KnobSize::Small, true);
    knob(eqPanel_, "eq_low_q", "Q", KnobSize::Small);
    knob(eqPanel_, "eq_mid_freq", "Freq", KnobSize::Small);
    knob(eqPanel_, "eq_mid_gain", "Gain", KnobSize::Small, true);
    knob(eqPanel_, "eq_mid_q", "Q", KnobSize::Small);
    choice(eqPanel_, "eq_high_type");
    knob(eqPanel_, "eq_high_freq", "Freq", KnobSize::Small);
    knob(eqPanel_, "eq_high_gain", "Gain", KnobSize::Small, true);
    knob(eqPanel_, "eq_high_q", "Q", KnobSize::Small);
    knob(eqPanel_, "eq_highcut", "Freq", KnobSize::Small);

    libraryButton_.setTooltip("Choose a different impulse-response folder");
    libraryButton_.onClick = [this] { chooseLibrary(); };
    display_.browse.onClick = [this] {
        browser_.setPrograms(proc_.programs(), proc_.currentProgramKey());
        browser_.setVisible(true);
        browser_.toFront(false);
    };
    browser_.onClose = [this] { browser_.setVisible(false); };
    addChildComponent(browser_);

    setup_.download.onClick = [] { juce::URL(SetupPanel::downloadUrl()).launchInDefaultBrowser(); };
    setup_.choose.onClick = [this] { chooseLibrary(); };
    setup_.openDefault.onClick = [] {
        const auto f = SeptimaProcessor::userDefaultLibraryFolder(true);
        f.startAsProcess(); // opens the folder in Finder / Explorer
    };
    setup_.rescan.onClick = [this] { proc_.rescanLibrary(); };
    addChildComponent(setup_);

    setSize(kWidth, kHeight);
    startTimerHz(30);
}

SeptimaEditor::~SeptimaEditor() {
    proc_.presets().onPresetLoaded = nullptr;
    setLookAndFeel(nullptr);
}

rrui::Knob& SeptimaEditor::knob(juce::Component& parent, const juce::String& id, const juce::String& label,
                                KnobSize size, bool bipolar) {
    auto k = std::make_unique<rrui::Knob>(proc_.state(), id, label, size, bipolar);
    auto& ref = *k;
    parent.addAndMakeVisible(ref);
    controls_[id] = std::move(k);
    return ref;
}

rrui::Choice& SeptimaEditor::choice(juce::Component& parent, const juce::String& id, const juce::String& label) {
    auto k = std::make_unique<rrui::Choice>(proc_.state(), id, label);
    auto& ref = *k;
    parent.addAndMakeVisible(ref);
    controls_[id] = std::move(k);
    return ref;
}

void SeptimaEditor::chooseLibrary() {
    chooser_ = std::make_unique<juce::FileChooser>("Choose the folder containing your impulse responses (the unzipped download folder is fine)",
                                                   proc_.libraryFolder().exists() ? proc_.libraryFolder()
                                                       : juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f.isDirectory()) proc_.setLibraryFolder(f);
                          });
}

void SeptimaEditor::paint(juce::Graphics& g) {
    juce::ColourGradient bg(juce::Colour(0xff111215), 0, 0, Colours::background, 0, static_cast<float>(getHeight()), false);
    g.setGradientFill(bg);
    g.fillAll();
    auto header = getLocalBounds().removeFromTop(56);
    g.setColour(Colours::header);
    g.fillRect(header);
    g.setColour(Colours::outline);
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));
    paintWordmark(g, header.reduced(20, 8).withWidth(260), "SEPTIMA", "CONVOLUTION REVERB", kAccent);
}

void SeptimaEditor::paintOverChildren(juce::Graphics& g) {
    // Group captions inside the EQ panel.
    const char* names[] = {"Low", "Mid", "High", "High Cut"};
    for (int i = 0; i < 4; ++i)
        if (!eqGroups_[i].isEmpty()) rrui::paintGroupLabel(g, eqGroups_[i].translated(eqPanel_.getX(), eqPanel_.getY()), names[i]);
}

void SeptimaEditor::resized() {
    presetBar_.setBounds(300, 13, 420, 30);
    libraryButton_.setBounds(kWidth - 16 - 104, 14, 104, 28);
    display_.setBounds(16, 64, 968, 150);
    browser_.setBounds(16, 64, 968, 492);
    setup_.setBounds(16, 64, 968, 492);

    using rrui::Knob;
    const int lw = Knob::cellWidth(KnobSize::Large), lh = Knob::cellHeight(KnobSize::Large);
    const int mw = Knob::cellWidth(KnobSize::Medium), mh = Knob::cellHeight(KnobSize::Medium);
    const int sh = Knob::cellHeight(KnobSize::Small);

    mainPanel_.setBounds(16, 222, 744, 178);
    {
        auto row = juce::Rectangle<int>(24, 34, 744 - 48, lh);
        rrui::placeRow(row, {c("decay"), c("predelay"), c("early_late"), c("vlf"), c("modulation"), c("motion")}, lw);
        const int below = row.getBottom() + 4;
        sampledButton_.setBounds(c("decay")->getBounds().getCentreX() - 38, below, 76, 22);
        c("predelay_sync")->setBounds(c("predelay")->getBounds().getCentreX() - 40, below, 80, 22);
    }
    outPanel_.setBounds(768, 222, 216, 334);
    {
        rrui::placeRow({14, 34, 188, mh}, {c("mix"), c("gain")}, mw);
        meters_.setBounds(18, 34 + mh + 14, 180, 334 - (34 + mh + 14) - 14);
    }
    earlyPanel_.setBounds(16, 408, 200, 148);
    {
        c("er_set")->setBounds(14, 56, 92, 40);
        c("early_rolloff")->setBounds(112, 36, mw, mh);
    }
    latePanel_.setBounds(224, 408, 268, 148);
    {
        rrui::placeRow({12, 34, 244, mh}, {c("delay_time"), c("delay_level"), c("late_rolloff")}, mw);
        c("delay_sync")->setBounds(c("delay_time")->getBounds().getCentreX() - 36, 34 + mh + 2, 72, 22);
    }
    shapePanel_.setBounds(500, 408, 260, 148);
    rrui::placeRow({10, 44, 240, sh}, {c("low_mult"), c("low_xover"), c("high_mult"), c("high_xover")}, 58);

    eqPanel_.setBounds(16, 564, 968, 132);
    {
        eqOn_->setBounds(968 - 14 - 64, 5, 64, 22);
        eqCurve_.setBounds(14, 36, 200, 84);
        const int cw = 56, gy = 32, ky = 48;
        int x = 232;
        auto group = [&](int index, std::initializer_list<juce::Component*> items) {
            const int w = static_cast<int>(items.size()) * cw;
            eqGroups_[index] = {x, gy, w, 12};
            int cx = x;
            for (auto* it : items) {
                if (dynamic_cast<rrui::Choice*>(it) != nullptr) it->setBounds(cx + 2, ky + (sh - 22) / 2 - 4, cw - 4, 22);
                else it->setBounds(cx, ky, cw, sh);
                cx += cw;
            }
            x += w + 16;
        };
        group(0, {c("eq_low_type"), c("eq_low_freq"), c("eq_low_gain"), c("eq_low_q")});
        group(1, {c("eq_mid_freq"), c("eq_mid_gain"), c("eq_mid_q")});
        group(2, {c("eq_high_type"), c("eq_high_freq"), c("eq_high_gain"), c("eq_high_q")});
        group(3, {c("eq_highcut")});
    }
}

void SeptimaEditor::timerCallback() {
    const auto st = proc_.status();
    if (st.generation != lastGeneration_) {
        lastGeneration_ = st.generation;
        if (browser_.isVisible()) browser_.setPrograms(proc_.programs(), proc_.currentProgramKey());
    }
    display_.refresh(st);
    // Setup screen until a library with programs has been found.
    const bool noLibrary = !st.busy && !st.ready && proc_.programs().empty();
    if (noLibrary != setup_.isVisible()) {
        setup_.setVisible(noLibrary);
        if (noLibrary) setup_.toFront(false);
    }
    if (noLibrary) {
        const auto folder = proc_.libraryFolder();
        setup_.setMessage(folder.isDirectory() ? "No impulse responses found in " + folder.getFullPathName() : juce::String());
    }
    auto take = [](std::atomic<float>& a) { return a.exchange(0.0f); };
    meters_.setLevels({take(proc_.meters.in), take(proc_.meters.early), take(proc_.meters.late),
                       take(proc_.meters.vlf), take(proc_.meters.out)});
}
