// C_Car: the controls and the updates (original 0x004d9050 ApplyControls, 0x004d9280 SetInput;
// 0x004d3110 Update, 0x004d2790 Tick and their helpers 0x004d38f0 UpdateVehicle, 0x004d3970
// UpdateDetail, 0x004d2c50 CheckWater, 0x004d2dc0 GetEngineNoise; 0x004d5430 IsDrivable,
// 0x004dd090 GetEnvironmentFlags; 0x004db2a0 Restart, 0x004db2b0 / 0x004db2e0 SendMessage(Ex),
// 0x004e0760 / 0x004e0770 and 0x004e07b0 / 0x004e07c0 the kind-only messages' slots).
#include "game/car.h"

#include <cfloat>
#include <cstdint>

#include "game/actors_module.h"                         // CreateActorMessage, SendActorMessage
#include "game/car_internal.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"                               // g_gameProfile, g_mpSettings
#include "game/seat_weapon.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

// The driver's restrictions on the throttle and the steering (inline in ApplyControls and
// SetInput).
void Restrict(uint32_t restrictions, float &throttle, float &steering)
{
    if (restrictions & CAR_NO_THROTTLE)
        throttle = 0.0f;
    if ((restrictions & (CAR_PULLS_LEFT | CAR_PULLS_RIGHT)) == (CAR_PULLS_LEFT | CAR_PULLS_RIGHT)) {
        throttle = 0.0f;
        steering = 0.0f;
        return;
    }
    if (restrictions & CAR_PULLS_LEFT) {
        if (steering < 0.0f)
            steering = 0.0f;
        if (throttle == 0.0f || steering != 0.0f)
            return;
        steering = -1.0f;
    } else if (restrictions & CAR_PULLS_RIGHT) {
        if (steering > 0.0f)
            steering = 0.0f;
        if (throttle == 0.0f || steering != 0.0f)
            return;
        steering = 1.0f;
    } else {
        return;
    }
    throttle *= 0.3f;
}

}  // namespace

float C_Car::GetUprightness() const
{
    return (m_vehicle.m_modelAxisY.z + m_vehicle.m_modelAxisY.x) * 0.0f + m_vehicle.m_modelAxisY.y;
}

// --- the messages ------------------------------------------------------------------------------------

void C_CarMessage::Write(C_BitStream *stream)           // 0x004e0770 (0x004e07c0 the same)
{
    stream->WriteBits(static_cast<uint32_t>(m_kind), CAR_MSG_KIND_BITS);
}

void C_Car::SendMessage(C_CarMessage &msg)              // 0x004db2b0
{
    C_BitStream *stream = CreateActorMessage(GetNetId(), false);
    msg.Write(stream);
    SendActorMessage(stream);
}

void C_Car::SendMessageEx(C_CarMessage &msg)            // 0x004db2e0
{
    C_BitStream *stream = CreateActorMessageEx(GetNetId(), false);
    msg.Write(stream);
    SendActorMessage(stream);
}

// --- the controls ------------------------------------------------------------------------------------

void C_Car::ApplyControls(float *axes, float * /*buttons*/)   // 0x004d9050
{
    if (!m_driver || (!IsLocalGame() && !m_driver->IsLocalPlayer()))
        return;
    if (!m_vehicle.m_engineOn && m_vehicle.m_fuel > 0.001f)
        m_vehicle.SetEngine(true, false);
    float throttle = axes[0];
    float steering = axes[3] - axes[2];
    if (!m_driver || m_driver->IsDead()) {
        m_vehicle.SetThrottle(0.0f, true);
        m_vehicle.SetSteering(0.0f);
        m_vehicle.SetBrake(0.0f);
        m_vehicle.SetHandbrake(false, true);
        m_vehicle.SetUnknown4f23b0(0.0f);
        return;
    }
    Restrict(m_restrictions, throttle, steering);
    m_vehicle.SetThrottle(throttle, true);
    m_vehicle.SetSteering(steering);
    m_vehicle.SetBrake(axes[1]);
    m_vehicle.SetHandbrake(axes[18] != 0.0f, !(throttle > 0.0f));
    m_vehicle.SetUnknown4f2630(m_vehicle.m_topSpeed);
    m_vehicle.SetUnknown4f23b0(1.0f - axes[61]);
}

