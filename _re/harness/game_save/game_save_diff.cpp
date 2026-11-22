// Human part 79: the game's saves, loads and restarts (game/game_save.cpp: CanQuickSave, LoadSavedGame, RestartMission,
// SaveGame, LoadGame) against the original's (0x0069c550 .. 0x0069d960). MinGW; build_game_save.bat; runs
// game_save_diff.exe ("game_save_diff: N checks, 0 failures"). game_save.cpp is compiled into this file. What the game
// reaches outside itself is faked on both sides with the same loggers: the checkpoints, the interface, the first person
// view, the quick messages (their resets; their saves write a marked chunk, their loads read one), the modules, the
// timed events, the mission (its objectives' statuses, DeactivateObjectives, its progress saved and loaded), the
// campaign (the current campaign, GetMissionNumber, GetCurrentMission), the profile (the difficulty, the stains
// option), the network game (the players' humans and lives, CountTeamPlayers, the replication), the actors module (its
// actors by id: spawn points and others, DestroyActor, FindSpawnPoint), the spawn point's Spawn, the mission squad (its
// soldiers deployed or not), the application (its state, PostCommand), the speech sets' delays, the stains, the music,
// the texts and messages; the chunks real on both sides (common/chunk.cpp, the original's own) over data files in
// memory. The original's calls into its copy protection (0x00a59000, its timing) are patched out.
// Each scenario sets the game's fields (state, mode, the clocks, the switches, the saves used, the pending spawns), the
// static state record (0x008ae69c), the world, then runs up to 12 operations (CanQuickSave, RestartMission, SaveGame
// compared byte by byte with the next chunk after it, LoadGame of files the world builds (every chunk of the game's in
// any order, versions and mission names that differ, short and long state records, unknown chunks, a child chunk running
// past its parent), LoadGame of the side's own last save, LoadSavedGame, the world changed); the logs (every call out,
// the game's fields after each operation) compared.
// HD2_GAME_SAVE_ROUNDS: scenarios (default 60,000; HD2_GAME_SAVE_QUICK: 20,000); HD2_INTERFACE_ULPS,
// HD2_INTERFACE_SHOW, HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/game_save.cpp"
#include "game/campaign.h"
#include "game/game_app.h"
#include "game/profile.h"
#include "game/spawn_point.h"
#include "net/net_game.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "original_exe.h"

#include "../interface/interface_kit.inc"

