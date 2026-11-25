// Net part 13c: GameSpy's query & reporting 2 (net/gamespy_qr2.cpp: the record made, its socket, the heartbeats and
// keepalives, the queries' replies (by keys and the old "\status\" form), the master's challenge, the echo, the add
// error, the client messages, the think, the shutdown, the buffers and keys) against the original executable's.
// MinGW; build_qr2.bat; runs qr2_diff.exe ("qr2_diff: N checks, 0 failures"). The port's file is included here (its
// state and helpers are its own TU's), so the mutation runner rebuilds this file (MUT_WRAPPER=qr2_diff.cpp).
//
// The original's SDK is __fastcall, and so are the game's callbacks it calls (the server's, players' and teams' keys,
// the key list, the counts, the add error, the CD key's packets): each side gets its own, logging alike and answering
// from the round's script. Each round makes the same record on both sides (the default one or a test's: its socket,
// names, instance key, times, listing state, flags, master address, message keys), the machine's addresses, the
// clock, the random numbers, the packets the socket will receive, and runs up to 8 calls; after each the result, the
// logs (sendto's, the callbacks', the sockets', the clock's) and the records are compared. HD2_QR2_ROUNDS: rounds
// (default 30,000; HD2_QR2_QUICK: 8,000); HD2_QR2_SEED.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

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
std::string Bytes(const void *data, int size)
{
    if (!data)
        return "null";
    std::string s = N(size) + ":";
    const auto *b = static_cast<const uint8_t *>(data);
    for (int i = 0; i < size && i < 600; ++i) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }

// ---- the script ----
struct S_Script {
    uint32_t ticks, step;
    unsigned randState;
    std::deque<std::string> packets;  // the socket's
    std::vector<uint32_t> hostIps;    // the machine's (gethostbyname of its name)
    bool hostFound, masterFound;
    int bindFailures;                 // binds that fail before one works (100: none works)
    bool socketFails;
    bool socknameFails;
    int counts[3];                    // the counts callback's (players, teams)
    std::vector<int> keys[3];         // the key list callback's
    std::vector<bool> answers;        // whether the key callbacks add a value (cycled)
    int answer;
} g_script;

// ---- the system calls (both sides') ----
extern "C" DWORD WINAPI FakeGetTickCount()
{
    Log("GetTickCount");
    const uint32_t t = g_script.ticks;
    g_script.ticks += g_script.step;
    return t;
}
extern "C" int FakeRand()
{
    Log("rand");
    g_script.randState = g_script.randState * 214013u + 2531011u;
    return static_cast<int>((g_script.randState >> 16) & 0x7FFF);
}
extern "C" void FakeSrand(unsigned seed)
{
    Log("srand " + Hex(seed));
    g_script.randState = seed;
}
extern "C" int WSAAPI FakeSelect(int count, fd_set *readable, fd_set *, fd_set *, const timeval *)
{
    Log("select " + N(count) + " " + N(readable ? static_cast<long long>(readable->fd_count) : -1));
    return g_script.packets.empty() ? 0 : 1;
}
extern "C" int WSAAPI FakeRecvfrom(SOCKET s, char *data, int size, int flags, sockaddr *from, int *fromSize)
{
    Log("recvfrom " + N(static_cast<long long>(s)) + " " + N(size) + " " + N(flags) + " " + N(*fromSize));
    const std::string p = g_script.packets.front();
    g_script.packets.pop_front();
    if (p == "!")
        return -1;
    const int n = std::min<int>(size, static_cast<int>(p.size()));
    std::memcpy(data, p.data(), static_cast<size_t>(n));
    auto *in = reinterpret_cast<sockaddr_in *>(from);
    std::memset(in, 0, sizeof *in);
    in->sin_family = AF_INET;
    in->sin_addr.s_addr = 0x0A0B0C0D;
    in->sin_port = htons(4242);
    return n;
}
std::string Address(const sockaddr *a)
{
    const auto *in = reinterpret_cast<const sockaddr_in *>(a);
    return N(in->sin_family) + "/" + Hex(in->sin_addr.s_addr) + ":" + N(ntohs(in->sin_port));
}
extern "C" int WSAAPI FakeSendto(SOCKET s, const char *data, int size, int flags, const sockaddr *to, int toSize)
{
    Log("sendto " + N(static_cast<long long>(s)) + " " + Bytes(data, size) + " " + N(flags) + " " + Address(to) + " " + N(toSize));
    return size;
}
extern "C" SOCKET WSAAPI FakeSocket(int family, int type, int protocol)
{
    Log("socket " + N(family) + " " + N(type) + " " + N(protocol));
    return g_script.socketFails ? INVALID_SOCKET : static_cast<SOCKET>(0x2A2);
}
int g_binds;
extern "C" int WSAAPI FakeBind(SOCKET s, const sockaddr *name, int size)
{
    Log("bind " + N(static_cast<long long>(s)) + " " + Address(name) + " " + N(size));
    return g_binds++ < g_script.bindFailures ? SOCKET_ERROR : 0;
}
extern "C" int WSAAPI FakeGetsockname(SOCKET s, sockaddr *name, int *size)
{
    Log("getsockname " + N(static_cast<long long>(s)) + " " + N(*size));
    auto *in = reinterpret_cast<sockaddr_in *>(name);
    in->sin_family = AF_INET;
    in->sin_port = htons(51234);
    in->sin_addr.s_addr = 0;
    return g_script.socknameFails ? SOCKET_ERROR : 0;
}
extern "C" int WSAAPI FakeGethostname(char *name, int size)
{
    Log("gethostname " + N(size));
    std::snprintf(name, static_cast<size_t>(size), "testhost");
    return 0;
}
hostent g_host;
char *g_hostList[8];
uint32_t g_hostIps[8];
extern "C" hostent *WSAAPI FakeGethostbyname(const char *name)
{
    Log("gethostbyname " + Str(name));
    const bool machine = std::strcmp(name, "testhost") == 0;
    if (machine ? !g_script.hostFound : !g_script.masterFound)
        return nullptr;
    const std::vector<uint32_t> ips = machine ? g_script.hostIps : std::vector<uint32_t>{0x44332211};
    for (size_t i = 0; i < ips.size() && i < 7; ++i) {
        g_hostIps[i] = ips[i];
        g_hostList[i] = reinterpret_cast<char *>(&g_hostIps[i]);
    }
    g_hostList[std::min<size_t>(ips.size(), 7)] = nullptr;
    g_host.h_addr_list = g_hostList;
    return &g_host;
}
extern "C" int WSAAPI FakeClosesocket(SOCKET s)
{
    Log("closesocket " + N(static_cast<long long>(s)));
    return 0;
}
extern "C" int WSAAPI FakeWSACleanup()
{
    Log("WSACleanup");
    return 0;
}
extern "C" int WSAAPI FakeWSAStartup(WORD version, LPWSADATA)
{
    Log("WSAStartup " + Hex(version));
    return 0;
}

