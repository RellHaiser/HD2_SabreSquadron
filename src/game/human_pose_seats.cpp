// The weapon pose in seats (original source 0x007ae190..0x007b0080 and 0x007b9070): the hands on
// a car's steering wheel ("LWC" / "RWC"), on a seat weapon or a static weapon ("LWS" / "RWS"),
// the gunner's pose of a static weapon blended from six sampled poses by the weapon's aim, and
// the neck swaying with the vehicle's acceleration.
#include "game/human_pose_internal.h"

#include <cmath>

#include "game/actor.h"
#include "game/human.h"
#include "game/human_internal.h"
#include "game/human_movement.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// The steering wheel turns 60 degrees each way before a hand lets go of its grip.
const float WHEEL_HOLD_ANGLE = 1.0471976f;

const char *const GRIP_LEFT_CAR = "LWC";
const char *const GRIP_RIGHT_CAR = "RWC";
const char *const GRIP_LEFT_SEAT = "LWS";
const char *const GRIP_RIGHT_SEAT = "RWS";

// The velocity of the vehicle (C_Car +0x4A0: its C_Vehicle's). The original reads +0x4A0 of any seat
// actor. Fix: none for the other kinds (the submarine and the static weapon are smaller: the original
// reads past them).
const S_vector4 &VehicleVelocity(C_Actor *vehicle)
{
    static const S_vector4 still = {0.0f, 0.0f, 0.0f, 0.0f};
    if (vehicle->GetType() == ACTOR_CAR)
        return static_cast<C_Car *>(vehicle)->m_vehicle.GetVelocity();
    return still;
}

// The target of a hand's effector: the hand's offset point mirrored about the grip.
S_vector MirrorAbout(const S_matrix &grip, const S_vector &point)
{
    return grip(3) + grip(3) - point;
}

// `v` scaled down to `max` when longer. (The SDK's tests as the original inlines them, 0x007af436: below 1e-8 as an
// integer compare; a NaN takes the square root and is returned as it is.)
S_vector Capped(S_vector v, float max)
{
    const float sq = v.Square();
    if (PoseBelowEpsilon(sq))
        return v;
    float length = 1.0f;
    if (!(std::fabs(sq - 1.0f) < 1e-8f)) {
        length = std::sqrt(sq);
        if (!(length > max))
            return v;
    }
    return v * (max / length);
}

}  // namespace

