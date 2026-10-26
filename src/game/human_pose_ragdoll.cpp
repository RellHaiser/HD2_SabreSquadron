// The ragdoll of the dead body (original source 0x007c11f0..0x007ce4d0): eleven rigid parts
// (S_RagdollBody, capsules carrying the model's bones) between sixteen points (S_RagdollPoint),
// simulated by Verlet integration in substeps. Gravity, pushes and holds move the points; the
// parts keep their lengths, their joint limits and their twist by corrections the points share
// by the parts' masses; the points and parts collide with the world (pushing cars they hit) and
// with each other; and the body settles once it lies still. The parts then pose the model's
// bones.
#include "game/human_pose_internal.h"

#include <cmath>
#include <cstring>
#include <malloc.h>
#include <new>

#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "ls3d/i3d.h"

namespace {

// What the collision queries of the ragdoll test (C_CollisionWorld type and flag masks to skip).
const uint32_t RAGDOLL_TEST_TYPES = 0xCC002;
const uint32_t RAGDOLL_TEST_FLAGS = 0x230000;
// The same without skipping water (the pelvis tests for it first).
const uint32_t RAGDOLL_TEST_FLAGS_WATER = 0x220000;

// The type mask of the frames a contact is looked up by after a saved game (I3D_scene::FindFrame).
const uint32_t RAGDOLL_CONTACT_FRAME_MASK = 0x8FFFF;

template <class T> T *AlignedNew(size_t size)
{
    void *memory = _aligned_malloc(size, 16);
    if (memory)
        std::memset(memory, 0, size);
    return static_cast<T *>(memory);
}

// A joint limit on one axis of the part's direction in its parent's axes, and its sine.
void SetLimit(float &limit, float &sine, float value)
{
    limit = value;
    sine = std::sqrt(1.0f - limit * limit);
}

void ClearXYZ(S_vector4 &v)
{
    v.x = 0.0f;
    v.y = 0.0f;
    v.z = 0.0f;
}

}  // namespace

const int g_ragdollRestBones[RAGDOLL_REST_BONE_COUNT] = {
    POSE_BONE_SPINE,       POSE_BONE_LOWER_BACK,   POSE_BONE_BACK,      POSE_BONE_UPPER_CHEST, POSE_BONE_CHEST,
    POSE_BONE_LEFT_COLLAR, POSE_BONE_RIGHT_COLLAR, POSE_BONE_NECK,      POSE_BONE_HEAD_TOP,    POSE_BONE_HEAD,
    POSE_BONE_LEFT_FOOT,   POSE_BONE_RIGHT_FOOT,   POSE_BONE_LEFT_HAND, POSE_BONE_RIGHT_HAND,
};

// ---- building ---------------------------------------------------------------------------

// 0x007c54e0: a new part under `parent` (null: the root), room for `boneCount` bones.
S_RagdollBody *C_HumanWeaponPose::NewRagdollBody(S_RagdollBody *parent, int boneCount)
{
    S_RagdollBody *body = AlignedNew<S_RagdollBody>(sizeof(S_RagdollBody));
    if (!body)
        return nullptr;
    body->parent = parent;
    body->local.Identity();
    body->world.Identity();
    body->endBone = -1;
    body->startBone = -1;
    body->mass = 1.0f;
    body->boneCount = 0;
    body->bones = nullptr;
    body->boneOffsets = nullptr;
    body->limitMin.x = body->limitMin.y = body->limitMin.z = PoseUnset();
    body->limitMax.x = body->limitMax.y = body->limitMax.z = PoseUnset();
    body->unknown220.x = body->unknown220.y = body->unknown220.z = PoseUnset();
    body->end = nullptr;
    body->start = nullptr;
    if (parent) {
        int slot = 0;
        while (slot < 3 && parent->children[slot])
            ++slot;
        if (slot == 3) {
            // Fix: the original returned null here without freeing the new part.
            _aligned_free(body);
            return nullptr;
        }
        parent->children[slot] = body;
    }
    if (boneCount) {
        body->bones = static_cast<int *>(operator new(boneCount * sizeof(int)));
        body->boneOffsets = static_cast<S_matrix *>(_aligned_malloc(boneCount * sizeof(S_matrix), 16));
    }
    return body;
}

// 0x007c5600: frees `body` and the parts below it, unlinking them from their points.
void C_HumanWeaponPose::FreeRagdollBody(S_RagdollBody *body)
{
    if (!body)
        return;
    for (int i = 0; i < 3; ++i)
        if (body->children[i])
            FreeRagdollBody(body->children[i]);
    if (!body->parent)
        m_rootBody = nullptr;
    m_bodies[body->index] = nullptr;
    if (body->bones)
        operator delete(body->bones);
    if (body->boneOffsets)
        _aligned_free(body->boneOffsets);
    if (S_RagdollBody *parent = body->parent) {
        for (int i = 0; i < 3; ++i)
            if (parent->children[i] == body) {
                parent->children[i] = nullptr;
                break;
            }
    }
    S_RagdollPoint *points[2] = {body->start, body->end};
    for (S_RagdollPoint *point : points) {
        if (!point)
            continue;
        for (int i = 0; i < 4; ++i)
            if (point->bodies[i] == body) {
                point->bodies[i] = nullptr;
                break;
            }
    }
    _aligned_free(body);
}

