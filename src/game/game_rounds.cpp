// C_game, part 2 (original source 0x006991c0..0x0069a5b0, 0x0069ad20..0x0069b500,
// 0x0069b500..0x0069c240 and 0x0069d970..0x0069e8b0, 0x0069e9b0..0x0069ec40, 0x0069f1a0):
// the game's ticks and frames, the objectives, the multiplayer rounds (warmup, round end,
// the VIP, respawns, players joining and leaving) and the spectator camera.
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "game/actor.h"
#include "game/actors_module.h"
#include "game/car_effects.h"
#include "game/collision.h"
#include "game/console.h"
#include "game/first_person_view.h"
#include "game/game_internal.h"
#include "game/game_menu.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/human_movement.h"
#include "net/bitstream.h"
#include "game/interface.h"
#include "game/particles.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "script/script_module.h"

namespace {

// Message text of OnSessionClosing()'s reasons 0..3.
const int SESSION_CLOSING_TEXTS[4] = {0x22A7, 0x22A6, 0x22A9, 0x22A8};

// Timers of the server's periodic work (0x008ae6e4, 0x008ae6e8, 0x008ae698) and a counter
// nothing reads (0x008ae6e0).
int g_roundCheckTimer = 0;
int g_scoreSendTimer = 0;
int g_areaCheckTimer = 0;
int g_roundChecks = 0;

// The other of the two teams.
int OtherTeam(int team)                             // 0x00725270 (DS 0x0077c9d0)
{
    return (team + 1) % 2;
}

// The scores go to the clients every 5 s.
void SendScoresPeriodically(int timeDelta)          // 0x006995f0
{
    g_scoreSendTimer += timeDelta;
    if (g_scoreSendTimer > 4999) {
        g_scoreSendTimer = 0;
        GetGameApp()->SendGameMessage(GAME_MSG_SCORES, 0, 0, 0, false);
    }
}

// Freezes (no collision, no physics, hidden) / thaws the players' humans.
void FreezePlayers(bool frozen)                     // 0x00699d70
{
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const int count = g_netGame.GetPlayerCount();
    for (int slot = 0; slot < count; ++slot) {
        if (!players[slot].IsConnected() || !players[slot].human)
            continue;
        C_Human *human = players[slot].human;
        C_HumanMovement *movement = human->GetMovement();
        if (movement->IsDetached() != frozen) {
            movement->SetDetachedFlag(frozen);
            movement->OnFrozen(frozen);
            human->GetFrame()->SetOn(!frozen);
        }
    }
    g_netGame.UnlockPlayers();
}

// `points` for the team and for each of its living, playing members.
void AwardTeamPoints(int points, int team)          // 0x0069bbd0
{
    g_netGame.AddTeamScore(team, static_cast<float>(points));
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const int count = g_netGame.GetPlayerCount();
    for (int slot = 0; slot < count; ++slot) {
        if (!players[slot].IsConnected() || !players[slot].human)
            continue;
        C_Human *human = players[slot].human;
        if (!human->IsAlive() || human->GetController()->IsSpectating())
            continue;
        if (human->GetTeam(false) == team)
            g_netGame.AddScore(slot, points);
    }
    g_netGame.UnlockPlayers();
}

}  // namespace

// Switches the session to map `name` (null: the next one).
void ChangeMap(const char *name)                    // 0x0069bcc0
{
    if (!g_mpSettings->SelectMap(name))
        return;
    C_game::SendToPlayers(GAME_MSG_SETTINGS);
    GetGameApp()->SendGameMessage(GAME_MSG_SESSION, 0, 0, 0, true);
    g_netGame.Process(1000, false);
    TeamWins(0) = 0;
    TeamWins(1) = 0;
    TeamWins(2) = 0;
    GetGameApp()->m_state->SetPendingState(GAME_STATE_NEXT_MAP);
}

void LoadNextMap()                                  // 0x0069bca0
{
    g_mpSettings->SelectMap(nullptr);
    GetGameApp()->m_state->SetPendingState(GAME_STATE_NEXT_MAP);
}

// A human or a car within 2 m above `pos` (spawning there would collide).
bool IsSpawnBlocked(const S_vector4 &pos)           // 0x0069d970
{
    float hitDist = 0.0f;
    float height = 2.0f;
    for (;;) {
        const S_vector from = {pos.x, height + pos.y, pos.z};
        const S_vector dir = {0.0f, -height, 0.0f};
        const S_CollisionVolume *hit = CollisionSphereTestDistance(from, dir, 0.2f, hitDist, nullptr, nullptr, 0, 0);
        height -= hitDist + 0.01f;
        if (!hit)
            return false;
        if ((hit->flags & 0x40) && (hit->flags & 0xC0) && hit->frame) {
            const S_FrameData *data = hit->frame->GetData();
            if (data && data->actor) {
                const int type = data->actor->GetType();
                if (type == ACTOR_HUMAN || type == ACTOR_CAR)
                    return true;
            }
        }
        if (height <= 0.01f)
            return false;
    }
}

// Index (in the actors module) of the multiplayer zone number `number`, -1 if none.
int FindZoneByNumber(int number)                    // 0x0069dbb0
{
    for (int i = 0; i < g_actorsModule.GetActorCount(); ++i) {
        C_Actor *actor = g_actorsModule.GetActor(i);
        if (actor && actor->GetType() == ACTOR_MP_ZONE && static_cast<C_MPZone *>(actor)->GetNumber() == number)
            return i;
    }
    return -1;
}

// Index of the team's spawn zone when it has exactly one, else -1.
int FindOnlyTeamZone(int team)                      // 0x0069dc10
{
    int found = 0;
    int index = -1;
    for (int i = 0; i < g_actorsModule.GetActorCount(); ++i) {
        C_Actor *actor = g_actorsModule.GetActor(i);
        if (actor && actor->GetType() == ACTOR_MP_ZONE && static_cast<C_MPZone *>(actor)->GetTeam() == team) {
            ++found;
            index = i;
        }
        if (found > 1)
            break;
    }
    C_Actor *zone = g_actorsModule.GetActor(index);
    if (found == 1 && zone)
        return index;
    return -1;
}

