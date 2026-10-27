#include "Widgets.h"

#include <cmath>
#include <complex>

namespace rrui {

juce::String formatValue(float v, Unit unit) {
    switch (unit) {
        case Unit::Percent: return juce::String(juce::roundToInt(v)) + "%";
        case Unit::Bipolar: return (v > 0.5f ? "+" : "") + juce::String(juce::roundToInt(v));
        case Unit::Ms:
            return v < 10.0f ? juce::String(v, 1) + " ms" : juce::String(juce::roundToInt(v)) + " ms";
        case Unit::Seconds:
            return v < 10.0f ? juce::String(v, 2) + " s" : juce::String(v, 1) + " s";
        case Unit::Hz:
            if (v >= 19999.0f) return "Off";
            return v >= 1000.0f ? juce::String(v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                : juce::String(juce::roundToInt(v)) + " Hz";
        case Unit::Db: return (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB";
        case Unit::Multiplier: return juce::String(v, 2) + "x";
        case Unit::None: default: return juce::String(v, 2);
    }
}

float parseValue(const juce::String& text, Unit unit) {
    auto t = text.trim().toLowerCase();
    if (unit == Unit::Hz && t == "off") return 20000.0f;
    float v = t.retainCharacters("0123456789.-+").getFloatValue();
    if (unit == Unit::Hz && t.contains("k")) v *= 1000.0f;
    if (unit == Unit::Seconds && t.endsWith("ms")) v *= 0.001f;
    if (unit == Unit::Ms && t.endsWith("s") && !t.endsWith("ms")) v *= 1000.0f;
    return v;
}

void addFloat(ParamList& list, const juce::String& id, const juce::String& name, float min, float max,
              float def, Unit unit, float centre, float step) {
    juce::NormalisableRange<float> range(min, max, step);
    if (centre > min && centre < max) range.setSkewForCentre(centre);
    auto attrs = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([unit](float v, int) { return formatValue(v, unit); })
        .withValueFromStringFunction([unit](const juce::String& s) { return parseValue(s, unit); });
    list.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name, range, def, attrs));
}

void addChoice(ParamList& list, const juce::String& id, const juce::String& name,
               const juce::StringArray& choices, int def) {
    list.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, choices, def));
}

void addBool(ParamList& list, const juce::String& id, const juce::String& name, bool def) {
    list.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, def));
}

const juce::StringArray& syncChoices() {
    static const juce::StringArray c{"Free", "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T",
                                     "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/1"};
    return c;
}

double syncSeconds(int index, double bpm) {
    static const double q[] = {0.0, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 3.0 / 16, 1.0 / 4, 1.0 / 3,
                                3.0 / 8, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 4.0};
    if (index <= 0 || index >= static_cast<int>(std::size(q)) || bpm <= 0.0) return 0.0;
    return q[index] * 60.0 / bpm;
}

// ----------------------------------------------------------------------------
Knob::Knob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label,
           KnobSize size, bool bipolar)
    : size_(size), label_(label.toUpperCase()) {
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider_.getProperties().set("bipolar", bipolar);
    slider_.setMouseDragSensitivity(220);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, cellWidth(size), 16);
    slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramId, slider_);
    if (auto* p = state.getParameter(paramId)) {
        slider_.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        slider_.setTooltip(p->getName(64) + "  (double-click to reset, click value to type)");
    }
}

void Knob::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(16);
    const int d = dial(size_);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, r.getWidth(), 16);
    slider_.setBounds(r.withSizeKeepingCentre(r.getWidth(), d + 18).withY(r.getY()));
}

void Knob::paint(juce::Graphics& g) {
    g.setFont(capsFont(size_ == KnobSize::Small ? 9.5f : 10.0f));
    g.setColour(Colours::textDim.brighter(0.25f));
    g.drawText(label_, getLocalBounds().removeFromTop(14), juce::Justification::centred, false);
}

Choice::Choice(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label)
    : label_(label.toUpperCase()) {
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(paramId)))
        box_.addItemList(p->choices, 1);
    addAndMakeVisible(box_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, paramId, box_);
}

