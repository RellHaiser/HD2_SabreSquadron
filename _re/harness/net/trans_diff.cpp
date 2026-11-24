// Net part 4b: the transport's connections and their manager (net/net_transport.cpp) against the original
// executable's. MinGW; build_trans.bat; runs trans_diff.exe ("trans_diff: N checks, 0 failures").
//
// The system calls are part 4a's fakes (sock_fakes.h, sock_fakes.inc): the sockets send nothing, their sendto calls
// are logged, and what they sent comes back through recvfrom (as sent, its session or a bit changed, anything): each
// transport talks to itself, a datagram going to the connection of the id in its header. Each round makes a transport
// pair (the original's by its constructor in a buffer of its layout) and runs a random sequence of calls: Start / Stop,
// connections made, added, removed and deleted, found by id and address, Lock / Unlock, the connections' Send, Resend,
// Receive, Flush, Gather, pings (due or not, echoed, their round trip), SetResendTime, ResendAll, AcknowledgeAll, the
// transport's Tick, FlushAll, ReceiveAll, its receive thread (run here, told to stop after some selects), the
// socket's queued datagrams sent. After each call the transports, every connection (its fields, its three channels'
// numbers and list lengths), the sockets' queues, the send stream, the pools' free counts and the call logs (with the
// game's packet callback's calls) are compared.
// HD2_TRANS_ROUNDS: rounds (default 40,000, about 12 s; HD2_TRANS_QUICK: 10,000); HD2_TRANS_SEED; HD2_TRANS_FIRST_CHANCE.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"   // (the test's operator new is calloc, its delete free)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "sock_fakes.h"

#define protected public
#define private public
#include "net/bitstream.h"
#include "net/net_channel.h"
#include "net/net_socket.h"
#include "net/net_transport.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

int64_t g_now = 1000000;
int64_t GetPerfTimeMs() { return g_now; }

namespace {

template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T OFC(const void *o, int at) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(o) + at); }

#define THISCALL __attribute__((thiscall))
using FV = void(THISCALL *)(void *);
using FVI = void(THISCALL *)(void *, int);
using FVP = void(THISCALL *)(void *, void *);
using FPtr = void *(THISCALL *)(void *);
using FPtrI = void *(THISCALL *)(void *, int);

const uint32_t O_T_CTOR = 0x00746b40, O_T_DTOR = 0x00746bb0, O_T_START = 0x00746c10, O_T_STOP = 0x00746ca0,
               O_T_DELETE = 0x00746cf0, O_T_FIND_ID = 0x00746d60, O_T_FIND_ADDRESS = 0x00746dc0, O_T_LOCK = 0x00746e40,
               O_T_UNLOCK = 0x00746e60, O_T_RECEIVE_ALL = 0x00746e70, O_T_ADD = 0x00746fb0, O_T_REMOVE = 0x00747180,
               O_T_TICK = 0x00747200, O_T_FLUSH_ALL = 0x00747260, O_T_THREAD = 0x00747310;
const uint32_t O_C_CTOR = 0x00746180, O_C_SEND = 0x00746300, O_C_RECEIVE = 0x00746380, O_C_RESEND = 0x007463e0,
               O_C_POP = 0x00746410, O_C_PING = 0x00746460, O_C_PING_DUE = 0x00746550, O_C_FLUSH = 0x00746580,
               O_C_HANDLE = 0x007465d0, O_C_READ = 0x007467f0, O_C_GATHER = 0x007468f0, O_C_RESEND_ALL = 0x00746a50,
               O_C_ACK_ALL = 0x00746a90, O_C_RESEND_TIME = 0x00746ac0;
const uint32_t O_SEND_STREAM = 0x009830e8, O_GROWING_CTOR = 0x0073d650, O_SOCK_SEND_QUEUED = 0x007485e0;
const uint32_t O_POOL = 0x00983150, O_POOL_CTOR = 0x00749830, O_FREE_BUFFER = 0x00749b20;
const uint32_t O_CLOCK = 0x00748dc0, O_NEW = 0x007e566f, O_DELETE = 0x007e2120, O_MALLOC = 0x007e8627, O_FREE = 0x007e8761;

// ---- checks ----
int checks, failures;
int g_round;
const char *g_op = "";
std::string g_trace;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, %s: %s\n   (ops:%s)\n", g_round, g_op, what.c_str(), g_trace.c_str());
}
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok)
        Fail(what);
}
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%x", v);
    return b;
}
std::string N(long long v) { return std::to_string(v); }

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
uint32_t U32() { return static_cast<uint32_t>(rng()); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
int64_t OClock() { return g_now; }

}   // namespace

// The port's allocations zeroed too (part 3: a new packet's sequence number is never set).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

// (The fakes; it leaves the anonymous namespace open.)
#include "sock_fakes.inc"

// The game's packet callback (both sides': it logs as the side calling).
void __fastcall FakeCallback(int connection, void *data, int size, int kind, uint32_t flags)
{
    g_log[g_side].push_back("callback " + N(connection) + " " + Bytes(data, size) + " " + N(size) + " " + N(kind) + " " + Hex(flags));
}

