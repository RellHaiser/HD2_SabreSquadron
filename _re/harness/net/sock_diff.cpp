// Net part 4a: the transport's UDP socket (net/net_socket.cpp) against the original executable's. MinGW;
// build_sock.bat; runs sock_diff.exe ("sock_diff: N checks, 0 failures").
//
// No real socket or thread: Winsock's calls and the thread calls go to the test's fakes on both sides (the port's
// renamed by sock_fakes.h, the original's through its import table), which log every call (compared) and answer from
// a script both sides read alike: select's answer, the datagrams recvfrom gives (the ones the sockets sent, as sent,
// with a byte changed, cut short, or anything), the host's name and address. Each round makes a socket pair (the
// original's by its constructor in a buffer of its layout: its vector and lists MSVC6's) and runs a random sequence of
// calls: Seal / Unseal, Send (whole or in fragments), FlushDelayed (the clock moved on), SendQueued, SendThread (its
// waits answered until it is told to stop), QueueSend, SendRaw, Receive (fragments in any order, repeated, expired),
// TakeCounters, GetLocalAddress, Open / Close. After each call the sockets (their buffers, fragments, queues, counters),
// the pools' free counts, g_netLastFragmentTime and the call logs are compared. Each program keeps its own pool (part
// 3); the queued buffers are compared by their bytes.
// HD2_SOCK_ROUNDS: rounds (default 20,000; HD2_SOCK_QUICK: 5,000); HD2_SOCK_SEED; HD2_SOCK_FIRST_CHANCE.
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

#define THISCALL __attribute__((thiscall))
using FV = void(THISCALL *)(void *);
using FVI = void(THISCALL *)(void *, int);
using FPtr = void *(THISCALL *)(void *);
using FSeal = void(THISCALL *)(void *, uint8_t *, int);
using FUnseal = bool(THISCALL *)(void *, uint8_t *, int);
using FSend = void(THISCALL *)(void *, const void *, int, uint32_t, int);
using FReceive = void(THISCALL *)(void *, uint8_t **, int *, uint32_t *, uint32_t *);
using FCounters = void(THISCALL *)(void *, int *, int *);
using FAddress = uint32_t(THISCALL *)(void *);
using FFind = bool(THISCALL *)(void *, int *, int *);
using FReassemble = bool(THISCALL *)(void *, uint8_t **, int *);

const uint32_t O_CTOR = 0x00747940, O_DTOR = 0x00747a10, O_OPEN = 0x00747af0, O_CLOSE = 0x00747bb0, O_SEND = 0x00747cc0,
               O_RECEIVE = 0x00747ea0, O_FLUSH = 0x007484d0, O_COUNTERS = 0x007486e0, O_LOCAL = 0x00748790,
               O_SEAL = 0x00747c00, O_UNSEAL = 0x00747c60, O_FIND = 0x00748110, O_REASSEMBLE = 0x00748230,
               O_SEND_RAW = 0x00747e60, O_QUEUE = 0x00748550, O_SEND_QUEUED = 0x007485e0, O_SEND_THREAD = 0x00748680,
               O_LOOKUP = 0x00748700;
const uint32_t O_LAST_FRAGMENT = 0x00983100, O_POOL = 0x00983150, O_POOL_CTOR = 0x00749830;
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

// ---- the original's allocations and clock ----
void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
int64_t OClock() { return g_now; }

}   // namespace

// (The fakes; it leaves the anonymous namespace open.)
#include "sock_fakes.inc"


// ---- the sockets ----
struct S_Pair {
    alignas(16) uint8_t o[0x100];
    alignas(16) uint8_t pStore[sizeof(C_NetSocket)];
    C_NetSocket *p = nullptr;
} g;

// Every call on both: the port's, then the original's, each logging as its side.
template <class FP, class FO> void Both(FP port, FO orig)
{
    g_log[0].clear();
    g_log[1].clear();
    g_handles[0] = g_handles[1] = g_events[0] = g_events[1] = 0;
    g_sockFaking = true;
    g_side = 0;
    port();
    g_side = 1;
    orig();
    g_side = 0;
    g_sockFaking = false;
}

