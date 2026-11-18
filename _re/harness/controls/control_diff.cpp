// Human part 48: the controls' base (game/control.cpp: C_Control, the descriptions, the message queue) against the
// original's functions in the original executable mapped into this process (../collision/original_exe.cpp). MinGW;
// build_base.bat; runs control_diff.exe ("control_diff: N checks, 0 failures").
//
// The port's controls are real C_Control objects in a random tree; the original's are raw blocks (0x6C bytes, the
// original's own vtable 0x00813dd0, so its virtual calls stay in its code; MSVC6 vectors of 16 bytes: allocator, first,
// last, end) built from the port's before each call and compared field by field after it, with the message queue
// (the original's MSVC6 deque at 0x008ae590), the calls on the frames (one set of fakes: their slots are stdcall) and on
// the callbacks (a cdecl fake per callback on the port's side, a thiscall one on the original's: mapped to each other).
// The factory (CreateChild) is faked on both sides: the controls it makes are controls.cpp's (later parts).
// HD2_CONTROL_ROUNDS: rounds per call kind (default 20,000; HD2_CONTROL_QUICK: 4,000); HD2_CONTROL_CASE: one kind only.
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/control.h"
#include "ls3d/i3d.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

namespace {

int checks, failures;
const char *g_case = "";
int g_round;
void Fail(const char *format, ...)
{
    ++failures;
    if (failures > 40)
        return;
    std::printf("FAIL [%s round %d] ", g_case, g_round);
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}
#define CHECK(cond, ...)               \
    do {                               \
        ++checks;                      \
        if (!(cond))                   \
            Fail(__VA_ARGS__);         \
    } while (0)

// ---- random values ----
std::mt19937 g_rng(48);
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng) < p; }
uint32_t U32() { return static_cast<uint32_t>(g_rng()); }
template <class T, size_t N> T Pick(const T (&list)[N]) { return list[I(0, int(N) - 1)]; }

// ---- the calls on the fakes, per side ----
std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[300];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
}
std::string Joined(const std::vector<std::string> &log)
{
    std::string all;
    for (const std::string &line : log)
        all += line + "; ";
    return all;
}

// ---- the two trees ----
const uint32_t NOTIFICATION_BIT_FOR_TEST = 0x4000000;
const int MAX_NODES = 10;
// (Saved for the original's side: what the port's call was given.)
S_ControlMessage mp_before;
std::vector<S_ControlMessage> g_listForOrig, g_listAfter, g_postList;
const uint32_t O_SIZE = 0x6C, O_CHILDREN = 0x04, O_ROUTES = 0x18, O_CALLBACKS = 0x2C, O_ANY = 0x40, O_PARENT = 0x4C, O_FRAME = 0x50,
               O_TYPE = 0x54, O_VALUE = 0x58, O_INDEX = 0x5C, O_ID = 0x60, O_INACTIVE = 0x64, O_SHOWN = 0x65, O_TAKES = 0x66,
               O_SCENE = 0x68;
const uint32_t O_VTABLE = 0x00813dd0;
// The original's controls get a copy of its vtable whose Refresh (0x18) and HandleMessageCopy (0x74) are logged first
// (the port's are C_LoggedControl): the recursion into the children shows.
uint32_t g_origVt[0x88 / 4];
uint32_t LoggedVt() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_origVt)); }
std::vector<C_Control *> g_portNodes;
std::vector<uint8_t *> g_origNodes;
// (Blocks of the original's side made by the test, freed before the next call: its own allocations are left.)
std::vector<void *> g_origBlocks;
void *OrigAlloc(size_t bytes)
{
    void *p = std::calloc(1, bytes ? bytes : 1);
    g_origBlocks.push_back(p);
    return p;
}
template <class T> T &OF(uint8_t *block, uint32_t offset) { return *reinterpret_cast<T *>(block + offset); }
// A control's name in the logs (the same on both sides): its index, or the pointer.
std::string Name(const void *control)
{
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        if (control == g_portNodes[i] || control == g_origNodes[i])
            return "c" + std::to_string(i);
    }
    char text[24];
    std::snprintf(text, sizeof text, "%p", control);
    return text;
}

class C_LoggedControl : public C_Control {
public:
    using C_Control::C_Control;
    void Refresh() override
    {
        Log("refresh %s", Name(this).c_str());
        C_Control::Refresh();
    }
    bool HandleMessageCopy(const S_ControlMessage &message) override
    {
        Log("copy %s target %x id %x value %x", Name(this).c_str(), message.target, message.id, message.value);
        return C_Control::HandleMessageCopy(message);
    }
};
void __attribute__((thiscall)) O_LoggedRefresh(void *self)
{
    Log("refresh %s", Name(self).c_str());
    OriginalAt<void(__attribute__((thiscall)) *)(void *)>(0x00661c30)(self);
}
bool __attribute__((thiscall)) O_LoggedCopy(void *self, const uint32_t *message)
{
    // (Its value2 and value3 are stack words in the original's refresh: not logged.)
    Log("copy %s target %x id %x value %x", Name(self).c_str(), message[0], message[1], message[2]);
    return OriginalAt<bool(__attribute__((thiscall)) *)(void *, const uint32_t *)>(0x0064df50)(self, message);
}

// ---- the frames (one set for both sides: stdcall slots) ----
struct S_FakeFrame {
    void **vtable;
    uint8_t bytes[0x200];
};
S_FakeFrame g_frames[3];
void *g_frameVt[64];
int FrameIndex(const void *f)
{
    for (int i = 0; i < 3; ++i)
        if (f == &g_frames[i])
            return i;
    return -1;
}
void __stdcall F_Update(void *f) { Log("frame%d Update", FrameIndex(f)); }
void __stdcall F_SetOn(void *f, bool on) { Log("frame%d SetOn %d", FrameIndex(f), on); }
void *__stdcall F_FindChild(void *f, const char *name, uint32_t mask)
{
    Log("frame%d FindChildFrame %s %x", FrameIndex(f), name, mask);
    return nullptr;
}
void __stdcall F_Other(void *f) { Log("frame%d other slot", FrameIndex(f)); }
uint32_t FrameData(int i) { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(&g_frames[i]) + 0xEC); }

// ---- the callbacks: per side, mapped ----
std::vector<int> g_cbResults;
size_t g_cbNext;
bool CbResult() { return g_cbResults.empty() ? true : g_cbResults[g_cbNext++ % g_cbResults.size()]; }
#define CALLBACK_PAIR(K)                                                                                                     \
    bool PortCb##K(void *object, void *argument, C_Control *control, uint32_t notification, uint32_t value)                \
    {                                                                                                                        \
        Log("cb%d %p %p %s %x %x", K, object, argument, Name(control).c_str(), notification, value);                        \
        return CbResult();                                                                                                   \
    }                                                                                                                        \
    bool __attribute__((thiscall)) OrigCb##K(void *object, void *argument, void *control, uint32_t notification, uint32_t value) \
    {                                                                                                                        \
        Log("cb%d %p %p %s %x %x", K, object, argument, Name(control).c_str(), notification, value);                        \
        return CbResult();                                                                                                   \
    }
