// C_Vehicle: the per-step update (original 0x0050b180 Update, with its doors 0x00501750, windows
// 0x00501ab0, wheel spin 0x00507a40 and track strips 0x00506700). Update's display of the wheels'
// forces for the vehicle at 0x0089c204 (arrows 0x0089c1d4.., mode 0x0089c20c) is left out: nothing
// sets those globals, so it never runs.
#include "game/vehicle.h"

#include <cmath>
#include <cstdlib>

#include "game/collision.h"                             // g_collisionWorld
#include "game/game.h"                                  // GetGame
#include "game/pending.h"                               // g_carScene, g_carSurfaces
#include "ls3d/i3d.h"

namespace {

constexpr float TWO_PI = 6.283185f;                     // 0x00810934

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

float DotZYX(const S_vector &a, const S_vector &b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }

// The quaternion product a b (a's turn after b's) as these functions inline it, with the scalar
// part's dot product summed by the caller.
S_quat Product(const S_quat &a, const S_quat &b, float dot)
{
    S_quat q;
    q.v = (b.v * a.s + a.v * b.s) + a.v.Cross(b.v);
    q.s = a.s * b.s - dot;
    return q;
}
S_quat ProductZYX(const S_quat &a, const S_quat &b) { return Product(a, b, DotZYX(a.v, b.v)); }

}  // namespace

