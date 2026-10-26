// The map of the interface (see interface.h; the original's 0x004a4ae0..0x004a8020): the
// mission's map model (missions\<mission>\map) in the interface scene with the markers of the
// squad or the team, the seen enemies, the pick-ups, the VIP, the objectives and the spawn
// zones. In multiplayer it is also the respawn screen: a click on a zone of the player's side
// picks where to spawn.
#include "game/interface.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/campaign.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/team.h"
#include "game/text_box.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// What the map shows (C_MapScreen::m_kind): the game's mode.
enum : int {
    MAP_KIND_SINGLE_PLAYER = 0,                         // the squad, the seen enemies, the objectives
    MAP_KIND_DEATHMATCH = 1,                            // only the player
    MAP_KIND_OCCUPATION = 2,                            // the team, the zones
    MAP_KIND_OBJECTIVES = 3,                            // the team, the zones, the objectives
    MAP_KIND_COOPERATIVE = 4,                           // the same
};

// The spawn zones' ids (the frame data the map cursor picks; at most 15 zones).
constexpr int ZONE_ID_FIRST = 1000;
constexpr int ZONE_ID_LAST = 1014;
constexpr size_t MAX_ZONES = 15;
// The map actors start after the players' slots.
constexpr size_t FIRST_MAP_ACTOR = 32;

// The engine's driver states the map model loads with (as the interface model).
constexpr int DRIVER_STATE_11 = 0x11;
constexpr int DRIVER_STATE_15 = 0x15;
constexpr int DRIVER_STATE_19 = 0x19;

constexpr float HALF_PI = 1.57079637f;
// The markers' depth in front of the map.
constexpr float MARKER_Z = -0.5f;
// The active soldier's (and the VIP's own) marker is larger.
constexpr float ACTIVE_SCALE = 1.5f;

// The text styles (the menus' style table).
enum : uint8_t {
    STYLE_OBJECTIVE = 0x2D,
    STYLE_CHAT_ALLIED = 0x4C,
    STYLE_CHAT_AXIS = 0x4D,
};

// The objectives' result lines.
enum : int {
    TEXT_OBJECTIVE_WON = 0xB63,
    TEXT_OBJECTIVE_LOST = 0xB64,
};

C_Team *PlayerTeam()
{
    return g_teamManager.GetPlayerTeam();
}

// A horizontal direction (x, z) made a unit vector (the SDK's inline for two components).
void NormalizeXZ(float &x, float &z)
{
    const float sq = z * z + x * x;
    if (!(std::fabs(sq - 1.0f) >= 1e-8f))
        return;
    if (sq >= 1e-8f) {
        const float k = 1.0f / std::sqrt(sq);
        x = x * k;
        z = k * z;
    } else if (x == 0.0f) {
        if (z != 0.0f)
            z = z < 0.0f ? -1.0f : 1.0f;
    } else {
        x = x < 0.0f ? -1.0f : 1.0f;
    }
}

// A marker turned along a human's view (the map is the xz plane).
void PointAlong(I3D_frame *marker, const S_vector &dir)
{
    float x = dir.x;
    float z = dir.z;
    NormalizeXZ(x, z);
    marker->SetDir(S_vector(x, z, 0.0f), HALF_PI);
}

C_TextBox *NewText()
{
    return new C_TextBox();
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The zones and the objectives

struct C_MapScreen::S_Zone {                            // 0x18 bytes
    // The zone's dummy on the map, its "zone_disabled*", "allied*" and "axis*" parts get the
    // id (the cursor picks them).
    void Init(I3D_frame *frame_, C_MPZone *zone_, int id_);   // 0x004a7970
    // The parts by the zone's side.
    void SetSide(int side_);                            // 0x004a7a20
    void UpdateSide();                                  // 0x004a7a70 (__fastcall)

    I3D_frame *frame = nullptr;
    I3D_frame *allied = nullptr;                        // +0x04
    I3D_frame *axis = nullptr;                          // +0x08
    int id = 0;                                         // +0x0C ZONE_ID_FIRST + index
    int side = 2;                                       // +0x10 0 axis, 1 allied, 2 none
    C_MPZone *zone = nullptr;                           // +0x14
};

void C_MapScreen::S_Zone::Init(I3D_frame *frame_, C_MPZone *zone_, int id_)
{
    frame = frame_;
    zone = zone_;
    id = id_;
    I3D_frame *disabled = frame->FindChildFrame("zone_disabled*", 0x10001);
    allied = frame->FindChildFrame("allied*", 0x10001);
    axis = frame->FindChildFrame("axis*", 0x10001);
    // Fix: a zone without "zone_disabled*" (the original wrote its data unchecked).
    if (disabled)
        disabled->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(id)));
    if (allied) {
        allied->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(id)));
        allied->SetOn(false);
        allied->Update();
    }
    if (axis) {
        axis->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(id)));
        axis->SetOn(false);
        axis->Update();
    }
    side = 2;
}

