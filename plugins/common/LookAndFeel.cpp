#include "LookAndFeel.h"

#include "BinaryData.h"

namespace rrui {
namespace {

struct Faces {
    juce::Typeface::Ptr regular, medium, semibold;
    Faces() {
        regular = juce::Typeface::createSystemTypefaceFor(RRFonts::InterRegular_ttf, RRFonts::InterRegular_ttfSize);
        medium = juce::Typeface::createSystemTypefaceFor(RRFonts::InterMedium_ttf, RRFonts::InterMedium_ttfSize);
        semibold = juce::Typeface::createSystemTypefaceFor(RRFonts::InterSemiBold_ttf, RRFonts::InterSemiBold_ttfSize);
    }
};

Faces& faces() {
    static Faces f;
    return f;
}

} // namespace

juce::Font uiFont(float height, Weight weight) {
    auto& f = faces();
    auto tf = weight == Weight::SemiBold ? f.semibold : weight == Weight::Medium ? f.medium : f.regular;
    return juce::Font(juce::FontOptions(tf).withHeight(height));
}

juce::Font capsFont(float height) {
    return uiFont(height, Weight::SemiBold).withExtraKerningFactor(0.09f);
}

LookAndFeel::LookAndFeel(juce::Colour accent) : accent_(accent) {
    setColour(juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour(juce::Slider::textBoxTextColourId, Colours::text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, accent.withAlpha(0.35f));
    setColour(juce::Label::textColourId, Colours::text);
    setColour(juce::Label::textWhenEditingColourId, Colours::text);
    setColour(juce::Label::backgroundWhenEditingColourId, Colours::panelRaised);
    setColour(juce::Label::outlineWhenEditingColourId, accent);
    setColour(juce::TextEditor::backgroundColourId, Colours::panelRaised);
    setColour(juce::TextEditor::textColourId, Colours::text);
    setColour(juce::TextEditor::outlineColourId, Colours::outline);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.35f));
    setColour(juce::CaretComponent::caretColourId, accent);
    setColour(juce::ComboBox::backgroundColourId, Colours::panelRaised);
    setColour(juce::ComboBox::textColourId, Colours::text);
    setColour(juce::ComboBox::outlineColourId, Colours::outline);
    setColour(juce::ComboBox::arrowColourId, Colours::textMid);
    setColour(juce::PopupMenu::backgroundColourId, Colours::panelRaised);
    setColour(juce::PopupMenu::textColourId, Colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.22f));
    setColour(juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour(juce::PopupMenu::headerTextColourId, accent);
    setColour(juce::TextButton::buttonColourId, Colours::panelRaised);
    setColour(juce::TextButton::buttonOnColourId, accent);
    setColour(juce::TextButton::textColourOffId, Colours::textMid);
    setColour(juce::TextButton::textColourOnId, Colours::text);
    setColour(juce::ScrollBar::thumbColourId, Colours::track);
    setColour(juce::TooltipWindow::backgroundColourId, Colours::panelRaised);
    setColour(juce::TooltipWindow::textColourId, Colours::text);
    setColour(juce::TooltipWindow::outlineColourId, Colours::outline);
    setColour(juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour(juce::AlertWindow::textColourId, Colours::text);
    setColour(juce::AlertWindow::outlineColourId, Colours::outline);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& f) {
    auto& fc = faces();
    return f.isBold() ? fc.semibold : fc.regular;
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                   float start, float end, juce::Slider& s) {
    const auto area = juce::Rectangle<int>(x, y, w, h).toFloat();
    const float size = juce::jmin(area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float ringR = size * 0.5f - 2.0f;
    const float ringW = juce::jlimit(2.0f, 3.0f, size * 0.045f);
    const float bodyR = ringR - ringW - juce::jmax(3.0f, size * 0.07f);
    const float angle = start + pos * (end - start);
    const bool enabled = s.isEnabled();
    const bool bipolar = s.getProperties().getWithDefault("bipolar", false);

    // Value ring
    juce::Path track;
    track.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, start, end, true);
    g.setColour(Colours::track);
    g.strokePath(track, juce::PathStrokeType(ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs(angle - from) > 0.001f) {
        juce::Path value;
        value.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(enabled ? accent_ : Colours::textDim);
        g.strokePath(value, juce::PathStrokeType(ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body: soft shadow, machined cap with top highlight.
    juce::Path body;
    body.addEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), juce::roundToInt(bodyR * 0.35f), {0, juce::roundToInt(bodyR * 0.12f)})
        .drawForPath(g, body);
    juce::ColourGradient cap(juce::Colour(0xff3a3f46), c.x, c.y - bodyR,
                             juce::Colour(0xff17191c), c.x, c.y + bodyR, false);
    cap.addColour(0.5, juce::Colour(0xff25292e));
    g.setGradientFill(cap);
    g.fillPath(body);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);
    const float innerR = bodyR * 0.82f;
    juce::ColourGradient face(juce::Colour(0xff2b2f35), c.x, c.y - innerR, juce::Colour(0xff1d2024), c.x, c.y + innerR, false);
    g.setGradientFill(face);
    g.fillEllipse(c.x - innerR, c.y - innerR, innerR * 2, innerR * 2);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawEllipse(c.x - innerR, c.y - innerR, innerR * 2, innerR * 2, 0.8f);

    // Indicator
    const auto p1 = c.getPointOnCircumference(innerR * 0.38f, angle);
    const auto p2 = c.getPointOnCircumference(innerR * 0.92f, angle);
    g.setColour(enabled ? Colours::text : Colours::textDim);
    g.drawLine({p1, p2}, juce::jlimit(1.6f, 2.4f, size * 0.035f));
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState() && b.getClickingTogglesState();
    juce::Colour fill = Colours::panelRaised;
    if (over) fill = fill.brighter(0.06f);
    if (down) fill = fill.darker(0.15f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(on ? accent_.withAlpha(0.7f) : Colours::outline);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    if (b.getClickingTogglesState()) {
        // LED
        const float d = 6.0f;
        auto led = juce::Rectangle<float>(d, d).withCentre({r.getX() + 11.0f, r.getCentreY()});
        if (on) {
            g.setColour(accent_.withAlpha(0.25f));
            g.fillEllipse(led.expanded(3.0f));
        }
        g.setColour(on ? accent_ : Colours::track.brighter(0.15f));
        g.fillEllipse(led);
    }
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(getTextButtonFont(b, b.getHeight()));
    const bool on = b.getToggleState() && b.getClickingTogglesState();
    g.setColour(!b.isEnabled() ? Colours::textDim : on ? Colours::text : Colours::textMid);
    auto r = b.getLocalBounds().reduced(6, 1);
    if (b.getClickingTogglesState()) r.removeFromLeft(12);
    g.drawFittedText(b.getButtonText(), r, juce::Justification::centred, 1);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int) { return capsFont(10.5f); }
juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return uiFont(12.0f, Weight::Medium); }
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont(13.0f); }
juce::Font LookAndFeel::getLabelFont(juce::Label& l) { return l.getFont(); }

void LookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    auto r = juce::Rectangle<int>(0, 0, w, h).toFloat().reduced(0.5f);
    g.setColour(Colours::panelRaised);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(box.isPopupActive() ? accent_.withAlpha(0.7f) : Colours::outline);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    const float ax = static_cast<float>(w) - 11.0f, ay = static_cast<float>(h) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(ax - 3.5f, ay - 1.5f);
    chevron.lineTo(ax, ay + 2.0f);
    chevron.lineTo(ax + 3.5f, ay - 1.5f);
    g.setColour(Colours::textMid);
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& s) {
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(uiFont(11.5f, Weight::Medium));
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::textColourId, Colours::textMid);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout(juce::Slider& s) {
    juce::Slider::SliderLayout layout;
    auto r = s.getLocalBounds();
    if (s.getTextBoxPosition() == juce::Slider::TextBoxBelow) {
        layout.textBoxBounds = r.removeFromBottom(16);
        r.removeFromBottom(2);
    }
    layout.sliderBounds = r;
    return layout;
}

void LookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.fillAll(Colours::panelRaised);
    g.setColour(Colours::outline);
    g.drawRect(0, 0, w, h);
}

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
    g.fillAll(Colours::panelRaised);
    g.setColour(Colours::outline);
    g.drawRect(0, 0, w, h);
    g.setColour(Colours::text);
    g.setFont(uiFont(12.0f));
    g.drawFittedText(text, 8, 0, w - 16, h, juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) {
    juce::GlyphArrangement ga;
    ga.addLineOfText(uiFont(12.0f), text, 0.0f, 0.0f);
    const int w = juce::jmin(320, static_cast<int>(ga.getBoundingBox(0, -1, true).getWidth()) + 18), h = 24;
    return juce::Rectangle<int>(pos.x + 12, pos.y + 16, w, h).constrainedWithin(parent);
}

void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                int thumbStart, int thumbSize, bool over, bool) {
    auto thumb = vertical ? juce::Rectangle<int>(x + w / 2 - 2, thumbStart, 4, thumbSize)
                          : juce::Rectangle<int>(thumbStart, y + h / 2 - 2, thumbSize, 4);
    g.setColour(over ? Colours::textDim : Colours::track);
    g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
}

} // namespace rrui
