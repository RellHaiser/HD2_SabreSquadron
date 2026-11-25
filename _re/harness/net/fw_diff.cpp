// Net part 12b: Windows XP's firewall (net/net_firewall.cpp: ConfigureFirewall, FirewallBlocks, the port mappings,
// the dialogs' watcher; net/net_create.cpp's NetFirewallAvailable) against the original executable's. MinGW;
// build_fw.bat (-include fw_fakes.h); runs fw_diff.exe ("fw_diff: N checks, 0 failures"). The port's file is
// included here (its state and helpers are its own TU's), so the mutation runner rebuilds this file
// (MUT_WRAPPER=fw_diff.cpp).
//
// Each round makes a world of network connections (each: its item's type, its INetConnection, its properties'
// characteristics and media, its sharing configuration (sharing, the firewall, its port mappings: their item types,
// protocols, external ports), the results of every call) and the firewall's state (the ports, open, watching, the
// dialog found), the system's version, and runs up to 6 calls. The COM objects are the test's on both sides (their
// calls logged by object name), and so are the system calls (the version, COM's start, the watcher's thread, the
// windows (their heights, their children), Sleep, the strings and variants). After each call the result, the logs and
// the state are compared. HD2_FW_ROUNDS: rounds (default 50,000; HD2_FW_QUICK: 10,000); HD2_FW_SEED.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "game/game_app.h"
#include "net/net_game.h"
#include "original_exe.h"

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
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
std::string WStr(const wchar_t *s)
{
    if (!s)
        return "null";
    std::string out;
    for (; *s; ++s)
        out += *s < 128 ? static_cast<char>(*s) : '?';
    return "L\"" + out + "\"";
}

// ---- the world ----
struct S_Mapping {
    VARTYPE vt;
    bool qi, props;
    long propsHr;
    UCHAR protocol;
    long external;
    long deleteHr;
};
struct S_Connection {
    VARTYPE vt;
    bool qi, props;
    long propsHr;
    DWORD characteristics;
    int media;
    bool config;
    long configHr;
    VARIANT_BOOL sharing, firewall;
    long addHr;
    bool addMapping;
    long enableHr;
    bool mappings, mappingItems, mappingQi;
    long mappingsHr, mappingItemsHr, mappingQiHr;
    std::vector<S_Mapping> list;
};
struct S_Script {
    bool versionFirst;               // GetVersionExA with the big structure succeeds
    bool versionSecond;
    DWORD platform, major, minor;
    bool manager;                    // CoCreateInstance hands the manager
    bool connections, items, itemsQi;
    long connectionsHr, itemsHr, itemsQiHr;
    std::vector<S_Connection> list;
    std::vector<LONG> windows;       // the thread's windows' heights
    int children;                    // the dialog's child windows
    int sleeps;                      // Sleep calls before the watcher's flag drops
} g_script;
int g_sleeps;

// ---- the COM objects: the test's, by kind and index (both sides': stdcall, `this` first) ----
enum E_Kind { K_MANAGER, K_CONNECTIONS, K_ITEMS, K_ENUM, K_CONNECTION, K_PROPS, K_CONFIG, K_MAPPINGS, K_MAPPING, K_MAPPING_PROPS, K_COUNT };
struct S_Obj {
    void **vtbl;
    int kind;
    int index;   // the connection's
    int sub;     // the mapping's (an enumerator: its list, -1 the connections)
    int cursor;  // (an enumerator's)
};
void *g_vtables[K_COUNT][32];
std::vector<S_Obj *> g_objects;      // (made per call; freed each round)
S_Obj *Make(int kind, int index = 0, int sub = 0)
{
    auto *o = new S_Obj{g_vtables[kind], kind, index, sub, 0};
    g_objects.push_back(o);
    return o;
}
std::string Name(const void *p)
{
    if (!p)
        return "null";
    const auto *o = static_cast<const S_Obj *>(p);
    static const char *const names[] = {"manager", "connections", "items", "enum", "conn", "props", "config", "mappings", "mapping", "mprops"};
    for (S_Obj *x : g_objects)
        if (x == o)
            return std::string(names[o->kind]) + N(o->index) + (o->kind >= K_MAPPINGS || o->kind == K_ENUM ? "." + N(o->sub) : "");
    return "?";
}
std::string Iid(REFIID iid);
const S_Connection &Conn(const S_Obj *o) { return g_script.list[static_cast<size_t>(o->index)]; }
const S_Mapping &Map(const S_Obj *o) { return Conn(o).list[static_cast<size_t>(o->sub)]; }

