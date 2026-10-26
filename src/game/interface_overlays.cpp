// The interface's overlays (see interface.h; the original's 0x004ad250..0x004b0220): the damage
// flash, the big message, the objectives' icons, the score table, the voting panel, the photo
// flash, the explosives' timers, the carnage counter, the pause and countdown texts.
#include "game/interface.h"
#include "game/interface_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "script/script_module.h"

namespace {

// The text styles (the menus' style table).
enum : uint8_t {
    STYLE_BIG_MESSAGE = 0x3F,
    STYLE_BIG_TITLE = 0x40,
    STYLE_BIG_LINE = 0x41,
    STYLE_SCORE_1 = 0x43,
    STYLE_SCORE_2 = 0x44,
    STYLE_SCORE_3 = 0x45,
    STYLE_SCORE_4 = 0x46,
    STYLE_EXPLOSIVE_TIMER = 0x48,
    STYLE_CARNAGE = 0x49,
    STYLE_STATUS = 0x4A,
};

enum : int {
    TEXT_ALLIES = 0xA3C,
    TEXT_AXIS = 0xA3D,
    TEXT_PAUSED = 0xA46,
};

// The photo's shutter (the object manipulation chapter's group 0x2E).
constexpr int PHOTO_SOUND_GROUP = 0x2E;
constexpr int PHOTO_TIME = 2000;

// The players' actor slots counted by the score table.
constexpr int PLAYER_SLOTS = 32;

C_TextBox *NewText(I3D_scene *scene, I3D_frame *frame, bool clear90)
{
    auto *text = new C_TextBox();
    if (clear90)
        text->SetUnknown90(0);
    text->AttachTo(scene, frame);
    return text;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// C_InjuryFlash

bool C_InjuryFlash::Init(I3D_scene *scene)
{
    m_video = nullptr;
    for (int i = 0; i < VIDEOS; ++i) {
        char name[256];
        std::sprintf(name, "GameMovie\\AnimHit%d.bik", i + 1);
        m_videos[i] = GetIGraph()->CreateStream();
        if (m_videos[i]->Open(name, 0) != 0) {
            m_videos[i]->Release();
            m_videos[i] = nullptr;
        }
    }
    m_frame = scene->FindFrame("injury", FRAME_MASK_ANY);
    static_cast<I3D_object_visual *>(m_frame)->DuplicateMesh();
    static_cast<I3D_visual *>(m_frame)->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].DuplicateMaterial();
    m_frame->SetOn(false);
    m_frame->Update();
    return true;
}

void C_InjuryFlash::Done()
{
    for (ISND_stream *&video : m_videos) {
        if (!video)
            continue;
        video->Stop();
        video->Close();
        video->Release();
        video = nullptr;
    }
}

void C_InjuryFlash::PickVideo()
{
    if (m_video)
        m_video->Stop();
    m_video = m_videos[std::rand() % VIDEOS];
}

// Fades in within 125 ms to half, then out at 0.5 a second (the video playing).
void C_InjuryFlash::Start(bool on)
{
    m_frame->SetOn(on);
    if (!on) {
        if (m_video)
            m_video->Stop();
        m_video = nullptr;
        m_frame->SetOn(false);
        m_frame->Update();
        return;
    }
    PickVideo();
    if (!m_video)
        return;
    m_frame->SetOn(true);
    I3D_material *material = FirstMaterial(m_frame);
    material->SetTexture(*m_video->GetVideoTexture());
    m_video->SetPosition(0);
    m_video->Play();
    m_frame->Update();
    m_rising = true;
    m_time = 0;
    m_alpha = 0.0f;
}

void C_InjuryFlash::Update(int timeDelta)
{
    if (!m_video)
        return;
    m_time += timeDelta;
    if (m_rising) {
        m_alpha += static_cast<float>(timeDelta) * 0.001f * 4.0f;
        if (m_alpha >= 0.5f) {
            m_alpha = 0.5f;
            m_rising = false;
        }
    } else {
        m_alpha -= static_cast<float>(timeDelta) * 0.001f * 0.5f;
        if (m_alpha <= 0.0f) {
            m_alpha = 0.0f;
            Start(false);
            return;
        }
    }
    static_cast<I3D_object_visual *>(m_frame)->SetAlpha(m_alpha);
    m_video->UpdateVideo(0);
}

// ---------------------------------------------------------------------------------------------
// C_BigMessage

bool C_BigMessage::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("Htext", FRAME_MASK_ANY);
    m_text = NewText(scene, m_frame, false);
    Show(false, -1);
    return true;
}

