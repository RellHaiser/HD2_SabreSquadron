// C_Car: the vehicle's callbacks (original 0x004d4130..0x004d4eb0, SetFrame hands them to the
// vehicle; 0x004d5140 their debris) and the messages they send (0x004e0180 / 0x004e0270
// CAR_MSG_HIT, 0x004e0540 / 0x004e05b0 CAR_MSG_WHEEL).
#include "game/car.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/actors_module.h"                         // g_temporaryActors
#include "game/car_internal.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"                               // GetGEffects
#include "game/physics.h"
#include "game/shots.h"
#include "game/versions.h"
#include "game/world.h"                                 // C_Debris, C_Mine
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// Dot products and squares in the orders the original sums them.
float DotZYX(const S_vector &a, const S_vector &b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }
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

// The actors' OnCollision(Ex) take pointers and floats as ints.
int PointerArg(const void *p) { return static_cast<int>(reinterpret_cast<intptr_t>(p)); }
int FloatArg(float value) { return static_cast<int>(FloatBits(value)); }

// The water the car sinks into: neither the body nor the wheels meet it.
bool IsWater(int material)
{
    return material == MATERIAL_COLD_WATER || material == MATERIAL_WADING_WATER || material == MATERIAL_SWIMMING_WATER;
}

// The original 0x004d5140 (the car, the frame, the parameters, their kind, the index; the car
// and the index unused): a copy of a visual thrown off where it is, facing its z axis, then set up
// as a part's debris (kind 1: S_DebrisParams, fading out) or a wheel's (kind 0); true (other kinds:
// false; none are passed). A frame that is not a visual throws nothing (true as well). Here the
// copy, null for no visual; Fix: the callers skip a null copy (a visual without a type; the
// original sets it up unchecked).
C_Debris *ThrowOff(I3D_frame *frame)
{
    if (frame->GetType() != FRAME_VISUAL)
        return nullptr;
    return g_temporaryActors.CreateDebris(frame, frame->GetWorldPos(), frame->GetWorldDir());
}

}  // namespace

// --- the messages ------------------------------------------------------------------------------------

void C_CarHitMessage::Write(C_BitStream *stream)       // 0x004e0270
{
    C_CarMessage::Write(stream);
    stream->WriteBits(static_cast<uint32_t>(human), 16);
    stream->WriteBits(static_cast<uint32_t>(amount), 16);
    stream->WriteBits(FloatBits(impulse.x), 32);
    stream->WriteBits(FloatBits(impulse.y), 32);
    stream->WriteBits(FloatBits(impulse.z), 32);
    stream->WriteBits(FloatBits(point.x), 32);
    stream->WriteBits(FloatBits(point.y), 32);
    stream->WriteBits(FloatBits(point.z), 32);
}

void C_CarHitMessage::Read(C_BitStream *stream)        // 0x004e0180
{
    // (The original then turns 0xFFFF into -1, which the sign extension already gave.)
    human = static_cast<int16_t>(stream->ReadBits(16));
    amount = static_cast<int16_t>(stream->ReadBits(16));
    impulse.x = BitsFloat(stream->ReadBits(32));
    impulse.y = BitsFloat(stream->ReadBits(32));
    impulse.z = BitsFloat(stream->ReadBits(32));
    point.x = BitsFloat(stream->ReadBits(32));
    point.y = BitsFloat(stream->ReadBits(32));
    point.z = BitsFloat(stream->ReadBits(32));
}

void C_CarWheelMessage::Write(C_BitStream *stream)     // 0x004e05b0
{
    C_CarMessage::Write(stream);
    stream->WriteBits(static_cast<uint32_t>(wheel), 8);
    stream->WriteBits(static_cast<uint32_t>(health), 10);
    if (health <= 0)
        static_cast<C_NetMessage *>(stream)->WritePosition(XYZ(velocity));
}

