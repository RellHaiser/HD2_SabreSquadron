// Human part 71: the map screen (game/interface_map.cpp: C_MapScreen and its zones' and objectives' parts) against the
// original's (0x004a4ae0..0x004a8020). MinGW; build_map.bat; runs map_diff.exe ("map_diff: N checks, 0 failures").
// interface_map.cpp is compiled into this file; the kit (interface_kit.inc, interface_gfx.inc) fakes the frames, the
// scenes, the driver, the graph and the text boxes. Scenarios of the map's operations (Init, Done, Show, Update, Render,
// HandleInput, SelectSpawnZone, ShowSelectedZone; between them the world changing) run from one start on each side (the
// same random words first), every call out logged by the same fakes, the map's fields (its markers, zones and
// objectives) snapped after each operation, the frames compared at the end.
// The world: 12 humans, 6 items, up to 18 map actors (mostly multiplayer zones), up to 6 mission objectives, the squad's
// first soldiers; each side its own blocks where the layouts differ (the humans, the items, the zones, the game, the
// mission, the squad: the port's vectors are libstdc++'s). Faked on both sides with the same loggers: the player's team
// (GetActivePlayer, GetMemberCount, GetMember, GetSeenLivingEnemies), the actors module (GetHuman; its actors and
// carnage enemy read), the humans (GetViewDir, IsUnconscious, IsInPlay, IsSpectating, GetTeam), the game (GetVipSlot;
// its mission name, mission and scene read), the items (GetFrame; the respawning items read), the map cursor (SetMode,
// GetHoveredId), the controls' ClearAction, the network game's GetLocalTeam, the text box's SetRect, the driver's
// CreateFrame, GetState and SetState and the model's Open.
// HD2_MAP_ROUNDS: scenarios per case (default 10,000; HD2_MAP_QUICK: 3,000); HD2_MAP_CASE: one case only;
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

#define INTERFACE_FRAMES 96
#define protected public
#define private public
#include "game/interface_map.cpp"
#include "game/mp_zone.h"
#include "game/text_box.h"
#include "net/net_game.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

template <class T> T &At(void *p, int offset) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + offset); }
template <class T> T At(const void *p, int offset) { return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + offset); }
template <class T> void Put(void *p, int offset, T v) { std::memcpy(static_cast<uint8_t *>(p) + offset, &v, sizeof v); }

// ---- the frames ----
// f0 the map model, f1 "center", f2 "map*", f3..f8 the scene's templates ("allied_active", "vip", "mapcamera", "enemy",
// "objective", "allied"), f9 "selectzone", f10 the interface's camera, f11 the game's camera; f12..f39 the map's parts
// and the humans' and items' frames; f40.. the visuals the driver creates.
constexpr int F_MODEL = 0, F_CENTER = 1, F_MAP = 2, F_ACTIVE = 3, F_VIP = 4, F_CAMMARK = 5, F_ENEMY = 6, F_ITEM = 7,
              F_ALLIED = 8, F_SELECT = 9, F_UICAMERA = 10, F_CAMERA = 11, F_POOL = 12, POOL = 28, F_VISUALS = 40;

// ---- the world ----
constexpr int HUMANS = 12, ITEMS = 6, ZONES = 18, OBJS = 6, SOLDIERS = 2, SLOTS = 32, LIST = 8;
struct S_Hum {
    int health, frame, netId, chatTime, team;
    bool unconscious, inPlay, spectating, pendingTeam;
    float view[4];
    char chat[24];
};
struct S_Zn {
    int type, objective, team, netId, frame;
};
struct S_ObjW {
    int status, team, alliedText, axisText;
};
struct S_World {
    S_Hum hum[HUMANS];
    int slot[SLOTS];   // GetHuman: a human or -1
    int active, members[LIST], memberCount, enemies[LIST], enemyCount, carnage, playMode;
    int vip;
    int itemOwner[ITEMS], itemFrame[ITEMS], respawn[ITEMS], respawnCount;
    int actorCount, zoneCount;   // the actors vector's size (the map actors from 32)
    S_Zn zone[ZONES];
    bool mission;
    int objCount;
    S_ObjW obj[OBJS];
    int soldierCount, soldierTeam[SOLDIERS], soldierZone[SOLDIERS];
    bool soldierNull;
    int localTeam, hovered;
    float held[64], pressed[64];
    uint32_t role;
    int gameType;
    int openResult;
    bool noCenter, noMap, noSelect, noAllied;
    uint32_t childSeed;
    int childMissing;   // percent of the parts not found
    bool noProps;
    char props[24];
    int scrnX, scrnY;
    char missionName[16];
};
S_World g_w, g_wStart;

// ---- each side's objects ----
alignas(16) uint8_t g_origHum[HUMANS][0x300], g_portHum[HUMANS][0x300];
alignas(16) uint8_t g_origItem[ITEMS][0x40], g_portItem[ITEMS][0x40];
alignas(16) uint8_t g_origZone[ZONES][0x100], g_portZone[ZONES][0x100];
alignas(16) uint8_t g_origGame[0x100], g_portGame[0x100], g_origMission[0x40], g_portMission[0x40];
alignas(16) uint8_t g_origSquad[0x20], g_portSquad[0x20], g_origSoldier[SOLDIERS][0x60], g_portSoldier[SOLDIERS][0x60];
alignas(16) uint8_t g_objectives[OBJS][0x40];                    // (the same layout: shared)
alignas(16) uint8_t g_zoneFrames[ZONES][0x100];                  // the zones' frames (their names)
alignas(16) uint8_t g_mapScene[0x400], g_gameScene[0x400], g_sector[0x40], g_cursor[0x80], g_playModeBlock[0x20];
alignas(16) uint8_t g_settings[0x400];
char g_zoneNames[ZONES][24];
uint8_t g_origNameRep[48];   // the original's C_str rep (the characters at +8)
C_str g_portName;
void *g_origVectors[4][64], *g_portVectors[4][64];   // the actors', the respawning items', the objectives', the soldiers' storage
int g_side;
int g_nextVisual;

void *Hum(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portHum[i]) : static_cast<void *>(g_origHum[i]); }
void *Item(int i) { return i < 0 ? nullptr : g_side ? static_cast<void *>(g_portItem[i]) : static_cast<void *>(g_origItem[i]); }
void *Zone(int i) { return g_side ? static_cast<void *>(g_portZone[i]) : static_cast<void *>(g_origZone[i]); }
void *Soldier(int i) { return g_side ? static_cast<void *>(g_portSoldier[i]) : static_cast<void *>(g_origSoldier[i]); }
template <size_t N, size_t M> int IndexIn(const void *p, uint8_t (&a)[N][M], uint8_t (&b)[N][M])
{
    for (size_t i = 0; i < N; ++i)
        if (p == a[i] || p == b[i])
            return int(i);
    return -1;
}
int HumIndex(const void *p) { return IndexIn(p, g_origHum, g_portHum); }
const S_Hum &HW(const void *p) { return g_w.hum[HumIndex(p)]; }
std::string PartName(const void *p)
{
    char text[24];
    int i;
    if ((i = HumIndex(p)) >= 0)
        std::snprintf(text, sizeof text, "h%d", i);
    else if ((i = IndexIn(p, g_origItem, g_portItem)) >= 0)
        std::snprintf(text, sizeof text, "i%d", i);
    else if ((i = IndexIn(p, g_origZone, g_portZone)) >= 0)
        std::snprintf(text, sizeof text, "z%d", i);
    else if ((i = IndexIn(p, g_origSoldier, g_portSoldier)) >= 0)
        std::snprintf(text, sizeof text, "s%d", i);
    else if ((i = IndexIn(p, g_objectives, g_objectives)) >= 0)
        std::snprintf(text, sizeof text, "obj%d", i);
    else if (p == g_mapScene)
        return "scene";
    else if (p == g_gameScene)
        return "gscene";
    else if (p == g_sector)
        return "sector";
    else if (p == g_cursor)
        return "cursor";
    else if (p == g_origMission || p == g_portMission)
        return "mission";
    else
        return "";
    return text;
}