namespace H {

int g_side;
template <class T> void Put(void *b, uint32_t o, T v) { std::memcpy(static_cast<uint8_t *>(b) + o, &v, sizeof v); }
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
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

// ---- the objects (both sides' blocks where the layouts differ) ----
constexpr int ACTORS = 40, SOLDIERS = 6, PLAYERS = 8, OBJECTIVES = 4, PENDING = 4, SPEECH_SETS = 2;
constexpr uint32_t SPEECH_SET_BYTES = 0x1C + 0x8A * 0x1C;
alignas(16) uint8_t g_origGame[0x100], g_portGame[0x400], g_objs[4][0x40], g_origMission[0x100], g_portMission[0x400],
    g_objective[2][OBJECTIVES][0x100], g_origCampaign[0x100], g_portCampaign[0x800], g_origSquad[0x40], g_portSquad[0x100],
    g_soldier[2][SOLDIERS][0x200], g_origMp[0x300], g_portMp[0x400], g_players[2][PLAYERS][0xC4], g_origPlayers[0x10],
    g_actor[2][ACTORS][0x400], g_spawnPoint[0x40], g_origApp[0x40], g_portApp[0x200], g_origSpeech[0x10], g_portSpeech[0x40],
    g_speechSet[2][SPEECH_SETS][SPEECH_SET_BYTES], g_detail[0x40], g_localHuman[0x40], g_actorList[ACTORS * 4];
void *g_origGameVt[0x40], *g_portGameVt[0x40], *g_origMissionVt[0x40], *g_portMissionVt[0x40], *g_actorVt[2][0x60];
void *g_objectivePtr[2][OBJECTIVES], *g_soldierPtr[2][SOLDIERS], *g_pendingPtr[2][PENDING], *g_setPtr[2][SPEECH_SETS];
const char *const OBJ_NAMES[4] = {"checkpoints", "ui", "fpv", "quick"};
enum { OBJ_CHECKPOINTS, OBJ_UI, OBJ_FPV, OBJ_QUICK };
C_game *PG() { return reinterpret_cast<C_game *>(g_portGame); }
void *Game(int side) { return side ? static_cast<void *>(g_portGame) : static_cast<void *>(g_origGame); }
void *Mission(int side) { return side ? static_cast<void *>(g_portMission) : static_cast<void *>(g_origMission); }
void *g_chunkOf[2];   // the chunk an operation passes (for its name)

// ---- the answers ----
uint32_t g_retSeed;
std::vector<std::pair<std::string, int>> g_retCounts;
uint32_t RetWord(const char *name)
{
    int count = 0;
    for (auto &c : g_retCounts)
        if (c.first == name) {
            count = ++c.second;
            break;
        }
    if (!count)
        g_retCounts.emplace_back(name, count = 1);
    uint32_t h = Fnv(name, g_retSeed) ^ uint32_t(count) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}
bool RetBool(const char *name) { return (RetWord(name) & 3) != 0; }

// ---- the world ----
struct S_World {
    uint32_t role;
    int difficulty;
    // The game's fields.
    int state, mode, savesUsed, fadeTime, endDelay, roundTime, inputDelay, vipSlot, teamLivesUsed, roundState,
        respawnRequest, spectatedSlot, cameraMode, musicTimer, alarmCount;
    uint32_t missionTime, time;
    bool hasQuickSave, uniformDisguise, noTeamDamage, tacticalMode, signalPistolFired, paused, playerInputEnabled,
        missionFailed, vipChosen, unknown94;
    int pending;
    char missionName[48];
    uint8_t savedState[0x18];   // the static record a short chunk 9000 leaves
    // The mission (or none), its objectives' statuses; the campaign.
    bool hasMission;
    int objectives, status[OBJECTIVES];
    int campaign, missionNumber;
    // The network game: the shared lives, the players (a human, lives used).
    int teamLives, players;
    bool human[PLAYERS];
    int livesUsed[PLAYERS];
    // The actors module: actors by id (null, a spawn point or another kind).
    int actors;
    int actorKind[ACTORS];   // 0 none, 1 spawn point, 2 other
    // The mission squad.
    int soldiers;
    bool deployed[SOLDIERS];
    // The application's state is the game; the speech sets.
    bool appIsGame;
    int sets;
};
S_World g_w;

std::string PartName(const void *p);
void *g_retMission[2];
void *RetPtr(const char *name)
{
    const uint32_t w = RetWord(name);
    if (std::strcmp(name, "C_ActorsModule::FindSpawnPoint") == 0)
        return (w & 3) == 0 ? nullptr : static_cast<void *>(g_spawnPoint);
    if (std::strcmp(name, "C_NetGame::GetLocalHuman") == 0)
        return (w & 3) == 0 ? nullptr : static_cast<void *>(g_localHuman);
    if (std::strcmp(name, "C_Campaign::GetCurrentMission") == 0)
        return g_w.hasMission ? Mission(g_side) : nullptr;
    return nullptr;
}

// ---- names ----
std::string PartName(const void *p)
{
    for (int i = 0; i < 4; ++i)
        if (p == g_objs[i])
            return OBJ_NAMES[i];
    for (int side = 0; side < 2; ++side) {
        if (p == Game(side))
            return "game";
        if (p == Mission(side))
            return "mission";
        if (g_chunkOf[side] && p == g_chunkOf[side])
            return "chunk";
        for (int i = 0; i < ACTORS; ++i)
            if (p == g_actor[side][i])
                return "a" + std::to_string(i);
        for (int i = 0; i < SOLDIERS; ++i)
            if (p == g_soldier[side][i])
                return "s" + std::to_string(i);
    }
    if (p == g_spawnPoint)
        return "spawnPoint";
    if (p == g_localHuman)
        return "localHuman";
    if (p == g_detail)
        return "detail";
    return "";
}

// ---- the chunks (each side's own functions; the layouts are the same) ----
using T_ChunkOpen = bool(__attribute__((thiscall)) *)(void *, const char *, bool);
using T_ChunkClose = bool(__attribute__((thiscall)) *)(void *);
using T_ChunkBegin = bool(__attribute__((thiscall)) *)(void *, uint16_t);
using T_ChunkWrite = bool(__attribute__((thiscall)) *)(void *, const void *, int);
using T_ChunkAscend = bool(__attribute__((thiscall)) *)(void *, uint32_t *);
int FileSeek(int handle, int offset, int origin);
bool CBegin(void *c, uint16_t id) { return g_side ? static_cast<C_chunk *>(c)->Begin(id) : OriginalAt<T_ChunkBegin>(0x00405820)(c, id); }
bool CWrite(void *c, const void *d, int n) { return g_side ? static_cast<C_chunk *>(c)->Write(d, n) : OriginalAt<T_ChunkWrite>(0x004059b0)(c, d, n); }
bool CAscend(void *c) { return g_side ? static_cast<C_chunk *>(c)->Ascend() : OriginalAt<T_ChunkAscend>(0x00405890)(c, nullptr); }
bool CRead(void *c, void *d, int n) { return g_side ? static_cast<C_chunk *>(c)->Read(d, n) : OriginalAt<T_ChunkWrite>(0x00405940)(c, d, n); }
int CPos(const void *c) { return At<bool>(c, 0) ? FileSeek(At<int>(c, 4), 0, 1) : -1; }
int CDepth(const void *c) { return At<int>(c, 0x410); }

// ---- the fakes (F_: both sides' logic; O_: the original's entry points) ----
void Called(const void *self, const char *what) { Log("%s %s", Name(self).c_str(), what); }
// A save: a marked chunk (an id of its own, a word of the answers).
bool SaveFake(const void *self, void *chunk, const char *name, uint16_t id)
{
    const uint32_t v = RetWord(name);
    Log("%s %s at %d depth %d", Name(self).c_str(), name, CPos(chunk), CDepth(chunk));
    CBegin(chunk, id);
    CWrite(chunk, &v, 4);
    CAscend(chunk);
    return RetBool(name);
}
// A load: a word read (or not).
bool LoadFake(const void *self, void *chunk, const char *name)
{
    uint32_t v = 0xDEAD;
    const int at = CPos(chunk);
    const bool ok = CRead(chunk, &v, 4);
    Log("%s %s at %d depth %d: %d %08x", Name(self).c_str(), name, at, CDepth(chunk), ok, ok ? v : 0u);
    return RetBool(name);
}
void __attribute__((thiscall)) O_CheckpointsReset(void *self) { Called(self, "ResetState"); }
bool __attribute__((thiscall)) O_CheckpointsSave(void *self, void *c) { return SaveFake(self, c, "C_Checkpoints::SaveGame", 0x5A01); }
bool __attribute__((thiscall)) O_CheckpointsLoad(void *self, void *c) { return LoadFake(self, c, "C_Checkpoints::LoadGame"); }
void __attribute__((thiscall)) O_UiReset(void *self) { Called(self, "Reset"); }
bool __attribute__((thiscall)) O_UiSave(void *self, void *c) { return SaveFake(self, c, "C_Interface::SaveGame", 0x5A02); }
bool __attribute__((thiscall)) O_UiLoad(void *self, void *c) { return LoadFake(self, c, "C_Interface::LoadGame"); }
void __attribute__((thiscall)) O_FpvReset(void *self) { Called(self, "Reset"); }
bool __attribute__((thiscall)) O_FpvSave(void *self, void *c) { return SaveFake(self, c, "C_FirstPersonView::SaveGame", 0x5A03); }
bool __attribute__((thiscall)) O_FpvLoad(void *self, void *c) { return LoadFake(self, c, "C_FirstPersonView::LoadGame"); }
void __attribute__((thiscall)) O_QuickReset(void *self) { Called(self, "Reset"); }
bool __attribute__((thiscall)) O_QuickSave(void *self, void *c) { return SaveFake(self, c, "C_QuickMessages::SaveGame", 0x5A04); }
bool __attribute__((thiscall)) O_QuickLoad(void *self, void *c) { return LoadFake(self, c, "C_QuickMessages::LoadGame"); }
// The modules and the timed events (the chunk in ECX; the broadcast's flag in CL).
void __attribute__((fastcall)) O_Broadcast(uint32_t ecx) { Log("modules BroadcastRestartMission %d", int(ecx & 0xFF)); }
bool __attribute__((fastcall)) O_ModulesSave(void *c) { return SaveFake(nullptr, c, "ModuleManager::SaveGame", 0x5A05); }
bool __attribute__((fastcall)) O_ModulesLoad(void *c) { return LoadFake(nullptr, c, "ModuleManager::LoadGame"); }
void __cdecl O_TimedClear() { Log("TimedEvents_Clear"); }
bool __attribute__((fastcall)) O_TimedSave(void *c) { return SaveFake(nullptr, c, "TimedEvents_Save", 0x5A06); }
bool __attribute__((fastcall)) O_TimedLoad(void *c) { return LoadFake(nullptr, c, "TimedEvents_Load"); }
// The mission.
void __attribute__((thiscall)) O_Deactivate(void *self) { Called(self, "DeactivateObjectives"); }
bool __attribute__((thiscall)) V_MissionSave(void *self, void *c) { return SaveFake(self, c, "C_MissionDef::SaveProgress", 0x5A07); }
bool __attribute__((thiscall)) V_MissionLoad(void *self, void *c) { return LoadFake(self, c, "C_MissionDef::LoadProgress"); }
void __attribute__((thiscall)) V_Other(void *self) { Log("%s other slot", Name(self).c_str()); }
// The campaign.
int MissionNumber(int campaign)
{
    const uint32_t w = RetWord("C_Campaign::GetMissionNumber");
    Log("campaign GetMissionNumber %d", campaign);
    return (w & 3) == 0 ? g_w.missionNumber : int(w >> 8 & 3);
}
int __attribute__((thiscall)) O_MissionNumber(void *, int campaign) { return MissionNumber(campaign); }
void *CurrentMission()
{
    Log("campaign GetCurrentMission");
    return RetPtr("C_Campaign::GetCurrentMission");
}
void *__attribute__((thiscall)) O_CurrentMission(void *) { return CurrentMission(); }
// The profile.
// (A getter, not logged: the original's CanQuickSave asks twice.)
int __attribute__((thiscall)) O_Difficulty(void *) { return g_w.difficulty; }
void *__attribute__((thiscall)) O_DetailSettings(void *)
{
    Log("profile GetDetailSettings");
    return g_detail;
}
bool DetailEnabled(const void *self, int option)
{
    Log("%s IsEnabled %d", Name(self).c_str(), option);
    return RetBool("C_PlayerOptions::IsEnabled");
}
bool __attribute__((thiscall)) O_DetailEnabled(void *self, int option) { return DetailEnabled(self, option); }
// The network game (the original's players: a vector {allocator, first, last, end} of 0xC4-byte states).
void CountTeams(int *a, int *b)
{
    const uint32_t w = RetWord("C_NetGame::CountTeamPlayers");
    *a = int(w & 7);
    *b = int(w >> 8 & 7);
    Log("net CountTeamPlayers %d %d", *a, *b);
}
void __attribute__((thiscall)) O_CountTeams(void *, int *a, int *b) { CountTeams(a, b); }
void *__attribute__((thiscall)) O_LockPlayers(void *)
{
    Log("net LockPlayers");
    return g_origPlayers;
}
void __attribute__((thiscall)) O_UnlockPlayers(void *) { Log("net UnlockPlayers"); }
void __attribute__((thiscall)) O_FlipParity(void *) { Log("net FlipSessionParity"); }
void __attribute__((thiscall)) O_ResetReplication(void *, bool all) { Log("net ResetReplication %d", int(all)); }
void *LocalHuman()
{
    Log("net GetLocalHuman");
    return RetPtr("C_NetGame::GetLocalHuman");
}
void *__attribute__((thiscall)) O_LocalHuman(void *) { return LocalHuman(); }
// The actors module.
void *ActorById(int id)
{
    Log("actors GetActorById %d", id);
    return id >= 0 && id < g_w.actors && g_w.actorKind[id] ? static_cast<void *>(g_actor[g_side][id]) : nullptr;
}
void *__attribute__((thiscall)) O_ActorById(void *, int id) { return ActorById(id); }
void DestroyActor(const void *actor, bool now) { Log("actors DestroyActor %s %d", Name(actor).c_str(), int(now)); }
void __attribute__((thiscall)) O_DestroyActor(void *, void *actor, bool now) { DestroyActor(actor, now); }
void *FindSpawnPoint(int team)
{
    Log("actors FindSpawnPoint %d", team);
    return RetPtr("C_ActorsModule::FindSpawnPoint");
}
void *__attribute__((thiscall)) O_FindSpawnPoint(void *, int team) { return FindSpawnPoint(team); }
void __attribute__((thiscall)) V_StartMission(void *self, bool server) { Log("%s StartMission %d", Name(self).c_str(), int(server)); }
void __attribute__((thiscall)) V_StopMission(void *self, bool server) { Log("%s StopMission %d", Name(self).c_str(), int(server)); }
void Spawn(const void *self, int slot, const void *state, const int *stats)
{
    Log("%s Spawn %d %s %s", Name(self).c_str(), slot, Name(state).c_str(), stats ? "stats" : "-");
}
void __attribute__((thiscall)) O_Spawn(void *self, int slot, void *state, int *stats) { Spawn(self, slot, state, stats); }
// The application.
void *__cdecl O_GetGameApp() { return g_origApp; }
void *__cdecl O_GetGame() { return g_origGame; }
void PostCommand(int command, int a, int b, bool flush) { Log("app PostCommand %d %d %d %d", command, a, b, int(flush)); }
void __attribute__((thiscall)) O_PostCommand(void *, int command, int a, int b, bool flush) { PostCommand(command, a, b, flush); }
// The game's own.
void RestoreMusic(const void *self, bool alarm) { Log("%s RestoreMusic %d", Name(self).c_str(), int(alarm)); }
void __attribute__((thiscall)) O_RestoreMusic(void *self, bool alarm) { RestoreMusic(self, alarm); }
bool __attribute__((thiscall)) V_Start(void *self, void *arg)
{
    Log("%s Start %s", Name(self).c_str(), Name(arg).c_str());
    return RetBool("C_game::Start");
}
void PrintText(const void *self, const float *color, const char *format, va_list args)
{
    char text[600];
    std::vsnprintf(text, sizeof text, format, args);
    Log("%s PrintMessage %s %s %s %s", Name(self).c_str(), FloatText(color[0]).c_str(), FloatText(color[1]).c_str(),
        FloatText(color[2]).c_str(), text);
}
void __cdecl O_PrintMessage(void *self, const float *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    PrintText(self, color, format, args);
    va_end(args);
}
// The texts: "text N" (TEXT_SAVES_LEFT a format).
std::string TextOf(int id) { return id == 0x2536 ? "saves left %d" : "text " + std::to_string(id); }
void *__attribute__((thiscall)) O_TextsGet(void *, void *out, int id)
{
    Log("texts Get %d", id);
    using T_Assign = void *(__attribute__((thiscall)) *)(void *, const char *);
    Put<void *>(out, 0, nullptr);
    OriginalAt<T_Assign>(0x00405180)(out, TextOf(id).c_str());
    return out;
}
int __cdecl O_Vsnprintf(char *buffer, size_t size, const char *format, va_list args) { return std::vsnprintf(buffer, size, format, args); }
// The speech slots, the sets (the original resets their delays inline), the stains.
void __cdecl O_FreeSpeechSlots() { Log("FreeSpeechSlots"); }
void __cdecl O_ClearSpeechSlots() { Log("ClearSpeechSlots"); }
void *__cdecl O_GetSpeechSets()
{
    Log("GetSpeechSets");
    return g_origSpeech;
}
void __attribute__((thiscall)) O_StainsReset(void *) { Log("stains Reset"); }
// The original's aligned allocations (its CRT's: not started here).
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }

}  // namespace H

