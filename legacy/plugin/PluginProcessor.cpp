#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {
struct FactoryPreset {
    const char* name;
    const char* category;
    const char* description;
    float roomSize;
    float decay;
    float preDelay;
    float early;
    float late;
    float balance;
    float width;
};

constexpr FactoryPreset factoryPresets[] = {
    {"Small Scoring Room", "Rooms", "Compact, clear space for close orchestral cues.", 0.22f, 0.65f, 12.0f, 0.55f, 0.55f, 0.42f, 0.85f},
    {"Chamber Hall", "Chamber", "Moderate early detail for chamber textures.", 0.40f, 1.15f, 18.0f, 0.42f, 0.68f, 0.48f, 1.00f},
    {"Concert Hall", "Concert Halls", "Balanced hall starting point for mixed ensembles.", 0.62f, 1.75f, 24.0f, 0.34f, 0.76f, 0.52f, 1.08f},
    {"Large Orchestral Hall", "Large Halls", "Broad late field for full orchestral writing.", 0.78f, 2.50f, 32.0f, 0.28f, 0.82f, 0.56f, 1.15f},
    {"Cathedral", "Cathedral", "Long, spacious tail for slow architectural textures.", 1.00f, 4.20f, 42.0f, 0.22f, 0.88f, 0.62f, 1.20f},
    {"Solo Piano Hall", "Piano", "Pre-delay and width tuned to retain piano attacks.", 0.55f, 1.60f, 30.0f, 0.30f, 0.72f, 0.50f, 0.95f},
    {"Warm Strings", "Strings", "Gentle early field and restrained width for strings.", 0.68f, 2.10f, 20.0f, 0.38f, 0.78f, 0.46f, 0.92f},
    {"Full Orchestra", "Full Orchestra", "Dense, controlled starting point for tuttis.", 0.84f, 2.80f, 28.0f, 0.25f, 0.84f, 0.58f, 1.05f}
};
}

GrandHallAudioProcessor::GrandHallAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "PARAMETERS", createParameterLayout()) {
    for (const auto id : {"decay", "lowdecay", "highdecay", "highdamp"})
        parameters_.getParameter(id)->addListener(this);
}

GrandHallAudioProcessor::~GrandHallAudioProcessor() {
    {
        const std::lock_guard lock(loaderMutex_);
        stopLoader_ = true;
    }
    loaderWake_.notify_one();
    if (loaderThread_.joinable()) loaderThread_.join();
}

juce::AudioProcessorValueTreeState::ParameterLayout
GrandHallAudioProcessor::createParameterLayout() {
    using Parameter = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<Parameter>("wet", "Wet", 0.0f, 1.0f, 0.5f));
    parameters.push_back(std::make_unique<Parameter>("dry", "Dry", 0.0f, 1.0f, 0.5f));
    parameters.push_back(std::make_unique<Parameter>("predelay", "Pre-delay",
                                                      juce::NormalisableRange<float>(0.0f, 200.0f, 0.01f), 20.0f));
    parameters.push_back(std::make_unique<Parameter>("decay", "Decay",
                                                      juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f), 1.0f));
    parameters.push_back(std::make_unique<Parameter>("width", "Width", 0.0f, 2.0f, 1.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        "ir", "IR", juce::StringArray{"Default Hall", "User IR"}, 0));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Reverb Mode", juce::StringArray{"Convolution", "Algorithmic"}, 0));
    parameters.push_back(std::make_unique<Parameter>("roomsize", "Room Size", 0.0f, 1.0f, 0.65f));
    parameters.push_back(std::make_unique<Parameter>("early", "Early Reflections", 0.0f, 1.0f, 0.35f));
    parameters.push_back(std::make_unique<Parameter>("late", "Late Tail", 0.0f, 1.0f, 0.75f));
    parameters.push_back(std::make_unique<Parameter>("balance", "Early/Late Balance", 0.0f, 1.0f, 0.5f));
    parameters.push_back(std::make_unique<Parameter>("lowdecay", "Low Decay",
                                                      juce::NormalisableRange<float>(0.25f, 2.0f, 0.001f), 1.0f));
    parameters.push_back(std::make_unique<Parameter>("highdecay", "High Decay",
                                                      juce::NormalisableRange<float>(0.25f, 2.0f, 0.001f), 1.0f));
    parameters.push_back(std::make_unique<Parameter>("highdamp", "High Damping", 0.0f, 0.95f, 0.2f));
    parameters.push_back(std::make_unique<Parameter>("output", "Output",
                                                      juce::NormalisableRange<float>(-24.0f, 12.0f, 0.01f), 0.0f));
    const std::array<float, 5> frequencies{100.0f, 300.0f, 1000.0f, 4000.0f, 12000.0f};
    for (std::size_t band = 0; band < frequencies.size(); ++band) {
        const auto number = juce::String(static_cast<int>(band + 1));
        parameters.push_back(std::make_unique<Parameter>(
            "eq" + number + "freq", "EQ " + number + " Frequency",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.35f), frequencies[band]));
        parameters.push_back(std::make_unique<Parameter>(
            "eq" + number + "gain", "EQ " + number + " Gain",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
        parameters.push_back(std::make_unique<Parameter>(
            "eq" + number + "q", "EQ " + number + " Q",
            juce::NormalisableRange<float>(0.1f, 10.0f, 0.001f, 0.35f), 0.707f));
    }
    return {parameters.begin(), parameters.end()};
}

