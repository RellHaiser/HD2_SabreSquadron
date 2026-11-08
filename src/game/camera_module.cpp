// The camera module (0x00462320..0x0046cb20): see game/camera_module.h. This file: the module's
// core (part 8e-i: 0x00464d90..0x004668c0), the human effects, the command map's tactical
// camera (part 8e-ii: 0x00462320..0x00464d77), the update (part 8e-iii: 0x00469300 and the car's
// camera, the scripted view, the debug camera), the placement, the move, the view and the special
// modes (part 8e-iv: 0x00466970, 0x00468130, 0x0046c370, 0x0046cb20).
#include "game/camera_module.h"

#include <cmath>
#include <cstddef>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/first_person_view.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/human_pose_internal.h"
#include "game/junkers88.h"
#include "game/pending.h"
#include "game/profile.h"
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
    static_assert(offsetof(C_CameraModule, m_spectator) == 0x274, "C_CameraModule layout");
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
        SelectView(0);
}

bool C_CameraModule::SaveGame(C_chunk &)
{
    return true;
}

bool C_CameraModule::LoadGame(C_chunk &)
{
    SelectView(0);
    Update(100);
    return true;
}

bool C_CameraModule::GetUnknown78()
{
    return m_debugCamera;
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
    return m_spectating;
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
    m_scriptedViewOn = false;
    m_scriptedView = nullptr;
    m_turnSpeed = 6.0f;
    m_cut = false;
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
    m_debugCamera = false;
    m_debugSpeed = 5;
    m_spectating = false;
    m_mouseLook = true;
    m_underwater = false;
    m_unknown278 = false;
    m_blendTime = 0;
    m_shakeTime = 0;
    m_shakePos.x = m_shakePos.y = m_shakePos.z = 0.0f;
    m_shakeDir.x = m_shakeDir.y = m_shakeDir.z = 0.0f;
    m_offsetDir.x = m_offsetDir.y = m_offsetDir.z = 0.0f;
    m_offsetPos.x = m_offsetPos.y = m_offsetPos.z = 0.0f;
    m_target = nullptr;
    m_targetSpeed = -1.0f;
    m_listenerSector = nullptr;
    m_listenerTime = 0;
    m_aiming = false;
    m_time = m_offsetPosDelay = m_offsetDirDelay = 0;
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

// (0x00462320 / 0x00463b70: the guarded atexit of a header's static, as 0x00464d90; the
// constructor's unwind tidies the list with 0x00463910.)

namespace {

// The tactical camera's queries: a 0.2 m sphere, these volume types and flags skipped.
const float TACTICAL_RADIUS = 0.2f;
const uint32_t TACTICAL_EXCLUDE_TYPES = 0x18100;
const uint32_t TACTICAL_EXCLUDE_FLAGS = 0x600000;
// The turn's pitch limit (89 degrees).
const float TACTICAL_PITCH_LIMIT = 1.5533430f;

const S_vector &Xyz(const S_vector4 &v)
{
    return reinterpret_cast<const S_vector &>(v);
}

// S_vector::Magnitude with the squared length the caller summed (0 below 1e-8).
float SummedLength(float square)
{
    if (square < 1e-8f)
        return 0.0f;
    if (std::fabs(square - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(square);
}

} // namespace

void C_CameraHumanEffects::SetTacticalZoom(float zoom)
{
    m_zoom = zoom;
}

bool C_CameraHumanEffects::IsTactical() const
{
    return m_tactical;
}

void C_CameraHumanEffects::SetTacticalMotion(float x, float y)
{
    m_motionX = x;
    m_motionY = y;
}

void C_CameraHumanEffects::StartFlight()
{
    S_vector move(m_target.x - m_pos.x, m_target.y - m_pos.y, m_target.z - m_pos.z);
    S_vector hit;
    if (CollisionSphereTest(Xyz(m_pos), move, TACTICAL_RADIUS, &hit, nullptr, TACTICAL_EXCLUDE_TYPES,
                            TACTICAL_EXCLUDE_FLAGS)) {
        // (The point's w takes an uninitialised local's in the original; kept here.)
        m_target.x = hit.x;
        m_target.y = hit.y;
        m_target.z = hit.z;
        move = S_vector(m_target.x - m_pos.x, m_target.y - m_pos.y, m_target.z - m_pos.z);
    }
    const float length = SummedLength((move.z * move.z + move.y * move.y) + move.x * move.x);
    const float part = length > 1.0f ? 0.75f : 0.0f;
    move.x *= part;
    move.y *= part;
    move.z *= part;
    m_flightDistance = SummedLength((move.z * move.z + move.y * move.y) + move.x * move.x);
    m_flightTime = 0.0f;
    const float third = (length < 5.0f ? 1.25f : length < 35.0f ? 1.875f : 2.5f) * 0.33333334f;
    m_flightAccel = (m_flightDistance * 0.6666667f) / (third * third);
    // (The point's w: as above.)
    m_target.x = move.x + m_pos.x;
    m_target.y = move.y + m_pos.y;
    m_target.z = move.z + m_pos.z;
    m_flightFrom.Make(m_camera->GetMatrix());
    m_flightTo = m_flightFrom;
    m_flightTo.SetDir(move, 0.0f);
}

void C_CameraHumanEffects::UpdateLookPoint()
{
    I3D_sector *primary = GetGame()->GetScene()->GetPrimarySector();
    m_lookDistance = GetGame()->GetScene()->GetSector(Xyz(m_pos)) == primary ? 10.0f : 2.0f;
    const S_vector4 dir = WorldDir(m_camera);
    m_lookPoint = {dir.x * m_lookDistance + m_pos.x, dir.y * m_lookDistance + m_pos.y,
                   dir.z * m_lookDistance + m_pos.z, dir.w};
}

void C_CameraHumanEffects::LookAt(C_Human *human)
{
    const S_vector4 target = WorldPos(human ? human->GetBodyFrame() : m_gameCamera);
    S_vector dir;
    if (human) {
        dir = human->GetViewDir();
    } else {
        const S_vector4 view = WorldDir(m_gameCamera);
        dir = S_vector(view.x, view.y, view.z);
    }
    m_pos = target;
    float back = 3.0f;
    if (human && human->GetVehicle()) {
        switch (human->GetVehicle()->GetType()) {
        case ACTOR_CAR:
        case ACTOR_SUBMARINE:
        case ACTOR_STATIC_WEAPON:
        case ACTOR_JUNKERS88:
            m_pos.y += 1.5f;
            back = 5.0f;
            break;
        default:
            break;
        }
    }
    Move(S_vector(-(dir.x * back), 1.5f - dir.y * back, -(dir.z * back)));
    m_camera->SetDir(S_vector(target.x - m_pos.x, target.y - m_pos.y, target.z - m_pos.z), 0.0f);
    m_camera->Update();
}

void C_CameraHumanEffects::SetTactical(bool on)
{
    if (on == IsTactical())
        return;
    m_mode = TACTICAL_IDLE;
    m_tactical = !m_tactical;
    if (!m_tactical) {
        GetGame()->GetScene()->SetActiveCamera(m_gameCamera);
        g_cameraModule.SelectView(0);
        ResetHumans(m_tactical);
        return;
    }
    m_flightSpeed = 0.0f;
    m_gameCamera = GetGame()->GetScene()->GetActiveCamera();
    m_camera->Duplicate(m_gameCamera);
    m_camera->SetFOV(GetGame()->GetCutsceneFOV());
    m_camera->SetRange(GetGame()->GetNearClip(), GetGame()->GetFarClip());
    m_camera->SetAspectRatio(m_gameCamera->GetAspectRatio());

    // Behind and above the active soldier, along his view.
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    const S_vector4 body = WorldPos(player->GetBodyFrame());
    const S_matrix &m = player->GetFrame()->GetMatrix();
    S_vector view(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeSummed(view, (view.z * view.z + view.x * view.x) + view.y * view.y);
    S_vector flat(view.x, 0.0f, view.z);
    NormalizeSummed(flat, flat.z * flat.z + flat.x * flat.x);
    m_pos = body;
    Move(S_vector((body.x - flat.x * 7.0f) - m_pos.x, ((body.y - flat.y * 7.0f) + 3.0f) - m_pos.y,
                  (body.z - flat.z * 7.0f) - m_pos.z));
    S_vector dir(flat.x, -0.35f, flat.z);
    NormalizeSummed(dir, (dir.z * dir.z + dir.x * dir.x) + 0.35f * 0.35f);
    m_camera->SetDir(dir, 0.0f);
    m_camera->Update();
    GetGame()->GetScene()->SetActiveCamera(m_camera);
    ResetHumans(m_tactical);
}

void C_CameraHumanEffects::Remove(C_Human *human)
{
    for (size_t i = m_humans.size(); i-- > 0;) {
        if (m_humans[i].human == human) {
            human->SetUnknown94(0.0f);
            m_humans[i].human = m_humans.back().human;
            m_humans.pop_back();
            return;
        }
    }
}

void C_CameraHumanEffects::SetTacticalMode(int mode, const S_vector4 *point)
{
    const int old = m_mode;
    if (old == mode)
        return;
    if (old == TACTICAL_FLY) {
        if (mode == TACTICAL_IDLE)
            return;
        if (mode == TACTICAL_DRAG) {
            if (m_flightTime < 300.0f)
                return;
            m_keepSpeed = true;
        } else {
            m_flightTime = 0.0f;
        }
    }
    switch (old) {
    case TACTICAL_IDLE:
    case TACTICAL_MOVE:
    case TACTICAL_DRAG:
    case TACTICAL_UP:
    case TACTICAL_DOWN:
        UpdateLookPoint();
        break;
    default:
        break;
    }
    if (point)
        m_target = *point;
    if (mode == TACTICAL_DRAG) {
        if (m_clickTime != 0) {
            // A double click: the flight.
            if (!m_keepSpeed)
                m_flightSpeed = 0.0f;
            m_keepSpeed = false;
            StartFlight();
            m_clickTime = 0;
            m_mode = TACTICAL_FLY;
            return;
        }
        m_clickTime = 500;
    }
    m_mode = mode;
}

void C_CameraHumanEffects::ResetHumans(bool tactical)
{
    if (tactical)
        return;
    for (size_t i = m_humans.size(); i-- > 0;)
        m_humans[i].human->SetUnknown94(0.0f);
    m_humans.clear();
}

float C_CameraHumanEffects::GetTransparency(C_Human *human) const
{
    const S_vector4 body = WorldPos(human->GetBodyFrame());
    const S_vector4 frame = WorldPos(human->GetFrame());
    float t;
    const float distance2 = SegmentDistance2(Xyz(m_pos), Xyz(body), S_vector(frame.x, frame.y + 0.1f, frame.z), &t);
    if (distance2 > 2.4f)
        return 0.0f;
    if (distance2 < 0.4f)
        return 1.0f;
    return 1.0f - (distance2 - 0.4f) * 0.5f;
}

void C_CameraHumanEffects::UpdateHuman(C_Human *human)
{
    const float value = GetTransparency(human);
    for (size_t i = m_humans.size(); i-- > 0;) {
        if (m_humans[i].human != human)
            continue;
        if (m_humans[i].transparency != value) {
            m_humans[i].human->SetUnknown94(value);
            if (m_humans[i].transparency == 0.0f)
                m_humans.erase(m_humans.begin() + static_cast<std::ptrdiff_t>(i));
            else
                m_humans[i].transparency = value;
        }
        return;
    }
    if (value != 0.0f) {
        m_humans.push_back({human, value});
        human->SetUnknown94(value);
    }
}

void C_CameraHumanEffects::UpdateHumans(int)
{
    const C_EnemyRecords &enemies = g_teamManager.GetPlayerTeam()->GetEnemyRecords();
    for (int i = enemies.GetCount(); i-- > 0;)
        UpdateHuman(enemies.GetRecord(i)->enemy);
    for (int i = g_teamManager.GetPlayerTeam()->GetMemberCount(); i-- > 0;)
        UpdateHuman(g_teamManager.GetPlayerTeam()->GetMember(i));
}

void C_CameraHumanEffects::UpdateTactical(int timeDelta)
{
    const S_matrix &m = m_camera->GetMatrix();
    S_vector view(m.m[2][0], m.m[2][1], m.m[2][2]);
    NormalizeSummed(view, (view.x * view.x + view.y * view.y) + view.z * view.z);
    const S_vector right(m.m[0][0], m.m[0][1], m.m[0][2]);
    // A quarter of the speed outside the primary sector (the moves but the zoom's).
    float factor = 1.0f;
    I3D_sector *sector = GetGame()->GetScene()->GetSector(Xyz(m_pos));
    if (sector != GetGame()->GetScene()->GetPrimarySector())
        factor = 0.25f;
    const float delta = static_cast<float>(timeDelta);
    const float t = delta * m_zoom * 0.001f;
    if (m_clickTime != 0) {
        m_clickTime -= timeDelta;
        if (m_clickTime <= 0) {
            m_clickTime = 0;
            m_keepSpeed = false;
        }
    }

    switch (m_mode) {
    case TACTICAL_MOVE: {
        // Sideways, then forward along the ground.
        if (m_motionX != 0.0f)
            Move(S_vector(right.x * factor * t * 10.0f * -m_motionX, right.y * factor * t * 10.0f * -m_motionX,
                          right.z * factor * t * 10.0f * -m_motionX));
        S_vector flat(view.x, 0.0f, view.z);
        NormalizeSummed(flat, flat.z * flat.z + flat.x * flat.x);
        if (m_motionY != 0.0f)
            Move(S_vector(flat.x * factor * t * 10.0f * m_motionY, flat.y * factor * t * 10.0f * m_motionY,
                          flat.z * factor * t * 10.0f * m_motionY));
        break;
    }
    case TACTICAL_TURN:
    case 4:
    case 5:
    case 6:
    case 7: {
        // Pitched about its right axis (the original's limits: past +89 degrees the angle becomes
        // the excess, past -89 degrees the way back to it), then turned about the y axis.
        const double pitch = std::asin(static_cast<double>(view.y));
        float angle = t * m_motionY * -2.1f;
        const float sum = static_cast<float>(angle + pitch);
        if (sum >= TACTICAL_PITCH_LIMIT)
            angle = static_cast<float>(pitch - TACTICAL_PITCH_LIMIT);
        if (sum <= -TACTICAL_PITCH_LIMIT)
            angle = static_cast<float>(-TACTICAL_PITCH_LIMIT - pitch);
        S_quat q;
        q.Make(right, angle);
        view = view.RotateByMatrix(q.RotationMatrix());
        view = view.RotateByMatrix(RotateYMatrix(t * m_motionX * -2.1f));
        m_camera->SetDir(view, 0.0f);
        m_camera->Update();
        break;
    }
    case TACTICAL_DRAG: {
        // The ground under the cursor follows it.
        S_vector flat(view.x, 0.0f, view.z);
        NormalizeSummed(flat, flat.z * flat.z + flat.x * flat.x);
        const S_vector forward(flat.x * t * 10.0f * m_motionY, flat.y * t * 10.0f * m_motionY,
                               flat.z * t * 10.0f * m_motionY);
        const S_vector side(right.x * t * 10.0f * m_motionX, right.y * t * 10.0f * m_motionX,
                            right.z * t * 10.0f * m_motionX);
        const S_vector move(forward.x * 100.0f - side.x * 100.0f, forward.y * 100.0f - side.y * 100.0f,
                            forward.z * 100.0f - side.z * 100.0f);
        if ((move.z * move.z + move.y * move.y) + move.x * move.x != 0.0f)
            Move(S_vector(move.x * factor, move.y * factor, move.z * factor));
        break;
    }
    case TACTICAL_UP:
    case TACTICAL_DOWN: {
        // Up (down) 12 m/s; stopped by a water surface, through it when there is room.
        const float sign = m_mode == TACTICAL_UP ? 1.0f : -1.0f;
        S_CollisionVolume *hit = Move(S_vector(0.0f, t * factor * (sign * 12.0f), 0.0f));
        if (hit && (hit->flags & COLLISION_FLAG_WATER) &&
            !CollisionSphereTest(Xyz(m_pos), S_vector(0.0f, sign * 0.6f, 0.0f), TACTICAL_RADIUS, nullptr, nullptr,
                                 TACTICAL_EXCLUDE_TYPES, TACTICAL_EXCLUDE_FLAGS)) {
            m_pos.y += sign * 0.4f;
            m_camera->SetPos(Xyz(m_pos));
            m_camera->Update();
        }
        break;
    }
    case TACTICAL_FLY: {
        m_flightTime += delta;
        S_vector way(m_target.x - m_pos.x, m_target.y - m_pos.y, m_target.z - m_pos.z);
        const float remaining = SummedLength((way.x * way.x + way.z * way.z) + way.y * way.y);
        // Turned as far as it has come.
        const float done = remaining / m_flightDistance * 0.9f;
        const S_quat rot = m_flightFrom.Slerp(m_flightTo, 1.0f - (1.0f < done ? 1.0f : done), false);
        m_camera->SetDir(rot.GetDir(), 0.0f);
        if (m_flightDistance * 0.6666667f < remaining)
            m_flightSpeed = delta * m_flightAccel * 0.001f + m_flightSpeed;
        else if (m_flightDistance * 0.33333334f > remaining)
            m_flightSpeed = m_flightSpeed - delta * m_flightAccel * 0.001f;
        float step = delta * m_flightSpeed * 0.001f;
        NormalizeSummed(way, (way.x * way.x + way.z * way.z) + way.y * way.y);
        bool arrived = false;
        if (step >= remaining || m_flightSpeed <= 0.0f) {
            arrived = true;
            step = remaining;
        }
        way.x *= step;
        way.y *= step;
        way.z *= step;
        S_vector end;
        if (CollisionSlideSphere(Xyz(m_pos), way, TACTICAL_RADIUS, end, nullptr, TACTICAL_EXCLUDE_TYPES,
                                 TACTICAL_EXCLUDE_FLAGS)) {
            // (The w: an uninitialised local's in the original; kept here.)
            m_pos.x = end.x;
            m_pos.y = end.y;
            m_pos.z = end.z;
        } else {
            m_pos.x = way.x + m_pos.x;
            m_pos.y = way.y + m_pos.y;
            m_pos.z = way.z + m_pos.z;
        }
        m_camera->SetPos(Xyz(m_pos));
        m_camera->Update();
        if (arrived) {
            // (Then an empty function, 0x00464a60.)
            m_mode = TACTICAL_IDLE;
            m_flightTime = 0.0f;
            UpdateLookPoint();
        }
        break;
    }
    case TACTICAL_PLUS:
    case TACTICAL_MINUS: {
        // Along the view, 8 m/s (the zoom's speed, the same in every sector).
        const float speed = m_mode == TACTICAL_PLUS ? 8.0f : -8.0f;
        Move(S_vector(view.x * t * speed, view.y * t * speed, view.z * t * speed));
        break;
    }
    default:
        break;
    }
    UpdateHumans(timeDelta);
}

S_CollisionVolume *C_CameraHumanEffects::Move(const S_vector &move)
{
    S_vector end;
    S_CollisionVolume *hit = CollisionSlideSphere(Xyz(m_pos), move, TACTICAL_RADIUS, end, nullptr,
                                                  TACTICAL_EXCLUDE_TYPES, TACTICAL_EXCLUDE_FLAGS);
    if (hit) {
        // 5 cm off where it stopped, back towards where it was. (The w: an uninitialised local's
        // in the original; kept here.)
        S_vector back(m_pos.x - end.x, m_pos.y - end.y, m_pos.z - end.z);
        NormalizeSummed(back, (back.z * back.z + back.y * back.y) + back.x * back.x);
        m_pos.x = back.x * 0.05f + end.x;
        m_pos.y = back.y * 0.05f + end.y;
        m_pos.z = back.z * 0.05f + end.z;
    } else {
        m_pos.x = m_pos.x + move.x;
        m_pos.y = move.y + m_pos.y;
        m_pos.z = move.z + m_pos.z;
    }
    m_camera->SetPos(Xyz(m_pos));
    m_camera->Update();
    return hit;
}

// --- the update (part 8e-iii) ----------------------------------------------------------------------

namespace {

// The look actions (C_Controls; the car's camera and the debug camera) and the debug camera's
// speed steps.
const int ACTION_LOOK_X = 0x34;
const int ACTION_LOOK_Y = 0x35;
const int ACTION_DEBUG_SPEED = 0x36;
const float LOOK_TURN = 0.9424779f;                     // radians per unit of a look action
// The debug camera's speeds (m/s; 0x0080feac).
const float DEBUG_CAMERA_SPEEDS[10] = {0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f};
const float PI_F = 3.1415927f;

S_vector &XyzRef(S_vector4 &v)
{
    return reinterpret_cast<S_vector &>(v);
}

// An offset shrinks by 1 per second once its delay is over (then the delay stays 0).
void ShrinkOffset(S_vector4 &offset, int &delay, int timeDelta)
{
    S_vector &v = XyzRef(offset);
    float length = v.Magnitude();
    if (length == 0.0f)
        return;
    if (delay != 0)
        delay -= timeDelta;
    if (delay > 0)
        return;
    length -= static_cast<float>(timeDelta - delay) * 0.001f;
    if (length < 0.0f)
        length = 0.0f;
    v.Normalize();
    v *= length;
    delay = 0;
}

} // namespace

void C_CameraModule::Update(int timeDelta)
{
    if (m_spectating && !m_debugCamera)
        return;
    if (m_debugCamera) {
        UpdateDebugCamera(timeDelta);
        return;
    }
    if (m_scriptedViewOn) {
        UpdateScriptedView(timeDelta);
        return;
    }
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (player && player->GetVehicle() && player->GetVehicle()->GetType() == ACTOR_CAR &&
        !(player->GetVehicle()->GetSeatFlags(player->GetVehicleSeat()) & SEAT_FLAG_WEAPON)) {
        UpdateCarCamera(timeDelta);
        return;
    }

#ifndef HD2_DEDICATED   // no mouse in LS3DRF.dll
    int yaw = GetIGraph()->GetMouseDeltaX();
    int pitch = GetIGraph()->GetMouseDeltaY();
#else
    int yaw = 0;
    int pitch = 0;
#endif
    if (g_gameProfile.GetDetailSettings()->invertMouse)
        pitch = -pitch;
    C_Human *selected = g_teamManager.GetPlayerTeam()->GetSelectedMember();
    m_time += timeDelta;
    if (!m_mouseLook || !GetGame()->IsPlayerInputEnabled())
        pitch = yaw = 0;
    S_vector &dir = XyzRef(m_wantedDir);

    // The first-person view: the soldier's view without a member selected; the camera kept out
    // of the band around the water's surface while his head is under it.
    m_waterOffset = 0.0f;
    if (m_view == 2) {
        if (!selected) {
            pitch = yaw = 0;
            if (player)
                m_wantedDir = reinterpret_cast<const S_vector4 &>(player->GetViewDir());   // (an S_vector4)
        }
        bool underWater = false;
        float surface;
        if (player && (player->GetMovement()->GetHeadWater(&underWater, &surface), underWater)) {
            const float height = m_camera->GetMatrix().m[3][1];
            if (height < m_waterLevel) {
                const float limit = (m_waterLevel - 0.1f) - m_viewSettings->radius;
                if (height > limit)
                    m_waterOffset = limit - height;
            } else {
                const float limit = (m_viewSettings->radius + m_waterLevel) + 0.1f;
                if (height < limit)
                    m_waterOffset = limit - height;
            }
        }
    }

    // (The debug camera's branches here are not reached: it returned above.)
    // Turned by the mouse about the y axis (without a member selected, or in view 10) ...
    if (yaw != 0 && (!selected || m_view == 10)) {
        dir.Normalize();
        S_quat turn;
        turn.Make(S_vector(0.0f, -1.0f, 0.0f), static_cast<float>(yaw) * 0.004f);
        dir.Normalize();
        dir = dir.RotateByMatrix(turn.RotationMatrix());   // (the w: a local's, kept here)
        dir.Normalize();
    }
    // ... and pitched about its side axis within the view's limits.
    if (((m_viewSettings->flags & CAMERA_SETTINGS_PITCH) || !selected) && pitch != 0) {
        dir.Normalize();
        S_vector axis(1.0f, 0.0f, 0.0f);
        if (!((dir.x == 0.0f && dir.y == 1.0f && dir.z == 0.0f) || (dir.x == 0.0f && dir.y == -1.0f && dir.z == 0.0f)))
            axis = S_vector(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f).NormalizedGame();
        float angle = static_cast<float>(pitch) * 0.004f;
        const double current = std::asin(static_cast<double>(dir.y));
        const float after = static_cast<float>(current - angle);
        if (after >= m_viewSettings->maxPitch)
            angle = static_cast<float>(current - m_viewSettings->maxPitch);
        if (after <= m_viewSettings->minPitch)
            angle = static_cast<float>(current - m_viewSettings->minPitch);
        S_quat turn;
        turn.Make(axis, angle);
        dir.Normalize();
        dir = dir.RotateByMatrix(turn.RotationMatrix());
        dir.Normalize();
    }
    // The camera dummy's own direction taken (views 7 .. 9, or none selected).
    if ((m_viewSettings->flags & CAMERA_SETTINGS_FOLLOW) && m_camera) {
        dir.Normalize();
        const bool own = m_view == 9 || m_view == 7 || m_view == 8;
        if ((m_viewSettings->flags & CAMERA_SETTINGS_FIXED_DIR) && (own || !selected)) {
            m_wantedDir = WorldDir(m_camera);
            dir.Normalize();
        }
    }

    PlaceCamera(timeDelta);
    MoveCamera(timeDelta);
    m_gameCamera->Update();

    // The sound listener's sector, every 500 ms.
    m_listenerTime -= timeDelta;
    if (m_listenerTime < 0) {
        I3D_camera *active = GetGame()->GetScene()->GetActiveCamera();
        const S_vector &pos = active->GetMatrix()(3);
        m_listenerSector = GetGame()->GetScene()->GetSector(pos);
        while (m_listenerTime < 0)
            m_listenerTime += 500;
    }
    ShrinkOffset(m_offsetDir, m_offsetDirDelay, timeDelta);
    ShrinkOffset(m_offsetPos, m_offsetPosDelay, timeDelta);
    if (m_aiming) {
        m_aimTime += timeDelta;
        if (static_cast<float>(m_aimTime) > 5000.0f)
            m_aimTime = 5000;
    }
}

void C_CameraModule::UpdateCarCamera(int timeDelta)
{
    float lookX = g_controls.GetValue(ACTION_LOOK_X, false);
    float lookY = g_controls.GetValue(ACTION_LOOK_Y, false);
    if (g_gameProfile.GetDetailSettings()->invertMouse)
        lookY = -lookY;
    const float roll = PoseGetRoll(m_camera->GetMatrix());
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    const uint32_t flags = player->GetVehicle()->GetSeatFlags(player->GetVehicleSeat());
    if (!GetGame()->IsPlayerInputEnabled() || player->GetHealth() <= 0)
        lookX = lookY = 0.0f;

    if ((flags & SEAT_FLAG_WEAPON) || ((flags & SEAT_FLAG_HIDDEN) && m_view == 9) ||
        ((flags & SEAT_FLAG_ARMED) && player->IsLocalPlayer())) {
        m_wantedDir = reinterpret_cast<const S_vector4 &>(player->GetViewDir());   // (an S_vector4)
    } else {
        // Turned from the camera dummy's view: 30 degrees aside, 25 up or down.
        if (lookX != 0.0f) {
            m_carYaw = lookX * LOOK_TURN + m_carYaw;
            if (m_carYaw > 0.5235988f)
                m_carYaw = 0.5235988f;
            else if (m_carYaw < -0.5235988f)
                m_carYaw = -0.5235988f;
        }
        if (lookY != 0.0f) {
            m_carPitch = m_carPitch - lookY * LOOK_TURN;
            if (m_carPitch > 0.43633232f)
                m_carPitch = 0.43633232f;
            else if (m_carPitch < -0.43633232f)
                m_carPitch = -0.43633232f;
        }
        const S_vector view = m_camera->GetMatrix()(2).NormalizedGame();
        const float viewPitch = static_cast<float>(
            std::atan2(static_cast<double>(view.y), static_cast<double>(std::sqrt(view.z * view.z + view.x * view.x))));
        float heading =
            static_cast<float>(std::atan2(static_cast<double>(view.x), static_cast<double>(view.z)) + m_carYaw);
        if (heading < 0.0f)
            heading = heading + 6.2831855f;
        else if (heading > 6.2831855f)
            heading = heading - 6.2831855f;
        // (Quirk kept: out of -pi .. pi by pi, not 2 pi.)
        float elevation = viewPitch + m_carPitch;
        if (elevation < -PI_F)
            elevation = elevation + PI_F;
        else if (elevation > PI_F)
            elevation = elevation - PI_F;
        if (elevation > 0.5235988f)
            elevation = 0.5235988f;
        else if (elevation < -0.5235988f)
            elevation = -0.5235988f;
        const double cosElevation = std::cos(static_cast<double>(elevation));
        m_wantedDir.x = static_cast<float>(cosElevation * std::sin(static_cast<double>(heading)));
        m_wantedDir.z = static_cast<float>(std::cos(static_cast<double>(heading)) * cosElevation);
        m_wantedDir.y = static_cast<float>(std::sin(static_cast<double>(elevation)));
    }
    // (Then an empty function, 0x004d0440, given the camera dummy's position and that plus the
    // direction: a debug line's leftover.)
    XyzRef(m_wantedDir).Normalize();
    PlaceCamera(timeDelta);
    MoveCamera(timeDelta);
    // Rolled with the camera dummy.
    S_quat rot;
    rot.SetDir(m_gameCamera->GetMatrix()(2).NormalizedGame(), roll);
    m_gameCamera->SetRot(rot);
    m_gameCamera->Update();
    GetGame()->Print("Yaw %.2f, Pitch %.2f, Roll %.2f", m_carYaw, m_carPitch, roll);
}

void C_CameraModule::UpdateScriptedView(int timeDelta)
{
    if (!m_scriptedView || !m_gameCamera)
        return;
    S_vector4 pos = WorldPos(m_scriptedView);
    const S_matrix &frame = m_scriptedView->GetMatrix();
    S_vector4 dir = {frame.m[2][0], frame.m[2][1], frame.m[2][2], frame.m[2][3]};
    XyzRef(dir).Normalize();
    // 10 m behind it.
    const S_vector back(dir.x * 10.0f, dir.y * 10.0f, dir.z * 10.0f);
    pos.x -= back.x;
    pos.y -= back.y;
    pos.z -= back.z;
    // Swaying up and down and aside with the game's time.
    S_vector side(dir.y * 0.0f - dir.z, dir.z * 0.0f - dir.x * 0.0f, dir.x - dir.y * 0.0f);
    S_vector up(dir.y * side.z - dir.z * side.y, dir.z * side.x - side.z * dir.x, side.y * dir.x - dir.y * side.x);
    NormalizeSummed(up, (up.z * up.z + up.y * up.y) + up.x * up.x);
    side = side.NormalizedGame();
    const float phase = static_cast<float>(static_cast<double>(GetGame()->GetTime()) * 0.0015f);
    const float swayUp = static_cast<float>(std::sin(std::cos(static_cast<double>(phase))) * 0.30000001192092896);
    up.x *= swayUp;
    up.y *= swayUp;
    up.z *= swayUp;
    const float phase2 = static_cast<float>(static_cast<double>(GetGame()->GetTime()) * 0.0015f) + 3.0f;
    const float wave = static_cast<float>(std::sin(static_cast<double>(phase2)) + 1.0);
    const float swaySide = static_cast<float>(std::cos(static_cast<double>(wave)) * 0.30000001192092896);
    side.x *= swaySide;
    side.y *= swaySide;
    side.z *= swaySide;
    pos.x = (up.x + pos.x) + side.x;
    pos.y = (pos.y + up.y) + side.y;
    pos.z = (pos.z + up.z) + side.z;
    m_gameCamera->SetPosInline(Xyz(pos));
    m_gameCamera->SetWorldDir(Xyz(dir), 0.0f);

    // The shake, and the offsets turned with the game camera.
    if (m_shakeTime > 0) {
        m_shakeTime -= timeDelta;
        if (m_shakeTime < 0) {
            m_shakeTime = 0;
            m_shakePos.x = m_shakePos.y = m_shakePos.z = 0.0f;
            m_shakeDir.x = m_shakeDir.y = m_shakeDir.z = 0.0f;
            m_gameCamera->Update();
            return;
        }
        const double left = static_cast<double>(m_shakeTime);
        const float a = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(0.07f * left))) *
                                           static_cast<double>(static_cast<float>(left * 6e-05f)));
        const float b = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.08f))) *
                                           static_cast<double>(static_cast<float>(left * 4e-05f)));
        const float c = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.05f))) *
                                           static_cast<double>(static_cast<float>(left * 7e-05f)));
        // (Their w: a local's in the original; kept here.)
        m_shakePos.x = a;
        m_shakePos.y = b;
        m_shakePos.z = c;
        m_shakeDir.x = c * 0.15f;
        m_shakeDir.y = b * -0.25f;
        m_shakeDir.z = a * 0.15f;
        S_vector offset = Xyz(m_offsetPos);
        offset *= m_gameCamera->GetMatrix();
        const S_vector &camera = m_gameCamera->GetMatrix()(3);
        offset = S_vector(offset.x - camera.x, offset.y - camera.y, offset.z - camera.z);
        m_gameCamera->SetPosInline(S_vector((pos.x + m_shakePos.x) + offset.x, offset.y + (pos.y + m_shakePos.y),
                                            offset.z + (pos.z + m_shakePos.z)));
        S_vector turn = Xyz(m_offsetDir);
        turn *= m_gameCamera->GetMatrix();
        const S_vector &moved = m_gameCamera->GetMatrix()(3);
        turn = S_vector(turn.x - moved.x, turn.y - moved.y, turn.z - moved.z);
        const S_vector shaken(dir.x + m_shakeDir.x, dir.y + m_shakeDir.y, dir.z + m_shakeDir.z);
        m_gameCamera->SetWorldDir(S_vector(shaken.x + turn.x, turn.y + shaken.y, turn.z + shaken.z), 0.0f);
    }
    m_gameCamera->Update();
}

