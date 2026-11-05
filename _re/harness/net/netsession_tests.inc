// (Part 10d-iv.) The DirectPlay session, the voice chat, the bandwidth, a status in a message (net/net_session.cpp,
// net/net_voice.cpp, net/net_com.cpp). The session build only (build_session.bat: -DNET_SESSION_TEST); included by
// netgame_test.cpp after the other tests (their Fresh, Player, MakeHuman, TestPort, ...).

// ---- the fakes ---------------------------------------------------------------------------------------------------------

std::string Narrow(const wchar_t *text)
{
    std::string out;
    for (; text && *text; ++text)
        out += static_cast<char>(*text);
    return out;
}
std::string GuidName(const GUID *guid)
{
    if (!guid)
        return "null";
    if (std::memcmp(guid, &CLSID_NetDP8SP_TCPIP, sizeof(GUID)) == 0)
        return "tcpip";
    if (std::memcmp(guid, &g_netApplicationGuid, sizeof(GUID)) == 0)
        return "game";
    for (int i = 0; i < 8; ++i)
        if (std::memcmp(guid, &g_netVoiceCodecs[i], sizeof(GUID)) == 0)
            return "codec" + std::to_string(i == 4 ? 0 : i);
    char text[16];
    std::snprintf(text, sizeof text, "%08lx", static_cast<unsigned long>(guid->Data1));
    return text;
}

