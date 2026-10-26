// C_Car: the seats (original 0x004d93c0..0x004da2a0 C_Actor's seat slots, 0x004d11c0 SetDriver,
// 0x004d1200 SetDriven, 0x004da2f0 PutOutside, 0x004da420 GetSeatEntry): the vehicle's seats
// (S_VehicleSeat), who sits in them and the seat weapons (game/seat_weapon.h).
#include "game/car.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/car_internal.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/seat_weapon.h"
#include "ls3d/i3d.h"
#include "script/script.h"

namespace {

// The camera dummies (wildcard names; 0x008392ec, 0x008392a8).
const char *const CAMERA_3RD = "camera3RD*";
const char *const CAMERA_FPV = "cameraFPV*";

// S_vector::Normalize from a square the caller summed (the last bit follows its order).
void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f)
        v *= 1.0f / std::sqrt(square);
    else if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
}

float SquareYZX(const S_vector &v) { return (v.y * v.y + v.z * v.z) + v.x * v.x; }

}  // namespace

// --- the occupants -------------------------------------------------------------------------------

bool C_Car::IsSeatBlocked(int seat)                     // 0x004d93c0
{
    // Fix: a seat out of range is not taken (the original reads past the seats).
    if (!HasSeat(seat))
        return false;
    return (m_vehicle.m_seats[seat].flags & SEAT_FLAG_TAKEN) != 0;
}

bool C_Car::SetSeatBlocked(int seat, bool blocked)      // 0x004d93e0
{
    if (!HasSeat(seat))
        return false;
    uint32_t &flags = m_vehicle.m_seats[seat].flags;
    if (blocked) {
        flags |= SEAT_FLAG_TAKEN;
        return true;
    }
    flags &= ~SEAT_FLAG_TAKEN;
    return false;
}

void C_Car::SetSeatOccupant(int seat, C_Actor *occupant)   // 0x004d9430
{
    // Dropped: the copy protection (the 0x008ca840 / 0x008caf40 check): in single player from
    // campaign mission 6 on, a cracked copy's car lost its fuel here.
    if (IsNetServer())
        SendFuel();
    if (!HasSeat(seat))
        return;
    const C_Actor *taken = m_vehicle.m_seats[seat].occupant;
    if (taken && taken != occupant)
        return;
    // Out of the seats it has (Quirk kept: without an occupant every empty seat is left).
    for (int i = static_cast<int>(m_vehicle.m_seats.size()); i-- > 0;) {
        if (m_vehicle.m_seats[i].occupant == occupant)
            LeaveSeat(i);
    }
    if (C_SeatWeapon *weapon = GetSeatWeapon(seat))
        weapon->SetGunner(static_cast<C_Human *>(occupant));

    if (occupant) {
        m_vehicle.SetSeatLoad(seat, 80.0f);
        // The seat dummy's world z axis.
        const S_matrix &seatMatrix = m_vehicle.m_seats[seat].frame->GetMatrix();
        S_vector dir = seatMatrix(2);
        NormalizeBy(dir, SquareYZX(dir));
        const S_vector4 facing = {dir.x, dir.y, dir.z, seatMatrix.m[2][3]};
        m_vehicle.m_seats[seat].occupant = occupant;

        I3D_frame *frame = occupant->GetFrame();
        frame->LinkTo(m_vehicle.m_seats[seat].frame, 0);
        frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
        frame->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        frame->Update();
        const S_matrix &at = frame->GetMatrix();
        occupant->SetPosition4({at.m[3][0], at.m[3][1], at.m[3][2], at.m[3][3]});
        C_Human *human = static_cast<C_Human *>(m_vehicle.m_seats[seat].occupant);
        human->SetViewDir(facing);
        human->SetFacingDir(facing);
        human->SetDirection4(facing);

        if (seat == 0) {
            SetDriver(human);
            // (Clients start it the slow way.)
            m_vehicle.SetEngine(true, (g_gameRole & GAME_ROLE_CLIENT) == 0);
            // 0x800000 unless the player or a squad member drives (not identified further).
            uint32_t a = static_cast<uint32_t>(m_vehicle.m_unknown538);
            uint32_t b = static_cast<uint32_t>(m_vehicle.m_unknown540);
            const int type = human->GetControllerType();
            if (type == HUMAN_CONTROLLER_PLAYER || type == HUMAN_CONTROLLER_SQUAD) {
                a &= ~0x800000u;
                b &= ~0x800000u;
            } else {
                a |= 0x800000u;
                b |= 0x800000u;
            }
            m_vehicle.m_unknown538 = static_cast<int>(a | 0x200000u);
            m_vehicle.m_unknown540 = static_cast<int>(b | 0x200000u);
            if (IsLocalGame()) {
                PeriodicLinks_SetRate(m_periodicLink, 3);
                PeriodicLinks_SetRate(human->GetPeriodicLink(), 3);
            }
        }
        m_vehicle.m_unknown2299 = 0;
        m_vehicle.m_unknown229A = 0;
        m_vehicle.m_unknown229B = 0;
        m_vehicle.m_unknown229C = 0;
    }

    GetFrame()->Update();
    if (const S_FrameData *data = GetFrame()->GetData()) {
        if (data->script)
            data->script->OnUse(1);
    }
    // Fix: an empty seat has no player (the original reads through null).
    if (occupant && static_cast<C_Human *>(occupant)->IsLocalPlayer())
        ShowSeatView(seat, GetGame()->IsFirstPersonView());
    if (IsNetServer()) {
        if (C_SeatWeapon *weapon = GetSeatWeapon(seat)) {
            auto *message = new C_SeatWeaponRoundsMessage;
            message->rounds = weapon->GetRounds();
            message->seat = seat;
            message->fromClient = true;
            m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
        }
    }
    if (IsLocalGame())
        m_vehicle.m_unknown600 |= VEHICLE_PHYSICS_KEEP_AWAKE;
}

