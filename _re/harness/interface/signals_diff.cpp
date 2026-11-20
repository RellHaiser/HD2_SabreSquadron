// Human part 70: the signals panel (game/interface_signals.cpp: C_SignalsPanel) against the original's
// (0x004bf2e0..0x004c1200). MinGW; build_signals.bat; runs signals_diff.exe ("signals_diff: N checks, 0 failures").
// interface_signals.cpp is compiled into this file; the kit (interface_kit.inc, interface_gfx.inc) fakes the frames, the
// scene, the graph and the text boxes. Scenarios of the panel's operations (Init, Done, Open, IsOpen, SetEntry,
// HasTargetCommands, BuildPage, BuildTargetPage, Refresh, Activate, SelectNext, SelectPrevious, HandleInput, Issue,
// Render, SetTarget, Update, Relayout, OnTargetDeleted, AimPoint; between them the world changing) run from one start
// on each side (the same random words first), every call out logged by the same fakes, the panel's fields snapped
// after each operation, the frames compared at the end.
// The world: 12 objects of any actor type (humans, cars, doors, radios, supply boxes, places, animations ...), each
// side its own blocks (the port's actors are laid out their own way: the type at +0x0C, not +0x18) with the same
// contents and per-side vtables (GetName, HasSeatWeapon; the port's slots from its member pointers). Faked on both
// sides with the same loggers: the player's team (GetSelectedMember, GetMember, GetMemberCount, IsMember, HasItem,
// HasMedkitBesides, GetAliveCount), the humans (FindItem, CountItems, FindWireCutter, GetHandsWeapon (the weapon's
// definition's GetAmmoItemId), GetMedkit, GetOrderCount, IsSurrendered, IsUnconscious, GetFormationType, GetShortName,
// GetViewDir, PlayerSendActionCommand, the body's RemoveCollision and RestoreCollision), the cars (CanEnter,
// HasEnemyAboard, IsDestroyed), the squad commander (CollectGroup, IsSpacingMin, IsSpacingMax), the interface's
// HighlightMembers, the items module's CanAddItemToInventory and definitions (the original's table 0x0098b030), the
// controls' ClearAction and ClearKey, the graph's GetAsciiKey and KeyToChar, the game's camera module (GetCameraPos,
// GetViewDistance), the collision world's RayTest, the click sound (a frame), the game role and type.
// HD2_SIGNALS_ROUNDS: scenarios per case (default 20,000; HD2_SIGNALS_QUICK: 5,000); HD2_SIGNALS_CASE: one case only;
// HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW, HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
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
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/interface_signals.cpp"
#include "game/animation_actor.h"
#include "game/car.h"
#include "game/document_place.h"
#include "game/door.h"
#include "game/explosive_place.h"
#include "game/radio.h"
#include "game/supply_box.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

// ---- the frames: f0 "signals", f1 the click sound ----
constexpr int F_SIGNALS = 0, F_CLICK = 1;
bool g_missingFrame;
I3D_frame *FindFrame(const char *name) { return !g_missingFrame && std::strcmp(name, "signals") == 0 ? FrameAt(F_SIGNALS) : nullptr; }

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
constexpr int OBJECTS = 12, MEMBERS = 8, SUPPLY = 16;
// The item ids the humans and the team may have (FindItem, CountItems, CanAddItemToInventory, HasItem), the supply
// boxes' ones among them (the original's table has their definitions: 0..499).
const int ITEM_IDS[] = {0xE5, 0xF3, 0xF4, 1, 2, 3, 4, 5, 6, 0, -1, 7, 12, 50, 250, 499};
constexpr int IDS = int(sizeof ITEM_IDS / sizeof ITEM_IDS[0]);
const int SUPPLY_IDS[] = {3, 7, 12, 50, 250, 499, 0xE5, 0xF3};
int IdIndex(int id)
{
    for (int k = 0; k < IDS; ++k)
        if (ITEM_IDS[k] == id)
            return k;
    return -1;
}
const int TYPES[] = {ACTOR_SUPPLY_BOX, ACTOR_DOOR, ACTOR_MINE, ACTOR_HUMAN, ACTOR_CAR, ACTOR_DOOR, ACTOR_RADIO, ACTOR_LADDER, ACTOR_SUPPLY_BOX,
                     ACTOR_ITEM, ACTOR_SWITCH, ACTOR_PHOTO_PLACE, ACTOR_SUBMARINE, ACTOR_STATIC_WEAPON,
                     ACTOR_EXPLOSIVE_PLACE, ACTOR_DOCUMENT_PLACE, ACTOR_PIKE, ACTOR_MELEE_TRACKER, ACTOR_WIRE_CUT,
                     ACTOR_ANIMATION, ACTOR_BARREL, ACTOR_SPAWN_POINT, ACTOR_CLOCK, ACTOR_NULL, ACTOR_TS_EXPLOSION, 0, 1,
                     16, 33, 34, 40};
struct S_Obj {
    int type;
    char name[12];   // GetName and GetShortName
    // a human's
    int health, vehicle, seat, carried, orders, formation, weapon;   // weapon: -2 none, else the ammo's id
    bool unconscious, surrendered, u1a5, medkit, wireCutter;
    uint32_t items;   // FindItem: bits over ITEM_IDS
    int counts[IDS], canAdd[IDS];
    float view[4];
    // a car's
    bool canEnter, enemyAboard, destroyed;
    uint32_t seatWeapons;
    // a door's
    bool locked, pickable, u118, portal, otherPortal;
    int key, motion;
    // a radio's, an explosive place's, a document place's
    int state, itemId, textId;
    // a supply box's
    int charges, maxCarried, hasLists, supplyCount, supply[SUPPLY];
    // an animation's
    bool stopped, noInfo;
};
struct S_World {
    S_Obj obj[OBJECTS];
    int members[MEMBERS], memberCount, selected;   // object indices (-1: none)
    uint32_t teamItems;                            // HasItem: bits over ITEM_IDS
    uint32_t medkitBesides;                        // HasMedkitBesides: bits over the objects
    int group[OBJECTS], groupCount;                // CollectGroup's members
    bool spacingMin, spacingMax;
    float cameraPos[4], viewDistance, hit[3];
    bool rayHit;
    int key;
    uint32_t keyChar;
    float held[64], pressed[64];
    uint32_t role;
    int gameType;
    int car;   // a car (or a submarine, a static weapon) some members are in, or a supply box (-1: none)
};
S_World g_w, g_wStart;

// ---- each side's objects ----
alignas(16) uint8_t g_orig[OBJECTS][0x400], g_port[OBJECTS][0x400];
alignas(16) uint8_t g_origWeapon[OBJECTS][0x20], g_portWeapon[OBJECTS][0x20];   // the hands weapons (+4 the definition)
alignas(16) uint8_t g_origWDef[OBJECTS][0x40], g_portWDef[OBJECTS][0x40];       // their definitions (vtables)
alignas(16) uint8_t g_itemDefs[500][0x20];                                       // the supply items' (+0x14 the text)
alignas(16) uint8_t g_portCommander[0x40], g_origCommander[0x40], g_origCamera[0x40], g_game[0x40], g_ui[0x40];
alignas(16) uint8_t g_settings[0x400];
void *g_origActorVt[0x40], *g_portActorVt[0x80], *g_origDefVt[16], *g_portDefVt[32], *g_origCommanderVt[32],
    *g_portCommanderVt[32], *g_origCameraVt[32];
C_str g_names[OBJECTS];
int g_side;   // the side running