// DirectPlay's server and client: the session's calls recorded (the 10d-i fakes' others kept).
long g_hostResult, g_initResult, g_enumResult, g_clientInitResult, g_enumHostsResult, g_clientInfoResult, g_connectResult;
int g_enumNullBuffer;
struct T_SessionServer : T_Server {
    HRESULT STDMETHODCALLTYPE Initialize(void *, PFNDPNMESSAGEHANDLER handler, DWORD flags) override
    {
        Log("server init %d %u", handler == reinterpret_cast<PFNDPNMESSAGEHANDLER>(ServerMessageHandler), flags);
        return g_initResult;
    }
    HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID *sp, const GUID *application, void *buffer, DWORD *size,
                                                   DWORD *count, DWORD flags) override
    {
        Log("server enum %s %d %d %u", GuidName(sp).c_str(), application != nullptr, buffer != nullptr, flags);
        if (!buffer) {
            *size = 0x30;
            *count = 1;
            return static_cast<HRESULT>(0x80158230);   // (the buffer too small)
        }
        // The first provider's device GUID at +4: 0x0D0D0D0D...
        for (int i = 0; i < 0x30; ++i)
            static_cast<uint8_t *>(buffer)[i] = static_cast<uint8_t>(i);
        return g_enumResult;
    }
    HRESULT STDMETHODCALLTYPE SetCaps(const void *caps, DWORD flags) override
    {
        const auto *c = static_cast<const DPN_CAPS *>(caps);
        Log("server caps %u %u %u %d %u %u", c->size, c->flags, c->connectTimeout, static_cast<int>(c->connectRetries),
            c->timeoutUntilKeepAlive, flags);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Host(const void *desc, void **addresses, DWORD count, const void *security,
                                   const void *credentials, void *context, DWORD flags) override
    {
        const auto *d = static_cast<const DPN_APPLICATION_DESC *>(desc);
        Log("server host %u %u %s %s %d %u %d %d %d %u", d->size, d->flags, GuidName(&d->application).c_str(),
            Narrow(d->sessionName).c_str(), addresses && *addresses, count, security != nullptr,
            credentials != nullptr, context != nullptr, flags);
        return g_hostResult;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionInfo(DPNID id, void *info, DWORD) override
    {
        auto *i = static_cast<DPN_CONNECTION_INFO *>(info);
        i->roundTripLatency = id * 10;
        i->bytesSentGuaranteed = id * 100;
        i->bytesSentNonGuaranteed = id;
        i->bytesReceivedGuaranteed = id * 1000;
        i->bytesReceivedNonGuaranteed = id * 2;
        return 0;
    }
};
DPN_CONNECTION_INFO g_clientInfo;
struct T_SessionClient : T_Client {
    HRESULT STDMETHODCALLTYPE Initialize(void *, PFNDPNMESSAGEHANDLER handler, DWORD flags) override
    {
        Log("client init %d %u", handler == reinterpret_cast<PFNDPNMESSAGEHANDLER>(ClientMessageHandler), flags);
        return g_clientInitResult;
    }
    HRESULT STDMETHODCALLTYPE EnumHosts(void *desc, void *host, void *device, const void *data, DWORD size, DWORD count,
                                        DWORD interval, DWORD timeout, void *context, DPNHANDLE *handle,
                                        DWORD flags) override
    {
        const auto *d = static_cast<const DPN_APPLICATION_DESC *>(desc);
        Log("client enum %u %s %d %d %d %u %d %u %d %d %d %u", d->size, GuidName(&d->application).c_str(),
            host != nullptr, device != nullptr, *static_cast<const int *>(data), size, static_cast<int>(count),
            interval, static_cast<int>(timeout), context != nullptr, handle != nullptr, flags);
        return g_enumHostsResult;
    }
    HRESULT STDMETHODCALLTYPE SetClientInfo(const void *info, void *context, DPNHANDLE *handle, DWORD flags) override
    {
        const auto *i = static_cast<const DPN_PLAYER_INFO *>(info);
        Log("client info %u %u %s %d %d %08x", i->size, i->infoFlags, Narrow(i->name).c_str(), context != nullptr,
            handle != nullptr, flags);
        return g_clientInfoResult;
    }
    HRESULT STDMETHODCALLTYPE SetCaps(const void *caps, DWORD flags) override
    {
        const auto *c = static_cast<const DPN_CAPS *>(caps);
        Log("client caps %u %u %u %d %u %u", c->size, c->flags, c->connectTimeout, static_cast<int>(c->connectRetries),
            c->timeoutUntilKeepAlive, flags);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Connect(const void *desc, void *host, void *device, const void *security,
                                      const void *credentials, const void *data, DWORD size, void *context,
                                      DPNHANDLE *handle, DWORD flags) override
    {
        const auto *d = static_cast<const DPN_APPLICATION_DESC *>(desc);
        const auto *join = static_cast<const S_JoinRequest *>(data);
        Log("client connect %s %d %d %d %d %s %d %d %d %s %u %d %d %u", GuidName(&d->application).c_str(),
            host != nullptr, device != nullptr, security != nullptr, credentials != nullptr, join->password,
            join->versionMajor, join->versionMinor, join->unknown1C, join->name, size, context != nullptr,
            handle != nullptr, flags);
        return g_connectResult;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionInfo(void *info, DWORD) override
    {
        std::memcpy(info, &g_clientInfo, sizeof g_clientInfo);
        return 0;
    }
};

// A DirectPlay address: its calls recorded (its hostname answered).
int g_addresses;
const wchar_t *g_addressHost = L"10.1.2.3";
struct T_Address final : IDirectPlay8Address {
    int id = ++g_addresses;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        Log("address %d release", id);
        delete this;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE BuildFromURLW(const wchar_t *) override { return 0; }
    HRESULT STDMETHODCALLTYPE BuildFromURLA(const char *) override { return 0; }
    HRESULT STDMETHODCALLTYPE Duplicate(IDirectPlay8Address **) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetEqual(IDirectPlay8Address *) override { return 0; }
    HRESULT STDMETHODCALLTYPE IsEqual(IDirectPlay8Address *) override { return 0; }
    HRESULT STDMETHODCALLTYPE Clear() override { return 0; }
    HRESULT STDMETHODCALLTYPE GetURLW(wchar_t *, DWORD *) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetURLA(char *, DWORD *) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetSP(GUID *) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetUserData(void *, DWORD *) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetSP(const GUID *sp) override
    {
        Log("address %d sp %s", id, GuidName(sp).c_str());
        return g_setSPResult;
    }
    HRESULT STDMETHODCALLTYPE SetUserData(const void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetNumComponents(DWORD *) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetComponentByName(const wchar_t *name, void *buffer, DWORD *size, DWORD *type) override
    {
        Log("address %d get %s %u", id, Narrow(name).c_str(), *size);
        std::wcscpy(static_cast<wchar_t *>(buffer), g_addressHost);
        *type = 1;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetComponentByIndex(DWORD, wchar_t *, DWORD *, void *, DWORD *, DWORD *) override { return 0; }
    HRESULT STDMETHODCALLTYPE AddComponent(const wchar_t *name, const void *data, DWORD size, DWORD type) override
    {
        if (type == 2)
            Log("address %d add %s %u %u", id, Narrow(name).c_str(), *static_cast<const DWORD *>(data), size);
        else
            Log("address %d add %s %s %u %u", id, Narrow(name).c_str(), Narrow(static_cast<const wchar_t *>(data)).c_str(),
                size, type);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(GUID *) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetDevice(const GUID *device) override
    {
        Log("address %d device %08lx", id, static_cast<unsigned long>(device->Data1));
        return 0;
    }
    static long g_setSPResult;
};
long T_Address::g_setSPResult;

// DirectPlay Voice's objects.
long g_voiceTestResult, g_voiceSecondTestResult;
DWORD g_voiceSoundFlags;
struct T_VoiceTest final : IDirectPlayVoiceTest {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        Log("voice test release");
        delete this;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE CheckAudioSetup(const GUID *playback, const GUID *capture, HWND window, DWORD flags) override
    {
        Log("voice test %d %d %p %u", std::memcmp(playback, &g_netVoicePlayback, sizeof(GUID)) == 0,
            std::memcmp(capture, &g_netVoiceCapture, sizeof(GUID)) == 0, static_cast<void *>(window), flags);
        return flags == 2 ? g_voiceTestResult : g_voiceSecondTestResult;
    }
};
struct T_VoiceServer final : IDirectPlayVoiceServer {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        Log("voice server release");
        delete this;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IUnknown *session, PDVMESSAGEHANDLER handler, void *context, DWORD *mask,
                                         DWORD count) override
    {
        Log("voice server init %p %d %d %d %u", static_cast<void *>(session),
            handler == reinterpret_cast<PDVMESSAGEHANDLER>(VoiceServerHandler), context != nullptr, mask != nullptr, count);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE StartSession(void *desc, DWORD flags) override
    {
        const auto *d = static_cast<const DWORD *>(desc);
        Log("voice server session %u %u %u %s %u %u %u", d[0], d[1], d[2],
            GuidName(reinterpret_cast<const GUID *>(d + 3)).c_str(), d[7], d[8], flags);
        return 1;
    }
    HRESULT STDMETHODCALLTYPE StopSession(DWORD flags) override
    {
        Log("voice server stop %u", flags);
        return 0;
    }
};
struct T_VoiceClient final : IDirectPlayVoiceClient {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        Log("voice client release");
        delete this;
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IUnknown *session, PDVMESSAGEHANDLER handler, void *context, DWORD *mask,
                                         DWORD count) override
    {
        Log("voice client init %p %d %p %d %u", static_cast<void *>(session),
            handler == reinterpret_cast<PDVMESSAGEHANDLER>(VoiceClientHandler), context, mask != nullptr, count);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Connect(void *sound, void *config, DWORD flags) override
    {
        const auto *s = static_cast<const uint8_t *>(sound);
        DWORD size, soundFlags;
        GUID playback, capture;
        void *window;
        std::memcpy(&size, s, 4);
        std::memcpy(&soundFlags, s + 4, 4);
        std::memcpy(&playback, s + 8, 16);
        std::memcpy(&capture, s + 0x1C, 16);
        std::memcpy(&window, s + 0x30, 4);
        static const DWORD none[8] = {};
        const auto *c = config ? static_cast<const DWORD *>(config) : none;
        Log("voice client connect %u %u %d %d %p %u %u %u %d %u", size, soundFlags,
            std::memcmp(&playback, &g_netVoicePlayback, 16) == 0, std::memcmp(&capture, &g_netVoiceCapture, 16) == 0,
            window, c[0], c[1], c[2], static_cast<int>(c[4]), flags);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Disconnect(DWORD flags) override
    {
        Log("voice client disconnect %u", flags);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetSessionDesc(void *) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetClientConfig(void *) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetClientConfig(void *config) override
    {
        Log("voice client config %d", static_cast<int>(static_cast<const DWORD *>(config)[3]));
        return 3;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(void *) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetCompressionTypes(void *, DWORD *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetTransmitTargets(const DWORD *targets, DWORD count, DWORD flags) override
    {
        std::string list;
        for (DWORD i = 0; targets && i < count; ++i)
            list += " " + std::to_string(targets[i]);
        Log("voice client targets %u%s %d %u", count, list.c_str(), targets != nullptr, flags);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetTransmitTargets(DWORD *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Create3DSoundBuffer(DWORD id, void *buffer, DWORD priority, DWORD flags,
                                                  void **user3DBuffer) override;
    HRESULT STDMETHODCALLTYPE Delete3DSoundBuffer(DWORD, void **) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetNotifyMask(DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetSoundDeviceConfig(void *config, DWORD *size) override
    {
        if (!config) {
            *size = 0x48;
            return static_cast<HRESULT>(0x8015001E);
        }
        Log("voice client device %u %u", static_cast<const DWORD *>(config)[0], *size);
        static_cast<DWORD *>(config)[1] = g_voiceSoundFlags;
        return 0;
    }
};
struct T_Sound3D : IDirectSound3DBufferLite {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused0C() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused10() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused14() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused18() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused1C() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused20() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused24() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused28() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused2C() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused30() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused34() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused38() override { return 0; }
    HRESULT STDMETHODCALLTYPE Unused3C() override { return 0; }
    HRESULT STDMETHODCALLTYPE SetMaxDistance(float distance, DWORD apply) override
    {
        Log("3d max %.0f %u", distance, apply);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SetMinDistance(float distance, DWORD apply) override
    {
        Log("3d min %.0f %u", distance, apply);
        return 0;
    }
};
T_Sound3D g_sound3D;
HRESULT STDMETHODCALLTYPE T_VoiceClient::Create3DSoundBuffer(DWORD id, void *buffer, DWORD priority, DWORD flags,
                                                           void **user3DBuffer)
{
    Log("voice client 3d %u %d %u %u", id, buffer != nullptr, priority, flags);
    *user3DBuffer = id == 0 ? nullptr : &g_sound3D;
    return 0;
}

// The objects made by CLSID (`g_createFail`: that one fails).
const GUID *g_createFail;
HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object)
{
    Log("create %s %u", GuidName(&clsid).c_str(), context);
    *object = nullptr;
    if (g_createFail == &clsid)
        return static_cast<HRESULT>(0x80040154);
    auto is = [&](const GUID &other) { return std::memcmp(&clsid, &other, sizeof(GUID)) == 0; };
    if (is(CLSID_NetDirectPlay8Server) && std::memcmp(&iid, &IID_NetDirectPlay8Server, sizeof(GUID)) == 0)
        *object = static_cast<IDirectPlay8Server *>(new T_SessionServer);
    else if (is(CLSID_NetDirectPlay8Client) && std::memcmp(&iid, &IID_NetDirectPlay8Client, sizeof(GUID)) == 0)
        *object = static_cast<IDirectPlay8Client *>(new T_SessionClient);
    else if (is(CLSID_NetDirectPlay8Address) && std::memcmp(&iid, &IID_NetDirectPlay8Address, sizeof(GUID)) == 0)
        *object = static_cast<IDirectPlay8Address *>(new T_Address);
    else if (is(CLSID_NetVoiceTest) && std::memcmp(&iid, &IID_NetVoiceTest, sizeof(GUID)) == 0)
        *object = static_cast<IDirectPlayVoiceTest *>(new T_VoiceTest);
    else if (is(CLSID_NetVoiceServer) && std::memcmp(&iid, &IID_NetVoiceServer, sizeof(GUID)) == 0)
        *object = static_cast<IDirectPlayVoiceServer *>(new T_VoiceServer);
    else if (is(CLSID_NetVoiceClient) && std::memcmp(&iid, &IID_NetVoiceClient, sizeof(GUID)) == 0)
        *object = static_cast<IDirectPlayVoiceClient *>(new T_VoiceClient);
    return *object ? 0 : static_cast<HRESULT>(0x80004002);
}

// The game around: the app's state (SetPendingState, vtable +0x2C), IGraph's GetHWND (+0x48) and +0xDC, the Chinese
// text check, the settings' description, the replication's factories, the server's messages (part 10d-v).
void __attribute__((thiscall)) FakeSetPendingState(void *, int state) { Log("pending state %d", state); }
void *g_stateVtable[32];
void *g_stateObject = g_stateVtable;
HWND __stdcall FakeGetHWND(void *) { return reinterpret_cast<HWND>(0x1234); }
void __stdcall FakeUnknownDC(void *, int on) { Log("graph dc %d", on); }
bool g_cjk;
bool HasCjkCharacters(const uint16_t *) { return g_cjk; }
void S_MultiplayerSettings::WriteDescription(const char *fileName) { Log("describe %s", fileName); }
C_NetObjectMessage *__fastcall NetCreateMessage(void *, C_BitStream *, bool) { return nullptr; }
void __fastcall NetReadControlBits(void *, C_BitStream *) {}
C_NetObjectMessage *__fastcall MakeSeatWeaponMessage(void *, C_BitStream *, bool) { return nullptr; }
void __fastcall CarNetReadState(void *, C_BitStream *) {}
#ifndef NET_MESSAGES_TEST
// (The messages' build links the real one: net_messages.cpp.)
long __stdcall C_NetGame::HandleServerMessage(C_NetGame *net, void *, uint32_t type, void *)
{
    Log("server message %08x %d", type, net == &g_netGame);
    return 7;
}
#endif

// ---- the tests ---------------------------------------------------------------------------------------------------------

void SessionReset()
{
    g_hostResult = g_initResult = g_enumResult = g_clientInitResult = g_enumHostsResult = g_clientInfoResult = 0;
    g_connectResult = 0;
    T_Address::g_setSPResult = 0;
    g_createFail = nullptr;
    g_addresses = 0;
}

void TestNetText()
{
    wchar_t wide[16];
    NetAnsiToWide(wide, "abc", static_cast<unsigned>(-1));
    CHECK(Narrow(wide) == "abc");
    NetAnsiToWide(wide, "abcdef", 4);
    CHECK(Narrow(wide) == "abc" && wide[3] == 0);
    NetAnsiToWide(nullptr, "x", 1);
    NetAnsiToWide(wide, nullptr, 1);
    char narrow[16];
    NetWideToAnsi(narrow, L"hello", -1);
    CHECK(std::strcmp(narrow, "hello") == 0);
    NetWideToAnsi(narrow, L"hello", 3);
    CHECK(std::strcmp(narrow, "he") == 0);
    NetWideToAnsi(nullptr, L"x", 1);
    // UTF-8: two bytes for an e acute.
    NetUtf8ToWide(wide, "\xC3\xA9t\xC3\xA9", static_cast<unsigned>(-1));
    CHECK(wide[0] == 0xE9 && wide[1] == 't' && wide[2] == 0xE9 && wide[3] == 0);
    NetWideToUtf8(narrow, L"\x00E9t", 16);
    CHECK(std::strcmp(narrow, "\xC3\xA9t") == 0);
    NetWideToUtf8(narrow, L"\x00E9t", 3);
    CHECK(std::strcmp(narrow, "\xC3\xA9") == 0);
    NetUtf8ToWide(wide, "abc", 2);
    CHECK(wide[0] == 'a' && wide[1] == 0);
    CHECK(!DirectPlayMissing());
    // An address's hostname: a.b.c.d, a in the low byte.
    T_Address address;
    g_log.clear();
    CHECK(GetHostAddress(&address) == 0x0302010Au && Logged("address 1 get hostname 50"));
}

void TestHost()
{
    C_NetGame &net = *new C_NetGame;
    net.Init();
    SessionReset();
    S_MultiplayerSettings &mp = g_testMp;
    g_gameProfile.m_mpSettings = &g_testMp;
    g_gameProfile.m_player = TestPlayerProfile("Hoster");
    mp.unknown174 = 1;
    mp.port = TestPort(70);
    mp.dedicated = false;
    mp.unknown178 = 0;
    S_NetSessionDesc desc = {};
    net.m_unknown448 = 5;
    net.m_unknown8 = true;
    net.m_graphFlags = 0x25;
    std::strcpy(desc.name, "Srv");
    std::memcpy(desc.serviceProvider, &CLSID_NetDP8SP_TCPIP, 16);
    std::strcpy(desc.password, "pw");
    g_log.clear();
    const int result = net.Host(desc, true);
    CHECK(result == 0 && net.m_server != nullptr && g_gameRole == (GAME_ROLE_SERVER | GAME_ROLE_CLIENT));
    CHECK(Logged("firewall 0 0") && Logged("create 2ae5b082 1") == false && Logged("server init 1 0") &&
          Logged("server enum tcpip 0 0 0") && Logged("server enum tcpip 0 1 0") && Logged("create 934a9523 23") &&
          Logged("address 1 sp tcpip") && Logged("address 1 device 07060504"));
    char line[128];
    std::snprintf(line, sizeof line, "address 1 add port %d 4", TestPort(70));
    CHECK(Logged(line) && Logged("server caps 20 0 2000 -1 4000 0") && Logged("app print Creating H&D2 Server v1.02") &&
          Logged("server host 72 1 game Srv 1 1 0 0 0 0") && Logged("address 1 release"));
    std::snprintf(line, sizeof line, "firewall %d 1", TestPort(70));
    CHECK(Logged(line) && net.m_string501C == "pw" && g_localPlayerSlot == 0 &&
          std::strcmp(net.m_players[0].name, "Hoster") == 0 && net.m_players[0].left && net.m_joinStatus == NET_JOIN_CONNECTED);
    const S_ServerEntry &entry = *reinterpret_cast<S_ServerEntry *>(net.m_serverEntry);
    CHECK(std::strcmp(entry.name, "Srv") == 0 && entry.address == 0 && entry.port == TestPort(70) && net.m_unknown448 == 0 &&
          !net.m_unknown8 && Logged("graph flags 5"));
    {
        sockaddr_in bound = {};
        int size = sizeof bound;
        getsockname(net.m_transport->m_socket.m_socket, reinterpret_cast<sockaddr *>(&bound), &size);
        CHECK(ntohs(bound.sin_port) == TestPort(71));
    }
    // GameSpy's listing: the reporting; the replication made; the transport on the port + 1.
    CHECK(Logged("gcd init 0 750") && net.m_reporting && net.m_replication != nullptr && net.m_transport->m_running);
    CHECK(net.m_replication->GetLocalObject().GetId() == 0 && net.m_replication->m_lag == 0.035f &&
          net.m_replication->m_field2 == 0x3B && g_netServerSide);
    std::snprintf(line, sizeof line, "%d", TestPort(71));
    CHECK(net.m_transport->m_socket.m_socket != INVALID_SOCKET);
    // The replication's quantisations: positions, deltas, rates.
    S_NetQuantization value, delta, rate;
    value.Setup({3000, 3000, 3000, 0}, {-1000, -1000, -1000, 0}, {0.01f, 0.01f, 0.01f, 0}, 100);
    delta.Setup({0.6f, 0.6f, 0.6f, 0}, {-0.3f, -0.3f, -0.3f, 0}, {0.005f, 0.007f, 0.005f, 0}, 100);
    rate.Setup({15, 15, 15, 0}, {-7.5f, -7.5f, -7.5f, 0}, {0.1f, 0.1f, 0.1f, 0}, 100);
    CHECK(SameQuantization(net.m_quantizations[0], value) && SameQuantization(net.m_quantizations[2], delta) &&
          SameQuantization(net.m_quantizations[1], rate) && net.m_replication->m_read[0] == &net.m_quantizations[0] &&
          net.m_replication->m_read[1] == &net.m_quantizations[2] && net.m_replication->m_read[2] == &net.m_quantizations[1] &&
          net.m_replication->m_write[1] == &net.m_quantizations[2]);
    // Its factories by the object's id: below 32 the controllers'; a human's too; a car's; others none.
    alignas(16) static unsigned char car[0x400], mine[0x400], human[0x400];
    reinterpret_cast<C_Actor *>(car)->m_type = ACTOR_CAR;
    reinterpret_cast<C_Actor *>(mine)->m_type = ACTOR_MINE;
    reinterpret_cast<C_Actor *>(human)->m_type = ACTOR_HUMAN;
    g_actors[40] = reinterpret_cast<C_Actor *>(car);
    g_actors[41] = reinterpret_cast<C_Actor *>(mine);
    g_actors[42] = reinterpret_cast<C_Actor *>(human);
    g_actors[5] = reinterpret_cast<C_Actor *>(car);
    g_actors[31] = reinterpret_cast<C_Actor *>(car);
    g_actors[32] = reinterpret_cast<C_Actor *>(car);
    auto factory = net.m_replication->m_makeFactory;
    auto reader = net.m_replication->m_makeStateReader;
    CHECK(factory(5) == NetCreateMessage && factory(40) == MakeSeatWeaponMessage && factory(41) == nullptr &&
          factory(42) == NetCreateMessage && factory(43) == NetCreateMessage && reader(5) == NetReadControlBits &&
          reader(40) == CarNetReadState && reader(41) == nullptr && reader(42) == NetReadControlBits &&
          reader(99) == NetReadControlBits && factory(31) == NetCreateMessage &&
          factory(32) == MakeSeatWeaponMessage && reader(31) == NetReadControlBits && reader(32) == CarNetReadState);
    net.DeleteReplication();
    CHECK(net.m_replication == nullptr);
    net.DeleteReplication();
    g_actors.clear();
    net.Close(true);
    // A dedicated server, no firewall, its voice (codec 2) and no client voice.
    mp.dedicated = true;
    mp.unknown178 = 2;
    g_options.voiceChat = true;
    g_log.clear();
    g_addresses = 0;
    CHECK(net.Host(desc, false) == 0 && g_gameRole == GAME_ROLE_SERVER && g_localPlayerSlot == -1 &&
          Logged("graph dc 0") && !Logged(line) && Logged("voice server init 00001234 1 0 0 0") == false);
    CHECK(net.m_voiceServer != nullptr && net.m_voiceClient == nullptr && Logged("voice server session 36 0 3 codec2 0 0 0"));
    g_options.voiceChat = false;
    net.Close(true);
    g_log.clear();
    net.StopVoiceServer();
    CHECK(net.m_voiceServer == nullptr && Logged("voice server release"));
    // Not GameSpy's listing: the port 31567, no reporting.
    mp.unknown174 = 0;
    mp.dedicated = false;
    mp.unknown178 = 0;
    g_log.clear();
    g_addresses = 0;
    CHECK(net.Host(desc, true) == 0 && Logged("firewall 31567 1") && Logged("address 1 add port 31567 4") &&
          !Logged("gcd init 0 750"));
    net.Close(true);
    // A provider other than TCP/IP: no port.
    std::memset(desc.serviceProvider, 0x11, 16);
    mp.unknown174 = 1;
    g_log.clear();
    g_addresses = 0;
    net.Host(desc, false);
    std::snprintf(line, sizeof line, "address 1 add port %d 4", TestPort(70));
    CHECK(!Logged(line) && Logged("address 1 sp 11111111"));
    net.Close(true);
    std::memcpy(desc.serviceProvider, &CLSID_NetDP8SP_TCPIP, 16);
    // The failures: the text and the result kept, the server released.
    struct Case {
        const char *text;
    };
    auto fails = [&](const char *text) {
        g_log.clear();
        g_addresses = 0;
        const int hr = net.Host(desc, false);
        return hr < 0 && net.m_lastError == hr && std::strcmp(net.m_lastErrorText, text) == 0 && net.m_server == nullptr &&
               (g_createFail == &CLSID_NetDirectPlay8Server || Logged("server release"));
    };
    g_createFail = &CLSID_NetDirectPlay8Server;
    CHECK(fails("DirectX 8 not installed. Can't create server"));
    g_createFail = nullptr;
    g_initResult = -5;
    CHECK(fails("Can't initialize server") && Logged("server release"));
    g_initResult = 0;
    g_enumResult = -6;
    CHECK(fails("Enumeration of devices failed.") && Logged("server cancel 0 32768") && Logged("server close 0"));
    g_enumResult = 0;
    g_createFail = &CLSID_NetDirectPlay8Address;
    CHECK(fails("Can't create address object"));
    g_createFail = nullptr;
    T_Address::g_setSPResult = -7;
    CHECK(fails("Can't set self address") && Logged("address 1 release"));
    T_Address::g_setSPResult = 0;
    g_hostResult = -8;
    CHECK(fails("Server::Host failed") && g_gameRole == GAME_ROLE_LOCAL && Logged("address 1 release"));
    g_hostResult = 0;
    net.Close(true);
    g_gameProfile.m_player = nullptr;
}

void TestHostDedicated()
{
    C_NetGame &net = Fresh();
    g_testMp.unknown174 = 1;
    g_testMp.port = 11001;
    g_log.clear();
    // (No hd2ds_sabresquadron.exe here: false.)
    CHECK(!net.HostDedicated(true) && Logged("firewall 11001 1") && Logged("describe XDServer_11001.txt") &&
          !Logged("pending state 4"));
    g_testMp.unknown174 = 0;
    g_log.clear();
    CHECK(!net.HostDedicated(false) && !Logged("firewall 31567 1") && Logged("describe XDServer_11001.txt"));
    g_testMp.unknown174 = 0;
    CHECK(!net.HostDedicated(true) && Logged("firewall 31567 1"));
}

void TestClientObjects()
{
    C_NetGame &net = Fresh();
    SessionReset();
    // Made once.
    g_log.clear();
    CHECK(net.CreateClient() == 0 && net.m_client != nullptr && Logged("create 743f1dc6 1") && Logged("client init 1 0"));
    g_log.clear();
    CHECK(net.CreateClient() == 0 && g_log.empty());
    net.m_client->Release();
    net.m_client = nullptr;
    g_createFail = &CLSID_NetDirectPlay8Client;
    CHECK(net.CreateClient() < 0 && std::strcmp(net.m_lastErrorText, "DirectX 8 not installed. Can't create client") == 0);
    g_createFail = nullptr;
    g_clientInitResult = -3;
    g_log.clear();
    CHECK(net.CreateClient() == -3 && net.m_client == nullptr && Logged("client release") &&
          std::strcmp(net.m_lastErrorText, "Client::Initialize failed") == 0 && net.m_lastError == -3);
    g_clientInitResult = 0;
    // The addresses: TCP/IP; the host's hostname and port (none: the port 31567).
    IDirectPlay8Address *device = nullptr, *host = nullptr;
    S_ServerEntry server;
    server.address = inet_addr("1.2.3.4");
    server.port = 4242;
    g_log.clear();
    net.CreateAddresses(&server, &device, &host);
    CHECK(Logged("address 1 sp tcpip") && Logged("address 2 sp tcpip") && Logged("address 2 add hostname 1.2.3.4 16 1") &&
          Logged("address 2 add port 4242 4") && g_log.size() == 6);
    device->Release();
    host->Release();
    g_log.clear();
    net.CreateAddresses(nullptr, &device, &host);
    CHECK(Logged("address 4 add port 31567 4") && !Logged("address 4 add hostname 1.2.3.4 16 1") && g_log.size() == 5);
    device->Release();
    host->Release();
}

void TestEnumHosts()
{
    C_NetGame &net = Fresh();
    SessionReset();
    S_ServerEntry server;
    server.address = inet_addr("5.6.7.8");
    server.port = 1000;
    // A server: once, 1 s; the request; the role 0x12; the addresses released.
    g_log.clear();
    CHECK(net.EnumHosts(&server, 1) == 0 && g_gameRole == 0x12u && Logged("client enum 72 game 1 1 1 4 1 0 1000 1 1 0") &&
          Logged("address 1 release") && Logged("address 2 release"));
    // The LAN: until cancelled.
    g_log.clear();
    CHECK(net.EnumHosts(nullptr, 0) == 0 && Logged("client enum 72 game 1 1 0 4 -1 0 -1 0 1 0"));
    for (long result : {0x0015800EL, static_cast<long>(0x80158040), static_cast<long>(0x80158320)}) {
        g_enumHostsResult = result;
        g_gameRole = 0;
        CHECK(net.EnumHosts(nullptr, 2) == 0 && g_gameRole == 0x12u);
    }
    // Failed: cancelled, closed, released; the text.
    g_enumHostsResult = -9;
    g_log.clear();
    CHECK(net.EnumHosts(nullptr, 0) == -9 && Logged("client cancel 0 32768") && Logged("client close 0") &&
          Logged("client release") && net.m_client == nullptr && std::strcmp(net.m_lastErrorText, "Client::EnumHosts failed") == 0);
    g_enumHostsResult = 1;
    CHECK(net.EnumHosts(nullptr, 0) == 1);
    g_enumHostsResult = 0;
    g_createFail = &CLSID_NetDirectPlay8Client;
    CHECK(net.EnumHosts(nullptr, 0) < 0 && std::strcmp(net.m_lastErrorText, "Client::Create failed") == 0);
    g_createFail = nullptr;
    // Pings, lists, status queries: their callbacks; requests 0, 2, 1.
    auto search = reinterpret_cast<void(__fastcall *)(S_ServerEntry *)>(+[](S_ServerEntry *) {});
    auto status = reinterpret_cast<void(__fastcall *)(S_ServerStatus *)>(+[](S_ServerStatus *) {});
    g_log.clear();
    net.PingServer(search, &server);
    CHECK(net.m_searchCallback == search && Logged("client enum 72 game 1 1 0 4 1 0 1000 1 1 0"));
    std::vector<S_ServerEntry *> servers = {&server, &server};
    g_log.clear();
    net.m_searchCallback = nullptr;
    net.PingServers(search, servers);
    CHECK(net.m_searchCallback == search);
    int twos = 0;
    for (const std::string &l : g_log)
        twos += l == "client enum 72 game 1 1 2 4 1 0 1000 1 1 0";
    CHECK(twos == 2);
    g_log.clear();
    CHECK(net.QueryServerStatus(status, &server) == 0 && net.m_statusCallback == status &&
          Logged("client enum 72 game 1 1 1 4 1 0 1000 1 1 0"));
    net.m_client->Release();
    net.m_client = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
}

struct S_ExpireLater {
    static DWORD WINAPI Run(void *)
    {
        Sleep(300);
        g_perfOffset += 20000;
        return 0;
    }
};

void TestConnect()
{
    C_NetGame &net = *new C_NetGame;
    net.Init();
    SessionReset();
    g_gameProfile.m_player = TestPlayerProfile("Joiner");
    g_testMp.unknown174 = 0;
    g_testMp.password = C_str("secret");
    S_ServerEntry server;
    server.address = inet_addr("127.0.0.1");
    server.port = 1234;
    server.voice = false;
    // Without GameSpy's listing: the port 31567; the firewall; the transport, the server's link (its port + 1); the
    // client's name, caps, the join request; joining.
    net.m_unknown448 = 5;
    net.m_unknown8 = true;
    net.m_joinTime = 9;
    net.m_joinStatus = 0;
    net.m_graphFlags = 0x21;
    g_log.clear();
    g_addresses = 0;
    CHECK(net.Connect(&server) == 0 && server.port == 31567 && Logged("firewall 31567 1") && Logged("graph flags 1") &&
          net.m_unknown448 == 0 && !net.m_unknown8 && net.m_serverLink->m_id == 0 && net.m_transport->m_running &&
          Logged("address 1 release") && Logged("address 2 release"));
    CHECK(net.m_serverLink != nullptr && net.m_serverLink->m_port == 31568 && net.m_serverLink->m_address == server.address &&
          net.m_transport->m_connections.size() == 1 && net.m_unknown338 == static_cast<int>(server.address) &&
          reinterpret_cast<S_ServerEntry *>(net.m_serverEntry)->port == 1234);
    CHECK(Logged("client cancel 0 32768") && Logged("client info 24 1 Joiner 0 0 80000000") &&
          Logged("client caps 20 0 2000 -1 4000 0") && Logged("client connect game 1 1 0 0 secret 1 2 2 Joiner 84 0 1 0") &&
          net.m_joinStatus == NET_JOIN_JOINING && g_gameRole == GAME_ROLE_CLIENT && net.m_joinTime == 0);
    // The connect's failures: role 7 (not E_PENDING).
    net.Close(true);
    g_connectResult = static_cast<long>(0x8000000A);
    CHECK(net.Connect(&server) == static_cast<int>(0x8000000A) && g_gameRole == GAME_ROLE_CLIENT);
    net.Close(true);
    g_connectResult = -4;
    CHECK(net.Connect(&server) == -4 && g_gameRole == GAME_ROLE_LOCAL &&
          std::strcmp(net.m_lastErrorText, "Connect failed") == 0);
    net.Close(true);
    g_connectResult = 0;
    g_clientInfoResult = -2;
    CHECK(net.Connect(&server) == -2 && std::strcmp(net.m_lastErrorText, "SetClientInfo failed") == 0);
    net.Close(true);
    g_clientInfoResult = 0;
    // The Unicode font: the name from UTF-8; a long name cut to the request's room.
    g_gameProfile.m_player = TestPlayerProfile("J\xC3\xA9" "0123456789012345678901234567890123456789012345678901234567");
    g_gameProfile.m_unicodeFont = true;
    g_log.clear();
    net.Connect(&server);
    bool named = false;
    for (const std::string &l : g_log)
        named = named || (l.rfind("client info 24 1 J", 0) == 0 && static_cast<unsigned char>(l[18]) == 0xE9);
    CHECK(named);
    char cut[64];
    std::snprintf(cut, sizeof cut, "J\xC3\xA9%.48s 84", "0123456789012345678901234567890123456789012345678901234567");
    bool cutName = false;
    for (const std::string &l : g_log)
        cutName = cutName || l.find(cut) != std::string::npos;
    CHECK(cutName);
    g_gameProfile.m_unicodeFont = false;
    g_gameProfile.m_player = TestPlayerProfile("Joiner");
    net.Close(true);
    // GameSpy's listing: the CD key first (no server: timed out, nothing more).
    g_testMp.unknown174 = 1;
    server.port = TestPort(80);
    HANDLE later = CreateThread(nullptr, 0, S_ExpireLater::Run, nullptr, 0, nullptr);
    g_log.clear();
    CHECK(net.Connect(&server) == 0 && net.m_joinStatus == NET_JOIN_TIMED_OUT &&
          !Logged("client info 24 1 Joiner 0 0 80000000") && net.m_keyClient.port == TestPort(82) &&
          server.port == TestPort(80));
    WaitForSingleObject(later, INFINITE);
    CloseHandle(later);
    closesocket(net.m_keySocket);
    net.m_keySocket = static_cast<uintptr_t>(-1);
    // Connected: joined, its voice when the server has one, the replication.
    net.m_joinStatus = 0;
    reinterpret_cast<S_ServerEntry *>(net.m_serverEntry)->voice = false;
    g_options.voiceChat = true;
    net.OnConnected();
    g_options.voiceChat = false;
    CHECK(net.m_joinStatus == NET_JOIN_CONNECTED && net.m_replication != nullptr && net.m_voiceClient == nullptr);
    g_testMp.unknown174 = 0;
    g_gameProfile.m_player = nullptr;
    net.Close(true);
}

// A host's answer for the handler.
std::vector<uint8_t> EnumAnswer(int kind, const char *map, bool newer, bool demo = false)
{
    C_GrowingBitStream w;
    w.WriteBits(static_cast<uint32_t>(kind), 2);
    w.WriteString(map);
    if (!newer) {
        w.WriteBits(2, 2);       // objectives
        w.WriteBits(1, 1);       // dedicated
        w.WriteBits(16, 6);
        w.WriteBits(5, 6);
        w.WriteBits(1, 1);       // password
        w.WriteBits(12, 8);      // minor
        w.WriteBits(1, 8);       // major
        w.WriteBits(static_cast<uint32_t>(-3) & 0xFFFF, 16);
        w.WriteBits(1, 1);       // voice
    } else {
        w.WriteBits(30, 8);
        w.WriteBits(1, 8);
        w.WriteBits(3, 3);       // cooperative
        w.WriteBits(0, 1);
        w.WriteBits(8, 6);
        w.WriteBits(2, 6);
        w.WriteBits(0, 1);
        w.WriteBits(4000, 16);
        w.WriteBits(1, 1);       // voice
        if (!demo)
            w.WriteBits(3, 2);
    }
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t *>(w.Data()), reinterpret_cast<const uint8_t *>(w.Data()) + w.ByteSize());
}

S_ServerEntry *g_answered;
S_ServerStatus *g_statusAnswered;
void __fastcall AnsweredServer(S_ServerEntry *server) { g_answered = server; }
void __fastcall AnsweredStatus(S_ServerStatus *status) { g_statusAnswered = status; }

void TestClientMessages()
{
    C_NetGame &net = Fresh();
    C_NetSocket socket;
    // Connected: the slot the reply gives (the link's id); refused: the reason.
    C_NetConnection link(5, 0, &socket);
    net.m_serverLink = &link;
    const uint8_t accept[1] = {0x83};
    DPNMSG_CONNECT_COMPLETE complete = {sizeof complete, 0, nullptr, 0, accept, 1};
    CHECK(C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_CONNECT_COMPLETE, &complete) == 0 &&
          g_localPlayerSlot == 3 && link.m_id == 3);
    const uint8_t refuse[1] = {4};
    complete.result = -1;
    complete.replyData = refuse;
    CHECK(C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_CONNECT_COMPLETE, &complete) == 0 && net.m_joinStatus == 4);
    net.m_joinStatus = 9;
    complete.replySize = 0;
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_CONNECT_COMPLETE, &complete);
    CHECK(net.m_joinStatus == 9 && g_localPlayerSlot == 3);
    // A host's answer: a search's (a new entry to the search callback).
    net.m_searchCallback = AnsweredServer;
    net.m_statusCallback = AnsweredStatus;
    g_game = nullptr;
    T_Address sender;
    DPN_APPLICATION_DESC application;
    application.sessionName = L"Hosted";
    std::vector<uint8_t> data = EnumAnswer(0, "Mapped", false);
    DPNMSG_ENUM_HOSTS_RESPONSE response = {sizeof response, &sender, nullptr, &application, data.data(),
                                           static_cast<DWORD>(data.size()), nullptr, 77};
    g_answered = nullptr;
    g_cjk = false;
    CHECK(C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response) == 0 && g_answered);
    S_ServerEntry *entry = g_answered;
    CHECK(std::strcmp(entry->name, "Hosted") == 0 && std::strcmp(entry->map, "Mapped") == 0 && entry->ping == 77 &&
          entry->address == 0x0302010Au && entry->gameType == MP_GAME_OBJECTIVES && entry->dedicated &&
          entry->maxPlayers == 16 && entry->players == 5 && entry->password && entry->versionMinor == 12 &&
          entry->versionMajor == 1 && entry->port == -3 && !entry->internet && entry->voice && entry->unknown94 == 1 &&
          entry->listed);
    delete entry;
    // A newer server's layout (a ping of a listed entry: that one filled); the expansion; "DEMO": 2.
    S_ServerEntry listed;
    response.context = &listed;
    data = EnumAnswer(0, "M2", true);
    response.responseData = data.data();
    response.responseSize = static_cast<DWORD>(data.size());
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response);
    CHECK(g_answered == &listed && listed.versionMinor == 30 && listed.versionMajor == 1 &&
          listed.gameType == MP_GAME_COOPERATIVE && !listed.dedicated && listed.maxPlayers == 8 && listed.players == 2 &&
          listed.port == 4000 && listed.unknown94 == 3 && std::strcmp(listed.map, "M2") == 0 && !listed.password &&
          listed.voice && !listed.internet);
    {
        // Version 1.20: the older layout still.
        std::vector<uint8_t> twenty = EnumAnswer(0, "M20", false);
        C_GrowingBitStream patch;
        patch.Attach(twenty.data(), static_cast<uint32_t>(twenty.size()) * 8);
        patch.SetBitPosition(2 + 4 * 8 + 2 + 1 + 6 + 6 + 1);
        patch.WriteBits(20, 8);
        response.responseData = twenty.data();
        response.responseSize = static_cast<DWORD>(twenty.size());
        C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response);
        CHECK(listed.versionMinor == 20 && listed.gameType == MP_GAME_OBJECTIVES && listed.port == -3);
    }
    application.sessionName = L"A DEMO";
    data = EnumAnswer(0, "M3", true, true);
    response.responseData = data.data();
    response.responseSize = static_cast<DWORD>(data.size());
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response);
    CHECK(listed.unknown94 == 2);
    // A Chinese name: from UTF-8.
    g_cjk = true;
    application.sessionName = L"\x00E9";
    data = EnumAnswer(0, "M", false);
    response.responseData = data.data();
    response.responseSize = static_cast<DWORD>(data.size());
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response);
    CHECK(std::strcmp(listed.name, "\xC3\xA9") == 0);
    g_cjk = false;
    // A status query's: the status read, the status callback; 1.
    S_ServerStatus sent;
    sent.players.resize(2);
    std::strcpy(sent.players[0].name, "p1");
    std::strcpy(sent.players[1].name, "p2");
    sent.players[1].team = 1;
    sent.players[1].points = -5;
    sent.maxClients = 12;
    sent.lives = -1;
    sent.difficulty = 3;
    sent.unknown40 = 9;
    sent.axisScore = 7;
    sent.alliedScore = 1000;
    C_GrowingBitStream w;
    w.WriteBits(1, 2);
    w.WriteString("M");
    for (uint32_t v : {2u, 1u, 16u, 5u, 1u, 12u, 1u, 3u, 1u}) {
        static const int bits[] = {2, 1, 6, 6, 1, 8, 8, 16, 1};
        static int i = 0;
        w.WriteBits(v, bits[i++ % 9]);
    }
    sent.Write(&w);
    std::vector<uint8_t> statusData(reinterpret_cast<const uint8_t *>(w.Data()),
                                    reinterpret_cast<const uint8_t *>(w.Data()) + w.ByteSize());
    response.responseData = statusData.data();
    response.responseSize = static_cast<DWORD>(statusData.size());
    g_statusAnswered = nullptr;
    g_answered = nullptr;
    CHECK(C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response) == 1 &&
          g_statusAnswered == &net.m_status && g_answered == nullptr);
    const S_ServerStatus &got = net.m_status;
    CHECK(got.players.size() == 2 && std::strcmp(got.players[1].name, "p2") == 0 && got.players[1].team == 1 &&
          got.players[1].points == -5 && got.maxClients == 12 && got.lives == -1 && got.difficulty == 3 &&
          got.unknown40 == 0 && got.axisScore == 7 && got.alliedScore == 1000);
    // In a game: ignored.
    g_game = reinterpret_cast<C_game *>(g_gameStorage);
    g_statusAnswered = nullptr;
    CHECK(C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_ENUM_HOSTS_RESPONSE, &response) == 0 && !g_statusAnswered);
    // Data: queued as outgoing (0x80).
    uint8_t bytes[3] = {1, 2, 3};
    DPNMSG_RECEIVE receive = {sizeof receive, 0, nullptr, bytes, 3, 0};
    net.m_outgoing.clear();
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_RECEIVE, &receive);
    receive.dataSize = 0;
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_RECEIVE, &receive);
    CHECK(net.m_outgoing.size() == 1 && net.m_outgoing.front().flags == 0x80 && net.m_outgoing.front().size == 3);
    net.m_outgoing.clear();
    // A send done: its data freed.
    DPNMSG_RETURN_BUFFER done = {sizeof done, 0, operator new(16)};
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_RETURN_BUFFER, &done);
    // The end: the link gone; back to the menus (0xB).
    auto *own = new C_NetConnection(1, 0, &socket);
    net.m_serverLink = own;
    net.m_transport->m_connections.push_back(own);
    net.m_joinStatus = 2;
    for (void *&slot : g_stateVtable)
        slot = reinterpret_cast<void *>(&FakeSetPendingState);   // (g++'s slots: two destructors)
    GetGameApp()->m_state = reinterpret_cast<C_GameState *>(&g_stateObject);
    g_log.clear();
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_TERMINATE_SESSION, nullptr);
    CHECK(net.m_serverLink == nullptr && net.m_transport->m_connections.empty() && net.m_joinStatus == 0 &&
          Logged("pending state 11"));
    GetGameApp()->m_state = nullptr;
    g_log.clear();
    C_NetGame::HandleClientMessage(&net, nullptr, DPN_MSGID_TERMINATE_SESSION, nullptr);
    CHECK(g_log.empty());
    CHECK(C_NetGame::HandleClientMessage(&net, nullptr, 0x1234, nullptr) == 0);
    // The handlers: the client's not in role 7; the server's for the server only (g_netGame's).
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(ClientMessageHandler(nullptr, DPN_MSGID_RECEIVE, &receive) == 0);
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    CHECK(ServerMessageHandler(nullptr, 5, nullptr) == 0 && g_log.empty());
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(ServerMessageHandler(nullptr, 5, nullptr) == 7 && Logged("server message 00000005 1"));
    g_gameRole = GAME_ROLE_CLIENT;
    g_netGame.m_outgoing.clear();
    receive.dataSize = 2;
    CHECK(ClientMessageHandler(nullptr, DPN_MSGID_RECEIVE, &receive) == 0 && g_netGame.m_outgoing.size() == 1);
    g_netGame.m_outgoing.clear();
    CHECK(VoiceClientHandler(nullptr, 1, nullptr) == 0 && VoiceServerHandler(nullptr, 1, nullptr) == 0);
    g_game = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
    net.m_serverLink = nullptr;
}

