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
        ports.p15 = psu.nodeId("P15"); ports.n15 = psu.nodeId("N15");
        ports.p25 = psu.nodeId("P25"); ports.n25 = psu.nodeId("N25");
        ports.p45 = psu.nodeId("P45"); ports.p75 = psu.nodeId("P75");
        ports.p150 = psu.nodeId("P150"); ports.tube = psu.nodeId("TUBEP");
        ports.heater = psu.resistorHandle("R30");
        ports.s1 = psu.inputHandle("nS1"); ports.s2 = psu.inputHandle("nS2");
        ports.regP = psu.inputHandle("P15"); ports.regN = psu.inputHandle("N15");
        ports.loadP = psu.loads.at("P15"); ports.loadN = psu.loads.at("N15");
        ports.load45 = psu.loads.at("P45"); ports.loadTube = psu.loads.at("TUBEP");
        ports.load150 = psu.loads.at("P150");
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

    double p15() const { return psu.nodeV(ports.p15); }
    double n15() const { return psu.nodeV(ports.n15); }
    double p45() const { return psu.nodeV(ports.p45); }
    double tubeP() const { return psu.nodeV(ports.tube); }
    double p25() const { return psu.nodeV(ports.p25); }
    double n25() const { return psu.nodeV(ports.n25); }
    double p75() const { return psu.nodeV(ports.p75); }
    double p150() const { return psu.nodeV(ports.p150); }
    double heaterCurrent() const { return psu.c.resistorCurrent(ports.heater); }
    double mainsPhase() const { return phase; }
    PowerSupplySettings settings;
    circuit::Block psu;

private:
    void set(double iP15, double iN15, double iP45, double iTube, bool high)
    {
        psu.c.setCurrent(ports.loadP, iP15);
        psu.c.setCurrent(ports.loadN, iN15);
        psu.c.setCurrent(ports.load45, iP45);
        psu.c.setCurrent(ports.loadTube, iTube);
        psu.c.setCurrent(ports.load150, 0.0);
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
        psu.setSourceH(ports.s1, v);
        psu.setSourceH(ports.s2, -v);
        psu.setSourceH(ports.regP, std::max(std::min(15.0, p25() - 2.0), 0.0));
        psu.setSourceH(ports.regN, std::min(std::max(-15.0, n25() + 2.0), 0.0));
        set(iP15, iN15, iP45, iTube, high);
        psu.step();
    }

    double phase = 0.0;
    bool lastHigh = true;
    struct Ports
    {
        int p15 = 0, n15 = 0, p25 = 0, n25 = 0, p45 = 0, p75 = 0, p150 = 0, tube = 0;
        int heater = 0, s1 = 0, s2 = 0, regP = 0, regN = 0;
        int loadP = 0, loadN = 0, load45 = 0, loadTube = 0, load150 = 0;
    } ports;
};
} // namespace tubechannel::channel
