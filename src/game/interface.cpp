// The game interface's core (see interface.h; the original's 0x0049a420..0x0049fc10): its scene,
// the panels' life, drawing and updates, the keys, the actor under the crosshair, the lower
// panel's position and the saved state.
#include "game/interface.h"

#include <cstdio>
#include <vector>

#include <windows.h>
#include <mmsystem.h>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/squad_commander.h"
#include "game/team.h"
#include "game/world.h"
#include "game/collision.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

I3D_sound *g_clickSound = nullptr;                      // 0x0089bc8c

namespace {

// The 16 bytes of saved-game chunk 0x3EA (0x0089bc7c).
// Dropped: the original stamps the MD5 of its executable there (0x006ea340; "BAADBAADBAADBAAD"
// when it cannot read itself); nothing checks it. The port writes what it last loaded (zeros
// at first) so the saves keep their format.
uint8_t s_saveStamp[16] = {};
// timeGetTime() of the last click on a member on the maps (the double clicks).
uint32_t s_lastMemberClick = 0;                         // 0x0089bc90

// The saved game's chunks.
enum : uint16_t {
    CHUNK_INTERFACE = 1000,                             // the commanded member, the command map open
    CHUNK_INTERFACE_HINTS = 0x3E9,
    CHUNK_INTERFACE_STAMP = 0x3EA,
    CHUNK_INTERFACE_TIMER = 0x3EB,
};

// The interface's own scene: an orthogonal camera at (0.5, -0.375) looking along z at the
// model, which spans x 0..1, y 0..-0.75.
const S_vector CAMERA_POS(0.5f, -0.375f, -15.5f);
constexpr float CAMERA_NEAR = 0.2f;
constexpr float CAMERA_FAR = 500.0f;
constexpr float CAMERA_ORTHO_SCALE = 2.0f;
// The driver's states the model is loaded with (the setup's texture options).
constexpr int DRIVER_STATE_15 = 0x15;
constexpr int DRIVER_STATE_19 = 0x19;

// The lower panel's three heights.
constexpr float DOWN_PANEL_X = 0.451414f;
constexpr float DOWN_PANEL_UP = -1.28972f;
constexpr float DOWN_PANEL_LOW = -1.321f;
constexpr float DOWN_PANEL_DOWN = -1.375f;

// The explosive timer's places: its own, below the signals, on the command map.
const float TIMER_POS[3] = {0.938749f, -0.461257f, 0.0f};
const float TIMER_POS_SIGNALS[3] = {0.938749f, -0.23f, 0.0f};
const float TIMER_POS_COMMAND_MAP[3] = {0.87f, -0.4f, 0.0f};

// The pick: every 100 ms in single player; the last actor stays for 4 misses.
constexpr int PICK_INTERVAL = 100;
constexpr int PICK_MISSES = 5;
// In a vehicle the ray starts 1.5 m ahead of the camera.
constexpr float PICK_VEHICLE_AHEAD = 1.5f;
// The first ray skips water and what bullets pass; behind water or a shoot-through part the
// second one looks for a human.
constexpr uint32_t PICK_EXCLUDE = COLLISION_FLAG_WATER | COLLISION_FLAG_BULLETS_PASS;
constexpr uint32_t PICK_SEE_THROUGH = COLLISION_FLAG_WATER | COLLISION_VOLUME_SHOOT_THROUGH;
constexpr float PICK_DOOR_RANGE = 0.65f;
constexpr float PICK_USE_RANGE = 0.2f;
constexpr float PICK_HUMAN_BEHIND_RANGE = 2.0f;

// The compass item.
constexpr int ITEM_COMPASS = 9;
// GetBehaviour() of the scoped rifles (inferred).
constexpr int ITEM_BEHAVIOUR_SCOPED = 4;
// C_ScopeComp::GetAimMode() 0..3: the reticles.
constexpr int AIM_MODE_BINOCULARS = 4;
constexpr int AIM_MODE_NONE = 6;

// The keys of the soldier switch (DirectInput).
constexpr int KEY_LSHIFT = 0x2A;
constexpr int KEY_RSHIFT = 0x36;
constexpr int KEY_LALT = 0x38;
constexpr int KEY_RALT = 0xB8;

// The squad members (the map cursor's ids are 100 + slot).
constexpr int MEMBER_ID_FIRST = 100;
constexpr int DOUBLE_CLICK_TIME = 300;

// The pick outline: 10 px from the screen's edges, at least 2.5 % of it, corners a quarter of
// its sides, white at half alpha.
constexpr float OUTLINE_MARGIN = 10.0f;
constexpr float OUTLINE_MIN_SIZE = 0.025f;
constexpr uint32_t OUTLINE_COLOR = 0x80FFFFFF;

// The camera modes C_Interface::SetViewMode() gets (C_PlayerController's view modes).
enum : int {
    VIEW_BINOCULARS = 1,
    VIEW_SCOPE = 3,
};

C_Team *PlayerTeam()
{
    return g_teamManager.GetPlayerTeam();
}

void PlaceExplosiveTimer(C_ExplosiveTimer &timer, const float *pos)
{
    timer.SetPos(pos);
}

// Zeroes the soldier's movement controls / every action's values of the frame.
void ClearMoveControls(float *held, float *pressed)    // 0x0049c1a0
{
    // Quirk kept: the bindings on the key codes 0..9 (Escape and 1..8 of the main keyboard), not
    // those of the actions 0..9.
    for (int key = 0; key < 10; ++key)
        g_controls.ClearKey(key, held, pressed);
}

void ClearAllControls(float *held, float *pressed)     // 0x0049c1d0
{
    for (int i = 64; i-- > 0;) {
        pressed[i] = 0.0f;
        held[i] = 0.0f;
    }
}

// The texture of the first face group of a mesh visual (the icons).
I3D_texture *VisualTexture(I3D_frame *frame)
{
    I3D_mesh_level *level = static_cast<I3D_visual *>(frame)->GetVisualMesh()->GetLevel(0);
    return level->FaceGroups()[0].material->GetTexture();
}

// The command map's double click: its camera behind `human` (0x004bf210).
void LookAtOnCommandMap(C_Human *human)
{
    GetCameraModule()->GetHumanEffects()->LookAt(human);
}

bool IsSeatVehicle(const C_Actor *actor)
{
    switch (actor->GetType()) {
    case ACTOR_CAR:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
    case ACTOR_JUNKERS88:
        return true;
    default:
        return false;
    }
}

// The distance from `from` to `to` as the game's S_vector::Magnitude (the sum z, y, x).
float Distance(const S_vector &to, const S_vector &from)
{
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float dz = to.z - from.z;
    return MagnitudeBy(dz * dz + dy * dy + dx * dx);
}

} // namespace

C_Interface::C_Interface() = default;

C_Interface::~C_Interface() = default;

// ---------------------------------------------------------------------------------------------
// Life