// ---- the pair ----
struct S_Conn {
    C_NetConnection *p;
    uint8_t *o;
};
struct S_World {
    alignas(16) uint8_t o[0x100];
    alignas(16) uint8_t pStore[sizeof(C_NetTransport)];
    C_NetTransport *p = nullptr;
    std::vector<S_Conn> conns;          // every connection made (added or not)
    C_NetPacket *lastP = nullptr;       // the last packet sent (Resend's)
    uint8_t *lastO = nullptr;
} g;

uint8_t *OSocket() { return g.o + 0x10; }

template <class FP, class FO> void Both(FP port, FO orig)
{
    g_log[0].clear();
    g_log[1].clear();
    g_handles[0] = g_handles[1] = g_events[0] = g_events[1] = 0;
    // (The clock as the port's call left it must be where the original's leaves it: recvfrom moves it on.)
    const int64_t start = g_now;
    g_sockFaking = true;
    g_side = 0;
    port();
    const int64_t end = g_now;
    g_now = start;
    g_side = 1;
    orig();
    g_side = 0;
    g_sockFaking = false;
    Check(g_now == end, "the clock moved alike");
}

void CompareLogs(bool unordered = false)
{
    if (unordered) {
        // (Start's socket Open: the port makes its events before the thread, the original after; part 4a.)
        std::sort(g_log[0].begin(), g_log[0].end());
        std::sort(g_log[1].begin(), g_log[1].end());
    }
    if (g_log[0] == g_log[1]) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(g_log[0].size(), g_log[1].size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            size_t at = 0;
            while (at < x.size() && at < y.size() && x[at] == y[at])
                ++at;
            const size_t from = at > 60 ? at - 60 : 0;
            s += " (call " + N(i) + ", at character " + N(at) + ")\n     orig: " + x.substr(from, 160) + "\n     port: " + y.substr(from, 160);
            break;
        }
    }
    Check(false, s);
}

S_Conn *PairOfPort(const C_NetConnection *p)
{
    for (S_Conn &c : g.conns)
        if (c.p == p)
            return &c;
    return nullptr;
}

// An MSVC6 list's length (its size word).
int OListSize(const uint8_t *o, int at) { return OFC<int>(o, at + 8); }

void CompareChannels(const S_Conn &c, const std::string &what)
{
    for (int k = 0; k < 3; ++k) {
        const C_NetChannel *p = c.p->m_channels[k];
        const uint8_t *o = OFC<const uint8_t *>(c.o, 0x14 + 4 * k);
        std::string s;
        if (p->m_sent != OFC<uint32_t>(o, 4) || p->m_received != OFC<uint32_t>(o, 8))
            s += "sent / received " + Hex(OFC<uint32_t>(o, 4)) + " " + Hex(OFC<uint32_t>(o, 8)) + " / " + Hex(p->m_sent) + " " +
                 Hex(p->m_received) + "; ";
        if (static_cast<int>(p->m_outgoing.size()) != OListSize(o, 0xC) || static_cast<int>(p->m_incoming.size()) != OListSize(o, 0x18))
            s += "outgoing / incoming " + N(OListSize(o, 0xC)) + " " + N(OListSize(o, 0x18)) + " / " + N(p->m_outgoing.size()) + " " +
                 N(p->m_incoming.size()) + "; ";
        if (k >= 1) {
            const auto *r = static_cast<const C_NetReliableChannel *>(p);
            if (r->m_delivered != OFC<uint32_t>(o, 0x24) || r->m_acked != OFC<uint32_t>(o, 0x28) || r->m_resendTime != OFC<int>(o, 0x2C))
                s += "delivered / acked / resend " + Hex(OFC<uint32_t>(o, 0x24)) + " " + Hex(OFC<uint32_t>(o, 0x28)) + " " +
                     N(OFC<int>(o, 0x2C)) + " / " + Hex(r->m_delivered) + " " + Hex(r->m_acked) + " " + N(r->m_resendTime) + "; ";
            if (static_cast<int>(r->m_pending.size()) != OListSize(o, 0x30) || static_cast<int>(r->m_unordered.size()) != OListSize(o, 0x3C))
                s += "pending / unordered " + N(OListSize(o, 0x30)) + " " + N(OListSize(o, 0x3C)) + " / " + N(r->m_pending.size()) + " " +
                     N(r->m_unordered.size()) + "; ";
            if (k == 2 && static_cast<const C_NetOrderedChannel *>(p)->m_next != OFC<uint32_t>(o, 0x48))
                s += "next; ";
        }
        Check(s.empty(), what + " channel " + N(k) + ": " + s);
    }
}

