// Net part 11b: the network game's DirectPlay session (net/net_session.cpp: the text conversions, the host's
// address, hosting, the dedicated server's process, the client's object and addresses, the hosts' enumeration
// (pings, status queries), connecting, the client's DirectPlay messages, the replication made, the voice chat, the
// bandwidth, a server's status in a message) against the original executable's. MinGW; build_sess.bat; runs
// sess_diff.exe ("sess_diff: N checks, 0 failures").
//
// The original's net game is its own global made by its constructor and Init (statics never run), the port's the
// real g_netGame (net_game.cpp linked too: Close, QueueOutgoing). Each round sets both alike (the role, the local
// slot, the players, the rules, the profile, DirectPlay's objects, the voices, the replication) and runs up to 8
// calls. What they reach is the test's on both sides, logging alike: COM (CoInitialize, CoCreateInstance: the test's
// DirectPlay server, client and addresses, one COM object each, their calls' arguments logged and their results
// scripted), the voice's objects (Init; the voice client's targets and 3D buffers), the replication (its
// constructor, quantisations, local object, lag, deleting destructor), the transport (Start, Add, Remove, FlushAll,
// FindById, Lock, Unlock; the connections made and deleted; the socket's counters), the key query, GameSpy's
// reporting, the firewall, IGraph (SetFlags, InitMouse, GetHWND), ShowWindow, the dedicated server's process
// (GetStartupInfo, CreateProcess, the settings' file), the app's lines and pending state, the game's lines, the
// humans' teams, the actors (GetActor), the stats' bits, HasCjkCharacters, the server entries' reset. After each call
// the result, the logs and the state are compared. HD2_SESS_ROUNDS: rounds (default 20,000; HD2_SESS_QUICK: 5,000);
// HD2_SESS_SEED; HD2_SESS_FIRST_CHANCE.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <malloc.h>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/console.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_state.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/module.h"
#include "game/pending.h"
#include "game/perf_timer.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "net/dplay8.h"
#include "net/net_channel.h"
#include "net/net_game.h"
#include "net/net_object.h"
#include "net/net_socket.h"
#include "net/net_transport.h"
#undef private
#undef protected

#include "original_exe.h"

// (The renames of sess_fakes.h undone here: the fakes below call the real functions by these names.)
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef InitializeCriticalSection
#undef DeleteCriticalSection
#undef timeGetTime
#undef closesocket
#undef WaitForSingleObject
#undef CloseHandle
#undef clock
#undef CoInitialize
#undef CoInitializeEx
#undef ShowWindow
#undef GetStartupInfoA
#undef CreateProcessA

#ifdef SendMessage
#undef SendMessage
#endif
#ifdef DispatchMessage
#undef DispatchMessage
#endif

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

static_assert(sizeof(S_NetPlayerState) == 0xC4, "the player state's size");
static_assert(sizeof(S_StatusPlayer) == 0x5C, "the status player's size");

// ---- the static objects the port deletes (never freed) ----
constexpr int LINKS = 14;
alignas(16) unsigned char g_pLinks[LINKS][sizeof(C_NetConnection)];
alignas(16) unsigned char g_pReplication[sizeof(C_NetReplication)];
alignas(16) unsigned char g_pVoices[2][sizeof(C_NetVoice)];
bool InStatic(const void *p)
{
    const auto *b = static_cast<const unsigned char *>(p);
    return (b >= g_pLinks[0] && b < g_pLinks[0] + sizeof g_pLinks) || (b >= g_pReplication && b < g_pReplication + sizeof g_pReplication) ||
           (b >= g_pVoices[0] && b < g_pVoices[0] + sizeof g_pVoices);
}

// The port's allocations zeroed, as the original's (the test's calloc).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept
{
    if (!InStatic(p))
        std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    if (!InStatic(p))
        std::free(p);
}
void *operator new(size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, static_cast<size_t>(a));
    std::memset(p, 0, n);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept
{
    if (!InStatic(p))
        _aligned_free(p);
}
void operator delete(void *p, size_t, std::align_val_t) noexcept
{
    if (!InStatic(p))
        _aligned_free(p);
}

// ---- the logs (both sides) ----
int g_side;
bool g_faking;
std::vector<std::string> g_log[2];
void Log(const std::string &s)
{
    if (g_faking)
        g_log[g_side].push_back(s);
}
std::string N(long long v) { return std::to_string(v); }
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }
std::string WStr(const wchar_t *s)
{
    if (!s)
        return "null";
    std::string out = "L\"";
    for (; *s; ++s) {
        char b[8];
        if (*s >= 32 && *s < 127)
            out += static_cast<char>(*s);
        else {
            std::snprintf(b, sizeof b, "\\%04x", static_cast<unsigned>(*s));
            out += b;
        }
    }
    return out + "\"";
}
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
std::string F(float f)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", static_cast<double>(f));
    return b;
}
std::string VFormat(const char *format, va_list args)
{
    char line[1024];
    std::vsnprintf(line, sizeof line, format, args);
    return line;
}
std::string Bytes(const void *data, int size)
{
    if (!data)
        return N(size) + ":null";
    std::string s = N(size) + ":";
    const auto *b = static_cast<const uint8_t *>(data);
    for (int i = 0; i < size && i < 200; ++i) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}
std::string Guid(const GUID *g)
{
    if (!g)
        return "null";
    const struct {
        const GUID *guid;
        const char *name;
    } known[] = {{&CLSID_NetDirectPlay8Server, "server"},   {&IID_NetDirectPlay8Server, "IServer"},
                 {&CLSID_NetDirectPlay8Client, "client"},   {&IID_NetDirectPlay8Client, "IClient"},
                 {&CLSID_NetDirectPlay8Address, "address"}, {&IID_NetDirectPlay8Address, "IAddress"},
                 {&CLSID_NetDP8SP_TCPIP, "TCPIP"},          {&g_netApplicationGuid, "application"}};
    for (const auto &k : known)
        if (std::memcmp(g, k.guid, sizeof(GUID)) == 0)
            return k.name;
    for (int i = 0; i < 8; ++i)
        if (std::memcmp(g, &g_netVoiceCodecs[i], sizeof(GUID)) == 0)
            return "codec" + N(i);
    return Bytes(g, 16);
}

// ---- the round's script ----
constexpr int HUMANS = 8;
constexpr int ADDRESSES = 6;
struct S_Script {
    bool game = true;
    uint32_t now = 0;                // timeGetTime
    int64_t perfNow = 0;             // GetPerfTimeMs
    int linkById = -1;               // FindById: the connection (-1: none) for any id
    bool voiceChat = false;
    // COM: CoCreateInstance's results by class (server, client, address), the objects' calls' results.
    long createServer = 0, createClient = 0, createAddress = 0;
    long serverInit = 0, enumProviders = 0, setSP = 0, host = 0;
    long clientInit = 0, enumHosts = 0, setClientInfo = 0, connect = 0;
    bool noDevices = false;          // EnumServiceProviders hands no buffer size
    std::wstring hostname;           // an address's "hostname"
    int teams[HUMANS];               // the humans' GetTeam
    bool spectating[HUMANS];
    int keyStatus = 0;               // RunKeyQuery: the join status it leaves
    bool createProcess = true;
    bool cjk = false;                // HasCjkCharacters
    bool voiceBuffer = true;         // Create3DSoundBuffer hands a buffer
    uint32_t info[0x17];             // GetConnectionInfo
    int counters[2];                 // the socket's TakeCounters
    int actorType = -1;              // GetActor (-1: none)
    bool failWithObject = false;     // a failed CoCreateInstance hands its object all the same
} g_script;
int g_linkPort[LINKS], g_linkRoundTrip[LINKS], g_linkLastReceive[LINKS];
int g_addressNext;                   // the next address object CoCreateInstance hands

