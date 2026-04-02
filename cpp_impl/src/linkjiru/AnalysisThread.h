// ReSharper disable CppDFAConstantConditions
#pragma once

#include <juce_core/juce_core.h>
#include "SharedRingBuffer.h"
#include "RmsAnalyzer.h"
#include "VTubeStudioClient.h"
#include <vector>

class AnalysisThread final : public juce::Thread
{
public:
    static constexpr int ringBufferCapacity = 131072;

    struct Config
    {
        int   analysisWindowSamples = 2048;  // ~46ms at 44.1kHz
        int   pollIntervalMs        = 16;    // ~60fps to match VTube Studio
        int   sustainMs             = 100;   // hold speech state after last detection
        int   calibrationDurationMs = 1500;  // 1.5s of noise floor measurement
        float noiseFloorMultiplier  = 4.0f;  // threshold = noiseFloor * this
    };

    AnalysisThread(const SharedRingBuffer<ringBufferCapacity>& buffer,
                   const double sampleRate,
                   const Config& config = {})
        : Thread("LinkjiruAnalysis"),
          sharedBuffer(buffer),
          sampleRate(sampleRate),
          config(config)
    {
    }

    ~AnalysisThread() override
    {
        stopThread(3000);
    }

    // VTS state (read by UI via processor passthrough)
    bool isVtsConnected() const    { return vtsConnected.load(); }
    bool isVtsRegistered() const   { return vtsRegistered.load(); }

    // Model output (read by UI via processor passthrough)
    float getDetectValue() const   { return detectValue.load(); }

    // Called from UI thread via processor — sets a flag the run loop picks up
    void requestVtsRegister()      { registerRequested.store(true); }

    void run() override
    {
        // Set up RMS analyzer
        RmsAnalyzer::Config rmsConfig;
        rmsConfig.noiseFloorMultiplier = config.noiseFloorMultiplier;
        rmsConfig.calibrationSamples   = static_cast<int>(
            sampleRate * config.calibrationDurationMs / 1000.0);

        RmsAnalyzer analyzer(rmsConfig);
        std::vector<float> windowBuf(config.analysisWindowSamples);

        // VTube Studio connection
        VTubeStudioClient vtsClient;
        int64_t lastReconnectAttempt = 0;
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

                    if (vtsClient.connect() && vtsClient.authenticate())
                        vtsConnected.store(true);
                }
            }

            // Handle registration request from UI
            if (vtsClient.isConnected() &&
                !vtsRegistered.load() &&
                registerRequested.load())
            {
                if (vtsClient.registerParameter(
                        "LinkjiruDetectLowji",
                        "1 when speaker detected, 0 otherwise",
                        0.0f, 1.0f, 0.0f))
                {
                    vtsRegistered.store(true);
                }

                registerRequested.store(false);
            }

            // Read latest audio from ring buffer
            sharedBuffer.readLastN(windowBuf.data(), config.analysisWindowSamples);

            // Calibration phase
            if (!analyzer.isCalibrated())
            {
                analyzer.calibrate(windowBuf.data(), config.analysisWindowSamples);
                sleep(config.pollIntervalMs);
                continue;
            }

            // Speech detection
            // ReSharper disable once CppDFAUnreachableCode
            const bool speechDetected = analyzer.isSpeechActive(
                windowBuf.data(), config.analysisWindowSamples);

            if (speechDetected)
            {
                lastSpeechTime = now;

                if (!currentlySpeaking)
                    currentlySpeaking = true;
            }
            else if (currentlySpeaking &&
                     (now - lastSpeechTime > config.sustainMs))
            {
                currentlySpeaking = false;
            }

            const float value = currentlySpeaking ? 1.0f : 0.0f;
            detectValue.store(value);

            // Send to VTube Studio every frame (only if registered)
            if (vtsClient.isConnected() && vtsRegistered.load())
                vtsClient.injectParameter("LinkjiruDetectLowji", value);

            sleep(config.pollIntervalMs);
        }

        // Clean shutdown
        if (vtsClient.isConnected())
        {
            if (vtsRegistered.load())
                vtsClient.injectParameter("LinkjiruDetectLowji", 0.0f);

            vtsClient.disconnect();
        }

        detectValue.store(0.0f);
        vtsConnected.store(false);
        vtsRegistered.store(false);
    }

private:
    const SharedRingBuffer<ringBufferCapacity>& sharedBuffer;
    double sampleRate;
    Config config;

    std::atomic<bool> vtsConnected{false};
    std::atomic<bool> vtsRegistered{false};
    std::atomic<bool> registerRequested{false};
    std::atomic<float> detectValue{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisThread)
};
