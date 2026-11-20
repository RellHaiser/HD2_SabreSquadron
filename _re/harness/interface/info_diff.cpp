// Human part 69: the mission information and the scores (game/interface_info.cpp: C_InfoScreen) against the original's
// (0x00496410..0x00499ad0). MinGW; build_info.bat; runs info_diff.exe ("info_diff: N checks, 0 failures").
// interface_info.cpp is compiled into this file; the kit (interface_kit.inc, interface_gfx.inc) fakes the frames, the
// scene, the driver, the graph and the text boxes. Scenarios of the screen's operations (Init, Done, Update, Render,
// Show, IsShown, AddHint, ClearHints, SaveHints, LoadHints; between them the world changing) run from one start on each
// side (the same random words first), every call out logged by the same fakes, the screen's fields snapped after each
// operation, the frames compared at the end.
// Faked on both sides with the same loggers: the driver's CreateFrame (0x54), the model's SetName (0x34), the scene's
// AddFrame (0x80) and primary sector (+0x1F8), the model cache (the port's OpenInto; the original's inline lookup:
// FUN_00409660's slot answered, the cache's vtable slot 0x30), the text styles (CreateTextStyle, ReleaseTextStyle: every
// word of a style logged), the graph's render states (0x74, 0x78), the texts (the same strings kept for both: the
// original reads a hint's characters after releasing its copy), the key bindings' ReplaceTags (a table of three tags),
// the campaign's current mission, the missions and objectives (per side: the vectors' layouts differ), the game (round
// state, time, mission), the settings, the net game (the players, LockPlayers, UnlockPlayers, GetLocalTeam, the teams'
// scores and wins, the server's name), the players' IsSpectating, the humans' health and uniform (the original's
// inline path: +0x74, +0x10, the slot 0x40), the items' definitions and teams, rw_data's file calls (in memory).
// HD2_INFO_ROUNDS: scenarios per case (default 20,000; HD2_INFO_QUICK: 5,000); HD2_INFO_CASE: one case only;
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
#include "game/resource_cache.h"
#include "ls3d/rw_data.h"
// (The test's model cache: declared before the port's source uses it.)
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *model, const char *name, uint32_t, void *, void *, int);
#include "game/interface_info.cpp"
#include "game/game_texts.h"
#include "game/resource_cache.h"
#include "game/text_box.h"
#include "game/text_style.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

// ---- the frames ----
// f0 the model ("ingame_info"), f1 "game_info", f2 "header", f3 "text", f4 the scene's primary sector.
constexpr int F_MODEL = 0, F_INFO = 1, F_HEADER = 2, F_TEXT = 3, F_SECTOR = 4;
bool g_missing[FRAMES];
I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    if (FrameIndex(f) != F_MODEL)
        return nullptr;
    int at = -1;
    if (std::strcmp(name, "game_info") == 0)
        at = F_INFO;
    else if (std::strcmp(name, "header") == 0)
        at = F_HEADER;
    else if (std::strcmp(name, "text") == 0)
        at = F_TEXT;
    return at < 0 || g_missing[at] ? nullptr : FrameAt(at);
}
alignas(16) uint8_t g_sceneBlock[0x400];   // the scene Init gets (its vtable the kit's, +0x1F8 the primary sector)
void *__stdcall D_CreateFrame(void *, int type)
{
    Log("driver CreateFrame %d -> %s", type, Name(FrameAt(F_MODEL)).c_str());
    return FrameAt(F_MODEL);
}
// The driver's states up to 0x3F (the kit's keep 0..15; Init's are 0x15 and 0x19).
uint32_t g_states[64], g_statesStart[64];
void __stdcall D_SetState2(void *, int state, uint32_t value)
{
    Log("driver SetState %d %x", state, value);
    g_states[state & 63] = value;
}
uint32_t __stdcall D_GetState2(void *, int state)
{
    Log("driver GetState %d", state);
    return g_states[state & 63];
}
void __stdcall F_SetName(void *f, const char *name) { Log("%s SetName %s", Name(f).c_str(), name); }
void __stdcall SC_AddFrame(void *, void *f) { Log("scene AddFrame %s", Name(f).c_str()); }

// ---- the model cache ----
HRESULT g_openResult;   // the port's OpenInto (the original's cached path does not look at it)
void *g_cacheVt[32];
void *g_cachedModel = &g_cacheVt;   // FUN_00409660's slot: an entry (the original's cached path)
void CacheOpen(const void *model, const char *name) { Log("modelcache OpenInto %s %s", Name(model).c_str(), name); }
void **__attribute__((thiscall)) O_CacheFind(void *, void *) { return &g_cachedModel; }
HRESULT __attribute__((thiscall)) O_CacheDuplicate(void *, void *model, void *)
{
    CacheOpen(model, OriginalAt<const char *>(0x0083a8f8));
    return 0;
}
void __attribute__((thiscall)) O_CacheOther(void *) { Log("modelcache other slot"); }

// ---- the text styles ----
int g_nextStyle, g_nextStyleStart;
int CreateStyle(const void *desc)
{
    uint32_t w[13];
    std::memcpy(w, desc, sizeof w);
    std::string s;
    char text[12];
    for (uint32_t v : w) {
        std::snprintf(text, sizeof text, " %x", v);
        s += text;
    }
    // (The words: font, colour, size, width, x, y, below, above, +0x20, align, valign, joins / wraps, flags.)
    Log("CreateTextStyle%s -> %d", s.c_str(), g_nextStyle);
    return g_nextStyle++;
}
void ReleaseStyle(int style) { Log("ReleaseTextStyle %d", style); }
int __attribute__((fastcall)) O_CreateStyle(const void *desc) { return CreateStyle(desc); }
void __attribute__((fastcall)) O_ReleaseStyle(int style) { ReleaseStyle(style); }

