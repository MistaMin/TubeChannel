// Matched tube-law comparisons through the complete channel; no waveform alignment or gain fitting.
#include "../Source/Plugin/Chain.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel;
static std::string read(const std::string& path)
{
    std::ifstream f(path); if (!f) throw std::runtime_error("missing netlist");
    std::stringstream s; s << f.rdbuf(); return s.str();
}
struct Capture { std::vector<float> audio, gr; bool finite = true; std::array<long, 11> retries{}, rescues{}; };
static Capture run(const channel::NetlistSet& nets, int fixture, bool lookup, const std::string& kind)
{
    tc::ChainConfig cfg; cfg.maxBlock = 256;
    cfg.sampleRate = fixture == 5 ? 44100 : fixture == 6 ? 96000 : fixture == 7 ? 192000 : 48000;
    cfg.channels = fixture == 7 || (kind != "validation" && kind != "sensitivity") ? 1 : 2;
    cfg.lockstep = fixture != 6;
    cfg.tolerances = (kind == "validation" || kind == "sensitivity") && fixture % 2 != 0;
    cfg.seed = 1 + fixture;
    cfg.tubeMode = lookup && kind != "sensitivity" ? tc::TubeMode::Prototype : tc::TubeMode::Original;
    // Hold detector selection fixed so this comparison isolates the tube-current change.
    cfg.detectorMode = tc::DetectorMode::Prototype;
    cfg.engine.guardedTubes = std::getenv("TUBE_GUARD") != nullptr;
    if (std::getenv("TUBE_EXTRAP0")) cfg.engine.tubeExtrapolation = 0;
    auto circuitNets = nets;
    if (kind == "sensitivity" && lookup)
    {
        for (auto* stage : { &circuitNets.tube, &circuitNets.comp })
            for (const auto& pair : { std::pair { std::string("KG1=825"), std::string("KG1=825.000000000825") },
                                     std::pair { std::string("KG1=424"), std::string("KG1=424.000000000424") } })
            {
                size_t position = 0;
                while ((position = stage->find(pair.first, position)) != std::string::npos)
                { stage->replace(position, pair.first.size(), pair.second); position += pair.second.size(); }
            }
    }
    tc::Chain chain; chain.prepare(cfg, circuitNets);
    channel::ChannelControls controls;
    controls.gainPlus20 = fixture == 1 || fixture == 6;
    controls.plateHigh = fixture != 1 && fixture != 6;
    controls.compActive = fixture != 0;
    controls.instrument = fixture == 2;
    controls.attack = controls.release = fixture == 3 ? 1.0 : .5;
    controls.autoDetect = fixture == 3;
    controls.externalSidechain = fixture == 3;
    tc::ChainGlobals globals;
    globals.noise = kind == "validation" && fixture == 5;
    globals.dualMono = fixture == 5;
    globals.psuModel = kind != "sweep";
    if (kind == "sweep") controls.compActive = false;
    channel::Telemetry meters[2]; channel::Telemetry* tp[] = { &meters[0], &meters[1] };
    float left[256], right[256], sc[256]; float* audio[] = { left, right }; const float* side[] = { sc };
    if (kind == "sweep")
    {
        // Precondition the selected plate/gain mode, then freeze that actual supply snapshot.
        globals.psuModel = true;
        for (int warm = 0; warm < int(2.0 * cfg.sampleRate); warm += 256)
        {
            std::fill_n(left, 256, 0.f); std::fill_n(right, 256, 0.f);
            chain.process(audio, 256, nullptr, 0, controls, controls, globals, tp);
        }
        globals.psuModel = false;
        std::printf("sweep fixture %d %s: preconditioned 2 s, frozen plate %.6f V\n",
            fixture, lookup ? "lookup" : "original", meters[0].tubeP.load());
    }
    const double duration = kind == "sweep" ? 3.0 : kind == "harmonic" ? 3.0 : fixture == 1 ? 6.0 : 1.0;
    const int samples = int(duration * cfg.sampleRate);
    Capture capture; capture.audio.reserve(size_t(samples * cfg.channels)); capture.gr.reserve(size_t((samples + 255) / 256));
    for (int start = 0; start < samples; start += 256)
    {
        const int count = std::min(256, samples - start);
        if ((kind == "validation" || kind == "sensitivity") && fixture == 4)
        {
            const int interval = start / 4096;
            controls.gainPlus20 = interval % 3 == 1;
            controls.plateHigh = interval % 2 == 0;
            controls.preOut = .6 + .4 * (interval % 2);
            controls.eqActive = interval % 2 == 0;
            controls.gain = .1 * (interval % 3);
        }
        for (int i = 0; i < count; ++i)
        {
            const double t = double(start + i) / cfg.sampleRate;
            double value = 0;
            if (kind == "sweep")
            {
                if (t >= .1 && t < 2.1)
                {
                    const double u = t - .1, scale = 2.0 / std::log(1000.0);
                    const double fade = std::clamp(std::min(u / .01, (2.0 - u) / .01), 0.0, 1.0);
                    value = (fixture == 2 ? .8 : .126) * fade * std::sin(6.283185307179586 * 20 * scale * std::expm1(u / scale));
                }
            }
            else if (kind == "harmonic")
            {
                constexpr double frequencies[] = { 100, 1000, 3000, 6000, 10000, 18000 };
                const int segment = std::min(5, int(t / .5));
                value = (fixture == 2 ? .8 : .126) * std::sin(6.283185307179586 * frequencies[segment] * t);
            }
            else
            {
                const double cycle = t - std::floor(t);
                const double envelope = std::clamp(std::min((cycle - .05) / .005, (.35 - cycle) / .005), 0.0, 1.0);
                const double level = fixture == 1 ? 1.2 : fixture ? .8 : .126;
                value = level * envelope * (.8 * std::sin(6.283185307179586 * 997 * t)
                    + .2 * std::sin(6.283185307179586 * 11003 * t));
            }
            left[i] = float(value);
            right[i] = float(.7 * value);
            sc[i] = float(.8 * std::sin(6.283185307179586 * 337 * t));
        }
        chain.process(audio, count, side, controls.externalSidechain ? 1 : 0, controls, controls, globals, tp);
        capture.gr.push_back(meters[0].grDb.load());
        for (int i = 0; i < count; ++i)
            for (int ch = 0; ch < cfg.channels; ++ch)
            { capture.finite &= std::isfinite(audio[ch][i]); capture.audio.push_back(audio[ch][i]); }
    }
    chain.collectSolverRecoveries(capture.retries.data(), capture.rescues.data());
    return capture;
}
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 4) return 2;
    try
    {
        const std::string dir = argv[1], kind = argc > 2 ? argv[2] : "validation";
        if (kind != "validation" && kind != "sweep" && kind != "harmonic" && kind != "sensitivity") return 2;
        channel::NetlistSet nets;
#define LOAD(stage) nets.stage = read(dir + "/" #stage ".net")
        LOAD(mic); LOAD(inst); LOAD(tube); LOAD(preout); LOAD(comp); LOAD(det);
        LOAD(eq1); LOAD(eq2); LOAD(eq3); LOAD(eq4); LOAD(out); LOAD(psu);
#undef LOAD
        int failures = 0;
        for (int fixture = 0; fixture < (kind == "validation" || kind == "sensitivity" ? 8 : 3); ++fixture)
        {
            const auto ref = run(nets, fixture, false, kind), candidate = run(nets, fixture, true, kind);
            double signal = 0, error = 0, maxError = 0, grError = 0;
            for (size_t i = 0; i < ref.audio.size(); ++i)
            {
                const double a = ref.audio[i], d = double(candidate.audio[i]) - a;
                signal += a*a; error += d*d; maxError = std::max(maxError, std::fabs(d));
            }
            for (size_t i = 0; i < ref.gr.size(); ++i)
                grError = std::max(grError, std::fabs(double(ref.gr[i]) - candidate.gr[i]));
            const double residual = 10 * std::log10(error / std::max(signal, 1e-30) + 1e-30);
            std::printf("%s fixture %d: residual %.3f dB, max audio error %.9g, max GR error %.6g dB, finite %s\n",
                kind.c_str(), fixture, residual, maxError, grError, ref.finite && candidate.finite ? "yes" : "NO");
            for (int stage : { 2, 4 })
                std::printf("  stage %d original retries/rescues %ld/%ld, candidate %ld/%ld\n", stage,
                    ref.retries[stage], ref.rescues[stage], candidate.retries[stage], candidate.rescues[stage]);
            if (!ref.finite || !candidate.finite || residual > -80 || maxError > 1e-4 || grError > .005) ++failures;
            if (argc > 3)
                for (int variant = 0; variant < 2; ++variant)
                {
                    const auto& data = variant ? candidate.audio : ref.audio;
                    std::ofstream f(std::string(argv[3]) + "/" + kind + "-" + std::to_string(fixture)
                        + (variant ? "-lookup.f32" : "-original.f32"), std::ios::binary);
                    f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size() * sizeof(float)));
                    if (!f) throw std::runtime_error("capture write failed");
                }
        }
        std::printf("tube comparison failures %d\n", failures); return failures ? 1 : 0;
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
