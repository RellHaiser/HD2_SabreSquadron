// C_Car: contacts, hits, the wreck and the charges (original 0x004d3ef0 OnCollision with 0x004d3d10
// TakePush and 0x004de800 IsSimulatedHere; 0x004dba10 Hit with 0x004d3b70 IsTyreHit; 0x004dbf00
// Wreck with 0x004dc580 BlastAround; 0x004dead0 / 0x004deb50 / 0x004dec10 the charges; 0x004dec60
// RayTest; the crew's queries 0x004ddcf0..0x004ddee0, 0x004dea90; 0x004dbed0 / 0x004dbee0 the
// materials) and the messages they send (0x004e0340..0x004e0830).
#include "game/car.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "game/actors_module.h"                         // g_actorsModule
#include "game/car_internal.h"
#include "game/collision.h"                             // CollisionRayTest
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"                               // g_netGame
#include "game/profile.h"                               // g_gameProfile, g_mpSettings
#include "game/team.h"                                  // g_teamManager
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "script/script.h"

std::vector<C_Actor *> g_blastActors;                   // 0x0089b4cc

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// Squares in the orders the original sums them.
float SquareXYZ(const S_vector &v) { return (v.x * v.x + v.y * v.y) + v.z * v.z; }
float SquareZXY(const S_vector &v) { return (v.z * v.z + v.x * v.x) + v.y * v.y; }
float SquareZYX(const S_vector &v) { return (v.z * v.z + v.y * v.y) + v.x * v.x; }

// S_vector::Magnitude and Normalize from a square summed in another order.
float LengthOf(float square)
{
    if (square < 1e-8f)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}

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

// The actors' OnCollision arguments: vectors as pointers in ints.
const S_vector &VectorArg(int arg) { return *reinterpret_cast<const S_vector *>(static_cast<intptr_t>(arg)); }

}  // namespace

// --- the messages ------------------------------------------------------------------------------------

void C_CarGlassMessage::Write(C_BitStream *stream)     // 0x004e03a0
{
    C_CarMessage::Write(stream);
    stream->WriteBits(static_cast<uint32_t>(part), 10);
    stream->WriteBits(static_cast<uint32_t>(health), 10);
    static_cast<C_NetMessage *>(stream)->WriteDirection3D(XYZ(dir), -1);
}

void C_CarGlassMessage::Read(C_BitStream *stream)      // 0x004e0340
{
    part = static_cast<int>(stream->ReadBits(10));
    health = static_cast<int>(stream->ReadBits(10));
    S_vector v;
    static_cast<C_NetMessage *>(stream)->ReadDirection3D(v, -1);
    dir.x = v.x;
    dir.y = v.y;
    dir.z = v.z;
}

void C_CarPartMessage::Write(C_BitStream *stream)      // 0x004e04b0
{
    C_CarMessage::Write(stream);
    stream->WriteBits(static_cast<uint32_t>(part), 10);
    stream->WriteBits(static_cast<uint32_t>(health), 10);
    if (health > 0)
        return;
    static_cast<C_NetMessage *>(stream)->WritePosition(XYZ(velocity));
    static_cast<C_NetMessage *>(stream)->WritePosition(XYZ(push));
}

void C_CarPartMessage::Read(C_BitStream *stream)       // 0x004e0410
{
    part = static_cast<int>(stream->ReadBits(10));
    health = static_cast<int>(stream->ReadBits(10));
    if (health > 0)
        return;
    S_vector v;
    static_cast<C_NetMessage *>(stream)->ReadPosition(v);
    velocity.x = v.x;
    velocity.y = v.y;
    velocity.z = v.z;
    static_cast<C_NetMessage *>(stream)->ReadPosition(v);
    push.x = v.x;
    push.y = v.y;
    push.z = v.z;
}

void C_CarTyreMessage::Write(C_BitStream *stream)      // 0x004e0650
{
    C_CarMessage::Write(stream);
    stream->WriteBits(static_cast<uint32_t>(wheel), 8);
}

void C_CarTyreMessage::Read(C_BitStream *stream)       // 0x004e0630
{
    wheel = static_cast<int>(stream->ReadBits(8));
}