bool C_Interface::Init()
{
    if (m_flags & INTERFACE_ACTIVE)
        return true;
    I3D_driver *driver = I3DGetDriver();
    m_scene = reinterpret_cast<I3D_scene *>(driver->CreateFrame(FRAME_SCENE));
    if (!m_scene)
        return false;
    // The whole screen.
    IGraph *graph = GetIGraph();
    const int *viewport = m_scene->GetViewportRect();
    if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != graph->ScrnX() || viewport[3] != graph->ScrnY())
        m_scene->SetViewport(0, 0, graph->ScrnX(), graph->ScrnY());

    // The "gameinterface" model.
    m_model = static_cast<I3D_model *>(driver->CreateFrame(FRAME_MODEL));
    const bool state15 = driver->GetState(DRIVER_STATE_15) != 0;
    const bool state19 = driver->GetState(DRIVER_STATE_19) != 0;
    driver->SetState(DRIVER_STATE_15, 0);
    driver->SetState(DRIVER_STATE_19, 1);
    const HRESULT result = g_modelCache.OpenInto(m_model, "gameinterface");
    driver->SetState(DRIVER_STATE_15, state15);
    driver->SetState(DRIVER_STATE_19, state19);
    if (FAILED(result)) {
        m_scene->Release();
        m_scene = nullptr;
        // Fix: the original kept the model (and leaked it).
        m_model->Release();
        m_model = nullptr;
        return false;
    }
    I3D_frame *sector = reinterpret_cast<I3D_frame *>(m_scene->GetPrimarySector());
    m_model->LinkTo(sector, 0);
    m_model->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    m_scene->AddFrame(m_model);

    auto *camera = static_cast<I3D_camera *>(driver->CreateFrame(FRAME_CAMERA));
    m_camera = camera;
    m_scene->SetActiveCamera(camera);
    camera->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    camera->SetPosInline(CAMERA_POS);
    camera->SetRange(CAMERA_NEAR, CAMERA_FAR);
    camera->SetOrthogonal(true);
    camera->SetOrthoScale(CAMERA_ORTHO_SCALE);
    camera->Update();

    // A white ambient light.
    auto *light = static_cast<I3D_light *>(driver->CreateFrame(FRAME_LIGHT));
    light->SetName("GI_Light");
    light->SetLightType(4);
    light->SetAmbientColor(S_vector(1.0f, 1.0f, 1.0f));
    light->SetAmbientPower(1.0f);
    light->SetMode(0x11);
    m_scene->AddFrame(light);
    m_scene->GetPrimarySector()->AddLight(light);
    light->Release();

    // The panels' click.
    g_clickSound = static_cast<I3D_sound *>(driver->CreateFrame(FRAME_SOUND));
    if (ISndGetDriver()->IsInitialized())
        g_soundCache.OpenInto(g_clickSound, "button.wav");
    g_clickSound->SetSoundType(I3DSOUND_AMBIENT);
    g_clickSound->SetOn(false);
    g_clickSound->SetVolume(1.0f);
    g_clickSound->SetName("sound_click");
    g_clickSound->LinkTo(sector, 0);

    LoadActionIcons();
    LoadStateIcons();
    InitPanels();
    InitDownPanel();
    m_flags = INTERFACE_ACTIVE | INTERFACE_PANEL_UP;
    m_pickTime = 0;
    m_pickMisses = 0;
    m_savedFlags = 0;
    m_infoForced = false;
    m_respawnForced = false;
    // (Style 0x4E: not one of the interface's.)
    m_debugText.AttachTo(m_scene, m_scene->FindFrame("Border", FRAME_MASK_ANY));
    m_debugText.AddText(C_fstr("Camera pos: 0,0,0"), 0x4E);
    UpdateCompassShown();
    return true;
}

bool C_Interface::Done()
{
    if (m_flags == 0)
        return false;
    RemoveSquadMembers();
    m_pickedActor = nullptr;
    m_usableActor = nullptr;
    m_debugText.Clear();
    ClearPicks();
    DonePanels();
    if (g_clickSound) {
        g_clickSound->Release();
        g_clickSound = nullptr;
    }
    if (m_camera) {
        m_camera->Release();
        m_camera = nullptr;
    }
    if (m_model) {
        m_model->Release();
        m_model = nullptr;
    }
    if (m_scene) {
        m_scene->SetActiveCamera(nullptr);
        m_scene->Release();
        m_scene = nullptr;
    }
    m_flags = 0;
    for (I3D_texture *&icon : m_stateIcons)
        icon = nullptr;
    for (I3D_texture *&icon : m_stateIcons2)
        icon = nullptr;
    for (I3D_texture *&icon : m_actionIcons)
        icon = nullptr;
    return true;
}

void C_Interface::Reset()
{
    Done();
    Init();
    m_info.ClearHints();
}

bool C_Interface::InitPanels()
{
    m_speedometer.Init(m_scene);
    m_reticle.Init(m_scene);
    m_signals.Init(this, m_scene);
    m_rollMenu.Init(this, m_scene, false);
    for (int i = MEMBER_PANELS; i-- > 0;)
        m_members[i].Init(m_scene, i, m_stateIcons, m_stateIcons2);
    m_messages.Init(m_scene, 50);
    m_crosshair.Init(m_scene);
    m_weaponPanel.Init(m_scene);
    m_stancePanel.Init(m_scene);
    m_compass.Init(m_scene);
    m_cursor.Init(m_scene, GetGame()->GetScene());
    m_map.Init(m_scene, &m_cursor);
    m_inventory.Init(m_scene, &m_cursor);
    m_progress.Init(m_scene);
    m_info.Init(m_scene);
    m_weaponSelector.Init(m_scene);
    m_injuryFlash.Init(m_scene);
    m_bigMessage.Init(m_scene);
    m_photoFlash.Init(m_scene);
    m_objectives.Init(m_scene);
    m_scoreTable.Init(m_scene);
    m_voting.Init(m_scene);
    m_explosiveTimer.Init(m_scene);
    m_carnage.Init(m_scene);
    m_pauseText.Init(m_scene);
    m_countdown.Init(m_scene);
    // The carnage statistics: single player's carnage mode.
    m_carnage.GetFrame()->SetOn(IsLocalGame() && g_playMode->mode == PLAY_MODE_CARNAGE);
    m_infoSelector.Init(m_scene, m_stateIcons, m_actionIcons);
    m_commandPanel.Init(m_scene, this, m_stateIcons, m_actionIcons, &m_infoSelector, &m_cursor);
    return true;
}

void C_Interface::DonePanels()
{
    // (The speedometer's and the inventory's Done are empty.)
    m_reticle.Done();
    m_signals.Done();
    m_rollMenu.Done();
    for (int i = MEMBER_PANELS; i-- > 0;)
        m_members[i].Done();
    m_messages.Done();
    m_crosshair.Done();
    m_weaponPanel.Done();
    m_stancePanel.Done();
    m_compass.Done();
    m_map.Done();
    m_commandPanel.Done();
    m_progress.Done();
    m_info.Done();
    m_weaponSelector.Done();
    m_injuryFlash.Done();
    m_bigMessage.Done();
    m_scoreTable.Done();
    m_voting.Done();
    m_explosiveTimer.Done();
    m_carnage.Done();
    m_pauseText.Done();
    m_countdown.Done();
}

bool C_Interface::InitDownPanel()
{
    m_downPanel = m_scene->FindFrame("down_panel", FRAME_MASK_ANY);
    m_pickedActor = nullptr;
    m_usableActor = nullptr;
    return true;
}

void C_Interface::ClearPicks()
{
    m_pickedActor = nullptr;
    m_usableActor = nullptr;
}

// The member panels' state icons: the textures of the model's "playerstate1_%02d" /
// "playerstate2_%02d" visuals, which are hidden.
void C_Interface::LoadStateIcons()
{
    char name[32];
    for (int i = 0; i < 20; ++i) {
        std::sprintf(name, "playerstate1_%02d", i);
        I3D_frame *frame = m_scene->FindFrame(name, FRAME_MASK_ANY);
        if (!frame) {
            m_stateIcons[i] = nullptr;
            continue;
        }
        m_stateIcons[i] = VisualTexture(frame);
        frame->SetOn(false);
    }
    for (int i = 0; i < 40; ++i) {
        std::sprintf(name, "playerstate2_%02d", i);
        I3D_frame *frame = m_scene->FindFrame(name, FRAME_MASK_ANY);
        if (!frame) {
            m_stateIcons2[i] = nullptr;
            continue;
        }
        frame->SetOn(false);
        m_stateIcons2[i] = VisualTexture(frame);
    }
}

void C_Interface::LoadActionIcons()
{
    char name[32];
    for (int i = 0; i < 39; ++i) {
        std::sprintf(name, "action%02d", i);
        I3D_frame *frame = m_scene->FindFrame(name, FRAME_MASK_ANY);
        if (!frame) {
            m_actionIcons[i] = nullptr;
            continue;
        }
        m_actionIcons[i] = VisualTexture(frame);
        frame->SetOn(false);
    }
}

