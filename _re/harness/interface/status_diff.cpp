// Human part 66a: the soldier's status panels but the crosshair (game/interface_status.cpp: the shared helpers
// FindTexture and DuplicateMaterial, the speedometer, the reticles, the weapon panel, the stance panel, the progress bar)
// against the original's (0x004a89e0..0x004a9740, 0x004aaac0..0x004ab230). MinGW; build_status.bat; runs status_diff.exe
// ("status_diff: N checks, 0 failures"). interface_status.cpp is compiled into this file; the kit (interface_kit.inc,
// interface_gfx.inc) fakes the frames, the scene, the graph, the text boxes and the graphics. A case per panel: scenarios
// of its operations (Init, Update, Render, Relayout, Done, SetMode ...; between them the world changing) run from one
// start on each side, every call out logged by the same fakes, the panel's fields snapped after each operation, the
// frames and the graphics compared at the end; the helpers called straight on random meshes.
// Faked on both sides: the humans (GetHandsItem 0x005a2ac0, GetLongGunItem 0x005a2af0, CountAmmo 0x005a6c50,
// CountThrownItems 0x005a6d10, GetStanceFlags 0x005a5d10, GetStateFlags 0x005a5c80, GetTeam 0x005a8820,
// GetActionProgress 0x005a9830: the original's redirected; the vehicle and the seat in raw blocks), the items (GetAmount),
// the item definitions (GetCarryMode, GetBehaviour, GetAmmoItemId; GetImage 0x007dfab0) and the vehicle (HasSeatWeapon,
// GetSeatWeaponRounds, GetSeatAmmoCount, GetSeatWeaponDef): shared blocks whose vtable is each side's before its run (the
// port's slots 4 bytes on: g++'s second destructor slot), the active player (0x0045d1d0), the multiplayer settings.
// LS3DF's S_quat::Make (both forms) and Normalize on both sides.
// HD2_STATUS_ROUNDS: scenarios per case (default 20,000; HD2_STATUS_QUICK: 5,000); HD2_STATUS_CASE: one case only;
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
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/interface_status.cpp"
#include "game/game_texts.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"

void __stdcall S_quat::Make(const S_vector &axis, float angle)
{
    using T = void(__stdcall *)(S_quat *, const S_vector &, float);
    static const T make = reinterpret_cast<T>(H::Ls3df("?Make@S_quat@@QAGXABUS_vector@@M@Z"));
    make(this, axis, angle);
}

namespace H {

// ---- the scene's frames ----
const char *const SCENE_NAMES[] = {"spee",   "sniper", "scope",  "arisaka", "k98",     "springfield", "lee_enfield", "tabl02", "tabl01",
                                   "pkneel", "play",   "mwalk",  "mrun",    "msprint", "mpaxis",      "mpallied",    "prgbrbk", "prgbr01"};
constexpr int SCENE_COUNT = 18;   // f0..f17
// Children: spee's speed01..03 (f18..f20), tabl02's i_hands, i_shoulder (f21, f22), prgbrbk's progress_name (f23).
const char *const CHILD_NAMES[] = {"speed01", "speed02", "speed03", "i_hands", "i_shoulder", "progress_name"};
const int CHILD_PARENT[] = {0, 0, 0, 7, 7, 16};
constexpr int CHILD_FIRST = 18, CHILD_COUNT = 6;
constexpr int HUMAN_FRAME = 24;
bool g_missing[SCENE_COUNT];
I3D_frame *FindFrame(const char *name)
{
    for (int k = 0; k < SCENE_COUNT; ++k)
        if (std::strcmp(name, SCENE_NAMES[k]) == 0)
            return g_missing[k] ? nullptr : FrameAt(k);
    return nullptr;
}
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    for (int k = 0; k < CHILD_COUNT; ++k)
        if (std::strcmp(name, CHILD_NAMES[k]) == 0 && FrameIndex(f) == CHILD_PARENT[k])
            return FrameAt(CHILD_FIRST + k);
    return nullptr;
}

// ---- the humans, items, definitions, the vehicle ----
constexpr int HUMANS = 3, ITEMS = 4, DEFS = 4;
alignas(16) uint8_t g_humans[HUMANS][0x800], g_humansStart[HUMANS][0x800];
alignas(16) uint8_t g_items[ITEMS][0x40], g_defs[DEFS][0x100], g_vehicle[0x400], g_itemsStart[ITEMS][0x40], g_defsStart[DEFS][0x100];
struct S_ItemAnswers {
    float amount;
};
struct S_DefAnswers {
    int carry, behaviour, ammoId, image;   // image: a texture or -1
};
struct S_HumanAnswers {
    int hands, longGun;   // an item or -1
    int ammo, thrown;
    uint32_t stance, state;
    int team;
    bool progress;
    int time, total, textId;
};
struct S_VehicleAnswers {
    bool seatWeapon;
    int def, rounds, ammo;
};
struct S_World {
    S_ItemAnswers items[ITEMS];
    S_DefAnswers defs[DEFS];
    S_HumanAnswers humans[HUMANS];
    S_VehicleAnswers vehicle;
    int active;
};
S_World g_world, g_worldStart;
void *g_itemVt[2][16], *g_defVt[2][16], *g_vehicleVt[2][64];   // [0] the original's, [1] the port's
int IndexIn(const void *p, const void *base, size_t stride, int count)
{
    for (int i = 0; i < count; ++i)
        if (p == static_cast<const uint8_t *>(base) + i * stride)
            return i;
    return -1;
}
int HumanIndex(const void *h) { return IndexIn(h, g_humans, sizeof g_humans[0], HUMANS); }
int ItemIndex(const void *p) { return IndexIn(p, g_items, sizeof g_items[0], ITEMS); }
int DefIndex(const void *p) { return IndexIn(p, g_defs, sizeof g_defs[0], DEFS); }
C_Human *HumanAt(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_humans[i]); }
const S_HumanAnswers &HA(const void *h) { return g_world.humans[HumanIndex(h)]; }