// Fresh blocks hold garbage, as the original's heap's: 0xCD, alike on both sides.
void *TestMalloc(size_t n)
{
    void *p = std::malloc(n ? n : 1);
    std::memset(p, 0xCD, n ? n : 1);
    return p;
}
namespace std {
using ::TestMalloc;
}

// ---- the port's files (gamespy_util.cpp's helpers included too: their select and WSAStartup the test's) ----
#define select FakeSelect
#define WSAStartup FakeWSAStartup
#include "net/gamespy_util.cpp"
#undef select
#undef WSAStartup
namespace std {
using ::FakeRand;
using ::FakeSrand;
}
#define GetTickCount FakeGetTickCount
#define malloc TestMalloc
#define rand FakeRand
#define srand FakeSrand
#define recvfrom FakeRecvfrom
#define sendto FakeSendto
#define socket(a, b, c) FakeSocket(a, b, c)
#define bind FakeBind
#define getsockname FakeGetsockname
#define gethostname FakeGethostname
#define gethostbyname FakeGethostbyname
#define closesocket FakeClosesocket
#define WSACleanup FakeWSACleanup
#include "net/gamespy_qr2.cpp"
#undef GetTickCount
#undef malloc
#undef rand
#undef srand
#undef recvfrom
#undef sendto
#undef socket
#undef bind
#undef getsockname
#undef gethostname
#undef gethostbyname
#undef closesocket
#undef WSACleanup

