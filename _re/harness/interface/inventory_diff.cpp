// Human part 73b: the inventory screen (game/interface_inventory.cpp lines 522-1354: C_InventoryScreen) against the
// original's (0x0049fef0..0x004a4aa0). MinGW; build_inventory.bat; runs inventory_diff.exe ("inventory_diff: N checks,
// 0 failures"). interface_inventory.cpp is compiled into this file (its scroll bars run as they are: part 73a); the kit
// (interface_kit.inc, interface_gfx.inc) fakes the frames, the scene, the driver, the graph, the text boxes and the
// meshes, materials and textures. Each side's screen is built once (the original's constructor 0x0049fef0, the port's)
// and its bytes restored before each scenario (the text boxes' methods are faked: their insides do not change).
// Scenarios of the screen's operations (Init, Open, Show, Refresh, Update, Render, HandleInput and the private
// ShowSlots, StartDrag, SendAcross, Drop, OnPress, OnHold, OnRelease, OnDoubleClick, UpdateDescription, UpdateSkills,
// IsCursorInside, HideSelections, HandleScrollBars; between them the world changing) run from one start on each side,
// every call out logged by the same fakes, the screen's fields snapped after each operation by each side's layout (the
// original's text boxes are 0x9C bytes, the port's 0x84; its scroll bars 0x94, the port's 0x84), the frames compared at
// the end.
// The world: 4 humans (the soldier, the searched body, two more; now and then the body another actor), their item
// containers (an item, groups of items, spaces), 16 items, 8 item definitions with their components, the map cursor
// (position, hovered frame data, motion, inside the screen), the frame the scene picks, the rank names, the buttons.
// Faked on both sides: the humans (GetName, GetHitPoints, GetFullName, GetProfileUnknown24, IsUnconscious,
// GetHealthPercent, GetStat, GetStatFactor, GetViewDir, RequestMoveItem, RequestTransferItem, RequestThrowItem), the
// containers' (GetMaxSpace, GetTotalSpace, GetUsedSlotCount, GetSlots, GetItem), the items' (GetAmount, GetMaxAmount),
// the definitions' (GetImage, GetBehaviour, GetAmmoItemId), the components' GetMuzzleVelocity, the cursor (GetPos,
// GetHoveredId, GetMove, IsInRect), the texts' Get, FindTexture and DuplicateMaterial, the scene's PickFrame and
// AddFrame, the frames' SetName, the meshes' DuplicateMaterials.
// HD2_INVENTORY_ROUNDS: scenarios (default 8,000; HD2_INVENTORY_QUICK: 3,000); HD2_INVENTORY_CASE: one case only;
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
#include "game/interface_inventory.cpp"
#include "game/game_texts.h"
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

// ---- the frames: f0 "inventory", f1 "items00", f2 "items01", f3 "description", f4 "skills", f5 the scene's sector;
// the rest the children the frames find by name (a hash: the same name of the same frame the same child) and the
// dragged icon the driver creates (f127) ----
constexpr int F_INVENTORY = 0, F_PAGE0 = 1, F_PAGE1 = 2, F_DESCRIPTION = 3, F_SKILLS = 4, F_SECTOR = 5, F_POOL = 6,
              POOL = 120, F_DRAG = 127;
uint32_t g_childSeed;
I3D_frame *FindFrame(const char *name)
{
    static const char *const NAMES[] = {"inventory", "items00", "items01", "description", "skills"};
    for (int k = 0; k < 5; ++k)
        if (std::strcmp(name, NAMES[k]) == 0)
            return FrameAt(k);
    return nullptr;
}
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    const uint32_t h = Fnv(name, g_childSeed ^ (uint32_t(FrameIndex(f) + 1) * 0x9E3779B1u));
    return FrameAt(F_POOL + int(h % POOL));
}
I3D_frame *CreateVisual(uint32_t) { return FrameAt(F_DRAG); }

// ---- the world ----
constexpr int HUMANS = 4, CONTAINERS = 7, ITEMS = 16, DEFS = 8, COMPS = 6, GROUP_SLOTS = 16, GROUP_ITEMS = 3;
struct S_Con {
    bool present;
    int item;   // GetItem: an item or -1
    int used;   // GetUsedSlotCount
    int groupSize[GROUP_SLOTS], group[GROUP_SLOTS][GROUP_ITEMS];
    float total, max;
};
struct S_Hum {
    int type, health, role, profile24, healthPercent, stats[12], view;
    float statFactors[12], weight, viewDir[4];
    bool u1a5, changed, unconscious, hasContainers;
    char name[16], fullName[20];
    S_Con con[CONTAINERS];
};
struct S_Item {
    int def, kind;
    float amount, maxAmount;
};
struct S_Def {
    int textId, behaviour, ammoId, component, secondary;
    float weight;
};
struct S_Comp {
    int kind, aimMode;
    float fov, velocity;
};
struct S_World {
    S_Hum hum[HUMANS];
    S_Item item[ITEMS];
    S_Def def[DEFS];
    S_Comp comp[COMPS];
    float cursor[4], move[3];
    int hovered, picked;   // GetHoveredId; the frame PickFrame gives (-1: none)
    bool inRect;
    float held[64], pressed[64];
    uint32_t role;
};
S_World g_w, g_wStart;

// ---- each side's objects ----
alignas(16) uint8_t g_origHum[HUMANS][0x300], g_portHum[HUMANS][0x300];
alignas(16) uint8_t g_origCon[HUMANS][CONTAINERS][0x60], g_portCon[HUMANS][CONTAINERS][0x60];
alignas(16) uint8_t g_origItem[ITEMS][0x40], g_portItem[ITEMS][0x40];
alignas(16) uint8_t g_origDef[DEFS][0x80], g_portDef[DEFS][0x80];
alignas(16) uint8_t g_origComp[COMPS][0x40], g_portComp[COMPS][0x40];
alignas(16) uint8_t g_cursorBlock[0x80], g_game[0x80], g_scene2[0x400];
void *g_origHumVt[0x40], *g_portHumVt[0x80], *g_origConVt[0x20], *g_portConVt[0x40], *g_origItemVt[0x10], *g_portItemVt[0x20],
    *g_origDefVt[0x10], *g_portDefVt[0x20], *g_origCompVt[0x10], *g_portCompVt[0x20];
void *g_origStores[HUMANS][CONTAINERS + 1], *g_portStores[HUMANS][CONTAINERS + 1];
// The groups: the original's an array of pointers to MSVC vectors (+4 first, +8 last), the port's libstdc++ vectors.
uint8_t g_origVec[HUMANS][CONTAINERS][GROUP_SLOTS][0x10];
void *g_origVecPtr[HUMANS][CONTAINERS][GROUP_SLOTS];
std::vector<C_Item *> g_portVec[HUMANS][CONTAINERS][GROUP_SLOTS];
void *g_groupItems[2][HUMANS][CONTAINERS][GROUP_SLOTS][GROUP_ITEMS];
C_str g_names[HUMANS];
int g_side;

