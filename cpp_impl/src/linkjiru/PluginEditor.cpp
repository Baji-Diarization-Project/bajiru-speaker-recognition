#include "PluginEditor.h"
#include "Constants.h"

namespace
{

enum DetectState : std::uint8_t
{
    dsStopped,
    dsLoading,
    dsError,
    dsIdle,
    dsBaji,
    dsRu
};

juce::Colour stateColour(const int s)
{
    switch (s)
    {
    case dsBaji:
        return juce::Colour(0xff2ecc71); // green
    case dsRu:
        return juce::Colour(0xff9b59b6); // purple
    case dsIdle:
        return juce::Colour(0xff3a3f5a); // dim slate
    case dsLoading:
        return juce::Colour(0xffe0a020); // amber
    case dsError:
        return juce::Colour(0xffe74c3c); // red
    default:
        return juce::Colour(0xff2a2a3a); // stopped / dark
    }
}

const char* stateLabel(const int s)
{
    switch (s)
    {
    case dsBaji:
        return "BAJI";
    case dsRu:
        return "RU";
    case dsIdle:
        return "IDLE";
    case dsLoading:
        return "LOADING...";
    case dsError:
        return "INFERENCE ERROR";
    default:
        return "STOPPED";
    }
}

int detectStateFor(const PipelineStatus& s)
{
    if (!s.running)
    {
        return dsStopped;
    }
    if (s.inferenceFailed)
    {
        return dsError;
    }
    if (!s.modelReady)
    {
        return dsLoading;
    }
    // Latched speaker: once heard, the box holds baji/ru and never returns to
    // IDLE. IDLE shows only before the very first detection of a session.
    if (s.speaker == 0)
    {
        return dsBaji;
    }
    if (s.speaker == 1)
    {
        return dsRu;
    }
    return dsIdle;
}

} // namespace

LinkjiruEditor::LinkjiruEditor(LinkjiruProcessor& p) : AudioProcessorEditor(&p), processor(p)
{
    // All three drive the "Analysis Active" parameter / restart request; the actual
    // start/stop is reconciled on the message thread (see the processor). The 15 Hz
    // timer reflects the resulting state, so the handlers stay trivial.
    startButton.onClick   = [this] { processor.setAnalysisActive(true); };
    stopButton.onClick    = [this] { processor.setAnalysisActive(false); };
    restartButton.onClick = [this] { processor.requestRestart(); };

    vtsRegisterButton.onClick = [this] { processor.requestVtsRegister(); };

    // Stream a bundled test sample through the input path (plays + gets analysed).
    playBajiButton.onClick = [this] { processor.playSample(0); };
    playRuButton.onClick   = [this] { processor.playSample(1); };

    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::FontOptions(14.0f));

    modelLabel.setJustificationType(juce::Justification::centred);
    modelLabel.setFont(juce::FontOptions(12.0f));

    detectRect.setJustificationType(juce::Justification::centred);
    detectRect.setFont(juce::Font(juce::FontOptions(16.0f).withStyle("Bold")));

    scoreLabel.setJustificationType(juce::Justification::centred);
    scoreLabel.setFont(juce::FontOptions(12.0f));

    vtsStatusLabel.setJustificationType(juce::Justification::centred);
    vtsStatusLabel.setFont(juce::FontOptions(12.0f));

    addAndMakeVisible(startButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(restartButton);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(modelLabel);
    addAndMakeVisible(detectRect);
    addAndMakeVisible(scoreLabel);
    addAndMakeVisible(vtsRegisterButton);
    addAndMakeVisible(vtsStatusLabel);
    addAndMakeVisible(playBajiButton);
    addAndMakeVisible(playRuButton);

    const bool haveSamples = processor.hasSamples();
    playBajiButton.setEnabled(haveSamples);
    playRuButton.setEnabled(haveSamples);

    refresh(processor.getStatus());

    // ~15 Hz: responsive without hammering the message thread.
    startTimerHz(15);
    setSize(400, 586);
}

LinkjiruEditor::~LinkjiruEditor()
{
    stopTimer();
}

void LinkjiruEditor::timerCallback()
{
    refresh(processor.getStatus());
}