// ---- the fakes shared by both sides (C calls, COM) ----
CRITICAL_SECTION *g_watched[2][2];   // [side][0 players, 1 queue]
std::string Watched(void *cs)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 2; ++k)
            if (cs == g_watched[s][k])
                return k ? "queue" : "players";
    return "";
}
extern "C" void WINAPI FakeEnterCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Enter " + w);
    else
        ::EnterCriticalSection(section);
}
extern "C" void WINAPI FakeLeaveCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Leave " + w);
    else
        ::LeaveCriticalSection(section);
}
extern "C" void WINAPI FakeInitializeCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Initialize " + w);
    else
        ::InitializeCriticalSection(section);
}
extern "C" void WINAPI FakeDeleteCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Delete " + w);
    else
        ::DeleteCriticalSection(section);
}
extern "C" DWORD WINAPI FakeTimeGetTime()
{
    Log("timeGetTime");
    return g_script.now;
}
extern "C" int WSAAPI FakeClosesocket(SOCKET s)
{
    Log("closesocket " + N(static_cast<long long>(s)));
    return 0;
}
HANDLE g_queryThread = reinterpret_cast<HANDLE>(0x7777);
extern "C" DWORD WINAPI FakeWaitForSingleObject(HANDLE handle, DWORD ms)
{
    if (handle != g_queryThread)
        return ::WaitForSingleObject(handle, ms);
    Log("WaitForSingleObject " + N(ms));
    return 0;
}
extern "C" BOOL WINAPI FakeCloseHandle(HANDLE handle)
{
    if (handle != g_queryThread)
        return ::CloseHandle(handle);
    Log("CloseHandle");
    return TRUE;
}
extern "C" clock_t FakeClock()
{
    Log("clock");
    return 0;
}
extern "C" HRESULT WINAPI FakeCoInitialize(LPVOID reserved)
{
    Log("CoInitialize " + N(reserved != nullptr));
    return 0;
}
extern "C" HRESULT WINAPI FakeCoInitializeEx(LPVOID reserved, DWORD flags)
{
    Log("CoInitializeEx " + N(reserved != nullptr) + " " + Hex(flags));
    return 0;
}
const HWND g_window = reinterpret_cast<HWND>(0x4321);
extern "C" BOOL WINAPI FakeShowWindow(HWND window, int show)
{
    Log("ShowWindow " + N(window == g_window) + " " + N(show));
    return TRUE;
}
extern "C" void WINAPI FakeGetStartupInfoA(LPSTARTUPINFOA info)
{
    if (!g_faking)
        return ::GetStartupInfoA(info);   // (the harness's own: original_exe.cpp's relaunch)
    Log("GetStartupInfoA");
    std::memset(info, 0, sizeof *info);
    info->cb = sizeof *info;
}
extern "C" BOOL WINAPI FakeCreateProcessA(LPCSTR application, LPSTR command, LPSECURITY_ATTRIBUTES process, LPSECURITY_ATTRIBUTES thread,
                                          BOOL inherit, DWORD flags, LPVOID environment, LPCSTR directory, LPSTARTUPINFOA startup,
                                          LPPROCESS_INFORMATION info)
{
    if (!g_faking)
        return ::CreateProcessA(application, command, process, thread, inherit, flags, environment, directory, startup, info);
    Log("CreateProcessA " + Str(application) + " " + Str(command) + " " + N(process != nullptr) + " " + N(thread != nullptr) + " " +
        N(inherit) + " " + Hex(flags) + " " + N(environment != nullptr) + " " + Str(directory) + " " + N(startup && startup->cb == sizeof *startup) +
        " " + N(info != nullptr));
    return g_script.createProcess;
}

