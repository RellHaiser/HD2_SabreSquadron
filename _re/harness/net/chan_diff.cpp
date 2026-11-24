// Net part 3: the transport's packets, pools, channels and messages (net/net_channel.cpp) against the original
// executable's. MinGW; build_chan.bat; runs chan_diff.exe ("chan_diff: N checks, 0 failures").
//
// Both programs keep their own pool (the port's g_netPool; the original's at 0x00983150, built by its constructor);
// their packets and buffers are paired as they first appear (the free lists walked in order, the results of the
// calls), so every later pointer is checked against its partner. Each round makes a channel pair (plain, reliable or
// ordered; the original's by its constructor in a buffer of its layout: its std::lists are MSVC6's, walked node by
// node) and runs a random sequence of calls: packets from the pool (sizes up to 1000, bigger, none), Send,
// PopOutgoing, Receive (sequence numbers near the newest, behind it, repeated, across the 23-bit wrap), PopReceived,
// WriteAck / ReadAck, Resend (the clock moved on), Acknowledge, AddRef / Release, the packets' Write / Read, FreeData
// and Clear, the pool's calls, UnwrapSequence, the messages (the header, the packet list, the ping). After each call
// the channels, every packet the test holds, the pools' free lists and the streams are compared. The original's
// operator new / delete, malloc / free and clock (0x00748dc0) are the test's.
// HD2_CHAN_ROUNDS: rounds (default 30,000; HD2_CHAN_QUICK: 8,000); HD2_CHAN_SEED; HD2_CHAN_FIRST_CHANCE.
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "net/bitstream.h"
#include "net/net_channel.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

// The deletes, recorded on each side while g_record (the destructors' tests): fixed arrays (a vector's growth would
// delete through the recorder itself).
bool g_record;
struct S_Deleted {
    void *items[4096];
    int count = 0;
    void push_back(void *p)
    {
        if (count < 4096)
            items[count++] = p;
    }
    void clear() { count = 0; }
} g_deleted[2];

// The port's free (linked with --wrap=free: its pool's big buffers), recorded likewise.
extern "C" void __real_free(void *);
extern "C" void __wrap_free(void *p)
{
    if (g_record && p)
        g_deleted[0].push_back(p);
    __real_free(p);
}

// The port's allocations zeroed too (its pool's packets and buffers).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept
{
    if (g_record && p)
        g_deleted[0].push_back(p);
    std::free(p);
}
void operator delete(void *p, size_t) noexcept { operator delete(p); }

// The clock (both programs').
int64_t g_now = 1000000;
int64_t GetPerfTimeMs() { return g_now; }

namespace {

template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T OF(const void *o, int at) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(o) + at); }

#define THISCALL __attribute__((thiscall))
using FV = void(THISCALL *)(void *);
using FVP = void(THISCALL *)(void *, void *);
using FVPI = void(THISCALL *)(void *, void *, int);
using FVI = void(THISCALL *)(void *, int);
using FPtr = void *(THISCALL *)(void *);
using FPtrSize = void *(THISCALL *)(void *, size_t);
using FUnwrap = void(__attribute__((stdcall)) *)(uint32_t *, uint32_t);
using FCtorFixed = void *(THISCALL *)(void *, uint32_t *, uint32_t);

const uint32_t O_POOL = 0x00983150, O_POOL_CTOR = 0x00749830, O_ALLOC_BUFFER = 0x00749a70, O_FREE_BUFFER = 0x00749b20,
               O_ALLOC_PACKET = 0x00749b80, O_FREE_PACKET = 0x00749bd0;
const uint32_t O_UNWRAP = 0x00745520;
const uint32_t O_CHAN_CTOR[3] = {0x00745330, 0x007457b0, 0x00745d30};
const uint32_t O_CHAN_VT[3] = {0x008165f0, 0x00816614, 0x00816638};
const uint32_t O_CLOCK = 0x00748dc0, O_NEW = 0x007e566f, O_DELETE = 0x007e2120, O_MALLOC = 0x007e8627, O_FREE = 0x007e8761;
const uint32_t O_HEADER_CTOR = 0x00747430, O_LIST_CTOR = 0x00747500, O_LIST_DTOR = 0x00747590, O_LIST_ADD = 0x00747720,
               O_LIST_POP = 0x00747770, O_PING_CTOR = 0x007477b0;
const uint32_t O_STREAM_CTOR = 0x0073d290;
// The slots: the packet's, the channel's, the message's.
enum { PK_DTOR, PK_ADDREF, PK_RELEASE, PK_CLEAR, PK_FREEDATA, PK_WRITE, PK_READ };
enum { CH_DTOR, CH_SEND, CH_POP_RECEIVED, CH_POP_OUTGOING, CH_RECEIVE, CH_WRITE_ACK, CH_READ_ACK, CH_RESEND, CH_ACK };
enum { MS_DTOR, MS_READ, MS_WRITE, MS_WRITE_BODY, MS_READ_BODY };

uint32_t Slot(const void *o, int slot) { return OF<uint32_t>(reinterpret_cast<const void *>(OF<uint32_t>(o, 0)), slot * 4); }

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

std::map<const void *, const void *> g_p2o, g_o2p;

// ---- the original's allocations ----
// Zeroed, like the port's (below): a new packet's sequence number is never set (Clear leaves it), so both programs'
// fresh memory must agree.
void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p)
{
    if (g_record && p)
        g_deleted[1].push_back(p);
    std::free(p);
}
bool Deleted(int side, const void *p)
{
    for (int i = 0; i < g_deleted[side].count; ++i)
        if (g_deleted[side].items[i] == p)
            return true;
    return false;
}
void Unpair(const void *p, const void *o)
{
    g_p2o.erase(p);
    g_o2p.erase(o);
}
int64_t OClock() { return g_now; }

