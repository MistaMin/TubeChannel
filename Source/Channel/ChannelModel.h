#pragma once

// One complete channel: input stage, tube stage, insert-free signal path, opto compressor, four-band
// EQ, output stage and the power supply, all as circuits solved at the oversampled rate.
//
//   process(vin, vsc)  one oversampled sample in volts (differential at the XLR input; the sidechain
//                      signal is level matched to the detector input), returns the balanced output in volts.
//
// The stages are chained one way: an op-amp output drives the next stage's input through its
// output resistance. The supply is solved at its own fixed rate and read at the oversampled rate.

#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <string>

#include "../Circuit/Block.h"
#include "Vactrol.h"
#include "../Circuit/CircuitPair.h"
#include "PowerSupply.h"

#ifdef TC_PROFILE
#define TC_T(slot) { const auto now_ = std::chrono::steady_clock::now(); profile[slot] += std::chrono::duration<double>(now_ - tprev).count(); tprev = now_; }
#else
#define TC_T(slot)
#endif

namespace tubechannel::channel
{
// ---- controls (what the front panel does) ----------------------------------------------------------------
struct ChannelControls
{
    // input
    bool instrument = false;      // front jack in use: the mic stage is bypassed
    double gain = 0.0;            // 0..1
    bool gainPlus20 = false;      // tube stage +20 dB
    double impedance = 0.4;       // 0..1 -> 150 ohm .. 3.4 k
    bool phantom = false;
    bool invert = false;
    double lowCut = 0.0;          // 0..1 -> 11 .. 250 Hz
    bool plateHigh = true;
    double preOut = 1.0;
    // compressor
    bool compActive = true;
    double threshold = 0.25, ratio = 0.3, attack = 0.5, release = 0.5, compOut = 0.88, vactrolCal = 0.5;
    bool autoDetect = false;
    bool externalSidechain = false;
    bool limiter = false;         // RATIO knob popped: the circuit compressor is off, a brickwall limiter takes over
    double limiterRelease = 0.2;  // seconds
    // equaliser
    bool eqActive = true;
    double low = 0.5, high = 0.5;
    bool loShift40 = true;        // false = 120 Hz
    bool hiShift6k = false;       // false = 18 kHz
    double hmLevel = 0.5, hmFreq = 0.5, lmLevel = 0.5, lmFreq = 0.5;
    bool hmX10 = false, lmX10 = false;
    bool hmNarrow = true, lmNarrow = true;
    // output
    double master = 0.8;
    bool outPlus4 = true;         // false = -10 dBV
    // plugin
    double micSourceOhms = 150.0;
    double instSourceOhms = 10000.0;

    bool operator==(const ChannelControls&) const = default;
};

struct Telemetry
{
    std::atomic<float> outPeak { 0.f }, grDb { 0.f }, tubeDrive { 0.f }, clip { 0.f };
    std::atomic<float> p15 { 15.f }, n15 { -15.f }, p25 { 25.f }, p45 { 45.f }, p75 { 75.f }, p150 { 150.f }, tubeP { 150.f };
    std::atomic<float> mainsPhase { 0.f }, heaterCurrent { 0.f }, loadP15 { 0.f }, loadTube { 0.f };
    std::atomic<float> inPeak { 0.f }, outRms { 0.f };
    std::atomic<float> vuTap[4];     // rms volts at: preamp out, compressor out, EQ out, main out
    std::atomic<float> clipPre { 0.f };
    // recent supply rail history at the supply's own rate (about 8 kHz), for the power panel scope
    static constexpr int kScope = 256;
    std::atomic<float> scope25[kScope], scope150[kScope], scopeLoad[kScope];
    std::atomic<int> scopeWrite { 0 };
    Telemetry() { for (auto& v : vuTap) v = 0.f; for (int i = 0; i < kScope; ++i) { scope25[i] = 26.f; scope150[i] = 150.f; scopeLoad[i] = 0.f; } }
};

struct NetlistSet
{
    std::string mic, inst, tube, preout, comp, det, eq1, eq2, eq3, eq4, out, psu;
};

// small, cheap, roughly Gaussian white noise (sum of four uniforms)
class Noise
{
public:
    explicit Noise(unsigned seed = 1) : s(seed * 2654435761u + 12345u) {}
    double next()
    {
        double a = 0;
        for (int i = 0; i < 4; ++i)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            a += double(s) * (1.0 / 4294967296.0);
        }
        return (a - 2.0) * 1.7320508;     // unit variance
    }
private:
    unsigned s;
};