void *Hum(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portHum[i]) : static_cast<void *>(g_origHum[i]); }
void *Item(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portItem[i]) : static_cast<void *>(g_origItem[i]); }
void *Def(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portDef[i]) : static_cast<void *>(g_origDef[i]); }
void *Comp(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portComp[i]) : static_cast<void *>(g_origComp[i]); }
template <size_t N, size_t M> int IndexIn(const void *p, uint8_t (&a)[N][M], uint8_t (&b)[N][M])
{
    for (size_t i = 0; i < N; ++i)
        if (p == a[i] || p == b[i])
            return int(i);
    return -1;
}
bool ConIndex(const void *p, int &h, int &c)
{
    for (h = 0; h < HUMANS; ++h)
        for (c = 0; c < CONTAINERS; ++c)
            if (p == g_origCon[h][c] || p == g_portCon[h][c])
                return true;
    return false;
}
std::string PartName(const void *p)
{
    char text[24];
    int i, c;
    if ((i = IndexIn(p, g_origHum, g_portHum)) >= 0)
        std::snprintf(text, sizeof text, "h%d", i);
    else if ((i = IndexIn(p, g_origItem, g_portItem)) >= 0)
        std::snprintf(text, sizeof text, "i%d", i);
    else if ((i = IndexIn(p, g_origDef, g_portDef)) >= 0)
        std::snprintf(text, sizeof text, "def%d", i);
    else if ((i = IndexIn(p, g_origComp, g_portComp)) >= 0)
        std::snprintf(text, sizeof text, "comp%d", i);
    else if (ConIndex(p, i, c))
        std::snprintf(text, sizeof text, "h%d.c%d", i, c);
    else if (p == g_cursorBlock)
        return "cursor";
    else if (p == g_scene2)
        return "scene";
    else
        return GfxName(p);
    return text;
}
const S_Hum &HW(const void *h) { return g_w.hum[IndexIn(h, g_origHum, g_portHum)]; }

// ---- the humans ----
C_str *GetName(const void *h)
{
    Log("%s GetName", Name(h).c_str());
    return &g_names[IndexIn(h, g_origHum, g_portHum)];
}
int HitPoints(const void *h)
{
    Log("%s GetHitPoints", Name(h).c_str());
    return HW(h).stats[STAT_STRENGTH];
}
const char *FullName(const void *h)
{
    Log("%s GetFullName", Name(h).c_str());
    return HW(h).fullName;
}
int Profile24(const void *h)
{
    Log("%s GetProfileUnknown24", Name(h).c_str());
    return HW(h).profile24;
}
bool Unconscious(const void *h)
{
    Log("%s IsUnconscious", Name(h).c_str());
    return HW(h).unconscious;
}
int HealthPercent(const void *h)
{
    Log("%s GetHealthPercent", Name(h).c_str());
    return HW(h).healthPercent;
}
int Stat(const void *h, int stat)
{
    Log("%s GetStat %d", Name(h).c_str(), stat);
    return stat >= 0 && stat < 12 ? HW(h).stats[stat] : 0;
}
float StatFactor(const void *h, int stat)
{
    Log("%s GetStatFactor %d", Name(h).c_str(), stat);
    return stat >= 0 && stat < 12 ? HW(h).statFactors[stat] : 0.0f;
}
const float *ViewDir(const void *h)
{
    Log("%s GetViewDir", Name(h).c_str());
    return HW(h).viewDir;
}
std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }
bool MoveItem(const void *h, const void *item, const void *other, int from, int to, int slot)
{
    Log("%s RequestMoveItem %s %s %d %d %d", Name(h).c_str(), Name(item).c_str(), Name(other).c_str(), from, to, slot);
    return true;
}
bool TransferItem(const void *h, const void *item, int arg, const void *other, const void *otherItem, int container)
{
    Log("%s RequestTransferItem %s %d %s %s %d", Name(h).c_str(), Name(item).c_str(), arg, Name(other).c_str(), Name(otherItem).c_str(), container);
    return true;
}
void ThrowItem(const void *h, const void *item, int arg, const float *dir)
{
    Log("%s RequestThrowItem %s %d %s", Name(h).c_str(), Name(item).c_str(), arg, F3(dir).c_str());
}
C_str *__attribute__((thiscall)) V_GetName(void *h) { return GetName(h); }
int __attribute__((thiscall)) V_HitPoints(void *h) { return HitPoints(h); }
void __attribute__((thiscall)) V_Other(void *p) { Log("%s other slot", Name(p).c_str()); }
const char *__attribute__((thiscall)) O_FullName(void *h) { return FullName(h); }
int __attribute__((thiscall)) O_Profile24(void *h) { return Profile24(h); }
bool __attribute__((thiscall)) O_Unconscious(void *h) { return Unconscious(h); }
int __attribute__((thiscall)) O_HealthPercent(void *h) { return HealthPercent(h); }
int __attribute__((thiscall)) O_Stat(void *h, int stat) { return Stat(h, stat); }
float __attribute__((thiscall)) O_StatFactor(void *h, int stat) { return StatFactor(h, stat); }
const float *__attribute__((thiscall)) O_ViewDir(void *h) { return ViewDir(h); }
bool __attribute__((thiscall)) O_MoveItem(void *h, void *item, void *other, int from, int to, int slot) { return MoveItem(h, item, other, from, to, slot); }
bool __attribute__((thiscall)) O_TransferItem(void *h, void *item, int arg, void *other, void *otherItem, int container)
{
    return TransferItem(h, item, arg, other, otherItem, container);
}
void __attribute__((thiscall)) O_ThrowItem(void *h, void *item, int arg, const float *dir) { ThrowItem(h, item, arg, dir); }

// ---- the containers, the items, the definitions, the components ----
const S_Con &CW(const void *c)
{
    int h, k;
    ConIndex(c, h, k);
    return g_w.hum[h].con[k];
}
float MaxSpace(const void *c)
{
    Log("%s GetMaxSpace", Name(c).c_str());
    return CW(c).max;
}
float TotalSpace(const void *c)
{
    Log("%s GetTotalSpace", Name(c).c_str());
    return CW(c).total;
}
int UsedSlots(const void *c)
{
    Log("%s GetUsedSlotCount", Name(c).c_str());
    return CW(c).used;
}
void *Slots(const void *c)
{
    int h, k;
    ConIndex(c, h, k);
    Log("%s GetSlots", Name(c).c_str());
    return g_side ? static_cast<void *>(g_portVec[h][k]) : static_cast<void *>(g_origVecPtr[h][k]);
}
void *ConItem(const void *c)
{
    Log("%s GetItem", Name(c).c_str());
    return Item(CW(c).item);
}
float Amount(const void *i)
{
    Log("%s GetAmount", Name(i).c_str());
    return g_w.item[IndexIn(i, g_origItem, g_portItem)].amount;
}
float MaxAmount(const void *i)
{
    Log("%s GetMaxAmount", Name(i).c_str());
    return g_w.item[IndexIn(i, g_origItem, g_portItem)].maxAmount;
}
const S_Def &DW(const void *d) { return g_w.def[IndexIn(d, g_origDef, g_portDef)]; }
void *Image(const void *d)
{
    Log("%s GetImage", Name(d).c_str());
    return g_gfx.textures[IndexIn(d, g_origDef, g_portDef) + 8];
}
int Behaviour(const void *d)
{
    Log("%s GetBehaviour", Name(d).c_str());
    return DW(d).behaviour;
}
int AmmoId(const void *d)
{
    Log("%s GetAmmoItemId", Name(d).c_str());
    return DW(d).ammoId;
}
float Velocity(const void *c)
{
    Log("%s GetMuzzleVelocity", Name(c).c_str());
    return g_w.comp[IndexIn(c, g_origComp, g_portComp)].velocity;
}
float __attribute__((thiscall)) V_MaxSpace(void *c) { return MaxSpace(c); }
float __attribute__((thiscall)) V_TotalSpace(void *c) { return TotalSpace(c); }
int __attribute__((thiscall)) V_UsedSlots(void *c) { return UsedSlots(c); }
void *__attribute__((thiscall)) V_Slots(void *c) { return Slots(c); }
void *__attribute__((thiscall)) V_ConItem(void *c) { return ConItem(c); }
float __attribute__((thiscall)) V_Amount(void *i) { return Amount(i); }
float __attribute__((thiscall)) V_MaxAmount(void *i) { return MaxAmount(i); }
void *__attribute__((thiscall)) O_Image(void *d) { return Image(d); }
int __attribute__((thiscall)) V_Behaviour(void *d) { return Behaviour(d); }
int __attribute__((thiscall)) V_AmmoId(void *d) { return AmmoId(d); }
float __attribute__((thiscall)) V_Velocity(void *c) { return Velocity(c); }