bool C_Car::SetInput(float throttle, float steering)    // 0x004d9280
{
    if (!m_driver || m_driver->IsDead())
        return false;
    Restrict(m_restrictions, throttle, steering);
    if (IsLocalGame() || m_driver->GetNetId() == g_localPlayerSlot) {
        m_vehicle.SetThrottle(throttle, true);
        m_vehicle.SetSteering(steering);
    }
    return true;
}

// --- the car's state -----------------------------------------------------------------------------------

bool C_Car::IsDrivable(uint32_t checks) const           // 0x004d5430
{
    const C_Vehicle &v = m_vehicle;
    if (checks & CAR_DRIVABLE_INTACT) {
        if (v.m_unknown610 <= 0.0f || v.m_unknown63C <= 0.0f || v.m_fuel <= 0.0f)
            return false;
        for (int i = v.m_wheelCount; i-- > 0;) {
            // Fix: a missing wheel is skipped (the original reads through null; also below).
            if (v.m_wheels[i] && (v.m_wheels[i]->flags & WHEEL_DETACHED))
                return false;
        }
    }
    if ((checks & CAR_DRIVABLE_ON_WHEELS) && v.m_speed < 1.0f) {
        int grounded = 0;
        for (int i = v.m_wheelCount; i-- > 0;) {
            if (v.m_wheels[i] && (v.m_wheels[i]->flags & WHEEL_ON_GROUND))
                ++grounded;
        }
        if (grounded < 2 || GetUprightness() < 0.6f)
            return false;
    }
    if (checks & CAR_DRIVABLE_BOTH_SIDES) {
        // A wheel standing on each side (the wheels go left, right per axle).
        bool left = false, right = false;
        for (int i = v.m_wheelCount; i-- > 0;) {
            const S_VehicleWheel *w = v.m_wheels[i];
            if (w && (w->flags & WHEEL_ON_GROUND) && !(w->flags & WHEEL_DETACHED))
                (i % 2 ? right : left) = true;
        }
        if (!right || !left)
            return false;
    }
    return true;
}

uint32_t C_Car::GetEnvironmentFlags() const             // 0x004dd090
{
    uint32_t flags = 0;
    for (int i = m_vehicle.m_wheelCount; i-- > 0;) {
        const S_VehicleWheel *w = m_vehicle.m_wheels[i];
        if (w && (w->flags & WHEEL_ON_GROUND) && w->ground)   // (Fix: a missing wheel)
            flags |= w->ground->flags & (HUMAN_ENV_DEADLY | HUMAN_ENV_RESTRICTED);
    }
    return flags;
}

void C_Car::GetEngineNoise(float *volume, float *range) const   // 0x004d2dc0
{
    *range = m_vehicle.m_unknownEE8[1];
    *volume = 0.0f;
    if (!m_vehicle.m_engineOn)
        return;
    const auto louder = [volume](const I3D_sound *sound) {
        if (sound && sound->IsOn() && *volume < sound->GetVolumeInline())
            *volume = sound->GetVolumeInline();
    };
    // Group 2's engine layers, then its single loop.
    for (int i = 4; i < 19; ++i)
        louder(m_vehicle.m_sounds2[i]);
    louder(m_vehicle.m_sounds2[3]);
}

void C_Car::CheckWater()                                // 0x004d2c50
{
    float bottom, top;
    g_collisionWorld.GetWaterHeightRange(bottom, top);
    if (top == -FLT_MAX)                                // (no water)
        return;
    if (!(GetFrame()->GetWorldPos().y < top) || !(m_vehicle.m_unknown610 > 0.0f))
        return;
    // Water above the car: the engine dies, the crew drowns.
    if (!g_collisionWorld.RayTestWater(GetFrame()->GetWorldPos(), S_vector(0.0f, 100.0f, 0.0f), nullptr, nullptr, 0))
        return;
    m_vehicle.m_unknown610 = 0.0f;
    for (int i = static_cast<int>(m_vehicle.m_seats.size()); i-- > 0;) {
        if (C_Actor *occupant = GetSeatOccupant(i)) {
            const S_vector zero(0.0f, 0.0f, 0.0f);
            occupant->Hit(HIT_DROWNING, 1000.0f, zero, zero, nullptr, 0);
        }
    }
    if (!IsLocalGame() && !g_gameProfile.IsCooperativeGame() && g_mpSettings->gameType != MP_GAME_OBJECTIVES)
        m_respawnTimer.Start();
}

// --- the updates -----------------------------------------------------------------------------------------

