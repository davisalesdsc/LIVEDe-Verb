#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AuroraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AuroraLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class AuroraDeVerbEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AuroraDeVerbEditor (AuroraDeVerbProcessor&);
    ~AuroraDeVerbEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const char* paramId, const juce::String& title);

    AuroraDeVerbProcessor& deverb;
    AuroraLookAndFeel lnf;

    Knob reduction, decay, early, floorKnob, release, lowProtect, mix, output;
    juce::ToggleButton listen { "Listen Removed" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttachment;

    float meterDb = 0.0f;
    juce::Rectangle<int> meterArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraDeVerbEditor)
};