// ---- the pairs: the port's object (packet or buffer) and the original's (g_p2o, g_o2p) ----
bool Match(const void *p, const void *o, const std::string &what)
{
    if (!p || !o) {
        Check(!p && !o, what + ": " + (p ? "" : "null") + " / " + (o ? "" : "null"));
        return !p && !o;
    }
    auto a = g_p2o.find(p), b = g_o2p.find(o);
    if (a == g_p2o.end() && b == g_o2p.end()) {
        g_p2o[p] = o;
        g_o2p[o] = p;
        ++checks;
        return true;
    }
    const bool ok = a != g_p2o.end() && a->second == o;
    Check(ok, what + ": not the partner");
    return ok;
}

// A data buffer: a pool's (up to 1000 bytes) paired; a bigger one (malloc'ed, freed by FreeBuffer: its address can come
// back on either side for another) only null or not.
bool MatchData(const void *p, const void *o, int size, const std::string &what)
{
    if (size > 1000) {
        Check((p == nullptr) == (o == nullptr), what + ": null / not");
        return (p == nullptr) == (o == nullptr);
    }
    return Match(p, o, what);
}

// ---- the packets ----
void *OPool() { return reinterpret_cast<void *>(O_POOL); }

bool ComparePacket(C_NetPacket *p, const uint8_t *o, const std::string &what)
{
    std::string s;
    if (p->m_refs != OF<int>(o, 4))
        s += "refs " + N(OF<int>(o, 4)) + " / " + N(p->m_refs) + "; ";
    if (p->m_flags != OF<uint32_t>(o, 8) || p->m_size != OF<int>(o, 0xC) || p->m_kind != OF<uint32_t>(o, 0x10) ||
        p->m_sequence != OF<uint32_t>(o, 0x14) || p->m_session != OF<uint32_t>(o, 0x18))
        s += "fields " + Hex(OF<uint32_t>(o, 8)) + " " + N(OF<int>(o, 0xC)) + " " + N(OF<uint32_t>(o, 0x10)) + " " +
             Hex(OF<uint32_t>(o, 0x14)) + " " + N(OF<uint32_t>(o, 0x18)) + " / " + Hex(p->m_flags) + " " + N(p->m_size) +
             " " + N(p->m_kind) + " " + Hex(p->m_sequence) + " " + N(p->m_session) + "; ";
    Check(s.empty(), what + ": " + s);
    const void *od = OF<void *>(o, 0x1C);
    if (!MatchData(p->m_data, od, p->m_size, what + " data"))
        return false;
    if (p->m_data && p->m_size > 0) {
        const size_t n = static_cast<size_t>(std::min(p->m_size, 1000));
        Check(std::memcmp(p->m_data, od, n) == 0, what + ": the data's bytes");
    }
    return s.empty();
}

// The packets the test holds (one reference each): the port's and the original's.
struct S_Held {
    C_NetPacket *p;
    uint8_t *o;
};
std::vector<S_Held> g_held;

void CompareHeld()
{
    for (const S_Held &h : g_held)
        ComparePacket(h.p, h.o, "a held packet");
}

// The free lists: the port's vector (the last freed at the back), the original's stack of {item, next} nodes.
void CompareFreeList(const S_NetFreeList &p, int at, const char *what)
{
    const uint8_t *o = reinterpret_cast<const uint8_t *>(O_POOL) + at;
    Check(p.count == OF<int>(o, 0x10) && p.growBy == OF<int>(o, 0xC),
          std::string(what) + " count " + N(OF<int>(o, 0x10)) + " / " + N(p.count));
    const uint8_t *node = OF<const uint8_t *>(o, 4);
    size_t i = p.items.size();
    int n = 0;
    while (node && i > 0 && n < 100000) {
        if (!Match(p.items[i - 1], OF<const void *>(node, 0), std::string(what) + " item " + N(n)))
            return;
        node = OF<const uint8_t *>(node, 4);
        --i;
        ++n;
    }
    Check(!node && i == 0, std::string(what) + " lengths");
}

void ComparePools()
{
    // The pools' locks let go after every call (a missing Enter or Leave unbalances them).
    const CRITICAL_SECTION &ol = *reinterpret_cast<const CRITICAL_SECTION *>(O_POOL + 0x30);
    Check(g_netPool.m_lock.RecursionCount == 0 && g_netPool.m_lock.OwningThread == nullptr && g_netPool.m_lock.LockCount == -1,
          "the port's pool lock held: " + N(g_netPool.m_lock.RecursionCount) + " " + N(g_netPool.m_lock.LockCount));
    Check(ol.RecursionCount == 0 && ol.OwningThread == nullptr && ol.LockCount == -1,
          "the original's pool lock held: " + N(ol.RecursionCount) + " " + N(ol.LockCount));
    if (g_netPool.m_lock.RecursionCount != 0 || g_netPool.m_lock.LockCount != -1) {
        // (Reset, so one fault is reported once.)
        DeleteCriticalSection(&g_netPool.m_lock);
        InitializeCriticalSection(&g_netPool.m_lock);
    }
    CompareFreeList(g_netPool.m_buffers, 0, "free buffers");
    CompareFreeList(g_netPool.m_packets, 0x18, "free packets");
}

// ---- the channels ----
struct S_Chan {
    int kind = 0;   // 0 plain, 1 reliable, 2 ordered
    alignas(16) uint8_t o[0x50];
    alignas(16) uint8_t pStore[sizeof(C_NetOrderedChannel)];
    C_NetChannel *p = nullptr;
    C_NetReliableChannel *rel() { return static_cast<C_NetReliableChannel *>(p); }
} g_ch;

