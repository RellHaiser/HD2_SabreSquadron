// The command map of the interface (see interface.h; the original's 0x004b4980..0x004bf2b0): the
// tactical view in which the player gives the squad its orders, their markers and lines, and the
// brackets and boxes over the soldiers and the enemies.
#include "game/interface.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "game/actor.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/interface_internal.h"
#include "game/key_bindings.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/squad_order.h"
#include "game/team.h"
#include "game/text_box.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/math.h"

namespace {

// The buttons' ids (their frames' data; the info icons' are INFO_ICON_FIRST + the order's index).
enum : int {
    MAP_BUTTON_FOLLOW = 1,                              // "regroup": the commanded soldier follows
    MAP_BUTTON_DELETE = 2,                              // the chosen order
    MAP_BUTTON_DELETE_ALL = 3,
    MAP_BUTTON_WAIT_1 = 4,                              // "wait01" / "wait02" (the hints' "signals"
    MAP_BUTTON_WAIT_2 = 5,                              //   group): orders of kind 6 / 7
    MAP_BUTTON_STAND = 6,                               // "stay": the stances
    MAP_BUTTON_KNEEL = 7,
    MAP_BUTTON_LIE = 8,
    MAP_BUTTON_SPRINT = 9,                              // the paces
    MAP_BUTTON_RUN = 10,
    MAP_BUTTON_WALK = 11,
    MAP_BUTTON_SLOW = 12,
    MAP_BUTTON_PASSIVE = 13,                            // the AI modes
    MAP_BUTTON_DEFENSIVE = 14,
    MAP_BUTTON_AGGRESSIVE = 15,
    MAP_BUTTON_OPTIONS = 16,                            // the in-game menu
    MAP_BUTTON_ZOOM = 17,                               // "TMCamera"
    MAP_BUTTON_REALTIME = 18,
    INFO_ICON_FIRST = 20,
    INFO_ICONS = 10,
};

struct S_ButtonDef {
    const char *name;
    const char *unlitName;
    int id;
    bool lit;
};
// In the original's order.
const S_ButtonDef BUTTON_DEFS[] = {
    {"regroup", "~regroup", MAP_BUTTON_FOLLOW, true},
    {"delete", "~delete", MAP_BUTTON_DELETE, true},
    {"deleteall", "~deleteall", MAP_BUTTON_DELETE_ALL, true},
    {"stay", "~stay", MAP_BUTTON_STAND, false},
    {"kneel", "~kneel", MAP_BUTTON_KNEEL, false},
    {"lie", "~lie", MAP_BUTTON_LIE, false},
    {"sprint", "~sprint", MAP_BUTTON_SPRINT, false},
    {"run", "~run", MAP_BUTTON_RUN, false},
    {"walk", "~walk", MAP_BUTTON_WALK, false},
    {"slow", "~slow", MAP_BUTTON_SLOW, false},
    {"passive", "~passive", MAP_BUTTON_PASSIVE, false},
    {"aggresive", "~aggresive", MAP_BUTTON_AGGRESSIVE, false},
    {"defensive", "~defensive", MAP_BUTTON_DEFENSIVE, false},
    {"wait01", "~wait01", MAP_BUTTON_WAIT_1, true},
    {"wait02", "~wait02", MAP_BUTTON_WAIT_2, true},
    {"options", "~options", MAP_BUTTON_OPTIONS, false},
    {"TMCamera", "~TMCamera", MAP_BUTTON_ZOOM, true},
    {"realtime", "~realtime", MAP_BUTTON_REALTIME, false},
};

// The hint texts: a title per group, a button's hint while the cursor is over it.
enum : int {
    HINT_FOLLOW = 0,
    HINT_WAIT = 1,                                      // "text_signals"
    HINT_STANCE = 2,
    HINT_PACE = 3,
    HINT_MODE = 4,
    HINT_DELETE = 5,
    HINT_CAMERA = 6,
    HINT_OPTIONS = 7,
    HINT_NONE = 8,
};
constexpr int HINT_STYLE = 0x47;
struct S_HintDef {
    const char *frame;
    int title;
};
const S_HintDef HINT_DEFS[] = {
    {"text_regroup", 0x1C84},
    {"text_signals", 0x1C8E},
    {"text_pose", 0x1C98},
    {"text_movement", 0x1CA2},
    {"text_mind", 0x1CAC},
    {"text_delete", 0x1CB6},
    {"text_cameramotion", 0x1CC2},
    {"text_options", 0x1CC0},
};
struct S_ButtonHint {
    int id;
    int group;
    int text;
};
// Quirk kept: the follow and zoom buttons have no hint.
const S_ButtonHint BUTTON_HINTS[] = {
    {MAP_BUTTON_DELETE, HINT_DELETE, 0x1CB7},
    {MAP_BUTTON_DELETE_ALL, HINT_DELETE, 0x1CB8},
    {MAP_BUTTON_WAIT_1, HINT_WAIT, 0x1C8F},
    {MAP_BUTTON_WAIT_2, HINT_WAIT, 0x1C90},
    {MAP_BUTTON_STAND, HINT_STANCE, 0x1C99},
    {MAP_BUTTON_KNEEL, HINT_STANCE, 0x1C9A},
    {MAP_BUTTON_LIE, HINT_STANCE, 0x1C9B},
    {MAP_BUTTON_SPRINT, HINT_PACE, 0x1CA3},
    {MAP_BUTTON_RUN, HINT_PACE, 0x1CA4},
    {MAP_BUTTON_WALK, HINT_PACE, 0x1CA5},
    {MAP_BUTTON_SLOW, HINT_PACE, 0x1CA6},
    {MAP_BUTTON_PASSIVE, HINT_MODE, 0x1CAF},
    {MAP_BUTTON_DEFENSIVE, HINT_MODE, 0x1CAE},
    {MAP_BUTTON_AGGRESSIVE, HINT_MODE, 0x1CAD},
    {MAP_BUTTON_OPTIONS, HINT_OPTIONS, 0x1CC3},
    {MAP_BUTTON_REALTIME, HINT_OPTIONS, 0x1CC1},
};

// The command map's keys (C_Controls in BINDING_MODE_COMMAND_MAP; the default keys in brackets).
struct S_ButtonKey {
    int action;
    int id;
};
// Tested in this order; the first one pressed counts.
const S_ButtonKey BUTTON_KEYS[] = {
    {0, MAP_BUTTON_FOLLOW},                             // G
    {1, MAP_BUTTON_WAIT_1},                             // H
    {2, MAP_BUTTON_WAIT_2},                             // J
    {3, MAP_BUTTON_STAND},                              // D
    {4, MAP_BUTTON_KNEEL},                              // S
    {5, MAP_BUTTON_LIE},                                // A
    {6, MAP_BUTTON_SPRINT},                             // Q
    {7, MAP_BUTTON_RUN},                                // W
    {8, MAP_BUTTON_WALK},                               // E
    {9, MAP_BUTTON_SLOW},                               // R
    {10, MAP_BUTTON_PASSIVE},                           // Z
    {11, MAP_BUTTON_DEFENSIVE},                         // X
    {12, MAP_BUTTON_AGGRESSIVE},                        // C
    {13, MAP_BUTTON_DELETE},                            // keypad 0
    {15, MAP_BUTTON_DELETE_ALL},                        // keypad .
    {16, MAP_BUTTON_OPTIONS},                           // keypad /
    {17, MAP_BUTTON_ZOOM},                              // keypad *
    {INPUT_PAUSE, MAP_BUTTON_REALTIME},                 // P
};
// The camera's keys (held).
enum : int {
    MAP_KEY_FORWARD = 18,                               // up
    MAP_KEY_BACK = 32,                                  // down
    MAP_KEY_LEFT = 33,
    MAP_KEY_RIGHT = 34,
    MAP_KEY_UP = 39,                                    // page up
    MAP_KEY_DOWN = 40,                                  // page down
    MAP_KEY_TURN = 41,                                  // Ctrl: the arrows turn
    MAP_KEY_TURN_LEFT = 42,                             // keypad 4, 6, 8, 2
    MAP_KEY_TURN_RIGHT = 43,
    MAP_KEY_TURN_UP = 44,
    MAP_KEY_TURN_DOWN = 45,
    MAP_KEY_PLUS = 62,                                  // keypad + / -
    MAP_KEY_MINUS = 63,
    INPUT_CLICK3 = 57,                                  // the middle button: turning by the mouse
};

// C_MapCursor's shapes (its "kurzor", "k_*" and "t_*" frames).
enum : int {
    CURSOR_POINTER = 0,
    CURSOR_MOVE = 1,                                    // "k_move": the arrows, the map dragged
    CURSOR_ROTATE = 2,                                  // "k_rotate": the middle button
    CURSOR_TURN_LEFT = 3,                               // "t_rotleft".."t_rotdown"
    CURSOR_TURN_RIGHT = 4,
    CURSOR_TURN_UP = 5,
    CURSOR_TURN_DOWN = 6,
    CURSOR_LEFT = 7,                                    // "t_left".."t_backward": the screen's edges
    CURSOR_RIGHT = 8,
    CURSOR_FORWARD = 9,
    CURSOR_BACKWARD = 10,
    CURSOR_UP = 11,                                     // "t_up" / "t_down": page up / down, the wheel
    CURSOR_DOWN = 12,
    CURSOR_MOVE_KEYS = 13,                              // "k_movepos" (not set by the map)
    CURSOR_PLUS = 14,                                   // "k_move": the keypad's + / -
    CURSOR_MINUS = 15,
};

// The roll menu's pick-up action (see interface_rollmenu.cpp).
constexpr int ROLL_PICK_UP = 6;
// The values of the buttons' flags with no soldier.
constexpr uint32_t FLAGS_NONE = 0xFFFFFFFFu;
// ms a pressed button stays out (and the buttons do not answer).
constexpr int FLASH_TIME = 200;
// ms the wheel's cursor stays.
constexpr int WHEEL_TIME = 100;
// The rays' length (m).
constexpr float RAY_LENGTH = 100.0f;
// The picked point is pulled back along the ray (m).
constexpr float PICK_PULL_BACK = 0.2f;
// The steepest ground the orders and markers go to (the normal's y).
constexpr float FLAT_GROUND = 0.6f;
// A carried marker moves when it is this far (squared, m) from its order's place.
constexpr float MARKER_MOVE2 = 0.05f;
// The order lines float above the paths (m).
constexpr float LINE_LIFT = 0.1f;
// The marker parts' light level.
constexpr float MARKER_LIGHT = 0.6f;
// The ground and actors the cursor points at (the volumes' flags excluded).
constexpr uint32_t PICK_EXCLUDE_FLAGS = 0x610000;
// The brackets and boxes: their least size (of the screen) and the commanded soldier's / the
// target's brackets drawn inside / outside the others.
constexpr float BOX_MIN_SIZE = 0.025f;
constexpr float BRACKETS_GROW = 0.005f;

const float COLOR_BLACK[3] = {0.0f, 0.0f, 0.0f};
const float COLOR_RED[3] = {1.0f, 0.0f, 0.0f};
const float COLOR_GREEN[3] = {0.0f, 1.0f, 0.0f};
const float COLOR_BLUE[3] = {0.0f, 0.0f, 1.0f};
const float COLOR_WHITE[3] = {1.0f, 1.0f, 1.0f};

C_Team *PlayerTeam()
{
    return g_teamManager.GetPlayerTeam();
}

void SetFrameId(I3D_frame *frame, int id)
{
    frame->SetData(reinterpret_cast<S_FrameData *>(static_cast<intptr_t>(id)));
}

S_vector4 FramePos4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

S_vector XYZ(const S_vector4 &v)
{
    return S_vector(v.x, v.y, v.z);
}

S_vector4 Vector4(const S_vector &v, float w)
{
    return {v.x, v.y, v.z, w};
}

// Where an order leads: its actor or its place.
S_vector4 OrderPoint(const S_HumanOrder *order)
{
    return order->actor ? FramePos4(order->actor->GetFrame()) : order->pos;
}

// `color` (r, g, b 0..1) at half opacity (the original rounds 127.5 to 128).
uint32_t HalfColor(const float *color)
{
    const uint32_t r = static_cast<uint32_t>(std::lrintf(color[0] * 255.0f));
    const uint32_t g = static_cast<uint32_t>(std::lrintf(color[1] * 255.0f));
    const uint32_t b = static_cast<uint32_t>(std::lrintf(color[2] * 255.0f));
    return 0x80000000u | r << 16 | g << 8 | b;
}

// A part of an order's marker made its own (its mesh and material duplicated) and lit evenly.
void PrepareMarkerPart(I3D_frame *part, const S_vector &emissive, bool own)
{
    auto *visual = static_cast<I3D_object_visual *>(part);
    if (own) {
        visual->DuplicateMesh();
        visual->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].DuplicateMaterial();
    }
    I3D_material *material = visual->GetVisualMesh()->GetLevel(0)->GetFaceGroup(0)->material;
    material->Flags() |= I3DMATERIAL_TRACE_FLAG;
    material->SetEmissive(emissive);
    material->SetSpecular(S_vector(0.0f, 0.0f, 0.0f));
    material->SetDiffuse(S_vector(0.0f, 0.0f, 0.0f));
    material->SetPower(0.0f);
    visual->SetLightLevel(MARKER_LIGHT);
}