// ---- the graph's render states ----
uint32_t g_renderStates[8];
void __stdcall G_SetRenderState(void *, int state, int value) { Log("graph SetRenderState %d %x", state, value); }
int __stdcall G_GetRenderState(void *, int state)
{
    Log("graph GetRenderState %d", state);
    return int(g_renderStates[state & 7]);
}

// ---- the texts ----
std::map<int, C_str> g_textTable;
// The hints' texts (0x6000 ..): tags known and not, '<' and '>' in other orders, none, empty, unset.
const char *const HINT_TEXTS[] = {
    "Press <FIRE> to fire", "<FIRE> and <JUMP>", "a < b > c", "<>", "x > y < z", "<UNKNOWN>", "", "<USE><USE><USE>", "no tags",
    "<JUMP", "<FIRE> <BAD>", "<<FIRE>>", nullptr, "<FIRE>", "<b>", "tail <USE>", "a > b",
};
constexpr int HINT_BASE = 0x6000, HINTS = int(sizeof HINT_TEXTS / sizeof HINT_TEXTS[0]);
const C_str &TextEntry(int id)
{
    auto it = g_textTable.find(id);
    if (it == g_textTable.end()) {
        char text[16];
        std::snprintf(text, sizeof text, "T%x", id);
        const char *s = text;
        if (id >= HINT_BASE && id < HINT_BASE + HINTS)
            s = HINT_TEXTS[id - HINT_BASE];
        it = g_textTable.emplace(id, s ? C_str(s) : C_str()).first;
    }
    return it->second;
}
C_str TextOf(int id)
{
    Log("texts Get %x", id);
    return TextEntry(id);
}
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    C_str s = TextOf(id);
    *static_cast<void **>(out) = s.m_rep;
    s.m_rep = nullptr;
    return out;
}

// ---- the key bindings: the first of three tags found replaced (its first place); none: the text as it was, false ----
alignas(16) uint8_t g_profileBlock[0x2000];
bool ReplaceTags(C_str *out, const char *text)
{
    static const char *const TAGS[][2] = {{"<FIRE>", "[LMB]"}, {"<JUMP>", "[SPACE]"}, {"<USE>", "[E]"}};
    std::string s = text;
    bool done = false;
    for (const auto &t : TAGS) {
        const size_t at = s.find(t[0]);
        if (at != std::string::npos) {
            s.replace(at, std::strlen(t[0]), t[1]);
            done = true;
            break;
        }
    }
    Log("keys ReplaceTags '%s' -> '%s' %d", text, s.c_str(), done);
    *out = C_str(s.c_str());
    return done;
}
bool __attribute__((thiscall)) O_ReplaceTags(void *, C_str *out, const char *text) { return ReplaceTags(out, text); }

// ---- the world ----
constexpr int PLAYERS = 24, OBJECTIVES = 8;
alignas(16) uint8_t g_players[PLAYERS][0xC4];
void *g_playersVector[4];   // the original's {allocator, begin, end, end}
alignas(16) uint8_t g_humans[PLAYERS][0x800], g_holders[PLAYERS][0x20], g_items[PLAYERS][0x100], g_defs[PLAYERS][0x100];
struct S_Inventory {
    void **vt;
};
void *g_inventoryVt[32];
S_Inventory g_inventories[PLAYERS];
alignas(16) uint8_t g_objectives[OBJECTIVES][0x40];
alignas(16) uint8_t g_missions[2][0x100];   // the original's (vector at +0x30 {allocator, begin, end}), the port's
void *g_origObjectives[OBJECTIVES];
alignas(16) uint8_t g_game[0x400], g_campaignBlock[0x10];
alignas(16) static uint8_t g_settingsBlock[0x400];
const char *const NAMES[] = {"", "Bob", "bob", "alice", "Alice", "Zed", "zed ", "Ann", "ann", "\xdcnter", "x%sy", "Bob", "a", "B"};
const char *const SERVERS[] = {"", "Server", "My %d server", "A long server name with spaces"};
const char *const MAPS[] = {"", "Africa", "Normandy 2", nullptr};
struct S_PlayerW {
    int name;
    bool human, spectating;
    int health;
    int uniform;   // -1 none (null), else the item's definition's team
    int stats[6];
    int team;
};
struct S_InfoWorld {
    int count, slot;
    bool big;   // more than 16 players: names and scores distinct (the original's sort is not stable beyond 16)
    uint32_t role;
    int gameType, roundLimit, pointLimit, roundState, roundTime, server;
    float scores[3];
    int wins[3];
    S_PlayerW players[PLAYERS];
    bool campaignMission, gameMission;
    int objectiveCount, map;
    int id[OBJECTIVES], status[OBJECTIVES], team[OBJECTIVES], allied[OBJECTIVES], axis[OBJECTIVES];
};
S_InfoWorld g_w, g_wStart;
int PlayerIndex(const void *p)
{
    for (int i = 0; i < PLAYERS; ++i)
        if (p == g_players[i])
            return i;
    return -1;
}
int HumanIndex(const void *p)
{
    for (int i = 0; i < PLAYERS; ++i)
        if (p == g_humans[i])
            return i;
    return -1;
}
void LockPlayers() { Log("net LockPlayers"); }
void UnlockPlayers() { Log("net UnlockPlayers"); }
void *__attribute__((thiscall)) O_LockPlayers(void *)
{
    LockPlayers();
    return g_playersVector;
}
void __attribute__((thiscall)) O_UnlockPlayers(void *) { UnlockPlayers(); }
// (Not logged: a read the original makes once or twice per objective, the port once or twice in other places.)
int LocalTeam() { return g_w.players[g_w.slot].team; }
int __attribute__((thiscall)) O_LocalTeam(void *) { return LocalTeam(); }
bool Spectating(const void *player)
{
    Log("p%d IsSpectating", PlayerIndex(player));
    return g_w.players[PlayerIndex(player)].spectating;
}
bool __attribute__((thiscall)) O_Spectating(void *player) { return Spectating(player); }
void *Uniform(int h)
{
    void *item = g_w.players[h].uniform < 0 ? nullptr : g_items[h];
    Log("h%d GetUniformItem -> %s", h, Name(item).c_str());
    return item;
}
void *__attribute__((thiscall)) O_Uniform(void *inventory) { return Uniform(int(static_cast<S_Inventory *>(inventory) - g_inventories)); }
void *CurrentMission(int side)
{
    void *m = g_w.campaignMission ? g_missions[side] : nullptr;
    Log("campaign GetCurrentMission -> %s", m ? "mission" : "null");
    return m;
}
void *__attribute__((thiscall)) O_CurrentMission(void *) { return CurrentMission(0); }
void *__cdecl O_GetGame() { return g_game; }

