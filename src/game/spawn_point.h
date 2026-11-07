// A spawn point (C_SpawnPoint, actor type 10, vtable 0x008138e0, 0xB0 bytes; original
// 0x00611730..0x006131d0): where soldiers appear. Single player: the mission squad, each at a free
// point of its team (used once). Multiplayer: the server spawns a player here (or at a random free
// spawn point / a zone's spawn position, by the game type) and tells the clients, who make the
// soldier from the message.
#pragma once

#include <cstdint>

#include "game/actor.h"

class C_Human;
struct S_SoldierState;

class alignas(16) C_SpawnPoint : public C_Actor {       // "SpawnPoint", vtable 0x008138e0
    friend class C_GameApp;                             // the game commands (net/net_commands.cpp)
public:
    C_SpawnPoint() : C_Actor(ACTOR_SPAWN_POINT) {}      // 0x006118d0
    ~C_SpawnPoint() override = default;                 // 0x00611940 (scalar deleting 0x00611920)

    // The server spawns the soldier of player `slot` with `state` (the network players' `stats`;
    // null in single player). The state is an S_SquadSoldier (the squad's or the network's):
    // without a uniform it gets the point's, its stacks are split.
    void Spawn(int slot, S_SoldierState *state, int *stats);   // 0x00611c90
    C_Actor *GetSpawnedActor() const { return m_spawned; }     // +0xA8 (the last one)
    int GetTeam() const { return m_team; }              // +0x90
    // Which spawns it accepts; 2 = any (C_ActorsModule::FindRandomSpawnPoint).
    int GetKind() const { return m_kind; }              // +0x94
    // Free to spawn at (single player: until a soldier is spawned here).
    bool IsEnabled() const { return m_free; }           // +0x9C

    // A client: a soldier spawned (its place, slots, the player's name and connection, the
    // soldier state and, in multiplayer, the stats).
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x006125a0
    // (SetFrame 0x00611950 is the base's.)
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x00611960
    // Chunk 0 (version 3) first, then 1 team, 2 kind, 3 uniform, 4 (+0x98), 5 NPC.
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x006119b0
    void StartMission(bool server) override;            // 0x3C 0x00611970
    void StopMission(bool server) override;             // 0x40 0x00611990
    // Used and spawned: the frame's script told again.
    void RestartMission(bool server) override;          // 0x44 0x006117f0
    bool SaveGame(C_chunk &) override { return true; }  // 0x48 0x006118b0 (not the base's)
    bool LoadGame(C_chunk &) override { return true; }  // 0x4C 0x006118c0

private:
    // The spawned soldier set up: made on a client, found on the server; the local player's camera
    // and squad, the player's team, spectating and VIP flags.
    void SetupSpawned(int slot, int actorSlot, const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state,
                      const int *stats);                // 0x006123a0
    // A human for player `slot` in actor slot `actorSlot`: its model ("NA01", or the uniform's)
    // through the model cache, placed in the primary sector, named "_PLAYER_<actorSlot>", set up
    // from `state` (role NPC team when `npc`, else player). Null when the model does not load.
    static C_Human *CreateSoldier(int slot, int actorSlot, const S_vector4 &pos, const S_vector4 &dir,
                                  S_SoldierState *state, const int *stats, int uniform, bool npc);   // 0x00612a60 (__fastcall)

    int m_team = 0;                                     // +0x90
    int m_kind = 0;                                     // +0x94
    int m_unknown98 = 0;                                // +0x98 (property chunk 4; not used here)
    bool m_free = true;                                 // +0x9C
    int m_npc = 0;                                      // +0xA0 the soldiers are allied NPCs, not players
    int m_uniform = 0;                                  // +0xA4 the uniform item (single player)
    C_Actor *m_spawned = nullptr;                       // +0xA8
};

// The spawn points' creator (g_actorCreators[ACTOR_SPAWN_POINT], vtable 0x008138d0).
class C_SpawnPointCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x00611760
    const char *GetName() override { return "SpawnPoint"; }   // 0x006117c0
    void Destroy(C_Actor *actor) override;              // 0x006117d0
};
