// Human part 67: the interface's overlays (game/interface_overlays.cpp: the injury flash, the big message, the objectives'
// icons, the voting panel, the photo flash, the carnage counter, the pause and countdown texts, the score table, the
// explosive timers) against the original's (0x004ad250..0x004ad9f0, 0x004aed00..0x004b0220).
// MinGW; build_overlays.bat; runs overlays_diff.exe ("overlays_diff: N checks, 0 failures"). interface_overlays.cpp is
// compiled into this file; the kit (interface_kit.inc, interface_gfx.inc) fakes the frames, the scene, the graph, the
// text boxes and the graphics. A case per overlay: scenarios of its operations (Init, Done, Update, Render, Start, Show
// ...; between them the world changing) run from one start on each side (the same random words first: Init must set what
// the overlay reads), every call out logged by the same fakes, the overlay's fields snapped after each operation, the
// frames and the graphics compared at the end.
// Faked on both sides with the same loggers: the video streams (the graph's CreateStream; Open's answers, Release, Close,
// Play, Stop, SetPosition, GetVideoTexture, UpdateVideo), the CRT's rand (seeded the same), the sound module's Play (the
// original's GetSoundModule 0x004336c0 and its slot 0x54; the port's C_SoundModule::Play; every field of the request
// logged), the effects' SetFadeLevel (GetGEffects 0x00486510: its slot 0x88, the port's as g++ placed it), the scene's
// camera (+0x15C), the game (its scene +0x18: the original's pointer 0x008ae8f4), the texts, the actors module's carnage
// count (the original's 0x0089b500).
// HD2_OVERLAYS_ROUNDS: scenarios per case (default 20,000; HD2_OVERLAYS_QUICK: 5,000); HD2_OVERLAYS_CASE: one case only;
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
#include "game/interface_overlays.cpp"
#include "game/game_texts.h"
#include "game/geffects.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"
#undef private
#undef protected

#include "original_exe.h"

#define INTERFACE_FRAMES 72
#include "interface_kit.inc"
#include "interface_gfx.inc"

namespace H {

// ---- the scene's frames ----
// f0 "injury", f1 "Htext", f2 "mpobjectives", f3..f50 the objectives' icons (allied, axis, neutral of 0..15), f51
// "voting", f52 "camera", f53 "carnage_stats", f54 the scene's camera, f55 "explosivetimer" (f56..f65 its
// "timer-explosive00..09"), f66 "mp_scoretable" (f67 "mpallied_score", f68 "mpaxis_score").
constexpr int F_INJURY = 0, F_HTEXT = 1, F_OBJECTIVES = 2, F_ICONS = 3, F_VOTING = 51, F_PHOTO = 52, F_CARNAGE = 53, F_CAMERA = 54,
              F_TIMER = 55, F_TIMERS = 56, F_SCORE = 66, F_ALLIED_SCORE = 67, F_AXIS_SCORE = 68;
bool g_missing[FRAMES];
I3D_frame *FindFrame(const char *name)
{
    int n;
    int at = -1;
    if (std::strcmp(name, "injury") == 0)
        at = F_INJURY;
    else if (std::strcmp(name, "Htext") == 0)
        at = F_HTEXT;
    else if (std::strcmp(name, "mpobjectives") == 0)
        at = F_OBJECTIVES;
    else if (std::strcmp(name, "voting") == 0)
        at = F_VOTING;
    else if (std::strcmp(name, "camera") == 0)
        at = F_PHOTO;
    else if (std::strcmp(name, "carnage_stats") == 0)
        at = F_CARNAGE;
    else if (std::strcmp(name, "explosivetimer") == 0)
        at = F_TIMER;
    else if (std::strcmp(name, "mp_scoretable") == 0)
        at = F_SCORE;
    else if (std::sscanf(name, "mpallied_obj%02d", &n) == 1 && n >= 0 && n < 16 && std::strlen(name) == 14)
        at = F_ICONS + n * 3;
    else if (std::sscanf(name, "mpaxis_obj%02d", &n) == 1 && n >= 0 && n < 16 && std::strlen(name) == 12)
        at = F_ICONS + n * 3 + 1;
    else if (std::sscanf(name, "mpneutral_obj%02d", &n) == 1 && n >= 0 && n < 16 && std::strlen(name) == 15)
        at = F_ICONS + n * 3 + 2;
    return at < 0 || g_missing[at] ? nullptr : FrameAt(at);
}

I3D_frame *FindChild(I3D_frame *f, const char *name)
{
    int n;
    if (FrameIndex(f) == F_TIMER && std::sscanf(name, "timer-explosive%02d", &n) == 1 && n >= 0 && n < 10 && std::strlen(name) == 17)
        return FrameAt(F_TIMERS + n);
    if (FrameIndex(f) == F_SCORE && std::strcmp(name, "mpallied_score") == 0)
        return FrameAt(F_ALLIED_SCORE);
    if (FrameIndex(f) == F_SCORE && std::strcmp(name, "mpaxis_score") == 0)
        return FrameAt(F_AXIS_SCORE);
    return nullptr;
}

// ---- the video streams ----
constexpr int STREAMS = 8;
struct S_Stream {
    void **vt;
    void *texture;   // GetVideoTexture's answer points here
};
S_Stream g_streams[STREAMS];
void *g_streamVt[24];
struct S_StreamWorld {
    int next;                 // CreateStream's next
    HRESULT open[STREAMS];    // Open's answers
};
S_StreamWorld g_sw, g_swStart;
int StreamIndex(const void *s)
{
    for (int i = 0; i < STREAMS; ++i)
        if (s == &g_streams[i])
            return i;
    return -1;
}
void *__stdcall G_CreateStream(void *)
{
    const int s = g_sw.next < STREAMS ? g_sw.next++ : STREAMS - 1;
    Log("graph CreateStream -> s%d", s);
    return &g_streams[s];
}
uint32_t __stdcall S_Release(void *s)
{
    Log("s%d Release", StreamIndex(s));
    return 0;
}
HRESULT __stdcall S_Open(void *s, const char *name, uint32_t flags)
{
    Log("s%d Open '%s' %x", StreamIndex(s), name, flags);
    return g_sw.open[StreamIndex(s)];
}
void __stdcall S_Close(void *s) { Log("s%d Close", StreamIndex(s)); }
void __stdcall S_Play(void *s) { Log("s%d Play", StreamIndex(s)); }
void __stdcall S_Stop(void *s) { Log("s%d Stop", StreamIndex(s)); }
void __stdcall S_SetPosition(void *s, int position) { Log("s%d SetPosition %d", StreamIndex(s), position); }
void *__stdcall S_GetVideoTexture(void *s)
{
    Log("s%d GetVideoTexture", StreamIndex(s));
    return &static_cast<S_Stream *>(s)->texture;
}
void __stdcall S_UpdateVideo(void *s, int unknown) { Log("s%d UpdateVideo %d", StreamIndex(s), unknown); }
void __stdcall S_Other(void *s) { Log("s%d other slot", StreamIndex(s)); }

// ---- the sound module, the effects, the game, the texts, the actors module ----
std::string RequestText(const S_SoundRequest &r)
{
    char text[400];
    std::snprintf(text, sizeof text, "%s %s flags %x %s %s chapter %d group %d %s %s %s channel %d %s %s %s", VecText(r.pos).c_str(),
                  FloatText(r.posW).c_str(), r.flags, Name(r.attachTo).c_str(), Name(r.source).c_str(), r.chapter, r.group,
                  r.fileName ? r.fileName : "null", FloatText(r.nearRange).c_str(), FloatText(r.farRange).c_str(), r.channel,
                  FloatText(r.nearScale).c_str(), FloatText(r.farScale).c_str(), FloatText(r.volume).c_str());
    return text;
}
struct S_Fake2 {
    void **vt;
};
void *g_soundVt[32], *g_effectsVt[2][64];
S_Fake2 g_sound{g_soundVt}, g_effects{nullptr};
int __attribute__((thiscall)) O_SoundPlay(void *, const S_SoundRequest *r)
{
    Log("sound Play %s", RequestText(*r).c_str());
    return 1;
}
void *__cdecl O_GetSoundModule() { return &g_sound; }
void __attribute__((thiscall)) E_SetFadeLevel(void *, float level, const S_vector *color)
{
    Log("effects SetFadeLevel %s %s", FloatText(level).c_str(), color ? VecText(*color).c_str() : "null");
}
void __attribute__((thiscall)) E_Other(void *) { Log("effects other slot"); }
void *__cdecl O_GetGEffects() { return &g_effects; }
alignas(16) uint8_t g_game[0x400], g_sceneBlock[0x400];
C_str TextOf(int id)
{
    Log("texts Get %x", id);
    char text[16];
    std::snprintf(text, sizeof text, "T%x", id);
    return C_str(text);
}
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    C_str s = TextOf(id);
    *static_cast<void **>(out) = s.m_rep;
    s.m_rep = nullptr;
    return out;
}
int &OrigCarnage() { return *OriginalAt<int *>(0x0089b500); }