// 0x007ae190
void C_HumanWeaponPose::SetSeat(uint32_t seatFlags, C_Actor *vehicle, int seat)
{
    if (m_flags & POSE_FLAG_STEER)
        static_cast<C_Car *>(m_vehicle)->GetVehicle()->SetSteering(0.0f);
    m_flags &= ~(POSE_FLAG_STEER | POSE_FLAG_STATIC_WEAPON | POSE_FLAG_SEAT_WEAPON | POSE_FLAG_PASSENGER);
    if (!vehicle) {
        m_vehicle = nullptr;
        m_hasGunnerPose = false;
        m_stance = m_stanceBeforeSeat;
        if (m_stanceBeforeSeat == HUMAN_STANCE_PRONE) {
            m_proneFit = true;
            m_proneSettle = 500;
        }
        return;
    }
    if (m_stance != POSE_STANCE_SEATED)
        m_stanceBeforeSeat = m_stance;
    if (vehicle->GetType() == ACTOR_CAR) {
        if (!(seatFlags & SEAT_FLAG_HIDDEN)) {
            I3D_frame *frame = vehicle->GetFrame();
            if (seatFlags & SEAT_FLAG_CONTROLS) {
                m_seatLeftGrip = frame->FindChildFrame(GRIP_LEFT_CAR, FRAME_MASK_ANY);
                m_seatRightGrip = frame->FindChildFrame(GRIP_RIGHT_CAR, FRAME_MASK_ANY);
                I3D_frame *wheel = m_seatRightGrip && m_seatLeftGrip ? m_seatLeftGrip->GetParent() : nullptr;
                if (m_seatRightGrip && m_seatLeftGrip)
                    m_flags = POSE_FLAG_STEER;
                if (!wheel) {
                    m_flags = 0;
                    return;
                }
                // The wheel's turn is measured against its local matrix now; the grips are
                // remembered in the vehicle's axes.
                m_wheel[0] = PoseInverseRotation(wheel->GetLocalMatrix());
                const S_matrix vehicleInverse = PoseInverseRotation(vehicle->GetFrame()->GetMatrix());
                m_wheel[1] = m_seatLeftGrip->GetMatrix();
                m_wheel[2] = m_seatRightGrip->GetMatrix();
                const S_vector &vehiclePos = vehicle->GetFrame()->GetWorldPos();
                m_wheel[1](3) -= vehiclePos;
                m_wheel[2](3) -= vehiclePos;
                m_wheel[1] *= vehicleInverse;
                m_wheel[2] *= vehicleInverse;
                m_vehicleVelocity = VehicleVelocity(vehicle);
                PoseSetXYZ(m_accel, S_vector(0.0f, 0.0f, 0.0f));
                PoseSetXYZ(m_lastAccel, S_vector(0.0f, 0.0f, 0.0f));
                PoseSetXYZ(m_sampledAccel, S_vector(0.0f, 0.0f, 0.0f));
                m_accelTime = 0;
                m_chain.root = &m_bones[POSE_BONE_SPINE];
                m_chain.pins[0].bone = &m_bones[POSE_BONE_LEFT_HAND];
                m_chain.pins[1].bone = &m_bones[POSE_BONE_RIGHT_HAND];
                m_chain.pins[0].weight = 1.0f;
                m_chain.pins[1].weight = 1.0f;
                m_chain.pins[2].bone = &m_bones[POSE_BONE_NECK];
                m_chain.pins[2].weight = 1.0f;
                m_chain.count = 3;
                ClearChains();
                BuildChains();
            } else if (seatFlags & SEAT_FLAG_SIT) {
                m_stance = POSE_STANCE_SEATED;
                m_vehicleVelocity = VehicleVelocity(vehicle);
                PoseSetXYZ(m_accel, S_vector(0.0f, 0.0f, 0.0f));
                PoseSetXYZ(m_lastAccel, S_vector(0.0f, 0.0f, 0.0f));
                PoseSetXYZ(m_sampledAccel, S_vector(0.0f, 0.0f, 0.0f));
                m_accelTime = 0;
                m_seatRightGrip = nullptr;
                m_seatLeftGrip = nullptr;
                if ((seatFlags & SEAT_FLAG_WEAPON) && vehicle->HasSeatWeapon(seat)) {
                    m_seatLeftGrip = frame->FindChildFrame(GRIP_LEFT_SEAT, FRAME_MASK_ANY);
                    m_seatRightGrip = frame->FindChildFrame(GRIP_RIGHT_SEAT, FRAME_MASK_ANY);
                }
                if (!m_seatLeftGrip || !m_seatRightGrip) {
                    // Only the neck: it sways with the vehicle.
                    m_chain.pins[0].bone = &m_bones[POSE_BONE_NECK];
                    m_chain.root = &m_bones[POSE_BONE_SPINE];
                    m_chain.pins[0].weight = 1.0f;
                    m_chain.count = 1;
                    m_flags |= POSE_FLAG_PASSENGER;
                } else {
                    m_chain.root = &m_bones[POSE_BONE_SPINE];
                    m_chain.pins[0].bone = &m_bones[POSE_BONE_LEFT_HAND];
                    m_chain.pins[0].weight = 1.0f;
                    m_chain.pins[1].bone = &m_bones[POSE_BONE_RIGHT_HAND];
                    m_chain.pins[1].weight = 1.0f;
                    m_chain.pins[2].bone = &m_bones[POSE_BONE_NECK];
                    m_chain.pins[2].weight = 1.0f;
                    m_chain.count = 3;
                    m_blend = 1.0f;
                    m_flags = POSE_FLAG_SEAT_WEAPON;
                }
                ClearChains();
                BuildChains();
            }
        }
        m_seatFlags = seatFlags;
        m_seat = seat;
        m_seatCount = vehicle->GetSeatCount();
        m_vehicle = vehicle;
        return;
    }
    if (vehicle->GetType() == ACTOR_STATIC_WEAPON) {
        I3D_frame *frame = vehicle->GetFrame();
        m_seatLeftGrip = frame->FindChildFrame(GRIP_LEFT_SEAT, FRAME_MASK_ANY);
        m_seatRightGrip = frame->FindChildFrame(GRIP_RIGHT_SEAT, FRAME_MASK_ANY);
        if (m_seatRightGrip && m_seatLeftGrip) {
            m_chain.root = &m_bones[POSE_BONE_SPINE];
            m_seat = seat;
            m_chain.pins[1].bone = &m_bones[POSE_BONE_RIGHT_HAND];
            m_blend = 1.0f;
            m_flags = POSE_FLAG_STATIC_WEAPON;
            m_chain.pins[0].bone = &m_bones[POSE_BONE_LEFT_HAND];
            m_chain.pins[0].weight = 1.0f;
            m_chain.pins[1].weight = 1.0f;
            m_chain.count = 2;
            ClearChains();
            BuildChains();
            m_vehicle = vehicle;
            return;
        }
        m_flags = 0;
    }
}