// Brickwall peak limiter: the gain drops at once so that no sample exceeds the ceiling, and recovers with a one-pole release.
class BrickwallLimiter
{
public:
    void prepare(double sampleRate) { fs = sampleRate; reset(); }
    void reset() { gain = 1.0; }
    void setCeiling(double volts) { ceiling = std::max(volts, 1e-6); }
    void setRelease(double seconds) { aRel = 1.0 - std::exp(-1.0 / (std::max(seconds, 1e-3) * fs)); }
    double process(double x)
    {
        const double a = std::fabs(x);
        const double need = a > ceiling ? ceiling / a : 1.0;
        gain = need < gain ? need : gain + (1.0 - gain) * aRel;
        return x * gain;
    }
    double gainReductionDb() const { return -20.0 * std::log10(std::max(gain, 1e-6)); }
private:
    double fs = 48000.0, ceiling = 1.0, gain = 1.0, aRel = 1e-4;
};

class ChannelModel
{
public:
    static constexpr double kSidechainMatch = 0.6563;
    static constexpr double kDetectorRate = 48000.0;    // the detector follows envelopes: about 48 kHz however far the audio is oversampled
    static constexpr int kControlInterval = 64;       // oversampled samples between control updates

    void prepare(double oversampledRate, const NetlistSet& nets, unsigned seed, bool tolerances,
                 const PowerSupplySettings& psuSettings = {})
    {
        using namespace circuit;
        fs = oversampledRate;
        dt = 1.0 / fs;
        ToleranceSettings tol;
        tol.enabled = tolerances;
        tol.seed = seed;
        noise[0] = Noise(seed + 1); noise[1] = Noise(seed + 7); noise[2] = Noise(seed + 13);

        mic.build(nets.mic + "RSP SP XH 75\nRSN SN XC 75\n", dt, tol, { "VP", "VN", "V45" }, { { "SP", 1e-3 }, { "SN", 1e-3 } });
        inst.build(nets.inst, dt, tol, {}, { { "O1", 10.0 }, { "ISRC", 1e-3 } });
        tube.build(nets.tube, dt, tol, { "VN", "TUBEP" }, { { "O13B", 10.0 } });
        preout.build(nets.preout, dt, tol, {}, { { "V1OUT", 10.0 } });
        comp.build(nets.comp, dt, tol, { "VP", "VN", "V45" }, { { "J6T", 1.0 } });
        // The detector only has to follow envelopes: it runs at about 48 kHz however far the audio is oversampled,
        // fed with the average of the samples in between.
        detDecim = std::max(1, int(std::lround(fs / kDetectorRate)));
        det.build(nets.det, dt * detDecim, tol, { "VP", "VN" }, { { "O6B", 10.0 } });
        det.c.maxIter = 20;                         // a rectifier crossing can take a dozen iterations; retrying with damping costs far more
        det.c.extrapOrder = 0;                      // the rectifier switches hard: start each step from the last solution
        eq1.build(nets.eq1, dt, tol, { "VP", "VN" }, { { "COUT", 10.0 } });
        eq2.build(nets.eq2, dt, tol, {}, { { "X", 10.0 } });
        eq3.build(nets.eq3, dt, tol, {}, { { "O10A", 10.0 } });
        eq4.build(nets.eq4, dt, tol, {}, { { "O8A", 10.0 } });
        out.build(nets.out + "RL OUTP OUTN 600\n", dt, tol, {}, { { "O8C", 10.0 }, { "EQIN", 1.0 } });
        blocks = { &mic, &inst, &tube, &preout, &comp, &det, &eq1, &eq2, &eq3, &eq4, &out };
        // Stages with injected noise: accept an update below ~30 uV (chord error then stays near 1e-7 V)
        mic.c.tolStep = tube.c.tolStep = comp.c.tolStep = 3e-5;

        psu.prepare(nets.psu, psuSettings, tol);
        psu.settle(0.8, controls.plateHigh);
        psuInterval = std::max(1.0, fs / PowerSupply::kRate);
        psuCount = 0.0;
        loadAcc[0] = loadAcc[1] = loadAcc[2] = loadAcc[3] = 0.0;
        loadN = 0;

        resolvePorts();
        vuCoef = 1.0 - std::exp(-1.0 / (0.3 * fs));
        comp.c.setLowRankResistor(P.compRLDR);
        vactrol.prepare(fs);
        limiter.prepare(fs);
        applyControls(true);
        pushRails();
        for (auto* b : blocks) { link(); b->dc(); }
        for (int pass = 0; pass < 2; ++pass)
            for (auto* b : blocks) { link(); b->dc(); }
        tickCount = 0;
    }

