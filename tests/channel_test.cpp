// Full channel model checks: response, level, supply sag, timing. Usage: channel_test <netlist dir> [fs]
#include "../Source/Channel/ChannelModel.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel::channel;

static std::string slurp(const std::string& p)
{
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct Result { double amp, h2, h3, peakVal; };

static Result run(ChannelModel& m, double f, double vpk, double fs, double secs, double settleSecs)
{
    const int N = int(secs * fs), S = int(settleSecs * fs);
    double sc = 0, ss = 0, s2 = 0, c2 = 0, s3 = 0, c3 = 0, cnt = 0, pk = 0;
    for (int n = 0; n < N; ++n)
    {
        const double ph = 2.0 * 3.14159265358979 * f * double(n + 1) / fs;
        const double y = m.process(vpk * std::sin(ph), 0.0);
        if (n >= S)
        {
            sc += y * std::cos(ph); ss += y * std::sin(ph);
            s2 += y * std::sin(2 * ph); c2 += y * std::cos(2 * ph);
            s3 += y * std::sin(3 * ph); c3 += y * std::cos(3 * ph);
            pk = std::max(pk, std::fabs(y));
            cnt += 1;
        }
    }
    return { 2.0 * std::hypot(sc, ss) / cnt, 2.0 * std::hypot(s2, c2) / cnt, 2.0 * std::hypot(s3, c3) / cnt, pk };
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = argv[1];
    const double fs = argc > 2 ? std::atof(argv[2]) : 192000.0;
    NetlistSet nets;
    nets.mic = slurp(dir + "/mic.net"); nets.inst = slurp(dir + "/inst.net"); nets.tube = slurp(dir + "/tube.net");
    nets.preout = slurp(dir + "/preout.net"); nets.comp = slurp(dir + "/comp.net"); nets.det = slurp(dir + "/det.net"); nets.eq1 = slurp(dir + "/eq1.net");
    nets.eq2 = slurp(dir + "/eq2.net"); nets.eq3 = slurp(dir + "/eq3.net"); nets.eq4 = slurp(dir + "/eq4.net");
    nets.out = slurp(dir + "/out.net"); nets.psu = slurp(dir + "/psu.net");

    ChannelModel m;
    ChannelControls c;
    c.gain = 0.5; c.compOut = 0.7; c.threshold = 0.9; c.master = 0.8;
    if (std::getenv("THR")) c.threshold = std::atof(std::getenv("THR"));
    if (std::getenv("GAIN")) c.gain = std::atof(std::getenv("GAIN"));
    m.setControls(c);
    if (std::getenv("NONOISE")) m.setNoiseEnabled(false);
    const auto t0 = std::chrono::steady_clock::now();
    m.prepare(fs, nets, 1, false);
    if (std::getenv("FA")) for (auto* b : m.blocks) b->c.firstAccept = std::atof(std::getenv("FA"));
    if (std::getenv("JACTOL")) for (auto* b : m.blocks) b->c.jacTol = std::atof(std::getenv("JACTOL"));
    std::printf("prepare %.2f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());

    // warm-up so the slow bias networks and the supply settle
    run(m, 1000.0, 0.002, fs, 0.5, 0.5);
    Telemetry tel;
    std::printf("\nlevel sweep at 1 kHz (mic gain %.0f%%, compressor out of the way)\n", c.gain * 100);
    for (double vdb : { -70.0, -60.0, -50.0, -40.0, -30.0 })
    {
        const double vpk = 0.775 * 1.41421356 * std::pow(10.0, vdb / 20.0);
        auto r = run(m, 1000.0, vpk, fs, 0.25, 0.15);
        std::printf("  in %6.1f dBu  out %7.2f dBu  gain %6.2f dB   H2 %6.1f dB  H3 %6.1f dB\n", vdb,
                    20 * std::log10(r.amp / 1.41421356 / 0.775), 20 * std::log10(r.amp / vpk), 20 * std::log10(r.h2 / r.amp + 1e-12),
                    20 * std::log10(r.h3 / r.amp + 1e-12));
    }
    std::printf("\nfrequency response (-50 dBu in)\n");
    const double vpk = 0.775 * 1.41421356 * std::pow(10.0, -50.0 / 20.0);
    double ref = 0;
    for (double f : { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 12000.0, 16000.0, 20000.0 })
    {
        auto r = run(m, f, vpk, fs, std::max(0.12, 30.0 / f), std::max(0.06, 12.0 / f));
        if (f == 1000.0) ref = r.amp;
        std::printf("  %6.0f Hz  %7.2f dB re 1 kHz\n", f, ref > 0 ? 20 * std::log10(r.amp / ref) : 0.0);
    }
    m.publish(tel);
    std::printf("\nrails: +15 %.2f  -15 %.2f  +25 %.2f  +45 %.2f  +75 %.2f  +150 %.2f  plate %.2f  heater %.1f mA\n", tel.p15.load(), tel.n15.load(),
                tel.p25.load(), tel.p45.load(), tel.p75.load(), tel.p150.load(), tel.tubeP.load(), tel.heaterCurrent.load() * 1e3);

    // timing
    const int N = int(fs * 2.0);
#ifdef TC_PROFILE
    for (auto& t : m.profile) t = 0.0;
#endif
    const auto t1 = std::chrono::steady_clock::now();
    for (int n = 0; n < N; ++n)
        m.process((std::getenv("AMP") ? std::atof(std::getenv("AMP")) : 0.003) * std::sin(2.0 * 3.14159265358979 * 1000.0 * double(n) / fs), 0.0);
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::printf("\nspeed: %.2f us per oversampled sample = %.2fx realtime for one channel at %.0f kHz\n", el / N * 1e6, 2.0 / el, fs / 1000.0);
#ifdef TC_PROFILE
    {
        const char* names[] = { "mic", "instrument", "tube", "preout", "compressor audio", "detector", "opto + eq1", "eq2", "eq3", "eq4", "output", "controls + supply", "metering + loads" };
        double tot = 0;
        for (int i = 0; i < 13; ++i) tot += m.profile[i];
        for (int i = 0; i < 13; ++i)
            std::printf("  %-18s %6.2f us/sample  (%4.1f%%)\n", names[i], m.profile[i] / (N + 0.0) * 1e6, 100.0 * m.profile[i] / tot);
    }
#endif
    return 0;
}