// ---- rw_data's files (in memory) ----
std::map<std::string, std::string> g_files;
struct S_Open {
    std::string name;
    size_t at = 0;
};
std::map<int, S_Open> g_open;
int g_nextHandle = 1;

std::string PartName(const void *p)
{
    if (PlayerIndex(p) >= 0)
        return "p" + std::to_string(PlayerIndex(p));
    if (HumanIndex(p) >= 0)
        return "h" + std::to_string(HumanIndex(p));
    for (int i = 0; i < PLAYERS; ++i)
        if (p == g_items[i])
            return "i" + std::to_string(i);
    if (p == g_sceneBlock)
        return "scene";
    for (const void *b : g_boxes)
        if (b && p == b)
            return BoxName(p);
    return "";
}

}  // namespace H

// ---- rw_data ----
int RW_DATA_CALL dtaOpen(const char *name, bool)
{
    if (!H::g_files.count(name))
        return DTA_INVALID_HANDLE;
    H::g_open[H::g_nextHandle] = H::S_Open{name, 0};
    return H::g_nextHandle++;
}
int RW_DATA_CALL dtaOpenWrite(const char *name, int)
{
    H::g_files[name].clear();
    H::g_open[H::g_nextHandle] = H::S_Open{name, 0};
    return H::g_nextHandle++;
}
bool RW_DATA_CALL dtaClose(int handle) { return H::g_open.erase(handle) != 0; }
int RW_DATA_CALL dtaSeek(int handle, int offset, int origin)
{
    H::S_Open &o = H::g_open[handle];
    const std::string &f = H::g_files[o.name];
    o.at = origin == 0 ? offset : origin == 1 ? o.at + offset : f.size() + offset;
    return static_cast<int>(o.at);
}
int RW_DATA_CALL dtaRead(int handle, void *data, int size)
{
    H::S_Open &o = H::g_open[handle];
    const std::string &f = H::g_files[o.name];
    const int got = std::max(0, std::min(static_cast<int>(f.size()) - static_cast<int>(o.at), size));
    std::memcpy(data, f.data() + o.at, got);
    o.at += got;
    return got;
}
int RW_DATA_CALL dtaWrite(int handle, const void *data, int size)
{
    H::S_Open &o = H::g_open[handle];
    std::string &f = H::g_files[o.name];
    if (o.at + size > f.size())
        f.resize(o.at + size);
    std::memcpy(&f[o.at], data, size);
    o.at += size;
    return size;
}

// ---- the port's side ----
static C_Campaign *g_campaignPointer = reinterpret_cast<C_Campaign *>(H::g_campaignBlock);
C_Campaign *&g_campaign = g_campaignPointer;
static S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(H::g_settingsBlock);
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_game); }
C_str C_GameTexts::Get(int id) const { return H::TextOf(id); }
bool C_KeyBindings::ReplaceTags(C_str *out, const char *text) const { return H::ReplaceTags(out, text); }
int CreateTextStyle(const S_TextStyleDesc &desc) { return H::CreateStyle(&desc); }
void ReleaseTextStyle(int style) { H::ReleaseStyle(style); }
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *model, const char *name, uint32_t, void *, void *, int)
{
    H::CacheOpen(model, name);
    return H::g_openResult;
}
S_NetPlayerState *C_NetGame::LockPlayers()
{
    H::LockPlayers();
    return reinterpret_cast<S_NetPlayerState *>(H::g_players);
}
void C_NetGame::UnlockPlayers() { H::UnlockPlayers(); }
int C_NetGame::GetLocalTeam() { return H::LocalTeam(); }
bool S_NetPlayerState::IsSpectating() const { return H::Spectating(this); }
C_Item *C_Human::GetUniformItem() const { return static_cast<C_Item *>(H::Uniform(H::HumanIndex(this))); }
C_MissionDef *C_Campaign::GetCurrentMission() { return static_cast<C_MissionDef *>(H::CurrentMission(1)); }
S_MissionObjective *C_MissionDef::GetObjective(int index) const
{
    return index >= 0 && index < GetObjectiveCount() ? m_objectives[index] : nullptr;
}
static_assert(sizeof(S_NetPlayerState) == 0xC4 && offsetof(S_NetPlayerState, team) == 0x64 && offsetof(S_NetPlayerState, human) == 0x3C &&
                  offsetof(S_NetPlayerState, stats) == 0x40,
              "the players as the original's");
