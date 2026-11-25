// Net part 8: the replicated objects (net/net_object.cpp: the properties, the objects, their message queues, the
// replication's write and read, the view cone) against the original executable's. MinGW; build_repl.bat; runs
// repl_diff.exe ("repl_diff: N checks, 0 failures").
//
// Each round makes a replication in each program (the original's by its constructor, its objects and properties by
// its own code), the same quantisations (the test's, shared; the direction tracks' are each program's globals), the
// factories by id (the state readers and message factories: the test's, logging, in each program's calling
// convention) and the local object's id, then runs a random sequence of calls on both: objects made, found, removed,
// deleted; properties added, their flags, fields, lag and values; messages queued (the test's, in each program's
// layout), popped, cleared; the send masks, the live and forwarding flags, the state handlers; Update (the
// performance counter faked on both: QueryPerformanceCounter by the original's import, the port's by a renaming
// header), Refresh, SetLag, the quantisations, the epochs, the view cone; Write for a player (the bits compared) and
// Read of what an earlier Write gave. After each call the logs, the results and the state are compared: every
// object's fields, its properties' bytes (the original's layout: the port's matches it), its queues (the messages by
// their serial numbers, their fields), the replication's fields and the local object.
// HD2_REPL_ROUNDS: rounds (default 20,000; HD2_REPL_QUICK: 5,000); HD2_REPL_SEED; HD2_REPL_FIRST_CHANCE.
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

#define protected public
#define private public
#include "ls3d/math.h"
#include "net/bitstream.h"
#include "net/net_object.h"
#include "net/net_track.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

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

// ---- the performance counter (both sides) ----
int64_t g_clock;
extern "C" BOOL WINAPI FakeQueryPerformanceCounter(LARGE_INTEGER *now)
{
    now->QuadPart = g_clock;
    return TRUE;
}
extern "C" BOOL WINAPI FakeQueryPerformanceFrequency(LARGE_INTEGER *frequency)
{
    frequency->QuadPart = 1000;
    return TRUE;
}

// ---- the messages: each side's objects, a serial number each (the same on both: made by the same calls) ----
int g_serial[2];
struct S_MsgInfo {
    bool deleted;
};
std::map<int, S_MsgInfo> g_msgs[2];

class PMsg : public C_NetObjectMessage {
public:
    PMsg(int type, int serial) : C_NetObjectMessage(type), serial(serial) {}
    ~PMsg() override
    {
        Log("msg delete " + N(serial));
        g_msgs[0][serial].deleted = true;
    }
    void Write(C_BitStream *stream) override
    {
        Log("msg write " + N(serial));
        stream->WriteBits(static_cast<uint32_t>(type), 4);
        stream->WriteBits(static_cast<uint32_t>(serial & 0xFF), 8);
    }
    void Read(C_BitStream *stream) override
    {
        const uint32_t v = stream->ReadBits(8);
        Log("msg read " + N(serial) + " " + N(v));
    }
    int serial;
};

namespace {

template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))

// ---- the original's functions ----
const uint32_t O_WRITE_BITS = 0x0073d320, O_READ_BITS = 0x0073d3d0, O_MSG_CTOR = 0x0073d740, O_SMSG_CTOR = 0x0073fb40,
               O_SMSG_VTABLE = 0x00810630;
const uint32_t O_R_CTOR = 0x0073f020, O_R_RESET = 0x0073f150, O_R_SET_READ = 0x0073f620, O_R_SET_WRITE = 0x0073f5c0,
               O_R_UPDATE = 0x0073f550, O_R_REFRESH = 0x0073f590, O_R_WRITE = 0x0073f1e0, O_R_READ = 0x0073f320,
               O_R_FIND = 0x0073f9c0, O_R_CREATE = 0x0073f680, O_R_REMOVE = 0x0073f900, O_R_DELETE_ALL = 0x0073f970,
               O_R_SET_LAG = 0x0073fa30, O_R_DTOR = 0x0073f0d0;
const uint32_t O_O_ADD = 0x0073e460, O_O_GET = 0x0073e690, O_O_QUEUE = 0x0073e970, O_O_POP = 0x0073e9d0,
               O_O_CLEAR = 0x0073ea30, O_O_FIELD2 = 0x0073ed40, O_O_HAS_DATA = 0x0073edc0, O_O_SINCE = 0x0073ede0,
               O_O_GET_READ = 0x0073ec60, O_O_GET_WRITE = 0x0073ec80;
const uint32_t O_P_SET_FLAGS = 0x0073e0e0, O_P_SET_FIELD = 0x0073e110, O_P_SET_LAG = 0x0073e0b0;
const uint32_t O_PERIOD = 0x00982f90, O_SERVER = 0x00982f94, O_FREQUENCY = 0x00982df8;
const uint32_t O_ANGLE_Q[3] = {0x00982f10, 0x00982e90, 0x00982e10};
const uint32_t O_QPC_SLOT = 0x0080f1c8, O_QPF_SLOT = 0x0080f1c4;

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
void THISCALL ONoLock(void *) {}

// The original's messages: its constructor's fields, the test's vtable (a copy of its own, Write / Read / the
// deleting destructor the test's), the serial at +0x30.
void *g_oMsgVtable[16];
void THISCALL OMsgWrite(void *self, void *stream)
{
    const int serial = OF<int>(self, 0x30);
    Log("msg write " + N(serial));
    reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(O_WRITE_BITS)(stream, static_cast<uint32_t>(OF<int>(self, 0x1C)), 4);
    reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(O_WRITE_BITS)(stream, static_cast<uint32_t>(serial & 0xFF), 8);
}
void THISCALL OMsgRead(void *self, void *stream)
{
    const uint32_t v = reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(O_READ_BITS)(stream, 8);
    Log("msg read " + N(OF<int>(self, 0x30)) + " " + N(v));
}
void *THISCALL OMsgDelete(void *self, unsigned flags)
{
    const int serial = OF<int>(self, 0x30);
    Log("msg delete " + N(serial));
    g_msgs[1][serial].deleted = true;
    if (flags & 1)
        ODelete(self);
    return self;
}
void *OMakeMsg(int type, int serial)
{
    void *m = ONew(0x40);
    reinterpret_cast<void *(THISCALL *)(void *, int)>(O_SMSG_CTOR)(m, type);
    OF<void *>(m, 0) = g_oMsgVtable;
    OF<int>(m, 0x30) = serial;
    g_msgs[1][serial].deleted = false;
    return m;
}
PMsg *PMakeMsg(int type, int serial)
{
    auto *m = new PMsg(type, serial);
    g_msgs[0][serial].deleted = false;
    return m;
}

// ---- the handlers and factories (each side's convention; the port's: cdecl makers, the original's: the id in ECX) ----
void FAST PReadState(void *context, C_BitStream *stream)
{
    const uint32_t v = stream->ReadBits(8);
    Log("readState " + N(reinterpret_cast<intptr_t>(context)) + " " + N(v));
}
void FAST PWriteState(void *context, C_BitStream *stream)
{
    Log("writeState " + N(reinterpret_cast<intptr_t>(context)));
    stream->WriteBits(static_cast<uint32_t>(reinterpret_cast<intptr_t>(context) * 7 + 3) & 0xFF, 8);
}
C_NetObjectMessage *FAST PFactory(void *context, C_BitStream *stream, bool write)
{
    if (write) {
        Log("factory end " + N(reinterpret_cast<intptr_t>(context)));
        stream->WriteBits(0, 4);
        return nullptr;
    }
    const uint32_t type = stream->ReadBits(4);
    Log("factory read " + N(reinterpret_cast<intptr_t>(context)) + " " + N(type));
    if (!type)
        return nullptr;
    return PMakeMsg(static_cast<int>(type), 1000 + g_serial[0]++);
}
NetStateHandler PMakeStateReader(int id)
{
    Log("makeStateReader " + N(id));
    return id % 3 ? &PReadState : nullptr;
}
NetMessageFactory PMakeFactory(int id)
{
    Log("makeFactory " + N(id));
    return id % 2 == 0 ? &PFactory : nullptr;
}

