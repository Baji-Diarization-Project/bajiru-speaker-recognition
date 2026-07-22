#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Constants.h"
#include "SharedRingBuffer.h"
#include "PageLock.h"
#include "PipelineStatus.h"
#include <string>
#include <vector>

class AnalysisThread;

class LinkjiruProcessor final : public juce::AudioProcessor,
                                private juce::AudioProcessorParameter::Listener,
                                private juce::AsyncUpdater
{
public:
    LinkjiruProcessor();
    ~LinkjiruProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
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
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void startAnalysis(const std::string& vtsHost = linkjiru::defaultVtsHost,
                       const std::string& vtsPort = linkjiru::defaultVtsPort);
    void stopAnalysis();
    void restartAnalysis();
    bool isAnalysisRunning() const { return analysisRunning.load(); }

    // Analysis on/off is exposed as the automatable "Analysis Active" parameter, so
    // a host can toggle and record it like the UI does. These set that
    // parameter with host notification; the actual start/stop is reconciled on the
    // message thread (parameter changes can arrive on the audio thread). Restart
    // forces a stop+start even while the parameter stays on.
    void setAnalysisActive(bool active);
    void requestRestart();

    // One coherent snapshot of pipeline state for the UI.
    PipelineStatus getStatus() const;
    void requestVtsRegister() const;
    float getDetectValue() const;

    // Individual accessors retained for tests.
    bool isVtsConnected() const;
    bool isVtsRegistered() const;

    // Play a bundled test sample (0 = baji, 1 = ru): into the output and the
    // analysis input, as if it were live input. UI buttons call this.
    void playSample(int which);
    static bool hasSamples();

    // Benchmark GPU inference latency (headless host only, not real-time): build a
    // throwaway runner and run `iterations` synchronous inferences over one window.
    // Returns per-run latencies (ms), or empty if the runtime/model/GPU is absent.
    std::vector<double> benchmarkInference(int iterations);

private:
    // The audio thread writes host-rate mono into hostRing (wait-free); the analysis
    // thread drains and resamples it. hostRing lives here (not in the manager) so the
    // audio thread's writes can't outlive a torn-down manager. The 48 kHz model ring
    // lives in the AnalysisThread; it's that thread's sole writer and reader.
    SharedRingBuffer<linkjiru::ringBufferCapacity> hostRing;

    std::atomic<bool> analysisRunning{false};
    std::unique_ptr<AnalysisThread> analysisThread;

    // "Analysis Active": the automatable on/off control, owned by the base
    // AudioProcessor (addParameter). We listen for changes and, on the message
    // thread, reconcile the analysis thread to match. restartPending forces a
    // stop+start on the next reconcile.
    juce::AudioParameterBool* analysisParam = nullptr;
    std::atomic<bool> restartPending{false};

    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;

    // ORT runtime load state (message-thread only). Loaded lazily on first
    // startAnalysis(); if it can't be bound, ortRuntimeReady stays false and
    // analysis refuses to start rather than crashing.
    bool ortRuntimeInitialized = false;
    bool ortRuntimeReady       = false;

    double currentSampleRate = 44100.0;
    std::vector<float> monoMixBuf; // downmix scratch (live input + streamTestSample)

    // Bundled test samples are embedded (SampleData.hpp); processBlock reads them
    // directly (see playSample/streamTestSample). samplePlaying is the UI-thread
    // command; currentlyPlaying/samplePos are audio-thread only.
    std::atomic<int> samplePlaying{-1}; // -1 none, 0 baji, 1 ru
    int currentlyPlaying = -1;
    int samplePos        = 0;                  // read position in the 48 kHz sample array
    juce::LagrangeInterpolator playbackInterp; // 48 kHz sample -> host rate for output

    // Stream the selected test sample (audio thread): resample 48 kHz -> host rate
    // for output, and feed the raw 48 kHz to the ring buffer so the model sees the
    // pristine sample. Replaces live input while playing.
    void streamTestSample(juce::AudioBuffer<float>& buffer, int numSamples, int numChannels);

    // Page-locks pinning the RT buffers. Declared last so they destruct first,
    // unlocking while the buffers they pin are still alive.
    PageLock hostRingLock;
    PageLock monoMixLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkjiruProcessor)
};
