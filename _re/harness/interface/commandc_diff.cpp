// Human part 74c: the command map's mouse and input (game/interface_command.cpp lines 1339-1887: C_CommandPanel's
// IsOnMap, CursorRay, PickTarget, OnLeftPress, OnLeftDrag, OnLeftRelease, OnRightPress, OnRightRelease, ShowRollMenu,
// HandleInput, UpdateCursorMode, MoveCamera) against the original's (0x004bf230, 0x004ba070, 0x004ba390, 0x004ba660,
// 0x004bad50, 0x004bad80, 0x004bb210, 0x004bb0e0, 0x004b79c0, 0x004b7e50, 0x004b81a0). MinGW; build_commandc.bat; runs
// commandc_diff.exe ("commandc_diff: N checks, 0 failures"). Built like part 74b's commandb_diff.cpp (the same panel,
// fakes and world; parts 74a and 74b run for real: tested there). Added: the collision world's RayTest and
// CollisionRayTest (the world's hits: a volume of the world's or the ground, the point and the normal; the volumes'
// frames carry the orders), the game scene's UnmapScreenPoint, the cursor's GetScreenPos, SetMode (its mode written in
// place), ClampMotion and clip rectangle, the info selector's SelectOrder, the team's SetCommanded, the roll menu's
// HandleInput, IsCursorOver, Select, MoveTo, Prepare, Open (its open flag written), SetTarget (its choice, target and
// actions in place), the controls' ClearKey, the graph's GetAsciiKey and KeyToChar, the profile's GetDetailSettings
// (the inverted mouse), the camera's SetTacticalMotion, the visuals' UpdateWBoundProc; the humans' faded flag and
// position, a static weapon's state, an animation actor's no-info flag, the orders' soldiers.
// HD2_COMMANDC_ROUNDS: scenarios (default 20,000; HD2_COMMANDC_QUICK: 6,000); HD2_COMMANDC_CASE: one case only;
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

#define INTERFACE_FRAMES 128
#define protected public
#define private public
#include "game/interface_command.cpp"
#include "game/camera_module.h"
#include "game/geffects.h"
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
template <class M> int Slot(M m)   // a port virtual's byte offset in its vtable (g++: the member pointer holds 1 + it)
{
    struct {
        uintptr_t p;
        ptrdiff_t adjust;
    } r;
    static_assert(sizeof r == sizeof m, "member pointer");
    std::memcpy(&r, &m, sizeof r);
    return int(r.p - 1);
}
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
template <class M> void *Address(M m)
{
    void *at;
    std::memcpy(&at, &m, sizeof at);
    return at;
}

// ---- the frames: the scene's by name (a hash: the same name the same frame; "Border" missing now and then), f0 the
// cursor's frame, f1 the click sound, f2 the marker model the driver creates ----
constexpr int F_CURSOR = 0, F_CLICK = 1, F_MODEL = 2, F_CAMERA = 3, F_POOL = 4, POOL = 120;
uint32_t g_frameSeed;
bool g_noBorder;
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
I3D_frame *FindFrame(const char *name)
{
    if (g_noBorder && std::strcmp(name, "Border") == 0)
        return nullptr;
    return FrameAt(F_POOL + int(Fnv(name, g_frameSeed) % POOL));
}
// A frame's children (the markers' parts): a pool frame by the parent and the name.
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    const uint32_t index = uint32_t((reinterpret_cast<uint8_t *>(f) - reinterpret_cast<uint8_t *>(FrameAt(0))) /
                                    (reinterpret_cast<uint8_t *>(FrameAt(1)) - reinterpret_cast<uint8_t *>(FrameAt(0))));
    return FrameAt(F_POOL + int(Fnv(name, g_frameSeed ^ (index * 2654435761u)) % POOL));
}

// ---- the world ----
constexpr int HUMANS = 8, ORDERS = 4, ACTORS = 10;
constexpr int PATH_POINTS = 5;
struct S_OrderW {
    int kind, actor;   // actor: a human or -1
    int arg, pathCount;
    float path[PATH_POINTS][4];
    float data[4], pos[4];
    uint32_t flags;
    int marker;        // a frame
};
struct S_Hum {
    int type, health, role, frame, orderCount;
    uint32_t stateFlags, modeFlags, stanceFlags;
    bool suspended, unconscious, changed;
    int vehicle, seatCount, occupant[3], stamp;   // vehicle: a human (as a vehicle) or -1; stamp: its frame's render stamp
    float boxScale;   // (the box's corners about its centre: small ones are made the minimum size)
    bool fadedOut, noInfo;   // noInfo: as an animation actor
    int weaponState;         // as a static weapon
    float pos[4];
    S_OrderW order[ORDERS];
};
struct S_World {
    S_Hum hum[HUMANS];
    int members[6], memberCount, active, commanded, commandedIndex, seen[6], seenCount, actors[ACTORS], actorCount;
    int selectedHuman, selectedOrder;   // the info selector's chosen order (-1: none)
    int hovered, cursorMode;
    float cursor[4];
    bool rollOpen, paused, openResult;
    int state;
    int selectedIndex, sceneStamp;
    float shiftX, shiftY;   // the screen points moved (boxes off the screen: the box's start values)
    // Part 74c: the collision volumes (flags, type, actor, the frame whose data is an order), the ray's hit (a volume
    // or -1), point and normal; the drag ray's; the cursor's screen point; the roll menu's choice, target, actions,
    // cursor, Select's answer, whether Open opens it; the keys; the inverted mouse.
    struct S_Vol {
        uint32_t flags, type;
        int actor, frame, human, order;
    } vol[4];
    int rayHit;
    float rayPos[3], rayNormal[3];
    bool dragHit;
    float dragPos[3], dragNormal[3];
    int screenX, screenY;
    int rollSelected, rollTarget, rollActions[10];
    bool rollOver, rollSelect, rollOpens;
    int asciiKey;
    uint32_t keyChar;
    bool invertMouse;
};
S_World g_w, g_wStart;

// ---- each side's objects ----
alignas(16) uint8_t g_origHum[HUMANS][0x300], g_portHum[HUMANS][0x300];
alignas(16) uint8_t g_origOrder[HUMANS][ORDERS][0x140], g_portOrder[HUMANS][ORDERS][0x140];
alignas(16) uint8_t g_origGame[0x100], g_portGame[0x100], g_origCamera[0x40], g_portCamera[0x40], g_effects[0x40];
alignas(16) uint8_t g_origGfx[0x40], g_portGfx[0x40], g_cursorBlock[0x80], g_ui[0x40], g_selector[0x40], g_gameScene[0x400],
    g_checkpoints[0x10];
void *g_origCameraVt[0x40], *g_origGfxVt[0x40], *g_portGfxVt[0x60], *g_origActorVt[0x40], *g_portActorVt[0x80];
alignas(16) uint8_t g_origNew[16][0x140], g_portNew[16][0x140];   // AllocHumanOrder's (in turn)
int g_newCount;
alignas(16) float g_paths[2][HUMANS][ORDERS][PATH_POINTS][4];
alignas(16) uint8_t g_volumes[2][4][0x60], g_options[2][0x200];
void *g_origActors[ACTORS], *g_portActors[ACTORS];
int g_side;

void *Hum(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portHum[i]) : static_cast<void *>(g_origHum[i]); }
void *Order(int h, int k) { return h < 0 || k < 0 ? nullptr : g_side ? static_cast<void *>(g_portOrder[h][k]) : static_cast<void *>(g_origOrder[h][k]); }
int HumIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_origHum[i] || p == g_portHum[i])
            return i;
    return -1;
}
int BodyIndex(const void *body)   // the bodies: the original's at +0x98
{
    for (int i = 0; i < HUMANS; ++i)
        if (body == g_origHum[i] + 0x98 || body == g_portHum[i] + offsetof(C_Human, m_body))
            return i;
    return -1;
}
const void *OrigTeam() { return reinterpret_cast<const void *>(0x0089ae78 + 0x1C); }
const void *PortTeam() { return &g_teamManager.m_playerTeam; }
const void *g_panels[2];
std::string PartName(const void *p)
{
    char text[32];
    int i;
    if ((i = HumIndex(p)) >= 0) {
        std::snprintf(text, sizeof text, "h%d", i);
        return text;
    }
    for (int h = 0; h < HUMANS; ++h)
        for (int k = 0; k < ORDERS; ++k)
            if (p == g_origOrder[h][k] || p == g_portOrder[h][k]) {
                std::snprintf(text, sizeof text, "order%d.%d", h, k);
                return text;
            }
    for (int k = 0; k < 16; ++k)
        if (p == g_origNew[k] || p == g_portNew[k]) {
            std::snprintf(text, sizeof text, "new%d", k);
            return text;
        }
    if (p == g_ui)
        return "ui";
    if (p == g_selector)
        return "selector";
    if (p == g_cursorBlock)
        return "cursor";
    if (p == g_gameScene)
        return "gscene";
    if (p == g_checkpoints)
        return "checkpoints";
    if (p == g_effects)
        return "effects";
    if (p == OrigTeam() || p == PortTeam())
        return "team";
    for (int s = 0; s < 2; ++s)
        if (g_panels[s] && p >= g_panels[s] && p < static_cast<const uint8_t *>(g_panels[s]) + 0x5A7C0) {
            std::snprintf(text, sizeof text, "panel+%x", unsigned(static_cast<const uint8_t *>(p) - static_cast<const uint8_t *>(g_panels[s])));
            return text;
        }
    return GfxName(p);
}

// ---- the team, the humans, the orders ----
void *Active()
{
    Log("team GetActivePlayer -> h%d", g_w.active);
    return Hum(g_w.active);
}
void *Commanded()
{
    Log("team GetCommandedMember -> h%d", g_w.commanded);
    return Hum(g_w.commanded);
}
int MemberCount()
{
    Log("team GetMemberCount");
    return g_w.memberCount;
}
void *Member(int i)
{
    Log("team GetMember %d", i);
    return i >= 0 && i < g_w.memberCount ? Hum(g_w.members[i]) : nullptr;
}
bool IsMember(const void *h)
{
    Log("team IsMember %s", Name(h).c_str());
    for (int k = 0; k < g_w.memberCount; ++k)
        if (g_w.members[k] == HumIndex(h))
            return true;
    return false;
}
void SeenLog() { Log("team GetSeenEnemies -> %d", g_w.seenCount); }
void RefreshSight() { Log("team RefreshSight"); }
const S_Hum &HW(const void *h) { return g_w.hum[HumIndex(h)]; }
int OrderCount(const void *h)
{
    Log("%s GetOrderCount", Name(h).c_str());
    return HW(h).orderCount;
}
void *GetOrder(const void *h, int i)
{
    Log("%s GetOrder %d", Name(h).c_str(), i);
    return i >= 0 && i < HW(h).orderCount ? Order(HumIndex(h), i) : nullptr;
}
#define HUMAN_QUERY(NAME, TYPE, FIELD) \
    TYPE NAME(const void *h)           \
    {                                  \
        Log("%s " #NAME, Name(h).c_str()); \
        return HW(h).FIELD;            \
    }
HUMAN_QUERY(GetStateFlags, uint32_t, stateFlags)
HUMAN_QUERY(GetAIModeFlags, uint32_t, modeFlags)
HUMAN_QUERY(GetStanceFlags, uint32_t, stanceFlags)
HUMAN_QUERY(IsAISuspended, bool, suspended)
HUMAN_QUERY(IsUnconscious, bool, unconscious)
#undef HUMAN_QUERY
void Fade(const void *h, const char *what, int ms) { Log("%s %s %d", Name(h).c_str(), what, ms); }
void CancelOrders(const void *h) { Log("%s CancelOrders", Name(h).c_str()); }
void BodyPose(const void *body, int arg) { Log("h%d body Unknown5ac470 %d", BodyIndex(body), arg); }
void *__attribute__((thiscall)) O_Active(void *) { return Active(); }
void *__attribute__((thiscall)) O_Commanded(void *) { return Commanded(); }
int __attribute__((thiscall)) O_MemberCount(void *) { return MemberCount(); }
void *__attribute__((thiscall)) O_Member(void *, int i) { return Member(i); }
bool __attribute__((thiscall)) O_IsMember(void *, void *h) { return IsMember(h); }
void __attribute__((thiscall)) O_Seen(void *, void **vector)
{
    SeenLog();
    if (g_w.seenCount) {
        void **v = static_cast<void **>(std::malloc(sizeof(void *) * size_t(g_w.seenCount)));
        for (int k = 0; k < g_w.seenCount; ++k)
            v[k] = Hum(g_w.seen[k]);
        vector[1] = v;
        vector[2] = vector[3] = v + g_w.seenCount;
    }
}
void __attribute__((thiscall)) O_RefreshSight(void *) { RefreshSight(); }
int __attribute__((thiscall)) O_OrderCount(void *h) { return OrderCount(h); }
void *__attribute__((thiscall)) O_GetOrder(void *h, int i) { return GetOrder(h, i); }
uint32_t __attribute__((thiscall)) O_StateFlags(void *h) { return GetStateFlags(h); }
uint32_t __attribute__((thiscall)) O_ModeFlags(void *h) { return GetAIModeFlags(h); }
uint32_t __attribute__((thiscall)) O_StanceFlags(void *h) { return GetStanceFlags(h); }
bool __attribute__((thiscall)) O_Suspended(void *h) { return IsAISuspended(h); }
bool __attribute__((thiscall)) O_Unconscious(void *h) { return IsUnconscious(h); }
void __attribute__((thiscall)) O_FadeOut(void *h, int ms) { Fade(h, "FadeOut", ms); }
void __attribute__((thiscall)) O_FadeIn(void *h, int ms) { Fade(h, "FadeIn", ms); }
void __attribute__((thiscall)) O_CancelOrders(void *h) { CancelOrders(h); }
void __attribute__((thiscall)) O_BodyPose(void *body, int arg) { BodyPose(body, arg); }

// ---- the camera's human effects, the effects module, the profile, the game, the model cache, the selector, the
// cursor, the interface ----
std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }
std::string V4(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]) + " " + FloatText(v[3]); }
void *__attribute__((thiscall)) O_HumanEffects(void *) { return g_effects; }
void *__cdecl O_GetCameraModule() { return g_origCamera; }
void __attribute__((thiscall)) O_SetTactical(void *, bool on) { Log("effects SetTactical %d", on); }
void __attribute__((thiscall)) O_SetTacticalMode(void *, int mode, const float *point)
{
    Log("effects SetTacticalMode %d %s", mode, point ? V4(point).c_str() : "null");
}
void __attribute__((thiscall)) O_UpdateTactical(void *, int dt) { Log("effects UpdateTactical %d", dt); }
void __attribute__((thiscall)) O_SetTacticalZoom(void *, float zoom) { Log("effects SetTacticalZoom %s", FloatText(zoom).c_str()); }
void *__cdecl O_GetGfx() { return g_origGfx; }
void __attribute__((thiscall)) V_Thunderstorm(void *, bool on, void *light, bool immediate)
{
    Log("geffects SetThunderstorm %d %s %d", on, light ? "light" : "null", immediate);
}
void __attribute__((thiscall)) V_GfxUpdate(void *, int dt) { Log("geffects Update %d", dt); }
void __attribute__((thiscall)) V_GfxOther(void *) { Log("geffects other slot"); }
void __attribute__((thiscall)) O_ControlMode(void *, int mode) { Log("profile SetControlMode %d", mode); }
void SetPaused(void *game, bool paused)
{
    Log("game SetPaused %d", paused);
    Put<bool>(game, g_side ? int(offsetof(C_game, m_paused)) : 0x64, paused);
}
void __attribute__((thiscall)) O_SetPaused(void *game, bool paused) { SetPaused(game, paused); }
void *__cdecl O_GetGame() { return g_origGame; }
HRESULT OpenInto(const void *object, const char *name, uint32_t flags)
{
    Log("models OpenInto %s %s %x -> %d", Name(object).c_str(), name, flags, g_w.openResult ? 0 : 1);
    return g_w.openResult ? I3D_OK : HRESULT(0x80004005);
}
HRESULT __attribute__((thiscall)) O_OpenInto(void *, void *object, const char *name, uint32_t flags, void *, void *, int)
{
    return OpenInto(object, name, flags);
}
void *SelectedOrder()
{
    Log("selector GetSelectedOrder -> %d.%d", g_w.selectedHuman, g_w.selectedOrder);
    return Order(g_w.selectedHuman, g_w.selectedOrder);
}
void SelectIndex(int i) { Log("selector SelectOrderIndex %d", i); }
void *__attribute__((fastcall)) O_SelectedOrder(void *) { return SelectedOrder(); }
void __attribute__((thiscall)) O_SelectIndex(void *, int i) { SelectIndex(i); }
int Hovered()
{
    Log("cursor GetHoveredId -> %d", g_w.hovered);
    return g_w.hovered;
}
int __attribute__((thiscall)) O_Hovered(void *) { return Hovered(); }
void Highlight(const void *ui, const void *h, int what) { Log("%s HighlightMembers %s %d", Name(ui).c_str(), Name(h).c_str(), what); }
void __attribute__((thiscall)) O_Highlight(void *ui, void *h, int what) { Highlight(ui, h, what); }

