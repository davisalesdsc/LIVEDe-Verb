#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> skewedRange (float min, float max, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (min, max, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String formatValue (float v, int decimals, const juce::String& unit)
    {
        return juce::String (v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
    }
}

AuroraDeVerbProcessor::AuroraDeVerbProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AuroraDeVerb", createLayout())
{
    pReduction  = apvts.getRawParameterValue (ParamIDs::reduction);
    pDecay      = apvts.getRawParameterValue (ParamIDs::decay);
    pEarly      = apvts.getRawParameterValue (ParamIDs::early);
    pFloor      = apvts.getRawParameterValue (ParamIDs::floorDb);
    pRelease    = apvts.getRawParameterValue (ParamIDs::release);
    pLowProtect = apvts.getRawParameterValue (ParamIDs::lowProtect);
    pMix        = apvts.getRawParameterValue (ParamIDs::mix);
    pOutput     = apvts.getRawParameterValue (ParamIDs::output);
    pListen     = apvts.getRawParameterValue (ParamIDs::listen);
}

juce::AudioProcessorValueTreeState::ParameterLayout AuroraDeVerbProcessor::createLayout()
{
    using namespace juce;
    using Float = AudioParameterFloat;
    using Attr  = AudioParameterFloatAttributes;

    auto attr = [] (int decimals, const String& unit)
    {
        return Attr().withLabel (unit).withStringFromValueFunction ([decimals, unit] (float v, int) { return formatValue (v, decimals, unit); });
    };

    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::reduction, 1 },  "Reduction",   NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f, attr (0, "%")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::decay, 1 },      "Room Decay",  skewedRange (0.1f, 5.0f, 1.0f, 0.01f), 0.8f, attr (2, "s")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::early, 1 },      "Early Keep",  skewedRange (10.0f, 150.0f, 40.0f, 1.0f), 30.0f, attr (0, "ms")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::floorDb, 1 },      "Floor",       NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -24.0f, attr (1, "dB")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::release, 1 },    "Release",     NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, attr (0, "%")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::lowProtect, 1 }, "Low Protect", skewedRange (20.0f, 500.0f, 100.0f, 1.0f), 100.0f, attr (0, "Hz")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::mix, 1 },        "Mix",         NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, attr (0, "%")));
    layout.add (std::make_unique<Float> (ParameterID { ParamIDs::output, 1 },     "Output",      NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, attr (1, "dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::listen, 1 }, "Listen Removed", false));
    return layout;
}

bool AuroraDeVerbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void AuroraDeVerbProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, getTotalNumOutputChannels() > 1 ? 2 : 1);
    setLatencySamples (engine.getLatencySamples());
}

void AuroraDeVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    aurora::DeVerbParameters p;
    p.reduction     = pReduction->load() * 0.01f;
    p.decaySec      = pDecay->load();
    p.earlyMs       = pEarly->load();
    p.floorDb       = pFloor->load();
    p.release       = pRelease->load() * 0.01f;
    p.lowProtectHz  = pLowProtect->load();
    p.mix           = pMix->load() * 0.01f;
    p.outputDb      = pOutput->load();
    p.listenRemoved = pListen->load() > 0.5f;
    engine.setParameters (p);

    if (buffer.getNumChannels() == 0)
        return;

    float* left  = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (left, right, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AuroraDeVerbProcessor::createEditor()
{
    return new AuroraDeVerbEditor (*this);
}

void AuroraDeVerbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AuroraDeVerbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuroraDeVerbProcessor();
}
