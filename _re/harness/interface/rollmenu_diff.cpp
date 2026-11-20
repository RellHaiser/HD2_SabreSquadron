// Human part 72: the roll menu (game/interface_rollmenu.cpp: C_RollMenu) against the original's (0x004b0750..0x004b30b0).
// MinGW; build_rollmenu.bat; runs rollmenu_diff.exe ("rollmenu_diff: N checks, 0 failures").
// interface_rollmenu.cpp is compiled into this file; the kit (interface_kit.inc, interface_gfx.inc) fakes the frames, the
// scene, the graph and the text boxes. Scenarios of the menu's operations (Init, Done, SetTarget, HasActions, Open,
// Prepare, HandleInput, Update, Render, Relayout, IsCursorOver, Select, SelectAt, MoveTo and the private Refresh,
// BuildEntries, HasCommandMapActions, SetEntry, Activate, SelectPrevious, SelectNext; between them the world changing)
// run from one start on each side (the same random words first), every call out logged by the same fakes, the menu's
// fields snapped after each operation, the frames compared at the end. Both the crosshair's menu and the command map's.
// The world: 12 objects of any actor type (humans, cars, submarines, static weapons, the Junkers, doors, radios, supply
// boxes, item actors, places, animations ...), each side its own blocks (the port's actors are laid out their own way)
// with the same contents and per-side vtables (GetName, GetSeatOccupant, GetSeatCount, GetSeatText; the port's slots
// from its member pointers). Faked on both sides with the same loggers: the player's team (GetSelectedMember,
// GetCommandedMember, IsMember), the humans (FindItem, CountItems, CountAmmo, FindWireCutter, FindBeltItem,
// GetHandsWeapon (its definition's GetAmmoItemId), GetMedkit, IsSurrendered, IsUnconscious, IsInWater, IsHostileTo,
// GetHealthPercent, GetStanceFlags, GetPlayerViewMode, HasOrder, GetTeam, GetShortName, IssueCommand,
// PlayerSendActionCommand, RequestTakeFromBox, RequestPickUpItem, the body's GetClimbState and IsWorkState5), the cars
// (CanEnter, HasEnemyAboard, IsDestroyed), the radios' CanUse, the profile's IsCooperativeGame, the texts' Get, the actors
// module's GetActorById, the items module's CanPutItem and definitions (the original's table 0x0098b030), the
// interface's ShowInventory, the controls' ClearAction and ClearKey, the graph's GetAsciiKey and KeyToChar, the visuals'
// UpdateWBoundProc, the click sound (a frame), the game's mission time, role and type.
// HD2_ROLLMENU_ROUNDS: scenarios per case (default 40,000; HD2_ROLLMENU_QUICK: 15,000; the fixes' case a twentieth); HD2_ROLLMENU_CASE: one case only;
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
#include "game/interface_rollmenu.cpp"
#include "game/animation_actor.h"
#include "game/car.h"
#include "game/document_place.h"
#include "game/door.h"
#include "game/explosive_place.h"
#include "game/game_texts.h"
#include "game/item_actor.h"
#include "game/radio.h"
#include "game/static_weapon.h"
#include "game/supply_box.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

// ---- the frames: f0 "rollmenu", f1 the click sound, f2 "kurzor" ----
constexpr int F_MENU = 0, F_CLICK = 1, F_CURSOR = 2;
bool g_noCursor;
I3D_frame *FindFrame(const char *name)
{
    if (std::strcmp(name, "rollmenu") == 0)
        return FrameAt(F_MENU);
    if (std::strcmp(name, "kurzor") == 0)
        return g_noCursor ? nullptr : FrameAt(F_CURSOR);
    return nullptr;
}

template <class T> T &At(void *p, int offset) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + offset); }
template <class T> T At(const void *p, int offset) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + offset); }
template <class T> void Put(void *p, int offset, T v) { std::memcpy(static_cast<uint8_t *>(p) + offset, &v, sizeof v); }

// A port virtual's byte offset in its vtable (g++: a virtual member pointer holds 1 + the offset).
template <class M> int Slot(M m)
{
    struct {
        uintptr_t p;
        ptrdiff_t adjust;
    } r;
    static_assert(sizeof r == sizeof m, "member pointer");
    std::memcpy(&r, &m, sizeof r);
    return int(r.p - 1);
}

// ---- the world ----
constexpr int OBJECTS = 12, MEMBERS = 8, SUPPLY = 14, SEATS = 8;
// The item ids the humans may have (FindItem, CountItems), the supply boxes' ones among them (the original's table has
// their definitions: 0..499).
const int ITEM_IDS[] = {0xE5, 0x16, 7, 0x104, 1, 2, 3, 4, 5, 6, 0, -1, 12, 50, 250, 499};
constexpr int IDS = int(sizeof ITEM_IDS / sizeof ITEM_IDS[0]);
const int SUPPLY_IDS[] = {3, 7, 12, 50, 250, 499, 0xE5, 0x16};
int IdIndex(int id)
{
    for (int k = 0; k < IDS; ++k)
        if (ITEM_IDS[k] == id)
            return k;
    return -1;
}
const int TYPES[] = {ACTOR_SUPPLY_BOX, ACTOR_DOOR, ACTOR_MINE, ACTOR_HUMAN, ACTOR_CAR, ACTOR_DOOR, ACTOR_RADIO, ACTOR_LADDER,
                     ACTOR_SUPPLY_BOX, ACTOR_ITEM, ACTOR_SWITCH, ACTOR_PHOTO_PLACE, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON,
                     ACTOR_STATIC_WEAPON, ACTOR_EXPLOSIVE_PLACE, ACTOR_DOCUMENT_PLACE, ACTOR_PIKE, ACTOR_MELEE_TRACKER,
                     ACTOR_WIRE_CUT, ACTOR_ANIMATION, ACTOR_JUNKERS88, ACTOR_CAR, ACTOR_BARREL, ACTOR_SPAWN_POINT, ACTOR_NULL, 0,
                     1, 16, 33, 40};
struct S_Obj {
    int type, netId;
    char name[12];   // GetName and GetShortName
    // a human's
    int health, vehicle, seat, carried, role, team, healthPercent, viewMode, climb, ammo, weapon;   // weapon: -2 none
    uint32_t stance, readyTime;
    bool unconscious, surrendered, u1a5, medkit, wireCutter, beltItem, inWater, hostile, hasOrder, work5;
    uint32_t items;   // FindItem: bits over ITEM_IDS
    int counts[IDS];
    // a vehicle's
    int seatCount, occupant[SEATS], seatText[SEATS];
    bool canEnter, enemyAboard, destroyed;
    // a door's
    bool locked, pickable, u118, portal, otherPortal;
    int key, motion;
    // a radio's, an explosive place's, a document place's, a static weapon's; a radio's CanUse
    int state, itemId, textId;
    bool canUse;
    // a supply box's
    int charges, maxCarried, hasLists, boxState, alliedCount, axisCount, allied[SUPPLY], axis[SUPPLY];
    uint32_t boxFlags;
    // an item actor's: CanPutItem
    int canPut;
    // an animation's
    bool stopped, noInfo;
};
struct S_World {
    S_Obj obj[OBJECTS];
    int members[MEMBERS], memberCount, selected, commanded;   // object indices (-1: none)
    int key;
    uint32_t keyChar;
    float held[64], pressed[64];
    uint32_t role, missionTime;
    int gameType;
    bool coop;
    float cursor[3], bound[6];   // "kurzor"'s world position, the menu's world bound
    int focus;                   // an actor the menu is mostly aimed at (-1: none)
};
S_World g_w, g_wStart;

// ---- each side's objects ----
alignas(16) uint8_t g_orig[OBJECTS][0x400], g_port[OBJECTS][0x400];
alignas(16) uint8_t g_origWeapon[OBJECTS][0x100], g_portWeapon[OBJECTS][0x100];   // the hands weapons (+4 the definition)
alignas(16) uint8_t g_origWDef[OBJECTS][0x40], g_portWDef[OBJECTS][0x40];         // their definitions (vtables)
alignas(16) uint8_t g_items[OBJECTS][0x40];                                        // the item actors' items (not looked into)
alignas(16) uint8_t g_itemDefs[500][0x20];                                         // the supply items' (+0x14 the text)
alignas(16) uint8_t g_origGame[0x100], g_portGame[0x100], g_ui[0x40], g_settings[0x400];
void *g_origActorVt[0x40], *g_portActorVt[0x80], *g_origDefVt[16], *g_portDefVt[32];
C_str g_names[OBJECTS];
int g_side;   // the side running

void *Obj(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_port[i]) : static_cast<void *>(g_orig[i]); }
void *Weapon(int i) { return g_side ? static_cast<void *>(g_portWeapon[i]) : static_cast<void *>(g_origWeapon[i]); }
int ObjIndex(const void *p)
{
    for (int i = 0; i < OBJECTS; ++i)
        if (p == g_orig[i] || p == g_port[i])
            return i;
    return -1;
}
int BodyIndex(const void *body)   // the humans' bodies: the original's at +0x98
{
    for (int i = 0; i < OBJECTS; ++i)
        if (body == g_orig[i] + 0x98 || body == g_port[i] + offsetof(C_Human, m_body))
            return i;
    return -1;
}
std::string PartName(const void *p)
{
    char text[24];
    const int i = ObjIndex(p);
    if (i >= 0) {
        std::snprintf(text, sizeof text, "o%d", i);
        return text;
    }
    for (int k = 0; k < OBJECTS; ++k) {
        if (p == g_origWeapon[k] || p == g_portWeapon[k]) {
            std::snprintf(text, sizeof text, "w%d", k);
            return text;
        }
        if (p == g_items[k]) {
            std::snprintf(text, sizeof text, "item%d", k);
            return text;
        }
    }
    if (p == g_ui)
        return "ui";
    return "";
}
const S_Obj &O(const void *p) { return g_w.obj[ObjIndex(p)]; }

