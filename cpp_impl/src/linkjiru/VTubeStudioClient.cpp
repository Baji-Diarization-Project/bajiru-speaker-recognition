#ifndef _WIN32
#error "VTubeStudioClient requires Windows (MSVC). DPAPI and Winsock are not available on other platforms."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VTubeStudioClient.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <vector>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
using tcp           = net::ip::tcp;
using json          = nlohmann::json;
using clock_type    = std::chrono::steady_clock;

namespace
{

// Max inbound WebSocket message. 256 KB covers the largest VTS responses.
constexpr std::size_t maxWsMessageSize = std::size_t{256} * 1024;

// Temp-file suffix for atomic write-then-rename in saveToken().
constexpr const char* tokenTmpSuffix = ".tmp";

// Per-op timeout for connect/handshake/write/read (ms). Longer than this on
// localhost means VTS isn't responding.
constexpr int OP_TIMEOUT_MS = 3000;

// Auth-token read timeout (ms): long, since the user must click "Allow" in the
// VTS popup. The read is async, so the analysis loop isn't blocked; this only
// bounds how long we sit in Authenticating before retrying.
constexpr int AUTH_POPUP_TIMEOUT_MS = 60000;

// Backoff between initial connection attempts (ms). Avoids hammering the socket
// while VTS isn't running yet.
constexpr int RECONNECT_BACKOFF_MS = 5000;

json makeEnvelope(const std::string& messageType, const json& data, const std::string& requestID = "linkjiru-req")
{
    return {{"apiName", linkjiru::vtsApiName},
            {"apiVersion", linkjiru::vtsApiVersion},
            {"requestID", requestID},
            {"messageType", messageType},
            {"data", data}};
}

// ── Token persistence (DPAPI encrypted) ─────────────────────────────────────

std::string getTokenFilePath()
{
    wchar_t* widePath = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &widePath) != S_OK)
    {
        return {};
    }

    const int len = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1)
    {
        CoTaskMemFree(widePath);
        return {};
    }

    std::string result(static_cast<size_t>(len - 1), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, result.data(), len, nullptr, nullptr);
    CoTaskMemFree(widePath);

    if (written == 0)
    {
        return {};
    }

    return result + "\\" + linkjiru::pluginName + "\\vts_token.dat";
}