void C_CarImpulseMessage::Write(C_BitStream *stream)   // 0x004e0830
{
    C_CarMessage::Write(stream);
    static_cast<C_NetMessage *>(stream)->WritePosition(XYZ(point));
    stream->WriteBits(static_cast<uint32_t>(actor), 10);
}

void C_CarImpulseMessage::Read(C_BitStream *stream)    // 0x004e0800
{
    S_vector v;
    static_cast<C_NetMessage *>(stream)->ReadPosition(v);
    point.x = v.x;
    point.y = v.y;
    point.z = v.z;
    actor = static_cast<int>(stream->ReadBits(10));
}

// --- contacts --------------------------------------------------------------------------------------------

bool C_Car::IsSimulatedHere()                           // 0x004de800
{
    if (IsLocalGame())
        return true;
    C_Actor *driver = GetSeatOccupant(0);
    if (driver == g_netGame.GetLocalHuman())
        return true;
    if (driver && static_cast<C_Human *>(driver)->GetRole() != HUMAN_ROLE_PLAYER && (g_gameRole & GAME_ROLE_SERVER))
        return true;
    return !driver && (g_gameRole & GAME_ROLE_SERVER);
}

void C_Car::OnCollision(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int /*a6*/)   // 0x004d3ef0
{
    const S_vector4 &point = *reinterpret_cast<const S_vector4 *>(pos);
    if (!IsSimulatedHere()) {
        if (!(g_gameRole & GAME_ROLE_SERVER) && static_cast<int>(GetPerfTimeMs()) - m_impulseSent > 1000) {
            m_impulseSent = static_cast<int>(GetPerfTimeMs());
            GetGame()->Print("send clsv car impuls");
            C_CarImpulseMessage msg(point, other->GetNetId());
            SendMessageEx(msg);
            return;
        }
        TakePush(other->GetNetId(), point);
        return;
    }
    // (The original then calls an empty function, 0x00611080.)
    const S_vector &push = VectorArg(a3);
    switch (other->GetType()) {
    case ACTOR_HUMAN: {
        // Only the turn, at the centre of mass (the model's height summed in).
        const S_vector &centre = XYZ(m_vehicle.m_unknown1B0);
        const S_vector at(centre.x, (m_vehicle.m_modelPos.y - centre.y) + centre.y, centre.z);
        m_vehicle.AddForce(push * 500.0f, at, 1, S_vector(1.0f, 1.0f, 1.0f));
        return;
    }
    case ACTOR_CAR: {
        const S_vector &normal = VectorArg(a4);
        if (m_vehicle.m_unknown550 && static_cast<C_Car *>(other)->m_vehicle.m_unknown550 && IsDestroyed()) {
            const float mass = m_vehicle.m_currentMass;
            m_vehicle.m_currentMass = m_vehicle.m_currentMass * 0.5f;
            m_vehicle.ApplyCarHit(other, 1.2f, point, push * 1.4f, normal, a5);
            m_vehicle.m_currentMass = mass;
            return;
        }
        m_vehicle.ApplyCarHit(other, 1.2f, point, push, normal, a5);
        return;
    }
    default:
        return;
    }
}

void C_Car::TakePush(int id, const S_vector4 &point)    // 0x004d3d10
{
    C_Actor *driver = GetSeatOccupant(0);
    if (!driver || driver->GetNetId() != g_localPlayerSlot) {
        if (!(g_gameRole & GAME_ROLE_SERVER))
            return;
        if (driver) {
            // The server: the driver's client simulates the car.
            if (static_cast<int>(GetPerfTimeMs()) - m_impulseSent <= 1000)
                return;
            m_impulseSent = static_cast<int>(GetPerfTimeMs());
            GetGame()->Print("send svcl car impuls");
            C_CarImpulseMessage msg(point, id);
            SendMessage(msg);
            return;
        }
    }
    C_Actor *other = g_actorsModule.GetActor(id);
    if (!other)
        return;
    GetGame()->Print("car do collision");
    if (other->GetType() != ACTOR_CAR)
        return;
    const C_Vehicle &pusher = static_cast<C_Car *>(other)->m_vehicle;
    const float share = pusher.m_currentMass > m_vehicle.m_currentMass ? 1.0f
                                                                       : pusher.m_currentMass / m_vehicle.m_currentMass;
    // (The original then calls an empty function, 0x00611080.)
    const float mass = m_vehicle.m_currentMass;
    const S_vector impulse(share * pusher.m_velocity.x * mass * 0.3f, share * pusher.m_velocity.y * mass * 0.3f,
                           share * pusher.m_velocity.z * mass * 0.3f);
    m_vehicle.ApplyImpulse(impulse, XYZ(point), 0, S_vector(1.0f, 1.0f, 1.0f));
}