void CompareConnection(const S_Conn &c, const std::string &what)
{
    const C_NetConnection *p = c.p;
    const uint8_t *o = c.o;
    std::string s;
    if (p->m_id != OFC<int>(o, 4) || p->m_session != OFC<uint32_t>(o, 8) || p->m_address != OFC<uint32_t>(o, 0xC) ||
        p->m_port != OFC<int>(o, 0x10))
        s += "id / session / address / port " + N(OFC<int>(o, 4)) + " " + N(OFC<uint32_t>(o, 8)) + " " + Hex(OFC<uint32_t>(o, 0xC)) + " " +
             N(OFC<int>(o, 0x10)) + " / " + N(p->m_id) + " " + N(p->m_session) + " " + Hex(p->m_address) + " " + N(p->m_port) + "; ";
    if (p->m_lastSend != OFC<int>(o, 0x3C) || p->m_lastReceive != OFC<int>(o, 0x40) || p->m_lastPing != OFC<int>(o, 0x44))
        s += "times " + N(OFC<int>(o, 0x3C)) + " " + N(OFC<int>(o, 0x40)) + " " + N(OFC<int>(o, 0x44)) + " / " + N(p->m_lastSend) + " " +
             N(p->m_lastReceive) + " " + N(p->m_lastPing) + "; ";
    if (p->m_pingId != OFC<uint32_t>(o, 0x48) || p->m_roundTrip != OFC<uint32_t>(o, 0x4C))
        s += "ping " + N(OFC<uint32_t>(o, 0x48)) + " " + N(OFC<uint32_t>(o, 0x4C)) + " / " + N(p->m_pingId) + " " + N(p->m_roundTrip) + "; ";
    if ((p->m_socket == &g.p->m_socket) != (OFC<uint8_t *>(o, 0x38) == OSocket()))
        s += "socket; ";
    if (p->m_lock.RecursionCount != 0 || p->m_lock.LockCount != -1 || p->m_lock.OwningThread ||
        OFC<CRITICAL_SECTION>(o, 0x20).RecursionCount != 0 || OFC<CRITICAL_SECTION>(o, 0x20).LockCount != -1 ||
        OFC<CRITICAL_SECTION>(o, 0x20).OwningThread)
        s += "lock held; ";
    Check(s.empty(), what + ": " + s);
    CompareChannels(c, what);
}

// The socket's queues (its datagrams compared by their bytes).
void CompareSocketQueue(const std::list<S_NetDatagram> &p, int at, const char *what)
{
    const uint8_t *head = OFC<const uint8_t *>(OSocket(), at + 4);
    std::vector<const uint8_t *> o;
    for (const uint8_t *n = OFC<const uint8_t *>(head, 0); n != head && o.size() < 100000; n = OFC<const uint8_t *>(n, 0))
        o.push_back(n + 8);
    if (o.size() != p.size()) {
        Check(false, std::string(what) + " length " + N(o.size()) + " / " + N(p.size()));
        return;
    }
    size_t i = 0;
    for (const S_NetDatagram &d : p) {
        const int size = OFC<int>(o[i], 0xC);
        const void *data = OFC<void *>(o[i], 8);
        Check(d.size == size && d.address == OFC<uint32_t>(o[i], 0x18) && d.port == OFC<int>(o[i], 0x1C) &&
                  (size <= 0 || std::memcmp(d.data, data, static_cast<size_t>(size)) == 0),
              std::string(what) + " " + N(i));
        ++i;
    }
}

