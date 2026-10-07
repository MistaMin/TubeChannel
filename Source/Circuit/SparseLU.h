#pragma once

// Static-pattern sparse LU for nodal circuit matrices.
//
// The pattern is fixed at build time (union of every position any element can stamp). A minimum
// degree ordering and the fill-in are worked out once; after that every factor() / solve() is a
// fixed sequence of multiply-adds with no allocation. No numerical pivoting: nodal matrices of
// physical circuits with a small conductance to ground on every node are well behaved.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <type_traits>
#include <vector>

#include "Lane.h"

namespace tubechannel::circuit
{
template <class R>
class SparseLUT
{
public:
    struct Term { R v; int c; };

    // Register a matrix position. Returns the index into the value array. Duplicates share an index.
    int add(int row, int col)
    {
        const auto key = std::make_pair(row, col);
        auto it = lookup.find(key);
        if (it != lookup.end())
            return it->second;
        const int idx = int(entries.size());
        entries.push_back(key);
        lookup.emplace(key, idx);
        n = std::max(n, std::max(row, col) + 1);
        return idx;
    }

    int size() const { return n; }
    const std::vector<std::pair<int, int>>& entryList() const { return entries; }
    double minPivot() const { return minPiv; }
    int minPivotNode() const { return minPivIdx; }
    int nnz() const { return int(entries.size()); }
    int fillIn() const { return int(lCol.size() + uCol.size()) - int(entries.size()); }

    int treeHeight() const { return height; }
    // multiply-adds in one factorisation and in one solve (diagnostics)
    long factorOps() const
    {
        long ops = 0;
        for (int i = 0; i < n; ++i)
            for (int p = lStart[size_t(i)]; p < lStart[size_t(i) + 1]; ++p)
            {
                const int k = lCol[size_t(p)];
                ops += uStart[size_t(k) + 1] - uStart[size_t(k)];
            }
        return ops;
    }
    long solveOps() const { return long(lCol.size() + uCol.size()); }

    void analyze()
    {
        // Make sure every diagonal exists (needed as the pivot).
        for (int i = 0; i < n; ++i)
            add(i, i);

        std::vector<std::set<int>> adj(static_cast<size_t>(n));
        for (auto [r, c] : entries)
            if (r != c)
            {
                adj[size_t(r)].insert(c);
                adj[size_t(c)].insert(r);
            }

        // Candidate orderings. The solves are latency bound, so cost = fill + a weight on the
        // elimination-tree height (the longest dependent chain).
        std::vector<std::vector<int>> candidates;
        candidates.push_back(minimumDegree(adj));
        candidates.push_back(nestedDissection(adj));
        double bestCost = 1e300;
        std::vector<int> bestOrder;
        for (auto& cand : candidates)
        {
            symbolic(cand);
            const double cost = double(lCol.size() + uCol.size()) + 10.0 * double(height);
            if (std::getenv("TC_LU_DEBUG")) std::fprintf(stderr, "  ordering candidate: n=%d lu=%zu height=%d cost=%.0f\n", n, lCol.size() + uCol.size(), height, cost);
            if (cost < bestCost)
            {
                bestCost = cost;
                bestOrder = cand;
            }
        }
        symbolic(bestOrder);
        finishAnalyze();
    }