// ---- the roll menu's (faked: part 72's) ----
void RollInit(const void *ui, const void *scene, bool map) { Log("roll Init %s %s %d", Name(ui).c_str(), Name(scene).c_str(), map); }
void RollLog(const char *what) { Log("roll %s", what); }
bool __attribute__((thiscall)) O_RollInit(void *, void *ui, void *scene, bool map)
{
    RollInit(ui, scene, map);
    return true;
}
void __attribute__((thiscall)) O_RollDone(void *) { RollLog("Done"); }
void __attribute__((thiscall)) O_RollSelectAt(void *, const float *pos) { Log("roll SelectAt %s %s", FloatText(pos[0]).c_str(), FloatText(pos[1]).c_str()); }
void __attribute__((thiscall)) O_RollUpdate(void *, int dt) { Log("roll Update %d", dt); }


// ---- part 74b's: the order requests, the paths, the selected index, the actors' boxes and seats, the graph, the
// game scene's transform, the cursor's SetFromWorld, the roll menu's Render ----
bool g_actorOrder;   // (AddActorOrder's: the original's zero vectors have 3 components, their w stack words: not compared)
void *AllocOrder(const void *h, int kind, int arg, const float *pos, const float *data, uint32_t pace, uint32_t stance, uint32_t mode,
                 const void *actor, int unknown70)
{
    void *order = g_side ? static_cast<void *>(g_portNew[g_newCount % 16]) : static_cast<void *>(g_origNew[g_newCount % 16]);
    ++g_newCount;
    // (A follow order's data.w is a stack word too: PressOrderButton's, part 74a.)
    const bool actor3 = g_actorOrder || kind == 3;   // (74c: HandleInput's AddActorOrder too)
    const float p[4] = {pos[0], pos[1], pos[2], actor3 ? 0.0f : pos[3]},
                d[4] = {data[0], data[1], data[2], actor3 || kind == 4 ? 0.0f : data[3]};
    Log("AllocHumanOrder %s %d %d %s %s %x %x %x %s %d -> %s", Name(h).c_str(), kind, arg, V4(p).c_str(), V4(d).c_str(), pace,
        stance, mode, Name(actor).c_str(), unknown70, Name(order).c_str());
    return order;
}
void OrderRequest(const void *h, const char *what, int index, const void *order)
{
    Log("%s %s %d %s", Name(h).c_str(), what, index, Name(order).c_str());
}
bool OrdersChanged(const void *h)
{
    Log("%s HaveOrdersChanged", Name(h).c_str());
    return HW(h).changed;
}
void InitPlain(const void *order, const void *h, const float *pos, const float *data, uint32_t pace, uint32_t stance, uint32_t mode)
{
    // (The change is a stack object: not named. The original's zero vectors have 3 components: their w are stack words,
    // not compared.)
    (void)order;
    const float p3[4] = {pos[0], pos[1], pos[2], 0.0f}, d3[4] = {data[0], data[1], data[2], 0.0f};
    Log("change InitPlain %s %s %s %x %x %x", Name(h).c_str(), V4(p3).c_str(), V4(d3).c_str(), pace, stance, mode);
}
void UpdatePath(const void *order, const float *from) { Log("%s UpdatePath %s", Name(order).c_str(), V4(from).c_str()); }
int SelectedIndex()
{
    Log("selector GetSelectedIndex -> %d", g_w.selectedIndex);
    return g_w.selectedIndex;
}
void BoxCorners(const void *actor, float *corners)
{
    Log("%s GetBoxCorners", Name(actor).c_str());
    const int i = HumIndex(actor);
    for (int k = 0; k < 8; ++k) {
        corners[4 * k + 0] = float(i) + (k & 1 ? 0.5f : -0.5f) * HW(actor).boxScale;
        corners[4 * k + 1] = float(k & 2 ? 1 : 0) * HW(actor).boxScale + 0.25f * float(i);
        corners[4 * k + 2] = float(k & 4 ? 1 : -1) * (0.3f + 0.1f * float(i)) * HW(actor).boxScale;
        corners[4 * k + 3] = 1.0f;
    }
}
int SeatCount(const void *v)
{
    Log("%s GetSeatCount", Name(v).c_str());
    return HW(v).seatCount;
}
void *SeatOccupant(const void *v, int seat)
{
    Log("%s GetSeatOccupant %d", Name(v).c_str(), seat);
    return seat >= 0 && seat < 3 ? Hum(HW(v).occupant[seat]) : nullptr;
}
void Transform(const float *points, float *out, int count)
{
    Log("gscene TransformPoints %d", count);
    for (int k = 0; k < count; ++k) {
        out[4 * k + 0] = 512.0f + 140.0f * points[4 * k] - 30.0f * points[4 * k + 2] + g_w.shiftX;
        out[4 * k + 1] = 384.0f - 120.0f * points[4 * k + 1] + 20.0f * points[4 * k + 2] + g_w.shiftY;
        out[4 * k + 2] = 0.5f;
        out[4 * k + 3] = 1.0f;
    }
}
void GraphTexture(const void *texture, int stage) { Log("graph SetTexture %s %d", Name(texture).c_str(), stage); }
void GraphWorld(const float *m)
{
    std::string r;
    for (int k = 0; k < 16; ++k)
        r += " " + FloatText(m[k]);
    Log("graph SetWorldMatrix%s", r.c_str());
}
void GraphDraw(int kind, int count, const void *vertices, int transformed)
{
    if (kind == 1) {
        // (Lines on the screen: S_TLVertex, 2 per line, a log line each (Log's buffer is 600 bytes); their specular is not
        // set.)
        Log("graph DrawPrimitives %d %d %d", kind, count, transformed);
        const uint8_t *v = static_cast<const uint8_t *>(vertices);
        for (int k = 0; k < 2 * count; ++k, v += 0x20)
            Log("  vertex %s,%s,%s,%s,%u,%s,%s", FloatText(At<float>(v, 0)).c_str(), FloatText(At<float>(v, 4)).c_str(),
                FloatText(At<float>(v, 8)).c_str(), FloatText(At<float>(v, 0xC)).c_str(), At<uint32_t>(v, 0x10),
                FloatText(At<float>(v, 0x18)).c_str(), FloatText(At<float>(v, 0x1C)).c_str());
        return;
    }
    // (A line strip in the world: count + 1 vertices of 0x18 bytes, hashed.)
    uint32_t h = 2166136261u;
    const uint8_t *v = static_cast<const uint8_t *>(vertices);
    for (int k = 0; k < (count + 1) * 0x18; ++k)
        h = (h ^ v[k]) * 16777619u;
    Log("graph DrawPrimitives %d %d %d hash %08x", kind, count, transformed, h);
}
void SetFromWorld(const float *p) { Log("cursor SetFromWorld %s", V4(p).c_str()); }
void *__stdcall O_AllocOrder(void *h, int kind, int arg, const float *pos, const float *data, uint32_t pace, uint32_t stance,
                             uint32_t mode, void *actor, int unknown70)
{
    return AllocOrder(h, kind, arg, pos, data, pace, stance, mode, actor, unknown70);
}
void __attribute__((thiscall)) O_AddOrderReq(void *h, void *order) { OrderRequest(h, "AddOrder", -1, order); }
void __attribute__((thiscall)) O_InsertOrder(void *h, void *order, int index) { OrderRequest(h, "InsertOrder", index, order); }
void __attribute__((thiscall)) O_RemoveOrder(void *h, int index) { OrderRequest(h, "RemoveOrder", index, nullptr); }
void __attribute__((thiscall)) O_SetOrder(void *h, int index, void *) { OrderRequest(h, "SetOrder", index, nullptr); }
bool __attribute__((thiscall)) O_OrdersChanged(void *h) { return OrdersChanged(h); }
void __attribute__((thiscall)) O_InitPlain(void *order, void *h, const float *pos, const float *data, uint32_t pace, uint32_t stance,
                                           uint32_t mode)
{
    InitPlain(order, h, pos, data, pace, stance, mode);
}
void __attribute__((thiscall)) O_OrderDtor(void *) {}
void __attribute__((thiscall)) O_UpdatePath(void *order, const float *from) { UpdatePath(order, from); }
int __attribute__((fastcall)) O_SelectedIndex(void *) { return SelectedIndex(); }
void __attribute__((thiscall)) V_BoxCorners(void *actor, float *corners) { BoxCorners(actor, corners); }
int __attribute__((thiscall)) V_SeatCount(void *v) { return SeatCount(v); }
void *__attribute__((thiscall)) V_SeatOccupant(void *v, int seat) { return SeatOccupant(v, seat); }
void __attribute__((thiscall)) V_ActorOther(void *a) { Log("%s other slot", Name(a).c_str()); }
void __stdcall SC_Transform(void *, const float *points, float *out, int count) { Transform(points, out, count); }
void __stdcall G_Texture(void *, void *texture, int stage) { GraphTexture(texture, stage); }
void __stdcall G_World(void *, const float *m) { GraphWorld(m); }
void __stdcall G_Draw(void *, int kind, int count, const void *vertices, int transformed) { GraphDraw(kind, count, vertices, transformed); }
void __attribute__((thiscall)) O_SetFromWorld(void *, const float *p) { SetFromWorld(p); }
void __attribute__((thiscall)) O_RollRender(void *) { RollLog("Render"); }


