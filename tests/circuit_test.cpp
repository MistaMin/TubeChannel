// Circuit engine checks against analytic results.
#include "../Source/Circuit/Circuit.h"

#include <cstdio>
#include <complex>

using namespace tubechannel::circuit;

static int failures = 0;
static void check(bool ok, const char* what, double value)
{
    if (!ok) { ++failures; std::printf("  FAIL %-58s %g\n", what, value); }
}

// Drive a circuit input source with a sine, return steady-state amplitude and phase at `node`.
struct Resp { double gain, phase; };
template <class Setup>
static Resp measure(Setup&& build, double f, double fs, int cycles = 40)
{
    Circuit c;
    int src = -1, out = -1;
    build(c, src, out);
    c.finalize(1.0 / fs);
    c.solveDC();
    const int N = int(std::ceil(fs / f * cycles));
    const double w = 2.0 * 3.14159265358979323846 * f / fs;
    double sc = 0, ss = 0, ic = 0, is = 0;
    for (int n = 0; n < N; ++n)
    {
        c.setSource(src, 0.1 * std::sin(w * double(n + 1)));
        c.step();
        if (n > N / 2)
        {
            const double y = c.v(out);
            sc += y * std::cos(w * double(n + 1)); ss += y * std::sin(w * double(n + 1));
            ic += 0; is += 0;
        }
    }
    const double cnt = double(N - N / 2 - 1);
    const double a = 2.0 * ss / cnt / 0.1, b = 2.0 * sc / cnt / 0.1;
    return { std::hypot(a, b), std::atan2(b, a) };
}

int main()
{
    // 1. RC low-pass: R = 1k, C = 100n (fc = 1.59 kHz) at 192 kHz, theta = 0.55.
    for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 20000.0 })
    {
        auto r = measure([](Circuit& c, int& src, int& out) {
            const int in = c.node("in"), o = c.node("out");
            src = c.addSource(in, 1e-3);
            c.addResistor(in, o, 1000.0);
            c.addCapacitor(o, 0, 100e-9);
            out = o;
        }, f, 192000.0);
        const double wrc = 2.0 * 3.14159265358979 * f * 1000.0 * 100e-9;
        const double ga = 1.0 / std::sqrt(1.0 + wrc * wrc);
        const double err = 20.0 * std::log10(r.gain / ga);
        std::printf("  RC  %6.0f Hz  gain %.5f (analytic %.5f)  error %+.4f dB\n", f, r.gain, ga, err);
        check(std::fabs(err) < 0.25, "RC low-pass amplitude error < 0.25 dB at 192 kHz", err);
    }

    // 2. Inverting op-amp, gain -10 (10k / 100k), NE5532-like.
    for (double f : { 1000.0, 20000.0 })
    {
        auto r = measure([](Circuit& c, int& src, int& out) {
            const int in = c.node("in"), inn = c.node("inn"), o = c.node("out");
            src = c.addSource(in, 1e-3);
            c.addResistor(in, inn, 10e3);
            c.addResistor(inn, o, 100e3);
            OpAmpModel m; m.GBW = 12e6; m.SR = 9e6;
            c.addOpAmp(0, inn, o, m);
            out = o;
        }, f, 192000.0);
        std::printf("  OPA %6.0f Hz  gain %.4f  phase %.2f deg\n", f, r.gain, r.phase * 57.29578);
        check(std::fabs(r.gain - 10.0) < 0.05, "inverting amp gain = 10", r.gain);
    }

    // 3. Diode clipper: 1k series, anti-parallel 1N4148-ish pair, 1 V peak in.
    {
        Circuit c;
        const int in = c.node("in"), o = c.node("out");
        const int s = c.addSource(in, 1e-3);
        c.addResistor(in, o, 1000.0);
        DiodeModel d; d.IS = 2.52e-9; d.N = 1.752;
        c.addDiode(o, 0, d);
        c.addDiode(0, o, d);
        c.finalize(1.0 / 192000.0);
        c.solveDC();
        double pk = 0.0;
        int maxIt = 0;
        for (int n = 0; n < 4000; ++n)
        {
            c.setSource(s, 1.0 * std::sin(2.0 * 3.14159265 * 1000.0 * n / 192000.0));
            c.step();
            pk = std::max(pk, std::fabs(c.v(o)));
            maxIt = std::max(maxIt, c.lastIterations());
        }
        std::printf("  diode clipper: peak %.3f V, max Newton iterations %d, factorisations %d\n", pk, maxIt, c.factorCount());
        check(pk > 0.5 && pk < 0.8, "diode clipper clips near 0.6 V", pk);
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