void C_BigMessage::Done()
{
    delete m_text;
}

void C_BigMessage::ShowRoundResult(bool show, int textId, int axisWins, int alliedWins)
{
    m_text->Clear();
    if (show) {
        m_text->AddTextId(textId, STYLE_BIG_TITLE);
        const C_str axis = g_texts.Get(TEXT_AXIS);
        const C_str allies = g_texts.Get(TEXT_ALLIES);
        m_text->AddText(C_fstr("%s     %d  :  %d     %s", allies.c_str(), alliedWins, axisWins, axis.c_str()),
                        STYLE_BIG_LINE);
    }
    m_frame->SetOn(show);
}

void C_BigMessage::ShowCountdown(bool show, int textId, int ms)
{
    m_text->Clear();
    if (show) {
        m_text->AddTextId(textId, STYLE_BIG_TITLE);
        m_text->AddText(C_fstr("%02d:%02d", ms % 3600000 / 60000, ms % 60000 / 1000), STYLE_BIG_LINE);
    }
    m_frame->SetOn(show);
}

void C_BigMessage::Show(bool show, int textId)
{
    m_text->Clear();
    if (show)
        m_text->AddTextId(textId, STYLE_BIG_MESSAGE);
    m_frame->SetOn(show);
}

void C_BigMessage::Render()
{
    if (m_frame->IsOn())
        m_text->Render();
}

bool C_BigMessage::IsShown() const
{
    return m_frame->IsOn();
}

// ---------------------------------------------------------------------------------------------
// C_ObjectiveIcons

bool C_ObjectiveIcons::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("mpobjectives", FRAME_MASK_DUMMY);
    for (int i = 0; i < OBJECTIVES; ++i) {
        char name[32];
        std::sprintf(name, "mpallied_obj%02d", i);
        m_icons[i][0] = scene->FindFrame(name, FRAME_MASK_VISUAL);
        std::sprintf(name, "mpaxis_obj%02d", i);
        m_icons[i][1] = scene->FindFrame(name, FRAME_MASK_VISUAL);
        std::sprintf(name, "mpneutral_obj%02d", i);
        m_icons[i][2] = scene->FindFrame(name, FRAME_MASK_VISUAL);
        SetIcon(i, -1);
    }
    return true;
}

void C_ObjectiveIcons::SetIcon(int index, int state)
{
    m_icons[index][0]->SetOn(state == TEAM_ALLIES);
    m_icons[index][1]->SetOn(state == TEAM_AXIS);
    m_icons[index][2]->SetOn(state == TEAM_NONE);
    for (I3D_frame *icon : m_icons[index])
        icon->Update();
}

// ---------------------------------------------------------------------------------------------
// C_ScoreTable

bool C_ScoreTable::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("mp_scoretable", FRAME_MASK_ANY);
    m_text = NewText(scene, m_frame, true);
    m_gameType = -1;
    if (!IsLocalGame()) {
        m_gameType = g_mpSettings->gameType;
        if (m_gameType == MP_GAME_DEATHMATCH)
            m_gameType = -1;
    }
    switch (m_gameType) {
    case MP_GAME_OCCUPATION:
        // The teams' scores, the respawn and the round's clocks.
        m_text->AddText(C_fstr("0"), STYLE_SCORE_1);
        m_text->AddText(C_fstr("0"), STYLE_SCORE_2);
        m_text->AddText(C_fstr("00"), STYLE_SCORE_3);
        m_text->AddText(C_fstr("00:00"), STYLE_SCORE_4);
        m_frame->SetOn(true);
        return true;
    case MP_GAME_OBJECTIVES:
        // The teams' living players, the respawn's (if any) and the round's clocks.
        // (Quirk kept: the table's frame is left as it is.)
        m_text->AddText(C_fstr("0"), STYLE_SCORE_1);
        m_text->AddText(C_fstr("0"), STYLE_SCORE_2);
        if (g_mpSettings->allowRespawn)
            m_text->AddText(C_fstr("00"), STYLE_SCORE_3);
        m_text->AddText(C_fstr("00:00"), STYLE_SCORE_4);
        return true;
    case MP_GAME_COOPERATIVE: {
        // The lives left, the respawn's and the mission's clocks; the allied score's place.
        m_text->AddText(C_fstr("99/99"), STYLE_SCORE_2);
        m_text->AddText(C_fstr("00"), STYLE_SCORE_3);
        m_text->AddText(C_fstr("00:00"), STYLE_SCORE_4);
        I3D_frame *allied = m_frame->FindChildFrame("mpallied_score", FRAME_MASK_ANY);
        I3D_frame *axis = m_frame->FindChildFrame("mpaxis_score", FRAME_MASK_ANY);
        allied->SetPosInline(axis->GetPos());
        axis->SetOn(false);
        axis->Update();
        allied->Update();
        return true;
    }
    default:
        m_frame->SetOn(false);
        return true;
    }
}