void FAST OReadState(void *context, void *stream)
{
    const uint32_t v = reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(O_READ_BITS)(stream, 8);
    Log("readState " + N(reinterpret_cast<intptr_t>(context)) + " " + N(v));
}
void FAST OWriteState(void *context, void *stream)
{
    Log("writeState " + N(reinterpret_cast<intptr_t>(context)));
    reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(O_WRITE_BITS)(
        stream, static_cast<uint32_t>(reinterpret_cast<intptr_t>(context) * 7 + 3) & 0xFF, 8);
}
void *FAST OFactory(void *context, void *stream, bool write)
{
    if (write) {
        Log("factory end " + N(reinterpret_cast<intptr_t>(context)));
        reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(O_WRITE_BITS)(stream, 0, 4);
        return nullptr;
    }
    const uint32_t type = reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(O_READ_BITS)(stream, 4);
    Log("factory read " + N(reinterpret_cast<intptr_t>(context)) + " " + N(type));
    if (!type)
        return nullptr;
    return OMakeMsg(static_cast<int>(type), 1000 + g_serial[1]++);
}
void *FAST OMakeStateReader(int id)
{
    Log("makeStateReader " + N(id));
    return id % 3 ? reinterpret_cast<void *>(&OReadState) : nullptr;
}
void *FAST OMakeFactory(int id)
{
    Log("makeFactory " + N(id));
    return id % 2 == 0 ? reinterpret_cast<void *>(&OFactory) : nullptr;
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
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
float R(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
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
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 200) + "\n     port: " + y.substr(0, 200);
            break;
        }
    }
    Check(false, s);
}

// ---- the quantisations (the test's, shared) ----
alignas(16) S_NetQuantization g_q[6];
uintptr_t OrigQ(const void *q)
{
    if (q == &g_netAngleQuantization)
        return O_ANGLE_Q[0];
    if (q == &g_netAngleDeltaQuantization)
        return O_ANGLE_Q[1];
    if (q == &g_netAngleRateQuantization)
        return O_ANGLE_Q[2];
    return reinterpret_cast<uintptr_t>(q);
}
void MakeQuantizations()
{
    for (int i = 0; i < 6; ++i) {
        const float span = i < 2 ? R(500.0f, 5000.0f) : i < 4 ? R(1.0f, 40.0f) : R(10.0f, 200.0f);
        const float step = i < 2 ? R(0.005f, 0.05f) : i < 4 ? R(0.002f, 0.05f) : R(0.01f, 0.1f);
        g_q[i].Setup(S_vector4{span, span, span, 0.0f}, S_vector4{-span * 0.5f, -span * 0.5f, -span * 0.5f, 0.0f},
                     S_vector4{step, step, step, 0.0f}, 100);
    }
}
S_NetQuantization *RandomQ(int kind)
{
    // kind 0 value, 1 delta, 2 rate; sometimes none.
    if (P(0.15))
        return nullptr;
    return &g_q[kind * 2 + I(0, 1)];
}

// ---- the world ----
C_NetReplication *g_p;
alignas(16) uint8_t g_oStore[0x100];
void *g_o = g_oStore;

// The objects: the port's and the original's in the same order (their vectors).
std::vector<C_NetObject *> &PObjects() { return g_p->m_objects; }
int OObjectCount() { return static_cast<int>((OF<uint8_t *>(g_o, 0x24) - OF<uint8_t *>(g_o, 0x20)) / 4); }
void *OObject(int i) { return OF<void **>(g_o, 0x20)[i]; }
int PropertyCountP(C_NetObject *o) { return static_cast<int>(o->m_properties.size()); }
int PropertyCountO(void *o) { return OF<uint8_t *>(o, 0x24) ? static_cast<int>((OF<uint8_t *>(o, 0x28) - OF<uint8_t *>(o, 0x24)) / 4) : 0; }
void *OProperty(void *o, int i) { return OF<void **>(o, 0x24)[i]; }

// The original's std::list<T *> (+4 the head, +8 the size; a node: next, prev, the value).
std::vector<void *> OList(void *o, int at)
{
    std::vector<void *> v;
    uint8_t *head = OF<uint8_t *>(o, at + 4);
    if (!head)
        return v;
    for (uint8_t *n = OF<uint8_t *>(head, 0); n != head; n = OF<uint8_t *>(n, 0))
        v.push_back(OF<void *>(n, 8));
    return v;
}

void OListFree(void *o, int at)
{
    uint8_t *head = OF<uint8_t *>(o, at + 4);
    if (!head)
        return;
    for (uint8_t *n = OF<uint8_t *>(head, 0); n != head;) {
        uint8_t *next = OF<uint8_t *>(n, 0);
        ODelete(n);
        n = next;
    }
    OF<void *>(head, 0) = head;
    OF<void *>(head, 4) = head;
    OF<int>(o, at + 8) = 0;
}

C_NetObject *PObj(int i);
bool HeldByAnotherQueue(const C_NetObjectMessage *m, int i, bool outgoing)
{
    for (int k = -1; k < static_cast<int>(PObjects().size()); ++k)
        for (int q = 0; q < 2; ++q) {
            if (k == i && (q != 0) == outgoing)
                continue;
            const std::list<C_NetObjectMessage *> &l = q ? PObj(k)->m_outgoing : PObj(k)->m_received;
            if (std::find(l.begin(), l.end(), m) != l.end())
                return true;
        }
    return false;
}

std::string MsgFields(const C_NetObjectMessage *m)
{
    uint32_t age;
    std::memcpy(&age, &m->age, 4);
    return N(static_cast<const PMsg *>(m)->serial) + ":" + N(m->type) + "," + N(m->sentTo) + "," + N(m->refCount) + "," + Hex(age) + "," +
           N(m->fromClient);
}
std::string MsgFields(void *m)
{
    return N(OF<int>(m, 0x30)) + ":" + N(OF<int>(m, 0x1C)) + "," + N(OF<int>(m, 0x20)) + "," + N(OF<int>(m, 0x24)) + "," +
           Hex(OF<uint32_t>(m, 0x28)) + "," + N(OF<uint8_t>(m, 0x2C));
}

// A property: its bytes from +0x1C in the original's layout (the tracks' vtables skipped, their quantisations' by
// the original's address).
std::string CompareProperty(C_NetProperty *pp, void *op)
{
    const uint8_t *a = reinterpret_cast<const uint8_t *>(pp);
    const uint8_t *b = static_cast<const uint8_t *>(op);
    for (int i = 0; i < 3; ++i)
        if (OrigQ(pp->m_read[i]) != OF<uintptr_t>(op, 4 + 4 * i) || OrigQ(pp->m_write[i]) != OF<uintptr_t>(op, 0x10 + 4 * i))
            return "the property's quantisations; ";
    for (int at = 0x1C; at < 0x580; at += 4) {
        if (at == 0x30 || at == 0xC0)
            continue;   // the tracks' vtables
        // (What the original's constructors leave as the memory was: the padding before the position track, every
        // vector's w, the tracks' +0x1C, the direction track's turn sign and its padding.)
        if ((at >= 0x24 && at < 0x30) || (at & 0xF) == 0xC || at == 0x4C || at == 0xDC || (at >= 0x170 && at < 0x180))
            continue;
        uint32_t x, y;
        std::memcpy(&x, a + at, 4);
        std::memcpy(&y, b + at, 4);
        if ((at > 0x30 && at < 0x4C) || (at > 0xC0 && at < 0xDC))
            x = static_cast<uint32_t>(OrigQ(reinterpret_cast<const void *>(static_cast<uintptr_t>(x))));
        if (x != y)
            return "the property's +" + Hex(static_cast<uint32_t>(at)).substr(5) + " " + Hex(y) + " / " + Hex(x) + "; ";
    }
    return "";
}

