#pragma once

#include "Constants.h"
#include <memory>
#include <string>

// Asynchronous VTube Studio WebSocket client. Every network op is async and
// serviced by pump(), which never blocks: the analysis loop calls pump() once
// per tick and reads the status getters, never waiting on the socket.
//
// Connection policy: connect() begins an async connect + authenticate. While
// establishing the initial link it retries with its own backoff (VTS may not be
// up yet). Once Ready, if the link drops it stays Disconnected until connect()
// is called again. A deliberate, user-triggered reconnect.
//
// Single-threaded: every method must be called from the same thread (the
// AnalysisThread). No internal locking; one owner drives the state machine.
class VTubeStudioClient
{
public:
    VTubeStudioClient();
    ~VTubeStudioClient();

    VTubeStudioClient(const VTubeStudioClient&)            = delete;
    VTubeStudioClient& operator=(const VTubeStudioClient&) = delete;

    // Service the io_context and advance the state machine. Non-blocking; call
    // once per analysis-loop tick.
    void pump() const;

    // Begin (or restart) an async connect + authenticate. Idempotent while
    // already connecting/connected.
    void connect(const std::string& host = linkjiru::defaultVtsHost,
                 const std::string& port = linkjiru::defaultVtsPort) const;

    // Async close + teardown. Safe to call any time.
    void disconnect() const;

    [[nodiscard]] bool isConnected() const;      // Ready: socket up + authenticated
    [[nodiscard]] bool isRegistered() const;     // detect parameter created in VTS
    [[nodiscard]] bool isRegisterFailed() const; // last register attempt failed

    // Ask VTS to create the detect parameter; pump() performs it once Ready.
    void requestRegister() const;

    // Latest-value cell for the detect parameter. pump() sends the most recent
    // value fire-and-forget; intermediate values coalesce.
    void setDetectValue(float value) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
