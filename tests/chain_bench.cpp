// Benchmark of the plugin's processing chain without JUCE: stereo, Default quality at 48 kHz.
#include "../Source/Plugin/Chain.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

static std::string slurp(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

int main(int argc, char** argv)
{
    const std::string dir = argv[1];
    const int channels = argc > 2 ? std::atoi(argv[2]) : 2;
    tubechannel::channel::NetlistSet nets;
    nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net"); nets.tube = slurp(dir + "/tube.net");
    nets.preout = slurp(dir + "/preout.net"); nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net"); nets.eq1 = slurp(dir + "/eq1.net");
    nets.eq2 = slurp(dir + "/eq2.net"); nets.eq3 = slurp(dir + "/eq3.net"); nets.eq4 = slurp(dir + "/eq4.net");
    nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");
    tc::ChainConfig cfg;
    cfg.channels = channels; cfg.maxBlock = 256; cfg.tolerances = argc > 3 ? std::atoi(argv[3]) != 0 : true;
    cfg.lockstep = !std::getenv("NOLOCK");
    tc::Chain chain;
    const auto t0 = std::chrono::steady_clock::now();
    chain.prepare(cfg, nets);
    std::printf("prepare %.2f s (%d ch, factor %dx)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), channels, chain.factor());
    tubechannel::channel::Telemetry tel[2];
    tubechannel::channel::Telemetry* tp[2] = { &tel[0], &tel[1] };
    tubechannel::channel::ChannelControls ca, cb;
    tc::ChainGlobals g;
    g.psuModel = argc > 4 ? std::atoi(argv[4]) != 0 : true;
    std::vector<float> L(256), R(256);
    float* audio[2] = { L.data(), R.data() };
    long ph = 0;
    const int blocks = argc > 5 ? std::atoi(argv[5]) : 400;
    double worst = 0, total = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < 256; ++i, ++ph) L[size_t(i)] = R[size_t(i)] = float((argc > 6 ? std::atof(argv[6]) : 0.126) * std::sin(2.0 * 3.14159265 * 220.0 * double(ph) / 48000.0));
        const auto a = std::chrono::steady_clock::now();
        chain.process(audio, 256, nullptr, 0, ca, cb, g, tp);
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        if (b > 50) { worst = std::max(worst, t); total += t; }
    }
    const double perBlock = total / (blocks - 51);
    std::printf("block of 256 @48k: avg %.2f ms, worst %.2f ms -> %.0f%% of realtime (%.2f us per oversampled sample per channel)\n",
                perBlock * 1e3, worst * 1e3, perBlock / (256.0 / 48000.0) * 100.0, perBlock / (256.0 * chain.factor() * channels) * 1e6);
}
