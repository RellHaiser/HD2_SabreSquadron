// Null actors (C_NullActor; original 0x005e2a10..0x005e2c00).
#include "game/null_actor.h"

void C_NullActor::RestartMission(bool)                  // 0x005e2ad0
{
    StopMission(true);
    StartMission(true);
}

bool C_NullActor::SaveGame(C_chunk &chunk)              // 0x005e2af0
{
    C_Actor::SaveGame(chunk);
    return true;
}

bool C_NullActor::LoadGame(C_chunk &chunk)              // 0x005e2b00
{
    C_Actor::LoadGame(chunk);
    return true;
}

// --- creator --------------------------------------------------------------------------------------

C_Actor *C_NullActorCreator::Create()                   // 0x005e2a40
{
    return new C_NullActor();                           // _aligned_malloc(0x90, 16) in the original
}

void C_NullActorCreator::Destroy(C_Actor *actor)        // 0x005e2ab0
{
    delete actor;
}

static C_NullActorCreator g_nullActorCreator;           // 0x008ac9c0

// Registers the creator (static initialiser 0x005e2a20, thunk 0x005e2a10).
static const bool g_nullActorCreatorRegistered = [] {
    g_actorCreators[ACTOR_NULL] = &g_nullActorCreator;
    return true;
}();
