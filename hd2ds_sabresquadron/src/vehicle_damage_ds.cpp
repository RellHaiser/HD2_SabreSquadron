// The server's stand-ins for src/game/vehicle_damage.cpp, which HD2DS_SabreSquadron.exe does not have.
#include "game/vehicle.h"
#include <cmath>
#include <cstdlib>
#include "ls3d/i3d.h"

// Not in the server.
void C_Vehicle::CrackGlass(uint32_t index, const S_vector &velocity)          // 0x00500da0
{
}

// Not in the server.
void C_Vehicle::Deform(const S_vector4 &point, const S_vector4 &direction, float radius, float strength, uint8_t flags, const S_vector *push)                    // 0x004ff8b0
{
}

// Not in the server.
void C_Vehicle::ShowPartDamage(uint32_t index, const S_vector *push, bool detach)   // 0x004fefd0
{
}