void C_CameraModule::UpdateDebugCamera(int timeDelta)
{
    const float lookX = g_controls.GetValue(ACTION_LOOK_X, false);
    float lookY = g_controls.GetValue(ACTION_LOOK_Y, false);
    if (g_gameProfile.GetDetailSettings()->invertMouse)
        lookY = -lookY;
    if (lookX != 0.0f || lookY != 0.0f) {
        float heading = static_cast<float>(std::atan2(static_cast<double>(m_wantedDir.x), static_cast<double>(m_wantedDir.z)));
        double elevation = std::atan2(static_cast<double>(m_wantedDir.y),
                                      static_cast<double>(std::sqrt(m_wantedDir.z * m_wantedDir.z + m_wantedDir.x * m_wantedDir.x)));
        if (lookX != 0.0f)
            heading = lookX * LOOK_TURN + heading;
        if (lookY != 0.0f)
            elevation = static_cast<float>(elevation - static_cast<double>(lookY * LOOK_TURN));
        // (Quirk kept: out of -pi .. pi by pi, not 2 pi.) At most 85 degrees up or down.
        if (elevation < -PI_F)
            elevation = static_cast<float>(elevation + PI_F);
        else if (elevation > PI_F)
            elevation = static_cast<float>(elevation - PI_F);
        if (elevation > 1.4835299f)
            elevation = 1.4835299f;
        else if (elevation < -1.4835299f)
            elevation = -1.4835299f;
        const double cosElevation = std::cos(elevation);
        m_wantedDir.x = static_cast<float>(cosElevation * std::sin(static_cast<double>(heading)));
        m_wantedDir.z = static_cast<float>(std::cos(static_cast<double>(heading)) * cosElevation);
        m_wantedDir.y = static_cast<float>(std::sin(elevation));
        XyzRef(m_wantedDir).Normalize();
    }
    // The mouse buttons: forward, back.
#ifndef HD2_DEDICATED   // no mouse in LS3DRF.dll
    uint32_t buttons = GetIGraph()->GetMouseButtons();
#else
    uint32_t buttons = 0;
#endif
    if (!m_mouseLook || !GetGame()->IsPlayerInputEnabled())
        buttons = 0;
    if (buttons & 1) {
        const float t = static_cast<float>(timeDelta) * 0.001f;
        const float speed = DEBUG_CAMERA_SPEEDS[m_debugSpeed];
        m_wantedPos.x = t * m_wantedDir.x * speed + m_wantedPos.x;
        m_wantedPos.y = t * m_wantedDir.y * speed + m_wantedPos.y;
        m_wantedPos.z = t * m_wantedDir.z * speed + m_wantedPos.z;
    }
    if (buttons & 2) {
        const float t = static_cast<float>(timeDelta) * 0.001f;
        const float speed = DEBUG_CAMERA_SPEEDS[m_debugSpeed];
        m_wantedPos.x = m_wantedPos.x - t * m_wantedDir.x * speed;
        m_wantedPos.y = m_wantedPos.y - t * m_wantedDir.y * speed;
        m_wantedPos.z = m_wantedPos.z - t * m_wantedDir.z * speed;
    }
    // The speed action steps through the speeds.
    if (g_controls.GetValue(ACTION_DEBUG_SPEED, true) > 0.0f) {
        if (++m_debugSpeed > 9)
            m_debugSpeed = 9;
        GetGame()->Print("Debug camera speed %.2f", DEBUG_CAMERA_SPEEDS[m_debugSpeed]);
    } else if (g_controls.GetValue(ACTION_DEBUG_SPEED, true) < 0.0f) {
        if (--m_debugSpeed < 0)
            m_debugSpeed = 0;
        GetGame()->Print("Debug camera speed %.2f", DEBUG_CAMERA_SPEEDS[m_debugSpeed]);
    }
    m_gameCamera->SetPosInline(Xyz(m_wantedPos));
    m_gameCamera->SetWorldDir(Xyz(m_wantedDir), 0.0f);
    m_gameCamera->Update();
}