// The actor's box on the screen (pixels), at least BOX_MIN_SIZE of the screen wide and high;
// false while the actor was not drawn in the last frame or is behind the camera.
struct S_ScreenBox {
    float left, top, right, bottom;
    float width, height;                                // the box's
};
bool ActorScreenBox(I3D_scene *scene, C_Actor *actor, S_ScreenBox &box)
{
    I3D_frame *frame = actor->GetFrame();
    if (frame->GetRenderStamp() != scene->GetRenderStamp())
        return false;
    I3D_camera *camera = GetGame()->GetScene()->GetActiveCamera();
    const S_vector cameraPos = camera->GetWorldPos();
    const S_vector cameraDir = camera->GetMatrix()(2).Normalized();
    const S_vector toActor = (frame->GetWorldPos() - cameraPos).Normalized();
    if (!(0.0f <= toActor.x * cameraDir.x + toActor.y * cameraDir.y + toActor.z * cameraDir.z))
        return false;
    S_vector4 corners[8];
    actor->GetBoxCorners(corners);
    S_vector4 screen[8];
    scene->TransformPoints(corners, screen, 8);
    const float screenWidth = static_cast<float>(GetIGraph()->ScrnX());
    const float screenHeight = static_cast<float>(GetIGraph()->ScrnY());
    box.left = screenWidth;
    box.right = 0.0f;
    box.bottom = 0.0f;
    box.top = screenHeight;
    for (const S_vector4 &point : screen) {
        if (box.right < point.x)
            box.right = point.x;
        if (box.left > point.x)
            box.left = point.x;
        if (box.bottom < point.y)
            box.bottom = point.y;
        if (box.top > point.y)
            box.top = point.y;
    }
    box.width = box.right - box.left;
    box.height = box.bottom - box.top;
    const float minWidth = screenWidth * BOX_MIN_SIZE;
    const float minHeight = screenHeight * BOX_MIN_SIZE;
    if (box.width < minWidth) {
        const float pad = (minWidth - box.width) * 0.5f;
        box.left -= pad;
        box.right += pad;
        box.width = minWidth;
    }
    if (box.height < minHeight) {
        const float pad = (minHeight - box.height) * 0.5f;
        box.top -= pad;
        box.bottom += pad;
        box.height = minHeight;
    }
    return true;
}

// Lines between the points (pairs) in `color`, half opaque.
void DrawScreenLines(const float (*points)[2], int count, const float *color)
{
    S_TLVertex vertices[16];
    const uint32_t argb = HalfColor(color);
    for (int i = 0; i < count; ++i) {
        S_TLVertex &vertex = vertices[i];
        vertex.x = points[i][0];
        vertex.y = points[i][1];
        vertex.z = 0.5f;
        vertex.rhw = 2.0f;
        vertex.color = argb;
        // (The original leaves the specular colour unset.)
        vertex.specular = 0;
        vertex.u = 0.0f;
        vertex.v = 0.0f;
    }
    GetIGraph()->SetTexture(nullptr, 0);
    GetIGraph()->DrawPrimitives(1, count / 2, vertices, 1);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Set-up

void C_CommandPanel::S_Button::Light(bool lit, bool click)
{
    if (click)
        g_clickSound->SetOn(true);
    frame->SetOn(lit);
    unlit->SetOn(!lit);
}

bool C_CommandPanel::S_Button::IsLit() const
{
    return frame->IsOn();
}

bool C_CommandPanel::Init(I3D_scene *scene, C_Interface *ui, I3D_texture **stateIcons, I3D_texture **actionIcons,
                          C_InfoSelector *selector, C_MapCursor *cursor)
{
    m_ui = ui;
    m_stateIcons = stateIcons;
    m_scene = scene;
    m_actionIcons = actionIcons;
    m_selector = selector;
    m_cursor = cursor;
    m_frame = scene->FindFrame("Border", FRAME_MASK_ANY);
    // Fix: the original updated the frame also when it was missing.
    if (m_frame) {
        m_frame->SetOn(false);
        m_frame->Update();
    }
    m_gameScene = GetGame()->GetScene();
    m_active = nullptr;
    m_commanded = nullptr;
    m_target = nullptr;
    m_unknown10 = 0;
    InitButtons();
    InitHints();
    for (int i = 0; i < INFO_ICONS; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "info%02d", i);
        SetFrameId(scene->FindFrame(name, FRAME_MASK_ANY), INFO_ICON_FIRST + i);
    }
    m_markerModel = static_cast<I3D_model *>(I3DGetDriver()->CreateFrame(FRAME_MODEL));
    if (g_modelCache.OpenInto(m_markerModel, "l_aicomm") != I3D_OK) {
        m_markerModel->Release();
        m_markerModel = nullptr;
    }
    m_rollMenu.Init(nullptr, m_scene, true);
    m_realtime = false;
    m_checkpoints = GetGame()->GetCheckpoints();
    for (S_MemberLines &lines : m_lines) {
        lines.pathSegments = 0;
        lines.routeSegments = 0;
    }
    m_refreshPaths = false;
    m_unknown5A7BD = false;
    m_leftDown = false;
    m_rightDown = false;
    m_draggingMap = false;
    m_unknown5A7BC = false;
    // The markers' icons: the stance, pace and AI mode buttons' own.
    static const char *const STANCE_FRAMES[] = {"lie", "kneel", "stay"};
    static const char *const PACE_FRAMES[] = {"slow", "walk", "run", "sprint"};
    static const char *const MODE_FRAMES[] = {"passive", "defensive", "aggresive"};
    for (int i = 0; i < 3; ++i)
        m_stanceIcons[i] = FirstMaterial(m_scene->FindFrame(STANCE_FRAMES[i], FRAME_MASK_ANY))->GetTexture();
    for (int i = 0; i < 4; ++i)
        m_paceIcons[i] = FirstMaterial(m_scene->FindFrame(PACE_FRAMES[i], FRAME_MASK_ANY))->GetTexture();
    for (int i = 0; i < 3; ++i)
        m_modeIcons[i] = FirstMaterial(m_scene->FindFrame(MODE_FRAMES[i], FRAME_MASK_ANY))->GetTexture();
    m_time = 0;
    m_draggingMarker = false;
    return true;
}

