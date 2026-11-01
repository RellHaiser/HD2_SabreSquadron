#include "game/loader_screen.h"

#include "game/campaign.h"
#include "game/geffects.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

C_LoaderScreen g_loaderScreen;                          // 0x008aea80 (static init 0x006c97b0)

namespace {

// A stage's part of the bar.
struct S_LoaderStage {                                  // 0x0C bytes
    float start, length;
    const char *name;
};
// The mission's loading alone (0x00842fe8) / followed by C_game::Start (0x00843030).
const S_LoaderStage LOADER_STAGES[LOADER_STAGE_COUNT] = {
    {0.0f, 0.3f, "scene"},  {0.3f, 0.5f, "scene2.bin"},  {0.8f, 0.05f, "actors.bin"},
    {0.85f, 0.05f, "sounds.bin"}, {0.9f, 0.1f, "stuff.bin"}, {1.0f, 0.0f, "mission init"},
};
const S_LoaderStage LOADER_STAGES_TWO_PARTS[LOADER_STAGE_COUNT] = {
    {0.0f, 0.15f, "scene"},  {0.15f, 0.25f, "scene2.bin"},  {0.4f, 0.05f, "actors.bin"},
    {0.45f, 0.05f, "sounds.bin"}, {0.5f, 0.05f, "stuff.bin"}, {0.55f, 0.45f, "mission init"},
};
// The bar is drawn again once it moved on by more than this.
constexpr float PROGRESS_STEP = 0.003f;

// The loader scene's text boxes: the visual, the style and the label put first ("<label><suffix>",
// -1: none).
enum : int {
    LOADER_TEXT_BRIEFING = 0,
    LOADER_TEXT_BAR = 1,                                // ("press space" at the close)
    LOADER_TEXT_TITLE = 2,
    LOADER_TEXT_PLAYERS1 = 3,
    LOADER_TEXT_PLAYERS2 = 4,
    LOADER_TEXT_OBJECTIVES_ALLIED = 5,
    LOADER_TEXT_OBJECTIVES_AXIS = 6,
    LOADER_TEXT_MP_TITLE = 7,
    LOADER_TEXT_GAME_TYPE = 8,
    LOADER_TEXT_TEAM1 = 9,
    LOADER_TEXT_TEAM2 = 10,
    // (11 .. 13: the map survey, the progress, the designated: only their labels.)
    LOADER_TEXT_VERSION = 14,
};
struct S_LoaderText {                                   // 0x10 bytes
    const char *visual;
    int style;
    int label;
    const char *suffix;
};
const S_LoaderText LOADER_TEXTS[C_LoaderScreen::TEXT_COUNT] = {   // 0x00843078
    {"briefing", 84, -1, nullptr},
    {"textbar", 80, -1, nullptr},
    {"title", 80, -1, nullptr},
    {"MP_list1", 83, 2951, " :"},
    {"MP_list2", 83, 2951, " :"},
    {"MP_objective1", 82, 2950, " :"},
    {"MP_objective2", 82, 2950, " :"},
    {"MP_title", 80, -1, nullptr},
    {"MP_text_screen", 79, -1, nullptr},
    {"MP_team allied1", 80, 2621, nullptr},
    {"MP_team allied2", 80, 2620, nullptr},
    {"MP_map survey", 82, 2952, " :"},
    {"MP_progress", 82, 2953, " :"},
    {"MP_designated", 81, 2954, " :"},
    {"text_h&dversion", 40, -1, nullptr},
};
// The objectives' marks and the line style before them.
constexpr uint8_t OBJECTIVE_MARK_STYLE = 0x55;
// Texts: "press space" (the close's wait), the teams of a deathmatch, the version.
constexpr int TEXT_PRESS_SPACE = 0x7F8;
constexpr int TEXT_TEAM1_DEATHMATCH = 0x5028;
constexpr int TEXT_TEAM2_DEATHMATCH = 0x5029;
constexpr int TEXT_VERSION = 0x7D6;
// The key the close waits for (DIK_SPACE).
constexpr int KEY_SPACE = 0x39;

// The driver's states the scene is loaded with (as the interface's model).
constexpr int DRIVER_STATE_11 = 0x11;
constexpr int DRIVER_STATE_15 = 0x15;
constexpr int DRIVER_STATE_19 = 0x19;
// The camera (as the interface's: orthogonal at (0.5, -0.375) looking along z).
const S_vector CAMERA_POS(0.5f, -0.375f, -15.5f);
constexpr float CAMERA_NEAR = 0.2f;
constexpr float CAMERA_FAR = 500.0f;
constexpr float CAMERA_ORTHO_SCALE = 2.0f;

} // namespace

