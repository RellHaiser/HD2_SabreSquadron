// C_Vehicle: a run's start and end (original 0x004ea780 Start, 0x004ec5c0 Stop), the restart after
// the car was moved (0x004e9db0 Restart) and their steps:
// 0x004f3950 SetSeatLoad, 0x004fe910 RepairParts, 0x004feea0 ResetCondition, 0x004fe850
// LoadGlassTextures, 0x004f6080 AddLightsToSectors, 0x004f61e0 RemoveLights.
#include "game/vehicle.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "game/pending.h"                               // g_carScene, g_tyreTypes
#include "game/resource_cache.h"                        // g_modelCache
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

constexpr float PI = 3.1415927f;                        // 0x0080f7ac
constexpr float HALF_PI = 1.5707964f;                   // 0x00810180

// The collision volumes' types (S_CollisionVolume::type; meanings not identified).
constexpr uint32_t VOLUME_TYPE_BODY = 0x201;            // the body's and the special volumes
constexpr uint32_t VOLUME_TYPE_SECOND = 0x202;          // the second volume (+0x490)

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

void SetXYZ(S_vector4 &v, const S_vector &xyz)
{
    v.x = xyz.x;
    v.y = xyz.y;
    v.z = xyz.z;
}

S_vector4 Row(const S_matrix &m, int row)
{
    return {m.m[row][0], m.m[row][1], m.m[row][2], m.m[row][3]};
}

// The inverse of `m` written into `out` (`out` keeps `m` when it is singular).
void InvertInto(S_matrix &out, const S_matrix &m)
{
    const S_matrix copy = m;
    out = copy;
    out.Inverse(copy);
}

// The farthest of the corners of the box `min`..`max` from the origin.
float FarthestCorner(const S_vector &min, const S_vector &max)
{
    const S_vector corners[8] = {
        min,
        max,
        S_vector(min.x, min.y, max.z),
        S_vector(min.x, max.y, min.z),
        S_vector(min.x, max.y, max.z),
        S_vector(max.x, min.y, min.z),
        S_vector(max.x, min.y, max.z),
        S_vector(max.x, max.y, min.z),
    };
    float farthest = 0.0f;
    for (int i = 8; i-- > 0;) {
        const float distance = corners[i].Magnitude();
        if (distance > farthest)
            farthest = distance;
    }
    return farthest;
}

// The quaternion product a b in the original's order of operations.
S_quat Multiply(const S_quat &a, const S_quat &b)
{
    S_quat q;
    q.s = a.s * b.s - ((a.v.y * b.v.y + a.v.z * b.v.z) + b.v.x * a.v.x);
    const S_vector cross(a.v.y * b.v.z - a.v.z * b.v.y, a.v.z * b.v.x - b.v.z * a.v.x,
                         b.v.y * a.v.x - a.v.y * b.v.x);
    const S_vector fromA(b.s * a.v.x, a.v.y * b.s, a.v.z * b.s);
    const S_vector fromB(a.s * b.v.x, a.s * b.v.y, a.s * b.v.z);
    q.v = S_vector(fromA.x + fromB.x, fromA.y + fromB.y, fromA.z + fromB.z) + cross;
    return q;
}

// Rewrites `to`'s vertex positions with `from`'s (0x20-byte vertices, position first).
void CopyPositions(I3D_mesh_level *from, I3D_mesh_level *to)
{
    const auto *src = static_cast<const uint8_t *>(from->GetVertices(0));
    auto *dst = static_cast<uint8_t *>(to->GetVertices(0));
    for (int v = to->NumVertices(); v-- > 0;)
        std::memcpy(dst + v * 0x20, src + v * 0x20, sizeof(S_vector));
    from->UnlockVertices();
    to->UnlockVertices();
}

}  // namespace

// --- Start / Stop / Restart ------------------------------------------------------------------

void C_Vehicle::ReadModelPlace()
{
    m_modelMatrix = m_model->GetMatrix();
    InvertInto(m_invModelMatrix, m_model->GetMatrix());
    m_unknown310 = m_modelMatrix;
    m_unknown350 = m_invModelMatrix;
    m_unknown310.m[3][0] = m_unknown310.m[3][1] = m_unknown310.m[3][2] = 0.0f;
    m_unknown350.m[3][0] = m_unknown350.m[3][1] = m_unknown350.m[3][2] = 0.0f;
    m_modelAxisX = Row(m_modelMatrix, 0);
    m_modelAxisY = Row(m_modelMatrix, 1);
    m_modelAxisZ = Row(m_modelMatrix, 2);
    m_modelPos = Row(m_model->GetMatrix(), 3);
    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);
    m_unknown544 = m_model->GetParent();
    g_carScene->UpdateFrameSector(m_model, XYZ(m_unknown1B0));
}