void LinkjiruEditor::refresh(const PipelineStatus& s)
{
    // Analysis state.
    statusLabel.setText(s.running ? "Status: Running" : "Status: Stopped", juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, s.running ? juce::Colours::limegreen : juce::Colours::grey);

    // Model state.
    const auto setModel = [this](const char* text, const juce::Colour c)
    {
        modelLabel.setText(text, juce::dontSendNotification);
        modelLabel.setColour(juce::Label::textColourId, c);
    };
    if (!s.running)
    {
        setModel("Model: --", juce::Colours::grey);
    }
    else if (s.inferenceFailed)
    {
        setModel("Model: inference error", juce::Colours::red);
    }
    else if (s.modelReady)
    {
        setModel("Model: ready", juce::Colours::limegreen);
    }
    else
    {
        setModel("Model: loading...", juce::Colours::orange);
    }

    // Detection state box: a solid, flat colour for the latched speaker. It
    // holds baji/ru once heard (no dimming on pauses, so it doesn't pulse) and
    // only shows IDLE before the first detection of a session.
    const int ds          = detectStateFor(s);
    const juce::Colour bg = stateColour(ds);
    const bool darkText   = bg.getPerceivedBrightness() > 0.6f;
    detectRect.setColour(juce::Label::backgroundColourId, bg);
    detectRect.setColour(juce::Label::textColourId,
                         darkText ? juce::Colours::black.withAlpha(0.8f) : juce::Colours::white);
    detectRect.setText(stateLabel(ds), juce::dontSendNotification);

    // Small running smoothed-confidence readout (max of baji/ru, 0.00-1.00).
    const bool haveScore = s.running && s.modelReady && !s.inferenceFailed;
    scoreLabel.setText(haveScore ? "smoothed score: " + juce::String(s.rawScore, 2) : "smoothed score: --",
                       juce::dontSendNotification);
    scoreLabel.setColour(juce::Label::textColourId,
                         haveScore ? juce::Colours::white.withAlpha(0.85f) : juce::Colours::grey);

    // VTS connection + register button.
    const auto setVts = [this](const bool enabled, const char* btn, const char* status, const juce::Colour c)
    {
        vtsRegisterButton.setEnabled(enabled);
        vtsRegisterButton.setButtonText(btn);
        vtsStatusLabel.setText(status, juce::dontSendNotification);
        vtsStatusLabel.setColour(juce::Label::textColourId, c);
    };
    if (!s.running)
    {
        setVts(false, "Register in VTS", "VTS: start analysis first", juce::Colours::grey);
    }
    else if (s.vtsRegistered)
    {
        setVts(false, "Registered", "VTS: parameter active", juce::Colours::limegreen);
    }
    else if (s.vtsRegisterFailed && s.vtsConnected)
    {
        setVts(true, "Retry Register", "VTS: registration failed, try again", juce::Colours::red);
    }
    else if (s.vtsConnected)
    {
        setVts(true, "Register in VTS", "VTS: connected, ready to register", juce::Colours::yellow);
    }
    else
    {
        setVts(false, "Register in VTS", "VTS: waiting for connection...", juce::Colours::orange);
    }
}

void LinkjiruEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
    g.drawText(linkjiru::pluginName, getLocalBounds().removeFromTop(50), juce::Justification::centred);

    // Legend for the detection state box (swatch + label per state).
    if (!legendArea.isEmpty())
    {
        struct Entry
        {
            int state;
            const char* label;
        };
        static const Entry entries[] = {
            {dsBaji, "Baji"}, {dsRu, "Ru"}, {dsIdle, "Idle"}, {dsLoading, "Loading"}, {dsError, "Error"}};
        constexpr int n = 5;
        const int cellW = legendArea.getWidth() / n;
        const int y     = legendArea.getY();

        g.setFont(juce::FontOptions(11.0f));
        for (int i = 0; i < n; ++i)
        {
            constexpr int swatch = 11;
            const int cx         = legendArea.getX() + i * cellW;
            g.setColour(stateColour(entries[i].state));
            g.fillRoundedRectangle(static_cast<float>(cx), static_cast<float>(y + 3), static_cast<float>(swatch),
                                   static_cast<float>(swatch), 2.0f);
            g.setColour(juce::Colours::lightgrey);
            g.drawText(entries[i].label, cx + swatch + 4, y, cellW - swatch - 4, legendArea.getHeight(),
                       juce::Justification::left);
        }
    }
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
    modelLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(6);
    detectRect.setBounds(area.removeFromTop(40));
    area.removeFromTop(4);
    scoreLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);
    legendArea = area.removeFromTop(18);
    area.removeFromTop(12);
    vtsRegisterButton.setBounds(area.removeFromTop(40));
    area.removeFromTop(8);
    vtsStatusLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(12);
    playBajiButton.setBounds(area.removeFromTop(34));
    area.removeFromTop(8);
    playRuButton.setBounds(area.removeFromTop(34));
}