HRESULT __stdcall FQueryInterface(S_Obj *self, REFIID iid, void **out)
{
    Log(Name(self) + " QueryInterface " + Iid(iid));
    *out = nullptr;
    switch (self->kind) {
    case K_ITEMS:
        if (self->sub < 0 ? g_script.itemsQi : Conn(self).mappingQi)
            *out = Make(K_ENUM, self->index, self->sub);
        return self->sub < 0 ? g_script.itemsQiHr : Conn(self).mappingQiHr;
    case K_CONNECTION:
        // (The item: its INetConnection.)
        if (Conn(self).qi)
            *out = Make(K_CONNECTION, self->index);
        return Conn(self).qi ? S_OK : E_NOINTERFACE;
    case K_MAPPING:
        if (Map(self).qi)
            *out = Make(K_MAPPING, self->index, self->sub);
        return Map(self).qi ? S_OK : E_NOINTERFACE;
    default:
        return E_NOINTERFACE;
    }
}
ULONG __stdcall FAddRef(S_Obj *self)
{
    Log(Name(self) + " AddRef");
    return 1;
}
ULONG __stdcall FRelease(S_Obj *self)
{
    Log(Name(self) + " Release");
    return 0;
}
// The manager.
HRESULT __stdcall FConfigurationFor(S_Obj *, S_Obj *connection, S_Obj **out)
{
    Log("get_INetSharingConfigurationForINetConnection " + Name(connection));
    const S_Connection &c = Conn(connection);
    *out = c.config ? Make(K_CONFIG, connection->index) : nullptr;
    return c.configHr;
}
HRESULT __stdcall FEnumEveryConnection(S_Obj *, S_Obj **out)
{
    Log("get_EnumEveryConnection");
    *out = g_script.connections ? Make(K_CONNECTIONS, 0, -1) : nullptr;
    return g_script.connectionsHr;
}
HRESULT __stdcall FConnectionProps(S_Obj *, S_Obj *connection, S_Obj **out)
{
    Log("get_NetConnectionProps " + Name(connection));
    const S_Connection &c = Conn(connection);
    *out = c.props ? Make(K_PROPS, connection->index) : nullptr;
    return c.propsHr;
}
// The collections.
HRESULT __stdcall FNewEnum(S_Obj *self, S_Obj **out)
{
    Log(Name(self) + " get__NewEnum");
    if (self->kind == K_CONNECTIONS) {
        *out = g_script.items ? Make(K_ITEMS, 0, -1) : nullptr;
        return g_script.itemsHr;
    }
    *out = Conn(self).mappingItems ? Make(K_ITEMS, self->index, self->index) : nullptr;
    return Conn(self).mappingItemsHr;
}
// The enumerators (the connections' or a connection's mappings).
HRESULT __stdcall FNext(S_Obj *self, ULONG count, VARIANT *v, ULONG *fetched)
{
    Log(Name(self) + " Next " + N(count) + " " + N(v->vt) + " " + N(fetched != nullptr));
    const bool connections = self->sub < 0;
    const size_t n = connections ? g_script.list.size() : g_script.list[static_cast<size_t>(self->index)].list.size();
    if (static_cast<size_t>(self->cursor) >= n)
        return S_FALSE;
    const int i = self->cursor++;
    if (connections) {
        v->vt = g_script.list[static_cast<size_t>(i)].vt;
        v->punkVal = reinterpret_cast<IUnknown *>(Make(K_CONNECTION, i));
    } else {
        v->vt = g_script.list[static_cast<size_t>(self->index)].list[static_cast<size_t>(i)].vt;
        v->pdispVal = reinterpret_cast<IDispatch *>(Make(K_MAPPING, self->index, i));
    }
    return S_OK;
}
// A connection's properties.
HRESULT __stdcall FMediaType(S_Obj *self, int *media)
{
    Log(Name(self) + " get_MediaType " + N(*media));
    *media = Conn(self).media;
    return S_OK;
}
HRESULT __stdcall FCharacteristics(S_Obj *self, DWORD *c)
{
    Log(Name(self) + " get_Characteristics " + N(*c));
    *c = Conn(self).characteristics;
    return S_OK;
}
// A connection's configuration.
HRESULT __stdcall FSharingEnabled(S_Obj *self, VARIANT_BOOL *b)
{
    Log(Name(self) + " get_SharingEnabled " + N(*b));
    *b = Conn(self).sharing;
    return S_OK;
}
HRESULT __stdcall FFirewallEnabled(S_Obj *self, VARIANT_BOOL *b)
{
    Log(Name(self) + " get_InternetFirewallEnabled " + N(*b));
    *b = Conn(self).firewall;
    return S_OK;
}
HRESULT __stdcall FEnumPortMappings(S_Obj *self, int flags, S_Obj **out)
{
    Log(Name(self) + " get_EnumPortMappings " + N(flags));
    *out = Conn(self).mappings ? Make(K_MAPPINGS, self->index, self->index) : nullptr;
    return Conn(self).mappingsHr;
}
HRESULT __stdcall FAddPortMapping(S_Obj *self, BSTR name, UCHAR protocol, USHORT external, USHORT internal, DWORD options, BSTR target, int type,
                                  S_Obj **out)
{
    Log(Name(self) + " AddPortMapping " + WStr(name) + " " + N(protocol) + " " + N(external) + " " + N(internal) + " " + N(options) + " " + WStr(target) +
        " " + N(type));
    *out = Conn(self).addMapping ? Make(K_MAPPING, self->index, 99) : nullptr;
    return Conn(self).addHr;
}
// A mapping and its properties.
HRESULT __stdcall FEnable(S_Obj *self)
{
    Log(Name(self) + " Enable");
    return Conn(self).enableHr;
}
HRESULT __stdcall FProperties(S_Obj *self, S_Obj **out)
{
    Log(Name(self) + " get_Properties");
    *out = Map(self).props ? Make(K_MAPPING_PROPS, self->index, self->sub) : nullptr;
    return Map(self).propsHr;
}
HRESULT __stdcall FDelete(S_Obj *self)
{
    Log(Name(self) + " Delete");
    return Map(self).deleteHr;
}
HRESULT __stdcall FIPProtocol(S_Obj *self, UCHAR *p)
{
    Log(Name(self) + " get_IPProtocol " + N(*p));
    *p = Map(self).protocol;
    return S_OK;
}
HRESULT __stdcall FExternalPort(S_Obj *self, long *p)
{
    Log(Name(self) + " get_ExternalPort " + N(*p));
    *p = Map(self).external;
    return S_OK;
}