void *Obj(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_port[i]) : static_cast<void *>(g_orig[i]); }
int ObjIndex(const void *p)
{
    for (int i = 0; i < OBJECTS; ++i)
        if (p == g_orig[i] || p == g_port[i])
            return i;
    return -1;
}
int BodyIndex(const void *body)   // the humans' bodies: the original's at +0x98, the port's at +0x74
{
    for (int i = 0; i < OBJECTS; ++i)
        if (body == g_orig[i] + 0x98 || body == g_port[i] + offsetof(C_Human, m_body))
            return i;
    return -1;
}
const void *OrigTeam() { return reinterpret_cast<const void *>(0x0089ae78 + 0x1C); }
std::string PartName(const void *p)
{
    char text[24];
    const int i = ObjIndex(p);
    if (i >= 0) {
        std::snprintf(text, sizeof text, "o%d", i);
        return text;
    }
    for (int k = 0; k < OBJECTS; ++k)
        if (p == g_origWeapon[k] || p == g_portWeapon[k]) {
            std::snprintf(text, sizeof text, "w%d", k);
            return text;
        }
    if (p == g_ui)
        return "ui";
    if (p == OrigTeam() || p == &g_teamManager.m_playerTeam)
        return "team";
    return "";
}

// ---- the team ----
const S_Obj &O(const void *p) { return g_w.obj[ObjIndex(p)]; }
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
void *Member(int index)
{
    Log("team GetMember %d", index);
    return index >= 0 && index < g_w.memberCount ? Obj(g_w.members[index]) : nullptr;
}
int MemberCount()
{
    Log("team GetMemberCount");
    return g_w.memberCount;
}
bool IsMember(const void *h)
{
    Log("team IsMember %s", Name(h).c_str());
    return IsTeamMember(ObjIndex(h));
}
bool TeamHasItem(int id)
{
    Log("team HasItem %d", id);
    const int k = IdIndex(id);
    return k >= 0 && (g_w.teamItems >> k & 1);
}
bool MedkitBesides(const void *h)
{
    Log("team HasMedkitBesides %s", Name(h).c_str());
    return g_w.medkitBesides >> ObjIndex(h) & 1;
}
int AliveCount()
{
    Log("team GetAliveCount");
    int n = 0;
    for (int k = 0; k < g_w.memberCount; ++k)
        n += g_w.obj[g_w.members[k]].health > 0;
    return n;
}
void *__attribute__((thiscall)) O_Selected(void *) { return Selected(); }
void *__attribute__((thiscall)) O_Member(void *, int index) { return Member(index); }
int __attribute__((thiscall)) O_MemberCount(void *) { return MemberCount(); }
bool __attribute__((thiscall)) O_IsMember(void *, void *h) { return IsMember(h); }
bool __attribute__((thiscall)) O_TeamHasItem(void *, int id) { return TeamHasItem(id); }
bool __attribute__((thiscall)) O_MedkitBesides(void *, void *h) { return MedkitBesides(h); }
int __attribute__((thiscall)) O_AliveCount(void *) { return AliveCount(); }

// ---- the humans ----
void *FindItem(const void *h, int id)
{
    Log("%s FindItem %d", Name(h).c_str(), id);
    const int k = IdIndex(id);
    return k >= 0 && (O(h).items >> k & 1) ? static_cast<void *>(g_side ? g_portWeapon[ObjIndex(h)] : g_origWeapon[ObjIndex(h)]) : nullptr;
}
int CountItems(const void *h, int id)
{
    Log("%s CountItems %d", Name(h).c_str(), id);
    const int k = IdIndex(id);
    return k >= 0 ? O(h).counts[k] : 0;
}
void *WireCutter(const void *h)
{
    Log("%s FindWireCutter", Name(h).c_str());
    return O(h).wireCutter ? g_side ? g_portWeapon[ObjIndex(h)] : g_origWeapon[ObjIndex(h)] : nullptr;
}
void *HandsWeapon(const void *h)
{
    Log("%s GetHandsWeapon", Name(h).c_str());
    const int i = ObjIndex(h);
    return O(h).weapon == -2 ? nullptr : g_side ? static_cast<void *>(g_portWeapon[i]) : static_cast<void *>(g_origWeapon[i]);
}
void *Medkit(const void *h)
{
    Log("%s GetMedkit", Name(h).c_str());
    return O(h).medkit ? g_side ? g_portWeapon[ObjIndex(h)] : g_origWeapon[ObjIndex(h)] : nullptr;
}
int OrderCount(const void *h)
{
    Log("%s GetOrderCount", Name(h).c_str());
    return O(h).orders;
}
bool Surrendered(const void *h)
{
    Log("%s IsSurrendered", Name(h).c_str());
    return O(h).surrendered;
}
bool Unconscious(const void *h)
{
    Log("%s IsUnconscious", Name(h).c_str());
    return O(h).unconscious;
}
int Formation(const void *h)
{
    Log("%s GetFormationType", Name(h).c_str());
    return O(h).formation;
}
const char *ShortName(const void *h)
{
    Log("%s GetShortName", Name(h).c_str());
    return O(h).name;
}
const float *ViewDir(const void *h)
{
    Log("%s GetViewDir", Name(h).c_str());
    return O(h).view;
}
std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }
// (The command's dir: x, y, z; its w is the word after the view direction or a stack word in the original, 0 in the
// port: not used (the message writes x, y, z).)
void SendCommand(const void *h, int category, const float *dir, int action, int command, const void *actor, int value,
                 const void *member)
{
    Log("%s PlayerSendActionCommand category %d dir %s action %d command %d actor %s value %d member %s", Name(h).c_str(),
        category, F3(dir).c_str(), action, command, Name(actor).c_str(), value, Name(member).c_str());
}
void Body(const void *body, const char *what, bool b) { Log("o%d %s %d", BodyIndex(body), what, b); }
void *__attribute__((thiscall)) O_FindItem(void *h, int id, int *container)
{
    if (container)
        Log("FindItem container");
    return FindItem(h, id);
}
int __attribute__((thiscall)) O_CountItems(void *h, int id) { return CountItems(h, id); }
void *__attribute__((thiscall)) O_WireCutter(void *h) { return WireCutter(h); }
void *__attribute__((thiscall)) O_HandsWeapon(void *h) { return HandsWeapon(h); }
void *__attribute__((thiscall)) O_Medkit(void *h) { return Medkit(h); }
int __attribute__((thiscall)) O_OrderCount(void *h) { return OrderCount(h); }
bool __attribute__((thiscall)) O_Surrendered(void *h) { return Surrendered(h); }
bool __attribute__((thiscall)) O_Unconscious(void *h) { return Unconscious(h); }
int __attribute__((thiscall)) O_Formation(void *h) { return Formation(h); }
const char *__attribute__((thiscall)) O_ShortName(void *h) { return ShortName(h); }
const float *__attribute__((thiscall)) O_ViewDir(void *h) { return ViewDir(h); }
void __attribute__((thiscall)) O_SendCommand(void *h, const uint8_t *c)
{
    SendCommand(h, At<int>(c, 4), reinterpret_cast<const float *>(c + 0x10), At<int>(c, 0x20), At<int>(c, 0x24), At<void *>(c, 0x28),
                At<int>(c, 0x2C), At<void *>(c, 0x30));
}
void __attribute__((thiscall)) O_RemoveCollision(void *body, bool b) { Body(body, "RemoveCollision", b); }
void __attribute__((thiscall)) O_RestoreCollision(void *body, bool b) { Body(body, "RestoreCollision", b); }

