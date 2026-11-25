// Net part 12c: the network game's LAN search, its GameSpy reporting and the CD-key checks (net/net_search.cpp)
// against the original executable's. MinGW; build_search.bat (-include search_fakes.h); runs search_diff.exe
// ("search_diff: N checks, 0 failures").
//
// The port's net game is raw storage (net_game.cpp is not linked: the calls net_search.cpp makes into it are the
// test's), the original's its global (its zeroed storage: empty containers). Each round sets both alike (the role,
// the players, the key checks (the server's hundred, the client's), the key socket, the browser, the rules and the
// map, the profile's name, the clocks, the packets the key socket will receive, the browser's server) and runs up to 8
// of the file's functions. What they reach is the test's on both sides, logging alike: GameSpy (qr2's keys, buffers
// and init, gcd's calls, the browser's), the key socket (WSAStartup, socket, bind, select, recvfrom, sendto), Sleep,
// rand, the perf clock, the locks, the net game's other calls (LockPlayers, UnlockPlayers, CountPlayers, Close,
// EnumHosts), the main menu's OnServerFound, the server entries' reset, the CD key. After each call the result, the
// logs and the state are compared. HD2_SEARCH_ROUNDS: rounds (default 30,000; HD2_SEARCH_QUICK: 8,000);
// HD2_SEARCH_SEED.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/main_menu.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "net/gamespy.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#undef WSAStartup
#undef socket
#undef bind
#undef select
#undef recvfrom
#undef sendto
#undef Sleep
#undef rand

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);
static_assert(sizeof(S_KeyQuery) == 0xB8, "S_KeyQuery");

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
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
std::string Bytes(const void *data, int size)
{
    std::string s = N(size) + ":";
    const auto *b = static_cast<const uint8_t *>(data);
    for (int i = 0; i < size && i < 300; ++i) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}

// ---- the round's script ----
struct S_Packet {
    std::string data;   // (as received: XORed, the checksum after)
    uint32_t address;
    uint16_t port;
};
struct S_Script {
    bool game;
    int64_t perfNow;
    int perfStep;                    // the perf clock's step at each read
    std::deque<S_Packet> packets;    // the key socket's
    bool selectFails;
    int countPlayers;
    std::string hostname, mapname, gametype, gamever;
    bool hasHostname, hasMapname, hasGametype, hasGamever;
    int numplayers, maxplayers, hostport, password, isdedicated, voicechat, expansion;
    bool hasInts[7];
    uint32_t ping, address;
    unsigned randState;
} g_script;
// The test's objects GameSpy's calls are handed.
uint8_t g_browser[16], g_sbServer[16], g_buffer[16], g_keyBuffer[16];
std::string Obj(const void *p)
{
    if (!p)
        return "null";
    if (p == g_browser)
        return "browser";
    if (p == g_sbServer)
        return "sbserver";
    if (p == g_buffer)
        return "buffer";
    if (p == g_keyBuffer)
        return "keys";
    return "?";
}
std::string CallbackName(const void *p);

// ---- the fakes shared by both sides ----
extern "C" int WSAAPI FakeWSAStartup(WORD version, LPWSADATA)
{
    Log("WSAStartup " + Hex(version));
    return 0;
}
const SOCKET KEY_SOCKET = 0x3C3;
extern "C" SOCKET WSAAPI FakeSocket(int family, int type, int protocol)
{
    Log("socket " + N(family) + " " + N(type) + " " + N(protocol));
    return KEY_SOCKET;
}
std::string Address(const sockaddr *a)
{
    const auto *in = reinterpret_cast<const sockaddr_in *>(a);
    return N(in->sin_family) + "/" + Hex(in->sin_addr.s_addr) + ":" + N(ntohs(in->sin_port)) + "/" + Bytes(in->sin_zero, 8);
}
extern "C" int WSAAPI FakeBind(SOCKET s, const sockaddr *name, int size)
{
    Log("bind " + N(static_cast<long long>(s)) + " " + Address(name) + " " + N(size));
    return 0;
}
extern "C" int WSAAPI FakeSelect(int count, fd_set *readable, fd_set *writable, fd_set *errors, const timeval *wait)
{
    Log("select " + N(count) + " " + N(readable ? readable->fd_count : -1) + " " + N(writable ? writable->fd_count : -1) + " " +
        N(errors ? errors->fd_count : -1) + " " + (wait ? N(wait->tv_sec) + "." + N(wait->tv_usec) : "null"));
    if (g_script.selectFails)
        return -1;
    if (g_script.packets.empty()) {
        if (readable)
            readable->fd_count = 0;
        return 0;
    }
    return 1;
}
extern "C" int WSAAPI FakeRecvfrom(SOCKET s, char *data, int size, int flags, sockaddr *from, int *fromSize)
{
    Log("recvfrom " + N(static_cast<long long>(s)) + " " + N(size) + " " + N(flags) + " " + N(*fromSize));
    if (g_script.packets.empty())
        return -1;
    const S_Packet p = g_script.packets.front();
    g_script.packets.pop_front();
    const int n = std::min<int>(size, static_cast<int>(p.data.size()));
    std::memcpy(data, p.data.data(), static_cast<size_t>(n));
    auto *in = reinterpret_cast<sockaddr_in *>(from);
    in->sin_family = AF_INET;
    in->sin_addr.s_addr = p.address;
    in->sin_port = htons(p.port);
    return n;
}
extern "C" int WSAAPI FakeSendto(SOCKET s, const char *data, int size, int flags, const sockaddr *to, int toSize)
{
    Log("sendto " + N(static_cast<long long>(s)) + " " + Bytes(data, size) + " " + N(flags) + " " + Address(to) + " " + N(toSize));
    return size;
}
extern "C" void WINAPI FakeSleep(DWORD ms)
{
    if (!g_faking)
        return ::Sleep(ms);
    Log("Sleep " + N(ms));
    g_script.perfNow += ms;
}
extern "C" int FakeRand()
{
    Log("rand");
    g_script.randState = g_script.randState * 1103515245u + 12345u;
    return static_cast<int>((g_script.randState >> 16) & 0x7FFF);
}
long long PerfNow()
{
    Log("GetPerfTimeMs");
    const long long t = g_script.perfNow;
    g_script.perfNow += g_script.perfStep;
    return t;
}
// The critical sections: the net game's two by name (others real).
CRITICAL_SECTION *g_watched[2][2];
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
// GameSpy (the port's calls; the original's below with its conventions).
void QrRegisterKey(int key, const char *name) { Log("qr2_register_key " + N(key) + " " + Str(name)); }
void QrBufferAdd(void *buffer, const char *value) { Log("qr2_buffer_add " + Obj(buffer) + " " + Str(value)); }
void QrBufferAddInt(void *buffer, int value) { Log("qr2_buffer_add_int " + Obj(buffer) + " " + N(value)); }
void QrKeyBufferAdd(void *keys, int key) { Log("qr2_keybuffer_add " + Obj(keys) + " " + N(key)); }
std::string SbString(const char *key, const char *fallback)
{
    const struct {
        const char *name;
        const std::string *value;
        bool has;
    } k[] = {{"hostname", &g_script.hostname, g_script.hasHostname},
             {"mapname", &g_script.mapname, g_script.hasMapname},
             {"gametype", &g_script.gametype, g_script.hasGametype},
             {"gamever", &g_script.gamever, g_script.hasGamever}};
    for (const auto &x : k)
        if (std::strcmp(key, x.name) == 0)
            return x.has ? *x.value : fallback;
    return fallback;
}
int SbInt(const char *key, int fallback)
{
    const struct {
        const char *name;
        int value;
    } k[] = {{"numplayers", g_script.numplayers}, {"maxplayers", g_script.maxplayers}, {"hostport", g_script.hostport},
             {"password", g_script.password},     {"isdedicated", g_script.isdedicated}, {"voicechat", g_script.voicechat},
             {"expansion", g_script.expansion}};
    for (int i = 0; i < 7; ++i)
        if (std::strcmp(key, k[i].name) == 0)
            return g_script.hasInts[i] ? k[i].value : fallback;
    return fallback;
}
// (The strings handed back: kept until the round ends.)
std::deque<std::string> g_strings;
const char *Keep(const std::string &s)
{
    g_strings.push_back(s);
    return g_strings.back().c_str();
}