CALLBACK_PAIR(0)
CALLBACK_PAIR(1)
CALLBACK_PAIR(2)
const uintptr_t g_portCbs[3] = {reinterpret_cast<uintptr_t>(PortCb0), reinterpret_cast<uintptr_t>(PortCb1), reinterpret_cast<uintptr_t>(PortCb2)};
const uintptr_t g_origCbs[3] = {reinterpret_cast<uintptr_t>(OrigCb0), reinterpret_cast<uintptr_t>(OrigCb1), reinterpret_cast<uintptr_t>(OrigCb2)};
uintptr_t ToOrigFn(uintptr_t f)
{
    for (int i = 0; i < 3; ++i)
        if (f == g_portCbs[i])
            return g_origCbs[i];
    return f;
}
// The objects the callbacks are members of (opaque: the same pointers on both sides).
uint8_t g_objects[2][16];

// ---- the original's allocator ----
void *__cdecl O_New(size_t size) { return std::malloc(size ? size : 1); }
void __cdecl O_Delete(void *p) { std::free(p); }

// ---- texts: the port's C_str into the original's message (a rep of its own) ----
uint32_t OrigText(const C_str &text)
{
    if (text.Size() == 0 && !text.m_rep)
        return 0;
    C_str copy(text.c_str());
    void *rep = copy.m_rep;
    copy.m_rep = nullptr;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rep));
}
std::string TextOf(uint32_t rep)
{
    if (!rep)
        return "(null)";
    C_str s;
    s.m_rep = reinterpret_cast<C_str::Rep *>(static_cast<uintptr_t>(rep));
    std::string out = s.c_str();
    s.m_rep = nullptr;
    return out;
}
std::string TextOf(const C_str &s) { return s.m_rep ? std::string(s.c_str()) : "(null)"; }

// A message into the original's 0x18 bytes (its text a rep of its own; a port callback's function mapped).
void MessageToOrig(const S_ControlMessage &m, uint8_t *o)
{
    const uint32_t words[6] = {m.target, m.id, m.value, static_cast<uint32_t>(m.value2), static_cast<uint32_t>(ToOrigFn(m.value3)),
                               OrigText(m.text)};
    std::memcpy(o, words, 0x18);
}
std::string MessageDiffers(const S_ControlMessage &m, const uint8_t *o)
{
    uint32_t w[6];
    std::memcpy(w, o, 0x18);
    char text[200];
    if (w[0] != m.target || w[1] != m.id || w[2] != m.value || w[3] != m.value2 || w[4] != ToOrigFn(m.value3)) {
        std::snprintf(text, sizeof text, "%x %x %x %x %x / %x %x %x %x %x", w[0], w[1], w[2], w[3], w[4], m.target, m.id, m.value,
                      unsigned(m.value2), unsigned(m.value3));
        return text;
    }
    if (TextOf(w[5]) != TextOf(m.text))
        return "text " + TextOf(w[5]) + " / " + TextOf(m.text);
    return "";
}

// An MSVC6 vector at `at` (first, last, end at +4, +8, +0xC) filled with `bytes` (a block of the test's).
void OVecPut(uint8_t *block, uint32_t at, const void *data, size_t bytes)
{
    if (bytes == 0 && P(0.5)) {
        OF<void *>(block, at + 4) = OF<void *>(block, at + 8) = OF<void *>(block, at + 12) = nullptr;
        return;
    }
    const size_t spare = P(0.5) ? 0 : size_t(I(1, 3)) * 0x18;
    // (The original's growth frees the buffer: its own operator delete is free, the block is not the test's.)
    auto *buffer = static_cast<uint8_t *>(std::malloc(bytes + spare + 1));
    std::memcpy(buffer, data, bytes);
    OF<uint8_t *>(block, at + 4) = buffer;
    OF<uint8_t *>(block, at + 8) = buffer + bytes;
    OF<uint8_t *>(block, at + 12) = buffer + bytes + spare;
}
size_t OVecBytes(uint8_t *block, uint32_t at) { return size_t(OF<uint8_t *>(block, at + 8) - OF<uint8_t *>(block, at + 4)); }
uint8_t *OVecData(uint8_t *block, uint32_t at) { return OF<uint8_t *>(block, at + 4); }

uint8_t *ToOrigNode(const C_Control *c)
{
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (c == g_portNodes[i])
            return g_origNodes[i];
    return reinterpret_cast<uint8_t *>(const_cast<C_Control *>(c));
}

// The port's tree into fresh original blocks.
void ToOrig()
{
    for (void *p : g_origBlocks)
        std::free(p);
    g_origBlocks.clear();
    g_origNodes.clear();
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        g_origNodes.push_back(static_cast<uint8_t *>(OrigAlloc(O_SIZE + 0x10)));
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        const C_Control &c = *g_portNodes[i];
        uint8_t *o = g_origNodes[i];
        OF<uint32_t>(o, 0) = LoggedVt();
        std::vector<uint32_t> children;
        for (C_Control *child : c.m_children)
            children.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ToOrigNode(child))));
        OVecPut(o, O_CHILDREN, children.data(), children.size() * 4);
        std::vector<uint8_t> routes(c.m_routes.size() * 0x18);
        for (size_t k = 0; k < c.m_routes.size(); ++k)
            MessageToOrig(c.m_routes[k], routes.data() + k * 0x18);
        OVecPut(o, O_ROUTES, routes.data(), routes.size());
        std::vector<uint32_t> callbacks;
        for (const S_ControlCallback &cb : c.m_callbacks) {
            callbacks.push_back(cb.notification);
            callbacks.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(cb.object)));
            callbacks.push_back(static_cast<uint32_t>(ToOrigFn(reinterpret_cast<uintptr_t>(cb.function))));
        }
        OVecPut(o, O_CALLBACKS, callbacks.data(), callbacks.size() * 4);
        OF<uint32_t>(o, O_ANY) = c.m_anyCallback.notification;
        OF<void *>(o, O_ANY + 4) = c.m_anyCallback.object;
        OF<uint32_t>(o, O_ANY + 8) = static_cast<uint32_t>(ToOrigFn(reinterpret_cast<uintptr_t>(c.m_anyCallback.function)));
        OF<uint8_t *>(o, O_PARENT) = c.m_parent ? ToOrigNode(c.m_parent) : nullptr;
        OF<I3D_frame *>(o, O_FRAME) = c.m_frame;
        OF<int>(o, O_TYPE) = c.m_type;
        OF<uint32_t>(o, O_VALUE) = c.m_value;
        OF<int>(o, O_INDEX) = c.m_index;
        OF<uint32_t>(o, O_ID) = c.m_id;
        OF<bool>(o, O_INACTIVE) = c.m_inactive;
        OF<bool>(o, O_SHOWN) = c.m_shown;
        OF<bool>(o, O_TAKES) = c.m_takesInput;
        OF<I3D_scene *>(o, O_SCENE) = c.m_scene;
    }
}