// ---- the cars and the actors' virtuals ----
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
C_str *GetName(const void *a)
{
    Log("%s GetName", Name(a).c_str());
    return &g_names[ObjIndex(a)];
}
bool HasSeatWeapon(const void *a, int seat)
{
    Log("%s HasSeatWeapon %d", Name(a).c_str(), seat);
    return seat >= 0 && seat < 32 && (O(a).seatWeapons >> seat & 1);
}
bool __attribute__((thiscall)) O_CanEnter(void *car) { return CanEnter(car); }
bool __attribute__((thiscall)) O_EnemyAboard(void *car, void *h) { return EnemyAboard(car, h); }
bool __attribute__((thiscall)) O_Destroyed(void *car) { return Destroyed(car); }
C_str *__attribute__((thiscall)) V_GetName(void *a) { return GetName(a); }
bool __attribute__((thiscall)) V_HasSeatWeapon(void *a, int seat) { return HasSeatWeapon(a, seat); }
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

// ---- the squad commander, the interface, the items module, the controls, the graph ----
void CollectGroup(int group) { Log("commander CollectGroup %d -> %d", group, g_w.groupCount); }
void __attribute__((thiscall)) O_CollectGroup(void *, void **vector, int group)
{
    CollectGroup(group);
    if (g_w.groupCount) {
        void **v = static_cast<void **>(std::malloc(sizeof(void *) * size_t(g_w.groupCount)));
        for (int k = 0; k < g_w.groupCount; ++k)
            v[k] = Obj(g_w.group[k]);
        vector[1] = v;
        vector[2] = vector[3] = v + g_w.groupCount;
    }
}
void __attribute__((thiscall)) P_CollectGroup(void *, std::vector<C_Human *> *out, int group)
{
    CollectGroup(group);
    for (int k = 0; k < g_w.groupCount; ++k)
        out->push_back(static_cast<C_Human *>(Obj(g_w.group[k])));
}
bool SpacingMin()
{
    Log("commander IsSpacingMin");
    return g_w.spacingMin;
}
bool SpacingMax()
{
    Log("commander IsSpacingMax");
    return g_w.spacingMax;
}
bool __attribute__((thiscall)) O_SpacingMin(void *) { return SpacingMin(); }
bool __attribute__((thiscall)) O_SpacingMax(void *) { return SpacingMax(); }
void *__cdecl O_GetSquadCommander() { return g_origCommander; }
void Highlight(const void *ui, const void *member, int group) { Log("%s HighlightMembers %s %d", Name(ui).c_str(), Name(member).c_str(), group); }
void __attribute__((thiscall)) O_Highlight(void *ui, void *member, int group) { Highlight(ui, member, group); }
int CanAdd(const void *actor, int id)
{
    Log("items CanAddItemToInventory %s %d", Name(actor).c_str(), id);
    const int k = IdIndex(id);
    return k >= 0 ? O(actor).canAdd[k] : 0;
}
int __attribute__((thiscall)) O_CanAdd(void *, void *actor, int id) { return CanAdd(actor, id); }
int g_heldIndex(const float *held) { return held == g_w.held ? 0 : -1; }
void Clear(const char *what, int n, const float *held, const float *pressed)
{
    Log("controls %s %d %s %s", what, n, held == g_w.held ? "held" : "?", pressed == g_w.pressed ? "pressed" : "?");
}
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

// ---- the game's camera module, the collision world ----
bool CameraPos(int index, float *pos)
{
    Log("camera GetCameraPos %d", index);
    std::memcpy(pos, g_w.cameraPos, 16);
    return true;
}
float ViewDistance()
{
    Log("camera GetViewDistance");
    return g_w.viewDistance;
}
void *__cdecl O_GetGame() { return g_game; }
void *__attribute__((thiscall)) O_GetCameraModule(void *) { return g_origCamera; }
bool __attribute__((thiscall)) O_CameraPos(void *, int index, float *pos) { return CameraPos(index, pos); }
float __attribute__((thiscall)) O_ViewDistance(void *) { return ViewDistance(); }
void *RayTest(const float *from, const float *dir, float *hit, const void *normal, uint32_t mask, uint32_t flags)
{
    Log("collision RayTest %s %s %s %x %x", F3(from).c_str(), F3(dir).c_str(), normal ? "normal" : "null", mask, flags);
    if (!g_w.rayHit)
        return nullptr;
    std::memcpy(hit, g_w.hit, 12);
    return g_game;   // (a volume: not looked into)
}
void *__attribute__((thiscall)) O_RayTest(void *, const float *from, const float *dir, float *hit, void *normal, uint32_t mask,
                                         uint32_t flags)
{
    return RayTest(from, dir, hit, normal, mask, flags);
}

}  // namespace H

// ---- the port's side of the same fakes ----
I3D_sound *g_clickSound;
static S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(H::g_settings);
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;
C_Human *C_Team::GetSelectedMember() const { return static_cast<C_Human *>(H::Selected()); }
C_Human *C_TeamBase::GetMember(int index) const { return static_cast<C_Human *>(H::Member(index)); }
int C_TeamBase::GetMemberCount() const { return H::MemberCount(); }
bool C_TeamBase::IsMember(const C_Human *human) const { return H::IsMember(human); }
bool C_Team::HasItem(int itemId) const { return H::TeamHasItem(itemId); }
bool C_Team::HasMedkitBesides(const C_Human *human) const { return H::MedkitBesides(human); }
int C_TeamBase::GetAliveCount() const { return H::AliveCount(); }
C_Item *C_Human::FindItem(int typeId, int *container) const
{
    if (container)
        H::Log("FindItem container");
    return static_cast<C_Item *>(H::FindItem(this, typeId));
}
int C_Human::CountItems(int typeId) const { return H::CountItems(this, typeId); }
C_Item *C_Human::FindWireCutter() const { return static_cast<C_Item *>(H::WireCutter(this)); }
C_WeaponItem *C_Human::GetHandsWeapon() const { return static_cast<C_WeaponItem *>(H::HandsWeapon(this)); }
C_Item *C_Human::GetMedkit() const { return static_cast<C_Item *>(H::Medkit(this)); }
int C_Human::GetOrderCount() const { return H::OrderCount(this); }
bool C_Human::IsSurrendered() const { return H::Surrendered(this); }
bool C_Human::IsUnconscious() const { return H::Unconscious(this); }
int C_Human::GetFormationType() const { return H::Formation(this); }
const char *C_Human::GetShortName() const { return H::ShortName(this); }
const S_vector &C_Human::GetViewDir() const { return *reinterpret_cast<const S_vector *>(H::ViewDir(this)); }
void C_Human::PlayerSendActionCommand(const S_ActionCommand &c)
{
    H::SendCommand(this, c.category, &c.dir.x, c.action, c.command, c.actor, c.value, c.member);
}
void S_HumanBody::RemoveCollision(bool b) { H::Body(this, "RemoveCollision", b); }
void S_HumanBody::RestoreCollision(bool b) { H::Body(this, "RestoreCollision", b); }
bool C_Car::CanEnter() { return H::CanEnter(this); }
bool C_Car::HasEnemyAboard(const C_Human *human) { return H::EnemyAboard(this, human); }
bool C_Car::IsDestroyed() const { return H::Destroyed(this); }
void C_Interface::HighlightMembers(C_Human *human, int group) { H::Highlight(this, human, group); }
int C_ItemsModule::CanAddItemToInventory(C_Actor *actor, int itemId) { return H::CanAdd(actor, itemId); }
C_ItemDef *GetItemDef(int typeId)
{
    return typeId >= 0 && typeId < 500 ? reinterpret_cast<C_ItemDef *>(H::g_itemDefs[typeId]) : nullptr;
}
void C_Controls::ClearAction(int action, float *held, float *pressed) { H::Clear("ClearAction", action, held, pressed); }
void C_Controls::ClearKey(int key, float *held, float *pressed) { H::Clear("ClearKey", key, held, pressed); }
C_SquadCommander *GetSquadCommander() { return reinterpret_cast<C_SquadCommander *>(H::g_portCommander); }
void C_SquadCommander::CollectGroup(std::vector<C_Human *> &out, int group) { H::P_CollectGroup(this, &out, group); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_game); }
C_CameraModule *C_game::GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_origCamera); }
bool C_CameraModule::GetCameraPos(int index, S_vector4 *pos) { return H::CameraPos(index, &pos->x); }
float C_CameraModule::GetViewDistance() { return H::ViewDistance(); }
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal,
                                             uint32_t mask, uint32_t flags) const
{
    return static_cast<S_CollisionVolume *>(H::RayTest(&from.x, &dir.x, &hitPos->x, hitNormal, mask, flags));
}

