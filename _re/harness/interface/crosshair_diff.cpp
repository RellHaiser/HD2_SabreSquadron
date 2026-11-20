// Human part 66b: the crosshair (game/interface_status.cpp: C_Crosshair and the SetPosX / SetPosY helpers) against the
// original's (0x004a9740..0x004aaac0). MinGW; build_crosshair.bat; runs crosshair_diff.exe ("crosshair_diff: N checks, 0
// failures"). interface_status.cpp is compiled into this file; the kit (interface_kit.inc, interface_gfx.inc) fakes the
// frames, the scene, the graph, the text boxes and the graphics. Scenarios of operations (Init, Done, Show, SetTarget,
// SetColor, Update, Render; between them the world changing) run from one start on each side, every call out logged by
// the same fakes, the crosshair's fields snapped after each operation, the frames and the graphics compared at the end.
// Faked on both sides: the actors (four humans and four others, raw blocks: the type, the vehicle, the carried body, the
// health, the role at both sides' offsets; GetName, GetSeatOccupant, GetSeatCount virtual: each side's vtable before its
// run, the port's slots 4 bytes on), the humans' IsFriendOf (0x005a0ab0: the original's IsHostileTo runs as it is),
// GetFullName, GetProfileUnknown24, GetStat, GetHealthPercent, GetAccuracy, the car's HasEnemyAboard (0x004dde20), the
// team's IsMember and IndexOf (0x00461a20, 0x00461a60), the texts and the rank names, the profile's IsCrosshairAllowed
// (0x006b69f0), the active player. LS3DF's S_quat::Make and Normalize on both sides.
// HD2_CROSSHAIR_ROUNDS: scenarios (default 30,000; HD2_CROSSHAIR_QUICK: 6,000); HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW,
// HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

namespace H {

// ---- the frames: the cross (f0), its parts (f1..f4 L R T B), "cross_name" (f5) ----
const char *const PARTS[] = {"cross_L", "cross_R", "cross_T", "cross_B", "cross_name"};
bool g_nameMissing;
I3D_frame *FindFrame(const char *name) { return std::strcmp(name, "cross") == 0 ? FrameAt(0) : nullptr; }
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    if (FrameIndex(f) != 0)
        return nullptr;
    for (int k = 0; k < 5; ++k)
        if (std::strcmp(name, PARTS[k]) == 0)
            return k == 4 && g_nameMissing ? nullptr : FrameAt(1 + k);
    return nullptr;
}

// ---- the actors: h0..h3 humans, a0..a3 others ----
constexpr int HUMANS = 4, OTHERS = 4, ACTORS = HUMANS + OTHERS;
alignas(16) uint8_t g_actors[ACTORS][0x800], g_actorsStart[ACTORS][0x800];
C_str g_names[ACTORS];
struct S_World {
    bool friendOf[ACTORS][ACTORS];   // [self][other]
    bool member[HUMANS];
    int index[HUMANS];
    int rank[HUMANS], firstAid[HUMANS], health[HUMANS];
    float accuracy[HUMANS];
    int seatCount[ACTORS];
    int occupant[ACTORS][4];          // a human or -1
    bool enemyAboard;
    bool crosshairAllowed;
    int active;
};
S_World g_world, g_worldStart;
void *g_actorVt[2][64];
int ActorIndex(const void *p)
{
    for (int i = 0; i < ACTORS; ++i)
        if (p == g_actors[i])
            return i;
    return -1;
}
std::string ActorName(const void *p)
{
    const int i = ActorIndex(p);
    if (i < 0)
        return Name(p);
    return (i < HUMANS ? "h" : "a") + std::to_string(i < HUMANS ? i : i - HUMANS);
}
C_Actor *ActorAt(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Actor *>(g_actors[i]); }
C_Human *HumanAt(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_actors[i]); }

