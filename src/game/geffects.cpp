// The graphic effects module (see geffects.h).
#include "game/geffects.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/actor.h"
#include "game/air_attack.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/interface.h"
#include "game/particles.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

C_GEffectsModule g_geffectsModule(MODULE_GEFFECTS, "GEffects");   // 0x0089b880
float g_waterLevel = 0.0f;                              // 0x0089b760
int g_rainTime = 0;                                     // 0x0089bbb4
int g_rainTeamSteps = 3;                                // 0x008396e8

namespace {

// The rain's particle effects: a drop on the ground, a ring on the water.
constexpr int EFFECT_RAIN_DROP = 0x2C;
constexpr int EFFECT_RAIN_RING = 0x2D;

// The fade's quad (pre-transformed; z, rhw and the texture coordinates stay 0).
S_TLVertex g_fadeQuad[4];                               // 0x0089b7f8

// SetHierarchyOn's mask bit of a frame type (0: never switched; the models' and user frames'
// bits are the other way round from the search masks').
uint32_t HierarchyTypeBit(int type)
{
    switch (type) {
    case 1: return 0x1;
    case 2: return 0x2;
    case 3: return 0x4;
    case 4: return 0x8;
    case 5: return 0x10;
    case 6: return 0x20;
    case 7: return 0x40;
    case 8: return 0x100;
    case 9: return 0x80;
    case 10: return 0x400;
    case 11: return 0x200;
    case 12: return 0x800;
    case 14: return 0x1000;
    case 15: return 0x2000;
    case 16: return 0x4000;
    default: return 0;
    }
}

// The visual types the hierarchy fades act on.
bool IsFadedVisual(uint32_t type)
{
    switch (type) {
    case I3DVISUAL_LMAP:
    case I3DVISUAL_BILLBOARD:
    case I3DVISUAL_MORPH:
    case I3DVISUAL_SINGLEMESH:
    case I3DVISUAL_SINGLEMORPH:
    case I3DVISUAL_MIRROR:
    case I3DVISUAL_OBJECT:
        return true;
    default:
        return false;
    }
}

// The camera's own place and rotation from the model's camera frame.
void FollowFrame(I3D_frame *camera, I3D_frame *frame)
{
    camera->SetPosInline(frame->GetWorldPos());
    S_quat rot;
    rot.Make(frame->GetMatrix());
    camera->SetRot(rot);
    camera->Update();
}

}  // namespace

C_GEffectsModule *GetGEffects()
{
    return &g_geffectsModule;
}

// 0x00485fd0
void S_CameraAnimation::Restore()
{
    camera->LinkTo(parent, 0);
    camera->SetPosInline(pos);
    camera->SetRot(rot);
    camera->Update();
    camera->Release();
    model->Release();
}

// 0x004860b0
C_GEffectsModule::C_GEffectsModule(int id, const char *name) : C_Module(id, name)
{
    g_decorators = &m_decorators;
    m_started = false;
    Reset();
}

// 0x00486f00
C_GEffectsModule::~C_GEffectsModule()
{
    Reset();
}

// 0x00488b80
void C_GEffectsModule::Reset()
{
    m_timeScale = 1.0f;
    m_timeScaleTarget = 1.0f;
    m_timeScaleRate = 1.0f;
    for (size_t i = m_fadeOuts.size(); i-- > 0;) {
        if (S_ModelFade *fade = m_fadeOuts[i]) {
            if (fade->model)
                fade->model->Release();
            delete fade;
        }
    }
    m_fadeOuts.clear();
    for (size_t i = m_fadeIns.size(); i-- > 0;) {
        if (S_ModelFade *fade = m_fadeIns[i]) {
            if (fade->model)
                fade->model->Release();
            delete fade;
        }
    }
    m_fadeIns.clear();
    m_humanFades.clear();
    m_unknown220 = false;
    m_sun = nullptr;
    std::memset(m_unknown258 + 4, 0, sizeof(m_unknown258) - 4);   // (+0x25C..+0x2BB)
    for (size_t i = m_cameraAnimations.size(); i-- > 0;) {
        if (S_CameraAnimation *animation = m_cameraAnimations[i]) {
            animation->Restore();
            delete animation;
        }
    }
    m_cameraAnimations.clear();
}