C_Actor *C_Car::GetSeatOccupant(int seat)               // 0x004d99d0
{
    return HasSeat(seat) ? m_vehicle.m_seats[seat].occupant : nullptr;
}

int C_Car::FindSeat(const C_Actor *occupant)            // 0x004d9a00
{
    for (int i = static_cast<int>(m_vehicle.m_seats.size()); i-- > 0;) {
        if (m_vehicle.m_seats[i].occupant == occupant)
            return i;
    }
    return -1;
}

C_Actor *C_Car::LeaveSeat(int seat)                     // 0x004d9a40
{
    if (!HasSeat(seat))
        return nullptr;
    C_Actor *occupant = m_vehicle.m_seats[seat].occupant;
    if (occupant) {
        occupant->GetFrame()->LinkTo(GetGame()->GetScene()->GetPrimarySector(), I3DLINK_KEEP_WORLD);
        if (static_cast<C_Human *>(occupant)->IsLocalPlayer())
            ShowSeatView(seat, false);
    }
    m_vehicle.SetSeatLoad(seat, 0.0f);
    m_vehicle.m_seats[seat].occupant = nullptr;
    if (C_CameraFrameKeeper *camera = m_vehicle.m_seats[seat].camera)
        camera->Restore();
    if (seat == 0)
        StopDriving();
    if (C_SeatWeapon *weapon = GetSeatWeapon(seat))
        weapon->SetGunner(nullptr);
    m_vehicle.m_seats[seat].flags &= ~SEAT_FLAG_TAKEN;
    GetFrame()->Update();
    UpdateKeepAwake();
    if (!IsLocalGame() && occupant)
        RememberLeaving(seat, occupant);
    return occupant;
}