void GrandHallAudioProcessor::prepareToPlay(const double sampleRate,
                                             const int maximumExpectedSamplesPerBlock) {
    sampleRate_ = sampleRate;
    maximumBlockSize_ = static_cast<std::size_t>(maximumExpectedSamplesPerBlock);
    const auto irLength = static_cast<std::size_t>(sampleRate * tailLengthSeconds_);
    defaultImpulse_.resize(irLength);
    transitionLeft_.assign(maximumBlockSize_, 0.0f);
    transitionRight_.assign(maximumBlockSize_, 0.0f);
    algorithmicInputLeft_.assign(maximumBlockSize_, 0.0f);
    algorithmicInputRight_.assign(maximumBlockSize_, 0.0f);
    convolutionOutputLeft_.assign(maximumBlockSize_, 0.0f);
    convolutionOutputRight_.assign(maximumBlockSize_, 0.0f);
    for (std::size_t i = 0; i < irLength; ++i) {
        const auto time = static_cast<float>(i / sampleRate);
        defaultImpulse_[i] = std::exp(-3.5f * time / static_cast<float>(tailLengthSeconds_))
                             * (i == 0 ? 1.0f : 0.35f);
    }
    fallbackEngine_.prepare(sampleRate, maximumBlockSize_, irLength);
    fallbackEngine_.setImpulseResponse(defaultImpulse_, defaultImpulse_);
    fallbackEngine_.reset();
    algorithmicEngine_.prepare(sampleRate, maximumBlockSize_);
    algorithmicEngine_.reset();
    outputEq_.prepare(sampleRate);
    outputEq_.reset();
    previousEngine_ = nullptr;
    transitionRemaining_ = 0;
    transitionTotal_ = static_cast<std::size_t>(sampleRate * 0.1);
    activeResponse_ = nullptr;
    if (!loaderThread_.joinable())
        loaderThread_ = std::thread([this] { loaderLoop(); });
}

void GrandHallAudioProcessor::releaseResources() {
    fallbackEngine_.reset();
}

void GrandHallAudioProcessor::setCurrentProgram(const int index) {
    currentProgram_ = std::clamp(index, 0, getNumPrograms() - 1);
    const auto& preset = factoryPresets[currentProgram_];
    parameters_.getParameter("roomsize")->setValueNotifyingHost(preset.roomSize);
    parameters_.getParameter("decay")->setValueNotifyingHost(
        parameters_.getParameterRange("decay").convertTo0to1(preset.decay));
    parameters_.getParameter("predelay")->setValueNotifyingHost(
        parameters_.getParameterRange("predelay").convertTo0to1(preset.preDelay));
    parameters_.getParameter("early")->setValueNotifyingHost(preset.early);
    parameters_.getParameter("late")->setValueNotifyingHost(preset.late);
    parameters_.getParameter("balance")->setValueNotifyingHost(preset.balance);
    parameters_.getParameter("width")->setValueNotifyingHost(
        parameters_.getParameterRange("width").convertTo0to1(preset.width));
}

const juce::String GrandHallAudioProcessor::getProgramName(const int index) {
    return factoryPresets[std::clamp(index, 0, getNumPrograms() - 1)].name;
}

juce::String GrandHallAudioProcessor::getProgramCategory(const int index) const {
    return factoryPresets[std::clamp(index, 0, 7)].category;
}