// ---- the cursor, the texts, the materials, the scene's picking, the frames' names, the meshes ----
// (Not logged: the port's GetPos is inline.)
const float *CursorPos()
{
    return reinterpret_cast<const float *>(g_cursorBlock + 0x20);
}
int Hovered()
{
    Log("cursor GetHoveredId -> %d", g_w.hovered);
    return g_w.hovered;
}
void GetMove(float *move)
{
    Log("cursor GetMove");
    std::memcpy(move, g_w.move, 12);
}
bool InRect(const float *rect)
{
    Log("cursor IsInRect %s %s", F3(rect).c_str(), F3(rect + 4).c_str());
    return g_w.inRect;
}
std::string TextOf(int id)
{
    Log("texts Get %d", id);
    return "T" + std::to_string(id);
}
void *Texture(const void *visual, const char *name)
{
    Log("FindTexture %s %s", Name(visual).c_str(), name);
    return g_gfx.textures[FrameIndex(visual) % 8];
}
void *Material(const void *visual, const char *name)
{
    Log("DuplicateMaterial %s %s", Name(visual).c_str(), name);
    return g_gfx.materials[std::max(0, FrameIndex(visual)) % (MATERIALS - 8) + 8];
}
const float *__attribute__((thiscall)) O_CursorPos(void *) { return CursorPos(); }
int __attribute__((thiscall)) O_Hovered(void *) { return Hovered(); }
// (The original returns its argument in EAX: its caller passes that on as the motion.)
float *__attribute__((thiscall)) O_GetMove(void *, float *move)
{
    GetMove(move);
    return move;
}
bool __attribute__((thiscall)) O_InRect(void *, const float *rect) { return InRect(rect); }
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    new (out) C_str(TextOf(id).c_str());
    return out;
}
void *__attribute__((fastcall)) O_Texture(void *visual, const char *name) { return Texture(visual, name); }
void *__attribute__((fastcall)) O_Material(void *visual, const char *name) { return Material(visual, name); }
void *__stdcall SC_Pick(void *, const float *from, const float *dir, uint32_t flags, int, int, int, int, int, int)
{
    Log("scene PickFrame %s %s %s %x", F3(from).c_str(), FloatText(from[3]).c_str(), F3(dir).c_str(), flags);
    return g_w.picked >= 0 ? FrameAt(g_w.picked) : nullptr;
}
void __stdcall SC_Add(void *, void *f) { Log("scene AddFrame %s", Name(f).c_str()); }
void __stdcall F_SetName(void *f, const char *name) { Log("%s SetName %s", Name(f).c_str(), name); }
void __stdcall M_DuplicateMaterials(void *mesh) { Log("%s DuplicateMaterials", Name(mesh).c_str()); }
void *__cdecl O_GetGame() { return g_game; }
void __attribute__((thiscall)) O_FontsRelease(void *) {}

}  // namespace H

// ---- the port's side of the same fakes ----
S_RankName g_rankNames[8];
const char *C_Human::GetFullName() const { return H::FullName(this); }
int C_Human::GetProfileUnknown24() const { return H::Profile24(this); }
bool C_Human::IsUnconscious() const { return H::Unconscious(this); }
int C_Human::GetHealthPercent() const { return H::HealthPercent(this); }
int C_Human::GetStat(int stat) const { return H::Stat(this, stat); }
float C_Human::GetStatFactor(int stat) const { return H::StatFactor(this, stat); }
const S_vector &C_Human::GetViewDir() const { return *reinterpret_cast<const S_vector *>(H::ViewDir(this)); }
bool C_Human::RequestMoveItem(C_Item *item, C_Actor *other, int from, int to, int slot) { return H::MoveItem(this, item, other, from, to, slot); }
bool C_Human::RequestTransferItem(C_Item *item, int arg, C_Human *other, C_Item *otherItem, int container)
{
    return H::TransferItem(this, item, arg, other, otherItem, container);
}
void C_Human::RequestThrowItem(C_Item *item, int arg, const S_vector &dir) { H::ThrowItem(this, item, arg, &dir.x); }
IImage *C_ItemDef::GetImage() { return static_cast<IImage *>(H::Image(this)); }
int C_MapCursor::GetHoveredId() { return H::Hovered(); }
void C_MapCursor::GetMove(float *move) const { H::GetMove(move); }
bool C_MapCursor::IsInRect(const float *rect) const { return H::InRect(rect); }
C_str C_GameTexts::Get(int id) const { return C_str(H::TextOf(id).c_str()); }
I3D_material *DuplicateMaterial(I3D_frame *visual, const char *fileName) { return static_cast<I3D_material *>(H::Material(visual, fileName)); }
I3D_texture *FindTexture(I3D_frame *visual, const char *fileName) { return static_cast<I3D_texture *>(H::Texture(visual, fileName)); }

