#pragma once

// Two circuits of the same topology (the two channels of a stereo unit) advanced together. Everything that depends on
// the values of one channel (element values, devices, histories, convergence) stays scalar and is the Circuit's own
// code; only the heavy linear algebra is shared: the sparse LU factorisation, the triangular solves and the dense
// inverse of the linear stages run on two lanes at once. A step that is not routine (a matrix to rebuild, a Newton
// iteration that does not settle) is simply done by the two scalar circuits one after the other.

#include <cassert>

#include "Circuit.h"

namespace tubechannel::circuit
{
class CircuitPair
{
public:
    CircuitPair(Circuit& a, Circuit& b, bool tiledLinear = true) : c { &a, &b }, tiled(tiledLinear)
    {
        assert(a.lu.size() == b.lu.size() && a.lu.nnz() == b.lu.nnz());
        assert(a.rhsIdx == b.rhsIdx);
        for (const auto& [r, col] : a.lu.entryList())
            lu2.add(r, col);
        lu2.analyze();
        n = lu2.size();
        vals2.assign(size_t(lu2.nnz()), V2(0.0));
        rhs2.assign(size_t(n), V2(0.0));
        x2.assign(size_t(n), V2(0.0));
        minv2.assign(tiled ? ((size_t(n) + 3) & ~size_t(3)) * a.invCols : a.minvCols.size(), V2(0.0));
        packInverse();
    }

    bool step()
    {
        Circuit& a = *c[0];
        Circuit& b = *c[1];
        if (a.dirty || b.dirty)
            return scalarStep();
        const bool onlyOp = a.onlyOpampsBlock();
        if (onlyOp && !(a.fastValid && b.fastValid))
            return scalarStep();                                   // the inverse is being built
        a.rotateHistory();
        b.rotateHistory();
        if (onlyOp && linearStep())
        {
            a.converged = b.converged = true;
            return true;
        }
        return nonlinearStep(onlyOp);
    }

private:
    bool scalarStep()
    {
        pairNeedFactor = true;
        const bool oa = c[0]->step();
        const bool ob = c[1]->step();
        return oa && ob;
    }

    // ---- stages made only of passives and op-amps in their linear region: one product with the stored inverse ----
    bool linearStep()
    {
        Circuit& a = *c[0];
        Circuit& b = *c[1];
        a.buildFastRhs();
        b.buildFastRhs();
        packInverse();
        if (tiled)
            applyTiledInverse(a, b);
        else
            applyColumnInverse(a, b);
        if (!a.checkOpampsLinear() || !b.checkOpampsLinear())
        {
            a.x = a.xPrev;
            b.x = b.xPrev;
            return false;
        }
        ++a.iters;
        ++b.iters;
        a.updateHistories();
        b.updateHistories();
        return true;
    }

    // Keep four output nodes in registers through the complete product. Each four-term sum has
    // the same grouping as the column layout; all nodes and physical histories are still solved.
    template<int Rows>
    void applyInverseTile(const V2* m, int first, Circuit& a, Circuit& b)
    {
        V2 y[Rows];
        const double* ra = a.rhsLocal.data();
        const double* rb = b.rhsLocal.data();
        const size_t full = a.rhsIdx.size() & ~size_t(3);
        for (size_t j = 0; j < full; j += 4, m += 16)
        {
            const V2 r0(ra[j], rb[j]), r1(ra[j + 1], rb[j + 1]), r2(ra[j + 2], rb[j + 2]), r3(ra[j + 3], rb[j + 3]);
            for (int k = 0; k < Rows; ++k)
                y[k] += m[k] * r0 + m[4 + k] * r1 + m[8 + k] * r2 + m[12 + k] * r3;
        }
        // Padding columns are identically zero. Preserve the grouping of the live terms while
        // omitting those multiplications, and don't solve padded output rows in the last tile.
        const size_t tail = a.rhsIdx.size() - full;
        if (tail != 0)
        {
            const V2 r0(ra[full], rb[full]);
            const V2 r1 = tail > 1 ? V2(ra[full + 1], rb[full + 1]) : V2(0.0);
            const V2 r2 = tail > 2 ? V2(ra[full + 2], rb[full + 2]) : V2(0.0);
            for (int k = 0; k < Rows; ++k)
            {
                V2 term = m[k] * r0;
                if (tail > 1) term += m[4 + k] * r1;
                if (tail > 2) term += m[8 + k] * r2;
                y[k] += term;
            }
        }
        for (int k = 0; k < Rows; ++k)
        {
            a.x[size_t(first + k)] = y[k].lane0();
            b.x[size_t(first + k)] = y[k].lane1();
        }
    }