// ---- the port's side of the fakes ----
C_Campaign *g_campaignStore;
S_MissionSquad *g_squadStore;
S_MultiplayerSettings *g_mpStore;
C_Campaign *&g_campaign = g_campaignStore;
S_MissionSquad *&g_missionSquad = g_squadStore;
S_MultiplayerSettings *&g_mpSettings = g_mpStore;
void C_Checkpoints::ResetState() { H::O_CheckpointsReset(this); }
bool C_Checkpoints::SaveGame(C_chunk &chunk) { return H::O_CheckpointsSave(this, &chunk); }
bool C_Checkpoints::LoadGame(C_chunk &chunk) { return H::O_CheckpointsLoad(this, &chunk); }
void C_Interface::Reset() { H::O_UiReset(this); }
bool C_Interface::SaveGame(C_chunk &chunk) { return H::O_UiSave(this, &chunk); }
bool C_Interface::LoadGame(C_chunk &chunk) { return H::O_UiLoad(this, &chunk); }
void C_FirstPersonView::Reset() { H::O_FpvReset(this); }
bool C_FirstPersonView::SaveGame(C_chunk &chunk) { return H::O_FpvSave(this, &chunk); }
bool C_FirstPersonView::LoadGame(C_chunk &chunk) { return H::O_FpvLoad(this, &chunk); }
void C_QuickMessages::Reset() { H::O_QuickReset(this); }
bool C_QuickMessages::SaveGame(C_chunk &chunk) { return H::O_QuickSave(this, &chunk); }
bool C_QuickMessages::LoadGame(C_chunk &chunk) { return H::O_QuickLoad(this, &chunk); }
void ModuleManager::BroadcastRestartMission(bool server) { H::O_Broadcast(server); }
bool ModuleManager::SaveGame(C_chunk &chunk) { return H::O_ModulesSave(&chunk); }
bool ModuleManager::LoadGame(C_chunk &chunk) { return H::O_ModulesLoad(&chunk); }
void TimedEvents_Clear() { H::O_TimedClear(); }
bool TimedEvents_Save(C_chunk &chunk) { return H::O_TimedSave(&chunk); }
bool TimedEvents_Load(C_chunk &chunk) { return H::O_TimedLoad(&chunk); }
void C_MissionDef::DeactivateObjectives() { H::O_Deactivate(this); }
int C_Campaign::GetMissionNumber(int campaign) { return H::MissionNumber(campaign); }
C_MissionDef *C_Campaign::GetCurrentMission() { return static_cast<C_MissionDef *>(H::CurrentMission()); }
int C_GameProfile::GetDifficulty() const { return H::O_Difficulty(nullptr); }
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return reinterpret_cast<C_PlayerOptions *>(H::O_DetailSettings(nullptr)); }
bool C_PlayerOptions::IsEnabled(int option) const { return H::DetailEnabled(this, option); }
void C_NetGame::CountTeamPlayers(int *axis, int *allies) { H::CountTeams(axis, allies); }
S_NetPlayerState *C_NetGame::LockPlayers()
{
    H::Log("net LockPlayers");
    return reinterpret_cast<S_NetPlayerState *>(H::g_players[1]);
}
void C_NetGame::UnlockPlayers() { H::O_UnlockPlayers(nullptr); }
void C_NetGame::FlipSessionParity() { H::O_FlipParity(nullptr); }
void C_NetGame::ResetReplication(bool all) { H::O_ResetReplication(nullptr, all); }
C_Human *C_NetGame::GetLocalHuman() { return static_cast<C_Human *>(H::LocalHuman()); }
C_Actor *C_ActorsModule::GetActorById(int id) const { return static_cast<C_Actor *>(H::ActorById(id)); }
void C_ActorsModule::DestroyActor(C_Actor *actor, bool now) { H::DestroyActor(actor, now); }
C_Actor *C_ActorsModule::FindSpawnPoint(int team) const { return static_cast<C_Actor *>(H::FindSpawnPoint(team)); }
void C_SpawnPoint::Spawn(int slot, S_SoldierState *state, int *stats) { H::Spawn(this, slot, state, stats); }
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(H::g_portApp); }
C_game *GetGame() { return H::PG(); }
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush) { H::PostCommand(command, int(a), int(b), flush); }
void C_game::RestoreMusic(bool alarm) { H::RestoreMusic(this, alarm); }
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    H::PrintText(this, &color.x, format, args);
    va_end(args);
}
C_str C_GameTexts::Get(int id) const
{
    H::Log("texts Get %d", id);
    return C_str(H::TextOf(id).c_str());
}
void FreeSpeechSlots() { H::O_FreeSpeechSlots(); }
void ClearSpeechSlots() { H::O_ClearSpeechSlots(); }
C_SpeechSets *C_Human::GetSpeechSets()
{
    H::Log("GetSpeechSets");
    return reinterpret_cast<C_SpeechSets *>(H::g_portSpeech);
}
// (human_speech.cpp's: the delays of the port's mirror of the sets cleared, as the original's inline loop does.)
void C_SpeechSets::ResetDelays()
{
    for (int s = 0; s < H::g_w.sets; ++s)
        for (uint32_t k = 0; k < 0x8A; ++k)
            H::Put<uint32_t>(H::g_speechSet[1][s], 0x1C + 0x1C * k, 0);
}
void C_StainManager::Reset() { H::O_StainsReset(this); }
// (game_rounds.cpp's and game_menu.cpp's, not compiled here; the original inlines both: the same reads.)
bool C_game::AreObjectivesDecided() const { return m_mission && m_mission->AreObjectivesDecided(); }
bool C_MissionDef::AreObjectivesDecided() const
{
    if (AreObjectivesInactive())
        return false;
    for (const S_MissionObjective *objective : m_objectives) {
        if (objective->status == OBJECTIVE_OPEN)
            return false;
    }
    return true;
}