namespace H {

// ---- building each side's objects from the world ----
void SyncWorld(int side)
{
    g_side = side;
    for (int i = 0; i < ITEMS; ++i) {
        const S_Item &w = g_w.item[i];
        uint8_t *b = side ? g_portItem[i] : g_origItem[i];
        std::memset(b, 0, 0x40);
        if (!side) {
            Put<void *>(b, 0, g_origItemVt);
            Put<void *>(b, 4, g_origDef[w.def]);
            Put<int>(b, 8, w.kind);
        } else {
            Put<void *>(b, 0, g_portItemVt);
            reinterpret_cast<C_Item *>(b)->m_def = reinterpret_cast<C_ItemDef *>(g_portDef[w.def]);
            reinterpret_cast<C_Item *>(b)->m_kind = E_ItemKind(w.kind);
        }
    }
    for (int i = 0; i < COMPS; ++i) {
        const S_Comp &w = g_w.comp[i];
        uint8_t *b = side ? g_portComp[i] : g_origComp[i];
        std::memset(b, 0, 0x40);
        if (!side) {
            Put<void *>(b, 0, g_origCompVt);
            Put<int>(b, 8, w.kind);
            Put<int>(b, 0xC, w.aimMode);
            Put<float>(b, 0x10, w.fov);
        } else {
            Put<void *>(b, 0, g_portCompVt);
            C_ScopeComp *c = reinterpret_cast<C_ScopeComp *>(b);
            c->m_kind = E_ItemComponentKind(w.kind);
            c->m_aimMode = w.aimMode;
            c->m_aimedFovFactor = w.fov;
        }
    }
    for (int i = 0; i < DEFS; ++i) {
        const S_Def &w = g_w.def[i];
        uint8_t *b = side ? g_portDef[i] : g_origDef[i];
        std::memset(b, 0, 0x80);
        if (!side) {
            Put<void *>(b, 0, g_origDefVt);
            Put<int>(b, 0x14, w.textId);
            Put<float>(b, 0x18, w.weight);
            Put<void *>(b, 0x48, Comp(w.component));
            Put<void *>(b, 0x4C, Comp(w.secondary));
        } else {
            Put<void *>(b, 0, g_portDefVt);
            C_ItemDef *d = reinterpret_cast<C_ItemDef *>(b);
            d->m_textId = w.textId;
            d->m_weight = w.weight;
            d->m_component = static_cast<C_ItemComponent *>(Comp(w.component));
            d->m_secondaryComponent = static_cast<C_ItemComponent *>(Comp(w.secondary));
        }
    }
    for (int h = 0; h < HUMANS; ++h) {
        const S_Hum &w = g_w.hum[h];
        g_names[h] = C_str(w.name);
        void **stores = side ? g_portStores[h] : g_origStores[h];
        for (int c = 0; c < CONTAINERS; ++c) {
            const S_Con &cw = w.con[c];
            uint8_t *b = side ? g_portCon[h][c] : g_origCon[h][c];
            std::memset(b, 0, 0x60);
            Put<void *>(b, 0, side ? static_cast<void *>(g_portConVt) : static_cast<void *>(g_origConVt));
            stores[c] = cw.present ? b : nullptr;
            for (int g = 0; g < GROUP_SLOTS; ++g) {
                void **items = g_groupItems[side][h][c][g];
                for (int k = 0; k < cw.groupSize[g]; ++k)
                    items[k] = Item(cw.group[g][k]);
                if (!side) {
                    uint8_t *v = g_origVec[h][c][g];
                    std::memset(v, 0, 0x10);
                    if (cw.groupSize[g]) {
                        Put<void **>(v, 4, items);
                        Put<void **>(v, 8, items + cw.groupSize[g]);
                        Put<void **>(v, 0xC, items + cw.groupSize[g]);
                    }
                    g_origVecPtr[h][c][g] = v;
                } else {
                    g_portVec[h][c][g].assign(reinterpret_cast<C_Item **>(items), reinterpret_cast<C_Item **>(items) + cw.groupSize[g]);
                }
            }
        }
        uint8_t *b = side ? g_portHum[h] : g_origHum[h];
        std::memset(b, 0, 0x300);
        if (!side) {
            Put<void *>(b, 0, g_origHumVt);
            Put<int>(b, 0x18, w.type);
            Put<void **>(b, 0x74, w.hasContainers ? stores : nullptr);
            Put<float>(b, 0x7C, w.weight);
            Put<int>(b, 0xEC, w.health);
            Put<bool>(b, 0x178, w.changed);
            Put<int>(b, 0x19C, w.role);
            Put<bool>(b, 0x1A5, w.u1a5);
        } else {
            Put<void *>(b, 0, g_portHumVt);
            C_Human *p = reinterpret_cast<C_Human *>(b);
            p->m_type = E_ActorType(w.type);
            p->m_containers = w.hasContainers ? reinterpret_cast<C_ItemContainer **>(stores) : nullptr;
            p->m_carriedWeight = w.weight;
            p->m_stats[STAT_HEALTH] = w.health;
            p->m_inventoryChanged = w.changed;
            p->m_role = E_HumanRole(w.role);
            p->m_unknown1a5 = w.u1a5;
        }
    }
    std::memcpy(g_cursorBlock + offsetof(C_MapCursor, m_pos), g_w.cursor, 16);
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
}

const char *const NAMES[] = {"Bob", "Ann", "", "Sgt. Lee", "Jo-Jo", "100%"};
float Small() { return P(0.2) ? Pick({0.0f, 1.0f, -1.0f, 100.0f}) : F(-5, 200); }
void RandomCon(S_Con &c)
{
    c = S_Con{};
    c.present = P(0.85);
    c.item = P(0.25) ? -1 : I(0, ITEMS - 1);
    c.used = P(0.4) ? I(0, 3) : I(4, 10);
    for (int g = 0; g < GROUP_SLOTS; ++g) {
        c.groupSize[g] = g < c.used + 1 && P(0.8) ? I(1, GROUP_ITEMS) : P(0.1) ? 1 : 0;
        for (int k = 0; k < GROUP_ITEMS; ++k)
            c.group[g][k] = I(0, ITEMS - 1);
    }
    c.max = P(0.1) ? Pick({0.0f, 1.0f}) : F(1, 100);
    c.total = P(0.2) ? Pick({0.0f, c.max}) : F(0, c.max);
}
void RandomHuman(int i)
{
    S_Hum &h = g_w.hum[i];
    h = S_Hum{};
    h.type = i == 1 && P(0.1) ? Pick({int(ACTOR_SUPPLY_BOX), int(ACTOR_CAR)}) : int(ACTOR_HUMAN);
    h.health = P(0.8) ? I(1, 800) : Pick({0, -1});
    h.role = P(0.8) ? I(1, 5) : Pick({0, 6});
    h.profile24 = I(0, 7);
    h.healthPercent = P(0.5) ? I(0, 100) : Pick({0, 100, 101, -1});
    for (int &s : h.stats)
        s = P(0.8) ? I(0, 150) : Pick({0, 1, -1, 1000});
    for (float &f : h.statFactors)
        f = P(0.8) ? F(0, 1.5f) : Pick({0.0f, 1.0f, -0.5f, 0.999f});
    h.weight = P(0.2) ? Pick({0.0f, 1.0f}) : F(0, 60);
    for (float &f : h.viewDir)
        f = F(-1, 1);
    h.u1a5 = P(0.3);
    h.changed = P(0.3);
    h.unconscious = P(0.4);
    h.hasContainers = P(0.95);
    std::snprintf(h.name, sizeof h.name, "%s", NAMES[I(0, 5)]);
    std::snprintf(h.fullName, sizeof h.fullName, "%s Smith", NAMES[I(0, 5)]);
    for (S_Con &c : h.con)
        RandomCon(c);
}
void RandomItems()
{
    for (S_Item &it : g_w.item) {
        it.def = I(0, DEFS - 1);
        it.kind = P(0.9) ? I(0, 3) : Pick({4, -1});
        it.amount = P(0.3) ? Pick({-1.0f, 0.0f, 30.0f, 2.5f}) : F(0, 200);
        it.maxAmount = P(0.3) ? Pick({-1.0f, 0.0f, 30.0f}) : F(0, 200);
    }
    for (S_Def &d : g_w.def) {
        d.textId = 0x2000 + I(0, 0xFF);
        d.behaviour = P(0.3) ? 4 : P(0.7) ? I(0, 13) : Pick({-1, 12, 4, 100});
        d.ammoId = P(0.7) ? I(-1, 20) : Pick({-1, 0});
        d.component = P(0.2) ? -1 : I(0, COMPS - 1);
        d.secondary = P(0.3) ? -1 : I(0, COMPS - 1);
        d.weight = P(0.2) ? Pick({0.0f, 1.0f, 0.005f}) : F(0, 20);
    }
    for (S_Comp &c : g_w.comp) {
        c.kind = P(0.5) ? I(0, 10) : Pick({2, 3, 4, 4});
        c.aimMode = P(0.6) ? I(0, 3) : Pick({-1, 3, 4, 4, 4, 5, 6});
        c.fov = P(0.2) ? Pick({0.0f, 1.0f, 0.5f}) : F(0.05f, 1.5f);
        c.velocity = P(0.2) ? Pick({0.0f, 300.0f}) : F(0, 1000);
    }
}
const int IDS[] = {200, 201, 202, 203, 204, 205, 210, 211, 212, 213, 214, 215, 220, 221, 222, 223, 230, 231, 232, 233,
                   240, 241, 242, 243, 250, 251, 252, 253, 0xF11, 0xF12, 0xF13, 0xF21, 0xF22, 0xF23, 0xF31, 0xF33, 0xF41,
                   0xF42, 0xF43, 0, 0, 0xF00, 0xF50, 199, 206, 207, 209, 224, 226, 229, 244, 247, 249, 1000};
void RandomCursor()
{
    for (float &f : g_w.cursor)
        f = P(0.2) ? Pick({0.0f, 0.5f}) : F(-1, 1);
    for (float &f : g_w.move)
        f = P(0.4) ? 0.0f : F(-0.1f, 0.1f);
    g_w.hovered = Pick(IDS);
    // (Mostly an icon's frame: a slot's or a line's, as the pages find them.)
    static const char *const ICONS[] = {"bck*", "head*", "drop*", "hand*", "uniform*", "shoulder*", "item00*", "item01*", "item02*",
                                        "item03*", "item04*", "item05*", "item06*", "item07*"};
    g_w.picked = P(0.15) ? -1 : P(0.25) ? I(F_POOL, F_POOL + POOL - 1) : FrameIndex(FindChild(FrameAt(I(F_PAGE0, F_PAGE1)), Pick(ICONS)));
    g_w.inRect = P(0.7);
}
void RandomInput()
{
    for (float &f : g_w.held)
        f = 0.0f;
    for (float &f : g_w.pressed)
        f = 0.0f;
    g_w.pressed[INPUT_CLICK] = P(0.4) ? 1.0f : 0.0f;
    g_w.held[INPUT_CLICK] = P(0.4) ? 1.0f : 0.0f;
    g_w.held[INPUT_CLICK2] = P(0.15) ? 1.0f : 0.0f;
}
void RandomWorld()
{
    for (int i = 0; i < HUMANS; ++i)
        RandomHuman(i);
    RandomItems();
    RandomCursor();
    RandomInput();
    g_w.role = P(0.6) ? 7u : Pick({0u, 1u, 3u});
}
void RandomFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}
int g_humanSeed;
void RandomOneHuman()
{
    RandomHuman(g_humanSeed);
}
// A frame moved (the icons' world positions, the frames' bounds).
int g_frameSeed;
void MoveFrame()
{
    I3D_frame *f = FrameAt(g_frameSeed);
    f->m_worldMatrix(3) = S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
    f->m_frameFlags = P(0.5) ? f->m_frameFlags | 1u : f->m_frameFlags & ~1u;
}

