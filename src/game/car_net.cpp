// C_Car: the network side (original 0x004db360 OnMessage, 0x004db760 Unknown20, 0x004db310
// SendFuel, 0x004d2ec0 ReadMessages, 0x004d3380 UpdateNetwork, 0x004de150 WriteStore, 0x004de1f0
// ReadStore, 0x004ddf90 / 0x004de270 a joining client's WriteState / ReadState, 0x004d0610 /
// 0x004d08c0 the net object's state handlers, 0x004dc780 ShowDestroyed; the messages
// 0x004e0040..0x004e06c0).
#include "game/car.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/actors_module.h"                         // g_actorsModule
#include "game/car_internal.h"
#include "game/human.h"
#include "game/human_movement.h"                        // C_HumanLighting
#include "game/items.h"
#include "game/items_module.h"                          // g_itemsModule
#include "game/pending.h"                               // g_netReplication, g_localPlayerSlot
#include "game/seat_weapon.h"
#include "game/team.h"                                  // g_teamManager
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// Squares in the orders the original sums them.
float SquareXYZ(const S_vector &v) { return (v.x * v.x + v.y * v.y) + v.z * v.z; }
float SquareZXY(const S_vector &v) { return (v.z * v.z + v.x * v.x) + v.y * v.y; }

// S_vector::Normalize from a square summed in another order.
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

// The bits of a value below `n` as the original works them out (FYL2X in the game's float
// precision): ln n / ln 2 + 1, truncated.
int CountBits(float n)
{
    return static_cast<int>(std::log(n) / std::log(2.0f) + 1.0f);
}

// The low `bits` bits set (0x00816388).
uint32_t BitMask(int bits)
{
    return bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1u;
}

// The fuel's range in its message.
constexpr float FUEL_MESSAGE_MAX = 5000.0f;

// The fuel as its message and the join state send it: its share of 5000 l in the bits of 50000.
void WriteFuel(C_BitStream *stream, float fuel)
{
    const int bits = CountBits(50000.0f);
    stream->WriteBits(static_cast<uint32_t>(static_cast<int>(fuel * 0.0002f * static_cast<float>(BitMask(bits)))), bits);
}

// Read back clamped to 0..5000.
float ReadFuel(C_BitStream *stream)
{
    const int bits = CountBits(50000.0f);
    const uint32_t raw = stream->ReadBits(bits);
    const float value = static_cast<float>(raw) / static_cast<float>(BitMask(bits)) * FUEL_MESSAGE_MAX;
    return value < 0.0f ? 0.0f : value > FUEL_MESSAGE_MAX ? FUEL_MESSAGE_MAX : value;
}

// A float sent as its 32 bits.
uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float BitsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

}  // namespace

// --- the messages ------------------------------------------------------------------------------------

void C_CarPartEffectMessage::Read(C_BitStream *stream)  // 0x004e06a0
{
    part = static_cast<int>(stream->ReadBits(4));
}

void C_CarPartEffectMessage::Write(C_BitStream *stream) // 0x004e06c0
{
    C_CarMessage::Write(stream);
    stream->WriteBits(static_cast<uint32_t>(part), 4);
}

void C_CarFuelMessage::Read(C_BitStream *stream)        // 0x004e0040
{
    fuel = ReadFuel(stream);
}

void C_CarFuelMessage::Write(C_BitStream *stream)       // 0x004e00f0
{
    C_CarMessage::Write(stream);
    WriteFuel(stream, fuel);
}

void C_Car::SendFuel()                                  // 0x004db310
{
    C_CarFuelMessage msg(m_vehicle.m_fuel);
    if (g_gameRole & GAME_ROLE_SERVER)
        SendMessage(msg);
    else
        SendMessageEx(msg);
}

// --- the messages from the other machines -----------------------------------------------------------

// Fix (the messages' indices): a part or wheel past the car's, or a missing wheel, is ignored (the
// original indexes them by the sender's number).
S_VehiclePart *C_Car::PartAt(int index)
{
    return index >= 0 && index < static_cast<int>(m_vehicle.m_parts.size()) ? &m_vehicle.m_parts[index] : nullptr;
}