// ---------------------------------------------------------------------------------------------
// The squad

bool C_Interface::AddSquadMember(C_Human *human, int slot)
{
    if (m_flags == 0 || slot < 0 || slot >= MEMBER_PANELS)
        return false;
    m_members[slot].SetHuman(human);
    m_members[slot].GetFrame()->SetOn(true);
    m_flags |= INTERFACE_MEMBERS;
    if (C_Radar *radar = m_compass.GetRadar())
        radar->Add(human);
    if (human == PlayerTeam()->GetActivePlayer())
        UpdateCompassShown();
    return true;
}

bool C_Interface::RemoveSquadMember(C_Human *human, int slot)
{
    if (m_flags == 0 || slot < 0 || slot >= MEMBER_PANELS)
        return false;
    m_members[slot].GetFrame()->SetOn(false);
    // Quirk kept: only the last member's removal takes the blip off the radar.
    for (int i = MEMBER_PANELS; i-- > 0;) {
        if (m_members[i].GetFrame()->IsOn())
            return true;
    }
    m_flags &= ~INTERFACE_MEMBERS;
    if (C_Radar *radar = m_compass.GetRadar())
        radar->Remove(human);
    return true;
}

bool C_Interface::RemoveSquadMembers()
{
    if (m_flags == 0)
        return false;
    for (int i = MEMBER_PANELS; i-- > 0;) {
        if (C_Radar *radar = m_compass.GetRadar())
            radar->Remove(m_members[i].GetHuman());
        m_members[i].GetFrame()->SetOn(false);
    }
    // (Bit 1 is never set.)
    m_flags &= ~(INTERFACE_MEMBERS | 0x2u);
    return true;
}

void C_Interface::RemoveFromMap(C_Human *human)
{
    if (C_Radar *radar = m_compass.GetRadar())
        radar->Remove(human);
}

void C_Interface::ShowEnemy(C_Human *enemy, bool show)
{
    C_Radar *radar = m_compass.GetRadar();
    if (!radar)
        return;
    if (show)
        radar->Add(enemy);
    else
        radar->Remove(enemy);
}

C_Human *C_Interface::GetShownSquadMember() const
{
    if (m_flags == 0)
        return nullptr;
    return m_members[0].GetHuman();
}

void C_Interface::ShowSquadMember(C_Human *human)
{
    if (human) {
        m_members[0].SetHuman(human);
        m_members[0].GetFrame()->SetOn(true);
        return;
    }
    if (C_Human *selected = PlayerTeam()->GetSelectedMember()) {
        m_members[0].SetHuman(selected);
        m_members[0].GetFrame()->SetOn(true);
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        return;
    }
    m_members[0].GetFrame()->SetOn(false);
}

void C_Interface::HighlightMembers(C_Human *human, int group)
{
    C_Team *team = PlayerTeam();
    // Fix: the original went through all the members, past the six panels with a squad of more
    // (the command map calls this every frame).
    const int count = team->GetMemberCount() < MEMBER_PANELS ? team->GetMemberCount() : MEMBER_PANELS;
    if (group == HIGHLIGHT_COMMANDED) {
        C_Human *commanded = team->GetCommandedMember();
        for (int i = 0; i < count; ++i)
            m_members[i].SetSelected(team->GetMember(i) == commanded);
        return;
    }
    if (group == HIGHLIGHT_NONE) {
        for (int i = 0; i < count; ++i)
            m_members[i].SetSelected(false);
        return;
    }
    std::vector<C_Human *> members;
    GetSquadCommander()->CollectGroup(members, group);
    for (int i = 0; i < count; ++i) {
        C_Human *member = team->GetMember(i);
        bool inGroup = false;
        for (std::size_t j = members.size(); j-- > 0;) {
            if (members[j] == member) {
                inGroup = true;
                break;
            }
        }
        bool selected;
        if (!human)
            selected = inGroup;
        else if (human == member)
            selected = inGroup;
        else
            selected = false;
        m_members[i].SetSelected(selected);
    }
}

void C_Interface::SwitchToMember(int index)
{
    if (m_flags == 0)
        return;
    if (index == PlayerTeam()->GetActiveIndex())
        return;
    PlayerTeam()->SwitchTo(index);
    m_rollMenu.Open(false);
    m_signals.Open(false, nullptr, nullptr);
    // The inventory follows the new soldier.
    if (m_inventory.IsShown()) {
        m_inventory.Show(false);
        m_inventory.Open(1, PlayerTeam()->GetActivePlayer(), nullptr);
        m_inventory.Show(true);
    }
    UpdateCompassShown();
}

void C_Interface::OnMemberClicked(int id)
{
    if (id < MEMBER_ID_FIRST || id >= MEMBER_ID_FIRST + 6)
        return;
    SwitchToMember(id - MEMBER_ID_FIRST);
    if (!m_commandPanel.IsOpen())
        return;
    const uint32_t now = timeGetTime();
    const int elapsed = static_cast<int>(now - s_lastMemberClick);
    s_lastMemberClick = now;
    if (elapsed < DOUBLE_CLICK_TIME) {
        s_lastMemberClick = 0;
        LookAtOnCommandMap(PlayerTeam()->GetCommandedMember());
    }
}