void *__attribute__((thiscall)) V_GetName(void *a)
{
    Log("%s GetName", ActorName(a).c_str());
    return &g_names[ActorIndex(a)];
}
void *__attribute__((thiscall)) V_SeatOccupant(void *a, int seat)
{
    Log("%s GetSeatOccupant %d", ActorName(a).c_str(), seat);
    const int o = seat >= 0 && seat < 4 ? g_world.occupant[ActorIndex(a)][seat] : -1;
    return HumanAt(o);
}
int __attribute__((thiscall)) V_SeatCount(void *a)
{
    Log("%s GetSeatCount", ActorName(a).c_str());
    return g_world.seatCount[ActorIndex(a)];
}
void __attribute__((thiscall)) V_Other(void *a) { Log("%s other slot", ActorName(a).c_str()); }
bool FriendOf(const void *self, const void *other)
{
    Log("%s IsFriendOf %s", ActorName(self).c_str(), ActorName(other).c_str());
    return g_world.friendOf[ActorIndex(self)][ActorIndex(other)];
}
const char *FullName(const void *h)
{
    Log("%s GetFullName", ActorName(h).c_str());
    static const char *const names[HUMANS] = {"John Smith", "Bill O'Hara", "", "X"};
    return names[ActorIndex(h)];
}
int Rank(const void *h)
{
    Log("%s GetProfileUnknown24", ActorName(h).c_str());
    return g_world.rank[ActorIndex(h)];
}
int Stat(const void *h, int stat)
{
    Log("%s GetStat %d", ActorName(h).c_str(), stat);
    return stat == STAT_FIRST_AID ? g_world.firstAid[ActorIndex(h)] : 0;
}
int HealthPercent(const void *h)
{
    Log("%s GetHealthPercent", ActorName(h).c_str());
    return g_world.health[ActorIndex(h)];
}
float Accuracy(const void *h)
{
    Log("%s GetAccuracy", ActorName(h).c_str());
    return g_world.accuracy[ActorIndex(h)];
}
bool EnemyAboard(const void *car, const void *h)
{
    Log("%s HasEnemyAboard %s", ActorName(car).c_str(), ActorName(h).c_str());
    return g_world.enemyAboard;
}
bool IsMember(const void *h)
{
    Log("team IsMember %s", ActorName(h).c_str());
    return g_world.member[ActorIndex(h)];
}
int IndexOf(const void *h)
{
    Log("team IndexOf %s", ActorName(h).c_str());
    return g_world.index[ActorIndex(h)];
}
bool CrosshairAllowed()
{
    Log("profile IsCrosshairAllowed");
    return g_world.crosshairAllowed;
}
C_str TextOf(int id)
{
    Log("texts Get %x", id);
    char text[16];
    std::snprintf(text, sizeof text, "T%x", id);
    return C_str(text);
}
bool __attribute__((thiscall)) O_FriendOf(void *self, void *other) { return FriendOf(self, other); }
const char *__attribute__((thiscall)) O_FullName(void *h) { return FullName(h); }
int __attribute__((thiscall)) O_Rank(void *h) { return Rank(h); }
int __attribute__((thiscall)) O_Stat(void *h, int stat) { return Stat(h, stat); }
int __attribute__((thiscall)) O_Health(void *h) { return HealthPercent(h); }
float __attribute__((thiscall)) O_Accuracy(void *h) { return Accuracy(h); }
bool __attribute__((thiscall)) O_EnemyAboard(void *car, void *h) { return EnemyAboard(car, h); }
bool __attribute__((thiscall)) O_IsMember(void *, void *h) { return IsMember(h); }
int __attribute__((thiscall)) O_IndexOf(void *, void *h) { return IndexOf(h); }
bool __attribute__((thiscall)) O_CrosshairAllowed(void *) { return CrosshairAllowed(); }
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    C_str s = TextOf(id);
    *static_cast<void **>(out) = s.m_rep;
    s.m_rep = nullptr;
    return out;
}
void *__attribute__((thiscall)) O_GetActivePlayer(void *) { return HumanAt(g_world.active); }
void SetSide(int side)
{
    for (auto &a : g_actors)
        *reinterpret_cast<void ***>(a) = g_actorVt[side];
}

uint32_t &OrigRole() { return *OriginalAt<uint32_t *>(0x00982100); }
std::string PartName(const void *p)
{
    const std::string g = GfxName(p);
    if (!g.empty())
        return g;
    if (ActorIndex(p) >= 0)
        return ActorName(p);
    for (const void *b : g_boxes)
        if (b && p == b)
            return BoxName(p);
    return "";
}

}  // namespace H