int C_Vehicle::Update(uint32_t ms)                                              // 0x0050b180
{
    if (m_unknown22C1)
        m_engineOn = m_unknown22C2;
    if (ms == 0)
        ms = 1;
    const float dt = static_cast<float>(ms) * 0.001f;
    if (m_velocity.y < -85.0f)
        return VEHICLE_UPDATE_LOST;
    m_model->Update();
    g_carScene->UpdateFrameSector(m_model, XYZ(m_unknown1B0));

    // The sounds.
    if (m_engineOn) {
        if (m_soundsOn)
            UpdateEngineSound(dt);
    } else if (m_unknown22C1) {
        StopEngineLoops();
    } else if (m_unknown22A2) {
        SwitchEngine(true);
    }
    if (m_unknownF6C & VEHICLE_SIM_DETAIL_SOUNDS) {
        if (!(m_handbrake > 0.0f))
            m_unknown228E = 0;
        else if (m_soundsOn)
            PlayHandbrakeSound();
    }
    if (m_soundsOn) {
        SwitchLoopSoundA(m_unknown2294 != 0);
        SwitchLoopSoundB(m_unknown2295 != 0);
        UpdateTyreSqueal(dt);
        UpdateRollingSound();
        // The rim scraping: a wheel on its rim on the ground restarts the 1 s count.
        m_sinceRimScrape = static_cast<uint16_t>(m_sinceRimScrape + ms);
        for (int i = m_wheelCount; i-- > 0;) {
            const S_VehicleWheel *w = m_wheels[i];
            if (w && (w->flags & WHEEL_ON_RIM) && (w->flags & WHEEL_ON_GROUND) &&
                !(w->flags & (WHEEL_DETACHED | WHEEL_DUMMY | WHEEL_LOCKED))) {
                m_sinceRimScrape = 0;
                break;
            }
        }
        if (m_sinceRimScrape < 1000) {
            UpdateRimSound(true);
        } else {
            UpdateRimSound(false);
            m_sinceRimScrape = 1000;
        }
    }

    // The light level at the centre of mass every 100 ms (+0x2292): the occluded intensity over
    // the open one.
    m_unknown54C += ms;
    if (static_cast<uint32_t>(m_unknown54C) > 100 && m_unknown2292) {
        m_unknown54C = 0;
        g_collisionWorld.SetIgnoredVolume(&m_volume490, 0);
        const S_vector at = XYZ(m_unknown1B0);
        const float open = GetGame()->GetLightIntensity(at, nullptr, false);
        const float occluded = GetGame()->GetLightIntensity(at, nullptr, true);
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
        m_unknown548 = open > 0.0f ? occluded / open : 1.0f;
    }
    if (m_wheelCount > 0 && m_wheels[0])                // (Fix: a missing first wheel is skipped)
        m_unknown2224 = DotZYX(XYZ(m_velocityDir), m_wheels[0]->unknown0B0) * m_unknown8E0 + m_unknown2224;

    UpdateDoors(dt);
    UpdateWindows(dt);
    if (m_unknownF6C & VEHICLE_SIM_SKID_MARKS) {
        for (int i = m_wheelCount; i-- > 0;) {
            if (!m_wheels[i])                           // Fix: a missing wheel is skipped
                continue;
            if (m_wheels[i]->flags & WHEEL_SLIDING)
                LaySkidMark(i);
            else
                EndSkidMark(i);
        }
    }

    if (m_unknown600 & VEHICLE_PHYSICS_AWAKE) {
        // The volumes follow the body (a special one on a moving part through the part's matrix).
        if (m_mainVolumesInWorld) {
            if (m_specialVolumesOn) {
                for (size_t i = m_specialVolumes.size(); i-- > 0;) {
                    S_CollisionVolume &volume = m_specialVolumes[i];
                    S_VehicleSpecialCollision &special = m_specialCollisions[i];
                    if (special.parent) {
                        const S_matrix matrix = *volume.matrix;
                        volume.center = XYZ(special.localPos) * matrix;
                        special.invParentMatrix.Inverse(matrix);
                    } else {
                        volume.center = XYZ(m_modelPos);
                        volume.centerW = m_modelPos.w;
                    }
                    if (m_specialCollisionDescs[i].unknown04)
                        g_collisionWorld.MoveVolume(&volume);
                }
            } else {
                m_bodyVolume.center = XYZ(m_modelPos);
                m_bodyVolume.centerW = m_modelPos.w;
                g_collisionWorld.MoveVolume(&m_bodyVolume);
            }
        }
        if (m_volume490InWorld) {
            m_volume490.center = XYZ(m_modelPos);
            m_volume490.centerW = m_modelPos.w;
            g_collisionWorld.MoveVolume(&m_volume490);
        }
        // The surface's bumps under each grounded wheel (with +0x528): a smooth time
        // (unknown25C[0]), then the bump (unknown25C[2] = its height) for unknown25C[1]; each cycle
        // draws new times and a height (at least half its range) from the surface's ranges (or the
        // wheel's with +0x228D). A wheel out of the contacts turns the body collision on.
        for (int i = m_wheelCount; i-- > 0;) {
            S_VehicleWheel *w = m_wheels[i];
            if (!w)                                     // Fix: a missing wheel is skipped
                continue;
            if (w->flags & (WHEEL_DETACHED | WHEEL_DUMMY)) {
                m_unknownF6C |= VEHICLE_SIM_BODY_COLLISION;
                continue;
            }
            if (m_unknown528 == 0.0f || !(w->flags & WHEEL_ON_GROUND))
                continue;
            const float cycle = w->unknown25C[1] + w->unknown25C[0];
            if (!(w->unknown26C > cycle)) {
                if (w->unknown26C >= w->unknown25C[0])
                    w->unknown25C[2] = w->unknown268;
                continue;
            }
            w->unknown25C[2] = 0.0f;
            if (cycle > 0.0f) {
                const float into = w->unknown26C - std::floor(w->unknown26C / cycle) * cycle;
                if (into >= w->unknown25C[0])
                    w->unknown25C[2] = w->unknown268;
            }
            w->unknown26C = 0.0f;
            float smooth0, smooth1, bump0, bump1, height;
            if (!m_unknown228D) {
                const S_CarSurface &surface = g_carSurfaces[g_carSurfaceOfMaterial[w->surface]];
                smooth0 = surface.unknown04;
                smooth1 = surface.unknown08;
                bump0 = surface.unknown0C;
                bump1 = surface.unknown10;
                height = surface.unknown14;
            } else {
                smooth0 = w->unknown248;
                smooth1 = w->unknown24C;
                bump0 = w->unknown250;
                bump1 = w->unknown254;
                height = w->unknown258;
            }
            w->unknown25C[0] = (smooth1 - smooth0) * (static_cast<float>(std::rand() % 100) * 0.01f) + smooth0;
            w->unknown25C[1] = (bump1 - bump0) * (static_cast<float>(std::rand() % 100) * 0.01f) + bump0;
            w->unknown268 = static_cast<float>(std::rand() % 200 - 100) * 0.01f * height;
            const float least = height * 0.5f;
            if (least > std::fabs(w->unknown268))
                w->unknown268 = w->unknown268 > 0.0f ? least : -least;
        }
    }

    // The steering: mode 0 here; mode 2 (steered by the tracks; Step turns the holders) keeps
    // the steered holders at their rest turn (a turn by 0 about their y axis).
    if (m_unknown550 == 0) {
        UpdateSteering(dt);
    } else if (m_unknown550 == 2) {
        for (int i = m_wheelCount; i-- > 0;) {
            S_VehicleWheel *w = m_wheels[i];
            if (!w || !(w->flags & WHEEL_STEERED) || (w->flags & WHEEL_DUMMY))   // (Fix: a missing wheel)
                continue;
            I3D_frame *holder = w->holder;
            S_quat q;
            q.Make(holder->GetLocalMatrix()(1), 0.0f);
            holder->SetRot(ProductZYX(w->unknown000, q));
            holder->GetLocalMatrix();
        }
    }
    UpdateWheelSpin(dt);
    UpdateLamps(dt);
    if (m_steeringWheel)
        m_steeringWheel->SetWorldDir(XYZ(m_steeringAxis), m_unknown94C / m_unknown934 * m_unknown504);
    if (m_unknown2292) {
        UpdateStruts();
        UpdateTracks(dt);
    } else if (m_unknownF6C != m_unknownF70) {
        UpdateStruts();
    }
    bool quiet = UpdateEngineSmoke();
    UpdateWheelEffects();
    UpdateExhausts();
    UpdateContactEffects();

    // The per-step state.
    m_unknown270 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown280 = S_vector(0.0f, 0.0f, 0.0f);
    m_unknown5D0[0] = 0;
    m_unknown229E = 0;
    m_unknown229F = 1;
    if (m_unknown58C & 0x2) {
        // Parts and wheels hit this step: a hit wheel turns by its damage (+0x228 x the lost
        // share of +0x244).
        for (size_t i = m_parts.size(); i-- > 0;)
            m_parts[i].state &= ~VEHICLE_PART_STATE_2;
        for (int i = m_wheelCount; i-- > 0;) {
            S_VehicleWheel *w = m_wheels[i];
            if (!w)                                     // Fix: a missing wheel is skipped
                continue;
            if ((w->flags & WHEEL_HIT) && w->health < w->bendHealth)
                w->bend = (1.0f - w->health / w->bendHealth) * w->maxBend;
            w->flags &= ~WHEEL_HIT;
        }
        m_unknown58C &= ~0x2u;
    }
    m_unknown58C &= ~0xCu;
    for (size_t i = m_seats.size(); i-- > 0;) {
        if (m_seats[i].door && (m_seats[i].door->flags & DOOR_MOVING)) {
            quiet = false;
            break;
        }
    }
    m_unknown544 = m_model->GetParent();
    if (quiet && (m_unknown5B0 & (VEHICLE_LIGHT_FLASHING | VEHICLE_LIGHT_INDICATOR_LEFT | VEHICLE_LIGHT_INDICATOR_RIGHT)))
        quiet = false;
    m_unknownF70 = m_unknownF6C;
    if (m_wheelOnWater) {
        m_unknown500 = 0;
        if (m_unknown604 > 0)
            m_unknown604 = 0;
        m_unknown600 |= VEHICLE_PHYSICS_AWAKE | VEHICLE_PHYSICS_IN_WATER;
    } else {
        m_unknown600 &= ~VEHICLE_PHYSICS_IN_WATER;
    }
    if (!(m_unknown600 & VEHICLE_PHYSICS_AWAKE) && quiet && !m_driven && !m_engineOn) {
        // Quirk kept: idle, +0x2287 (steering at a point) is not cleared.
        if (!m_unknown22C0)
            return VEHICLE_UPDATE_IDLE;
    } else if (!m_unknown22C0 && (!(m_brake > 0.9f) || !(m_unknown85C > 0.0f))) {
        m_unknown22C0 = 0;
        m_wheelOnWater = 0;
        m_unknown2287 = 0;
        return VEHICLE_UPDATE_DONE;
    }
    // The automatic box drops to gear 1 / 2 when the wheels turn too slowly for the gear (pushed or
    // braking on the throttle).
    if ((!(m_unknownF6C & VEHICLE_SIM_IMPACT_DAMAGE) || m_unknown63C > 0.0f) && m_engineOn && m_automatic &&
        m_gear != 0 && (m_unknown2293 || m_tracked) && m_requestedGear > 0) {
        const float revs = m_speed / m_maxWheelCircumference * m_gearRatios[m_gear + 1];
        if (m_gearCount > 1 && revs < m_shiftDownRevs[2])
            SetGear(1);
        else if (m_gearCount > 2 && revs < m_shiftDownRevs[3])
            SetGear(2);
    }
    m_unknown22C0 = 0;
    m_wheelOnWater = 0;
    m_unknown2287 = 0;
    return VEHICLE_UPDATE_DONE;
}

