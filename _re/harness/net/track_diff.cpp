// Net part 2: the replicated values (net/net_track.cpp: the track, the position track, the direction track) against the
// original executable's. MinGW; build_track.bat; runs track_diff.exe ("track_diff: N checks, 0 failures"). Each round
// makes a pair of tracks (the original's built by its constructor in a buffer of its layout) and a pair of messages
// over the test's buffers (part 1's bit streams, compared there), then runs a random sequence of calls on both: the
// setters, the getters, Update, Write (delta or not, with the player's last value sent), Read (from where an earlier
// Write began, or anywhere), Reset, the quantisations, the direction's SetDirection / SetAngle / GetAngle. After each
// call the tracks' bytes, the messages and the globals (the update period, the server side) are compared.
// HD2_TRACK_ROUNDS: rounds (default 100,000, about 5 s; HD2_TRACK_QUICK: 50,000); HD2_TRACK_SEED; HD2_TRACK_FIRST_CHANCE.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "ls3d/math.h"
#include "net/bitstream.h"
#include "net/net_track.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

namespace {

using FCtor = void *(__attribute__((thiscall)) *)(void *);
using FVoid = void(__attribute__((thiscall)) *)(void *);
using FQ = void(__attribute__((thiscall)) *)(void *, void *, void *, void *);
using FFloat = void(__attribute__((thiscall)) *)(void *, float);
using FUint = void(__attribute__((thiscall)) *)(void *, uint32_t);
using FVec = void(__attribute__((thiscall)) *)(void *, const void *);
using FGet = const void *(__attribute__((thiscall)) *)(void *);
using FGetFloat = float(__attribute__((thiscall)) *)(void *);
using FWrite = void(__attribute__((thiscall)) *)(void *, void *, bool, void *);
using FRead = void(__attribute__((thiscall)) *)(void *, void *);
using FSetDir = void(__attribute__((thiscall)) *)(void *, const void *, float);
using FMsgCtor = void *(__attribute__((thiscall)) *)(void *);

const uint32_t O_CTOR[3] = {0x0073fbe0, 0x00740770, 0x007407e0};
const uint32_t O_VTABLE[3] = {0x00816490, 0x008164e0, 0x00816530};
const uint32_t O_RESET = 0x0073fe50, O_DIR_RESET = 0x00740ac0;
const uint32_t O_PERIOD = 0x00982f90, O_SERVER = 0x00982f94;
const uint32_t O_ANGLE_Q[3] = {0x00982f10, 0x00982e90, 0x00982e10};
const uint32_t O_MSG_CTOR = 0x0073d740;
const uint32_t O_TABLE = 0x00982dec, O_COUNT = 0x00982df0;
// The slots.
enum {
    S_SET_READ, S_SET_WRITE, S_UPDATE, S_REFRESH, S_DTOR, S_SET_FLAGS, S_SET_LAG, S_SET_VALUE, S_SET_NET, S_SET_RATE,
    S_CURRENT, S_TARGET, S_NET, S_RATE, S_WRITE, S_READ, S_BEGIN_WRITE, S_BEGIN_READ, S_END_WRITE, S_END_READ,
    S_SET_DIRECTION, S_SET_ANGLE, S_GET_ANGLE
};

template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }

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

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
uint32_t U32() { return static_cast<uint32_t>(rng()); }
float R(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

// ---- the quantisations (the test's, shared; the direction tracks' are each program's globals) ----
alignas(16) S_NetQuantization g_q[8];   // 0..1 values, 2..4 deltas, 5..7 rates

void MakeQuantizations()
{
    for (int i = 0; i < 8; ++i) {
        float span, step;
        if (i < 2) {
            span = i ? 512.0f : R(500.0f, 5000.0f);
            step = i ? 0.0078125f : R(0.005f, 0.05f);
        } else if (i < 5) {
            span = i == 2 ? 8.0f : R(1.0f, 40.0f);
            step = i == 2 ? 0.0078125f : R(0.002f, 0.05f);
        } else {
            span = i == 5 ? 64.0f : R(10.0f, 200.0f);
            step = i == 5 ? 0.0625f : R(0.01f, 0.1f);
        }
        const float ys = i < 2 ? span * 0.25f : span;
        g_q[i].Setup(S_vector4{span, ys, span, 0.0f}, S_vector4{-span * 0.5f, i < 2 ? -ys * 0.25f : -ys * 0.5f, -span * 0.5f, 0.0f},
                     S_vector4{step, step, step, 0.0f}, 100);
    }
}

// The original's pointer for a port's quantisation (the direction tracks' globals are each program's).
uintptr_t OrigQ(const S_NetQuantization *q)
{
    if (q == &g_netAngleQuantization)
        return O_ANGLE_Q[0];
    if (q == &g_netAngleDeltaQuantization)
        return O_ANGLE_Q[1];
    if (q == &g_netAngleRateQuantization)
        return O_ANGLE_Q[2];
    return reinterpret_cast<uintptr_t>(q);
}

// ---- the pair ----
const int WORDS = 2048;
const uint32_t LIMIT_BITS = (WORDS - 64) * 32;

struct S_Pair {
    int kind = 0;   // 0 the track, 1 the position track, 2 the direction track
    alignas(16) uint8_t o[0xC0];
    alignas(16) uint8_t pStore[sizeof(C_NetDirTrack)];
    C_NetTrack *p = nullptr;
    // The messages.
    alignas(16) uint8_t om[0x40];
    alignas(16) uint8_t pmStore[sizeof(C_NetMessage)];
    C_NetMessage *pm = nullptr;
    uint32_t pBuf[WORDS], oBuf[WORDS];
    std::vector<uint32_t> starts;   // where the writes began
    alignas(16) S_vector4 pLast, oLast;
    C_NetDirTrack *dir() { return static_cast<C_NetDirTrack *>(p); }
} g;

uint32_t OSlot(int slot) { return OF<uint32_t>(reinterpret_cast<void *>(OF<uint32_t>(g.o, 0)), slot * 4); }
int Size() { return g.kind == 2 ? 0xC0 : 0x90; }

std::string Floats(const uint8_t *b, int from, int to)
{
    std::string s;
    for (int i = from; i < to; i += 4) {
        char t[48];
        std::snprintf(t, sizeof t, " %+.8g", *reinterpret_cast<const float *>(b + i));
        s += t;
    }
    return s;
}

// At construction the original leaves +0x1C, the vectors' w and the direction track's turn sign as the memory was (the
// port's member initialisers zero them); not compared then.
bool Uninitialised(int at) { return at == 0x1C || (at & 0xF) == 0xC || at == 0xB0 || at > 0xB0; }

void CompareTracks(bool construct = false)
{
    // The port's bytes in the original's layout (the fields are where the comments say: checked in main).
    const uint8_t *pb = reinterpret_cast<const uint8_t *>(g.p);
    std::string s;
    for (int i = 0; i < 3; ++i) {
        if (OrigQ(g.p->m_read[i]) != OF<uintptr_t>(g.o, 4 + 4 * i))
            s += "read q " + std::to_string(i) + "; ";
        if (OrigQ(g.p->m_write[i]) != OF<uintptr_t>(g.o, 0x10 + 4 * i))
            s += "write q " + std::to_string(i) + "; ";
    }
    for (int at = 0x1C; at < Size(); at += 4) {
        if ((at == 0x5C && g.kind == 2) || at == 0x7C)
            continue;   // the rate's w after EndRead, the step's w after Update: the original's uninitialised locals
        if (construct && Uninitialised(at))
            continue;
        if (OF<uint32_t>(const_cast<uint8_t *>(pb), at) != OF<uint32_t>(g.o, at)) {
            const int row = at & ~0xF;
            s += "+" + Hex(at).substr(5) + ": orig" + Floats(g.o, row, row + 16) + " / port" + Floats(pb, row, row + 16) + "; ";
            at = row + 12;
        }
    }
    // (The direction track's turn sign is an int.)
    Check(s.empty(), s);
    // Carry on from the original's.
    std::memcpy(reinterpret_cast<uint8_t *>(g.p) + 0x1C, g.o + 0x1C, Size() - 0x1C);
    Check(Bits(g_netUpdatePeriod) == Bits(G<float>(O_PERIOD)), "the update period");
}

void CompareMessages()
{
    std::string s;
    if (g.pm->m_wordIndex != OF<uint32_t>(g.om, 8) || g.pm->m_bitIndex != OF<uint32_t>(g.om, 0xC))
        s += "position " + std::to_string(OF<uint32_t>(g.om, 8) * 32 + OF<uint32_t>(g.om, 0xC)) + " / " +
             std::to_string(g.pm->BitPosition()) + "; ";
    if (std::memcmp(g.pBuf, g.oBuf, sizeof g.pBuf) != 0) {
        int i = 0;
        while (g.pBuf[i] == g.oBuf[i])
            ++i;
        s += "buffer word " + std::to_string(i) + ": " + Hex(g.oBuf[i]) + " / " + Hex(g.pBuf[i]) + "; ";
    }
    Check(s.empty(), s);
    if (!s.empty()) {
        std::memcpy(g.pBuf, g.oBuf, sizeof g.pBuf);
        g.pm->m_wordIndex = OF<uint32_t>(g.om, 8);
        g.pm->m_bitIndex = OF<uint32_t>(g.om, 0xC);
    }
}

void SetServer(bool on)
{
    g_netServerSide = on;
    G<uint8_t>(O_SERVER) = on;
}

// ---- the values ----
float Coordinate(const S_NetQuantization *q, int i)
{
    if (!q || P(0.2))
        return P(0.3) ? 0.0f : R(-50.0f, 50.0f);
    switch (I(0, 5)) {
    case 0: return q->min[i];
    case 1: return q->min[i] + q->range[i];
    case 2: return R(-1.0f, 1.0f) * q->deadZone[i] * 2;
    default: return q->min[i] + q->range[i] * R(-0.05f, 1.05f);
    }
}
S_vector4 RandomVector(const S_NetQuantization *q)
{
    if (P(0.05))
        return S_vector4{P(0.5) ? 1e-8f : 9.9999e-9f, 0, 0, 0};   // a square at 1e-16
    return S_vector4{Coordinate(q, 0), Coordinate(q, 1), Coordinate(q, 2), P(0.8) ? 0.0f : R(-5.0f, 5.0f)};
}
// Near a vector (the last sent): within its steps, its delta range, or anywhere.
S_vector4 Near(const S_vector4 &v, const S_NetQuantization *delta)
{
    S_vector4 r = v;
    float *c = &r.x;
    const int how = I(0, 4);
    if (how == 4) {
        c[I(0, 2)] += (P(0.5) ? 1 : -1) * R(0.025f, 0.045f);
        return r;
    }
    for (int i = 0; i < 3; ++i) {
        if (how == 0)
            c[i] += 0.0f;
        else if (how == 1 && delta)
            c[i] += R(-0.9f, 0.9f) * delta->deadZone[i];
        else if (how == 2 && delta)
            c[i] += R(-0.6f, 0.6f) * delta->range[i];
        else
            c[i] += R(-30.0f, 30.0f);
    }
    return r;
}
// x whose square's bits are Magnitude's floor (0x322bcc77), and the next float (found in main).
float g_floorRoot[2] = {1e-4f, 1e-4f};

S_vector4 RandomRate()
{
    switch (I(0, 5)) {
    case 0: return S_vector4{0, 0, 0, 0};
    case 1:
        if (P(0.3))
            return S_vector4{g_floorRoot[I(0, 1)], 0, 0, 0};   // a square at Magnitude's floor or just over
        return P(0.5) ? S_vector4{1e-9f, 0, 0, 0} : S_vector4{P(0.5) ? 1e-8f : 1.0000001e-8f, 0, 0, 0};
    case 2: return S_vector4{R(-0.2f, 0.2f), R(-0.2f, 0.2f), R(-0.2f, 0.2f), 0};
    case 3: return S_vector4{1, 0, 0, 0};
    default: return S_vector4{R(-30, 30), R(-5, 5), R(-30, 30), P(0.8) ? 0.0f : 7.0f};
    }
}
S_vector4 RandomDir()
{
    switch (I(0, 7)) {
    case 0: return S_vector4{0, P(0.5) ? 1.0f : -1.0f, 0, 0};
    case 1: return S_vector4{-1, 0, P(0.5) ? 0.0f : -0.0f, 0};
    case 2: return S_vector4{1, 0, 0, 0};
    default: {
        std::normal_distribution<float> n;
        float x = n(rng), y = n(rng), z = n(rng);
        const float l = std::sqrt(x * x + y * y + z * z);
        if (l == 0)
            return S_vector4{1, 0, 0, 0};
        y = std::max(-1.0f, std::min(1.0f, y / l));
        return S_vector4{x / l, y, z / l, 0};
    }
    }
}
uint32_t RandomFlags()
{
    uint32_t f = static_cast<uint32_t>(I(0, 7));
    if (P(0.5))
        f |= 7;
    const uint32_t flags[5] = {NET_TRACK_KEEP_CURRENT, NET_TRACK_DELTA, NET_TRACK_DERIVE_RATE, NET_TRACK_SEND_RATE,
                               NET_TRACK_EXTRAPOLATE};
    for (uint32_t x : flags)
        if (P(0.4))
            f |= x;
    if (P(0.3))
        f |= NET_TRACK_MOVE_TARGET;
    if (P(0.05))
        f = U32();
    return f;
}

// ---- the round ----
bool HasQuantizations()
{
    for (int i = 0; i < 3; ++i)
        if (!g.p->m_read[i] || !g.p->m_write[i])
            return false;
    return true;
}

void NewPair()
{
    g.kind = I(0, 2);
    std::memset(g.o, 0xEE, sizeof g.o);
    std::memset(g.pStore, 0xEE, sizeof g.pStore);
    switch (g.kind) {
    case 0: g.p = new (g.pStore) C_NetTrack(); break;
    case 1: g.p = new (g.pStore) C_NetPositionTrack(); break;
    default: g.p = new (g.pStore) C_NetDirTrack(); break;
    }
    OriginalAt<FCtor>(O_CTOR[g.kind])(g.o);
    g_op = "construct";
    Check(OF<uint32_t>(g.o, 0) == O_VTABLE[g.kind], "vtable " + Hex(OF<uint32_t>(g.o, 0)));
    if (g.kind == 2)
        for (int i = 0; i < 3; ++i)
            Check(std::memcmp(reinterpret_cast<void *>(O_ANGLE_Q[i]),
                                  i == 0 ? &g_netAngleQuantization : i == 1 ? &g_netAngleDeltaQuantization : &g_netAngleRateQuantization,
                                  0x80) == 0,
                  "the angle quantisation " + std::to_string(i));
    CompareTracks(true);
    // The messages: over the test's buffers (never grown).
    for (int i = 0; i < WORDS; ++i)
        g.pBuf[i] = g.oBuf[i] = P(0.3) ? 0 : U32();
    g.pm = new (g.pmStore) C_NetMessage();
    OriginalAt<FMsgCtor>(O_MSG_CTOR)(g.om);
    g.pm->Attach(g.pBuf, WORDS * 32);
    OF<uint32_t *>(g.om, 4) = g.oBuf;
    OF<uint32_t>(g.om, 0x10) = WORDS * 32;
    g.starts.clear();
    g.pLast = g.oLast = RandomVector(&g_q[0]);
}

void SetQuantizations()
{
    g_op = "SetRead/WriteQuantizations";
    S_NetQuantization *q[3];
    for (int i = 0; i < 3; ++i)
        q[i] = P(0.15) ? nullptr : &g_q[i == 0 ? I(0, 1) : i == 1 ? I(2, 4) : I(5, 7)];
    const bool write = P(0.5);
    g_trace += write ? " QW" : " QR";
    if (write)
        g.p->SetWriteQuantizations(q[0], q[1], q[2]);
    else
        g.p->SetReadQuantizations(q[0], q[1], q[2]);
    OriginalAt<FQ>(OSlot(write ? S_SET_WRITE : S_SET_READ))(g.o, q[0], q[1], q[2]);
    CompareTracks();
}

void OpSetters()
{
    const int which = I(0, 5);
    switch (which) {
    case 0: {
        g_op = "SetFlags";
        const uint32_t f = RandomFlags();
        g_trace += " F" + Hex(f);
        g.p->SetFlags(f);
        OriginalAt<FUint>(OSlot(S_SET_FLAGS))(g.o, f);
        break;
    }
    case 1: {
        g_op = "SetLag";
        const float lag = P(0.3) ? 0.035f : R(0.0f, 0.5f);
        g_trace += " L";
        g.p->SetLag(lag);
        OriginalAt<FFloat>(OSlot(S_SET_LAG))(g.o, lag);
        break;
    }
    case 2: {
        g_op = "SetValue";
        alignas(16) S_vector4 v = RandomVector(g.p->m_write[0]);
        if (P(0.3))
            v = Near(g.pLast, g.p->m_write[1]);
        g_trace += " SV";
        g.p->SetValue(v);
        OriginalAt<FVec>(OSlot(S_SET_VALUE))(g.o, &v);
        break;
    }
    case 3: {
        g_op = "SetNetValue";
        alignas(16) S_vector4 v = P(0.3) ? Near(g.pLast, g.p->m_write[1]) : RandomVector(g.p->m_write[0]);
        g_trace += " SN";
        g.p->SetNetValue(v);
        OriginalAt<FVec>(OSlot(S_SET_NET))(g.o, &v);
        break;
    }
    case 4: {
        g_op = "SetRate";
        alignas(16) S_vector4 v = RandomRate();
        g_trace += " SR";
        g.p->SetRate(v);
        OriginalAt<FVec>(OSlot(S_SET_RATE))(g.o, &v);
        break;
    }
    default: {
        g_op = "getters";
        const uint8_t *pb = reinterpret_cast<const uint8_t *>(g.p);
        const int slots[4] = {S_CURRENT, S_TARGET, S_NET, S_RATE};
        const S_vector4 *pr[4] = {&g.p->GetCurrent(), &g.p->GetTarget(), &g.p->GetNetValue(), &g.p->GetRate()};
        for (int i = 0; i < 4; ++i) {
            const uint8_t *orr = static_cast<const uint8_t *>(OriginalAt<FGet>(OSlot(slots[i]))(g.o));
            Check(orr - g.o == reinterpret_cast<const uint8_t *>(pr[i]) - pb,
                  "getter " + std::to_string(i) + " at +" + Hex(static_cast<uint32_t>(orr - g.o)) + " / +" +
                      Hex(static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(pr[i]) - pb)));
        }
        g.p->Refresh();
        OriginalAt<FVoid>(OSlot(S_REFRESH))(g.o);
        if (g.kind == 2) {
            const float a = g.dir()->GetAngle(), b = OriginalAt<FGetFloat>(OSlot(S_GET_ANGLE))(g.o);
            Check(Bits(a) == Bits(b), "GetAngle");
        }
        return;
    }
    }
    CompareTracks();
}