// An MSVC6 std::list at `at`: its values (+8 in each node).
std::vector<const uint8_t *> OList(const void *o, int at)
{
    std::vector<const uint8_t *> v;
    const uint8_t *head = OF<const uint8_t *>(o, at + 4);
    const int size = OF<int>(o, at + 8);
    if (!head)
        return v;
    for (const uint8_t *n = OF<const uint8_t *>(head, 0); n != head && static_cast<int>(v.size()) <= size; n = OF<const uint8_t *>(n, 0))
        v.push_back(n + 8);
    Check(static_cast<int>(v.size()) == size, "an MSVC list's size");
    return v;
}

void ComparePacketList(const std::list<C_NetPacket *> &p, const void *o, int at, const char *what)
{
    std::vector<const uint8_t *> ov = OList(o, at);
    if (ov.size() != p.size()) {
        Check(false, std::string(what) + " length " + N(ov.size()) + " / " + N(p.size()));
        return;
    }
    size_t i = 0;
    for (C_NetPacket *pk : p) {
        const uint8_t *opk = OF<const uint8_t *>(ov[i], 0);
        if (Match(pk, opk, std::string(what) + " " + N(i)))
            ComparePacket(pk, opk, std::string(what) + " " + N(i));
        ++i;
    }
}

void CompareChannel()
{
    C_NetChannel *p = g_ch.p;
    const uint8_t *o = g_ch.o;
    Check(p->m_sent == OF<uint32_t>(o, 4) && p->m_received == OF<uint32_t>(o, 8),
          "sent / received " + Hex(OF<uint32_t>(o, 4)) + " " + Hex(OF<uint32_t>(o, 8)) + " / " + Hex(p->m_sent) + " " +
              Hex(p->m_received));
    ComparePacketList(p->m_outgoing, o, 0xC, "outgoing");
    ComparePacketList(p->m_incoming, o, 0x18, "incoming");
    if (g_ch.kind >= 1) {
        C_NetReliableChannel *r = g_ch.rel();
        Check(r->m_delivered == OF<uint32_t>(o, 0x24) && r->m_acked == OF<uint32_t>(o, 0x28) &&
                  r->m_resendTime == OF<int>(o, 0x2C),
              "delivered / acked / resend " + Hex(OF<uint32_t>(o, 0x24)) + " " + Hex(OF<uint32_t>(o, 0x28)) + " " +
                  N(OF<int>(o, 0x2C)) + " / " + Hex(r->m_delivered) + " " + Hex(r->m_acked) + " " + N(r->m_resendTime));
        std::vector<const uint8_t *> pend = OList(o, 0x30);
        if (pend.size() != r->m_pending.size()) {
            Check(false, "pending length " + N(pend.size()) + " / " + N(r->m_pending.size()));
        } else {
            size_t i = 0;
            for (const C_NetReliableChannel::S_Pending &e : r->m_pending) {
                Check(e.time == OF<int>(pend[i], 0) && e.interval == OF<int>(pend[i], 4),
                      "pending " + N(i) + " time " + N(OF<int>(pend[i], 0)) + " " + N(OF<int>(pend[i], 4)) + " / " +
                          N(e.time) + " " + N(e.interval));
                const uint8_t *opk = OF<const uint8_t *>(pend[i], 8);
                if (Match(e.packet, opk, "pending " + N(i)))
                    ComparePacket(e.packet, opk, "pending " + N(i));
                ++i;
            }
        }
        ComparePacketList(r->m_unordered, o, 0x3C, "unordered");
    }
    if (g_ch.kind == 2)
        Check(static_cast<C_NetOrderedChannel *>(p)->m_next == OF<uint32_t>(o, 0x48), "next");
}

void CompareAll()
{
    CompareChannel();
    CompareHeld();
    ComparePools();
}

// ---- the streams (fixed buffers) ----
const int WORDS = 4096;
uint32_t g_pBuf[WORDS], g_oBuf[WORDS];
alignas(16) uint8_t g_ostream[0x20];
alignas(16) uint8_t g_pstreamStore[sizeof(C_BitStream)];
C_BitStream *g_pstream;
std::vector<uint32_t> g_starts;   // where packets were written (read back from there only)

void NewStreams()
{
    for (int i = 0; i < WORDS; ++i)
        g_pBuf[i] = g_oBuf[i] = P(0.3) ? 0 : U32();
    g_pstream = new (g_pstreamStore) C_BitStream(g_pBuf, WORDS * 32);
    OriginalAt<FCtorFixed>(O_STREAM_CTOR)(g_ostream, g_oBuf, WORDS * 32);
    g_starts.clear();
}
uint32_t OStreamPos() { return OF<uint32_t>(g_ostream, 8) * 32 + OF<uint32_t>(g_ostream, 0xC); }
void SetStreamPos(uint32_t at)
{
    g_pstream->SetBitPosition(at);
    OF<uint32_t>(g_ostream, 8) = at >> 5;
    OF<uint32_t>(g_ostream, 0xC) = at & 31;
}
void CompareStreams()
{
    Check(g_pstream->BitPosition() == OStreamPos(), "stream position " + N(OStreamPos()) + " / " + N(g_pstream->BitPosition()));
    if (std::memcmp(g_pBuf, g_oBuf, sizeof g_pBuf) != 0) {
        int i = 0;
        while (g_pBuf[i] == g_oBuf[i])
            ++i;
        Check(false, "stream word " + N(i) + ": " + Hex(g_oBuf[i]) + " / " + Hex(g_pBuf[i]));
        std::memcpy(g_pBuf, g_oBuf, sizeof g_pBuf);
    }
    SetStreamPos(OStreamPos());
}
bool StreamRoom(uint32_t bits) { return g_pstream->BitPosition() + bits < (WORDS - 8) * 32u; }

// ---- the ops ----
uint32_t Seq23(uint32_t base)
{
    switch (I(0, 6)) {
    case 0: return U32() & 0x7FFFFF;
    case 1: return I(0, 3);
    case 2: return 0x7FFFFF - static_cast<uint32_t>(I(0, 3));
    case 3: return (base - static_cast<uint32_t>(I(0, 12))) & 0x7FFFFF;
    default: return (base + static_cast<uint32_t>(I(0, 3))) & 0x7FFFFF;
    }
}