// ---- the team, the actors module, the humans ----
void *ActivePlayer()
{
    Log("team GetActivePlayer -> h%d", g_w.active);
    return Hum(g_w.active);
}
int MemberCount()
{
    Log("team GetMemberCount");
    return g_w.memberCount;
}
void *Member(int index)
{
    Log("team GetMember %d", index);
    return index >= 0 && index < g_w.memberCount ? Hum(g_w.members[index]) : nullptr;
}
void SeenEnemiesLog() { Log("team GetSeenLivingEnemies -> %d", g_w.enemyCount); }
void *GetHuman(int id)
{
    const int h = id >= 0 && id < SLOTS ? g_w.slot[id] : -1;
    Log("actors GetHuman %d -> h%d", id, h);
    return Hum(h);
}
const float *ViewDir(const void *h)
{
    Log("%s GetViewDir", Name(h).c_str());
    return HW(h).view;
}
bool Unconscious(const void *h)
{
    Log("%s IsUnconscious", Name(h).c_str());
    return HW(h).unconscious;
}
bool InPlay(const void *h)
{
    Log("%s IsInPlay", Name(h).c_str());
    return HW(h).inPlay;
}
bool Spectating(const void *h)
{
    Log("%s IsSpectating", Name(h).c_str());
    return HW(h).spectating;
}
int Team(const void *h, bool pending)
{
    Log("%s GetTeam %d", Name(h).c_str(), pending);
    return HW(h).team;
}
void *__attribute__((thiscall)) O_ActivePlayer(void *) { return ActivePlayer(); }
int __attribute__((thiscall)) O_MemberCount(void *) { return MemberCount(); }
void *__attribute__((thiscall)) O_Member(void *, int index) { return Member(index); }
void __attribute__((thiscall)) O_SeenEnemies(void *, void **vector)
{
    SeenEnemiesLog();
    if (g_w.enemyCount) {
        void **v = static_cast<void **>(std::malloc(sizeof(void *) * size_t(g_w.enemyCount)));
        for (int k = 0; k < g_w.enemyCount; ++k)
            v[k] = Hum(g_w.enemies[k]);
        vector[1] = v;
        vector[2] = vector[3] = v + g_w.enemyCount;
    }
}
void *__attribute__((thiscall)) O_GetHuman(void *, int id) { return GetHuman(id); }
const float *__attribute__((thiscall)) O_ViewDir(void *h) { return ViewDir(h); }
bool __attribute__((thiscall)) O_Unconscious(void *h) { return Unconscious(h); }
bool __attribute__((thiscall)) O_InPlay(void *h) { return InPlay(h); }
bool __attribute__((thiscall)) O_Spectating(void *h) { return Spectating(h); }
int __attribute__((thiscall)) O_Team(void *h, bool pending) { return Team(h, pending); }

// ---- the game, the items, the cursor, the controls, the network game, the text boxes' rectangles ----
int VipSlot()
{
    Log("game GetVipSlot -> %d", g_w.vip);
    return g_w.vip;
}
void *ItemFrame(const void *item, int index)
{
    const int i = IndexIn(item, g_origItem, g_portItem);
    Log("i%d GetFrame %d", i, index);
    return FrameAt(g_w.itemFrame[i]);
}
void SetMode(int mode) { Log("cursor SetMode %d", mode); }
int Hovered()
{
    Log("cursor GetHoveredId -> %d", g_w.hovered);
    return g_w.hovered;
}
void ClearAction(int action, const float *held, const float *pressed)
{
    Log("controls ClearAction %d %s %s", action, held == g_w.held ? "held" : "?", pressed == g_w.pressed ? "pressed" : "?");
}
int LocalTeam()
{
    Log("net GetLocalTeam -> %d", g_w.localTeam);
    return g_w.localTeam;
}
void SetRect(const void *box, const float *r)
{
    std::string s;
    for (int k = 0; k < 7; ++k)
        s += " " + FloatText(r[k]);
    Log("%s SetRect%s", BoxName(box).c_str(), s.c_str());
}
void *__cdecl O_GetGame() { return g_origGame; }
int __attribute__((thiscall)) O_VipSlot(void *) { return VipSlot(); }
void *__attribute__((thiscall)) O_ItemFrame(void *item, int index) { return ItemFrame(item, index); }
void __attribute__((thiscall)) O_SetMode(void *, int mode) { SetMode(mode); }
int __attribute__((thiscall)) O_Hovered(void *) { return Hovered(); }
void __attribute__((thiscall)) O_ClearAction(void *, int action, float *held, float *pressed) { ClearAction(action, held, pressed); }
int __attribute__((thiscall)) O_LocalTeam(void *) { return LocalTeam(); }
void __attribute__((thiscall)) O_SetRect(void *box, const float *r) { SetRect(box, r); }

// ---- the driver and the model ----
uint32_t g_states[64];
void __stdcall MD_SetState(void *, int state, uint32_t value)
{
    Log("driver SetState %d %x", state, value);
    if (state >= 0 && state < 64)
        g_states[state] = value;
}
uint32_t __stdcall MD_GetState(void *, int state)
{
    Log("driver GetState %d", state);
    return state >= 0 && state < 64 ? g_states[state] : 0;
}
void *__stdcall MD_CreateFrame(void *, uint32_t type)
{
    Log("driver CreateFrame %u", type);
    return FrameAt(F_MODEL);
}
HRESULT __stdcall MF_Open(void *f, const char *name, uint32_t flags, void *callback, void *context)
{
    Log("%s Open %s %x %s %s", Name(f).c_str(), name, flags, callback ? "callback" : "null", context ? "context" : "null");
    return g_w.openResult;
}
I3D_frame *CreateVisual(uint32_t)
{
    I3D_frame *f = FrameAt(F_VISUALS + g_nextVisual);
    g_nextVisual = (g_nextVisual + 1) % (FRAMES - F_VISUALS);
    return f;
}
int __stdcall MG_ScrnX(void *)
{
    Log("graph ScrnX");
    return g_w.scrnX;
}
int __stdcall MG_ScrnY(void *)
{
    Log("graph ScrnY");
    return g_w.scrnY;
}

