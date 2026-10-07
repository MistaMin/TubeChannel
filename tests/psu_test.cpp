// Power supply alone with fixed loads. Usage: psu_test <netlist dir> [vsec] [rsec] [mains Hz] [high 0/1]
#include "../Source/Channel/PowerSupply.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel::channel;

int main(int argc, char** argv)
{
    std::ifstream f(std::string(argv[1]) + "/psu.net");
    std::stringstream ss; ss << f.rdbuf();
    if (std::getenv("PSURATE")) PowerSupply::kRate = std::atof(std::getenv("PSURATE"));
    PowerSupplySettings s;
    if (argc > 2) s.vsec = std::atof(argv[2]);
    if (argc > 3) s.rsec = std::atof(argv[3]);
    if (argc > 4) s.mainsHz = std::atof(argv[4]);
    const bool high = argc > 5 ? std::atoi(argv[5]) != 0 : true;
    PowerSupply p;
    p.prepare(ss.str(), s, {});
    p.settle(1.0, high);
    // average over 100 ms and report ripple (peak-to-peak) of each rail
    const char* names[] = { "P25", "N25", "P15", "N15", "P75", "P45", "P150", "TUBEP" };
    double sum[8] = {}, lo[8], hi[8];
    for (int i = 0; i < 8; ++i) { lo[i] = 1e9; hi[i] = -1e9; }
    const int N = int(0.1 * PowerSupply::kRate);
    for (int n = 0; n < N; ++n)
    {
        p.step(0.075, 0.075, 0.012, 0.006, high);
        const double v[8] = { p.p25(), p.n25(), p.p15(), p.n15(), p.p75(), p.p45(), p.p150(), p.tubeP() };
        for (int i = 0; i < 8; ++i) { sum[i] += v[i]; lo[i] = std::min(lo[i], v[i]); hi[i] = std::max(hi[i], v[i]); }
    }
    for (int i = 0; i < 8; ++i)
        std::printf("  %-6s mean %8.3f V   ripple %7.1f mV pp\n", names[i], sum[i] / N, (hi[i] - lo[i]) * 1e3);
    std::printf("  heater current %.1f mA\n", p.heaterCurrent() * 1e3);
    return 0;
}
