// Net part 1: the bit streams and the network messages' encodings (net/bitstream.cpp) against the original executable's.
// MinGW; build_bits.bat; runs bits_diff.exe ("bits_diff: N checks, 0 failures"). Each round makes a pair of streams (a
// fixed buffer's C_BitStream, or a growing C_NetMessage with its sender and recipient), runs a random sequence of
// calls on both and compares after every call: the stream's fields, its buffer, the results, the allocations (the
// original's malloc / realloc / free redirected, the port's wrapped with --wrap: both log to their own list and fill
// new memory alike). The quantisations come from Setup on both sides (compared), the player table is one table set in
// both programs' globals. HD2_BITS_ROUNDS: rounds (default 200,000, about 10 s; HD2_BITS_QUICK: 40,000); HD2_BITS_SEED;
// HD2_BITS_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "ls3d/math.h"
#include "net/bitstream.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

namespace {

// ---- the original's functions ----
using FWriteBits = void(__attribute__((thiscall)) *)(void *, uint32_t, int);
using FReadBits = uint32_t(__attribute__((thiscall)) *)(void *, int);
using FWriteString = void(__attribute__((thiscall)) *)(void *, const char *);
using FReadString = void(__attribute__((thiscall)) *)(void *, char *, int);
using FReserve = bool(__attribute__((thiscall)) *)(void *, uint32_t);
using FCtorFixed = void *(__attribute__((thiscall)) *)(void *, uint32_t *, uint32_t);
using FCtor = void *(__attribute__((thiscall)) *)(void *);
using FDtor = void(__attribute__((thiscall)) *)(void *);
using FPlayer = void *(__attribute__((thiscall)) *)(void *, int);
using FWriteQ = bool(__attribute__((thiscall)) *)(void *, const float *, const void *, int);
using FReadQ = void(__attribute__((thiscall)) *)(void *, float *, const void *, uint32_t);
using FWriteDir = void(__attribute__((thiscall)) *)(void *, const void *, int);
using FReadDir = void(__attribute__((thiscall)) *)(void *, void *, int);
using FWriteAxis = void(__attribute__((thiscall)) *)(void *, const void *, float, int);
using FReadAxis = void(__attribute__((thiscall)) *)(void *, void *, float *, int);
using FSetup = void(__attribute__((fastcall)) *)(void *, const void *, const void *, const void *, int);
using FTest = bool(__attribute__((thiscall)) *)(const void *, const float *);

const uint32_t O_WRITE_BITS = 0x0073d320, O_READ_BITS = 0x0073d3d0, O_WRITE_STRING = 0x0073d440,
               O_READ_STRING = 0x0073d470, O_CTOR_FIXED = 0x0073d290, O_CTOR_BASE = 0x0073d300,
               O_CTOR_GROWING = 0x0073d650, O_CTOR_MESSAGE = 0x0073d740, O_DTOR_GROWING = 0x0073d690,
               O_SENDER = 0x0073d770, O_RECIPIENT = 0x0073d7c0, O_WRITE_Q = 0x0073d810, O_READ_Q = 0x0073d900,
               O_WRITE_AXIS = 0x0073d9e0, O_READ_AXIS = 0x0073da30, O_WRITE_DIR3 = 0x0073da90,
               O_READ_DIR3 = 0x0073dae0, O_WRITE_DIR = 0x0073db30, O_READ_DIR = 0x0073db70, O_SETUP = 0x0073dbb0,
               O_CONTAINS = 0x0073dd30, O_WITHIN = 0x0073dda0;
const uint32_t O_TABLE = 0x00982dec, O_COUNT = 0x00982df0;
const uint32_t O_MALLOC = 0x007e8627, O_FREE = 0x007e8761, O_REALLOC = 0x007e884a;
const uint32_t O_VT_BASE = 0x0081640c, O_VT_GROWING = 0x00816414, O_VT_MESSAGE = 0x00816378;

template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }

// ---- the checks ----
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
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::string F(float f)
{
    char b[48];
    std::snprintf(b, sizeof b, "%.9g (%08x)", f, Bits(f));
    return b;
}

// ---- random ----
std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
uint32_t U32() { return static_cast<uint32_t>(rng()); }
float R(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

// ---- the allocations: logged per side, new memory filled alike ----
struct S_Alloc {
    char kind;         // 'm' malloc, 'r' realloc, 'f' free
    size_t size;
    void *old, *ptr;
};
std::vector<S_Alloc> g_allocs[2];
int g_allocSide = -1;   // 0 port, 1 original; -1: not logged (the test's own)

}   // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__real_realloc(void *, size_t);

