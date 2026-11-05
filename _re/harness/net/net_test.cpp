// The net module's replication (part 10): the tracks (net/net_track.cpp, part 10a), the objects
// (net/net_object.cpp, part 10b: net_object_test.inc). MinGW, float math as the game's (SSE); run net_test.exe
// ("net: N checks, 0 failures"). Mutations: mutations/mutate_10a.py, mutate_10b.py with the scratchpad's mutnet.py
// (it builds this harness).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <new>

#define protected public
#define private public
#include "net/bitstream.h"
#include "net/net_track.h"
#include "net/net_object.h"
#include "net/net_channel.h"
#include "game/perf_timer.h"
#undef private
#include <windows.h>
#undef protected

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

// (Part 10b.) The deletes, recorded while tracking (a fixed array: recording must not allocate).
bool g_trackDeletes;
void *g_deleted[512];
int g_deletedCount;
void RecordDelete(void *p)
{
    if (g_trackDeletes && p && g_deletedCount < 512)
        g_deleted[g_deletedCount++] = p;
}
bool WasDeleted(const void *p)
{
    for (int i = 0; i < g_deletedCount; ++i) {
        if (g_deleted[i] == p)
            return true;
    }
    return false;
}
void *operator new(std::size_t n)
{
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
    RecordDelete(p);
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    RecordDelete(p);
    std::free(p);
}
void *operator new(std::size_t n, std::align_val_t a)
{
    if (void *p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a)))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p, std::align_val_t) noexcept
{
    RecordDelete(p);
    _aligned_free(p);
}
void operator delete(void *p, std::size_t, std::align_val_t) noexcept
{
    RecordDelete(p);
    _aligned_free(p);
}

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

