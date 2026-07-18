#pragma once

#include "Constants.h"
#include <cstdint>

// A coherent snapshot of pipeline state for the UI / test host. The bools are
// packed by the analysis thread into one atomic word and read back in a single
// load, so the UI never sees an inconsistent mix (e.g. connected flipping
// between separate reads).
struct PipelineStatus
{
    bool running                                 = false; // analysis pipeline active
    bool modelReady                              = false; // ONNX session built + warmed
    bool inferenceFailed                         = false; // device lost / ORT error (sticky)
    bool vtsConnected                            = false; // VTS socket up + authenticated
    bool vtsRegistered                           = false; // detect parameter created in VTS
    bool vtsRegisterFailed                       = false; // last register attempt failed
    float detectValue                            = 0.0f;  // VTS signal: 0 = baji, 1 = ru (latched)
    float rawScore                               = 0.0f;  // latest smoothed confidence (max of baji/ru)
    uint64_t droppedWindows                      = 0;     // inferences dropped when the GPU couldn't keep pace
    float classScores[linkjiru::modelNumClasses] = {};    // baji, ru, env, instrumental, singing
    double lastInferenceMs                       = 0.0;   // last async inference latency (kick -> onComplete)
    uint64_t inferenceCount                      = 0;     // completed async inferences

    // Latched speaker: -1 before any detection, then 0 = baji / 1 = ru, held
    // across silences. detectValue says whether they're currently talking.
    int speaker = -1;
};

namespace linkjiru
{

// Bit layout for the packed status word (running is the processor's own flag;
// detectValue is a separate atomic). Base type matches the 32-bit word these OR
// into, not the value range.
// NOLINTNEXTLINE(performance-enum-size)
enum StatusBit : uint32_t
{
    statusModelReady        = 1u << 0,
    statusInferenceFailed   = 1u << 1,
    statusVtsConnected      = 1u << 2,
    statusVtsRegistered     = 1u << 3,
    statusVtsRegisterFailed = 1u << 4,
};

} // namespace linkjiru
