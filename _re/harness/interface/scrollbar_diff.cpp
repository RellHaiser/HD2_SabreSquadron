// Human part 73a: the inventory's scroll bars (game/interface_inventory.cpp: C_InventoryScrollBar with its S_Button
// arrows and S_Slider) against the original's (0x004a0270, 0x004a0350, 0x004ada20..0x004ae790). MinGW;
// build_scrollbar.bat; runs scrollbar_diff.exe ("scrollbar_diff: N checks, 0 failures").
// interface_inventory.cpp is compiled into this file; the kit (interface_kit.inc) fakes the frames (their bound's
// UpdateBound). Scenarios of the bar's operations (the constructor, Init, SetCount, Hover, Unhover, Press, Drag,
// Release, the destructor; the private SetDisabled, Step, OnSliderMoved; the arrows' Show, SetDisabled, Activate,
// Deactivate, Press; the slider's Layout, Show, SetDisabled, Activate, Deactivate, SetPosition, Follow, SetPositionAt,
// Drag) run from one start on each side, every call out logged by the same fakes, the bar's fields snapped after each
// operation, the frames compared at the end. The original's bar has a vtable at +0 (and its arrows and slider theirs):
// the port's fields sit 4 bytes lower, each side snapped by its own layout. The owner's OnScroll (the original's
// 0x004a2590, the port's C_InventoryScreen::OnScroll) is patched on both sides to the same logger.
// HD2_SCROLLBAR_ROUNDS: scenarios (default 40,000; HD2_SCROLLBAR_QUICK: 15,000); HD2_SCROLLBAR_CASE: one case only;
// HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
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
#include "game/interface_inventory.cpp"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

template <class T> T At(const void *p, int offset) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + offset); }
template <class T> void Put(void *p, int offset, T v) { std::memcpy(static_cast<uint8_t *>(p) + offset, &v, sizeof v); }

// ---- the frames: f0 the bar; f1 "barrow up*", f2 "barrow down*", f3 "slider*"; the arrows' looks f4..f6, f7..f9
// ("normal*", "actived*", "inactived*"); the slider's f10 "normal*", f11 "actived*", f12 "sliderbase*" ----
constexpr int F_BAR = 0;
struct S_Child {
    int parent;
    const char *name;
    int frame;
};
const S_Child CHILDREN[] = {{0, "barrow up*", 1},  {0, "barrow down*", 2}, {0, "slider*", 3},     {1, "normal*", 4},
                            {1, "actived*", 5},    {1, "inactived*", 6},   {2, "normal*", 7},     {2, "actived*", 8},
                            {2, "inactived*", 9},  {3, "normal*", 10},     {3, "actived*", 11},   {3, "sliderbase*", 12}};
constexpr int CHILD_COUNT = int(sizeof CHILDREN / sizeof CHILDREN[0]);
uint32_t g_missing;   // children not found (bits over CHILDREN)
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    const int parent = FrameIndex(f);
    for (int k = 0; k < CHILD_COUNT; ++k)
        if (CHILDREN[k].parent == parent && std::strcmp(CHILDREN[k].name, name) == 0)
            return g_missing >> k & 1 ? nullptr : FrameAt(CHILDREN[k].frame);
    return nullptr;
}
// UpdateBound: the bound taken as it is, made valid.
void UpdateBound(I3D_frame *f) { f->m_frameFlags |= FRMFLAGS_BOUND_VALID; }

// ---- the owners (each side's: OnScroll logged) ----
alignas(16) uint8_t g_origOwner[0x40], g_portOwner[0x40];
const void *g_bars[2];   // each side's bar
std::string PartName(const void *p) { return p == g_origOwner || p == g_portOwner ? "owner" : p && (p == g_bars[0] || p == g_bars[1]) ? "bar" : ""; }
void OnScroll(const void *owner, int id, int index) { Log("%s OnScroll %x %d", Name(owner).c_str(), id, index); }
void __attribute__((thiscall)) O_OnScroll(void *owner, int id, int index) { OnScroll(owner, id, index); }
// The original's floor (its CRT never started in the mapped copy: an infinity's errno would go through a null heap):
// the harness's (floor is exact either way).
double __cdecl O_Floor(double x) { return std::floor(x); }
// Patches the port's own code (its .text is read-only) with a jump.
void PatchPort(void *at, const void *to)
{
    DWORD old;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    uint8_t *p = static_cast<uint8_t *>(at);
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(p) + 5));
    p[0] = 0xE9;
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
}