S_VehicleWheel *C_Car::WheelAt(int index)
{
    return index >= 0 && index < m_vehicle.m_wheelCount ? m_vehicle.m_wheels[index] : nullptr;
}

void C_Car::TakeWheelMessage(const C_CarWheelMessage &m)   // (inline in OnMessage and Unknown20)
{
    S_VehicleWheel *wheel = WheelAt(m.wheel);
    if (!wheel || !(wheel->health > 0.0f))
        return;
    m_vehicle.m_unknown58C |= 2;
    wheel->flags |= WHEEL_HIT;
    wheel->health = static_cast<float>(m.health);
    if (wheel->health <= 0.0f && !m_vehicle.m_unknown550) {
        const S_vector velocity = XYZ(m.velocity);
        CarOnWheelOff(this, m.wheel, &velocity, nullptr);
    }
}

void C_Car::OnMessage(C_BitStream *msg)                 // 0x004db360
{
    switch (msg->ReadBits(CAR_MSG_KIND_BITS)) {
    case CAR_MSG_GLASS: {
        C_CarGlassMessage m;
        m.Read(msg);
        if (S_VehiclePart *part = PartAt(m.part)) {
            m_vehicle.m_unknown58C |= 2;
            part->state |= VEHICLE_PART_STATE_2;
            part->health = static_cast<float>(m.health);
            m_vehicle.CrackGlass(static_cast<uint32_t>(m.part), XYZ(m.dir));
        }
        break;
    }
    case CAR_MSG_PART: {
        C_CarPartMessage m;
        m.Read(msg);
        if (S_VehiclePart *part = PartAt(m.part)) {
            m_vehicle.m_unknown58C |= 2;
            part->state |= VEHICLE_PART_STATE_2;
            part->health = static_cast<float>(m.health);
            if (part->health <= 0.0f) {
                const S_vector velocity = XYZ(m.velocity), push = XYZ(m.push);
                CarOnPartOff(this, m.part, &velocity, &push);
            }
        }
        break;
    }
    case CAR_MSG_WHEEL: {
        C_CarWheelMessage m;
        m.Read(msg);
        if (!(g_gameRole & GAME_ROLE_SERVER))
            TakeWheelMessage(m);
        break;
    }
    case CAR_MSG_TYRE: {
        C_CarTyreMessage m;
        m.Read(msg);
        if (S_VehicleWheel *wheel = WheelAt(m.wheel)) {
            wheel->flags |= WHEEL_ON_RIM;
            m_vehicle.PlayWheelBurst(m.wheel);
        }
        break;
    }
    case CAR_MSG_DESTROYED: {
        C_CarMessage m(CAR_MSG_DESTROYED);
        m.Read(msg);
        m_parts.StartWreckEffects();
        ShowDestroyed();
        break;
    }
    case CAR_MSG_PART_EFFECT: {
        C_CarPartEffectMessage m;
        m.Read(msg);
        m_parts.StartPartEffect(m.part);
        break;
    }
    case CAR_MSG_RESTART:
        Restart();
        break;
    case CAR_MSG_STORE:
        ReadStore(msg, !(g_gameRole & GAME_ROLE_SERVER));
        break;
    case CAR_MSG_FUEL: {
        C_CarFuelMessage m;
        m.Read(msg);
        m_vehicle.SetFuel(m.fuel);
        break;
    }
    case CAR_MSG_IMPULSE: {
        C_CarImpulseMessage m;
        m.Read(msg);
        TakePush(m.actor, m.point);
        break;
    }
    default:                                            // (CAR_MSG_DEADLY_GROUND, CAR_MSG_HIT: the server's)
        break;
    }
    // (The original then calls an empty function, 0x00611080.)
}