void C_MapScreen::S_Zone::SetSide(int side_)
{
    if (side == side_)
        return;
    side = side_;
    if (allied) {
        allied->SetOn(side == 1);
        allied->Update();
    }
    if (axis) {
        axis->SetOn(side == 0);
        axis->Update();
    }
}

void C_MapScreen::S_Zone::UpdateSide()
{
    const int team = zone->GetTeam();
    if (side != team)
        SetSide(team);
}

struct C_MapScreen::S_Objective {                       // 0x1C bytes
    // "allied_obj*" / "axis_obj*" (both needed), "text_obj*" (optional) of the objective's dummy.
    bool Init(I3D_frame *frame_, S_MpObjective *objective_, I3D_scene *scene);   // 0x004a7a90
    // The parts and the text by the objective's status and the local player's side.
    void Update();                                      // 0x004a7c60 (__fastcall)
    void Render();                                      // 0x004a7eb0 (__fastcall)
    void Done();                                        // 0x004a7e80 (__fastcall)

    I3D_frame *frame = nullptr;
    I3D_frame *allied = nullptr;                        // +0x04
    I3D_frame *axis = nullptr;                          // +0x08
    int status = OBJECTIVE_INACTIVE;                    // +0x0C shown
    int side = 2;                                       // +0x10 the local player's, shown
    S_MpObjective *objective = nullptr;                 // +0x14
    C_TextBox *text = nullptr;                          // +0x18
};

bool C_MapScreen::S_Objective::Init(I3D_frame *frame_, S_MpObjective *objective_, I3D_scene *scene)
{
    frame = frame_;
    objective = objective_;
    I3D_frame *textVisual = frame->FindChildFrame("text_obj*", 0x10001);
    allied = frame->FindChildFrame("allied_obj*", 0x10001);
    axis = frame->FindChildFrame("axis_obj*", 0x10001);
    if (!allied || !axis)
        return false;
    if (!textVisual) {
        text = nullptr;
    } else {
        text = NewText();
        text->AttachTo(scene, textVisual);
        textVisual->SetOn(false);
    }
    allied->SetOn(false);
    axis->SetOn(false);
    frame->SetOn(false);
    status = OBJECTIVE_INACTIVE;
    side = 2;
    return true;
}

void C_MapScreen::S_Objective::Update()
{
    const int localSide = g_netGame.GetLocalTeam();
    if (localSide == 2)
        return;
    if (status == objective->status && side == localSide)
        return;
    status = objective->status;
    side = g_netGame.GetLocalTeam();
    // The title is the side's text (none for another side).
    auto title = [this]() {
        if (side == 1)
            text->SetTextId(objective->alliedText, STYLE_OBJECTIVE, 0);
        else if (side == 0)
            text->SetTextId(objective->axisText, STYLE_OBJECTIVE, 0);
    };
    switch (status) {
    case OBJECTIVE_OPEN:
        frame->SetOn(true);
        allied->SetOn(false);
        axis->SetOn(true);
        if (text) {
            // (Only the title; no result line.)
            if (side == 1)
                text->SetTextId(objective->alliedText, STYLE_OBJECTIVE, 0);
            else if (side == 0)
                text->SetTextId(objective->axisText, STYLE_OBJECTIVE, 0);
        }
        break;
    case OBJECTIVE_COMPLETED:
        frame->SetOn(true);
        allied->SetOn(true);
        axis->SetOn(false);
        if (text) {
            title();
            // Won by the attackers.
            text->SetTextId(side == objective->team ? TEXT_OBJECTIVE_WON : TEXT_OBJECTIVE_LOST, STYLE_OBJECTIVE, 1);
        }
        break;
    case OBJECTIVE_FAILED:
        frame->SetOn(true);
        if (text) {
            title();
            text->SetTextId(side == objective->team ? TEXT_OBJECTIVE_LOST : TEXT_OBJECTIVE_WON, STYLE_OBJECTIVE, 1);
        }
        break;
    case OBJECTIVE_INACTIVE:
        frame->SetOn(false);
        allied->SetOn(false);
        axis->SetOn(false);
        if (text)
            text->Clear();
        break;
    default:
        break;
    }
    allied->Update();
    axis->Update();
}