juce::String GrandHallAudioProcessor::getProgramDescription(const int index) const {
    return factoryPresets[std::clamp(index, 0, 7)].description;
}

void GrandHallAudioProcessor::parameterValueChanged(const int, const float) {
    requestGeneration_.fetch_add(1, std::memory_order_acq_rel);
    rebuildRequested_.store(true, std::memory_order_release);
    loaderWake_.notify_one();
}

bool GrandHallAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GrandHallAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    acceptPendingResponse();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto size = static_cast<std::size_t>(buffer.getNumSamples());
    std::copy_n(left, size, algorithmicInputLeft_.begin());
    std::copy_n(right, size, algorithmicInputRight_.begin());
    grand_hall::ReverbParameters p;
    p.wet = parameters_.getRawParameterValue("wet")->load();
    p.dry = parameters_.getRawParameterValue("dry")->load();
    p.preDelayMs = parameters_.getRawParameterValue("predelay")->load();
    p.width = parameters_.getRawParameterValue("width")->load();
    const auto outputDb = parameters_.getRawParameterValue("output")->load();
    p.outputGain = juce::Decibels::decibelsToGain(outputDb);
    p.earlyLevel = parameters_.getRawParameterValue("early")->load();
    p.lateLevel = parameters_.getRawParameterValue("late")->load();
    p.earlyLateBalance = parameters_.getRawParameterValue("balance")->load();
    const auto lowDecay = parameters_.getRawParameterValue("lowdecay")->load();
    const auto highDecay = parameters_.getRawParameterValue("highdecay")->load();
    p.lowDamping = 1.0f - lowDecay * 0.5f;
    p.highDamping = parameters_.getRawParameterValue("highdamp")->load()
                  + (1.0f - highDecay) * 0.25f;
    grand_hall::AlgorithmicParameters algorithmic;
    algorithmic.roomSize = parameters_.getRawParameterValue("roomsize")->load();
    algorithmic.decaySeconds = static_cast<float>(
        parameters_.getRawParameterValue("decay")->load() * tailLengthSeconds_);
    algorithmic.preDelayMs = p.preDelayMs;
    algorithmic.earlyLevel = parameters_.getRawParameterValue("early")->load();
    algorithmic.lateLevel = parameters_.getRawParameterValue("late")->load();
    algorithmic.earlyLateBalance = parameters_.getRawParameterValue("balance")->load();
    algorithmic.lowDecay = lowDecay;
    algorithmic.highDecay = highDecay;
    algorithmic.highDamping = parameters_.getRawParameterValue("highdamp")->load();
    algorithmic.width = p.width;
    algorithmic.wet = p.wet;
    algorithmic.dry = p.dry;
    algorithmic.outputGain = p.outputGain;
    algorithmicEngine_.setParameters(algorithmic);
    algorithmicEngine_.process({algorithmicInputLeft_.data(), size},
                               {algorithmicInputRight_.data(), size});
    targetModeMix_ = parameters_.getRawParameterValue("mode")->load() > 0.5f ? 1.0f : 0.0f;
    std::copy_n(left, size, convolutionOutputLeft_.begin());
    std::copy_n(right, size, convolutionOutputRight_.begin());
    auto* response = activeResponse_;
    auto* engine = response == nullptr ? &fallbackEngine_ : &response->engine;
    if (previousEngine_ != nullptr && transitionRemaining_ > 0) {
        std::copy_n(left, size, transitionLeft_.begin());
        std::copy_n(right, size, transitionRight_.begin());
        previousEngine_->setParameters(p);
        previousEngine_->process({transitionLeft_.data(), size},
                                 {transitionRight_.data(), size});
    }
    std::array<grand_hall::EqBand, 5> eqBands{};
    for (std::size_t band = 0; band < eqBands.size(); ++band) {
        const auto number = juce::String(static_cast<int>(band + 1));
        eqBands[band].frequencyHz = parameters_.getRawParameterValue("eq" + number + "freq")->load();
        eqBands[band].gainDb = parameters_.getRawParameterValue("eq" + number + "gain")->load();
        eqBands[band].q = parameters_.getRawParameterValue("eq" + number + "q")->load();
    }
    outputEq_.setBands(eqBands);
    engine->setParameters(p);
    engine->process({convolutionOutputLeft_.data(), size},
                    {convolutionOutputRight_.data(), size});
    const auto modeStep = 1.0f / static_cast<float>(std::max<std::size_t>(1, transitionTotal_));
    for (std::size_t i = 0; i < size; ++i) {
        modeMix_ += std::clamp(targetModeMix_ - modeMix_, -modeStep, modeStep);
        left[i] = convolutionOutputLeft_[i] * (1.0f - modeMix_)
                + algorithmicInputLeft_[i] * modeMix_;
        right[i] = convolutionOutputRight_[i] * (1.0f - modeMix_)
                 + algorithmicInputRight_[i] * modeMix_;
    }
    if (previousEngine_ != nullptr && transitionRemaining_ > 0) {
        for (std::size_t i = 0; i < size; ++i) {
            if (transitionRemaining_ > 0) {
                const auto progress = 1.0f - static_cast<float>(transitionRemaining_)
                                    / static_cast<float>(transitionTotal_);
                const auto oldGain = std::max(0.0f, 1.0f - progress);
                const auto newGain = std::min(1.0f, progress);
                left[i] = transitionLeft_[i] * oldGain + left[i] * newGain;
                right[i] = transitionRight_[i] * oldGain + right[i] * newGain;
                --transitionRemaining_;
            }
        }
        if (transitionRemaining_ == 0) previousEngine_ = nullptr;
    }
    outputEq_.process({left, size}, {right, size});
}

