#pragma once

#include <juce_core/juce_core.h>
#include "Constants.h"
#include "SharedRingBuffer.h"
#include "PageLock.h"
#include "CadenceGate.h"
#include "GatingLogic.hpp"
#include "OnnxRunner.hpp"
#include "PipelineStatus.h"
#include "Resampler.hpp"
#include "VTubeStudioClient.h"
#include <memory>
#include <string>
#include <vector>

// This is our manager thread. It drives the ONNX runner, the VTS client, and the
// Resampler through one non-blocking loop: resampleHostInput() -> vts.pump() ->
// runner->tryHarvest() -> runner->kick() the next (gated by CadenceGate::poll()) ->
// vts.setDetectValue() to push. Doing the resample here keeps the FIR off the audio
// thread entirely. Nothing blocks, so a stop is picked up within a tick.
class AnalysisThread final : public juce::Thread
{
public:
    struct Config
    {
        int pollIntervalMs    = 16;      // ~60 fps to match VTube Studio
        double hostSampleRate = 48000.0; // rate of the samples in the host ring
        std::string vtsHost   = linkjiru::defaultVtsHost;
        std::string vtsPort   = linkjiru::defaultVtsPort;
    };

    // hostRing: audio-thread-written host-rate mono we drain and resample. We own the
    // 48 kHz model ring (modelBuffer) outright; we are its sole writer and reader.
    AnalysisThread(const SharedRingBuffer<linkjiru::ringBufferCapacity>& hostRing, std::unique_ptr<OnnxRunner> runnerIn,
                   const Config& cfg)
        : Thread("LinkjiruAnalysis"), hostBuffer(hostRing), runner(std::move(runnerIn)), config(cfg)
    {
        resampler.prepare(cfg.hostSampleRate);
        drainScratch.resize(static_cast<size_t>(kDrainChunk));
        hostCursor = hostBuffer.getWriteCount(); // start at "now"; ignore any pre-start audio
        modelRingLock.lock(modelBuffer.storage(), modelBuffer.byteSize());
    }

    ~AnalysisThread() override { stopThread(linkjiru::threadStopTimeoutMs); }

    // All status bools packed into one word; the processor unpacks it.
    uint32_t getStatusBits() const { return statusBits.load(std::memory_order_relaxed); }
    float getDetectValue() const { return detectValue.load(); }
    float getRawScore() const { return lastScore.load(); }
    int getSpeaker() const { return latchedSpeaker.load(std::memory_order_relaxed); }
    void getScores(float* out5) const
    {
        for (int i = 0; i < linkjiru::modelNumClasses; ++i)
        {
            out5[i] = smoothedScores[i].load(std::memory_order_relaxed);
        }
    }
    uint64_t getDroppedWindows() const { return droppedWindows.load(std::memory_order_relaxed); }
    double getLastInferenceMs() const { return runner->lastInferenceMs(); }
    uint64_t getInferenceCount() const { return runner->inferenceCount(); }

    // Called from the UI thread via processor; sets a flag the loop picks up.
    // The VTS client clears its own register-failed state when it performs the
    // request, so we only raise the command flag here.
    void requestVtsRegister() { registerRequested.store(true); }