void CompareLogs(bool unordered = false)
{
    std::vector<std::string> a = g_log[0], b = g_log[1];
    if (unordered) {
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
    }
    if (a == b) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < b.size() ? b[i] : "-", y = i < a.size() ? a[i] : "-";
        if (x != y) {
            size_t at = 0;
            while (at < x.size() && at < y.size() && x[at] == y[at])
                ++at;
            const size_t from = at > 60 ? at - 60 : 0;
            s += "\n     (at character " + N(at) + ")\n     orig: " + x.substr(from, 160) + "\n     port: " + y.substr(from, 160);
            break;
        }
    }
    Check(false, s);
}

// An MSVC6 std::list's values (+8 in each node) of the original's socket at `at`.
std::vector<const uint8_t *> OList(int at)
{
    std::vector<const uint8_t *> v;
    const uint8_t *head = OF<const uint8_t *>(g.o, at + 4);
    for (const uint8_t *n = *reinterpret_cast<const uint8_t *const *>(head); n != head && v.size() < 10000;
         n = *reinterpret_cast<const uint8_t *const *>(n))
        v.push_back(n + 8);
    Check(static_cast<int>(v.size()) == OF<int>(g.o, at + 8), "an MSVC list's size");
    return v;
}

void CompareDatagrams(const std::list<S_NetDatagram> &p, int at, const char *what)
{
    std::vector<const uint8_t *> o = OList(at);
    if (o.size() != p.size()) {
        Check(false, std::string(what) + " length " + N(o.size()) + " / " + N(p.size()));
        return;
    }
    size_t i = 0;
    for (const S_NetDatagram &d : p) {
        const uint8_t *od = o[i];
        const int size = *reinterpret_cast<const int *>(od + 0xC);
        const void *data = *reinterpret_cast<void *const *>(od + 8);
        Check(d.size == size && d.address == *reinterpret_cast<const uint32_t *>(od + 0x18) &&
                  d.port == *reinterpret_cast<const int *>(od + 0x1C) && (d.data == nullptr) == (data == nullptr) &&
                  (!d.data || size <= 0 || std::memcmp(d.data, data, static_cast<size_t>(size)) == 0),
              std::string(what) + " " + N(i) + ": size " + N(size) + " / " + N(d.size));
        ++i;
    }
}

