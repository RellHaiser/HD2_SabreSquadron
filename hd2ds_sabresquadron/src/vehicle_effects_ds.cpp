// The server's stand-ins for src/game/vehicle_effects.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/vehicle.h"
#include <cstdlib>
#include <cstring>
#include "common/color.h"                               // RgbToHsv, HsvToRgb
#include "game/car_effects.h"
#include "game/game.h"                                  // GetGame
#include "game/pending.h"                               // g_carScene, g_carSurfaces
#include "ls3d/i3d.h"

// Not in the server.
bool C_Vehicle::StopEffect(int slot)                                           // 0x004ecfc0
{
    return false;
}

// Not in the server.
bool C_Vehicle::UpdateEngineSmoke()                                            // 0x00506f00
{
    return false;
}

// Not in the server.
void C_Vehicle::UpdateContactEffects()                                         // 0x00509540
{
}

// Not in the server.
void C_Vehicle::UpdateExhausts()                                               // 0x00508ee0
{
}

// Not in the server.
void C_Vehicle::UpdateWheelEffects()                                           // 0x00508350
{
}