void C_MapScreen::S_Objective::Render()
{
    if (text && status != OBJECTIVE_INACTIVE)
        text->Render();
}

void C_MapScreen::S_Objective::Done()
{
    objective = nullptr;
    delete text;
    text = nullptr;
}

struct C_MapScreen::S_SpObjective {                     // 0x18 bytes
    // The dummy's "allied_obj*" and "axis_obj*" (false without them).
    bool Init(I3D_frame *frame_, S_MissionObjective *objective_, I3D_scene *scene);   // 0x004a7ed0
    // The parts by the objective's status: open the axis part, completed the allied one.
    void Update();                                      // 0x004a7f50 (__fastcall)

    I3D_frame *frame = nullptr;
    I3D_frame *allied = nullptr;                        // +0x04
    I3D_frame *axis = nullptr;                          // +0x08
    int status = -1;                                    // +0x0C shown
    S_MissionObjective *objective = nullptr;            // +0x10 (cleared by 0x004a8010)
};

bool C_MapScreen::S_SpObjective::Init(I3D_frame *frame_, S_MissionObjective *objective_, I3D_scene * /*scene*/)
{
    if (!frame_)
        return false;
    frame = frame_;
    objective = objective_;
    allied = frame->FindChildFrame("allied_obj*", 0x10001);
    axis = frame->FindChildFrame("axis_obj*", 0x10001);
    if (!allied || !axis)
        return false;
    allied->SetOn(false);
    axis->SetOn(false);
    frame->SetOn(false);
    status = -1;
    return true;
}

void C_MapScreen::S_SpObjective::Update()
{
    const int current = objective->status;
    if (status == current)
        return;
    status = current;
    switch (current) {
    case OBJECTIVE_OPEN:
        frame->SetOn(true);
        allied->SetOn(false);
        axis->SetOn(true);
        break;
    case OBJECTIVE_COMPLETED:
        frame->SetOn(true);
        if (allied)
            allied->SetOn(true);
        if (axis)
            axis->SetOn(false);
        break;
    case OBJECTIVE_FAILED:
    case OBJECTIVE_FAILED_CRITICAL:
    case OBJECTIVE_INACTIVE:
        frame->SetOn(false);
        allied->SetOn(false);
        axis->SetOn(false);
        break;
    default:
        break;
    }
    allied->Update();
    axis->Update();
    frame->Update();
}

// ---------------------------------------------------------------------------------------------
// C_MapScreen

