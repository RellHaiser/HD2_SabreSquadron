// C_game, part 1 (original source 0x006987d0..0x0069c550 and 0x0069e8b0..0x0069f3c0):
// construction, the mission's start and end, the input, the HUD switches, the static lights,
// the music and the light level of a point. The multiplayer rounds are in game_rounds.cpp,
// the saved games in game_save.cpp.
#include "game/first_person_view.h"
#include "game/game.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/actor.h"
#include "game/actors_module.h"
#include "game/car_effects.h"
#include "game/collision.h"
#include "game/console.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/human_stains.h"
#include "game/interface.h"
#include "game/particles.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "script/script_module.h"

C_game *g_game = nullptr;                           // 0x008ae8f4

namespace {

// Empty control states the game hands on while the player's controls are off (0x008ae7f0 /
// 0x008ae6f0, cleared by the constructor).
float g_noHeldControls[64];
float g_noPressedControls[64];

// Light frames the game switches off for the mission: types 1..4 and 8 not marked dynamic
// (light mode bits 1 / 8; meaning inferred).
bool IsStaticSceneLight(const I3D_light *light)
{
    const int type = light->GetLightType();
    if (type <= 0 || (type > 4 && type != 8))
        return false;
    return (light->GetMode() & 9) == 0;
}

// I3D_scene::EnumFrames callback of SwitchStaticLights.
uint32_t __stdcall CollectStaticLight(I3D_frame *frame, void *context)   // 0x0069c350
{
    I3D_light *light = static_cast<I3D_light *>(frame);
    if (light->IsOn() && IsStaticSceneLight(light)) {
        light->SetOn(false);
        light->Update();
        static_cast<std::vector<I3D_light *> *>(context)->push_back(light);
    }
    return 0;
}

} // namespace

C_game *GetGame()                                   // 0x006987d0
{
    return g_game;
}

void S_MissionObjective::SetStatus(int newStatus)  // 0x0069c180
{
    status = newStatus;
    if (newStatus == OBJECTIVE_COMPLETED)
        rewarded = true;
}

C_game::C_game()                                    // 0x006987e0
{
    m_interface = new C_Interface();
    m_firstPersonView = new C_FirstPersonView();
    m_quickMessages = new C_QuickMessages();
    m_fadeTime = 0;
    m_scene = reinterpret_cast<I3D_scene *>(I3DGetDriver()->CreateFrame(FRAME_SCENE));
    m_scene->SetRenderCallback(reinterpret_cast<void *>(&GameRenderCallback), 6);
    if (m_scene) {
        // The whole screen.
        IGraph *graph = GetIGraph();
        const int *viewport = m_scene->GetViewportRect();
        if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != graph->ScrnX() || viewport[3] != graph->ScrnY())
            m_scene->SetViewport(0, 0, graph->ScrnX(), graph->ScrnY());
    }
    m_checkpoints = new C_Checkpoints();
    m_checkpoints->Clear();
    g_game = this;
    m_missionLoaded = false;
    m_savesUsed = 0;
    m_time = 0;
    m_missionTime = 0;
    m_roundTime = 0;
    m_aviName = "";
    m_playAvi = false;
    m_alarmMusic = -1;
    m_music = -1;
    std::memset(g_noHeldControls, 0, sizeof(g_noHeldControls));
    std::memset(g_noPressedControls, 0, sizeof(g_noPressedControls));
    m_mode = GAME_MODE_NORMAL;
    m_hasQuickSave = false;
}

C_game::~C_game()                                   // 0x00698b00
{
    ClearCarSurfaces();
    FreeTyres();
    delete m_interface;
    delete m_firstPersonView;
    delete m_quickMessages;
    FreeMission();
    ReleaseScene();
    g_game = nullptr;
    delete m_checkpoints;
    m_checkpoints = nullptr;
}

// ---------------------------------------------------------------------------
// The mission's start and end