// 0x007c5740: the part from bone `startBone` towards `endBone` (-1: the ones it has) as the
// model stands: its world axes (z towards the end) and its axes against the parent part, the
// rest pose taken from them; `setLength` measures the length, `setThickness` makes the
// thickness a fifth of it, `setRoll` keeps the roll against the parent.
void C_HumanWeaponPose::MeasureRagdollBody(S_RagdollBody *body, int startBone, int endBone, bool setLength,
                                           bool setThickness, bool setRoll)
{
    if (!body)
        return;
    if (startBone < 0)
        startBone = body->startBone;
    else
        body->startBone = startBone;
    if (endBone < 0)
        endBone = body->endBone;
    else
        body->endBone = endBone;
    I3D_frame *start = m_bones[startBone].frame;
    body->world = start->GetMatrix();
    S_vector dir = m_bones[endBone].frame->GetWorldPos() - start->GetWorldPos();
    const float sq = dir.Square();
    if (sq < 1e-8f)
        return;
    float length = 1.0f;
    if (std::fabs(sq - 1.0f) >= 1e-8f) {
        length = std::sqrt(sq);
        if (length < 1e-8f)
            return;
    }
    if (setLength) {
        body->currentLength = length;
        body->length = length;
    }
    if (setThickness)
        body->thickness = body->length * 0.2f;
    dir *= 1.0f / length;
    PoseTurnZTo(body->world, dir);
    body->world.m[3][0] = start->GetMatrix().m[3][0];
    body->world.m[3][1] = start->GetMatrix().m[3][1];
    body->world.m[3][2] = start->GetMatrix().m[3][2];
    body->world.m[3][3] = start->GetMatrix().m[3][3];
    if (!body->parent) {
        body->local = body->world;
    } else {
        const S_matrix inverse = PoseInverseRotation(body->parent->world);
        body->local = body->world;
        body->local.m[3][0] -= body->parent->world.m[3][0];
        body->local.m[3][1] -= body->parent->world.m[3][1];
        body->local.m[3][2] -= body->parent->world.m[3][2];
        body->local *= inverse;
        if (setRoll)
            body->roll = PoseGetRoll(body->local);
    }
    body->rot.Make(body->local);
    body->rest = body->local;
    body->from = body->local;
    body->restRot = body->rot;
    body->fromRot = body->restRot;
    body->restLength = body->currentLength;
    body->fromLength = body->currentLength;
}

// 0x007c5a80: the bone `bone` moves with the part (its matrix kept in the part's axes); false
// when the part has it already or is full.
bool C_HumanWeaponPose::AddRagdollBone(S_RagdollBody *body, int bone)
{
    const int count = body->boneCount;
    if (count == 16)
        return false;
    for (int i = 0; i < count; ++i)
        if (body->bones[i] == bone)
            return false;
    body->bones[count] = bone;
    const S_matrix inverse = PoseInverseRotation(body->world);
    S_matrix &offset = body->boneOffsets[count];
    offset = m_bones[bone].frame->GetMatrix();
    offset.m[3][0] -= body->world.m[3][0];
    offset.m[3][1] -= body->world.m[3][1];
    offset.m[3][2] -= body->world.m[3][2];
    offset *= inverse;
    ++body->boneCount;
    return true;
}

// 0x007c61b0: a new point at `pos` under `parent` (null: the root) where the parts `b0`..`b3`
// meet; it is where `startOf` starts (its position kept in that part's axes, z in lengths, and
// in its parent's) and where `endOf` ends.
S_RagdollPoint *C_HumanWeaponPose::NewRagdollPoint(S_RagdollPoint *parent, const S_vector4 &pos, S_RagdollBody *b0,
                                                   S_RagdollBody *b1, S_RagdollBody *b2, S_RagdollBody *b3,
                                                   S_RagdollBody *startOf, S_RagdollBody *endOf)
{
    // (The original leaves the fields it does not set below uninitialised.)
    S_RagdollPoint *point = AlignedNew<S_RagdollPoint>(sizeof(S_RagdollPoint));
    point->parent = parent;
    if (parent) {
        for (int i = 0; i < 3; ++i)
            if (!parent->children[i]) {
                parent->children[i] = point;
                break;
            }
    }
    point->pos = pos;
    point->oldPos = pos;
    point->lastPos = pos;
    ClearXYZ(point->step);
    ClearXYZ(point->velocity);
    ClearXYZ(point->gravityStep);
    ClearXYZ(point->force);
    ClearXYZ(point->smoothVelocity);
    ClearXYZ(point->parentLocalPos);
    point->flags = 0;
    point->weight = 1.0f;
    point->contactFrame = nullptr;
    std::memset(point->corrections, 0, sizeof(point->corrections));
    point->contactFramePos.x = PoseUnset();
    if (b0)
        point->bodies[0] = b0;
    if (b1)
        point->bodies[1] = b1;
    if (b2)
        point->bodies[2] = b2;
    if (b3)
        point->bodies[3] = b3;
    if (startOf)
        startOf->start = point;
    if (endOf)
        endOf->end = point;
    if (startOf) {
        const S_vector local = PoseIntoAxes(startOf->world, PoseXYZ(point->pos) - startOf->world(3));
        point->localPos.x = local.x;
        point->localPos.y = local.y;
        point->localPos.z = local.z / startOf->length;
        if (S_RagdollBody *above = startOf->parent) {
            const S_vector inParent = PoseIntoAxes(above->world, PoseXYZ(point->pos) - above->world(3));
            PoseSetXYZ(point->parentLocalPos, inParent);
        }
    } else if (endOf) {
        const S_vector local = PoseIntoAxes(endOf->world, PoseXYZ(point->pos) - endOf->world(3));
        point->localPos.x = local.x;
        point->localPos.y = local.y;
        point->localPos.z = local.z / endOf->length;
    }
    return point;
}