void Choice::resized() {
    auto r = getLocalBounds();
    if (label_.isNotEmpty()) r.removeFromTop(16);
    box_.setBounds(r.withHeight(kHeight).withY(label_.isNotEmpty() ? 16 : (r.getHeight() - kHeight) / 2));
}

void Choice::paint(juce::Graphics& g) {
    if (label_.isEmpty()) return;
    g.setFont(capsFont(10.0f));
    g.setColour(Colours::textDim.brighter(0.25f));
    g.drawText(label_, getLocalBounds().removeFromTop(14), juce::Justification::centredLeft, false);
}

Switch::Switch(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text) {
    button_.setButtonText(text.toUpperCase());
    button_.setClickingTogglesState(true);
    addAndMakeVisible(button_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, paramId, button_);
}

// ----------------------------------------------------------------------------
Panel::Panel(juce::String title, juce::Colour accent) : title_(std::move(title)), accent_(accent) {}

Panel::Page& Panel::addPage(const juce::String& name) {
    auto page = std::make_unique<Page>();
    auto& ref = *page;
    addChildComponent(ref);
    pages_.emplace_back(name, std::move(page));
    showPage(current_);
    return ref;
}

void Panel::showPage(int index) {
    if (pages_.empty()) return;
    current_ = juce::jlimit(0, static_cast<int>(pages_.size()) - 1, index);
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i) pages_[static_cast<std::size_t>(i)].second->setVisible(i == current_);
    if (onPageChanged) onPageChanged(current_);
    repaint();
}

void Panel::resized() {
    tabRects_.clear();
    if (pages_.empty()) return;
    auto strip = tabArea_.withHeight(24);
    int x = strip.getX();
    for (const auto& [name, page] : pages_) {
        const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(capsFont(10.0f), name.toUpperCase())) + 20;
        tabRects_.push_back({x, strip.getY(), w, strip.getHeight()});
        x += w + 2;
    }
    for (auto& [name, page] : pages_) page->setBounds(tabArea_.withTrimmedTop(30));
}

void Panel::mouseUp(const juce::MouseEvent& e) {
    for (int i = 0; i < static_cast<int>(tabRects_.size()); ++i)
        if (tabRects_[static_cast<std::size_t>(i)].contains(e.getPosition())) showPage(i);
}

void Panel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    juce::ColourGradient bg(Colours::panelTop, 0, r.getY(), Colours::panel, 0, r.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(Colours::outline);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.035f));
    g.drawHorizontalLine(1, r.getX() + 6, r.getRight() - 6);

    auto head = getLocalBounds().reduced(14, 0).removeFromTop(30);
    g.setFont(capsFont(11.0f));
    g.setColour(Colours::textMid);
    const int tw = juce::roundToInt(juce::GlyphArrangement::getStringWidth(capsFont(11.0f), title_.toUpperCase()));
    g.drawText(title_.toUpperCase(), head, juce::Justification::centredLeft, false);
    g.setColour(Colours::outlineSoft.brighter(0.08f));
    g.drawHorizontalLine(head.getCentreY(), static_cast<float>(head.getX() + tw + 10), static_cast<float>(head.getRight()));

    if (!tabRects_.empty()) {
        auto strip = tabArea_.withHeight(24);
        g.setColour(Colours::outlineSoft);
        g.drawHorizontalLine(strip.getBottom(), static_cast<float>(strip.getX()), static_cast<float>(strip.getRight()));
        for (int i = 0; i < static_cast<int>(tabRects_.size()); ++i) {
            const auto& tr = tabRects_[static_cast<std::size_t>(i)];
            const bool active = i == current_;
            g.setFont(capsFont(10.0f));
            g.setColour(active ? Colours::text : Colours::textDim);
            g.drawText(pages_[static_cast<std::size_t>(i)].first.toUpperCase(), tr, juce::Justification::centred, false);
            if (active) {
                g.setColour(accent_);
                g.fillRect(tr.getX() + 6, tr.getBottom() - 2, tr.getWidth() - 12, 2);
            }
        }
    }
}