bool C_Car::ExitSeat(int seat)                          // 0x004d9be0
{
    if (!HasSeat(seat))
        return false;
    C_Actor *occupant = m_vehicle.m_seats[seat].occupant;
    if (!occupant || !PutOutside(occupant))
        return false;
    C_Human *human = static_cast<C_Human *>(occupant);
    if (IsNetClient() && human->GetNetId() == g_localPlayerSlot && seat == 0)
        SendFuel();
    m_vehicle.SetSeatLoad(seat, 0.0f);
    m_vehicle.m_seats[seat].occupant = nullptr;
    if (C_SeatWeapon *weapon = GetSeatWeapon(seat))
        weapon->SetGunner(nullptr);
    if (seat == 0) {
        SetDriven(false);
        StopDriving();
        if (IsLocalGame()) {
            PeriodicLinks_SetRate(m_periodicLink, 4);
            PeriodicLinks_SetRate(human->GetPeriodicLink(), human->IsDead() ? 1 : 0);
        }
    }
    GetFrame()->Update();
    if (const S_FrameData *data = GetFrame()->GetData()) {
        if (data->script)
            data->script->OnUse(0);
    }
    if (human->IsLocalPlayer())
        ShowSeatView(seat, false);
    UpdateKeepAwake();
    // The local player lets go of the controls (in network games only the driver's).
    if (human->IsLocalPlayer() && (seat == 0 || IsLocalGame()))
        SetInput(0.0f, 0.0f);
    if (!IsLocalGame())
        RememberLeaving(seat, occupant);
    return true;
}

void C_Car::SetDriver(C_Human *driver)                  // 0x004d11c0
{
    m_driver = driver;
    m_vehicle.m_driven = driver != nullptr;
    // (With a driver the original then calls an empty function, 0x00611080.)
}

void C_Car::StopDriving()
{
    SetDriver(nullptr);
    m_vehicle.SetEngine(false, !m_vehicle.m_engineOn);
    m_vehicle.SetUnknown4f2630(m_vehicle.m_topSpeed);
    m_vehicle.SetHandbrake(false, false);
}

void C_Car::UpdateKeepAwake()
{
    bool empty = true;
    for (int i = static_cast<int>(m_vehicle.m_seats.size()); i-- > 0;) {
        if (GetSeatOccupant(i)) {
            empty = false;
            break;
        }
    }
    if (!IsLocalGame())
        return;
    if (empty)
        m_vehicle.m_unknown600 |= VEHICLE_PHYSICS_KEEP_AWAKE;
    else
        m_vehicle.m_unknown600 &= ~VEHICLE_PHYSICS_KEEP_AWAKE;
}

void C_Car::RememberLeaving(int seat, const C_Actor *occupant)
{
    // Fix: only while the mission's arrays exist (StartMission .. StopMission; the original writes
    // through null outside them).
    if (!m_seatLeftIds || !m_seatLeftTimes)
        return;
    m_seatLeftIds[seat] = occupant->GetNetId();
    m_seatLeftTimes[seat] = static_cast<int>(GetPerfTimeMs());
}

// --- the seats' properties -----------------------------------------------------------------------

uint32_t C_Car::GetSeatFlags(int seat)                  // 0x004d9fc0
{
    return HasSeat(seat) ? m_vehicle.m_seats[seat].flags : 0;
}

I3D_frame *C_Car::GetSeatCamera(int seat)               // 0x004d9e10
{
    if (!HasSeat(seat))
        return nullptr;
    const auto cameraName = [this, seat] {
        return GetGame()->IsFirstPersonView() || IsSeatFirstPersonOnly(seat) ? CAMERA_FPV : CAMERA_3RD;
    };
    const uint32_t mask = ENUMF_WILDMASK | FRAME_MASK_DUMMY;
    if (!(GetSeatFlags(seat) & SEAT_FLAG_HIDDEN) && seat != 0 && !HasSeatWeapon(seat)) {
        // A passenger looks through its own dummies.
        const C_Actor *occupant = m_vehicle.m_seats[seat].occupant;
        // Fix: an empty seat has none (the original reads through null).
        if (!occupant)
            return nullptr;
        return occupant->GetFrame()->FindChildFrame(cameraName(), mask);
    }
    // The seat dummy's (else its parent's), looked for with the occupant unlinked.
    if (const C_Actor *occupant = m_vehicle.m_seats[seat].occupant)
        occupant->GetFrame()->LinkTo(nullptr, 0);
    I3D_frame *camera = m_vehicle.m_seats[seat].frame->FindChildFrame(cameraName(), mask);
    if (!camera)
        camera = m_vehicle.m_seats[seat].frame->GetParent()->FindChildFrame(cameraName(), mask);
    if (const C_Actor *occupant = m_vehicle.m_seats[seat].occupant) {
        occupant->GetFrame()->LinkTo(m_vehicle.m_seats[seat].frame, 0);
        occupant->GetFrame()->Update();
    }
    return camera;
}

