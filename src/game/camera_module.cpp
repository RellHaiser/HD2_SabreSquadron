// The camera module (0x00462320..0x0046cb20): see game/camera_module.h. This file: the module's
// core (part 8e-i: 0x00464d90..0x004668c0) and the human effects' constructor and destructor.
#include "game/camera_module.h"

#include <cmath>
#include <cstddef>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/car.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/submarine.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

C_CameraModule g_cameraModule(MODULE_CAMERA, "CameraModule");   // 0x0089afb0 (static init 0x00464dc0)
float g_vehicleCameraSpeed;                             // 0x0089b208

C_CameraHumanEffects *&CameraHumanEffectsRef()
{
    return g_cameraModule.m_humanEffects;
}
C_CameraHumanEffects *&g_cameraHumanEffects = CameraHumanEffectsRef();

namespace {

const char *const SETTINGS_FILE = "tables\\gamecam.set";   // 0x00839254

// S_vector::Normalize with the squared length the caller summed (the original's inline copies
// sum it in their own orders).
void NormalizeSummed(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f) {
        const float k = 1.0f / std::sqrt(square);
        v.x *= k;
        v.y *= k;
        v.z *= k;
    } else if (v.x != 0.0f) {
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    } else {
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
    }
}

// A frame's world position and its normalized view direction (the matrix rows' w kept).
S_vector4 WorldPos(I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}
S_vector4 WorldDir(I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    const S_vector dir = m(2).NormalizedGame();
    return {dir.x, dir.y, dir.z, m.m[2][3]};
}

} // namespace

// The module's static initialiser 0x00464dc0 (0x00464dd0 the constructor, 0x00464df0 / 0x00464e00
// the atexit of the destructor); 0x00464d90 is the guarded atexit of a header's static.

C_CameraModule *GetCameraModule()
{
    return &g_cameraModule;
}

// (The intermediate class's constructor 0x00464ea0 and destructors 0x00464ec0 / 0x00464ee0 are
// C_Module's here.)
C_CameraModule::C_CameraModule(int id, const char *name) : C_Module(id, name)
{
    static_assert(sizeof(C_CameraModule) == 0x2C0, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_humanEffects) == 0x00C, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_unknown0D0) == 0x0D0, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_settings) == 0x0E0, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_gameCamera) == 0x240, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_underwater) == 0x270, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_unknown274) == 0x274, "C_CameraModule layout");
    static_assert(offsetof(C_CameraModule, m_scriptedView) == 0x2B4, "C_CameraModule layout");
    static_assert(sizeof(S_CameraSettings) == 0x20, "S_CameraSettings layout");
    m_humanEffects = new C_CameraHumanEffects;
}

// (Its deleting destructor 0x004650d0.)
C_CameraModule::~C_CameraModule()
{
    delete m_humanEffects;
}

void C_CameraModule::RestartMission(bool)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT))
        return;
    StopMission(false);
    StartMission(false);
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (!player || !player->GetVehicle() || player->GetVehicle()->GetType() != ACTOR_JUNKERS88)
        Unknown40(0);
}

bool C_CameraModule::SaveGame(C_chunk &)
{
    return true;
}

bool C_CameraModule::LoadGame(C_chunk &)
{
    Unknown40(0);
    Update(100);
    return true;
}

bool C_CameraModule::GetUnknown78()
{
    return m_unknown26D;
}

bool C_CameraModule::GetState()
{
    return m_underwater;
}

bool C_CameraModule::HasScriptedView()
{
    return m_scriptedView != nullptr;
}

bool C_CameraModule::GetUnknown84()
{
    return m_unknown26E;
}

C_CameraHumanEffects *C_CameraModule::GetHumanEffects()
{
    return m_humanEffects;
}

I3D_frame *C_CameraModule::GetCamera()
{
    return m_camera;
}

I3D_sector *C_CameraModule::GetListenerSector()
{
    return m_listenerSector;
}

