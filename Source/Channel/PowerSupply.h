#pragma once

// The channel's power supply as a circuit: transformer secondary with leakage inductance, two
// rectifiers, the +75 V / +150 V multipliers, the 7815/7915 regulators, the +45 V series regulator
// and the tube plate supply. Audio-dependent loads are fed in each step, so the rails sag with
// the signal and ripple at twice the mains frequency.
//
// The supply is solved at a fixed internal rate (its rails move far below 10 kHz); the audio path
// reads the rails at the oversampled rate (see ChannelModel).

#include <cmath>
#include <string>

#include "../Circuit/Block.h"

namespace tubechannel::channel
{
struct PowerSupplySettings
{
    double vsec = 22.0;          // rms volts per half secondary at nominal mains (open circuit)
    double rsec = 3.2;           // secondary + reflected primary resistance per half, ohms
    double mainsHz = 60.0;
    double mainsScale = 1.0;     // 1.0 = nominal mains voltage
};

class PowerSupply
{
public:
    static inline double kRate = 8000.0;

    void prepare(const std::string& netlist, const PowerSupplySettings& s, const circuit::ToleranceSettings& tol)
    {
        settings = s;
        using namespace circuit;
        psu.build(netlist, 1.0 / kRate, tol, {},
                  { { "nS1", s.rsec }, { "nS2", s.rsec }, { "P15", 0.015 }, { "N15", 0.015 } }, 0.5,
                  { { "P15", false }, { "N15", true }, { "P45", false }, { "TUBEP", false }, { "P150", false } });
        psu.dc();
        phase = 0.0;
        set(0.0, 0.0, 0.0, 0.0, false);
    }

    // Run to a steady state with nominal loads (called once, off the audio thread).
    void settle(double seconds = 0.5, bool high = true)
    {
        // The multiplier chain takes several seconds to charge from cold. Charge it with the
        // capacitors scaled down (the voltages carry over when the real values come back), in stages.
        psu.c.setTimeStep(1.0 / kRate);
        const struct { double scale, secs; } stages[] = { { 1.0 / 40.0, 0.35 }, { 1.0 / 10.0, 0.7 }, { 1.0 / 3.0, 0.6 } };
        for (const auto& st : stages)
        {
            psu.c.scaleCapacitors(st.scale);
            for (int i = 0; i < int(st.secs * kRate); ++i)
                stepWith(1.0 / kRate, 0.075, 0.075, 0.012, 0.006, high);
        }
        psu.c.scaleCapacitors(1.0);
        for (int i = 0; i < int(seconds * kRate); ++i)
            stepWith(1.0 / kRate, 0.075, 0.075, 0.012, 0.006, high);
        psu.c.setTimeStep(1.0 / kRate);
    }

    // One step at the supply's own rate. Loads in amps: positive-rail, negative-rail, +45 V, plate supply.
    void step(double iP15, double iN15, double iP45, double iTube, bool high)
    {
        stepWith(1.0 / kRate, iP15, iN15, iP45, iTube, high);
    }

    double p15() const { return psu.node("P15"); }
    double n15() const { return psu.node("N15"); }
    double p45() const { return psu.node("P45"); }
    double tubeP() const { return psu.node("TUBEP"); }
    double p25() const { return psu.node("P25"); }
    double n25() const { return psu.node("N25"); }
    double p75() const { return psu.node("P75"); }
    double p150() const { return psu.node("P150"); }
    double heaterCurrent() const { return psu.c.resistorCurrent(psu.nl.resistors.at("R30")); }
    double mainsPhase() const { return phase; }
    PowerSupplySettings settings;
    circuit::Block psu;

private:
    void set(double iP15, double iN15, double iP45, double iTube, bool high)
    {
        psu.setLoad("P15", iP15);
        psu.setLoad("N15", iN15);
        psu.setLoad("P45", iP45);
        psu.setLoad("TUBEP", iTube);
        psu.setLoad("P150", 0.0);
        if (high != lastHigh)
        {
            psu.setParam("HV", high ? 1.0 : 0.0);
            psu.applyParams();
            lastHigh = high;
        }
    }

    void stepWith(double dt, double iP15, double iN15, double iP45, double iTube, bool high)
    {
        phase += 6.283185307179586 * settings.mainsHz * dt;
        if (phase > 6.283185307179586) phase -= 6.283185307179586;
        const double v = settings.vsec * settings.mainsScale * 1.4142135623730951 * std::sin(phase);
        psu.setInput("nS1", v);
        psu.setInput("nS2", -v);
        psu.setInput("P15", std::max(std::min(15.0, psu.node("P25") - 2.0), 0.0));
        psu.setInput("N15", std::min(std::max(-15.0, psu.node("N25") + 2.0), 0.0));
        set(iP15, iN15, iP45, iTube, high);
        psu.step();
    }

    double phase = 0.0;
    bool lastHigh = true;
};
} // namespace tubechannel::channel