// --- the placement and the move (part 8e-iv) --------------------------------------------------------

namespace {

// (A constant 1 the distance's speed is scaled by; 0x0083926c.)
const float DISTANCE_SPEED_SCALE = 1.0f;

// The water band PlaceCamera keeps the camera out of (`inner`: 0.1 m; the push 0.3 m past it on
// the collision path, 0.1 m on the simple one).
float WaterPush(float height, float level, float radius, float push)
{
    if (height < level) {
        if ((level - 0.1f) - radius < height)
            return ((level - push) - radius) - height;
        return 0.0f;
    }
    const float edge = radius + level;
    if (!(0.1f + edge <= height))
        return (edge + push) - height;
    return 0.0f;
}

} // namespace

void C_CameraModule::PlaceCamera(int timeDelta)
{
    if (!m_gameCamera || !m_camera)
        return;
    S_CameraSettings *settings = m_viewSettings;
    float savedDistance = 0.0f;
    const float savedSpeed = settings->speed;
    if (m_cut)
        settings->speed = 1000.0f;
    if (m_blendTime > 0) {
        savedDistance = settings->distance;
        settings->distance = 0.0f;
    }
    S_vector &wanted = XyzRef(m_wantedDir);
    S_vector &dir = XyzRef(m_dir);

    // Turned towards the wanted direction (16 degree-ish units per second; 6 in view 7 after the
    // first frame). Near +z the turn is done 0.5 radians aside (the quaternions' blind spot).
    const double aside = std::fabs(S_vector(m_wantedDir.x, 0.0f, m_wantedDir.z).AngleTo(S_vector(0.0f, 0.0f, 1.0f)));
    if (aside < 0.08) {
        S_quat q;
        q.Make(S_vector(0.0f, 1.0f, 0.0f), 0.5f);
        wanted *= q.RotationMatrix();
        dir *= q.RotationMatrix();
    }
    dir.Normalize();
    S_quat from;
    from.SetDir(dir, 0.0f);
    wanted.Normalize();
    S_quat to;
    to.SetDir(wanted, 0.0f);
    const float turn = dir.AngleTo(wanted);
    if (m_view != 7)
        m_turnSpeed = 16.0f;
    const float t = static_cast<float>(timeDelta);
    const float step = t * m_turnSpeed * 0.0031415930f;
    m_turnSpeed = 6.0f;
    float ratio = 0.0f;
    if (turn != 0.0f) {
        ratio = step / turn;
        if (ratio > 1.0f)
            ratio = 1.0f;
    }
    S_vector4 look;
    {
        const S_vector d = from.Slerp(to, ratio, false).GetDir();
        look = {d.x, d.y, d.z, m_dir.w};               // (the w: a local's in the original)
    }
    if (aside < 0.08) {
        S_quat q;
        q.Make(S_vector(0.0f, 1.0f, 0.0f), -0.5f);
        wanted *= q.RotationMatrix();
        dir *= q.RotationMatrix();
        XyzRef(look) *= q.RotationMatrix();
    }
    XyzRef(look) = Xyz(look).NormalizedGame();

    if (!(settings->flags & CAMERA_SETTINGS_COLLIDE)) {
        // The plain placement: behind the dummy by the distance.
        if (m_view != 2) {
            m_waterOffset = 0.0f;
            if (m_underwater)
                m_waterOffset = WaterPush(m_camera->GetMatrix().m[3][1], m_waterLevel, settings->radius, 0.1f);
        }
        m_dir = look;
        const S_vector back(m_dir.x * settings->distance, m_dir.y * settings->distance, m_dir.z * settings->distance);
        const S_vector &at = m_camera->GetMatrix()(3);
        m_pos.x = (at.x + m_unknown050) - back.x;
        m_pos.y = (at.y + m_waterOffset) - back.y;
        m_pos.z = (at.z + m_unknown058) - back.z;
    } else {
        // Swept back from the dummy (the turret's arm in view 8) up to the distance + 1 m; the
        // distance eases in and out at the view's speed.
        if (m_view != 2) {
            m_waterOffset = 0.0f;
            if (m_underwater)
                m_waterOffset = WaterPush(m_camera->GetMatrix().m[3][1], m_waterLevel, settings->radius, 0.3f);
        }
        const S_vector &at = m_camera->GetMatrix()(3);
        S_vector start(m_unknown050 + at.x, at.y + m_waterOffset, at.z + m_unknown058);
        float distance = settings->distance;
        S_vector sweep(-look.x * (distance + 1.0f), -look.y * (distance + 1.0f), -look.z * (distance + 1.0f));
        const float radius = settings->radius;
        if (m_view == 8) {
            I3D_frame *arm = m_camera->GetParent();
            start = arm->GetMatrix()(3);
            distance = m_camera->GetPos().Magnitude();
            const S_vector &end = m_camera->GetMatrix()(3);
            const S_vector &pivot = arm->GetMatrix()(3);
            sweep = S_vector(end.x - pivot.x, end.y - pivot.y, end.z - pivot.z);
            NormalizeSummed(sweep, (sweep.z * sweep.z + sweep.y * sweep.y) + sweep.x * sweep.x);
            sweep *= distance + 1.0f;
        }
        C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
        C_Actor *vehicle = player ? player->GetVehicle() : nullptr;
        if (player)
            player->RemoveCollision(false);
        const uint32_t types = vehicle && vehicle->GetType() == ACTOR_CAR ? 0x98002u : 0x98000u;
        S_vector hitPos, normal;
        S_CollisionVolume *hit = CollisionSphereTest(start, sweep, radius, &hitPos, &normal, types, 0x200000);
        if (!hit && !(vehicle && vehicle->GetType() == ACTOR_CAR)) {
            hit = g_collisionWorld.RayTest(start, sweep, &hitPos, &normal, types, 0x200000);
            if (hit) {
                const float off = radius + 0.05f;
                hitPos = S_vector(normal.x * off + hitPos.x, hitPos.y + normal.y * off, hitPos.z + normal.z * off);
            }
        }
        // (The camera dummy's own model: its name begins with the dummy's.)
        if (hit && (hit->flags & COLLISION_FLAG_DYNAMIC) && hit->frame) {
            const char *name = hit->frame->GetName();
            const char *own = m_camera->GetName();
            if (std::strncmp(own, name, std::strlen(name)) == 0)
                hit = nullptr;
        }
        const float rate = t * settings->speed * DISTANCE_SPEED_SCALE * 0.001f;
        if (!hit) {
            if (m_distance > distance) {
                m_distance = m_distance - rate;
                if (m_distance < 0.0f)
                    m_distance = 0.0f;
            } else {
                m_distance = rate + m_distance;
                if (m_distance > distance)
                    m_distance = distance;
            }
            m_hit = nullptr;
            m_dir = look;
        } else {
            const S_vector gap(start.x - hitPos.x, start.y - hitPos.y, start.z - hitPos.z);
            float free = SummedLength((gap.z * gap.z + gap.y * gap.y) + gap.x * gap.x);
            if (free > distance)
                free = distance;
            if (m_underwater)
                free = m_camera->GetMatrix().m[3][1] < m_waterLevel ? free - 1.0f : free + 1.0f;
            if (!(radius + 0.05f <= free))
                free = 0.0f;
            if (!m_underwater && m_distance == settings->unknown04) {
                m_dir = look;
                m_hit = hit;
            } else if (!(free <= m_distance)) {
                m_dir = look;
                m_hit = hit;
                m_distance = rate + m_distance;
                if (m_distance > free)
                    m_distance = free;
                if (m_distance > distance)
                    m_distance = distance;
            } else if (m_hit) {
                m_dir = look;
                m_distance = free;
            } else {
                m_distance = m_distance - rate;
                if (m_distance < free - 0.01f)
                    m_distance = free - 0.01f;
                if (!m_underwater && m_distance < settings->unknown04)
                    m_hit = hit;
            }
        }
        if (player)
            player->RestoreCollision(false);

        if (!m_unknown278) {
            // (A GetGame() call when nearer than unknown04, its result unused.)
            const S_vector back(m_distance * m_dir.x, m_distance * m_dir.y, m_distance * m_dir.z);
            I3D_frame *base = m_view == 8 ? m_camera->GetParent() : m_camera;
            const S_vector &b = base->GetMatrix()(3);
            m_pos.x = (b.x + m_unknown050) - back.x;
            m_pos.y = (b.y + m_waterOffset) - back.y;
            m_pos.z = (b.z + m_unknown058) - back.z;
        } else {
            // Close enough: out of the first-person view.
            if (m_distance >= settings->unknown08) {
                C_FirstPersonView *fpv = GetGame()->GetFirstPersonView();
                if (fpv->IsShown() || fpv->IsBinocularsShown()) {
                    m_unknown278 = false;
                    GetGame()->GetFirstPersonView()->Show(false);
                }
            }
            const S_vector &b = m_camera->GetMatrix()(3);
            m_pos.x = b.x + m_unknown050;
            m_pos.y = b.y + m_waterOffset;
            m_pos.z = b.z + m_unknown058;
        }
    }

    // The blend: the distance 0 meanwhile (quirk kept: a negative blend time sets it 0).
    if (m_blendTime > 0) {
        settings->distance = savedDistance;
        m_blendTime -= timeDelta;
        if (m_blendTime < 0)
            m_blendTime = 0;
    }
    if (m_blendTime < 0) {
        settings->distance = savedDistance;
        m_blendTime += timeDelta;
        if (m_blendTime > 0)
            m_blendTime = 0;
    }
    if (m_cut) {
        settings->speed = savedSpeed;
        m_cut = false;
    }
}

