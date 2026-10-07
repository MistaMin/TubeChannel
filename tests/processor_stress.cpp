// Host-style test of the real processor: layouts, parameters, latency, live mode switching.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>

#include "PluginProcessor.h"

static int failures = 0;
static void check(bool ok, const char* what, double value = 0.0)
{
    if (!ok)
    {
        ++failures;
        std::printf("  FAIL %-60s %g\n", what, value);
    }
}

static void setChoice(TubeChannelProcessor& p, const char* id, int index)
{
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(float(index)));
}
static void setBool(TubeChannelProcessor& p, const char* id, bool on) { p.apvts.getParameter(id)->setValueNotifyingHost(on ? 1.0f : 0.0f); }
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("TubeChannel processor test\n");
    namespace id = tc::params::id;

    // 1. Layouts
    {
        TubeChannelProcessor p;
        using Set = juce::AudioChannelSet;
        auto layout = [&](Set in, Set out, Set sc)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add(in);
            l.outputBuses.add(out);
            l.inputBuses.add(sc);
            return p.checkBusesLayoutSupported(l);
        };
        check(layout(Set::stereo(), Set::stereo(), Set::disabled()), "stereo, no sidechain");
        check(layout(Set::stereo(), Set::stereo(), Set::stereo()), "stereo + stereo sidechain");
        check(layout(Set::mono(), Set::mono(), Set::mono()), "mono + mono sidechain");
        check(!layout(Set::stereo(), Set::mono(), Set::disabled()), "stereo in / mono out rejected");
        check(!layout(Set::create5point1(), Set::create5point1(), Set::disabled()), "5.1 rejected");
        std::printf("  layouts ok\n");
    }

    // 2. Audio: level, latency and the oversampling factors at 96 kHz (Default = 1x there)
    for (int quality = 0; quality < 2; ++quality)
        for (bool lin : { false, true })
        {
            TubeChannelProcessor p;
            setChoice(p, id::quality, quality);
            setBool(p, id::linearPhase, lin);
            p.setRateAndBufferSizeDetails(96000.0, 256);
            p.prepareToPlay(96000.0, 256);
            const int expected = quality == 0 ? 1 : 2;
            check(p.currentFactor() == expected, "factor adapts to the session rate", p.currentFactor());
            check(lin && expected > 1 ? p.getLatencySamples() > 0 : p.getLatencySamples() == 0, "latency only for +L", p.getLatencySamples());
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            double peak = 0.0;
            long phase = 0;
            for (int b = 0; b < 40; ++b)
            {
                for (int i = 0; i < 256; ++i, ++phase)
                {
                    const float v = float(0.1 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * double(phase) / 96000.0));
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
                p.processBlock(buf, midi);
                if (b > 20)
                    for (int i = 0; i < 256; ++i)
                        peak = std::max(peak, double(std::fabs(buf.getSample(0, i))));
            }
            check(std::isfinite(peak) && peak > 0.001 && peak < 2.0, "output is finite and non-silent", peak);
            std::printf("  quality %d%s: factor %d latency %d, output peak %.3f\n", quality, lin ? " +L" : "", p.currentFactor(), p.getLatencySamples(), peak);
        }

    // 3. Live mode switching
    {
        TubeChannelProcessor p;
        p.setRateAndBufferSizeDetails(96000.0, 256);
        p.prepareToPlay(96000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        long phase = 0;
        bool finite = true;
        setChoice(p, id::quality, 1);
        for (int b = 0; b < 30; ++b)
        {
            if (b == 3) pump(4000);
            for (int i = 0; i < 256; ++i, ++phase)
                buf.setSample(0, i, float(0.1 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * double(phase) / 96000.0))), buf.setSample(1, i, buf.getSample(0, i));
            p.processBlock(buf, midi);
            for (int i = 0; i < 256; ++i) finite = finite && std::isfinite(buf.getSample(0, i));
        }
        check(finite, "finite while switching");
        check(p.currentFactor() == 2, "HQ at 96 kHz is 2x", p.currentFactor());
        std::printf("  live switching ok (swaps %d)\n", p.swapCount());
    }
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