static_assert(sizeof(S_TextStyleDesc) == 0x34, "a style's words");
static_assert(sizeof(C_NetGame) <= 0x8000, "the net game's storage (interface_globals.cpp)");
static_assert(sizeof(C_Human) <= 0x800, "the human blocks");
constexpr uint32_t P_HEALTH = offsetof(C_Human, m_stats) + 4 * STAT_HEALTH, P_DEF = offsetof(C_Item, m_def), P_TEAM = offsetof(C_ItemDef, m_team),
                   P_ID = offsetof(S_MpObjective, m_id), P_STATUS = offsetof(S_MpObjective, status), P_OTEAM = offsetof(S_MpObjective, team),
                   P_ALLIED = offsetof(S_MpObjective, alliedText), P_AXIS = offsetof(S_MpObjective, axisText),
                   P_OBJECTIVES = offsetof(C_MissionDef, m_objectives), P_MAP = offsetof(C_MpMissionDef, m_mapName),
                   P_ROUNDSTATE = offsetof(C_game, m_roundState), P_ROUNDTIME = offsetof(C_game, m_roundTime), P_MISSION = offsetof(C_game, m_mission),
                   P_GAMETYPE = offsetof(S_MultiplayerSettings, gameType), P_ROUNDLIMIT = offsetof(S_MultiplayerSettings, roundLimit),
                   P_POINTLIMIT = offsetof(S_MultiplayerSettings, pointLimit);

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
template <class T> void Put(uint8_t *block, uint32_t at, T value) { std::memcpy(block + at, &value, sizeof value); }
// A g++ std::vector's three pointers set in place (the objects only named: raw storage).
template <class T> void SetVector(std::vector<T> &v, T *begin, T *end)
{
    T *p[3] = {begin, end, end};
    std::memcpy(static_cast<void *>(&v), p, sizeof p);
}

