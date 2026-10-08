// Compare every physical node against the previous paired inverse layout, including overload
// recovery, rail motion and EQ coefficient changes. No fitted response or timing alignment.
#include "../Source/Plugin/Chain.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel;

static std::string slurp(const std::string& path)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("missing netlist: " + path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

static int compareStages(const std::string& dir, double fs, bool tolerances)
{
    const char* names[] = { "inst", "preout", "eq1", "eq2", "eq3", "eq4", "out" };
    const char* inputs[] = { "O1", "V1OUT", "COUT", "X", "O10A", "O8A", "O8C" };
    int failures = 0;
    double worst = 0.0;
    size_t checked = 0;
    for (int stage = 0; stage < 7; ++stage)
    {
        circuit::Block blocks[4];
        int handles[4];
        for (int k = 0; k < 4; ++k)
        {
            circuit::ToleranceSettings tol;
            tol.enabled = tolerances;
            tol.seed = unsigned(11 + k % 2);
            auto text = slurp(dir + "/" + names[stage] + ".net");
            if (stage == 6) text += "RL OUTP OUTN 600\n";
            blocks[k].build(text, 1.0 / fs, tol, { "VP", "VN" }, { { inputs[stage], 10.0 } });
            blocks[k].setRails(15.0, -15.0, 45.0, 150.0);
            if (!blocks[k].dc()) ++failures;
            handles[k] = blocks[k].inputHandle(inputs[stage]);
        }
        circuit::CircuitPair reference(blocks[0].c, blocks[1].c, false);
        circuit::CircuitPair candidate(blocks[2].c, blocks[3].c, true);
        for (int window = 0; window < 16; ++window)
        {
            const double pot = double(window % 4) / 3.0;
            for (auto& b : blocks)
            {
                for (const auto* param : { "LOW", "HMID", "HMF", "MASTER" }) b.setParam(param, pot);
                for (const auto* param : { "HIGH", "LMID", "LMF" }) b.setParam(param, 1.0 - pot);
                for (const auto* param : { "LOSH", "HISH", "HMLO", "LMLO", "HMNAR", "LMNAR", "EQON" })
                    b.setParam(param, double((window / 2) % 2));
                b.applyParams();
            }
            for (int sample = 0; sample < 1024; ++sample)
            {
                const double t = double(window * 1024 + sample) / fs;
                // Alternating normal signal, slew/voltage overload, then silence for recovery.
                const double amplitude = window % 4 == 1 ? 24.0 : window % 4 == 2 ? 0.0 : 0.5;
                for (int k = 0; k < 4; ++k)
                {
                    const double phase = 6.283185307179586 * (k % 2 ? 11003.0 : 997.0) * t;
                    blocks[k].setSourceH(handles[k], amplitude * std::sin(phase));
                    const double rail = 15.0 - 2.0 * (0.5 + 0.5 * std::sin(6.283185307179586 * 60.0 * t));
                    blocks[k].setRails(rail, -rail, 45.0, 150.0);
                    blocks[k].setSupply(rail, -rail);
                }
                if (reference.step() != candidate.step()) ++failures;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& a = blocks[ch].c;
                    auto& b = blocks[ch + 2].c;
                    if (a.lastIterations() != b.lastIterations()) ++failures;
                    for (int node = 1; node <= a.nodeCount(); ++node)
                    {
                        const double av = a.v(node), bv = b.v(node);
                        const double error = std::fabs(av - bv);
                        worst = std::max(worst, error);
                        if (!std::isfinite(av) || !std::isfinite(bv) || error > 1e-10 * (1.0 + std::fabs(av))) ++failures;
                        ++checked;
                    }
                }
            }
        }
    }
    std::printf("%.1f kHz tolerances %s: %zu node comparisons, worst %.3g V, %d failures\n",
        fs / 1000.0, tolerances ? "on" : "off", checked, worst, failures);
    return failures;
}

static int compareChain(const channel::NetlistSet& nets, dsp::Quality quality, double fs)
{
    tc::Chain chains[2];
    for (int k = 0; k < 2; ++k)
    {
        tc::ChainConfig cfg;
        cfg.sampleRate = fs;
        cfg.channels = 2;
        cfg.maxBlock = 128;
        cfg.quality = quality;
        cfg.tiledLinear = k == 1;
        chains[k].prepare(cfg, nets);
    }
    channel::ChannelControls controls;
    channel::Telemetry meters[2][2];
    tc::ChainGlobals globals;
    int failures = 0;
    double energy = 0.0, error = 0.0;
    for (int block = 0; block < 100; ++block)
    {
        float audio[2][2][128];
        const double pot = double((block / 10) % 4) / 3.0;
        controls.low = controls.hmLevel = controls.hmFreq = pot;
        controls.high = controls.lmLevel = controls.lmFreq = 1.0 - pot;
        controls.eqActive = (block / 10) % 2 != 0;
        controls.hmX10 = controls.lmX10 = (block / 20) % 2 != 0;
        controls.hmNarrow = controls.lmNarrow = (block / 30) % 2 != 0;
        controls.attack = controls.release = pot;
        controls.autoDetect = (block / 25) % 2 != 0;
        controls.limiter = block > 70 && block < 80;
        globals.dualMono = block > 50;
        for (int ch = 0; ch < 2; ++ch)
            for (int sample = 0; sample < 128; ++sample)
            {
                const double t = double(block * 128 + sample) / fs;
                const float value = float((block < 40 ? 0.126 : block < 80 ? 1.2 : 0.0)
                    * std::sin(6.283185307179586 * (ch ? 11003.0 : 997.0) * t));
                audio[0][ch][sample] = audio[1][ch][sample] = value;
            }
        for (int k = 0; k < 2; ++k)
        {
            float* data[] = { audio[k][0], audio[k][1] };
            channel::Telemetry* telemetry[] = { &meters[k][0], &meters[k][1] };
            chains[k].process(data, 128, nullptr, 0, controls, controls, globals, telemetry);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int sample = 0; sample < 128; ++sample)
            {
                const double a = audio[0][ch][sample], b = audio[1][ch][sample];
                energy += a * a;
                error += (b - a) * (b - a);
                if (!std::isfinite(a) || !std::isfinite(b)) ++failures;
            }
    }
    const double residual = 10.0 * std::log10(error / std::max(energy, 1e-30) + 1e-30);
    if (residual > -140.0) ++failures;
    std::printf("chain %.0f kHz quality %d: residual %.2f dB, %d failures\n", fs / 1000, int(quality), residual, failures);
    return failures;
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try
    {
        const std::string dir = argv[1];
        channel::NetlistSet nets;
        nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net");
        nets.tube = slurp(dir + "/tube.net"); nets.preout = slurp(dir + "/preout.net");
        nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net");
        nets.eq1 = slurp(dir + "/eq1.net"); nets.eq2 = slurp(dir + "/eq2.net");
        nets.eq3 = slurp(dir + "/eq3.net"); nets.eq4 = slurp(dir + "/eq4.net");
        nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");
        int failures = 0;
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 384000.0 })
            failures += compareStages(dir, fs, fs != 48000.0);
        for (double fs : { 48000.0, 96000.0 })
            for (auto q : { dsp::Quality::Default, dsp::Quality::HQ, dsp::Quality::Derp })
                failures += compareChain(nets, q, fs);
        return failures ? 1 : 0;
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