// The actors (a0..a39: humans, explosive places, others), the players, the game's clocks, the net game's scores, the
// settings, the cutscene: the world of the score table and the timers.
constexpr int ACTORS = 40, PLAYERS = 8;
alignas(16) uint8_t g_actorBlocks[ACTORS][0x800];
struct S_World67 {
    int count;                // the actors module's
    int list[ACTORS];         // its entries (a block or -1: a null slot)
    int type[ACTORS], health[ACTORS], team[ACTORS], state[ACTORS], timeLeft[ACTORS];
    bool spectating[ACTORS];
    float scores[3];
    int roundLeft, respawnLeft, roundTime, teamLivesUsed;
    int gameType, allowRespawn, coopLives, teamLives;
    bool cutscene;
    int slot, livesUsed[PLAYERS];
    uint32_t role;
};
S_World67 g_w, g_wStart;
int ActorIndex(const void *p)
{
    for (int i = 0; i < ACTORS; ++i)
        if (p == g_actorBlocks[i])
            return i;
    return -1;
}
int RoundLeft(const void *game)
{
    Log("%s GetRoundTimeLeft", Name(game).c_str());
    return g_w.roundLeft;
}
int RespawnLeft(const void *game)
{
    Log("%s GetRespawnTimeLeft", Name(game).c_str());
    return g_w.respawnLeft;
}
bool Spectating(const void *h)
{
    Log("a%d IsSpectating", ActorIndex(h));
    return g_w.spectating[ActorIndex(h)];
}
int Team(const void *h, bool pending)
{
    Log("a%d GetTeam %d", ActorIndex(h), pending);
    return g_w.team[ActorIndex(h)];
}
uint8_t g_players[PLAYERS][0xC4];
void *g_playersVector[4];   // the original's {allocator, begin, end, end}
void LockPlayers() { Log("net LockPlayers"); }
void UnlockPlayers() { Log("net UnlockPlayers"); }
int __attribute__((thiscall)) O_RoundLeft(void *game) { return RoundLeft(game); }
int __attribute__((thiscall)) O_RespawnLeft(void *game) { return RespawnLeft(game); }
bool __attribute__((thiscall)) O_Spectating(void *h) { return Spectating(h); }
int __attribute__((thiscall)) O_Team(void *h, bool pending) { return Team(h, pending); }
void *__attribute__((thiscall)) O_LockPlayers(void *)
{
    LockPlayers();
    return g_playersVector;
}
void __attribute__((thiscall)) O_UnlockPlayers(void *) { UnlockPlayers(); }
void *g_origActors[ACTORS + 32];   // the original's vector's array (past its count: null, the original reads 32 slots)

