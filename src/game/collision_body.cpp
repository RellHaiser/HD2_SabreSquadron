// Collision world, part 4 (original source 0x0071ba20..0x0071e120): the body sweeps. A body of
// spheres (a human's stance spheres) is moved by one move: each step every sphere is swept
// (C_CollisionWorld::SphereTest), all are moved to where the nearest touches, and the rest of
// the move is projected onto the plane of that contact (slid along it); the contacts are
// collected into the S_MoveContacts (at most g_moveIterations of them).
//
// (0x007189c0, 0x007194a0, 0x0071a080 and 0x0071af10, earlier variants of these sweeps, have no
// callers and are not ported.)
#include <cfloat>
#include <cmath>

#include "game/collision.h"
#include "game/collision_internal.h"
#include "game/human.h"
#include "game/human_controller.h"

namespace {

S_vector4 ToVector4(const S_vector &v, float w) { return {v.x, v.y, v.z, w}; }

// The rest of `move` from `from` slid along the contact plane through `hitPos` with `normal`,
// pushed off the plane by `push`.
S_vector SlideAlong(const S_vector &from, const S_vector &move, const S_vector &hitPos, const S_vector &normal, float push)
{
    const float d = -hitPos.Dot(normal);
    const S_vector end = from + move;
    const float denom = normal.Dot(normal);
    S_vector projected = end;
    if (!(std::fabs(denom) < 1e-8f)) {
        const float t = -((normal.Dot(end) + d) / denom);
        projected = end + normal * t;
    }
    return (projected - hitPos) + normal * push;
}

// The contacts of a step: what every sphere touches (`hitPos` its center there).
struct S_StepHits {
    S_vector pos[MOVE_CONTACTS_MAX];
    S_vector4 normal[MOVE_CONTACTS_MAX];
};

// A steep normal (y between -0.3 and `top`) taken as a wall: made horizontal.
bool FlattenWall(S_vector4 &normal, float top)
{
    if (!(-0.3f < normal.y && normal.y < top))
        return false;
    normal.y = 0.0f;
    const S_vector flat = S_vector(normal.x, 0.0f, normal.z).Normalized();
    normal.x = flat.x;
    normal.y = flat.y;
    normal.z = flat.z;
    return true;
}

}  // namespace

S_CollisionVolume *C_CollisionWorld::MoveSpheres(S_MoveContacts &contacts) const
{
    const uint32_t count = contacts.sphereCount;
    S_vector4 spheres[MOVE_CONTACTS_MAX] = {};
    for (uint32_t i = 0; i < count; ++i)
        spheres[i] = contacts.spheres[i];
    S_vector move = ToVector(contacts.move);
    S_CollisionVolume *last = nullptr;
    int touched = 0;
    S_StepHits hits = {};
    int nearest;
    do {
        if (g_moveIterations <= static_cast<float>(static_cast<uint32_t>(touched)))
            break;
        nearest = -1;
        float best = FLT_MAX;
        S_vector step = move;
        if (count != 0) {
            for (uint32_t i = 0; i < count; ++i) {
                S_vector normal;
                S_CollisionVolume *hit = SphereTest(ToVector(spheres[i]), move, contacts.radii[i], &hits.pos[i], &normal,
                                                    contacts.excludeTypes, contacts.excludeFlags);
                if (hit) {
                    hits.normal[i] = ToVector4(normal, hits.normal[i].w);
                    if (g_sphereTestDistanceSq < best) {
                        best = g_sphereTestDistanceSq;
                        nearest = static_cast<int>(i);
                        last = hit;
                    }
                }
            }
            if (nearest >= 0) {
                const S_vector &hitPos = hits.pos[nearest];
                const S_vector4 &normal = hits.normal[nearest];
                step = hitPos - ToVector(spheres[nearest]);
                move = SlideAlong(ToVector(spheres[nearest]), move, hitPos, ToVector(normal), 0.01f);
                contacts.normals[touched] = normal;
                contacts.sphere[touched] = nearest;
                contacts.volumes[touched] = last;
                ++touched;
            }
        }
        for (uint32_t i = 0; i < count; ++i) {
            spheres[i].x += step.x;
            spheres[i].y += step.y;
            spheres[i].z += step.z;
        }
    } while (nearest >= 0);
    contacts.pos = spheres[0];
    contacts.count = touched;
    return last;
}

