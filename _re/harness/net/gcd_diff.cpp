// Net part 13b: GameSpy's CD key client and server (net/gamespy_gcd.cpp: the response to a challenge, the products and
// their clients, the validation server's packets, the checks' think, the installer key's check) against the original
// executable's. MinGW; build_gcd.bat; runs gcd_diff.exe ("gcd_diff: N checks, 0 failures"). The port's file is
// included here (its state and helpers are its own TU's), so the mutation runner rebuilds this file
// (MUT_WRAPPER=gcd_diff.cpp). (The keys are the test's made-up ones.)
//
// The original's SDK is __fastcall: each side gets its own callbacks, logging alike. Each round makes the same
// products and clients on both sides (their ids, keys, session keys, addresses, times, tries, states, errors and
// requests), the socket and the validation server's address, the clock, the random numbers, the packets the socket
// will receive, and runs up to 8 calls; after each the result, the logs (the callbacks', sendto's, the clock's,
// rand's) and the products are compared. HD2_GCD_ROUNDS: rounds (default 40,000; HD2_GCD_QUICK: 10,000);
// HD2_GCD_SEED.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
    for (int i = 0; i < size && i < 300; ++i) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }

// ---- the script ----
struct S_Script {
    uint32_t ticks;                  // GetTickCount (each call: + step)
    uint32_t step;
    unsigned randState;
    long long now;                   // time
    std::deque<std::string> packets; // the socket's (as received: XORed)
    bool selectFails;
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
extern "C" long FakeTime(long *out)
{
    Log("time " + N(out != nullptr));
    if (out)
        *out = static_cast<long>(g_script.now);
    return static_cast<long>(g_script.now);
}
extern "C" int WSAAPI FakeSelect(int count, fd_set *readable, fd_set *writable, fd_set *errors, const timeval *wait)
{
    Log("select " + N(count) + " " + N(readable ? static_cast<long long>(readable->fd_count) : -1) + "/" +
        N(readable && readable->fd_count ? static_cast<long long>(readable->fd_array[0]) : -1) + " " + N(writable != nullptr) + " " +
        N(errors != nullptr) + " " + (wait ? N(wait->tv_sec) + "." + N(wait->tv_usec) : "null"));
    if (g_script.selectFails)
        return -1;
    return g_script.packets.empty() ? 0 : 1;
}
extern "C" int WSAAPI FakeRecvfrom(SOCKET s, char *data, int size, int flags, sockaddr *from, int *fromSize)
{
    Log("recvfrom " + N(static_cast<long long>(s)) + " " + N(size) + " " + N(flags) + " " + N(*fromSize));
    if (g_script.packets.empty())
        return -1;
    const std::string p = g_script.packets.front();
    g_script.packets.pop_front();
    if (p == "!")
        return -1;   // (a failed read)
    const int n = std::min<int>(size, static_cast<int>(p.size()));
    std::memcpy(data, p.data(), static_cast<size_t>(n));
    auto *in = reinterpret_cast<sockaddr_in *>(from);
    std::memset(in, 0, sizeof *in);
    in->sin_family = AF_INET;
    in->sin_addr.s_addr = 0x0A0B0C0D;
    in->sin_port = htons(4242);
    return n;
}
std::string Xored(const char *data, int size)
{
    std::string s(data, static_cast<size_t>(size));
    static const char KEY[] = "gamespy";
    for (int i = 0; i < size; ++i)
        s[static_cast<size_t>(i)] = static_cast<char>(s[static_cast<size_t>(i)] ^ KEY[i % 7]);
    return s;
}
extern "C" int WSAAPI FakeSendto(SOCKET s, const char *data, int size, int flags, const sockaddr *to, int toSize)
{
    const auto *in = reinterpret_cast<const sockaddr_in *>(to);
    Log("sendto " + N(static_cast<long long>(s)) + " " + Str(Xored(data, size).c_str()) + " " + N(size) + " " + N(flags) + " " + N(in->sin_family) +
        "/" + Hex(in->sin_addr.s_addr) + ":" + N(ntohs(in->sin_port)) + " " + N(toSize));
    return size;
}

// Fresh blocks hold garbage, as the original's heap's: 0xCD, alike on both sides (a check refused at once keeps its
// unset fields).
void *TestMalloc(size_t n)
{
    void *p = std::malloc(n ? n : 1);
    std::memset(p, 0xCD, n ? n : 1);
    return p;
}

// ---- the port's file (its other SDK file, gamespy_util.cpp: linked) ----
namespace std {
using ::TestMalloc;
using ::FakeRand;
using ::FakeSrand;
using ::FakeTime;
}
#define GetTickCount FakeGetTickCount
#define malloc TestMalloc
#define rand FakeRand
#define srand FakeSrand
#define time(x) FakeTime(nullptr)   // (the file's one call passes null)
#define select FakeSelect
#define recvfrom FakeRecvfrom
#define sendto FakeSendto
#undef GCD_TEST_PORT   // (the mutation runner's parallel port: the original's 29910 here)
#include "net/gamespy_gcd.cpp"
#undef GetTickCount
#undef malloc
#undef rand
#undef srand
#undef time
#undef select
#undef recvfrom
#undef sendto
S_Qr2Record g_qr2DefaultRecord;

namespace {

// ---- the original's ----
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define FAST __attribute__((fastcall))
#define O_CALL(addr, type) reinterpret_cast<type>(static_cast<uintptr_t>(addr))
const uint32_t O_PRODUCTS = 0x0098c160, O_COUNT = 0x0098277c, O_SOCKET = 0x00845244, O_SERVER = 0x00982728, O_RECORD = 0x00845670;
void *OMalloc(size_t n) { return TestMalloc(n); }
void OFree(void *p) { std::free(p); }
DWORD OGetTickCount() { return FakeGetTickCount(); }
int ORand() { return FakeRand(); }
void OSrand(unsigned seed) { FakeSrand(seed); }
long OTime(long *t) { return FakeTime(t); }

// The checks' callbacks (each side's convention).
void PAuthenticated(int gameId, int localId, int authenticated, char *message, void *instance)
{
    Log("authenticated " + N(gameId) + " " + N(localId) + " " + N(authenticated) + " " + Str(message) + " " + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(instance))));
}
void FAST OAuthenticated(int gameId, int localId, int authenticated, char *message, void *instance)
{
    PAuthenticated(gameId, localId, authenticated, message, instance);
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
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 400) + "\n     port: " + y.substr(0, 400);
            break;
        }
    }
    Check(false, s);
}

