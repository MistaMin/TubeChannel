#pragma once

// Nodal circuit engine: sparse MNA-style solver with Newton iteration for the nonlinear devices.
//
//   - passive elements (R, C, L), Thevenin voltage sources, current sources
//   - diode (with series resistance, reverse breakdown), BJT (transport model with Early effect and
//     base resistance), Koren triode with grid diode, op-amp macromodel (GBW, slew, swing, output
//     resistance)
//   - theta-method integration (theta slightly above 0.5 so stiff modes decay instead of ringing)
//   - the factorisation is reused while the device Jacobians stay within a tolerance, so a block that
//     stays in its linear region costs one sparse solve per sample
//
// Everything is nodal (no branch unknowns): voltage sources are Thevenin sources with a small series
// resistance, which keeps the matrix well behaved with static pivoting.

#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <map>
#ifdef TC_TUBE_EXPERIMENT
#include <memory>
#endif
#include <string>
#include <vector>

#include "SparseLU.h"
#ifdef TC_TUBE_EXPERIMENT
#include "TriodeLookup.h"
#endif

#ifdef TC_NEWTON_PROF
#include <chrono>
namespace tubechannel::circuit { inline double gProf[16]; inline long gProfN[16]; }
#define TCP_BEGIN auto tcp0_ = std::chrono::steady_clock::now();
#define TCP_MARK(i) { auto t_ = std::chrono::steady_clock::now(); gProf[i] += std::chrono::duration<double>(t_ - tcp0_).count(); ++gProfN[i]; tcp0_ = t_; }
#else
#define TCP_BEGIN
#define TCP_MARK(i)
#endif

namespace tubechannel::circuit
{
#ifdef TC_DETECTOR_EXPERIMENT
// Offline experiments only. Plugin targets never define TC_DETECTOR_EXPERIMENT.
struct ExperimentalLinearSolver
{
    virtual ~ExperimentalLinearSolver() = default;
    virtual bool factor(const std::vector<double>& values, bool baseChanged) = 0;
    virtual void solve(const double* rhs, double* result) = 0;
};
#endif
constexpr double kVt = 0.025852;     // thermal voltage at 300 K
constexpr double kGmin = 1e-12;

struct DiodeModel
{
    double IS = 1e-14, N = 1.0, RS = 0.0;
    double BV = 0.0, IBV = 1e-3;            // BV = 0: no breakdown
};

struct BjtModel
{
    bool pnp = false;
    double IS = 1e-14, BF = 100.0, BR = 1.0, VAF = 0.0, RB = 0.0, CJC = 0.0, CJE = 0.0;
};

struct TriodeModel
{
    double MU = 100, EX = 1.4, KG1 = 1060, KP = 600, KVB = 300;
    double CGK = 0, CGP = 0, CPK = 0, RG = 2000.0, IGS = 1e-9;
};

struct OpAmpModel
{
    double GBW = 10e6, SR = 9e6 /* V/s */, AOL = 1e5, VSW = 13.0, ROUT = 10.0, RIN = 1e6;
};

namespace detail
{
inline double expLim(double x) { return std::exp(std::min(x, 80.0)); }

// SPICE pn-junction voltage limiting.
inline double pnjlim(double vnew, double vold, double vt, double vcrit)
{
    if (vnew > vcrit && std::fabs(vnew - vold) > vt + vt)
    {
        if (vold > 0.0)
        {
            const double arg = 1.0 + (vnew - vold) / vt;
            vnew = arg > 0.0 ? vold + vt * std::log(arg) : vcrit;
        }
        else
            vnew = vt * std::log(vnew / vt);
    }
    return vnew;
}

// Smooth saturating limiter: identity below ~55 % of the limit, sharp knee (exponent 16, like a real
// op-amp output stage rather than a soft tanh), derivative returned too.
inline double softClip(double v, double lim, double& d)
{
    const double u = std::fabs(v) / lim;
    const double u2 = u * u, u4 = u2 * u2, u8 = u4 * u4, u16 = u8 * u8;
    if (u < 0.62)
    {
        // (1+t)^(-1/16) and (1+t)^(-17/16) series; t = u^16 < 0.0004
        d = 1.0 - 1.0625 * u16 + 1.2539 * u16 * u16;
        return v * (1.0 - 0.0625 * u16 + 0.0166 * u16 * u16);
    }
    const double den = 1.0 + u16;
    const double r = std::sqrt(std::sqrt(std::sqrt(std::sqrt(den))));
    d = 1.0 / (den * r);
    return v / r;
}
} // namespace detail

class CircuitPair;

class Circuit
{
    friend class CircuitPair;
public:
    static constexpr int kGround = 0;

    // ---- build -------------------------------------------------------------------------------------
    int node(const std::string& name)
    {
        if (name == "0" || name == "gnd" || name == "GND")
            return 0;
        auto it = nodeIds.find(name);
        if (it != nodeIds.end())
            return it->second;
        const int id = int(nodeNames.size()) + 1;
        nodeIds.emplace(name, id);
        nodeNames.push_back(name);
        return id;
    }

    int internalNode(const std::string& hint)
    {
        return node("#" + hint + std::to_string(nodeNames.size()));
    }

    int hasNode(const std::string& name) const { return nodeIds.count(name) ? nodeIds.at(name) : -1; }
    const std::string& nodeName(int id) const { return nodeNames[size_t(id) - 1]; }
    int nodeCount() const { return int(nodeNames.size()); }

    int addResistor(int a, int b, double R)
    {
        Res r;
        r.a = u(a);
        r.b = u(b);
        r.R = std::max(R, 1e-6);
        reg2(r.a, r.b, r.p);
        resistors.push_back(r);
        return int(resistors.size()) - 1;
    }

    int addCapacitor(int a, int b, double C)
    {
        Cap c;
        c.a = u(a);
        c.b = u(b);
        c.C = C;
        c.Cnom = C;
        reg2(c.a, c.b, c.p);
        caps.push_back(c);
        return int(caps.size()) - 1;
    }

    int addInductor(int a, int b, double L)
    {
        Ind l;
        l.a = u(a);
        l.b = u(b);
        l.L = L;
        reg2(l.a, l.b, l.p);
        inds.push_back(l);
        return int(inds.size()) - 1;
    }

    // Voltage source of value V behind series resistance Rs into node n.
    int addSource(int n, double Rs, double V = 0.0)
    {
        Src s;
        s.n = u(n);
        s.Rs = std::max(Rs, 1e-6);
        s.V = V;
        s.p = s.n >= 0 ? lu.add(s.n, s.n) : -1;
        sources.push_back(s);
        return int(sources.size()) - 1;
    }

    // Current I flows from node a, through the source, into node b.
    int addCurrentSource(int a, int b, double I = 0.0)
    {
        ISrc s;
        s.a = u(a);
        s.b = u(b);
        s.I = I;
        isources.push_back(s);
        return int(isources.size()) - 1;
    }

    int addDiode(int a, int k, const DiodeModel& m)
    {
        Dio d;
        d.m = m;
        d.k = u(k);
        int anode = u(a);
        if (m.RS > 0.0)
        {
            const int ai = u(internalNode("da"));
            addResistor(a, ai + 1, m.RS);
            anode = ai;
        }
        d.a = anode;
        reg2(d.a, d.k, d.p);
        d.vcrit = m.N * kVt * std::log(m.N * kVt / (std::sqrt(2.0) * m.IS));
        diodes.push_back(d);
        return int(diodes.size()) - 1;
    }

