// Check safeguarded rectifier solves against the legacy equations and independent stereo solves.
#include "../Source/Circuit/Block.h"
#include "../Source/Circuit/CircuitPair.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

struct Error
{
    double power = 0.0, difference = 0.0;
    void add(double reference, double actual)
    {
        power += reference * reference;
        difference += (reference - actual) * (reference - actual);
    }
    double db() const { return 10.0 * std::log10(difference / std::max(power, 1e-30) + 1e-30); }
};

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    std::ifstream file(std::string(argv[1]) + "/det.net");
    if (!file) return 2;
    std::stringstream text;
    text << file.rdbuf();
    int failures = 0, legacyRetries = 0, guardedRetries = 0;
    std::array<int, 8> solveFailures{};
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    for (int scenario = 0; scenario < 8; ++scenario)
    {
        using namespace tubechannel::circuit;
        std::array<Block, 8> blocks;
        std::array<int, 8> inputs, leds, timing;
        for (size_t i = 0; i < blocks.size(); ++i)
        {
            auto& b = blocks[i];
            ToleranceSettings tol;
            tol.enabled = scenario % 4 >= 2;
            tol.seed = unsigned(12345 + i % 2);
            b.build(text.str(), 1.0 / rate, tol, { "VP", "VN" }, { { "O6B", 10.0 } });
            b.c.extrapOrder = 0;
            b.c.guardJunctionUpdates = i >= 2;
            b.c.maxIter = i >= 2 ? 50 : 20;
            if (i >= 6) { b.c.tolStep = 1e-9; b.c.firstAccept = 1.0; b.c.jacTol = 1e-5; }
            b.setParam("AUTO", scenario % 2);
            b.setParam("ATTR", scenario % 4 == 0 ? 1e-3 : 50000.0);
            b.setParam("RELW", scenario % 4 == 3 ? 1.0 : 0.154);
            b.setParam("THRW", scenario < 4 ? 0.2 : 0.5);
            b.setParam("RATR", scenario < 4 ? 1e-3 : 25000.0);
            b.applyParams();
            b.setRails(15.0, -15.0, 45.0, 100.0);
            b.setSupply(15.0, -15.0);
            if (!b.dc()) ++failures;
            inputs[i] = b.inputHandle("O6B");
            leds[i] = b.resistorHandle("RLED");
            timing[i] = b.nodeId("NC");
        }
        CircuitPair pair(blocks[4].c, blocks[5].c);
        Error ledError, timingError, pairedError, loadError, preciseLed, preciseTiming, preciseLoad;
        const int count = int(rate * 0.8);
        for (int n = 0; n < count; ++n)
        {
            const double t = n / rate;
            // Abrupt bursts, two unrelated channels, silence and changes to controls/supplies.
            const double envelope = t < 0.05 || (t >= 0.35 && t < 0.5) ? 0.0 : (t < 0.2 ? 0.1 : (scenario < 4 ? 8.0 : 1.0));
            for (size_t i = 0; i < blocks.size(); ++i)
            {
                if (n == count / 2)
                {
                    blocks[i].setParam("RELW", scenario % 2 ? 0.01 : 1.0);
                    blocks[i].setParam("THRW", 0.35);
                    blocks[i].applyParams();
                    blocks[i].setRails(14.5, -14.5, 45.0, 100.0);
                    blocks[i].setSupply(14.5, -14.5);
                }
                const double f = i % 2 ? 337.0 : 220.0;
                const double input = envelope * (std::sin(6.283185307179586 * f * t)
                    + 0.3 * std::sin(6.283185307179586 * 3101.0 * t));
                blocks[i].setSourceH(inputs[i], input);
                if ((i < 4 || i >= 6) && !blocks[i].step()) { if (i >= 2 || scenario >= 4) ++failures; ++solveFailures[i]; }
            }
            if (!pair.step()) { ++failures; ++solveFailures[4]; }
            for (size_t lane = 0; lane < 2; ++lane)
            {
                const double referenceLed = blocks[lane].c.resistorCurrent(leds[lane]);
                const double guardedLed = blocks[lane + 2].c.resistorCurrent(leds[lane + 2]);
                const double pairedLed = blocks[lane + 4].c.resistorCurrent(leds[lane + 4]);
                const double referenceTiming = blocks[lane].nodeV(timing[lane]);
                const double guardedTiming = blocks[lane + 2].nodeV(timing[lane + 2]);
                const double pairedTiming = blocks[lane + 4].nodeV(timing[lane + 4]);
                if (!std::isfinite(referenceLed) || !std::isfinite(guardedLed) || !std::isfinite(pairedLed)
                    || !std::isfinite(referenceTiming) || !std::isfinite(guardedTiming) || !std::isfinite(pairedTiming)) ++failures;
                ledError.add(referenceLed, guardedLed);
                timingError.add(referenceTiming, guardedTiming);
                pairedError.add(guardedLed, pairedLed);
                pairedError.add(guardedTiming, pairedTiming);
                loadError.add(blocks[lane].supplyCurrent(), blocks[lane + 2].supplyCurrent());
                preciseLed.add(blocks[lane + 6].c.resistorCurrent(leds[lane + 6]), guardedLed);
                preciseTiming.add(blocks[lane + 6].nodeV(timing[lane + 6]), guardedTiming);
                preciseLoad.add(blocks[lane + 6].supplyCurrent(), blocks[lane + 2].supplyCurrent());
            }
        }
        std::printf("%.0f Hz case %d: LED %.1f dB, timing %.1f dB, load %.1f dB, paired %.1f dB\n",
                    rate, scenario, ledError.db(), timingError.db(), loadError.db(), pairedError.db());
        // This is numerical equivalence to the existing stopping tolerance, not bit identity.
        // Hard overload exposes failures in the legacy solver, so its result is not an accuracy reference.
        // Require successful guarded solves and agreement between guarded scalar and paired paths.
        // Check accuracy against solves with the same equations and much tighter stopping tolerances.
        if (!(pairedError.db() < -125)) ++failures;
        std::printf("  vs tighter solves: LED %.1f dB, timing %.1f dB, load %.1f dB\n", preciseLed.db(), preciseTiming.db(), preciseLoad.db());
        if (!(preciseLed.db() < -125) || !(preciseTiming.db() < -125) || !(preciseLoad.db() < -125)) ++failures;
        for (size_t lane = 0; lane < 2; ++lane)
        {
            legacyRetries += blocks[lane].c.retryCount();
            guardedRetries += blocks[lane + 2].c.retryCount();
        }
    }
    std::printf("retries: legacy %d, guarded %d; failures %d\n", legacyRetries, guardedRetries, failures);
    std::printf("unsuccessful solves: legacy L/R %d/%d, guarded L/R %d/%d, pair %d, tighter L/R %d/%d\n",
                solveFailures[0], solveFailures[1], solveFailures[2], solveFailures[3], solveFailures[4], solveFailures[6], solveFailures[7]);
    if (guardedRetries >= legacyRetries) ++failures;
    return failures ? 1 : 0;
}
