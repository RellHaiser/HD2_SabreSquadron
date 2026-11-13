// The dead body (original source 0x007b0d70..0x007b7e10 and 0x007b88b0): a death either plays
// the dying animation (the body slides along the ground with it, its three points kept on the
// ground; a body held when it died falls into the ragdoll when it hits something) or drops
// the ragdoll (human_pose_ragdoll.cpp) at once, pushed by the hit; later hits push the ragdoll
// again. A body coming to rest leaves a blood pool.
#include "game/human_pose_internal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/human_stains.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// StartDeath: hit flags of C_HumanWork::ApplyHit beyond the zones.
enum : uint32_t {
    DEATH_HIT_ZONES = 0x3F,                             // HIT_ZONE bits: 1 head, 2 / 4 arms, 8 body, 0x10 / 0x20 legs
    DEATH_HIT_FALL = 0x1000,                            // falls without a push (full gravity)
    DEATH_HIT_BLAST = 0x4000,                           // the force used as it is (a fifth, at most 450)
    DEATH_HIT_SINK = 0x8400,                            // sinks slowly (as in water; inferred)
};

// The death animation plays at one and a half times its speed and blends in over 300 ms.
const double DEATH_ANIM_SPEED = 1.5;

// What the dying body's sweeps test (C_CollisionWorld type and flag masks to skip).
const uint32_t DEATH_TEST_TYPES = 0xCC002;
const uint32_t DEATH_SLIDE_TEST_TYPES = 0xCC102;
const uint32_t DEATH_TEST_FLAGS = 0x230000;
// The volume a dead body lies on (or in).
const uint32_t DEATH_VEHICLE_TYPES = 0xD8102;
// The first person camera's sweep to the head.
const uint32_t DEATH_CAMERA_TYPES = 0xC8002;

// The square of the sum of the frame's world axes: about zero for a collapsed (hidden) part.
float AxesSumSquare(I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return (m(0) + m(1) + m(2)).Square();
}

bool IsCollapsed(I3D_frame *frame) { return AxesSumSquare(frame) < 1e-8f; }

S_vector4 Scaled4(const S_vector4 &v, float s) { return {v.x * s, v.y * s, v.z * s, v.w}; }

}  // namespace

// ---- dying --------------------------------------------------------------------------------

// 0x007b0d70: the death of the body (clients only): `hitFlags` of the killing hit and its
// `force`, the dying animation (null: none), the kind of death (E_DeathKind) and the human
// carrying the body when it is dropped (null: none). Bodies far from the camera, behind it, or
// in seats play the animation; the rest (and those without one) fall into the ragdoll.
void C_HumanWeaponPose::StartDeath(uint32_t hitFlags, const float *force, I3D_animation_set *deathAnim, int kind,
                                   C_Human *carrier)
{
    if (!IsClient()) {
        m_ragdollWeight = 0.0f;
        return;
    }
    if (m_slidePoints)
        _aligned_free(m_slidePoints);
    m_slidePoints = nullptr;
    m_deathKind = static_cast<int16_t>(kind);
    m_carrier = carrier;
    bool ragdoll = kind == DEATH_RAGDOLL;
    if (IsLocalGame() && g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_RAGDOLL) && kind != DEATH_RAGDOLL) {
        I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
        const S_vector toBody = m_bones[POSE_BONE_SPINE].frame->GetWorldPos() - camera->GetWorldPos();
        S_vector look = camera->GetMatrix()(2);
        look.Normalize();
        ragdoll = !(PoseIsNegative(look.Dot(toBody)) || 100.0f <= toBody.Square() || kind == DEATH_KIND_1 ||
                    kind == DEATH_SEATED || kind == DEATH_HELD);
    }
    const uint32_t detail = m_movement->GetFlags();
    if (!ragdoll) {
        const bool fall = kind != DEATH_KIND_1 && kind != DEATH_SEATED && kind != DEATH_HELD &&
                          !(detail & MOVE_FLAG_TINY) && !(detail & (MOVE_FLAG_HIDDEN | MOVE_FLAG_NOT_DRAWN)) &&
                          !(m_flags & (POSE_FLAG_STEER | POSE_FLAG_SEAT_WEAPON | POSE_FLAG_PASSENGER)) &&
                          g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_RAGDOLL);
        if (!fall) {
            m_deathAnim = deathAnim;
            if (deathAnim) {
                StartDeathAnim();
                return;
            }
        }
    }
    StartRagdoll(hitFlags, force, carrier);
}

// The animated part of 0x007b0d70: the bones' matrices kept, the keys from the start; a held
// body remembers where its head, back and knees are, a falling one its neck, pelvis and knees
// on the ground (in the model's up axes, a quarter metre up).
void C_HumanWeaponPose::StartDeathAnim()
{
    for (S_PoseBone &bone : m_bones) {
        bone.chainLocal = bone.frame->GetLocalMatrix();
        bone.chainWorld = bone.frame->GetMatrix();
    }
    std::memset(m_deathPosKey, 0, sizeof(m_deathPosKey));
    std::memset(m_deathRotKey, 0, sizeof(m_deathRotKey));
    m_bodyPos = PoseWorldPos4(m_bones[POSE_BONE_SPINE].frame);
    m_slideVelocity.x = 0.0f;
    m_slideVelocity.y = 0.0f;
    m_slideVelocity.z = 0.0f;
    if (m_deathKind == DEATH_HELD) {
        m_slidePoints = static_cast<S_vector4 *>(_aligned_malloc(DEATH_HELD_POINT_COUNT * sizeof(S_vector4), 16));
        StoreHeldPoints();
    } else if (m_deathKind != DEATH_SEATED) {
        m_slidePoints = static_cast<S_vector4 *>(_aligned_malloc(DEATH_HELD_POINT_COUNT * sizeof(S_vector4), 16));
        const S_vector up = m_model->GetMatrix()(1);
        const S_vector raise = up * 0.25f;
        auto onGround = [&](const S_vector &pos) {
            const S_vector &origin = m_model->GetWorldPos();
            const S_vector rel = pos - origin;
            return PoseVector4((rel - up * up.Dot(rel)) + origin + raise, 0.0f);
        };
        m_slidePoints[DEATH_SLIDE_NECK] = onGround(m_bones[POSE_BONE_NECK].frame->GetWorldPos());
        m_slidePoints[DEATH_SLIDE_PELVIS] = onGround(m_bones[POSE_BONE_PELVIS].frame->GetWorldPos());
        const S_vector knees =
            (m_bones[POSE_BONE_RIGHT_KNEE].frame->GetWorldPos() + m_bones[POSE_BONE_LEFT_KNEE].frame->GetWorldPos()) *
            0.5f;
        m_slidePoints[DEATH_SLIDE_KNEES] = onGround(knees);
    }
    m_deathTime = 0;
    m_flags |= POSE_FLAG_DEAD;
    m_cameraPos = PoseWorldPos4(m_cameraFpv);
}