// DirectPlay's server, client and addresses (one COM object each, both sides': stdcall, `this` first).
void *g_serverVtable[64], *g_clientVtable[64], *g_addressVtable[64];
void *g_serverObject = g_serverVtable, *g_clientObject = g_clientVtable;
void *g_addressObjects[ADDRESSES];
int AddressIndex(const void *p)
{
    for (int i = 0; i < ADDRESSES; ++i)
        if (p == &g_addressObjects[i])
            return i;
    return p ? 99 : -1;
}
std::string Who(const void *p)
{
    if (p == &g_serverObject)
        return "server";
    if (p == &g_clientObject)
        return "client";
    const int a = AddressIndex(p);
    return a < 0 ? "null" : "address" + N(a);
}
// Each side's message handlers by name.
std::string HandlerName(const void *p);
std::string Caps(const void *c)
{
    const auto *d = static_cast<const DWORD *>(c);
    return d ? N(d[0]) + "," + N(d[1]) + "," + N(d[2]) + "," + N(static_cast<int>(d[3])) + "," + N(d[4]) : "null";
}
std::string AppDesc(const DPN_APPLICATION_DESC *a)
{
    if (!a)
        return "null";
    return Hex(a->size) + " " + Hex(a->flags) + " " + Guid(&a->instance) + " " + Guid(&a->application) + " " + N(a->maxPlayers) + " " +
           N(a->currentPlayers) + " " + WStr(a->sessionName) + " " + N(a->password != nullptr) + " " + N(a->reservedData != nullptr) + " " +
           N(a->reservedSize) + " " + N(a->applicationData != nullptr) + " " + N(a->applicationSize);
}
ULONG __stdcall FakeRelease(void *self)
{
    Log(Who(self) + " Release");
    return 0;
}
HRESULT __stdcall FakeServerInitialize(void *, void *context, void *handler, DWORD flags)
{
    Log("server Initialize " + N(context != nullptr) + " " + HandlerName(handler) + " " + Hex(flags));
    return g_script.serverInit;
}
HRESULT __stdcall FakeEnumServiceProviders(void *, const GUID *sp, const GUID *application, void *buffer, DWORD *size, DWORD *count, DWORD flags)
{
    Log("EnumServiceProviders " + Guid(sp) + " " + Guid(application) + " " + N(buffer != nullptr) + " " + N(*size) + " " + N(*count) + " " +
        Hex(flags));
    if (!buffer) {
        *size = g_script.noDevices ? 0 : 0x30;
        *count = g_script.noDevices ? 0 : 1;
        return static_cast<HRESULT>(0x80158060);   // DPNERR_BUFFERTOOSMALL
    }
    if (*size < 0x14)
        return static_cast<HRESULT>(0x80004005);   // (no device: the original would read its GUID past the buffer)
    auto *b = static_cast<uint8_t *>(buffer);
    for (int i = 0; i < 16; ++i)
        b[4 + i] = static_cast<uint8_t>(0xA0 + i);
    return g_script.enumProviders;
}
HRESULT __stdcall FakeServerCancel(void *, DWORD handle, DWORD flags)
{
    Log("server Cancel " + N(handle) + " " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeHost(void *, const DPN_APPLICATION_DESC *desc, void **addresses, DWORD count, const void *security, const void *credentials,
                           void *context, DWORD flags)
{
    // (The port's Fix: DPNSVR not used, the flag 0x40 added.)
    DPN_APPLICATION_DESC d = *desc;
    if (g_side == 0)
        d.flags &= ~0x40u;
    Log("Host " + AppDesc(&d) + " " + Who(addresses ? addresses[0] : nullptr) + " " + N(count) + " " + N(security != nullptr) + " " +
        N(credentials != nullptr) + " " + N(context != nullptr) + " " + Hex(flags));
    return g_script.host;
}
HRESULT __stdcall FakeServerClose(void *, DWORD flags)
{
    Log("server Close " + N(flags));
    return 0;
}
HRESULT __stdcall FakeDestroyClient(void *, DWORD player, const void *data, DWORD size, DWORD flags)
{
    Log("DestroyClient " + N(player) + " " + N(data != nullptr) + " " + N(size) + " " + N(flags));
    return 0;
}
HRESULT __stdcall FakeServerSetCaps(void *, const void *caps, DWORD flags)
{
    Log("server SetCaps " + Caps(caps) + " " + Hex(flags));
    return 0;
}
void FillInfo(void *info, DWORD player)
{
    auto *d = static_cast<uint32_t *>(info);
    const uint32_t size = d[0];
    for (int i = 1; i < 0x17; ++i)
        d[i] = g_script.info[i] + player * 3;
    d[0] = size;
}
HRESULT __stdcall FakeServerConnectionInfo(void *, DWORD player, void *info, DWORD flags)
{
    Log("server GetConnectionInfo " + N(player) + " " + N(*static_cast<DWORD *>(info)) + " " + Hex(flags));
    FillInfo(info, player);
    return 0;
}
HRESULT __stdcall FakeClientInitialize(void *, void *context, void *handler, DWORD flags)
{
    Log("client Initialize " + N(context != nullptr) + " " + HandlerName(handler) + " " + Hex(flags));
    return g_script.clientInit;
}
std::string ServerEntryText(const S_ServerEntry *e);
HRESULT __stdcall FakeEnumHosts(void *, const DPN_APPLICATION_DESC *desc, void *host, void *device, const void *data, DWORD size, DWORD count,
                                DWORD retry, DWORD timeout, void *context, DPNHANDLE *handle, DWORD flags)
{
    Log("EnumHosts " + Guid(&desc->application) + " " + Who(host) + " " + Who(device) + " " + Bytes(data, static_cast<int>(size)) + " " +
        N(static_cast<int>(count)) + " " + N(static_cast<int>(retry)) + " " + N(static_cast<int>(timeout)) + " " +
        ServerEntryText(static_cast<const S_ServerEntry *>(context)) + " " + N(handle != nullptr) + " " + Hex(flags));
    if (handle)
        *handle = 0x5150;
    return g_script.enumHosts;
}
HRESULT __stdcall FakeClientCancel(void *, DWORD handle, DWORD flags)
{
    Log("client Cancel " + N(handle) + " " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeConnect(void *, const DPN_APPLICATION_DESC *desc, void *host, void *device, const void *security, const void *credentials,
                              const void *data, DWORD size, void *context, DPNHANDLE *handle, DWORD flags)
{
    std::string request = "null";
    if (data && size == sizeof(S_JoinRequest)) {
        const auto *r = static_cast<const S_JoinRequest *>(data);
        request = Str(r->password) + "," + N(r->versionMajor) + "," + N(r->versionMinor) + "," + N(r->unknown1C) + "," + Str(r->name);
    }
    Log("Connect " + Guid(&desc->application) + " " + Who(host) + " " + Who(device) + " " + N(security != nullptr) + " " +
        N(credentials != nullptr) + " " + request + " " + N(size) + " " + N(context != nullptr) + " " + N(handle != nullptr) + " " + Hex(flags));
    if (handle)
        *handle = 0x6160;
    return g_script.connect;
}
HRESULT __stdcall FakeSetClientInfo(void *, const DPN_PLAYER_INFO *info, void *context, DPNHANDLE *handle, DWORD flags)
{
    Log("SetClientInfo " + N(info->size) + " " + N(info->infoFlags) + " " + WStr(info->name) + " " + N(info->data != nullptr) + " " +
        N(info->dataSize) + " " + N(info->playerFlags) + " " + N(context != nullptr) + " " + N(handle != nullptr) + " " + Hex(flags));
    return g_script.setClientInfo;
}
HRESULT __stdcall FakeClientClose(void *, DWORD flags)
{
    Log("client Close " + N(flags));
    return 0;
}
HRESULT __stdcall FakeClientSetCaps(void *, const void *caps, DWORD flags)
{
    Log("client SetCaps " + Caps(caps) + " " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeClientConnectionInfo(void *, void *info, DWORD flags)
{
    Log("client GetConnectionInfo " + N(*static_cast<DWORD *>(info)) + " " + Hex(flags));
    FillInfo(info, 0);
    return 0;
}
HRESULT __stdcall FakeSetSP(void *self, const GUID *sp)
{
    Log(Who(self) + " SetSP " + Guid(sp));
    return g_script.setSP;
}
HRESULT __stdcall FakeSetDevice(void *self, const GUID *device)
{
    Log(Who(self) + " SetDevice " + Guid(device));
    return 0;
}
HRESULT __stdcall FakeAddComponent(void *self, const wchar_t *name, const void *data, DWORD size, DWORD type)
{
    const std::string value = type == 1 ? WStr(static_cast<const wchar_t *>(data)) : type == 2 ? N(*static_cast<const DWORD *>(data)) : Bytes(data, 4);
    Log(Who(self) + " AddComponent " + WStr(name) + " " + value + " " + N(size) + " " + N(type));
    return 0;
}
HRESULT __stdcall FakeGetComponentByName(void *self, const wchar_t *name, void *buffer, DWORD *size, DWORD *type)
{
    Log(Who(self) + " GetComponentByName " + WStr(name) + " " + N(*size));   // (*type: the original's stack's)
    const size_t n = std::min<size_t>(g_script.hostname.size(), *size - 1);
    std::memcpy(buffer, g_script.hostname.c_str(), n * 2);
    static_cast<wchar_t *>(buffer)[n] = 0;
    *size = static_cast<DWORD>(n + 1);
    *type = 1;
    return 0;
}
HRESULT WINAPI CreateInstance(const GUID &clsid, IUnknown *outer, DWORD context, const GUID &iid, void **object)
{
    Log("CoCreateInstance " + Guid(&clsid) + " " + N(outer != nullptr) + " " + Hex(context) + " " + Guid(&iid));
    if (std::memcmp(&clsid, &CLSID_NetDirectPlay8Server, sizeof(GUID)) == 0) {
        *object = g_script.createServer < 0 && !g_script.failWithObject ? nullptr : &g_serverObject;
        return g_script.createServer;
    }
    if (std::memcmp(&clsid, &CLSID_NetDirectPlay8Client, sizeof(GUID)) == 0) {
        *object = g_script.createClient < 0 && !g_script.failWithObject ? nullptr : &g_clientObject;
        return g_script.createClient;
    }
    *object = g_script.createAddress < 0 && !g_script.failWithObject ? nullptr : &g_addressObjects[g_addressNext++ % ADDRESSES];
    return g_script.createAddress;
}
// (Each side's addresses handed from the same first one.)
int g_addressFirst;
extern "C" HRESULT WINAPI FakeCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID *object)
{
    return CreateInstance(clsid, outer, context, iid, object);
}
HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object)
{
    return CreateInstance(clsid, nullptr, context, iid, object);
}
// The voice client (DirectPlay Voice's: SetTransmitTargets, Create3DSoundBuffer) and its 3D buffers.
void *g_voiceClientVtable[32], *g_bufferVtable[32];
void *g_voiceClientObject = g_voiceClientVtable;
void *g_bufferObject = g_bufferVtable;
HRESULT __stdcall FakeSetTransmitTargets(void *, const DWORD *targets, DWORD count, DWORD flags)
{
    std::string s;
    for (DWORD i = 0; targets && i < count; ++i)
        s += N(targets[i]) + ",";
    Log("SetTransmitTargets " + N(targets != nullptr) + " [" + s + "] " + N(count) + " " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeCreate3DSoundBuffer(void *, DWORD id, void *buffer, DWORD priority, DWORD flags, void **out)
{
    Log("Create3DSoundBuffer " + N(id) + " " + N(buffer != nullptr) + " " + N(priority) + " " + Hex(flags));
    *out = g_script.voiceBuffer ? &g_bufferObject : nullptr;
    return 0;
}
HRESULT __stdcall FakeSetMinDistance(void *, float d, DWORD apply)
{
    Log("SetMinDistance " + F(d) + " " + N(apply));
    return 0;
}
HRESULT __stdcall FakeSetMaxDistance(void *, float d, DWORD apply)
{
    Log("SetMaxDistance " + F(d) + " " + N(apply));
    return 0;
}
// IGraph (SetFlags +0x08, GetHWND +0x48, InitMouse +0xDC).
void __stdcall FakeSetFlags(void *, uint32_t flags) { Log("IGraph SetFlags " + Hex(flags)); }
HWND __stdcall FakeGetHWND(void *)
{
    Log("IGraph GetHWND");
    return g_window;
}
bool __stdcall FakeInitMouse(void *, void *window)
{
    Log("IGraph InitMouse " + N(window != nullptr));
    return true;
}
void *g_graphVtable[128];
void *g_graphObject = g_graphVtable;
// The search's and the status query's callbacks (fastcall; each side's entry and status).
std::string StatusText(const void *status);
void __fastcall SearchCallback(S_ServerEntry *entry);
void __fastcall StatusCallback(S_ServerStatus *status) { Log("status " + StatusText(status)); }

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_localPlayerSlot;
char g_votableCommands[4][0x204] = {"maxclients", "kick", "map", "timelimit"};
void *g_netQueryThread;
char g_netDefaultSessionText[] = "session";
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;
unsigned g_graphFlags;
int g_netMaxFreq, g_netMaxPing, g_netMaxInactivity;
int g_versionMajor = 1, g_versionMinor = 3;
bool g_netServerSide;

C_NetGame &PNet() { return g_netGame; }

// The transport (the port's own object: its constructor's members; its calls logged).
C_NetSocket::C_NetSocket() = default;
C_NetSocket::~C_NetSocket() = default;
C_NetTransport::C_NetTransport() = default;
C_NetTransport::~C_NetTransport() = default;
int PLinkIndex(const void *p)
{
    for (int i = 0; i < LINKS; ++i)
        if (p == g_pLinks[i])
            return i;
    return p ? 99 : -1;
}
void C_NetTransport::Stop() { Log("Transport Stop"); }
void C_NetTransport::DeleteConnections() { Log("Transport DeleteConnections"); }
void C_NetTransport::Start(int port) { Log("Transport Start " + N(port)); }
void C_NetTransport::Add(C_NetConnection *connection) { Log("Transport Add " + N(PLinkIndex(connection))); }
void C_NetTransport::Remove(C_NetConnection *connection) { Log("Transport Remove " + N(PLinkIndex(connection))); }
C_NetConnection *C_NetTransport::FindById(int id)
{
    Log("FindById " + N(id));
    return g_script.linkById < 0 ? nullptr : reinterpret_cast<C_NetConnection *>(g_pLinks[g_script.linkById]);
}
C_NetTransport *C_NetTransport::Lock()
{
    Log("Transport Lock");
    return this;
}
void C_NetTransport::Unlock() { Log("Transport Unlock"); }
void C_NetTransport::FlushAll(bool force) { Log("FlushAll " + N(force)); }
void C_NetSocket::TakeCounters(int *received, int *sent)
{
    Log("TakeCounters");
    *received = g_script.counters[0];
    *sent = g_script.counters[1];
}
C_NetConnection::C_NetConnection(int id, uint32_t address, C_NetSocket *socket)
{
    Log("C_NetConnection " + N(id) + " " + Hex(address) + " " + N(socket == &PNet().m_transport->m_socket));
    m_id = id;
}
C_NetConnection::~C_NetConnection() { Log("~C_NetConnection " + N(PLinkIndex(this))); }
C_NetPacket *C_NetConnection::Send(const void *data, int size, int kind, int channel)
{
    Log("Send " + N(PLinkIndex(this)) + " " + Bytes(data, size) + " " + N(kind) + " " + N(channel));
    return nullptr;
}
void C_NetConnection::SetResendTime(int ms) { Log("SetResendTime " + N(PLinkIndex(this)) + " " + N(ms)); }
// The pool (its buffers the test's: logged by size).
void *C_NetPool::AllocBuffer(size_t size)
{
    Log("AllocBuffer " + N(static_cast<long long>(size)));
    return std::calloc(size + 4, 1);
}
void C_NetPool::FreeBuffer(void *buffer, int size)
{
    Log("FreeBuffer " + N(size));
    std::free(buffer);
}
C_NetPool g_netPool;
C_NetPool::C_NetPool() = default;
C_NetPool::~C_NetPool() = default;
// The clocks.
int64_t GetPerfTimeMs()
{
    Log("GetPerfTimeMs");
    return g_script.perfNow;
}
void InitPerfTime() { Log("InitPerfTime"); }
S_PerfTimer::S_PerfTimer(int periodMs) : period(periodMs) { Log("S_PerfTimer " + N(periodMs)); }
bool S_PerfTimer::Fire()
{
    Log("Fire " + N(running));
    return false;
}
// The replication (the port's: the test's virtuals and members; the static one's and those made).
C_NetReplication *g_netReplication;
std::string PReplicationName(const void *p) { return p == g_pReplication ? "static" : p ? "new" : "null"; }
std::string QuantName(const void *q);
C_NetReplication::C_NetReplication() { Log("C_NetReplication"); }
C_NetReplication::~C_NetReplication() { Log("~C_NetReplication " + PReplicationName(this)); }
void C_NetReplication::SetReadQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    Log("Replication SetReadQuantizations " + QuantName(value) + " " + QuantName(delta) + " " + QuantName(rate));
}
void C_NetReplication::SetWriteQuantizations(S_NetQuantization *value, S_NetQuantization *delta, S_NetQuantization *rate)
{
    Log("Replication SetWriteQuantizations " + QuantName(value) + " " + QuantName(delta) + " " + QuantName(rate));
}
void C_NetReplication::Update(float) { Log("Replication Update"); }
void C_NetReplication::Refresh() { Log("Replication Refresh"); }
bool C_NetReplication::Write(C_NetMessage *, int player, bool fromClient)
{
    Log("Replication Write " + N(player) + " " + N(fromClient));
    return false;
}
void C_NetReplication::Read(C_NetMessage *, uint32_t players, bool fromClient) { Log("Replication Read " + Hex(players) + " " + N(fromClient)); }
void C_NetReplication::SetLag(float lag) { Log("Replication SetLag " + PReplicationName(this) + " " + F(lag)); }
C_NetObject *C_NetReplication::FindObject(int id, bool create)
{
    Log("FindObject " + N(id) + " " + N(create));
    return nullptr;
}
void C_NetReplication::DeleteObjects() { Log("DeleteObjects"); }
void C_NetQuantized::SetReadQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) {}
void C_NetQuantized::SetWriteQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) {}
C_NetObject::C_NetObject() = default;
C_NetObject::~C_NetObject() = default;
void C_NetObject::SetReadQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) {}
void C_NetObject::SetWriteQuantizations(S_NetQuantization *, S_NetQuantization *, S_NetQuantization *) {}
void C_NetObject::Update(float) {}
void C_NetObject::Refresh() {}
void C_NetObject::GetReadQuantizations(S_NetQuantization **, S_NetQuantization **, S_NetQuantization **) {}
void C_NetObject::GetWriteQuantizations(S_NetQuantization **, S_NetQuantization **, S_NetQuantization **) {}
bool C_NetObject::Write(C_NetMessage *, int, bool) { return false; }
void C_NetObject::Read(C_NetMessage *, int, bool) {}
void C_NetObject::SetPropertyField2(int value)
{
    const auto *at = reinterpret_cast<const unsigned char *>(this);
    Log("SetPropertyField2 " + PReplicationName(at - offsetof(C_NetReplication, m_local)) + " " + N(value));
}
void C_NetObject::SetLag(float lag) { Log("Object SetLag " + F(lag)); }
// The replication's factories' results (the port's functions: their names).
C_NetObjectMessage *__fastcall NetCreateMessage(void *, C_BitStream *, bool) { return nullptr; }
C_NetObjectMessage *__fastcall MakeSeatWeaponMessage(void *, C_BitStream *, bool) { return nullptr; }
void __fastcall CarNetReadState(void *, C_BitStream *) {}
void __fastcall NetReadControlBits(void *, C_BitStream *) {}
// The actors (GetActor: an actor of the script's type).
alignas(16) unsigned char g_pActor[sizeof(C_Actor)];
C_Actor *C_ActorsModule::GetActor(int index) const
{
    Log("GetActor " + N(index));
    if (g_script.actorType < 0)
        return nullptr;
    reinterpret_cast<C_Actor *>(g_pActor)->m_type = static_cast<E_ActorType>(g_script.actorType);
    return reinterpret_cast<C_Actor *>(g_pActor);
}
// The app, the game, the console, the texts, the profile.
alignas(16) unsigned char g_pApp[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_pApp); }
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(type == 9 ? 0 : a) + " " + N(b) + " " + N(reliable));
}
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
void C_GameApp::PrintMessage(const char *text) { Log("app PrintMessage " + Str(text)); }
void C_GameApp::HandleClientCommand(int id, C_BitStream *) { Log("HandleClientCommand " + N(id)); }
void C_GameApp::HandleServerCommand(int id, C_BitStream *) { Log("HandleServerCommand " + N(id)); }
alignas(16) unsigned char g_pState[0x100];
void *g_pStateVtable[0x40];
void __attribute__((thiscall)) PSetPendingState(void *, int state) { Log("SetPendingState " + N(state)); }
alignas(16) unsigned char g_pGame[sizeof(C_game)];
C_game *GetGame() { return g_script.game ? reinterpret_cast<C_game *>(g_pGame) : nullptr; }
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color.x) + " " + F(color.y) + " " + F(color.z) + " " + VFormat(format, args));
    va_end(args);
}
void C_game::Print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("Print " + VFormat(format, args));
    va_end(args);
}
namespace ModuleManager {
bool DispatchMessage(int moduleId, C_BitStream *)
{
    Log("DispatchMessage " + N(moduleId));
    return true;
}
bool CallUnknown28(int moduleId, C_BitStream *)
{
    Log("CallUnknown28 " + N(moduleId));
    return true;
}
}  // namespace ModuleManager
alignas(16) unsigned char g_pConsole[sizeof(C_Console)];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_pConsole); }
char *C_Console::PopCommand()
{
    Log("PopCommand");
    return nullptr;
}
alignas(16) unsigned char g_pProcessor[0x10];
C_CommandProcessor *GetCommandProcessor() { return reinterpret_cast<C_CommandProcessor *>(g_pProcessor); }
bool C_CommandProcessor::Execute(const char *command)
{
    Log("Execute " + Str(command));
    return true;
}
C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    char text[32];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
C_PlayerOptions g_options;
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return &g_options; }
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graphObject); }
void S_MultiplayerSettings::WriteDescription(const char *fileName) { Log("WriteDescription " + Str(fileName)); }
// The net game's other files' calls.
int C_NetGame::CountPlayers()
{
    Log("CountPlayers");
    return 0;
}
void C_NetGame::KickPlayer(int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void C_NetGame::SendPlayerScore(int slot, bool lock) { Log("SendPlayerScore " + N(slot) + " " + N(lock)); }
void C_NetGame::SetLevelBounds(const float *, const float *) { Log("SetLevelBounds"); }
void C_NetGame::AnswerQueries() { Log("AnswerQueries"); }
// (The port's low 16 bits: all the firewall reads; Connect's original pushes a register's high half with them.)
void C_NetGame::ConfigureFirewall(int port, bool open) { Log("ConfigureFirewall " + N(static_cast<int16_t>(port)) + " " + N(open)); }
void C_NetGame::StartReporting() { Log("StartReporting"); }
void C_NetGame::StartKeyQuery(const S_ServerEntry *server) { Log("StartKeyQuery " + ServerEntryText(server)); }
bool C_NetGame::RunKeyQuery()
{
    Log("RunKeyQuery");
    if (g_script.keyStatus >= 0)   // (-1: the check passed: the status left)
        m_joinStatus = g_script.keyStatus;
    return true;
}
long __stdcall C_NetGame::HandleServerMessage(C_NetGame *net, void *context, uint32_t type, void *message)
{
    Log("HandleServerMessage " + N(net == &g_netGame) + " " + N(context != nullptr) + " " + Hex(type) + " " + N(message != nullptr));
    return 7;
}
void StopGameSpyQueries(uintptr_t *socket) { Log("StopGameSpyQueries " + N(socket != nullptr)); }
void S_ServerEntry::Reset()
{
    Log("S_ServerEntry::Reset");
    std::memset(this, 0x5A, sizeof *this);
}
bool HasCjkCharacters(const uint16_t *text)
{
    Log("HasCjkCharacters " + WStr(reinterpret_cast<const wchar_t *>(text)));
    return g_script.cjk;
}
// The stats' bits (the test's on both sides: values 1..5, 8 bits each).
void S_NetStats::Write(C_BitStream *stream)
{
    Log("Stats Write");
    for (int i = 1; i <= 5; ++i)
        stream->WriteBits(static_cast<uint32_t>(values[i]), 8);
}
void S_NetStats::Read(C_BitStream *stream)
{
    Log("Stats Read");
    for (int i = 1; i <= 5; ++i)
        values[i] = static_cast<int>(stream->ReadBits(8));
}
// The voice (its objects the test's: Init, SetClientConfig and the deleting destructor logged).
std::string PVoiceName(const void *p) { return p == g_pVoices[0] ? "0" : p == g_pVoices[1] ? "1" : p ? "new" : "null"; }
long C_NetVoice::SetClientConfig(void *) { Log("SetClientConfig " + PVoiceName(this)); return 0; }
C_NetVoice::~C_NetVoice() { Log("~C_NetVoice " + PVoiceName(this)); }
C_NetVoice::C_NetVoice(NetVoiceHandler clientHandler, NetVoiceHandler serverHandler)
    : m_clientHandler(clientHandler), m_serverHandler(serverHandler)
{
    Log("C_NetVoice " + HandlerName(reinterpret_cast<const void *>(clientHandler)) + " " +
        HandlerName(reinterpret_cast<const void *>(serverHandler)));
}
std::string ConfigText(const void *config)
{
    if (!config)
        return "null";
    const auto *c = static_cast<const uint32_t *>(config);
    return Hex(c[0]) + "," + Hex(c[1]) + "," + Hex(c[2]) + "," + Hex(c[3]) + "," + Hex(c[4]) + "," + Hex(c[5]) + "," + Hex(c[6]) + "," + Hex(c[7]);
}
long C_NetVoice::Init(void *window, bool server, bool client, IUnknown *session, int sessionType, const _GUID *codec, void *clientConfig,
                      int unused)
{
    Log("Voice Init " + PVoiceName(this) + " " + N(window == g_window) + " " + N(server) + " " + N(client) + " " + Who(session) + " " +
        N(sessionType) + " " + Guid(reinterpret_cast<const GUID *>(codec)) + " " + ConfigText(clientConfig) + " " + N(unused));
    return 0;
}
// The unidentified object's Destroy (+0x0C).
void *g_pUnknownVtable[8];
alignas(16) unsigned char g_pUnknown[0x10];
void __attribute__((thiscall)) PDestroy(void *, bool free) { Log("Destroy " + N(free)); }
// The humans (their teams).
alignas(16) unsigned char g_pHumans[HUMANS][sizeof(C_Human)];
C_Human *PHuman(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_pHumans[i]); }
int PHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_pHumans[i])
            return i;
    return p ? 99 : -1;
}
int C_Human::GetTeam(bool pending) const
{
    const int i = PHumanIndex(this);
    Log("GetTeam " + N(i) + " " + N(pending));
    return g_script.teams[i];
}
// Their controllers' IsSpectating.
alignas(16) unsigned char g_pControllers[HUMANS][sizeof(C_PlayerController)];
void *g_pControllerVtable[0x80];
bool __attribute__((thiscall)) PIsSpectating(void *self)
{
    const int i = static_cast<int>((static_cast<unsigned char *>(self) - g_pControllers[0]) / sizeof(C_PlayerController));
    Log("IsSpectating " + N(i));
    return g_script.spectating[i];
}

