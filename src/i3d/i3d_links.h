// The part of the I3D scene engine that IGraph touches (implemented with the I3D modules).
#pragma once

#include <cstdint>

namespace ls3d::i3d {

// I3D_driver::GetStats(0) block (0x100adeb0, 8 dwords), cleared by IGraph::Init.
extern uint32_t g_renderStats[8];

}  // namespace ls3d::i3d