namespace H {

// ---- building each side's objects from the world ----
void PortVectorOf(void *vector, const int *begin, int n)   // (libstdc++'s: start, finish, end of storage)
{
    const int **v = static_cast<const int **>(vector);
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
            Put<int>(b, 0x18, o.type);
            if (o.type == ACTOR_HUMAN) {
                Put<int>(b, 0xEC, o.health);
                Put<void *>(b, 0x160, Obj(o.vehicle));
                Put<int>(b, 0x164, o.seat);
                Put<void *>(b, 0x174, Obj(o.carried));
                Put<bool>(b, 0x1A5, o.u1a5);
            } else if (o.type == ACTOR_DOOR) {
                Put<int>(b, 0x224, o.motion);
                Put<int>(b, 0x230, o.key);
                Put<bool>(b, 0x234, o.locked);
                Put<bool>(b, 0x235, o.pickable);
                Put<bool>(b, 0x118, o.u118);
                Put<void *>(b, 0xFC, o.portal ? g_game : nullptr);
                Put<void *>(b, 0x100, o.otherPortal ? g_game : nullptr);
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
            } else if (o.type == ACTOR_SUPPLY_BOX) {
                Put<int>(b, 0x130, o.charges);
                Put<int>(b, 0x11C, o.hasLists);
                Put<int>(b, 0x128, o.maxCarried);
                Put<const int *>(b, 0x110, o.supplyCount ? o.supply : nullptr);
                Put<const int *>(b, 0x114, o.supplyCount ? o.supply + o.supplyCount : nullptr);
                Put<const int *>(b, 0x118, o.supplyCount ? o.supply + o.supplyCount : nullptr);
            }
            Put<void *>(g_origWeapon[i], 4, g_origWDef[i]);
            Put<void *>(g_origWDef[i], 0, g_origDefVt);
        } else {
            uint8_t *b = g_port[i];
            std::memset(b, 0, 0x400);
            Put<void *>(b, 0, g_portActorVt);
            reinterpret_cast<C_Actor *>(b)->m_type = E_ActorType(o.type);
            if (o.type == ACTOR_HUMAN) {
                C_Human *h = reinterpret_cast<C_Human *>(b);
                h->m_stats[STAT_HEALTH] = o.health;
                h->m_vehicle = static_cast<C_Actor *>(Obj(o.vehicle));
                h->m_seat = o.seat;
                h->m_carried = static_cast<C_Actor *>(Obj(o.carried));
                h->m_unknown1a5 = o.u1a5;
            } else if (o.type == ACTOR_DOOR) {
                C_Door *d = reinterpret_cast<C_Door *>(b);
                d->m_motion = o.motion;
                d->m_key = o.key;
                d->m_locked = o.locked;
                d->m_pickable = o.pickable;
                d->m_unknown118 = o.u118;
                d->m_portal = o.portal ? reinterpret_cast<I3D_portal *>(g_game) : nullptr;
                d->m_otherPortal = o.otherPortal ? reinterpret_cast<I3D_portal *>(g_game) : nullptr;
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
            } else if (o.type == ACTOR_SUPPLY_BOX) {
                C_SupplyBox *s = reinterpret_cast<C_SupplyBox *>(b);
                s->m_charges = o.charges;
                s->m_hasItemLists = o.hasLists;
                s->m_maxCarried = o.maxCarried;
                PortVectorOf(&s->m_alliedItems, o.supply, o.supplyCount);
            }
            reinterpret_cast<C_Item *>(g_portWeapon[i])->m_def = reinterpret_cast<C_ItemDef *>(g_portWDef[i]);
            Put<void *>(g_portWDef[i], 0, g_portDefVt);
        }
    }
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    Put<int>(g_settings, 0x224, g_w.gameType);
}

