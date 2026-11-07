// Windows XP's firewall, the loading progress and the CD key (part 10d-vii: net/net_firewall.cpp,
// net/net_console.cpp) with the NetSharingManager faked: connections (characteristics, media, sharing / firewall)
// with port mappings, reference counted (every object released: none left alive). MinGW; build_firewall.bat; run
// firewall_test.exe ("firewall: N checks, 0 failures"). Mutations: mutations/mutate_10d7.py.
#include <winsock2.h>
#include <windows.h>
#include <netcon.h>
#include <oleauto.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/console.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/igraph.h"
#include "net/dplay8.h"
#include "net/gamespy.h"
#include "net/net_game.h"
#undef private
#undef protected

namespace {

int checks, failures;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    g_log.push_back(line);
}
bool Logged(const char *line)
{
    for (const std::string &l : g_log)
        if (l == line)
            return true;
    return false;
}
int CountLogged(const char *prefix)
{
    int n = 0;
    for (const std::string &l : g_log)
        n += l.compare(0, std::strlen(prefix), prefix) == 0;
    return n;
}

}  // namespace

// ---- the game around ---------------------------------------------------------------------------------------------------

alignas(16) static unsigned char g_appStorage[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_appStorage); }
void C_GameApp::PrintMessage(const char *text) { Log("app print %s", text); }
C_GameTexts g_texts;
C_str C_GameTexts::Get(int id) const
{
    char text[16];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
alignas(16) static unsigned char g_consoleStorage[sizeof(C_Console)];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_consoleStorage); }
void C_Console::OnChar(char ch) { Log("char %c", ch); }
void C_Console::AddLine(const char *text) { Log("line %s", text); }
void C_Console::MoveInputLineToHistory() { Log("history"); }
C_MpMap *g_currentMap;
C_MpMap *S_MultiplayerSettings::GetCurrentMap() { return g_currentMap; }
alignas(16) static unsigned char g_mpStorage[sizeof(S_MultiplayerSettings)];
extern "C" IGraph *GetIGraph() { return nullptr; }
void SocketStartUp() { Log("socket start"); }

// ---- the firewall --------------------------------------------------------------------------------------------------------

bool g_available = true;
bool NetFirewallAvailable() { return g_available; }

int g_live;   // the fake COM objects alive

// IUnknown and IDispatch for a fake (its own reference count; `iid` answered by QueryInterface).
#define T_UNKNOWN(iidName)                                                                                       \
    ULONG refs = 1;                                                                                              \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override                                    \
    {                                                                                                            \
        if (std::memcmp(&iid, &iidName, sizeof(GUID)) == 0) {                                                    \
            ++refs;                                                                                              \
            *out = this;                                                                                         \
            return S_OK;                                                                                         \
        }                                                                                                        \
        *out = nullptr;                                                                                          \
        return E_NOINTERFACE;                                                                                    \
    }                                                                                                            \
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }                                                 \
    ULONG STDMETHODCALLTYPE Release() override                                                                   \
    {                                                                                                            \
        if (--refs == 0) {                                                                                       \
            --g_live;                                                                                            \
            delete this;                                                                                         \
            return 0;                                                                                            \
        }                                                                                                        \
        return refs;                                                                                             \
    }
#define T_DISPATCH                                                                                               \
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *) override { return E_NOTIMPL; }                            \
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }               \
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) override { return E_NOTIMPL; } \
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *) override \
    {                                                                                                            \
        return E_NOTIMPL;                                                                                        \
    }

