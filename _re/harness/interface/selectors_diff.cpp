// Human part 68: the command map's queue of orders, the maps' cursor and the weapon selector (game/interface_selectors.cpp:
// C_InfoSelector, C_MapCursor, C_WeaponSelector) against the original's (0x004ab230..0x004ad250). MinGW;
// build_selectors.bat; runs selectors_diff.exe
// ("selectors_diff: N checks, 0 failures"). interface_selectors.cpp is compiled into this file; the kit
// (interface_kit.inc, interface_gfx.inc) fakes the frames, the scene, the graph, the text boxes and the graphics. A case per
// class: scenarios of its operations (Init, Update, SelectOrder ...; SetMode, HandleInput, GetHoveredId ...; between them
// the world changing) run from one start on each side (the same random words first), every call out logged by the same
// fakes, the fields snapped after each operation, the frames and the graphics compared at the end.
// Faked on both sides: the humans' orders (HaveOrdersChanged 0x005a5ed0, GetOrderCount 0x005a5f00, GetOrder 0x005a5f50;
// the orders raw blocks with kind, arg, the human and the selected flag at both sides' offsets), the scene's PickFrame
// (0xFC) and TransformPoints (0x98), the graph's screen; for the weapon selector the driver's CreateVisual (a pool of
// visuals with an "i_frehnd.bmp" face group, released ones reused), the team's selected member and active player, the
// humans' containers (per-side vtables: GetItem, GetUsedSlotCount, GetSlots, IsLocked), items (GetAmount, GetCharges) and
// definitions (GetBehaviour, GetAmmoItemId, the medkit's capacity), IsPlayerHoldingBreath, HasOrder, CountAmmo,
// CountItems, RequestMoveItem, RequestTransferItem, IssueCommand, the image textures.
// HD2_SELECTORS_ROUNDS: scenarios per case (default 20,000; HD2_SELECTORS_QUICK: 5,000); HD2_SELECTORS_CASE: one case
// only; HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW, HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/interface_selectors.cpp"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#define INTERFACE_FRAMES 160
#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

// ---- the scene's frames: f0..f9 "info00..09", f10 "infosel", f11..f24 the cursor's shapes, f25 "select_weapon", f26
// "selector_weapon", f27 "drkr1", f28 "select_weapon00", f29 "tabl03", f30 "drkr2", f31 "select_weapon01", f32
// "select_weapon02", f33 the templates' parent; f40.. the entries' visuals and shades (CreateVisual) ----
const char *const SHAPES[] = {"kurzor", "k_move", "k_rotate", "t_rotleft", "t_rotright", "t_rotup", "t_rotdown",
                              "t_left", "t_right", "t_forward", "t_backward", "t_up", "t_down", "k_movepos"};
I3D_frame *FindFrame(const char *name)
{
    int n;
    if (std::sscanf(name, "info%02d", &n) == 1 && n >= 0 && n < 10 && std::strlen(name) == 6)
        return FrameAt(n);
    if (std::strcmp(name, "infosel") == 0)
        return FrameAt(10);
    for (int k = 0; k < 14; ++k)
        if (std::strcmp(name, SHAPES[k]) == 0)
            return FrameAt(11 + k);
    const char *const others[] = {"select_weapon", "selector_weapon", "", "", "tabl03", "drkr2"};
    for (int k = 0; k < 6; ++k)
        if (*others[k] && std::strcmp(name, others[k]) == 0)
            return FrameAt(25 + k);
    return nullptr;
}
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    if (FrameIndex(f) != 25)
        return nullptr;
    const char *const children[] = {"drkr1", "select_weapon00", "", "", "select_weapon01", "select_weapon02"};
    for (int k = 0; k < 6; ++k)
        if (*children[k] && std::strcmp(name, children[k]) == 0)
            return FrameAt(27 + k);
    return nullptr;
}
constexpr int VISUAL_FIRST = 40;
int g_nextVisual;
std::vector<int> g_freeVisuals;   // released visuals, reused last first (the same on both sides)
I3D_frame *CreateVisual(uint32_t)
{
    if (!g_freeVisuals.empty()) {
        const int f = g_freeVisuals.back();
        g_freeVisuals.pop_back();
        return FrameAt(f);
    }
    return FrameAt(g_nextVisual < FRAMES ? g_nextVisual++ : FRAMES - 1);
}

// ---- the humans' orders ----
constexpr int HUMANS = 3, ORDERS = 16;
alignas(16) uint8_t g_humans[HUMANS][0x800];
// (Each side its own orders, the same content: the port's S_HumanOrder has no vtable word and is laid out its own way.)
alignas(16) uint8_t g_orders[2][ORDERS][0x140], g_ordersStart[2][ORDERS][0x140];
int g_side;   // the side running: whose orders GetOrder answers
struct S_Queue {
    int count;
    int order[14];   // the queue's orders (indices)
    bool changed;
};
S_Queue g_queues[HUMANS], g_queuesStart[HUMANS];
int HumanIndex(const void *h)
{
    for (int i = 0; i < HUMANS; ++i)
        if (h == g_humans[i])
            return i;
    return -1;
}
int OrderIndex(const void *o)
{
    for (int side = 0; side < 2; ++side)
        for (int i = 0; i < ORDERS; ++i)
            if (o == g_orders[side][i])
                return i;
    return -1;
}
C_Human *HumanAt(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_humans[i]); }
bool OrdersChanged(const void *h)
{
    Log("h%d HaveOrdersChanged", HumanIndex(h));
    return g_queues[HumanIndex(h)].changed;
}
int OrderCount(const void *h)
{
    Log("h%d GetOrderCount", HumanIndex(h));
    return g_queues[HumanIndex(h)].count;
}
void *Order(const void *h, int index)
{
    Log("h%d GetOrder %d", HumanIndex(h), index);
    const S_Queue &q = g_queues[HumanIndex(h)];
    return index >= 0 && index < 14 ? g_orders[g_side][q.order[index]] : nullptr;
}
bool __attribute__((thiscall)) O_OrdersChanged(void *h) { return OrdersChanged(h); }
int __attribute__((thiscall)) O_OrderCount(void *h) { return OrderCount(h); }
void *__attribute__((thiscall)) O_Order(void *h, int index) { return Order(h, index); }

