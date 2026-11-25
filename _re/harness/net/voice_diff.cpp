// Net part 12a: the voice chat's DirectPlay Voice wrapper (net/net_voice.cpp: C_NetVoice) against the original
// executable's. MinGW; build_voice.bat; runs voice_diff.exe ("voice_diff: N checks, 0 failures").
//
// Each round makes a voice object on each side (the port's C_NetVoice, the original's 0x18 bytes from its
// constructor) with the same fields (the client's and server's DirectPlay Voice objects or none, the handlers, the half
// duplex flag) and runs up to 6 calls (Init, StartServer, CheckAudio, StartClient, SetClientConfig, Stop, StopClient,
// StopServer, the destructor). COM is the test's on both sides (CoCreateInstance: the test's voice test, server and
// client objects, their calls' arguments logged and their results scripted). After each call the result, the logs and
// the fields are compared. HD2_VOICE_ROUNDS: rounds (default 50,000; HD2_VOICE_QUICK: 10,000); HD2_VOICE_SEED.
#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "net/dplay8.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// The port's allocations zeroed and logged by size, as the original's (the test's calloc).
int g_side;
bool g_faking;
std::vector<std::string> g_log[2];
// (The allocations are logged too, the test's own texts' as well: alike on both sides. The log's own growth is not.)
bool g_inLog;
void Log(const std::string &s)
{
    if (g_faking && !g_inLog) {
        g_inLog = true;
        g_log[g_side].push_back(s);
        g_inLog = false;
    }
}
std::string N(long long v) { return std::to_string(v); }
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
void LogRaw(const char *what, size_t n)
{
    if (!g_faking || g_inLog)
        return;
    g_inLog = true;
    char b[32];
    std::snprintf(b, sizeof b, "%s %u", what, static_cast<unsigned>(n));
    g_log[g_side].push_back(b);
    g_inLog = false;
}
// (Fresh blocks hold garbage, as the original's heap's: 0xCD, alike on both sides.)
void *Garbage(size_t n)
{
    void *p = std::malloc(n ? n : 1);
    std::memset(p, 0xCD, n ? n : 1);
    return p;
}
void *operator new(size_t n)
{
    LogRaw("new", n);
    return Garbage(n);
}
void operator delete(void *p) noexcept
{
    if (p)
        LogRaw("delete", 0);
    std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    if (p)
        LogRaw("delete", 0);
    std::free(p);
}

