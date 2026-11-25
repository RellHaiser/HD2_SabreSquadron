// Net part 13d: GameSpy's server browser (net/gamespy_browser.cpp: the shared strings, a server's values and the
// queries' answers, the query engine, the master server's cipher, the list (its request, the main list, the messages
// after it, the LAN answers), the browser's events, the API) against the original executable's. MinGW;
// build_browser.bat; runs browser_diff.exe ("browser_diff: N checks, 0 failures"). The port's file is included here
// (its state and helpers are its own TU's), so the mutation runner rebuilds this file (MUT_WRAPPER=browser_diff.cpp).
//
// The original's SDK is __fastcall, and so is the browser's callback it calls: each side gets its own, logging alike.
// Each round makes a browser on each side (ServerBrowserNew, compared) and runs up to 10 calls on it: updates (a
// master server's stream made for each side's own challenge: the cipher's header, the list's keys, popular values and
// servers, its end, the messages after it; cut into chunks), thinks (the servers' answers to the queries, binary or
// the old text), the API's values, the list's helpers, detached servers, the cipher, the message readers. After each
// call the result, the logs (the sockets', the callback's, the clock's, rand's, Sleep's) and both sides' browsers,
// servers and shared strings are compared. The original's shared strings table (a TLS slot the harness's copy never
// set up) is the test's. HD2_SB_ROUNDS: rounds (default 25,000; HD2_SB_QUICK: 6,000); HD2_SB_SEED; HD2_SB_FIRST: the
// first round; HD2_SB_PROGRESS: each round's calls; HD2_SB_DUMP: the master server's streams; HD2_SB_INBUF: the list's
// unread bytes; HD2_SB_VERBOSE: both sides' calls at a difference; HD2_SB_FIRST_CHANCE: crashes under the original's
// exception frames. A watchdog reports a call that makes no progress for 20 s (where, the list's unread bytes).
//
// The streams stay within what the original survives (its weaknesses, kept): a record with both kinds of keys only
// when the whole stream arrives at once; no fixed fields before a key list; popular indices within the list's; no
// messages after a list with no end or after an error; a stream only for a request sent; no update over unread bytes.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// ---- the logs (both sides) ----
int g_side;
bool g_faking;
volatile long g_progress;   // (the watchdog's: calls made)
HANDLE g_mainThread;
std::vector<std::string> g_log[2];
void Log(const std::string &s)
{
    if (!g_faking)
        return;
    g_log[g_side].push_back(s);
    if (g_log[g_side].size() > 300000) {   // (a loop that does not end)
        std::printf("runaway calls on side %d:\n", g_side);
        for (size_t i = g_log[g_side].size() - 12; i < g_log[g_side].size(); ++i)
            std::printf("    %s\n", g_log[g_side][i].substr(0, 200).c_str());
        std::fflush(stdout);
        ExitProcess(4);
    }
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
const SOCKET UDP_SOCKET = 0x2A1, TCP_SOCKET = 0x2A2, LAN_SOCKET = 0x2A3;
struct S_Item {
    std::string data;   // "!": a failed read
    uint32_t ip;
    u_short port;       // (network order)
};
// The master server's stream, made for the side's own challenge at its first read.
struct S_Plan {
    bool waiting;           // made, its request not sent yet (the stream answers only a request sent)
    bool pending;
    std::string header;     // the cipher's header (plain): its size's byte, filler, the key's size, the key
    std::string key;        // the server's half of the key
    std::string plain;      // the rest, encrypted
    std::vector<int> cuts;  // the chunks' sizes (the last: the rest)
    int stop;               // the stream cut short here (-1: all of it)
    bool fails;             // a failed read after it
};
struct S_Script {
    uint32_t ticks, step;
    unsigned randState;
    std::deque<S_Item> udp, lan;
    std::deque<std::string> tcp;
    S_Plan plan;
    bool closeWhenEmpty;    // the master server closes once its stream is read (a read of 0)
    bool dnsFails, connectFails, udpSocketFails, tcpSocketFails;
    std::deque<int> sends;  // the next sends' results (then all work)
    int sleeps;
    bool hang;              // (too many sleeps: any list socket reads as closed)
    bool clearOnError;      // the browser's callback clears it on the master server's error message
} g_script;
struct S_ServerBrowser *g_listOwner[2];   // (each side's browser: its list's state)

// ---- the system calls (both sides') ----
extern "C" DWORD WINAPI FakeGetTickCount()
{
    Log("GetTickCount");
    const uint32_t t = g_script.ticks;
    g_script.ticks += g_script.step;
    return t;
}
extern "C" void WINAPI FakeSleep(DWORD ms)
{
    Log("Sleep " + N(ms));
    g_script.ticks += 100;
    if (++g_script.sleeps > 3000) {   // (a loop that would not end: the master server closes, the answers stop)
        g_script.closeWhenEmpty = true;
        g_script.hang = true;
        if (g_script.sleeps > 6000 && g_listOwner[g_side])   // (a trigger wait whose server never answers: given up)
            *reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(g_listOwner[g_side]) + 0x610) = 0;   // (its triggerIp)
        g_script.plan.pending = false;
        g_script.tcp.clear();
        g_script.udp.clear();
    }
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
std::string Address(const sockaddr *a)
{
    const auto *in = reinterpret_cast<const sockaddr_in *>(a);
    return N(in->sin_family) + "/" + Hex(in->sin_addr.s_addr) + ":" + N(ntohs(in->sin_port));
}
void (*g_makeStream)();   // (MakeStream: the plan's stream for the reading side)
extern "C" int WSAAPI FakeSelect(int count, fd_set *readable, fd_set *, fd_set *, const timeval *)
{
    const SOCKET s = readable && readable->fd_count ? readable->fd_array[0] : INVALID_SOCKET;
    Log("select " + N(count) + " " + N(static_cast<long long>(s)));
    if (g_script.hang && s != UDP_SOCKET && g_listOwner[g_side] && *reinterpret_cast<const int *>(reinterpret_cast<const char *>(g_listOwner[g_side]) + 0x48) != 0)
        return 1;   // (the list's state, not LAN: its read finds the socket closed)
    if (s == UDP_SOCKET)
        return g_script.udp.empty() ? 0 : 1;
    if (s == LAN_SOCKET)
        return g_script.lan.empty() ? 0 : 1;
    if (s == TCP_SOCKET && std::getenv("HD2_SB_INBUF") && g_listOwner[g_side]) {   // (the list's unread bytes, decrypted)
        const char *l = reinterpret_cast<const char *>(g_listOwner[g_side]) + 0x48;
        const char *in = *reinterpret_cast<char *const *>(l + 0x74);
        const int n = *reinterpret_cast<const int *>(l + 0x78);
        std::printf("  side %d in %d:%s\n", g_side, n, in ? Bytes(in, n).c_str() : "null");
    }
    if (s == TCP_SOCKET)
        return g_script.tcp.empty() && !g_script.plan.pending && !g_script.closeWhenEmpty ? 0 : 1;
    return 0;
}
extern "C" int WSAAPI FakeRecvfrom(SOCKET s, char *data, int size, int flags, sockaddr *from, int *fromSize)
{
    Log("recvfrom " + N(static_cast<long long>(s)) + " " + N(size) + " " + N(flags) + " " + N(*fromSize));
    std::deque<S_Item> &q = s == LAN_SOCKET ? g_script.lan : g_script.udp;
    if (q.empty())
        return -1;
    const S_Item p = q.front();
    q.pop_front();
    if (p.data == "!")
        return -1;
    const int n = std::min<int>(size, static_cast<int>(p.data.size()));
    std::memcpy(data, p.data.data(), static_cast<size_t>(n));
    auto *in = reinterpret_cast<sockaddr_in *>(from);
    std::memset(in, 0, sizeof *in);
    in->sin_family = AF_INET;
    in->sin_addr.s_addr = p.ip;
    in->sin_port = p.port;
    return n;
}
extern "C" int WSAAPI FakeRecv(SOCKET s, char *data, int size, int flags)
{
    Log("recv " + N(static_cast<long long>(s)) + " " + N(size) + " " + N(flags));
    if (g_script.plan.pending)
        g_makeStream();
    if (g_script.tcp.empty())
        return 0;
    std::string p = g_script.tcp.front();
    g_script.tcp.pop_front();
    if (p == "!")
        return -1;
    const int n = std::min<int>(size, static_cast<int>(p.size()));
    std::memcpy(data, p.data(), static_cast<size_t>(n));
    if (n < static_cast<int>(p.size()))
        g_script.tcp.push_front(p.substr(static_cast<size_t>(n)));
    return n;
}
extern "C" int WSAAPI FakeSendto(SOCKET s, const char *data, int size, int flags, const sockaddr *to, int toSize)
{
    Log("sendto " + N(static_cast<long long>(s)) + " " + Bytes(data, size) + " " + N(flags) + " " + Address(to) + " " + N(toSize));
    return size;
}
extern "C" int WSAAPI FakeSend(SOCKET s, const char *data, int size, int flags)
{
    Log("send " + N(static_cast<long long>(s)) + " " + Bytes(data, size) + " " + N(flags));
    int r = size;
    if (!g_script.sends.empty()) {
        r = g_script.sends.front();   // (-1, 0: failed; 1: one byte; 2: all)
        g_script.sends.pop_front();
        if (r == 2)
            r = size;
    }
    // A list request sent: the master server's answer comes (a new connection's stream).
    if (r >= 1 && g_script.plan.waiting && size >= 5 && data[2] == 0 && data[3] == 1 && data[4] == 3) {
        g_script.plan.waiting = false;
        g_script.plan.pending = true;
        g_script.tcp.clear();
    }
    return r;
}
extern "C" SOCKET WSAAPI FakeSocket(int family, int type, int protocol)
{
    Log("socket " + N(family) + " " + N(type) + " " + N(protocol));
    if (type == SOCK_DGRAM)
        return g_script.udpSocketFails ? INVALID_SOCKET : UDP_SOCKET;
    return g_script.tcpSocketFails ? INVALID_SOCKET : TCP_SOCKET;
}
extern "C" int WSAAPI FakeConnect(SOCKET s, const sockaddr *to, int size)
{
    Log("connect " + N(static_cast<long long>(s)) + " " + Address(to) + " " + N(size));
    return g_script.connectFails ? SOCKET_ERROR : 0;
}
hostent g_host;
char *g_hostList[2];
uint32_t g_hostIp = 0x0A000001;
extern "C" hostent *WSAAPI FakeGethostbyname(const char *name)
{
    Log("gethostbyname " + Str(name));
    if (g_script.dnsFails)
        return nullptr;
    g_hostList[0] = reinterpret_cast<char *>(&g_hostIp);
    g_hostList[1] = nullptr;
    g_host.h_addr_list = g_hostList;
    return &g_host;
}
extern "C" unsigned long WSAAPI FakeInetAddr(const char *text)
{
    Log("inet_addr " + Str(text));
    return inet_addr(text);
}
extern "C" int WSAAPI FakeClosesocket(SOCKET s)
{
    Log("closesocket " + N(static_cast<long long>(s)));
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
#define malloc TestMalloc
#include "net/gamespy_util.cpp"
#undef select
#undef WSAStartup
namespace std {
using ::FakeRand;
using ::FakeSrand;
}
#define GetTickCount FakeGetTickCount
#define Sleep FakeSleep
#define rand FakeRand
#define srand FakeSrand
#define recvfrom FakeRecvfrom
#define recv FakeRecv
#define sendto FakeSendto
#define send FakeSend
#define socket(a, b, c) FakeSocket(a, b, c)
#define connect FakeConnect
#define gethostbyname FakeGethostbyname
#define inet_addr FakeInetAddr
#define closesocket FakeClosesocket
#undef SB_TEST_PORT   // (the mutation runner's parallel port: the original's 28910 here)
#include "net/gamespy_browser.cpp"
#undef GetTickCount
#undef Sleep
#undef malloc
#undef rand
#undef srand
#undef recvfrom
#undef recv
#undef sendto
#undef send
#undef socket
#undef connect
#undef gethostbyname
#undef inet_addr
#undef closesocket

// The qr2 key names the browser asks for (the original's texts; the game's three registered).
const char *g_qr2KeyNames[0x100];

static_assert(sizeof(S_SbServer) == 0x24 && sizeof(S_SbQueryEngine) == 0x48 && sizeof(S_SbCipher) == 0x105, "layouts");
static_assert(sizeof(S_SbServerList) == 0x5C0 && offsetof(S_SbServerList, cipher) == 0x4AC && offsetof(S_SbServerList, deadList) == 0x5BC, "list");
static_assert(sizeof(S_ServerBrowser) == 0x620 && offsetof(S_ServerBrowser, list) == 0x48 && offsetof(S_ServerBrowser, callback) == 0x618, "browser");

namespace {

// ---- the original's ----
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
const uint32_t O_NAMES = 0x00845828;
const char **ONames() { return reinterpret_cast<const char **>(static_cast<uintptr_t>(O_NAMES)); }
void *OMalloc(size_t n) { return TestMalloc(n); }
void OFree(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
int ORand() { return FakeRand(); }
void OSrand(unsigned seed) { FakeSrand(seed); }
DWORD OGetTickCount() { return FakeGetTickCount(); }
void FAST OMsleep(DWORD ms) { FakeSleep(ms); }
// The shared strings' table (the original keeps it in a TLS slot).
S_HashTable *g_oShared;
S_HashTable *OSharedStrings()
{
    if (!g_oShared)
        g_oShared = O_CALL(0x00736dd0, S_HashTable *(FAST *)(int, int, int, const void *, const void *, const void *))(
            8, 500, 4, reinterpret_cast<const void *>(0x0073a290), reinterpret_cast<const void *>(0x0073a2a0), reinterpret_cast<const void *>(0x0073a2c0));
    return g_oShared;
}
void OFreeSharedStrings()
{
    if (g_oShared && O_CALL(0x00736e80, int(FAST *)(S_HashTable *))(g_oShared) == 0) {
        O_CALL(0x00736e40, void(FAST *)(S_HashTable *))(g_oShared);
        g_oShared = nullptr;
    }
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

// ---- the browsers (each side's) and their callbacks ----
S_ServerBrowser *g_browser[2];
int g_addedRound = -1;   // (the round of the test's own shared strings)
void *const INSTANCE = reinterpret_cast<void *>(0x7777);
std::string ServerName(const S_SbServer *s) { return s ? Hex(s->publicIp) + ":" + N(ntohs(s->publicPort)) : "none"; }
void BrowserEvent(S_ServerBrowser *b, int reason, S_SbServer *server, void *instance)
{
    std::string s = "browser " + N(reason) + " " + ServerName(server) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance)));
    if (reason == 5)
        s += " " + Str(b->list.errorMessage);
    if (server && (reason == 1 || reason == 0))
        s += " " + Str(SBServerGetStringValue(server, "hostname", "-")) + " " + N(SBServerGetIntValue(server, "numplayers", -7));
    Log(s);
    if (reason == 5 && g_script.clearOnError)   // (the game's callers may clear the browser on the master's error)
        ServerBrowserClear(b);
}
void FAST OBrowserEvent(S_ServerBrowser *b, int reason, S_SbServer *server, void *instance)
{
    std::string s = "browser " + N(reason) + " " + ServerName(server) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance)));
    if (reason == 5)
        s += " " + Str(b->list.errorMessage);
    if (server && (reason == 1 || reason == 0)) {
        s += " " + Str(O_CALL(0x0073a3f0, const char *(FAST *)(S_SbServer *, const char *, const char *))(server, "hostname", "-"));
        s += " " + N(O_CALL(0x0073a420, int(FAST *)(S_SbServer *, const char *, int))(server, "numplayers", -7));
    }
    Log(s);
    if (reason == 5 && g_script.clearOnError)
        O_CALL(0x0073b280, void(FAST *)(S_ServerBrowser *))(b);
}

