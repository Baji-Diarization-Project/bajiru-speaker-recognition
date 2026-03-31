#pragma once

#include "PluginProcessor.h"

class LinkjiruEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LinkjiruEditor(LinkjiruProcessor &);
    ~LinkjiruEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

private:
    LinkjiruProcessor &processor;

    juce::TextButton startButton{"Start Analysis"};
    juce::TextButton stopButton{"Stop Analysis"};
    juce::TextButton restartButton{"Restart Analysis"};
    juce::Label statusLabel;

    void updateStatus();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkjiruEditor)
};