void OpNewPacket()
{
    g_op = "AllocPacket";
    C_NetPacket *p = g_netPool.AllocPacket();
    uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(O_ALLOC_PACKET)(OPool()));
    if (!Match(p, o, "the packet allocated"))
        return;
    ComparePacket(p, o, "the new packet");
    // Its fields and data (the pool's buffer: up to 1000 bytes, more, or none).
    const uint32_t flags = static_cast<uint32_t>(I(0, 3)), kind = static_cast<uint32_t>(I(0, 63)),
                   seq = Seq23(g_ch.p->m_sent + 1), session = static_cast<uint32_t>(I(0, 1));
    int size = P(0.1) ? 0 : I(1, 300);
    if (P(0.05))
        size = I(1001, 1400);
    if (P(0.05))
        size = 1000;
    g_op = "AllocBuffer";
    void *pd = nullptr, *od = nullptr;
    if (size > 0 || P(0.5)) {
        pd = g_netPool.AllocBuffer(static_cast<size_t>(size));
        od = OriginalAt<FPtrSize>(O_ALLOC_BUFFER)(OPool(), static_cast<size_t>(size));
        if (!MatchData(pd, od, size, "the buffer allocated"))
            return;
        for (int i = 0; i < size; ++i)
            static_cast<uint8_t *>(pd)[i] = static_cast<uint8_t *>(od)[i] = static_cast<uint8_t>(U32());
    }
    p->m_flags = flags, p->m_kind = kind, p->m_sequence = seq, p->m_session = session, p->m_size = size, p->m_data = pd;
    OF<uint32_t>(o, 8) = flags, OF<int>(o, 0xC) = size, OF<uint32_t>(o, 0x10) = kind, OF<uint32_t>(o, 0x14) = seq;
    OF<uint32_t>(o, 0x18) = session, OF<void *>(o, 0x1C) = od;
    p->AddRef();
    OriginalAt<FV>(Slot(o, PK_ADDREF))(o);
    g_held.push_back({p, o});
    g_trace += " N" + N(size);
    CompareAll();
}

S_Held *PickHeld()
{
    if (g_held.empty())
        return nullptr;
    return &g_held[static_cast<size_t>(I(0, static_cast<int>(g_held.size()) - 1))];
}

// A packet's references before a call that may take the caller's (the reliable channel's Receive of a repeat): one
// held entry dropped when they went down (or the packet went back to the pool).
void TakeIfReleased(S_Held h, int before)
{
    if (h.p->m_refs < before || h.p->m_refs == 0) {
        for (size_t i = 0; i < g_held.size(); ++i)
            if (g_held[i].p == h.p) {
                g_held.erase(g_held.begin() + static_cast<long>(i));
                break;
            }
    }
}

void OpSend()
{
    S_Held *h = PickHeld();
    if (!h)
        return;
    g_op = "Send";
    g_trace += " S";
    g_ch.p->Send(h->p);
    OriginalAt<FVP>(Slot(g_ch.o, CH_SEND))(g_ch.o, h->o);
    CompareAll();
}

void OpPop(bool outgoing)
{
    g_op = outgoing ? "PopOutgoing" : "PopReceived";
    g_trace += outgoing ? " PO" : " PR";
    C_NetPacket *p = outgoing ? g_ch.p->PopOutgoing() : g_ch.p->PopReceived();
    uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(Slot(g_ch.o, outgoing ? CH_POP_OUTGOING : CH_POP_RECEIVED))(g_ch.o));
    if (Match(p, o, "the packet popped") && p) {
        // The channel's reference is the caller's now.
        g_held.push_back({p, o});
    }
    CompareAll();
}

void OpReceive()
{
    S_Held *hp = PickHeld();
    if (!hp)
        return;
    const S_Held h = *hp;
    g_op = "Receive";
    // Its number near the newest received (or delivered), behind it, a repeat, across the wrap.
    const uint32_t base = g_ch.kind ? (P(0.5) ? g_ch.rel()->m_delivered + 1 : g_ch.p->m_received + 1) : g_ch.p->m_received + 1;
    uint32_t seq = Seq23(base);
    if (g_ch.kind && P(0.2) && !g_ch.rel()->m_unordered.empty())
        seq = g_ch.rel()->m_unordered.front()->m_sequence & 0x7FFFFF;   // a repeat
    h.p->m_sequence = seq;
    OF<uint32_t>(h.o, 0x14) = seq;
    g_trace += " V" + Hex(seq);
    const int before = h.p->m_refs;
    g_ch.p->Receive(h.p);
    OriginalAt<FVP>(Slot(g_ch.o, CH_RECEIVE))(g_ch.o, h.o);
    TakeIfReleased(h, before);
    CompareAll();
}

void OpAck()
{
    if (!StreamRoom(64))
        return;
    if (P(0.5)) {
        g_op = "WriteAck";
        g_trace += " WA";
        g_ch.p->WriteAck(g_pstream);
        OriginalAt<FVP>(Slot(g_ch.o, CH_WRITE_ACK))(g_ch.o, g_ostream);
    } else {
        g_op = "ReadAck";
        // A number near the last acknowledged (or anything), then read back.
        const uint32_t at = g_pstream->BitPosition();
        const uint32_t base = g_ch.kind ? g_ch.rel()->m_acked + 1 : 0;
        const uint32_t seq = P(0.2) ? U32() & 0x7FFFFF : Seq23(base + static_cast<uint32_t>(I(0, 8)));
        g_pstream->WriteBits(seq, 23);
        g_pstream->WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
        std::memcpy(g_oBuf, g_pBuf, sizeof g_pBuf);
        SetStreamPos(at);
        g_trace += " RA" + Hex(seq);
        g_ch.p->ReadAck(g_pstream);
        OriginalAt<FVP>(Slot(g_ch.o, CH_READ_ACK))(g_ch.o, g_ostream);
    }
    CompareStreams();
    CompareAll();
}