// ---- the team ----
bool IsTeamMember(int i)
{
    for (int k = 0; k < g_w.memberCount; ++k)
        if (g_w.members[k] == i)
            return true;
    return false;
}
void *Selected()
{
    Log("team GetSelectedMember -> o%d", g_w.selected);
    return Obj(g_w.selected);
}
void *Commanded()
{
    Log("team GetCommandedMember -> o%d", g_w.commanded);
    return Obj(g_w.commanded);
}
bool IsMember(const void *h)
{
    Log("team IsMember %s", Name(h).c_str());
    return IsTeamMember(ObjIndex(h));
}
void *__attribute__((thiscall)) O_Selected(void *) { return Selected(); }
void *__attribute__((thiscall)) O_Commanded(void *) { return Commanded(); }
bool __attribute__((thiscall)) O_IsMember(void *, void *h) { return IsMember(h); }

// ---- the humans ----
void *ItemOf(const void *h, bool has) { return has ? Weapon(ObjIndex(h)) : nullptr; }
void *FindItem(const void *h, int id)
{
    Log("%s FindItem %d", Name(h).c_str(), id);
    const int k = IdIndex(id);
    return ItemOf(h, k >= 0 && (O(h).items >> k & 1));
}
int CountItems(const void *h, int id)
{
    Log("%s CountItems %d", Name(h).c_str(), id);
    const int k = IdIndex(id);
    return k >= 0 ? O(h).counts[k] : 0;
}
int CountAmmo(const void *h, int id, bool backpack)
{
    Log("%s CountAmmo %d %d", Name(h).c_str(), id, backpack);
    return O(h).ammo;
}
void *WireCutter(const void *h)
{
    Log("%s FindWireCutter", Name(h).c_str());
    return ItemOf(h, O(h).wireCutter);
}
void *BeltItem(const void *h, int behaviour)
{
    Log("%s FindBeltItem %d", Name(h).c_str(), behaviour);
    return ItemOf(h, O(h).beltItem);
}
void *HandsWeapon(const void *h)
{
    Log("%s GetHandsWeapon", Name(h).c_str());
    return ItemOf(h, O(h).weapon != -2);
}
void *Medkit(const void *h)
{
    Log("%s GetMedkit", Name(h).c_str());
    return ItemOf(h, O(h).medkit);
}
#define HUMAN_QUERY(NAME, TYPE, FIELD) \
    TYPE NAME(const void *h)           \
    {                                  \
        Log("%s " #NAME, Name(h).c_str()); \
        return O(h).FIELD;             \
    }
HUMAN_QUERY(IsSurrendered, bool, surrendered)
HUMAN_QUERY(IsUnconscious, bool, unconscious)
HUMAN_QUERY(IsInWater, bool, inWater)
HUMAN_QUERY(GetHealthPercent, int, healthPercent)
HUMAN_QUERY(GetStanceFlags, uint32_t, stance)
HUMAN_QUERY(GetPlayerViewMode, int, viewMode)
HUMAN_QUERY(HasOrder, bool, hasOrder)
#undef HUMAN_QUERY
bool IsHostileTo(const void *h, const void *other)
{
    Log("%s IsHostileTo %s", Name(h).c_str(), Name(other).c_str());
    return O(h).hostile;
}
int Team(const void *h, bool pending)
{
    Log("%s GetTeam %d", Name(h).c_str(), pending);
    return O(h).team;
}
const char *ShortName(const void *h)
{
    Log("%s GetShortName", Name(h).c_str());
    return O(h).name;
}
std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }
void Issue(const void *h, int category, int type, int param1, int param2)
{
    Log("%s IssueCommand category %d type %d param1 %d param2 %d", Name(h).c_str(), category, type, param1, param2);
}
void SendCommand(const void *h, int category, const float *dir, int action, int command, const void *actor, int value,
                 const void *member)
{
    // (The dir's w: a stack word in the original, 0 in the port; the message writes x, y, z only.)
    Log("%s PlayerSendActionCommand category %d dir %s action %d command %d actor %s value %d member %s", Name(h).c_str(),
        category, F3(dir).c_str(), action, command, Name(actor).c_str(), value, Name(member).c_str());
}
void TakeFromBox(const void *h, const void *box, int index) { Log("%s RequestTakeFromBox %s %d", Name(h).c_str(), Name(box).c_str(), index); }
void PickUpItem(const void *h, const void *item) { Log("%s RequestPickUpItem %s", Name(h).c_str(), Name(item).c_str()); }
int ClimbState(const void *body)
{
    const int i = BodyIndex(body);
    Log("o%d body GetClimbState", i);
    return g_w.obj[i].climb;
}
bool WorkState5(const void *body)
{
    const int i = BodyIndex(body);
    Log("o%d body IsWorkState5", i);
    return g_w.obj[i].work5;
}
void *__attribute__((thiscall)) O_FindItem(void *h, int id, int *container)
{
    if (container)
        Log("FindItem container");
    return FindItem(h, id);
}
int __attribute__((thiscall)) O_CountItems(void *h, int id) { return CountItems(h, id); }
int __attribute__((thiscall)) O_CountAmmo(void *h, int id, bool backpack) { return CountAmmo(h, id, backpack); }
void *__attribute__((thiscall)) O_WireCutter(void *h) { return WireCutter(h); }
void *__attribute__((thiscall)) O_BeltItem(void *h, int behaviour) { return BeltItem(h, behaviour); }
void *__attribute__((thiscall)) O_HandsWeapon(void *h) { return HandsWeapon(h); }
void *__attribute__((thiscall)) O_Medkit(void *h) { return Medkit(h); }
bool __attribute__((thiscall)) O_Surrendered(void *h) { return IsSurrendered(h); }
bool __attribute__((thiscall)) O_Unconscious(void *h) { return IsUnconscious(h); }
bool __attribute__((thiscall)) O_InWater(void *h) { return IsInWater(h); }
int __attribute__((thiscall)) O_HealthPercent(void *h) { return GetHealthPercent(h); }
uint32_t __attribute__((thiscall)) O_Stance(void *h) { return GetStanceFlags(h); }
int __attribute__((thiscall)) O_ViewMode(void *h) { return GetPlayerViewMode(h); }
bool __attribute__((thiscall)) O_HasOrder(void *h) { return HasOrder(h); }
bool __attribute__((thiscall)) O_Hostile(void *h, void *other) { return IsHostileTo(h, other); }
int __attribute__((thiscall)) O_Team(void *h, bool pending) { return Team(h, pending); }
const char *__attribute__((thiscall)) O_ShortName(void *h) { return ShortName(h); }
void __attribute__((thiscall)) O_Issue(void *h, const uint8_t *c) { Issue(h, At<int>(c, 4), At<int>(c, 8), At<int>(c, 0xC), At<int>(c, 0x10)); }
void __attribute__((thiscall)) O_SendCommand(void *h, const uint8_t *c)
{
    SendCommand(h, At<int>(c, 4), reinterpret_cast<const float *>(c + 0x10), At<int>(c, 0x20), At<int>(c, 0x24), At<void *>(c, 0x28),
                At<int>(c, 0x2C), At<void *>(c, 0x30));
}
void __attribute__((thiscall)) O_TakeFromBox(void *h, void *box, int index) { TakeFromBox(h, box, index); }
void __attribute__((thiscall)) O_PickUpItem(void *h, void *item) { PickUpItem(h, item); }
int __attribute__((thiscall)) O_ClimbState(void *body) { return ClimbState(body); }
bool __attribute__((thiscall)) O_WorkState5(void *body) { return WorkState5(body); }

// ---- the cars, the radios and the actors' virtuals ----
bool CanEnter(const void *car)
{
    Log("%s CanEnter", Name(car).c_str());
    return O(car).canEnter;
}
bool EnemyAboard(const void *car, const void *h)
{
    Log("%s HasEnemyAboard %s", Name(car).c_str(), Name(h).c_str());
    return O(car).enemyAboard;
}
bool Destroyed(const void *car)
{
    Log("%s IsDestroyed", Name(car).c_str());
    return O(car).destroyed;
}
bool CanUse(const void *radio, const void *h)
{
    Log("%s CanUse %s", Name(radio).c_str(), Name(h).c_str());
    return O(radio).canUse;
}
C_str *GetName(const void *a)
{
    Log("%s GetName", Name(a).c_str());
    return &g_names[ObjIndex(a)];
}
void *SeatOccupant(const void *a, int seat)
{
    Log("%s GetSeatOccupant %d", Name(a).c_str(), seat);
    return seat >= 0 && seat < SEATS ? Obj(O(a).occupant[seat]) : nullptr;
}
int SeatCount(const void *a)
{
    Log("%s GetSeatCount", Name(a).c_str());
    return O(a).seatCount;
}
int SeatText(const void *a, int seat)
{
    Log("%s GetSeatText %d", Name(a).c_str(), seat);
    return seat >= 0 && seat < SEATS ? O(a).seatText[seat] : -1;
}
bool __attribute__((thiscall)) O_CanEnter(void *car) { return CanEnter(car); }
bool __attribute__((thiscall)) O_EnemyAboard(void *car, void *h) { return EnemyAboard(car, h); }
bool __attribute__((thiscall)) O_Destroyed(void *car) { return Destroyed(car); }
bool __attribute__((thiscall)) O_CanUse(void *radio, void *h) { return CanUse(radio, h); }
C_str *__attribute__((thiscall)) V_GetName(void *a) { return GetName(a); }
void *__attribute__((thiscall)) V_SeatOccupant(void *a, int seat) { return SeatOccupant(a, seat); }
int __attribute__((thiscall)) V_SeatCount(void *a) { return SeatCount(a); }
int __attribute__((thiscall)) V_SeatText(void *a, int seat) { return SeatText(a, seat); }
void __attribute__((thiscall)) V_Other(void *p) { Log("%s other slot", Name(p).c_str()); }
int AmmoItemId(const void *def)
{
    int i = -1;
    for (int k = 0; k < OBJECTS; ++k)
        if (def == g_origWDef[k] || def == g_portWDef[k])
            i = k;
    Log("wdef%d GetAmmoItemId", i);
    return g_w.obj[i].weapon;
}
int __attribute__((thiscall)) V_AmmoItemId(void *def) { return AmmoItemId(def); }