juce::AudioProcessorEditor* GrandHallAudioProcessor::createEditor() {
    return new GrandHallEditor(*this);
}

void GrandHallAudioProcessor::getStateInformation(juce::MemoryBlock& state) {
    auto stateTree = parameters_.copyState();
    {
        const std::lock_guard lock(loaderMutex_);
        stateTree.setProperty("userIRPath", juce::String(loadedPath_), nullptr);
    }
    const auto xml = stateTree.createXml();
    copyXmlToBinary(*xml, state);
}

void GrandHallAudioProcessor::setStateInformation(const void* data, const int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (xml != nullptr && xml->hasTagName(parameters_.state.getType())) {
        parameters_.replaceState(juce::ValueTree::fromXml(*xml));
        const auto path = xml->getStringAttribute("userIRPath");
        if (path.isNotEmpty()) loadUserImpulseResponse(juce::File(path));
    }
}

void GrandHallAudioProcessor::loadUserImpulseResponse(const juce::File& file) {
    if (!file.existsAsFile()) return;
    {
        const std::lock_guard lock(loaderMutex_);
        requestedPath_ = file.getFullPathName().toStdString();
        loadedPath_ = requestedPath_;
        lastLoadError_.clear();
    }
    requestGeneration_.fetch_add(1, std::memory_order_acq_rel);
    rebuildRequested_.store(true, std::memory_order_release);
    parameters_.getParameter("ir")->setValueNotifyingHost(1.0f);
    loaderWake_.notify_one();
}

void GrandHallAudioProcessor::saveUserPreset(const juce::File& file) {
    if (file.existsAsFile()) {
        juce::Logger::writeToLog("Grand Hall preset already exists: " + file.getFullPathName());
        return;
    }
    auto tree = parameters_.copyState();
    tree.setProperty("presetSchema", 1, nullptr);
    tree.setProperty("presetName", file.getFileNameWithoutExtension(), nullptr);
    tree.setProperty("presetCategory", "User Presets", nullptr);
    tree.setProperty("presetDescription", "User-created Grand Hall settings", nullptr);
    if (!file.replaceWithText(tree.createXml()->toString()))
        juce::Logger::writeToLog("Could not write Grand Hall preset: " + file.getFullPathName());
}

bool GrandHallAudioProcessor::loadUserPreset(const juce::File& file) {
    if (!file.existsAsFile()) return false;
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName(parameters_.state.getType())
        || xml->getIntAttribute("presetSchema", 0) != 1)
        return false;
    parameters_.replaceState(juce::ValueTree::fromXml(*xml));
    const auto path = xml->getStringAttribute("userIRPath");
    if (path.isNotEmpty()) loadUserImpulseResponse(juce::File(path));
    return true;
}

juce::String GrandHallAudioProcessor::getIRStatus() const {
    const std::lock_guard lock(loaderMutex_);
    if (!lastLoadError_.empty())
        return "IR error: " + juce::String(lastLoadError_);
    if (loadedPath_.empty())
        return "IR: Generated Default Hall";
    return "IR: " + juce::File(loadedPath_).getFileName();
}