void qr2_register_key(int key, const char *name) { QrRegisterKey(key, name); }
int qr2_init(void *record, const char *ip, int basePort, const char *gameName, const char *secretKey, int isPublic, int natNegotiate,
             Qr2ServerKeyCallback server, Qr2PlayerKeyCallback player, Qr2TeamKeyCallback team, Qr2KeyListCallback keyList,
             Qr2CountCallback count, Qr2AddErrorCallback error, void *userData)
{
    Log("qr2_init " + N(record != nullptr) + " " + Str(ip) + " " + N(basePort) + " " + Str(gameName) + " " + Str(secretKey) + " " + N(isPublic) + " " +
        N(natNegotiate) + " " + CallbackName(reinterpret_cast<const void *>(server)) + " " + CallbackName(reinterpret_cast<const void *>(player)) + " " +
        CallbackName(reinterpret_cast<const void *>(team)) + " " + CallbackName(reinterpret_cast<const void *>(keyList)) + " " +
        CallbackName(reinterpret_cast<const void *>(count)) + " " + CallbackName(reinterpret_cast<const void *>(error)) + " " + N(userData != nullptr));
    return 0;
}
void qr2_think(void *record) { Log("qr2_think " + N(record != nullptr)); }
void qr2_buffer_add(S_Qr2Buffer *buffer, const char *value) { QrBufferAdd(buffer, value); }
void qr2_buffer_add_int(S_Qr2Buffer *buffer, int value) { QrBufferAddInt(buffer, value); }
void qr2_keybuffer_add(S_Qr2KeyBuffer *keys, int key) { QrKeyBufferAdd(keys, key); }
int gcd_init(void *record, int gameId)
{
    Log("gcd_init " + N(record != nullptr) + " " + N(gameId));
    return 0;
}
void gcd_think() { Log("gcd_think"); }
void gcd_authenticate_user(int gameId, int localId, uint32_t address, const char *challenge, const char *response, GcdAuthCallback callback,
                           void *instance);
