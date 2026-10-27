#pragma once

#include "PresetManager.h"
#include "TailConvolver.h"
#include "Widgets.h"
#include "rr/ir_reshaper.h"
#include "rr/m7_library.h"
#include "rr/septima_engine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <mutex>

class SeptimaProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::Thread,
                         private juce::AsyncUpdater {
public:
    SeptimaProcessor();
    ~SeptimaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Septima"; }
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

    // ---- Library / program API (message thread) ----
    void setLibraryFolder(const juce::File& folder);
    juce::File libraryFolder() const;
    std::vector<rr::IrProgram> programs() const;
    juce::String currentProgramKey() const;
    void selectProgram(const juce::String& key, bool applySampledDecay);

    struct Status {
        juce::String text;
        float sampledDecay = 0.0f;
        juce::String format;
        double sampleRate = 0.0;
        bool busy = false;
        bool ready = false;
        int generation = 0;
        std::array<float, rr::kDecayBands> bandRt{};
    };
    Status status() const;

    struct Meters { std::atomic<float> in{0}, early{0}, late{0}, vlf{0}, out{0}; };
    Meters meters;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct IrPackage {
        juce::AudioBuffer<float> earlyA, earlyB, lateA, lateB, vlfA, vlfB;   // late/VLF: first 2P samples only
        std::unique_ptr<septima::TailSpectra> tailA, tailB;                   // the rest, for the two tail stages
        double sampleRate = 48000.0;
        float syntheticErGain = 1.0f;
        int totalLength = 0;                                                  // full late IR length (samples)
    };

    void parameterChanged(const juce::String& id, float) override;
    void run() override;
    void handleAsyncUpdate() override;
    void doLoadProgram(const juce::String& key);
    void doReshape();
    rr::SeptimaSettings buildSettings(double bpm) const;
    float p(const char* id) const { return state_.getRawParameterValue(id)->load(); }
    void setStatus(const juce::String& text, bool busy);
    static juce::File globalSettingsFile();
public:
    static juce::File bundledLibraryFolder();
private:
    void saveGlobalLibrary(const juce::File& folder);

    juce::AudioProcessorValueTreeState state_;
    std::unique_ptr<rrui::PresetManager> presets_;
    rr::SeptimaEngine engine_;

    juce::dsp::ConvolutionMessageQueue queue_;
    juce::dsp::Convolution earlyA_{juce::dsp::Convolution::NonUniform{256}, queue_};
    juce::dsp::Convolution earlyB_{juce::dsp::Convolution::NonUniform{256}, queue_};
    juce::dsp::Convolution lateA_{juce::dsp::Convolution::NonUniform{256}, queue_};
    juce::dsp::Convolution lateB_{juce::dsp::Convolution::NonUniform{256}, queue_};
    juce::dsp::Convolution vlfA_{juce::dsp::Convolution::NonUniform{256}, queue_};
    juce::dsp::Convolution vlfB_{juce::dsp::Convolution::NonUniform{256}, queue_};
    // Late/VLF IRs: [0, 2Pa) on the audio thread, [2Pa, 2Pb) on tailA_ (partition Pa),
    // [2Pb, end) on tailB_ (partition Pb = 8 Pa).
    septima::TailConvolver tailA_{"Septima tail A"}, tailB_{"Septima tail B"};
    juce::AudioBuffer<float> eA_, eB_, lA_, lB_, v_, vB_, feedL_, feedR_, feedV_;
    int maxBlock_ = 512;
    double sampleRate_ = 48000.0;
    std::atomic<double> hostRate_{0.0};   // IRs are resampled to this in the worker

    // Worker <-> audio handoff.
    std::atomic<IrPackage*> pending_{nullptr};
    std::array<std::atomic<IrPackage*>, 8> retired_{};
    IrPackage* overflow_ = nullptr; // audio thread only
    std::atomic<bool> irReady_{false};
    // Offline (bounce) support: the worker flags when it is mid-job, and the audio
    // thread remembers the IR sizes it handed to the convolvers so it can tell when
    // their background loading has finished.
    std::atomic<bool> workerBusy_{false}, reshapeBusy_{false};
    std::array<int, 6> expectedIrSize_{}, previousIrSize_{};
    double convLoadStartMs_ = 0.0;
    bool convLoadPending_ = false;
    bool offlineGaveUp_ = false;   // a wait timed out (e.g. no library): never block again
    juce::AudioBuffer<float> offlineScratch_;
    void acceptPendingPackage();
    void waitUntilReadyForOffline();
    // Silence skipping: once the input has been silent for longer than the whole
    // response (plus the longest pre-delay/delay), every convolution output is
    // zero, so the convolvers are not run until signal returns.
    long long silentSamples_ = 1LL << 40;   // starts idle, so a bounce's first block may wait
    int irLength_ = 0;
    std::atomic<float> syntheticErGain_{1.0f};

    // Worker state (guarded by mutex_).
    mutable std::mutex mutex_;
    juce::File libraryFolder_;
    std::vector<rr::IrProgram> programs_;
    juce::String programKey_;
    juce::String requestedKey_;
    bool rescanRequested_ = false;
    bool applySampledDecay_ = false;
    Status status_;
    std::unique_ptr<rr::TrueStereoIr> ir_;
    rr::IrAnalysis analysis_;
    std::atomic<bool> loadRequested_{false};
    std::atomic<bool> reshapeRequested_{false};
    std::atomic<juce::int64> reshapeDue_{0};
    std::atomic<float> pendingDecayValue_{-1.0f};

    std::atomic<bool> dirty_{true};
    double lastBpm_ = 120.0;
    juce::AudioFormatManager formats_;
    std::vector<juce::String> ids_;
};