int C_Car::GetSeatText(int seat)                        // 0x004d9ff0
{
    return HasSeat(seat) ? m_vehicle.m_seats[seat].text : 0;
}

bool C_Car::IsSeatFirstPersonOnly(int seat)             // 0x004da2a0
{
    if (!HasSeat(seat) || !m_vehicle.m_seats[seat].frame)
        return false;
    return std::strstr(m_vehicle.m_seats[seat].frame->GetUserProps(), "NO3RD") != nullptr;
}

void C_Car::ShowSeatView(int seat, bool on)             // 0x004da090
{
    if (C_SeatWeapon *weapon = GetSeatWeapon(seat)) {
        weapon->ShowView(on);
        return;
    }
    char name[128];
    std::sprintf(name, "VIEW%02d", seat);
    if (I3D_frame *view = GetFrame()->FindChildFrame(name, ENUMF_WILDMASK | FRAME_MASK_VISUAL))
        view->SetOn(on);
}

// --- the seat weapons ----------------------------------------------------------------------------

C_SeatWeapon *C_Car::GetSeatWeapon(int seat) const
{
    // Fix: no weapon past the ten (the original reads past m_seatWeapons).
    if (seat < 0 || seat >= static_cast<int>(sizeof m_seatWeapons / sizeof *m_seatWeapons))
        return nullptr;
    return m_seatWeapons[seat];
}

bool C_Car::HasSeatWeapon(int seat)                     // 0x004da020
{
    return GetSeatWeapon(seat) != nullptr;
}

int C_Car::GetSeatWeaponRounds(int seat)                // 0x004da040
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon ? weapon->GetRounds() : 0;
}

S_vector C_Car::GetSeatWeaponAim(int seat)              // 0x004da150
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon ? weapon->GetAim() : S_vector(0.0f, 0.0f, 0.0f);
}

S_vector C_Car::GetSeatWeaponOrigin(int seat)           // 0x004da110
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon ? weapon->GetOrigin() : S_vector(0.0f, 0.0f, 0.0f);
}

float C_Car::UnknownAC(int seat)                        // 0x004da190
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon ? weapon->UnknownAC() : 3.14159265f;
}

int C_Car::GetSeatWeaponText(int seat)                  // 0x004da1b0
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon ? weapon->GetItemText() : -1;
}

C_ItemDef *C_Car::GetSeatWeaponDef(int seat)            // 0x004da1d0
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon ? weapon->GetItemDef() : nullptr;
}

void C_Car::GetSeatWeaponLimits(int seat, float *a, float *b, float *c, float *d)   // 0x004da1f0
{
    if (C_SeatWeapon *weapon = GetSeatWeapon(seat))
        weapon->GetLimits(a, b, c, d);
}

bool C_Car::CanSeatWeaponAim(int seat, float *pos)      // 0x004da240
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon && weapon->CanAim(pos);
}

void C_Car::ApplySeatControls(int seat, float *axes, float *buttons)   // 0x004da060
{
    if (C_SeatWeapon *weapon = GetSeatWeapon(seat))
        weapon->ApplyControls(axes, buttons);
}

bool C_Car::UnknownC8(int seat, int *a, int *b, int *c)  // 0x004da270
{
    C_SeatWeapon *weapon = GetSeatWeapon(seat);
    return weapon && weapon->UnknownC8(a, b, c);
}

// --- getting out ---------------------------------------------------------------------------------