void C_CarWheelMessage::Read(C_BitStream *stream)      // 0x004e0540
{
    wheel = static_cast<int>(stream->ReadBits(8));
    health = static_cast<int>(stream->ReadBits(10));
    if (health > 0)
        return;
    S_vector v;
    static_cast<C_NetMessage *>(stream)->ReadPosition(v);
    velocity.x = v.x;
    velocity.y = v.y;
    velocity.z = v.z;
}

// --- the body against actors -----------------------------------------------------------------------

bool __fastcall CarOnBodyActorResponse(void * /*vehicle*/, S_CollisionVolume *hit, const S_vector4 *point,
                                       const S_vector *normal, S_vector4 *arm, S_vector4 *velocity, float *mass,
                                       S_vector4 *turn, float *scale, bool *push)   // 0x004d4130
{
    // (The vehicle asks only for a moving actor's volume.)
    C_Actor *actor = hit->actor;
    switch (actor->GetType()) {
    case ACTOR_CAR:
        static_cast<C_Car *>(actor)->GetVehicle()->GetContactResponse(*point, *normal, arm, velocity, mass, turn);
        *scale = -1.2f;
        *push = true;
        return true;
    case ACTOR_HUMAN:
        // (The original leaves the arm's and the velocity's fourth float and the turn's alone.)
        *arm = {};
        *velocity = {};
        *mass = 0.0f;
        turn->x = 0.0f;
        turn->y = 0.0f;
        turn->z = 0.0f;
        *scale = -1.0f;
        *push = false;
        return true;
    default:
        *push = true;
        return false;
    }
}

void __fastcall CarOnBodyActorImpulse(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                      const S_vector4 *impulse, const S_vector *normal, float mass)   // 0x004d4230
{
    C_Car *car = static_cast<C_Car *>(context);
    if (!(hit->flags & COLLISION_FLAG_DYNAMIC))
        return;
    C_Actor *other = GetFrameActor(hit->frame);
    if (!other)
        return;
    const S_vector *pos = reinterpret_cast<const S_vector *>(point);
    switch (other->GetType()) {
    case ACTOR_CAR:
        other->OnCollision(car, pos, PointerArg(impulse), PointerArg(normal), FloatArg(mass), 0);
        return;
    default:
        other->OnCollisionEx(car, pos, PointerArg(&car->GetVelocity()), PointerArg(normal), FloatArg(mass), 0,
                             PointerArg(car->GetDriver()));
        return;
    case ACTOR_HUMAN:
        break;
    }
    // A human: the one of the volume is hurt (the frame's gets the contact).
    C_Actor *human = hit->actor;
    // Network games: none within 3 s of anyone leaving a seat. (Quirk kept: whoever it was.)
    if (!IsLocalGame()) {
        for (int seat = 0; seat < car->GetEntryCount(); ++seat) {
            if (car->m_seatLeftIds[seat] != -1 &&
                static_cast<int>(GetPerfTimeMs()) < car->m_seatLeftTimes[seat] + 3000)
                return;
        }
    }
    other->OnCollisionEx(car, pos, PointerArg(&car->GetVelocity()), PointerArg(normal), FloatArg(mass), 0,
                         PointerArg(car->GetDriver()));
    // Run over at 4 m/s or more: 1000 damage, the driver the culprit (a client tells the server).
    if (car->GetSpeed() < 4.0f || !static_cast<C_Human *>(human)->IsAlive())
        return;
    if (g_gameRole & GAME_ROLE_SERVER) {
        human->Hit(HIT_THROWN_OBJECT, 1000.0f, XYZ(*point), XYZ(*impulse), car->GetSeatOccupant(0), 0);
        return;
    }
    C_CarHitMessage msg(human->GetNetId(), 1000, XYZ(*impulse), XYZ(*point));
    car->SendMessageEx(msg);
}