// ---- the layouts: the original's (a vtable at +0 of the bar, its arrows and its slider) and the port's ----
struct S_Layout {
    int frame, id, disabled, owner, up, down, slider, step, count, index, part, unknown90;
    // the parts' (from the part)
    int pFrame, pId, pDisabled, pOwner, pNormal, pActive, pInactive, sBase, sRect, sHorizontal, sVertical, sPosition;
};
const S_Layout ORIG = {0x04, 0x08, 0x0C, 0x10, 0x14, 0x34, 0x54, 0x80, 0x84, 0x88, 0x8C, 0x90,
                       0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x1C, 0x20, 0x24, 0x25, 0x28};
using T_Bar = C_InventoryScrollBar;
using T_Button = C_InventoryScrollBar::S_Button;
using T_Slider = C_InventoryScrollBar::S_Slider;
const S_Layout PORT = {int(offsetof(T_Bar, m_frame)), int(offsetof(T_Bar, m_id)), int(offsetof(T_Bar, m_disabled)),
                       int(offsetof(T_Bar, m_owner)), int(offsetof(T_Bar, m_up)), int(offsetof(T_Bar, m_down)),
                       int(offsetof(T_Bar, m_slider)), int(offsetof(T_Bar, m_step)), int(offsetof(T_Bar, m_count)),
                       int(offsetof(T_Bar, m_index)), int(offsetof(T_Bar, m_part)), int(offsetof(T_Bar, m_unknown90)),
                       int(offsetof(T_Button, frame)), int(offsetof(T_Button, id)), int(offsetof(T_Button, disabled)),
                       int(offsetof(T_Button, owner)), int(offsetof(T_Button, normal)), int(offsetof(T_Button, active)),
                       int(offsetof(T_Button, inactive)), int(offsetof(T_Slider, base)), int(offsetof(T_Slider, rect)), int(offsetof(T_Slider, horizontal)),
                       int(offsetof(T_Slider, vertical)), int(offsetof(T_Slider, position))};
int g_side;
const S_Layout &L() { return g_side ? PORT : ORIG; }

std::string PartSnap(const uint8_t *p, bool slider)
{
    const S_Layout &l = L();
    char text[200];
    std::snprintf(text, sizeof text, "%s %x %d %s %s %s %s", Name(At<void *>(p, l.pFrame)).c_str(), At<int>(p, l.pId), At<uint8_t>(p, l.pDisabled),
                  Name(At<void *>(p, l.pOwner)).c_str(), Name(At<void *>(p, l.pNormal)).c_str(), Name(At<void *>(p, l.pActive)).c_str(),
                  Name(At<void *>(p, l.pInactive)).c_str());
    std::string s = text;
    if (slider) {
        const float *rect = At<const float *>(p, l.sRect);
        s += " rect";
        if (rect)
            for (int k = 0; k < 6; ++k)
                s += " " + FloatText(rect[k]);
        else
            s += " null";
        s += " " + std::to_string(At<uint8_t>(p, l.sHorizontal)) + " " + std::to_string(At<uint8_t>(p, l.sVertical)) + " " +
             FloatText(At<float>(p, l.sPosition));
    }
    return s;
}
std::string Snap(const uint8_t *b)
{
    const S_Layout &l = L();
    char text[200];
    std::snprintf(text, sizeof text, "bar %s %x %d %s step %s count %d index %d part %d u90 %d", Name(At<void *>(b, l.frame)).c_str(),
                  At<int>(b, l.id), At<uint8_t>(b, l.disabled), Name(At<void *>(b, l.owner)).c_str(), FloatText(At<float>(b, l.step)).c_str(),
                  At<int>(b, l.count), At<int>(b, l.index), At<int>(b, l.part), At<int>(b, l.unknown90));
    return std::string(text) + " | up " + PartSnap(b + l.up, false) + " | down " + PartSnap(b + l.down, false) + " | slider " +
           PartSnap(b + l.slider, true);
}

