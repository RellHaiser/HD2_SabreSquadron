// A collision box that follows a frame, with the inverse of the frame's world matrix next to it: the
// submarine's hull boxes (0x005df710) and the train's wagons (inline in its StartMission) are these
// 0xA0-byte blocks (_aligned_malloc'd).
#pragma once

#include <cmath>
#include <cstdint>

#include "game/collision.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

class C_Actor;

struct alignas(16) S_FrameVolume {
    S_matrix invMatrix;                                 // +0x00 the frame's (the volume's invMatrix)
    S_CollisionVolume volume;                           // +0x40

    // The box around `frame` (a dummy: its own box, the radius half its height; else its
    // hierarchy's), under the frame's world matrix; the radius scaled by the largest world scale.
    // Added to the world and moved there.
    void Init(I3D_frame *frame, uint32_t flags, uint32_t type, C_Actor *actor)   // 0x005df710
    {
        volume = S_CollisionVolume();
        if (!frame)
            return;
        I3D_bbox box;
        // Fix: an empty hierarchy's centre is 0 (the original leaves it unset).
        S_vector center(0.0f, 0.0f, 0.0f);
        float radius = 0.0f;
        if (frame->GetType() == FRAME_DUMMY) {
            box = static_cast<I3D_dummy *>(frame)->GetBBox();
            center = S_vector((box.max.x + box.min.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.max.z + box.min.z) * 0.5f);
            radius = (box.max.y - box.min.y) * 0.5f;
        } else {
            I3D_bbox hierarchy;
            box = *I3DGetDriver()->GetHierarchyBBox(hierarchy, frame);
            if (box.min.x <= box.max.x) {
                const S_vector half = (box.max - box.min) * 0.5f;
                center = box.min + half;
                radius = std::sqrt((half.z * half.z + half.y * half.y) + half.x * half.x);
            }
        }
        volume.flags = flags;
        const S_matrix &world = frame->GetMatrix();
        volume.bbox = box;
        volume.matrix = &world;
        invMatrix.Inverse(world);
        volume.invMatrix = &invMatrix;
        volume.frame = frame;
        volume.type = type;
        volume.actor = actor;
        const S_vector scale = frame->GetWorldScale();
        const float maxYZ = scale.y <= scale.z ? scale.z : scale.y;
        const float maxScale = scale.x <= maxYZ ? maxYZ : scale.x;
        volume.radius = maxScale * radius;
        // (The centre's w: a stale stack value in the original.)
        volume.center = center * world;
        g_collisionWorld.AddVolume(&volume);
        g_collisionWorld.MoveVolume(&volume);
    }
};
