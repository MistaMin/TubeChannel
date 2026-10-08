// Offline reference measurements for detector optimization; never used by the plugin.
#include "../Source/Circuit/Block.h"
#include "DetectorReduction.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

struct Sample
{
    double input, led, timing, positiveLoad, negativeLoad, opampLoad;
    int iterations, factors, retries, rescues;
    bool converged;
};

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 5)
    {
        std::puts("usage: detector_capture <netlist dir> [output.csv] [detector rate=48000] [reduced=0]");
        return 2;
    }
    try
    {
        const double rate = argc > 3 ? std::stod(argv[3]) : 48000.0;
        const bool useReduced = argc > 4 && std::stoi(argv[4]) != 0;
        if (!std::isfinite(rate) || rate < 8000 || rate > 192000)
            throw std::runtime_error("detector rate must be between 8000 and 192000");
        std::ifstream net(std::string(argv[1]) + "/det.net");
        if (!net) throw std::runtime_error("cannot read detector netlist");
        std::stringstream text;
        text << net.rdbuf();
        std::ofstream csv;
        if (argc > 2 && std::string(argv[2]) != "-")
        {
            csv.open(argv[2]);
            if (!csv) throw std::runtime_error("cannot write reference capture");
            csv << std::setprecision(17)
                << "case,sample,time_seconds,input_volts,led_amps,timing_volts,positive_load_amps,negative_load_amps,opamp_load_amps,iterations,factors,retries,rescues,converged\n";
        }
        const int count = int(std::ceil(1.5 * rate));
        int failures = 0;
        for (int scenario = 0; scenario < 7; ++scenario)
        {
            tubechannel::circuit::Block detector;
            tubechannel::circuit::ToleranceSettings tol;
            tol.enabled = scenario == 3;
            tol.seed = 12345;
            detector.build(text.str(), 1.0 / rate, tol, { "VP", "VN" }, { { "O6B", 10.0 } });
            detector.c.maxIter = 20;
            detector.c.extrapOrder = 0;
            detector.setParam("AUTO", scenario == 1 ? 1.0 : 0.0);
            const double attack = scenario == 2 ? 1e-3 : (scenario == 4 || scenario == 6 ? 50000.0 : 10000.0);
            const double release = scenario == 2 ? 0.0 : (scenario == 5 || scenario == 6 ? 1.0 : 0.5);
            detector.setParam("ATTR", attack);
            detector.setParam("RELW", release);
            std::printf("case %d settings: attack resistance %.6g, release fraction %.2f\n", scenario, attack, release);
            detector.applyParams();
            detector.setRails(15.0, -15.0, 45.0, 100.0);
            detector.setSupply(15.0, -15.0);
            if (!detector.dc()) throw std::runtime_error("detector initialization did not converge");
            std::unique_ptr<DetectorReduction> reduction;
            if (useReduced)
            {
                reduction = std::make_unique<DetectorReduction>(detector.c);
                detector.c.setExperimentalSolver(reduction.get());
                std::printf("reduction: %d linear nodes eliminated, %d retained, %d reduced matrix entries\n",
                            reduction->eliminated(), reduction->retained(), reduction->reducedEntries());
            }
            const int input = detector.inputHandle("O6B");
            const int led = detector.resistorHandle("RLED");
            const int timing = detector.nodeId("NC");
            std::printf("case %d: %d unknowns, %d matrix entries, %d fill entries\n",
                        scenario, detector.c.size(), detector.c.nnz(), detector.c.fill());
            std::vector<Sample> samples(static_cast<size_t>(count));
            const auto start = std::chrono::steady_clock::now();
            for (int n = 0; n < count; ++n)
            {
                const double t = n / rate;
                // Silence, quiet signal, loud burst, release, then two-tone excitation.
                double x = 0.0;
                if (t >= 0.1 && t < 0.3) x = 0.1 * std::sin(2.0 * 3.141592653589793 * 220.0 * t);
                else if (t >= 0.3 && t < 0.6) x = 8.0 * std::sin(2.0 * 3.141592653589793 * 220.0 * t);
                else if (t >= 1.0) x = 3.0 * (std::sin(2.0 * 3.141592653589793 * 73.0 * t)
                                            + std::sin(2.0 * 3.141592653589793 * 3101.0 * t));
                // One case exercises parameter and rail changes as well as tolerance variation.
                if (scenario == 3 && n == count / 2)
                {
                    detector.setParam("THRW", 0.2);
                    detector.setParam("RATR", 10000.0);
                    detector.applyParams();
                    detector.setRails(14.5, -14.5, 45.0, 100.0);
                    detector.setSupply(14.5, -14.5);
                }
                detector.setSourceH(input, x);
                const int factorsBefore = detector.c.factorCount();
                const int retriesBefore = detector.c.retryCount();
                const int rescuesBefore = detector.c.rescueCount();
                const bool ok = detector.step();
                samples[static_cast<size_t>(n)] = { x, detector.c.resistorCurrent(led), detector.nodeV(timing),
                    detector.currentVP(), detector.currentVN(), detector.supplyCurrent(),
                    detector.c.lastIterations(), detector.c.factorCount() - factorsBefore,
                    detector.c.retryCount() - retriesBefore, detector.c.rescueCount() - rescuesBefore, ok };
            }
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            double maxLed = 0.0;
            long totalIterations = 0, totalFactors = 0;
            int retries = 0, rescues = 0;
            int bad = 0;
            for (int n = 0; n < count; ++n)
            {
                const auto& s = samples[static_cast<size_t>(n)];
                if (!s.converged || !std::isfinite(s.led) || !std::isfinite(s.timing)
                    || !std::isfinite(s.positiveLoad) || !std::isfinite(s.negativeLoad) || !std::isfinite(s.opampLoad)) ++bad;
                maxLed = std::max(maxLed, s.led);
                totalIterations += s.iterations;
                totalFactors += s.factors;
                retries += s.retries;
                rescues += s.rescues;
                if (csv.is_open())
                    csv << scenario << ',' << n << ',' << n / rate << ',' << s.input << ',' << s.led << ','
                        << s.timing << ',' << s.positiveLoad << ',' << s.negativeLoad << ',' << s.opampLoad << ','
                        << s.iterations << ',' << s.factors << ',' << s.retries << ',' << s.rescues << ',' << s.converged << '\n';
            }
            std::printf("case %d: %d samples, %.3f us/sample, final-solve iterations %.2f/sample, factors %.2f/sample, max LED %.6g A, failures %d\n",
                        scenario, count, elapsed * 1e6 / count, double(totalIterations) / count,
                        double(totalFactors) / count, maxLed, bad);
            std::printf("  damping retries %d, half-step rescues %d\n", retries, rescues);
            failures += bad;
        }
        if (csv.is_open())
        {
            csv.flush();
            if (!csv) throw std::runtime_error("reference capture write failed");
        }
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