// The acceleration of the vehicle, sampled every 200 ms (the velocity's change times 5) and
// blended between the last two samples.
void C_HumanWeaponPose::UpdateVehicleAccel()
{
    m_accelTime -= m_time;
    if (m_accelTime < 0) {
        m_accelTime = 200;
        m_lastAccel = m_sampledAccel;
        const S_vector4 &velocity = VehicleVelocity(m_vehicle);
        PoseSetXYZ(m_sampledAccel, (PoseXYZ(velocity) - PoseXYZ(m_vehicleVelocity)) * 5.0f);
        m_vehicleVelocity = velocity;
    }
    const float f = static_cast<float>(m_accelTime) * 0.005f;
    PoseSetXYZ(m_accel, PoseXYZ(m_lastAccel) * f + PoseXYZ(m_sampledAccel) * (1.0f - f));
}

// 0x007ae820: the hands on the steering wheel (each lets go of its grip turned past 60
// degrees and holds the wheel where it was), the neck swaying with the acceleration and the
// elbows out to the sides by the wheel's turn (clients only).
void C_HumanWeaponPose::UpdateSteering()
{
    if (!IsClient() || !m_visible || !m_seatLeftGrip || !m_seatRightGrip)
        return;
    I3D_frame *wheel = m_seatLeftGrip->GetParent();
    const S_matrix turn = wheel->GetLocalMatrix() * m_wheel[0];
    float angle = std::atan2(turn.m[0][1], turn.m[0][0]);
    I3D_frame *vehicleFrame = m_vehicle->GetFrame();
    S_matrix left;
    S_matrix right;
    if (angle > WHEEL_HOLD_ANGLE) {
        left = m_wheel[1] * vehicleFrame->GetMatrix();
        right = m_seatRightGrip->GetMatrix();
    } else if (angle < -WHEEL_HOLD_ANGLE) {
        right = m_wheel[2] * vehicleFrame->GetMatrix();
        left = m_seatLeftGrip->GetMatrix();
    } else {
        left = m_seatLeftGrip->GetMatrix();
        right = m_seatRightGrip->GetMatrix();
    }
    const S_vector rightTarget = PoseXYZ(m_rightHandOffset) * right;
    PoseSetXYZ(m_rightHandTarget, rightTarget);
    const S_vector leftTarget = PoseXYZ(m_leftHandOffset) * left;
    PoseSetXYZ(m_leftHandTarget, leftTarget);
    m_chain.pins[0].weight = 1.0f;
    PoseSetXYZ(m_chain.pins[0].target, MirrorAbout(left, leftTarget));
    PoseSetXYZ(m_chain.pins[1].target, MirrorAbout(right, rightTarget));
    m_chain.pins[1].weight = 1.0f;
    RefreshChain(m_chain);
    m_chain.count = 2;
    SolveChain(m_chain, 16, false);

    UpdateVehicleAccel();
    const S_PoseBone *neck = m_chain.pins[2].bone;
    const S_vector tip = neck->chainWorld(2) * neck->chainLength + neck->chainWorld(3);
    const S_vector sway = Capped(PoseXYZ(m_accel) * 0.0075f, 0.4f);
    m_chain.pins[0].weight = 1.0f;
    m_chain.pins[1].weight = 1.0f;
    m_chain.pins[2].weight = 0.75f;
    PoseSetXYZ(m_chain.pins[2].target, tip - sway);
    m_chain.count = 3;
    SolveChain(m_chain, 16, false);
    ApplyChain(m_chain);
    SetHandMatrices(left, right);

    // The elbows out by the wheel's turn (0: turned 60 degrees right, 1: left).
    float t = angle * 0.47746482f + 0.5f;
    if (PoseIsNegative(t))
        t = 0.0f;
    else if (t > 1.0f)
        t = 1.0f;
    const double outer = (1.0 - t) * 0.7853982;
    const S_vector leftOut = S_vector(static_cast<float>(-std::sin(outer)), static_cast<float>(-std::cos(outer)), 0.0f)
                                 .RotateByMatrix(vehicleFrame->GetMatrix());
    const double inner = t * 0.7853982;
    const S_vector rightOut = S_vector(static_cast<float>(std::sin(inner)), static_cast<float>(-std::cos(inner)), 0.0f)
                                  .RotateByMatrix(vehicleFrame->GetMatrix());
    BendJoint(POSE_BONE_LEFT_ELBOW, leftOut, 1.0f);
    BendJoint(POSE_BONE_RIGHT_ELBOW, rightOut, 1.0f);

    // The grips as they are now, in the vehicle's axes.
    const S_matrix vehicleInverse = PoseInverseRotation(vehicleFrame->GetMatrix());
    m_wheel[1] = left;
    m_wheel[2] = right;
    const S_vector &vehiclePos = vehicleFrame->GetWorldPos();
    m_wheel[1](3) -= vehiclePos;
    m_wheel[2](3) -= vehiclePos;
    m_wheel[1] *= vehicleInverse;
    m_wheel[2] *= vehicleInverse;
}