void C_CameraModule::StartMission(bool server)
{
    if (server)
        return;
    m_unknown2B0 = false;
    m_scriptedView = nullptr;
    m_unknown258 = 6.0f;
    m_unknown26C = false;
    LoadSettings();
    m_gameCamera = static_cast<I3D_camera *>(I3DGetDriver()->CreateFrame(FRAME_CAMERA));
    C_game *game = GetGame();
    m_sceneCamera = game->GetScene()->GetActiveCamera();
    if (!m_sceneCamera || !m_gameCamera) {
        dbgPrintf(C_fstr("###### (MO) Game camera not created."));
    } else {
        m_gameCamera->SetFOV(game->GetCutsceneFOV());
        m_gameCamera->SetRange(game->GetNearClip(), game->GetFarClip());
        game->GetScene()->SetActiveCamera(m_gameCamera);
        // In the scene camera's place.
        m_gameCamera->SetPosInline(m_sceneCamera->GetPos());
        m_gameCamera->SetRot(m_sceneCamera->GetRot());
        m_gameCamera->SetName("GameCamera");
        m_gameCamera->SetNearSectorFOV(1.1344640f);     // 65 degrees
        m_gameCamera->SetNearSectorRange(0.01f, 10.0f);
        m_gameCamera->Update();
        m_dir = m_wantedDir = WorldDir(m_gameCamera);
        m_pos = m_wantedPos = WorldPos(m_gameCamera);
    }
    m_unknown26D = false;
    m_unknown268 = 5;
    m_unknown26E = false;
    m_unknown26F = true;
    m_underwater = false;
    m_unknown278 = false;
    m_blendTime = 0;
    m_unknown284 = 0;
    m_unknown060 = m_unknown064 = m_unknown068 = 0;
    m_unknown070 = m_unknown074 = m_unknown078 = 0;
    m_unknown080[0] = m_unknown080[1] = m_unknown080[2] = 0.0f;
    m_unknown090[0] = m_unknown090[1] = m_unknown090[2] = 0.0f;
    m_target = nullptr;
    m_targetSpeed = -1.0f;
    m_listenerSector = nullptr;
    m_unknown250 = 0;
    m_unknown290 = false;
    m_unknown298 = m_unknown29C = m_unknown2A0 = 0;
    m_unknown0D0 = 0;
}

void C_CameraModule::StopMission(bool server)
{
    if (m_humanEffects->IsTactical())
        m_humanEffects->SetTactical(false);
    if (server)
        return;
    if (m_sceneCamera) {
        GetGame()->GetScene()->SetActiveCamera(m_sceneCamera);
        m_sceneCamera = nullptr;
    }
    if (m_gameCamera) {
        m_gameCamera->Release();
        m_gameCamera = nullptr;
    }
    if (m_camera) {
        // A human's camera dummy: its effect value back to 0.
        I3D_frame *model = m_camera->GetOwnerModel();
        if (model && model->GetType() == FRAME_MODEL) {
            if (C_Human *human = GetFrameActorAs<C_Human>(model, ACTOR_HUMAN))
                human->SetUnknown94(0.0f);
        }
        m_camera->Release();
        m_camera = nullptr;
    }
}

void C_CameraModule::LoadSettings()
{
    ResetUnknown2AC();
    C_chunk chunk;
    if (!chunk.Open(SETTINGS_FILE, false)) {
        dbgPrintf(C_fstr("###### (MO) Load camera file '%s' failed.", SETTINGS_FILE));
        return;
    }
    uint16_t id;
    chunk.Descend(id);
    uint32_t version = 0;                               // (not initialised by the original)
    chunk.Read(version);
    chunk.Ascend();
    if (version == 2) {
        chunk.Descend(id);
        // As many whole records as the chunk holds.
        for (int size = static_cast<int>(sizeof m_settings); size >= static_cast<int>(sizeof(S_CameraSettings));
             size -= static_cast<int>(sizeof(S_CameraSettings))) {
            if (chunk.Read(m_settings, size))
                break;
        }
        chunk.Ascend();
    }
    chunk.Close();
    m_unknown2AC = m_settings[1].distance;
}

void C_CameraModule::ResetUnknown2AC()
{
    m_unknown2AC = 3.0f;
}

