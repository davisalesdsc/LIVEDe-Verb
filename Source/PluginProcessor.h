#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/AuroraDeVerbEngine.h"

namespace ParamIDs
{
    inline constexpr auto reduction  = "reduction";
    inline constexpr auto decay      = "decay";
    inline constexpr auto early      = "early";
    inline constexpr auto floorDb    = "floor";
    inline constexpr auto release    = "release";
    inline constexpr auto lowProtect = "lowprotect";
    inline constexpr auto mix        = "mix";
    inline constexpr auto output     = "output";
    inline constexpr auto listen     = "listen";
}

class AuroraDeVerbProcessor final : public juce::AudioProcessor
{
public:
    AuroraDeVerbProcessor();
    ~AuroraDeVerbProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
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

    float getReductionDb() const noexcept { return engine.getReductionDb(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    aurora::DeVerbEngine engine;

    std::atomic<float>* pReduction = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pEarly = nullptr;
    std::atomic<float>* pFloor = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pLowProtect = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pListen = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraDeVerbProcessor)
};