// ---- the system calls (both sides') ----
#undef GetVersionExA
#undef CoInitialize
#undef CoInitializeSecurity
#undef CoCreateInstance
#undef CreateThread
#undef GetCurrentThreadId
#undef PostMessageA
#undef GetWindowRect
#undef EnumThreadWindows
#undef EnumChildWindows
#undef Sleep
#undef SysAllocString
#undef SysFreeString
#undef VariantInit
#undef VariantClear
const DWORD THREAD_ID = 0x7171;
bool *g_watching[2];   // (each side's watcher flag: Sleep drops it after the script's count)
std::string CallbackName(const void *p);
extern "C" BOOL WINAPI FakeGetVersionExA(LPOSVERSIONINFOA info)
{
    if (!g_faking)
        return ::GetVersionExA(info);
    Log("GetVersionExA " + Hex(info->dwOSVersionInfoSize));
    const bool big = info->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA);
    if (big ? !g_script.versionFirst : !g_script.versionSecond)
        return FALSE;
    info->dwPlatformId = g_script.platform;
    info->dwMajorVersion = g_script.major;
    info->dwMinorVersion = g_script.minor;
    return TRUE;
}
extern "C" HRESULT WINAPI FakeCoInitialize(LPVOID reserved)
{
    if (!g_faking)
        return ::CoInitialize(reserved);
    Log("CoInitialize " + N(reserved != nullptr));
    return S_OK;
}
extern "C" HRESULT WINAPI FakeCoInitializeSecurity(PSECURITY_DESCRIPTOR descriptor, LONG count, SOLE_AUTHENTICATION_SERVICE *services, void *reserved1,
                                                   DWORD authentication, DWORD impersonation, void *list, DWORD capabilities, void *reserved3)
{
    Log("CoInitializeSecurity " + N(descriptor != nullptr) + " " + N(count) + " " + N(services != nullptr) + " " + N(reserved1 != nullptr) + " " +
        N(authentication) + " " + N(impersonation) + " " + N(list != nullptr) + " " + Hex(capabilities) + " " + N(reserved3 != nullptr));
    return S_OK;
}
extern "C" HRESULT WINAPI FakeCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID *object)
{
    if (!g_faking)
        return ::CoCreateInstance(clsid, outer, context, iid, object);
    Log("CoCreateInstance " + Hex(clsid.Data1) + " " + N(outer != nullptr) + " " + Hex(context) + " " + Iid(iid));
    *object = g_script.manager ? Make(K_MANAGER) : nullptr;
    return g_script.manager ? S_OK : REGDB_E_CLASSNOTREG;
}
extern "C" HANDLE WINAPI FakeCreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID parameter, DWORD flags,
                                          LPDWORD id)
{
    if (!g_faking)
        return ::CreateThread(attributes, stack, start, parameter, flags, id);
    // (The watcher's flag as the thread starts.)
    Log("CreateThread " + N(attributes != nullptr) + " " + N(static_cast<long long>(stack)) + " " + CallbackName(reinterpret_cast<const void *>(start)) + " " +
        Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(parameter))) + " " + Hex(flags) + " " + N(id != nullptr) + " " + N(*g_watching[g_side]));
    if (id)
        *id = 0x5252;
    return reinterpret_cast<HANDLE>(0x5353);
}
extern "C" DWORD WINAPI FakeGetCurrentThreadId()
{
    if (!g_faking)
        return ::GetCurrentThreadId();
    Log("GetCurrentThreadId");
    return THREAD_ID;
}
HWND Window(int i) { return reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1000 + 0x10 * i)); }
std::string WindowName(HWND w) { return w ? "w" + N((static_cast<long long>(reinterpret_cast<uintptr_t>(w)) - 0x1000) / 0x10) : "null"; }
extern "C" BOOL WINAPI FakePostMessageA(HWND window, UINT message, WPARAM w, LPARAM l)
{
    Log("PostMessageA " + WindowName(window) + " " + Hex(message) + " " + N(static_cast<long long>(w)) + " " + WindowName(reinterpret_cast<HWND>(l)));
    return TRUE;
}
extern "C" BOOL WINAPI FakeGetWindowRect(HWND window, LPRECT rect)
{
    Log("GetWindowRect " + WindowName(window));
    const int i = static_cast<int>((reinterpret_cast<uintptr_t>(window) - 0x1000) / 0x10);
    rect->left = 10;
    rect->right = 300;
    rect->top = 100 + i;
    rect->bottom = rect->top + (i < static_cast<int>(g_script.windows.size()) ? g_script.windows[static_cast<size_t>(i)] : 500);
    return TRUE;
}
extern "C" BOOL WINAPI FakeEnumThreadWindows(DWORD thread, WNDENUMPROC callback, LPARAM l)
{
    Log("EnumThreadWindows " + Hex(thread) + " " + CallbackName(reinterpret_cast<const void *>(callback)) + " " + Hex(static_cast<uint32_t>(l)));
    for (size_t i = 0; i < g_script.windows.size(); ++i)
        callback(Window(static_cast<int>(i)), l);
    return TRUE;
}
extern "C" BOOL WINAPI FakeEnumChildWindows(HWND parent, WNDENUMPROC callback, LPARAM l)
{
    Log("EnumChildWindows " + WindowName(parent) + " " + CallbackName(reinterpret_cast<const void *>(callback)) + " " + Hex(static_cast<uint32_t>(l)));
    for (int i = 0; i < g_script.children; ++i)
        callback(Window(50 + i), l);
    return TRUE;
}
extern "C" void WINAPI FakeSleep(DWORD ms)
{
    if (!g_faking)
        return ::Sleep(ms);
    Log("Sleep " + N(ms));
    if (++g_sleeps >= g_script.sleeps)
        *g_watching[g_side] = false;
}
extern "C" BSTR WINAPI FakeSysAllocString(const OLECHAR *text)
{
    Log("SysAllocString " + WStr(text));
    return ::SysAllocString(text);
}
extern "C" void WINAPI FakeSysFreeString(BSTR text)
{
    Log("SysFreeString " + WStr(text));
    ::SysFreeString(text);
}
extern "C" void WINAPI FakeVariantInit(VARIANTARG *v)
{
    Log("VariantInit");
    v->vt = VT_EMPTY;
}
extern "C" HRESULT WINAPI FakeVariantClear(VARIANTARG *v)
{
    Log("VariantClear " + N(v->vt) + " " + Name(v->punkVal));
    v->vt = VT_EMPTY;
    return S_OK;
}