void CompareAll(bool unordered = false)
{
    C_NetTransport *p = g.p;
    uint8_t *o = g.o;
    std::string s;
    // The connections added, in order (MSVC6 vector: first +4, last +8).
    const auto *first = OFC<uint8_t *const *>(o, 4), *last = OFC<uint8_t *const *>(o, 8);
    const size_t count = first ? static_cast<size_t>(last - first) : 0;
    if (count != p->m_connections.size()) {
        s += "connections " + N(count) + " / " + N(p->m_connections.size()) + "; ";
    } else {
        for (size_t i = 0; i < count; ++i) {
            S_Conn *c = PairOfPort(p->m_connections[i]);
            if (!c || c->o != first[i])
                s += "connection " + N(i) + " not its partner; ";
        }
    }
    if (p->m_running != (OF<uint8_t>(o, 0xA4) != 0) || p->m_threadId != OF<uint32_t>(o, 0xA8) || p->m_stop != (OF<uint8_t>(o, 0xAC) != 0) ||
        reinterpret_cast<uintptr_t>(p->m_stopped) != OF<uint32_t>(o, 0xB0))
        s += "running / thread / stop / stopped " + N(OF<uint8_t>(o, 0xA4)) + " " + Hex(OF<uint32_t>(o, 0xA8)) + " " + N(OF<uint8_t>(o, 0xAC)) +
             " " + Hex(OF<uint32_t>(o, 0xB0)) + " / " + N(p->m_running) + " " + Hex(p->m_threadId) + " " + N(p->m_stop) + " " +
             Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p->m_stopped))) + "; ";
    if (p->m_lastFlush != OF<int>(o, 0xCC) || p->m_lastReceive != OF<int>(o, 0xD0) || p->m_session != OF<uint32_t>(o, 0xD4))
        s += "flush / receive / session " + N(OF<int>(o, 0xCC)) + " " + N(OF<int>(o, 0xD0)) + " " + N(OF<uint32_t>(o, 0xD4)) + " / " +
             N(p->m_lastFlush) + " " + N(p->m_lastReceive) + " " + N(p->m_session) + "; ";
    if (p->m_lock.RecursionCount != 0 || p->m_lock.LockCount != -1 || p->m_lock.OwningThread ||
        OF<CRITICAL_SECTION>(o, 0xB4).RecursionCount != 0 || OF<CRITICAL_SECTION>(o, 0xB4).LockCount != -1 ||
        OF<CRITICAL_SECTION>(o, 0xB4).OwningThread)
        s += "lock held; ";
    // The socket's numbers.
    const C_NetSocket &ps = p->m_socket;
    uint8_t *os = OSocket();
    if (ps.m_socket != OF<uint32_t>(os, 0) || ps.m_message != OF<uint32_t>(os, 0x24) || ps.m_received != OF<int>(os, 0x1C) ||
        ps.m_sent != OF<int>(os, 0x20))
        s += "socket " + Hex(OF<uint32_t>(os, 0)) + " " + N(OF<uint32_t>(os, 0x24)) + " " + N(OF<int>(os, 0x1C)) + " " + N(OF<int>(os, 0x20)) +
             " / " + Hex(static_cast<uint32_t>(ps.m_socket)) + " " + N(ps.m_message) + " " + N(ps.m_received) + " " + N(ps.m_sent) + "; ";
    // The send stream (up to where it was written).
    auto *ostream = reinterpret_cast<uint8_t *>(O_SEND_STREAM);
    const uint32_t opos = OF<uint32_t>(ostream, 8) * 32 + OF<uint32_t>(ostream, 0xC);
    if (g_netSendStream.BitPosition() != opos)
        s += "the send stream at " + N(opos) + " / " + N(g_netSendStream.BitPosition()) + "; ";
    else if (opos && (!g_netSendStream.Data() || !OF<void *>(ostream, 4) ||
                      std::memcmp(g_netSendStream.Data(), OF<void *>(ostream, 4), (opos + 7) / 8) != 0))
        s += "the send stream's bytes; ";
    if (g_netPool.m_buffers.count != OF<int>(reinterpret_cast<void *>(O_POOL), 0x10) ||
        g_netPool.m_packets.count != OF<int>(reinterpret_cast<void *>(O_POOL), 0x28))
        s += "the pools' free buffers / packets " + N(OF<int>(reinterpret_cast<void *>(O_POOL), 0x10)) + " " +
             N(OF<int>(reinterpret_cast<void *>(O_POOL), 0x28)) + " / " + N(g_netPool.m_buffers.count) + " " + N(g_netPool.m_packets.count) + "; ";
    Check(s.empty(), s);
    CompareSocketQueue(ps.m_delayed, 0x3C, "the socket's delayed");
    CompareSocketQueue(ps.m_queue, 0x48, "the socket's queue");
    for (size_t i = 0; i < g.conns.size(); ++i)
        CompareConnection(g.conns[i], "connection " + N(i) + " (id " + N(g.conns[i].p->m_id) + ")");
    CompareLogs(unordered);
}

// ---- the wire: what the sockets sent (both alike: compared in the logs) ----
struct S_Sent {
    std::vector<uint8_t> bytes;
    uint32_t address;
    uint16_t port;
};
std::vector<S_Sent> g_sent;
void CollectSent()
{
    for (const std::string &line : g_log[0]) {
        if (line.compare(0, 7, "sendto ") != 0)
            continue;
        const size_t a = line.find(' ', 7), b = line.find(' ', a + 1), c = line.find(' ', b + 1);
        const std::string hex = line.substr(a + 1, b - a - 1), addr = line.substr(c + 1);
        S_Sent sent;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            sent.bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        std::vector<uint8_t> to;
        for (size_t i = 0; i + 1 < addr.size(); i += 2)
            to.push_back(static_cast<uint8_t>(std::stoul(addr.substr(i, 2), nullptr, 16)));
        sent.port = 0;
        sent.address = 0;
        if (to.size() >= 8) {
            sent.port = static_cast<uint16_t>(to[2] << 8 | to[3]);
            std::memcpy(&sent.address, &to[4], 4);
        }
        g_sent.push_back(sent);
        if (g_sent.size() > 200)
            g_sent.erase(g_sent.begin());
    }
}

void Script(const S_Script &s) { g_script[0] = g_script[1] = s; }

S_Conn *PickConn(bool added)
{
    std::vector<S_Conn *> v;
    for (S_Conn &c : g.conns) {
        bool in = false;
        for (C_NetConnection *x : g.p->m_connections)
            in |= x == c.p;
        if (!added || in)
            v.push_back(&c);
    }
    if (v.empty())
        return nullptr;
    return v[static_cast<size_t>(I(0, static_cast<int>(v.size()) - 1))];
}

// ---- the ops ----
void OpStartStop()
{
    if (P(0.6)) {
        g_op = "Start";
        const int port = P(0.3) ? -1 : I(1024, 65535);
        g_trace += " T" + N(port);
        Both([&] { g.p->Start(port); }, [&] { OriginalAt<FVI>(O_T_START)(g.o, port); });
        // (Start forgets the connections: they are still the test's, and deleted at the end.)
        CompareAll(true);
        return;
    } else {
        g_op = "Stop";
        g_trace += " P";
        Both([&] { g.p->Stop(); }, [&] { OriginalAt<FV>(O_T_STOP)(g.o); });
    }
    CompareAll();
}