namespace {

// Each block has 64 bytes of slack: a stream whose capacity is not a multiple of 32 bits writes its last word past
// the block (the original's flaw, the port's too: Reserve's realloc of capacity / 8 bytes). The sizes asked for are
// kept here (the slack not counted).
const uint8_t FILL = 0xA5;
const size_t SLACK = 64;
std::map<void *, size_t> g_sizes;
bool g_busy;   // inside the logger (the map's own allocations not logged)
struct S_Busy {
    S_Busy() { g_busy = true; }
    ~S_Busy() { g_busy = false; }
};
size_t SizeOf(const void *p)
{
    auto it = g_sizes.find(const_cast<void *>(p));
    return it == g_sizes.end() ? 0 : it->second;
}
void *Malloc(int side, size_t n)
{
    S_Busy busy;
    void *p = __real_malloc(n + SLACK);
    if (p) {
        std::memset(p, FILL, n + SLACK);
        g_sizes[p] = n;
    }
    if (side >= 0)
        g_allocs[side].push_back({'m', n, nullptr, p});
    return p;
}
void Free(int side, void *p)
{
    S_Busy busy;
    if (side >= 0)
        g_allocs[side].push_back({'f', 0, p, nullptr});
    g_sizes.erase(p);
    __real_free(p);
}
void *Realloc(int side, void *old, size_t n)
{
    S_Busy busy;
    // A new block every time (filled, the old bytes copied): the same on both sides.
    void *p = __real_malloc(n + SLACK);
    if (p) {
        std::memset(p, FILL, n + SLACK);
        if (old) {
            const size_t had = SizeOf(old);
            std::memcpy(p, old, had < n ? had : n);
        }
        g_sizes[p] = n;
    }
    if (old) {
        g_sizes.erase(old);
        __real_free(old);
    }
    if (side >= 0)
        g_allocs[side].push_back({'r', n, old, p});
    return p;
}

void *OMalloc(size_t n) { return Malloc(1, n); }
void OFree(void *p) { Free(1, p); }
void *ORealloc(void *p, size_t n) { return Realloc(1, p, n); }

}   // namespace

extern "C" void *__wrap_malloc(size_t n) { return g_allocSide == 0 && !g_busy ? Malloc(0, n) : __real_malloc(n); }
extern "C" void __wrap_free(void *p)
{
    if (g_allocSide == 0 && !g_busy)
        Free(0, p);
    else
        __real_free(p);
}
extern "C" void *__wrap_realloc(void *p, size_t n) { return g_allocSide == 0 && !g_busy ? Realloc(0, p, n) : __real_realloc(p, n); }