// ---- the weapon selector's world ----
constexpr int ITEMS = 8, DEFS = 4;
alignas(16) uint8_t g_items[ITEMS][0x40], g_defs[DEFS][0x100], g_components[DEFS][0x40];
struct S_WeaponWorld {
    int selected, active;          // the team's selected member, the active player (humans)
    int hands, longGun;            // the hands' and the long gun's items (-1: none)
    int belt[6], beltSizes[6], beltUsed;
    bool locked, holdingBreath, hasOrder, inventoryChanged;
    int health;                    // the selected member's (IsAlive)
    void *carried;                 // the selected member's carried body
    float amount[ITEMS], charges[ITEMS];
    int kind[ITEMS], def[ITEMS], typeId[ITEMS], uid[ITEMS];
    int behaviour[DEFS], ammoId[DEFS], image[DEFS];
    float capacity[DEFS];
    int ammo, count;
};
S_WeaponWorld g_ww, g_wwStart;
void *g_itemVt[2][16], *g_defVt[2][16], *g_containerVt[2][32];
uint8_t g_containers[3][0x20];   // hands, long gun, belt (each side's vtable)
void *g_containerTable[8];       // the humans' m_containers
// The belt's slots: the original's an array of MSVC vectors {allocator, first, last, end}, the port's of std::vectors.
uint8_t g_origSlots[6][16];
void *g_origSlotPointers[6];   // (the original's GetSlots: an array of pointers to its slots' vectors)
std::vector<C_Item *> g_portSlots[6];
void *g_slotItems[6][4];
int ItemIndex(const void *p)
{
    for (int i = 0; i < ITEMS; ++i)
        if (p == g_items[i])
            return i;
    return -1;
}
int DefIndex(const void *p)
{
    for (int i = 0; i < DEFS; ++i)
        if (p == g_defs[i])
            return i;
    return -1;
}
int ContainerIndex(const void *p)
{
    for (int i = 0; i < 3; ++i)
        if (p == g_containers[i])
            return i;
    return -1;
}
void *ItemAt(int i) { return i < 0 ? nullptr : g_items[i]; }
void *__attribute__((thiscall)) V_GetItem(void *c)
{
    Log("c%d GetItem", ContainerIndex(c));
    return ItemAt(ContainerIndex(c) == 0 ? g_ww.hands : ContainerIndex(c) == 1 ? g_ww.longGun : -1);
}
int __attribute__((thiscall)) V_UsedSlots(void *c)
{
    Log("c%d GetUsedSlotCount", ContainerIndex(c));
    return g_ww.beltUsed;
}
void *__attribute__((thiscall)) V_SlotsOrig(void *c)
{
    Log("c%d GetSlots", ContainerIndex(c));
    return g_origSlotPointers;
}
void *__attribute__((thiscall)) V_SlotsPort(void *c)
{
    Log("c%d GetSlots", ContainerIndex(c));
    return g_portSlots;
}
bool __attribute__((thiscall)) V_IsLocked(void *c)
{
    Log("c%d IsLocked", ContainerIndex(c));
    return g_ww.locked;
}
float __attribute__((thiscall)) V_Amount(void *item)
{
    Log("item%d GetAmount", ItemIndex(item));
    return g_ww.amount[ItemIndex(item)];
}
float __attribute__((thiscall)) V_Charges(void *item)
{
    Log("item%d GetCharges", ItemIndex(item));
    return g_ww.charges[ItemIndex(item)];
}
int __attribute__((thiscall)) V_Behaviour(void *def)
{
    Log("def%d GetBehaviour", DefIndex(def));
    return g_ww.behaviour[DefIndex(def)];
}
int __attribute__((thiscall)) V_AmmoId(void *def)
{
    Log("def%d GetAmmoItemId", DefIndex(def));
    return g_ww.ammoId[DefIndex(def)];
}
void __attribute__((thiscall)) V_Other(void *p) { Log("%s other slot", Name(p).c_str()); }
void *DefImage(const void *def)
{
    Log("def%d GetImage", DefIndex(def));
    const int image = g_ww.image[DefIndex(def)];
    return image < 0 ? nullptr : g_gfx.textures[image];
}
void *__attribute__((thiscall)) O_DefImage(void *def) { return DefImage(def); }
const char *HumanTag(const void *h) { return HumanIndex(h) == 0 ? "h0" : HumanIndex(h) == 1 ? "h1" : HumanIndex(h) == 2 ? "h2" : "h?"; }
void *SelectedMember()
{
    Log("team GetSelectedMember");
    return HumanAt(g_ww.selected);
}
void *ActivePlayer()
{
    Log("team GetActivePlayer");
    return HumanAt(g_ww.active);
}
bool HoldingBreath(const void *h)
{
    Log("%s IsPlayerHoldingBreath", HumanTag(h));
    return g_ww.holdingBreath;
}
bool HasOrder(const void *h)
{
    Log("%s HasOrder", HumanTag(h));
    return g_ww.hasOrder;
}
int CountAmmo(const void *h, int type, bool backpack)
{
    Log("%s CountAmmo %d %d", HumanTag(h), type, backpack);
    return g_ww.ammo;
}
int CountItems(const void *h, int type)
{
    Log("%s CountItems %d", HumanTag(h), type);
    return g_ww.count;
}
bool MoveItem(const void *h, const void *item, const void *other, int from, int to, int slot)
{
    Log("%s RequestMoveItem %s %s %d %d %d", HumanTag(h), Name(item).c_str(), Name(other).c_str(), from, to, slot);
    return true;
}
bool TransferItem(const void *h, const void *item, int arg, const void *other, const void *otherItem, int container)
{
    Log("%s RequestTransferItem %s %d %s %s %d", HumanTag(h), Name(item).c_str(), arg, Name(other).c_str(), Name(otherItem).c_str(), container);
    return true;
}
void Command(const void *h, int category, int type, int param1, int param2)
{
    Log("%s IssueCommand %d %x %d %d", HumanTag(h), category, type, param1, param2);
}
void *__attribute__((thiscall)) O_SelectedMember(void *) { return SelectedMember(); }
void *__attribute__((thiscall)) O_ActivePlayer(void *) { return ActivePlayer(); }
bool __attribute__((thiscall)) O_HoldingBreath(void *h) { return HoldingBreath(h); }
bool __attribute__((thiscall)) O_HasOrder(void *h) { return HasOrder(h); }
int __attribute__((thiscall)) O_CountAmmo(void *h, int type, bool backpack) { return CountAmmo(h, type, backpack); }
int __attribute__((thiscall)) O_CountItems(void *h, int type) { return CountItems(h, type); }
bool __attribute__((thiscall)) O_MoveItem(void *h, void *item, void *other, int from, int to, int slot) { return MoveItem(h, item, other, from, to, slot); }
bool __attribute__((thiscall)) O_TransferItem(void *h, void *item, int arg, void *other, void *otherItem, int container)
{
    return TransferItem(h, item, arg, other, otherItem, container);
}
void __attribute__((thiscall)) O_IssueCommand(void *h, const uint8_t *command)
{
    int f[4];
    std::memcpy(f, command + 4, 16);
    Command(h, f[0], f[1], f[2], f[3]);
}
// A visual's Release (logged as the kit's): a created one goes back to the pool, its frame and face groups as they started.
uint32_t __stdcall F_ReleaseVisual(void *f)
{
    Log("%s Release", Name(f).c_str());
    const int i = FrameIndex(f);
    if (i >= VISUAL_FIRST) {
        const float *scale = reinterpret_cast<const float *>(g_arena.frames[i] + 0xD0);
        Log("%s scale %s %s %s", Name(f).c_str(), FloatText(scale[0]).c_str(), FloatText(scale[1]).c_str(), FloatText(scale[2]).c_str());
    }
    if (i >= VISUAL_FIRST) {
        std::memcpy(g_arena.frames[i], g_arenaStart.frames[i], FRAME_BYTES);
        std::memcpy(g_gfx.groups[i], g_gfxStart.groups[i], sizeof g_gfx.groups[i]);
        g_freeVisuals.push_back(i);
    }
    return 0;
}
// The original's entries' boxes are destroyed inline: its FreeLayout logs the box's end, the font manager's Release silent.
void __attribute__((thiscall)) O_FreeLayout(void *box) { BoxDelete(box); }
void __attribute__((thiscall)) O_FontsRelease(void *) {}
void SetSide(int side)
{
    for (auto &item : g_items)
        *reinterpret_cast<void ***>(item) = g_itemVt[side];
    for (auto &def : g_defs)
        *reinterpret_cast<void ***>(def) = g_defVt[side];
    for (auto &c : g_containers)
        *reinterpret_cast<void ***>(c) = g_containerVt[side];
}