// The data files in memory (both sides).
namespace H {
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        Log("dta open %s -> -1", name);
        return -1;
    }
    if (write)
        g_files[name].clear();
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    Log("dta open %s %d -> %d", name, write, g_nextFile);
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    if (!g_openFiles.count(handle))
        return -1;
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
int FileWrite(int handle, const void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    std::string &f = g_files[o.name];
    if (o.at + size_t(size) > f.size())
        f.resize(o.at + size_t(size));
    std::memcpy(&f[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}
bool FileClose(int handle)
{
    Log("dta close %d", handle);
    return g_openFiles.erase(handle) != 0;
}
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpen(name, true); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }
}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

namespace H {

// ---- each side's world ----
// (An MSVC6 vector {allocator, first, last, end}; a g++ one {begin, end, capacity}.)
void PutVector(void *b, uint32_t at, int side, void *first, int count, uint32_t elem = 4)
{
    uint8_t *f = static_cast<uint8_t *>(first), *l = f ? f + elem * uint32_t(count) : nullptr;
    if (side) {
        Put<uint8_t *>(b, at, f);
        Put<uint8_t *>(b, at + 4, l);
        Put<uint8_t *>(b, at + 8, l);
    } else {
        Put<uint8_t *>(b, at + 4, f);
        Put<uint8_t *>(b, at + 8, l);
        Put<uint8_t *>(b, at + 12, l);
    }
}
#define OFS(type, member) uint32_t(offsetof(type, member))
uint8_t *g_origSaved() { return OriginalAt<uint8_t *>(0x008ae69c); }

// The game's fields: name, the original's offset, the type, the port's member.
#define GAME_FIELDS(X)                                      \
    X(state, 0x04, int, m_state)                            \
    X(hasQuickSave, 0x0C, bool, m_hasQuickSave)             \
    X(savesUsed, 0x10, int, m_savesUsed)                    \
    X(mode, 0x1C, int, m_mode)                              \
    X(fadeTime, 0x20, int, m_fadeTime)                      \
    X(endDelay, 0x24, int, m_endDelay)                      \
    X(missionTime, 0x28, uint32_t, m_missionTime)           \
    X(time, 0x2C, uint32_t, m_time)                         \
    X(roundTime, 0x30, int, m_roundTime)                    \
    X(uniformDisguise, 0x34, bool, m_uniformDisguise)       \
    X(noTeamDamage, 0x35, bool, m_noTeamDamage)             \
    X(tacticalMode, 0x36, bool, m_tacticalMode)             \
    X(inputDelay, 0x38, int, m_inputDelay)                  \
    X(signalPistolFired, 0x40, bool, m_signalPistolFired)   \
    X(paused, 0x64, bool, m_paused)                         \
    X(playerInputEnabled, 0x6E, bool, m_playerInputEnabled) \
    X(missionFailed, 0x6F, bool, m_missionFailed)           \
    X(vipChosen, 0x70, bool, m_vipChosen)                   \
    X(vipSlot, 0x78, int, m_vipSlot)                        \
    X(teamLivesUsed, 0x84, int, m_teamLivesUsed)            \
    X(roundState, 0x88, int, m_roundState)                  \
    X(respawnRequest, 0x90, int, m_respawnRequest)          \
    X(unknown94, 0x94, bool, m_unknown94)                   \
    X(spectatedSlot, 0xB4, int, m_spectatedSlot)            \
    X(cameraMode, 0xB8, int, m_cameraMode)                  \
    X(musicTimer, 0xD0, int, m_musicTimer)                  \
    X(alarmCount, 0xD4, int, m_alarmCount)

void SetRole(uint32_t role)
{
    g_w.role = role;
    g_gameRole = role;
    *OriginalAt<uint32_t *>(0x00982100) = role;
}

void SetOutside(int side);
void SetWorld(int side)
{
    SetRole(g_w.role);
    // The game.
    uint8_t *g = static_cast<uint8_t *>(Game(side));
    if (side) {
        std::memset(g_portGame, 0, sizeof g_portGame);
        *reinterpret_cast<void ***>(g_portGame) = g_portGameVt;
        C_game *p = PG();
#define SET_PORT(name, at, type, member) p->member = g_w.name;
        GAME_FIELDS(SET_PORT)
#undef SET_PORT
        p->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_objs[OBJ_CHECKPOINTS]);
        p->m_interface = reinterpret_cast<C_Interface *>(g_objs[OBJ_UI]);
        p->m_firstPersonView = reinterpret_cast<C_FirstPersonView *>(g_objs[OBJ_FPV]);
        p->m_quickMessages = reinterpret_cast<C_QuickMessages *>(g_objs[OBJ_QUICK]);
        p->m_mission = g_w.hasMission ? reinterpret_cast<C_MissionDef *>(g_portMission) : nullptr;
        p->m_missionName = g_w.missionName;
    } else {
        std::memset(g_origGame, 0, sizeof g_origGame);
        Put<void **>(g, 0, g_origGameVt);
#define SET_ORIG(name, at, type, member) Put<type>(g, at, g_w.name);
        GAME_FIELDS(SET_ORIG)
#undef SET_ORIG
        for (int i = 0; i < 4; ++i)
            Put<void *>(g, 0x54 + 4 * i, g_objs[i]);
        Put<void *>(g, 0xA8, g_w.hasMission ? static_cast<void *>(g_origMission) : nullptr);
        using T_Assign = void *(__attribute__((thiscall)) *)(void *, const char *);
        OriginalAt<T_Assign>(0x00405180)(g + 0x6A, g_w.missionName);
    }
    // The pending spawns (the port's own their state and stats).
    for (int i = 0; i < g_w.pending; ++i) {
        auto *s = static_cast<S_PendingSpawn *>(_aligned_malloc(sizeof(S_PendingSpawn), 16));
        std::memset(s, 0, sizeof *s);
        if (side) {
            s->state = new S_SoldierState();
            s->stats = new int[12]();
        }
        g_pendingPtr[side][i] = s;
    }
    if (side)
        PutVector(g, OFS(C_game, m_pendingSpawns), 1, g_pendingPtr[1], g_w.pending);
    else
        PutVector(g, 0x98, 0, g_pendingPtr[0], g_w.pending);
    // The static state record.
    if (side)
        std::memcpy(&::g_savedGameState, g_w.savedState, 0x18);
    else
        std::memcpy(g_origSaved(), g_w.savedState, 0x18);
    SetOutside(side);
}
// The world outside the game.
void SetOutside(int side)
{
    // The mission and its objectives.
    for (int i = 0; i < g_w.objectives; ++i) {
        std::memset(g_objective[side][i], 0, sizeof g_objective[side][i]);
        if (side)
            reinterpret_cast<S_MissionObjective *>(g_objective[1][i])->status = g_w.status[i];
        else
            Put<int>(g_objective[0][i], 0x10, g_w.status[i]);
        g_objectivePtr[side][i] = g_objective[side][i];
    }
    if (side) {
        std::memset(g_portMission, 0, sizeof g_portMission);
        *reinterpret_cast<void ***>(g_portMission) = g_portMissionVt;
        PutVector(g_portMission, OFS(C_MissionDef, m_objectives), 1, g_objectivePtr[1], g_w.objectives);
    } else {
        std::memset(g_origMission, 0, sizeof g_origMission);
        Put<void **>(g_origMission, 0, g_origMissionVt);
        PutVector(g_origMission, 0x30, 0, g_objectivePtr[0], g_w.objectives);
    }
    // The campaign.
    if (side) {
        std::memset(g_portCampaign, 0, sizeof g_portCampaign);
        reinterpret_cast<C_Campaign *>(g_portCampaign)->m_current = g_w.campaign;
        g_campaignStore = reinterpret_cast<C_Campaign *>(g_portCampaign);
    } else {
        Put<int>(g_origCampaign, 0xE8, g_w.campaign);
        *OriginalAt<void **>(0x008aea10) = g_origCampaign;
    }
    // The multiplayer settings, the players.
    for (int i = 0; i < PLAYERS; ++i) {
        std::memset(g_players[side][i], 0, 0xC4);
        Put<void *>(g_players[side][i], 0x3C, g_w.human[i] ? static_cast<void *>(g_localHuman) : nullptr);
        Put<int>(g_players[side][i], 0xB0, g_w.livesUsed[i]);
    }
    if (side) {
        reinterpret_cast<S_MultiplayerSettings *>(g_portMp)->teamLives = g_w.teamLives;
        g_mpStore = reinterpret_cast<S_MultiplayerSettings *>(g_portMp);
        PutVector(&g_netGame, OFS(C_NetGame, m_players), 1, g_players[1], g_w.players, 0xC4);
    } else {
        Put<int>(g_origMp, 0x27C, g_w.teamLives);
        *OriginalAt<void **>(0x008aea30) = g_origMp;
        PutVector(g_origPlayers, 0, 0, g_players[0], g_w.players, 0xC4);
    }
    // The actors (their kind; the vtables log the mission's start and stop).
    for (int i = 0; i < g_w.actors; ++i) {
        uint8_t *a = g_actor[side][i];
        std::memset(a, 0, 0x400);
        *reinterpret_cast<void ***>(a) = g_actorVt[side];
        const int type = g_w.actorKind[i] == 1 ? int(ACTOR_SPAWN_POINT) : g_w.actorKind[i] == 2 ? int(ACTOR_HUMAN) : 0;
        if (side)
            reinterpret_cast<C_Actor *>(a)->m_type = E_ActorType(type);
        else
            Put<int>(a, 0x18, type);
    }
    if (side)
        PutVector(&g_actorsModule, OFS(C_ActorsModule, m_actors), 1, g_actorList, g_w.actors);
    else {
        Put<void *>(OriginalAt<void *>(0x0089b4c0), 0, g_w.actors ? static_cast<void *>(g_actorList) : nullptr);
        Put<void *>(OriginalAt<void *>(0x0089b4c4), 0, g_w.actors ? static_cast<void *>(g_actorList + 4 * g_w.actors) : nullptr);
    }
    // The mission squad.
    for (int i = 0; i < g_w.soldiers; ++i) {
        std::memset(g_soldier[side][i], 0, sizeof g_soldier[side][i]);
        if (side)
            reinterpret_cast<S_SquadSoldier *>(g_soldier[1][i])->deployed = g_w.deployed[i];
        else
            Put<bool>(g_soldier[0][i], 0x44, g_w.deployed[i]);
        g_soldierPtr[side][i] = g_soldier[side][i];
    }
    if (side) {
        std::memset(g_portSquad, 0, sizeof g_portSquad);
        PutVector(g_portSquad, OFS(S_MissionSquad, states), 1, g_soldierPtr[1], g_w.soldiers);
        g_squadStore = reinterpret_cast<S_MissionSquad *>(g_portSquad);
    } else {
        PutVector(g_origSquad, 4, 0, g_soldierPtr[0], g_w.soldiers);
        *OriginalAt<void **>(0x008aea20) = g_origSquad;
    }
    // The application.
    if (side)
        reinterpret_cast<C_GameApp *>(g_portApp)->m_state = g_w.appIsGame ? PG() : reinterpret_cast<C_GameState *>(g_detail);
    else
        Put<void *>(g_origApp, 0x14, g_w.appIsGame ? static_cast<void *>(g_origGame) : static_cast<void *>(g_detail));
    // The speech sets: every delay set.
    for (int s = 0; s < g_w.sets; ++s) {
        for (uint32_t k = 0; k < 0x8A; ++k)
            Put<uint32_t>(g_speechSet[side][s], 0x1C + 0x1C * k, 1000 + k);
        g_setPtr[side][s] = g_speechSet[side][s];
    }
    if (!side)
        PutVector(g_origSpeech, 0, 0, g_setPtr[0], g_w.sets);
}