void *HHands(const void *h)
{
    Log("h%d GetHandsItem", HumanIndex(h));
    return HA(h).hands < 0 ? nullptr : g_items[HA(h).hands];
}
void *HLongGun(const void *h)
{
    Log("h%d GetLongGunItem", HumanIndex(h));
    return HA(h).longGun < 0 ? nullptr : g_items[HA(h).longGun];
}
int HCountAmmo(const void *h, int type, bool backpack)
{
    Log("h%d CountAmmo %d %d", HumanIndex(h), type, backpack);
    return HA(h).ammo;
}
int HThrown(const void *h)
{
    Log("h%d CountThrownItems", HumanIndex(h));
    return HA(h).thrown;
}
uint32_t HStance(const void *h)
{
    Log("h%d GetStanceFlags", HumanIndex(h));
    return HA(h).stance;
}
uint32_t HState(const void *h)
{
    Log("h%d GetStateFlags", HumanIndex(h));
    return HA(h).state;
}
int HTeam(const void *h, bool pending)
{
    Log("h%d GetTeam %d", HumanIndex(h), pending);
    return HA(h).team;
}
bool HProgress(const void *h, int *time, int *total, int *textId)
{
    Log("h%d GetActionProgress", HumanIndex(h));
    const S_HumanAnswers &a = HA(h);
    if (a.progress) {
        *time = a.time;
        *total = a.total;
        *textId = a.textId;
    }
    return a.progress;
}
void *__attribute__((thiscall)) O_Hands(void *h) { return HHands(h); }
void *__attribute__((thiscall)) O_LongGun(void *h) { return HLongGun(h); }
int __attribute__((thiscall)) O_CountAmmo(void *h, int type, bool backpack) { return HCountAmmo(h, type, backpack); }
int __attribute__((thiscall)) O_Thrown(void *h) { return HThrown(h); }
uint32_t __attribute__((thiscall)) O_Stance(void *h) { return HStance(h); }
uint32_t __attribute__((thiscall)) O_State(void *h) { return HState(h); }
int __attribute__((thiscall)) O_Team(void *h, bool pending) { return HTeam(h, pending); }
bool __attribute__((thiscall)) O_Progress(void *h, int *time, int *total, int *textId) { return HProgress(h, time, total, textId); }
void *__attribute__((thiscall)) O_GetActivePlayer(void *) { return HumanAt(g_world.active); }

// The virtuals (one function for both sides' slots).
float __attribute__((thiscall)) V_Amount(void *item)
{
    Log("item%d GetAmount", ItemIndex(item));
    return g_world.items[ItemIndex(item)].amount;
}
int __attribute__((thiscall)) V_Carry(void *def)
{
    Log("def%d GetCarryMode", DefIndex(def));
    return g_world.defs[DefIndex(def)].carry;
}
int __attribute__((thiscall)) V_Behaviour(void *def)
{
    Log("def%d GetBehaviour", DefIndex(def));
    return g_world.defs[DefIndex(def)].behaviour;
}
int __attribute__((thiscall)) V_AmmoId(void *def)
{
    Log("def%d GetAmmoItemId", DefIndex(def));
    return g_world.defs[DefIndex(def)].ammoId;
}
void *DefImage(const void *def)
{
    Log("def%d GetImage", DefIndex(def));
    const int image = g_world.defs[DefIndex(def)].image;
    return image < 0 ? nullptr : g_gfx.textures[image];
}
void *__attribute__((thiscall)) O_DefImage(void *def) { return DefImage(def); }
bool __attribute__((thiscall)) V_HasSeatWeapon(void *, int seat)
{
    Log("vehicle HasSeatWeapon %d", seat);
    return g_world.vehicle.seatWeapon;
}
int __attribute__((thiscall)) V_SeatRounds(void *, int seat)
{
    Log("vehicle GetSeatWeaponRounds %d", seat);
    return g_world.vehicle.rounds;
}
int __attribute__((thiscall)) V_SeatAmmo(void *, int seat)
{
    Log("vehicle GetSeatAmmoCount %d", seat);
    return g_world.vehicle.ammo;
}
void *__attribute__((thiscall)) V_SeatDef(void *, int seat)
{
    Log("vehicle GetSeatWeaponDef %d", seat);
    return g_world.vehicle.def < 0 ? nullptr : g_defs[g_world.vehicle.def];
}
void __attribute__((thiscall)) V_Other(void *p) { Log("%s other slot", Name(p).c_str()); }
// The objects' vtables for a side (0 the original's, 1 the port's).
void SetSide(int side)
{
    for (auto &item : g_items)
        *reinterpret_cast<void ***>(item) = g_itemVt[side];
    for (auto &def : g_defs)
        *reinterpret_cast<void ***>(def) = g_defVt[side];
    *reinterpret_cast<void ***>(g_vehicle) = g_vehicleVt[side];
}