void C_Vehicle::UpdateDoors(float timeDelta)                                    // 0x00501750
{
    for (size_t i = m_seats.size(); i-- > 0;) {
        S_VehicleDoor *door = m_seats[i].door;
        if (!door || !(door->flags & DOOR_MOVING))
            continue;
        const int seat = static_cast<int>(i);
        if (door->angle < door->targetAngle) {
            if (door->angle < 0.0001f)
                PlayDoorSound(seat, true);
            door->angle = timeDelta * door->speed + door->angle;
            if (door->angle > door->targetAngle) {
                door->angle = door->targetAngle;
                door->flags &= ~DOOR_MOVING;
            }
        } else {
            door->angle = door->angle - timeDelta * door->speed;
            if (door->angle <= door->targetAngle) {
                door->angle = door->targetAngle;
                door->flags &= ~DOOR_MOVING;
                PlayDoorSound(seat, false);
            }
        }
        // About the y axis, the sense by the side (and DOOR_FLAG_4 without DOOR_BACKWARD).
        const bool flipped = (door->flags & DOOR_FLAG_4) && !(door->flags & DOOR_BACKWARD);
        const bool left = door->pos.x < 0.0f;
        S_quat turn;
        turn.Make(S_vector(0.0f, 1.0f, 0.0f), left == flipped ? door->angle : -door->angle);
        const S_quat &rest = door->closedRot;
        door->frame->SetRot(Product(turn, rest, (turn.v.x * rest.v.x + turn.v.z * rest.v.z) + turn.v.y * rest.v.y));
        door->frame->Update();
    }
}