// ---- the operations ----
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
};
enum {
    OP_INIT, OP_OPEN, OP_SHOW, OP_REFRESH, OP_UPDATE, OP_RENDER, OP_INPUT, OP_SHOWSLOTS, OP_STARTDRAG, OP_SENDACROSS, OP_DROP,
    OP_PRESS, OP_HOLD, OP_RELEASE, OP_DOUBLECLICK, OP_DESCRIPTION, OP_SKILLS, OP_INSIDE, OP_HIDE, OP_SCROLLBARS, OP_SHOWN,
    W_WORLD = 100, W_HUMAN, W_ITEMS, W_CURSOR, W_INPUT, W_FRAME, W_HOVER, W_CLICK, W_NUDGE
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
    case W_ITEMS:
        RandomFrom(op.a, &RandomItems);
        break;
    case W_CURSOR:
        RandomFrom(op.a, &RandomCursor);
        break;
    case W_INPUT:
        RandomFrom(op.a, &RandomInput);
        break;
    case W_FRAME:
        g_frameSeed = op.b;
        RandomFrom(op.a, &MoveFrame);
        break;
    case W_HOVER:
        g_w.hovered = op.a;
        break;
    case W_NUDGE:
        // (One coordinate of the cursor moved.)
        g_w.cursor[op.a % 3] += 0.01f * float(op.b + 1);
        break;
    case W_CLICK:
        g_w.pressed[INPUT_CLICK] = op.a & 1 ? 1.0f : 0.0f;
        g_w.held[INPUT_CLICK] = op.a & 2 ? 1.0f : 0.0f;
        g_w.held[INPUT_CLICK2] = 0.0f;
        break;
    }
}

// ---- each side's layout of the screen ----
struct S_Layout {
    int frame, pages, emptyTexture, description, hoverTime, hovered, descText, dragIcon, dragMaterial, dragItem, dragOffset,
        dragContainer, dragHuman, weightBars, slots, slotSize, belts, backpacks, listSize, skills, skillsTitle, skillNames,
        skillBars, skillValues, bars, barSize, mode, human, other, names, weights, cursor, pressed, doubleClick, pressPos,
        activeBar, box, boxMargin, boxDimmed;
    // a slot's; a list's; a bar's
    int sSelection, sIcon, sMaterial, sAmount, sItem, lSelection, lIcons, lMaterials, lAmounts, lItems, lBar, lCapacity, lScroll,
        bDisabled, bStep, bCount, bIndex, bPart;
};
const S_Layout ORIG = {4, 8, 0x10, 0x14, 0x18, 0x1C, 0x20, 0xBC, 0xC0, 0xC4, 0xD0, 0xE0, 0xE4, 0xE8, 0xF0, 0x148, 0x1050,
                       0x1818, 0x3E4, 0x1FE0, 0x1FE4, 0x2080, 0x24C4, 0x24E0, 0x2924, 0x94, 0x2B74, 0x2B78, 0x2B7C, 0x2B80,
                       0x2CB8, 0x2DF0, 0x2DF4, 0x2DF8, 0x2E00, 0x2E10, 0x9C, 0x90, 0x71,
                       0x9C, 0xA0, 0xA4, 0xA8, 0x144, 0x9C, 0xA0, 0xB0, 0xC0, 0x330, 0x340, 0x344, 0x3E0,
                       0x0C, 0x80, 0x84, 0x88, 0x8C};
using T_Screen = C_InventoryScreen;
using T_SlotP = C_InventoryScreen::S_Slot;
using T_ListP = C_InventoryScreen::S_List;
using T_BarP = C_InventoryScrollBar;
#define OF(T, f) int(offsetof(T, f))
const S_Layout PORT = {OF(T_Screen, m_frame), OF(T_Screen, m_pages), OF(T_Screen, m_emptyTexture), OF(T_Screen, m_description),
                       OF(T_Screen, m_hoverTime), OF(T_Screen, m_hovered), OF(T_Screen, m_descriptionText), OF(T_Screen, m_dragIcon),
                       OF(T_Screen, m_dragMaterial), OF(T_Screen, m_dragItem), OF(T_Screen, m_dragOffset), OF(T_Screen, m_dragContainer),
                       OF(T_Screen, m_dragHuman), OF(T_Screen, m_weightBars), OF(T_Screen, m_slots), int(sizeof(T_SlotP)),
                       OF(T_Screen, m_belts), OF(T_Screen, m_backpacks), int(sizeof(T_ListP)), OF(T_Screen, m_skills),
                       OF(T_Screen, m_skillsTitle), OF(T_Screen, m_skillNames), OF(T_Screen, m_skillBars), OF(T_Screen, m_skillValues),
                       OF(T_Screen, m_scrollBars), int(sizeof(T_BarP)), OF(T_Screen, m_mode), OF(T_Screen, m_human), OF(T_Screen, m_other),
                       OF(T_Screen, m_names), OF(T_Screen, m_weights), OF(T_Screen, m_cursor), OF(T_Screen, m_pressed),
                       OF(T_Screen, m_doubleClickTime), OF(T_Screen, m_pressPos), OF(T_Screen, m_activeBar), int(sizeof(C_TextBox)),
                       OF(C_TextBox, m_margin), OF(C_TextBox, m_dimmed),
                       OF(T_SlotP, selection), OF(T_SlotP, icon), OF(T_SlotP, material), OF(T_SlotP, amount), OF(T_SlotP, item),
                       OF(T_ListP, selection), OF(T_ListP, icons), OF(T_ListP, materials), OF(T_ListP, amounts), OF(T_ListP, items),
                       OF(T_ListP, capacityBar), OF(T_ListP, capacity), OF(T_ListP, scroll),
                       OF(T_BarP, m_disabled), OF(T_BarP, m_step), OF(T_BarP, m_count), OF(T_BarP, m_index), OF(T_BarP, m_part)};