void C_Interface::OnMemberClicked2(int id)
{
    if (id < MEMBER_ID_FIRST || id >= MEMBER_ID_FIRST + 6)
        return;
    PlayerTeam()->SetCommandedIndex(id - MEMBER_ID_FIRST);
    if (!m_commandPanel.IsOpen())
        return;
    const uint32_t now = timeGetTime();
    const int elapsed = static_cast<int>(now - s_lastMemberClick);
    s_lastMemberClick = now;
    if (elapsed < DOUBLE_CLICK_TIME) {
        s_lastMemberClick = 0;
        LookAtOnCommandMap(PlayerTeam()->GetCommandedMember());
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing

void C_Interface::RenderOverlay()
{
    if (GetGame()->GetCameraModule()->HasScriptedView())
        return;
    if (m_commandPanel.IsOpen())
        m_commandPanel.RenderOverlay();
}

void C_Interface::Render()
{
    if (GetGame()->GetCameraModule()->HasScriptedView())
        return;
    // The console's NO_GI.
    if (GetCheatValue(CONSOLE_NO_GI) != 0)
        return;
    I3D_frame *map = m_map.GetFrame();
    if (!(m_flags & INTERFACE_MEMBERS)) {
        // Network games without a soldier: the respawn screen, the scores, the big message.
        if (IsLocalGame())
            return;
        if (m_respawnForced || m_infoForced || m_bigMessage.IsShown()) {
            m_scene->Render(0);
            if (map && map->IsOn())
                m_map.Render();
            if (m_info.IsShown())
                m_info.Render();
            if (m_bigMessage.IsShown())
                m_bigMessage.Render();
        }
        m_messages.Render();
        return;
    }

    DrawPickOutline();
    if (m_pauseText.IsShown())
        m_pauseText.Render();
    if (m_countdown.IsShown())
        m_countdown.Render();
    if (IsLocalGame() && m_commandPanel.IsOpen()) {
        // The command map under the scene, its markers over it.
        m_commandPanel.Render();
        if (m_scene)
            m_scene->Render(0);
        m_commandPanel.RenderMarkers();
    } else if (m_scene) {
        m_scene->Render(0);
    }
    m_crosshair.Render();
    if (m_signals.IsOpen())
        m_signals.Render();
    if (m_rollMenu.IsOpen())
        m_rollMenu.Render();
    if (!m_inventory.IsShown())
        m_messages.Render();
    if (m_flags & (INTERFACE_PANEL_UP | INTERFACE_PANEL_LOW)) {
        if (m_weaponPanel.GetFrame()->IsOn())
            m_weaponPanel.Render();
        for (int i = MEMBER_PANELS; i-- > 0;) {
            if (m_members[i].GetFrame()->IsOn())
                m_members[i].Render();
        }
    }
    if (m_explosiveTimer.GetFrame()->IsOn())
        m_explosiveTimer.Render();
    if (m_carnage.GetFrame()->IsOn())
        m_carnage.Render();
    if (m_inventory.IsShown())
        m_inventory.Render();
    if (map && map->IsOn())
        m_map.Render();
    if (m_info.IsShown())
        m_info.Render();
    if (m_weaponSelector.GetFrame()->IsOn())
        m_weaponSelector.Render();
    if (m_stancePanel.GetFrame()->IsOn())
        m_stancePanel.Render();
    if (m_progress.GetFrame()->IsOn())
        m_progress.Render();
    I3D_frame *scores = m_scoreTable.GetFrame();
    if (scores && m_scoreTable.IsUsed() && scores->IsOn())
        m_scoreTable.Render();
    I3D_frame *voting = m_voting.GetFrame();
    if (voting && voting->IsOn())
        m_voting.Render();
    // Quirk kept: forced, the map and the scores draw a second time.
    if (m_respawnForced || m_infoForced) {
        if (map && map->IsOn())
            m_map.Render();
        if (m_info.IsShown())
            m_info.Render();
    }
    if (m_bigMessage.IsShown())
        m_bigMessage.Render();
}

void C_Interface::DrawPickOutline()
{
    C_Actor *actor = m_pickedActor && m_signals.IsOpen() ? m_pickedActor : m_usableActor;
    if (!actor || m_commandPanel.IsOpen())
        return;
    // The actor's box on the screen.
    S_vector4 corners[8];
    actor->GetBoxCorners(corners);
    S_vector4 screen[8];
    GetGame()->GetScene()->TransformPoints(corners, screen, 8);
    const float width = static_cast<float>(GetIGraph()->ScrnX());
    const float height = static_cast<float>(GetIGraph()->ScrnY());
    float left = width;
    float right = 0.0f;
    float bottom = 0.0f;
    float top = height;
    for (const S_vector4 &point : screen) {
        if (right < point.x)
            right = point.x;
        if (left > point.x)
            left = point.x;
        if (bottom < point.y)
            bottom = point.y;
        if (top > point.y)
            top = point.y;
    }
    if (left < OUTLINE_MARGIN)
        left = OUTLINE_MARGIN;
    if (right > width - OUTLINE_MARGIN)
        right = width - OUTLINE_MARGIN;
    if (top < OUTLINE_MARGIN)
        top = OUTLINE_MARGIN;
    if (bottom > height - OUTLINE_MARGIN)
        bottom = height - OUTLINE_MARGIN;
    float boxWidth = right - left;
    float boxHeight = bottom - top;
    const float minWidth = width * OUTLINE_MIN_SIZE;
    const float minHeight = height * OUTLINE_MIN_SIZE;
    if (boxWidth < minWidth) {
        const float pad = (minWidth - boxWidth) * 0.5f;
        left -= pad;
        right += pad;
        boxWidth = minWidth;
    }
    if (boxHeight < minHeight) {
        const float pad = (minHeight - boxHeight) * 0.5f;
        top -= pad;
        bottom += pad;
        boxHeight = minHeight;
    }
    const float cornerWidth = boxWidth * 0.25f;
    const float cornerHeight = boxHeight * 0.25f;

    // Eight lines: two at each corner.
    const float points[16][2] = {
        {left, top}, {left, top + cornerHeight},
        {left, top}, {left + cornerWidth, top},
        {right, top}, {right, top + cornerHeight},
        {right, top}, {right - cornerWidth, top},
        {right, bottom}, {right, bottom - cornerHeight},
        {right - cornerWidth, bottom}, {right, bottom},
        {left, bottom}, {left, bottom - cornerHeight},
        {left, bottom}, {left + cornerWidth, bottom},
    };
    S_TLVertex vertices[16];
    for (int i = 0; i < 16; ++i) {
        S_TLVertex &vertex = vertices[i];
        vertex.x = points[i][0];
        vertex.y = points[i][1];
        vertex.z = 0.5f;
        vertex.rhw = 2.0f;
        vertex.color = OUTLINE_COLOR;
        // (The original leaves the specular colour unset.)
        vertex.specular = 0;
        vertex.u = 0.0f;
        vertex.v = 0.0f;
    }
    GetIGraph()->SetTexture(nullptr, 0);
    GetIGraph()->DrawPrimitives(1, 8, vertices, 1);
}

// ---------------------------------------------------------------------------------------------
// Updates

void C_Interface::Update(int timeDelta)
{
    if (m_flags == 0)
        return;
    if (!(m_flags & INTERFACE_MEMBERS)) {
        m_messages.Update(timeDelta);
        m_crosshair.Show(false);
        return;
    }
    if (m_countdown.IsShown())
        m_countdown.Update(timeDelta);
    // "Paused" (not over the command map).
    m_pauseText.SetShown(GetGame()->IsPaused() && !m_commandPanel.IsOpen());
    C_Human *human = PlayerTeam()->GetActivePlayer();
    if (human && human->IsInventoryChanged())
        UpdateCompassShown();
    if (m_injuryFlash.GetFrame()->IsOn())
        m_injuryFlash.Update(timeDelta);
    m_cursor.Update(timeDelta);
    if (m_info.IsShown())
        m_info.Update(timeDelta);
    I3D_frame *map = m_map.GetFrame();
    if (map && map->IsOn())
        m_map.Update(timeDelta);
    I3D_frame *photo = m_photoFlash.GetFrame();
    if (photo && photo->IsOn())
        m_photoFlash.Update(timeDelta);
    if (IsLocalGame()) {
        if (m_commandPanel.IsOpen()) {
            C_Human *commanded = PlayerTeam()->GetCommandedMember();
            m_infoSelector.Update(commanded ? commanded : human);
        } else {
            m_infoSelector.Update(human);
            m_crosshair.Update(timeDelta);
        }
        m_messages.Update(timeDelta);
        UpdatePick(timeDelta);
    } else {
        UpdatePick(timeDelta);
        m_crosshair.Update(timeDelta);
        m_messages.Update(timeDelta);
    }
    if (m_signals.IsOpen())
        m_signals.Update(timeDelta);
    else if (m_rollMenu.IsOpen())
        m_rollMenu.Update(timeDelta);
    else if (m_weaponSelector.GetFrame()->IsOn())
        m_weaponSelector.Update(timeDelta);
    m_progress.Update(human);
    if (m_flags & (INTERFACE_PANEL_UP | INTERFACE_PANEL_LOW)) {
        m_weaponPanel.Update(timeDelta);
        if (!GetGame()->IsPaused())
            m_stancePanel.Update(timeDelta);
        for (int i = MEMBER_PANELS; i-- > 0;) {
            if (m_members[i].GetFrame()->IsOn())
                m_members[i].Update();
        }
    }
    m_explosiveTimer.Update(timeDelta);
    m_carnage.Update(timeDelta);
    if (m_flags & (INTERFACE_PANEL_UP | INTERFACE_PANEL_LOW))
        m_compass.Update(timeDelta);
    if (m_commandPanel.IsOpen())
        m_commandPanel.Update(timeDelta);
    if (m_inventory.IsShown())
        m_inventory.Update(timeDelta);

    // The speedometer: in a car, with the lower panel up and nothing open.
    I3D_frame *speedometer = m_speedometer.GetFrame();
    if ((m_flags & INTERFACE_PANEL_DOWN) || m_commandPanel.IsOpen() || m_inventory.IsShown() ||
        m_rollMenu.IsOpen()) {
        speedometer->SetOn(false);
    } else {
        // Fix: the original read the vehicle of a missing soldier.
        C_Actor *vehicle = human ? human->GetVehicle() : nullptr;
        if (!vehicle || vehicle->GetType() != ACTOR_CAR) {
            speedometer->SetOn(false);
        } else {
            speedometer->SetOn(true);
            auto *car = static_cast<C_Car *>(vehicle);
            C_Vehicle *physics = car->GetVehicle();
            const float fuel = physics->GetFuel() / physics->GetTankSize();
            m_speedometer.Update(car->GetSpeed() * 3.6f, physics->GetEngineRpm(), fuel);
        }
    }
    m_scoreTable.Update(timeDelta);
    // (A call of an empty function, 0x004af470, follows.)
}

void C_Interface::UpdateCrosshair()
{
    // Spectating / waiting: none.
    if (m_flags & (INTERFACE_SCREEN_1 | INTERFACE_SCREEN_2)) {
        m_crosshair.SetTarget(nullptr, true);
        m_crosshair.Show(false);
        return;
    }
    if (m_commandPanel.IsOpen() || GetGame()->GetCameraModule()->HasScriptedView()) {
        m_crosshair.Show(false);
        return;
    }
    C_Human *human = PlayerTeam()->GetActivePlayer();
    // Fix: the original went on without an active soldier.
    if (!human) {
        m_crosshair.Show(false);
        return;
    }
    if (C_Actor *vehicle = human->GetVehicle()) {
        switch (vehicle->GetType()) {
        case ACTOR_CAR: {
            const uint32_t seat = vehicle->GetSeatFlags(human->GetVehicleSeat());
            if (seat & SEAT_FLAG_WEAPON) {
                // A seat weapon: its own sights in the first-person view.
                if (vehicle->IsSeatFirstPersonOnly(human->GetVehicleSeat())) {
                    m_crosshair.Show(false);
                    return;
                }
                m_crosshair.Show(!GetGame()->IsFirstPersonView());
                return;
            }
            if (!(seat & SEAT_FLAG_ARMED)) {
                m_crosshair.Show(false);
                return;
            }
            break;
        }
        case ACTOR_STATIC_WEAPON: {
            const bool firstPerson = GetGame()->IsFirstPersonView();
            const bool ownSights = vehicle->IsSeatFirstPersonOnly(0);
            m_crosshair.Show(!ownSights && !firstPerson);
            return;
        }
        case ACTOR_JUNKERS88:
            m_crosshair.Show(false);
            return;
        default:
            break;
        }
    }
    // Not through a scope's reticle, not with a launcher, not in the player's view modes 1..3.
    if (C_WeaponItem *weapon = human->GetHandsWeapon()) {
        C_ItemDef *def = weapon->GetDef();
        if (def->GetBehaviour() == ITEM_BEHAVIOUR_SCOPED) {
            C_ItemComponent *scope = def->GetSecondaryComponent();
            if (scope && scope->GetKind() == ICK_SCOPE &&
                static_cast<C_ScopeComp *>(scope)->GetAimMode() < AIM_MODE_BINOCULARS) {
                m_crosshair.Show(false);
                return;
            }
        }
        if (weapon->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER) {
            m_crosshair.Show(false);
            return;
        }
    }
    const int viewMode = human->GetPlayerViewMode();
    m_crosshair.Show(viewMode < 1 || viewMode > 3);
}

void C_Interface::UpdatePick(int timeDelta)
{
    UpdateCrosshair();
    if (m_commandPanel.IsOpen())
        return;
    m_pickTime += timeDelta;
    if (IsLocalGame() && m_pickTime < PICK_INTERVAL)
        return;
    m_pickTime = 0;
    C_Actor *picked = nullptr;
    C_Human *human = PlayerTeam()->GetActivePlayer();
    // Fix: the original went on without an active soldier.
    if (!human)
        return;

    // A ray along the camera's view from the human's camera dummy, as far as the view reaches
    // (the human's own volumes off).
    S_vector4 dir4;
    GetGame()->GetCameraModule()->GetCameraDir(0, &dir4);
    I3D_frame *cameraDummy =
        human->GetFrame()->FindChildFrame(GetGame()->IsFirstPersonView() ? "cameraFPV" : "camera3RD", FRAME_MASK_DUMMY);
    S_vector from = cameraDummy->GetWorldPos();
    S_vector dir(dir4.x, dir4.y, dir4.z);
    if (human->GetVehicle()) {
        const float aheadZ = dir.z * PICK_VEHICLE_AHEAD;
        from.x = dir.x * PICK_VEHICLE_AHEAD + from.x;
        from.y = from.y + dir.y * PICK_VEHICLE_AHEAD;
        from.z = from.z + aheadZ;
    }
    const float range = GetGame()->GetCameraModule()->GetViewDistance();
    dir.x *= range;
    dir.y *= range;
    dir.z *= range;
    S_vector hit;
    human->RemoveCollision(false);
    S_CollisionVolume *volume = g_collisionWorld.RayTest(from, dir, &hit, nullptr, 0, PICK_EXCLUDE);
    human->RestoreCollision(false);
    if (volume && (volume->flags & COLLISION_FLAG_DYNAMIC))
        picked = volume->actor;
    I3D_frame *body = human->GetBodyFrame();
    const S_vector &bodyPos = body->GetWorldPos();

    float distance;
    if (picked) {
        distance = Distance(hit, bodyPos);
        m_pickMisses = 0;
    } else {
        // The last actor stays for a few misses.
        if (m_pickedActor && ++m_pickMisses < PICK_MISSES)
            return;
        distance = 0.0f;
    }

    bool usable;
    C_Actor *target = picked;
    bool behind = false;
    if (volume && (volume->flags & PICK_SEE_THROUGH)) {
        // Through water or a shoot-through part: a human behind it is pointed at (usable within
        // 2 m), though the pick stays the first actor.
        human->RemoveCollision(false);
        S_CollisionVolume *second = g_collisionWorld.RayTest(from, dir, &hit, nullptr, 0, PICK_SEE_THROUGH);
        human->RestoreCollision(false);
        if (second != volume && second && (second->flags & COLLISION_FLAG_DYNAMIC)) {
            C_Actor *actor = second->actor;
            if (!actor || actor->GetType() == ACTOR_HUMAN) {
                // Quirk kept: a hit without an actor still replaces the distance (and the hit).
                distance = Distance(hit, body->GetWorldPos());
                if (actor) {
                    m_rollMenu.SetTarget(nullptr, distance);
                    m_crosshair.SetTarget(actor, distance <= PICK_HUMAN_BEHIND_RANGE);
                    m_usableActor = nullptr;
                    behind = true;
                }
            }
        }
    }
    if (!behind) {
        // A door is only picked near.
        if (target && target->GetType() == ACTOR_DOOR && static_cast<C_Door *>(target)->GetUnknown118() &&
            distance > PICK_DOOR_RANGE)
            target = nullptr;
        C_Actor *vehicle = human->GetVehicle();
        if (!vehicle || !IsSeatVehicle(vehicle))
            m_rollMenu.SetTarget(target, distance);
        if (m_rollMenu.HasActions(target, distance)) {
            m_crosshair.SetTarget(target, true);
            m_usableActor = target;
        } else {
            usable = distance <= PICK_USE_RANGE;
            m_crosshair.SetTarget(target, usable);
            m_usableActor = nullptr;
        }
        picked = target;
    }
    m_pickedActor = picked;
    // In a vehicle: its actions, nothing picked.
    if (C_Actor *vehicle = human->GetVehicle()) {
        if (IsSeatVehicle(vehicle)) {
            m_rollMenu.SetTarget(vehicle, 1.0f);
            m_pickedActor = nullptr;
            m_usableActor = nullptr;
        }
    }
    if (m_signals.IsOpen())
        m_signals.SetTarget(m_pickedActor);
}

void C_Interface::UpdateCompassShown()
{
    bool show = false;
    if (IsLocalGame() && !(m_flags & (INTERFACE_SCREEN_1 | INTERFACE_SCREEN_2 | INTERFACE_PANEL_DOWN)) &&
        !m_inventory.IsShown() && m_messages.GetMode() != 1 && !m_commandPanel.IsOpen()) {
        C_Human *human = PlayerTeam()->GetActivePlayer();
        show = human && human->FindItem(ITEM_COMPASS) != nullptr;
    }
    m_compass.Show(show);
}

// ---------------------------------------------------------------------------------------------
// Input

void C_Interface::ReturnControlOnInput(float * /*held*/, float *pressed)
{
    if (m_flags == 0)
        return;
    // The soldier's controls (0..17, 19) and the signals (39..45).
    for (int action = 63; action >= 0; --action) {
        const bool control = action <= 0x11 || action == INPUT_ROLL_MENU ||
                             (action >= INPUT_SIGNAL_ALL && action <= INPUT_MENU - 1);
        if (control && pressed[action] != 0.0f)
            PlayerTeam()->ReturnControl();
    }
}

bool C_Interface::CloseAll()
{
    const bool wasOpen = m_messages.GetMode() == 1 || m_signals.IsOpen() || m_rollMenu.IsOpen() ||
                         m_weaponSelector.GetFrame()->IsOn() || m_inventory.IsShown();
    if (m_messages.GetMode() == 1)
        m_messages.SetMode(0);
    if (m_messages.IsInputOpen())
        m_messages.OpenInput(false);
    m_signals.Open(false, nullptr, nullptr);
    m_rollMenu.Open(false);
    m_weaponSelector.Show(false);
    ShowInventory(false, nullptr);
    m_cursor.SetMode(m_commandPanel.IsOpen() ? 0 : -1);
    PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
    return wasOpen;
}

void C_Interface::HandleInput(float *held, float *pressed)
{
    if (m_flags == 0)
        return;
    // The menu key closes the open panels first (the game's menu waits for the next press).
    if (pressed[INPUT_MENU] != 0.0f) {
        if (CloseAll())
            pressed[INPUT_MENU] = 0.0f;
        return;
    }
    C_game *game = GetGame();
    if (pressed[INPUT_PAUSE] != 0.0f && !m_commandPanel.IsOpen() && !m_messages.IsInputOpen() &&
        m_messages.GetMode() != 1)
        game->SetPaused(!game->IsPaused());
    if (game->IsPaused() && !m_commandPanel.IsOpen() && m_messages.GetMode() != 1)
        return;
    if (game->GetCameraModule()->HasScriptedView())
        return;

    // The messages' history; while it or the chat line is open the soldier gets no keys.
    if (m_commandPanel.IsOpen() || pressed[INPUT_MESSAGE_LOG] == 0.0f) {
        if (m_messages.GetMode() == 1 || m_messages.IsInputOpen()) {
            ClearAllControls(held, pressed);
            ClearMoveControls(held, pressed);
            return;
        }
    } else if (m_messages.GetMode() == 1) {
        m_messages.SetMode(0);
    } else {
        m_messages.SetMode(1);
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        m_signals.Open(false, nullptr, nullptr);
        m_rollMenu.Open(false);
        m_weaponSelector.Show(false);
        m_inventory.Show(false);
        m_info.Show(false);
        m_map.Show(false, false);
        m_cursor.SetMode(-1);
    }

    // The chat line.
    I3D_frame *map = m_map.GetFrame();
    if (!map || !map->IsOn()) {
        if (pressed[INPUT_SAY] != 0.0f) {
            m_inventory.Show(false);
            m_cursor.SetMode(-1);
            m_messages.StartSay();
        } else if (pressed[INPUT_SAY_TEAM] != 0.0f) {
            m_inventory.Show(false);
            m_cursor.SetMode(-1);
            m_messages.StartSayTeam();
        }
    }
    m_cursor.HandleInput(held, pressed);

    // The map while its key is held (the respawn screen: always).
    if (pressed[INPUT_MAP] != 0.0f) {
        m_map.Show(true, m_commandPanel.IsOpen());
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        m_signals.Open(false, nullptr, nullptr);
        m_rollMenu.Open(false);
        m_weaponSelector.Show(false);
    }
    map = m_map.GetFrame();
    if (map && map->IsOn()) {
        if (m_respawnForced || held[INPUT_MAP] != 0.0f) {
            m_map.HandleInput(held, pressed);
            return;
        }
        m_map.Show(false, false);
    }

    // The information / scores while their key is held.
    if (pressed[INPUT_INFO] != 0.0f) {
        m_info.Show(true);
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        m_signals.Open(false, nullptr, nullptr);
        m_rollMenu.Open(false);
        m_weaponSelector.Show(false);
    } else if (!m_infoForced && held[INPUT_INFO] == 0.0f && m_info.IsShown()) {
        m_info.Show(false);
    }

    if (!(m_flags & INTERFACE_MEMBERS))
        return;
    C_Team *team = PlayerTeam();
    if (IsLocalGame() && !m_commandPanel.IsOpen() && !team->GetSelectedMember())
        ReturnControlOnInput(held, pressed);
    if (m_signals.IsOpen()) {
        m_signals.HandleInput(held, pressed);
        if (!m_signals.IsOpen())
            PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        return;
    }
    if (m_rollMenu.IsOpen()) {
        m_rollMenu.HandleInput(held, pressed);
        return;
    }

    // The next soldier (Shift: the previous one; not with Alt).
    if (IsLocalGame() && pressed[INPUT_SWITCH_SOLDIER] != 0.0f) {
        IGraph *graph = GetIGraph();
        if (!graph->IsKeyDown(KEY_LALT) && !graph->IsKeyDown(KEY_RALT)) {
            const bool previous = graph->IsKeyDown(KEY_LSHIFT) || graph->IsKeyDown(KEY_RSHIFT);
            SwitchToMember(previous ? team->GetPreviousControllable() : team->GetNextControllable());
        }
    }
    if (pressed[INPUT_LOWER_PANEL] != 0.0f && !m_commandPanel.IsOpen())
        CycleDownPanel();
    if (m_weaponSelector.GetFrame()->IsOn()) {
        m_weaponSelector.HandleInput(held, pressed);
        return;
    }
    if ((pressed[INPUT_WEAPON_PREV] != 0.0f || pressed[INPUT_WEAPON_NEXT] != 0.0f) && !m_commandPanel.IsOpen() &&
        !m_inventory.IsShown() && team->GetSelectedMember()) {
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        m_weaponSelector.Show(true);
        m_signals.Open(false, nullptr, nullptr);
        m_rollMenu.Open(false);
        m_info.Show(false);
        m_map.Show(false, false);
    }
    if (pressed[INPUT_INVENTORY] != 0.0f)
        ShowInventory(!m_inventory.IsShown(), nullptr);
    if (IsLocalGame() && pressed[INPUT_COMMAND_MAP] != 0.0f) {
        OpenCommandMap(!(m_commandPanel.IsOpen() && m_messages.GetMode() != 1));
        return;
    }

    // The command map or the inventory takes the keys.
    if (m_commandPanel.IsOpen() || m_inventory.IsShown()) {
        if (!m_inventory.IsShown()) {
            if (m_commandPanel.IsOpen())
                m_commandPanel.HandleInput(held, pressed);
        } else if (pressed[INPUT_ROLL_MENU] == 0.0f) {
            m_inventory.HandleInput(held, pressed);
        } else {
            ShowInventory(false, nullptr);
        }
        if (pressed[INPUT_CLICK] != 0.0f || pressed[INPUT_CLICK2] != 0.0f) {
            if (const int id = m_cursor.GetHoveredId()) {
                if (pressed[INPUT_CLICK] != 0.0f)
                    OnMemberClicked(id);
                else if (pressed[INPUT_CLICK2] != 0.0f)
                    OnMemberClicked2(id);
            }
        }
        ClearAllControls(held, pressed);
        ClearMoveControls(held, pressed);
        return;
    }

    if (pressed[INPUT_ROLL_MENU] != 0.0f) {
        // The actions on the actor under the crosshair.
        m_rollMenu.Prepare();
        m_rollMenu.Open(true);
        if (!m_rollMenu.IsOpen())
            return;
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
        m_signals.Open(false, nullptr, nullptr);
    } else {
        // The signals to the squad or to a soldier; or the control of soldier 1..4.
        if (pressed[INPUT_SIGNAL_ALL] != 0.0f) {
            m_signals.Open(true, m_pickedActor, nullptr);
        } else {
            int slot = -1;
            for (int i = 0; i < 6; ++i) {
                if (pressed[INPUT_SIGNAL_SOLDIER_1 + i] != 0.0f) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                for (int i = 0; i < 4; ++i) {
                    if (pressed[INPUT_SELECT_SOLDIER_1 + i] == 0.0f)
                        continue;
                    C_Human *member = team->GetMember(i);
                    if (!member || member->IsDead() || member->IsLocalPlayer())
                        return;
                    SwitchToMember(i);
                    team->ReturnControl();
                    return;
                }
                return;
            }
            C_Human *member = team->GetMember(slot);
            if (member && !member->IsLocalPlayer())
                m_signals.Open(true, m_pickedActor, member);
        }
        if (!m_signals.IsOpen())
            return;
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS_SIGNALS);
        m_rollMenu.Open(false);
    }
    m_weaponSelector.Show(false);
    m_info.Show(false);
    m_map.Show(false, false);
}

void C_Interface::FilterInput(float *held, float *pressed)
{
    if (!m_rollMenu.IsOpen() && !m_signals.IsOpen())
        return;
    // The numeric keypad's keys 0..9 (they choose the menus' entries).
    static const int KEYPAD_KEYS[] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};
    for (const int key : KEYPAD_KEYS)
        g_controls.ClearKey(key, held, pressed);
}