void CompareTrees()
{
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        const C_Control &c = *g_portNodes[i];
        uint8_t *o = g_origNodes[i];
        const std::string n = "c" + std::to_string(i) + " ";
        CHECK(OF<uint32_t>(o, 0) == LoggedVt(), "%svtable", n.c_str());
        CHECK(OVecBytes(o, O_CHILDREN) == c.m_children.size() * 4, "%schildren %u / %u", n.c_str(), unsigned(OVecBytes(o, O_CHILDREN) / 4),
              unsigned(c.m_children.size()));
        if (OVecBytes(o, O_CHILDREN) == c.m_children.size() * 4) {
            for (size_t k = 0; k < c.m_children.size(); ++k)
                CHECK(reinterpret_cast<uint8_t **>(OVecData(o, O_CHILDREN))[k] == ToOrigNode(c.m_children[k]), "%schild %u", n.c_str(), unsigned(k));
        }
        CHECK(OVecBytes(o, O_ROUTES) == c.m_routes.size() * 0x18, "%sroutes %u / %u", n.c_str(), unsigned(OVecBytes(o, O_ROUTES) / 0x18),
              unsigned(c.m_routes.size()));
        if (OVecBytes(o, O_ROUTES) == c.m_routes.size() * 0x18) {
            for (size_t k = 0; k < c.m_routes.size(); ++k) {
                const std::string why = MessageDiffers(c.m_routes[k], OVecData(o, O_ROUTES) + k * 0x18);
                CHECK(why.empty(), "%sroute %u: %s", n.c_str(), unsigned(k), why.c_str());
            }
        }
        CHECK(OVecBytes(o, O_CALLBACKS) == c.m_callbacks.size() * 12, "%scallbacks %u / %u", n.c_str(), unsigned(OVecBytes(o, O_CALLBACKS) / 12),
              unsigned(c.m_callbacks.size()));
        if (OVecBytes(o, O_CALLBACKS) == c.m_callbacks.size() * 12) {
            for (size_t k = 0; k < c.m_callbacks.size(); ++k) {
                const uint32_t *w = reinterpret_cast<uint32_t *>(OVecData(o, O_CALLBACKS) + k * 12);
                const S_ControlCallback &cb = c.m_callbacks[k];
                CHECK(w[0] == cb.notification && w[1] == reinterpret_cast<uintptr_t>(cb.object) &&
                          w[2] == ToOrigFn(reinterpret_cast<uintptr_t>(cb.function)),
                      "%scallback %u: %x %x %x / %x %p %p", n.c_str(), unsigned(k), w[0], w[1], w[2], cb.notification, cb.object,
                      reinterpret_cast<void *>(cb.function));
            }
        }
        CHECK(OF<uint32_t>(o, O_ANY) == c.m_anyCallback.notification && OF<void *>(o, O_ANY + 4) == c.m_anyCallback.object &&
                  OF<uint32_t>(o, O_ANY + 8) == ToOrigFn(reinterpret_cast<uintptr_t>(c.m_anyCallback.function)),
              "%sany callback", n.c_str());
        CHECK(OF<uint8_t *>(o, O_PARENT) == (c.m_parent ? ToOrigNode(c.m_parent) : nullptr), "%sparent", n.c_str());
        CHECK(OF<I3D_frame *>(o, O_FRAME) == c.m_frame, "%sframe", n.c_str());
        CHECK(OF<int>(o, O_TYPE) == c.m_type, "%stype %d / %d", n.c_str(), OF<int>(o, O_TYPE), c.m_type);
        CHECK(OF<uint32_t>(o, O_VALUE) == c.m_value, "%svalue %x / %x", n.c_str(), OF<uint32_t>(o, O_VALUE), c.m_value);
        CHECK(OF<int>(o, O_INDEX) == c.m_index, "%sindex %d / %d", n.c_str(), OF<int>(o, O_INDEX), c.m_index);
        CHECK(OF<uint32_t>(o, O_ID) == c.m_id, "%sid", n.c_str());
        CHECK(OF<bool>(o, O_INACTIVE) == c.m_inactive && OF<bool>(o, O_SHOWN) == c.m_shown && OF<bool>(o, O_TAKES) == c.m_takesInput,
              "%sflags %d%d%d / %d%d%d", n.c_str(), OF<bool>(o, O_INACTIVE), OF<bool>(o, O_SHOWN), OF<bool>(o, O_TAKES), c.m_inactive,
              c.m_shown, c.m_takesInput);
        CHECK(OF<I3D_scene *>(o, O_SCENE) == c.m_scene, "%sscene", n.c_str());
    }
}

// ---- the message queue: the original's MSVC6 deque (begin iterator +4: first, last, next, node; end +0x14; the map
// +0x24, its size +0x28; the count +0x2C; 170 messages a block) ----
uint8_t *const ORIG_QUEUE = OriginalAt<uint8_t *>(0x008ae590);
std::vector<const uint8_t *> OrigQueue()
{
    std::vector<const uint8_t *> out;
    const uint32_t count = OF<uint32_t>(ORIG_QUEUE, 0x2C);
    if (count == 0)
        return out;
    uint8_t *first = OF<uint8_t *>(ORIG_QUEUE, 0x4), *next = OF<uint8_t *>(ORIG_QUEUE, 0xC);
    uint8_t **node = OF<uint8_t **>(ORIG_QUEUE, 0x10);
    size_t pos = size_t(next - first) / 0x18;
    for (uint32_t i = 0; i < count; ++i, ++pos)
        out.push_back(node[pos / 170] + (pos % 170) * 0x18);
    return out;
}
void ClearQueues()
{
    g_controlMessages.clear();
    // (The original's emptied by hand: its blocks and map freed, its words zero.)
    uint8_t **map = OF<uint8_t **>(ORIG_QUEUE, 0x24);
    const uint32_t mapSize = OF<uint32_t>(ORIG_QUEUE, 0x28);
    if (map) {
        uint8_t **firstNode = OF<uint8_t **>(ORIG_QUEUE, 0x10), **lastNode = OF<uint8_t **>(ORIG_QUEUE, 0x20);
        for (uint8_t **n = firstNode; n && n <= lastNode && n < map + mapSize; ++n)
            std::free(*n);
        std::free(map);
    }
    std::memset(ORIG_QUEUE, 0, 0x30);
}
void CompareQueues()
{
    const std::vector<const uint8_t *> o = OrigQueue();
    CHECK(o.size() == g_controlMessages.size(), "queue %u / %u", unsigned(o.size()), unsigned(g_controlMessages.size()));
    if (o.size() == g_controlMessages.size()) {
        for (size_t i = 0; i < o.size(); ++i) {
            const std::string why = MessageDiffers(g_controlMessages[i], o[i]);
            CHECK(why.empty(), "queued %u: %s", unsigned(i), why.c_str());
        }
    }
}

// ---- random trees ----
const uint32_t NOTIFICATIONS[] = {CONTROL_NOTIFY_CLICK, CONTROL_NOTIFY_02, CONTROL_NOTIFY_ENTER, CONTROL_NOTIFY_LEAVE, CONTROL_NOTIFY_05,
                                  CONTROL_NOTIFY_PRESS, CONTROL_NOTIFY_RELEASE, CONTROL_NOTIFY_08, CONTROL_NOTIFY_09, CONTROL_NOTIFY_0A,
                                  CONTROL_NOTIFY_0B, CONTROL_NOTIFY_HIDDEN, CONTROL_NOTIFY_SHOWN, CONTROL_NOTIFY_WHEEL,
                                  CONTROL_NOTIFY_KEY_CLICK, CONTROL_NOTIFY_CHANGED, CONTROL_NOTIFY_13};
