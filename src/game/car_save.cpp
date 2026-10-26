// C_Car: the saved game (original 0x004dd350 SaveGame, 0x004dd750 LoadGame).
#include "game/car.h"

#include <cstdint>
#include <vector>

#include "common/chunk.h"
#include "game/item_defs.h"
#include "game/items_module.h"                          // g_itemsModule
#include "game/seat_weapon.h"
#include "ls3d/i3d.h"

namespace {

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }

// Saved-game chunks of a car.
enum : uint16_t {
    CHUNK_CAR_VEHICLE = 10000,          // the vehicle's state block, its size first
    CHUNK_CAR_SEAT_WEAPON = 0x2711,     // a seat's index, its weapon's
    CHUNK_CAR_WRECK = 0x2713,           // m_wreckShown, m_wrecked (0x2712: none)
    CHUNK_CAR_LINKS = 0x2714,           // the periodic link, the last attacker
    CHUNK_CAR_ACTOR = 0x2715,           // C_Actor::SaveGame()
    CHUNK_CAR_PARTS = 0x2716,           // the damage parts' (S_CarParts)
    CHUNK_CAR_CHARGE = 0x2717,          // the placed charge: time, owner, place, normal
};

// The explosive charge's item type (items.sav: "w_charge"; the original reads the items module's
// table entry, 0x0098b138, itself).
constexpr int CHARGE_ITEM_TYPE = 66;

// The car's seat weapons (C_Car::m_seatWeapons).
constexpr int MAX_SEAT_WEAPONS = 10;

}  // namespace

bool C_Car::SaveGame(C_chunk &chunk)                    // 0x004dd350
{
    chunk.Begin(CHUNK_CAR_ACTOR);
    C_Actor::SaveGame(chunk);
    chunk.Ascend();

    const uint32_t size = m_vehicle.StateSize();
    std::vector<uint8_t> state(size);
    m_vehicle.SaveState(state.data());
    chunk.Begin(CHUNK_CAR_VEHICLE);
    chunk.Write(size);
    chunk.Write(state.data(), static_cast<int>(size));
    chunk.Ascend();

    for (int seat = GetSeatCount(); seat-- > 0;) {
        // Fix: a seat whose weapon could not be set up is passed over (the original saves through
        // null).
        if (!(m_vehicle.m_seats[seat].flags & SEAT_FLAG_WEAPON) || !m_seatWeapons[seat])
            continue;
        chunk.Begin(CHUNK_CAR_SEAT_WEAPON);
        chunk.Write(seat);
        m_seatWeapons[seat]->SaveGame(chunk);
        chunk.Ascend();
    }

    chunk.Begin(CHUNK_CAR_WRECK);
    chunk.Write(m_wreckShown);
    chunk.Write(m_wrecked);
    chunk.Ascend();
    chunk.Begin(CHUNK_CAR_LINKS);
    chunk.Write(m_periodicLink);
    chunk.Write(m_lastAttacker);
    chunk.Ascend();
    chunk.Begin(CHUNK_CAR_PARTS);
    m_parts.SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_CAR_CHARGE);
    chunk.Write(m_chargeTime);
    chunk.Write(m_chargeOwner);
    chunk.Write(&m_chargePos, sizeof(S_vector));
    chunk.Write(&m_chargeNormal, sizeof(S_vector));
    chunk.Ascend();
    return false;                                       // (Quirk kept: the original's)
}

bool C_Car::LoadGame(C_chunk &chunk)                    // 0x004dd750
{
    // (The wreck's flag goes to ShowWreck below; the seat's index is kept from the chunk before
    // when one cannot be read.)
    uint8_t wreckShown = 0;
    int seat = -1;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_CAR_ACTOR:
            C_Actor::LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_CAR_VEHICLE: {
            uint32_t size = 0;
            chunk.Read(size);
            // Fix: the block is zeroed first and an empty one not loaded (the original's allocation
            // keeps whatever a short read leaves, and LoadState reads through a 0-byte one).
            std::vector<uint8_t> state(size);
            chunk.Read(state.data(), static_cast<int>(size));
            if (!state.empty())
                m_vehicle.LoadState(state.data());
            chunk.Ascend();
            break;
        }
        case CHUNK_CAR_SEAT_WEAPON:
            chunk.Read(seat);
            // Fix: a seat past the weapons or without one is passed over (the original loads
            // through whatever the index meets).
            if (seat >= 0 && seat < MAX_SEAT_WEAPONS && m_seatWeapons[seat])
                m_seatWeapons[seat]->LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_CAR_WRECK:
            chunk.Read(wreckShown);
            chunk.Read(m_wrecked);
            chunk.Ascend();
            break;
        case CHUNK_CAR_LINKS:
            chunk.Read(m_periodicLink);
            chunk.Read(m_lastAttacker);
            chunk.Ascend();
            break;
        case CHUNK_CAR_PARTS:
            m_parts.LoadGame(chunk);
            chunk.Ascend();
            break;
        case CHUNK_CAR_CHARGE:
            chunk.Read(m_chargeTime);
            chunk.Read(m_chargeOwner);
            chunk.Read(&m_chargePos, sizeof(S_vector));
            chunk.Read(&m_chargeNormal, sizeof(S_vector));
            chunk.Ascend();
            break;
        default:
            // (The original does not leave unknown chunks: their contents are read next.)
            break;
        }
    }

    // A placed charge: its visual made again (as AttachCharge, from the charge item's model).
    if (m_chargeTime != -1) {
        I3D_frame *model = g_itemsModule.GetItemDef(CHARGE_ITEM_TYPE)->GetModel();
        m_chargeVisual = I3DGetDriver()->CreateVisual(model->GetVisualType());
        m_chargeVisual->Duplicate(model);
        m_chargeVisual->LinkTo(m_frame, 0);
        m_chargeVisual->SetPosDir(XYZ(m_chargePos), XYZ(m_chargeNormal), 0.0f);
    }
    // The wreck: its meshes, every glass part not yet gone shattered out of sight.
    if (wreckShown) {
        ShowWreck();
        for (size_t i = m_vehicle.m_parts.size(); i-- > 0;) {
            S_VehiclePart &part = m_vehicle.m_parts[i];
            if ((part.state & VEHICLE_PART_STATE_1) || part.type != VEHICLE_PART_GLASS)
                continue;
            part.health = 0.0f;
            part.state |= VEHICLE_PART_STATE_1;
            part.frame->SetOn(false);
        }
    }
    return true;
}