    int addBjt(int c, int b, int e, const BjtModel& m)
    {
        Bjt q;
        q.m = m;
        q.c = u(c);
        q.e = u(e);
        int bi = u(b);
        if (m.RB > 0.0)
        {
            const int n = u(internalNode("bi"));
            addResistor(b, n + 1, m.RB);
            bi = n;
        }
        q.b = bi;
        const int cc[3] = { q.c, q.b, q.e };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                q.p[i][j] = (cc[i] >= 0 && cc[j] >= 0) ? lu.add(cc[i], cc[j]) : -1;
        q.vcrit = kVt * std::log(kVt / (std::sqrt(2.0) * m.IS));
        if (m.CJC > 0.0) addCapacitor(bi + 1, c, m.CJC);
        if (m.CJE > 0.0) addCapacitor(bi + 1, e, m.CJE);
        bjts.push_back(q);
        return int(bjts.size()) - 1;
    }

    int addTriode(int p, int g, int k, const TriodeModel& m)
    {
        Tri t;
        t.m = m;
        const int ng = u(internalNode("tg"));
        addResistor(g, ng + 1, m.RG);
        t.p_ = u(p);
        t.g_ = ng;
        t.k_ = u(k);
        const int cc[3] = { t.p_, t.g_, t.k_ };
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                t.p[i][j] = (cc[i] >= 0 && cc[j] >= 0) ? lu.add(cc[i], cc[j]) : -1;
        DiodeModel gd;
        gd.IS = m.IGS;
        addDiode(ng + 1, k, gd);
        if (m.CGK > 0.0) addCapacitor(g, k, m.CGK);
        if (m.CGP > 0.0) addCapacitor(g, p, m.CGP);
        if (m.CPK > 0.0) addCapacitor(p, k, m.CPK);
        addResistor(p, k, 1e9);
        triodes.push_back(std::move(t));
        return int(triodes.size()) - 1;
    }

    int addOpAmp(int inp, int inn, int out, const OpAmpModel& m)
    {
        Opa o;
        o.m = m;
        o.inp = u(inp);
        o.inn = u(inn);
        o.out = u(out);
        o.n1 = u(internalNode("oa"));
        constexpr double CI = 1e-12;
        o.gm = 6.283185307179586 * m.GBW * CI;
        o.imax = m.SR * CI;
        o.rp = m.AOL / o.gm;
        o.ci = CI;
        o.vpos = o.vneg = m.VSW;
        reg2(o.inp, o.inn, o.pin);                       // input resistance
        // n1 row: diag, and coupling to inp / inn; out row: diag and coupling to n1
        o.pn1n1 = lu.add(o.n1, o.n1);
        o.pn1p = o.inp >= 0 ? lu.add(o.n1, o.inp) : -1;
        o.pn1n = o.inn >= 0 ? lu.add(o.n1, o.inn) : -1;
        o.poo = o.out >= 0 ? lu.add(o.out, o.out) : -1;
        o.pon1 = o.out >= 0 ? lu.add(o.out, o.n1) : -1;
        opamps.push_back(o);
        return int(opamps.size()) - 1;
    }

    // Finish the build: fix sparsity, allocate state, build the constant matrix.
    void finalize(double dt, double theta = 0.55, double warpHz = 14000.0)
    {
        h = dt;
        th = theta;
        {
            const double a = 3.14159265358979 * warpHz * dt;
            kappa = a > 1e-9 && a < 1.2 ? a / std::tan(a) : 1.0;
        }
        groundDiag.resize(nodeNames.size());
        for (int i = 0; i < int(nodeNames.size()); ++i)
            groundDiag[size_t(i)] = lu.add(i, i);
        lu.analyze();
        const int n = lu.size();
        const int nz = lu.nnz();
        baseVals.assign(size_t(nz), 0.0);
        vals.assign(size_t(nz), 0.0);
        factoredVals.assign(size_t(nz), 0.0);
        x.assign(size_t(n), 0.0);
        xPrev.assign(size_t(n), 0.0);
        xPrev2.assign(size_t(n), 0.0);
        xPrev3.assign(size_t(n), 0.0);
        xNew.assign(size_t(n), 0.0);
        rhs.assign(size_t(n), 0.0);
        rhsBase.assign(size_t(n), 0.0);
        unitRhs.assign(size_t(n), 0.0);
        inverseColumn.assign(size_t(n), 0.0);
        lrZ.assign(size_t(n), 0.0);
        prepareInverseStorage();
        for (auto& o : opamps) { o.vpos = o.m.VSW; o.vneg = o.m.VSW; }
        dirty = true;
        needFactor = true;
    }

    // ---- run-time control -----------------------------------------------------------------------------
#ifdef TC_TUBE_EXPERIMENT
    // Preparation only: no table construction or heap work in a device evaluation.
    void enableTriodeLookup(bool enabled)
    {
        if (enabled)
            for (auto& t : triodes)
                if (!t.lookup)
                {
                    t.lookup = std::make_unique<TriodeLookup>();
                    t.lookup->prepare(t.m.EX, t.m.KG1);
                }
        useTriodeLookup = enabled;
    }

#endif

    void setResistance(int h_, double R)
    {
        auto& r = resistors[size_t(h_)];
        R = std::max(R, 1e-6);
        if (R != r.R)
        {
            r.R = R;
            // low-rank resistor: only refactor when it has wandered far from the factored value
            if (h_ == lrHandle && lrG0 > 0.0 && !dirty && std::fabs(1.0 / R - lrG0) < 0.3 * lrG0)
                return;
            dirty = true;
        }
    }
    // Scale every capacitor (1 = nominal). Used to charge a power supply quickly from cold.
    void scaleCapacitors(double f)
    {
        for (auto& c : caps)
        {
            const double cn = c.Cnom * f;
            if (c.C > 0.0)
                c.iPrev *= cn / c.C;      // keep dv/dt consistent
            c.C = cn;
        }
        dirty = true;
    }
    void setTheta(double theta) { th = theta; dirty = true; }
    void setTimeStep(double dt, double warpHz = 14000.0)
    {
        h = dt;
        const double a = 3.14159265358979 * warpHz * dt;
        kappa = a > 1e-9 && a < 1.2 ? a / std::tan(a) : 1.0;
        dirty = true;
    }
    // One resistor whose value changes often (e.g. the vactrol): it is handled by a rank-1 correction
    // of the solve instead of refactoring the matrix each time it moves.
    void setLowRankResistor(int h_) { lrHandle = h_; lrDirty = true; }
    void setSource(int h_, double V) { sources[size_t(h_)].V = V; }
    void setCurrent(int h_, double I) { isources[size_t(h_)].I = I; }
    void setOpAmpRails(int h_, double vpos, double vneg)
    {
        auto& o = opamps[size_t(h_)];
        o.vpos = vpos;
        o.vneg = vneg;
    }

