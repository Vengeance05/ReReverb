#pragma once

#include "LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace rrui {

// After AudioProcessorValueTreeState::replaceState: make every parameter's
// normalised value exactly match the restored state. (replaceState skips values
// that are equal after snapping, so e.g. a bool left at a raw 0.46 by a host
// would otherwise survive a restore of "off".)
void syncParametersToState(juce::AudioProcessorValueTreeState& state);

class PresetManager {
public:
    struct Factory {
        juce::String name;
        juce::String category;
        std::vector<std::pair<juce::String, float>> values; // param id -> real value
        juce::String extra;  // plugin-specific (Septima: program key)
    };
    struct Entry {
        juce::String name;
        juce::String category;
        bool factory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    PresetManager(juce::AudioProcessorValueTreeState& state, juce::String pluginName, std::vector<Factory> factory);

    // Optional hooks for non-parameter state (e.g. Septima's program selection).
    std::function<void(juce::XmlElement&)> writeExtra;
    std::function<void(const juce::XmlElement&)> readExtra;
    std::function<void()> onPresetLoaded;
    std::function<void(const Factory&)> applyFactoryExtra;

    juce::File userFolder() const;
    void refresh();
    const std::vector<Entry>& entries() const noexcept { return entries_; }
    void load(int entryIndex);
    void step(int direction);
    bool save(const juce::String& name);
    bool loadFile(const juce::File& file);
    int currentIndex() const noexcept { return current_; }
    // Factory presets come first in entries(), so they double as host programs.
    int factoryCount() const noexcept { return static_cast<int>(factory_.size()); }
    int hostProgram() const noexcept { return current_ >= 0 && current_ < factoryCount() ? current_ : 0; }
    juce::String hostProgramName(int i) const {
        if (i < 0 || i >= factoryCount() || i >= static_cast<int>(entries_.size())) return {};
        const auto& e = entries_[static_cast<std::size_t>(i)];
        return e.category + " - " + e.name;
    }
    // Ignores a request for the program that is already current, so hosts that
    // re-send the current program after restoring a session don't clobber edits.
    void setHostProgram(int i) {
        if (i < 0 || i >= factoryCount() || i == hostProgram()) return;
        load(i);
    }
    juce::String currentName() const { return currentName_; }
    void setCurrentName(const juce::String& n) { currentName_ = n; }

private:
    juce::AudioProcessorValueTreeState& state_;
    juce::String pluginName_;
    std::vector<Factory> factory_;
    std::vector<Entry> entries_;
    int current_ = -1;
    juce::String currentName_ = "Default";
    void applyFactory(const Factory& f);
};

// Header preset selector: [<] [ Category  >  Name  v ] [>]  [SAVE]
class PresetBar : public juce::Component {
public:
    explicit PresetBar(PresetManager& pm, juce::Colour accent);
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refreshMenu() { repaint(); }
private:
    void showMenu();
    void promptSave();
    PresetManager& pm_;
    juce::Colour accent_;
    std::unique_ptr<juce::Button> prev_, next_;
    juce::TextButton save_{"SAVE"};
    juce::Rectangle<int> field_;
};

} // namespace rrui