bool C_Interface::IsCapturingInput() const
{
    return m_commandPanel.IsOpen() || m_inventory.IsShown() || m_messages.IsInputOpen();
}

void C_Interface::AddMessage(const S_vector &color, const char *text)
{
    if (m_flags != 0)
        m_messages.AddMessage(color, text);
}

// ---------------------------------------------------------------------------------------------
// Panels

void C_Interface::ShowInventory(bool show, C_Actor *container)
{
    if (!show) {
        if (!m_commandPanel.IsOpen())
            m_cursor.SetMode(-1);
        m_inventory.Show(false);
        UpdateCompassShown();
        return;
    }
    C_Team *team = PlayerTeam();
    C_Human *human;
    int mode = 1;
    if (IsLocalGame()) {
        human = team->GetActivePlayer();
        if (container)
            mode = 3;
    } else {
        human = team->GetSelectedMember();
        if (!human) {
            UpdateCompassShown();
            return;
        }
        if (g_gameProfile.IsCooperativeGame() && container) {
            // The container's items come from the server.
            human->PlayerRequestEquipment(container->GetNetId());
            mode = 3;
        } else {
            container = nullptr;
        }
    }
    m_inventory.Open(mode, human, container);
    m_signals.Open(false, nullptr, nullptr);
    m_rollMenu.Open(false);
    m_weaponSelector.Show(false);
    m_info.Show(false);
    m_map.Show(false, false);
    m_cursor.SetMode(0);
    m_inventory.Show(show);
    UpdateCompassShown();
}

