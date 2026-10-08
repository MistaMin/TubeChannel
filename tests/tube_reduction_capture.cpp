// Exact linear-node elimination experiment. Production never defines TC_DETECTOR_EXPERIMENT.
#include "../Source/Channel/ChannelModel.h"
#include "DetectorReduction.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

static std::string text(const std::string& path)
{
    std::ifstream f(path); if (!f) throw std::runtime_error("missing netlist");
    std::stringstream s; s << f.rdbuf(); return s.str();
}
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    try
    {
        using namespace tubechannel;
        const std::string dir = argv[1];
        for (const auto* name : { "tube", "comp" })
        {
            std::vector<double> captures[2];
            for (int reduced = 0; reduced < 2; ++reduced)
            {
                circuit::Block b; circuit::ToleranceSettings tolerance; tolerance.enabled = false;
                const std::string input = std::string(name) == "tube" ? "O13B" : "J6T";
                b.build(text(dir + "/" + name + ".net"), 1.0 / 96000.0, tolerance,
                    { "VP", "VN", "V45", "TUBEP" }, { { input, 10.0 } });
                b.setRails(15, -15, 45, 150); b.setSupply(15, -15);
                b.c.tolStep = 3e-5;
                if (!b.dc()) throw std::runtime_error("DC failed");
                std::unique_ptr<DetectorReduction> solver;
                if (reduced)
                {
                    solver = std::make_unique<DetectorReduction>(b.c);
                    b.c.setExperimentalSolver(solver.get());
                    std::printf("%s: eliminated %d, retained %d of %d unknowns\n", name, solver->eliminated(), solver->retained(), b.c.size());
                }
                const int src = b.inputHandle(input), out = b.nodeId(std::string(name) == "tube" ? "V1OUT" : "COUT");
                captures[reduced].resize(96000);
                const auto start = std::chrono::steady_clock::now();
                for (int n = 0; n < 96000; ++n)
                {
                    b.setSourceH(src, .5 * std::sin(6.283185307179586 * 997 * n / 96000.0));
                    b.step(); captures[reduced][n] = b.nodeV(out);
                }
                std::printf("%s %s: %.3f ms\n", name, reduced ? "reduced" : "original",
                    1000 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            }
            double signal = 0, error = 0;
            for (int n = 0; n < 96000; ++n)
            { const double a = captures[0][n], d = captures[1][n] - a; signal += a*a; error += d*d; }
            std::printf("%s reduction residual %.2f dB relative RMS\n", name, 10 * std::log10(error / std::max(signal, 1e-30) + 1e-30));
        }
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