#undef OF
const S_Layout &L() { return g_side ? PORT : ORIG; }
std::string Box(const uint8_t *b)
{
    const S_Layout &l = L();
    return BoxName(b) + ":" + std::to_string(At<uint8_t>(b, l.boxMargin)) + ":" + std::to_string(At<uint8_t>(b, l.boxDimmed));
}
std::string Ptr(const uint8_t *p, int at) { return Name(At<void *>(p, at)); }
std::string Snap(const uint8_t *s)
{
    const S_Layout &l = L();
    std::string r = "screen " + Ptr(s, l.frame) + " " + Ptr(s, l.pages) + " " + Ptr(s, l.pages + 4) + " tex " + Ptr(s, l.emptyTexture) +
                     " desc " + Ptr(s, l.description) + " hover " + std::to_string(At<int>(s, l.hoverTime)) + " " + Ptr(s, l.hovered) +
                     " " + Box(s + l.descText) + " drag " + Ptr(s, l.dragIcon) + " " + Ptr(s, l.dragMaterial) + " " + Ptr(s, l.dragItem) +
                     " " + F3(reinterpret_cast<const float *>(s + l.dragOffset)) + " " + std::to_string(At<int>(s, l.dragContainer)) + " " +
                     Ptr(s, l.dragHuman) + " weights " + Ptr(s, l.weightBars) + " " + Ptr(s, l.weightBars + 4);
    r += " slots";
    for (int i = 0; i < 12; ++i) {
        const uint8_t *p = s + l.slots + l.slotSize * i;
        r += " " + Box(p) + "," + Ptr(p, l.sSelection) + "," + Ptr(p, l.sIcon) + "," + Ptr(p, l.sMaterial) + "," + Box(p + l.sAmount) +
             "," + Ptr(p, l.sItem);
    }
    r += " lists";
    for (int n = 0; n < 4; ++n) {
        const uint8_t *p = s + (n < 2 ? l.belts : l.backpacks) + l.listSize * (n & 1);
        r += " " + Box(p) + "," + Ptr(p, l.lSelection) + "," + Ptr(p, l.lBar) + "," + Box(p + l.lCapacity) + "," +
             std::to_string(At<int>(p, l.lScroll));
        for (int j = 0; j < 4; ++j)
            r += "," + Ptr(p, l.lIcons + 4 * j) + "/" + Ptr(p, l.lMaterials + 4 * j) + "/" + Box(p + l.lAmounts + l.box * j) + "/" +
                 Ptr(p, l.lItems + 4 * j);
    }
    r += " skills " + Ptr(s, l.skills) + " " + Box(s + l.skillsTitle);
    for (int i = 0; i < 7; ++i)
        r += " " + Box(s + l.skillNames + l.box * i) + "," + Ptr(s, l.skillBars + 4 * i) + "," + Box(s + l.skillValues + l.box * i);
    r += " bars";
    for (int i = 0; i < 4; ++i) {
        const uint8_t *b = s + l.bars + l.barSize * i;
        r += " " + std::to_string(At<uint8_t>(b, l.bDisabled)) + "," + FloatText(At<float>(b, l.bStep)) + "," +
             std::to_string(At<int>(b, l.bCount)) + "," + std::to_string(At<int>(b, l.bIndex)) + "," + std::to_string(At<int>(b, l.bPart));
    }
    r += " mode " + std::to_string(At<uint32_t>(s, l.mode)) + " " + Ptr(s, l.human) + " " + Ptr(s, l.other) + " " + Box(s + l.names) + " " +
         Box(s + l.names + l.box) + " " + Box(s + l.weights) + " " + Box(s + l.weights + l.box) + " " + Ptr(s, l.cursor) + " pressed " +
         std::to_string(At<uint8_t>(s, l.pressed)) + " " + std::to_string(At<int>(s, l.doubleClick)) + " " +
         F3(reinterpret_cast<const float *>(s + l.pressPos)) + " bar " + std::to_string(At<int>(s, l.activeBar));
    return r;
}

// Both sides crash: no soldier (the operations read him), the right page without its actor, a drop by nobody (an
// item of the right page dragged after the actor left), Init twice (its new dragged icon added again: not a crash,
// left out to keep one icon).
const char *Skip(const uint8_t *s, const S_Op &op)
{
    const S_Layout &l = L();
    const void *human = At<void *>(s, l.human), *other = At<void *>(s, l.other);
    const bool inited = At<void *>(s, l.frame) != nullptr;
    const uint32_t mode = At<uint32_t>(s, l.mode);
    if (op.kind == OP_INIT)
        return inited ? "Init twice" : nullptr;
    if (!inited)
        return "before Init";
    if (op.kind == OP_OPEN || op.kind == OP_RENDER || op.kind == OP_INSIDE || op.kind == OP_HIDE || op.kind == OP_SHOWN)
        return nullptr;
    if (!human)
        return "no soldier";
    if ((mode & 2) && !other)
        return "the right page without its actor";
    if (At<void *>(s, l.dragItem) && !At<void *>(s, l.dragHuman))
        return "a drag by nobody";
    if (op.kind == OP_SHOWSLOTS && op.b == 1 && !other)
        return "the right page without its actor";
    return nullptr;
}

using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, void *);
using T_Open = void(__attribute__((thiscall)) *)(void *, int, void *, void *);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Ret = bool(__attribute__((thiscall)) *)(void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Input = void(__attribute__((thiscall)) *)(void *, float *, float *);
using T_Slots = void(__attribute__((thiscall)) *)(void *, void *, int);

void Run(int side, C_InventoryScreen *port, uint8_t *orig, const S_Op &op)
{
    uint8_t *self = side ? reinterpret_cast<uint8_t *>(port) : orig;
    if (const char *why = Skip(self, op)) {
        Log("skipped: %s", why);
        return;
    }
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(g_scene2), reinterpret_cast<C_MapCursor *>(g_cursorBlock))
                               : OriginalAt<T_Init>(0x004a0ee0)(orig, g_scene2, g_cursorBlock));
        break;
    case OP_OPEN:
        side ? port->Open(op.a, static_cast<C_Human *>(Hum(op.b)), static_cast<C_Actor *>(Hum(op.c)))
             : OriginalAt<T_Open>(0x004a4380)(orig, op.a, Hum(op.b), Hum(op.c));
        break;
    case OP_SHOW:
        side ? port->Show(op.a != 0) : OriginalAt<T_Bool>(0x004a3920)(orig, op.a != 0);
        break;
    case OP_REFRESH:
        side ? port->Refresh() : OriginalAt<T_Void>(0x004a26d0)(orig);
        break;
    case OP_UPDATE:
        side ? port->Update(op.a) : OriginalAt<T_Int>(0x004a4150)(orig, op.a);
        break;
    case OP_RENDER:
        side ? port->Render() : OriginalAt<T_Void>(0x004a41e0)(orig);
        break;
    case OP_INPUT:
        side ? port->HandleInput(g_w.held, g_w.pressed) : OriginalAt<T_Input>(0x004a3420)(orig, g_w.held, g_w.pressed);
        break;
    case OP_SHOWSLOTS:
        side ? port->ShowSlots(static_cast<C_Human *>(Hum(op.a)), op.b) : OriginalAt<T_Slots>(0x004a1e20)(orig, Hum(op.a), op.b);
        break;
    case OP_STARTDRAG:
        side ? port->StartDrag(op.a) : OriginalAt<T_Int>(0x004a34d0)(orig, op.a);
        break;
    case OP_SENDACROSS:
        side ? port->SendAcross(op.a) : OriginalAt<T_Int>(0x004a35d0)(orig, op.a);
        break;
    case OP_DROP:
        if (!At<void *>(self, L().dragItem)) {
            Log("skipped: no drag");
            break;
        }
        side ? port->Drop() : OriginalAt<T_Void>(0x004a2de0)(orig);
        break;
    case OP_PRESS:
        side ? port->OnPress() : OriginalAt<T_Void>(0x004a3790)(orig);
        break;
    case OP_HOLD:
        side ? port->OnHold() : OriginalAt<T_Void>(0x004a3870)(orig);
        break;
    case OP_RELEASE:
        side ? port->OnRelease() : OriginalAt<T_Void>(0x004a38f0)(orig);
        break;
    case OP_DOUBLECLICK:
        side ? port->OnDoubleClick() : OriginalAt<T_Void>(0x004a3830)(orig);
        break;
    case OP_DESCRIPTION:
        side ? port->UpdateDescription(op.a) : OriginalAt<T_Int>(0x004a39b0)(orig, op.a);
        break;
    case OP_SKILLS:
        side ? port->UpdateSkills() : OriginalAt<T_Void>(0x004a4680)(orig);
        break;
    case OP_INSIDE:
        Log("IsCursorInside %d", side ? port->IsCursorInside() : OriginalAt<T_Ret>(0x004a4630)(orig));
        break;
    case OP_HIDE:
        side ? port->HideSelections() : OriginalAt<T_Void>(0x004a31f0)(orig);
        break;
    case OP_SCROLLBARS:
        side ? port->HandleScrollBars(g_w.held, g_w.pressed) : OriginalAt<T_Input>(0x004a3280)(orig, g_w.held, g_w.pressed);
        break;
    case OP_SHOWN:
        Log("IsShown %d", side ? port->IsShown() : (FrameAt(F_INVENTORY)->m_frameFlags & 1) != 0);
        break;
    }
}