std::string PartName(const void *p)
{
    const std::string g = GfxName(p);
    if (!g.empty())
        return g;
    if (StreamIndex(p) >= 0)
        return "s" + std::to_string(StreamIndex(p));
    if (p == &g_sound)
        return "sound";
    if (p == &g_effects)
        return "effects";
    if (ActorIndex(p) >= 0)
        return "a" + std::to_string(ActorIndex(p));
    if (p == g_game)
        return "game";
    for (int i = 0; i < STREAMS; ++i)
        if (p == &g_streams[i].texture)
            return "s" + std::to_string(i) + ".texture";
    for (const void *b : g_boxes)
        if (b && p == b)
            return BoxName(p);
    return "";
}

}  // namespace H

// ---- the port's side ----
int C_SoundModule::Play(const S_SoundRequest &request) { return H::O_SoundPlay(this, &request); }
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(&H::g_sound); }
C_GEffectsModule *GetGEffects() { return reinterpret_cast<C_GEffectsModule *>(&H::g_effects); }
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_game); }
C_str C_GameTexts::Get(int id) const { return H::TextOf(id); }
alignas(16) static uint8_t g_settingsBlock[0x400];
static S_MultiplayerSettings *g_settingsPointer = reinterpret_cast<S_MultiplayerSettings *>(g_settingsBlock);
S_MultiplayerSettings *&g_mpSettings = g_settingsPointer;
static_assert(offsetof(C_game, m_scene) == 0x18, "the game's scene");
static_assert(sizeof(C_ActorsModule) <= 0x400, "the actors module's storage (interface_globals.cpp)");
int C_game::GetRoundTimeLeft() const { return H::RoundLeft(this); }
int C_game::GetRespawnTimeLeft() const { return H::RespawnLeft(this); }
bool C_Human::IsSpectating() const { return H::Spectating(this); }
int C_Human::GetTeam(bool pending) const { return H::Team(this, pending); }
S_NetPlayerState *C_NetGame::LockPlayers()
{
    H::LockPlayers();
    return reinterpret_cast<S_NetPlayerState *>(H::g_players);
}
void C_NetGame::UnlockPlayers() { H::UnlockPlayers(); }
static_assert(sizeof(S_NetPlayerState) == 0xC4 && offsetof(S_NetPlayerState, livesUsed) == 0xB0, "the players as the original's");
static_assert(sizeof(C_Human) <= 0x800 && sizeof(C_ExplosivePlace) <= 0x800, "the actor blocks");
constexpr uint32_t P_TYPE = offsetof(C_Actor, m_type), P_HEALTH = offsetof(C_Human, m_stats), P_STATE = offsetof(C_ExplosivePlace, m_state),
                   P_TIMELEFT = offsetof(C_ExplosivePlace, m_timeLeft), P_ROUNDTIME = offsetof(C_game, m_roundTime),
                   P_TEAMLIVES = offsetof(C_game, m_teamLivesUsed), P_GAMETYPE = offsetof(S_MultiplayerSettings, gameType),
                   P_RESPAWN = offsetof(S_MultiplayerSettings, allowRespawn), P_COOPLIVES = offsetof(S_MultiplayerSettings, coopLives),
                   P_SHAREDLIVES = offsetof(S_MultiplayerSettings, teamLives);

