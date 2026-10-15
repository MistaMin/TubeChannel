// Host-style test of the real processor: layouts, parameters, latency, live mode switching.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <memory>
#include <vector>

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
    if (juce::String(id) == tc::params::id::quality)
    {
        p.apvts.getParameter(tc::params::id::offlineBounceEnabled)->setValueNotifyingHost(0.f);
        auto* os = p.apvts.getParameter(tc::params::id::oversampling);
        os->setValueNotifyingHost(os->convertTo0to1(0.f)); // legacy-mode regression fixtures
        p.apvts.getParameter(tc::params::id::eco)->setValueNotifyingHost(index == 3 ? 1.0f : 0.0f);
        if (index == 3) return;
    }
    auto* prm = p.apvts.getParameter(id);
    prm->setValueNotifyingHost(prm->convertTo0to1(float(index)));
}
static void setBool(TubeChannelProcessor& p, const char* id, bool on) { p.apvts.getParameter(id)->setValueNotifyingHost(on ? 1.0f : 0.0f); }
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Offline render integration and callback allocation checks.
#include "offline_bounce_test.h"

// Serial callbacks through the real processor, including its parameter reads,
// meters and spectrum ring. This is a host-style harness, not live DAW playback.
static int benchmark()
{
    constexpr int instances = 16, warmup = 64, blocks = 320;
    namespace id = tc::params::id;
    int failures = 0;
    for (double rate : { 48000.0, 96000.0 })
        for (int size : { 32, 64, 256 })
            for (int quality : { 0, 3 })
            {
                std::vector<std::unique_ptr<TubeChannelProcessor>> processors;
                std::vector<juce::AudioBuffer<float>> audio;
                for (int i = 0; i < instances; ++i)
                {
                    auto p = std::make_unique<TubeChannelProcessor>();
                    setChoice(*p, id::quality, quality);
                    p->setRateAndBufferSizeDetails(rate, size);
                    p->prepareToPlay(rate, size);
                    processors.push_back(std::move(p));
                    audio.emplace_back(2, size);
                }
                juce::MidiBuffer midi;
                double cpu = 0, worst = 0;
                int late = 0;
                for (int block = 0; block < blocks; ++block)
                {
                    for (auto& buffer : audio)
                        for (int i = 0; i < size; ++i)
                        {
                            const float value = float(.126 * std::sin(6.283185307179586 * 220 * double(block * size + i) / rate));
                            buffer.setSample(0, i, value); buffer.setSample(1, i, value);
                        }
                    const auto c = std::clock(); const auto t = std::chrono::steady_clock::now();
                    for (int i = 0; i < instances; ++i) processors[size_t(i)]->processBlock(audio[size_t(i)], midi);
                    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
                    const double used = double(std::clock()-c)/CLOCKS_PER_SEC;
                    if (block >= warmup) { cpu += used; worst = std::max(worst, wall); if (wall > size/rate) ++late; }
                    for (const auto& buffer : audio)
                        for (int ch = 0; ch < 2; ++ch)
                            for (int i = 0; i < size; ++i) if (!std::isfinite(buffer.getSample(ch,i))) ++failures;
                }
                std::printf("HOST %.0f %d %d instances %d CPU_ms %.6f worst_ms %.6f missed %d/%d\n",
                            rate,size,quality,instances,cpu/(blocks-warmup)*1000,worst*1000,late,blocks-warmup);
                std::fflush(stdout);
            }
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 2 && juce::String(argv[1]) == "benchmark") return benchmark();
    if (argc == 2 && juce::String(argv[1]) == "offline") return offlineBounceTest();
    std::printf("TubeChannel processor test\n");
    namespace id = tc::params::id;

    // New instances use Eco. Released normalized automation anchors and stored
    // quality indices must retain their meanings after appending a fourth mode.
    {
        TubeChannelProcessor p;
        auto* quality = p.apvts.getParameter(id::quality);
        check(p.getParameters()[p.getParameters().size()-4] == p.apvts.getParameter(id::eco), "Eco parameter appended after every released parameter");
        check(p.apvts.getParameter(id::eco)->getVersionHint() == 2, "Eco gets a new AU ordering version hint");
        check(p.apvts.getRawParameterValue(id::eco)->load() == 1, "new instances default to Eco");
        for (int index = 0; index < 3; ++index) {
            quality->setValueNotifyingHost(float(index) * .5f);
            check(p.apvts.getRawParameterValue(id::quality)->load() == index, "released automation anchor preserved");
        }
        for (int index = 0; index < 4; ++index) {
            setChoice(p, id::quality, index);
            juce::MemoryBlock state; p.getStateInformation(state);
            setChoice(p, id::quality, (index + 1) % 4);
            p.setStateInformation(state.getData(), int(state.getSize()));
            check((p.apvts.getRawParameterValue(id::eco)->load() > .5f ? 3 : int(p.apvts.getRawParameterValue(id::quality)->load())) == index, "saved quality index restores correctly");
        }
    }

    // A released state lacks the Eco flag. It must override the new default
    // and retain its original quality and continuous normalized automation.
    {
        TubeChannelProcessor p;
        setChoice(p, id::quality, 2);
        auto legacy = p.apvts.copyState();
        legacy.removeChild(legacy.getChildWithProperty("id", id::eco), nullptr);
        legacy.removeChild(legacy.getChildWithProperty("id", id::oversampling), nullptr);
        juce::MemoryBlock binary;
        juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), binary);
        setChoice(p, id::quality, 3);
        p.setStateInformation(binary.getData(), int(binary.getSize()));
        check(p.apvts.getRawParameterValue(id::eco)->load() == 0, "released state restores original engine rather than Eco");
        check(p.apvts.getRawParameterValue(id::quality)->load() == 2, "released Derp choice preserved");
        auto* q = p.apvts.getParameter(id::quality);
        for (float value : { .2f, .3f, .7f, .8f }) {
            q->setValueNotifyingHost(value);
            const int expected = value < .25f ? 0 : value < .75f ? 1 : 2;
            check(p.apvts.getRawParameterValue(id::quality)->load() == expected, "released automation ramp thresholds preserved");
        }
    }

    // Every explicit oversampling setting applies independently of the engine and session rate.
    for(double rate : {48000.,96000.}) for(int mode : {3,0,1}) for(int stages=0;stages<=3;++stages) {
        TubeChannelProcessor p;
        setChoice(p,id::quality,mode);
        setChoice(p,id::oversampling,stages+1);
        setBool(p,id::linearPhase,stages==3);
        p.prepareToPlay(rate,64);
        check(p.currentFactor()==(1<<stages),"independent explicit oversampling factor",p.currentFactor());
        check(p.usesPrototypeCompressor()==(mode!=1),"oversampling does not alter engine detector");
        juce::AudioBuffer<float> b(2,64);juce::MidiBuffer midi;
        for(int block=0;block<12;++block) {
            for(int i=0;i<64;++i){float v=float(.05*std::sin(6.283185307179586*997*(block*64+i)/rate));b.setSample(0,i,v);b.setSample(1,i,v);}
            p.processBlock(b,midi);
            for(int ch=0;ch<2;++ch)for(int i=0;i<64;++i)check(std::isfinite(b.getSample(ch,i)),"oversampled engine output finite");
        }
        juce::MemoryBlock saved;p.getStateInformation(saved);setChoice(p,id::oversampling,1);
        p.setStateInformation(saved.getData(),int(saved.getSize()));
        check(p.apvts.getRawParameterValue(id::oversampling)->load()==stages+1,"independent oversampling state restores");
    }

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

    // Cached parameter addresses must keep following automation, state restoration and presets.
    {
        TubeChannelProcessor p;
        tc::params::ParameterValues a(p.apvts, false), b(p.apvts, true);
        p.apvts.getParameter(id::gain)->setValueNotifyingHost(0.25f);
        p.apvts.getParameter(tc::params::idFor(id::gain, true))->setValueNotifyingHost(0.75f);
        setChoice(p, id::micZ, 300);
        const auto saved = p.apvts.copyState();
        p.apvts.getParameter(id::gain)->setValueNotifyingHost(0.9f);
        check(std::fabs(tc::params::readControls(a).gain - 0.9) < 1e-6, "cached controls follow automation");
        p.apvts.replaceState(saved);
        check(std::fabs(tc::params::readControls(a).gain - 0.25) < 1e-6, "cached channel A survives state restoration");
        check(std::fabs(tc::params::readControls(b).gain - 0.75) < 1e-6, "cached channel B stays independent");
        check(std::fabs(tc::params::readControls(b).micSourceOhms - 300.0) < 1e-3, "cached global impedance is shared");
        p.setCurrentProgram(0);
        check(std::fabs(tc::params::readControls(a).gain) < 1e-6, "cached controls follow presets");
    }

    // 2. Audio: level, latency and the oversampling factors at 96 kHz (Default = 1x there)
    for (int quality = 0; quality < 4; ++quality)
        for (bool lin : { false, true })
        {
            TubeChannelProcessor p;
            setChoice(p, id::quality, quality);
            setBool(p, id::linearPhase, lin);
            p.setRateAndBufferSizeDetails(96000.0, 256);
            p.prepareToPlay(96000.0, 256);
            const int expected = quality == 2 ? 2 : 1;
            check(p.usesPrototypeCompressor() == ((quality == 0 || quality == 3)), "quality selects the compressor backend");
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
        check(p.currentFactor() == 1 && !p.usesPrototypeCompressor(), "HQ at 96 kHz uses original detector at 1x", p.currentFactor());
        auto adopt = [&](int before) {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.;
            do {
                pump(10); buf.clear(); p.processBlock(buf, midi);
            } while (p.swapCount() == before && juce::Time::getMillisecondCounterHiRes() < deadline);
        };
        for (int quality : { 0, 1, 2, 3, 1, 3, 0 })
        {
            const int before = p.swapCount();
            setChoice(p, id::quality, quality);
            adopt(before);
            check(p.usesPrototypeCompressor() == ((quality == 0 || quality == 3)), "live switching adopts requested compressor backend");
            check(p.swapCount() == before + 1, "successive mode switches always have a retirement slot");
            check(p.currentFactor() == (quality == 2 ? 2 : 1), "successive mode switch adopts requested factor");
        }
        // The saved choice must restore the backend even when the factor stays unchanged.
        int before = p.swapCount(); setChoice(p, id::quality, 1); adopt(before);
        juce::MemoryBlock saved; p.getStateInformation(saved);
        before = p.swapCount(); setChoice(p, id::quality, 0); adopt(before);
        check(p.usesPrototypeCompressor(), "Default restores the prototype after HQ");
        before = p.swapCount();
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        adopt(before);
        check(!p.usesPrototypeCompressor() && p.currentFactor() == 1, "saved HQ state restores original response at unchanged factor");
        std::printf("  live switching and state restoration ok (swaps %d)\n", p.swapCount());
    }
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