    void setControls(const ChannelControls& c)
    {
        if (!(c == pending))
        {
            pending = c;
            controlsDirty = true;
        }
    }

    // One oversampled sample: volts in (differential at the XLR, or the instrument tip), volts of sidechain at the
    // detector, volts out. The sample is a sequence of stages, one per circuit; processPair() (stereo) runs two models
    // through the stages together so that the circuit solves can share SIMD lanes.
    static constexpr int kStages = 11;
    static inline unsigned gPairSkip = 0;       // debugging: stages that are stepped by the scalar circuits

    double process(double vin, double vsc)
    {
        beginSample(vin, vsc);
#ifdef TC_PROFILE
        auto tprev = std::chrono::steady_clock::now();
        for (int k = 0; k < kStages; ++k)
        {
            if (auto* b = stage(k))
                b->step();
            const auto now = std::chrono::steady_clock::now();
            profile[k] += std::chrono::duration<double>(now - tprev).count();
            tprev = now;
        }
#else
        for (int k = 0; k < kStages; ++k)
            if (auto* b = stage(k))
                b->step();
#endif
        return endSample();
    }

    // Two channels in lockstep. The stage blocks must have been linked with linkPair().
    static void processPair(ChannelModel& A, ChannelModel& B, double vinA, double vscA, double vinB, double vscB, double& yA, double& yB)
    {
        A.beginSample(vinA, vscA);
        B.beginSample(vinB, vscB);
        for (int k = 0; k < kStages; ++k)
        {
            auto* a = A.stage(k);
            auto* b = B.stage(k);
            if (a != nullptr && b != nullptr && !((gPairSkip >> k) & 1u))
                A.pairs[size_t(k)]->step();
            else
            {
                if (a != nullptr) a->step();
                if (b != nullptr) b->step();
            }
        }
        yA = A.endSample();
        yB = B.endSample();
    }

    // Build the paired solvers for two models prepared from the same netlists.
    static void linkPair(ChannelModel& A, ChannelModel& B)
    {
        for (int k = 0; k < kStages; ++k)
            A.pairs[size_t(k)] = std::make_unique<circuit::CircuitPair>(A.stageBlock(k).c, B.stageBlock(k).c);
    }

private:
    circuit::Block& stageBlock(int k)
    {
        circuit::Block* order[kStages] = { &mic, &inst, &tube, &preout, &comp, &det, &eq1, &eq2, &eq3, &eq4, &out };
        return *order[k];
    }

    void beginSample(double vin, double vsc)
    {
        sVin = vin;
        sVsc = vsc;
        if (++tickCount >= kControlInterval)
        {
            tickCount = 0;
            controlTick();
        }
        // ---- supply ----
        if (psuOn && (psuCount += 1.0) >= psuInterval)
        {
            psuCount -= psuInterval;
            const double n = double(std::max(loadN, 1));
            psu.step(loadAcc[0] / n + 0.075, loadAcc[1] / n + 0.075, loadAcc[2] / n + 0.004, loadAcc[3] / n, controls.plateHigh);
            lastLoadP15 = loadAcc[0] / n + 0.075;
            lastLoadTube = loadAcc[3] / n;
            if (tel != nullptr)
            {
                const int w = tel->scopeWrite.load(std::memory_order_relaxed);
                tel->scope25[w].store(float(psu.p25()), std::memory_order_relaxed);
                tel->scope150[w].store(float(psu.tubeP()), std::memory_order_relaxed);
                tel->scopeLoad[w].store(float(lastLoadP15), std::memory_order_relaxed);
                tel->scopeWrite.store((w + 1) % Telemetry::kScope, std::memory_order_release);
            }
            loadAcc[0] = loadAcc[1] = loadAcc[2] = loadAcc[3] = 0.0;
            loadN = 0;
            pushRails();
        }
    }