// ---- the scene's frames and the parts ----
I3D_frame *FindFrame(const char *name)
{
    static const struct {
        const char *name;
        int frame;
    } frames[] = {{"allied_active", F_ACTIVE}, {"vip", F_VIP}, {"mapcamera", F_CAMMARK}, {"enemy", F_ENEMY}, {"objective", F_ITEM},
                  {"allied", F_ALLIED}};
    for (const auto &f : frames)
        if (std::strcmp(name, f.name) == 0)
            return f.frame == F_ALLIED && g_w.noAllied ? nullptr : FrameAt(f.frame);
    return nullptr;
}
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
bool LocalRole() { return (g_w.role & 7) == 7; }
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    const int fi = FrameIndex(f);
    if (fi == F_MODEL) {
        if (std::strcmp(name, "center") == 0)
            return g_w.noCenter ? nullptr : FrameAt(F_CENTER);
        if (std::strcmp(name, "map*") == 0)
            return g_w.noMap ? nullptr : FrameAt(F_MAP);
        if (std::strcmp(name, "selectzone") == 0)
            return g_w.noSelect ? nullptr : FrameAt(F_SELECT);
        return nullptr;
    }
    const uint32_t h = Fnv(name, g_w.childSeed ^ (uint32_t(fi + 1) * 0x9E3779B1u));
    // (A zone's disabled part, a multiplayer objective's parts: always there (the original wrote to the first unchecked
    // and kept an objective without the others: the port's fixes, tested alone).)
    const bool needed = std::strcmp(name, "zone_disabled*") == 0 ||
                        (!LocalRole() && (std::strcmp(name, "allied_obj*") == 0 || std::strcmp(name, "axis_obj*") == 0));
    if (!needed && int(h % 100) < g_w.childMissing)
        return nullptr;
    return FrameAt(F_POOL + int((h >> 8) % POOL));
}

}  // namespace H

