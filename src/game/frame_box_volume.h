// The collision box of an actor's frame, as the original builds it inline in the usable actors'
// StartMission (switches, photo places).
#pragma once

#include <cmath>
#include <cstdint>

#include "game/collision.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

class C_Actor;

// `volume` made a box (COLLISION_SHAPE_BOX of `type`) around `frame`'s hierarchy: the box in the
// frame's space under its world matrix (scale included), whose inverse is kept in `invMatrix`; the
// bounding sphere's radius scaled by the largest world scale. It is not added to the world.
inline void MakeFrameBoxVolume(S_CollisionVolume &volume, S_matrix &invMatrix, I3D_frame *frame, uint32_t type,
                               C_Actor *actor)
{
    I3D_bbox hierarchy;
    const I3D_bbox box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, frame);
    // Fix: the original leaves the centre unset for an empty box.
    S_vector center(0.0f, 0.0f, 0.0f);
    float radius = 0.0f;
    if (box.min.x <= box.max.x) {
        const S_vector half = (box.max - box.min) * 0.5f;
        center = box.min + half;
        radius = std::sqrt(half.z * half.z + half.y * half.y + half.x * half.x);
    }
    volume = S_CollisionVolume();
    volume.flags = COLLISION_SHAPE_BOX;
    volume.bbox = box;
    volume.matrix = &frame->GetMatrix();
    invMatrix.Inverse(frame->GetMatrix());
    volume.invMatrix = &invMatrix;
    volume.frame = frame;
    volume.type = type;
    const S_vector scale = frame->GetWorldScale();
    const float maxYZ = scale.y <= scale.z ? scale.z : scale.y;
    const float maxScale = scale.x <= maxYZ ? maxYZ : scale.x;
    volume.radius = maxScale * radius;
    volume.center = center * frame->GetMatrix();
    volume.centerW = frame->GetMatrix().m[0][3];        // (a stale local in the original: this)
    volume.actor = actor;
}