    // The work that precedes the circuit of stage k (it also reads the result of the stage before). Returns the block
    // to step, or nullptr when this stage has nothing to solve for this sample.
    circuit::Block* stage(int k)
    {
        const double sg = std::sqrt(0.5 * fs);                     // noise density -> per-sample sigma
        switch (k)
        {
            case 0:                                                // mic stage
                if (controls.instrument)
                    return nullptr;
                {
                    const double vn = noiseOn ? 1.8e-9 * sg * noise[0].next() : 0.0;      // 150 ohm thermal + amplifier noise at the XLR
                    mic.setSourceH(P.micSP, 0.5 * (sVin + vn));
                    mic.setSourceH(P.micSN, -0.5 * (sVin + vn));
                }
                return &mic;
            case 1:                                                // instrument / line input stage
                if (!controls.instrument)
                {
                    inst.setSourceH(P.instO1, mic.opampOutH(P.micA1A));
                    inst.setSourceH(P.instISRC, 0.0);
                }
                else
                {
                    inst.setSourceH(P.instO1, 0.0);
                    inst.setSourceH(P.instISRC, sVin);
                }
                return &inst;
            case 2:                                                // tube stage
                tube.setSourceH(P.tubeIn, inst.opampOutH(P.instA13B) + (noiseOn ? 13e-9 * sg * noise[1].next() : 0.0));   // grid stopper thermal noise
                return &tube;
            case 3:
                preout.setSourceH(P.preIn, tube.nodeV(P.tubeV1OUT));
                return &preout;
            case 4:                                                // compressor audio path
                comp.setSourceH(P.compIn, preout.nodeV(P.preJ6T) + (noiseOn ? 5e-9 * sg * noise[2].next() : 0.0));
                return &comp;
            case 5:                                                // compressor detector
            {
                // The detector listens to the compressor's own output (feedback topology) or to the sidechain input. An
                // external signal is scaled like the main input would be at minimum gain: at its default setting the path
                // from the input to this node loses 3.66 dB, so equal digital levels give equal detector levels.
                const double own = controls.externalSidechain ? kSidechainMatch * sVsc : comp.nodeV(P.compO6B);
                lastDetector = std::fabs(own);
                // linked stereo: both detectors hear the louder of the two channels
                const double detector = (linkEnabled && linkIn > lastDetector) ? std::copysign(linkIn, own) : own;
                detSum += detector;
                detStepped = false;
                if (!(controls.compActive && !controls.limiter))
                {
                    detSum = 0.0;
                    detCount = 0;
                }
                else if (++detCount >= detDecim)
                {
                    det.setSourceH(P.detIn, detSum / double(detDecim));
                    detSum = 0.0;
                    detCount = 0;
                    detStepped = true;
                    return &det;
                }
                return nullptr;
            }
            case 6:                                                // first EQ stage; the detector result is applied here
            {
                if (detStepped)
                    ledCurrent = det.c.resistorCurrent(P.detRled);
                if (!(controls.compActive && !controls.limiter))
                    ledCurrent = 0.0;
                const double rldr = vactrol.process(ledCurrent);
                if (rldr != lastLdr && std::fabs(rldr - lastLdr) > 2e-5 * lastLdr)
                {
                    lastLdr = rldr;
                    comp.setParam("LDRR", rldr);
                    comp.c.setResistance(P.compRLDR, rldr);
                }
                double compOut = comp.nodeV(P.compCOUT);
                if (controls.limiter)
                    compOut = limiter.process(compOut);
                eq1.setSourceH(P.eq1In, compOut);
                lastCompOut = compOut;
                return &eq1;
            }
            case 7:
                eq2.setSourceH(P.eq2In, eq1.opampOutH(P.eq1A10B));
                return &eq2;
            case 8:
                eq3.setSourceH(P.eq3In, eq2.opampOutH(P.eq2A10A));
                return &eq3;
            case 9:
                eq4.setSourceH(P.eq4In, eq3.opampOutH(P.eq3A8A));
                return &eq4;
            default:
                out.setSourceH(P.outIn, eq4.opampOutH(P.eq4A8C));
                out.setSourceH(P.outEQIN, eq1.nodeV(P.eq1EQIN));
                return &out;
        }
    }

