#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Silence gate for the manager: per tick, decides whether to run the model and
// whether to push to VTS. On silence, neither: holding the last result keeps
// silence from flipping detection. Adaptive (tracked noise floor). Pure: RMS in,
// bools out.
class GatingLogic
{
public:
    struct Config
    {
        float wakeRatio    = 3.0f;    // enter "sound" when RMS > noiseFloor * this
        float silenceRatio = 1.6f;    // enter "silence" when RMS < noiseFloor * this
        float floorRise    = 0.0015f; // how fast the noise floor rises toward louder RMS
        float minFloor     = 1.0e-5f; // floor clamp (avoids odd behaviour on digital silence)

        int holdMs = 300; // keep evaluating this long after sound stops

        int sendKeepaliveMs = 500; // resend the current value to VTS at least this often
    };

    GatingLogic() = default;
    explicit GatingLogic(const Config& c) : config(c) {}

    // RMS of a mono window.
    static float rms(const float* data, const int n)
    {
        if (n <= 0)
        {
            return 0.0f;
        }
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i)
        {
            sumSq += static_cast<double>(data[i]) * data[i];
        }
        return static_cast<float>(std::sqrt(sumSq / n));
    }

    // Run the model this tick? True while there's sound and for holdMs after it stops.
    bool shouldEvaluate(const float windowRms, const int64_t nowMs)
    {
        // Track the noise floor: drop instantly to quieter, rise slowly.
        if (windowRms < noiseFloor)
        {
            noiseFloor = windowRms;
        }
        else
        {
            noiseFloor += (windowRms - noiseFloor) * config.floorRise;
        }

        const float floor = std::max(noiseFloor, config.minFloor);

        if (awake)
        {
            if (windowRms < floor * config.silenceRatio)
            {
                awake = false;
            }
            else
            {
                lastLoudMs = nowMs;
            }
        }
        else if (windowRms > floor * config.wakeRatio)
        {
            awake      = true;
            lastLoudMs = nowMs;
        }

        return awake || (nowMs - lastLoudMs) < config.holdMs;
    }

    // Send this value to VTS now? On a change, or as a periodic keepalive (which
    // also covers a dropped fire-and-forget send).
    bool shouldSend(const float detectValue, const int64_t nowMs)
    {
        if (detectValue != lastSent || (nowMs - lastSentMs) >= config.sendKeepaliveMs)
        {
            lastSent   = detectValue;
            lastSentMs = nowMs;
            return true;
        }
        return false;
    }

private:
    Config config;
    float noiseFloor   = 1.0e-4f;  // adaptive input noise floor
    bool awake         = false;    // hysteretic "sound present" state
    int64_t lastLoudMs = 0;        // last tick with sound
    float lastSent     = -1.0f;    // != any valid detect -> first send goes
    int64_t lastSentMs = -1000000; // force an early keepalive send
};

// One-pole low-pass (EMA) over the per-class scores, applied once per inference.
// Stops brief spikes from flipping the speaker (baji <-> ru) or the detect flag.
// Lower alpha = smoother, more lag.
class ScoreLowPass
{
public:
    struct Config
    {
        float alpha = 0.2f; // EMA coefficient; ~5-frame time constant at 60 Hz
    };

    ScoreLowPass() = default;
    explicit ScoreLowPass(const Config& c) : config(c) {}

    void update(const float* raw, const int n)
    {
        const int m = std::min(n, kMax);
        for (int i = 0; i < m; ++i)
        {
            smoothed[i] += config.alpha * (raw[i] - smoothed[i]);
        }
    }

    float operator[](const int i) const { return (i >= 0 && i < kMax) ? smoothed[i] : 0.0f; }

private:
    static constexpr int kMax = 8;
    Config config;
    float smoothed[kMax] = {};
};

// Schmitt trigger: output turns ON at `high`, OFF at `low`, holds between;
// keeps the signal from flipping while the input hovers near one threshold.
class Hysteresis
{
public:
    struct Config
    {
        float high = 0.8f;
        float low  = 0.2f;
    };

    Hysteresis() = default;
    explicit Hysteresis(const Config& c) : config(c) {}

    // Feed the current confidence; returns the (possibly held) state.
    bool update(const float confidence)
    {
        if (!state && confidence >= config.high)
        {
            state = true;
        }
        else if (state && confidence <= config.low)
        {
            state = false;
        }
        return state;
    }

    [[nodiscard]] bool value() const { return state; }
    void reset() { state = false; }

private:
    Config config;
    bool state = false;
};
