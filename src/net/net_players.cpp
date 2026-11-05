// The network game's players, teams and scores (part 10d-ii): the kills scored, the ranks, the kicks, the scores'
// messages, the teams counted and balanced, a server's status for a query, the net library's quantisation table.
// See net_game.h.
#include <winsock2.h>

#include "net/net_game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/actors_module.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/perf_timer.h"
#include "game/profile.h"
#include "ls3d/math.h"
#include "net/dplay8.h"
#include "net/net_channel.h"
#include "net/net_object.h"
#include "net/net_transport.h"

C_NetQuantizationTable g_netQuantizations;           // 0x00982de8

namespace {

// The scores a rank needs (rank n: the first n reached; 0x0084507c).
const int g_rankScores[8] = {10, 20, 30, 40, 50, 60, 70, 80};

// The scores message's end of the players (6 bits).
constexpr uint32_t kScoresEnd = 0x21;
// The co-op lives sent for the first six slots (7 bits; 0x7F: none used yet).
constexpr int kLivesSlots = 6;

// DirectPlay's DPN_CONNECTION_INFO (0x5C bytes): its size, the round trip, the rest not read.
struct S_ConnectionInfo {
    DWORD size;
    DWORD roundTripLatency;                          // ms
    DWORD rest[21];
};

}  // namespace

// ---- the stats in a message ------------------------------------------------------------------------------------------

void S_NetStats::Write(C_BitStream *stream)
{
    int *stats = values;
    int score = stats[NET_STAT_SCORE];
    if (score < 0)
        score += 0x800;
    stream->WriteBits(static_cast<uint32_t>(score), 11);
    stream->WriteBits(static_cast<uint32_t>(stats[NET_STAT_KILLS]), 10);
    stream->WriteBits(static_cast<uint32_t>(stats[NET_STAT_DEATHS]), 10);
    if (stats[NET_STAT_PING] > 0x3FF || stats[NET_STAT_PING] < 0)
        stats[NET_STAT_PING] = 0x3FF;
    stream->WriteBits(static_cast<uint32_t>(stats[NET_STAT_PING]), 10);
    stream->WriteBits(static_cast<uint32_t>(stats[NET_STAT_RANK]), 5);
}

void S_NetStats::Read(C_BitStream *stream)
{
    int *stats = values;
    const auto score = static_cast<int>(stream->ReadBits(11));
    stats[NET_STAT_SCORE] = score;
    if (score > 0x400)
        stats[NET_STAT_SCORE] = score - 0x800;
    stats[NET_STAT_KILLS] = static_cast<int>(stream->ReadBits(10));
    stats[NET_STAT_DEATHS] = static_cast<int>(stream->ReadBits(10));
    stats[NET_STAT_PING] = static_cast<int>(stream->ReadBits(10));
    stats[NET_STAT_RANK] = static_cast<int>(stream->ReadBits(5));
}

// ---- the player state --------------------------------------------------------------------------------------------------

bool S_NetPlayerState::IsSpectating() const
{
    if (!human)
        return true;
    const int type = human->GetControllerType();
    if (type != HUMAN_CONTROLLER_PLAYER && type != HUMAN_CONTROLLER_REMOTE)
        return false;
    return human->GetController()->IsSpectating();
}

C_str S_NetPlayerState::GetRankName() const
{
    int side = team;
    if (g_gameProfile.m_mpSettings->gameType == MP_GAME_DEATHMATCH && human) {
        // A deathmatch: the side of the uniform worn (an allied one: the allies' names).
        const C_Item *uniform = human->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
        side = (!uniform || uniform->GetDef()->GetTeam() != SIDE_ALLIES) ? 0 : 1;
    }
    return g_texts.Get((side != 1 ? 10 : 0) + 0x2792 + stats[NET_STAT_RANK]);
}

// ---- kills, ranks, kicks ---------------------------------------------------------------------------------------------

