#include "PluginProcessor.h"
#include "PluginEditor.h"

DARVCProcessor::DARVCProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{}

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
    // Safe pass-through placeholder: RVC inference is intentionally not run on the audio thread.
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();
    for (int channel = inputs; channel < outputs; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DARVCProcessor::createEditor()
{
    return new DARVCEditor(*this);
}

void DARVCProcessor::getStateInformation(juce::MemoryBlock&) {}
void DARVCProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DARVCProcessor();
}