std::string CompareObject(C_NetObject *p, void *o, const std::string &name)
{
    std::string s;
    for (int i = 0; i < 3; ++i)
        if (OrigQ(p->m_read[i]) != OF<uintptr_t>(o, 4 + 4 * i) || OrigQ(p->m_write[i]) != OF<uintptr_t>(o, 0x10 + 4 * i))
            s += "quantisations; ";
    uint32_t second;
    std::memcpy(&second, &p->m_second, 4);
    uint32_t u34;
    std::memcpy(&u34, &p->m_unknown34, 4);
    if (p->m_id != OF<int>(o, 0x1C))
        s += "id " + N(OF<int>(o, 0x1C)) + " / " + N(p->m_id) + "; ";
    if (p->m_propertyCount != OF<int>(o, 0x30))
        s += "property count; ";
    if (u34 != OF<uint32_t>(o, 0x34))
        s += "+34; ";
    if (p->m_sendMask != OF<int>(o, 0x38) || p->GetLive() != OF<int>(o, 0x3C) || p->m_forward != (OF<uint8_t>(o, 0x40) != 0))
        s += "mask / live / forward; ";
    if (second != OF<uint32_t>(o, 0x44))
        s += "second " + Hex(OF<uint32_t>(o, 0x44)) + " / " + Hex(second) + "; ";
    if (p->m_lastRead != OF<int64_t>(o, 0x48))
        s += "last read; ";
    if (p->m_writtenTo != OF<uint32_t>(o, 0x50))
        s += "written to " + Hex(OF<uint32_t>(o, 0x50)) + " / " + Hex(p->m_writtenTo) + "; ";
    const bool rs = p->m_readState != nullptr, ws = p->m_writeState != nullptr, f = p->m_factory != nullptr;
    if (rs != (OF<void *>(o, 0x54) != nullptr) || ws != (OF<void *>(o, 0x58) != nullptr) || f != (OF<void *>(o, 0x64) != nullptr))
        s += "handlers; ";
    if (p->m_stateContext != OF<void *>(o, 0x5C) || p->m_factoryContext != OF<void *>(o, 0x60))
        s += "contexts; ";
    if (PropertyCountP(p) != PropertyCountO(o)) {
        s += "properties " + N(PropertyCountO(o)) + " / " + N(PropertyCountP(p)) + "; ";
    } else {
        for (int i = 0; i < PropertyCountP(p); ++i) {
            const std::string d = CompareProperty(p->m_properties[static_cast<size_t>(i)], OProperty(o, i));
            if (!d.empty()) {
                s += "property " + N(i) + ": " + d;
                break;
            }
        }
    }
    for (int q = 0; q < 2; ++q) {
        const std::list<C_NetObjectMessage *> &pl = q ? p->m_outgoing : p->m_received;
        const std::vector<void *> ol = OList(o, q ? 0x74 : 0x68);
        std::string x, y;
        for (C_NetObjectMessage *m : pl)
            x += MsgFields(m) + " ";
        for (void *m : ol)
            y += MsgFields(m) + " ";
        if (x != y || static_cast<int>(pl.size()) != OF<int>(o, q ? 0x7C : 0x70))
            s += std::string(q ? "outgoing" : "received") + " [" + y + "] / [" + x + "]; ";
    }
    return s.empty() ? s : name + ": " + s;
}

void CompareState()
{
    std::string s;
    for (int i = 0; i < 3; ++i)
        if (OrigQ(g_p->m_read[i]) != OF<uintptr_t>(g_o, 4 + 4 * i) || OrigQ(g_p->m_write[i]) != OF<uintptr_t>(g_o, 0x10 + 4 * i))
            s += "the replication's quantisations; ";
    uint32_t lag;
    std::memcpy(&lag, &g_p->m_lag, 4);
    if (g_p->m_useViewCone != (OF<uint8_t>(g_o, 0xBC) != 0) || g_p->m_field2 != OF<int>(g_o, 0xC0) ||
        g_p->m_writeEpoch != OF<int>(g_o, 0xC4) || g_p->m_readEpoch != OF<int>(g_o, 0xC8) || g_p->m_bitsRead != OF<int>(g_o, 0xCC) ||
        g_p->m_bitsWritten != OF<int>(g_o, 0xD0) || g_p->m_enabled != (OF<uint8_t>(g_o, 0xD4) != 0) || lag != OF<uint32_t>(g_o, 0xD8) ||
        g_p->GetLocalId() != OF<int>(g_o, 0xDC))
        s += "the replication's fields (bits read " + N(OF<int>(g_o, 0xCC)) + " / " + N(g_p->m_bitsRead) + ", written " +
             N(OF<int>(g_o, 0xD0)) + " / " + N(g_p->m_bitsWritten) + "); ";
    if ((g_p->m_makeStateReader != nullptr) != (OF<void *>(g_o, 0xE0) != nullptr) ||
        (g_p->m_makeFactory != nullptr) != (OF<void *>(g_o, 0xE4) != nullptr))
        s += "the makers; ";
    if (static_cast<int>(PObjects().size()) != OObjectCount()) {
        s += "the objects " + N(OObjectCount()) + " / " + N(static_cast<long long>(PObjects().size())) + "; ";
    } else {
        for (int i = 0; i < OObjectCount() && s.empty(); ++i)
            s += CompareObject(PObjects()[static_cast<size_t>(i)], OObject(i), "object " + N(i));
    }
    if (s.empty())
        s += CompareObject(&g_p->m_local, static_cast<uint8_t *>(g_o) + 0x30, "the local object");
    Check(s.empty(), s);
}

// ---- the messages' streams ----
constexpr int WORDS = 8192;
uint32_t g_pBuf[WORDS], g_oBuf[WORDS];
alignas(16) uint8_t g_pmStore[sizeof(C_NetMessage)];
C_NetMessage *g_pm;
alignas(16) uint8_t g_om[0x40];
// The last writes (their bits, equal on both sides when compared): what Read is given.
std::vector<std::vector<uint32_t>> g_written;

void ResetStreams()
{
    std::memset(g_pBuf, 0, sizeof g_pBuf);
    std::memset(g_oBuf, 0, sizeof g_oBuf);
    g_pm->Attach(g_pBuf, WORDS * 32);
    g_pm->ResetPosition();
    OF<uint32_t *>(g_om, 4) = g_oBuf;
    OF<uint32_t>(g_om, 8) = 0;
    OF<uint32_t>(g_om, 0xC) = 0;
    OF<uint32_t>(g_om, 0x10) = WORDS * 32;
}
uint32_t OPosition() { return OF<uint32_t>(g_om, 8) * 32 + OF<uint32_t>(g_om, 0xC); }

// ---- the calls ----
template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_trace += " " + what;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const long long x = port();
    g_side = 1;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
}

int ObjectIndex() { return PObjects().empty() ? -1 : I(0, static_cast<int>(PObjects().size()) - 1); }
// An object by index (-1: the local one).
C_NetObject *PObj(int i) { return i < 0 ? &g_p->m_local : PObjects()[static_cast<size_t>(i)]; }
void *OObj(int i) { return i < 0 ? static_cast<uint8_t *>(g_o) + 0x30 : OObject(i); }
int AnyObject() { return PObjects().empty() || P(0.15) ? -1 : ObjectIndex(); }

S_vector4 RandomVector(const S_NetQuantization *q)
{
    if (!q || P(0.2))
        return S_vector4{R(-100.0f, 100.0f), R(-100.0f, 100.0f), R(-100.0f, 100.0f), 0.0f};
    S_vector4 v;
    v.x = q->offset[0] + R(0.0f, q->range40[0]);
    v.y = q->offset[1] + R(0.0f, q->range40[1]);
    v.z = q->offset[2] + R(0.0f, q->range40[2]);
    v.w = 0.0f;
    return v;
}

