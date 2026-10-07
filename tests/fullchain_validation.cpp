// Validation only: load the whole reference netlist as ONE circuit, drive it, dump the output.
// Usage: fullchain_validation <netlist.cir> <amp_peak_V> <seconds> <fs> <out.txt> [PARAM=value ...]
#include "../Source/Circuit/Netlist.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel::circuit;

static std::string extractUnit(const std::string& cir)
{
    std::istringstream in(cir);
    std::string line, out;
    bool inSub = false, inUnit = false, afterUnit = false;
    while (std::getline(in, line))
    {
        std::string low = line;
        for (auto& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (low.rfind(".subckt", 0) == 0)
        {
            inSub = true;
            inUnit = low.find("proch2") != std::string::npos;
            continue;
        }
        if (low.rfind(".ends", 0) == 0)
        {
            if (inUnit) afterUnit = true;
            inSub = inUnit = false;
            continue;
        }
        if (afterUnit) continue;
        if (inSub && !inUnit) continue;
        out += line + "\n";
    }
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 6) { std::puts("usage"); return 2; }
    std::ifstream f(argv[1]);
    std::stringstream ss; ss << f.rdbuf();
    const double amp = std::atof(argv[2]), secs = std::atof(argv[3]), fs = std::atof(argv[4]);

    Circuit c;
    Netlist nl;
    nl.load(c, extractUnit(ss.str()));
    for (int i = 6; i < argc; ++i)
    {
        std::string a = argv[i];
        const auto eq = a.find('=');
        nl.params[a.substr(0, eq)] = parseNumber(a.substr(eq + 1));
    }
    nl.update(c);

    auto src = [&](const char* n, double rs, double v) { return c.addSource(c.node(n), rs, v); };
    src("VP", 1e-3, 15.0); src("VN", 1e-3, -15.0); src("V45", 1e-3, 45.0);
    src("TUBEP", 1e-3, nl.params.count("HV") && nl.params["HV"] > 0 ? 148.0 : 44.0);
    const int sp = src("sp", 1e-3, 0.0), sn = src("sn", 1e-3, 0.0);
    c.addResistor(c.node("sp"), c.node("XH"), 75.0);
    c.addResistor(c.node("sn"), c.node("XC"), 75.0);
    c.addResistor(c.node("OUTP"), c.node("OUTN"), 600.0);

    c.finalize(1.0 / fs, 0.55);
    c.verbose = std::getenv("TC_VERBOSE") != nullptr;
    std::printf("nodes %d  nnz %d  fill %d  warnings %zu\n", c.size(), c.nnz(), c.fill(), nl.warnings.size());
    const auto t0 = std::chrono::steady_clock::now();
    const bool dc = c.solveDC();
    std::printf("DC solve %s (%.2f s)\n", dc ? "converged" : "DID NOT CONVERGE",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (const char* n : { "VB", "EH", "EC", "O1", "O13B", "K", "PA", "PB", "O2B", "V1OUT", "P3A", "K3B", "E19", "O9A", "O8C", "V3OUT" })
        std::printf("  DC %-6s %10.4f V\n", n, c.v(c.node(n)));

    const int N = int(secs * fs);
    std::ofstream out(argv[5]);
    const auto t1 = std::chrono::steady_clock::now();
    int maxIt = 0; long long itSum = 0; int fail = 0;
    for (int n = 0; n < N; ++n)
    {
        const double t = double(n + 1) / fs;
        const double v = amp * std::sin(2.0 * 3.14159265358979 * 1000.0 * t);
        c.setSource(sp, 0.5 * v);
        c.setSource(sn, -0.5 * v);
        if (!c.step()) ++fail;
        maxIt = std::max(maxIt, c.lastIterations());
        itSum += c.lastIterations();
        out << t << " " << c.v(c.node("OUTP")) - c.v(c.node("OUTN")) << " " << v << "\n";
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::printf("ran %d steps in %.2f s (%.1f us/step), max iterations %d, avg %.2f, factorisations %d, non-converged %d\n",
                N, el, el / N * 1e6, maxIt, double(itSum) / N, c.factorCount(), fail);
    return 0;
}
