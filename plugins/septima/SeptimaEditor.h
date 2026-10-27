#pragma once

#include "SeptimaProcessor.h"

#include <map>

class SeptimaEditor;

// Central screen: program name, key readouts and the decay-vs-frequency graph.
class SeptimaDisplay : public juce::Component {
public:
    SeptimaDisplay(SeptimaProcessor& p, juce::Colour accent);
    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(const SeptimaProcessor::Status& st) { status_ = st; repaint(); }
    juce::TextButton browse{"BROWSE PROGRAMS"};
private:
    void paintDecayGraph(juce::Graphics&, juce::Rectangle<float>);
    SeptimaProcessor& proc_;
    juce::Colour accent_;
    SeptimaProcessor::Status status_;
};

// Full-panel program matrix: categories on the left, programs in a grid.
class ProgramBrowser : public juce::Component {
public:
    ProgramBrowser(SeptimaProcessor& p, juce::Colour accent);
    void setPrograms(std::vector<rr::IrProgram> programs, const juce::String& currentKey);
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }
    std::function<void()> onClose;
private:
    void layoutCells();
    SeptimaProcessor& proc_;
    juce::Colour accent_;
    std::vector<rr::IrProgram> programs_;
    std::vector<juce::String> categories_;
    juce::String category_, currentKey_;
    std::vector<std::pair<juce::Rectangle<int>, int>> cells_;
    std::vector<juce::Rectangle<int>> catRects_;
    juce::Rectangle<int> closeRect_;
    int hover_ = -1;
};

class SeptimaEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SeptimaEditor(SeptimaProcessor&);
    ~SeptimaEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseLibrary();
    rrui::Knob& knob(juce::Component& parent, const juce::String& id, const juce::String& label,
                     rrui::KnobSize size, bool bipolar = false);
    rrui::Choice& choice(juce::Component& parent, const juce::String& id, const juce::String& label = {});
    juce::Component* c(const juce::String& id) { return controls_.at(id).get(); }

    SeptimaProcessor& proc_;
    rrui::LookAndFeel lnf_;
    std::map<juce::String, std::unique_ptr<juce::Component>> controls_;
    rrui::Panel mainPanel_, outPanel_, earlyPanel_, latePanel_, shapePanel_, eqPanel_;
    rrui::PresetBar presetBar_;
    SeptimaDisplay display_;
    ProgramBrowser browser_;
    rrui::MeterBank meters_;
    rrui::ResponseCurve eqCurve_;
    juce::TextButton libraryButton_{"IR LIBRARY"}, sampledButton_{"SAMPLED"};
    std::unique_ptr<rrui::Switch> eqOn_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::TooltipWindow tooltips_{this, 700};
    int lastGeneration_ = -1;
    juce::Rectangle<int> eqGroups_[4];
};