const char *const NAMES[] = {"Bob", "Ann", "x", "Sgt. Lee", "", "Jo-Jo", "Mac", "A b c", "Ivan", "Pvt 1", "Kay", "Zed"};
void RandomObj(int i)
{
    S_Obj &o = g_w.obj[i];
    o = S_Obj{};
    o.type = P(0.45) ? int(ACTOR_HUMAN) : Pick(TYPES);
    std::snprintf(o.name, sizeof o.name, "%s", NAMES[I(0, int(sizeof NAMES / sizeof NAMES[0]) - 1)]);
    o.health = P(0.75) ? I(1, 800) : Pick({0, -1, 1});
    o.vehicle = P(0.7) ? -1 : I(0, OBJECTS - 1);
    o.seat = P(0.8) ? I(0, 5) : Pick({-1, 31, 7});
    o.carried = P(0.75) ? -1 : I(0, OBJECTS - 1);
    o.orders = P(0.6) ? 0 : Pick({1, 2, 14, -1});
    o.formation = P(0.6) ? Pick({0, 1, 3, 0xD}) : I(-1, 15);
    o.weapon = P(0.2) ? -2 : P(0.7) ? Pick(ITEM_IDS) : Pick({-1, 0, 1000});
    o.unconscious = P(0.25);
    o.surrendered = P(0.3);
    o.u1a5 = P(0.4);
    o.medkit = P(0.4);
    o.wireCutter = P(0.5);
    o.items = U32() & (P(0.3) ? 0xFFFFu : 0x0Fu << I(0, 12));
    for (int k = 0; k < IDS; ++k) {
        o.counts[k] = P(0.5) ? I(0, 6) : Pick({0, 1, 10, 100});
        o.canAdd[k] = P(0.5) ? Pick({0, 1, -1}) : I(-3, 5);
    }
    for (float &v : o.view)
        v = P(0.15) ? Pick({0.0f, 1.0f, -1.0f}) : F(-1, 1);
    o.canEnter = P(0.5);
    o.enemyAboard = P(0.3);
    o.destroyed = P(0.3);
    o.seatWeapons = U32();
    o.locked = P(0.4);
    o.pickable = P(0.5);
    o.u118 = P(0.3);
    o.portal = P(0.6);
    o.otherPortal = P(0.4);
    o.key = P(0.7) ? Pick(ITEM_IDS) : I(-5, 600);
    o.motion = P(0.6) ? 0 : I(-1, 3);
    o.state = P(0.7) ? I(0, 2) : I(-1, 4);
    o.itemId = P(0.8) ? Pick(ITEM_IDS) : I(-5, 600);
    o.textId = P(0.5) ? 0x1B00 + I(0, 0xFF) : int(U32());
    o.charges = P(0.6) ? I(0, 3) : Pick({0, -1, 100});
    o.maxCarried = P(0.6) ? I(0, 6) : Pick({0, 1, -1, 100});
    o.hasLists = P(0.5) ? 0 : Pick({1, 2, -1});
    o.supplyCount = P(0.15) ? 0 : I(1, C_SignalsPanel::ENTRIES);   // (more than ENTRIES: the port's fix, tested alone)
    for (int k = 0; k < SUPPLY; ++k)
        o.supply[k] = Pick(SUPPLY_IDS);
    o.stopped = P(0.5);
    o.noInfo = P(0.4);
}
void RandomTeam()
{
    g_w.memberCount = P(0.1) ? I(0, 1) : I(2, MEMBERS);
    for (int k = 0; k < g_w.memberCount; ++k)
        g_w.members[k] = I(0, OBJECTS - 1);
    // (Mostly humans: the team's members.)
    for (int k = 0; k < g_w.memberCount; ++k)
        if (P(0.9))
            g_w.obj[g_w.members[k]].type = ACTOR_HUMAN;
    g_w.selected = P(0.08) ? -1 : g_w.memberCount && P(0.9) ? g_w.members[I(0, g_w.memberCount - 1)] : I(0, OBJECTS - 1);
    if (g_w.selected >= 0 && P(0.9))
        g_w.obj[g_w.selected].type = ACTOR_HUMAN;
    if (g_w.selected >= 0 && P(0.85))
        g_w.obj[g_w.selected].health = std::max(g_w.obj[g_w.selected].health, 1);
    g_w.teamItems = U32();
    g_w.medkitBesides = U32();
}
void RandomGroup()
{
    g_w.groupCount = P(0.3) ? 0 : I(1, 5);
    for (int k = 0; k < g_w.groupCount; ++k)
        g_w.group[k] = P(0.7) && g_w.memberCount ? g_w.members[I(0, g_w.memberCount - 1)] : I(0, OBJECTS - 1);
    g_w.spacingMin = P(0.3);
    g_w.spacingMax = P(0.3);
}
void RandomInput()
{
    g_w.key = P(0.7) ? I(0, 0x60) : int(U32());
    const uint32_t c = P(0.6) ? uint32_t('0' + I(0, 9)) : P(0.5) ? Pick({0u, uint32_t('a'), uint32_t('/'), uint32_t(':'), 0xB0u}) : U32() & 0xFF;
    g_w.keyChar = P(0.2) ? c | (U32() & 0xFFFFFF00u) : c;
    for (float &f : g_w.held)
        f = P(0.8) ? 0.0f : 1.0f;
    for (float &f : g_w.pressed)
        f = P(0.8) ? 0.0f : 1.0f;
    g_w.pressed[INPUT_ROLL_MENU] = P(0.6) ? 0.0f : Pick({1.0f, -1.0f, 0.5f, -0.0f});
    g_w.pressed[INPUT_WHEEL] = P(0.4) ? 0.0f : Pick({1.0f, -1.0f, 120.0f, -120.0f, -0.0f});
}
void RandomCamera()
{
    for (float &f : g_w.cameraPos)
        f = F(-500, 500);
    g_w.viewDistance = P(0.3) ? Pick({0.0f, 40.0f, 100.0f}) : F(1, 1000);
    g_w.rayHit = P(0.5);
    for (float &f : g_w.hit)
        f = F(-500, 500);
}
void RandomRole()
{
    g_w.role = P(0.65) ? 7u : Pick({0u, 1u, 2u, 3u, 6u, 0xFu, 0x17u});
    g_w.gameType = P(0.3) ? MP_GAME_DEATHMATCH : I(0, 6);
}
void RandomWorld()
{
    for (int i = 0; i < OBJECTS; ++i)
        RandomObj(i);
    RandomTeam();
    // (Now and then a car with members aboard: the car's page for them.)
    if (P(0.4)) {
        g_w.car = I(0, OBJECTS - 1);
        g_w.obj[g_w.car].type = P(0.8) ? int(ACTOR_CAR) : Pick({int(ACTOR_SUBMARINE), int(ACTOR_STATIC_WEAPON)});
        for (int k = 0; k < g_w.memberCount; ++k)
            if (g_w.members[k] != g_w.car && P(0.6)) {
                // (Some in another vehicle: its seat weapon.)
                g_w.obj[g_w.members[k]].vehicle = P(0.8) ? g_w.car : I(0, OBJECTS - 1);
                if (P(0.7))
                    g_w.obj[g_w.members[k]].orders = 0;
            }
    } else if (P(0.3)) {
        // (Or a supply box with charges: its items' page or its ammunition's (a member's count at the box's most).)
        g_w.car = I(0, OBJECTS - 1);
        S_Obj &box = g_w.obj[g_w.car];
        box.type = ACTOR_SUPPLY_BOX;
        box.hasLists = P(0.5);
        box.charges = P(0.8) ? I(1, 3) : 0;
        for (int k = 0; k < g_w.memberCount; ++k) {
            S_Obj &m = g_w.obj[g_w.members[k]];
            m.weapon = Pick({0, 1, 2, 3});
            m.counts[IdIndex(m.weapon)] = box.maxCarried + I(-1, 1);
        }
    } else if (P(0.5)) {
        // (Or another target the target page knows.)
        g_w.car = I(0, OBJECTS - 1);
        g_w.obj[g_w.car].type = Pick({int(ACTOR_DOOR), int(ACTOR_RADIO), int(ACTOR_EXPLOSIVE_PLACE), int(ACTOR_WIRE_CUT),
                                     int(ACTOR_DOCUMENT_PLACE), int(ACTOR_PHOTO_PLACE), int(ACTOR_HUMAN), int(ACTOR_ANIMATION)});
        // (A radio ready, explosives planted or not: their commands.)
        S_Obj &focus = g_w.obj[g_w.car];
        focus.state = P(0.5) ? 1 : 0;
        // (A body a member carries.)
        if (focus.type == ACTOR_HUMAN && P(0.5) && g_w.memberCount) {
            focus.health = 0;
            for (int k = 0; k < g_w.memberCount; ++k)
                if (P(0.5))
                    g_w.obj[g_w.members[k]].carried = g_w.car;
        }
    } else {
        g_w.car = -1;
    }
    RandomGroup();
    RandomInput();
    RandomCamera();
    RandomRole();
}
void RandomWorldFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}
int g_objSeed;
void RandomOneObj() { RandomObj(g_objSeed); }