void TestVoice()
{
    // Init: the server first; the client after the audio check.
    char line0[128];
    C_NetVoice voice(VoiceClientHandler, VoiceServerHandler);
    IDirectPlay8Server *session = new T_SessionServer;
    g_voiceTestResult = 0;
    g_log.clear();
    CHECK(voice.Init(reinterpret_cast<void *>(0x99), true, true, session, 3, &g_netVoiceCodecs[5], nullptr, 0) == 0);
    std::vector<std::string> expect = {"create d3f5b8e6 1"};
    CHECK(g_log.size() > 8 && g_log[0] == "create d3f5b8e6 1" && g_log[2] == "voice server session 36 0 3 codec5 0 0 0" &&
          g_log[3] == "create 0f0f094b 1" && g_log[4] == "voice test 1 1 00000099 2" && g_log[5] == "voice test release" &&
          g_log[6] == "create b9f3eb85 1");
    std::snprintf(line0, sizeof line0, "voice server init %p 1 0 0 0", static_cast<void *>(static_cast<IUnknown *>(session)));
    CHECK(Logged(line0));
    uint32_t config[8] = {0x20, 0x28, 1, 0, 0xFFFFFFFE, 0, 0, 0};
    C_NetVoice client(VoiceClientHandler, nullptr);
    g_voiceSoundFlags = 4;
    g_log.clear();
    CHECK(client.StartClient(reinterpret_cast<void *>(0x77), session, config, 0) == 0 && client.m_halfDuplex == 1);
    char line[128];
    std::snprintf(line, sizeof line, "voice client init %p 1 00000077 0 0", static_cast<void *>(session));
    CHECK(Logged(line) && Logged("voice client connect 64 0 1 1 00000077 32 40 1 -2 1") &&
          Logged("voice client targets 1 0 1 0") && Logged("voice client device 64 72"));
    g_voiceSoundFlags = 3;
    C_NetVoice full(VoiceClientHandler, nullptr);
    full.StartClient(nullptr, session, config, 0);
    CHECK(full.m_halfDuplex == 0);
    // The config: set (its success 0); none: CO_E_NOTINITIALIZED.
    CHECK(client.SetClientConfig(config) == 0 && C_NetVoice(nullptr, nullptr).SetClientConfig(config) == static_cast<long>(0x800401F0));
    // The audio check: the wizard when the setup must run.
    g_voiceTestResult = static_cast<long>(0x80150183);
    g_voiceSecondTestResult = 5;
    g_log.clear();
    CHECK(C_NetVoice::CheckAudio(nullptr) == 5 && Logged("voice test 1 1 00000000 16"));
    g_voiceTestResult = -1;
    CHECK(C_NetVoice::CheckAudio(nullptr) == -1);
    g_voiceTestResult = 0;
    g_createFail = &CLSID_NetVoiceTest;
    CHECK(C_NetVoice::CheckAudio(nullptr) < 0);
    g_createFail = nullptr;
    // A client not made, the audio check failing: Init fails; deleted: stopped.
    {
        C_NetVoice failing(VoiceClientHandler, nullptr);
        g_createFail = &CLSID_NetVoiceClient;
        CHECK(failing.Init(nullptr, false, true, session, 3, nullptr, config, 0) < 0);
        g_createFail = nullptr;
        g_voiceTestResult = -4;
        CHECK(failing.Init(nullptr, false, true, session, 3, nullptr, config, 0) == -4 && failing.m_client == nullptr);
        g_voiceTestResult = 0;
        g_createFail = &CLSID_NetVoiceServer;
        CHECK(failing.Init(nullptr, true, false, session, 3, &g_netVoiceCodecs[1], nullptr, 0) < 0);
        g_createFail = nullptr;
        auto *deleted = new C_NetVoice(VoiceClientHandler, nullptr);
        deleted->StartClient(nullptr, session, config, 0);
        g_log.clear();
        delete deleted;
        CHECK(Logged("voice client disconnect 1") && Logged("voice client release"));
    }
    // Stopped: the client disconnected, the server's session stopped.
    g_log.clear();
    CHECK(voice.Stop() == 0 && voice.m_client == nullptr && voice.m_server == nullptr && Logged("voice client disconnect 1") &&
          Logged("voice server stop 0") && Logged("voice client release") && Logged("voice server release"));
    // The game's: the server's codec by the settings; the client on the server's object or the client's.
    C_NetGame &net = Fresh();
    net.m_server = session;
    net.m_client = new T_SessionClient;
    const int codecs[] = {1, 2, 3, 4, 5, 6, 7, 9};
    const char *names[] = {"codec1", "codec2", "codec3", "codec0", "codec5", "codec6", "codec7", "codec0"};
    bool chosen = true;
    for (int i = 0; i < 8; ++i) {
        g_testMp.unknown178 = codecs[i];
        g_log.clear();
        net.StartVoiceServer();
        chosen = chosen && Logged(("voice server session 36 0 3 " + std::string(names[i]) + " 0 0 0").c_str());
        net.StopVoiceServer();
    }
    CHECK(chosen && net.m_voiceServer == nullptr);
    g_testMp.unknown178 = 0;
    g_log.clear();
    net.StartVoiceServer();
    CHECK(net.m_voiceServer == nullptr && g_log.empty());
    g_log.clear();
    net.StartVoiceClient(true);
    CHECK(Logged("voice client connect 64 0 1 1 00001234 32 40 1 -2 1"));
    std::snprintf(line, sizeof line, "voice client init %p 1 00001234 0 0", static_cast<void *>(static_cast<IUnknown *>(session)));
    CHECK(Logged(line) && net.m_voiceClient != nullptr);
    g_log.clear();
    net.StopVoiceClient();
    CHECK(net.m_voiceClient == nullptr && Logged("voice client release"));
    g_log.clear();
    net.StartVoiceClient(false);
    std::snprintf(line, sizeof line, "voice client init %p 1 00001234 0 0",
                  static_cast<void *>(static_cast<IUnknown *>(net.m_client)));
    CHECK(Logged(line));
    // A player's 3D buffer: 5 m to 20 m.
    net.m_players.clear();
    Named(net, {"a", "b"});
    g_log.clear();
    net.MakeVoiceBuffer(1);
    CHECK(Logged("voice client 3d 20 0 0 0") && net.m_players[1].voiceBuffer == &g_sound3D && Logged("3d min 5 0") &&
          Logged("3d max 20 0"));
    net.m_players[0].connection = 0;
    g_log.clear();
    net.MakeVoiceBuffer(0);
    CHECK(g_log.size() == 1 && net.m_players[0].voiceBuffer == nullptr);
    net.VisitPlayers();
    // The team's players the targets (the server, or a server with voice).
    net.m_players.clear();
    Named(net, {"a", "b", "c", "d"});
    for (int i = 0; i < 4; ++i)
        net.m_players[static_cast<size_t>(i)].human = MakeHuman(false);
    g_humanTeams[net.m_players[1].human] = 1;
    net.m_players[2].connection = 0;
    g_localPlayerSlot = 0;
    g_game = reinterpret_cast<C_game *>(g_gameStorage);
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    net.UpdateVoiceTeam();
    CHECK(Logged("voice client targets 2 10 40 1 0") && Logged("game print Set Transmition team: 0 ------------------\n") &&
          Logged("game print a\n") && Logged("game print d\n") && !Logged("game print b\n") &&
          Logged("game print ------------------\n"));
    g_gameRole = GAME_ROLE_CLIENT;
    reinterpret_cast<S_ServerEntry *>(net.m_serverEntry)->voice = false;
    g_log.clear();
    net.UpdateVoiceTeam();
    CHECK(g_log.empty());
    reinterpret_cast<S_ServerEntry *>(net.m_serverEntry)->voice = true;
    net.UpdateVoiceTeam();
    CHECK(Logged("voice client targets 2 10 40 1 0"));
    // Nobody of the team: none.
    g_humanTeams[net.m_players[0].human] = 2;
    g_log.clear();
    net.UpdateVoiceTeam();
    CHECK(Logged("voice client targets 1 10 1 0"));
    for (int i = 1; i < 4; ++i)
        net.m_players[static_cast<size_t>(i)].connection = 0;
    net.m_players[0].connection = 0;
    g_log.clear();
    net.UpdateVoiceTeam();
    CHECK(Logged("voice client targets 0 0 0"));
    net.m_players[0].human = nullptr;
    g_log.clear();
    net.UpdateVoiceTeam();
    CHECK(g_log.empty());
    g_humanTeams.clear();
    net.StopVoiceClient();
    net.m_server = nullptr;
    net.m_client = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
    g_game = nullptr;
}