bool Near(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

bool Same(const S_vector4 &a, const S_vector4 &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

bool Same3(const S_vector4 &a, float x, float y, float z)
{
    return a.x == x && a.y == y && a.z == z;
}

void Rewind(C_NetMessage &msg)
{
    msg.m_wordIndex = 0;
    msg.m_bitIndex = 0;
}

// Quantisations as the replication's (positions over 3000 m in 0.01 steps, deltas over 0.6, rates over 15).
S_NetQuantization g_value, g_delta, g_rate;

void MakeQuantizations()
{
    g_value.Setup({3000.0f, 3000.0f, 3000.0f, 0.0f}, {-1000.0f, -1000.0f, -1000.0f, 0.0f}, {0.01f, 0.01f, 0.01f, 0.0f}, 100);
    g_delta.Setup({0.6f, 0.6f, 0.6f, 0.0f}, {-0.3f, -0.3f, -0.3f, 0.0f}, {0.005f, 0.007f, 0.005f, 0.0f}, 100);
    g_rate.Setup({15.0f, 15.0f, 15.0f, 0.0f}, {-7.5f, -7.5f, -7.5f, 0.0f}, {0.1f, 0.1f, 0.1f, 0.0f}, 100);
}

void Quantize(C_NetTrack &t)
{
    t.SetReadQuantizations(&g_value, &g_delta, &g_rate);
    t.SetWriteQuantizations(&g_value, &g_delta, &g_rate);
}

// ---- the base, the defaults, the setters ---------------------------------------------------------------------------

void TestDefaults()
{
    g_netUpdatePeriod = 5.0f;
    C_NetPositionTrack t;
    CHECK(t.m_flags == 7 && t.m_speed == 40.0f && t.m_lag == 0.035f && t.m_snapDistance == 3.0f);
    CHECK(g_netUpdatePeriod == 0.1f);
    for (int i = 0; i < 3; ++i)
        CHECK(t.m_read[i] == nullptr && t.m_write[i] == nullptr);
    CHECK(Same3(t.m_current, 0, 0, 0) && Same3(t.m_target, 0, 0, 0) && Same3(t.m_net, 0, 0, 0) &&
          Same3(t.m_rate, 0, 0, 0) && Same3(t.m_sentRate, 0, 0, 0) && Same3(t.m_step, 0, 0, 0));
    // Reset after changes: back to the defaults, the quantisations dropped, w kept.
    t.m_current = {1, 2, 3, 4};
    t.m_step = {5, 6, 7, 8};
    t.m_target = {1, 1, 1, 1};
    t.m_net = {2, 2, 2, 2};
    t.m_rate = {3, 3, 3, 3};
    t.m_sentRate = {4, 4, 4, 4};
    t.m_flags = 0xFFFF;
    t.m_speed = t.m_lag = t.m_snapDistance = 9.0f;
    Quantize(t);
    g_netUpdatePeriod = 2.0f;
    t.Reset();
    CHECK(Same(t.m_current, {0, 0, 0, 4}) && Same(t.m_step, {0, 0, 0, 8}));
    CHECK(Same(t.m_target, {0, 0, 0, 1}) && Same(t.m_net, {0, 0, 0, 2}) && Same(t.m_rate, {0, 0, 0, 3}) &&
          Same(t.m_sentRate, {0, 0, 0, 4}));
    CHECK(t.m_flags == 7 && t.m_speed == 40.0f && t.m_lag == 0.035f && t.m_snapDistance == 3.0f &&
          g_netUpdatePeriod == 0.1f && t.m_read[1] == nullptr && t.m_write[2] == nullptr);
    // Setters.
    t.SetFlags(0x12345);
    t.SetLag(0.5f);
    CHECK(t.GetFlags() == 0x12345 && t.GetLag() == 0.5f);
    t.Refresh();
}

void TestQuantizationSetters()
{
    C_NetPositionTrack t;
    S_NetQuantization a, b, c;
    t.SetReadQuantizations(&a, &b, &c);
    t.SetWriteQuantizations(&c, &a, &b);
    CHECK(t.m_read[0] == &a && t.m_read[1] == &b && t.m_read[2] == &c);
    CHECK(t.m_write[0] == &c && t.m_write[1] == &a && t.m_write[2] == &b);
    // Null ones are not stored.
    t.SetReadQuantizations(nullptr, &c, nullptr);
    t.SetWriteQuantizations(&b, nullptr, nullptr);
    CHECK(t.m_read[0] == &a && t.m_read[1] == &c && t.m_read[2] == &c);
    CHECK(t.m_write[0] == &b && t.m_write[1] == &a && t.m_write[2] == &b);
    t.SetReadQuantizations(nullptr, nullptr, &a);
    t.SetWriteQuantizations(nullptr, &c, nullptr);
    CHECK(t.m_read[2] == &a && t.m_read[0] == &a && t.m_write[1] == &c && t.m_write[0] == &b);
}

void TestSetValue()
{
    C_NetPositionTrack t;
    t.m_net = {9, 9, 9, 9};
    g_netServerSide = false;
    t.SetValue({1, 2, 3, 4});
    CHECK(Same(t.m_current, {1, 2, 3, 4}) && Same(t.m_target, {1, 2, 3, 4}) && Same(t.m_net, {1, 2, 3, 4}));
    // The server's net value cleared (x, y, z).
    t.m_net = {9, 9, 9, 9};
    g_netServerSide = true;
    t.SetValue({5, 6, 7, 8});
    CHECK(Same(t.m_current, {5, 6, 7, 8}) && Same(t.m_target, {5, 6, 7, 8}) && Same(t.m_net, {0, 0, 0, 9}));
    g_netServerSide = false;
    t.SetNetValue({1, 1, 2, 3});
    CHECK(Same(t.GetNetValue(), {1, 1, 2, 3}) && &t.GetCurrent() == &t.m_current && &t.GetTarget() == &t.m_target);
}

void TestRate()
{
    C_NetPositionTrack t;
    t.SetRate(S_vector4{3, 4, 0, 7});
    CHECK(Same(t.m_rate, {3, 4, 0, 7}) && t.GetSpeed() == 5.0f && &t.GetRate() == &t.m_rate);
    // A length within 1e-8 of 1: exactly 1; a tiny one: 0 (compared as bits).
    t.SetRate(S_vector4{1.0f, 0.0f, 0.0f, 0.0f});
    CHECK(t.m_speed == 1.0f);
    t.SetRate(S_vector4{0.6f, 0.8f, 0.0f, 0.0f});
    CHECK(t.m_speed == 1.0f);
    t.SetRate(S_vector4{0.00009f, 0.0f, 0.0f, 0.0f});
    CHECK(t.m_speed == 0.0f);
    t.SetRate(S_vector4{0.0002f, 0.0f, 0.0f, 0.0f});
    CHECK(t.m_speed == std::sqrt(0.0002f * 0.0002f));
    t.SetRate(S_vector{0.0f, 0.0f, 2.0f});
    CHECK(Same(t.m_rate, {0, 0, 2, 0}) && t.m_speed == 2.0f);
    // A zero rate: the update's step.
    t.SetRate(S_vector4{0, 0, 0, 0});
    CHECK(&t.GetRate() == &t.m_step);
    t.m_rate.x = 1e-9f;
    CHECK(&t.GetRate() == &t.m_step);
    t.m_rate.x = 1e-7f;
    CHECK(&t.GetRate() == &t.m_rate);
}

// ---- the update ----------------------------------------------------------------------------------------------------

void TestUpdate()
{
    C_NetPositionTrack t;
    // A move: the step at the speed towards the target.
    t.m_current = {0, 0, 0, 1};
    t.m_target = {2, 0, 0, 2};
    t.m_speed = 4.0f;
    t.m_step = {9, 9, 9, 9};
    t.Update(0.25f);
    CHECK(Same3(t.m_current, 1.0f, 0, 0) && t.m_current.w == 1.0f && Same(t.m_step, {4, 0, 0, 9}));
    // Arrived (the step would pass it): the target, w too; the step cleared.
    t.Update(0.3f);
    CHECK(Same(t.m_current, {2, 0, 0, 2}) && Same(t.m_step, {0, 0, 0, 9}));
    // Exactly reaching it: still a step (only beyond it snaps).
    t.m_current = {0, 0, 0, 1};
    t.Update(0.5f);
    CHECK(Same(t.m_current, {2, 0, 0, 1}) && t.m_step.x == 4.0f);
    // Farther than the snap distance: jumps; nearer than 1e-5 too.
    t.m_current = {0, 0, 0, 1};
    t.m_target = {0, 3.5f, 0, 3};
    t.Update(0.01f);
    CHECK(Same(t.m_current, {0, 3.5f, 0, 3}) && Same3(t.m_step, 0, 0, 0));
    t.m_current = {0, 0, 0, 1};
    t.m_target = {0, 3.0f, 0, 3};
    t.Update(0.01f);
    CHECK(Same3(t.m_current, 0, 0.04f, 0) && t.m_current.w == 1.0f);
    t.m_current = {0, 0, 0, 1};
    t.m_target = {0, 0, 0.000005f, 5};
    t.Update(0.01f);
    CHECK(Same(t.m_current, {0, 0, 0.000005f, 5}));
    // The target moving at the rate, the speed boosted by a third of the distance.
    t.m_flags = NET_TRACK_MOVE_TARGET;
    t.m_rate = {1e-9f, -1e-9f, 10, 0};
    t.m_current = {0, 0, 0, 0};
    t.m_target = {0, 0, 0, 0};
    t.m_speed = 3.0f;
    t.Update(0.1f);
    // Target z 1: the speed 3 x (1/3 + 1) = 4: 0.4 of the 1.
    CHECK(t.m_target.x == 1e-10f && t.m_target.y == -1e-10f && t.m_target.z == 1.0f && Near(t.m_current.z, 0.4f, 1e-6f) &&
          Near(t.m_step.z, 4.0f, 1e-5f));
    // Without the flag the target stays.
    t.m_flags = 0;
    t.m_target = {0, 0, 1, 0};
    t.m_current = {0, 0, 0, 0};
    t.Update(0.1f);
    CHECK(Same3(t.m_target, 0, 0, 1.0f) && Near(t.m_current.z, 0.3f, 1e-6f));
    // A diagonal step: the components by speed / distance.
    t.m_current = {0, 0, 0, 0};
    t.m_target = {0.6f, 0, 0.8f, 0};
    t.m_speed = 2.0f;
    t.Update(0.1f);
    CHECK(Near(t.m_current.x, 0.12f, 1e-6f) && Near(t.m_current.z, 0.16f, 1e-6f) && Near(t.m_step.x, 1.2f));
}

// ---- writing and reading -----------------------------------------------------------------------------------------

uint32_t Bits(C_NetMessage &msg, int n)
{
    return msg.ReadBits(n);
}

void TestWrite()
{
    MakeQuantizations();
    g_netServerSide = false;
    C_NetPositionTrack t;
    Quantize(t);
    t.SetValue({10, 20, 30, 1});
    // Absolute (no delta): 1, 0, the quantised value; the last sent follows.
    {
        C_NetMessage msg;
        S_vector4 last = {0, 0, 0, 0};
        t.Write(&msg, false, &last);
        CHECK(msg.BitPosition() == 2u + 3u * static_cast<uint32_t>(g_value.bits[0]) + 1u);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 0);
        float v[3];
        msg.ReadQuantized(v, &g_value, 7);
        CHECK(Near(v[0], 10, 0.01f) && Near(v[1], 20, 0.01f) && Near(v[2], 30, 0.01f));
        CHECK(Same(last, {10, 20, 30, 1}));
        CHECK(Same(t.m_sentRate, t.m_rate));
    }
    // Delta, unchanged (within the steps): one 0 bit, the last sent kept.
    {
        C_NetMessage msg;
        S_vector4 last = {10.005f, 20, 29.995f, 7};
        t.Write(&msg, true, &last);
        CHECK(msg.BitPosition() == 1u);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 0 && Same(last, {10.005f, 20, 29.995f, 7}));
    }
    // Delta beyond the steps but the delta flag off: absolute.
    {
        C_NetMessage msg;
        S_vector4 last = {10.1f, 20, 30, 0};
        t.Write(&msg, true, &last);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 0 && Same(last, {10, 20, 30, 1}));
    }
    // The delta flag: a change within the delta range goes as the delta.
    t.SetFlags(7 | NET_TRACK_DELTA);
    {
        C_NetMessage msg;
        S_vector4 last = {10.1f, 20, 29.9f, 0};
        t.Write(&msg, true, &last);
        const uint32_t written = msg.BitPosition();
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 1);
        float d[3];
        msg.ReadQuantized(d, &g_delta, 7);
        CHECK(Near(d[0], -0.1f, 0.006f) && Near(d[1], 0, 0.007f) && Near(d[2], 0.1f, 0.006f));
        CHECK(Same(last, {10, 20, 30, 1}));
        // (y zero: only its presence bit.)
        CHECK(written == static_cast<uint32_t>(2 + g_delta.bits[0] + 1 + g_delta.bits[2]));
    }
    // A change in y or z alone is a change.
    {
        C_NetMessage msg;
        S_vector4 last = {10, 20.1f, 30, 0};
        t.Write(&msg, true, &last);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 1);
        float d[3];
        msg.ReadQuantized(d, &g_delta, 7);
        CHECK(Near(d[1], -0.1f, 0.008f) && Same(last, {10, 20, 30, 1}));
        C_NetMessage msg2;
        last = {10, 20, 30.2f, 0};
        t.Write(&msg2, true, &last);
        Rewind(msg2);
        CHECK(Bits(msg2, 1) == 1 && Bits(msg2, 1) == 1);
        msg2.ReadQuantized(d, &g_delta, 7);
        CHECK(Near(d[2], -0.2f, 0.006f));
    }
    // Out of the delta's range: absolute.
    {
        C_NetMessage msg;
        S_vector4 last = {9.0f, 20, 30, 0};
        t.Write(&msg, true, &last);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 0);
    }
    // Near the range's edge: inside.
    {
        C_NetMessage msg;
        S_vector4 last = {10.29f, 20, 30, 0};
        t.Write(&msg, true, &last);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 1);
    }
    // The rate changed from zero since last sent: sent even when the value did not move (not as a delta).
    {
        t.m_sentRate = {0, 0, 0, 0};
        t.m_rate = {1, 0, 0, 0};
        C_NetMessage msg;
        S_vector4 last = {10, 20, 30, 0};
        t.Write(&msg, true, &last);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 0 && Same(t.m_sentRate, {1, 0, 0, 0}));
        // "Changed" is the value written being above the steps (not its change): the last sent follows.
        CHECK(Same(last, {10, 20, 30, 1}));
        // Next time both nonzero: nothing.
        C_NetMessage msg2;
        t.Write(&msg2, true, &last);
        CHECK(msg2.BitPosition() == 1u);
        t.m_rate = {0, 0, 0, 0};
        t.m_sentRate = {0, 0, 0, 0};
    }
    // The rate sent: after a change, 1 and the rate; else 0.
    t.SetFlags(7 | NET_TRACK_SEND_RATE);
    t.m_rate = {2, -1, 0.5f, 0};
    {
        C_NetMessage msg;
        S_vector4 last = {0, 0, 0, 0};
        t.Write(&msg, false, &last);
        Rewind(msg);
        CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 0);
        float v[3];
        msg.ReadQuantized(v, &g_value, 7);
        CHECK(Bits(msg, 1) == 1);
        msg.ReadQuantized(v, &g_rate, 7);
        CHECK(Near(v[0], 2, 0.06f) && Near(v[1], -1, 0.06f) && Near(v[2], 0.5f, 0.06f));
        CHECK(msg.BitPosition() == msg.m_wordIndex * 32 + msg.m_bitIndex);
        C_NetMessage msg2;
        t.m_sentRate = t.m_rate;
        last = {10, 20, 30, 0};
        t.Write(&msg2, true, &last);
        Rewind(msg2);
        CHECK(msg2.BitPosition() == 0u && Bits(msg2, 1) == 0 && Bits(msg2, 1) == 0);
    }
    // A value of zeros: written, but not "changed": the last sent kept, no rate.
    {
        C_NetPositionTrack z;
        Quantize(z);
        z.SetFlags(7 | NET_TRACK_SEND_RATE);
        z.SetValue({0, 0, 0, 0});
        C_NetMessage msg;
        S_vector4 last = {5, 5, 5, 5};
        z.Write(&msg, false, &last);
        // (y: only its presence bit.)
        CHECK(Same(last, {5, 5, 5, 5}) && msg.BitPosition() == 2u + 2u * static_cast<uint32_t>(g_value.bits[0]) + 1u + 1u);
        Rewind(msg);
        Bits(msg, 2);
        float v[3];
        msg.ReadQuantized(v, &g_value, 7);
        CHECK(Bits(msg, 1) == 0);
    }
    // The server sends the net value when it has one, else the current one.
    {
        C_NetPositionTrack s;
        Quantize(s);
        s.m_current = {1, 2, 3, 0};
        s.m_net = {4, 5, 6, 0};
        g_netServerSide = true;
        C_NetMessage msg;
        S_vector4 last = {0, 0, 0, 0};
        s.Write(&msg, false, &last);
        CHECK(Same(last, {4, 5, 6, 0}));
        s.m_net = {0, 0, 0, 0};
        C_NetMessage msg2;
        s.Write(&msg2, false, &last);
        CHECK(Same(last, {1, 2, 3, 0}));
        g_netServerSide = false;
        s.m_net = {4, 5, 6, 0};
        C_NetMessage msg3;
        s.Write(&msg3, false, &last);
        CHECK(Same(last, {1, 2, 3, 0}));
    }
    // Only the flags' low components written.
    {
        C_NetPositionTrack m;
        Quantize(m);
        m.SetFlags(5);
        m.SetValue({1, 2, 3, 0});
        C_NetMessage msg;
        S_vector4 last = {};
        m.Write(&msg, false, &last);
        CHECK(msg.BitPosition() == 2u + 2u * static_cast<uint32_t>(g_value.bits[0]));
    }
}