void C_Vehicle::ReadModelState()                                                // 0x004f5d10
{
    m_modelMatrix = m_model->GetMatrix();
    InvertInto(m_invModelMatrix, m_model->GetMatrix());
    m_unknown310 = m_modelMatrix;
    m_unknown310.m[3][0] = m_unknown310.m[3][1] = m_unknown310.m[3][2] = 0.0f;
    m_unknown350 = m_invModelMatrix;
    m_unknown350.m[3][0] = m_unknown350.m[3][1] = m_unknown350.m[3][2] = 0.0f;
    m_modelPos = Row(m_model->GetMatrix(), 3);
    SetXYZ(m_unknown1B0, XYZ(m_unknown1E0) * m_modelMatrix);
    m_modelAxisX = Row(m_modelMatrix, 0);
    m_modelAxisY = Row(m_modelMatrix, 1);
    m_modelAxisZ = Row(m_modelMatrix, 2);
    m_velocityDir = m_velocity;
    S_vector direction = XYZ(m_velocityDir);
    direction.Normalize();
    SetXYZ(m_velocityDir, direction);
    m_unknown229F = 0;
    m_speed = XYZ(m_velocity).Magnitude();
}

void C_Vehicle::Start()                                                         // 0x004ea780 (DS 0x004da0f0)
{
    m_previousSpeed = 0.0f;
    m_unknown7C4 = 0.02f;
    m_unknown22C6 = 0;
    m_unknown508 = 1.0f;
    m_unknown50C = 1.0f;
    m_unknown229E = 0;
    m_unknown229F = 0;
    m_tracked = !m_tracksL.empty() || !m_tracksR.empty();
    if (m_tracked || m_unknown550 == 2)
        m_unknownF7C = 0;
    m_unknown554 = 0;
    m_unknown228B = 0;
    m_unknown5B0 = 0;
    m_unknown5B4 = 0;
    m_unknown908 = 1.0f;
    m_unknown90C = 1.0f;
    m_unknown910 = 0x20;

    m_model->Update();
    ReadModelPlace();
    m_unknown6E8 = 4.0f;

    // The turning circle: the reach of the first and last axles' steering lines (sine rule over
    // the wheelbase) plus the widest step between neighbouring wheels, x 1.2.
    if (!(m_unknownF74 & 0x2)) {
        // Fix (here and in the wheel loops below): missing wheels (the null slots Setup allows)
        // are left out; the original reads through them.
        float track = 0.0f;
        for (int i = 1; i < m_wheelCount; ++i) {
            if (!m_wheels[i] || !m_wheels[i - 1])
                continue;
            const float step = m_wheels[i]->modelPos.x - m_wheels[i - 1]->modelPos.x;
            if (step > track)
                track = step;
        }
        const S_VehicleWheel *front = m_wheels[0];
        const S_VehicleWheel *back = m_wheels[m_wheelCount - 2];
        if (front && back) {
            const float wheelbase = front->modelPos.z - back->modelPos.z;
            const float frontAngle = (front->flags & WHEEL_STEERED) ? m_unknown92C : HALF_PI;
            const float backAngle = (back->flags & WHEEL_STEERED) ? m_unknown92C : HALF_PI;
            const float reach = std::sin(backAngle) * std::sin(frontAngle) * wheelbase /
                                std::sin(PI - (backAngle + frontAngle));
            m_unknown6EC = m_unknown6E8 = (track + reach) * 1.2f;
        }
    }

    // The body's volume: the CENTRE box 0.1 inside, its sphere around the model's origin.
    const S_vector bodyMax = XYZ(m_bodyMax);
    const float bodyRadius = FarthestCorner(XYZ(m_bodyMin), bodyMax) + 0.1f;
    m_bodyVolume = S_CollisionVolume();
    m_bodyVolume.flags = m_bodyCollisionDesc.unknown00;
    const S_matrix &world = m_model->GetMatrix();
    m_bodyVolume.center = world(3);
    m_bodyVolume.centerW = world.m[3][3];
    m_bodyVolume.radius = bodyRadius;
    m_bodyVolume.bbox.min = S_vector(m_bodyMin.x + 0.1f, m_bodyMin.y + 0.1f, m_bodyMin.z + 0.1f);
    m_bodyVolume.bbox.max = S_vector(m_bodyMax.x - 0.1f, m_bodyMax.y - 0.1f, m_bodyMax.z - 0.1f);
    m_modelMatrix = m_model->GetMatrix();
    m_bodyVolume.matrix = &m_modelMatrix;
    InvertInto(m_invModelMatrix, m_modelMatrix);
    m_bodyMin.y -= 0.5f;
    m_bodyVolume.invMatrix = &m_invModelMatrix;
    m_bodyVolume.frame = m_model;
    m_bodyVolume.actor = static_cast<C_Actor *>(m_callbackContext);
    m_bodyVolume.type = VOLUME_TYPE_BODY;

    // The second volume: the CENTRE box 0.5 deeper.
    const float secondRadius = FarthestCorner(XYZ(m_bodyMin), bodyMax) + 0.1f;
    m_volume490 = S_CollisionVolume();
    m_volume490.flags = m_bodyCollisionDesc.unknown00;
    const S_matrix &world2 = m_model->GetMatrix();
    m_volume490.center = world2(3);
    m_volume490.centerW = world2.m[3][3];
    m_volume490.type = VOLUME_TYPE_SECOND;
    m_volume490.radius = secondRadius;
    m_volume490.bbox.min = XYZ(m_bodyMin);
    m_volume490.bbox.minW = m_bodyMin.w;
    m_volume490.bbox.max = bodyMax;
    m_volume490.bbox.maxW = m_bodyMax.w;
    m_bodyMin.y += 0.5f;                                // (y - 0.5) + 0.5: may differ in the last bit
    m_volume490.frame = m_model;
    m_volume490.matrix = &m_modelMatrix;
    m_volume490.invMatrix = &m_invModelMatrix;
    m_volume490.actor = static_cast<C_Actor *>(m_callbackContext);

    // The special volumes: their boxes, on a moving part with its world matrix.
    for (int i = 0; i < static_cast<int>(m_specialCollisionCorners.size()) / 2; ++i) {
        const S_vector4 &min = m_specialCollisionCorners[2 * i];
        const S_vector4 &max = m_specialCollisionCorners[2 * i + 1];
        S_CollisionVolume volume;
        volume.flags = m_specialCollisionDescs[i].unknown00;
        volume.radius = FarthestCorner(XYZ(min), XYZ(max)) + 0.1f;
        volume.bbox.min = XYZ(min);
        volume.bbox.minW = min.w;
        volume.bbox.max = XYZ(max);
        volume.bbox.maxW = max.w;
        // Fix: a second Start without a Stop reads records past the end (the original takes the
        // record by the volumes' count).
        const size_t index = m_specialVolumes.size();
        if (index >= m_specialCollisions.size())
            break;
        S_VehicleSpecialCollision &special = m_specialCollisions[index];
        if (special.parent) {
            const S_matrix &parentWorld = special.parent->GetMatrix();
            volume.matrix = &parentWorld;
            volume.invMatrix = &special.invParentMatrix;
            volume.center = XYZ(special.localPos) * parentWorld;
        } else {
            volume.matrix = &m_modelMatrix;
            volume.invMatrix = &m_invModelMatrix;
            const S_matrix &modelWorld = m_model->GetMatrix();
            volume.center = modelWorld(3);
            volume.centerW = modelWorld.m[3][3];
        }
        volume.frame = m_model;
        volume.actor = static_cast<C_Actor *>(m_callbackContext);
        volume.type = VOLUME_TYPE_BODY;
        m_specialVolumes.push_back(volume);
    }
    for (S_CollisionVolume &volume : m_specialVolumes)
        m_specialVolumeList.push_back(&volume);

    m_driven = 0;
    m_unknown58C = 0;
    m_unknown270 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown280 = S_vector(0.0f, 0.0f, 0.0f);
    m_specialVolumesOn = false;
    m_mainVolumesInWorld = false;
    m_volume490InWorld = false;
    SetVolumesInWorld(true, true);

    // The driving state.
    m_unknown2293 = 0;
    std::fill(std::begin(m_unknownF80), std::end(m_unknownF80), 0u);
    m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
    m_unknown5F0 = 0.0f;
    m_unknown710 = 0.0f;
    m_velocityDir = m_velocity;
    m_unknown54C = 0;
    m_unknown22C0 = 0;
    m_unknown2224 = 0.0f;
    m_speed = 0.0f;
    m_previousSpeed = 0.0f;
    m_unknown8D0 = 0.0f;
    m_unknown600 = 1;
    m_unknown548 = 1.0f;
    m_unknown8D4 = 0.0f;
    m_engineRevs = 0.0f;
    m_previousRevs = 0.0f;
    SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
    m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    m_engineOn = false;
    m_unknown22A2 = 0;
    m_unknown78C = 0.0f;
    m_handbrake = 0.0f;
    m_unknown724 = 0.0f;
    m_unknown904 = 1.0f;
    m_gear = 1;
    m_requestedGear = 1;
    m_unknown790 = 1000.0f;
    m_brake = 0.0f;
    m_unknown22A0 = 0;
    m_unknown85C = 0.0f;
    m_unknown860 = 0.0f;
    m_steering = 0.0f;
    m_unknown2294 = 0;
    m_unknown2295 = 0;
    m_unknown780 = 0.0f;
    m_throttle = 0.0f;
    m_unknown700 = 0.0f;
    m_unknown2299 = 0;
    m_unknown229A = 0;
    m_unknown229B = 0;
    m_unknown229C = 0;
    m_fuel = 0.0f;
    m_unknown958 = 0.0f;
    m_unknown94C = 0.0f;
    m_sinceRimScrape = 30000;
    m_unknown604 = 0;
    m_unknown714 = 0.0f;
    m_unknown718 = 0.0f;
    m_unknown8E0 = 0.0f;
    SetXYZ(m_frontAxleCentre, S_vector(0.0f, 0.0f, 0.0f));
    SetXYZ(m_rearAxleCentre, S_vector(0.0f, 0.0f, 0.0f));
    if (m_wheelCount > 1) {
        // The middles of the first and the last axle (the w words keep the original's stack
        // garbage out: left as they were).
        const auto middle = [](S_vector4 &out, const S_VehicleWheel *left, const S_VehicleWheel *right) {
            if (!left || !right)
                return;
            const S_vector half((right->modelPos.x - left->modelPos.x) * 0.5f,
                                (right->modelPos.y - left->modelPos.y) * 0.5f,
                                (right->modelPos.z - left->modelPos.z) * 0.5f);
            SetXYZ(out, S_vector(half.x + left->modelPos.x, half.y + left->modelPos.y, half.z + left->modelPos.z));
        };
        middle(m_frontAxleCentre, m_wheels[0], m_wheels[1]);
        middle(m_rearAxleCentre, m_wheels[m_wheelCount - 2], m_wheels[m_wheelCount - 1]);
    }

    // The wheels: positions, state, pose (frame and holder straight, the camber / toe turns in
    // unknown000 / unknown010) and the tyre's entry for the surface.
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)
            continue;
        I3D_frame *frame = wheel->frame;
        wheel->worldPos = Row(frame->GetMatrix(), 3);
        const float reach = wheel->unknown1E4;
        const S_vector down(reach * m_modelAxisY.x, m_modelAxisY.y * reach, m_modelAxisY.z * reach);
        SetXYZ(wheel->unknown050, XYZ(wheel->worldPos) - down);
        SetXYZ(wheel->unknown060, XYZ(wheel->unknown050) - XYZ(m_modelAxisY));
        wheel->skidMark = 0;
        wheel->extension = 0.0f;
        wheel->flags &= WHEEL_KEPT_BY_START;
        wheel->unknown0A0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown0B0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown1F0 = 0.0f;
        wheel->surface = 0;
        std::fill(std::begin(wheel->unknown25C), std::end(wheel->unknown25C), 0.0f);
        wheel->unknown26C = 1000.0f;
        wheel->ground = nullptr;
        frame->SetOn(true);
        wheel->surfaceEntry = 0;
        wheel->unknown168 = 0.0f;
        wheel->unknown1A8 = 0.0f;
        wheel->unknown0C0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown0D0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->groundPlane = {S_vector(0.0f, 1.0f, 0.0f), 0.0f};
        wheel->groundPlaneVolume = nullptr;
        // Quirk kept: the first tyre type (index 0) is never looked up.
        if (wheel->tyreIndex > 0) {
            // Fix: a type without a table counts as no entry (the original reads through it).
            const std::vector<S_TyreSurface> *surfaces = g_tyreTypes[wheel->tyreIndex].surfaces;
            int entry = surfaces ? static_cast<int>(surfaces->size()) : 0;
            while (--entry >= 0 && (*surfaces)[entry].surface != wheel->surface) {
            }
            wheel->surfaceEntry = entry;
        }
        frame->SetLocalDir3(S_vector(0.0f, 0.0f, 1.0f), S_vector(0.0f, 1.0f, 0.0f));
        frame->Update();
        const bool right = i % 2 != 0;
        float angle = wheel->unknown150 + wheel->unknown158;
        if (!right)
            angle = -angle;
        wheel->unknown010.Make(S_vector(0.0f, 0.0f, 1.0f), angle);
        I3D_frame *holder = wheel->holder;
        if (holder->GetParent() == m_model)
            holder->SetLocalDir3(S_vector(0.0f, 0.0f, 1.0f), S_vector(0.0f, 1.0f, 0.0f));
        holder->Update();
        S_quat tilt;
        tilt.Make(S_vector(1.0f, 0.0f, 0.0f), wheel->unknown154);
        S_quat turn;
        turn.Make(S_vector(0.0f, 0.0f, 1.0f), right ? -wheel->unknown158 : wheel->unknown158);
        wheel->unknown000 = Multiply(tilt, turn);
    }

    // The skid marks: per wheel +0x678 pairs of hidden frames in the scene (the first child of a
    // "shlaif01.i3d" and a "shlaif02.i3d" model). A model that fails to load ends them all.
    if (!(m_unknownF74 & 0x1)) {
        bool failed = false;
        for (int i = m_wheelCount; i-- > 0 && !failed;) {
            S_VehicleWheel *wheel = m_wheels[i];
            if (!wheel)
                continue;
            for (int k = 0; k < m_unknown678; ++k) {
                auto *model0 = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
                auto *model1 = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
                if (!model0) {
                    if (model1)
                        model1->Release();
                    continue;
                }
                if (!model1) {
                    model0->Release();
                    continue;
                }
                const HRESULT loaded0 = g_modelCache.OpenInto(model0, "shlaif01.i3d");
                const HRESULT loaded1 = g_modelCache.OpenInto(model1, "shlaif02.i3d");
                if (loaded0 != I3D_OK || loaded1 != I3D_OK) {
                    model0->Release();
                    model1->Release();
                    failed = true;
                    break;
                }
                I3D_frame *frame0 = model0->GetChild(0);
                I3D_frame *frame1 = model1->GetChild(0);
                // Fix: a model without a child is skipped (the original reads through it).
                if (frame0 && frame1) {
                    for (I3D_frame *frame : {frame0, frame1}) {
                        frame->AddRef();
                        frame->LinkTo(g_carScene->GetPrimarySector(), 0);
                        frame->SetOn(false);
                        frame->Update();
                    }
                    S_VehicleSkidMark mark = {};
                    mark.frame0 = frame0;
                    mark.frame1 = frame1;
                    wheel->skidMarks.push_back(mark);
                }
                model0->Release();
                model1->Release();
            }
        }
    }

    for (size_t i = m_seats.size(); i-- > 0;)
        m_seats[i].load = 0.0f;
    SetSeatLoad(0, 0.0f);
    m_unknown228E = 0;
    m_unknownF68 = 0.0f;