bool C_game::Start(void *arg)                       // 0x00698c80
{
    C_MissionDef *mission = static_cast<C_MissionDef *>(arg);
    if (m_state == GAME_STATE_STARTING)
        return false;
    const int campaign = g_campaign->GetCurrentIndex();
    if (campaign == 0)
        m_campaignMissionNumber = g_campaign->GetMissionNumber(0);
    else
        m_campaignMissionNumber = g_campaign->CountMissions(0) + g_campaign->GetMissionNumber(campaign);
    m_uniformDisguise = true;
    m_noTeamDamage = false;
    m_tacticalMode = true;
    m_missionTime = 0;
    m_time = 0;
    m_roundTime = 0;
    m_mission = nullptr;
    m_signalPistolFired = false;

    const uint32_t graphFlags = GetIGraph()->GetInfo()->flags;
    GetIGraph()->SetFlags(graphFlags & ~IGRAPH_FLAG_20);
    if (!mission) {
        // A multiplayer map: loaded already when the session restarts it.
        if (!m_missionLoaded && LoadMission(g_mpSettings->GetCurrentMap()->GetDirectory(), nullptr) != 0) {
            GetIGraph()->SetFlags(graphFlags);
            return false;
        }
    } else {
        if (LoadMission(mission->GetName(), mission) != 0) {
            GetIGraph()->SetFlags(graphFlags);
            return false;
        }
        m_mission = mission;
    }

    g_loaderScreen.SetProgress(LOADER_STAGE_START, 0.05f);
    m_playerInputEnabled = true;
    m_missionFailed = false;
    LoadMaterialTables();
    LoadTyres();
    InitCarSurfaces();
    InitCarEffects();
    g_netGame.Command(NET_COMMAND_SCAN_LEVEL, nullptr, 0);
    g_loaderScreen.SetProgress(LOADER_STAGE_START, 0.1f);
    SwitchStaticLights(false);
    if (IsLocalGame() && g_gameProfile.GetDetailSettings()->IsEnabled(DETAIL_STAINS))
        g_stainManager.Init(0);
    if (IsServer())
        ModuleManager::BroadcastStartMission(true);
    g_loaderScreen.SetProgress(LOADER_STAGE_START, 0.35f);
    if (IsClient()) {
        m_interface->Init();
        ModuleManager::BroadcastStartMission(false);
    }
    g_loaderScreen.SetProgress(LOADER_STAGE_START, 0.5f);
    if (IsClient())
        m_firstPersonView->Init();
    g_loaderScreen.SetProgress(LOADER_STAGE_START, 0.8f);

    m_state = GAME_STATE_STARTING;
    m_fadeTime = 500;
    m_endDelay = 0;
    if (IsLocalGame()) {
        // The squad chosen in the menus appears at the spawn points.
        std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
        for (size_t i = 0; i < squad.size(); ++i) {
            if (!squad[i]->deployed)
                continue;
            C_Actor *spawnPoint = g_actorsModule.FindSpawnPoint(0);
            if (!spawnPoint)
                break;
            static_cast<C_SpawnPoint *>(spawnPoint)->Spawn(0, squad[i], nullptr);
        }
    }
    if (IsClient())
        ClearSpeechSlots();
    m_paused = false;
    g_loaderScreen.SetProgress(LOADER_STAGE_START, 0.9f);
    if (m_quickMessages)
        m_quickMessages->Init(m_missionName);
    m_round = 0;
    m_teamLivesUsed = 0;
    m_vipChosen = false;
    m_hasVipObjective = false;
    m_vipSlot = -1;
    m_respawnRequest = 0;
    m_roundState = IsLocalGame() || !IsServer() ? ROUND_PLAYING : ROUND_WARMUP;
    m_respawnWaveTime = 0;
    m_cameraMode = GAME_CAMERA_FOLLOW;
    m_spectatedSlot = -1;
    m_unknown94 = false;
    m_inRestrictedArea = false;
    if (!IsLocalGame()) {
        for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
            if (GetMpObjective(m_mission, i)->type == OBJECTIVE_TYPE_VIP) {
                m_hasVipObjective = true;
                break;
            }
        }
        if (IsServer())
            SendToPlayers(GAME_MSG_TEMP_ACTORS_START);
    }
    m_musicTimer = 0;
    m_alarmCount = 0;
    m_inputDelay = 2500;
    RestoreMusic(false);
    g_loaderScreen.Finish();
    if (IsLocalGame() && GetGameApp())
        m_hasQuickSave = GetGameApp()->HasQuickSave();
    GetIGraph()->SetFlags(graphFlags);
    return true;
}

