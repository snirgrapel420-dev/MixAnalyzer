#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AnalysisService.h"

class ProductionAnalyzerProcessor : public juce::AudioProcessor
{
public:
    ProductionAnalyzerProcessor();
    ~ProductionAnalyzerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    AnalysisService& getService() noexcept { return service; }

    // capture only while the host transport is running (standalone: always)
    std::atomic<bool> onlyWhilePlaying { true };
    std::atomic<bool> hostIsPlaying { false };

private:
    AnalysisService service;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProductionAnalyzerProcessor)
};
