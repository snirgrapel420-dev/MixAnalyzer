#include "PluginProcessor.h"
#include "PluginEditor.h"

ProductionAnalyzerProcessor::ProductionAnalyzerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void ProductionAnalyzerProcessor::prepareToPlay (double sampleRate, int)
{
    service.setHostSampleRate (sampleRate);
}

bool ProductionAnalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void ProductionAnalyzerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Pass-through: the analyzer never changes the audio.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    bool playing = true;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    hostIsPlaying.store (playing);

    if (! service.isCapturing()) return;
    if (onlyWhilePlaying.load() && ! playing) return;

    const int numCh = buffer.getNumChannels();
    if (numCh <= 0) return;
    const float* l = buffer.getReadPointer (0);
    const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
    service.pushAudio (l, r, buffer.getNumSamples());
}

juce::AudioProcessorEditor* ProductionAnalyzerProcessor::createEditor()
{
    return new ProductionAnalyzerEditor (*this);
}

void ProductionAnalyzerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("ProductionAnalyzer");
    state.setProperty ("profile", service.getProfile(), nullptr);
    state.setProperty ("onlyWhilePlaying", onlyWhilePlaying.load(), nullptr);
    juce::MemoryOutputStream mos (destData, false);
    state.writeToStream (mos);
}

void ProductionAnalyzerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! state.isValid()) return;
    service.setProfile ((int) state.getProperty ("profile", 0));
    onlyWhilePlaying.store ((bool) state.getProperty ("onlyWhilePlaying", true));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ProductionAnalyzerProcessor();
}