// ---- the operations ----
struct S_Op {
    int kind;
    int a = 0, b = 0;
    float f = 0.0f, g = 0.0f, h = 0.0f, k = 0.0f;
};
enum {
    OP_INIT, OP_SETCOUNT, OP_HOVER, OP_UNHOVER, OP_PRESS, OP_DRAG, OP_RELEASE, OP_SETDISABLED, OP_STEP, OP_SLIDERMOVED,
    OP_B_SHOW, OP_B_SETDISABLED, OP_B_ACTIVATE, OP_B_DEACTIVATE, OP_B_PRESS,
    OP_S_LAYOUT, OP_S_SHOW, OP_S_SETDISABLED, OP_S_ACTIVATE, OP_S_DEACTIVATE, OP_S_SETPOSITION, OP_S_FOLLOW, OP_S_SETAT, OP_S_DRAG,
    OP_DTOR, OP_B_INIT, OP_S_INIT,
    W_FRAME = 100, W_FLAGS
};

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
uint32_t g_seed;

float Coord() { return P(0.2) ? Pick({0.0f, 1.0f, -1.0f, 0.5f}) : F(-1.5f, 1.5f); }
// A frame's places: its world position, its local position (the slider's looks), its bound (min, max).
void RandomPlace(int i)
{
    I3D_frame *f = FrameAt(i);
    f->m_worldMatrix(3) = S_vector(Coord(), Coord(), Coord());
    f->m_localMatrix(3) = S_vector(Coord(), Coord(), Coord());
    S_vector a(Coord(), Coord(), Coord()), b(Coord(), Coord(), Coord());
    // (Now and then a flat bound: a zero range.)
    if (P(0.15))
        b = a;
    f->m_bound.bbox.min = S_vector(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
    f->m_bound.bbox.max = S_vector(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
    // (Now and then a tall base or a wide one.)
    if (i == 12 && P(0.5)) {
        if (P(0.5))
            f->m_bound.bbox.max.y = f->m_bound.bbox.min.y + F(0.5f, 2.0f);
        else
            f->m_bound.bbox.max.x = f->m_bound.bbox.min.x + F(0.5f, 2.0f);
    }
}
void StartScenario()
{
    g_seed = U32();
    for (int i = 0; i < FRAMES; ++i) {
        RandomFrame(i);
        RandomPlace(i);
    }
    // (Now and then a slider larger than its base: its range reversed.)
    if (P(0.2)) {
        const I3D_bbox &base = FrameAt(12)->m_bound.bbox;
        I3D_bbox &own = FrameAt(10)->m_bound.bbox;
        const float grow = F(1.2f, 3.0f);
        own.min = S_vector(base.min.x * grow - 0.1f, base.min.y * grow - 0.1f, base.min.z);
        own.max = S_vector(base.max.x * grow + 0.1f, base.max.y * grow + 0.1f, base.max.z);
    }
    g_missing = P(0.85) ? 0u : U32() & (P(0.5) ? 0xFFFu : 1u << I(0, CHILD_COUNT - 1));
}
void StartSide()
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    g_log.clear();
    std::srand(g_seed);
}

using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, void *, int);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_IntBool = bool(__attribute__((thiscall)) *)(void *, int);
using T_Drag = bool(__attribute__((thiscall)) *)(void *, const float *, const float *);
using T_Float = void(__attribute__((thiscall)) *)(void *, float);
using T_FloatBool = bool(__attribute__((thiscall)) *)(void *, float);
using T_Show3 = void(__attribute__((thiscall)) *)(void *, bool, bool, bool);
using T_Show2 = void(__attribute__((thiscall)) *)(void *, bool, bool);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Fast = bool(__attribute__((thiscall)) *)(void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);