void OpUpdate()
{
    // (A direction track reset has no quantisations: its Update reads through null, the original's too. The game resets
    // them only in the constructor, before setting them.)
    if (g.kind == 2 && !g.p->m_read[0])
        return;
    g_op = "Update";
    float s;
    switch (I(0, 4)) {
    case 0: s = 0.0f; break;
    case 1: s = 0.1f; break;
    case 2: s = R(1.0f, 5.0f); break;
    default: s = R(0.001f, 0.2f); break;
    }
    g_trace += " U";
    g.p->Update(s);
    OriginalAt<FFloat>(OSlot(S_UPDATE))(g.o, s);
    CompareTracks();
}

void OpWrite()
{
    if (!HasQuantizations() || g.pm->BitPosition() > LIMIT_BITS)
        return;
    g_op = "Write";
    const bool delta = P(0.7);
    if (P(0.3)) {
        // The last sent near the value (within its steps, its delta range).
        const S_vector4 &from = g_netServerSide && P(0.5) ? g.p->m_net : g.p->m_current;
        g.pLast = g.oLast = Near(from, g.p->m_write[1]);
    }
    g_trace += delta ? " WD" : " W";
    g.starts.push_back(g.pm->BitPosition());
    g.p->Write(g.pm, delta, &g.pLast);
    OriginalAt<FWrite>(OSlot(S_WRITE))(g.o, g.om, delta, &g.oLast);
    Check(std::memcmp(&g.pLast, &g.oLast, 16) == 0, "the last sent");
    g.oLast = g.pLast;
    CompareTracks();
    CompareMessages();
}

