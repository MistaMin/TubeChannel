// Renders the editor to PNG files (analogue and digital skin). Usage: ui_snapshot <output dir>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"

static void save(const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream out(f);
    juce::PNGImageFormat png;
    png.writeImageToStream(img, out);
}

static const char* kNames[] = { "analogue.png", "digital.png", "system-analogue.png", "system-digital.png" };

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const juce::File dir(argc > 1 ? argv[1] : ".");
    dir.createDirectory();

    TubeChannelProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    // drive it so the meters and the power panel have something to show
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    long ph = 0;
    for (int b = 0; b < 120; ++b)
    {
        for (int i = 0; i < 256; ++i, ++ph)
        {
            const float v = float(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * double(ph) / 48000.0));
            buf.setSample(0, i, v); buf.setSample(1, i, v);
        }
        p.processBlock(buf, midi);
    }
    for (int skin = 0; skin < 4; ++skin)
    {
        p.setSkin(skin == 1 || skin == 3 ? 1 : 0);
        p.apvts.state.setProperty("sysPage", skin >= 2 ? 1 : 0, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(TubeChannelEditor::kW, TubeChannelEditor::kH);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        save(img, dir.getChildFile(kNames[skin]));
        if (skin == 0) save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f), dir.getChildFile("analogue@2x.png"));
        if (skin == 1)
        {
            save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f), dir.getChildFile("digital@2x.png"));
            ed->setSize(TubeChannelEditor::kW / 2, TubeChannelEditor::kH / 2);
            save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("digital-small.png"));
        }
        std::printf("wrote %s\n", kNames[skin]);
    }
    // the RATIO knob popped (brickwall limiter), in both skins
    p.apvts.getParameter("ratiolimit")->setValueNotifyingHost(1.0f);
    for (int skin = 0; skin < 2; ++skin)
    {
        p.setSkin(skin);
        p.apvts.state.setProperty("sysPage", 0, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(TubeChannelEditor::kW, TubeChannelEditor::kH);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile(skin == 0 ? "limiter-analogue.png" : "limiter-digital.png"));
    }
    // A positive dBFS readout and final-output clip lamp must also fit the compact meter.
    p.setSkin(1);
    p.apvts.state.setProperty("sysPage", 0, nullptr);
    auto* outputTrim = p.apvts.getParameter(tc::params::id::outputTrim);
    outputTrim->setValueNotifyingHost(outputTrim->convertTo0to1(18.f));
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 256; ++i, ++ph)
        {
            const float v = float(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * double(ph) / 48000.0));
            buf.setSample(0, i, v); buf.setSample(1, i, v);
        }
        p.processBlock(buf, midi);
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil(400);
        save(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f), dir.getChildFile("digital-overload.png"));
    }
    return 0;
}
