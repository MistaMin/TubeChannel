#pragma once

#include "../Channel/ChannelTypes.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace tubechannel::hybrid
{
// Reduced piecewise circuit: two directional charging paths, their shared attack
// resistance, the timing capacitor's series resistance, and the release network.
// Four diode states give four precomputed scalar solves. Auto retains the same
// capacitor and uses a continuously connected slow path. Calibration is offline.
class Detector
{
public:
    // Round 8 offline fit; transient fidelity remains experimental. See the
    // hybrid-calibration measurement record before enabling this replacement.
    struct Calibration
    {
        double rectifierDrop = 0.4015805349376797, rectifierLogDrop = 6.6049205359322257e-05, rectifierFloor = 0.011427725025523822;
        double driveScale = 1, driverCeiling = 5.5652910978686965, fastFraction = 0.44881379821521744;
        double fastDrop = 9.9999999999999995e-07, slowDrop = 9.9999999999999995e-07;
        double fastSourceOhms = 1388.5606769447177, slowSourceOhms = 11617.728769148143;
        double timingFarads = 1.6613945837475307e-06;
        double releaseBaseOhms = 39725.662191053307, releaseSpanOhms = 70000, releaseFeedback = 0.069486636287972345;
        double autoFastOhms = 4525.9880629264653, autoSlowOhms = 81105.574163230587;
        bool valid() const
        {
            return positive(rectifierDrop) && positive(rectifierLogDrop) && positive(rectifierFloor)
                && positive(driveScale) && positive(driverCeiling) && positive(fastFraction)
                && fastFraction <= 1.0 && positive(fastDrop) && positive(slowDrop) && positive(fastSourceOhms) && positive(slowSourceOhms)
                && positive(timingFarads) && positive(releaseBaseOhms) && positive(releaseSpanOhms)
                && positive(releaseFeedback) && positive(autoFastOhms) && positive(autoSlowOhms);
        }
    private:
        static bool positive(double x) { return std::isfinite(x) && x > 0.0; }
    };
    bool prepare(double rate, const Calibration& c)
    {
        if (!std::isfinite(rate) || rate <= 0.0 || !c.valid()) return false;
        fs = rate; calibration = c;
        configure({}); reset();
        return true;
    }
    void reset() { timing = led = 0.0; }
    void configure(const channel::ChannelControls& c)
    {
        const double threshold = std::clamp(c.threshold, 0.0, 1.0);
        const double ratio = std::clamp(c.ratio, 0.0, 1.0);
        const double attack = std::clamp(c.attack, 0.0, 1.0);
        rectifierGain = 0.762 / (1.0960 * std::pow(10.0, (-20.0 + 40.0 * threshold) / 20.0));
        configureTiming(25000.0 * std::pow(1.0 - ratio, 3.0),
                        50000.0 * (std::pow(10.0, 2.0 * attack) - 1.0) / 99.0,
                        std::clamp(c.release, 0.0, 1.0), c.autoDetect);
    }
    void configureElectrical(double wiper, double ratioOhms, double attackOhms, double release, bool autoMode)
    {
        const double up = 10000.0 * wiper;
        const double down = 10000.0 * (1.0 - wiper) + 100.0;
        const double parallel = down * 2210.0 / (down + 2210.0);
        rectifierGain = parallel / (up + parallel) * (12000.0 / 2210.0);
        configureTiming(ratioOhms, attackOhms, release, autoMode);
    }
    double process(double volts)
    {
        const double amplitude = std::fabs(volts) * rectifierGain;
        const double rectified = std::clamp(amplitude - calibration.rectifierDrop
            - calibration.rectifierLogDrop * std::log1p(amplitude), -calibration.rectifierFloor, 6.8);
        const double slowDrive = std::min(rectified * driveGain - calibration.slowDrop, calibration.driverCeiling);
        const double fastDrive = slowDrive * calibration.fastFraction - calibration.fastDrop;
        double next = timing, sensed = 0.0, smallestViolation = 1e300;
        // The monotone passive network has one consistent active set. Every
        // candidate is a scalar backward-Euler solve, with no Newton iteration.
        for (unsigned mask = automatic ? 2u : 0u; mask < 4u; ++mask)
        {
            const auto& b = branches[mask];
            const double source = b.fastWeight * fastDrive + b.slowWeight * slowDrive;
            const double stored = (timing + b.inputStep * source) * b.inverse;
            const double y = b.capGain * stored + b.inputGain * source;
            const double x = b.sourceFraction * source + (1.0 - b.sourceFraction) * y;
            // At a diode boundary either neighboring solve is equivalent. Select
            // the least violating branch as a roundoff fallback; never drop the
            // LED to zero solely because no exact active-set predicate matched.
            const double fastError = mask & 1u ? std::max(0.0, x - fastDrive) : std::max(0.0, fastDrive - x);
            const double slowError = automatic ? 0.0 : mask & 2u ? std::max(0.0, x - slowDrive) : std::max(0.0, slowDrive - x);
            const double violation = fastError + slowError;
            if (violation < smallestViolation)
            {
                smallestViolation = violation; next = stored; sensed = y;
            }
            if (violation == 0.0)
            {
                next = stored; sensed = y;
                break;
            }
        }
        timing = std::clamp(next, 0.0, calibration.driverCeiling);
        led = std::clamp(sensed / 300.0, 0.0, 0.04);
        return led;
    }
    double ledCurrent() const { return led; }

private:
    struct Branch
    {
        double fastWeight = 0.0, slowWeight = 0.0;
        double capGain = 1.0, inputGain = 0.0, inputStep = 0.0, inverse = 1.0, sourceFraction = 0.0;
    };
    void configureTiming(double ratioOhms, double attackOhms, double release, bool autoMode)
    {
        automatic = autoMode;
        driveGain = calibration.driveScale * 4990.0 / (1000.0 + ratioOhms);
        const double r = std::clamp(release, 0.0, 1.0);
        const double releaseOhms = calibration.releaseBaseOhms
            + calibration.releaseSpanOhms * r / (1.0 - r + calibration.releaseFeedback);
        const double leak = automatic ? 0.0 : 1.0 / releaseOhms;
        const double fast = 1.0 / (automatic ? calibration.autoFastOhms : calibration.fastSourceOhms);
        const double slow = 1.0 / (automatic ? calibration.autoSlowOhms : calibration.slowSourceOhms);
        const double common = automatic ? 0.0 : attackOhms;
        constexpr double capConductance = 1.0 / 2210.0;
        const double step = 1.0 / (fs * calibration.timingFarads * 2210.0);
        for (unsigned mask = 0; mask < 4; ++mask)
        {
            auto& b = branches[mask];
            const double gFast = mask & 1u ? fast : 0.0;
            const double gSlow = mask & 2u ? slow : 0.0;
            const double total = gFast + gSlow;
            const double sourceOhms = total > 0.0 ? 1.0 / total : 0.0;
            const double conductance = total > 0.0 ? 1.0 / (common + sourceOhms) : 0.0;
            b.fastWeight = total > 0.0 ? gFast / total : 0.0;
            b.slowWeight = total > 0.0 ? gSlow / total : 0.0;
            b.capGain = capConductance / (capConductance + conductance + leak);
            b.inputGain = conductance / (capConductance + conductance + leak);
            b.inputStep = step * b.inputGain;
            b.inverse = 1.0 / (1.0 + step * (1.0 - b.capGain));
            b.sourceFraction = total > 0.0 ? common / (common + sourceOhms) : 0.0;
        }
    }
    Calibration calibration;
    std::array<Branch, 4> branches;
    double fs = 48000.0, rectifierGain = 1.0, driveGain = 1.0, timing = 0.0, led = 0.0;
    bool automatic = false;
};
} // namespace tubechannel::hybrid
