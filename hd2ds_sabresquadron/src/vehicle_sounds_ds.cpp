// The server's stand-ins for src/game/vehicle_sounds.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/vehicle.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "game/pending.h"                               // g_carScene, g_carSurfaces
#include "game/resource_cache.h"                        // g_soundCache
#include "ls3d/i3d.h"

// Not in the server.
void C_Vehicle::AddSoundsToSectors()                                            // 0x00517cf0
{
}

// Not in the server.
void C_Vehicle::PlayBumpSound(float volume)                                     // 0x0051b560
{
}

// Not in the server.
void C_Vehicle::PlayDoorSound(int seat, bool opening)                           // 0x0051b660
{
}

// Not in the server.
void C_Vehicle::PlayEngineFail()                                                // 0x0051a620
{
}

// Not in the server.
void C_Vehicle::PlayGearSound()                                                 // 0x0051a480
{
}

// Not in the server.
void C_Vehicle::PlayHandbrakeSound()                                            // 0x0051a580
{
}

// Not in the server.
void C_Vehicle::PlayImpactSound(float impulse, bool unknown)                    // 0x0051a950
{
}

// Not in the server.
void C_Vehicle::PlayWheelBurst(int wheel)                                       // 0x0051a880
{
}

// Not in the server.
void C_Vehicle::ReleaseSounds()                                                 // 0x0051c180
{
}

// Not in the server.
void C_Vehicle::RemoveSoundsFromSectors()                                       // 0x005181c0
{
}

// Not in the server.
void C_Vehicle::StopEngineLoops()                                               // 0x0051b7c0
{
}

// Not in the server.
void C_Vehicle::StopSounds()                                                    // 0x0051b8f0
{
}

// Not in the server.
void C_Vehicle::SwitchEngine(bool on)                                           // 0x00518710
{
}

// Not in the server.
void C_Vehicle::SwitchLoopSoundA(bool on)                                       // 0x0051a6c0
{
}

// Not in the server.
void C_Vehicle::SwitchLoopSoundB(bool on)                                       // 0x0051a7a0
{
}

// Not in the server.
void C_Vehicle::UpdateEngineSound(float timeDelta)                              // 0x00518ab0
{
}

// Not in the server.
void C_Vehicle::UpdateRimSound(bool on)                                         // 0x00519dc0
{
}

// Not in the server.
void C_Vehicle::UpdateRollingSound()                                            // 0x00519a60
{
}

// Not in the server.
void C_Vehicle::UpdateTyreSqueal(float timeDelta)                               // 0x0051a130
{
}

// Not in the server.
bool C_Vehicle::LoadSounds(const S_VehicleParams &params)                      // 0x00517370
{
    return true;
}
