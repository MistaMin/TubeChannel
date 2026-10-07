#include "Params.h"

namespace tc::params
{
namespace
{
using juce::AudioParameterBool;
using juce::AudioParameterChoice;
using juce::AudioParameterFloat;
using juce::NormalisableRange;
using juce::ParameterID;
using Attr = juce::AudioParameterFloatAttributes;

constexpr int kVersion = 1;

juce::String fmt(double v, int dp, const char* unit)
{
    // JUCE treats 0 decimal places as "as many as needed", so format whole numbers explicitly
    return (dp == 0 ? juce::String(juce::roundToInt(v)) : juce::String(v, dp)) + " " + unit;
}

void addFloat(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& idStr, const juce::String& name,
              float lo, float hi, float def, std::function<juce::String(float, int)> text = nullptr)
{
    Attr a;
    if (text)
        a = a.withStringFromValueFunction(text);
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { idStr, kVersion }, name, NormalisableRange<float>(lo, hi), def, a));
}

void addBool(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& idStr, const juce::String& name, bool def)
{
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { idStr, kVersion }, name, def));
}

void addChoice(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const juce::String& idStr, const juce::String& name,
               const juce::StringArray& choices, int def)
{
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { idStr, kVersion }, name, choices, def));
}

void addChannel(juce::AudioProcessorValueTreeState::ParameterLayout& l, bool b)
{
    const juce::String sx = b ? " B" : "";
    auto I = [&](const char* base) { return idFor(base, b); };

    addBool(l, I(id::instrument), "Instrument input" + sx, false);
    addFloat(l, I(id::gain), "Gain" + sx, 0.f, 1.f, 0.0f, [](float v, int) { return fmt(gainDb(v), 1, "dB"); });
    addBool(l, I(id::gain20), "Gain +20 dB" + sx, false);
    addFloat(l, I(id::impedance), "Impedance" + sx, 0.f, 1.f, 0.4f, [](float v, int) { return fmt(impedanceOhms(v), 0, "ohm"); });
    addBool(l, I(id::phantom), "Phantom 48V" + sx, false);
    addBool(l, I(id::invert), "Invert" + sx, false);
    addFloat(l, I(id::lowcut), "Low cut" + sx, 0.f, 1.f, 0.f, [](float v, int) { return fmt(lowCutHz(v), 0, "Hz"); });
    addBool(l, I(id::plateHigh), "Plate voltage high" + sx, true);
    addFloat(l, I(id::preOut), "Preamp output" + sx, 0.f, 1.f, 1.0f, [](float v, int) { return v < 0.005f ? juce::String("mute") : fmt(20.0 * std::log10(v), 1, "dB"); });

    addBool(l, I(id::comp), "Compressor on" + sx, true);
    addFloat(l, I(id::threshold), "Threshold" + sx, 0.f, 1.f, 0.25f, [](float v, int) { return fmt(thresholdDbm(v), 1, "dBm"); });
    addFloat(l, I(id::ratio), "Ratio" + sx, 0.f, 1.f, 0.3f, [](float v, int) {
        return fmt(ratioValue(v), 1, ":1").replace(" :1", ":1");
    });
    addFloat(l, I(id::attack), "Attack" + sx, 0.f, 1.f, 0.5f, [](float v, int) {
        const double s = attackSeconds(v);
        return s < 1e-3 ? fmt(s * 1e6, 0, "us") : fmt(s * 1e3, s < 0.01 ? 1 : 0, "ms");
    });
    addFloat(l, I(id::release), "Release" + sx, 0.f, 1.f, 0.5f, [](float v, int) {
        const double s = releaseSeconds(v);
        return s < 1.0 ? fmt(s * 1e3, 0, "ms") : fmt(s, 2, "s");
    });
    addBool(l, I(id::autoDetect), "Detect auto" + sx, false);
    addFloat(l, I(id::compOut), "Comp output" + sx, 0.f, 1.f, 0.88f, [](float v, int) {
        auto a = [](double k) { return (std::pow(10.0, 2.0 * k) - 1.0) / 99.0; };
        return v < 0.01f ? juce::String("mute") : fmt(20.0 * std::log10(a(v) / a(0.88)), 1, "dB");
    });
    addBool(l, I(id::extSc), "External sidechain" + sx, false);
    addBool(l, I(id::ratioLimit), "Brickwall limiter" + sx, false);

    addBool(l, I(id::eq), "EQ on" + sx, true);
    addFloat(l, I(id::low), "Low" + sx, 0.f, 1.f, 0.5f, [](float v, int) { return fmt(eqGainDb(v), 1, "dB"); });
    addBool(l, I(id::loShift), "Low shift 40 Hz" + sx, true);
    addFloat(l, I(id::high), "High" + sx, 0.f, 1.f, 0.5f, [](float v, int) { return fmt(eqGainDb(v), 1, "dB"); });
    addBool(l, I(id::hiShift), "High shift 6 kHz" + sx, false);
    addFloat(l, I(id::lmLevel), "Lo-Mid level" + sx, 0.f, 1.f, 0.5f, [](float v, int) { return fmt(eqGainDb(v), 1, "dB"); });
    addFloat(l, I(id::lmFreq), "Lo-Mid freq" + sx, 0.f, 1.f, 0.5f, [](float v, int) { return fmt(lmFreqHz(v, false), 0, "Hz"); });
    addBool(l, I(id::lmX10), "Lo-Mid x10" + sx, false);
    addBool(l, I(id::lmNarrow), "Lo-Mid narrow" + sx, true);
    addFloat(l, I(id::hmLevel), "Hi-Mid level" + sx, 0.f, 1.f, 0.5f, [](float v, int) { return fmt(eqGainDb(v), 1, "dB"); });
    addFloat(l, I(id::hmFreq), "Hi-Mid freq" + sx, 0.f, 1.f, 0.5f, [](float v, int) { return fmt(hmFreqHz(v, false), 0, "Hz"); });
    addBool(l, I(id::hmX10), "Hi-Mid x10" + sx, false);
    addBool(l, I(id::hmNarrow), "Hi-Mid narrow" + sx, true);

    addFloat(l, I(id::master), "Master" + sx, 0.f, 1.f, 0.8f, [](float v, int) { return v < 0.005f ? juce::String("mute") : fmt(20.0 * std::log10(v / 0.8), 1, "dB"); });
    addBool(l, I(id::outLevel), "Output +4 dBu" + sx, true);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    addChannel(l, false);

    addFloat(l, id::inputTrim, "Input trim", -24.f, 24.f, 0.f, [](float v, int) { return fmt(v, 1, "dB"); });
    addFloat(l, id::outputTrim, "Output trim", -24.f, 24.f, 0.f, [](float v, int) { return fmt(v, 1, "dB"); });
    addChoice(l, id::alignment, "0 VU level", { "-14 dBFS", "-16 dBFS", "-18 dBFS", "-20 dBFS", "-22 dBFS" }, 2);
    addFloat(l, id::micZ, "Mic source impedance", 50.f, 600.f, 150.f, [](float v, int) { return fmt(v, 0, "ohm"); });
    addFloat(l, id::instZ, "Instrument source impedance", 1000.f, 100000.f, 10000.f, [](float v, int) { return fmt(v / 1000.0, 1, "kohm"); });
    addChoice(l, id::quality, "Oversampling", { "Default", "HQ", "Derp mode" }, 0);
    addBool(l, id::linearPhase, "Linear phase (+L)", false);
    addBool(l, id::stereoLink, "Stereo link", true);
    addBool(l, id::dualMono, "Dual mono", false);
    addChoice(l, id::mains, "Mains frequency", { "60 Hz", "50 Hz" }, 0);
    addFloat(l, id::mainsVolts, "Mains voltage", 0.85f, 1.1f, 1.0f, [](float v, int) { return fmt(v * 100.0, 0, "%"); });
    addBool(l, id::tolerances, "Component tolerances", true);
    addFloat(l, id::seed, "Unit seed", 0.f, 99.f, 1.f, [](float v, int) { return juce::String(juce::roundToInt(v)); });
    addBool(l, id::noise, "Circuit noise", true);
    addBool(l, id::psuModel, "Power supply model", true);

    addChannel(l, true);
    return l;
}