namespace {

std::string Guid(const GUID *g)
{
    if (!g)
        return "null";
    const struct {
        const GUID *guid;
        const char *name;
    } known[] = {{&CLSID_NetVoiceClient, "client"}, {&IID_NetVoiceClient, "IClient"}, {&CLSID_NetVoiceServer, "server"},
                 {&IID_NetVoiceServer, "IServer"},  {&CLSID_NetVoiceTest, "test"},    {&IID_NetVoiceTest, "ITest"},
                 {&g_netVoicePlayback, "playback"}, {&g_netVoiceCapture, "capture"}};
    for (const auto &k : known)
        if (std::memcmp(g, k.guid, sizeof(GUID)) == 0)
            return k.name;
    for (int i = 0; i < 8; ++i)
        if (std::memcmp(g, &g_netVoiceCodecs[i], sizeof(GUID)) == 0)
            return "codec" + N(i);
    char b[40];
    std::snprintf(b, sizeof b, "%08lx", g->Data1);
    return b;
}

// ---- the script ----
struct S_Script {
    long create[3];           // CoCreateInstance: test, server, client
    bool failWithObject;      // a failed creation hands its object all the same
    long check[2];            // CheckAudioSetup: the query, the wizard
    long serverInit, startSession;
    long clientInit, connect, targets, sizeQuery, configGet;
    uint32_t size, flags;     // the sound device config's size and flags
    long setConfig;
} g_script;
int g_checkCalls;

// ---- the COM objects (both sides': stdcall, `this` first) ----
void *g_testVtable[16], *g_serverVtable[16], *g_clientVtable[32];
void *g_testObject = g_testVtable, *g_serverObject = g_serverVtable, *g_clientObject = g_clientVtable;
void *const g_session = reinterpret_cast<void *>(0x5E55);
void *const g_window = reinterpret_cast<void *>(0x0A1D);
void *const g_handlers[2] = {reinterpret_cast<void *>(0x4A01), reinterpret_cast<void *>(0x4A02)};
std::string Who(const void *p)
{
    if (!p)
        return "null";
    if (p == &g_testObject)
        return "test";
    if (p == &g_serverObject)
        return "server";
    if (p == &g_clientObject)
        return "client";
    if (p == g_session)
        return "session";
    if (p == g_window)
        return "window";
    if (p == g_handlers[0])
        return "handler0";
    if (p == g_handlers[1])
        return "handler1";
    return "?";
}
ULONG __stdcall FakeRelease(void *self)
{
    Log(Who(self) + " Release");
    return 0;
}
HRESULT __stdcall FakeCheckAudioSetup(void *, const GUID *playback, const GUID *capture, HWND window, DWORD flags)
{
    Log("CheckAudioSetup " + Guid(playback) + " " + Guid(capture) + " " + Who(window) + " " + Hex(flags));
    return g_script.check[g_checkCalls++ & 1];
}
HRESULT __stdcall FakeServerInitialize(void *, void *session, void *handler, void *context, DWORD *mask, DWORD count)
{
    Log("server Initialize " + Who(session) + " " + Who(handler) + " " + Who(context) + " " + N(mask != nullptr) + " " + N(count));
    return g_script.serverInit;
}
HRESULT __stdcall FakeStartSession(void *, const uint8_t *desc, DWORD flags)
{
    const auto *d = reinterpret_cast<const DWORD *>(desc);
    Log("StartSession " + N(d[0]) + " " + Hex(d[1]) + " " + N(d[2]) + " " + Guid(reinterpret_cast<const GUID *>(desc + 12)) + " " + N(d[7]) + " " + N(d[8]) +
        " " + Hex(flags));
    return g_script.startSession;
}
HRESULT __stdcall FakeStopSession(void *, DWORD flags)
{
    Log("StopSession " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeClientInitialize(void *, void *session, void *handler, void *context, DWORD *mask, DWORD count)
{
    Log("client Initialize " + Who(session) + " " + Who(handler) + " " + Who(context) + " " + N(mask != nullptr) + " " + N(count));
    return g_script.clientInit;
}
HRESULT __stdcall FakeConnect(void *, const uint8_t *sound, const uint32_t *config, DWORD flags)
{
    const auto *d = reinterpret_cast<const DWORD *>(sound);
    std::string s = "Connect " + N(d[0]) + " " + Hex(d[1]) + " " + Guid(reinterpret_cast<const GUID *>(sound + 8)) + " " + N(d[6]) + " " +
                    Guid(reinterpret_cast<const GUID *>(sound + 0x1C)) + " " + N(d[11] != 0) + " " + Who(reinterpret_cast<const void *>(d[12])) +
                    " " + N(d[13] != 0) + " " + Hex(d[14]) + " " + Hex(d[15]) + " ";
    for (int i = 0; config && i < 8; ++i)
        s += Hex(config[i]) + ",";
    Log(s + " " + Hex(flags));
    return g_script.connect;
}
HRESULT __stdcall FakeDisconnect(void *, DWORD flags)
{
    Log("Disconnect " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeSetClientConfig(void *, const uint32_t *config)
{
    std::string s = "SetClientConfig ";
    for (int i = 0; config && i < 8; ++i)
        s += Hex(config[i]) + ",";
    Log(s);
    return g_script.setConfig;
}
HRESULT __stdcall FakeSetTransmitTargets(void *, const DWORD *targets, DWORD count, DWORD flags)
{
    Log("SetTransmitTargets " + (targets ? N(targets[0]) : std::string("null")) + " " + N(count) + " " + Hex(flags));
    return g_script.targets;
}
HRESULT __stdcall FakeGetSoundDeviceConfig(void *, uint8_t *config, DWORD *size)
{
    Log("GetSoundDeviceConfig " + (config ? N(*reinterpret_cast<DWORD *>(config)) + "/" + N(reinterpret_cast<DWORD *>(config)[1]) : std::string("null")) + " " +
        N(*size));
    if (!config) {
        *size = g_script.size;
        return g_script.sizeQuery;
    }
    reinterpret_cast<DWORD *>(config)[1] = g_script.flags;
    return g_script.configGet;
}
HRESULT CreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object)
{
    Log("CoCreateInstance " + Guid(&clsid) + " " + Hex(context) + " " + Guid(&iid));
    const int k = std::memcmp(&clsid, &CLSID_NetVoiceTest, sizeof(GUID)) == 0 ? 0 : std::memcmp(&clsid, &CLSID_NetVoiceServer, sizeof(GUID)) == 0 ? 1 : 2;
    void *objects[3] = {&g_testObject, &g_serverObject, &g_clientObject};
    *object = g_script.create[k] < 0 && !g_script.failWithObject ? nullptr : objects[k];
    return g_script.create[k];
}
extern "C" HRESULT WINAPI FakeCoCreateInstance(REFCLSID clsid, LPUNKNOWN, DWORD context, REFIID iid, LPVOID *object)
{
    return CreateInstance(clsid, context, iid, object);
}

// ---- the original's ----
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
void *ONew(size_t n)
{
    LogRaw("new", n);
    return Garbage(n);
}
void ODelete(void *p)
{
    if (p)
        LogRaw("delete", 0);
    std::free(p);
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
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x + "\n     port: " + y;
            break;
        }
    }
    Check(false, s);
}

// ---- the objects ----
alignas(16) unsigned char g_p[sizeof(C_NetVoice)];
alignas(16) uint8_t g_o[0x40];
C_NetVoice &PV() { return *reinterpret_cast<C_NetVoice *>(g_p); }
bool g_alive;   // (destroyed: not compared until made again)

void CompareState()
{
    if (!g_alive)
        return;
    const C_NetVoice &p = PV();
    std::string s;
    if (Who(p.m_client) != Who(OF<void *>(g_o, 4)) || Who(p.m_server) != Who(OF<void *>(g_o, 8)))
        s += "the objects " + Who(OF<void *>(g_o, 4)) + " " + Who(OF<void *>(g_o, 8)) + " / " + Who(p.m_client) + " " + Who(p.m_server) + "; ";
    if (Who(reinterpret_cast<void *>(p.m_clientHandler)) != Who(OF<void *>(g_o, 0xC)) ||
        Who(reinterpret_cast<void *>(p.m_serverHandler)) != Who(OF<void *>(g_o, 0x10)))
        s += "the handlers; ";
    if (p.m_halfDuplex != OF<int>(g_o, 0x14))
        s += "the half duplex " + N(OF<int>(g_o, 0x14)) + " / " + N(p.m_halfDuplex) + "; ";
    Check(s.empty(), s);
}

template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_trace += " " + what;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const int calls = g_checkCalls;
    const long long x = port();
    g_side = 1;
    g_checkCalls = calls;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + Hex(static_cast<uint32_t>(y)) + " / " + Hex(static_cast<uint32_t>(x)));
    CompareLogs();
    CompareState();
}