// ---- the world drawn ----
void RandomPlayers()
{
    g_w.big = P(0.1);
    g_w.count = g_w.big ? I(17, PLAYERS) : P(0.1) ? Pick({0, 1, 16}) : I(0, 16);
    g_w.slot = I(0, std::max(0, g_w.count - 1));
    std::vector<int> scores;
    for (int i = 0; i < PLAYERS; ++i)
        scores.push_back(i * 7 - 40);
    std::shuffle(scores.begin(), scores.end(), g_rng);
    for (int i = 0; i < PLAYERS; ++i) {
        S_PlayerW &p = g_w.players[i];
        p.name = g_w.big ? 100 + i : P(0.15) ? 0 : I(1, int(sizeof NAMES / sizeof NAMES[0]) - 1);
        p.human = P(0.75);
        p.spectating = P(0.2);
        p.health = P(0.7) ? I(1, 100) : Pick({0, -5, 1});
        p.uniform = P(0.2) ? -1 : P(0.8) ? I(0, 1) : Pick({2, 3, -2});
        for (int &s : p.stats)
            s = P(0.3) ? Pick({0, -1, 1, 9, 100, INT_MIN, INT_MAX}) : I(0, 50);
        p.stats[3] = g_w.big ? scores[i] : P(0.4) ? I(0, 3) : p.stats[3];   // (equal scores often)
        p.stats[5] = P(0.3) ? Pick({-3, 0, 7, 8, 9, 100}) : I(0, 7);
        p.team = P(0.85) ? I(0, 1) : Pick({2, -1, 3});
    }
}
void RandomGame()
{
    g_w.roundState = P(0.3) ? 3 : I(0, 4);
    g_w.roundTime = P(0.2) ? Pick({0, -1000, 59999, 60000, 3599999, 3600000, INT_MAX, 359999999}) : I(0, 7200000);
    for (float &s : g_w.scores)
        s = P(0.2) ? Pick({0.0f, -1.0f, 0.5f, 1e10f, -1e10f, NAN, INFINITY, 2147483648.0f, 99.99f}) : float(I(0, 100));
    for (int &w : g_w.wins)
        w = P(0.2) ? Pick({0, -1, INT_MAX}) : I(0, 10);
    g_w.roundLimit = P(0.3) ? Pick({0, -1, 1, 35791, 35792}) : I(0, 60);
    g_w.pointLimit = P(0.3) ? Pick({0, -1, 1, INT_MAX}) : I(0, 100);
    g_w.server = I(0, int(sizeof SERVERS / sizeof SERVERS[0]) - 1);
}
void RandomSettings()
{
    g_w.role = P(0.3) ? 7u : Pick({0u, 1u, 3u, 6u, 15u});
    g_w.gameType = P(0.9) ? I(0, 3) : Pick({-1, 4, 7});
}
void RandomMission()
{
    g_w.campaignMission = P(0.85);
    g_w.gameMission = P(0.85);
    g_w.map = I(0, int(sizeof MAPS / sizeof MAPS[0]) - 1);
    g_w.objectiveCount = P(0.1) ? 0 : I(1, OBJECTIVES);
    for (int i = 0; i < OBJECTIVES; ++i) {
        g_w.id[i] = I(0x100, 0x2000);
        g_w.status[i] = P(0.9) ? I(0, 4) : Pick({5, -1});
        g_w.team[i] = P(0.85) ? I(0, 1) : Pick({2, -1});
        g_w.allied[i] = I(0x3000, 0x3100);
        g_w.axis[i] = I(0x3100, 0x3200);
    }
}
// The world to both sides' memory (side: whose missions the game and the campaign hold).
void SyncWorld(int side)
{
    for (int i = 0; i < PLAYERS; ++i) {
        const S_PlayerW &w = g_w.players[i];
        uint8_t *p = g_players[i];
        std::memset(p, 0, 0xC4);
        const char *name = w.name >= 100 ? nullptr : NAMES[w.name];
        if (name)
            std::strcpy(reinterpret_cast<char *>(p + 8), name);
        else
            std::snprintf(reinterpret_cast<char *>(p + 8), 0x34, "P%02d", w.name - 100);
        Put<void *>(p, 0x3C, w.human ? g_humans[i] : nullptr);
        for (int s = 0; s < 6; ++s)
            Put(p, 0x40 + s * 4, w.stats[s]);
        Put(p, 0x64, w.team);
        uint8_t *h = g_humans[i];
        std::memset(h, 0, sizeof g_humans[i]);
        Both(h, 0xEC, P_HEALTH, w.health);
        Put<void *>(h, 0x74, g_holders[i]);   // (the original's path to the uniform: +0x74, +0x10, the slot 0x40)
        Put<void *>(g_holders[i], 0x10, &g_inventories[i]);
        Both<void *>(g_items[i], 4, P_DEF, g_defs[i]);
        Both(g_defs[i], 0x44, P_TEAM, w.uniform);
    }
    g_playersVector[1] = g_players;
    g_playersVector[2] = g_players + g_w.count;
    g_playersVector[3] = g_players + g_w.count;
    auto *players = reinterpret_cast<S_NetPlayerState *>(g_players);
    SetVector(g_netGame.m_players, players, players + g_w.count);
    for (int t = 0; t < 3; ++t) {
        OriginalAt<float *>(0x0097d4bc)[t] = g_w.scores[t];
        g_netGame.m_teamScores[t] = g_w.scores[t];
        OriginalAt<int *>(0x0097d4c8)[t] = g_w.wins[t];
        g_netGame.m_teamWins[t] = g_w.wins[t];
    }
    std::memset(OriginalAt<char *>(0x00981e70), 0, 0xA0);
    std::strcpy(OriginalAt<char *>(0x00981e70), SERVERS[g_w.server]);
    std::memset(g_netGame.m_serverEntry, 0, sizeof g_netGame.m_serverEntry);
    std::strcpy(reinterpret_cast<char *>(g_netGame.m_serverEntry), SERVERS[g_w.server]);
    g_localPlayerSlot = g_w.slot;
    *OriginalAt<int *>(0x00982104) = g_w.slot;
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    Both(g_settingsBlock, 0x224, P_GAMETYPE, g_w.gameType);
    Both(g_settingsBlock, 0x230, P_ROUNDLIMIT, g_w.roundLimit);
    Both(g_settingsBlock, 0x22C, P_POINTLIMIT, g_w.pointLimit);
    Both(g_game, 0x88, P_ROUNDSTATE, g_w.roundState);
    Both(g_game, 0x30, P_ROUNDTIME, g_w.roundTime);
    void *mission = g_w.gameMission ? g_missions[side] : nullptr;
    Put(g_game, side ? P_MISSION : 0xA8, mission);
    for (int i = 0; i < OBJECTIVES; ++i) {
        uint8_t *o = g_objectives[i];
        std::memset(o, 0, sizeof g_objectives[i]);
        Both(o, 8, P_ID, g_w.id[i]);
        Both(o, 0x10, P_STATUS, g_w.status[i]);
        Both(o, 0x18, P_OTEAM, g_w.team[i]);
        Both(o, 0x1C, P_ALLIED, g_w.allied[i]);
        Both(o, 0x20, P_AXIS, g_w.axis[i]);
        g_origObjectives[i] = o;
    }
    // The missions: the original's vector {allocator, begin, end} at +0x30 (null begin: none), its map's name at +0x6C.
    std::memset(g_missions[0], 0, sizeof g_missions[0]);
    Put<void *>(g_missions[0], 0x34, g_w.objectiveCount ? g_origObjectives : nullptr);
    Put<void *>(g_missions[0], 0x38, g_w.objectiveCount ? g_origObjectives + g_w.objectiveCount : nullptr);
    std::memset(g_missions[1], 0, sizeof g_missions[1]);
    auto **objectives = reinterpret_cast<S_MissionObjective **>(g_origObjectives);
    SetVector(*reinterpret_cast<std::vector<S_MissionObjective *> *>(g_missions[1] + P_OBJECTIVES), objectives, objectives + g_w.objectiveCount);
    static C_str maps[sizeof MAPS / sizeof MAPS[0]];
    static bool made = false;
    if (!made) {
        for (size_t m = 0; m < sizeof MAPS / sizeof MAPS[0]; ++m)
            maps[m] = MAPS[m] ? C_str(MAPS[m]) : C_str();
        made = true;
    }
    Put<void *>(g_missions[0], 0x6C, maps[g_w.map].m_rep);   // (the reps kept by the table: never released here)
    Put<void *>(g_missions[1], P_MAP, maps[g_w.map].m_rep);
}
void RandomWorldFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}