    double endSample()
    {
        // ---- supply loads: sampled when the supply next steps ----
        if (psuOn && psuCount + 1.0 >= psuInterval)
        {
            const double ia = mic.supplyCurrent() + tube.supplyCurrent() + comp.supplyCurrent() + eq1.supplyCurrent() +
                              inst.supplyCurrent() + preout.supplyCurrent() + eq2.supplyCurrent() + eq3.supplyCurrent() +
                              eq4.supplyCurrent() + out.supplyCurrent() + det.supplyCurrent();
            loadAcc[0] += mic.currentVP() + comp.currentVP() + eq1.currentVP() + 0.5 * ia;
            loadAcc[1] += -(mic.currentVN() + tube.currentVN() + comp.currentVN() + det.currentVN() + eq1.currentVN()) + 0.5 * ia;
            loadAcc[2] += mic.currentV45() + comp.currentV45();
            loadAcc[3] += tube.currentTube();
            ++loadN;
        }
        const double y = out.nodeV(P.outP) - out.nodeV(P.outN);
        {
            const double taps[4] = { preout.nodeV(P.prePOW), comp.nodeV(P.compCOUT), out.nodeV(P.outV3OUT), y };
            for (int i = 0; i < 4; ++i)
                tapMs[i] += (taps[i] * taps[i] - tapMs[i]) * vuCoef;
        }
        const double ay = std::fabs(y);
        peak = std::max(peak * peakDecay, ay);
        drive = std::max(drive * peakDecay, std::fabs(tube.nodeV(P.tubeO2B)));
        return y;
    }

public:
    // call from the control thread / block boundary to publish meters
    void publish(Telemetry& t) const
    {
        t.outPeak.store(float(peak));
        t.grDb.store(float(controls.limiter ? limiter.gainReductionDb() : vactrol.gainReductionDb(12.5e3 * controls.vactrolCal + 2.21e3)));
        t.tubeDrive.store(float(drive));
        t.clip.store(peak > 12.5 ? 1.f : 0.f);
        for (int i = 0; i < 4; ++i) t.vuTap[i].store(float(std::sqrt(tapMs[i])));
        t.p15.store(float(psu.p15())); t.n15.store(float(psu.n15()));
        t.p25.store(float(psu.p25())); t.p45.store(float(psu.p45()));
        t.p75.store(float(psu.p75())); t.p150.store(float(psu.p150()));
        t.tubeP.store(float(psu.tubeP()));
        t.mainsPhase.store(float(psu.mainsPhase()));
        t.heaterCurrent.store(float(psu.heaterCurrent()));
        t.loadP15.store(float(lastLoadP15));
        t.loadTube.store(float(lastLoadTube));
    }