void C_CommandPanel::InitButtons()
{
    for (const S_ButtonDef &def : BUTTON_DEFS) {
        S_Button &button = m_buttons[def.id - 1];
        button.frame = m_scene->FindFrame(def.name, FRAME_MASK_ANY);
        I3D_frame *unlit = m_scene->FindFrame(def.unlitName, FRAME_MASK_ANY);
        button.id = def.id;
        SetFrameId(button.frame, def.id);
        button.unlit = unlit;
        unlit->SetOn(false);
        SetFrameId(unlit, def.id);
        unlit->SetPos(button.frame->GetWorldPos());
        button.Light(def.lit, false);
    }
    m_flashButton = nullptr;
    m_flashTime = 0;
    static const char *const ZOOM_FRAMES[ZOOMS] = {"TMcamera1x", "TMcamera2x", "TMcamera4x", "TMcamera8x",
                                                   "TMcamera16x"};
    for (int i = 0; i < ZOOMS; ++i)
        m_zoomFrames[i] = m_scene->FindFrame(ZOOM_FRAMES[i], FRAME_MASK_ANY);
    m_zoom = 1;
    CycleZoom();
}

void C_CommandPanel::InitHints()
{
    for (int i = 0; i < HINTS; ++i) {
        I3D_frame *frame = m_scene->FindFrame(HINT_DEFS[i].frame, FRAME_MASK_ANY);
        m_hints[i].SetUnknown90(0);
        m_hints[i].AttachTo(m_scene, frame);
        m_hints[i].SetTextId(HINT_DEFS[i].title, HINT_STYLE, 0);
        frame->SetOn(false);
    }
}

void C_CommandPanel::DoneHints()
{
    // Quirk kept: the camera's hint text is not cleared.
    for (int i = 0; i < HINTS; ++i) {
        if (i != HINT_CAMERA)
            m_hints[i].Clear();
    }
}

void C_CommandPanel::Done()
{
    g_gameProfile.SetControlMode(BINDING_MODE_GAME);
    DoneHints();
    if (m_markerModel) {
        m_markerModel->Release();
        m_markerModel = nullptr;
    }
    for (std::size_t i = m_spareMarkers.size(); i-- > 0;)
        m_spareMarkers[i]->Release();
    m_spareMarkers.clear();
    m_rollMenu.Done();
}

// ---------------------------------------------------------------------------------------------
// Opening and the frame's update

bool C_CommandPanel::IsOpen() const
{
    return m_frame && m_frame->IsOn();
}

void C_CommandPanel::Open(bool open)
{
    if (!m_frame)
        return;
    m_frame->SetOn(open);
    m_frame->Update();
    GetCameraModule()->GetHumanEffects()->SetTactical(open);
    m_draggingMarker = false;
    C_Team *team = PlayerTeam();
    const std::vector<C_Actor *> &actors = g_actorsModule.GetActors();
    if (open) {
        m_refreshPaths = true;
        m_wheelTime = 0;
        m_unknown5A7BD = false;
        m_leftDown = false;
        m_rightDown = false;
        m_draggingMap = false;
        m_unknown5A7BC = false;
        m_active = team->GetActivePlayer();
        m_commanded = team->GetCommandedMember();
        m_buttons[MAP_BUTTON_REALTIME - 1].Light(m_realtime, true);
        team->RefreshSight();
        GetGame()->SetPaused(!m_realtime);
        ReadOrderFlags();
        team->GetEnemyRecords().SetTacticalView(true);
        // The humans the squad has not seen fade out (not the squad, the allied and neutral
        // NPCs, the unconscious and the dead).
        std::vector<C_Human *> seen;
        team->GetSeenEnemies(seen);
        for (std::size_t i = actors.size(); i-- > 0;) {
            C_Actor *actor = actors[i];
            if (!actor || actor->GetType() != ACTOR_HUMAN)
                continue;
            auto *human = static_cast<C_Human *>(actor);
            if (team->IsMember(human) || human->IsAISuspended() || human->IsUnconscious() || human->IsDead() ||
                human->GetRole() == HUMAN_ROLE_NEUTRAL || human->GetRole() == HUMAN_ROLE_NPC_TEAM)
                continue;
            bool isSeen = false;
            for (C_Human *enemy : seen) {
                if (enemy == human) {
                    isSeen = true;
                    break;
                }
            }
            if (!isSeen)
                human->FadeOut(100);
        }
        // (The third argument is 1 here; the scripts pass 0.)
        if (!m_realtime)
            GetGEffects()->SetThunderstorm(false, nullptr, true);
        g_gameProfile.SetControlMode(BINDING_MODE_COMMAND_MAP);
        return;
    }
    for (int i = 0; i < team->GetMemberCount(); ++i) {
        C_Human *member = team->GetMember(i);
        for (int j = 0; j < member->GetOrderCount(); ++j)
            member->GetOrder(j)->marker->SetOn(false);
    }
    m_ui->HighlightMembers(nullptr, HIGHLIGHT_NONE);
    ShowRollMenu(false, nullptr);
    GetGame()->SetPaused(false);
    GetCameraModule()->GetHumanEffects()->SetTacticalMode(TACTICAL_IDLE, nullptr);
    team->GetEnemyRecords().SetTacticalView(false);
    for (std::size_t i = actors.size(); i-- > 0;) {
        C_Actor *actor = actors[i];
        if (actor && actor->GetType() == ACTOR_HUMAN && !static_cast<C_Human *>(actor)->IsAISuspended())
            static_cast<C_Human *>(actor)->FadeIn(200);
    }
    g_gameProfile.SetControlMode(BINDING_MODE_GAME);
}

void C_CommandPanel::Update(int timeDelta)
{
    if (!m_frame || !IsOpen())
        return;
    UpdateHint(timeDelta);
    if (m_wheelTime != 0) {
        m_wheelTime -= timeDelta;
        if (m_wheelTime < 0)
            m_wheelTime = 0;
    }
    C_Team *team = PlayerTeam();
    m_active = team->GetActivePlayer();
    C_Human *previous = m_commanded;
    m_commanded = team->GetCommandedMember();
    if (m_commanded != previous) {
        m_refreshPaths = true;
        ReadOrderFlags();
        if (m_rollMenu.IsOpen())
            ShowRollMenu(false, nullptr);
    }
    int lastKind = 0;
    if (m_commanded && m_commanded->GetOrderCount() != 0)
        lastKind = m_commanded->GetOrder(m_commanded->GetOrderCount() - 1)->kind;
    LightOrderButtons(lastKind);
    // While the game is paused the bodies are posed here.
    for (int i = 0; i < team->GetMemberCount(); ++i) {
        C_Human *member = team->GetMember(i);
        if (!member)
            continue;
        UpdateMember(member, i);
        if (!m_realtime)
            member->BodyUnknown5ac470(i);
    }
    m_refreshPaths = false;
    if (m_rollMenu.IsOpen()) {
        if (!m_commanded) {
            ShowRollMenu(false, nullptr);
        } else {
            m_rollMenu.SelectAt(&m_cursor->GetPos().x);
            m_rollMenu.Update(timeDelta);
        }
    }
    if (m_flashButton) {
        m_flashTime -= timeDelta;
        if (m_flashTime <= 0) {
            m_flashButton->Light(!m_flashButton->IsLit(), false);
            m_flashButton = nullptr;
        }
    }
    if (!m_leftDown && !m_rightDown)
        PickTarget(timeDelta);
    GetCameraModule()->GetHumanEffects()->UpdateTactical(timeDelta);
    if (!m_realtime) {
        GetGEffects()->Update(timeDelta);
        std::vector<C_Human *> seen;
        team->GetSeenEnemies(seen);
        for (std::size_t i = seen.size(); i-- > 0;)
            seen[i]->BodyUnknown5ac470(static_cast<int>(i));
    }
    m_ui->HighlightMembers(nullptr, HIGHLIGHT_COMMANDED);
    // The turning cursor spins.
    m_time += timeDelta;
    if (m_cursor->GetMode() == CURSOR_ROTATE)
        m_cursor->GetFrame()->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), static_cast<float>(-m_time) * 0.003f);
}

// The hint texts.

void C_CommandPanel::ShowHint(int group, int textId)
{
    for (int i = 0; i < HINTS; ++i)
        m_hints[i].SetTextId(HINT_DEFS[i].title, HINT_STYLE, 0);
    if (!m_rollMenu.IsOpen() && group != HINT_NONE)
        m_hints[group].SetTextId(textId, HINT_STYLE, 0);
}

void C_CommandPanel::UpdateHint(int /*timeDelta*/)
{
    const int id = m_cursor->GetHoveredId();
    for (const S_ButtonHint &hint : BUTTON_HINTS) {
        if (hint.id == id) {
            ShowHint(hint.group, hint.text);
            return;
        }
    }
    ShowHint(HINT_NONE, 0);
}

// ---------------------------------------------------------------------------------------------
// The buttons

void C_CommandPanel::ReadOrderFlags()
{
    if (S_HumanOrder *order = m_selector->GetSelectedOrder()) {
        // The order's unset flags get the defaults.
        if (!(order->flags & MOVE_PACE_MASK))
            order->flags |= MOVE_PACE_4;
        m_pace = order->flags & MOVE_PACE_MASK;
        if (!(order->flags & MOVE_AI_MODE_MASK))
            order->flags |= 0x200;
        m_mode = order->flags & MOVE_AI_MODE_MASK;
        if (!(order->flags & MOVE_STANCE_MASK))
            order->flags |= MOVE_STANDING;
        m_stance = order->flags & MOVE_STANCE_MASK;
    } else if (m_commanded) {
        m_pace = m_commanded->GetStateFlags();
        m_mode = m_commanded->GetAIModeFlags();
        m_stance = m_commanded->GetStanceFlags();
    } else {
        m_pace = FLAGS_NONE;
        m_mode = FLAGS_NONE;
        m_stance = FLAGS_NONE;
    }
    LightFlagButtons();
    g_clickSound->SetOn(true);
}

