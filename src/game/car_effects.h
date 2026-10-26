// The vehicles' particle effects: the definitions of tables\effects_car.def (LoadMaterialTables
// 0x0071fd80 reads them; C_game::Stop frees them) and the pool of emitters the vehicles take them
// on (made by InitCarEffects at C_game::Start, run by C_game::Update, freed by C_game::Stop).
#pragma once

#include <cstdint>
#include <vector>

#include "ls3d/math.h"

class I3D_particle_visual;

// An effect (0xF0 bytes; index 0 is an empty one). The emitter properties they are set as are
// given (I3D_particle_visual::SetRenderState; their meanings not identified).
struct S_CarEffectDef {
    char texture[32];                                   // +0x00 the particles' texture file
    char alphaTexture[32];                              // +0x20 its alpha file (from 4 characters on)
    float color1[4];                                    // +0x40 property 0x1D (the vehicles dim it by the light)
    float color2[4];                                    // +0x50 property 0x1E (the same)
    float rect[4];                                      // +0x60 the texture rectangle (its emitter key is selected)
    int unknown70;                                      // +0x70 property 0
    int unknown74;                                      // +0x74 property 1
    int unknown78;                                      // +0x78 property 2
    int unknown7C;                                      // +0x7C property 3
    float unknown80;                                    // +0x80 property 4
    float unknown84;                                    // +0x84 property 5
    float unknown88;                                    // +0x88 property 6
    float unknown8C;                                    // +0x8C property 7 (truncated to an int)
    int unknown90;                                      // +0x90 property 8
    int unknown94;                                      // +0x94 property 9
    uint32_t flags;                                     // +0x98 property 0x13 (the blend mode) its bits 0x107
    uint32_t unknown9C;
    float unknownA0;                                    // +0xA0 property 0xA
    float unknownA4;                                    // +0xA4 property 0xB
    float unknownA8;                                    // +0xA8 property 0xC
    float unknownAC;                                    // +0xAC property 0x22 (SetupCarEmitter only)
    S_vector velocity;                                  // +0xB0 properties 0xD..0xF (the vehicles add the carried velocity)
    S_vector velocity2;                                 // +0xBC properties 0x10..0x12 (the vehicles add a share of it)
    float unknownC8;                                    // +0xC8 property 0x18
    float unknownCC;                                    // +0xCC property 0x19
    uint32_t unknownD0;
    float unknownD4;                                    // +0xD4 property 0x1A
    uint32_t unknownD8;
    uint8_t unknownDC[0x14];
};
static_assert(sizeof(S_CarEffectDef) == 0xF0, "S_CarEffectDef layout");
extern S_CarEffectDef *g_carEffects;                    // 0x0097b9ac
extern uint32_t g_carEffectCount;                       // 0x0097b9b0

// An emitter of the pool (0x30 bytes). A vehicle takes one by its slot (C_Vehicle::StartEffect)
// and lets it go (StopEffect: it stops emitting and is free once its particles are gone).
struct S_CarEmitter {
    S_vector4 velocity;                                 // +0x00 its effect's velocity (w not set)
    S_vector4 velocity2;                                // +0x10 its effect's velocity2 (w not set)
    I3D_particle_visual *visual;                        // +0x20 an I3DVISUAL_EMITTER named "Car_particle"
    int effect;                                         // +0x24 its effect (g_carEffects)
    bool active;                                        // +0x28 taken (run by UpdateCarEffectEmitters)
    bool stopping;                                      // +0x29 let go: no new particles
};
// The pool (0x0089bd50: data, capacity +4, count +8, grown by +0xC = 2; 30 emitters).
extern std::vector<S_CarEmitter> g_carEmitters;

bool InitCarEffects();                                  // 0x004d5a50
void FreeCarEffectEmitters();                           // 0x004ed0c0
void UpdateCarEffectEmitters(int timeDelta);            // 0x004ed130 ms
// The car's effect callbacks of its vehicle (C_Car::SetFrame; VEHICLE_CALLBACK_SETUP_EMITTER /
// _REUSE_EMITTER): an emitter set up for an effect (its texture loaded), or a slot whose emitter
// has the effect's texture switched to the effect in place (-1: not that texture).
bool __fastcall SetupCarEmitter(S_CarEmitter *emitter, uint32_t effect);   // 0x004d5b10
int __fastcall ReuseCarEmitter(int slot, uint32_t effect);                 // 0x004d62c0