void C_Car::Unknown20(C_BitStream *msg)                 // 0x004db760
{
    switch (msg->ReadBits(CAR_MSG_KIND_BITS)) {
    case CAR_MSG_WHEEL: {
        C_CarWheelMessage m;
        m.Read(msg);
        TakeWheelMessage(m);
        break;
    }
    case CAR_MSG_DEADLY_GROUND:
        m_parts.wrecked = true;
        Wreck(true);
        break;
    case CAR_MSG_HIT: {
        C_CarHitMessage m;
        m.Read(msg);
        const S_vector point = XYZ(m.point), impulse = XYZ(m.impulse);
        if (m.human == -1) {
            // The crew (from the last), by the driver.
            for (int seat = static_cast<int>(m_vehicle.m_seats.size()); seat-- > 0;) {
                C_Actor *occupant = GetSeatOccupant(seat);
                if (occupant && static_cast<C_Human *>(occupant)->IsAlive())
                    occupant->Hit(HIT_THROWN_OBJECT, static_cast<float>(m.amount), point, impulse, GetSeatOccupant(0), 0);
            }
        } else if (C_Actor *human = g_actorsModule.GetActorById(m.human)) {
            human->Hit(HIT_THROWN_OBJECT, static_cast<float>(m.amount), point, impulse, GetSeatOccupant(0), 0);
        }
        break;
    }
    case CAR_MSG_FUEL: {
        C_CarFuelMessage m;
        m.Read(msg);
        m_vehicle.SetFuel(m.fuel);
        SendMessage(m);                                 // on to the clients
        break;
    }
    case CAR_MSG_IMPULSE: {
        C_CarImpulseMessage m;
        m.Read(msg);
        TakePush(m.actor, m.point);
        break;
    }
    default:
        break;
    }
}

// --- the seat weapons' messages -------------------------------------------------------------------------

void C_Car::ReadMessages()                              // 0x004d2ec0
{
    // A client's player sitting at `seat` (its own shots and reloads come back to it).
    const auto ownSeat = [this](int seat) {
        return (g_gameRole & GAME_ROLE_CLIENT) &&
               static_cast<C_Actor *>(g_teamManager.GetPlayerTeam()->GetSelectedMember()) == GetSeatOccupant(seat);
    };
    const auto livingGunner = [this](int seat) {
        C_Actor *gunner = GetSeatOccupant(seat);
        return gunner && static_cast<C_Human *>(gunner)->IsAlive();
    };
    while (C_NetObjectMessage *msg = m_netObject->PopMessage(0)) {
        switch (msg->GetType()) {
        case SEAT_WEAPON_MSG_SHOT: {
            const auto *shot = static_cast<C_SeatWeaponShotMessage *>(msg);
            C_SeatWeapon *weapon = GetSeatWeapon(shot->seat);
            if (ownSeat(shot->seat) || !livingGunner(shot->seat) || !weapon)
                break;
            // (The weapon reads neither the barrel PlayShot is given nor FireShot's 1000.)
            if (!(g_gameRole & GAME_ROLE_SERVER)) {
                weapon->PlayShot(shot->barrel);
            } else if (weapon->FireShot(&shot->pos, &shot->dir, shot->barrel, 1000)) {
                msg->fromClient = false;
                m_netObject->QueueMessage(msg, g_netReplication->GetLocalId(), true);
            }
            break;
        }
        case SEAT_WEAPON_MSG_RELOAD: {
            const auto *reload = static_cast<C_SeatWeaponReloadMessage *>(msg);
            C_SeatWeapon *weapon = GetSeatWeapon(reload->seat);
            if (ownSeat(reload->seat) || !livingGunner(reload->seat) || !weapon)
                break;
            // (The store is not read by the weapon either: it reloads from container 0.)
            if (!(g_gameRole & GAME_ROLE_SERVER)) {
                weapon->ShowReload(GetContainer(0));
            } else {
                weapon->Reload(GetContainer(0));
                msg->fromClient = false;
                m_netObject->QueueMessage(msg, g_netReplication->GetLocalId(), true);
            }
            break;
        }
        case SEAT_WEAPON_MSG_ROUNDS: {
            const auto *rounds = static_cast<C_SeatWeaponRoundsMessage *>(msg);
            if (C_SeatWeapon *weapon = GetSeatWeapon(rounds->seat))
                weapon->SetRounds(rounds->rounds);
            break;
        }
        case SEAT_WEAPON_MSG_AMMO: {
            const auto *ammo = static_cast<C_SeatWeaponAmmoMessage *>(msg);
            C_Item *item = g_itemsModule.CreateItem(ammo->typeId, 0, nullptr, ammo->uid);
            C_Item *replaced = nullptr;
            GetContainer(0)->AddItem(item, &replaced, -1);
            break;
        }
        default:
            break;
        }
        // (Queued again it is not deleted: the queue holds it.)
        if (msg->unknown24 < 1)
            delete msg;
    }
}