void C_Vehicle::UpdateWindows(float timeDelta)                                  // 0x00501ab0
{
    for (size_t i = m_seats.size(); i-- > 0;) {
        S_VehicleDoor *door = m_seats[i].door;
        if (!door || !door->windowMoving)
            continue;
        if (door->windowPart != 0xFFFF && (m_parts[door->windowPart].state & VEHICLE_PART_STATE_1)) {
            door->windowMoving = false;
            continue;
        }
        // Lower, it moves 0.1 inwards (by its side).
        const auto slide = [door] {
            const float up = (door->windowY - door->windowLowY) + (door->windowY - door->windowLowY);
            const float in = (1.0f - up) * 0.1f;
            door->windowX = door->windowPos.x > 0.0f ? in + door->windowPos.x : door->windowPos.x - in;
        };
        const float step = timeDelta * door->unknown60;
        if (door->windowDown) {
            door->windowY = door->windowY - step;
            if (door->windowY < door->windowLowY) {
                door->windowY = door->windowLowY;
                door->windowMoving = false;
                door->window->SetOn(false);
            } else {
                slide();
            }
        } else {
            door->windowY = step + door->windowY;
            door->window->SetOn(true);
            slide();
            if (door->windowY > door->windowPos.y) {
                door->windowX = door->windowPos.x;
                door->windowY = door->windowPos.y;
                door->windowMoving = false;
            }
        }
        door->window->SetPosInline(S_vector(door->windowX, door->windowY, door->windowPos.z));
        door->window->Update();
    }
}