// Occupation: a respawning player faces the zone to take (axis: the highest number, allies:
// number 0) from `pos`, level.
void FaceTargetZone(S_vector4 *dir, const S_vector4 &pos, int team)   // 0x0069e370
{
    int best = 0;
    int bestId = 0;
    for (int id = 0; id < g_actorsModule.GetActorCount(); ++id) {
        C_Actor *actor = g_actorsModule.GetActorById(id);
        if (!actor || actor->GetType() != ACTOR_MP_ZONE)
            continue;
        const int number = static_cast<C_MPZone *>(actor)->GetNumber();
        if ((best < number && team == 0) || (number == 0 && team == 1)) {
            best = number;
            bestId = id;
        }
    }
    C_Actor *zone = g_actorsModule.GetActorById(bestId);
    if (!zone || zone->GetType() != ACTOR_MP_ZONE)
        return;
    const S_vector4 &zonePos = zone->GetPosition4();
    // (The original leaves w as it was on the stack.)
    *dir = {zonePos.x - pos.x, 0.0f, zonePos.z - pos.z, 0.0f};
    const float lenSq = dir->z * dir->z + dir->x * dir->x;
    if (std::fabs(lenSq - 1.0f) < 1e-8f)
        return;
    if (lenSq < 1e-8f) {
        if (dir->x != 0.0f)
            dir->x = dir->x < 0.0f ? -1.0f : 1.0f;
        else if (dir->z == 0.0f)
            dir->y = 1.0f;
        else
            dir->z = dir->z < 0.0f ? -1.0f : 1.0f;
        return;
    }
    const float scale = 1.0f / std::sqrt(lenSq);
    dir->x *= scale;
    dir->y = scale * 0.0f;
    dir->z *= scale;
}

namespace {

// Shows a message in the in-game menu when it is open.
void ShowGameMenuMessage(int textId)                // 0x0069e9b0
{
    C_GameState *state = GetGameApp()->m_state;
    if (textId >= 0 && state && state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU)
        static_cast<C_GameMenu *>(state)->ShowMessage(textId, -1);
}

} // namespace

// Sends message `type` to each connected player but the local one.
void C_game::SendToPlayers(int type)                // 0x00699560
{
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const int count = g_netGame.GetPlayerCount();
    for (int slot = 0; slot < count; ++slot) {
        if (slot != g_localPlayerSlot && players[slot].IsConnected())
            GetGameApp()->SendGameMessage(type, players[slot].connection, 0, 0, false);
    }
    g_netGame.UnlockPlayers();
}

// ---------------------------------------------------------------------------
// Ticks and frames

void C_game::Tick(int timeDelta)                    // 0x0069b1e0 (DS 0x006dc050)
{
    if (IsClient()) {
        m_interface->FilterInput(nullptr, nullptr);
        if (IsClient())
            g_teamManager.GetPlayerTeam()->Tick(timeDelta);
    }
    if (!IsLocalGame()) {
        if (g_mpSettings->dedicated) {
            // The dedicated server's console commands.
            for (char *command = GetConsole()->PopCommand(); command; command = GetConsole()->PopCommand())
                GetCommandProcessor()->Execute(command);
        }
        if (m_roundState == ROUND_WARMUP) {
            if (static_cast<uint32_t>(g_mpSettings->warmup * 1000) < m_missionTime) {
                // The warmup is over: the round starts.
                if (g_mpSettings->gameType != MP_GAME_OBJECTIVES && !g_gameProfile.IsCooperativeGame())
                    g_netGame.ResetScores();
                m_roundState = ROUND_PLAYING;
                if (IsClient()) {
                    C_Human *human = g_netGame.GetPlayerHuman(g_localPlayerSlot);
                    if (human)
                        human->GetController()->SetSpectating(false);
                }
                for (int id = 0; id < g_actorsModule.GetActorCount(); ++id) {
                    C_Actor *actor = g_actorsModule.GetActorById(id);
                    if (actor && actor->GetType() == ACTOR_MP_ZONE)
                        static_cast<C_MPZone *>(actor)->ResetSpawnIndex();
                }
                m_roundTime = 0;
                if (g_mpSettings->teamLives == 0) {
                    S_NetPlayerState *players = g_netGame.LockPlayers();
                    const int count = g_netGame.GetPlayerCount();
                    for (int slot = 0; slot < count; ++slot)
                        players[slot].livesUsed = players[slot].human ? -1 : 0;
                    g_netGame.UnlockPlayers();
                } else {
                    int axis = 0;
                    int allied = 0;
                    g_netGame.CountTeamPlayers(&axis, &allied);
                    m_teamLivesUsed = -(axis + allied);
                }
                GetGameApp()->SendGameMessage(GAME_MSG_WARMUP_FINISHED, 0, 0, 0, false);
                ChooseVip();
            }
            if (m_missionTime == 100)
                StartWarmup();
        } else {
            if (m_roundState == ROUND_INTERMISSION && m_roundStateTime + 5000 < m_missionTime) {
                if (++m_round < g_mpSettings->roundCount)
                    StartNextRound();
                else
                    ChangeMap(nullptr);
            }
            CheckRoundEnd(timeDelta);
            SendScoresPeriodically(timeDelta);
        }
    }
    if (m_paused)
        return;
    m_missionTime += timeDelta;
    if (!IsClient()) {
        if (m_roundState == ROUND_PLAYING) {
            m_roundTime += timeDelta;
            m_respawnWaveTime += timeDelta;
        }
        m_time += timeDelta;
    }
    ModuleManager::BroadcastTick(timeDelta);
    TimedEvents_Update(timeDelta, false);
    if (!IsClient()) {
        m_scene->Tick(timeDelta);
        for (size_t i = m_animatedModels.size(); i-- > 0;)
            m_animatedModels[i]->Tick(timeDelta);
    }
}