// ---- the operations ----
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
    float f = 0.0f;
};
enum {
    OP_INIT, OP_DONE, OP_OPEN, OP_ISOPEN, OP_SETENTRY, OP_HASTARGET, OP_BUILDPAGE, OP_BUILDTARGET, OP_REFRESH, OP_ACTIVATE,
    OP_NEXT, OP_PREVIOUS, OP_INPUT, OP_ISSUE, OP_RENDER, OP_SETTARGET, OP_UPDATE, OP_RELAYOUT, OP_DELETED, OP_AIM,
    OP_PAGE,   // (the test's: m_page and m_selected set, as the pages' moves leave them)
    OP_SELECT, // (the test's: m_selected set)
    W_WORLD = 100, W_OBJ, W_TEAM, W_GROUP, W_INPUT, W_CAMERA, W_ROLE, W_HEALTH, W_FRAME
};
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
        RandomWorldFrom(op.a, &RandomTeam);
        break;
    case W_GROUP:
        RandomWorldFrom(op.a, &RandomGroup);
        break;
    case W_INPUT:
        RandomWorldFrom(op.a, &RandomInput);
        break;
    case W_CAMERA:
        RandomWorldFrom(op.a, &RandomCamera);
        break;
    case W_ROLE:
        RandomWorldFrom(op.a, &RandomRole);
        break;
    case W_HEALTH:
        g_w.obj[op.a].health = op.b;
        break;
    case W_FRAME:
        FrameAt(F_SIGNALS)->m_frameFlags = op.a ? FrameAt(F_SIGNALS)->m_frameFlags | 1u : FrameAt(F_SIGNALS)->m_frameFlags & ~1u;
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
        w = P(0.3) ? Pick({0u, 0xFFFFFFFFu, 1u, 0x32u, 0x48u}) : U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    g_missingFrame = P(0.03);
    RandomWorld();
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

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Bool = bool(__attribute__((thiscall)) *)(void *);
using T_Open = void(__attribute__((thiscall)) *)(void *, bool, void *, void *);
using T_Entry = void(__attribute__((thiscall)) *)(void *, int, int, int);
using T_Input = void(__attribute__((thiscall)) *)(void *, float *, float *);
using T_Ptr = void(__attribute__((thiscall)) *)(void *, void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Aim = bool(__attribute__((thiscall)) *)(void *, float, S_vector4 *);

// The panel's fields (the same layout on both sides: the port's C_SignalsPanel is the original's 0x80 bytes).
std::string Snap(const void *panel, int side)
{
    const C_SignalsPanel *p = static_cast<const C_SignalsPanel *>(panel);
    char text[200];
    const void *pages = side ? static_cast<const void *>(s_pages) : *OriginalAt<void **>(0x0089bcc8);
    const char *table = pages == SINGLE_PLAYER_PAGES || pages == OriginalAt<void *>(0x0083b620)   ? "single"
                        : pages == MULTIPLAYER_PAGES || pages == OriginalAt<void *>(0x0083b990) ? "multi"
                                                                                                 : "?";
    std::snprintf(text, sizeof text, "panel %s %s %s %s page %x selected %d target %s member %s noGroup %d pages %s entries",
                  Name(p->m_ui).c_str(), Name(p->m_frame).c_str(), Name(p->m_scene).c_str(), BoxName(p->m_text).c_str(), p->m_page,
                  p->m_selected, Name(p->m_target).c_str(), Name(p->m_member).c_str(), At<uint8_t>(panel, 0x6C), table);
    std::string s = text;
    for (const S_Entry &e : p->m_entries)
        s += " " + std::to_string(e.textId) + ":" + std::to_string(e.action);
    return s;
}
// Without a target, a target page (entered by an entry the test's start words or SetEntry made) would name it: both
// sides read the null actor's vtable. Those operations are left out then.
bool TargetPageWithoutTarget(const C_SignalsPanel *p)
{
    if (p->m_target)
        return false;
    if (p->m_page == 0x35 || p->m_page == 0x38)
        return true;
    for (const S_Entry &e : p->m_entries)
        if (e.action == 0x35 || e.action == 0x38)
            return true;
    return false;
}
void Run(int side, C_SignalsPanel *port, uint8_t *orig, const S_Op &op)
{
    void *self = side ? static_cast<void *>(port) : orig;
    C_SignalsPanel *view = static_cast<C_SignalsPanel *>(self);
    if ((op.kind == OP_ACTIVATE || op.kind == OP_INPUT || op.kind == OP_REFRESH || op.kind == OP_NEXT || op.kind == OP_PREVIOUS) &&
        TargetPageWithoutTarget(view)) {
        Log("skipped: a target page without a target");
        return;
    }
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<C_Interface *>(g_ui), reinterpret_cast<I3D_scene *>(&g_scene))
                               : OriginalAt<T_Init>(0x004bf2e0)(orig, g_ui, &g_scene));
        break;
    case OP_DONE:
        side ? port->Done() : OriginalAt<T_Void>(0x004bf480)(orig);
        break;
    case OP_OPEN:
        side ? port->Open(op.a != 0, static_cast<C_Actor *>(Obj(op.b)), static_cast<C_Human *>(Obj(op.c)))
             : OriginalAt<T_Open>(0x004bf4a0)(orig, op.a != 0, Obj(op.b), Obj(op.c));
        break;
    case OP_ISOPEN:
        Log("IsOpen %d", side ? port->IsOpen() : OriginalAt<T_Bool>(0x004c05f0)(orig));
        break;
    case OP_SETENTRY:
        side ? port->SetEntry(op.a, op.b, op.c) : OriginalAt<T_Entry>(0x004bf640)(orig, op.a, op.b, op.c);
        break;
    case OP_HASTARGET:
        Log("HasTargetCommands %d", side ? port->HasTargetCommands() : OriginalAt<T_Bool>(0x004c10f0)(orig));
        break;
    case OP_BUILDPAGE:
        side ? port->BuildPage() : OriginalAt<T_Void>(0x004bffe0)(orig);
        break;
    case OP_BUILDTARGET:
        side ? port->BuildTargetPage() : OriginalAt<T_Void>(0x004bf670)(orig);
        break;
    case OP_REFRESH:
        side ? port->Refresh() : OriginalAt<T_Void>(0x004c0370)(orig);
        break;
    case OP_ACTIVATE:
        side ? port->Activate() : OriginalAt<T_Void>(0x004c0600)(orig);
        break;
    case OP_NEXT:
        side ? port->SelectNext() : OriginalAt<T_Void>(0x004c06e0)(orig);
        break;
    case OP_PREVIOUS:
        // (From -1 the original walks back through memory: the port's fix, tested alone.)
        if (view->m_selected < 0) {
            Log("SelectPrevious skipped");
            break;
        }
        side ? port->SelectPrevious() : OriginalAt<T_Void>(0x004c0720)(orig);
        break;
    case OP_INPUT: {
        // (Not the wheel up from -1: SelectPrevious's.)
        const int k = int(g_w.keyChar & 0xFF) - '0';
        if (view->m_selected < 0 && !(k >= 0 && k < 10) && g_w.pressed[INPUT_ROLL_MENU] == 0.0f && g_w.pressed[INPUT_WHEEL] > 0.0f) {
            Log("HandleInput skipped");
            break;
        }
        side ? port->HandleInput(g_w.held, g_w.pressed) : OriginalAt<T_Input>(0x004c0760)(orig, g_w.held, g_w.pressed);
        break;
    }
    case OP_ISSUE:
        side ? port->Issue() : OriginalAt<T_Void>(0x004c0990)(orig);
        break;
    case OP_RENDER:
        side ? port->Render() : OriginalAt<T_Void>(0x004c0880)(orig);
        break;
    case OP_SETTARGET:
        side ? port->SetTarget(static_cast<C_Actor *>(Obj(op.a))) : OriginalAt<T_Ptr>(0x004c0ef0)(orig, Obj(op.a));
        break;
    case OP_UPDATE:
        side ? port->Update(op.a) : OriginalAt<T_Int>(0x004c0f70)(orig, op.a);
        break;
    case OP_RELAYOUT:
        side ? port->Relayout() : OriginalAt<T_Void>(0x004c10d0)(orig);
        break;
    case OP_DELETED:
        // (The port forgets its target when it is the actor deleted; the original kept the freed pointer: its target
        // cleared first, the same as the port's.)
        if (side) {
            port->OnTargetDeleted(static_cast<C_Actor *>(Obj(op.a)));
        } else {
            if (At<void *>(orig, 0x64) == Obj(op.a))
                Put<void *>(orig, 0x64, nullptr);
            OriginalAt<T_Void>(0x004c11c0)(orig);
        }
        break;
    case OP_AIM: {
        S_vector4 point = {op.f, -op.f, 2 * op.f, 3.0f};
        const bool hit = side ? AimPoint(op.f, &point) : OriginalAt<T_Aim>(0x004c08a0)(orig, op.f, &point);
        // (The point's w: the hit's stack word in the original (the ray test writes x, y, z), kept in the port.)
        Log("AimPoint -> %d %s", hit, F3(&point.x).c_str());
        break;
    }
    case OP_PAGE:
        view->m_page = op.a;
        view->m_selected = op.b;
        break;
    case OP_SELECT:
        view->m_selected = op.b;
        break;
    }
}
void RunScenario(const std::vector<S_Op> &ops)
{
    for (int side = 0; side < 2; ++side) {
        StartSide();
        uint8_t *orig = static_cast<uint8_t *>(std::calloc(1, 0x80));
        C_SignalsPanel *port = static_cast<C_SignalsPanel *>(std::calloc(1, sizeof(C_SignalsPanel)));
        uint8_t *self = side ? reinterpret_cast<uint8_t *>(port) : orig;
        // (The same start words in the fields Init does not set: the entries, m_page.)
        for (int k = 0; k < 0x14 + 1; ++k)
            Put<uint32_t>(self, 0x0C + 4 * k, g_startWords[k % 0x20]);
        // (And m_selected, m_target, m_member, m_noGroup: Init clears them; objects of the side.)
        Put<int>(self, 0x60, int(g_startWords[0x1C] % 11) - 1);
        Put<void *>(self, 0x64, Obj(int(g_startWords[0x1D] % OBJECTS)));
        Put<void *>(self, 0x68, Obj(int(g_startWords[0x1E] % OBJECTS)));
        Put<uint8_t>(self, 0x6C, uint8_t(g_startWords[0x1F]));
        for (const S_Op &op : ops) {
            Log("-- %d %d %d %d %s", op.kind, op.a, op.b, op.c, FloatText(op.f).c_str());
            if (op.kind >= 100)
                ApplyWorld(op);
            SyncWorld(side);
            if (op.kind < 100)
                Run(side, port, orig, op);
            g_log.push_back(Snap(self, side));
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

const int PAGES[] = {0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B};
int AnyObj() { return P(0.15) ? -1 : I(0, OBJECTS - 1); }
void FillOp(S_Op &op, bool targetPage)
{
    switch (op.kind) {
    case OP_OPEN:
        op.a = P(0.8);
        op.b = g_w.car >= 0 && P(0.3) ? g_w.car : AnyObj();
        op.c = P(0.4) ? -1 : g_w.memberCount && P(0.8) ? g_w.members[I(0, g_w.memberCount - 1)] : I(0, OBJECTS - 1);
        break;
    case OP_SETENTRY:
        op.a = I(0, C_SignalsPanel::ENTRIES - 1);
        op.b = P(0.5) ? 0x1B00 + I(0, 0xFF) : I(-1, 3);
        op.c = P(0.4) ? SIGNAL_DISABLED_COMMAND : I(-110, 60);
        break;
    case OP_SETTARGET:
        op.a = g_w.car >= 0 && P(0.3) ? g_w.car : AnyObj();
        break;
    case OP_DELETED:
        op.a = AnyObj();
        break;
    case OP_UPDATE:
        op.a = P(0.5) ? Pick({0, 16, 100}) : I(-5, 1000);
        break;
    case OP_AIM:
        op.f = P(0.3) ? Pick({40.0f, 0.0f, 1.0f, 100.0f}) : F(0, 1000);
        break;
    case OP_PAGE:
        // (Pages the moves reach; the target pages too (without a target: the operations naming it left out).)
        do
            op.a = Pick(PAGES);
        while (!targetPage && (op.a == 0x35 || op.a == 0x38));
        op.b = P(0.85) ? I(0, C_SignalsPanel::ENTRIES - 1) : -1;
        break;
    case W_WORLD:
    case W_TEAM:
    case W_GROUP:
    case W_INPUT:
    case W_CAMERA:
    case W_ROLE:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case W_OBJ:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, OBJECTS - 1);
        break;
    case W_HEALTH:
        op.a = P(0.6) && g_w.memberCount ? g_w.members[I(0, g_w.memberCount - 1)] : I(0, OBJECTS - 1);
        op.b = P(0.5) ? Pick({0, 1, -1}) : I(2, 800);
        break;
    case W_FRAME:
        op.a = P(0.5);
        break;
    }
}

// ---- the panel ----
void TestSignals()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{OP_INIT});
    if (!g_missingFrame) {
        // (The panel opened first now and then: its pages need the menus' state.)
        if (P(0.7)) {
            S_Op open{OP_OPEN};
            FillOp(open, false);
            open.a = 1;
            ops.push_back(open);
            // (Then now and then into a page and on: the target page's entries issued.)
            if (P(0.3))
                for (int n = I(1, 3); n > 0; --n)
                    ops.push_back(S_Op{OP_ACTIVATE});
            // (Or straight into a page (mostly a target page): its entries chosen and issued.)
            if (P(0.4)) {
                S_Op page{OP_PAGE};
                page.a = P(0.6) ? Pick({0x35, 0x38}) : Pick(PAGES);
                page.b = 0;
                ops.push_back(page);
                ops.push_back(S_Op{OP_BUILDPAGE});
                for (int n = I(1, 3); n > 0; --n) {
                    S_Op select{OP_SELECT};
                    select.b = I(0, C_SignalsPanel::ENTRIES - 1);
                    ops.push_back(select);
                    ops.push_back(S_Op{Pick({OP_ISSUE, OP_ISSUE, OP_ACTIVATE, OP_REFRESH})});
                }
            }
        }
        static const int KINDS[] = {OP_OPEN, OP_OPEN, OP_ISOPEN, OP_SETENTRY, OP_HASTARGET, OP_BUILDPAGE, OP_BUILDTARGET,
                                    OP_REFRESH, OP_ACTIVATE, OP_ACTIVATE, OP_NEXT, OP_PREVIOUS, OP_INPUT, OP_INPUT, OP_INPUT,
                                    OP_ISSUE, OP_RENDER, OP_SETTARGET, OP_UPDATE, OP_UPDATE, OP_RELAYOUT, OP_DELETED, OP_AIM,
                                    OP_PAGE, OP_PAGE, W_WORLD, W_OBJ, W_OBJ, W_TEAM, W_GROUP, W_INPUT, W_INPUT, W_CAMERA,
                                    W_ROLE, W_HEALTH, W_FRAME};
        for (int n = I(0, 24); n > 0; --n) {
            S_Op op{Pick(KINDS)};
            FillOp(op, P(0.5));
            ops.push_back(op);
        }
    }
    if (P(0.5))
        ops.push_back(S_Op{OP_DONE});
    SaveStart();
    RunScenario(ops);
}

