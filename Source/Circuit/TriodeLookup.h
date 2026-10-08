#pragma once

// Smooth scalar lookups inside the existing triode law, not a memoryless audio waveshaper.
// Value and derivative come from the same cubic. Prepare tables before processing;
// unsupported inputs use the original equation in Circuit::evaluateDevices.
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace tubechannel::circuit
{
class TriodeLookup
{
    struct Cubic
    {
        double a, b, c, d;
        static Cubic hermite(double y0, double y1, double slope0, double slope1)
        {
            const double delta = y1 - y0;
            return { slope0 + slope1 - 2.0 * delta, 3.0 * delta - 2.0 * slope0 - slope1, slope0, y0 };
        }
        void evaluate(double t, double& value, double& slope) const
        {
            value = ((a * t + b) * t + c) * t + d;
            slope = (3.0 * a * t + 2.0 * b) * t + c;
        }
    };
    static constexpr int softSteps = 64, softCount = 60 * softSteps;
    struct SoftTable
    {
        std::array<Cubic, softCount> cells;
        SoftTable()
        {
            for (int i = 0; i < softCount; ++i)
            {
                const double x0 = -30.0 + double(i) / softSteps, x1 = x0 + 1.0 / softSteps;
                const double e0 = std::exp(x0), e1 = std::exp(x1);
                cells[size_t(i)] = Cubic::hermite(std::log1p(e0), std::log1p(e1),
                    e0 / (1.0 + e0) / softSteps, e1 / (1.0 + e1) / softSteps);
            }
        }
    };
    static const SoftTable& sharedSoftTable()
    {
        static const SoftTable table;
        return table;
    }
    static constexpr int powerBits = 7, powerCount = 1 << powerBits;
    static constexpr int minExponent = -64, maxExponent = 16;
    std::array<Cubic, powerCount> powerCells {};
    std::array<double, maxExponent - minExponent + 1> currentScale {}, derivativeScale {};
    const SoftTable* soft = nullptr;
    bool ready = false;
public:
    void prepare(double exponent, double kg)
    {
        ready = std::isfinite(exponent) && exponent >= 1.0 && exponent <= 2.0 && std::isfinite(kg) && kg > 0.0;
        if (!ready) return;
        soft = &sharedSoftTable(); // finish lazy initialization before the audio callback
        for (int i = 0; i < powerCount; ++i)
        {
            const double x0 = 1.0 + double(i) / powerCount, x1 = x0 + 1.0 / powerCount;
            powerCells[size_t(i)] = Cubic::hermite(std::pow(x0, exponent), std::pow(x1, exponent),
                exponent * std::pow(x0, exponent - 1.0) / powerCount,
                exponent * std::pow(x1, exponent - 1.0) / powerCount);
        }
        for (int e = minExponent; e <= maxExponent; ++e)
        {
            currentScale[size_t(e - minExponent)] = 2.0 / kg * std::exp2(e * exponent);
            derivativeScale[size_t(e - minExponent)] = 2.0 / kg * std::exp2(e * (exponent - 1.0));
        }
    }

    bool softplus(double x, double& value, double& derivative) const
    {
        if (!ready || !std::isfinite(x) || x < -30.0 || x > 30.0) return false;
        const double position = (x + 30.0) * softSteps;
        const int index = position >= softCount ? softCount - 1 : int(position);
        soft->cells[size_t(index)].evaluate(position - index, value, derivative);
        derivative *= softSteps;
        return true;
    }

    bool current(double energy, double& value, double& derivative) const
    {
        if (!ready || !(energy > 0.0) || !std::isfinite(energy)) return false;
        // IEEE double normalization replaces logarithms/exponentiation in the callback.
        const auto bits = std::bit_cast<std::uint64_t>(energy);
        const int exponent = int((bits >> 52) & 0x7ffu) - 1023;
        if (exponent < minExponent || exponent > maxExponent) return false;
        constexpr std::uint64_t fractionMask = (std::uint64_t(1) << 52) - 1;
        const auto fraction = bits & fractionMask;
        const int index = int(fraction >> (52 - powerBits));
        const double mantissa = std::bit_cast<double>((std::uint64_t(1023) << 52) | fraction);
        double power, slope;
        powerCells[size_t(index)].evaluate((mantissa - 1.0) * powerCount - index, power, slope);
        value = currentScale[size_t(exponent - minExponent)] * power;
        derivative = derivativeScale[size_t(exponent - minExponent)] * slope * powerCount;
        return true;
    }
};
} // namespace tubechannel::circuit