// The parts never set up (Init without them) read their null looks; a slider without its range divides by its null
// rect: both crash, left out.
bool ButtonReady(const uint8_t *b, int part)
{
    const S_Layout &l = L();
    const uint8_t *p = b + (part == 1 ? l.up : l.down);
    return At<void *>(p, l.pNormal) && At<void *>(p, l.pActive) && At<void *>(p, l.pInactive);
}
bool SliderReady(const uint8_t *b)
{
    const S_Layout &l = L();
    const uint8_t *p = b + l.slider;
    return At<void *>(p, l.pNormal) && At<void *>(p, l.pActive) && At<void *>(p, l.sRect) && At<void *>(p, l.sBase);
}
bool Ready(const uint8_t *b) { return ButtonReady(b, 1) && ButtonReady(b, 2) && SliderReady(b); }
const char *Skip(const uint8_t *b, const S_Op &op)
{
    const S_Layout &l = L();
    const bool inited = At<void *>(b, l.frame) != nullptr;
    switch (op.kind) {
    case OP_INIT:
        // (A part or a look missing: Init's SetDisabled shows the null looks; both crash.)
        return g_missing ? "Init with a part missing" : nullptr;
    case OP_DTOR:
    case OP_B_INIT:
    case OP_S_INIT:
        return nullptr;
    case OP_B_SHOW:
    case OP_B_SETDISABLED:
    case OP_B_ACTIVATE:
    case OP_B_DEACTIVATE:
    case OP_B_PRESS:
        return !ButtonReady(b, op.a) ? "an arrow without its looks" : nullptr;
    case OP_S_LAYOUT:
    case OP_S_SHOW:
    case OP_S_SETDISABLED:
    case OP_S_ACTIVATE:
    case OP_S_DEACTIVATE:
    case OP_S_SETPOSITION:
    case OP_S_FOLLOW:
    case OP_S_SETAT:
    case OP_S_DRAG:
        return !SliderReady(b) ? "a slider without its looks or range" : nullptr;
    default:
        return !inited || !Ready(b) ? "a bar without its parts" : nullptr;
    }
}
void Run(int side, C_InventoryScrollBar *port, uint8_t *orig, const S_Op &op)
{
    uint8_t *self = side ? reinterpret_cast<uint8_t *>(port) : orig;
    if (const char *why = Skip(self, op)) {
        Log("skipped: %s", why);
        return;
    }
    const S_Layout &l = L();
    void *button = self + (op.a == 1 ? l.up : l.down);
    void *slider = self + l.slider;
    auto *pb = reinterpret_cast<T_Button *>(button);
    auto *ps = reinterpret_cast<T_Slider *>(slider);
    const float cursor[4] = {op.f, op.g, 0.0f, 1.0f}, move[2] = {op.h, op.k};
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<C_InventoryScreen *>(g_portOwner), FrameAt(F_BAR), op.a)
                               : OriginalAt<T_Init>(0x004ae2e0)(orig, g_origOwner, FrameAt(F_BAR), op.a));
        break;
    case OP_SETCOUNT:
        side ? port->SetCount(op.a) : OriginalAt<T_Int>(0x004ae4d0)(orig, op.a);
        break;
    case OP_HOVER:
        Log("Hover -> %d", side ? port->Hover(op.a) : OriginalAt<T_IntBool>(0x004ae6a0)(orig, op.a));
        break;
    case OP_UNHOVER:
        Log("Unhover -> %d", side ? port->Unhover(op.a) : OriginalAt<T_IntBool>(0x004ae640)(orig, op.a));
        break;
    case OP_PRESS:
        Log("Press -> %d", side ? port->Press(op.a) : OriginalAt<T_IntBool>(0x004ae6f0)(orig, op.a));
        break;
    case OP_DRAG:
        Log("Drag -> %d", side ? port->Drag(*reinterpret_cast<const S_vector4 *>(cursor), move)
                               : OriginalAt<T_Drag>(0x004ae730)(orig, cursor, move));
        break;
    case OP_RELEASE:
        Log("Release -> %d", side ? port->Release(op.a) : OriginalAt<T_IntBool>(0x004ae760)(orig, op.a));
        break;
    case OP_SETDISABLED:
        side ? port->SetDisabled(op.a != 0) : OriginalAt<T_Bool>(0x004ae790)(orig, op.a != 0);
        break;
    case OP_STEP:
        side ? port->Step(op.b) : OriginalAt<T_Int>(0x004ae3b0)(orig, op.b);
        break;
    case OP_SLIDERMOVED:
        side ? port->OnSliderMoved(op.f) : OriginalAt<T_Float>(0x004ae470)(orig, op.f);
        break;
    case OP_B_SHOW:
        side ? pb->Show(op.b & 1, op.b & 2, op.b & 4) : OriginalAt<T_Show3>(0x004ada20)(button, op.b & 1, op.b & 2, op.b & 4);
        break;
    case OP_B_SETDISABLED:
        side ? pb->SetDisabled(op.b & 1) : OriginalAt<T_Bool>(0x004adba0)(button, op.b & 1);
        break;
    case OP_B_ACTIVATE:
        Log("Activate -> %d", side ? pb->Activate() : OriginalAt<T_Fast>(0x004adbc0)(button));
        break;
    case OP_B_DEACTIVATE:
        Log("Deactivate -> %d", side ? pb->Deactivate() : OriginalAt<T_Fast>(0x004adbe0)(button));
        break;
    case OP_B_PRESS:
        Log("Press -> %d", side ? pb->Press() : OriginalAt<T_Fast>(0x004adc10)(button));
        break;
    case OP_S_LAYOUT:
        side ? ps->Layout() : OriginalAt<T_Void>(0x004addd0)(slider);
        break;
    case OP_S_SHOW:
        side ? ps->Show(op.b & 1, op.b & 2) : OriginalAt<T_Show2>(0x004adc50)(slider, op.b & 1, op.b & 2);
        break;
    case OP_S_SETDISABLED:
        side ? ps->SetDisabled(op.b & 1) : OriginalAt<T_Bool>(0x004ae290)(slider, op.b & 1);
        break;
    case OP_S_ACTIVATE:
        Log("Activate -> %d", side ? ps->Activate() : OriginalAt<T_Fast>(0x004ae230)(slider));
        break;
    case OP_S_DEACTIVATE:
        Log("Deactivate -> %d", side ? ps->Deactivate() : OriginalAt<T_Fast>(0x004ae250)(slider));
        break;
    case OP_S_SETPOSITION:
        side ? ps->SetPosition(op.f) : OriginalAt<T_Float>(0x004ae0f0)(slider, op.f);
        break;
    case OP_S_FOLLOW:
        Log("Follow -> %d", side ? ps->Follow(cursor, move) : OriginalAt<T_Drag>(0x004adf40)(slider, cursor, move));
        break;
    case OP_S_SETAT:
        Log("SetPositionAt -> %d", side ? ps->SetPositionAt(op.f) : OriginalAt<T_FloatBool>(0x004ae0b0)(slider, op.f));
        break;
    case OP_S_DRAG:
        Log("Drag -> %d", side ? ps->Drag(cursor, move) : OriginalAt<T_Drag>(0x004ae260)(slider, cursor, move));
        break;
    case OP_B_INIT:
        Log("Init -> %d", side ? pb->Init(port, FrameAt(op.a), op.b) : OriginalAt<T_Init>(0x004ada70)(button, orig, FrameAt(op.a), op.b));
        break;
    case OP_S_INIT:
        Log("Init -> %d", side ? ps->Init(port, FrameAt(3), op.b) : OriginalAt<T_Init>(0x004adc90)(slider, orig, FrameAt(3), op.b));
        break;
    case OP_DTOR:
        // (The destructor frees the range; the slider's rect then cleared for the snap.)
        if (side) {
            port->~C_InventoryScrollBar();
        } else {
            OriginalAt<T_Void>(0x004a0350)(orig);
        }
        Put<void *>(slider, l.sRect, nullptr);
        Log("destroyed");
        break;
    }
}
void ApplyWorld(const S_Op &op)
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(op.a));
    if (op.kind == W_FRAME)
        RandomPlace(op.b);
    else
        FrameAt(op.b)->m_frameFlags ^= uint32_t(Pick({FRMFLAGS_BOUND_VALID, FRMFLAGS_WMATRIX_VALID, FRMFLAGS_POS_LOCKED}));
    g_rng = saved;
}
void RunScenario(const std::vector<S_Op> &ops)
{
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        StartSide();
        uint8_t *orig = static_cast<uint8_t *>(std::calloc(1, 0x94));
        C_InventoryScrollBar *port = static_cast<C_InventoryScrollBar *>(std::calloc(1, sizeof(C_InventoryScrollBar)));
        // The constructors (the original's sets its vtables and defaults).
        if (side)
            new (port) C_InventoryScrollBar();
        else
            OriginalAt<T_Ctor>(0x004a0270)(orig);
        uint8_t *self = side ? reinterpret_cast<uint8_t *>(port) : orig;
        g_bars[0] = orig;
        g_bars[1] = port;
        g_log.push_back(Snap(self));
        bool destroyed = false;
        for (const S_Op &op : ops) {
            Log("-- %d %d %d %s %s %s %s", op.kind, op.a, op.b, FloatText(op.f).c_str(), FloatText(op.g).c_str(), FloatText(op.h).c_str(),
                FloatText(op.k).c_str());
            if (op.kind >= 100)
                ApplyWorld(op);
            else
                Run(side, port, orig, op);
            destroyed = destroyed || op.kind == OP_DTOR;
            // (After the destructor: the original's clears its parts' pointers; not compared.)
            g_log.push_back(destroyed ? std::string("bar destroyed") : Snap(self));
        }
        if (!destroyed) {
            // (The ranges freed without a log: the original's Init leaked earlier ones.)
            if (side)
                port->~C_InventoryScrollBar();
        }
        std::free(orig);
        std::free(port);
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

const int BAR_IDS[] = {0xF10, 0xF20, 0xF30, 0xF40};
int g_barId;
int AnyId()
{
    // (Mostly the bar's parts: 1 up, 2 down, 3 the slider; else others.)
    return P(0.8) ? g_barId | I(0, 4) : P(0.5) ? I(0, 0x20) : int(U32());
}
float Position() { return P(0.3) ? Pick({0.0f, 50.0f, 100.0f, -5.0f, 105.0f, 33.3333f}) : F(-20, 120); }
void FillOp(S_Op &op)
{
    switch (op.kind) {
    case OP_INIT:
        op.a = P(0.85) ? g_barId : Pick({0x0, 0xF50, int(U32())});
        break;
    case OP_SETCOUNT:
        op.a = P(0.7) ? I(0, 12) : Pick({-1, 1, 2, 100, 1000000, INT_MIN + 1});
        break;
    case OP_HOVER:
    case OP_UNHOVER:
    case OP_PRESS:
    case OP_RELEASE:
        op.a = AnyId();
        break;
    case OP_SETDISABLED:
        op.a = P(0.5);
        break;
    case OP_STEP:
        op.b = AnyId();
        break;
    case OP_SLIDERMOVED:
    case OP_S_SETPOSITION:
        op.f = P(0.05) ? Pick({INFINITY, -INFINITY, NAN, 1e30f}) : Position();
        break;
    case OP_S_SETAT:
        op.f = Coord();
        break;
    case OP_DRAG:
    case OP_S_FOLLOW:
    case OP_S_DRAG:
        op.f = Coord();
        op.g = Coord();
        op.h = P(0.4) ? 0.0f : F(-0.1f, 0.1f);
        op.k = P(0.4) ? 0.0f : F(-0.1f, 0.1f);
        break;
    case OP_B_SHOW:
    case OP_B_SETDISABLED:
    case OP_B_ACTIVATE:
    case OP_B_DEACTIVATE:
    case OP_B_PRESS:
    case OP_S_SHOW:
    case OP_S_SETDISABLED:
        op.a = I(1, 2);
        op.b = I(0, 7);
        break;
    case OP_B_INIT:
        op.a = I(1, 2);
        op.b = g_barId | op.a;
        break;
    case OP_S_INIT:
        op.b = g_barId | 3;
        break;
    case W_FRAME:
    case W_FLAGS:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, 12);
        break;
    }
}