// The products and their clients (each side's).
S_Product *Products(int side) { return side ? reinterpret_cast<S_Product *>(static_cast<uintptr_t>(O_PRODUCTS)) : g_products; }
int &Count(int side) { return side ? G<int>(O_COUNT) : g_productCount; }
std::string ClientText(const S_Client *c, int side)
{
    const void *cb = side ? reinterpret_cast<const void *>(&OAuthenticated) : reinterpret_cast<const void *>(&PAuthenticated);
    return N(c->localId) + "," + std::string(c->hkey, strnlen(c->hkey, sizeof c->hkey)) + "," + N(c->sessionKey) + "," + Hex(c->ip) + "," + N(c->sentTime) +
           "," + N(c->tries) + "," + N(c->state) + "," + Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c->instance))) + "," +
           N(reinterpret_cast<const void *>(c->authenticated) == cb) + "," + Str(c->error) + "," + Bytes(c->request, c->request ? c->requestSize : 0);
}
std::string ProductsText(int side)
{
    std::string s = N(Count(side)) + ":";
    for (int i = 0; i < Count(side) && i < MAX_PRODUCTS; ++i) {
        const S_Product &p = Products(side)[i];
        s += N(p.id) + "[";
        int guard = 0;
        for (const S_Node *n = p.clients.next; n && guard < 50; n = n->next, ++guard)
            s += ClientText(n->client, side) + (n->previous && n->previous->next == n ? "" : "!links") + " ";
        s += "]";
    }
    return s;
}
std::string ServerText(const sockaddr_in &a) { return N(a.sin_family) + "/" + Hex(a.sin_addr.s_addr) + ":" + N(ntohs(a.sin_port)) + "/" + Bytes(a.sin_zero, 8); }
void CompareState()
{
    std::string s;
    const std::string pp = ProductsText(0), op = ProductsText(1);
    if (pp != op) {
        // (Where they part: 150 characters each side of it.)
        size_t at = 0;
        while (at < pp.size() && at < op.size() && pp[at] == op[at])
            ++at;
        const size_t from = at > 150 ? at - 150 : 0;
        s += "the products at " + N(static_cast<long long>(at)) + ": " + op.substr(from, 300) + " / " + pp.substr(from, 300) + "; ";
    }
    if (g_socket != G<SOCKET>(O_SOCKET))
        s += "the socket; ";
    if (ServerText(g_validationServer) != ServerText(G<sockaddr_in>(O_SERVER)))
        s += "the validation server " + ServerText(G<sockaddr_in>(O_SERVER)) + " / " + ServerText(g_validationServer) + "; ";
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

// ---- the world ----
std::string RandomKey()
{
    static const char hex[] = "0123456789abcdef";
    std::string k;
    const int n = P(0.1) ? I(0, 40) : 32;
    for (int i = 0; i < n; ++i)
        k += hex[I(0, 15)];
    return k;
}
void FreeLists(int side)
{
    for (int i = 0; i < Count(side) && i < MAX_PRODUCTS; ++i) {
        S_Product &p = Products(side)[i];
        for (S_Node *n = p.clients.next; n;) {
            S_Node *next = n->next;
            std::free(n->client->error);
            std::free(n->client->request);
            std::free(n->client);
            std::free(n);
            n = next;
        }
        p.clients.next = nullptr;
    }
}
std::vector<std::string> g_keys;   // (the round's clients' keys: the packets name them)
std::vector<int> g_sessions;       // (and their session keys)
std::vector<uint32_t> g_sentTimes; // (and their requests' times)
void SetWorld()
{
    FreeLists(0);
    FreeLists(1);
    g_keys.clear();
    g_sessions.clear();
    g_sentTimes.clear();
    const int count = P(0.1) ? 0 : I(1, MAX_PRODUCTS);
    for (int side = 0; side < 2; ++side)
        Count(side) = count;
    for (int i = 0; i < count; ++i) {
        const int id = P(0.5) ? 0x2EE : I(1, 3000);
        std::vector<S_Client> clients;
        for (int k = I(0, 4); k > 0; --k) {
            S_Client c{};
            c.localId = I(0, 5);
            const std::string key = RandomKey();
            std::snprintf(c.hkey, sizeof c.hkey, "%.32s", key.c_str());
            g_keys.push_back(c.hkey);
            c.sessionKey = I(0, 0x3FFF);
            g_sessions.push_back(c.sessionKey);
            c.ip = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
            c.sentTime = static_cast<uint32_t>(I(0, 100000));
            g_sentTimes.push_back(c.sentTime);
            c.tries = I(0, 3);
            c.state = I(0, 3);
            c.instance = reinterpret_cast<void *>(static_cast<uintptr_t>(I(1, 0xFFFF)));
            c.error = nullptr;
            c.request = nullptr;
            c.requestSize = I(1, 30);
            clients.push_back(c);
        }
        const bool errors = P(0.5), requests = P(0.7);
        for (int side = 0; side < 2; ++side) {
            S_Product &p = Products(side)[i];
            p.id = id;
            p.clients = S_Node{nullptr, nullptr, nullptr};
            S_Node *last = &p.clients;
            for (const S_Client &c : clients) {
                auto *x = static_cast<S_Client *>(std::malloc(sizeof(S_Client)));
                *x = c;
                x->authenticated = side ? reinterpret_cast<GcdAuthCallback>(&OAuthenticated) : &PAuthenticated;
                if (errors && c.state == CLIENT_BAD) {
                    x->error = static_cast<char *>(std::malloc(8));
                    std::strcpy(x->error, "Bad key");
                }
                if (requests || c.state == CLIENT_WAITING) {   // (a waiting check has its request: sent again)
                    x->request = static_cast<char *>(std::malloc(static_cast<size_t>(c.requestSize)));
                    for (int b = 0; b < c.requestSize; ++b)
                        x->request[b] = static_cast<char>('a' + (b * 7 + c.localId) % 26);
                }
                auto *n = static_cast<S_Node *>(std::malloc(sizeof(S_Node)));
                n->client = x;
                n->next = nullptr;
                n->previous = last;
                last->next = n;
                last = n;
            }
        }
    }
    g_socket = static_cast<SOCKET>(I(1, 999));
    G<SOCKET>(O_SOCKET) = g_socket;
    g_validationServer = sockaddr_in{};
    g_validationServer.sin_family = AF_INET;
    g_validationServer.sin_port = htons(29910);
    g_validationServer.sin_addr.s_addr = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
    G<sockaddr_in>(O_SERVER) = g_validationServer;
    g_script.ticks = static_cast<uint32_t>(I(0, 120000));
    g_script.step = P(0.5) ? 0 : static_cast<uint32_t>(I(1, 3000));
    if (!g_sentTimes.empty() && P(0.3)) {   // (the retry time's edge: 2 s after a request, or just before)
        g_script.ticks = g_sentTimes[static_cast<size_t>(I(0, static_cast<int>(g_sentTimes.size()) - 1))] + 2000u - static_cast<uint32_t>(I(0, 1));
        g_script.step = 0;
    }
    g_script.randState = static_cast<unsigned>(I(0, 0x7FFFFFFF));
    g_script.now = I(0, 0x7FFFFFFF);
    g_script.selectFails = P(0.1);
    g_script.packets.clear();
}

// A packet of the validation server's, XORed as sent.
std::string MakePacket()
{
    // (A client's key and, mostly, its session key.)
    const int which = g_keys.empty() ? -1 : I(0, static_cast<int>(g_keys.size()) - 1);
    const std::string key = which >= 0 && P(0.8) ? g_keys[static_cast<size_t>(which)] : RandomKey();
    const int skey = which >= 0 && P(0.6) ? g_sessions[static_cast<size_t>(which)] : P(0.5) ? I(0, 0x3FFF) : -1;
    std::string text;
    switch (I(0, 8)) {
    case 0:
        text = "\\uok\\\\cd\\" + key + "\\skey\\" + N(skey);
        break;
    case 1:
        // (a message of up to 200 letters kept: the limit's edge sometimes)
        text = "\\unok\\\\cd\\" + key + "\\skey\\" + N(skey) +
               (P(0.7) ? "\\errmsg\\" + (P(0.3) ? std::string(static_cast<size_t>(I(198, 202)), 'e') : std::string("Invalid key")) : "");
        break;
    case 2:
        text = "\\ison\\\\skey\\" + N(skey) + "\\cd\\" + key;
        break;
    case 3:
        text = "\\ucount\\" + std::string(P(0.5) ? "\\pid\\" + N(P(0.5) ? 0x2EE : I(0, 3000)) : "");
        break;
    case 4:
        text = "\\nothing\\";   // (one without a second backslash crashes the original: the port's Fix)
        break;
    case 5:
        text = "x\\uok\\";
        break;
    case 6:
        text = "\\" + std::string(static_cast<size_t>(I(30, 40)), 'c') + "\\";
        break;
    case 7:
        text = "\\\\";
        break;
    default:
        text = "\\uok\\\\cd\\" + key + "\\skey\\" + N(skey) + "\\cd\\" + std::string(static_cast<size_t>(I(190, 230)), 'z');
        break;
    }
    return Xored(text.data(), static_cast<int>(text.size()));
}

void Call()
{
    const int op = I(0, 21);
    const int productId = Count(0) && P(0.8) ? Products(0)[I(0, Count(0) - 1)].id : (P(0.5) ? 0x2EE : I(0, 3000));
    switch (op) {
    case 0: {
        char data[2][64];
        const int size = I(0, 60);
        for (int i = 0; i < size; ++i)
            data[0][i] = data[1][i] = static_cast<char>(I(0, 255));
        Run("XorKey " + N(size), [&] { XorKey(data[0], size); return 0; }, [&] { O_CALL(0x00734d10, void(FAST *)(char *, int))(data[1], size); return 0; });
        Check(std::memcmp(data[0], data[1], static_cast<size_t>(size)) == 0, "the bytes");
        break;
    }
    case 1:
        Run("FindProduct " + N(productId), [&] {
                S_Product *p = FindProduct(productId);
                return p ? static_cast<long long>(p - g_products) : -1LL;
            },
            [&] {
                auto *p = O_CALL(0x00734e40, S_Product *(FAST *)(int))(productId);
                return p ? static_cast<long long>(p - Products(1)) : -1LL;
            });
        break;
    case 2: {
        const std::string key = !g_keys.empty() && P(0.8) ? g_keys[static_cast<size_t>(I(0, static_cast<int>(g_keys.size()) - 1))] : RandomKey();
        const int skey = P(0.5) ? -1 : I(0, 0x3FFF);
        Run("FindClient", [&] {
                S_Client *c = FindClient(key.c_str(), skey);
                return c ? static_cast<long long>(c->localId * 100000 + c->sessionKey) : -1LL;
            },
            [&] {
                auto *c = O_CALL(0x007346a0, S_Client *(FAST *)(const char *, int))(key.c_str(), skey);
                return c ? static_cast<long long>(c->localId * 100000 + c->sessionKey) : -1LL;
            });
        break;
    }
    case 3: {
        static const char *keys[] = {"cd", "skey", "pid", "errmsg", "x"};
        const char *key = keys[I(0, 4)];
        std::string packet = MakePacket();
        packet = Xored(packet.data(), static_cast<int>(packet.size()));
        std::string pt, ot;
        Run(std::string("ValueForKey ") + key, [&] { pt = ValueForKey(packet.c_str(), key); return 0; },
            [&] { ot = O_CALL(0x00734c00, const char *(FAST *)(const char *, const char *))(packet.c_str(), key); return 0; });
        Check(pt == ot, "the value " + ot + " / " + pt);
        break;
    }
    case 4:
    case 5: {
        // A packet of the validation server's handled (its own copy each side).
        std::string packet = MakePacket();
        std::vector<char> b[2] = {std::vector<char>(packet.begin(), packet.end()), std::vector<char>(packet.begin(), packet.end())};
        b[0].push_back(0);
        b[1].push_back(0);
        sockaddr_in from{};
        from.sin_family = AF_INET;
        from.sin_addr.s_addr = 0x01020304;
        from.sin_port = htons(5555);
        Run("ProcessPacket", [&] { ProcessPacket(b[0].data(), static_cast<int>(packet.size()), reinterpret_cast<sockaddr *>(&from)); return 0; },
            [&] { O_CALL(0x007342a0, void(FAST *)(char *, int, sockaddr *))(b[1].data(), static_cast<int>(packet.size()), reinterpret_cast<sockaddr *>(&from)); return 0; });
        break;
    }
    case 6: {
        if (Count(0) >= MAX_PRODUCTS && P(0.5))
            break;
        Run("AddProduct", [&] { return static_cast<long long>(AddProduct(productId)); }, [&] { return static_cast<long long>(O_CALL(0x00734530, int(FAST *)(int))(productId)); });
        break;
    }
    case 7: {
        // A response (a made-up key; a too long one sometimes).
        std::string key = RandomKey();
        if (P(0.05))
            key = std::string(260, 'k');
        std::string challenge = P(0.05) ? std::string(450, 'c') : "chal" + N(I(0, 999));
        if (P(0.1) && key.size() < 200)   // (the length limit's edge: 511 letters in all, or 512)
            challenge = std::string(static_cast<size_t>(511 - 8 - 2 * static_cast<int>(key.size()) + I(0, 1)), 'c');
        char response[2][0x80];
        std::memset(response, 'q', sizeof response);
        Run("gcd_compute_response", [&] { gcd_compute_response(key.c_str(), challenge.c_str(), response[0]); return 0; },
            [&] { O_CALL(0x00733af0, void(FAST *)(const char *, const char *, char *))(key.c_str(), challenge.c_str(), response[1]); return 0; });
        Check(std::memcmp(response[0], response[1], sizeof response[0]) == 0, "the response " + Str(response[1]) + " / " + Str(response[0]));
        break;
    }
    case 8: {
        // gcd_init (the default record or a given one; a full table of products sometimes).
        static S_Qr2Record record[2];
        const bool given = P(0.3);
        for (int side = 0; side < 2; ++side) {
            auto &r = side ? *reinterpret_cast<S_Qr2Record *>(static_cast<uintptr_t>(O_RECORD)) : g_qr2DefaultRecord;
            for (S_Qr2Record *x : {&r, &record[side]}) {
                x->socket = static_cast<SOCKET>(77 + (x == &r ? 0 : 1));
                x->masterAddress.sin_addr.s_addr = x == &r ? 0x11223344u : 0x55667788u;
                x->cdKeyProcess = nullptr;
            }
        }
        Run("gcd_init " + N(given), [&] { return static_cast<long long>(gcd_init(given ? &record[0] : nullptr, productId)); },
            [&] { return static_cast<long long>(O_CALL(0x00733cb0, int(FAST *)(void *, int))(given ? &record[1] : nullptr, productId)); });
        const void *pp = reinterpret_cast<const void *>((given ? record[0] : g_qr2DefaultRecord).cdKeyProcess);
        const void *op = reinterpret_cast<const void *>((given ? record[1] : *reinterpret_cast<S_Qr2Record *>(static_cast<uintptr_t>(O_RECORD))).cdKeyProcess);
        Check((pp == reinterpret_cast<const void *>(&ProcessPacket)) == (op == reinterpret_cast<const void *>(static_cast<uintptr_t>(0x007342a0))), "the record's callback");
        break;
    }
    case 9:
    case 10:
    case 11: {
        // A user to check (a 72-letter response or a short one; a key in use sometimes).
        std::string response = RandomKey() + std::string(40, 'r');
        if (!g_keys.empty() && P(0.3))
            response = g_keys[static_cast<size_t>(I(0, static_cast<int>(g_keys.size()) - 1))] + std::string(40, 'r');
        if (P(0.2))
            response = response.substr(0, static_cast<size_t>(I(0, 0x47)));
        const int localId = I(0, 5);
        const uint32_t address = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
        const std::string challenge = "ch" + N(I(0, 99));
        void *instance = reinterpret_cast<void *>(static_cast<uintptr_t>(I(1, 0xFFFF)));
        Run("gcd_authenticate_user", [&] { gcd_authenticate_user(productId, localId, address, challenge.c_str(), response.c_str(), &PAuthenticated, instance); return 0; },
            [&] {
                O_CALL(0x00733d80, void(FAST *)(int, int, uint32_t, const char *, const char *, void *, void *))(productId, localId, address, challenge.c_str(),
                                                                                                                     response.c_str(), reinterpret_cast<void *>(&OAuthenticated), instance);
                return 0;
            });
        break;
    }
    case 12: {
        const int localId = I(0, 5);
        Run("gcd_disconnect_user " + N(localId), [&] { gcd_disconnect_user(productId, localId); return 0; },
            [&] { O_CALL(0x00733f40, void(FAST *)(int, int))(productId, localId); return 0; });
        break;
    }
    case 13:
    case 14:
    case 15: {
        // The think (the socket's packets read, the checks answered).
        for (int k = I(0, 4); k > 0; --k)
            g_script.packets.push_back(P(0.1) ? "!" : MakePacket());
        Run("gcd_think", [&] { gcd_think(); return 0; }, [&] { O_CALL(0x00734040, void(FAST *)())(); return 0; });
        g_script.packets.clear();
        break;
    }
    case 16: {
        // The installer's key: made right (the check of its digits) or not.
        char key[24];
        for (int i = 0; i < 20; ++i)
            key[i] = "0123456789ABCDEF"[I(0, 15)];
        key[3] = key[4] = key[9] = key[10] = key[14] = '-';
        key[20] = 0;
        if (P(0.5)) {
            char hex[16] = "0x";
            std::memcpy(hex + 2, key, 3);
            std::memcpy(hex + 5, key + 5, 4);
            std::memcpy(hex + 9, key + 11, 3);
            hex[12] = 0;
            long long number = 0;
            std::sscanf(hex, "%llx", &number);
            char check[8];
            std::snprintf(check, sizeof check, "%04X", static_cast<unsigned>(static_cast<uint16_t>(number % 0xFFFF) ^ 0x1DEA));
            std::memcpy(key + 15, check, 4);
        }
        if (P(0.2))   // (a letter that's no digit: the number unread)
            key[I(0, 19)] = "Gz -"[I(0, 3)];
        Run("ValidateCDKey", [&] { return static_cast<long long>(ValidateCDKey(key)); },
            [&] { return static_cast<long long>(O_CALL(0x007350a0, uint8_t(FAST *)(const char *))(key)); });
        break;
    }
    case 17: {
        // The list helpers on the first product's list (a node added, or one removed).
        if (!Count(0))
            break;
        if (P(0.5)) {
            S_Node *n[2];
            for (int side = 0; side < 2; ++side) {
                n[side] = static_cast<S_Node *>(std::malloc(sizeof(S_Node)));
                auto *c = static_cast<S_Client *>(std::calloc(1, sizeof(S_Client)));
                c->localId = 9;
                c->authenticated = side ? reinterpret_cast<GcdAuthCallback>(&OAuthenticated) : &PAuthenticated;
                n[side]->client = c;
            }
            Run("ListAdd", [&] { ListAdd(n[0], &Products(0)[0].clients); return 0; },
                [&] { O_CALL(0x00734e70, void(FAST *)(S_Node *, S_Node *))(n[1], &Products(1)[0].clients); return 0; });
        } else {
            S_Node *n[2] = {Products(0)[0].clients.next, Products(1)[0].clients.next};
            if (!n[0])
                break;
            const int skip = I(0, 3);
            for (int k = 0; k < skip && n[0]->next; ++k) {
                n[0] = n[0]->next;
                n[1] = n[1]->next;
            }
            Run("ListRemove", [&] { ListRemove(n[0]); return 0; }, [&] { O_CALL(0x00734e90, void(FAST *)(S_Node *))(n[1]); return 0; });
            for (S_Node *x : n) {
                std::free(x->client->error);
                std::free(x->client->request);
                std::free(x->client);
                std::free(x);
            }
        }
        break;
    }
    default: {
        // A request sent again / a disconnect / an auth request (the first product's first client).
        if (!Count(0) || !Products(0)[0].clients.next)
            break;
        S_Client *c[2] = {Products(0)[0].clients.next->client, Products(1)[0].clients.next->client};
        const int which = I(0, 2);
        if (which == 0) {
            if (!c[0]->request)
                break;
            Run("ResendRequest", [&] { ResendRequest(c[0]); return 0; }, [&] { O_CALL(0x00734bc0, void(FAST *)(S_Client *))(c[1]); return 0; });
        } else if (which == 1) {
            Run("SendDisconnect", [&] { SendDisconnect(&Products(0)[0], c[0]); return 0; },
                [&] { O_CALL(0x00734970, void(FAST *)(const S_Product *, const S_Client *))(&Products(1)[0], c[1]); return 0; });
        } else {
            std::free(c[0]->request);
            std::free(c[1]->request);
            c[0]->request = c[1]->request = nullptr;
            const std::string challenge = "ch" + N(I(0, 99)), response = RandomKey() + std::string(40, 'r');
            Run("SendAuthRequest", [&] { SendAuthRequest(&Products(0)[0], c[0], challenge.c_str(), response.c_str()); return 0; },
                [&] { O_CALL(0x00734a40, void(FAST *)(const S_Product *, S_Client *, const char *, const char *))(&Products(1)[0], c[1], challenge.c_str(), response.c_str()); return 0; });
        }
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
    if (std::getenv("HD2_GCD_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("gcd_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // (The key the packets are xored with: the original's start fills it; its statics never ran here.)
    std::strcpy(reinterpret_cast<char *>(static_cast<uintptr_t>(0x00982518)), "gamespy");
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(OMalloc));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(OFree));
    RedirectOriginal(0x00737c90, reinterpret_cast<const void *>(OGetTickCount));
    RedirectOriginal(0x007e6393, reinterpret_cast<const void *>(ORand));
    RedirectOriginal(0x007e6386, reinterpret_cast<const void *>(OSrand));
    RedirectOriginal(0x007e767a, reinterpret_cast<const void *>(OTime));
    for (const char *dll : {"ws2_32.dll", "wsock32.dll"}) {
        HMODULE m = LoadLibraryA(dll);
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "select")), reinterpret_cast<const void *>(&FakeSelect));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "recvfrom")), reinterpret_cast<const void *>(&FakeRecvfrom));
        RedirectImport(reinterpret_cast<const void *>(GetProcAddress(m, "sendto")), reinterpret_cast<const void *>(&FakeSendto));
    }

    int rounds = std::getenv("HD2_GCD_QUICK") ? 10000 : 40000;
    if (const char *r = std::getenv("HD2_GCD_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_GCD_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        g_trace.clear();
        SetWorld();
        for (int k = I(1, 8); k > 0; --k) {
            try {
                Call();
            } catch (const std::exception &e) {
                Fail(std::string("an exception: ") + e.what() + " (side " + N(g_side) + ")");
                for (size_t i = g_log[g_side].size() > 4 ? g_log[g_side].size() - 4 : 0; i < g_log[g_side].size(); ++i)
                    std::printf("    last: %s\n", g_log[g_side][i].substr(0, 200).c_str());
                g_faking = false;
            }
        }
    }
    std::printf("gcd_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