void C_CommandPanel::DarkenButtons(bool dark)
{
    if (dark) {
        for (int i = BUTTONS; i-- > 0;)
            m_buttons[i].Light(false, false);
        return;
    }
    LightFlagButtons();
    m_buttons[MAP_BUTTON_FOLLOW - 1].Light(true, false);
    m_buttons[MAP_BUTTON_DELETE - 1].Light(true, false);
    m_buttons[MAP_BUTTON_DELETE_ALL - 1].Light(true, false);
    m_buttons[MAP_BUTTON_WAIT_1 - 1].Light(true, false);
    m_buttons[MAP_BUTTON_WAIT_2 - 1].Light(true, false);
    m_buttons[MAP_BUTTON_OPTIONS - 1].Light(false, false);
    m_buttons[MAP_BUTTON_ZOOM - 1].Light(true, false);
    m_buttons[MAP_BUTTON_REALTIME - 1].Light(!GetGame()->IsPaused(), false);
}

void C_CommandPanel::LightOrderButtons(int lastKind)
{
    // A follow or wait order ends the queue.
    const bool lit = lastKind != HUMAN_ORDER_6 && lastKind != HUMAN_ORDER_7 && lastKind != HUMAN_ORDER_FOLLOW;
    m_buttons[MAP_BUTTON_FOLLOW - 1].Light(lit, false);
    m_buttons[MAP_BUTTON_WAIT_1 - 1].Light(lit, false);
    m_buttons[MAP_BUTTON_WAIT_2 - 1].Light(lit, false);
}

void C_CommandPanel::LightFlagButtons()
{
    // Lights `lit` of the buttons first..last with a click and puts out the others (0: all).
    auto lightGroup = [this](int first, int last, int lit) {
        for (int id = first; id <= last; ++id)
            m_buttons[id - 1].Light(id == lit, id == lit);
    };
    // (The first flag of a group that is set counts; with none of them the group stays.)
    if (m_pace == FLAGS_NONE)
        lightGroup(MAP_BUTTON_SPRINT, MAP_BUTTON_SLOW, 0);
    else if (m_pace & MOVE_PACE_MARCH)
        lightGroup(MAP_BUTTON_SPRINT, MAP_BUTTON_SLOW, MAP_BUTTON_SPRINT);
    else if (m_pace & MOVE_PACE_4)
        lightGroup(MAP_BUTTON_SPRINT, MAP_BUTTON_SLOW, MAP_BUTTON_RUN);
    else if (m_pace & MOVE_PACE_3)
        lightGroup(MAP_BUTTON_SPRINT, MAP_BUTTON_SLOW, MAP_BUTTON_WALK);
    else if (m_pace & MOVE_PACE_1)
        lightGroup(MAP_BUTTON_SPRINT, MAP_BUTTON_SLOW, MAP_BUTTON_SLOW);
    if (m_mode == FLAGS_NONE)
        lightGroup(MAP_BUTTON_PASSIVE, MAP_BUTTON_AGGRESSIVE, 0);
    else if (m_mode & 0x400)
        lightGroup(MAP_BUTTON_PASSIVE, MAP_BUTTON_AGGRESSIVE, MAP_BUTTON_PASSIVE);
    else if (m_mode & 0x200)
        lightGroup(MAP_BUTTON_PASSIVE, MAP_BUTTON_AGGRESSIVE, MAP_BUTTON_DEFENSIVE);
    else if (m_mode & 0x100)
        lightGroup(MAP_BUTTON_PASSIVE, MAP_BUTTON_AGGRESSIVE, MAP_BUTTON_AGGRESSIVE);
    if (m_stance == FLAGS_NONE)
        lightGroup(MAP_BUTTON_STAND, MAP_BUTTON_LIE, 0);
    else if (m_stance & MOVE_STANDING)
        lightGroup(MAP_BUTTON_STAND, MAP_BUTTON_LIE, MAP_BUTTON_STAND);
    else if (m_stance & MOVE_CROUCHING)
        lightGroup(MAP_BUTTON_STAND, MAP_BUTTON_LIE, MAP_BUTTON_KNEEL);
    else if (m_stance & MOVE_LYING)
        lightGroup(MAP_BUTTON_STAND, MAP_BUTTON_LIE, MAP_BUTTON_LIE);
}

void C_CommandPanel::Press(int id)
{
    // (Not while a pressed button is out.)
    if (m_flashTime > 0)
        return;
    if (id >= MAP_BUTTON_STAND && id <= MAP_BUTTON_AGGRESSIVE) {
        if (!m_buttons[id - 1].IsLit())
            SetFlag(id, id - 1);
    } else if (id >= MAP_BUTTON_OPTIONS && id <= MAP_BUTTON_REALTIME) {
        PressToolButton(id, id - 1);
    } else if (id >= MAP_BUTTON_FOLLOW && id <= MAP_BUTTON_WAIT_2) {
        PressOrderButton(id, id - 1);
    } else if (id >= INFO_ICON_FIRST && id < INFO_ICON_FIRST + INFO_ICONS) {
        m_selector->SelectOrderIndex(id - INFO_ICON_FIRST);
        ReadOrderFlags();
    }
}

void C_CommandPanel::SetFlag(int id, int /*index*/)
{
    switch (id) {
    case MAP_BUTTON_STAND:
    case MAP_BUTTON_KNEEL:
    case MAP_BUTTON_LIE: {
        static const uint32_t STANCES[] = {MOVE_STANDING, MOVE_CROUCHING, MOVE_LYING};
        m_stance = STANCES[id - MAP_BUTTON_STAND];
        for (int other = MAP_BUTTON_STAND; other <= MAP_BUTTON_LIE; ++other)
            m_buttons[other - 1].Light(other == id, other == id);
        break;
    }
    case MAP_BUTTON_SPRINT:
    case MAP_BUTTON_RUN:
    case MAP_BUTTON_WALK:
    case MAP_BUTTON_SLOW: {
        static const uint32_t PACES[] = {MOVE_PACE_MARCH, MOVE_PACE_4, MOVE_PACE_3, MOVE_PACE_1};
        m_pace = PACES[id - MAP_BUTTON_SPRINT];
        for (int other = MAP_BUTTON_SPRINT; other <= MAP_BUTTON_SLOW; ++other)
            m_buttons[other - 1].Light(other == id, other == id);
        break;
    }
    case MAP_BUTTON_PASSIVE:
    case MAP_BUTTON_DEFENSIVE:
    case MAP_BUTTON_AGGRESSIVE: {
        static const uint32_t MODES[] = {0x400, 0x200, 0x100};
        m_mode = MODES[id - MAP_BUTTON_PASSIVE];
        for (int other = MAP_BUTTON_PASSIVE; other <= MAP_BUTTON_AGGRESSIVE; ++other)
            m_buttons[other - 1].Light(other == id, other == id);
        break;
    }
    default:
        break;
    }
    ApplyFlags();
}

void C_CommandPanel::PressToolButton(int id, int index)
{
    S_Button &button = m_buttons[index];
    switch (id) {
    case MAP_BUTTON_OPTIONS:
        button.Light(!button.IsLit(), true);
        m_flashButton = &button;
        m_flashTime = FLASH_TIME;
        GetGame()->OpenGameMenu();
        break;
    case MAP_BUTTON_ZOOM:
        button.Light(false, true);
        CycleZoom();
        m_flashButton = &button;
        m_flashTime = FLASH_TIME;
        break;
    case MAP_BUTTON_REALTIME:
        m_realtime = !m_realtime;
        button.Light(m_realtime, true);
        GetGame()->SetPaused(!m_realtime);
        break;
    default:
        break;
    }
}

void C_CommandPanel::PressOrderButton(int id, int index)
{
    S_Button &button = m_buttons[index];
    C_Team *team = PlayerTeam();
    if (!button.IsLit() || team->GetCommandedIndex() < 0)
        return;
    C_Human *commanded = team->GetCommandedMember();
    switch (id) {
    case MAP_BUTTON_FOLLOW: {
        // From the last order's place.
        const int count = commanded->GetOrderCount();
        const S_vector4 pos =
            count != 0 ? OrderPoint(commanded->GetOrder(count - 1)) : FramePos4(commanded->GetFrame());
        AddOrder(HUMAN_ORDER_FOLLOW, pos, S_vector4{0.0f, 0.0f, 0.0f, 0.0f});
        button.Light(false, true);
        break;
    }
    case MAP_BUTTON_DELETE:
        button.Light(false, true);
        RemoveSelectedOrder();
        break;
    case MAP_BUTTON_DELETE_ALL:
        button.Light(false, true);
        PlayerTeam()->GetCommandedMember()->CancelOrders();
        break;
    case MAP_BUTTON_WAIT_1:
    case MAP_BUTTON_WAIT_2: {
        // At the last order's place, facing its way (or the soldier's).
        S_vector4 pos;
        S_vector4 dir;
        const int count = commanded->GetOrderCount();
        if (count != 0) {
            const S_HumanOrder *last = commanded->GetOrder(count - 1);
            pos = OrderPoint(last);
            dir = last->data;
        } else {
            I3D_frame *frame = commanded->GetFrame();
            pos = FramePos4(frame);
            const S_matrix &m = frame->GetMatrix();
            dir = Vector4(m(2).Normalized(), m.m[2][3]);
        }
        AddOrder(id == MAP_BUTTON_WAIT_1 ? HUMAN_ORDER_6 : HUMAN_ORDER_7, pos, dir);
        button.Light(false, true);
        break;
    }
    default:
        break;
    }
    m_flashButton = &button;
    m_flashTime = FLASH_TIME;
}