const IID kConnection = {0xc08956a1, 0x1cd3, 0x11d1, {0xb1, 0xc5, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e}};
const IID kMapping = {0xc08956b1, 0x1cd3, 0x11d1, {0xb1, 0xc5, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e}};
const IID kEnum = {0x00020404, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kManager = {0xc08956b7, 0x1cd3, 0x11d1, {0xb1, 0xc5, 0x00, 0x80, 0x5f, 0xc1, 0x27, 0x0e}};
const CLSID kManagerClass = {0x5c63c1ad, 0x3956, 0x4ff8, {0x84, 0x86, 0x40, 0x03, 0x47, 0x58, 0x31, 0x5b}};
const IID kNone = {};

// The model: connections with their mappings.
struct S_Mapping {
    std::string name;
    UCHAR protocol;
    long port;
    long internal;
    std::string target;
    long targetType;
    bool enabled = false;
    bool deleted = false;
};
struct S_Connection {
    DWORD characteristics;
    NETCON_MEDIATYPE media;
    VARIANT_BOOL sharing;
    VARIANT_BOOL firewall;
    std::vector<S_Mapping> mappings;
    bool noConfig = false;
};
std::vector<S_Connection> g_connections;
// Messages pumped while a mapping is added (the watcher thread's clicks arrive; ms).
int g_pumpMs;
void Pump(int ms)
{
    const DWORD end = GetTickCount() + static_cast<DWORD>(ms);
    while (GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageA(&msg);
        Sleep(10);
    }
}

// An enumeration of VARIANTs (each item AddRef'd for the caller).
struct T_Enum : IEnumVARIANT {
    T_UNKNOWN(kEnum)
    explicit T_Enum(std::vector<IUnknown *> items, VARTYPE vt) : items(std::move(items)), vt(vt) { ++g_live; }
    ~T_Enum()
    {
        for (IUnknown *item : items)
            item->Release();
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, VARIANT *out, ULONG *fetched) override
    {
        if (count != 1 || fetched != nullptr)
            Log("enum next %lu %d", count, fetched != nullptr);
        if (at >= items.size())
            return S_FALSE;
        out->vt = vt;
        out->punkVal = items[at++];
        out->punkVal->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Reset() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT **) override { return E_NOTIMPL; }
    std::vector<IUnknown *> items;
    VARTYPE vt;
    size_t at = 0;
};
HRESULT MakeEnum(std::vector<IUnknown *> items, VARTYPE vt, IUnknown **out)
{
    // (The IUnknown handed out answers IEnumVARIANT with its inner enumeration.)
    struct T_Source : IUnknown {
        ULONG refs = 1;
        T_Enum *inner;
        explicit T_Source(T_Enum *e) : inner(e) { ++g_live; }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
        {
            if (std::memcmp(&iid, &kEnum, sizeof(GUID)) == 0) {
                inner->AddRef();
                *out = inner;
                return S_OK;
            }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            if (--refs == 0) {
                inner->Release();
                --g_live;
                delete this;
                return 0;
            }
            return refs;
        }
    };
    *out = new T_Source(new T_Enum(std::move(items), vt));
    return S_OK;
}

struct T_MappingProps : INetSharingPortMappingProps {
    T_UNKNOWN(kNone)
    T_DISPATCH
    explicit T_MappingProps(S_Mapping *m) : m(m) { ++g_live; }
    HRESULT STDMETHODCALLTYPE get_Name(BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_IPProtocol(UCHAR *p) override
    {
        *p = m->protocol;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExternalPort(long *p) override
    {
        *p = m->port;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_InternalPort(long *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Options(long *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_TargetName(BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_TargetIPAddress(BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Enabled(VARIANT_BOOL *) override { return E_NOTIMPL; }
    S_Mapping *m;
};
struct T_Mapping : INetSharingPortMapping {
    T_UNKNOWN(kMapping)
    T_DISPATCH
    explicit T_Mapping(S_Mapping *m) : m(m) { ++g_live; }
    HRESULT STDMETHODCALLTYPE Disable() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Enable() override
    {
        m->enabled = true;
        return 0x77;
    }
    HRESULT STDMETHODCALLTYPE get_Properties(INetSharingPortMappingProps **p) override
    {
        *p = new T_MappingProps(m);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Delete() override
    {
        m->deleted = true;
        Log("delete %s", m->name.c_str());
        return 0x55;
    }
    S_Mapping *m;
};
struct T_MappingCollection : INetSharingPortMappingCollection {
    T_UNKNOWN(kNone)
    T_DISPATCH
    explicit T_MappingCollection(S_Connection *c) : c(c) { ++g_live; }
    HRESULT STDMETHODCALLTYPE get__NewEnum(IUnknown **out) override
    {
        std::vector<IUnknown *> items;
        for (S_Mapping &m : c->mappings)
            if (!m.deleted)
                items.push_back(new T_Mapping(&m));
        return MakeEnum(std::move(items), VT_DISPATCH, out);
    }
    HRESULT STDMETHODCALLTYPE get_Count(long *) override { return E_NOTIMPL; }
    S_Connection *c;
};
struct T_Config : INetSharingConfiguration {
    T_UNKNOWN(kNone)
    T_DISPATCH
    explicit T_Config(S_Connection *c) : c(c) { ++g_live; }
    HRESULT STDMETHODCALLTYPE get_SharingEnabled(VARIANT_BOOL *p) override
    {
        *p = c->sharing;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SharingConnectionType(SHARINGCONNECTIONTYPE *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DisableSharing() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnableSharing(SHARINGCONNECTIONTYPE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_InternetFirewallEnabled(VARIANT_BOOL *p) override
    {
        *p = c->firewall;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DisableInternetFirewall() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnableInternetFirewall() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_EnumPortMappings(SHARINGCONNECTION_ENUM_FLAGS flags, INetSharingPortMappingCollection **p)
        override
    {
        Log("enum mappings %d", static_cast<int>(flags));
        *p = new T_MappingCollection(c);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddPortMapping(BSTR name, UCHAR protocol, USHORT external, USHORT internal, DWORD options,
                                             BSTR target, ICS_TARGETTYPE type, INetSharingPortMapping **out) override
    {
        S_Mapping m;
        for (const wchar_t *w = name; *w; ++w)
            m.name += static_cast<char>(*w);
        for (const wchar_t *w = target; *w; ++w)
            m.target += static_cast<char>(*w);
        m.protocol = protocol;
        m.port = external;
        m.internal = internal;
        m.targetType = type;
        Log("add %s %u %u %u %lu %s %d", m.name.c_str(), protocol, external, internal, options, m.target.c_str(),
            static_cast<int>(type));
        c->mappings.push_back(m);
        if (g_pumpMs)
            Pump(g_pumpMs);
        // (Pointers into the vector: kept valid by reserving.)
        *out = new T_Mapping(&c->mappings.back());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RemovePortMapping(INetSharingPortMapping *) override { return E_NOTIMPL; }
    S_Connection *c;
};
struct T_ConnectionProps : INetConnectionProps {
    T_UNKNOWN(kNone)
    T_DISPATCH
    explicit T_ConnectionProps(S_Connection *c) : c(c) { ++g_live; }
    HRESULT STDMETHODCALLTYPE get_Guid(BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Name(BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_DeviceName(BSTR *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_Status(NETCON_STATUS *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_MediaType(NETCON_MEDIATYPE *p) override
    {
        *p = c->media;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Characteristics(DWORD *p) override
    {
        *p = c->characteristics;
        return S_OK;
    }
    S_Connection *c;
};
struct T_Connection : INetConnection {
    T_UNKNOWN(kConnection)
    explicit T_Connection(S_Connection *c) : c(c) { ++g_live; }
    HRESULT STDMETHODCALLTYPE Connect() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Disconnect() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Delete() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Duplicate(LPCWSTR, INetConnection **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetProperties(NETCON_PROPERTIES **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetUiObjectClassId(CLSID *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Rename(LPCWSTR) override { return E_NOTIMPL; }
    S_Connection *c;
};
struct T_EveryConnection : INetSharingEveryConnectionCollection {
    T_UNKNOWN(kNone)
    T_DISPATCH
    T_EveryConnection() { ++g_live; }
    HRESULT STDMETHODCALLTYPE get__NewEnum(IUnknown **out) override
    {
        std::vector<IUnknown *> items;
        for (S_Connection &c : g_connections)
            items.push_back(new T_Connection(&c));
        return MakeEnum(std::move(items), VT_UNKNOWN, out);
    }
    HRESULT STDMETHODCALLTYPE get_Count(long *) override { return E_NOTIMPL; }
};
struct T_Manager : INetSharingManager {
    T_UNKNOWN(kManager)
    T_DISPATCH
    T_Manager() { ++g_live; }
    HRESULT STDMETHODCALLTYPE get_SharingInstalled(VARIANT_BOOL *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_EnumPublicConnections(SHARINGCONNECTION_ENUM_FLAGS, INetSharingPublicConnectionCollection **)
        override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_EnumPrivateConnections(SHARINGCONNECTION_ENUM_FLAGS,
                                                         INetSharingPrivateConnectionCollection **) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE get_INetSharingConfigurationForINetConnection(INetConnection *connection,
                                                                             INetSharingConfiguration **out) override
    {
        S_Connection *c = static_cast<T_Connection *>(connection)->c;
        *out = c->noConfig ? nullptr : new T_Config(c);
        return 0x33;
    }
    HRESULT STDMETHODCALLTYPE get_EnumEveryConnection(INetSharingEveryConnectionCollection **out) override
    {
        *out = g_noConnections ? nullptr : new T_EveryConnection();
        return 0x44;
    }
    HRESULT STDMETHODCALLTYPE get_NetConnectionProps(INetConnection *connection, INetConnectionProps **out) override
    {
        *out = new T_ConnectionProps(static_cast<T_Connection *>(connection)->c);
        return S_OK;
    }
    static bool g_noConnections;
};
bool T_Manager::g_noConnections;

bool g_makeManager = true;
HRESULT NetCreateInstance(const GUID &clsid, DWORD context, const GUID &iid, void **object)
{
    Log("create %d %lu %d", std::memcmp(&clsid, &kManagerClass, sizeof(GUID)) == 0, context,
        std::memcmp(&iid, &kManager, sizeof(GUID)) == 0);
    *object = g_makeManager ? static_cast<INetSharingManager *>(new T_Manager()) : nullptr;
    return S_OK;
}

// ---- the tests ---------------------------------------------------------------------------------------------------------

constexpr DWORD FIREWALLED = 0x400;
S_Connection Connection(DWORD characteristics, NETCON_MEDIATYPE media = NCM_LAN, bool sharing = false,
                        bool firewall = true)
{
    S_Connection c;
    c.characteristics = characteristics;
    c.media = media;
    c.sharing = sharing ? VARIANT_TRUE : VARIANT_FALSE;
    c.firewall = firewall ? VARIANT_TRUE : VARIANT_FALSE;
    c.mappings.reserve(32);
    return c;
}
C_NetGame &Net()
{
    alignas(16) static unsigned char storage[sizeof(C_NetGame)];
    return *reinterpret_cast<C_NetGame *>(storage);
}
void Reset()
{
    g_log.clear();
    g_connections.clear();
    g_connections.reserve(8);
    g_available = true;
    g_makeManager = true;
    T_Manager::g_noConnections = false;
    g_gameRole = GAME_ROLE_SERVER;
}

void TestOpen()
{
    // Opened: told (not on a client), port .. port + 3 mapped on the firewalled connections (not a shared access
    // host's, not one without sharing or a firewall), enabled; every object released.
    Reset();
    g_connections.push_back(Connection(FIREWALLED));
    g_connections.push_back(Connection(0));                                       // not firewalled
    g_connections.push_back(Connection(0x100, NCM_LAN, true, false));             // sharing only
    g_connections.push_back(Connection(FIREWALLED, static_cast<NETCON_MEDIATYPE>(8)));   // shared access host
    g_connections.push_back(Connection(FIREWALLED, static_cast<NETCON_MEDIATYPE>(9)));
    g_connections.push_back(Connection(FIREWALLED, NCM_LAN, false, false));      // neither: nothing mapped
    Net().ConfigureFirewall(11001, true);
    CHECK(Logged("create 1 23 1") && Logged("app print Firewall settings have been modified!"));
    CHECK(g_connections[0].mappings.size() == 4 && g_connections[2].mappings.size() == 4);
    CHECK(g_connections[1].mappings.empty() && g_connections[3].mappings.empty() && g_connections[4].mappings.empty() &&
          g_connections[5].mappings.empty());
    CHECK(Logged("add HD2 port 11001 17 11001 11001 0 * 1") && Logged("add HD2 port 11004 17 11004 11004 0 * 1"));
    bool enabled = true;
    for (const S_Mapping &m : g_connections[0].mappings)
        enabled &= m.enabled;
    CHECK(enabled && g_live == 0 && CountLogged("enum next") == 0);
    // Open already: nothing; a client: not told.
    g_log.clear();
    Net().ConfigureFirewall(11001, true);
    CHECK(g_log.empty());
    // Closed: the first firewalled connection's UDP mappings of port .. port + 3 deleted (4 times).
    g_connections[0].mappings.push_back({"other", 6, 11001, 11001, "*", 1});
    g_connections[0].mappings.push_back({"other2", 17, 11009, 11009, "*", 1});
    Net().ConfigureFirewall(0, false);
    CHECK(CountLogged("delete HD2 port") == 4 && Logged("enum mappings 0") && g_live == 0);
    CHECK(!g_connections[0].mappings[4].deleted && !g_connections[0].mappings[5].deleted &&
          g_connections[0].mappings[0].deleted && g_connections[0].mappings[3].deleted);
    CHECK(g_connections[2].mappings[0].deleted == false);
    // Closed already: nothing.
    g_log.clear();
    Net().ConfigureFirewall(0, false);
    CHECK(g_log.empty());
    // The port stored: the next open's 2..5 removed after an open of 2.
    g_gameRole = GAME_ROLE_CLIENT;
    g_connections.assign(1, Connection(FIREWALLED));
    g_connections[0].mappings.reserve(32);
    Net().ConfigureFirewall(-3, true);
    CHECK(!Logged("app print Firewall settings have been modified!") && g_connections[0].mappings.size() == 4);
    CHECK(Logged("add HD2 port -3 17 65533 65533 0 * 1") && Logged("add HD2 port 0 17 0 0 0 * 1"));
    // (Closing compares the sign-extended port: 65533 .. 65535 stay, as in the original.)
    g_log.clear();
    Net().ConfigureFirewall(77, false);
    CHECK(CountLogged("delete HD2 port") == 1 && Logged("delete HD2 port 0") && g_live == 0);
}

void TestNoFirewall()
{
    // Not XP: nothing; no manager: nothing changed (still closed).
    Reset();
    g_connections.push_back(Connection(FIREWALLED));
    g_available = false;
    Net().ConfigureFirewall(5000, true);
    CHECK(g_log.empty() && g_connections[0].mappings.empty());
    CHECK(!Net().FirewallBlocks(5000) && g_log.empty());
    g_available = true;
    g_makeManager = false;
    Net().ConfigureFirewall(5000, true);
    CHECK(g_connections[0].mappings.empty() && !Logged("app print Firewall settings have been modified!"));
    g_makeManager = true;
    Net().ConfigureFirewall(5000, true);
    CHECK(g_connections[0].mappings.size() == 4);
    // No connections at all: nothing, still opened.
    T_Manager::g_noConnections = true;
    Net().ConfigureFirewall(0, false);
    CHECK(CountLogged("delete") == 0 && g_live == 0);
    Net().ConfigureFirewall(1, false);
    CHECK(CountLogged("delete") == 0);
    T_Manager::g_noConnections = false;
    // No configuration: nothing mapped.
    g_connections.assign(1, Connection(FIREWALLED));
    g_connections[0].noConfig = true;
    Net().ConfigureFirewall(6000, true);
    CHECK(g_connections[0].mappings.empty() && g_live == 0);
    Net().ConfigureFirewall(0, false);
    CHECK(g_live == 0);
}

void TestRemoveFirst()
{
    // Closing finds the first firewalled connection that is no shared access host (not the others).
    Reset();
    g_connections.push_back(Connection(0x100, static_cast<NETCON_MEDIATYPE>(8)));
    g_connections.push_back(Connection(0));
    g_connections.push_back(Connection(0x100));
    g_connections.push_back(Connection(FIREWALLED));
    for (S_Connection &c : g_connections) {
        for (int port = 7000; port < 7004; ++port)
            c.mappings.push_back({"m", 17, port, port, "*", 1});
    }
    Net().ConfigureFirewall(7000, true);
    for (S_Connection &c : g_connections)
        c.mappings.resize(4);
    g_log.clear();
    Net().ConfigureFirewall(0, false);
    CHECK(CountLogged("delete m") == 4 && g_connections[2].mappings[0].deleted && g_connections[2].mappings[3].deleted);
    CHECK(!g_connections[0].mappings[0].deleted && !g_connections[1].mappings[0].deleted &&
          !g_connections[3].mappings[0].deleted && g_live == 0);
}

void TestNothingToClose()
{
    // No firewalled connection (or only a shared access host): nothing deleted, nothing kept alive; a mapping past
    // port + 3 stays.
    Reset();
    g_connections.push_back(Connection(FIREWALLED));
    Net().ConfigureFirewall(8000, true);
    g_connections[0].characteristics = 0;
    g_log.clear();
    Net().ConfigureFirewall(0, false);
    CHECK(CountLogged("delete") == 0 && g_live == 0);
    g_connections[0].characteristics = FIREWALLED;
    g_connections[0].media = static_cast<NETCON_MEDIATYPE>(9);
    Net().ConfigureFirewall(8000, true);
    g_log.clear();
    Net().ConfigureFirewall(0, false);
    CHECK(CountLogged("delete") == 0 && g_live == 0);
    g_connections.assign(1, Connection(FIREWALLED));
    Net().ConfigureFirewall(8000, true);
    g_connections[0].mappings.push_back({"past", 17, 8004, 8004, "*", 1});
    g_log.clear();
    Net().ConfigureFirewall(0, false);
    CHECK(CountLogged("delete HD2 port") == 4 && !Logged("delete past") && g_live == 0 && CountLogged("enum next") == 0);
}

// The watcher's clicks: the windows' WM_COMMANDs logged.
HWND g_smallWindow, g_tallWindow, g_smallChild, g_tallChild;
LRESULT __stdcall WatchedProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COMMAND)
        Log("click %s %d %s", window == g_smallWindow ? "small" : "tall", static_cast<int>(wParam),
            reinterpret_cast<HWND>(lParam) == g_smallChild ? "smallChild"
            : reinterpret_cast<HWND>(lParam) == g_tallChild ? "tallChild" : "other");
    return DefWindowProcA(window, message, wParam, lParam);
}

void TestWatcher()
{
    // While the ports are opened: the thread's window shorter than 250 px gets OK from each of its children; when
    // done the clicks stop. (Made tall first: the enumeration meets the small one first, the tall one last.)
    Reset();
    // (No IME window for this thread: hidden and enumerated last, it would be the small window the watcher picks.)
    using T_DisableIme = BOOL(WINAPI *)(DWORD);
    if (const auto disable = reinterpret_cast<T_DisableIme>(GetProcAddress(LoadLibraryA("imm32.dll"), "ImmDisableIME")))
        disable(GetCurrentThreadId());
    WNDCLASSA type = {};
    type.lpfnWndProc = WatchedProc;
    type.hInstance = GetModuleHandleA(nullptr);
    type.lpszClassName = "T_Watched";
    RegisterClassA(&type);
    g_tallWindow = CreateWindowExA(0, "T_Watched", "", WS_POPUP, 0, 0, 300, 250, nullptr, nullptr, type.hInstance, nullptr);
    g_tallChild = CreateWindowExA(0, "STATIC", "", WS_CHILD, 0, 0, 9, 9, g_tallWindow, nullptr, type.hInstance, nullptr);
    g_smallWindow = CreateWindowExA(0, "T_Watched", "", WS_POPUP, 0, 0, 300, 249, nullptr, nullptr, type.hInstance, nullptr);
    g_smallChild = CreateWindowExA(0, "STATIC", "", WS_CHILD, 0, 0, 9, 9, g_smallWindow, nullptr, type.hInstance, nullptr);
    g_connections.push_back(Connection(FIREWALLED));
    g_pumpMs = 150;
    Net().ConfigureFirewall(9000, true);
    g_pumpMs = 0;
    CHECK(CountLogged("click small 1 smallChild") >= 1 && CountLogged("click tall") == 0);
    CHECK(CountLogged("click") == CountLogged("click small 1 smallChild"));
    // (No clicks once done: one 200 ms round may still be on its way.)
    Pump(300);
    g_log.clear();
    Pump(500);
    CHECK(CountLogged("click") == 0);
    Net().ConfigureFirewall(0, false);
    DestroyWindow(g_tallWindow);
    DestroyWindow(g_smallWindow);
}

void TestBlocks()
{
    // The firewall's state: each connection decides in turn (the last one's); nothing mapped.
    Reset();
    g_connections.push_back(Connection(0));
    g_connections.push_back(Connection(FIREWALLED));
    CHECK(Net().FirewallBlocks(123) && g_connections[1].mappings.empty() && g_live == 0);
    g_connections.push_back(Connection(0));
    CHECK(!Net().FirewallBlocks(123));
    g_connections.assign(1, Connection(0x100));
    CHECK(Net().FirewallBlocks(0));
    g_connections.clear();
    CHECK(!Net().FirewallBlocks(0));
    g_makeManager = false;
    g_connections.assign(1, Connection(FIREWALLED));
    CHECK(!Net().FirewallBlocks(0));
}

// ---- the loading progress, the CD key ----------------------------------------------------------------------------------

void TestLoadProgress()
{
    Reset();
    auto &settings = *reinterpret_cast<S_MultiplayerSettings *>(g_mpStorage);
    g_gameProfile.m_mpSettings = &settings;
    C_MpMap map(C_str("Arnhem"), C_str("dir"));
    g_currentMap = &map;
    // Not loading: no dots; 0: the map's line, loading.
    Net().PrintLoadProgress(4);
    Net().PrintLoadProgress(100);
    CHECK(g_log.empty());
    Net().PrintLoadProgress(0);
    CHECK(Logged("line T22C8  Arnhem") && g_log.size() == 1);
    g_log.clear();
    Net().PrintLoadProgress(0);
    CHECK(Logged("char .") && g_log.size() == 1);
    g_log.clear();
    for (int p : {3, 8, 10, 12, -4, -5})
        Net().PrintLoadProgress(p);
    CHECK(CountLogged("char") == 3);
    // 100: the end (once).
    g_log.clear();
    Net().PrintLoadProgress(100);
    CHECK(Logged("char .") && Logged("history") && Logged("line T22C9") && g_log.size() == 3);
    g_log.clear();
    Net().PrintLoadProgress(100);
    Net().PrintLoadProgress(8);
    CHECK(g_log.empty());
    g_currentMap = nullptr;
}

void TestCDKey()
{
    // The installer's key (HKLM ... Hidden & Dangerous 2 "key"), else none.
    HKEY key;
    char expected[0x40] = {};
    DWORD size = sizeof expected;
    bool present = false;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Illusion Softworks\\Hidden & Dangerous 2", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        present = RegQueryValueExA(key, "key", nullptr, nullptr, reinterpret_cast<BYTE *>(expected), &size) ==
                      ERROR_SUCCESS && size;
        RegCloseKey(key);
    }
    const char *got = GetCDKey();
    CHECK(present ? got && std::strcmp(got, expected) == 0 : got == nullptr);
    CHECK(GetCDKey() == got);
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestOpen();
    TestNoFirewall();
    TestRemoveFirst();
    TestNothingToClose();
    TestWatcher();
    TestBlocks();
    TestLoadProgress();
    TestCDKey();
    std::printf("firewall: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