// 0x007c64b0: frees `point` and the points below it.
void C_HumanWeaponPose::FreeRagdollPoint(S_RagdollPoint *point)
{
    if (!point)
        return;
    for (int i = 0; i < 3; ++i)
        if (point->children[i])
            FreeRagdollPoint(point->children[i]);
    if (!point->parent)
        m_rootPoint = nullptr;
    m_points[point->index] = nullptr;
    if (S_RagdollPoint *parent = point->parent) {
        for (int i = 0; i < 3; ++i)
            if (parent->children[i] == point) {
                parent->children[i] = nullptr;
                break;
            }
    }
    _aligned_free(point);
}

// 0x007c11f0: builds the ragdoll from the model as it stands (not after a saved game, which
// brings its own).
void C_HumanWeaponPose::BuildRagdoll(bool reload)
{
    if (reload)
        return;
    if (m_rootBody)
        FreeRagdollBody(m_rootBody);
    if (m_rootPoint)
        FreeRagdollPoint(m_rootPoint);
    m_rootBody = nullptr;
    m_rootPoint = nullptr;
    for (S_RagdollBody *&body : m_bodies)
        body = nullptr;
    for (S_RagdollPoint *&point : m_points)
        point = nullptr;
    m_gravity.x = 0.0f;
    m_gravity.y = -9.0f;
    m_gravity.z = 0.0f;
    m_ragdollWeight = 1.0f;
    m_unknown68E8 = 0;
    m_steps = 6;
    m_settleDelay = 1.0f;
    m_groundTime = 4.0f;
    m_sinceCollision = 0.0f;
    m_collideStep = false;
    m_groundedPoints = 0;
    m_contactFrames = 0;
    m_inWater = false;
    m_impactSound = -1;
    m_dropSound = -1;

    // The parts: bones from / to, the bones carried, the mass, the thickness, the limits.
    S_RagdollBody *body = NewRagdollBody(nullptr, 5);
    m_bodies[RAGDOLL_PELVIS] = body;
    if (body) {
        m_rootBody = body;
        MeasureRagdollBody(body, POSE_BONE_PELVIS, POSE_BONE_UPPER_CHEST, true, true, true);
        AddRagdollBone(body, POSE_BONE_PELVIS);
        AddRagdollBone(body, POSE_BONE_SPINE);
        AddRagdollBone(body, POSE_BONE_LOWER_BACK);
        AddRagdollBone(body, POSE_BONE_BACK);
        AddRagdollBone(body, POSE_BONE_UPPER_CHEST);
        body->mass = 2.0f;
        body->thickness *= 3.0f;
        body->index = RAGDOLL_PELVIS;
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_PELVIS], 4);
    m_bodies[RAGDOLL_CHEST] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_CHEST, POSE_BONE_NECK, true, true, true);
        AddRagdollBone(body, POSE_BONE_CHEST);
        AddRagdollBone(body, POSE_BONE_LEFT_COLLAR);
        AddRagdollBone(body, POSE_BONE_RIGHT_COLLAR);
        AddRagdollBone(body, POSE_BONE_NECK);
        body->mass = 2.0f;
        body->thickness *= 3.2f;
        body->index = RAGDOLL_CHEST;
        SetLimit(body->limitMin.z, body->limitMinSin.z, 0.3f);
        SetLimit(body->limitMin.y, body->limitMinSin.y, -0.7f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.7f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, -0.5f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.5f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_CHEST], 2);
    m_bodies[RAGDOLL_HEAD] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_NECK, POSE_BONE_HEAD, true, true, true);
        AddRagdollBone(body, POSE_BONE_HEAD_TOP);
        AddRagdollBone(body, POSE_BONE_HEAD);
        body->mass = 1.0f;
        body->length *= 3.0f;
        body->currentLength = body->length;
        body->thickness *= 5.5f;
        body->index = RAGDOLL_HEAD;
        SetLimit(body->limitMin.z, body->limitMinSin.z, 0.707f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_CHEST], 1);
    m_bodies[RAGDOLL_LEFT_UPPER_ARM] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_LEFT_SHOULDER, POSE_BONE_LEFT_ELBOW, true, true, true);
        AddRagdollBone(body, POSE_BONE_LEFT_SHOULDER);
        body->mass = 1.0f;
        body->thickness *= 1.5f;
        body->index = RAGDOLL_LEFT_UPPER_ARM;
        SetLimit(body->limitMax.z, body->limitMaxSin.z, 0.8f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.0f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.707f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_LEFT_UPPER_ARM], 2);
    m_bodies[RAGDOLL_LEFT_FOREARM] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_LEFT_ELBOW, POSE_BONE_LEFT_HAND, true, true, true);
        AddRagdollBone(body, POSE_BONE_LEFT_ELBOW);
        AddRagdollBone(body, POSE_BONE_LEFT_HAND);
        body->mass = 1.0f;
        body->length *= 1.25f;
        body->currentLength = body->length;
        body->thickness *= 1.5f;
        body->index = RAGDOLL_LEFT_FOREARM;
        SetLimit(body->limitMin.z, body->limitMinSin.z, -0.5f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, 0.0f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.1f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_CHEST], 1);
    m_bodies[RAGDOLL_RIGHT_UPPER_ARM] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_RIGHT_SHOULDER, POSE_BONE_RIGHT_ELBOW, true, true, true);
        AddRagdollBone(body, POSE_BONE_RIGHT_SHOULDER);
        body->mass = 1.0f;
        body->thickness *= 1.5f;
        body->index = RAGDOLL_RIGHT_UPPER_ARM;
        SetLimit(body->limitMax.z, body->limitMaxSin.z, 0.8f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, 0.0f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.707f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_RIGHT_UPPER_ARM], 2);
    m_bodies[RAGDOLL_RIGHT_FOREARM] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_RIGHT_ELBOW, POSE_BONE_RIGHT_HAND, true, true, true);
        AddRagdollBone(body, POSE_BONE_RIGHT_ELBOW);
        AddRagdollBone(body, POSE_BONE_RIGHT_HAND);
        body->mass = 1.0f;
        body->length *= 1.25f;
        body->currentLength = body->length;
        body->thickness *= 1.5f;
        body->index = RAGDOLL_RIGHT_FOREARM;
        SetLimit(body->limitMin.z, body->limitMinSin.z, -0.5f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.0f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.1f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_PELVIS], 1);
    m_bodies[RAGDOLL_LEFT_THIGH] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_LEFT_HIP, POSE_BONE_LEFT_KNEE, true, true, true);
        AddRagdollBone(body, POSE_BONE_LEFT_HIP);
        body->mass = 2.5f;
        body->thickness *= 1.5f;
        body->index = RAGDOLL_LEFT_THIGH;
        SetLimit(body->limitMax.z, body->limitMaxSin.z, -0.15f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.0f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, -0.5f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.707f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_LEFT_THIGH], 2);
    m_bodies[RAGDOLL_LEFT_SHIN] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_LEFT_KNEE, POSE_BONE_LEFT_FOOT, true, true, true);
        AddRagdollBone(body, POSE_BONE_LEFT_KNEE);
        AddRagdollBone(body, POSE_BONE_LEFT_FOOT);
        body->mass = 2.0f;
        body->index = RAGDOLL_LEFT_SHIN;
        PoseSetXYZ(body->unknown220, S_vector(0.0f, -1.0f, 0.0f));
        SetLimit(body->limitMin.z, body->limitMinSin.z, -0.707f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.0f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, 0.0f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.0f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_PELVIS], 1);
    m_bodies[RAGDOLL_RIGHT_THIGH] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_RIGHT_HIP, POSE_BONE_RIGHT_KNEE, true, true, true);
        AddRagdollBone(body, POSE_BONE_RIGHT_HIP);
        body->mass = 2.5f;
        body->thickness *= 1.5f;
        body->index = RAGDOLL_RIGHT_THIGH;
        SetLimit(body->limitMax.z, body->limitMaxSin.z, -0.15f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, 0.0f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.5f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.707f);
    }
    body = NewRagdollBody(m_bodies[RAGDOLL_RIGHT_THIGH], 2);
    m_bodies[RAGDOLL_RIGHT_SHIN] = body;
    if (body) {
        MeasureRagdollBody(body, POSE_BONE_RIGHT_KNEE, POSE_BONE_RIGHT_FOOT, true, true, true);
        AddRagdollBone(body, POSE_BONE_RIGHT_KNEE);
        AddRagdollBone(body, POSE_BONE_RIGHT_FOOT);
        body->mass = 2.0f;
        body->index = RAGDOLL_RIGHT_SHIN;
        PoseSetXYZ(body->unknown220, S_vector(0.0f, -1.0f, 0.0f));
        SetLimit(body->limitMin.z, body->limitMinSin.z, -0.707f);
        SetLimit(body->limitMax.y, body->limitMaxSin.y, 0.0f);
        SetLimit(body->limitMin.x, body->limitMinSin.x, 0.0f);
        SetLimit(body->limitMax.x, body->limitMaxSin.x, 0.0f);
    }

    // The points: where each part starts and ends.
    S_RagdollBody **b = m_bodies;
    auto end = [](const S_RagdollBody *part) {
        return PoseVector4(part->world(3) + part->world(2) * part->length, 0.0f);
    };
    auto start = [](const S_RagdollBody *part) { return PoseRow4(part->world, 3); };
    S_RagdollPoint **p = m_points;
    p[RAGDOLL_POINT_PELVIS] = NewRagdollPoint(nullptr, start(b[0]), b[0], b[7], b[9], nullptr, b[0], nullptr);
    p[RAGDOLL_POINT_CHEST] = NewRagdollPoint(p[0], start(b[1]), b[1], b[0], nullptr, nullptr, b[1], b[0]);
    p[RAGDOLL_POINT_NECK] = NewRagdollPoint(p[1], end(b[1]), b[1], b[3], b[5], b[2], b[2], b[1]);
    p[RAGDOLL_POINT_HEAD] = NewRagdollPoint(p[2], end(b[2]), b[2], nullptr, nullptr, nullptr, nullptr, b[2]);
    p[RAGDOLL_POINT_LEFT_SHOULDER] = NewRagdollPoint(p[2], start(b[3]), b[3], b[1], nullptr, nullptr, b[3], nullptr);
    p[RAGDOLL_POINT_LEFT_ELBOW] = NewRagdollPoint(p[4], end(b[3]), b[3], b[4], nullptr, nullptr, b[4], b[3]);
    p[RAGDOLL_POINT_LEFT_HAND] = NewRagdollPoint(p[5], end(b[4]), b[4], nullptr, nullptr, nullptr, nullptr, b[4]);
    p[RAGDOLL_POINT_RIGHT_SHOULDER] = NewRagdollPoint(p[2], start(b[5]), b[5], b[1], nullptr, nullptr, b[5], nullptr);
    p[RAGDOLL_POINT_RIGHT_ELBOW] = NewRagdollPoint(p[7], end(b[5]), b[5], b[6], nullptr, nullptr, b[6], b[5]);
    p[RAGDOLL_POINT_RIGHT_HAND] = NewRagdollPoint(p[8], end(b[6]), b[6], nullptr, nullptr, nullptr, nullptr, b[6]);
    p[RAGDOLL_POINT_LEFT_HIP] = NewRagdollPoint(p[0], start(b[7]), b[7], b[0], nullptr, nullptr, b[7], nullptr);
    p[RAGDOLL_POINT_LEFT_KNEE] = NewRagdollPoint(p[10], end(b[7]), b[7], b[8], nullptr, nullptr, b[8], b[7]);
    p[RAGDOLL_POINT_LEFT_FOOT] = NewRagdollPoint(p[11], end(b[8]), b[8], nullptr, nullptr, nullptr, nullptr, b[8]);
    p[RAGDOLL_POINT_RIGHT_HIP] = NewRagdollPoint(p[0], start(b[9]), b[9], b[0], nullptr, nullptr, b[9], nullptr);
    p[RAGDOLL_POINT_RIGHT_KNEE] = NewRagdollPoint(p[13], end(b[9]), b[9], b[10], nullptr, nullptr, b[10], b[9]);
    p[RAGDOLL_POINT_RIGHT_FOOT] = NewRagdollPoint(p[14], end(b[10]), b[10], nullptr, nullptr, nullptr, nullptr, b[10]);
    for (int i = 0; i < RAGDOLL_POINT_COUNT; ++i)
        p[i]->index = i;
    m_rootPoint = p[RAGDOLL_POINT_PELVIS];

    // The twist of the pelvis and the chest: the line between the hips / shoulders (off the
    // part's axis) turned onto x.
    struct S_Twist {
        int body;
        int left;
        int right;
    };
    const S_Twist twists[2] = {{RAGDOLL_PELVIS, RAGDOLL_POINT_LEFT_HIP, RAGDOLL_POINT_RIGHT_HIP},
                               {RAGDOLL_CHEST, RAGDOLL_POINT_LEFT_SHOULDER, RAGDOLL_POINT_RIGHT_SHOULDER}};
    for (const S_Twist &twist : twists) {
        S_RagdollBody *part = b[twist.body];
        // (The axis is not measured yet: zero here, so nothing is taken off.)
        const S_vector axis = PoseXYZ(part->axis);
        S_vector left = PoseXYZ(p[twist.left]->pos) - part->world(3);
        left -= axis * left.Dot(axis);
        S_vector right = PoseXYZ(p[twist.right]->pos) - part->world(3);
        right -= axis * right.Dot(axis);
        S_vector across = right - left;
        across.Normalize();
        PoseRotationBetween(PoseIntoAxes(part->world, across), S_vector(1.0f, 0.0f, 0.0f), part->lateral);
    }

    // The local matrices of the bones the parts keep straight (restored when the body falls).
    if (m_restMatrices)
        _aligned_free(m_restMatrices);
    m_restMatrices = static_cast<S_matrix *>(_aligned_malloc(RAGDOLL_REST_BONE_COUNT * sizeof(S_matrix), 16));
    for (int i = 0; i < RAGDOLL_REST_BONE_COUNT; ++i)
        m_restMatrices[i] = m_bones[g_ragdollRestBones[i]].frame->GetLocalMatrix();

    if (m_pushes)
        _aligned_free(m_pushes);
    m_pushes = AlignedNew<S_RagdollPush>(RAGDOLL_PUSH_COUNT * sizeof(S_RagdollPush));
    if (m_holds)
        _aligned_free(m_holds);
    m_holds = AlignedNew<S_RagdollPush>(RAGDOLL_HOLD_COUNT * sizeof(S_RagdollPush));
}