// ---- the state's text (alike layouts: read the same way on both sides) ----
int ArrayCount(const S_DArray *a) { return a ? a->count : -1; }
const void *ArrayAt(const S_DArray *a, int i) { return a->list + i * a->elementSize; }
// The table's elements as text (bucket by bucket, in their order).
std::string TableText(const S_HashTable *t, bool pairs)
{
    if (!t)
        return "null";
    std::string s = N(t->bucketCount) + "{";
    for (int b = 0; b < t->bucketCount; ++b) {
        const S_DArray *a = t->buckets[b];
        for (int i = 0; i < ArrayCount(a); ++i) {
            const auto *e = static_cast<const char *const *>(ArrayAt(a, i));
            if (pairs)
                s += Str(e[0]) + "=" + Str(e[1]) + ",";
            else
                s += Str(e[0]) + "#" + N(reinterpret_cast<const int *>(e)[1]) + ",";
        }
    }
    return s + "}";
}
std::string ServerText(const S_SbServer *s)
{
    return ServerName(s) + "/" + Hex(s->privateIp) + ":" + N(s->privatePort) + "/" + Hex(s->icmpIp) + "/" + N(s->state) + "/" + N(s->flags) + "/" + N(s->updateTime) +
           "/" + TableText(s->keyValues, true);
}
// A server as the list knows it: its index there (or x).
std::string ServerRef(const S_ServerBrowser *b, const S_SbServer *s)
{
    for (int i = 0; i < ArrayCount(b->list.servers); ++i)
        if (*static_cast<S_SbServer *const *>(ArrayAt(b->list.servers, i)) == s)
            return "#" + N(i);
    return "x" + ServerText(s);
}
std::string QueueText(const S_ServerBrowser *b, const S_SbQueryList &q)
{
    std::string s = N(q.count) + "[";
    int n = 0;
    for (const S_SbServer *x = q.first; x && n < 300; x = x->next, ++n)
        s += ServerRef(b, x) + ",";
    return s + "]last=" + (q.last ? ServerRef(b, q.last) : "null");
}
std::string BrowserText(const S_ServerBrowser *b)
{
    if (!b)
        return "none";
    const S_SbQueryEngine &e = b->engine;
    std::string s = "engine " + N(e.queryVersion) + "," + N(e.maxUpdates) + ",q" + QueueText(b, e.querying) + ",p" + QueueText(b, e.pending) + "," +
                    N(static_cast<long long>(e.socket)) + "," + Hex(e.publicIp) + "," + N(e.keyCount) + ":" + Bytes(e.keys, std::min(std::max(e.keyCount, 0), 0x14)) + "," +
                    Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e.instance) - reinterpret_cast<uintptr_t>(b)));
    const S_SbServerList &l = b->list;
    s += "; list " + N(l.state) + ",servers[";
    for (int i = 0; i < ArrayCount(l.servers); ++i)
        s += ServerText(*static_cast<S_SbServer *const *>(ArrayAt(l.servers, i))) + ";";
    s += "],keys[";
    for (int i = 0; i < ArrayCount(l.keyList); ++i) {
        const auto *k = static_cast<const S_ListKey *>(ArrayAt(l.keyList, i));
        s += Str(k->name) + ":" + N(k->type) + ",";
    }
    s += "]," + Str(l.queryForGame) + Str(l.queryFromGame) + Str(l.queryFromKey) + "," + Bytes(l.challenge, 8) + ",";
    s += l.inBuffer ? "in" + Bytes(l.inBuffer, l.inBufferSize) : "noin" + N(l.inBufferSize);
    s += ",popular[";
    for (int i = 0; i < l.popularCount && i < 0xFF; ++i)
        s += Str(l.popularValues[i]) + ",";
    s += "]" + N(l.popularCount) + "," + N(l.expected) + "," + Str(l.sortKey) + "," + Hex(l.publicIp) + "," + Hex(l.sourceIp) + "," + N(l.defaultPort) + ",";
    // (The error message's text is the callback's: once its buffer is freed, the pointer is only the heap's.)
    s += N(static_cast<long long>(l.socket)) + "," + N(l.queryFromVersion);
    if (l.state >= LIST_STATE_MAIN_LIST || l.parseState > PARSE_CRYPT_HEADER)
        s += "," + Bytes(&l.cipher, sizeof l.cipher) + "," + N(l.options) + "," + N(l.parseState);
    s += ",dead[";
    int n = 0;
    for (const S_SbServer *x = l.deadList; x && n < 300; x = x->next, ++n)
        s += ServerText(x) + ";";
    s += "]; browser " + N(b->disconnectOnComplete) + "," + N(b->dontUpdate) + "," + Hex(b->triggerIp) + ":" + N(b->triggerPort);
    return s;
}
std::string SharedText(int side) { return TableText(side ? g_oShared : t_sharedStrings, false); }