// --- the item store ------------------------------------------------------------------------------------

void C_Car::WriteStore(C_BitStream *msg)                // 0x004de150
{
    C_ItemContainer *store = GetContainer(0);
    for (int i = 0; i < store->GetUsedSlotCount(); ++i) {
        for (const C_Item *item : store->GetSlots()[i]) {
            msg->WriteBits(static_cast<uint32_t>(item->GetUid()), 16);
            msg->WriteBits(static_cast<uint32_t>(item->GetTypeId()), 8);
        }
    }
    msg->WriteBits(0xFFFF, 16);
}

void C_Car::ReadStore(C_BitStream *msg, bool add)       // 0x004de1f0
{
    for (uint32_t uid = msg->ReadBits(16); uid != 0xFFFF; uid = msg->ReadBits(16)) {
        const int typeId = static_cast<int>(msg->ReadBits(8));
        if (!add)
            continue;
        C_Item *item = g_itemsModule.CreateItem(typeId, 0, nullptr, static_cast<int>(uid));
        C_Item *replaced = nullptr;
        GetContainer(0)->AddItem(item, &replaced, -1);
    }
}

// --- a joining client's state ----------------------------------------------------------------------------

void C_Car::WriteState(C_BitStream *msg)                // 0x004ddf90
{
    const S_vector &pos = m_frame->GetPos();
    msg->WriteBits(FloatBits(pos.x), 32);
    msg->WriteBits(FloatBits(pos.y), 32);
    msg->WriteBits(FloatBits(pos.z), 32);
    // (LS3D's Inverse writes four floats: the axis, then the rotation's s.)
    S_vector4 axis = {};
    float angle;
    m_frame->GetRot().Inverse(reinterpret_cast<S_vector &>(axis), angle);
    msg->WriteBits(FloatBits(axis.x), 32);
    msg->WriteBits(FloatBits(axis.y), 32);
    msg->WriteBits(FloatBits(axis.z), 32);
    msg->WriteBits(FloatBits(angle), 32);
    WriteStore(msg);
    msg->WriteBits(FloatBits(m_vehicle.m_unknown610), 32);
    msg->WriteBits(m_wreckShown ? 1 : 0, 1);
    for (int i = m_vehicle.m_wheelCount; i-- > 0;) {
        // Fix: a missing wheel is sent as on (the original reads through it).
        const S_VehicleWheel *wheel = m_vehicle.m_wheels[i];
        msg->WriteBits(wheel && (wheel->flags & WHEEL_DETACHED) ? 1 : 0, 1);
    }
    WriteFuel(msg, m_vehicle.m_fuel);
}

void C_Car::ReadState(C_BitStream *msg)                 // 0x004de270
{
    // The place, and the rotation's axis as the direction. (The original also copies a w component
    // from uninitialised stack memory into both.)
    m_position.x = BitsFloat(msg->ReadBits(32));
    m_position.y = BitsFloat(msg->ReadBits(32));
    m_position.z = BitsFloat(msg->ReadBits(32));
    m_direction.x = BitsFloat(msg->ReadBits(32));
    m_direction.y = BitsFloat(msg->ReadBits(32));
    m_direction.z = BitsFloat(msg->ReadBits(32));
    const float angle = BitsFloat(msg->ReadBits(32));
    m_frame->SetPosInline(XYZ(m_position));
    S_quat rot;
    rot.Make(XYZ(m_direction), angle);
    m_frame->SetRot(rot);
    m_frame->Update();
    C_NetProperty *property = m_netObject->GetProperty(1);
    property->GetTrack().SetValue(m_position);
    property->GetDirTrack().SetValue(m_direction, angle);
    ReadStore(msg, true);
    m_vehicle.m_unknown610 = BitsFloat(msg->ReadBits(32));
    if (msg->ReadBits(1)) {
        ShowWreck();
        m_vehicle.SetEngine(false, true);
    }
    for (int i = m_vehicle.m_wheelCount; i-- > 0;) {
        // Fix: a missing wheel is passed over (the original reads through it).
        if (msg->ReadBits(1) && m_vehicle.m_wheels[i])
            ThrowWheel(i);
    }
    m_vehicle.SetFuel(ReadFuel(msg));
}