C_LoaderScreen::~C_LoaderScreen()
{
    Close();
}

void C_LoaderScreen::Start(const char *name, bool show, bool waitForKey, bool twoParts, const C_MissionDef *mission)
{
    Close();
    m_waitForKey = waitForKey;
    m_progress = 0.0f;
    m_twoParts = twoParts;
    if (show) {
        const C_fstr path("missions\\%s\\loader", name);
        I3D_driver *driver = I3DGetDriver();
        m_scene = reinterpret_cast<I3D_scene *>(driver->CreateFrame(FRAME_SCENE));
        if (m_scene) {
            IGraph *graph = GetIGraph();
            const int *viewport = m_scene->GetViewportRect();
            if (viewport[0] != 0 || viewport[1] != 0 || viewport[2] != graph->ScrnX() ||
                viewport[3] != GetIGraph()->ScrnY()) {
                const int bottom = GetIGraph()->ScrnY();
                const int right = GetIGraph()->ScrnX();
                m_scene->SetViewport(0, 0, right, bottom);
            }
        }
        const bool state15 = driver->GetState(DRIVER_STATE_15) != 0;
        const bool state19 = I3DGetDriver()->GetState(DRIVER_STATE_19) != 0;
        const bool state11 = I3DGetDriver()->GetState(DRIVER_STATE_11) != 0;
        I3DGetDriver()->SetState(DRIVER_STATE_15, 0);
        I3DGetDriver()->SetState(DRIVER_STATE_19, 1);
        I3DGetDriver()->SetState(DRIVER_STATE_11, 1);
        // (Quirk kept: no check of the scene.)
        const HRESULT result = m_scene->Open(path, 0, nullptr, nullptr);
        I3DGetDriver()->SetState(DRIVER_STATE_15, state15);
        I3DGetDriver()->SetState(DRIVER_STATE_19, state19);
        I3DGetDriver()->SetState(DRIVER_STATE_11, state11);
        if (result < 0) {
            m_scene->Release();
            m_scene = nullptr;
            return;
        }

        auto *camera = static_cast<I3D_camera *>(I3DGetDriver()->CreateFrame(FRAME_CAMERA));
        m_camera = camera;
        m_scene->SetActiveCamera(camera);
        camera->SetWorldDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
        camera->SetPosInline(CAMERA_POS);
        camera->SetRange(CAMERA_NEAR, CAMERA_FAR);
        camera->SetOrthogonal(true);
        camera->SetOrthoScale(CAMERA_ORTHO_SCALE);
        camera->Update();

        auto *light = static_cast<I3D_light *>(I3DGetDriver()->CreateFrame(FRAME_LIGHT));
        m_light = light;
        light->SetName("C_LoaderScreen");
        light->SetLightType(4);
        light->SetAmbientColor(S_vector(1.0f, 1.0f, 1.0f));
        light->SetAmbientPower(1.0f);
        light->SetMode(0x11);
        m_scene->GetPrimarySector()->AddLight(light);

        // The text boxes, the last first.
        for (int i = TEXT_COUNT; i-- > 0;) {
            const S_LoaderText &entry = LOADER_TEXTS[i];
            I3D_frame *visual = m_scene->FindFrame(entry.visual, 0xFFFF);
            if (!visual)
                continue;
            visual->SetOn(false);
            C_TextBox *box = new C_TextBox;
            m_texts[i] = box;
            box->SetUnknown90(5);
            box->AttachTo(m_scene, visual);
            box->Clear();
            const uint8_t style = static_cast<uint8_t>(entry.style);
            if (entry.label != -1) {
                const char *suffix = entry.suffix ? entry.suffix : "";
                const C_str label = g_texts.Get(entry.label);
                box->AddText(C_fstr("%s%s", static_cast<const char *>(label), suffix), style);
            }
            const auto *mp = static_cast<const C_MpMissionDef *>(mission);
            switch (i) {
            case LOADER_TEXT_BRIEFING:
                if (mission) {
                    const int text = g_gameProfile.IsCooperativeGame() ? mp->GetBriefingText() : mission->GetText(0);
                    if (text != -1)
                        box->AddTextId(text, style);
                }
                break;
            case LOADER_TEXT_TITLE:
                if (g_gameProfile.IsCooperativeGame()) {
                    if (mission)
                        box->AddText(mp->GetMapName(), style);
                } else if (mission && mission->GetText(1) != -1) {
                    box->AddTextId(mission->GetText(1), style);
                }
                break;
            case LOADER_TEXT_PLAYERS1:
            case LOADER_TEXT_PLAYERS2: {
                box->AddText(C_fstr(" "), style);
                const std::vector<C_str> &players = m_players[i - LOADER_TEXT_PLAYERS1];
                for (const C_str &player : players)
                    m_texts[i]->AddText(player, style);
                break;
            }
            case LOADER_TEXT_OBJECTIVES_ALLIED:
            case LOADER_TEXT_OBJECTIVES_AXIS: {
                // A network map's objectives, each with its side's text after a mark (none: the
                // box dimmed).
                if (!mission) {
                    box->SetDimmed(true);
                    break;
                }
                const bool allied = i == LOADER_TEXT_OBJECTIVES_ALLIED;
                box->AddText(C_fstr(" "), style);
                const bool any = mission->GetObjectiveCount() != 0;
                for (int k = 0; k < mission->GetObjectiveCount(); ++k) {
                    m_texts[i]->AddText(C_fstr(allied ? "=" : "!"), OBJECTIVE_MARK_STYLE);
                    const S_MpObjective *objective = mp->GetMpObjective(k);
                    const C_str text = g_texts.Get(allied ? objective->alliedText : objective->axisText);
                    m_texts[i]->AddText(C_fstr(" %s", static_cast<const char *>(text)), style);
                }
                if (!any)
                    m_texts[i]->SetDimmed(true);
                break;
            }
            case LOADER_TEXT_MP_TITLE: {
                // Fix: the name as text (the original used it as the format).
                C_str title = C_fstr("%s", name);
                if (mission)
                    title = mp->GetMapName();
                box->AddText(title, style);
                break;
            }
            case LOADER_TEXT_GAME_TYPE:
                box->AddTextId(GetGameTypeTextId(g_mpSettings->gameType), style);
                break;
            case LOADER_TEXT_TEAM1:
                if (g_mpSettings->gameType == 0)
                    box->SetTextId(TEXT_TEAM1_DEATHMATCH, style, 0);
                break;
            case LOADER_TEXT_TEAM2:
                if (g_mpSettings->gameType == 0)
                    box->SetTextId(TEXT_TEAM2_DEATHMATCH, style, 0);
                break;
            case LOADER_TEXT_VERSION:
                box->SetTextId(TEXT_VERSION, style, 0);
                break;
            default:
                break;
            }
        }
        m_loadBar = m_scene->FindFrame("loadbar", 0xFFFF);
        Draw();
        Draw();
    }
    ClearPlayers();
    m_shown = true;
}