bool C_MapScreen::Init(I3D_scene *scene, C_MapCursor *cursor)
{
    m_scene = scene;
    m_cursor = cursor;
    const C_fstr fileName("missions\\%s\\map", GetGame()->GetMissionName());
    I3D_driver *driver = I3DGetDriver();
    m_model = static_cast<I3D_model *>(driver->CreateFrame(FRAME_MODEL));
    const bool state15 = driver->GetState(DRIVER_STATE_15) != 0;
    const bool state19 = driver->GetState(DRIVER_STATE_19) != 0;
    const bool state11 = driver->GetState(DRIVER_STATE_11) != 0;
    driver->SetState(DRIVER_STATE_15, 0);
    driver->SetState(DRIVER_STATE_19, 1);
    driver->SetState(DRIVER_STATE_11, 0);
    const HRESULT result = m_model->Open(fileName, 0, nullptr, nullptr);
    driver->SetState(DRIVER_STATE_15, state15);
    driver->SetState(DRIVER_STATE_19, state19);
    driver->SetState(DRIVER_STATE_11, state11);
    if (result != I3D_OK) {
        m_frame = nullptr;
        m_map = nullptr;
        m_model->Release();
        m_model = nullptr;
        return false;
    }
    m_frame = m_model->FindChildFrame("center", FRAME_MASK_ANY);
    m_map = m_model->FindChildFrame("map*", 0x10001);
    if (m_frame) {
        // In the middle of the screen, in front of the panels.
        m_frame->LinkTo(reinterpret_cast<I3D_frame *>(scene->GetPrimarySector()), 0);
        m_frame->SetPos(S_vector(0.5f, -0.375f, -1.0f));
        m_frame->Update();
    }
    // The map's scale: the "scale" of its user properties.
    m_scale = 0.0f;
    if (m_map) {
        const char *scale = std::strstr(m_map->GetUserProps(), "scale");
        m_scale = scale ? static_cast<float>(std::atof(scale + std::strlen("scale"))) : 0.0f;
    }
    // The interface's templates go onto the map.
    auto adopt = [this, scene](const char *name) {
        I3D_frame *frame = scene->FindFrame(name, FRAME_MASK_ANY);
        frame->LinkTo(m_map, 0);
        frame->SetOn(false);
        frame->Update();
        return frame;
    };
    m_activeMarker = adopt("allied_active");
    m_vipMarker = adopt("vip");
    m_cameraMarker = adopt("mapcamera");
    m_enemyTemplate = adopt("enemy");
    m_itemTemplate = adopt("objective");

    if (IsLocalGame()) {
        m_kind = MAP_KIND_SINGLE_PLAYER;
        m_markerCount = 6;
    } else {
        // (Other game types: the kind and the count stay as they were.)
        switch (g_mpSettings->gameType) {
        case MP_GAME_DEATHMATCH:
            m_kind = MAP_KIND_DEATHMATCH;
            m_markerCount = 0;
            break;
        case MP_GAME_OCCUPATION:
            m_kind = MAP_KIND_OCCUPATION;
            m_markerCount = MARKERS;
            break;
        case MP_GAME_OBJECTIVES:
            m_kind = MAP_KIND_OBJECTIVES;
            m_markerCount = MARKERS;
            break;
        case MP_GAME_COOPERATIVE:
            m_kind = MAP_KIND_COOPERATIVE;
            m_markerCount = 6;
            break;
        default:
            break;
        }
    }
    CreateMarkers();
    switch (m_kind) {
    case MAP_KIND_SINGLE_PLAYER:
        CreateSpObjectives(scene);
        break;
    case MAP_KIND_OCCUPATION:
        CreateZones();
        break;
    case MAP_KIND_OBJECTIVES:
    case MAP_KIND_COOPERATIVE:
        CreateZones();
        CreateObjectives(scene);
        break;
    default:
        break;
    }

    // The map's depth in the camera's range (the chat lines are drawn there; -1 outside).
    if (!m_map) {
        m_depth = 1.0f;
    } else {
        I3D_camera *camera = m_scene->GetActiveCamera();
        float nearRange;
        float farRange;
        camera->GetRange(nearRange, farRange);
        const float depth =
            (std::fabs(m_map->GetWorldPos().z - camera->GetWorldPos().z) - 0.4f - nearRange) / (farRange - nearRange);
        m_depth = depth;
        if (depth > 1.0f || depth < 0.0f)
            m_depth = -1.0f;
    }
    Show(false, false);
    return true;
}

void C_MapScreen::Done()
{
    DestroyMarkers();
    ResizeEnemyMarkers(0);
    ResizeItemMarkers(0);
    switch (m_kind) {
    case MAP_KIND_SINGLE_PLAYER:
        DestroySpObjectives();
        break;
    case MAP_KIND_OCCUPATION:
        DestroyZones();
        break;
    case MAP_KIND_OBJECTIVES:
    case MAP_KIND_COOPERATIVE:
        DestroyZones();
        DestroyObjectives();
        break;
    default:
        break;
    }
    if (m_model)
        m_model->Release();
    m_model = nullptr;
}

void C_MapScreen::CreateMarkers()
{
    I3D_frame *source = m_scene->FindFrame("allied", FRAME_MASK_ANY);
    for (int i = 0; i < MARKERS; ++i) {
        S_Marker &marker = m_markers[i];
        if (i < m_markerCount) {
            marker.visual = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
            marker.visual->Duplicate(source);
            marker.visual->LinkTo(m_map, 0);
            marker.visual->SetOn(false);
            marker.visual->Update();
            marker.text = NewText();
        } else {
            marker.visual = nullptr;
            marker.text = nullptr;
        }
        marker.shown = false;
    }
}

void C_MapScreen::DestroyMarkers()
{
    for (int i = 0; i < m_markerCount; ++i) {
        S_Marker &marker = m_markers[i];
        marker.visual->Release();
        marker.visual = nullptr;
        delete marker.text;
        marker.text = nullptr;
        marker.shown = false;
    }
}

void C_MapScreen::ResizeEnemyMarkers(size_t count)
{
    while (m_enemyMarkers.size() > count) {
        m_enemyMarkers.back()->Release();
        m_enemyMarkers.pop_back();
    }
    while (m_enemyMarkers.size() < count) {
        I3D_frame *marker = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
        marker->Duplicate(m_enemyTemplate);
        marker->LinkTo(m_map, 0);
        marker->SetOn(false);
        marker->Update();
        m_enemyMarkers.push_back(marker);
    }
}