namespace {

// The aiming sway's size (0x00839268): shrinks to 0.0015 while the soldier holds his breath,
// grows back to 0.008 otherwise; 0.002 in view mode 2.
float g_aimSway = 0.005f;

// An offset in the game camera's space, turned into the world's.
S_vector Turned(const S_vector4 &offset, I3D_frame *camera)
{
    S_vector v = Xyz(offset);
    v *= camera->GetMatrix();
    const S_vector &at = camera->GetMatrix()(3);
    return S_vector(v.x - at.x, v.y - at.y, v.z - at.z);
}

} // namespace

void C_CameraModule::UpdateShake(int timeDelta)
{
    if (m_shakeTime <= 0)
        return;
    m_shakeTime -= timeDelta;
    if (m_shakeTime < 0) {
        m_shakeTime = 0;
        m_shakePos.x = m_shakePos.y = m_shakePos.z = 0.0f;
        m_shakeDir.x = m_shakeDir.y = m_shakeDir.z = 0.0f;
        return;
    }
    const double left = static_cast<double>(m_shakeTime);
    const float a = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(0.07f * left))) *
                                       static_cast<double>(static_cast<float>(left * 6e-05f)));
    const float b = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.08f))) *
                                       static_cast<double>(static_cast<float>(left * 4e-05f)));
    const float c = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.05f))) *
                                       static_cast<double>(static_cast<float>(left * 7e-05f)));
    // (Their w: a local's in the original; kept here.)
    m_shakePos.x = a;
    m_shakePos.y = b;
    m_shakePos.z = c;
    m_shakeDir.x = c * 0.15f;
    m_shakeDir.y = b * -0.25f;
    m_shakeDir.z = a * 0.15f;
}