void C_NetGame::OnPlayerKilled(int victim, int killer)
{
    m_playersLock.Enter();
    const size_t count = m_players.size();
    if (static_cast<size_t>(victim) >= count || victim == -1) {
        // Not a player: in co-op its killer's score (an ally or a neutral killed: 10 off).
        // (Fix: the killer's slot checked with <; the original's <= let one past the end through.)
        if (g_gameProfile.IsCooperativeGame() && killer != -1 && static_cast<size_t>(killer) < count) {
            int *stats = m_players[static_cast<size_t>(killer)].stats;
            C_Actor *actor = g_actorsModule.GetActor(victim);
            const int role = actor ? static_cast<C_Human *>(actor)->GetRole() : 0;
            if (!actor || (role != HUMAN_ROLE_NPC_TEAM && role != HUMAN_ROLE_NEUTRAL && role != HUMAN_ROLE_NPC_TEAM_5)) {
                ++stats[NET_STAT_KILLS];
                ++stats[NET_STAT_TOTAL_KILLS];
                ++stats[NET_STAT_SCORE];
                ++stats[NET_STAT_TOTAL_SCORE];
            } else {
                stats[NET_STAT_TOTAL_SCORE] -= 10;
                stats[NET_STAT_SCORE] -= 10;
            }
            if (g_gameRole & GAME_ROLE_SERVER)
                SendPlayerScore(killer, false);
        }
        m_playersLock.Leave();
        return;
    }
    S_NetPlayerState &dead = m_players[static_cast<size_t>(victim)];
    ++dead.stats[NET_STAT_DEATHS];
    ++dead.stats[NET_STAT_TOTAL_DEATHS];
    ++m_teamStats[dead.team][NET_STAT_DEATHS];
    ++m_teamStats[dead.team][NET_STAT_TOTAL_DEATHS];
    if (killer == victim) {
        // A suicide: a point off.
        --dead.stats[NET_STAT_SCORE];
        --dead.stats[NET_STAT_TOTAL_SCORE];
        SendPlayerScore(victim, false);
    } else if (killer != -1 && static_cast<size_t>(killer) < m_players.size()) {
        S_NetPlayerState &player = m_players[static_cast<size_t>(killer)];
        C_Human *human = player.human;
        if (human && human->GetControllerType() == HUMAN_CONTROLLER_PLAYER) {
            if (dead.team != player.team || g_gameProfile.m_mpSettings->gameType == MP_GAME_DEATHMATCH) {
                ++player.stats[NET_STAT_KILLS];
                ++player.stats[NET_STAT_TOTAL_KILLS];
                ++player.stats[NET_STAT_SCORE];
                ++player.stats[NET_STAT_TOTAL_SCORE];
                ++m_teamStats[player.team][NET_STAT_KILLS];
                ++m_teamStats[player.team][NET_STAT_TOTAL_KILLS];
                ++m_teamStats[player.team][NET_STAT_SCORE];
                ++m_teamStats[player.team][NET_STAT_TOTAL_SCORE];
                if (g_gameProfile.m_mpSettings->gameType == MP_GAME_OBJECTIVES)
                    m_teamScores[player.team] = m_teamScores[player.team] + 1.0f;
                if (g_gameRole & GAME_ROLE_SERVER) {
                    SendPlayerScore(killer, false);
                    SendPlayerScore(victim, false);
                }
                m_playersLock.Leave();
                return;
            }
            // A team kill: 10 off the killer and its team; the third kicks it (the server; not its own player).
            player.stats[NET_STAT_SCORE] -= 10;
            player.stats[NET_STAT_TOTAL_SCORE] -= 10;
            ++player.teamKills;
            m_teamStats[player.team][NET_STAT_SCORE] -= 10;
            m_teamStats[player.team][NET_STAT_TOTAL_SCORE] -= 10;
            m_playersLock.Leave();
            if (!(g_gameRole & GAME_ROLE_SERVER) || player.teamKills < 3 || killer == g_localPlayerSlot)
                return;
            KickPlayer(killer, 1);
            return;
        }
    }
    m_playersLock.Leave();
}

void C_NetGame::KickPlayerByName(const char *name, int reason)
{
    m_playersLock.Enter();
    for (size_t i = 0; i < m_players.size(); ++i) {
        if (std::strcmp(m_players[i].name, name) == 0) {
            m_playersLock.Leave();
            KickPlayer(static_cast<int>(i), reason);
            return;
        }
    }
    m_playersLock.Leave();
}