// --- hits ------------------------------------------------------------------------------------------------

bool C_Car::IsTyreHit(int wheel, const S_vector &pos, bool axle) const   // 0x004d3b70
{
    if (m_vehicle.m_unknown550)
        return false;
    const S_VehicleWheel &w = *m_vehicle.m_wheels[wheel];
    float distance;
    if (axle) {
        S_matrix inverse;
        inverse.Inverse(w.frame->GetMatrix());
        const S_vector local = pos * inverse;
        distance = LengthOf(local.y * local.y + local.z * local.z);
    } else {
        distance = LengthOf(SquareZYX(XYZ(w.worldPos) - pos));
    }
    return distance > w.radius * 0.7f;
}

void C_Car::Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
                int /*flags*/)                          // 0x004dba10
{
    if (attacker && static_cast<C_Human *>(attacker)->GetRole() == HUMAN_ROLE_ENEMY &&
        !HasEnemyAboard(static_cast<C_Human *>(attacker)))
        return;
    if (m_wrecked && type == HIT_EXPLOSION)
        return;
    m_lastAttacker = attacker ? attacker->GetNetId() : -1;
    I3D_collision_info info;
    I3D_frame *hit = GetGame()->GetScene()->IntersectFrame(pos, dir * 10.0f, m_frame, &info, 0, 0, 0, 0);
    if (hit) {
        // The damage part it met (from the last): its health, the clients told.
        for (size_t i = m_vehicle.m_parts.size(); i-- > 0;) {
            S_VehiclePart &part = m_vehicle.m_parts[i];
            if ((part.state & VEHICLE_PART_STATE_1) || part.frame != hit)
                continue;
            part.health = part.health - (amount - part.absorbed * 0.01f * amount) * 0.2f;
            m_vehicle.m_unknown58C |= 2;
            part.state |= VEHICLE_PART_STATE_2;
            if (part.health < 0.0f)
                part.health = 0.0f;
            if (part.type == VEHICLE_PART_GLASS) {
                C_CarGlassMessage msg(static_cast<int>(i), static_cast<int>(part.health), dir);
                SendMessage(msg);
                break;
            }
            // The push: a bumper or a wing up and away from the model's place, others along the
            // shot and up a little; times the amount (a bullet's a hundredth).
            S_vector push;
            if (part.type == VEHICLE_PART_BUMPER || part.type == VEHICLE_PART_WING) {
                const S_vector away = part.frame->GetWorldPos() - m_vehicle.m_modelMatrix(3);
                push = S_vector(away.x * 0.2f, 1.5f, away.z * 0.2f);
            } else {
                push = S_vector(dir.x, dir.y + 0.1f, dir.z);
            }
            const float scale = type == HIT_BULLET ? amount * 0.01f : amount;
            push = S_vector(scale * push.x, scale * push.y, scale * push.z);
            // The body's velocity at the part.
            const S_vector arm = part.frame->GetWorldPos() - XYZ(m_vehicle.m_unknown1B0);
            const S_vector &turn = m_vehicle.m_unknown210;
            const S_vector carried(arm.z * turn.y - arm.y * turn.z, arm.x * turn.z - arm.z * turn.x,
                                   arm.y * turn.x - arm.x * turn.y);
            C_CarPartMessage msg(static_cast<int>(i), static_cast<int>(part.health), XYZ(m_vehicle.m_velocity) - carried,
                                 push);
            SendMessage(msg);
            break;
        }
        // A wheel (or its holder) met: its tyre.
        for (int w = m_vehicle.m_wheelCount; w-- > 0;) {
            const S_VehicleWheel *wheel = m_vehicle.m_wheels[w];
            // Fix: a missing wheel is skipped (the original reads through it).
            if (!wheel || (wheel->flags & WHEEL_DETACHED))
                continue;
            if (wheel->frame != hit && wheel->holder != hit->GetParent() && wheel->frame != hit->GetParent())
                continue;
            if (IsTyreHit(w, pos, wheel->frame != hit) && !(wheel->flags & WHEEL_ON_RIM)) {
                C_CarTyreMessage msg(w);
                SendMessage(msg);
            }
        }
    }
    m_parts.Hit(type, amount, pos, dir, attacker);
    m_vehicle.m_unknown22C0 = 1;
    // (The original then calls an empty function, 0x00611080.)
}