void C_Car::UpdateVehicle(int time)                     // 0x004d38f0
{
    m_vehicle.Update(static_cast<uint32_t>(time));
    // (Idle or lost, the original then calls an empty function, 0x00611080.)
    SetDirection4(m_vehicle.m_modelAxisZ);
    SetPosition4(m_vehicle.m_modelPos);
    UpdateDetail();
}

void C_Car::UpdateDetail()                              // 0x004d3970
{
    // The options this does not touch; all it sets (a wreck: m_wrecked); a dedicated server's.
    constexpr uint32_t KEPT = 0x7FFC2026;
    constexpr uint32_t FULL = 0x800313D1;
    constexpr uint32_t SERVER = 0x8000CC10;
    uint32_t &options = m_vehicle.m_unknownF6C;
    options &= KEPT;
    if (m_wrecked) {
        options |= FULL;
        return;
    }
    if (!IsLocalGame()) {
        if (!(g_gameRole & GAME_ROLE_CLIENT)) {
            options |= SERVER;
            return;
        }
        if (g_gameRole & GAME_ROLE_SERVER)
            options |= SERVER;
    }
    // By the square of the distance to the active camera (and whether the car was drawn).
    const S_vector &camera = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
    const S_vector &at = GetFrame()->GetWorldPos();
    const float dx = at.x - camera.x, dy = at.y - camera.y, dz = at.z - camera.z;
    const float d2 = (dz * dz + dx * dx) + dy * dy;
    const bool shown = m_vehicle.m_unknown2292 != 0;
    if (d2 < 900.0f)
        options |= VEHICLE_SIM_SKID_MARKS | 0x1;
    if (shown || d2 < 900.0f)
        options |= VEHICLE_SIM_EFFECTS;
    if (shown || d2 < 600.0f)
        options |= VEHICLE_SIM_LAMP_SPOT_LIGHTS | VEHICLE_SIM_LAMP_PROJECTORS;
    if (d2 < 22500.0f)
        options |= VEHICLE_SIM_LAMPS;
    if (shown || d2 < 400.0f)
        options |= VEHICLE_SIM_IMPACT_DAMAGE | VEHICLE_SIM_DEFORM;
    if (shown && d2 < 600.0f)
        options |= VEHICLE_SIM_RAY_EVERY_WHEEL;
    else
        options |= VEHICLE_SIM_WHEEL_PLANES;
    options |= VEHICLE_SIM_BODY_COLLISION;
    if (d2 < 900.0f)
        options |= VEHICLE_SIM_COLLIDE_EACH_SUBSTEP;
    options |= 0x80000000u;
    m_vehicle.EnableSounds(d2 < 10000.0f && m_vehicle.m_model->IsOn());
}

void C_Car::ForgetLeaving()
{
    // Fix: only while the mission's arrays exist (the original reads through null outside them).
    if (!m_seatLeftIds || !m_seatLeftTimes)
        return;
    for (int i = 0; i < static_cast<int>(m_vehicle.m_seats.size()); ++i) {
        if (m_seatLeftIds[i] != -1 && m_seatLeftTimes[i] + 3000 < static_cast<int>(GetPerfTimeMs()))
            m_seatLeftIds[i] = -1;
    }
}

void C_Car::Update(int frameTime)                       // 0x004d3110
{
    uint32_t time = static_cast<uint32_t>(frameTime);
    if (time > 83)
        time = 83;
    m_vehicle.m_unknown5C4 = m_vehicle.m_unknown934 * 0.9f;
    m_vehicle.m_unknown2292 = GetGame()->GetScene()->GetRenderStamp() == GetFrame()->GetRenderStamp();
    if (IsLocalGame())
        m_vehicle.Step(time);
    else
        UpdateNetwork(static_cast<int>(time));
    for (int i = 10; i-- > 0;) {
        if (m_seatWeapons[i])
            m_seatWeapons[i]->Update(static_cast<int>(time));
    }
    UpdateVehicle(static_cast<int>(time));
    if (!IsLocalGame()) {
        ReadMessages();
        // On deadly ground without a driver (but in co-op), or with a player at the wheel.
        if (!m_deadlyReported && (GetEnvironmentFlags() & HUMAN_ENV_DEADLY) &&
            ((!m_driver && !g_gameProfile.IsCooperativeGame()) ||
             (m_driver && m_driver->GetRole() == HUMAN_ROLE_PLAYER))) {
            m_deadlyReported = true;
            C_CarMessage msg(CAR_MSG_DEADLY_GROUND);
            SendMessageEx(msg);
        }
        ForgetLeaving();
    }
    for (const S_VehicleSeat &seat : m_vehicle.m_seats) {
        if (seat.camera)
            seat.camera->Update(static_cast<int>(time));
    }
    // The checkpoint marks, every 500 ms of driving (and once more after it stops).
    if (m_vehicle.m_speed != 0.0f || m_marksTime > 0) {
        m_marksTime += static_cast<int>(time);
        if (m_marksTime > 500) {
            m_marksTime = m_vehicle.m_speed != 0.0f ? 1 : 0;
            MarkCheckpoints();
        }
    }
    // (The original then looks each marked checkpoint up and does nothing with it.)
    GetFrame()->Tick(static_cast<int>(time));
}