namespace {

// ---- the original's ----
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
const uint32_t O_RECORD = 0x00845670, O_NAMES = 0x00845828, O_IPS = 0x00982dc0, O_IP_COUNT = 0x00982dd4;
const char **ONames() { return reinterpret_cast<const char **>(static_cast<uintptr_t>(O_NAMES)); }
void *OMalloc(size_t n) { return TestMalloc(n); }
void OFree(void *p) { std::free(p); }
int ORand() { return FakeRand(); }
void OSrand(unsigned seed) { FakeSrand(seed); }

// The game's callbacks (each side's convention).
std::string Buf(const S_Qr2Buffer *b) { return b ? N(b->length) : "null"; }
bool NextAnswer()
{
    if (g_script.answers.empty())
        return true;
    const bool a = g_script.answers[static_cast<size_t>(g_script.answer) % g_script.answers.size()];
    ++g_script.answer;
    return a;
}
void ServerKey(int key, S_Qr2Buffer *buffer, void *userData)
{
    Log("serverKey " + N(key) + " " + Buf(buffer) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (NextAnswer())
        qr2_buffer_add(buffer, ("s" + N(key)).c_str());
}
void PlayerKey(int key, int index, S_Qr2Buffer *buffer, void *userData)
{
    Log("playerKey " + N(key) + " " + N(index) + " " + Buf(buffer) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (NextAnswer())
        qr2_buffer_add_int(buffer, key * 100 + index);
}
void TeamKey(int key, int index, S_Qr2Buffer *buffer, void *userData)
{
    Log("teamKey " + N(key) + " " + N(index) + " " + Buf(buffer) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (NextAnswer())
        qr2_buffer_add(buffer, ("t" + N(index)).c_str());
}
void KeyList(int keyType, S_Qr2KeyBuffer *keys, void *userData)
{
    Log("keyList " + N(keyType) + " " + N(keys->count) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (keyType >= 0 && keyType < 3)
        for (int k : g_script.keys[keyType])
            qr2_keybuffer_add(keys, k);
}
int Count(int keyType, void *userData)
{
    Log("count " + N(keyType) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    return keyType >= 0 && keyType < 3 ? g_script.counts[keyType] : 0;
}
void AddError(int error, char *message, void *userData)
{
    Log("addError " + N(error) + " " + Str(message) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
}
void CdKeyPacket(char *packet, int size, sockaddr *from) { Log("cdKey " + Bytes(packet, size) + " " + Address(from)); }
// (The original's: its own buffer calls, through its __fastcall functions.)
void FAST OServerKey(int key, S_Qr2Buffer *buffer, void *userData)
{
    Log("serverKey " + N(key) + " " + Buf(buffer) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (NextAnswer())
        O_CALL(0x00738830, void(FAST *)(S_Qr2Buffer *, const char *))(buffer, ("s" + N(key)).c_str());
}
void FAST OPlayerKey(int key, int index, S_Qr2Buffer *buffer, void *userData)
{
    Log("playerKey " + N(key) + " " + N(index) + " " + Buf(buffer) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (NextAnswer())
        O_CALL(0x00738800, void(FAST *)(S_Qr2Buffer *, int))(buffer, key * 100 + index);
}
void FAST OTeamKey(int key, int index, S_Qr2Buffer *buffer, void *userData)
{
    Log("teamKey " + N(key) + " " + N(index) + " " + Buf(buffer) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (NextAnswer())
        O_CALL(0x00738830, void(FAST *)(S_Qr2Buffer *, const char *))(buffer, ("t" + N(index)).c_str());
}
void FAST OKeyList(int keyType, S_Qr2KeyBuffer *keys, void *userData)
{
    Log("keyList " + N(keyType) + " " + N(keys->count) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userData))));
    if (keyType >= 0 && keyType < 3)
        for (int k : g_script.keys[keyType])
            O_CALL(0x007387d0, void(FAST *)(S_Qr2KeyBuffer *, int))(keys, k);
}
int FAST OCount(int keyType, void *userData) { return Count(keyType, userData); }
void FAST OAddError(int error, char *message, void *userData) { AddError(error, message, userData); }
void FAST OCdKeyPacket(char *packet, int size, sockaddr *from) { CdKeyPacket(packet, size, from); }

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
    if (std::getenv("HD2_QR2_VERBOSE"))   // (every call of both)
        for (size_t i = 0; i < n; ++i)
            std::printf("  %-60s | %s\n", (i < g_log[1].size() ? g_log[1][i] : "-").substr(0, 60).c_str(), (i < g_log[0].size() ? g_log[0][i] : "-").substr(0, 60).c_str());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 600) + "\n     port: " + y.substr(0, 600);
            break;
        }
    }
    Check(false, s);
}

// ---- the records (each side's: the default one, or a test's) ----
S_Qr2Record g_testRecord[2];
S_Qr2Record *DefaultRecord(int side) { return side ? reinterpret_cast<S_Qr2Record *>(static_cast<uintptr_t>(O_RECORD)) : &g_qr2DefaultRecord; }
bool g_useTest;
S_Qr2Record *Record(int side) { return g_useTest ? &g_testRecord[side] : DefaultRecord(side); }
// A record's fields, its callbacks as the side's (or none).
std::string RecordText(const S_Qr2Record *r, int side)
{
    auto cb = [&](const void *p, const void *port, const void *orig) { return std::string(p == nullptr ? "0" : p == (side ? orig : port) ? "1" : "?"); };
    std::string s = N(static_cast<long long>(r->socket)) + "," + Str(r->gameName) + "," + Str(r->secretKey) + "," + Bytes(r->instanceKey, 4) + ",";
    s += cb(reinterpret_cast<const void *>(r->serverKey), reinterpret_cast<const void *>(&ServerKey), reinterpret_cast<const void *>(&OServerKey));
    s += cb(reinterpret_cast<const void *>(r->playerKey), reinterpret_cast<const void *>(&PlayerKey), reinterpret_cast<const void *>(&OPlayerKey));
    s += cb(reinterpret_cast<const void *>(r->teamKey), reinterpret_cast<const void *>(&TeamKey), reinterpret_cast<const void *>(&OTeamKey));
    s += cb(reinterpret_cast<const void *>(r->keyList), reinterpret_cast<const void *>(&KeyList), reinterpret_cast<const void *>(&OKeyList));
    s += cb(reinterpret_cast<const void *>(r->count), reinterpret_cast<const void *>(&Count), reinterpret_cast<const void *>(&OCount));
    s += cb(reinterpret_cast<const void *>(r->addError), reinterpret_cast<const void *>(&AddError), reinterpret_cast<const void *>(&OAddError));
    s += cb(reinterpret_cast<const void *>(r->cdKeyProcess), reinterpret_cast<const void *>(&CdKeyPacket), reinterpret_cast<const void *>(&OCdKeyPacket));
    s += "," + N(r->natCallback != nullptr) + N(r->clientMessage != nullptr) + "," + N(r->lastHeartbeat) + "," + N(r->lastKeepAlive) + "," + N(r->listedState) +
         "," + N(r->isPublic) + "," + N(r->queryPort) + "," + N(r->readSocket) + "," + N(r->natNegotiate) + "," + Address(reinterpret_cast<const sockaddr *>(&r->masterAddress)) +
         "," + Bytes(r->clientMessageKeys, 40) + "," + N(r->nextMessageKey) + "," + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r->userData)));
    return s;
}
S_Qr2Record *g_made[2];   // (records qr2_init made: compared, freed by the round)
void CompareState()
{
    std::string s;
    for (int k = 0; k < 2; ++k) {
        const S_Qr2Record *p = k ? &g_testRecord[0] : &g_qr2DefaultRecord, *o = k ? &g_testRecord[1] : DefaultRecord(1);
        if (RecordText(p, 0) != RecordText(o, 1))
            s += std::string(k ? "the test's record " : "the default record ") + RecordText(o, 1).substr(0, 400) + " / " + RecordText(p, 0).substr(0, 400) + "; ";
    }
    if ((g_made[0] != nullptr) != (g_made[1] != nullptr) || (g_made[0] && RecordText(g_made[0], 0) != RecordText(g_made[1], 1)))
        s += "the record made " + (g_made[1] ? RecordText(g_made[1], 1) : "none") + " / " + (g_made[0] ? RecordText(g_made[0], 0) : "none") + "; ";
    if (g_localIpCount != G<int>(O_IP_COUNT) || std::memcmp(g_localIps, reinterpret_cast<void *>(static_cast<uintptr_t>(O_IPS)), sizeof g_localIps) != 0)
        s += "the local addresses; ";
    for (int k = 0; k < 0x40; ++k)
        if (Str(g_qr2KeyNames[k]) != Str(ONames()[k])) {
            s += "the key names; ";
            break;
        }
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
    const S_Script saved = g_script;
    const int binds = g_binds;
    const long long x = port();
    g_side = 1;
    g_script = saved;
    g_binds = binds;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
}

