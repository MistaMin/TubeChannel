#include "PluginProcessor.h"

#include "EmbeddedNetlists.h"
#include "PluginEditor.h"

using tubechannel::dsp::Quality;

namespace
{
tubechannel::channel::NetlistSet embeddedNetlists()
{
    tubechannel::channel::NetlistSet n;
    n.mic = tc::embedded::kMic; n.inst = tc::embedded::kInst; n.tube = tc::embedded::kTube;
    n.preout = tc::embedded::kPreout; n.comp = tc::embedded::kComp; n.det = tc::embedded::kDet; n.eq1 = tc::embedded::kEq1;
    n.eq2 = tc::embedded::kEq2; n.eq3 = tc::embedded::kEq3; n.eq4 = tc::embedded::kEq4;
    n.out = tc::embedded::kOut; n.psu = tc::embedded::kPsu;
    return n;
}

constexpr const char* kStateTag = "TubeChannelState";
} // namespace

TubeChannelProcessor::TubeChannelProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, &undo, kStateTag, tc::params::createLayout()),
      paramsA(apvts, false), paramsB(apvts, true)
{
    for (auto& v : specRing)
        v.store(0.f);
    for (const char* id : { tc::params::id::quality, tc::params::id::linearPhase, tc::params::id::tolerances, tc::params::id::seed })
        apvts.addParameterListener(id, this);
    startTimer(250);
}

TubeChannelProcessor::~TubeChannelProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    for (const char* id : { tc::params::id::quality, tc::params::id::linearPhase, tc::params::id::tolerances, tc::params::id::seed })
        apvts.removeParameterListener(id, this);
}

bool TubeChannelProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()))
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses.getReference(1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

TubeChannelProcessor::Key TubeChannelProcessor::requestedKey() const
{
    Key k;
    k.sampleRate = sampleRate_;
    k.maxBlock = maxBlock_;
    k.channels = std::max(1, std::min(2, getMainBusNumInputChannels()));
    k.quality = juce::roundToInt(paramsA.quality->load());
    k.linear = paramsA.linearPhase->load() > 0.5f;
    k.tolerances = paramsA.tolerances->load() > 0.5f;
    k.seed = juce::roundToInt(paramsA.seed->load());
    return k;
}

std::unique_ptr<tc::Chain> TubeChannelProcessor::buildChain(const Key& k) const
{
    tc::ChainConfig cfg;
    cfg.sampleRate = k.sampleRate;
    cfg.maxBlock = k.maxBlock;
    cfg.channels = k.channels;
    cfg.quality = k.quality <= 0 ? Quality::Default : (k.quality == 1 ? Quality::HQ : Quality::Derp);
    cfg.linearPhase = k.linear;
    cfg.tolerances = k.tolerances;
    cfg.seed = k.seed;
    cfg.engine.compactSupply = TC_EXPERIMENTAL_SUPPLY != 0;
    cfg.psu.mainsHz = paramsA.mains->load() > 0.5f ? 50.0 : 60.0;
    cfg.psu.mainsScale = paramsA.mainsVolts->load();
    auto c = std::make_unique<tc::Chain>();
    c->prepare(cfg, embeddedNetlists());
    return c;
}

tc::ChainGlobals TubeChannelProcessor::readGlobals() const
{
    tc::ChainGlobals g;
    g.inputTrimDb = paramsA.inputTrim->load();
    g.outputTrimDb = paramsA.outputTrim->load();
    static const double kAlign[] = { -14.0, -16.0, -18.0, -20.0, -22.0 };
    g.alignmentDbfs = kAlign[juce::jlimit(0, 4, juce::roundToInt(paramsA.alignment->load()))];
    g.stereoLink = paramsA.stereoLink->load() > 0.5f;
    g.dualMono = paramsA.dualMono->load() > 0.5f;
    g.noise = paramsA.noise->load() > 0.5f;
    g.psuModel = paramsA.psuModel->load() > 0.5f;
    g.mainsHz = paramsA.mains->load() > 0.5f ? 50.0 : 60.0;
    g.mainsScale = paramsA.mainsVolts->load();
    return g;
}

void TubeChannelProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(samplesPerBlock, 1);
    building.store(true);
    const Key k = requestedKey();
    auto fresh = buildChain(k);
    {
        const juce::SpinLock::ScopedLockType l(handoverLock);
        incoming.reset();
        retired.reset();
    }
    chain = std::move(fresh);
    {
        const juce::SpinLock::ScopedLockType l(keyLock);
        builtKey = k;
    }
    fadeInRemaining = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        outputMeterPeak[ch].store(0.f, std::memory_order_relaxed);
        outputClip[ch].store(false, std::memory_order_relaxed);
    }
    fadeInLength = std::max(1, juce::roundToInt(sampleRate_ * 0.02));
    activeChannelCount.store(chain->channels());
    activeFactor.store(chain->factor());
    activePrototypeCompressor.store(chain->config().engine.compactDetector);
    activeLatency.store(chain->latency());
    setLatencySamples(chain->latency());
    prepared.store(true);
    building.store(false);
}