    double v(int nodeId) const { return nodeId <= 0 ? 0.0 : x[size_t(nodeId) - 1]; }
    double sourceCurrent(int h_) const        // current delivered into the node by the source
    {
        const auto& s = sources[size_t(h_)];
        return (s.V - (s.n >= 0 ? x[size_t(s.n)] : 0.0)) / s.Rs;
    }
    double resistorCurrent(int h_) const
    {
        const auto& r = resistors[size_t(h_)];
        return ((r.a >= 0 ? x[size_t(r.a)] : 0.0) - (r.b >= 0 ? x[size_t(r.b)] : 0.0)) / r.R;
    }
    double opampOut(int h_) const              // ideal (pre-ROUT) output voltage
    {
        const auto& o = opamps[size_t(h_)];
        const double v1 = o.n1 >= 0 ? x[size_t(o.n1)] : 0.0;
        double d;
        return o.clipOut(v1, d);
    }
    // Sum of the op-amps' output currents (class-AB supply draw estimate).
    double opampLoadCurrent() const
    {
        double sum = 0.0;
        for (const auto& o : opamps)
        {
            double d;
            const double ideal = o.clipOut(o.n1 >= 0 ? x[size_t(o.n1)] : 0.0, d);
            sum += std::fabs(ideal - (o.out >= 0 ? x[size_t(o.out)] : 0.0)) / o.m.ROUT;
        }
        return sum;
    }
    SparseLU& luRef() { return lu; }
    const SparseLU& luRef() const { return lu; }
    int lastIterations() const { return iters; }
#ifdef TC_DETECTOR_EXPERIMENT
    void setExperimentalSolver(ExperimentalLinearSolver* solver)
    {
        experimentalSolver = solver;
        experimentalBaseChanged = true;
        needFactor = true;
    }
    std::vector<bool> nonlinearNodes() const
    {
        std::vector<bool> nodes(size_t(size()), false);
        const auto mark = [&nodes](int n) { if (n >= 0) nodes[size_t(n)] = true; };
        for (const auto& d : diodes) { mark(d.a); mark(d.k); }
        for (const auto& q : bjts) { mark(q.c); mark(q.b); mark(q.e); }
        for (const auto& t : triodes) { mark(t.p_); mark(t.g_); mark(t.k_); }
        for (const auto& o : opamps) { mark(o.inp); mark(o.inn); mark(o.out); mark(o.n1); }
        return nodes;
    }
#endif
    int factorCount() const { return factors; }
    int size() const { return lu.size(); }
    double lastMinPivot() const { return lu.minPivot(); }
    int nnz() const { return lu.nnz(); }
    int fill() const { return lu.fillIn(); }
    long factorOps() const { return lu.factorOps(); }
    long solveOps() const { return lu.solveOps(); }
    bool lastConverged() const { return converged; }

    // DC operating point: capacitors open, inductors shorted. Convergence aid: the supplies are
    // ramped up with a heavy conductance to ground on every node, which is then removed in decades.
    bool staticDC(bool fromCurrent)
    {
        dcMode = true;
        std::vector<double> srcSave(sources.size());
        for (size_t i = 0; i < sources.size(); ++i)
            srcSave[i] = sources[i].V;
        if (!fromCurrent)
            std::fill(x.begin(), x.end(), 0.0);
        bool ok = true;
        auto run = [&](double f, double gm, double gs, int maxIt)
        {
            for (size_t i = 0; i < sources.size(); ++i)
                sources[i].V = srcSave[i] * f;
            gminExtra = gm;
            opScale = gs;
            dirty = true;
            ok = newton(maxIt, true);
            if (verbose)
            {
                int bad = -1;
                for (size_t i = 0; i < x.size(); ++i)
                    if (!std::isfinite(x[i])) { bad = int(i); break; }
                std::fprintf(stderr, "  DC stage f=%.2f gmin=%.0e opgain=%.0e: %s after %d iterations%s%s\n", f, gm, gs,
                             ok ? "converged" : "NOT converged", iters, bad >= 0 ? ", first NaN node " : "",
                             bad >= 0 ? nodeNames[size_t(bad)].c_str() : "");
            }
            return ok;
        };
        if (!fromCurrent)
        {
            // 1. supplies up, op-amps nearly dead, heavy damping
            for (double f : { 0.1, 0.25, 0.5, 0.75, 1.0 })
                run(f, 1e-3, 1e-4, 200);
            // 2. op-amp gain up in decades
            for (double gs : { 1e-3, 1e-2, 1e-1, 0.3, 1.0 })
                run(1.0, 1e-3, gs, 200);
            // 3. remove the damping conductance in half decades
            for (double g : { 3e-4, 1e-4, 3e-5, 1e-5, 3e-6, 1e-6, 1e-7, 1e-8, 1e-9, 1e-10, 1e-11 })
                run(1.0, g, 1.0, 200);
        }
        run(1.0, 0.0, 1.0, 400);
        for (size_t i = 0; i < sources.size(); ++i)
            sources[i].V = srcSave[i];
        gminExtra = 0.0;
        opScale = 1.0;
        dcMode = false;
        dirty = true;
        initHistories();
        xPrev = x;
        xPrev2 = x;
        xPrev3 = x;
        return ok;
    }

    // Operating point. First the static homotopy; if that does not settle, power the circuit up in
    // (pseudo) time with growing steps so its own dynamics select the right branch, then polish.
    bool solveDC()
    {
        if (staticDC(false))
            return true;
        const double h0 = h, th0 = th;
        std::vector<double> target(sources.size());
        for (size_t i = 0; i < sources.size(); ++i)
            target[i] = sources[i].V;
        std::fill(x.begin(), x.end(), 0.0);
        std::fill(xPrev.begin(), xPrev.end(), 0.0);
        std::fill(xPrev2.begin(), xPrev2.end(), 0.0);
        dcMode = false;
        th = 1.0;                                   // backward Euler: unconditionally damped
        initHistories();
        limitSteps = true;
        double hh = 2e-5;
        double elapsed = 0.0;
        const double rampTime = 2e-3;
        while (elapsed < 0.4)
        {
            h = hh;
            dirty = true;
            elapsed += hh;
            const double f = std::min(1.0, elapsed / rampTime);
            for (size_t i = 0; i < sources.size(); ++i)
                sources[i].V = target[i] * f;
            xPrev2 = xPrev;
            xPrev = x;
            newton(60, false);
            updateHistories();
            hh = std::min(hh * 1.12, 4e-3);
        }
        limitSteps = false;
        for (size_t i = 0; i < sources.size(); ++i)
            sources[i].V = target[i];
        h = h0;
        th = th0;
        dirty = true;
        const bool ok = staticDC(true);
        return ok;
    }

    // Start from a guessed state (e.g. a supply near its steady state) instead of the DC solution.
    void presetNodes(const std::map<std::string, double>& volts)
    {
        for (const auto& [name, v] : volts)
        {
            const int id = hasNode(name);
            if (id > 0)
                x[size_t(id) - 1] = v;
        }
        initHistories();
        xPrev = x;
        xPrev2 = x;
        xPrev3 = x;
    }

    // One time step. If Newton does not converge the step is retried with damping, then split in two.
    bool step()
    {
        rotateHistory();
        if (fastValid && !dirty && stepLinearFast())
        {
            converged = true;
            return true;
        }
        if (newton(maxIter, false) && finite())
        {
            converged = true;
            updateHistories();
            return true;
        }
        return recoverStep();
    }

    void rotateHistory()
    {
        xPrev3.swap(xPrev2);                 // xPrev3 <- xPrev2 <- xPrev <- x with one copy
        xPrev2.swap(xPrev);
        xPrev = x;
    }

    // The plain Newton iteration did not converge: retry with damping, then split the step in two.
    bool recoverStep()
    {
        ++retries;
        x = xPrev;
        limitSteps = true;
        bool ok = newton(100, false) && finite();
        limitSteps = false;
        if (!ok)
        {
            // two half steps (sources held constant)
            const double h0 = h;
            x = xPrev;
            h = 0.5 * h0;
            dirty = true;
            for (int k = 0; k < 2; ++k)
            {
                limitSteps = true;
                ok = newton(100, false) && finite();
                limitSteps = false;
                if (!ok)
                    x = xPrev;
                updateHistories();
                xPrev3 = xPrev2;
                xPrev2 = xPrev;
                xPrev = x;
            }
            h = h0;
            dirty = true;
            converged = ok;
            ++rescues;
            return ok;
        }
        converged = true;
        updateHistories();
        return true;
    }

