// Stage chain (one-way links) vs the reference. Usage: chain_test <netlist dir> <amp_peak> <seconds> <fs> <out.txt> [PARAM=v ...]
#include "../Source/Circuit/Block.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace tubechannel::circuit;

static std::string slurp(const std::string& p)
{
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv)
{
    if (argc < 6) { std::puts("usage"); return 2; }
    const std::string dir = argv[1];
    const double amp = std::atof(argv[2]), secs = std::atof(argv[3]), fs = std::atof(argv[4]);
    ToleranceSettings tol;
    std::map<std::string, double> userParams;
    for (int i = 6; i < argc; ++i)
    {
        std::string a = argv[i];
        const auto eq = a.find('=');
        userParams[a.substr(0, eq)] = parseNumber(a.substr(eq + 1));
    }
    const double dt = 1.0 / fs;
    const bool high = !userParams.count("HV") || userParams["HV"] > 0;

    Block mic, inst, tube, preout, comp, eq1, eq2, eq3, eq4, out;
    mic.build(slurp(dir + "/mic.net") + "RSP SP XH 75\nRSN SN XC 75\n", dt, tol, { "VP", "VN", "V45" }, { { "SP", 1e-3 }, { "SN", 1e-3 } });
    inst.build(slurp(dir + "/inst.net"), dt, tol, {}, { { "O1", 10.0 } });
    tube.build(slurp(dir + "/tube.net"), dt, tol, { "VN", "TUBEP" }, { { "O13B", 10.0 } });
    preout.build(slurp(dir + "/preout.net"), dt, tol, {}, { { "V1OUT", 10.0 } });
    comp.build(slurp(dir + "/comp.net"), dt, tol, { "VP", "VN", "V45" }, { { "J6T", 1.0 } });
    eq1.build(slurp(dir + "/eq1.net"), dt, tol, { "VP", "VN" }, { { "COUT", 10.0 } });
    eq2.build(slurp(dir + "/eq2.net"), dt, tol, {}, { { "X", 10.0 } });
    eq3.build(slurp(dir + "/eq3.net"), dt, tol, {}, { { "O10A", 10.0 } });
    eq4.build(slurp(dir + "/eq4.net"), dt, tol, {}, { { "O8A", 10.0 } });
    out.build(slurp(dir + "/out.net") + "RL OUTP OUTN 600\n", dt, tol, {}, { { "O8C", 10.0 }, { "EQIN", 1.0 } });
    Block* all[] = { &mic, &inst, &tube, &preout, &comp, &eq1, &eq2, &eq3, &eq4, &out };
    const char* names[] = { "mic", "inst", "tube", "preout", "comp", "eq1", "eq2", "eq3", "eq4", "out" };
    for (auto* b : all)
    {
        for (auto& [k, v] : userParams)
            b->setParam(k, v);
        b->applyParams();
        b->setRail("VP", 15.0); b->setRail("VN", -15.0); b->setRail("V45", 45.0);
        b->setRail("TUBEP", high ? 148.0 : 44.0);
    }

    auto links = [&]()
    {
        inst.setInput("O1", mic.opamp("A1A"));
        tube.setInput("O13B", inst.opamp("A13B"));
        preout.setInput("V1OUT", tube.node("V1OUT"));
        comp.setInput("J6T", preout.node("J6T"));
        eq1.setInput("COUT", comp.node("COUT"));
        eq2.setInput("X", eq1.opamp("A10B"));
        eq3.setInput("O10A", eq2.opamp("A10A"));
        eq4.setInput("O8A", eq3.opamp("A8A"));
        out.setInput("O8C", eq4.opamp("A8C"));
        out.setInput("EQIN", eq1.node("EQIN"));
    };

    // DC operating point, stage by stage
    for (int i = 0; i < 10; ++i)
    {
        links();
        const bool ok = all[i]->dc();
        std::printf("DC %-6s %s  (nodes %d, nnz %d, fill %d)\n", names[i], ok ? "ok " : "FAILED", all[i]->c.size(), all[i]->c.nnz(), all[i]->c.fill());
    }
    // a couple of passes so links that feed back settle
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < 10; ++i) { links(); all[i]->dc(); }

    const int N = int(secs * fs);
    std::ofstream o(argv[5]);
    double tAcc[10] = {}; double itAcc[10] = {};
    const auto t0 = std::chrono::steady_clock::now();
    int fail = 0;
    for (int n = 0; n < N; ++n)
    {
        const double t = double(n + 1) / fs;
        const double v = amp * std::sin(2.0 * 3.14159265358979 * 1000.0 * t);
        mic.setInput("SP", 0.5 * v);
        mic.setInput("SN", -0.5 * v);
        for (int i = 0; i < 10; ++i)
        {
            links();
            const auto a = std::chrono::steady_clock::now();
            if (!all[i]->step()) ++fail;
            tAcc[i] += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
            itAcc[i] += all[i]->c.lastIterations();
        }
        o << t << " " << out.node("OUTP") - out.node("OUTN") << " " << v << "\n";
    }
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ran %d steps in %.2f s = %.2f us per step for the whole channel (non-converged %d)\n", N, el, el / N * 1e6, fail);
    for (int i = 0; i < 10; ++i)
        std::printf("  %-6s %6.2f us/step  (avg solves %.2f, factorisations %.3f per step)\n", names[i], tAcc[i] / N * 1e6, itAcc[i] / N, double(all[i]->c.factorCount()) / N);
    return 0;
}