// ---- saved games ------------------------------------------------------------------------

namespace {

int BodyNumber(const S_RagdollBody *body) { return body ? body->index + 1 : 0; }
int PointNumber(const S_RagdollPoint *point) { return point ? point->index + 1 : 0; }

void WriteQuat(C_chunk &chunk, const S_quat &q)
{
    float f[4];
    q.ToFile(f);
    chunk.Write(f, sizeof(f));
}

void ReadQuat(C_chunk &chunk, S_quat &q)
{
    float f[4];
    chunk.Read(f, sizeof(f));
    q = S_quat::FromFile(f);
}

}  // namespace

// 0x007c3c90: `body` and the parts below it.
void C_HumanWeaponPose::SaveRagdollBodies(C_chunk &chunk, const S_RagdollBody *body)
{
    for (; body; body = body->children[2]) {
        int number = BodyNumber(body->parent);
        chunk.Write(&number, 4);
        for (int i = 0; i < 3; ++i) {
            number = BodyNumber(body->children[i]);
            chunk.Write(&number, 4);
        }
        chunk.Write(&body->local, sizeof(S_matrix));
        chunk.Write(&body->world, sizeof(S_matrix));
        WriteQuat(chunk, body->rot);
        chunk.Write(&body->rest, sizeof(S_matrix));
        chunk.Write(&body->from, sizeof(S_matrix));
        WriteQuat(chunk, body->restRot);
        WriteQuat(chunk, body->fromRot);
        chunk.Write(&body->fromLength, 4);
        chunk.Write(&body->restLength, 4);
        chunk.Write(&body->savedStart, 12);
        chunk.Write(&body->savedEnd, 12);
        chunk.Write(&body->roll, 4);
        chunk.Write(&body->length, 4);
        chunk.Write(&body->savedLength, 4);
        chunk.Write(&body->thickness, 4);
        chunk.Write(&body->startBone, 4);
        chunk.Write(&body->endBone, 4);
        chunk.Write(&body->mass, 4);
        chunk.Write(&body->unknown19C, 4);
        chunk.Write(&body->unknown1A0, 4);
        chunk.Write(&body->boneCount, 4);
        for (int i = 0; i < body->boneCount; ++i)
            chunk.Write(&body->bones[i], 4);
        for (int i = 0; i < body->boneCount; ++i)
            chunk.Write(&body->boneOffsets[i], sizeof(S_matrix));
        chunk.Write(&body->limitMin, 12);
        chunk.Write(&body->limitMinSin, 12);
        chunk.Write(&body->limitMax, 12);
        chunk.Write(&body->limitMaxSin, 12);
        number = PointNumber(body->start);
        chunk.Write(&number, 4);
        number = PointNumber(body->end);
        chunk.Write(&number, 4);
        chunk.Write(&body->axis, 12);
        chunk.Write(&body->savedAxis, 12);
        chunk.Write(&body->unknown220, 12);
        chunk.Write(&body->lateral, sizeof(S_matrix));
        chunk.Write(&body->currentLength, 4);
        if (body->children[0])
            SaveRagdollBodies(chunk, body->children[0]);
        if (body->children[1])
            SaveRagdollBodies(chunk, body->children[1]);
    }
}