void C_CameraModule::MoveCamera(int timeDelta)
{
    if (!m_gameCamera || GetGame()->IsPaused())
        return;
    UpdateShake(timeDelta);
    if (!m_debugCamera) {
        // The place, shaken and offset (the turret's view places itself below).
        const S_vector off = Turned(m_offsetPos, m_gameCamera);
        if (m_view != 8)
            m_gameCamera->SetPosInline(S_vector((m_pos.x + m_shakePos.x) + off.x, off.y + (m_shakePos.y + m_pos.y),
                                                off.z + (m_shakePos.z + m_pos.z)));
        if (m_target) {
            TurnToTarget(timeDelta);
        } else if (!m_aiming) {
            const S_vector turn = Turned(m_offsetDir, m_gameCamera);
            if (m_view != 8)
                m_gameCamera->SetWorldDir(S_vector((m_dir.x + m_shakeDir.x) + turn.x, turn.y + (m_shakeDir.y + m_dir.y),
                                                   turn.z + (m_shakeDir.z + m_dir.z)),
                                          0.0f);
        } else {
            // Aiming: the view sways (less while the breath is held, more with less skill).
            S_vector base((m_dir.x + m_shakeDir.x) + m_offsetDir.x, (m_shakeDir.y + m_dir.y) + m_offsetDir.y,
                          (m_shakeDir.z + m_dir.z) + m_offsetDir.z);
            S_vector side(base.y * 0.0f - base.z, base.z * 0.0f - base.x * 0.0f, base.x - base.y * 0.0f);
            S_vector up(side.z * base.y - side.y * base.z, base.z * side.x - side.z * base.x,
                        side.y * base.x - base.y * side.x);
            NormalizeSummed(up, (up.z * up.z + up.y * up.y) + up.x * up.x);
            NormalizeSummed(side, (side.z * side.z + side.y * side.y) + side.x * side.x);
            float amplitude = 0.006f;
            if (C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer()) {
                if (player->GetPlayerViewMode() == 2) {
                    g_aimSway = 0.002f;
                } else if (player->IsPlayerHoldingBreath()) {
                    if (g_aimSway > 0.0015f) {
                        g_aimSway = static_cast<float>(g_aimSway - static_cast<float>(static_cast<double>(timeDelta) * 5e-06));
                        if (g_aimSway < 0.0015f)
                            g_aimSway = 0.0015f;
                    }
                } else if (g_aimSway < 0.008f) {
                    g_aimSway = static_cast<float>(static_cast<double>(timeDelta) * 4e-06) + g_aimSway;
                    if (g_aimSway > 0.008f)
                        g_aimSway = 0.008f;
                }
                amplitude = (2.0f - player->GetStatFactor(4)) * g_aimSway;
            }
            const double time = static_cast<double>(m_time);
            const float swayUp = static_cast<float>(
                std::sin(std::cos(static_cast<double>(static_cast<float>(time * 0.0018f)))) * amplitude);
            up *= swayUp;
            const float phase = static_cast<float>(time * 0.0008f) + 3.0f;
            const float wave = static_cast<float>(std::sin(static_cast<double>(phase)) + 1.0);
            const float swaySide = static_cast<float>(std::cos(static_cast<double>(wave)) * amplitude);
            side *= swaySide;
            base = S_vector(base.x + up.x, up.y + base.y, up.z + base.z);
            const S_vector final = S_vector(base.x + side.x, base.y + side.y, base.z + side.z).NormalizedGame();
            m_gameCamera->SetWorldDir(final, 0.0f);
        }

        if (m_view == 8 && m_camera)
            PlaceTurretCamera();
        m_gameCamera->Update();
    }

    // The soldier the camera dummy belongs to fades out as the camera nears his body (0.1 per
    // frame towards 1 within 0.2 m^2 of the segment from his body frame to his model, 0 past 1.2).
    if (!m_camera)
        return;
    C_FirstPersonView *fpv = GetGame()->GetFirstPersonView();
    if (fpv->IsShown() || fpv->IsBinocularsShown())
        return;
    C_Human *human = nullptr;
    I3D_frame *model = m_camera->GetOwnerModel();
    if (model && model->GetType() == FRAME_MODEL) {
        human = GetFrameActorAs<C_Human>(model, ACTOR_HUMAN);
        if (human) {
            const S_vector4 body = WorldPos(human->GetBodyFrame());
            const S_vector &top = model->GetMatrix()(3);
            const S_vector head(top.x, top.y + 0.1f, top.z);
            const S_vector4 camera = WorldPos(m_gameCamera);
            float fade = 0.0f;
            if (m_gameCamera == GetGame()->GetScene()->GetActiveCamera()) {
                float t;
                const float distance2 = SegmentDistance2(Xyz(camera), Xyz(body), head, &t);
                if (!(distance2 > 1.2f))
                    fade = distance2 < 0.2f ? 1.0f : 1.0f - (distance2 - 0.2f);
            }
            if (fade != m_fadeTarget)
                m_fadeTarget = fade;
        }
    }
    if (m_fadeTarget != m_fade) {
        if (m_fadeTarget > m_fade) {
            m_fade = m_fade + 0.1f;
            if (m_fade > m_fadeTarget)
                m_fade = m_fadeTarget;
        } else {
            m_fade = m_fade - 0.1f;
            if (m_fade < m_fadeTarget)
                m_fade = m_fadeTarget;
        }
        if (human)
            human->SetUnknown94(m_fade);
    }
}