namespace H {

template <class T> T At(const uint8_t *b, uint32_t o)
{
    T v;
    std::memcpy(&v, b + o, sizeof v);
    return v;
}

// ---- a case's scenario ----
struct S_Op {
    int kind;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
    S_Gfx gfx;
};
S_Run g_runs[2];
uint32_t g_seed;
std::vector<uint32_t> g_startWords;
int g_carnage, g_carnageStart;
enum { W_CARNAGE = 100, W_FRAME, W_ACTORS, W_PLACE, W_CLOCKS, W_SETTINGS, W_CUTSCENE, W_PLAYERS };
template <class T> void Both(uint8_t *block, uint32_t orig, uint32_t port, T value)
{
    std::memcpy(block + orig, &value, sizeof value);
    std::memcpy(block + port, &value, sizeof value);
}
// (The score table's slots are the players': humans only, as the game's; its objectives count reads every slot as one.)
bool g_humansOnly;
void RandomActors()
{
    g_w.count = P(0.1) ? Pick({0, 1, 31, 32, 33}) : I(0, ACTORS);
    for (int i = 0; i < ACTORS; ++i) {
        g_w.list[i] = P(0.15) ? -1 : I(0, ACTORS - 1);
        g_w.type[i] = P(0.5) || g_humansOnly ? int(ACTOR_HUMAN) : P(0.6) ? int(ACTOR_EXPLOSIVE_PLACE) : Pick({int(ACTOR_DOOR), int(ACTOR_CAR), 0});
        g_w.health[i] = P(0.7) ? I(1, 100) : Pick({0, -2});
        g_w.team[i] = P(0.8) ? I(0, 1) : I(-1, 3);
        g_w.spectating[i] = P(0.2);
        g_w.state[i] = P(0.6) ? 1 : I(0, 3);
        g_w.timeLeft[i] = P(0.2) ? Pick({0, -1, 1, 999, 1000, 59999, 60000, 3600000}) : I(-100, 200000);
    }
}
void RandomClocks()
{
    g_w.roundLeft = P(0.2) ? Pick({0, -1, 59999, 60000, 999, INT_MAX}) : I(0, 3600000);
    g_w.respawnLeft = P(0.2) ? Pick({0, -1, 999, 1000, 60000, 61000, 3660000}) : I(0, 200000);
    g_w.roundTime = P(0.2) ? Pick({0, -5000, 59999}) : I(0, 7200000);
    g_w.teamLivesUsed = I(-1, 20);
    for (float &v : g_w.scores)
        v = P(0.2) ? Pick({0.0f, -1.0f, 0.5f, 1e10f, -1e10f, NAN, INFINITY, 2147483648.0f, 99.99f}) : float(I(0, 100));
}
void RandomSettings()
{
    g_w.gameType = P(0.8) ? I(0, 3) : Pick({-1, 4});
    g_w.allowRespawn = P(0.5);
    g_w.coopLives = P(0.3) ? -1 : I(0, 20);
    g_w.teamLives = P(0.5);
    g_w.role = P(0.3) ? 7u : Pick({0u, 1u, 3u, 6u});
    g_w.slot = I(0, PLAYERS - 1);
    for (int &l : g_w.livesUsed)
        l = I(-1, 20);
}
// The world to both sides' memory (the port's own objects, the original's addresses).
void SyncWorld67()
{
    for (int i = 0; i < ACTORS; ++i) {
        // (Only the type's own fields: the port's layouts of a human and a place may overlap.)
        uint8_t *b = g_actorBlocks[i];
        std::memset(b, 0, sizeof g_actorBlocks[i]);
        Both(b, 0x18, P_TYPE, g_w.type[i]);
        if (g_w.type[i] == ACTOR_HUMAN)
            Both(b, 0xEC, P_HEALTH, g_w.health[i]);
        if (g_w.type[i] == ACTOR_EXPLOSIVE_PLACE) {
            Both(b, 0x94, P_STATE, g_w.state[i]);
            Both(b, 0xC8, P_TIMELEFT, g_w.timeLeft[i]);
        }
    }
    std::fill(std::begin(g_origActors), std::end(g_origActors), nullptr);
    std::vector<C_Actor *> list;
    for (int i = 0; i < g_w.count; ++i) {
        void *a = g_w.list[i] < 0 ? nullptr : g_actorBlocks[g_w.list[i]];
        g_origActors[i] = a;
        list.push_back(static_cast<C_Actor *>(a));
    }
    *OriginalAt<void **>(0x0089b4c0) = g_origActors;
    *OriginalAt<void **>(0x0089b4c4) = g_origActors + g_w.count;
    *OriginalAt<void **>(0x0089b4c8) = g_origActors + g_w.count;
    g_actorsModule.m_actors = list;
    Both(g_game, 0x30, P_ROUNDTIME, g_w.roundTime);
    Both(g_game, 0x84, P_TEAMLIVES, g_w.teamLivesUsed);
    for (int t = 0; t < 3; ++t) {
        OriginalAt<float *>(0x0097d4bc)[t] = g_w.scores[t];
        g_netGame.m_teamScores[t] = g_w.scores[t];
    }
    uint8_t *settings = reinterpret_cast<uint8_t *>(g_mpSettings);
    Both(settings, 0x224, P_GAMETYPE, g_w.gameType);
    Both(settings, 0x240, P_RESPAWN, g_w.allowRespawn);
    Both(settings, 0x270, P_COOPLIVES, g_w.coopLives);
    Both(settings, 0x27C, P_SHAREDLIVES, g_w.teamLives);
    *OriginalAt<bool *>(0x0086933c) = g_w.cutscene;
    g_scriptModule.m_cutsceneRunning = g_w.cutscene;
    g_localPlayerSlot = g_w.slot;
    *OriginalAt<int *>(0x00982104) = g_w.slot;
    for (int p = 0; p < PLAYERS; ++p)
        std::memcpy(g_players[p] + 0xB0, &g_w.livesUsed[p], 4);
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
}
// A world part drawn again from the operation's own seed (the same on both sides).
void RandomWorldFrom(int seed, void (*draw)())
{
    const std::mt19937 saved = g_rng;
    g_rng.seed(uint32_t(seed));
    draw();
    g_rng = saved;
}
void ApplyWorld(const S_Op &op)
{
    switch (op.kind) {
    case W_CARNAGE:
        g_carnage = op.a;
        break;
    case W_FRAME:   // (the interface switches an overlay's frame)
        FrameAt(op.a)->m_frameFlags = op.b ? FrameAt(op.a)->m_frameFlags | 1u : FrameAt(op.a)->m_frameFlags & ~1u;
        break;
    case W_ACTORS:
        RandomWorldFrom(op.a, &RandomActors);
        break;
    case W_PLACE:
        g_w.state[op.a] = op.b;
        g_w.timeLeft[op.a] = op.c;
        break;
    case W_CLOCKS:
        RandomWorldFrom(op.a, &RandomClocks);
        break;
    case W_SETTINGS:
        RandomWorldFrom(op.a, &RandomSettings);
        break;
    case W_CUTSCENE:
        g_w.cutscene = op.a != 0;
        break;
    case W_PLAYERS:
        g_w.livesUsed[op.a] = op.b;
        g_w.slot = op.c;
        break;
    }
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
    // (The injury's first face group has a material: Init duplicates it, Start sets its texture.)
    if (!*reinterpret_cast<void **>(g_gfx.groups[F_INJURY][0] + 4))
        *reinterpret_cast<void **>(g_gfx.groups[F_INJURY][0] + 4) = g_gfx.materials[0];
    std::fill(std::begin(g_missing), std::end(g_missing), false);
    // (The camera's world matrix with its w column: [3][3] 1 as a real one, at times not.)
    FrameAt(F_CAMERA)->m_worldMatrix.m[3][3] = P(0.8) ? 1.0f : F(-2, 2);
    g_sw.next = 0;
    for (HRESULT &h : g_sw.open)
        h = P(0.75) ? 0 : Pick({-1, 1, int(0x80004005)});
    for (int i = 0; i < STREAMS; ++i)
        g_streams[i].texture = g_gfx.textures[I(0, TEXTURES - 1)];
    g_carnageStart = P(0.3) ? Pick({0, -1, 999}) : I(0, 200);
    RandomActors();
    RandomClocks();
    RandomSettings();
    g_w.cutscene = P(0.2);
    for (int p = 0; p < PLAYERS; ++p)
        std::memset(g_players[p], 0, sizeof g_players[p]);
}
void SaveStart()
{
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    std::memcpy(&g_gfxStart, &g_gfx, sizeof g_gfx);
    g_swStart = g_sw;
    g_wStart = g_w;
}
void StartSide(int side)
{
    std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
    std::memcpy(&g_gfx, &g_gfxStart, sizeof g_gfx);
    g_sw = g_swStart;
    g_carnage = g_carnageStart;
    g_w = g_wStart;
    g_log.clear();
    g_boxes.clear();
    g_effects.vt = g_effectsVt[side];
    std::srand(g_seed);
}
// The world's carnage count on both sides (the port's own copy, the original's word), each operation.
void SyncWorld()
{
    reinterpret_cast<C_ActorsModule &>(g_actorsModule).m_carnageEnemies = g_carnage;
    OrigCarnage() = g_carnage;
    SyncWorld67();
}
template <class P_T>
void RunCase(const std::vector<S_Op> &ops, size_t origBytes, const std::function<void(int side, P_T *port, uint8_t *orig, const S_Op &op)> &run,
             const std::function<std::string(int side, P_T *port, const uint8_t *orig)> &snap, size_t startBytes = 0x100)
{
    for (int side = 0; side < 2; ++side) {
        StartSide(side);
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
            Log("-- %d %d %d %d", op.kind, op.a, op.b, op.c);
            SyncWorld();
            if (op.kind >= 100)
                ApplyWorld(op);
            else
                run(side, port, orig, op);
            g_log.push_back(snap(side, port, orig));
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
// The operations' common shape: Init first, then `count` of `kinds`, a Done (kind 1) ending them, then a Done if alive.
std::vector<S_Op> RandomOps(std::initializer_list<int> kinds, const std::function<void(S_Op &)> &fill, bool doneAtEnd = true)
{
    std::vector<S_Op> ops;
    ops.push_back(S_Op{0});
    const std::vector<int> k(kinds);
    bool alive = true;
    for (int n = I(0, 14); n > 0 && alive; --n) {
        S_Op op{k[I(0, int(k.size()) - 1)]};
        fill(op);
        alive = op.kind != 1;
        ops.push_back(op);
    }
    if (alive && doneAtEnd)
        ops.push_back(S_Op{1});
    return ops;
}
std::string Ptrs(std::initializer_list<const void *> p)
{
    std::string s;
    for (const void *q : p)
        s += " " + Name(q);
    return s;
}
using T_Init = bool(__attribute__((thiscall)) *)(void *, void *);
using T_Void = void(__attribute__((thiscall)) *)(void *);
using T_Int = void(__attribute__((thiscall)) *)(void *, int);
using T_Bool = void(__attribute__((thiscall)) *)(void *, bool);
using T_Show2 = void(__attribute__((thiscall)) *)(void *, bool, int);
using T_Show3 = void(__attribute__((thiscall)) *)(void *, bool, int, int);
using T_Show4 = void(__attribute__((thiscall)) *)(void *, bool, int, int, int);
using T_Icon = void(__attribute__((thiscall)) *)(void *, int, int);
using T_IsShown = bool(__attribute__((thiscall)) *)(void *);
int Delta() { return P(0.6) ? Pick({0, 1, 16, 33, 100, 125, 250, 1000, 2000}) : I(-50, 3000); }
#define INIT(addr) Log("Init -> %d", side ? port->Init(reinterpret_cast<I3D_scene *>(&g_scene)) : OriginalAt<T_Init>(addr)(orig, &g_scene))
// A text box's margin (+0x90 the original's), logged right after the calls that set it (the snaps would read freed boxes).
void LogMargin(int side, const C_TextBox *portBox, const uint8_t *origBlock, uint32_t at)
{
    const uint8_t *box = side ? nullptr : At<const uint8_t *>(origBlock, at);
    if (side ? !portBox : !box)
        return;
    Log("margin %d", side ? portBox->m_margin : box[0x90]);
}

// ---- the injury flash ----
void TestInjury()
{
    g_humansOnly = false;
    StartScenario();
    const auto ops = RandomOps({1, 2, 2, 2, 3, 3, 3, 3}, [](S_Op &op) {   // Done, Start, Update
        op.a = op.kind == 2 ? P(0.7) : Delta();
    });
    SaveStart();
    RunCase<C_InjuryFlash>(
        ops, 0x30,
        [](int side, C_InjuryFlash *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004ad250);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004ad310)(orig);
                break;
            case 2:
                side ? port->Start(op.a != 0) : OriginalAt<T_Bool>(0x004ad380)(orig, op.a != 0);
                break;
            case 3:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004ad440)(orig, op.a);
                break;
            }
        },
        [](int side, C_InjuryFlash *port, const uint8_t *orig) {
            std::string s = "injury" + Ptrs({side ? port->m_frame : At<void *>(orig, 0), side ? port->m_video : At<void *>(orig, 4)});
            for (int i = 0; i < 7; ++i)
                s += " " + Name(side ? port->m_videos[i] : At<void *>(orig, 8 + i * 4));
            char text[80];
            std::snprintf(text, sizeof text, " time %d alpha %s rising %d", side ? port->m_time : At<int>(orig, 0x24),
                          FloatText(side ? port->m_alpha : At<float>(orig, 0x28)).c_str(), side ? port->m_rising : At<bool>(orig, 0x2C));
            return s + text;
        });
}

