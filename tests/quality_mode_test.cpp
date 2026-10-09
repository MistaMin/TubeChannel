// Check quality-selected channel output against independently forced backends.
#include "../Source/Plugin/Chain.h"
#include <cstdio>
#include <fstream>
#include <sstream>

static std::string slurp(const std::string& path)
{
    std::ifstream file(path); if (!file) throw std::runtime_error("missing netlist");
    std::stringstream text; text << file.rdbuf(); return text.str();
}
struct Capture { std::vector<float> samples; bool prototype = false, finite = true; int factor = 0; };
static Capture run(const tubechannel::channel::NetlistSet& nets, double fs,
                   tubechannel::dsp::Quality quality, tc::DetectorMode mode)
{
    tc::ChainConfig cfg; cfg.sampleRate = fs; cfg.maxBlock = 256;
    cfg.quality = quality; cfg.detectorMode = mode;
    tc::Chain chain; chain.prepare(cfg, nets);
    Capture result; result.factor = chain.factor(); result.prototype = chain.config().engine.compactDetector;
    tubechannel::channel::ChannelControls c; c.attack = c.release = 1.0;
    tc::ChainGlobals globals; globals.noise = false;
    std::vector<float> left(256), right(256); float* audio[] = { left.data(), right.data() };
    tubechannel::channel::Telemetry tel[2]; tubechannel::channel::Telemetry* tp[] = { &tel[0], &tel[1] };
    for (int b = 0; b < 120; ++b)
    {
        for (int i = 0; i < 256; ++i)
        {
            const double t = double(b * 256 + i) / fs;
            const double envelope = t >= .02 && t < .2 ? 1.0 : .15;
            left[i] = float(.8 * envelope * std::sin(6.283185307179586 * 220.0 * t));
            right[i] = float(.6 * envelope * std::sin(6.283185307179586 * 337.0 * t));
        }
        chain.process(audio, 256, nullptr, 0, c, c, globals, tp);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
            { result.samples.push_back(audio[ch][i]); result.finite &= std::isfinite(audio[ch][i]); }
    }
    return result;
}
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    tubechannel::channel::NetlistSet nets;
#define LOAD(stage) nets.stage = slurp(std::string(argv[1]) + "/" #stage ".net")
    LOAD(mic); LOAD(inst); LOAD(tube); LOAD(preout); LOAD(comp); LOAD(det);
    LOAD(eq1); LOAD(eq2); LOAD(eq3); LOAD(eq4); LOAD(out); LOAD(psu);
#undef LOAD
    using tubechannel::dsp::Quality;
    int failures = 0;
    for (double fs : { 48000.0, 96000.0 })
    {
        std::vector<float> prototypeSamples;
        for (auto quality : { Quality::Default, Quality::HQ, Quality::Derp })
        {
            const auto selected = run(nets, fs, quality, tc::DetectorMode::FollowQuality);
            const auto forced = run(nets, fs, quality == Quality::HQ ? Quality::Default : quality,
                quality == Quality::Default ? tc::DetectorMode::Prototype : tc::DetectorMode::Original);
            const int expectedFactor = fs == 48000.0 ? (quality == Quality::Derp ? 4 : 2) : (quality == Quality::Derp ? 2 : 1);
            const bool equal = selected.samples == forced.samples;
            if (!selected.finite || !forced.finite || !equal || selected.factor != expectedFactor
                || selected.prototype != (quality == Quality::Default)) ++failures;
            // Default and HQ must produce distinct compression responses despite equal factors.
            if (quality == Quality::Default) prototypeSamples = selected.samples;
            if (quality == Quality::HQ && selected.samples == prototypeSamples) ++failures;
            std::printf("%.0f Hz mode %d: %dx, %s detector, forced-backend waveform match %s\n", fs, int(quality),
                selected.factor, selected.prototype ? "prototype" : "original", equal ? "exact" : "FAILED");
        }
    }
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        // Empty netlists are intentional: Eco must not construct a circuit.
        const auto eco = run({}, fs, Quality::Eco, tc::DetectorMode::FollowQuality);
        if (!eco.finite || !eco.prototype || eco.factor != 1) ++failures;
        std::printf("%.0f Hz Eco: solver-free preparation, 1x, finite %s\n", fs, eco.finite ? "yes" : "FAILED");
    }
    std::printf("quality selection failures %d\n", failures); return failures ? 1 : 0;
}
