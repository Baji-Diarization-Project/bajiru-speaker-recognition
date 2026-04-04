#include <gtest/gtest.h>
#include "SharedRingBuffer.h"
#include "RmsAnalyzer.h"
#include "bench_csv.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

static constexpr int CAPACITY = 131072;
static constexpr int WINDOW = 2048;

static constexpr double MAX_INPUT_LATENCY_US = 20'000.0;
static constexpr double MAX_COMPUTE_LATENCY_US = 100.0;
static constexpr double MAX_OUTPUT_LATENCY_US = 10.0;
static constexpr double MAX_FULL_CHAIN_US = 25'000.0;

static const std::string CSV_PATH = "bench_results.csv";

using Clock = std::chrono::high_resolution_clock;

static std::vector<float> generateSilence(const int count)
{
    return std::vector<float>(count, 0.001f);
}

static std::vector<float> generateSpeech(const int count)
{
    std::vector<float> samples(count);
    for (int i = 0; i < count; ++i)
        samples[i] = 0.5f * std::sin(static_cast<float>(i) * 0.3f);
    return samples;
}

class ChainLatencyBench : public ::testing::Test
{
protected:
    SharedRingBuffer<CAPACITY> buffer;
    std::unordered_map<std::string, BenchBaseline> baselines;

    void SetUp() override
    {
        baselines = loadBaselines(CSV_PATH);
    }
};

TEST_F(ChainLatencyBench, ComputeLatency)
{
    RmsAnalyzer::Config cfg;
    cfg.calibrationSamples = 2048;
    RmsAnalyzer analyzer(cfg);

    const auto silence = generateSilence(2048);
    analyzer.calibrate(silence.data(), 2048);
    ASSERT_TRUE(analyzer.isCalibrated());

    const auto speech = generateSpeech(WINDOW);
    std::vector<double> timings;

    for (int iter = 0; iter < 10000; ++iter)
    {
        auto start = Clock::now();
        volatile bool result = analyzer.isSpeechActive(speech.data(), WINDOW);
        auto end = Clock::now();
        (void)result;

        timings.push_back(
            std::chrono::duration<double, std::micro>(end - start).count());
    }

    const double m = vecMean(timings);
    const double s = vecStddev(timings, m);
    checkBenchmark(CSV_PATH, baselines, "ComputeLatency_RMS_2048", m, s,
                   MAX_COMPUTE_LATENCY_US);
}

TEST_F(ChainLatencyBench, OutputLatency)
{
    std::atomic<float> detectValue{0.0f};
    bool currentlySpeaking = false;
    int64_t lastSpeechTime = 0;
    static constexpr int sustainMs = 100;
    std::vector<double> timings;

    for (int iter = 0; iter < 10000; ++iter)
    {
        const bool speechDetected = (iter % 2 == 0);
        const auto now = static_cast<int64_t>(iter * 16);

        auto start = Clock::now();

        if (speechDetected)
        {
            lastSpeechTime = now;
            if (!currentlySpeaking)
                currentlySpeaking = true;
        }
        else if (currentlySpeaking && (now - lastSpeechTime > sustainMs))
        {
            currentlySpeaking = false;
        }

        detectValue.store(currentlySpeaking ? 1.0f : 0.0f);

        auto end = Clock::now();

        timings.push_back(
            std::chrono::duration<double, std::micro>(end - start).count());
    }

    const double m = vecMean(timings);
    const double s = vecStddev(timings, m);
    checkBenchmark(CSV_PATH, baselines, "OutputLatency_StateMachine", m, s,
                   MAX_OUTPUT_LATENCY_US);
}

TEST_F(ChainLatencyBench, InputLatency)
{
    /* Single-threaded write → readLastN round-trip.
       Just the raw transport cost. Real-world
       adds one poll interval (~16ms) but that's a known constant, unlike everything
       else in concurrent C++. */
    const auto silence = generateSilence(CAPACITY / 2);
    buffer.write(silence.data(), CAPACITY / 2);

    const auto speech = generateSpeech(512);
    std::vector<float> dest(WINDOW);
    std::vector<double> timings;

    for (int iter = 0; iter < 1000; ++iter)
    {
        auto start = Clock::now();
        buffer.write(speech.data(), 512);
        buffer.readLastN(dest.data(), WINDOW);
        auto end = Clock::now();

        timings.push_back(
            std::chrono::duration<double, std::micro>(end - start).count());
    }

    const double m = vecMean(timings);
    const double s = vecStddev(timings, m);
    checkBenchmark(CSV_PATH, baselines, "InputLatency_Write_to_Read",
                   m, s, MAX_INPUT_LATENCY_US);
}

TEST_F(ChainLatencyBench, FullChainLatency)
{
    RmsAnalyzer::Config cfg;
    cfg.calibrationSamples = 2048;
    RmsAnalyzer analyzer(cfg);

    const auto silence = generateSilence(2048);
    buffer.write(silence.data(), 2048);

    std::vector<float> windowBuf(WINDOW);
    buffer.readLastN(windowBuf.data(), WINDOW);
    analyzer.calibrate(windowBuf.data(), WINDOW);
    ASSERT_TRUE(analyzer.isCalibrated());

    const auto speech = generateSpeech(512);
    std::atomic<float> detectValue{0.0f};
    bool currentlySpeaking = false;
    int64_t lastSpeechTime = 0;
    static constexpr int sustainMs = 100;

    std::vector<double> timings;

    for (int iter = 0; iter < 100; ++iter)
    {
        auto start = Clock::now();

        buffer.write(speech.data(), 512);
        buffer.readLastN(windowBuf.data(), WINDOW);
        const bool speechDetected = analyzer.isSpeechActive(windowBuf.data(), WINDOW);

        const auto now = static_cast<int64_t>(iter * 16);
        if (speechDetected)
        {
            lastSpeechTime = now;
            if (!currentlySpeaking)
                currentlySpeaking = true;
        }
        else if (currentlySpeaking && (now - lastSpeechTime > sustainMs))
        {
            currentlySpeaking = false;
        }

        detectValue.store(currentlySpeaking ? 1.0f : 0.0f);

        auto end = Clock::now();

        timings.push_back(
            std::chrono::duration<double, std::micro>(end - start).count());
    }

    const double m = vecMean(timings);
    const double s = vecStddev(timings, m);
    checkBenchmark(CSV_PATH, baselines, "FullChainLatency", m, s,
                   MAX_FULL_CHAIN_US);
}
