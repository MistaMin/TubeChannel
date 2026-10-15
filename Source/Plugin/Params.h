#pragma once

// The plugin's parameters and their mapping to the channel model's controls.
//
// Every front-panel control exists twice: channel A (no suffix) and channel B ("_b", used in
// Dual-mono). In Stereo-linked mode channel B follows channel A. Plain ASCII names throughout; the
// emoticon for Derp mode is drawn by the UI.

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Channel/ChannelModel.h"

namespace tc::params
{
// ---- ids ---------------------------------------------------------------------------------------------
namespace id
{
// per channel (append "_b" for channel B)
inline constexpr const char* instrument = "instrument";
inline constexpr const char* gain = "gain";
inline constexpr const char* gain20 = "gain20";
inline constexpr const char* impedance = "impedance";
inline constexpr const char* phantom = "phantom";
inline constexpr const char* invert = "invert";
inline constexpr const char* lowcut = "lowcut";
inline constexpr const char* plateHigh = "platehigh";
inline constexpr const char* preOut = "preout";
inline constexpr const char* comp = "comp";
inline constexpr const char* threshold = "threshold";
inline constexpr const char* ratio = "ratio";
inline constexpr const char* attack = "attack";
inline constexpr const char* release = "release";
inline constexpr const char* autoDetect = "autodetect";
inline constexpr const char* compOut = "compout";
inline constexpr const char* extSc = "extsc";
inline constexpr const char* ratioLimit = "ratiolimit";
inline constexpr const char* eq = "eq";
inline constexpr const char* low = "low";
inline constexpr const char* loShift = "loshift";
inline constexpr const char* high = "high";
inline constexpr const char* hiShift = "hishift";
inline constexpr const char* hmLevel = "hmlevel";
inline constexpr const char* hmFreq = "hmfreq";
inline constexpr const char* hmX10 = "hmx10";
inline constexpr const char* hmNarrow = "hmnarrow";
inline constexpr const char* lmLevel = "lmlevel";
inline constexpr const char* lmFreq = "lmfreq";
inline constexpr const char* lmX10 = "lmx10";
inline constexpr const char* lmNarrow = "lmnarrow";
inline constexpr const char* master = "master";
inline constexpr const char* outLevel = "outlevel";

// global
inline constexpr const char* inputTrim = "inputtrim";
inline constexpr const char* outputTrim = "outputtrim";
inline constexpr const char* alignment = "alignment";
inline constexpr const char* micZ = "micz";
inline constexpr const char* instZ = "instz";
inline constexpr const char* offlineBounce = "offlinebounce";
inline constexpr const char* offlineBounceEnabled = "offlinebounceenabled";
inline constexpr const char* oversampling = "oversampling";
inline constexpr const char* quality = "quality";
inline constexpr const char* eco = "ecomode";
inline constexpr const char* linearPhase = "linearphase";
inline constexpr const char* stereoLink = "stereolink";
inline constexpr const char* dualMono = "dualmono";
inline constexpr const char* mains = "mains";
inline constexpr const char* mainsVolts = "mainsvolts";
inline constexpr const char* tolerances = "tolerances";
inline constexpr const char* seed = "seed";
inline constexpr const char* noise = "noise";
inline constexpr const char* psuModel = "psumodel";
} // namespace id

inline juce::String idFor(const char* base, bool channelB) { return channelB ? juce::String(base) + "_b" : juce::String(base); }

// ---- display ----------------------------------------------------------------------------------------
inline double gainDb(double k) { return 40.6 * k; }
inline double impedanceOhms(double k) { return 150.0 + 3250.0 * (std::pow(10.0, 2.0 * k) - 1.0) / 99.0; }
inline double lowCutHz(double k) { return 11.2 * std::pow(250.0 / 11.2, k); }
inline double thresholdDbm(double k) { return -20.0 + 40.0 * k; }
// Effective compression ratio of the detector circuit for a RATIO knob setting: measured on the closed loop (detector,
// vactrol and the series divider of the audio path) between 6 and 12 dB above the onset, for the rheostat value the knob
// sets. It is a soft-knee optical compressor: the ratio falls again deeper into the curve.
inline double ratioValue(double k)
{
    static constexpr double kR[] = { 0.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 8000.0, 12000.0, 20000.0, 40000.0 };
    static constexpr double kRatio[] = { 6.71, 5.61, 4.95, 4.13, 3.63, 3.06, 2.60, 2.25, 1.89, 1.54 };
    const double r = tubechannel::channel::ChannelModel::ratioPotOhms(k);
    double eff;
    if (r >= kR[9]) eff = kRatio[9];
    else
    {
        int i = 1;
        while (r > kR[i]) ++i;
        eff = kRatio[i - 1] + (kRatio[i] - kRatio[i - 1]) * (r - kR[i - 1]) / (kR[i] - kR[i - 1]);
    }
    // the labelled scale: the measured 1.8 .. 6.7 spread over 2:1 .. 7:1
    return std::min(7.0, std::max(2.0, 2.0 + 5.0 * (eff - 1.8) / (6.71 - 1.8)));
}
// the knob position that gives a ratio (bisection; the ratio falls as the knob turns down)
inline double ratioKnobFor(double ratio)
{
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 30; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        (ratioValue(mid) < ratio ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}
// Attack and release as measured on the detector circuit with the vactrol (time to 63 percent of the final gain
// reduction, time to fall to 37 percent), +12 dB over the onset, Manual mode.
inline double attackSeconds(double k)
{
    static constexpr double kR[] = { 0.0, 5e3, 20e3, 50e3 };
    static constexpr double kT[] = { 4.3e-3, 10.8e-3, 21.3e-3, 33.3e-3 };
    const double r = 50e3 * (std::pow(10.0, 2.0 * k) - 1.0) / 99.0;          // VR14, A taper, both gangs in parallel
    int i = 1;
    while (i < 3 && r > kR[i]) ++i;
    return kT[i - 1] + (kT[i] - kT[i - 1]) * (r - kR[i - 1]) / (kR[i] - kR[i - 1]);
}
inline double releaseSeconds(double k)
{
    static constexpr double kK[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
    static constexpr double kT[] = { 0.097, 0.137, 0.185, 0.267, 1.107 };
    int i = 1;
    while (i < 4 && k > kK[i]) ++i;
    return kT[i - 1] + (kT[i] - kT[i - 1]) * (k - kK[i - 1]) / (kK[i] - kK[i - 1]);
}
inline double eqGainDb(double k) { return (k - 0.5) * 24.0; }
// panel ranges of the two mid bands as the circuit actually has them (low range / x10 range)
inline double lmFreqHz(double k, bool x10) { return (x10 ? 178.0 : 16.0) * std::pow((x10 ? 2350.0 : 214.0) / (x10 ? 178.0 : 16.0), k); }
inline double hmFreqHz(double k, bool x10) { return (x10 ? 1800.0 : 162.0) * std::pow((x10 ? 23000.0 : 2100.0) / (x10 ? 1800.0 : 162.0), k); }

// Menu order is three engines times three explicit oversampling factors.
inline juce::StringArray offlineBounceChoices()
{
    return { "Eco · 2x", "Eco · 4x", "Eco · Derp (8x)",
             "Circuit · 2x", "Circuit · 4x", "Circuit · Derp (8x)",
             "HQ · 2x", "HQ · 4x", "HQ · Derp (8x)" };
}
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Parameter addresses stay valid for the APVTS lifetime. Resolve IDs once, off the audio thread.
struct ParameterValues
{
    ParameterValues(const juce::AudioProcessorValueTreeState&, bool channelB);
    std::atomic<float>* instrument, *gain, *gain20, *impedance, *phantom;
    std::atomic<float>* invert, *lowcut, *plateHigh, *preOut, *comp;
    std::atomic<float>* threshold, *ratio, *attack, *release, *autoDetect;
    std::atomic<float>* compOut, *extSc, *ratioLimit, *eq, *low;
    std::atomic<float>* loShift, *high, *hiShift, *hmLevel, *hmFreq;
    std::atomic<float>* hmX10, *hmNarrow, *lmLevel, *lmFreq, *lmX10;
    std::atomic<float>* lmNarrow, *master, *outLevel, *inputTrim, *outputTrim;
    std::atomic<float>* alignment, *micZ, *instZ, *quality, *eco, *oversampling, *offlineBounce, *offlineBounceEnabled, *linearPhase;
    std::atomic<float>* stereoLink, *dualMono, *mains, *mainsVolts, *tolerances;
    std::atomic<float>* seed, *noise, *psuModel;
};

// Read one channel using cached addresses, without constructing strings or searching the tree.
tubechannel::channel::ChannelControls readControls(const ParameterValues&);

// Factory presets (name + a list of id/value pairs; unspecified parameters keep their defaults).
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};
const std::vector<Preset>& factoryPresets();
} // namespace tc::params