void OpNewConnection()
{
    g_op = "a connection made and added";
    if (g.conns.size() >= 8)
        return;
    const int id = P(0.7) ? I(0, 7) : I(0, 255);
    const uint32_t address = P(0.5) ? 0x0100007F : U32();
    if (P(0.2)) {
        // The transport's session (C_NetGame's to set).
        const uint32_t session = static_cast<uint32_t>(I(0, 1));
        g.p->m_session = session;
        OF<uint32_t>(g.o, 0xD4) = session;
    }
    S_Conn c{};
    g_trace += " C" + N(id);
    Both([&] { c.p = new C_NetConnection(id, address, &g.p->m_socket); },
         [&] {
             c.o = static_cast<uint8_t *>(ONew(0x50));
             reinterpret_cast<void *(THISCALL *)(void *, int, uint32_t, void *)>(O_C_CTOR)(c.o, id, address, OSocket());
         });
    g.conns.push_back(c);
    if (P(0.6)) {
        // Its port known (else the first datagram gives it).
        const int port = P(0.5) ? 7777 : I(0, 65535);
        c.p->m_port = OF<int>(c.o, 0x10) = port;
    }
    if (P(0.9))
        Both([&] { g.p->Add(c.p); }, [&] { OriginalAt<FVP>(O_T_ADD)(g.o, c.o); });
    CompareAll();
}

void OpRemove()
{
    S_Conn *c = PickConn(false);
    if (!c)
        return;
    if (P(0.15)) {
        g_op = "DeleteConnections";
        g_trace += " DA";
        // Those in the transport's list are deleted; the others stay the test's.
        std::vector<S_Conn> outside;
        for (S_Conn &x : g.conns) {
            bool in = false;
            for (C_NetConnection *q : g.p->m_connections)
                in |= q == x.p;
            if (!in)
                outside.push_back(x);
        }
        Both([&] { g.p->DeleteConnections(); }, [&] { OriginalAt<FV>(O_T_DELETE)(g.o); });
        g.conns = outside;
        g.lastP = nullptr;
        g.lastO = nullptr;
        CompareAll();
        return;
    }
    g_op = "Remove";
    g_trace += " RM";
    Both([&] { g.p->Remove(c->p); }, [&] { OriginalAt<FVP>(O_T_REMOVE)(g.o, c->o); });
    CompareAll();
}

void OpFind()
{
    g_op = "FindById / FindByAddress";
    g_trace += " FD";
    S_Conn *c = PickConn(false);
    const int id = c && P(0.7) ? c->p->m_id : I(0, 255);
    void *pr = nullptr, *orr = nullptr;
    Both([&] { pr = g.p->FindById(id); }, [&] { orr = OriginalAt<FPtrI>(O_T_FIND_ID)(g.o, id); });
    S_Conn *pc = PairOfPort(static_cast<C_NetConnection *>(pr));
    Check((pr == nullptr && orr == nullptr) || (pc && pc->o == orr), "FindById " + N(id));
    const uint32_t address = c && P(0.7) ? c->p->m_address : U32();
    const int port = c && P(0.7) ? c->p->m_port : I(-1, 65535);
    Both([&] { pr = g.p->FindByAddress(address, port); },
         [&] { orr = reinterpret_cast<void *(THISCALL *)(void *, uint32_t, int)>(O_T_FIND_ADDRESS)(g.o, address, port); });
    pc = PairOfPort(static_cast<C_NetConnection *>(pr));
    Check((pr == nullptr && orr == nullptr) || (pc && pc->o == orr), "FindByAddress");
    Both([&] { Check(g.p->Lock() == g.p, "Lock"); g.p->Unlock(); },
         [&] { Check(OriginalAt<FPtr>(O_T_LOCK)(g.o) == g.o, "Lock"); OriginalAt<FV>(O_T_UNLOCK)(g.o); });
    CompareAll();
}

void OpSend()
{
    S_Conn *c = PickConn(false);
    if (!c)
        return;
    g_op = "Send";
    const int size = P(0.8) ? I(0, 120) : I(400, 1500);
    std::vector<uint8_t> data(static_cast<size_t>(size + 1));
    for (uint8_t &b : data)
        b = static_cast<uint8_t>(U32());
    const int kind = I(0, 63), channel = I(0, 2);
    g_trace += " S" + N(channel);
    C_NetPacket *pp = nullptr;
    uint8_t *op = nullptr;
    Both([&] { pp = c->p->Send(data.data(), size, kind, channel); },
         [&] { op = static_cast<uint8_t *>(reinterpret_cast<void *(THISCALL *)(void *, const void *, int, int, int)>(O_C_SEND)(c->o, data.data(), size, kind, channel)); });
    g.lastP = pp;
    g.lastO = op;
    CompareAll();
}

void OpResend()
{
    // The last packet sent, again (while a channel holds it: still to go out).
    if (!g.lastP)
        return;
    S_Conn *owner = nullptr;
    for (S_Conn &c : g.conns)
        for (C_NetChannel *ch : c.p->m_channels)
            for (C_NetPacket *x : ch->m_outgoing)
                if (x == g.lastP)
                    owner = &c;
    if (!owner)
        return;
    g_op = "Resend";
    g_trace += " RS";
    Both([&] { owner->p->Resend(g.lastP); }, [&] { OriginalAt<FVP>(O_C_RESEND)(owner->o, g.lastO); });
    CompareAll();
}

