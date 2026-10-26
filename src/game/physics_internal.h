// Helpers shared by physics.cpp, physics_door.cpp and physics_box.cpp.
#pragma once

#include "game/physics.h"

namespace physics {

// Stores `object` as the frame's physics object (creating the frame data if
// needed and `object` is not null).
void SetFramePhysicsObject(I3D_frame *frame, T_PhysicsObject object);
// Center and radius of the sphere around `bound.bbox` (radius 0 for an empty box).
void ComputeBoundSphere(I3D_bound_volume &bound);           // 0x00406c70
// Box around the frame and its children, with the sphere around it.
I3D_bound_volume HierarchyBound(I3D_frame *frame);
// The visual's own bound for visuals, else HierarchyBound().
I3D_bound_volume ModelBound(I3D_frame *frame);

} // namespace physics