    int rescueCount() const { return rescues; }
    int retryCount() const { return retries; }

private:
    // ---- element records --------------------------------------------------------------------------------
    struct Pair { int aa = -1, bb = -1, ab = -1, ba = -1; };
    struct Res { int a, b; double R; Pair p; };
    struct Cap { int a, b; double C; Pair p; double g = 0, vPrev = 0, iPrev = 0, ihist = 0; double Cnom = 0.0; };
    struct Ind { int a, b; double L; Pair p; double g = 0, iPrev = 0, vPrev = 0, ihist = 0; };
    struct Src { int n; double Rs, V; int p; };
    struct ISrc { int a, b; double I; };

    struct Dio
    {
        DiodeModel m;
        int a, k;
        Pair p;
        double vcrit, vlim = 0.0;
        double g = 0, i0 = 0;                 // current linearisation
        double gf = 0;                        // factored
    };

    struct Bjt
    {
        BjtModel m;
        int c, b, e;
        int p[3][3];                          // positions: rows/cols ordered c, b, e
        double vcrit, vbeLim = 0.0, vbcLim = 0.0;
        double Ic0 = 0.0, Ib0 = 0.0;
        double d[4] = {}, df[4] = {};            // dIc/dVbe, dIc/dVbc, dIb/dVbe, dIb/dVbc (current / factored)
        double sgn() const { return m.pnp ? -1.0 : 1.0; }
        // Jacobian of the three node currents (rows c, b, e) wrt node voltages (c, b, e)
        static void jac(const double* dd, double J[3][3])
        {
            J[0][0] = -dd[1];  J[0][1] = dd[0] + dd[1];  J[0][2] = -dd[0];
            J[1][0] = -dd[3];  J[1][1] = dd[2] + dd[3];  J[1][2] = -dd[2];
            for (int j = 0; j < 3; ++j)
                J[2][j] = -(J[0][j] + J[1][j]);
        }
    };

    struct Tri
    {
        TriodeModel m;
#ifdef TC_TUBE_EXPERIMENT
        std::unique_ptr<TriodeLookup> lookup;
#endif
        int p_, g_, k_;
        int p[3][3];                          // rows/cols ordered p, g, k
        double J[3][3] = {}, I0[3] = {};
        double Jf[3][3] = {};
    };

    struct Opa
    {
        OpAmpModel m;
        int inp, inn, out, n1;
        Pair pin;
        int pn1n1, pn1p, pn1n, poo, pon1;
        double gm, imax, rp, ci, vpos, vneg;
        // dynamic state / linearisation
        double gC = 0, v1Prev = 0, iCPrev = 0, ihist = 0;
        double gmEff = 0, igm0 = 0;           // gm linearisation (current into n1)
        double dOut = 0, out0 = 0;            // output buffer linearisation
        double gmEffF = 0, dOutF = 0;
        // input protection (anti-parallel diodes across the inputs) and anti-windup of the internal node
        double gclamp = 0, iclamp = 0, gclampF = 0, gwind = 0, iwind = 0, gwindF = 0;

        double clipOut(double v1, double& d) const
        {
            if (v1 >= 0.0)
                return detail::softClip(v1, vpos, d);
            return detail::softClip(v1, vneg, d);
        }
    };

    int u(int node) const { return node - 1; }       // unknown index, -1 for ground

    void reg2(int a, int b, Pair& p)
    {
        p.aa = a >= 0 ? lu.add(a, a) : -1;
        p.bb = b >= 0 ? lu.add(b, b) : -1;
        p.ab = (a >= 0 && b >= 0) ? lu.add(a, b) : -1;
        p.ba = (a >= 0 && b >= 0) ? lu.add(b, a) : -1;
    }

    static void stamp2(std::vector<double>& m, const Pair& p, double g)
    {
        if (p.aa >= 0) m[size_t(p.aa)] += g;
        if (p.bb >= 0) m[size_t(p.bb)] += g;
        if (p.ab >= 0) m[size_t(p.ab)] -= g;
        if (p.ba >= 0) m[size_t(p.ba)] -= g;
    }

    double xv(int idx) const { return idx >= 0 ? x[size_t(idx)] : 0.0; }
    static double pick(const std::vector<double>& vec, int idx) { return idx >= 0 ? vec[size_t(idx)] : 0.0; }

    void rebuildBase()
    {
#ifdef TC_DETECTOR_EXPERIMENT
        experimentalBaseChanged = true;
#endif
        std::fill(baseVals.begin(), baseVals.end(), 0.0);
        for (auto& r : resistors)
            stamp2(baseVals, r.p, 1.0 / r.R);
        for (auto& c : caps)
        {
            c.g = dcMode ? 0.0 : kappa * c.C / (th * h);
            stamp2(baseVals, c.p, c.g);
        }
        for (auto& l : inds)
        {
            l.g = dcMode ? 1e3 : th * h / (l.L * kappa);   // DC: short (1 mohm)
            stamp2(baseVals, l.p, l.g);
        }
        for (auto& s : sources)
            if (s.p >= 0)
                baseVals[size_t(s.p)] += 1.0 / s.Rs;
        for (auto& o : opamps)
        {
            if (o.pin.aa >= 0 || o.pin.bb >= 0)
                stamp2(baseVals, o.pin, 1.0 / o.m.RIN);
            o.gC = dcMode ? 0.0 : kappa * o.ci / (th * h);
            baseVals[size_t(o.pn1n1)] += 1.0 / o.rp + o.gC;
            if (o.poo >= 0)
                baseVals[size_t(o.poo)] += 1.0 / o.m.ROUT;
        }
        for (auto& d : diodes)
            if (d.p.aa >= 0 || d.p.bb >= 0)
                stamp2(baseVals, d.p, kGmin);
        for (int i = 0; i < lu.size(); ++i)
            baseVals[size_t(groundDiag[size_t(i)])] += kGmin + gminExtra;
        // the low-rank resistor is stamped at its present value: corrections are relative to that
        if (lrHandle >= 0)
            lrG0 = 1.0 / resistors[size_t(lrHandle)].R;
        dirty = false;
        fastValid = false;
    }

    void initHistories()
    {
        for (auto& c : caps)
        {
            c.vPrev = xv(c.a) - xv(c.b);
            c.iPrev = 0.0;
        }
        for (auto& l : inds)
        {
            l.vPrev = 0.0;
            l.iPrev = (xv(l.a) - xv(l.b)) * 1e3;
        }
        for (auto& o : opamps)
        {
            o.v1Prev = xv(o.n1);
            o.iCPrev = 0.0;
        }
    }

    void updateHistories()
    {
        const double k = (1.0 - th) / th;
        for (auto& c : caps)
        {
            const double vn = xv(c.a) - xv(c.b);
            c.iPrev = c.g * (vn - c.vPrev) - k * c.iPrev;
            c.vPrev = vn;
        }
        for (auto& l : inds)
        {
            const double vn = xv(l.a) - xv(l.b);
            l.iPrev = l.g * vn + l.ihist;
            l.vPrev = vn;
        }
        for (auto& o : opamps)
        {
            const double vn = xv(o.n1);
            o.iCPrev = o.gC * (vn - o.v1Prev) - k * o.iCPrev;
            o.v1Prev = vn;
        }
    }

