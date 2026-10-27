#pragma once

#include "LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace rrui {

// ----------------------------------------------------------------------------
// Parameter construction helpers.
enum class Unit { None, Percent, Ms, Seconds, Hz, Db, Multiplier, Bipolar };

juce::String formatValue(float v, Unit unit);
float parseValue(const juce::String& text, Unit unit);

using ParamList = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

void addFloat(ParamList& list, const juce::String& id, const juce::String& name, float min, float max,
              float def, Unit unit, float centre = 0.0f, float step = 0.0f);
void addChoice(ParamList& list, const juce::String& id, const juce::String& name,
               const juce::StringArray& choices, int def);
void addBool(ParamList& list, const juce::String& id, const juce::String& name, bool def);

const juce::StringArray& syncChoices();
double syncSeconds(int index, double bpm); // 0 if free

// ----------------------------------------------------------------------------
enum class KnobSize { Small, Medium, Large };

class Knob : public juce::Component {
public:
    Knob(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label,
         KnobSize size = KnobSize::Medium, bool bipolar = false);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& slider() noexcept { return slider_; }
    static int dial(KnobSize s) noexcept { return s == KnobSize::Large ? 68 : s == KnobSize::Medium ? 50 : 38; }
    static int cellWidth(KnobSize s) noexcept { return s == KnobSize::Large ? 92 : s == KnobSize::Medium ? 74 : 62; }
    static int cellHeight(KnobSize s) noexcept { return 16 + dial(s) + 20; }
    KnobSize size() const noexcept { return size_; }
private:
    KnobSize size_;
    juce::String label_;
    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

class Choice : public juce::Component {
public:
    Choice(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label = {});
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::ComboBox& box() noexcept { return box_; }
    static constexpr int kHeight = 22;
private:
    juce::String label_;
    juce::ComboBox box_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;
};

class Switch : public juce::Component {
public:
    Switch(juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text);
    void resized() override { button_.setBounds(getLocalBounds()); }
    juce::TextButton& button() noexcept { return button_; }
private:
    juce::TextButton button_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment_;
};

// Titled panel with optional tabbed pages inside it.
class Panel : public juce::Component {
public:
    explicit Panel(juce::String title, juce::Colour accent);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

    juce::Rectangle<int> content() const { return getLocalBounds().reduced(14, 0).withTrimmedTop(32).withTrimmedBottom(12); }

    // Tabbed pages: each page lays out its own children via `layout`.
    struct Page : juce::Component {
        std::function<void(juce::Rectangle<int>)> layout;
        void resized() override { if (layout) layout(getLocalBounds()); }
    };
    Page& addPage(const juce::String& name);
    void setTabArea(juce::Rectangle<int> stripAndPages) { tabArea_ = stripAndPages; resized(); }
    void showPage(int index);
    int currentPage() const noexcept { return current_; }
    std::function<void(int)> onPageChanged;

private:
    juce::String title_;
    juce::Colour accent_;
    std::vector<std::pair<juce::String, std::unique_ptr<Page>>> pages_;
    std::vector<juce::Rectangle<int>> tabRects_;
    juce::Rectangle<int> tabArea_;
    int current_ = 0;
};

// Peak meters with dB scale.
class MeterBank : public juce::Component {
public:
    MeterBank(juce::StringArray labels, juce::Colour accent);
    void setLevels(const std::vector<float>& linearPeaks);
    void paint(juce::Graphics& g) override;
private:
    juce::StringArray labels_;
    juce::Colour accent_;
    std::vector<float> display_, hold_;
    std::vector<int> holdAge_;
};

// Frequency response view; `provider` returns the active filter stages.
struct CurveStage { double b0, b1, b2, a1, a2; };
class ResponseCurve : public juce::Component, private juce::Timer {
public:
    ResponseCurve(juce::Colour accent, std::function<std::vector<CurveStage>()> provider,
                  std::function<bool()> enabled);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    juce::Colour accent_;
    std::function<std::vector<CurveStage>()> provider_;
    std::function<bool()> enabled_;
};

// Dark "display glass" background used by the central screens.
void paintDisplayGlass(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent);

// Places fixed-size knob cells (and other components) centred in a row.
void placeRow(juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int cellWidth);
// Places components left-to-right with explicit widths.
void placeFlow(juce::Rectangle<int> area, std::initializer_list<std::pair<juce::Component*, int>> items, int gap = 8);

// Small caps group heading with a hairline, drawn by the owner.
void paintGroupLabel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text);

} // namespace rrui