// ---- the profile, the texts, the actors and items modules, the interface, the controls, the graph, the bound ----
bool Cooperative()
{
    Log("profile IsCooperativeGame");
    return g_w.coop;
}
std::string LockedText() { return "Locked"; }
void TextsGet(int id) { Log("texts Get %d", id); }
void *GetActorById(int id)
{
    int found = -1;
    for (int i = 0; i < OBJECTS && found < 0; ++i)
        if (g_w.obj[i].netId == id)
            found = i;
    Log("actors GetActorById %d -> o%d", id, found);
    return Obj(found);
}
int CanPut(const void *h, const void *item)
{
    int i = -1;
    for (int k = 0; k < OBJECTS; ++k)
        if (item == g_items[k])
            i = k;
    Log("items CanPutItem %s item%d", Name(h).c_str(), i);
    return i >= 0 ? g_w.obj[i].canPut : 0;
}
void ShowInventory(const void *ui, bool show, const void *actor) { Log("%s ShowInventory %d %s", Name(ui).c_str(), show, Name(actor).c_str()); }
void Clear(const char *what, int n, const float *held, const float *pressed)
{
    Log("controls %s %d %s %s", what, n, held == g_w.held ? "held" : "?", pressed == g_w.pressed ? "pressed" : "?");
}
bool __attribute__((thiscall)) O_Cooperative(void *) { return Cooperative(); }
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    TextsGet(id);
    new (out) C_str(LockedText().c_str());
    return out;
}
void *__attribute__((thiscall)) O_ActorById(void *, int id) { return GetActorById(id); }
int __attribute__((thiscall)) O_CanPut(void *, void *h, void *item) { return CanPut(h, item); }
void __attribute__((thiscall)) O_ShowInventory(void *ui, bool show, void *actor) { ShowInventory(ui, show, actor); }
void __attribute__((thiscall)) O_ClearAction(void *, int action, float *held, float *pressed) { Clear("ClearAction", action, held, pressed); }
void __attribute__((thiscall)) O_ClearKey(void *, int key, float *held, float *pressed) { Clear("ClearKey", key, held, pressed); }
int __stdcall G_AsciiKey(void *)
{
    Log("graph GetAsciiKey");
    return g_w.key;
}
uint32_t __stdcall G_KeyToChar(void *, int key, int unknown)
{
    Log("graph KeyToChar %d %d", key, unknown);
    return g_w.keyChar;
}
void WBound(void *visual)
{
    Log("%s UpdateWBoundProc", Name(visual).c_str());
    At<uint32_t>(visual, 0x1D0) |= VISFLAGS_WBOUND_VALID;
}
void __stdcall O_WBound(void *visual) { WBound(visual); }
void *__cdecl O_GetGame() { return g_origGame; }

}  // namespace H