ParameterValues::ParameterValues(const juce::AudioProcessorValueTreeState& s, bool b)
{
    instrument = s.getRawParameterValue(idFor(id::instrument, b));
    gain = s.getRawParameterValue(idFor(id::gain, b));
    gain20 = s.getRawParameterValue(idFor(id::gain20, b));
    impedance = s.getRawParameterValue(idFor(id::impedance, b));
    phantom = s.getRawParameterValue(idFor(id::phantom, b));
    invert = s.getRawParameterValue(idFor(id::invert, b));
    lowcut = s.getRawParameterValue(idFor(id::lowcut, b));
    plateHigh = s.getRawParameterValue(idFor(id::plateHigh, b));
    preOut = s.getRawParameterValue(idFor(id::preOut, b));
    comp = s.getRawParameterValue(idFor(id::comp, b));
    threshold = s.getRawParameterValue(idFor(id::threshold, b));
    ratio = s.getRawParameterValue(idFor(id::ratio, b));
    attack = s.getRawParameterValue(idFor(id::attack, b));
    release = s.getRawParameterValue(idFor(id::release, b));
    autoDetect = s.getRawParameterValue(idFor(id::autoDetect, b));
    compOut = s.getRawParameterValue(idFor(id::compOut, b));
    extSc = s.getRawParameterValue(idFor(id::extSc, b));
    ratioLimit = s.getRawParameterValue(idFor(id::ratioLimit, b));
    eq = s.getRawParameterValue(idFor(id::eq, b));
    low = s.getRawParameterValue(idFor(id::low, b));
    loShift = s.getRawParameterValue(idFor(id::loShift, b));
    high = s.getRawParameterValue(idFor(id::high, b));
    hiShift = s.getRawParameterValue(idFor(id::hiShift, b));
    hmLevel = s.getRawParameterValue(idFor(id::hmLevel, b));
    hmFreq = s.getRawParameterValue(idFor(id::hmFreq, b));
    hmX10 = s.getRawParameterValue(idFor(id::hmX10, b));
    hmNarrow = s.getRawParameterValue(idFor(id::hmNarrow, b));
    lmLevel = s.getRawParameterValue(idFor(id::lmLevel, b));
    lmFreq = s.getRawParameterValue(idFor(id::lmFreq, b));
    lmX10 = s.getRawParameterValue(idFor(id::lmX10, b));
    lmNarrow = s.getRawParameterValue(idFor(id::lmNarrow, b));
    master = s.getRawParameterValue(idFor(id::master, b));
    outLevel = s.getRawParameterValue(idFor(id::outLevel, b));
    inputTrim = s.getRawParameterValue(id::inputTrim);
    outputTrim = s.getRawParameterValue(id::outputTrim);
    alignment = s.getRawParameterValue(id::alignment);
    micZ = s.getRawParameterValue(id::micZ);
    instZ = s.getRawParameterValue(id::instZ);
    quality = s.getRawParameterValue(id::quality);
    linearPhase = s.getRawParameterValue(id::linearPhase);
    stereoLink = s.getRawParameterValue(id::stereoLink);
    dualMono = s.getRawParameterValue(id::dualMono);
    mains = s.getRawParameterValue(id::mains);
    mainsVolts = s.getRawParameterValue(id::mainsVolts);
    tolerances = s.getRawParameterValue(id::tolerances);
    seed = s.getRawParameterValue(id::seed);
    noise = s.getRawParameterValue(id::noise);
    psuModel = s.getRawParameterValue(id::psuModel);
}