alignas(16) uint8_t g_settings[0x400];
uint32_t &OrigRole() { return *OriginalAt<uint32_t *>(0x00982100); }

std::string PartName(const void *p)
{
    const std::string g = GfxName(p);
    if (!g.empty())
        return g;
    if (HumanIndex(p) >= 0)
        return "h" + std::to_string(HumanIndex(p));
    if (ItemIndex(p) >= 0)
        return "item" + std::to_string(ItemIndex(p));
    if (DefIndex(p) >= 0)
        return "def" + std::to_string(DefIndex(p));
    if (p == g_vehicle)
        return "vehicle";
    for (const void *b : g_boxes)
        if (b && p == b)
            return BoxName(p);
    return "";
}

}  // namespace H

// ---- the port's side ----
C_Item *C_Human::GetHandsItem() const { return static_cast<C_Item *>(H::HHands(this)); }
C_Item *C_Human::GetLongGunItem() const { return static_cast<C_Item *>(H::HLongGun(this)); }
int C_Human::CountAmmo(int typeId, bool backpack) const { return H::HCountAmmo(this, typeId, backpack); }
int C_Human::CountThrownItems() const { return H::HThrown(this); }
uint32_t C_Human::GetStanceFlags() const { return H::HStance(this); }
uint32_t C_Human::GetStateFlags() const { return H::HState(this); }
int C_Human::GetTeam(bool pending) const { return H::HTeam(this, pending); }
bool C_Human::GetActionProgress(int *time, int *total, int *textId) const { return H::HProgress(this, time, total, textId); }
IImage *C_ItemDef::GetImage() { return static_cast<IImage *>(H::DefImage(this)); }
// (The crosshair's: part 66b's; here unexpected.)
namespace {
void Unexpected(const char *what)
{
    H::Log("UNEXPECTED %s", what);
    ++H::failures;
    std::printf("FAIL unexpected call: %s\n", what);
}
}  // namespace
bool C_GameProfile::IsCrosshairAllowed() const { return Unexpected("IsCrosshairAllowed"), false; }
float C_Human::GetAccuracy() const { return Unexpected("GetAccuracy"), 0.0f; }
bool C_Human::IsFriendOf(const C_Actor *) const { return Unexpected("IsFriendOf"), false; }
bool C_TeamBase::IsMember(const C_Human *) const { return Unexpected("IsMember"), false; }
int C_TeamBase::IndexOf(const C_Human *) const { return Unexpected("IndexOf"), 0; }
const char *C_Human::GetFullName() const { return Unexpected("GetFullName"), ""; }
int C_Human::GetProfileUnknown24() const { return Unexpected("GetProfileUnknown24"), 0; }
C_str C_GameTexts::Get(int) const { return Unexpected("C_GameTexts::Get"), C_str(); }
int C_Human::GetStat(int) const { return Unexpected("GetStat"), 0; }
int C_Human::GetHealthPercent() const { return Unexpected("GetHealthPercent"), 0; }
bool C_Car::HasEnemyAboard(const C_Human *) { return Unexpected("HasEnemyAboard"), false; }
S_RankName g_rankNames[8];
C_Human *C_Team::GetActivePlayer() const { return H::HumanAt(H::g_world.active); }
namespace {
S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(H::g_settings);
}
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;

constexpr uint32_t P_VEHICLE = offsetof(C_Human, m_vehicle), P_SEAT = offsetof(C_Human, m_seat),
                   P_GAMETYPE = offsetof(S_MultiplayerSettings, gameType);
static_assert(offsetof(C_Item, m_def) == 4 && offsetof(C_Item, m_kind) == 8 && offsetof(C_ItemDef, m_textId) == 0x14, "items as the original's");

