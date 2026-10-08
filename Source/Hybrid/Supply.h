#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace tubechannel::hybrid
{
// Rail-response prototype initialized from the settled reference supply. Load
// sensitivity and recovery constants must be fitted; no runtime circuit solve.
class Supply
{
public:
    static constexpr std::size_t kRails = 7;
    enum Rail : std::size_t { Positive, Negative, Raw, Auxiliary, Multiplier, High, Plate };
    struct Snapshot
    {
        std::array<double, kRails> volts {};
        double phase = 0.0, heaterCurrent = 0.0;
    };
    struct Calibration
    {
        std::array<double, kRails> loadOhms { 0.08, 0.08, 3.2, 2.0, 200.0, 500.0, 500.0 };
        std::array<double, kRails> recoverySeconds { 0.01, 0.01, 0.032, 0.08, 0.15, 0.3, 0.3 };
        std::array<double, kRails> rippleVolts {}; // measured amplitudes; zero until fitted
        std::array<double, 4> restingLoads { 0.075, 0.075, 0.012, 0.006 };
        bool valid() const
        {
            for (std::size_t i = 0; i < kRails; ++i)
                if (!std::isfinite(loadOhms[i]) || loadOhms[i] < 0.0
                    || !std::isfinite(recoverySeconds[i]) || recoverySeconds[i] <= 0.0
                    || !std::isfinite(rippleVolts[i]) || rippleVolts[i] < 0.0) return false;
            for (double x : restingLoads) if (!std::isfinite(x) || x < 0.0) return false;
            return true;
        }
    };
    bool prepare(double rate, const Snapshot& settled, bool highPlate, const Calibration& c)
    {
        if (!std::isfinite(rate) || rate <= 0.0 || !c.valid()) return false;
        for (double x : settled.volts) if (!std::isfinite(x)) return false;
        if (!std::isfinite(settled.phase) || !std::isfinite(settled.heaterCurrent)) return false;
        fs = rate; calibration = c; state = anchor = settled;
        highAtPrepare = highPlate;
        baseline = settled.volts;
        for (std::size_t i = 0; i < kRails; ++i)
            coefficients[i] = -std::expm1(-1.0 / (fs * c.recoverySeconds[i]));
        return true;
    }
    void step(const std::array<double, 4>& loads, bool highPlate, double mainsHz, double mainsScale)
    {
        constexpr double twoPi = 6.283185307179586;
        state.phase = std::fmod(state.phase + twoPi * mainsHz / fs, twoPi);
        const std::array<std::size_t, kRails> loadForRail { 0, 1, 0, 2, 3, 3, 3 };
        for (std::size_t i = 0; i < kRails; ++i)
        {
            const auto load = loadForRail[i];
            double nominal = anchor.volts[i];
            if (i == Plate && highPlate != highAtPrepare)
                nominal = highPlate ? anchor.volts[High] : anchor.volts[Auxiliary];
            if (i >= Raw) nominal *= mainsScale;
            const double sag = (loads[load] - calibration.restingLoads[load]) * calibration.loadOhms[i];
            const double target = std::max(0.0, std::fabs(nominal) - sag);
            const double signedTarget = std::copysign(target, nominal);
            baseline[i] += (signedTarget - baseline[i]) * coefficients[i];
            state.volts[i] = baseline[i] + calibration.rippleVolts[i] * std::sin(2.0 * state.phase);
        }
    }
    const Snapshot& snapshot() const { return state; }

private:
    double fs = 8000.0;
    bool highAtPrepare = true;
    Calibration calibration;
    Snapshot state, anchor;
    std::array<double, kRails> baseline {}, coefficients {};
};
} // namespace tubechannel::hybrid