// 0x007c42a0: reads `body` and the parts below it into the built ragdoll (parts the saved one
// lost are freed).
void C_HumanWeaponPose::LoadRagdollBodies(C_chunk &chunk, S_RagdollBody *body)
{
    int number = 0;
    chunk.Read(&number, 4);
    body->parent = number ? m_bodies[number - 1] : nullptr;
    for (int i = 0; i < 3; ++i) {
        chunk.Read(&number, 4);
        if (number == 0) {
            FreeRagdollBody(body->children[i]);
            body->children[i] = nullptr;
        } else {
            body->children[i] = m_bodies[number - 1];
        }
    }
    chunk.Read(&body->local, sizeof(S_matrix));
    chunk.Read(&body->world, sizeof(S_matrix));
    ReadQuat(chunk, body->rot);
    chunk.Read(&body->rest, sizeof(S_matrix));
    chunk.Read(&body->from, sizeof(S_matrix));
    ReadQuat(chunk, body->restRot);
    ReadQuat(chunk, body->fromRot);
    chunk.Read(&body->fromLength, 4);
    chunk.Read(&body->restLength, 4);
    chunk.Read(&body->savedStart, 12);
    chunk.Read(&body->savedEnd, 12);
    chunk.Read(&body->roll, 4);
    chunk.Read(&body->length, 4);
    chunk.Read(&body->savedLength, 4);
    chunk.Read(&body->thickness, 4);
    chunk.Read(&body->startBone, 4);
    chunk.Read(&body->endBone, 4);
    chunk.Read(&body->mass, 4);
    chunk.Read(&body->unknown19C, 4);
    chunk.Read(&body->unknown1A0, 4);
    chunk.Read(&body->boneCount, 4);
    for (int i = 0; i < body->boneCount; ++i)
        chunk.Read(&body->bones[i], 4);
    for (int i = 0; i < body->boneCount; ++i)
        chunk.Read(&body->boneOffsets[i], sizeof(S_matrix));
    chunk.Read(&body->limitMin, 12);
    chunk.Read(&body->limitMinSin, 12);
    chunk.Read(&body->limitMax, 12);
    chunk.Read(&body->limitMaxSin, 12);
    chunk.Read(&number, 4);
    body->start = number ? m_points[number - 1] : nullptr;
    chunk.Read(&number, 4);
    body->end = number ? m_points[number - 1] : nullptr;
    chunk.Read(&body->axis, 12);
    chunk.Read(&body->savedAxis, 12);
    chunk.Read(&body->unknown220, 12);
    chunk.Read(&body->lateral, sizeof(S_matrix));
    chunk.Read(&body->currentLength, 4);
    for (int i = 0; i < 3; ++i)
        if (body->children[i])
            LoadRagdollBodies(chunk, body->children[i]);
}