// ---- the scene's PickFrame and TransformPoints ----
int g_pick, g_pickStart;            // a frame or -1
S_vector4 g_transform, g_transformStart;   // TransformPoints' answer
void *__stdcall SC_PickFrame(void *, const S_vector4 &from, const S_vector &dir, uint32_t flags, int a, int b, int c, int d, int e, int f)
{
    Log("scene PickFrame %s %s %s %s %s %x %d %d %d %d %d %d", FloatText(from.x).c_str(), FloatText(from.y).c_str(), FloatText(from.z).c_str(),
        FloatText(from.w).c_str(), VecText(dir).c_str(), flags, a, b, c, d, e, f);
    return g_pick < 0 ? nullptr : FrameAt(g_pick);
}
void __stdcall SC_TransformPoints(void *, const S_vector4 *points, S_vector4 *out, int count)
{
    Log("scene TransformPoints %s %s %s %s %d", FloatText(points->x).c_str(), FloatText(points->y).c_str(), FloatText(points->z).c_str(),
        FloatText(points->w).c_str(), count);
    *out = g_transform;
}
int g_scrnX = 1024, g_scrnY = 768;
int __stdcall G_ScrnX2(void *)
{
    Log("graph ScrnX");
    return g_scrnX;
}
int __stdcall G_ScrnY2(void *)
{
    Log("graph ScrnY");
    return g_scrnY;
}

std::string PartName(const void *p)
{
    const std::string g = GfxName(p);
    if (!g.empty())
        return g;
    if (HumanIndex(p) >= 0)
        return "h" + std::to_string(HumanIndex(p));
    if (OrderIndex(p) >= 0)
        return "o" + std::to_string(OrderIndex(p));
    if (ItemIndex(p) >= 0)
        return "item" + std::to_string(ItemIndex(p));
    if (DefIndex(p) >= 0)
        return "def" + std::to_string(DefIndex(p));
    if (ContainerIndex(p) >= 0)
        return "c" + std::to_string(ContainerIndex(p));
    for (const void *b : g_boxes)
        if (b && p == b)
            return BoxName(p);
    return "";
}

}  // namespace H

bool C_Human::HaveOrdersChanged() const { return H::OrdersChanged(this); }
int C_Human::GetOrderCount() const { return H::OrderCount(this); }
S_HumanOrder *C_Human::GetOrder(int index) const { return static_cast<S_HumanOrder *>(H::Order(this, index)); }
constexpr uint32_t P_HUMAN = offsetof(S_HumanOrder, human), P_KIND = offsetof(S_HumanOrder, kind), P_ARG = offsetof(S_HumanOrder, arg),
                   P_SELECTED = offsetof(S_HumanOrder, selected);
static_assert(sizeof(S_HumanOrder) <= 0x140, "the order blocks");
// (The texture helper is interface_status.cpp's: the original's on both sides, tested in part 66a.)
I3D_material *DuplicateMaterial(I3D_frame *visual, const char *fileName)
{
    using T = I3D_material *(__attribute__((fastcall)) *)(I3D_frame *, const char *);
    return OriginalAt<T>(0x004a8a90)(visual, fileName);
}
IImage *C_ItemDef::GetImage() { return static_cast<IImage *>(H::DefImage(this)); }
C_Human *C_Team::GetActivePlayer() const { return static_cast<C_Human *>(H::ActivePlayer()); }
C_Human *C_Team::GetSelectedMember() const { return static_cast<C_Human *>(H::SelectedMember()); }
int C_Human::CountAmmo(int type, bool backpack) const { return H::CountAmmo(this, type, backpack); }
int C_Human::CountItems(int type) const { return H::CountItems(this, type); }
bool C_Human::IsPlayerHoldingBreath() const { return H::HoldingBreath(this); }
bool C_Human::HasOrder() const { return H::HasOrder(this); }
bool C_Human::RequestMoveItem(C_Item *item, C_Actor *other, int from, int to, int slot) { return H::MoveItem(this, item, other, from, to, slot); }
bool C_Human::RequestTransferItem(C_Item *item, int arg, C_Human *other, C_Item *otherItem, int container)
{
    return H::TransferItem(this, item, arg, other, otherItem, container);
}
void C_Human::IssueCommand(const S_HumanCommand &c) { H::Command(this, c.category, c.type, c.param1, c.param2); }
constexpr uint32_t P_CONTAINERS = offsetof(C_Actor, m_containers), P_CARRIED = offsetof(C_Human, m_carried),
                   P_INVENTORY = offsetof(C_Human, m_inventoryChanged), P_HEALTH = offsetof(C_Human, m_stats);
static_assert(offsetof(C_Item, m_def) == 4 && offsetof(C_Item, m_kind) == 8 && offsetof(C_Item, m_typeId) == 0xC && offsetof(C_Item, m_uid) == 0x10 &&
                  offsetof(C_ItemDef, m_component) == 0x48 && offsetof(C_MedkitComp, m_healingCapacity) == 0xC,
              "the items as the original's");