#ifndef HD2_DEDICATED   // no sounds on the server
    LoadSounds(m_params);
    AddSoundsToSectors();
#endif
    ResetCondition();
    RepairParts(true);
    LoadGlassTextures(m_params.glassTexture, m_params.glassTextureAlpha);
    // (0x004f6070 with m_bodyCollision: an empty function.)
    m_unknown2288 = 1;
    m_unknown22C1 = 0;
    m_specialVolumesOn = false;
    AddLightsToSectors();
    m_unknown52C = 0.0f;
    m_unknown530 = 1.0f;
    m_unknown2287 = 0;
    m_unknown040 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown528 = 1.0f;
    m_wheelOnWater = 0;
    m_unknown5D0[0] = 0;
    m_unknown5D0[2] = 0;
    m_unknown5D0[1] = 0;
    m_unknown5E0 = 0;
    m_unknown650 = 0;
    m_automatic = true;
    m_unknown86C = 0.0f;
    m_unknown934 = 0.0f;
    m_unknown22C2 = 0;
    m_unknown51C = 0.0f;
    m_unknown520 = 0.0f;
    m_unknownF48 = 0.0f;
    UpdateStruts();
    m_unknown500 = 0;
    m_unknown030 = m_modelPos;

    // Quirk kept: the wheels', exhausts' and +0x638 effect slots are dropped without being
    // stopped (Stop and Reset stop them).
    for (int i = 0; i < m_wheelCount; ++i) {
        if (m_wheels[i])
            m_wheels[i]->effect = -1;
    }
    for (size_t i = m_exhausts.size(); i-- > 0;)
        m_exhausts[i].effect = -1;
    if (m_unknown638 >= 0)
        m_unknown638 = -1;
    m_bodyContacts.clear();
    m_bodyContacts.shrink_to_fit();
    for (int i = 3; i-- > 0;) {
        if (m_bodyContactEffects[i] >= 0) {
            StopEffect(m_bodyContactEffects[i]);
            m_bodyContactEffects[i] = -1;
        }
    }
    m_unknown010 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown020 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown22C7 = 0;
    m_rearGroundVolume = nullptr;
    m_frontGroundVolume = nullptr;
    m_unknown4F8[1] = 0;
    m_unknown4F8[0] = 0;
    m_unknown22C8 = 0;
    m_unknownF4C = 0.0f;
    m_unknownF50 = 0.0f;
}