// ---- the port's side ----
bool C_Human::IsFriendOf(const C_Actor *other) const { return H::FriendOf(this, other); }
const char *C_Human::GetFullName() const { return H::FullName(this); }
int C_Human::GetProfileUnknown24() const { return H::Rank(this); }
int C_Human::GetStat(int stat) const { return H::Stat(this, stat); }
int C_Human::GetHealthPercent() const { return H::HealthPercent(this); }
float C_Human::GetAccuracy() const { return H::Accuracy(this); }
bool C_Car::HasEnemyAboard(const C_Human *human) { return H::EnemyAboard(this, human); }
bool C_TeamBase::IsMember(const C_Human *human) const { return H::IsMember(human); }
int C_TeamBase::IndexOf(const C_Human *human) const { return H::IndexOf(human); }
bool C_GameProfile::IsCrosshairAllowed() const { return H::CrosshairAllowed(); }
C_str C_GameTexts::Get(int id) const { return H::TextOf(id); }
C_Human *C_Team::GetActivePlayer() const { return H::HumanAt(H::g_world.active); }
S_RankName g_rankNames[8];
// (The other panels' callees: part 66a's; here unexpected.)
namespace {
void Unexpected(const char *what)
{
    H::Log("UNEXPECTED %s", what);
    ++H::failures;
    std::printf("FAIL unexpected call: %s\n", what);
}
}  // namespace
C_Item *C_Human::GetHandsItem() const { return Unexpected("GetHandsItem"), nullptr; }
C_Item *C_Human::GetLongGunItem() const { return Unexpected("GetLongGunItem"), nullptr; }
int C_Human::CountAmmo(int, bool) const { return Unexpected("CountAmmo"), 0; }
int C_Human::CountThrownItems() const { return Unexpected("CountThrownItems"), 0; }
uint32_t C_Human::GetStanceFlags() const { return Unexpected("GetStanceFlags"), 0; }
uint32_t C_Human::GetStateFlags() const { return Unexpected("GetStateFlags"), 0; }
int C_Human::GetTeam(bool) const { return Unexpected("GetTeam"), 0; }
bool C_Human::GetActionProgress(int *, int *, int *) const { return Unexpected("GetActionProgress"), false; }
IImage *C_ItemDef::GetImage() { return Unexpected("GetImage"), nullptr; }
void __stdcall S_quat::Make(const S_vector &, float) { Unexpected("S_quat::Make (axis)"); }
alignas(16) static uint8_t g_settingsBlock[0x400];
static S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(g_settingsBlock);
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;

constexpr uint32_t P_TYPE = offsetof(C_Actor, m_type), P_VEHICLE = offsetof(C_Human, m_vehicle), P_CARRIED = offsetof(C_Human, m_carried),
                   P_HEALTH = offsetof(C_Human, m_stats), P_ROLE = offsetof(C_Human, m_role);
static_assert(sizeof(C_Human) <= 0x800, "actor block");

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

// ---- the scenarios ----
enum { OP_INIT, OP_DONE, OP_SHOW, OP_TARGET, OP_COLOR, OP_UPDATE, OP_RENDER, W_ACTIVE = 100, W_FRIEND, W_MEMBER, W_HUMAN, W_ACTOR, W_SEATS, W_GAME };
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
    float f[4] = {};
    int seats[4] = {-1, -1, -1, -1};
};
uint32_t g_role, g_seed;
uint32_t g_startWords[0x10];
static_assert(sizeof(C_Crosshair) <= sizeof g_startWords, "start words");
const int TYPES[] = {ACTOR_MINE,  ACTOR_DOOR,          ACTOR_RADIO,           ACTOR_LADDER,         ACTOR_SUPPLY_BOX, ACTOR_ITEM,
                     ACTOR_SWITCH, ACTOR_PHOTO_PLACE,  ACTOR_EXPLOSIVE_PLACE, ACTOR_DOCUMENT_PLACE, ACTOR_WIRE_CUT,   ACTOR_CAR,
                     ACTOR_CAR,    ACTOR_SUBMARINE,    ACTOR_STATIC_WEAPON,   ACTOR_STATIC_WEAPON,  ACTOR_MELEE_TRACKER,
                     ACTOR_BARREL, ACTOR_SPAWN_POINT,  ACTOR_BIKE,            ACTOR_TS_EXPLOSION,   1,                0};
