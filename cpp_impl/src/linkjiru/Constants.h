#pragma once

// Constants shared across translation units. File-local values stay in their own files.

namespace linkjiru
{

// ── Audio pipeline ──

// Pipeline rate. The audio thread resamples host-rate -> 48 kHz before writing,
// so every count below is in 48 kHz samples.
inline constexpr int modelSampleRate = 48000;

// Samples per inference: 24000 = 0.5 s of context.
inline constexpr int modelWindowSamples = 24000;

// Model's native window step: 800 samples = 16.67 ms = 60 Hz. Inference may run
// at any multiple of this.
inline constexpr int modelHopSamples = 800;

// Per-class scores: 0 baji, 1 ru, 2 env, 3 instrumental, 4 singing.
inline constexpr int modelNumClasses = 5;

// Detect hysteresis on smoothed max(baji, ru): ON above high, OFF below low, hold
// between; stops a hovering score from flipping the output.
inline constexpr float detectHigh = 0.8f;
inline constexpr float detectLow  = 0.2f;

// Ring buffer size: total history retained, not the model window. Power of two
// (bitmask wrap), kept well above the window so a reader isn't lapped
// mid-snapshot: 131072 ≈ 2.73 s.
inline constexpr int ringBufferCapacity = 131072;

static_assert((ringBufferCapacity & (ringBufferCapacity - 1)) == 0, "ringBufferCapacity must be a power of two");
static_assert(ringBufferCapacity >= 2 * modelWindowSamples,
              "ringBufferCapacity must leave margin above the model window");

// Hard cap on a callback block (host-rate samples). The mono mix buffer is sized
// to this once and never resized; larger blocks are truncated. ~1.4 s at 48 kHz.
inline constexpr int maxAudioBlockSamples = 65536;

// ── VTube Studio ──

// Parameter we drive in VTS; changing it means re-registering there.
inline constexpr const char* detectParamName = "LinkjiruDetectLowji";

// VTS listens on localhost:8001 by default.
inline constexpr const char* defaultVtsHost = "localhost";
inline constexpr const char* defaultVtsPort = "8001";

// Envelope fields on every VTS request/response.
inline constexpr const char* vtsApiName    = "VTubeStudioPublicAPI";
inline constexpr const char* vtsApiVersion = "1.0";

// ── Plugin identity ──

// VTS auth, editor title, token folder under %APPDATA%. Keep in sync with CMake.
inline constexpr const char* pluginName    = "Linkjiru";
inline constexpr const char* developerName = "tomobaji";

// ── Threading ──

// stopThread() timeout; the analysis loop normally exits within one poll.
inline constexpr int threadStopTimeoutMs = 3000;

} // namespace linkjiru