void __fastcall CarOnBodyActorTouch(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                    const S_vector4 *velocity, const S_vector *normal)   // 0x004d4450
{
    C_Car *car = static_cast<C_Car *>(context);
    // (The vehicle calls it only for a moving actor's volume.)
    C_Actor *human = hit->actor;
    if (!human || human->GetType() != ACTOR_HUMAN)
        return;
    S_vector away(-velocity->x, -velocity->y, -velocity->z);
    NormalizeBy(away, SquareZYX(away));
    if (car->GetSpeed() < 2.0f &&
        !(std::fabs(DotZYX(XYZ(car->m_vehicle.m_modelAxisZ), *normal)) > 0.7))
        return;
    human->OnCollision(car, reinterpret_cast<const S_vector *>(point), PointerArg(velocity), PointerArg(&away),
                       FloatArg(car->m_vehicle.m_currentMass), PointerArg(hit->frame));
}

bool __fastcall CarIsActorFirm(void *context, S_CollisionVolume *hit, bool * /*deform*/)   // 0x004d4620
{
    const C_Car *car = static_cast<const C_Car *>(context);
    if (!(hit->flags & COLLISION_FLAG_DYNAMIC))
        return true;
    const C_Actor *actor = hit->actor;
    if (!actor)
        return false;
    switch (static_cast<int>(actor->GetType())) {
    case ACTOR_HUMAN:
        return car->GetSpeed() < 4.0f && car->GetPosition4().y + 0.3f > actor->GetPosition4().y;
    case ACTOR_MELEE_TRACKER:
        return car->GetSpeed() < 4.0f;
    case 0:
    case 1:
    case ACTOR_MINE:
    case ACTOR_CAR:
    case ACTOR_SPAWN_POINT:
    case ACTOR_ITEM:
    case ACTOR_DEBRIS:
    case ACTOR_EXPLOSIVE_PLACE:
    case ACTOR_FOWL_GENERATOR:
    case ACTOR_FISH:
    case ACTOR_NULL:
    case ACTOR_MP_ZONE:
        return false;
    default:
        return true;
    }
}

// --- the impacts -----------------------------------------------------------------------------------

void __fastcall CarOnVehicleImpact(void *context, const S_vector *impulse, const S_vector * /*largest*/,
                                   const S_vector4 *point, float /*radius*/, const S_vector * /*velocity*/,
                                   C_Actor *actor, const S_CollisionVolume *volume, int /*unknown*/)   // 0x004d4740
{
    C_Car *car = static_cast<C_Car *>(context);
    // Network games: the local driver's client, else the server unless a player drives.
    if (!IsLocalGame()) {
        C_Human *driver = static_cast<C_Human *>(car->GetSeatOccupant(0));
        if (!((g_gameRole & GAME_ROLE_CLIENT) && driver && driver->IsLocalPlayer())) {
            if (!(g_gameRole & GAME_ROLE_SERVER))
                return;
            if (driver && driver->GetRole() == HUMAN_ROLE_PLAYER)
                return;
        }
    }
    const float perMass = LengthOf(SquareZXY(*impulse)) / car->m_vehicle.m_currentMass;
    float damage = perMass * (actor ? 3.0f : 5.0f);
    // The world against the roof (the impulse over 2.7 rad off the model's y axis): 15 times, or
    // 1000 with the vehicle's +0x2289 clear.
    if (impulse->AngleTo(XYZ(car->m_vehicle.m_modelAxisY)) > 2.7f && !actor) {
        if (!car->m_vehicle.m_unknown2289)
            damage = 1000.0f;
        else
            damage *= 15.0f;
    }
    if (!(damage > 20.0f))
        return;
    GetGame()->Print("Damage driver - Damage :%.2f, impuls %.2f ", damage, perMass);
    // Not when the body met a human taken as the world.
    if (volume && (volume->flags & COLLISION_FLAG_DYNAMIC) && volume->actor &&
        volume->actor->GetType() == ACTOR_HUMAN)
        return;
    if (!(g_gameRole & GAME_ROLE_SERVER)) {
        C_CarHitMessage msg(-1, static_cast<int>(damage), *impulse, XYZ(*point));
        car->SendMessageEx(msg);
        return;
    }
    for (int seat = car->GetEntryCount(); seat-- > 0;) {
        C_Actor *occupant = car->GetSeatOccupant(seat);
        if (occupant && static_cast<C_Human *>(occupant)->IsAlive())
            occupant->Hit(HIT_THROWN_OBJECT, damage, XYZ(*point), *impulse, car->GetSeatOccupant(0), 0);
    }
}