    // values: one number per registered entry. Returns false if a pivot is (numerically) zero.
    // The elimination is a precomputed program over the (value, column) records: every update knows the record it
    // changes and the record it reads, so there is no work row, no gather and no scatter.
    bool factor(const R* values)
    {
        bool ok = true;
        minPiv = 1e300;
        Term* t = T.data();
        const size_t total = T.size();
        for (size_t p = 0; p < total; ++p)
            t[p].v = R(0.0);
        for (size_t e = 0; e < slotOf.size(); ++e)
            t[slotOf[e]].v += values[e];
        R* ip = invPiv.data();
        const int* pivL = opPiv.data();
        const int* pivK = opK.data();
        const int* cnt = opCount.data();
        const int* tg = opTgt.data();
        const int* sr = opSrc.data();
        size_t op = 0, up = 0;
        for (int i = 0; i < n; ++i)
        {
            for (int c = opRowStart[size_t(i)]; c < opRowStart[size_t(i) + 1]; ++c, ++op)
            {
                const int pl = pivL[op];
                const R lik = t[pl].v * ip[pivK[op]];
                t[pl].v = lik;
                for (int m = cnt[op]; m > 0; --m, ++up)
                    t[tg[up]].v -= lik * t[sr[up]].v;
            }
            const size_t di = size_t(nL + uStart[size_t(i)]);            // the diagonal is first in the U row
            R d = t[di].v;
            const double ad = minLane(fabsR(d));
            if (ad < minPiv)
            {
                minPiv = ad;
                minPivIdx = order[size_t(i)];
            }
            if (ad < 1e-300)
            {
                ok = false;
                d = t[di].v = clampPivot(d);
            }
            ip[size_t(i)] = R(1.0) / d;
        }
        return ok;
    }

    // Row-oriented solves: every row is a dot product accumulated in a register, so the only stores are the n
    // results and nothing waits on a store-to-load forward (the scatter form needs one store per operation).
    void solve(const R* b, R* x)
    {
        R* yy = y.data();
        const int* ord = order.data();
        const int* ls = lStart.data();
        const Term* lt = T.data();
        for (int i = 0; i < n; ++i)
        {
            R acc = b[ord[i]];
            for (int p = ls[i]; p < ls[i + 1]; ++p)
                acc -= lt[p].v * yy[lt[p].c];
            yy[i] = acc;
        }
        const int* us = uStart.data();
        const Term* ut = T.data() + nL;
        const R* ip = invPiv.data();
        for (int i = n - 1; i >= 0; --i)
        {
            R acc = yy[i];
            for (int p = us[i] + 1; p < us[i + 1]; ++p)
                acc -= ut[p].v * yy[ut[p].c];
            yy[i] = acc * ip[i];
        }
        for (int i = 0; i < n; ++i)
            x[ord[i]] = yy[i];
    }

private:
    static std::vector<int> minimumDegree(std::vector<std::set<int>> adj)
    {
        const int n = int(adj.size());
        std::vector<bool> done(static_cast<size_t>(n), false);
        std::vector<int> ord(static_cast<size_t>(n), 0);
        for (int step = 0; step < n; ++step)
        {
            int best = -1;
            size_t bestDeg = ~size_t(0);
            for (int i = 0; i < n; ++i)
                if (!done[size_t(i)] && adj[size_t(i)].size() < bestDeg)
                {
                    best = i;
                    bestDeg = adj[size_t(i)].size();
                }
            ord[size_t(step)] = best;
            done[size_t(best)] = true;
            const auto nb = adj[size_t(best)];
            for (int a : nb)
            {
                adj[size_t(a)].erase(best);
                for (int b : nb)
                    if (a != b)
                        adj[size_t(a)].insert(b);
            }
        }
        return ord;
    }

    // Recursive bisection by BFS level sets: the middle level is the separator and is eliminated last.
    static std::vector<int> nestedDissection(const std::vector<std::set<int>>& adj)
    {
        const int n = int(adj.size());
        std::vector<int> result;
        std::vector<int> all(static_cast<size_t>(n));
        std::iota(all.begin(), all.end(), 0);
        dissect(adj, all, result);
        return result;
    }