void OpResend()
{
    g_op = "Resend";
    g_now += P(0.3) ? 0 : (P(0.5) ? I(0, 400) : I(0, 8000));
    g_trace += " RS";
    g_ch.p->Resend();
    OriginalAt<FV>(Slot(g_ch.o, CH_RESEND))(g_ch.o);
    CompareAll();
}

void OpAcknowledge()
{
    g_op = "Acknowledge";
    g_trace += " AK";
    g_ch.p->Acknowledge();
    OriginalAt<FV>(Slot(g_ch.o, CH_ACK))(g_ch.o);
    CompareAll();
}

void OpRefs()
{
    S_Held *hp = PickHeld();
    if (!hp)
        return;
    const S_Held h = *hp;
    if (P(0.4)) {
        g_op = "AddRef";
        g_trace += " +";
        h.p->AddRef();
        OriginalAt<FV>(Slot(h.o, PK_ADDREF))(h.o);
        g_held.push_back(h);
    } else {
        g_op = "Release";
        g_trace += " -";
        for (size_t i = 0; i < g_held.size(); ++i)
            if (g_held[i].p == h.p) {
                g_held.erase(g_held.begin() + static_cast<long>(i));
                break;
            }
        h.p->Release();
        OriginalAt<FV>(Slot(h.o, PK_RELEASE))(h.o);
        // (Back in the pool when it was the last: compared there.)
        bool still = false;
        for (const S_Held &x : g_held)
            still |= x.p == h.p;
        if (!still)
            ComparePacket(h.p, h.o, "the packet released");
    }
    CompareAll();
}

void OpPacketIO()
{
    if (P(0.5) || g_starts.empty()) {
        S_Held *hp = PickHeld();
        if (!hp || hp->p->m_size < 0 || (!hp->p->m_data && hp->p->m_size) ||
            !StreamRoom(48 + static_cast<uint32_t>(hp->p->m_size) * 8 + 64))
            return;
        g_op = "packet Write";
        g_trace += " PW";
        g_starts.push_back(g_pstream->BitPosition());
        hp->p->Write(g_pstream);
        OriginalAt<FVP>(Slot(hp->o, PK_WRITE))(hp->o, g_ostream);
        CompareStreams();
        CompareAll();
        return;
    }
    // Read into a new packet from where a write began (its data from the pool).
    const uint32_t at = g_starts[static_cast<size_t>(I(0, static_cast<int>(g_starts.size()) - 1))];
    SetStreamPos(at);
    const uint32_t save = at;
    g_pstream->ReadBits(2);
    const int size = static_cast<int16_t>(g_pstream->ReadBits(16));
    SetStreamPos(save);
    if (size < 0)
        return;
    g_op = "packet Read";
    g_trace += " PRd";
    C_NetPacket *p = g_netPool.AllocPacket();
    uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(O_ALLOC_PACKET)(OPool()));
    if (!Match(p, o, "the packet allocated"))
        return;
    p->Read(g_pstream);
    OriginalAt<FVP>(Slot(o, PK_READ))(o, g_ostream);
    p->AddRef();
    OriginalAt<FV>(Slot(o, PK_ADDREF))(o);
    g_held.push_back({p, o});
    CompareStreams();
    CompareAll();
}

void OpClearPacket()
{
    // A packet with data: FreeData, Clear, back to the pool by FreePacket (as Release does, called one by one).
    g_op = "FreeData/Clear/FreePacket";
    g_trace += " FC";
    C_NetPacket *p = g_netPool.AllocPacket();
    uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(O_ALLOC_PACKET)(OPool()));
    if (!Match(p, o, "the packet allocated"))
        return;
    const int size = P(0.8) ? I(0, 1000) : I(1001, 1200);
    void *pd = g_netPool.AllocBuffer(static_cast<size_t>(size));
    void *od = OriginalAt<FPtrSize>(O_ALLOC_BUFFER)(OPool(), static_cast<size_t>(size));
    MatchData(pd, od, size, "the buffer allocated");
    p->m_size = size, p->m_data = P(0.1) ? nullptr : pd;
    OF<int>(o, 0xC) = size, OF<void *>(o, 0x1C) = p->m_data ? od : nullptr;
    p->m_refs = OF<int>(o, 4) = I(0, 3);
    p->m_flags = OF<uint32_t>(o, 8) = 3;
    p->FreeData();
    OriginalAt<FV>(Slot(o, PK_FREEDATA))(o);
    if (!p->m_data) {
        // (The buffer not given to the packet goes back by FreeBuffer.)
        g_netPool.FreeBuffer(pd, size);
        OriginalAt<FVPI>(O_FREE_BUFFER)(OPool(), od, size);
    }
    p->Clear();
    OriginalAt<FV>(Slot(o, PK_CLEAR))(o);
    ComparePacket(p, o, "the packet cleared");
    g_netPool.FreePacket(p);
    OriginalAt<FVP>(O_FREE_PACKET)(OPool(), o);
    CompareAll();
}