bool C_game::Stop()                                 // 0x00699250
{
    if (IsLocalGame()) {
        // The squad's soldiers go back to the campaign's records (the players have the
        // first actor ids).
        for (int id = 0;; ++id) {
            C_Human *human = static_cast<C_Human *>(g_actorsModule.GetActorById(id));
            if (!human || human->GetRole() != HUMAN_ROLE_PLAYER)
                break;
            S_SquadSoldier *state = g_gameProfile.FindSoldierState(human->GetProfileIndex());
            if (state) {
                ClearSoldierItems(state);
                human->GetSoldierState(*state);
                MergeSoldierItems(state);
                UpdateSoldierLoad(state);
            }
        }
    } else {
        std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
        while (!squad.empty()) {
            delete squad.back();
            squad.pop_back();
        }
    }
    if (IsClient())
        m_firstPersonView->Done();
    SwitchStaticLights(true);
    if (IsClient()) {
        ModuleManager::BroadcastStopMission(false);
        FreeSpeechSlots();
    }
    if (IsServer())
        ModuleManager::BroadcastStopMission(true);
    if (IsClient())
        m_interface->Done();
    g_collisionWorld.Unload(false);
    // The tables LoadMaterialTables() read.
    operator delete(g_materials);
    operator delete(g_carEffects);
    g_materialCount = 0;
    g_materials = nullptr;
    g_carEffectCount = 0;
    g_carEffects = nullptr;
    if (m_state == GAME_STATE_RUNNING)
        m_state = GAME_STATE_ENDING;
    TimedEvents_Update(0, true);
    if (m_quickMessages)
        m_quickMessages->Done();
    m_checkpoints->ResetState();
    FreeCarEffectEmitters();
    g_stainManager.Done();
    return true;
}

void C_game::ReleaseScene()                         // 0x00699540
{
    if (!m_scene)
        return;
    FreeMission();
    m_scene->Release();
    m_scene = nullptr;
}

void C_game::FreeMission()                          // 0x0069a6a0
{
    if (m_state == GAME_STATE_RUNNING)
        Stop();
    if (!m_missionLoaded)
        return;
    m_alarmMusic = -1;
    m_music = -1;
    FreeAllFrameData(m_scene);
    if (IsServer())
        ModuleManager::BroadcastFreeMissionData(true);
    if (IsClient())
        ModuleManager::BroadcastFreeMissionData(false);
    for (size_t i = m_animatedModels.size(); i-- > 0;)
        m_animatedModels[i]->Release();
    m_animatedModels.clear();
    m_checkpoints->Clear();
    m_scene->Close();
    m_missionName = nullptr;
    m_missionLoaded = false;
}

void C_game::EndMission(int)                        // 0x0069a7d0
{
    if (m_state == GAME_STATE_ENDING)
        return;
    m_state = GAME_STATE_ENDING;
    m_fadeTime = 500;
    m_endDelay = 1000;
    if (!IsLocalGame()) {
        g_netGame.Close(false);
        if (IsServer())
            GetGameApp()->SendGameMessage(GAME_MSG_GAME_ENDED, 0, 0, 0, false);
    }
}

void C_game::ApplyFade()                            // 0x0069b0e0
{
    if (m_state == GAME_STATE_STARTING)
        return;
    // The fade in / out over the last 300 ms of the 500.
    float elapsed = static_cast<float>(m_fadeTime - 200);
    if (elapsed < 0.0f)
        elapsed = 0.0f;
    const float level = 1.0f - elapsed * (1.0f / 300.0f);
    if (level != 0.0f)
        GetGEffects()->SetFadeLevel(level, nullptr);
}

C_GameState *C_game::OpenGameMenu(int arg)         // 0x0069e8f0
{
    C_GameState *current = this;
    C_GameMenu *menu = new C_GameMenu();
    if (menu) {
        SetState(GAME_STATE_RUNNING);
        menu->SetGame(this);
        menu->Start(reinterpret_cast<void *>(static_cast<intptr_t>(arg)));
        current = menu;
    }
    current->SetState(GAME_STATE_RUNNING);
    // (The original calls an empty interface method here.)
    GetCameraModule()->SetSpecialMode(3, true, nullptr);
    return current;
}