void CompareSockets()
{
    C_NetSocket *p = g.p;
    uint8_t *o = g.o;
    std::string s;
    if (p->m_socket != OF<uint32_t>(o, 0))
        s += "socket " + Hex(OF<uint32_t>(o, 0)) + " / " + Hex(static_cast<uint32_t>(p->m_socket)) + "; ";
    if (std::memcmp(p->m_address, o + 4, 16) != 0)
        s += "address " + Bytes(o + 4, 16) + " / " + Bytes(p->m_address, 16) + "; ";
    if (p->m_received != OF<int>(o, 0x1C) || p->m_sent != OF<int>(o, 0x20))
        s += "counters " + N(OF<int>(o, 0x1C)) + " " + N(OF<int>(o, 0x20)) + " / " + N(p->m_received) + " " + N(p->m_sent) + "; ";
    if (p->m_message != OF<uint32_t>(o, 0x24) || p->m_key != OF<uint32_t>(o, 0x28))
        s += "message / key " + Hex(OF<uint32_t>(o, 0x24)) + " " + Hex(OF<uint32_t>(o, 0x28)) + " / " + Hex(p->m_message) + " " +
             Hex(p->m_key) + "; ";
    if (std::memcmp(p->m_buffer, OF<uint8_t *>(o, 0x14), 0x8000) != 0) {
        int i = 0;
        while (p->m_buffer[i] == OF<uint8_t *>(o, 0x14)[i])
            ++i;
        s += "buffer byte " + N(i) + "; ";
    }
    if (std::memcmp(p->m_fragmentBuffer, OF<uint8_t *>(o, 0x18), 800) != 0)
        s += "fragment buffer; ";
    if (p->m_threadId != OF<uint32_t>(o, 0x6C) || p->m_stop != (OF<uint8_t>(o, 0x70) != 0) ||
        reinterpret_cast<uintptr_t>(p->m_stopped) != OF<uint32_t>(o, 0x74) || reinterpret_cast<uintptr_t>(p->m_wake) != OF<uint32_t>(o, 0x78))
        s += "thread " + Hex(OF<uint32_t>(o, 0x6C)) + " " + N(OF<uint8_t>(o, 0x70)) + " " + Hex(OF<uint32_t>(o, 0x74)) + " " +
             Hex(OF<uint32_t>(o, 0x78)) + " / " + Hex(p->m_threadId) + " " + N(p->m_stop) + " " +
             Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p->m_stopped))) + " " +
             Hex(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p->m_wake))) + "; ";
    if (p->m_localAddress != OF<uint32_t>(o, 0x7C) || std::memcmp(p->m_localName, o + 0x80, 20) != 0)
        s += "local " + Hex(OF<uint32_t>(o, 0x7C)) + " / " + Hex(p->m_localAddress) + "; ";
    // The fragments (MSVC6 vector: first +0x30, last +0x34; 0x20 each).
    const uint8_t *first = OF<uint8_t *>(o, 0x30), *last = OF<uint8_t *>(o, 0x34);
    const size_t count = first ? static_cast<size_t>(last - first) / 0x20 : 0;
    if (count != p->m_fragments.size()) {
        s += "fragments " + N(count) + " / " + N(p->m_fragments.size()) + "; ";
    } else {
        for (size_t i = 0; i < count; ++i) {
            const uint8_t *f = first + i * 0x20;
            const S_NetFragment &pf = p->m_fragments[i];
            const int size = *reinterpret_cast<const int *>(f + 0xC);
            const void *data = *reinterpret_cast<void *const *>(f + 8);
            if (pf.message != *reinterpret_cast<const uint32_t *>(f) || pf.index != *reinterpret_cast<const uint32_t *>(f + 4) ||
                pf.size != size || pf.last != (f[0x10] != 0) || pf.time != *reinterpret_cast<const int *>(f + 0x14) ||
                (pf.data == nullptr) != (data == nullptr) || (size > 0 && pf.data && std::memcmp(pf.data, data, static_cast<size_t>(size)) != 0))
                s += "fragment " + N(i) + " " + Hex(*reinterpret_cast<const uint32_t *>(f)) + ":" +
                     N(*reinterpret_cast<const uint32_t *>(f + 4)) + " / " + Hex(pf.message) + ":" + N(pf.index) + "; ";
        }
    }
    Check(s.empty(), s);
    CompareDatagrams(p->m_delayed, 0x3C, "delayed");
    CompareDatagrams(p->m_queue, 0x48, "queued");
    Check(g_netLastFragmentTime == OF<int>(reinterpret_cast<void *>(O_LAST_FRAGMENT), 0), "g_netLastFragmentTime");
    Check(g_netPool.m_buffers.count == OF<int>(reinterpret_cast<void *>(O_POOL), 0x10),
          "the pools' free buffers " + N(OF<int>(reinterpret_cast<void *>(O_POOL), 0x10)) + " / " + N(g_netPool.m_buffers.count));
    // The locks let go.
    Check(p->m_lock.RecursionCount == 0 && OF<CRITICAL_SECTION>(o, 0x54).RecursionCount == 0, "the socket's lock held");
    CompareLogs();
}

