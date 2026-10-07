// GUI regressions: skin switching, state restoration, compact output layout and final-output metering.
#include "PluginProcessor.h"
#include "UI/MeterBallistics.h"

#include <cstdio>
#include <cmath>

static int failures = 0;
static void check(bool ok, const char* message)
{
    if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
static juce::Component* find(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id) return &parent;
    for (auto* child : parent.getChildren())
        if (auto* result = find(*child, id)) return result;
    return nullptr;
}
static void click(juce::Component& parent, const char* id)
{
    auto* button = dynamic_cast<juce::Button*>(find(parent, id));
    check(button != nullptr, "toolbar button exists");
    if (button) { button->triggerClick(); pump(30); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        TubeChannelProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        auto* analogue = find(*editor, "analogue-meter");
        auto* digital = find(*editor, "digital-meter");
        check(analogue && digital, "both skins have meters");
        if (!analogue || !digital) return 1;
        check(analogue->isVisible() && !digital->isVisible(), "analogue is initially visible");
        click(*editor, "skin-switch");
        check(p.skin() == 1 && digital->isVisible() && !analogue->isVisible(), "skin button changes the visible skin");
        auto* inputTrim = find(*editor, "input-trim");
        auto* outputTrim = find(*editor, "output-trim");
        check(inputTrim && outputTrim && inputTrim->isVisible() && outputTrim->isVisible(), "digital main page exposes gain trims");
        check(outputTrim && outputTrim->getBottom() <= digital->getY(), "output trim does not overlap meter");
        p.telemetry[0].vuTap[0].store(0.347f);
        p.telemetry[1].vuTap[0].store(0.0347f);
        p.apvts.state.setProperty("vuSrc", 0, nullptr);
        pump(80);
        check(digital->getTitle().contains("0.0 VU"), "internal taps retain their VU reference");
        p.apvts.getParameter(tc::params::id::dualMono)->setValueNotifyingHost(1.f);
        pump(160);
        click(*editor, "channel-b");
        pump(80);
        check(digital->getTitle().contains("-20.0 VU"), "dual-mono meter follows selected channel B");
        p.apvts.state.setProperty("vuSrc", 3, nullptr);
        pump(80);
        check(digital->getTitle().contains("dBFS"), "output source returns to dBFS units");
        p.telemetry[0].vuTap[0].store(0.0347f);
        p.telemetry[1].vuTap[0].store(3.47f);
        p.apvts.state.setProperty("vuSrc", 0, nullptr);
        pump(80);
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
        check(p.setBusesLayout(layout), "host can change layout to mono");
        p.setRateAndBufferSizeDetails(96000.0, 256);
        p.prepareToPlay(96000.0, 256);
        pump(160);
        check(digital->getTitle().contains("-20.0 VU"), "mono meter ignores stale right-channel history");
        click(*editor, "system-page");
        check(!digital->isVisible(), "system page hides digital main meter");
        click(*editor, "skin-switch");
        check(p.skin() == 0 && int(p.apvts.state.getProperty("sysPage")) == 1, "skin switching preserves system page");
        click(*editor, "system-page");
        check(analogue->isVisible(), "returning to main shows the selected skin");
        const auto saved = p.apvts.copyState();
        p.setSkin(1);
        pump(160);
        check(digital->isVisible(), "open editor follows external skin changes");
        p.apvts.replaceState(saved);
        pump(160);
        check(analogue->isVisible(), "open editor follows restored skin state");
    }
    {
        tc::ui::MeterBallistics meter;
        meter.update(0.1f, 1.0 / 30.0);
        check(std::fabs(meter.db + 20.f) < 1e-5f, "peak meter attacks immediately in dBFS");
        meter.update(0.f, 0.5);
        check(std::fabs(meter.db + 32.f) < 1e-5f && std::fabs(meter.heldDb + 20.f) < 1e-5f, "peak holds while bar releases");
        meter.update(0.f, 0.75);
        check(std::fabs(meter.heldDb + 26.f) < 1e-5f, "peak hold releases after one second");
        tc::ui::MeterBallistics a, b;
        a.update(0.5f, 0.0); b.update(0.5f, 0.0);
        for (int i = 0; i < 30; ++i) a.update(0.f, 1.0 / 30.0);
        for (int i = 0; i < 60; ++i) b.update(0.f, 1.0 / 60.0);
        check(std::fabs(a.db - b.db) < 1e-4f && std::fabs(a.heldDb - b.heldDb) < 1e-4f, "meter response is independent of frame rate");
    }
    {
        TubeChannelProcessor p;
        p.setRateAndBufferSizeDetails(96000.0, 256);
        p.prepareToPlay(96000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        float expected[2] = {};
        long phase = 0;
        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < 256; ++i, ++phase)
            {
                buf.setSample(0, i, float(0.1 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * phase / 96000.0)));
                buf.setSample(1, i, 0.f);
            }
            p.processBlock(buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i) expected[ch] = std::max(expected[ch], std::fabs(buf.getSample(ch, i)));
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            check(std::fabs(p.takeOutputMeterPeak(ch) - expected[ch]) < 1e-7f, "meter accumulates exact final output peaks per channel");
            check(p.takeOutputMeterPeak(ch) == 0.f, "reading a peak clears the pending value");
        }
        check(expected[0] > 0.01f && expected[1] < 0.001f, "stereo meter distinguishes signal from silent channel");
        auto* trim = p.apvts.getParameter(tc::params::id::outputTrim);
        trim->setValueNotifyingHost(trim->convertTo0to1(24.f));
        for (int i = 0; i < 256; ++i)
        {
            buf.setSample(0, i, float(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 96000.0)));
            buf.setSample(1, i, 0.f);
        }
        p.processBlock(buf, midi);
        check(p.takeOutputMeterPeak(0) >= 1.f && p.takeOutputClip(0), "digital clip detection includes final output trim");
        check(!p.takeOutputClip(0), "clip events are consumed once");
        p.setSkin(1);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        click(*editor, "system-page");
        for (int i = 0; i < 256; ++i)
        {
            buf.setSample(0, i, float(0.2 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 96000.0)));
            buf.setSample(1, i, 0.f);
        }
        p.processBlock(buf, midi);
        click(*editor, "system-page");
        check(p.takeOutputMeterPeak(0) == 0.f && !p.takeOutputClip(0), "returning from System clears stale peaks and clip events");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