void C_NetGame::KickPlayer(int slot, int reason)
{
    const S_NetPlayerState &player = m_players[static_cast<size_t>(slot)];
    if (player.connection == 0)
        return;
    if (g_gameRole & GAME_ROLE_SERVER)
        GetGameApp()->SendGameMessage(0x11, 0, reason, slot, false);
    char line[512];
    int text;
    switch (reason) {
    case 0:
        text = 0x22AB;
        break;
    case 1:
        text = 0x22AA;
        break;
    case 2:
        text = 0x22AD;
        break;
    case 3:
        text = 0x22AC;
        break;
    default:
        std::sprintf(line, "%s kicked because unknow(new?) reason", player.name);
        GetGameApp()->PrintMessage(line);
        return;
    }
    const C_str format = g_texts.Get(text);
    std::sprintf(line, format.c_str(), player.name);
    GetGameApp()->PrintMessage(line);
}

int *C_NetGame::GetPlayerStats(int slot)
{
    return m_players[static_cast<size_t>(slot)].stats;
}

int *C_NetGame::GetLocalPlayerStats()
{
    return m_players[static_cast<size_t>(g_localPlayerSlot)].stats;
}

void C_NetGame::SetPlayerRank(int rank, int slot)
{
    m_playersLock.Enter();
    S_NetPlayerState &player = m_players[static_cast<size_t>(slot)];
    const int previous = player.stats[NET_STAT_RANK];
    player.stats[NET_STAT_RANK] = rank;
    // Down: red; up (or the same): green.
    const bool down = rank < previous;
    const S_vector color = down ? S_vector(255.0f, 0.0f, 0.0f) : S_vector(0.0f, 255.0f, 0.0f);
    if (slot == g_localPlayerSlot) {
        const C_str text = g_texts.Get(down ? 0x22C4 : 0x22C5);
        GetGame()->PrintMessage(color, "%s %s", text.c_str(), player.GetRankName().c_str());
    } else {
        const C_str text = g_texts.Get(down ? 0x22C6 : 0x22C7);
        GetGame()->PrintMessage(color, "%s %s %s", player.name, text.c_str(), player.GetRankName().c_str());
    }
    m_playersLock.Leave();
}

void C_NetGame::SendPlayerScore(int slot, bool lock)
{
    if (!(g_gameRole & GAME_ROLE_SERVER))
        return;
    if (lock)
        m_playersLock.Enter();
    S_NetPlayerState &player = m_players[static_cast<size_t>(slot)];
    if (player.name[0] && player.human) {
        int rank = -1;
        do {
            if (rank > 6)
                break;
            ++rank;
        } while (g_rankScores[rank] <= player.stats[NET_STAT_SCORE]);
        auto *controller = static_cast<C_PlayerController *>(player.human->GetController());
        if (rank != player.stats[NET_STAT_RANK] && controller) {
            controller->SendSoldierClass(rank);
            player.stats[NET_STAT_RANK] = rank;
        }
    }
    if (lock)
        m_playersLock.Leave();
}

void C_NetGame::SendScores()
{
    m_playersLock.Enter();
    for (size_t i = 0; i < m_players.size(); ++i)
        SendPlayerScore(static_cast<int>(i), false);
    m_playersLock.Leave();
}

// ---- the scores' message ---------------------------------------------------------------------------------------------

