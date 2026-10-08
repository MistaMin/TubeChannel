#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

#include "Chain.h"
#include "Params.h"

class TubeChannelProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::AsyncUpdater,
                             private juce::Timer
{
public:
    TubeChannelProcessor();
    ~TubeChannelProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tube Channel"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return int(tc::params::factoryPresets().size()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    // ---- for the UI ----
    tubechannel::channel::Telemetry telemetry[2];
    int activeChannels() const { return activeChannelCount.load(); }
    int currentFactor() const { return activeFactor.load(); }
    bool usesPrototypeCompressor() const { return activePrototypeCompressor.load(); }
    int currentLatency() const { return activeLatency.load(); }
    bool isBuilding() const { return building.load(); }
    float cpuLoad() const { return cpu.load(); }
    int swapCount() const { return swaps.load(); }

    // Final output peaks accumulated between UI polls, after trims and mode-switch fades.
    float takeOutputMeterPeak(int ch) { return outputMeterPeak[ch].exchange(0.f, std::memory_order_relaxed); }
    bool takeOutputClip(int ch) { return outputClip[ch].exchange(false, std::memory_order_relaxed); }

    // most recent output samples (mono sum) for the spectrum display
    static constexpr int kSpecSize = 4096;
    std::atomic<float> specRing[kSpecSize];
    std::atomic<int> specWrite { 0 };

    int skin() const { return int(apvts.state.getProperty("skin", 0)); }
    void setSkin(int s) { apvts.state.setProperty("skin", s, nullptr); }

    // A/B comparison of the whole control state
    bool isB() const { return abIsB; }
    void toggleAB();
    void copyToOther();

private:
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void timerCallback() override;

    struct Key
    {
        double sampleRate = 0;
        int maxBlock = 0, channels = 0, quality = 0, seed = 0;
        bool linear = false, tolerances = false;
        bool operator==(const Key& o) const
        {
            return sampleRate == o.sampleRate && maxBlock == o.maxBlock && channels == o.channels && quality == o.quality &&
                   seed == o.seed && linear == o.linear && tolerances == o.tolerances;
        }
    };
    Key requestedKey() const;
    std::unique_ptr<tc::Chain> buildChain(const Key&) const;
    tc::ChainGlobals readGlobals() const;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;

    const tc::params::ParameterValues paramsA, paramsB;

    // audio thread
    std::unique_ptr<tc::Chain> chain;
    int fadeInRemaining = 0, fadeInLength = 0;

    // hand-over from the message thread
    juce::SpinLock handoverLock;
    std::unique_ptr<tc::Chain> incoming, retired;

    juce::SpinLock keyLock;
    Key builtKey;

    std::atomic<int> activeChannelCount { 2 }, activeFactor { 2 }, activeLatency { 0 }, swaps { 0 };
    std::atomic<bool> activePrototypeCompressor { true };
    std::atomic<bool> prepared { false }, building { false };
    std::atomic<float> cpu { 0.f };
    std::atomic<float> outputMeterPeak[2] {};
    std::atomic<bool> outputClip[2] {};
    int currentProgram = 0;

    juce::ValueTree otherState;
    bool abIsB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TubeChannelProcessor)
};