// ---------------------------------------------------------------------------
// Frames

void C_game::Render()                               // 0x0069bb40
{
    // A dedicated server draws nothing.
    if (IsLocalGame() || !g_mpSettings->dedicated)
        m_scene->Render(0);
}

bool C_game::HandleInput(float *held, float *pressed)   // 0x0069a830
{
    if (m_fadeTime > 0 || m_inputDelay != 0)
        return true;
    if (IsClient())
        m_interface->FilterInput(held, pressed);

    // Quick save / quick load.
    if (pressed && GetGameApp()->m_state == this && IsLocalGame() && m_playerInputEnabled) {
        if (pressed[INPUT_QUICK_SAVE] == 0.0f || (m_mission && m_mission->AreObjectivesDecided())) {
            if (pressed[INPUT_QUICK_LOAD] != 0.0f && m_hasQuickSave)
                GetGameApp()->m_pendingCommand = APP_COMMAND_QUICK_LOAD;
        } else {
            const int difficulty = g_gameProfile.GetDifficulty();
            if (difficulty == DIFFICULTY_NO_SAVES) {
                PrintMessage(MESSAGE_WHITE, g_texts.Get(TEXT_SAVE_DISABLED));
            } else if (difficulty != DIFFICULTY_LIMITED_SAVES || m_savesUsed < 3) {
                // Not in the very first mission.
                const int campaign = g_campaign->GetCurrentIndex();
                const int number = g_campaign->GetMissionNumber(campaign);
                if (campaign != 0 || number != 0)
                    GetGameApp()->m_pendingCommand = APP_COMMAND_QUICK_SAVE;
            } else {
                const C_fstr text(g_texts.Get(TEXT_SAVES_LEFT), 0);
                PrintMessage(MESSAGE_WHITE, text);
            }
        }
    }
    if (!IsLocalGame() && !m_interface->IsRespawnScreenShown())
        UpdateSpectatorCamera(held, pressed);

    if (!m_playerInputEnabled || !held || !pressed) {
        // A cutscene is skipped with the menu key.
        if (!m_playerInputEnabled || !held) {
            if (pressed && pressed[INPUT_MENU] != 0.0f && g_scriptModule.IsCutsceneRunning()) {
                g_scriptModule.EndCutscene();
                return true;
            }
        }
        held = g_noHeldControls;
        pressed = g_noPressedControls;
    }

    if (IsLocalGame()) {
        if (m_state == GAME_STATE_RUNNING) {
            // Once the objectives are decided the soldier takes no more orders.
            if (!AreObjectivesDecided()) {
                if (held && pressed)
                    m_interface->HandleInput(held, pressed);
                C_Human *player = g_teamManager.GetPlayerTeam()->GetSelectedMember();
                if (!player || m_paused) {
                    if (!pressed)
                        return true;
                    if (pressed[INPUT_TOGGLE_HUD] != 0.0f) {
                        GetGame()->ShowFirstPersonView(!GetGame()->IsFirstPersonView());
                        g_hudOption->shown = GetGame()->IsFirstPersonView();
                    }
                } else {
                    float *playerPressed = pressed;
                    if (m_interface->IsCapturingInput() || m_roundState != ROUND_PLAYING) {
                        held = nullptr;
                        playerPressed = nullptr;
                    }
                    player->HandlePlayerInput(held, playerPressed);
                }
            }
        }
    } else if (IsClient() && m_state == GAME_STATE_RUNNING) {
        if (held && pressed)
            m_interface->HandleInput(held, pressed);
        C_Human *player = g_netGame.GetLocalHuman();
        if (player) {
            float *playerPressed = pressed;
            if (m_interface->IsCapturingInput() || m_roundState != ROUND_PLAYING) {
                held = nullptr;
                playerPressed = nullptr;
            }
            player->HandlePlayerInput(held, playerPressed);
        }
        if (!pressed)
            return true;
        if (pressed[INPUT_MP_MENU] != 0.0f) {
            m_state = GAME_STATE_MP_GAME_MENU;
            return true;
        }
    }
    if (pressed && pressed[INPUT_MENU] != 0.0f)
        m_state = GAME_STATE_GAME_MENU;
    return true;
}