// ---- part 74c's: the rays, the scene's UnmapScreenPoint, the cursor, the selector, the team, the roll menu, the
// controls, the graph's keys, the profile, the camera's motion, the visuals' bounds ----
void *Ray(const char *what, const float *from, const float *dir, float *pos, float *normal, uint32_t types, uint32_t flags)
{
    const bool drag = what[0] == 'C';
    const int hit = drag ? (g_w.dragHit ? 0 : -1) : g_w.rayHit;
    Log("%s %s %s %s %x %x -> %d", what, F3(from).c_str(), F3(dir).c_str(), normal ? "normal" : "null", types, flags, hit);
    if (hit < 0)
        return nullptr;
    if (pos)
        std::memcpy(pos, drag ? g_w.dragPos : g_w.rayPos, 12);
    if (normal)
        std::memcpy(normal, drag ? g_w.dragNormal : g_w.rayNormal, 12);
    return g_volumes[g_side][hit];
}
void Unmap(int x, int y, float *origin, float *dir)
{
    Log("gscene UnmapScreenPoint %d %d", x, y);
    origin[0] = float(x) * 0.01f;
    origin[1] = 40.0f;
    origin[2] = float(y) * 0.02f;
    origin[3] = 1.0f;
    dir[0] = 0.1f + float(x % 7) * 0.01f;
    dir[1] = -1.0f;
    dir[2] = 0.2f - float(y % 5) * 0.03f;
    dir[3] = 0.0f;
}
void ScreenPos(int *x, int *y)
{
    Log("cursor GetScreenPos -> %d %d", g_w.screenX, g_w.screenY);
    *x = g_w.screenX;
    *y = g_w.screenY;
}
void SetMode(int mode)
{
    Log("cursor SetMode %d", mode);
    Put<int>(g_cursorBlock, 0xC, mode);
    g_w.cursorMode = mode;   // (kept: SyncWorld writes the world's before each operation)
}
void Clamp(const float *held, const float *pressed)
{
    Log("cursor ClampMotion %s %s", FloatText(held[INPUT_CURSOR_X]).c_str(), FloatText(held[INPUT_CURSOR_Y]).c_str());
}
void SelectOrderLog(const void *order) { Log("selector SelectOrder %s", Name(order).c_str()); }
bool SetCommanded(const void *h)
{
    Log("team SetCommanded %s", Name(h).c_str());
    return true;
}
void RollInput(const float *held, const float *pressed) { Log("roll HandleInput"); }
bool RollOver()
{
    Log("roll IsCursorOver -> %d", g_w.rollOver);
    return g_w.rollOver;
}
bool RollSelect(int i)
{
    Log("roll Select %d -> %d", i, g_w.rollSelect);
    return g_w.rollSelect;
}
void RollMove(const float *p) { Log("roll MoveTo %s", F3(p).c_str()); }
void RollOpen(void *menu, bool open)
{
    Log("roll Open %d", open);
    Put<bool>(menu, g_side ? int(offsetof(C_RollMenu, m_open)) : 0x30, open && g_w.rollOpens);
}
void RollTarget(const void *target, float distance) { Log("roll SetTarget %s %s", Name(target).c_str(), FloatText(distance).c_str()); }
void ClearKeyLog(int key) { Log("controls ClearKey %d", key); }
int AsciiKey()
{
    Log("graph GetAsciiKey -> %d", g_w.asciiKey);
    return g_w.asciiKey;
}
uint32_t KeyChar(int key, int unknown)
{
    Log("graph KeyToChar %d %d -> %u", key, unknown, g_w.keyChar);
    return g_w.keyChar;
}
void *DetailSettings()
{
    Log("profile GetDetailSettings");
    Put<bool>(g_options[g_side], g_side ? int(offsetof(C_PlayerOptions, invertMouse)) : 0x39, g_w.invertMouse);
    return g_options[g_side];
}
void Motion(float x, float y) { Log("effects SetTacticalMotion %s %s", FloatText(x).c_str(), FloatText(y).c_str()); }
void WBound(void *visual)
{
    Log("%s UpdateWBoundProc", Name(visual).c_str());
    Put<uint32_t>(visual, 0x1D0, At<uint32_t>(visual, 0x1D0) | 0x40000u);
}
void *__attribute__((thiscall)) O_RayTest(void *, const float *from, const float *dir, float *pos, float *normal, uint32_t types,
                                           uint32_t flags)
{
    return Ray("RayTest", from, dir, pos, normal, types, flags);
}
void *__stdcall O_CollisionRayTest(const float *from, const float *dir, float *pos, float *normal, uint32_t types, uint32_t flags)
{
    return Ray("CollisionRayTest", from, dir, pos, normal, types, flags);
}
void __stdcall SC_Unmap(void *, int x, int y, float *origin, float *dir) { Unmap(x, y, origin, dir); }
void __attribute__((thiscall)) O_ScreenPos(void *, int *x, int *y) { ScreenPos(x, y); }
void __attribute__((thiscall)) O_SetMode(void *, int mode) { SetMode(mode); }
void __attribute__((thiscall)) O_Clamp(void *, float *held, float *pressed) { Clamp(held, pressed); }
void __attribute__((thiscall)) O_SelectOrder(void *, void *order) { SelectOrderLog(order); }
bool __attribute__((thiscall)) O_SetCommanded(void *, void *h) { return SetCommanded(h); }
void __attribute__((thiscall)) O_RollInput(void *, float *held, float *pressed) { RollInput(held, pressed); }
bool __attribute__((fastcall)) O_RollOver(void *) { return RollOver(); }
bool __attribute__((thiscall)) O_RollSelect(void *, int i) { return RollSelect(i); }
void __attribute__((thiscall)) O_RollMove(void *, const float *p) { RollMove(p); }
void __attribute__((thiscall)) O_RollPrepare(void *) { RollLog("Prepare"); }
void __attribute__((thiscall)) O_RollOpen(void *menu, bool open) { RollOpen(menu, open); }
void __attribute__((thiscall)) O_RollTarget(void *, void *target, float distance) { RollTarget(target, distance); }
void __attribute__((thiscall)) O_ClearKey(void *, int key, float *, float *) { ClearKeyLog(key); }
int __stdcall G_AsciiKey(void *) { return AsciiKey(); }
uint32_t __stdcall G_KeyChar(void *, int key, int unknown) { return KeyChar(key, unknown); }
void *__attribute__((thiscall)) O_DetailSettings(void *) { return DetailSettings(); }
void __attribute__((thiscall)) O_Motion(void *, float x, float y) { Motion(x, y); }
void __stdcall O_WBound(void *visual) { WBound(visual); }

}  // namespace H

// ---- the port's side of the same fakes ----
I3D_sound *g_clickSound;
C_Human *C_Team::GetActivePlayer() const { return static_cast<C_Human *>(H::Active()); }
C_Human *C_Team::GetCommandedMember() const { return static_cast<C_Human *>(H::Commanded()); }
int C_TeamBase::GetMemberCount() const { return H::MemberCount(); }
C_Human *C_TeamBase::GetMember(int i) const { return static_cast<C_Human *>(H::Member(i)); }
bool C_TeamBase::IsMember(const C_Human *h) const { return H::IsMember(h); }
void C_Team::GetSeenEnemies(std::vector<C_Human *> &enemies) const
{
    H::SeenLog();
    for (int k = 0; k < H::g_w.seenCount; ++k)
        enemies.push_back(static_cast<C_Human *>(H::Hum(H::g_w.seen[k])));
}
void C_Team::RefreshSight() { H::RefreshSight(); }
int C_Human::GetOrderCount() const { return H::OrderCount(this); }
S_HumanOrder *C_Human::GetOrder(int i) const { return static_cast<S_HumanOrder *>(H::GetOrder(this, i)); }
uint32_t C_Human::GetStateFlags() const { return H::GetStateFlags(this); }
uint32_t C_Human::GetAIModeFlags() const { return H::GetAIModeFlags(this); }
uint32_t C_Human::GetStanceFlags() const { return H::GetStanceFlags(this); }
bool C_Human::IsAISuspended() const { return H::IsAISuspended(this); }
bool C_Human::IsUnconscious() const { return H::IsUnconscious(this); }
void C_Human::FadeOut(int ms) { H::Fade(this, "FadeOut", ms); }
void C_Human::FadeIn(int ms) { H::Fade(this, "FadeIn", ms); }
void C_Human::CancelOrders() { H::CancelOrders(this); }
void S_HumanBody::Unknown5ac470(int arg) { H::BodyPose(this, arg); }
C_CameraModule *GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_portCamera); }
C_CameraHumanEffects *C_CameraModule::GetHumanEffects() { return reinterpret_cast<C_CameraHumanEffects *>(H::g_effects); }
void C_CameraHumanEffects::SetTactical(bool on) { H::O_SetTactical(this, on); }
void C_CameraHumanEffects::SetTacticalMode(int mode, const S_vector4 *point) { H::O_SetTacticalMode(this, mode, point ? &point->x : nullptr); }
void C_CameraHumanEffects::UpdateTactical(int dt) { H::O_UpdateTactical(this, dt); }
void C_CameraHumanEffects::SetTacticalZoom(float zoom) { H::O_SetTacticalZoom(this, zoom); }
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(H::g_portGfx); }
void C_GameProfile::SetControlMode(int mode) { H::O_ControlMode(this, mode); }
void C_game::SetPaused(bool paused) { H::SetPaused(this, paused); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
template <class T>
HRESULT C_ResourceCache<T>::OpenInto(T *object, const char *fileName, uint32_t flags, void *, void *, int)
{
    return H::OpenInto(object, fileName, flags);
}
S_HumanOrder *C_InfoSelector::GetSelectedOrder() const { return static_cast<S_HumanOrder *>(H::SelectedOrder()); }
void C_InfoSelector::SelectOrderIndex(int i) { H::SelectIndex(i); }
int C_MapCursor::GetHoveredId() { return H::Hovered(); }
void C_Interface::HighlightMembers(C_Human *h, int what) { H::Highlight(this, h, what); }
bool C_RollMenu::Init(C_Interface *ui, I3D_scene *scene, bool map)
{
    H::RollInit(ui, scene, map);
    return true;
}
void C_RollMenu::Done() { H::RollLog("Done"); }
void C_RollMenu::SelectAt(const float *pos) { H::O_RollSelectAt(this, pos); }
void C_RollMenu::Update(int dt) { H::O_RollUpdate(this, dt); }
void C_RollMenu::Render() { H::RollLog("Render"); }
S_HumanOrder *AllocHumanOrder(C_Human *h, int kind, int arg, const S_vector4 &pos, const S_vector4 &data, uint32_t pace, uint32_t stance,
                              uint32_t mode, C_Actor *actor, int unknown70)
{
    return static_cast<S_HumanOrder *>(H::AllocOrder(h, kind, arg, &pos.x, &data.x, pace, stance, mode, actor, unknown70));
}
void C_Human::AddOrder(S_HumanOrder *order) { H::OrderRequest(this, "AddOrder", -1, order); }
void C_Human::InsertOrder(S_HumanOrder *order, int index) { H::OrderRequest(this, "InsertOrder", index, order); }
void C_Human::RemoveOrder(int index) { H::OrderRequest(this, "RemoveOrder", index, nullptr); }
void C_Human::SetOrder(int index, S_HumanOrder *) { H::OrderRequest(this, "SetOrder", index, nullptr); }
bool C_Human::HaveOrdersChanged() const { return H::OrdersChanged(this); }
void S_HumanOrder::InitPlain(C_Human *h, const S_vector4 &pos, const S_vector4 &data, uint32_t pace, uint32_t stance, uint32_t mode)
{
    H::InitPlain(this, h, &pos.x, &data.x, pace, stance, mode);
}
void S_HumanOrder::UpdatePath(const S_vector4 &from) { H::UpdatePath(this, &from.x); }
S_HumanOrder::~S_HumanOrder() {}
int C_InfoSelector::GetSelectedIndex() const { return H::SelectedIndex(); }
void C_MapCursor::SetFromWorld(const S_vector4 &p) { H::SetFromWorld(&p.x); }

namespace H {

// ---- building each side's objects from the world ----
void SyncWorld(int side)
{
    g_side = side;
    for (int h = 0; h < HUMANS; ++h) {
        const S_Hum &w = g_w.hum[h];
        uint8_t *b = side ? g_portHum[h] : g_origHum[h];
        std::memset(b, 0, 0x300);
        if (!side) {
            Put<void *>(b, 0, g_origActorVt);
            Put<int>(b, 0x18, w.type);
            Put<void *>(b, 0x20, FrameAt(w.frame));
            Put<void *>(b, 0x160, Hum(w.vehicle));
            Put<int>(b, 0xEC, w.health);
            Put<int>(b, 0x19C, w.role);
            Put<bool>(b, 0x150, w.fadedOut);
            std::memcpy(b + 0x30, w.pos, 16);
            Put<int>(b, 0x2F0, w.weaponState);
            Put<bool>(b, 0xA4, w.noInfo);
        } else {
            C_Human *p = reinterpret_cast<C_Human *>(b);
            p->m_type = E_ActorType(w.type);
            Put<void *>(b, 0, g_portActorVt);
            p->m_frame = FrameAt(w.frame);
            p->m_vehicle = static_cast<C_Actor *>(Hum(w.vehicle));
            p->m_stats[STAT_HEALTH] = w.health;
            p->m_role = E_HumanRole(w.role);
            p->m_fadedOut = w.fadedOut;
            std::memcpy(&p->m_position, w.pos, 16);
            Put<int>(b, offsetof(C_StaticWeapon, m_state), w.weaponState);
            Put<bool>(b, offsetof(C_AnimationActor, m_showNoInfo), w.noInfo);
        }
        for (int k = 0; k < ORDERS; ++k) {
            const S_OrderW &o = w.order[k];
            uint8_t *ob = side ? g_portOrder[h][k] : g_origOrder[h][k];
            std::memset(ob, 0, 0x140);
            const int kind = side ? int(offsetof(S_HumanOrder, kind)) : 0x0C, data = side ? int(offsetof(S_HumanOrder, data)) : 0x20,
                      pos = side ? int(offsetof(S_HumanOrder, pos)) : 0x30, actor = side ? int(offsetof(S_HumanOrder, actor)) : 0x40,
                      flags = side ? int(offsetof(S_HumanOrder, flags)) : 0x44, marker = side ? int(offsetof(S_HumanOrder, marker)) : 0x84,
                      arg = side ? int(offsetof(S_HumanOrder, arg)) : 0x10, path = side ? int(offsetof(S_HumanOrder, path)) : 0x74;
            Put<int>(ob, arg, o.arg);
            Put<void *>(ob, side ? int(offsetof(S_HumanOrder, human)) : 8, Hum(h));
            float(*points)[4] = g_paths[side][h][k];
            std::memcpy(points, o.path, sizeof o.path);
            Put<void *>(ob, path, o.pathCount ? points : nullptr);
            Put<int>(ob, path + 4, PATH_POINTS);
            Put<int>(ob, path + 8, o.pathCount);
            Put<int>(ob, kind, o.kind);
            std::memcpy(ob + data, o.data, 16);
            std::memcpy(ob + pos, o.pos, 16);
            Put<void *>(ob, actor, Hum(o.actor));
            Put<uint32_t>(ob, flags, o.flags);
            Put<void *>(ob, marker, FrameAt(o.marker));
        }
    }
    void **actors = side ? g_portActors : g_origActors;
    for (int k = 0; k < g_w.actorCount; ++k)
        actors[k] = Hum(g_w.actors[k]);
    if (!side) {
        void **v = OriginalAt<void **>(0x0089b4c0);
        v[0] = g_w.actorCount ? actors : nullptr;
        v[1] = v[2] = g_w.actorCount ? actors + g_w.actorCount : nullptr;
        uint8_t *team = OriginalAt<uint8_t *>(0x0089ae78 + 0x1C);
        Put<int>(team, 0x30, g_w.commandedIndex);
        Put<void *>(g_origGame, 0x18, g_gameScene);
        Put<void *>(g_origGame, 0x54, g_checkpoints);
    } else {
        void **v = reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&g_actorsModule) + offsetof(C_ActorsModule, m_actors));
        v[0] = g_w.actorCount ? actors : nullptr;
        v[1] = v[2] = g_w.actorCount ? actors + g_w.actorCount : nullptr;
        Put<int>(&g_teamManager.m_playerTeam, offsetof(C_Team, m_commanded), g_w.commandedIndex);
        Put<void *>(g_portGame, offsetof(C_game, m_scene), g_gameScene);
        Put<void *>(g_portGame, offsetof(C_game, m_checkpoints), g_checkpoints);
    }
    // The frames' and the game scene's render stamps (+0x11C), the game scene's camera (+0x15C).
    for (int h = 0; h < HUMANS; ++h)
        Put<int>(FrameAt(g_w.hum[h].frame), 0x11C, g_w.hum[h].stamp);
    Put<void *>(g_gameScene, 0, g_sceneVt);
    Put<int>(g_gameScene, 0x11C, g_w.sceneStamp);
    Put<void *>(g_gameScene, 0x15C, FrameAt(F_CAMERA));
    // The collision volumes (the same layout); their frames' data the orders.
    for (int k = 0; k < 4; ++k) {
        const auto &v = g_w.vol[k];
        auto *volume = reinterpret_cast<S_CollisionVolume *>(g_volumes[side][k]);
        volume->flags = v.flags;
        volume->type = v.type;
        volume->frame = FrameAt(v.frame);
        volume->actor = static_cast<C_Actor *>(Hum(v.actor));
        FrameAt(v.frame)->SetData(static_cast<S_FrameData *>(Order(v.human, v.order)));
    }
    // The cursor (the same layout: mode +0x0C, position +0x20, frame +0).
    Put<void *>(g_cursorBlock, 0, FrameAt(F_CURSOR));
    Put<int>(g_cursorBlock, 0xC, g_w.cursorMode);
    std::memcpy(g_cursorBlock + 0x20, g_w.cursor, 16);
}
// The game's paused flag and state, the team's tactical flag: each side's, set at the start and read back (both
// sides write them).
void StartGame()
{
    Put<bool>(g_origGame, 0x64, g_w.paused);
    Put<int>(g_origGame, 4, g_w.state);
    Put<bool>(g_portGame, offsetof(C_game, m_paused), g_w.paused);
    Put<int>(g_portGame, offsetof(C_game, m_state), g_w.state);
    Put<bool>(OriginalAt<uint8_t *>(0x0089ae78 + 0x1C), 0x54, false);
    Put<bool>(&g_teamManager.m_playerTeam, offsetof(C_Team, m_enemyRecords) + offsetof(C_EnemyRecords, m_tacticalView), false);
}
std::string GameSnap()
{
    if (!g_side)
        return "game paused " + std::to_string(At<uint8_t>(g_origGame, 0x64)) + " state " + std::to_string(At<int>(g_origGame, 4)) +
               " tactical " + std::to_string(At<uint8_t>(OriginalAt<uint8_t *>(0x0089ae78 + 0x1C), 0x54));
    return "game paused " + std::to_string(At<uint8_t>(g_portGame, offsetof(C_game, m_paused))) + " state " +
           std::to_string(At<int>(g_portGame, offsetof(C_game, m_state))) + " tactical " +
           std::to_string(At<uint8_t>(&g_teamManager.m_playerTeam, offsetof(C_Team, m_enemyRecords) + offsetof(C_EnemyRecords, m_tacticalView)));
}

