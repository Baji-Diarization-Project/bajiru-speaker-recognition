#pragma once

#include <juce_core/juce_core.h>
#include "SharedRingBuffer.h"
#include "Analyzer.h"
#include "VTubeStudioClient.h"
#include <memory>
#include <string>
#include <vector>

class AnalysisThread final : public juce::Thread
{
public:
    static constexpr int ringBufferCapacity = 131072;

    struct Config
    {
        int analysisWindowSamples = 2048; // ~46ms at 44.1kHz (not that we are using 44.1kHz)
        int pollIntervalMs        = 16;   // ~60fps to match VTube Studio
        int sustainMs             = 100;  // hold speech state after last detection
        std::string vtsHost       = "localhost";
        std::string vtsPort       = "8001";
    };

    AnalysisThread(const SharedRingBuffer<ringBufferCapacity>& buffer, std::unique_ptr<Analyzer> analyzerIn)
        : Thread("LinkjiruAnalysis"), sharedBuffer(buffer), analyzer(std::move(analyzerIn)), config()
    {
    }

    AnalysisThread(const SharedRingBuffer<ringBufferCapacity>& buffer, std::unique_ptr<Analyzer> analyzerIn,
                   const Config& config)
        : Thread("LinkjiruAnalysis"), sharedBuffer(buffer), analyzer(std::move(analyzerIn)), config(config)
    {
    }

    ~AnalysisThread() override { stopThread(3000); }

    // VTS state (read by UI via processor passthrough)
    bool isVtsConnected() const { return vtsConnected.load(); }
    bool isVtsRegistered() const { return vtsRegistered.load(); }
    bool isRegisterFailed() const { return registerFailed.load(); }

    // Model output (read by UI via processor passthrough)
    float getDetectValue() const { return detectValue.load(); }

    // Called from UI thread via processor — sets a flag the run loop picks up
    void requestVtsRegister()
    {
        registerFailed.store(false);
        registerRequested.store(true);
    }

    void run() override
    {
        std::vector<float> windowBuf(config.analysisWindowSamples);

        // VTube Studio connection
        VTubeStudioClient vtsClient;
        int64_t lastReconnectAttempt             = 0;
        static constexpr int reconnectIntervalMs = 5000;

        bool currentlySpeaking = false;
        int64_t lastSpeechTime = 0;

        while (!threadShouldExit())
        {
            const auto now = juce::Time::currentTimeMillis();

            // Manage VTS connection
            if (!vtsClient.isConnected())
            {
                vtsConnected.store(false);
                vtsRegistered.store(false);

                if (now - lastReconnectAttempt >= reconnectIntervalMs)
                {
                    lastReconnectAttempt = now;

                    if (vtsClient.connect(config.vtsHost, config.vtsPort) && vtsClient.authenticate())
                    {
                        vtsConnected.store(true);
                    }
                }
            }

            // Handle registration request from UI
            if (vtsClient.isConnected() && !vtsRegistered.load() && registerRequested.load())
            {
                if (vtsClient.registerParameter("LinkjiruDetectLowji", "1 when speaker detected, 0 otherwise", 0.0f,
                                                1.0f, 0.0f))
                {
                    vtsRegistered.store(true);
                    registerRequested.store(false);
                }
                else
                {
                    registerFailed.store(true);
                    registerRequested.store(false);
                }
            }

            // Read latest audio from ring buffer
            sharedBuffer.readLastN(windowBuf.data(), config.analysisWindowSamples);

            // Calibration phase
            if (!analyzer->isCalibrated())
            {
                analyzer->calibrate(windowBuf.data(), config.analysisWindowSamples);
                sleep(config.pollIntervalMs);
                continue;
            }

            // Speech detection
            const bool speechDetected = analyzer->isSpeechActive(windowBuf.data(), config.analysisWindowSamples);

            if (speechDetected)
            {
                lastSpeechTime    = now;
                currentlySpeaking = true;
            }
            else if (currentlySpeaking && (now - lastSpeechTime > config.sustainMs))
            {
                currentlySpeaking = false;
            }

            const float value = currentlySpeaking ? 1.0f : 0.0f;
            detectValue.store(value);

            // Send to VTube Studio every frame (only if registered)
            if (vtsClient.isConnected() && vtsRegistered.load())
            {
                vtsClient.injectParameter("LinkjiruDetectLowji", value);
            }

            sleep(config.pollIntervalMs);
        }

        // Clean shutdown
        if (vtsClient.isConnected())
        {
            if (vtsRegistered.load())
            {
                vtsClient.injectParameter("LinkjiruDetectLowji", 0.0f);
            }

            vtsClient.disconnect();
        }

        detectValue.store(0.0f);
        vtsConnected.store(false);
        vtsRegistered.store(false);
    }

private:
    const SharedRingBuffer<ringBufferCapacity>& sharedBuffer;
    std::unique_ptr<Analyzer> analyzer;
    Config config;

    std::atomic<bool> vtsConnected{false};
    std::atomic<bool> vtsRegistered{false};
    std::atomic<bool> registerRequested{false};
    std::atomic<bool> registerFailed{false};
    std::atomic<float> detectValue{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisThread)
};