void TestBandwidth()
{
    C_NetGame &net = Fresh();
    C_NetSocket socket;
    // A client: the server link's ping, DirectPlay's bytes since the last time; the socket's, the replication's.
    T_SessionClient client;
    net.m_client = &client;
    C_NetConnection link(0, 0, &socket);
    link.m_roundTrip = 33;
    net.m_serverLink = &link;
    g_clientInfo.bytesSentGuaranteed = 100;
    g_clientInfo.bytesSentNonGuaranteed = 20;
    g_clientInfo.bytesReceivedGuaranteed = 300;
    g_clientInfo.bytesReceivedNonGuaranteed = 4;
    C_NetReplication replication;
    replication.m_bitsWritten = 80;
    replication.m_bitsRead = 17;
    net.m_replication = &replication;
    g_gameRole = GAME_ROLE_CLIENT;
    int figures[7];
    net.TakeBandwidth(figures);
    const int sent0 = figures[1], received0 = figures[2];
    CHECK(figures[0] == 33 && figures[5] == 10 && figures[6] == 2 && replication.m_bitsWritten == 0 &&
          replication.m_bitsRead == 0);
    g_clientInfo.bytesSentGuaranteed = 150;
    g_clientInfo.bytesReceivedNonGuaranteed = 10;
    net.TakeBandwidth(figures);
    CHECK(figures[1] == 50 && figures[2] == 6 && sent0 + 0 == sent0 && received0 + 0 == received0 && figures[5] == 0);
    net.m_serverLink = nullptr;
    net.m_replication = nullptr;
    net.TakeBandwidth(figures);
    CHECK(figures[0] == 0 && figures[1] == 0 && figures[5] == 0 && figures[6] == 0);
    // The server: the last connected player's (its link's ping, else DirectPlay's); one player: -1.
    T_SessionServer server;
    net.m_server = &server;
    g_gameRole = GAME_ROLE_SERVER;
    net.m_players.clear();
    Named(net, {"host"});
    net.TakeBandwidth(figures);
    CHECK(figures[0] == -1 && figures[1] == 0 && figures[2] == 0);
    Named(net, {"x"});
    net.m_players[1].connection = 0;
    net.TakeBandwidth(figures);
    CHECK(figures[0] == 0 && figures[1] == -170 && figures[2] == -310);
    net.m_players[1].connection = 5;
    net.TakeBandwidth(figures);
    CHECK(figures[0] == 50 && figures[1] == 505 && figures[2] == 5010);
    net.m_players.pop_back();
    net.m_transport->m_socket.m_sent = 12;
    net.m_transport->m_socket.m_received = 34;
    net.TakeBandwidth(figures);
    CHECK(figures[3] == 12 && figures[4] == 34 && net.m_transport->m_socket.m_sent == 0);
    Named(net, {"a", "b", "c"});
    net.m_players[3].connection = 0;
    C_NetConnection one(1, 0, &socket);
    one.m_roundTrip = 77;
    net.m_transport->m_connections.push_back(&one);
    net.TakeBandwidth(figures);
    // (Player 2, connection 20, the last: its DirectPlay ping; the sums 2000 + 20, 20000 + 40 against the client's
    // last 505, 5010.)
    CHECK(figures[0] == 200 && figures[1] == 2020 - 505 && figures[2] == 20040 - 5010);
    net.TakeBandwidth(figures);
    CHECK(figures[0] == 200 && figures[1] == 0 && figures[2] == 0);
    net.m_players[2].connection = 0;
    net.TakeBandwidth(figures);
    CHECK(figures[0] == 77 && figures[1] == 1010 - 2020 && figures[2] == 10020 - 20040);
    net.m_transport->m_connections.clear();
    net.m_server = nullptr;
    net.m_client = nullptr;
    net.m_players.clear();
    g_gameRole = GAME_ROLE_LOCAL;
}