uint32_t Flags()
{
    // (Pace, AI mode and stance bits, now and then none or all.)
    const uint32_t paces[] = {0, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x1F000};
    const uint32_t modes[] = {0, 0x100, 0x200, 0x400, 0x700};
    const uint32_t stances[] = {0, 1, 2, 4, 7};
    return P(0.1) ? Pick({0u, 0xFFFFFFFFu}) : Pick(paces) | Pick(modes) | Pick(stances);
}
void RandomRays();
void RandomHuman(int i)
{
    S_Hum &h = g_w.hum[i];
    h = S_Hum{};
    h.type = P(0.65) ? int(ACTOR_HUMAN)
                     : Pick({int(ACTOR_CAR), 0, int(ACTOR_STATIC_WEAPON), int(ACTOR_SUBMARINE), int(ACTOR_ANIMATION)});
    h.health = P(0.8) ? I(1, 800) : Pick({0, -1});
    h.role = P(0.8) ? I(1, 5) : Pick({0, 6});
    h.frame = F_POOL + I(0, POOL - 1);
    h.orderCount = P(0.3) ? 0 : I(1, ORDERS);
    h.stateFlags = Flags();
    h.modeFlags = Flags();
    h.stanceFlags = Flags();
    h.suspended = P(0.2);
    h.unconscious = P(0.2);
    h.changed = P(0.4);
    h.vehicle = P(0.75) ? -1 : I(0, HUMANS - 1);
    h.seatCount = P(0.2) ? 0 : I(1, 3);
    for (int &o : h.occupant)
        o = P(0.4) ? -1 : I(0, HUMANS - 1);
    h.stamp = P(0.8) ? 7 : I(5, 9);
    h.boxScale = P(0.5) ? 1.0f : P(0.5) ? F(0.0f, 0.2f) : Pick({0.0f, 0.05f, 2.0f});
    h.fadedOut = P(0.2);
    h.noInfo = P(0.5);
    h.weaponState = P(0.5) ? 1 : I(0, 3);
    for (float &f : h.pos)
        f = F(-20, 20);
    for (S_OrderW &o : h.order) {
        o.kind = P(0.6) ? Pick({6, 7, 1, 2, 3, 0x20}) : I(0, 0x22);
        o.actor = P(0.6) ? -1 : I(0, HUMANS - 1);
        for (float &f : o.data)
            f = P(0.2) ? Pick({0.0f, 1.0f}) : F(-1, 1);
        for (float &f : o.pos)
            f = F(-100, 100);
        o.flags = Flags();
        o.marker = F_POOL + I(0, POOL - 1);
        o.arg = I(0, 3);
        o.pathCount = I(1, PATH_POINTS);   // (UpdatePath gives at least the start; an empty path: the fixes' case)
        for (auto &pt : o.path)
            for (float &f : pt)
                f = F(-50, 50);
    }
}
void RandomTeam()
{
    g_w.memberCount = P(0.1) ? I(0, 1) : I(2, 6);
    for (int k = 0; k < g_w.memberCount; ++k)
        g_w.members[k] = P(0.05) ? -1 : I(0, HUMANS - 1);   // (now and then nobody: Update skips him)
    g_w.active = P(0.9) ? I(0, HUMANS - 1) : -1;
    g_w.commanded = P(0.85) ? I(0, HUMANS - 1) : -1;
    g_w.commandedIndex = g_w.commanded >= 0 && P(0.9) ? I(0, 5) : P(0.5) ? -1 : I(0, 5);
    g_w.seenCount = P(0.4) ? 0 : I(1, 6);
    for (int k = 0; k < g_w.seenCount; ++k)
        g_w.seen[k] = I(0, HUMANS - 1);
    g_w.actorCount = I(0, ACTORS);
    for (int k = 0; k < g_w.actorCount; ++k)
        g_w.actors[k] = P(0.1) ? -1 : I(0, HUMANS - 1);
    g_w.selectedHuman = P(0.4) ? -1 : I(0, HUMANS - 1);
    g_w.selectedOrder = g_w.selectedHuman >= 0 ? I(0, ORDERS - 1) : -1;
    g_w.selectedIndex = P(0.3) ? -1 : I(0, ORDERS - 1);
    g_w.sceneStamp = 7;
    g_w.shiftX = P(0.8) ? 0.0f : Pick({-3000.0f, 3000.0f});
    g_w.shiftY = P(0.8) ? 0.0f : Pick({-3000.0f, 500.0f, 3000.0f});
    RandomRays();
}
const int IDS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 25, 29, 30, -1, 100};
void RandomCursor()
{
    g_w.hovered = P(0.5) ? 0 : Pick(IDS);
    g_w.cursorMode = P(0.6) ? I(0, 2) : I(0, 15);
    for (float &f : g_w.cursor)
        f = F(-1, 1);
    if (P(0.6)) {
        g_w.cursor[0] = F(0.0f, 1.0f);
        g_w.cursor[1] = F(-0.62f, 0.0f);
    }
    // (The edges exactly: the map's, the roll menu's place, the screen's edges.)
    if (P(0.2))
        g_w.cursor[0] = Pick({0.5f, 0.01f, 0.99f});
    if (P(0.2))
        g_w.cursor[1] = Pick({-0.2f, -0.6f, -0.01f, -0.74f, -0.61f});
    g_w.rollOpen = P(0.3);
}
void RandomWorld()
{
    for (int i = 0; i < HUMANS; ++i)
        RandomHuman(i);
    RandomTeam();
    RandomCursor();
    g_w.paused = P(0.5);
    g_w.state = I(0, 5);
    g_w.openResult = P(0.9);
}
void RandomFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}
int g_humanSeed;
void RandomOneHuman() { RandomHuman(g_humanSeed); }

// ---- the operations ----
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0, d = 0;
};
enum {
    OP_INIT, OP_DONE, OP_OPEN, OP_ISOPEN, OP_UPDATE, OP_SHOWHINT, OP_UPDATEHINT, OP_READFLAGS, OP_DARKEN, OP_LIGHTORDERS,
    OP_LIGHTFLAGS, OP_PRESS, OP_SETFLAG, OP_TOOL, OP_ORDER, OP_ZOOM, OP_LIGHT,
    OP_STATE,   // (the test's: the fields 74c's input sets: the buttons' states, the dragged marker, the flash, the zoom)
    OP_REMOVESEL, OP_REMOVEAT, OP_ADDORDER, OP_ADDACTOR, OP_APPLY, OP_ACQUIRE, OP_RELEASEMARKER, OP_UPDATEMARKER, OP_ADDPATH,
    OP_UPDATEMEMBER, OP_OVERLAY, OP_RENDER, OP_RENDERMARKERS, OP_BRACKETS, OP_DRAWBOX,
    OP_ISONMAP, OP_PICK, OP_LEFTPRESS, OP_LEFTDRAG, OP_LEFTRELEASE, OP_RIGHTPRESS, OP_RIGHTRELEASE, OP_SHOWROLL, OP_INPUT,
    OP_CURSORMODE, OP_MOVECAMERA,
    W_WORLD = 100, W_HUMAN, W_TEAM, W_CURSOR, W_ROLL, W_RAYS
};
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case W_WORLD:
        RandomFrom(op.a, &RandomWorld);
        break;
    case W_HUMAN:
        g_humanSeed = op.b;
        RandomFrom(op.a, &RandomOneHuman);
        break;
    case W_TEAM:
        RandomFrom(op.a, &RandomTeam);
        break;
    case W_CURSOR:
        RandomFrom(op.a, &RandomCursor);
        break;
    case W_ROLL:
        g_w.rollOpen = op.a != 0;
        break;
    case W_RAYS:
        RandomFrom(op.a, &RandomRays);
        break;
    }
}


