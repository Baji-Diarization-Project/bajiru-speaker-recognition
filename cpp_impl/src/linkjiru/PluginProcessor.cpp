#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AnalysisThread.h"
#include "OnnxRunner.hpp"
#include "SampleData.hpp"

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{

// A file shipped next to the plugin binary (see CMake).
juce::File siblingFile(const char* name)
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile(name);
}

std::string resolveModelPath()
{
    return siblingFile("runtime_model.onnx").getFullPathName().toStdString();
}

#if JUCE_WINDOWS
// SEH-isolated (no C++ locals to unwind, so no C2712): bind the C++ API to the
// runtime we just loaded. A version mismatch makes GetApi() return null; a hard
// delay-load failure raises an SEH exception we swallow.
bool bindOrtApi()
{
    __try
    {
        Ort::InitApi();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return Ort::Global<void>::api_ != nullptr;
}
#endif

// Load our bundled onnxruntime.dll and bind the C++ API to it before any Ort
// object exists. With ORT_API_MANUAL_INIT (see OnnxRunner.hpp), this is what keeps
// a stray system onnxruntime.dll from being picked up. We add the plugin dir to
// the search path (so DirectML.dll resolves), pin our exact DLL by full path (so a
// wrong-version system copy can't shadow it), then InitApi(). Returns false (and
// analysis won't start) if the runtime can't be loaded or bound.
bool prepareOrtRuntime()
{
#if JUCE_WINDOWS
    const auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    AddDllDirectory(dir.getFullPathName().toWideCharPointer());

    const auto ortDll = dir.getChildFile("onnxruntime.dll").getFullPathName();
    if (LoadLibraryExW(ortDll.toWideCharPointer(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) == nullptr)
    {
        return false;
    }
    return bindOrtApi();
#else
    Ort::InitApi();
    return Ort::Global<void>::api_ != nullptr;
#endif
}

} // namespace

LinkjiruProcessor::LinkjiruProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // ORT loads lazily on the first startAnalysis() (see prepareOrtRuntime), so
    // plugin scanning/instantiation never pulls in the runtime.

    // The one automatable control: analysis on/off. Owned by the base processor;
    // we listen so UI clicks and host automation drive the same start/stop path.
    addParameter(analysisParam =
                     new juce::AudioParameterBool(juce::ParameterID{"analysisActive", 1}, "Analysis Active", false));
    analysisParam->addListener(this);

    // Size the mono mix buffer to its hard cap once and never resize it: some
    // hosts send varying block sizes without re-calling prepareToPlay, and a
    // resize on any thread would race the audio thread's read. Oversized blocks
    // are truncated in processBlock.
    monoMixBuf.resize(linkjiru::maxAudioBlockSamples);

    // Best-effort: pin the RT buffers into the working set (VirtualLock) so the
    // audio thread doesn't page-fault touching them. Fixed size and address for our
    // lifetime. VirtualLock often fails on large regions (working-set quota); fine,
    // since every callback touches these buffers and keeps them resident anyway.
    hostRingLock.lock(hostRing.storage(), hostRing.byteSize());
    monoMixLock.lock(monoMixBuf.data(), monoMixBuf.size() * sizeof(float));
}

LinkjiruProcessor::~LinkjiruProcessor()
{
    if (analysisParam != nullptr)
    {
        analysisParam->removeListener(this);
    }
    cancelPendingUpdate(); // no reconcile can fire after we start tearing down
    analysisThread.reset();
}

const juce::String LinkjiruProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LinkjiruProcessor::acceptsMidi() const
{
    return false;
}

bool LinkjiruProcessor::producesMidi() const
{
    return false;
}

bool LinkjiruProcessor::isMidiEffect() const
{
    return false;
}

double LinkjiruProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int LinkjiruProcessor::getNumPrograms()
{
    return 1;
}

int LinkjiruProcessor::getCurrentProgram()
{
    return 0;
}

void LinkjiruProcessor::setCurrentProgram(int) {}

const juce::String LinkjiruProcessor::getProgramName(int)
{
    return {};
}

void LinkjiruProcessor::changeProgramName(int, const juce::String&) {}

void LinkjiruProcessor::prepareToPlay(const double sampleRate, const int)
{
    // Just record the host rate; the analysis thread owns the resampler and is
    // configured for this rate when startAnalysis() builds it. The audio thread only
    // downmixes and writes host-rate mono to hostRing (see processBlock).
    currentSampleRate = sampleRate;
}

void LinkjiruProcessor::releaseResources()
{
    if (analysisThread)
    {
        analysisThread->stopThread(linkjiru::threadStopTimeoutMs);
        analysisThread.reset();
    }

    analysisRunning.store(false);
}

bool LinkjiruProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    const auto& mainInput  = layouts.getMainInputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono() && mainOutput != juce::AudioChannelSet::stereo())
    {
        return false;
    }

    return mainInput == mainOutput;
}

void LinkjiruProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
    {
        return;
    }

    // Test-sample playback (UI buttons) takes over the input while active; see
    // streamTestSample. A new command resets the read position.
    const int req = samplePlaying.load(std::memory_order_acquire);
    if (req != currentlyPlaying)
    {
        currentlyPlaying = req;
        samplePos        = 0;
        playbackInterp.reset();
    }
    if (currentlyPlaying >= 0)
    {
        streamTestSample(buffer, numSamples, numChannels);
        return;
    }

    if (!analysisRunning.load(std::memory_order_acquire))
    {
        return;
    }

    // Live input: downmix to mono and hand the host-rate samples to the analysis
    // thread via hostRing. That's the whole audio-thread cost; no resampling here.
    // The manager drains hostRing and resamples to 48 kHz on its own thread.
    const float gain = 1.0f / static_cast<float>(numChannels);
    const int n      = std::min(numSamples, static_cast<int>(monoMixBuf.size()));
    if (n <= 0)
    {
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            sample += buffer.getReadPointer(ch)[i];
        }
        monoMixBuf[static_cast<size_t>(i)] = sample * gain;
    }

    hostRing.write(monoMixBuf.data(), n);
}

void LinkjiruProcessor::streamTestSample(juce::AudioBuffer<float>& buffer, const int numSamples, const int numChannels)
{
    const float* src        = (currentlyPlaying == 0) ? linkjiru::samples::baji : linkjiru::samples::ru;
    const int len           = (currentlyPlaying == 0) ? linkjiru::samples::bajiCount : linkjiru::samples::ruCount;
    const bool feedAnalysis = analysisRunning.load(std::memory_order_acquire);
    const int outN          = std::min(numSamples, static_cast<int>(monoMixBuf.size()));

    // 48 kHz frames consumed this block: advances the read position and is the
    // exact slice pushed to the ring buffer (pristine, no round-trip).
    int consumed = 0;

    if (currentSampleRate == static_cast<double>(linkjiru::modelSampleRate))
    {
        // Host already at 48 kHz: 1:1 for both output and analysis, no resampling.
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = (samplePos + i < len) ? src[samplePos + i] : 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                buffer.getWritePointer(ch)[i] = v;
            }
        }
        consumed = numSamples;
    }
    else
    {
        // Resample 48 kHz -> host rate for output; the interpolator reports how many
        // 48 kHz frames it consumed. speedRatio = in / out.
        const double speedRatio = static_cast<double>(linkjiru::modelSampleRate) / currentSampleRate;
        const int available     = std::max(0, len - samplePos);
        consumed = playbackInterp.process(speedRatio, src + samplePos, monoMixBuf.data(), outN, available, 0);

        for (int i = 0; i < numSamples; ++i)
        {
            const float v = (i < outN) ? monoMixBuf[static_cast<size_t>(i)] : 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                buffer.getWritePointer(ch)[i] = v;
            }
        }
    }

    // Feed the manager host-rate audio via hostRing, the same path as live input, so
    // the audio thread never writes the model ring. At 48 kHz that's the pristine
    // sample slice (the manager bypasses); at other rates it's the output signal we
    // just produced (a transparent round trip through the resampler).
    if (feedAnalysis)
    {
        if (currentSampleRate == static_cast<double>(linkjiru::modelSampleRate))
        {
            const int count = std::min(consumed, std::max(0, len - samplePos));
            if (count > 0)
            {
                hostRing.write(src + samplePos, count);
            }
        }
        else if (outN > 0)
        {
            hostRing.write(monoMixBuf.data(), outN);
        }
    }

    samplePos += consumed;
    if (samplePos >= len)
    {
        samplePlaying.store(-1, std::memory_order_release);
        currentlyPlaying = -1;
        samplePos        = 0;
        playbackInterp.reset();
    }
}

void LinkjiruProcessor::startAnalysis(const std::string& vtsHost, const std::string& vtsPort)
{
    bool expected = false;
    if (!analysisRunning.compare_exchange_strong(expected, true))
    {
        return;
    }

    // Load + bind the ORT runtime on the first start. If it can't be loaded, fail
    // gracefully: don't construct the runner (its Ort members would deref a null
    // API and crash).
    if (!ortRuntimeInitialized)
    {
        ortRuntimeReady       = prepareOrtRuntime();
        ortRuntimeInitialized = true;
    }
    if (!ortRuntimeReady)
    {
        analysisRunning.store(false);
        return;
    }

    AnalysisThread::Config threadConfig;
    threadConfig.hostSampleRate = currentSampleRate;
    threadConfig.vtsHost        = vtsHost;
    threadConfig.vtsPort        = vtsPort;

    auto runner    = std::make_unique<OnnxRunner>(resolveModelPath());
    analysisThread = std::make_unique<AnalysisThread>(hostRing, std::move(runner), threadConfig);
    analysisThread->startThread();
}

void LinkjiruProcessor::stopAnalysis()
{
    if (analysisThread)
    {
        analysisThread->stopThread(linkjiru::threadStopTimeoutMs);
        analysisThread.reset();
    }

    analysisRunning.store(false);
}

void LinkjiruProcessor::restartAnalysis()
{
    stopAnalysis();
    startAnalysis();
}