namespace H {

template <class T> void Both(uint8_t *block, uint32_t orig, uint32_t port, T value)
{
    std::memcpy(block + orig, &value, sizeof value);
    std::memcpy(block + port, &value, sizeof value);
}
template <class T> T At(const uint8_t *b, uint32_t o)
{
    T v;
    std::memcpy(&v, b + o, sizeof v);
    return v;
}

// ---- the world ----
S_HumanAnswers RandomHuman()
{
    S_HumanAnswers a;
    a.hands = P(0.2) ? -1 : I(0, ITEMS - 1);
    a.longGun = P(0.3) ? -1 : I(0, ITEMS - 1);
    a.ammo = P(0.2) ? Pick({0, -1, 100000}) : I(0, 300);
    a.thrown = P(0.2) ? Pick({0, -1, 10}) : I(0, 9);
    a.stance = P(0.7) ? Pick({0u, 1u, 2u, 3u}) : U32() & 0xF;
    a.state = P(0.7) ? Pick({0u, 0x2000u, 0x4000u, 0x10000u, 0x6000u, 0x12000u, 0x1F000u}) : U32();
    a.team = P(0.8) ? I(0, 1) : I(-1, 3);
    a.progress = P(0.6);
    a.total = P(0.1) ? Pick({0, -100}) : I(1, 5000);
    a.time = P(0.2) ? Pick({0, a.total, -1}) : I(0, std::max(1, a.total));
    a.textId = P(0.3) ? Pick({-1, -5, 0}) : I(0x100, 0x2000);
    return a;
}
float RandomAmount()
{
    return P(0.2) ? Pick({0.0f, -1.0f, 0.5f, 2.9999998f, 1e10f, -1e10f, NAN, INFINITY, 2147483648.0f}) : float(I(0, 200));
}
S_DefAnswers RandomDef()
{
    S_DefAnswers d;
    d.carry = P(0.3) ? 6 : I(0, 7);
    d.behaviour = P(0.3) ? 6 : I(0, 12);
    d.ammoId = I(-1, 300);
    d.image = P(0.2) ? -1 : I(0, TEXTURES - 1);
    return d;
}
void RandomWorld()
{
    for (auto &item : g_world.items)
        item.amount = RandomAmount();
    for (auto &def : g_world.defs)
        def = RandomDef();
    for (auto &human : g_world.humans)
        human = RandomHuman();
    g_world.vehicle = {P(0.5), P(0.2) ? -1 : I(0, DEFS - 1), I(-1, 200), I(-1, 500)};
    g_world.active = P(0.9) ? I(0, HUMANS - 1) : -1;
    for (int i = 0; i < ITEMS; ++i) {
        std::memset(g_items[i], 0, sizeof g_items[i]);
        *reinterpret_cast<void **>(g_items[i] + 4) = g_defs[I(0, DEFS - 1)];
        *reinterpret_cast<int *>(g_items[i] + 8) = P(0.6) ? 0 : I(1, 6);   // (the kind: 0 a weapon)
    }
    for (int d = 0; d < DEFS; ++d) {
        std::memset(g_defs[d], 0, sizeof g_defs[d]);
        *reinterpret_cast<int *>(g_defs[d] + 0x14) = I(0x100, 0x2000);
    }
    for (int h = 0; h < HUMANS; ++h) {
        std::memset(g_humans[h], 0, sizeof g_humans[h]);
        Both(g_humans[h], 0x160, P_VEHICLE, static_cast<void *>(P(0.3) ? g_vehicle : nullptr));
        Both(g_humans[h], 0x164, P_SEAT, P(0.8) ? I(0, 3) : I(-1, 8));
    }
}
void RandomScene()
{
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    std::fill(std::begin(g_missing), std::end(g_missing), false);
}
void SaveStart()
{
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    std::memcpy(g_humansStart, g_humans, sizeof g_humans);
    std::memcpy(g_itemsStart, g_items, sizeof g_items);
    std::memcpy(g_defsStart, g_defs, sizeof g_defs);
    g_worldStart = g_world;
}

// ---- a case: its operations, run on each side ----
struct S_Op {
    int kind;
    int a = 0, b = 0;
    float f[3] = {};
    S_HumanAnswers human{};
    S_ItemAnswers item{};
    S_DefAnswers def{};
    S_VehicleAnswers vehicle{};
};
struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Gfx gfx;
};
S_Run g_runs[2];
uint32_t g_role, g_gameType, g_seed;
// The world's operations (both sides alike): kinds from 100.
enum { W_ACTIVE = 100, W_HUMAN, W_ITEM, W_DEF, W_VEHICLE, W_SEAT, W_GAME };
S_Op RandomWorldOp()
{
    S_Op op;
    op.kind = Pick({W_ACTIVE, W_HUMAN, W_HUMAN, W_HUMAN, W_ITEM, W_DEF, W_VEHICLE, W_SEAT, W_GAME});
    switch (op.kind) {
    case W_ACTIVE:
        op.a = P(0.9) ? I(0, HUMANS - 1) : -1;
        break;
    case W_HUMAN:
        op.a = I(0, HUMANS - 1);
        op.human = P(0.5) ? RandomHuman() : g_world.humans[op.a];
        if (P(0.5))
            op.human.state = RandomHuman().state;
        break;
    case W_ITEM:
        op.a = I(0, ITEMS - 1);
        op.item.amount = RandomAmount();
        op.b = I(0, DEFS - 1);
        break;
    case W_DEF:
        op.a = I(0, DEFS - 1);
        op.def = RandomDef();
        break;
    case W_VEHICLE:
        op.vehicle = {P(0.5), P(0.2) ? -1 : I(0, DEFS - 1), I(-1, 200), I(-1, 500)};
        break;
    case W_SEAT:
        op.a = I(0, HUMANS - 1);
        op.b = P(0.5) ? I(0, 3) : -1;   // the seat; -1: out of the vehicle
        break;
    case W_GAME:
        op.a = P(0.7) ? 7 : Pick({0, 1, 3, 6});
        op.b = Pick({0, 0, 1, 2, 3});
        break;
    }
    return op;
}
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case W_ACTIVE:
        g_world.active = op.a;
        break;
    case W_HUMAN:
        g_world.humans[op.a] = op.human;
        break;
    case W_ITEM:
        g_world.items[op.a] = op.item;
        *reinterpret_cast<void **>(g_items[op.a] + 4) = g_defs[op.b];
        break;
    case W_DEF:
        g_world.defs[op.a] = op.def;
        break;
    case W_VEHICLE:
        g_world.vehicle = op.vehicle;
        break;
    case W_SEAT:
        Both(g_humans[op.a], 0x160, P_VEHICLE, static_cast<void *>(op.b < 0 ? nullptr : g_vehicle));
        Both(g_humans[op.a], 0x164, P_SEAT, op.b);
        break;
    case W_GAME:
        g_gameRole = uint32_t(op.a);
        OrigRole() = uint32_t(op.a);
        Both(g_settings, 0x224, P_GAMETYPE, uint32_t(op.b));
        break;
    }
}
void StartSide(int side)
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
    std::memcpy(g_humans, g_humansStart, sizeof g_humans);
    std::memcpy(g_items, g_itemsStart, sizeof g_items);
    std::memcpy(g_defs, g_defsStart, sizeof g_defs);
    g_world = g_worldStart;
    g_log.clear();
    g_boxes.clear();
    g_gameRole = g_role;
    OrigRole() = g_role;
    Both(g_settings, 0x224, P_GAMETYPE, g_gameType);
    SetSide(side);
    std::srand(g_seed);
}
void FinishSide(int side)
{
    g_runs[side].log.swap(g_log);
    std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
    std::memcpy(&g_runs[side].gfx, &g_gfx, sizeof g_gfx);
}
void CompareRuns()
{
    // (+0xDC, m_scaleW: the original's inline SetScale copies the caller's 12-byte scale in as 16 bytes from a stack
    // temporary. Put back from the start on both sides.)
    for (int i = 0; i < FRAMES; ++i)
        for (S_Run &r : g_runs)
            std::memcpy(r.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ++checks;
    if (std::memcmp(&g_runs[0].gfx, &g_runs[1].gfx, sizeof g_gfx) != 0) {
        const uint8_t *o = reinterpret_cast<const uint8_t *>(&g_runs[0].gfx), *p = reinterpret_cast<const uint8_t *>(&g_runs[1].gfx);
        size_t at = 0;
        while (o[at] == p[at])
            ++at;
        CHECK(false, "the graphics differ at +0x%x", unsigned(at));
    }
    ShowLogs(g_runs[0].log, g_runs[1].log);
}
// The panel's start: the same random words on both sides (the classes' fields all 4 bytes, laid out alike; Init must set
// every one the panel reads).
std::vector<uint32_t> g_startWords;
void StartWords(void *object, size_t bytes)
{
    std::memcpy(object, g_startWords.data(), std::min(bytes, g_startWords.size() * 4));
}
// A text box's margin (+0x90 the original's) right after Init: the snaps would read it after Done, freed.
void LogMargin(int side, const void *portBox, const uint8_t *origBlock, uint32_t at)
{
    const uint8_t margin = side ? static_cast<const C_TextBox *>(portBox)->m_margin : At<const uint8_t *>(origBlock, at)[0x90];
    Log("margin %d", margin);
}

// A case's scenario: the ops, a runner per side (side 0 the original's block, 1 the port's object).
void RunScenario(const std::vector<S_Op> &ops, const std::function<void(int side, const S_Op &op)> &run,
                 const std::function<std::string(int side)> &snap, const std::function<void(int side)> &make,
                 const std::function<void(int side)> &free)
{
    for (int side = 0; side < 2; ++side) {
        StartSide(side);
        make(side);
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            if (op.kind >= 100)
                ApplyWorld(op);
            else
                run(side, op);
            g_log.push_back(snap(side));
        }
        free(side);
        FinishSide(side);
    }
    CompareRuns();
}
void StartScenario()
{
    g_seed = U32();
    g_startWords.resize(0x20);
    for (uint32_t &w : g_startWords)
        w = P(0.3) ? Pick({0u, 0xFFFFFFFFu, 1u}) : U32();
    g_role = P(0.7) ? 7u : Pick({0u, 1u, 3u, 6u});
    g_gameType = Pick({0u, 0u, 1u, 2u, 3u});
    RandomScene();
    RandomWorld();
}

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Ptr = void(__attribute__((thiscall)) *)(void *, void *);
using T_Speed = void(__attribute__((thiscall)) *)(void *, float, float, float);
std::string Ptrs(const void *const *p, int n)
{
    std::string s;
    for (int k = 0; k < n; ++k)
        s += " " + Name(p[k]);
    return s;
}