// ---- the port's side of the same fakes ----
I3D_sound *g_clickSound;
static S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(H::g_settings);
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;
C_Human *C_Team::GetSelectedMember() const { return static_cast<C_Human *>(H::Selected()); }
C_Human *C_Team::GetCommandedMember() const { return static_cast<C_Human *>(H::Commanded()); }
bool C_TeamBase::IsMember(const C_Human *human) const { return H::IsMember(human); }
C_Item *C_Human::FindItem(int typeId, int *container) const
{
    if (container)
        H::Log("FindItem container");
    return static_cast<C_Item *>(H::FindItem(this, typeId));
}
int C_Human::CountItems(int typeId) const { return H::CountItems(this, typeId); }
int C_Human::CountAmmo(int typeId, bool backpack) const { return H::CountAmmo(this, typeId, backpack); }
C_Item *C_Human::FindWireCutter() const { return static_cast<C_Item *>(H::WireCutter(this)); }
C_Item *C_Human::FindBeltItem(int behaviour) const { return static_cast<C_Item *>(H::BeltItem(this, behaviour)); }
C_WeaponItem *C_Human::GetHandsWeapon() const { return static_cast<C_WeaponItem *>(H::HandsWeapon(this)); }
C_Item *C_Human::GetMedkit() const { return static_cast<C_Item *>(H::Medkit(this)); }
bool C_Human::IsSurrendered() const { return H::IsSurrendered(this); }
bool C_Human::IsUnconscious() const { return H::IsUnconscious(this); }
bool C_Human::IsInWater() const { return H::IsInWater(this); }
int C_Human::GetHealthPercent() const { return H::GetHealthPercent(this); }
uint32_t C_Human::GetStanceFlags() const { return H::GetStanceFlags(this); }
int C_Human::GetPlayerViewMode() const { return H::GetPlayerViewMode(this); }
bool C_Human::HasOrder() const { return H::HasOrder(this); }
bool C_Human::IsFriendOf(const C_Actor *other) const { return !H::IsHostileTo(this, other); }
int C_Human::GetTeam(bool pending) const { return H::Team(this, pending); }
const char *C_Human::GetShortName() const { return H::ShortName(this); }
void C_Human::IssueCommand(const S_HumanCommand &c) { H::Issue(this, c.category, c.type, c.param1, c.param2); }
void C_Human::PlayerSendActionCommand(const S_ActionCommand &c)
{
    H::SendCommand(this, c.category, &c.dir.x, c.action, c.command, c.actor, c.value, c.member);
}
void C_Human::RequestTakeFromBox(C_Actor *box, int index) { H::TakeFromBox(this, box, index); }
void C_Human::RequestPickUpItem(C_Actor *itemActor) { H::PickUpItem(this, itemActor); }
int S_HumanBody::GetClimbState() const { return H::ClimbState(this); }
bool S_HumanBody::IsWorkState5() const { return H::WorkState5(this); }
bool C_Car::CanEnter() { return H::CanEnter(this); }
bool C_Car::HasEnemyAboard(const C_Human *human) { return H::EnemyAboard(this, human); }
bool C_Car::IsDestroyed() const { return H::Destroyed(this); }
bool C_Radio::CanUse(C_Human *human) const { return H::CanUse(this, human); }
bool C_GameProfile::IsCooperativeGame() const { return H::Cooperative(); }
C_str C_GameTexts::Get(int id) const
{
    H::TextsGet(id);
    return C_str(H::LockedText().c_str());
}
C_Actor *C_ActorsModule::GetActorById(int id) const { return static_cast<C_Actor *>(H::GetActorById(id)); }
int C_ItemsModule::CanPutItem(C_Actor *actor, C_Item *item) { return H::CanPut(actor, item); }
void C_Interface::ShowInventory(bool show, C_Actor *container) { H::ShowInventory(this, show, container); }
C_ItemDef *GetItemDef(int typeId)
{
    return typeId >= 0 && typeId < 500 ? reinterpret_cast<C_ItemDef *>(H::g_itemDefs[typeId]) : nullptr;
}
void C_Controls::ClearAction(int action, float *held, float *pressed) { H::Clear("ClearAction", action, held, pressed); }
void C_Controls::ClearKey(int key, float *held, float *pressed) { H::Clear("ClearKey", key, held, pressed); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
void __stdcall I3D_visual::UpdateWBoundProc() { H::WBound(this); }

namespace H {

// ---- building each side's objects from the world ----
void PortVectorOf(void *vector, const int *begin, int n)   // (libstdc++'s: start, finish, end of storage)
{
    const int **v = static_cast<const int **>(vector);
    v[0] = n ? begin : nullptr;
    v[1] = v[2] = n ? begin + n : nullptr;
}
void OrigVectorOf(void *first, const int *begin, int n)   // (MSVC's: first, last, end)
{
    const int **v = static_cast<const int **>(first);
    v[0] = n ? begin : nullptr;
    v[1] = v[2] = n ? begin + n : nullptr;
}
void SyncWorld(int side)
{
    g_side = side;
    for (int i = 0; i < OBJECTS; ++i) {
        const S_Obj &o = g_w.obj[i];
        g_names[i] = C_str(o.name);
        if (!side) {
            uint8_t *b = g_orig[i];
            std::memset(b, 0, 0x400);
            Put<void *>(b, 0, g_origActorVt);
            Put<int>(b, 0x10, o.netId);
            Put<int>(b, 0x18, o.type);
            if (o.type == ACTOR_HUMAN) {
                Put<int>(b, 0xEC, o.health);
                Put<void *>(b, 0x160, Obj(o.vehicle));
                Put<int>(b, 0x164, o.seat);
                Put<void *>(b, 0x174, Obj(o.carried));
                Put<int>(b, 0x19C, o.role);
                Put<bool>(b, 0x1A5, o.u1a5);
            } else if (o.type == ACTOR_DOOR) {
                Put<int>(b, 0x224, o.motion);
                Put<int>(b, 0x230, o.key);
                Put<bool>(b, 0x234, o.locked);
                Put<bool>(b, 0x235, o.pickable);
                Put<bool>(b, 0x118, o.u118);
                Put<void *>(b, 0xFC, o.portal ? g_ui : nullptr);
                Put<void *>(b, 0x100, o.otherPortal ? g_ui : nullptr);
            } else if (o.type == ACTOR_RADIO) {
                Put<int>(b, 0xA0, o.state);
                Put<int>(b, 0x98, o.itemId);
            } else if (o.type == ACTOR_EXPLOSIVE_PLACE) {
                Put<int>(b, 0x94, o.state);
                Put<int>(b, 0xA0, o.itemId);
            } else if (o.type == ACTOR_DOCUMENT_PLACE) {
                Put<int>(b, 0x98, o.itemId);
                Put<int>(b, 0x9C, o.textId);
            } else if (o.type == ACTOR_ANIMATION) {
                Put<bool>(b, 0xA1, o.stopped);
                Put<bool>(b, 0xA4, o.noInfo);
            } else if (o.type == ACTOR_STATIC_WEAPON) {
                Put<int>(b, 0x2F0, o.state);
            } else if (o.type == ACTOR_ITEM) {
                Put<void *>(b, 0x90, g_items[i]);
            } else if (o.type == ACTOR_SUPPLY_BOX) {
                OrigVectorOf(b + 0x100, o.axis, o.axisCount);
                OrigVectorOf(b + 0x110, o.allied, o.alliedCount);
                Put<int>(b, 0x11C, o.hasLists);
                Put<uint32_t>(b, 0x124, o.boxFlags);
                Put<int>(b, 0x128, o.maxCarried);
                Put<int>(b, 0x130, o.charges);
                Put<int>(b, 0x250, o.boxState);
            }
            Put<void *>(g_origWeapon[i], 4, g_origWDef[i]);
            Put<uint32_t>(g_origWeapon[i], 0x5C, o.readyTime);
            Put<void *>(g_origWDef[i], 0, g_origDefVt);
        } else {
            uint8_t *b = g_port[i];
            std::memset(b, 0, 0x400);
            Put<void *>(b, 0, g_portActorVt);
            C_Actor *a = reinterpret_cast<C_Actor *>(b);
            a->m_type = E_ActorType(o.type);
            a->m_netId = o.netId;
            if (o.type == ACTOR_HUMAN) {
                C_Human *h = reinterpret_cast<C_Human *>(b);
                h->m_stats[STAT_HEALTH] = o.health;
                h->m_vehicle = static_cast<C_Actor *>(Obj(o.vehicle));
                h->m_seat = o.seat;
                h->m_carried = static_cast<C_Actor *>(Obj(o.carried));
                h->m_role = E_HumanRole(o.role);
                h->m_unknown1a5 = o.u1a5;
            } else if (o.type == ACTOR_DOOR) {
                C_Door *d = reinterpret_cast<C_Door *>(b);
                d->m_motion = o.motion;
                d->m_key = o.key;
                d->m_locked = o.locked;
                d->m_pickable = o.pickable;
                d->m_unknown118 = o.u118;
                d->m_portal = o.portal ? reinterpret_cast<I3D_portal *>(g_ui) : nullptr;
                d->m_otherPortal = o.otherPortal ? reinterpret_cast<I3D_portal *>(g_ui) : nullptr;
            } else if (o.type == ACTOR_RADIO) {
                reinterpret_cast<C_Radio *>(b)->m_state = o.state;
                reinterpret_cast<C_Radio *>(b)->m_itemId = o.itemId;
            } else if (o.type == ACTOR_EXPLOSIVE_PLACE) {
                reinterpret_cast<C_ExplosivePlace *>(b)->m_state = o.state;
                reinterpret_cast<C_ExplosivePlace *>(b)->m_itemId = o.itemId;
            } else if (o.type == ACTOR_DOCUMENT_PLACE) {
                reinterpret_cast<C_DocumentPlace *>(b)->m_itemId = o.itemId;
                reinterpret_cast<C_DocumentPlace *>(b)->m_textId = o.textId;
            } else if (o.type == ACTOR_ANIMATION) {
                reinterpret_cast<C_AnimationActor *>(b)->m_stopped = o.stopped;
                reinterpret_cast<C_AnimationActor *>(b)->m_showNoInfo = o.noInfo;
            } else if (o.type == ACTOR_STATIC_WEAPON) {
                reinterpret_cast<C_StaticWeapon *>(b)->m_state = o.state;
            } else if (o.type == ACTOR_ITEM) {
                reinterpret_cast<C_ItemActor *>(b)->m_item = reinterpret_cast<C_Item *>(g_items[i]);
            } else if (o.type == ACTOR_SUPPLY_BOX) {
                C_SupplyBox *s = reinterpret_cast<C_SupplyBox *>(b);
                PortVectorOf(&s->m_axisItems, o.axis, o.axisCount);
                PortVectorOf(&s->m_alliedItems, o.allied, o.alliedCount);
                s->m_hasItemLists = o.hasLists;
                s->m_flags = o.boxFlags;
                s->m_maxCarried = o.maxCarried;
                s->m_charges = o.charges;
                s->m_state = decltype(s->m_state)(o.boxState);
            }
            C_WeaponItem *w = reinterpret_cast<C_WeaponItem *>(g_portWeapon[i]);
            w->m_def = reinterpret_cast<C_ItemDef *>(g_portWDef[i]);
            w->m_readyTime = o.readyTime;
            Put<void *>(g_portWDef[i], 0, g_portDefVt);
        }
    }
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    Put<int>(g_settings, 0x224, g_w.gameType);
    Put<uint32_t>(g_origGame, 0x28, g_w.missionTime);
    Put<uint32_t>(g_portGame, offsetof(C_game, m_missionTime), g_w.missionTime);
    // The cursor's world position, the menu's world bound (+0x170: min, max).
    FrameAt(F_CURSOR)->m_worldMatrix(3) = S_vector(g_w.cursor[0], g_w.cursor[1], g_w.cursor[2]);
}

const char *const NAMES[] = {"Bob", "Ann", "x", "Sgt. Lee", "", "Jo-Jo", "Mac", "A b c", "Ivan", "Pvt 1", "Kay", "Zed"};
void RandomObj(int i)
{
    S_Obj &o = g_w.obj[i];
    o = S_Obj{};
    o.type = P(0.45) ? int(ACTOR_HUMAN) : Pick(TYPES);
    o.netId = P(0.85) ? i : I(-2, 20);
    std::snprintf(o.name, sizeof o.name, "%s", NAMES[I(0, int(sizeof NAMES / sizeof NAMES[0]) - 1)]);
    o.health = P(0.75) ? I(1, 800) : Pick({0, -1, 1});
    o.vehicle = P(0.75) ? -1 : I(0, OBJECTS - 1);
    o.seat = P(0.8) ? I(0, 5) : Pick({-1, 31, 7});
    o.carried = P(0.8) ? -1 : I(0, OBJECTS - 1);
    o.role = P(0.8) ? I(1, 5) : Pick({0, 6, -1});
    o.team = P(0.8) ? I(0, 1) : Pick({2, -1});
    o.healthPercent = P(0.5) ? 100 : Pick({0, 1, 50, 99, 101});
    o.viewMode = P(0.85) ? 0 : Pick({1, 2, -1});
    o.climb = P(0.85) ? 0 : Pick({1, 2, -1});
    o.ammo = P(0.5) ? I(0, 6) : Pick({0, 10, -5});
    o.weapon = P(0.2) ? -2 : P(0.7) ? Pick(ITEM_IDS) : Pick({-1, 0, 1000});
    o.stance = P(0.5) ? 4u : Pick({0u, 1u, 2u, 5u, 0xFFu, 0xFBu});
    o.readyTime = P(0.6) ? 0u : U32() % 2000u;
    o.unconscious = P(0.2);
    o.surrendered = P(0.3);
    o.u1a5 = P(0.4);
    o.medkit = P(0.4);
    o.wireCutter = P(0.5);
    o.beltItem = P(0.5);
    o.inWater = P(0.2);
    o.hostile = P(0.5);
    o.hasOrder = P(0.12);
    o.work5 = P(0.15);
    o.items = U32() & (P(0.3) ? 0xFFFFu : 0x0Fu << I(0, 12));
    for (int k = 0; k < IDS; ++k)
        o.counts[k] = P(0.5) ? I(0, 6) : Pick({0, 1, 10, 100});
    o.seatCount = P(0.7) ? I(0, 6) : Pick({0, 1, 7, 8, -1});   // (more than 8: past the entries, the port's fix, tested alone)
    for (int k = 0; k < SEATS; ++k) {
        o.occupant[k] = P(0.6) ? -1 : I(0, OBJECTS - 1);
        o.seatText[k] = P(0.6) ? -1 : 0x1C00 + I(0, 0xFF);
    }
    o.canEnter = P(0.6);
    o.enemyAboard = P(0.3);
    o.destroyed = P(0.25);
    o.locked = P(0.4);
    o.pickable = P(0.5);
    o.u118 = P(0.3);
    o.portal = P(0.6);
    o.otherPortal = P(0.4);
    o.key = P(0.7) ? Pick(ITEM_IDS) : I(-5, 600);
    o.motion = P(0.6) ? I(0, 1) : I(-1, 3);
    o.state = P(0.7) ? I(0, 2) : I(-1, 4);
    o.itemId = P(0.8) ? Pick(ITEM_IDS) : I(-5, 600);
    o.textId = P(0.5) ? 0x1B00 + I(0, 0xFF) : int(U32());
    o.canUse = P(0.5);
    o.charges = P(0.6) ? I(0, 3) : Pick({0, -1, 100});
    o.maxCarried = P(0.6) ? I(0, 6) : Pick({0, 1, -1, 100});
    o.hasLists = P(0.5) ? 0 : Pick({1, 2, -1});
    o.boxState = P(0.8) ? 0 : I(1, 3);
    o.boxFlags = P(0.8) ? 1u : U32() & 0xFu;
    // (More than the entries' ten: the port's fix, tested alone.)
    o.alliedCount = P(0.15) ? 0 : I(1, C_RollMenu::ENTRIES);
    o.axisCount = P(0.15) ? 0 : I(1, C_RollMenu::ENTRIES);
    for (int k = 0; k < SUPPLY; ++k) {
        o.allied[k] = Pick(SUPPLY_IDS);
        o.axis[k] = Pick(SUPPLY_IDS);
    }
    o.canPut = P(0.5) ? Pick({0, 1}) : I(-2, 3);
    o.stopped = P(0.5);
    o.noInfo = P(0.4);
}
// The occupants, the members, the selected and commanded soldiers are humans (the menu reads their health and fields
// as a human's; another actor's would be laid out its own way on each side).
void Humanize(int i)
{
    if (i >= 0)
        g_w.obj[i].type = ACTOR_HUMAN;
}
void HumanizeTeam();
void FixOccupants()
{
    HumanizeTeam();
    // (A seat of another actor left empty.)
    for (S_Obj &o : g_w.obj)
        for (int k = 0; k < SEATS; ++k)
            if (o.occupant[k] >= 0 && g_w.obj[o.occupant[k]].type != ACTOR_HUMAN)
                o.occupant[k] = -1;
}
// (The team stays human: a vehicle or a focus drawn for one soldier may be another.)
void HumanizeTeam()
{
    for (int k = 0; k < g_w.memberCount; ++k)
        Humanize(g_w.members[k]);
    Humanize(g_w.selected);
    Humanize(g_w.commanded);
}
void RandomTeam()
{
    g_w.memberCount = P(0.1) ? I(0, 1) : I(2, MEMBERS);
    for (int k = 0; k < g_w.memberCount; ++k)
        Humanize(g_w.members[k] = I(0, OBJECTS - 1));
    g_w.selected = P(0.06) ? -1 : g_w.memberCount && P(0.85) ? g_w.members[I(0, g_w.memberCount - 1)] : I(0, OBJECTS - 1);
    Humanize(g_w.selected);
    if (g_w.selected >= 0 && P(0.85))
        g_w.obj[g_w.selected].health = std::max(g_w.obj[g_w.selected].health, 1);
    g_w.commanded = P(0.06) ? -1 : g_w.memberCount && P(0.85) ? g_w.members[I(0, g_w.memberCount - 1)] : I(0, OBJECTS - 1);
    Humanize(g_w.commanded);
}
void RandomInput()
{
    g_w.key = P(0.7) ? I(0, 0x60) : int(U32());
    const uint32_t c = P(0.6) ? uint32_t('0' + I(0, 9)) : P(0.5) ? Pick({0u, uint32_t('a'), uint32_t('/'), uint32_t(':'), 0xB0u}) : U32() & 0xFF;
    g_w.keyChar = P(0.2) ? c | (U32() & 0xFFFFFF00u) : c;
    for (float &f : g_w.held)
        f = P(0.8) ? 0.0f : 1.0f;
    for (float &f : g_w.pressed)
        f = P(0.85) ? 0.0f : 1.0f;
    g_w.pressed[INPUT_ROLL_MENU] = P(0.7) ? 0.0f : Pick({1.0f, -1.0f, 0.5f, -0.0f});
    g_w.pressed[INPUT_WHEEL] = P(0.5) ? 0.0f : Pick({1.0f, -1.0f, 120.0f, -120.0f, -0.0f});
}
void RandomRole()
{
    g_w.role = P(0.6) ? 7u : Pick({0u, 1u, 2u, 3u, 6u, 0xFu, 0x17u});
    g_w.gameType = P(0.3) ? MP_GAME_DEATHMATCH : I(0, 6);
    g_w.coop = P(0.5);
    g_w.missionTime = P(0.5) ? 1000u : U32() % 3000u;
}
void RandomPlace()
{
    for (float &f : g_w.cursor)
        f = P(0.2) ? Pick({0.0f, 0.5f, -0.5f}) : F(-1, 1);
    for (int k = 0; k < 3; ++k) {
        const float a = F(-1, 1), b = F(-1, 1);
        g_w.bound[k] = std::min(a, b);
        g_w.bound[3 + k] = std::max(a, b);
    }
    // (Now and then the cursor on the bound's edges.)
    if (P(0.3)) {
        g_w.cursor[0] = P(0.5) ? Pick({g_w.bound[0], g_w.bound[3]}) : (g_w.bound[0] + g_w.bound[3]) / 2;
        g_w.cursor[1] = P(0.5) ? Pick({g_w.bound[1], g_w.bound[4]}) : (g_w.bound[1] + g_w.bound[4]) / 2;
    }
}
void RandomWorld()
{
    for (int i = 0; i < OBJECTS; ++i)
        RandomObj(i);
    RandomTeam();
    // (The selected or commanded soldier now and then in a vehicle (a seat taken), carrying a body.)
    for (const int who : {g_w.selected, g_w.commanded}) {
        if (who < 0)
            continue;
        if (P(0.25)) {
            const int v = I(0, OBJECTS - 1);
            if (v != who) {
                g_w.obj[who].vehicle = v;
                g_w.obj[v].type = Pick({int(ACTOR_CAR), int(ACTOR_SUBMARINE), int(ACTOR_STATIC_WEAPON), int(ACTOR_JUNKERS88)});
                g_w.obj[who].carried = -1;
            }
        } else if (P(0.15)) {
            g_w.obj[who].carried = I(0, OBJECTS - 1);
        } else if (P(0.7)) {
            g_w.obj[who].vehicle = g_w.obj[who].carried = -1;
        }
    }
    // (A focus: a human, a vehicle, a door, a supply box (a member's ammunition at its most) ...)
    g_w.focus = P(0.85) ? I(0, OBJECTS - 1) : -1;
    if (g_w.focus >= 0 && g_w.focus != g_w.selected && g_w.focus != g_w.commanded) {
        S_Obj &f = g_w.obj[g_w.focus];
        f.type = Pick({int(ACTOR_HUMAN), int(ACTOR_HUMAN), int(ACTOR_CAR), int(ACTOR_SUBMARINE), int(ACTOR_STATIC_WEAPON),
                       int(ACTOR_DOOR), int(ACTOR_SUPPLY_BOX), int(ACTOR_RADIO), int(ACTOR_ITEM), int(ACTOR_EXPLOSIVE_PLACE),
                       int(ACTOR_DOCUMENT_PLACE), int(ACTOR_PHOTO_PLACE), int(ACTOR_WIRE_CUT), int(ACTOR_ANIMATION),
                       int(ACTOR_MINE), int(ACTOR_SWITCH), int(ACTOR_LADDER), int(ACTOR_MELEE_TRACKER)});
        if (f.type == ACTOR_SUPPLY_BOX && g_w.selected >= 0 && P(0.6)) {
            S_Obj &s = g_w.obj[g_w.selected];
            s.ammo = f.maxCarried + I(-2, 0);
            if (g_w.commanded >= 0)
                g_w.obj[g_w.commanded].ammo = f.maxCarried + I(-1, 0);
            f.charges = P(0.8) ? 1 : 0;
            f.boxState = 0;
            f.boxFlags = 1;
        }
        if (f.type == ACTOR_HUMAN && P(0.3) && g_w.memberCount)
            g_w.members[I(0, g_w.memberCount - 1)] = g_w.focus;
        if (f.type == ACTOR_STATIC_WEAPON && P(0.6))
            f.state = 1;
        // (A gunner now and then, dead now and then; the soldier's stance any.)
        if (f.type == ACTOR_STATIC_WEAPON && P(0.5)) {
            const int gunner = I(0, OBJECTS - 1);
            if (gunner != g_w.focus && g_w.obj[gunner].type == ACTOR_HUMAN) {
                f.occupant[0] = gunner;
                if (P(0.5))
                    g_w.obj[gunner].health = 0;
            }
            if (g_w.selected >= 0)
                g_w.obj[g_w.selected].stance = Pick({0u, 1u, 4u, 5u});
        }
    }
    // (Mostly the selected soldier free to act: no view mode, order, climb, busy weapon, vehicle or body.)
    if (g_w.selected >= 0 && P(0.6)) {
        S_Obj &s = g_w.obj[g_w.selected];
        s.viewMode = s.climb = 0;
        s.hasOrder = false;
        s.readyTime = 0;
        if (P(0.8))
            s.vehicle = s.carried = -1;
    }
    // (Now and then the commanded soldier's own vehicle: the command map's way out.)
    if (g_w.commanded >= 0 && g_w.obj[g_w.commanded].vehicle >= 0 && P(0.2))
        g_w.focus = g_w.obj[g_w.commanded].vehicle;
    FixOccupants();
    RandomInput();
    RandomRole();
    RandomPlace();
}
void RandomWorldFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}
int g_objSeed;
void RandomOneObj()
{
    const int type = g_w.obj[g_objSeed].type;
    RandomObj(g_objSeed);
    // (The team's humans stay humans.)
    if (type == ACTOR_HUMAN)
        g_w.obj[g_objSeed].type = ACTOR_HUMAN;
    FixOccupants();
}
void RandomTeamFixed()
{
    RandomTeam();
    FixOccupants();
}

// ---- the operations ----
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
    float f = 0.0f, g = 0.0f;
};
enum {
    OP_INIT, OP_DONE, OP_SETTARGET, OP_HASACTIONS, OP_OPEN, OP_PREPARE, OP_INPUT, OP_UPDATE, OP_RENDER, OP_RELAYOUT, OP_CURSOROVER,
    OP_SELECT, OP_SELECTAT, OP_MOVETO, OP_REFRESH, OP_BUILD, OP_HASMAP, OP_SETENTRY, OP_ACTIVATE, OP_PREVIOUS, OP_NEXT,
    OP_TARGET,   // (the test's: m_target set)
    OP_CHOOSE,   // (the test's: m_selected set)
    W_WORLD = 100, W_OBJ, W_TEAM, W_INPUT, W_ROLE, W_PLACE, W_HEALTH, W_FRAME
};
// The menu's world bound (+0x170 the min, +0x180 the max: S_vector4s).
void SetBound()
{
    for (int k = 0; k < 3; ++k) {
        At<float>(FrameAt(F_MENU), 0x170 + 4 * k) = g_w.bound[k];
        At<float>(FrameAt(F_MENU), 0x180 + 4 * k) = g_w.bound[3 + k];
    }
}
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case W_WORLD:
        RandomWorldFrom(op.a, &RandomWorld);
        break;
    case W_OBJ:
        g_objSeed = op.b;
        RandomWorldFrom(op.a, &RandomOneObj);
        break;
    case W_TEAM:
        RandomWorldFrom(op.a, &RandomTeamFixed);
        break;
    case W_INPUT:
        RandomWorldFrom(op.a, &RandomInput);
        break;
    case W_ROLE:
        RandomWorldFrom(op.a, &RandomRole);
        break;
    case W_PLACE:
        RandomWorldFrom(op.a, &RandomPlace);
        break;
    case W_HEALTH:
        g_w.obj[op.a].health = op.b;
        break;
    case W_FRAME:
        FrameAt(F_MENU)->m_frameFlags = op.a ? FrameAt(F_MENU)->m_frameFlags | 1u : FrameAt(F_MENU)->m_frameFlags & ~1u;
        // (The bound recomputed (or not) and moved.)
        if (op.b)
            At<uint32_t>(FrameAt(F_MENU), 0x1D0) &= ~uint32_t(VISFLAGS_WBOUND_VALID);
        else
            At<uint32_t>(FrameAt(F_MENU), 0x1D0) |= VISFLAGS_WBOUND_VALID;
        SetBound();
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
uint32_t g_seed;
uint32_t g_startWords[0x20];
void StartScenario()
{
    g_seed = U32();
    for (uint32_t &w : g_startWords)
        w = P(0.3) ? Pick({0u, 0xFFFFFFFFu, 1u, 0x32u, 0x33u, 8u, 6u}) : U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    g_noCursor = P(0.03);
    RandomWorld();
    // The menu's world bound (+0x170) and its flag (+0x1D0).
    SetBound();
    At<uint32_t>(FrameAt(F_MENU), 0x1D0) = P(0.5) ? uint32_t(VISFLAGS_WBOUND_VALID) : 0u;
}
void SaveStart()
{
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    g_wStart = g_w;
}
void StartSide()
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    g_w = g_wStart;
    g_log.clear();
    g_boxes.clear();
    std::srand(g_seed);
}

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, void *, bool);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Bool = bool(__attribute__((thiscall)) *)(void *);
using T_Target = void(__attribute__((thiscall)) *)(void *, void *, float);
using T_Has = bool(__attribute__((thiscall)) *)(void *, void *, float);
using T_Open = void(__attribute__((thiscall)) *)(void *, bool);
using T_Input = void(__attribute__((thiscall)) *)(void *, float *, float *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Select = bool(__attribute__((thiscall)) *)(void *, int);
using T_Floats = void(__attribute__((thiscall)) *)(void *, const float *);
using T_Entry = void(__attribute__((thiscall)) *)(void *, int, int, int);

// The menu's fields (the same layout on both sides: the port's C_RollMenu is the original's 0x90 bytes).
std::string Snap(const void *menu)
{
    const C_RollMenu *m = static_cast<const C_RollMenu *>(menu);
    char text[300];
    std::snprintf(text, sizeof text, "menu distance %s %s %s %s %s %s openTime %d home %s %s open %d map %d selected %d target %s entries",
                  FloatText(m->m_distance).c_str(), Name(m->m_scene).c_str(), Name(m->m_ui).c_str(), Name(m->m_frame).c_str(),
                  Name(m->m_mapCursor).c_str(), m->m_text ? BoxName(m->m_text).c_str() : "null", m->m_openTime,
                  F3(&m->m_home.x).c_str(), FloatText(m->m_home.w).c_str(), At<uint8_t>(menu, 0x30), At<uint8_t>(menu, 0x31),
                  m->m_selected, Name(m->m_target).c_str());
    std::string s = text;
    for (const C_RollMenu::S_Entry &e : m->m_entries)
        s += " " + std::to_string(e.action) + ":" + std::to_string(e.textId);
    return s;
}

// Both sides crash (the guards left out): a seat entry's occupant read without a target (Refresh), the actions on a
// null target or by nobody (Activate), the command map's entries without a target or a commanded soldier, the command
// map's SetTarget on no actor, IsCursorOver without "kurzor". Activate from -1 reads the field before the entries in the
// original (the port: no action; the same unless that word is a small number).
// SelectAt's line past the entries (the original read past them: the port's fix, tested alone).
bool LinePastEntries(const C_RollMenu *m, float x, float y)
{
    const uint8_t *b = reinterpret_cast<const uint8_t *>(m->m_frame) + 0x170;
    const float minX = At<float>(b, 0), minY = At<float>(b, 4), maxX = At<float>(b, 0x10), maxY = At<float>(b, 0x14);
    if (!(minX <= x && x <= maxX && y <= maxY && minY <= y))
        return false;
    return (double(-y) + double(maxY) - 0.045) * 58.8235 >= 9.5;
}
bool SeatEntryWithoutTarget(const C_RollMenu *m)
{
    if (m->m_target)
        return false;
    for (const C_RollMenu::S_Entry &e : m->m_entries)
        if (e.action >= 8 && e.action < 14)
            return true;
    return false;
}
// Activate's effect on entry `index` needs the target / the selected soldier.
bool ActivateUnsafe(const C_RollMenu *m, int index)
{
    if (index == C_RollMenu::ENTRIES - 1)
        return false;
    int action = -1;
    if (index >= 0) {
        action = m->m_entries[index].action;
    } else {
        const uint32_t word = At<uint32_t>(m, 0x2C);
        if (word < 0x100)
            return true;
    }
    const bool selected = g_w.selected >= 0;
    switch (action) {
    case HUMAN_COMMAND_LOCK_DOOR:
    case HUMAN_COMMAND_UNLOCK_DOOR:
    case HUMAN_COMMAND_ENTER_VEHICLE:
    case HUMAN_COMMAND_PLANT_EXPLOSIVE:
    case HUMAN_COMMAND_PICK_LOCK:
    case HUMAN_COMMAND_HEAL:
    case HUMAN_COMMAND_CUT_WIRE:
    case HUMAN_COMMAND_TIE_UP:
    case HUMAN_COMMAND_OPEN_DOOR:
    case HUMAN_COMMAND_CLOSE_DOOR:
    case HUMAN_COMMAND_DELIVER_DOCUMENTS:
    case HUMAN_COMMAND_DEFUSE_MINE:
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
    case 6:
        return !selected || !m->m_target;
    case HUMAN_COMMAND_LEAVE_VEHICLE:
    case 0x1D:
        return !selected;
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13: {
        if (!selected)
            return true;
        if (m->m_target)
            return false;
        const S_Obj &s = g_w.obj[g_w.selected];
        const int v = s.vehicle;
        return !(v >= 0 && (g_w.obj[v].type == ACTOR_CAR || g_w.obj[v].type == ACTOR_SUBMARINE ||
                            g_w.obj[v].type == ACTOR_STATIC_WEAPON || g_w.obj[v].type == ACTOR_JUNKERS88));
    }
    default:
        return false;
    }
}
// HandleInput's Activate (a digit's enabled entry, or the right button).
bool InputUnsafe(const C_RollMenu *m)
{
    if (m->m_commandMap)
        return false;
    const int digit = int(g_w.keyChar & 0xFF) - '0';
    int index = m->m_selected;
    if (digit >= 0 && digit <= 9) {
        const int k = digit == 0 ? C_RollMenu::ENTRIES - 1 : digit - 1;
        const C_RollMenu::S_Entry &e = m->m_entries[k];
        if (e.action == -1 || e.action == 0x32)
            return false;
        index = k;
        return ActivateUnsafe(m, index) || SeatEntryWithoutTarget(m);
    }
    if (g_w.pressed[INPUT_ROLL_MENU] != 0.0f)
        return ActivateUnsafe(m, index);
    // (SelectPrevious from -1: the port's fix, tested alone.)
    if ((g_w.pressed[INPUT_WHEEL] > 0.0f || g_w.pressed[INPUT_MOVE_FORWARD] != 0.0f) && m->m_selected < 0)
        return true;
    return SeatEntryWithoutTarget(m);
}
const char *Skip(const C_RollMenu *m, const S_Op &op)
{
    const bool commanded = g_w.commanded >= 0;
    switch (op.kind) {
    case OP_SETTARGET:
        // (It refreshes only after Open has built the entries.)
        return m->m_commandMap && commanded && op.a < 0 ? "the command map's target: no actor" : nullptr;
    case OP_PREVIOUS:
        if (m->m_selected < 0)
            return "SelectPrevious from -1";
        return SeatEntryWithoutTarget(m) ? "a seat entry without a target" : nullptr;
    case OP_SELECTAT:
        if (LinePastEntries(m, op.f, op.g))
            return "a line past the entries";
        return SeatEntryWithoutTarget(m) ? "a seat entry without a target" : nullptr;
    case OP_REFRESH:
    case OP_SELECT:
    case OP_NEXT:
        return SeatEntryWithoutTarget(m) ? "a seat entry without a target" : nullptr;
    case OP_BUILD:
        return m->m_commandMap && (!m->m_target || !commanded) ? "the command map's entries without a target or a soldier" : nullptr;
    case OP_ACTIVATE:
        return ActivateUnsafe(m, m->m_selected) ? "an action without its actor" : nullptr;
    case OP_INPUT:
        return InputUnsafe(m) ? "an action without its actor" : nullptr;
    case OP_CURSOROVER:
        return !m->m_mapCursor ? "no cursor" : nullptr;
    default:
        return nullptr;
    }
}
void Run(int side, C_RollMenu *port, uint8_t *orig, const S_Op &op)
{
    void *self = side ? static_cast<void *>(port) : orig;
    C_RollMenu *view = static_cast<C_RollMenu *>(self);
    if (const char *why = Skip(view, op)) {
        Log("skipped: %s", why);
        return;
    }
    const float point[2] = {op.f, op.g};
    const S_vector pos(op.f, op.g, op.f - op.g);
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<C_Interface *>(g_ui), reinterpret_cast<I3D_scene *>(&g_scene), op.a != 0)
                               : OriginalAt<T_Init>(0x004b0750)(orig, g_ui, &g_scene, op.a != 0));
        break;
    case OP_DONE:
        // (The original leaves its freed text box in the menu: cleared, as the port does.)
        side ? port->Done() : (OriginalAt<T_Void>(0x004b0900)(orig), Put<void *>(orig, 0x14, nullptr));
        break;
    case OP_SETTARGET:
        side ? port->SetTarget(static_cast<C_Actor *>(Obj(op.a)), op.f) : OriginalAt<T_Target>(0x004b0920)(orig, Obj(op.a), op.f);
        break;
    case OP_HASACTIONS:
        Log("HasActions %d", side ? port->HasActions(static_cast<C_Actor *>(Obj(op.a)), op.f)
                                  : OriginalAt<T_Has>(0x004b0f30)(orig, Obj(op.a), op.f));
        break;
    case OP_OPEN:
        side ? port->Open(op.a != 0) : OriginalAt<T_Open>(0x004b27b0)(orig, op.a != 0);
        break;
    case OP_PREPARE:
        side ? port->Prepare() : OriginalAt<T_Void>(0x004b3060)(orig);
        break;
    case OP_INPUT:
        side ? port->HandleInput(g_w.held, g_w.pressed) : OriginalAt<T_Input>(0x004b2dc0)(orig, g_w.held, g_w.pressed);
        // (The right button a digit presses.)
        Log("pressed roll menu %s", FloatText(g_w.pressed[INPUT_ROLL_MENU]).c_str());
        break;
    case OP_UPDATE:
        side ? port->Update(op.a) : OriginalAt<T_Int>(0x004b2fc0)(orig, op.a);
        break;
    case OP_RENDER:
        side ? port->Render() : OriginalAt<T_Void>(0x004b2fa0)(orig);
        break;
    case OP_RELAYOUT:
        side ? port->Relayout() : OriginalAt<T_Void>(0x004b3010)(orig);
        break;
    case OP_CURSOROVER:
        Log("IsCursorOver %d", side ? port->IsCursorOver() : OriginalAt<T_Bool>(0x004b2be0)(orig));
        break;
    case OP_SELECT:
        // (Past the entries: the port's fix, tested alone.)
        Log("Select %d", side ? port->Select(op.a) : OriginalAt<T_Select>(0x004b2ca0)(orig, op.a));
        break;
    case OP_SELECTAT:
        side ? port->SelectAt(point) : OriginalAt<T_Floats>(0x004b2ce0)(orig, point);
        break;
    case OP_MOVETO:
        side ? port->MoveTo(pos) : OriginalAt<T_Floats>(0x004b3030)(orig, &pos.x);
        break;
    case OP_REFRESH:
        side ? port->Refresh() : OriginalAt<T_Void>(0x004b0b20)(orig);
        break;
    case OP_BUILD:
        side ? port->BuildEntries() : OriginalAt<T_Void>(0x004b0ef0)(orig);
        break;
    case OP_HASMAP:
        Log("HasCommandMapActions %d", side ? port->HasCommandMapActions() : OriginalAt<T_Bool>(0x004b11f0)(orig));
        break;
    case OP_SETENTRY:
        side ? port->SetEntry(op.a, op.b, op.c) : OriginalAt<T_Entry>(0x004b13a0)(orig, op.a, op.b, op.c);
        break;
    case OP_ACTIVATE:
        side ? port->Activate() : OriginalAt<T_Void>(0x004b2830)(orig);
        break;
    case OP_PREVIOUS:
        side ? port->SelectPrevious() : OriginalAt<T_Void>(0x004b2f00)(orig);
        break;
    case OP_NEXT:
        side ? port->SelectNext() : OriginalAt<T_Void>(0x004b2f50)(orig);
        break;
    case OP_TARGET:
        view->m_target = static_cast<C_Actor *>(Obj(op.a));
        break;
    case OP_CHOOSE:
        view->m_selected = op.a;
        break;
    }
}
void RunScenario(const std::vector<S_Op> &ops)
{
    for (int side = 0; side < 2; ++side) {
        StartSide();
        uint8_t *orig = static_cast<uint8_t *>(std::calloc(1, 0x90));
        C_RollMenu *port = static_cast<C_RollMenu *>(std::calloc(1, sizeof(C_RollMenu)));
        uint8_t *self = side ? reinterpret_cast<uint8_t *>(port) : orig;
        // (The same start words in the fields Init does not set: the distance, the open time, the entries, the choice.)
        Put<uint32_t>(self, 0x00, g_startWords[0]);
        Put<uint32_t>(self, 0x18, g_startWords[1]);
        for (int k = 0; k < C_RollMenu::ENTRIES; ++k) {
            const uint32_t w = g_startWords[2 + k];
            // (Seats' actions now and then: without a target they are left out.)
            const int action = int(w % 0x36);
            Put<int>(self, 0x34 + 8 * k, w % 4 == 0 ? -1 : action >= 8 && action < 14 && w % 7 ? action + 6 : action);
            Put<int>(self, 0x38 + 8 * k, w % 5 == 0 ? -1 : int(0x1B00 + w % 0x100));
        }
        Put<int>(self, 0x84, int(g_startWords[0x14] % 11) - 1);
        // (And the target and the open flag: Init clears them.)
        Put<void *>(self, 0x88, Obj(int(g_startWords[0x15] % (OBJECTS + 1)) - 1));
        Put<uint8_t>(self, 0x30, uint8_t(g_startWords[0x16]));
        for (const S_Op &op : ops) {
            Log("-- %d %d %d %d %s %s", op.kind, op.a, op.b, op.c, FloatText(op.f).c_str(), FloatText(op.g).c_str());
            if (op.kind >= 100)
                ApplyWorld(op);
            SyncWorld(side);
            if (op.kind < 100)
                Run(side, port, orig, op);
            g_log.push_back(Snap(self));
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

int AnyObj() { return P(0.12) ? -1 : g_w.focus >= 0 && P(0.4) ? g_w.focus : I(0, OBJECTS - 1); }
float Distance() { return P(0.3) ? Pick({0.0f, 1.8f, 0.2f, 1.80001f, -1.0f, 100.0f}) : F(0, 3); }
void FillOp(S_Op &op)
{
    switch (op.kind) {
    case OP_SETTARGET:
    case OP_HASACTIONS:
        op.a = AnyObj();
        op.f = Distance();
        break;
    case OP_OPEN:
        op.a = P(0.75);
        break;
    case OP_UPDATE:
        op.a = P(0.5) ? Pick({0, 16, 400}) : I(-5, 1000);
        break;
    case OP_SELECT:
        op.a = I(-1, C_RollMenu::ENTRIES - 1);
        break;
    case OP_SELECTAT: {
        // (Mostly inside the bound, on a line; now and then far off: the conversion's corners.)
        const float *b = g_w.bound;
        op.f = P(0.15) ? Pick({b[0], b[3]}) : P(0.8) ? F(b[0], b[3]) : F(-2, 2);
        op.g = P(0.7) ? b[4] - 0.045f - 0.017f * (float(I(0, 11)) + F(0.05f, 0.95f)) : P(0.5) ? F(b[1], b[4]) : Pick({1e10f, -1e10f, b[4], b[1]});
        break;
    }
    case OP_MOVETO:
        op.f = F(-1, 1);
        op.g = F(-1, 1);
        break;
    case OP_SETENTRY:
        op.a = I(0, C_RollMenu::ENTRIES - 1);
        op.b = P(0.5) ? 0x1B00 + I(0, 0xFF) : P(0.5) ? I(-21, -10) : I(-1, 3);
        op.c = P(0.4) ? Pick({0x32, -1, 0x33, 6, 7, 8, 9, 0x1D}) : I(-5, 0x40);
        break;
    case OP_TARGET:
        op.a = AnyObj();
        break;
    case OP_CHOOSE:
        op.a = I(-1, C_RollMenu::ENTRIES - 1);
        break;
    case W_WORLD:
    case W_TEAM:
    case W_INPUT:
    case W_ROLE:
    case W_PLACE:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case W_OBJ:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, OBJECTS - 1);
        break;
    case W_HEALTH:
        op.a = P(0.6) && g_w.selected >= 0 ? g_w.selected : I(0, OBJECTS - 1);
        op.b = P(0.5) ? Pick({0, 1, -1}) : I(2, 800);
        break;
    case W_FRAME:
        op.a = P(0.5);
        op.b = P(0.5);
        break;
    }
}

// ---- the menu ----
void TestRollMenu()
{
    StartScenario();
    std::vector<S_Op> ops;
    S_Op init{OP_INIT};
    init.a = P(0.4);
    ops.push_back(init);
    // (Mostly aimed at the focus and opened (or its entries built straight), an entry chosen and acted on.)
    if (P(0.25)) {
        S_Op target{OP_TARGET};
        target.a = g_w.focus;
        ops.push_back(target);
        ops.push_back(S_Op{OP_BUILD});
        for (int n = I(0, 2); n > 0; --n) {
            S_Op choose{Pick({OP_CHOOSE, OP_SELECT, OP_NEXT})};
            FillOp(choose);
            ops.push_back(choose);
        }
        ops.push_back(S_Op{Pick({OP_ACTIVATE, OP_ACTIVATE, OP_INPUT, OP_REFRESH})});
    } else if (P(0.7)) {
        S_Op target{init.a ? OP_TARGET : OP_SETTARGET};
        target.a = g_w.focus;
        target.f = P(0.8) ? F(0, 1.8f) : Distance();
        ops.push_back(target);
        S_Op open{OP_OPEN};
        open.a = 1;
        ops.push_back(open);
        for (int n = I(0, 3); n > 0; --n) {
            S_Op choose{Pick({OP_CHOOSE, OP_SELECT, OP_NEXT, OP_PREVIOUS})};
            FillOp(choose);
            ops.push_back(choose);
        }
        if (P(0.6))
            ops.push_back(S_Op{Pick({OP_ACTIVATE, OP_ACTIVATE, OP_INPUT})});
    }
    static const int KINDS[] = {OP_SETTARGET, OP_SETTARGET, OP_HASACTIONS, OP_HASACTIONS, OP_OPEN, OP_OPEN, OP_PREPARE, OP_INPUT,
                                OP_INPUT, OP_INPUT, OP_UPDATE, OP_RENDER, OP_RELAYOUT, OP_CURSOROVER, OP_SELECT, OP_SELECTAT,
                                OP_SELECTAT, OP_MOVETO, OP_REFRESH, OP_BUILD, OP_BUILD, OP_HASMAP, OP_HASMAP, OP_SETENTRY,
                                OP_ACTIVATE, OP_ACTIVATE, OP_PREVIOUS, OP_NEXT, OP_TARGET, OP_TARGET, OP_CHOOSE, W_WORLD, W_OBJ,
                                W_OBJ, W_TEAM, W_INPUT, W_INPUT, W_ROLE, W_PLACE, W_HEALTH, W_FRAME};
    for (int n = I(0, 20); n > 0; --n) {
        S_Op op{Pick(KINDS)};
        FillOp(op);
        ops.push_back(op);
    }
    if (P(0.5))
        ops.push_back(S_Op{OP_DONE});
    SaveStart();
    RunScenario(ops);
}

// ---- the port's fixes, alone ----
// A supply box of more than ten items, a vehicle of more than nine seats: the entries' ten (the original wrote over the
// choice and the target); a player's name with '%': the name as text (the original used it as the format); Activate
// without a choice: no action (the original read the field before the entries); Select and SelectAt past the entries:
// none chosen (the original read past them).
void TestFixes()
{
    StartScenario();
    SaveStart();
    StartSide();
    SyncWorld(1);
    C_RollMenu menu{};
    CHECK(menu.Init(reinterpret_cast<C_Interface *>(g_ui), reinterpret_cast<I3D_scene *>(&g_scene), false), "Init: false");
    // The selected soldier on foot, a supply box of SUPPLY allied items before him.
    g_w.role = 7;
    g_w.selected = 1;
    S_Obj &s = g_w.obj[1];
    s.type = ACTOR_HUMAN;
    s.vehicle = s.carried = -1;
    s.team = 1;
    S_Obj &box = g_w.obj[0];
    box.type = ACTOR_SUPPLY_BOX;
    box.hasLists = 1;
    box.charges = 1;
    box.alliedCount = SUPPLY;
    SyncWorld(1);
    menu.m_target = static_cast<C_Actor *>(Obj(0));
    menu.m_selected = 4;
    menu.BuildActions();
    for (int k = 0; k < C_RollMenu::ENTRIES - 1; ++k)
        CHECK(menu.m_entries[k].textId == At<int>(g_itemDefs[box.allied[k]], 0x14), "supply box entry %d: %d", k, menu.m_entries[k].textId);
    CHECK(menu.m_selected == 4 && menu.m_target == Obj(0), "supply box: past the entries (%d %s)", menu.m_selected, Name(menu.m_target).c_str());
    // A car of more than nine seats.
    box.type = ACTOR_CAR;
    box.seatCount = 12;
    for (int &o : box.occupant)
        o = -1;
    SyncWorld(1);
    menu.BuildActions();
    CHECK(menu.m_selected == 4 && menu.m_target == Obj(0), "car of 12 seats: past the entries (%d %s)", menu.m_selected, Name(menu.m_target).c_str());
    // A player's name with '%'.
    S_Obj &player = g_w.obj[2];
    player.type = ACTOR_HUMAN;
    player.role = HUMAN_ROLE_PLAYER;
    std::snprintf(player.name, sizeof player.name, "%%s%%d%%n");
    SyncWorld(1);
    menu.m_target = static_cast<C_Actor *>(Obj(2));
    for (C_RollMenu::S_Entry &e : menu.m_entries)
        e = {-1, -1};
    g_log.clear();
    menu.Refresh();
    CHECK(std::find(g_log.begin(), g_log.end(), BoxName(menu.m_text) + " AddText '%s%d%n' 49") != g_log.end(), "name with %%: not as text");
    // Activate without a choice: no action, closed.
    menu.m_selected = -1;
    menu.m_home.w = FromBits(HUMAN_COMMAND_LEAVE_VEHICLE);
    g_log.clear();
    menu.Activate();
    for (const std::string &line : g_log)
        CHECK(line.find("IssueCommand") == std::string::npos, "Activate from -1: %s", line.c_str());
    // Select and SelectAt past the entries.
    for (C_RollMenu::S_Entry &e : menu.m_entries)
        e = {6, 0x1B00};
    menu.m_selected = 2;
    CHECK(!menu.Select(C_RollMenu::ENTRIES) && !menu.Select(-1) && menu.m_selected == 2, "Select past the entries");
    I3D_visual *v = static_cast<I3D_visual *>(FrameAt(F_MENU));
    v->VisualFlags() |= VISFLAGS_WBOUND_VALID;
    I3D_bbox &bound = *reinterpret_cast<I3D_bbox *>(reinterpret_cast<uint8_t *>(v) + 0x170);
    bound.min = S_vector(0, -1, 0);
    bound.max = S_vector(1, 0, 0);
    const float below[2] = {0.5f, -0.045f - 0.017f * 10.5f};
    menu.SelectAt(below);
    CHECK(menu.m_selected == -1, "SelectAt past the entries: %d", menu.m_selected);
    menu.Done();
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    SetUpGfx();
    g_graphVt[0xA0 / 4] = reinterpret_cast<void *>(G_AsciiKey);
    g_graphVt[0xCC / 4] = reinterpret_cast<void *>(G_KeyToChar);
    // The actors' vtables: the original's GetName (0x04), GetSeatOccupant (0x78), GetSeatCount (0x88), GetSeatText
    // (0x94); the port's from its member pointers.
    for (void *&v : g_origActorVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portActorVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origActorVt[0x04 / 4] = reinterpret_cast<void *>(V_GetName);
    g_origActorVt[0x78 / 4] = reinterpret_cast<void *>(V_SeatOccupant);
    g_origActorVt[0x88 / 4] = reinterpret_cast<void *>(V_SeatCount);
    g_origActorVt[0x94 / 4] = reinterpret_cast<void *>(V_SeatText);
    g_portActorVt[Slot(&C_Actor::GetName) / 4] = reinterpret_cast<void *>(V_GetName);
    g_portActorVt[Slot(&C_Actor::GetSeatOccupant) / 4] = reinterpret_cast<void *>(V_SeatOccupant);
    g_portActorVt[Slot(&C_Actor::GetSeatCount) / 4] = reinterpret_cast<void *>(V_SeatCount);
    g_portActorVt[Slot(&C_Actor::GetSeatText) / 4] = reinterpret_cast<void *>(V_SeatText);
    for (void *&v : g_origDefVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portDefVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origDefVt[0x18 / 4] = reinterpret_cast<void *>(V_AmmoItemId);
    g_portDefVt[Slot(&C_ItemDef::GetAmmoItemId) / 4] = reinterpret_cast<void *>(V_AmmoItemId);
    // The supply items' definitions (the original's table; the port's GetItemDef answers the same).
    for (int id = 0; id < 500; ++id) {
        Put<int>(g_itemDefs[id], 0x14, 0x3000 + id);
        OriginalAt<void **>(0x0098b030)[id] = g_itemDefs[id];
    }
    g_clickSound = reinterpret_cast<I3D_sound *>(FrameAt(F_CLICK));
    *OriginalAt<void **>(0x0089bc8c) = FrameAt(F_CLICK);
    *OriginalAt<void **>(0x008aea30) = g_settings;
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0045d160, reinterpret_cast<const void *>(O_Selected)},
        {0x0045d1b0, reinterpret_cast<const void *>(O_Commanded)},
        {0x00461a20, reinterpret_cast<const void *>(O_IsMember)},
        {0x005a78f0, reinterpret_cast<const void *>(O_FindItem)},
        {0x005a6aa0, reinterpret_cast<const void *>(O_CountItems)},
        {0x005a6c50, reinterpret_cast<const void *>(O_CountAmmo)},
        {0x005a7ed0, reinterpret_cast<const void *>(O_WireCutter)},
        {0x005a6730, reinterpret_cast<const void *>(O_BeltItem)},
        {0x005a2b10, reinterpret_cast<const void *>(O_HandsWeapon)},
        {0x005a7e00, reinterpret_cast<const void *>(O_Medkit)},
        {0x005a6450, reinterpret_cast<const void *>(O_Surrendered)},
        {0x005a6380, reinterpret_cast<const void *>(O_Unconscious)},
        {0x005a6420, reinterpret_cast<const void *>(O_InWater)},
        {0x005a5ff0, reinterpret_cast<const void *>(O_HealthPercent)},
        {0x005a5d10, reinterpret_cast<const void *>(O_Stance)},
        {0x005a8170, reinterpret_cast<const void *>(O_ViewMode)},
        {0x005a45c0, reinterpret_cast<const void *>(O_HasOrder)},
        {0x005a0980, reinterpret_cast<const void *>(O_Hostile)},
        {0x005a8820, reinterpret_cast<const void *>(O_Team)},
        {0x005a82c0, reinterpret_cast<const void *>(O_ShortName)},
        {0x005a45b0, reinterpret_cast<const void *>(O_Issue)},
        {0x005a45e0, reinterpret_cast<const void *>(O_SendCommand)},
        {0x005a2cf0, reinterpret_cast<const void *>(O_TakeFromBox)},
        {0x005a2d20, reinterpret_cast<const void *>(O_PickUpItem)},
        {0x005ac2b0, reinterpret_cast<const void *>(O_ClimbState)},
        {0x005ac310, reinterpret_cast<const void *>(O_WorkState5)},
        {0x004ddd10, reinterpret_cast<const void *>(O_CanEnter)},
        {0x004dde20, reinterpret_cast<const void *>(O_EnemyAboard)},
        {0x004ddcf0, reinterpret_cast<const void *>(O_Destroyed)},
        {0x0060b460, reinterpret_cast<const void *>(O_CanUse)},
        {0x006b69d0, reinterpret_cast<const void *>(O_Cooperative)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
        {0x00475d50, reinterpret_cast<const void *>(O_ActorById)},
        {0x007db3e0, reinterpret_cast<const void *>(O_CanPut)},
        {0x0049de90, reinterpret_cast<const void *>(O_ShowInventory)},
        {0x006c73f0, reinterpret_cast<const void *>(O_ClearAction)},
        {0x006c7510, reinterpret_cast<const void *>(O_ClearKey)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x007e5356, reinterpret_cast<const void *>(O_WBound)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("rollmenu_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_ROLLMENU_QUICK") ? 15000 : 40000;
    if (const char *r = std::getenv("HD2_ROLLMENU_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_ROLLMENU_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"RollMenu", TestRollMenu}, {"RollMenuFixes", TestFixes}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        // (The fixes' case: a twentieth of the rounds.)
        const int n = c.run == TestFixes ? std::max(1, rounds / 20) : rounds;
        for (g_round = 0; g_round < n; ++g_round)
            c.run();
    }
    std::printf("rollmenu_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