void OpRead()
{
    if (!HasQuantizations())
        return;
    g_op = "Read";
    uint32_t at;
    if (!g.starts.empty() && P(0.85))
        at = g.starts[I(0, static_cast<int>(g.starts.size()) - 1)];
    else
        at = static_cast<uint32_t>(I(0, static_cast<int>(LIMIT_BITS)));
    g_trace += " R" + std::to_string(at);
    g.pm->SetBitPosition(at);
    OF<uint32_t>(g.om, 8) = at >> 5;
    OF<uint32_t>(g.om, 0xC) = at & 31;
    g.p->Read(g.pm);
    OriginalAt<FRead>(OSlot(S_READ))(g.o, g.om);
    CompareTracks();
    CompareMessages();
}

void OpReset()
{
    g_op = "Reset";
    g_trace += " X";
    if (g.kind == 2) {
        g.dir()->Reset();
        OriginalAt<FVoid>(O_DIR_RESET)(g.o);
    } else {
        g.p->Reset();
        OriginalAt<FVoid>(O_RESET)(g.o);
    }
    CompareTracks();
}

void OpDirection()
{
    if (g.kind != 2)
        return;
    if (P(0.7)) {
        g_op = "SetDirection";
        alignas(16) S_vector4 d = RandomDir();
        const float angle = P(0.3) ? 0.0f : R(-3.2f, 3.2f);
        g_trace += " D";
        g.dir()->SetDirection(d, angle);
        OriginalAt<FSetDir>(OSlot(S_SET_DIRECTION))(g.o, &d, angle);
    } else {
        g_op = "SetAngle";
        const float angle = R(-3.2f, 3.2f);
        g_trace += " A";
        g.dir()->SetAngle(angle);
        OriginalAt<FFloat>(OSlot(S_SET_ANGLE))(g.o, angle);
    }
    CompareTracks();
}

