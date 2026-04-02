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
        updateVtsStatus();
    };

    restartButton.onClick = [this]
    {
        processor.restartAnalysis();
        updateStatus();
    };

    vtsRegisterButton.onClick = [this]
    {
        processor.requestVtsRegister();
    };

    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::FontOptions(14.0f));

    detectLabel.setJustificationType(juce::Justification::centred);
    detectLabel.setFont(juce::FontOptions(13.0f));
    detectLabel.setText("LinkjiruDetectLowji = --", juce::dontSendNotification);
    detectLabel.setColour(juce::Label::textColourId, juce::Colours::grey);

    vtsStatusLabel.setJustificationType(juce::Justification::centred);
    vtsStatusLabel.setFont(juce::FontOptions(12.0f));

    addAndMakeVisible(startButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(restartButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(detectLabel);
    addAndMakeVisible(vtsRegisterButton);
    addAndMakeVisible(vtsStatusLabel);

    updateStatus();
    updateVtsStatus();

    startTimerHz(2);
    setSize(400, 420);
}

LinkjiruEditor::~LinkjiruEditor()
{
    stopTimer();
}

void LinkjiruEditor::timerCallback()
{
    updateVtsStatus();

    if (processor.isAnalysisRunning())
    {
        const float val = processor.getDetectValue();
        detectLabel.setText("LinkjiruDetectLowji = " + juce::String(val, 1),
                           juce::dontSendNotification);
        detectLabel.setColour(juce::Label::textColourId,
                              val > 0.5f ? juce::Colours::limegreen
                                         : juce::Colours::grey);
    }
    else
    {
        detectLabel.setText("LinkjiruDetectLowji = --", juce::dontSendNotification);
        detectLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    }
}

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
    area.removeFromTop(12);
    statusLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);
    detectLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(12);
    vtsRegisterButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(8);
    vtsStatusLabel.setBounds(area.removeFromTop(20));
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

void LinkjiruEditor::updateVtsStatus()
{
    const bool running    = processor.isAnalysisRunning();
    const bool connected  = processor.isVtsConnected();
    const bool registered = processor.isVtsRegistered();

    if (!running)
    {
        vtsRegisterButton.setEnabled(false);
        vtsRegisterButton.setButtonText("Register in VTS");
        vtsStatusLabel.setText("VTS: start analysis first", juce::dontSendNotification);
        vtsStatusLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    }
    else if (registered)
    {
        vtsRegisterButton.setEnabled(false);
        vtsRegisterButton.setButtonText("Registered");
        vtsStatusLabel.setText("VTS: parameter active", juce::dontSendNotification);
        vtsStatusLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen);
    }
    else if (connected)
    {
        vtsRegisterButton.setEnabled(true);
        vtsRegisterButton.setButtonText("Register in VTS");
        vtsStatusLabel.setText("VTS: connected, ready to register", juce::dontSendNotification);
        vtsStatusLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
    }
    else
    {
        vtsRegisterButton.setEnabled(false);
        vtsRegisterButton.setButtonText("Register in VTS");
        vtsStatusLabel.setText("VTS: waiting for connection...", juce::dontSendNotification);
        vtsStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    }
}
