#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rrui {

namespace Colours {
inline const juce::Colour background{0xff0d0e10};
inline const juce::Colour header{0xff121316};
inline const juce::Colour panel{0xff16181b};
inline const juce::Colour panelTop{0xff1a1c20};
inline const juce::Colour panelRaised{0xff202328};
inline const juce::Colour outline{0xff25282e};
inline const juce::Colour outlineSoft{0xff1e2125};
inline const juce::Colour text{0xffe4e7eb};
inline const juce::Colour textMid{0xffaab0b8};
inline const juce::Colour textDim{0xff6f757e};
inline const juce::Colour track{0xff2a2e34};
inline const juce::Colour display{0xff08090a};
} // namespace Colours

enum class Weight { Regular, Medium, SemiBold };

// Embedded Inter typeface.
juce::Font uiFont(float height, Weight weight = Weight::Regular);
// Small caps style label font (letter-spaced).
juce::Font capsFont(float height = 10.5f);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit LookAndFeel(juce::Colour accent);
    juce::Colour accent() const noexcept { return accent_; }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                       int thumbStart, int thumbSize, bool over, bool down) override;

private:
    juce::Colour accent_;
};

} // namespace rrui
