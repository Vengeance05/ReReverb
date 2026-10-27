#include "PresetManager.h"

namespace rrui {

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& state, juce::String pluginName,
                             std::vector<Factory> factory)
    : state_(state), pluginName_(std::move(pluginName)), factory_(std::move(factory)) {
    refresh();
}

juce::File PresetManager::userFolder() const {
#if JUCE_MAC
    auto base = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Presets/Rereverb");
#else
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Rereverb/Presets");
#endif
    return base.getChildFile(pluginName_);
}

void syncParametersToState(juce::AudioProcessorValueTreeState& state) {
    for (auto* p : state.processor.getParameters()) {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (rp == nullptr) continue;
        if (auto* raw = state.getRawParameterValue(rp->getParameterID())) {
            const float norm = rp->convertTo0to1(raw->load());
            if (std::abs(rp->getValue() - norm) > 1.0e-7f) rp->setValueNotifyingHost(norm);
        }
    }
}

void PresetManager::refresh() {
    entries_.clear();
    // Group factory presets by category (in order of first appearance).
    juce::StringArray categories;
    for (const auto& f : factory_) categories.addIfNotAlreadyThere(f.category);
    for (const auto& cat : categories)
        for (int i = 0; i < static_cast<int>(factory_.size()); ++i)
            if (factory_[static_cast<std::size_t>(i)].category == cat)
                entries_.push_back({factory_[static_cast<std::size_t>(i)].name, cat, true, i, {}});
    auto folder = userFolder();
    if (folder.isDirectory()) {
        auto files = folder.findChildFiles(juce::File::findFiles, true, "*.rrpreset");
        files.sort();
        for (const auto& f : files) {
            const auto rel = f.getParentDirectory() == folder ? juce::String("User")
                                                               : "User / " + f.getParentDirectory().getFileName();
            entries_.push_back({f.getFileNameWithoutExtension(), rel, false, -1, f});
        }
    }
}

void PresetManager::applyFactory(const Factory& f) {
    for (auto* p : state_.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            rp->setValueNotifyingHost(rp->getDefaultValue());
    for (const auto& [id, value] : f.values)
        if (auto* p = state_.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    if (applyFactoryExtra) applyFactoryExtra(f);
}

void PresetManager::load(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    const auto& e = entries_[static_cast<std::size_t>(index)];
    if (e.factory) {
        applyFactory(factory_[static_cast<std::size_t>(e.factoryIndex)]);
    } else if (!loadFile(e.file)) {
        return;
    }
    current_ = index;
    currentName_ = e.name;
    if (onPresetLoaded) onPresetLoaded();
}

void PresetManager::step(int direction) {
    if (entries_.empty()) return;
    const int n = static_cast<int>(entries_.size());
    load(((current_ < 0 ? 0 : current_ + direction) % n + n) % n);
}

bool PresetManager::loadFile(const juce::File& file) {
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("RereverbPreset") || xml->getStringAttribute("plugin") != pluginName_)
        return false;
    if (auto* params = xml->getChildByName(state_.state.getType())) {
        auto tree = juce::ValueTree::fromXml(*params);
        for (auto* p : state_.processor.getParameters()) {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
            if (rp == nullptr) continue;
            auto child = tree.getChildWithProperty("id", rp->paramID);
            const float v = child.isValid() ? static_cast<float>(child.getProperty("value")) : rp->convertFrom0to1(rp->getDefaultValue());
            rp->setValueNotifyingHost(rp->convertTo0to1(v));
        }
    }
    if (readExtra) if (auto* extra = xml->getChildByName("Extra")) readExtra(*extra);
    currentName_ = file.getFileNameWithoutExtension();
    return true;
}

bool PresetManager::save(const juce::String& name) {
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) return false;
    auto folder = userFolder();
    folder.createDirectory();
    auto file = folder.getChildFile(clean + ".rrpreset");
    juce::XmlElement root("RereverbPreset");
    root.setAttribute("plugin", pluginName_);
    root.setAttribute("version", 1);
    if (auto params = state_.copyState().createXml()) root.addChildElement(params.release());
    if (writeExtra) {
        auto* extra = root.createNewChildElement("Extra");
        writeExtra(*extra);
    }
    if (!root.writeTo(file)) return false;
    refresh();
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        if (!entries_[static_cast<std::size_t>(i)].factory && entries_[static_cast<std::size_t>(i)].file == file) current_ = i;
    currentName_ = clean;
    return true;
}