// ---- the datagrams the sockets sent (from the logs: their bytes), for Receive ----
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
        // "sendto S HEX FLAGS ADDRHEX"
        const size_t a = line.find(' ', 7), b = line.find(' ', a + 1), c = line.find(' ', b + 1);
        const std::string hex = line.substr(a + 1, b - a - 1), addr = line.substr(c + 1);
        S_Sent sent;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            sent.bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        std::vector<uint8_t> to;
        for (size_t i = 0; i + 1 < addr.size(); i += 2)
            to.push_back(static_cast<uint8_t>(std::stoul(addr.substr(i, 2), nullptr, 16)));
        if (to.size() >= 8) {
            sent.port = static_cast<uint16_t>(to[2] << 8 | to[3]);
            std::memcpy(&sent.address, &to[4], 4);
        }
        g_sent.push_back(sent);
        if (g_sent.size() > 400)
            g_sent.erase(g_sent.begin());
    }
}

void Script(const S_Script &s)
{
    g_script[0] = g_script[1] = s;
}

// ---- the ops ----
std::vector<uint8_t> RandomBytes(int n)
{
    std::vector<uint8_t> v(static_cast<size_t>(n));
    for (uint8_t &b : v)
        b = static_cast<uint8_t>(U32());
    return v;
}

void OpSeal()
{
    g_op = "Seal/Unseal";
    const int size = P(0.1) ? I(0, 7) : I(0, 600);
    std::vector<uint8_t> a = RandomBytes(size + 3 + 4), b;
    if (P(0.3)) {
        const uint32_t key = P(0.5) ? U32() : 0;
        g.p->m_key = key;
        OF<uint32_t>(g.o, 0x28) = key;
    }
    g_trace += " SL" + N(size);
    b = a;
    Both([&] { g.p->Seal(a.data(), size); }, [&] { OriginalAt<FSeal>(O_SEAL)(g.o, b.data(), size); });
    Check(a == b, "sealed bytes");
    // Unsealed as sealed, or changed, or with another size.
    if (P(0.3) && size > 0)
        a[static_cast<size_t>(I(0, size + 2))] ^= static_cast<uint8_t>(1 << I(0, 7));
    const int with = P(0.2) ? std::max(0, size + I(-4, 4)) : size;
    if (with > size + 4)
        return;
    b = a;
    bool x = false, y = false;
    Both([&] { x = g.p->Unseal(a.data(), with); }, [&] { y = OriginalAt<FUnseal>(O_UNSEAL)(g.o, b.data(), with); });
    Check(x == y && a == b, "Unseal " + N(y) + " / " + N(x));
    CompareSockets();
}

void OpSend()
{
    g_op = "Send";
    int size = P(0.6) ? I(0, 499) : I(500, 3000);
    if (P(0.1))
        size = P(0.5) ? 500 : 1000;
    if (P(0.05))
        size = 501;
    std::vector<uint8_t> data = RandomBytes(size + 1);
    const uint32_t address = P(0.5) ? 0x0100007F : U32();
    const int port = P(0.5) ? 7777 : I(0, 65535);
    g_trace += " S" + N(size);
    Both([&] { g.p->Send(data.data(), size, address, port); },
         [&] { OriginalAt<FSend>(O_SEND)(g.o, data.data(), size, address, port); });
    CompareSockets();
}

void OpFlush()
{
    g_op = "FlushDelayed";
    g_now += P(0.3) ? 0 : I(1, 3);
    if (P(0.1)) {
        g_netLastFragmentTime = OF<int>(reinterpret_cast<void *>(O_LAST_FRAGMENT), 0) = static_cast<int>(g_now) + I(-1, 1);
    }
    g_trace += " F";
    Both([&] { g.p->FlushDelayed(); }, [&] { OriginalAt<FV>(O_FLUSH)(g.o); });
    CompareSockets();
}