// The ids the objects get (0..40; the local one's among them at times).
int RandomId() { return P(0.1) ? g_p->m_localId : I(0, 40); }

// Whether ClearMessages ends in the original: it does not move past a message it keeps (releasing, it releases it
// again until it goes; not releasing, it loops for ever: not given).
bool ClearsAll(C_NetObject *o, bool release, bool outgoing, bool force)
{
    if (force || release)
        return true;
    for (C_NetObjectMessage *m : outgoing ? o->m_outgoing : o->m_received)
        if (m->refCount >= 1)
            return false;
    return true;
}

// Whether a message is in a queue other than `queue` of object `i` (deleting it from one leaves the other's pointer
// dangling, in both programs: a server's forwarded messages are in both its object's queues).
bool HeldByAnotherQueue(const C_NetObjectMessage *m, int i, bool outgoing);
bool SharedMessages(int i, bool outgoing)
{
    for (const C_NetObjectMessage *m : outgoing ? PObj(i)->m_outgoing : PObj(i)->m_received)
        if (HeldByAnotherQueue(m, i, outgoing))
            return true;
    return false;
}

// Whether the view cone can be used: every object has a property 0 (both read it unchecked, before asking whether the
// object has data to send).
bool ConeUsable()
{
    for (C_NetObject *o : PObjects())
        if (!o->GetProperty(0))
            return false;
    return true;
}

// The layout of an object's data in a write, or the one a reader expects: its id, whether the properties go (each
// one's flags and quantisations: the writer's write ones, the reader's read ones), whether messages and the state
// may follow.
struct S_PropFormat {
    uint32_t flags;      // the position track's (a new property's tracks keep theirs until a field is set)
    uint32_t dirFlags;   // the direction track's
    const S_NetQuantization *q[3];
};
struct S_Format {
    int id;
    bool properties;
    std::vector<S_PropFormat> props;
    bool factory;
    bool state;
};
S_Format WriterFormat(C_NetObject *o, bool fromClient)
{
    S_Format f;
    f.id = o->m_id;
    f.properties = (o->m_sendMask & NET_SEND_PROPERTIES) && !fromClient;
    for (C_NetProperty *p : o->m_properties)
        f.props.push_back({p->m_position.m_flags, p->m_direction.m_flags, {p->m_write[0], p->m_write[1], p->m_write[2]}});
    f.factory = o->m_factory != nullptr;
    f.state = o->m_writeState != nullptr;
    return f;
}
uint32_t WithField2(uint32_t flags, int field2) { return (flags & ~(0x3Fu << 12)) | static_cast<uint32_t>(field2) << 12; }
// A property's format after InitObject (its field 2 set: both tracks take the property's flags; the direction track
// drops the rate bits).
S_PropFormat Initialised(uint32_t flags, int field2, S_NetQuantization *const *q)
{
    const uint32_t f = WithField2(flags, field2);
    return {f, f & 0xFFFC3FFFu, {q[0], q[1], q[2]}};
}
// What Read reads object `id`'s data into (it exists: it was written): the object of that id when live and of the
// epoch read; else the local object (made as InitObject makes it) when the id is its own; else a scratch object (one
// default property, the object's quantisations).
S_Format ReaderFormat(int writtenId)
{
    // (The id goes in 9 bits: -2 reads as 510; 0x1FF ends the objects.)
    const int id = writtenId & 0x1FF;
    S_Format f;
    f.id = id;
    f.properties = true;
    if (id == 0x1FF) {
        f.properties = false;
        f.factory = f.state = true;
        f.props.resize(99);
        return f;   // (agrees with nothing)
    }
    C_NetObject *found = nullptr;
    for (C_NetObject *o : PObjects())
        if (o->m_id == id) {
            found = o;
            break;
        }
    const bool otherEpoch = (g_p->m_writeEpoch & 1) != g_p->m_readEpoch;
    if (!found) {
        // Made by the read: one default property, the replication's quantisations, the makers' handlers.
        f.factory = id % 2 == 0;
        f.state = id % 3 != 0;
        if (otherEpoch && id == g_p->m_local.m_id) {
            for (C_NetProperty *p : g_p->m_local.m_properties)
                f.props.push_back(Initialised(p->m_flags, g_p->m_field2, g_p->m_read));
            return f;
        }
        f.props.push_back(Initialised(7u | 3u << 6, g_p->m_field2, g_p->m_read));
        return f;
    }
    if (found->m_live != 0 && !otherEpoch) {
        for (C_NetProperty *p : found->m_properties)
            f.props.push_back({p->m_position.m_flags, p->m_direction.m_flags, {p->m_read[0], p->m_read[1], p->m_read[2]}});
        f.factory = found->m_factory != nullptr;
        f.state = found->m_readState != nullptr;
        return f;
    }
    f.factory = id % 2 == 0;
    f.state = id % 3 != 0;
    if (id == g_p->m_local.m_id) {
        for (C_NetProperty *p : g_p->m_local.m_properties)
            f.props.push_back(Initialised(p->m_flags, g_p->m_field2, g_p->m_read));
        return f;
    }
    f.props.push_back(Initialised(7u | 3u << 6, g_p->m_field2, found->m_read));
    return f;
}
bool Agrees(const S_Format &w, const S_Format &r)
{
    if (w.factory != r.factory || w.state != r.state)
        return false;
    if (!w.properties)
        return true;
    if (w.props.size() != r.props.size())
        return false;
    for (size_t i = 0; i < w.props.size(); ++i) {
        if (w.props[i].flags != r.props[i].flags || w.props[i].dirFlags != r.props[i].dirFlags)
            return false;
        for (int k = 0; k < 3; ++k)
            if (w.props[i].q[k] != r.props[i].q[k])
                return false;
    }
    return true;
}