// Where a held body's head, neck, chest, back, spine and knees are (m_slidePoints).
void C_HumanWeaponPose::StoreHeldPoints()
{
    static const int bones[DEATH_HELD_POINT_COUNT] = {POSE_BONE_HEAD, POSE_BONE_NECK, POSE_BONE_CHEST,
                                                      POSE_BONE_BACK, POSE_BONE_SPINE, POSE_BONE_LEFT_KNEE,
                                                      POSE_BONE_RIGHT_KNEE};
    for (int i = 0; i < DEATH_HELD_POINT_COUNT; ++i)
        m_slidePoints[i] = PoseWorldPos4(m_bones[bones[i]].frame);
}

// The ragdoll part of 0x007b0d70: the parts measured from the model as it stands (an
// unconscious body carried off is built again with its saved rolls; parts whose end bone is
// collapsed are dropped; an unconscious body left lying loses its arms), the points started
// with the model's speed, the model taken out of its parent, and the hit's push (away from a
// hit from behind if there is no ground ahead; a slow push also holds the feet).
void C_HumanWeaponPose::StartRagdoll(uint32_t hitFlags, const float *force, C_Human *carrier)
{
    const bool unconscious = m_owner->IsUnconscious();
    if (unconscious && carrier) {
        BuildRagdoll(false);
        for (int i = 0; i < RAGDOLL_BODY_COUNT; ++i)
            if (m_bodies[i])
                m_bodies[i]->roll = m_savedRolls[i];
    }
    m_gravity.x = 0.0f;
    m_gravity.y = -9.0f;
    m_gravity.z = 0.0f;
    m_ragdollWeight = 1.0f;
    for (S_RagdollPoint *point : m_points)
        if (point)
            point->weight = 1.0f;
    m_steps = 6;
    m_unknown68E8 = 0;
    m_settleDelay = 1.0f;
    m_groundTime = 4.0f;
    m_deathAnim = nullptr;
    if (m_contactFrames)
        ReleaseRagdollContacts();
    m_inWater = false;

    // The bones the parts carry stiffly back to their rest matrices.
    auto restore = [&](int slot) {
        m_bones[g_ragdollRestBones[slot]].frame->SetLocalMatrix(m_restMatrices[slot]);
    };
    restore(RAGDOLL_REST_SPINE);
    restore(RAGDOLL_REST_LOWER_BACK);
    restore(RAGDOLL_REST_BACK);
    restore(RAGDOLL_REST_UPPER_CHEST);
    if (!m_owner->IsUnconscious() || carrier) {        // (asked again, as the original does, 0x007b1a12)
        restore(RAGDOLL_REST_LEFT_COLLAR);
        restore(RAGDOLL_REST_RIGHT_COLLAR);
    }
    restore(RAGDOLL_REST_NECK);
    restore(RAGDOLL_REST_HEAD_TOP);
    restore(RAGDOLL_REST_HEAD);
    m_bones[POSE_BONE_SPINE].frame->Update();

    // The parts measured again; those ending in a collapsed bone are dropped with their end.
    auto measure = [&](int body) { MeasureRagdollBody(m_bodies[body], -1, -1, false, false, false); };
    auto dropCollapsed = [&](int body, int endPoint) {
        // Fix: the original reads the part's end bone without checking that the part is still
        // there (a second death of a body that lost it before).
        S_RagdollBody *part = m_bodies[body];
        if (!part || !IsCollapsed(m_bones[part->endBone].frame))
            return;
        FreeRagdollBody(part);
        m_bodies[body] = nullptr;
        FreeRagdollPoint(m_points[endPoint]);
        m_points[endPoint] = nullptr;
    };
    measure(RAGDOLL_PELVIS);
    measure(RAGDOLL_CHEST);
    measure(RAGDOLL_HEAD);
    dropCollapsed(RAGDOLL_HEAD, RAGDOLL_POINT_HEAD);
    measure(RAGDOLL_LEFT_UPPER_ARM);
    measure(RAGDOLL_LEFT_FOREARM);
    if (!m_owner->IsUnconscious())
        dropCollapsed(RAGDOLL_LEFT_FOREARM, RAGDOLL_POINT_LEFT_HAND);
    measure(RAGDOLL_RIGHT_UPPER_ARM);
    measure(RAGDOLL_RIGHT_FOREARM);
    if (!m_owner->IsUnconscious())
        dropCollapsed(RAGDOLL_RIGHT_FOREARM, RAGDOLL_POINT_RIGHT_HAND);
    measure(RAGDOLL_LEFT_THIGH);
    measure(RAGDOLL_LEFT_SHIN);
    dropCollapsed(RAGDOLL_LEFT_SHIN, RAGDOLL_POINT_LEFT_FOOT);
    measure(RAGDOLL_RIGHT_THIGH);
    measure(RAGDOLL_RIGHT_SHIN);
    dropCollapsed(RAGDOLL_RIGHT_SHIN, RAGDOLL_POINT_RIGHT_FOOT);

    // The speed of the model along the ground (at most 5 m/s; the height not kept).
    float speedX = m_velocity.x;
    float speedZ = m_velocity.z;
    const float speedSq = speedX * speedX + speedZ * speedZ;
    if (speedSq > 25.0f) {
        const float scale = PoseFastInvSqrt(speedSq) * 5.0f;
        speedX *= scale;
        speedZ *= scale;
    }
    const float step = static_cast<float>(m_time) * 0.00055f;
    S_vector speed(speedX * step, 0.0f, speedZ * step);
    if (!(hitFlags & DEATH_HIT_FALL) && !carrier) {
        if (hitFlags & DEATH_HIT_SINK) {
            m_gravity.x = 0.0f;
            m_gravity.y = -2.25f;
            m_gravity.z = 0.0f;
            m_inWater = true;
        }
    } else {
        m_gravity.x = 0.0f;
        m_gravity.y = -9.81f;
        m_gravity.z = 0.0f;
        if (carrier)
            speed = S_vector(0.0f, 0.0f, 0.0f);
    }

    // The points where the parts start and end, moving with the speed.
    auto place = [&](S_RagdollPoint *point, const S_vector4 &pos) {
        point->pos = pos;
        point->oldPos = PoseVector4(PoseXYZ(pos) - speed, pos.w);
    };
    auto startOf = [&](int body) { return PoseRow4(m_bodies[body]->world, 3); };
    // (The original leaves the w of the end points stale.)
    auto endOf = [&](int body) {
        const S_RagdollBody *part = m_bodies[body];
        return PoseVector4(part->world(3) + part->world(2) * part->length, 0.0f);
    };
    S_RagdollPoint **p = m_points;
    place(p[RAGDOLL_POINT_PELVIS], startOf(RAGDOLL_PELVIS));
    place(p[RAGDOLL_POINT_CHEST], startOf(RAGDOLL_CHEST));
    place(p[RAGDOLL_POINT_NECK], endOf(RAGDOLL_CHEST));
    if (p[RAGDOLL_POINT_HEAD])
        place(p[RAGDOLL_POINT_HEAD], endOf(RAGDOLL_HEAD));
    place(p[RAGDOLL_POINT_LEFT_SHOULDER], startOf(RAGDOLL_LEFT_UPPER_ARM));
    if (p[RAGDOLL_POINT_LEFT_ELBOW])
        place(p[RAGDOLL_POINT_LEFT_ELBOW], endOf(RAGDOLL_LEFT_UPPER_ARM));
    if (p[RAGDOLL_POINT_LEFT_HAND])
        place(p[RAGDOLL_POINT_LEFT_HAND], endOf(RAGDOLL_LEFT_FOREARM));
    place(p[RAGDOLL_POINT_RIGHT_SHOULDER], startOf(RAGDOLL_RIGHT_UPPER_ARM));
    if (p[RAGDOLL_POINT_RIGHT_ELBOW])
        place(p[RAGDOLL_POINT_RIGHT_ELBOW], endOf(RAGDOLL_RIGHT_UPPER_ARM));
    if (p[RAGDOLL_POINT_RIGHT_HAND])
        place(p[RAGDOLL_POINT_RIGHT_HAND], endOf(RAGDOLL_RIGHT_FOREARM));
    place(p[RAGDOLL_POINT_LEFT_HIP], startOf(RAGDOLL_LEFT_THIGH));
    place(p[RAGDOLL_POINT_LEFT_KNEE], endOf(RAGDOLL_LEFT_THIGH));
    if (p[RAGDOLL_POINT_LEFT_FOOT])
        place(p[RAGDOLL_POINT_LEFT_FOOT], endOf(RAGDOLL_LEFT_SHIN));
    place(p[RAGDOLL_POINT_RIGHT_HIP], startOf(RAGDOLL_RIGHT_THIGH));
    place(p[RAGDOLL_POINT_RIGHT_KNEE], endOf(RAGDOLL_RIGHT_THIGH));
    if (p[RAGDOLL_POINT_RIGHT_FOOT])
        place(p[RAGDOLL_POINT_RIGHT_FOOT], endOf(RAGDOLL_RIGHT_SHIN));

    // An unconscious body left lying loses its arms.
    if (m_owner->IsUnconscious() && !carrier) {
        S_RagdollPoint *neck = p[RAGDOLL_POINT_NECK];
        for (int arm : {RAGDOLL_LEFT_UPPER_ARM, RAGDOLL_RIGHT_UPPER_ARM})
            for (S_RagdollBody *&body : neck->bodies)
                if (body == m_bodies[arm]) {
                    body = nullptr;
                    break;
                }
        FreeRagdollBody(m_bodies[RAGDOLL_LEFT_UPPER_ARM]);
        m_bodies[RAGDOLL_LEFT_UPPER_ARM] = nullptr;
        m_bodies[RAGDOLL_LEFT_FOREARM] = nullptr;
        FreeRagdollPoint(p[RAGDOLL_POINT_LEFT_ELBOW]);
        p[RAGDOLL_POINT_LEFT_ELBOW] = nullptr;
        p[RAGDOLL_POINT_LEFT_HAND] = nullptr;
        FreeRagdollBody(m_bodies[RAGDOLL_RIGHT_UPPER_ARM]);
        m_bodies[RAGDOLL_RIGHT_UPPER_ARM] = nullptr;
        m_bodies[RAGDOLL_RIGHT_FOREARM] = nullptr;
        FreeRagdollPoint(p[RAGDOLL_POINT_RIGHT_ELBOW]);
        p[RAGDOLL_POINT_RIGHT_ELBOW] = nullptr;
        p[RAGDOLL_POINT_RIGHT_HAND] = nullptr;
    }

    StoreRagdollRest(m_rootBody);
    UpdateRagdollBodies(m_rootBody, true, false);
    PlaceRagdollPoint(m_rootPoint, m_rootPoint->pos, false);
    m_model->LinkTo(nullptr, 5);
    S_RagdollPoint *pelvis = p[RAGDOLL_POINT_PELVIS];
    m_bodyPos = pelvis->pos;
    // Quirk: the scene's two frames are passed the other way round than C_HumanMovement::Init
    // passes them ("z", then "q").
    GetGame()->GetScene()->AddHumanModel(m_model, m_dummyZ, m_bones[POSE_BONE_RIGHT_FOOT].frame);
    GetGame()->GetScene()->MarkTrackedModel(m_model);

    if (!(hitFlags & DEATH_HIT_FALL) && !carrier) {
        S_vector push(force[0], force[1], force[2]);
        float magnitude;
        int time;
        if (!(hitFlags & DEATH_HIT_BLAST)) {
            const S_matrix &model = m_model->GetMatrix();
            if (PoseIsNegative(push.Dot(model(2)))) {
                // From behind: forwards instead if there is no ground ahead.
                const S_vector ahead = m_bones[POSE_BONE_SPINE].frame->GetWorldPos() + model(2) * 0.65f;
                if (!CollisionRayTest(ahead, S_vector(0.0f, -3.0f, 0.0f), nullptr, nullptr, 0xFFFFFFFF, 0)) {
                    const S_vector &up = m_model->GetMatrix()(1);
                    push = (push - up * push.Dot(up)) * -1.0f;
                }
            }
            const float sq = push.Square();
            if (1e-8f <= sq) {
                magnitude = std::sqrt(sq);
                push *= 1.0f / magnitude;
                magnitude *= 0.22222222f;
                if (magnitude > 150.0f)
                    magnitude = 150.0f;
            } else {
                magnitude = 1.0f;
            }
            push *= magnitude;
            if ((push + PoseXYZ(m_velocity)).Square() < 225.0f) {
                // A slow push: the feet are held where they are (see AddRagdollHold; 0x007b3372)
                // and the push made 30.
                for (int foot : {RAGDOLL_POINT_LEFT_FOOT, RAGDOLL_POINT_RIGHT_FOOT})
                    if (S_RagdollPoint *point = p[foot])
                        AddRagdollHold(point, point->pos, 300);
                push = push * (1.0f / magnitude) * 30.0f;
                magnitude = 30.0f;
            }
            push.y += magnitude * 0.33333334f;
            time = 250;
        } else {
            magnitude = std::sqrt(push.Square());
            const float inv = 1.0f / magnitude;
            magnitude *= 0.2f;
            if (magnitude > 450.0f)
                magnitude = 450.0f;
            push = push * inv * magnitude;
            push.y += magnitude * 0.33333334f;
            time = 200;
        }
        ApplyHitImpulse(hitFlags, PoseVector4(push, 0.0f), time);
    }
    m_flags |= POSE_FLAG_DEAD;
    m_cameraPos = PoseWorldPos4(m_cameraFpv);

    // The vehicle the body lies on (or in) does not collide with it.
    m_owner->RemoveCollision(false);
    for (S_RagdollPoint *point : m_points) {
        if (!point)
            continue;
        const S_CollisionVolume *volume = g_collisionWorld.GetVolumeAt(PoseXYZ(point->pos), DEATH_VEHICLE_TYPES);
        if (!volume || !(volume->flags & (COLLISION_FLAG_DYNAMIC | COLLISION_FLAG_PRIMITIVE)))
            continue;
        if (C_Actor *actor = GetFrameActor(volume->frame)) {
            m_vehicle = actor;
            m_flags |= POSE_FLAG_ON_VEHICLE;
            break;
        }
    }
    m_owner->RestoreCollision(false);
}