void C_Car::Tick(int timeDelta)                         // 0x004d2790
{
    if (m_respawnTimer.Fire()) {
        C_CarMessage msg(CAR_MSG_RESTART);
        SendMessage(msg);
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            Restart();
        m_respawnTimer.running = false;
    }
    if (m_chargeTime != -1)
        UpdateCharge(timeDelta);
    if (m_stateChanged && IsNetServer()) {
        C_BitStream *msg = CreateActorMessage(GetNetId(), false);
        msg->WriteBits(CAR_MSG_STORE, CAR_MSG_KIND_BITS);
        WriteStore(msg);
        SendActorMessage(msg);
        m_stateChanged = false;
    }
    CheckWater();
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        UpdateNetwork(timeDelta);
    for (int i = 10; i-- > 0;) {
        if (m_seatWeapons[i])
            m_seatWeapons[i]->Tick(timeDelta);
    }
    if (!IsLocalGame()) {
        // Network games: a seat weapon short of ammo in the vehicle's store gets more every 30 s
        // (but in co-op); the clients are told.
        for (int i = 10; i-- > 0;) {
            C_SeatWeapon *weapon = m_seatWeapons[i];
            if (!weapon || !(weapon->GetRefillTime() + 30000 < GetGame()->GetMissionTime()))
                continue;
            C_ItemDef *def = weapon->GetItemDef();
            if (!def)                                   // (Fix: the original calls through null)
                continue;
            if (GetContainer(0)->CountItems(def->GetAmmoItemId()) > weapon->GetRefillBelow() ||
                g_gameProfile.IsCooperativeGame())
                continue;
            C_Item *ammo = g_itemsModule.CreateItem(weapon->GetItemDef()->GetAmmoItemId(), 0, nullptr, -1);
            C_Item *replaced = nullptr;
            GetContainer(0)->AddItem(ammo, &replaced, -1);
            auto *message = new C_SeatWeaponAmmoMessage;
            message->typeId = ammo->GetTypeId();
            message->uid = ammo->GetUid();
            message->fromClient = true;
            m_netObject->QueueMessage(message, g_netReplication->GetLocalId(), true);
            weapon->SetRefillTime(GetGame()->GetMissionTime());
        }
    }
    if (!(g_gameRole & GAME_ROLE_CLIENT)) {
        ForgetLeaving();
        UpdateVehicle(timeDelta);
        ReadMessages();
    }
    m_parts.Tick(timeDelta);
    // Single player: the crew bails out of a car stuck on its side.
    if (IsLocalGame() && !IsDestroyed() && m_vehicle.m_speed < 0.1f && !IsDrivable(CAR_DRIVABLE_ON_WHEELS) &&
        GetUprightness() < 0.707f) {
        for (int i = GetSeatCount(); i-- > 0;) {
            if (GetSeatOccupant(i))
                static_cast<C_Human *>(GetSeatOccupant(i))->RequestLeaveVehicle();
        }
    }
    // Single player: the engine's noise, for the AI to hear.
    if (IsLocalGame() && GetDriver()) {
        float volume = 0.0f, range = 0.0f;
        GetEngineNoise(&volume, &range);
        if (volume != 0.0f) {
            S_SoundRequest request;
            request.flags = SOUND_AI_ONLY;
            request.attachTo = GetFrame();
            request.source = GetDriver();
            request.chapter = 0;
            request.group = 0x2B;
            request.farScale = range;
            request.volume = volume;                    // (the original's posW is stack garbage)
            GetSoundModule()->Play(request);
        }
    }
}