void C_MapScreen::ResizeItemMarkers(size_t count)
{
    while (m_itemMarkers.size() > count) {
        m_itemMarkers.back()->Release();
        m_itemMarkers.pop_back();
    }
    while (m_itemMarkers.size() < count) {
        I3D_frame *marker = I3DGetDriver()->CreateVisual(I3DVISUAL_OBJECT);
        marker->Duplicate(m_itemTemplate);
        marker->LinkTo(m_map, 0);
        marker->SetOn(false);
        marker->Update();
        m_itemMarkers.push_back(marker);
    }
}

void C_MapScreen::Place(I3D_frame *marker, const S_vector &pos) const
{
    marker->SetPosInline(S_vector(pos.x * m_scale, pos.z * m_scale, MARKER_Z));
}

void C_MapScreen::Show(bool show, bool commandMap)
{
    if (!m_frame)
        return;
    m_commandMap = commandMap;
    m_frame->SetOn(show);
    m_map->SetOn(show);
    // The team games' respawn screen: the cursor and the selected zone.
    if (m_kind == MAP_KIND_DEATHMATCH || m_kind == MAP_KIND_SINGLE_PLAYER)
        return;
    if (!show) {
        m_cursor->SetMode(-1);
        return;
    }
    m_cursor->SetMode(0);
    ShowSelectedZone();
    UpdateObjectives();
}

void C_MapScreen::Update(int timeDelta)
{
    if (!m_frame)
        return;
    switch (m_kind) {
    case MAP_KIND_SINGLE_PLAYER:
        UpdateSinglePlayer(timeDelta);
        break;
    case MAP_KIND_DEATHMATCH:
        UpdateDeathmatch(timeDelta);
        break;
    case MAP_KIND_OCCUPATION:
    case MAP_KIND_OBJECTIVES:
    case MAP_KIND_COOPERATIVE:
        UpdateTeams(timeDelta);
        break;
    default:
        break;
    }
}

void C_MapScreen::Render()
{
    RenderObjectives();
    for (int i = m_markerCount; i-- > 0;) {
        if (m_markers[i].shown)
            m_markers[i].text->Render();
    }
}

void C_MapScreen::HandleInput(float *held, float *pressed)
{
    if (m_kind == MAP_KIND_SINGLE_PLAYER || m_kind == MAP_KIND_DEATHMATCH)
        return;
    if (pressed[INPUT_CLICK] == 0.0f)
        return;
    SelectSpawnZone();
    g_controls.ClearAction(INPUT_CLICK, held, pressed);
}

void C_MapScreen::UpdateVip()
{
    const int vip = GetGame()->GetVipSlot();
    if (vip < 0) {
        if (m_vipMarker->IsOn())
            m_vipMarker->SetOn(false);
        m_vipMarker->Update();
        return;
    }
    C_Human *human = static_cast<C_Human *>(g_actorsModule.GetHuman(vip));
    // Dimmed when dead.
    static_cast<I3D_object_visual *>(m_vipMarker)->SetLightLevel(human->IsDead() ? 0.3f : 1.0f);
    Place(m_vipMarker, human->GetFrame()->GetWorldPos());
    const S_vector &view = human->GetViewDir();
    float x = view.x;
    float z = view.z;
    NormalizeXZ(x, z);
    // Larger for the VIP himself.
    const float scale = GetGame()->GetVipSlot() == PlayerTeam()->GetActivePlayer()->GetNetId() ? ACTIVE_SCALE : 1.0f;
    m_vipMarker->SetScale(S_vector(scale, scale, scale));
    m_vipMarker->SetDir(S_vector(x, z, 0.0f), HALF_PI);
    m_vipMarker->SetOn(true);
    m_vipMarker->Update();
}

void C_MapScreen::UpdateItemMarkers()
{
    // (A copy: the list may change.)
    const std::vector<C_Item *> items = g_itemsModule.GetRespawningItems();
    ResizeItemMarkers(items.size());
    for (size_t i = items.size(); i-- > 0;) {
        C_Item *item = items[i];
        // Carried: where its owner is.
        const S_vector &pos =
            item->GetOwner() ? item->GetOwner()->GetFrame()->GetWorldPos() : item->GetFrame(0)->GetWorldPos();
        I3D_frame *marker = m_itemMarkers[i];
        marker->SetPosInline(S_vector(pos.x * m_scale, pos.z * m_scale + 0.01f, MARKER_Z));
        marker->SetOn(true);
        marker->Update();
    }
}