void TestRead()
{
    MakeQuantizations();
    g_netServerSide = false;
    C_NetPositionTrack src, dst;
    Quantize(src);
    Quantize(dst);
    // Absolute: the net value, target and current set; no rate worked out.
    src.SetValue({10, 20, 30, 1});
    dst.SetFlags(7 | NET_TRACK_DERIVE_RATE);
    dst.m_net = {0, 0, 0, 9};
    {
        C_NetMessage msg;
        S_vector4 last = {};
        src.Write(&msg, false, &last);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Near(dst.m_net.x, 10, 0.01f) && Near(dst.m_net.y, 20, 0.01f) && Near(dst.m_net.z, 30, 0.01f));
        CHECK(dst.m_net.w == 9 && Same(dst.m_target, dst.m_net) && Same(dst.m_current, dst.m_net));
        CHECK(Same3(dst.m_rate, 0, 0, 0));
    }
    // A delta: added; the rate from the change over the period (w the old one's).
    src.SetFlags(7 | NET_TRACK_DELTA);
    dst.m_flags = 7 | NET_TRACK_DERIVE_RATE;
    {
        const S_vector4 before = dst.m_net;
        S_vector4 last = before;
        src.SetValue({before.x + 0.2f, before.y + 0.15f, before.z - 0.1f, 1});
        C_NetMessage msg;
        src.Write(&msg, true, &last);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Near(dst.m_net.x, before.x + 0.2f, 0.006f) && Near(dst.m_net.z, before.z - 0.1f, 0.006f));
        CHECK(Near(dst.m_net.y, before.y + 0.15f, 0.008f) && Near(dst.m_rate.y, 1.5f, 0.08f));
        CHECK(Near(dst.m_rate.x, 2.0f, 0.06f) && Near(dst.m_rate.z, -1.0f, 0.06f) && dst.m_rate.w == before.w);
        CHECK(Near(dst.m_speed, std::sqrt(dst.m_rate.x * dst.m_rate.x + dst.m_rate.y * dst.m_rate.y +
                                          dst.m_rate.z * dst.m_rate.z)));
    }
    // Unchanged: the rate from no change: zero (below 0.1 squared); the speed kept.
    {
        dst.m_speed = 7.0f;
        C_NetMessage msg;
        msg.WriteBits(0, 1);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Same3(dst.m_rate, 0, 0, 0) && dst.m_speed == 7.0f);
    }
    // A small rate (squared under 0.1) dropped; just above kept.
    {
        dst.m_net = {0, 0, 0, 0};
        dst.m_read[1] = &g_delta;
        C_NetMessage msg;
        msg.WriteBits(1, 1);
        msg.WriteBits(1, 1);
        const float d[3] = {0.03f, 0, 0};
        msg.WriteQuantized(d, &g_delta, 7);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Same3(dst.m_rate, 0, 0, 0));
        C_NetMessage msg2;
        msg2.WriteBits(1, 1);
        msg2.WriteBits(1, 1);
        const float d2[3] = {0.0356f, 0, 0};
        msg2.WriteQuantized(d2, &g_delta, 7);
        Rewind(msg2);
        dst.m_net = {0, 0, 0, 0};
        dst.Read(&msg2);
        // (0.0354 read: a rate of 0.354, squared 0.125.)
        CHECK(dst.m_rate.x > 0.3f && Near(dst.m_speed, dst.m_rate.x));
    }
    // The rate read: below 0.02 squared dropped.
    dst.m_flags = 7 | NET_TRACK_SEND_RATE;
    {
        C_NetMessage msg;
        msg.WriteBits(0, 1);
        msg.WriteBits(1, 1);
        const float r[3] = {3, 0, -4};
        msg.WriteQuantized(r, &g_rate, 7);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Near(dst.m_rate.x, 3, 0.06f) && Near(dst.m_rate.z, -4, 0.06f) && Near(dst.m_speed, 5, 0.1f));
        C_NetMessage msg2;
        msg2.WriteBits(0, 1);
        msg2.WriteBits(1, 1);
        const float r2[3] = {0.1f, 0, 0.1f};
        msg2.WriteQuantized(r2, &g_rate, 7);
        Rewind(msg2);
        dst.m_speed = 6;
        dst.Read(&msg2);
        CHECK(Same3(dst.m_rate, 0, 0, 0) && dst.m_speed == 6);
        // Squared 0.0216 (0.147 read): kept.
        C_NetMessage msg5;
        msg5.WriteBits(0, 1);
        msg5.WriteBits(1, 1);
        const float r5[3] = {0.15f, 0, 0};
        msg5.WriteQuantized(r5, &g_rate, 7);
        Rewind(msg5);
        dst.Read(&msg5);
        CHECK(Near(dst.m_rate.x, 0.147f, 0.001f) && Near(dst.m_speed, 0.147f, 0.001f));
        // A 0 bit: the rate kept.
        dst.m_rate = {1, 2, 3, 4};
        C_NetMessage msg3;
        msg3.WriteBits(0, 1);
        msg3.WriteBits(0, 1);
        Rewind(msg3);
        dst.Read(&msg3);
        CHECK(Same(dst.m_rate, {1, 2, 3, 4}));
    }
    // The rate after an absolute value too.
    {
        C_NetMessage msg;
        msg.WriteBits(1, 1);
        msg.WriteBits(0, 1);
        const float v[3] = {1, 2, 3};
        msg.WriteQuantized(v, &g_value, 7);
        msg.WriteBits(1, 1);
        const float r[3] = {0, 2, 0};
        msg.WriteQuantized(r, &g_rate, 7);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Near(dst.m_rate.y, 2, 0.06f) && Near(dst.m_net.z, 3, 0.01f));
    }
    // Extrapolated: the target leads by the rate x the lag; the current at the value.
    dst.m_flags = 7 | NET_TRACK_EXTRAPOLATE;
    dst.m_rate = {10, 0, -20, 0};
    dst.m_lag = 0.5f;
    dst.m_net = {1, 2, 3, 8};
    {
        C_NetMessage msg;
        msg.WriteBits(0, 1);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Same(dst.m_target, {6, 2, -7, 8}) && Same(dst.m_current, {1, 2, 3, 8}));
    }
    // Keep the current: only the target moves.
    dst.m_flags = 7 | NET_TRACK_KEEP_CURRENT;
    dst.m_current = {9, 9, 9, 9};
    {
        C_NetMessage msg;
        msg.WriteBits(0, 1);
        Rewind(msg);
        dst.Read(&msg);
        CHECK(Same(dst.m_target, dst.m_net) && Same(dst.m_current, {9, 9, 9, 9}));
    }
}