namespace {

// ---- the player table (one, in both programs' globals) ----
alignas(16) uint8_t g_table[6][0x80];

// ---- a pair of streams ----
const int WORDS = 256;
const uint32_t LIMIT_BITS = 200 * 32;   // no call starts past this (the buffers have room beyond)

struct S_Pair {
    bool growing = false;
    bool attached = false;   // a growing message over the test's buffer (never grown)
    alignas(16) uint8_t o[0x40];
    alignas(16) uint8_t pStore[sizeof(C_NetMessage)];
    C_BitStream *p = nullptr;
    alignas(16) uint32_t pBuf[WORDS + 8];
    alignas(16) uint32_t oBuf[WORDS + 8];
    C_NetMessage *pm() { return static_cast<C_NetMessage *>(p); }
} g_pair;

uint32_t OWord(int at) { return OF<uint32_t>(g_pair.o, at); }
uint32_t OPosition() { return OWord(0x8) * 32 + OWord(0xC); }
uint32_t *OData() { return reinterpret_cast<uint32_t *>(OWord(0x4)); }

// The bytes the two streams' buffers hold (a growing stream's: the smaller block).
size_t BufferBytes(const uint32_t *pd, const uint32_t *od)
{
    if (!g_pair.growing)
        return sizeof g_pair.pBuf;
    if (!pd || !od)
        return 0;
    const size_t a = SizeOf(pd), b = SizeOf(od);
    return a < b ? a : b;
}

// The original wrote a sign-extended char (WriteString): stray ones above the stream's end, never read back (later
// writes clear their own fields). The port's buffer takes the original's bits there.
void SyncTail()
{
    uint8_t *pd = reinterpret_cast<uint8_t *>(const_cast<uint32_t *>(g_pair.p->Data()));
    uint8_t *od = reinterpret_cast<uint8_t *>(OData());
    const size_t n = BufferBytes(reinterpret_cast<uint32_t *>(pd), reinterpret_cast<uint32_t *>(od));
    const uint32_t pos = g_pair.p->BitPosition();
    for (size_t bit = pos; bit < n * 8; ++bit) {
        const uint8_t m = static_cast<uint8_t>(1u << (bit & 7));
        pd[bit >> 3] = static_cast<uint8_t>((pd[bit >> 3] & ~m) | (od[bit >> 3] & m));
    }
}

// The first block for a request over 800 bits: the original's is 100 bytes whatever the capacity (its overflow), the
// port's the capacity's words (kept: the port's fix).
bool BigFirstBlock(size_t orig, size_t port)
{
    return orig == 100 && g_pair.growing && port == (g_pair.p->m_capacityBits + 31) / 32 * 4 && g_pair.p->m_capacityBits > 800;
}

void CompareAllocs()
{
    const std::vector<S_Alloc> &pa = g_allocs[0], &oa = g_allocs[1];
    bool ok = pa.size() == oa.size();
    for (size_t i = 0; ok && i < pa.size(); ++i)
        ok = pa[i].kind == oa[i].kind && (pa[i].size == oa[i].size || (pa[i].kind == 'm' && BigFirstBlock(oa[i].size, pa[i].size)));
    std::string s;
    if (!ok) {
        s = "allocations:";
        for (const S_Alloc &a : oa)
            s += std::string(" ") + a.kind + std::to_string(a.size);
        s += " /";
        for (const S_Alloc &a : pa)
            s += std::string(" ") + a.kind + std::to_string(a.size);
    }
    Check(ok, s);
}

void CompareStreams()
{
    C_BitStream *p = g_pair.p;
    std::string s;
    if (p->m_wordIndex != OWord(0x8) || p->m_bitIndex != OWord(0xC) || p->m_capacityBits != OWord(0x10))
        s += "word " + std::to_string(OWord(0x8)) + " bit " + std::to_string(OWord(0xC)) + " capacity " +
             std::to_string(OWord(0x10)) + " / " + std::to_string(p->m_wordIndex) + " " + std::to_string(p->m_bitIndex) +
             " " + std::to_string(p->m_capacityBits) + "; ";
    const uint32_t *pd = p->Data(), *od = OData();
    if (g_pair.growing) {
        C_NetMessage *m = g_pair.pm();
        if ((pd == nullptr) != (od == nullptr))
            s += "data null / not; ";
        if (m->m_ownsBuffer != (OF<uint8_t>(g_pair.o, 0x14) != 0))
            s += "owns " + std::to_string(OF<uint8_t>(g_pair.o, 0x14)) + " / " + std::to_string(m->m_ownsBuffer) + "; ";
        if (m->m_sender != OF<void *>(g_pair.o, 0x2C) || m->m_recipient != OF<void *>(g_pair.o, 0x30) ||
            m->m_senderIndex != OF<int>(g_pair.o, 0x34) || m->m_recipientIndex != OF<int>(g_pair.o, 0x38))
            s += "players; ";
        if (pd && od && SizeOf(pd) != SizeOf(od) && !BigFirstBlock(SizeOf(od), SizeOf(pd)))
            s += "block " + std::to_string(SizeOf(od)) + " / " +
                 std::to_string(SizeOf(pd)) + "; ";
    } else if (pd != g_pair.pBuf || od != g_pair.oBuf) {
        s += "data moved; ";
    }
    const size_t n = BufferBytes(pd, od);
    if (n && std::memcmp(pd, od, n) != 0) {
        size_t i = 0;
        while (reinterpret_cast<const uint8_t *>(pd)[i] == reinterpret_cast<const uint8_t *>(od)[i])
            ++i;
        s += "buffer differs at byte " + std::to_string(i) + ": " + Hex(od[i / 4]) + " / " + Hex(pd[i / 4]) + "; ";
    }
    Check(s.empty(), s);
    if (!s.empty()) {
        // Carry on from the original's state (one difference reported once).
        if (n)
            std::memcpy(const_cast<uint32_t *>(pd), od, n);
        p->m_wordIndex = OWord(0x8);
        p->m_bitIndex = OWord(0xC);
    }
    CompareAllocs();
}

// Runs `port` then `orig` with each side's allocations logged.
template <class FP, class FO> void Both(FP port, FO orig)
{
    g_allocs[0].clear();
    g_allocs[1].clear();
    g_allocSide = 0;
    port();
    g_allocSide = 1;
    orig();
    g_allocSide = -1;
}

// ---- the quantisations ----
struct S_QCase {
    S_vector4 span, from, step;
    int unknown;
};

S_QCase RandomQ(bool usable)
{
    S_QCase c{};
    float *sp = &c.span.x, *fr = &c.from.x, *st = &c.step.x;
    for (int i = 0; i < 4; ++i) {
        float span;
        switch (I(0, 4)) {
        case 0: span = R(0.5f, 5000.0f); break;
        case 1: span = std::ldexp(1.0f, I(-2, 13)); break;
        case 2: span = 6.2831855f; break;
        case 3: span = 3.1415927f; break;
        default: span = R(0.01f, 50.0f); break;
        }
        int bits = usable ? I(1, 30) : I(-3, 34);
        if (usable && P(0.5))
            bits = I(4, 20);
        float k;
        switch (I(0, 3)) {
        case 0: k = std::ldexp(1.0f, bits - 1); break;                                   // an exact power of two
        case 1: k = std::ldexp(1.0f, bits - 1) * R(1.0f, 2.0f); break;
        case 2: k = std::nextafter(std::ldexp(1.0f, bits), 0.0f); break;                  // just below the next
        default: k = std::nextafter(std::ldexp(1.0f, bits - 1), 1e30f); break;            // just above
        }
        float step = span / k;
        if (step == 0.0f || !std::isfinite(step))
            step = span;
        sp[i] = span;
        st[i] = step;
        fr[i] = P(0.3) ? -span * 0.5f : (P(0.5) ? 0.0f : R(-5000.0f, 5000.0f));
    }
    c.unknown = P(0.5) ? 100 : static_cast<int>(U32());
    return c;
}

void SetupBoth(S_QCase &c, S_NetQuantization &pq, uint8_t *oq)
{
    std::memset(&pq, 0x5A, sizeof pq);
    std::memset(oq, 0x5A, 0x80);
    pq.Setup(c.span, c.from, c.step, c.unknown);
    OriginalAt<FSetup>(O_SETUP)(oq, &c.span, &c.from, &c.step, c.unknown);
}

bool Usable(const S_NetQuantization &q)
{
    for (int i = 0; i < 3; ++i)
        if (q.bits[i] < 1 || q.bits[i] > 32 || !(q.scale[i] > 0.0f) || !std::isfinite(q.scale[i]))
            return false;
    return true;
}

// Setup on both sides, compared; the port's then in the table entry or `out`.
bool MakeQ(uint8_t *out, bool usable)
{
    for (int tries = 0; tries < 20; ++tries) {
        S_QCase c = RandomQ(usable);
        alignas(16) S_NetQuantization pq;
        alignas(16) uint8_t oq[0x80];
        SetupBoth(c, pq, oq);
        // A component past 32 bits or below 1: the original's scale reads its mask outside the table (0x00816388 +
        // bits x 4: the data next to it); the port's mask is all ones or none. Not compared.
        for (int i = 0; i < 3; ++i)
            if (pq.bits[i] < 0 || pq.bits[i] > 32)
                OF<float>(oq, 0x60 + 4 * i) = pq.scale[i];
        if (std::memcmp(&pq, oq, 0x80) != 0) {
            std::string s = "Setup: span " + F(c.span.x) + " " + F(c.span.y) + " " + F(c.span.z) + " step " + F(c.step.x) +
                            " " + F(c.step.y) + " " + F(c.step.z) + ":";
            for (int i = 0; i < 0x80; i += 4)
                if (OF<uint32_t>(oq, i) != OF<uint32_t>(&pq, i))
                    s += " +" + Hex(i).substr(6) + " " + Hex(OF<uint32_t>(oq, i)) + "/" + Hex(OF<uint32_t>(&pq, i));
            g_op = "Setup";
            Fail(s);
        } else {
            ++checks;
        }
        if (!usable || Usable(pq)) {
            std::memcpy(out, &pq, 0x80);
            return true;
        }
    }
    return false;
}

// ---- the values ----
float RandomValue(const S_NetQuantization &q, int i)
{
    switch (I(0, 12)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return q.deadZone[i] * (P(0.5) ? 1 : -1);
    case 3: return std::nextafter(q.deadZone[i], 0.0f) * (P(0.5) ? 1 : -1);
    case 4: return std::nextafter(q.deadZone[i], 1e30f) * (P(0.5) ? 1 : -1);
    case 5: return q.min[i];
    case 6: return q.min[i] + q.range[i];
    case 7: return std::nextafter(q.min[i], -1e30f);
    case 8: return q.min[i] + q.range[i] * R(-0.2f, 1.2f);
    case 9: return P(0.5) ? std::nanf("") : (P(0.5) ? 3e9f : -3e9f) / (q.scale[i] > 1 ? q.scale[i] : 1.0f);
    case 10: return R(-1.0f, 1.0f) * q.deadZone[i] * 3;
    default: return q.min[i] + q.range[i] * R(0.0f, 1.0f);
    }
}

S_vector RandomDir()
{
    switch (I(0, 9)) {
    case 0: return S_vector(0, P(0.5) ? 1.0f : -1.0f, 0);
    case 1: return S_vector(0, 0, 0);
    case 2: return S_vector(P(0.5) ? 1.0f : -1.0f, 0, 0);
    case 3: return S_vector(0, 0, P(0.5) ? 1.0f : -1.0f);
    case 4: return S_vector(-1.0f, 0, P(0.5) ? 0.0f : -0.0f);
    default: {
        std::normal_distribution<float> n;
        float x = n(rng), y = n(rng), z = n(rng);
        const float l = std::sqrt(x * x + y * y + z * z);
        if (l == 0)
            return S_vector(1, 0, 0);
        y = y / l;
        if (y > 1.0f)
            y = 1.0f;
        if (y < -1.0f)
            y = -1.0f;
        return S_vector(x / l, y, z / l);
    }
    }
}

// ---- the round ----
void SetTable()
{
    g_netPlayerCount = I(0, 5);
    for (int i = 0; i < g_netPlayerCount; ++i)
        if (!MakeQ(g_table[i], true))
            g_netPlayerCount = i;
    g_netPlayerTable = g_netPlayerCount || P(0.5) ? reinterpret_cast<S_NetPlayer *>(g_table) : nullptr;
    if (!g_netPlayerTable)
        g_netPlayerCount = 0;
    OF<void *>(reinterpret_cast<void *>(O_TABLE), 0) = g_netPlayerTable;
    OF<int>(reinterpret_cast<void *>(O_COUNT), 0) = g_netPlayerCount;
}

void NewPair()
{
    S_Pair &pr = g_pair;
    pr.growing = P(0.6);
    pr.attached = false;
    for (int i = 0; i < WORDS + 8; ++i)
        pr.pBuf[i] = pr.oBuf[i] = P(0.3) ? 0 : U32();
    std::memset(pr.o, 0xEE, sizeof pr.o);
    std::memset(pr.pStore, 0xEE, sizeof pr.pStore);
    if (pr.growing) {
        pr.p = new (pr.pStore) C_NetMessage();
        OriginalAt<FCtor>(O_CTOR_MESSAGE)(pr.o);
        C_NetMessage *m = pr.pm();
        // The players: by pointer (a table entry, or the second quantisation), by index.
        void *ptrs[3] = {nullptr, g_table[0], g_table[5]};
        m->m_sender = static_cast<S_NetPlayer *>(P(0.5) ? nullptr : ptrs[I(0, 2)]);
        m->m_recipient = static_cast<S_NetPlayer *>(P(0.5) ? nullptr : ptrs[I(0, 2)]);
        m->m_senderIndex = I(-1, 6);
        m->m_recipientIndex = I(-1, 6);
        OF<void *>(pr.o, 0x2C) = m->m_sender;
        OF<void *>(pr.o, 0x30) = m->m_recipient;
        OF<int>(pr.o, 0x34) = m->m_senderIndex;
        OF<int>(pr.o, 0x38) = m->m_recipientIndex;
        // The message's own fields (+0x18..+0x28): the original's constructor leaves them.
        if (P(0.1)) {
            // Over a buffer it does not own (C_NetGame's queued datagrams): the destructor leaves it.
            const uint32_t cap = static_cast<uint32_t>(I(64, WORDS * 32));
            m->Attach(pr.pBuf, cap);
            OF<uint32_t *>(pr.o, 4) = pr.oBuf;
            OF<uint32_t>(pr.o, 0x10) = cap;
            g_sizes[pr.pBuf] = g_sizes[pr.oBuf] = sizeof pr.pBuf;
            pr.attached = true;
        }
    } else {
        const uint32_t cap = P(0.2) ? 0 : static_cast<uint32_t>(I(0, WORDS * 32));
        if (P(0.5)) {
            pr.p = new (pr.pStore) C_BitStream(pr.pBuf, cap);
            OriginalAt<FCtorFixed>(O_CTOR_FIXED)(pr.o, pr.oBuf, cap);
        } else {
            // The default constructor, then attached (C_NetGame's queued datagrams).
            pr.p = new (pr.pStore) C_BitStream();
            OriginalAt<FCtor>(O_CTOR_BASE)(pr.o);
            Check(pr.p->m_data == nullptr && pr.p->m_capacityBits == 0 && pr.p->m_wordIndex == 0 &&
                      pr.p->m_bitIndex == 0 && OWord(4) == 0 && OWord(8) == 0 && OWord(0xC) == 0 && OWord(0x10) == 0 &&
                      OWord(0) == O_VT_BASE,
                  "default constructor");
            pr.p->Attach(pr.pBuf, cap);
            OF<uint32_t *>(pr.o, 4) = pr.oBuf;
            OF<uint32_t>(pr.o, 0x10) = cap;
        }
    }
    Check(OWord(0) == (pr.growing ? O_VT_MESSAGE : O_VT_BASE), "vtable " + Hex(OWord(0)));
    g_op = "construct";
    CompareStreams();
}

void EndPair()
{
    g_op = "destroy";
    if (g_pair.growing) {
        const bool owned = g_pair.pm()->m_ownsBuffer && g_pair.p->Data();
        Both([] { g_pair.p->~C_BitStream(); }, [] { OriginalAt<FDtor>(O_DTOR_GROWING)(g_pair.o); });
        Check(OWord(0) == O_VT_BASE, "vtable after the destructor " + Hex(OWord(0)));
        CompareAllocs();
        Check(g_allocs[0].size() == (owned ? 1u : 0u), "the port's frees");
    } else {
        g_pair.p->~C_BitStream();
    }
}

bool Readable(uint32_t bits)
{
    // A read must stay inside both buffers (a growing stream's block; the next word read too).
    if (!g_pair.p->Data() || !OData())
        return false;
    const size_t n = BufferBytes(g_pair.p->Data(), OData());
    return (g_pair.p->BitPosition() + bits) / 32 + 2 <= n / 4;
}
bool Writable(uint32_t bits)
{
    if (g_pair.p->BitPosition() + bits > LIMIT_BITS)
        return false;
    if (!g_pair.growing)
        return true;
    // A growing stream grows first: only what fits once grown. The original's first block is 100 bytes, whatever the
    // capacity it records (a request over 800 bits overflows it): none bigger without a buffer.
    if (!g_pair.p->Data())
        return bits <= 768;
    if (g_pair.attached || SizeOf(OData()) * 8 < g_pair.p->CapacityBits())
        // Not past the buffer (an attached one cannot grow; the original's 100-byte first block under a bigger capacity
        // keeps only its 100 bytes when it grows, the rest written past it lost).
        return g_pair.p->BitPosition() + bits + 32 <= std::min<uint32_t>(g_pair.p->CapacityBits(), SizeOf(OData()) * 8);
    return true;
}

void OpWriteBits()
{
    g_op = "WriteBits";
    int n = P(0.1) ? 32 : I(0, 32);
    if (P(0.05))
        n = 0;
    uint32_t v = n == 32 ? U32() : (U32() & ((1u << n) - 1));
    if (P(0.1))
        v = U32();   // out of range: not masked
    if (!Writable(32))
        return;
    g_trace += " W" + std::to_string(n);
    Both([&] { g_pair.p->WriteBits(v, n); }, [&] { OriginalAt<FWriteBits>(O_WRITE_BITS)(g_pair.o, v, n); });
    CompareStreams();
}

void OpReadBits()
{
    g_op = "ReadBits";
    const int n = P(0.1) ? 32 : I(0, 32);
    if (!Readable(32))
        return;
    g_trace += " R" + std::to_string(n);
    uint32_t a = 0, b = 0;
    Both([&] { a = g_pair.p->ReadBits(n); }, [&] { b = OriginalAt<FReadBits>(O_READ_BITS)(g_pair.o, n); });
    Check(a == b, "read " + Hex(b) + " / " + Hex(a));
    CompareStreams();
}

void OpWriteString()
{
    g_op = "WriteString";
    char text[64];
    const int len = P(0.1) ? 0 : I(0, 40);
    const bool high = P(0.3);
    for (int i = 0; i < len; ++i)
        text[i] = static_cast<char>(high ? I(1, 255) : I(32, 126));
    text[len] = 0;
    if (!Writable((len + 1) * 8 + 32))
        return;
    g_trace += " WS" + std::to_string(len);
    Both([&] { g_pair.p->WriteString(text); }, [&] { OriginalAt<FWriteString>(O_WRITE_STRING)(g_pair.o, text); });
    SyncTail();
    CompareStreams();
}

void OpReadString()
{
    g_op = "ReadString";
    const int size = P(0.2) ? I(1, 3) : I(1, 48);
    if (!Readable(size * 8 + 32))
        return;
    g_trace += " RS" + std::to_string(size);
    char a[64], b[64];
    std::memset(a, 0x77, sizeof a);
    std::memset(b, 0x77, sizeof b);
    Both([&] { g_pair.p->ReadString(a, size); }, [&] { OriginalAt<FReadString>(O_READ_STRING)(g_pair.o, b, size); });
    Check(std::memcmp(a, b, sizeof a) == 0, "string \"" + std::string(b) + "\" / \"" + std::string(a) + "\"");
    CompareStreams();
}

void OpPosition()
{
    g_op = "SetBitPosition";
    const uint32_t top = g_pair.growing ? (g_pair.p->Data() ? g_pair.p->CapacityBits() : 0) : LIMIT_BITS;
    const uint32_t bits = P(0.3) ? 0 : static_cast<uint32_t>(I(0, static_cast<int>(top)));
    g_trace += " P" + std::to_string(bits);
    g_pair.p->SetBitPosition(bits);
    OF<uint32_t>(g_pair.o, 8) = bits >> 5;
    OF<uint32_t>(g_pair.o, 0xC) = bits & 31;
    if (P(0.2)) {
        g_pair.p->ResetPosition();
        OF<uint32_t>(g_pair.o, 8) = OF<uint32_t>(g_pair.o, 0xC) = 0;
    }
    Check(g_pair.p->ByteSize() == static_cast<int>((OPosition() + 7) >> 3), "ByteSize");
    Check(g_pair.p->IsValid() == (OPosition() <= OWord(0x10)), "IsValid");
    CompareStreams();
}

void OpReserve()
{
    g_op = "Reserve";
    uint32_t bits = static_cast<uint32_t>(P(0.5) ? I(0, 64) : I(0, 3000));
    if (P(0.15))
        bits = static_cast<uint32_t>(I(760, 810));
    // (Over 800 bits without a buffer: the original's block is 100 bytes, the capacity the request's; its writes past the
    // block stay in the harness's slack: up to 900 bits.)
    if (g_pair.growing && !g_pair.p->Data() && bits > 900)
        bits = static_cast<uint32_t>(I(795, 900));
    if (g_pair.growing && g_pair.p->BitPosition() + bits > 4 * LIMIT_BITS)
        return;
    if (g_pair.attached && g_pair.p->BitPosition() + bits >= g_pair.p->CapacityBits())
        return;
    g_trace += " V" + std::to_string(bits);
    bool a = false, b = false;
    const uint32_t slot0 = OF<uint32_t>(reinterpret_cast<void *>(OWord(0)), 0);
    Both([&] { a = g_pair.p->Reserve(bits); }, [&] { b = OriginalAt<FReserve>(slot0)(g_pair.o, bits); });
    Check(a == b, "Reserve " + std::to_string(b) + " / " + std::to_string(a));
    CompareStreams();
}

uint8_t *PickQ()
{
    // A table entry, or the spare (index 5).
    if (g_netPlayerCount && P(0.6))
        return g_table[I(0, g_netPlayerCount - 1)];
    return g_table[5];
}

void OpWriteQ()
{
    g_op = "WriteQuantized";
    if (!Writable(100))
        return;
    const uint8_t *qb = PickQ();
    const S_NetQuantization &q = *reinterpret_cast<const S_NetQuantization *>(qb);
    float v[4];
    for (int i = 0; i < 3; ++i)
        v[i] = RandomValue(q, i);
    v[3] = 0;
    const int mask = P(0.5) ? 7 : I(0, 255);
    g_trace += " WQ" + std::to_string(mask);
    bool a = false, b = false;
    Both([&] { a = g_pair.pm()->WriteQuantized(v, &q, static_cast<uint8_t>(mask)); },
         [&] { b = OriginalAt<FWriteQ>(O_WRITE_Q)(g_pair.o, v, qb, mask); });
    Check(a == b, "result " + std::to_string(b) + " / " + std::to_string(a) + " for " + F(v[0]) + " " + F(v[1]) + " " +
                      F(v[2]));
    CompareStreams();
}

void OpReadQ()
{
    g_op = "ReadQuantized";
    if (!Readable(100))
        return;
    const uint8_t *qb = PickQ();
    const S_NetQuantization &q = *reinterpret_cast<const S_NetQuantization *>(qb);
    const uint32_t mask = P(0.5) ? 7u : U32();
    g_trace += " RQ" + std::to_string(mask & 7);
    float a[4] = {9, 9, 9, 9}, b[4] = {9, 9, 9, 9};
    Both([&] { g_pair.pm()->ReadQuantized(a, &q, mask); },
         [&] { OriginalAt<FReadQ>(O_READ_Q)(g_pair.o, b, qb, mask); });
    Check(std::memcmp(a, b, sizeof a) == 0, "values " + F(b[0]) + " " + F(b[1]) + " " + F(b[2]) + " / " + F(a[0]) + " " +
                                                F(a[1]) + " " + F(a[2]));
    CompareStreams();
}

void OpPlayers()
{
    g_op = "GetSender/GetRecipient";
    const int i = I(-2, 7);
    void *a = g_pair.pm()->GetSender(i), *b = OriginalAt<FPlayer>(O_SENDER)(g_pair.o, i);
    Check(a == b, "sender " + std::to_string(i));
    a = g_pair.pm()->GetRecipient(i);
    b = OriginalAt<FPlayer>(O_RECIPIENT)(g_pair.o, i);
    Check(a == b, "recipient " + std::to_string(i));
}

// A player index whose quantisation is usable (GetRecipient's or GetSender's answer not null).
bool PlayerFor(bool sender, int &index)
{
    for (int tries = 0; tries < 8; ++tries) {
        index = I(-2, 6);
        const void *q = sender ? g_pair.pm()->GetSender(index) : g_pair.pm()->GetRecipient(index);
        if (q && Usable(*static_cast<const S_NetQuantization *>(q)))
            return true;
    }
    return false;
}

void OpDirections()
{
    int index;
    const int kind = I(0, 2);
    if (!PlayerFor(false, index))
        return;
    const bool write = P(0.5);
    if (write ? !Writable(100) : !Readable(100))
        return;
    alignas(16) float pv[4] = {7, 7, 7, 7}, ov[4] = {7, 7, 7, 7};
    float pa = 5, oa = 5;
    S_vector dir = RandomDir();
    const float angle = P(0.2) ? 0.0f : R(-7.0f, 7.0f);
    alignas(16) float din[4] = {dir.x, dir.y, dir.z, 0};
    static const char *names[2][3] = {{"ReadDirection", "ReadDirection3D", "ReadAxisAngle"},
                                      {"WriteDirection", "WriteDirection3D", "WriteAxisAngle"}};
    g_op = names[write][kind];
    g_trace += std::string(" ") + g_op;
    C_NetMessage *m = g_pair.pm();
    S_vector &pout = *reinterpret_cast<S_vector *>(pv);
    if (write) {
        switch (kind) {
        case 0: Both([&] { m->WriteDirection(dir, index); }, [&] { OriginalAt<FWriteDir>(O_WRITE_DIR)(g_pair.o, din, index); }); break;
        case 1: Both([&] { m->WriteDirection3D(dir, index); }, [&] { OriginalAt<FWriteDir>(O_WRITE_DIR3)(g_pair.o, din, index); }); break;
        default:
            Both([&] { m->WriteAxisAngle(dir, angle, index); },
                 [&] { OriginalAt<FWriteAxis>(O_WRITE_AXIS)(g_pair.o, din, angle, index); });
        }
        CompareStreams();
        return;
    }
    switch (kind) {
    case 0: Both([&] { m->ReadDirection(pout, index); }, [&] { OriginalAt<FReadDir>(O_READ_DIR)(g_pair.o, ov, index); }); break;
    case 1: Both([&] { m->ReadDirection3D(pout, index); }, [&] { OriginalAt<FReadDir>(O_READ_DIR3)(g_pair.o, ov, index); }); break;
    default:
        Both([&] { m->ReadAxisAngle(pout, pa, index); }, [&] { OriginalAt<FReadAxis>(O_READ_AXIS)(g_pair.o, ov, &oa, index); });
    }
    Check(std::memcmp(pv, ov, sizeof pv) == 0 && Bits(pa) == Bits(oa),
          "read " + F(ov[0]) + " " + F(ov[1]) + " " + F(ov[2]) + " " + F(oa) + " / " + F(pv[0]) + " " + F(pv[1]) + " " +
              F(pv[2]) + " " + F(pa));
    CompareStreams();
}

void OpPosition3()
{
    // WritePosition / ReadPosition (the port's; the original's callers inline them: WriteQuantized and ReadQuantized
    // with GetSender(-1), mask 7).
    int dummy;
    if (!PlayerFor(true, dummy) || !g_pair.pm()->GetSender(-1) ||
        !Usable(*reinterpret_cast<const S_NetQuantization *>(g_pair.pm()->GetSender(-1))))
        return;
    const void *q = g_pair.pm()->GetSender(-1);
    const S_NetQuantization &qq = *static_cast<const S_NetQuantization *>(q);
    if (P(0.5)) {
        if (!Writable(100))
            return;
        g_op = "WritePosition";
        g_trace += " WP";
        alignas(16) float v[4] = {RandomValue(qq, 0), RandomValue(qq, 1), RandomValue(qq, 2), 0};
        const S_vector pos(v[0], v[1], v[2]);
        Both([&] { g_pair.pm()->WritePosition(pos); }, [&] { OriginalAt<FWriteQ>(O_WRITE_Q)(g_pair.o, v, q, 7); });
    } else {
        if (!Readable(100))
            return;
        g_op = "ReadPosition";
        g_trace += " RP";
        S_vector pos(9, 9, 9);
        float o[4] = {9, 9, 9, 9};
        Both([&] { g_pair.pm()->ReadPosition(pos); }, [&] { OriginalAt<FReadQ>(O_READ_Q)(g_pair.o, o, q, 7); });
        Check(Bits(pos.x) == Bits(o[0]) && Bits(pos.y) == Bits(o[1]) && Bits(pos.z) == Bits(o[2]),
              "position " + F(o[0]) + " " + F(o[1]) + " " + F(o[2]) + " / " + F(pos.x) + " " + F(pos.y) + " " + F(pos.z));
    }
    CompareStreams();
}

void OpRanges()
{
    g_op = "Contains/WithinSteps";
    const uint8_t *qb = PickQ();
    const S_NetQuantization &q = *reinterpret_cast<const S_NetQuantization *>(qb);
    float v[4] = {RandomValue(q, 0), RandomValue(q, 1), RandomValue(q, 2), 0};
    if (P(0.3))
        for (int i = 0; i < 3; ++i)
            v[i] = q.offset[i] + q.range40[i] * (P(0.5) ? 1.0f : 0.0f);
    const bool a = q.Contains(v), b = OriginalAt<FTest>(O_CONTAINS)(qb, v);
    Check(a == b, "Contains " + std::to_string(b) + " / " + std::to_string(a) + " for " + F(v[0]) + " " + F(v[1]) + " " + F(v[2]));
    const bool c = q.WithinSteps(v), d = OriginalAt<FTest>(O_WITHIN)(qb, v);
    Check(c == d, "WithinSteps " + std::to_string(d) + " / " + std::to_string(c) + " for " + F(v[0]) + " " + F(v[1]) + " " + F(v[2]));
}

// The raw bytes (WriteRawBytes / ReadRawBytes): the original has them inline in its packets' Write (0x00745fa0) and
// Read (0x00746050), called here on a packet's fields (the pool's AllocBuffer faked); the port's side is its packet's
// Write / Read as net_channel.cpp has them (five header fields, then the bytes).
const uint32_t O_PACKET_WRITE = 0x00745fa0, O_PACKET_READ = 0x00746050, O_ALLOC_BUFFER = 0x00749a70;
using FPacket = void(__attribute__((thiscall)) *)(void *, void *);
uint8_t g_packetData[2][400];
void *__attribute__((thiscall)) FakeAllocBuffer(void *, size_t) { return g_packetData[1]; }

void OpPacket()
{
    if (P(0.5)) {
        g_op = "packet Write";
        const int size = P(0.1) ? 0 : I(1, 300);
        if (!Writable(48 + size * 8 + 64))
            return;
        g_trace += " PW" + std::to_string(size);
        uint32_t f[5] = {static_cast<uint32_t>(I(0, 3)), static_cast<uint32_t>(size), static_cast<uint32_t>(I(0, 63)),
                         U32() & 0x7FFFFF, static_cast<uint32_t>(I(0, 1))};
        for (int i = 0; i < size; ++i)
            g_packetData[0][i] = static_cast<uint8_t>(U32());
        alignas(4) uint8_t packet[0x20] = {};
        std::memcpy(packet + 8, f, sizeof f);
        OF<void *>(packet, 0x1C) = g_packetData[0];
        static const int widths[5] = {2, 16, 6, 23, 1};
        Both(
            [&] {
                for (int i = 0; i < 5; ++i)
                    g_pair.p->WriteBits(f[i], widths[i]);
                g_pair.p->WriteRawBytes(g_packetData[0], static_cast<uint32_t>(size));
            },
            [&] { OriginalAt<FPacket>(O_PACKET_WRITE)(packet, g_pair.o); });
        CompareStreams();
        return;
    }
    g_op = "packet Read";
    // The size the header holds (16 bits after 2, sign-extended): only what the buffers hold.
    if (!Readable(48 + 32))
        return;
    const uint32_t at = g_pair.p->BitPosition();
    g_pair.p->ReadBits(2);
    const int size = static_cast<int16_t>(g_pair.p->ReadBits(16));
    g_pair.p->SetBitPosition(at);
    if (size < 0 || size > 400 || !Readable(48 + size * 8 + 64))
        return;
    g_trace += " PR" + std::to_string(size);
    std::memset(g_packetData, 0x3C, sizeof g_packetData);
    uint32_t f[5];
    alignas(4) uint8_t packet[0x20] = {};
    static const int widths[5] = {2, 16, 6, 23, 1};
    Both(
        [&] {
            for (int i = 0; i < 5; ++i)
                f[i] = g_pair.p->ReadBits(widths[i]);
            g_pair.p->ReadRawBytes(g_packetData[0], static_cast<uint32_t>(size));
        },
        [&] { OriginalAt<FPacket>(O_PACKET_READ)(packet, g_pair.o); });
    Check(OF<uint32_t>(packet, 8) == f[0] && OF<int>(packet, 0xC) == size && OF<uint32_t>(packet, 0x10) == f[2] &&
              OF<uint32_t>(packet, 0x14) == f[3] && OF<uint32_t>(packet, 0x18) == f[4] &&
              OF<void *>(packet, 0x1C) == g_packetData[1],
          "packet header");
    Check(std::memcmp(g_packetData[0], g_packetData[1], sizeof g_packetData[0]) == 0, "packet data");
    CompareStreams();
}

void Round()
{
    g_trace.clear();
    SetTable();
    if (P(0.1)) {
        // Setup's corners (span below its step, past 32 bits): compared only.
        uint8_t q[0x80];
        MakeQ(q, false);
    }
    MakeQ(g_table[5], true);
    NewPair();
    const int ops = I(1, 30);
    for (int k = 0; k < ops; ++k) {
        const int op = I(0, 99);
        if (op < 22)
            OpWriteBits();
        else if (op < 36)
            OpReadBits();
        else if (op < 42)
            OpWriteString();
        else if (op < 47)
            OpReadString();
        else if (op < 53)
            OpPosition();
        else if (op < 58)
            OpReserve();
        else if (op < 62)
            OpRanges();
        else if (op < 66)
            OpPacket();
        else if (!g_pair.growing)
            OpWriteBits();
        else if (op < 74)
            OpWriteQ();
        else if (op < 82)
            OpReadQ();
        else if (op < 85)
            OpPlayers();
        else if (op < 95)
            OpDirections();
        else
            OpPosition3();
    }
    EndPair();
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
    if (std::getenv("HD2_BITS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("bits_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    RedirectOriginal(O_MALLOC, reinterpret_cast<const void *>(OMalloc));
    RedirectOriginal(O_FREE, reinterpret_cast<const void *>(OFree));
    RedirectOriginal(O_REALLOC, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(O_ALLOC_BUFFER, reinterpret_cast<const void *>(FakeAllocBuffer));
    std::printf("bits_diff: port C_NetMessage fields at +%02x +%02x +%02x +%02x (size %02x)\n",
                static_cast<int>(offsetof(C_NetMessage, m_ownsBuffer)), static_cast<int>(offsetof(C_NetMessage, m_unreliable)),
                static_cast<int>(offsetof(C_NetMessage, m_sender)), static_cast<int>(offsetof(C_NetMessage, m_recipientIndex)),
                static_cast<int>(sizeof(C_NetMessage)));
    int rounds = std::getenv("HD2_BITS_QUICK") ? 40000 : 200000;
    if (const char *r = std::getenv("HD2_BITS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_BITS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("bits_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
