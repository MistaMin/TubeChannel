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
    for (const char* id : { tc::params::id::quality, tc::params::id::eco, tc::params::id::oversampling, tc::params::id::offlineBounce, tc::params::id::offlineBounceEnabled, tc::params::id::linearPhase, tc::params::id::tolerances, tc::params::id::seed })
        apvts.addParameterListener(id, this);
    startTimer(250);
}

TubeChannelProcessor::~TubeChannelProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    for (const char* id : { tc::params::id::quality, tc::params::id::eco, tc::params::id::oversampling, tc::params::id::offlineBounce, tc::params::id::offlineBounceEnabled, tc::params::id::linearPhase, tc::params::id::tolerances, tc::params::id::seed })
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
    k.quality = paramsA.eco->load() > .5f ? 3 : juce::roundToInt(paramsA.quality->load());
    k.oversampling = juce::roundToInt(paramsA.oversampling->load());
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
    cfg.quality = static_cast<Quality>(juce::jlimit(0, 3, k.quality));
    cfg.linearPhase = k.linear;
    cfg.oversamplingStages = k.oversampling - 1;
    cfg.tolerances = k.tolerances;
    cfg.seed = k.seed;
    cfg.engine.compactSupply = TC_EXPERIMENTAL_SUPPLY != 0;
    cfg.psu.mainsHz = paramsA.mains->load() > 0.5f ? 50.0 : 60.0;
    cfg.psu.mainsScale = paramsA.mainsVolts->load();
    auto c = std::make_unique<tc::Chain>();
    c->prepare(cfg, embeddedNetlists());
    return c;
}

void TubeChannelProcessor::setNonRealtime(bool offline) noexcept
{
    if (offline && !isNonRealtime()) offlineEpoch.fetch_add(1, std::memory_order_relaxed);
    AudioProcessor::setNonRealtime(offline);
}

void TubeChannelProcessor::reset()
{
    // Hosts may reset an offline processor between renders without toggling its offline flag.
    offlineEpoch.fetch_add(1, std::memory_order_relaxed);
}

TubeChannelProcessor::StateKey TubeChannelProcessor::requestedStateKey() const
{
    StateKey key;
    key.playback = requestedKey();
    key.bounce = key.playback;
    key.bounceEnabled = paramsA.offlineBounceEnabled->load() > .5f;
    if (key.bounceEnabled)
    {
        const int choice = juce::jlimit(0, 8, juce::roundToInt(paramsA.offlineBounce->load()));
        constexpr int qualities[] = { 3, 0, 1 }; // Eco, Circuit, HQ
        key.bounce.quality = qualities[choice / 3];
        key.bounce.oversampling = choice % 3 + 2;
    }
    return key;
}

void TubeChannelProcessor::Padding::prepare(int size)
{
    delay = size;
    position = 0;
    for (auto& channel : samples) channel.assign(size_t(delay), 0.f);
}

void TubeChannelProcessor::Padding::process(float* const* audio, int channels, int frames)
{
    if (delay == 0) return;
    for (int i = 0; i < frames; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
            std::swap(audio[ch][i], samples[size_t(ch)][size_t(position)]);
        if (++position == delay) position = 0;
    }
}

void TubeChannelProcessor::PreparedState::preserve(PreparedState& previous, bool preserveBounce)
{
    // A bounce-only edit must not reset the working engine or its delay history.
    if (key.playback == previous.key.playback)
    {
        playback.swap(previous.playback);
        if (latency == previous.latency) std::swap(playbackPadding, previous.playbackPadding);
    }
    if (preserveBounce && bounce && previous.bounce && key.bounce == previous.key.bounce)
    {
        bounce.swap(previous.bounce);
        if (latency == previous.latency) std::swap(bouncePadding, previous.bouncePadding);
    }
}

std::unique_ptr<TubeChannelProcessor::PreparedState> TubeChannelProcessor::buildState(const StateKey& key) const
{
    auto state = std::make_unique<PreparedState>();
    state->key = key;
    state->playback = buildChain(key.playback);
    state->latency = state->playback->latency();
    if (key.bounceEnabled)
    {
        state->bounce = buildChain(key.bounce);
        state->latency = std::max(state->latency, state->bounce->latency());
        if (key.playback.linear)
        {
            // A constant budget across all nine bounce choices avoids PDC changes at render entry.
            tubechannel::dsp::Oversampler reference;
            for (int stages = 1; stages <= 3; ++stages)
            {
                reference.prepare(key.playback.sampleRate, 1, 1, Quality::HQ, true, stages);
                state->latency = std::max(state->latency, reference.latencySamples());
            }
        }
    }
    state->playbackPadding.prepare(state->latency - state->playback->latency());
    if (state->bounce) state->bouncePadding.prepare(state->latency - state->bounce->latency());
    return state;
}

bool TubeChannelProcessor::adoptIncoming(const StateKey& wanted, bool& playbackChanged)
{
    const juce::SpinLock::ScopedTryLockType lock(handoverLock);
    if (!lock.isLocked() || !incoming || retired || !(incoming->key == wanted)) return false;
    playbackChanged = !(chain->key.playback == incoming->key.playback) || chain->latency != incoming->latency;
    incoming->preserve(*chain);
    chain.swap(incoming);
    retired = std::move(incoming);
    swaps.fetch_add(1);
    return true;
}