// ---- the speedometer ----
void TestSpeedometer()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    for (int n = I(0, 10); n > 0; --n) {
        S_Op op{1};
        for (float &v : op.f)
            v = P(0.2) ? Pick({0.0f, 180.0f, 180.1f, 9000.0f, 9001.0f, 1.0f, -5.0f, 1e9f, NAN}) : F(0, 10000);
        op.f[2] = P(0.5) ? F(0, 1) : op.f[2];
        ops.push_back(op);
    }
    SaveStart();
    C_Speedometer *port = nullptr;
    uint8_t *orig = nullptr;
    RunScenario(
        ops,
        [&](int side, const S_Op &op) {
            if (op.kind == 0)
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(0x004a8d70)(orig, &g_scene));
            else if (side)
                port->Update(op.f[0], op.f[1], op.f[2]);
            else
                OriginalAt<T_Speed>(0x004a8df0)(orig, op.f[0], op.f[1], op.f[2]);
        },
        [&](int side) {
            const void *p[4];
            for (int k = 0; k < 4; ++k)
                p[k] = side ? (k ? port->m_needles[k - 1] : port->m_frame) : At<void *>(orig, k * 4);
            return "speedometer" + Ptrs(p, 4);
        },
        [&](int side) { side ? void(port = new C_Speedometer) : void(orig = static_cast<uint8_t *>(std::calloc(1, 0x40))); StartWords(side ? static_cast<void *>(port) : orig, sizeof(C_Speedometer)); },
        [&](int side) { side ? delete port : std::free(orig); });
}

