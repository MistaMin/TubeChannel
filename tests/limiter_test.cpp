// RATIO knob popped: the circuit compressor is out and a brickwall limiter holds the compressor output at the
// THRESHOLD ceiling. Usage: limiter_test <netlist dir>
#include "../Source/Channel/ChannelModel.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel::channel;
static std::string slurp(const std::string& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static int failures = 0;
static void check(bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }

static double peakAtCompOut(const std::string& dir, bool limiter, double thr, double levelDbu)
{
    NetlistSet n;
    n.mic = slurp(dir + "/mic.net"); n.inst = slurp(dir + "/inst.net"); n.tube = slurp(dir + "/tube.net");
    n.preout = slurp(dir + "/preout.net"); n.comp = slurp(dir + "/comp.net"); n.det = slurp(dir + "/det.net");
    n.eq1 = slurp(dir + "/eq1.net"); n.eq2 = slurp(dir + "/eq2.net"); n.eq3 = slurp(dir + "/eq3.net");
    n.eq4 = slurp(dir + "/eq4.net"); n.out = slurp(dir + "/out.net"); n.psu = slurp(dir + "/psu.net");
    const double fs = 96000.0;
    ChannelModel m;
    ChannelControls c;
    c.threshold = thr; c.limiter = limiter; c.limiterRelease = 0.1;
    m.setControls(c);
    m.setNoiseEnabled(false);
    m.prepare(fs, n, 1, false);
    const double vin = 0.775 * 1.41421356 * std::pow(10.0, levelDbu / 20.0);
    double peak = 0.0;
    const int N = int(fs * 0.8);
    for (int i = 0; i < N; ++i)
    {
        m.process(vin * std::sin(2.0 * 3.14159265 * 1000.0 * double(i) / fs), 0.0);
        if (i > N / 2) peak = std::max(peak, std::fabs(m.compressorOutput()));
    }
    return peak;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::puts("usage: limiter_test <netlist dir>"); return 2; }
    const std::string dir = argv[1];
    const double thr = 0.25, ceiling = 1.0960 * std::pow(10.0, (-20.0 + 40.0 * thr) / 20.0);
    const double off = peakAtCompOut(dir, false, thr, -10.0);
    const double on = peakAtCompOut(dir, true, thr, -10.0);
    const double onQuiet = peakAtCompOut(dir, true, thr, -40.0);
    std::printf("  ceiling %.3f V peak; comp output peak: loud, limiter off %.3f V; loud, limiter on %.3f V; quiet, limiter on %.3f V\n", ceiling, off, on, onQuiet);
    check(on <= ceiling * 1.001, "the compressor output never exceeds the ceiling");
    check(on > ceiling * 0.9, "the limiter holds the level at the ceiling");
    check(off > ceiling * 1.2, "without the limiter the same signal is above the ceiling");
    check(onQuiet < ceiling * 0.5, "a signal below the ceiling is untouched");
    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