    // Right-hand side terms that do not depend on the Newton iterate.
    void buildRhsBase()
    {
        std::fill(rhsBase.begin(), rhsBase.end(), 0.0);
        const double k = (1.0 - th) / th;
        for (auto& s : sources)
            if (s.n >= 0)
                rhsBase[size_t(s.n)] += s.V / s.Rs;
        for (auto& i : isources)
        {
            if (i.a >= 0) rhsBase[size_t(i.a)] -= i.I;
            if (i.b >= 0) rhsBase[size_t(i.b)] += i.I;
        }
        if (!dcMode)
        {
            for (auto& c : caps)
            {
                c.ihist = c.g * c.vPrev + k * c.iPrev;
                if (c.a >= 0) rhsBase[size_t(c.a)] += c.ihist;
                if (c.b >= 0) rhsBase[size_t(c.b)] -= c.ihist;
            }
            for (auto& l : inds)
            {
                l.ihist = l.iPrev + (1.0 - th) * h / (l.L * kappa) * l.vPrev;
                if (l.a >= 0) rhsBase[size_t(l.a)] -= l.ihist;
                if (l.b >= 0) rhsBase[size_t(l.b)] += l.ihist;
            }
            for (auto& o : opamps)
            {
                o.ihist = o.gC * o.v1Prev + k * o.iCPrev;
                rhsBase[size_t(o.n1)] += o.ihist;
            }
        }
    }

    // Evaluate every nonlinear device at the iterate x; fills J/I0 (current linearisation).
    void evaluateDevices(bool firstIter)
    {
        for (auto& d : diodes)
        {
            const double va = xv(d.a), vk = xv(d.k);
            double vd = va - vk;
            if (firstIter)
                d.vlim = vd;
            const double nvt = d.m.N * kVt;
            vd = detail::pnjlim(vd, d.vlim, nvt, d.vcrit);
            d.vlim = vd;
            const double e = detail::expLim(vd / nvt);
            double i = d.m.IS * (e - 1.0);
            double g = d.m.IS * e / nvt;
            if (d.m.BV > 0.0)
            {
                const double eb = detail::expLim(-(vd + d.m.BV) / kVt);
                i -= d.m.IBV * eb;
                g += d.m.IBV * eb / kVt;
            }
            d.g = g;
            d.i0 = i;
        }

        for (auto& q : bjts)
        {
            const double s = q.m.pnp ? -1.0 : 1.0;
            const double vc = xv(q.c), vb = xv(q.b), ve = xv(q.e);
            double vbe = s * (vb - ve), vbc = s * (vb - vc);
            if (firstIter)
            {
                q.vbeLim = vbe;
                q.vbcLim = vbc;
            }
            vbe = detail::pnjlim(vbe, q.vbeLim, kVt, q.vcrit);
            vbc = detail::pnjlim(vbc, q.vbcLim, kVt, q.vcrit);
            q.vbeLim = vbe;
            q.vbcLim = vbc;
            const double ef = detail::expLim(vbe / kVt);
            const double er = vbc < -1.0 ? 0.0 : detail::expLim(vbc / kVt);
            const double If = q.m.IS * (ef - 1.0), Ir = q.m.IS * (er - 1.0);
            const double gf = q.m.IS * ef / kVt, gr = q.m.IS * er / kVt;
            double qbi = 1.0, dqbi = 0.0;
            if (q.m.VAF > 0.0)
            {
                qbi = std::max(1.0 - vbc / q.m.VAF, 0.05);
                dqbi = -1.0 / q.m.VAF;
            }
            q.Ic0 = (If - Ir) * qbi - Ir / q.m.BR;
            q.Ib0 = If / q.m.BF + Ir / q.m.BR;
            q.d[0] = gf * qbi;
            q.d[1] = -gr * qbi + (If - Ir) * dqbi - gr / q.m.BR;
            q.d[2] = gf / q.m.BF;
            q.d[3] = gr / q.m.BR;
        }

#ifdef TC_TUBE_EXPERIMENT
        // Use the approximation only for routine, small-signal iterates. DC, overload and
        // recovery retain the original law on the same physical states, without a reset.
        const bool lookupActive = useTriodeLookup && !dcMode && !limitSteps && !tightJac && checkOpampsLinear();
#endif
        for (auto& t : triodes)
        {
            const double vp = xv(t.p_), vg = xv(t.g_), vk = xv(t.k_);
            const double vpk = vp - vk, vgk = vg - vk;
            double Ia = 0.0, dIa_dpk = 0.0, dIa_dgk = 0.0;
            if (vpk > 0.0)
            {
                const double S = std::sqrt(t.m.KVB + vpk * vpk);
                const double arg = t.m.KP * (1.0 / t.m.MU + vgk / S);
                double L, sig;
#ifdef TC_TUBE_EXPERIMENT
                if (!(lookupActive && t.lookup && t.lookup->softplus(arg, L, sig)))
                {
#endif
                if (arg > 30.0) { L = arg; sig = 1.0; }
                else if (arg < -30.0) { L = std::exp(arg); sig = L; }
                else { const double ea = std::exp(arg); L = std::log1p(ea); sig = ea / (1.0 + ea); }
#ifdef TC_TUBE_EXPERIMENT
                }
#endif
                const double E1 = vpk / t.m.KP * L;
                if (E1 > 0.0)
                {
                    const double dE_dpk = L / t.m.KP - vpk * vpk * vgk * sig / (S * S * S);
                    const double dE_dgk = vpk * sig / S;
                    double dIa_dE;
#ifdef TC_TUBE_EXPERIMENT
                    if (!(lookupActive && t.lookup && t.lookup->current(E1, Ia, dIa_dE)))
                    {
#endif
                        const double pw = std::pow(E1, t.m.EX - 1.0);
                        Ia = 2.0 * pw * E1 / t.m.KG1;
                        dIa_dE = 2.0 * t.m.EX * pw / t.m.KG1;
#ifdef TC_TUBE_EXPERIMENT
                    }
#endif
                    dIa_dpk = dIa_dE * dE_dpk;
                    dIa_dgk = dIa_dE * dE_dgk;
                }
            }
            // plate current flows p -> k. Rows p, g, k; columns p, g, k.
            t.I0[0] = Ia;
            t.I0[1] = 0.0;
            t.I0[2] = -Ia;
            t.J[0][0] = dIa_dpk;  t.J[0][1] = dIa_dgk;  t.J[0][2] = -dIa_dpk - dIa_dgk;
            for (int j = 0; j < 3; ++j) { t.J[1][j] = 0.0; t.J[2][j] = -t.J[0][j]; }
        }

        for (auto& o : opamps)
        {
            const double vd = xv(o.inp) - xv(o.inn);
            const double v1 = xv(o.n1);
            // input stage: Igm = clamp(gm*vd) into n1 (slew limit)
            double dl;
            const double gmUse = o.gm * opScale;
            const double sat = detail::softClip(gmUse * vd, o.imax, dl);
            o.igm0 = sat;
            o.gmEff = gmUse * dl;
            // buffer: out source = clip(v1) behind ROUT
            o.out0 = o.clipOut(v1, o.dOut);
            // input clamp: i = 2 Is sinh(vd / (n Vt)), the two protection diodes
            {
                constexpr double kIs = 2.5e-9, kNvt = 1.75 * kVt;
                const double a = std::max(-40.0, std::min(40.0, vd / kNvt));
                const double e = std::exp(a), ei = 1.0 / e;
                o.iclamp = kIs * (e - ei);
                o.gclamp = kIs * (e + ei) / kNvt + 1e-12;
            }
            // anti-windup: soft conduction from the internal node to ground beyond the swing limit
            {
                const double lim = (v1 >= 0.0 ? o.vpos : o.vneg) * 1.15;
                const double over = std::fabs(v1) - lim;
                constexpr double kWind = 2.5e-5;                 // A per volt, enough to cancel the slew current
                if (over > 0.0)
                {
                    o.iwind = (v1 >= 0.0 ? 1.0 : -1.0) * kWind * over;
                    o.gwind = kWind;
                }
                else
                {
                    o.iwind = 0.0;
                    o.gwind = 0.0;
                }
            }
        }
    }