void C_CameraModule::TurnToTarget(int timeDelta)
{
    // The scene's active camera turns to the target (the game camera's ScriptCamera target; at
    // once with a speed of 0 or less, else at the speed's radians per second, then at once).
    if (!GetGame() || !GetGame()->GetScene())
        return;
    I3D_camera *active = GetGame()->GetScene()->GetActiveCamera();
    if (!active)
        return;
    const S_vector &from = active->GetMatrix()(3);
    const S_vector &to = m_target->GetMatrix()(3);
    S_vector way(to.x - from.x, to.y - from.y, to.z - from.z);
    way.Normalize();
    if (m_targetSpeed <= 0.0f) {
        active->SetDir(way, 0.0f);
        active->Update();
        return;
    }
    const S_vector view = active->GetMatrix()(2).NormalizedGame();
    S_quat a, b;
    a.SetDir(view, 0.0f);
    b.SetDir(way, 0.0f);
    const float angle = view.AngleTo(way);
    float ratio = static_cast<float>(timeDelta) * m_targetSpeed * 0.001f / angle;
    if (ratio > 1.0f) {
        ratio = 1.0f;
        m_targetSpeed = -1.0f;
    }
    active->SetDir(a.Slerp(b, ratio, false).GetDir().NormalizedGame(), 0.0f);
    active->Update();
}