void C_Vehicle::UpdateWheelSpin(float timeDelta)                                // 0x00507a40
{
    // The engine's turn of the largest wheel this step.
    const float engineTurn = TWO_PI / m_gearRatios[m_gear + 1] * m_engineRevs * timeDelta;
    // With a tracked wheel on the ground the tracks move by the middle wheels' movement (the even
    // wheels by the next one's, the odd ones by the middle one's).
    bool trackOnGround = false;
    float trackEven = 0.0f, trackOdd = 0.0f;
    for (int i = m_wheelCount; i-- > 0;) {
        const S_VehicleWheel *w = m_wheels[i];
        if (w && (w->flags & WHEEL_TRACKED) && (w->flags & WHEEL_ON_GROUND) && !(w->flags & WHEEL_DUMMY)) {
            trackOnGround = true;
            const S_VehicleWheel *middle = m_wheels[m_wheelCount / 2];
            const S_VehicleWheel *next = m_wheels[m_wheelCount / 2 + 1];
            trackOdd = DotZYX(middle->unknown0B0, middle->unknown0C0);
            trackEven = DotZYX(next->unknown0B0, next->unknown0C0);
            break;
        }
    }

    for (int i = m_wheelCount; i-- > 0;) {
        S_VehicleWheel *w = m_wheels[i];
        if (!w || (w->flags & WHEEL_DUMMY))             // (Fix: a missing wheel is skipped)
            continue;
        const uint32_t flags = w->flags;
        const float driven = m_maxWheelRadius / w->radius * engineTurn;
        bool turn = true;
        if (flags & WHEEL_TRACKED) {
            if (flags & WHEEL_DETACHED) {
                turn = false;
            } else if (m_unknown229F) {
                if (m_gear > 0)
                    w->unknown1F0 = w->unknown1F0 - driven;
                else if (m_gear == -1)
                    w->unknown1F0 = driven + w->unknown1F0;
            } else {
                float move;
                if (trackOnGround) {
                    move = i % 2 == 0 ? trackEven : trackOdd;
                } else {
                    // Quirk kept: the engine's turn divided by the radius once more.
                    move = m_gear < 0 ? -driven : driven;
                }
                w->unknown1F0 = w->unknown1F0 - move / w->radius;
            }
        } else if ((flags & WHEEL_LOCKED) || (flags & WHEEL_DETACHED)) {
            turn = false;
        } else if (!(flags & WHEEL_DRIVEN)) {
            w->unknown1F0 = w->unknown1F0 - DotZYX(w->unknown0B0, w->unknown0C0) / w->radius;
        } else if (flags & WHEEL_SPINNING) {
            if (m_gear > 0)
                w->unknown1F0 = w->unknown1F0 - driven;
            else if (m_gear == -1)
                w->unknown1F0 = driven + w->unknown1F0;
        } else {
            float move = 0.0f;
            if (flags & WHEEL_ON_GROUND)
                move = DotZYX(w->unknown0C0, w->unknown0B0);
            else if (m_unknown904 < 0.1f || m_gear == 0)
                move = DotZYX(XYZ(m_modelAxisZ), w->unknown0C0);
            else if (m_gear > 0)
                move = driven;
            else if (m_gear == -1)
                move = -driven;
            w->unknown1F0 = w->unknown1F0 - move / w->radius;
        }
        if (turn) {
            if (w->unknown1F0 > TWO_PI)
                w->unknown1F0 = w->unknown1F0 - TWO_PI;
            else if (w->unknown1F0 < 0.0f)
                w->unknown1F0 = w->unknown1F0 + TWO_PI;
            S_quat spin, tilt;
            spin.Make(S_vector(1.0f, 0.0f, 0.0f), w->unknown1F0);
            tilt.Make(S_vector(0.0f, 0.0f, 1.0f), w->bend);
            w->frame->SetRot(ProductZYX(ProductZYX(tilt, spin), w->unknown010));
        }
        w->unknown0C0 = S_vector(0.0f, 0.0f, 0.0f);
    }
}

