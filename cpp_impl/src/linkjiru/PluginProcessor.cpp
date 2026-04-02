#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AnalysisThread.h"

LinkjiruProcessor::LinkjiruProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    /* Some hosts are absolute menaces and will happily send wildly different
     * block sizes per processBlock callback (7, 127, 1000, you name it)
     * without ever bothering to call prepareToPlay again like the spec says
     * they should. We do resize in prepareToPlay like good citizens, but we
     * also pre-allocate 65536 floats (256KB) here because we refuse to
     * allocate on the audio thread when some rogue host decides to surprise
     * us with a block size it never mentioned. Only the first N samples get
     * touched per callback — the rest sits cold in virtual memory. */
    monoMixBuf.resize(65536);
}

LinkjiruProcessor::~LinkjiruProcessor()
{
    analysisThread.reset();
}

const juce::String LinkjiruProcessor::getName() const { return JucePlugin_Name; }
bool LinkjiruProcessor::acceptsMidi() const { return false; }
bool LinkjiruProcessor::producesMidi() const { return false; }
bool LinkjiruProcessor::isMidiEffect() const { return false; }
double LinkjiruProcessor::getTailLengthSeconds() const { return 0.0; }
int LinkjiruProcessor::getNumPrograms() { return 1; }
int LinkjiruProcessor::getCurrentProgram() { return 0; }
void LinkjiruProcessor::setCurrentProgram(int) {}
const juce::String LinkjiruProcessor::getProgramName(int) { return {}; }
void LinkjiruProcessor::changeProgramName(int, const juce::String &) {}

void LinkjiruProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    monoMixBuf.resize(static_cast<size_t>(samplesPerBlock));
}

void LinkjiruProcessor::releaseResources()
{
    if (analysisThread)
    {
        analysisThread->stopThread(3000);
        analysisThread.reset();
    }

    analysisRunning.store(false);
}

bool LinkjiruProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    const auto &mainOutput = layouts.getMainOutputChannelSet();
    const auto &mainInput = layouts.getMainInputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono() && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    return mainInput == mainOutput;
}

void LinkjiruProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
        return;

    if (!analysisRunning.load(std::memory_order_acquire))
        // we need some priority over a default memory_order_relaxed
        return;

    // Mono mixdown — average across channels
    const float gain = 1.0f / static_cast<float>(numChannels);
    const int n = std::min(numSamples, static_cast<int>(monoMixBuf.size()));
    if (n <= 0)
        return;

    for (int i = 0; i < n; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sample += buffer.getReadPointer(ch)[i];
        monoMixBuf[static_cast<size_t>(i)] = sample * gain;
    }

    sharedBuffer.write(monoMixBuf.data(), n);
}

void LinkjiruProcessor::startAnalysis()
{
    bool expected = false;
    if (!analysisRunning.compare_exchange_strong(expected, true))
        return;

    analysisThread = std::make_unique<AnalysisThread>(sharedBuffer, currentSampleRate);
    analysisThread->startThread();
}

void LinkjiruProcessor::stopAnalysis()
{
    if (analysisThread)
    {
        analysisThread->stopThread(3000);
        analysisThread.reset();
    }

    analysisRunning.store(false);
}

void LinkjiruProcessor::restartAnalysis()
{
    stopAnalysis();
    startAnalysis();
}

bool LinkjiruProcessor::isVtsConnected() const
{
    return analysisThread && analysisThread->isVtsConnected();
}

bool LinkjiruProcessor::isVtsRegistered() const
{
    return analysisThread && analysisThread->isVtsRegistered();
}

void LinkjiruProcessor::requestVtsRegister() const
{
    if (analysisThread)
        analysisThread->requestVtsRegister();
}

float LinkjiruProcessor::getDetectValue() const
{
    return analysisThread ? analysisThread->getDetectValue() : 0.0f;
}

void LinkjiruProcessor::getStateInformation(juce::MemoryBlock &) {}
void LinkjiruProcessor::setStateInformation(const void *, int) {}

juce::AudioProcessorEditor *LinkjiruProcessor::createEditor()
{
    return new LinkjiruEditor(*this);
}

bool LinkjiruProcessor::hasEditor() const { return true; }

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new LinkjiruProcessor();
}