// ---- the big message ----
void TestBigMessage()
{
    StartScenario();
    const auto ops = RandomOps({1, 2, 2, 3, 3, 4, 4, 5, 6, W_FRAME}, [](S_Op &op) {   // Done, ShowRoundResult, ShowCountdown, Show, Render, IsShown
        op.a = P(0.7);
        op.b = I(0x100, 0x2000);
        op.c = op.kind == 3 ? (P(0.3) ? Pick({0, -1, 59999, 60000, 3599999, 3600000, INT_MAX, INT_MIN}) : I(0, 7200000)) : I(-1, 9);
        op.d = I(-1, 9);
        if (op.kind == W_FRAME) {
            op.a = F_HTEXT;
            op.b = P(0.5);
        }
    });
    SaveStart();
    RunCase<C_BigMessage>(
        ops, 8,
        [](int side, C_BigMessage *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004ad4e0);
                LogMargin(side, side ? port->m_text : nullptr, orig, 4);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004ad640)(orig);
                break;
            case 2:
                side ? port->ShowRoundResult(op.a != 0, op.b, op.c, op.d) : OriginalAt<T_Show4>(0x004ad650)(orig, op.a != 0, op.b, op.c, op.d);
                break;
            case 3:
                side ? port->ShowCountdown(op.a != 0, op.b, op.c) : OriginalAt<T_Show3>(0x004ad790)(orig, op.a != 0, op.b, op.c);
                break;
            case 4:
                side ? port->Show(op.a != 0, op.b) : OriginalAt<T_Show2>(0x004ad880)(orig, op.a != 0, op.b);
                break;
            case 5:
                side ? port->Render() : OriginalAt<T_Void>(0x004ad8d0)(orig);
                break;
            case 6:   // (the original's IsShown is its frame's flag read inline)
                Log("IsShown %d", side ? port->IsShown() : (At<I3D_frame *>(orig, 0)->m_frameFlags & 1) != 0);
                break;
            }
        },
        [](int side, C_BigMessage *port, const uint8_t *orig) {
            return "big" + Ptrs({side ? port->m_frame : At<void *>(orig, 0), side ? port->m_text : At<void *>(orig, 4)});
        });
}

