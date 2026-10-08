// External sidechain: the detector must follow the sidechain input and ignore the main input, and an external
// signal at the same digital level as the main signal must give a comparable gain reduction.
// Usage: sidechain_test <netlist dir>
#include "../Source/Plugin/Chain.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static std::string slurp(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static int failures = 0;
static void check(bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

// returns the gain reduction (dB) after `seconds`, for a main level and an optional sidechain level (both dBFS peak)
static double run(const std::string& dir, bool external, double mainDb, double scDb, bool hybrid = false)
{
    tubechannel::channel::NetlistSet nets;
    nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net"); nets.tube = slurp(dir + "/tube.net");
    nets.preout = slurp(dir + "/preout.net"); nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net"); nets.eq1 = slurp(dir + "/eq1.net");
    nets.eq2 = slurp(dir + "/eq2.net"); nets.eq3 = slurp(dir + "/eq3.net"); nets.eq4 = slurp(dir + "/eq4.net");
    nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");
    tc::ChainConfig cfg;
    cfg.channels = 1; cfg.maxBlock = 256; cfg.tolerances = false;
    cfg.detectorMode = hybrid ? tc::DetectorMode::Prototype : tc::DetectorMode::Original;
    tc::Chain chain;
    chain.prepare(cfg, nets);
    tubechannel::channel::Telemetry tel[2];
    tubechannel::channel::Telemetry* tp[2] = { &tel[0], &tel[1] };
    tubechannel::channel::ChannelControls ca, cb;
    ca.threshold = 0.2; ca.ratio = 0.6; ca.externalSidechain = external;
    tc::ChainGlobals g;
    g.noise = false;
    std::vector<float> L(256), S(256);
    float* audio[1] = { L.data() };
    const float* sc[1] = { S.data() };
    const double am = std::pow(10.0, mainDb / 20.0), as = std::pow(10.0, scDb / 20.0);
    long ph = 0;
    double gr = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < 256; ++i, ++ph)
        {
            L[size_t(i)] = float(am * std::sin(2.0 * 3.14159265 * 220.0 * double(ph) / 48000.0));
            S[size_t(i)] = float(as * std::sin(2.0 * 3.14159265 * 1000.0 * double(ph) / 48000.0));
        }
        chain.process(audio, 256, sc, 1, ca, cb, g, tp);
    }
    gr = tel[0].grDb.load();
    return gr;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::puts("usage: sidechain_test <netlist dir>"); return 2; }
    const std::string dir = argv[1];
    const double intLoud = run(dir, false, -14.0, -120.0);
    const double extLoudQuietMain = run(dir, true, -80.0, -14.0);
    const double extQuietLoudMain = run(dir, true, -14.0, -120.0);
    const double intQuiet = run(dir, false, -80.0, -14.0);
    std::printf("  internal, loud main:                 GR %.2f dB\n", intLoud);
    std::printf("  external, loud sidechain, quiet main: GR %.2f dB\n", extLoudQuietMain);
    std::printf("  external, silent sidechain, loud main: GR %.2f dB\n", extQuietLoudMain);
    std::printf("  internal, quiet main, loud sidechain: GR %.2f dB\n", intQuiet);
    check(intLoud > 3.0, "internal detector compresses a loud main signal");
    check(extLoudQuietMain > 3.0, "external detector compresses on the sidechain alone");
    check(extQuietLoudMain < 0.5, "external detector ignores the main input");
    check(intQuiet < 0.5, "internal detector ignores the sidechain input");
    // the internal detector sits after the gain element (it hears the compressed signal), the external one is
    // feed-forward, so at the same digital level the external reduction is larger
    check(extLoudQuietMain >= intLoud && extLoudQuietMain < intLoud + 20.0, "external (feed-forward) reduction is at least the internal one");
    const double hybridExternal = run(dir, true, -80.0, -14.0, true);
    const double hybridSilent = run(dir, true, -14.0, -120.0, true);
    const double hybridInternal = run(dir, false, -80.0, -14.0, true);
    check(hybridExternal > 3.0, "compact detector follows external sidechain");
    check(hybridSilent < 0.5 && hybridInternal < 0.5, "compact detector respects internal/external routing");
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