void C_Vehicle::Stop()                                                          // 0x004ec5c0
{
    SetVolumesInWorld(false, false);
    m_specialVolumes.clear();
    m_specialVolumes.shrink_to_fit();
    m_specialVolumeList.clear();
    ReleaseSounds();

    // The steered wheels' holders straight again, every wheel shown.
    for (S_VehicleWheel *wheel = m_wheelList; wheel; wheel = wheel->next) {
        if ((wheel->flags & WHEEL_STEERED) && !(wheel->flags & WHEEL_DUMMY) &&
            wheel->holder->GetParent() == m_model) {
            S_quat straight;
            straight.Make(S_vector(0.0f, 1.0f, 0.0f), 0.0f);
            wheel->holder->SetRot(straight);
            wheel->holder->GetLocalMatrix();
            wheel->holder->Update();
        }
        wheel->frame->SetOn(true);
    }

    // The holders back in place, the skid marks freed.
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)                                     // Fix: the original crashes on a missing wheel
            continue;
        EndSkidMark(i);
        wheel->holder->SetPosInline(XYZ(wheel->holderPos));
        wheel->holder->Update();
        for (size_t k = wheel->skidMarks.size(); k-- > 0;) {
            wheel->skidMarks[k].frame0->Release();
            wheel->skidMarks[k].frame1->Release();
        }
        wheel->skidMarks.clear();
    }

    RepairParts(false);
    m_unknown5B0 = 0;
    UpdateLamps(0.1f);                                  // the lamps dark
    RemoveSoundsFromSectors();
    RemoveLights();

    for (size_t i = m_exhausts.size(); i-- > 0;) {
        if (m_exhausts[i].effect >= 0) {
            StopEffect(m_exhausts[i].effect);
            m_exhausts[i].effect = -1;
        }
    }
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (wheel && wheel->effect >= 0) {
            StopEffect(wheel->effect);
            wheel->effect = -1;
        }
    }
    if (m_unknown638 >= 0) {
        StopEffect(m_unknown638);
        m_unknown638 = -1;
    }
    m_bodyContacts.clear();
    m_bodyContacts.shrink_to_fit();
    for (int i = 3; i-- > 0;) {
        if (m_bodyContactEffects[i] >= 0) {
            StopEffect(m_bodyContactEffects[i]);
            m_bodyContactEffects[i] = -1;
        }
    }
}

