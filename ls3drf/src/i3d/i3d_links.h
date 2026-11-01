// Parts of the I3D scene engine that IGraph calls into (implemented with the I3D modules).
#pragma once

#include <cstdint>

namespace ls3d::i3d {

// I3D_driver::GetStats(0) block (0x100adeb0, 8 dwords), cleared by IGraph::Init.
extern uint32_t g_renderStats[8];

// After a device reset: forgets the driver's cached states and sets them again (each of the 0x45
// states at 0x100be058 set to -1, then I3D_driver::SetState(i, old)).
void ReapplyDriverStates();
// Driver state 4 (0x100be068): w-buffer instead of z.
bool UseWBuffer();
// Marks the hardware lights for re-sending after a device reset (0x100647f0).
void InvalidateLights();
// Re-creates the particle emitters' shared buffers (0x1003ac00).
int InitParticleBuffers();

}  // namespace ls3d::i3d
