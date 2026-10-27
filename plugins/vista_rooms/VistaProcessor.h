#pragma once

#include "PresetManager.h"
#include "Widgets.h"
#include "rr/vista_engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

class VistaProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener {
public:
    VistaProcessor();
    ~VistaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vista Rooms"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    int getNumPrograms() override { return presets_ ? std::max(1, presets_->factoryCount()) : 1; }
    int getCurrentProgram() override { return presets_ ? presets_->hostProgram() : 0; }
    void setCurrentProgram(int i) override { if (presets_) presets_->setHostProgram(i); }
    const juce::String getProgramName(int i) override { return presets_ ? presets_->hostProgramName(i) : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& state() noexcept { return state_; }
    rrui::PresetManager& presets() noexcept { return *presets_; }

    struct Meters { std::atomic<float> in{0}, refl{0}, reverb{0}, out{0}, duck{0}; };
    Meters meters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged(const juce::String&, float) override { dirty_ = true; }
    rr::VistaSettings buildSettings(double bpm) const;
    float p(const char* id) const;

    juce::AudioProcessorValueTreeState state_;
    std::unique_ptr<rrui::PresetManager> presets_;
    rr::VistaEngine engine_;
    std::atomic<bool> dirty_{true};
    double lastBpm_ = 120.0;
    std::vector<juce::String> ids_;
};