void C_CommandPanel::CycleZoom()
{
    float zoom = 1.0f;
    switch (m_zoom) {
    case 0:
        m_zoom = 1;
        zoom = 0.5f;
        break;
    case 1:
        m_zoom = 2;
        zoom = 1.0f;
        break;
    case 2:
        m_zoom = 3;
        zoom = 2.0f;
        break;
    case 3:
        m_zoom = 4;
        zoom = 4.0f;
        break;
    case 4:
        m_zoom = 0;
        zoom = 0.25f;
        break;
    default:
        break;
    }
    for (int i = 0; i < ZOOMS; ++i)
        m_zoomFrames[i]->SetOn(i == m_zoom);
    GetCameraModule()->GetHumanEffects()->SetTacticalZoom(zoom);
}

// ---------------------------------------------------------------------------------------------
// The orders

void C_CommandPanel::RemoveSelectedOrder()
{
    const int index = m_selector->GetSelectedIndex();
    if (index > -1)
        PlayerTeam()->GetCommandedMember()->RemoveOrder(index);
}

void C_CommandPanel::RemoveOrderAt(int id)
{
    if (id >= INFO_ICON_FIRST && id < INFO_ICON_FIRST + INFO_ICONS && m_commanded) {
        m_commanded->RemoveOrder(id - INFO_ICON_FIRST);
        ReadOrderFlags();
    }
}

void C_CommandPanel::AddOrder(int kind, const S_vector4 &pos, const S_vector4 &data)
{
    C_Human *human = PlayerTeam()->GetCommandedMember();
    const int index = m_selector->GetSelectedIndex();
    S_HumanOrder *order = AllocHumanOrder(human, kind, -1, pos, data, m_pace, m_stance, m_mode, nullptr, 0);
    if (kind != HUMAN_ORDER_FOLLOW && index > -1)
        human->InsertOrder(order, index);
    else
        human->AddOrder(order);
}

void C_CommandPanel::AddActorOrder(int command, C_Actor *actor, int entry)
{
    C_Human *human = PlayerTeam()->GetCommandedMember();
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
    S_HumanOrder *order = AllocHumanOrder(human, HUMAN_ORDER_ACT, command, zero, zero, m_pace, m_stance, m_mode,
                                          actor, entry);
    human->AddOrder(order);
}

void C_CommandPanel::ApplyFlags()
{
    const int index = m_selector->GetSelectedIndex();
    if (index == -1)
        return;
    C_Human *human = PlayerTeam()->GetCommandedMember();
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
    S_HumanOrder change;
    change.InitPlain(human, zero, zero, m_pace, m_stance, m_mode);
    human->SetOrder(index, &change);
}

// ---------------------------------------------------------------------------------------------
// The order markers

I3D_frame *C_CommandPanel::AcquireOrderMarker(S_HumanOrder *order)
{
    if (!m_markerModel)
        return nullptr;
    I3D_frame *marker;
    bool fresh = true;
    if (!m_spareMarkers.empty()) {
        marker = m_spareMarkers.back();
        fresh = false;
        m_spareMarkers.pop_back();
    } else {
        marker = I3DGetDriver()->CreateFrame(FRAME_MODEL);
        marker->Duplicate(m_markerModel);
    }
    const S_vector white(1.0f, 1.0f, 1.0f);
    // The order's kind.
    I3D_frame *part = marker->FindChildFrame("3Dcommand", FRAME_MASK_VISUAL);
    if (fresh)
        PrepareMarkerPart(part, white, true);
    I3D_texture *icon = nullptr;
    switch (order->kind) {
    case HUMAN_ORDER_MOVE:
        icon = m_stateIcons[10];
        break;
    case HUMAN_ORDER_WATCH:
        icon = m_stateIcons[11];
        break;
    case HUMAN_ORDER_ACT:
        icon = m_actionIcons[order->arg];
        break;
    case HUMAN_ORDER_FOLLOW:
        icon = m_stateIcons[19];
        break;
    case HUMAN_ORDER_6:
        icon = m_stateIcons[13];
        break;
    case HUMAN_ORDER_7:
        icon = m_stateIcons[14];
        break;
    default:
        break;
    }
    if (icon)
        FirstMaterial(part)->SetTexture(icon);
    // The stance (the order's unset flags get the defaults).
    part = marker->FindChildFrame("3Dpose", FRAME_MASK_VISUAL);
    if (fresh)
        PrepareMarkerPart(part, white, true);
    if (!(order->flags & MOVE_STANCE_MASK))
        order->flags |= MOVE_STANDING;
    // (One of the stances is set: `icon` keeps the kind's otherwise.)
    if (order->flags & MOVE_LYING)
        icon = m_stanceIcons[0];
    else if (order->flags & MOVE_CROUCHING)
        icon = m_stanceIcons[1];
    else if (order->flags & MOVE_STANDING)
        icon = m_stanceIcons[2];
    FirstMaterial(part)->SetTexture(icon);
    // The pace. Quirk kept: a pace of none of the buttons shows the stance's icon.
    part = marker->FindChildFrame("3Dmove", FRAME_MASK_VISUAL);
    if (fresh)
        PrepareMarkerPart(part, white, true);
    if (!(order->flags & MOVE_PACE_MASK))
        order->flags |= MOVE_PACE_4;
    if (order->flags & MOVE_PACE_1)
        icon = m_paceIcons[0];
    else if (order->flags & MOVE_PACE_3)
        icon = m_paceIcons[1];
    else if (order->flags & MOVE_PACE_4)
        icon = m_paceIcons[2];
    else if (order->flags & MOVE_PACE_MARCH)
        icon = m_paceIcons[3];
    FirstMaterial(part)->SetTexture(icon);
    // The AI mode.
    part = marker->FindChildFrame("3Dstand", FRAME_MASK_VISUAL);
    if (fresh) {
        auto *visual = static_cast<I3D_object_visual *>(part);
        visual->DuplicateMesh();
        visual->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].DuplicateMaterial();
        // (The original first gives it the pace's icon, replaced below.)
        FirstMaterial(part)->SetTexture(icon);
        PrepareMarkerPart(part, white, false);
    }
    if (!(order->flags & MOVE_AI_MODE_MASK))
        order->flags |= 0x200;
    if (order->flags & 0x400)
        icon = m_modeIcons[0];
    else if (order->flags & 0x200)
        icon = m_modeIcons[1];
    else if (order->flags & 0x100)
        icon = m_modeIcons[2];
    FirstMaterial(part)->SetTexture(icon);
    // The chosen order's mark (red; the model's material, not a copy).
    part = marker->FindChildFrame("selector_commands", FRAME_MASK_VISUAL);
    if (fresh)
        PrepareMarkerPart(part, S_vector(2.0f, 0.0f, 0.0f), false);
    part->Update();
    marker->SetOn(false);
    marker->Update();
    return marker;
}

void C_CommandPanel::ReleaseOrderMarker(I3D_frame *marker)
{
    marker->SetOn(false);
    m_spareMarkers.push_back(marker);
}

void C_CommandPanel::UpdateOrderMarker(S_HumanOrder *order, I3D_frame *marker)
{
    if (!m_markerModel)
        return;
    I3D_frame *part = marker->FindChildFrame("3Dcommand", FRAME_MASK_VISUAL);
    I3D_texture *icon = nullptr;
    switch (order->kind) {
    case HUMAN_ORDER_MOVE:
        icon = m_stateIcons[10];
        break;
    case HUMAN_ORDER_WATCH:
        icon = m_stateIcons[11];
        break;
    case HUMAN_ORDER_ACT:
        icon = m_actionIcons[order->arg];
        break;
    case HUMAN_ORDER_FOLLOW:
        icon = m_stateIcons[19];
        break;
    case HUMAN_ORDER_6:
        icon = m_stateIcons[13];
        break;
    case HUMAN_ORDER_7:
        icon = m_stateIcons[14];
        break;
    default:
        break;
    }
    if (icon)
        FirstMaterial(part)->SetTexture(icon);
    part->Update();
    part = marker->FindChildFrame("3Dpose", FRAME_MASK_VISUAL);
    if (!(order->flags & MOVE_STANCE_MASK))
        order->flags |= MOVE_STANDING;
    icon = nullptr;
    if (order->flags & MOVE_LYING)
        icon = m_stanceIcons[0];
    else if (order->flags & MOVE_CROUCHING)
        icon = m_stanceIcons[1];
    else if (order->flags & MOVE_STANDING)
        icon = m_stanceIcons[2];
    if (icon)
        FirstMaterial(part)->SetTexture(icon);
    part->Update();
    part = marker->FindChildFrame("3Dmove", FRAME_MASK_VISUAL);
    if (!(order->flags & MOVE_PACE_MASK))
        order->flags |= MOVE_PACE_4;
    // (Here a pace of none of the buttons leaves no icon.)
    icon = nullptr;
    if (order->flags & MOVE_PACE_1)
        icon = m_paceIcons[0];
    else if (order->flags & MOVE_PACE_3)
        icon = m_paceIcons[1];
    else if (order->flags & MOVE_PACE_4)
        icon = m_paceIcons[2];
    else if (order->flags & MOVE_PACE_MARCH)
        icon = m_paceIcons[3];
    FirstMaterial(part)->SetTexture(icon);
    part->Update();
    part = marker->FindChildFrame("3Dstand", FRAME_MASK_VISUAL);
    if (!(order->flags & MOVE_AI_MODE_MASK))
        order->flags |= 0x200;
    icon = nullptr;
    if (order->flags & 0x400)
        icon = m_modeIcons[0];
    else if (order->flags & 0x200)
        icon = m_modeIcons[1];
    else if (order->flags & 0x100)
        icon = m_modeIcons[2];
    FirstMaterial(part)->SetTexture(icon);
    part->Update();
    marker->SetOn(false);
    marker->Update();
}