// (Self routes that raise notifications - show, hide, notify - only on these, and notify only those of NOTIFY_RAISED:
// no cycle.)
const uint32_t NOTIFY_RAISING[] = {CONTROL_NOTIFY_CLICK, CONTROL_NOTIFY_02, CONTROL_NOTIFY_ENTER, CONTROL_NOTIFY_LEAVE, CONTROL_NOTIFY_05,
                                   CONTROL_NOTIFY_KEY_CLICK};
const uint32_t NOTIFY_RAISED[] = {CONTROL_NOTIFY_08, CONTROL_NOTIFY_09, CONTROL_NOTIFY_0A, CONTROL_NOTIFY_0B, CONTROL_NOTIFY_13,
                                  CONTROL_NOTIFY_CHANGED, CONTROL_NOTIFY_PRESS};
const uint32_t QUIET_CODES[] = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT,
                                CONTROL_MSG_ADD_CALLBACK, CONTROL_MSG_REMOVE_CALLBACK, CONTROL_MSG_SET_INDEX, CONTROL_MSG_GET_INDEX,
                                CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE, CONTROL_MSG_REFRESH, 0x1000000, 0x100000D, 0x2000001,
                                0x3000FFF, 0};
uint32_t RandomNotification()
{
    if (P(0.05))
        return P(0.5) ? 0 : U32() & ~CONTROL_NOTIFY_MASK;
    if (P(0.05))
        return CONTROL_NOTIFY_ANY;
    return Pick(NOTIFICATIONS);
}
uintptr_t RandomObject() { return P(0.2) ? 0 : reinterpret_cast<uintptr_t>(g_objects[I(0, 1)]); }
uintptr_t RandomFunction() { return P(0.15) ? 0 : g_portCbs[I(0, 2)]; }
// A message: `quiet` only codes that raise no notification.
S_ControlMessage RandomMessage(bool quiet)
{
    S_ControlMessage m;
    m.target = P(0.5) ? 0xFFFFFFFF : P(0.7) && !g_portNodes.empty() ? g_portNodes[size_t(I(0, int(g_portNodes.size()) - 1))]->m_id : U32();
    if (quiet || P(0.6)) {
        m.id = Pick(QUIET_CODES);
    } else {
        const uint32_t loud[] = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY};
        m.id = Pick(loud);
    }
    if (P(0.1))
        m.id |= U32() & 0xF8000000u;   // (bits outside both masks)
    m.value = U32();
    m.value2 = U32();
    m.value3 = U32();
    // (A notify message's value only from NOTIFY_RAISED: a callback's values in one would make a cycle.)
    if ((m.id & CONTROL_MSG_MASK) == CONTROL_MSG_ADD_CALLBACK || (m.id & CONTROL_MSG_MASK) == CONTROL_MSG_REMOVE_CALLBACK ||
        ((m.id & CONTROL_MSG_MASK) != CONTROL_MSG_NOTIFY && P(0.1))) {
        m.value = P(0.15) ? 0 : RandomNotification();
        m.value2 = RandomObject();
        m.value3 = RandomFunction();
    } else if ((m.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY) {
        m.value = P(0.9) ? Pick(NOTIFY_RAISED) : U32() & ~NOTIFICATION_BIT_FOR_TEST;
    }
    if (P(0.1))
        m.text = P(0.5) ? "route text" : "x";
    return m;
}

void ClearPortTree()
{
    for (C_Control *c : g_portNodes) {
        c->m_children.clear();   // (each deleted on its own below)
        delete c;
    }
    g_portNodes.clear();
}

void RandomTree()
{
    ClearPortTree();
    const int count = P(0.1) ? 1 : I(2, MAX_NODES);
    std::vector<int> depth;
    const uint32_t top = P(0.8) ? (uint32_t(I(1, 63)) << 26) : 0;
    for (int i = 0; i < count; ++i) {
        C_Control *parent = i == 0 ? nullptr : g_portNodes[size_t(I(0, i - 1))];
        int d = 0;
        uint32_t id = top;
        if (parent) {
            const size_t pi = size_t(std::find(g_portNodes.begin(), g_portNodes.end(), parent) - g_portNodes.begin());
            d = depth[pi] + 1;
            const uint32_t index = uint32_t(parent->m_children.size() + 1);
            const int shift = d == 1 ? 20 : d == 2 ? 12 : 4;
            id = d <= 3 ? parent->m_id | (index << shift) : parent->m_id;
        }
        if (P(0.05))
            id = P(0.5) ? 0xFFFFFFFF : U32();
        if (P(0.05) && i > 0)
            id = g_portNodes[size_t(I(0, i - 1))]->m_id;
        auto *c = new C_LoggedControl(reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000 + I(0, 3))), id, parent);
        if (parent && P(0.95))
            parent->m_children.push_back(c);
        g_portNodes.push_back(c);
        depth.push_back(d);
        c->m_frame = reinterpret_cast<I3D_frame *>(&g_frames[I(0, 2)]);
        c->m_type = P(0.5) ? 0 : I(-1, 0x30);
        c->m_value = P(0.5) ? 0 : U32();
        c->m_index = P(0.5) ? -1 : I(-3, 30);
        c->m_inactive = P(0.3);
        c->m_shown = P(0.7);
        c->m_takesInput = P(0.7);
        for (int k = I(0, 4); k > 0; --k) {
            // A route: its notification, its message. Self routes that raise notifications only on the raising set.
            S_ControlMessage m = RandomMessage(false);
            const bool loud = (m.id & CONTROL_MSG_MASK) == CONTROL_MSG_SHOW || (m.id & CONTROL_MSG_MASK) == CONTROL_MSG_HIDE ||
                              (m.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY;
            const uint32_t on = loud ? Pick(NOTIFY_RAISING) : RandomNotification();
            m.id = (m.id & ~CONTROL_NOTIFY_MASK) | (on & CONTROL_NOTIFY_MASK);
            c->m_routes.push_back(m);
        }
        for (int k = I(0, 3); k > 0; --k) {
            S_ControlCallback cb;
            cb.notification = P(0.1) ? 0 : RandomNotification();
            cb.object = reinterpret_cast<void *>(RandomObject());
            cb.function = reinterpret_cast<ControlCallback>(RandomFunction());
            c->m_callbacks.push_back(cb);
        }
        if (P(0.2)) {
            c->m_anyCallback.notification = P(0.8) ? CONTROL_NOTIFY_ANY : RandomNotification();
            c->m_anyCallback.object = reinterpret_cast<void *>(RandomObject());
            c->m_anyCallback.function = reinterpret_cast<ControlCallback>(g_portCbs[I(0, 2)]);
        }
    }
    // (A stray parent link now and then: not in its parent's children.)
    g_cbResults.clear();
    for (int k = 0; k < 7; ++k)
        g_cbResults.push_back(P(0.7));
}