// ---- the port (its file here: its state and helpers) ----
uint32_t g_gameRole;
alignas(16) unsigned char g_pApp[0x100];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_pApp); }
void C_GameApp::PrintMessage(const char *text) { Log("PrintMessage \"" + std::string(text) + "\""); }
#define CoInitialize FakeCoInitialize
#define CoInitializeSecurity FakeCoInitializeSecurity
#define CreateThread FakeCreateThread
#define GetCurrentThreadId FakeGetCurrentThreadId
#define PostMessageA FakePostMessageA
#define GetWindowRect FakeGetWindowRect
#define EnumThreadWindows FakeEnumThreadWindows
#define EnumChildWindows FakeEnumChildWindows
#define Sleep FakeSleep
#define SysAllocString FakeSysAllocString
#define SysFreeString FakeSysFreeString
#define VariantInit FakeVariantInit
#define VariantClear FakeVariantClear
#include "net/net_firewall.cpp"
#undef CoInitialize
#undef CoInitializeSecurity
#undef CreateThread
#undef GetCurrentThreadId
#undef PostMessageA
#undef GetWindowRect
#undef EnumThreadWindows
#undef EnumChildWindows
#undef Sleep
#undef SysAllocString
#undef SysFreeString
#undef VariantInit
#undef VariantClear