// 0x007af040: a seat: the hands on the seat weapon's grips, the elbows out and the neck swaying
// with the acceleration; without a weapon only the neck sways (clients only).
void C_HumanWeaponPose::UpdateSeat()
{
    if (!IsClient() || !m_visible)
        return;
    RefreshChain(m_chain);
    UpdateVehicleAccel();
    if (m_seatLeftGrip && m_seatRightGrip) {
        const S_matrix &right = m_seatRightGrip->GetMatrix();
        PoseSetXYZ(m_rightHandTarget, PoseXYZ(m_rightHandOffset) * right);
        const S_matrix &left = m_seatLeftGrip->GetMatrix();
        PoseSetXYZ(m_leftHandTarget, PoseXYZ(m_leftHandOffset) * left);
        PoseSetXYZ(m_chain.pins[0].target, MirrorAbout(m_seatLeftGrip->GetMatrix(), PoseXYZ(m_leftHandTarget)));
        m_chain.pins[0].weight = 1.0f;
        PoseSetXYZ(m_chain.pins[1].target, MirrorAbout(m_seatRightGrip->GetMatrix(), PoseXYZ(m_rightHandTarget)));
        m_chain.pins[1].weight = 1.0f;
        m_chain.count = 2;
        SolveChain(m_chain, 16, false);
        const S_PoseBone *neck = m_chain.pins[2].bone;
        const S_vector tip = neck->chainWorld(2) * neck->chainLength + neck->chainWorld(3);
        const S_vector sway = Capped(PoseXYZ(m_accel) * 0.0075f, 0.4f);
        m_chain.pins[0].weight = 1.0f;
        m_chain.pins[1].weight = 1.0f;
        PoseSetXYZ(m_chain.pins[2].target, tip - sway);
        m_chain.pins[2].weight = 0.75f;
        m_chain.count = 3;
        SolveChain(m_chain, 16, false);
        ApplyChain(m_chain);
        SetHandRotations(m_seatLeftGrip, m_seatRightGrip);
        const S_matrix &vehicle = m_vehicle->GetFrame()->GetMatrix();
        const S_vector leftOut = S_vector(-0.5f, -0.866f, 0.0f).RotateByMatrix(vehicle);
        const S_vector rightOut = S_vector(0.5f, -0.866f, 0.0f).RotateByMatrix(m_vehicle->GetFrame()->GetMatrix());
        BendJoint(POSE_BONE_LEFT_ELBOW, leftOut, 1.0f);
        BendJoint(POSE_BONE_RIGHT_ELBOW, rightOut, 1.0f);
        // (Slot 0xA4: the seat weapon's aim, used as a direction.)
        PoseSetXYZ(m_aimDir, m_vehicle->GetSeatWeaponAim(m_seat));
        return;
    }
    // Only the neck.
    const S_vector4 neck = PoseWorldPos4(m_bones[POSE_BONE_NECK].frame);
    const S_vector sway = Capped(PoseXYZ(m_accel) * 0.009f, 0.45f);
    m_chain.pins[0].weight = 1.0f;
    m_chain.count = 1;
    m_chain.pins[0].target = PoseVector4(PoseXYZ(neck) - sway, neck.w);
    SolveChain(m_chain, 8, false);
    ApplyChain(m_chain);
}