// The direction track's EndWrite and EndRead alone (Write and Read call them after the value; here on the angles poked).
void OpEnds()
{
    if (g.kind != 2 || !HasQuantizations() || g.pm->BitPosition() > LIMIT_BITS)
        return;
    if (P(0.5)) {
        g_op = "EndWrite";
        g_trace += " EW";
        g.starts.push_back(g.pm->BitPosition());
        g.p->EndWrite(g.pm);
        OriginalAt<FRead>(OSlot(S_END_WRITE))(g.o, g.om);
    } else {
        g_op = "EndRead";
        g_trace += " ER";
        g.p->EndRead(g.pm);
        OriginalAt<FRead>(OSlot(S_END_READ))(g.o, g.om);
    }
    CompareTracks();
    CompareMessages();
}

// Fields no call sets (the snap distance stays 3; the direction track's SetFlags drops the rate flags) written on both
// sides, to run the code behind them: the direction track's moving target and its short way round.
void OpPoke()
{
    g_op = "poke";
    const int how = I(0, 3);
    if (how == 2) {
        // The snap distance at the distance itself (the port's sum: z, y, x).
        const float dx = g.p->m_target.x - g.p->m_current.x, dy = g.p->m_target.y - g.p->m_current.y,
                    dz = g.p->m_target.z - g.p->m_current.z;
        const float snap = std::sqrt(dz * dz + dy * dy + dx * dx);
        g_trace += " Kdist";
        g.p->m_snapDistance = snap;
        OF<float>(g.o, 0x8C) = snap;
    } else if (how == 3 && g.kind == 2) {
        // The direction track's angles: a turn of exactly 0.001 either way or none, the current and the target apart by
        // 1e-8 or 0.8 on an axis (EndRead's square at 1e-16; a rate of exactly 8), or anything.
        float *cur = &g.p->m_current.x, *tgt = &g.p->m_target.x, *prev = &g.dir()->m_previous.x;
        const float a = R(-3.0f, 3.0f);
        prev[0] = 0.0f;
        switch (I(0, 3)) {
        case 0: cur[0] = P(0.5) ? -0.001f : 0.001f; break;
        case 1: cur[0] = 0.0f; break;
        default: cur[0] = a; break;
        }
        for (int i = 0; i < 3; ++i)
            tgt[i] = cur[i] = i ? R(-1.0f, 1.0f) : cur[0];
        const int axis = I(0, 2);
        switch (I(0, 2)) {
        case 0: {
            // Exact distances on one axis from 0: 1e-8 (a square at 1e-16), 0.8 (a rate of 8 over 0.1 s), 0.785 (the
            // yaw's limit), half the angles' span (the short way round; the snap distance past it).
            const float twoPi = 6.2831855f, square = twoPi * twoPi;
            const float span = std::sqrt(square + square + square);   // (the angles' range40, summed as the port does)
            const float apart[4] = {1e-8f, 0.8f, 0.785f, span * 0.5f};
            const float d = apart[I(0, 3)];
            cur[axis] = 0.0f;
            tgt[axis] = -d * (P(0.5) ? 1 : -1);
            if (P(0.5)) {
                g.p->m_snapDistance = 12.0f;
                OF<float>(g.o, 0x8C) = 12.0f;
            }
            break;
        }
        case 1: tgt[axis] = cur[axis] + R(-1.0f, 1.0f); break;
        default: break;
        }
        g_trace += " Kangles";
        std::memcpy(g.o + 0x20, &g.p->m_current, 16);
        std::memcpy(g.o + 0x30, &g.p->m_target, 16);
        std::memcpy(g.o + 0xA0, &g.dir()->m_previous, 16);
    } else if (how == 0) {
        const float snap = P(0.5) ? R(5.0f, 12.0f) : R(0.0f, 3.0f);
        g_trace += " Ksnap";
        g.p->m_snapDistance = snap;
        OF<float>(g.o, 0x8C) = snap;
    } else {
        const uint32_t f = RandomFlags() | (P(0.5) ? static_cast<uint32_t>(NET_TRACK_MOVE_TARGET) : 0u);
        g_trace += " Kflags" + Hex(f);
        g.p->m_flags = f;
        OF<uint32_t>(g.o, 0x88) = f;
    }
    CompareTracks();
}