// 0x00487240
void C_GEffectsModule::Update(int timeDelta)
{
    // The cameras follow their animations' camera frames (the last key held at the end).
    for (size_t i = m_cameraAnimations.size(); i-- > 0;) {
        S_CameraAnimation *animation = m_cameraAnimations[i];
        if (!animation || !animation->model || !animation->model->GetAnimation(0))
            continue;
        I3D_model *model = animation->model;
        const int end = model->GetAnimation(0)->GetEndTime();
        const int time = model->GetAnimTime(0);
        if (model->GetAnimTime(0) + timeDelta < end)
            model->Tick(timeDelta);
        else
            model->Tick(end - time - 1);
        model->Update();
        // Fix: a model without a camera frame (no "*cam*", no child) is not followed (the
        // original reads the null frame).
        if (animation->cameraFrame)
            FollowFrame(animation->camera, animation->cameraFrame);
    }

    // The lightning flickers at random up to the light's own power.
    if (m_lightning) {
        const float power = RandFloat() * m_lightningPower;
        if (power <= 0.5f) {
            if (m_lightning->IsOn()) {
                m_lightning->SetOn(false);
                m_lightning->Update();
            }
        } else {
            if (!m_lightning->IsOn())
                m_lightning->SetOn(true);
            m_lightning->SetPower(power);
            m_lightning->Update();
        }
        if (m_lightningStop)
            SetThunderstorm(false, nullptr, true);
        if (m_lightningTime > 0 && (m_lightningTime -= timeDelta) < 1)
            m_lightningStop = true;
    }

    // The screen fade (-1 once it is past its target).
    if (m_fadeLevel < 0.0f) {
        m_fadeTarget = 0.0f;
        m_fadeLevel = 0.0f;
    }
    if (m_fadeTarget != m_fadeLevel) {
        const float step = static_cast<float>(timeDelta) * m_fadeRate * 0.001f;
        if (m_fadeTarget <= m_fadeLevel) {
            m_fadeLevel -= step;
            if (m_fadeLevel < m_fadeTarget)
                m_fadeLevel = -1.0f;
        } else {
            m_fadeLevel = step + m_fadeLevel;
            if (m_fadeTarget < m_fadeLevel)
                m_fadeLevel = -1.0f;
        }
    }
    m_time += timeDelta;

    // The underwater view.
    C_game *game = GetGame();
    I3D_scene *scene = game->GetScene();
    if (game->IsMissionRunning() && m_waterCamera.enabled && g_teamManager.GetPlayerTeam()->GetActivePlayer()) {
        if (m_waterCamera.underwaterNow && m_waterCamera.fovWobble) {
            I3D_camera *camera = scene->GetActiveCamera();
            if (GetCameraModule()->IsGameCamera(camera)) {
                const float time = static_cast<float>(static_cast<uint32_t>(m_time));
                camera->SetFOV(std::sin(0.001f * time) * 0.008f + std::cos(time * 0.002f) * 0.01f +
                               m_waterCamera.fov * 0.0174533f);
                camera->Update();
            }
        }
        C_Human *player = g_teamManager.GetPlayerTeam()->GetActivePlayer();
        bool headUnderWater = false;
        player->GetMovement()->GetHeadWater(&headUnderWater, &g_waterLevel);
        const C_Actor *vehicle = player->GetVehicle();
        if (headUnderWater || (vehicle && vehicle->GetType() == ACTOR_SUBMARINE) || m_waterCamera.underwaterNow) {
            I3D_camera *camera = scene->GetActiveCamera();
            GetCameraModule()->IsGameCamera(camera);    // (the answer is not used)
            const bool otherCamera = m_waterCameraFrame && m_waterCameraFrame != camera;
            const float height = camera->GetWorldPos().y;
            if (height < g_waterLevel && (!m_waterCamera.underwaterNow || otherCamera)) {
                RestoreCamera();
                TakeCamera(camera);
                GetCameraModule()->SetSpecialMode(CAMERA_MODE_UNDERWATER, true, g_waterLevel);
                m_waterCamera.underwaterNow = true;
                camera->SetFOV(m_waterCamera.fov * 0.0174533f);
                camera->SetRange(m_waterCamera.nearRange, m_waterCamera.farRange);
                camera->Update();
                ShowAboveWater(false);
                ShowUnderWater(true);
                ShowBackdrop(false);
                SwitchSounds(true);
                if (m_waterCamera.weather)
                    scene->SetWeather(WEATHER_ON, 1);
            } else if (height >= g_waterLevel && (m_waterCamera.underwaterNow || otherCamera)) {
                // (Quirk kept: the camera is taken again above water, its fov and range kept.)
                RestoreCamera();
                TakeCamera(camera);
                GetCameraModule()->SetSpecialMode(CAMERA_MODE_UNDERWATER, false, g_waterLevel);
                m_waterCamera.underwaterNow = false;
                ShowUnderWater(false);
                ShowAboveWater(true);
                ShowBackdrop(true);
                SwitchSounds(false);
                if (m_waterCamera.weather)
                    scene->SetWeather(WEATHER_ON, 0);
            }
        }
    }

    // The visuals fading out and in (a finished one swapped with the last).
    for (size_t i = m_fadeOuts.size(); i-- > 0;) {
        S_ModelFade *fade = m_fadeOuts[i];
        fade->timeLeft -= timeDelta;
        if (fade->timeLeft < 1) {
            SetModelAlpha(fade->model, 1.0f);
            if (fade->switchOff)
                fade->model->SetOn(false);
            if (fade->model)
                fade->model->Release();
            delete fade;
            m_fadeOuts[i] = m_fadeOuts.back();
            m_fadeOuts.pop_back();
        } else {
            float level = 0.0f;
            if (fade->time != 0)
                level = 1.0f - static_cast<float>(fade->timeLeft) / static_cast<float>(fade->time);
            SetModelAlpha(fade->model, level);
        }
    }
    for (size_t i = m_fadeIns.size(); i-- > 0;) {
        S_ModelFade *fade = m_fadeIns[i];
        fade->timeLeft -= timeDelta;
        if (fade->timeLeft < 1) {
            SetModelAlpha(fade->model, 0.0f);
            if (fade->model)
                fade->model->Release();
            delete fade;
            m_fadeIns[i] = m_fadeIns.back();
            m_fadeIns.pop_back();
        } else {
            float level = 1.0f;
            if (fade->time != 0)
                level = static_cast<float>(fade->timeLeft) / static_cast<float>(fade->time);
            SetModelAlpha(fade->model, level);
        }
    }
    // The humans fading.
    for (size_t i = m_humanFades.size(); i-- > 0;) {
        S_HumanFade &fade = m_humanFades[i];
        const float step = static_cast<float>(timeDelta) * fade.rate;
        I3D_frame *model = fade.human->GetFrame();
        if (!fade.in) {
            fade.alpha -= step;
            if (fade.alpha <= 0.0f) {
                model->SetHierarchyAlpha(0.0f);
                m_humanFades.erase(m_humanFades.begin() + i);
                continue;
            }
        } else {
            fade.alpha = step + fade.alpha;
            if (!(fade.alpha < 1.0f)) {
                model->SetHierarchyAlpha(1.0f);
                m_humanFades.erase(m_humanFades.begin() + i);
                continue;
            }
        }
        model->SetHierarchyAlpha(fade.alpha);
    }

    // The sun keeps its place relative to the camera.
    if (m_sun) {
        if (I3D_camera *camera = scene->GetActiveCamera()) {
            const S_vector &eye = camera->GetWorldPos();
            m_sun->SetPosInline(S_vector(eye.x + m_sunPos.x, eye.y + m_sunPos.y, eye.z + m_sunPos.z));
            m_sun->Update();
        }
    }
    UpdateBirds(timeDelta);
    // Rain also while the command map has the game paused.
    if (!game->IsPaused() || (game->IsMissionRunning() && game->GetInterface()->IsCommandMapOpen()))
        UpdateRain(timeDelta);
    if (m_started)
        m_decorators.Update(timeDelta);

    // The time scale moves towards its target.
    if (m_timeScale != m_timeScaleTarget) {
        const bool down = m_timeScaleTarget <= m_timeScale;
        float step = static_cast<float>(timeDelta) * m_timeScaleRate * 0.001f;
        if (down)
            step = -step;
        m_timeScale = step + m_timeScale;
        if (down) {
            if (m_timeScale < m_timeScaleTarget)
                m_timeScale = m_timeScaleTarget;
        } else if (m_timeScaleTarget < m_timeScale) {
            m_timeScale = m_timeScaleTarget;
        }
    }
    m_meshPlayer.Update(timeDelta);
}