// --- the replication ------------------------------------------------------------------------------------

void C_Car::UpdateNetwork(int time)                     // 0x004d3380
{
    m_netObject->SetUnknown3C(7);
    const bool server = (g_gameRole & GAME_ROLE_SERVER) != 0;
    if ((m_driver && m_driver->GetNetId() == g_localPlayerSlot) ||
        (server && (!m_driver || m_driver->GetRole() != HUMAN_ROLE_PLAYER))) {
        // Driven here: the place, the rotation and the velocity to the others.
        m_netObject->SetSendMask(7);
        C_NetProperty *property = m_netObject->GetProperty(1);
        property->GetTrack().SetValue(m_position);
        // (LS3D's Inverse writes four floats, the rotation's s after the axis: sent so.)
        S_vector4 axis = {};
        float angle;
        m_frame->GetRot().Inverse(reinterpret_cast<S_vector &>(axis), angle);
        property->GetDirTrack().SetValue(axis, angle);
        property->GetTrack().SetRate(XYZ(m_vehicle.m_velocity));
        m_netObject->Tick(static_cast<float>(time) * 0.001f);
        if (m_vehiclesAllowed) {
            m_vehicle.m_unknown600 &= ~VEHICLE_PHYSICS_KEEP_AWAKE;
            m_vehicle.Step(static_cast<uint32_t>(time));
        } else {
            // Vehicles are not allowed: the car waits 8 m under its start place.
            m_position = m_startPosition;
            m_position.y = m_position.y - 8.0f;
            m_vehicle.m_modelPos = m_position;
            m_frame->SetPosInline(XYZ(m_position));
            m_frame->SetWorldDir(XYZ(m_direction), 0.0f);
            m_frame->Update();
        }
        if (!server)
            return;
    }
    if (server && !(m_driver && m_driver->GetNetId() != g_localPlayerSlot && m_driver->GetRole() == HUMAN_ROLE_PLAYER))
        return;

    // Driven elsewhere: the replicated place, rotation and velocity.
    m_netObject->SetSendMask(server ? 7 : 2);
    m_netObject->SetUnknown3C(7);
    C_NetProperty *property = m_netObject->GetProperty(1);
    property->SetFlags(property->GetFlags() & ~0x20000u);
    const float ms = static_cast<float>(time);
    const float seconds = ms * 0.001f;
    m_netObject->Update(seconds);
    const S_vector4 &target = property->GetDirTrack().GetTarget();
    if (!(SquareXYZ(XYZ(target)) < 1e-16f)) {
        m_position = property->GetTrack().GetCurrent();
        m_direction = property->GetDirTrack().GetValue();
        m_frame->SetPosInline(XYZ(m_position));
        S_quat rot;
        rot.Make(XYZ(m_direction), property->GetDirTrack().GetAngle());
        m_frame->SetRot(rot);
        m_vehicle.m_velocity = property->GetTrack().GetRate();
        // The speed (S_vector::Magnitude; the small test on the square's bits).
        const float square = SquareXYZ(XYZ(m_vehicle.m_velocity));
        int32_t bits;
        std::memcpy(&bits, &square, sizeof bits);
        m_vehicle.m_speed = bits < 0x322bcc77 ? 0.0f : std::fabs(square - 1.0f) < 1e-8f ? 1.0f : std::sqrt(square);
        if (auto callback = reinterpret_cast<T_VehicleCallback>(m_vehicle.m_callbacks[VEHICLE_CALLBACK_IMPULSE]))
            callback(m_vehicle.m_callbackContext);
        m_frame->Update();
    }
    m_vehicle.m_unknown600 = VEHICLE_PHYSICS_AWAKE;
    m_vehicle.ReadModelState();
    const S_vector4 down = {-m_vehicle.m_modelAxisY.x, -m_vehicle.m_modelAxisY.y, -m_vehicle.m_modelAxisY.z, 0.0f};
    S_vector4 heading = {};
    // (Quirk kept: the frame's ms, where the vehicle's own solver passes seconds.)
    m_vehicle.UpdateWheelContacts(&heading, ms, &down, false);
    // The rear axle's wheels (the middle two of the list): their centres' velocities.
    const float inverse = 1.0f / seconds;
    for (int i = m_vehicle.m_wheelCount / 2; i < m_vehicle.m_wheelCount / 2 + 2; ++i) {
        // Fix: a wheel past the list or missing is passed over (the original reads through it).
        S_VehicleWheel *wheel = WheelAt(i);
        if (!wheel)
            continue;
        wheel->unknown0A0 = S_vector((wheel->worldPos.x - wheel->unknown060.x) * inverse,
                                     (wheel->worldPos.y - wheel->unknown060.y) * inverse,
                                     (wheel->worldPos.z - wheel->unknown060.z) * inverse);
        // (The original writes the stack's garbage into the fourth float, +0xAC.)
        wheel->unknown060 = wheel->worldPos;
    }
}