// ----------------------------------------------------------------------------
MeterBank::MeterBank(juce::StringArray labels, juce::Colour accent)
    : labels_(std::move(labels)), accent_(accent),
      display_(static_cast<std::size_t>(labels_.size()), 0.0f), hold_(display_), holdAge_(display_.size(), 0) {}

void MeterBank::setLevels(const std::vector<float>& v) {
    for (std::size_t i = 0; i < display_.size() && i < v.size(); ++i) {
        const float db = juce::jlimit(-60.0f, 6.0f, juce::Decibels::gainToDecibels(v[i], -60.0f));
        const float norm = (db + 60.0f) / 66.0f;
        display_[i] = norm > display_[i] ? norm : display_[i] * 0.88f + norm * 0.12f;
        if (norm >= hold_[i]) { hold_[i] = norm; holdAge_[i] = 0; }
        else if (++holdAge_[i] > 30) hold_[i] = std::max(display_[i], hold_[i] - 0.01f);
    }
    repaint();
}

void MeterBank::paint(juce::Graphics& g) {
    const int n = labels_.size();
    if (n == 0) return;
    auto r = getLocalBounds();
    auto labelRow = r.removeFromBottom(14);
    r.removeFromBottom(4);
    auto scale = r.removeFromLeft(22);
    g.setFont(uiFont(9.0f));
    for (int db : {0, -12, -24, -36, -48}) {
        const float y = r.getY() + (1.0f - (static_cast<float>(db) + 60.0f) / 66.0f) * r.getHeight();
        g.setColour(Colours::textDim);
        g.drawText(juce::String(db), scale.withY(juce::roundToInt(y) - 6).withHeight(12), juce::Justification::centredRight, false);
        g.setColour(Colours::outlineSoft);
        g.drawHorizontalLine(juce::roundToInt(y), static_cast<float>(r.getX()), static_cast<float>(r.getRight()));
    }
    const int w = r.getWidth() / n;
    for (int i = 0; i < n; ++i) {
        auto cell = juce::Rectangle<int>(r.getX() + i * w, r.getY(), w, r.getHeight()).reduced(juce::jmax(3, w / 2 - 4), 0);
        g.setColour(Colours::display);
        g.fillRoundedRectangle(cell.toFloat(), 2.0f);
        const float lvl = display_[static_cast<std::size_t>(i)];
        auto fill = cell.toFloat().withTop(cell.getBottom() - lvl * cell.getHeight());
        juce::ColourGradient grad(accent_.withAlpha(0.55f), 0, static_cast<float>(cell.getBottom()),
                                  accent_, 0, static_cast<float>(cell.getY()), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(fill, 2.0f);
        if (lvl > 60.0f / 66.0f) {
            g.setColour(juce::Colour(0xffe5484d));
            g.fillRect(cell.toFloat().withHeight(cell.getHeight() * (1.0f - 60.0f / 66.0f)));
        }
        const float hy = cell.getBottom() - hold_[static_cast<std::size_t>(i)] * cell.getHeight();
        g.setColour(Colours::text.withAlpha(0.7f));
        g.fillRect(cell.toFloat().withY(hy).withHeight(1.5f));
        g.setColour(Colours::textDim);
        g.setFont(capsFont(8.5f));
        g.drawText(labels_[i], labelRow.withX(r.getX() + i * w).withWidth(w), juce::Justification::centred, false);
    }
}

// ----------------------------------------------------------------------------
ResponseCurve::ResponseCurve(juce::Colour accent, std::function<std::vector<CurveStage>()> provider,
                             std::function<bool()> enabled)
    : accent_(accent), provider_(std::move(provider)), enabled_(std::move(enabled)) {
    startTimerHz(20);
}

void ResponseCurve::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    paintDisplayGlass(g, r, accent_);
    r = r.reduced(6.0f, 6.0f);
    auto xFor = [&](double f) { return r.getX() + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * r.getWidth(); };
    auto yFor = [&](double db) { return r.getCentreY() - static_cast<float>(db / 20.0) * r.getHeight() * 0.5f; };
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) g.drawVerticalLine(juce::roundToInt(xFor(f)), r.getY(), r.getBottom());
    for (double db : {-12.0, -6.0, 6.0, 12.0}) g.drawHorizontalLine(juce::roundToInt(yFor(db)), r.getX(), r.getRight());
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawHorizontalLine(juce::roundToInt(yFor(0.0)), r.getX(), r.getRight());

    const auto stages = provider_();
    const double fs = 48000.0;
    juce::Path path, fill;
    for (int i = 0; i <= 240; ++i) {
        const double f = 20.0 * std::pow(1000.0, i / 240.0);
        const double w = juce::MathConstants<double>::twoPi * f / fs;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2 * w);
        double mag = 1.0;
        for (const auto& c : stages) mag *= std::abs((c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2));
        const float x = xFor(f), y = juce::jlimit(r.getY(), r.getBottom(), yFor(20.0 * std::log10(std::max(mag, 1e-6))));
        if (i == 0) { path.startNewSubPath(x, y); fill.startNewSubPath(x, yFor(0.0)); fill.lineTo(x, y); }
        else { path.lineTo(x, y); fill.lineTo(x, y); }
    }
    fill.lineTo(r.getRight(), yFor(0.0));
    fill.closeSubPath();
    const bool on = enabled_ ? enabled_() : true;
    const auto col = on ? accent_ : Colours::textDim;
    g.setColour(col.withAlpha(0.14f));
    g.fillPath(fill);
    g.setColour(col);
    g.strokePath(path, juce::PathStrokeType(1.8f));
    g.setFont(uiFont(9.0f));
    g.setColour(Colours::textDim);
    for (auto [f, t] : std::initializer_list<std::pair<double, const char*>>{{100.0, "100"}, {1000.0, "1k"}, {10000.0, "10k"}})
        g.drawText(t, juce::Rectangle<float>(xFor(f) + 3, r.getBottom() - 11, 30, 10), juce::Justification::left, false);
}