// ---------------------------------------------------------------------------------------------
// The order lines

void C_CommandPanel::AddPath(int member, const float *color, const S_Vector4Array &path)
{
    // Fix: the original wrote past the six members' buffers (a squad of more members), past a
    // buffer's points, and before it after an empty path (its segment count became -1).
    if (member < 0 || member >= MEMBERS || path.count <= 0)
        return;
    S_MemberLines &lines = m_lines[member];
    // The path starts where the last one ended: its first point replaces that one.
    const int first = lines.pathSegments;
    const uint32_t argb = HalfColor(color);
    int end = first;
    for (int i = 0; i < path.count && end < PATH_VERTICES; ++i, ++end) {
        S_LineVertex &vertex = lines.paths[end];
        vertex.x = path.data[i].x;
        vertex.y = path.data[i].y + LINE_LIFT;
        vertex.z = path.data[i].z;
        vertex.color = argb;
        vertex.u = 0.0f;
        vertex.v = 0.0f;
    }
    lines.pathSegments = end - 1;
}

void C_CommandPanel::UpdateMember(C_Human *member, int index)
{
    if (!m_realtime && !member->HaveOrdersChanged() && !m_refreshPaths)
        return;
    // Fix: a squad of more than six members wrote past the lines' buffers (its markers are kept
    // up to date all the same).
    S_MemberLines *lines = index < MEMBERS ? &m_lines[index] : nullptr;
    if (member->GetOrderCount() == 0) {
        if (lines) {
            lines->pathSegments = 0;
            lines->routeSegments = 0;
        }
        return;
    }
    if (lines)
        lines->pathSegments = 0;
    S_vector4 from = FramePos4(member->GetFrame());
    for (int i = 0; i < member->GetOrderCount(); ++i) {
        S_HumanOrder *order = member->GetOrder(i);
        order->UpdatePath(from);
        AddPath(index, member == m_commanded ? COLOR_BLUE : COLOR_BLACK, order->path);
        order->marker->SetOn(true);
        I3D_frame *selection = order->marker->FindChildFrame("selector_commands", FRAME_MASK_VISUAL);
        selection->SetOn(m_commanded && member == m_commanded && m_selector->GetSelectedIndex() == i);
        from = OrderPoint(order);
    }
}

