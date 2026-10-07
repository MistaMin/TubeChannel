#pragma once

// The vactrol (LED + photoresistor) of the compressor. The LED current comes from the detector circuit
// (Netlists/det.net); the photoresistor sits in the audio path (Netlists/comp.net, RLDR).
//
// Light follows the LED current with the datasheet times (3.5 ms rise, 72 ms fall). The photoresistor
// follows the output-resistance curve of the datasheet (log-log, read from the plot and checked against the
// table: 5.5k at 1 mA, 800 at 10 mA, 200 at 40 mA, R(0.5 mA) / R(5 mA) = 24), which is very steep below 1 mA,
// in parallel with the dark resistance of 1 Mohm.

#include <algorithm>
#include <cmath>

namespace tubechannel::channel
{
class Vactrol
{
public:
    void prepare(double sampleRate)
    {
        const double dt = 1.0 / sampleRate;
        aRise = 1.0 - std::exp(-dt / 3.5e-3);
        aFall = 1.0 - std::exp(-dt / 72e-3);
        light = 0.0;
        ldrR = 1e6;
        tick = 0;
    }

    void reset()
    {
        light = 0.0;
        ldrR = 1e6;
    }

    // iled: amps through the LED for this sample. Returns the photoresistor value (ohms).
    double process(double iled)
    {
        iled = std::min(std::max(iled, 0.0), 40e-3);          // the LED maximum
        light += (iled - light) * (iled > light ? aRise : aFall);
        if (++tick >= 4)
        {
            tick = 0;
            ldrR = 1.0 / (1.0 / 1e6 + 1.0 / onResistance(light * 1e3));
        }
        return ldrR;
    }

    // on-resistance (ohms) for an LED current in mA: piecewise power law between the anchor points
    static double onResistance(double mA)
    {
        static constexpr double kI[] = { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 40.0 };
        static constexpr double kR[] = { 1.0e6, 4.0e5, 9.0e4, 2.8e4, 5.5e3, 2.9e3, 1.3e3, 8.0e2, 4.5e2, 2.0e2 };
        constexpr int n = 10;
        if (mA <= kI[0]) return kR[0];
        if (mA >= kI[n - 1]) return kR[n - 1];
        int k = 1;
        while (mA > kI[k]) ++k;
        const double t = std::log(mA / kI[k - 1]) / std::log(kI[k] / kI[k - 1]);
        return kR[k - 1] * std::pow(kR[k] / kR[k - 1], t);
    }

    double ldrResistance() const { return ldrR; }
    double ledCurrent() const { return light; }

    // Gain reduction in dB as seen by the audio path: the divider formed by the series resistance and the
    // photoresistor, relative to the dark state.
    double gainReductionDb(double rSeries = 14.7e3) const
    {
        const double dark = 1e6 / (1e6 + rSeries);
        const double now = ldrR / (ldrR + rSeries);
        return -20.0 * std::log10(std::max(now / dark, 1e-6));
    }

private:
    double aRise = 0.0, aFall = 0.0, light = 0.0, ldrR = 1e6;
    int tick = 0;
};
} // namespace tubechannel::channel
