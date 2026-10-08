// Offline closed-loop comparison. Reports fidelity; does not certify a sound percentage.
#include "../Source/Plugin/Chain.h"
#include <cstdio>
#include <fstream>
#include <sstream>

static std::string readText(const std::string& p)
{
    std::ifstream f(p); if (!f) throw std::runtime_error("missing netlist: " + p);
    std::stringstream s; s << f.rdbuf(); return s.str();
}
struct Capture { std::vector<float> audio, gr; bool finite = true; };
static Capture run(const tubechannel::channel::NetlistSet& nets, int fixture, bool hybrid)
{
    tc::ChainConfig cfg; cfg.channels = 2; cfg.maxBlock = 1024; cfg.detectorMode = hybrid ? tc::DetectorMode::Prototype : tc::DetectorMode::Original;
    tc::Chain chain; chain.prepare(cfg, nets);
    tc::ChainGlobals globals; globals.noise = false;
    tubechannel::channel::ChannelControls controls;
    if (fixture) controls.attack = controls.release = 1.0;
    controls.autoDetect = fixture == 2;
    controls.externalSidechain = fixture == 3;
    tubechannel::channel::Telemetry telemetry[2];
    tubechannel::channel::Telemetry* tp[] = { &telemetry[0], &telemetry[1] };
    std::vector<float> left(1024), right(1024), sc(1024);
    float* audio[] = { left.data(), right.data() }; const float* sidechain[] = { sc.data() };
    Capture capture;
    for (int b = 0; b < 180; ++b)
    {
        for (int i = 0; i < 1024; ++i)
        {
            const double t = double(b * 1024 + i) / 48000.0;
            const double cycle = t - std::floor(t);
            const double envelope = fixture ? std::clamp(std::min((cycle - 0.1) / 0.005, (0.4 - cycle) / 0.005), 0.0, 1.0) : 1.0;
            const double level = fixture ? 0.8 : 0.126;
            left[i] = float(level * envelope * std::sin(2.0 * 3.141592653589793 * 220.0 * t));
            right[i] = float(level * envelope * 0.7 * std::sin(2.0 * 3.141592653589793 * 337.0 * t));
            sc[i] = float(0.8 * envelope * std::sin(2.0 * 3.141592653589793 * 997.0 * t));
        }
        chain.process(audio, 1024, sidechain, controls.externalSidechain ? 1 : 0, controls, controls, globals, tp);
        if (b >= 30)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                capture.gr.push_back(telemetry[ch].grDb.load());
                for (int i = 0; i < 1024; ++i)
                {
                    capture.finite &= std::isfinite(audio[ch][i]);
                    capture.audio.push_back(audio[ch][i]);
                }
            }
        }
    }
    return capture;
}
int main(int argc, char** argv)
{
    if (argc != 2) { std::puts("usage: hybrid_chain_compare <netlist dir>"); return 2; }
    tubechannel::channel::NetlistSet nets;
#define LOAD(stage) nets.stage = readText(std::string(argv[1]) + "/" #stage ".net")
    LOAD(mic); LOAD(inst); LOAD(tube); LOAD(preout); LOAD(comp); LOAD(det);
    LOAD(eq1); LOAD(eq2); LOAD(eq3); LOAD(eq4); LOAD(out); LOAD(psu);
#undef LOAD
    const char* names[] = { "default quiet", "maximum manual attack/release", "Auto bursts", "external sidechain, maximum manual timing" };
    bool finite = true;
    for (int fixture = 0; fixture < 4; ++fixture)
    {
        const auto ref = run(nets, fixture, false), candidate = run(nets, fixture, true);
        double signal = 0.0, difference = 0.0, peak = 0.0, grSquare = 0.0, grMax = 0.0;
        for (size_t i = 0; i < ref.audio.size(); ++i)
        {
            signal += double(ref.audio[i]) * ref.audio[i];
            const double d = double(candidate.audio[i]) - ref.audio[i]; difference += d * d;
            peak = std::max(peak, std::fabs(double(candidate.audio[i])));
        }
        for (size_t i = 0; i < ref.gr.size(); ++i)
        {
            const double d = double(candidate.gr[i]) - ref.gr[i];
            grSquare += d * d; grMax = std::max(grMax, std::fabs(d));
        }
        finite &= ref.finite && candidate.finite;
        std::printf("%s: audio error %.3f dB relative RMS, GR RMS %.3f dB, max %.3f dB, candidate peak %.6f, finite %s\n",
            names[fixture], 10.0 * std::log10(difference / std::max(signal, 1e-30) + 1e-30),
            std::sqrt(grSquare / ref.gr.size()), grMax, peak, ref.finite && candidate.finite ? "yes" : "NO");
    }
    // Accuracy is reported separately; a finite result is not a fidelity pass.
    return finite ? 0 : 1;
}