// (Part of 0x00487240.)
void C_GEffectsModule::UpdateRain(int timeDelta)
{
    if (!m_rain)
        return;
    g_rainTime += timeDelta;
    if (g_rainTime < 100)
        return;
    // Fix: the direction's fourth float is 0 (left unset in the original; not read).
    const S_vector4 dir = {0.0f, -200.0f, 0.0f, 0.0f};
    const S_matrix &view = GetGame()->GetScene()->GetActiveCamera()->GetMatrix();
    const S_vector eye = view(3);
    const float eyeW = view.m[3][3];
    for (; g_rainTime >= 100; g_rainTime -= 100) {
        const int drops = static_cast<int>(static_cast<float>(m_rainRate) * 0.1f);
        const float top = eye.y + 100.0f;
        for (int i = drops; i != 0; --i) {
            S_vector4 from = {eye.x, top, eye.z, eyeW};
            from.z = RandomFloat(m_rainArea + m_rainArea) - m_rainArea + from.z;
            from.x = RandomFloat(m_rainArea + m_rainArea) - m_rainArea + from.x;
            S_vector4 hit;
            const S_CollisionVolume *volume = RayTestPastBorders(from, dir, &hit, nullptr, 0xFFFFFFFF, 0);
            if (!volume)
                continue;
            const S_vector up(0.0f, 1.0f, 0.0f);
            if (volume->flags & COLLISION_FLAG_WATER) {
                const S_vector pos(hit.x, hit.y + 0.02f, hit.z);
                g_particlesModule.CreateAt(EFFECT_RAIN_RING, pos, up, nullptr, false, false);
            } else {
                g_particlesModule.CreateAt(EFFECT_RAIN_DROP, S_vector(hit.x, hit.y, hit.z), up, nullptr, true, false);
            }
        }
        if (--g_rainTeamSteps == 0) {
            g_rainTeamSteps = 3;
            RainOnTeam();
        }
    }
}