    static void dissect(const std::vector<std::set<int>>& adj, const std::vector<int>& nodes, std::vector<int>& out)
    {
        if (nodes.size() <= 3)
        {
            // small: eliminate in minimum degree order of the induced subgraph
            std::vector<int> rest = nodes;
            std::sort(rest.begin(), rest.end(), [&](int a, int b) { return adj[size_t(a)].size() < adj[size_t(b)].size(); });
            out.insert(out.end(), rest.begin(), rest.end());
            return;
        }
        std::set<int> inSet(nodes.begin(), nodes.end());
        // connected components first
        std::vector<std::vector<int>> comps;
        std::set<int> seen;
        for (int start : nodes)
        {
            if (seen.count(start)) continue;
            std::vector<int> comp, queue { start };
            seen.insert(start);
            for (size_t q = 0; q < queue.size(); ++q)
            {
                comp.push_back(queue[q]);
                for (int nb : adj[size_t(queue[q])])
                    if (inSet.count(nb) && !seen.count(nb)) { seen.insert(nb); queue.push_back(nb); }
            }
            comps.push_back(comp);
        }
        if (comps.size() > 1)
        {
            for (auto& c : comps) dissect(adj, c, out);
            return;
        }
        // pseudo-peripheral start: BFS twice
        auto bfs = [&](int start, std::vector<int>& levelOf)
        {
            std::map<int, int> dist;
            std::vector<int> queue { start };
            dist[start] = 0;
            for (size_t q = 0; q < queue.size(); ++q)
                for (int nb : adj[size_t(queue[q])])
                    if (inSet.count(nb) && !dist.count(nb)) { dist[nb] = dist[queue[q]] + 1; queue.push_back(nb); }
            levelOf.assign(nodes.size(), 0);
            int maxLevel = 0;
            for (size_t i = 0; i < nodes.size(); ++i) { levelOf[i] = dist[nodes[i]]; maxLevel = std::max(maxLevel, levelOf[i]); }
            return std::make_pair(queue.back(), maxLevel);
        };
        std::vector<int> lev;
        auto [far, ml] = bfs(nodes[0], lev);
        auto [far2, ml2] = bfs(far, lev);
        (void) far2;
        if (ml2 < 2)
        {
            out.insert(out.end(), nodes.begin(), nodes.end());
            return;
        }
        // choose the level whose removal best balances the two sides, preferring small levels
        int bestLevel = ml2 / 2;
        double bestScore = 1e300;
        for (int L = 1; L < ml2; ++L)
        {
            int lo = 0, mid = 0, hi = 0;
            for (size_t i = 0; i < nodes.size(); ++i)
                (lev[i] < L ? lo : lev[i] == L ? mid : hi)++;
            const double score = double(mid) * 3.0 + std::fabs(double(lo - hi));
            if (score < bestScore) { bestScore = score; bestLevel = L; }
        }
        std::vector<int> a, b, sep;
        for (size_t i = 0; i < nodes.size(); ++i)
            (lev[i] < bestLevel ? a : lev[i] == bestLevel ? sep : b).push_back(nodes[i]);
        dissect(adj, a, out);
        dissect(adj, b, out);
        out.insert(out.end(), sep.begin(), sep.end());
    }

    // fill-in pattern for the given order; also the elimination-tree height
    void symbolic(const std::vector<int>& ord)
    {
        order = ord;
        pos.assign(size_t(n), 0);
        for (int i = 0; i < n; ++i)
            pos[size_t(order[size_t(i)])] = i;
        std::vector<std::set<int>> pat(static_cast<size_t>(n));
        for (auto [r, c] : entries)
            pat[size_t(pos[size_t(r)])].insert(pos[size_t(c)]);
        std::vector<std::vector<int>> uRows(static_cast<size_t>(n));
        lStart.assign(size_t(n) + 1, 0);
        uStart.assign(size_t(n) + 1, 0);
        lCol.clear();
        uCol.clear();
        std::vector<int> parent(static_cast<size_t>(n), -1);
        for (int i = 0; i < n; ++i)
        {
            std::set<int> row = pat[size_t(i)];
            row.insert(i);
            for (auto it = row.begin(); it != row.end() && *it < i; ++it)
            {
                const int k = *it;
                for (int j : uRows[size_t(k)])
                    if (j > k)
                        row.insert(j);
            }
            for (int j : row)
                (j < i ? lCol : uCol).push_back(j);
            for (int j : row)
                if (j >= i)
                    uRows[size_t(i)].push_back(j);
            for (int j : row)
                if (j > i) { parent[size_t(i)] = j; break; }
            lStart[size_t(i) + 1] = int(lCol.size());
            uStart[size_t(i) + 1] = int(uCol.size());
        }
        // height of the elimination tree
        std::vector<int> depth(static_cast<size_t>(n), 1);
        height = 1;
        for (int i = 0; i < n; ++i)
            if (parent[size_t(i)] >= 0)
                depth[size_t(parent[size_t(i)])] = std::max(depth[size_t(parent[size_t(i)])], depth[size_t(i)] + 1);
        for (int i = 0; i < n; ++i) height = std::max(height, depth[size_t(i)]);
    }

