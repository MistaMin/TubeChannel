#pragma once

// One stage of the channel: a netlist loaded into its own Circuit, with Thevenin inputs,
// supply rails and read-out helpers. Stages are chained one way (an op-amp output drives the next
// stage's input through its output resistance), which keeps every matrix small.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Netlist.h"

namespace tubechannel::circuit
{
class Block
{
public:
    Circuit c;
    Netlist nl;

    // railNames: nodes that become supply rails if the netlist has them (VP, VN, V45, TUBEP)
    // loads: (node, intoNode): a current sink on the node, or a source feeding current into it
    void build(const std::string& netlistText, double dt, const ToleranceSettings& tol,
               const std::vector<std::string>& railNames, const std::vector<std::pair<std::string, double>>& inputs,
               double theta = 0.55, const std::vector<std::pair<std::string, bool>>& loadNodes = {})
    {
        nl.load(c, netlistText, tol);
        for (const auto& r : railNames)
            if (c.hasNode(r) > 0)
            {
                const int h = c.addSource(c.node(r), 0.02, 0.0);
                rails[r] = h;
                if (r == "VP") hVP = h;
                else if (r == "VN") hVN = h;
                else if (r == "V45") hV45 = h;
                else if (r == "TUBEP") hTube = h;
            }
        for (const auto& [name, rs] : inputs)
            this->inputs[name] = c.addSource(c.node(name), rs, 0.0);
        for (const auto& [name, into] : loadNodes)
            loads[name] = into ? c.addCurrentSource(0, c.node(name), 0.0) : c.addCurrentSource(c.node(name), 0, 0.0);
        c.finalize(dt, theta);
        for (auto& [name, h] : nl.opamps)
            opampList.push_back(h);
    }

    // ---- by name (set-up time) ----
    int inputHandle(const std::string& name) const { return inputs.at(name); }
    int nodeId(const std::string& name) const { return c.hasNode(name); }
    int opampHandle(const std::string& name) const { return nl.opamps.at(name); }
    int resistorHandle(const std::string& ref) const { return nl.resistors.at(ref); }

    // ---- by handle (per-sample) ----
    void setSourceH(int h, double v) { c.setSource(h, v); }
    double nodeV(int id) const { return c.v(id); }
    double opampOutH(int h) const { return c.opampOut(h); }
    double sourceCurrentH(int h) const { return h >= 0 ? c.sourceCurrent(h) : 0.0; }

    void setParam(const std::string& name, double value) { nl.params[name] = value; }
    void applyParams() { nl.update(c); }
    void setRail(const std::string& name, double v)
    {
        auto it = rails.find(name);
        if (it != rails.end())
            c.setSource(it->second, v);
    }
    void setRails(double vp, double vn, double v45, double tubep)
    {
        if (hVP >= 0) c.setSource(hVP, vp);
        if (hVN >= 0) c.setSource(hVN, vn);
        if (hV45 >= 0) c.setSource(hV45, v45);
        if (hTube >= 0) c.setSource(hTube, tubep);
    }
    void setInput(const std::string& name, double v) { c.setSource(inputs.at(name), v); }
    void setLoad(const std::string& name, double amps) { c.setCurrent(loads.at(name), amps); }
    double railCurrent(const std::string& name) const
    {
        auto it = rails.find(name);
        return it == rails.end() ? 0.0 : c.sourceCurrent(it->second);
    }
    double currentVP() const { return sourceCurrentH(hVP); }
    double currentVN() const { return sourceCurrentH(hVN); }
    double currentV45() const { return sourceCurrentH(hV45); }
    double currentTube() const { return sourceCurrentH(hTube); }
    bool hasInput(const std::string& name) const { return inputs.count(name) > 0; }

    // Op-amp swing follows the supply (nominal +/-15 V gives the model's own swing).
    void setSupply(double vp, double vn)
    {
        const double up = 13.0 + (vp - 15.0), dn = 13.0 + (-vn - 15.0);
        for (int h : opampList)
            c.setOpAmpRails(h, up, dn);
    }

    double node(const std::string& name) const { return c.v(c.hasNode(name)); }
    double opamp(const std::string& name) const { return c.opampOut(nl.opamps.at(name)); }
    bool step() { return c.step(); }
    double supplyCurrent() const { return c.opampLoadCurrent(); }
    void setResistor(const std::string& ref, double ohms) { c.setResistance(nl.resistors.at(ref), ohms); }
    bool dc() { return c.solveDC(); }
    void preset(const std::map<std::string, double>& v) { c.presetNodes(v); }

    std::map<std::string, int> rails, inputs, loads;
    std::map<std::string, double> swing;
    std::vector<int> opampList;
    int hVP = -1, hVN = -1, hV45 = -1, hTube = -1;
};
} // namespace tubechannel::circuit