    bool jacobianChanged()
    {
        const double jt = (dcMode || limitSteps || tightJac) ? 2e-3 : jacTol;     // tight while solving DC, rescuing a step or when the chord iteration is slow
        auto differs = [jt](double a, double b) { return std::fabs(a - b) > jt * std::fabs(b) + 1e-15; };
        for (auto& d : diodes) if (differs(d.g, d.gf)) return true;
        for (auto& q : bjts)
            for (int i = 0; i < 4; ++i)
                if (differs(q.d[i], q.df[i])) return true;
        for (auto& t : triodes)
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (differs(t.J[i][j], t.Jf[i][j])) return true;
        for (auto& o : opamps)
            if (differs(o.gmEff, o.gmEffF) || differs(o.dOut, o.dOutF) || differs(o.gclamp, o.gclampF) || o.gwind != o.gwindF) return true;
        return false;
    }

    void acceptJacobian()
    {
        for (auto& d : diodes) d.gf = d.g;
        for (auto& q : bjts)
            for (int i = 0; i < 4; ++i) q.df[i] = q.d[i];
        for (auto& t : triodes)
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) t.Jf[i][j] = t.J[i][j];
        for (auto& o : opamps) { o.gmEffF = o.gmEff; o.dOutF = o.dOut; o.gclampF = o.gclamp; o.gwindF = o.gwind; }
    }

    // Stamp the factored Jacobians into `m` (matrix values).
    void stampJacobians(std::vector<double>& m)
    {
        for (auto& d : diodes)
            stamp2(m, d.p, d.gf);
        for (auto& q : bjts)
        {
            double J[3][3];
            Bjt::jac(q.df, J);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (q.p[i][j] >= 0) m[size_t(q.p[i][j])] += J[i][j];
        }
        for (auto& t : triodes)
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (t.p[i][j] >= 0) m[size_t(t.p[i][j])] += t.Jf[i][j];
        for (auto& o : opamps)
        {
            if (o.pn1p >= 0) m[size_t(o.pn1p)] -= o.gmEffF;
            if (o.pn1n >= 0) m[size_t(o.pn1n)] += o.gmEffF;
            if (o.pon1 >= 0) m[size_t(o.pon1)] -= o.dOutF / o.m.ROUT;
            stamp2(m, o.pin, o.gclampF);
            m[size_t(o.pn1n1)] += o.gwindF;
        }
    }

    // rhs contributions of the nonlinear devices, consistent with the factored Jacobians.
    void addDeviceRhs()
    {
        for (auto& d : diodes)
        {
            const double ieq = d.i0 - d.gf * d.vlim;       // linearised about the (limited) junction voltage
            if (d.a >= 0) rhs[size_t(d.a)] -= ieq;
            if (d.k >= 0) rhs[size_t(d.k)] += ieq;
        }
        for (auto& q : bjts)
        {
            const double s = q.sgn();
            const double cc = q.Ic0 - q.df[0] * q.vbeLim - q.df[1] * q.vbcLim;
            const double cb = q.Ib0 - q.df[2] * q.vbeLim - q.df[3] * q.vbcLim;
            if (q.c >= 0) rhs[size_t(q.c)] -= s * cc;
            if (q.b >= 0) rhs[size_t(q.b)] -= s * cb;
            if (q.e >= 0) rhs[size_t(q.e)] += s * (cc + cb);
        }
        for (auto& t : triodes)
        {
            const int idx[3] = { t.p_, t.g_, t.k_ };
            const double xs[3] = { xv(t.p_), xv(t.g_), xv(t.k_) };
            for (int i = 0; i < 3; ++i)
            {
                if (idx[i] < 0) continue;
                double ieq = t.I0[i];
                for (int j = 0; j < 3; ++j) ieq -= t.Jf[i][j] * xs[j];
                rhs[size_t(idx[i])] -= ieq;
            }
        }
        for (auto& o : opamps)
        {
            // n1: current injected = igm0 + gmEffF * ((vp - vn) - vd0) -> rhs; matrix side already holds -gmEff terms
            const double vd = xv(o.inp) - xv(o.inn);
            rhs[size_t(o.n1)] += o.igm0 - o.gmEffF * vd;
            // out: source current out0/ROUT with derivative wrt v1
            if (o.out >= 0)
                rhs[size_t(o.out)] += (o.out0 - o.dOutF * xv(o.n1)) / o.m.ROUT;
            {
                const double ieq = o.iclamp - o.gclampF * vd;
                if (o.inp >= 0) rhs[size_t(o.inp)] -= ieq;
                if (o.inn >= 0) rhs[size_t(o.inn)] += ieq;
                rhs[size_t(o.n1)] -= o.iwind - o.gwindF * xv(o.n1);
            }
        }
    }

    bool hasNonlinear() const
    {
        return !diodes.empty() || !bjts.empty() || !triodes.empty() || (!opamps.empty() && !opampsLinear);
    }

    // True while every op-amp sits well inside its linear region (no clipping, no slew limiting):
    // then the whole block is linear and one solve per step is exact.
    bool checkOpampsLinear() const
    {
        // clamp and protection diodes count as open circuits while they are reverse biased by more than 0.2 V
        for (const auto& d : diodes)
        {
            const double vd = (d.a >= 0 ? x[size_t(d.a)] : 0.0) - (d.k >= 0 ? x[size_t(d.k)] : 0.0);
            if (d.m.BV > 0.0 || vd > -0.2)
                return false;
        }
        for (const auto& o : opamps)
        {
            const double v1 = o.n1 >= 0 ? x[size_t(o.n1)] : 0.0;
            const double vd = (o.inp >= 0 ? x[size_t(o.inp)] : 0.0) - (o.inn >= 0 ? x[size_t(o.inn)] : 0.0);
            const double lim = std::min(o.vpos, o.vneg);
            if (std::fabs(v1) > 0.55 * lim || std::fabs(o.gm * opScale * vd) > 0.55 * o.imax)
                return false;
        }
        return true;
    }

    void startGuess()
    {
        if (!dcMode && !limitSteps && extrapOrder == 2)
        {
            // quadratic extrapolation of the last three solutions as the starting point
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 3.0 * (xPrev[i] - xPrev2[i]) + xPrev3[i];
        }
        else if (!dcMode && !limitSteps && extrapOrder == 1)
        {
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 2.0 * xPrev[i] - xPrev2[i];
        }
        else if (!dcMode)
            x = xPrev;
    }

    bool onlyOpampsBlock() const { return bjts.empty() && triodes.empty() && !opamps.empty(); }

    // Limit the proposed Newton direction, not the signal or the device equations. Preventing
    // large forward-junction excursions avoids an expensive divergent solve at rectifier crossings.
    // Use one scale for the entire direction so the coupled linear equations remain consistent.
    double junctionUpdateScale() const
    {
        double alpha = 1.0;
        auto limit = [&alpha](double oldV, double newV, double vt, double critical) {
            const double limited = detail::pnjlim(newV, oldV, vt, critical);
            if (limited != newV && newV != oldV)
                alpha = std::min(alpha, std::max(0.0, (limited - oldV) / (newV - oldV)));
        };
        auto nextV = [this](int node) { return node >= 0 ? xNew[size_t(node)] : 0.0; };
        for (const auto& d : diodes)
        {
            const double oldV = xv(d.a) - xv(d.k), newV = nextV(d.a) - nextV(d.k);
            limit(oldV, newV, d.m.N * kVt, d.vcrit);
            if (d.m.BV > 0.0)
                limit(-oldV - d.m.BV, -newV - d.m.BV, kVt, kVt);
        }
        for (const auto& q : bjts)
        {
            const double sign = q.m.pnp ? -1.0 : 1.0;
            limit(sign * (xv(q.b) - xv(q.e)), sign * (nextV(q.b) - nextV(q.e)), kVt, q.vcrit);
            limit(sign * (xv(q.b) - xv(q.c)), sign * (nextV(q.b) - nextV(q.c)), kVt, q.vcrit);
        }
        for (const auto& o : opamps)
        {
            const double oldV = xv(o.inp) - xv(o.inn);
            const double newV = nextV(o.inp) - nextV(o.inn);
            // Both polarities of the input protection junctions; start guarding before steep conduction.
            limit(oldV, newV, 1.75 * kVt, 0.6);
            limit(-oldV, -newV, 1.75 * kVt, 0.6);
        }
        return alpha;
    }