void C_Interface::CycleDownPanel()
{
    if (m_flags & INTERFACE_PANEL_UP) {
        m_downPanel->SetPos(S_vector(DOWN_PANEL_X, DOWN_PANEL_LOW, 0.0f));
        m_flags = (m_flags & ~INTERFACE_PANEL_UP) | INTERFACE_PANEL_LOW;
        if (IsLocalGame()) {
            if (g_playMode->mode == PLAY_MODE_CARNAGE)
                m_carnage.GetFrame()->SetOn(false);
        } else {
            m_scoreTable.GetFrame()->SetOn(false);
        }
    } else if (m_flags & INTERFACE_PANEL_LOW) {
        m_downPanel->SetPos(S_vector(DOWN_PANEL_X, DOWN_PANEL_DOWN, 0.0f));
        m_flags = (m_flags & ~INTERFACE_PANEL_LOW) | INTERFACE_PANEL_DOWN;
    } else if (m_flags & INTERFACE_PANEL_DOWN) {
        m_downPanel->SetPos(S_vector(DOWN_PANEL_X, DOWN_PANEL_UP, 0.0f));
        m_flags = (m_flags & ~INTERFACE_PANEL_DOWN) | INTERFACE_PANEL_UP;
        if (IsLocalGame()) {
            if (g_playMode->mode == PLAY_MODE_CARNAGE)
                m_carnage.GetFrame()->SetOn(true);
        } else {
            m_scoreTable.GetFrame()->SetOn(m_scoreTable.IsUsed());
        }
    }
    m_downPanel->Update();
    for (int i = MEMBER_PANELS; i-- > 0;)
        m_members[i].Relayout();
    m_weaponPanel.Relayout();
    m_signals.Relayout();
    m_rollMenu.Relayout();
    m_stancePanel.Relayout();
    UpdateCompassShown();
}