void C_MapScreen::UpdateSinglePlayer(int /*timeDelta*/)
{
    C_Human *active = PlayerTeam()->GetActivePlayer();
    // Fix: nothing to show without an active soldier (the original read its frame).
    if (!active)
        return;
    Place(m_activeMarker, active->GetFrame()->GetWorldPos());
    PointAlong(m_activeMarker, active->GetViewDir());
    m_activeMarker->SetScale(S_vector(ACTIVE_SCALE, ACTIVE_SCALE, ACTIVE_SCALE));
    m_activeMarker->SetOn(true);
    m_activeMarker->Update();

    // The squad (the dead dimmed).
    for (int i = 0; i < PlayerTeam()->GetMemberCount(); ++i) {
        C_Human *member = PlayerTeam()->GetMember(i);
        // Fix: no more members than markers (the original went on past them).
        if (i >= m_markerCount)
            break;
        I3D_frame *marker = m_markers[i].visual;
        if (member == active) {
            marker->SetOn(false);
            continue;
        }
        static_cast<I3D_object_visual *>(marker)->SetLightLevel(member->IsDead() ? 0.0f : 1.0f);
        Place(marker, member->GetFrame()->GetWorldPos());
        PointAlong(marker, member->GetViewDir());
        marker->SetOn(true);
        marker->Update();
    }

    // The seen enemies (carnage: the last one when none is seen).
    std::vector<C_Human *> enemies;
    PlayerTeam()->GetSeenLivingEnemies(enemies);
    if (g_playMode->mode == PLAY_MODE_CARNAGE && enemies.empty() && g_actorsModule.GetCarnageEnemy())
        enemies.push_back(g_actorsModule.GetCarnageEnemy());
    ResizeEnemyMarkers(enemies.size());
    for (size_t i = enemies.size(); i-- > 0;) {
        C_Human *enemy = enemies[i];
        I3D_frame *marker = m_enemyMarkers[i];
        if (enemy->IsDead() || enemy->IsUnconscious()) {
            marker->SetOn(false);
            continue;
        }
        Place(marker, enemy->GetFrame()->GetWorldPos());
        PointAlong(marker, enemy->GetViewDir());
        marker->SetOn(true);
        marker->Update();
    }

    // Over the command map: the game camera.
    if (!m_commandMap) {
        m_cameraMarker->SetOn(false);
    } else {
        I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
        Place(m_cameraMarker, camera->GetWorldPos());
        S_vector dir = camera->GetMatrix()(2);
        // (The SDK's normalization, summed y, z, then x.)
        const float sq = (dir.y * dir.y + dir.z * dir.z) + dir.x * dir.x;
        if (std::fabs(sq - 1.0f) >= 1e-8f) {
            if (sq >= 1e-8f) {
                const float k = 1.0f / std::sqrt(sq);
                dir.x = dir.x * k;
                dir.y = dir.y * k;
                dir.z = dir.z * k;
            } else if (dir.x != 0.0f) {
                dir.x = dir.x < 0.0f ? -1.0f : 1.0f;
            } else if (dir.z != 0.0f) {
                dir.z = dir.z < 0.0f ? -1.0f : 1.0f;
            } else {
                dir.y = dir.y < 0.0f ? -1.0f : 1.0f;
            }
        }
        PointAlong(m_cameraMarker, dir);
        m_cameraMarker->SetOn(true);
        m_cameraMarker->Update();
    }
    UpdateSpObjectives();
}