// ---- the world ----
void SetRecord(S_Qr2Record &r, int side, const S_Qr2Record &base)
{
    r = base;
    r.serverKey = side ? reinterpret_cast<Qr2ServerKeyCallback>(&OServerKey) : &ServerKey;
    r.playerKey = side ? reinterpret_cast<Qr2PlayerKeyCallback>(&OPlayerKey) : &PlayerKey;
    r.teamKey = side ? reinterpret_cast<Qr2TeamKeyCallback>(&OTeamKey) : &TeamKey;
    r.keyList = side ? reinterpret_cast<Qr2KeyListCallback>(&OKeyList) : &KeyList;
    r.count = side ? reinterpret_cast<Qr2CountCallback>(&OCount) : &Count;
    r.addError = side ? reinterpret_cast<Qr2AddErrorCallback>(&OAddError) : &AddError;
    r.cdKeyProcess = base.cdKeyProcess ? (side ? reinterpret_cast<Qr2PacketFn>(&OCdKeyPacket) : &CdKeyPacket) : nullptr;
}
void SetWorld()
{
    for (int side = 0; side < 2; ++side) {
        std::free(g_made[side]);
        g_made[side] = nullptr;
    }
    S_Qr2Record base{};
    base.socket = P(0.1) ? INVALID_SOCKET : static_cast<SOCKET>(I(1, 999));
    std::snprintf(base.gameName, sizeof base.gameName, "hd2");
    std::snprintf(base.secretKey, sizeof base.secretKey, "%s", P(0.8) ? "sK8pQ9" : "k");
    for (uint8_t &b : base.instanceKey)
        b = static_cast<uint8_t>(I(0, 255));
    if (P(0.2))
        base.instanceKey[3] = 0;   // (an empty challenge's byte before it: a terminator)
    base.lastHeartbeat = P(0.2) ? 0 : static_cast<uint32_t>(I(0, 200000));
    base.lastKeepAlive = static_cast<uint32_t>(I(0, 200000));
    base.listedState = I(-1, 5);
    base.isPublic = I(0, 1);
    base.queryPort = I(1000, 9000);
    base.readSocket = I(0, 1);
    base.natNegotiate = I(0, 1);
    base.masterAddress.sin_family = AF_INET;
    base.masterAddress.sin_port = htons(27900);
    base.masterAddress.sin_addr.s_addr = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
    base.cdKeyProcess = P(0.7) ? reinterpret_cast<Qr2PacketFn>(1) : nullptr;
    for (int &k : base.clientMessageKeys)
        k = P(0.5) ? -1 : I(0, 20);
    base.nextMessageKey = I(0, 9);
    base.userData = reinterpret_cast<void *>(static_cast<uintptr_t>(I(1, 0xFFFF)));
    for (int side = 0; side < 2; ++side) {
        SetRecord(*DefaultRecord(side), side, base);
        SetRecord(g_testRecord[side], side, base);
    }
    g_useTest = P(0.3);
    S_Script &w = g_script;
    w.ticks = static_cast<uint32_t>(I(0, 300000));
    w.step = P(0.5) ? 0 : static_cast<uint32_t>(I(1, 70000));
    if (P(0.3)) {   // (a time's edge: the challenge's retry, the heartbeat's, the keepalive's, or just before)
        const int edge = I(0, 2);
        w.ticks = (edge == 2 ? base.lastKeepAlive + 20000u : base.lastHeartbeat + (edge == 0 ? 10000u : 60000u)) + static_cast<uint32_t>(I(0, 1));
        w.step = 0;
    }
    w.randState = static_cast<unsigned>(I(0, 0x7FFFFFFF));
    w.packets.clear();
    w.hostIps.clear();
    for (int k = I(0, 7); k > 0; --k)
        w.hostIps.push_back(static_cast<uint32_t>(I(0, 0x7FFFFFFF)));
    w.hostFound = P(0.9);
    w.masterFound = P(0.9);
    w.bindFailures = P(0.7) ? 0 : P(0.9) ? I(1, 5) : 100;
    w.socketFails = P(0.05);
    w.socknameFails = P(0.2);
    for (int &c : w.counts)
        c = I(0, 4);
    for (auto &k : w.keys) {
        k.clear();
        for (int n = I(0, 5); n > 0; --n)
            k.push_back(P(0.1) ? I(-2, 300) : I(1, 0x36));
        for (int &key : k)
            if (key == 0xFE)
                key = 0xFD;   // (the original's table has 254 names: 0xFE reads past it)
    }
    w.answers.clear();
    for (int n = I(0, 4); n > 0; --n)
        w.answers.push_back(P(0.8));
    w.answer = 0;
    g_binds = 0;
    // The machine's addresses (none yet, or some), the key names registered.
    g_localIpCount = P(0.3) ? 0 : I(1, 5);
    for (int i = 0; i < MAX_LOCAL_IPS; ++i)
        g_localIps[i] = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
    G<int>(O_IP_COUNT) = g_localIpCount;
    std::memcpy(reinterpret_cast<void *>(static_cast<uintptr_t>(O_IPS)), g_localIps, sizeof g_localIps);
    static const char *names[] = {"isdedicated", "voicechat", "expansion"};
    for (int k = 0x33; k <= 0x35; ++k) {
        g_qr2KeyNames[k] = names[k - 0x33];
        ONames()[k] = names[k - 0x33];
    }
}