void CompareLogs()
{
    if (g_log[0] == g_log[1]) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(g_log[0].size(), g_log[1].size());
    if (std::getenv("HD2_SB_VERBOSE"))   // (every call of both)
        for (size_t i = 0; i < n; ++i)
            std::printf("  %-70s | %s\n", (i < g_log[1].size() ? g_log[1][i] : "-").substr(0, 70).c_str(), (i < g_log[0].size() ? g_log[0][i] : "-").substr(0, 70).c_str());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 600) + "\n     port: " + y.substr(0, 600);
            break;
        }
    }
    Check(false, s);
}
std::string Diff(const std::string &o, const std::string &p)
{
    size_t i = 0;
    while (i < o.size() && i < p.size() && o[i] == p[i])
        ++i;
    const size_t from = i > 80 ? i - 80 : 0;
    return "\n     orig: ..." + o.substr(from, 400) + "\n     port: ..." + p.substr(from, 400);
}
void CompareState()
{
    const std::string pb = BrowserText(g_browser[0]), ob = BrowserText(g_browser[1]);
    Check(pb == ob, "the browser" + Diff(ob, pb));
    const std::string ps = SharedText(0), os = SharedText(1);
    Check(ps == os, "the shared strings" + Diff(os, ps));
}

template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_trace += " " + what;
    ++g_progress;
    if (std::getenv("HD2_SB_PROGRESS"))
        std::printf("  %s\n", what.c_str());
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    g_script.sleeps = 0;
    const S_Script saved = g_script;
    const long long x = port();
    g_side = 1;
    g_script = saved;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    g_script.hang = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
}

