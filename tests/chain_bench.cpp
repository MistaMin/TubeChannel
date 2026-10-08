// Benchmark the processing chain without JUCE at 48 kHz, including deadline misses and automation.
#include "../Source/Plugin/Chain.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

static std::string slurp(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::puts("usage: chain_bench <netlist dir> [channels=2] [tolerances=1] [psu=1] [blocks=400] [amplitude=0.126] [block=256] [quality=0] [automate=0:off/1:gain/2:EQ] [attack=0.5] [release=0.5] [bursts=0] [detectorOverride=-1:quality/0:circuit/1:prototype] [compactSupply=0]");
        return 2;
    }
    const std::string dir = argv[1];
    const int channels = argc > 2 ? std::atoi(argv[2]) : 2;
    const int blocks = argc > 5 ? std::atoi(argv[5]) : 400;
    const double amplitude = argc > 6 ? std::atof(argv[6]) : 0.126;
    const int block = argc > 7 ? std::atoi(argv[7]) : 256;
    const int quality = argc > 8 ? std::atoi(argv[8]) : 0;
    const int automate = argc > 9 ? std::atoi(argv[9]) : 0;
    const double attack = argc > 10 ? std::atof(argv[10]) : 0.5;
    const double release = argc > 11 ? std::atof(argv[11]) : 0.5;
    const bool bursts = argc > 12 && std::atoi(argv[12]) != 0;
    constexpr int warmup = 51;
    if (channels < 1 || channels > 2 || blocks <= warmup || block < 1 || quality < 0 || quality > 2 || automate < 0 || automate > 2 || !std::isfinite(amplitude)
        || !std::isfinite(attack) || !std::isfinite(release) || attack < 0 || attack > 1 || release < 0 || release > 1)
    {
        std::puts("invalid benchmark configuration");
        return 2;
    }
    tubechannel::channel::NetlistSet nets;
    nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net"); nets.tube = slurp(dir + "/tube.net");
    nets.preout = slurp(dir + "/preout.net"); nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net"); nets.eq1 = slurp(dir + "/eq1.net");
    nets.eq2 = slurp(dir + "/eq2.net"); nets.eq3 = slurp(dir + "/eq3.net"); nets.eq4 = slurp(dir + "/eq4.net");
    nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");
    tc::ChainConfig cfg;
    cfg.channels = channels;
    cfg.maxBlock = block;
    cfg.quality = static_cast<tubechannel::dsp::Quality>(quality);
    cfg.tolerances = argc > 3 ? std::atoi(argv[3]) != 0 : true;
    cfg.tiledLinear = !std::getenv("REFERENCE_LINEAR");
    cfg.lockstep = !std::getenv("NOLOCK");
    std::printf("paired linear layout: %s\n", cfg.tiledLinear ? "tiled" : "reference columns");
    const int detectorOverride = argc > 13 ? std::atoi(argv[13]) : -1;
    if (detectorOverride < -1 || detectorOverride > 1) { std::puts("invalid detector override"); return 2; }
    cfg.detectorMode = detectorOverride < 0 ? tc::DetectorMode::FollowQuality
        : detectorOverride == 0 ? tc::DetectorMode::Original : tc::DetectorMode::Prototype;
    cfg.engine.compactSupply = argc > 14 && std::atoi(argv[14]) != 0;
    tc::Chain chain;
    const auto t0 = std::chrono::steady_clock::now();
    chain.prepare(cfg, nets);
    std::printf("engine: detector %s, supply %s\n", chain.config().engine.compactDetector ? "prototype" : "circuit",
                chain.config().engine.compactSupply ? "prototype" : "circuit");
    std::printf("prepare %.2f s (%d ch, factor %dx, automation %s)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), channels, chain.factor(), automate ? "on" : "off");
    tubechannel::channel::Telemetry tel[2];
    tubechannel::channel::Telemetry* tp[2] = { &tel[0], &tel[1] };
    tubechannel::channel::ChannelControls ca, cb;
    ca.attack = cb.attack = attack;
    ca.release = cb.release = release;
    std::printf("attack %.2f, release %.2f, stimulus %s\n", attack, release, bursts ? "bursts" : "continuous sine");
    tc::ChainGlobals g;
    g.psuModel = argc > 4 ? std::atoi(argv[4]) != 0 : true;
    std::vector<float> L(size_t(block), 0.f), R(size_t(block), 0.f);
    std::vector<double> times;
    times.reserve(size_t(blocks - warmup));
    float* audio[2] = { L.data(), R.data() };
    long ph = 0;
    double worst = 0, total = 0, totalCpu = 0;
    const double budget = double(block) / 48000.0;
    int late = 0;
    int nonfinite = 0;
    double peakOutput = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i, ++ph)
        {
            const double t = double(ph) / 48000.0;
            const double cycle = t - std::floor(t);
            const double envelope = bursts ? std::clamp(std::min((cycle - 0.1) / 0.005, (0.4 - cycle) / 0.005), 0.0, 1.0) : 1.0;
            L[size_t(i)] = R[size_t(i)] = float(amplitude * envelope * std::sin(2.0 * 3.14159265 * 220.0 * t));
        }
        if (automate == 1)
            ca.gain = 0.1 + 0.1 * std::sin(b * 0.05);
        if (automate == 2)
        {
            ca.low = ca.hmLevel = 0.5 + 0.45 * std::sin(b * 0.07);
            ca.high = ca.lmLevel = 1.0 - ca.low;
            ca.hmFreq = ca.lmFreq = 0.5 + 0.45 * std::sin(b * 0.03);
            ca.eqActive = (b / 20) % 2 != 0;
            ca.hmNarrow = ca.lmNarrow = (b / 30) % 2 != 0;
            ca.hmX10 = ca.lmX10 = (b / 40) % 2 != 0;
        }