// ---- the quantisation's tests and WriteQuantized's result -----------------------------------------------------------

void TestQuantizationTests()
{
    S_NetQuantization q;
    q.Setup({2.0f, 4.0f, 8.0f, 0.0f}, {-1.0f, -3.0f, -5.0f, 0.0f}, {0.1f, 0.2f, 0.3f, 0.0f}, 1);
    // The range per component, its ends included.
    const float lo[3] = {q.offset[0], q.offset[1], q.offset[2]};
    const float hi[3] = {q.range40[0] + q.offset[0], q.range40[1] + q.offset[1], q.range40[2] + q.offset[2]};
    CHECK(q.Contains(lo) && q.Contains(hi));
    for (int i = 0; i < 3; ++i) {
        float v[3] = {0, 0, 0};
        v[i] = std::nextafter(lo[i], -100.0f);
        CHECK(!q.Contains(v));
        v[i] = std::nextafter(hi[i], 100.0f);
        CHECK(!q.Contains(v));
        v[i] = lo[i];
        CHECK(q.Contains(v));
    }
    // (Component i's own range: x [-1, 1], y [-3, 1], z [-5, 3].)
    const float a[3] = {0.0f, 0.0f, 2.5f}, b[3] = {0.0f, -2.5f, 0.0f}, c[3] = {2.5f, 0.0f, 0.0f};
    CHECK(q.Contains(a) && q.Contains(b) && !q.Contains(c));
    const float e[3] = {0.0f, -4.0f, 0.0f}, f[3] = {0.0f, 0.0f, -4.0f};
    CHECK(!q.Contains(e) && q.Contains(f));
    const float g[3] = {0.0f, 1.5f, 3.5f};
    CHECK(!q.Contains(g));
    // Within the steps: each below its own, strictly.
    const float s0[3] = {0.09f, 0.19f, 0.29f};
    CHECK(q.WithinSteps(s0));
    const float s1[3] = {0.1f, 0.0f, 0.0f}, s2[3] = {0.0f, 0.2f, 0.0f}, s3[3] = {0.0f, 0.0f, -0.3f};
    CHECK(!q.WithinSteps(s1) && !q.WithinSteps(s2) && !q.WithinSteps(s3));
    const float s4[3] = {0.0f, 0.15f, 0.25f};
    CHECK(q.WithinSteps(s4));
    // WriteQuantized's result: any component above its step (whatever the mask); at the step: not.
    C_NetMessage msg;
    const float w0[3] = {0.1f, 0.2f, 0.3f};
    CHECK(!msg.WriteQuantized(w0, &q, 7));
    const float w1[3] = {0.11f, 0.0f, 0.0f}, w2[3] = {0.0f, -0.21f, 0.0f}, w3[3] = {0.0f, 0.0f, 0.31f};
    CHECK(msg.WriteQuantized(w1, &q, 7) && msg.WriteQuantized(w2, &q, 7) && msg.WriteQuantized(w3, &q, 0));
    const float w4[3] = {0.0f, 0.15f, 0.25f}, w5[3] = {0.15f, 0.0f, 0.0f}, w6[3] = {0.0f, 0.25f, 0.0f};
    CHECK(!msg.WriteQuantized(w4, &q, 7) && msg.WriteQuantized(w5, &q, 7) && msg.WriteQuantized(w6, &q, 1));
}