void RandomName(char *out, size_t size)
{
    static const char *const NAMES[] = {"Mission01", "Mission02", "Africa1", "Normandy", "Mission0", "Mission011", "x"};
    std::snprintf(out, size, "%s", Pick(NAMES));
}

void RandomWorld()
{
    S_World &w = g_w;
    w.role = Pick({7u, 7u, 1u, 2u, 3u, 5u, 6u});
    w.difficulty = I(1, 4);
    w.state = P(0.7) ? 1 : I(0, 6);
    w.mode = P(0.7) ? 0 : I(0, 2);
    w.savesUsed = P(0.3) ? I(0, 4) : I(-2, 9);
    w.fadeTime = I(-5, 3000);
    w.endDelay = I(-5, 3000);
    w.roundTime = I(0, 99999);
    w.inputDelay = I(-1, 5000);
    w.vipSlot = I(-1, 7);
    w.teamLivesUsed = I(-9, 9);
    w.roundState = I(0, 4);
    w.respawnRequest = I(0, 2);
    w.spectatedSlot = I(-1, 7);
    w.cameraMode = I(0, 3);
    w.musicTimer = I(0, 9000);
    w.alarmCount = I(0, 3);
    w.missionTime = U32() >> I(0, 31);
    w.time = U32() >> I(0, 31);
    for (bool *b : {&w.hasQuickSave, &w.uniformDisguise, &w.noTeamDamage, &w.tacticalMode, &w.signalPistolFired, &w.paused,
                    &w.playerInputEnabled, &w.missionFailed, &w.vipChosen, &w.unknown94})
        *b = P(0.5);
    w.pending = I(0, PENDING);
    RandomName(w.missionName, sizeof w.missionName);
    for (uint8_t &b : w.savedState)
        b = uint8_t(U32());
    w.hasMission = P(0.85);
    w.objectives = I(0, OBJECTIVES);
    for (int &s : w.status)
        s = P(0.4) ? 4 : I(0, 4);
    w.campaign = P(0.5) ? 0 : I(-1, 3);
    w.missionNumber = P(0.5) ? 0 : I(-1, 5);
    w.teamLives = P(0.5) ? 0 : I(1, 9);
    w.players = I(0, PLAYERS - 1);   // (a slot past them watched)
    for (int i = 0; i < PLAYERS; ++i) {
        w.human[i] = P(0.6);
        w.livesUsed[i] = I(-2, 5);
    }
    w.actors = P(0.2) ? 0 : I(1, ACTORS);
    for (int &k : w.actorKind)
        k = P(0.3) ? 0 : P(0.4) ? 1 : 2;
    w.soldiers = I(0, SOLDIERS);
    for (bool &d : w.deployed)
        d = P(0.7);
    w.appIsGame = P(0.7);
    w.sets = I(0, SPEECH_SETS);
}