void C_game::SetPaused(bool paused)                 // 0x0069bb80
{
    // Never in the first 2.5 s, and only in single player.
    if (m_missionTime < 2500)
        m_paused = false;
    else if (IsLocalGame())
        m_paused = paused;
    else
        m_paused = false;
    PauseSpeechSlots(m_paused);
    PauseActorEffects(paused);
    g_scriptModule.SetPauseSoundsPaused(paused);
}

// ---------------------------------------------------------------------------
// Animated models, HUD switches

void C_game::AddAnimatedModel(I3D_frame *frame)     // 0x0069a5b0
{
    if (!frame)
        return;
    I3D_model *model = static_cast<I3D_model *>(frame);
    for (size_t i = m_animatedModels.size(); i-- > 0;) {
        if (m_animatedModels[i] == model)
            return;
    }
    if (frame->GetType() != FRAME_MODEL)
        return;
    // Fix: the original takes the reference also when the model has nothing to animate and
    // is not kept, so it is never released.
    if (model->NumTracks() != 0) {
        m_animatedModels.push_back(model);
        model->AddRef();
    }
}

void C_game::RemoveAnimatedModel(I3D_frame *frame)  // 0x0069a630
{
    if (!frame)
        return;
    for (size_t i = m_animatedModels.size(); i-- > 0;) {
        if (m_animatedModels[i] == frame) {
            frame->Release();
            m_animatedModels[i] = m_animatedModels.back();
            m_animatedModels.pop_back();
            return;
        }
    }
}

C_CameraModule *C_game::GetCameraModule()           // 0x0069a620
{
    return ::GetCameraModule();
}

void C_game::ShowFirstPersonView(bool show)                     // 0x0069a770
{
    m_firstPersonView->Show(show);
}

bool C_game::IsFirstPersonView() const                     // 0x0069a780
{
    return m_firstPersonView->IsShown() || m_firstPersonView->IsBinocularsShown();
}

void C_game::ShowBinocularsView(bool show)                // 0x0069a7a0
{
    m_firstPersonView->ShowBinoculars(show);
}

bool C_game::IsBinocularsView() const                // 0x0069a7b0
{
    if (!IsClient())
        return false;
    return m_firstPersonView->IsBinocularsShown();
}

void C_game::Print(const char *, ...)               // 0x0069c2b0
{
    // Empty in the release build.
}

void C_game::PrintMessage(const S_vector &color, const char *format, ...)   // 0x0069c240
{
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsprintf(line, format, args);
    va_end(args);
    if (IsClient()) {
        m_interface->AddMessage(color, line);
        return;
    }
    GetConsole()->AddLine(line);
}

void C_game::PlayAvi(const char *fileName)          // 0x0069e8b0
{
    m_playAvi = true;
    m_aviName = fileName;
}

// ---------------------------------------------------------------------------
// Static lights

void C_game::SwitchStaticLights(bool restore)       // 0x0069c2c0
{
    if (restore) {
        for (size_t i = m_staticLights.size(); i-- > 0;)
            m_staticLights[i]->SetOn(true);
        m_staticLights.clear();
        return;
    }
    SwitchStaticLights(true);
    GetGame()->GetScene()->EnumFrames(CollectStaticLight, &m_staticLights, FRAME_MASK_LIGHT, 0);
}

// ---------------------------------------------------------------------------
// Music

void C_game::RestoreMusic(bool alarm)               // 0x0069ec40
{
    if (!IsClient())
        return;
    int music = alarm ? m_alarmMusic : m_music;
    if (!IsLocalGame())
        music = -1;
    if (GetSoundModule()->GetBackgroundMusic() != music)
        GetSoundModule()->CrossfadeMusic(music, 4000, 0);
    m_musicTimer = 5000;
}

void C_game::SetAlarm(bool on)                      // 0x0069ebf0
{
    if (on) {
        ++m_alarmCount;
        RestoreMusic(true);
        return;
    }
    if (--m_alarmCount == 0)
        RestoreMusic(false);
}

// ---------------------------------------------------------------------------
// Light level