// (Part of 0x00487240.)
void C_GEffectsModule::RainOnTeam()
{
    I3D_scene *scene = GetGame()->GetScene();
    I3D_sector *primary = scene->GetPrimarySector();
    const S_vector eye = scene->GetActiveCamera()->GetWorldPos();
    const int stamp = scene->GetRenderStamp();
    const int boxCount = scene->NumDrawnDummies();
    const S_dummyBox *boxes = scene->GetDrawnDummies();
    C_Team *team = g_teamManager.GetPlayerTeam();
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *member = team->GetMember(i);
        if (member->GetBodySector() != primary)
            continue;
        const S_vector &pos = member->GetFrame()->GetWorldPos();
        const float dx = pos.x - eye.x;
        const float dy = pos.y - eye.y;
        const float dz = pos.z - eye.z;
        if ((dz * dz + dy * dy) + dx * dx > 100.0f || stamp != member->GetFrame()->GetRenderStamp())
            continue;
        const S_vector body = member->GetBodyFrame()->GetWorldPos();
        bool covered = false;
        for (int b = boxCount; b-- > 0;) {
            const S_dummyBox &box = boxes[b];
            const float bx = body.x - box.center.x;
            const float by = body.y - box.center.y;
            const float bz = body.z - box.center.z;
            if (!((bz * bz + by * by) + bx * bx < box.radiusSquare))
                continue;
            I3D_dummy *dummy = static_cast<I3D_dummy *>(box.frame);
            const I3D_bbox bbox = dummy->GetBBox();
            S_matrix inverse;
            inverse.Inverse(dummy->GetMatrix());
            const S_vector local = body * inverse;
            if (bbox.min.x <= local.x && local.x < bbox.max.x && bbox.min.y <= local.y && local.y < bbox.max.y &&
                bbox.min.z <= local.z && local.z < bbox.max.z) {
                covered = true;
                break;
            }
        }
        if (covered)
            continue;
        I3D_frame *volume = team->GetRandomVolume(i);
        const S_vector &at = volume->GetWorldPos();
        g_particlesModule.CreateAt(EFFECT_RAIN_DROP, S_vector(at.x, at.y + 0.1f, at.z), S_vector(0.0f, 1.0f, 0.0f),
                                   volume, true, false);
    }
}

// 0x00488560
void C_GEffectsModule::LoadMissionData(bool server)
{
    if (server)
        return;
    Reset();
    m_volumetrics.Load();
    m_volumetrics.Prune(true);
    m_meshPlayer.Load();
}

// 0x004885a0
void C_GEffectsModule::FreeMissionData(bool server)
{
    if (server)
        return;
    Reset();
    m_volumetrics.Free();
    m_meshPlayer.Free();
}

I3D_frame *C_GEffectsModule::FindSceneFrame(const char *name, uint32_t typeMask)
{
    I3D_scene *scene = GetGame()->GetScene();
    if (I3D_frame *frame = scene->FindFrame(name, typeMask))
        return frame;
    return I3DGetDriver()->FindFrame(scene, name);
}

// 0x004885d0
void C_GEffectsModule::StartMission(bool server)
{
    if (server) {
        CollectBirds();
        return;
    }
    m_time = 0;
    I3D_scene *scene = GetGame()->GetScene();
    scene->GetPrimarySector()->SetEnvironment(1);
    scene->GetOmniSector()->SetEnvironment(1);
    Reset();
    m_sun = scene->FindFrame("mirasun", FRAME_MASK_ANY);
    if (m_sun) {
        m_sun->LinkTo(scene->GetPrimarySector(), I3DLINK_KEEP_WORLD);
        m_sun->Update();
        m_sunPos = m_sun->GetPos();
    }
    if (!(g_gameRole & GAME_ROLE_SERVER))
        CollectBirds();
    LoadWaterCamera();
    if (m_waterCamera.enabled) {
        m_waterCameraFrame = nullptr;
        const I3D_camera *camera = scene->GetActiveCamera();
        m_waterCamera.savedFov = camera->GetFOV();
        camera->GetRange(m_waterCamera.savedNear, m_waterCamera.savedFar);
        m_waterCamera.underwaterNow = false;
        if (m_waterCamera.weather)
            scene->SetWeather(WEATHER_ON, 0);
        m_seaSound = FindSceneFrame("a_nsea_", 4);
        m_underwaterSound = FindSceneFrame("a_undw_", 4);
        if (m_seaSound)
            SetHierarchyOn(m_seaSound, true, FRAME_MASK_ANY);
        if (m_underwaterSound)
            SetHierarchyOn(m_underwaterSound, false, FRAME_MASK_ANY);
        m_ambientSound = FindSceneFrame("amb", 2);
        m_seaAmbientSound = FindSceneFrame("sea_amb", 2);
        if (m_ambientSound)
            SetHierarchyOn(m_ambientSound, true, FRAME_MASK_ANY);
        if (m_seaAmbientSound)
            SetHierarchyOn(m_seaAmbientSound, false, FRAME_MASK_ANY);
    }
    m_decorators.Load();
    m_decorators.Start();
    m_fadeTarget = 0.0f;
    m_fadeLevel = 0.0f;
    m_started = true;
}