void C_game::Update(int timeDelta)                  // 0x0069b500 (DS 0x006dc3b0)
{
#ifndef HD2_DEDICATED   // no interface on the server
    if (IsClient())
        m_interface->FilterInput(nullptr, nullptr);
#endif
    if (m_inputDelay > 0) {
        m_inputDelay -= timeDelta;
        if (m_inputDelay < 0)
            m_inputDelay = 0;
    }
    if (m_musicTimer != 0) {
        m_musicTimer -= timeDelta;
        if (m_musicTimer < 0)
            m_musicTimer = 0;
    }

    if (IsLocalGame()) {
        // 4 s after the objectives are decided (or the mission failed) the mission ends.
        if (m_roundState == ROUND_OVER && m_roundStateTime + 4000 < m_missionTime &&
            (AreObjectivesDecided() || m_missionFailed)) {
            m_roundState = ROUND_PLAYING;
            if (GetGameApp()->m_state != this)
                return;
            if (!g_playMode->singleMission && !m_missionFailed) {
                EndMission(100);
                if (g_teamManager.GetPlayerTeam()->GetActivePlayer())
                    ::GetCameraModule()->SetSpecialMode(2, true, nullptr);
                return;
            }
            m_state = GAME_STATE_GAME_MENU;
            return;
        }
    } else {
        CheckPlayerAreas(timeDelta);
        if (m_roundState == ROUND_WARMUP) {
#ifndef HD2_DEDICATED   // no interface on the server
            int left = g_mpSettings->warmup * 1000 - static_cast<int>(m_missionTime);
            if (left < 0)
                left = 0;
            m_interface->ShowCountdown(TEXT_WARMUP, left);
#endif
        } else {
            if (g_mpSettings->gameType != MP_GAME_DEATHMATCH &&
                (g_mpSettings->respawnTime + 1) * 1000 < m_respawnWaveTime && g_mpSettings->allowRespawn) {
                m_respawnWaveTime = m_roundTime % 1000;
                GetGameApp()->SendGameMessage(GAME_MSG_RESPAWN_WAVE, 0, 0, 0, false);
            }
#ifdef HD2_DEDICATED   // no interface on the server (the state is still read)
            if (!IsServer())
                g_netGame.GetConnectionState();
#else
            if (!IsServer() && g_netGame.GetConnectionState() == NET_CONNECTION_LOST)
                GetGame()->GetInterface()->ShowTimer(TEXT_CONNECTION_PROBLEM, 1000);
#endif
        }
        if (m_roundState == ROUND_CLOSING && m_roundStateTime + 4000 < m_missionTime) {
            m_roundState = ROUND_PLAYING;
            g_netGame.Close(false);
            GetGameApp()->m_state->SetPendingState(GAME_STATE_LOBBY);
        }
        if (m_roundState == ROUND_OVER && m_roundStateTime + 4000 < m_missionTime) {
            m_roundStateTime = m_missionTime;
            m_roundState = ROUND_INTERMISSION;
#ifndef HD2_DEDICATED   // no interface on the server
            if (IsClient()) {
                m_interface->ShowScores(true);
                m_interface->ShowMessage(-1);
            }
#endif
        }
        g_netGame.UpdateVote(timeDelta);
        if (m_respawnRequest != 0) {
            // The command goes out once the respawn screen picked a zone.
            const std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
            const S_SoldierState *state = squad.empty() ? nullptr : squad[0];
            if (state->spawnZone != -1) {
                if (m_respawnRequest == 1)
                    GetGameApp()->PostCommand(APP_NET_COMMAND_0, 0, 0, false);
                else if (m_respawnRequest == 2)
                    GetGameApp()->PostCommand(APP_NET_COMMAND_RESPAWN, 0, 0, false);
                m_respawnRequest = 0;
            }
        }
        if (IsServer() && !m_pendingSpawns.empty()) {
            // The message frees the spawn.
            GetGameApp()->SendGameMessage(GAME_MSG_SPAWN, 0, reinterpret_cast<intptr_t>(m_pendingSpawns.back()), 0, false);
            m_pendingSpawns.pop_back();
        }
    }

    g_collisionWorld.ReleaseEmptyCells();
#ifdef HD2_DEDICATED   // the server has only the server's side
    {
#else
    if (!IsClient()) {
#endif
        if (m_state == GAME_STATE_STARTING)
            m_state = GAME_STATE_RUNNING;
        for (int id = 0; id < g_actorsModule.GetActorCount(); ++id) {
            C_Actor *actor = g_actorsModule.GetActorById(id);
            if (actor && g_actorsModule.GetActorById(id)->GetType() == ACTOR_HUMAN)
                g_actorsModule.GetActorById(id)->Update(timeDelta);
        }
        return;
    }
#ifndef HD2_DEDICATED   // the server has only the server's side
    if (GetGameApp()->m_state == this) {
        if (!m_interface->IsActive()) {
            // The selected soldier looks where the camera does.
            C_Human *human = g_teamManager.GetPlayerTeam()->GetSelectedMember();
            if (human) {
                // (The original copies the camera's w from the stack as well.)
                S_vector4 dir;
                GetGame()->GetCameraModule()->GetCameraDir(3, &dir);
                human->SetDirection(S_vector(dir.x, dir.y, dir.z));
            }
        } else {
            m_interface->Update(timeDelta);
        }
    }
    if (!m_paused && IsClient()) {
        if (m_roundState == ROUND_PLAYING) {
            m_roundTime += timeDelta;
            m_respawnWaveTime += timeDelta;
        }
        m_time += timeDelta;
        if (!IsServer())
            m_missionTime += timeDelta;
    }
    if (m_endDelay != 0) {
        m_endDelay -= timeDelta;
        if (m_endDelay < 0)
            m_endDelay = 0;
    } else if (m_fadeTime != 0) {
        m_fadeTime -= timeDelta;
        if (m_fadeTime < 0)
            m_fadeTime = 0;
    } else if (m_state == GAME_STATE_STARTING) {
        // (Outside cutscenes the original calls an empty interface method here.)
        m_state = GAME_STATE_RUNNING;
    } else if (m_state == GAME_STATE_ENDING) {
        if (IsLocalGame())
            m_state = m_mode == GAME_MODE_QUIT ? GAME_STATE_MAIN_MENU : GAME_STATE_ENDED;
        else
            m_state = GAME_STATE_MAIN_MENU;
    }
    g_teamManager.GetPlayerTeam()->Update(timeDelta);
    if (m_paused) {
        ::GetCameraModule()->Update(timeDelta);
        g_particlesModule.Update(timeDelta);
        if (m_quickMessages)
            m_quickMessages->Update(timeDelta);
        return;
    }
    ModuleManager::BroadcastUpdate(timeDelta);
    m_firstPersonView->Update(timeDelta);
    m_scene->Tick(timeDelta);
    for (size_t i = m_animatedModels.size(); i-- > 0;)
        m_animatedModels[i]->Tick(timeDelta);
    UpdateSpeechSlots(timeDelta);
    RefreshSpeechSlotFrames();
    if (m_playAvi) {
        SetState(GAME_STATE_PLAY_AVI);
        m_playAvi = false;
    }
    if (m_quickMessages)
        m_quickMessages->Update(timeDelta);
    UpdateCarEffectEmitters(timeDelta);
#endif
}

// ---------------------------------------------------------------------------
// Objectives

bool C_game::AreObjectivesDecided() const
{
    return m_mission && m_mission->AreObjectivesDecided();
}

int C_game::GetAttackingTeam() const
{
    // (The original reads the first objective without a check.)
    return GetMpObjective(m_mission, 0)->team;
}

void C_game::SetObjectiveStatus(int objective, int status)   // 0x0069bd30 (DS 0x006dcaa0)
{
    if (!IsLocalGame()) {
        S_MpObjective *target = GetMpObjective(m_mission, objective - 1);
        if (g_mpSettings->gameType == MP_GAME_OBJECTIVES && status == OBJECTIVE_COMPLETED)
            AwardTeamPoints(target->points, target->team);
        ApplyObjectiveStatus(objective, status);
        GetGameApp()->SendGameMessage(GAME_MSG_OBJECTIVE, 0, objective, status, false);
        return;
    }
    // (A failed copy protection check made an objective fail critically now and then.)
    if (status == OBJECTIVE_COMPLETED) {
        bool bonus = false;
        if (m_mission) {
            S_MissionObjective *target = m_mission->GetObjective(objective - 1);
            if (!target)
                return;
            if (target->rewarded) {
                ApplyObjectiveStatus(objective, OBJECTIVE_COMPLETED);
                return;
            }
            bonus = target->bonus;
        }
        // Experience for the squad's living members.
        C_Team *team = g_teamManager.GetPlayerTeam();
        for (int i = team->GetMemberCount(); i-- > 0;) {
            C_Human *member = team->GetMember(i);
            if (member->IsAlive())
                member->AddExperience(bonus ? 1500 : 1000);
        }
    }
    ApplyObjectiveStatus(objective, status);
}

void C_game::ApplyObjectiveStatus(int objective, int status)   // 0x0069bf10 (DS 0x006dcc80)
{
    if (m_mission) {
        S_MissionObjective *target = m_mission->GetObjective(objective - 1);
        if (target)
            target->SetStatus(status);
    }
    // (The release build's debug prints "Objective %d. finished" / "failed" / "failed
    // critical" are empty.)
    if (status == OBJECTIVE_FAILED_CRITICAL) {
        FailMission(false);
        return;
    }
    if (status == OBJECTIVE_INACTIVE)
        return;
    if (IsLocalGame()) {
        if (!AreObjectivesDecided())
            return;
        // All decided: the mission ends. (The message follows this objective's status.)
        m_roundState = ROUND_OVER;
        m_roundStateTime = m_missionTime;
#ifndef HD2_DEDICATED   // the server shows no message
        const bool completed = status == OBJECTIVE_COMPLETED && !m_missionFailed;
        m_interface->ShowMessage(completed ? TEXT_MISSION_COMPLETED : TEXT_MISSION_FAILED);
#endif
        return;
    }
    if (!IsClient())
        return;
    const S_MpObjective *target = GetMpObjective(m_mission, objective - 1);
    if (!target)
        return;
    // The icon shows the team it went to.
    if (status == OBJECTIVE_COMPLETED)
        m_interface->SetObjectiveIcon(objective - 1, target->team);
    else
        m_interface->SetObjectiveIcon(objective - 1, OtherTeam(target->team));
}

void C_game::FailMission(bool deadlyArea)           // 0x0069c1a0 (DS 0x006dcf30)
{
    m_missionFailed = true;
    m_roundState = ROUND_OVER;
    m_roundStateTime = m_missionTime;
#ifdef HD2_DEDICATED   // the server shows no message
    (void)deadlyArea;
#else
    m_interface->ShowMessage(deadlyArea ? TEXT_DEADLY_AREA : TEXT_MISSION_FAILED);
#endif
    // Fix: the original reads the objectives of a missing mission.
    if (!m_mission)
        return;
    for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
        S_MissionObjective *target = m_mission->GetObjective(i);
        if (target->status == OBJECTIVE_OPEN)
            target->status = OBJECTIVE_FAILED;
    }
}