#include "net_object_test.inc"
#include "net_channel_test.inc"

// ---- the direction track -------------------------------------------------------------------------------------------

void TestDirDefaults()
{
    g_netUpdatePeriod = 3.0f;
    C_NetDirTrack d;
    CHECK(d.m_flags == 3 && g_netUpdatePeriod == 0.1f && d.m_speed == 40.0f && d.m_snapDistance == 3.0f);
    CHECK(d.m_read[0] == &g_netAngleQuantization && d.m_write[0] == &g_netAngleQuantization &&
          d.m_read[1] == &g_netAngleDeltaQuantization && d.m_write[1] == &g_netAngleDeltaQuantization &&
          d.m_read[2] == &g_netAngleRateQuantization && d.m_write[2] == &g_netAngleRateQuantization);
    CHECK(g_netAngleQuantization.offset[0] == -3.1415927f && g_netAngleQuantization.range40[1] == 6.2831855f &&
          g_netAngleQuantization.deadZone[2] == 0.0122f && g_netAngleQuantization.unknown70 == 100);
    CHECK(g_netAngleDeltaQuantization.offset[0] == -0.15707964f && g_netAngleDeltaQuantization.range[2] == 0.31415927f);
    CHECK(g_netAngleRateQuantization.offset[1] == -0.07853982f && g_netAngleRateQuantization.range40[0] == 0.15707964f);
    CHECK(g_netAngleQuantization.bits[0] == 10 && g_netAngleDeltaQuantization.bits[0] == 5 &&
          g_netAngleRateQuantization.bits[0] == 4);
    // Made once: a later track does not set them again.
    g_netAngleQuantization.unknown70 = 7;
    C_NetDirTrack again;
    CHECK(g_netAngleQuantization.unknown70 == 7);
    g_netAngleQuantization.unknown70 = 100;
    // Its quantisations cannot be replaced; the rate flags are dropped.
    S_NetQuantization q;
    d.SetReadQuantizations(&q, &q, &q);
    d.SetWriteQuantizations(&q, &q, &q);
    CHECK(d.m_read[0] == &g_netAngleQuantization && d.m_write[2] == &g_netAngleRateQuantization);
    d.SetFlags(0xFFFFFFFFu);
    CHECK(d.m_flags == 0xFFFC3FFFu);
    // Reset: the direction and the previous angles too.
    d.m_direction = {1, 1, 1, 1};
    d.m_previous = {2, 2, 2, 2};
    d.Reset();
    CHECK(d.m_flags == 3 && Same(d.m_direction, {0, 0, 0, 1}) && Same(d.m_previous, {0, 0, 0, 2}) &&
          d.m_read[0] == nullptr);
    C_NetDirTrack e;
    e.BeginWrite(nullptr);
}