// ---- the master server's stream ----
// A string of letters (sometimes empty).
std::string Text(int lo, int hi)
{
    std::string s;
    for (int n = I(lo, hi); n > 0; --n)
        s += static_cast<char>("abcdefghij_0123"[I(0, 14)]);
    return s;
}
std::string U16(int v)
{
    std::string s;
    s += static_cast<char>(v >> 8 & 0xFF);
    s += static_cast<char>(v & 0xFF);
    return s;
}
std::string RawU16(u_short v) { return std::string(reinterpret_cast<const char *>(&v), 2); }
std::string RawU32(uint32_t v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
struct S_Key {
    std::string name;
    int type;
};
std::vector<uint32_t> g_ips;   // the round's servers' addresses (the answers and removals name them)
uint32_t NewIp()
{
    const uint32_t ip = P(0.3) && !g_ips.empty() ? g_ips[static_cast<size_t>(I(0, static_cast<int>(g_ips.size()) - 1))] : static_cast<uint32_t>(I(1, 0x7FFFFFFF));
    g_ips.push_back(ip);
    return ip;
}
// A server's record: its flags, address, private address, ICMP address, fixed fields (popular indices in the main
// list), its keys and values.
// (No fixed fields without the list's keys: both read the missing key list.)
// (Both kinds of keys only when `both`: the list's check of a record's full keys starts at its fixed fields, so a
// record cut by a read can pass it and be read past what arrived.)
std::string Record(const std::vector<S_Key> &keys, int popularCount, bool usePopular, uint32_t ip, bool haveKeys = true, bool both = true)
{
    int flags = 0;
    for (int bit : {0x02, 0x08, 0x10, 0x20, 0x40, 0x80, 0x01, 0x04})
        if (P(0.35))
            flags |= bit;
    if (P(0.1))
        flags &= ~(FLAG_HAS_KEYS | FLAG_HAS_FULL_KEYS);
    if (!haveKeys)
        flags &= ~FLAG_HAS_KEYS;
    if (!both && (flags & FLAG_HAS_KEYS))
        flags &= ~FLAG_HAS_FULL_KEYS;
    std::string r(1, static_cast<char>(flags));
    r += RawU32(ip);
    if (flags & FLAG_NONSTANDARD_PORT)
        r += RawU16(htons(static_cast<u_short>(I(1000, 1010))));
    if (flags & FLAG_PRIVATE_IP)
        r += RawU32(static_cast<uint32_t>(I(1, 0x7FFFFFFF)));
    if (flags & FLAG_NONSTANDARD_PRIVATE_PORT)
        r += RawU16(htons(static_cast<u_short>(I(2000, 2010))));
    if (flags & FLAG_ICMP_IP)
        r += RawU32(static_cast<uint32_t>(I(1, 0x7FFFFFFF)));
    if (flags & FLAG_HAS_KEYS) {
        for (const S_Key &k : keys) {
            if (k.type == 0) {
                if (usePopular) {
                    const bool popular = popularCount > 0 && P(0.5);
                    r += static_cast<char>(popular ? I(0, popularCount - 1) : 0xFF);
                    if (!popular)
                        r += Text(0, 6) + '\0';
                } else {
                    r += Text(0, 6) + '\0';
                }
            } else if (k.type == 1) {
                r += static_cast<char>(I(0, 255));
            } else if (k.type == 2) {
                r += RawU16(static_cast<u_short>(I(0, 0xFFFF)));
            }
        }
    }
    if (flags & FLAG_HAS_FULL_KEYS) {
        for (int n = I(0, 3); n > 0; --n)
            r += (P(0.5) ? std::string("hostname") : "k" + Text(1, 3)) + '\0' + Text(0, 5) + '\0';
        r += '\0';
    }
    return r;
}
std::vector<S_Key> KeyList(std::string &out)
{
    std::vector<S_Key> keys;
    const int n = P(0.1) ? 0 : I(1, 5);
    out += static_cast<char>(n);
    for (int i = 0; i < n; ++i) {
        S_Key k{P(0.3) ? std::string("numplayers") : P(0.3) ? std::string("hostname") : P(0.1) ? std::string() : "f" + Text(1, 4), P(0.03) ? 3 : I(0, 2)};
        out += static_cast<char>(k.type);
        out += k.name + '\0';
        keys.push_back(k);
    }
    return keys;
}
// The master server's answer to a list request (the plan: made for each side's challenge at its first read).
void PlanStream(uint32_t options)
{
    S_Plan &p = g_script.plan;
    p = S_Plan{};
    p.waiting = true;
    const int headerSize = I(2, 12);
    const int keySize = P(0.1) ? 0 : I(1, 14);
    p.header += static_cast<char>((headerSize - 2) ^ 0xEC);
    for (int i = 1; i < headerSize - 1; ++i)
        p.header += static_cast<char>(I(0, 255));
    p.header += static_cast<char>(keySize ^ 0xEA);
    for (int i = 0; i < keySize; ++i)
        p.key += static_cast<char>(I(0, 255));
    std::string &s = p.plain;
    const uint32_t ours = static_cast<uint32_t>(I(1, 0x7FFFFFFF));   // (our public address: servers behind it sometimes)
    if (P(0.5))
        g_ips.push_back(ours);
    s += RawU32(ours);
    const bool error = P(0.08);
    s += error ? RawU16(0xFFFF) : RawU16(htons(static_cast<u_short>(I(1, 0xFFFE))));
    if (error)
        s += (P(0.2) ? std::string() : "Bad " + Text(0, 8)) + (P(0.9) ? std::string(1, '\0') : "");
    std::vector<S_Key> keys;
    int popularCount = 0;
    bool haveKeys = false;
    // (No messages after an error: its text stays unread, read as their start; one under 3 bytes long hangs the
    // original.)
    bool ended = !error;
    const bool whole = P(0.3);   // (all of it in one read)
    if (!error && !(options & OPTION_NO_SERVER_LIST)) {
        keys = KeyList(s);
        haveKeys = true;
        popularCount = P(0.2) ? 0 : I(1, 4);
        s += static_cast<char>(popularCount);
        for (int i = 0; i < popularCount; ++i)
            s += (P(0.2) ? std::string("") : "pop" + Text(0, 4)) + '\0';
        for (int n = P(0.1) ? 0 : I(1, 6); n > 0; --n)
            s += Record(keys, popularCount, true, NewIp(), true, whole);
        ended = P(0.9);
        if (ended)
            s += std::string(1, static_cast<char>(I(0, 255))) + RawU32(0xFFFFFFFFu);
    }
    // The messages after the list (none after a list with no end: they would be read as its servers): new keys, a
    // server's update, an echo, a removal, unknown ones.
    for (int n = ended ? I(0, 4) : 0; n > 0; --n) {
        std::string m;
        const int kind = I(1, 6);
        if (kind == MESSAGE_KEY_LIST) {
            keys = KeyList(m);
            haveKeys = true;
            if (P(0.1))
                m.resize(static_cast<size_t>(I(1, static_cast<int>(m.size()))));
        } else if (kind == MESSAGE_SERVER) {
            // (A short one: no fixed fields, the key list may be missing; no port of its own, the original's stays its stack's.)
            m = P(0.05) ? std::string(1, static_cast<char>(I(0, 0x3F) & ~FLAG_NONSTANDARD_PORT)) + Text(0, 4)
                        : Record(keys, 0, false, P(0.6) && !g_ips.empty() ? g_ips[static_cast<size_t>(I(0, static_cast<int>(g_ips.size()) - 1))] : NewIp(), haveKeys);
        } else if (kind == MESSAGE_REMOVE) {
            m = RawU32(!g_ips.empty() && P(0.8) ? g_ips[static_cast<size_t>(I(0, static_cast<int>(g_ips.size()) - 1))] : 5u) + RawU16(htons(static_cast<u_short>(I(1000, 1001))));
            if (P(0.1))
                m.resize(static_cast<size_t>(I(0, 5)));
        } else {
            m = Text(0, 10);
        }
        const int size = P(0.03) ? 0x1001 : static_cast<int>(m.size()) + 3;   // (never under 3: the original stays on such a message for ever)
        s += U16(size) + static_cast<char>(kind) + m;
    }
    // Chunks (a read gets one or part of one); a stream cut short sometimes.
    const int total = static_cast<int>(p.header.size() + p.key.size() + s.size());
    p.stop = !whole && P(0.1) ? I(0, total) : -1;
    p.fails = P(0.03);
    for (int left = total; left > 0;) {
        int c = whole || P(0.3) ? left : I(1, std::max(1, left / 2 + 1));
        if (!whole && p.cuts.empty() && P(0.2))
            c = std::min(left, I(1, 4));   // (the cipher's header cut)
        p.cuts.push_back(c);
        left -= c;
    }
}
// The stream for the reading side's own challenge and key (its cipher as the list will set it up).
void MakeStream()
{
    S_Plan &p = g_script.plan;
    p.pending = false;
    const S_SbServerList &l = g_browser[g_side]->list;
    uint8_t challenge[8];
    std::memcpy(challenge, l.challenge, 8);
    const auto keySize = static_cast<int>(std::strlen(l.queryFromKey));
    for (int i = 0; i < static_cast<int>(p.key.size()); ++i)
        challenge[(l.queryFromKey[i % keySize] * i) % 8] ^= static_cast<uint8_t>(challenge[i % 8] ^ static_cast<uint8_t>(p.key[static_cast<size_t>(i)]));
    S_SbCipher cipher;
    CipherInit(&cipher, challenge, 8);
    std::string crypt = p.plain;
    for (char &c : crypt)
        c = static_cast<char>(CipherEncryptByte(&cipher, static_cast<uint8_t>(c)));
    std::string all = p.header + p.key + crypt;
    if (std::getenv("HD2_SB_DUMP"))
        std::printf("stream (side %d): %s | %s | plain %s\n", g_side, Bytes(p.header.data(), static_cast<int>(p.header.size())).c_str(),
                    Bytes(p.key.data(), static_cast<int>(p.key.size())).c_str(), Bytes(p.plain.data(), static_cast<int>(p.plain.size())).c_str());
    if (p.stop >= 0)
        all.resize(static_cast<size_t>(p.stop));
    size_t at = 0;
    for (int c : p.cuts) {
        if (at >= all.size())
            break;
        g_script.tcp.push_back(all.substr(at, static_cast<size_t>(c)));
        at += static_cast<size_t>(c);
    }
    if (p.fails)
        g_script.tcp.push_back("!");
}

// ---- the servers' answers ----
std::string BinaryAnswer(const S_SbServer *s, int keyCount)
{
    std::string a(1, P(0.95) ? '\0' : 'x');
    a += RawU32(static_cast<uint32_t>(I(0, 0x7FFFFFFF)));
    if (s->state & SERVER_PENDING_BASIC) {
        for (int i = P(0.1) ? I(0, keyCount) : keyCount; i > 0; --i)
            a += Text(0, 5) + '\0';
        if (P(0.15))
            a += "extra" + std::string(1, '\0');   // (a value past the keys)
    } else {
        for (int n = I(0, 3); n > 0; --n)
            a += (P(0.5) ? std::string("hostname") : "k" + Text(1, 3)) + '\0' + Text(0, 5) + '\0';
        a += '\0';
        for (int table = 0; table < 2; ++table) {
            const int rows = P(0.2) ? 0 : I(1, 3);
            a += U16(rows);
            const int columns = I(0, 3);
            for (int c = 0; c < columns; ++c)
                a += (P(0.03) ? std::string(static_cast<size_t>(I(99, 102)), 'n') : "c" + Text(1, 3) + "_") + '\0';
            a += '\0';
            for (int v = rows * columns; v > 0; --v)
                a += Text(0, 4) + '\0';
        }
        if (P(0.15))   // (bytes after the two tables: a third's look)
            a += U16(1) + "cx_" + std::string(1, '\0') + std::string(1, '\0') + "v" + std::string(1, '\0');
    }
    if (P(0.15))
        a.resize(static_cast<size_t>(I(0, static_cast<int>(a.size()))));
    return a;
}
std::string OldAnswer()
{
    std::string a;
    for (int n = I(0, 4); n > 0; --n) {
        const int k = I(0, 9);
        a += "\\" + std::string(k == 0 ? "queryid" : k == 1 ? "hostname" : k == 2 ? "numplayers" : "o" + Text(1, 3)) + "\\" + (k == 2 ? N(I(0, 16)) : Text(0, 4));
    }
    if (P(0.6))
        a += "\\final\\";
    if (P(0.2))
        a += "\\queryid\\1.1";
    return a;
}
// Answers from the servers being asked (by their public or private addresses), others' and failed reads.
void QueueAnswers()
{
    const S_ServerBrowser *b = g_browser[0];
    int n = 0;
    for (const S_SbServer *s = b->engine.querying.first; s && n < 50; s = s->next, ++n) {
        if (!P(0.6))
            continue;
        S_Item item;
        item.data = b->engine.queryVersion == 1 ? BinaryAnswer(s, b->engine.keyCount) : OldAnswer();
        if (item.data.empty())
            item.data = "\\final\\";   // (an empty old answer is read from its second byte: the stack's)
        const bool privately = (s->flags & FLAG_PRIVATE_IP) && P(0.5);
        item.ip = privately ? s->privateIp : s->publicIp;
        item.port = privately ? s->privatePort : s->publicPort;
        g_script.udp.push_back(item);
    }
    for (int k = I(0, 2); k > 0; --k)
        g_script.udp.push_back(S_Item{P(0.5) ? "!" : std::string(1, '\0') + "junk", static_cast<uint32_t>(I(1, 99)), htons(5)});
}

// ---- the world ----
void FreeWorld()
{
    if (g_browser[0]) {
        ServerBrowserClear(g_browser[0]);
        ArrayFree(g_browser[0]->list.servers);
        std::free(g_browser[0]);
    }
    if (g_browser[1])
        O_CALL(0x0073add0, void(FAST *)(S_ServerBrowser *))(g_browser[1]);   // (ServerBrowserFree)
    g_browser[0] = g_browser[1] = nullptr;
    g_listOwner[0] = g_listOwner[1] = nullptr;
    if (t_sharedStrings)
        TableFree(t_sharedStrings);
    t_sharedStrings = nullptr;
    if (g_oShared)
        O_CALL(0x00736e40, void(FAST *)(S_HashTable *))(g_oShared);
    g_oShared = nullptr;
}
void SetWorld()
{
    FreeWorld();
    g_ips.clear();
    S_Script &w = g_script;
    w.ticks = static_cast<uint32_t>(I(0, 300000));
    w.step = P(0.4) ? 0 : static_cast<uint32_t>(I(1, 900));
    w.randState = static_cast<unsigned>(I(0, 0x7FFFFFFF));
    w.udp.clear();
    w.lan.clear();
    w.tcp.clear();
    w.plan = S_Plan{};
    w.closeWhenEmpty = false;
    w.dnsFails = P(0.05);
    w.connectFails = P(0.05);
    w.udpSocketFails = P(0.02);
    w.tcpSocketFails = P(0.03);
    w.sends.clear();
    if (P(0.15))
        for (int n = I(1, 4); n > 0; --n)
            w.sends.push_back(I(-1, 2));
    w.clearOnError = P(0.3);
    const char *master = P(0.1) ? (P(0.5) ? "10.1.2.3" : "master.test") : nullptr;
    g_sbMasterOverride = master;
    G<const char *>(0x00982de4) = master;
    // The browser (each side's).
    const std::string game = P(0.8) ? "hd2" : Text(1, 12);
    const std::string key = P(0.8) ? "sK8pQ9" : Text(1, 9);
    const int version = I(0, 3), maxUpdates = P(0.2) ? I(0, 2) : I(3, 20), queryVersion = P(0.7) ? 1 : I(0, 2);
    Run("ServerBrowserNew",
        [&] {
            g_browser[0] = ServerBrowserNew(game.c_str(), "hd2", key.c_str(), version, maxUpdates, queryVersion, &BrowserEvent, INSTANCE);
            return 0;
        },
        [&] {
            g_browser[1] = O_CALL(0x0073ab70, S_ServerBrowser *(FAST *)(const char *, const char *, const char *, int, int, int, void *, void *))(
                game.c_str(), "hd2", key.c_str(), version, maxUpdates, queryVersion, reinterpret_cast<void *>(&OBrowserEvent), INSTANCE);
            // (The original leaves these two as the heap gave them; the port sets them 0.)
            g_browser[1]->disconnectOnComplete = 0;
            g_browser[1]->triggerIp = 0;
            g_browser[1]->triggerPort = 0;
            return 0;
        });
    g_listOwner[0] = g_browser[0];
    g_listOwner[1] = g_browser[1];
}

S_SbServer *ServerAt(int side, int i) { return *static_cast<S_SbServer **>(const_cast<void *>(ArrayAt(g_browser[side]->list.servers, i))); }
int ServerCount() { return ArrayCount(g_browser[0]->list.servers); }

// Some fields given alike on both sides.
template <class F> void Both(F f)
{
    f(g_browser[0]);
    f(g_browser[1]);
}

void Call()
{
    S_ServerBrowser *pb = g_browser[0], *ob = g_browser[1];
    const int op = I(0, 19);
    switch (op) {
    case 0:
    case 1:
    case 2:
    case 3: {
        // The think: the servers' answers, the master server's chunks. (A server being asked as the trigger update's
        // sometimes: its answer ends the wait.)
        if (pb->engine.querying.first && P(0.5)) {
            int k = I(0, pb->engine.querying.count - 1);
            const S_SbServer *t = pb->engine.querying.first;
            while (k-- > 0 && t->next)
                t = t->next;
            const uint32_t ip = t->publicIp;
            const u_short port = t->publicPort;
            Both([&](S_ServerBrowser *b) {
                b->triggerIp = ip;
                b->triggerPort = port;
            });
        }
        QueueAnswers();
        Run("ServerBrowserThink", [&] { return static_cast<long long>(ServerBrowserThink(pb)); },
            [&] { return static_cast<long long>(O_CALL(0x0073b240, int(FAST *)(S_ServerBrowser *))(ob)); });
        g_script.udp.clear();
        break;
    }
    case 4:
    case 5: {
        // An update (asynchronous mostly; a synchronous one thinks to its end: the master server closes). (Not on a
        // list with unread bytes: a new request keeps them, read as the new stream's start; the game clears first.)
        if (pb->list.inBuffer && pb->list.inBufferSize > 0)
            break;
        uint8_t fields[48];
        int count = P(0.1) ? 0 : I(1, 12);
        for (uint8_t &f : fields)
            f = static_cast<uint8_t>(P(0.85) ? I(1, 0x1D) : I(0x33, 0x35));
        if (P(0.1)) {
            // A long list: its text (\name each) reaches 255 or 256 letters exactly at a field, then more fields.
            const int target = 0xFF + I(0, 1);
            for (int attempt = 0; attempt < 200; ++attempt) {
                int length = 0, n = 0, tries = 0;
                while (n < 40 && ++tries < 400) {   // (no field fits what is left: this attempt given up)
                    const int left = target - length;
                    int exact = -1;
                    for (int k = 1; k <= 0x1D && exact < 0; ++k)
                        if (static_cast<int>(std::strlen(g_qr2KeyNames[k])) + 1 == left)
                            exact = k;
                    if (exact >= 0 && (left < 15 || P(0.2))) {
                        fields[n++] = static_cast<uint8_t>(exact);
                        break;
                    }
                    const int k = I(1, 0x1D);
                    if (static_cast<int>(std::strlen(g_qr2KeyNames[k])) + 1 >= left)
                        continue;
                    fields[n++] = static_cast<uint8_t>(k);
                    length += static_cast<int>(std::strlen(g_qr2KeyNames[k])) + 1;
                }
                if (n < 40 && n > 0 && tries < 400 && length + static_cast<int>(std::strlen(g_qr2KeyNames[fields[n - 1]])) + 1 == target) {
                    fields[n] = static_cast<uint8_t>(I(1, 0x1D));
                    fields[n + 1] = 1;
                    count = n + 2;
                    break;
                }
            }
        }
        const bool async = P(0.7);
        const int disconnect = I(0, 1);
        const std::string filter = P(0.5) ? "" : P(0.05) ? std::string(0x101, 'f') : "numplayers>" + N(I(0, 9));
        PlanStream(0);
        g_script.closeWhenEmpty = !async;
        Run("ServerBrowserUpdate " + N(async), [&] { ServerBrowserUpdate(pb, async, disconnect, fields, count, filter.c_str()); return 0; },
            [&] {
                O_CALL(0x0073af00, void(FAST *)(S_ServerBrowser *, int, int, const uint8_t *, int, const char *))(ob, async, disconnect, fields, count, filter.c_str());
                return 0;
            });
        g_script.closeWhenEmpty = false;
        g_script.udp.clear();
        break;
    }
    case 6: {
        // A request with options (the source address, a count, no list).
        if (pb->list.inBuffer && pb->list.inBufferSize > 0)
            break;
        uint32_t options = 0;
        for (uint32_t bit : {1u, 2u, 8u, 0x80u, 0x10u})
            if (P(0.4))
                options |= bit;
        const uint32_t max = static_cast<uint32_t>(I(0, 500));
        const uint32_t source = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
        Both([&](S_ServerBrowser *b) { b->list.sourceIp = source; });
        // (Texts of the limit's length, or one over, sometimes.)
        const std::string longFields(static_cast<size_t>(0x100 + I(0, 1)), 'h'), longText(static_cast<size_t>(0x100 + I(0, 1)), 'q');
        const char *fields = P(0.1) ? longFields.c_str() : P(0.5) ? nullptr : P(0.1) ? "\\x" : "\\hostname\\numplayers";
        const char *filter = P(0.1) ? longText.c_str() : P(0.5) ? nullptr : "x";
        PlanStream(options);
        Run("ListConnectAndQuery " + N(options), [&] { return static_cast<long long>(ListConnectAndQuery(&pb->list, fields, filter, options, max)); },
            [&] {
                return static_cast<long long>(O_CALL(0x0073b4a0, int(FAST *)(S_SbServerList *, const char *, const char *, uint32_t, uint32_t))(&ob->list, fields, filter, options, max));
            });
        break;
    }
    case 7:
        Run("ServerBrowserClear", [&] { ServerBrowserClear(pb); return 0; }, [&] { O_CALL(0x0073b280, void(FAST *)(S_ServerBrowser *))(ob); return 0; });
        break;
    case 8: {
        // A server's values (one of the list's).
        if (ServerCount() < 1)
            break;
        const int i = I(0, ServerCount() - 1);
        S_SbServer *ps = ServerAt(0, i), *os = ServerAt(1, i);
        const char *key = std::vector<const char *>{"hostname", "numplayers", "ping", "nothing", "k", "f", "fabc", "c_0"}[static_cast<size_t>(I(0, 7))];
        const int which = I(0, 4);
        if (which == 0)
            Run("SBServerGetStringValue", [&] { return static_cast<long long>(std::hash<std::string>()(Str(SBServerGetStringValue(ps, key, "fb")))); },
                [&] { return static_cast<long long>(std::hash<std::string>()(Str(O_CALL(0x0073a3f0, const char *(FAST *)(S_SbServer *, const char *, const char *))(os, key, "fb")))); });
        else if (which == 1)
            Run("SBServerGetIntValue", [&] { return static_cast<long long>(SBServerGetIntValue(ps, key, -3)); },
                [&] { return static_cast<long long>(O_CALL(0x0073a420, int(FAST *)(S_SbServer *, const char *, int))(os, key, -3)); });
        else if (which == 2) {
            double v[2];
            Run("SBServerGetFloatValue", [&] { v[0] = SBServerGetFloatValue(ps, key, 2.5); return 0; },
                [&] { v[1] = O_CALL(0x0073a470, double(FAST *)(S_SbServer *, const char *, double))(os, key, 2.5); return 0; });
            Check(std::memcmp(&v[0], &v[1], sizeof v[0]) == 0, "the float " + std::to_string(v[1]) + " / " + std::to_string(v[0]));
        } else if (which == 3)
            Run("SBServerGetPing", [&] { return static_cast<long long>(SBServerGetPing(ps)); }, [&] { return static_cast<long long>(O_CALL(0x0073aa10, uint32_t(FAST *)(S_SbServer *))(os)); });
        else
            Run("SBServerGetPublicInetAddress", [&] { return static_cast<long long>(SBServerGetPublicInetAddress(ps)); },
                [&] { return static_cast<long long>(O_CALL(0x0073a6a0, uint32_t(FAST *)(S_SbServer *))(os)); });
        break;
    }
    case 9: {
        // The cipher: its set-up by a key (none sometimes), bytes decrypted and encrypted.
        S_SbCipher c[2];
        std::memset(c, 0x5A, sizeof c);
        uint8_t key[40];
        const int keySize = P(0.1) ? 0 : I(1, 40);
        for (uint8_t &k : key)
            k = static_cast<uint8_t>(I(0, 255));
        char data[2][64];
        const int size = I(0, 64);
        for (int i = 0; i < 64; ++i)
            data[0][i] = data[1][i] = static_cast<char>(I(0, 255));
        const uint8_t extra = static_cast<uint8_t>(I(0, 255));
        Run("Cipher " + N(keySize) + " " + N(size),
            [&] {
                CipherInit(&c[0], key, static_cast<uint8_t>(keySize));
                CipherDecrypt(&c[0], data[0], size);
                return static_cast<long long>(CipherEncryptByte(&c[0], extra)) * 256 + CipherDecryptByte(&c[0], extra);
            },
            [&] {
                O_CALL(0x007397b0, void(FAST *)(S_SbCipher *, const uint8_t *, uint8_t))(&c[1], key, static_cast<uint8_t>(keySize));
                O_CALL(0x00739b60, void(FAST *)(S_SbCipher *, char *, int))(&c[1], data[1], size);
                const long long e = O_CALL(0x00739910, uint8_t(FAST *)(S_SbCipher *, uint8_t))(&c[1], extra);
                return e * 256 + O_CALL(0x00739a50, uint8_t(FAST *)(S_SbCipher *, uint8_t))(&c[1], extra);
            });
        Check(std::memcmp(&c[0], &c[1], sizeof c[0]) == 0 && std::memcmp(data[0], data[1], sizeof data[0]) == 0, "the cipher");
        break;
    }
    case 10: {
        // The parsing helpers on bytes (a string's size, a hash, a header's size, the full keys there, the fixed fields
        // there by the list's keys, an address).
        std::string d;
        for (int n = I(0, 24); n > 0; --n)
            d += P(0.3) ? '\0' : static_cast<char>(I(1, 255));
        d.resize(d.size() + 8, '\0');
        const int size = I(0, static_cast<int>(d.size()) - 8);
        const int which = I(0, 5);
        if (which == 0)
            Run("StringSize", [&] { return static_cast<long long>(StringSize(d.data(), size)); },
                [&] { return static_cast<long long>(O_CALL(0x0073b3b0, int(FAST *)(const char *, int))(d.data(), size)); });
        else if (which == 1) {
            const int buckets = I(1, 600);
            Run("HashString", [&] { return static_cast<long long>(HashString(d.data(), buckets)); },
                [&] { return static_cast<long long>(O_CALL(0x0073a9d0, int(FAST *)(const char *, int))(d.data(), buckets)); });
        } else if (which == 2) {
            const auto flags = static_cast<uint8_t>(I(0, 255));
            Run("ServerHeaderSize", [&] { return static_cast<long long>(ServerHeaderSize(flags)); },
                [&] { return static_cast<long long>(O_CALL(0x0073c480, int(FAST *)(uint8_t))(flags)); });
        } else if (which == 3)
            Run("HasFullKeys", [&] { return static_cast<long long>(HasFullKeys(d.data(), size)); },
                [&] { return static_cast<long long>(O_CALL(0x0073c4b0, uint8_t(FAST *)(const char *, int))(d.data(), size)); });
        else if (which == 4) {
            if (!pb->list.keyList)
                break;
            if (P(0.5))
                d[0] = static_cast<char>(0xFF);
            Run("HasFixedFields", [&] { return static_cast<long long>(HasFixedFields(&pb->list, d.data(), size)); },
                [&] { return static_cast<long long>(O_CALL(0x0073c4f0, uint8_t(FAST *)(S_SbServerList *, const char *, int))(&ob->list, d.data(), size)); });
        } else {
            uint32_t ip[2] = {7, 7};
            u_short port[2] = {9, 9};
            Run("ReadAddress", [&] { ReadAddress(&pb->list, d.data(), size, &ip[0], &port[0]); return 0; },
                [&] { O_CALL(0x0073c580, void(FAST *)(S_SbServerList *, const char *, int, uint32_t *, u_short *))(&ob->list, d.data(), size, &ip[1], &port[1]); return 0; });
            Check(ip[0] == ip[1] && port[0] == port[1], "the address");
        }
        break;
    }
    case 11: {
        // A detached server: its values set, an old answer, a full answer, a record (the list's keys).
        S_SbServer *s[2] = {nullptr, nullptr};
        const uint32_t ip = static_cast<uint32_t>(I(1, 0x7FFFFFFF));
        const u_short port = static_cast<u_short>(I(1, 0xFFFF));
        Run("NewServer", [&] { s[0] = NewServer(ip, port); return 0; },
            [&] { s[1] = O_CALL(0x0073aa20, S_SbServer *(FAST *)(S_SbServerList *, uint32_t, u_short))(&ob->list, ip, port); return 0; });
        const auto same = [&] { Check(ServerText(s[0]) == ServerText(s[1]), "the server" + Diff(ServerText(s[1]), ServerText(s[0]))); };
        same();
        for (int k = I(1, 3); k > 0; --k) {
            const int which = I(0, 4);
            if (which == 0) {
                const std::string key = P(0.5) ? "hostname" : Text(0, 3), value = Text(0, 5);
                Run("SetValue", [&] { SetValue(s[0], key.c_str(), value.c_str()); return 0; },
                    [&] { O_CALL(0x0073a330, void(FAST *)(S_SbServer *, const char *, const char *))(s[1], key.c_str(), value.c_str()); return 0; });
            } else if (which == 1) {
                const int v = I(-100000, 100000);
                Run("SetIntValue", [&] { SetIntValue(s[0], "numplayers", v); return 0; },
                    [&] { O_CALL(0x0073a370, void(FAST *)(S_SbServer *, const char *, int))(s[1], "numplayers", v); return 0; });
            } else if (which == 2) {
                const std::string a = OldAnswer() + (P(0.3) ? "\\" : "") + (P(0.2) ? "\\\\x" : "");
                std::vector<char> b[2] = {std::vector<char>(a.begin(), a.end()), std::vector<char>(a.begin(), a.end())};
                b[0].resize(a.size() + 2, 0);   // (an empty answer is read from its second byte: past its end)
                b[1].resize(a.size() + 2, 0);
                Run("ParseOldReply " + a, [&] { ParseOldReply(s[0], b[0].data()); return 0; },
                    [&] { O_CALL(0x0073a740, void(FAST *)(S_SbServer *, char *))(s[1], b[1].data()); return 0; });
                Check(b[0] == b[1], "the answer's text");
            } else if (which == 3) {
                S_SbServer probe{};
                probe.state = 0;
                std::string a = BinaryAnswer(&probe, 0).substr(0);
                a = a.size() > 5 ? a.substr(5) : std::string();
                a.resize(a.size() + 4, '\0');
                const int size = static_cast<int>(a.size()) - 4 - (P(0.2) ? I(0, static_cast<int>(a.size()) - 4) : 0);
                Run("ParseFullReply", [&] { ParseFullReply(s[0], a.data(), size); return 0; },
                    [&] { O_CALL(0x0073a860, void(FAST *)(S_SbServer *, const char *, int))(s[1], a.data(), size); return 0; });
            } else {
                if (!pb->list.keyList)
                    continue;
                std::vector<S_Key> keys;
                for (int i = 0; i < ArrayCount(pb->list.keyList); ++i) {
                    const auto *lk = static_cast<const S_ListKey *>(ArrayAt(pb->list.keyList, i));
                    keys.push_back(S_Key{lk->name, lk->type});
                }
                const bool popular = pb->list.popularCount > 0 && P(0.5);
                std::string r = Record(keys, popular ? pb->list.popularCount : 0, popular, ip);
                r.resize(r.size() + 8, '\0');
                const int size = static_cast<int>(r.size()) - 8;
                Run("ParseServer " + N(popular), [&] { return static_cast<long long>(ParseServer(&pb->list, s[0], r.data(), size, popular)); },
                    [&] { return static_cast<long long>(O_CALL(0x0073c5d0, int(FAST *)(S_SbServerList *, S_SbServer *, const char *, int, int))(&ob->list, s[1], r.data(), size, popular)); });
            }
            same();
        }
        Run("FreeServer", [&] { FreeServer(s[0]); return 0; }, [&] { O_CALL(0x0073a310, void(FAST *)(S_SbServer **))(&s[1]); return 0; });   // (the original's takes the server's address)
        break;
    }
    case 12: {
        // The list's helpers: find a server (by address, by itself), remove one, the dead list freed.
        const int which = I(0, 3);
        if (which == 0) {
            const int i = ServerCount() > 0 && P(0.7) ? I(0, ServerCount() - 1) : -1;
            const uint32_t ip = i >= 0 ? ServerAt(0, i)->publicIp : static_cast<uint32_t>(I(0, 99));
            const u_short port = i >= 0 && P(0.9) ? ServerAt(0, i)->publicPort : static_cast<u_short>(I(0, 99));
            Run("FindServer", [&] { return static_cast<long long>(FindServer(&pb->list, ip, port)); },
                [&] { return static_cast<long long>(O_CALL(0x0073cd30, int(FAST *)(S_SbServerList *, uint32_t, u_short))(&ob->list, ip, port)); });
        } else if (which == 1) {
            const int i = ServerCount() > 0 && P(0.8) ? I(0, ServerCount() - 1) : -1;
            S_SbServer other{};
            Run("ListFindServer", [&] { return static_cast<long long>(ListFindServer(&pb->list, i >= 0 ? ServerAt(0, i) : &other)); },
                [&] { return static_cast<long long>(O_CALL(0x0073ccf0, int(FAST *)(S_SbServerList *, S_SbServer *))(&ob->list, i >= 0 ? ServerAt(1, i) : &other)); });
        } else if (which == 2) {
            if (ServerCount() < 1)
                break;
            const int i = I(0, ServerCount() - 1);
            Run("RemoveFromList " + N(i), [&] { RemoveFromList(&pb->list, i); return 0; },
                [&] { O_CALL(0x0073cd90, void(FAST *)(S_SbServerList *, int))(&ob->list, i); return 0; });
        } else {
            Run("FreeDeadList", [&] { FreeDeadList(&pb->list); return 0; }, [&] { O_CALL(0x0073ce10, void(FAST *)(S_SbServerList *))(&ob->list); return 0; });
        }
        break;
    }
    case 13: {
        // Data sent to the master server (reconnecting once).
        const std::string d = std::string("\x00\x07\x02", 3) + Text(0, 4);
        if (!g_script.sends.empty() && g_script.sends.front() <= 0)   // (a new stream only for the reconnection: the list's own goes on)
            PlanStream(2);
        Run("ListSendData", [&] { return static_cast<long long>(ListSendData(&pb->list, d.data(), static_cast<int>(d.size()))); },
            [&] { return static_cast<long long>(O_CALL(0x0073bc60, int(FAST *)(S_SbServerList *, const char *, int))(&ob->list, d.data(), static_cast<int>(d.size()))); });
        break;
    }
    case 14: {
        // Waiting for a trigger update (none waited for; the list's state 0 by the master; the master server's close).
        const int kind = I(0, 2);
        const bool viaMaster = kind != 0 && P(0.5);
        // (A server of the list's sometimes: its update ends the wait.)
        const int i = ServerCount() > 0 && P(0.5) ? I(0, ServerCount() - 1) : -1;
        const uint32_t trigger = kind == 0 ? 0 : i >= 0 ? ServerAt(0, i)->publicIp : static_cast<uint32_t>(I(1, 99));
        const u_short triggerPort = i >= 0 && P(0.9) ? ServerAt(0, i)->publicPort : static_cast<u_short>(I(0, 9));
        if (kind == 1 && !(viaMaster && pb->list.state == 0))
            break;
        if (kind == 2 && pb->list.state != LIST_STATE_CONNECTED && pb->list.state != LIST_STATE_MAIN_LIST)
            break;
        Both([&](S_ServerBrowser *b) {
            b->triggerIp = trigger;
            b->triggerPort = triggerPort;
        });
        if (P(0.5))
            QueueAnswers();
        g_script.closeWhenEmpty = true;
        Run("ServerBrowserWaitForTrigger", [&] { ServerBrowserWaitForTrigger(pb, viaMaster); return 0; },
            [&] { O_CALL(0x0073b100, void(FAST *)(S_ServerBrowser *, int))(ob, viaMaster); return 0; });
        g_script.closeWhenEmpty = false;
        break;
    }
    case 15: {
        // The messages' readers on bytes (the list's keys, a server's update, a removal).
        // (No new keys while the main list is read: its servers' fields follow its own keys.)
        const int which = pb->list.state == LIST_STATE_MAIN_LIST ? I(1, 2) : I(0, 2);
        std::string m;
        if (which == 0) {
            KeyList(m);
        } else if (which == 1) {
            std::vector<S_Key> keys;
            for (int i = 0; i < ArrayCount(pb->list.keyList); ++i) {
                const auto *lk = static_cast<const S_ListKey *>(ArrayAt(pb->list.keyList, i));
                keys.push_back(S_Key{lk->name, lk->type});
            }
            m = Record(keys, 0, false, ServerCount() > 0 && P(0.5) ? ServerAt(0, I(0, ServerCount() - 1))->publicIp : NewIp(), pb->list.keyList != nullptr);
        } else {
            const int i = ServerCount() > 0 && P(0.7) ? I(0, ServerCount() - 1) : -1;
            m = RawU32(i >= 0 ? ServerAt(0, i)->publicIp : 3u) + RawU16(i >= 0 ? ServerAt(0, i)->publicPort : 4);
        }
        if (P(0.2))
            m.resize(static_cast<size_t>(I(0, static_cast<int>(m.size()))));
        if (which == 1 && (m.size() == 5 || m.size() == 6) && (m[0] & FLAG_NONSTANDARD_PORT))
            m.resize(4);   // (its port unread: the original's is its stack's)
        m.resize(m.size() + 8, '\0');
        const int size = static_cast<int>(m.size()) - 8;
        static const uint32_t at[3] = {0x0073c8a0, 0x0073c9b0, 0x0073c970};
        Run("ReadMessage " + N(which) + " " + N(size),
            [&] {
                return static_cast<long long>(which == 0 ? ReadKeyListMessage(&pb->list, m.data(), size)
                                              : which == 1 ? ReadServerMessage(&pb->list, m.data(), size)
                                                           : ReadRemoveMessage(&pb->list, m.data(), size));
            },
            [&] { return static_cast<long long>(O_CALL(at[which], int(FAST *)(S_SbServerList *, const char *, int))(&ob->list, m.data(), size)); });
        break;
    }
    case 16: {
        // The engine by itself: a list server queued (front or back; basic, full or neither), removed, the keys, the
        // engine cleared.
        const int which = I(0, 4);
        if (which <= 1) {
            if (ServerCount() < 1)
                break;
            // (One in neither of the engine's lists: one there would be linked twice.)
            std::vector<int> free;
            for (int i = 0; i < ServerCount(); ++i) {
                bool queued = false;
                for (const S_SbQueryList *q : {&pb->engine.querying, &pb->engine.pending})
                    for (const S_SbServer *x = q->first; x && !queued; x = x->next)
                        queued = x == ServerAt(0, i);
                if (!queued)
                    free.push_back(i);
            }
            if (free.empty())
                break;
            const int i = free[static_cast<size_t>(I(0, static_cast<int>(free.size()) - 1))];
            S_SbServer *ps = ServerAt(0, i), *os = ServerAt(1, i);
            const bool front = P(0.5);
            const int mode = I(0, 2);
            Run("QueueQuery " + N(front) + " " + N(mode), [&] { QueueQuery(&pb->engine, ps, front, mode); return 0; },
                [&] { O_CALL(0x00739c90, void(FAST *)(S_SbQueryEngine *, S_SbServer *, int, int))(&ob->engine, os, front, mode); return 0; });
        } else if (which == 2) {
            if (ServerCount() < 1)
                break;
            const int i = I(0, ServerCount() - 1);
            Run("EngineRemove", [&] { EngineRemove(&pb->engine, ServerAt(0, i)); return 0; },
                [&] { O_CALL(0x0073a210, void(FAST *)(S_SbQueryEngine *, S_SbServer *))(&ob->engine, ServerAt(1, i)); return 0; });
        } else if (which == 3) {
            const auto key = static_cast<uint8_t>(I(1, 0x1D));
            for (int k = P(0.1) ? 21 : 1; k > 0; --k)
                Run("EngineAddKey", [&] { EngineAddKey(&pb->engine, key); return 0; },
                    [&] { O_CALL(0x0073a1f0, void(FAST *)(S_SbQueryEngine *, uint8_t))(&ob->engine, key); return 0; });
        } else {
            Run("EngineClear", [&] { EngineClear(&pb->engine); return 0; }, [&] { O_CALL(0x00739c40, void(FAST *)(S_SbQueryEngine *))(&ob->engine); return 0; });
        }
        break;
    }
    case 17: {
        // The LAN answers (only the dead LAN update starts them: its state and socket given alike).
        const uint32_t start = g_script.ticks - static_cast<uint32_t>(P(0.3) ? 2000 + I(0, 1) : I(0, 4000));   // (the time-out's edge sometimes)
        Both([&](S_ServerBrowser *b) {
            b->list.state = LIST_STATE_LAN;
            b->list.socket = LAN_SOCKET;
            b->list.lanStartTime = start;
        });
        for (int k = I(0, 4); k > 0; --k) {
            const int i = ServerCount() > 0 && P(0.3) ? I(0, ServerCount() - 1) : -1;
            g_script.lan.push_back(S_Item{P(0.15) ? "!" : Text(0, 9), i >= 0 ? ServerAt(0, i)->publicIp : NewIp(), i >= 0 ? ServerAt(0, i)->publicPort : htons(6500)});
        }
        QueueAnswers();
        Run("ServerBrowserThink (LAN)", [&] { return static_cast<long long>(ServerBrowserThink(pb)); },
            [&] { return static_cast<long long>(O_CALL(0x0073b240, int(FAST *)(S_ServerBrowser *))(ob)); });
        g_script.lan.clear();
        g_script.udp.clear();
        break;
    }
    case 18: {
        // The shared strings: one added and released (only the test's own: the list's and servers' are theirs), the
        // table freed once empty.
        static std::vector<std::string> added;
        if (g_round != g_addedRound) {
            added.clear();
            g_addedRound = g_round;
        }
        int which = I(0, 2);
        if (which == 1 && added.empty())
            which = 0;
        std::string text = P(0.5) ? "hostname" : Text(0, 3);
        if (which == 1) {
            const size_t k = static_cast<size_t>(I(0, static_cast<int>(added.size()) - 1));
            text = added[k];
            added.erase(added.begin() + static_cast<std::ptrdiff_t>(k));
        } else if (which == 0) {
            added.push_back(text);
        }
        if (which == 0)
            Run("AddSharedString", [&] { AddSharedString(text.c_str()); return 0; },
                [&] { O_CALL(0x0073cec0, const char *(FAST *)(void *, const char *))(nullptr, text.c_str()); return 0; });   // (an unused list first)
        else if (which == 1)
            Run("ReleaseSharedString", [&] { ReleaseSharedString(text.c_str()); return 0; },
                [&] { O_CALL(0x0073cf20, void(FAST *)(void *, const char *))(nullptr, text.c_str()); return 0; });
        else
            Run("SbFreeSharedStrings", [&] { SbFreeSharedStrings(); return 0; }, [&] { OFreeSharedStrings(); return 0; });
        break;
    }
    default: {
        // The list disconnected, or its error (as the API's callers see it).
        if (P(0.5))
            Run("ListDisconnect", [&] { ListDisconnect(&pb->list); return 0; }, [&] { O_CALL(0x0073bac0, void(FAST *)(S_SbServerList *))(&ob->list); return 0; });
        else
            Run("ListError", [&] { ListError(&pb->list); return 0; }, [&] { O_CALL(0x0073bcd0, void(FAST *)(S_SbServerList *))(&ob->list); return 0; });
        break;
    }
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n   (ops:%s)\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, g_round,
                g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    for (size_t i = g_log[g_side].size() > 6 ? g_log[g_side].size() - 6 : 0; i < g_log[g_side].size(); ++i)
        std::printf("    last: %s\n", g_log[g_side][i].substr(0, 200).c_str());
    // The stack's words that look like code addresses (the test's, the original's).
    const auto *sp = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(e->ContextRecord->Esp));
    std::printf("    base %p, stack:", static_cast<void *>(GetModuleHandleA(nullptr)));
    for (int i = 0; i < 96; ++i)
        std::printf(" %08x", sp[i]);
    std::printf("\n");
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

// A watchdog: no progress for 20 s (a loop that never calls out): where the main thread is.
DWORD WINAPI Watchdog(void *)
{
    long last = -1;
    for (;;) {
        Sleep(20000);
        if (g_progress != last) {
            last = g_progress;
            continue;
        }
        SuspendThread(g_mainThread);
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_FULL;
        GetThreadContext(g_mainThread, &c);
        std::printf("STUCK in round %d, %s, %s's side, at %08lx (base %p), stack:", g_round, g_line.c_str(), g_side ? "the original" : "the port", c.Eip,
                    static_cast<void *>(GetModuleHandleA(nullptr)));
        const auto *sp = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(c.Esp));
        for (int i = 0; i < 64; ++i)
            std::printf(" %08x", sp[i]);
        std::printf("\n");
        if (g_listOwner[g_side]) {   // (the list's unread bytes)
            const char *l = reinterpret_cast<const char *>(g_listOwner[g_side]) + 0x48;
            const char *in = *reinterpret_cast<char *const *>(l + 0x74);
            const int n = *reinterpret_cast<const int *>(l + 0x78);
            std::printf("  in %d:%s\n", n, in ? Bytes(in, std::min(n, 64)).c_str() : "null");
        }
        std::fflush(stdout);
        ExitProcess(5);
    }
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
    if (std::getenv("HD2_SB_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("browser_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    O_CALL(0x007e599c, void (*)())();   // (the original's CRT's float conversions)
    g_makeStream = MakeStream;
    // The key names: the original's texts; the game's three registered on both sides.
    for (int k = 0; k <= 0x1D; ++k)
        g_qr2KeyNames[k] = ONames()[k];
    static const char *names[] = {"isdedicated", "voicechat", "expansion"};
    for (int k = 0x33; k <= 0x35; ++k) {
        g_qr2KeyNames[k] = names[k - 0x33];
        ONames()[k] = names[k - 0x33];
    }
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(OMalloc));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(OFree));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e6393, reinterpret_cast<const void *>(ORand));
    RedirectOriginal(0x007e6386, reinterpret_cast<const void *>(OSrand));
    RedirectOriginal(0x00737c90, reinterpret_cast<const void *>(OGetTickCount));
    RedirectOriginal(0x00737ca0, reinterpret_cast<const void *>(OMsleep));
    RedirectOriginal(0x0073a240, reinterpret_cast<const void *>(OSharedStrings));
    RedirectOriginal(0x0073a2d0, reinterpret_cast<const void *>(OFreeSharedStrings));
    RedirectImport(reinterpret_cast<const void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetTickCount")), reinterpret_cast<const void *>(&FakeGetTickCount));
    RedirectImport(reinterpret_cast<const void *>(GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep")), reinterpret_cast<const void *>(&FakeSleep));
    const std::pair<const char *, const void *> sockets[] = {
        {"select", reinterpret_cast<const void *>(&FakeSelect)},         {"recvfrom", reinterpret_cast<const void *>(&FakeRecvfrom)},
        {"recv", reinterpret_cast<const void *>(&FakeRecv)},             {"sendto", reinterpret_cast<const void *>(&FakeSendto)},
        {"send", reinterpret_cast<const void *>(&FakeSend)},             {"socket", reinterpret_cast<const void *>(&FakeSocket)},
        {"connect", reinterpret_cast<const void *>(&FakeConnect)},       {"gethostbyname", reinterpret_cast<const void *>(&FakeGethostbyname)},
        {"inet_addr", reinterpret_cast<const void *>(&FakeInetAddr)},    {"closesocket", reinterpret_cast<const void *>(&FakeClosesocket)},
        {"WSAStartup", reinterpret_cast<const void *>(&FakeWSAStartup)}};
    for (const char *dll : {"ws2_32.dll", "wsock32.dll"}) {
        HMODULE m = LoadLibraryA(dll);
        for (const auto &s : sockets)
            RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, s.first)), s.second);
    }

    int rounds = std::getenv("HD2_SB_QUICK") ? 6000 : 25000;
    if (const char *r = std::getenv("HD2_SB_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_SB_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    const int first = std::getenv("HD2_SB_FIRST") ? std::atoi(std::getenv("HD2_SB_FIRST")) : 0;   // (from this round on)
    for (g_round = first; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        g_trace.clear();
        if (std::getenv("HD2_SB_PROGRESS"))
            std::printf("round %d\n", g_round);
        SetWorld();
        for (int k = I(1, 10); k > 0; --k)
            Call();
    }
    FreeWorld();
    std::printf("browser_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