// ---- a scenario ----
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0;
};
enum { W_PLAYERS = 100, W_GAME, W_SETTINGS, W_MISSION, W_FRAME, W_SPECTATE };
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case W_PLAYERS:
        RandomWorldFrom(op.a, &RandomPlayers);
        break;
    case W_GAME:
        RandomWorldFrom(op.a, &RandomGame);
        break;
    case W_SETTINGS:
        RandomWorldFrom(op.a, &RandomSettings);
        break;
    case W_MISSION:
        RandomWorldFrom(op.a, &RandomMission);
        break;
    case W_FRAME:   // (the interface's other screens switch the frame)
        FrameAt(F_INFO)->m_frameFlags = op.a ? FrameAt(F_INFO)->m_frameFlags | 1u : FrameAt(F_INFO)->m_frameFlags & ~1u;
        break;
    case W_SPECTATE:
        g_w.players[op.a].spectating = op.b != 0;
        g_w.players[op.a].health = op.c;
        break;
    }
}
struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
uint32_t g_seed;
std::vector<uint32_t> g_startWords;
uint32_t g_renderStart[8];
void StartScenario()
{
    g_seed = U32();
    g_startWords.resize(0x50);
    for (uint32_t &w : g_startWords)
        w = P(0.3) ? Pick({0u, 0xFFFFFFFFu, 1u, 0x3F000000u}) : U32();
    g_startWords[0] = P(0.6) ? uint32_t(I(-1, 6)) : U32();   // (the kind: Init keeps it for other game types)
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    std::fill(std::begin(g_missing), std::end(g_missing), false);
    g_missing[F_HEADER] = P(0.3);
    g_missing[F_TEXT] = P(0.3);
    std::memset(g_sceneBlock, 0, sizeof g_sceneBlock);
    *reinterpret_cast<void ***>(g_sceneBlock) = g_sceneVt;
    Put<void *>(g_sceneBlock, 0x1F8, FrameAt(F_SECTOR));
    g_nextStyleStart = P(0.5) ? I(0, 20) : I(180, 300);
    for (uint32_t &r : g_renderStart)
        r = P(0.5) ? Pick({0u, 1u, 4u}) : U32();
    for (uint32_t &d : g_statesStart)
        d = P(0.5) ? Pick({0u, 1u}) : U32();
    RandomPlayers();
    RandomGame();
    RandomSettings();
    RandomMission();
}
void SaveStart()
{
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    g_wStart = g_w;
}
void StartSide()
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    std::memcpy(g_states, g_statesStart, sizeof g_states);
    std::memcpy(g_renderStates, g_renderStart, sizeof g_renderStates);
    g_w = g_wStart;
    g_nextStyle = g_nextStyleStart;
    g_log.clear();
    g_boxes.clear();
    g_files.clear();
    g_open.clear();
    std::srand(g_seed);
}