void TestDirSet()
{
    C_NetDirTrack d;
    d.m_current = {0.5f, 0.25f, 0.125f, 3};
    const float s = std::sqrt(0.5f);
    d.SetDirection({s, 0, s, 7}, 1.5f);
    CHECK(Same(d.m_previous, {0.5f, 0.25f, 0.125f, 3}));
    CHECK(Near(d.m_current.x, 0.7853982f) && d.m_current.y == 0.0f && d.m_current.z == 1.5f && d.m_current.w == 3);
    CHECK(Same(d.m_net, d.m_current) && Same(d.m_target, d.m_current) && Same(d.m_direction, {s, 0, s, 7}));
    CHECK(&d.GetCurrent() == &d.m_direction && d.GetAngle() == 1.5f);
    d.SetDirection({0, 1, 0, 0}, 0);
    CHECK(Near(d.m_current.y, 1.5707964f) && d.m_current.x == 0.0f);
    d.SetDirection({-1, 0, 0, 0}, 0);
    CHECK(Near(d.m_current.x, 3.1415927f));
    d.SetAngle(2.5f);
    CHECK(d.GetAngle() == 2.5f && d.m_current.z == 2.5f);
}

void TestDirSign()
{
    C_NetDirTrack d;
    // The yaw's turn since SetDirection: 0 decreasing, 1 increasing, 2 within 0.001.
    struct {
        float from, to;
        uint32_t sign;
    } cases[] = {{0.0f, 0.5f, 1}, {0.5f, 0.0f, 0}, {0.3f, 0.3005f, 2}, {0.3f, 0.2995f, 2}, {0.3f, 0.302f, 1},
                 {0.3f, 0.298f, 0}, {0.001f, 0.0f, 2}, {0.0f, 0.001f, 2}, {0.3f, 0.2988f, 0}, {0.3f, 0.3012f, 1}};
    for (auto &c : cases) {
        d.m_previous.x = c.from;
        d.m_current.x = c.to;
        C_NetMessage msg;
        d.EndWrite(&msg);
        CHECK(msg.BitPosition() == 2u);
        Rewind(msg);
        CHECK(Bits(msg, 2) == c.sign);
    }
    // Written after the value.
    d.SetDirection({1, 0, 0, 0}, 0);
    d.m_previous.x = -0.5f;
    C_NetMessage msg;
    S_vector4 last = {};
    d.Write(&msg, false, &last);
    Rewind(msg);
    CHECK(Bits(msg, 1) == 1 && Bits(msg, 1) == 0);
    float v[3];
    msg.ReadQuantized(v, &g_netAngleQuantization, 3);
    CHECK(Bits(msg, 2) == 1);
}

