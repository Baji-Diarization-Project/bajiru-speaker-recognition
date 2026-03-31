#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BufferWriterThread.h"

class LinkjiruProcessor final : public juce::AudioProcessor
{
public:
    LinkjiruProcessor();
    ~LinkjiruProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String &newName) override;

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    void startAnalysis();
    void stopAnalysis();
    void restartAnalysis();
    bool isAnalysisRunning() const { return analysisRunning.load(); }

private:
    static constexpr int fifoSize = 131072; // ~3s at 44.1kHz mono

    juce::AbstractFifo fifo{fifoSize};
    std::array<float, fifoSize> fifoBuffer{};

    std::atomic<bool> analysisRunning{false};
    std::unique_ptr<BufferWriterThread> writerThread;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkjiruProcessor)
};