// 0x007b3510: the body is picked up: no longer dead to the pose, its sounds stopped, its
// contacts let go.
void C_HumanWeaponPose::PickUpBody()
{
    m_flags &= ~POSE_FLAG_DEAD;
    if (m_impactSound != -1 && GetSoundModule())
        GetSoundModule()->Stop(&m_impactSound);
    m_impactSound = -1;
    if (m_dropSound >= 0 && GetSoundModule())
        GetSoundModule()->Stop(&m_dropSound);
    m_dropSound = -2;
    if (m_contactFrames)
        ReleaseRagdollContacts();
}

// 0x007b36d0: the ragdoll pushed by `impulse` for `time` ms where the hit (`hitFlags` zone)
// struck: shared at random between the two points of the zone (the body's three).
void C_HumanWeaponPose::ApplyHitImpulse(uint32_t hitFlags, const S_vector4 &impulse, int time)
{
    const int a = std::rand() + 1;
    const int b = std::rand() + 1;
    const int c = std::rand();
    auto split = [&](int first, int second) {
        S_RagdollPoint *other = m_points[second];
        if (!other) {
            AddRagdollPush(m_points[first], impulse, time);
            return;
        }
        const float sum = static_cast<float>(b + a);
        AddRagdollPush(m_points[first], Scaled4(impulse, static_cast<float>(a) / sum), time);
        AddRagdollPush(other, Scaled4(impulse, static_cast<float>(b) / sum), time);
    };
    switch (hitFlags & DEATH_HIT_ZONES) {
    case 0x01:
        // (The head takes the first share when it is there.)
        if (m_points[RAGDOLL_POINT_HEAD]) {
            const float sum = static_cast<float>(b + a);
            AddRagdollPush(m_points[RAGDOLL_POINT_HEAD], Scaled4(impulse, static_cast<float>(a) / sum), time);
            AddRagdollPush(m_points[RAGDOLL_POINT_NECK], Scaled4(impulse, static_cast<float>(b) / sum), time);
        } else {
            AddRagdollPush(m_points[RAGDOLL_POINT_NECK], impulse, time);
        }
        break;
    case 0x02:
        split(RAGDOLL_POINT_LEFT_ELBOW, RAGDOLL_POINT_LEFT_HAND);
        break;
    case 0x04:
        split(RAGDOLL_POINT_RIGHT_ELBOW, RAGDOLL_POINT_RIGHT_HAND);
        break;
    case 0x08: {
        const float sum = static_cast<float>(c + 1 + b + a);
        AddRagdollPush(m_points[RAGDOLL_POINT_PELVIS], Scaled4(impulse, static_cast<float>(a) / sum), time);
        AddRagdollPush(m_points[RAGDOLL_POINT_CHEST], Scaled4(impulse, static_cast<float>(b) / sum), time);
        AddRagdollPush(m_points[RAGDOLL_POINT_NECK], Scaled4(impulse, static_cast<float>(c + 1) / sum), time);
        break;
    }
    case 0x10:
        split(RAGDOLL_POINT_LEFT_KNEE, RAGDOLL_POINT_LEFT_FOOT);
        break;
    case 0x20:
        split(RAGDOLL_POINT_RIGHT_KNEE, RAGDOLL_POINT_RIGHT_FOOT);
        break;
    default:
        AddRagdollPush(m_points[RAGDOLL_POINT_CHEST], impulse, time);
        break;
    }
}