// ---- the bar ----
void TestBar()
{
    StartScenario();
    g_barId = Pick(BAR_IDS);
    std::vector<S_Op> ops;
    S_Op init{OP_INIT};
    FillOp(init);
    if (P(0.97))
        ops.push_back(init);
    // (With a part missing: the parts' own Inits.)
    if (g_missing)
        for (int n = I(1, 3); n > 0; --n) {
            S_Op part{Pick({OP_B_INIT, OP_S_INIT})};
            FillOp(part);
            ops.push_back(part);
        }
    // (Mostly a count, the cursor over a part, a click or a drag.)
    if (P(0.7)) {
        S_Op count{OP_SETCOUNT};
        FillOp(count);
        ops.push_back(count);
        for (int n = I(1, 4); n > 0; --n) {
            S_Op hover{OP_HOVER};
            hover.a = g_barId | I(1, 3);
            ops.push_back(hover);
            S_Op act{Pick({OP_PRESS, OP_PRESS, OP_DRAG, OP_DRAG, OP_RELEASE})};
            FillOp(act);
            ops.push_back(act);
        }
    }
    static const int KINDS[] = {OP_INIT, OP_SETCOUNT, OP_SETCOUNT, OP_HOVER, OP_HOVER, OP_UNHOVER, OP_PRESS, OP_PRESS, OP_DRAG, OP_DRAG,
                                OP_RELEASE, OP_SETDISABLED, OP_STEP, OP_SLIDERMOVED, OP_SLIDERMOVED, OP_B_SHOW, OP_B_SETDISABLED,
                                OP_B_ACTIVATE, OP_B_DEACTIVATE, OP_B_PRESS, OP_S_LAYOUT, OP_S_SHOW, OP_S_SETDISABLED, OP_S_ACTIVATE,
                                OP_S_DEACTIVATE, OP_S_SETPOSITION, OP_S_FOLLOW, OP_S_SETAT, OP_S_DRAG, W_FRAME, W_FRAME, W_FLAGS};
    for (int n = I(0, 16); n > 0; --n) {
        S_Op op{Pick(KINDS)};
        FillOp(op);
        ops.push_back(op);
    }
    if (P(0.3))
        ops.push_back(S_Op{OP_DTOR});
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    RunScenario(ops);
}