void Round()
{
    g_trace.clear();
    SetServer(P(0.5));
    if (P(0.2)) {
        const float period = P(0.5) ? 0.05f : 0.2f;
        g_netUpdatePeriod = period;
        G<float>(O_PERIOD) = period;
    }
    NewPair();
    if (g.kind != 2 && P(0.9)) {
        // Both sets, as the game's callers do (then changed now and then).
        S_NetQuantization *q[3] = {&g_q[I(0, 1)], &g_q[I(2, 4)], &g_q[I(5, 7)]};
        g.p->SetReadQuantizations(q[0], q[1], q[2]);
        g.p->SetWriteQuantizations(q[0], q[1], q[2]);
        OriginalAt<FQ>(OSlot(S_SET_READ))(g.o, q[0], q[1], q[2]);
        OriginalAt<FQ>(OSlot(S_SET_WRITE))(g.o, q[0], q[1], q[2]);
    }
    const int ops = I(1, 40);
    for (int k = 0; k < ops; ++k) {
        const int op = I(0, 99);
        if (op < 6)
            SetQuantizations();
        else if (op < 36)
            OpSetters();
        else if (op < 52)
            OpUpdate();
        else if (op < 70)
            OpWrite();
        else if (op < 84)
            OpRead();
        else if (op < 87)
            OpReset();
        else if (op < 89)
            SetServer(!g_netServerSide);
        else if (op < 91 + (g.kind == 2 ? 6 : 0))
            OpPoke();
        else if (g.kind == 2 && op < 99)
            OpEnds();
        else
            OpDirection();
    }
    g.p->~C_NetTrack();
    g.pm->~C_NetMessage();
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
    if (std::getenv("HD2_TRACK_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("track_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // The port's layout is the original's (the comments' offsets).
    if (offsetof(C_NetTrack, m_read) != 4 || offsetof(C_NetTrack, m_unknown1C) != 0x1C || offsetof(C_NetTrack, m_current) != 0x20 ||
        offsetof(C_NetTrack, m_step) != 0x70 || offsetof(C_NetTrack, m_snapDistance) != 0x8C ||
        offsetof(C_NetDirTrack, m_direction) != 0x90 || offsetof(C_NetDirTrack, m_turnSign) != 0xB0 || sizeof(C_NetDirTrack) > 0xC0) {
        std::printf("track_diff: the port's track layout is not the original's\n");
        return 1;
    }
    G<void *>(O_TABLE) = nullptr;
    G<int>(O_COUNT) = 0;
    int rounds = std::getenv("HD2_TRACK_QUICK") ? 50000 : 100000;
    if (const char *r = std::getenv("HD2_TRACK_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_TRACK_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    rng.seed(seed);
    MakeQuantizations();
    for (float x = 0.9999e-4f; x < 1.0001e-4f; x = std::nextafter(x, 1.0f)) {
        const float sq = x * x;
        if (Bits(sq) >= 0x322bcc77u) {
            g_floorRoot[0] = x;
            // (No float squares to it exactly: x is the first whose square is over it, 0x322bcc78.)
            g_floorRoot[1] = std::nextafter(x, 1.0f);
            break;
        }
    }
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("track_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