// 0x007b3c20: a body lying flat leaves a blood pool at a random point along its back (on the
// ground of its chest, pelvis or neck; offset off it by the trunk's thickness).
void C_HumanWeaponPose::AddSettledBloodPool()
{
    if (m_bloodPool)
        return;
    S_RagdollPoint *pelvis = m_points[RAGDOLL_POINT_PELVIS];
    S_RagdollPoint *chest = m_points[RAGDOLL_POINT_CHEST];
    S_RagdollPoint *neck = m_points[RAGDOLL_POINT_NECK];
    S_vector spine = PoseXYZ(neck->pos) - PoseXYZ(pelvis->pos);
    spine.Normalize();
    if (!(spine.y < 0.5f))
        return;
    S_vector normal(0.0f, 1.0f, 0.0f);
    if (chest->flags & RAGDOLL_POINT_CONTACT)
        normal = PoseXYZ(chest->normal);
    else if (pelvis->flags & RAGDOLL_POINT_CONTACT)
        normal = PoseXYZ(pelvis->normal);
    else if (neck->flags & RAGDOLL_POINT_CONTACT)
        normal = PoseXYZ(neck->normal);
    normal -= spine * normal.Dot(spine);
    normal.Normalize();
    const float t = static_cast<float>(std::rand()) * 3.051851e-05f;
    const float tt = t * t;
    S_vector pos = PoseXYZ(neck->pos) * ((1.0f - (t + t)) + tt) + PoseXYZ(chest->pos) * ((t + t) - (tt + tt)) +
                   PoseXYZ(pelvis->pos) * tt;
    float thickness = m_bodies[RAGDOLL_PELVIS]->thickness;
    if (thickness <= m_bodies[RAGDOLL_CHEST]->thickness)
        thickness = m_bodies[RAGDOLL_CHEST]->thickness;
    pos -= normal * (thickness * 0.4f);
    m_movement->AddBloodPool(pos, normal, 0);
    m_bloodPool = true;
}