void gcd_disconnect_user(int gameId, int localId) { Log("gcd_disconnect_user " + N(gameId) + " " + N(localId)); }
void gcd_compute_response(const char *cdKey, const char *challenge, char *response)
{
    Log("gcd_compute_response " + Str(cdKey) + " " + Str(challenge));
    std::snprintf(response, 0x4C, "R-%s", challenge);
}
S_ServerBrowser *ServerBrowserNew(const char *queryForGame, const char *queryFromGame, const char *queryFromKey, int queryFromVersion,
                                  int maxConcurrentUpdates, int queryVersion, ServerBrowserCallback callback, void *instance)
{
    Log("ServerBrowserNew " + Str(queryForGame) + " " + Str(queryFromGame) + " " + Str(queryFromKey) + " " + N(queryFromVersion) + " " +
        N(maxConcurrentUpdates) + " " + N(queryVersion) + " " + CallbackName(reinterpret_cast<const void *>(callback)) + " " + N(instance != nullptr));
    return reinterpret_cast<S_ServerBrowser *>(g_browser);
}
void ServerBrowserUpdate(S_ServerBrowser *browser, int async, int disconnectOnComplete, const uint8_t *fields, int fieldCount, const char *filter)
{
    Log("ServerBrowserUpdate " + Obj(browser) + " " + N(async) + " " + N(disconnectOnComplete) + " " + Bytes(fields, fieldCount) + " " + Str(filter));
}
int ServerBrowserThink(S_ServerBrowser *browser)
{
    Log("ServerBrowserThink " + Obj(browser));
    return 0;
}
void ServerBrowserClear(S_ServerBrowser *browser) { Log("ServerBrowserClear " + Obj(browser)); }
const char *SBServerGetStringValue(S_SbServer *server, const char *key, const char *fallback)
{
    Log("SBServerGetStringValue " + Obj(server) + " " + Str(key) + " " + Str(fallback));
    return Keep(SbString(key, fallback));
}
int SBServerGetIntValue(S_SbServer *server, const char *key, int fallback)
{
    Log("SBServerGetIntValue " + Obj(server) + " " + Str(key) + " " + N(fallback));
    return SbInt(key, fallback);
}
uint32_t SBServerGetPing(S_SbServer *server)
{
    Log("SBServerGetPing " + Obj(server));
    return g_script.ping;
}
uint32_t SBServerGetPublicInetAddress(S_SbServer *server)
{
    Log("SBServerGetPublicInetAddress " + Obj(server));
    return g_script.address;
}

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_versionMajor = 1, g_versionMinor = 3;
std::string EntryText(const S_ServerEntry *e)
{
    std::string s = std::string(e->name, strnlen(e->name, sizeof e->name)) + "|" + std::string(e->map, strnlen(e->map, sizeof e->map)) + "|";
    return s + Bytes(reinterpret_cast<const uint8_t *>(e) + 0x68, 0x9C - 0x68);
}
void __fastcall C_MainMenu::OnServerFound(S_ServerEntry *server)
{
    Log("OnServerFound " + EntryText(server));
    delete server;
}
void S_ServerEntry::Reset()
{
    Log("S_ServerEntry::Reset");
    std::memset(this, 0x5A, sizeof *this);
}
alignas(16) unsigned char g_pGame[0x400];
C_game *GetGame() { return g_script.game ? reinterpret_cast<C_game *>(g_pGame) : nullptr; }
alignas(16) unsigned char g_map[0x80];   // (the original's: the name's C_str at +1)
alignas(16) unsigned char g_pMap[sizeof(C_MpMap)];
C_MpMap *S_MultiplayerSettings::GetCurrentMap()
{
    Log("GetCurrentMap");
    return reinterpret_cast<C_MpMap *>(g_pMap);
}
const char *GetCDKey()
{
    Log("GetCDKey");
    return "TEST-CD-KEY";
}
int64_t GetPerfTimeMs() { return PerfNow(); }
// The net game's calls into its other files.
void C_NetLock::Enter() { FakeEnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(m_storage)); }
void C_NetLock::Leave() { FakeLeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(m_storage)); }
S_NetPlayerState *C_NetGame::LockPlayers()
{
    Log("LockPlayers");
    return m_players.data();
}
void C_NetGame::UnlockPlayers() { Log("UnlockPlayers"); }
int C_NetGame::CountPlayers()
{
    Log("CountPlayers");
    return g_script.countPlayers;
}
void C_NetGame::Close(bool now) { Log("Close " + N(now)); }
int C_NetGame::EnumHosts(S_ServerEntry *server, int request)
{
    Log("EnumHosts " + N(server != nullptr) + " " + N(request));
    return 0;
}
C_NetGame &PNet() { return g_netGame; }
S_NetPlayerState::S_NetPlayerState() = default;   // (net_game.cpp's: the test overwrites every byte)

void gcd_authenticate_user(int gameId, int localId, uint32_t address, const char *challenge, const char *response, GcdAuthCallback callback,
                           void *instance)
{
    Log("gcd_authenticate_user " + N(gameId) + " " + N(localId) + " " + Hex(address) + " " + Str(challenge) + " " + Str(response) + " " +
        CallbackName(reinterpret_cast<const void *>(callback)) + " " + N(instance == (g_side ? reinterpret_cast<void *>(0x0097d070 + 0x55C)
                                                                                              : static_cast<void *>(PNet().m_keyQueries))));
}

namespace {

// ================================================================================================================
// The original's world.
// ================================================================================================================
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
const uint32_t O_NET = 0x0097d070, O_ROLE = 0x00982100, O_PROFILE = 0x008aea0c, O_RANDOM_NAME = 0x0097d048;
const uint32_t O_VERSION_MAJOR = 0x00845070, O_VERSION_MINOR = 0x00845074;
uint8_t *ONet() { return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(O_NET)); }
uint8_t *OPlayers() { return OF<uint8_t *>(ONet(), 0x1E0); }
uint8_t g_oGame[0x400];

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void THISCALL ONoLock(void *) {}
int OAtexit(void *) { return 0; }
void FAST OQrRegisterKey(int key, const char *name) { QrRegisterKey(key, name); }
void FAST OQrBufferAdd(void *buffer, const char *value) { QrBufferAdd(buffer, value); }
void FAST OQrBufferAddInt(void *buffer, int value) { QrBufferAddInt(buffer, value); }
void FAST OQrKeyBufferAdd(void *keys, int key) { QrKeyBufferAdd(keys, key); }
int FAST OQrInit(void *record, const char *ip, int basePort, const char *gameName, const char *secretKey, int isPublic, int natNegotiate, void *server,
                 void *player, void *team, void *keyList, void *count, void *error, void *userData)
{
    Log("qr2_init " + N(record != nullptr) + " " + Str(ip) + " " + N(basePort) + " " + Str(gameName) + " " + Str(secretKey) + " " + N(isPublic) + " " +
        N(natNegotiate) + " " + CallbackName(server) + " " + CallbackName(player) + " " + CallbackName(team) + " " + CallbackName(keyList) + " " +
        CallbackName(count) + " " + CallbackName(error) + " " + N(userData != nullptr));
    return 0;
}
void FAST OQrThink(void *record) { Log("qr2_think " + N(record != nullptr)); }
int FAST OGcdInit(void *record, int gameId)
{
    Log("gcd_init " + N(record != nullptr) + " " + N(gameId));
    return 0;
}
void OGcdThink() { Log("gcd_think"); }
void FAST OGcdAuthenticate(int gameId, int localId, uint32_t address, const char *challenge, const char *response, void *callback, void *instance)
{
    Log("gcd_authenticate_user " + N(gameId) + " " + N(localId) + " " + Hex(address) + " " + Str(challenge) + " " + Str(response) + " " +
        CallbackName(callback) + " " + N(instance == reinterpret_cast<void *>(0x0097d070 + 0x55C)));
}
void FAST OGcdDisconnect(int gameId, int localId) { Log("gcd_disconnect_user " + N(gameId) + " " + N(localId)); }
void FAST OGcdComputeResponse(const char *cdKey, const char *challenge, char *response)
{
    Log("gcd_compute_response " + Str(cdKey) + " " + Str(challenge));
    std::snprintf(response, 0x4C, "R-%s", challenge);
}
void *FAST OBrowserNew(const char *queryForGame, const char *queryFromGame, const char *queryFromKey, int queryFromVersion, int maxConcurrentUpdates,
                       int queryVersion, void *callback, void *instance)
{
    Log("ServerBrowserNew " + Str(queryForGame) + " " + Str(queryFromGame) + " " + Str(queryFromKey) + " " + N(queryFromVersion) + " " +
        N(maxConcurrentUpdates) + " " + N(queryVersion) + " " + CallbackName(callback) + " " + N(instance != nullptr));
    return g_browser;
}
void FAST OBrowserUpdate(void *browser, int async, int disconnectOnComplete, const uint8_t *fields, int fieldCount, const char *filter)
{
    Log("ServerBrowserUpdate " + Obj(browser) + " " + N(async) + " " + N(disconnectOnComplete) + " " + Bytes(fields, fieldCount) + " " + Str(filter));
}
int FAST OBrowserThink(void *browser)
{
    Log("ServerBrowserThink " + Obj(browser));
    return 0;
}
void FAST OBrowserClear(void *browser) { Log("ServerBrowserClear " + Obj(browser)); }
const char *FAST OSbString(void *server, const char *key, const char *fallback)
{
    Log("SBServerGetStringValue " + Obj(server) + " " + Str(key) + " " + Str(fallback));
    return Keep(SbString(key, fallback));
}
int FAST OSbInt(void *server, const char *key, int fallback)
{
    Log("SBServerGetIntValue " + Obj(server) + " " + Str(key) + " " + N(fallback));
    return SbInt(key, fallback);
}
uint32_t FAST OSbPing(void *server)
{
    Log("SBServerGetPing " + Obj(server));
    return g_script.ping;
}
uint32_t FAST OSbAddress(void *server)
{
    Log("SBServerGetPublicInetAddress " + Obj(server));
    return g_script.address;
}
void FAST OOnServerFound(S_ServerEntry *server)
{
    Log("OnServerFound " + EntryText(server));
    ODelete(server);
}
void *FAST OServerEntryReset(void *self)
{
    Log("S_ServerEntry::Reset");
    std::memset(self, 0x5A, sizeof(S_ServerEntry));
    return self;
}
void *OGetGame() { return g_script.game ? g_oGame : nullptr; }
void *FAST OGetCurrentMap(void *)
{
    Log("GetCurrentMap");
    return g_map;
}
const char *OGetCDKey()
{
    Log("GetCDKey");
    return "TEST-CD-KEY";
}
long long OGetPerfTimeMs() { return PerfNow(); }
void *FAST OLockPlayers(void *)
{
    Log("LockPlayers");
    return ONet() + 0x1DC;
}
void FAST OUnlockPlayers(void *) { Log("UnlockPlayers"); }
int FAST OCountPlayers(void *)
{
    Log("CountPlayers");
    return g_script.countPlayers;
}
void THISCALL OClose(void *, bool now) { Log("Close " + N(now)); }
int THISCALL OEnumHosts(void *, S_ServerEntry *server, int request)
{
    Log("EnumHosts " + N(server != nullptr) + " " + N(request));
    return 0;
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
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 500) + "\n     port: " + y.substr(0, 500);
            break;
        }
    }
    Check(false, s);
}

