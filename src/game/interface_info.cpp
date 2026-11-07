// The mission information and the scores (see interface.h; the original's 0x00496360..0x0049a200).
#include "game/interface.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/campaign.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/key_bindings.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The texts.
enum : int {
    TEXT_SERVER = 0x930,
    TEXT_BLANK = 0x7D2,
    TEXT_TITLE_SINGLE_PLAYER = 0xA29,
    TEXT_TITLE_DEATHMATCH = 0xA2A,
    TEXT_TITLE_OCCUPATION = 0xA2B,
    TEXT_TITLE_OBJECTIVES = 0xA2C,
    TEXT_TITLE_COOP = 0x4E22,
    TEXT_HINTS = 0xA2D,
    // The columns.
    TEXT_COLUMN_NAME = 0xA33,
    TEXT_COLUMN_STAT4 = 0xA34,
    TEXT_COLUMN_KILLS = 0xA35,
    TEXT_COLUMN_DEATHS = 0xA36,
    TEXT_COLUMN_SCORE = 0xA38,
    TEXT_ALLIES = 0xA3C,
    TEXT_AXIS = 0xA3D,
    TEXT_TEAM_SCORE = 0xA3E,
    TEXT_UNDER_HEADINGS = 0xA3F,                        // twice, under the teams' headings
    TEXT_ROUNDS_WON = 0xA41,
    // An objective's state.
    TEXT_OBJECTIVE_COMPLETED = 0xB63,
    TEXT_OBJECTIVE_FAILED = 0xB64,
    TEXT_OBJECTIVE_OPEN = 0xB65,
    TEXT_OBJECTIVE_FAILED_CRITICAL = 0xB66,
    TEXT_TIME_LEFT = 0x1A58,
    TEXT_POINT_LIMIT = 0x1A59,
    TEXT_ROUND_TIME = 0x3D22,
    // The ranks' short names (+ rank 0..7) by the uniform's side.
    TEXT_RANKS_ALLIED = 0x2792,
    TEXT_RANKS_AXIS = 0x279C,
};

// S_NetPlayerState::stats.
enum : int {
    PLAYER_KILLS = 1,
    PLAYER_DEATHS = 2,
    PLAYER_SCORE = 3,                                   // the round's
    PLAYER_STAT4 = 4,                                   // (the column TEXT_COLUMN_STAT4; not identified)
    PLAYER_RANK = 5,
};
constexpr int MAX_RANK = 7;

// The rank insignia (font 4) of the left and right columns.
const char RANK_GLYPHS[2][9] = {"oklmpqrn", "OKLMPQRN"};