void TubeChannelProcessor::handleAsyncUpdate()
{
    if (!prepared.load())
        return;
    const Key k = requestedKey();
    {
        const juce::SpinLock::ScopedLockType l(keyLock);
        if (k == builtKey)
            return;
        builtKey = k;
    }
    building.store(true);
    auto fresh = buildChain(k);            // heavy (the circuits are built and warmed up): message thread only
    const int latency = fresh->latency();
    std::unique_ptr<tc::Chain> trash;
    {
        const juce::SpinLock::ScopedLockType l(handoverLock);
        // Free the retirement slot before publishing another chain. Destruction stays on this
        // thread, and the next audio callback can adopt the published latency without waiting.
        trash = std::move(retired);
        incoming = std::move(fresh);
    }
    setLatencySamples(latency);
    building.store(false);
}

void TubeChannelProcessor::timerCallback()
{
    std::unique_ptr<tc::Chain> trash;
    {
        const juce::SpinLock::ScopedLockType l(handoverLock);
        trash = std::move(retired);
    }
}

void TubeChannelProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int total = buffer.getNumSamples();
    for (int ch = getMainBusNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, total);
    if (chain == nullptr || total == 0)
        return;

    const int nch = std::min(chain->channels(), std::min(getMainBusNumInputChannels(), buffer.getNumChannels()));
    if (nch > 0)
    {
        // controls
        const auto ca = tc::params::readControls(paramsA);
        const auto cb = tc::params::readControls(paramsB);
        const auto g = readGlobals();
        // sidechain bus
        const float* scPtrs[2] = { nullptr, nullptr };
        int nsc = 0;
        if (getBusCount(true) > 1 && getBus(true, 1) != nullptr && getBus(true, 1)->isEnabled())
        {
            auto sb = getBusBuffer(buffer, true, 1);
            nsc = std::min(2, sb.getNumChannels());
            for (int i = 0; i < nsc; ++i)
                scPtrs[i] = sb.getReadPointer(i);
        }
        float* audio[2] = { buffer.getWritePointer(0), nch > 1 ? buffer.getWritePointer(1) : nullptr };
        tubechannel::channel::Telemetry* tel[2] = { &telemetry[0], &telemetry[1] };
        chain->process(audio, total, scPtrs, nsc, ca, cb, g, tel);
    }

    bool swapped = false;
    {
        const juce::SpinLock::ScopedTryLockType l(handoverLock);
        // Leave the incoming chain pending until the old one has a retirement slot. Both moves
        // happen under the same lock, so processing can never destroy a chain on return.
        if (l.isLocked() && incoming != nullptr && retired == nullptr)
        {
            std::swap(chain, incoming);
            retired = std::move(incoming);
            swapped = true;
        }
    }
    if (swapped)
    {
        for (int ch = 0; ch < nch; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < total; ++i)
                data[i] *= 1.0f - float(i + 1) / float(total);
        }
        fadeInRemaining = fadeInLength;
        activeChannelCount.store(chain->channels());
        activeFactor.store(chain->factor());
        activePrototypeCompressor.store(chain->config().engine.compactDetector);
        activeLatency.store(chain->latency());
        swaps.fetch_add(1);
    }
    else if (fadeInRemaining > 0)
    {
        const int len = std::min(fadeInRemaining, total);
        for (int ch = 0; ch < nch; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < len; ++i)
                data[i] *= 1.0f - float(fadeInRemaining - i) / float(fadeInLength);
        }
        fadeInRemaining -= len;
    }

    {
        int w = specWrite.load(std::memory_order_relaxed);
        const int nc = std::max(1, std::min(buffer.getNumChannels(), getMainBusNumOutputChannels()));
        float peaks[2] = {};
        for (int i = 0; i < total; ++i)
        {
            float v = 0.f;
            for (int ch = 0; ch < nc; ++ch)
            {
                const float sample = buffer.getSample(ch, i);
                v += sample;
                peaks[ch] = std::max(peaks[ch], std::fabs(sample));
            }
            specRing[w].store(v / float(nc), std::memory_order_relaxed);
            w = (w + 1) % kSpecSize;
        }
        specWrite.store(w, std::memory_order_release);
        for (int ch = 0; ch < nc; ++ch)
        {
            float previous = outputMeterPeak[ch].load(std::memory_order_relaxed);
            // The UI only resets this value to zero. A failed comparison means it just consumed
            // the previous peak; publish this block's peak with a bounded fallback.
            if (previous < peaks[ch] && !outputMeterPeak[ch].compare_exchange_strong(previous, peaks[ch], std::memory_order_relaxed))
                outputMeterPeak[ch].store(peaks[ch], std::memory_order_relaxed);
            if (peaks[ch] >= 1.f) outputClip[ch].store(true, std::memory_order_relaxed);
        }
    }

    const double secs = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
    const float load = float(secs / (double(total) / sampleRate_));
    cpu.store(cpu.load() * 0.9f + load * 0.1f);
}

