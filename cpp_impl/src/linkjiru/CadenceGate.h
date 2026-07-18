#pragma once

#include <cstdint>

// Backlog policy: how to reschedule when the reader falls behind by more than one
// hop. Templated for zero cost at the poll site; only SkipToLatest ships.
struct SkipToLatest
{
    // Schedule one hop past the head: serve the newest window, not a backlog.
    static uint64_t reschedule(const uint64_t writeCount, uint64_t /*prevNextAt*/, const int hop)
    {
        return writeCount + static_cast<uint64_t>(hop);
    }
};

// Paces evaluations against the ring's write count: warm-up, hop pacing, backlog.
// Decision-only: never reads samples; the caller owns the snapshot.
//
// windowSamples: samples per evaluation. hopSamples: min new samples between
// evaluations (0 = every tick). requireFullWindow: withhold until windowSamples
// real samples exist (never evaluate a zero-padded window).
template <class BacklogPolicy = SkipToLatest> class CadenceGate
{
public:
    CadenceGate(const int windowSamples, const int hopSamples, const bool requireFullWindow)
        : windowSamples(windowSamples), hopSamples(hopSamples), requireFullWindow(requireFullWindow)
    {
    }

    struct Decision
    {
        bool evaluate = false; // run the model this tick
        bool skipped  = false; // fell behind; intermediate hops were dropped
    };

    // Decide whether to evaluate now. Advances state only when evaluate = true.
    Decision poll(const uint64_t writeCount)
    {
        // Warm-up: never evaluate a zero-padded window.
        if (requireFullWindow && writeCount < static_cast<uint64_t>(windowSamples))
        {
            return {};
        }

        // Continuous (hop 0) evaluates every tick.
        if (hopSamples <= 0)
        {
            return {true, false};
        }

        // Not enough new samples since the last evaluation.
        if (writeCount < nextAt)
        {
            return {};
        }

        // Behind by a full hop or more => at least one window was dropped.
        const bool skipped = started && writeCount >= nextAt + static_cast<uint64_t>(hopSamples);

        nextAt  = BacklogPolicy::reschedule(writeCount, nextAt, hopSamples);
        started = true;
        return {true, skipped};
    }

private:
    int windowSamples;
    int hopSamples;
    bool requireFullWindow;
    uint64_t nextAt = 0;     // writeCount the next evaluation is due at
    bool started    = false; // first evaluation done (so it's never "skipped")
};
