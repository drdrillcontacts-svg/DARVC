#include "PluginProcessor.h"
#include "PluginEditor.h"

DARVCProcessor::DARVCProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    loadSettings();
    engine.setPythonOverride(settings.pythonExe);
}

DARVCProcessor::~DARVCProcessor() = default;

void DARVCProcessor::prepareToPlay(double, int) {}
void DARVCProcessor::releaseResources() {}

bool DARVCProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Only mono/stereo pass-through is supported. Hosts such as Logic Pro
    // (auval) reject AU plug-ins that do not constrain their bus layouts.
    const auto mainOutput = layouts.getMainOutputChannelSet();
    if (mainOutput != juce::AudioChannelSet::mono()
        && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    if (mainOutput != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void DARVCProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // Conversion happens offline in ConversionEngine (a background child
    // process), never on the audio thread: live audio simply passes through.
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();
    for (int channel = inputs; channel < outputs; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DARVCProcessor::createEditor()
{
    return new DARVCEditor(*this);
}

//==============================================================================
void DARVCProcessor::loadSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "DARVC";
    options.filenameSuffix = ".settings";
    options.folderName = "DARVC";
    properties = std::make_unique<juce::PropertiesFile>(options);

    settings.modelsDir = { properties->getValue("modelsDir",
                                                ConversionEngine::defaultModelsDir().getFullPathName()) };
    settings.libraryDir = { properties->getValue("libraryDir",
                                                 ConversionEngine::defaultLibraryDir().getFullPathName()) };
    settings.pythonExe = { properties->getValue("pythonExe") };
    settings.device = properties->getValue("device", "auto");
    settings.modelVersion = properties->getValue("modelVersion", "v2");
    settings.modelFile = { properties->getValue("modelFile") };

    settings.modelsDir.createDirectory();
    settings.libraryDir.createDirectory();
}

void DARVCProcessor::saveSettings()
{
    engine.setPythonOverride(settings.pythonExe);
    if (properties == nullptr)
        return;
    properties->setValue("modelsDir", settings.modelsDir.getFullPathName());
    properties->setValue("libraryDir", settings.libraryDir.getFullPathName());
    properties->setValue("pythonExe", settings.pythonExe.getFullPathName());
    properties->setValue("device", settings.device);
    properties->setValue("modelVersion", settings.modelVersion);
    properties->setValue("modelFile", settings.modelFile.getFullPathName());
    properties->saveIfNeeded();
}

//==============================================================================
juce::String DARVCProcessor::f0MethodForId(int comboId)
{
    switch (comboId)
    {
        case 2: return "harvest";
        case 3: return "pm";
        case 4: return "crepe";
        default: return "rmvpe";
    }
}

juce::File DARVCProcessor::findSiblingIndex(const juce::File& modelFile)
{
    if (!modelFile.existsAsFile())
        return {};
    const auto exact = modelFile.withFileExtension("index");
    if (exact.existsAsFile())
        return exact;
    juce::File fallback;
    for (const auto& candidate : modelFile.getParentDirectory()
             .findChildFiles(juce::File::findFiles, false, "*.index"))
    {
        if (candidate.getFileNameWithoutExtension().contains(modelFile.getFileNameWithoutExtension()))
            return candidate;
        if (!fallback.existsAsFile())
            fallback = candidate;
    }
    return fallback;
}

juce::Array<juce::File> DARVCProcessor::scanModels() const
{
    juce::Array<juce::File> models;
    if (settings.modelsDir.isDirectory())
        models = settings.modelsDir.findChildFiles(juce::File::findFiles, true, "*.pth");
    std::sort(models.begin(), models.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFileName().compareIgnoreCase(b.getFileName()) < 0; });
    // Keep a model chosen from elsewhere visible in the list too.
    if (settings.modelFile.existsAsFile() && !models.contains(settings.modelFile))
        models.insert(0, settings.modelFile);
    return models;
}

juce::Array<juce::File> DARVCProcessor::scanLibrary() const
{
    juce::Array<juce::File> files;
    if (settings.libraryDir.isDirectory())
        files = settings.libraryDir.findChildFiles(
            juce::File::findFiles, false, "*.wav;*.flac;*.aif;*.aiff;*.mp3");
    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return files;
}

juce::String DARVCProcessor::validateForConversion() const
{
    if (!settings.inputFile.existsAsFile())
        return "Choose an input audio file first (From File).";
    if (!settings.modelFile.existsAsFile())
        return "Choose an RVC .pth model first (Browse or Models).";
    if (engine.isBusy())
        return "A conversion is already running.";
    return {};
}

DarvcJob DARVCProcessor::buildJob() const
{
    DarvcJob job;
    job.inputFile = settings.inputFile;
    job.modelFile = settings.modelFile;
    job.indexFile = settings.indexFile.existsAsFile()
                        ? settings.indexFile
                        : findSiblingIndex(settings.modelFile);
    job.modelsDir = settings.modelsDir;
    job.pitch = settings.pitch;
    job.f0Method = f0MethodForId(settings.f0Id);
    job.indexRate = settings.indexPercent / 100.0;
    job.protect = settings.protectPercent / 100.0;
    job.device = settings.device;
    job.modelVersion = settings.modelVersion;

    const auto stem = settings.inputFile.getFileNameWithoutExtension()
                      + " (DARVC " + settings.modelFile.getFileNameWithoutExtension() + ")";
    job.outputFile = settings.libraryDir.getChildFile(stem + ".wav");
    for (int suffix = 2; job.outputFile.existsAsFile(); ++suffix)
        job.outputFile = settings.libraryDir.getChildFile(stem + " " + juce::String(suffix) + ".wav");
    return job;
}

//==============================================================================
void DARVCProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("DARVC");
    state.setProperty("inputFile", settings.inputFile.getFullPathName(), nullptr);
    state.setProperty("modelFile", settings.modelFile.getFullPathName(), nullptr);
    state.setProperty("indexFile", settings.indexFile.getFullPathName(), nullptr);
    state.setProperty("pitch", settings.pitch, nullptr);
    state.setProperty("indexPercent", settings.indexPercent, nullptr);
    state.setProperty("protectPercent", settings.protectPercent, nullptr);
    state.setProperty("f0Id", settings.f0Id, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void DARVCProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;
    const auto state = juce::ValueTree::fromXml(*xml);
    if (!state.isValid())
        return;

    settings.inputFile = { state.getProperty("inputFile").toString() };
    settings.modelFile = { state.getProperty("modelFile").toString() };
    settings.indexFile = { state.getProperty("indexFile").toString() };
    settings.pitch = juce::jlimit(-24, 24, static_cast<int>(state.getProperty("pitch", 0)));
    settings.indexPercent = juce::jlimit(0, 100, static_cast<int>(state.getProperty("indexPercent", 75)));
    settings.protectPercent = juce::jlimit(0, 50, static_cast<int>(state.getProperty("protectPercent", 33)));
    settings.f0Id = juce::jlimit(1, 4, static_cast<int>(state.getProperty("f0Id", 1)));
    updateHostDisplay();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DARVCProcessor();
}