// ---- the reticles ----
void TestReticle()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    bool modes = false;
    for (int n = I(0, 10); n > 0; --n) {
        S_Op op{P(0.9) ? 1 : 2};
        op.a = P(0.8) ? Pick({0, 1, 2, 3, 4, 6}) : Pick({5, 7, -1, 100});
        if (op.kind == 2) {   // Done, then Init again
            ops.push_back(op);
            op.kind = 0;
        }
        modes |= op.kind == 1;
        ops.push_back(op);
    }
    // (A reticle may be missing: Init checks; SetMode does not, nor Init the scope (both crash): only without SetMode.)
    if (!modes)
        for (int k = 3; k <= 6; ++k)
            g_missing[k] = P(0.3);
    SaveStart();
    C_Reticle *port = nullptr;
    uint8_t *orig = nullptr;
    RunScenario(
        ops,
        [&](int side, const S_Op &op) {
            if (op.kind == 0)
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(0x004a8c10)(orig, &g_scene));
            else if (op.kind == 2)
                side ? port->Done() : OriginalAt<T_Void>(0x004a8d50)(orig);
            else
                side ? port->SetMode(op.a) : OriginalAt<T_Int>(0x004a8cc0)(orig, op.a);
        },
        [&](int side) {
            const void *p[6];
            for (int k = 0; k < 6; ++k)
                p[k] = side ? (k == 0 ? port->m_mask : k == 5 ? port->m_binoculars : port->m_reticles[k - 1]) : At<void *>(orig, k * 4);
            return "reticle" + Ptrs(p, 6);
        },
        [&](int side) { side ? void(port = new C_Reticle) : void(orig = static_cast<uint8_t *>(std::calloc(1, 0x40))); StartWords(side ? static_cast<void *>(port) : orig, sizeof(C_Reticle)); },
        [&](int side) { side ? delete port : std::free(orig); });
}

// ---- the weapon panel ----
// The icons' face groups: each with a material and a texture (FindTexture tests none), one of them "X_SQUARE.BMP" in any
// case (the game's icons; missing, both sides use the null texture and material).
void IconGroups(int frame)
{
    const int count = I(1, GROUPS);
    *reinterpret_cast<int *>(g_gfx.levels[frame] + 0x58) = count;
    const int square = I(0, count - 1);
    for (int g = 0; g < count; ++g) {
        const int m = MATERIALS - 8 + (frame & 1) * 4 + g;   // (its own: m56..m63)
        *reinterpret_cast<void **>(g_gfx.groups[frame][g] + 4) = g_gfx.materials[m];
        int t = I(0, TEXTURES - 1);
        if (g == square) {
            t = 20 + (frame & 3);
            std::strcpy(g_textureNames[t], P(0.5) ? "X_SQUARE.BMP" : "x_square.bmp");
        } else if (!*reinterpret_cast<const char **>(g_gfx.textures[t] + 0x18) || std::strstr(g_textureNames[t], "X_SQ") || std::strstr(g_textureNames[t], "x_sq")) {
            t = 19;
            std::strcpy(g_textureNames[t], "menu.bmp");
        }
        *reinterpret_cast<const char **>(g_gfx.textures[t] + 0x18) = g_textureNames[t];
        *reinterpret_cast<void **>(g_gfx.materials[m] + 0xC) = g_gfx.textures[t];
    }
}

void TestWeapons()
{
    StartScenario();
    IconGroups(21);
    IconGroups(22);
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    // (One Init, as the interface's: the icons' materials then hold an item's picture, no "X_SQUARE.BMP" for another.)
    bool alive = true;
    for (int n = I(0, 16); n > 0 && alive; --n) {
        if (P(0.4)) {
            ops.push_back(RandomWorldOp());
            continue;
        }
        S_Op op{Pick({1, 1, 1, 1, 2, 3, 4})};   // Update, Render, Relayout, Done
        op.a = I(0, 100);
        alive = op.kind != 4;
        ops.push_back(op);
    }
    if (alive)
        ops.push_back(S_Op{4});
    SaveStart();
    C_WeaponPanel *port = nullptr;
    uint8_t *orig = nullptr;
    RunScenario(
        ops,
        [&](int side, const S_Op &op) {
            switch (op.kind) {
            case 0:
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(0x004a9010)(orig, &g_scene));
                LogMargin(side, side ? port->m_text : nullptr, orig, 0x24);
                break;
            case 1:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004a9530)(orig, op.a);
                break;
            case 2:
                side ? port->Render() : OriginalAt<T_Void>(0x004a9720)(orig);
                break;
            case 3:
                side ? port->Relayout() : OriginalAt<T_Void>(0x004a9730)(orig);
                break;
            case 4:
                side ? port->Done() : OriginalAt<T_Void>(0x004a92f0)(orig);
                break;
            }
        },
        [&](int side) {
            const void *p[10];
            int n[5];
            if (side) {
                const C_WeaponPanel &w = *port;
                const void *q[10] = {w.m_scene, w.m_frame, w.m_handsIcon, w.m_shoulderIcon, w.m_emptyTexture, w.m_handsMaterial,
                                     w.m_shoulderMaterial, w.m_handsDef, w.m_shoulderDef, w.m_text};
                std::copy(q, q + 10, p);
                const int m[5] = {w.m_handsRounds, w.m_shoulderRounds, w.m_handsAmmo, w.m_shoulderAmmo, w.m_thrownItems};
                std::copy(m, m + 5, n);
            } else {
                for (int k = 0; k < 10; ++k)
                    p[k] = At<void *>(orig, k * 4);
                for (int k = 0; k < 5; ++k)
                    n[k] = At<int>(orig, 0x28 + k * 4);
            }
            char text[80];
            std::snprintf(text, sizeof text, " rounds %d %d ammo %d %d thrown %d", n[0], n[1], n[2], n[3], n[4]);
            return "weapons" + Ptrs(p, 10) + text;
        },
        [&](int side) { side ? void(port = new C_WeaponPanel) : void(orig = static_cast<uint8_t *>(std::calloc(1, 0x80))); StartWords(side ? static_cast<void *>(port) : orig, sizeof(C_WeaponPanel)); },
        [&](int side) { side ? delete port : std::free(orig); });
}