// ---- the death animation --------------------------------------------------------------------

// 0x007b40e0: plays the death animation. A held body checks that its head, its trunk and its
// knees can move where the animation takes them (or falls into the ragdoll, pushed by the
// move; with nothing under it, it just falls). A falling body (every other update) slides its
// neck, pelvis and knees along the ground with its slide speed (falling with 45 m/s², sliding
// along what it hits, losing a tenth each time) and turns the model to the ground under them
// (at most 10 degrees from its up). After the animation the body stays where it is
// (C_HumanMovement::Ragdoll), with a blood pool under the pelvis on solid ground.
void C_HumanWeaponPose::UpdateDeathAnim()
{
    I3D_animation_set *anim = m_deathAnim;
    if (!anim)
        return;
    m_deathTime = static_cast<int>(static_cast<double>(m_time) * DEATH_ANIM_SPEED + m_deathTime);
    int time = m_deathTime;
    if (anim->GetEndTime() < time)
        time = m_deathAnim->GetEndTime();
    float blend = static_cast<float>(time) * 0.0033333334f;
    if (blend > 1.0f)
        blend = 1.0f;
    SampleDeathAnim(m_root, time, blend);
    m_model->Update();
    const S_CollisionVolume *pelvisGround = nullptr;
    if (m_deathKind == DEATH_HELD) {
        m_owner->RemoveCollision(false);
        if (!UpdateHeldDeath())
            return;
    } else if (m_deathKind != DEATH_SEATED && (m_updateCount & 1)) {
        m_owner->RemoveCollision(false);
        pelvisGround = SlideDeadBody();
    }

    if (m_deathAnim && m_deathAnim->GetEndTime() + 300 < m_deathTime &&
        (m_deathKind == DEATH_SEATED || m_deathKind == DEATH_HELD || PoseXYZ(m_slideVelocity).Square() < 0.0001 ||
         m_deathAnim->GetEndTime() + 10000 < m_deathTime)) {
        m_flags &= ~POSE_FLAG_DEAD;
        m_movement->Ragdoll(true, true);
        if (!m_bloodPool && pelvisGround && !(pelvisGround->flags & COLLISION_FLAG_DYNAMIC)) {
            m_movement->AddBloodPool(m_bones[POSE_BONE_PELVIS].frame->GetWorldPos(), m_model->GetMatrix()(1), 0);
            m_bloodPool = true;
        }
    }
    // The first person camera follows the top of the head (not into walls).
    if (m_owner->IsLocalPlayer()) {
        C_CameraModule *cameras = GetGame()->GetCameraModule();
        if (cameras && cameras->GetCamera() == m_cameraFpv) {
            const S_vector headTop = m_bones[POSE_BONE_HEAD_TOP].frame->GetWorldPos();
            S_vector hit;
            if (CollisionSphereTest(PoseXYZ(m_cameraPos), headTop - PoseXYZ(m_cameraPos), 0.17f, &hit, nullptr,
                                    DEATH_CAMERA_TYPES, DEATH_TEST_FLAGS))
                PoseSetXYZ(m_cameraPos, hit);
            else
                m_cameraPos = PoseWorldPos4(m_bones[POSE_BONE_HEAD_TOP].frame);
            m_cameraFpv->SetPos(PoseXYZ(m_cameraPos));
        }
    }
    m_owner->RestoreCollision(false);
}