using T_Init = bool(__attribute__((thiscall)) *)(void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Chunk = bool(__attribute__((thiscall)) *)(void *, C_chunk *);
using T_Insert = void(__attribute__((thiscall)) *)(void *, int *, int, const int *);
std::string Hex(const std::string &bytes)
{
    std::string s;
    char text[4];
    for (unsigned char c : bytes) {
        std::snprintf(text, sizeof text, "%02x", c);
        s += text;
    }
    return s;
}
// A saved game's hints for LoadHints: an outer chunk (0x77) of chunks: 0x1F5 with 0 to 6 ids, others between (sizes
// of any length, empty too); `whole`: every 0x1F5 a whole number of ids (the original loops forever on a part).
std::string HintsFile(int seed, bool whole)
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    std::string inner;
    for (int n = I(0, 4); n > 0; --n) {
        const uint16_t id = P(0.6) ? 0x1F5 : Pick({uint16_t(0x1F4), uint16_t(0x1F6), uint16_t(0x0001), uint16_t(0xF501)});
        std::string data;
        if (id == 0x1F5) {
            for (int k = P(0.15) ? 0 : I(1, 6); k > 0; --k) {
                const int hint = P(0.7) ? HINT_BASE + I(0, HINTS - 1) : int(U32());
                data.append(reinterpret_cast<const char *>(&hint), 4);
            }
            if (!whole)
                data.append(size_t(I(1, 3)), char(I(0, 255)));
        } else {
            for (int k = I(0, 9); k > 0; --k)
                data.push_back(char(I(0, 255)));
        }
        const uint32_t size = uint32_t(data.size() + 6);
        inner.append(reinterpret_cast<const char *>(&id), 2);
        inner.append(reinterpret_cast<const char *>(&size), 4);
        inner += data;
    }
    g_rng = saved;
    const uint16_t outer = 0x77;
    const uint32_t size = uint32_t(inner.size() + 6);
    return std::string(reinterpret_cast<const char *>(&outer), 2) + std::string(reinterpret_cast<const char *>(&size), 4) + inner;
}
std::vector<int> OrigHints(const uint8_t *orig)
{
    std::vector<int> v;
    for (const int *p = At<const int *>(orig, 4); p && p < At<const int *>(orig, 8); ++p)
        v.push_back(*p);
    return v;
}
std::string Snap(int side, C_InfoScreen *port, const uint8_t *orig)
{
    char text[120];
    std::snprintf(text, sizeof text, "info kind %d %s %s refresh %d styles", side ? port->m_kind : At<int>(orig, 0x10),
                  Name(side ? port->m_frame : At<void *>(orig, 0x14)).c_str(), Name(side ? port->m_text : At<void *>(orig, 0x18)).c_str(),
                  side ? port->m_refreshTime : At<int>(orig, 0x140));
    std::string s = text;
    for (int i = 0; i < C_InfoScreen::STYLE_COUNT; ++i)
        s += " " + std::to_string(side ? port->m_styles[i] : At<int>(orig, 0x1C + i * 4));
    s += " hints";
    for (int h : side ? port->m_hints : OrigHints(orig))
        s += " " + std::to_string(h);
    return s;
}
void Run(int side, C_InfoScreen *port, uint8_t *orig, const S_Op &op)
{
    switch (op.kind) {
    case 0:
        Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(g_sceneBlock)) : OriginalAt<T_Init>(0x00496450)(orig, g_sceneBlock));
        break;
    case 1:
        side ? port->Done() : OriginalAt<T_Void>(0x00496dc0)(orig);
        break;
    case 2:
        side ? port->Update(op.a) : OriginalAt<T_Int>(0x00499210)(orig, op.a);
        break;
    case 3:
        side ? port->Render() : OriginalAt<T_Void>(0x00496e00)(orig);
        break;
    case 4:
        side ? port->Show(op.a != 0) : OriginalAt<T_Bool>(0x00496e40)(orig, op.a != 0);
        break;
    case 5:   // (the original's IsShown is its frame's flag read inline)
        Log("IsShown %d", side ? port->IsShown() : (At<I3D_frame *>(orig, 0x14)->m_frameFlags & 1) != 0);
        break;
    case 6:   // AddHint (the original's inline: its vector's insert 0x00420ec0)
        if (side)
            port->AddHint(op.a);
        else
            OriginalAt<T_Insert>(0x00420ec0)(orig, At<int *>(orig, 8), 1, &op.a);
        break;
    case 7:
        side ? port->ClearHints() : OriginalAt<T_Void>(0x00498ec0)(orig);
        break;
    case 8: {   // SaveHints into a chunk of its own
        C_chunk chunk;
        chunk.Open("save", true);
        chunk.Begin(0x77);
        const bool ok = side ? port->SaveHints(chunk) : OriginalAt<T_Chunk>(0x00498ef0)(orig, &chunk);
        Log("depth %d", chunk.m_depth);
        chunk.Ascend();
        chunk.Close();
        Log("SaveHints -> %d %s", ok, Hex(g_files["save"]).c_str());
        break;
    }
    case 9: {   // LoadHints from the outer chunk's inside (then where the chunk is)
        g_files["load"] = HintsFile(op.a, true);
        C_chunk chunk;
        chunk.Open("load", false);
        uint16_t id = 0;
        if (op.b)
            chunk.Descend(id);
        const bool ok = side ? port->LoadHints(chunk) : OriginalAt<T_Chunk>(0x00499020)(orig, &chunk);
        Log("LoadHints -> %d at %d depth %d", ok, chunk.GetPosition(), chunk.m_depth);
        chunk.Close();
        break;
    }
    }
}
void RunScenario(const std::vector<S_Op> &ops)
{
    for (int side = 0; side < 2; ++side) {
        StartSide();
        C_InfoScreen *port = nullptr;
        uint8_t *orig = nullptr;
        if (side) {
            port = new C_InfoScreen;
            port->m_kind = int(g_startWords[0]);
            port->m_frame = reinterpret_cast<I3D_frame *>(uintptr_t(g_startWords[1]));
            port->m_text = reinterpret_cast<C_TextBox *>(uintptr_t(g_startWords[2]));
            for (int i = 0; i < C_InfoScreen::STYLE_COUNT; ++i)
                port->m_styles[i] = int(g_startWords[3 + i % 0x40]);
            port->m_refreshTime = int(g_startWords[0x4F]);
        } else {
            orig = static_cast<uint8_t *>(std::calloc(1, 0x144 + 0x40));
            Put(orig, 0x10, g_startWords[0]);
            Put(orig, 0x14, g_startWords[1]);
            Put(orig, 0x18, g_startWords[2]);
            for (int i = 0; i < C_InfoScreen::STYLE_COUNT; ++i)
                Put(orig, 0x1C + i * 4, g_startWords[3 + i % 0x40]);
            Put(orig, 0x140, g_startWords[0x4F]);
        }
        for (const S_Op &op : ops) {
            Log("-- %d %d %d %d", op.kind, op.a, op.b, op.c);
            if (op.kind >= 100)
                ApplyWorld(op);
            SyncWorld(side);
            if (op.kind < 100)
                Run(side, port, orig, op);
            g_log.push_back(Snap(side, port, orig));
        }
        if (side) {
            delete port;
        } else {
            std::free(At<void *>(orig, 4));
            std::free(orig);
        }
        // (The original's AddTeamPlayers fetches text 0xA40 and does not use it: that fetch left out.)
        if (!side)
            g_log.erase(std::remove(g_log.begin(), g_log.end(), std::string("texts Get a40")), g_log.end());
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
    }
    for (int i = 0; i < FRAMES; ++i)
        for (S_Run &r : g_runs)
            std::memcpy(r.arena.frames[i] + 0xDC, g_arenaStart.frames[i] + 0xDC, 4);
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}
int Delta() { return P(0.6) ? Pick({0, 1, 16, 100, 499, 500, 501, 1000}) : I(-50, 1500); }
void FillOp(S_Op &op)
{
    switch (op.kind) {
    case 2:
        op.a = Delta();
        break;
    case 4:
        op.a = P(0.7);
        break;
    case 6:
        op.a = P(0.8) ? HINT_BASE + I(0, HINTS - 1) : I(-1, 0x7000);
        break;
    case 9:
        op.a = int(U32() & 0x7FFFFFFF);
        op.b = P(0.9);
        break;
    case W_PLAYERS:
    case W_GAME:
    case W_SETTINGS:
    case W_MISSION:
        op.a = int(U32() & 0x7FFFFFFF);
        break;
    case W_FRAME:
        op.a = P(0.5);
        break;
    case W_SPECTATE:
        op.a = I(0, PLAYERS - 1);
        op.b = P(0.5);
        op.c = P(0.5) ? I(1, 100) : Pick({0, -1});
        break;
    }
}

// ---- the screen ----
void TestInfo()
{
    StartScenario();
    std::vector<S_Op> ops;
    // (Hints first now and then: Init clears them; Show fills with them.)
    for (int n = P(0.3) ? I(1, 2) : 0; n > 0; --n) {
        S_Op hint{6};
        FillOp(hint);
        ops.push_back(hint);
    }
    ops.push_back(S_Op{0});
    static const int KINDS[] = {1, 2, 2, 2, 3, 4, 4, 4, 5, 6, 6, 6, 7, 8, 9, W_PLAYERS, W_GAME, W_SETTINGS, W_MISSION, W_FRAME, W_SPECTATE};
    bool alive = true;
    for (int n = I(0, 20); n > 0 && alive; --n) {
        S_Op op{Pick(KINDS)};
        FillOp(op);
        alive = op.kind != 1;
        ops.push_back(op);
    }
    if (alive && P(0.5))
        ops.push_back(S_Op{1});
    SaveStart();
    RunScenario(ops);
}