// ---- the stance panel ----
void TestStance()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    bool alive = true;
    for (int n = I(0, 16); n > 0; --n) {
        if (!alive) {
            ops.push_back(S_Op{0});
            alive = true;
            continue;
        }
        if (P(0.35)) {
            ops.push_back(RandomWorldOp());
            continue;
        }
        S_Op op{Pick({1, 1, 1, 1, 2, 3, 4, 5})};   // Update, Render, Relayout, Done, the timer set
        op.a = P(0.6) ? Pick({0, 16, 100, 999, 1000, 1001, 60000}) : I(-50, 5000);
        if (op.kind == 5)
            op.a = P(0.3) ? Pick({0, -5, 1, 999, 1000, 59999, 60000, 3600000}) : I(0, 200000);
        alive = op.kind != 4;
        ops.push_back(op);
    }
    if (alive)
        ops.push_back(S_Op{4});
    SaveStart();
    C_StancePanel *port = nullptr;
    uint8_t *orig = nullptr;
    RunScenario(
        ops,
        [&](int side, const S_Op &op) {
            switch (op.kind) {
            case 0:
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(0x004aaac0)(orig, &g_scene));
                LogMargin(side, side ? port->m_text : nullptr, orig, 0x24);
                break;
            case 1:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004aad30)(orig, op.a);
                break;
            case 2:
                side ? port->Render() : OriginalAt<T_Void>(0x004aad00)(orig);
                break;
            case 3:
                side ? port->Relayout() : OriginalAt<T_Void>(0x004aad20)(orig);
                break;
            case 4:
                side ? port->Done() : OriginalAt<T_Void>(0x004aad10)(orig);
                break;
            case 5:   // (the scripts' timer: the interface writes it straight)
                if (side)
                    port->Timer() = op.a;
                else
                    std::memcpy(orig + 0x34, &op.a, 4);
                break;
            }
        },
        [&](int side) {
            const void *p[10];
            uint32_t n[4];
            if (side) {
                const C_StancePanel &s = *port;
                const void *q[10] = {s.m_scene, s.m_frame, s.m_kneel, s.m_prone, s.m_walk, s.m_run, s.m_sprint, s.m_axis, s.m_allied, s.m_text};
                std::copy(q, q + 10, p);
                const uint32_t m[4] = {s.m_stance, s.m_state, uint32_t(s.m_team), uint32_t(s.m_timer)};
                std::copy(m, m + 4, n);
            } else {
                for (int k = 0; k < 10; ++k)
                    p[k] = At<void *>(orig, k * 4);
                for (int k = 0; k < 4; ++k)
                    n[k] = At<uint32_t>(orig, 0x28 + k * 4);
            }
            char text[80];
            std::snprintf(text, sizeof text, " stance %x state %x team %d timer %d", n[0], n[1], int(n[2]), int(n[3]));
            return "stance" + Ptrs(p, 10) + text;
        },
        [&](int side) { side ? void(port = new C_StancePanel) : void(orig = static_cast<uint8_t *>(std::calloc(1, 0x80))); StartWords(side ? static_cast<void *>(port) : orig, sizeof(C_StancePanel)); },
        [&](int side) { side ? delete port : std::free(orig); });
}

// ---- the progress bar ----
void TestProgress()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    bool alive = true;
    for (int n = I(0, 16); n > 0; --n) {
        if (!alive) {
            ops.push_back(S_Op{0});
            alive = true;
            continue;
        }
        if (P(0.35)) {
            ops.push_back(RandomWorldOp());
            continue;
        }
        S_Op op{Pick({1, 1, 1, 1, 2, 4})};   // Update(a human or none), Render, Done
        op.a = P(0.85) ? I(0, HUMANS - 1) : -1;
        alive = op.kind != 4;
        ops.push_back(op);
    }
    if (alive)
        ops.push_back(S_Op{4});
    SaveStart();
    C_ProgressBar *port = nullptr;
    uint8_t *orig = nullptr;
    RunScenario(
        ops,
        [&](int side, const S_Op &op) {
            switch (op.kind) {
            case 0:
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(0x004aaf20)(orig, &g_scene));
                LogMargin(side, side ? port->m_text : nullptr, orig, 0x14);
                break;
            case 1:
                side ? port->Update(HumanAt(op.a)) : OriginalAt<T_Ptr>(0x004ab110)(orig, HumanAt(op.a));
                break;
            case 2:
                side ? port->Render() : OriginalAt<T_Void>(0x004ab220)(orig);
                break;
            case 4:
                side ? port->Done() : OriginalAt<T_Void>(0x004ab0f0)(orig);
                break;
            }
        },
        [&](int side) {
            const void *p[3] = {side ? port->m_frame : At<void *>(orig, 0), side ? port->m_bar : At<void *>(orig, 4),
                                side ? port->m_text : At<void *>(orig, 0x14)};
            const int n[3] = {side ? port->m_unused08 : At<int>(orig, 8), side ? port->m_unused0C : At<int>(orig, 0xC),
                              side ? port->m_textId : At<int>(orig, 0x10)};
            char text[60];
            std::snprintf(text, sizeof text, " %d %d text %d", n[0], n[1], n[2]);
            return "progress" + Ptrs(p, 3) + text;
        },
        [&](int side) { side ? void(port = new C_ProgressBar) : void(orig = static_cast<uint8_t *>(std::calloc(1, 0x40))); StartWords(side ? static_cast<void *>(port) : orig, sizeof(C_ProgressBar)); },
        [&](int side) { side ? delete port : std::free(orig); });
}