void C_game::DecideObjectives(int winner)           // 0x006991c0
{
    for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
        if (m_mission->GetObjective(i)->status == OBJECTIVE_OPEN)
            SetObjectiveStatus(i + 1, GetAttackingTeam() != winner ? OBJECTIVE_FAILED : OBJECTIVE_COMPLETED);
    }
}

void C_game::WriteObjectiveStates(C_BitStream *msg) const   // 0x00699480
{
    for (int i = 0; i < m_mission->GetObjectiveCount(); ++i)
        msg->WriteBits(m_mission->GetObjective(i)->status, 4);
}

void C_game::ReadObjectiveStates(C_BitStream *msg)  // 0x006994f0
{
    for (int i = 0; i < m_mission->GetObjectiveCount();) {
        ++i;
        ApplyObjectiveStatus(i, msg->ReadBits(4));
    }
}

// ---------------------------------------------------------------------------
// Rounds (server)

void C_game::CheckRoundEnd(int timeDelta)           // 0x00699e20
{
    g_roundCheckTimer += timeDelta;
    if (g_roundCheckTimer < 1000)
        return;
    if (m_roundState == ROUND_OVER || m_roundState == ROUND_INTERMISSION || m_roundState == ROUND_WARMUP)
        return;
    g_roundCheckTimer = 0;
    const S_MultiplayerSettings &settings = *g_mpSettings;
    const bool timeUp = settings.roundLimit != 0 && m_roundTime >= settings.roundLimit * 60000;
    int winner = 0;
    switch (settings.gameType) {
    case MP_GAME_DEATHMATCH:
        if ((g_netGame.GetBestScore() >= settings.pointLimit && settings.pointLimit != 0) || timeUp) {
            m_roundState = ROUND_INTERMISSION;
            m_roundStateTime = m_missionTime;
            g_netGame.SendScores();
            GetGameApp()->SendGameMessage(GAME_MSG_SCORES, 0, 0, 0, false);
            GetGameApp()->SendGameMessage(GAME_MSG_ROUND_OVER, 0, 0, 0, false);
        }
        break;

    case MP_GAME_OCCUPATION: {
        // The zones each team holds.
        int zones = 0;
        int teamZones[3] = {0, 0, 0};
        for (int id = 0; id < g_actorsModule.GetActorCount(); ++id) {
            C_Actor *actor = g_actorsModule.GetActorById(id);
            if (actor && actor->GetType() == ACTOR_MP_ZONE) {
                ++zones;
                ++teamZones[static_cast<C_MPZone *>(actor)->GetTeam()];
            }
        }
        const int axisScore = static_cast<int>(g_netGame.GetTeamScore(0));
        const int alliedScore = static_cast<int>(g_netGame.GetTeamScore(1));
        const bool pointsReached = settings.pointLimit != 0 &&
                                   (axisScore >= settings.pointLimit || alliedScore >= settings.pointLimit);
        if (!pointsReached && !timeUp && (g_netGame.IsTeamAlive(0) || teamZones[0] != 0) &&
            (g_netGame.IsTeamAlive(1) || teamZones[1] != 0) && zones != teamZones[0] && zones != teamZones[1])
            break;
        if (zones == teamZones[1] || (static_cast<int>(g_netGame.GetTeamScore(0)) <
                                          static_cast<int>(g_netGame.GetTeamScore(1)) &&
                                      teamZones[1] != 0))
            winner = 1;
        m_roundState = ROUND_OVER;
        m_roundStateTime = m_missionTime;
        ++TeamWins(winner);
        g_netGame.SendScores();
        AnnounceRoundEnd(winner);
        break;
    }

    case MP_GAME_OBJECTIVES: {
        const bool axisAlive = g_netGame.IsTeamAlive(0);
        const bool alliedAlive = g_netGame.IsTeamAlive(1);
        int axisPlayers = 0;
        int alliedPlayers = 0;
        g_netGame.CountTeamPlayers(&axisPlayers, &alliedPlayers);
        // A team with players of whom none is alive loses; so does the attacker whose time
        // is up or the side the objectives went against.
        if (!timeUp && !AreObjectivesDecided() && (axisAlive || axisPlayers == 0) && (alliedAlive || alliedPlayers == 0))
            break;
        const int attackers = GetAttackingTeam();
        winner = OtherTeam(attackers);
        if (!axisAlive)
            winner = 1;
        if (!alliedAlive)
            winner = 0;
        if (AreObjectivesDecided()) {
            int completed = 0;
            for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
                if (m_mission->GetObjective(i)->status == OBJECTIVE_COMPLETED)
                    ++completed;
            }
            winner = completed == m_mission->GetObjectiveCount() ? attackers : OtherTeam(attackers);
        }
        if (winner == attackers) {
            // Bonus for wiping the other team out.
            if (!axisAlive || !alliedAlive)
                AwardTeamPoints(2, winner);
        } else {
            // The defenders get the points of the objectives they kept.
            for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
                if (m_mission->GetObjective(i)->status == OBJECTIVE_OPEN)
                    AwardTeamPoints(GetMpObjective(m_mission, i)->points, winner);
            }
        }
        m_roundState = ROUND_OVER;
        m_roundStateTime = m_missionTime;
        ++TeamWins(winner);
        DecideObjectives(winner);
        g_netGame.SendScores();
        AnnounceRoundEnd(winner);
        break;
    }

    case MP_GAME_COOPERATIVE: {
        const bool axisAlive = g_netGame.IsTeamAlive(0);
        const bool alliedAlive = g_netGame.IsTeamAlive(1);
        bool livesLeft = false;
        const int lives = settings.coopLives;
        S_NetPlayerState *players = g_netGame.LockPlayers();
        const int count = g_netGame.GetPlayerCount();
        for (int slot = 0; slot < count; ++slot) {
            if (players[slot].IsConnected() && players[slot].human && players[slot].livesUsed < lives)
                livesLeft = true;
        }
        g_netGame.UnlockPlayers();
        int axisPlayers = 0;
        int alliedPlayers = 0;
        g_netGame.CountTeamPlayers(&axisPlayers, &alliedPlayers);
        if (!AreObjectivesDecided()) {
            // The round goes on while anyone lives or may respawn.
            if (axisAlive || alliedAlive)
                break;
            const bool teamLivesUsedUp = m_teamLivesUsed >= lives && lives != -1 && settings.teamLives != 0;
            if (!teamLivesUsedUp) {
                if (settings.teamLives != 0 || lives == -1 || livesLeft)
                    break;
                if (axisPlayers == 0 && alliedPlayers == 0)
                    break;
            }
        }
        // Won when the objectives are all decided (the mission's last ones).
        const int won = AreObjectivesDecided() ? 1 : 0;
        m_roundState = ROUND_INTERMISSION;
        m_roundStateTime = m_missionTime;
        DecideObjectives(won);
        g_netGame.SendScores();
        AnnounceRoundEnd(won);
        break;
    }

    default:
        break;
    }
    ++g_roundChecks;
    // (Dead code: the timer is 0 here.)
    if (g_roundCheckTimer >= 300 && m_roundState == ROUND_PLAYING)
        g_roundCheckTimer = 0;
}