void C_CameraModule::PlaceTurretCamera()
{
    // The turret's view (8): in first person (or a seat that only has it) the game camera at the
    // camera dummy, turned as it; else between the dummy and its pivot, the distance back from
    // the dummy towards the pivot.
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    C_FirstPersonView *fpv = GetGame()->GetFirstPersonView();
    if (fpv->IsShown() || fpv->IsBinocularsShown() ||
        (player && player->GetVehicle() && player->GetVehicle()->IsSeatFirstPersonOnly(player->GetVehicleSeat()))) {
        m_gameCamera->SetPos(m_camera->GetMatrix()(3));
        S_quat rot;
        rot.Make(m_camera->GetMatrix());
        m_gameCamera->SetRot(rot);
    } else {
        const float arm = m_camera->GetPos().Magnitude();
        const S_vector &at = m_camera->GetMatrix()(3);
        const S_vector &pivot = m_camera->GetParent()->GetMatrix()(3);
        S_vector way(pivot.x - at.x, pivot.y - at.y, pivot.z - at.z);
        NormalizeSummed(way, (way.x * way.x + way.z * way.z) + way.y * way.y);
        const float along = arm - m_distance;
        const S_vector off(way.x * along, way.y * along, way.z * along);
        const S_vector &dummy = m_camera->GetMatrix()(3);
        m_gameCamera->SetPosInline(S_vector(off.x + dummy.x, off.y + dummy.y, off.z + dummy.z));
        m_gameCamera->SetWorldDir(m_camera->GetMatrix()(2).NormalizedGame(), 0.0f);
    }
    // The seat's own view shown (a seat that only has the first-person one).
    if (player) {
        I3D_frame *model = m_camera->GetOwnerModel();
        if (model && player->GetVehicle() && model->GetData() && model->GetData()->actor) {
            C_Actor *owner = model->GetData()->actor;
            if (owner->IsSeatFirstPersonOnly(player->GetVehicleSeat()))
                owner->ShowSeatView(player->GetVehicleSeat(), true);
        }
    }
}

// --- the view, the special modes (part 8e-iv) --------------------------------------------------------

namespace {

// The first-person view (or the binoculars) is up. (The original asks the binoculars again on a
// client: the same.)
bool InFirstPerson()
{
    C_FirstPersonView *fpv = GetGame()->GetFirstPersonView();
    return fpv->IsShown() || fpv->IsBinocularsShown();
}

} // namespace