bool loadToken(std::string& token)
{
    const std::string path = getTokenFilePath();
    if (path.empty())
    {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    std::string encrypted((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (encrypted.empty())
    {
        return false;
    }

    DATA_BLOB encryptedBlob;
    encryptedBlob.pbData = reinterpret_cast<BYTE*>(encrypted.data());
    encryptedBlob.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB decryptedBlob;
    if (!CryptUnprotectData(&encryptedBlob, nullptr, nullptr, nullptr, nullptr, 0, &decryptedBlob))
    {
        std::filesystem::remove(path);
        return false;
    }

    token.assign(reinterpret_cast<char*>(decryptedBlob.pbData), decryptedBlob.cbData);
    LocalFree(decryptedBlob.pbData);

    return !token.empty();
}

bool saveToken(const std::string& token)
{
    const std::string path = getTokenFilePath();
    if (path.empty())
    {
        return false;
    }

    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    std::vector<BYTE> plainCopy(token.begin(), token.end());
    DATA_BLOB plainBlob;
    plainBlob.pbData = plainCopy.data();
    plainBlob.cbData = static_cast<DWORD>(plainCopy.size());

    DATA_BLOB encryptedBlob;
    if (!CryptProtectData(&plainBlob, nullptr, nullptr, nullptr, nullptr, 0, &encryptedBlob))
    {
        return false;
    }

    const std::string tmpPath = path + tokenTmpSuffix;
    {
        std::ofstream file(tmpPath, std::ios::binary);
        if (!file.is_open())
        {
            LocalFree(encryptedBlob.pbData);
            return false;
        }

        file.write(reinterpret_cast<char*>(encryptedBlob.pbData), static_cast<std::streamsize>(encryptedBlob.cbData));
        LocalFree(encryptedBlob.pbData);

        if (!file.good())
        {
            std::filesystem::remove(tmpPath);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmpPath);
        return false;
    }

    return true;
}

} // namespace

// ── Impl: the async state machine ───────────────────────────────────────────

struct VTubeStudioClient::Impl
{
    enum class State : std::uint8_t
    {
        Disconnected,
        Resolving,
        Connecting,
        Handshaking,
        Authenticating,
        Ready
    };

    net::io_context ioc;
    tcp::resolver resolver{ioc};
    std::unique_ptr<websocket::stream<beast::tcp_stream>> ws;
    beast::flat_buffer readBuf;

    State state = State::Disconnected;
    std::string host;
    std::string port;

    // Drives auto-retry of the initial link: set by connect(), cleared once we've
    // been Ready and then dropped, so a post-connection drop needs an explicit
    // connect() to resume.
    bool wantConnection                = false;
    clock_type::time_point nextAttempt = clock_type::now();

    // Auth flow bookkeeping.
    std::string token;
    bool triedStoredToken = false;

    // Register.
    bool registerRequested = false;
    bool registering       = false;
    bool registered        = false;
    bool registerFailed    = false;

    // Inject (latest-value, fire-and-forget).
    float pendingValue = 0.0f;
    bool valueDirty    = false;

    // One write in flight at a time (inject OR register).
    bool writeInFlight = false;
    std::string writePayload; // kept alive for the duration of async_write

    // Teardown guard: handlers must not re-arm once this is set.
    bool shuttingDown = false;

    // Set by a handler that must drop the connection. The stream isn't destroyed
    // inside the handler (other ops may be pending); pump() recycles it after poll().
    bool needsRecycle = false;

    void setTimeout(const int ms) const { beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(ms)); }

    // Drop the connection. afterReady=true (we'd reached Ready) leaves
    // wantConnection false so the user must re-trigger connect(); afterReady=false
    // (initial-connect failure) keeps retrying with backoff. Never touches ws
    // directly (see needsRecycle). Idempotent: first drop wins, later aborted
    // handlers see Disconnected and no-op.
    void dropConnection(const bool afterReady)
    {
        if (state == State::Disconnected)
        {
            return;
        }
        state         = State::Disconnected;
        registering   = false;
        registered    = false;
        writeInFlight = false;
        needsRecycle  = true;
        if (afterReady)
        {
            wantConnection = false;
        }
        nextAttempt = clock_type::now() + std::chrono::milliseconds(RECONNECT_BACKOFF_MS);
    }

    void fail() { dropConnection(true); }        // steady-state drop -> user reconnect
    void retryLater() { dropConnection(false); } // pre-Ready failure -> auto-retry

    void startConnect()
    {
        ws = std::make_unique<websocket::stream<beast::tcp_stream>>(ioc);
        readBuf.clear();
        triedStoredToken = false;
        state            = State::Resolving;

        resolver.async_resolve(host, port,
                               [this](const beast::error_code& ec, const tcp::resolver::results_type& results)
                               {
                                   if (shuttingDown)
                                   {
                                       return;
                                   }
                                   if (ec)
                                   {
                                       retryLater();
                                       return;
                                   }
                                   onResolve(results);
                               });
    }

    void onResolve(const tcp::resolver::results_type& results)
    {
        state = State::Connecting;
        setTimeout(OP_TIMEOUT_MS);
        beast::get_lowest_layer(*ws).async_connect(results,
                                                   [this](const beast::error_code& ec, const tcp::endpoint&)
                                                   {
                                                       if (shuttingDown)
                                                       {
                                                           return;
                                                       }
                                                       if (ec)
                                                       {
                                                           retryLater();
                                                           return;
                                                       }
                                                       onConnected();
                                                   });
    }

    void onConnected()
    {
        state = State::Handshaking;
        setTimeout(OP_TIMEOUT_MS);
        ws->async_handshake(host + ":" + port, "/",
                            [this](const beast::error_code& ec)
                            {
                                if (shuttingDown)
                                {
                                    return;
                                }
                                if (ec)
                                {
                                    retryLater();
                                    return;
                                }
                                onHandshake();
                            });
    }

    void onHandshake()
    {
        ws->read_message_max(maxWsMessageSize);
        state = State::Authenticating;
        beginAuth();
    }

    // ── Auth: try stored token, else request a new one via the popup ────────

    void beginAuth()
    {
        token.clear();
        if (loadToken(token) && !token.empty())
        {
            triedStoredToken = true;
            sendRequestThenRead(makeEnvelope("AuthenticationRequest",
                                             {{"pluginName", linkjiru::pluginName},
                                              {"pluginDeveloper", linkjiru::developerName},
                                              {"authenticationToken", token}},
                                             "auth-stored"),
                                OP_TIMEOUT_MS, [this](const json& resp) { onAuthResponse(resp); });
        }
        else
        {
            requestNewToken();
        }
    }

    void requestNewToken()
    {
        triedStoredToken = false;
        sendRequestThenRead(
            makeEnvelope("AuthenticationTokenRequest",
                         {{"pluginName", linkjiru::pluginName}, {"pluginDeveloper", linkjiru::developerName}},
                         "auth-token-req"),
            AUTH_POPUP_TIMEOUT_MS, [this](const json& resp) { onTokenResponse(resp); });
    }

    void onTokenResponse(const json& resp)
    {
        if (!resp.contains("data") || !resp["data"].contains("authenticationToken"))
        {
            retryLater();
            return;
        }

        token = resp["data"]["authenticationToken"].get<std::string>();
        saveToken(token);

        sendRequestThenRead(makeEnvelope("AuthenticationRequest",
                                         {{"pluginName", linkjiru::pluginName},
                                          {"pluginDeveloper", linkjiru::developerName},
                                          {"authenticationToken", token}},
                                         "auth-req"),
                            OP_TIMEOUT_MS, [this](const json& resp2) { onAuthResponse(resp2); });
    }

    void onAuthResponse(const json& resp)
    {
        const bool authed = resp.contains("data") && resp["data"].value("authenticated", false);
        if (authed)
        {
            state = State::Ready;
            startSteadyRead();
            return;
        }

        // Stored token rejected (revoked / different VTS): fall back to popup.
        if (triedStoredToken)
        {
            requestNewToken();
            return;
        }

        retryLater();
    }

    // One-shot request -> response, used during the auth handshake only. After
    // Ready we switch to the continuous steady-read loop.
    template <class OnResponse> void sendRequestThenRead(const json& request, int readTimeoutMs, OnResponse onResp)
    {
        writePayload = request.dump();
        setTimeout(OP_TIMEOUT_MS);
        ws->async_write(net::buffer(writePayload),
                        [this, readTimeoutMs, onResp](const beast::error_code& ec, std::size_t)
                        {
                            if (shuttingDown)
                            {
                                return;
                            }
                            if (ec)
                            {
                                retryLater();
                                return;
                            }
                            readBuf.clear();
                            setTimeout(readTimeoutMs);
                            ws->async_read(readBuf,
                                           [this, onResp](const beast::error_code& ec2, std::size_t)
                                           {
                                               if (shuttingDown)
                                               {
                                                   return;
                                               }
                                               if (ec2)
                                               {
                                                   retryLater();
                                                   return;
                                               }
                                               json resp;
                                               try
                                               {
                                                   resp = json::parse(beast::buffers_to_string(readBuf.data()));
                                               }
                                               catch (...)
                                               {
                                                   retryLater();
                                                   return;
                                               }
                                               onResp(resp);
                                           });
                        });
    }

    // ── Steady state: continuous read drain + fire-and-forget writes ────────

    // Arm one steady-state read. On error this is a post-Ready drop (fail() ->
    // await user reconnect); otherwise drain the message and re-arm.
    void armSteadyRead()
    {
        ws->async_read(readBuf,
                       [this](const beast::error_code& ec, std::size_t)
                       {
                           if (shuttingDown)
                           {
                               return;
                           }
                           if (ec)
                           {
                               fail();
                               return;
                           }
                           onSteadyMessage();
                       });
    }

    void startSteadyRead()
    {
        readBuf.clear();
        beast::get_lowest_layer(*ws).expires_never(); // VTS only sends in reply; write failures detect drops
        armSteadyRead();
    }

    void onSteadyMessage()
    {
        try
        {
            const auto resp     = json::parse(beast::buffers_to_string(readBuf.data()));
            const auto typeName = resp.value("messageType", "");
            if (typeName == "ParameterCreationResponse")
            {
                registered     = true;
                registering    = false;
                registerFailed = false;
            }
            else if (registering && typeName == "APIError")
            {
                registering    = false;
                registerFailed = true;
            }
            // InjectParameterDataResponse and everything else: drained, ignored.
        }
        catch (...)
        {
            // Malformed frame; ignore and keep reading.
        }

        readBuf.clear();
        if (!shuttingDown && state == State::Ready)
        {
            armSteadyRead();
        }
    }

    void startWriteIfNeeded()
    {
        if (writeInFlight || state != State::Ready)
        {
            return;
        }

        if (registerRequested && !registering)
        {
            registerRequested = false;
            registering       = true;
            registerFailed    = false;
            sendFireAndForget(makeEnvelope("ParameterCreationRequest",
                                           {{"parameterName", linkjiru::detectParamName},
                                            {"explanation", "speaker: 0 = baji, 1 = ru"},
                                            {"min", 0.0f},
                                            {"max", 1.0f},
                                            {"defaultValue", 0.0f}},
                                           "param-create"));
            return;
        }

        if (valueDirty)
        {
            valueDirty = false;
            sendFireAndForget(makeEnvelope(
                "InjectParameterDataRequest",
                {{"faceFound", false},
                 {"mode", "set"},
                 {"parameterValues", json::array({{{"id", linkjiru::detectParamName}, {"value", pendingValue}}})}},
                "inject-param"));
        }
    }

    void sendFireAndForget(const json& request)
    {
        writePayload  = request.dump();
        writeInFlight = true;
        setTimeout(OP_TIMEOUT_MS);
        ws->async_write(net::buffer(writePayload),
                        [this](const beast::error_code& ec, std::size_t)
                        {
                            if (shuttingDown)
                            {
                                return;
                            }
                            writeInFlight = false;
                            if (ec)
                            {
                                fail();
                            }
                            // Success: response (ack / creation / error) arrives via the steady read.
                        });
    }

    void pump()
    {
        if (ioc.stopped())
        {
            ioc.restart();
        }

        if (wantConnection && state == State::Disconnected && !needsRecycle && clock_type::now() >= nextAttempt)
        {
            startConnect();
        }

        if (state == State::Ready)
        {
            startWriteIfNeeded();
        }

        ioc.poll();

        // Recycle a dropped socket now that no handler is executing: cancel any
        // stragglers, drain their (Disconnected-guarded, no-op) completions,
        // then destroy the stream. Safe because we are outside poll().
        if (needsRecycle)
        {
            if (ws)
            {
                beast::get_lowest_layer(*ws).cancel();
            }
            while (ioc.poll() > 0)
            {
            }
            ws.reset();
            needsRecycle = false;
        }
    }

    void teardown()
    {
        shuttingDown = true;
        if (ws)
        {
            beast::get_lowest_layer(*ws).cancel();
        }
        // Drain any pending handlers (they observe shuttingDown and bail).
        if (ioc.stopped())
        {
            ioc.restart();
        }
        ioc.poll();
        ws.reset();
        shuttingDown = false;
    }
};

// ── Public interface ────────────────────────────────────────────────────────

VTubeStudioClient::VTubeStudioClient() : impl(std::make_unique<Impl>()) {}

VTubeStudioClient::~VTubeStudioClient()
{
    // Never let a Boost/asio teardown exception escape a destructor.
    try
    {
        impl->teardown();
    }
    catch (...)
    {
    }
}

void VTubeStudioClient::pump() const
{
    impl->pump();
}

void VTubeStudioClient::connect(const std::string& host, const std::string& port) const
{
    impl->host           = host;
    impl->port           = port;
    impl->wantConnection = true;
    impl->nextAttempt    = clock_type::now();
}

void VTubeStudioClient::disconnect() const
{
    impl->wantConnection = false;
    impl->teardown();
    impl->state          = Impl::State::Disconnected;
    impl->registered     = false;
    impl->registering    = false;
    impl->registerFailed = false;
}

bool VTubeStudioClient::isConnected() const
{
    return impl->state == Impl::State::Ready;
}

bool VTubeStudioClient::isRegistered() const
{
    return impl->registered;
}

bool VTubeStudioClient::isRegisterFailed() const
{
    return impl->registerFailed;
}

void VTubeStudioClient::requestRegister() const
{
    impl->registerFailed    = false;
    impl->registerRequested = true;
}

void VTubeStudioClient::setDetectValue(const float value) const
{
    impl->pendingValue = value;
    impl->valueDirty   = true;
}
