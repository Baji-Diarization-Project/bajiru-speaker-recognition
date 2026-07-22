#pragma once

#include "PluginProcessor.h"

class LinkjiruEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LinkjiruEditor(LinkjiruProcessor&);
    ~LinkjiruEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    LinkjiruProcessor& processor;

    juce::TextButton startButton{"Start Analysis"};
    juce::TextButton stopButton{"Stop Analysis"};
    juce::TextButton restartButton{"Restart Analysis"};
    juce::TextButton vtsRegisterButton{"Register in VTS"};
    juce::TextButton playBajiButton{"Play baji sample"};
    juce::TextButton playRuButton{"Play ru sample"};
    juce::Label statusLabel;
    juce::Label modelLabel;
    juce::Label detectRect; // colored state box (Baji / Ru / Idle / Loading / Error)
    juce::Rectangle<int> legendArea;
    juce::Label scoreLabel; // small running smoothed confidence readout (0.00-1.00)
    juce::Label vtsStatusLabel;

    void timerCallback() override;
    void refresh(const PipelineStatus& status);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkjiruEditor)
};