void C_Vehicle::Restart(float speed, bool repair)                               // 0x004e9db0
{
    // The steered wheels' holders straight (Start's pose: their rest turn by a turn of 0 about the
    // holder's y axis), every holder back in place.
    m_model->Update();
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)                                     // Fix: the original crashes on a missing wheel
            continue;
        I3D_frame *holder = wheel->holder;
        if ((wheel->flags & WHEEL_STEERED) && !(wheel->flags & WHEEL_DUMMY) && holder->GetParent() == m_model) {
            S_quat straight;
            straight.Make(holder->GetLocalMatrix()(1), 0.0f);
            // (The original's out-of-line vector helpers give Multiply's order of operations.)
            holder->SetRot(Multiply(wheel->unknown000, straight));
            holder->GetLocalMatrix();
        }
        holder->SetPosInline(XYZ(wheel->holderPos));
        holder->Update();
    }
    ReadModelPlace();

    // The motion: at rest, or `speed` m/s along the model's z axis with the engine running.
    m_unknown2293 = 0;
    m_unknown210 = S_vector(0.0f, 0.0f, 0.0f);
    SetXYZ(m_velocity, S_vector(0.0f, 0.0f, 0.0f));
    m_velocityDir = m_velocity;
    m_unknown600 = VEHICLE_PHYSICS_AWAKE;
    m_unknown5F0 = 0.0f;
    m_unknown710 = 0.0f;
    m_unknown22C0 = 0;
    m_speed = 0.0f;
    m_engineRevs = 0.0f;
    m_previousRevs = 0.0f;
    if (speed != 0.0f) {
        const S_vector velocity(speed * m_modelAxisZ.x, speed * m_modelAxisZ.y, speed * m_modelAxisZ.z);
        SetXYZ(m_velocity, velocity);
        SetXYZ(m_velocityDir, velocity.Normalized());
        m_speed = std::fabs(speed);
    }
    SetEngine(speed != 0.0f, true);

    // The controls released, in gear 1.
    m_gear = 1;
    m_requestedGear = 1;
    m_unknown78C = 0.0f;
    m_handbrake = 0.0f;
    m_unknown724 = 0.0f;
    m_unknown904 = 1.0f;
    m_unknown94C = 0.0f;
    m_brake = 0.0f;
    m_unknown22A0 = 0;
    m_unknown85C = 0.0f;
    m_unknown860 = 0.0f;
    m_steering = 0.0f;
    m_unknown2294 = 0;
    m_unknown2295 = 0;
    m_unknown780 = 0.0f;
    m_unknown958 = 0.0f;

    // The wheels off the ground, their contact under them at their reach (as Start); repaired: whole
    // and shown.
    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)                                     // Fix: the original crashes on a missing wheel
            continue;
        I3D_frame *frame = wheel->frame;
        wheel->worldPos = Row(frame->GetMatrix(), 3);
        wheel->flags &= ~(WHEEL_ON_GROUND | WHEEL_SLIDING | WHEEL_LOCKED | WHEEL_BUMPED | WHEEL_SPINNING);
        if (repair) {
            m_sinceRimScrape = 30000;                   // (in the loop: not without wheels)
            wheel->flags &= ~(WHEEL_ON_RIM | WHEEL_DETACHED);
            frame->SetOn(true);
        }
        wheel->skidMark = 0;
        wheel->flags &= ~WHEEL_HIT;
        wheel->extension = 0.0f;
        wheel->unknown0A0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown0B0 = S_vector(0.0f, 0.0f, 0.0f);
        wheel->unknown1F0 = 0.0f;
        wheel->surface = 0;
        std::fill(std::begin(wheel->unknown25C), std::end(wheel->unknown25C), 0.0f);
        wheel->unknown26C = 1000.0f;
        wheel->ground = nullptr;
        wheel->unknown168 = 0.0f;
        wheel->groundPlane = {S_vector(0.0f, 1.0f, 0.0f), 0.0f};
        wheel->groundPlaneVolume = nullptr;
        const float reach = wheel->unknown1E4;
        const S_vector down(reach * m_modelAxisY.x, m_modelAxisY.y * reach, m_modelAxisY.z * reach);
        SetXYZ(wheel->unknown050, XYZ(wheel->worldPos) - down);
        SetXYZ(wheel->unknown060, XYZ(wheel->unknown050) - XYZ(m_modelAxisY));
    }
    m_unknown908 = 1.0f;
    m_unknown90C = 1.0f;
    m_unknown910 &= ~(VEHICLE_CLUTCH_ENGAGING | VEHICLE_CLUTCH_RELEASING | VEHICLE_CLUTCH_4 | VEHICLE_CLUTCH_8 |
                      VEHICLE_CLUTCH_10);
    m_unknownF68 = 0.0f;
    if (repair) {
        ResetCondition();
        RepairParts(false);
    }
    m_unknown228E = 0;

    // The lamps' own and the projectors' objects back into the scene (Reset took them out), in the
    // model's parent.
    for (size_t i = m_lamps.size(); i-- > 0;) {
        const S_VehicleLamp &lamp = m_lamps[i];
        if (!lamp.projector || (lamp.flags & VEHICLE_LAMP_SHARED))
            continue;
        lamp.projector->LinkTo(m_unknown544, 0);
        if (lamp.light)
            lamp.light->LinkTo(m_unknown544, 0);
    }
    for (size_t i = m_projectors.size(); i-- > 0;) {
        m_projectors[i].projector->LinkTo(m_unknown544, 0);
        if (m_projectors[i].light)
            m_projectors[i].light->LinkTo(m_unknown544, 0);
    }

    UpdateWheelSpin(1.0f);
    SetVolumesInWorld(true, true);
    m_unknown22C0 = 1;
    m_unknown22C1 = 0;
    m_brake = 0.0f;
    m_unknown700 = 0.0f;
    m_throttle = 0.0f;
    m_rearGroundVolume = nullptr;
    m_frontGroundVolume = nullptr;
    m_unknown4F8[1] = 0;
    m_unknown4F8[0] = 0;
}