// The held body's checks of 0x007b40e0: false when it fell into the ragdoll.
bool C_HumanWeaponPose::UpdateHeldDeath()
{
    const S_vector4 *was = m_slidePoints;
    auto pos = [&](int bone) { return m_bones[bone].frame->GetWorldPos(); };
    // Falls into the ragdoll with the speed of the move from `from` to `to` (`flags`: the zone).
    auto fallIfBlocked = [&](const S_vector &from, const S_vector &to, float radius, uint32_t flags) {
        const S_vector move = to - from;
        if (!CollisionSphereTest(from, move, radius, nullptr, nullptr, DEATH_TEST_TYPES, DEATH_TEST_FLAGS))
            return false;
        const float k = 1000.0f / static_cast<float>(m_time);
        const S_vector4 velocity = PoseVector4((to - from) * k, 0.0f);
        StartDeath(flags, &velocity.x, nullptr, DEATH_RAGDOLL, nullptr);
        return true;
    };
    // Is there ground under `p`?
    auto grounded = [&](const S_vector &p) {
        const S_vector above(p.x, m_model->GetWorldPos().y + 0.5f, p.z);
        return CollisionRayTest(above, S_vector(0.0f, -1.5f, 0.0f), nullptr, nullptr, DEATH_TEST_TYPES,
                                DEATH_TEST_FLAGS) != nullptr;
    };
    auto fall = [&]() {
        const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
        StartDeath(DEATH_HIT_FALL, &zero.x, nullptr, DEATH_RAGDOLL, nullptr);
    };

    // The head.
    S_vector from = PoseXYZ(was[0]) * 0.5f + PoseXYZ(was[1]) * 0.5f;
    S_vector to = pos(POSE_BONE_HEAD) * 0.5f + pos(POSE_BONE_NECK) * 0.5f;
    if (fallIfBlocked(from, to, 0.2f, 0x01))
        return false;
    if (!grounded(to)) {
        fall();
        return false;
    }
    // The trunk.
    from = PoseXYZ(was[4]) * 0.2f + (PoseXYZ(was[3]) * 0.3f + (PoseXYZ(was[2]) * 0.3f + PoseXYZ(was[1]) * 0.2f));
    to = pos(POSE_BONE_SPINE) * 0.2f +
         (pos(POSE_BONE_BACK) * 0.3f + (pos(POSE_BONE_CHEST) * 0.3f + pos(POSE_BONE_NECK) * 0.2f));
    if (fallIfBlocked(from, to, 0.25f, 0x08))
        return false;
    if (!grounded(to)) {
        fall();
        return false;
    }
    // The knees.
    from = PoseXYZ(was[6]) * 0.4f + (PoseXYZ(was[5]) * 0.4f + PoseXYZ(was[4]) * 0.2f);
    to = pos(POSE_BONE_RIGHT_KNEE) * 0.4f + (pos(POSE_BONE_LEFT_KNEE) * 0.4f + pos(POSE_BONE_SPINE) * 0.2f);
    if (fallIfBlocked(from, to, 0.2f, 0x08))
        return false;
    if (!grounded(to)) {
        fall();
        return false;
    }
    StoreHeldPoints();
    m_owner->RestoreCollision(false);
    return true;
}