// --- the wreck -------------------------------------------------------------------------------------------

void C_Car::Wreck(bool told)                            // 0x004dbf00
{
    if (m_wrecked)
        return;
    m_wrecked = true;
    C_CarMessage msg(CAR_MSG_DESTROYED);
    SendMessage(msg);
    if (!IsLocalGame() && !g_gameProfile.IsCooperativeGame() && g_mpSettings->gameType != MP_GAME_OBJECTIVES)
        m_respawnTimer.Start();
    // (The original then calls an empty function, 0x00611080.)

    ShutDown();
    if (S_FrameData *data = m_frame->GetData())
        if (data->script)
            data->script->OnDeath();

    // The crew dies.
    C_Actor *attacker = m_lastAttacker != -1 ? g_actorsModule.GetActorById(m_lastAttacker) : nullptr;
    bool hostile = false;
    for (int seat = GetSeatCount(); seat-- > 0;) {
        C_Actor *occupant = GetSeatOccupant(seat);
        if (!occupant)
            continue;
        const S_vector up(0.0f, 1.0f, 0.0f);
        if (told) {
            occupant->Hit(HIT_THROWN_OBJECT, -1.0f, GetPosition(), up, m_driver, 0);
            continue;
        }
        occupant->Hit(HIT_THROWN_OBJECT, -1.0f, GetPosition(), up, attacker, 0);
        if (attacker && static_cast<C_Human *>(occupant)->IsHostileTo(attacker))
            hostile = true;
    }
    if (m_chargeVisual) {
        m_chargeVisual->Release();
        m_chargeVisual = nullptr;
    }

    // Of the first six wheels, each on its own coin flip flies off.
    for (int i = 0; i < m_vehicle.m_wheelCount && i < 6; ++i) {
        S_VehicleWheel *wheel = m_vehicle.m_wheels[i];
        // Fix: a missing wheel is skipped (the original reads through it).
        if (!wheel || (wheel->flags & WHEEL_DETACHED) || !(std::rand() & 1) || m_vehicle.m_unknown550)
            continue;
        ThrowWheel(i);
    }
    if (hostile && attacker)
        static_cast<C_Human *>(attacker)->AddExperience(600);
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        m_wreckShown = true;
    BlastAround();
}

void C_Car::ShutDown()                                  // (inline in Wreck and ShowDestroyed)
{
    // The original's inlined light setters, each leaving alone a light its automatic switch drives
    // (those were just cleared) and calling the vehicle's callback 9 (none for the car).
    uint32_t &lights = m_vehicle.m_unknown5B0;
    const auto notify = [this] {
        if (auto callback = reinterpret_cast<T_VehicleCallback>(m_vehicle.m_callbacks[VEHICLE_CALLBACK_IMPULSE]))
            callback(m_vehicle.m_callbackContext);
    };
    lights &= ~(VEHICLE_LIGHT_AUTO_INDICATORS | VEHICLE_LIGHT_AUTO_BRAKE | VEHICLE_LIGHT_AUTO_REVERSE |
                VEHICLE_LIGHT_FLASHING);
    notify();
    if (!(lights & VEHICLE_LIGHT_FLASHING)) {
        lights &= ~VEHICLE_LIGHT_FLASH_LEFT;
        notify();
    }
    if (!(lights & VEHICLE_LIGHT_FLASHING)) {
        lights &= ~VEHICLE_LIGHT_FLASH_RIGHT;
        notify();
    }
    if (!(lights & VEHICLE_LIGHT_AUTO_INDICATORS))
        lights &= ~VEHICLE_LIGHT_INDICATOR_LEFT;
    if (!(lights & VEHICLE_LIGHT_AUTO_INDICATORS))
        lights &= ~VEHICLE_LIGHT_INDICATOR_RIGHT;
    lights &= ~VEHICLE_LIGHT_HEADLIGHTS;
    notify();
    if (!(lights & VEHICLE_LIGHT_AUTO_BRAKE)) {
        lights &= ~VEHICLE_LIGHT_BRAKE;
        notify();
    }
    if (!(lights & VEHICLE_LIGHT_AUTO_REVERSE)) {
        lights &= ~VEHICLE_LIGHT_REVERSE;
        notify();
    }

    m_vehicle.SetEngine(false, true);
    m_vehicle.SetFuel(0.0f);
    m_vehicle.m_unknown610 = 0.0f;
}