// --- the loads --------------------------------------------------------------------------------

void C_Vehicle::SetSeatLoad(int seat, float mass)                               // 0x004f3950
{
    // Fix: a negative seat wrote before the seats (the original checks only the end).
    if (seat < 0 || seat >= static_cast<int>(m_seats.size()))
        return;
    m_seats[seat].load = mass;

    // The loads' centre: from the first seat, each further load pulls it by its share of the
    // loads so far.
    S_vector centre = XYZ(m_seats[0].pos);
    float total = m_seats[0].load;
    const auto add = [&](const S_vector4 &pos, float load) {
        S_vector pull = XYZ(pos) - centre;
        if (!(load < 0.001f) || !(total < 0.001f)) {
            const float share = load / (total + load);
            pull = S_vector(pull.x * share, pull.y * share, pull.z * share);
        } else {
            pull = S_vector(0.0f, 0.0f, 0.0f);
        }
        centre = S_vector(centre.x + pull.x, pull.y + centre.y, pull.z + centre.z);
        total = total + load;
    };
    for (size_t i = 1; i < m_seats.size(); ++i)
        add(m_seats[i].pos, m_seats[i].load);
    for (size_t i = m_ballasts.size(); i-- > 0;)
        add(m_ballasts[i].pos, m_ballasts[i].mass);

    // The centre of mass moves towards it by the loads' share of the whole mass; the inertia
    // grows by the loads at that distance.
    S_vector shift = centre - XYZ(m_centreOfMass);
    const float share = total / (total + m_mass);
    shift = S_vector(share * shift.x, share * shift.y, share * shift.z);
    SetXYZ(m_unknown1E0, XYZ(m_centreOfMass) + shift);
    const float distance = shift.Magnitude();
    m_unknown22C0 = 1;
    const float added = distance * distance * total;
    m_unknown220.x = 1.0f / (1.0f / m_inverseInertia.x + added);
    m_unknown220.y = 1.0f / (1.0f / m_inverseInertia.y + added);
    m_unknown220.z = 1.0f / (1.0f / m_inverseInertia.z + added);
}

