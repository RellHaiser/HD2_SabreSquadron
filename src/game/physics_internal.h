// Helpers shared by physics.cpp, physics_door.cpp and physics_box.cpp.
#pragma once

#include <cmath>

#include "game/physics.h"

namespace physics {

// `point` moved by `m` with S_vector::operator*= (the original's choice in the boxes' and doors' steps: it sums in
// another order than operator*, and the box picks the end to test by comparing distances that are equal up to that).
inline S_vector MulAssigned(S_vector point, const S_matrix &m)
{
    point *= m;
    return point;
}

// |a - b| as the boxes' steps compute it inline (0x0040cf65, 0x0040d805): the differences left on the x87 stack
// with z on top, so the squares are summed z first. Their two ends are equally far from the contact, and which
// one is tested first comes down to this rounding.
inline float DistanceZYX(const S_vector &a, const S_vector &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    const float sq = (dz * dz + dy * dy) + dx * dx;
    if (sq < 1e-8f)
        return 0.0f;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(sq);
}

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