void C_Car::ThrowWheel(int index)                       // (inline in Wreck and ReadState)
{
    S_vector push = XYZ(m_vehicle.m_wheels[index]->worldPos) - XYZ(m_vehicle.m_unknown1B0);
    NormalizeBy(push, SquareZXY(push));
    const float speed = ((RandFloat() - 0.5f) + 1.0f) * 10.0f;
    push = S_vector(push.x * speed, std::fabs(push.y * speed), push.z * speed);
    S_vector velocity = push;
    NormalizeBy(velocity, SquareZXY(velocity));
    const float up = RandFloat() + 2.0f;
    velocity = S_vector(velocity.x * up, up, velocity.z * up);
    CarOnWheelOff(this, index, &velocity, &push);
    m_vehicle.m_unknown22C0 = 1;
    // (The original then calls an empty function, 0x00611080.)
}

void C_Car::BlastAround()                               // 0x004dc580
{
    S_vector centre = m_vehicle.m_modelMatrix(3);
    centre.y = centre.y + 0.5f;
    C_Actor *attacker = m_lastAttacker != -1 ? g_actorsModule.GetActorById(m_lastAttacker) : nullptr;
    for (size_t i = g_blastActors.size(); i-- > 0;) {
        C_Actor *actor = g_blastActors[i];
        if (!actor || actor->GetType() != ACTOR_HUMAN)
            continue;
        C_Human *human = static_cast<C_Human *>(actor);
        const S_vector toward = human->GetBodyFrame()->GetWorldPos() - centre;
        if (SquareXYZ(toward) > 25.0f || CollisionRayTest(centre, toward, nullptr, nullptr, 0xFFFFFFFFu, 0))
            continue;
        const float distance = LengthOf(SquareZYX(toward));
        human->Hit(HIT_EXPLOSION, (1.0f - distance * 0.2f) * 1000.0f, human->GetRandomBonePos(), toward, attacker, 0);
    }
}

// --- the charges -----------------------------------------------------------------------------------------

bool C_Car::PlaceCharge(C_Item *item, C_Human *human, const S_vector4 &pos, const S_vector4 &normal)   // 0x004dead0
{
    if (!item || !human || m_chargeTime != -1)
        return false;
    if (g_gameRole & GAME_ROLE_CLIENT)
        AttachCharge(item, human, pos, normal);
    m_chargeTime = 15000;
    m_chargeOwner = human->GetNetId();
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        human->RemoveItem(item->GetUid(), true);
    return true;
}

bool C_Car::AttachCharge(C_Item *item, C_Human *human, const S_vector4 &pos, const S_vector4 &normal)   // 0x004deb50
{
    m_chargePos = pos;
    m_chargeNormal = normal;
    I3D_frame *model = item->GetDef()->GetModel();
    m_chargeVisual = I3DGetDriver()->CreateVisual(model->GetVisualType());
    m_chargeVisual->Duplicate(model);
    human->RemoveItem(item->GetUid(), true);
    m_chargeVisual->LinkTo(m_frame, 0);
    m_chargeVisual->SetPosDir(XYZ(pos), XYZ(normal), 0.0f);
    return true;
}

void C_Car::UpdateCharge(int time)                      // 0x004dec10
{
    m_chargeTime -= time;
    if (m_chargeTime >= 1)
        return;
    m_chargeTime = -1;
    m_lastAttacker = m_chargeOwner;
    m_parts.wrecked = true;
    Wreck(false);
}