// --- the net object's state handlers ------------------------------------------------------------------

void __fastcall CarNetReadState(void *context, C_BitStream *stream)   // 0x004d0610
{
    auto *car = static_cast<C_Car *>(context);
    C_Vehicle &vehicle = *car->GetVehicle();
    if (!stream->ReadBits(1))
        return;
    const bool apply = car->m_netObject->GetUnknown3C() != 0;
    // (Quirk kept: the flags cleared also when nothing is applied.)
    for (int i = 0; i < vehicle.m_wheelCount; ++i) {
        if (S_VehicleWheel *wheel = vehicle.m_wheels[i])   // Fix: a missing wheel is passed over
            wheel->flags &= ~(WHEEL_SLIDING | WHEEL_LOCKED);
    }
    const int bits = CountBits(static_cast<float>(vehicle.m_wheelCount));
    for (const uint32_t flag : {static_cast<uint32_t>(WHEEL_SLIDING), static_cast<uint32_t>(WHEEL_LOCKED)}) {
        for (uint32_t i = stream->ReadBits(bits); i != static_cast<uint32_t>(vehicle.m_wheelCount); i = stream->ReadBits(bits)) {
            S_VehicleWheel *wheel = i < static_cast<uint32_t>(vehicle.m_wheelCount) ? vehicle.m_wheels[i] : nullptr;
            if (apply && wheel)
                wheel->flags |= flag;
        }
    }
    const int gear = static_cast<int>(stream->ReadBits(4)) - 1;
    if (apply)
        vehicle.m_gear = gear;
    const int steeringBits = CountBits(33.333334f);
    const float raw = static_cast<float>(stream->ReadBits(steeringBits)) / static_cast<float>(BitMask(steeringBits));
    float steering = raw + raw - 1.0f;
    steering = steering < -1.0f ? -1.0f : steering > 1.0f ? 1.0f : steering;
    if (apply)
        vehicle.SetSteering(steering);
    const float limit = vehicle.m_revLimit;
    const int revBits = CountBits(limit / (limit * 0.004f));
    float revs = static_cast<float>(stream->ReadBits(revBits)) / static_cast<float>(BitMask(revBits)) * limit;
    revs = revs < 0.0f ? 0.0f : revs > limit ? limit : revs;
    if (apply)
        vehicle.m_engineRevs = revs;
}

void __fastcall CarNetWriteState(void *context, C_BitStream *stream)  // 0x004d08c0
{
    auto *car = static_cast<C_Car *>(context);
    C_Vehicle &vehicle = *car->GetVehicle();
    static S_PerfTimer s_timer(300);                    // 0x0089bd24 (Quirk kept: one for every car)
    s_timer.running = true;
    if (!s_timer.Fire()) {
        stream->WriteBits(0, 1);
        return;
    }
    stream->WriteBits(1, 1);
    for (const uint32_t flag : {static_cast<uint32_t>(WHEEL_SLIDING), static_cast<uint32_t>(WHEEL_LOCKED)}) {
        for (int i = 0; i < vehicle.m_wheelCount; ++i) {
            const S_VehicleWheel *wheel = vehicle.m_wheels[i];
            if (wheel && (wheel->flags & flag))         // Fix: a missing wheel is passed over
                stream->WriteBits(static_cast<uint32_t>(i), CountBits(static_cast<float>(vehicle.m_wheelCount)));
        }
        stream->WriteBits(static_cast<uint32_t>(vehicle.m_wheelCount), CountBits(static_cast<float>(vehicle.m_wheelCount)));
    }
    stream->WriteBits(static_cast<uint32_t>(vehicle.m_gear + 1), 4);
    float steering = vehicle.m_unknown94C;
    steering = steering < -1.0f ? -1.0f : steering > 1.0f ? 1.0f : steering;
    const int steeringBits = CountBits(33.333334f);
    stream->WriteBits(static_cast<uint32_t>(static_cast<int>((steering + 1.0f) * 0.5f *
                                                             static_cast<float>(BitMask(steeringBits)))),
                      steeringBits);
    if (vehicle.m_engineRevs > vehicle.m_revLimit)
        vehicle.m_engineRevs = vehicle.m_revLimit;
    const float limit = vehicle.m_revLimit;
    const int revBits = CountBits(limit / (limit * 0.004f));
    stream->WriteBits(static_cast<uint32_t>(static_cast<int>(vehicle.m_engineRevs / limit *
                                                             static_cast<float>(BitMask(revBits)))),
                      revBits);
}