bool C_CameraModule::GetCameraPos(int index, S_vector4 *pos)
{
    switch (index) {
    case CAMERA_POS_SCENE:
        if (I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera()) {
            *pos = WorldPos(camera);
            return true;
        }
        break;
    case CAMERA_POS_GAME:
        if (m_gameCamera) {
            *pos = WorldPos(m_gameCamera);
            return true;
        }
        break;
    case CAMERA_POS_CURRENT:
        *pos = m_pos;
        return true;
    case CAMERA_POS_WANTED:
        *pos = m_wantedPos;
        return true;
    }
    return false;
}

void C_CameraModule::SetBlendTime(int time)
{
    if (g_gameRole & GAME_ROLE_CLIENT) {
        C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
        if (player && player->GetVehicle())
            return;
    }
    m_blendTime = time;
}

void C_CameraModule::SetTarget(I3D_frame *frame, int speed)
{
    m_target = frame;
    m_targetSpeed = static_cast<float>(speed) * 0.017453292f;
}

bool C_CameraModule::GetCameraDir(int index, S_vector4 *dir)
{
    switch (index) {
    case CAMERA_POS_SCENE:
        if (I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera()) {
            *dir = WorldDir(camera);
            return true;
        }
        break;
    case CAMERA_POS_GAME:
        if (m_gameCamera) {
            *dir = WorldDir(m_gameCamera);
            return true;
        }
        break;
    case CAMERA_POS_CURRENT:
        *dir = m_dir;
        return true;
    case CAMERA_POS_WANTED:
        *dir = m_wantedDir;
        return true;
    }
    return false;
}

float C_CameraModule::GetViewDistance()
{
    float nearRange, farRange;
    GetGame()->GetScene()->GetActiveCamera()->GetRange(nearRange, farRange);
    return farRange;
}

bool C_CameraModule::SetNearSectorFov(float fov)
{
    if (!m_gameCamera)
        return false;
    m_gameCamera->SetNearSectorFOV(fov);
    m_gameCamera->Update();
    return true;
}

float C_CameraModule::GetNearSectorFov()
{
    return m_gameCamera ? m_gameCamera->GetNearSectorFOV() : 1.3962635f;
}

bool C_CameraModule::SetFov(float fov)
{
    if (!m_gameCamera)
        return false;
    m_gameCamera->SetFOV(fov);
    m_gameCamera->Update();
    return true;
}

float C_CameraModule::GetFov()
{
    return m_gameCamera ? m_gameCamera->GetFOV() : 1.3962635f;   // 80 degrees
}

void C_CameraModule::SetViewDir(const S_vector4 &dir)
{
    if (!(m_viewSettings->flags & CAMERA_SETTINGS_FIXED_DIR) && m_view != 10)
        m_wantedDir = dir;
}

bool C_CameraModule::IsGameCamera(I3D_frame *camera)
{
    return m_gameCamera == camera;
}

// --- the vehicles' camera dummies ----------------------------------------------------------------

C_CameraFrameKeeper::C_CameraFrameKeeper(C_Actor *owner, I3D_frame *frame) : m_frame(frame), m_owner(owner)
{
    static_assert(sizeof(C_CameraFrameKeeper) == 0x50, "C_CameraFrameKeeper layout");
    static_assert(offsetof(C_CameraFrameKeeper, m_frame) == 0x40, "C_CameraFrameKeeper layout");
    if (!frame)
        return;
    m_rot = frame->GetRot();
    const S_vector &pos = frame->GetPos();
    m_localPos = {pos.x, pos.y, pos.z, frame->GetPosW()};
    m_parentPos = WorldPos(m_frame->GetParent());
    m_worldPos = WorldPos(m_frame);
}

// (0x00466000, a JMP to Restore.)
C_CameraFrameKeeper::~C_CameraFrameKeeper()
{
    Restore();
}

bool C_CameraFrameKeeper::Restore()
{
    if (!m_frame)
        return false;
    m_frame->SetPosInline(S_vector(m_localPos.x, m_localPos.y, m_localPos.z));
    m_frame->SetRot(m_rot);
    m_frame->Update();
    m_parentPos = WorldPos(m_frame->GetParent());
    m_worldPos = WorldPos(m_frame);
    m_cut = true;
    return true;
}