long Result() { return P(0.8) ? 0 : P(0.5) ? static_cast<long>(0x80004005) : 1; }

void SetWorld()
{
    for (long &c : g_script.create)
        c = Result();
    g_script.failWithObject = P(0.3);
    g_script.check[0] = P(0.3) ? static_cast<long>(0x80150183) : Result();
    g_script.check[1] = Result();
    g_checkCalls = 0;
    g_script.serverInit = Result();
    g_script.startSession = P(0.2) ? 0x150010 : Result();
    g_script.clientInit = Result();
    g_script.connect = Result();
    g_script.targets = Result();
    g_script.sizeQuery = P(0.6) ? static_cast<long>(0x8015001E) : Result();
    g_script.configGet = Result();
    g_script.size = P(0.8) ? 0x40 : static_cast<uint32_t>(I(8, 0x80));
    g_script.flags = static_cast<uint32_t>(I(0, 0x3F));
    g_script.setConfig = P(0.2) ? 0x150010 : Result();
    // The objects: made (the constructor compared), their fields set alike.
    void *client = P(0.5) ? g_handlers[0] : nullptr, *server = P(0.5) ? g_handlers[1] : nullptr;
    g_trace.clear();
    Run("constructor", [&] { new (g_p) C_NetVoice(reinterpret_cast<NetVoiceHandler>(client), reinterpret_cast<NetVoiceHandler>(server)); return 0; },
        [&] {
            std::memset(g_o, 0xCD, sizeof g_o);
            O_CALL(0x00742190, void *(THISCALL *)(void *, void *, void *))(g_o, client, server);
            return 0;
        });
    g_alive = true;
    C_NetVoice &p = PV();
    const bool hasClient = P(0.4), hasServer = P(0.4);
    p.m_client = hasClient ? reinterpret_cast<IDirectPlayVoiceClient *>(&g_clientObject) : nullptr;
    p.m_server = hasServer ? reinterpret_cast<IDirectPlayVoiceServer *>(&g_serverObject) : nullptr;
    OF<void *>(g_o, 4) = p.m_client;
    OF<void *>(g_o, 8) = p.m_server;
    p.m_halfDuplex = I(0, 1);
    OF<int>(g_o, 0x14) = p.m_halfDuplex;
}