// --- a client's wreck -----------------------------------------------------------------------------------

void C_Car::ShowDestroyed()                             // 0x004dc780
{
    // (The original first calls an empty function, 0x00611080.)
    // The vehicle whole again at its speed, but the first six wheels that were off.
    bool off[6] = {};
    for (int i = 0; i < m_vehicle.m_wheelCount && i < 6; ++i)
        off[i] = m_vehicle.m_wheels[i] && (m_vehicle.m_wheels[i]->flags & WHEEL_DETACHED);   // Fix: a missing wheel
    m_vehicle.Restart(m_vehicle.m_speed, true);
    for (int i = 0; i < m_vehicle.m_wheelCount && i < 6; ++i) {
        if (!off[i])
            continue;
        S_VehicleWheel *wheel = m_vehicle.m_wheels[i];
        wheel->flags |= WHEEL_DETACHED;
        wheel->frame->SetOn(false);
        wheel->frame->Update();
    }
    ShutDown();
    if (IsSimulatedHere()) {
        const S_vector hop(0.0f, m_vehicle.m_currentMass * 3.0f, 0.0f);
        m_vehicle.ApplyImpulse(hop, m_vehicle.m_modelMatrix(3), 0, S_vector(1.0f, 1.0f, 1.0f));
    }
    ShowWreck();

    // The parts left are thrown off.
    for (size_t i = m_vehicle.m_parts.size(); i-- > 0;) {
        S_VehiclePart &part = m_vehicle.m_parts[i];
        if (part.state & VEHICLE_PART_STATE_1)
            continue;
        S_vector dir = part.frame->GetWorldPos() - m_vehicle.m_modelMatrix(3);
        NormalizeBy(dir, SquareZXY(dir));
        const float speed = (RandFloat() - 0.5f) * 10.0f + 20.0f;
        dir = S_vector(dir.x * speed, std::fabs(dir.y * speed), dir.z * speed);
        if (dir.y < 5.0f)
            dir.y = 5.0f;
        if (part.type == VEHICLE_PART_GLASS) {
            part.health = 0.0f;
            m_vehicle.CrackGlass(static_cast<uint32_t>(i), S_vector(dir.x * 0.1f, dir.y * 0.1f, dir.z * 0.1f));
        } else if (std::rand() & 1) {
            part.health = 0.0f;
            const S_vector velocity(0.0f, 0.0f, 0.0f);
            CarOnPartOff(this, static_cast<int>(i), &velocity, &dir);
        }
    }
    if (m_chargeVisual) {
        m_chargeVisual->Release();
        m_chargeVisual = nullptr;
    }
    // The crew darkened.
    for (int seat = 0; seat < GetSeatCount(); ++seat) {
        C_Actor *occupant = GetSeatOccupant(seat);
        if (!occupant)
            continue;
        if (C_HumanMovement *movement = static_cast<C_Human *>(occupant)->GetMovement()) {
            C_HumanLighting &lighting = movement->m_visual.m_lighting;
            lighting.m_level = 0.2f;
            lighting.m_shownLevel = 0.3f;
            lighting.Update(occupant->GetFrame(), 500, 0);
        }
    }
}