// ---- the objectives' icons ----
void TestObjectives()
{
    StartScenario();
    g_missing[F_OBJECTIVES] = P(0.3);
    const auto ops = RandomOps({2, 2, 2}, [](S_Op &op) {   // SetIcon
        op.a = I(0, 15);
        op.b = P(0.8) ? I(-1, 2) : Pick({3, -5, 100});
    }, false);
    SaveStart();
    RunCase<C_ObjectiveIcons>(
        ops, 0xC4,
        [](int side, C_ObjectiveIcons *port, uint8_t *orig, const S_Op &op) {
            if (op.kind == 0)
                INIT(0x004ad8f0);
            else
                side ? port->SetIcon(op.a, op.b) : OriginalAt<T_Icon>(0x004ad9b0)(orig, op.a, op.b);
        },
        [](int side, C_ObjectiveIcons *port, const uint8_t *orig) {
            std::string s = "objectives " + Name(side ? port->m_frame : At<void *>(orig, 0));
            for (int i = 0; i < 16; ++i)
                for (int k = 0; k < 3; ++k)
                    s += " " + Name(side ? port->m_icons[i][k] : At<void *>(orig, 4 + (i * 3 + k) * 4));
            return s;
        });
}

// ---- the voting panel ----
void TestVoting()
{
    StartScenario();
    g_missing[F_VOTING] = P(0.3);
    const auto ops = RandomOps({1, 2, 2, W_FRAME}, [](S_Op &op) {   // Done, Render
        if (op.kind == W_FRAME) {
            op.a = F_VOTING;
            op.b = P(0.5);
        }
    });
    SaveStart();
    RunCase<C_VotingPanel>(
        ops, 8,
        [](int side, C_VotingPanel *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004af2e0);
                LogMargin(side, side ? port->m_text : nullptr, orig, 4);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004af450)(orig);
                break;
            case 2:
                side ? port->Render() : OriginalAt<T_Void>(0x004af480)(orig);
                break;
            }
        },
        [](int side, C_VotingPanel *port, const uint8_t *orig) {
            return "voting" + Ptrs({side ? port->m_frame : At<void *>(orig, 0), side ? port->m_text : At<void *>(orig, 4)});
        });
}

// ---- the photo flash ----
void TestPhoto()
{
    StartScenario();
    g_missing[F_PHOTO] = P(0.2);
    const auto ops = RandomOps({2, 2, 3, 3, 3, 3, 3}, [](S_Op &op) {   // Start, Update
        op.a = op.kind == 2 ? P(0.7) : (P(0.5) ? Pick({0, 1, 50, 150, 200, 1000, 1001, 1050, 1200, 1998, 1999, 2000, 2001}) : I(-20, 2500));
    }, false);
    SaveStart();
    RunCase<C_PhotoFlash>(
        ops, 0xC,
        [](int side, C_PhotoFlash *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004af4a0);
                break;
            case 2:
                side ? port->Start(op.a != 0) : OriginalAt<T_Bool>(0x004af600)(orig, op.a != 0);
                break;
            case 3:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004af4f0)(orig, op.a);
                break;
            }
        },
        [](int side, C_PhotoFlash *port, const uint8_t *orig) {
            char text[80];
            std::snprintf(text, sizeof text, " left %d pending %d", side ? port->m_timeLeft : At<int>(orig, 4),
                          side ? port->m_soundPending : At<bool>(orig, 8));
            return "photo " + Name(side ? port->m_frame : At<void *>(orig, 0)) + text;
        });
}

// ---- the carnage counter ----
void TestCarnage()
{
    StartScenario();
    const auto ops = RandomOps({1, 2, 2, 2, 3, W_CARNAGE, W_FRAME}, [](S_Op &op) {   // Done, Update, Render
        if (op.kind == W_CARNAGE)
            op.a = P(0.3) ? Pick({0, -1, 999, INT_MIN}) : I(0, 200);
        if (op.kind == W_FRAME) {
            op.a = F_CARNAGE;
            op.b = P(0.6);
        }
    });
    SaveStart();
    RunCase<C_CarnageStats>(
        ops, 8,
        [](int side, C_CarnageStats *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004afc00);
                LogMargin(side, side ? port->m_text : nullptr, orig, 4);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004afda0)(orig);
                break;
            case 2:
                side ? port->Update(0) : OriginalAt<T_Int>(0x004afdb0)(orig, 0);
                break;
            case 3:
                side ? port->Render() : OriginalAt<T_Void>(0x004afe30)(orig);
                break;
            }
        },
        [](int side, C_CarnageStats *port, const uint8_t *orig) {
            return "carnage" + Ptrs({side ? port->m_frame : At<void *>(orig, 0), side ? port->m_text : At<void *>(orig, 4)});
        });
}