void OpConnectionCalls()
{
    S_Conn *c = PickConn(false);
    if (!c)
        return;
    switch (I(0, 6)) {
    case 0: {
        g_op = "Receive";
        g_trace += " RV";
        void *pd = reinterpret_cast<void *>(1), *od = reinterpret_cast<void *>(1);   // (none: null written)
        int ps = -1, os = -1, pk = -1, ok = -1;
        uint32_t pf = 9, of = 9;
        bool a = false, b = false;
        Both([&] { a = c->p->Receive(&pd, &ps, &pk, &pf); },
             [&] { b = reinterpret_cast<bool(THISCALL *)(void *, void **, int *, int *, uint32_t *)>(O_C_RECEIVE)(c->o, &od, &os, &ok, &of); });
        Check(a == b && ps == os && (a || (pd == nullptr && od == nullptr)) && (!a || (pk == ok && pf == of && (ps <= 0 || std::memcmp(pd, od, static_cast<size_t>(ps)) == 0))),
              "Receive " + N(b) + " " + N(os) + " / " + N(a) + " " + N(ps));
        // The data is the caller's to give back.
        if (a && b) {
            g_netPool.FreeBuffer(pd, ps);
            reinterpret_cast<void(THISCALL *)(void *, void *, int)>(O_FREE_BUFFER)(reinterpret_cast<void *>(O_POOL), od, os);
        }
        break;
    }
    case 1:
        g_op = "SendPing";
        g_trace += " PG";
        Both([&] { c->p->SendPing(); }, [&] { OriginalAt<FV>(O_C_PING)(c->o); });
        break;
    case 2:
        g_op = "PingIfDue";
        g_now += P(0.5) ? I(0, 100) : I(1990, 2010);
        g_trace += " PD";
        Both([&] { c->p->PingIfDue(); }, [&] { OriginalAt<FV>(O_C_PING_DUE)(c->o); });
        break;
    case 3: {
        g_op = "Flush";
        const bool force = P(0.5);
        g_now += I(0, 300);
        g_trace += force ? " FL1" : " FL0";
        Both([&] { c->p->Flush(force); }, [&] { OriginalAt<FVI>(O_C_FLUSH)(c->o, force); });
        break;
    }
    case 4: {
        g_op = "Gather";
        const bool force = P(0.5);
        g_trace += " GA";
        void *pd = nullptr, *od = nullptr;
        int ps = -1, os = -1;
        Both([&] { c->p->Gather(&pd, &ps, force); },
             [&] { reinterpret_cast<void(THISCALL *)(void *, void **, int *, bool)>(O_C_GATHER)(c->o, &od, &os, force); });
        Check(ps == os && (pd == nullptr) == (od == nullptr), "Gather " + N(os) + " / " + N(ps));
        break;
    }
    case 5: {
        g_op = "SetResendTime";
        const int ms = P(0.5) ? I(10, 700) : (P(0.5) ? I(-5, 12) : I(695, 2000));
        g_trace += " RT" + N(ms);
        Both([&] { c->p->SetResendTime(ms); }, [&] { OriginalAt<FVI>(O_C_RESEND_TIME)(c->o, ms); });
        break;
    }
    default:
        g_op = "ResendAll / AcknowledgeAll";
        g_now += I(0, 6000);
        g_trace += " RA";
        Both([&] { c->p->ResendAll(); c->p->AcknowledgeAll(); }, [&] { OriginalAt<FV>(O_C_RESEND_ALL)(c->o); OriginalAt<FV>(O_C_ACK_ALL)(c->o); });
        break;
    }
    CompareAll();
}

void OpTransportCalls()
{
    switch (I(0, 2)) {
    case 0:
        g_op = "Tick";
        g_now += P(0.5) ? I(0, 600) : I(1900, 2500);
        g_trace += " TK";
        Both([&] { g.p->Tick(); }, [&] { OriginalAt<FV>(O_T_TICK)(g.o); });
        break;
    case 1: {
        g_op = "FlushAll";
        const bool force = P(0.5);
        g_now += I(0, 300);
        g_trace += " FA";
        Both([&] { g.p->FlushAll(force); }, [&] { OriginalAt<FVI>(O_T_FLUSH_ALL)(g.o, force); });
        break;
    }
    default:
        // The socket's queued datagrams sent (its thread's work): onto the wire.
        g_op = "the socket's SendQueued";
        g_trace += " SQ";
        Both([&] { g.p->m_socket.SendQueued(); }, [&] { OriginalAt<FV>(O_SOCK_SEND_QUEUED)(OSocket()); });
        CollectSent();
        break;
    }
    CompareAll();
}