void TestStatusMessage()
{
    // A status in a message and back: the players (stats, team, name), the rules (+0x40 not sent; the 1-bit flags),
    // the scores (10 bits).
    S_ServerStatus sent;
    sent.players.resize(2);
    sent.players[0].kills = 4;
    sent.players[0].points = -2;
    sent.players[0].team = 2;
    std::strcpy(sent.players[0].name, "alpha");
    std::strcpy(sent.players[1].name, "beta");
    int *rules[] = {&sent.maxClients, &sent.fragLimit, &sent.roundLimit, &sent.roundCount, &sent.preparationTime,
                    &sent.respawnTime, &sent.difficulty, &sent.lives, &sent.inverseDamage};
    int value = 1;
    for (int *rule : rules)
        *rule = value++ * 3;
    sent.lives = -1;
    sent.fragLimit = -20;
    sent.allowRespawn = 1;
    sent.friendlyFire = 0;
    sent.autoTeamBalance = 1;
    sent.thirdPersonView = 1;
    sent.spawnProtectionTime = 1;
    sent.fallingDamage = 1;
    sent.allowVehicles = 1;
    sent.allowCrosshair = 0;
    sent.spawnOnStart = 1;
    sent.teamLives = 1;
    sent.unknown40 = 77;
    sent.axisScore = 1023;
    sent.alliedScore = 5;
    C_GrowingBitStream w;
    sent.Write(&w);
    // (32 + 2 x (46 + 32 + names) + rules 8 + 5 x 16 + 7 + 8 + 16 + 3 + 16 + 20.)
    const uint32_t names = (6 + 5) * 8;
    CHECK(w.BitPosition() == 32 + 2 * (46 + 32) + names + 8 + 80 + 7 + 8 + 16 + 3 + 16 + 20);
    w.ResetPosition();
    S_ServerStatus got = S_ServerStatus();
    got.players.resize(5);
    got.unknown40 = 55;
    got.Read(&w);
    CHECK(got.players.size() == 2 && got.players[0].kills == 4 && got.players[0].points == -2 && got.players[0].team == 2 &&
          std::strcmp(got.players[0].name, "alpha") == 0 && std::strcmp(got.players[1].name, "beta") == 0);
    CHECK(got.maxClients == 3 && got.fragLimit == -20 && got.roundLimit == 9 && got.roundCount == 12 &&
          got.preparationTime == 15 && got.respawnTime == 18 && got.difficulty == 21 && got.lives == -1 &&
          got.inverseDamage == 27 && got.allowRespawn == 1 && got.friendlyFire == 0 && got.autoTeamBalance == 1 &&
          got.thirdPersonView == 1 && got.spawnProtectionTime == 1 && got.fallingDamage == 1 && got.allowVehicles == 1 &&
          got.allowCrosshair == 0 && got.spawnOnStart == 1 && got.teamLives == 1 && got.unknown40 == 55 &&
          got.axisScore == 1023 && got.alliedScore == 5);
    // None: an empty list.
    S_ServerStatus none;
    C_GrowingBitStream e;
    none.Write(&e);
    e.ResetPosition();
    got.Read(&e);
    CHECK(got.players.empty());
}

void RunNetSessionTests()
{
    g_netGame.m_playersLock.Init();
    g_netGame.m_queueLock.Init();
    g_graphVtable[18] = reinterpret_cast<void *>(&FakeGetHWND);
    g_graphVtable[55] = reinterpret_cast<void *>(&FakeUnknownDC);
    g_gameProfile.m_mpSettings = &g_testMp;
    std::printf("> NetText\n");
    TestNetText();
    std::printf("> Host\n");
    TestHost();
    std::printf("> HostDedicated\n");
    TestHostDedicated();
    std::printf("> ClientObjects\n");
    TestClientObjects();
    std::printf("> EnumHosts\n");
    TestEnumHosts();
    std::printf("> Connect\n");
    TestConnect();
    std::printf("> ClientMessages\n");
    TestClientMessages();
    std::printf("> Voice\n");
    TestVoice();
    std::printf("> Bandwidth\n");
    TestBandwidth();
    std::printf("> StatusMessage\n");
    TestStatusMessage();
}
