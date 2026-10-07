#pragma once

#include "../UI/Panels.h"
#include "PluginProcessor.h"

class TubeChannelEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TubeChannelEditor(TubeChannelProcessor&);
    ~TubeChannelEditor() override;

    void paint(juce::Graphics&) override {}
    void resized() override;

    static constexpr int kW = 1200, kH = 284;

private:
    class Content;
    void timerCallback() override;

    TubeChannelProcessor& proc;
    std::unique_ptr<Content> content;
    juce::ComponentBoundsConstrainer constrainer;
};