namespace {

// ================================================================================================================
// The original's world.
// ================================================================================================================
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))

const uint32_t O_NET = 0x0097d070, O_ROLE = 0x00982100, O_LOCAL_SLOT = 0x00982104, O_PROFILE = 0x008aea0c;
const uint32_t O_REPLICATION = 0x0097d084, O_QUERY_THREAD = 0x009820f8, O_GRAPH_FLAGS = 0x00981e6c, O_SERVER_SIDE = 0x00982f94;
const uint32_t O_MAX_PING = 0x009820d4, O_MAX_FREQ = 0x009820d8, O_MAX_INACTIVITY = 0x009820dc;
const uint32_t O_VOTABLE = 0x00846088, O_FSTR = 0x00405100, O_VERSION_MAJOR = 0x00845070, O_VERSION_MINOR = 0x00845074;

volatile uintptr_t g_oNetAddress = O_NET;
uint8_t *ONet() { return reinterpret_cast<uint8_t *>(g_oNetAddress); }
uint8_t *OPlayers() { return OF<uint8_t *>(ONet(), 0x1E0); }
int OPlayerCount() { return OPlayers() ? static_cast<int>((OF<uint8_t *>(ONet(), 0x1E4) - OPlayers()) / 0xC4) : 0; }

alignas(16) uint8_t g_oGame[0x200], g_oApp[0x100], g_oState[0x40], g_oLinks[LINKS][0x80];
alignas(16) uint8_t g_oReplication[0x100], g_oVoices[2][0x20], g_oUnknown[0x10], g_oActor[0x40];
alignas(16) uint8_t g_oHumans[HUMANS][0x400], g_oControllers[HUMANS][0x400];
void *g_oControllerVtable[0x40];
alignas(16) uint8_t g_oProfile[0x40], g_oOptions[0x100];
void *g_oReplicationVtable[0x10], *g_oVoiceVtable[4], *g_oUnknownVtable[8], *g_oLinkVtable[4], *g_oStateVtable[0x20];
bool InOStatic(const void *p)
{
    const auto *b = static_cast<const uint8_t *>(p);
    return (b >= g_oLinks[0] && b < g_oLinks[0] + sizeof g_oLinks) || (b >= g_oReplication && b < g_oReplication + sizeof g_oReplication) ||
           (b >= g_oVoices[0] && b < g_oVoices[0] + sizeof g_oVoices);
}
int OLinkIndex(const void *p)
{
    for (int i = 0; i < LINKS; ++i)
        if (p == g_oLinks[i])
            return i;
    return p ? 99 : -1;
}
int OHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_oHumans[i])
            return i;
    return p ? 99 : -1;
}
std::string OReplicationName(const void *p) { return p == g_oReplication ? "static" : p ? "new" : "null"; }
std::string OVoiceName(const void *p) { return p == g_oVoices[0] ? "0" : p == g_oVoices[1] ? "1" : p ? "new" : "null"; }

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p)
{
    if (!InOStatic(p))
        std::free(p);
}
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
void *OAlignedMalloc(size_t n, size_t align)
{
    void *p = _aligned_malloc(n ? n : 1, align);
    std::memset(p, 0, n);
    return p;
}
void THISCALL ONoLock(void *) {}
int OAtexit(void *) { return 0; }