// ----------------------------------------------------------------------------
namespace {
class ArrowButton : public juce::TextButton {
public:
    explicit ArrowButton(bool left) : left_(left) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(down ? Colours::panelRaised.darker(0.1f) : over ? Colours::panelRaised.brighter(0.06f) : Colours::panelRaised);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(Colours::outline);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
        const auto c = r.getCentre();
        juce::Path p;
        const float d = left_ ? 1.0f : -1.0f;
        p.startNewSubPath(c.x + 2.0f * d, c.y - 4.0f);
        p.lineTo(c.x - 2.0f * d, c.y);
        p.lineTo(c.x + 2.0f * d, c.y + 4.0f);
        g.setColour(over ? Colours::text : Colours::textMid);
        g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    bool left_;
};
} // namespace

PresetBar::PresetBar(PresetManager& pm, juce::Colour accent) : pm_(pm), accent_(accent) {
    prev_ = std::make_unique<ArrowButton>(true);
    next_ = std::make_unique<ArrowButton>(false);
    addAndMakeVisible(*prev_);
    addAndMakeVisible(*next_);
    prev_->onClick = [this] { pm_.step(-1); repaint(); };
    next_->onClick = [this] { pm_.step(1); repaint(); };
    prev_->setTooltip("Previous preset");
    next_->setTooltip("Next preset");
    addAndMakeVisible(save_);
    save_.setTooltip("Save the current settings as a user preset");
    save_.onClick = [this] { promptSave(); };
}

void PresetBar::resized() {
    auto r = getLocalBounds();
    save_.setBounds(r.removeFromRight(58));
    r.removeFromRight(8);
    next_->setBounds(r.removeFromRight(26));
    prev_->setBounds(r.removeFromLeft(26));
    field_ = r.reduced(4, 0);
}

void PresetBar::paint(juce::Graphics& g) {
    auto r = field_.toFloat();
    g.setColour(Colours::display);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(Colours::outline);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    juce::String category;
    if (pm_.currentIndex() >= 0 && pm_.currentIndex() < static_cast<int>(pm_.entries().size()))
        category = pm_.entries()[static_cast<std::size_t>(pm_.currentIndex())].category;
    auto text = field_.reduced(12, 0);
    text.removeFromRight(16);
    if (category.isNotEmpty()) {
        g.setFont(capsFont(10.0f));
        g.setColour(accent_);
        const auto cat = category.toUpperCase();
        const int w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(capsFont(10.0f), cat)) + 4;
        g.drawText(cat, text.removeFromLeft(w), juce::Justification::centredLeft, false);
        g.setColour(Colours::textDim);
        g.setFont(uiFont(12.0f));
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x80\xba")), text.removeFromLeft(16), juce::Justification::centred, false);
    }
    g.setColour(Colours::text);
    g.setFont(uiFont(13.0f, Weight::Medium));
    g.drawText(pm_.currentName(), text, juce::Justification::centredLeft, true);
    const float ax = field_.getRight() - 14.0f, ay = field_.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath(ax - 3.5f, ay - 1.5f);
    chevron.lineTo(ax, ay + 2.0f);
    chevron.lineTo(ax + 3.5f, ay - 1.5f);
    g.setColour(Colours::textMid);
    g.strokePath(chevron, juce::PathStrokeType(1.4f));
}

void PresetBar::mouseUp(const juce::MouseEvent& e) {
    if (field_.contains(e.getPosition())) showMenu();
}

void PresetBar::showMenu() {
    juce::PopupMenu menu;
    std::vector<std::pair<juce::String, juce::PopupMenu>> cats;
    const auto& entries = pm_.entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& e = entries[static_cast<std::size_t>(i)];
        auto it = std::find_if(cats.begin(), cats.end(), [&](const auto& c) { return c.first == e.category; });
        if (it == cats.end()) { cats.emplace_back(e.category, juce::PopupMenu{}); it = cats.end() - 1; }
        it->second.addItem(i + 1, e.name, true, i == pm_.currentIndex());
    }
    for (auto& [name, sub] : cats) {
        bool containsCurrent = pm_.currentIndex() >= 0 && pm_.currentIndex() < static_cast<int>(entries.size())
                            && entries[static_cast<std::size_t>(pm_.currentIndex())].category == name;
        menu.addSubMenu(name, sub, true, nullptr, containsCurrent);
    }
    menu.addSeparator();
    menu.addItem(100000, "Save preset...");
    menu.addItem(100001, "Show user presets folder");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(field_.getWidth()),
                       [this](int result) {
                           if (result == 100000) promptSave();
                           else if (result == 100001) { pm_.userFolder().createDirectory(); pm_.userFolder().revealToUser(); }
                           else if (result > 0) pm_.load(result - 1);
                           repaint();
                       });
}

void PresetBar::promptSave() {
    auto* w = new juce::AlertWindow("Save Preset", "Name for the new user preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("name", pm_.currentName());
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result) {
        if (result == 1) pm_.save(w->getTextEditorContents("name"));
        repaint();
    }), true);
}

} // namespace rrui