// 0x004888c0
void C_GEffectsModule::StopMission(bool server)
{
    if (g_cameraHumanEffects)
        g_cameraHumanEffects->SetTactical(false);
    if (server)
        return;
    for (size_t i = m_cameraAnimations.size(); i-- > 0;) {
        if (S_CameraAnimation *animation = m_cameraAnimations[i]) {
            animation->Restore();
            delete animation;
        }
    }
    m_cameraAnimations.clear();
    SetLensFlareOn(true);
    SetThunderstorm(false, nullptr, true);
    if (m_sun) {
        m_sun->SetPosInline(m_sunPos);
        m_sun->Update();
    }
    Reset();
    FreeBirds();
    if (m_waterCamera.enabled) {
        RestoreCamera();
        if (m_waterCamera.weather)
            GetGame()->GetScene()->SetWeather(WEATHER_ON, 0);
        ShowAboveWater(true);
        ShowUnderWater(false);
        SwitchSounds(false);
        m_underwaterSound = nullptr;
        m_seaSound = nullptr;
        m_seaAmbientSound = nullptr;
        m_ambientSound = nullptr;
        ShowBackdrop(true);
    }
    m_decorators.Free();
    m_fadeTarget = 0.0f;
    m_fadeLevel = -1000.0f;
    m_started = false;
    GetGame()->GetScene()->SetWeather(WEATHER_ON, 0);
    SetRain(false);
}

// 0x004863f0
void C_GEffectsModule::RestartMission(bool)
{
    Reset();
    m_decorators.Free();
    m_decorators.Load();
    m_decorators.Start();
    FreeBirds();
    CollectBirds();
    m_time = 0;
    SetRain(false);
    if (g_cameraHumanEffects)
        g_cameraHumanEffects->SetTactical(false);
    SetThunderstorm(false, nullptr, true);
}

// 0x00486470
bool C_GEffectsModule::LoadGame(C_chunk &)
{
    m_time = 2500;
    return true;
}

// 0x004863e0
void C_GEffectsModule::SetRain(bool on)
{
    m_rain = on;
}

// 0x004870a0
void C_GEffectsModule::SetThunderstorm(bool on, I3D_light *light, bool immediate)
{
    if (!on) {
        if (!m_lightning)
            return;
        if (!immediate) {
            m_lightningStop = true;
            return;
        }
        m_lightning->SetMode(m_lightningMode);
        m_lightning->SetPower(m_lightningPower);
        m_lightning->SetOn(false);
        m_lightning->Release();
        m_lightning = nullptr;
    } else if (!m_lightning && light) {
        m_lightning = light;
        light->AddRef();
        m_lightningMode = light->GetMode();
        light->SetMode(0x11);
        light->SetOn(true);
        light->Update();
        m_lightningStop = false;
        m_lightningPower = light->GetPower();
        m_lightningTime = -1;
    }
}

// 0x004871a0
void C_GEffectsModule::StartMpLightning(int time)
{
    // Quirk kept: a storm's light in place is dropped without its mode, power or reference
    // given back.
    I3D_frame *frame = GetGame()->GetScene()->FindFrame("MP_Lightning", FRAME_MASK_ANY);
    // Fix: a frame of that name that is not a light is not kept (the original keeps it,
    // unreferenced, and flickers it as one).
    m_lightning = frame && frame->GetType() == FRAME_LIGHT ? static_cast<I3D_light *>(frame) : nullptr;
    if (!m_lightning)
        return;
    m_lightning->AddRef();
    m_lightningMode = m_lightning->GetMode();
    m_lightning->SetMode(0x11);
    m_lightning->SetOn(true);
    m_lightning->Update();
    m_lightningStop = false;
    m_lightningPower = m_lightning->GetPower();
    m_lightningTime = time;
}

// 0x00486e60
bool C_GEffectsModule::SetModelAlpha(I3D_frame *model, float level)
{
    if (!model)
        return false;
    static_cast<I3D_object_visual *>(model)->SetAlpha(1.0f - level);
    return true;
}