// The lower panel back at its position (after the command map).
void C_Interface::RefreshDownPanel()
{
    float y = 0.0f;
    bool place = true;
    if (m_flags & INTERFACE_PANEL_LOW)
        y = DOWN_PANEL_LOW;
    else if (m_flags & INTERFACE_PANEL_DOWN)
        y = DOWN_PANEL_DOWN;
    else if (m_flags & INTERFACE_PANEL_UP)
        y = DOWN_PANEL_UP;
    else
        place = false;
    if (place)
        m_downPanel->SetPos(S_vector(DOWN_PANEL_X, y, 0.0f));
    m_downPanel->Update();
    for (int i = MEMBER_PANELS; i-- > 0;)
        m_members[i].Relayout();
    m_weaponPanel.Relayout();
    m_signals.Relayout();
    m_rollMenu.Relayout();
    // (The stance panel is not laid out again here.)
    UpdateCompassShown();
}

void C_Interface::SetViewMode(int viewMode)
{
    switch (viewMode) {
    case 0:
    case 2:
        m_reticle.SetMode(AIM_MODE_NONE);
        break;
    case VIEW_BINOCULARS:
        m_reticle.SetMode(AIM_MODE_BINOCULARS);
        break;
    case VIEW_SCOPE: {
        C_WeaponItem *weapon = PlayerTeam()->GetActivePlayer()->GetHandsWeapon();
        m_reticle.SetMode(static_cast<C_ScopeComp *>(weapon->GetDef()->GetSecondaryComponent())->GetAimMode());
        break;
    }
    default:
        break;
    }
}

void C_Interface::SetTimer(int ms)
{
    m_stancePanel.Timer() = ms;
}

void C_Interface::ShowRoundResult(int textId, int axisWins, int alliedWins)
{
    m_bigMessage.ShowRoundResult(true, textId, axisWins, alliedWins);
}

void C_Interface::ShowCountdown(int textId, int ms)
{
    m_bigMessage.ShowCountdown(true, textId, ms);
}

void C_Interface::ShowMessage(int textId)
{
    m_bigMessage.Show(textId >= 0, textId);
}

void C_Interface::ShowTimer(int textId, int ms)
{
    if (textId == -1)
        m_countdown.Show(false, -1, ms);
    else
        m_countdown.Show(true, textId, ms);
}

void C_Interface::ShowSoundDirection(const S_vector &dir, int chapter)
{
    m_compass.ShowSound(dir, chapter);
}

void C_Interface::ShowScores(bool show)
{
    m_info.Show(show);
    m_infoForced = show;
}

