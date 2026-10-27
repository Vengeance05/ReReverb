#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "grand_hall/prepared_response.h"
#include "grand_hall/algorithmic_reverb.h"
#include "grand_hall/reverb_engine.h"
#include "grand_hall/five_band_eq.h"

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <thread>

class GrandHallAudioProcessor final : public juce::AudioProcessor,
                                      private juce::AudioProcessorParameter::Listener {
public:
    GrandHallAudioProcessor();
    ~GrandHallAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Grand Hall Professional"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tailLengthSeconds_; }
    int getNumPrograms() override { return 8; }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    juce::String getProgramCategory(int index) const;
    juce::String getProgramDescription(int index) const;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void loadUserImpulseResponse(const juce::File& file);
    void saveUserPreset(const juce::File& file);
    bool loadUserPreset(const juce::File& file);
    juce::String getIRStatus() const;
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters_; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState parameters_;
    grand_hall::ReverbEngine fallbackEngine_;
    grand_hall::AlgorithmicReverb algorithmicEngine_;
    grand_hall::FiveBandEq outputEq_;
    grand_hall::PreparedResponse* activeResponse_ = nullptr;
    std::atomic<grand_hall::PreparedResponse*> activeResponseAtomic_{nullptr};
    std::atomic<grand_hall::PreparedResponse*> pendingResponse_{nullptr};
    std::atomic<std::uint64_t> requestGeneration_{0};
    std::atomic<std::uint64_t> pendingGeneration_{0};
    std::vector<std::unique_ptr<grand_hall::PreparedResponse>> responseOwners_;
    std::thread loaderThread_;
    mutable std::mutex loaderMutex_;
    std::condition_variable loaderWake_;
    std::string requestedPath_;
    std::string loadedPath_;
    bool stopLoader_ = false;
    std::string lastLoadError_;
    double sampleRate_ = 44100.0;
    std::size_t maximumBlockSize_ = 0;
    std::vector<float> defaultImpulse_;
    std::vector<float> transitionLeft_;
    std::vector<float> transitionRight_;
    std::vector<float> algorithmicInputLeft_;
    std::vector<float> algorithmicInputRight_;
    std::vector<float> convolutionOutputLeft_;
    std::vector<float> convolutionOutputRight_;
    float modeMix_ = 0.0f;
    float targetModeMix_ = 0.0f;
    grand_hall::ReverbEngine* previousEngine_ = nullptr;
    std::size_t transitionRemaining_ = 0;
    std::size_t transitionTotal_ = 0;
    double tailLengthSeconds_ = 2.5;

    void loaderLoop();
    void acceptPendingResponse() noexcept;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int, bool) override {}
    std::atomic<bool> rebuildRequested_{false};
    int currentProgram_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrandHallAudioProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