// 0x00486d30
bool C_GEffectsModule::SetModelTransparency(I3D_frame *model, float level)
{
    if (!model)
        return false;
    model->SetHierarchyAlpha(1.0f - level);
    return true;
}

// 0x00486df0
bool C_GEffectsModule::SetModelLightLevel(I3D_frame *model, float level)
{
    if (!(g_gameRole & GAME_ROLE_CLIENT) || !model)
        return false;
    model->SetUnknown50(level);
    return true;
}

// 0x004865e0
void C_GEffectsModule::SetHierarchyOn(I3D_frame *frame, bool on, uint32_t typeMask)
{
    for (int i = frame->NumChildren(); i-- > 0;)
        SetHierarchyOn(frame->GetChild(i), on, typeMask);
    const uint32_t bit = HierarchyTypeBit(frame->GetType());
    if (!(typeMask & bit))
        return;
    frame->SetOn(on);
    if (frame->GetType() == FRAME_SOUND)
        static_cast<I3D_sound *>(frame)->SetActive(on, false);
    frame->Update();
}

// 0x004866f0
void C_GEffectsModule::HideVisual(I3D_frame *visual, bool on)
{
    visual->SetOn(on);
    visual->Update();
}

// 0x00486e90
bool C_GEffectsModule::ClearModelTransparency(I3D_frame *model)
{
    if (!model)
        return false;
    SetModelTransparency(model, 0.0f);
    return true;
}

// 0x00488d60
void C_GEffectsModule::FadeOutModel(I3D_frame *model, int time, bool switchOff)
{
    if (!model)
        return;
    if (time < 1) {
        // (Quirk kept: a fade-in in progress goes on.)
        SetModelAlpha(model, 1.0f);
        if (switchOff)
            model->SetOn(false);
        return;
    }
    // (Quirk kept: a second fade-out of the same visual is one more record.)
    m_fadeOuts.push_back(new S_ModelFade{model, time, time, switchOff});
    model->AddRef();
    for (size_t i = m_fadeIns.size(); i-- > 0;) {
        if (m_fadeIns[i]->model == model) {
            // Fix: the record and its reference are freed (the original drops them).
            model->Release();
            delete m_fadeIns[i];
            m_fadeIns.erase(m_fadeIns.begin() + i);
            return;
        }
    }
}

// 0x00488ee0
void C_GEffectsModule::FadeIn(I3D_frame *model, int time, bool turnOn)
{
    if (!model)
        return;
    if (time < 1) {
        SetModelAlpha(model, 0.0f);
    } else {
        m_fadeIns.push_back(new S_ModelFade{model, time, time, turnOn});
        model->AddRef();
        SetModelAlpha(model, 1.0f);
        for (size_t i = m_fadeOuts.size(); i-- > 0;) {
            if (m_fadeOuts[i]->model == model) {
                // Fix: the record and its reference are freed (the original drops them).
                model->Release();
                delete m_fadeOuts[i];
                m_fadeOuts.erase(m_fadeOuts.begin() + i);
                break;
            }
        }
    }
    if (turnOn)
        model->SetOn(true);
}

// 0x00489060
void C_GEffectsModule::FadeInModel(I3D_frame *model, int time, bool turnOn)
{
    for (int i = model->NumChildren(); i-- > 0;)
        FadeInModel(model->GetChild(i), time, turnOn);
    if (IsFadedVisual(model->GetVisualType()))
        FadeIn(model, time, turnOn);
}

// 0x004890f0
void C_GEffectsModule::FadeOutHierarchy(I3D_frame *model, int time, bool switchOff)
{
    for (int i = model->NumChildren(); i-- > 0;)
        FadeOutHierarchy(model->GetChild(i), time, switchOff);
    if (IsFadedVisual(model->GetVisualType()))
        FadeOutModel(model, time, switchOff);
}

// 0x00489180
void C_GEffectsModule::FadeHuman(C_Human *human, int time, bool in)
{
    I3D_frame *model = human->GetFrame();
    if (time == 0) {
        // (Quirk kept: a fade in progress goes on.)
        model->SetHierarchyAlpha(in ? 1.0f : 0.0f);
        return;
    }
    const auto *body = static_cast<const I3D_object_visual *>(model->FindChildFrame("a", FRAME_MASK_VISUAL));
    const float alpha = body ? body->GetAlpha() : 1.0f;
    for (size_t i = m_humanFades.size(); i-- > 0;) {
        S_HumanFade &fade = m_humanFades[i];
        if (fade.human == human) {
            fade.in = in;
            fade.rate = 1.0f / static_cast<float>(time);
            fade.alpha = alpha;
            return;
        }
    }
    m_humanFades.push_back(S_HumanFade{human, in, 1.0f / static_cast<float>(time), alpha});
}