void C_NetGame::WriteScores(C_BitStream *stream)
{
    if (!(g_gameRole & GAME_ROLE_SERVER))
        return;
    m_playersLock.Enter();
    // Each player with a human: its slot and stats (its ping from DirectPlay; the server's own player none).
    for (size_t i = 0; i < m_players.size(); ++i) {
        S_NetPlayerState &player = m_players[i];
        if (i == 0 && (g_gameRole & GAME_ROLE_CLIENT)) {
            player.stats[NET_STAT_PING] = 0;
        } else {
            S_ConnectionInfo info;
            info.size = sizeof info;
            m_server->GetConnectionInfo(static_cast<DPNID>(player.connection), &info, 0);
            player.stats[NET_STAT_PING] = static_cast<int>(info.roundTripLatency >> 1);
        }
        if (player.name[0] && player.human) {
            stream->WriteBits(static_cast<uint32_t>(i), 6);
            player.stats.Write(stream);
        }
    }
    stream->WriteBits(kScoresEnd, 6);
    C_game *game = GetGame();
    const int roundTime = game ? GetGame()->GetRoundTime() : 0;
    const uint32_t missionTime = GetGame() ? GetGame()->GetMissionTime() : 0;
    const int livesUsed = GetGame() ? GetGame()->GetTeamLivesUsed() : 0;
    stream->WriteBits(static_cast<uint32_t>(roundTime), 32);
    stream->WriteBits(missionTime, 32);
    if (g_gameProfile.IsCooperativeGame()) {
        if (!g_gameProfile.m_mpSettings->teamLives) {
            for (int i = 0; i < kLivesSlots; ++i) {
                // (-1, none used yet, goes as 0x7E: the reader takes only 0x7F for it.)
                auto lives = static_cast<uint32_t>(m_players[static_cast<size_t>(i)].livesUsed);
                if (lives > 0x7F)
                    lives = 0x7E;
                stream->WriteBits(lives, 7);
            }
        } else {
            stream->WriteBits(static_cast<uint32_t>(livesUsed), 16);
        }
    }
    m_playersLock.Leave();
    m_teamStats[0].Write(stream);
    m_teamStats[1].Write(stream);
    stream->WriteBits(static_cast<uint32_t>(static_cast<int>(m_teamScores[0])), 10);
    stream->WriteBits(static_cast<uint32_t>(static_cast<int>(m_teamScores[1])), 10);
    stream->WriteBits(static_cast<uint32_t>(m_teamWins[0]), 8);
    stream->WriteBits(static_cast<uint32_t>(m_teamWins[1]), 8);
}

void C_NetGame::ReadScores(C_BitStream *stream)
{
    // (The server reads into the scratch block: the message is its own.)
    S_NetStats scratch;
    const bool server = (g_gameRole & GAME_ROLE_SERVER) != 0;
    m_playersLock.Enter();
    for (uint32_t slot = stream->ReadBits(6); slot != kScoresEnd; slot = stream->ReadBits(6)) {
        S_NetStats *stats = &scratch;
        if (slot < m_players.size() && m_players[slot].name[0] && m_players[slot].human && !server)
            stats = &m_players[slot].stats;
        stats->Read(stream);
    }
    const uint32_t roundTime = stream->ReadBits(32);
    const uint32_t missionTime = stream->ReadBits(32);
    int livesUsed = 0;
    if (g_gameProfile.IsCooperativeGame()) {
        if (!g_gameProfile.m_mpSettings->teamLives) {
            for (int i = 0; i < kLivesSlots; ++i) {
                uint32_t lives = stream->ReadBits(7);
                if (!server) {
                    if (lives == 0x7F)
                        lives = static_cast<uint32_t>(-1);
                    m_players[static_cast<size_t>(i)].livesUsed = static_cast<int>(lives);
                }
            }
        } else {
            livesUsed = static_cast<int16_t>(stream->ReadBits(16));
        }
    }
    m_playersLock.Leave();
    if (GetGame() && missionTime != 0 && roundTime != 0 && !(g_gameRole & GAME_ROLE_SERVER)) {
        GetGame()->SetMissionClock(missionTime);
        GetGame()->SetRoundTime(static_cast<int>(roundTime));
        GetGame()->SetTeamLivesUsed(livesUsed);
    }
    for (int team = 0; team < 2; ++team)
        ((g_gameRole & GAME_ROLE_SERVER) ? scratch : m_teamStats[team]).Read(stream);
    for (int team = 0; team < 2; ++team) {
        const uint32_t score = stream->ReadBits(10);
        if (!(g_gameRole & GAME_ROLE_SERVER))
            m_teamScores[team] = static_cast<float>(static_cast<int>(score));
    }
    for (int team = 0; team < 2; ++team) {
        const uint32_t wins = stream->ReadBits(8);
        if (!(g_gameRole & GAME_ROLE_SERVER))
            m_teamWins[team] = static_cast<int>(wins);
    }
}

// ---- the level's quantisation ------------------------------------------------------------------------------------------