// The falling body's slide of 0x007b40e0; returns the ground under the pelvis (null: none or
// not flat).
const S_CollisionVolume *C_HumanWeaponPose::SlideDeadBody()
{
    const float dt = static_cast<float>(m_time) * 0.001f;
    m_slideVelocity.y -= dt * 45.0f;
    const S_vector up = m_model->GetMatrix()(1);
    const S_vector raise = up * 0.25f;
    auto onGround = [&](const S_vector &p) {
        const S_vector &origin = m_model->GetWorldPos();
        const S_vector rel = p - origin;
        return raise + ((rel - up * rel.Dot(up)) + origin);
    };
    S_vector points[3] = {
        onGround(m_bones[POSE_BONE_NECK].frame->GetWorldPos()),
        onGround(m_bones[POSE_BONE_PELVIS].frame->GetWorldPos()),
        onGround((m_bones[POSE_BONE_RIGHT_KNEE].frame->GetWorldPos() + m_bones[POSE_BONE_LEFT_KNEE].frame->GetWorldPos()) *
                 0.5f),
    };
    const S_vector pelvisBefore = points[DEATH_SLIDE_PELVIS];
    // The points from where they were to where the animation takes them, stopped by the world.
    if (m_slidePoints) {
        for (int i = 0; i < 3; ++i) {
            S_vector stopped = points[i];
            const S_vector last = PoseXYZ(m_slidePoints[i]);
            CollisionSphereTest(last, points[i] - last, 0.25f, &stopped, nullptr, DEATH_SLIDE_TEST_TYPES,
                                DEATH_TEST_FLAGS);
            points[i] = stopped;
        }
    }
    // The vehicle the body lies in is not in the way.
    auto ownVehicle = [&](const S_CollisionVolume *volume, bool dynamicFirst) {
        if (!m_vehicle || !volume)
            return false;
        if (dynamicFirst && !(volume->flags & COLLISION_FLAG_DYNAMIC))
            return false;
        if (!(volume->flags & (COLLISION_FLAG_DYNAMIC | COLLISION_FLAG_PRIMITIVE)))
            return false;
        return GetFrameActor(volume->frame) == m_vehicle;
    };
    // The slide: up to two moves, each stopped at the first point to hit something (the speed
    // then sliding along it).
    for (int tries = 2;;) {
        --tries;
        S_vector normals[3] = {S_vector(0, 0, 0), S_vector(0, 0, 0), S_vector(0, 0, 0)};
        float part = 1.0f;
        int first = 0;
        const S_vector move = PoseXYZ(m_slideVelocity) * dt;
        for (int i = 0; i < 3; ++i) {
            S_vector hit;
            const S_CollisionVolume *volume = CollisionSphereTest(points[i], move, 0.25f, &hit, &normals[i],
                                                                  DEATH_SLIDE_TEST_TYPES, DEATH_TEST_FLAGS);
            if (!volume || ownVehicle(volume, true))
                continue;
            const S_vector toHit = hit - points[i];
            // (Summed (x + z) + y as the original, 0x007b5a64: the rounding picks the first point hit.)
            const float along = (move.x * toHit.x + toHit.z * move.z) + toHit.y * move.y;
            const float t = !(along > 1e-8f) ? 0.0f : ((toHit.x * toHit.x + toHit.z * toHit.z) + toHit.y * toHit.y) / along;
            if (t < part) {
                part = t;
                first = i;
            }
        }
        const S_vector step = move * part;
        for (S_vector &p : points)
            p += step;
        if (1.0f - part < 1e-8f)
            break;
        const S_vector &normal = normals[first];
        const S_vector slide = normal * PoseDotZYX(normal, PoseXYZ(m_slideVelocity));
        m_slideVelocity.x = (m_slideVelocity.x - slide.x) * 0.9f;
        m_slideVelocity.y = (m_slideVelocity.y - slide.y) * 0.9f;
        m_slideVelocity.z = (m_slideVelocity.z - slide.z) * 0.9f;
        if (part < 1e-8f || tries == 0)
            break;
    }
    if (m_slidePoints)
        for (int i = 0; i < 3; ++i)
            PoseSetXYZ(m_slidePoints[i], points[i]);
    m_model->SetPos(m_model->GetWorldPos() + (points[DEATH_SLIDE_PELVIS] - pelvisBefore));

    // The ground under the points (a sphere down the model's up axis); too far above or below
    // the model's ground, or too steep, counts as flat.
    const S_vector down = up * -10.0f;
    const S_vector flat(0.0f, 1.0f, 0.0f);
    S_vector ground[3];
    S_vector normals[3];
    const S_CollisionVolume *volumes[3];
    for (int i = 0; i < 3; ++i) {
        volumes[i] = CollisionSphereTest(points[i], down, 0.25f, &ground[i], &normals[i], DEATH_SLIDE_TEST_TYPES,
                                         DEATH_TEST_FLAGS);
        if (ownVehicle(volumes[i], false))
            volumes[i] = nullptr;
    }
    for (int i = 0; i < 3; ++i) {
        const S_vector &origin = m_model->GetWorldPos();
        if (volumes[i]) {
            ground[i] -= normals[i] * 0.25f;
            const float height = (ground[i] - origin).Dot(up);
            if (height > 0.75f || height < -0.75f || normals[i].y < 0.707f) {
                normals[i] = flat;
                // (Only the pelvis forgets its ground then.)
                if (i == DEATH_SLIDE_PELVIS)
                    volumes[i] = nullptr;
            }
        } else {
            ground[i] = points[i] + up * (origin - points[i]).Dot(up);
            normals[i] = flat;
        }
    }
    // The ground's normal: the pelvis's blended with the ends', off the body's axis, at most 10
    // degrees from the model's up.
    const S_vector ends = PoseSlerpDir(normals[DEATH_SLIDE_NECK], normals[DEATH_SLIDE_KNEES], 0.5f);
    S_vector normal = PoseSlerpDir(ends, normals[DEATH_SLIDE_PELVIS], 0.75f);
    S_vector axis = ground[DEATH_SLIDE_NECK] - ground[DEATH_SLIDE_KNEES];
    axis.Normalize();
    normal -= axis * normal.Dot(axis);
    normal.Normalize();
    const S_matrix &model = m_model->GetMatrix();
    S_vector local = PoseIntoAxes(model, normal);
    if (local.y < 0.9848f) {
        const float s = std::sqrt(1.0f - local.y * local.y);
        if (!(s > 1e-8f)) {                              // (a NaN too, as the original, 0x007b7053)
            local.x = 0.1736f;
            local.z = 0.0f;
        } else {
            const float k = 0.1736f / s;
            local.x *= k;
            local.z *= k;
        }
        local.y = 0.9848f;
        normal = local.RotateByMatrix(model);
        normal.Normalize();
    }
    // The model turned onto it.
    S_matrix m = m_model->GetMatrix();
    I3D_frame *parent = m_model->GetParent();
    const S_matrix parentInverse = parent ? PoseInverseRotation(parent->GetMatrix()) : IdentityMatrix();
    m(1) = normal;
    // (The original leaves stale values in m[0][3] and m[1][3].)
    m.m[1][3] = 0.0f;
    m(2) -= normal * m(2).Dot(normal);
    m(2).Normalize();
    m(0) = normal.Cross(m(2));
    m.m[0][3] = 0.0f;
    const float roll = PoseGetRoll(m);
    m_owner->SetDirection4(PoseRow4(m, 2));
    m_movement->m_unknown250 = roll;
    m *= parentInverse;
    const S_vector &modelPos = m_model->GetPos();
    m.m[3][0] = modelPos.x;
    m.m[3][1] = modelPos.y;
    m.m[3][2] = modelPos.z;
    m.m[3][3] = m_model->GetPosW();
    m_model->SetLocalMatrix(m);
    m_model->Update();
    return volumes[DEATH_SLIDE_PELVIS];
}