// The text styles (CreateTextStyle; the table at 0x008399f8). Indices, by where they are used:
//  0 titles; 1 headings; 2 an objective's state; 54 hints; 55 the server; 64 blank lines; 65 the
//  round's time; 66 the map; 71 / 72 the point limit (without / with a time limit).
//  Deathmatch and co-op: column headings 3 name, 4 score, 5 kills, 6 deaths, 7 stat 4; rows (+41
//  dimmed) 8 name, 9 score, 10 kills, 11 deaths, 12 stat 4; the local player's name 60 / 61;
//  co-op names 67 / 68, the local player's 69 / 70.
//  Teams: 23 / 29 the allies' / axis' heading, 45 / 46 their score; 47 / 48 the lines under them;
//  column headings 13..17 left, 18..22 right (name, score, kills, deaths, stat 4); rows below.
const S_TextStyleDesc STYLE_TABLE[C_InfoScreen::STYLE_COUNT] = {
    {2, 0xFF000000, 0.04f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 1, 0x8},   // 0
    {3, 0xFF000000, 0.02f, 0.6f, 0.05f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 1, 0x18},   // 1
    {3, 0xFF000000, 0.02f, 0.0f, 0.75f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 2
    {3, 0xFF000000, 0.025f, 0.18f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 3
    {3, 0xFF000000, 0.025f, 0.06f, 0.54f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 4
    {3, 0xFF000000, 0.025f, 0.06f, 0.66f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 5
    {3, 0xFF000000, 0.025f, 0.06f, 0.78f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 6
    {3, 0xFF000000, 0.025f, 0.06f, 0.9f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 7
    {3, 0xFF000000, 0.02f, 0.18f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 8
    {3, 0xFF000000, 0.02f, 0.04f, 0.54f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 9
    {3, 0xFF000000, 0.02f, 0.04f, 0.66f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 10
    {3, 0xFF000000, 0.02f, 0.05f, 0.78f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 11
    {3, 0xFF000000, 0.02f, 0.04f, 0.9f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 12
    {3, 0xFF000000, 0.022f, 0.16f, 0.05f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 13
    {3, 0xFF000000, 0.022f, 0.06f, 0.27f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 14
    {3, 0xFF000000, 0.022f, 0.06f, 0.34f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 15
    {3, 0xFF000000, 0.022f, 0.06f, 0.41f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 16
    {3, 0xFF000000, 0.022f, 0.06f, 0.47f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 17
    {3, 0xFF000000, 0.022f, 0.16f, 0.55f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 18
    {3, 0xFF000000, 0.022f, 0.06f, 0.77f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 19
    {3, 0xFF000000, 0.022f, 0.06f, 0.84f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 20
    {3, 0xFF000000, 0.022f, 0.06f, 0.91f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 21
    {3, 0xFF000000, 0.022f, 0.06f, 0.97f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 22
    {3, 0xFF000000, 0.03f, 0.3f, 0.05f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 23
    {3, 0xFF000000, 0.02f, 0.15f, 0.07f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 24
    {3, 0xFF000000, 0.02f, 0.04f, 0.27f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 25
    {3, 0xFF000000, 0.02f, 0.04f, 0.34f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 26
    {3, 0xFF000000, 0.02f, 0.04f, 0.41f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 27
    {3, 0xFF000000, 0.02f, 0.04f, 0.47f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 28
    {3, 0xFF000000, 0.03f, 0.3f, 0.55f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 29
    {3, 0xFF000000, 0.02f, 0.15f, 0.57f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 30
    {3, 0xFF000000, 0.02f, 0.04f, 0.77f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 31
    {3, 0xFF000000, 0.02f, 0.04f, 0.84f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 32
    {3, 0xFF000000, 0.02f, 0.04f, 0.91f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 33
    {3, 0xFF000000, 0.02f, 0.04f, 0.97f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 34
    {3, 0x88000000, 0.02f, 0.15f, 0.07f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 35
    {3, 0x88000000, 0.02f, 0.04f, 0.27f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 36
    {3, 0x88000000, 0.02f, 0.04f, 0.34f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 37
    {3, 0x88000000, 0.02f, 0.04f, 0.41f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 38
    {3, 0x88000000, 0.02f, 0.04f, 0.47f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 39
    {3, 0x88000000, 0.02f, 0.15f, 0.57f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 40
    {3, 0x88000000, 0.02f, 0.04f, 0.77f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 41
    {3, 0x88000000, 0.02f, 0.04f, 0.84f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 42
    {3, 0x88000000, 0.02f, 0.04f, 0.91f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 43
    {3, 0x88000000, 0.02f, 0.04f, 0.97f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 44
    {3, 0xFF000000, 0.03f, 0.3f, 0.485f, -1.0f, 0.0f, 0.0f, 0, 3, 1, 1, 0, 0x10},   // 45
    {3, 0xFF000000, 0.03f, 0.3f, 0.985f, -1.0f, 0.0f, 0.0f, 0, 3, 1, 1, 0, 0x10},   // 46
    {3, 0xFF000000, 0.02f, 0.38f, 0.05f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 47
    {3, 0xFF000000, 0.02f, 0.38f, 0.55f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 48
    {3, 0x88000000, 0.02f, 0.18f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 49
    {3, 0x88000000, 0.02f, 0.04f, 0.54f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 50
    {3, 0x88000000, 0.02f, 0.04f, 0.66f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 51
    {3, 0x88000000, 0.02f, 0.05f, 0.78f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 52
    {3, 0x88000000, 0.02f, 0.04f, 0.9f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 0, 0x10},   // 53
    {3, 0xFF000000, 0.02f, 0.8f, 0.05f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 0, 1, 0x18},   // 54
    {3, 0xFF000000, 0.022f, 0.31f, 0.07f, 0.98f, 0.0f, 0.0f, 0, 1, 1, 0, 0, 0x0},   // 55
    {3, 0xFF000088, 0.02f, 0.15f, 0.07f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 56
    {3, 0xFF880000, 0.02f, 0.15f, 0.57f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 57
    {3, 0x88000088, 0.02f, 0.15f, 0.07f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 58
    {3, 0x88880000, 0.02f, 0.15f, 0.57f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x10},   // 59
    {3, 0xFF000088, 0.02f, 0.18f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 60
    {3, 0x88000088, 0.02f, 0.18f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 61
    {4, 0xFFFFFFFF, 0.02f, 0.04f, 0.05f, -1.0f, 0.001f, 0.001f, 0, 1, 1, 1, 0, 0x98},   // 62
    {4, 0xFFFFFFFF, 0.02f, 0.04f, 0.55f, -1.0f, 0.001f, 0.001f, 0, 1, 1, 1, 0, 0x90},   // 63
    {3, 0xFF000000, 0.02f, 0.16f, 0.05f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 64
    {3, 0xFF000000, 0.022f, 0.8f, 0.93f, 0.98f, 0.0f, 0.0f, 0, 3, 1, 0, 1, 0x0},   // 65
    {2, 0xFF000000, 0.04f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0, 2, 1, 1, 1, 0x8},   // 66
    {3, 0xFF000000, 0.02f, 0.3f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 67
    {3, 0x88000000, 0.02f, 0.3f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 68
    {3, 0xFF000088, 0.02f, 0.3f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 69
    {3, 0x88000088, 0.02f, 0.3f, 0.1f, -1.0f, 0.0f, 0.0f, 0, 1, 1, 1, 0, 0x18},   // 70
    {3, 0xFF000000, 0.022f, 0.8f, 0.93f, 0.98f, 0.0f, 0.0f, 0, 3, 1, 0, 1, 0x0},   // 71
    {3, 0xFF000000, 0.022f, 0.8f, 0.93f, 0.95f, 0.0f, 0.0f, 0, 3, 1, 0, 1, 0x0},   // 72
};

// A row's styles in the team columns (the second of each pair: dimmed).
struct S_ColumnStyles {
    int insignia;
    int name[2];
    int localName[2];
    int stat4[2];
    int kills[2];
    int deaths[2];
    int score[2];
};
const S_ColumnStyles COLUMN_STYLES[2] = {
    {62, {24, 35}, {56, 58}, {28, 39}, {26, 37}, {27, 38}, {25, 36}},   // left: the allies
    {63, {30, 40}, {57, 59}, {34, 44}, {32, 42}, {33, 43}, {31, 41}},   // right: the axis
};

// The chunk of the hints in a saved game (C_Interface::SaveGame).
constexpr uint16_t CHUNK_INFO_HINTS = 0x1F5;

// The screen redraws every 500 ms while shown.
constexpr int REFRESH_TIME = 500;

// The driver's states the model is loaded without (the setup's texture options).
constexpr int DRIVER_STATE_15 = 0x15;
constexpr int DRIVER_STATE_19 = 0x19;

int ClampRank(int rank)
{
    return rank < 8 ? (rank < 0 ? 0 : rank) : MAX_RANK;
}

// The players dim while spectating or dead.
bool IsInactive(const S_NetPlayerState *player)
{
    bool inactive = player->IsSpectating();
    if (player->human)
        inactive |= player->human->IsDead();
    return inactive;
}

// The connected players (in slot order), sorted.
std::vector<S_NetPlayerState *> ConnectedPlayers(S_NetPlayerState *players, int count)
{
    std::vector<S_NetPlayerState *> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (players[i].IsConnected())
            list.push_back(&players[i]);
    }
    return list;
}

// The original sorts with its std::sort (an insertion sort up to 16 players), which keeps the
// order of equal ones there; stable_sort does too (and beyond).
void SortByScore(std::vector<S_NetPlayerState *> &list)   // ComparePlayersByScore 0x00496410
{
    std::stable_sort(list.begin(), list.end(), [](const S_NetPlayerState *a, const S_NetPlayerState *b) {
        return b->stats[PLAYER_SCORE] < a->stats[PLAYER_SCORE];
    });
}

void SortByName(std::vector<S_NetPlayerState *> &list)    // ComparePlayersByName 0x00496430
{
    std::stable_sort(list.begin(), list.end(), [](const S_NetPlayerState *a, const S_NetPlayerState *b) {
        return _stricmp(a->name, b->name) < 0;
    });
}

} // namespace

bool C_InfoScreen::Init(I3D_scene *scene)
{
    m_hints.clear();
    m_refreshTime = 0;
    // Quirk kept: the states come back as 0 / 1.
    I3D_driver *driver = I3DGetDriver();
    const bool state15 = driver->GetState(DRIVER_STATE_15) != 0;
    const bool state19 = driver->GetState(DRIVER_STATE_19) != 0;
    driver->SetState(DRIVER_STATE_15, 0);
    driver->SetState(DRIVER_STATE_19, 0);
    auto *model = static_cast<I3D_model *>(driver->CreateFrame(FRAME_MODEL));
    const bool loaded = g_modelCache.OpenInto(model, "ingame_info") == I3D_OK;
    if (loaded) {
        model->SetName("ingame_info");
        model->LinkTo(reinterpret_cast<I3D_frame *>(scene->GetPrimarySector()), 0);
        scene->AddFrame(model);
        m_frame = model->FindChildFrame("game_info", FRAME_MASK_VISUAL);
        // Fix: the original went on without the model or its frame (and crashed).
        if (m_frame)
            m_frame->SetPos(S_vector(0.5f, -0.375f, -0.5f));
    }
    driver->SetState(DRIVER_STATE_15, state15);
    driver->SetState(DRIVER_STATE_19, state19);
    if (!loaded || !m_frame) {
        m_frame = nullptr;
        model->Release();
        return false;
    }

    if (IsLocalGame()) {
        m_kind = 0;
    } else {
        switch (g_mpSettings->gameType) {
        case MP_GAME_DEATHMATCH: m_kind = 1; break;
        case MP_GAME_OCCUPATION: m_kind = 2; break;
        case MP_GAME_OBJECTIVES: m_kind = 3; break;
        case MP_GAME_COOPERATIVE: m_kind = 4; break;
        default: break;
        }
    }
    // Deathmatch narrower, co-op also lower.
    if (m_kind == 1)
        m_frame->SetScale(S_vector(0.7f, 1.0f, 1.0f));
    else if (m_kind == 4)
        m_frame->SetScale(S_vector(0.7f, 0.7f, 1.0f));
    else
        m_frame->SetScale(S_vector(1.0f, 1.0f, 1.0f));
    m_frame->Update();
    if (I3D_frame *header = model->FindChildFrame("header", FRAME_MASK_VISUAL))
        header->SetOn(false);
    if (I3D_frame *text = model->FindChildFrame("text", FRAME_MASK_VISUAL))
        text->SetOn(false);

    m_text = new C_TextBox();
    m_text->AttachTo(scene, m_frame);
    for (int i = 0; i < STYLE_COUNT; ++i)
        m_styles[i] = CreateTextStyle(STYLE_TABLE[i]);
    Show(false);
    model->Release();
    return true;
}

void C_InfoScreen::Done()
{
    for (int &style : m_styles) {
        ReleaseTextStyle(style);
        style = -1;
    }
    delete m_text;
    m_text = nullptr;
}

void C_InfoScreen::Render()
{
    IGraph *graph = GetIGraph();
    const uint32_t old = graph->GetRenderState(4);
    GetIGraph()->SetRenderState(4, 4);
    if (m_text)
        m_text->Render();
    GetIGraph()->SetRenderState(4, old);
}

void C_InfoScreen::Show(bool show)
{
    if (show) {
        Fill();
        m_refreshTime = 0;
    }
    m_frame->SetOn(show);
}

bool C_InfoScreen::IsShown() const
{
    return m_frame->IsOn();
}

void C_InfoScreen::Update(int timeDelta)
{
    m_refreshTime += timeDelta;
    if (m_refreshTime > REFRESH_TIME && m_frame->IsOn()) {
        Fill();
        m_refreshTime = 0;
    }
}

void C_InfoScreen::Fill()
{
    m_text->Clear();
    switch (m_kind) {
    case 0: FillSinglePlayer(); break;
    case 1: FillDeathmatch(); break;
    case 2: FillOccupation(); break;
    case 3: FillObjectiveGame(); break;
    case 4: FillCoop(); break;
    default: break;
    }
}

void C_InfoScreen::FillSinglePlayer()
{
    AddBlankLine();
    AddTitle(TEXT_TITLE_SINGLE_PLAYER);
    AddBlankLine();
    AddObjectives();
    AddHints();
}

void C_InfoScreen::FillDeathmatch()
{
    AddBlankLine();
    AddTitle(TEXT_TITLE_DEATHMATCH);
    AddMissionName();
    AddBlankLine();
    AddDeathmatchPlayers();
    AddServer();
    AddLimits();
}

void C_InfoScreen::FillOccupation()
{
    AddBlankLine();
    AddTitle(TEXT_TITLE_OCCUPATION);
    AddMissionName();
    AddBlankLine();
    AddTeamPlayers();
    AddServer();
}

void C_InfoScreen::FillObjectiveGame()
{
    AddBlankLine();
    AddTitle(TEXT_TITLE_OBJECTIVES);
    AddMissionName();
    AddBlankLine();
    AddObjectives();
    AddBlankLine();
    AddBlankLine();
    AddTeamPlayers();
    AddServer();
}

void C_InfoScreen::FillCoop()
{
    AddBlankLine();
    AddTitle(TEXT_TITLE_COOP);
    AddMissionName();
    AddBlankLine();
    AddObjectives();
    AddBlankLine();
    AddBlankLine();
    AddCoopPlayers();
    AddServer();
    AddRoundTime();
}

void C_InfoScreen::AddBlankLine()
{
    m_text->AddTextId(TEXT_BLANK, Style(64));
}

void C_InfoScreen::AddTitle(int textId)
{
    m_text->AddTextId(textId, Style(0));
}

void C_InfoScreen::AddNumber(int value, int style)
{
    m_text->AddText(C_fstr("%d", value), Style(style));
}

// The scripts' hints, their key tags ("<...>") replaced by the keys bound.
void C_InfoScreen::AddHints()
{
    if (m_hints.empty())
        return;
    AddBlankLine();
    AddBlankLine();
    m_text->AddTextId(TEXT_HINTS, Style(1));
    AddBlankLine();
    const C_KeyBindings *keys = g_gameProfile.GetPlayerProfile()->GetKeyBindings();
    for (const int hint : m_hints) {
        const C_str source = g_texts.Get(hint);
        const char *text = source.c_str();
        C_str replaced;
        C_str line;
        bool done = false;
        for (;;) {
            const char *open = std::strchr(text, '<');
            const char *close = std::strchr(text, '>');
            if (!open || !close || close <= open + 1)
                break;
            C_str next;
            if (!keys->ReplaceTags(&next, text)) {
                // (Then `next` is the text as it was.)
                line = next;
                done = true;
                break;
            }
            replaced = next;
            text = replaced.c_str();
        }
        if (!done)
            line = C_str(text);
        m_text->AddText(line, Style(54));
    }
}

// The objectives that are part of the mission and their states (multiplayer: as the local
// player's team sees them).
void C_InfoScreen::AddObjectives()
{
    C_MissionDef *mission = g_campaign->GetCurrentMission();
    if (!mission)
        return;
    for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
        S_MissionObjective *objective = mission->GetObjective(i);
        const int status = objective->status;
        if (status == OBJECTIVE_INACTIVE)
            continue;
        int textId;
        if (IsLocalGame()) {
            textId = objective->GetId();
        } else {
            const auto *mp = static_cast<S_MpObjective *>(objective);
            const int team = g_netGame.GetLocalTeam();
            if (team == TEAM_AXIS)
                textId = mp->axisText;
            else if (team == TEAM_ALLIES)
                textId = mp->alliedText;
            else
                continue;
        }
        m_text->AddTextId(textId, Style(1));

        int stateText;
        if (IsLocalGame()) {
            switch (status) {
            case OBJECTIVE_OPEN: stateText = TEXT_OBJECTIVE_OPEN; break;
            case OBJECTIVE_COMPLETED: stateText = TEXT_OBJECTIVE_COMPLETED; break;
            case OBJECTIVE_FAILED: stateText = TEXT_OBJECTIVE_FAILED; break;
            case OBJECTIVE_FAILED_CRITICAL: stateText = TEXT_OBJECTIVE_FAILED_CRITICAL; break;
            default: continue;
            }
        } else {
            // Completed / failed from the attackers' side (the objective's team).
            const bool attacker = static_cast<S_MpObjective *>(objective)->team == g_netGame.GetLocalTeam();
            switch (status) {
            case OBJECTIVE_OPEN: stateText = TEXT_OBJECTIVE_OPEN; break;
            case OBJECTIVE_COMPLETED:
                stateText = attacker ? TEXT_OBJECTIVE_COMPLETED : TEXT_OBJECTIVE_FAILED;
                break;
            case OBJECTIVE_FAILED:
                stateText = attacker ? TEXT_OBJECTIVE_FAILED : TEXT_OBJECTIVE_COMPLETED;
                break;
            default: continue;
            }
        }
        m_text->AddTextId(stateText, Style(2));
    }
}

// Co-op: the players by name (their rank, kills, deaths and stat 4).
void C_InfoScreen::AddCoopPlayers()
{
    m_text->AddTextId(TEXT_COLUMN_NAME, Style(3));
    m_text->AddTextId(TEXT_COLUMN_STAT4, Style(7));
    m_text->AddTextId(TEXT_COLUMN_KILLS, Style(5));
    m_text->AddTextId(TEXT_COLUMN_DEATHS, Style(6));
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const S_NetPlayerState *local = players + g_localPlayerSlot;
    std::vector<S_NetPlayerState *> list = ConnectedPlayers(players, g_netGame.GetPlayerCount());
    SortByName(list);
    for (S_NetPlayerState *player : list) {
        const bool inactive = IsInactive(player);
        const int rank = ClampRank(player->stats[PLAYER_RANK]);
        const C_str name = C_fstr("%s %s", g_texts.Get(TEXT_RANKS_ALLIED + rank).c_str(), player->name);
        int style;
        if (player == local)
            style = inactive ? 70 : 69;
        else
            style = inactive ? 68 : 67;
        m_text->AddText(name, Style(style));
        AddNumber(player->stats[PLAYER_KILLS], inactive ? 51 : 10);
        AddNumber(player->stats[PLAYER_DEATHS], inactive ? 52 : 11);
        AddNumber(player->stats[PLAYER_STAT4], inactive ? 53 : 12);
    }
    g_netGame.UnlockPlayers();
}

// Deathmatch: the players by score (their uniform's rank names).
void C_InfoScreen::AddDeathmatchPlayers()
{
    m_text->AddTextId(TEXT_COLUMN_NAME, Style(3));
    m_text->AddTextId(TEXT_COLUMN_STAT4, Style(7));
    m_text->AddTextId(TEXT_COLUMN_KILLS, Style(5));
    m_text->AddTextId(TEXT_COLUMN_DEATHS, Style(6));
    m_text->AddTextId(TEXT_COLUMN_SCORE, Style(4));
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const S_NetPlayerState *local = players + g_localPlayerSlot;
    std::vector<S_NetPlayerState *> list = ConnectedPlayers(players, g_netGame.GetPlayerCount());
    SortByScore(list);
    for (S_NetPlayerState *player : list) {
        const bool inactive = IsInactive(player);
        const int rank = ClampRank(player->stats[PLAYER_RANK]);
        // The rank by the side of the uniform worn (none: only the name).
        C_str name;
        const C_Item *uniform = player->human ? player->human->GetUniformItem() : nullptr;
        const int side = uniform ? uniform->GetDef()->GetTeam() : -1;
        if (side == 0)
            name = C_fstr("%s %s", g_texts.Get(TEXT_RANKS_ALLIED + rank).c_str(), player->name);
        else if (side == 1)
            name = C_fstr("%s %s", g_texts.Get(TEXT_RANKS_AXIS + rank).c_str(), player->name);
        else
            name = C_fstr("%s", player->name);
        int style;
        if (player == local)
            style = inactive ? 61 : 60;
        else
            style = inactive ? 49 : 8;
        m_text->AddText(name, Style(style));
        AddNumber(player->stats[PLAYER_SCORE], inactive ? 50 : 9);
        AddNumber(player->stats[PLAYER_KILLS], inactive ? 51 : 10);
        AddNumber(player->stats[PLAYER_DEATHS], inactive ? 52 : 11);
        AddNumber(player->stats[PLAYER_STAT4], inactive ? 53 : 12);
    }
    g_netGame.UnlockPlayers();
}

void C_InfoScreen::AddTeamPlayers()
{
    // The teams' headings with their scores (the objective game: the rounds won).
    auto teamValue = [this](int team) {
        if (m_kind == 3)
            return C_str(C_fstr("%d %s", g_netGame.GetTeamWins()[team], g_texts.Get(TEXT_ROUNDS_WON).c_str()));
        const int score = static_cast<int>(g_netGame.GetTeamScore(team));
        return C_str(C_fstr("%d %s", score, g_texts.Get(TEXT_TEAM_SCORE).c_str()));
    };
    C_str value = teamValue(TEAM_ALLIES);
    m_text->AddTextId(TEXT_ALLIES, Style(23));
    m_text->AddText(value, Style(45));
    value = teamValue(TEAM_AXIS);
    m_text->AddTextId(TEXT_AXIS, Style(29));
    m_text->AddText(value, Style(46));
    AddBlankLine();
    m_text->AddTextId(TEXT_COLUMN_NAME, Style(13));
    m_text->AddTextId(TEXT_COLUMN_STAT4, Style(17));
    m_text->AddTextId(TEXT_COLUMN_KILLS, Style(15));
    m_text->AddTextId(TEXT_COLUMN_DEATHS, Style(16));
    m_text->AddTextId(TEXT_COLUMN_SCORE, Style(14));
    m_text->AddTextId(TEXT_COLUMN_NAME, Style(18));
    m_text->AddTextId(TEXT_COLUMN_STAT4, Style(22));
    m_text->AddTextId(TEXT_COLUMN_KILLS, Style(20));
    m_text->AddTextId(TEXT_COLUMN_DEATHS, Style(21));
    m_text->AddTextId(TEXT_COLUMN_SCORE, Style(19));

    S_NetPlayerState *players = g_netGame.LockPlayers();
    const S_NetPlayerState *local = players + g_localPlayerSlot;
    const int count = g_netGame.GetPlayerCount();
    std::vector<S_NetPlayerState *> allies;
    std::vector<S_NetPlayerState *> axis;
    for (int i = 0; i < count; ++i) {
        S_NetPlayerState *player = &players[i];
        if (!player->IsConnected())
            continue;
        if (player->team == TEAM_AXIS)
            axis.push_back(player);
        else if (player->team == TEAM_ALLIES)
            allies.push_back(player);
    }
    // (The original also fetches text 0xA40 here and does not use it.)
    m_text->AddTextId(TEXT_UNDER_HEADINGS, Style(47));
    m_text->AddTextId(TEXT_UNDER_HEADINGS, Style(48));
    SortByScore(allies);
    SortByScore(axis);
    const std::size_t rows = std::max(allies.size(), axis.size());
    for (std::size_t i = 0; i < rows; ++i) {
        S_NetPlayerState *left = i < allies.size() ? allies[i] : nullptr;
        AddTeamPlayer(left, left && left == local, 0);
        if (i < axis.size())
            AddTeamPlayer(axis[i], axis[i] == local, 1);
    }
    g_netGame.UnlockPlayers();
}

void C_InfoScreen::AddTeamPlayer(S_NetPlayerState *player, bool local, int column)
{
    if (player) {
        const S_ColumnStyles &styles = COLUMN_STYLES[column];
        // Nobody dims during the warm-up.
        bool inactive = false;
        if (GetGame()->GetRoundState() != ROUND_WARMUP)
            inactive = IsInactive(player);
        const int rank = ClampRank(player->stats[PLAYER_RANK]);
        m_text->AddText(C_fstr("%c", RANK_GLYPHS[column][rank]), Style(styles.insignia));
        const int dim = inactive ? 1 : 0;
        m_text->AddText(C_fstr("%s", player->name), Style(local ? styles.localName[dim] : styles.name[dim]));
        AddNumber(player->stats[PLAYER_STAT4], styles.stat4[dim]);
        AddNumber(player->stats[PLAYER_KILLS], styles.kills[dim]);
        AddNumber(player->stats[PLAYER_DEATHS], styles.deaths[dim]);
        AddNumber(player->stats[PLAYER_SCORE], styles.score[dim]);
    }
    if (column == 0)
        AddBlankLine();
}

void C_InfoScreen::AddServer()
{
    m_text->AddText(C_fstr("%s %s", g_texts.Get(TEXT_SERVER).c_str(), g_netGame.GetServerName()), Style(55));
}

void C_InfoScreen::AddMissionName()
{
    if (IsLocalGame())
        return;
    if (C_MissionDef *mission = GetGame()->GetMission())
        m_text->AddText(static_cast<C_MpMissionDef *>(mission)->GetMapName(), Style(66));
}

void C_InfoScreen::AddRoundTime()
{
    const int seconds = GetGame()->GetRoundTime() / 1000;
    m_text->AddText(C_fstr("%s %02d:%02d:%02d", g_texts.Get(TEXT_ROUND_TIME).c_str(), seconds / 3600,
                           seconds / 60 % 60, seconds % 60),
                    Style(65));
}

// Deathmatch: the time left of the round's limit and the point limit.
void C_InfoScreen::AddLimits()
{
    const int roundLimit = g_mpSettings->roundLimit;
    const int pointLimit = g_mpSettings->pointLimit;
    if (roundLimit > 0) {
        const int seconds = (roundLimit * 60000 - GetGame()->GetRoundTime()) / 1000;
        m_text->AddText(C_fstr("%s %02d:%02d:%02d", g_texts.Get(TEXT_TIME_LEFT).c_str(), seconds / 3600,
                               seconds / 60 % 60, seconds % 60),
                        Style(65));
    }
    if (pointLimit > 0) {
        const int style = roundLimit > 0 ? 72 : 71;
        m_text->AddText(C_fstr("%s %d", g_texts.Get(TEXT_POINT_LIMIT).c_str(), pointLimit), Style(style));
    }
}

bool C_InfoScreen::SaveHints(C_chunk &chunk) const
{
    if (m_hints.empty())
        return true;
    chunk.Begin(CHUNK_INFO_HINTS);
    for (const int hint : m_hints)
        chunk.Write(hint);
    chunk.Ascend();
    return true;
}

bool C_InfoScreen::LoadHints(C_chunk &chunk)
{
    m_hints.clear();
    while (chunk.RemainingSize() > 0) {
        uint16_t id;
        chunk.Descend(id);
        if (id == CHUNK_INFO_HINTS) {
            // Fix: a partial id at the end made the original add the last id forever.
            int hint = 0;
            while (chunk.RemainingSize() >= static_cast<int>(sizeof(hint))) {
                chunk.Read(hint);
                m_hints.push_back(hint);
            }
        }
        chunk.Ascend();
    }
    return true;
}