// --- the damage parts -----------------------------------------------------------------------------

void C_Vehicle::RepairParts(bool healthOnly)                                    // 0x004fe910
{
    for (size_t i = m_parts.size(); i-- > 0;) {
        if (!healthOnly) {
            S_VehiclePart &part = m_parts[i];
            // A deformed part's vertices back from its original (dropped with its last user).
            if (!part.frames) {
                for (size_t k = m_meshOriginals.size(); k-- > 0;) {
                    S_VehicleMeshOriginal &original = m_meshOriginals[k];
                    if (m_modelFileName != original.modelFile || original.partName != part.name ||
                        !(part.state & VEHICLE_PART_DEFORMED))
                        continue;
                    CopyPositions(original.mesh->GetLevel(0), part.mesh->GetLevel(0));
                    const bool twoLevels = original.mesh->NumLevels() > 1;
                    if (twoLevels) {
                        I3D_mesh_level *from = original.mesh->GetLevel(1);
                        I3D_mesh_level *to = part.mesh->GetLevel(1);
                        if (from && to)
                            CopyPositions(from, to);
                    }
                    auto *visual = static_cast<I3D_object_visual *>(part.frame);
                    part.mesh->GetLevel(0)->Unknown2C();
                    if (twoLevels)
                        part.mesh->GetLevel(1)->Unknown2C();
                    part.mesh->ComputeBounds();
                    visual->UnknownAC(0);
                    if (twoLevels)
                        visual->UnknownAC(1);
                    part.frame->Update();
                    part.state &= ~VEHICLE_PART_DEFORMED;
                    if (--original.refs == 0) {
                        if (original.mesh)
                            original.mesh->Release();
                        m_meshOriginals.erase(m_meshOriginals.begin() + static_cast<ptrdiff_t>(k));
                    }
                }
            }
            // A broken glass part shows its intact texture again (the cracked one kept).
            if (part.type == VEHICLE_PART_GLASS && (part.state & VEHICLE_PART_GLASS_BROKEN)) {
#ifndef HD2_DEDICATED   // the server's glass has no texture to swap back: the state only
                I3D_material *material = part.mesh->GetLevel(0)->GetFaceGroup(0)->material;
                I3D_texture *shown = material->GetTexture();
                if (shown && part.glassTexture) {
                    shown->AddRef();
                    material->SetTexture(part.glassTexture);
                    part.glassTexture->Release();
                    part.glassTexture = shown;
                    part.frame->Update();
                }
#endif
                part.state &= ~VEHICLE_PART_GLASS_BROKEN;
            }
            // The door whose part this is gets the part's main visual back.
            for (size_t s = m_seats.size(); s-- > 0;) {
                S_VehicleDoor *door = m_seats[s].door;
                if (door && door->doorPart == i) {
                    door->frame = part.frame;
                    break;
                }
            }
            if (part.frames) {
                for (size_t j = part.frames->size(); j-- > 0;) {
                    I3D_frame *other = (*part.frames)[j];
                    if (!other)                         // Fix: a group's empty slot (null)
                        continue;
                    other->SetOn(false);
                    other->Update();
                }
            }
            part.frame->SetOn(true);
            part.frame->Update();
        }
        m_parts[i].health = m_parts[i].fullHealth;
        m_parts[i].state &= ~VEHICLE_PART_STATE_1;
    }
    if (auto callback = reinterpret_cast<T_VehicleCallback>(m_callbacks[VEHICLE_CALLBACK_REPAIRED]))
        callback(m_callbackContext);
}