void FAST OTransportStop(void *) { Log("Transport Stop"); }
void FAST OTransportDeleteConnections(void *) { Log("Transport DeleteConnections"); }
void THISCALL OTransportStart(void *, int port) { Log("Transport Start " + N(port)); }
void THISCALL OTransportAdd(void *, void *connection) { Log("Transport Add " + N(OLinkIndex(connection))); }
void THISCALL OTransportRemove(void *, void *connection) { Log("Transport Remove " + N(OLinkIndex(connection))); }
void *THISCALL OFindById(void *, int id)
{
    Log("FindById " + N(id));
    return g_script.linkById < 0 ? nullptr : g_oLinks[g_script.linkById];
}
void *FAST OTransportLock(void *self)
{
    Log("Transport Lock");
    return self;
}
void FAST OTransportUnlock(void *) { Log("Transport Unlock"); }
void THISCALL OFlushAll(void *, bool force) { Log("FlushAll " + N(force)); }
void THISCALL OTakeCounters(void *, int *received, int *sent)
{
    Log("TakeCounters");
    *received = g_script.counters[0];
    *sent = g_script.counters[1];
}
void *THISCALL OConnectionCtor(void *self, int id, uint32_t address, void *socket)
{
    Log("C_NetConnection " + N(id) + " " + Hex(address) + " " + N(socket == ONet() + 0x34C));
    OF<void *>(self, 0) = g_oLinkVtable;
    OF<int>(self, 4) = id;
    return self;
}
void *THISCALL OConnectionDelete(void *self, unsigned flags)
{
    Log("~C_NetConnection " + N(OLinkIndex(self)));
    if (flags & 1)
        ODelete(self);
    return self;
}
void *THISCALL OSend(void *self, const void *data, int size, int kind, int channel)
{
    Log("Send " + N(OLinkIndex(self)) + " " + Bytes(data, size) + " " + N(kind) + " " + N(channel));
    return nullptr;
}
void THISCALL OSetResendTime(void *self, int ms) { Log("SetResendTime " + N(OLinkIndex(self)) + " " + N(ms)); }
void *THISCALL OAllocBuffer(void *, size_t size)
{
    Log("AllocBuffer " + N(static_cast<long long>(size)));
    return std::calloc(size + 4, 1);
}
void THISCALL OFreeBuffer(void *, void *buffer, int size)
{
    Log("FreeBuffer " + N(size));
    std::free(buffer);
}
long long OGetPerfTimeMs()
{
    Log("GetPerfTimeMs");
    return g_script.perfNow;
}
void OInitPerfTime() { Log("InitPerfTime"); }
void *THISCALL OPerfTimerCtor(void *self, int periodMs)
{
    Log("S_PerfTimer " + N(periodMs));
    OF<int>(self, 4) = periodMs;
    return self;
}
bool THISCALL OPerfTimerFire(void *self)
{
    Log("Fire " + N(OF<uint8_t>(self, 8)));
    return false;
}
// The replication.
void *THISCALL OReplicationCtor(void *self)
{
    Log("C_NetReplication");
    OF<void *>(self, 0) = g_oReplicationVtable;
    return self;
}
void *THISCALL OReplicationDelete(void *self, unsigned flags)
{
    Log("~C_NetReplication " + OReplicationName(self));
    if ((flags & 1) && !InOStatic(self))
        _aligned_free(self);
    return self;
}
void THISCALL OSetReadQuantizations(void *, void *value, void *delta, void *rate)
{
    Log("Replication SetReadQuantizations " + QuantName(value) + " " + QuantName(delta) + " " + QuantName(rate));
}
void THISCALL OSetWriteQuantizations(void *, void *value, void *delta, void *rate)
{
    Log("Replication SetWriteQuantizations " + QuantName(value) + " " + QuantName(delta) + " " + QuantName(rate));
}
void THISCALL OSetPropertyField2(void *self, int value)
{
    Log("SetPropertyField2 " + OReplicationName(static_cast<uint8_t *>(self) - 0x30) + " " + N(value));
}
void THISCALL OReplicationSetLag(void *self, float lag) { Log("Replication SetLag " + OReplicationName(self) + " " + F(lag)); }
bool THISCALL ORepWrite(void *, void *, int player, bool fromClient)
{
    Log("Replication Write " + N(player) + " " + N(fromClient));
    return false;
}
void THISCALL ORepRead(void *, void *, uint32_t players, bool fromClient) { Log("Replication Read " + Hex(players) + " " + N(fromClient)); }
void *THISCALL OFindObject(void *, int id, bool create)
{
    Log("FindObject " + N(id) + " " + N(create));
    return nullptr;
}
void FAST ODeleteObjects(void *) { Log("DeleteObjects"); }
void THISCALL OObjectSetLag(void *, float lag) { Log("Object SetLag " + F(lag)); }
bool THISCALL OIsSpectating(void *self)
{
    const int i = static_cast<int>((static_cast<uint8_t *>(self) - g_oControllers[0]) / 0x400);
    Log("IsSpectating " + N(i));
    return g_script.spectating[i];
}
void *THISCALL OGetActor(void *, int index)
{
    Log("GetActor " + N(index));
    if (g_script.actorType < 0)
        return nullptr;
    OF<int>(g_oActor, 0x18) = g_script.actorType;
    return g_oActor;
}
// The app, the game, the console.
void *OGetGameApp() { return g_oApp; }
void THISCALL OSendGameMessage(void *, int type, int to, intptr_t a, int b, bool reliable)
{
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(type == 9 ? 0 : a) + " " + N(b) + " " + N(reliable));
}
void THISCALL OPostCommand(void *, int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
void THISCALL OAppPrintMessage(void *, const char *text) { Log("app PrintMessage " + Str(text)); }
void THISCALL OSetPendingState(void *, int state) { Log("SetPendingState " + N(state)); }
void THISCALL OHandleClientCommand(void *, int id, void *) { Log("HandleClientCommand " + N(id)); }
void THISCALL OHandleServerCommand(void *, int id, void *) { Log("HandleServerCommand " + N(id)); }
void *OGetGame() { return g_script.game ? g_oGame : nullptr; }
void __cdecl OPrintMessage(void *, const float *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color[0]) + " " + F(color[1]) + " " + F(color[2]) + " " + VFormat(format, args));
    va_end(args);
}
void __cdecl OPrint(void *, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("Print " + VFormat(format, args));
    va_end(args);
}
bool FAST ODispatchMessage(int moduleId, void *)
{
    Log("DispatchMessage " + N(moduleId));
    return true;
}
bool FAST OCallUnknown28(int moduleId, void *)
{
    Log("CallUnknown28 " + N(moduleId));
    return true;
}
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    OF<void *>(ret, 0) = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(ret, "T%X", id);
    return ret;
}
int FAST OCountPlayers(void *)
{
    Log("CountPlayers");
    return 0;
}
void THISCALL OKickPlayer(void *, int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void THISCALL OSendPlayerScore(void *, int slot, bool lock) { Log("SendPlayerScore " + N(slot) + " " + N(lock)); }
void THISCALL OSetLevelBounds(void *, const float *, const float *) { Log("SetLevelBounds"); }
void FAST OAnswerQueries(void *) { Log("AnswerQueries"); }
void THISCALL OConfigureFirewall(void *, int port, bool open) { Log("ConfigureFirewall " + N(static_cast<int16_t>(port)) + " " + N(open)); }
void FAST OStartReporting(void *) { Log("StartReporting"); }
void THISCALL OStartKeyQuery(void *, const S_ServerEntry *server) { Log("StartKeyQuery " + ServerEntryText(server)); }
bool FAST ORunKeyQuery(void *self)
{
    Log("RunKeyQuery");
    if (g_script.keyStatus >= 0)
        OF<int>(self, 0x5044) = g_script.keyStatus;
    return true;
}
long __stdcall OHandleServerMessage(void *net, void *context, uint32_t type, void *message)
{
    Log("HandleServerMessage " + N(net == ONet()) + " " + N(context != nullptr) + " " + Hex(type) + " " + N(message != nullptr));
    return 7;
}
void FAST OStopGameSpyQueries(uintptr_t *socket) { Log("StopGameSpyQueries " + N(socket != nullptr)); }
void *FAST OServerEntryCtor(void *self)
{
    Log("S_ServerEntry::Reset");
    std::memset(self, 0x5A, sizeof(S_ServerEntry));
    return self;
}
bool FAST OHasCjkCharacters(const uint16_t *text)
{
    Log("HasCjkCharacters " + WStr(reinterpret_cast<const wchar_t *>(text)));
    return g_script.cjk;
}
const uint32_t O_READ_BITS = 0x0073d3d0, O_WRITE_BITS = 0x0073d320;
uint32_t ORead(void *stream, int bits) { return reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(O_READ_BITS)(stream, bits); }
void OWrite(void *stream, uint32_t value, int bits) { reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(O_WRITE_BITS)(stream, value, bits); }
void THISCALL OStatsWrite(void *self, void *stream)
{
    Log("Stats Write");
    for (int i = 1; i <= 5; ++i)
        OWrite(stream, static_cast<uint32_t>(OF<int>(self, 4 * i)), 8);
}
void THISCALL OStatsRead(void *self, void *stream)
{
    Log("Stats Read");
    for (int i = 1; i <= 5; ++i)
        OF<int>(self, 4 * i) = static_cast<int>(ORead(stream, 8));
}
void *OGetIGraph() { return &g_graphObject; }
void *THISCALL OGetDetailSettings(void *)
{
    g_oOptions[0x38] = g_script.voiceChat;
    return g_oOptions;
}
void THISCALL OWriteDescription(void *, const char *fileName) { Log("WriteDescription " + Str(fileName)); }
// The voice.
void *THISCALL OVoiceCtor(void *self, void *clientHandler, void *serverHandler)
{
    Log("C_NetVoice " + HandlerName(clientHandler) + " " + HandlerName(serverHandler));
    OF<void *>(self, 0) = g_oVoiceVtable;
    OF<void *>(self, 0xC) = clientHandler;
    OF<void *>(self, 0x10) = serverHandler;
    return self;
}
long THISCALL OVoiceInit(void *self, void *window, bool server, bool client, void *session, int sessionType, const GUID *codec, void *config,
                         int unused)
{
    Log("Voice Init " + OVoiceName(self) + " " + N(window == g_window) + " " + N(server) + " " + N(client) + " " + Who(session) + " " +
        N(sessionType) + " " + Guid(codec) + " " + ConfigText(config) + " " + N(unused));
    return 0;
}
long THISCALL OSetClientConfig(void *self, const uint32_t *) { Log("SetClientConfig " + OVoiceName(self)); return 0; }
void *THISCALL OVoiceDelete(void *self, unsigned flags)
{
    Log("~C_NetVoice " + OVoiceName(self));
    if (flags & 1)
        ODelete(self);
    return self;
}
void THISCALL ODestroy(void *, bool free) { Log("Destroy " + N(free)); }
int THISCALL OGetTeam(void *self, bool pending)
{
    const int i = OHumanIndex(self);
    Log("GetTeam " + N(i) + " " + N(pending));
    return g_script.teams[i];
}

// ---- checks ----
int checks, failures;
int g_round;
std::string g_line, g_trace;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, %s: %s\n   (ops:%s)\n", g_round, g_line.c_str(), what.c_str(), g_trace.c_str());
}
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok)
        Fail(what);
}

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
float R(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

void CompareLogs()
{
    if (g_log[0] == g_log[1]) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(g_log[0].size(), g_log[1].size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 500) + "\n     port: " + y.substr(0, 500);
            break;
        }
    }
    Check(false, s);
}

}   // namespace

