#pragma once

#include "Constants.h"

// Suppress the ORT C++ API's static-init auto-load of onnxruntime.dll: it runs at
// module-load via a delay-loaded import, before the plugin can add its own dir to
// the DLL search path, so it can bind to a stale system onnxruntime.dll of the
// wrong version -> GetApi() returns null -> the first Ort call crashes. The
// processor calls Ort::InitApi() explicitly instead, after pinning our own DLL.
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Runs the speaker-detection ONNX model on the GPU via DirectML.
//
// Async: kick() starts an inference and returns; the loop harvests it on a later
// tick, so nothing here blocks. No fallback: if the session can't be built or an
// inference fails, the runner reports failed()/!isReady() and stops detecting.
//
// Single-threaded: prepare()/kick()/tryHarvest() run on the analysis thread; the
// RunAsync completion runs on an ORT thread and hands back its result via atomics.
// Header-only, so only the analysis path pulls in the ONNX Runtime headers.
class OnnxRunner
{
public:
    explicit OnnxRunner(std::string modelPath) : modelPath(std::move(modelPath)) {}

    OnnxRunner(const OnnxRunner&)            = delete;
    OnnxRunner& operator=(const OnnxRunner&) = delete;

    ~OnnxRunner()
    {
        // Don't tear the session down while an async run is in flight; its
        // completion would touch freed state. Wait briefly; a GPU that never
        // returns is pathological (TDR territory), so we give up and proceed.
        for (int i = 0; i < 200 && running.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // Build the DirectML session and warm it up. Blocking; call once on the
    // analysis thread before the loop. Returns false (isReady() stays false) if
    // the model or DirectML is unavailable.
    bool prepare()
    {
        try
        {
            // DirectML requires sequential execution + no memory pattern, else
            // session creation fails.
            sessionOptions.SetExecutionMode(ORT_SEQUENTIAL);
            sessionOptions.DisableMemPattern();
            sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            // Append the DirectML EP (device 0). C API, no C++ wrapper.
            Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(sessionOptions, 0));

            const std::wstring wpath = std::filesystem::path(modelPath).wstring();
            session                  = std::make_unique<Ort::Session>(env, wpath.c_str(), sessionOptions);

            const Ort::AllocatorWithDefaultOptions alloc;
            inputName     = session->GetInputNameAllocated(0, alloc).get();
            outputName    = session->GetOutputNameAllocated(0, alloc).get();
            inputNamePtr  = inputName.c_str();
            outputNamePtr = outputName.c_str();

            memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            inputBuf.assign(static_cast<size_t>(kWindow), 0.0f);
            outputBuf.assign(static_cast<size_t>(kClasses), 0.0f);
            inputTensor  = Ort::Value::CreateTensor<float>(memInfo, inputBuf.data(), inputBuf.size(), inputShape.data(),
                                                           inputShape.size());
            outputTensor = Ort::Value::CreateTensor<float>(memInfo, outputBuf.data(), outputBuf.size(),
                                                           outputShape.data(), outputShape.size());

            // Warm up with one synchronous run so the first real kick isn't slow;
            // compiles kernels and grows the arena off the hot path.
            session->Run(runOptions, &inputNamePtr, &inputTensor, 1, &outputNamePtr, &outputTensor, 1);

            ready = true;
            return true;
        }
        catch (const Ort::Exception&)
        {
            hardFailed.store(true);
            ready = false;
            return false;
        }
    }

    [[nodiscard]] bool isReady() const { return ready && !hardFailed.load(); }
    [[nodiscard]] bool inFlight() const { return running.load(); }
    [[nodiscard]] bool failed() const { return hardFailed.load(); }

    // Async inference latency (kick -> onComplete, ms) and completed-inference
    // count (real pipeline timing).
    [[nodiscard]] double lastInferenceMs() const { return lastLatencyMs.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t inferenceCount() const { return inferenceCounter.load(std::memory_order_relaxed); }

    // Start an async inference over n samples (expects modelWindowSamples; extra
    // ignored, short zero-padded). No-op if not ready or already in flight.
    void kick(const float* window, const int n)
    {
        if (!ready || hardFailed.load() || running.load())
        {
            return;
        }

        const int count = std::min(n, kWindow);
        std::copy_n(window, count, inputBuf.begin());
        if (count < kWindow)
        {
            std::fill(inputBuf.begin() + count, inputBuf.end(), 0.0f);
        }

        resultReady.store(false);
        running.store(true);
        kickTimeNs.store(nowNs(), std::memory_order_relaxed);
        try
        {
            session->RunAsync(runOptions, &inputNamePtr, &inputTensor, 1, &outputNamePtr, &outputTensor, 1, onComplete,
                              this);
        }
        catch (const Ort::Exception&)
        {
            running.store(false);
            hardFailed.store(true);
        }
    }

    // If a fresh inference completed, copy its per-class scores (baji, ru, env,
    // instrumental, singing) into out5 and return true, consuming the result;
    // otherwise false. out5 must hold modelNumClasses floats.
    bool tryHarvest(float* out5)
    {
        bool expected = true;
        if (resultReady.compare_exchange_strong(expected, false))
        {
            for (int i = 0; i < kClasses; ++i)
            {
                out5[i] = classScore[i].load();
            }
            return true;
        }
        return false;
    }

    // Benchmark only (headless host, not real-time): run `iterations` synchronous
    // inferences over `window` and return each run's latency in ms. Synchronous
    // (session->Run, not RunAsync) to time raw GPU compute. prepare() must have
    // succeeded; returns empty on error.
    std::vector<double> benchmark(const float* window, const int n, const int iterations)
    {
        std::vector<double> latenciesMs;
        if (!ready || hardFailed.load())
        {
            return latenciesMs;
        }

        const int count = std::min(n, kWindow);
        std::copy_n(window, count, inputBuf.begin());
        if (count < kWindow)
        {
            std::fill(inputBuf.begin() + count, inputBuf.end(), 0.0f);
        }

        latenciesMs.reserve(static_cast<size_t>(iterations));
        try
        {
            for (int i = 0; i < iterations; ++i)
            {
                const auto s = std::chrono::high_resolution_clock::now();
                session->Run(runOptions, &inputNamePtr, &inputTensor, 1, &outputNamePtr, &outputTensor, 1);
                const auto e = std::chrono::high_resolution_clock::now();
                latenciesMs.push_back(std::chrono::duration<double, std::milli>(e - s).count());
            }
        }
        catch (const Ort::Exception&)
        {
            hardFailed.store(true);
        }
        return latenciesMs;
    }

private:
    static constexpr int kWindow  = linkjiru::modelWindowSamples; // 24000
    static constexpr int kClasses = linkjiru::modelNumClasses;    // 5

    // Runs on an ORT worker thread when an async inference finishes. Publishes
    // the result through atomics: the one cross-thread hand-off.
    static void onComplete(void* user, OrtValue** /*outputs*/, size_t /*numOutputs*/, const OrtStatusPtr status)
    {
        auto* self = static_cast<OnnxRunner*>(user);

        if (status != nullptr)
        {
            Ort::GetApi().ReleaseStatus(status);
            self->hardFailed.store(true);
            self->running.store(false);
            return;
        }

        // outputBuf holds the 5 per-class sigmoids; publish them and flag ready.
        // The detect decision (max(baji, ru) -> hysteresis) lives in the loop.
        for (int i = 0; i < kClasses; ++i)
        {
            self->classScore[i].store(self->outputBuf[static_cast<size_t>(i)]);
        }

        const double latencyMs =
            static_cast<double>(nowNs() - self->kickTimeNs.load(std::memory_order_relaxed)) / 1.0e6;
        self->lastLatencyMs.store(latencyMs, std::memory_order_relaxed);
        self->inferenceCounter.fetch_add(1, std::memory_order_relaxed);
        self->resultReady.store(true);
        self->running.store(false);
    }

    static int64_t nowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::string modelPath;

    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "linkjiru"};
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    Ort::RunOptions runOptions;
    Ort::MemoryInfo memInfo{nullptr};

    std::string inputName;
    std::string outputName;
    const char* inputNamePtr  = nullptr;
    const char* outputNamePtr = nullptr;

    // Reused across inferences; no per-run allocation.
    std::vector<float> inputBuf;  // kWindow, written by kick, read by ORT
    std::vector<float> outputBuf; // kClasses, written by ORT
    Ort::Value inputTensor{nullptr};
    Ort::Value outputTensor{nullptr};
    std::array<int64_t, 1> inputShape{{kWindow}};
    std::array<int64_t, 1> outputShape{{kClasses}};

    bool ready = false;                        // analysis-thread only
    std::atomic<bool> running{false};          // an async run is in flight
    std::atomic<bool> resultReady{false};      // fresh result waiting to harvest
    std::atomic<bool> hardFailed{false};       // device lost / ORT error (sticky)
    std::atomic<float> classScore[kClasses]{}; // latest per-class scores

    std::atomic<int64_t> kickTimeNs{0};        // kick timestamp, for async latency
    std::atomic<double> lastLatencyMs{0.0};    // last kick -> onComplete latency (ms)
    std::atomic<uint64_t> inferenceCounter{0}; // completed async inferences
};