// 0x007af780: a gunner at a static weapon: with the gunner poses the bones blend between the
// six sampled poses by the weapon's aim (left / middle / right by down / up, against the
// weapon's limits); the hands then go onto the grips and the elbows down (clients only).
void C_HumanWeaponPose::UpdateStaticWeapon()
{
    if (!IsClient() || !m_visible)
        return;
    if (m_hasGunnerPose) {
        PoseSetXYZ(m_aimDir, m_vehicle->GetSeatWeaponAim(m_seat));
        // The aim in the weapon's axes.
        const S_vector aim = PoseXYZ(m_aimDir);
        // (Summed z first, as the original: the turn's and the pitch's tests at +-1 and 0 depend on the rounding.)
        const float x = PoseDotZYX(aim, m_vehicle->GetFrame()->GetMatrix()(0));
        const float y = PoseDotZYX(aim, m_vehicle->GetFrame()->GetMatrix()(1));
        const float z = PoseDotZYX(aim, m_vehicle->GetFrame()->GetMatrix()(2));
        float up = 0.0f;
        float down = 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        m_vehicle->GetSeatWeaponLimits(m_seat, &up, &down, &left, &right);
        // The turn (0..1 across the half the aim is in; `half` 0: right, 1: left) and the
        // pitch (0..1 from down to up).
        float turn;
        int half;
        const double epsilon = 1e-8f;
        if (1.0 - std::fabs(y) <= epsilon || std::fabs(x) <= epsilon) {
            turn = 0.0f;
            half = 1;
        } else if (PoseIsNegative(x)) {
            turn = static_cast<float>(-((1.5707964 - std::atan2(std::fabs(z) / -x, 1.0)) / left));
            half = 1;
        } else {
            turn = static_cast<float>(1.0 - (1.5707964 - std::atan2(std::fabs(z) / x, 1.0)) / right);
            half = 0;
        }
        float pitch;
        if (PoseIsNegative(y))
            pitch = static_cast<float>(0.5 - (-0.5 / down) * (1.5707964 - std::acos(-y)));
        else
            pitch = static_cast<float>((0.5 / up) * (1.5707964 - std::acos(y)) + 0.5);
        if (PoseIsNegative(turn))
            turn = 0.0f;
        else if (turn > 1.0f)
            turn = 1.0f;
        if (PoseIsNegative(pitch))
            pitch = 0.0f;
        else if (pitch > 1.0f)
            pitch = 1.0f;
        for (int i = 0; i < POSE_BONE_COUNT; ++i) {
            I3D_frame *frame = m_bones[i].frame;
            const S_quat *rot[4] = {&m_gunnerRot[half * 2][i], &m_gunnerRot[half * 2 + 1][i],
                                    &m_gunnerRot[half * 2 + 2][i], &m_gunnerRot[half * 2 + 3][i]};
            if (!PoseIsUnset(rot[0]->s) && !PoseIsUnset(rot[1]->s) && !PoseIsUnset(rot[2]->s) &&
                !PoseIsUnset(rot[3]->s)) {
                const S_quat a = rot[0]->Slerp(*rot[2], turn, false);
                const S_quat b = rot[1]->Slerp(*rot[3], turn, false);
                frame->SetRot(a.Slerp(b, pitch, false));
                frame->Update();
            }
            const S_vector4 *pos[4] = {&m_gunnerPos[half * 2][i], &m_gunnerPos[half * 2 + 1][i],
                                       &m_gunnerPos[half * 2 + 2][i], &m_gunnerPos[half * 2 + 3][i]};
            if (!PoseIsUnset(pos[0]->x) && !PoseIsUnset(pos[1]->x) && !PoseIsUnset(pos[2]->x) &&
                !PoseIsUnset(pos[3]->x)) {
                const S_vector a = PoseXYZ(*pos[0]) * (1.0f - turn) + PoseXYZ(*pos[2]) * turn;
                const S_vector b = PoseXYZ(*pos[1]) * (1.0f - turn) + PoseXYZ(*pos[3]) * turn;
                frame->SetPosInline(a * (1.0f - pitch) + b * pitch);
                frame->Update();
            }
        }
    }
    if (m_seatLeftGrip && m_seatRightGrip) {
        PoseSetXYZ(m_aimDir, m_vehicle->GetSeatWeaponAim(m_seat));
        const S_matrix &right = m_seatRightGrip->GetMatrix();
        PoseSetXYZ(m_rightHandTarget, PoseXYZ(m_rightHandOffset) * right);
        const S_matrix &left = m_seatLeftGrip->GetMatrix();
        PoseSetXYZ(m_leftHandTarget, PoseXYZ(m_leftHandOffset) * left);
        PoseSetXYZ(m_chain.pins[0].target, MirrorAbout(m_seatLeftGrip->GetMatrix(), PoseXYZ(m_leftHandTarget)));
        m_chain.pins[0].weight = 1.0f;
        PoseSetXYZ(m_chain.pins[1].target, MirrorAbout(m_seatRightGrip->GetMatrix(), PoseXYZ(m_rightHandTarget)));
        m_chain.pins[1].weight = 1.0f;
        RefreshChain(m_chain);
        m_chain.count = 2;
        SolveChain(m_chain, 16, true);
        ApplyChain(m_chain);
        SetHandRotations(m_seatLeftGrip, m_seatRightGrip);
        const S_vector down = S_vector(0.0f, -1.0f, 0.0f).RotateByMatrix(m_vehicle->GetFrame()->GetMatrix());
        BendJoint(POSE_BONE_LEFT_ELBOW, down, 1.0f);
        BendJoint(POSE_BONE_RIGHT_ELBOW, down, 1.0f);
    }
}

// 0x007b9070: the six gunner poses from the first six keys of `anim`'s tracks (missing keys
// marked unset); null: no gunner pose.
void C_HumanWeaponPose::SetSeatWeaponPose(I3D_animation_set *anim)
{
    if (!anim) {
        m_hasGunnerPose = false;
        return;
    }
    for (int i = 0; i < POSE_BONE_COUNT; ++i) {
        const int track = anim->FindTrack(g_poseBoneNames[i]);
        for (int k = 0; k < 6; ++k) {
            int time;
            if (k < anim->NumPosKeys(track))
                anim->GetPosKey(track, k, &time, reinterpret_cast<S_vector *>(&m_gunnerPos[k][i]));
            else
                m_gunnerPos[k][i].x = PoseUnset();
            if (k < anim->NumRotKeys(track))
                anim->GetRotKey(track, k, &time, &m_gunnerRot[k][i]);
            else
                m_gunnerRot[k][i].s = PoseUnset();
        }
    }
    m_hasGunnerPose = true;
}