// 0x007c4a70: `point` and the points below it (a contact's frame by its name, if the scene
// still has it).
void C_HumanWeaponPose::SaveRagdollPoints(C_chunk &chunk, const S_RagdollPoint *point)
{
    for (; point; point = point->children[2]) {
        int number;
        for (int i = 0; i < 4; ++i) {
            number = BodyNumber(point->bodies[i]);
            chunk.Write(&number, 4);
        }
        number = PointNumber(point->parent);
        chunk.Write(&number, 4);
        for (int i = 0; i < 3; ++i) {
            number = PointNumber(point->children[i]);
            chunk.Write(&number, 4);
        }
        chunk.Write(&point->weight, 4);
        chunk.Write(&point->pos, 12);
        chunk.Write(&point->oldPos, 12);
        chunk.Write(&point->velocity, 12);
        chunk.Write(&point->gravityStep, 12);
        chunk.Write(&point->step, 12);
        chunk.Write(&point->force, 12);
        chunk.Write(&point->localPos, 12);
        chunk.Write(&point->smoothVelocity, 12);
        chunk.Write(&point->target, 12);
        chunk.Write(&point->lastPos, 12);
        chunk.Write(&point->parentLocalPos, 12);
        chunk.Write(&point->normal, 12);
        chunk.Write(&point->contactFramePos, 12);
        const I3D_frame *frame = point->contactFrame;
        chunk.WriteSizedString(frame && frame->GetRefCount() >= 2 ? frame->GetName() : "");
        chunk.Write(&point->material, 4);
        const int flags = point->flags;
        chunk.Write(&flags, 4);
        if (point->children[0])
            SaveRagdollPoints(chunk, point->children[0]);
        if (point->children[1])
            SaveRagdollPoints(chunk, point->children[1]);
    }
}

