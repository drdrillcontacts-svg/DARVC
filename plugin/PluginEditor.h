#pragma once

// Include JUCE module headers directly (see PluginProcessor.h).
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

#include <memory>

class DARVCEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DARVCEditor(DARVCProcessor&);
    ~DARVCEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DARVCProcessor& processor;
    juce::TextButton fromDawButton { "From DAW" }, fromFileButton { "From File" };
    juce::TextButton chooseFileButton { "Choose WAV" }, browseModelButton { "Browse" };
    juce::TextButton convertButton { "✧   Convert" }, outputButton { "Reveal Output" };
    juce::TextButton modelsButton { "Models" }, libraryButton { "Library" }, settingsButton { "Settings" };
    juce::ComboBox f0Box;
    juce::Slider pitchSlider, indexSlider, protectSlider;
    juce::Label inputLabel, modelLabel, statusLabel, outputLabel, pitchLabel, indexLabel, protectLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File inputFile;
    juce::String inputMode { "From DAW" };
    juce::String statusText { "Ready  •  RVC engine not connected" };
    juce::String selectedModel { "No model selected" };

    void chooseInputFile();
    void chooseModelFile();
    void requestConversion();
    // Keeps statusText and the visible label in sync. Previously several
    // handlers updated statusText and only called repaint(), but the text is
    // drawn by statusLabel (a child component), so the UI never changed.
    void setStatus(const juce::String&);
    void styleButton(juce::TextButton&, bool primary = false);
    void setupSlider(juce::Slider&, double, double, double, const juce::String&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DARVCEditor)
};