void C_MapScreen::UpdateTeams(int /*timeDelta*/)
{
    // The chat lines' rectangle from the marker (top left, bottom right; the interface's units).
    static const float CHAT_TOP_LEFT[2] = {0.01f, -0.01f};
    static const float CHAT_BOTTOM_RIGHT[2] = {0.4f, 0.01f};
    const int width = GetIGraph()->ScrnX();
    const int height = GetIGraph()->ScrnY();
    C_Human *active = PlayerTeam()->GetActivePlayer();
    // (The VIP has his own marker.)
    if (GetGame()->GetVipSlot() == active->GetNetId()) {
        m_activeMarker->SetOn(false);
        m_activeMarker->Update();
    } else {
        Place(m_activeMarker, active->GetFrame()->GetWorldPos());
        PointAlong(m_activeMarker, active->GetViewDir());
        m_activeMarker->SetScale(S_vector(ACTIVE_SCALE, ACTIVE_SCALE, ACTIVE_SCALE));
        m_activeMarker->SetOn(true);
        m_activeMarker->Update();
        static_cast<I3D_object_visual *>(m_activeMarker)->SetAlpha(1.0f);
    }

    // The players of the active one's side.
    for (int i = 0; i < m_markerCount; ++i) {
        C_Human *human = static_cast<C_Human *>(g_actorsModule.GetHuman(i));
        S_Marker &marker = m_markers[i];
        if (!marker.visual)
            continue;
        if (!human || !human->IsInPlay() || human->IsDead() || human->IsSpectating() ||
            active->GetTeam(false) != human->GetTeam(false)) {
            marker.visual->SetOn(false);
            continue;
        }
        Place(marker.visual, human->GetFrame()->GetWorldPos());
        if (m_kind == MAP_KIND_DEATHMATCH || human->GetChatText()[0] == '\0') {
            static_cast<I3D_object_visual *>(marker.visual)->SetAlpha(1.0f);
            marker.shown = false;
        } else {
            // The chat line next to the marker; the marker blinks while it lasts.
            const S_vector &at = marker.visual->GetWorldPos();
            const float x1 = CHAT_BOTTOM_RIGHT[0] + at.x;
            const float y1 = CHAT_BOTTOM_RIGHT[1] + at.y;
            const float y0 = CHAT_TOP_LEFT[1] + marker.visual->GetWorldPos().y;
            const float x0 = CHAT_TOP_LEFT[0] + marker.visual->GetWorldPos().x;
            const float w = static_cast<float>(width);
            float rect[7];
            rect[0] = x0 * w;
            rect[1] = y0 * static_cast<float>(height) * -1.33333337f;
            rect[2] = x1 * w;
            rect[3] = y1 * static_cast<float>(height) * -1.33333337f;
            rect[4] = rect[2] - rect[0];
            rect[5] = rect[1] - rect[3];
            rect[6] = m_depth;
            marker.text->SetRect(rect);
            marker.text->SetText(C_fstr("%s", human->GetChatText()),
                                 human->GetTeam(false) == 1 ? STYLE_CHAT_ALLIED : STYLE_CHAT_AXIS, 0);
            marker.shown = true;
            const int phase = human->GetChatTime() % 500;
            const float alpha = phase > 250 ? static_cast<float>(phase - 250) * 0.004f
                                            : 1.0f - static_cast<float>(phase) * 0.004f;
            if (active == human)
                static_cast<I3D_object_visual *>(m_activeMarker)->SetAlpha(alpha);
            else
                static_cast<I3D_object_visual *>(marker.visual)->SetAlpha(alpha);
        }
        // (The active one has his own marker, the VIP the VIP's.)
        if (active == human || i == GetGame()->GetVipSlot()) {
            marker.visual->SetOn(false);
            continue;
        }
        PointAlong(marker.visual, human->GetViewDir());
        marker.visual->SetOn(true);
        marker.visual->Update();
    }
    UpdateVip();
    UpdateItemMarkers();
    UpdateObjectives();
    ShowSelectedZone();
}

void C_MapScreen::UpdateDeathmatch(int /*timeDelta*/)
{
    C_Human *active = PlayerTeam()->GetActivePlayer();
    Place(m_activeMarker, active->GetFrame()->GetWorldPos());
    PointAlong(m_activeMarker, active->GetViewDir());
    m_activeMarker->SetOn(true);
    m_activeMarker->Update();
}

void C_MapScreen::SelectSpawnZone()
{
    // The local player's soldier (the squad's first; none for spectators, side 2).
    if (g_missionSquad->states.empty())
        return;
    S_SquadSoldier *soldier = g_missionSquad->states[0];
    if (!soldier || soldier->team == 2)
        return;
    const int id = m_cursor->GetHoveredId();
    if (id < ZONE_ID_FIRST || id > ZONE_ID_LAST)
        return;
    for (size_t i = m_zones.size(); i-- > 0;) {
        if (m_zones[i]->id != id)
            continue;
        S_Zone *zone = m_zones[i];
        // Only the soldier's side's zones.
        if (!zone || !zone->frame || zone->side != soldier->team)
            return;
        soldier->spawnZone = zone->zone->GetNetId();
        const S_vector &pos = zone->frame->GetWorldPos();
        m_selectMarker->SetPos(S_vector(pos.x, pos.y, pos.z - 0.5f));
        m_selectMarker->SetOn(true);
        m_selectMarker->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(id)));
        m_selectMarker->Update();
        return;
    }
}