// Part 74c's: the rays' answers, the cursor's screen point, the roll menu, the keys.
void RandomRays()
{
    for (auto &v : g_w.vol) {
        v.flags = (P(0.7) ? uint32_t(COLLISION_FLAG_DYNAMIC) : 0u) | (P(0.2) ? U32() & 0x0F00003Fu : 0u);
        v.type = (P(0.4) ? ORDER_VOLUME_TYPE : 0u) | (P(0.2) ? U32() & 0x7Fu : 0u);
        v.actor = P(0.85) ? I(0, HUMANS - 1) : -1;
        v.frame = F_POOL + I(0, POOL - 1);
        v.human = I(0, HUMANS - 1);
        v.order = I(0, ORDERS - 1);
    }
    g_w.rayHit = P(0.15) ? -1 : I(0, 3);
    for (int k = 0; k < 3; ++k) {
        g_w.rayPos[k] = F(-30, 30);
        g_w.dragPos[k] = F(-30, 30);
    }
    const float normals[][3] = {{0, 1, 0}, {0, 0.6f, 0.8f}, {0.8f, 0.6f, 0}, {0, -0.6f, 0.8f}, {1, 0, 0}, {0, -1, 0}, {0.6f, 0.8f, 0}};
    std::memcpy(g_w.rayNormal, normals[I(0, 6)], 12);
    std::memcpy(g_w.dragNormal, normals[I(0, 6)], 12);
    if (P(0.2))
        for (int k = 0; k < 3; ++k)
            g_w.rayNormal[k] = F(-1, 1);
    g_w.dragHit = P(0.8);
    // (Now and then the drag's point near the chosen order's place: the marker not moved.)
    if (g_w.selectedHuman >= 0 && g_w.selectedOrder >= 0 && P(0.3))
        for (int k = 0; k < 3; ++k)
            g_w.dragPos[k] = g_w.hum[g_w.selectedHuman].order[g_w.selectedOrder].pos[k] + F(-0.15f, 0.15f);
    g_w.screenX = I(-5, 1100);
    g_w.screenY = I(-5, 800);
    g_w.rollSelected = P(0.2) ? -1 : I(0, 9);
    g_w.rollTarget = I(0, HUMANS - 1);
    // (Half the time a vehicle's, when there is one: the pick-up on a seat's occupant.)
    if (P(0.5))
        for (int k = 0; k < HUMANS; ++k)
            if (g_w.hum[k].type != ACTOR_HUMAN) {
                g_w.rollTarget = k;
                break;
            }
    for (int &a : g_w.rollActions)
        a = P(0.4) ? ROLL_PICK_UP : I(0, 0x30);
    g_w.rollOver = P(0.5);
    g_w.rollSelect = P(0.5);
    g_w.rollOpens = P(0.7);
    g_w.asciiKey = P(0.5) ? 0 : I(1, 0x60);
    g_w.keyChar = P(0.6) ? uint32_t('0' + I(0, 9)) : P(0.5) ? 0u : uint32_t(I(0, 0x1FF));
    g_w.invertMouse = P(0.5);
}

// ---- each side's layout of the panel ----
struct S_Layout {
    int frame, scene, gameScene, ui, u10, commanded, active, target, selector, cursor, pace, mode, stance, time, wheel, realtime,
        stateIcons, actionIcons, stanceIcons, paceIcons, modeIcons, zoom, zoomFrames, hints, box, boxMargin, roll, rollOpen,
        buttons, flashTime, flashButton, markerModel, spareFirst, spareLast, lines, linesSize, routeCount, dragging, refresh,
        checkpoints, leftDown, u5A7BD;
};
const S_Layout ORIG = {0, 4, 8, 0xC, 0x10, 0x14, 0x18, 0x1C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x64, 0x68, 0x74,
                       0x84, 0x90, 0x94, 0xA8, 0x9C, 0x90, 0x590, 0x30, 0x620, 0x740, 0x744, 0x748, 0x750, 0x754, 0x75C, 0xF008,
                       0xF004, 0x5A7B0, 0x5A7B1, 0x5A7B4, 0x5A7B8, 0x5A7BD};
#define OF(f) int(offsetof(C_CommandPanel, f))
const S_Layout PORT = {OF(m_frame), OF(m_scene), OF(m_gameScene), OF(m_ui), OF(m_unknown10), OF(m_commanded), OF(m_active),
                       OF(m_target), OF(m_selector), OF(m_cursor), OF(m_pace), OF(m_mode), OF(m_stance), OF(m_time), OF(m_wheelTime),
                       OF(m_realtime), OF(m_stateIcons), OF(m_actionIcons), OF(m_stanceIcons), OF(m_paceIcons), OF(m_modeIcons),
                       OF(m_zoom), OF(m_zoomFrames), OF(m_hints), int(sizeof(C_TextBox)), int(offsetof(C_TextBox, m_margin)),
                       OF(m_rollMenu), int(offsetof(C_RollMenu, m_open)), OF(m_buttons), OF(m_flashTime), OF(m_flashButton),
                       OF(m_markerModel), OF(m_spareMarkers), OF(m_spareMarkers) + 4, OF(m_lines),
                       int(sizeof(C_CommandPanel::S_MemberLines)), int(offsetof(C_CommandPanel::S_MemberLines, routeSegments)),
                       OF(m_draggingMarker), OF(m_refreshPaths), OF(m_checkpoints), OF(m_leftDown), OF(m_unknown5A7BD)};
#undef OF
const S_Layout &L() { return g_side ? PORT : ORIG; }
std::string Ptr(const uint8_t *p, int at) { return Name(At<void *>(p, at)); }
std::string Snap(const uint8_t *s)
{
    const S_Layout &l = L();
    char text[400];
    std::snprintf(text, sizeof text,
                  "panel %s %s %s %s u10 %d commanded %s active %s target %s %s %s flags %x %x %x time %d wheel %d realtime %d icons %s %s",
                  Ptr(s, l.frame).c_str(), Ptr(s, l.scene).c_str(), Ptr(s, l.gameScene).c_str(), Ptr(s, l.ui).c_str(), At<int>(s, l.u10),
                  Ptr(s, l.commanded).c_str(), Ptr(s, l.active).c_str(), Ptr(s, l.target).c_str(), Ptr(s, l.selector).c_str(),
                  Ptr(s, l.cursor).c_str(), At<uint32_t>(s, l.pace), At<uint32_t>(s, l.mode), At<uint32_t>(s, l.stance), At<int>(s, l.time),
                  At<int>(s, l.wheel), At<uint8_t>(s, l.realtime), Ptr(s, l.stateIcons).c_str(), Ptr(s, l.actionIcons).c_str());
    std::string r = text;
    for (int k = 0; k < 3; ++k)
        r += " " + Ptr(s, l.stanceIcons + 4 * k);
    for (int k = 0; k < 4; ++k)
        r += " " + Ptr(s, l.paceIcons + 4 * k);
    for (int k = 0; k < 3; ++k)
        r += " " + Ptr(s, l.modeIcons + 4 * k);
    r += " zoom " + std::to_string(At<int>(s, l.zoom));
    for (int k = 0; k < 5; ++k)
        r += " " + Ptr(s, l.zoomFrames + 4 * k);
    r += " hints";
    for (int k = 0; k < 8; ++k) {
        const uint8_t *b = s + l.hints + l.box * k;
        r += " " + BoxName(b) + ":" + std::to_string(At<uint8_t>(b, l.boxMargin));
    }
    r += " roll " + std::to_string(At<uint8_t>(s + l.roll, l.rollOpen)) + " buttons";
    for (int k = 0; k < 18; ++k) {
        const uint8_t *b = s + l.buttons + 0x10 * k;
        r += " " + Ptr(b, 0) + ":" + std::to_string(At<int>(b, 4)) + ":" + Ptr(b, 0xC);
    }
    const void *flash = At<void *>(s, l.flashButton);
    r += " flash " + std::to_string(At<int>(s, l.flashTime)) + " " +
         (flash ? "button" + std::to_string((static_cast<const uint8_t *>(flash) - (s + l.buttons)) / 0x10) : std::string("null")) +
         " model " + Ptr(s, l.markerModel) + " spares";
    for (void *const *p = At<void *const *>(s, l.spareFirst); p && p != At<void *const *>(s, l.spareLast); ++p)
        r += " " + Name(*p);
    r += " lines";
    for (int k = 0; k < 6; ++k) {
        const uint8_t *m = s + l.lines + l.linesSize * k;
        const int segments = At<int>(m, 0xC000);
        uint32_t h = 2166136261u;
        for (int k = 0; k < (std::min(std::max(segments, 0), 2047) + 1) * 0x18; ++k)
            h = (h ^ m[k]) * 16777619u;
        char hash[16];
        std::snprintf(hash, sizeof hash, "%08x", h);
        r += " " + std::to_string(segments) + "/" + std::to_string(At<int>(m, l.routeCount)) + ":" + hash;
    }
    r += " drag " + std::to_string(At<uint8_t>(s, l.dragging)) + " refresh " + std::to_string(At<uint8_t>(s, l.refresh)) + " " +
         Ptr(s, l.checkpoints) + " buttons " + std::to_string(At<uint8_t>(s, l.leftDown)) + std::to_string(At<uint8_t>(s, l.leftDown + 1)) +
         std::to_string(At<uint8_t>(s, l.leftDown + 2)) + std::to_string(At<uint8_t>(s, l.leftDown + 3)) +
         std::to_string(At<uint8_t>(s, l.leftDown + 4)) + std::to_string(At<uint8_t>(s, l.u5A7BD)) + " " + GameSnap();
    {
        const bool port = g_side != 0;
        const int point = port ? int(offsetof(C_CommandPanel, m_cursorPoint)) : 0x20,
                  drag = port ? int(offsetof(C_CommandPanel, m_dragPoint)) : 0x5A790,
                  marker = port ? int(offsetof(C_CommandPanel, m_dragMarker)) : 0x5A7A0;
        r += " point " + F3(reinterpret_cast<const float *>(s + point)) + " grip " + F3(reinterpret_cast<const float *>(s + drag)) +
             " marker " + F3(reinterpret_cast<const float *>(s + marker)) + " cursor " + std::to_string(At<int>(g_cursorBlock, 0xC));
        const uint8_t *clip = At<const uint8_t *>(g_cursorBlock, 0x78);
        r += clip ? " clip " + Name(clip - 0x170) : " clip null";
    }
    return r;
}

// The input's arrays: a few of the map's keys and buttons, held or pressed, from a seed.
constexpr int INPUTS = 128;
uint32_t FloatBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
void InputArrays(uint32_t seed, float *held, float *pressed)
{
    std::fill(held, held + INPUTS, 0.0f);
    std::fill(pressed, pressed + INPUTS, 0.0f);
    static const int KEYS[] = {0, 1, 2, 3, 13, 15, 16, 17, 18, 32, 33, 34, 39, 40, 41, 42, 43, 44, 45, 52, 53, 54, 55, 55, 55,
                               56, 56, 57, 62, 63, INPUT_PAUSE, 4, 8, 12, 41, 41, 33, 34, 18, 32};
    static const float VALUES[] = {1.0f, 1.0f, 1.0f, -1.0f, 0.5f, -0.25f, 2.0f};
    std::mt19937 rng(seed);
    const int n = int(rng() % 7);
    for (int k = 0; k < n; ++k) {
        float *a = rng() % 2 ? held : pressed;
        a[KEYS[rng() % (sizeof KEYS / sizeof *KEYS)]] = VALUES[rng() % 7];
    }
}

// Both sides crash: the original's Init without "Border" (the port's fix, tested alone), the operations before Init
// (the null buttons), the order buttons without a commanded soldier (with a commanded index) and Open closing with a
// member's order without a marker (all have one here).
const char *Skip(const uint8_t *s, const S_Op &op)
{
    const S_Layout &l = L();
    const bool inited = At<void *>(s, l.buttons) != nullptr;
    if (op.kind == OP_INIT)
        return g_noBorder ? "Init without Border" : inited ? "Init twice" : nullptr;
    if (!inited)
        return "before Init";
    if ((op.kind == OP_ORDER || op.kind == OP_PRESS || op.kind == OP_LEFTPRESS || op.kind == OP_RIGHTPRESS || op.kind == OP_INPUT) &&
        g_w.commandedIndex >= 0 && g_w.commanded < 0)
        return "an order button without a soldier";
    if (op.kind == OP_OPEN && !op.a && std::count(g_w.members, g_w.members + g_w.memberCount, -1))
        return "closing with a null member";
    // (Part 74b: the original writes outside the panel for a member outside the six lines' buffers, and an empty path
    // leaves its count at -1: the port's fixes, tested alone.)
    if (op.kind == OP_ADDPATH && (op.a < 0 || op.a >= 6 || g_w.hum[op.b].order[op.c].pathCount == 0))
        return "a path outside the buffers or empty";
    if (op.kind == OP_UPDATEMEMBER && op.b >= 6)
        return "a member outside the buffers";
    // (Part 74c: a marker dragged without a commanded soldier: both use the null soldier.)
    if (g_w.commanded < 0 && (op.kind == OP_LEFTDRAG || op.kind == OP_INPUT)) {
        float held[INPUTS], pressed[INPUTS];
        InputArrays(uint32_t(op.c), held, pressed);
        if (op.kind == OP_LEFTDRAG || (!g_w.rollOpen && pressed[INPUT_CLICK] == 0.0f && held[INPUT_CLICK] != 0.0f))
            return "a marker dragged without a soldier";
    }
    return nullptr;
}

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, void *, void *, void *, void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_RetBool = bool(__attribute__((thiscall)) *)(void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_IntInt = void(__attribute__((thiscall)) *)(void *, int, int);
using T_Light = void(__attribute__((thiscall)) *)(void *, bool, bool);
using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
using T_AddOrder = void(__attribute__((thiscall)) *)(void *, int, const float *, const float *);
using T_AddActor = void(__attribute__((thiscall)) *)(void *, int, void *, int);
using T_Acquire = void *(__attribute__((thiscall)) *)(void *, void *);
using T_Ptr = void(__attribute__((thiscall)) *)(void *, void *);
using T_PtrPtr = void(__attribute__((thiscall)) *)(void *, void *, void *);
using T_PtrInt = void(__attribute__((thiscall)) *)(void *, void *, int);
using T_AddPath = void(__attribute__((thiscall)) *)(void *, int, const float *, const void *);
using T_Draw = void(__attribute__((thiscall)) *)(void *, void *, const float *);
using T_OnMap = bool(__attribute__((thiscall)) *)(void *, const float *);
using T_Pick = void(__attribute__((fastcall)) *)(void *, int);
using T_ShowRoll = void(__attribute__((thiscall)) *)(void *, bool, void *);
using T_Input = void(__attribute__((thiscall)) *)(void *, float *, float *);
// The colours the original passes (its own tables) and odd ones.
const float COLORS[6][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0.5f, 0.25f, 0.75f}};