void OpSendQueued()
{
    if (P(0.7)) {
        g_op = "SendQueued";
        g_trace += " SQ";
        Both([&] { g.p->SendQueued(); }, [&] { OriginalAt<FV>(O_SEND_QUEUED)(g.o); });
    } else {
        // The thread's loop: its waits answered, the stop flag set after some.
        g_op = "SendThread";
        g_trace += " ST";
        S_Script s;
        s.waits = I(0, 3);
        Script(s);
        g.p->m_stop = false;
        OF<uint8_t>(g.o, 0x70) = 0;
        g_stopFlag[0] = reinterpret_cast<uint8_t *>(&g.p->m_stop);
        g_stopFlag[1] = g.o + 0x70;
        Both([&] { g.p->SendThread(); }, [&] { OriginalAt<FV>(O_SEND_THREAD)(g.o); });
        g_stopFlag[0] = g_stopFlag[1] = nullptr;
    }
    CollectSent();
    CompareSockets();
}

void OpQueueRaw()
{
    // QueueSend and SendRaw alone (a pool buffer queued; bytes sent at once).
    const int size = I(0, 900);
    std::vector<uint8_t> data = RandomBytes(size + 1);
    const uint32_t address = U32();
    const int port = I(0, 65535);
    if (P(0.5)) {
        g_op = "QueueSend";
        g_trace += " Q";
        void *pd = g_netPool.AllocBuffer(static_cast<size_t>(size));
        void *od = reinterpret_cast<void *(THISCALL *)(void *, size_t)>(0x00749a70)(reinterpret_cast<void *>(O_POOL), static_cast<size_t>(size));
        std::memcpy(pd, data.data(), static_cast<size_t>(size));
        std::memcpy(od, data.data(), static_cast<size_t>(size));
        Both([&] { g.p->QueueSend(pd, size, address, port); },
             [&] { OriginalAt<void(THISCALL *)(void *, void *, int, uint32_t, int)>(O_QUEUE)(g.o, od, size, address, port); });
    } else {
        g_op = "SendRaw";
        g_trace += " R";
        Both([&] { g.p->SendRaw(data.data(), size, address, port); },
             [&] { OriginalAt<void(THISCALL *)(void *, const void *, int, uint32_t, int)>(O_SEND_RAW)(g.o, data.data(), size, address, port); });
    }
    CollectSent();
    CompareSockets();
}

S_Wire g_lastWire;   // (the crash report's)

void OpReceive()
{
    g_op = "Receive";
    S_Script s;
    const int how = I(0, 9);
    S_Wire w;
    w.address = U32();
    w.port = static_cast<uint16_t>(I(0, 65535));
    w.result = 1;
    if (how == 0) {
        s.selects.push_back(0);   // nothing waiting
    } else if (how == 1) {
        s.selects.push_back(P(0.5) ? -1 : 1);   // an error (recvfrom tried anyway) or readable
        w.result = P(0.5) ? 0 : -1;
        s.wire.push_back(w);
    } else {
        s.selects.push_back(P(0.9) ? 1 : -1);
        if (!g_sent.empty() && how >= 4) {
            const S_Sent &sent = g_sent[static_cast<size_t>(I(0, static_cast<int>(g_sent.size()) - 1))];
            w.bytes = sent.bytes;
            w.address = sent.address;
            w.port = sent.port;
            if (P(0.1) && !w.bytes.empty())
                w.bytes[static_cast<size_t>(I(0, static_cast<int>(w.bytes.size()) - 1))] ^= static_cast<uint8_t>(1 << I(0, 7));
            if (P(0.05) && w.bytes.size() > 9)
                w.bytes.resize(static_cast<size_t>(I(9, static_cast<int>(w.bytes.size()))));
        } else {
            // Anything (at least 8 bytes: a shorter sealed fragment would copy a negative size, both programs'
            // fault; see the notes), or a short one that is no fragment.
            w.bytes = RandomBytes(P(0.2) ? I(1, 7) : I(8, 600));
            if (w.bytes.size() < 8 && w.bytes.size() > 3)
                w.bytes[3] &= 0x7F;
            if (P(0.5) && w.bytes.size() >= 8) {
                // Sealed properly (a fragment or not).
                const int size = static_cast<int>(w.bytes.size()) - 3;
                if (P(0.5))
                    w.bytes[3] |= 0x80;
                g.p->Seal(w.bytes.data(), size);
            }
        }
        s.wire.push_back(w);
    }
    // A datagram under 8 bytes whose fourth byte (past its end under 4: the buffer's old byte) has the high bit: under
    // 7 bytes no word is checksummed, so it passes as a fragment and its data's size (the bytes after the first 8) is
    // negative: both programs copy that and fault (the original's flaw: see the notes). None given.
    for (S_Wire &x : s.wire)
        if (!x.bytes.empty() && x.bytes.size() < 8)
            x.bytes.resize(8, 0);
    Script(s);
    g_lastWire = s.wire.empty() ? S_Wire{} : s.wire.front();
    if (P(0.15))
        g_now += P(0.5) ? I(0, 30000) : 20000 + I(-1, 1);
    g_trace += " V" + N(how);
    uint8_t *pd = reinterpret_cast<uint8_t *>(1), *od = reinterpret_cast<uint8_t *>(1);
    int ps = -7, os = -7;
    uint32_t pa = 9, oa = 9, pp = 9, op = 9;
    Both([&] { g.p->Receive(&pd, &ps, &pa, &pp); }, [&] { OriginalAt<FReceive>(O_RECEIVE)(g.o, &od, &os, &oa, &op); });
    const long pOff = pd ? pd - g.p->m_buffer : -1, oOff = od ? od - OF<uint8_t *>(g.o, 0x14) : -1;
    Check(pOff == oOff && ps == os && pa == oa && pp == op,
          "received at " + N(oOff) + " size " + N(os) + " from " + Hex(oa) + ":" + N(op) + " / " + N(pOff) + " " + N(ps) + " " +
              Hex(pa) + ":" + N(pp));
    CompareSockets();
}