    bool newton(int maxIt, bool dc)
    {
        TCP_BEGIN
        if (dirty)
        {
            rebuildBase();
            needFactor = true;
        }
        buildRhsBase();
        TCP_MARK(0)
        iters = 0;
        tightJac = false;

        // Fast path: a block made only of passives and op-amps that are all in their linear region is
        // exactly linear, so one back-substitution per step is the whole solve.
        const bool onlyOpamps = onlyOpampsBlock();
        if (onlyOpamps && !dc && !dcMode && !limitSteps)
        {
            if (checkOpampsLinear())
            {
                if (!opampsWereLinear)
                    needFactor = true;
                if (needFactor)
                {
                    for (auto& o : opamps)
                    {
                        o.gmEffF = o.gm * opScale;
                        o.dOutF = 1.0;
                    }
                    for (auto& d : diodes)
                        d.gf = 0.0;
                    vals = baseVals;
                    stampJacobians(vals);
                    lu.factor(vals.data());
                    ++factors;
                    needFactor = false;
                    buildInverse();
                }
                ++iters;
                // x = Minv * rhs, using only the columns where the right-hand side can be non-zero
                for (size_t j = 0; j < rhsIdx.size(); ++j)
                    rhsLocal[j] = rhsBase[size_t(rhsIdx[j])];
                applyInverse(rhsLocal.data(), x.data());
                if (checkOpampsLinear())
                {
                    opampsWereLinear = true;
                    return true;
                }
            }
            opampsWereLinear = false;
            needFactor = true;
        }
        opampsLinear = false;
        startGuess();

        for (int it = 0; it < maxIt; ++it)
        {
            ++iters;
            tightJac = tightJac || it >= 2;        // a third iteration means the stale Jacobian is not good enough
            TCP_MARK(1)
            evaluateDevices(it == 0);
            TCP_MARK(2)
            const bool jc_ = needFactor || jacobianChanged();
            TCP_MARK(7)
            if (jc_)
            {
                acceptJacobian();
                vals = baseVals;
                stampJacobians(vals);
#ifdef TC_DETECTOR_EXPERIMENT
                if (experimentalSolver)
                {
                    experimentalSolver->factor(vals, experimentalBaseChanged);
                    experimentalBaseChanged = false;
                }
                else
#endif
                lu.factor(vals.data());
                ++factors;
                needFactor = false;
                afterFactor();
            }
            TCP_MARK(3)
            std::memcpy(rhs.data(), rhsBase.data(), rhsBase.size() * sizeof(double));
            addDeviceRhs();
            TCP_MARK(4)
#ifdef TC_DETECTOR_EXPERIMENT
            if (experimentalSolver) experimentalSolver->solve(rhs.data(), xNew.data());
            else
#endif
            lu.solve(rhs.data(), xNew.data());
            applyLowRank(xNew.data());
            TCP_MARK(5)
            double worst = 0.0, big = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                if (guardJunctionUpdates && !std::isfinite(xNew[i]))
                    return false;
                const double d = xNew[i] - x[i];
                worst = std::max(worst, std::fabs(d) / (1.0 + 1e-3 * std::fabs(xNew[i])));
                big = std::max(big, std::fabs(xNew[i]));
            }
            if (traceNewton)
            {
                int wi = 0; double wd = 0.0;
                for (size_t i = 0; i < x.size(); ++i) { const double d = std::fabs(xNew[i] - x[i]); if (d > wd) { wd = d; wi = int(i); } }
                std::fprintf(stderr, "      it %3d worst %.3e at %-8s x=%.6g -> %.6g (limit %d)\n", it, worst, nodeNames[size_t(wi)].c_str(), x[size_t(wi)], xNew[size_t(wi)], limitSteps ? 1 : 0);
            }
            if (guardJunctionUpdates && !dcMode && !dc && !limitSteps)
            {
                const double alpha = junctionUpdateScale();
                if (alpha < 1.0)
                {
                    for (size_t i = 0; i < x.size(); ++i)
                        x[i] += alpha * (xNew[i] - x[i]);
                    continue; // A limited direction cannot satisfy the convergence test.
                }
            }
            if (!(big < 1e4)) // Reject an unsafe full step after allowing junction limiting to reduce it.
                return false;
            // DC / start-up / rescue: damped Newton. The whole update vector is scaled (keeping its
            // direction) so that no node moves more than dcStep volts in one iteration.
            if (dc || limitSteps)
            {
                double maxd = 0.0;
                for (size_t i = 0; i < x.size(); ++i)
                    maxd = std::max(maxd, std::fabs(xNew[i] - x[i]));
                const double a = maxd > dcStep ? dcStep / maxd : 1.0;
                for (size_t i = 0; i < x.size(); ++i)
                    x[i] += a * (xNew[i] - x[i]);
                if (a < 1.0)
                    continue;               // a damped step is never a converged one
            }
            else
                x.swap(xNew);
            TCP_MARK(6)
            // After one chord step the remaining error is about jacTol times the update, so a first update a few
            // times larger than the tolerance is already accurate to the tolerance.
            if (worst < (dc ? 1e-9 : (it == 0 && !limitSteps ? firstAccept * tolStep : tolStep)))
                return true;
        }
        return false;
    }

    // Called after every factorisation: remember the factored value of the low-rank resistor and the
    // response of the factored matrix to a unit conductance change across it.
    void afterFactor()
    {
        if (lrHandle < 0)
            return;
        const auto& r = resistors[size_t(lrHandle)];
        std::fill(unitRhs.begin(), unitRhs.end(), 0.0);
        if (r.a >= 0) unitRhs[size_t(r.a)] += 1.0;
        if (r.b >= 0) unitRhs[size_t(r.b)] -= 1.0;
        lu.solve(unitRhs.data(), lrZ.data());
        lrZu = (r.a >= 0 ? lrZ[size_t(r.a)] : 0.0) - (r.b >= 0 ? lrZ[size_t(r.b)] : 0.0);
    }

    // x = y - dg (u.y) / (1 + dg u.z) z   for the conductance change dg since the last factorisation
    void applyLowRank(double* y)
    {
        if (lrHandle < 0 || lrZ.empty())
            return;
        const auto& r = resistors[size_t(lrHandle)];
        const double dg = 1.0 / r.R - lrG0;
        if (dg == 0.0)
            return;
        const double uy = (r.a >= 0 ? y[r.a] : 0.0) - (r.b >= 0 ? y[r.b] : 0.0);
        const double k = dg * uy / (1.0 + dg * lrZu);
        const int n = lu.size();
        for (int i = 0; i < n; ++i)
            y[i] -= k * lrZ[size_t(i)];
    }

    // One step of a block that is exactly linear (passives + op-amps in their linear region): build the
    // few non-zero right-hand-side entries directly and multiply by the stored inverse.
    // the sparse right-hand side of a linear step, in the inverse's column order
    void buildFastRhs()
    {
        const size_t m = rhsIdx.size();
        double* rl = rhsLocal.data();
        for (size_t j = 0; j < m; ++j)
            rl[j] = 0.0;
        for (size_t k = 0; k < sources.size(); ++k)
            if (fmSrc[k] >= 0)
                rl[fmSrc[k]] += sources[k].V / sources[k].Rs;
        for (size_t k = 0; k < isources.size(); ++k)
        {
            if (fmIa[k] >= 0) rl[fmIa[k]] -= isources[k].I;
            if (fmIb[k] >= 0) rl[fmIb[k]] += isources[k].I;
        }
        const double kk = (1.0 - th) / th;
        for (size_t k = 0; k < caps.size(); ++k)
        {
            auto& c = caps[k];
            c.ihist = c.g * c.vPrev + kk * c.iPrev;
            if (fmCa[k] >= 0) rl[fmCa[k]] += c.ihist;
            if (fmCb[k] >= 0) rl[fmCb[k]] -= c.ihist;
        }
        for (size_t k = 0; k < inds.size(); ++k)
        {
            auto& l = inds[k];
            l.ihist = l.iPrev + (1.0 - th) * h / (l.L * kappa) * l.vPrev;
            if (fmLa[k] >= 0) rl[fmLa[k]] -= l.ihist;
            if (fmLb[k] >= 0) rl[fmLb[k]] += l.ihist;
        }
        for (size_t k = 0; k < opamps.size(); ++k)
        {
            auto& o = opamps[k];
            o.ihist = o.gC * o.v1Prev + kk * o.iCPrev;
            rl[fmOp[k]] += o.ihist;
        }
    }

    bool stepLinearFast()
    {
        buildFastRhs();
        applyInverse(rhsLocal.data(), x.data());
        if (!checkOpampsLinear())
        {
            x = xPrev;
            return false;
        }
        ++iters;
        updateHistories();
        return true;
    }

    // xo = Minv * r over the stored columns, four columns per pass so each output element is loaded and
    // stored once per four multiply-adds (the column count is padded to a multiple of four with zeros).
    void applyInverse(const double* r, double* xo) const
    {
        const int n = lu.size();
        const double* c0 = minvCols.data();
        for (int i = 0; i < n; ++i)
            xo[i] = 0.0;
        for (size_t j = 0; j < invCols; j += 4, c0 += 4 * size_t(n))
        {
            const double r0 = r[j], r1 = r[j + 1], r2 = r[j + 2], r3 = r[j + 3];
            const double* c1 = c0 + n;
            const double* c2 = c1 + n;
            const double* c3 = c2 + n;
            for (int i = 0; i < n; ++i)
                xo[i] += c0[i] * r0 + c1[i] * r1 + c2[i] * r2 + c3[i] * r3;
        }
    }

    // The circuit topology is fixed after finalize. Allocate and resolve the inverse layout once.
    void prepareInverseStorage()
    {
        const int n = lu.size();
        if (rhsIdx.empty())
        {
            std::vector<char> mark(static_cast<size_t>(n), 0);
            auto add = [&](int idx) { if (idx >= 0) mark[size_t(idx)] = 1; };
            for (auto& src : sources) add(src.n);
            for (auto& i : isources) { add(i.a); add(i.b); }
            for (auto& c : caps) { add(c.a); add(c.b); }
            for (auto& l : inds) { add(l.a); add(l.b); }
            for (auto& o : opamps) add(o.n1);
            for (int i = 0; i < n; ++i)
                if (mark[size_t(i)]) rhsIdx.push_back(i);
        }
        {
            std::vector<int> slot(static_cast<size_t>(n), -1);
            for (size_t j = 0; j < rhsIdx.size(); ++j)
                slot[size_t(rhsIdx[j])] = int(j);
            auto sl = [&](int idx) { return idx >= 0 ? slot[size_t(idx)] : -1; };
            fmSrc.clear(); fmIa.clear(); fmIb.clear(); fmCa.clear(); fmCb.clear(); fmLa.clear(); fmLb.clear(); fmOp.clear();
            for (auto& src : sources) fmSrc.push_back(sl(src.n));
            for (auto& i : isources) { fmIa.push_back(sl(i.a)); fmIb.push_back(sl(i.b)); }
            for (auto& c : caps) { fmCa.push_back(sl(c.a)); fmCb.push_back(sl(c.b)); }
            for (auto& l : inds) { fmLa.push_back(sl(l.a)); fmLb.push_back(sl(l.b)); }
            for (auto& o : opamps) fmOp.push_back(sl(o.n1));
            invCols = (rhsIdx.size() + 3) & ~size_t(3);
            rhsLocal.assign(invCols, 0.0);
        }
        minvCols.assign(invCols * size_t(n), 0.0);
    }

    // Dense inverse restricted to the columns where the right-hand side is non-zero.
    void buildInverse()
    {
        const int n = lu.size();
        std::fill(minvCols.begin(), minvCols.end(), 0.0);
        for (size_t j = 0; j < rhsIdx.size(); ++j)
        {
            std::fill(unitRhs.begin(), unitRhs.end(), 0.0);
            unitRhs[size_t(rhsIdx[j])] = 1.0;
            lu.solve(unitRhs.data(), inverseColumn.data());
            std::copy(inverseColumn.begin(), inverseColumn.end(), minvCols.begin() + long(j * size_t(n)));
        }
        fastValid = true;
        ++invVersion;
    }

    // ---- state --------------------------------------------------------------------------------------------
    SparseLU lu;
    std::map<std::string, int> nodeIds;
    std::vector<std::string> nodeNames;

    std::vector<Res> resistors;
    std::vector<Cap> caps;
    std::vector<Ind> inds;
    std::vector<Src> sources;
    std::vector<ISrc> isources;
    std::vector<Dio> diodes;
    std::vector<Bjt> bjts;
    std::vector<Tri> triodes;
    std::vector<Opa> opamps;

    double h = 1e-5, th = 0.55, kappa = 1.0, gminExtra = 0.0, dcStep = 1.5, opScale = 1.0;
    bool limitSteps = false;