// 0x007c4f40
void C_HumanWeaponPose::LoadRagdollPoints(C_chunk &chunk, S_RagdollPoint *point)
{
    int number = 0;
    for (int i = 0; i < 4; ++i) {
        chunk.Read(&number, 4);
        point->bodies[i] = number ? m_bodies[number - 1] : nullptr;
    }
    chunk.Read(&number, 4);
    point->parent = number ? m_points[number - 1] : nullptr;
    for (int i = 0; i < 3; ++i) {
        chunk.Read(&number, 4);
        if (number == 0) {
            FreeRagdollPoint(point->children[i]);
            point->children[i] = nullptr;
        } else {
            point->children[i] = m_points[number - 1];
        }
    }
    chunk.Read(&point->weight, 4);
    chunk.Read(&point->pos, 12);
    chunk.Read(&point->oldPos, 12);
    chunk.Read(&point->velocity, 12);
    chunk.Read(&point->gravityStep, 12);
    chunk.Read(&point->step, 12);
    chunk.Read(&point->force, 12);
    chunk.Read(&point->localPos, 12);
    chunk.Read(&point->smoothVelocity, 12);
    chunk.Read(&point->target, 12);
    chunk.Read(&point->lastPos, 12);
    chunk.Read(&point->parentLocalPos, 12);
    chunk.Read(&point->normal, 12);
    chunk.Read(&point->contactFramePos, 12);
    char name[0x80];
    name[0] = '\0';
    chunk.ReadSizedString(name, sizeof(name));
    point->contactFrame = nullptr;
    if (name[0])
        point->contactFrame = GetGame()->GetScene()->FindFrame(name, RAGDOLL_CONTACT_FRAME_MASK);
    if (point->contactFrame)
        point->contactFrame->AddRef();
    chunk.Read(&point->material, 4);
    int flags = 0;
    chunk.Read(&flags, 4);
    point->flags = static_cast<uint8_t>(flags);
    for (int i = 0; i < 3; ++i)
        if (point->children[i])
            LoadRagdollPoints(chunk, point->children[i]);
}

// ---- posing the model -------------------------------------------------------------------

// 0x007c5e20: the world matrices of the bones the parts carry (into S_PoseBone::poseMatrix).
void C_HumanWeaponPose::RagdollBoneMatrices(const S_RagdollBody *body)
{
    for (; body; body = body->children[2]) {
        for (int i = 0; i < body->boneCount; ++i)
            m_bones[body->bones[i]].poseMatrix = body->boneOffsets[i] * body->world;
        if (body->children[0])
            RagdollBoneMatrices(body->children[0]);
        if (body->children[1])
            RagdollBoneMatrices(body->children[1]);
    }
}

// 0x007c5ef0: the frames of the bones the parts carry to those world matrices (their rotation
// slerped from the matrix's towards the frame's own by `weight` below 1).
void C_HumanWeaponPose::SetRagdollBones(const S_RagdollBody *body, float weight)
{
    for (int i = 0; i < body->boneCount; ++i) {
        S_PoseBone &bone = m_bones[body->bones[i]];
        I3D_frame *parent = bone.parent ? bone.parent->frame : m_model;
        S_matrix m = bone.poseMatrix * PoseInverseRotation(parent->GetMatrix());
        I3D_frame *frame = bone.frame;
        const S_vector &pos = frame->GetPos();
        m.m[3][0] = pos.x;
        m.m[3][1] = pos.y;
        m.m[3][2] = pos.z;
        m.m[3][3] = 1.0f;
        if (weight < 1.0f) {
            S_quat current;
            current.Make(frame->GetLocalMatrix());
            S_quat target;
            target.Make(m);
            m.SetRot3(target.Slerp(current, weight, false));
        }
        PoseOrthonormalize(m);
        PoseSetFrameMatrix(frame, m);
    }
    for (int i = 0; i < 3; ++i)
        if (body->children[i])
            SetRagdollBones(body->children[i], weight);
}

