// Cross-version audio checkpoint: this tool also builds against the 1.1.3 header API.
#include "Plugin/Chain.h"
#include <fstream>
#include <sstream>
#include <cstdio>

static std::string read(const std::string& path)
{
    std::ifstream f(path); if (!f) throw std::runtime_error("missing netlist");
    std::stringstream s; s << f.rdbuf(); return s.str();
}
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    using namespace tubechannel;
    channel::NetlistSet nets; const std::string dir = argv[1];
#define LOAD(stage) nets.stage = read(dir + "/" #stage ".net")
    LOAD(mic); LOAD(inst); LOAD(tube); LOAD(preout); LOAD(comp); LOAD(det);
    LOAD(eq1); LOAD(eq2); LOAD(eq3); LOAD(eq4); LOAD(out); LOAD(psu);
#undef LOAD
    std::ofstream out(argv[2], std::ios::binary); if (!out) return 2;
    for (double fs : { 48000.0, 96000.0 })
        for (auto quality : { dsp::Quality::Default, dsp::Quality::HQ, dsp::Quality::Derp })
        {
            tc::ChainConfig cfg; cfg.sampleRate = fs; cfg.maxBlock = 256; cfg.quality = quality;
            tc::Chain chain; chain.prepare(cfg, nets);
            tc::ChainGlobals globals; globals.noise = true;
            channel::ChannelControls controls;
            channel::Telemetry meters[2]; channel::Telemetry* tp[] = { &meters[0], &meters[1] };
            float left[256], right[256]; float* data[] = { left, right };
            for (int block = 0; block < 160; ++block)
            {
                controls.gainPlus20 = block >= 40 && block < 100;
                controls.plateHigh = !(block >= 60 && block < 100);
                controls.attack = controls.release = block > 80 ? 1.0 : .5;
                controls.autoDetect = block > 120;
                controls.eqActive = block % 40 < 20;
                for (int i = 0; i < 256; ++i)
                {
                    const double t = double(block * 256 + i) / fs;
                    const double amplitude = block > 140 ? 0.0 : block > 80 ? .8 : .126;
                    left[i] = float(amplitude * std::sin(6.283185307179586 * 997 * t));
                    right[i] = float(.7 * amplitude * std::sin(6.283185307179586 * 337 * t));
                }
                chain.process(data, 256, nullptr, 0, controls, controls, globals, tp);
                for (int ch = 0; ch < 2; ++ch)
                    out.write(reinterpret_cast<const char*>(data[ch]), sizeof(left));
            }
        }
    std::puts("captured 491520 float samples across Default/HQ/Derp at 48/96 kHz");
    return out ? 0 : 1;
}