// A datagram from the wire, as sent or changed, or anything.
S_Wire RandomWire()
{
    S_Wire w;
    w.result = 1;
    w.address = P(0.5) ? 0x0100007F : U32();
    w.port = static_cast<uint16_t>(P(0.5) ? 7777 : I(0, 65535));
    if (!g_sent.empty() && P(0.85)) {
        const S_Sent &s = g_sent[static_cast<size_t>(I(0, static_cast<int>(g_sent.size()) - 1))];
        w.bytes = s.bytes;
        if (P(0.5)) {
            w.address = s.address;
            w.port = s.port;
        }
        // A bit changed in a checksummed word (Unseal refuses it). (Not in the last bytes past the whole words: the
        // checksum does not cover them, and a packet header there can take any size: see the notes.)
        const int words = (static_cast<int>(w.bytes.size()) - 3) / 4;
        if (P(0.1) && words > 0)
            w.bytes[static_cast<size_t>(I(3, 3 + words * 4 - 1))] ^= static_cast<uint8_t>(1 << I(0, 7));
        return w;
    }
    // Anything, sealed (a fragment or not); at least 8 bytes (part 4a: a shorter one can fault both programs).
    const int n = I(8, 200);
    w.bytes.resize(static_cast<size_t>(n));
    for (uint8_t &b : w.bytes)
        b = static_cast<uint8_t>(U32());
    w.bytes[3] &= 0x7F;
    // Not a packet list (type 1): random packet headers fault both programs (see the notes); a ping or anything else.
    if (w.bytes[3] == 1)
        w.bytes[3] = P(0.5) ? 2 : 0;
    if (P(0.7))
        g.p->m_socket.Seal(w.bytes.data(), n - 3);
    return w;
}

void OpReceiveAll()
{
    g_op = "ReceiveAll";
    S_Script s;
    for (int k = I(0, 4); k > 0; --k) {
        s.selects.push_back(P(0.95) ? 1 : -1);
        s.wire.push_back(RandomWire());
    }
    s.selects.push_back(0);
    Script(s);
    g_now += I(0, 50);
    g_recvClockStep = I(0, 2);   // (time passing between the datagrams: the fragments flushed between them)
    g_trace += " RA" + N(s.wire.size());
    Both([&] { g.p->ReceiveAll(); }, [&] { OriginalAt<FV>(O_T_RECEIVE_ALL)(g.o); });
    CompareAll();
}

void OpThread()
{
    // The receive thread: told to stop after some selects (its entry as CreateThread was given it, or the loop).
    g_op = "ReceiveThread";
    S_Script s;
    for (int k = I(0, 3); k > 0; --k) {
        s.selects.push_back(1);
        s.wire.push_back(RandomWire());
        s.selects.push_back(0);
    }
    s.selectsBeforeStop = I(0, 3);
    Script(s);
    g.p->m_stop = false;
    OF<uint8_t>(g.o, 0xAC) = 0;
    g_selectStopFlag[0] = reinterpret_cast<uint8_t *>(&g.p->m_stop);
    g_selectStopFlag[1] = g.o + 0xAC;
    g_now += I(0, 700);
    if (g_threadStart[0] && g_threadStart[1] && P(0.5)) {
        g_trace += " TE";
        if (std::getenv("HD2_TRANS_SHOW"))
            std::printf("TE: port %p(%p) transport %p; orig %p(%p) transport %p\n", reinterpret_cast<void *>(g_threadStart[0]),
                        g_threadParam[0], static_cast<void *>(g.p), reinterpret_cast<void *>(g_threadStart[1]), g_threadParam[1],
                        static_cast<void *>(g.o));
        DWORD a = 7, b = 7;
        Both([&] { a = g_threadStart[0](g_threadParam[0]); }, [&] { b = g_threadStart[1](g_threadParam[1]); });
        Check(a == b, "the thread's exit code");
    } else {
        g_trace += " TL";
        Both([&] { g.p->ReceiveThread(); }, [&] { OriginalAt<FV>(O_T_THREAD)(g.o); });
    }
    g_selectStopFlag[0] = g_selectStopFlag[1] = nullptr;
    CompareAll();
}

void OpHandleDirect()
{
    // HandleDatagram and ReadPackets on a datagram's body as ReceiveAll passes it (a ping, an echo, packets).
    S_Conn *c = PickConn(false);
    if (!c || g_sent.empty())
        return;
    const S_Sent &sent = g_sent[static_cast<size_t>(I(0, static_cast<int>(g_sent.size()) - 1))];
    if (sent.bytes.size() < 8 || (sent.bytes[3] & 0x80))
        return;
    // Unsealed (a copy each).
    std::vector<uint8_t> a = sent.bytes, b;
    if (!g.p->m_socket.Unseal(a.data(), static_cast<int>(a.size()) - 3))
        return;
    a.erase(a.begin(), a.begin() + 3);
    a.resize(a.size() + 8, 0);
    b = a;
    // (Its own type: packets parsed from another body can claim sizes past it, or of 32 KB and more, read sign-extended:
    // both programs fault; see the notes.)
    const uint32_t type = a[0];
    const int size = static_cast<int>(a.size()) - 8;
    NetPacketCallback cb = P(0.8) ? FakeCallback : nullptr;
    g_op = "HandleDatagram";
    g_trace += " HD" + N(type);
    Both([&] { c->p->HandleDatagram(type, a.data(), size, cb); },
         [&] { reinterpret_cast<void(THISCALL *)(void *, uint32_t, void *, int, NetPacketCallback)>(O_C_HANDLE)(c->o, type, b.data(), size, cb); });
    CompareAll();
}