void C_NetGame::SetLevelBounds(const float *min, const float *size)
{
    if (!m_replication)
        return;
    S_NetQuantization *quantization = &m_quantizations[0];
    const S_vector4 step = {0.01f, 0.0013f, 0.01f, 0.0f};   // (w: unset in the original)
    quantization->Setup(*reinterpret_cast<const S_vector4 *>(size), *reinterpret_cast<const S_vector4 *>(min), step,
                        100);
    m_replication->SetReadQuantizations(quantization, nullptr, nullptr);
    m_replication->SetWriteQuantizations(quantization, nullptr, nullptr);
    g_netQuantizations.Set(0, size, min, &step.x);
    m_replication->Enable();
}

C_NetQuantizationTable::C_NetQuantizationTable() : m_entries(10)
{
    const float positionSpan[4] = {3000.0f, 3000.0f, 3000.0f, 0.0f};
    const float positionFrom[4] = {-1500.0f, -1500.0f, -1500.0f, 0.0f};
    const float positionStep[4] = {0.01f, 0.01f, 0.01f, 0.0f};
    Set(0, positionSpan, positionFrom, positionStep);
    const float angleSpan[4] = {6.29f, 6.29f, 6.29f, 0.0f};
    const float angleFrom[4] = {-3.145f, -3.145f, -3.145f, 0.0f};
    const float angleStep[4] = {0.025f, 0.025f, 0.025f, 0.0f};
    Set(1, angleSpan, angleFrom, angleStep);
    const float rateSpan[4] = {2.0f, 2.0f, 2.0f, 0.0f};
    const float rateFrom[4] = {-2.0f, -2.0f, -2.0f, 0.0f};
    const float rateStep[4] = {0.02f, 0.02f, 0.02f, 0.0f};
    Set(2, rateSpan, rateFrom, rateStep);
    const float spinStep[4] = {0.2f, 0.2f, 0.2f, 0.0f};
    Set(3, angleSpan, angleFrom, spinStep);
}

C_NetQuantizationTable::~C_NetQuantizationTable() = default;

void C_NetQuantizationTable::Set(int index, const float *span, const float *from, const float *step)
{
    S_NetQuantization quantization;
    quantization.Setup(*reinterpret_cast<const S_vector4 *>(span), *reinterpret_cast<const S_vector4 *>(from),
                       *reinterpret_cast<const S_vector4 *>(step), 100);
    // (Too small: doubled once.)
    if (static_cast<size_t>(index) >= m_entries.size())
        m_entries.resize(m_entries.size() * 2);
    m_entries[static_cast<size_t>(index)] = quantization;
}

// ---- the teams ---------------------------------------------------------------------------------------------------------

void C_NetGame::CountTeamPlayers(int *axis, int *allies)
{
    m_playersLock.Enter();
    *allies = 0;
    *axis = 0;
    for (const S_NetPlayerState &player : m_players) {
        if (!player.name[0] || !player.human)
            continue;
        if (player.team == 0)
            ++*axis;
        else if (player.team == 1)
            ++*allies;
    }
    m_playersLock.Leave();
}

void C_NetGame::CountTeams(int *axis, int *allies, bool pending, bool inGame)
{
    m_playersLock.Enter();
    *allies = 0;
    *axis = 0;
    for (const S_NetPlayerState &player : m_players) {
        if (!player.name[0])
            continue;
        const bool counted = !inGame || player.human;
        if (((player.team == 0 && !pending) || (player.pendingTeam == 0 && pending)) && counted)
            ++*axis;
        else if (((player.team == 1 && !pending) || (player.pendingTeam == 1 && pending)) && counted)
            ++*allies;
    }
    m_playersLock.Leave();
}

int C_NetGame::CountPlayers()
{
    m_playersLock.Enter();
    int count = 0;
    for (const S_NetPlayerState &player : m_players) {
        if (player.connection != 0 && player.name[0])
            ++count;
    }
    m_playersLock.Leave();
    return count;
}

void C_NetGame::ApplyPendingTeams()
{
    m_playersLock.Enter();
    for (S_NetPlayerState &player : m_players) {
        if (player.name[0])
            player.team = player.pendingTeam;
    }
    m_playersLock.Leave();
}

void C_NetGame::SetPlayerTeam(int slot, int team, bool pending)
{
    if (static_cast<size_t>(slot) >= m_players.size())
        return;
    m_playersLock.Enter();
    if (pending)
        m_players[static_cast<size_t>(slot)].pendingTeam = team;
    else
        m_players[static_cast<size_t>(slot)].team = team;
    m_playersLock.Leave();
}

