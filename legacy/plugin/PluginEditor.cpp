#include "PluginEditor.h"

GrandHallEditor::GrandHallEditor(GrandHallAudioProcessor& audioProcessor)
    : AudioProcessorEditor(audioProcessor), processor_(audioProcessor) {
    title_.setText("GRAND HALL  /  PROFESSIONAL", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions{}.withHeight(20.0f).withStyle("Bold")));
    title_.setColour(juce::Label::textColourId, juce::Colour(0xffe9e2d4));
    addAndMakeVisible(title_);
    modeStatus_.setText("CONVOLUTION", juce::dontSendNotification);
    modeStatus_.setJustificationType(juce::Justification::centredRight);
    modeStatus_.setColour(juce::Label::textColourId, juce::Colour(0xffc7a879));
    addAndMakeVisible(modeStatus_);
    const auto setupHeading = [this](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f).withStyle("Bold")));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffc7a879));
        addAndMakeVisible(label);
    };
    setupHeading(mainHeading_, "REVERB");
    setupHeading(toneHeading_, "TONE  /  FIVE-BAND EQ");
    setupHeading(irHeading_, "RESPONSE  /  PRESET");
    addAndMakeVisible(loadButton_);
    addAndMakeVisible(savePresetButton_);
    addAndMakeVisible(loadPresetButton_);
    addAndMakeVisible(modeBox_);
    addAndMakeVisible(presetBox_);
    addAndMakeVisible(categoryBox_);
    addAndMakeVisible(searchBox_);
    addAndMakeVisible(favouriteButton_);
    addAndMakeVisible(eqToggleButton_);
    modeBox_.addItem("Convolution", 1);
    modeBox_.addItem("Algorithmic", 2);
    modeAttachment_ = std::make_unique<ComboAttachment>(
        processor_.getParameters(), "mode", modeBox_);
    categoryBox_.addItem("All categories", 1);
    for (const auto& category : {"Rooms", "Chamber", "Concert Halls", "Large Halls",
                                 "Cathedral", "Piano", "Strings", "Full Orchestra"})
        categoryBox_.addItem(category, categoryBox_.getNumItems() + 1);
    categoryBox_.setSelectedId(1);
    searchBox_.setTextToShowWhenEmpty("Search presets", juce::Colour(0xffaaa59b));
    searchBox_.onTextChange = [this] { rebuildPresetList(); };
    categoryBox_.onChange = [this] { rebuildPresetList(); };
    rebuildPresetList();
    presetBox_.onChange = [this] {
        if (presetBox_.getSelectedId() > 0) {
            presetIndex_ = presetBox_.getSelectedId() - 1;
            processor_.setCurrentProgram(presetIndex_);
            status_.setText(processor_.getProgramDescription(presetIndex_),
                            juce::dontSendNotification);
        }
    };
    loadButton_.onClick = [this] { showFileChooser(false); };
    savePresetButton_.onClick = [this] { showFileChooser(true); };
    favouriteButton_.onClick = [this] {
        favouriteButton_.setButtonText(favouriteButton_.getButtonText() == "FAVOURITE"
                                           ? "FAVOURITED" : "FAVOURITE");
    };
    eqToggleButton_.setClickingTogglesState(true);
    eqToggleButton_.setTooltip("Show or hide the five-band post-output EQ.");
    eqToggleButton_.onClick = [this] { setEqVisible(eqToggleButton_.getToggleState()); };
    loadPresetButton_.onClick = [this] {
        ownedChooser_ = std::make_unique<juce::FileChooser>(
            "Open Grand Hall preset", juce::File{}, "*.ghpreset");
        ownedChooser_->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                                   [this](const juce::FileChooser& chooser) {
                                       processor_.loadUserPreset(chooser.getResult());
                                   });
    };
    const std::array<std::pair<const char*, const char*>, 28> controls{{
        {"decay", "DECAY"}, {"predelay", "PRE-DELAY"}, {"wet", "WET"},
        {"dry", "DRY"}, {"width", "WIDTH"}, {"output", "OUTPUT"},
        {"early", "EARLY LEVEL"}, {"late", "LATE LEVEL"},
        {"balance", "EARLY / LATE"}, {"lowdecay", "LOW DECAY"},
        {"highdecay", "HIGH DECAY"}, {"highdamp", "HF DAMPING"},
        {"roomsize", "ROOM SIZE"},
        {"eq1freq", "EQ 1 FREQ"}, {"eq1gain", "EQ 1 GAIN"}, {"eq1q", "EQ 1 Q"},
        {"eq2freq", "EQ 2 FREQ"}, {"eq2gain", "EQ 2 GAIN"}, {"eq2q", "EQ 2 Q"},
        {"eq3freq", "EQ 3 FREQ"}, {"eq3gain", "EQ 3 GAIN"}, {"eq3q", "EQ 3 Q"},
        {"eq4freq", "EQ 4 FREQ"}, {"eq4gain", "EQ 4 GAIN"}, {"eq4q", "EQ 4 Q"},
        {"eq5freq", "EQ 5 FREQ"}, {"eq5gain", "EQ 5 GAIN"}, {"eq5q", "EQ 5 Q"} }};
    for (const auto& [id, name] : controls) addSlider(id, name);
    setEqVisible(false);
    status_.setText("Ready  |  default hall", juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, juce::Colour(0xffaaa59b));
    addAndMakeVisible(status_);
    irStatus_.setColour(juce::Label::textColourId, juce::Colour(0xffaaa59b));
    irStatus_.setTooltip("Loaded impulse response or the generated fallback response.");
    addAndMakeVisible(irStatus_);
    for (auto* button : {&loadButton_, &savePresetButton_, &loadPresetButton_, &favouriteButton_}) {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303438));
        button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe9e2d4));
    }
    setResizable(true, true);
    setResizeLimits(760, 760, 1500, 1100);
    setSize(920, 760);
    startTimerHz(8);
}