void C_ScoreTable::Done()
{
    delete m_text;
    m_text = nullptr;
}

void C_ScoreTable::Update(int /*timeDelta*/)
{
    C_game *game = GetGame();
    switch (m_gameType) {
    case MP_GAME_OCCUPATION: {
        const int axis = static_cast<int>(g_netGame.GetTeamScore(0));
        const int allied = static_cast<int>(g_netGame.GetTeamScore(1));
        const int respawn = game->GetRespawnTimeLeft() / 1000;
        const int round = game->GetRoundTimeLeft() / 1000;
        m_text->SetText(C_fstr("%d", axis), STYLE_SCORE_1, 0);
        m_text->SetText(C_fstr("%d", allied), STYLE_SCORE_2, 1);
        m_text->SetText(C_fstr("%2d:%02d", respawn / 60, respawn % 60), STYLE_SCORE_3, 2);
        m_text->SetText(C_fstr("%2d:%02d", round / 60, round % 60), STYLE_SCORE_4, 3);
        break;
    }
    case MP_GAME_OBJECTIVES: {
        // The living players in play of each team (the players' actor slots).
        int axis = 0;
        int allied = 0;
        const std::vector<C_Actor *> &actors = g_actorsModule.GetActors();
        // Fix: the original read the 32 slots whatever the actor count.
        const int slots = std::min(PLAYER_SLOTS, static_cast<int>(actors.size()));
        for (int i = 0; i < slots; ++i) {
            auto *human = static_cast<C_Human *>(actors[i]);
            if (!human || !human->IsAlive() || human->IsSpectating())
                continue;
            if (human->GetTeam(false) == TEAM_AXIS)
                ++axis;
            else
                ++allied;
        }
        const int round = game->GetRoundTimeLeft() / 1000;
        m_text->SetText(C_fstr("%d", axis), STYLE_SCORE_1, 0);
        m_text->SetText(C_fstr("%d", allied), STYLE_SCORE_2, 1);
        if (!g_mpSettings->allowRespawn) {
            m_text->SetText(C_fstr("%02d:%02d", round / 60, round % 60), STYLE_SCORE_4, 2);
        } else {
            const int respawn = game->GetRespawnTimeLeft() / 1000;
            m_text->SetText(C_fstr("%2d:%02d", respawn / 60, respawn % 60), STYLE_SCORE_3, 2);
            m_text->SetText(C_fstr("%02d:%02d", round / 60, round % 60), STYLE_SCORE_4, 3);
        }
        break;
    }
    case MP_GAME_COOPERATIVE: {
        const int respawn = game->GetRespawnTimeLeft() / 1000;
        const int elapsed = game->GetRoundTime() / 1000;
        // The lives: the team's shared ones or the local player's.
        int used;
        const int lives = g_mpSettings->coopLives;
        if (g_mpSettings->teamLives) {
            used = game->GetTeamLivesUsed();
        } else {
            S_NetPlayerState *players = g_netGame.LockPlayers();
            used = players[g_localPlayerSlot].livesUsed;
            g_netGame.UnlockPlayers();
        }
        if (lives != -1)
            m_text->SetText(C_fstr("%d/%d", lives - used, lives), STYLE_SCORE_2, 0);
        m_text->SetText(C_fstr("%2d:%02d", respawn / 60, respawn % 60), STYLE_SCORE_3, 1);
        m_text->SetText(C_fstr("%2d:%02d", elapsed / 60, elapsed % 60), STYLE_SCORE_4, 2);
        break;
    }
    default:
        break;
    }
}