void Call()
{
    switch (I(0, 28)) {
    case 0:
    case 1: {
        const int id = RandomId();
        Run("CreateObject " + N(id), [&] { return reinterpret_cast<intptr_t>(g_p->CreateObject(id)) != 0; },
            [&] { return reinterpret_cast<intptr_t>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_R_CREATE)(g_o, id)) != 0; });
        break;
    }
    case 2: {
        const int id = RandomId();
        const bool create = P(0.5);
        Run("FindObject " + N(id) + " " + N(create),
            [&] {
                C_NetObject *o = g_p->FindObject(id, create);
                return o ? static_cast<long long>(std::find(PObjects().begin(), PObjects().end(), o) - PObjects().begin()) : -1LL;
            },
            [&] {
                void *o = reinterpret_cast<void *(THISCALL *)(void *, int, bool)>(O_R_FIND)(g_o, id, create);
                if (!o)
                    return -1LL;
                for (int i = 0; i < OObjectCount(); ++i)
                    if (OObject(i) == o)
                        return static_cast<long long>(i);
                return -2LL;
            });
        break;
    }
    case 3: {
        if (PObjects().empty() || P(0.5))
            return;
        const int i = ObjectIndex();
        C_NetObject *po = PObj(i);
        void *oo = OObj(i);
        Run("RemoveObject " + N(i), [&] { g_p->RemoveObject(po); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, void *)>(O_R_REMOVE)(g_o, oo); return 0; });
        break;
    }
    case 4:
        if (P(0.8))
            return;
        Run("DeleteObjects", [&] { g_p->DeleteObjects(); return 0; },
            [&] { reinterpret_cast<void(FAST *)(void *)>(O_R_DELETE_ALL)(g_o); return 0; });
        break;
    case 5:
    case 6: {
        const int i = AnyObject(), index = I(0, 3);
        Run("AddProperty " + N(i) + " " + N(index),
            [&] { return static_cast<long long>(PObj(i)->AddProperty(index)->GetIndex()); },
            [&] { return static_cast<long long>(OF<int>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_ADD)(OObj(i), index), 0x1C)); });
        break;
    }
    case 7: {
        const int i = AnyObject(), index = I(0, 3);
        Run("GetProperty " + N(i) + " " + N(index),
            [&] {
                C_NetProperty *p = PObj(i)->GetProperty(index);
                return p ? static_cast<long long>(std::find(PObj(i)->m_properties.begin(), PObj(i)->m_properties.end(), p) -
                                                  PObj(i)->m_properties.begin())
                         : -1LL;
            },
            [&] {
                void *p = reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_GET)(OObj(i), index);
                if (!p)
                    return -1LL;
                for (int k = 0; k < PropertyCountO(OObj(i)); ++k)
                    if (OProperty(OObj(i), k) == p)
                        return static_cast<long long>(k);
                return -2LL;
            });
        break;
    }
    case 8:
    case 9: {
        // A property's flags, a field, its lag; its tracks' values.
        const int i = AnyObject();
        if (PObj(i)->m_properties.empty())
            return;
        const int k = I(0, PropertyCountP(PObj(i)) - 1);
        C_NetProperty *pp = PObj(i)->m_properties[static_cast<size_t>(k)];
        void *op = OProperty(OObj(i), k);
        switch (I(0, 4)) {
        case 0: {
            uint32_t flags = static_cast<uint32_t>(I(0, 7)) | static_cast<uint32_t>(I(0, 0x3F)) << 12;
            if (P(0.3))
                flags = static_cast<uint32_t>(rng());
            Run("SetFlags " + Hex(flags), [&] { pp->SetFlags(flags); return 0; },
                [&] { reinterpret_cast<void(THISCALL *)(void *, uint32_t)>(O_P_SET_FLAGS)(op, flags); return 0; });
            break;
        }
        case 1: {
            const int field = I(0, 3), value = I(0, 0x3F);
            Run("SetField " + N(field) + " " + N(value), [&] { pp->SetField(field, value); return 0; },
                [&] { reinterpret_cast<void(THISCALL *)(void *, int, int)>(O_P_SET_FIELD)(op, field, value); return 0; });
            break;
        }
        case 2: {
            const float lag = R(0.0f, 0.5f);
            Run("SetLag", [&] { pp->SetLag(lag); return 0; },
                [&] { reinterpret_cast<void(THISCALL *)(void *, float)>(O_P_SET_LAG)(op, lag); return 0; });
            break;
        }
        default: {
            // The position's value, rate or net value; or the direction (its slot 20: a direction and an angle).
            void *track = static_cast<uint8_t *>(op) + (P(0.5) ? 0x30 : 0xC0);
            const bool dir = track == static_cast<uint8_t *>(op) + 0xC0;
            S_vector4 v = RandomVector(pp->m_write[0]);
            const int what = I(0, 3);
            if (!dir && what == 1) {
                const S_vector4 r = RandomVector(pp->m_write[2]);
                Run("SetRate", [&] { pp->GetTrack().SetRate(r); return 0; },
                    [&] { reinterpret_cast<void(THISCALL *)(void *, const void *)>(OF<void **>(track, 0)[9])(track, &r); return 0; });
            } else if (!dir && what == 2) {
                Run("SetNetValue", [&] { pp->GetTrack().SetNetValue(v); return 0; },
                    [&] { reinterpret_cast<void(THISCALL *)(void *, const void *)>(OF<void **>(track, 0)[8])(track, &v); return 0; });
            } else if (dir) {
                const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                if (len > 0.001f) {
                    v.x /= len;
                    v.y /= len;
                    v.z /= len;
                }
                const float angle = R(-3.0f, 3.0f);
                Run("SetDirection", [&] { pp->GetDirTrack().SetDirection(v, angle); return 0; },
                    [&] {
                        reinterpret_cast<void(THISCALL *)(void *, const void *, float)>(OF<void **>(track, 0)[20])(track, &v, angle);
                        return 0;
                    });
            } else {
                Run("SetValue", [&] { pp->GetTrack().SetValue(v); return 0; },
                    [&] { reinterpret_cast<void(THISCALL *)(void *, const void *)>(OF<void **>(track, 0)[7])(track, &v); return 0; });
            }
            break;
        }
        }
        break;
    }
    case 10:
    case 11:
    case 12: {
        // A message queued (the test's: sent to none, some, all), to send or as received.
        const int i = AnyObject();
        const int type = I(1, 15), sentTo = P(0.2) ? -1 : (P(0.5) ? 0 : I(0, 0xFFFF));
        const bool outgoing = P(0.7), fromClient = P(0.5);
        const int serial = g_serial[0]++;
        g_serial[1]++;
        PMsg *pm = PMakeMsg(type, serial);
        void *om = OMakeMsg(type, serial);
        pm->fromClient = fromClient;
        OF<uint8_t>(om, 0x2C) = fromClient;
        if (P(0.2)) {
            // Held elsewhere too.
            pm->refCount = 1;
            OF<int>(om, 0x24) = 1;
        }
        Run("QueueMessage " + N(i) + " " + N(serial) + " " + N(sentTo) + " " + N(outgoing),
            [&] { PObj(i)->QueueMessage(pm, sentTo, outgoing); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, void *, int, bool)>(O_O_QUEUE)(OObj(i), om, sentTo, outgoing); return 0; });
        break;
    }
    case 13: {
        const int i = AnyObject();
        const bool outgoing = P(0.5);
        Run("PopMessage " + N(i) + " " + N(outgoing),
            [&] {
                C_NetObjectMessage *m = PObj(i)->PopMessage(outgoing);
                return m ? static_cast<long long>(static_cast<PMsg *>(m)->serial) * 1000 + m->refCount : -1LL;
            },
            [&] {
                void *m = reinterpret_cast<void *(THISCALL *)(void *, bool)>(O_O_POP)(OObj(i), outgoing);
                return m ? static_cast<long long>(OF<int>(m, 0x30)) * 1000 + OF<int>(m, 0x24) : -1LL;
            });
        break;
    }
    case 14: {
        const int i = AnyObject();
        const bool release = P(0.5), outgoing = P(0.5), force = P(0.3);
        if (!ClearsAll(PObj(i), release, outgoing, force) || SharedMessages(i, outgoing))
            return;
        Run("ClearMessages " + N(i) + " " + N(release) + N(outgoing) + N(force),
            [&] { PObj(i)->ClearMessages(release, outgoing, force); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, bool, bool, bool)>(O_O_CLEAR)(OObj(i), release, outgoing, force); return 0; });
        break;
    }
    case 15:
    case 16: {
        // An object's settings (the original's inline setters: its fields).
        const int i = AnyObject();
        C_NetObject *p = PObj(i);
        void *o = OObj(i);
        g_line = "settings";
        switch (I(0, 4)) {
        case 0: {
            const int mask = I(0, 7);
            p->SetSendMask(mask);
            OF<int>(o, 0x38) = mask;
            break;
        }
        case 1: {
            const int live = P(0.7) ? 7 : I(0, 1);
            p->SetLive(live);
            OF<int>(o, 0x3C) = live;
            break;
        }
        case 2: {
            const bool forward = P(0.5);
            p->SetForwarding(forward);
            OF<uint8_t>(o, 0x40) = forward;
            break;
        }
        case 3: {
            const intptr_t context = I(0, 50);
            const bool read = P(0.7), write = P(0.8) ? read : P(0.5);
            p->SetStateHandlers(read ? &PReadState : nullptr, write ? &PWriteState : nullptr, reinterpret_cast<void *>(context));
            OF<void *>(o, 0x54) = read ? reinterpret_cast<void *>(&OReadState) : nullptr;
            OF<void *>(o, 0x58) = write ? reinterpret_cast<void *>(&OWriteState) : nullptr;
            OF<void *>(o, 0x5C) = reinterpret_cast<void *>(context);
            break;
        }
        default: {
            const intptr_t context = I(0, 50);
            const bool on = P(0.7);
            p->SetMessageFactory(on ? &PFactory : nullptr, reinterpret_cast<void *>(context));
            OF<void *>(o, 0x64) = on ? reinterpret_cast<void *>(&OFactory) : nullptr;
            OF<void *>(o, 0x60) = reinterpret_cast<void *>(context);
            break;
        }
        }
        CompareState();
        break;
    }
    case 17: {
        const int i = AnyObject();
        Run("HasDataToSend " + N(i), [&] { return PObj(i)->HasDataToSend(); },
            [&] { return reinterpret_cast<bool(FAST *)(void *)>(O_O_HAS_DATA)(OObj(i)); });
        Run("SecondsSinceRead " + N(i),
            [&] {
                const float f = PObj(i)->SecondsSinceRead();
                uint32_t u;
                std::memcpy(&u, &f, 4);
                return static_cast<long long>(u);
            },
            [&] {
                const float f = reinterpret_cast<float(FAST *)(void *)>(O_O_SINCE)(OObj(i));
                uint32_t u;
                std::memcpy(&u, &f, 4);
                return static_cast<long long>(u);
            });
        S_NetQuantization *a[6] = {}, *b[6] = {};
        PObj(i)->GetReadQuantizations(&a[0], &a[1], &a[2]);
        PObj(i)->GetWriteQuantizations(&a[3], &a[4], &a[5]);
        reinterpret_cast<void(THISCALL *)(void *, void *, void *, void *)>(O_O_GET_READ)(OObj(i), &b[0], &b[1], &b[2]);
        reinterpret_cast<void(THISCALL *)(void *, void *, void *, void *)>(O_O_GET_WRITE)(OObj(i), &b[3], &b[4], &b[5]);
        bool same = true;
        for (int k = 0; k < 6; ++k)
            same &= OrigQ(a[k]) == reinterpret_cast<uintptr_t>(b[k]);
        Check(same, "the quantisations got");
        break;
    }
    case 18: {
        const int i = AnyObject(), value = I(0, 0x3F);
        Run("SetPropertyField2 " + N(i) + " " + N(value), [&] { PObj(i)->SetPropertyField2(value); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int)>(O_O_FIELD2)(OObj(i), value); return 0; });
        break;
    }
    case 19:
    case 20:
    case 21: {
        static const float exact[] = {0.125f, 0.25f, 0.5f, 1.0f};
        const float seconds = P(0.3) ? exact[I(0, 3)] : P(0.2) ? R(0.5f, 1.5f) : R(0.0f, 0.2f);
        g_clock += P(0.4) ? 100 * I(0, 6) : P(0.3) ? I(300, 700) : I(0, 120);
        if (P(0.2) && !PObjects().empty())
            g_clock = PObj(ObjectIndex())->m_lastRead + 400;   // (an object silent for 0.4 s exactly)
        Run("Update", [&] { g_p->Update(seconds); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, float)>(O_R_UPDATE)(g_o, seconds); return 0; });
        break;
    }
    case 22:
        Run("Refresh", [&] { g_p->Refresh(); return 0; },
            [&] { reinterpret_cast<void(FAST *)(void *)>(O_R_REFRESH)(g_o); return 0; });
        break;
    case 23: {
        const float lag = R(0.0f, 0.3f);
        Run("SetLag", [&] { g_p->SetLag(lag); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, float)>(O_R_SET_LAG)(g_o, lag); return 0; });
        break;
    }
    case 24: {
        S_NetQuantization *v = RandomQ(0), *d = RandomQ(1), *r = RandomQ(2);
        const bool read = P(0.5);
        Run(std::string(read ? "SetReadQuantizations" : "SetWriteQuantizations"),
            [&] {
                if (read)
                    g_p->SetReadQuantizations(v, d, r);
                else
                    g_p->SetWriteQuantizations(v, d, r);
                return 0;
            },
            [&] {
                reinterpret_cast<void(THISCALL *)(void *, void *, void *, void *)>(read ? O_R_SET_READ : O_R_SET_WRITE)(g_o, v, d, r);
                return 0;
            });
        break;
    }
    case 25: {
        // The replication's settings (its fields on both: the epochs, the field 2, the view cone, the enabled flag).
        g_line = "replication settings";
        switch (I(0, 4)) {
        case 0: {
            const int e = I(0, 1);
            g_p->SetWriteEpoch(e);
            OF<int>(g_o, 0xC4) = e;
            break;
        }
        case 1: {
            const int e = I(0, 1);
            g_p->SetReadEpoch(e);
            OF<int>(g_o, 0xC8) = e;
            break;
        }
        case 2: {
            const int f = I(0, 0x3F);
            g_p->SetField2(f);
            OF<int>(g_o, 0xC0) = f;
            break;
        }
        case 3: {
            const bool on = P(0.5);
            g_p->m_enabled = on;
            OF<uint8_t>(g_o, 0xD4) = on;
            break;
        }
        default: {
            // The view cone: the local object's property 0 (made when missing), a cosine; every object given a property
            // 0 (the write reads it unchecked).
            const bool on = P(0.6);
            if (on)
                for (int i = 0; i < static_cast<int>(PObjects().size()); ++i)
                    if (!PObj(i)->GetProperty(0))
                        Run("AddProperty " + N(i) + " 0", [&] { return static_cast<long long>(PObj(i)->AddProperty(0)->GetIndex()); },
                            [&] {
                                return static_cast<long long>(
                                    OF<int>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_ADD)(OObj(i), 0), 0x1C));
                            });
            if (on && !g_p->m_local.GetProperty(0)) {
                Run("AddProperty local 0", [&] { return static_cast<long long>(g_p->m_local.AddProperty(0)->GetIndex()); },
                    [&] {
                        return static_cast<long long>(
                            OF<int>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_ADD)(OObj(-1), 0), 0x1C));
                    });
            }
            g_p->m_useViewCone = on;
            OF<uint8_t>(g_o, 0xBC) = on;
            if (on) {
                g_p->m_viewCone.m_viewer = g_p->m_local.GetProperty(0);
                OF<void *>(g_o, 0xB4) = reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_GET)(OObj(-1), 0);
                const float c = P(0.3) ? 0.737f : R(-1.0f, 1.0f);
                g_p->m_viewCone.m_cosine = c;
                OF<float>(g_o, 0xB8) = c;
                if (P(0.7)) {
                    // (The viewer away from the origin: its place is subtracted.)
                    C_NetProperty *vp = g_p->m_local.GetProperty(0);
                    void *posTrack = static_cast<uint8_t *>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_GET)(OObj(-1), 0)) + 0x30;
                    const S_vector4 at = {R(-200.0f, 200.0f), R(-50.0f, 50.0f), R(-200.0f, 200.0f), 0.0f};
                    Run("SetValue of the viewer", [&] { vp->GetTrack().SetValue(at); return 0; },
                        [&] { reinterpret_cast<void(THISCALL *)(void *, const void *)>(OF<void **>(posTrack, 0)[7])(posTrack, &at); return 0; });
                }
                if (P(0.7) && !PObjects().empty()) {
                    // (An object at the viewer's place, or a hair off it on x or z: below the square's 1e-8.)
                    const int i = ObjectIndex();
                    S_vector4 v = g_p->m_viewCone.m_viewer->GetTrack().GetCurrent();
                    const int axis = I(0, 3);
                    const float off = P(0.5) ? 5e-5f : -5e-5f;
                    if (axis == 0)
                        v.x += off;
                    else if (axis == 1)
                        v.z += off;
                    else if (axis == 2)
                        v.y += off;
                    C_NetProperty *pp = PObj(i)->GetProperty(0);
                    void *track = static_cast<uint8_t *>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_GET)(OObj(i), 0)) + 0x30;
                    Run("SetValue near the viewer", [&] { pp->GetTrack().SetValue(v); return 0; },
                        [&] { reinterpret_cast<void(THISCALL *)(void *, const void *)>(OF<void **>(track, 0)[7])(track, &v); return 0; });
                }
                // The viewer's facing (its direction track: a unit direction) and the objects' places around it.
                if (P(0.8)) {
                    C_NetProperty *vp = g_p->m_local.GetProperty(0);
                    void *vo = reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_GET)(OObj(-1), 0);
                    void *dirTrack = static_cast<uint8_t *>(vo) + 0xC0;
                    S_vector4 d = {R(-1.0f, 1.0f), R(-1.0f, 1.0f), R(-1.0f, 1.0f), 0.0f};
                    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                    d.x /= len;
                    d.y /= len;
                    d.z /= len;
                    const float angle = R(-3.0f, 3.0f);
                    Run("SetDirection of the viewer", [&] { vp->GetDirTrack().SetDirection(d, angle); return 0; },
                        [&] {
                            reinterpret_cast<void(THISCALL *)(void *, const void *, float)>(OF<void **>(dirTrack, 0)[20])(dirTrack, &d, angle);
                            return 0;
                        });
                    for (int i = 0; i < static_cast<int>(PObjects().size()); ++i) {
                        C_NetProperty *pp = PObj(i)->GetProperty(0);
                        if (!pp || P(0.3))
                            continue;
                        const S_vector4 &from = vp->GetTrack().GetCurrent();
                        const S_vector4 v = {from.x + R(-50.0f, 50.0f), from.y + R(-50.0f, 50.0f), from.z + R(-50.0f, 50.0f), 0.0f};
                        void *track =
                            static_cast<uint8_t *>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_GET)(OObj(i), 0)) + 0x30;
                        Run("SetValue around the viewer", [&] { pp->GetTrack().SetValue(v); return 0; },
                            [&] { reinterpret_cast<void(THISCALL *)(void *, const void *)>(OF<void **>(track, 0)[7])(track, &v); return 0; });
                    }
                }
            }
            break;
        }
        }
        CompareState();
        break;
    }
    case 26:
    case 27:
    case 28: {
        // Write for a player.
        if (g_p->m_useViewCone && !ConeUsable()) {
            // (Every object given a property 0 first: the cone reads it unchecked.)
            for (int i = 0; i < static_cast<int>(PObjects().size()); ++i)
                if (!PObj(i)->GetProperty(0))
                    Run("AddProperty " + N(i) + " 0", [&] { return static_cast<long long>(PObj(i)->AddProperty(0)->GetIndex()); },
                        [&] {
                            return static_cast<long long>(OF<int>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_O_ADD)(OObj(i), 0), 0x1C));
                        });
        }
        const int player = P(0.8) ? I(0, 7) : I(0, 31);
        const bool fromClient = P(0.4);
        if (g_pm->BitPosition() > (WORDS - 2048) * 32)
            ResetStreams();
        const bool readBack = P(0.7);
        if (readBack) {
            g_line = "the write handlers matched";
            for (int i = -1; i < static_cast<int>(PObjects().size()); ++i) {
                C_NetObject *p = PObj(i);
                if ((p->m_readState != nullptr) == (p->m_writeState != nullptr))
                    continue;
                const bool on = p->m_readState != nullptr;
                p->m_writeState = on ? &PWriteState : nullptr;
                OF<void *>(OObj(i), 0x58) = on ? reinterpret_cast<void *>(&OWriteState) : nullptr;
            }
            CompareState();
            if (P(0.5) && !PObjects().empty()) {
                const int i = ObjectIndex();
                if (PObj(i)->m_factory && (PObj(i)->m_sendMask & NET_SEND_MESSAGES)) {
                    const int serial = g_serial[0]++;
                    g_serial[1]++;
                    PMsg *pm = PMakeMsg(I(1, 15), serial);
                    void *om = OMakeMsg(pm->type, serial);
                    pm->fromClient = fromClient;
                    OF<uint8_t>(om, 0x2C) = fromClient;
                    Run("QueueMessage " + N(i) + " " + N(serial) + " to send",
                        [&] { PObj(i)->QueueMessage(pm, 0, true); return 0; },
                        [&] { reinterpret_cast<void(THISCALL *)(void *, void *, int, bool)>(O_O_QUEUE)(OObj(i), om, 0, true); return 0; });
                }
            }
        }
        const uint32_t start = g_pm->BitPosition();
        // (The objects it writes, their formats taken before: the write changes their queues.)
        std::vector<S_Format> written;
        for (C_NetObject *o : PObjects()) {
            if (g_p->m_useViewCone && !g_p->m_viewCone.Contains(o->GetProperty(0)->GetTrack().GetCurrent()))
                continue;
            if (o->HasDataToSend())
                written.push_back(WriterFormat(o, fromClient));
        }
        // (A property written needs its quantisations: both read them unchecked; the game sets them first.)
        for (const S_Format &w : written)
            if (w.properties)
                for (const S_PropFormat &p : w.props)
                    if (!p.q[0] || !p.q[1] || !p.q[2])
                        return;
        Run("Write " + N(player) + " " + N(fromClient), [&] { return g_p->Write(g_pm, player, fromClient); },
            [&] { return reinterpret_cast<bool(THISCALL *)(void *, void *, int, bool)>(O_R_WRITE)(g_o, g_om, player, fromClient); });
        Check(g_pm->BitPosition() == OPosition() && std::memcmp(g_pBuf, g_oBuf, sizeof g_pBuf) == 0,
              "the bits written (" + N(OPosition()) + " / " + N(g_pm->BitPosition()) + ")");
        // Read back by both when each object written finds a reader of its format (else the reads go astray).
        if (!readBack)
            return;
        // The reader's side changed at times (the same changes on both; what is read does not depend on them, or the
        // format check below sees it): the epoch read, an object not live, one removed (the read makes it anew), the
        // send masks (the server's forwarding).
        g_line = "the reader's changes";
        if (P(0.25)) {
            g_p->m_readEpoch ^= 1;
            OF<int>(g_o, 0xC8) = g_p->m_readEpoch;
        }
        if (P(0.2) && !PObjects().empty()) {
            const int i = ObjectIndex();
            const int live = I(0, 1);
            PObj(i)->SetLive(live);
            OF<int>(OObj(i), 0x3C) = live;
        }
        if (P(0.3) && !PObjects().empty()) {
            const int i = ObjectIndex();
            const int mask = I(0, 7);
            PObj(i)->SetSendMask(mask);
            OF<int>(OObj(i), 0x38) = mask;
        }
        CompareState();
        if (P(0.2) && !PObjects().empty()) {
            const int i = ObjectIndex();
            C_NetObject *po = PObj(i);
            void *oo = OObj(i);
            Run("RemoveObject " + N(i) + " before the read", [&] { g_p->RemoveObject(po); return 0; },
                [&] { reinterpret_cast<void(THISCALL *)(void *, void *)>(O_R_REMOVE)(g_o, oo); return 0; });
        }
        for (const S_Format &w : written)
            if (!Agrees(w, ReaderFormat(w.id)))
                return;
        if (std::getenv("HD2_REPL_DEBUG") && g_round == std::atoi(std::getenv("HD2_REPL_DEBUG"))) {
            for (const S_Format &w : written) {
                const S_Format r = ReaderFormat(w.id);
                std::printf("  id %d props %d (%d/%d) factory %d state %d\n", w.id, w.properties, static_cast<int>(w.props.size()),
                            static_cast<int>(r.props.size()), w.factory, w.state);
                for (size_t k = 0; k < w.props.size(); ++k)
                    std::printf("    flags %08x/%08x\n", w.props[k].flags, r.props[k].flags);
            }
            std::printf("  epochs %d %d local %d live", g_p->m_writeEpoch, g_p->m_readEpoch, g_p->m_local.m_id);
            for (C_NetObject *o : PObjects())
                std::printf(" %d:%d", o->m_id, o->m_live);
            std::printf("\n  bits %u:", g_pm->BitPosition() - start);
            for (uint32_t b = start; b < g_pm->BitPosition(); ++b)
                std::printf("%u", g_pBuf[b >> 5] >> (b & 31) & 1);
            std::printf("\n");
        }
        // (The bits copied after a few others: the read starts past the stream's start.)
        const uint32_t end = g_pm->BitPosition(), skip = static_cast<uint32_t>(P(0.5) ? 0 : I(1, 40));
        static uint32_t pb[WORDS], ob[WORDS];
        std::memset(pb, 0, sizeof pb);
        for (uint32_t b = start; b < end; ++b)
            if (g_pBuf[b >> 5] >> (b & 31) & 1)
                pb[(b - start + skip) >> 5] |= 1u << ((b - start + skip) & 31);
        std::memcpy(ob, pb, sizeof pb);
        alignas(16) uint8_t pms[sizeof(C_NetMessage)];
        C_NetMessage *pm = new (pms) C_NetMessage();
        pm->Attach(pb, WORDS * 32);
        pm->SetBitPosition(skip);
        alignas(16) uint8_t oms[0x40];
        reinterpret_cast<void *(THISCALL *)(void *)>(O_MSG_CTOR)(oms);
        OF<uint32_t *>(oms, 4) = ob;
        OF<uint32_t>(oms, 8) = skip >> 5;
        OF<uint32_t>(oms, 0xC) = skip & 31;
        OF<uint32_t>(oms, 0x10) = WORDS * 32;
        const uint32_t forwardTo = P(0.5) ? static_cast<uint32_t>(I(0, 0xFF)) : 0xFFFFFFFEu;
        const bool readFromClient = P(0.5);
        if (P(0.3)) {
            g_netServerSide = P(0.5);
            G<uint8_t>(O_SERVER) = g_netServerSide;
        }
        g_clock += I(0, 50);
        Run("Read " + Hex(forwardTo) + " " + N(readFromClient), [&] { g_p->Read(pm, forwardTo, readFromClient); return 0; },
            [&] {
                reinterpret_cast<void(THISCALL *)(void *, void *, uint32_t, bool)>(O_R_READ)(g_o, oms, forwardTo, readFromClient);
                return 0;
            });
        Check(pm->BitPosition() == OF<uint32_t>(oms, 8) * 32 + OF<uint32_t>(oms, 0xC), "the bits read");
        pm->Attach(nullptr, 0);
        pm->~C_NetMessage();
        OF<uint32_t *>(oms, 4) = nullptr;
        break;
    }
    }
}