void C_Interface::ShowRespawnScreen(bool show)
{
    m_map.Show(show, false);
    I3D_frame *map = m_map.GetFrame();
    m_respawnForced = map && map->IsOn();
    if (show)
        m_messages.SetMode(0);
}

bool C_Interface::IsRespawnScreenShown() const
{
    if (!(m_flags & INTERFACE_ACTIVE))
        return false;
    I3D_frame *map = m_map.GetFrame();
    return map && map->IsOn();
}

void C_Interface::FlashDamage()
{
    m_injuryFlash.Start(true);
}

void C_Interface::SetObjectiveIcon(int index, int state)
{
    if (m_flags != 0)
        m_objectives.SetIcon(index, state);
}

void C_Interface::ShowPhotoFlash()
{
    m_photoFlash.Start(true);
}

void C_Interface::RefreshInventory()
{
    if (m_inventory.IsShown())
        m_inventory.Refresh();
}

void C_Interface::OnActorEvent(int event, C_Actor *actor)
{
    if (m_flags == 0 || !IsClient())
        return;
    if (event == INTERFACE_ACTOR_DELETED) {
        // Fix: a deleted human's radar blip goes with it (the original's blip kept the pointer
        // and read the freed human while it faded out).
        if (actor->GetType() == ACTOR_HUMAN) {
            if (C_Radar *radar = m_compass.GetRadar())
                radar->Forget(static_cast<C_Human *>(actor));
        }
        if (actor == m_usableActor)
            m_usableActor = nullptr;
        if (actor == m_pickedActor)
            m_pickedActor = nullptr;
        if (m_rollMenu.GetTarget() == actor) {
            m_rollMenu.SetTarget(nullptr, 0.0f);
            m_signals.OnTargetDeleted(actor);
        }
    } else if (event == INTERFACE_ACTOR_DROPPED) {
        if (m_rollMenu.GetTarget() == actor)
            m_rollMenu.SetTarget(nullptr, 0.0f);
    } else {
        return;
    }
    if (m_commandPanel.GetTarget() == actor)
        m_commandPanel.ClearTarget();
}

// 0 the soldier's screen; 1 spectating (the soldier's panels off, the objectives on); 2 waiting
// (everything off).
void C_Interface::SetScreen(int screen)
{
    if (screen == 2) {
        m_flags = (m_flags & ~INTERFACE_SCREEN_1) | INTERFACE_SCREEN_2;
        m_inventory.Show(false);
        m_speedometer.GetFrame()->SetOn(false);
        m_reticle.SetMode(AIM_MODE_NONE);
        m_signals.Open(false, nullptr, nullptr);
        m_rollMenu.Open(false);
        m_messages.SetMode(0);
        m_weaponPanel.GetFrame()->SetOn(false);
        m_explosiveTimer.GetFrame()->SetOn(false);
        m_stancePanel.GetFrame()->SetOn(false);
        m_map.Show(false, false);
        m_cursor.SetMode(-1);
        m_scoreTable.GetFrame()->SetOn(false);
        // (The original switches the objectives off twice.)
        if (I3D_frame *objectives = m_objectives.GetFrame())
            objectives->SetOn(false);
    } else if (screen == 1) {
        m_flags = (m_flags & ~INTERFACE_SCREEN_2) | INTERFACE_SCREEN_1;
        m_stancePanel.GetFrame()->SetOn(false);
        m_weaponPanel.GetFrame()->SetOn(false);
        if (I3D_frame *objectives = m_objectives.GetFrame())
            objectives->SetOn(true);
        m_explosiveTimer.GetFrame()->SetOn(false);
    } else if (screen == 0) {
        m_flags &= ~(INTERFACE_SCREEN_1 | INTERFACE_SCREEN_2);
        m_weaponPanel.GetFrame()->SetOn(true);
        m_stancePanel.GetFrame()->SetOn(true);
        m_scoreTable.GetFrame()->SetOn(m_scoreTable.IsUsed());
        if (I3D_frame *objectives = m_objectives.GetFrame())
            objectives->SetOn(true);
        m_explosiveTimer.GetFrame()->SetOn(true);
    }
    UpdateCompassShown();
}

// The command map: the lower panel up and the first-person view off meanwhile (both come back).
void C_Interface::OpenCommandMap(bool open)
{
    if (open) {
        if (!GetGame()->IsTacticalMode())
            return;
        m_savedFlags = m_flags & INTERFACE_PANEL_MASK;
        m_flags = (m_flags & ~(INTERFACE_PANEL_LOW | INTERFACE_PANEL_DOWN)) | INTERFACE_PANEL_UP;
        RefreshDownPanel();
        if (GetGame()->IsFirstPersonView()) {
            m_savedFlags |= INTERFACE_FPV_SAVED;
            PlayerTeam()->GetActivePlayer()->PlayerSetViewMode(0);
            GetGame()->ShowFirstPersonView(false);
        }
        PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS_COMMAND_MAP);
        m_commandPanel.Open(true);
        m_inventory.Show(false);
        m_signals.Open(false, nullptr, nullptr);
        m_rollMenu.Open(false);
        m_weaponSelector.Show(false);
        m_info.Show(false);
        m_map.Show(false, false);
        m_crosshair.SetTarget(nullptr, false);
        m_cursor.SetMode(0);
    } else {
        if (m_commandPanel.IsOpen()) {
            m_flags ^= (m_flags ^ m_savedFlags) & INTERFACE_PANEL_MASK;
            RefreshDownPanel();
            if (m_savedFlags & INTERFACE_FPV_SAVED)
                GetGame()->ShowFirstPersonView(true);
            PlaceExplosiveTimer(m_explosiveTimer, TIMER_POS);
            m_commandPanel.Open(false);
            m_inventory.Show(false);
            m_cursor.SetMode(-1);
        }
    }
    UpdateCompassShown();
}

// ---------------------------------------------------------------------------------------------
// Saved games

// Chunk 1000 (the commanded member, the command map open), 0x3E9 (the hints), 0x3EA (16 bytes,
// see s_saveStamp), 0x3EB (the stance panel's timer).
bool C_Interface::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_INTERFACE);
    chunk.Write(PlayerTeam()->GetCommandedIndex());
    chunk.Write(static_cast<int>(m_commandPanel.IsOpen()));
    chunk.Ascend();
    chunk.Begin(CHUNK_INTERFACE_HINTS);
    m_info.SaveHints(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_INTERFACE_STAMP);
    chunk.Write(s_saveStamp, sizeof(s_saveStamp));
    chunk.Ascend();
    chunk.Begin(CHUNK_INTERFACE_TIMER);
    chunk.Write(m_stancePanel.Timer());
    chunk.Ascend();
    return true;
}

bool C_Interface::LoadGame(C_chunk &chunk)
{
    while (chunk.RemainingSize() > 0) {
        uint16_t id;
        chunk.Descend(id);
        switch (id) {
        case CHUNK_INTERFACE: {
            int value = 0;
            chunk.Read(value);
            if (value >= 0)
                PlayerTeam()->SetCommandedIndex(value);
            chunk.Read(value);
            if (value == 1)
                OpenCommandMap(true);
            chunk.Ascend();
            break;
        }
        case CHUNK_INTERFACE_HINTS:
            m_info.LoadHints(chunk);
            chunk.Ascend();
            break;
        case CHUNK_INTERFACE_STAMP:
            if (chunk.RemainingSize() >= static_cast<int>(sizeof(s_saveStamp)))
                chunk.Read(s_saveStamp, sizeof(s_saveStamp));
            chunk.Ascend();
            break;
        case CHUNK_INTERFACE_TIMER: {
            int timer = 0;
            chunk.Read(timer);
            chunk.Ascend();
            m_stancePanel.Timer() = timer;
            break;
        }
        default:
            // Quirk kept: an unknown chunk is not left (its contents are read as chunks).
            break;
        }
    }
    return true;
}