bool C_CameraFrameKeeper::Update(int)
{
    if (!m_frame || !m_owner)
        return false;
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();

    // Over the parent by the dummy's height, or on the ground below.
    const S_vector4 parent = WorldPos(m_frame->GetParent());
    S_vector pos(parent.x, parent.y, parent.z);
    S_vector ground;
    if (CollisionSphereTest(pos, S_vector(0.0f, m_localPos.y, 0.0f), 0.3f, &ground, nullptr, 0xFFFFFFFFu, 0))
        pos.y = ground.y;
    else
        pos.y += m_localPos.y;
    m_frame->SetPos(pos);

    // The vehicle's motion: where to look and the camera's blend speed.
    S_vector dir(0.0f, 0.0f, 0.0f);
    float roll = 0.0f;
    float speed = 3.0f;
    if (m_owner->GetType() == ACTOR_CAR) {
        const C_Car *car = static_cast<const C_Car *>(m_owner);
        const S_vector4 &heading = car->GetDirection4();
        const S_vector &velocity = car->GetVelocity();
        dir.x = heading.x * 0.5f + velocity.x;
        dir.z = heading.z * 0.5f + velocity.z;
        NormalizeSummed(dir, dir.z * dir.z + dir.x * dir.x);
        speed = (car->GetSpeed() + 2.5f) * 0.3f;
    } else if (m_owner->GetType() == ACTOR_SUBMARINE) {
        const C_Submarine *submarine = static_cast<const C_Submarine *>(m_owner);
        const float k = submarine->GetSpeed() + 0.5f;
        const S_vector4 &heading = submarine->GetDirection4();
        dir = S_vector(heading.x * k, heading.y * k, heading.z * k);
        speed = std::fabs(k * 0.025f);
        if (speed < 0.03)
            speed = static_cast<float>(0.03);
        // Lowered with the body's bobbing, and rolled to and fro.
        const S_vector &local = m_frame->GetPos();
        m_frame->SetPosInline(S_vector(local.x, m_localPos.y - submarine->GetBob() * 0.4f, local.z));
        const float t = static_cast<float>(static_cast<double>(GetGame()->GetTime()) * 0.001f);
        roll = static_cast<float>(std::sin(static_cast<double>(t)) * 0.01884955660992227);
    }
    if (m_cut) {
        speed = 1000.0f;
        m_cut = false;
    }
    if (player && player->GetVehicle() == m_owner)
        g_vehicleCameraSpeed = speed;

    // Turned to look down along the motion unless it already does. (Quirk kept: the test compares
    // the dummy's y with +0.3 where the direction set has -0.3 before its normalization, so it is
    // always turned.)
    const S_vector view = m_frame->GetMatrix()(2).NormalizedGame();
    S_vector flat(view.x, 0.0f, view.z);
    NormalizeSummed(flat, flat.z * flat.z + flat.x * flat.x);
    const float dot = (dir.z * flat.z + flat.x * dir.x) + flat.y * dir.y;
    if (dot < 0.9999f || view.y != 0.3f) {
        dir.y = -0.3f;
        NormalizeSummed(dir, (dir.z * dir.z + dir.x * dir.x) + 0.09f);
        m_frame->SetDir(dir, roll);
    }
    m_frame->Update();
    return true;
}

// --- the human effects ---------------------------------------------------------------------------

C_CameraHumanEffects::C_CameraHumanEffects()
{
    static_assert(sizeof(C_CameraHumanEffects) == 0xA0, "C_CameraHumanEffects layout");
    static_assert(offsetof(C_CameraHumanEffects, m_tactical) == 0x48, "C_CameraHumanEffects layout");
    static_assert(offsetof(C_CameraHumanEffects, m_camera) == 0x80, "C_CameraHumanEffects layout");
    static_assert(offsetof(C_CameraHumanEffects, m_humans) == 0x8C, "C_CameraHumanEffects layout");
    m_camera = static_cast<I3D_camera *>(I3DGetDriver()->CreateFrame(FRAME_CAMERA));
}

C_CameraHumanEffects::~C_CameraHumanEffects()
{
    if (m_camera) {
        m_camera->Release();
        m_camera = nullptr;
    }
}