S_CollisionVolume *C_CollisionWorld::MoveSpheresStepping(S_MoveContacts &contacts) const
{
    const uint32_t count = contacts.sphereCount;
    // How far ahead a wall stops the step up (from the first sphere).
    const float reach = contacts.radii[0] + 0.75f;
    const float moveY = contacts.move.y;
    const int most = static_cast<int>(g_moveIterations);
    int touched = 0;
    S_CollisionVolume *last = nullptr;                      // what the nearest sphere touched last
    uint32_t lastType = 0;
    bool lastDynamic = false;
    S_CollisionVolume *lyingHuman = nullptr;                // the first lying human stepped onto
    // The spheres lifted for the step up.
    float lift = 0.3f;
    bool lifted = true;
    S_vector4 spheres[MOVE_CONTACTS_MAX] = {};
    for (uint32_t i = 0; i < count; ++i)
        spheres[i] = {contacts.spheres[i].x, contacts.spheres[i].y + 0.3f, contacts.spheres[i].z, 0.0f};
    const auto lower = [&]() {
        // No step up: only just above the ground.
        for (uint32_t i = 0; i < count; ++i)
            spheres[i] = {contacts.spheres[i].x, contacts.spheres[i].y + 0.001f, contacts.spheres[i].z, 0.0f};
        lift = 0.001f;
        lifted = false;
    };
    S_vector move(contacts.move.x, 0.0f, contacts.move.z);
    S_StepHits hits = {};
    const float horizontal = move.z * move.z + move.x * move.x;
    if (horizontal < 1e-16f) {
        // No horizontal move: the first sphere just above the ground (the others stay lifted).
        spheres[0] = {contacts.spheres[0].x, contacts.spheres[0].y + 0.001f, contacts.spheres[0].z, 0.0f};
        lift = 0.001f;
    } else {
        // A wall right ahead: no step up.
        const float inverse = 1.0f / S_vector(move.x, 0.0f, move.z).Magnitude();
        const S_vector probe(move.x * inverse * reach * 1.1f, inverse * 0.0f * reach * 1.1f, move.z * inverse * reach * 1.1f);
        S_vector probePos, probeNormal;
        if (RayTest(ToVector(spheres[0]), probe, &probePos, &probeNormal, 0, contacts.excludeFlags))
            lower();
        int nearest;
        do {
            if (most <= touched)
                break;
            float best = FLT_MAX;
            nearest = -1;
            for (uint32_t i = 0; i < count; ++i) {
                S_vector normal;
                S_CollisionVolume *hit = SphereTest(ToVector(spheres[i]), move, contacts.radii[i], &hits.pos[i], &normal,
                                                    contacts.excludeTypes, contacts.excludeFlags);
                if (!hit)
                    continue;
                hits.normal[i] = ToVector4(normal, hits.normal[i].w);
                if (g_sphereTestDistanceSq < best) {
                    nearest = static_cast<int>(i);
                    lastType = hit->type;
                    lastDynamic = (hit->flags & COLLISION_FLAG_DYNAMIC) != 0;
                    best = g_sphereTestDistanceSq;
                    last = hit;
                }
            }
            // Onto a lying human: a second one in the way stops the move.
            if (last && lastDynamic && (lastType & 0x100) &&
                (static_cast<C_Human *>(last->actor)->GetStanceFlags() & MOVE_LYING)) {
                if (lyingHuman && lyingHuman != last) {
                    contacts.pos = spheres[0];
                    return last;
                }
                lyingHuman = last;
            }
            if (lifted && (nearest > 0 || (last && lastDynamic && (lastType & 0x505300)))) {
                // Touched above the lowest sphere, or a dynamic object: no step up after all.
                lower();
                continue;
            }
            S_vector step = move;
            if (nearest >= 0) {
                S_vector4 &normal = hits.normal[nearest];
                const S_vector &hitPos = hits.pos[nearest];
                step = hitPos - ToVector(spheres[nearest]);
                if (FlattenWall(normal, 0.5f) && lifted) {
                    // A wall while lifted: no step up.
                    lower();
                    continue;
                }
                move = SlideAlong(ToVector(spheres[nearest]), move, hitPos, ToVector(normal), 0.0005f);
                contacts.normals[touched] = normal;
                contacts.sphere[touched] = nearest;
                contacts.volumes[touched] = last;
                ++touched;
            }
            if (step.Square() > 1e-8f) {
                for (uint32_t i = 0; i < count; ++i) {
                    spheres[i].x += step.x;
                    spheres[i].y += step.y;
                    spheres[i].z += step.z;
                }
            }
        } while (nearest >= 0);
    }
    // Down by the lift and the vertical move: from the top sphere when going up (moved to
    // the first one's place), the first sphere otherwise.
    const float down = moveY - lift;
    int from = 0;
    S_vector shift(0.0f, 0.0f, 0.0f);
    if (down > 0.0f) {
        from = static_cast<int>(count) - 1;
        shift = ToVector(spheres[0]) - ToVector(spheres[from]);
    }
    S_vector groundPos, groundNormal;
    S_CollisionVolume *ground = SphereTest(ToVector(spheres[from]), S_vector(0.0f, down, 0.0f), contacts.radii[from] - 0.001f,
                                           &groundPos, &groundNormal, contacts.excludeTypes | 0x100, contacts.excludeFlags);
    if (!ground) {
        // Nothing below: in the air (the end set only after a contact).
        if (last)
            contacts.pos = {spheres[0].x, spheres[0].y + down, spheres[0].z, contacts.pos.w};
        return last;
    }
    contacts.sphere[touched] = 0;
    contacts.normals[touched] = ToVector4(groundNormal, 0.0f);
    contacts.volumes[touched] = ground;
    contacts.count = touched + 1;
    contacts.pos = {groundPos.x + shift.x, groundPos.y + 0.001f + shift.y, groundPos.z + shift.z, spheres[0].w};
    return ground;
}

