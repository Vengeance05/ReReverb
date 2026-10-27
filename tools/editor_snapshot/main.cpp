// In-process editor screenshot for UI checks: <exe> <out.png> [wait-seconds]
// Built per plugin (links that plugin's shared code); run under xvfb-run when headless.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf("usage: <out.png> [wait]\n"); return 2; }
    juce::PluginHostType::jucePlugInClientCurrentWrapperType = juce::AudioProcessor::wrapperType_Standalone;
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    p->setPlayConfigDetails(2, 2, 48000.0, 512);
    p->prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditorIfNeeded());
    ed->setVisible(true);
    const double wait = argc > 2 ? juce::String(argv[2]).getDoubleValue() : 2.0;
    juce::Timer::callAfterDelay(static_cast<int>(wait * 1000), [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();
    auto img = ed->createComponentSnapshot(ed->getLocalBounds());
    juce::File out{juce::String(argv[1])};
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    std::printf("wrote %s (%dx%d)\n", argv[1], img.getWidth(), img.getHeight());
    ed.reset();
    p->releaseResources();
    return 0;
}