// ---- the pause and countdown texts ----
void TestPause()
{
    StartScenario();
    const auto ops = RandomOps({1, 2, 2, 3}, [](S_Op &op) { op.a = P(0.5); });   // Done, Render, SetShown (inline both)
    SaveStart();
    RunCase<C_PauseText>(
        ops, 0xC,
        [](int side, C_PauseText *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004afe50);
                LogMargin(side, side ? port->m_text : nullptr, orig, 0);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004affc0)(orig);
                break;
            case 2:
                side ? port->Render() : OriginalAt<T_Void>(0x004afff0)(orig);
                break;
            case 3:
                if (side)
                    port->SetShown(op.a != 0);
                else
                    orig[4] = op.a != 0;
                break;
            }
        },
        [](int side, C_PauseText *port, const uint8_t *orig) {
            char text[60];
            std::snprintf(text, sizeof text, " shown %d %d", side ? port->m_shown : At<bool>(orig, 4), side ? port->m_unused08 : At<int>(orig, 8));
            return "pause " + Name(side ? port->m_text : At<void *>(orig, 0)) + text;
        });
}
void TestCountdown()
{
    StartScenario();
    const auto ops = RandomOps({1, 2, 2, 3, 3, 3, 4}, [](S_Op &op) {   // Done, Show, Update, Render
        op.a = op.kind == 2 ? P(0.7) : Delta();
        op.b = I(0x100, 0x2000);
        op.c = P(0.4) ? Pick({-1, 0, 1, 16, 1000, -5}) : I(0, 5000);
    });
    SaveStart();
    RunCase<C_CountdownText>(
        ops, 0xC,
        [](int side, C_CountdownText *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004b0000);
                LogMargin(side, side ? port->m_text : nullptr, orig, 0);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004b0180)(orig);
                break;
            case 2:
                side ? port->Show(op.a != 0, op.b, op.c) : OriginalAt<T_Show3>(0x004b01a0)(orig, op.a != 0, op.b, op.c);
                break;
            case 3:
                side ? port->Update(op.a) : OriginalAt<T_Int>(0x004b01e0)(orig, op.a);
                break;
            case 4:
                side ? port->Render() : OriginalAt<T_Void>(0x004b0210)(orig);
                break;
            }
        },
        [](int side, C_CountdownText *port, const uint8_t *orig) {
            char text[60];
            std::snprintf(text, sizeof text, " shown %d left %d", side ? port->m_shown : At<bool>(orig, 4), side ? port->m_timeLeft : At<int>(orig, 8));
            return "countdown " + Name(side ? port->m_text : At<void *>(orig, 0)) + text;
        });
}

// ---- the score table ----
void TestScore()
{
    g_humansOnly = true;
    StartScenario();
    const auto ops = RandomOps({1, 2, 2, 2, 2, 3, W_ACTORS, W_CLOCKS, W_CLOCKS, W_SETTINGS, W_PLAYERS, W_FRAME}, [](S_Op &op) {   // Done, Update, Render
        switch (op.kind) {
        case W_ACTORS:
        case W_CLOCKS:
        case W_SETTINGS:
            op.a = int(U32() & 0x7FFFFFFF);
            break;
        case W_PLAYERS:
            op.a = I(0, PLAYERS - 1);
            op.b = I(-1, 20);
            op.c = I(0, PLAYERS - 1);
            break;
        case W_FRAME:
            op.a = F_SCORE;
            op.b = P(0.5);
            break;
        }
    });
    SaveStart();
    RunCase<C_ScoreTable>(
        ops, 0xC,
        [](int side, C_ScoreTable *port, uint8_t *orig, const S_Op &op) {
            switch (op.kind) {
            case 0:
                INIT(0x004ae7c0);
                LogMargin(side, side ? port->m_text : nullptr, orig, 4);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004aed00)(orig);
                break;
            case 2:
                side ? port->Update(0) : OriginalAt<T_Int>(0x004aed20)(orig, 0);
                break;
            case 3:
                side ? port->Render() : OriginalAt<T_Void>(0x004af2b0)(orig);
                break;
            }
        },
        [](int side, C_ScoreTable *port, const uint8_t *orig) {
            return "score" + Ptrs({side ? port->m_frame : At<void *>(orig, 0), side ? port->m_text : At<void *>(orig, 4)}) + " type " +
                   std::to_string(side ? port->m_gameType : At<int>(orig, 8));
        });
}