bool C_Car::RayTest(const S_vector4 &from, const S_vector &dir, S_vector4 *pos, S_vector4 *normal)   // 0x004dec60
{
    I3D_collision_info info;
    if (!GetGame()->GetScene()->IntersectFrame(XYZ(from), dir * 10.0f, m_frame, &info,
                                            I3DCOL_HIT_ZONE | I3DCOL_VISUAL_GEOMETRY, 0, 0, 0))
        return false;
    // (The original leaves both fourth floats as they were on its stack.)
    *pos = {info.distance * dir.x + from.x, info.distance * dir.y + from.y, info.distance * dir.z + from.z, 0.0f};
    *normal = {-info.normal.x, -info.normal.y, -info.normal.z, 0.0f};
    return true;
}

// --- the crew and the materials --------------------------------------------------------------------------

bool C_Car::IsDestroyed() const                         // 0x004ddcf0
{
    return m_vehicle.m_unknown610 == 0.0f;
}

bool C_Car::HasLocalPlayerAboard()                      // 0x004dea90
{
    for (int seat = GetSeatCount(); seat-- > 0;) {
        C_Actor *occupant = GetSeatOccupant(seat);
        if (occupant && static_cast<C_Human *>(occupant)->IsLocalPlayer())
            return true;
    }
    return false;
}

bool C_Car::HasEnemyAboard(const C_Human *human)        // 0x004dde20
{
    if (IsLocalGame()) {
        for (int seat = GetSeatCount(); seat-- > 0;) {
            C_Actor *occupant = GetSeatOccupant(seat);
            if (occupant && human->IsHostileTo(occupant) && static_cast<C_Human *>(occupant)->IsAlive())
                return true;
        }
        return false;
    }
    for (int seat = GetSeatCount(); seat-- > 0;) {
        const C_Human *occupant = static_cast<C_Human *>(GetSeatOccupant(seat));
        if (occupant && occupant->IsAlive() && occupant->IsInPlay() && !occupant->IsSpectating() &&
            occupant->GetTeam(false) != human->GetTeam(false))
            return true;
    }
    return false;
}

bool C_Car::CanEnter()                                  // 0x004ddd10
{
    if (m_wrecked || !IsDrivable(CAR_DRIVABLE_ON_WHEELS))
        return false;
    if (IsLocalGame()) {
        for (int seat = GetSeatCount(); seat-- > 0;) {
            const C_Human *occupant = static_cast<C_Human *>(GetSeatOccupant(seat));
            if (occupant && occupant->IsAlive() && !g_teamManager.GetPlayerTeam()->IsMember(occupant))
                return false;
        }
        return true;
    }
    const C_Human *local = g_netGame.GetLocalHuman();
    for (int seat = GetSeatCount(); seat-- > 0;) {
        const C_Human *occupant = static_cast<C_Human *>(GetSeatOccupant(seat));
        if (occupant && occupant->IsAlive() && occupant->IsInPlay() && !occupant->IsSpectating() &&
            occupant->GetTeam(false) != local->GetTeam(false))
            return false;
    }
    return true;
}

int C_Car::GetCrewSide()                                // 0x004ddee0
{
    for (int seat = GetSeatCount(); seat-- > 0;) {
        const C_Human *occupant = static_cast<C_Human *>(GetSeatOccupant(seat));
        if (!occupant || !occupant->IsAlive() || !occupant->IsInPlay() || occupant->IsSpectating())
            continue;
        switch (occupant->GetRole()) {
        case HUMAN_ROLE_ENEMY:
        case HUMAN_ROLE_NPC_TEAM_5:
            return 0;
        case HUMAN_ROLE_NPC_TEAM:
            return 1;
        case HUMAN_ROLE_PLAYER:
            return occupant->GetTeam(false);
        default:                                        // neutral, or no role
            continue;
        }
    }
    return 2;
}

int C_Car::GetHitMaterial(const S_vector &pos, const S_vector &dir)   // 0x004dbed0
{
    return m_parts.GetHitMaterial(pos, dir);
}

int C_Car::GetMaterialId(I3D_frame *part)               // 0x004dbee0
{
    const int id = GetFrameMaterialId(part);
    return id < 0 ? 11 : id;
}