void GrandHallAudioProcessor::acceptPendingResponse() noexcept {
    if (auto* response = pendingResponse_.exchange(nullptr, std::memory_order_acq_rel)) {
        const auto responseGeneration = pendingGeneration_.load(std::memory_order_acquire);
        if (responseGeneration != requestGeneration_.load(std::memory_order_acquire))
            return;
        previousEngine_ = activeResponse_ == nullptr ? &fallbackEngine_ : &activeResponse_->engine;
        transitionRemaining_ = transitionTotal_;
        response->engine.reset();
        activeResponse_ = response;
        activeResponseAtomic_.store(response, std::memory_order_release);
    }
}

void GrandHallAudioProcessor::loaderLoop() {
    for (;;) {
        std::string path;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock(loaderMutex_);
            loaderWake_.wait(lock, [this] {
                return stopLoader_ || !requestedPath_.empty()
                    || rebuildRequested_.load(std::memory_order_acquire);
            });
            if (stopLoader_) return;
            path = std::exchange(requestedPath_, {});
            if (path.empty()) path = loadedPath_;
            generation = requestGeneration_.load(std::memory_order_acquire);
        }
        try {
            auto shape = grand_hall::DecayShape{};
            shape.referenceSeconds = static_cast<float>(tailLengthSeconds_);
            shape.multiplier = parameters_.getRawParameterValue("decay")->load();
            shape.lowMultiplier = parameters_.getRawParameterValue("lowdecay")->load();
            shape.highMultiplier = parameters_.getRawParameterValue("highdecay")->load();
            shape.highDamping = parameters_.getRawParameterValue("highdamp")->load();
            std::unique_ptr<grand_hall::PreparedResponse> prepared;
            if (!path.empty()) {
                prepared = std::make_unique<grand_hall::PreparedResponse>(
                    grand_hall::prepareWavResponse(path, sampleRate_, maximumBlockSize_, shape));
            } else {
                auto shapedLeft = grand_hall::shapeDecay(defaultImpulse_, sampleRate_, shape);
                auto shapedRight = shapedLeft;
                const auto leftComponents = grand_hall::splitImpulseResponse(
                    shapedLeft, sampleRate_);
                const auto rightComponents = grand_hall::splitImpulseResponse(
                    shapedRight, sampleRate_);
                prepared = std::make_unique<grand_hall::PreparedResponse>();
                prepared->engine.prepare(sampleRate_, maximumBlockSize_, shapedLeft.size());
                prepared->engine.setImpulseResponseComponents(
                    leftComponents.early, leftComponents.late,
                    rightComponents.early, rightComponents.late);
                prepared->metadata.name = "Generated Default Hall";
                prepared->metadata.source = "Grand Hall generated response";
                prepared->metadata.referenceDecaySeconds = shape.referenceSeconds;
                prepared->metadata.notes = "Synthetic fallback response; no third-party material is bundled.";
            }
            if (generation != requestGeneration_.load(std::memory_order_acquire))
                continue;
            std::lock_guard lock(loaderMutex_);
            if (generation != requestGeneration_.load(std::memory_order_acquire))
                continue;
            if (responseOwners_.size() >= 8) {
                const auto active = activeResponseAtomic_.load(std::memory_order_acquire);
                const auto pending = pendingResponse_.load(std::memory_order_acquire);
                const auto reusable = std::find_if(responseOwners_.begin(), responseOwners_.end(),
                    [active, pending](const auto& owner) {
                        return owner.get() != active && owner.get() != pending;
                    });
                if (reusable == responseOwners_.end())
                    throw std::runtime_error("All prepared IR slots are active; try again after the next audio block.");
                responseOwners_.erase(reusable);
            }
            auto* raw = prepared.get();
            responseOwners_.push_back(std::move(prepared));
            pendingGeneration_.store(generation, std::memory_order_release);
            pendingResponse_.store(raw, std::memory_order_release);
            rebuildRequested_.store(false, std::memory_order_release);
        } catch (const std::exception& error) {
            const std::lock_guard lock(loaderMutex_);
            lastLoadError_ = error.what();
            if (generation == requestGeneration_.load(std::memory_order_acquire))
                rebuildRequested_.store(false, std::memory_order_release);
            juce::Logger::writeToLog("Grand Hall IR load failed: " + juce::String(error.what()));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new GrandHallAudioProcessor();
}