namespace H {

template <class T> T At(const uint8_t *b, uint32_t o)
{
    T v;
    std::memcpy(&v, b + o, sizeof v);
    return v;
}
template <class T> void Both(uint8_t *block, uint32_t orig, uint32_t port, T value)
{
    std::memcpy(block + orig, &value, sizeof value);
    std::memcpy(block + port, &value, sizeof value);
}
// The weapon selector's world to memory (both sides' layouts).
void SyncWeapons()
{
    for (int h = 0; h < HUMANS; ++h) {
        Both(g_humans[h], 0x74, P_CONTAINERS, static_cast<void *>(g_containerTable));
        Both(g_humans[h], 0x174, P_CARRIED, h == g_ww.selected ? g_ww.carried : static_cast<void *>(nullptr));
        Both(g_humans[h], 0x178, P_INVENTORY, h == g_ww.selected && g_ww.inventoryChanged);
        Both(g_humans[h], 0xEC, P_HEALTH, h == g_ww.selected ? g_ww.health : 100);
    }
    for (int i = 0; i < ITEMS; ++i) {
        std::memcpy(g_items[i] + 4, &g_defs[g_ww.def[i]], 4);
        void *def = g_defs[g_ww.def[i]];
        std::memcpy(g_items[i] + 4, &def, 4);
        std::memcpy(g_items[i] + 8, &g_ww.kind[i], 4);
        std::memcpy(g_items[i] + 0xC, &g_ww.typeId[i], 4);
        std::memcpy(g_items[i] + 0x10, &g_ww.uid[i], 4);
    }
    for (int d = 0; d < DEFS; ++d) {
        void *component = g_components[d];
        std::memcpy(g_defs[d] + 0x48, &component, 4);
        std::memcpy(g_components[d] + 0xC, &g_ww.capacity[d], 4);
    }
    for (int k = 0; k < 6; ++k) {
        g_portSlots[k].clear();
        for (int j = 0; j < g_ww.beltSizes[k]; ++j) {
            g_slotItems[k][j] = g_items[(g_ww.belt[k] + j) % ITEMS];
            g_portSlots[k].push_back(static_cast<C_Item *>(g_slotItems[k][j]));
        }
        void *first = g_slotItems[k], *last = g_slotItems[k] + g_ww.beltSizes[k];
        std::memcpy(g_origSlots[k] + 4, &first, 4);
        std::memcpy(g_origSlots[k] + 8, &last, 4);
        std::memcpy(g_origSlots[k] + 12, &last, 4);
        g_origSlotPointers[k] = g_origSlots[k];
    }
}
bool Selected(int side, int o) { return side ? g_orders[1][o][P_SELECTED] != 0 : g_orders[0][o][0x4A] != 0; }

// ---- a case's scenario ----
struct S_Op {
    int kind;
    int a = 0, b = 0;
    float f[8] = {};
};
struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Gfx gfx;
};
S_Run g_runs[2];
uint32_t g_seed;
std::vector<uint32_t> g_startWords;
I3D_texture *g_stateIcons[24], *g_actionIcons[24];
float g_clip[2][8];   // the clip rectangles (x0, y0 .. [4], [5])
enum { W_QUEUE = 100, W_ORDER, W_PICK, W_TRANSFORM, W_SCREEN, W_WEAPONS, W_WEAPON_STATE };
void RandomWeapons()
{
    g_ww.selected = P(0.9) ? I(0, HUMANS - 1) : -1;
    g_ww.active = I(0, HUMANS - 1);
    g_ww.hands = P(0.3) ? -1 : I(0, ITEMS - 1);
    g_ww.longGun = P(0.4) ? -1 : I(0, ITEMS - 1);
    g_ww.beltUsed = P(0.2) ? 0 : I(1, 6);
    for (int k = 0; k < 6; ++k) {
        g_ww.belt[k] = I(0, ITEMS - 1);
        g_ww.beltSizes[k] = I(1, 4);
    }
    g_ww.locked = P(0.2);
    g_ww.holdingBreath = P(0.15);
    g_ww.hasOrder = P(0.15);
    g_ww.inventoryChanged = P(0.2);
    g_ww.health = P(0.85) ? I(1, 100) : Pick({0, -3});
    g_ww.carried = P(0.15) ? static_cast<void *>(g_humans[0]) : nullptr;
    for (int i = 0; i < ITEMS; ++i) {
        g_ww.amount[i] = P(0.2) ? Pick({0.0f, -1.0f, 1e10f, NAN, INFINITY, 2147483648.0f}) : float(I(0, 200));
        g_ww.charges[i] = P(0.2) ? Pick({0.0f, 50.0f, -1.0f, NAN}) : F(0, 300);
        g_ww.kind[i] = P(0.6) ? 0 : I(1, 6);
        g_ww.def[i] = I(0, DEFS - 1);
        g_ww.typeId[i] = I(0, 300);
        g_ww.uid[i] = I(1, 9999);
    }
    for (int d = 0; d < DEFS; ++d) {
        g_ww.behaviour[d] = P(0.7) ? Pick({int(ITEM_BEHAVIOUR_MELEE), 1, int(ITEM_BEHAVIOUR_LAUNCHER), int(ITEM_BEHAVIOUR_THROWN), int(ITEM_BEHAVIOUR_MINE),
                                           int(ITEM_BEHAVIOUR_STICKY_CHARGE), int(ITEM_BEHAVIOUR_MEDKIT)})
                                  : I(0, 14);
        g_ww.ammoId[d] = I(-1, 300);
        g_ww.image[d] = P(0.2) ? -1 : I(0, TEXTURES - 1);
        g_ww.capacity[d] = P(0.2) ? Pick({0.0f, 100.0f, 1e-30f}) : F(10, 500);
    }
    g_ww.ammo = I(-1, 500);
    g_ww.count = I(0, 9);
}
// (The original's flaws left out of the comparison: a queue of more than 10 orders runs it past its icons, and an order
// without an icon of its own (kind 5, unknown kinds) shows the previous order's, for the first one an unset stack word.
// So at most 10 orders, the first from o0..o7, whose kinds have icons; TestQueueFixes runs the port alone on the rest.)
bool g_portOnly;
void RandomQueue(int h)
{
    S_Queue &q = g_queues[h];
    q.count = P(0.1) ? Pick({0, 9, 10}) : I(0, 8);
    if (g_portOnly && P(0.6))
        q.count = I(11, 14);
    for (int &o : q.order)
        o = I(0, ORDERS - 1);
    if (!g_portOnly)
        q.order[0] = I(0, 7);
    q.changed = P(0.4);
}
void SetOrder(int o, void *human, int kind, int arg, bool selected)
{
    for (auto &side : g_orders)
        std::memset(side[o], 0, sizeof side[o]);
    std::memcpy(g_orders[0][o] + 0x08, &human, 4);
    std::memcpy(g_orders[0][o] + 0x0C, &kind, 4);
    std::memcpy(g_orders[0][o] + 0x10, &arg, 4);
    g_orders[0][o][0x4A] = selected;
    std::memcpy(g_orders[1][o] + P_HUMAN, &human, 4);
    std::memcpy(g_orders[1][o] + P_KIND, &kind, 4);
    std::memcpy(g_orders[1][o] + P_ARG, &arg, 4);
    g_orders[1][o][P_SELECTED] = selected;
}
void RandomOrder(int o)
{
    void *const human = HumanAt(I(0, HUMANS - 1));
    const int kind = o < 8 || P(0.8) ? Pick({1, 2, 3, 4, 6, 7}) : I(0, 9);
    const int arg = I(0, 23);
    SetOrder(o, human, kind, arg, P(0.3));
}
void StartScenario()
{
    g_seed = U32();
    g_startWords.resize(0x40);
    for (uint32_t &w : g_startWords)
        w = P(0.3) ? Pick({0u, 0xFFFFFFFFu, 1u, 0x3F000000u}) : U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    for (int i = 0; i < 10; ++i)   // (the icons' first face groups have materials: Init duplicates them)
        if (!*reinterpret_cast<void **>(g_gfx.groups[i][0] + 4))
            *reinterpret_cast<void **>(g_gfx.groups[i][0] + 4) = g_gfx.materials[i % 8];
    for (int h = 0; h < HUMANS; ++h)
        RandomQueue(h);
    for (int o = 0; o < ORDERS; ++o)
        RandomOrder(o);
    for (int k = 0; k < 24; ++k) {
        g_stateIcons[k] = reinterpret_cast<I3D_texture *>(g_gfx.textures[k]);
        g_actionIcons[k] = reinterpret_cast<I3D_texture *>(g_gfx.textures[23 - k]);
    }
    g_pick = P(0.3) ? -1 : I(0, FRAMES - 1);
    for (int i = 0; i < FRAMES; ++i)
        FrameAt(i)->m_data = reinterpret_cast<S_FrameData *>(intptr_t(P(0.5) ? I(100, 105) : I(-5, 1000)));
    g_transform = {F(-200, 1400), F(-200, 1000), F(0, 1), F(0, 2)};
    RandomWeapons();
    for (int i = 0; i < 3; ++i)
        std::memset(g_containers[i] + 4, 0, sizeof g_containers[i] - 4);
    for (int k = 0; k < 8; ++k)   // (the hands 2, the long gun 3, the belt 1: ITEM_CONTAINER_*)
        g_containerTable[k] = k == ITEM_CONTAINER_HANDS ? g_containers[0] : k == ITEM_CONTAINER_LONG_GUN ? g_containers[1]
                            : k == ITEM_CONTAINER_BELT  ? g_containers[2]
                                                         : nullptr;
    // (The created visuals (copies of the entry's template) have an "i_frehnd.bmp" face group, as the game's template:
    // the entries' constructor uses its material unchecked.)
    std::strcpy(g_textureNames[23], "i_frehnd.bmp");
    g_logMargins = true;   // (the entries' boxes' margins: Init sets them)
    g_materialPoolEnd = 48;   // (m48..m55 the visuals' own: the duplicates never overwrite them)
    *reinterpret_cast<const char **>(g_gfx.textures[23] + 0x18) = g_textureNames[23];
    for (int f = VISUAL_FIRST; f < FRAMES; ++f) {
        const int count = *reinterpret_cast<int *>(g_gfx.levels[f] + 0x58) = I(1, GROUPS);
        const int g = I(0, count - 1);
        void *material = g_gfx.materials[48 + f % 8];
        *reinterpret_cast<void **>(g_gfx.groups[f][g] + 4) = material;
        *reinterpret_cast<void **>(static_cast<uint8_t *>(material) + 0xC) = g_gfx.textures[23];
    }
    // (The templates' parent: f33.)
    for (int k = 25; k <= 32; ++k)
        FrameAt(k)->m_parent = FrameAt(33);
    for (auto &c : g_clip) {
        c[0] = F(0, 0.5f);
        c[1] = F(-0.75f, -0.3f);
        c[4] = c[0] + F(0.01f, 0.5f);
        c[5] = c[1] + F(0.01f, 0.4f);
        c[2] = c[3] = c[6] = c[7] = F(-1, 1);
    }
}
void SaveStart()
{
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    std::memcpy(g_ordersStart, g_orders, sizeof g_orders);
    std::memcpy(g_queuesStart, g_queues, sizeof g_queues);
    g_pickStart = g_pick;
    g_transformStart = g_transform;
    g_wwStart = g_ww;
}
void ApplyWorld(const S_Op &op)
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(op.b));   // (the op's own draws: the same on both sides)
    switch (op.kind) {
    case W_QUEUE:
        RandomQueue(op.a);
        break;
    case W_ORDER:
        RandomOrder(op.a);
        break;
    case W_PICK:
        g_pick = P(0.3) ? -1 : I(0, FRAMES - 1);
        break;
    case W_TRANSFORM:
        g_transform = {P(0.2) ? Pick({0.0f, 1024.0f, -1.0f}) : F(-200, 1400), P(0.2) ? Pick({0.0f, 768.0f}) : F(-200, 1000), F(0, 1), F(0, 2)};
        break;
    case W_SCREEN:
        g_scrnX = Pick({640, 800, 1024, 1600, 0});
        g_scrnY = Pick({480, 600, 768, 1200, 0});
        break;
    case W_WEAPONS:
        RandomWeapons();
        break;
    case W_WEAPON_STATE: {   // (one answer of the selected member's changing)
        const int k = I(0, 6);
        if (k == 0)
            g_ww.inventoryChanged = P(0.5);
        else if (k == 1)
            g_ww.health = P(0.7) ? I(1, 100) : 0;
        else if (k == 2)
            g_ww.hasOrder = P(0.3);
        else if (k == 3)
            g_ww.holdingBreath = P(0.3);
        else if (k == 4)
            g_ww.selected = P(0.8) ? g_ww.selected : I(-1, HUMANS - 1);
        else if (k == 5)
            g_ww.locked = P(0.3);
        else
            g_ww.amount[I(0, ITEMS - 1)] = float(I(0, 200));
        break;
    }
    }
    g_rng = saved;
}
template <class P_T>
void RunCase(const std::vector<S_Op> &ops, size_t origBytes, const std::function<void(int side, P_T *port, uint8_t *orig, const S_Op &op)> &run,
             const std::function<std::string(int side, P_T *port, const uint8_t *orig)> &snap, size_t startBytes = 0x100)
{
    for (int side = 0; side < 2; ++side) {
        std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
        std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
        std::memcpy(g_orders, g_ordersStart, sizeof g_orders);
        std::memcpy(g_queues, g_queuesStart, sizeof g_queues);
        g_side = side;
        g_pick = g_pickStart;
        g_transform = g_transformStart;
        g_ww = g_wwStart;
        g_nextVisual = VISUAL_FIRST;
        g_freeVisuals.clear();
        SetSide(side);
        g_scrnX = 1024;
        g_scrnY = 768;
        g_log.clear();
        g_boxes.clear();
        std::srand(g_seed);
        P_T *port = nullptr;
        uint8_t *orig = nullptr;
        if (side) {
            port = new P_T;
            std::memcpy(static_cast<void *>(port), g_startWords.data(), std::min({sizeof(P_T), g_startWords.size() * 4, startBytes}));
        } else {
            orig = static_cast<uint8_t *>(std::calloc(1, origBytes + 0x40));
            std::memcpy(orig, g_startWords.data(), std::min({origBytes, g_startWords.size() * 4, startBytes}));
        }
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            SyncWeapons();
            if (op.kind >= 100)
                ApplyWorld(op);
            else
                run(side, port, orig, op);
            std::string s = snap(side, port, orig);
            for (int o = 0; o < ORDERS; ++o)
                s += Selected(side, o) ? "1" : "0";
            g_log.push_back(s);
        }
        side ? delete port : std::free(orig);
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        std::memcpy(&g_runs[side].gfx, &g_gfx, sizeof g_gfx);
    }
    for (int i = 0; i < FRAMES; ++i)
        for (S_Run &r : g_runs)
            std::memcpy(r.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ++checks;
    if (std::memcmp(&g_runs[0].gfx, &g_runs[1].gfx, sizeof g_gfx) != 0)
        CHECK(false, "the graphics differ");
    ShowLogs(g_runs[0].log, g_runs[1].log);
}
S_Op WorldOp(std::initializer_list<int> kinds)
{
    const std::vector<int> k(kinds);
    S_Op op{k[I(0, int(k.size()) - 1)]};
    op.a = op.kind == W_QUEUE ? I(0, HUMANS - 1) : I(0, ORDERS - 1);
    op.b = int(U32() & 0x7FFFFFFF);
    return op;
}
std::string Ptrs(std::initializer_list<const void *> p)
{
    std::string s;
    for (const void *q : p)
        s += " " + Name(q);
    return s;
}
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Ptr = void(__attribute__((thiscall)) *)(void *, void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_FastPtr = void *(__attribute__((fastcall)) *)(const void *);
using T_FastInt = int(__attribute__((fastcall)) *)(const void *);

// ---- the queue of orders ----
using T_InfoInit = bool(__attribute__((thiscall)) *)(void *, void *, void *, void *);
void TestInfo()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    for (int n = I(1, 16); n > 0; --n) {
        if (P(0.3)) {
            ops.push_back(WorldOp({W_QUEUE, W_ORDER, W_ORDER}));
            continue;
        }
        S_Op op{Pick({1, 1, 1, 2, 2, 3, 3, 4, 5})};   // Update, SelectOrder, SelectOrderIndex, GetSelectedOrder, GetSelectedIndex
        op.a = op.kind == 1 ? I(0, HUMANS - 1) : op.kind == 2 ? I(0, ORDERS - 1) : I(-2, 15);
        ops.push_back(op);
    }
    // (SelectOrder and the getters need a human shown: an Update first; GetSelectedIndex answers without one.)
    ops.insert(ops.begin() + 1, S_Op{1, I(0, HUMANS - 1)});
    if (P(0.3))
        ops.insert(ops.begin() + 1, S_Op{5});
    SaveStart();
    RunCase<C_InfoSelector>(
        ops, 0x40,
        [](int side, C_InfoSelector *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene), g_stateIcons, g_actionIcons)
                                       : OriginalAt<T_InfoInit>(0x004ab230)(orig, &g_scene, g_stateIcons, g_actionIcons));
                break;
            case 1:
                side ? port->Update(HumanAt(op.a)) : OriginalAt<T_Ptr>(0x004ab2e0)(orig, HumanAt(op.a));
                break;
            case 2:
                side ? port->SelectOrder(reinterpret_cast<S_HumanOrder *>(g_orders[1][op.a])) : OriginalAt<T_Ptr>(0x004ab610)(orig, g_orders[0][op.a]);
                break;
            case 3:
                side ? port->SelectOrderIndex(op.a) : OriginalAt<T_Int>(0x004ab700)(orig, op.a);
                break;
            case 4:
                Log("-> %s", Name(side ? static_cast<void *>(port->GetSelectedOrder()) : OriginalAt<T_FastPtr>(0x004ab7e0)(orig)).c_str());
                break;
            case 5:
                Log("-> %d", side ? port->GetSelectedIndex() : OriginalAt<T_FastInt>(0x004ab820)(orig));
                break;
            }
        },
        [](int side, C_InfoSelector *port, const uint8_t *orig) {
            std::string s = "info " + Name(side ? port->m_human : At<void *>(orig, 0));
            for (int i = 0; i < 10; ++i)
                s += " " + Name(side ? port->m_icons[i] : At<void *>(orig, 4 + i * 4));
            return s + Ptrs({side ? static_cast<const void *>(port->m_stateIcons) : At<void *>(orig, 0x2C),
                             side ? static_cast<const void *>(port->m_actionIcons) : At<void *>(orig, 0x30),
                             side ? port->m_marker : At<void *>(orig, 0x34)}) + " ";
        });
}

