#pragma once

#include "PolyphaseFir.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

// Host-rate -> target-rate resampler for the analysis capture path. Mono and
// real-time safe: process() never allocates, locks, or does unbounded work.
// Stateful: one instance per stream; prepare() rebuilds it on a rate change.
//
// A cascade of polyphase FIR stages (PolyphaseFir). Heavy downsampling is split
// into stages: while the rate would drop by more than half, a 2:1 anti-alias
// decimation stage is prepended; the remainder (a <=2x change, up or down) is one
// final rational stage. So 96->48 is a single stage, 192->48 is two, and any
// upsample (44.1->48) is one. Multistage keeps each filter's transition band
// modest, so the total work stays small while the stopband stays deep.
//
// Owns a stable output buffer (preallocated to a hard cap) and exposes
// storage()/byteSize() for page-locking.
class Resampler
{
public:
    struct Block
    {
        const float* data = nullptr;
        int numSamples    = 0;
    };

    // targetSampleRate: output rate (e.g. 48000). maxInputSamples: largest input
    // block process() will get. Output buffer is 2x that: enough headroom for any
    // host rate down to targetSampleRate / 2 (an upsample-by-2).
    Resampler(const int targetSampleRate, const int maxInputSamples)
        : target(targetSampleRate), maxInput(maxInputSamples)
    {
        finalOut.resize(static_cast<std::size_t>(maxInputSamples) * 2);
    }

    // Configure for a host sample rate. Call when NOT processing (e.g. from
    // prepareToPlay); it rebuilds and resets the whole cascade.
    void prepare(const double hostSampleRate)
    {
        stages.clear();
        bypass = (hostSampleRate == static_cast<double>(target));
        if (bypass)
        {
            return;
        }

        int inRate   = static_cast<int>(std::lround(hostSampleRate));
        int stageMax = maxInput;

        // Staged 2:1 decimation while the rate would drop by more than half.
        while (inRate > 2 * target)
        {
            stages.emplace_back();
            stages.back().design(1, 2, kStageTaps, stageMax);
            inRate /= 2;
            stageMax = stageMax / 2 + 2;
        }

        // Final rational stage for the remaining <=2x change (up or down).
        const int g = std::gcd(target, inRate);
        stages.emplace_back();
        stages.back().design(target / g, inRate / g, kStageTaps, stageMax);
    }

    // Resample n host-rate mono samples; returns a view valid until the next call.
    // At the target rate it's a zero-copy passthrough (returns the input view).
    Block process(const float* input, const int n)
    {
        if (bypass)
        {
            return {input, n};
        }

        const float* cur = input;
        int count        = n;
        for (auto& stage : stages)
        {
            const auto out = stage.process(cur, count);
            cur            = out.data;
            count          = out.numSamples;
        }

        // Copy the cascade's result into our stable (page-locked) buffer so the
        // returned pointer never depends on which stage ran last.
        count = std::min(count, static_cast<int>(finalOut.size()));
        std::copy_n(cur, count, finalOut.data());
        return {finalOut.data(), count};
    }

    // Output-buffer storage for page-locking; not a sample accessor.
    void* storage() noexcept { return finalOut.data(); }
    [[nodiscard]] std::size_t byteSize() const noexcept { return finalOut.size() * sizeof(float); }

    // True if the FIR stages are using the AVX dot (CPU-dependent).
    [[nodiscard]] bool usesSimd() const noexcept { return !stages.empty() && stages.front().usesSimd(); }

private:
    // Per-stage polyphase length. 48 taps/phase with a beta-9 Kaiser gives a deep
    // (~80 dB) stopband; the 2:1 stages (L=1) get all 48 taps, upsampling stages
    // spread them across phases.
    static constexpr int kStageTaps = 48;

    std::vector<PolyphaseFir> stages;
    std::vector<float> finalOut;
    int target;
    int maxInput;
    bool bypass = true;
};