void OpPool()
{
    g_op = "the pool's buffers";
    g_trace += " B";
    if (P(0.2)) {
        // FreeBuffer of none (small: nothing; big: free(null)), FreePacket of none.
        g_netPool.FreeBuffer(nullptr, 10);
        OriginalAt<FVPI>(O_FREE_BUFFER)(OPool(), nullptr, 10);
        g_netPool.FreePacket(nullptr);
        OriginalAt<FVP>(O_FREE_PACKET)(OPool(), nullptr);
        CompareAll();
        return;
    }
    // Some buffers out (more than the pool holds now and then: it grows), then back in any order.
    const int n = P(0.2) ? I(17, 40) : I(1, 5);
    std::vector<std::pair<void *, void *>> got;
    std::vector<int> sizes;
    for (int i = 0; i < n; ++i) {
        const int size = P(0.9) ? I(0, 1000) : I(1001, 3000);
        void *pd = g_netPool.AllocBuffer(static_cast<size_t>(size));
        void *od = OriginalAt<FPtrSize>(O_ALLOC_BUFFER)(OPool(), static_cast<size_t>(size));
        MatchData(pd, od, size, "the buffer allocated");
        got.push_back({pd, od});
        sizes.push_back(size);
    }
    ComparePools();
    while (!got.empty()) {
        const size_t i = static_cast<size_t>(I(0, static_cast<int>(got.size()) - 1));
        g_netPool.FreeBuffer(got[i].first, sizes[i]);
        OriginalAt<FVPI>(O_FREE_BUFFER)(OPool(), got[i].second, sizes[i]);
        got.erase(got.begin() + static_cast<long>(i));
        sizes.erase(sizes.begin() + static_cast<long>(i));
    }
    CompareAll();
}

void OpUnwrap()
{
    g_op = "UnwrapSequence";
    const uint32_t edges[] = {0, 1, 0x1FFFFE, 0x1FFFFF, 0x200000, 0x5FFFFD, 0x5FFFFE, 0x5FFFFF, 0x7FFFFE, 0x7FFFFF};
    for (int k = 0; k < 8; ++k) {
        uint32_t seq = P(0.5) ? edges[I(0, 9)] : U32();
        if (P(0.3))
            seq |= U32() & 0xFF800000u;   // high bits: dropped
        uint32_t ref = P(0.5) ? (edges[I(0, 9)] + (U32() & 0xFF800000u)) : U32();
        if (P(0.3))
            ref = static_cast<uint32_t>(I(-3, 3)) + 0x3FFFFF;
        uint32_t a = seq, b = seq;
        UnwrapSequence(&a, ref);
        OriginalAt<FUnwrap>(O_UNWRAP)(&b, ref);
        Check(a == b, "UnwrapSequence(" + Hex(seq) + ", " + Hex(ref) + ") " + Hex(b) + " / " + Hex(a));
    }
}

// The messages: the header's Write / Read, the packet list (the packets held added; written; read back into packets
// from the pool, popped; destroyed with what is left), the ping.
void OpMessages()
{
    if (!StreamRoom(16 + 8 + 6 * (48 + 1400 * 8) + 64))   // (a list carries up to 6 packets of up to 1400 bytes)
        return;
    const int which = I(0, 2);
    if (which == 0) {
        g_op = "C_NetHeader Write/Read";
        g_trace += " H";
        C_NetHeader ph;
        alignas(16) uint8_t oh[0x10];
        OriginalAt<FPtr>(O_HEADER_CTOR)(oh);
        ph.m_type = OF<uint32_t>(oh, 4) = static_cast<uint32_t>(I(0, 255));
        ph.m_connection = OF<uint32_t>(oh, 8) = static_cast<uint32_t>(I(0, 255));
        const uint32_t at = g_pstream->BitPosition();
        ph.Write(g_pstream);
        OriginalAt<FVP>(Slot(oh, MS_WRITE))(oh, g_ostream);
        CompareStreams();
        SetStreamPos(at);
        ph.m_type = ph.m_connection = 77;
        OF<uint32_t>(oh, 4) = OF<uint32_t>(oh, 8) = 77;
        ph.Read(g_pstream);
        OriginalAt<FVP>(Slot(oh, MS_READ))(oh, g_ostream);
        Check(ph.m_type == OF<uint32_t>(oh, 4) && ph.m_connection == OF<uint32_t>(oh, 8), "the header read");
        CompareStreams();
        // The header's bodies are empty.
        C_NetHeader *volatile base = &ph;   // (through the vtable: not inlined away)
        base->WriteBody(g_pstream);
        base->ReadBody(g_pstream);
        OriginalAt<FVP>(Slot(oh, MS_WRITE_BODY))(oh, g_ostream);
        OriginalAt<FVP>(Slot(oh, MS_READ_BODY))(oh, g_ostream);
        CompareStreams();
        OriginalAt<FVI>(Slot(oh, MS_DTOR))(oh, 0);
        return;
    }
    if (which == 1) {
        g_op = "C_NetPacketList";
        g_trace += " L";
        alignas(16) uint8_t store[sizeof(C_NetPacketList)];
        alignas(16) uint8_t ol[0x20];
        C_NetPacketList *pl = new (store) C_NetPacketList();
        OriginalAt<FPtr>(O_LIST_CTOR)(ol);
        Check(OF<uint32_t>(ol, 4) == 1, "the list's type");
        // Packets added: each with a reference of its own (the list releases them).
        const int n = g_held.empty() ? 0 : I(0, std::min(6, static_cast<int>(g_held.size())));
        for (int i = 0; i < n; ++i) {
            S_Held h = g_held[static_cast<size_t>(I(0, static_cast<int>(g_held.size()) - 1))];
            if (h.p->m_size < 0 || (!h.p->m_data && h.p->m_size))
                continue;
            h.p->AddRef();
            OriginalAt<FV>(Slot(h.o, PK_ADDREF))(h.o);
            pl->Add(h.p);
            OriginalAt<FVP>(O_LIST_ADD)(ol, h.o);
        }
        ComparePacketList(pl->m_packets, ol, 0xC, "the list");
        const uint32_t at = g_pstream->BitPosition();
        pl->WriteBody(g_pstream);
        OriginalAt<FVP>(Slot(ol, MS_WRITE_BODY))(ol, g_ostream);
        CompareStreams();
        // Read back into a second list.
        alignas(16) uint8_t store2[sizeof(C_NetPacketList)];
        alignas(16) uint8_t ol2[0x20];
        C_NetPacketList *pl2 = new (store2) C_NetPacketList();
        OriginalAt<FPtr>(O_LIST_CTOR)(ol2);
        SetStreamPos(at);
        pl2->ReadBody(g_pstream);
        OriginalAt<FVP>(Slot(ol2, MS_READ_BODY))(ol2, g_ostream);
        CompareStreams();
        ComparePacketList(pl2->m_packets, ol2, 0xC, "the list read");
        // Some popped (the test's then, a reference added), the rest left to the destructors.
        for (int k = I(0, static_cast<int>(pl2->m_packets.size()) + 1); k > 0; --k) {
            C_NetPacket *p = pl2->Pop();
            uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(O_LIST_POP)(ol2));
            if (Match(p, o, "the packet popped") && p) {
                p->AddRef();
                OriginalAt<FV>(Slot(o, PK_ADDREF))(o);
                g_held.push_back({p, o});
            }
        }
        if (P(0.5) && !pl->m_packets.empty()) {
            C_NetPacket *p = pl->Pop();
            uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(O_LIST_POP)(ol));
            if (Match(p, o, "the packet popped") && p)
                g_held.push_back({p, o});   // (the list's reference is the test's now)
        }
        pl->~C_NetPacketList();
        OriginalAt<FV>(O_LIST_DTOR)(ol);
        pl2->~C_NetPacketList();
        OriginalAt<FV>(O_LIST_DTOR)(ol2);
        CompareAll();
        return;
    }
    g_op = "C_NetPing";
    g_trace += " G";
    C_NetPing pp;
    alignas(16) uint8_t op[0x20];
    OriginalAt<FPtr>(O_PING_CTOR)(op);
    Check(OF<uint32_t>(op, 4) == 2, "the ping's type");
    pp.m_reply = OF<uint32_t>(op, 0xC) = static_cast<uint32_t>(I(0, 1));
    pp.m_time = OF<uint32_t>(op, 0x10) = U32();
    pp.m_id = OF<uint32_t>(op, 0x14) = static_cast<uint32_t>(I(0, 255));
    const uint32_t at = g_pstream->BitPosition();
    pp.WriteBody(g_pstream);
    OriginalAt<FVP>(Slot(op, MS_WRITE_BODY))(op, g_ostream);
    CompareStreams();
    // Read back later (the round trip; now and then exactly 0: 1).
    g_now += P(0.2) ? 0 : I(0, 300);
    if (P(0.1)) {
        // A sent time equal to now: a round trip of 0.
        SetStreamPos(at + 9);
        g_pstream->WriteBits(static_cast<uint32_t>(g_now), 32);
        std::memcpy(g_oBuf, g_pBuf, sizeof g_pBuf);
    }
    SetStreamPos(at);
    pp.ReadBody(g_pstream);
    OriginalAt<FVP>(Slot(op, MS_READ_BODY))(op, g_ostream);
    Check(pp.m_reply == OF<uint32_t>(op, 0xC) && pp.m_time == OF<uint32_t>(op, 0x10) && pp.m_id == OF<uint32_t>(op, 0x14),
          "the ping read " + N(OF<uint32_t>(op, 0x10)) + " / " + N(pp.m_time));
    CompareStreams();
    OriginalAt<FVI>(Slot(op, MS_DTOR))(op, 0);
}

