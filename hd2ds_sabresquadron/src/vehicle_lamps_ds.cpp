// The server's stand-ins for src/game/vehicle_lamps.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/vehicle.h"
#include <cmath>
#include "ls3d/i3d.h"

// Not in the server.
void C_Vehicle::UpdateLamps(float timeDelta)                                    // 0x00501cb0
{
}

// Not in the server.
void C_Vehicle::SetBeamScale(uint32_t beams, const S_vector &scale)              // 0x004f4e40
{
}
