// The vehicles' tyre and surface tables (game/car_tables.cpp), built at the mission's start
// (C_game::Start) and dropped with the game.
#pragma once

#include <cstdint>
#include <vector>

// An entry of a tyre type's per-surface table (0x34 bytes; C_Vehicle::Start looks up the wheel's
// surface in it). The values mirror a wheel's own from +0x18C (S_VehicleWheel, which the solver
// takes for tyre index 0): +0x04 / +0x24 its unknown18C / unknown1C4, +0x08..+0x10 unknown194..19C,
// +0x14 unknown190, +0x18..+0x30 unknown1B8..1D0. tables\tyres.bin keeps them as 12 floats: +0x04
// and +0x24 in thousands, +0x08 / +0x0C in degrees.
struct S_TyreSurface {
    int surface;                                    // +0x00 (inferred: a ground surface kind)
    float unknown04;
    float unknown08;                                // radians
    float unknown0C;                                // radians
    float unknown10;
    float unknown14;
    float unknown18;
    float unknown1C;
    float unknown20;
    float unknown24;
    float unknown28;
    float unknown2C;
    float unknown30;
};
// A tyre type of tables\tyres.bin (0x28 bytes; the axles' parameters name it by id). The first is
// always "develop" (id 0, no table).
struct S_TyreType {
    int id;
    char name[0x20];                                // +0x04 ("offroad", "pasy", "LKW offroad")
    std::vector<S_TyreSurface> *surfaces;           // +0x24 (null: none)
};
extern std::vector<S_TyreType> g_tyreTypes;        // 0x0089c174 (data 0x0089c178)

// A material's values for the vehicles (0x30 bytes; InitCarSurfaces from the material table, one
// entry of defaults without materials). The material record's (S_MaterialDef) offsets.
struct S_CarSurface {
    int material;                                   // +0x00
    float unknown04;                                // +0x04 the smaller of the material's +0xE0 / +0xE4
    float unknown08;                                // +0x08 its +0xE0
    float unknown0C;                                // +0x0C the smaller of +0xE8 / +0xEC
    float unknown10;                                // +0x10 its +0xE8
    float unknown14;                                // +0x14 its +0xF0, 0..0.2
    float friction;                                 // +0x18 its +0xD8, 0..1 (default 0.35): the body's sliding friction
    float bounce;                                   // +0x1C its +0xDC, 1..2 (default 1): 1 + the body's restitution
    int dustEffect;                                 // +0x20 its +0xF4: a rolling wheel's car effect (UpdateWheelEffects)
    int skidEffect;                                 // +0x24 its +0xF8: a skidding wheel's
    int contactEffect;                              // +0x28 its +0xFC: the body contacts' (its absolute value; UpdateContactEffects)
    // +0x2C set for materials 0x30, 0x33..0x37, 0x40, 0x41, 0x5F: the tyres squeal on it
    // (C_Vehicle::UpdateTyreSqueal).
    uint8_t tyreSqueal;
    uint8_t skidMarks;                              // +0x2D (the same materials: roads, rock, concrete, paving) the wheels leave skid marks
};
extern std::vector<S_CarSurface> g_carSurfaces;     // 0x0089c160 (data 0x0089c164)
extern int g_carSurfaceOfMaterial[256];             // 0x0089bd60 a material's entry

// tables\tyres.bin into g_tyreTypes (after "develop"; false: no file, another version, no memory:
// "develop" alone then).
bool LoadTyres();                                   // 0x004d6900
void FreeTyres();                                   // 0x004eccd0
// g_carSurfaces from the material table (true).
bool InitCarSurfaces();                             // 0x004d57a0
void ClearCarSurfaces();                            // 0x004ecde0