    void run() override
    {
        // Build + warm up the model (heavy, off the audio/UI threads). On failure
        // the loop runs but never detects; until it returns, statusBits is 0 so the
        // UI reads "loading".
        const bool modelReadyFlag = runner->prepare();
        if (threadShouldExit())
        {
            return;
        }

        constexpr int windowSamples = linkjiru::modelWindowSamples;
        std::vector<float> windowBuf(static_cast<size_t>(windowSamples));
        CadenceGate<> cadenceGate{windowSamples, linkjiru::modelHopSamples, /*requireFullWindow=*/true};
        GatingLogic silenceGate;  // silence + send gating
        ScoreLowPass scoreFilter; // low-pass the per-class scores (anti-flicker)
        Hysteresis detectHysteresis{{linkjiru::detectHigh, linkjiru::detectLow}}; // confidence -> binary

        const VTubeStudioClient vts;
        vts.connect(config.vtsHost, config.vtsPort);

        while (!threadShouldExit())
        {
            const auto now = juce::Time::currentTimeMillis();

            // Drain + resample the audio thread's host-rate input into the model ring.
            resampleHostInput();

            // Service VTS I/O (non-blocking).
            vts.pump();
            if (registerRequested.exchange(false))
            {
                vts.requestRegister();
            }

            // Publish all status bools as one coherent word for the UI.
            uint32_t bits = 0;
            if (modelReadyFlag)
            {
                bits |= linkjiru::statusModelReady;
            }
            if (runner->failed())
            {
                bits |= linkjiru::statusInferenceFailed;
            }
            if (vts.isConnected())
            {
                bits |= linkjiru::statusVtsConnected;
            }
            if (vts.isRegistered())
            {
                bits |= linkjiru::statusVtsRegistered;
            }
            if (vts.isRegisterFailed())
            {
                bits |= linkjiru::statusVtsRegisterFailed;
            }
            statusBits.store(bits, std::memory_order_relaxed);

            // Snapshot the latest window and its RMS for the silence gate.
            const bool haveWindow = modelBuffer.readLastN(windowBuf.data(), windowSamples);
            const float windowRms = haveWindow ? GatingLogic::rms(windowBuf.data(), windowSamples) : 0.0f;

            // Only run the model on sound; during silence hold the last result.
            if (silenceGate.shouldEvaluate(windowRms, now))
            {
                // Harvest, low-pass the scores (so spikes don't flip baji/ru), then
                // run the smoothed max(baji, ru) through the hysteresis.
                float raw[linkjiru::modelNumClasses];
                if (runner->tryHarvest(raw))
                {
                    scoreFilter.update(raw, linkjiru::modelNumClasses);
                    for (int i = 0; i < linkjiru::modelNumClasses; ++i)
                    {
                        smoothedScores[i].store(scoreFilter[i], std::memory_order_relaxed);
                    }

                    const float confidence = std::max(scoreFilter[0], scoreFilter[1]); // max(baji, ru)
                    lastScore.store(confidence);
                    detectHysteresis.update(confidence);
                }

                // Kick the next inference at the hop cadence; if the GPU is still
                // busy, count the dropped window.
                const auto tick = cadenceGate.poll(modelBuffer.getWriteCount());
                if (tick.evaluate)
                {
                    if (runner->isReady() && !runner->inFlight() && haveWindow)
                    {
                        runner->kick(windowBuf.data(), windowSamples);
                    }
                    else if (runner->inFlight())
                    {
                        droppedWindows.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }

            // Binary detect = hysteresis state, forced OFF unless the model is
            // healthy, so a dead model can't leave a stale positive stuck on.
            const bool healthy = modelReadyFlag && !runner->failed();
            const float detect = (healthy && detectHysteresis.value()) ? 1.0f : 0.0f;

            // Latch the active speaker (baji/ru): held through silence, never
            // reset, so the UI leaves IDLE for good after the first detection.
            if (detect > 0.5f)
            {
                latchedSpeaker.store(scoreFilter[0] >= scoreFilter[1] ? 0 : 1, std::memory_order_relaxed);
            }

            // The VTS parameter encodes the latched speaker: 0 = baji, 1 = ru. There
            // is no idle value, so it holds the last speaker through silence, and
            // before the first detection (latch -1) it reads 0 (baji). shouldSend
            // gates on change/keepalive.
            const int speaker    = latchedSpeaker.load(std::memory_order_relaxed);
            const float vtsValue = (speaker == 1) ? 1.0f : 0.0f;
            detectValue.store(vtsValue);
            if (silenceGate.shouldSend(vtsValue, now))
            {
                vts.setDetectValue(vtsValue);
            }

            sleep(config.pollIntervalMs);
        }

        // Clean shutdown: try to leave the parameter at 0, then close.
        vts.setDetectValue(0.0f);
        vts.pump();
        vts.disconnect();

        detectValue.store(0.0f);
        latchedSpeaker.store(-1, std::memory_order_relaxed); // next session starts at IDLE
        for (auto& sc : smoothedScores)
        {
            sc.store(0.0f, std::memory_order_relaxed);
        }
        statusBits.store(0, std::memory_order_relaxed);
    }

private:
    // Host samples pulled per drain call (bounds the resampler + scratch). One loop
    // gathers ~a hop's worth (~735 at 44.1k); we loop the read if more piled up.
    static constexpr int kDrainChunk = 4096;

    // Drain the host-rate audio the audio thread collected, resample it to 48 kHz,
    // and append to the model ring. We are the model ring's only writer.
    void resampleHostInput()
    {
        for (;;)
        {
            const auto rr = hostBuffer.read(drainScratch.data(), kDrainChunk, hostCursor);
            if (rr.samplesRead > 0)
            {
                const auto out = resampler.process(drainScratch.data(), rr.samplesRead);
                if (out.numSamples > 0)
                {
                    modelBuffer.write(out.data, out.numSamples);
                }
            }
            if (rr.samplesRead < kDrainChunk)
            {
                break;
            }
        }
    }

    const SharedRingBuffer<linkjiru::ringBufferCapacity>& hostBuffer; // audio thread writes; we drain
    SharedRingBuffer<linkjiru::ringBufferCapacity> modelBuffer;       // we own it: resample into + read windows from
    std::unique_ptr<OnnxRunner> runner;
    Config config;

    Resampler resampler{linkjiru::modelSampleRate, kDrainChunk};
    std::vector<float> drainScratch;
    uint64_t hostCursor = 0;

    // Pins the model ring into the working set. Declared after modelBuffer so it
    // destructs first, unlocking before the ring's storage is freed.
    PageLock modelRingLock;

    std::atomic<uint32_t> statusBits{0};                            // packed status bools
    std::atomic<bool> registerRequested{false};                     // UI command flag (in)
    std::atomic<float> detectValue{0.0f};                           // 0/1 sent to VTS: 0 baji, 1 ru
    std::atomic<float> lastScore{0.0f};                             // latest smoothed confidence (observability)
    std::atomic<float> smoothedScores[linkjiru::modelNumClasses]{}; // low-passed per-class (UI)
    std::atomic<int> latchedSpeaker{-1};                            // -1 none, 0 baji, 1 ru (held after first detect)
    std::atomic<uint64_t> droppedWindows{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisThread)
};
