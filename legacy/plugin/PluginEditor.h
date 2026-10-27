#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class GrandHallEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer {
public:
    explicit GrandHallEditor(GrandHallAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    GrandHallAudioProcessor& processor_;
    juce::TextButton loadButton_{"LOAD IR"};
    juce::TextButton savePresetButton_{"SAVE PRESET"};
    juce::TextButton loadPresetButton_{"OPEN PRESET"};
    juce::TextButton eqToggleButton_{"SHOW EQ"};
    juce::ComboBox modeBox_;
    juce::ComboBox presetBox_;
    juce::ComboBox categoryBox_;
    juce::TextEditor searchBox_;
    juce::TextButton favouriteButton_{"FAVOURITE"};
    juce::Label title_;
    juce::Label status_;
    juce::Label mainHeading_;
    juce::Label toneHeading_;
    juce::Label irHeading_;
    juce::Label irStatus_;
    juce::Label modeStatus_;
    std::vector<std::unique_ptr<juce::Slider>> sliders_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    std::vector<juce::String> sliderIds_;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments_;
    std::unique_ptr<ComboAttachment> modeAttachment_;
    std::unique_ptr<juce::FileChooser> ownedChooser_;
    int presetIndex_ = 0;
    bool eqVisible_ = false;
    void rebuildPresetList();

    void addSlider(const juce::String& id, const juce::String& name);
    void setEqVisible(bool visible);
    void showFileChooser(bool preset);
    void timerCallback() override;
    static juce::Colour panelColour() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrandHallEditor)
};