// 0x007b7870: the keys of the death animation at `time` on `bone` and the bones below it
// (positions lerped, rotations slerped between the keys around the time; the search goes on
// from where it got to). (`blend` is not used.)
void C_HumanWeaponPose::SampleDeathAnim(S_PoseBone *bone, int time, float blend)
{
    I3D_animation_set *anim = m_deathAnim;
    while (bone) {
        const int index = bone->index;
        const int track = anim->FindTrack(g_poseBoneNames[index]);
        if (track >= 0) {
            S_vector4 prevPos = {0.0f, 0.0f, 0.0f, 0.0f};
            S_vector4 nextPos = {0.0f, 0.0f, 0.0f, 0.0f};
            int prevPosTime = 0;
            int nextPosTime = 0;
            S_quat prevRot(1.0f, 0.0f, 0.0f, 0.0f);
            S_quat nextRot(1.0f, 0.0f, 0.0f, 0.0f);
            int prevRotTime = 0;
            int nextRotTime = 0;
            for (;;) {
                prevPos.x = PoseUnset();
                nextPos.x = PoseUnset();
                const int key = m_deathPosKey[index];
                if (key < anim->NumPosKeys(track))
                    anim->GetPosKey(track, key, &prevPosTime, reinterpret_cast<S_vector *>(&prevPos));
                const int next = m_deathPosKey[index] + 1;
                if (next < anim->NumPosKeys(track))
                    anim->GetPosKey(track, next, &nextPosTime, reinterpret_cast<S_vector *>(&nextPos));
                if (PoseIsUnset(nextPos.x)) {
                    nextPos = prevPos;
                    nextPosTime = prevPosTime;
                    break;
                }
                if (nextPosTime >= time)
                    break;
                ++m_deathPosKey[index];
            }
            for (;;) {
                prevRot.s = PoseUnset();
                nextRot.s = PoseUnset();
                const int key = m_deathRotKey[index];
                if (key < anim->NumRotKeys(track))
                    anim->GetRotKey(track, key, &prevRotTime, &prevRot);
                const int next = m_deathRotKey[index] + 1;
                if (next < anim->NumRotKeys(track))
                    anim->GetRotKey(track, next, &nextRotTime, &nextRot);
                if (PoseIsUnset(nextRot.s)) {
                    nextRot = prevRot;
                    nextRotTime = prevRotTime;
                    break;
                }
                if (nextRotTime >= time)
                    break;
                ++m_deathRotKey[index];
            }
            I3D_frame *frame = m_bones[index].frame;
            const bool collapsed = !(1e-8f < AxesSumSquare(frame));
            if (!PoseIsUnset(prevPos.x)) {
                S_vector p;
                if (nextPosTime - prevPosTime < 1) {
                    p = PoseXYZ(prevPos);
                } else {
                    const float f = static_cast<float>(time - prevPosTime) / static_cast<float>(nextPosTime - prevPosTime);
                    p = PoseXYZ(prevPos) * (1.0f - f) + PoseXYZ(nextPos) * f;
                }
                frame->SetPosInline(p);
            }
            if (!PoseIsUnset(prevRot.s)) {
                S_quat rot = prevRot;
                if (nextRotTime - prevRotTime >= 1)
                    rot = prevRot.Slerp(nextRot, static_cast<float>(time - prevRotTime) /
                                                     static_cast<float>(nextRotTime - prevRotTime),
                                        false);
                // (Not on a collapsed bone.)
                if (!collapsed)
                    frame->SetRot(rot);
            }
        }
        if (bone->child[0])
            SampleDeathAnim(bone->child[0], time, blend);
        if (bone->child[1])
            SampleDeathAnim(bone->child[1], time, blend);
        bone = bone->child[2];
    }
}

// ---- hits ---------------------------------------------------------------------------------

// 0x007b88b0: a hit of the body (`flags`: the zone bits) at `pos` with `force`: the stains, and
// a dead body's ragdoll pushed (a ninth of the force, at most 150, a third of it up; woken
// first if it has settled).
void C_HumanWeaponPose::OnHit(uint32_t flags, const S_vector &pos, const S_vector &force)
{
    if (m_stains)
        m_stains->AddHit(pos, force, flags);
    if (!(m_flags & POSE_FLAG_DEAD) || m_deathAnim)
        return;
    float magnitude = force.Square();
    if (!flags || !(1e-8f < magnitude))
        return;
    magnitude = std::sqrt(magnitude);
    const float inv = 1.0f / magnitude;
    magnitude *= 0.11111111f;
    if (magnitude > 150.0f)
        magnitude = 150.0f;
    S_vector push = force * inv * magnitude;
    push.y = magnitude * 0.33333334f + inv * force.y * magnitude;
    if (m_ragdollWeight < 1e-8f)
        PlaceRagdollPoint(m_rootPoint, m_rootPoint->pos, true);
    ApplyHitImpulse(flags, PoseVector4(push, 0.0f), 200);
}
