// The server's stand-ins for src/game/car_effects.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/car_effects.h"
#include <cstring>
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// The server's copy only returns true (DS 0x004c3530): C_game::Start's call, no emitters made.
bool InitCarEffects()                                                         // 0x004d5a50
{
    return true;
}

// Not in the server.
bool __fastcall SetupCarEmitter(S_CarEmitter *emitter, uint32_t effect)        // 0x004d5b10
{
    return {};
}

// Not in the server.
int __fastcall ReuseCarEmitter(int slot, uint32_t effect)                      // 0x004d62c0
{
    return {};
}

S_CarEffectDef *g_carEffects;                           // (LoadMaterialTables fills them)
uint32_t g_carEffectCount;
// Not in the server.
void FreeCarEffectEmitters()                                                   // 0x004ed0c0
{
}

// Not in the server.
void UpdateCarEffectEmitters(int timeDelta)                                    // 0x004ed130
{
}
