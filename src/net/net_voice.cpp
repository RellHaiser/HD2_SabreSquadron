// The voice chat's DirectPlay Voice wrapper (part 10d-iv; the net library's 0x00742190..0x00742640): a server's
// session and a client's connection on the game's DirectPlay object. See net_game.h.
#include "net/dplay8.h"

#include "net/net_game.h"

#include <cstring>

namespace {

// DirectPlay Voice's codes and flags the wrapper uses.
constexpr HRESULT DVERR_RUNSETUP = static_cast<HRESULT>(0x80150183);
constexpr HRESULT DVERR_BUFFERTOOSMALL = static_cast<HRESULT>(0x8015001E);
constexpr HRESULT CO_E_NOTINITIALIZED_ = static_cast<HRESULT>(0x800401F0);
constexpr DWORD DVFLAGS_SYNC = 1;
constexpr DWORD DVFLAGS_QUERYONLY = 2;
constexpr DWORD DVFLAGS_ALLOWBACK = 0x10;
constexpr DWORD DVSOUNDCONFIG_HALFDUPLEX = 4;

// DVSESSIONDESC (0x24) and DVSOUNDDEVICECONFIG (0x40).
struct S_VoiceSession {
    DWORD size;
    DWORD flags;
    DWORD sessionType;
    GUID codec;
    DWORD bufferQuality;
    DWORD bufferAggressiveness;
};
struct S_VoiceSoundConfig {
    DWORD size;
    DWORD flags;
    GUID playbackDevice;
    void *playback;
    GUID captureDevice;
    void *capture;
    HWND window;
    void *mainBuffer;
    DWORD mainBufferFlags;
    DWORD mainBufferPriority;
};
static_assert(sizeof(S_VoiceSession) == 0x24 && sizeof(S_VoiceSoundConfig) == 0x40, "DirectPlay Voice structures");

// The result with its success codes made 0.
long Succeeded0(HRESULT hr)
{
    return hr >= 0 ? 0 : hr;
}

}  // namespace

C_NetVoice::C_NetVoice(NetVoiceHandler clientHandler, NetVoiceHandler serverHandler)
    : m_clientHandler(clientHandler), m_serverHandler(serverHandler)
{
    m_halfDuplex = 0;
}

C_NetVoice::~C_NetVoice()
{
    Stop();
}

long C_NetVoice::Init(void *window, bool server, bool client, IUnknown *session, int sessionType, const _GUID *codec,
                      void *clientConfig, int unused)
{
    if (server) {
        const long hr = StartServer(session, sessionType, codec);
        if (hr < 0)
            return hr;
    }
    if (client) {
        long hr = CheckAudio(window);
        if (hr < 0)
            return hr;
        hr = StartClient(window, session, clientConfig, unused);
        if (hr < 0)
            return hr;
    }
    return 0;
}

long C_NetVoice::Stop()
{
    if (m_client) {
        const long hr = StopClient();
        if (hr < 0)
            return hr;
    }
    if (m_server) {
        const long hr = StopServer();
        if (hr < 0)
            return hr;
    }
    return 0;
}

long __stdcall C_NetVoice::CheckAudio(void *window)
{
    IDirectPlayVoiceTest *test = nullptr;
    HRESULT hr = NetCreateInstance(CLSID_NetVoiceTest, CLSCTX_INPROC_SERVER, IID_NetVoiceTest, reinterpret_cast<void **>(&test));
    if (hr < 0)
        return hr;
    // Only asked first; DirectPlay Voice's wizard when the setup must be run.
    hr = test->CheckAudioSetup(&g_netVoicePlayback, &g_netVoiceCapture, static_cast<HWND>(window), DVFLAGS_QUERYONLY);
    if (hr == DVERR_RUNSETUP)
        hr = test->CheckAudioSetup(&g_netVoicePlayback, &g_netVoiceCapture, static_cast<HWND>(window), DVFLAGS_ALLOWBACK);
    if (test)
        test->Release();
    return hr;
}

long C_NetVoice::StartServer(IUnknown *session, int sessionType, const _GUID *codec)
{
    HRESULT hr = NetCreateInstance(CLSID_NetVoiceServer, CLSCTX_INPROC_SERVER, IID_NetVoiceServer,
                                   reinterpret_cast<void **>(&m_server));
    if (hr < 0)
        return hr;
    hr = m_server->Initialize(session, reinterpret_cast<PDVMESSAGEHANDLER>(m_serverHandler), nullptr, nullptr, 0);
    if (hr < 0)
        return hr;
    S_VoiceSession desc = {};
    desc.size = sizeof desc;
    desc.sessionType = static_cast<DWORD>(sessionType);
    desc.codec = *codec;
    return Succeeded0(m_server->StartSession(&desc, 0));
}

long C_NetVoice::StartClient(void *window, IUnknown *session, void *clientConfig, int)
{
    HRESULT hr = NetCreateInstance(CLSID_NetVoiceClient, CLSCTX_INPROC_SERVER, IID_NetVoiceClient,
                                   reinterpret_cast<void **>(&m_client));
    if (hr < 0)
        return hr;
    hr = m_client->Initialize(session, reinterpret_cast<PDVMESSAGEHANDLER>(m_clientHandler), window, nullptr, 0);
    if (hr < 0)
        return hr;
    S_VoiceSoundConfig sound = {};
    sound.size = sizeof sound;
    sound.playbackDevice = g_netVoicePlayback;
    sound.captureDevice = g_netVoiceCapture;
    sound.window = static_cast<HWND>(window);
    hr = m_client->Connect(&sound, clientConfig, DVFLAGS_SYNC);
    if (hr < 0)
        return hr;
    // Everyone hears it (the team: UpdateVoiceTeam).
    const DWORD all = 0;
    hr = m_client->SetTransmitTargets(&all, 1, 0);
    if (hr < 0)
        return hr;
    DWORD size = 0;
    hr = m_client->GetSoundDeviceConfig(nullptr, &size);
    if (hr < 0 && hr != DVERR_BUFFERTOOSMALL)
        return hr;
    auto *config = static_cast<S_VoiceSoundConfig *>(operator new(size));
    std::memset(config, 0, size);
    config->size = sizeof(S_VoiceSoundConfig);
    hr = m_client->GetSoundDeviceConfig(config, &size);
    if (hr < 0)
        return hr;
    m_halfDuplex = (config->flags & DVSOUNDCONFIG_HALFDUPLEX) != 0;
    operator delete(config);
    return 0;
}

long C_NetVoice::SetClientConfig(void *config)
{
    if (!m_client)
        return CO_E_NOTINITIALIZED_;
    return Succeeded0(m_client->SetClientConfig(config));
}

long C_NetVoice::StopClient()
{
    if (m_client) {
        m_client->Disconnect(DVFLAGS_SYNC);
        if (m_client) {
            m_client->Release();
            m_client = nullptr;
        }
    }
    return 0;
}

long C_NetVoice::StopServer()
{
    if (m_server) {
        m_server->StopSession(0);
        if (m_server) {
            m_server->Release();
            m_server = nullptr;
        }
    }
    return 0;
}
