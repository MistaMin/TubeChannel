#pragma once

#include <atomic>
#include <string>

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

} // namespace tubechannel::channel
