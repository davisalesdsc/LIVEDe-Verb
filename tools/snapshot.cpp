// Renders the plugin editor to a PNG while feeding it a reverberant instrument
// signal, so the analyser shows real data. Used by CI as a GUI smoke test.
//   AuroraDeVerbSnapshot <output.png>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <random>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: %s output.png\n", argv[0]);
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sampleRate = 48000.0;
    constexpr int block = 512;

    auto processor = std::make_unique<AuroraDeVerbProcessor>();
    processor->setPlayConfigDetails (2, 2, sampleRate, block);
    processor->prepareToPlay (sampleRate, block);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());

    // Plucked notes (harmonics + attack noise) in a room with ~1.2 s of reverb.
    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 1.0f);
    const float notes[] = { 110.0f, 164.8f, 220.0f, 146.8f };
    const int noteLen = (int) (0.55 * sampleRate);
    float tailL = 0.0f, tailR = 0.0f, tailEnv = 0.0f;

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const int total = (int) (2.3 * sampleRate); // stop in the tail after the last note
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const int n = pos + i;
            const int note = n / noteLen;
            const float t = (float) (n % noteLen) / (float) sampleRate;
            float dry = 0.0f;
            if (note < 4)
            {
                const float f = notes[note];
                for (int h = 1; h <= 10; ++h)
                    dry += std::sin (juce::MathConstants<float>::twoPi * f * (float) h * t) / (float) h;
                dry = 0.25f * dry * std::exp (-t / 0.18f) + 0.1f * noise (rng) * std::exp (-t / 0.01f);
            }
            // Diffuse room tail driven by the note energy.
            tailEnv = std::max (tailEnv * 0.99988f, std::abs (dry));
            tailL = 0.995f * tailL + 0.005f * noise (rng) * tailEnv * 6.0f;
            tailR = 0.995f * tailR + 0.005f * noise (rng) * tailEnv * 6.0f;
            buffer.setSample (0, i, dry + tailL);
            buffer.setSample (1, i, dry + tailR);
        }
        processor->processBlock (buffer, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
    std::printf ("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());

    editor.reset();
    processor.reset();
    return ok ? 0 : 1;
}
