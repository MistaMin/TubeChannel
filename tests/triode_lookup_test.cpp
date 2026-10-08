#include "../Source/Circuit/TriodeLookup.h"
#include <algorithm>
#include <cstdio>
#include <limits>

int main()
{
    using tubechannel::circuit::TriodeLookup;
    int failures = 0;
    for (double exponent : { 1.1, 1.2, 1.4 })
    {
        TriodeLookup lookup; lookup.prepare(exponent, 825.0);
        double maxValue = 0, maxDerivative = 0, maxSoft = 0, maxSig = 0;
        for (int i = 0; i <= 100000; ++i)
        {
            const double arg = -30.0 + 60.0 * double(i) / 100000.0;
            double value, derivative;
            if (!lookup.softplus(arg, value, derivative)) { ++failures; continue; }
            const double ea = std::exp(arg), ref = std::log1p(ea), sig = ea / (1.0 + ea);
            maxSoft = std::max(maxSoft, std::fabs(value - ref) / std::max(ref, 1e-30));
            maxSig = std::max(maxSig, std::fabs(derivative - sig) / std::max(sig, 1e-30));
            if (!(value > 0 && derivative > 0)) ++failures;
        }
        for (int e = -64; e <= 16; ++e)
            for (int i = 0; i < 2048; ++i)
            {
                const double energy = std::ldexp(1.0 + double(i) / 2048.0, e);
                double value, derivative;
                if (!lookup.current(energy, value, derivative)) { ++failures; continue; }
                const double ref = 2.0 / 825.0 * std::pow(energy, exponent);
                const double slope = 2.0 / 825.0 * exponent * std::pow(energy, exponent - 1.0);
                maxValue = std::max(maxValue, std::fabs(value / ref - 1.0));
                maxDerivative = std::max(maxDerivative, std::fabs(derivative / slope - 1.0));
                const double h = energy * 1e-5;
                double lo, hi, ignored;
                if (lookup.current(energy - h, lo, ignored) && lookup.current(energy + h, hi, ignored))
                    if (std::fabs((hi - lo) / (2 * h) / derivative - 1.0) > 1e-6) ++failures;
            }
        std::printf("EX %.1f: relative max errors current %.3g, derivative %.3g, softplus %.3g, sigmoid %.3g\n",
            exponent, maxValue, maxDerivative, maxSoft, maxSig);
        if (maxValue > 1e-9 || maxDerivative > 1e-7 || maxSoft > 1e-9 || maxSig > 1e-7) ++failures;
        double value, derivative;
        for (double energy : { 0.0, -1.0, 1e-300, 1e10, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
            if (lookup.current(energy, value, derivative)) ++failures;
        for (double arg : { -31.0, 31.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
            if (lookup.softplus(arg, value, derivative)) ++failures;
    }
    std::printf("triode curve failures %d\n", failures);
    return failures ? 1 : 0;
}