// The destructors: a reference-counted object released below one (deleted), a packet deleted (its data back to the
// pool), a pool of its own destroyed (its packets and buffers deleted; the original's list nodes too, not compared).
const uint32_t O_REFCOUNTED_VT = 0x00816678, O_POOL_DTOR = 0x00749970;
void OpDestructors()
{
    const int which = I(0, 2);
    g_deleted[0].clear();
    g_deleted[1].clear();
    if (which == 0) {
        g_op = "C_NetRefCounted::Release";
        g_trace += " RC";
        auto *p = new C_NetRefCounted;
        uint8_t *o = static_cast<uint8_t *>(ONew(8));
        OF<uint32_t>(o, 0) = O_REFCOUNTED_VT;
        const int refs = I(-1, 3);
        p->m_refs = OF<int>(o, 4) = refs;
        g_record = true;
        p->Release();
        OriginalAt<FV>(Slot(o, 2))(o);
        g_record = false;
        const bool pd = Deleted(0, p), od = Deleted(1, o);
        Check(pd == od, "deleted " + N(od) + " / " + N(pd) + " (refs " + N(refs) + ")");
        if (!pd) {
            Check(p->m_refs == OF<int>(o, 4), "refs");
            delete p;
        }
        if (!od)
            std::free(o);
        return;
    }
    if (which == 1) {
        g_op = "~C_NetPacket";
        g_trace += " PD";
        C_NetPacket *p = g_netPool.AllocPacket();
        uint8_t *o = static_cast<uint8_t *>(OriginalAt<FPtr>(O_ALLOC_PACKET)(OPool()));
        if (!Match(p, o, "the packet allocated"))
            return;
        const int size = P(0.2) ? 0 : (P(0.9) ? I(1, 1000) : I(1001, 1200));
        void *pd = nullptr, *od = nullptr;
        if (size || P(0.5)) {
            pd = g_netPool.AllocBuffer(static_cast<size_t>(size));
            od = OriginalAt<FPtrSize>(O_ALLOC_BUFFER)(OPool(), static_cast<size_t>(size));
            MatchData(pd, od, size, "the buffer allocated");
        }
        p->m_size = size, p->m_data = pd;
        OF<int>(o, 0xC) = size, OF<void *>(o, 0x1C) = od;
        g_record = true;
        delete p;
        OriginalAt<FVI>(Slot(o, PK_DTOR))(o, 1);
        g_record = false;
        Check(Deleted(0, p) && Deleted(1, o), "the packet deleted");
        Unpair(p, o);
        if (size > 1000)
            Check(Deleted(0, pd) == Deleted(1, od), "its big buffer freed");
        CompareAll();
        return;
    }
    g_op = "~C_NetPool";
    g_trace += " PL";
    alignas(16) uint8_t store[sizeof(C_NetPool)];
    alignas(16) uint8_t o[0x48];
    C_NetPool *pp = new (store) C_NetPool();
    OriginalAt<FPtr>(O_POOL_CTOR)(o);
    // Its packets and buffers: some taken (past the first 16 now and then: it grows) and given back.
    std::vector<std::pair<void *, void *>> packets, buffers;
    for (int i = I(0, 40); i > 0; --i)
        packets.push_back({pp->AllocPacket(), OriginalAt<FPtr>(O_ALLOC_PACKET)(o)});
    for (int i = I(0, 40); i > 0; --i)
        buffers.push_back({pp->AllocBuffer(100), OriginalAt<FPtrSize>(O_ALLOC_BUFFER)(o, 100)});
    for (auto &x : packets) {
        pp->FreePacket(static_cast<C_NetPacket *>(x.first));
        OriginalAt<FVP>(O_FREE_PACKET)(o, x.second);
    }
    for (auto &x : buffers) {
        pp->FreeBuffer(x.first, 100);
        OriginalAt<FVPI>(O_FREE_BUFFER)(o, x.second, 100);
    }
    // Every item each holds, in order.
    std::vector<std::pair<void *, void *>> items;
    for (int list = 0; list < 2; ++list) {
        const S_NetFreeList &fl = list ? pp->m_packets : pp->m_buffers;
        const uint8_t *node = OF<const uint8_t *>(o, list * 0x18 + 4);
        Check(fl.count == OF<int>(o, list * 0x18 + 0x10), "a pool's count");
        for (size_t i = fl.items.size(); i > 0 && node; --i, node = OF<const uint8_t *>(node, 4))
            items.push_back({fl.items[i - 1], OF<void *>(node, 0)});
    }
    g_record = true;
    pp->~C_NetPool();
    OriginalAt<FV>(O_POOL_DTOR)(o);
    g_record = false;
    int missing = 0;
    for (auto &x : items)
        missing += !Deleted(0, x.first) || !Deleted(1, x.second);
    Check(missing == 0, "a pool's items deleted: " + N(missing) + " not");
}