S_CollisionVolume *C_CollisionWorld::MoveSpheresLow(S_MoveContacts &contacts) const
{
    const uint32_t count = contacts.sphereCount;
    const float moveY = contacts.move.y;
    int touched = 0;
    S_CollisionVolume *last = nullptr;
    bool lifted = true;
    bool onHuman = false;                                   // touched a human's volume
    int most = static_cast<int>(g_moveIterations);
    S_vector4 spheres[MOVE_CONTACTS_MAX] = {};
    for (uint32_t i = 0; i < count; ++i)
        spheres[i] = {contacts.spheres[i].x, contacts.spheres[i].y + 0.1f, contacts.spheres[i].z, 0.0f};
    S_vector move(contacts.move.x, 0.0f, contacts.move.z);
    const S_vector way = S_vector(move.x, 0.0f, move.z).Normalized();
    S_StepHits hits = {};
    bool keptLift = true;
    if (move.z * move.z + move.x * move.x >= 1e-16f) {
        int nearest;
        do {
            if (most <= touched)
                break;
            float best = FLT_MAX;
            nearest = -1;
            for (uint32_t i = 0; i < count; ++i) {
                S_vector normal;
                S_CollisionVolume *hit = SphereTest(ToVector(spheres[i]), move, contacts.radii[i], &hits.pos[i], &normal,
                                                    contacts.excludeTypes, contacts.excludeFlags);
                if (!hit)
                    continue;
                hits.normal[i] = ToVector4(normal, hits.normal[i].w);
                onHuman = onHuman || ((hit->flags & COLLISION_FLAG_DYNAMIC) && (hit->type & 0x100));
                if (g_sphereTestDistanceSq < best) {
                    best = g_sphereTestDistanceSq;
                    nearest = static_cast<int>(i);
                    last = hit;
                }
            }
            if (lifted && onHuman) {
                // A human in the way: again from just above the ground.
                for (uint32_t i = 0; i < count; ++i)
                    spheres[i] = {contacts.spheres[i].x, contacts.spheres[i].y + 0.001f, contacts.spheres[i].z, 0.0f};
                nearest = static_cast<int>(count);
                lifted = false;
                last = nullptr;
                touched = 0;
                most = static_cast<int>(g_moveIterations);
                continue;
            }
            S_vector step = move;
            if (nearest >= 0) {
                S_vector4 &normal = hits.normal[nearest];
                const S_vector &hitPos = hits.pos[nearest];
                step = hitPos - ToVector(spheres[nearest]);
                FlattenWall(normal, 0.6f);
                move = SlideAlong(ToVector(spheres[nearest]), move, hitPos, ToVector(normal), 0.0005f);
                contacts.normals[touched] = normal;
                contacts.sphere[touched] = nearest;
                contacts.volumes[touched] = last;
                ++touched;
            }
            if (step.Square() > 1e-8f) {
                for (uint32_t i = 0; i < count; ++i) {
                    spheres[i].x += step.x;
                    spheres[i].y += step.y;
                    spheres[i].z += step.z;
                }
            }
        } while (nearest >= 0);
        keptLift = lifted;
    }
    // The vertical move (and the lift back): sliding off humans and out of wedges between
    // walls, 3 steps at most.
    const float down = moveY - (keptLift ? 0.1f : 0.001f);
    move = S_vector(0.0f, down, 0.0f);
    S_vector wallNormal(0.0f, down, 0.0f);
    S_vector wallPos(0.0f, 0.0f, 0.0f);
    int lastSphere = -1;                                    // the sphere of the last contact below
    for (int steps = 0;;) {
        float best = FLT_MAX;
        int nearest = -1;
        int walls = 0;
        if (static_cast<int>(count) - 1 < 0)
            break;
        for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
            S_vector normal;
            S_CollisionVolume *hit = SphereTest(ToVector(spheres[i]), move, contacts.radii[i], &hits.pos[i], &normal,
                                                contacts.excludeTypes, contacts.excludeFlags);
            if (!hit)
                continue;
            hits.normal[i] = ToVector4(normal, hits.normal[i].w);
            if (g_sphereTestDistanceSq < best) {
                best = g_sphereTestDistanceSq;
                nearest = i;
                lastSphere = i;
                last = hit;
            }
            const S_vector flat = S_vector(normal.x, 0.0f, normal.z).Normalized();
            if (-0.3f < normal.y && normal.y < 0.6f) {
                // A wall: across the way counts twice.
                const float across = way.z * flat.z + flat.x * way.x + way.y * flat.y;
                if (-0.707f < across && across < 0.707f)
                    ++walls;
                ++walls;
                wallPos = hits.pos[i];
                wallNormal = flat;
            }
        }
        if (walls == 1) {
            // A single wall: something behind (0.3 m up)?
            const float length = contacts.radii[0] + 0.3f;
            S_vector behindPos, behindNormal;
            if (RayTest(S_vector(wallPos.x, wallPos.y + 0.3f, wallPos.z), -wallNormal * length, &behindPos, &behindNormal,
                        contacts.excludeTypes, contacts.excludeFlags))
                walls = 2;
        }
        if (nearest < 0 || (walls < 2 && (!(last->flags & COLLISION_FLAG_DYNAMIC) || !(last->type & 0x100))))
            break;
        // Wedged or on a human: slid along, the rest of the move kept to its length.
        const S_vector &hitPos = hits.pos[nearest];
        const S_vector4 &normal = hits.normal[nearest];
        const S_vector step = hitPos - ToVector(spheres[nearest]);
        const S_vector slid = SlideAlong(ToVector(spheres[nearest]), move, hitPos, ToVector(normal), 0.0f);
        const S_vector flat = S_vector(normal.x, 0.0f, normal.z).Normalized();
        const float length = slid.Magnitude();
        contacts.normals[touched] = normal;
        move = flat * length * 0.001f + slid;
        contacts.sphere[touched] = nearest;
        contacts.volumes[touched] = last;
        contacts.pos = ToVector4(ToVector(spheres[0]) + hitPos - ToVector(spheres[nearest]), contacts.pos.w);
        ++touched;
        contacts.count = touched;
        for (uint32_t i = 0; i < count; ++i) {
            spheres[i].x += step.x;
            spheres[i].y += step.y;
            spheres[i].z += step.z;
        }
        if (++steps > 2)
            break;
    }
    // The ground under the last sphere that touched.
    const auto onto = [&](int sphere) {
        contacts.normals[touched] = hits.normal[sphere];
        contacts.sphere[touched] = 0;
        contacts.volumes[touched] = last;
        contacts.pos = ToVector4(ToVector(spheres[0]) + hits.pos[sphere] - ToVector(spheres[sphere]), contacts.pos.w);
        contacts.count = touched + 1;
        return last;
    };
    if (lastSphere >= 0)
        return onto(lastSphere);
    // Nothing below: tested again from 1 m up.
    if (static_cast<int>(count) - 1 >= 0) {
        float best = FLT_MAX;
        int nearest = -1;
        const S_vector drop(0.0f, down - 1.0f, 0.0f);
        for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
            S_vector normal;
            S_CollisionVolume *hit = SphereTest(S_vector(spheres[i].x, spheres[i].y + 1.0f, spheres[i].z), drop, contacts.radii[i],
                                                &hits.pos[i], &normal, contacts.excludeTypes, contacts.excludeFlags);
            if (hit) {
                hits.normal[i] = ToVector4(normal, hits.normal[i].w);
                if (g_sphereTestDistanceSq < best) {
                    best = g_sphereTestDistanceSq;
                    nearest = i;
                    last = hit;
                }
            }
        }
        if (nearest >= 0)
            return onto(nearest);
    }
    if (last)
        contacts.pos = {spheres[0].x, spheres[0].y + down, spheres[0].z, spheres[0].w};
    return last;
}