// The handlers by name (each side's functions).
std::string HandlerName(const void *p)
{
    if (!p)
        return "null";
    const struct {
        const void *port;
        uint32_t orig;
        const char *name;
    } known[] = {{reinterpret_cast<const void *>(&ServerMessageHandler), 0x00726440, "ServerMessageHandler"},
                 {reinterpret_cast<const void *>(&ClientMessageHandler), 0x00722520, "ClientMessageHandler"},
                 {reinterpret_cast<const void *>(&VoiceServerHandler), 0x00726430, "VoiceServerHandler"},
                 {reinterpret_cast<const void *>(&VoiceClientHandler), 0x00722510, "VoiceClientHandler"}};
    for (const auto &k : known)
        if (p == (g_side ? reinterpret_cast<const void *>(static_cast<uintptr_t>(k.orig)) : k.port))
            return k.name;
    return "?";
}
// The net game's quantisations by index (each side's).
std::string QuantName(const void *q)
{
    const auto *b = static_cast<const uint8_t *>(q);
    for (int i = 0; i < 3; ++i)
        if (b == (g_side ? ONet() + 0x20 + 0x80 * i : reinterpret_cast<const uint8_t *>(&PNet().m_quantizations[i])))
            return "q" + N(i);
    return q ? "?" : "null";
}
std::string ServerEntryText(const S_ServerEntry *e)
{
    if (!e)
        return "null";
    // (Every byte: the name's after its terminator too.)
    std::string s = std::string(e->name, strnlen(e->name, sizeof e->name)) + "|" + std::string(e->map, strnlen(e->map, sizeof e->map)) + "|";
    return s + Bytes(e, 0x9C);
}
// The entries the search callback gets: logged, freed when made by the handler (not the test's).
S_ServerEntry *g_testEntries[2];
void __fastcall SearchCallback(S_ServerEntry *entry)
{
    Log("search " + ServerEntryText(entry));
    if (entry != g_testEntries[g_side]) {
        if (g_side)
            ODelete(entry);
        else
            std::free(entry);
    }
}
// A status: the players (their stats 1..5, team, name), the rules' bytes, the scores.
std::string StatusText(const void *status)
{
    const auto *s = static_cast<const uint8_t *>(status);
    const uint8_t *first, *last;
    if (g_side) {
        first = OF<uint8_t *>(const_cast<uint8_t *>(s), 4);
        last = OF<uint8_t *>(const_cast<uint8_t *>(s), 8);
    } else {
        const auto *p = static_cast<const S_ServerStatus *>(status);
        first = reinterpret_cast<const uint8_t *>(p->players.data());
        last = first + p->players.size() * sizeof(S_StatusPlayer);
    }
    std::string t = N((last - first) / 0x5C) + "[";
    for (const uint8_t *p = first; p && p < last; p += 0x5C)
        t += Bytes(p + 4, 20) + "," + N(*reinterpret_cast<const int *>(p + 0x24)) + "," +
             std::string(reinterpret_cast<const char *>(p + 0x28), strnlen(reinterpret_cast<const char *>(p + 0x28), 0x34)) + " ";
    return t + "] " + Bytes(s + 0x10, 0x58);
}

#include "sess_diff_calls.inc"
