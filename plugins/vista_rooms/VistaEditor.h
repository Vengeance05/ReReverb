#pragma once

#include "VistaProcessor.h"

#include <map>

// Central screen: reflection pattern + reverb envelope and key readouts.
class VistaDisplay : public juce::Component, private juce::Timer {
public:
    VistaDisplay(juce::AudioProcessorValueTreeState& s, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    void recomputeTaps();
    juce::AudioProcessorValueTreeState& state_;
    juce::Colour accent_;
    std::vector<std::pair<float, float>> taps_;
    rr::ErSettings lastEr_{};
    bool first_ = true;
};

class VistaEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit VistaEditor(VistaProcessor&);
    ~VistaEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    rrui::Knob& knob(juce::Component& parent, const juce::String& id, const juce::String& label,
                     rrui::KnobSize size, bool bipolar = false);
    rrui::Choice& choice(juce::Component& parent, const juce::String& id, const juce::String& label = {});
    rrui::Switch& toggle(juce::Component& parent, const juce::String& id, const juce::String& text);
    juce::Component* c(const juce::String& id) { return controls_.at(id).get(); }

    VistaProcessor& proc_;
    rrui::LookAndFeel lnf_;
    std::map<juce::String, std::unique_ptr<juce::Component>> controls_;
    std::vector<std::unique_ptr<juce::Component>> extras_;
    rrui::Panel reflPanel_, revPanel_, outPanel_, masterPanel_;
    rrui::PresetBar presetBar_;
    VistaDisplay display_;
    rrui::MeterBank meters_;
    rrui::ResponseCurve eqCurve_;
    juce::TooltipWindow tooltips_{this, 700};
};