uint8_t *g_orig, *g_origPristine, *g_portPristine;
constexpr size_t PANEL_GUARD = sizeof(C_CommandPanel::S_MemberLines);
void *g_spares[2][4];
C_CommandPanel *g_port;
void *g_icons[2][20];   // the interface's state icons (20: the markers read up to 19) and action icons
void Run(int side, const S_Op &op)
{
    uint8_t *self = side ? reinterpret_cast<uint8_t *>(g_port) : g_orig;
    C_CommandPanel *port = g_port;
    uint8_t *orig = g_orig;
    if (const char *why = Skip(self, op)) {
        Log("skipped: %s", why);
        return;
    }
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene), reinterpret_cast<C_Interface *>(g_ui),
                                            reinterpret_cast<I3D_texture **>(g_icons[0]), reinterpret_cast<I3D_texture **>(g_icons[1]),
                                            reinterpret_cast<C_InfoSelector *>(g_selector), reinterpret_cast<C_MapCursor *>(g_cursorBlock))
                               : OriginalAt<T_Init>(0x004b4980)(orig, &g_scene, g_ui, g_icons[0], g_icons[1], g_selector, g_cursorBlock));
        break;
    case OP_DONE:
        side ? port->Done() : OriginalAt<T_Void>(0x004b4d40)(orig);
        break;
    case OP_OPEN:
        side ? port->Open(op.a != 0) : OriginalAt<T_Bool>(0x004b5e40)(orig, op.a != 0);
        break;
    case OP_ISOPEN:
        Log("IsOpen %d", side ? port->IsOpen() : OriginalAt<T_RetBool>(0x004b6180)(orig));
        break;
    case OP_UPDATE:
        side ? port->Update(op.a) : OriginalAt<T_Int>(0x004b6420)(orig, op.a);
        break;
    case OP_SHOWHINT:
        side ? port->ShowHint(op.a, op.b) : OriginalAt<T_IntInt>(0x004b61a0)(orig, op.a, op.b);
        break;
    case OP_UPDATEHINT:
        side ? port->UpdateHint(op.a) : OriginalAt<T_Int>(0x004b6280)(orig, op.a);
        break;
    case OP_READFLAGS:
        side ? port->ReadOrderFlags() : OriginalAt<T_Void>(0x004b5a20)(orig);
        break;
    case OP_DARKEN:
        side ? port->DarkenButtons(op.a != 0) : OriginalAt<T_Bool>(0x004b5ad0)(orig, op.a != 0);
        break;
    case OP_LIGHTORDERS:
        side ? port->LightOrderButtons(op.a) : OriginalAt<T_Int>(0x004b5b90)(orig, op.a);
        break;
    case OP_LIGHTFLAGS:
        side ? port->LightFlagButtons() : OriginalAt<T_Void>(0x004b5c10)(orig);
        break;
    case OP_PRESS:
        side ? port->Press(op.a) : OriginalAt<T_Int>(0x004ba2d0)(orig, op.a);
        break;
    case OP_SETFLAG:
        side ? port->SetFlag(op.a, op.a - 1) : OriginalAt<T_IntInt>(0x004b8470)(orig, op.a, op.a - 1);
        break;
    case OP_TOOL:
        side ? port->PressToolButton(op.a, op.a - 1) : OriginalAt<T_IntInt>(0x004b86d0)(orig, op.a, op.a - 1);
        break;
    case OP_ORDER:
        side ? port->PressOrderButton(op.a, op.a - 1) : OriginalAt<T_IntInt>(0x004b87b0)(orig, op.a, op.a - 1);
        break;
    case OP_ZOOM:
        side ? port->CycleZoom() : OriginalAt<T_Void>(0x004bf130)(orig);
        break;
    case OP_REMOVESEL:
        side ? port->RemoveSelectedOrder() : OriginalAt<T_Void>(0x004b8e00)(orig);
        break;
    case OP_REMOVEAT:
        side ? port->RemoveOrderAt(op.a) : OriginalAt<T_Int>(0x004ba360)(orig, op.a);
        break;
    case OP_ADDORDER: {
        const S_vector4 pos = {float(op.b), 1.5f, -float(op.b) * 0.5f, 1.0f}, data = {0.25f, 0.0f, -0.75f, float(op.a & 1)};
        side ? port->AddOrder(op.a, pos, data) : OriginalAt<T_AddOrder>(0x004b8e30)(orig, op.a, &pos.x, &data.x);
        break;
    }
    case OP_ADDACTOR:
        g_actorOrder = true;
        side ? port->AddActorOrder(op.a, static_cast<C_Actor *>(Hum(op.b)), op.b + 1)
             : OriginalAt<T_AddActor>(0x004b8ff0)(orig, op.a, Hum(op.b), op.b + 1);
        g_actorOrder = false;
        break;
    case OP_APPLY:
        side ? port->ApplyFlags() : OriginalAt<T_Void>(0x004b8ee0)(orig);
        break;
    case OP_ACQUIRE: {
        void *order = Order(op.a, op.b);
        Log("AcquireOrderMarker -> %s", Name(side ? static_cast<void *>(port->AcquireOrderMarker(static_cast<S_HumanOrder *>(order)))
                                                  : OriginalAt<T_Acquire>(0x004b9240)(orig, order)).c_str());
        break;
    }
    case OP_RELEASEMARKER:
        side ? port->ReleaseOrderMarker(FrameAt(op.a)) : OriginalAt<T_Ptr>(0x004b9080)(orig, FrameAt(op.a));
        break;
    case OP_UPDATEMARKER: {
        void *order = Order(op.a, op.b);
        side ? port->UpdateOrderMarker(static_cast<S_HumanOrder *>(order), FrameAt(F_POOL + op.c))
             : OriginalAt<T_PtrPtr>(0x004b9b10)(orig, order, FrameAt(F_POOL + op.c));
        break;
    }
    case OP_ADDPATH: {
        const void *order = Order(op.b, op.c);
        const float *color = COLORS[op.d];
        const int path = side ? int(offsetof(S_HumanOrder, path)) : 0x74;
        side ? port->AddPath(op.a, color, *reinterpret_cast<const S_Vector4Array *>(static_cast<const uint8_t *>(order) + path))
             : OriginalAt<T_AddPath>(0x004b9d50)(orig, op.a, color, static_cast<const uint8_t *>(order) + path);
        break;
    }
    case OP_UPDATEMEMBER:
        side ? port->UpdateMember(static_cast<C_Human *>(Hum(op.a)), op.b) : OriginalAt<T_PtrInt>(0x004b9e80)(orig, Hum(op.a), op.b);
        break;
    case OP_OVERLAY:
        side ? port->RenderOverlay() : OriginalAt<T_Void>(0x004b7780)(orig);
        break;
    case OP_RENDER:
        side ? port->Render() : OriginalAt<T_Void>(0x004b6720)(orig);
        break;
    case OP_RENDERMARKERS:
        side ? port->RenderMarkers() : OriginalAt<T_Void>(0x004b7830)(orig);
        break;
    case OP_BRACKETS: {
        // (The original's three copies: grow 0, -0.005 and +0.005.)
        static const uint32_t COPIES[3] = {0x004bb260, 0x004bc300, 0x004bd3d0};
        static const float GROWS[3] = {0.0f, -BRACKETS_GROW, BRACKETS_GROW};
        side ? port->DrawBrackets(static_cast<C_Actor *>(Hum(op.a)), COLORS[op.d], GROWS[op.b])
             : OriginalAt<T_Draw>(COPIES[op.b])(orig, Hum(op.a), COLORS[op.d]);
        break;
    }
    case OP_DRAWBOX:
        side ? port->DrawBox(static_cast<C_Actor *>(Hum(op.a)), COLORS[op.d]) : OriginalAt<T_Draw>(0x004be670)(orig, Hum(op.a), COLORS[op.d]);
        break;
    case OP_ISONMAP: {
        static const float VALUES[] = {0.01f, 0.99f, -0.01f, -0.61f, 0.5f, -0.3f, 0.0f, 1.0f, -1.0f, 0.0100001f, -0.6099999f};
        const S_vector4 pos = {VALUES[op.a % 11], VALUES[op.b % 11], 0.0f, 1.0f};
        Log("IsOnMap -> %d", side ? port->IsOnMap(pos) : OriginalAt<T_OnMap>(0x004bf230)(orig, &pos.x));
        break;
    }
    case OP_PICK:
        side ? port->PickTarget(op.a) : OriginalAt<T_Pick>(0x004ba070)(orig, op.a);
        break;
    case OP_LEFTPRESS:
        side ? port->OnLeftPress() : OriginalAt<T_Void>(0x004ba390)(orig);
        break;
    case OP_LEFTRELEASE:
        side ? port->OnLeftRelease() : OriginalAt<T_Void>(0x004bad50)(orig);
        break;
    case OP_RIGHTPRESS:
        // (Its move order's place and direction: the w of both stack words in the original, not compared.)
        g_actorOrder = true;
        side ? port->OnRightPress() : OriginalAt<T_Void>(0x004bad80)(orig);
        g_actorOrder = false;
        break;
    case OP_RIGHTRELEASE:
        side ? port->OnRightRelease() : OriginalAt<T_Void>(0x004bb210)(orig);
        break;
    case OP_SHOWROLL:
        side ? port->ShowRollMenu(op.a != 0, static_cast<C_Actor *>(Hum(op.b))) : OriginalAt<T_ShowRoll>(0x004bb0e0)(orig, op.a != 0, Hum(op.b));
        break;
    case OP_LEFTDRAG:
    case OP_INPUT:
    case OP_CURSORMODE:
    case OP_MOVECAMERA: {
        float held[INPUTS], pressed[INPUTS];
        InputArrays(op.c, held, pressed);
        g_actorOrder = op.kind == OP_INPUT;   // (OnRightPress's orders: their w not compared)
        static const uint32_t AT[] = {0x004ba660, 0x004b79c0, 0x004b7e50, 0x004b81a0};
        const int which = op.kind == OP_LEFTDRAG ? 0 : op.kind == OP_INPUT ? 1 : op.kind == OP_CURSORMODE ? 2 : 3;
        if (side) {
            if (which == 0)
                port->OnLeftDrag(held, pressed);
            else if (which == 1)
                port->HandleInput(held, pressed);
            else if (which == 2)
                port->UpdateCursorMode(held, pressed);
            else
                port->MoveCamera(held, pressed);
        } else
            OriginalAt<T_Input>(AT[which])(orig, held, pressed);
        g_actorOrder = false;
        uint32_t h = 2166136261u;
        for (int k = 0; k < INPUTS; ++k)
            h = (h ^ FloatBits(held[k])) * 16777619u ^ FloatBits(pressed[k]);
        Log("input after %08x", h);
        break;
    }
    case OP_STATE: {
        const S_Layout &l = L();
        for (int k = 0; k < 5; ++k)
            Put<uint8_t>(self, l.leftDown + k, uint8_t(op.a >> k & 1));
        Put<uint8_t>(self, l.u5A7BD, uint8_t(op.a >> 5 & 1));
        Put<uint8_t>(self, l.dragging, uint8_t(op.a >> 6 & 1));
        Put<uint8_t>(self, l.refresh, uint8_t(op.a >> 7 & 1));
        Put<int>(self, l.flashTime, op.b & 0xFFFF);
        if (op.a & 0x100)
            Put<int>(self, l.zoom, op.b >> 16);
        // (Part 74b: the target and the members' route counts, set by 74c's input.)
        Put<void *>(self, l.target, Hum(op.c));
        Put<int>(self, l.u10, op.c + 2);
        for (int k = 0; k < 6; ++k)
            Put<int>(self, l.lines + l.linesSize * k + l.routeCount, op.d >> 2 * k & 3);
        Log("state %x %x %d %x", op.a, op.b, op.c, op.d);
        break;
    }
    case OP_LIGHT: {
        uint8_t *button = self + L().buttons + 0x10 * op.a;
        side ? reinterpret_cast<C_CommandPanel::S_Button *>(button)->Light(op.b & 1, op.b & 2)
             : OriginalAt<T_Light>(0x004a8bd0)(button, op.b & 1, op.b & 2);
        break;
    }
    }
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
uint32_t g_seed, g_startWords[8];
void StartScenario()
{
    g_seed = U32();
    for (uint32_t &w : g_startWords)
        w = U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    // (Every face group with a material: Init reads the icons' first unchecked, on both sides.)
    for (int i = 0; i < FRAMES; ++i)
        for (int g = 0; g < GROUPS; ++g)
            if (!At<void *>(g_gfx.groups[i][g], 4))
                Put<void *>(g_gfx.groups[i][g], 4, g_gfx.materials[g]);
    g_frameSeed = U32();
    g_noBorder = P(0.03);
    RandomWorld();
    // (Now and then the camera looking along z and the humans in its plane: the facing test's tie, 0 counted in front.)
    if (P(0.1)) {
        const auto place = [](I3D_frame *f, const S_vector &pos) {
            f->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
            for (S_matrix *m : {&f->m_localMatrix, &f->m_worldMatrix}) {
                (*m)(0) = S_vector(1, 0, 0);
                (*m)(1) = S_vector(0, 1, 0);
                (*m)(2) = S_vector(0, 0, 1);
                (*m)(3) = pos;
            }
        };
        place(FrameAt(F_CAMERA), S_vector(0, 0, 5));
        for (const S_Hum &h : g_w.hum)
            place(FrameAt(h.frame), S_vector(F(-3, 3), F(-3, 3), 5));
    }
}
void RunScenario(const std::vector<S_Op> &ops)
{
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
        std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
        g_w = g_wStart;
        g_log.clear();
        g_boxes.clear();
        std::srand(g_seed);
        g_newCount = 0;
        std::memcpy(g_orig, g_origPristine, 0x5A7C0);
        std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_CommandPanel));
        uint8_t *self = side ? reinterpret_cast<uint8_t *>(g_port) : g_orig;
        {
            // (The same start words in what Init leaves: the pace, mode and stance, the wheel's time, the dragged state.)
            const S_Layout &l = L();
            Put<uint32_t>(self, l.pace, g_startWords[0] % 3 ? g_startWords[0] & 0x1F000 : 0xFFFFFFFFu);
            Put<uint32_t>(self, l.mode, g_startWords[1]);
            Put<uint32_t>(self, l.stance, g_startWords[2]);
            Put<int>(self, l.wheel, int(g_startWords[3] % 300));
            // (And what Init clears: the soldiers, the target, +0x10, the realtime flag, the lines' counts, the input's state,
            // the time, the dragged marker, the flash, the zoom.)
            const uint32_t w = g_startWords[6];
            Put<void *>(self, l.active, Hum(int(w % HUMANS)));
            Put<void *>(self, l.commanded, Hum(int(w / 8 % HUMANS)));
            Put<void *>(self, l.target, Hum(int(w / 64 % HUMANS)));
            Put<int>(self, l.u10, int(w % 7));
            Put<uint8_t>(self, l.realtime, uint8_t(w >> 9 & 1));
            for (int k = 0; k < 6; ++k) {
                Put<int>(self, l.lines + l.linesSize * k + 0xC000, int(w >> k & 3));
                Put<int>(self, l.lines + l.linesSize * k + l.routeCount, int(w >> (k + 2) & 3));
            }
            for (int k = 0; k < 5; ++k)
                Put<uint8_t>(self, l.leftDown + k, uint8_t(w >> (10 + k) & 1));
            Put<uint8_t>(self, l.u5A7BD, uint8_t(w >> 15 & 1));
            Put<uint8_t>(self, l.dragging, uint8_t(w >> 16 & 1));
            Put<uint8_t>(self, l.refresh, uint8_t(w >> 17 & 1));
            Put<int>(self, l.time, int(g_startWords[7] % 1000));
            Put<int>(self, l.flashTime, int(g_startWords[7] % 300));
            Put<void *>(self, l.flashButton, g_startWords[7] % 5 ? self + l.buttons + 0x10 * int(g_startWords[7] % 18) : nullptr);
            Put<int>(self, l.zoom, int(g_startWords[7] / 7 % 6));
            // (The members' first path vertices not zero: AddPath sets every field.)
            for (int k = 0; k < 6; ++k)
                for (int b = 0; b < 32 * 0x18; b += 4)
                    Put<uint32_t>(self, l.lines + l.linesSize * k + b, g_startWords[k] * 2654435761u + uint32_t(b) * 40503u);
            // (Spare markers now and then: Done releases them.)
            const int spares = int(g_startWords[4] % 4);
            for (int k = 0; k < spares; ++k)
                g_spares[side][k] = FrameAt(F_POOL + int((g_startWords[5] + 7 * k) % POOL));
            if (side) {
                g_port->m_spareMarkers.assign(reinterpret_cast<I3D_frame **>(g_spares[1]), reinterpret_cast<I3D_frame **>(g_spares[1]) + spares);
            } else {
                Put<void **>(self, 0x750, spares ? g_spares[0] : nullptr);
                Put<void **>(self, 0x754, spares ? g_spares[0] + spares : nullptr);
                Put<void **>(self, 0x758, spares ? g_spares[0] + spares : nullptr);
            }
        }
        StartGame();
        Put<void *>(g_cursorBlock, 0x78, nullptr);
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            if (op.kind >= 100)
                ApplyWorld(op);
            SyncWorld(side);
            Put<bool>(self + L().roll, L().rollOpen, g_w.rollOpen);
            {
                uint8_t *menu = self + L().roll;
                Put<int>(menu, side ? int(offsetof(C_RollMenu, m_selected)) : 0x84, g_w.rollSelected);
                Put<void *>(menu, side ? int(offsetof(C_RollMenu, m_target)) : 0x88, Hum(g_w.rollTarget));
                for (int k = 0; k < 10; ++k)
                    Put<int>(menu, (side ? int(offsetof(C_RollMenu, m_entries)) : 0x34) + 8 * k, g_w.rollActions[k]);   // (the action first)
            }
            Run(side, op);
            g_log.push_back(Snap(self));
        }
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        // (The original's SetScale writes m_scaleW from a stack word: not compared.)
        for (int i = 0; i < FRAMES; ++i)
            Put<uint32_t>(g_runs[side].arena.frames[i], 0xDC, 0);
        // (The volumes' frames carry each side's own orders: not compared.)
        for (int i = 0; i < FRAMES; ++i)
            Put<uint32_t>(g_runs[side].arena.frames[i], 0xEC, 0);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