void OpCounters()
{
    g_op = "TakeCounters";
    g_trace += " C";
    int pr = 1, ps = 1, orr = 2, os = 2;
    Both([&] { g.p->TakeCounters(&pr, &ps); }, [&] { OriginalAt<FCounters>(O_COUNTERS)(g.o, &orr, &os); });
    Check(pr == orr && ps == os, "counters");
    CompareSockets();
}

void OpLocal()
{
    g_op = "GetLocalAddress";
    S_Script s;
    s.hostResult = P(0.2) ? -1 : 0;
    s.host = I(0, 2);
    s.hostAddress = P(0.5) ? 0x0A00A8C0 : U32();
    Script(s);
    if (P(0.6)) {
        g.p->m_localAddress = 0;
        OF<uint32_t>(g.o, 0x7C) = 0;
    }
    g_trace += " A";
    uint32_t a = 1, b = 2;
    Both([&] { a = g.p->GetLocalAddress(); }, [&] { b = OriginalAt<FAddress>(O_LOCAL)(g.o); });
    Check(a == b, "the local address " + Hex(b) + " / " + Hex(a));
    CompareSockets();
}

void OpOpenClose()
{
    if (P(0.6)) {
        g_op = "Open";
        const int port = P(0.3) ? -1 : I(0, 65535);
        g_trace += " O" + N(port);
        Both([&] { g.p->Open(port); }, [&] { OriginalAt<FVI>(O_OPEN)(g.o, port); });
        // (The port makes its two events before starting the thread, the original after it: the same calls.)
        CompareLogs(true);
        g_log[0].clear();
        g_log[1].clear();
    } else {
        g_op = "Close";
        g_trace += " X";
        Both([&] { g.p->Close(); }, [&] { OriginalAt<FV>(O_CLOSE)(g.o); });
    }
    CompareSockets();
}