// ---- one call on both sides ----
template <class PortCall, class OrigCall> void Diff(PortCall port, OrigCall orig)
{
    ToOrig();
    ClearQueues();
    for (S_FakeFrame &f : g_frames)
        std::memset(f.bytes, 0xAB, sizeof f.bytes);
    g_log.clear();
    g_cbNext = 0;
    port();
    const std::vector<std::string> portLog = g_log;
    uint32_t portData[3];
    for (int i = 0; i < 3; ++i)
        portData[i] = FrameData(i);
    for (S_FakeFrame &f : g_frames)
        std::memset(f.bytes, 0xAB, sizeof f.bytes);
    g_log.clear();
    g_cbNext = 0;
    orig();
    CHECK(g_log == portLog, "calls: %s / %s", Joined(g_log).c_str(), Joined(portLog).c_str());
    for (int i = 0; i < 3; ++i)
        CHECK(FrameData(i) == portData[i], "frame%d data %x / %x", i, FrameData(i), portData[i]);
    CompareTrees();
    CompareQueues();
}

size_t RandomNode() { return size_t(I(0, int(g_portNodes.size()) - 1)); }
uint32_t RandomId()
{
    if (P(0.7))
        return g_portNodes[RandomNode()]->m_id ^ (P(0.2) ? (1u << I(0, 31)) : 0);
    return P(0.3) ? 0xFFFFFFFF : U32();
}

using ThisFn = void(__attribute__((thiscall)) *)(void *);
using IntFn = int(__attribute__((thiscall)) *)(void *, int);
using Int2Fn = int(__attribute__((thiscall)) *)(void *, int, int);
using BoolFn = bool(__attribute__((thiscall)) *)(void *);
using BoolIntFn = bool(__attribute__((thiscall)) *)(void *, int);
using FindFn = void *(__attribute__((thiscall)) *)(void *, uint32_t, int);
using MsgFn = bool(__attribute__((thiscall)) *)(void *, void *);
using NotifyFn = bool(__attribute__((thiscall)) *)(void *, uint32_t, uint32_t);
using CreateFn = bool(__attribute__((thiscall)) *)(void *, void *, void *, void *);

void TestFindChild()
{
    const size_t i = RandomNode();
    const uint32_t id = RandomId();
    int rp = 0, ro = 0;
    Diff([&] { rp = g_portNodes[i]->FindChild(id); }, [&] { ro = OriginalAt<IntFn>(0x0065cb50)(g_origNodes[i], int(id)); });
    CHECK(rp == ro, "FindChild %d / %d", ro, rp);
}

void TestFindControl()
{
    const size_t i = RandomNode();
    const uint32_t id = RandomId();
    const bool up = P(0.5);
    C_Control *rp = nullptr;
    void *ro = nullptr;
    Diff([&] { rp = g_portNodes[i]->FindControl(id, up); }, [&] { ro = OriginalAt<FindFn>(0x0065cae0)(g_origNodes[i], id, up); });
    CHECK(Name(rp) == Name(ro) || (!rp && !ro), "FindControl %s / %s", Name(ro).c_str(), Name(rp).c_str());
}

void TestSmall()
{
    const size_t i = RandomNode();
    C_Control *p = g_portNodes[i];
    switch (I(0, 9)) {
    case 0: {
        const int index = P(0.8) ? I(-1, int(p->m_children.size()) + 1) : Pick({int(0x80000000), 1000, -100});
        bool rp = false, ro = false;
        Diff([&] { rp = p->IsChild(index); }, [&] { ro = OriginalAt<BoolIntFn>(0x0064dce0)(g_origNodes[i], index); });
        CHECK(rp == ro, "IsChild %d / %d", ro, rp);
        break;
    }
    case 1: {
        bool rp = false, ro = false;
        Diff([&] { rp = p->TakesInput(); }, [&] { ro = OriginalAt<BoolFn>(0x0064dd10)(g_origNodes[i]); });
        CHECK(rp == ro, "TakesInput %d / %d", ro, rp);
        break;
    }
    case 2:
        Diff([&] { p->Show(); }, [&] { OriginalAt<ThisFn>(0x0065ca80)(g_origNodes[i]); });
        break;
    case 3:
        Diff([&] { p->Hide(); }, [&] { OriginalAt<ThisFn>(0x0065cab0)(g_origNodes[i]); });
        break;
    case 4:
        Diff([&] { p->Refresh(); }, [&] { reinterpret_cast<ThisFn>(static_cast<uintptr_t>(g_origVt[0x18 / 4]))(g_origNodes[i]); });
        break;
    case 5: {
        const bool on = P(0.5);
        Diff([&] { p->SetInactive(on); }, [&] { OriginalAt<BoolIntFn>(0x00661c20)(g_origNodes[i], on); });
        break;
    }
    case 6: {
        // The base's trivial slots: highlight frame, Tick, Unknown20, OnChar, IsEditing, BeginEdit, SetStrings.
        const int arg = I(-5, 500);
        void *hp = nullptr, *ho = nullptr;
        bool b1p = false, b1o = false, b2p = false, b2o = false, b3p = false, b3o = false;
        int ep = 0, eo = 0;
        Diff(
            [&] {
                hp = p->GetHighlightFrame();
                p->Tick(arg);
                b1p = p->Unknown20(arg);
                b2p = p->OnChar(arg);
                b3p = p->IsEditing();
                ep = p->BeginEdit(arg);
                p->SetStrings(std::vector<C_str>());
            },
            [&] {
                ho = OriginalAt<void *(__attribute__((thiscall)) *)(void *)>(0x0064dd30)(g_origNodes[i]);
                OriginalAt<IntFn>(0x0064dd40)(g_origNodes[i], arg);
                b1o = OriginalAt<BoolIntFn>(0x0064dd50)(g_origNodes[i], arg);
                b2o = OriginalAt<BoolIntFn>(0x0064dd60)(g_origNodes[i], arg);
                b3o = OriginalAt<BoolFn>(0x0064dd70)(g_origNodes[i]);
                eo = OriginalAt<IntFn>(0x0064df40)(g_origNodes[i], arg);
                uint32_t vec[4] = {};
                OriginalAt<MsgFn>(0x0065d050)(g_origNodes[i], vec);
            });
        CHECK(hp == ho && b1p == b1o && b2p == b2o && b3p == b3o && ep == eo, "trivial slots");
        break;
    }
    default: {
        // The input slots: each notifies, 7 (OnKeyClick: then OnClick).
        struct S_Slot {
            uint32_t at;
            int (C_Control::*port)(int);
        };
        static const S_Slot slots[] = {
            {0x0064dd80, &C_Control::OnEnter},   {0x0064dda0, &C_Control::OnLeave},   {0x0064ddc0, &C_Control::OnClick},
            {0x0064dde0, &C_Control::OnInput02}, {0x0064de00, &C_Control::OnInput05}, {0x0064de20, &C_Control::OnRelease},
            {0x0064de40, &C_Control::OnPress},   {0x0064de80, &C_Control::OnInput08}, {0x0064dea0, &C_Control::OnInput09},
            {0x0064dec0, &C_Control::OnInput0A}, {0x0064dee0, &C_Control::OnKeyClick}, {0x0064df00, &C_Control::OnInput13},
            {0x0064df20, &C_Control::OnInput0B},
        };
        const int k = I(0, int(sizeof slots / sizeof slots[0]));
        const int id = I(-2, 100), amount = I(-5, 5);
        int rp = 0, ro = 0;
        if (k == int(sizeof slots / sizeof slots[0])) {
            Diff([&] { rp = p->OnWheel(id, amount); }, [&] { ro = OriginalAt<Int2Fn>(0x0064de60)(g_origNodes[i], id, amount); });
        } else {
            Diff([&] { rp = (p->*slots[k].port)(id); }, [&] { ro = OriginalAt<IntFn>(slots[k].at)(g_origNodes[i], id); });
        }
        CHECK(rp == ro, "input slot %d: %d / %d", k, ro, rp);
        break;
    }
    }
}