namespace {

// ---- the original's ----
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
const uint32_t O_FIREWALL = 0x0097cc00, O_DIALOG = 0x0097d000, O_ROLE = 0x00982100;
uint8_t g_oApp[0x100], g_oNet[0x100];
void *OGetGameApp() { return g_oApp; }
void THISCALL OPrintMessage(void *, const char *text) { Log("PrintMessage \"" + std::string(text) + "\""); }

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

void CompareState()
{
    std::string s;
    if (g_firewall.port != G<int16_t>(O_FIREWALL) || g_firewall.open != (G<uint8_t>(O_FIREWALL + 2) != 0) ||
        g_firewall.watching != (G<uint8_t>(O_FIREWALL + 3) != 0))
        s += "the firewall's state " + N(G<int16_t>(O_FIREWALL)) + " " + N(G<uint8_t>(O_FIREWALL + 2)) + " " + N(G<uint8_t>(O_FIREWALL + 3)) + " / " +
             N(g_firewall.port) + " " + N(g_firewall.open) + " " + N(g_firewall.watching) + "; ";
    if (g_firewallDialog != G<HWND>(O_DIALOG))
        s += "the dialog " + WindowName(G<HWND>(O_DIALOG)) + " / " + WindowName(g_firewallDialog) + "; ";
    if (g_gameRole != G<uint32_t>(O_ROLE))
        s += "the role; ";
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
    const int sleeps = g_sleeps;
    const long long x = port();
    g_side = 1;
    g_sleeps = sleeps;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + Hex(static_cast<uint32_t>(y)) + " / " + Hex(static_cast<uint32_t>(x)));
    CompareLogs();
    CompareState();
}

long Result() { return P(0.8) ? S_OK : P(0.5) ? static_cast<long>(E_FAIL) : S_FALSE; }

