#include "PluginProcessor.h"
#include "PluginEditor.h"

LinkjiruProcessor::LinkjiruProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

LinkjiruProcessor::~LinkjiruProcessor()
{
    if (writerThread)
        writerThread->stopThread(2000);
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

void LinkjiruProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    fifo.reset();
    fifoBuffer.fill(0.0f);
}

void LinkjiruProcessor::releaseResources()
{
    if (writerThread)
    {
        writerThread->stopThread(2000);
        writerThread.reset();
        analysisRunning.store(false);
    }
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

    const float gain = 1.0f / static_cast<float>(numChannels);
    const auto scope = fifo.write(numSamples);

    for (int i = 0; i < scope.blockSize1; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sample += buffer.getReadPointer(ch)[i];
        fifoBuffer[static_cast<size_t>(scope.startIndex1 + i)] = sample * gain;
    }

    for (int i = 0; i < scope.blockSize2; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sample += buffer.getReadPointer(ch)[scope.blockSize1 + i];
        fifoBuffer[static_cast<size_t>(scope.startIndex2 + i)] = sample * gain;
    }

    // Audio passes through unchanged
}

void LinkjiruProcessor::startAnalysis()
{
    if (writerThread && writerThread->isThreadRunning())
        return;

    writerThread = std::make_unique<BufferWriterThread>(fifo, fifoBuffer);
    writerThread->startThread();
    analysisRunning.store(true);
}

void LinkjiruProcessor::stopAnalysis()
{
    if (writerThread)
    {
        writerThread->stopThread(2000);
        writerThread.reset();
    }

    analysisRunning.store(false);
}

void LinkjiruProcessor::restartAnalysis()
{
    stopAnalysis();
    fifo.reset();
    fifoBuffer.fill(0.0f);
    startAnalysis();
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
