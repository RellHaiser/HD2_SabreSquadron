// The actor lists: C_ActorsModule ("Actors") holds the mission's actors,
// C_TemporaryActorsList ("Temp. Actors") the ones created during play (dropped
// items, mines, debris, breakable fences).
//
// Both keep an active subset that gets Update/Tick; changes of the active set
// requested while it is being iterated are queued and applied afterwards.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/items_module.h"
#include "game/module.h"

class C_BitStream;
class C_Debris;
class C_Human;
class C_Item;
class C_PhysDoor;
struct S_CollisionVolume;

// Actor removed from play, destroyed when its delay runs out.
struct S_RemovedActor {                                 // 0x08 bytes
    C_Actor *actor;
    int timeLeft;                                       // ms
};

class C_ActorsModule : public C_Module {                // "Actors", vtable 0x008100a8
public:
    // Slots below PLAYER_SLOTS are reserved for the players (may be null).
    static constexpr int PLAYER_SLOTS = 32;
    // Delay before an actor removed with DestroyActor(actor, false) is deleted.
    static constexpr int REMOVAL_DELAY = 20000;

    C_ActorsModule(int id, const char *name);                       // 0x00475690
    ~C_ActorsModule() override;                                     // 0x00475790

    void Tick(int timeDelta) override;                              // 0x00476080
    void Update(int frameTime) override;                            // 0x00476160
    void FreeMissionData(bool server) override { Clear(); }         // 0x00475760
    void StartMission(bool server) override;                        // 0x00476210
    void StopMission(bool server) override;                         // 0x00476260
    bool OnMessage(C_BitStream *msg) override;                      // 0x00475630
    bool Unknown28(C_BitStream *msg) override;                      // 0x004755d0
    void RestartMission(bool server) override;                      // 0x004762f0
    bool SaveGame(C_chunk &chunk) override;                         // 0x00476370
    bool LoadGame(C_chunk &chunk) override;                         // 0x004764d0
    void WriteState(C_BitStream *msg) override;                     // 0x004769d0
    void ReadState(C_BitStream *msg) override;                      // 0x00476a10

    // Creates an actor of `type` bound to `frame` (may be null). `slot` reuses a
    // player slot (the previous occupant is destroyed); -1 appends.
    C_Actor *CreateActor(E_ActorType type, I3D_frame *frame, bool active, int slot);   // 0x004759c0
    // Removes the actor; it is deleted now or after REMOVAL_DELAY.
    void DestroyActor(C_Actor *actor, bool immediately);            // 0x00475c50
    void Clear();                                                   // 0x00475fb0

    int GetActorCount() const { return static_cast<int>(m_actors.size()); }
    // The actors (the players' slots first; entries may be null).
    const std::vector<C_Actor *> &GetActors() const { return m_actors; }
    C_Actor *GetActor(int index) const;                             // 0x00475dd0
    // Like GetActor(), but an empty player slot finds the removed actor that had it.
    C_Actor *GetActorById(int id) const;                            // 0x00475d50
    // Human with this id, or the removed actor that had it.
    C_Actor *GetHuman(int id) const;                                // 0x00475e00
    void SetActorActive(C_Actor *actor, bool active);               // 0x00475ec0
    int FindFreePlayerSlot() const;                                 // 0x004759a0
    // How often player slot `slot` was reused (the byte as signed; -1 outside the slots).
    int GetPlayerSlotUse(int slot) const                            // 0x004754f0
    {
        return slot >= 0 && slot < PLAYER_SLOTS ? static_cast<int8_t>(m_playerSlotUse[slot]) : -1;
    }
    void SetPlayerSlotUse(int slot, int use)                        // 0x00475510
    {
        if (slot >= 0 && slot < PLAYER_SLOTS)
            m_playerSlotUse[slot] = static_cast<uint8_t>(use);
    }
    // An enabled spawn point of `team`; FindRandomSpawnPoint also matches `kind`
    // (spawn points of kind 2 accept any).
    C_Actor *FindSpawnPoint(int team) const;                        // 0x00475940
    C_Actor *FindRandomSpawnPoint(int team, int kind) const;        // 0x00475840

    // Enemies counted in single player / co-op (CountEnemies()).
    int GetLiveEnemyCount() const { return m_liveEnemies; }
    int GetCarnageEnemyCount() const { return m_carnageEnemies; }
    C_Human *GetCarnageEnemy() const { return m_carnageEnemy; }

private:
    void ScheduleRemoval(C_Actor *actor, int delay);                // 0x00476680
    void TickRemovals(int timeDelta);                               // 0x00476860
    void ApplyPendingActivations();
    void CountEnemies();                                            // 0x00476a50

    std::vector<C_Actor *> m_actors;                    // +0x0C
    std::vector<C_Actor *> m_active;                    // +0x1C
    std::vector<C_Actor *> m_pending;                   // +0x2C
    std::vector<S_RemovedActor> m_removed;              // +0x3C
    int m_liveEnemies = 0;                              // +0x4C
    int m_carnageEnemies = 0;                           // +0x50
    C_Human *m_carnageEnemy = nullptr;                  // +0x54 set when there is exactly one
    bool m_iterating = false;                           // +0x58
    uint8_t m_playerSlotUse[PLAYER_SLOTS];              // +0x59 times a slot was reused (0xFF = unused)
};
extern C_ActorsModule g_actorsModule;                   // 0x0089b4b0

// Breakable fence (physics door) that vehicles break when driving into it.
struct S_BreakableObject {                              // 0x0C bytes
    I3D_frame *frame;
    C_PhysDoor *door;
    S_CollisionVolume *volume;
};