void SetWorld()
{
    for (S_Obj *o : g_objects)
        delete o;
    g_objects.clear();
    S_Script &w = g_script;
    w.versionFirst = P(0.8);
    w.versionSecond = P(0.8);
    w.platform = P(0.9) ? VER_PLATFORM_WIN32_NT : static_cast<DWORD>(I(0, 1));
    w.major = static_cast<DWORD>(P(0.7) ? 5 : I(4, 10));
    w.minor = static_cast<DWORD>(P(0.5) ? 1 : I(0, 3));
    w.manager = P(0.9);
    w.connections = P(0.9);
    w.items = P(0.9);
    w.itemsQi = P(0.9);
    w.connectionsHr = Result();
    w.itemsQiHr = Result();
    w.itemsHr = Result();
    w.list.clear();
    for (int k = I(0, 4); k > 0; --k) {
        S_Connection c;
        c.vt = P(0.9) ? VT_UNKNOWN : VT_I4;
        c.qi = P(0.9);
        c.props = P(0.9);
        c.propsHr = Result();
        static const DWORD characteristics[] = {0, 0x100, 0x400, 0x500, 0x1, 0xFFFF};
        c.characteristics = characteristics[I(0, 5)];
        static const int media[] = {0, 1, 3, 8, 9, 7};
        c.media = media[I(0, 5)];
        c.config = P(0.9);
        c.configHr = Result();
        c.sharing = P(0.5) ? VARIANT_TRUE : VARIANT_FALSE;
        c.firewall = P(0.6) ? VARIANT_TRUE : VARIANT_FALSE;
        c.addHr = Result();
        c.addMapping = P(0.8);
        c.enableHr = Result();
        c.mappings = P(0.9);
        c.mappingItems = P(0.9);
        c.mappingQi = P(0.9);
        c.mappingsHr = Result();
        c.mappingItemsHr = Result();
        c.mappingQiHr = Result();
        for (int j = I(0, 4); j > 0; --j) {
            S_Mapping m;
            m.vt = P(0.9) ? VT_DISPATCH : VT_UNKNOWN;
            m.qi = P(0.9);
            m.props = P(0.9);
            m.propsHr = Result();
            m.protocol = static_cast<UCHAR>(P(0.7) ? 0x11 : 6);
            m.external = P(0.5) ? I(31560, 31580) : P(0.5) ? I(-70000, 70000) : I(0x10000 + 31560, 0x10000 + 31580);
            m.deleteHr = Result();
            c.list.push_back(m);
        }
        w.list.push_back(c);
    }
    w.windows.clear();
    for (int k = I(0, 4); k > 0; --k)
        w.windows.push_back(P(0.5) ? I(100, 300) : I(249, 251));
    w.children = I(0, 3);
    w.sleeps = I(1, 3);
    g_sleeps = 0;
    // The firewall's state, the dialog, the role.
    g_firewall.port = static_cast<int16_t>(P(0.5) ? I(31560, 31580) : I(-32768, 32767));
    g_firewall.open = P(0.5);
    g_firewall.watching = P(0.5);
    G<int16_t>(O_FIREWALL) = g_firewall.port;
    G<uint8_t>(O_FIREWALL + 2) = g_firewall.open;
    G<uint8_t>(O_FIREWALL + 3) = g_firewall.watching;
    g_firewallDialog = P(0.5) ? nullptr : Window(I(0, 4));
    G<HWND>(O_DIALOG) = g_firewallDialog;
    static const uint32_t roles[] = {7, 1, 2, 3, 0x12};
    g_gameRole = roles[I(0, 4)];
    G<uint32_t>(O_ROLE) = g_gameRole;
}