    void finishAnalyze()
    {
        work.assign(size_t(n), R(0.0));
        y.assign(size_t(n), R(0.0));
        invPiv.assign(size_t(n), R(1.0));
        nL = int(lCol.size());
        T.assign(lCol.size() + uCol.size(), Term { R(0.0), 0 });
        for (size_t p = 0; p < lCol.size(); ++p) T[p].c = lCol[p];
        for (size_t p = 0; p < uCol.size(); ++p) T[size_t(nL) + p].c = uCol[p];
        // where every registered matrix entry lives
        auto find = [&](int row, int col) -> int
        {
            if (col < row)
            {
                for (int p = lStart[size_t(row)]; p < lStart[size_t(row) + 1]; ++p)
                    if (lCol[size_t(p)] == col) return p;
            }
            else
            {
                for (int p = uStart[size_t(row)]; p < uStart[size_t(row) + 1]; ++p)
                    if (uCol[size_t(p)] == col) return nL + p;
            }
            std::fprintf(stderr, "SparseLU: entry (%d,%d) missing from the factor pattern\n", row, col);
            std::abort();
        };
        slotOf.assign(entries.size(), 0);
        for (size_t e = 0; e < entries.size(); ++e)
            slotOf[e] = find(pos[size_t(entries[e].first)], pos[size_t(entries[e].second)]);
        // the elimination program
        opRowStart.assign(size_t(n) + 1, 0);
        opPiv.clear(); opK.clear(); opCount.clear(); opTgt.clear(); opSrc.clear();
        for (int i = 0; i < n; ++i)
        {
            for (int p = lStart[size_t(i)]; p < lStart[size_t(i) + 1]; ++p)
            {
                const int k = lCol[size_t(p)];
                opPiv.push_back(p);
                opK.push_back(k);
                opCount.push_back(uStart[size_t(k) + 1] - uStart[size_t(k)] - 1);
                for (int q = uStart[size_t(k)] + 1; q < uStart[size_t(k) + 1]; ++q)
                {
                    opTgt.push_back(find(i, uCol[size_t(q)]));
                    opSrc.push_back(nL + q);
                }
            }
            opRowStart[size_t(i) + 1] = int(opPiv.size());
        }
    }

    int n = 0;
    int height = 0;
    double minPiv = 0.0;
    int minPivIdx = -1;
    std::vector<std::pair<int, int>> entries;
    std::map<std::pair<int, int>, int> lookup;
    std::vector<int> order, pos;
    std::vector<int> lStart, lCol, uStart, uCol;
    std::vector<R> work, y, invPiv;
    int nL = 0;
    std::vector<Term> T;                                  // L records, then U records (diagonal first in each U row)
    std::vector<int> slotOf;                              // registered entry -> record
    std::vector<int> opRowStart, opPiv, opK, opCount, opTgt, opSrc;   // the elimination program
};
using SparseLU = SparseLUT<double>;
using SparseLU2 = SparseLUT<V2>;
} // namespace tubechannel::circuit