void Call()
{
    if (!g_alive)
        return;
    C_NetVoice &p = PV();
    void *o = g_o;
    const int op = I(0, 9);
    void *window = P(0.8) ? g_window : nullptr;
    uint32_t config[8];
    for (uint32_t &c : config)
        c = static_cast<uint32_t>(I(0, 0xFFFF));
    void *cfg = P(0.9) ? config : nullptr;
    const int codec = I(0, 7);
    const GUID *codecGuid = &g_netVoiceCodecs[codec];
    switch (op) {
    case 0: {
        const bool server = P(0.5), client = P(0.5);
        const int type = I(0, 4), unused = I(0, 3);
        Run("Init " + N(server) + " " + N(client),
            [&] { return static_cast<long long>(p.Init(window, server, client, static_cast<IUnknown *>(g_session), type, codecGuid, cfg, unused)); },
            [&] {
                return static_cast<long long>(O_CALL(0x007421f0, long(THISCALL *)(void *, void *, bool, bool, void *, int, const GUID *, void *, int))(
                    o, window, server, client, g_session, type, codecGuid, cfg, unused));
            });
        break;
    }
    case 1: {
        const int type = I(0, 4);
        Run("StartServer", [&] { return static_cast<long long>(p.StartServer(static_cast<IUnknown *>(g_session), type, codecGuid)); },
            [&] { return static_cast<long long>(O_CALL(0x007423b0, long(THISCALL *)(void *, void *, int, const GUID *))(o, g_session, type, codecGuid)); });
        break;
    }
    case 2:
        Run("CheckAudio", [&] { return static_cast<long long>(C_NetVoice::CheckAudio(window)); },
            [&] { return static_cast<long long>(O_CALL(0x007422c0, long(THISCALL *)(void *, void *))(o, window)); });
        break;
    case 3: {
        const int unused = I(0, 3);
        Run("StartClient", [&] { return static_cast<long long>(p.StartClient(window, static_cast<IUnknown *>(g_session), cfg, unused)); },
            [&] { return static_cast<long long>(O_CALL(0x00742460, long(THISCALL *)(void *, void *, void *, void *, int))(o, window, g_session, cfg, unused)); });
        break;
    }
    case 4:
        Run("SetClientConfig", [&] { return static_cast<long long>(p.SetClientConfig(cfg)); },
            [&] { return static_cast<long long>(O_CALL(0x007425e0, long(THISCALL *)(void *, void *))(o, cfg)); });
        break;
    case 5:
        Run("Stop", [&] { return static_cast<long long>(p.Stop()); }, [&] { return static_cast<long long>(O_CALL(0x00742250, long(FAST *)(void *))(o)); });
        break;
    case 6:
        Run("StopClient", [&] { return static_cast<long long>(p.StopClient()); },
            [&] { return static_cast<long long>(O_CALL(0x00742610, long(FAST *)(void *))(o)); });
        break;
    case 7:
        Run("StopServer", [&] { return static_cast<long long>(p.StopServer()); },
            [&] { return static_cast<long long>(O_CALL(0x00742640, long(FAST *)(void *))(o)); });
        break;
    default:
        // The destructor (the original's: its body; the deleting one frees through the test's delete).
        Run("destructor", [&] { p.~C_NetVoice(); return 0; }, [&] { O_CALL(0x007421e0, void(FAST *)(void *))(o); return 0; });
        g_alive = false;
        break;
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n   (ops:%s)\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, g_round,
                g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    HMODULE module = nullptr;
    char moduleName[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char *>(e->ExceptionRecord->ExceptionAddress), &module))
        GetModuleFileNameA(module, moduleName, sizeof moduleName);
    std::printf("  in %s; esp %08lx ebp %08lx\n", moduleName, e->ContextRecord->Esp, e->ContextRecord->Ebp);
    for (auto *bp = reinterpret_cast<const uint32_t *>(e->ContextRecord->Ebp); bp && reinterpret_cast<uintptr_t>(bp) > 0x10000 && bp[1]; bp = reinterpret_cast<const uint32_t *>(bp[0]))
        std::printf("  frame: %08x\n", bp[1]);
    const auto *sp = reinterpret_cast<const uint32_t *>(e->ContextRecord->Esp);
    for (int i = 0; i < 48; ++i)
        if ((sp[i] >= 0x20000000u && sp[i] < 0x21000000u) || (sp[i] >= 0x400000u && sp[i] < 0x900000u))
            std::printf("  stack +%02x: %08x\n", i * 4, sp[i]);
    std::fflush(stdout);
    ExitProcess(3);
}