bool C_Car::PutOutside(C_Actor *occupant)               // 0x004da2f0
{
    if (!occupant)
        return false;
    const int seat = FindSeat(occupant);
    if (seat == -1)
        return false;
    S_vector4 pos, dir;
    bool found = GetSeatEntry(seat, &pos, &dir);
    for (int i = 0; !found && i < static_cast<int>(m_vehicle.m_seats.size()); ++i) {
        if (i != seat)
            found = GetSeatEntry(i, &pos, &dir);
    }
    if (!found)
        return false;
    I3D_frame *frame = occupant->GetFrame();
    frame->LinkTo(GetGame()->GetScene()->GetPrimarySector(), 0);
    occupant->SetDirection4(dir);
    occupant->SetPosition4(pos);
    frame->SetPos(S_vector(pos.x, pos.y, pos.z));
    frame->SetWorldDir(S_vector(dir.x, dir.y, dir.z), 0.0f);
    frame->Update();
    return true;
}

bool C_Car::GetSeatEntry(int seat, S_vector4 *pos, S_vector4 *dir)   // 0x004da420
{
    if (!HasSeat(seat))
        return false;
    const bool behind = (GetSeatFlags(seat) & SEAT_FLAG_200) != 0;
    // The side of the seat: its position in the body's space.
    const S_vector &seatAt = m_vehicle.m_seats[seat].frame->GetWorldPos();
    S_vector local = seatAt;
    I3D_frame *body = m_vehicle.m_body;
    S_matrix inverse = body->GetMatrix();
    const S_matrix source = inverse;
    inverse.Inverse(source);
    local *= inverse;
    const bool left = local.x < 0.0f;

    // The step out: the body's level x axis, three quarters of the box's width (and half a metre
    // more to the side; none behind the car).
    const S_matrix &bodyMatrix = body->GetMatrix();
    S_vector side(bodyMatrix.m[0][0], 0.0f, bodyMatrix.m[0][2]);
    side.Normalize();
    const float width = (m_vehicle.m_bodyMax.x - m_vehicle.m_bodyMin.x) * 0.75f + (behind ? 0.0f : 0.5f);
    side.Normalize();
    const float reach = left ? -width : width;
    const S_vector step(side.x * reach, reach * side.y, side.z * reach);
    // From the body's centre line at the seat's height (behind the car: a metre behind its box).
    S_vector start(0.0f, local.y, behind ? m_vehicle.m_bodyMin.z - 1.0f : local.z);
    start *= body->GetMatrix();

    if (!m_vehicle.m_specialVolumesOn)
        g_collisionWorld.SetIgnoredVolume(&m_vehicle.m_bodyVolume, 0);
    else
        g_collisionWorld.SetIgnoredVolumes(m_vehicle.m_specialVolumeList.data(),
                                           static_cast<uint32_t>(m_vehicle.m_specialVolumeList.size()), 0);
    // Room for a 0.25 m sphere moved out a metre up, then the ground below the end (from 1.5 m
    // up, 3.5 m down).
    S_vector hit;
    if (!CollisionSphereTest(S_vector(start.x, start.y + 1.0f, start.z), step, 0.25f, &hit, nullptr, 0x102, 0)) {
        start = S_vector(step.x + start.x, start.y + step.y, step.z + start.z);
        if (CollisionSphereTest(S_vector(start.x, start.y + 1.5f, start.z), S_vector(0.0f, -3.5f, 0.0f), 0.25f,
                                &hit, nullptr, 0x102, 0)) {
            g_collisionWorld.SetIgnoredVolume(nullptr, 0);
            // (The original's w of both is stack garbage.)
            *pos = {hit.x, hit.y - 0.2f, hit.z, 1.0f};
            // Facing the seat, level.
            const S_vector &target = m_vehicle.m_seats[seat].frame->GetWorldPos();
            S_vector toSeat(target.x - pos->x, 0.0f, target.z - pos->z);
            toSeat.Normalize();
            *dir = {toSeat.x, toSeat.y, toSeat.z, 0.0f};
            return true;
        }
    }
    g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    return false;
}