    void setPeakDecay(double d) { peakDecay = d; }
    void attach(Telemetry* t) { tel = t; }
    void setNoiseEnabled(bool on) { noiseOn = on; }
    // Off: the rails stay at their present values and the supply circuit is not solved.
    void setPsuEnabled(bool on)
    {
        if (on != psuOn)
        {
            psuOn = on;
            psuCount = 0.0;
            loadAcc[0] = loadAcc[1] = loadAcc[2] = loadAcc[3] = 0.0;
            loadN = 0;
        }
    }
    // stereo link: the other channel's detector level (volts) as of the last oversampled sample
    void setLink(bool on, double otherDetector) { linkEnabled = on; linkIn = otherDetector; }
    double detectorLevel() const { return lastDetector; }
    double vactrolGainReduction() const { return vactrol.gainReductionDb(14.7e3); }
    double vactrolLed() const { return vactrol.ledCurrent(); }
    double compressorOutput() const { return lastCompOut; }      // volts into the EQ, after the limiter
    void setMains(double hz, double scale) { psu.settings.mainsHz = hz; psu.settings.mainsScale = scale; }

#ifdef TC_PROFILE
    double profile[16] = {};
#endif
    ChannelControls controls;
    Vactrol vactrol;
    BrickwallLimiter limiter;
    PowerSupply psu;
    circuit::Block mic, inst, tube, preout, comp, det, eq1, eq2, eq3, eq4, out;
    std::vector<circuit::Block*> blocks;

private:
    static double aLaw(double k) { return (std::pow(10.0, 2.0 * k) - 1.0) / 99.0; }       // audio taper

public:
    // THRESHOLD pot position for a knob setting. The label is the level (dBm) at which the detector starts to
    // act: gain reduction of about 1 dB (measured on the closed loop) needs about 0.76 V peak after A3A, which
    // the pot, loaded by R94, sets up. Solved for the wiper position by bisection.
    static double thresholdWiper(double k)
    {
        const double dbm = -20.0 + 40.0 * std::min(std::max(k, 0.0), 1.0);
        const double target = 0.762 / (1.0960 * std::pow(10.0, dbm / 20.0));      // gain from the node to the A3A output
        auto gainAt = [](double w)
        {
            const double rUp = 10e3 * w, rb = 10e3 * (1.0 - w) + 100.0, rp = rb * 2210.0 / (rb + 2210.0);
            return rp / (rUp + rp) * (12e3 / 2210.0);
        };
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 40; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (gainAt(mid) > target ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

    // RATIO rheostat (VR5, 100k B) for a knob setting. Measured on the closed loop (detector, vactrol, divider) at
    // 6 to 12 dB above the onset: 25k gives about 1.9:1, 3k about 3.6:1, 0 about 6.7:1, which is as far as this
    // circuit goes; the knob spans that range.
    static double ratioPotOhms(double k)
    {
        const double r = std::min(std::max(k, 0.0), 1.0);
        return 25e3 * (1.0 - r) * (1.0 - r) * (1.0 - r);
    }

private:

    // Knob positions -> the parameters the netlists are written in.
    void mapControls(const ChannelControls& c, std::map<std::string, double>& p) const
    {
        p["INST"] = c.instrument ? 1.0 : 0.0;
        p["ZINST"] = c.instSourceOhms;
        // gain: dB linear over the stage's range
        if (!c.instrument)
        {
            const double g = std::pow(10.0, (40.6 * c.gain) / 20.0);
            p["GAINPOT"] = std::max(0.5, 4990.0 / std::max(g - 0.5, 1e-3) - 47.0);
        }
        else
        {
            const double g = std::pow(10.0, (3.5 + 30.5 * c.gain) / 20.0);
            p["GAINPOT"] = std::max(0.5, 4990.0 / std::max(g - 1.0, 1e-3) - 100.0);
        }
        p["IMP"] = 10e3 * aLaw(c.impedance);
        p["PH48"] = c.phantom ? 1.0 : 0.0;
        p["INVERT"] = c.invert ? 1.0 : 0.0;
        {
            const double f = 11.2 * std::pow(250.0 / 11.2, c.lowCut);
            p["LOCUT"] = std::max(0.5, std::min(10e3, 1.0 / (6.283185307 * f * 1.36e-6) - 470.0));
        }
        p["TUBE20"] = c.gainPlus20 ? 1.0 : 0.0;
        p["HV"] = c.plateHigh ? 1.0 : 0.0;
        p["PREOUT"] = c.preOut;
        p["COMP"] = c.compActive ? 1.0 : 0.0;
        p["VTRIM"] = c.vactrolCal;
        p["CMPOUT"] = std::max(0.001, aLaw(c.compOut));
        p["EQON"] = c.eqActive ? 1.0 : 0.0;
        p["LOW"] = c.low;
        p["HIGH"] = c.high;
        p["LOSH"] = c.loShift40 ? 1.0 : 0.0;
        p["HISH"] = c.hiShift6k ? 1.0 : 0.0;
        p["HMID"] = c.hmLevel;
        p["LMID"] = c.lmLevel;
        auto freqFraction = [](double k) { return (108.2e3 * std::pow(8.2 / 108.2, k) - 8.2e3) / 100e3; };
        p["HMF"] = std::max(0.0, freqFraction(c.hmFreq));
        p["LMF"] = std::max(0.0, freqFraction(c.lmFreq));
        p["HMLO"] = c.hmX10 ? 0.0 : 1.0;
        p["LMLO"] = c.lmX10 ? 0.0 : 1.0;
        p["HMNAR"] = c.hmNarrow ? 1.0 : 0.0;
        p["LMNAR"] = c.lmNarrow ? 1.0 : 0.0;
        p["MASTER"] = c.master;
        // compressor detector pots
        p["THRW"] = thresholdWiper(c.threshold);
        p["RATR"] = ratioPotOhms(c.ratio);
        p["ATTR"] = 50e3 * aLaw(c.attack);         // VR14A/B, 100k A taper, both gangs in parallel
        p["RELW"] = c.release;
        p["AUTO"] = c.autoDetect ? 1.0 : 0.0;
    }

    void applyControls(bool immediate)
    {
        const bool wasLimiter = controls.limiter;
        controls = pending;
        // the limiter ceiling is the THRESHOLD label (dBm, peak) at the compressor output
        limiter.setCeiling(1.0960 * std::pow(10.0, (-20.0 + 40.0 * controls.threshold) / 20.0));
        limiter.setRelease(controls.limiterRelease);
        if (controls.limiter && !wasLimiter)
        {
            limiter.reset();
            vactrol.reset();                // the circuit compressor is out: the photoresistor goes dark at once
            lastLdr = 1e6;
            comp.setParam("LDRR", 1e6);
            comp.c.setResistance(P.compRLDR, 1e6);
        }
        std::map<std::string, double> target;
        mapControls(controls, target);
        if (immediate || smoothed.empty())
            smoothed = target;
        for (auto& [k, v] : target)
        {
            // continuous controls glide, switches jump
            const bool isSwitch = k == "INST" || k == "PH48" || k == "INVERT" || k == "TUBE20" || k == "HV" || k == "COMP" ||
                                  k == "EQON" || k == "LOSH" || k == "HISH" || k == "HMLO" || k == "LMLO" || k == "HMNAR" ||
                                  k == "LMNAR" || k == "ZINST" || k == "AUTO";
            double& cur = smoothed[k];
            cur = (immediate || isSwitch) ? v : cur + (v - cur) * 0.35;
        }
        for (auto* b : blocks)
        {
            for (auto& [k, v] : smoothed)
                if (!(b == &comp && k == "LDRR"))
                    b->setParam(k, v);
            b->applyParams();
        }
        comp.setParam("LDRR", lastLdr);
        // the instrument source impedance and the mic source impedance
        mic.setResistor("RSP", std::max(1.0, 0.5 * controls.micSourceOhms));
        mic.setResistor("RSN", std::max(1.0, 0.5 * controls.micSourceOhms));
    }

    void controlTick()
    {
        if (controlsDirty || !smoothSettled)
        {
            controlsDirty = false;
            applyControls(false);
            // keep gliding until the smoothed values reach the targets
            std::map<std::string, double> target;
            mapControls(controls, target);
            smoothSettled = true;
            for (auto& [k, v] : target)
                if (std::fabs(smoothed[k] - v) > 1e-6 * (1.0 + std::fabs(v)))
                    smoothSettled = false;
        }
    }

    void pushRails()
    {
        const double vp = psu.p15(), vn = psu.n15(), v45 = psu.p45(), tp = psu.tubeP();
        for (auto* b : blocks)
        {
            b->setRails(vp, vn, v45, tp);
            b->setSupply(vp, vn);
        }
    }

    void link()
    {
        inst.setInput("O1", mic.opamp("A1A"));
        tube.setInput("O13B", inst.opamp("A13B"));
        preout.setInput("V1OUT", tube.node("V1OUT"));
        comp.setInput("J6T", preout.node("J6T"));
        det.setInput("O6B", comp.node("O6B"));
        eq1.setInput("COUT", comp.node("COUT"));
        eq2.setInput("X", eq1.opamp("A10B"));
        eq3.setInput("O10A", eq2.opamp("A10A"));
        eq4.setInput("O8A", eq3.opamp("A8A"));
        out.setInput("O8C", eq4.opamp("A8C"));
        out.setInput("EQIN", eq1.node("EQIN"));
    }

    struct Ports
    {
        int micSP = 0, micSN = 0, micA1A = 0, instO1 = 0, instISRC = 0, instA13B = 0, tubeIn = 0, tubeV1OUT = 0, tubeO2B = 0;
        int preIn = 0, preJ6T = 0, compIn = 0, compO6B = 0, compCOUT = 0, compRLDR = 0, eq1In = 0, eq1A10B = 0, eq1EQIN = 0;
        int prePOW = 0, outV3OUT = 0, detIn = 0, detRled = 0;
        int eq2In = 0, eq2A10A = 0, eq3In = 0, eq3A8A = 0, eq4In = 0, eq4A8C = 0, outIn = 0, outEQIN = 0, outP = 0, outN = 0;
    } P;

    void resolvePorts()
    {
        P.micSP = mic.inputHandle("SP"); P.micSN = mic.inputHandle("SN"); P.micA1A = mic.opampHandle("A1A");
        P.instO1 = inst.inputHandle("O1"); P.instISRC = inst.inputHandle("ISRC"); P.instA13B = inst.opampHandle("A13B");
        P.tubeIn = tube.inputHandle("O13B"); P.tubeV1OUT = tube.nodeId("V1OUT"); P.tubeO2B = tube.nodeId("O2B");
        P.prePOW = preout.nodeId("P7A"); P.outV3OUT = out.nodeId("V3OUT");
        P.preIn = preout.inputHandle("V1OUT"); P.preJ6T = preout.nodeId("J6T");
        P.compIn = comp.inputHandle("J6T"); P.compO6B = comp.nodeId("O6B"); P.compCOUT = comp.nodeId("COUT");
        P.compRLDR = comp.resistorHandle("RLDR");
        P.detIn = det.inputHandle("O6B"); P.detRled = det.resistorHandle("RLED");
        P.eq1In = eq1.inputHandle("COUT"); P.eq1A10B = eq1.opampHandle("A10B"); P.eq1EQIN = eq1.nodeId("EQIN");
        P.eq2In = eq2.inputHandle("X"); P.eq2A10A = eq2.opampHandle("A10A");
        P.eq3In = eq3.inputHandle("O10A"); P.eq3A8A = eq3.opampHandle("A8A");
        P.eq4In = eq4.inputHandle("O8A"); P.eq4A8C = eq4.opampHandle("A8C");
        P.outIn = out.inputHandle("O8C"); P.outEQIN = out.inputHandle("EQIN");
        P.outP = out.nodeId("OUTP"); P.outN = out.nodeId("OUTN");
    }

    double fs = 192000.0, dt = 1.0 / 192000.0;
    Noise noise[3];
    ChannelControls pending;
    bool controlsDirty = true, smoothSettled = true;
    std::map<std::string, double> smoothed;
    int tickCount = 0;
    double psuInterval = 2.0, psuCount = 0.0;
    double loadAcc[4] = {};
    int loadN = 0;
    double lastLoadP15 = 0.0, lastLoadTube = 0.0;
    double lastLdr = 1e6, lastCompOut = 0.0;
    double sVin = 0.0, sVsc = 0.0;
    bool detStepped = false;
    std::array<std::unique_ptr<circuit::CircuitPair>, kStages> pairs;
    int detDecim = 1, detCount = 0;
    double detSum = 0.0, ledCurrent = 0.0;
    double tapMs[4] = {}, vuCoef = 1e-5;
    Telemetry* tel = nullptr;
    bool noiseOn = true, linkEnabled = false, psuOn = true;
    double linkIn = 0.0, lastDetector = 0.0;
    double peak = 0.0, drive = 0.0, peakDecay = 0.99995;
};
} // namespace tubechannel::channel
