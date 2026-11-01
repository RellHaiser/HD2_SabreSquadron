// C_Car: the mission's start, stop and restart, and the model's meshes (original 0x004d16a0
// StartMission, 0x004d25c0 StopMission, 0x004dd0d0 RestartMission; 0x004dce40 RestoreModel,
// 0x004dcd10 ShowWreck, 0x004dd040 RefreshPartMeshes, 0x004dcfe0 / 0x004dcde0 their mesh copies).
#include "game/car.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "game/camera_module.h"
#include "game/car_internal.h"
#include "game/game.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"                               // g_netReplication, PeriodicLinks_*
#include "game/perf_timer.h"
#include "game/profile.h"                               // g_mpSettings
#include "game/resource_cache.h"                        // g_modelCache
#include "game/seat_weapon.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"

namespace {

S_vector XYZ(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

// The seat dummies' property texts (0x0083bdd8, 0x0083bdd0; the camera's: game/car_seats.cpp's).
const char *const SEAT_FLAGS_PREFIX = "SEAT";
const char *const SEAT_TEXT_KEY = "name";

// An EnumFrames callback (0x004dcfe0, and the same code at 0x004dcde0): `frame` takes the mesh of
// the visual of the same short name in the model `context` (any type found; 0 goes on).
uint32_t __stdcall CopyNamesakeMesh(I3D_frame *frame, void *context)
{
    I3D_frame *other = static_cast<I3D_frame *>(context)->FindChildFrame(frame->GetShortName(), FRAME_MASK_ANY);
    if (other) {
        static_cast<I3D_object_visual *>(frame)->SetMesh(static_cast<I3D_visual *>(other)->GetVisualMesh(), 1);
        frame->Update();
    }
    return 0;
}

}  // namespace

// --- the mission ----------------------------------------------------------------------------------

void C_Car::StartMission(bool server)                   // 0x004d16a0
{
    m_wreckShown = false;
    m_wrecked = false;
    // (Quirk kept: a charge's visual is dropped without a Release; the car's frame keeps it.)
    m_chargeVisual = nullptr;
    m_stateChanged = (g_gameRole & GAME_ROLE_SERVER) != 0;
    m_chargeOwner = -1;
    m_chargeTime = -1;
    m_deadlyReported = false;
    m_impulseSent = static_cast<int>(GetPerfTimeMs());
    m_vehicle.m_unknownF50 = 0.04f;
    m_vehiclesAllowed = g_mpSettings->allowVehicles != 0;
    m_parts.Reset();
    if (server) {
        m_lastAttacker = -1;
        if (IsLocalGame())
            m_periodicLink = PeriodicLinks_Add(this, 4);
    }
    Activate(server);
    m_restrictions = 0;
    m_startPosition = m_position;
    m_startDirection = m_direction;

    if (!IsLocalGame()) {
        m_netObject = g_netReplication->FindObject(GetNetId(), true);
        m_netObject->SetStateHandlers(&CarNetReadState, &CarNetWriteState, this);
        m_netObject->SetUnknown40(false);
        m_netObject->SetMessageFactory(&MakeSeatWeaponMessage, this);
        const float top = m_vehicle.m_topSpeed;
        m_quantizations[0].Setup({top + top, 20.0f, top + top, 0.0f}, {-top, -10.0f, -top, 0.0f},
                                 {top * 0.01f, 0.2f, top * 0.01f, 0.0f}, 100);
        m_quantizations[1].Setup({top * 0.1f, top * 0.05f, top * 0.1f, 0.0f},
                                 {top * -0.05f, top * -0.025f, top * -0.05f, 0.0f}, {0.01f, 0.013f, 0.01f, 0.0f},
                                 100);
        m_netObject->SetQuantizations0(nullptr, &m_quantizations[1], &m_quantizations[0]);
        m_netObject->SetQuantizations1(nullptr, &m_quantizations[1], &m_quantizations[0]);
        C_NetProperty *property = m_netObject->GetProperty(1);
        property->SetField(2, 0x3b);
        property->GetTrack().SetValue(m_position);
        property->GetDirTrack().SetValue(m_direction, 0.0f);
        if (IsNetServer()) {
            g_netReplication->SetUnknownC4(0);
            g_netReplication->SetUnknownC8(1);
        }
    }
    m_respawnTimer.period = 38000;
    if ((server && IsLocalGame()) || m_started)
        return;

    // The vehicle, once.
    m_started = true;
    m_driver = nullptr;
    m_vehicle.m_modelPos = m_position;
    m_vehicle.m_modelAxisZ = m_direction;
    m_vehicle.Start();
    if (IsLocalGame()) {
        m_vehicle.SetSpecialVolumes(true);
    } else {
        m_seatLeftTimes = new int[m_vehicle.m_seats.size()];
        m_seatLeftIds = new int[m_vehicle.m_seats.size()];
        // A client's car is solid only where the server allows vehicles.
        const bool solid = m_vehiclesAllowed || !IsNetClient();
        m_vehicle.SetSpecialVolumes(solid);
        m_vehicle.SetVolumesInWorld(solid, solid);
    }
    m_vehicle.SetFuel(1000.0f);

    // The lights on automatic: the original's inlined light setters, each calling the vehicle's
    // callback 9 (none for the car); the indicators are left (their switch was just set).
    uint32_t &lights = m_vehicle.m_unknown5B0;
    const auto notify = [this] {
        if (auto callback = reinterpret_cast<T_VehicleCallback>(m_vehicle.m_callbacks[VEHICLE_CALLBACK_IMPULSE]))
            callback(m_vehicle.m_callbackContext);
    };
    lights = (lights & ~VEHICLE_LIGHT_FLASHING) | VEHICLE_LIGHT_AUTO_INDICATORS | VEHICLE_LIGHT_AUTO_BRAKE |
             VEHICLE_LIGHT_AUTO_REVERSE;
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
    m_vehicle.SetBeamScale(0xFFFFFFFF, S_vector(12.0f, 12.0f, 40.0f));
    lights &= 0xF00FFFFF;                               // every lamp kind's beams off
    notify();
    m_vehicle.m_unknown534 = 0x98302;
    m_vehicle.m_unknown53C = 0xd8002;
    m_vehicle.m_model->Update();

    // The seats, from the last: the dummy's flags and text, the weapon, the camera's keeper.
    for (int seat = GetSeatCount(); seat-- > 0;) {
        S_VehicleSeat &s = m_vehicle.m_seats[seat];
        const char *props = s.frame->GetUserProps();
        if (std::strncmp(props, SEAT_FLAGS_PREFIX, 4) == 0) {
            for (const char *c = props + 4; *c != '\0' && *c != ',' && *c != ' '; ++c)
                s.flags |= 1u << ((static_cast<unsigned char>(*c) - '0') & 31);
        }
        const char *text = std::strstr(s.frame->GetUserProps(), SEAT_TEXT_KEY);
        s.text = text ? std::atoi(text + 4) : -1;
        if (s.flags & SEAT_FLAG_WEAPON) {
            auto *weapon = new C_SeatWeapon();              // _aligned_malloc(0x180, 16) in the original
            if (!weapon->Init(this, m_frame, s.frame, seat)) {
                delete weapon;
            } else {
                m_seatWeapons[seat] = weapon;
                if (!IsLocalGame()) {
                    // The store's rounds now: the server adds ammo below it (Tick).
                    C_ItemDef *def = weapon->GetItemDef();
                    weapon->SetRefillBelow(GetContainer(0)->CountItems(def->GetAmmoItemId()));
                    weapon->SetMessageSender(&SeatWeaponSendMessage, this);
                }
            }
        }
        ShowSeatView(seat, false);
        I3D_frame *camera = s.frame->FindChildFrame("camera3RD*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
        if (!camera || (s.flags & (SEAT_FLAG_ARMED | SEAT_FLAG_WEAPON)))
            s.camera = nullptr;
        else
            s.camera = new C_CameraFrameKeeper(this, camera);   // _aligned_malloc(0x50, 16) in the original
    }

    const S_vector &modelPos = m_vehicle.m_model->GetWorldPos();
    GetGame()->GetScene()->UpdateFrameSector(m_vehicle.m_model, S_vector(modelPos.x, modelPos.y + 1.0f, modelPos.z));
    m_marksTime = 0;
    MarkCheckpoints();

    // The wreck's model. Fix: the frame is released when it does not load (the original leaks it);
    // and the name is not limited to the original's 132-byte buffer.
    const std::string wreckName = std::string("C") + static_cast<I3D_model *>(m_frame)->GetFileName();
    m_wreckModel = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(m_wreckModel, wreckName.c_str()) != I3D_OK) {
        m_wreckModel->Release();
        m_wreckModel = nullptr;
    }

    m_vehicle.Settle(20, 0.0f, 0);
    if (!IsLocalGame()) {
        for (size_t seat = 0; seat < m_vehicle.m_seats.size(); ++seat) {
            m_seatLeftTimes[seat] = static_cast<int>(GetPerfTimeMs());
            m_seatLeftIds[seat] = -1;
        }
    }
}

void C_Car::StopMission(bool server)                    // 0x004d25c0
{
    if (m_started)
        RestoreModel();
    if (m_wreckModel)
        m_wreckModel->Release();
    m_wreckModel = nullptr;
    if (!IsLocalGame())
        g_netReplication->RemoveObject(m_netObject);
    for (int i = 10; i-- > 0;) {
        if (C_SeatWeapon *weapon = m_seatWeapons[i]) {
            weapon->Stop();
            delete weapon;                              // the destructor and _aligned_free in the original
        }
        m_seatWeapons[i] = nullptr;
    }
    for (int seat = 0; seat < static_cast<int>(m_vehicle.m_seats.size()); ++seat) {
        LeaveSeat(seat);
        if (C_CameraFrameKeeper *camera = m_vehicle.m_seats[seat].camera) {
            delete camera;                              // puts the dummy back
            m_vehicle.m_seats[seat].camera = nullptr;
        }
    }
    if ((!server && IsLocalGame()) || m_started) {
        m_started = false;
        m_vehicle.Stop();
        if (!IsLocalGame()) {
            delete[] m_seatLeftTimes;
            m_seatLeftTimes = nullptr;
            delete[] m_seatLeftIds;
            m_seatLeftIds = nullptr;
        }
    }
    Deactivate(server);
    ClearMarks();
    if (server && IsLocalGame()) {
        PeriodicLinks_Remove(m_periodicLink);
        m_periodicLink = -1;
    }
}

void C_Car::RestartMission(bool server)                 // 0x004dd0d0
{
    RestoreModel();
    if (server && IsLocalGame())
        m_periodicLink = PeriodicLinks_Add(this, 4);
    for (int seat = 0; seat < static_cast<int>(m_vehicle.m_seats.size()); ++seat) {
        LeaveSeat(seat);
        if (m_seatWeapons[seat])
            m_seatWeapons[seat]->Restart();
    }
    m_vehicle.Reset(2);
    m_position = m_startPosition;
    m_vehicle.m_modelPos = m_startPosition;
    m_direction = m_startDirection;
    m_vehicle.m_modelAxisZ = m_startDirection;
    m_frame->SetPosInline(XYZ(m_position));
    m_frame->SetWorldDir(XYZ(m_direction), 0.0f);
    m_frame->Update();
    m_vehiclesAllowed = g_mpSettings->allowVehicles != 0;
    // (Quirk kept: the vehicle's Restart below puts the volumes back in the world, so after a
    // restart a client's car is solid also where the server does not allow vehicles.)
    if (IsLocalGame()) {
        m_vehicle.SetSpecialVolumes(true);
    } else {
        const bool solid = m_vehiclesAllowed || !IsNetClient();
        m_vehicle.SetSpecialVolumes(solid);
        m_vehicle.SetVolumesInWorld(solid, solid);
    }
    m_vehicle.m_unknown600 = 1;
    m_vehicle.Restart(0.0f, false);
    m_vehicle.Settle(5, 0.0f, 0);
    m_vehicle.SetFuel(1000.0f);
    if (!IsLocalGame()) {
        C_NetProperty *property = m_netObject->GetProperty(1);
        property->GetTrack().SetValue(m_position);
        property->GetDirTrack().SetValue(m_direction, 0.0f);
        m_stateChanged = true;
    }
    m_lastAttacker = -1;
    m_driver = nullptr;
    m_deadlyReported = false;
    MarkCheckpoints();
    if (m_chargeVisual)
        m_chargeVisual->Release();
    m_chargeOwner = -1;
    m_chargeTime = -1;
    m_chargeVisual = nullptr;
}

// --- the model's meshes -----------------------------------------------------------------------------

void C_Car::RestoreModel()                              // 0x004dce40
{
    m_parts.Reset();
    m_wrecked = false;
    if (m_wreckShown) {
        I3D_model *model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
        if (!model)
            return;
        // The model's file without its directory. (Fix: not limited to the original's 128-byte
        // buffer.)
        const char *fileName = static_cast<I3D_model *>(m_frame)->GetFileName();
        if (const char *slash = std::strrchr(fileName, '\\'))
            fileName = slash + 1;
        const std::string name = fileName;
        if (g_modelCache.OpenInto(model, name.c_str()) != I3D_OK) {
            model->Release();
            return;
        }
        // The occupants unlinked while the meshes change (the frames of the model only).
        for (int seat = GetSeatCount(); seat-- > 0;) {
            if (C_Actor *occupant = m_vehicle.m_seats[seat].occupant)
                occupant->GetFrame()->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
        }
        m_frame->EnumFrames(&CopyNamesakeMesh, model, FRAME_MASK_VISUAL, nullptr);
        m_frame->Update();
        for (int seat = GetSeatCount(); seat-- > 0;) {
            if (C_Actor *occupant = m_vehicle.m_seats[seat].occupant) {
                occupant->GetFrame()->LinkTo(m_vehicle.m_seats[seat].frame, I3DLINK_KEEP_WORLD);
                occupant->GetFrame()->Update();
            }
        }
        m_wreckShown = false;
        RefreshPartMeshes();
        model->Release();
    }
    m_vehicle.Restart(0.0f, true);
}

void C_Car::ShowWreck()                                 // 0x004dcd10
{
    // Fix: nothing without the wreck's model (the original enumerates its children through null).
    if (m_wreckShown || !m_wreckModel)
        return;
    for (int seat = GetSeatCount(); seat-- > 0;) {
        if (C_Actor *occupant = m_vehicle.m_seats[seat].occupant)
            occupant->GetFrame()->LinkTo(nullptr, I3DLINK_KEEP_WORLD);
    }
    m_frame->EnumFrames(&CopyNamesakeMesh, m_wreckModel, FRAME_MASK_VISUAL, nullptr);
    m_frame->Update();
    for (int seat = GetSeatCount(); seat-- > 0;) {
        if (C_Actor *occupant = m_vehicle.m_seats[seat].occupant) {
            occupant->GetFrame()->LinkTo(m_vehicle.m_seats[seat].frame, I3DLINK_KEEP_WORLD);
            occupant->GetFrame()->Update();
        }
    }
    RefreshPartMeshes();
    m_wreckShown = true;
}

void C_Car::RefreshPartMeshes()                         // 0x004dd040
{
    for (size_t i = m_vehicle.m_parts.size(); i-- > 0;) {
        S_VehiclePart &part = m_vehicle.m_parts[i];
        part.mesh = static_cast<I3D_visual *>(part.frame)->GetVisualMesh();
    }
}
