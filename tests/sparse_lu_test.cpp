// Sparse LU vs a dense reference on random circuit-like matrices.
#include "../Source/Circuit/SparseLU.h"

#include <cstdio>
#include <random>

using namespace tubechannel::circuit;

int main()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    int failures = 0;
    for (int trial = 0; trial < 200; ++trial)
    {
        const int n = 3 + int(rng() % 60);
        std::vector<std::vector<double>> A(size_t(n), std::vector<double>(size_t(n), 0.0));
        SparseLU lu;
        std::vector<std::pair<int, int>> pat;
        // diagonally dominant sparse matrix with some non-symmetric coupling
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k)
            {
                const int j = int(rng() % size_t(n));
                if (j == i) continue;
                const double g = std::fabs(u(rng)) + 0.1;
                A[size_t(i)][size_t(i)] += g; A[size_t(j)][size_t(j)] += g;
                A[size_t(i)][size_t(j)] -= g; A[size_t(j)][size_t(i)] -= g;
                const double c = u(rng) * 0.5;                      // VCCS-like asymmetric term
                A[size_t(i)][size_t(j)] += c;
            }
        for (int i = 0; i < n; ++i) A[size_t(i)][size_t(i)] += 1e-3;
        std::vector<int> idx;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                if (A[size_t(i)][size_t(j)] != 0.0) { idx.push_back(lu.add(i, j)); pat.push_back({ i, j }); }
        lu.analyze();
        std::vector<double> vals(size_t(lu.nnz()), 0.0);
        for (size_t e = 0; e < pat.size(); ++e) vals[size_t(idx[e])] = A[size_t(pat[e].first)][size_t(pat[e].second)];
        std::vector<double> b(static_cast<size_t>(n)), x(static_cast<size_t>(n));
        for (auto& v : b) v = u(rng);
        lu.factor(vals.data());
        lu.solve(b.data(), x.data());
        double worst = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double s = 0.0;
            for (int j = 0; j < n; ++j) s += A[size_t(i)][size_t(j)] * x[size_t(j)];
            worst = std::max(worst, std::fabs(s - b[size_t(i)]));
        }
        if (worst > 1e-8) { ++failures; std::printf("trial %d n=%d residual %.3g\n", trial, n, worst); }
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