float C_game::GetLightIntensity(const S_vector &pos, I3D_sector *sector, bool occlusion)   // 0x0069eca0
{
    if (!sector) {
        sector = m_scene->GetSector(pos);
        if (!sector)
            return 0.0f;
    }
    const uint32_t rayFlags = IsMissionRunning() ? 0x20C101 : 0x20C301;
    float direct = 0.0f;
    float ambient = 0.0f;
    for (int i = sector->NumLights(); i-- > 0;) {
        I3D_light *light = sector->GetLight(i);
        if (!light->IsOn())
            continue;
        const uint32_t mode = light->GetMode();
        if (!(mode & 1) || !(mode & 0x40))
            continue;
        // Luminance of the light's color.
        const S_vector &color = light->GetEffectiveColor();
        float value = (color.x + color.x) * 0.3f + (color.y + color.y) * 0.6f + (color.z + color.z) * 0.1f;
        const int type = light->GetLightType();
        if (type == I3DLIGHT_AMBIENT) {
            ambient += value;
            continue;
        }
        if (type == 0 || (type >= 5 && type <= 7))
            continue;
        if (type == I3DLIGHT_SPOT || type == I3DLIGHT_POINT) {
            const S_vector &lightPos = light->GetLightPos();
            if (type == I3DLIGHT_SPOT) {
                S_vector dir = {pos.x - lightPos.x, pos.y - lightPos.y, pos.z - lightPos.z};
                dir.Normalize();
                const S_vector &axis = light->GetLightDir();
                const float cosine = dir.x * axis.x + dir.y * axis.y + dir.z * axis.z;
                if (cosine <= light->GetConeOuterCos())
                    continue;
                if (cosine <= light->GetConeInnerCos())
                    value = (cosine - light->GetConeOuterCos()) * light->GetConeFalloff() * value;
            }
            const float dx = pos.x - lightPos.x;
            const float dy = pos.y - lightPos.y;
            const float dz = pos.z - lightPos.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            float dist;
            if (distSq < 1e-8f)
                dist = 0.0f;
            else if (std::fabs(distSq - 1.0f) < 1e-8f)
                dist = 1.0f;
            else
                dist = std::sqrt(distSq);
            if (!(dist < light->GetRange()))
                continue;
            value = value / (dist * dist * light->GetAttenuation(2) + dist * light->GetAttenuation(1) +
                             light->GetAttenuation(0));
        }
        if (occlusion) {
            // Lit only when the ray to the light hits nothing (or glass / foliage, 'M' / 'L').
            S_vector delta;
            if (type == I3DLIGHT_DIRECTIONAL) {
                const S_vector &axis = light->GetLightDir();
                delta = {-axis.x * 100.0f, -axis.y * 100.0f, -axis.z * 100.0f};
            } else {
                const S_vector &lightPos = light->GetWorldPos();
                delta = {lightPos.x - pos.x, lightPos.y - pos.y, lightPos.z - pos.z};
            }
            const S_CollisionVolume *hit = g_collisionWorld.RayTest(pos, delta, nullptr, nullptr, rayFlags, 0x910000);
            if (hit) {
                const int material = GetVolumeMaterial(*hit);
                if (material != 'M' && material != 'L')
                    continue;
            }
        }
        direct += value;
    }
    const float total = direct + ambient;
    if (total < 0.0f)
        return 0.0f;
    if (total < 1.0f)
        return total;
    return 1.0f;
}

// The mission scripts' objective changes.
void SetObjectiveStatus(int objective, int status)  // 0x006b6830
{
    GetGame()->SetObjectiveStatus(objective, status);
}

// Enables a multiplayer spawn zone for a side.
bool EnableSpawnZone(const char *zoneName, int side)   // 0x0069f320
{
    // The first 32 actors are the players.
    for (int i = 32; i < g_actorsModule.GetActorCount(); ++i) {
        C_Actor *actor = g_actorsModule.GetActor(i);
        if (actor->GetType() != ACTOR_MP_ZONE)
            continue;
        C_MPZone *zone = static_cast<C_MPZone *>(actor);
        if (zone->GetObjective() != -1 || !zone->GetFrame())
            continue;
        if (_stricmp(zone->GetFrame()->GetShortName(), zoneName) == 0) {
            zone->EnableForSide(side);
            return true;
        }
    }
    return false;
}