void C_Vehicle::ResetCondition()                                                // 0x004feea0 (DS 0x004f20b0)
{
    m_unknown63C = m_unknown640;
    m_unknown610 = m_unknown614;
    m_unknown60C = 1.0f;
    for (int i = 0; i < m_wheelCount; ++i) {
        S_VehicleWheel *wheel = m_wheels[i];
        if (!wheel)                                     // Fix: the original crashes on a missing wheel
            continue;
        wheel->bend = 0.0f;
        wheel->health = wheel->fullHealth;
    }
#ifndef HD2_DEDICATED   // the server has no lamps or projectors
    for (size_t i = m_lamps.size(); i-- > 0;) {
        m_lamps[i].health = m_lampFullHealth;
        m_lamps[i].flags &= ~0x1u;
    }
    for (size_t i = m_projectors.size(); i-- > 0;) {
        S_VehicleProjector &projector = m_projectors[i];
        projector.state = 3;
        projector.projector->SetTexture(projector.textures[0]);
        if (projector.light)                            // Fix: the original crashes without a spot light
            projector.light->SetPower(2.0f);
    }
#endif
}

bool C_Vehicle::LoadGlassTextures(const char *name, const char *alphaName)      // 0x004fe850 (DS 0x004f1a50)
{
#ifdef HD2_DEDICATED   // no textures in LS3DRF.dll: the server's only returns true
    (void)name;
    (void)alphaName;
    return true;
#else
    if (!name || !alphaName)
        return true;
    for (S_VehiclePart &part : m_parts) {
        if (part.type != VEHICLE_PART_GLASS || part.glassTexture || !*name)
            continue;
        IImage *image = nullptr;
        if (GetIGraph()->LoadTexture(name, alphaName, 4, &image) == 0)
            part.glassTexture = reinterpret_cast<I3D_texture *>(image);
    }
    return true;
#endif
}

// --- the lights ---------------------------------------------------------------------------------

namespace {

// Whether `light` is a projector record's (the lamps sharing it).
bool IsProjectorLight(const std::vector<S_VehicleProjector> &projectors, const I3D_light *light)
{
    for (size_t i = projectors.size(); i-- > 0;) {
        if (projectors[i].light == light)
            return true;
    }
    return false;
}

}  // namespace

void C_Vehicle::AddLightsToSectors()                                            // 0x004f6080
{
    for (size_t i = m_lamps.size(); i-- > 0;) {
        I3D_light *light = m_lamps[i].light;
        if (!light || IsProjectorLight(m_projectors, light))
            continue;
        for (size_t s = s_lightSectors.size(); s-- > 0;)
            s_lightSectors[s]->AddLight(light);
    }
    for (size_t i = m_projectors.size(); i-- > 0;) {
        if (!m_projectors[i].light)                     // Fix: a projector without a spot light
            continue;
        for (size_t s = s_lightSectors.size(); s-- > 0;)
            s_lightSectors[s]->AddLight(m_projectors[i].light);
    }
}

void C_Vehicle::RemoveLights()                                                  // 0x004f61e0
{
    for (size_t i = m_lamps.size(); i-- > 0;) {
        I3D_light *light = m_lamps[i].light;
        if (!light)
            continue;
        if (!IsProjectorLight(m_projectors, light)) {
            for (size_t s = s_lightSectors.size(); s-- > 0;)
                s_lightSectors[s]->RemoveLight(light);
        }
        light->LinkTo(nullptr, 0);
    }
    for (size_t i = m_projectors.size(); i-- > 0;) {
        I3D_light *light = m_projectors[i].light;
        if (!light)                                     // Fix: the original crashes without a spot light
            continue;
        for (size_t s = s_lightSectors.size(); s-- > 0;)
            s_lightSectors[s]->RemoveLight(light);
        light->LinkTo(nullptr, 0);
    }
}