void CompareState()
{
    C_NetGame &net = PNet();
    std::string s;
    for (int i = 0; i < 100; ++i)
        if (std::memcmp(&net.m_keyQueries[i], ONet() + 0x55C + 0xB8 * i, 0xB8) != 0) {
            s += "key query " + N(i) + " " + Bytes(ONet() + 0x55C + 0xB8 * i, 0xB8) + " / " + Bytes(&net.m_keyQueries[i], 0xB8) + "; ";
            break;
        }
    if (std::memcmp(&net.m_keyClient, ONet() + 0x4D40, 0xB8) != 0)
        s += "the client's key " + Bytes(ONet() + 0x4D40, 0xB8) + " / " + Bytes(&net.m_keyClient, 0xB8) + "; ";
    if (net.m_keySocket != OF<uintptr_t>(ONet(), 0x4D3C))
        s += "the key socket; ";
    if (net.m_reporting != (OF<uint8_t>(ONet(), 0x4DF8) != 0))
        s += "reporting; ";
    if (Obj(net.m_serverBrowser) != Obj(OF<void *>(ONet(), 0x4E4)))
        s += "the browser; ";
    if (net.m_joinStatus != OF<int>(ONet(), 0x5044))
        s += "the join status " + N(OF<int>(ONet(), 0x5044)) + " / " + N(net.m_joinStatus) + "; ";
    if (reinterpret_cast<void *>(net.m_searchCallback) != OF<void *>(ONet(), 0x4EC))
        s += "the search callback; ";
    if (g_gameRole != G<uint32_t>(O_ROLE))
        s += "the role; ";
    if (std::memcmp(g_randomName, reinterpret_cast<void *>(static_cast<uintptr_t>(O_RANDOM_NAME)), sizeof g_randomName) != 0)
        s += "the random name; ";
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
    const S_Script saved = g_script;   // (each side's packets, clocks, rand)
    const long long x = port();
    g_side = 1;
    g_script = saved;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
}

// A key packet as sent: the text XORed with 0xA0, the checksum of those bytes after (a wrong one sometimes).
S_Packet MakePacket(const std::string &text, uint32_t address, uint16_t port)
{
    std::string d = text;
    for (char &c : d)
        c = static_cast<char>(c ^ 0xA0);
    int sum = 0;
    for (char c : d) {
        const int b = static_cast<signed char>(c);
        sum += b << 16 | b;
    }
    if (P(0.1))
        sum += 1;
    d.append(reinterpret_cast<const char *>(&sum), 4);
    return {d, address, port};
}
std::string RandomPlayerName()
{
    static const char *names[] = {"P1", "P2", "p1", "Bob", "Bob2", "x", ""};
    std::string n = names[I(0, 6)];
    // (48 at most: the original copies a packet's name unbounded; with a number after it, it overruns a check's name.)
    if (P(0.05))
        n = std::string(static_cast<size_t>(I(30, 48)), 'q');
    return n;
}