// ---- the port's fix, alone: a second Init's range replaces the first's (the original's leaked) ----
void TestFixes()
{
    StartScenario();
    g_missing = 0;
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    g_side = 1;
    StartSide();
    C_InventoryScrollBar bar;
    CHECK(bar.Init(reinterpret_cast<C_InventoryScreen *>(g_portOwner), FrameAt(F_BAR), 0xF10), "Init: false");
    const float *first = bar.m_slider.rect;
    CHECK(first != nullptr, "Init: no range");
    CHECK(bar.Init(reinterpret_cast<C_InventoryScreen *>(g_portOwner), FrameAt(F_BAR), 0xF20) && bar.m_slider.rect != nullptr,
          "second Init: no range");
}

void SetUp()
{
    g_partName = PartName;
    g_findChild = FindChild;
    g_updateBound = UpdateBound;
    RedirectOriginal(0x004a2590, reinterpret_cast<const void *>(O_OnScroll));
    RedirectOriginal(0x007e67c8, reinterpret_cast<const void *>(O_Floor));
    void (C_InventoryScreen::*onScroll)(int, int) = &C_InventoryScreen::OnScroll;
    void *at;
    std::memcpy(&at, &onScroll, sizeof at);
    PatchPort(at, reinterpret_cast<const void *>(O_OnScroll));
}

}  // namespace H