void paintDisplayGlass(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent) {
    g.setColour(Colours::display);
    g.fillRoundedRectangle(r, 5.0f);
    juce::ColourGradient sheen(accent.withAlpha(0.06f), r.getX(), r.getY(), juce::Colours::transparentBlack,
                               r.getX(), r.getY() + r.getHeight() * 0.6f, false);
    g.setGradientFill(sheen);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine(juce::roundToInt(r.getBottom()) , r.getX() + 4, r.getRight() - 4);
}

void placeRow(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int cellWidth) {
    const int n = static_cast<int>(items.size());
    if (n == 0) return;
    const int gap = n > 1 ? juce::jmax(0, (area.getWidth() - n * cellWidth) / (n - 1)) : 0;
    const int total = n * cellWidth + (n - 1) * gap;
    int x = area.getX() + (area.getWidth() - total) / 2;
    for (auto* c : items) {
        if (c) c->setBounds(x, area.getY(), cellWidth, area.getHeight());
        x += cellWidth + gap;
    }
}

void placeFlow(juce::Rectangle<int> area, std::initializer_list<std::pair<juce::Component*, int>> items, int gap) {
    int x = area.getX();
    for (const auto& [c, w] : items) {
        if (c) c->setBounds(x, area.getY(), w, area.getHeight());
        x += w + gap;
    }
}

void paintGroupLabel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text) {
    g.setFont(capsFont(9.5f));
    g.setColour(Colours::textDim);
    const auto t = text.toUpperCase();
    const int tw = juce::roundToInt(juce::GlyphArrangement::getStringWidth(capsFont(9.5f), t));
    g.drawText(t, r, juce::Justification::centred, false);
    g.setColour(Colours::outlineSoft.brighter(0.05f));
    const int cy = r.getCentreY();
    const int l = r.getCentreX() - tw / 2 - 6, rr = r.getCentreX() + tw / 2 + 6;
    if (l - r.getX() > 4) g.drawHorizontalLine(cy, static_cast<float>(r.getX()), static_cast<float>(l));
    if (r.getRight() - rr > 4) g.drawHorizontalLine(cy, static_cast<float>(rr), static_cast<float>(r.getRight()));
}

} // namespace rrui