void GrandHallEditor::addSlider(const juce::String& id, const juce::String& name) {
    auto slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                  juce::Slider::TextBoxBelow);
    slider->setName(name);
    auto tooltip = name;
    if (id == "decay") tooltip = "Decay multiplier. Shapes the prepared response tail.";
    else if (id == "roomsize") tooltip = "Algorithmic mode room-size scaling.";
    else if (id == "predelay") tooltip = "Wet-signal pre-delay in milliseconds.";
    else if (id == "early") tooltip = "Level of the separated early response component.";
    else if (id == "late") tooltip = "Level of the separated late response component.";
    else if (id == "balance") tooltip = "Relative early/late contribution in both modes.";
    else if (id == "lowdecay") tooltip = "Low-frequency decay multiplier.";
    else if (id == "highdecay") tooltip = "High-frequency decay multiplier.";
    else if (id == "highdamp") tooltip = "High-frequency damping applied to the response.";
    else if (id == "width") tooltip = "Stereo mid/side width.";
    else if (id == "wet") tooltip = "Wet signal level.";
    else if (id == "dry") tooltip = "Dry signal level.";
    else if (id == "output") tooltip = "Final output gain in decibels.";
    else if (id.startsWith("eq")) tooltip = "Stereo-linked post-output EQ control.";
    slider->setTooltip(tooltip);
    slider->setDoubleClickReturnValue(true, processor_.getParameters().getParameter(id)->getDefaultValue());
    addAndMakeVisible(*slider);
    auto label = std::make_unique<juce::Label>();
    label->setText(name, juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);
    label->setColour(juce::Label::textColourId, juce::Colour(0xffaaa59b));
    addAndMakeVisible(*label);
    sliderAttachments_.push_back(std::make_unique<SliderAttachment>(
        processor_.getParameters(), id, *slider));
    sliderIds_.push_back(id);
    sliders_.push_back(std::move(slider));
    labels_.push_back(std::move(label));
}

void GrandHallEditor::setEqVisible(const bool visible) {
    eqVisible_ = visible;
    eqToggleButton_.setButtonText(visible ? "HIDE EQ" : "SHOW EQ");
    for (std::size_t index = 13; index < sliders_.size(); ++index) {
        sliders_[index]->setVisible(visible);
        labels_[index]->setVisible(visible);
    }
    resized();
}

void GrandHallEditor::rebuildPresetList() {
    const auto selectedName = presetBox_.getText();
    presetBox_.clear(juce::dontSendNotification);
    const auto category = categoryBox_.getText();
    const auto search = searchBox_.getText().trim().toLowerCase();
    int item = 1;
    for (int index = 0; index < processor_.getNumPrograms(); ++index) {
        const auto name = processor_.getProgramName(index);
        if ((category == "All categories" || processor_.getProgramCategory(index) == category)
            && (search.isEmpty() || name.toLowerCase().contains(search)))
            presetBox_.addItem(name, item++);
    }
    if (selectedName.isNotEmpty()) presetBox_.setText(selectedName, juce::dontSendNotification);
    else if (presetBox_.getNumItems() > 0) presetBox_.setSelectedItemIndex(0);
}