void TubeChannelProcessor::publishActive(bool offline)
{
    auto& engine = chain->select(offline);
    activeChannelCount.store(engine.channels());
    activeFactor.store(engine.factor());
    activePrototypeCompressor.store(engine.config().engine.compactDetector);
    activeQuality.store(int(engine.config().quality));
    activeOfflineBounce.store(offline && chain->key.bounceEnabled);
    activeLatency.store(chain->latency);
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
    const StateKey k = requestedStateKey();
    auto fresh = buildState(k);
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
    bounceHasRun = false;
    renderedEpoch = offlineEpoch.load(std::memory_order_relaxed);
    publishActive(isNonRealtime());
    setLatencySamples(chain->latency);
    prepared.store(true);
    building.store(false);
}

void TubeChannelProcessor::handleAsyncUpdate()
{
    if (!prepared.load())
        return;
    const StateKey k = requestedStateKey();
    {
        const juce::SpinLock::ScopedLockType l(keyLock);
        if (k == builtKey)
            return;
        builtKey = k;
    }
    building.store(true);
    auto fresh = buildState(k);            // heavy preparation stays off the live callback
    const int latency = fresh->latency;
    std::unique_ptr<PreparedState> trash;
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
    std::unique_ptr<PreparedState> trash;
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

    const bool offline = isNonRealtime();
    const StateKey wanted = requestedStateKey();
    if (offline)
    {
        bool ignored = false;
        adoptIncoming(wanted, ignored);
        const auto epoch = offlineEpoch.load(std::memory_order_relaxed);
        if (!(chain->key == wanted) || (wanted.bounceEnabled && bounceHasRun && epoch != renderedEpoch))
        {
            // Some hosts signal offline mode or restore parameters immediately before processing,
            // without another prepare call or message loop. Synchronous work is allowed ONLY offline.
            auto fresh = buildState(wanted);
            fresh->preserve(*chain, false);
            chain = std::move(fresh);
            { const juce::SpinLock::ScopedLockType lock(keyLock); builtKey = wanted; }
            setLatencySamples(chain->latency);
        }
        renderedEpoch = epoch;
        bounceHasRun = wanted.bounceEnabled;
        fadeInRemaining = 0;
    }
    publishActive(offline);
    auto& engine = chain->select(offline);
    const int nch = std::min(engine.channels(), std::min(getMainBusNumInputChannels(), buffer.getNumChannels()));
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
        engine.process(audio, total, scPtrs, nsc, ca, cb, g, tel);
        auto& padding = offline && chain->bounce ? chain->bouncePadding : chain->playbackPadding;
        padding.process(audio, nch, total);
    }

    bool playbackChanged = false;
    const bool swapped = !offline && adoptIncoming(wanted, playbackChanged) && playbackChanged;
    if (swapped)
    {
        for (int ch = 0; ch < nch; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < total; ++i)
                data[i] *= 1.0f - float(i + 1) / float(total);
        }
        fadeInRemaining = fadeInLength;
        publishActive(false);
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
                                pid == id::instZ || pid == id::quality || pid == id::eco || pid == id::oversampling || pid == id::offlineBounce || pid == id::offlineBounceEnabled || pid == id::linearPhase || pid == id::stereoLink ||
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
        {
            auto state = juce::ValueTree::fromXml(*xml);
            // Released states have no Eco flag. Restore the original engine,
            // retaining the complete legacy choice/automation range.
            if (!state.getChildWithProperty("id", tc::params::id::eco).isValid())
            {
                juce::ValueTree eco("PARAM");
                eco.setProperty("id", tc::params::id::eco, nullptr);
                eco.setProperty("value", 0.0f, nullptr);
                state.addChild(eco, -1, nullptr);
            }
            if (!state.getChildWithProperty("id", tc::params::id::oversampling).isValid())
            {
                juce::ValueTree os("PARAM");
                os.setProperty("id", tc::params::id::oversampling, nullptr);
                os.setProperty("value", 0.0f, nullptr); // preserve pre-split adaptive oversampling
                state.addChild(os, -1, nullptr);
            }
            if (!state.getChildWithProperty("id", tc::params::id::offlineBounce).isValid())
            {
                juce::ValueTree choice("PARAM");
                choice.setProperty("id", tc::params::id::offlineBounce, nullptr);
                choice.setProperty("value", 8.f, nullptr);
                state.addChild(choice, -1, nullptr);
                if (!state.getChildWithProperty("id", tc::params::id::offlineBounceEnabled).isValid())
                {
                    juce::ValueTree enabled("PARAM");
                    enabled.setProperty("id", tc::params::id::offlineBounceEnabled, nullptr);
                    enabled.setProperty("value", 0.f, nullptr);
                    state.addChild(enabled, -1, nullptr);
                }
            }
            apvts.replaceState(state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TubeChannelProcessor();
}