// A packet the socket receives: a query, a challenge, an echo, an add error, a client message, an old query, the CD
// key's, others.
std::string MakePacket(const S_Qr2Record &r)
{
    std::string p;
    const int kind = I(0, 9);
    if (kind == 0)
        return ";cd\\key";
    if (kind == 1)
        return "\\status\\";
    p += static_cast<char>(P(0.95) ? 0xFE : 0x11);
    p += static_cast<char>(0xFD);
    const int type = kind - 2 < 6 ? (kind - 2 == 5 ? 6 : kind - 2) : I(0, 9);
    p += static_cast<char>(type);
    const bool mine = P(0.7);
    for (int i = 0; i < 4; ++i)
        p += static_cast<char>(mine ? r.instanceKey[i] : I(0, 255));
    if (type == PACKET_QUERY) {
        for (int list = 0; list < 3; ++list) {
            const int n = P(0.3) ? 0xFF : P(0.2) ? 0 : I(1, 4);
            p += static_cast<char>(n);
            if (n != 0xFF && n != 0)
                for (int k = 0; k < n; ++k)
                    p += static_cast<char>(I(1, 0x36));
        }
        if (P(0.2))
            p.resize(static_cast<size_t>(I(0, static_cast<int>(p.size()))));
    } else if (type == PACKET_CHALLENGE) {
        const int n = P(0.1) ? I(60, 70) : I(0, 30);
        for (int k = 0; k < n; ++k)
            p += static_cast<char>(I(1, 255));
        if (P(0.8))
            p += '\0';
    } else {
        for (int k = I(0, 40); k > 0; --k)
            p += static_cast<char>(I(0, 255));
    }
    return p;
}