void LinkjiruProcessor::setAnalysisActive(const bool active)
{
    // Drive the parameter (not startAnalysis directly) so the host sees the gesture
    // and UI + automation share one source of truth. Message thread only.
    if (analysisParam == nullptr)
    {
        return;
    }
    analysisParam->beginChangeGesture();
    analysisParam->setValueNotifyingHost(active ? 1.0f : 0.0f);
    analysisParam->endChangeGesture();
}

void LinkjiruProcessor::requestRestart()
{
    restartPending.store(true);
    triggerAsyncUpdate();
}

void LinkjiruProcessor::parameterValueChanged(int, float)
{
    // Called on the host's automation thread (possibly the audio thread): never
    // start/stop here: that blocks and isn't RT-safe. Defer to the message thread.
    triggerAsyncUpdate();
}

void LinkjiruProcessor::parameterGestureChanged(int, bool) {}

void LinkjiruProcessor::handleAsyncUpdate()
{
    // Message thread: reconcile the analysis thread to the parameter. A pending
    // restart forces a stop+start even when the parameter stays on.
    if (restartPending.exchange(false))
    {
        stopAnalysis();
        if (analysisParam->get())
        {
            startAnalysis();
        }
        return;
    }

    const bool want = analysisParam->get();
    if (want && !isAnalysisRunning())
    {
        startAnalysis();
    }
    else if (!want && isAnalysisRunning())
    {
        stopAnalysis();
    }
}

PipelineStatus LinkjiruProcessor::getStatus() const
{
    PipelineStatus s;
    s.running = analysisRunning.load();

    if (analysisThread)
    {
        const uint32_t bits = analysisThread->getStatusBits();
        s.modelReady        = (bits & linkjiru::statusModelReady) != 0;
        s.inferenceFailed   = (bits & linkjiru::statusInferenceFailed) != 0;
        s.vtsConnected      = (bits & linkjiru::statusVtsConnected) != 0;
        s.vtsRegistered     = (bits & linkjiru::statusVtsRegistered) != 0;
        s.vtsRegisterFailed = (bits & linkjiru::statusVtsRegisterFailed) != 0;
        s.detectValue       = analysisThread->getDetectValue();
        s.rawScore          = analysisThread->getRawScore();
        s.droppedWindows    = analysisThread->getDroppedWindows();
        s.speaker           = analysisThread->getSpeaker();
        s.lastInferenceMs   = analysisThread->getLastInferenceMs();
        s.inferenceCount    = analysisThread->getInferenceCount();
        analysisThread->getScores(s.classScores);
    }

    return s;
}

bool LinkjiruProcessor::isVtsConnected() const
{
    return analysisThread && (analysisThread->getStatusBits() & linkjiru::statusVtsConnected) != 0;
}

bool LinkjiruProcessor::isVtsRegistered() const
{
    return analysisThread && (analysisThread->getStatusBits() & linkjiru::statusVtsRegistered) != 0;
}

void LinkjiruProcessor::requestVtsRegister() const
{
    if (analysisThread)
    {
        analysisThread->requestVtsRegister();
    }
}

float LinkjiruProcessor::getDetectValue() const
{
    return analysisThread ? analysisThread->getDetectValue() : 0.0f;
}

void LinkjiruProcessor::playSample(const int which)
{
    // 0 = baji, 1 = ru; anything else stops playback. Picked up by processBlock.
    samplePlaying.store((which == 0 || which == 1) ? which : -1, std::memory_order_release);
}

bool LinkjiruProcessor::hasSamples()
{
    return linkjiru::samples::bajiCount > 0 && linkjiru::samples::ruCount > 0;
}

std::vector<double> LinkjiruProcessor::benchmarkInference(const int iterations)
{
    if (!ortRuntimeInitialized)
    {
        ortRuntimeReady       = prepareOrtRuntime();
        ortRuntimeInitialized = true;
    }
    if (!ortRuntimeReady)
    {
        return {};
    }

    OnnxRunner runner(resolveModelPath());
    if (!runner.prepare())
    {
        return {};
    }

    // One window of silence: GPU compute is input-independent (same graph and
    // tensor sizes), so this times the real inference cost.
    const std::vector<float> window(static_cast<size_t>(linkjiru::modelWindowSamples), 0.0f);
    return runner.benchmark(window.data(), linkjiru::modelWindowSamples, iterations);
}

void LinkjiruProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Persist analysis on/off so it survives a DAW save/reload.
    juce::MemoryOutputStream(destData, false).writeBool(analysisParam->get());
}

void LinkjiruProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 1)
    {
        return;
    }
    juce::MemoryInputStream in(data, static_cast<size_t>(sizeInBytes), false);
    const bool active = in.readBool();
    // Reconciled to the analysis thread on the message thread (see handleAsyncUpdate).
    analysisParam->setValueNotifyingHost(active ? 1.0f : 0.0f);
}

juce::AudioProcessorEditor* LinkjiruProcessor::createEditor()
{
    return new LinkjiruEditor(*this);
}

bool LinkjiruProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LinkjiruProcessor();
}