// ---- the maps' cursor ----
using T_CursorInit = bool(__attribute__((thiscall)) *)(void *, void *, void *);
using T_Input = void(__attribute__((thiscall)) *)(void *, float *, float *);
using T_GetInt = int(__attribute__((thiscall)) *)(void *);
using T_Move = void(__attribute__((thiscall)) *)(void *, float *);
using T_Screen = void(__attribute__((thiscall)) *)(void *, int *, int *);
using T_Rect = bool(__attribute__((thiscall)) *)(void *, const float *);
using T_World = void(__attribute__((thiscall)) *)(void *, const S_vector4 *);
float Motion() { return P(0.2) ? Pick({0.0f, 1.0f, -1.0f, 0.005f, -0.005f, 0.5f}) : F(-0.3f, 0.3f); }
// A motion landing the cursor on a bound exactly (which: 1..4 the screen's, 5..8 the clip rectangle's margins, 9 / 10 half
// inside the left / top margin), as HandleInput and ClampMotion add it (x + held x, y - held y).
void BoundMotion(int which, const S_vector4 &pos, const float *clip, float *held)
{
    const float margin = 0.002f;
    float x = pos.x + held[INPUT_CURSOR_X], y = pos.y - held[INPUT_CURSOR_Y];
    switch (which) {
    case 1: x = 0.0f; break;
    case 2: y = 0.0f; break;
    case 3: x = 0.995f; break;
    case 4: y = -0.745f; break;
    case 5: x = clip ? clip[0] + margin : x; break;
    case 6: x = clip ? clip[4] - margin : x; break;
    case 7: y = clip ? clip[1] + margin : y; break;
    case 8: y = clip ? clip[5] - margin : y; break;
    case 9: x = clip ? clip[0] + margin * 0.5f : x; break;
    case 10: y = clip ? clip[1] + margin * 0.5f : y; break;
    }
    held[INPUT_CURSOR_X] = x - pos.x;
    held[INPUT_CURSOR_Y] = pos.y - y;
}
void TestCursor()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    for (int n = I(1, 20); n > 0; --n) {
        if (P(0.15)) {
            ops.push_back(WorldOp({W_PICK, W_TRANSFORM, W_SCREEN}));
            continue;
        }
        // SetMode, HandleInput, GetHoveredId, Update, GetMove, GetScreenPos, IsInRect, SetClipRect, SetTargetScale, SetFromWorld,
        // ClampMotion
        S_Op op{Pick({1, 1, 2, 2, 2, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 10, 11})};
        op.a = op.kind == 1 ? (P(0.9) ? I(-1, 15) : -1) : op.kind == 8 ? I(-1, 1) : P(0.6) ? Pick({0, 1, 16, 33, 100, 200, 1000}) : I(-50, 500);
        for (float &f : op.f)
            f = Motion();
        if (op.kind == 7) {
            for (float &f : op.f)
                f = P(0.3) ? Pick({0.0f, 0.5f, -0.375f, 0.995f, -0.745f}) : F(-0.1f, 1.1f);
            if (P(0.4))
                op.b = I(1, 5);   // (around the cursor, op.b - 1: the edge on it; 4: none)
        }
        if (op.kind == 9)
            op.f[0] = P(0.3) ? Pick({1.0f, 1.1f, 0.5f, 2.0f}) : F(0.2f, 3);
        if ((op.kind == 2 || op.kind == 11) && P(0.4))
            op.b = I(1, 10);
        ops.push_back(op);
    }
    // (A shape first: SetFromWorld and Update use the frame unchecked; SetMode(-1) keeps the last one.)
    ops.insert(ops.begin() + 1, S_Op{1, I(0, 15)});
    // (The places' w start at 0 on both sides: the original's Init copies a local vector whose w is a stack word (set to 0
    // after it), the port keeps its member initialiser's 0; GetHoveredId passes it to PickFrame.)
    g_startWords[0x1C / 4] = 0;
    g_startWords[0x2C / 4] = 0;
    SaveStart();
    RunCase<C_MapCursor>(
        ops, 0x80,
        [](int side, C_MapCursor *port, uint8_t *orig, const S_Op &op) {
            float held[64] = {}, pressed[64] = {};
            held[INPUT_CURSOR_X] = op.f[0];
            held[INPUT_CURSOR_Y] = op.f[1];
            if (op.b)
                BoundMotion(op.b, side ? port->m_pos : At<S_vector4>(orig, 0x20), side ? port->m_clip : At<const float *>(orig, 0x78), held);
            switch (op.kind) {
            case 0:
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene), reinterpret_cast<I3D_scene *>(&g_scene))
                                       : OriginalAt<T_CursorInit>(0x004ab860)(orig, &g_scene, &g_scene));
                if (!side) {   // (the original's places' w: a stack word; the port's 0)
                    std::memset(orig + 0x1C, 0, 4);
                    std::memset(orig + 0x2C, 0, 4);
                }
                break;
            case 1:
                side ? port->SetMode(op.a) : OriginalAt<T_Int>(0x004abaf0)(orig, op.a);
                break;
            case 2:
                side ? port->HandleInput(held, pressed) : OriginalAt<T_Input>(0x004abc50)(orig, held, pressed);
                Log("held %s %s", FloatText(held[INPUT_CURSOR_X]).c_str(), FloatText(held[INPUT_CURSOR_Y]).c_str());
                break;
            case 3:
                Log("-> %d", side ? port->GetHoveredId() : OriginalAt<T_GetInt>(0x004abdd0)(orig));
                break;
            case 4:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004abe60)(orig, op.a);
                break;
            case 5: {
                float move[4] = {1234.5f, 1234.5f, 1234.5f, 1234.5f};
                side ? port->GetMove(move) : OriginalAt<T_Move>(0x004ab960)(orig, move);
                Log("move %s %s %s %s", FloatText(move[0]).c_str(), FloatText(move[1]).c_str(), FloatText(move[2]).c_str(),
                    FloatText(move[3]).c_str());
                break;
            }
            case 6: {
                int x, y;
                side ? port->GetScreenPos(&x, &y) : OriginalAt<T_Screen>(0x004ab9a0)(orig, &x, &y);
                Log("screen %d %d", x, y);
                break;
            }
            case 7: {
                float rect[8];
                std::memcpy(rect, op.f, sizeof rect);
                if (op.b) {   // (a rectangle around the side's cursor, one edge on it)
                    const S_vector4 pos = side ? port->m_pos : At<S_vector4>(orig, 0x20);
                    rect[0] = op.b == 1 ? pos.x : pos.x - 0.1f;
                    rect[1] = op.b == 2 ? pos.y : pos.y - 0.1f;
                    rect[4] = op.b == 3 ? pos.x : pos.x + 0.1f;
                    rect[5] = op.b == 4 ? pos.y : pos.y + 0.1f;
                }
                Log("-> %d", side ? port->IsInRect(rect) : OriginalAt<T_Rect>(0x004abc00)(orig, rect));
                break;
            }
            case 8: {   // (inline both: the rectangle's pointer)
                const float *clip = op.a < 0 ? nullptr : g_clip[op.a];
                if (side)
                    port->SetClipRect(clip);
                else
                    std::memcpy(orig + 0x78, &clip, 4);
                break;
            }
            case 9:   // (inline both)
                if (side)
                    port->SetTargetScale(op.f[0]);
                else
                    std::memcpy(orig + 0x74, &op.f[0], 4);
                break;
            case 10: {
                const S_vector4 point = {op.f[2], op.f[3], op.f[4], op.f[5]};
                side ? port->SetFromWorld(point) : OriginalAt<T_World>(0x004abf50)(orig, &point);
                break;
            }
            case 11:
                side ? port->ClampMotion(held, pressed) : OriginalAt<T_Input>(0x004ac090)(orig, held, pressed);
                Log("held %s %s", FloatText(held[INPUT_CURSOR_X]).c_str(), FloatText(held[INPUT_CURSOR_Y]).c_str());
                break;
            }
        },
        [](int side, C_MapCursor *port, const uint8_t *orig) {
            const auto v4 = [](const S_vector4 &v) {
                return FloatText(v.x) + " " + FloatText(v.y) + " " + FloatText(v.z) + " " + FloatText(v.w);
            };
            std::string s = "cursor" + Ptrs({side ? port->m_frame : At<void *>(orig, 0), side ? port->m_scene : At<void *>(orig, 4),
                                              side ? port->m_gameScene : At<void *>(orig, 8)});
            s += " mode " + std::to_string(side ? port->m_mode : At<int>(orig, 0xC));
            s += " prev " + v4(side ? port->m_prevPos : At<S_vector4>(orig, 0x10)) + " pos " + v4(side ? port->m_pos : At<S_vector4>(orig, 0x20));
            for (int i = 0; i < 16; ++i)
                s += " " + Name(side ? port->m_shapes[i] : At<void *>(orig, 0x30 + i * 4));
            const void *clip = side ? static_cast<const void *>(port->m_clip) : At<void *>(orig, 0x78);
            s += " scale " + FloatText(side ? port->m_scale : At<float>(orig, 0x70)) + " " + FloatText(side ? port->m_targetScale : At<float>(orig, 0x74)) +
                 " clip " + (clip == g_clip[0] ? "c0" : clip == g_clip[1] ? "c1" : clip ? "?" : "null") + " ";
            return s;
        });
}

