#pragma once

// JUCE CMake builds do not generate a <JuceHeader.h> unless
// juce_generate_juce_header() is called. Include the JUCE module headers
// directly instead (this is the approach used by JUCE's own CMake examples).
#include <juce_audio_processors/juce_audio_processors.h>

#include "ConversionEngine.h"

class DARVCProcessor final : public juce::AudioProcessor
{
public:
    DARVCProcessor();
    ~DARVCProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DARVC"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    //======================================================================
    /** User choices shared by the editor and the conversion engine.
        Machine-wide entries (folders, Python, device) persist in a
        properties file; per-session entries (files, sliders) also go into
        the DAW session state. Message-thread access only. */
    struct Settings
    {
        juce::File inputFile;
        juce::File modelFile;
        juce::File indexFile;   // empty -> worker auto-detects a sibling .index
        juce::File modelsDir;
        juce::File libraryDir;
        juce::File pythonExe;   // empty -> auto-detect
        juce::String device { "auto" };
        juce::String modelVersion { "v2" };
        int pitch = 0;
        int indexPercent = 75;
        int protectPercent = 33;
        int f0Id = 1;           // ComboBox id: 1=RMVPE, 2=Harvest, 3=PM, 4=Crepe
    };

    Settings settings;
    ConversionEngine engine;

    /** Persists machine-wide settings and syncs the engine's Python path. */
    void saveSettings();

    /** Returns an empty string if a conversion can start, else the reason. */
    [[nodiscard]] juce::String validateForConversion() const;
    /** Builds the worker job, including a unique output path in the library. */
    [[nodiscard]] DarvcJob buildJob() const;

    static juce::String f0MethodForId(int comboId);
    /** Scans the models folder (recursively) for .pth files. */
    [[nodiscard]] juce::Array<juce::File> scanModels() const;
    /** Audio files in the library folder, newest first. */
    [[nodiscard]] juce::Array<juce::File> scanLibrary() const;
    /** The .index file belonging to a model, if one sits next to it. */
    static juce::File findSiblingIndex(const juce::File& modelFile);

private:
    void loadSettings();

    std::unique_ptr<juce::PropertiesFile> properties;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DARVCProcessor)
};