void OpResendTime()
{
    if (!g_ch.kind)
        return;
    g_op = "resend time";
    const int t = P(0.5) ? 200 : I(1, 3000);
    g_ch.rel()->m_resendTime = t;
    OF<int>(g_ch.o, 0x2C) = t;
}

void Round()
{
    g_trace.clear();
    g_ch.kind = I(0, 2);
    std::memset(g_ch.o, 0xEE, sizeof g_ch.o);
    switch (g_ch.kind) {
    case 0: g_ch.p = new (g_ch.pStore) C_NetChannel(); break;
    case 1: g_ch.p = new (g_ch.pStore) C_NetReliableChannel(); break;
    default: g_ch.p = new (g_ch.pStore) C_NetOrderedChannel(); break;
    }
    OriginalAt<FPtr>(O_CHAN_CTOR[g_ch.kind])(g_ch.o);
    g_op = "construct";
    Check(OF<uint32_t>(g_ch.o, 0) == O_CHAN_VT[g_ch.kind], "vtable " + Hex(OF<uint32_t>(g_ch.o, 0)));
    if (P(0.3)) {
        // Numbers near the wrap.
        const uint32_t s = 0x7FFFFF - static_cast<uint32_t>(I(0, 6));
        g_ch.p->m_sent = g_ch.p->m_received = s;
        OF<uint32_t>(g_ch.o, 4) = OF<uint32_t>(g_ch.o, 8) = s;
    }
    CompareAll();
    NewStreams();
    const int ops = I(1, 60);
    for (int k = 0; k < ops; ++k) {
        const int op = I(0, 99);
        if (op < 14)
            OpNewPacket();
        else if (op < 26)
            OpSend();
        else if (op < 34)
            OpPop(true);
        else if (op < 48)
            OpReceive();
        else if (op < 56)
            OpPop(false);
        else if (op < 62)
            OpAck();
        else if (op < 68)
            OpResend();
        else if (op < 74)
            OpAcknowledge();
        else if (op < 82)
            OpRefs();
        else if (op < 86)
            OpPacketIO();
        else if (op < 88)
            OpClearPacket();
        else if (op < 90)
            OpPool();
        else if (op < 91)
            OpUnwrap();
        else if (op < 92)
            OpDestructors();
        else if (op < 98)
            OpMessages();
        else
            OpResendTime();
    }
    // The end: the channel destroyed (its packets to send and held released; received ones forgotten, as the
    // original), then the test's references released.
    g_op = "destroy";
    g_trace += " ~";
    g_ch.p->~C_NetChannel();
    OriginalAt<FVI>(Slot(g_ch.o, CH_DTOR))(g_ch.o, 0);
    CompareHeld();
    ComparePools();
    while (!g_held.empty()) {
        const S_Held h = g_held.back();
        g_held.pop_back();
        h.p->Release();
        OriginalAt<FV>(Slot(h.o, PK_RELEASE))(h.o);
    }
    ComparePools();
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s (ops:%s)\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, g_round, g_op, g_trace.c_str());
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
    if (std::getenv("HD2_CHAN_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("chan_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    RedirectOriginal(O_NEW, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(O_DELETE, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(O_MALLOC, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(O_FREE, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(O_CLOCK, reinterpret_cast<const void *>(OClock));
    // The original's pool (its static constructor never ran).
    OriginalAt<FPtr>(O_POOL_CTOR)(OPool());
    g_op = "the pools at first";
    ComparePools();
    int rounds = std::getenv("HD2_CHAN_QUICK") ? 8000 : 30000;
    if (const char *r = std::getenv("HD2_CHAN_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_CHAN_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("chan_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