// The port's fixes alone: a queue of more than 10 orders shows 10 icons (the original went on past them), the marker
// never past the last icon; an order without an icon first shows none (the original: a stack word).
void TestQueueFixes()
{
    g_portOnly = true;
    StartScenario();
    g_portOnly = false;
    for (int o = 0; o < ORDERS; ++o) {
        void *human;
        std::memcpy(&human, g_orders[1][o] + P_HUMAN, 4);
        SetOrder(o, human, P(0.5) ? Pick({5, 0, 8, 9}) : Pick({1, 2, 3, 4, 6, 7}), I(0, 23), P(0.3));
    }
    g_side = 1;
    g_log.clear();
    g_boxes.clear();
    C_InfoSelector info{};
    info.Init(reinterpret_cast<I3D_scene *>(&g_scene), g_stateIcons, g_actionIcons);
    g_log.clear();   // (Init's own GetLevel calls: the icons' materials)
    const int h = I(0, HUMANS - 1);
    info.Update(HumanAt(h));
    const int index = I(0, 14);
    info.SelectOrderIndex(index);
    const S_Queue &q = g_queues[h];
    CHECK(((info.m_marker->m_frameFlags & 1) != 0) == (index < std::min(q.count, 10)), "marker at %d of %d orders: %d", index, q.count,
          info.m_marker->m_frameFlags & 1);
    for (int i = 0; i < 10; ++i)
        CHECK(info.m_icons[i] == FrameAt(i), "icon %d %s", i, Name(info.m_icons[i]).c_str());
    for (int i = 0; i < 10; ++i)
        CHECK(((FrameAt(i)->m_frameFlags & 1) != 0) == (i < q.count), "icon %d shown %d of %d orders", i, FrameAt(i)->m_frameFlags & 1, q.count);
    // (No texture before the first order with one.)
    int first = 0;
    const auto kindOf = [&q](int i) { return At<int>(g_orders[1][q.order[i]], P_KIND); };
    while (first < std::min(q.count, 10) && !(kindOf(first) >= 1 && kindOf(first) <= 7 && kindOf(first) != 5))
        ++first;
    for (const std::string &line : g_log)
        for (int i = 0; i < first; ++i)
            CHECK(line.find("mesh" + std::to_string(i) + " GetLevel") == std::string::npos, "icon %d textured: %s", i, line.c_str());
}

