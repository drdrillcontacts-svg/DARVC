#pragma once

// Include JUCE module headers directly (see PluginProcessor.h).
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "DarvcLookAndFeel.h"
#include "PluginProcessor.h"

#include <functional>
#include <memory>

class DARVCEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit DARVCEditor(DARVCProcessor&);
    ~DARVCEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DARVCProcessor& processor;
    DarvcLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 500 };

    juce::TextButton fromDawButton { "From DAW" }, fromFileButton { "From File" };
    juce::TextButton chooseFileButton { "Choose WAV" }, browseModelButton { "Browse" };
    juce::TextButton convertButton { "Convert" }, outputButton { "Reveal Output" };
    juce::TextButton modelsButton { "Models" }, libraryButton { "Library" }, settingsButton { "Settings" };
    juce::ComboBox f0Box, versionBox;
    juce::Slider pitchSlider, indexSlider, protectSlider;
    juce::Label inputLabel, modelLabel, indexLabel, engineLabel, statusLabel, outputLabel,
        pitchLabel, indexRateLabel, protectLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Overlay panels (Models / Library / Settings), one at a time.
    std::unique_ptr<juce::Component> activePanel;
    std::function<void()> activePanelRefresh;

    // Waveform preview of the loaded input / last conversion output.
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 8 };
    juce::AudioThumbnail thumbnail;
    juce::File outputFile;

    bool wasEngineBusy = false;
    juce::String statusText;
    juce::String lastEngineMessage;

    //======================================================================
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    void refreshFromSettings();
    void refreshFromEngine();
    void updateModelLabels();
    void updatePreviewSource();
    void setStatus(const juce::String&);
    void setInputFile(juce::File);
    void setModelFile(juce::File);

    void chooseInputFile();
    void chooseModelFile();
    void requestConversion();

    void showModelsPanel();
    void showLibraryPanel();
    void showSettingsPanel();
    void showPanel(std::unique_ptr<juce::Component>, std::function<void()> refresh = {});
    void closePanel();

    void styleButton(juce::TextButton&, bool primary = false);
    void setupSlider(juce::Slider&, double, double, double, const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DARVCEditor)
};
