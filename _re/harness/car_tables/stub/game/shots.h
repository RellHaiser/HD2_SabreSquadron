// Harness stub of game/shots.h: only the material table game/car_tables.cpp reads.
#pragma once

#include <cstdint>

struct S_MaterialDef {
    uint8_t unknown00[0xD8];
    float carFriction;                                      // +0xD8
    float carBounce;                                        // +0xDC
    float carUnknownE0;                                     // +0xE0
    float carUnknownE4;                                     // +0xE4
    float carUnknownE8;                                     // +0xE8
    float carUnknownEC;                                     // +0xEC
    float carUnknownF0;                                     // +0xF0
    int carDustEffect;                                      // +0xF4
    int carSkidEffect;                                      // +0xF8
    int carContactEffect;                                   // +0xFC
};
static_assert(sizeof(S_MaterialDef) == 0x100, "S_MaterialDef layout");

extern S_MaterialDef *g_materials;
extern uint32_t g_materialCount;