// ---- the port's side of the same fakes ----
static S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(H::g_settings);
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;
static S_MissionSquad *g_squadPointer = reinterpret_cast<S_MissionSquad *>(H::g_portSquad);
S_MissionSquad *&g_missionSquad = g_squadPointer;
static S_PlayMode *g_playModePointer = reinterpret_cast<S_PlayMode *>(H::g_playModeBlock);
S_PlayMode *&g_playMode = g_playModePointer;
C_Human *C_Team::GetActivePlayer() const { return static_cast<C_Human *>(H::ActivePlayer()); }
int C_TeamBase::GetMemberCount() const { return H::MemberCount(); }
C_Human *C_TeamBase::GetMember(int index) const { return static_cast<C_Human *>(H::Member(index)); }
void C_Team::GetSeenLivingEnemies(std::vector<C_Human *> &enemies) const
{
    H::SeenEnemiesLog();
    for (int k = 0; k < H::g_w.enemyCount; ++k)
        enemies.push_back(static_cast<C_Human *>(H::Hum(H::g_w.enemies[k])));
}
C_Actor *C_ActorsModule::GetHuman(int id) const { return static_cast<C_Actor *>(H::GetHuman(id)); }
const S_vector &C_Human::GetViewDir() const { return *reinterpret_cast<const S_vector *>(H::ViewDir(this)); }
bool C_Human::IsUnconscious() const { return H::Unconscious(this); }
bool C_Human::IsInPlay() const { return H::InPlay(this); }
bool C_Human::IsSpectating() const { return H::Spectating(this); }
int C_Human::GetTeam(bool pending) const { return H::Team(this, pending); }
int C_game::GetVipSlot() const { return H::VipSlot(); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
I3D_frame *C_Item::GetFrame(int index) { return static_cast<I3D_frame *>(H::ItemFrame(this, index)); }
void C_MapCursor::SetMode(int mode) { H::SetMode(mode); }
int C_MapCursor::GetHoveredId() { return H::Hovered(); }
void C_Controls::ClearAction(int action, float *held, float *pressed) { H::ClearAction(action, held, pressed); }
int C_NetGame::GetLocalTeam() { return H::LocalTeam(); }
void C_TextBox::SetRect(const float *rect) { H::SetRect(this, rect); }
S_MissionObjective *C_MissionDef::GetObjective(int index) const
{
    if (index < 0 || index >= GetObjectiveCount())
        return nullptr;
    return m_objectives[index];
}

namespace H {

// ---- building each side's objects from the world ----
template <class T> void PortVector(void *at, T *const *begin, int n)   // (libstdc++'s: start, finish, end of storage)
{
    T *const **v = static_cast<T *const **>(at);
    v[0] = n ? begin : nullptr;
    v[1] = v[2] = n ? begin + n : nullptr;
}
void OrigVector(void *at, void *const *begin, int n)   // (MSVC's: first, last, end, after the allocator's word)
{
    void *const **v = static_cast<void *const **>(at);
    v[0] = n ? begin : nullptr;
    v[1] = v[2] = n ? begin + n : nullptr;
}
void SyncWorld(int side)
{
    g_side = side;
    // The humans.
    for (int i = 0; i < HUMANS; ++i) {
        const S_Hum &h = g_w.hum[i];
        if (!side) {
            uint8_t *b = g_origHum[i];
            std::memset(b, 0, 0x300);
            Put<int>(b, 0x10, h.netId);
            Put<void *>(b, 0x20, FrameAt(h.frame));
            Put<int>(b, 0xEC, h.health);
            std::memcpy(b + 0x214, h.chat, sizeof h.chat);
            Put<int>(b, 0x254, h.chatTime);
        } else {
            uint8_t *b = g_portHum[i];
            std::memset(b, 0, 0x300);
            C_Human *p = reinterpret_cast<C_Human *>(b);
            p->m_netId = h.netId;
            p->m_frame = FrameAt(h.frame);
            p->m_stats[STAT_HEALTH] = h.health;
            std::memcpy(p->m_chatText, h.chat, sizeof h.chat);
            p->m_chatTime = h.chatTime;
        }
    }
    // The items (+0x20 the owner on both sides).
    for (int i = 0; i < ITEMS; ++i) {
        uint8_t *b = side ? g_portItem[i] : g_origItem[i];
        std::memset(b, 0, 0x40);
        Put<void *>(b, 0x20, Hum(g_w.itemOwner[i]));
    }
    void **items = side ? g_portVectors[1] : g_origVectors[1];
    for (int k = 0; k < g_w.respawnCount; ++k)
        items[k] = Item(g_w.respawn[k]);
    // The map actors (from 32; the ones before are not looked at).
    void **actors = side ? g_portVectors[0] : g_origVectors[0];
    for (int k = 0; k < g_w.actorCount; ++k)
        actors[k] = k >= int(FIRST_MAP_ACTOR) && k - int(FIRST_MAP_ACTOR) < g_w.zoneCount ? Zone(k - int(FIRST_MAP_ACTOR)) : nullptr;
    for (int i = 0; i < ZONES; ++i) {
        const S_Zn &z = g_w.zone[i];
        uint8_t *b = side ? g_portZone[i] : g_origZone[i];
        std::memset(b, 0, 0x100);
        if (!side) {
            Put<int>(b, 0x10, z.netId);
            Put<int>(b, 0x18, z.type);
            Put<void *>(b, 0x20, g_zoneFrames[z.frame]);
            Put<int>(b, 0x98, z.team);
            Put<int>(b, 0xA4, z.objective);
        } else {
            C_MPZone *p = reinterpret_cast<C_MPZone *>(b);
            p->m_netId = z.netId;
            p->m_type = E_ActorType(z.type);
            p->m_frame = reinterpret_cast<I3D_frame *>(g_zoneFrames[z.frame]);
            p->m_team = z.team;
            p->m_objective = z.objective;
        }
    }
    // The objectives (shared), the mission, the game.
    for (int i = 0; i < OBJS; ++i) {
        Put<int>(g_objectives[i], 0x10, g_w.obj[i].status);
        Put<int>(g_objectives[i], 0x18, g_w.obj[i].team);
        Put<int>(g_objectives[i], 0x1C, g_w.obj[i].alliedText);
        Put<int>(g_objectives[i], 0x20, g_w.obj[i].axisText);
    }
    void **objectives = side ? g_portVectors[2] : g_origVectors[2];
    for (int k = 0; k < g_w.objCount; ++k)
        objectives[k] = g_objectives[k];
    if (!side) {
        OrigVector(g_origMission + 0x34, objectives, g_w.objCount);
        std::memset(g_origGame, 0, sizeof g_origGame);
        std::memset(g_origNameRep, 0, sizeof g_origNameRep);
        std::memcpy(g_origNameRep + 8, g_w.missionName, sizeof g_w.missionName);
        Put<void *>(g_origGame, 0x6A, g_origNameRep);
        Put<void *>(g_origGame, 0x18, g_gameScene);
        Put<void *>(g_origGame, 0xA8, g_w.mission ? g_origMission : nullptr);
        OrigVector(OriginalAt<uint8_t *>(0x0089b4b0) + 0x10, actors, g_w.actorCount);
        OrigVector(OriginalAt<uint8_t *>(0x0098b81c), items, g_w.respawnCount);
        *OriginalAt<void **>(0x0089b504) = Hum(g_w.carnage);
    } else {
        PortVector(g_portMission + offsetof(C_MissionDef, m_objectives), objectives, g_w.objCount);
        std::memset(g_portGame, 0, sizeof g_portGame);
        g_portName = C_str(g_w.missionName);
        std::memcpy(g_portGame + offsetof(C_game, m_missionName), &g_portName, sizeof g_portName);
        Put<void *>(g_portGame, offsetof(C_game, m_scene), g_gameScene);
        Put<void *>(g_portGame, offsetof(C_game, m_mission), g_w.mission ? g_portMission : nullptr);
        PortVector(reinterpret_cast<uint8_t *>(&g_actorsModule) + offsetof(C_ActorsModule, m_actors), actors, g_w.actorCount);
        PortVector(reinterpret_cast<uint8_t *>(&g_itemsModule) + offsetof(C_ItemsModule, m_respawningItems), items, g_w.respawnCount);
        Put<void *>(&g_actorsModule, offsetof(C_ActorsModule, m_carnageEnemy), Hum(g_w.carnage));
    }
    // The squad: its first soldiers (+0x04 the spawn zone, +0x40 the side; the zone kept from the last operation).
    void **soldiers = side ? g_portVectors[3] : g_origVectors[3];
    for (int k = 0; k < g_w.soldierCount; ++k)
        soldiers[k] = k == 0 && g_w.soldierNull ? nullptr : Soldier(k);
    for (int k = 0; k < SOLDIERS; ++k) {
        Put<int>(Soldier(k), 0x40, g_w.soldierTeam[k]);
        Put<int>(Soldier(k), 0x04, g_w.soldierZone[k]);
    }
    if (!side)
        OrigVector(g_origSquad + 8, soldiers, g_w.soldierCount);
    else
        PortVector(g_portSquad + offsetof(S_MissionSquad, states), soldiers, g_w.soldierCount);
    // The play mode, the role, the game type, the map's user properties.
    Put<int>(g_playModeBlock, 8, g_w.playMode);
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    Put<int>(g_settings, 0x224, g_w.gameType);
    FrameAt(F_MAP)->m_userProps = g_w.noProps ? nullptr : g_w.props;
}
// After an operation: the soldiers' spawn zones written back (both sides write the same ones).
void ReadBack()
{
    for (int k = 0; k < SOLDIERS; ++k)
        g_w.soldierZone[k] = At<int>(Soldier(k), 0x04);
}

const char *const CHATS[] = {"", "", "hello", "Go %s go", "a", "Need ammo!", "100%", "x y z"};
const char *const PROPS[] = {"scale 0.05", "scale=2", "scale0.01", "xscale 3.5", "", "size 2", "scale -1.25e1", "scale 1e40",
                             "scale 0x10", "scale inf", "scale  .5", "scale", "scale 7 scale 9"};
void RandomHuman(int i)
{
    S_Hum &h = g_w.hum[i];
    h = S_Hum{};
    h.health = P(0.75) ? I(1, 800) : Pick({0, -1, 1});
    h.frame = F_POOL + I(0, POOL - 1);
    h.netId = P(0.8) ? I(0, 13) : Pick({-1, 31, 100});
    h.chatTime = P(0.5) ? I(0, 2000) : P(0.5) ? Pick({0, 250, 251, 500, 749, 750, -1, -251}) : int(U32());
    h.team = P(0.8) ? I(0, 1) : Pick({2, -1, 3});
    h.unconscious = P(0.2);
    h.inPlay = P(0.85);
    h.spectating = P(0.15);
    for (float &v : h.view)
        v = P(0.15) ? Pick({0.0f, 1.0f, -1.0f, 1e-5f}) : F(-1, 1);
    // (Now and then x * x + z * z exactly 1e-8: the normalisation's bound.)
    if (P(0.05)) {
        h.view[0] = FromBits(P(0.5) ? 0x38bcbe62u : 0xb8bcbe62u);
        h.view[2] = FromBits(P(0.5) ? 0x3836d34fu : 0xb836d34fu);
    }
    std::snprintf(h.chat, sizeof h.chat, "%s", P(0.5) ? "" : Pick(CHATS));
}
void RandomHumans()
{
    for (int i = 0; i < HUMANS; ++i)
        RandomHuman(i);
}
void RandomSlots()
{
    for (int &s : g_w.slot)
        s = P(0.4) ? -1 : I(0, HUMANS - 1);
}
void RandomTeam()
{
    g_w.active = P(0.97) ? I(0, HUMANS - 1) : -1;
    g_w.memberCount = P(0.1) ? 0 : P(0.9) ? I(1, 6) : I(7, LIST);
    for (int k = 0; k < g_w.memberCount; ++k)
        g_w.members[k] = P(0.25) && g_w.active >= 0 ? g_w.active : I(0, HUMANS - 1);
    g_w.enemyCount = P(0.3) ? 0 : I(1, LIST);
    for (int k = 0; k < g_w.enemyCount; ++k)
        g_w.enemies[k] = I(0, HUMANS - 1);
    g_w.carnage = P(0.5) ? -1 : I(0, HUMANS - 1);
    g_w.playMode = P(0.4) ? PLAY_MODE_CARNAGE : I(0, 4);
}
void RandomVip()
{
    g_w.vip = P(0.4) ? -1 : I(0, 13);
    // (The VIP's slot holds a human: the original and the port read it unchecked.)
    if (g_w.vip >= 0 && g_w.vip < SLOTS && g_w.slot[g_w.vip] < 0)
        g_w.slot[g_w.vip] = I(0, HUMANS - 1);
    if (g_w.vip >= 0 && P(0.4) && g_w.active >= 0)
        g_w.hum[g_w.active].netId = g_w.vip;
}
void RandomItems()
{
    for (int i = 0; i < ITEMS; ++i) {
        g_w.itemOwner[i] = P(0.6) ? -1 : I(0, HUMANS - 1);
        g_w.itemFrame[i] = F_POOL + I(0, POOL - 1);
    }
    g_w.respawnCount = P(0.3) ? 0 : I(1, ITEMS);
    for (int k = 0; k < g_w.respawnCount; ++k)
        g_w.respawn[k] = I(0, ITEMS - 1);
}
void RandomZoneSides()
{
    for (S_Zn &z : g_w.zone)
        z.team = P(0.85) ? I(0, 2) : Pick({-1, 3});
}
void RandomStatuses()
{
    for (S_ObjW &o : g_w.obj)
        o.status = P(0.85) ? I(0, 4) : Pick({-1, 5, 100});
}
void RandomSquad()
{
    g_w.soldierCount = P(0.1) ? 0 : I(1, SOLDIERS);
    g_w.soldierNull = P(0.05);
    for (int k = 0; k < SOLDIERS; ++k) {
        g_w.soldierTeam[k] = P(0.85) ? I(0, 2) : Pick({-1, 3});
        // (Mostly a zone's net id.)
        g_w.soldierZone[k] = P(0.6) && g_w.zoneCount ? g_w.zone[I(0, g_w.zoneCount - 1)].netId : Pick({-1, 0, 1, 7, 99});
    }
}
void RandomInput()
{
    g_w.localTeam = P(0.85) ? I(0, 2) : Pick({-1, 3});
    g_w.hovered = P(0.7) ? 1000 + I(0, 16) : Pick({999, 0, -1, 100, 1015, 1016, 2000});
    for (float &f : g_w.held)
        f = P(0.8) ? 0.0f : 1.0f;
    for (float &f : g_w.pressed)
        f = P(0.8) ? 0.0f : 1.0f;
    g_w.pressed[INPUT_CLICK] = P(0.4) ? 0.0f : Pick({1.0f, -1.0f, 0.5f, -0.0f});
}
// The structure: the zones, the objectives, the map's parts (fixed for a scenario: the map holds them from Init).
void RandomStructure()
{
    g_w.actorCount = P(0.1) ? I(0, 33) : int(FIRST_MAP_ACTOR) + I(0, ZONES);
    g_w.zoneCount = std::max(0, g_w.actorCount - int(FIRST_MAP_ACTOR));
    for (int i = 0; i < ZONES; ++i) {
        S_Zn &z = g_w.zone[i];
        z.type = P(0.85) ? int(ACTOR_MP_ZONE) : Pick({int(ACTOR_HUMAN), int(ACTOR_SPAWN_POINT), 0, 36, 38});
        z.objective = P(0.8) ? -1 : I(0, 4);
        z.netId = P(0.9) ? 200 + i : Pick({0, -1, 1});
        z.frame = P(0.85) ? i : I(0, ZONES - 1);
    }
    // (Now and then more than 15 zones: the map shows none.)
    if (P(0.1)) {
        g_w.actorCount = int(FIRST_MAP_ACTOR) + ZONES;
        g_w.zoneCount = ZONES;
        for (S_Zn &z : g_w.zone)
            z.type = ACTOR_MP_ZONE, z.objective = -1;
    }
    g_w.objCount = P(0.15) ? 0 : I(1, OBJS);
    for (S_ObjW &o : g_w.obj) {
        o.team = P(0.85) ? I(0, 1) : Pick({2, -1});
        o.alliedText = 0x1000 + I(0, 0xFF);
        o.axisText = 0x2000 + I(0, 0xFF);
    }
    g_w.childSeed = U32();
    g_w.childMissing = P(0.3) ? 0 : P(0.5) ? I(0, 30) : I(30, 90);
    g_w.openResult = P(0.05) ? Pick({1, -1, int(0x80004005)}) : 0;
    g_w.noCenter = P(0.04);
    g_w.noSelect = P(0.05);
    g_w.noAllied = P(0.05);
    g_w.noProps = P(0.05);
    std::snprintf(g_w.props, sizeof g_w.props, "%s", Pick(PROPS));
    std::snprintf(g_w.missionName, sizeof g_w.missionName, "%s", Pick({"Africa1", "m01", "", "Norway 3"}));
    g_w.role = P(0.4) ? 7u : Pick({0u, 1u, 2u, 3u, 6u, 0xFu, 0x17u});
    g_w.gameType = P(0.85) ? I(0, 3) : Pick({4, -1, 6});
    // (A multiplayer game has its mission: the objectives read unchecked.)
    g_w.mission = !LocalRole() || P(0.9);
    // (No "map*" now and then: only without "center" (Show sets the map on with it) and where nothing is put on it
    // (deathmatch, single player without objectives): the original and the port read it unchecked there.)
    g_w.noMap = g_w.noCenter && P(0.5) && (LocalRole() ? !g_w.mission || g_w.objCount == 0 : g_w.gameType == 0);
    g_w.scrnX = P(0.6) ? 1024 : Pick({640, 800, 1600, 1});
    g_w.scrnY = P(0.6) ? 768 : Pick({480, 600, 1200, 1});
}
void RandomWorld()
{
    RandomHumans();
    RandomSlots();
    RandomTeam();
    RandomVip();
    RandomItems();
    RandomZoneSides();
    RandomStatuses();
    RandomSquad();
    RandomInput();
}
void RandomWorldFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}
int g_humanSeed;
void RandomOneHuman() { RandomHuman(g_humanSeed); }
void RandomTeamVip()
{
    RandomTeam();
    RandomVip();
}
void RandomSlotsVip()
{
    RandomSlots();
    RandomVip();
}
// A frame of the pool moved (the humans', the items', the parts'), or a camera.
int g_frameSeed;
void MoveFrame()
{
    I3D_frame *f = FrameAt(g_frameSeed);
    for (int r = 0; r < 4; ++r)
        f->m_worldMatrix(r) = S_vector(F(-300, 300), F(-50, 50), F(-300, 300));
    // (The camera's view mostly tiny or none: the normalisations' corners.)
    if (P(g_frameSeed == F_CAMERA ? 0.6 : 0.2))
        f->m_worldMatrix(2) = S_vector(Pick({0.0f, 1e-5f, -1e-5f}), Pick({0.0f, -1e-5f, 1e-5f}), Pick({0.0f, 2.0f, 1e-5f, -1e-5f}));
    else if (P(g_frameSeed == F_CAMERA ? 0.3 : 0.0))
        f->m_worldMatrix(2) = S_vector(FromBits(0x38bcbe62u), 0.0f, FromBits(0x3836d34fu));   // (the square exactly 1e-8)
    f->m_frameFlags = P(0.5) ? f->m_frameFlags | FRMFLAGS_WMATRIX_VALID : f->m_frameFlags & ~uint32_t(FRMFLAGS_WMATRIX_VALID);
}

// ---- the operations ----
struct S_Op {
    int kind;
    int a = 0, b = 0;
};
enum {
    OP_INIT, OP_DONE, OP_SHOW, OP_UPDATE, OP_RENDER, OP_INPUT, OP_SELECTZONE, OP_SHOWZONE,
    W_WORLD = 100, W_HUMAN, W_TEAM, W_SLOTS, W_ITEMS, W_ZONES, W_STATUS, W_SQUAD, W_INPUT, W_FRAME
};
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case W_WORLD:
        RandomWorldFrom(op.a, &RandomWorld);
        break;
    case W_HUMAN:
        g_humanSeed = op.b;
        RandomWorldFrom(op.a, &RandomOneHuman);
        break;
    case W_TEAM:
        RandomWorldFrom(op.a, &RandomTeamVip);
        break;
    case W_SLOTS:
        RandomWorldFrom(op.a, &RandomSlotsVip);
        break;
    case W_ITEMS:
        RandomWorldFrom(op.a, &RandomItems);
        break;
    case W_ZONES:
        RandomWorldFrom(op.a, &RandomZoneSides);
        break;
    case W_STATUS:
        RandomWorldFrom(op.a, &RandomStatuses);
        break;
    case W_SQUAD:
        RandomWorldFrom(op.a, &RandomSquad);
        break;
    case W_INPUT:
        RandomWorldFrom(op.a, &RandomInput);
        break;
    case W_FRAME:
        g_frameSeed = op.b;
        RandomWorldFrom(op.a, &MoveFrame);
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
uint32_t g_seed;
uint32_t g_startWords[12];
uint8_t g_staleBox[0x10];   // (a stale text box pointer in the start markers: named, not used)
void RandomCamera(int i)
{
    // (The range the depth is taken in: +0x124 near, +0x128 far.)
    const float nearRange = P(0.2) ? Pick({0.0f, 0.1f, 1.0f}) : F(0, 2), farRange = P(0.2) ? Pick({0.0f, 1.0f, 100.0f}) : F(0.5f, 50);
    Put<float>(FrameAt(i), 0x124, nearRange);
    Put<float>(FrameAt(i), 0x128, farRange);
}
void StartScenario()
{
    g_seed = U32();
    for (uint32_t &w : g_startWords)
        w = U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomCamera(F_UICAMERA);
    // (Now and then the map exactly at the near range: a depth of 0.)
    if (P(0.1)) {
        const float nearRange = std::fabs(FrameAt(F_MAP)->m_worldMatrix(3).z - FrameAt(F_UICAMERA)->m_worldMatrix(3).z) - 0.4f;
        Put<float>(FrameAt(F_UICAMERA), 0x124, nearRange);
        Put<float>(FrameAt(F_UICAMERA), 0x128, nearRange + F(0.5f, 50));
    }
    RandomCamera(F_CAMERA);
    RandomStructure();
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
    g_nextVisual = 0;
    std::memset(g_states, 0, sizeof g_states);
    g_states[0x15] = g_seed & 1;
    g_states[0x19] = g_seed >> 1 & 1;
    g_states[0x11] = g_seed >> 2 & 3;
    std::srand(g_seed);
}

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Show = void(__attribute__((thiscall)) *)(void *, bool, bool);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Input = void(__attribute__((thiscall)) *)(void *, float *, float *);

// The map's fields, each side's (the original's vectors at +0x1C0.. are MSVC's: the allocator's word first).
struct S_View {
    I3D_frame *frame, *map, *select;
    int kind, markerCount;
    std::vector<uint8_t *> zones, objectives, spObjectives;
    std::vector<void *> enemies, items;
};
template <class T> std::vector<T> OrigList(const void *map, int at)
{
    std::vector<T> v;
    for (T *p = At<T *>(map, at + 4); p && p != At<T *>(map, at + 8); ++p)
        v.push_back(*p);
    return v;
}
S_View View(const void *self, int side)
{
    S_View v;
    v.frame = At<I3D_frame *>(self, 4);
    v.map = At<I3D_frame *>(self, 8);
    v.select = At<I3D_frame *>(self, 0xC);
    v.kind = At<int>(self, 0x18);
    v.markerCount = At<int>(self, 0x1C);
    if (!side) {
        v.enemies = OrigList<void *>(self, 0x1C0);
        v.items = OrigList<void *>(self, 0x1D0);
        v.zones = OrigList<uint8_t *>(self, 0x1E0);
        v.objectives = OrigList<uint8_t *>(self, 0x1F0);
        v.spObjectives = OrigList<uint8_t *>(self, 0x200);
    } else {
        const C_MapScreen *p = static_cast<const C_MapScreen *>(self);
        v.enemies.assign(p->m_enemyMarkers.begin(), p->m_enemyMarkers.end());
        v.items.assign(p->m_itemMarkers.begin(), p->m_itemMarkers.end());
        for (auto *z : p->m_zones)
            v.zones.push_back(reinterpret_cast<uint8_t *>(z));
        for (auto *o : p->m_objectives)
            v.objectives.push_back(reinterpret_cast<uint8_t *>(o));
        for (auto *o : p->m_spObjectives)
            v.spObjectives.push_back(reinterpret_cast<uint8_t *>(o));
    }
    return v;
}
std::string Snap(const void *self, int side)
{
    const S_View v = View(self, side);
    char text[400];
    std::snprintf(text, sizeof text, "map %s %s %s %s %s %s kind %d count %d scale %s command %d depth %s active %s vip %s camera %s item %s enemy %s",
                  Name(At<void *>(self, 0)).c_str(), Name(v.frame).c_str(), Name(v.map).c_str(), Name(v.select).c_str(),
                  Name(At<void *>(self, 0x10)).c_str(), Name(At<void *>(self, 0x14)).c_str(), v.kind, v.markerCount,
                  FloatText(At<float>(self, 0x20)).c_str(), At<uint8_t>(self, 0x24), FloatText(At<float>(self, 0x28)).c_str(),
                  Name(At<void *>(self, 0x2C)).c_str(), Name(At<void *>(self, 0x1B0)).c_str(), Name(At<void *>(self, 0x1B4)).c_str(),
                  Name(At<void *>(self, 0x1B8)).c_str(), Name(At<void *>(self, 0x1BC)).c_str());
    std::string s = text;
    s += " markers";
    for (int k = 0; k < C_MapScreen::MARKERS; ++k) {
        const uint8_t *m = static_cast<const uint8_t *>(self) + 0x30 + 0xC * k;
        const void *box = At<void *>(m, 4);
        s += " " + Name(At<void *>(m, 0)) + ":" + (box ? BoxName(box) : std::string("null")) + ":" + std::to_string(m[8]);
    }
    s += " enemies";
    for (void *f : v.enemies)
        s += " " + Name(f);
    s += " items";
    for (void *f : v.items)
        s += " " + Name(f);
    s += " zones";
    for (const uint8_t *z : v.zones)
        s += " " + Name(At<void *>(z, 0)) + ":" + Name(At<void *>(z, 4)) + ":" + Name(At<void *>(z, 8)) + ":" + std::to_string(At<int>(z, 0xC)) +
             ":" + std::to_string(At<int>(z, 0x10)) + ":" + Name(At<void *>(z, 0x14));
    s += " objectives";
    for (const uint8_t *o : v.objectives) {
        const void *box = At<void *>(o, 0x18);
        s += " " + Name(At<void *>(o, 0)) + ":" + Name(At<void *>(o, 4)) + ":" + Name(At<void *>(o, 8)) + ":" + std::to_string(At<int>(o, 0xC)) +
             ":" + std::to_string(At<int>(o, 0x10)) + ":" + Name(At<void *>(o, 0x14)) + ":" + (box ? BoxName(box) : std::string("null"));
    }
    s += " sp";
    for (const uint8_t *o : v.spObjectives)
        s += " " + Name(At<void *>(o, 0)) + ":" + Name(At<void *>(o, 4)) + ":" + Name(At<void *>(o, 8)) + ":" + std::to_string(At<int>(o, 0xC)) +
             ":" + Name(At<void *>(o, 0x10));
    s += " soldiers";
    for (int k = 0; k < SOLDIERS; ++k)
        s += " " + std::to_string(At<int>(Soldier(k), 0x04));
    return s;
}

// The original's (and the port's) crashes left out: a marker never created (Done before CreateMarkers ran), the
// selection marker missing in a team game (Show, Update, ShowSelectedZone), no active soldier, more squad members than
// markers (the original wrote past them: the port's fix, tested alone).
bool MarkersMissing(const void *self, const S_View &v)
{
    for (int k = 0; k < v.markerCount && k < C_MapScreen::MARKERS; ++k)
        if (!At<void *>(self, 0x30 + 0xC * k))
            return true;
    return false;
}
bool TeamKind(int kind) { return kind != 0 && kind != 1; }
const char *Skip(const void *self, const S_View &v, const S_Op &op)
{
    switch (op.kind) {
    case OP_DONE:
        return MarkersMissing(self, v) ? "a marker never created" : nullptr;
    case OP_SHOW:
        return v.frame && op.a && TeamKind(v.kind) && !v.select ? "no selection marker" : nullptr;
    case OP_SHOWZONE:
        return !v.select ? "no selection marker" : nullptr;
    case OP_UPDATE:
        if (!v.frame || v.kind < 0 || v.kind > 4)
            return nullptr;
        if (g_w.active < 0)
            return "no active soldier";
        if (v.kind == 0 && g_w.memberCount > v.markerCount)
            return "more members than markers";
        if (v.kind >= 2 && !v.select)
            return "no selection marker";
        return nullptr;
    default:
        return nullptr;
    }
}
void Run(int side, C_MapScreen *port, uint8_t *orig, const S_Op &op)
{
    void *self = side ? static_cast<void *>(port) : orig;
    if (const char *why = Skip(self, View(self, side), op)) {
        Log("skipped: %s", why);
        return;
    }
    switch (op.kind) {
    case OP_INIT:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(g_mapScene), reinterpret_cast<C_MapCursor *>(g_cursor))
                               : OriginalAt<T_Init>(0x004a4ae0)(orig, g_mapScene, g_cursor));
        break;
    case OP_DONE:
        side ? port->Done() : OriginalAt<T_Void>(0x004a4f60)(orig);
        break;
    case OP_SHOW:
        side ? port->Show(op.a != 0, op.b != 0) : OriginalAt<T_Show>(0x004a5630)(orig, op.a != 0, op.b != 0);
        break;
    case OP_UPDATE:
        side ? port->Update(op.a) : OriginalAt<T_Int>(0x004a6fa0)(orig, op.a);
        break;
    case OP_RENDER:
        side ? port->Render() : OriginalAt<T_Void>(0x004a6fe0)(orig);
        break;
    case OP_INPUT:
        side ? port->HandleInput(g_w.held, g_w.pressed) : OriginalAt<T_Input>(0x004a7010)(orig, g_w.held, g_w.pressed);
        break;
    case OP_SELECTZONE:
        side ? port->SelectSpawnZone() : OriginalAt<T_Void>(0x004a7050)(orig);
        break;
    case OP_SHOWZONE:
        side ? port->ShowSelectedZone() : OriginalAt<T_Void>(0x004a7180)(orig);
        break;
    }
}
// (The original's SetScale writes m_scaleW (+0xDC) from a stack word; the port leaves it: not compared.)
void ClearScaleW(S_Arena &a)
{
    for (int i = 0; i < FRAMES; ++i)
        Put<uint32_t>(a.frames[i], 0xDC, 0);
}
void RunScenario(const std::vector<S_Op> &ops)
{
    for (int side = 0; side < 2; ++side) {
        StartSide();
        uint8_t *orig = static_cast<uint8_t *>(std::calloc(1, 0x210));
        C_MapScreen *port = static_cast<C_MapScreen *>(std::calloc(1, sizeof(C_MapScreen)));
        uint8_t *self = side ? reinterpret_cast<uint8_t *>(port) : orig;
        // (The same start words in the fields Init leaves when it fails or keeps for other game types: the kind, the
        // markers' count, the selection marker, the command map's flag, the scale and the depth.)
        Put<int>(self, 0x18, int(g_startWords[0] % 7));
        Put<int>(self, 0x1C, int(g_startWords[1] % (C_MapScreen::MARKERS + 1)));
        Put<void *>(self, 0xC, g_startWords[2] % 3 ? FrameAt(F_POOL + int(g_startWords[2] % POOL)) : nullptr);
        Put<uint8_t>(self, 0x24, uint8_t(g_startWords[3]));
        Put<uint32_t>(self, 0x20, g_startWords[4]);
        Put<uint32_t>(self, 0x28, g_startWords[5]);
        // (The frame and the map (Init clears them when it fails), the markers: frames of the arena, a stale text box
        // and the chat flag past the count (CreateMarkers clears what it does not make).)
        Put<void *>(self, 4, g_startWords[6] % 3 ? FrameAt(F_POOL + int(g_startWords[6] % POOL)) : nullptr);
        Put<void *>(self, 8, g_startWords[7] % 3 ? FrameAt(F_POOL + int(g_startWords[7] % POOL)) : nullptr);
        for (int k = 0; k < C_MapScreen::MARKERS; ++k) {
            const bool past = k >= At<int>(self, 0x1C);
            Put<void *>(self, 0x30 + 0xC * k, FrameAt(F_POOL + int((g_startWords[8] + k) % POOL)));
            Put<void *>(self, 0x34 + 0xC * k, past ? g_staleBox : nullptr);
            Put<uint8_t>(self, 0x38 + 0xC * k, past);
        }
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            if (op.kind >= 100)
                ApplyWorld(op);
            SyncWorld(side);
            if (op.kind < 100)
                Run(side, port, orig, op);
            ReadBack();
            g_log.push_back(Snap(self, side));
        }
        std::free(orig);
        std::free(port);
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
        ClearScaleW(g_runs[side].arena);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

void FillOp(S_Op &op)
{
    switch (op.kind) {
    case OP_SHOW:
        op.a = P(0.7);
        op.b = P(0.5);
        break;
    case OP_UPDATE:
        op.a = P(0.5) ? Pick({0, 16, 100}) : I(-5, 1000);
        break;
    case W_WORLD:
    case W_TEAM:
    case W_SLOTS:
    case W_ITEMS:
    case W_ZONES:
    case W_STATUS:
    case W_SQUAD:
    case W_INPUT:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case W_HUMAN:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = I(0, HUMANS - 1);
        break;
    case W_FRAME:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = P(0.75) ? F_POOL + I(0, POOL - 1) : Pick({F_CAMERA, F_CAMERA, F_UICAMERA, F_MAP});
        break;
    }
}

// ---- the map ----
void TestMap()
{
    StartScenario();
    std::vector<S_Op> ops;
    ops.push_back(S_Op{OP_INIT});
    static const int KINDS[] = {OP_SHOW, OP_SHOW, OP_UPDATE, OP_UPDATE, OP_UPDATE, OP_UPDATE, OP_RENDER, OP_RENDER, OP_INPUT, OP_INPUT,
                                OP_SELECTZONE, OP_SHOWZONE, W_WORLD, W_HUMAN, W_HUMAN, W_TEAM, W_SLOTS, W_ITEMS, W_ZONES, W_STATUS,
                                W_STATUS, W_SQUAD, W_INPUT, W_INPUT, W_FRAME, W_FRAME};
    for (int n = I(0, 16); n > 0; --n) {
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
// A zone without "zone_disabled*": its other parts set (the original wrote its data at null + 0xEC); no active soldier
// in single player: nothing done (the original read its frame); more squad members than markers: the markers' only (the
// original went on past them); a multiplayer objective without its parts: dropped (the original kept it half set up).
I3D_frame *FindChildFixes(I3D_frame *f, const char *name)
{
    if (std::strcmp(name, "zone_disabled*") == 0 || std::strcmp(name, "allied_obj*") == 0)
        return nullptr;
    return FindChild(f, name);
}
void TestFixes()
{
    StartScenario();
    g_w.openResult = 0;
    g_w.noCenter = g_w.noMap = g_w.noSelect = false;
    g_w.childMissing = 0;
    SaveStart();
    StartSide();
    SyncWorld(1);
    g_findChild = FindChildFixes;
    // The zone and the objective.
    C_MapScreen::S_Zone zone;
    zone.Init(FrameAt(F_POOL), reinterpret_cast<C_MPZone *>(g_portZone[0]), 1003);
    CHECK(zone.id == 1003 && zone.side == 2 && zone.allied && zone.axis && zone.allied->m_data == reinterpret_cast<S_FrameData *>(intptr_t(1003)),
          "zone without its disabled part: %d %d", zone.id, zone.side);
    C_MapScreen map{};
    map.m_map = FrameAt(F_MAP);
    g_w.mission = true;
    g_w.objCount = 3;
    SyncWorld(1);
    map.CreateObjectives(reinterpret_cast<I3D_scene *>(g_mapScene));
    CHECK(map.m_objectives.empty(), "objectives without their parts: %u kept", unsigned(map.m_objectives.size()));
    g_findChild = FindChild;
    // Single player without an active soldier.
    g_w.role = 7;
    SyncWorld(1);
    CHECK(map.Init(reinterpret_cast<I3D_scene *>(g_mapScene), reinterpret_cast<C_MapCursor *>(g_cursor)), "Init: false");
    CHECK(map.m_kind == 0 && map.m_markerCount == 6, "single player: kind %d count %d", map.m_kind, map.m_markerCount);
    g_w.active = -1;
    SyncWorld(1);
    g_log.clear();
    map.Update(16);
    CHECK(g_log.size() == 1, "no active soldier: %u calls", unsigned(g_log.size()));
    // More members than markers: the first six placed, the rest left.
    g_w.active = 0;
    g_w.memberCount = LIST;
    for (int k = 0; k < LIST; ++k)
        g_w.members[k] = 1 + k % (HUMANS - 1);
    SyncWorld(1);
    g_log.clear();
    map.Update(16);
    int asked = 0;
    for (const std::string &line : g_log)
        asked += line.rfind("team GetMember ", 0) == 0;
    CHECK(asked == map.m_markerCount + 1, "more members than markers: %d asked", asked);
    map.Done();
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    g_createVisual = CreateVisual;
    SetUpGfx();
    g_graphVt[0x18 / 4] = reinterpret_cast<void *>(MG_ScrnX);
    g_graphVt[0x1C / 4] = reinterpret_cast<void *>(MG_ScrnY);
    g_driverVt[0x18 / 4] = reinterpret_cast<void *>(MD_SetState);
    g_driverVt[0x1C / 4] = reinterpret_cast<void *>(MD_GetState);
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(MD_CreateFrame);
    g_frameVt[0x74 / 4] = reinterpret_cast<void *>(MF_Open);
    // The scenes (the interface's: +0x1F8 the primary sector, +0x15C the camera; the game's: its camera).
    Put<void *>(g_mapScene, 0, g_sceneVt);
    Put<void *>(g_mapScene, 0x1F8, g_sector);
    Put<void *>(g_mapScene, 0x15C, FrameAt(F_UICAMERA));
    Put<void *>(g_gameScene, 0, g_sceneVt);
    Put<void *>(g_gameScene, 0x15C, FrameAt(F_CAMERA));
    // The zones' frames' names (+0xE8 the name, +0xE6 the short name's offset).
    for (int i = 0; i < ZONES; ++i) {
        const bool prefixed = i % 3 == 1;
        std::snprintf(g_zoneNames[i], sizeof g_zoneNames[i], "%szone%c", prefixed ? "map01." : "", 'A' + i);
        Put<const char *>(g_zoneFrames[i], 0xE8, i % 7 == 6 ? nullptr : g_zoneNames[i]);
        Put<int16_t>(g_zoneFrames[i], 0xE6, prefixed ? 6 : 0);
    }
    // The original's globals: the squad, the play mode, the settings; the chat lines' rectangle (its first-call statics,
    // set here: their atexit is the CRT's of the mapped copy).
    *OriginalAt<void **>(0x008aea20) = g_origSquad;
    *OriginalAt<void **>(0x008aea34) = g_playModeBlock;
    *OriginalAt<void **>(0x008aea30) = g_settings;
    *OriginalAt<uint32_t *>(0x0089bcc0) |= 1;
    const float chat[] = {0.01f, -0.01f, 0.0f, 0.0f, 0.4f, 0.01f, 0.0f, 0.0f};
    float *const chatAt = OriginalAt<float *>(0x0089bca0);
    for (int k = 0; k < 8; ++k)
        chatAt[k] = chat[k];
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x0045d1d0, reinterpret_cast<const void *>(O_ActivePlayer)},
        {0x00462300, reinterpret_cast<const void *>(O_MemberCount)},
        {0x004619f0, reinterpret_cast<const void *>(O_Member)},
        {0x0045ed10, reinterpret_cast<const void *>(O_SeenEnemies)},
        {0x00475e00, reinterpret_cast<const void *>(O_GetHuman)},
        {0x005a80e0, reinterpret_cast<const void *>(O_ViewDir)},
        {0x005a6380, reinterpret_cast<const void *>(O_Unconscious)},
        {0x005a63b0, reinterpret_cast<const void *>(O_InPlay)},
        {0x005a63f0, reinterpret_cast<const void *>(O_Spectating)},
        {0x005a8820, reinterpret_cast<const void *>(O_Team)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x0069e340, reinterpret_cast<const void *>(O_VipSlot)},
        {0x007d3f00, reinterpret_cast<const void *>(O_ItemFrame)},
        {0x004abaf0, reinterpret_cast<const void *>(O_SetMode)},
        {0x004abdd0, reinterpret_cast<const void *>(O_Hovered)},
        {0x006c73f0, reinterpret_cast<const void *>(O_ClearAction)},
        {0x00730880, reinterpret_cast<const void *>(O_LocalTeam)},
        {0x00644ea0, reinterpret_cast<const void *>(O_SetRect)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("map_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_MAP_QUICK") ? 3000 : 10000;
    if (const char *r = std::getenv("HD2_MAP_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_MAP_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Map", TestMap}, {"MapFixes", TestFixes}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("map_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