void TestHandleMessage()
{
    const size_t i = RandomNode();
    S_ControlMessage mp = RandomMessage(P(0.5));
    if (P(0.2))
        mp.id |= RandomNotification() & CONTROL_NOTIFY_MASK;   // (a route added)
    // (A message that raises notifications on a control: only those with no loud self route on them.)
    if ((mp.id & CONTROL_NOTIFY_MASK) == 0 && (mp.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY)
        mp.value = Pick(NOTIFY_RAISED) | (P(0.1) ? 0x80000000u : 0);
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const int how = I(0, 2);
    mp_before = mp;
    Diff(
        [&] {
            if (how == 0)
                rp = g_portNodes[i]->HandleMessage(&mp);
            else if (how == 1)
                rp = g_portNodes[i]->HandleMessageCopy(mp);
            else {
                std::vector<S_ControlMessage> list{mp};
                for (int k = I(0, 2); k > 0; --k)
                    list.push_back(RandomMessage(true));
                g_listForOrig = list;
                rp = g_portNodes[i]->HandleMessages(list);
                g_listAfter = list;
            }
        },
        [&] {
            if (how == 0) {
                MessageToOrig(mp_before, mo);
                ro = OriginalAt<MsgFn>(OF<uint32_t>(OriginalAt<uint8_t *>(O_VTABLE), 0x6C))(g_origNodes[i], mo);
            } else if (how == 1) {
                MessageToOrig(mp_before, mo);
                ro = reinterpret_cast<MsgFn>(static_cast<uintptr_t>(g_origVt[0x74 / 4]))(g_origNodes[i], mo);
            } else {
                std::vector<uint8_t> buffer(g_listForOrig.size() * 0x18);
                for (size_t k = 0; k < g_listForOrig.size(); ++k)
                    MessageToOrig(g_listForOrig[k], buffer.data() + k * 0x18);
                uint8_t *data = static_cast<uint8_t *>(std::malloc(buffer.size()));
                std::memcpy(data, buffer.data(), buffer.size());
                uint8_t *vec[4] = {nullptr, data, data + buffer.size(), data + buffer.size()};
                ro = OriginalAt<MsgFn>(0x0065d220)(g_origNodes[i], vec);
                for (size_t k = 0; k < g_listAfter.size(); ++k) {
                    const std::string why = MessageDiffers(g_listAfter[k], data + k * 0x18);
                    CHECK(why.empty(), "HandleMessages' message %u after: %s", unsigned(k), why.c_str());
                }
            }
        });
    CHECK(rp == ro, "HandleMessage %d / %d", ro, rp);
    if (how == 0) {
        const std::string why = MessageDiffers(mp, mo);
        CHECK(why.empty(), "the message after: %s", why.c_str());
    }
}

void TestNotify()
{
    const size_t i = RandomNode();
    // (Only notifications no loud self route is on, or those that cannot cycle.)
    const uint32_t notification = P(0.8) ? (P(0.5) ? Pick(NOTIFY_RAISING) : Pick(NOTIFY_RAISED)) : RandomNotification();
    const uint32_t value = P(0.5) ? 0 : U32();
    if (P(0.3)) {
        bool rp = false, ro = false;
        Diff([&] { rp = g_portNodes[i]->CallCallbacks(notification, value); },
             [&] { ro = OriginalAt<NotifyFn>(0x0065cc20)(g_origNodes[i], notification, value); });
        CHECK(rp == ro, "CallCallbacks %d / %d", ro, rp);
        return;
    }
    Diff([&] { g_portNodes[i]->Notify(notification, value); }, [&] { OriginalAt<NotifyFn>(0x0065d270)(g_origNodes[i], notification, value); });
}

void TestCallbacks()
{
    const size_t i = RandomNode();
    S_ControlMessage m;
    m.value = P(0.15) ? 0 : RandomNotification();
    m.value2 = RandomObject();
    m.value3 = RandomFunction();
    // (Removing: often one that is there.)
    const C_Control &c = *g_portNodes[i];
    if (!c.m_callbacks.empty() && P(0.5)) {
        const S_ControlCallback &cb = c.m_callbacks[size_t(I(0, int(c.m_callbacks.size()) - 1))];
        m.value = P(0.6) ? cb.notification : 0;
        m.value2 = P(0.5) ? reinterpret_cast<uintptr_t>(cb.object) : 0;
        m.value3 = P(0.5) ? reinterpret_cast<uintptr_t>(cb.function) : 0;
    }
    uint8_t mo[0x18];
    const bool add = P(0.5);
    bool rp = false, ro = false;
    Diff([&] { rp = add ? g_portNodes[i]->AddCallback(m) : g_portNodes[i]->RemoveCallback(m); },
         [&] {
             MessageToOrig(m, mo);
             ro = OriginalAt<MsgFn>(add ? 0x0065cde0 : 0x0065ccc0)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "%s %d / %d", add ? "AddCallback" : "RemoveCallback", ro, rp);
}

// Create: the frame's data, the type, a child made per child description (the factory faked on both sides).
struct S_OrigDesc {
    uint8_t bytes[0x40];
};
const S_ControlDesc *g_descs[4];
S_OrigDesc g_origDescs[4];
int DescIndex(const void *d)
{
    for (int i = 0; i < 4; ++i)
        if (d == g_descs[i] || d == &g_origDescs[i])
            return i;
    return -1;
}
bool g_createChildResult;
bool __attribute__((thiscall)) O_CreateChild(void *self, void *frame, void *root, void *desc)
{
    Log("CreateChild %s frame%d root%d desc%d", Name(self).c_str(), FrameIndex(frame), FrameIndex(root), DescIndex(desc));
    return g_createChildResult;
}
void TestCreate()
{
    const size_t i = RandomNode();
    // The descriptions: 0 the one passed, its children 1..3.
    static S_ControlDesc *owned[4];
    for (S_ControlDesc *&d : owned) {
        if (d) {
            d->children.clear();
            delete d;
        }
        d = nullptr;
    }
    owned[0] = new S_ControlDesc(I(-1, 0x30), "desc", U32());
    const int children = I(0, 3);
    for (int k = 1; k <= children; ++k) {
        owned[k] = new S_ControlDesc(I(0, 0x28), "child", U32());
        owned[0]->children.push_back(owned[k]);
    }
    for (int k = 0; k < 4; ++k)
        g_descs[k] = owned[k];
    std::memset(g_origDescs, 0, sizeof g_origDescs);
    uint8_t *od = g_origDescs[0].bytes;
    OF<int>(od, 0x0C) = owned[0]->type;
    std::vector<uint32_t> childPtrs;
    for (int k = 1; k <= children; ++k)
        childPtrs.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_origDescs[k])));
    OVecPut(od, 0x30, childPtrs.data(), childPtrs.size() * 4);
    const bool noDesc = P(0.1);
    I3D_frame *frame = reinterpret_cast<I3D_frame *>(&g_frames[I(0, 2)]), *root = reinterpret_cast<I3D_frame *>(&g_frames[I(0, 2)]);
    g_createChildResult = P(0.5);
    bool rp = false, ro = false;
    Diff([&] { rp = g_portNodes[i]->Create(frame, root, noDesc ? nullptr : owned[0]); },
         [&] { ro = OriginalAt<CreateFn>(0x00661bc0)(g_origNodes[i], frame, root, noDesc ? nullptr : od); });
    CHECK(rp == ro, "Create %d / %d", ro, rp);
}