bool C_NetGame::TeamsBalanced(int axis, int allied, bool pending)
{
    int axisCount;
    int alliedCount;
    CountTeams(&axisCount, &alliedCount, pending, false);
    const float off = std::fabs(static_cast<float>(axisCount) -
                                static_cast<float>(axis) / static_cast<float>(allied) * static_cast<float>(alliedCount));
    return off <= 1.5f;
}

bool C_NetGame::IsTeamAlive(int team)
{
    m_playersLock.Enter();
    for (const S_NetPlayerState &player : m_players) {
        C_Human *human = player.human;
        if (player.name[0] && human && player.team == team && human->GetHealth() > 0 && human->GetController() &&
            !human->GetController()->IsSpectating()) {
            m_playersLock.Leave();
            return true;
        }
    }
    m_playersLock.Leave();
    return false;
}

void C_NetGame::BalanceTeams(int, int percent)
{
    // `percent` of the players (truncated) against team 0's: moved one by one from the last slot while more than
    // one off.
    const int players = CountPlayers();
    int axis;
    int allies;
    const int wanted = static_cast<int>(static_cast<float>(players) * (static_cast<float>(percent) * 0.01f));
    CountTeams(&axis, &allies, false, false);
    int off = wanted - axis;
    if (off == 0 || off == -1 || off == 1)
        return;
    if (off > 0) {
        for (int i = static_cast<int>(m_players.size()) - 1; i >= 0; --i) {
            S_NetPlayerState &player = m_players[static_cast<size_t>(i)];
            if (player.name[0] && player.team == 1) {
                player.pendingTeam = 0;
                player.team = 0;
                if (--off < 1)
                    return;
            }
        }
    } else {
        for (int i = static_cast<int>(m_players.size()) - 1; i >= 0; --i) {
            S_NetPlayerState &player = m_players[static_cast<size_t>(i)];
            if (player.name[0] && player.team == 0) {
                player.pendingTeam = 1;
                player.team = 1;
                if (++off >= 0)
                    return;
            }
        }
    }
}

bool IsPlayerAdmin(int slot)
{
    const S_NetPlayerState *players = g_netGame.LockPlayers();
    if (players[slot].name[0]) {
        const bool admin = players[slot].admin;
        g_netGame.UnlockPlayers();
        return admin;
    }
    g_netGame.UnlockPlayers();
    return false;
}

C_Human *C_NetGame::GetLocalHuman()
{
    if (static_cast<size_t>(g_localPlayerSlot) < m_players.size())
        return m_players[static_cast<size_t>(g_localPlayerSlot)].human;
    return nullptr;
}

int C_NetGame::GetLocalTeam()
{
    return m_players[static_cast<size_t>(g_localPlayerSlot)].team;
}

bool C_NetGame::IsPlayerSpectating(int slot)
{
    if (static_cast<size_t>(slot) < m_players.size() && m_players[static_cast<size_t>(slot)].name[0])
        return m_players[static_cast<size_t>(slot)].IsSpectating();
    return false;
}

int C_NetGame::GetPlayerTeam(int slot, bool pending)
{
    if (static_cast<size_t>(slot) < m_players.size()) {
        if (pending)
            return m_players[static_cast<size_t>(slot)].pendingTeam;
        return m_players[static_cast<size_t>(slot)].team;
    }
    return 2;
}

bool CanPlayerRespawn(int slot)
{
    const S_MultiplayerSettings &settings = *g_gameProfile.m_mpSettings;
    if (!g_gameProfile.IsCooperativeGame())
        return settings.allowRespawn != 0;
    const S_NetPlayerState *players = g_netGame.LockPlayers();
    const int lives = settings.coopLives;
    const int used = settings.teamLives == 0 ? players[slot].livesUsed : GetGame()->GetTeamLivesUsed();
    if (used >= lives && lives != -1) {
        g_netGame.UnlockPlayers();
        return false;
    }
    g_netGame.UnlockPlayers();
    return true;
}

void C_NetGame::AddTeamScore(int team, float points)
{
    if (g_gameRole & GAME_ROLE_SERVER)
        m_teamScores[team] = points + m_teamScores[team];
}