class C_TemporaryActorsList : public C_Module {         // "Temp. Actors", vtable 0x008100e8
public:
    C_TemporaryActorsList(int id, const char *name);                // 0x00476ec0
    ~C_TemporaryActorsList() override;                              // 0x00476fe0

    void Tick(int timeDelta) override;                              // 0x004774c0
    void Update(int frameTime) override;                            // 0x00477590
    void FreeMissionData(bool server) override { Clear(); }         // 0x00476fb0
    void StartMission(bool server) override;                        // 0x00477660
    void StopMission(bool server) override;                         // 0x00477a90
    bool OnMessage(C_BitStream *msg) override;                      // 0x004788e0
    bool Unknown28(C_BitStream *msg) override;                      // 0x004788a0
    void RestartMission(bool server) override;                      // 0x00477b00
    bool SaveGame(C_chunk &chunk) override;                         // 0x00477b90
    bool LoadGame(C_chunk &chunk) override;                         // 0x00477f50
    void WriteState(C_BitStream *msg) override;                     // 0x004782c0
    void ReadState(C_BitStream *msg) override;                      // 0x00478520

    // Creates a temporary actor of `type` on `frame`; id < 0 takes the next free id.
    C_Actor *CreateActor(E_ActorType type, I3D_frame *frame, int id = -1);    // 0x004770b0
    void DestroyActor(C_Actor *actor);                              // 0x00477190
    // The mines `owner` laid go (a player left the game).
    void DestroyMinesOf(C_Human *owner);                            // 0x00479700
    void SetActorActive(C_Actor *actor, bool active);               // 0x00477330
    C_Actor *FindActor(int id) const;                               // 0x004772f0
    void Clear();                                                   // 0x00477430
    const std::vector<C_Actor *> &GetActors() const { return m_actors; }

    // Physical copy of `frame` (FRM_CreatePhysicalObject).
    C_Debris *CreateDebris(I3D_frame *frame, const S_vector &pos, const S_vector &dir);  // 0x00479450
    // A mine of item `itemId` at `pos` facing `dir`, laid by `owner` (may be null);
    // the owner loses the item and, if `send`, clients are told.
    void CreateMine(C_Human *owner, int itemId, const S_vector &pos, const S_vector &dir, bool send);   // 0x00478b90
    // Client side of an item a human dropped: the actor the server announced.
    // `exactDir` (if given) replaces the normalized `dir` as the item's facing.
    C_Actor *ClientCreateItem(int id, int ownerId, int itemType, int itemUid, const S_vector &pos,
                              const S_vector &dir, const S_vector *exactDir);   // 0x004790c0
    // Moves item `itemUid` from `human`'s stores to the world; returns its actor id.
    int DropItem(C_Human *human, int itemUid, const S_vector &pos, const S_vector &dir, bool unknown);   // 0x00478ee0

    // Is an item `itemId` lying within `range` of `pos`?
    bool IsItemInRange(int itemId, const S_vector &pos, int range) const;   // 0x00478a20
    bool IsMineInRange(const S_vector &pos, int range) const;      // 0x00478ae0
    // An item lying within `range` of `pos` of the pick-up types 3, 4, 5, 6 or 12.
    C_Actor *FindItemNear(const S_vector &pos, float range) const;  // 0x00479600

    // The mission's pick-ups' placements (multiplayer): a dropped one goes back to its own.
    const std::vector<S_ItemPlacement> &GetRespawnItems() const { return m_respawnItems; }

    void AddBreakable(const S_BreakableObject &object);             // 0x00479540
    void ClearBreakables();                                         // 0x004795e0

private:
    // Client side: actors the server announced.
    C_Actor *ClientCreateMine(int id, int ownerId, int itemId, const S_vector &pos, const S_vector &dir);   // 0x00478d30
    C_Actor *LoadMine(int itemId, int id);                          // 0x00478e50
    void SendCreateMine(int id, int ownerId, int itemId, const S_vector &pos, const S_vector &dir);   // 0x00478cc0
    void CreateBreakables();                                        // 0x004794d0
    void CreateBreakablesOnClient();                                // 0x00479530
    void ApplyPendingChanges();

    std::vector<C_Actor *> m_actors;                    // +0x0C
    std::vector<C_Actor *> m_active;                    // +0x1C
    std::vector<C_Actor *> m_pending;                   // +0x2C activation changes while iterating
    std::vector<C_Actor *> m_toDestroy;                 // +0x3C destruction requests while iterating
    std::vector<S_BreakableObject> m_breakables;        // +0x4C (aligned array, grows by 10)
    std::vector<S_ItemPlacement> m_respawnItems;        // +0x5C (aligned array, grows by 2)
    bool m_iterating = false;                           // +0x6C
    int m_nextId = 0;                                   // +0x70
    int m_debrisCount = 0;                              // +0x74
};
extern C_TemporaryActorsList g_temporaryActors;         // 0x0089b5d0

// Actor network messages (module channel 0xD): the actor's id (10 bits), then its payload;
// the Ex variant is C_NetGame::CreateMessageEx's.
C_BitStream *CreateActorMessage(int id, bool reliable);             // 0x00475530
C_BitStream *CreateActorMessageEx(int id, bool reliable);           // 0x00475570
void SendActorMessage(C_BitStream *msg);                            // 0x004755b0

// Temporary-list network messages (module channel MODULE_TEMP_ACTORS): a command
// (0 = actor message, 1 = create) or an actor id, then the payload.
C_BitStream *CreateTempActorMessage(int id, bool reliable);         // 0x00478780
C_BitStream *CreateTempActorCommand(int command, bool reliable);    // 0x004787e0
C_BitStream *CreateTempActorMessage2(int id, bool reliable);        // 0x00478840
void SendTempActorMessage(C_BitStream *msg);                        // 0x00478880