void Call()
{
    const int op = I(0, 7);
    const int port = P(0.5) ? I(31560, 31580) : P(0.5) ? 0 : I(-100000, 100000);
    switch (op) {
    case 0:
    case 1: {
        const bool open = P(0.5);
        Run("ConfigureFirewall " + N(port) + " " + N(open),
            [&] { reinterpret_cast<C_NetGame *>(g_oNet)->ConfigureFirewall(port, open); return 0; },
            [&] { O_CALL(0x00732730, void(THISCALL *)(void *, int, bool))(g_oNet, port, open); return 0; });
        break;
    }
    case 2:
        Run("FirewallBlocks " + N(port), [&] { return static_cast<long long>(reinterpret_cast<C_NetGame *>(g_oNet)->FirewallBlocks(static_cast<uint16_t>(port))); },
            [&] { return static_cast<long long>(O_CALL(0x007328d0, bool(THISCALL *)(void *, int))(g_oNet, static_cast<uint16_t>(port))); });
        break;
    case 3: {
        // (The manager made by the test.)
        bool blocked[2];
        blocked[0] = blocked[1] = P(0.5);
        S_Obj *manager = Make(K_MANAGER);
        Run("MapConnections " + N(port),
            [&] { return static_cast<long long>(MapConnections(reinterpret_cast<INetSharingManager *>(manager), static_cast<int16_t>(port), &blocked[0])); },
            [&] { return static_cast<long long>(O_CALL(0x00732550, long(FAST *)(void *, int, bool *))(manager, static_cast<int16_t>(port), &blocked[1])); });
        Check(blocked[0] == blocked[1], "blocked");
        break;
    }
    case 4: {
        S_Obj *manager = Make(K_MANAGER);
        const UCHAR protocol = static_cast<UCHAR>(P(0.8) ? 0x11 : 6);
        Run("RemovePortMapping " + N(port),
            [&] { return static_cast<long long>(RemovePortMapping(reinterpret_cast<INetSharingManager *>(manager), protocol, port)); },
            [&] { return static_cast<long long>(O_CALL(0x00732120, long(FAST *)(void *, int, int))(manager, protocol, port)); });
        break;
    }
    case 5: {
        if (g_script.list.empty())
            return;
        S_Obj *config = Make(K_CONFIG, I(0, static_cast<int>(g_script.list.size()) - 1));
        Run("AddPortMapping " + N(port),
            [&] { return static_cast<long long>(AddPortMapping(reinterpret_cast<INetSharingConfiguration *>(config), static_cast<int16_t>(port))); },
            [&] { return static_cast<long long>(O_CALL(0x00732470, long(FAST *)(void *, int))(config, static_cast<int16_t>(port))); });
        break;
    }
    case 6: {
        // The watcher (its flag set; Sleep drops it).
        g_firewall.watching = true;
        G<uint8_t>(O_FIREWALL + 3) = 1;
        const DWORD thread = P(0.8) ? THREAD_ID : 0x99;
        Run("WatchDialogs", [&] { return static_cast<long long>(WatchDialogs(reinterpret_cast<void *>(static_cast<uintptr_t>(thread)))); },
            [&] { return static_cast<long long>(O_CALL(0x007320c0, DWORD(__stdcall *)(void *))(reinterpret_cast<void *>(static_cast<uintptr_t>(thread)))); });
        break;
    }
    default: {
        const HWND w = Window(I(0, 6));
        if (P(0.5))
            Run("FindSmallWindow", [&] { return static_cast<long long>(FindSmallWindow(w, 5)); },
                [&] { return static_cast<long long>(O_CALL(0x00732080, BOOL(__stdcall *)(HWND, LPARAM))(w, 5)); });
        else
            Run("ClickChild", [&] { return static_cast<long long>(ClickChild(w, 5)); },
                [&] { return static_cast<long long>(O_CALL(0x00732050, BOOL(__stdcall *)(HWND, LPARAM))(w, 5)); });
        break;
    }
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n   (ops:%s)\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, g_round,
                g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
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
bool RedirectApi(const char *dll, const char *name, const void *to)
{
    HMODULE m = GetModuleHandleA(dll);
    if (!m)
        m = LoadLibraryA(dll);
    const int n = m ? RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, name)), to) : 0;
    if (!n)
        std::printf("the original does not import %s\n", name);
    return n > 0;
}

}   // namespace