void C_NetGame::PrintTeams()
{
    m_playersLock.Enter();
    int axisKills = 0;
    int alliedKills = 0;
    for (const S_NetPlayerState &player : m_players) {
        if (!player.name[0] || !player.human)
            continue;
        GetGame()->Print("%15s %d\n", player.name, player.human->GetTeam(false));
        if (player.human->GetTeam(false) == 0)
            axisKills += player.stats[NET_STAT_KILLS];
        else
            alliedKills += player.stats[NET_STAT_KILLS];
    }
    const S_vector grey(200.0f, 200.0f, 200.0f);
    GetGame()->PrintColored(grey, "Kills  Axis: %2d Allied: %2d\n", axisKills, alliedKills);
    GetGame()->PrintColored(grey, "Score  Axis: %2.0f Allied: %2.0f\n", static_cast<double>(m_teamScores[0]),
                            static_cast<double>(m_teamScores[1]));
    m_playersLock.Leave();
}

void C_NetGame::FillServerStatus(S_ServerStatus *status)
{
    m_playersLock.Enter();
    status->players.clear();
    for (const S_NetPlayerState &player : m_players) {
        if (!player.name[0])
            continue;
        S_StatusPlayer entry;
        std::strcpy(entry.name, player.name);
        std::memcpy(&entry, player.stats, sizeof player.stats);
        entry.team = player.human ? player.human->GetTeam(false) : 2;
        status->players.push_back(entry);
    }
    m_playersLock.Leave();
    const S_MultiplayerSettings &settings = *g_gameProfile.m_mpSettings;
    status->maxClients = settings.maxClients;
    status->fragLimit = settings.pointLimit;
    status->roundLimit = settings.roundLimit;
    status->roundCount = settings.roundCount;
    status->preparationTime = settings.warmup;
    status->respawnTime = settings.respawnTime;
    status->allowRespawn = settings.allowRespawn;
    status->friendlyFire = settings.friendlyFire;
    status->autoTeamBalance = settings.autoTeamBalance;
    status->thirdPersonView = settings.thirdPersonView;
    status->spawnProtectionTime = settings.spawnProtection;
    status->fallingDamage = settings.fallingDamage;
    status->unknown40 = settings.unknown25C;
    status->inverseDamage = settings.inverseDamage;
    status->allowVehicles = settings.allowVehicles;
    status->difficulty = settings.coopDifficulty;
    status->lives = settings.coopLives;
    status->allowCrosshair = settings.allowCrosshair;
    status->spawnOnStart = settings.spawnOnStart;
    status->teamLives = settings.teamLives;
    status->axisScore = static_cast<int>(m_teamScores[0]);
    status->alliedScore = static_cast<int>(m_teamScores[1]);
}

void C_NetGame::FlipSessionParity()
{
    if (g_gameRole & GAME_ROLE_SERVER) {
        m_playersLock.Enter();
        for (S_NetPlayerState &player : m_players) {
            if (player.name[0] && player.joinStage == 2) {
                player.session ^= 1;
                player.link->m_session = player.session;
            }
        }
        m_playersLock.Leave();
        return;
    }
    S_NetPlayerState &local = m_players[static_cast<size_t>(g_localPlayerSlot)];
    local.session ^= 1;
    m_serverLink->m_session = local.session;
}

void C_NetGame::ResetReplication(bool all)
{
    if (all) {
        for (C_NetObject *object : m_replication->GetObjects()) {
            object->ClearMessages(true, false, true);
            object->ClearMessages(true, true, true);
        }
    }
    std::list<S_NetQueued> &queue = (g_gameRole & GAME_ROLE_SERVER) ? m_received : m_outgoing;
    m_queueLock.Enter();
    while (!queue.empty()) {
        const S_NetQueued queued = queue.front();
        queue.pop_front();
        g_netPool.FreeBuffer(queued.data, queued.size);
    }
    m_queueLock.Leave();
}

int C_NetGame::GetConnectionState()
{
    if (static_cast<int>(GetPerfTimeMs()) - m_transport->m_lastReceive < 601)
        return m_joinStatus;
    return NET_CONNECTION_LOST;
}