void TestDirRead()
{
    C_NetDirTrack d;
    // The sign read (2 keeps the last); the rate from current - target over the period.
    d.m_current = {0.3f, 0.1f, 0.0f, 0};
    d.m_target = {0.2f, 0.0f, 0.0f, 0};
    d.m_turnSign = 1;
    {
        C_NetMessage msg;
        msg.WriteBits(2, 2);
        msg.WriteBits(1, 1);
        Rewind(msg);
        d.EndRead(&msg);
        CHECK(d.m_turnSign == 1 && Near(d.m_rate.x, 1.0f, 1e-5f) && Near(d.m_rate.y, 1.0f, 1e-5f) &&
              d.m_rate.z == 0.0f && Near(d.m_speed, std::sqrt(2.0f), 1e-5f) && msg.BitPosition() == 2u);
    }
    // The sign against the change: x flipped.
    {
        C_NetMessage msg;
        msg.WriteBits(0, 2);
        Rewind(msg);
        d.EndRead(&msg);
        CHECK(d.m_turnSign == 0 && Near(d.m_rate.x, -1.0f, 1e-5f));
        d.m_current.x = 0.1f;
        C_NetMessage msg2;
        msg2.WriteBits(1, 2);
        Rewind(msg2);
        d.EndRead(&msg2);
        CHECK(d.m_turnSign == 1 && Near(d.m_rate.x, 1.0f, 1e-5f));
        // Agreeing: kept.
        C_NetMessage msg3;
        msg3.WriteBits(0, 2);
        Rewind(msg3);
        d.EndRead(&msg3);
        CHECK(Near(d.m_rate.x, -1.0f, 1e-5f));
    }
    // No change: nothing (the rate kept), the sign still read.
    {
        d.m_current = d.m_target = {0.4f, 0, 0, 0};
        d.m_rate = {5, 5, 5, 5};
        C_NetMessage msg;
        msg.WriteBits(1, 2);
        Rewind(msg);
        d.EndRead(&msg);
        CHECK(Same(d.m_rate, {5, 5, 5, 5}) && d.m_turnSign == 1);
    }
    // More than half the span's magnitude (2 pi x sqrt 3 / 2): the short way round, the angles wrapped.
    {
        d.m_current = {3.0f, 3.0f, 3.0f, 0};
        d.m_target = {-3.0f, -3.0f, -3.0f, 0};
        d.m_turnSign = 2;
        C_NetMessage msg;
        msg.WriteBits(2, 2);
        Rewind(msg);
        d.EndRead(&msg);
        // 2 pi - 6 = 0.2832: rates 2.832 each.
        CHECK(Near(d.m_rate.x, (6.2831855f - 6.0f) * 10.0f, 1e-4f) && Near(d.m_rate.z, d.m_rate.x) &&
              Near(d.m_rate.y, d.m_rate.x));
        // Not over half: as is.
        d.m_current = {2.0f, 2.0f, 2.0f, 0};
        d.m_target = {-1.0f, -1.0f, -1.0f, 0};
        C_NetMessage msg2;
        msg2.WriteBits(2, 2);
        Rewind(msg2);
        d.EndRead(&msg2);
        // Over 8: clamped to 8 along the rate.
        CHECK(d.m_speed == 8.0f && Near(d.m_rate.x, 8.0f / std::sqrt(3.0f), 1e-4f) && Near(d.m_rate.y, d.m_rate.x) &&
              Near(d.m_rate.z, d.m_rate.x));
        // Between 8 and 12: clamped too; 7.5: kept.
        d.m_current = {1.0f, 0.0f, 0.0f, 0};
        d.m_target = {0.0f, 0.0f, 0.0f, 0};
        C_NetMessage msg3;
        msg3.WriteBits(2, 2);
        Rewind(msg3);
        d.EndRead(&msg3);
        CHECK(d.m_speed == 8.0f && Near(d.m_rate.x, 8.0f, 1e-5f));
        d.m_current = {0.0f, 0.0f, 0.75f, 0};
        C_NetMessage msg4;
        msg4.WriteBits(2, 2);
        Rewind(msg4);
        d.EndRead(&msg4);
        CHECK(Near(d.m_speed, 7.5f, 1e-5f) && Near(d.m_rate.z, 7.5f, 1e-5f));
    }
    // Wrapped twice when far beyond.
    {
        d.m_current = {6.0f, 0.0f, 0.0f, 0};
        d.m_target = {-6.0f, 0.0f, 0.0f, 0};
        C_NetMessage msg;
        msg.WriteBits(2, 2);
        Rewind(msg);
        d.EndRead(&msg);
        // 2 pi - 12 = -5.717: not over pi, kept: rate -57 clamped to 8 (x negative: -8).
        CHECK(d.m_speed == 8.0f && Near(d.m_rate.x, -8.0f, 1e-4f));
        d.m_current = {-3.0f, 0.0f, 0.0f, 0};
        d.m_target = {3.0f, 0.0f, 0.0f, 0};
        C_NetMessage msg2;
        msg2.WriteBits(2, 2);
        Rewind(msg2);
        d.EndRead(&msg2);
        // 2 pi + 6 = 12.28, twice over pi: 6 - 2 pi = -0.283.
        CHECK(Near(d.m_rate.x, (6.0f - 6.2831855f) * 10.0f, 1e-3f));
        // Half the span exactly not exceeded (5 of 5.44): as is.
        d.m_current = {-1.0f, 0.0f, 0.0f, 0};
        d.m_target = {4.0f, 0.0f, 0.0f, 0};
        C_NetMessage msg3;
        msg3.WriteBits(2, 2);
        Rewind(msg3);
        d.EndRead(&msg3);
        CHECK(Near(d.m_rate.x, -8.0f, 1e-4f));
    }
}