// --- the damage parts --------------------------------------------------------------------------------

bool __fastcall CarOnPartOff(void *context, int index, const S_vector *velocity, const S_vector *push)   // 0x004d4ca0
{
    C_Car *car = static_cast<C_Car *>(context);
    C_Vehicle &vehicle = car->m_vehicle;
    S_VehiclePart &part = vehicle.m_parts[index];
    // The weight (kg); glass, the body, the roof and the spare wheel stay.
    S_DebrisParams params;
    switch (part.type) {
    case VEHICLE_PART_BUMPER:
        params.weight = 10.0f;
        break;
    case VEHICLE_PART_MIRROR:
    case VEHICLE_PART_PLATE:
        params.weight = 2.0f;
        break;
    case VEHICLE_PART_LIGHT:
        params.weight = 4.0f;
        break;
    case VEHICLE_PART_WING:
        params.weight = 7.0f;
        break;
    case VEHICLE_PART_DOOR:
        params.weight = 20.0f;
        break;
    default:
        return false;
    }
    params.friction = 0.5f;
    // The body's velocity there and the push by the weight.
    params.SetVelocity(*velocity);
    if (push) {
        const float inverse = 1.0f / params.weight;
        params.velocity.x = inverse * push->x + params.velocity.x;
        params.velocity.y = params.velocity.y + inverse * push->y;
        params.velocity.z = params.velocity.z + inverse * push->z;
    }
    if (C_Debris *debris = ThrowOff(part.frame))
        debris->Setup(params, true);
    // The last lamp on a part under this one breaks (one at most).
    for (size_t i = vehicle.m_lamps.size(); i-- > 0;) {
        S_VehicleLamp &lamp = vehicle.m_lamps[i];
        if ((lamp.flags & VEHICLE_LAMP_BROKEN) || lamp.part == 0xFFFF)
            continue;
        const S_VehiclePart &on = vehicle.m_parts[lamp.part];
        if ((on.state & VEHICLE_PART_STATE_1) || !on.frame || on.frame->GetParent() != part.frame)
            continue;
        lamp.health = 0.0f;
        lamp.flags |= VEHICLE_LAMP_BROKEN;
        break;
    }
    part.state |= VEHICLE_PART_STATE_1;
    part.frame->SetOn(false);
    part.frame->Update();
    return true;
}

bool __fastcall CarOnWheelOff(void *context, int index, const S_vector *velocity, const S_vector *push)   // 0x004d4eb0
{
    C_Car *car = static_cast<C_Car *>(context);
    S_VehicleWheel *wheel = car->m_vehicle.m_wheels[index];
    const auto hide = [wheel] {
        wheel->flags |= WHEEL_DETACHED;
        wheel->frame->SetOn(false);
        wheel->frame->Update();
    };
    // A server without the view: hidden, the clients told.
    if (!(g_gameRole & GAME_ROLE_CLIENT)) {
        hide();
        C_CarWheelMessage msg(index, static_cast<int>(wheel->health), *velocity);
        car->SendMessage(msg);
        return true;
    }
    // Thrown off with a tenth of the push.
    S_WheelDebrisParams params;
    params.radius = wheel->radius;
    params.width = wheel->width;
    params.weight = 10.0f;
    params.SetVelocity(*velocity);
    if (push) {
        params.velocity.x = push->x * 0.1f + params.velocity.x;
        params.velocity.y = params.velocity.y + push->y * 0.1f;
        params.velocity.z = params.velocity.z + push->z * 0.1f;
    }
    params.unknown1C = wheel->unknown1B0;
    params.unknown20 = wheel->unknown1C0;
    if (C_Debris *debris = ThrowOff(wheel->frame))
        debris->SetupWheel(params);
    hide();
    C_CarWheelMessage msg(index, static_cast<int>(wheel->health), *velocity);
    if (g_gameRole & GAME_ROLE_SERVER) {
        car->SendMessage(msg);
        return true;
    }
    // A client: the local driver's tells the server.
    C_Actor *driver = car->GetSeatOccupant(0);
    if (driver && driver->GetNetId() == g_localPlayerSlot)
        car->SendMessageEx(msg);
    return true;
}