void SetKeyQuery(S_KeyQuery &q, void *o)
{
    auto *raw = reinterpret_cast<uint8_t *>(&q);
    for (size_t i = 0; i < sizeof q; ++i)
        raw[i] = static_cast<uint8_t>(I(0, 255));
    std::snprintf(q.challenge, sizeof q.challenge, "ch%d", I(0, 99));
    std::snprintf(q.response, sizeof q.response, "re%d", I(0, 99));
    std::snprintf(q.name, sizeof q.name, "%s", P(0.3) ? "" : RandomPlayerName().c_str());
    static const uint32_t addresses[] = {0x0100007F, 0x0200A8C0, 0x0300A8C0};
    q.address = addresses[I(0, 2)];
    q.port = I(9000, 9002);
    q.state = I(0, 7);
    q.expiry = static_cast<int>(g_script.perfNow) + (P(0.3) ? I(-20000, -1) : I(0, 20000));
    q.lastSend = static_cast<int>(g_script.perfNow) - I(0, 400);
    std::memcpy(o, &q, sizeof q);
}

void SetWorld()
{
    C_NetGame &net = PNet();
    g_strings.clear();
    static const uint32_t roles[] = {7, 1, 2, 3, 0x12};
    g_gameRole = roles[I(0, 4)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    S_Script &w = g_script;
    w.game = P(0.8);
    w.perfNow = I(100000, 900000);
    w.perfStep = P(0.5) ? 0 : I(1, 300);
    w.selectFails = P(0.1);
    w.countPlayers = I(0, 12);
    w.randState = static_cast<unsigned>(I(0, 0x7FFFFFFF));
    static const char *types[] = {"occupation", "Objectives", "COOPERATIVE", "deathmatch", "other", ""};
    // (Within their fields: the original copies a server's strings unbounded, a longer one overruns the entry; the
    // port's Fix cuts them.)
    w.hostname = P(0.1) ? std::string(static_cast<size_t>(I(40, 50)), 'h') : "Host" + N(I(0, 9));
    w.mapname = P(0.1) ? std::string(static_cast<size_t>(I(40, 52)), 'm') : "Map" + N(I(0, 9));
    w.gametype = P(0.05) ? std::string(static_cast<size_t>(I(30, 39)), 'o') : types[I(0, 5)];
    static const char *versions[] = {"1.1", "1.3", "2.15", "x", "7", ""};
    w.gamever = versions[I(0, 5)];
    w.hasHostname = P(0.9);
    w.hasMapname = P(0.9);
    w.hasGametype = P(0.9);
    w.hasGamever = P(0.9);
    w.numplayers = I(0, 32);
    w.maxplayers = I(0, 32);
    w.hostport = I(0, 70000);
    w.password = I(0, 2);
    w.isdedicated = I(0, 2);
    w.voicechat = I(0, 2);
    w.expansion = I(0, 3);
    for (bool &b : w.hasInts)
        b = P(0.9);
    w.ping = static_cast<uint32_t>(I(0, 999));
    w.address = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
    // The packets the key socket will receive.
    w.packets.clear();
    static const uint32_t addresses[] = {0x0100007F, 0x0200A8C0, 0x0300A8C0};
    for (int k = I(0, 4); k > 0; --k) {
        const uint32_t address = addresses[I(0, 2)];
        const auto port = static_cast<uint16_t>(I(9000, 9002));
        const int kind = I(0, 7);
        std::string text;
        if (kind == 0)
            // (49 to 51 letters no player has: cut at the name's room, no number after it.)
            text = "c:" + (P(0.1) ? std::string(static_cast<size_t>(I(49, 51)), 'w') : RandomPlayerName());
        else if (kind == 1)
            text = "r:" + (P(0.3) ? std::string(static_cast<size_t>(I(1, 2)), 'x') : "re" + N(I(0, 99))) + ":" + RandomPlayerName();
        else if (kind == 2)
            text = "c:ch" + N(I(0, 99));
        else if (kind == 3)
            text = std::string(1, "meu"[I(0, 2)]);
        else
            text = "zz" + N(I(0, 9));
        // (Sent with their NUL: one without would be read on into the receiver's stack, as would a response without
        // its ':', whose name is the original's stack's.)
        text.push_back('\0');
        w.packets.push_back(MakePacket(text, address, port));
    }
    // The rules, the map, the profile's name.
    S_MultiplayerSettings &mp = *reinterpret_cast<C_GameProfile &>(g_gameProfile).m_mpSettings;
    std::memset(&mp, 0, 0x300);
    std::snprintf(mp.sessionName, sizeof mp.sessionName, "Session%d", I(0, 9));
    mp.port = I(0, 70000);
    mp.gameType = I(0, 4);
    mp.maxClients = I(0, 32);
    static C_str passwords[2] = {C_str(""), C_str("pw")};
    new (&mp.password) C_str(passwords[I(0, 1)]);
    mp.dedicated = P(0.5);
    mp.unknown174 = P(0.5) ? 1 : 0;
    mp.unknown178 = I(0, 3);
    static C_str maps[3] = {C_str(""), C_str("Desert"), C_str("Snow")};
    // (The original's map: its name at +1; the port's object its own layout's.)
    std::memset(g_map, 0, sizeof g_map);
    std::memset(g_pMap, 0, sizeof g_pMap);
    if (P(0.9)) {
        const C_str &name = maps[I(0, 2)];
        new (g_map + 1) C_str(name);
        new (g_pMap + offsetof(C_MpMap, name)) C_str(name);
    }
    static C_str players[3] = {C_str("Me"), C_str("LongNameOfMine"), C_str("")};
    reinterpret_cast<C_GameProfile &>(g_gameProfile).m_player->m_name = players[I(0, 2)];
    g_versionMajor = I(0, 3);
    g_versionMinor = I(0, 30);
    G<int>(O_VERSION_MAJOR) = g_versionMajor;
    G<int>(O_VERSION_MINOR) = g_versionMinor;
    // The players (the same bytes).
    const int slots = P(0.1) ? 0 : I(1, 40);
    net.m_players.assign(static_cast<size_t>(slots), S_NetPlayerState());
    ODelete(OPlayers());
    uint8_t *op = slots ? static_cast<uint8_t *>(ONew(static_cast<size_t>(slots) * 0xC4)) : nullptr;
    OF<uint8_t *>(ONet(), 0x1E0) = op;
    OF<uint8_t *>(ONet(), 0x1E4) = op ? op + slots * 0xC4 : nullptr;
    OF<uint8_t *>(ONet(), 0x1E8) = op ? op + slots * 0xC4 : nullptr;
    for (int s = 0; s < slots; ++s) {
        S_NetPlayerState &p = net.m_players[static_cast<size_t>(s)];
        auto *raw = reinterpret_cast<uint8_t *>(&p);
        for (int k = 0; k < 0xC4; ++k)
            raw[k] = static_cast<uint8_t>(I(0, 255));
        std::memset(p.name, 0, sizeof p.name);
        if (P(0.7))
            std::snprintf(p.name, sizeof p.name, "%s", RandomPlayerName().c_str());
        p.human = nullptr;
        p.link = nullptr;
        std::memcpy(op + s * 0xC4, raw, 0xC4);
    }
    // The key checks, the socket, the browser, the rest.
    for (int i = 0; i < 100; ++i)
        SetKeyQuery(net.m_keyQueries[i], ONet() + 0x55C + 0xB8 * i);
    SetKeyQuery(net.m_keyClient, ONet() + 0x4D40);
    // (A response for a challenged check, query 0 often: its name, address and port.)
    if (P(0.4)) {
        const int i = P(0.3) ? 0 : I(1, 99);
        S_KeyQuery &q = net.m_keyQueries[i];
        std::snprintf(q.name, sizeof q.name, "K%d", i);
        q.state = KEY_CHALLENGED;
        std::memcpy(ONet() + 0x55C + 0xB8 * i, &q, sizeof q);
        g_script.packets.push_front(MakePacket("r:" + std::string(static_cast<size_t>(I(1, 3)), 'x') + ":K" + N(i) + std::string(1, '\0'), q.address,
                                               static_cast<uint16_t>(q.port)));
    }
    net.m_keySocket = KEY_SOCKET;
    OF<uintptr_t>(ONet(), 0x4D3C) = KEY_SOCKET;
    net.m_reporting = P(0.5);
    OF<uint8_t>(ONet(), 0x4DF8) = net.m_reporting;
    const bool browser = P(0.5);
    net.m_serverBrowser = browser ? reinterpret_cast<S_ServerBrowser *>(g_browser) : nullptr;
    OF<void *>(ONet(), 0x4E4) = browser ? g_browser : nullptr;
    net.m_joinStatus = I(0, 9);
    OF<int>(ONet(), 0x5044) = net.m_joinStatus;
    net.m_searchCallback = nullptr;
    OF<void *>(ONet(), 0x4EC) = nullptr;
    for (int t = 0; t < 3; ++t) {
        net.m_teamScores[t] = static_cast<float>(I(-5, 50)) + (P(0.3) ? 0.5f : 0.0f);
        OF<float>(ONet(), 0x44C + 4 * t) = net.m_teamScores[t];
    }
    std::memset(g_randomName, 'z', sizeof g_randomName);
    std::memset(reinterpret_cast<void *>(static_cast<uintptr_t>(O_RANDOM_NAME)), 'z', sizeof g_randomName);
}

void __fastcall TestSearchCallback(S_ServerEntry *) {}

void Call()
{
    C_NetGame &net = PNet();
    void *on = ONet();
    const int op = I(0, 23);
    switch (op) {
    case 0: {
        const int reason = P(0.7) ? 1 : I(0, 3);
        Run("OnBrowserCallback " + N(reason), [&] { OnBrowserCallback(reinterpret_cast<S_ServerBrowser *>(g_browser), reason, reinterpret_cast<S_SbServer *>(g_sbServer), nullptr); return 0; },
            [&] { O_CALL(0x00723190, void(FAST *)(void *, int, void *, void *))(g_browser, reason, g_sbServer, nullptr); return 0; });
        break;
    }
    case 1:
        Run("AddBrowserServer", [&] { AddBrowserServer(reinterpret_cast<S_SbServer *>(g_sbServer)); return 0; },
            [&] { O_CALL(0x007231b0, void(__stdcall *)(void *))(g_sbServer); return 0; });
        break;
    case 2:
        Run("StartLanSearch", [&] { return static_cast<long long>(net.StartLanSearch()); },
            [&] { return static_cast<long long>(O_CALL(0x00723620, int(FAST *)(void *))(on)); });
        break;
    case 3:
        Run("PollSearch", [&] { net.PollSearch(); return 0; }, [&] { O_CALL(0x00723610, void(FAST *)(void *))(on); return 0; });
        break;
    case 4:
        Run("StartSearch", [&] { return static_cast<long long>(net.StartSearch()); },
            [&] { return static_cast<long long>(O_CALL(0x00724010, int(FAST *)(void *))(on)); });
        break;
    case 5:
        Run("SearchServers", [&] { net.SearchServers(TestSearchCallback); return 0; },
            [&] { O_CALL(0x00724040, void(THISCALL *)(void *, void *))(on, reinterpret_cast<void *>(TestSearchCallback)); return 0; });
        break;
    case 6: {
        const int key = P(0.8) ? I(0, 0x36) : I(-3, 100);
        Run("ReportServerKey " + N(key), [&] { ReportServerKey(key, reinterpret_cast<S_Qr2Buffer *>(g_buffer), nullptr); return 0; },
            [&] { O_CALL(0x007237d0, void(FAST *)(int, void *, void *))(key, g_buffer, nullptr); return 0; });
        break;
    }
    case 7: {
        static const int keys[] = {QR2_PLAYER, QR2_SCORE, QR2_PING, QR2_TEAM, QR2_DEATHS, QR2_SKILL, 0, 99};
        int index = P(0.3) ? I(13, 40) : I(-1, 12);
        const int key = keys[I(0, 7)];
        // (Sometimes the first named player past the 32 slots read: none is reported.)
        if (net.m_players.size() > 32 && P(0.3)) {
            int named = 0;
            for (int slot = 0; slot < 32; ++slot)
                named += net.m_players[static_cast<size_t>(slot)].name[0] != 0;
            index = named;
        }
        // (Fix of the port: not past the table; the original reads 32 slots whatever its size: the test gives 32 or more,
        // or the index found first.)
        if (net.m_players.size() < 32) {
            int named = 0;
            bool found = false;
            for (const S_NetPlayerState &p : net.m_players)
                if (p.name[0] && named++ == index)
                    found = true;
            if (!found)
                return;
        }
        Run("ReportPlayerKey " + N(key) + " " + N(index), [&] { ReportPlayerKey(key, index, reinterpret_cast<S_Qr2Buffer *>(g_buffer), nullptr); return 0; },
            [&] { O_CALL(0x00723a40, void(FAST *)(int, int, void *, void *))(key, index, g_buffer, nullptr); return 0; });
        break;
    }
    case 8: {
        const int key = P(0.8) ? I(QR2_TEAM_NAME, QR2_TEAM_SCORE) : I(0, 40), index = I(0, 2);
        Run("ReportTeamKey " + N(key) + " " + N(index), [&] { ReportTeamKey(key, index, reinterpret_cast<S_Qr2Buffer *>(g_buffer), nullptr); return 0; },
            [&] { O_CALL(0x00723b70, void(FAST *)(int, int, void *, void *))(key, index, g_buffer, nullptr); return 0; });
        break;
    }
    case 9: {
        const int type = I(-1, 3);
        Run("ReportKeyList " + N(type), [&] { ReportKeyList(type, reinterpret_cast<S_Qr2KeyBuffer *>(g_keyBuffer), nullptr); return 0; },
            [&] { O_CALL(0x00723bb0, void(FAST *)(int, void *, void *))(type, g_keyBuffer, nullptr); return 0; });
        break;
    }
    case 10: {
        const int type = I(-1, 3);
        if (P(0.5))
            Run("ReportCount " + N(type), [&] { return static_cast<long long>(ReportCount(type, nullptr)); },
                [&] { return static_cast<long long>(O_CALL(0x00723cc0, int(FAST *)(int, void *))(type, nullptr)); });
        else
            Run("ReportError", [&] { ReportError(type, nullptr, nullptr); return 0; },
                [&] { O_CALL(0x00723cf0, void(FAST *)(int, char *, void *))(type, nullptr, nullptr); return 0; });
        break;
    }
    case 11:
        Run("StartReporting", [&] { net.StartReporting(); return 0; }, [&] { O_CALL(0x00723d20, void(FAST *)(void *))(on); return 0; });
        break;
    case 12:
        Run("AnswerQueries", [&] { net.AnswerQueries(); return 0; }, [&] { O_CALL(0x00723dc0, void(FAST *)(void *))(on); return 0; });
        break;
    case 13:
        Run("OpenKeyServer", [&] { net.OpenKeyServer(); return 0; }, [&] { O_CALL(0x00730f00, void(FAST *)(void *))(on); return 0; });
        break;
    case 14: {
        char data[2][256];
        std::memset(data, 0x33, sizeof data);
        int size[2] = {256, 256};
        uint32_t address[2] = {1, 1}, port[2] = {2, 2};
        Run("ReceiveKeyPacket", [&] { return static_cast<long long>(net.ReceiveKeyPacket(data[0], &size[0], &address[0], &port[0])); },
            [&] {
                return static_cast<long long>(
                    O_CALL(0x00730fa0, bool(THISCALL *)(void *, char *, int *, uint32_t *, uint32_t *))(on, data[1], &size[1], &address[1], &port[1]));
            });
        Check(std::memcmp(data[0], data[1], 256) == 0 && size[0] == size[1] && address[0] == address[1] && port[0] == port[1], "the packet read");
        break;
    }
    case 15: {
        char data[64];
        const int size = I(1, 60);
        for (int i = 0; i < size; ++i)
            data[i] = static_cast<char>(I(0, 255));
        const uint32_t address = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
        const auto port = static_cast<uint16_t>(I(0, 65535));
        Run("SendKeyPacket " + N(size), [&] { net.SendKeyPacket(data, size, address, port); return 0; },
            [&] { O_CALL(0x007310e0, void(THISCALL *)(void *, const char *, int, uint32_t, uint32_t))(on, data, size, address, port); return 0; });
        break;
    }
    case 16: {
        const int length = P(0.8) ? I(0, 32) : I(33, 60);
        Run("RandomName " + N(length), [&] { return static_cast<long long>(std::strlen(RandomName(length))); },
            [&] { return static_cast<long long>(std::strlen(O_CALL(0x007311b0, const char *(FAST *)(int))(length))); });
        break;
    }
    case 17: {
        const int localId = I(0, 99), authenticated = I(0, 1);
        char message[2][8] = {"Clash", "Clash"};
        if (P(0.5))
            message[0][0] = message[1][0] = 'x';
        const int gameId = I(0, 0x300);
        Run("OnKeyChecked " + N(localId) + " " + N(authenticated), [&] { OnKeyChecked(gameId, localId, authenticated, message[0], nullptr); return 0; },
            [&] { O_CALL(0x007311f0, void(FAST *)(int, int, int, char *, void *))(gameId, localId, authenticated, message[1], nullptr); return 0; });
        break;
    }
    case 18: {
        const std::string name = RandomPlayerName();
        const int number = I(-1, 4);
        Run("FindFreeNameNumber " + name + " " + N(number), [&] { return static_cast<long long>(net.FindFreeNameNumber(name.c_str(), number)); },
            [&] { return static_cast<long long>(O_CALL(0x00724c20, int(THISCALL *)(void *, const char *, int))(on, name.c_str(), number)); });
        break;
    }
    case 19:
    case 20:
        Run("ProcessKeyQueries", [&] { net.ProcessKeyQueries(); return 0; }, [&] { O_CALL(0x00731250, void(FAST *)(void *))(on); return 0; });
        break;
    case 21: {
        static S_ServerEntry server;
        std::memset(&server, 0, sizeof server);
        server.address = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
        server.port = I(-5, 70000);
        Run("StartKeyQuery", [&] { net.StartKeyQuery(&server); return 0; },
            [&] { O_CALL(0x007316f0, void(THISCALL *)(void *, const S_ServerEntry *))(on, &server); return 0; });
        break;
    }
    case 22:
        // (It loops until the expiry: the clock moves on.)
        if (g_script.perfStep == 0)
            g_script.perfStep = 7;
        Run("RunKeyQuery", [&] { return static_cast<long long>(net.RunKeyQuery()); },
            [&] { return static_cast<long long>(O_CALL(0x007317a0, bool(FAST *)(void *))(on)); });
        break;
    default: {
        const int index = P(0.8) ? I(0, 99) : (P(0.5) ? -1 : 100);
        Run("DropKeyQuery " + N(index), [&] { net.DropKeyQuery(index); return 0; },
            [&] { O_CALL(0x00731990, void(THISCALL *)(void *, int))(on, index); return 0; });
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

std::string CallbackName(const void *p)
{
    if (!p)
        return "null";
    const struct {
        const void *port;
        uint32_t orig;
        const char *name;
    } known[] = {{reinterpret_cast<const void *>(&OnBrowserCallback), 0x00723190, "OnBrowserCallback"},
                 {reinterpret_cast<const void *>(&ReportServerKey), 0x007237d0, "ReportServerKey"},
                 {reinterpret_cast<const void *>(&ReportPlayerKey), 0x00723a40, "ReportPlayerKey"},
                 {reinterpret_cast<const void *>(&ReportTeamKey), 0x00723b70, "ReportTeamKey"},
                 {reinterpret_cast<const void *>(&ReportKeyList), 0x00723bb0, "ReportKeyList"},
                 {reinterpret_cast<const void *>(&ReportCount), 0x00723cc0, "ReportCount"},
                 {reinterpret_cast<const void *>(&ReportError), 0x00723cf0, "ReportError"},
                 {reinterpret_cast<const void *>(&OnKeyChecked), 0x007311f0, "OnKeyChecked"}};
    for (const auto &k : known)
        if (p == (g_side ? reinterpret_cast<const void *>(static_cast<uintptr_t>(k.orig)) : k.port))
            return k.name;
    return "?";
}

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
        std::printf("search_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // The profile (both sides' settings and player profile), the locks.
    static uint8_t mp[0x400], player[0x40];
    reinterpret_cast<C_GameProfile &>(g_gameProfile).m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(mp);
    reinterpret_cast<C_GameProfile &>(g_gameProfile).m_player = reinterpret_cast<C_PlayerProfile *>(player);
    G<void *>(O_PROFILE + 0x24) = mp;
    G<void *>(O_PROFILE + 0x20) = player;
    g_watched[0][0] = reinterpret_cast<CRITICAL_SECTION *>(&PNet().m_playersLock);
    g_watched[0][1] = reinterpret_cast<CRITICAL_SECTION *>(&PNet().m_queueLock);
    g_watched[1][0] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x418);
    g_watched[1][1] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x430);

    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(OAtexit));
    RedirectOriginal(0x007e6393, reinterpret_cast<const void *>(FakeRand));
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00739750, reinterpret_cast<const void *>(OQrRegisterKey)},
        {0x00738830, reinterpret_cast<const void *>(OQrBufferAdd)},
        {0x00738800, reinterpret_cast<const void *>(OQrBufferAddInt)},
        {0x007387d0, reinterpret_cast<const void *>(OQrKeyBufferAdd)},
        {0x00738500, reinterpret_cast<const void *>(OQrInit)},
        {0x007385d0, reinterpret_cast<const void *>(OQrThink)},
        {0x00733cb0, reinterpret_cast<const void *>(OGcdInit)},
        {0x00734040, reinterpret_cast<const void *>(OGcdThink)},
        {0x00733d80, reinterpret_cast<const void *>(OGcdAuthenticate)},
        {0x00733f40, reinterpret_cast<const void *>(OGcdDisconnect)},
        {0x00733af0, reinterpret_cast<const void *>(OGcdComputeResponse)},
        {0x0073ab70, reinterpret_cast<const void *>(OBrowserNew)},
        {0x0073af00, reinterpret_cast<const void *>(OBrowserUpdate)},
        {0x0073b240, reinterpret_cast<const void *>(OBrowserThink)},
        {0x0073b280, reinterpret_cast<const void *>(OBrowserClear)},
        {0x0073a3f0, reinterpret_cast<const void *>(OSbString)},
        {0x0073a420, reinterpret_cast<const void *>(OSbInt)},
        {0x0073aa10, reinterpret_cast<const void *>(OSbPing)},
        {0x0073a6a0, reinterpret_cast<const void *>(OSbAddress)},
        {0x00635b70, reinterpret_cast<const void *>(OOnServerFound)},
        {0x007252b0, reinterpret_cast<const void *>(OServerEntryReset)},
        {0x007252c0, reinterpret_cast<const void *>(OServerEntryReset)},
        {0x006987d0, reinterpret_cast<const void *>(OGetGame)},
        {0x006ad830, reinterpret_cast<const void *>(OGetCurrentMap)},
        {0x00732000, reinterpret_cast<const void *>(OGetCDKey)},
        {0x00748dc0, reinterpret_cast<const void *>(OGetPerfTimeMs)},
        {0x0072f040, reinterpret_cast<const void *>(OLockPlayers)},
        {0x0072f100, reinterpret_cast<const void *>(OUnlockPlayers)},
        {0x00730410, reinterpret_cast<const void *>(OCountPlayers)},
        {0x0072e8b0, reinterpret_cast<const void *>(OClose)},
        {0x00723e80, reinterpret_cast<const void *>(OEnumHosts)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    RedirectApi("kernel32.dll", "EnterCriticalSection", reinterpret_cast<const void *>(&FakeEnterCriticalSection));
    RedirectApi("kernel32.dll", "LeaveCriticalSection", reinterpret_cast<const void *>(&FakeLeaveCriticalSection));
    RedirectApi("kernel32.dll", "Sleep", reinterpret_cast<const void *>(&FakeSleep));
    const char *sockets[] = {"ws2_32.dll", "wsock32.dll"};
    for (const char *dll : sockets) {
        HMODULE m = LoadLibraryA(dll);
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "WSAStartup")), reinterpret_cast<const void *>(&FakeWSAStartup));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "socket")), reinterpret_cast<const void *>(&FakeSocket));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "bind")), reinterpret_cast<const void *>(&FakeBind));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "select")), reinterpret_cast<const void *>(&FakeSelect));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "recvfrom")), reinterpret_cast<const void *>(&FakeRecvfrom));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "sendto")), reinterpret_cast<const void *>(&FakeSendto));
    }

    int rounds = std::getenv("HD2_SEARCH_QUICK") ? 8000 : 30000;
    if (const char *r = std::getenv("HD2_SEARCH_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_SEARCH_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        g_trace.clear();
        SetWorld();
        for (int k = I(1, 8); k > 0; --k)
            Call();
    }
    std::printf("search_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