void Call()
{
    const int op = I(0, 17);
    S_Qr2Record *pr = Record(0), *orr = Record(1);
    void *precord = g_useTest ? static_cast<void *>(pr) : nullptr, *orecord = g_useTest ? static_cast<void *>(orr) : nullptr;
    switch (op) {
    case 0:
    case 1: {
        // A packet handled (each side its copy: the handler writes it).
        const std::string packet = MakePacket(*pr);
        std::vector<char> b[2] = {std::vector<char>(packet.begin(), packet.end()), std::vector<char>(packet.begin(), packet.end())};
        b[0].resize(packet.size() + 1);
        b[1].resize(packet.size() + 1);
        sockaddr_in from{};
        from.sin_family = AF_INET;
        from.sin_addr.s_addr = 0x01020304;
        from.sin_port = htons(5555);
        Run("HandlePacket " + N(static_cast<long long>(packet.size())), [&] { HandlePacket(pr, b[0].data(), static_cast<int>(packet.size()), reinterpret_cast<sockaddr *>(&from)); return 0; },
            [&] { O_CALL(0x00738950, void(FAST *)(S_Qr2Record *, char *, int, sockaddr *))(orr, b[1].data(), static_cast<int>(packet.size()), reinterpret_cast<sockaddr *>(&from)); return 0; });
        break;
    }
    case 2:
    case 3: {
        // The think (heartbeats due, packets read).
        for (int k = I(0, 4); k > 0; --k)
            g_script.packets.push_back(P(0.1) ? "!" : MakePacket(*pr));
        Run("qr2_think", [&] { qr2_think(precord); return 0; }, [&] { O_CALL(0x007385d0, void(FAST *)(void *))(orecord); return 0; });
        g_script.packets.clear();
        break;
    }
    case 4: {
        const int state = std::vector<int>{0, 2, 3}[static_cast<size_t>(I(0, 2))];
        Run("SendHeartbeat " + N(state), [&] { SendHeartbeat(pr, state); return 0; }, [&] { O_CALL(0x007395d0, void(FAST *)(S_Qr2Record *, int))(orr, state); return 0; });
        break;
    }
    case 5:
        Run("SendKeepAlive", [&] { SendKeepAlive(pr); return 0; }, [&] { O_CALL(0x00739570, void(FAST *)(S_Qr2Record *))(orr); return 0; });
        break;
    case 6: {
        // qr2_init (a record made, or the default one).
        const bool make = P(0.3);
        S_Qr2Record **out[2] = {make ? &g_made[0] : nullptr, make ? &g_made[1] : nullptr};
        const int basePort = P(0.1) ? 0 : I(1000, 9000);
        const bool isPublic = P(0.7);
        const char *ip = P(0.2) ? "127.0.0.1" : P(0.2) ? "192.168.0.2" : P(0.1) ? "255.255.255.255" : nullptr;
        Run("qr2_init", [&] {
                return static_cast<long long>(qr2_init(out[0], ip, basePort, "hd2", "sK8pQ9", isPublic, 0, &ServerKey, &PlayerKey, &TeamKey, &KeyList, &Count,
                                                       &AddError, reinterpret_cast<void *>(0x5151)));
            },
            [&] {
                return static_cast<long long>(O_CALL(0x00738500, int(FAST *)(void *, const char *, int, const char *, const char *, int, int, void *, void *, void *, void *,
                                                                             void *, void *, void *))(out[1], ip, basePort, "hd2", "sK8pQ9", isPublic, 0,
                                                                                                      reinterpret_cast<void *>(&OServerKey), reinterpret_cast<void *>(&OPlayerKey),
                                                                                                      reinterpret_cast<void *>(&OTeamKey), reinterpret_cast<void *>(&OKeyList),
                                                                                                      reinterpret_cast<void *>(&OCount), reinterpret_cast<void *>(&OAddError),
                                                                                                      reinterpret_cast<void *>(0x5151)));
            });
        break;
    }
    case 7: {
        // The shutdown (a test's record: a heap copy, freed by it).
        S_Qr2Record *heap[2] = {nullptr, nullptr};
        if (g_useTest)
            for (int side = 0; side < 2; ++side) {
                heap[side] = static_cast<S_Qr2Record *>(std::malloc(sizeof(S_Qr2Record)));
                *heap[side] = *Record(side);
            }
        Run("StopGameSpyQueries", [&] { StopGameSpyQueries(reinterpret_cast<uintptr_t *>(heap[0])); return 0; },
            [&] { O_CALL(0x00738760, void(FAST *)(void *))(heap[1]); return 0; });
        break;
    }
    case 8: {
        // A key buffer's adds.
        S_Qr2KeyBuffer k[2];
        std::memset(k, 0x44, sizeof k);
        k[0].count = k[1].count = P(0.1) ? I(0xFC, 0xFE) : I(0, 5);
        const int key = P(0.2) ? std::vector<int>{0, 0xFF, -1, 0x100}[static_cast<size_t>(I(0, 3))] : I(1, 0xFE);
        Run("qr2_keybuffer_add " + N(key), [&] { qr2_keybuffer_add(&k[0], key); return static_cast<long long>(k[0].count); },
            [&] { O_CALL(0x007387d0, void(FAST *)(S_Qr2KeyBuffer *, int))(&k[1], key); return static_cast<long long>(k[1].count); });
        Check(std::memcmp(k[0].keys, k[1].keys, static_cast<size_t>(std::min(k[0].count, 0xFF))) == 0, "the keys");
        break;
    }
    case 9:
    case 10: {
        // A buffer's adds (near full sometimes).
        static S_Qr2Buffer b[2];
        b[0].length = b[1].length = P(0.2) ? I(QR2_PACKET_SIZE - 12, QR2_PACKET_SIZE) : I(0, 20);
        std::memset(b[0].data, 'x', sizeof b[0].data);
        std::memset(b[1].data, 'x', sizeof b[1].data);
        if (op == 9) {
            const std::string v = P(0.2) ? std::string(static_cast<size_t>(I(5, 20)), 'v') : "v" + N(I(0, 99));
            Run("qr2_buffer_add", [&] { qr2_buffer_add(&b[0], v.c_str()); return 0; },
                [&] { O_CALL(0x00738830, void(FAST *)(S_Qr2Buffer *, const char *))(&b[1], v.c_str()); return 0; });
        } else {
            const int v = I(-100000, 100000);
            Run("qr2_buffer_add_int", [&] { qr2_buffer_add_int(&b[0], v); return 0; }, [&] { O_CALL(0x00738800, void(FAST *)(S_Qr2Buffer *, int))(&b[1], v); return 0; });
        }
        Check(b[0].length == b[1].length && std::memcmp(b[0].data, b[1].data, sizeof b[0].data) == 0, "the buffer");
        break;
    }
    case 11: {
        // (Not 0xFE: the original's table has 254 names, the 255th is the "score_t" text's first word.)
        int key = P(0.3) ? I(0, 0x31) : I(0x32, 0x110);
        if (key == 0xFE)
            key = 0xFD;
        static const char *const name = "newkey";
        Run("qr2_register_key " + N(key), [&] { qr2_register_key(key, name); return 0; }, [&] { O_CALL(0x00739750, void(FAST *)(int, const char *))(key, name); return 0; });
        Check(g_qr2KeyNames[0xFF] == nullptr, "the key 255 registered");   // (the original's table stops before it)
        if (key > 0x31 && key < 0xFF) {
            Check(g_qr2KeyNames[key] == ONames()[key], "the key's name");
            g_qr2KeyNames[key] = nullptr;
            ONames()[key] = nullptr;
        }
        break;
    }
    case 12: {
        // The cipher, base 64.
        char key[2][16];
        std::snprintf(key[0], 16, "%s", P(0.5) ? "sK8pQ9" : "abc");
        std::strcpy(key[1], key[0]);
        uint8_t data[2][80];
        std::memset(data, 0x33, sizeof data);
        const int size = I(0, 70);
        for (int i = 0; i < size; ++i)
            data[0][i] = data[1][i] = static_cast<uint8_t>(I(0, 255));
        if (P(0.5)) {
            Run("Encrypt " + N(size), [&] { Encrypt(key[0], static_cast<int>(std::strlen(key[0])), data[0], size); return 0; },
                [&] { O_CALL(0x00738d80, void(FAST *)(const char *, int, uint8_t *, int))(key[1], static_cast<int>(std::strlen(key[1])), data[1], size); return 0; });
            Check(std::memcmp(data[0], data[1], static_cast<size_t>(size) + 1) == 0, "the cipher");   // (and the byte after)
        } else {
            char out[2][128];
            std::memset(out, 'q', sizeof out);
            Run("Base64 " + N(size), [&] { Base64(data[0], size, out[0]); return 0; }, [&] { O_CALL(0x00738c90, void(FAST *)(const uint8_t *, int, char *))(data[1], size, out[1]); return 0; });
            Check(std::memcmp(out[0], out[1], sizeof out[0]) == 0, "the digits");
        }
        break;
    }
    case 13: {
        const int key = P(0.5) ? I(0, 20) : I(-5, 300);
        Run("MessageKeySeen " + N(key), [&] { return static_cast<long long>(MessageKeySeen(pr, key)); },
            [&] { return static_cast<long long>(O_CALL(0x00739520, uint8_t(FAST *)(S_Qr2Record *, int))(orr, key)); });
        break;
    }
    case 14: {
        // Old replies and key replies built into a buffer each side.
        static S_Qr2Buffer b[2];
        b[0].length = b[1].length = P(0.3) ? I(QR2_PACKET_SIZE - 6, QR2_PACKET_SIZE) : I(0, 10);   // (near full sometimes)
        std::memset(b[0].data, 0, sizeof b[0].data);
        std::memset(b[1].data, 0, sizeof b[1].data);
        if (P(0.5)) {
            Run("BuildOldReply", [&] { BuildOldReply(pr, &b[0]); return 0; }, [&] { O_CALL(0x00739260, void(FAST *)(S_Qr2Record *, S_Qr2Buffer *))(orr, &b[1]); return 0; });
        } else {
            uint8_t keys[3][4];
            int counts[3];
            for (int t = 0; t < 3; ++t) {
                counts[t] = P(0.3) ? 0xFF : P(0.2) ? 0 : I(1, 4);
                for (uint8_t &k : keys[t])
                    k = static_cast<uint8_t>(I(1, 0x36));
            }
            Run("BuildReply", [&] { BuildReply(pr, &b[0], counts[0], keys[0], counts[1], keys[1], counts[2], keys[2]); return 0; },
                [&] {
                    O_CALL(0x00738fd0, void(FAST *)(S_Qr2Record *, S_Qr2Buffer *, int, const uint8_t *, int, const uint8_t *, int, const uint8_t *))(
                        orr, &b[1], counts[0], keys[0], counts[1], keys[1], counts[2], keys[2]);
                    return 0;
                });
        }
        Check(b[0].length == b[1].length && std::memcmp(b[0].data, b[1].data, static_cast<size_t>(std::min(b[0].length, QR2_PACKET_SIZE))) == 0,
              "the reply " + Bytes(b[1].data, b[1].length) + " / " + Bytes(b[0].data, b[0].length));
        break;
    }
    case 15: {
        // FindLocalIps (the addresses forgotten first, alike).
        Run("FindLocalIps", [&] { FindLocalIps(); return 0; }, [&] { O_CALL(0x00738890, void(FAST *)())(); return 0; });
        break;
    }
    case 16: {
        // A socket bound (the first free port).
        SOCKET s[2] = {7, 7};
        int port[2];
        port[0] = port[1] = P(0.1) ? 0 : I(1000, 9000);
        const char *ip = P(0.3) ? "127.0.0.1" : P(0.3) ? "10.0.0.1" : nullptr;
        Run("InitSocket", [&] { return static_cast<long long>(InitSocket(&s[0], ip, &port[0])); },
            [&] { return static_cast<long long>(O_CALL(0x00738400, int(FAST *)(SOCKET *, const char *, int *))(&s[1], ip, &port[1])); });
        Check(s[0] == s[1] && port[0] == port[1], "the socket and port");
        break;
    }
    default:
        Run("SendHeartbeatsIfDue", [&] { SendHeartbeatsIfDue(pr); return 0; }, [&] { O_CALL(0x00738670, void(FAST *)(S_Qr2Record *))(orr); return 0; });
        break;
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n   (ops:%s)\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, g_round,
                g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    for (size_t i = g_log[g_side].size() > 6 ? g_log[g_side].size() - 6 : 0; i < g_log[g_side].size(); ++i)
        std::printf("    last: %s\n", g_log[g_side][i].substr(0, 200).c_str());
    std::fflush(stdout);
    ExitProcess(3);
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW || code == 0xC0000374)
        OnCrash(e);
    return EXCEPTION_CONTINUE_SEARCH;
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

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_QR2_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("qr2_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    if (std::getenv("HD2_QR2_NAMES"))   // (the two key name tables side by side)
        for (int k = 0; k < 0x40; ++k)
            std::printf("key %d: %08x / %s\n", k, static_cast<unsigned>(reinterpret_cast<uintptr_t>(ONames()[k])), (Str(ONames()[k]) + " / " + Str(g_qr2KeyNames[k])).c_str());
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(OMalloc));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(OFree));
    RedirectOriginal(0x007e6393, reinterpret_cast<const void *>(ORand));
    RedirectOriginal(0x007e6386, reinterpret_cast<const void *>(OSrand));
    RedirectImport(reinterpret_cast<const void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount")), reinterpret_cast<const void *>(&FakeGetTickCount));
    const std::pair<const char *, const void *> sockets[] = {
        {"select", reinterpret_cast<const void *>(&FakeSelect)},         {"recvfrom", reinterpret_cast<const void *>(&FakeRecvfrom)},
        {"sendto", reinterpret_cast<const void *>(&FakeSendto)},         {"socket", reinterpret_cast<const void *>(&FakeSocket)},
        {"bind", reinterpret_cast<const void *>(&FakeBind)},             {"getsockname", reinterpret_cast<const void *>(&FakeGetsockname)},
        {"gethostname", reinterpret_cast<const void *>(&FakeGethostname)}, {"gethostbyname", reinterpret_cast<const void *>(&FakeGethostbyname)},
        {"closesocket", reinterpret_cast<const void *>(&FakeClosesocket)}, {"WSACleanup", reinterpret_cast<const void *>(&FakeWSACleanup)},
        {"WSAStartup", reinterpret_cast<const void *>(&FakeWSAStartup)}};
    for (const char *dll : {"ws2_32.dll", "wsock32.dll"}) {
        HMODULE m = LoadLibraryA(dll);
        for (const auto &s : sockets)
            RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, s.first)), s.second);
    }

    // The default record as its static initialiser leaves it (the original's in its data: never run here).
    Check(g_qr2DefaultRecord.socket == INVALID_SOCKET && G<SOCKET>(O_RECORD) == INVALID_SOCKET, "the default record's socket");
    int rounds = std::getenv("HD2_QR2_QUICK") ? 8000 : 30000;
    if (const char *r = std::getenv("HD2_QR2_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_QR2_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        g_trace.clear();
        SetWorld();
        for (int k = I(1, 8); k > 0; --k)
            Call();
    }
    std::printf("qr2_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