// The send thread's entry as CreateThread was given it (each program's own), run here: its waits answered until it is
// told to stop.
void OpThreadEntry()
{
    if (!g_threadStart[0] || !g_threadStart[1] || g.p->m_socket == static_cast<uintptr_t>(-1))
        return;
    g_op = "the send thread's entry";
    g_trace += " TE";
    S_Script sc;
    sc.waits = I(0, 2);
    Script(sc);
    g.p->m_stop = false;
    OF<uint8_t>(g.o, 0x70) = 0;
    g_stopFlag[0] = reinterpret_cast<uint8_t *>(&g.p->m_stop);
    g_stopFlag[1] = g.o + 0x70;
    DWORD a = 7, b = 7;
    Both([&] { a = g_threadStart[0](g_threadParam[0]); }, [&] { b = g_threadStart[1](g_threadParam[1]); });
    g_stopFlag[0] = g_stopFlag[1] = nullptr;
    Check(a == b, "the thread's exit code");
    CollectSent();
    CompareSockets();
}

void Round()
{
    g_trace.clear();
    std::memset(g.o, 0xEE, sizeof g.o);
    g.p = new (g.pStore) C_NetSocket();
    OriginalAt<FV>(O_CTOR)(g.o);
    // (The original's constructor leaves the byte counters and the thread's fields as the memory was: see the notes.)
    OF<int>(g.o, 0x1C) = OF<int>(g.o, 0x20) = 0;
    OF<uint32_t>(g.o, 0x6C) = 0;
    OF<uint8_t>(g.o, 0x70) = 0;
    OF<uint32_t>(g.o, 0x74) = OF<uint32_t>(g.o, 0x78) = 0;
    // (Its sockaddr and the local name past their first byte likewise: SendRaw before Open sends with the family as
    // the memory was.)
    std::memset(g.o + 4, 0, 16);
    std::memset(g.o + 0x80, 0, 20);
    g_op = "construct";
    std::memset(g.p->m_buffer, 0, 0x8000);
    std::memset(OF<uint8_t *>(g.o, 0x14), 0, 0x8000);
    std::memset(g.p->m_fragmentBuffer, 0, 800);
    std::memset(OF<uint8_t *>(g.o, 0x18), 0, 800);
    g_log[0].clear();
    g_log[1].clear();
    CompareSockets();
    const int ops = I(1, 50);
    for (int k = 0; k < ops; ++k) {
        const int op = I(0, 99);
        if (op < 10)
            OpSeal();
        else if (op < 30)
            OpSend();
        else if (op < 44)
            OpFlush();
        else if (op < 56)
            OpSendQueued();
        else if (op < 62)
            OpQueueRaw();
        else if (op < 88)
            OpReceive();
        else if (op < 91)
            OpCounters();
        else if (op < 94)
            OpLocal();
        else if (op < 96)
            OpThreadEntry();
        else
            OpOpenClose();
    }
    g_threadStart[0] = g_threadStart[1] = nullptr;
    g_op = "destroy";
    g_trace += " ~";
    Both([&] { g.p->~C_NetSocket(); }, [&] { OriginalAt<FV>(O_DTOR)(g.o); });
    CompareLogs();
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side (ops:%s)\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, g_round, g_op, g_side ? "the original" : "the port", g_trace.c_str());
    if (!g_lastWire.bytes.empty())
        std::printf("  the datagram: %d bytes, %02x %02x %02x %02x; fragments held %d\n", static_cast<int>(g_lastWire.bytes.size()),
                    g_lastWire.bytes[0], g_lastWire.bytes.size() > 1 ? g_lastWire.bytes[1] : 0,
                    g_lastWire.bytes.size() > 2 ? g_lastWire.bytes[2] : 0, g_lastWire.bytes.size() > 3 ? g_lastWire.bytes[3] : 0,
                    static_cast<int>(g.p->m_fragments.size()));
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
    if (std::getenv("HD2_SOCK_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("sock_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    RedirectOriginal(O_NEW, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(O_DELETE, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(O_MALLOC, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(O_FREE, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(O_CLOCK, reinterpret_cast<const void *>(OClock));
    if (!RedirectSystemCalls())
        return 1;
    OriginalAt<FPtr>(O_POOL_CTOR)(reinterpret_cast<void *>(O_POOL));
    int rounds = std::getenv("HD2_SOCK_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_SOCK_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_SOCK_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("sock_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
