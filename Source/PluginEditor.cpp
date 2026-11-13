#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background { 0xff15181d };
    const juce::Colour panel      { 0xff1e232a };
    const juce::Colour track      { 0xff313843 };
    const juce::Colour accent     { 0xff5ad1c4 };
    const juce::Colour accentWarm { 0xfff2a65a };
    const juce::Colour text       { 0xffe6e9ee };
    const juce::Colour textDim    { 0xff8b95a3 };
}

// ---------------------------------------------------------------- look and feel
AuroraLookAndFeel::AuroraLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::textDim);
}

void AuroraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = juce::jmax (3.0f, radius * 0.12f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path trackArc;
    trackArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::track);
    g.strokePath (trackArc, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (Colours::accent);
    g.strokePath (valueArc, { lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    const float knobR = arcR - lineW * 1.4f;
    g.setColour (Colours::panel.brighter (0.15f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    const juce::Point<float> tip (centre.x + (knobR - 4.0f) * std::sin (angle),
                                  centre.y - (knobR - 4.0f) * std::cos (angle));
    g.setColour (Colours::text);
    g.drawLine ({ centre.getPointOnCircumference (knobR * 0.35f, angle), tip }, 2.5f);
}

void AuroraLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? Colours::accentWarm : Colours::track.brighter (highlighted ? 0.15f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? Colours::background : Colours::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

// ---------------------------------------------------------------- editor
AuroraDeVerbEditor::AuroraDeVerbEditor (AuroraDeVerbProcessor& p)
    : AudioProcessorEditor (&p), deverb (p)
{
    setLookAndFeel (&lnf);

    addKnob (reduction,  ParamIDs::reduction,  "REDUCTION");
    addKnob (decay,      ParamIDs::decay,      "ROOM DECAY");
    addKnob (early,      ParamIDs::early,      "EARLY KEEP");
    addKnob (floorKnob,  ParamIDs::floorDb,      "FLOOR");
    addKnob (release,    ParamIDs::release,    "RELEASE");
    addKnob (lowProtect, ParamIDs::lowProtect, "LOW PROTECT");
    addKnob (mix,        ParamIDs::mix,        "MIX");
    addKnob (output,     ParamIDs::output,     "OUTPUT");

    addAndMakeVisible (listen);
    listen.setTooltip ("Hear only the reverb that is being removed");
    listenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (deverb.apvts, ParamIDs::listen, listen);

    setSize (720, 420);
    startTimerHz (30);
}

AuroraDeVerbEditor::~AuroraDeVerbEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AuroraDeVerbEditor::addKnob (Knob& k, const char* paramId, const juce::String& title)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (k.slider);

    k.label.setText (title, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (deverb.apvts, paramId, k.slider);
}

void AuroraDeVerbEditor::timerCallback()
{
    const float target = deverb.getReductionDb();
    // Fast fall, slower recovery so the meter is readable.
    meterDb = target < meterDb ? target : meterDb + 0.15f * (target - meterDb);
    repaint (meterArea);
}

void AuroraDeVerbEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    g.setColour (Colours::text);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("AURORA DEVERB", header, juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("instrument de-reverb", header, juce::Justification::centredRight);

    // Reduction meter
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (meterArea.toFloat(), 4.0f);
    const float maxDb = 30.0f;
    const float amount = juce::jlimit (0.0f, 1.0f, -meterDb / maxDb);
    auto bar = meterArea.toFloat().reduced (3.0f);
    g.setColour (Colours::accentWarm);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * amount), 3.0f);
    g.setColour (Colours::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("REDUCTION  " + juce::String (meterDb, 1) + " dB", meterArea.reduced (10, 0), juce::Justification::centredLeft);
}

void AuroraDeVerbEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 0);
    area.removeFromTop (56);

    auto bottom = area.removeFromBottom (56);
    bottom.removeFromBottom (14);
    listen.setBounds (bottom.removeFromRight (160));
    bottom.removeFromRight (14);
    meterArea = bottom;

    Knob* rows[2][4] = { { &reduction, &decay, &early, &floorKnob },
                         { &release, &lowProtect, &mix, &output } };

    const int rowH = area.getHeight() / 2;
    for (auto& row : rows)
    {
        auto r = area.removeFromTop (rowH);
        const int colW = r.getWidth() / 4;
        for (auto* k : row)
        {
            auto cell = r.removeFromLeft (colW).reduced (6, 4);
            k->label.setBounds (cell.removeFromTop (18));
            k->slider.setBounds (cell);
        }
    }
}