void FillOp(S_Op &op)
{
    switch (op.kind) {
    case OP_OPEN:
    case OP_DARKEN:
        op.a = P(0.6);
        break;
    case OP_UPDATE:
    case OP_UPDATEHINT:
        op.a = P(0.5) ? Pick({0, 16, 100, 200, 250}) : I(-5, 600);
        break;
    case OP_SHOWHINT:
        op.a = I(0, 8);
        op.b = 0x1C80 + I(0, 0x40);
        break;
    case OP_LIGHTORDERS:
        op.a = P(0.6) ? Pick({6, 7, 0x20, 0, 1, 2}) : I(-1, 0x22);
        break;
    case OP_PRESS:
        op.a = Pick(IDS);
        break;
    case OP_SETFLAG:
        op.a = I(4, 17);
        break;
    case OP_TOOL:
        op.a = P(0.8) ? I(16, 18) : I(1, 15);
        break;
    case OP_ORDER:
        op.a = P(0.85) ? Pick({1, 2, 3, 4, 5}) : I(6, 18);
        break;
    case OP_LIGHT:
        op.a = I(0, 17);
        op.b = I(0, 3);
        break;
    case OP_REMOVEAT:
        op.a = P(0.7) ? I(20, 29) : Pick({0, 19, 30, 2});
        break;
    case OP_ADDORDER:
        op.a = P(0.7) ? Pick({1, 2, 4, 6, 7}) : I(0, 0x22);
        op.b = I(-20, 20);
        break;
    case OP_ADDACTOR:
        op.a = I(0, 0x30);
        op.b = P(0.9) ? I(0, HUMANS - 1) : -1;
        break;
    case OP_ACQUIRE:
    case OP_UPDATEMARKER:
        op.a = I(0, HUMANS - 1);
        op.b = I(0, ORDERS - 1);
        op.c = I(0, POOL - 1);
        break;
    case OP_RELEASEMARKER:
        op.a = F_POOL + I(0, POOL - 1);
        break;
    case OP_ADDPATH:
        op.a = P(0.9) ? I(0, 5) : Pick({-1, 6, 7});
        op.b = I(0, HUMANS - 1);
        op.c = I(0, ORDERS - 1);
        op.d = I(0, 5);
        break;
    case OP_UPDATEMEMBER:
        op.a = I(0, HUMANS - 1);
        op.b = P(0.9) ? I(0, 5) : Pick({6, 7});
        break;
    case OP_BRACKETS:
    case OP_DRAWBOX:
        op.a = P(0.9) ? I(0, HUMANS - 1) : -1;
        op.b = I(0, 2);
        op.d = I(0, 5);
        break;
    case OP_ISONMAP:
        op.a = I(0, 10);
        op.b = I(0, 10);
        break;
    case OP_PICK:
        op.a = I(0, 100);
        break;
    case OP_SHOWROLL:
        op.a = P(0.6);
        op.b = P(0.9) ? I(0, HUMANS - 1) : -1;
        break;
    case OP_LEFTDRAG:
    case OP_INPUT:
    case OP_CURSORMODE:
    case OP_MOVECAMERA:
        op.c = int(U32() & 0x7FFFFFFF);
        break;
    case OP_STATE:
        op.a = I(0, 0x1FF);
        op.b = (P(0.5) ? Pick({0, 1, 2, 3, 4, 5, -1}) : I(0, 4)) << 16 | (P(0.5) ? Pick({0, 1, 200}) : I(0, 300));
        op.c = P(0.5) ? -1 : I(0, HUMANS - 1);
        op.d = P(0.5) ? 0 : I(0, 0xFFF);
        break;
    case W_WORLD:
    case W_TEAM:
    case W_CURSOR:
    case W_RAYS:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case W_HUMAN:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, HUMANS - 1);
        break;
    case W_ROLL:
        op.a = P(0.5);
        break;
    }
}

// ---- the panel ----
void TestCommand()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{OP_INIT});
    // (Now and then the input's state set first: Open clears it.)
    if (P(0.3)) {
        S_Op state{OP_STATE};
        FillOp(state);
        ops.push_back(state);
    }
    if (P(0.7)) {
        S_Op open{OP_OPEN};
        open.a = 1;
        ops.push_back(open);
    }
    static const int KINDS[] = {OP_OPEN, OP_OPEN, OP_ISOPEN, OP_UPDATE, OP_UPDATE, OP_UPDATE, OP_SHOWHINT, OP_UPDATEHINT,
                                OP_READFLAGS, OP_DARKEN, OP_LIGHTORDERS, OP_LIGHTFLAGS, OP_PRESS, OP_PRESS, OP_PRESS, OP_SETFLAG,
                                OP_TOOL, OP_ORDER, OP_ORDER, OP_ZOOM, OP_LIGHT, W_WORLD, W_HUMAN, W_HUMAN, W_TEAM, W_CURSOR,
                                W_CURSOR, W_ROLL, OP_STATE, OP_REMOVESEL, OP_REMOVEAT, OP_ADDORDER, OP_ADDACTOR, OP_APPLY,
                                OP_ACQUIRE, OP_ACQUIRE, OP_RELEASEMARKER, OP_UPDATEMARKER, OP_UPDATEMARKER, OP_ADDPATH, OP_ADDPATH,
                                OP_UPDATEMEMBER, OP_UPDATEMEMBER, OP_OVERLAY, OP_RENDER, OP_RENDER, OP_RENDERMARKERS, OP_BRACKETS,
                                OP_BRACKETS, OP_DRAWBOX, OP_DRAWBOX, OP_ISONMAP, OP_PICK, OP_PICK, OP_PICK, OP_LEFTPRESS,
                                OP_LEFTPRESS, OP_LEFTPRESS, OP_LEFTDRAG, OP_LEFTDRAG, OP_LEFTDRAG, OP_LEFTRELEASE, OP_RIGHTPRESS,
                                OP_RIGHTPRESS, OP_RIGHTPRESS, OP_RIGHTRELEASE, OP_SHOWROLL, OP_SHOWROLL, OP_INPUT, OP_INPUT,
                                OP_INPUT, OP_INPUT, OP_CURSORMODE, OP_CURSORMODE, OP_MOVECAMERA, OP_MOVECAMERA, W_RAYS, W_RAYS};
    for (int n = I(0, 18); n > 0; --n) {
        S_Op op{Pick(KINDS)};
        FillOp(op);
        ops.push_back(op);
    }
    if (P(0.3))
        ops.push_back(S_Op{OP_DONE});
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    g_wStart = g_w;
    RunScenario(ops);
}