// ---- the port's fixes, alone (the original crashes or loops forever) ----
// Init without the model (OpenInto failing) or without its "game_info": false, no frame, the model released, the
// driver's states put back; LoadHints with a part of an id at a chunk's end: the whole ids, then on.
void TestFixes()
{
    StartScenario();
    SaveStart();
    for (int k = 0; k < 2; ++k) {
        StartSide();
        SyncWorld(1);
        g_openResult = k == 0 ? (P(0.5) ? 1 : HRESULT(0x80004005)) : 0;
        g_missing[F_INFO] = k == 1;
        C_InfoScreen screen;
        screen.m_frame = FrameAt(F_HEADER);
        const uint32_t states[2] = {g_states[0x15], g_states[0x19]};
        CHECK(!screen.Init(reinterpret_cast<I3D_scene *>(g_sceneBlock)), "Init without %s: true", k ? "game_info" : "the model");
        CHECK(screen.m_frame == nullptr && screen.m_text == nullptr, "Init without %s: a frame or a box", k ? "game_info" : "the model");
        CHECK(std::find(g_log.begin(), g_log.end(), "f0 Release") != g_log.end(), "Init without %s: the model kept", k ? "game_info" : "the model");
        CHECK(g_states[0x15] == uint32_t(states[0] != 0) && g_states[0x19] == uint32_t(states[1] != 0),
              "Init without %s: the driver's states", k ? "game_info" : "the model");
        g_openResult = 0;
        g_missing[F_INFO] = false;
    }
    // (A part of an id: the whole ones read, the part skipped, the chunks after it read.)
    StartSide();
    const int seed = int(U32() & 0x7FFFFFFF);
    g_files["load"] = HintsFile(seed, false);
    std::vector<int> expected;
    {
        const std::string &f = g_files["load"];
        size_t at = 6;
        while (at + 6 <= f.size()) {
            uint16_t id;
            uint32_t size;
            std::memcpy(&id, f.data() + at, 2);
            std::memcpy(&size, f.data() + at + 2, 4);
            if (id == 0x1F5)
                for (size_t k = at + 6; k + 4 <= at + size; k += 4) {
                    int hint;
                    std::memcpy(&hint, f.data() + k, 4);
                    expected.push_back(hint);
                }
            at += size;
        }
    }
    C_InfoScreen screen;
    screen.m_hints = {1, 2, 3};
    C_chunk chunk;
    chunk.Open("load", false);
    uint16_t id;
    chunk.Descend(id);
    CHECK(screen.LoadHints(chunk), "LoadHints with parts: false");
    CHECK(screen.m_hints == expected, "LoadHints with parts: %u hints, %u expected", unsigned(screen.m_hints.size()), unsigned(expected.size()));
    CHECK(chunk.GetPosition() == int(g_files["load"].size()) && chunk.m_depth == 1, "LoadHints with parts: at %d depth %d", chunk.GetPosition(),
          chunk.m_depth);
    chunk.Close();
}

void SetUp()
{
    g_partName = PartName;
    g_findChild = FindChild;
    SetUpGfx();
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(D_CreateFrame);
    g_driverVt[0x18 / 4] = reinterpret_cast<void *>(D_SetState2);
    g_driverVt[0x1C / 4] = reinterpret_cast<void *>(D_GetState2);
    g_frameVt[0x34 / 4] = reinterpret_cast<void *>(F_SetName);
    g_sceneVt[0x80 / 4] = reinterpret_cast<void *>(SC_AddFrame);
    g_graphVt[0x74 / 4] = reinterpret_cast<void *>(G_SetRenderState);
    g_graphVt[0x78 / 4] = reinterpret_cast<void *>(G_GetRenderState);
    for (void *&v : g_cacheVt)
        v = reinterpret_cast<void *>(O_CacheOther);
    g_cacheVt[0x30 / 4] = reinterpret_cast<void *>(O_CacheDuplicate);
    *OriginalAt<void **>(0x008aeda0) = g_cacheVt;
    for (void *&v : g_inventoryVt)
        v = reinterpret_cast<void *>(O_CacheOther);
    g_inventoryVt[0x40 / 4] = reinterpret_cast<void *>(O_Uniform);
    for (S_Inventory &i : g_inventories)
        i.vt = g_inventoryVt;
    *OriginalAt<void **>(0x008aea30) = g_mpSettings;   // (one settings block: each side reads its own offsets)
    *OriginalAt<void **>(0x008aea10) = g_campaign;
    *OriginalAt<void **>(0x008aea2c) = g_profileBlock;
    g_gameProfile.m_player = reinterpret_cast<C_PlayerProfile *>(g_profileBlock);
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x00409660, reinterpret_cast<const void *>(O_CacheFind)},
        {0x00643c00, reinterpret_cast<const void *>(O_CreateStyle)},
        {0x00643bf0, reinterpret_cast<const void *>(O_ReleaseStyle)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
        {0x006ac470, reinterpret_cast<const void *>(O_ReplaceTags)},
        {0x006b94b0, reinterpret_cast<const void *>(O_CurrentMission)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x0072f040, reinterpret_cast<const void *>(O_LockPlayers)},
        {0x0072f100, reinterpret_cast<const void *>(O_UnlockPlayers)},
        {0x00730880, reinterpret_cast<const void *>(O_LocalTeam)},
        {0x00725e90, reinterpret_cast<const void *>(O_Spectating)},
        {0x007e5392, reinterpret_cast<const void *>(dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("info_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_INFO_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_INFO_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_INFO_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Info", TestInfo}, {"InfoFixes", TestFixes}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("info_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