// ---- the explosive timers ----
using T_SetPos = void(__attribute__((thiscall)) *)(void *, const float *);
void TestTimers()
{
    g_humansOnly = false;
    StartScenario();
    const auto ops = RandomOps({1, 2, 3, 3, 3, 3, 4, W_ACTORS, W_PLACE, W_PLACE, W_CUTSCENE}, [](S_Op &op) {   // Done, SetPos, Update, Render
        switch (op.kind) {
        case 2:
            op.a = I(-1000, 1000);
            op.b = I(-1000, 1000);
            op.c = I(-1000, 1000);
            break;
        case W_ACTORS:
            op.a = int(U32() & 0x7FFFFFFF);
            break;
        case W_PLACE:
            op.a = I(0, ACTORS - 1);
            op.b = P(0.6) ? 1 : I(0, 3);
            op.c = P(0.2) ? Pick({0, -1, 1, 999, 1000, 60000}) : I(-100, 200000);
            break;
        case W_CUTSCENE:
            op.a = P(0.3);
            break;
        }
    });
    SaveStart();
    RunCase<C_ExplosiveTimer>(
        ops, 0x74,
        [](int side, C_ExplosiveTimer *port, uint8_t *orig, const S_Op &op) {
            const float pos[3] = {op.a * 0.001f, op.b * 0.001f, op.c * 0.001f};
            switch (op.kind) {
            case 0:
                INIT(0x004af630);
                for (int i = 0; i < 10; ++i)
                    LogMargin(side, side ? port->m_texts[i] : nullptr, orig, 0x2C + i * 4);
                break;
            case 1:
                side ? port->Done() : OriginalAt<T_Void>(0x004af8b0)(orig);
                break;
            case 2:
                side ? port->SetPos(pos) : OriginalAt<T_SetPos>(0x004af900)(orig, pos);
                for (int i = 0; i < 10; ++i)
                    LogMargin(side, side ? port->m_texts[i] : nullptr, orig, 0x2C + i * 4);
                break;
            case 3:
                side ? port->Update(0) : OriginalAt<T_Int>(0x004af980)(orig, 0);
                break;
            case 4:
                side ? port->Render() : OriginalAt<T_Void>(0x004afbd0)(orig);
                break;
            }
        },
        [](int side, C_ExplosiveTimer *port, const uint8_t *orig) {
            std::string s = "timers " + Name(side ? port->m_frame : At<void *>(orig, 0));
            for (int i = 0; i < 10; ++i)
                s += " " + Name(side ? port->m_timers[i] : At<void *>(orig, 4 + i * 4)) + "/" +
                     Name(side ? port->m_texts[i] : At<void *>(orig, 0x2C + i * 4));
            s += " " + Name(side ? port->m_scene : At<void *>(orig, 0x60)) + " explosives";
            if (side) {
                for (C_Actor *a : port->m_explosives)
                    s += " " + Name(a);
            } else {
                for (void *const *a = At<void *const *>(orig, 0x68); a && a < At<void *const *>(orig, 0x6C); ++a)
                    s += " " + Name(*a);
            }
            return s;
        },
        0x54);   // (the frame, the timers and their texts: the vectors after them each side's own)
}

void SetUp()
{
    g_partName = PartName;
    g_findFrame = FindFrame;
    g_findChild = FindChild;
    SetUpGfx();
    g_playersVector[1] = g_players;
    g_playersVector[2] = g_players + PLAYERS;
    g_playersVector[3] = g_players + PLAYERS;
    for (void *&v : g_streamVt)
        v = reinterpret_cast<void *>(S_Other);
    g_streamVt[0x04 / 4] = reinterpret_cast<void *>(S_Release);
    g_streamVt[0x0C / 4] = reinterpret_cast<void *>(S_Open);
    g_streamVt[0x10 / 4] = reinterpret_cast<void *>(S_Close);
    g_streamVt[0x14 / 4] = reinterpret_cast<void *>(S_Play);
    g_streamVt[0x18 / 4] = reinterpret_cast<void *>(S_Stop);
    g_streamVt[0x30 / 4] = reinterpret_cast<void *>(S_SetPosition);
    g_streamVt[0x48 / 4] = reinterpret_cast<void *>(S_GetVideoTexture);
    g_streamVt[0x4C / 4] = reinterpret_cast<void *>(S_UpdateVideo);
    for (S_Stream &s : g_streams)
        s.vt = g_streamVt;
    g_graphVt[0x38 / 4] = reinterpret_cast<void *>(G_CreateStream);
    for (void *&v : g_soundVt)
        v = reinterpret_cast<void *>(E_Other);
    g_soundVt[0x54 / 4] = reinterpret_cast<void *>(O_SoundPlay);
    // The effects' SetFadeLevel: the original's slot 0x88, the port's where g++ put it (its member pointer: 1 + the offset).
    auto fade = &C_GEffectsModule::SetFadeLevel;
    uintptr_t raw;
    std::memcpy(&raw, &fade, sizeof raw);
    for (int side = 0; side < 2; ++side)
        for (void *&v : g_effectsVt[side])
            v = reinterpret_cast<void *>(E_Other);
    g_effectsVt[0][0x88 / 4] = reinterpret_cast<void *>(E_SetFadeLevel);
    g_effectsVt[1][(raw - 1) / 4] = reinterpret_cast<void *>(E_SetFadeLevel);
    // The game's scene, the scene's camera (f54).
    *reinterpret_cast<void **>(g_game + 0x18) = g_sceneBlock;
    *reinterpret_cast<void **>(g_sceneBlock + 0x15C) = FrameAt(F_CAMERA);
    *OriginalAt<void **>(0x008ae8f4) = g_game;
    *OriginalAt<void **>(0x008aea30) = g_mpSettings;   // (one settings block: each side reads its own offsets)
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x004336c0, reinterpret_cast<const void *>(O_GetSoundModule)},
        {0x00486510, reinterpret_cast<const void *>(O_GetGEffects)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
        {0x0069b150, reinterpret_cast<const void *>(O_RoundLeft)},
        {0x0069b180, reinterpret_cast<const void *>(O_RespawnLeft)},
        {0x005a63f0, reinterpret_cast<const void *>(O_Spectating)},
        {0x005a8820, reinterpret_cast<const void *>(O_Team)},
        {0x0072f040, reinterpret_cast<const void *>(O_LockPlayers)},
        {0x0072f100, reinterpret_cast<const void *>(O_UnlockPlayers)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("overlays_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_OVERLAYS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_OVERLAYS_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_OVERLAYS_CASE");
    const struct {
        const char *name;
        void (*run)();
    } cases[] = {{"Injury", TestInjury}, {"BigMessage", TestBigMessage}, {"Objectives", TestObjectives}, {"Voting", TestVoting},
                 {"Photo", TestPhoto},   {"Carnage", TestCarnage},       {"Pause", TestPause},           {"Countdown", TestCountdown},
                 {"Score", TestScore},   {"Timers", TestTimers}};
    for (const auto &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round)
            c.run();
    }
    std::printf("overlays_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