// ---- the game's fields snapped ----
std::string Snap(int side)
{
    std::string s = "game";
    char t[64];
    const uint8_t *g = static_cast<const uint8_t *>(Game(side));
    if (side) {
        const C_game *p = PG();
#define SNAP_PORT(name, at, type, member)                            \
    std::snprintf(t, sizeof t, " " #name " %d", int(p->member)); \
    s += t;
        GAME_FIELDS(SNAP_PORT)
#undef SNAP_PORT
        std::snprintf(t, sizeof t, " pending %d", int(p->m_pendingSpawns.size()));
    } else {
#define SNAP_ORIG(name, at, type, member)                              \
    std::snprintf(t, sizeof t, " " #name " %d", int(At<type>(g, at))); \
    s += t;
        GAME_FIELDS(SNAP_ORIG)
#undef SNAP_ORIG
        std::snprintf(t, sizeof t, " pending %d", int((At<uint8_t *>(g, 0xA0) - At<uint8_t *>(g, 0x9C)) / 4));
    }
    s += t;
    // The static state record; the players' lives; the speech delays left.
    const uint8_t *saved = side ? reinterpret_cast<const uint8_t *>(&::g_savedGameState) : g_origSaved();
    s += " saved";
    for (int i = 0; i < 0x18; ++i) {
        std::snprintf(t, sizeof t, "%02x", saved[i]);
        s += t;
    }
    s += " lives";
    for (int i = 0; i < PLAYERS; ++i)   // (the slots past the players too: none written)
        s += " " + std::to_string(At<int>(g_players[side][i], 0xB0));
    int delays = 0;
    for (int set = 0; set < g_w.sets; ++set)
        for (uint32_t k = 0; k < 0x8A; ++k)
            delays += At<uint32_t>(g_speechSet[side][set], 0x1C + 0x1C * k) != 0;
    s += " delays " + std::to_string(delays);
    return s;
}

// ---- the files a load reads: 0x29A { 1000 { 2 } 2000 { the name } 9000 { the state } 0x238C { saves } 0x23F0 { the
// pistol } 5000 4000 6000 8000 0x20D0 3000 0x2134 { a word } }, in any order, some missing or doubled ----
std::string Chunk(uint16_t id, const std::string &payload, int extra = 0)
{
    std::string c;
    const uint32_t size = uint32_t(6 + int(payload.size()) + extra);
    c.append(reinterpret_cast<const char *>(&id), 2);
    c.append(reinterpret_cast<const char *>(&size), 4);
    return c + payload;
}
std::string Int4(uint32_t v) { return std::string(reinterpret_cast<const char *>(&v), 4); }
std::string MakeLoadFile()
{
    if (P(0.03))
        return std::string();   // the first Descend fails
    std::vector<std::string> parts;
    // (The version and the saves' and the pistol's words are never short: the original reads stack words then.)
    if (P(0.9))
        parts.push_back(Chunk(1000, Int4(P(0.9) ? 2 : Pick({1u, 3u, 0u}))));
    if (P(0.9)) {
        // (Always terminated within the first 999 bytes: the original reads its stack past the bytes read.)
        std::string name = P(0.85) ? std::string(g_w.missionName) : P(0.5) ? std::string(g_w.missionName) + "x" : "Other";
        if (P(0.05))
            name = std::string(size_t(I(980, 998)), 'n');
        name += '\0';
        if (P(0.2))
            name += std::string(size_t(P(0.5) ? I(1, 9) : I(990, 1100)), char(I(0, 255)));
        parts.push_back(Chunk(2000, name));
    }
    if (P(0.8)) {
        std::string state;
        const int n = P(0.8) ? 0x18 : P(0.5) ? I(0, 0x17) : I(0x19, 0x30);
        for (int i = 0; i < n; ++i)
            state += char(P(0.5) ? I(0, 1) : I(0, 255));
        parts.push_back(Chunk(9000, state));
    }
    if (P(0.7))
        parts.push_back(Chunk(0x238C, Int4(uint32_t(I(0, 5)))));
    if (P(0.7))
        parts.push_back(Chunk(0x23F0, Int4(P(0.7) ? uint32_t(I(0, 1)) : U32())));
    const uint16_t WORDS[] = {5000, 4000, 6000, 8000, 0x20D0, 3000, 0x2134};
    for (uint16_t id : WORDS) {
        if (id == 0x2134 && !g_w.hasMission)
            continue;   // (Without a mission the original and the port call through null.)
        if (P(0.8))
            parts.push_back(Chunk(id, P(0.85) ? Int4(U32()) : std::string()));
    }
    for (int n = P(0.3) ? I(1, 3) : 0; n > 0; --n)
        parts.push_back(Chunk(uint16_t(Pick({999u, 7000u, 0x29Au, 0x2135u, 10000u})), Int4(U32())));
    if (P(0.5))
        std::shuffle(parts.begin(), parts.end(), g_rng);
    else if (P(0.5))
        std::reverse(parts.begin(), parts.end());
    if (P(0.1) && !parts.empty())
        parts.push_back(parts[size_t(I(0, int(parts.size()) - 1))]);   // one twice
    std::string body;
    for (const std::string &p : parts)
        body += p;
    const std::string next = Chunk(0x7777, Int4(1));
    // A child running past its parent (the last one, of a kind whose load reads its own chunk only): the reader past
    // the parent's end, the original reads on to the file's end.
    if (P(0.06)) {
        const uint16_t id = Pick({5000, 4000, 6000, 8000, 0x20D0, 3000, 9000, 0x2135});
        return Chunk(0x29A, body + Chunk(id, Int4(U32()), int(next.size()))) + next;
    }
    std::string f = Chunk(P(0.97) ? 0x29A : 0x29B, body);
    if (P(0.5))
        f += next;
    return f;
}

// ---- the scenarios ----
enum E_Op { OP_CAN_QUICK_SAVE, OP_RESTART, OP_SAVE, OP_LOAD, OP_LOAD_OWN, OP_LOAD_SAVED, W_WORLD, W_ROLE, W_STATE, OP_COUNT };
struct S_Op {
    int kind;
    int a = 0, b = 0;
};
S_Op RandomOp()
{
    S_Op op{Pick({OP_CAN_QUICK_SAVE, OP_CAN_QUICK_SAVE, OP_RESTART, OP_RESTART, OP_SAVE, OP_SAVE, OP_LOAD, OP_LOAD, OP_LOAD, OP_LOAD_OWN,
                  OP_LOAD_SAVED, W_WORLD, W_ROLE, W_STATE})};
    op.a = int(U32() >> 1);
    op.b = I(0, 9);
    return op;
}

using T_V = void(__attribute__((thiscall)) *)(void *);
using T_RetB = bool(__attribute__((thiscall)) *)(void *);
using T_B = void(__attribute__((thiscall)) *)(void *, bool);
using T_RetBP = bool(__attribute__((thiscall)) *)(void *, void *);

// A chunk on the side's: the port's C_chunk, the original's block.
struct S_SideChunk {
    alignas(16) uint8_t orig[0x500] = {};
    C_chunk port;
    void *Ptr(int side) { return side ? static_cast<void *>(&port) : static_cast<void *>(orig); }
};
bool ChunkOpen(int side, S_SideChunk &c, const char *name, bool write)
{
    return side ? c.port.Open(name, write) : OriginalAt<T_ChunkOpen>(0x00405620)(c.orig, name, write);
}
void ChunkClose(int side, S_SideChunk &c) { side ? c.port.Close() : OriginalAt<T_ChunkClose>(0x00405750)(c.orig); }

std::string g_ownSave[2];
void Run(int side, const S_Op &op)
{
    C_game *p = PG();
    void *g = g_origGame;
    switch (op.kind) {
    case OP_CAN_QUICK_SAVE:
        Log("result %d", side ? p->CanQuickSave() : OriginalAt<T_RetB>(0x0069c550)(g));
        break;
    case OP_RESTART:
        side ? p->RestartMission(op.b < 4) : OriginalAt<T_B>(0x0069c660)(g, op.b < 4);
        break;
    case OP_SAVE:
    case OP_LOAD:
    case OP_LOAD_OWN:
    case OP_LOAD_SAVED: {
        const bool save = op.kind == OP_SAVE;
        const char *name = save ? "save.sav" : "load.sav";
        if (op.kind == OP_LOAD_OWN) {
            if (g_ownSave[side].empty())
                break;
            g_files[name] = g_ownSave[side];
        }
        S_SideChunk c;
        // (Now and then the chunk not open.)
        const bool open = op.b != 9;
        if (open)
            ChunkOpen(side, c, name, save);
        g_chunkOf[side] = c.Ptr(side);
        bool result;
        if (op.kind == OP_LOAD_SAVED)
            result = side ? p->LoadSavedGame(c.port) : OriginalAt<T_RetBP>(0x0069c610)(g, c.orig);
        else if (save)
            result = side ? p->SaveGame(c.port) : OriginalAt<T_RetBP>(0x0069cbb0)(g, c.orig);
        else
            result = side ? p->LoadGame(c.port) : OriginalAt<T_RetBP>(0x0069d290)(g, c.orig);
        Log("result %d", int(result));
        if (open) {
            if (save) {
                // (The game's save goes on: the next chunk.)
                CBegin(c.Ptr(side), 0x7777);
                const uint32_t next = 0x12345678;
                CWrite(c.Ptr(side), &next, 4);
                CAscend(c.Ptr(side));
            } else
                Log("left at %d depth %d", CPos(c.Ptr(side)), CDepth(c.Ptr(side)));
            ChunkClose(side, c);
        }
        g_chunkOf[side] = nullptr;
        if (save && open) {
            const std::string &f = g_files[name];
            std::string hex;
            char t[4];
            for (unsigned char ch : f) {
                std::snprintf(t, sizeof t, "%02x", ch);
                hex += t;
            }
            Log("file %d %s", int(f.size()), hex.c_str());
            g_ownSave[side] = f;
        }
        break;
    }
    default:
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
};
S_Run g_runs[2];
void TestScenarios()
{
    g_retSeed = U32();
    RandomWorld();
    const S_World start = g_w;
    std::vector<S_Op> ops;
    for (int n = I(1, 12); n > 0; --n)
        ops.push_back(RandomOp());
    // The load files (made now: both sides read the same).
    std::vector<std::string> files;
    for (const S_Op &op : ops)
        files.push_back(op.kind == OP_LOAD || op.kind == OP_LOAD_SAVED ? MakeLoadFile() : std::string());
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        g_w = start;
        g_log.clear();
        g_retCounts.clear();
        g_files.clear();
        g_openFiles.clear();
        g_nextFile = 1;
        g_ownSave[side].clear();
        SetWorld(side);
        for (size_t i = 0; i < ops.size(); ++i) {
            const S_Op &op = ops[i];
            Log("-- %d %d %d", op.kind, op.a, op.b);
            if (op.kind == W_WORLD) {
                // (The world's own draws: the shared generator kept.)
                const std::mt19937 kept = g_rng;
                g_rng.seed(uint32_t(op.a));
                const S_World before = g_w;
                RandomWorld();
                g_rng = kept;
                // (Only the world outside the game changes: the game's fields as the operations left them; its
                // mission there or not.)
                std::memcpy(g_w.missionName, before.missionName, sizeof g_w.missionName);
                g_w.pending = before.pending;
                g_w.hasMission = before.hasMission;
                SetRole(g_w.role);
                SetOutside(side);
            } else if (op.kind == W_ROLE) {
                const uint32_t ROLES[] = {7u, 1u, 2u, 3u, 5u, 6u};
                SetRole(ROLES[op.b % 6]);
            }
            else if (op.kind == W_STATE) {
                // The game's state and mode (the saves' and loads' guards).
                const int state = op.b < 6 ? 1 : op.b - 5;
                const int mode = op.b == 0 ? 1 : 0;
                if (side) {
                    PG()->m_state = state;
                    PG()->m_mode = mode;
                } else {
                    Put<int>(g_origGame, 0x04, state);
                    Put<int>(g_origGame, 0x1C, mode);
                }
            } else {
                if (op.kind == OP_LOAD || op.kind == OP_LOAD_SAVED)
                    g_files["load.sav"] = files[i];
                Run(side, op);
            }
            g_log.push_back(Snap(side));
        }
        g_runs[side].log.swap(g_log);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

// The original's calls into its copy protection (CALL [0x00a59000]): out; the second's argument popped.
void PatchCode(uint32_t at, std::initializer_list<uint8_t> bytes)
{
    DWORD old;
    VirtualProtect(OriginalAt<void *>(at), bytes.size(), PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(OriginalAt<void *>(at), bytes.begin(), bytes.size());
    VirtualProtect(OriginalAt<void *>(at), bytes.size(), old, &old);
    FlushInstructionCache(GetCurrentProcess(), OriginalAt<void *>(at), bytes.size());
}
uint32_t __stdcall G_Time(void *) { return 0; }

void SetUp()
{
    g_partName = PartName;
    for (void *&v : g_origGameVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portGameVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origGameVt[1] = reinterpret_cast<void *>(V_Start);
    g_portGameVt[Slot(&C_game::Start) / 4] = reinterpret_cast<void *>(V_Start);
    for (void *&v : g_origMissionVt)
        v = reinterpret_cast<void *>(V_Other);
    for (void *&v : g_portMissionVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origMissionVt[0x10 / 4] = reinterpret_cast<void *>(V_MissionLoad);
    g_origMissionVt[0x14 / 4] = reinterpret_cast<void *>(V_MissionSave);
    g_portMissionVt[Slot(&C_MissionDef::LoadProgress) / 4] = reinterpret_cast<void *>(V_MissionLoad);
    g_portMissionVt[Slot(&C_MissionDef::SaveProgress) / 4] = reinterpret_cast<void *>(V_MissionSave);
    for (int side = 0; side < 2; ++side)
        for (void *&v : g_actorVt[side])
            v = reinterpret_cast<void *>(V_Other);
    g_actorVt[0][0x3C / 4] = reinterpret_cast<void *>(V_StartMission);
    g_actorVt[0][0x40 / 4] = reinterpret_cast<void *>(V_StopMission);
    g_actorVt[1][Slot(&C_Actor::StartMission) / 4] = reinterpret_cast<void *>(V_StartMission);
    g_actorVt[1][Slot(&C_Actor::StopMission) / 4] = reinterpret_cast<void *>(V_StopMission);
    // (RestartMission's second protection call takes the graph's slot 0x48 first.)
    g_graphVt[0x48 / 4] = reinterpret_cast<void *>(G_Time);
    PatchCode(0x0069c6bc, {0x31, 0xC0, 0x90, 0x90, 0x90, 0x90});
    PatchCode(0x0069ca59, {0x58, 0x90, 0x90, 0x90, 0x90, 0x90});
    PatchCode(0x0069cc59, {0x31, 0xC0, 0x90, 0x90, 0x90, 0x90});
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x00523e30, reinterpret_cast<const void *>(O_CheckpointsReset)},
        {0x00523f30, reinterpret_cast<const void *>(O_CheckpointsSave)},
        {0x005241a0, reinterpret_cast<const void *>(O_CheckpointsLoad)},
        {0x0049f5e0, reinterpret_cast<const void *>(O_UiReset)},
        {0x0049f600, reinterpret_cast<const void *>(O_UiSave)},
        {0x0049f8d0, reinterpret_cast<const void *>(O_UiLoad)},
        {0x00493360, reinterpret_cast<const void *>(O_FpvReset)},
        {0x00493390, reinterpret_cast<const void *>(O_FpvSave)},
        {0x004933a0, reinterpret_cast<const void *>(O_FpvLoad)},
        {0x0048f400, reinterpret_cast<const void *>(O_QuickReset)},
        {0x0048fc50, reinterpret_cast<const void *>(O_QuickSave)},
        {0x0048fdd0, reinterpret_cast<const void *>(O_QuickLoad)},
        {0x0048dbc0, reinterpret_cast<const void *>(O_Broadcast)},
        {0x0048dc90, reinterpret_cast<const void *>(O_ModulesSave)},
        {0x0048de60, reinterpret_cast<const void *>(O_ModulesLoad)},
        {0x006df880, reinterpret_cast<const void *>(O_TimedClear)},
        {0x006df890, reinterpret_cast<const void *>(O_TimedSave)},
        {0x006dfa60, reinterpret_cast<const void *>(O_TimedLoad)},
        {0x006b86e0, reinterpret_cast<const void *>(O_Deactivate)},
        {0x006b91e0, reinterpret_cast<const void *>(O_MissionNumber)},
        {0x006b94b0, reinterpret_cast<const void *>(O_CurrentMission)},
        {0x006b6a20, reinterpret_cast<const void *>(O_Difficulty)},
        {0x006b6a60, reinterpret_cast<const void *>(O_DetailSettings)},
        {0x006b3700, reinterpret_cast<const void *>(O_DetailEnabled)},
        {0x00730230, reinterpret_cast<const void *>(O_CountTeams)},
        {0x0072f040, reinterpret_cast<const void *>(O_LockPlayers)},
        {0x0072f100, reinterpret_cast<const void *>(O_UnlockPlayers)},
        {0x00730e10, reinterpret_cast<const void *>(O_FlipParity)},
        {0x007319e0, reinterpret_cast<const void *>(O_ResetReplication)},
        {0x00730830, reinterpret_cast<const void *>(O_LocalHuman)},
        {0x00475d50, reinterpret_cast<const void *>(O_ActorById)},
        {0x00475c50, reinterpret_cast<const void *>(O_DestroyActor)},
        {0x00475940, reinterpret_cast<const void *>(O_FindSpawnPoint)},
        {0x00611c90, reinterpret_cast<const void *>(O_Spawn)},
        {0x006dcd10, reinterpret_cast<const void *>(O_GetGameApp)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x0072b590, reinterpret_cast<const void *>(O_PostCommand)},
        {0x0069ec40, reinterpret_cast<const void *>(O_RestoreMusic)},
        {0x0069c240, reinterpret_cast<const void *>(O_PrintMessage)},
        {0x006e5380, reinterpret_cast<const void *>(O_TextsGet)},
        {0x007e5ad2, reinterpret_cast<const void *>(O_Vsnprintf)},
        {0x0071e230, reinterpret_cast<const void *>(O_FreeSpeechSlots)},
        {0x0071e1e0, reinterpret_cast<const void *>(O_ClearSpeechSlots)},
        {0x0059ef80, reinterpret_cast<const void *>(O_GetSpeechSets)},
        {0x00796bf0, reinterpret_cast<const void *>(O_StainsReset)},
        {0x007e559c, reinterpret_cast<const void *>(O_AlignedFree)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(O_dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5392, reinterpret_cast<const void *>(O_dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    g_log.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("game_save_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_GAME_SAVE_QUICK") ? 20000 : 60000;
    if (const char *r = std::getenv("HD2_GAME_SAVE_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Game";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    std::printf("game_save_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
