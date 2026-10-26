// C_Car: the creator, the lifetime, the frame and the mission file's properties (original 0x004d04d0
// / 0x004d04f0..0x004d0570 the creator, 0x004d0b60 C_Car, 0x004d0f20 ~C_Car, 0x004d10a0 SetFrame,
// 0x004d1220 CopyFrom, 0x004d1280 WriteProperties, 0x004d55c0 ReadProperties, 0x004d0e30
// GetSeatCount; 0x004d49f0 / 0x004d4a00 two empty vehicle callbacks; the damage parts:
// game/car_parts.cpp; the performance timer: game/perf_timer.cpp).
#include "game/car.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "common/chunk.h"
#include "game/car_effects.h"                           // SetupCarEmitter, ReuseCarEmitter
#include "game/pending.h"                               // g_texts
#include "game/vehicle_params.h"

std::vector<C_Car *> g_cars;                            // 0x0089bd34

// --- the creator ---------------------------------------------------------------------------------

C_Actor *C_CarCreator::Create()                         // 0x004d04f0
{
    return new C_Car();                                 // _aligned_malloc(0x25c0, 16) in the original
}

void C_CarCreator::Destroy(C_Actor *actor)              // 0x004d0570
{
    delete actor;
}

static C_CarCreator g_carCreator;                       // 0x0089bd44

// Registers the creator (static initialiser 0x004d04d0, through 0x004d04c0).
static const bool g_carCreatorRegistered = [] {
    g_actorCreators[ACTOR_CAR] = &g_carCreator;
    return true;
}();

// --- the car's lifetime ----------------------------------------------------------------------------

C_Car::C_Car() : C_Actor(ACTOR_CAR)                     // 0x004d0b60 (the timer made now: S_PerfTimer)
{
    m_vehicle.m_callbackContext = this;
    m_name = g_texts.Get(0x1bf0);                       // the localized actor name (text 0x1bf0)
    g_cars.push_back(this);
}

C_Car::~C_Car()                                         // 0x004d0f20
{
    m_parts.Release();
    // Out of the list: the last car takes its place (looked for from the end).
    for (size_t i = g_cars.size(); i-- > 0;) {
        if (g_cars[i] == this) {
            g_cars[i] = g_cars.back();
            g_cars.pop_back();
            break;
        }
    }
}

int C_Car::GetSeatCount()                               // 0x004d0e30
{
    return static_cast<int>(m_vehicle.m_seats.size());
}

// --- the frame -------------------------------------------------------------------------------------

bool C_Car::SetFrame(I3D_frame *frame)                  // 0x004d10a0
{
    m_started = false;
    std::fill(std::begin(m_seatWeapons), std::end(m_seatWeapons), nullptr);
    if (!C_Actor::SetFrame(frame) || !m_vehicle.Setup(frame))
        return false;
    m_vehicle.SetCallback(VEHICLE_CALLBACK_ACTOR_RESPONSE, reinterpret_cast<void *>(&CarOnBodyActorResponse));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_ACTOR_IMPULSE, reinterpret_cast<void *>(&CarOnBodyActorImpulse));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_ACTOR_TOUCH, reinterpret_cast<void *>(&CarOnBodyActorTouch));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_ACTOR_FIRM, reinterpret_cast<void *>(&CarIsActorFirm));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_IMPACT, reinterpret_cast<void *>(&CarOnVehicleImpact));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_PART_OFF, reinterpret_cast<void *>(&CarOnPartOff));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_WHEEL_OFF, reinterpret_cast<void *>(&CarOnWheelOff));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_GLASS_BROKEN, reinterpret_cast<void *>(&CarOnGlassBroken));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_IMPULSE, nullptr);
    m_vehicle.SetCallback(VEHICLE_CALLBACK_BODY_GROUND, reinterpret_cast<void *>(&CarOnBodyGround));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_WHEEL_GROUND, reinterpret_cast<void *>(&CarOnWheelGround));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_PART_HIT, reinterpret_cast<void *>(&CarOnPartHit));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_REPAIRED, reinterpret_cast<void *>(&CarOnRepaired));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_SETUP_EMITTER, reinterpret_cast<void *>(&SetupCarEmitter));
    m_vehicle.SetCallback(VEHICLE_CALLBACK_REUSE_EMITTER, reinterpret_cast<void *>(&ReuseCarEmitter));
    m_parts.Setup(this);
    return true;
}

void __fastcall CarOnPartHit(void *, I3D_frame *, int, const S_vector4 *, const S_vector *, bool)   // 0x004d49f0
{
}

void __fastcall CarOnRepaired(void *)                   // 0x004d4a00
{
}

// --- the mission file's properties -------------------------------------------------------------------

namespace {
constexpr int CAR_PROPERTIES_VERSION = 3;
}  // namespace

bool C_Car::WriteProperties(C_chunk &chunk)             // 0x004d1280
{
    // Fix: the blob is freed (the original leaks it, also when the packing fails).
    std::vector<uint8_t> blob(VEHICLE_PARAMS_BLOB_SIZE, 0);
    WriteVehicleParams(blob.data(), m_vehicle.m_params, false);
    std::vector<uint8_t> packed;
    if (!PackVehicleBlob(blob.data(), VEHICLE_PARAMS_BLOB_SIZE, packed))
        return false;
    const int version = CAR_PROPERTIES_VERSION;
    const uint32_t size = static_cast<uint32_t>(packed.size());
    chunk.Write(version);
    chunk.Write(size);
    chunk.Write(packed.data(), static_cast<int>(size));
    return true;
}

bool C_Car::ReadProperties(C_chunk &chunk)              // 0x004d55c0
{
    if (!chunk.IsOpen() || chunk.IsWriting())
        return false;
    int version = 0;
    chunk.Read(version);
    if (version != CAR_PROPERTIES_VERSION)
        return false;
    uint32_t size = 0;
    chunk.Read(size);
    std::vector<uint8_t> packed(size);
    chunk.Read(packed.data(), static_cast<int>(size));
    std::vector<uint8_t> blob;
    if (!UnpackVehicleBlob(packed.data(), size, blob))
        return false;
    // Fix: a blob shorter than the block (the original reads the block's 0x124B bytes past it).
    if (blob.size() < VEHICLE_PARAMS_BLOB_SIZE)
        blob.resize(VEHICLE_PARAMS_BLOB_SIZE, 0);
    // (Zeroed: the reader leaves the names' tails as they are, the original's are stack garbage.)
    S_VehicleParams params{};
    ReadVehicleParams(blob.data(), params, false);
    // (The result is not checked: a block ApplyParams stops in runs partly applied.)
    m_vehicle.ApplyParams(params, 0);
    return true;
}

void C_Car::CopyFrom(const C_Actor *source)             // 0x004d1220
{
    // Quirk kept: the frame is taken through C_Actor's, before the type is looked at.
    C_Actor::SetFrame(source->GetFrame());
    if (source->GetType() != ACTOR_CAR)
        return;
    C_Actor::CopyFrom(source);
    S_VehicleParams params;
    static_cast<const C_Car *>(source)->m_vehicle.GetParams(&params);
    m_vehicle.ApplyParams(params, 0);
}