void C_Vehicle::UpdateTracks(float timeDelta)                                   // 0x00506700
{
    // The vertices of each strip's wheels: through the strip's matrix into the model, at the
    // holder's height plus their offset, and back (the matrices kept while the strip repeats).
    I3D_frame *strip = nullptr;
    S_matrix matrix, inverse;
    const auto follow = [&](std::vector<S_VehicleTrack> &tracks) {
        for (size_t i = tracks.size(); i-- > 0;) {
            S_VehicleTrack &track = tracks[i];
            if (track.strip != strip) {
                strip = track.strip;
                matrix = strip->GetLocalMatrix();
                inverse.Inverse(matrix);
            }
            const float holderY = m_wheels[track.wheel]->holder->GetPos().y;
            I3D_mesh *mesh = static_cast<I3D_visual *>(strip)->GetVisualMesh();
            I3D_mesh_level *level = mesh->GetLevel(0);
            auto *vertices = static_cast<uint8_t *>(level->GetVertices(0));
            for (size_t k = track.vertices.size(); k-- > 0;) {
                auto *pos = reinterpret_cast<S_vector *>(vertices + track.vertices[k] * 0x20);
                S_vector v = *pos;
                v *= matrix;
                v.y = holderY + track.offsets[k];
                v *= inverse;
                *pos = v;
            }
            level->UnlockVertices();
            level->ComputeBounds();
            mesh->ComputeBounds();
            static_cast<I3D_object_visual *>(strip)->RefreshLevel(0);
            strip->Update();
        }
    };
    follow(m_tracksL);
    follow(m_tracksR);

    // The texture scrolls by the track speed (the engine's while +0x229F, else the middle wheels'
    // contact velocity along the car), wrapped into -1..1.
    const auto scroll = [&](std::vector<S_VehicleTrack> &tracks, float &offset, int wheel) {
        if (tracks.empty())
            return;
        float speed;
        if (m_unknown229F) {
            speed = m_engineRevs / m_gearRatios[m_gear + 1] * m_maxWheelCircumference;
            if (m_gear < 0)
                speed = -speed;
        } else {
            speed = DotZYX(XYZ(m_modelAxisZ), m_wheels[wheel]->unknown0A0);
        }
        offset = speed * m_unknown518 * timeDelta;
        if (offset > 1.0f)
            offset = offset - static_cast<float>(std::floor(static_cast<double>(offset)));
        else if (offset < -1.0f)
            offset = offset - static_cast<float>(std::ceil(static_cast<double>(offset)));
        const I3D_frame *last = nullptr;
        for (size_t i = tracks.size(); i-- > 0;) {
            I3D_frame *frame = tracks[i].strip;
            if (frame == last)
                continue;
            last = frame;
            if (frame->GetType() != FRAME_VISUAL || frame->GetVisualType() != I3DVISUAL_OBJECT)
                continue;
            I3D_mesh *mesh = static_cast<I3D_visual *>(frame)->GetVisualMesh();
            I3D_mesh_level *level = mesh->GetLevel(0);
            auto *vertices = static_cast<uint8_t *>(level->GetVertices(0));
            for (int v = level->NumVertices(); v-- > 0;) {
                float &u = *reinterpret_cast<float *>(vertices + v * 0x20 + 0x18);
                u = offset + u;
            }
            level->UnlockVertices();
            level->ComputeBounds();
            mesh->ComputeBounds();
            static_cast<I3D_object_visual *>(frame)->RefreshLevel(0);
            frame->Update();
        }
    };
    scroll(m_tracksL, m_unknown51C, m_wheelCount / 2 + 1);
    scroll(m_tracksR, m_unknown520, m_wheelCount / 2);
}
