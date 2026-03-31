#include "PluginEditor.h"

LinkjiruEditor::LinkjiruEditor(LinkjiruProcessor &p)
    : AudioProcessorEditor(&p), processor(p)
{
    startButton.onClick = [this]
    {
        processor.startAnalysis();
        updateStatus();
    };

    stopButton.onClick = [this]
    {
        processor.stopAnalysis();
        updateStatus();
    };

    restartButton.onClick = [this]
    {
        processor.restartAnalysis();
        updateStatus();
    };

    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::FontOptions(14.0f));
    updateStatus();

    addAndMakeVisible(startButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(restartButton);
    addAndMakeVisible(statusLabel);

    setSize(400, 300);
}

LinkjiruEditor::~LinkjiruEditor() = default;

void LinkjiruEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
    g.drawText("Linkjiru",
               getLocalBounds().removeFromTop(50),
               juce::Justification::centred);
}

void LinkjiruEditor::resized()
{
    auto area = getLocalBounds().reduced(30);
    area.removeFromTop(60);

    startButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);
    stopButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(10);
    restartButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(20);
    statusLabel.setBounds(area.removeFromTop(30));
}

void LinkjiruEditor::updateStatus()
{
    if (processor.isAnalysisRunning())
    {
        statusLabel.setText("Status: Running", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen);
    }
    else
    {
        statusLabel.setText("Status: Stopped", juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    }
}
