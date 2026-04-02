#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <mutex>
/*
 * All public methods must be called from the SAME thread (the AnalysisThread).
 * The internal mutex serializes connect/disconnect within that thread.
 * Calling from multiple threads is NOT safe — Boost.Beast's io_context is not
 * thread-safe, and the mutex only protects the high-level call boundary.
 */
class VTubeStudioClient
{
public:
    VTubeStudioClient();
    ~VTubeStudioClient();

    VTubeStudioClient(const VTubeStudioClient&) = delete;
    VTubeStudioClient& operator=(const VTubeStudioClient&) = delete;

    bool connect(const std::string& host = "localhost",
                 const std::string& port = "8001");

    void disconnect();
    bool isConnected() const { return connected.load(); }

    // Full VTS auth flow: try stored token, fall back to popup approval
    bool authenticate();

    bool registerParameter(const std::string& paramId,
                           const std::string& explanation,
                           float minValue = 0.0f,
                           float maxValue = 1.0f,
                           float defaultValue = 0.0f);

    bool injectParameter(const std::string& paramId, float value);

    bool triggerHotkey(const std::string& hotkeyId);

private:
    struct WebSocketState;
    std::unique_ptr<WebSocketState> wsState;
    std::mutex sendMutex;
    std::atomic<bool> connected{false};

    static constexpr const char* PLUGIN_NAME    = "Linkjiru";
    static constexpr const char* DEVELOPER_NAME = "tomobaji";
    static constexpr int OP_TIMEOUT_MS          = 2000;
    static constexpr int AUTH_POPUP_TIMEOUT_MS  = 60000;

    static std::string getTokenFilePath();
    static bool loadToken(std::string& token);
    static bool saveToken(const std::string& token);
};