juce::AudioProcessorEditor* TubeChannelProcessor::createEditor()
{
    return new TubeChannelEditor(*this);
}

void TubeChannelProcessor::setCurrentProgram(int index)
{
    const auto& presets = tc::params::factoryPresets();
    if (index < 0 || index >= int(presets.size()))
        return;
    currentProgram = index;
    namespace id = tc::params::id;
    undo.beginNewTransaction("Preset");
    // reset the channel controls of channel A to their defaults, keep the global setup
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const juce::String pid = rp->getParameterID();
            if (pid.endsWith("_b"))
                continue;
            const bool global = pid == id::inputTrim || pid == id::outputTrim || pid == id::alignment || pid == id::micZ ||
                                pid == id::instZ || pid == id::quality || pid == id::linearPhase || pid == id::stereoLink ||
                                pid == id::dualMono || pid == id::mains || pid == id::mainsVolts || pid == id::tolerances ||
                                pid == id::seed || pid == id::noise || pid == id::psuModel;
            if (!global)
                rp->setValueNotifyingHost(rp->getDefaultValue());
        }
    for (const auto& [pid, v] : presets[size_t(index)].values)
        if (auto* rp = apvts.getParameter(pid))
            rp->setValueNotifyingHost(rp->convertTo0to1(v));
}

const juce::String TubeChannelProcessor::getProgramName(int index)
{
    const auto& presets = tc::params::factoryPresets();
    return index >= 0 && index < int(presets.size()) ? juce::String(presets[size_t(index)].name) : juce::String();
}

void TubeChannelProcessor::toggleAB()
{
    if (!otherState.isValid())
        otherState = apvts.copyState();
    auto current = apvts.copyState();
    apvts.replaceState(otherState);
    otherState = current;
    abIsB = !abIsB;
}

void TubeChannelProcessor::copyToOther()
{
    otherState = apvts.copyState();
}

void TubeChannelProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void TubeChannelProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TubeChannelProcessor();
}