void TestDirUpdate()
{
    C_NetDirTrack d;
    d.m_speed = 1.0f;
    // A step towards the target; the direction from the angles.
    d.m_current = {0.0f, 0.0f, 0.0f, 0};
    d.m_target = {0.5f, 0.0f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 0.1f, 1e-6f) && Near(d.m_direction.x, std::cos(0.1f)) &&
          Near(d.m_direction.z, std::sin(0.1f)) && d.m_direction.y == 0.0f);
    // Beyond 0.785 the yaw moves at the rate's x.
    d.m_current = {0.0f, 0.0f, 0.0f, 0};
    d.m_target = {0.0f, 0.0f, 0.9f, 0};
    d.m_rate = {2.0f, 0, 0, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 0.2f, 1e-6f) && Near(d.m_current.z, 0.1f, 1e-6f));
    // Reached: the target; the angles wrapped above pi.
    d.m_current = {3.1f, 0.0f, 0.0f, 0};
    d.m_target = {3.2f, 0.0f, 0.0f, 5};
    d.Update(0.5f);
    CHECK(Near(d.m_current.x, 3.2f - 6.2831855f, 1e-6f) && d.m_current.w == 5);
    // The short way round: from 3.0 to -3.0 goes up past pi.
    d.m_snapDistance = 10.0f;
    d.m_current = {3.0f, 0.0f, 0.0f, 0};
    d.m_target = {-3.0f, 0.0f, 0.0f, 0};
    d.m_speed = 1.0f;
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 3.1f, 1e-5f));
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 3.2f - 6.2831855f, 1e-5f));
    // Not past half the span: the long way (and over 0.785: the yaw at the rate's x).
    d.m_current = {1.5f, 0.0f, 0.0f, 0};
    d.m_target = {-1.5f, 0.0f, 0.0f, 0};
    d.m_rate = {-1.0f, 0, 0, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 1.4f, 1e-5f));
    d.m_rate = {0, 0, 0, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 1.4f, 1e-5f));
    // Snaps beyond the snap distance or within 1e-5.
    d.m_snapDistance = 3.0f;
    d.m_current = {0.0f, 0.0f, 0.0f, 0};
    d.m_target = {0.0f, 3.1f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(d.m_current.y == 3.1f && Near(d.m_direction.y, std::sin(3.1f)));
    d.m_target = {0.0f, 3.1f, 0.000005f, 0};
    d.Update(0.1f);
    CHECK(d.m_current.z == 0.000005f);
    // The target moving at the rate.
    d.m_flags = NET_TRACK_MOVE_TARGET;
    d.m_current = {0, 0, 0, 0};
    d.m_target = {0, 0, 0, 0};
    d.m_rate = {0, 1.0f, 0, 0};
    d.m_speed = 0.5f;
    d.Update(0.2f);
    CHECK(Near(d.m_target.y, 0.2f) && Near(d.m_current.y, 0.1f, 1e-6f));
    // The short way round in y and z; twice wrapped (from -3 to 3: down past -pi).
    d.m_flags = 0;
    d.m_snapDistance = 20.0f;
    d.m_speed = 1.0f;
    d.m_current = {3.0f, 3.0f, 3.0f, 0};
    d.m_target = {-3.0f, -3.0f, -3.0f, 0};
    d.Update(0.1f);
    {
        const float step = 0.1f / std::sqrt(3.0f);
        CHECK(Near(d.m_current.x, 3.0f + step, 1e-5f) && Near(d.m_current.y, 3.0f + step, 1e-5f) &&
              Near(d.m_current.z, 3.0f + step, 1e-5f));
    }
    d.m_current = {-3.0f, 0.0f, 0.0f, 0};
    d.m_target = {3.0f, 0.0f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, -3.1f, 1e-5f));
    d.m_current = {0.0f, -3.0f, 0.0f, 0};
    d.m_target = {0.0f, 3.0f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.y, -3.1f, 1e-5f));
    d.m_current = {0.0f, 0.0f, -3.0f, 0};
    d.m_target = {0.0f, 0.0f, 3.0f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.z, -3.1f, 1e-5f));
    // Under half the span (4 of 5.44): the long way, in y.
    d.m_current = {0.0f, 2.0f, 0.0f, 0};
    d.m_target = {0.0f, -2.0f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.y, 1.9f, 1e-5f));
    // The current angles wrapped after the move, twice when far out.
    d.m_current = {9.5f, 9.5f, 9.5f, 0};
    d.m_target = {9.55f, 9.55f, 9.55f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.x, 9.55f - 2.0f * 6.2831855f, 1e-4f) && Near(d.m_current.y, d.m_current.x) &&
          Near(d.m_current.z, d.m_current.x));
    // Exactly pi: kept.
    d.m_speed = 40.0f;
    d.m_current = {3.0f, 0.0f, 0.0f, 0};
    d.m_target = {3.1415927f, 0.0f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(d.m_current.x == 3.1415927f);
    // Exactly the snap distance: a step, not a jump.
    d.m_speed = 1.0f;
    d.m_snapDistance = 3.0f;
    d.m_current = {0.0f, 0.0f, 0.0f, 0};
    d.m_target = {0.0f, 3.0f, 0.0f, 0};
    d.Update(0.1f);
    CHECK(Near(d.m_current.y, 0.1f, 1e-6f));
    // The target moving in x and z too.
    d.m_flags = NET_TRACK_MOVE_TARGET;
    d.m_current = d.m_target = {0, 0, 0, 0};
    d.m_rate = {1e-9f, 0, -1e-9f, 0};
    d.Update(0.1f);
    CHECK(d.m_target.x == 1e-10f && d.m_target.z == -1e-10f);
    d.m_flags = 0;
    // Pitch in the direction.
    d.m_flags = 0;
    d.m_current = {0.0f, 0.5f, 0.0f, 0};
    d.m_target = d.m_current;
    d.Update(0.1f);
    CHECK(Near(d.m_direction.y, std::sin(0.5f)) && Near(d.m_direction.x, std::cos(0.5f)));
}

void TestDirRoundTrip()
{
    // The server's direction to a client: the angles, the sign, the rate worked out there.
    g_netServerSide = true;
    C_NetDirTrack server, client;
    server.SetDirection({1, 0, 0, 0}, 0.0f);
    S_vector4 last = {};
    C_NetMessage first;
    server.Write(&first, true, &last);
    Rewind(first);
    g_netServerSide = false;
    client.Read(&first);
    CHECK(Near(client.m_net.x, 0.0f, 0.007f));
    g_netServerSide = true;
    const float a = 0.3f;
    server.SetDirection({std::cos(a), 0, std::sin(a), 0}, 0.0f);
    C_NetMessage msg;
    server.Write(&msg, true, &last);
    Rewind(msg);
    g_netServerSide = false;
    client.Read(&msg);
    CHECK(Near(client.m_net.x, 0.3f, 0.007f) && client.m_turnSign == 1);
    CHECK(Same(client.m_current, client.m_net));
    client.Update(0.1f);
    CHECK(Near(client.GetCurrent().x, std::cos(client.m_current.x)));
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestDefaults();
    TestQuantizationSetters();
    TestSetValue();
    TestRate();
    TestUpdate();
    TestWrite();
    TestRead();
    TestQuantizationTests();
    TestDirDefaults();
    TestDirSet();
    TestDirSign();
    TestDirRead();
    TestDirUpdate();
    TestDirRoundTrip();
    TestMessage();
    TestProperty();
    TestPropertyWrite();
    TestObjectBasics();
    TestObjectQueues();
    TestObjectWrite();
    TestObjectRead();
    TestObjectUpdate();
    TestViewCone();
    TestReplicationBasics();
    TestReplicationWrite();
    TestReplicationRead();
    TestOwnership();
    InitPerfTime();
    TestUnwrap();
    TestRefCounted();
    TestPool();
    TestPacket();
    TestChannel();
    TestReliable();
    TestOrdered();
    TestMessages();
    std::printf("net: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