void GrandHallEditor::showFileChooser(const bool preset) {
    ownedChooser_ = std::make_unique<juce::FileChooser>(
        preset ? "Save Grand Hall preset" : "Select WAV impulse response",
        juce::File{}, preset ? "*.ghpreset" : "*.wav");
    const auto mode = preset ? juce::FileBrowserComponent::saveMode
                             : juce::FileBrowserComponent::openMode;
    ownedChooser_->launchAsync(mode
                               | juce::FileBrowserComponent::canSelectFiles,
                               [this, preset](const juce::FileChooser& chooser) {
                                   const auto file = chooser.getResult();
                                   if (file == juce::File{}) return;
                                   if (preset) processor_.saveUserPreset(file.withFileExtension("ghpreset"));
                                   else processor_.loadUserImpulseResponse(file);
                               });
}

void GrandHallEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour(0xff17191b));
    graphics.setColour(panelColour());
    graphics.fillRoundedRectangle(18.0f, 68.0f, static_cast<float>(getWidth() - 36),
                                  static_cast<float>(getHeight() - 86), 6.0f);
    graphics.setColour(juce::Colour(0xff34383b));
    graphics.drawHorizontalLine(58, 24.0f, static_cast<float>(getWidth() - 24));
    graphics.drawHorizontalLine(292, 28.0f, static_cast<float>(getWidth() - 28));
    if (eqVisible_)
        graphics.drawHorizontalLine(444, 28.0f, static_cast<float>(getWidth() - 28));
}

void GrandHallEditor::resized() {
    const auto margin = 34;
    const auto contentWidth = getWidth() - 2 * margin;
    title_.setBounds(margin, 12, 310, 28);
    modeStatus_.setBounds(getWidth() - 330, 12, 120, 20);
    modeBox_.setBounds(getWidth() - 195, 10, 165, 28);
    status_.setBounds(margin, 42, contentWidth, 17);
    mainHeading_.setBounds(margin, 72, contentWidth, 18);
    toneHeading_.setBounds(margin, 303, contentWidth, 18);
    eqToggleButton_.setBounds(getWidth() - 145, 299, 111, 26);

    const auto placeRow = [this, contentWidth](std::size_t first,
                                                        std::size_t count, int y,
                                                        int height, int columns) {
        const auto cell = contentWidth / columns;
        for (std::size_t offset = 0; offset < count; ++offset) {
            const auto index = first + offset;
            const auto x = 34 + static_cast<int>(
                offset % static_cast<std::size_t>(columns)) * cell;
            const auto row = static_cast<int>(
                offset / static_cast<std::size_t>(columns));
            const auto top = y + row * height;
            sliders_[index]->setBounds(x, top + 18, cell - 10, height - 20);
            labels_[index]->setBounds(x, top, cell - 10, 18);
        }
    };
    placeRow(0, 6, 92, 92, 6);
    placeRow(6, 3, 190, 92, 3);
    placeRow(9, 4, 323, 88, 4);
    if (eqVisible_) placeRow(13, 15, 464, 60, 5);

    const auto footerY = getHeight() - 106;
    irHeading_.setBounds(margin, footerY, contentWidth, 18);
    categoryBox_.setBounds(margin, footerY + 23, 130, 26);
    searchBox_.setBounds(margin + 140, footerY + 23, 150, 26);
    presetBox_.setBounds(margin + 300, footerY + 23, 190, 26);
    favouriteButton_.setBounds(getWidth() - 145, footerY + 23, 111, 26);
    loadButton_.setBounds(margin, footerY + 58, 100, 26);
    savePresetButton_.setBounds(margin + 110, footerY + 58, 115, 26);
    loadPresetButton_.setBounds(margin + 235, footerY + 58, 115, 26);
    irStatus_.setBounds(margin + 360, footerY + 58, contentWidth - 360, 26);
}

void GrandHallEditor::timerCallback() {
    const auto mode = processor_.getParameters().getRawParameterValue("mode")->load();
    const auto algorithmic = mode > 0.5f;
    sliders_[12]->setVisible(algorithmic);
    labels_[12]->setVisible(algorithmic);
    modeStatus_.setText(algorithmic ? "ALGORITHMIC" : "CONVOLUTION",
                        juce::dontSendNotification);
    status_.setText(mode > 0.5f ? "Algorithmic mode  |  room size active"
                                : "Convolution mode  |  early/late response controls active",
                    juce::dontSendNotification);
    irStatus_.setText(processor_.getIRStatus(), juce::dontSendNotification);
}

juce::Colour GrandHallEditor::panelColour() noexcept {
    return juce::Colour(0xff202326);
}