// ---- the helpers straight ----
void CompleteGroups(int frame)
{
    const int count = *reinterpret_cast<int *>(g_gfx.levels[frame] + 0x58);
    for (int g = 0; g < count; ++g) {
        void *&material = *reinterpret_cast<void **>(g_gfx.groups[frame][g] + 4);
        if (!material)
            material = g_gfx.materials[48 + g];
        void *&texture = *reinterpret_cast<void **>(static_cast<uint8_t *>(material) + 0xC);
        if (!texture)
            texture = g_gfx.textures[g];
        const char *&name = *reinterpret_cast<const char **>(static_cast<uint8_t *>(texture) + 0x18);
        if (!name)
            name = g_textureNames[texture == g_gfx.textures[g] ? g : 0];
    }
}
using T_Find = void *(__attribute__((fastcall)) *)(void *, const char *);
void TestHelpers()
{
    StartScenario();
    std::vector<S_Op> ops;
    for (int n = I(1, 8); n > 0; --n) {
        S_Op op{P(0.5) ? 1 : 2};   // FindTexture, DuplicateMaterial
        op.a = I(0, FRAMES - 1);
        // (FindTexture tests neither a material nor a texture: the frame's groups all have both.)
        if (op.kind == 1)
            CompleteGroups(op.a);
        op.b = I(0, int(sizeof TEXTURE_NAMES / sizeof TEXTURE_NAMES[0]) - 1);
        ops.push_back(op);
    }
    SaveStart();
    RunScenario(
        ops,
        [&](int side, const S_Op &op) {
            const char *name = TEXTURE_NAMES[op.b];
            void *r;
            if (op.kind == 1)
                r = side ? static_cast<void *>(FindTexture(FrameAt(op.a), name)) : OriginalAt<T_Find>(0x004a89e0)(FrameAt(op.a), name);
            else
                r = side ? static_cast<void *>(DuplicateMaterial(FrameAt(op.a), name)) : OriginalAt<T_Find>(0x004a8a90)(FrameAt(op.a), name);
            Log("-> %s", Name(r).c_str());
        },
        [](int) { return std::string("helpers"); }, [](int) {}, [](int) {});
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    SetUpGfx();
    for (int side = 0; side < 2; ++side) {
        for (void *&v : g_itemVt[side])
            v = reinterpret_cast<void *>(V_Other);
        for (void *&v : g_defVt[side])
            v = reinterpret_cast<void *>(V_Other);
        for (void *&v : g_vehicleVt[side])
            v = reinterpret_cast<void *>(V_Other);
        g_itemVt[side][0x0C / 4 + side] = reinterpret_cast<void *>(V_Amount);
        g_defVt[side][0x0C / 4 + side] = reinterpret_cast<void *>(V_Carry);
        g_defVt[side][0x10 / 4 + side] = reinterpret_cast<void *>(V_Behaviour);
        g_defVt[side][0x18 / 4 + side] = reinterpret_cast<void *>(V_AmmoId);
        g_vehicleVt[side][0x98 / 4 + side] = reinterpret_cast<void *>(V_HasSeatWeapon);
        g_vehicleVt[side][0x9C / 4 + side] = reinterpret_cast<void *>(V_SeatRounds);
        g_vehicleVt[side][0xA0 / 4 + side] = reinterpret_cast<void *>(V_SeatAmmo);
        g_vehicleVt[side][0xB4 / 4 + side] = reinterpret_cast<void *>(V_SeatDef);
    }
    *OriginalAt<void **>(0x008aea30) = g_settings;
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x005a2ac0, reinterpret_cast<const void *>(O_Hands)},
        {0x005a2af0, reinterpret_cast<const void *>(O_LongGun)},
        {0x005a6c50, reinterpret_cast<const void *>(O_CountAmmo)},
        {0x005a6d10, reinterpret_cast<const void *>(O_Thrown)},
        {0x005a5d10, reinterpret_cast<const void *>(O_Stance)},
        {0x005a5c80, reinterpret_cast<const void *>(O_State)},
        {0x005a8820, reinterpret_cast<const void *>(O_Team)},
        {0x005a9830, reinterpret_cast<const void *>(O_Progress)},
        {0x007dfab0, reinterpret_cast<const void *>(O_DefImage)},
        {0x0045d1d0, reinterpret_cast<const void *>(O_GetActivePlayer)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("status_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_STATUS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_STATUS_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_STATUS_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Speedometer", TestSpeedometer}, {"Reticle", TestReticle}, {"Weapons", TestWeapons},
                 {"Stance", TestStance},           {"Progress", TestProgress}, {"Helpers", TestHelpers}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("status_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