void C_LoaderScreen::SetProgress(int stage, float fraction)
{
    if (!IsShown())
        return;
    const S_LoaderStage &part = (m_twoParts ? LOADER_STAGES_TWO_PARTS : LOADER_STAGES)[stage];
    const float progress = part.length * fraction + part.start;
    if (m_progress + PROGRESS_STEP < progress) {
        m_progress = progress;
        Draw();
    }
}

void C_LoaderScreen::Finish()
{
    if (m_twoParts)
        Close();
}

void C_LoaderScreen::FinishLoading()
{
    if (!m_twoParts)
        Close();
}

void C_LoaderScreen::AddPlayer(const char *text, int team)
{
    // Fix: the name as text (the original used it as the format; null: "-unnamed-", not added).
    const C_fstr line("%s", text ? text : "-unnamed-");
    if (text && *text)
        m_players[team == 1 ? 0 : 1].push_back(line);
}

void C_LoaderScreen::Close()
{
    m_progress = 1.0f;
    if (!IsShown())
        return;
    Draw();
    if (m_waitForKey && m_scene) {
        // Single player: "press space" until it is pressed.
        GetGEffects()->Update(100);
        const uint8_t barStyle = static_cast<uint8_t>(LOADER_TEXTS[LOADER_TEXT_BAR].style);
        if (m_texts[LOADER_TEXT_BAR])
            m_texts[LOADER_TEXT_BAR]->SetTextId(TEXT_PRESS_SPACE, barStyle, 0);
        do {
            GetIGraph()->ProcessWinMessages();
            GetIGraph()->ReadKeyboard();
            Draw();
        } while (GetIGraph()->GetAsciiKey() != KEY_SPACE);
        if (m_texts[LOADER_TEXT_BAR])
            m_texts[LOADER_TEXT_BAR]->SetText(C_fstr(""), barStyle, 0);
        for (int i = 0; i < 3; ++i) {
            GetIGraph()->Clear(0, 1.0f, 3);
            GetIGraph()->Flip();
        }
    }
    for (int i = TEXT_COUNT; i-- > 0;) {
        if (m_texts[i]) {
            m_texts[i]->Clear();
            delete m_texts[i];
            m_texts[i] = nullptr;
        }
    }
    // (Quirk kept: the load bar, found in the scene, is released too.)
    if (m_light) {
        m_light->Release();
        m_light = nullptr;
    }
    if (m_loadBar) {
        m_loadBar->Release();
        m_loadBar = nullptr;
    }
    if (m_scene) {
        m_scene->Release();
        m_scene = nullptr;
    }
    if (m_camera) {
        m_camera->Release();
        m_camera = nullptr;
    }
    ClearPlayers();
    m_progress = 0.0f;
    m_shown = false;
}

void C_LoaderScreen::Draw()
{
    GetIGraph()->ProcessWinMessages();
    const S_MultiplayerSettings *settings = g_mpSettings;
    if (settings && settings->dedicated) {
        g_netGame.PrintLoadProgress(static_cast<int>(m_progress * 100.0f));
        return;
    }
    if (!m_scene)
        return;
    if (m_loadBar)
        m_loadBar->SetScale(S_vector(m_progress, 1.0f, 1.0f));
    m_scene->Render(0);
    IGraph *graph = GetIGraph();
    const int state = graph->GetRenderState(0);
    GetIGraph()->SetRenderState(0, 1);
    GetIGraph()->BeginScene();
    for (int i = TEXT_COUNT; i-- > 0;) {
        if (m_texts[i])
            m_texts[i]->Render();
    }
    GetIGraph()->EndScene();
    GetIGraph()->SetRenderState(0, state);
    GetIGraph()->Flip();
    GetIGraph()->Clear(0, 1.0f, 3);
}

void C_LoaderScreen::ClearPlayers()
{
    m_players[0].clear();
    m_players[1].clear();
}