// ---- the screens: built once per side, restored before each scenario ----
uint8_t *g_orig, *g_origPristine;
C_InventoryScreen *g_port;
uint8_t *g_portPristine;
struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
uint32_t g_seed;
uint32_t g_startWords[8];
void StartScenario()
{
    g_seed = U32();
    for (uint32_t &w : g_startWords)
        w = U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    g_childSeed = U32();
    RandomWorld();
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
        std::memcpy(g_orig, g_origPristine, 0x2E20);
        std::memcpy(static_cast<void *>(g_port), g_portPristine, sizeof(C_InventoryScreen));
        uint8_t *self = side ? reinterpret_cast<uint8_t *>(g_port) : g_orig;
        // (The same start words in what Init clears or leaves: the drag, the button, the double click, the active bar,
        // the lists' scrolls.)
        {
            const S_Layout &l = L();
            Put<void *>(self, l.dragItem, g_startWords[0] % 3 ? Item(int(g_startWords[0] % ITEMS)) : nullptr);
            Put<uint8_t>(self, l.pressed, uint8_t(g_startWords[1] & 1));
            Put<int>(self, l.doubleClick, int(g_startWords[2] % 300));
            Put<int>(self, l.activeBar, int(g_startWords[3] % 5));
            for (int n = 0; n < 4; ++n)
                Put<int>(self + (n < 2 ? l.belts : l.backpacks) + l.listSize * (n & 1), l.lScroll, int(g_startWords[4 + n] % 6));
        }
        for (const S_Op &op : ops) {
            Log("-- %d %d %d %d", op.kind, op.a, op.b, op.c);
            if (op.kind >= 100)
                ApplyWorld(op);
            SyncWorld(side);
            if (op.kind < 100)
                Run(side, g_port, g_orig, op);
            g_log.push_back(Snap(self));
        }
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        // (The original's SetScale writes m_scaleW (+0xDC) from a stack word; the port leaves it: not compared.)
        for (int i = 0; i < FRAMES; ++i)
            Put<uint32_t>(g_runs[side].arena.frames[i], 0xDC, 0);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

int AnyHuman() { return P(0.05) ? -1 : I(0, HUMANS - 1); }
void FillOp(S_Op &op)
{
    switch (op.kind) {
    case OP_OPEN:
        op.a = P(0.5) ? 1 : P(0.7) ? 3 : Pick({0, 2, 9, 0xB});
        op.b = P(0.95) ? 0 : AnyHuman();
        op.c = (op.a & 2) && P(0.9) ? 1 : P(0.5) ? -1 : AnyHuman();
        break;
    case OP_SHOW:
        op.a = P(0.7);
        break;
    case OP_UPDATE:
    case OP_DESCRIPTION:
        op.a = P(0.5) ? Pick({0, 16, 100, 401, 500}) : I(-5, 600);
        break;
    case OP_SHOWSLOTS:
        op.a = P(0.8) ? I(0, 1) : AnyHuman();
        op.b = I(0, 1);
        break;
    case OP_STARTDRAG:
    case OP_SENDACROSS:
        op.a = Pick(IDS);
        break;
    case W_WORLD:
    case W_ITEMS:
    case W_CURSOR:
    case W_INPUT:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case W_HUMAN:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, HUMANS - 1);
        break;
    case W_FRAME:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, FRAMES - 1);
        break;
    }
}