// ---- the port's fixes, alone ----
// A supply box of more than ENTRIES items: the entries' ten (the original wrote past them); a member's name with '%':
// the name as text (the original used it as the format); SelectPrevious from -1: nothing chosen (the original walked
// back through memory); OnTargetDeleted on the target: forgotten (the original read the freed actor).
void TestFixes()
{
    StartScenario();
    g_missingFrame = false;
    SaveStart();
    StartSide();
    SyncWorld(1);
    C_SignalsPanel panel{};
    CHECK(panel.Init(reinterpret_cast<C_Interface *>(g_ui), reinterpret_cast<I3D_scene *>(&g_scene)), "Init: false");
    // A supply box of SUPPLY items (allied lists), nobody for the member.
    S_Obj &box = g_w.obj[0];
    box.type = ACTOR_SUPPLY_BOX;
    box.hasLists = 1;
    box.charges = 1;
    box.supplyCount = I(C_SignalsPanel::ENTRIES + 1, SUPPLY);
    SyncWorld(1);
    panel.m_target = static_cast<C_Actor *>(Obj(0));
    panel.m_member = nullptr;
    for (S_Entry &e : panel.m_entries)
        e = {-7, -7};
    const uint8_t after = At<uint8_t>(&panel, 0x60);
    panel.m_selected = 5;
    panel.BuildTargetPage();
    for (int k = 0; k < C_SignalsPanel::ENTRIES; ++k)
        CHECK(panel.m_entries[k].textId == At<int>(g_itemDefs[box.supply[k]], 0x14) && panel.m_entries[k].action == 6 + SIGNAL_ACTOR_COMMAND,
              "supply box entry %d: %d %d", k, panel.m_entries[k].textId, panel.m_entries[k].action);
    CHECK(panel.m_selected == 5 && At<uint8_t>(&panel, 0x60) == uint8_t(5), "supply box: past the entries");
    (void)after;
    // A name with '%'.
    g_w.obj[1].type = ACTOR_HUMAN;
    std::snprintf(g_w.obj[1].name, sizeof g_w.obj[1].name, "%%s%%d%%n");
    SyncWorld(1);
    panel.m_member = static_cast<C_Human *>(Obj(1));
    panel.m_page = 0x32;
    g_log.clear();
    panel.Refresh();
    CHECK(std::find(g_log.begin(), g_log.end(), BoxName(panel.m_text) + " AddText '%s%d%n' 49") != g_log.end(), "name with %%: not as text");
    // SelectPrevious from -1: nothing chosen, refreshed.
    panel.m_selected = -1;
    g_log.clear();
    panel.SelectPrevious();
    CHECK(panel.m_selected == -1, "SelectPrevious from -1: %d", panel.m_selected);
    CHECK(std::find(g_log.begin(), g_log.end(), "f1 SetOn 1") != g_log.end(), "SelectPrevious from -1: no click");
    // OnTargetDeleted on the target, open on the main page: forgotten, the page built without it.
    FrameAt(F_SIGNALS)->m_frameFlags |= 1;
    panel.m_target = static_cast<C_Actor *>(Obj(2));
    panel.m_page = 0x35;
    panel.OnTargetDeleted(static_cast<C_Actor *>(Obj(2)));
    CHECK(panel.m_target == nullptr && panel.m_page == 0x32 && panel.m_selected == 0, "OnTargetDeleted: %s page %x selected %d",
          Name(panel.m_target).c_str(), panel.m_page, panel.m_selected);
    // Closed: forgotten all the same, nothing built.
    FrameAt(F_SIGNALS)->m_frameFlags &= ~1u;
    panel.m_target = static_cast<C_Actor *>(Obj(3));
    panel.m_page = 0x35;
    panel.OnTargetDeleted(static_cast<C_Actor *>(Obj(3)));
    CHECK(panel.m_target == nullptr && panel.m_page == 0x35, "OnTargetDeleted closed: %s page %x", Name(panel.m_target).c_str(), panel.m_page);
    panel.Done();
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    SetUpGfx();
    g_graphVt[0xA0 / 4] = reinterpret_cast<void *>(G_AsciiKey);
    g_graphVt[0xCC / 4] = reinterpret_cast<void *>(G_KeyToChar);
    // The actors' vtables: the original's GetName (0x04) and HasSeatWeapon (0x98), the port's from its member pointers.
    for (void *&v : g_origActorVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portActorVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origActorVt[0x04 / 4] = reinterpret_cast<void *>(V_GetName);
    g_origActorVt[0x98 / 4] = reinterpret_cast<void *>(V_HasSeatWeapon);
    g_portActorVt[Slot(&C_Actor::GetName) / 4] = reinterpret_cast<void *>(V_GetName);
    g_portActorVt[Slot(&C_Actor::HasSeatWeapon) / 4] = reinterpret_cast<void *>(V_HasSeatWeapon);
    for (void *&v : g_origDefVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portDefVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origDefVt[0x18 / 4] = reinterpret_cast<void *>(V_AmmoItemId);
    g_portDefVt[Slot(&C_ItemDef::GetAmmoItemId) / 4] = reinterpret_cast<void *>(V_AmmoItemId);
    for (void *&v : g_origCommanderVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portCommanderVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origCommanderVt[0x08 / 4] = reinterpret_cast<void *>(O_CollectGroup);
    g_origCommanderVt[0x40 / 4] = reinterpret_cast<void *>(O_SpacingMin);
    g_origCommanderVt[0x3C / 4] = reinterpret_cast<void *>(O_SpacingMax);
    g_portCommanderVt[Slot(&C_SquadCommander::CollectGroup) / 4] = reinterpret_cast<void *>(P_CollectGroup);
    g_portCommanderVt[Slot(&C_SquadCommander::IsSpacingMin) / 4] = reinterpret_cast<void *>(O_SpacingMin);
    g_portCommanderVt[Slot(&C_SquadCommander::IsSpacingMax) / 4] = reinterpret_cast<void *>(O_SpacingMax);
    Put<void *>(g_origCommander, 0, g_origCommanderVt);
    Put<void *>(g_portCommander, 0, g_portCommanderVt);
    for (void *&v : g_origCameraVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origCameraVt[0x64 / 4] = reinterpret_cast<void *>(O_CameraPos);
    g_origCameraVt[0x58 / 4] = reinterpret_cast<void *>(O_ViewDistance);
    Put<void *>(g_origCamera, 0, g_origCameraVt);
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
        {0x004619f0, reinterpret_cast<const void *>(O_Member)},
        {0x00462300, reinterpret_cast<const void *>(O_MemberCount)},
        {0x00461a20, reinterpret_cast<const void *>(O_IsMember)},
        {0x0045e9a0, reinterpret_cast<const void *>(O_TeamHasItem)},
        {0x0045ea00, reinterpret_cast<const void *>(O_MedkitBesides)},
        {0x004622c0, reinterpret_cast<const void *>(O_AliveCount)},
        {0x005a78f0, reinterpret_cast<const void *>(O_FindItem)},
        {0x005a6aa0, reinterpret_cast<const void *>(O_CountItems)},
        {0x005a7ed0, reinterpret_cast<const void *>(O_WireCutter)},
        {0x005a2b10, reinterpret_cast<const void *>(O_HandsWeapon)},
        {0x005a7e00, reinterpret_cast<const void *>(O_Medkit)},
        {0x005a5f00, reinterpret_cast<const void *>(O_OrderCount)},
        {0x005a6450, reinterpret_cast<const void *>(O_Surrendered)},
        {0x005a6380, reinterpret_cast<const void *>(O_Unconscious)},
        {0x005a9960, reinterpret_cast<const void *>(O_Formation)},
        {0x005a82c0, reinterpret_cast<const void *>(O_ShortName)},
        {0x005a80e0, reinterpret_cast<const void *>(O_ViewDir)},
        {0x005a45e0, reinterpret_cast<const void *>(O_SendCommand)},
        {0x005ac1c0, reinterpret_cast<const void *>(O_RemoveCollision)},
        {0x005ac1b0, reinterpret_cast<const void *>(O_RestoreCollision)},
        {0x004ddd10, reinterpret_cast<const void *>(O_CanEnter)},
        {0x004dde20, reinterpret_cast<const void *>(O_EnemyAboard)},
        {0x004ddcf0, reinterpret_cast<const void *>(O_Destroyed)},
        {0x004563e0, reinterpret_cast<const void *>(O_GetSquadCommander)},
        {0x0049d340, reinterpret_cast<const void *>(O_Highlight)},
        {0x007db5d0, reinterpret_cast<const void *>(O_CanAdd)},
        {0x006c73f0, reinterpret_cast<const void *>(O_ClearAction)},
        {0x006c7510, reinterpret_cast<const void *>(O_ClearKey)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x0069a620, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x00718830, reinterpret_cast<const void *>(O_RayTest)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("signals_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_SIGNALS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_SIGNALS_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_SIGNALS_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Signals", TestSignals}, {"SignalsFixes", TestFixes}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("signals_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
