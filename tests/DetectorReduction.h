#pragma once

#include "../Source/Circuit/Circuit.h"
#include <stdexcept>

// Algebraic elimination of the linear-only nodes. Kept outside production targets.
class DetectorReduction : public tubechannel::circuit::ExperimentalLinearSolver
{
    using LU = tubechannel::circuit::SparseLU;
    struct Entry { int source, row, col, slot; };
    struct Term { int row, col; double value; };
    LU linear, reduced;
    std::vector<int> eNodes, rNodes;
    std::vector<Entry> ee, er, re, rr;
    std::vector<Term> reTerms, wTerms;
    std::vector<double> linearValues, reducedValues;
    std::vector<double> couplingER, couplingRE, w, correction, bE, y, bR, xR;
    bool ready = false;
    int ne = 0, nr = 0;
public:
    explicit DetectorReduction(const tubechannel::circuit::Circuit& circuit)
    {
        const auto nonlinear = circuit.nonlinearNodes();
        std::vector<int> ei(nonlinear.size(), -1), ri(nonlinear.size(), -1);
        for (int i = 0; i < int(nonlinear.size()); ++i)
            if (nonlinear[size_t(i)]) { ri[size_t(i)] = int(rNodes.size()); rNodes.push_back(i); }
            else { ei[size_t(i)] = int(eNodes.size()); eNodes.push_back(i); }
        ne = int(eNodes.size()); nr = int(rNodes.size());
        if (!ne || !nr) throw std::runtime_error("no useful linear/nonlinear partition");
        std::vector<std::vector<bool>> connected(size_t(ne), std::vector<bool>(size_t(ne), false));
        for (int i = 0; i < ne; ++i) connected[size_t(i)][size_t(i)] = true;
        const auto& entries = circuit.luRef().entryList();
        for (int s = 0; s < int(entries.size()); ++s)
        {
            const auto [a, b] = entries[size_t(s)];
            const int ea = ei[size_t(a)], eb = ei[size_t(b)], ra = ri[size_t(a)], rb = ri[size_t(b)];
            if (ea >= 0 && eb >= 0)
            {
                ee.push_back({s, ea, eb, linear.add(ea, eb)});
                connected[size_t(ea)][size_t(eb)] = connected[size_t(eb)][size_t(ea)] = true;
            }
            else if (ea >= 0) er.push_back({s, ea, rb, -1});
            else if (eb >= 0) re.push_back({s, ra, eb, -1});
            else rr.push_back({s, ra, rb, reduced.add(ra, rb)});
        }
        // Structural closure: elimination can couple any boundary nodes of a linear component.
        for (int k = 0; k < ne; ++k)
            for (int i = 0; i < ne; ++i)
                for (int j = 0; j < ne; ++j)
                    connected[size_t(i)][size_t(j)] = connected[size_t(i)][size_t(j)]
                        || (connected[size_t(i)][size_t(k)] && connected[size_t(k)][size_t(j)]);
        for (const auto& a : re)
            for (const auto& b : er)
                if (connected[size_t(a.col)][size_t(b.row)]) reduced.add(a.row, b.col);
        linear.analyze(); reduced.analyze();
        linearValues.resize(size_t(linear.nnz())); reducedValues.resize(size_t(reduced.nnz()));
        couplingER.resize(size_t(ne * nr)); couplingRE.resize(size_t(nr * ne)); w.resize(size_t(ne * nr));
        correction.resize(size_t(reduced.nnz()));
        bE.resize(size_t(ne)); y.resize(size_t(ne)); bR.resize(size_t(nr)); xR.resize(size_t(nr));
        reTerms.reserve(size_t(nr * ne)); wTerms.reserve(size_t(ne * nr));
    }
    int eliminated() const { return ne; }
    int retained() const { return nr; }
    int reducedEntries() const { return reduced.nnz(); }
    bool factor(const std::vector<double>& values, bool baseChanged) override
    {
        if (!ready || baseChanged)
        {
            std::fill(linearValues.begin(), linearValues.end(), 0.0);
            std::fill(couplingER.begin(), couplingER.end(), 0.0);
            std::fill(couplingRE.begin(), couplingRE.end(), 0.0);
            for (const auto& a : ee) linearValues[size_t(a.slot)] = values[size_t(a.source)];
            for (const auto& a : er) couplingER[size_t(a.row * nr + a.col)] = values[size_t(a.source)];
            for (const auto& a : re) couplingRE[size_t(a.row * ne + a.col)] = values[size_t(a.source)];
            if (!linear.factor(linearValues.data())) throw std::runtime_error("singular eliminated block");
            for (int col = 0; col < nr; ++col)
            {
                for (int row = 0; row < ne; ++row) bE[size_t(row)] = couplingER[size_t(row * nr + col)];
                linear.solve(bE.data(), y.data());
                for (int row = 0; row < ne; ++row) w[size_t(row * nr + col)] = y[size_t(row)];
            }
            reTerms.clear(); wTerms.clear();
            for (int row = 0; row < nr; ++row)
                for (int col = 0; col < ne; ++col)
                    if (couplingRE[size_t(row * ne + col)] != 0.0)
                        reTerms.push_back({row, col, couplingRE[size_t(row * ne + col)]});
            for (int row = 0; row < ne; ++row)
                for (int col = 0; col < nr; ++col)
                    if (w[size_t(row * nr + col)] != 0.0)
                        wTerms.push_back({row, col, w[size_t(row * nr + col)]});
            const auto& slots = reduced.entryList();
            for (size_t i = 0; i < slots.size(); ++i)
            {
                const auto [row, col] = slots[i];
                double sum = 0.0;
                for (int k = 0; k < ne; ++k) sum += couplingRE[size_t(row * ne + k)] * w[size_t(k * nr + col)];
                correction[i] = sum;
            }
            ready = true;
        }
        for (size_t i = 0; i < correction.size(); ++i) reducedValues[i] = -correction[i];
        for (const auto& a : rr) reducedValues[size_t(a.slot)] += values[size_t(a.source)];
        return reduced.factor(reducedValues.data());
    }
    void solve(const double* rhs, double* result) override
    {
        for (int i = 0; i < ne; ++i) bE[size_t(i)] = rhs[eNodes[size_t(i)]];
        linear.solve(bE.data(), y.data());
        for (int i = 0; i < nr; ++i) bR[size_t(i)] = rhs[rNodes[size_t(i)]];
        for (const auto& term : reTerms) bR[size_t(term.row)] -= term.value * y[size_t(term.col)];
        reduced.solve(bR.data(), xR.data());
        for (int i = 0; i < nr; ++i) result[rNodes[size_t(i)]] = xR[size_t(i)];
        for (int i = 0; i < ne; ++i) result[eNodes[size_t(i)]] = y[size_t(i)];
        for (const auto& term : wTerms) result[eNodes[size_t(term.row)]] -= term.value * xR[size_t(term.col)];
    }
};