void C_game::AnnounceRoundEnd(int winner)           // 0x00699630 (DS 0x006da370)
{
    if (!IsClient()) {
        // The dedicated server's console.
        PrintMessage(MESSAGE_WHITE, g_texts.Get(winner != 0 ? TEXT_WINS_ALLIED : TEXT_WINS_AXIS));
        const C_str alliedText = g_texts.Get(TEXT_SCORE_ALLIED);
        const C_str axisText = g_texts.Get(TEXT_SCORE_AXIS);
        PrintMessage(MESSAGE_WHITE, "%s%d %s%d", static_cast<const char *>(axisText), TeamWins(0),
                     static_cast<const char *>(alliedText), TeamWins(1));
    }
    GetGameApp()->SendGameMessage(GAME_MSG_SCORES, 0, 0, 0, false);
    GetGameApp()->SendGameMessage(GAME_MSG_ROUND_OVER, 0, winner, 0, false);
}

void C_game::ChooseVip()                            // 0x00699950
{
    if (g_mpSettings->gameType != MP_GAME_OBJECTIVES)
        return;
    const int attackers = GetAttackingTeam();
    int axis = 0;
    int allied = 0;
    g_netGame.CountTeams(&axis, &allied, true, true);
    const int defenders = attackers == 1 ? axis : allied;
    const int range = defenders + 1;
    int pick = static_cast<int>(std::floor(static_cast<double>(static_cast<float>(std::rand()) * 3.051851e-05f *
                                                               static_cast<float>(range))));
    if (range < pick)
        pick = range;
    // The pick-th defender in the game.
    S_NetPlayerState *players = g_netGame.LockPlayers();
    const int count = g_netGame.GetPlayerCount();
    for (int slot = 0; slot < count; ++slot) {
        if (players[slot].connection != 0 && players[slot].pendingTeam != attackers && players[slot].human &&
            --pick < 1) {
            m_vipSlot = slot;
            break;
        }
    }
    g_netGame.UnlockPlayers();
    m_vipChosen = false;
}

void C_game::StartWarmup()                          // 0x00699b60
{
    g_netGame.ApplyPendingTeams();
    FreezePlayers(true);
    if (g_mpSettings->autoTeamBalance)
        g_netGame.BalanceTeams(GetMpMission(m_mission)->GetBalanceAxis(), GetMpMission(m_mission)->GetBalanceAllied());
    m_roundState = ROUND_WARMUP;
    m_roundStateTime = m_missionTime;
    GetGameApp()->SendGameMessage(GAME_MSG_WARMUP_STARTED, 0, 0, 0, false);
}

void C_game::StartNextRound()                       // 0x00699a80
{
    RestartMission(true);
    SendToPlayers(GAME_MSG_TEMP_ACTORS);
    GetGameApp()->SendGameMessage(GAME_MSG_SESSION, 0, 0, 0, true);
    g_netGame.Process(1000, false);
    g_netGame.FlipSessionParity();
    g_netGame.ResetReplication(true);
}