public:
    int extrapOrder = 2;       // starting point of a step: 2 quadratic prediction, 1 linear, 0 the last solution (hard switching)
    bool tightJac = false;
    int maxIter = 12;          // plain Newton iterations before a step is retried with damping
    bool guardJunctionUpdates = false; // opt-in safeguard for strongly switching rectifiers
    double jacTol = 0.05;      // transient: refactor when a device's linearisation has moved by more than this fraction
    double firstAccept = 8.0;  // multiple of tolStep that a first-iteration update may reach and still be accepted
    double tolStep = 3e-6;     // accept a step when the update is below this (relative to 1 + |x|/1000)
private:
#ifdef TC_TUBE_EXPERIMENT
    bool useTriodeLookup = false;
#endif
    int rescues = 0, retries = 0;
    int lrHandle = -1;
    bool lrDirty = false;
    double lrG0 = 0.0, lrZu = 0.0;
    std::vector<double> lrZ, unitRhs, inverseColumn;
    std::vector<int> groundDiag, rhsIdx, fmSrc, fmIa, fmIb, fmCa, fmCb, fmLa, fmLb, fmOp;
    std::vector<double> minvCols, rhsLocal;
    size_t invCols = 0;
    unsigned long invVersion = 0;
    bool fastValid = false;
    bool opampsLinear = false;
    bool opampsWereLinear = false;
    bool finite() const
    {
        for (double v : x)
            if (!std::isfinite(v)) return false;
        return true;
    }
public:
    bool verbose = false, traceNewton = false;
private:
    bool dirty = true, needFactor = true, dcMode = false, converged = true;
    int iters = 0, factors = 0;
#ifdef TC_DETECTOR_EXPERIMENT
    ExperimentalLinearSolver* experimentalSolver = nullptr;
    bool experimentalBaseChanged = true;
#endif
    std::vector<double> baseVals, vals, factoredVals, x, xPrev, xPrev2, xPrev3, xNew, rhs, rhsBase;
};
} // namespace tubechannel::circuit