void RandomHumanFields(int h)
{
    Both(g_actors[h], 0xEC, P_HEALTH, P(0.8) ? I(1, 100) : Pick({0, -3}));
    Both(g_actors[h], 0x19C, P_ROLE, uint32_t(P(0.4) ? 0 : I(0, 5)));   // (0: a player)
}
void RandomWorld()
{
    for (auto &row : g_world.friendOf)
        for (bool &f : row)
            f = P(0.5);
    for (int h = 0; h < HUMANS; ++h) {
        g_world.member[h] = P(0.6);
        g_world.index[h] = I(0, 5);
        g_world.rank[h] = I(0, 7);
        g_world.firstAid[h] = P(0.3) ? Pick({49, 50, 51}) : I(0, 100);
        g_world.health[h] = I(0, 100);
        g_world.accuracy[h] = P(0.2) ? Pick({0.0f, 1.0f, 0.4f, -1.0f, 1e6f}) : F(0, 2);
    }
    for (int a = 0; a < ACTORS; ++a) {
        g_world.seatCount[a] = I(0, 4);
        for (int &o : g_world.occupant[a])
            o = P(0.4) ? -1 : I(0, HUMANS - 1);
    }
    g_world.enemyAboard = P(0.4);
    g_world.crosshairAllowed = P(0.8);
    g_world.active = I(0, HUMANS - 1);
    for (int a = 0; a < ACTORS; ++a) {
        std::memset(g_actors[a], 0, sizeof g_actors[a]);
        Both(g_actors[a], 0x18, P_TYPE, a < HUMANS ? int(ACTOR_HUMAN) : Pick(TYPES));
        g_names[a] = C_str(P(0.1) ? "" : a < HUMANS ? "Soldier" : "Thing");
    }
    for (int h = 0; h < HUMANS; ++h) {
        RandomHumanFields(h);
        Both(g_actors[h], 0x160, P_VEHICLE, static_cast<void *>(P(0.2) ? g_actors[HUMANS + I(0, OTHERS - 1)] : nullptr));
        Both(g_actors[h], 0x174, P_CARRIED, static_cast<void *>(P(0.15) ? g_actors[I(0, ACTORS - 1)] : nullptr));
    }
}
S_Op RandomTarget()
{
    S_Op op{OP_TARGET};
    op.a = P(0.1) ? -1 : I(0, ACTORS - 1);
    op.b = P(0.6);
    return op;
}
S_Op RandomWorldOp()
{
    S_Op op{Pick({W_ACTIVE, W_FRIEND, W_FRIEND, W_MEMBER, W_HUMAN, W_ACTOR, W_SEATS, W_GAME})};
    switch (op.kind) {
    case W_ACTIVE:
        op.a = I(0, HUMANS - 1);
        break;
    case W_FRIEND:
        op.a = I(0, ACTORS - 1);
        op.b = I(0, ACTORS - 1);
        op.c = P(0.5);
        break;
    case W_MEMBER:
        op.a = I(0, HUMANS - 1);
        op.b = P(0.6);
        op.c = I(0, 5);
        break;
    case W_HUMAN:
        op.a = I(0, HUMANS - 1);
        op.b = P(0.8) ? I(1, 100) : Pick({0, -3});
        op.c = P(0.4) ? 0 : I(0, 5);
        op.f[0] = P(0.2) ? Pick({0.0f, 1.0f, -1.0f}) : F(0, 2);
        op.f[1] = float(I(0, 100));
        break;
    case W_ACTOR:
        op.a = I(HUMANS, ACTORS - 1);
        op.b = Pick(TYPES);
        break;
    case W_SEATS:
        op.a = I(0, ACTORS - 1);
        op.b = I(0, 4);
        for (int &s : op.seats)
            s = P(0.4) ? -1 : I(0, HUMANS - 1);
        op.c = P(0.4);
        break;
    case W_GAME:
        op.a = P(0.6) ? 7 : Pick({0, 1, 3, 6});
        op.b = P(0.8);
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
    case W_FRIEND:
        g_world.friendOf[op.a][op.b] = op.c != 0;
        break;
    case W_MEMBER:
        g_world.member[op.a] = op.b != 0;
        g_world.index[op.a] = op.c;
        break;
    case W_HUMAN:
        Both(g_actors[op.a], 0xEC, P_HEALTH, op.b);
        Both(g_actors[op.a], 0x19C, P_ROLE, uint32_t(op.c));
        g_world.accuracy[op.a] = op.f[0];
        g_world.health[op.a] = int(op.f[1]);
        break;
    case W_ACTOR:
        Both(g_actors[op.a], 0x18, P_TYPE, op.b);
        break;
    case W_SEATS:
        g_world.seatCount[op.a] = op.b;
        std::copy(op.seats, op.seats + 4, g_world.occupant[op.a]);
        g_world.enemyAboard = op.c != 0;
        break;
    case W_GAME:
        g_gameRole = uint32_t(op.a);
        OrigRole() = uint32_t(op.a);
        g_world.crosshairAllowed = op.b != 0;
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Gfx gfx;
};
S_Run g_runs[2];

std::string Snap(const void *const *p, float spread, const float *color, const void *target, bool usable, bool shown, const void *name)
{
    // (Not the colour's w: SetColor copies it from the caller's array, the original's a stack word; nothing reads it.)
    std::string s = "crosshair";
    for (int k = 0; k < 5; ++k)
        s += " " + Name(p[k]);
    char text[200];
    std::snprintf(text, sizeof text, " spread %s colour %s %s %s target %s usable %d shown %d name %s", FloatText(spread).c_str(),
                  FloatText(color[0]).c_str(), FloatText(color[1]).c_str(), FloatText(color[2]).c_str(), ActorName(target).c_str(), usable,
                  shown, Name(name).c_str());
    return s + text;
}
std::string SnapPort(const C_Crosshair &c)
{
    const void *p[5] = {c.m_frame, c.m_left, c.m_right, c.m_top, c.m_bottom};
    return Snap(p, c.m_spread, c.m_color, c.m_target, c.m_usable, c.m_shown, c.m_name);
}
std::string SnapOrig(const uint8_t *c)
{
    const void *p[5];
    for (int k = 0; k < 5; ++k)
        p[k] = At<void *>(c, k * 4);
    const float color[3] = {At<float>(c, 0x20), At<float>(c, 0x24), At<float>(c, 0x28)};
    return Snap(p, At<float>(c, 0x14), color, At<void *>(c, 0x30), At<bool>(c, 0x34), At<bool>(c, 0x35), At<void *>(c, 0x38));
}

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Target = void(__attribute__((thiscall)) *)(void *, void *, bool);
using T_Color = void(__attribute__((thiscall)) *)(void *, const float *);

void TestScenarios()
{
    g_seed = U32();
    for (uint32_t &w : g_startWords)
        w = P(0.3) ? Pick({0u, 0x3F800000u, 1u}) : U32();
    g_role = P(0.6) ? 7u : Pick({0u, 1u, 3u, 6u});
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomGfx();
    // (The parts' first face groups have materials: Init and SetColor use them unchecked, as the game's always have.)
    for (int i = 1; i <= 4; ++i)
        if (!*reinterpret_cast<void **>(g_gfx.groups[i][0] + 4))
            *reinterpret_cast<void **>(g_gfx.groups[i][0] + 4) = g_gfx.materials[i];
    g_nameMissing = P(0.2);
    RandomWorld();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{OP_INIT});
    bool alive = true;
    for (int n = I(1, 20); n > 0 && alive; --n) {
        if (P(0.3)) {
            ops.push_back(RandomWorldOp());
            continue;
        }
        S_Op op{Pick({OP_SHOW, OP_TARGET, OP_TARGET, OP_TARGET, OP_TARGET, OP_COLOR, OP_UPDATE, OP_UPDATE, OP_RENDER, OP_DONE})};
        switch (op.kind) {
        case OP_SHOW:
            op.a = P(0.7);
            break;
        case OP_TARGET:
            op = RandomTarget();
            break;
        case OP_COLOR:
            for (float &v : op.f)
                v = P(0.5) ? Pick({0.0f, 1.0f}) : F(0, 1);
            break;
        case OP_UPDATE:
            op.a = P(0.6) ? Pick({0, 1, 16, 33, 100, 1000, 100000}) : I(-50, 3000);
            break;
        case OP_DONE:
            if (P(0.6))
                op.kind = OP_RENDER;
            alive = op.kind != OP_DONE;
            break;
        }
        ops.push_back(op);
    }
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    std::memcpy(g_actorsStart, g_actors, sizeof g_actors);
    g_worldStart = g_world;

    for (int side = 0; side < 2; ++side) {
        std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
        std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
        std::memcpy(g_actors, g_actorsStart, sizeof g_actors);
        g_world = g_worldStart;
        g_log.clear();
        g_boxes.clear();
        g_gameRole = g_role;
        OrigRole() = g_role;
        SetSide(side);
        std::srand(g_seed);
        C_Crosshair *port = side ? new C_Crosshair : nullptr;
        uint8_t *orig = side ? nullptr : static_cast<uint8_t *>(std::calloc(1, 0x80));
        // (The same random words on both sides first: Init must set what the crosshair reads; the classes' fields all
        // 4 bytes or bytes, laid out alike.)
        std::memcpy(side ? static_cast<void *>(port) : orig, g_startWords, sizeof(C_Crosshair));
        bool made = false;
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            switch (op.kind) {
            case OP_INIT:
                Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(0x004a9740)(orig, &g_scene));
                made = true;
                break;
            case OP_DONE:
                side ? port->Done() : OriginalAt<T_Void>(0x004a9f00)(orig);
                made = false;
                break;
            case OP_SHOW:
                side ? port->Show(op.a != 0) : OriginalAt<T_Bool>(0x004a9f10)(orig, op.a != 0);
                break;
            case OP_TARGET:
                side ? port->SetTarget(ActorAt(op.a), op.b != 0) : OriginalAt<T_Target>(0x004a9f50)(orig, ActorAt(op.a), op.b != 0);
                break;
            case OP_COLOR:
                side ? port->SetColor(op.f) : OriginalAt<T_Color>(0x004aa760)(orig, op.f);
                break;
            case OP_UPDATE:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004aa840)(orig, op.a);
                break;
            case OP_RENDER:
                side ? port->Render() : OriginalAt<T_Void>(0x004aaab0)(orig);
                break;
            default:
                ApplyWorld(op);
                break;
            }
            g_log.push_back(side ? SnapPort(*port) : SnapOrig(orig));
        }
        (void)made;
        side ? delete port : std::free(orig);
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        std::memcpy(&g_runs[side].gfx, &g_gfx, sizeof g_gfx);
    }
    // (+0xDC, m_scaleW: the inline SetScale's stack word; put back from the start on both sides.)
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

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    SetUpGfx();
    for (int side = 0; side < 2; ++side) {
        for (void *&v : g_actorVt[side])
            v = reinterpret_cast<void *>(V_Other);
        g_actorVt[side][0x04 / 4 + side] = reinterpret_cast<void *>(V_GetName);
        g_actorVt[side][0x78 / 4 + side] = reinterpret_cast<void *>(V_SeatOccupant);
        g_actorVt[side][0x88 / 4 + side] = reinterpret_cast<void *>(V_SeatCount);
    }
    for (int r = 0; r < 8; ++r) {
        g_rankNames[r].textId = 0x2790 + r;
        *OriginalAt<int *>(0x008ae960 + r * 0x10) = 0x2790 + r;
    }
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x005a0ab0, reinterpret_cast<const void *>(O_FriendOf)},
        {0x005a8320, reinterpret_cast<const void *>(O_FullName)},
        {0x005a8380, reinterpret_cast<const void *>(O_Rank)},
        {0x005a6600, reinterpret_cast<const void *>(O_Stat)},
        {0x005a5ff0, reinterpret_cast<const void *>(O_Health)},
        {0x005a8100, reinterpret_cast<const void *>(O_Accuracy)},
        {0x004dde20, reinterpret_cast<const void *>(O_EnemyAboard)},
        {0x00461a20, reinterpret_cast<const void *>(O_IsMember)},
        {0x00461a60, reinterpret_cast<const void *>(O_IndexOf)},
        {0x006b69f0, reinterpret_cast<const void *>(O_CrosshairAllowed)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
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
    if (!StartKit("crosshair_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_CROSSHAIR_QUICK") ? 6000 : 30000;
    if (const char *r = std::getenv("HD2_CROSSHAIR_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Scenarios";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    std::printf("crosshair_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
