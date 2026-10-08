#include "Hybrid/Detector.h"
#include "Hybrid/Equalizer.h"
#include "Hybrid/Output.h"
#include "Hybrid/Supply.h"

#include <cstdio>
#include <limits>

using namespace tubechannel::hybrid;
static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

int main()
{
    FilterBank<4> filter;
    FilterBank<4>::Profile fp;
    fp.count = 1;
    fp.sections[0] = { 0.25, 0.0, 0.0, -0.75, 0.0 };
    check(filter.configure(fp), "accept stable fitted section");
    double error = 0.0;
    for (int i = 0; i < 100; ++i)
        error = std::max(error, std::fabs(filter.process(i == 0 ? 1.0 : 0.0) - 0.25 * std::pow(0.75, i)));
    check(error < 1e-14, "impulse agrees with independent one-pole solution");
    auto bad = fp; bad.sections[0].a1 = -1.1;
    check(!filter.configure(bad), "reject unstable coefficients");
    bad = fp; bad.sections[0].b0 = std::numeric_limits<double>::quiet_NaN();
    check(!filter.configure(bad), "reject nonfinite coefficients");
    check(!Detector::Calibration { -1.0 }.valid(), "reject invalid detector calibration");

    Equalizer eq;
    Equalizer::Profile ep;
    ep.bands[0] = fp;
    check(eq.configure(ep), "accept measured EQ sections");
    auto taps = eq.process(1.0);
    check(taps.wet == 0.25 && taps.dry == 1.0, "retain separate filtered and bypass taps");
    for (int i = 0; i < 100; ++i) taps = eq.process(1.0);
    check(std::fabs(taps.wet - 1.0) < 1e-12, "wet filters continue settling while dry tap is selected");

    Output output;
    Output::Profile op;
    op.useTransfer = true;
    for (std::size_t i = 0; i < Output::kPoints; ++i)
    {
        op.volts[i] = -1.0 + 2.0 * double(i) / double(Output::kPoints - 1);
        op.result[i] = op.volts[i] * (op.volts[i] < 0.0 ? 0.5 : 0.8);
    }
    check(output.configure(op), "accept asymmetric measured transfer");
    check(std::fabs(output.process(-0.37) + 0.185) < 1e-14
          && std::fabs(output.process(0.37) - 0.296) < 1e-14, "interpolate both polarities without symmetry assumptions");
    check(output.process(-2.0) == -0.5 && output.process(2.0) == 0.8, "bound transfer outside captured range");
    auto invalidOutput = op; invalidOutput.volts[10] = invalidOutput.volts[9];
    check(!output.configure(invalidOutput), "reject ambiguous transfer coordinates");

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        Detector detector;
        Detector::Calibration dc;
        check(detector.prepare(rate, dc), "prepare detector at test rate");
        tubechannel::channel::ChannelControls c;
        c.attack = c.release = 1.0;
        detector.configure(c);
        check(detector.process(0.0) == 0.0, "silence does not light the optical element");
        // Compare timing behavior, then check passive discharge against the RC law.
        Detector fast;
        check(fast.prepare(rate, dc), "prepare short-attack comparison");
        auto shortControls = c; shortControls.attack = 0.0;
        fast.configure(shortControls);
        const double slowFirst = detector.process(4.0);
        const double fastFirst = fast.process(4.0);
        check(fastFirst > slowFirst, "short attack responds sooner to the same step");
        for (int i = 0; i < int(rate); ++i) detector.process(4.0);
        const double initialRelease = detector.process(0.0);
        const int dischargeSamples = int(rate * 0.1);
        for (int i = 0; i < dischargeSamples; ++i) detector.process(0.0);
        const double releaseOhms = dc.releaseBaseOhms + dc.releaseSpanOhms / dc.releaseFeedback;
        const double analyticDecay = std::exp(-0.1 / (dc.timingFarads * (releaseOhms + 2210.0)));
        check(initialRelease > 0.0 && std::fabs(detector.ledCurrent() / initialRelease - analyticDecay) < 1e-4,
              "unlit charging paths discharge with the passive RC time constant");
        Detector autoA, autoB;
        check(autoA.prepare(rate, dc) && autoB.prepare(rate, dc), "prepare Auto comparisons");
        auto a = c; a.autoDetect = true; a.attack = a.release = 0.0;
        auto b = a; b.attack = b.release = 1.0;
        autoA.configure(a); autoB.configure(b);
        bool autoEqual = true;
        for (int i = 0; i < int(rate * 0.2); ++i)
        {
            const double v = 4.0 * std::sin(i * 0.037);
            autoEqual &= autoA.process(v) == autoB.process(v);
        }
        check(autoEqual, "Auto timing is independent of disconnected manual controls");
        const double before = detector.ledCurrent();
        detector.configure(c);
        check(detector.ledCurrent() == before, "control update preserves the existing optical drive state");
        c.autoDetect = true; detector.configure(c);
        bool finite = true;
        for (int i = 0; i < int(rate); ++i)
        {
            const double led = detector.process(i < int(rate / 2) ? 100.0 * std::sin(i * 0.13) : 0.0);
            finite &= std::isfinite(led) && led >= 0.0 && led <= 0.04;
        }
        check(finite, "Auto and overloaded bursts remain bounded through recovery");
    }

    Supply supply;
    Supply::Snapshot settled { { 15.0, -15.0, 26.0, 45.0, 75.0, 150.0, 148.0 }, 0.0, 0.13 };
    Supply::Calibration sp;
    check(supply.prepare(8000.0, settled, true, sp), "initialize compact supply from settled circuit rails");
    auto loads = sp.restingLoads;
    for (int i = 0; i < 8000; ++i) supply.step(loads, true, 60.0, 1.0);
    check(supply.snapshot().volts == settled.volts, "resting loads preserve rail anchors exactly");
    loads[3] += 0.01;
    for (int i = 0; i < 2400; ++i) supply.step(loads, true, 60.0, 1.0);
    check(std::fabs(supply.snapshot().volts[Supply::Plate] - (148.0 - 5.0 * (1.0 - std::exp(-1.0)))) < 1e-10,
          "supply load response agrees with analytic recovery");
    for (int i = 0; i < 16000; ++i) supply.step(sp.restingLoads, false, 50.0, 1.0);
    check(std::fabs(supply.snapshot().volts[Supply::Plate] - 45.0) < 0.2, "plate selection recovers toward the low rail");
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