// The constructor (every field) and the destructor (children deleted: the original's through its own vtable).
void TestLifetime()
{
    const uint32_t id = U32();
    I3D_scene *scene = reinterpret_cast<I3D_scene *>(uintptr_t(U32()));
    C_Control *parent = g_portNodes[RandomNode()];
    ToOrig();
    auto *port = new C_Control(scene, id, parent);
    uint8_t *orig = static_cast<uint8_t *>(OrigAlloc(O_SIZE + 0x10));
    std::memset(orig, 0xCD, O_SIZE);
    OriginalAt<void(__attribute__((thiscall)) *)(void *, void *, uint32_t, void *)>(0x0064dfe0)(orig, scene, id, ToOrigNode(parent));
    g_portNodes.push_back(port);
    g_origNodes.push_back(orig);
    // (Compared as part of a tree: the vectors empty, the fields the constructor's.)
    const C_Control &c = *port;
    CHECK(OVecBytes(orig, O_CHILDREN) == 0 && OVecBytes(orig, O_ROUTES) == 0 && OVecBytes(orig, O_CALLBACKS) == 0, "new: vectors");
    CHECK(OF<uint32_t>(orig, O_ANY) == 0 && OF<uint32_t>(orig, O_ANY + 4) == 0 && OF<uint32_t>(orig, O_ANY + 8) == 0, "new: any callback");
    CHECK(OF<uint32_t>(orig, 0) == O_VTABLE && OF<uint8_t *>(orig, O_PARENT) == ToOrigNode(parent) && OF<void *>(orig, O_FRAME) == nullptr &&
              OF<int>(orig, O_TYPE) == c.m_type && OF<uint32_t>(orig, O_VALUE) == c.m_value && OF<int>(orig, O_INDEX) == c.m_index &&
              OF<uint32_t>(orig, O_ID) == c.m_id && OF<bool>(orig, O_INACTIVE) == c.m_inactive && OF<bool>(orig, O_SHOWN) == c.m_shown &&
              OF<bool>(orig, O_TAKES) == c.m_takesInput && OF<I3D_scene *>(orig, O_SCENE) == c.m_scene,
          "new: fields");
    g_portNodes.pop_back();
    g_origNodes.pop_back();
    delete port;
    // The destructor: a subtree deleted on both sides. (The original's subtree copied into blocks of its own, which its
    // operator delete - free - may free; the test's blocks are freed by ToOrig.)
    ToOrig();
    const size_t i = RandomNode();
    std::map<uint8_t *, uint8_t *> moved;
    std::vector<uint8_t *> stack{g_origNodes[i]};
    while (!stack.empty()) {
        uint8_t *n = stack.back();
        stack.pop_back();
        auto *copy = static_cast<uint8_t *>(std::malloc(O_SIZE));
        std::memcpy(copy, n, O_SIZE);
        moved[n] = copy;
        const size_t kids = OVecBytes(n, O_CHILDREN) / 4;
        for (size_t k = 0; k < kids; ++k)
            stack.push_back(reinterpret_cast<uint8_t **>(OVecData(n, O_CHILDREN))[k]);
    }
    for (auto &m : moved) {
        uint8_t **kids = reinterpret_cast<uint8_t **>(OVecData(m.second, O_CHILDREN));
        const size_t n = OVecBytes(m.second, O_CHILDREN) / 4;
        for (size_t k = 0; k < n; ++k)
            if (moved.count(kids[k]))
                kids[k] = moved[kids[k]];
    }
    // (Shared children: a child listed twice would be deleted twice on both sides; the trees have none.)
    g_log.clear();
    OriginalAt<IntFn>(OF<uint32_t>(OriginalAt<uint8_t *>(O_VTABLE), 0x80))(moved[g_origNodes[i]], 1);
    CHECK(g_log.empty(), "destructor's calls: %s", Joined(g_log).c_str());
    // The port's: its own children deleted with it (taken out of the list first).
    std::vector<C_Control *> gone;
    std::vector<C_Control *> todo{g_portNodes[i]};
    while (!todo.empty()) {
        C_Control *n = todo.back();
        todo.pop_back();
        gone.push_back(n);
        for (C_Control *k : n->m_children)
            todo.push_back(k);
    }
    C_Control *root = g_portNodes[i];
    if (root->m_parent) {
        std::vector<C_Control *> &siblings = root->m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), root), siblings.end());
    }
    for (C_Control *n : gone)
        g_portNodes.erase(std::find(g_portNodes.begin(), g_portNodes.end(), n));
    g_log.clear();
    delete root;
    CHECK(g_log.empty(), "the port's destructor's calls: %s", Joined(g_log).c_str());
    // (The original's nodes of the subtree out of the list too: the lists stay parallel.)
    g_origNodes.clear();
}