// ---- the weapon selector ----
using T_WInit = bool(__attribute__((thiscall)) *)(void *, void *);
using T_WBool = void(__attribute__((thiscall)) *)(void *, bool);
void TestWeapons()
{
    StartScenario();
    int shows = 0;
    const bool holdNext = P(0.3), holdPrev = P(0.3), choose = P(0.3);
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    for (int n = I(1, 20); n > 0; --n) {
        if (P(0.25)) {
            S_Op op = WorldOp({W_WEAPONS, W_WEAPON_STATE, W_WEAPON_STATE, W_WEAPON_STATE});
            ops.push_back(op);
            continue;
        }
        S_Op op{Pick({1, 1, 2, 3, 3, 3, 3, 4, 4, 5})};   // Show, HandleInput, Update, Render, Done
        op.a = op.kind == 1 ? P(0.7) : op.kind == 3 ? (P(0.6) ? Pick({0, 1, 16, 100, 199, 200, 500, 1000, 2000, 2001}) : I(-20, 2500)) : 0;
        if (op.kind == 3 && (holdNext || holdPrev) && P(0.6))   // (the repeats landing on 1 and 0 ms)
            op.a = Pick({1, 199, 199, 200, 100});
        // (Show(true) builds the list again without freeing it, as the original's: at most 4, the visuals' pool.)
        if (op.kind == 1 && op.a && ++shows > 4)
            op.a = 0;
        // HandleInput: the roll menu key (the choice), the next and previous keys held and pressed, the roll menu key held.
        op.f[0] = P(choose ? 0.4 : 0.15);
        op.f[1] = holdNext || P(0.4);
        op.f[2] = holdPrev || P(0.4);
        op.f[3] = P(0.3);
        op.f[4] = P(0.3);
        op.f[5] = op.f[0] != 0.0f || P(0.2);
        ops.push_back(op);
    }
    // (Now and then both keys held, one pressed, then the other, then updates: the second key's repeat reaches 0 while the
    // first's runs, which then counts down; held on, the keys fire again; at last the entry chosen.)
    if (P(0.4)) {
        if (shows < 4)
            ops.push_back(S_Op{1, 1});
        const bool nextFirst = P(0.5);
        const auto input = [&ops](int pressedKey, bool choose) {
            S_Op op{2};
            op.f[1] = op.f[2] = 1.0f;
            if (pressedKey)
                op.f[pressedKey] = 1.0f;
            op.f[0] = op.f[5] = choose;
            ops.push_back(op);
        };
        input(nextFirst ? 3 : 4, false);
        input(nextFirst ? 4 : 3, false);
        for (int dt : {200, 100, 100, 1})
            ops.push_back(S_Op{3, dt});
        input(0, false);
        for (int dt : {200, 200, 1})
            ops.push_back(S_Op{3, dt});
        input(0, false);
        input(0, true);
    }
    ops.push_back(S_Op{5});
    SaveStart();
    RunCase<C_WeaponSelector>(
        ops, 0x3C,
        [](int side, C_WeaponSelector *port, uint8_t *orig, const S_Op &op) {
            float held[64] = {}, pressed[64] = {};
            switch (op.kind) {
            case 0:
                // (The fields Init leaves alone start at the port's member initialisers' values on both sides.)
                if (!side) {
                    const int start[5] = {0, 0, -1, -1, 0};   // the human, the chosen entry, the repeats, the idle time
                    std::memcpy(orig + 0x28, start, sizeof start);
                }
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_WInit>(0x004ac440)(orig, &g_scene));
                break;
            case 1:
                side ? port->Show(op.a != 0) : OriginalAt<T_WBool>(0x004ace20)(orig, op.a != 0);
                break;
            case 2:
                pressed[INPUT_ROLL_MENU] = op.f[0];
                held[INPUT_ROLL_MENU] = op.f[5];
                held[INPUT_WEAPON_NEXT] = op.f[1];
                held[INPUT_WEAPON_PREV] = op.f[2];
                pressed[INPUT_WEAPON_NEXT] = op.f[3];
                pressed[INPUT_WEAPON_PREV] = op.f[4];
                // (The input needs a list: Show first; with none both sides use the human unchecked.)
                if (!(side ? port->m_human : At<void *>(orig, 0x28)))
                    break;
                side ? port->HandleInput(held, pressed) : OriginalAt<T_Input>(0x004acf00)(orig, held, pressed);
                Log("pressed %s %s %s held %s %s %s", FloatText(pressed[INPUT_ROLL_MENU]).c_str(), FloatText(pressed[INPUT_WEAPON_NEXT]).c_str(),
                    FloatText(pressed[INPUT_WEAPON_PREV]).c_str(), FloatText(held[INPUT_ROLL_MENU]).c_str(),
                    FloatText(held[INPUT_WEAPON_NEXT]).c_str(), FloatText(held[INPUT_WEAPON_PREV]).c_str());
                break;
            case 3:
                // (Update closes the list unless the human is the selected one; without one both sides use it unchecked.)
                if (!(side ? port->m_human : At<void *>(orig, 0x28)))
                    break;
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004ad180)(orig, op.a);
                break;
            case 4:
                side ? port->Render() : OriginalAt<T_Void>(0x004ad140)(orig);
                break;
            case 5:
                side ? port->Done() : OriginalAt<T_Void>(0x004acdc0)(orig);
                break;
            }
        },
        [](int side, C_WeaponSelector *port, const uint8_t *orig) {
            std::string s = "weapons" + Ptrs({side ? static_cast<const void *>(port->m_scene) : At<void *>(orig, 0), side ? port->m_frame : At<void *>(orig, 4),
                                               side ? port->m_marker : At<void *>(orig, 8), side ? port->m_entryTemplate : At<void *>(orig, 0xC),
                                               side ? port->m_shadeTemplate : At<void *>(orig, 0x10), side ? port->m_human : At<void *>(orig, 0x28)});
            char text[120];
            std::snprintf(text, sizeof text, " selected %d repeat %d %d idle %d entries", side ? port->m_selected : At<int>(orig, 0x2C),
                          side ? port->m_nextRepeat : At<int>(orig, 0x30), side ? port->m_prevRepeat : At<int>(orig, 0x34),
                          side ? port->m_idleTime : At<int>(orig, 0x38));
            s += text;
            std::vector<const uint8_t *> entries;
            if (side) {
                for (auto *e : port->m_entries)
                    entries.push_back(reinterpret_cast<const uint8_t *>(e));
            } else {
                for (void *const *e = At<void *const *>(orig, 0x1C); e && e < At<void *const *>(orig, 0x20); ++e)
                    entries.push_back(static_cast<const uint8_t *>(*e));
            }
            for (const uint8_t *e : entries)
                s += " [" + Name(At<void *>(e, 0)) + " " + std::to_string(At<int>(e, 4)) + " " + std::to_string(At<int>(e, 8)) + " " +
                     Name(At<void *>(e, 0xC)) + " " + Name(At<void *>(e, 0x10)) + "]";
            return s + " ";
        },
        0x14);   // (the scene and the frames: the vector after them each side's own; the port's at +0x14)
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    g_createVisual = CreateVisual;
    SetUpGfx();
    g_frameVt[0] = reinterpret_cast<void *>(F_ReleaseVisual);
    for (int side = 0; side < 2; ++side) {
        for (void *&v : g_itemVt[side])
            v = reinterpret_cast<void *>(V_Other);
        for (void *&v : g_defVt[side])
            v = reinterpret_cast<void *>(V_Other);
        for (void *&v : g_containerVt[side])
            v = reinterpret_cast<void *>(V_Other);
        g_itemVt[side][0x0C / 4 + side] = reinterpret_cast<void *>(V_Amount);
        g_itemVt[side][0x20 / 4 + side] = reinterpret_cast<void *>(V_Charges);
        g_defVt[side][0x10 / 4 + side] = reinterpret_cast<void *>(V_Behaviour);
        g_defVt[side][0x18 / 4 + side] = reinterpret_cast<void *>(V_AmmoId);
        g_containerVt[side][0x40 / 4 + side] = reinterpret_cast<void *>(V_GetItem);
        g_containerVt[side][0x28 / 4 + side] = reinterpret_cast<void *>(V_UsedSlots);
        g_containerVt[side][0x3C / 4 + side] = reinterpret_cast<void *>(side ? V_SlotsPort : V_SlotsOrig);
        g_containerVt[side][0x48 / 4 + side] = reinterpret_cast<void *>(V_IsLocked);
    }
    g_sceneVt[0xFC / 4] = reinterpret_cast<void *>(SC_PickFrame);
    g_sceneVt[0x98 / 4] = reinterpret_cast<void *>(SC_TransformPoints);
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(G_ScrnX2);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(G_ScrnY2);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x005a5ed0, reinterpret_cast<const void *>(O_OrdersChanged)},
        {0x005a5f00, reinterpret_cast<const void *>(O_OrderCount)},
        {0x005a5f50, reinterpret_cast<const void *>(O_Order)},
        {0x007dfab0, reinterpret_cast<const void *>(O_DefImage)},
        {0x0045d160, reinterpret_cast<const void *>(O_SelectedMember)},
        {0x0045d1d0, reinterpret_cast<const void *>(O_ActivePlayer)},
        {0x005a6430, reinterpret_cast<const void *>(O_HoldingBreath)},
        {0x005a45c0, reinterpret_cast<const void *>(O_HasOrder)},
        {0x005a6c50, reinterpret_cast<const void *>(O_CountAmmo)},
        {0x005a6aa0, reinterpret_cast<const void *>(O_CountItems)},
        {0x005a2b40, reinterpret_cast<const void *>(O_MoveItem)},
        {0x005a2bb0, reinterpret_cast<const void *>(O_TransferItem)},
        {0x005a45b0, reinterpret_cast<const void *>(O_IssueCommand)},
        {0x00644ec0, reinterpret_cast<const void *>(O_FreeLayout)},
        {0x0063df60, reinterpret_cast<const void *>(O_FontsRelease)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("selectors_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_SELECTORS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_SELECTORS_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_SELECTORS_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Info", TestInfo}, {"Cursor", TestCursor}, {"QueueFixes", TestQueueFixes}, {"Weapons", TestWeapons}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("selectors_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