tubechannel::channel::ChannelControls readControls(const ParameterValues& p)
{
    tubechannel::channel::ChannelControls c;
    c.instrument = (p.instrument->load() > 0.5f);
    c.gain = p.gain->load();
    c.gainPlus20 = (p.gain20->load() > 0.5f);
    c.impedance = p.impedance->load();
    c.phantom = (p.phantom->load() > 0.5f);
    c.invert = (p.invert->load() > 0.5f);
    c.lowCut = p.lowcut->load();
    c.plateHigh = (p.plateHigh->load() > 0.5f);
    c.preOut = p.preOut->load();
    c.compActive = (p.comp->load() > 0.5f);
    c.threshold = p.threshold->load();
    c.ratio = p.ratio->load();
    c.attack = p.attack->load();
    c.release = p.release->load();
    c.autoDetect = (p.autoDetect->load() > 0.5f);
    c.compOut = p.compOut->load();
    c.externalSidechain = (p.extSc->load() > 0.5f);
    c.limiter = (p.ratioLimit->load() > 0.5f);
    c.limiterRelease = releaseSeconds(c.release);
    c.eqActive = (p.eq->load() > 0.5f);
    c.low = p.low->load();
    c.high = p.high->load();
    c.loShift40 = (p.loShift->load() > 0.5f);
    c.hiShift6k = (p.hiShift->load() > 0.5f);
    c.hmLevel = p.hmLevel->load();
    c.hmFreq = p.hmFreq->load();
    c.hmX10 = (p.hmX10->load() > 0.5f);
    c.hmNarrow = (p.hmNarrow->load() > 0.5f);
    c.lmLevel = p.lmLevel->load();
    c.lmFreq = p.lmFreq->load();
    c.lmX10 = (p.lmX10->load() > 0.5f);
    c.lmNarrow = (p.lmNarrow->load() > 0.5f);
    c.master = p.master->load();
    c.outPlus4 = (p.outLevel->load() > 0.5f);
    c.micSourceOhms = p.micZ->load();
    c.instSourceOhms = p.instZ->load();
    return c;
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets = {
        { "Init", {} },
        { "Vocal, warm and close",
          { { id::gain, 0.3f }, { id::lowcut, 0.35f }, { id::plateHigh, 0.f }, { id::threshold, 0.22f }, { id::ratio, 0.35f },
            { id::attack, 0.55f }, { id::release, 0.55f }, { id::high, 0.62f }, { id::hmLevel, 0.58f }, { id::hmFreq, 0.55f },
            { id::lmLevel, 0.46f }, { id::lmFreq, 0.4f } } },
        { "Bass guitar, DI",
          { { id::instrument, 1.f }, { id::gain, 0.25f }, { id::lowcut, 0.1f }, { id::threshold, 0.2f }, { id::ratio, 0.5f },
            { id::attack, 0.6f }, { id::release, 0.45f }, { id::low, 0.58f }, { id::lmLevel, 0.55f }, { id::lmFreq, 0.5f },
            { id::hmLevel, 0.55f }, { id::hmFreq, 0.4f }, { id::hmX10, 0.f } } },
        { "Drum bus, glue",
          { { id::gain, 0.1f }, { id::threshold, 0.18f }, { id::ratio, 0.22f }, { id::attack, 0.7f }, { id::release, 0.35f },
            { id::autoDetect, 1.f }, { id::low, 0.56f }, { id::high, 0.58f } } },
        { "Tube drive, plate low",
          { { id::gain, 0.4f }, { id::gain20, 1.f }, { id::plateHigh, 0.f }, { id::preOut, 0.5f }, { id::comp, 0.f } } },
        { "Limiter, infinity",
          { { id::gain, 0.2f }, { id::threshold, 0.3f }, { id::ratio, 1.f }, { id::attack, 0.15f }, { id::release, 0.4f } } },
    };
    return presets;
}
} // namespace tc::params