// ---- the descriptions ----
const uint32_t D_VTABLE = 0x00813ab0;
using DescCtorFn = void(__attribute__((thiscall)) *)(void *, int, const char *, uint32_t);
using AddChildFn = void *(__attribute__((thiscall)) *)(void *, int, const char *);
std::string DescDiffers(const S_ControlDesc *p, uint8_t *o, const std::string &at)
{
    char text[200];
    if (OF<uint32_t>(o, 0) != D_VTABLE)
        return at + "vtable";
    if (TextOf(OF<uint32_t>(o, 4)) != TextOf(p->name))
        return at + "name " + TextOf(OF<uint32_t>(o, 4)) + " / " + TextOf(p->name);
    if (OF<uint32_t>(o, 8) != p->id || OF<int>(o, 0xC) != p->type) {
        std::snprintf(text, sizeof text, "id %x type %d / %x %d", OF<uint32_t>(o, 8), OF<int>(o, 0xC), p->id, p->type);
        return at + text;
    }
    if (OVecBytes(o, 0x10) != p->messages.size() * 0x18)
        return at + "messages " + std::to_string(OVecBytes(o, 0x10) / 0x18) + " / " + std::to_string(p->messages.size());
    for (size_t k = 0; k < p->messages.size(); ++k) {
        const std::string why = MessageDiffers(p->messages[k], OVecData(o, 0x10) + k * 0x18);
        if (!why.empty())
            return at + "message " + std::to_string(k) + ": " + why;
    }
    if (OVecBytes(o, 0x20) != p->strings.size() * 4)
        return at + "strings";
    if (OVecBytes(o, 0x30) != p->children.size() * 4)
        return at + "children " + std::to_string(OVecBytes(o, 0x30) / 4) + " / " + std::to_string(p->children.size());
    for (size_t k = 0; k < p->children.size(); ++k) {
        const std::string why = DescDiffers(p->children[k], reinterpret_cast<uint8_t **>(OVecData(o, 0x30))[k], at + std::to_string(k) + ".");
        if (!why.empty())
            return why;
    }
    return "";
}
void TestDescriptions()
{
    const int type = I(-1, 0x28);
    const uint32_t id = P(0.5) ? (uint32_t(I(1, 63)) << 26) | (P(0.5) ? uint32_t(I(1, 63)) << 20 : 0) : U32();
    static const char *const names[] = {"button*", "", "frame_a", "x"};
    const char *name = Pick(names);
    auto *port = new S_ControlDesc(type, name, id);
    auto *orig = static_cast<uint8_t *>(std::malloc(0x40));
    std::memset(orig, 0xCD, 0x40);
    OriginalAt<DescCtorFn>(0x00615090)(orig, type, name, id);
    std::vector<S_ControlDesc *> portList{port};
    std::vector<uint8_t *> origList{orig};
    for (int op = I(0, 25); op > 0; --op) {
        const size_t at = size_t(I(0, int(portList.size()) - 1));
        if (P(0.4)) {
            const int t = I(-1, 0x28);
            const char *n = Pick(names);
            // (More than 255 children now and then: the index's byte wraps.)
            const int times = P(0.02) ? 260 : 1;
            for (int k = 0; k < times; ++k) {
                portList.push_back(portList[at]->AddChild(t, n));
                origList.push_back(static_cast<uint8_t *>(OriginalAt<AddChildFn>(0x0061da70)(origList[at], t, n)));
            }
            ++checks;
            continue;
        }
        const uint32_t target = P(0.5) ? 0xFFFFFFFF : U32(), mid = U32(), value = U32(), v2 = U32(), v3 = U32();
        switch (I(0, 3)) {
        case 0:
            AddDescMessage(portList[at], target, mid, value);
            OriginalAt<void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t)>(0x00614e80)(origList[at], target, mid, value);
            break;
        case 1:
            AddDescMessage(portList[at], target, mid, value, v2);
            OriginalAt<void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00614f10)(origList[at], target, mid, value, v2);
            break;
        case 2:
            AddDescMessage(portList[at], target, mid, value, v2, v3);
            OriginalAt<void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0061e100)(origList[at], target, mid,
                                                                                                             value, v2, v3);
            break;
        default: {
            const C_str text(P(0.3) ? "" : "a description's text");
            AddDescMessage(portList[at], target, mid, value, text);
            // (The original's takes the C_str by value, one pointer, and releases it: a reference of its own.)
            C_str arg(text);
            void *rep = arg.m_rep;
            arg.m_rep = nullptr;
            OriginalAt<void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t, void *)>(0x00614fa0)(origList[at], target, mid, value, rep);
            break;
        }
        }
    }
    const std::string why = DescDiffers(port, orig, "");
    CHECK(why.empty(), "descriptions: %s", why.c_str());
    // The destructors: the children deleted (the original's through its own vtable, freeing with free).
    delete port;
    OriginalAt<IntFn>(0x006151b0)(orig, 1);
    ++checks;
}

// ---- the queue ----
void TestPost()
{
    // Several messages, past a block's 170 now and then.
    const int n = P(0.1) ? I(160, 400) : I(1, 5);
    Diff(
        [&] {
            g_postList.clear();
            for (int k = 0; k < n; ++k) {
                g_postList.push_back(RandomMessage(true));
                PostControlMessage(g_postList.back());
            }
        },
        [&] {
            for (const S_ControlMessage &m : g_postList) {
                uint8_t mo[0x18];
                MessageToOrig(m, mo);
                OriginalAt<MsgFn>(0x00615a90)(ORIG_QUEUE, mo);
            }
        });
}

void SetUp()
{
    for (void *&slot : g_frameVt)
        slot = reinterpret_cast<void *>(F_Other);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    for (S_FakeFrame &f : g_frames)
        f.vtable = g_frameVt;
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(O_New));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(O_Delete));
    RedirectOriginal(0x0065d470, reinterpret_cast<const void *>(O_CreateChild));
    std::memcpy(g_origVt, OriginalAt<void *>(O_VTABLE), sizeof g_origVt);
    g_origVt[0x18 / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(O_LoggedRefresh));
    g_origVt[0x74 / 4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(O_LoggedCopy));
    std::memset(ORIG_QUEUE, 0, 0x30);
}

}  // namespace

// The port's factory, faked (control_factory.cpp is not linked: its controls are controls.cpp's).
bool C_Control::CreateChild(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    Log("CreateChild %s frame%d root%d desc%d", Name(this).c_str(), FrameIndex(frame), FrameIndex(root), DescIndex(desc));
    return g_createChildResult;
}

namespace {
LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    // (The address as linked: the test may load elsewhere than its base 0x20000000.)
    const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    std::printf("control_diff: exception %08lx at %p (linked %08x) [%s round %d]\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, unsigned(at - base + 0x20000000), g_case, g_round);
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
// (HD2_CONTROL_FIRST_CHANCE: faults the original's own exception frames would hide.)
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW) {
        const uintptr_t at = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        char text[160];
        const int n = std::snprintf(text, sizeof text, "first chance %08lx at %08x (linked %08x) [%s round %d]\n", code, unsigned(at),
                                    unsigned(at - base + 0x20000000), g_case, g_round);
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, DWORD(n), &written, nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_CONTROL_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("control_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_CONTROL_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_CONTROL_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_CONTROL_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"FindChild", TestFindChild},   {"FindControl", TestFindControl}, {"Small", TestSmall},         {"HandleMessage", TestHandleMessage},
        {"Notify", TestNotify},         {"Callbacks", TestCallbacks},     {"Create", TestCreate},       {"Lifetime", TestLifetime},
        {"Descriptions", TestDescriptions}, {"Post", TestPost},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        if (std::getenv("HD2_CONTROL_TRACE"))
            std::printf("case %s\n", c.name);
        const char *trace = std::getenv("HD2_CONTROL_TRACE");
        for (g_round = 0; g_round < rounds; ++g_round) {
            if (trace && std::strcmp(trace, c.name) == 0)
                std::printf("round %d\n", g_round);
            RandomTree();
            c.run();
        }
    }
    std::printf("control_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