void C_MapScreen::ShowSelectedZone()
{
    for (S_Zone *zone : m_zones)
        zone->UpdateSide();
    bool shown = false;
    if (!g_missionSquad->states.empty()) {
        S_SquadSoldier *soldier = g_missionSquad->states[0];
        if (soldier && soldier->spawnZone > 0) {
            for (size_t i = m_zones.size(); i-- > 0;) {
                if (m_zones[i]->zone->GetNetId() != soldier->spawnZone)
                    continue;
                // A zone of the other side (it changed hands): picked again.
                if (soldier->team == 2 || soldier->team != m_zones[i]->side) {
                    soldier->spawnZone = -1;
                    break;
                }
                const S_vector &pos = m_zones[i]->frame->GetWorldPos();
                m_selectMarker->SetPos(S_vector(pos.x, pos.y, pos.z - 0.5f));
                m_selectMarker->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(m_zones[i]->id)));
                shown = true;
                break;
            }
        }
    }
    m_selectMarker->SetOn(shown);
    m_selectMarker->Update();
}

void C_MapScreen::CreateZones()
{
    m_selectMarker = m_model->FindChildFrame("selectzone", FRAME_MASK_VISUAL);
    if (!m_selectMarker)
        return;
    m_selectMarker->LinkTo(m_frame, 1);
    // The map actors' zones without a side given by EnableSpawnZone.
    std::vector<C_MPZone *> zones;
    zones.reserve(20);
    const std::vector<C_Actor *> &actors = g_actorsModule.GetActors();
    for (size_t i = FIRST_MAP_ACTOR; i < actors.size(); ++i) {
        C_Actor *actor = actors[i];
        if (actor->GetType() == ACTOR_MP_ZONE && static_cast<C_MPZone *>(actor)->GetSide() == -1)
            zones.push_back(static_cast<C_MPZone *>(actor));
    }
    // Quirk kept: more than 15 zones and the map shows none.
    if (zones.size() > MAX_ZONES)
        return;
    for (size_t i = 0; i < zones.size(); ++i) {
        // The zone's dummy on the map has the zone's name.
        I3D_frame *frame = m_map->FindChildFrame(zones[i]->GetFrame()->GetShortName(), FRAME_MASK_DUMMY);
        if (!frame)
            continue;
        auto *zone = new S_Zone();
        m_zones.push_back(zone);
        zone->Init(frame, zones[i], ZONE_ID_FIRST + static_cast<int>(i));
    }
}

void C_MapScreen::DestroyZones()
{
    for (S_Zone *zone : m_zones)
        delete zone;
    m_zones.clear();
    m_selectMarker = nullptr;
}

void C_MapScreen::CreateObjectives(I3D_scene *scene)
{
    C_MissionDef *mission = GetGame()->GetMission();
    for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
        I3D_frame *frame = m_map->FindChildFrame(C_fstr("objective%d", i + 1), FRAME_MASK_DUMMY);
        if (!frame)
            continue;
        auto *objective = new S_Objective();
        // Fix: an objective without its parts is dropped (the original kept it half set up).
        if (!objective->Init(frame, static_cast<S_MpObjective *>(mission->GetObjective(i)), scene)) {
            delete objective;
            continue;
        }
        m_objectives.push_back(objective);
    }
}

void C_MapScreen::CreateSpObjectives(I3D_scene *scene)
{
    C_MissionDef *mission = GetGame()->GetMission();
    if (!mission)
        return;
    for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
        I3D_frame *frame = m_map->FindChildFrame(C_fstr("objective%d", i + 1), FRAME_MASK_DUMMY);
        if (!frame)
            continue;
        auto *objective = new S_SpObjective();
        if (!objective->Init(frame, mission->GetObjective(i), scene)) {
            delete objective;
            continue;
        }
        m_spObjectives.push_back(objective);
    }
}

void C_MapScreen::DestroyObjectives()
{
    for (S_Objective *objective : m_objectives) {
        objective->Done();
        delete objective;
    }
    m_objectives.clear();
}

void C_MapScreen::DestroySpObjectives()
{
    for (S_SpObjective *objective : m_spObjectives) {
        objective->objective = nullptr;
        delete objective;
    }
    m_spObjectives.clear();
}

void C_MapScreen::UpdateObjectives()
{
    for (S_Objective *objective : m_objectives)
        objective->Update();
}

void C_MapScreen::RenderObjectives()
{
    for (S_Objective *objective : m_objectives)
        objective->Render();
}

void C_MapScreen::UpdateSpObjectives()
{
    for (S_SpObjective *objective : m_spObjectives)
        objective->Update();
}