    void applyTiledInverse(Circuit& a, Circuit& b)
    {
        const V2* m = minv2.data();
        int i = 0;
        for (; i + 3 < n; i += 4, m += 4 * a.invCols)
            applyInverseTile<4>(m, i, a, b);
        switch (n - i)
        {
            case 1: applyInverseTile<1>(m, i, a, b); break;
            case 2: applyInverseTile<2>(m, i, a, b); break;
            case 3: applyInverseTile<3>(m, i, a, b); break;
            default: break;
        }
    }

    void applyColumnInverse(Circuit& a, Circuit& b)
    {
        const size_t cols = a.invCols;
        const V2* m = minv2.data();
        V2* xo = x2.data();
        for (int i = 0; i < n; ++i)
            xo[i] = V2(0.0);
        const double* ra = a.rhsLocal.data();
        const double* rb = b.rhsLocal.data();
        for (size_t j = 0; j < cols; j += 4, m += 4 * size_t(n))
        {
            const V2 r0(ra[j], rb[j]), r1(ra[j + 1], rb[j + 1]), r2(ra[j + 2], rb[j + 2]), r3(ra[j + 3], rb[j + 3]);
            const V2* c1 = m + n;
            const V2* c2 = c1 + n;
            const V2* c3 = c2 + n;
            for (int i = 0; i < n; ++i)
                xo[i] += m[i] * r0 + c1[i] * r1 + c2[i] * r2 + c3[i] * r3;
        }
        unpack(xo, a.x, b.x);
    }

    void packInverse()
    {
        Circuit& a = *c[0];
        Circuit& b = *c[1];
        if (!minv2.empty() && verA == a.invVersion && verB == b.invVersion)
            return;
        if (tiled)
        {
            size_t dst = 0;
            for (int i = 0; i < n; i += 4)
                for (size_t j = 0; j < a.invCols; ++j)
                    for (int k = 0; k < 4; ++k, ++dst)
                    {
                        const size_t src = j * size_t(n) + size_t(i + k);
                        minv2[dst] = i + k < n ? V2(a.minvCols[src], b.minvCols[src]) : V2(0.0);
                    }
        }
        else
            for (size_t i = 0; i < minv2.size(); ++i)
                minv2[i] = V2(a.minvCols[i], b.minvCols[i]);
        verA = a.invVersion;
        verB = b.invVersion;
    }

    // ---- stages with devices: chord Newton, both lanes iterated together ----
    bool nonlinearStep(bool onlyOp)
    {
        Circuit& a = *c[0];
        Circuit& b = *c[1];
        bool ok = newtonPair(onlyOp) && a.finite() && b.finite();
        if (ok)
        {
            a.converged = b.converged = true;
            a.updateHistories();
            b.updateHistories();
            return true;
        }
        // not routine: each channel recovers on its own (damping, then half steps)
        pairNeedFactor = true;
        a.x = a.xPrev;
        b.x = b.xPrev;
        const bool oa = a.recoverStep();
        const bool ob = b.recoverStep();
        return oa && ob;
    }