void C_ScoreTable::Render()
{
    if (m_frame && m_gameType != -1 && m_frame->IsOn())
        m_text->Render();
}

// ---------------------------------------------------------------------------------------------
// C_VotingPanel

bool C_VotingPanel::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("voting", FRAME_MASK_ANY);
    m_text = NewText(scene, m_frame, true);
    if (m_frame)
        m_frame->SetOn(false);
    return true;
}

void C_VotingPanel::Done()
{
    delete m_text;
    m_text = nullptr;
}

void C_VotingPanel::Render()
{
    if (m_frame && m_frame->IsOn())
        m_text->Render();
}

// ---------------------------------------------------------------------------------------------
// C_PhotoFlash

bool C_PhotoFlash::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("camera", FRAME_MASK_ANY);
    Start(false);
    m_timeLeft = 0;
    m_soundPending = false;
    return true;
}

void C_PhotoFlash::Update(int timeDelta)
{
    if (m_timeLeft == 0)
        return;
    m_timeLeft -= timeDelta;
    // The shutter at the camera.
    if (m_timeLeft < 1000 && m_soundPending) {
        S_SoundRequest request;
        request.chapter = SOUND_CHAPTER_OBJECT_MANIPULATION;
        request.pos = GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
        request.flags = SOUND_AMBIENT | SOUND_SILENT_TO_AI;
        request.attachTo = nullptr;
        request.source = nullptr;
        request.group = PHOTO_SOUND_GROUP;
        GetSoundModule()->Play(request);
        m_soundPending = false;
    }
    // A black frame (the shutter closed) between 950 and 800 ms.
    if (m_timeLeft < 950 && m_timeLeft > 800) {
        const S_vector black(0.0f, 0.0f, 0.0f);
        GetGEffects()->SetFadeLevel(1.0f, &black);
    }
    if (m_timeLeft < 1) {
        m_timeLeft = 0;
        Start(false);
    }
}

void C_PhotoFlash::Start(bool on)
{
    if (m_frame)
        m_frame->SetOn(on);
    if (on) {
        m_timeLeft = PHOTO_TIME;
        m_soundPending = true;
    }
}

// ---------------------------------------------------------------------------------------------
// C_ExplosiveTimer

bool C_ExplosiveTimer::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("explosivetimer", FRAME_MASK_ANY);
    m_scene = scene;
    // The mission's explosive places (last first).
    const std::vector<C_Actor *> &actors = g_actorsModule.GetActors();
    for (std::size_t i = actors.size(); i-- > 0;) {
        if (actors[i] && actors[i]->GetType() == ACTOR_EXPLOSIVE_PLACE)
            m_explosives.push_back(actors[i]);
    }
    for (int i = 0; i < TIMERS; ++i) {
        char name[32];
        std::sprintf(name, "timer-explosive%02d", i);
        m_timers[i] = m_frame->FindChildFrame(name, FRAME_MASK_VISUAL);
        m_texts[i] = NewText(scene, m_timers[i], true);
        m_texts[i]->AddText(C_fstr(""), STYLE_EXPLOSIVE_TIMER);
        m_timers[i]->SetOn(false);
    }
    return true;
}

void C_ExplosiveTimer::Done()
{
    m_explosives.clear();
    for (int i = 0; i < TIMERS; ++i) {
        m_timers[i] = nullptr;
        delete m_texts[i];
    }
}

void C_ExplosiveTimer::SetPos(const float *pos)
{
    m_frame->SetPosInline(S_vector(pos[0], pos[1], pos[2]));
    m_frame->Update();
    for (int i = 0; i < TIMERS; ++i) {
        m_texts[i]->AttachTo(m_scene, m_timers[i]);
        m_texts[i]->SetUnknown90(0);
    }
}