int RedirectImport(const void *real, const void *to)
{
    if (!real)
        return 0;
    const auto *base = reinterpret_cast<const uint8_t *>(0x400000);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int n = 0;
    for (auto *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp)
        for (auto *t = reinterpret_cast<IMAGE_THUNK_DATA32 *>(const_cast<uint8_t *>(base) + imp->FirstThunk); t->u1.Function; ++t)
            if (t->u1.Function == static_cast<DWORD>(reinterpret_cast<uintptr_t>(real))) {
                t->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(to));
                ++n;
            }
    return n;
}

}   // namespace

HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object) { return CreateInstance(clsid, context, iid, object); }

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("voice_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    for (void **v : {g_testVtable, g_serverVtable, g_clientVtable})
        v[2] = reinterpret_cast<void *>(&FakeRelease);
    g_testVtable[0x0C / 4] = reinterpret_cast<void *>(&FakeCheckAudioSetup);
    g_serverVtable[0x0C / 4] = reinterpret_cast<void *>(&FakeServerInitialize);
    g_serverVtable[0x10 / 4] = reinterpret_cast<void *>(&FakeStartSession);
    g_serverVtable[0x14 / 4] = reinterpret_cast<void *>(&FakeStopSession);
    g_clientVtable[0x0C / 4] = reinterpret_cast<void *>(&FakeClientInitialize);
    g_clientVtable[0x10 / 4] = reinterpret_cast<void *>(&FakeConnect);
    g_clientVtable[0x14 / 4] = reinterpret_cast<void *>(&FakeDisconnect);
    g_clientVtable[0x20 / 4] = reinterpret_cast<void *>(&FakeSetClientConfig);
    g_clientVtable[0x2C / 4] = reinterpret_cast<void *>(&FakeSetTransmitTargets);
    g_clientVtable[0x40 / 4] = reinterpret_cast<void *>(&FakeGetSoundDeviceConfig);
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    HMODULE ole = LoadLibraryA("ole32.dll");
    if (!RedirectImport(reinterpret_cast<const void *>(GetProcAddress(ole, "CoCreateInstance")), reinterpret_cast<const void *>(&FakeCoCreateInstance))) {
        std::printf("voice_diff: the original's CoCreateInstance not found\n");
        return 1;
    }

    int rounds = std::getenv("HD2_VOICE_QUICK") ? 10000 : 50000;
    if (const char *r = std::getenv("HD2_VOICE_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_VOICE_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        SetWorld();
        for (int k = I(1, 6); k > 0; --k)
            Call();
    }
    std::printf("voice_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