    bool newtonPair(bool onlyOp)
    {
        Circuit& a = *c[0];
        Circuit& b = *c[1];
        for (Circuit* k : c)
        {
            k->buildRhsBase();
            k->iters = 0;
            k->tightJac = false;
            k->opampsLinear = false;
            if (onlyOp)
                k->opampsWereLinear = false;
            k->startGuess();
        }
        if (onlyOp)
            pairNeedFactor = true;
        const int maxIt = a.maxIter;
        for (int it = 0; it < maxIt; ++it)
        {
            for (Circuit* k : c)
            {
                ++k->iters;
                k->tightJac = k->tightJac || it >= 2;
                k->evaluateDevices(it == 0);
            }
            if (pairNeedFactor || a.jacobianChanged() || b.jacobianChanged())
                factorPair();
            for (int l = 0; l < 2; ++l)
            {
                Circuit& k = *c[l];
                std::memcpy(k.rhs.data(), k.rhsBase.data(), k.rhsBase.size() * sizeof(double));
                k.addDeviceRhs();
            }
            for (int i = 0; i < n; ++i)
                rhs2[size_t(i)] = V2(a.rhs[size_t(i)], b.rhs[size_t(i)]);
            lu2.solve(rhs2.data(), x2.data());
            unpack(x2.data(), a.xNew, b.xNew);
            a.applyLowRank(a.xNew.data());
            b.applyLowRank(b.xNew.data());
            bool done = true;
            for (int l = 0; l < 2; ++l)
            {
                Circuit& k = *c[l];
                // converged when every node moved by less than tol * (1 + |x| / 1000)
                const double tol = it == 0 ? k.firstAccept * k.tolStep : k.tolStep;
                double big = 0.0;
                bool conv = true;
                const double* xn = k.xNew.data();
                const double* xo = k.x.data();
                for (size_t i = 0; i < k.x.size(); ++i)
                {
                    if (k.guardJunctionUpdates && !std::isfinite(xn[i]))
                        return false;
                    const double an = std::fabs(xn[i]);
                    if (!(std::fabs(xn[i] - xo[i]) < tol * (1.0 + 1e-3 * an)))
                        conv = false;
                    big = std::max(big, an);
                }
                if (k.guardJunctionUpdates)
                {
                    const double alpha = k.junctionUpdateScale();
                    if (alpha < 1.0)
                    {
                        for (size_t i = 0; i < k.x.size(); ++i)
                            k.x[i] += alpha * (k.xNew[i] - k.x[i]);
                        done = false;
                        continue;
                    }
                }
                if (!(big < 1e4))
                    return false;
                k.x.swap(k.xNew);
                if (!conv)
                    done = false;
            }
            if (done)
                return true;
        }
        return false;
    }

    void factorPair()
    {
        Circuit& a = *c[0];
        Circuit& b = *c[1];
        for (Circuit* k : c)
        {
            k->acceptJacobian();
            k->vals = k->baseVals;
            k->stampJacobians(k->vals);
            ++k->factors;
            k->needFactor = true;                // the scalar factorisation of this circuit is now out of date
        }
        for (size_t e = 0; e < vals2.size(); ++e)
            vals2[e] = V2(a.vals[e], b.vals[e]);
        lu2.factor(vals2.data());
        pairNeedFactor = false;
        // the response of the factored matrix to the low-rank resistor (the vactrol)
        if (a.lrHandle >= 0)
        {
            const auto& r = a.resistors[size_t(a.lrHandle)];
            for (int i = 0; i < n; ++i)
                rhs2[size_t(i)] = V2(0.0);
            if (r.a >= 0) rhs2[size_t(r.a)] += V2(1.0);
            if (r.b >= 0) rhs2[size_t(r.b)] -= V2(1.0);
            lu2.solve(rhs2.data(), x2.data());
            for (int l = 0; l < 2; ++l)
            {
                Circuit& k = *c[l];
                const auto& rl = k.resistors[size_t(k.lrHandle)];
                for (int i = 0; i < n; ++i)
                    k.lrZ[size_t(i)] = lane(x2[size_t(i)], l);
                k.lrZu = (rl.a >= 0 ? k.lrZ[size_t(rl.a)] : 0.0) - (rl.b >= 0 ? k.lrZ[size_t(rl.b)] : 0.0);
            }
        }
    }

    void unpack(const V2* v, std::vector<double>& a, std::vector<double>& b) const
    {
        for (int i = 0; i < n; ++i)
        {
            a[size_t(i)] = v[i].lane0();
            b[size_t(i)] = v[i].lane1();
        }
    }

    Circuit* c[2];
    SparseLU2 lu2;
    bool tiled = true;
    int n = 0;
    std::vector<V2> vals2, rhs2, x2, minv2;
    unsigned long verA = 0, verB = 0;
    bool pairNeedFactor = true;
};
} // namespace tubechannel::circuit