std::string Iid(REFIID iid)
{
    if (IsEqualIID(iid, IID_INetSharingManager_))
        return "INetSharingManager";
    if (IsEqualIID(iid, IID_INetConnection_))
        return "INetConnection";
    if (IsEqualIID(iid, IID_INetSharingPortMapping_))
        return "INetSharingPortMapping";
    if (IsEqualIID(iid, IID_IEnumVARIANT_))
        return "IEnumVARIANT";
    return Hex(iid.Data1);
}
std::string CallbackName(const void *p)
{
    const struct {
        const void *port;
        uint32_t orig;
        const char *name;
    } known[] = {{reinterpret_cast<const void *>(&WatchDialogs), 0x007320c0, "WatchDialogs"},
                 {reinterpret_cast<const void *>(&FindSmallWindow), 0x00732080, "FindSmallWindow"},
                 {reinterpret_cast<const void *>(&ClickChild), 0x00732050, "ClickChild"}};
    for (const auto &k : known)
        if (p == (g_side ? reinterpret_cast<const void *>(static_cast<uintptr_t>(k.orig)) : k.port))
            return k.name;
    return "?";
}

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

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
        std::printf("fw_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // The vtables: IUnknown's, then each kind's slots.
    for (auto &v : g_vtables) {
        v[0] = reinterpret_cast<void *>(&FQueryInterface);
        v[1] = reinterpret_cast<void *>(&FAddRef);
        v[2] = reinterpret_cast<void *>(&FRelease);
    }
    g_vtables[K_MANAGER][10] = reinterpret_cast<void *>(&FConfigurationFor);
    g_vtables[K_MANAGER][11] = reinterpret_cast<void *>(&FEnumEveryConnection);
    g_vtables[K_MANAGER][12] = reinterpret_cast<void *>(&FConnectionProps);
    g_vtables[K_CONNECTIONS][7] = reinterpret_cast<void *>(&FNewEnum);
    g_vtables[K_MAPPINGS][7] = reinterpret_cast<void *>(&FNewEnum);
    g_vtables[K_ENUM][3] = reinterpret_cast<void *>(&FNext);
    g_vtables[K_PROPS][11] = reinterpret_cast<void *>(&FMediaType);
    g_vtables[K_PROPS][12] = reinterpret_cast<void *>(&FCharacteristics);
    g_vtables[K_CONFIG][7] = reinterpret_cast<void *>(&FSharingEnabled);
    g_vtables[K_CONFIG][11] = reinterpret_cast<void *>(&FFirewallEnabled);
    g_vtables[K_CONFIG][14] = reinterpret_cast<void *>(&FEnumPortMappings);
    g_vtables[K_CONFIG][15] = reinterpret_cast<void *>(&FAddPortMapping);
    g_vtables[K_MAPPING][8] = reinterpret_cast<void *>(&FEnable);
    g_vtables[K_MAPPING][9] = reinterpret_cast<void *>(&FProperties);
    g_vtables[K_MAPPING][10] = reinterpret_cast<void *>(&FDelete);
    g_vtables[K_MAPPING_PROPS][8] = reinterpret_cast<void *>(&FIPProtocol);
    g_vtables[K_MAPPING_PROPS][9] = reinterpret_cast<void *>(&FExternalPort);
    g_watching[0] = &g_firewall.watching;
    g_watching[1] = reinterpret_cast<bool *>(static_cast<uintptr_t>(O_FIREWALL + 3));

    RedirectOriginal(0x006dcd10, reinterpret_cast<const void *>(OGetGameApp));
    RedirectOriginal(0x006c4e80, reinterpret_cast<const void *>(OPrintMessage));
    RedirectApi("kernel32.dll", "GetVersionExA", reinterpret_cast<const void *>(&FakeGetVersionExA));
    RedirectApi("kernel32.dll", "CreateThread", reinterpret_cast<const void *>(&FakeCreateThread));
    RedirectApi("kernel32.dll", "GetCurrentThreadId", reinterpret_cast<const void *>(&FakeGetCurrentThreadId));
    RedirectApi("kernel32.dll", "Sleep", reinterpret_cast<const void *>(&FakeSleep));
    RedirectApi("user32.dll", "PostMessageA", reinterpret_cast<const void *>(&FakePostMessageA));
    RedirectApi("user32.dll", "GetWindowRect", reinterpret_cast<const void *>(&FakeGetWindowRect));
    RedirectApi("user32.dll", "EnumThreadWindows", reinterpret_cast<const void *>(&FakeEnumThreadWindows));
    RedirectApi("user32.dll", "EnumChildWindows", reinterpret_cast<const void *>(&FakeEnumChildWindows));
    RedirectApi("ole32.dll", "CoInitialize", reinterpret_cast<const void *>(&FakeCoInitialize));
    RedirectApi("ole32.dll", "CoInitializeSecurity", reinterpret_cast<const void *>(&FakeCoInitializeSecurity));
    RedirectApi("ole32.dll", "CoCreateInstance", reinterpret_cast<const void *>(&FakeCoCreateInstance));
    RedirectApi("oleaut32.dll", "SysAllocString", reinterpret_cast<const void *>(&FakeSysAllocString));
    RedirectApi("oleaut32.dll", "SysFreeString", reinterpret_cast<const void *>(&FakeSysFreeString));
    RedirectApi("oleaut32.dll", "VariantInit", reinterpret_cast<const void *>(&FakeVariantInit));
    RedirectApi("oleaut32.dll", "VariantClear", reinterpret_cast<const void *>(&FakeVariantClear));

    int rounds = std::getenv("HD2_FW_QUICK") ? 10000 : 50000;
    if (const char *r = std::getenv("HD2_FW_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_FW_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        g_trace.clear();
        SetWorld();
        for (int k = I(1, 6); k > 0; --k)
            Call();
    }
    std::printf("fw_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