void Round()
{
    g_trace.clear();
    std::memset(g.o, 0xEE, sizeof g.o);
    g.p = new (g.pStore) C_NetTransport();
    OriginalAt<FPtr>(O_T_CTOR)(g.o);
    // (The original's constructor leaves the thread's fields, the times and the session as the memory was, its socket's
    // likewise (part 4a): zeroed here; see the notes.)
    OF<uint32_t>(g.o, 0xA8) = 0;
    OF<uint8_t>(g.o, 0xAC) = 0;
    OF<uint32_t>(g.o, 0xB0) = OF<int>(g.o, 0xCC) = OF<int>(g.o, 0xD0) = OF<uint32_t>(g.o, 0xD4) = 0;
    OF<int>(OSocket(), 0x1C) = OF<int>(OSocket(), 0x20) = 0;
    OF<uint32_t>(OSocket(), 0x6C) = OF<uint32_t>(OSocket(), 0x74) = OF<uint32_t>(OSocket(), 0x78) = 0;
    OF<uint8_t>(OSocket(), 0x70) = 0;
    std::memset(OSocket() + 4, 0, 16);
    std::memset(OSocket() + 0x80, 0, 20);
    std::memset(g.p->m_socket.m_buffer, 0, 0x8000);
    std::memset(OF<uint8_t *>(OSocket(), 0x14), 0, 0x8000);
    std::memset(g.p->m_socket.m_fragmentBuffer, 0, 800);
    std::memset(OF<uint8_t *>(OSocket(), 0x18), 0, 800);
    // The game's callback, or none.
    NetPacketCallback cb = P(0.85) ? FakeCallback : nullptr;
    g.p->m_callback = cb;
    OF<NetPacketCallback>(g.o, 0xD8) = cb;
    g.conns.clear();
    g.lastP = nullptr;
    g.lastO = nullptr;
    g_sent.clear();
    g_log[0].clear();
    g_log[1].clear();
    g_op = "construct";
    CompareAll();
    if (P(0.8)) {
        g_op = "Start";
        const int port = I(1024, 65535);
        g_trace += " T" + N(port);
        Both([&] { g.p->Start(port); }, [&] { OriginalAt<FVI>(O_T_START)(g.o, port); });
        CompareAll(true);
    }
    const int ops = I(1, 60);
    for (int k = 0; k < ops; ++k) {
        const int op = I(0, 99);
        if (op < 3)
            OpStartStop();
        else if (op < 13)
            OpNewConnection();
        else if (op < 17)
            OpRemove();
        else if (op < 21)
            OpFind();
        else if (op < 38)
            OpSend();
        else if (op < 40)
            OpResend();
        else if (op < 58)
            OpConnectionCalls();
        else if (op < 74)
            OpTransportCalls();
        else if (op < 90)
            OpReceiveAll();
        else if (op < 94)
            OpThread();
        else
            OpHandleDirect();
    }
    // The end: the connections deleted (the transport's by DeleteConnections, the others by the test), the
    // transport destroyed.
    g_op = "destroy";
    g_trace += " ~";
    std::vector<S_Conn> outside;
    for (S_Conn &c : g.conns) {
        bool in = false;
        for (C_NetConnection *x : g.p->m_connections)
            in |= x == c.p;
        if (!in)
            outside.push_back(c);
    }
    Both([&] { g.p->DeleteConnections(); }, [&] { OriginalAt<FV>(O_T_DELETE)(g.o); });
    for (S_Conn &c : outside)
        Both([&] { delete c.p; }, [&] { OriginalAt<FVI>(OFC<uint32_t>(reinterpret_cast<void *>(OFC<uint32_t>(c.o, 0)), 0))(c.o, 1); });
    g.conns.clear();
    Both([&] { g.p->~C_NetTransport(); }, [&] { OriginalAt<FV>(O_T_DTOR)(g.o); });
    CompareLogs();
    // (Its stopped event and socket are fakes: nothing to close.)
    g_threadStart[0] = g_threadStart[1] = nullptr;
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side (ops:%s)\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, g_round, g_op, g_side ? "the original" : "the port", g_trace.c_str());
    // The original's return addresses on the stack (its code: 0x401000..0x80d000).
    const auto *sp = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(e->ContextRecord->Esp));
    std::printf("  the original's code on the stack:");
    for (int i = 0, n = 0; i < 256 && n < 12; ++i)
        if (sp[i] >= 0x401000 && sp[i] < 0x80d000) {
            std::printf(" %08x", sp[i]);
            ++n;
        }
    std::printf("\n");
    std::fflush(stdout);
    ExitProcess(3);
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        OnCrash(e);
    return EXCEPTION_CONTINUE_SEARCH;
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_TRANS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("trans_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    RedirectOriginal(O_NEW, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(O_DELETE, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(O_MALLOC, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(O_FREE, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));   // (realloc: the send stream grows)
    RedirectOriginal(O_CLOCK, reinterpret_cast<const void *>(OClock));
    if (!RedirectSystemCalls())
        return 1;
    // The original's statics never ran: its pool and its send stream.
    OriginalAt<FPtr>(O_POOL_CTOR)(reinterpret_cast<void *>(O_POOL));
    OriginalAt<FPtr>(O_GROWING_CTOR)(reinterpret_cast<void *>(O_SEND_STREAM));
    int rounds = std::getenv("HD2_TRANS_QUICK") ? 10000 : 40000;
    if (const char *r = std::getenv("HD2_TRANS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_TRANS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("trans_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