void __fastcall CarOnGlassBroken(void *context, int part, const S_vector * /*velocity*/, const char * /*sound*/)   // 0x004d4a10
{
    C_Car *car = static_cast<C_Car *>(context);
    const S_VehiclePart &glass = car->m_vehicle.m_parts[part];
    // (The original reads the frame's flags before it tests the frame.)
    if (!glass.frame || !glass.frame->IsOn() || !glass.mesh)
        return;
    GetGEffects()->ShatterGlass(glass.frame, 0.01f);
    glass.frame->SetOn(false);
}

// --- the ground --------------------------------------------------------------------------------------

S_CollisionVolume *__fastcall CarOnBodyGround(void *context, S_CollisionVolume *hit, S_vector * /*hitPos*/,
                                              S_vector * /*normal*/)   // 0x004d4a70
{
    C_Car *car = static_cast<C_Car *>(context);
    if (IsWater(GetVolumeMaterial(*hit)))
        return nullptr;
    if (!(hit->flags & COLLISION_FLAG_DYNAMIC))
        return hit;
    const S_FrameData *data = hit->frame->GetData();
    const T_PhysicsObject object = data ? data->physicsObject : g_noPhysicsObject;
    if (object != g_noPhysicsObject) {
        // Hit along the car's velocity (kind 2 in the vehicle's mode +0x550, not identified).
        S_vector zero(0.0f, 0.0f, 0.0f);
        GetPhysicsModule()->Hit(object, car->GetUnknown5E0() ? 2 : PHYS_HIT_KICK, &car->GetVelocity(), &zero);
        return nullptr;
    }
    if (data && data->versionMaster) {
        data->versionMaster->DamageAll(1000, VERSION_EFFECTS_ALL);
        return nullptr;
    }
    if (hit->type & COLLISION_TYPE_SWINGING)
        return nullptr;
    return hit;
}

S_CollisionVolume *__fastcall CarOnWheelGround(void *context, S_CollisionVolume *ground, int wheel)   // 0x004d4b50
{
    C_Car *car = static_cast<C_Car *>(context);
    const int material = GetVolumeMaterial(*ground);
    if (IsWater(material))
        return nullptr;
    if (!(g_gameRole & GAME_ROLE_SERVER) || car->IsDestroyed())
        return ground;
    if ((ground->flags & COLLISION_FLAG_DYNAMIC) && (ground->type & COLLISION_TYPE_MINE)) {
        static_cast<C_Mine *>(ground->actor)->OnSteppedOn(car, wheel);
        return nullptr;
    }
    // Single player: a minefield blows up at the wheel's contact, the driver the culprit.
    if (IsLocalGame() && (material == MATERIAL_MINEFIELD || material == MATERIAL_MINEFIELD_2 || material == MATERIAL_62)) {
        const S_vector pos = XYZ(car->m_vehicle.m_wheels[wheel]->unknown050);
        g_shotManager.Explode(car->GetDriver(), pos, 3.0f, 5000.0f, 3);
        static const int EFFECTS[3] = {0x2B, 0x30, 0x31};
        const int effect = EFFECTS[std::rand() % 3];
        g_shotManager.PlayExplosion(pos, S_vector(0.0f, 1.0f, 0.0f), effect, material, 3);
    }
    return ground;
}