// 0x007c5d10: poses the model's bones by the parts below `body` (null: all), moves the model
// so that its root bone is where the root part starts and puts the owner there.
void C_HumanWeaponPose::PoseFromRagdoll(const S_RagdollBody *body, float weight)
{
    if (!body)
        body = m_rootBody;
    RagdollBoneMatrices(body);
    SetRagdollBones(body, weight);
    const S_vector offset = m_rootBody->world(3) - m_bones[m_rootBody->startBone].frame->GetWorldPos();
    m_model->SetPos(offset + m_model->GetWorldPos());
    m_model->Update();
    m_owner->SetPosition4(PoseWorldPos4(m_model));
}

// ---- the rest pose ------------------------------------------------------------------------

// 0x007cdfd0: the parts as they lie now become their rest pose (against their parents).
void C_HumanWeaponPose::StoreRagdollRest(S_RagdollBody *body)
{
    for (; body; body = body->children[2]) {
        if (!body->parent) {
            body->rest = body->world;
        } else {
            const S_matrix &parent = body->parent->world;
            body->rest = body->world;
            body->rest.m[3][0] -= parent.m[3][0];
            body->rest.m[3][1] -= parent.m[3][1];
            body->rest.m[3][2] -= parent.m[3][2];
            body->rest.m[3][3] = 1.0f;
            body->rest *= PoseInverseRotation(parent);
        }
        body->restRot.Make(body->rest);
        body->restLength = body->currentLength;
        if (body->children[0])
            StoreRagdollRest(body->children[0]);
        if (body->children[1])
            StoreRagdollRest(body->children[1]);
    }
}

// 0x007cdef0: the parts back in their rest pose (the blend starting from there).
void C_HumanWeaponPose::ResetRagdollToRest(S_RagdollBody *body)
{
    for (; body; body = body->children[2]) {
        body->from = body->rest;
        body->fromRot = body->restRot;
        body->fromLength = body->restLength;
        body->currentLength = body->restLength;
        body->world = body->parent ? body->rest * body->parent->world : body->rest;
        if (body->children[0])
            ResetRagdollToRest(body->children[0]);
        if (body->children[1])
            ResetRagdollToRest(body->children[1]);
    }
}

// 0x007ce0f0: the parts blended from their `from` pose to the rest pose by `t`.
void C_HumanWeaponPose::BlendRagdollToRest(S_RagdollBody *body, float t)
{
    const float u = 1.0f - t;
    for (; body; body = body->children[2]) {
        body->rot = body->fromRot.Slerp(body->restRot, t, false);
        body->local.SetRot3(body->rot);
        body->local.m[3][0] = u * body->from.m[3][0] + t * body->rest.m[3][0];
        body->local.m[3][1] = u * body->from.m[3][1] + t * body->rest.m[3][1];
        body->local.m[3][2] = u * body->from.m[3][2] + t * body->rest.m[3][2];
        body->local.m[3][3] = 1.0f;
        body->world = body->parent ? body->local * body->parent->world : body->local;
        body->currentLength = t * body->restLength + u * body->fromLength;
        if (body->children[0])
            BlendRagdollToRest(body->children[0], t);
        if (body->children[1])
            BlendRagdollToRest(body->children[1], t);
    }
}

// 0x007c6560: moves `point` (null: the root) and the points below it by the same offset to
// `pos`, stopped (`stop` also takes away the speed the old position gives), and wakes the
// ragdoll.
void C_HumanWeaponPose::PlaceRagdollPoint(S_RagdollPoint *point, const S_vector4 &pos, bool stop)
{
    if (!point)
        point = m_rootPoint;
    m_groundTime = 4.0f;
    m_ragdollWeight = 1.0f;
    m_settleDelay = 1.0f;
    const S_vector offset = PoseXYZ(pos) - PoseXYZ(point->pos);
    point->pos = pos;
    if (stop)
        point->oldPos = pos;
    point->lastPos = pos;
    ClearXYZ(point->velocity);
    ClearXYZ(point->gravityStep);
    ClearXYZ(point->step);
    point->weight = 1.0f;
    point->flags = 0;
    ClearXYZ(point->smoothVelocity);
    ClearXYZ(point->force);
    for (S_RagdollBody *body : point->bodies)
        if (body && body->start == point) {
            body->world.m[3][0] = pos.x;
            body->world.m[3][1] = pos.y;
            body->world.m[3][2] = pos.z;
            body->world.m[3][3] = pos.w;
        }
    for (S_RagdollPoint *child : point->children) {
        if (!child)
            continue;
        const S_vector4 moved = PoseVector4(PoseXYZ(child->pos) + offset, child->pos.w);
        PlaceRagdollPoint(child, moved, stop);
    }
}