#ifdef TC_PROFILE
        if (b == warmup) chain.resetProfile();
#endif
        const auto cpuStart = std::clock();
        const auto a = std::chrono::steady_clock::now();
        chain.process(audio, block, nullptr, 0, ca, cb, g, tp);
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        const double cpuTime = double(std::clock() - cpuStart) / CLOCKS_PER_SEC;
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < block; ++i)
                if (!std::isfinite(audio[ch][i])) ++nonfinite;
                else peakOutput = std::max(peakOutput, std::fabs(double(audio[ch][i])));
        if (b >= warmup)
        {
            worst = std::max(worst, t);
            total += t;
            totalCpu += cpuTime;
            times.push_back(t);
            if (t > budget) ++late;
        }
    }
    std::sort(times.begin(), times.end());
    const double perBlock = total / double(times.size());
    std::printf("block of %d @48k: avg %.2f ms, p99 %.2f ms, worst %.2f ms -> %.0f%% of realtime (%.2f us per oversampled sample per channel)\n",
                block, perBlock * 1e3, times[size_t(double(times.size() - 1) * 0.99)] * 1e3, worst * 1e3,
                perBlock / budget * 100.0, perBlock / (double(block) * chain.factor() * channels) * 1e6);
    std::printf("deadline %.3f ms: %d/%zu blocks missed\n", budget * 1e3, late, times.size());
    std::printf("process CPU: avg %.3f ms/block, %.1f%% of realtime (excludes time descheduled)\n",
                totalCpu / double(times.size()) * 1e3, totalCpu / double(times.size()) / budget * 100.0);
    std::printf("output peak %.6f, nonfinite samples %d\n", peakOutput, nonfinite);
#ifdef TC_PROFILE
    const char* names[] = { "mic", "instrument", "tube", "preout", "compressor audio", "detector", "opto + eq1", "eq2", "eq3", "eq4", "output", "controls + supply", "metering + loads" };
    double measured = 0.0;
    for (int i = 0; i < 13; ++i) measured += chain.stageTime(i);
    std::puts("Instrumented stage times (timers affect cost; excludes chain oversampling/scaling):");
    for (int i = 0; i < 13; ++i)
        std::printf("  %-20s %.3f ms/block %5.1f%%\n", names[i], 1000.0 * chain.stageTime(i) / times.size(), 100.0 * chain.stageTime(i) / measured);
#endif
    return nonfinite ? 1 : 0;
}