// ---- the screen's references (not run in this part: aborting stubs) ----
[[noreturn]] void NotHere(const char *what)
{
    std::printf("scrollbar_diff: %s reached\n", what);
    std::exit(3);
}
S_RankName g_rankNames[8];
C_str C_GameTexts::Get(int) const { NotHere("C_GameTexts::Get"); }
const char *C_Human::GetFullName() const { NotHere("GetFullName"); }
int C_Human::GetHealthPercent() const { NotHere("GetHealthPercent"); }
int C_Human::GetProfileUnknown24() const { NotHere("GetProfileUnknown24"); }
int C_Human::GetStat(int) const { NotHere("GetStat"); }
float C_Human::GetStatFactor(int) const { NotHere("GetStatFactor"); }
const S_vector &C_Human::GetViewDir() const { NotHere("GetViewDir"); }
bool C_Human::IsUnconscious() const { NotHere("IsUnconscious"); }
bool C_Human::RequestMoveItem(C_Item *, C_Actor *, int, int, int) { NotHere("RequestMoveItem"); }
void C_Human::RequestThrowItem(C_Item *, int, const S_vector &) { NotHere("RequestThrowItem"); }
bool C_Human::RequestTransferItem(C_Item *, int, C_Human *, C_Item *, int) { NotHere("RequestTransferItem"); }
IImage *C_ItemDef::GetImage() { NotHere("GetImage"); }
int C_MapCursor::GetHoveredId() { NotHere("GetHoveredId"); }
void C_MapCursor::GetMove(float *) const { NotHere("GetMove"); }
bool C_MapCursor::IsInRect(const float *) const { NotHere("IsInRect"); }
I3D_material *DuplicateMaterial(I3D_frame *, const char *) { NotHere("DuplicateMaterial"); }
I3D_texture *FindTexture(I3D_frame *, const char *) { NotHere("FindTexture"); }

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("scrollbar_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_SCROLLBAR_QUICK") ? 15000 : 40000;
    if (const char *r = std::getenv("HD2_SCROLLBAR_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_SCROLLBAR_CASE");
    const struct {
        const char *name;
        void (*run)();
        int divisor;
    } cases[] = {{"ScrollBar", TestBar, 1}, {"ScrollBarFixes", TestFixes, 100}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < std::max(1, rounds / c.divisor); ++g_round)
            c.run();
    }
    std::printf("scrollbar_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