// ---- the port's fix, alone: Init without "Border" (the original updated the null frame) ----
void TestFixes()
{
    StartScenario();
    g_noBorder = true;
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    g_side = 1;
    std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_CommandPanel));
    SyncWorld(1);
    CHECK(g_port->Init(reinterpret_cast<I3D_scene *>(&g_scene), reinterpret_cast<C_Interface *>(g_ui), nullptr, nullptr,
                       reinterpret_cast<C_InfoSelector *>(g_selector), reinterpret_cast<C_MapCursor *>(g_cursorBlock)) &&
              g_port->m_frame == nullptr && !g_port->IsOpen(),
          "Init without Border");
    g_port->Open(true);
    g_port->Update(16);
    // Part 74b: AddPath (the original wrote outside the six members' buffers, past a buffer's points, and left an
    // empty path's segment count at -1, writing before the buffer next time) ...
    alignas(16) S_vector4 points[5];
    for (int k = 0; k < 5; ++k)
        points[k] = {float(k), 2.0f * float(k), -float(k), 1.0f};
    S_Vector4Array path;
    path.data = points;
    path.capacity = 5;
    path.count = 5;
    auto &lines = g_port->m_lines;
    std::vector<uint8_t> before(reinterpret_cast<uint8_t *>(&lines), reinterpret_cast<uint8_t *>(&lines) + sizeof lines);
    g_port->AddPath(-1, COLORS[1], path);
    g_port->AddPath(C_CommandPanel::MEMBERS, COLORS[1], path);
    path.count = 0;
    g_port->AddPath(0, COLORS[1], path);
    CHECK(std::memcmp(before.data(), &lines, sizeof lines) == 0, "AddPath outside the buffers or empty");
    path.count = 5;
    lines[2].pathSegments = C_CommandPanel::PATH_VERTICES - 3;
    g_port->AddPath(2, COLORS[1], path);
    CHECK(lines[2].pathSegments == C_CommandPanel::PATH_VERTICES - 1 && lines[2].paths[C_CommandPanel::PATH_VERTICES - 1].x == 2.0f &&
              lines[2].paths[C_CommandPanel::PATH_VERTICES - 3].x == 0.0f,
          "AddPath at the buffer's end: %d", lines[2].pathSegments);
    path.data = nullptr;
    // ... and UpdateMember for a seventh member (the original wrote past the buffers): its markers kept up to date, no lines.
    for (int k = 0; k < ORDERS; ++k)
        FrameAt(g_w.hum[0].order[k].marker)->SetOn(false);
    g_w.hum[0].orderCount = 3;
    SyncWorld(1);
    g_port->m_refreshPaths = true;
    const uint8_t *panel = reinterpret_cast<const uint8_t *>(g_port);
    before.assign(panel, panel + sizeof(C_CommandPanel));
    g_log.clear();
    g_port->UpdateMember(static_cast<C_Human *>(Hum(0)), C_CommandPanel::MEMBERS);
    CHECK(std::memcmp(before.data(), panel, sizeof(C_CommandPanel)) == 0, "UpdateMember for a seventh member: the panel");
    CHECK(std::all_of(panel + sizeof(C_CommandPanel), panel + sizeof(C_CommandPanel) + PANEL_GUARD, [](uint8_t b) { return b == 0xA5; }),
          "UpdateMember for a seventh member: past the panel");
    // (The log: a marker can be another's "selector_commands" child, switched off after.)
    for (int k = 0; k < 3; ++k)
        CHECK(std::count(g_log.begin(), g_log.end(), Name(FrameAt(g_w.hum[0].order[k].marker)) + " SetOn 1") > 0,
              "UpdateMember for a seventh member: marker %d", k);
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    SetUpGfx();
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(+[](void *, uint32_t type) __attribute__((stdcall))->void * {
        Log("driver CreateFrame %u", type);
        return FrameAt(F_MODEL);
    });
    for (void *&v : g_origCameraVt)
        v = reinterpret_cast<void *>(O_HumanEffects);
    Put<void *>(g_origCamera, 0, g_origCameraVt);
    // The actors' vtables (GetBoxCorners 0xDC, GetSeatCount 0x88, GetSeatOccupant 0x78; the port's from its member
    // pointers), the game scene's TransformPoints (0x98, the kit's scene vtable), the graph's SetTexture, SetWorldMatrix,
    // DrawPrimitives.
    for (void *&v : g_origActorVt)
        v = reinterpret_cast<void *>(V_ActorOther);
    for (void *&v : g_portActorVt)
        v = reinterpret_cast<void *>(V_ActorOther);
    g_origActorVt[0xDC / 4] = reinterpret_cast<void *>(V_BoxCorners);
    g_origActorVt[0x88 / 4] = reinterpret_cast<void *>(V_SeatCount);
    g_origActorVt[0x78 / 4] = reinterpret_cast<void *>(V_SeatOccupant);
    g_portActorVt[Slot(&C_Actor::GetBoxCorners) / 4] = reinterpret_cast<void *>(V_BoxCorners);
    g_portActorVt[Slot(&C_Actor::GetSeatCount) / 4] = reinterpret_cast<void *>(V_SeatCount);
    g_portActorVt[Slot(&C_Actor::GetSeatOccupant) / 4] = reinterpret_cast<void *>(V_SeatOccupant);
    g_sceneVt[0x98 / 4] = reinterpret_cast<void *>(SC_Transform);
    g_findChild = FindChild;
    g_sceneVt[0x94 / 4] = reinterpret_cast<void *>(SC_Unmap);
    g_graphVt[0xA0 / 4] = reinterpret_cast<void *>(G_AsciiKey);
    g_graphVt[0xCC / 4] = reinterpret_cast<void *>(G_KeyChar);
    g_graphVt[0x44 / 4] = reinterpret_cast<void *>(G_Texture);
    g_graphVt[0x84 / 4] = reinterpret_cast<void *>(G_World);
    g_graphVt[0x88 / 4] = reinterpret_cast<void *>(G_Draw);
    for (void *&v : g_origGfxVt)
        v = reinterpret_cast<void *>(V_GfxOther);
    for (void *&v : g_portGfxVt)
        v = reinterpret_cast<void *>(V_GfxOther);
    // (The port's slots are the original's + 4: g++'s two destructors.)
    const int thunder = Slot(&C_GEffectsModule::SetThunderstorm), update = Slot(&C_GEffectsModule::Update);
    g_portGfxVt[thunder / 4] = reinterpret_cast<void *>(V_Thunderstorm);
    g_portGfxVt[update / 4] = reinterpret_cast<void *>(V_GfxUpdate);
    g_origGfxVt[(thunder - 4) / 4] = reinterpret_cast<void *>(V_Thunderstorm);
    g_origGfxVt[(update - 4) / 4] = reinterpret_cast<void *>(V_GfxUpdate);
    Put<void *>(g_origGfx, 0, g_origGfxVt);
    Put<void *>(g_portGfx, 0, g_portGfxVt);
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 20; ++k)
            g_icons[s][k] = g_gfx.textures[(s * 20 + k) % TEXTURES];
    g_clickSound = reinterpret_cast<I3D_sound *>(FrameAt(F_CLICK));
    *OriginalAt<void **>(0x0089bc8c) = FrameAt(F_CLICK);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0045d1d0, reinterpret_cast<const void *>(O_Active)},
        {0x0045d1b0, reinterpret_cast<const void *>(O_Commanded)},
        {0x00462300, reinterpret_cast<const void *>(O_MemberCount)},
        {0x004619f0, reinterpret_cast<const void *>(O_Member)},
        {0x00461a20, reinterpret_cast<const void *>(O_IsMember)},
        {0x0045eb30, reinterpret_cast<const void *>(O_Seen)},
        {0x0045f080, reinterpret_cast<const void *>(O_RefreshSight)},
        {0x005a5f00, reinterpret_cast<const void *>(O_OrderCount)},
        {0x005a5f50, reinterpret_cast<const void *>(O_GetOrder)},
        {0x005a5c80, reinterpret_cast<const void *>(O_StateFlags)},
        {0x005a5cb0, reinterpret_cast<const void *>(O_ModeFlags)},
        {0x005a5d10, reinterpret_cast<const void *>(O_StanceFlags)},
        {0x005a5d30, reinterpret_cast<const void *>(O_Suspended)},
        {0x005a6380, reinterpret_cast<const void *>(O_Unconscious)},
        {0x005a86f0, reinterpret_cast<const void *>(O_FadeOut)},
        {0x005a8740, reinterpret_cast<const void *>(O_FadeIn)},
        {0x005a5e90, reinterpret_cast<const void *>(O_CancelOrders)},
        {0x005ac470, reinterpret_cast<const void *>(O_BodyPose)},
        {0x00464e90, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x00462ae0, reinterpret_cast<const void *>(O_SetTactical)},
        {0x004632c0, reinterpret_cast<const void *>(O_SetTacticalMode)},
        {0x00463ba0, reinterpret_cast<const void *>(O_UpdateTactical)},
        {0x00462750, reinterpret_cast<const void *>(O_SetTacticalZoom)},
        {0x00486510, reinterpret_cast<const void *>(O_GetGfx)},
        {0x006b7720, reinterpret_cast<const void *>(O_ControlMode)},
        {0x0069bb80, reinterpret_cast<const void *>(O_SetPaused)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x004290a0, reinterpret_cast<const void *>(O_OpenInto)},
        {0x004ab7e0, reinterpret_cast<const void *>(O_SelectedOrder)},
        {0x004ab700, reinterpret_cast<const void *>(O_SelectIndex)},
        {0x004abdd0, reinterpret_cast<const void *>(O_Hovered)},
        {0x0049d340, reinterpret_cast<const void *>(O_Highlight)},
        {0x004b0750, reinterpret_cast<const void *>(O_RollInit)},
        {0x004b0900, reinterpret_cast<const void *>(O_RollDone)},
        {0x004b2ce0, reinterpret_cast<const void *>(O_RollSelectAt)},
        {0x004b2fc0, reinterpret_cast<const void *>(O_RollUpdate)},
        {0x004b2fa0, reinterpret_cast<const void *>(O_RollRender)},
        {0x005825c0, reinterpret_cast<const void *>(O_AllocOrder)},
        {0x005a5f30, reinterpret_cast<const void *>(O_AddOrderReq)},
        {0x005a5f20, reinterpret_cast<const void *>(O_InsertOrder)},
        {0x005a5eb0, reinterpret_cast<const void *>(O_RemoveOrder)},
        {0x005a5f40, reinterpret_cast<const void *>(O_SetOrder)},
        {0x005a5ed0, reinterpret_cast<const void *>(O_OrdersChanged)},
        {0x0058c860, reinterpret_cast<const void *>(O_InitPlain)},
        {0x0058c540, reinterpret_cast<const void *>(O_OrderDtor)},
        {0x0058cce0, reinterpret_cast<const void *>(O_UpdatePath)},
        {0x004ab820, reinterpret_cast<const void *>(O_SelectedIndex)},
        {0x004abf50, reinterpret_cast<const void *>(O_SetFromWorld)},
        {0x00718830, reinterpret_cast<const void *>(O_RayTest)},
        {0x00718770, reinterpret_cast<const void *>(O_CollisionRayTest)},
        {0x004ab9a0, reinterpret_cast<const void *>(O_ScreenPos)},
        {0x004abaf0, reinterpret_cast<const void *>(O_SetMode)},
        {0x004ac090, reinterpret_cast<const void *>(O_Clamp)},
        {0x004ab610, reinterpret_cast<const void *>(O_SelectOrder)},
        {0x0045dd40, reinterpret_cast<const void *>(O_SetCommanded)},
        {0x004b2dc0, reinterpret_cast<const void *>(O_RollInput)},
        {0x004b2be0, reinterpret_cast<const void *>(O_RollOver)},
        {0x004b2ca0, reinterpret_cast<const void *>(O_RollSelect)},
        {0x004b3030, reinterpret_cast<const void *>(O_RollMove)},
        {0x004b3060, reinterpret_cast<const void *>(O_RollPrepare)},
        {0x004b27b0, reinterpret_cast<const void *>(O_RollOpen)},
        {0x004b0920, reinterpret_cast<const void *>(O_RollTarget)},
        {0x006c7510, reinterpret_cast<const void *>(O_ClearKey)},
        {0x006b6a60, reinterpret_cast<const void *>(O_DetailSettings)},
        {0x004633a0, reinterpret_cast<const void *>(O_Motion)},
        {0x007e5356, reinterpret_cast<const void *>(O_WBound)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // The panels, built once (the original's hint boxes by its text box constructor; their calls not logged).
    g_orig = static_cast<uint8_t *>(std::calloc(1, 0x5A7C0));
    for (int k = 0; k < 8; ++k)
        OriginalAt<T_Ctor>(0x00496cc0)(g_orig + 0xA8 + 0x9C * k);
    Put<int>(g_orig, 0x590 + 0x84, -1);   // (the roll menu's choice, as the port's default)
    Put<int>(g_orig, 0x90, 1);            // (the zoom, as the port's default)
    g_origPristine = static_cast<uint8_t *>(std::malloc(0x5A7C0));
    std::memcpy(g_origPristine, g_orig, 0x5A7C0);
    // (A seventh member's buffer after the panel, as a guard: the fixes' case checks UpdateMember leaves it.)
    uint8_t *block = static_cast<uint8_t *>(std::calloc(1, sizeof(C_CommandPanel) + PANEL_GUARD));
    std::memset(block + sizeof(C_CommandPanel), 0xA5, PANEL_GUARD);
    g_port = new (block) C_CommandPanel();
    g_portPristine = static_cast<uint8_t *>(std::malloc(sizeof(C_CommandPanel)));
    std::memcpy(g_portPristine, static_cast<void *>(g_port), sizeof(C_CommandPanel));
    g_panels[0] = g_orig;
    g_panels[1] = g_port;
    g_log.clear();
    g_boxes.clear();
}

}  // namespace H

// ---- the 74b / 74c references (not run in this part: aborting stubs) ----
// (Not [[noreturn]]: g++ would take the patched functions calling these as never returning and drop the code after
// their calls.)
volatile bool g_exitOnStub = true;
void NotHere(const char *what)
{
    std::printf("commandc_diff: %s reached\n", what);
    if (g_exitOnStub)
        std::exit(3);
}
void C_CameraHumanEffects::SetTacticalMotion(float x, float y) { H::Motion(x, y); }
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *pos, S_vector *normal, uint32_t types,
                                             uint32_t flags) const
{
    return static_cast<S_CollisionVolume *>(H::Ray("RayTest", &from.x, &dir.x, pos ? &pos->x : nullptr, normal ? &normal->x : nullptr,
                                                   types, flags));
}
S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *pos, S_vector *normal, uint32_t types,
                                    uint32_t flags)
{
    return static_cast<S_CollisionVolume *>(H::Ray("CollisionRayTest", &from.x, &dir.x, pos ? &pos->x : nullptr,
                                                   normal ? &normal->x : nullptr, types, flags));
}
void C_Controls::ClearKey(int key, float *, float *) { H::ClearKeyLog(key); }
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return static_cast<C_PlayerOptions *>(H::DetailSettings()); }
void C_InfoSelector::SelectOrder(S_HumanOrder *order) { H::SelectOrderLog(order); }
void C_MapCursor::ClampMotion(float *held, float *pressed) const { H::Clamp(held, pressed); }
void C_MapCursor::GetScreenPos(int *x, int *y) const { H::ScreenPos(x, y); }
void C_MapCursor::SetMode(int mode) { H::SetMode(mode); }
void C_RollMenu::HandleInput(float *held, float *pressed) { H::RollInput(held, pressed); }
bool C_RollMenu::IsCursorOver() const { return H::RollOver(); }
void C_RollMenu::MoveTo(const S_vector &p) { H::RollMove(&p.x); }
void C_RollMenu::Open(bool open) { H::RollOpen(this, open); }
void C_RollMenu::Prepare() { H::RollLog("Prepare"); }
bool C_RollMenu::Select(int i) { return H::RollSelect(i); }
void C_RollMenu::SetTarget(C_Actor *target, float distance) { H::RollTarget(target, distance); }
bool C_Team::SetCommanded(C_Human *h) { return H::SetCommanded(h); }
void __stdcall I3D_visual::UpdateWBoundProc() { H::WBound(this); }

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("commandc_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_COMMANDC_QUICK") ? 6000 : 20000;
    if (const char *r = std::getenv("HD2_COMMANDC_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_COMMANDC_CASE");
    const struct {
        const char *name;
        void (*run)();
        int divisor;
    } cases[] = {{"CommandC", TestCommand, 1}, {"CommandCFixes", TestFixes, 100}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < std::max(1, rounds / c.divisor); ++g_round)
            c.run();
    }
    std::printf("commandc_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