void Round()
{
    g_trace.clear();
    MakeQuantizations();
    g_clock = I(0, 100000);
    g_netUpdatePeriod = P(0.5) ? 0.1f : R(0.03f, 0.3f);
    G<float>(O_PERIOD) = g_netUpdatePeriod;
    g_netServerSide = P(0.5);
    G<uint8_t>(O_SERVER) = g_netServerSide;
    g_serial[0] = g_serial[1] = 0;
    g_msgs[0].clear();
    g_msgs[1].clear();
    g_written.clear();
    ResetStreams();

    g_line = "construct";
    g_faking = true;
    g_side = 0;
    g_p = new C_NetReplication();
    g_side = 1;
    std::memset(g_oStore, 0, sizeof g_oStore);
    reinterpret_cast<void *(THISCALL *)(void *)>(O_R_CTOR)(g_o);
    g_side = 0;
    g_faking = false;
    CompareState();
    g_p->SetFactories(&PMakeStateReader, &PMakeFactory);
    OF<void *>(g_o, 0xE0) = reinterpret_cast<void *>(&OMakeStateReader);
    OF<void *>(g_o, 0xE4) = reinterpret_cast<void *>(&OMakeFactory);
    const int localId = P(0.2) ? -2 : I(0, 40);
    g_p->m_localId = localId;
    OF<int>(g_o, 0xDC) = localId;
    g_p->m_local.m_id = localId;
    OF<int>(g_o, 0x30 + 0x1C) = localId;
    CompareState();
    // The quantisations first (the game's MakeReplication / SetLevelBounds).
    if (P(0.9)) {
        S_NetQuantization *v = &g_q[0], *d = &g_q[2], *r = &g_q[4];
        Run("quantisations", [&] { g_p->SetReadQuantizations(v, d, r); g_p->SetWriteQuantizations(v, d, r); return 0; },
            [&] {
                reinterpret_cast<void(THISCALL *)(void *, void *, void *, void *)>(O_R_SET_READ)(g_o, v, d, r);
                reinterpret_cast<void(THISCALL *)(void *, void *, void *, void *)>(O_R_SET_WRITE)(g_o, v, d, r);
                return 0;
            });
    }
    g_p->m_enabled = P(0.85);
    OF<uint8_t>(g_o, 0xD4) = g_p->m_enabled;
    for (int k = I(0, 4); k > 0; --k) {
        const int id = RandomId();
        Run("CreateObject " + N(id), [&] { return reinterpret_cast<intptr_t>(g_p->CreateObject(id)) != 0; },
            [&] { return reinterpret_cast<intptr_t>(reinterpret_cast<void *(THISCALL *)(void *, int)>(O_R_CREATE)(g_o, id)) != 0; });
    }
    for (int k = I(4, 40); k > 0; --k)
        Call();

    // The end: the messages left in the queues freed by the test (the replication's destructor frees only the
    // nodes), the replications destroyed.
    // (Each message once: one in two queues would be deleted twice by clearing both.)
    g_line = "destroy";
    std::set<C_NetObjectMessage *> pm;
    std::set<void *> om;
    for (int i = -1; i < static_cast<int>(PObjects().size()); ++i) {
        for (int q = 0; q < 2; ++q) {
            std::list<C_NetObjectMessage *> &l = q ? PObj(i)->m_outgoing : PObj(i)->m_received;
            pm.insert(l.begin(), l.end());
            l.clear();
            for (void *m : OList(OObj(i), q ? 0x74 : 0x68))
                om.insert(m);
            OListFree(OObj(i), q ? 0x74 : 0x68);
        }
    }
    for (C_NetObjectMessage *m : pm)
        delete m;
    for (void *m : om)
        OMsgDelete(m, 1);
    delete g_p;
    reinterpret_cast<void(THISCALL *)(void *)>(O_R_DTOR)(g_o);
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p (the test's 0x20000000 base: %08x) in round %d, %s, %s's side\n   (ops:%s)\n",
                e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) -
                                      reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) + 0x20000000u),
                g_round, g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
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
    if (std::getenv("HD2_REPL_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("repl_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    G<void *>(O_QPC_SLOT) = reinterpret_cast<void *>(&FakeQueryPerformanceCounter);
    G<void *>(O_QPF_SLOT) = reinterpret_cast<void *>(&FakeQueryPerformanceFrequency);
    G<int64_t>(O_FREQUENCY) = 0;
    // The original's message vtable copied, its Write, Read and deleting destructor the test's.
    std::memcpy(g_oMsgVtable, reinterpret_cast<void *>(static_cast<uintptr_t>(O_SMSG_VTABLE)), sizeof g_oMsgVtable);
    g_oMsgVtable[4] = reinterpret_cast<void *>(&OMsgDelete);
    g_oMsgVtable[8] = reinterpret_cast<void *>(&OMsgWrite);
    g_oMsgVtable[9] = reinterpret_cast<void *>(&OMsgRead);
    g_pm = new (g_pmStore) C_NetMessage();
    reinterpret_cast<void *(THISCALL *)(void *)>(O_MSG_CTOR)(g_om);

    int rounds = std::getenv("HD2_REPL_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_REPL_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_REPL_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("repl_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