// The armed explosives' time left, soonest first (none during a cutscene).
void C_ExplosiveTimer::Update(int /*timeDelta*/)
{
    if (m_explosives.empty())
        return;
    if (g_scriptModule.IsCutsceneRunning()) {
        if (m_timers[0]->IsOn()) {
            for (I3D_frame *timer : m_timers) {
                timer->SetOn(false);
                timer->Update();
            }
        }
        return;
    }
    struct S_Pair {
        uint32_t time;
        int index;
    };
    std::vector<S_Pair> pairs;
    for (std::size_t i = 0; i < m_explosives.size(); ++i) {
        auto *place = static_cast<C_ExplosivePlace *>(m_explosives[i]);
        if (place->GetState() == 1 && place->GetTimeLeft() > 0)
            pairs.push_back({static_cast<uint32_t>(place->GetTimeLeft()), static_cast<int>(i)});
    }
    // (A radix sort of the times in the original: stable.)
    std::stable_sort(pairs.begin(), pairs.end(), [](const S_Pair &a, const S_Pair &b) { return a.time < b.time; });
    for (int i = 0; i < TIMERS; ++i) {
        if (i < static_cast<int>(pairs.size())) {
            const int seconds = static_cast<int>(pairs[i].time) / 1000;
            m_timers[i]->SetOn(true);
            m_texts[i]->SetText(C_fstr("%02d:%02d", seconds / 60, seconds % 60), STYLE_EXPLOSIVE_TIMER, 0);
        } else {
            m_timers[i]->SetOn(false);
        }
        m_timers[i]->Update();
    }
}

void C_ExplosiveTimer::Render()
{
    for (int i = 0; i < TIMERS; ++i) {
        if (m_timers[i]->IsOn())
            m_texts[i]->Render();
    }
}

// ---------------------------------------------------------------------------------------------
// C_CarnageStats

bool C_CarnageStats::Init(I3D_scene *scene)
{
    m_frame = scene->FindFrame("carnage_stats", FRAME_MASK_ANY);
    m_text = NewText(scene, m_frame, true);
    m_text->AddText(C_fstr(""), STYLE_CARNAGE);
    return true;
}

void C_CarnageStats::Done()
{
    delete m_text;
}

void C_CarnageStats::Update(int /*timeDelta*/)
{
    if (m_frame->IsOn())
        m_text->SetText(C_fstr("%d", g_actorsModule.GetCarnageEnemyCount()), STYLE_CARNAGE, 0);
}

void C_CarnageStats::Render()
{
    if (m_frame->IsOn())
        m_text->Render();
}

// ---------------------------------------------------------------------------------------------
// C_PauseText, C_CountdownText

bool C_PauseText::Init(I3D_scene *scene)
{
    I3D_frame *frame = scene->FindFrame("injury", FRAME_MASK_ANY);
    m_text = NewText(scene, frame, true);
    m_text->AddTextId(TEXT_PAUSED, STYLE_STATUS);
    m_shown = false;
    m_unused08 = 0;
    return true;
}

void C_PauseText::Done()
{
    delete m_text;
    m_text = nullptr;
}

void C_PauseText::Render()
{
    if (m_shown)
        m_text->Render();
}

bool C_CountdownText::Init(I3D_scene *scene)
{
    I3D_frame *frame = scene->FindFrame("injury", FRAME_MASK_ANY);
    m_text = NewText(scene, frame, true);
    m_text->AddTextId(TEXT_PAUSED, STYLE_STATUS);
    Show(false, -1, -1);
    m_timeLeft = 0;
    return true;
}

void C_CountdownText::Done()
{
    delete m_text;
    m_text = nullptr;
}

void C_CountdownText::Show(bool show, int textId, int ms)
{
    m_text->Clear();
    if (show) {
        m_timeLeft = ms;
        m_text->AddTextId(textId, STYLE_STATUS);
        m_shown = true;
        return;
    }
    m_timeLeft = 0;
    m_shown = false;
}

void C_CountdownText::Update(int timeDelta)
{
    if (m_timeLeft == -1 || m_timeLeft == 0)
        return;
    m_timeLeft -= timeDelta;
    if (m_timeLeft < 1) {
        m_timeLeft = 0;
        m_shown = false;
    }
}

void C_CountdownText::Render()
{
    if (m_shown)
        m_text->Render();
}
