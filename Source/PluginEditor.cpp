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

// ---------------------------------------------------------------- spectrum view
float SpectrumView::xForHz (float hz, float width) const
{
    using E = aurora::DeVerbEngine;
    return width * std::log (hz / E::kDisplayMinHz) / std::log (E::kDisplayMaxHz / E::kDisplayMinHz);
}

float SpectrumView::yForDb (float db, float height) const
{
    constexpr float top = 6.0f, bottom = -78.0f;
    return height * (top - juce::jlimit (bottom, top, db)) / (top - bottom);
}

void SpectrumView::update (const AuroraDeVerbProcessor& p)
{
    for (int b = 0; b < numBands; ++b)
    {
        const float in = p.getDisplayInputDb (b), out = p.getDisplayOutputDb (b);
        if (! initialised)
        {
            inDb[(size_t) b] = in;
            outDb[(size_t) b] = out;
            continue;
        }
        // Fast rise, smooth fall (classic analyser ballistics).
        auto ballistics = [] (float& v, float target) { v = target > v ? v + 0.6f * (target - v) : v + 0.18f * (target - v); };
        ballistics (inDb[(size_t) b], in);
        ballistics (outDb[(size_t) b], out);
    }
    initialised = true;
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto plot = bounds.reduced (10.0f, 8.0f);
    plot.removeFromBottom (14.0f);
    const float w = plot.getWidth(), h = plot.getHeight();

    // Grid
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = plot.getX() + xForHz (hz, w);
        g.setColour (Colours::track.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (Colours::textDim);
        const juce::String label = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 1.0f, 40.0f, 13.0f), juce::Justification::centred);
    }
    for (float db : { 0.0f, -24.0f, -48.0f, -72.0f })
    {
        const float y = plot.getY() + yForDb (db, h);
        g.setColour (Colours::track.withAlpha (0.45f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
    }

    // Curves
    juce::Path inPath, outPath, removed, inFill;
    for (int b = 0; b < numBands; ++b)
    {
        const float x = plot.getX() + xForHz (aurora::DeVerbEngine::displayBandCentreHz (b), w);
        const float yi = plot.getY() + yForDb (inDb[(size_t) b], h);
        const float yo = plot.getY() + yForDb (outDb[(size_t) b], h);
        if (b == 0) { inPath.startNewSubPath (x, yi); outPath.startNewSubPath (x, yo); }
        else        { inPath.lineTo (x, yi);          outPath.lineTo (x, yo); }
    }

    inFill = inPath;
    inFill.lineTo (plot.getRight(), plot.getBottom());
    inFill.lineTo (plot.getX(), plot.getBottom());
    inFill.closeSubPath();
    g.setColour (Colours::text.withAlpha (0.06f));
    g.fillPath (inFill);

    // Removed region = area between the input and the processed curves.
    removed = outPath;
    for (int b = numBands - 1; b >= 0; --b)
        removed.lineTo (plot.getX() + xForHz (aurora::DeVerbEngine::displayBandCentreHz (b), w),
                        plot.getY() + yForDb (inDb[(size_t) b], h));
    removed.closeSubPath();
    g.setColour (Colours::accentWarm.withAlpha (0.55f));
    g.fillPath (removed);

    g.setColour (Colours::textDim.withAlpha (0.8f));
    g.strokePath (inPath, juce::PathStrokeType (1.0f));
    g.setColour (Colours::accent);
    g.strokePath (outPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Legend
    auto legend = bounds.reduced (14.0f, 10.0f).removeFromTop (14.0f).removeFromRight (230.0f);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.setColour (Colours::accent);
    g.fillRect (legend.removeFromLeft (10.0f).reduced (0.0f, 3.0f));
    legend.removeFromLeft (5.0f);
    g.drawText ("OUTPUT", legend.removeFromLeft (60.0f), juce::Justification::centredLeft);
    g.setColour (Colours::accentWarm);
    g.fillRect (legend.removeFromLeft (10.0f).reduced (0.0f, 3.0f));
    legend.removeFromLeft (5.0f);
    g.drawText ("REVERB REMOVED", legend, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- editor
AuroraDeVerbEditor::AuroraDeVerbEditor (AuroraDeVerbProcessor& p)
    : AudioProcessorEditor (&p), deverb (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (spectrum);

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

    setSize (760, 560);
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
    spectrum.update (deverb);
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
    spectrum.setBounds (area.removeFromTop (170));
    area.removeFromTop (8);

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