// ---- the screen ----
void TestInventory()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{OP_INIT});
    S_Op open{OP_OPEN};
    FillOp(open);
    ops.push_back(open);
    if (P(0.8)) {
        S_Op show{OP_SHOW};
        show.a = 1;
        ops.push_back(show);
    }
    // (Mostly a press on a place, the cursor moved and held, then let go; or a double click.)
    if (P(0.6)) {
        ops.push_back(S_Op{W_CURSOR, int(U32() & 0x7FFFFFFF)});
        ops.push_back(S_Op{OP_PRESS});
        if (P(0.3)) {
            ops.push_back(S_Op{OP_DOUBLECLICK});
        } else {
            for (int n = I(0, 2); n > 0; --n) {
                ops.push_back(S_Op{W_CURSOR, int(U32() & 0x7FFFFFFF)});
                ops.push_back(S_Op{OP_HOLD});
            }
            ops.push_back(S_Op{Pick({OP_RELEASE, OP_RELEASE, OP_DROP})});
        }
    }
    // (Now and then the cursor resting on an icon: the description; or a scroll bar hovered and clicked.)
    if (P(0.5)) {
        ops.push_back(S_Op{W_CURSOR, int(U32() & 0x7FFFFFFF)});
        ops.push_back(S_Op{OP_DESCRIPTION, I(0, 100)});
        ops.push_back(S_Op{OP_DESCRIPTION, P(0.2) ? 400 : I(401, 600)});
        if (P(0.3))
            ops.push_back(S_Op{OP_DESCRIPTION, I(0, 600)});
    }
    if (P(0.35)) {
        const int bar = I(1, 4), part = I(1, 3);
        ops.push_back(S_Op{W_HOVER, 0xF00 | bar << 4 | part});
        ops.push_back(S_Op{W_CLICK, 0});
        ops.push_back(S_Op{OP_SCROLLBARS});
        for (int n = I(1, 3); n > 0; --n) {
            ops.push_back(S_Op{W_CLICK, Pick({1, 2, 0})});
            ops.push_back(S_Op{Pick({OP_SCROLLBARS, OP_INPUT})});
        }
    }
    // (Now and then a drag from a place (either page) to another, the drop place or no place.)
    if (P(0.35)) {
        static const int FROM[] = {200, 201, 203, 204, 205, 210, 211, 213, 214, 215, 220, 221, 230, 231, 240, 241, 250, 251};
        static const int TO[] = {202, 212, 210, 230, 250, 220, 240, 200, 206, 207, 209, 224, 228, 244, 249, 0, 1000};
        ops.push_back(S_Op{W_HOVER, Pick(FROM)});
        ops.push_back(S_Op{OP_PRESS});
        ops.push_back(S_Op{W_NUDGE, I(0, 2), I(0, 3)});
        ops.push_back(S_Op{OP_HOLD});
        ops.push_back(S_Op{W_HOVER, Pick(TO)});
        ops.push_back(S_Op{Pick({OP_RELEASE, OP_RELEASE, OP_SENDACROSS})});
        if (ops.back().kind == OP_SENDACROSS)
            ops.back().a = Pick(TO);
    }
    static const int KINDS[] = {OP_OPEN, OP_SHOW, OP_REFRESH, OP_UPDATE, OP_UPDATE, OP_RENDER, OP_RENDER, OP_INPUT, OP_INPUT,
                                OP_INPUT, OP_SHOWSLOTS, OP_STARTDRAG, OP_SENDACROSS, OP_DROP, OP_PRESS, OP_HOLD, OP_RELEASE,
                                OP_DOUBLECLICK, OP_DESCRIPTION, OP_DESCRIPTION, OP_SKILLS, OP_INSIDE, OP_HIDE, OP_SCROLLBARS,
                                OP_SCROLLBARS, OP_SHOWN, W_WORLD, W_HUMAN, W_HUMAN, W_ITEMS, W_CURSOR, W_CURSOR, W_INPUT,
                                W_INPUT, W_FRAME};
    for (int n = I(0, 14); n > 0; --n) {
        S_Op op{Pick(KINDS)};
        FillOp(op);
        ops.push_back(op);
    }
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    g_wStart = g_w;
    RunScenario(ops);
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    g_createVisual = CreateVisual;
    SetUpGfx();
    g_frameVt[0x34 / 4] = reinterpret_cast<void *>(F_SetName);
    g_sceneVt[0x80 / 4] = reinterpret_cast<void *>(SC_Add);
    g_sceneVt[0xFC / 4] = reinterpret_cast<void *>(SC_Pick);
    g_meshVt[0x18 / 4] = reinterpret_cast<void *>(M_DuplicateMaterials);
    Put<void *>(g_scene2, 0, g_sceneVt);
    Put<void *>(g_scene2, 0x1F8, FrameAt(F_SECTOR));
    // The vtables: the original's slots, the port's from its member pointers.
    for (void **vt : {g_origHumVt, g_origConVt, g_origItemVt, g_origDefVt, g_origCompVt})
        for (int k = 0; k < 0x10; ++k)
            vt[k] = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_origHumVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_origConVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void **vt : {g_portHumVt, g_portConVt, g_portItemVt, g_portDefVt, g_portCompVt})
        for (int k = 0; k < 0x20; ++k)
            vt[k] = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portHumVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portConVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origHumVt[0x04 / 4] = reinterpret_cast<void *>(V_GetName);
    g_origHumVt[0x10 / 4] = reinterpret_cast<void *>(V_HitPoints);
    g_portHumVt[Slot(&C_Actor::GetName) / 4] = reinterpret_cast<void *>(V_GetName);
    g_portHumVt[Slot(&C_Actor::GetHitPoints) / 4] = reinterpret_cast<void *>(V_HitPoints);
    g_origConVt[0x10 / 4] = reinterpret_cast<void *>(V_MaxSpace);
    g_origConVt[0x20 / 4] = reinterpret_cast<void *>(V_TotalSpace);
    g_origConVt[0x28 / 4] = reinterpret_cast<void *>(V_UsedSlots);
    g_origConVt[0x3C / 4] = reinterpret_cast<void *>(V_Slots);
    g_origConVt[0x40 / 4] = reinterpret_cast<void *>(V_ConItem);
    g_portConVt[Slot(&C_ItemContainer::GetMaxSpace) / 4] = reinterpret_cast<void *>(V_MaxSpace);
    g_portConVt[Slot(&C_ItemContainer::GetTotalSpace) / 4] = reinterpret_cast<void *>(V_TotalSpace);
    g_portConVt[Slot(&C_ItemContainer::GetUsedSlotCount) / 4] = reinterpret_cast<void *>(V_UsedSlots);
    g_portConVt[Slot(&C_ItemContainer::GetSlots) / 4] = reinterpret_cast<void *>(V_Slots);
    g_portConVt[Slot(&C_ItemContainer::GetItem) / 4] = reinterpret_cast<void *>(V_ConItem);
    g_origItemVt[0x0C / 4] = reinterpret_cast<void *>(V_Amount);
    g_origItemVt[0x10 / 4] = reinterpret_cast<void *>(V_MaxAmount);
    g_portItemVt[Slot(&C_Item::GetAmount) / 4] = reinterpret_cast<void *>(V_Amount);
    g_portItemVt[Slot(&C_Item::GetMaxAmount) / 4] = reinterpret_cast<void *>(V_MaxAmount);
    g_origDefVt[0x10 / 4] = reinterpret_cast<void *>(V_Behaviour);
    g_origDefVt[0x18 / 4] = reinterpret_cast<void *>(V_AmmoId);
    g_portDefVt[Slot(&C_ItemDef::GetBehaviour) / 4] = reinterpret_cast<void *>(V_Behaviour);
    g_portDefVt[Slot(&C_ItemDef::GetAmmoItemId) / 4] = reinterpret_cast<void *>(V_AmmoId);
    g_origCompVt[0x14 / 4] = reinterpret_cast<void *>(V_Velocity);
    g_portCompVt[Slot(&C_WeaponComp::GetMuzzleVelocity) / 4] = reinterpret_cast<void *>(V_Velocity);
    // The rank names (the original's table 0x008ae960, 0x10 bytes each: +0 the text).
    for (int k = 0; k < 8; ++k) {
        g_rankNames[k].textId = 0x27A6 + k;
        Put<int>(OriginalAt<uint8_t *>(0x008ae960), 0x10 * k, 0x27A6 + k);
    }
    // (The game's campaign mission index (+0x3C) under 14: the original's copy protection in DragSlot not reached.)
    Put<int>(g_game, 0x3C, 3);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x005a8320, reinterpret_cast<const void *>(O_FullName)},
        {0x005a8380, reinterpret_cast<const void *>(O_Profile24)},
        {0x005a6380, reinterpret_cast<const void *>(O_Unconscious)},
        {0x005a5ff0, reinterpret_cast<const void *>(O_HealthPercent)},
        {0x005a6600, reinterpret_cast<const void *>(O_Stat)},
        {0x005a6530, reinterpret_cast<const void *>(O_StatFactor)},
        {0x005a80e0, reinterpret_cast<const void *>(O_ViewDir)},
        {0x005a2b40, reinterpret_cast<const void *>(O_MoveItem)},
        {0x005a2bb0, reinterpret_cast<const void *>(O_TransferItem)},
        {0x005a2c50, reinterpret_cast<const void *>(O_ThrowItem)},
        {0x007dfab0, reinterpret_cast<const void *>(O_Image)},
        {0x004ab990, reinterpret_cast<const void *>(O_CursorPos)},
        {0x004abdd0, reinterpret_cast<const void *>(O_Hovered)},
        {0x004ab960, reinterpret_cast<const void *>(O_GetMove)},
        {0x004abc00, reinterpret_cast<const void *>(O_InRect)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
        {0x004a89e0, reinterpret_cast<const void *>(O_Texture)},
        {0x004a8a90, reinterpret_cast<const void *>(O_Material)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x0063df60, reinterpret_cast<const void *>(O_FontsRelease)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    // The screens, built once (their constructors' calls not logged).
    g_orig = static_cast<uint8_t *>(std::calloc(1, 0x2E20));
    OriginalAt<T_Ctor>(0x0049fef0)(g_orig);
    g_origPristine = static_cast<uint8_t *>(std::malloc(0x2E20));
    std::memcpy(g_origPristine, g_orig, 0x2E20);
    g_port = new (std::calloc(1, sizeof(C_InventoryScreen))) C_InventoryScreen();
    g_portPristine = static_cast<uint8_t *>(std::malloc(sizeof(C_InventoryScreen)));
    std::memcpy(g_portPristine, static_cast<void *>(g_port), sizeof(C_InventoryScreen));
    g_log.clear();
    g_boxes.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("inventory_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_INVENTORY_QUICK") ? 3000 : 8000;
    if (const char *r = std::getenv("HD2_INVENTORY_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_INVENTORY_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Inventory", TestInventory}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("inventory_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
