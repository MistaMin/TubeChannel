// Reject heap activity inside audio processing, including startup, automation and nonlinear transitions.
#include "Plugin/Chain.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>

// This test runs serially. Count only work inside Chain::process, never fixture construction.
static bool inAudio = false;
static size_t allocations = 0, frees = 0;

void* operator new(std::size_t n)
{
    if (inAudio) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept
{
    if (inAudio && p) ++frees;
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

static std::string slurp(const std::string& p)
{
    std::ifstream f(p);
    if (!f) throw std::runtime_error("cannot read " + p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::puts("usage: realtime_test <netlist directory>");
        return 2;
    }
    const std::string dir = argv[1];
    tubechannel::channel::NetlistSet nets;
    nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net"); nets.tube = slurp(dir + "/tube.net");
    nets.preout = slurp(dir + "/preout.net"); nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net");
    nets.eq1 = slurp(dir + "/eq1.net"); nets.eq2 = slurp(dir + "/eq2.net"); nets.eq3 = slurp(dir + "/eq3.net");
    nets.eq4 = slurp(dir + "/eq4.net"); nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");
    int failures = 0;
    for (int scenario = 0; scenario < 21; ++scenario)
    {
        tc::ChainConfig cfg;
        const bool qualityMode = scenario >= 12;
        cfg.channels = qualityMode ? 2 : scenario % 2 + 1;
        cfg.maxBlock = 256;
        cfg.sampleRate = (scenario >= 6 && !qualityMode) || scenario == 16 ? 96000.0 : 48000.0;
        cfg.quality = qualityMode ? static_cast<tubechannel::dsp::Quality>(std::min(scenario - 12, 3))
            : scenario == 4 ? tubechannel::dsp::Quality::Derp : tubechannel::dsp::Quality::Default;
        cfg.linearPhase = scenario == 5 || scenario == 17 || scenario == 20;
        if(scenario>=18)cfg.oversamplingStages=scenario-17;
        cfg.detectorMode = qualityMode ? tc::DetectorMode::FollowQuality
            : scenario >= 8 ? tc::DetectorMode::Prototype : tc::DetectorMode::Original;
        cfg.engine.compactSupply = scenario == 10 || scenario == 11;
        tc::Chain chain;
        chain.prepare(cfg, nets);
        tubechannel::channel::Telemetry tel[2];
        tubechannel::channel::Telemetry* tp[] = { &tel[0], &tel[1] };
        tubechannel::channel::ChannelControls ca, cb;
        tc::ChainGlobals g;
        g.dualMono = scenario == 3 || scenario == 9;
        g.stereoLink = scenario != 11;
        ca.instrument = scenario == 2;
        ca.limiter = scenario == 7;
        std::vector<float> L(256), R(256);
        float* audio[] = { L.data(), R.data() };
        long phase = 0;
        allocations = frees = 0;
        for (int b = 0; b < 180; ++b)
        {
            const int count = b % 3 == 0 ? 63 : b % 3 == 1 ? 256 : 127;
            // Exercise continuous smoothing and independent stereo controls.
            if (scenario == 3 || scenario == 6 || scenario >= 8)
            {
                ca.gain = 0.18 + 0.15 * std::sin(b * 0.07);
                ca.low = 0.5 + 0.2 * std::sin(b * 0.13);
                ca.hmFreq = 0.4 + 0.3 * std::sin(b * 0.09);
                ca.threshold = 0.3 + 0.15 * std::sin(b * 0.04);
                if (scenario >= 8)
                {
                    ca.attack = 0.5 + 0.5 * std::sin(b * 0.09);
                    ca.release = 1.0;
                    ca.autoDetect = b >= 90;
                    ca.compActive = b < 130 || b >= 150;
                    ca.limiter = b >= 160;
                }
                cb = ca;
                cb.gain *= 0.8;
            }
            if (scenario == 2 && b == 90) ca.gainPlus20 = true;
            for (int i = 0; i < count; ++i, ++phase)
            {
                const double t = phase / cfg.sampleRate;
                L[size_t(i)] = float(b < 5 ? 0.0 : 0.16 * std::sin(2.0 * 3.141592653589793 * 220.0 * t)
                                                   + 0.035 * std::sin(2.0 * 3.141592653589793 * 4000.0 * t));
                R[size_t(i)] = float(b < 5 ? 0.0 : 0.13 * std::sin(2.0 * 3.141592653589793 * 337.0 * t));
            }
            inAudio = true;
            chain.process(audio, count, nullptr, 0, ca, cb, g, tp);
            inAudio = false;
            for (int ch = 0; ch < cfg.channels; ++ch)
                for (int i = 0; i < count; ++i)
                    if (!std::isfinite(audio[ch][i])) ++failures;
        }
        std::printf("scenario %d (%d channels, %.0f Hz, %dx): allocations %zu, frees %zu\n",
                    scenario, cfg.channels, cfg.sampleRate, chain.factor(), allocations, frees);
        if (allocations || frees) ++failures;
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