void C_CameraModule::SelectView(int)
{
    if (m_debugCamera || !m_gameCamera)
        return;
    I3D_frame *old = m_camera;
    C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
    if (player) {
        if (C_Actor *vehicle = player->GetVehicle()) {
            // In a seat: its camera dummy (nothing changes without one).
            const int seat = player->GetVehicleSeat();
            m_camera = vehicle->GetSeatCamera(seat);
            const uint32_t flags = vehicle->GetSeatFlags(seat);
            if (!m_camera)
                return;
            if (!InFirstPerson()) {
                if (vehicle->IsSeatFirstPersonOnly(seat))
                    vehicle->ShowSeatView(seat, true);
            } else {
                m_blendTime = 0;
            }
            if (flags & SEAT_FLAG_WEAPON) {
                // A seat weapon: the turret's view (8); the Junkers' gunner's own view (10) in
                // third person, with the game's field of view (the gun's in first person). An
                // AI-held Junkers gives the control back.
                if (vehicle->GetType() == ACTOR_JUNKERS88 && player->GetControllerType() != 5 && !player->IsLocalPlayer())
                    g_teamManager.GetPlayerTeam()->ReturnControl();
                const bool firstPerson = InFirstPerson();
                if (vehicle->GetType() == ACTOR_JUNKERS88) {
                    if (!firstPerson) {
                        m_view = 10;
                        m_gameCamera->SetFOV(GetGame()->GetCutsceneFOV());
                    } else {
                        m_view = 8;
                        m_gameCamera->SetFOV(static_cast<C_Junkers88 *>(vehicle)->GetGunFov());
                    }
                } else {
                    m_view = 8;
                }
            } else if (!(flags & SEAT_FLAG_ARMED)) {
                // A passenger: view 7 (a car's seat with its own camera keeper: as far as the
                // car's camera offset), 9 in first person.
                if (!InFirstPerson()) {
                    m_view = 7;
                    if (vehicle->GetType() == ACTOR_CAR) {
                        C_Car *car = static_cast<C_Car *>(vehicle);
                        if (car->HasSeatCamera(seat)) {
                            S_vector offset;
                            car->GetCameraOffset(&offset);
                            m_settings[7].distance = offset.y;
                        } else {
                            m_settings[m_view].distance = m_unknown2AC;
                        }
                    }
                } else {
                    m_view = 9;
                }
                m_carPitch = m_carYaw = 0.0f;
            } else if (!InFirstPerson()) {
                // Armed in the seat: on foot's view (1).
                m_view = 1;
                m_settings[1].distance = m_unknown2AC;
            } else {
                m_view = 2;
            }
        } else if (!InFirstPerson()) {
            // On foot: behind the soldier ("camera3RD*"), view 1 outdoors (or "nointerier"
            // sectors), 3 indoors, 4 in "interierclose" sectors; under water 5 / 6.
            m_blendTime = -1;
            m_camera = player->GetFrame()->FindChildFrame("camera3RD*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
            I3D_sector *sector;
            if (player->GetMovement())
                sector = player->GetBodySector();
            else
                sector = GetGame()->GetScene()->GetSector(player->GetFrame()->GetMatrix()(3));
            I3D_sector *primary = GetGame()->GetScene()->GetPrimarySector();
            bool outdoor = false, close = false;
            if (sector) {
                const char *props = sector->GetUserProps();
                if (std::strstr(props, "nointerier") || std::strstr(props, "NOINTERIER"))
                    outdoor = true;
                if (std::strstr(props, "interierclose") || std::strstr(props, "INTERIERCLOSE"))
                    close = true;
            }
            if (!m_underwater) {
                if (sector == primary || outdoor) {
                    m_view = 1;
                    m_settings[1].distance = m_unknown2AC;
                } else {
                    m_view = close ? 4 : 3;
                }
            } else {
                m_view = sector == primary || outdoor ? 5 : 6;
            }
        } else {
            m_camera = player->GetFrame()->FindChildFrame("cameraFPV*", ENUMF_WILDMASK | FRAME_MASK_DUMMY);
            m_view = 2;
        }
        m_viewSettings = &m_settings[m_view];
        if (!m_camera)
            m_camera = player->GetFrame();
    }

    if (old != m_camera) {
        // A new camera dummy: held (the old one let go, its soldier no longer faded); the view
        // and the place start from it.
        m_fadeTarget = m_fade = 0.0f;
        if (old) {
            I3D_frame *model = old->GetOwnerModel();
            if (model && model->GetType() == FRAME_MODEL) {
                if (C_Human *human = GetFrameActorAs<C_Human>(model, ACTOR_HUMAN))
                    human->SetUnknown94(0.0f);
            }
            old->Release();
        }
        if (m_camera)
            m_camera->AddRef();
        m_wantedDir = WorldDir(m_camera);
        const S_vector back(m_wantedDir.x * m_viewSettings->distance, m_wantedDir.y * m_viewSettings->distance,
                            m_wantedDir.z * m_viewSettings->distance);
        const S_vector &at = m_camera->GetMatrix()(3);
        // (The w: a local's in the original; kept here.)
        m_pos.x = at.x - back.x;
        m_pos.y = at.y - back.y;
        m_pos.z = at.z - back.z;
        m_wantedPos = m_pos;
        m_distance = m_viewSettings->distance;
        m_hit = nullptr;
        m_cut = true;
    }
    if (m_blendTime < 0) {
        m_blendTime = 0;
        m_distance = m_viewSettings->unknown08;
    }
}

void C_CameraModule::SetSpecialMode(int mode, bool on, I3D_frame *target)
{
    SpecialMode(mode, on, target != nullptr, target, nullptr, 0.0f);
}

void C_CameraModule::SetSpecialMode(int mode, bool on, int value)
{
    SpecialMode(mode, on, value != 0, nullptr, nullptr, 0.0f);
}

void C_CameraModule::SetSpecialMode(int mode, bool on, float height)
{
    uint32_t bits;
    std::memcpy(&bits, &height, sizeof bits);
    SpecialMode(mode, on, bits != 0, nullptr, nullptr, height);
}

void C_CameraModule::SetSpecialMode(int mode, const S_vector4 *vector, uint32_t value)
{
    SpecialMode(mode, vector != nullptr, value != 0, nullptr, vector, static_cast<float>(value));
}

void C_CameraModule::SpecialMode(int mode, bool on, bool value, I3D_frame *frame, const S_vector4 *vector, float number)
{
    switch (mode) {
    case CAMERA_MODE_DEBUG: {
        // The free debug camera (on: from the active camera's place); the spectators' camera kept.
        m_debugCamera = on ? value : !m_debugCamera;
        I3D_camera *active = GetGame()->GetScene()->GetActiveCamera();
        if (m_debugCamera && active)
            m_wantedPos = WorldPos(active);
        if (m_spectating) {
            if (!m_debugCamera) {
                GetGame()->GetScene()->SetActiveCamera(m_gameCamera);
                m_spectating = false;
            } else if (I3D_frame *spectator = GetGame()->GetScene()->FindFrame("spectator", FRAME_MASK_CAMERA)) {
                GetGame()->GetScene()->SetActiveCamera(static_cast<I3D_camera *>(spectator));
            }
        }
        if (!m_debugCamera)
            SelectView(0);
        break;
    }
    case CAMERA_MODE_EXPLOSION: {
        // A client: the shake (2 s) when the game camera is within `value` metres (2.5 times
        // that in the scripted view).
        if (!(g_gameRole & GAME_ROLE_CLIENT))
            break;
        float radius = number;
        if (m_scriptedView)
            radius = radius * 2.5f;
        const S_vector &at = m_gameCamera->GetMatrix()(3);
        const S_vector way(vector->x - at.x, vector->y - at.y, vector->z - at.z);
        if (SummedLength((way.y * way.y + way.x * way.x) + way.z * way.z) < radius)
            m_shakeTime = 2000;
        break;
    }
    case CAMERA_MODE_SPECTATOR: {
        // A spectator's camera ("spectator<n>", one at random); off: the game camera back.
        if ((g_gameRole & 7) == 7)
            break;
        m_spectating = on ? value : !m_spectating;
        if (!m_spectating) {
            GetGame()->GetScene()->SetActiveCamera(m_gameCamera);
            break;
        }
        int count = 0;
        while (GetGame()->GetScene()->FindFrame(C_fstr("spectator%d", count), FRAME_MASK_CAMERA))
            ++count;
        const int roll = std::rand();
        m_spectator = static_cast<int>(std::floor(static_cast<double>(static_cast<float>(
            static_cast<float>(roll) * 3.051851e-05f * static_cast<float>(count)))));
        if (I3D_frame *spectator = GetGame()->GetScene()->FindFrame(C_fstr("spectator%d", m_spectator), FRAME_MASK_CAMERA))
            GetGame()->GetScene()->SetActiveCamera(static_cast<I3D_camera *>(spectator));
        break;
    }
    case CAMERA_MODE_MOUSE_LOOK:
        m_mouseLook = on ? value : !m_mouseLook;
        break;
    case CAMERA_MODE_UNDERWATER:
        m_underwater = on;
        m_waterLevel = number;
        C_HumanMovement::SetCameraUnderWater(on);
        SelectView(0);
        break;
    case CAMERA_MODE_KICK_DIR: {
        // A kick of the view direction (at most 0.25), shrinking after 200 ms.
        if (m_aiming)
            break;
        S_vector &v = XyzRef(m_offsetDir);
        v = S_vector(vector->x + v.x, vector->y + v.y, vector->z + v.z);
        const float square = v.Square();
        if (!(square < 1e-8f) && !(std::fabs(square - 1.0f) >= 1e-8f && !(std::sqrt(square) > 0.25f))) {
            v.Normalize();
            v *= 0.25f;
        }
        m_offsetDirDelay = 200;
        break;
    }
    case CAMERA_MODE_KICK_POS: {
        // A kick of the position (at most 1 m), shrinking after 200 ms.
        if (m_aiming)
            break;
        S_vector &v = XyzRef(m_offsetPos);
        v = S_vector(vector->x + v.x, vector->y + v.y, vector->z + v.z);
        const float square = v.Square();
        if (!(square < 1e-8f) && std::fabs(square - 1.0f) >= 1e-8f && std::sqrt(square) > 1.0f)
            v.Normalize();
        m_offsetPosDelay = 200;
        break;
    }
    case CAMERA_MODE_AIM:
        m_aimTime = 0;
        m_aiming = on;
        break;
    case CAMERA_MODE_NEXT_SPECTATOR: {
        // The next spectator's camera (back to the first after the last).
        if (!m_spectating)
            break;
        ++m_spectator;
        for (;;) {
            if (I3D_frame *spectator = GetGame()->GetScene()->FindFrame(C_fstr("spectator%d", m_spectator), FRAME_MASK_CAMERA)) {
                GetGame()->GetScene()->SetActiveCamera(static_cast<I3D_camera *>(spectator));
                break;
            }
            if (m_spectator == 0)
                break;
            m_spectator = 0;
        }
        break;
    }
    case CAMERA_MODE_PROJECTILE: {
        // The scripted view behind `frame` (held); off without one.
        I3D_frame *old = m_scriptedView;
        m_scriptedViewOn = on;
        m_scriptedView = frame;
        if (frame)
            frame->AddRef();
        if (old)
            old->Release();
        if (!m_scriptedView && m_scriptedViewOn)
            m_scriptedViewOn = false;
        break;
    }
    default:
        break;
    }
}