void C_CommandPanel::RenderOverlay()
{
    for (S_MemberLines &lines : m_lines) {
        if (lines.routeSegments != 0) {
            GetIGraph()->SetTexture(nullptr, 0);
            GetIGraph()->SetWorldMatrix(IdentityMatrix());
            GetIGraph()->DrawPrimitives(2, lines.routeSegments, lines.routes, 0);
        }
        if (lines.pathSegments != 0) {
            GetIGraph()->SetTexture(nullptr, 0);
            GetIGraph()->SetWorldMatrix(IdentityMatrix());
            GetIGraph()->DrawPrimitives(2, lines.pathSegments, lines.paths, 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The brackets and boxes

void C_CommandPanel::Render()
{
    // The enemies seen (the dead and unconscious black).
    std::vector<C_Human *> enemies;
    PlayerTeam()->GetSeenEnemies(enemies);
    for (std::size_t i = enemies.size(); i-- > 0;) {
        C_Human *enemy = enemies[i];
        DrawBox(enemy, enemy->IsDead() || enemy->IsUnconscious() ? COLOR_BLACK : COLOR_RED);
    }
    if (m_active)
        DrawBrackets(m_active->GetVehicle() ? m_active->GetVehicle() : m_active, COLOR_GREEN, 0.0f);
    if (m_commanded)
        DrawBrackets(m_commanded->GetVehicle() ? m_commanded->GetVehicle() : m_commanded, COLOR_BLUE, -BRACKETS_GROW);
    if (m_target)
        DrawBrackets(m_target, COLOR_WHITE, BRACKETS_GROW);
    if (m_draggingMap)
        m_cursor->SetFromWorld(m_cursorPoint);
}

void C_CommandPanel::RenderMarkers()
{
    if (m_rollMenu.IsOpen())
        m_rollMenu.Render();
    for (C_TextBox &hint : m_hints)
        hint.Render();
}

void C_CommandPanel::DrawBrackets(C_Actor *actor, const float *color, float grow)
{
    if (!actor)
        return;
    S_ScreenBox box;
    if (!ActorScreenBox(m_gameScene, actor, box))
        return;
    const float cornerWidth = box.width * 0.25f;
    const float cornerHeight = box.height * 0.25f;
    // (The corners are measured on the box before it grows.)
    const float growX = static_cast<float>(GetIGraph()->ScrnX()) * grow;
    const float growY = static_cast<float>(GetIGraph()->ScrnY()) * grow;
    const float left = box.left - growX;
    const float right = box.right + growX;
    const float top = box.top - growY;
    const float bottom = box.bottom + growY;
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
    DrawScreenLines(points, 16, color);
}

void C_CommandPanel::DrawBox(C_Actor *actor, const float *color)
{
    if (!actor)
        return;
    // A crew is boxed once, as its vehicle, by its first occupant.
    if (C_Actor *vehicle = static_cast<C_Human *>(actor)->GetVehicle()) {
        for (int seat = 0; seat < vehicle->GetSeatCount(); ++seat) {
            if (C_Actor *occupant = vehicle->GetSeatOccupant(seat)) {
                if (occupant != actor)
                    return;
                actor = vehicle;
                break;
            }
        }
    }
    S_ScreenBox box;
    if (!ActorScreenBox(m_gameScene, actor, box))
        return;
    const float points[8][2] = {
        {box.left, box.top}, {box.right, box.top},
        {box.right, box.top}, {box.right, box.bottom},
        {box.right, box.bottom}, {box.left, box.bottom},
        {box.left, box.bottom}, {box.left, box.top},
    };
    DrawScreenLines(points, 8, color);
}

// ---------------------------------------------------------------------------------------------
// The mouse on the map

bool C_CommandPanel::IsOnMap(const S_vector4 &pos) const
{
    return pos.x > 0.01f && pos.x < 0.99f && pos.y < -0.01f && pos.y > -0.61f;
}

void C_CommandPanel::CursorRay(S_vector &origin, S_vector &dir) const
{
    int x;
    int y;
    m_cursor->GetScreenPos(&x, &y);
    S_vector4 origin4;
    S_vector4 dir4;
    m_gameScene->UnmapScreenPoint(x, y, origin4, dir4);
    origin = S_vector(origin4.x, origin4.y, origin4.z);
    dir = S_vector(dir4.x, dir4.y, dir4.z);
}

void C_CommandPanel::PickTarget(int /*timeDelta*/)
{
    if (m_rollMenu.IsOpen())
        return;
    m_unknown10 = 0;
    if (!IsOnMap(m_cursor->GetPos()))
        return;
    S_vector origin;
    S_vector dir;
    CursorRay(origin, dir);
    S_vector *point = reinterpret_cast<S_vector *>(&m_cursorPoint);
    const S_CollisionVolume *hit = g_collisionWorld.RayTest(origin, dir * RAY_LENGTH, point, nullptr, 0,
                                                            PICK_EXCLUDE_FLAGS);
    if (!hit) {
        // (w: a stale local in the original.)
        *point = dir * RAY_LENGTH + origin;
        m_target = nullptr;
        return;
    }
    *point -= dir * PICK_PULL_BACK;
    if (!(hit->flags & COLLISION_FLAG_DYNAMIC)) {
        m_target = nullptr;
        return;
    }
    m_target = hit->actor;
    if (!m_target)
        return;
    switch (m_target->GetType()) {
    case ACTOR_HUMAN: {
        auto *human = static_cast<C_Human *>(m_target);
        if (human->IsFadedOut())
            m_target = nullptr;
        else if (human->GetVehicle())
            m_target = human->GetVehicle();
        // A living neutral is no target (unless he is a member).
        else if (human->GetRole() == HUMAN_ROLE_NEUTRAL && !human->IsDead() && !PlayerTeam()->IsMember(human))
            m_target = nullptr;
        break;
    }
    case ACTOR_STATIC_WEAPON:
        if (static_cast<C_StaticWeapon *>(m_target)->GetState() != 1)
            m_target = nullptr;
        break;
    case ACTOR_ANIMATION:
        if (static_cast<C_AnimationActor *>(m_target)->ShowsNoInfo())
            m_target = nullptr;
        break;
    default:
        break;
    }
}

void C_CommandPanel::OnLeftPress()
{
    m_draggingMap = false;
    m_unknown5A7BC = false;
    m_unknown5A7BD = false;
    m_leftDown = true;
    if (const int id = m_cursor->GetHoveredId()) {
        Press(id);
        return;
    }
    if (!IsOnMap(m_cursor->GetPos()))
        return;
    if (m_selector->GetSelectedOrder()) {
        m_selector->SelectOrderIndex(-1);
        ReadOrderFlags();
    }
    S_vector origin;
    S_vector dir;
    CursorRay(origin, dir);
    S_vector hitPos;
    const S_CollisionVolume *hit = g_collisionWorld.RayTest(origin, dir * RAY_LENGTH, &hitPos, nullptr, 0, 0);
    if (hit) {
        if ((hit->flags & COLLISION_FLAG_DYNAMIC) && (hit->type & ORDER_VOLUME_TYPE)) {
            // An order's marker taken: its soldier is commanded, the order chosen.
            auto *order = reinterpret_cast<S_HumanOrder *>(hit->frame->GetData());
            m_dragMarker = FramePos4(order->marker);
            m_draggingMarker = true;
            // (w: a stale local in the original.)
            m_dragPoint = Vector4(hitPos, m_dragPoint.w);
            PlayerTeam()->SetCommanded(order->human);
            m_selector->SelectOrder(order);
            ReadOrderFlags();
            return;
        }
        // A soldier of the squad (or the first one in a vehicle) clicked: he is commanded.
        C_Actor *target = m_target;
        if (target && target != m_commanded) {
            switch (target->GetType()) {
            case ACTOR_HUMAN: {
                auto *human = static_cast<C_Human *>(target);
                if (!human->IsDead() && PlayerTeam()->IsMember(human)) {
                    PlayerTeam()->SetCommanded(human);
                    return;
                }
                break;
            }
            case ACTOR_CAR:
            case ACTOR_STATIC_WEAPON:
            case ACTOR_SUBMARINE:
                for (int seat = 0; seat < target->GetSeatCount(); ++seat) {
                    auto *occupant = static_cast<C_Human *>(target->GetSeatOccupant(seat));
                    if (occupant) {
                        if (!occupant->IsDead() && PlayerTeam()->IsMember(occupant)) {
                            PlayerTeam()->SetCommanded(occupant);
                            return;
                        }
                        break;
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    // Otherwise the map is dragged (the cursor kept on the panel).
    m_draggingMap = true;
    m_cursor->SetClipRect(&static_cast<I3D_visual *>(m_frame)->GetWorldBound().min.x);
    m_cursor->SetMode(CURSOR_MOVE);
}

void C_CommandPanel::OnLeftDrag(float * /*held*/, float * /*pressed*/)
{
    if (!m_draggingMarker)
        return;
    if (!m_selector->GetSelectedOrder())
        return;
    // The marker stays under the cursor where it was gripped.
    const S_vector grip = XYZ(m_dragPoint) - XYZ(m_dragMarker);
    S_vector4 screenPoint;
    S_vector4 screenMarker;
    m_gameScene->TransformPoints(&m_dragPoint, &screenPoint, 1);
    m_gameScene->TransformPoints(&m_dragMarker, &screenMarker, 1);
    const int offsetX = static_cast<int>(screenPoint.x - screenMarker.x);
    const int offsetY = static_cast<int>(screenPoint.y - screenMarker.y);
    const S_vector4 cursor = m_cursor->GetPos();
    const float width = static_cast<float>(GetIGraph()->ScrnX());
    const float height = static_cast<float>(GetIGraph()->ScrnY());
    const int x = static_cast<int>(cursor.x * width) - offsetX;
    const int y = static_cast<int>(static_cast<float>(cursor.y * -1.3333333333333333) * height) - offsetY;
    S_vector4 origin4;
    S_vector4 dir4;
    m_gameScene->UnmapScreenPoint(x, y, origin4, dir4);
    const S_vector origin(origin4.x, origin4.y, origin4.z);
    const S_vector dir(dir4.x, dir4.y, dir4.z);
    S_vector hitPos;
    S_vector normal;
    if (!CollisionRayTest(origin, dir * RAY_LENGTH, &hitPos, &normal, 0xFFFFFFFFu, 0))
        return;
    if (std::fabs(normal.y) < FLAT_GROUND)
        return;
    // (The w of both: stale locals in the original.)
    m_dragMarker = Vector4(hitPos, m_dragMarker.w);
    m_dragPoint = Vector4(grip + hitPos, m_dragPoint.w);
    C_Human *human = PlayerTeam()->GetCommandedMember();
    S_HumanOrder *order = m_selector->GetSelectedOrder();
    if ((XYZ(OrderPoint(order)) - hitPos).Square() < MARKER_MOVE2)
        return;
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};
    if (order->kind == HUMAN_ORDER_6 || order->kind == HUMAN_ORDER_7 || order->kind == HUMAN_ORDER_WATCH) {
        // A waiting order stays; it turns towards the point.
        S_vector dirTo = hitPos - XYZ(OrderPoint(order));
        dirTo.Normalize();
        S_HumanOrder change;
        change.InitPlain(human, zero, Vector4(dirTo, 0.0f), 0, 0, 0);
        human->SetOrder(m_selector->GetSelectedIndex(), &change);
        return;
    }
    S_HumanOrder change;
    change.InitPlain(human, Vector4(hitPos, 0.0f), zero, 0, 0, 0);
    human->SetOrder(m_selector->GetSelectedIndex(), &change);
    // A waiting order right after it goes along.
    if (m_selector->GetSelectedIndex() < human->GetOrderCount() - 1) {
        const int next = m_selector->GetSelectedIndex() + 1;
        const int kind = human->GetOrder(next)->kind;
        if (kind == HUMAN_ORDER_WATCH || kind == HUMAN_ORDER_6 || kind == HUMAN_ORDER_7) {
            S_HumanOrder follow;
            follow.InitPlain(human, Vector4(hitPos, 0.0f), zero, 0, 0, 0);
            human->SetOrder(next, &follow);
        }
    }
}

void C_CommandPanel::OnLeftRelease()
{
    m_draggingMarker = false;
    m_draggingMap = false;
    m_unknown5A7BC = false;
    m_leftDown = false;
    m_unknown5A7BD = false;
    m_cursor->SetClipRect(nullptr);
    m_cursor->SetMode(CURSOR_POINTER);
}

void C_CommandPanel::OnRightPress()
{
    m_rightDown = true;
    if (const int id = m_cursor->GetHoveredId()) {
        RemoveOrderAt(id);
        return;
    }
    if (!IsOnMap(m_cursor->GetPos()))
        return;
    S_vector origin;
    S_vector dir;
    CursorRay(origin, dir);
    S_vector hitPos;
    S_vector normal;
    const S_CollisionVolume *hit = g_collisionWorld.RayTest(origin, dir * RAY_LENGTH, &hitPos, &normal, 0,
                                                            PICK_EXCLUDE_FLAGS);
    if (!hit)
        return;
    // Nothing more after a follow order.
    auto followsLast = [this]() {
        return m_commanded && m_commanded->GetOrderCount() != 0 &&
               m_commanded->GetOrder(m_commanded->GetOrderCount() - 1)->kind == HUMAN_ORDER_FOLLOW;
    };
    if (!(hit->flags & COLLISION_FLAG_DYNAMIC)) {
        // The ground: the commanded soldier goes there, facing away from where he is.
        if (!(normal.y > FLAT_GROUND) || PlayerTeam()->GetCommandedIndex() < 0 || followsLast())
            return;
        S_vector away = hitPos - XYZ(PlayerTeam()->GetCommandedMember()->GetPosition4());
        away.Normalize();
        AddOrder(HUMAN_ORDER_MOVE, Vector4(hitPos, 0.0f), Vector4(away, 0.0f));
        return;
    }
    if (hit->type & ORDER_VOLUME_TYPE) {
        // An order's marker: the order is deleted.
        auto *order = reinterpret_cast<S_HumanOrder *>(hit->frame->GetData());
        PlayerTeam()->SetCommanded(order->human);
        m_selector->SelectOrder(order);
        const int index = m_selector->GetSelectedIndex();
        if (index >= 0)
            order->human->RemoveOrder(index);
        ReadOrderFlags();
        return;
    }
    // An actor: the roll menu of the commanded soldier's actions on it.
    C_Actor *actor = hit->actor;
    if (!actor || (actor->GetType() == ACTOR_HUMAN && static_cast<C_Human *>(actor)->IsFadedOut()) || followsLast())
        return;
    ShowRollMenu(true, actor);
}

void C_CommandPanel::OnRightRelease()
{
    m_unknown5A7BC = false;
    m_rightDown = false;
    m_unknown5A7BD = false;
    m_cursor->SetMode(CURSOR_POINTER);
}

void C_CommandPanel::ShowRollMenu(bool open, C_Actor *target)
{
    if (!open) {
        m_rollMenu.Prepare();
        m_rollMenu.Open(false);
        DarkenButtons(false);
        return;
    }
    // Beside the cursor, kept on the map.
    const S_vector4 &cursor = m_cursor->GetPos();
    S_vector pos(cursor.x, cursor.y, cursor.z + 1.0f);
    if (pos.x >= 0.5f)
        pos.x -= 0.1f;
    else
        pos.x += 0.1f;
    if (pos.y <= -0.2f) {
        if (pos.y < -0.6f)
            pos.y = -0.6f;
    } else {
        pos.y = -0.2f;
    }
    m_rollMenu.MoveTo(pos);
    m_rollMenu.SetTarget(target, 1.0f);
    m_rollMenu.Open(true);
    m_cursor->SetMode(CURSOR_POINTER);
    if (m_rollMenu.IsOpen())
        DarkenButtons(true);
}

// ---------------------------------------------------------------------------------------------
// The input

void C_CommandPanel::HandleInput(float *held, float *pressed)
{
    if (!m_frame || !IsOpen())
        return;
    if (m_rollMenu.IsOpen()) {
        // (The command map's menu takes no keys of its own.)
        m_rollMenu.HandleInput(held, pressed);
        // The number keys choose the entries (0: the last) and act at once; a click acts on the
        // entry under the cursor.
        bool chosen = false;
        const int key = GetIGraph()->GetAsciiKey();
        const int digit = static_cast<int>(GetIGraph()->KeyToChar(key, 0) & 0xFF) - '0';
        if (digit >= 0 && digit <= 9) {
            chosen = m_rollMenu.Select(digit == 0 ? C_RollMenu::ENTRIES - 1 : digit - 1);
            g_controls.ClearKey(key, held, pressed);
        }
        if (chosen || (pressed[INPUT_CLICK] != 0.0f && m_rollMenu.IsCursorOver())) {
            const int index = m_rollMenu.GetSelectedIndex();
            if (index == -1)
                return;
            if (index < C_RollMenu::ENTRIES - 1) {
                const int action = m_rollMenu.GetEntryAction(index);
                C_Actor *target = m_rollMenu.GetTarget();
                // Quirk kept: a pick-up on a vehicle is done on the occupant of the seat numbered
                // as the entry.
                const int type = target->GetType();
                if (action == ROLL_PICK_UP &&
                    (type == ACTOR_CAR || type == ACTOR_SUBMARINE || type == ACTOR_STATIC_WEAPON)) {
                    if (C_Actor *occupant = target->GetSeatOccupant(index))
                        target = occupant;
                }
                if (m_commanded)
                    AddActorOrder(action, target, index);
            }
            ShowRollMenu(false, nullptr);
        }
        MoveCamera(held, pressed);
        if (!m_rollMenu.IsCursorOver() && (pressed[INPUT_CLICK] != 0.0f || pressed[INPUT_CLICK2] != 0.0f))
            ShowRollMenu(false, nullptr);
        return;
    }
    for (const S_ButtonKey &buttonKey : BUTTON_KEYS) {
        if (pressed[buttonKey.action] != 0.0f) {
            Press(buttonKey.id);
            break;
        }
    }
    if (pressed[INPUT_CLICK] != 0.0f)
        OnLeftPress();
    else if (held[INPUT_CLICK] != 0.0f)
        OnLeftDrag(held, pressed);
    else if (m_leftDown)
        OnLeftRelease();
    // (Held, the right button does nothing; the original tests the right Shift for it.)
    if (pressed[INPUT_CLICK2] != 0.0f)
        OnRightPress();
    else if (held[INPUT_CLICK2] == 0.0f && m_rightDown)
        OnRightRelease();
    if (pressed[INPUT_CLICK3] != 0.0f)
        m_middleDown = true;
    else if (held[INPUT_CLICK3] == 0.0f && m_middleDown)
        m_middleDown = false;
    if (!m_rollMenu.IsOpen())
        UpdateCursorMode(held, pressed);
    MoveCamera(held, pressed);
}

void C_CommandPanel::UpdateCursorMode(const float *held, const float *pressed)
{
    if (m_draggingMap)
        return;
    if (m_wheelTime != 0) {
        // The wheel's shape stays a while (turned again, longer).
        if (pressed[INPUT_WHEEL] > 0.0f || pressed[INPUT_WHEEL] < 0.0f)
            m_wheelTime = WHEEL_TIME;
        return;
    }
    if (held[MAP_KEY_PLUS] != 0.0f) {
        m_cursor->SetMode(CURSOR_PLUS);
        return;
    }
    if (held[MAP_KEY_MINUS] != 0.0f) {
        m_cursor->SetMode(CURSOR_MINUS);
        return;
    }
    // The arrows move the camera (with Ctrl they turn it).
    const bool turn = held[MAP_KEY_TURN] != 0.0f;
    if (held[MAP_KEY_FORWARD] != 0.0f) {
        m_cursor->SetMode(turn ? CURSOR_TURN_UP : CURSOR_MOVE);
        return;
    }
    if (held[MAP_KEY_BACK] != 0.0f) {
        m_cursor->SetMode(turn ? CURSOR_TURN_DOWN : CURSOR_MOVE);
        return;
    }
    if (held[MAP_KEY_RIGHT] != 0.0f) {
        m_cursor->SetMode(turn ? CURSOR_TURN_RIGHT : CURSOR_MOVE);
        return;
    }
    if (held[MAP_KEY_LEFT] != 0.0f) {
        m_cursor->SetMode(turn ? CURSOR_TURN_LEFT : CURSOR_MOVE);
        return;
    }
    if (held[MAP_KEY_UP] != 0.0f) {
        m_cursor->SetMode(CURSOR_UP);
        return;
    }
    if (held[MAP_KEY_DOWN] != 0.0f) {
        m_cursor->SetMode(CURSOR_DOWN);
        return;
    }
    if (pressed[INPUT_WHEEL] > 0.0f) {
        m_wheelTime = WHEEL_TIME;
        m_cursor->SetMode(CURSOR_UP);
        return;
    }
    if (pressed[INPUT_WHEEL] < 0.0f) {
        m_wheelTime = WHEEL_TIME;
        m_cursor->SetMode(CURSOR_DOWN);
        return;
    }
    if (held[INPUT_CLICK3] != 0.0f) {
        m_cursor->SetMode(CURSOR_ROTATE);
        return;
    }
    if (held[MAP_KEY_TURN_LEFT] != 0.0f) {
        m_cursor->SetMode(CURSOR_TURN_LEFT);
        return;
    }
    if (held[MAP_KEY_TURN_RIGHT] != 0.0f) {
        m_cursor->SetMode(CURSOR_TURN_RIGHT);
        return;
    }
    if (held[MAP_KEY_TURN_UP] != 0.0f) {
        m_cursor->SetMode(CURSOR_TURN_UP);
        return;
    }
    if (held[MAP_KEY_TURN_DOWN] != 0.0f) {
        m_cursor->SetMode(CURSOR_TURN_DOWN);
        return;
    }
    // At the screen's edges the camera moves.
    const S_vector4 &pos = m_cursor->GetPos();
    if (pos.x < 0.01f)
        m_cursor->SetMode(CURSOR_LEFT);
    else if (pos.x > 0.99f)
        m_cursor->SetMode(CURSOR_RIGHT);
    else if (!(pos.y <= -0.01f))
        m_cursor->SetMode(CURSOR_FORWARD);
    else if (pos.y < -0.74f)
        m_cursor->SetMode(CURSOR_BACKWARD);
    else
        m_cursor->SetMode(CURSOR_POINTER);
}

void C_CommandPanel::MoveCamera(float *held, float *pressed)
{
    C_CameraHumanEffects *camera = GetCameraModule()->GetHumanEffects();
    switch (m_cursor->GetMode()) {
    case CURSOR_POINTER:
        camera->SetTacticalMode(TACTICAL_IDLE, nullptr);
        camera->SetTacticalMotion(0.0f, 0.0f);
        break;
    case CURSOR_MOVE:
        if (m_draggingMap) {
            // The ground under the cursor is dragged along.
            m_cursor->ClampMotion(held, pressed);
            camera->SetTacticalMode(TACTICAL_DRAG, &m_cursorPoint);
            camera->SetTacticalMotion(held[INPUT_CURSOR_X], held[INPUT_CURSOR_Y]);
        } else {
            camera->SetTacticalMode(TACTICAL_MOVE, nullptr);
            camera->SetTacticalMotion(held[MAP_KEY_LEFT] - held[MAP_KEY_RIGHT],
                                      held[MAP_KEY_FORWARD] - held[MAP_KEY_BACK]);
        }
        break;
    case CURSOR_ROTATE:
        camera->SetTacticalMode(TACTICAL_TURN, nullptr);
        // Quirk kept: the inverted mouse is applied to the frame's values themselves.
        if (g_gameProfile.GetDetailSettings()->invertMouse)
            held[INPUT_CURSOR_Y] = -held[INPUT_CURSOR_Y];
        camera->SetTacticalMotion(held[INPUT_CURSOR_X] * 40.0f, held[INPUT_CURSOR_Y] * 40.0f);
        break;
    case CURSOR_TURN_LEFT:
        camera->SetTacticalMode(TACTICAL_TURN, nullptr);
        camera->SetTacticalMotion(-1.0f, 0.0f);
        break;
    case CURSOR_TURN_RIGHT:
        camera->SetTacticalMode(TACTICAL_TURN, nullptr);
        camera->SetTacticalMotion(1.0f, 0.0f);
        break;
    case CURSOR_TURN_UP:
        camera->SetTacticalMode(TACTICAL_TURN, nullptr);
        camera->SetTacticalMotion(0.0f, -1.0f);
        break;
    case CURSOR_TURN_DOWN:
        camera->SetTacticalMode(TACTICAL_TURN, nullptr);
        camera->SetTacticalMotion(0.0f, 1.0f);
        break;
    case CURSOR_LEFT:
        camera->SetTacticalMode(TACTICAL_MOVE, nullptr);
        camera->SetTacticalMotion(1.0f, 0.0f);
        break;
    case CURSOR_RIGHT:
        camera->SetTacticalMode(TACTICAL_MOVE, nullptr);
        camera->SetTacticalMotion(-1.0f, 0.0f);
        break;
    case CURSOR_FORWARD:
        camera->SetTacticalMode(TACTICAL_MOVE, nullptr);
        camera->SetTacticalMotion(0.0f, 1.0f);
        break;
    case CURSOR_BACKWARD:
        camera->SetTacticalMode(TACTICAL_MOVE, nullptr);
        camera->SetTacticalMotion(0.0f, -1.0f);
        break;
    case CURSOR_UP:
        camera->SetTacticalMode(TACTICAL_UP, nullptr);
        camera->SetTacticalMotion(0.0f, 1.0f);
        break;
    case CURSOR_DOWN:
        camera->SetTacticalMode(TACTICAL_DOWN, nullptr);
        camera->SetTacticalMotion(0.0f, -1.0f);
        break;
    case CURSOR_MOVE_KEYS:
        // (The first four keys of the map; the map never sets this shape.)
        camera->SetTacticalMode(TACTICAL_MOVE, nullptr);
        camera->SetTacticalMotion(held[2] - held[3], held[0] - held[1]);
        break;
    case CURSOR_PLUS:
        camera->SetTacticalMode(TACTICAL_PLUS, nullptr);
        camera->SetTacticalMotion(0.0f, 1.0f);
        break;
    case CURSOR_MINUS:
        camera->SetTacticalMode(TACTICAL_MINUS, nullptr);
        camera->SetTacticalMotion(0.0f, -1.0f);
        break;
    default:
        break;
    }
}