// 0x004867f0
void C_GEffectsModule::SetFadeLevel(float level, const S_vector *color)
{
    S_vector rgb(0.0f, 0.0f, 0.0f);
    if (color)
        rgb = *color;
    I3D_driver *driver = I3DGetDriver();
    const bool state3 = driver->GetState(3) != 0;
    IGraph *graph = GetIGraph();
    const bool renderState14 = graph->GetRenderState(0xE) != 0;
    driver->SetState(3, 0);
    graph->SetRenderState(0xE, 0);
    const float width = static_cast<float>(static_cast<uint32_t>(graph->ScrnX()));
    const float height = static_cast<float>(static_cast<uint32_t>(graph->ScrnY()));
    const int a = static_cast<int>(std::lrint(level * 255.0f));
    const int r = static_cast<int>(std::lrint(rgb.x * 255.0f));
    const int g = static_cast<int>(std::lrint(rgb.y * 255.0f));
    const int b = static_cast<int>(std::lrint(rgb.z * 255.0f));
    const uint32_t argb = static_cast<uint32_t>(((a << 8 | r) << 8 | g) << 8 | b);
    g_fadeQuad[0].x = 0.0f;
    g_fadeQuad[0].y = 0.0f;
    g_fadeQuad[1].x = width;
    g_fadeQuad[1].y = 0.0f;
    g_fadeQuad[2].x = 0.0f;
    g_fadeQuad[2].y = height;
    g_fadeQuad[3].x = width;
    g_fadeQuad[3].y = height;
    for (S_TLVertex &vertex : g_fadeQuad)
        vertex.color = argb;
    graph->SetRenderState(0, 1);
    graph->SetTexture(nullptr, 0);
    graph->DrawPrimitives(5, 2, g_fadeQuad, 1);
    driver->SetState(3, state3);
    graph->SetRenderState(0xE, renderState14);
}

// 0x00486b30
void C_GEffectsModule::Fade(int time, int direction, const S_vector *color)
{
    m_fadeTarget = direction != 0 ? 1.0f : 0.0f;
    m_fadeRate = time == 0 ? FLT_MAX : 1000.0f / static_cast<float>(time);
    // (The original copies a fourth float past the caller's colour.)
    m_fadeColor = color ? *color : S_vector(0.0f, 0.0f, 0.0f);
    if (m_fadeLevel < 0.0f)
        m_fadeLevel = 0.0f;
}

// 0x004867b0
void C_GEffectsModule::SetWeatherOn(bool on)
{
    if (g_gameRole & GAME_ROLE_CLIENT)
        GetGame()->GetScene()->SetWeather(WEATHER_ON, on);
}

// 0x004867e0
float C_GEffectsModule::GetGroundCover(int material)
{
    return m_decorators.GetGroundCover(material);
}

// 0x00486480
void C_GEffectsModule::SetTimeScale(float scale, float acceleration)
{
    m_timeScaleRate = std::fabs(acceleration);
    m_timeScaleTarget = scale;
}

// 0x004864a0
float C_GEffectsModule::GetTimeScale()
{
    return m_timeScale;
}

// 0x00486c10
void C_GEffectsModule::RenderFade()
{
    if (m_fadeTarget != m_fadeLevel && m_fadeLevel < 0.0f)
        m_fadeLevel = m_fadeTarget;
    if (m_time < 4000 && m_started) {
        float alpha = 1.0f;
        if (m_time > 3000)
            alpha = 1.0f - static_cast<float>(m_time - 3000) * 0.001f;
        if (m_fadeLevel != 0.0f && m_fadeLevel > alpha)
            alpha = m_fadeLevel;
        const S_vector black(0.0f, 0.0f, 0.0f);
        SetFadeLevel(alpha, &black);
        return;
    }
    if (m_fadeLevel != 0.0f)
        SetFadeLevel(m_fadeLevel, &m_fadeColor);
}

// 0x00486bf0
void C_GEffectsModule::RenderVolumetrics()
{
    m_volumetrics.Render(m_time);
}

