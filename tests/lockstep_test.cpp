// Stereo in lockstep (two channels sharing SIMD lanes in the circuit solves) must give what two independent channels
// give. Usage: lockstep_test <netlist dir>
#include "../Source/Plugin/Chain.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static std::string slurp(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static int failures = 0;
static void check(bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

// returns the difference between the two runs in dB relative to the signal, over the second half of the run
static double compare(const std::string& dir, double levelDb, bool differentChannels, bool compOn, double thr, bool hybrid = false)
{
    tubechannel::channel::NetlistSet nets;
    nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net"); nets.tube = slurp(dir + "/tube.net");
    nets.preout = slurp(dir + "/preout.net"); nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net");
    nets.eq1 = slurp(dir + "/eq1.net"); nets.eq2 = slurp(dir + "/eq2.net"); nets.eq3 = slurp(dir + "/eq3.net");
    nets.eq4 = slurp(dir + "/eq4.net"); nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");
    std::vector<double> out[2];
    for (int mode = 0; mode < 2; ++mode)
    {
        tc::ChainConfig cfg;
        cfg.channels = 2; cfg.maxBlock = 256; cfg.tolerances = true; cfg.lockstep = mode == 1;
        cfg.detectorMode = hybrid ? tc::DetectorMode::Prototype : tc::DetectorMode::Original;
        cfg.engine.compactSupply = hybrid;
        tc::Chain chain;
        chain.prepare(cfg, nets);
        tubechannel::channel::Telemetry tel[2];
        tubechannel::channel::Telemetry* tp[2] = { &tel[0], &tel[1] };
        tubechannel::channel::ChannelControls ca, cb;
        ca.compActive = cb.compActive = compOn;
        ca.threshold = cb.threshold = thr;
        ca.gain = cb.gain = 0.2;
        tc::ChainGlobals g;
        std::vector<float> L(256), R(256);
        float* audio[2] = { L.data(), R.data() };
        long ph = 0;
        const double a = std::pow(10.0, levelDb / 20.0);
        for (int b = 0; b < 300; ++b)
        {
            for (int i = 0; i < 256; ++i, ++ph)
            {
                L[size_t(i)] = float(a * std::sin(2.0 * 3.14159265 * 220.0 * double(ph) / 48000.0));
                R[size_t(i)] = float(differentChannels ? 0.7 * a * std::sin(2.0 * 3.14159265 * 337.0 * double(ph) / 48000.0) : L[size_t(i)]);
            }
            chain.process(audio, 256, nullptr, 0, ca, cb, g, tp);
            if (b >= 150)
                for (int i = 0; i < 256; ++i) { out[mode].push_back(L[size_t(i)]); out[mode].push_back(R[size_t(i)]); }
        }
    }
    double sig = 0.0, diff = 0.0;
    for (size_t i = 0; i < out[0].size(); ++i)
    {
        sig += out[0][i] * out[0][i];
        diff += (out[0][i] - out[1][i]) * (out[0][i] - out[1][i]);
    }
    return 10.0 * std::log10(diff / std::max(sig, 1e-30) + 1e-30);
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::puts("usage: lockstep_test <netlist dir>"); return 2; }
    const std::string dir = argv[1];
    if (std::getenv("SWEEP")) { for (double lv : { -30.0, -24.0, -18.0, -12.0, -6.0, -3.0 }) for (double th : { 0.1, 0.5 }) std::printf("  level %.0f thr %.1f: %.1f dB\n", lv, th, compare(dir, lv, std::getenv("SAME") == nullptr, true, th)); return 0; }
    const double a = compare(dir, -24.0, true, true, 0.5);
    const double b = compare(dir, -6.0, true, true, 0.1);
    const double c = compare(dir, -12.0, false, false, 0.5);
    std::printf("  difference, lockstep against independent channels: quiet %.1f dB, loud and compressing %.1f dB, mono-compatible bypassed %.1f dB\n", a, b, c);
    check(a < -100.0, "quiet, different channels");
    check(b < -100.0, "loud with compression, different channels");
    check(c < -100.0, "compressor bypassed");
    const double h = compare(dir, -6.0, true, true, 0.1, true);
    std::printf("  hybrid lockstep against independent channels: %.1f dB\n", h);
    check(h < -125.0, "hybrid detector/supply preserve stereo lockstep agreement");
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
