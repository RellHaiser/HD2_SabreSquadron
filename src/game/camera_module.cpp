// The camera module (0x00462320..0x0046cb20): see game/camera_module.h. This file: the module's
// core (part 8e-i: 0x00464d90..0x004668c0) and the human effects, the command map's tactical
// camera (part 8e-ii: 0x00462320..0x00464d77).
#include "game/camera_module.h"

#include <cmath>
#include <cstddef>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/car.h"
#include "game/checkpoints.h"
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
        g_cameraModule.Unknown40(0);
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