// 0x00485c70
void C_GEffectsModule::SetCameraAnimation(I3D_frame *camera, const char *fileName)
{
    for (size_t i = m_cameraAnimations.size(); i-- > 0;) {
        if (m_cameraAnimations[i]->camera == camera) {
            if (S_CameraAnimation *animation = m_cameraAnimations[i]) {
                animation->Restore();
                delete animation;
            }
            m_cameraAnimations[i] = m_cameraAnimations.back();
            m_cameraAnimations.pop_back();
            break;
        }
    }
    if (*fileName == '\0')
        return;
    // (The original copies the name and appends "" after its first "." (meant to cut the
    // extension off, inferred): the name stays as it is.)
    auto *animation = new S_CameraAnimation;
    animation->camera = camera;
    camera->AddRef();
    animation->pos = camera->GetPos();
    animation->rot = camera->GetRot();
    animation->parent = camera->GetParent();
    camera->LinkTo(GetGame()->GetScene()->GetPrimarySector(), I3DLINK_KEEP_WORLD);
    camera->Update();
    animation->model = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    g_modelCache.OpenInto(animation->model, fileName, 0, nullptr, nullptr, 0);
    animation->cameraFrame = animation->model->FindChildFrame("*cam*", 0x1FFFF);
    if (!animation->cameraFrame && animation->model->NumChildren() != 0)
        animation->cameraFrame = animation->model->GetChild(0);
    animation->model->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    S_quat identity;
    identity.Identity();
    animation->model->SetRot(identity);
    animation->model->Update();
    m_cameraAnimations.push_back(animation);
}

// 0x00485b50
void C_GEffectsModule::SetCloudsOn(bool on)
{
    if (on)
        LoadClouds();
    else
        FreeClouds();
}

// 0x00485b70
void C_GEffectsModule::SetLensFlareOn(bool on)
{
    if (I3D_frame *flare = GetGame()->GetScene()->GetBackdropSector()->FindChildFrame("lensflare", FRAME_MASK_ANY))
        flare->SetOn(on);
}

// 0x00486580
void C_GEffectsModule::ShowAboveWater(bool on)
{
    if (!m_waterCamera.enabled || m_waterCamera.aboveWater[0] == '\0')
        return;
    if (I3D_frame *frame = GetGame()->GetScene()->FindFrame(m_waterCamera.aboveWater, FRAME_MASK_ANY))
        SetHierarchyOn(frame, on, FRAME_MASK_ANY);
}

// 0x00486750
void C_GEffectsModule::ShowUnderWater(bool on)
{
    if (!m_waterCamera.enabled || m_waterCamera.underWater[0] == '\0')
        return;
    if (I3D_frame *frame = GetGame()->GetScene()->FindFrame(m_waterCamera.underWater, FRAME_MASK_ANY))
        SetHierarchyOn(frame, on, FRAME_MASK_ANY);
}

// 0x00486520
void C_GEffectsModule::ShowBackdrop(bool on)
{
    if (!m_waterCamera.enabled || !m_waterCamera.switchBackdrop)
        return;
    I3D_sector *backdrop = GetGame()->GetScene()->GetBackdropSector();
    for (int i = backdrop->NumChildren(); i-- > 0;)
        GetGame()->GetScene()->GetBackdropSector()->GetChild(i)->SetOn(on);
}

void C_GEffectsModule::TakeCamera(I3D_camera *camera)
{
    m_waterCameraFrame = camera;
    camera->AddRef();
    m_waterCamera.savedFov = camera->GetFOV();
    camera->GetRange(m_waterCamera.savedNear, m_waterCamera.savedFar);
}

void C_GEffectsModule::RestoreCamera()
{
    if (!m_waterCameraFrame)
        return;
    m_waterCameraFrame->SetFOV(m_waterCamera.savedFov);
    m_waterCameraFrame->SetRange(m_waterCamera.savedNear, m_waterCamera.savedFar);
    m_waterCameraFrame->Update();
    m_waterCameraFrame->Release();
    m_waterCameraFrame = nullptr;
}

void C_GEffectsModule::SwitchSounds(bool underwater)
{
    if (m_seaSound)
        SetHierarchyOn(m_seaSound, !underwater, FRAME_MASK_ANY);
    if (m_underwaterSound)
        SetHierarchyOn(m_underwaterSound, underwater, FRAME_MASK_ANY);
    if (m_ambientSound)
        SetHierarchyOn(m_ambientSound, !underwater, FRAME_MASK_ANY);
    if (m_seaAmbientSound)
        SetHierarchyOn(m_seaAmbientSound, underwater, FRAME_MASK_ANY);
}

// 0x00489c60
void C_GEffectsModule::LoadWaterCamera()
{
    std::memset(&m_waterCamera, 0, sizeof(m_waterCamera));
    m_waterCameraFrame = nullptr;
    C_game *game = GetGame();
    if (!game || game->GetMissionName()[0] == '\0')
        return;
    C_chunk chunk;
    const C_fstr fileName("missions\\%s\\%s", game->GetMissionName(), "watercam.set");
    if (!chunk.Open(fileName, false))
        return;
    uint16_t id;
    chunk.Descend(id);
    // Fix: the version is 0 when it cannot be read (the original's is left over from the name).
    int version = 0;
    chunk.Read(&version, 4);
    chunk.Ascend();
    if (version != 1)
        return;
    chunk.Descend(id);
    chunk.Read(&m_waterCamera, sizeof(m_waterCamera));
    chunk.Ascend();
}
