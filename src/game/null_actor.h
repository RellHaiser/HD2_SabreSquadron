// An actor that does nothing (C_NullActor, actor type 33, vtable 0x00812840, 0x90 bytes; original
// 0x005e2a10..0x005e2c00): a frame of the scene that the scripts and the vehicles' callbacks can
// treat as an actor.
#pragma once

#include "game/actor.h"

class alignas(16) C_NullActor : public C_Actor {        // "NullActor", vtable 0x00812840
public:
    C_NullActor() : C_Actor(ACTOR_NULL) {}              // 0x005e2b10
    ~C_NullActor() override = default;                  // 0x005e2b50 (scalar deleting 0x005e2b30)

    // (SetFrame 0x005e2b60 is the base's; WriteProperties 0x005e2bb0 and ReadProperties 0x005e2bc0
    // return true, as the base's do.)
    void CopyFrom(const C_Actor *source) override       // 0x38 0x005e2b70
    {
        if (source)
            C_Actor::CopyFrom(source);
    }
    void StartMission(bool server) override { Activate(server); }    // 0x3C 0x005e2b90 (DS 0x00600f70)
    void StopMission(bool server) override { Deactivate(server); }   // 0x40 0x005e2ba0 (DS 0x00600f90)
    void RestartMission(bool server) override;          // 0x44 0x005e2ad0
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005e2af0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005e2b00
};

// The null actors' creator (g_actorCreators[ACTOR_NULL], vtable 0x00812830).
class C_NullActorCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005e2a40
    const char *GetName() override { return "NullActor"; }   // 0x005e2aa0
    void Destroy(C_Actor *actor) override;              // 0x005e2ab0
};
