// Harness stub of game/actors_module.h (the real one is too wide): the actor messages the car sends
// (game/car_update.cpp) and the debris its callbacks throw (game/car_callbacks.cpp), as the real
// header declares them; the temporary list's part the debris and item actors use (game/debris.cpp,
// game/item_actor.cpp; part 6a).
#pragma once

#include <vector>

#include "game/actor.h"                                 // E_ActorType
#include "game/items_module.h"                          // S_ItemPlacement

class C_Actor;
class C_BitStream;

C_BitStream *CreateActorMessage(int id, bool reliable);
C_BitStream *CreateActorMessageEx(int id, bool reliable);
void SendActorMessage(C_BitStream *msg);

class C_Debris;
class I3D_frame;
struct S_vector;
class C_TemporaryActorsList {
public:
    C_Debris *CreateDebris(I3D_frame *frame, const S_vector &pos, const S_vector &dir);
    void DestroyActor(C_Actor *actor);
    const std::vector<S_ItemPlacement> &GetRespawnItems() const { return m_respawnItems; }

    std::vector<S_ItemPlacement> m_respawnItems;
};
extern C_TemporaryActorsList g_temporaryActors;

C_BitStream *CreateTempActorMessage(int id, bool reliable);
void SendTempActorMessage(C_BitStream *msg);

// The actors by index and by id (game/car_hits.cpp).
class C_ActorsModule {
public:
    static constexpr int PLAYER_SLOTS = 32;
    C_Actor *GetActor(int index) const;
    C_Actor *GetActorById(int id) const;
    // (Part 7b: the mine, the document place, the spawn point.)
    C_Actor *GetHuman(int id) const;
    int FindFreePlayerSlot() const;
    C_Actor *FindRandomSpawnPoint(int team, int kind) const;
    C_Actor *CreateActor(E_ActorType type, I3D_frame *frame, bool active, int slot);
    void DestroyActor(C_Actor *actor, bool immediately);   // (scene2.bin's actors, part 8f-iii)
    int GetPlayerSlotUse(int slot) const;
    void SetPlayerSlotUse(int slot, int use);
    // (Part 7c: the fowl generator; the first 32 are the players'.)
    const std::vector<C_Actor *> &GetActors() const;
};
extern C_ActorsModule g_actorsModule;