void C_game::CheckPlayerAreas(int timeDelta)        // 0x0069f1a0 (DS 0x006e0700)
{
    g_areaCheckTimer += timeDelta;
    if (g_areaCheckTimer < 500 || m_roundState == ROUND_OVER || m_roundState == ROUND_INTERMISSION ||
        m_roundState == ROUND_WARMUP)
        return;
    g_areaCheckTimer = 0;
    if (IsServer()) {
        // Players in a deadly area die.
        S_NetPlayerState *players = g_netGame.LockPlayers();
        const int count = g_netGame.GetPlayerCount();
        for (int slot = 0; slot < count; ++slot) {
            C_Human *human = players[slot].human;
            if (!players[slot].IsConnected() || !human)
                continue;
            if (human->GetEnvironmentFlags() & HUMAN_ENV_DEADLY) {
                // (The original passes two uninitialised vectors.)
                const S_vector pos = {0.0f, 0.0f, 0.0f};
                const S_vector dir = {0.0f, 0.0f, 0.0f};
                human->Hit(HIT_SUICIDE, -1.0f, pos, dir, human, 0);
            }
        }
        g_netGame.UnlockPlayers();
    }
#ifndef HD2_DEDICATED   // no interface on the server
    if (IsClient()) {
        // The local player is told about restricted areas.
        C_Human *human = g_netGame.GetLocalHuman();
        if (human) {
            if (human->GetEnvironmentFlags() & HUMAN_ENV_RESTRICTED) {
                m_inRestrictedArea = true;
                GetGame()->GetInterface()->ShowMessage(TEXT_RESTRICTED_AREA);
            }
            if (!(human->GetEnvironmentFlags() & HUMAN_ENV_RESTRICTED) && m_inRestrictedArea) {
                m_inRestrictedArea = false;
                GetGame()->GetInterface()->ShowMessage(-1);
            }
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Rounds (client)

void C_game::OnRoundOver(int winner)                // 0x00699860
{
    if (g_mpSettings->gameType != MP_GAME_DEATHMATCH && g_mpSettings->gameType != MP_GAME_COOPERATIVE) {
        m_roundState = ROUND_OVER;
        m_interface->ShowRespawnScreen(false);
        m_interface->ShowRoundResult(TEXT_TEAM_WINS + (winner != 0), TeamWins(0), TeamWins(1));
        m_roundStateTime = m_missionTime;
        return;
    }
    m_roundState = ROUND_INTERMISSION;
    m_interface->ShowScores(true);
    m_interface->ShowMessage(-1);
    m_roundStateTime = m_missionTime;
}

void C_game::OnRespawnScreen(bool unknown)          // 0x006998f0
{
    if (m_roundState != ROUND_PLAYING && m_roundState != ROUND_WARMUP)
        return;
    m_respawnRequest = unknown ? 1 : 2;
    // The respawn screen picks the zone anew.
    const std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
    S_SoldierState *state = squad.empty() ? nullptr : squad[0];
    state->spawnZone = -1;
    m_interface->ShowRespawnScreen(true);
}

void C_game::OnWarmupStarted()                      // 0x00699ae0
{
    m_interface->ShowScores(false);
    m_interface->ShowMessage(TEXT_WARMUP);
    FreezePlayers(true);
    C_Human *human = g_netGame.GetLocalHuman();
    if (human) {
        g_teamManager.GetPlayerTeam()->SelectMember(human);
        human->GetController()->SetSpectating(true);
    }
    SetCameraMode(GAME_CAMERA_FREE);
    m_roundState = ROUND_WARMUP;
    m_roundStateTime = m_missionTime;
}

void C_game::OnWarmupFinished()                     // 0x00699bd0
{
    m_roundTime = 0;
    FreezePlayers(false);
    m_interface->ShowMessage(-1);
    C_Human *human = g_netGame.GetPlayerHuman(g_localPlayerSlot);
    if (human)
        human->GetController()->SetSpectating(false);
    m_roundState = ROUND_PLAYING;
    m_cameraMode = GAME_CAMERA_OWN;
    if (IsClient() && g_netGame.GetLocalHuman()) {
        const std::vector<S_SquadSoldier *> &squad = g_missionSquad->states;
        const S_SoldierState *state = squad.empty() ? nullptr : squad[0];
        if (state->uniform.typeId != -1 && state->uniform.present == 1) {
            GetGameApp()->PostCommand(APP_NET_COMMAND_RESPAWN, 0, 0, false);
            Print("Warmup finished");
            return;
        }
        // Otherwise the player picks a uniform in the in-game menu.
        C_GameApp *app = GetGameApp();
        if (app->m_state->GetTypeId() != GAME_STATE_TYPE_GAME_MENU) {
            app->m_state = static_cast<C_game *>(app->m_state)->OpenGameMenu(-2);
            // (The original opens a second menu over this game and drops it.)
            OpenGameMenu(-2);
        }
        static_cast<C_GameMenu *>(app->m_state)->SetRespawnOnApply(true);
    }
    Print("Warmup finished");
}

void C_game::OnSessionClosing(int reason)           // 0x00699cf0
{
    m_roundState = ROUND_CLOSING;
    m_roundStateTime = m_missionTime;
    if (reason >= 0 && reason < 4)
        m_interface->ShowMessage(SESSION_CLOSING_TEXTS[reason]);
}

int C_game::SetRoundTime(int ms)                    // 0x0069b1a0
{
    m_roundTime = ms;
    m_respawnWaveTime = ms % 1000 + (m_respawnWaveTime / 1000) * 1000;
    return ms / 1000;
}

int C_game::GetRoundTimeLeft() const                // 0x0069b150
{
    const int left = g_mpSettings->roundLimit * 60000 - m_roundTime;
    return left < 0 ? 0 : left;
}

int C_game::GetRespawnTimeLeft() const              // 0x0069b180
{
    return g_mpSettings->respawnTime * 1000 - m_respawnWaveTime;
}

int C_game::GetVipSlot() const                      // 0x0069e340
{
    if (m_hasVipObjective && m_vipSlot != -1 && g_actorsModule.GetActorById(m_vipSlot))
        return m_vipSlot;
    return -1;
}

// ---------------------------------------------------------------------------
// Spawning

void C_game::RespawnPlayer(int slot, const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state,
                           const int *stats)        // 0x0069da50
{
    S_NetPlayerState *players = g_netGame.LockPlayers();
    if (!IsServer() && g_mpSettings->gameType == MP_GAME_COOPERATIVE) {
        if (g_mpSettings->teamLives == 0)
            ++players[slot].livesUsed;
        else
            ++m_teamLivesUsed;
    }
    if (slot < g_netGame.GetPlayerCount()) {
        C_Human *human = players[slot].human;
        if (human && players[slot].IsConnected()) {
            human->Respawn(pos, dir, state, stats);
            if (g_mpSettings->gameType == MP_GAME_OBJECTIVES) {
                const int type = human->GetControllerType();
                if (type == HUMAN_CONTROLLER_PLAYER)
                    static_cast<C_PlayerController *>(human->GetController())->SetVip(state->vip);
                else if (type == HUMAN_CONTROLLER_REMOTE)
                    static_cast<C_RemoteController *>(human->GetController())->SetVip(state->vip);
            }
            g_netGame.UnlockPlayers();
            if (g_gameProfile.GetDetailSettings()->IsVoiceChatEnabled())
                g_netGame.UpdateVoiceTeam();
            if (slot != g_localPlayerSlot)
                return;
            m_respawnRequest = 0;
            m_interface->ShowRespawnScreen(false);
            return;
        }
    }
    g_netGame.UnlockPlayers();
}

void C_game::OnRespawnRequest(int slot, S_SoldierState *state, int *stats)   // 0x0069dc90
{
    S_vector4 pos;
    S_vector4 dir;
    switch (g_mpSettings->gameType) {
    case MP_GAME_DEATHMATCH: {
        // A random spawn point nobody stands on (49 tries).
        for (int tries = 49; tries > 0; --tries) {
            const int team = g_mpSettings->gameType == MP_GAME_DEATHMATCH ? 0 : state->team;
            C_Actor *spawnPoint = g_actorsModule.FindRandomSpawnPoint(!IsLocalGame(), team);
            pos = spawnPoint->GetPosition4();
            dir = spawnPoint->GetDirection4();
            if (!IsSpawnBlocked(pos))
                break;
        }
        state->team = 0;
        break;
    }
    case MP_GAME_OCCUPATION:
    case MP_GAME_OBJECTIVES:
    case MP_GAME_COOPERATIVE:
        if (g_mpSettings->gameType == MP_GAME_COOPERATIVE && g_mpSettings->spawnOnStart)
            state->spawnZone = FindZoneByNumber(0);
        {
            C_Actor *actor = g_actorsModule.GetActor(state->spawnZone);
            C_MPZone *zone = static_cast<C_MPZone *>(actor);
            if (!actor || zone->GetTeam() != state->team ||
                !zone->PickSpawnPosition(&pos, &dir, g_mpSettings->allowRespawn != 0)) {
                actor = g_actorsModule.GetActor(FindOnlyTeamZone(state->team));
                zone = static_cast<C_MPZone *>(actor);
                if (!actor || !zone->PickSpawnPosition(&pos, &dir, g_mpSettings->allowRespawn != 0)) {
                    GetGameApp()->SendGameMessage(GAME_MSG_SPAWN_FAILED, 0, slot, 0, false);
                    delete state;
                    delete[] stats;
                    return;
                }
                state->spawnZone = actor->GetNetId();
            }
        }
        if (g_gameProfile.IsCooperativeGame()) {
            // The lives of the player / of the team.
            S_NetPlayerState *players = g_netGame.LockPlayers();
            const int lives = g_mpSettings->coopLives;
            int &used = g_mpSettings->teamLives == 0 ? players[slot].livesUsed : m_teamLivesUsed;
            if (used >= lives && lives != -1) {
                g_netGame.UnlockPlayers();
                // Fix: the original keeps neither the state nor the stats and leaks them.
                delete state;
                delete[] stats;
                return;
            }
            ++used;
            g_netGame.UnlockPlayers();
        }
        if (g_mpSettings->gameType == MP_GAME_OBJECTIVES) {
            // The first defender to spawn after the warmup becomes the VIP.
            const int attackers = GetAttackingTeam();
            state->vip = false;
            if (slot == m_vipSlot && state->team == attackers)
                m_vipSlot = -1;
            if (m_roundState != ROUND_WARMUP && m_hasVipObjective && !m_vipChosen &&
                (slot == m_vipSlot || m_vipSlot == -1) && state->team != attackers) {
                m_vipSlot = slot;
                state->vip = true;
                m_vipChosen = true;
                for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
                    const S_MpObjective *target = GetMpObjective(m_mission, i);
                    if (target->type == OBJECTIVE_TYPE_VIP) {
                        state->uniform.typeId = target->vipUniform;
                        break;
                    }
                }
            }
        }
        if (g_mpSettings->gameType == MP_GAME_OCCUPATION)
            FaceTargetZone(&dir, pos, state->team);
        break;
    default:
        pos = {0.0f, 0.0f, 0.0f, 0.0f};
        dir = {0.0f, 0.0f, 1.0f, 0.0f};
        break;
    }
    if (!IsLocalGame())
        GetGame()->RespawnPlayer(slot, pos, dir, state, stats);
    // The clients hear of it at the next frame (GAME_MSG_SPAWN frees it).
    S_PendingSpawn *spawn = static_cast<S_PendingSpawn *>(_aligned_malloc(sizeof(S_PendingSpawn), 16));
    spawn->slot = slot;
    spawn->pos = pos;
    spawn->dir = dir;
    spawn->state = state;
    spawn->stats = stats;
    m_pendingSpawns.push_back(spawn);
}

void C_game::OnPlayerJoined(int slot, S_SoldierState *state, int *stats)   // 0x0069e610
{
    if (!IsLocalGame() && g_actorsModule.GetActor(slot))
        return;
    switch (g_mpSettings->gameType) {
    case MP_GAME_DEATHMATCH:
        state->team = 0;
        break;
    case MP_GAME_OCCUPATION:
    case MP_GAME_COOPERATIVE: {
        if (g_mpSettings->spawnOnStart && g_gameProfile.IsCooperativeGame())
            state->spawnZone = FindZoneByNumber(0);
        C_Actor *zone = g_actorsModule.GetActor(state->spawnZone);
        if (!zone || static_cast<C_MPZone *>(zone)->GetTeam() != state->team) {
            zone = g_actorsModule.GetActor(FindOnlyTeamZone(state->team));
            if (!zone) {
                GetGameApp()->SendGameMessage(GAME_MSG_SPAWN_FAILED, 0, slot, 1, false);
                return;
            }
            state->spawnZone = zone->GetNetId();
        }
        g_netGame.SetPlayerTeam(slot, state->team, false);
        break;
    }
    case MP_GAME_OBJECTIVES: {
        C_Actor *zone = g_actorsModule.GetActor(state->spawnZone);
        if (!zone || static_cast<C_MPZone *>(zone)->GetTeam() != state->team) {
            GetGameApp()->SendGameMessage(GAME_MSG_SPAWN_FAILED, 0, slot, 1, false);
            return;
        }
        // A late joiner of a running round only watches.
        if (m_roundState != ROUND_WARMUP && m_roundTime > 30000 && g_netGame.IsTeamAlive(state->team) &&
            !g_mpSettings->allowRespawn)
            state->spectating = true;
        const int attackers = GetAttackingTeam();
        state->vip = false;
        if (slot == m_vipSlot && state->team == attackers)
            m_vipSlot = -1;
        if (m_roundState != ROUND_WARMUP && m_hasVipObjective && !m_vipChosen &&
            (slot == m_vipSlot || m_vipSlot == -1) && state->team != attackers) {
            m_vipSlot = slot;
            state->vip = true;
            m_vipChosen = true;
            for (int i = 0; i < m_mission->GetObjectiveCount(); ++i) {
                const S_MpObjective *target = GetMpObjective(m_mission, i);
                if (target->type == OBJECTIVE_TYPE_VIP) {
                    state->uniform.typeId = target->vipUniform;
                    break;
                }
            }
        }
        g_netGame.SetPlayerTeam(slot, state->team, false);
        break;
    }
    default:
        break;
    }
    if (!IsLocalGame()) {
        C_Actor *spawnPoint = g_actorsModule.FindSpawnPoint(1);
        if (spawnPoint)
            static_cast<C_SpawnPoint *>(spawnPoint)->Spawn(slot, state, stats);
    }
    if (slot == g_localPlayerSlot)
        m_interface->ShowRespawnScreen(false);
}

void C_game::OnPlayerSpawned(int slot, C_Human *human)   // 0x0069e560 (DS 0x006df940)
{
    if (slot != g_localPlayerSlot)
        return;
    if (m_roundState == ROUND_WARMUP) {
        human->GetController()->SetSpectating(true);
        SetCameraMode(GAME_CAMERA_FREE);
    }
#ifndef HD2_DEDICATED   // no interface on the server
    else {
        GetGame()->GetInterface()->SetScreen(0);
        GetGame()->GetInterface()->ShowSquadMember(nullptr);
    }
#endif
    m_respawnRequest = 0;
#ifndef HD2_DEDICATED   // no interface on the server
    m_interface->ShowRespawnScreen(false);
#endif
    if (g_mpSettings->gameType == MP_GAME_OBJECTIVES && human->IsSpectating()) {
#ifndef HD2_DEDICATED   // no interface on the server (the test is still made)
        GetGame()->GetInterface()->ShowTimer(TEXT_SPECTATING, 5000);
#endif
    }
}

void C_game::OnPlayerLeft(int connection)           // 0x0069e9f0 (DS 0x006dfe70)
{
    S_NetPlayerState *players = g_netGame.LockPlayers();
    int slot = -1;
    for (int i = g_netGame.GetPlayerCount(); i-- > 0;) {
        if (players[i].connection == connection) {
            players[i].connection = 0;
            players[i].name[0] = 0;
            slot = i;
            break;
        }
    }
    if (slot != -1 && players[slot].human) {
        S_NetPlayerState &player = players[slot];
        player.left = true;
        if (m_vipSlot == slot)
            m_vipSlot = -1;
#ifndef HD2_DEDICATED   // the server has only the server's side
        if (!IsLocalGame() && IsClient()) {
            if (m_interface->GetShownSquadMember() == player.human)
                m_interface->ShowSquadMember(nullptr);
            if (g_teamManager.GetPlayerTeam()->GetWatchedHuman() == player.human)
                g_teamManager.GetPlayerTeam()->SetWatchedHuman(nullptr);
            if (g_netGame.GetLocalHuman() && g_netGame.GetLocalHuman()->GetController() && m_spectatedSlot == slot)
                m_spectatedSlot = -1;
        }
#endif
        if (IsServer() && g_gameProfile.IsCooperativeGame())
            g_teamManager.GetCoopTeam()->RemoveMember(player.human);
        g_temporaryActors.DestroyMinesOf(player.human);
        g_actorsModule.DestroyActor(player.human, false);
        player.human = nullptr;
        std::memset(player.stats, 0, sizeof(player.stats));
    }
    g_netGame.UnlockPlayers();
}

// ---------------------------------------------------------------------------
// Spectator camera

void C_game::SetCameraMode(int mode)                // 0x0069ad20 (DS 0x006dbd80)
{
    m_cameraMode = mode;
    if (mode < 0)
        return;
    if (mode < GAME_CAMERA_FREE) {
#ifndef HD2_DEDICATED   // no interface on the server
        GetGame()->GetInterface()->SetScreen(1);
#endif
        m_spectatedSlot = -1;
    } else if (mode == GAME_CAMERA_FREE) {
#ifndef HD2_DEDICATED   // no interface on the server
        GetGame()->GetInterface()->SetScreen(mode);
#endif
        ::GetCameraModule()->SetSpecialMode(2, true, 1);
    }
}

void C_game::UpdateSpectatorCamera(float *, float *pressed)   // 0x0069ad80 (DS 0x006dbdd0)
{
    C_Human *local = g_netGame.GetLocalHuman();
    C_HumanController *controller = local ? local->GetController() : nullptr;
    if (!controller || m_roundState == ROUND_WARMUP)
        m_cameraMode = GAME_CAMERA_FREE;
    else if (m_cameraMode == GAME_CAMERA_FREE)
        m_cameraMode = GAME_CAMERA_FOLLOW;

    if (((controller && controller->IsSpectating()) || m_cameraMode == GAME_CAMERA_FREE) && IsClient()) {
        // The next player to follow: the slots after the current one (all 32 when none).
        if ((m_spectatedSlot == -1 || (pressed && pressed[INPUT_NEXT_PLAYER] != 0.0f)) &&
            m_cameraMode != GAME_CAMERA_FREE) {
            int next = m_spectatedSlot + 1;
            if ((next & 31) != m_spectatedSlot) {
                for (;;) {
                    C_Human *candidate = static_cast<C_Human *>(g_actorsModule.GetActorById(next & 31));
                    if (candidate && candidate != g_netGame.GetLocalHuman() &&
                        candidate->IsAlive() && !g_netGame.IsPlayerSpectating(candidate->GetNetId()) &&
                        candidate->IsInPlay() && IsTeamGame() &&
                        (g_netGame.GetLocalHuman()->GetTeam(false) == candidate->GetTeam(false) ||
                         g_netGame.GetLocalHuman()->GetTeam(false) == TEAM_NONE)) {
                        g_teamManager.GetPlayerTeam()->SetWatchedHuman(candidate);
                        GetGame()->ShowFirstPersonView(false);
                        GetGame()->GetInterface()->SetScreen(1);
                        GetGame()->GetInterface()->ShowSquadMember(candidate);
                        m_spectatedSlot = next & 31;
                        break;
                    }
                    if (m_spectatedSlot == -1 && next == 32)
                        break;
                    ++next;
                    if ((next & 31) == m_spectatedSlot)
                        break;
                }
            }
        }
        // Cycles the camera modes (the free camera only for spectators).
        if (controller && pressed && pressed[INPUT_CAMERA_MODE] != 0.0f &&
            GetGame()->m_roundState != ROUND_WARMUP) {
            m_cameraMode = (m_cameraMode + 1) % 3;
            if (m_cameraMode == GAME_CAMERA_FREE) {
                if (g_netGame.GetLocalHuman()->GetTeam(false) == TEAM_NONE)
                    ::GetCameraModule()->SetSpecialMode(2, true, 1);
                else
                    m_cameraMode = (m_cameraMode + 1) % 3;
            }
        }
        if (m_cameraMode == GAME_CAMERA_FREE && pressed && pressed[INPUT_NEXT_PLAYER] != 0.0f)
            ::GetCameraModule()->SetSpecialMode(8, true, 1);
        return;
    }
    // A dead player may switch to watching the others after 3 s.
    if (controller && (controller->GetMoveFlags() & MOVE_DEAD) && !IsLocalGame() && !controller->IsSpectating() &&
        pressed && pressed[INPUT_NEXT_PLAYER] != 0.0f && g_mpSettings->gameType != MP_GAME_DEATHMATCH &&
        local->GetDeadTime() > 3000) {
        controller->SetSpectating(true);
        m_spectatedSlot = -1;
        m_cameraMode = GAME_CAMERA_FOLLOW;
    }
}
