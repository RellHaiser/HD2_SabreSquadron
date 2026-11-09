// The game commands (part 10d-vi): the server's (a player's join, respawn request, chat, suicide, team change, admin
// login and command, the state it asks for, votes: HandleServerCommand) and the client's (the session joined, a
// player left or respawned, chat, the map, the scores, the rounds, the teams, the whole game: HandleClientCommand).
// They arrive as NET_MESSAGE_GAME messages (C_NetGame::DispatchNetMessage); the other side's are written by
// net_messages.cpp (SendGameMessage, PostCommand). See game/game_app.h, net_game.h.
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

#include "net/net_game.h"

#include <cstdio>
#include <cstring>

#include "game/actors_module.h"
#include "game/campaign.h"
#include "game/console.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_menu.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/loader_screen.h"
#include "game/main_menu.h"
#include "game/module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/spawn_point.h"
#include "game/team.h"
#include "game/text_box.h"

namespace {

// A player's stats until the profile's (as the spawn point's).
constexpr int DEFAULT_STATS[STAT_COUNT] = {800, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};
// The stats after the health in a respawn (game message 3), in the message's order.
constexpr int STAT_ORDER[] = {2, 1, 3, 4, 5, 6, 7, 8, 9, 11, 10};

// The chat's prefix of a team line; the stand-ins for UTF-8 text without the unicode font.
const char TEAM_PREFIX[] = "<TEAM> ";
const char HIDDEN_NAME[] = "XXX";
const char HIDDEN_TEXT[] = "????????";

// The texts: a player left / joined; the first rank's name (the allies'; the axis' 10 on).
constexpr int TEXT_PLAYER_LEFT = 0x22CB;
constexpr int TEXT_PLAYER_JOINED = 0x22CA;
constexpr int TEXT_FIRST_RANK = 0x27A6;
// The in-game menu's message once the whole game is in (game command 0x15).
constexpr int TEXT_JOINED_GAME = 0x92C;

// A soldier state's defaults before a message's (no profile or team, the spawn zone chosen later, deployed).
void ResetState(S_SoldierState &state)
{
    state = S_SoldierState();
    state.speechSet = -1;
    state.unknown34 = -1;
    state.unknown38 = -1;
    state.team = -1;
    state.spawnZone = 0x3FF;
    state.deployed = true;
    state.profileIndex = -1;
}

// A joining or respawning player's stats from its soldier's profile (its skills; a VIP's strength gains its uniform's
// weight and 1).
void ProfileStats(int *stats, const S_SoldierState &state)
{
    const S_SoldierProfile *profile = g_gameProfile.m_humanStore->Get(state.profileIndex);
    if (!profile)
        return;
    stats[STAT_HEALTH] = static_cast<int>(profile->stats[STAT_HEALTH]);
    stats[STAT_STRENGTH] = static_cast<int>(profile->stats[STAT_STRENGTH]);
    if (state.vip) {
        // Fix: an unknown uniform counts no weight (the original read it through null).
        const C_ItemDef *uniform = GetItemDef(state.uniform.typeId);
        const float weight = uniform ? uniform->GetWeight() : 0.0f;
        stats[STAT_STRENGTH] = static_cast<int>(weight + static_cast<float>(stats[STAT_STRENGTH]) + 1.0f);
    }
    for (int i = STAT_ENDURANCE; i <= STAT_LOCKPICK; ++i)
        stats[i] = static_cast<int>(profile->stats[i]);
}

// The squad's first soldier (the player's), null for none.
S_SquadSoldier *FirstSoldier()
{
    const std::vector<S_SquadSoldier *> &soldiers = g_gameProfile.m_missionSquad->states;
    return soldiers.empty() ? nullptr : soldiers[0];
}

float ReadFloat(C_BitStream *stream)
{
    const uint32_t bits = stream->ReadBits(32);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}


}  // namespace

// ---- the server's ----------------------------------------------------------------------------------------------------

void C_GameApp::HandleServerCommand(int id, C_BitStream *stream)
{
    int type;
    int to = 0;
    intptr_t a = 0;
    int b = 0;
    switch (id) {
    case 0: {
        // A player joined with its soldier (its stats from the profile; a local game: none).
        const auto slot = static_cast<int>(stream->ReadBits(32));
        int stats[STAT_COUNT];
        std::memcpy(stats, DEFAULT_STATS, sizeof stats);
        S_SoldierState state;
        ResetState(state);
        NetReadSoldierState(static_cast<C_NetMessage *>(stream), state, false);
        SplitSoldierStacks(state);
        ProfileStats(stats, state);
        GetGame()->OnPlayerJoined(slot, &state, (g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL ? stats : nullptr);
        return;
    }
    case 1: {
        // A player asks to respawn with its soldier (the state and stats handed over: the game frees them; the stats
        // also by the player's rank).
        const auto slot = static_cast<int>(stream->ReadBits(32));
        auto *state = new S_SoldierState;
        ResetState(*state);
        NetReadSoldierState(static_cast<C_NetMessage *>(stream), *state, false);
        SplitSoldierStacks(*state);
        int *stats = new int[STAT_COUNT];
        std::memcpy(stats, DEFAULT_STATS, sizeof DEFAULT_STATS);
        ProfileStats(stats, *state);
        g_gameProfile.m_humanStore->GetStats(stats, state->profileIndex, g_netGame.GetPlayerStats(slot)[NET_STAT_RANK]);
        GetGame()->OnRespawnRequest(slot, state, stats);
        return;
    }
    case 2: {
        // A player asks where everybody is (game message 5 to it).
        const auto slot = static_cast<size_t>(stream->ReadBits(32));
        to = g_netGame.LockPlayers()[slot].connection;
        g_netGame.UnlockPlayers();
        type = 5;
        break;
    }
    case 3: {
        // A chat line: its sender active; sent on to everyone (game message 4 frees it).
        auto *line = static_cast<char *>(operator new(0x110));
        *reinterpret_cast<uint32_t *>(line + 0x104) = stream->ReadBits(32);
        *reinterpret_cast<uint32_t *>(line + 0x108) = stream->ReadBits(32);
        line[0x10C] = stream->ReadBits(1) != 0;
        const int sender = *reinterpret_cast<const int *>(line + 0x104);
        if (sender >= 0) {
            g_netGame.LockPlayers()[sender].activityTime = timeGetTime();
            g_netGame.UnlockPlayers();
        }
        stream->ReadString(line, 0x103);
#ifdef HD2_DEDICATED
        {
            // The server's console shows the line (DS 0x00780a41). (The original first builds "<TEAM> %s", then
            // overwrites it in every case.)
            // Fix: the original's sprintf can overflow this buffer, and it tests only sender == -1.
            char shown[0x144];
            if (sender < 0) {
                std::snprintf(shown, sizeof shown, "ADMIN : %s", line);
            } else {
                const S_NetPlayerState &player = g_netGame.LockPlayers()[sender];
                if (line[0x10C])
                    std::snprintf(shown, sizeof shown, player.team ? "%s <TEAM_ALIED>: %s" : "%s <TEAM_AXIS>: %s", player.name, line);
                else
                    std::snprintf(shown, sizeof shown, "%s: %s", player.name, line);
                g_netGame.UnlockPlayers();
            }
            GetConsole()->AddLine(shown);
        }
#endif
        type = 4;
        a = reinterpret_cast<intptr_t>(line);
        break;
    }
    case 4: {
        // A player's suicide.
        C_Human *human = g_netGame.GetPlayerHuman(static_cast<int>(stream->ReadBits(32)));
        if (!human)
            return;
        const S_vector zero(0.0f, 0.0f, 0.0f);
        human->Hit(HIT_ENVIRONMENT, -1.0f, zero, zero, human, 0);
        return;
    }
    case 5: {
        // A team asked for (2: the smaller; with the automatic balance kept only while the map's balance holds);
        // told to everyone (0xE).
        const auto slot = static_cast<int>(stream->ReadBits(32));
        auto team = static_cast<int>(stream->ReadBits(32));
        g_netGame.LockPlayers();
        g_netGame.UnlockPlayers();
        const bool balance = g_gameProfile.m_mpSettings->autoTeamBalance != 0;
        if (team == 2) {
            int axis;
            int allied;
            g_netGame.CountTeams(&axis, &allied, true, false);
            team = allied <= axis ? 1 : 0;
            if (balance) {
                g_netGame.SetPlayerTeam(slot, team, true);
                type = 0xE;
                a = slot;
                b = team;
                break;
            }
        }
        const int old = g_netGame.GetPlayerTeam(slot, true);
        g_netGame.SetPlayerTeam(slot, team, true);
        // (The map's balance as the original reads it: +0x58 then +0x5C.)
        const C_MpMissionDef *mission = static_cast<const C_MpMissionDef *>(GetGame()->GetMission());
        const int first = mission->GetBalanceAllied();
        const int second = mission->GetBalanceAxis();
        if (balance && !g_netGame.TeamsBalanced(first, second, true)) {
            g_netGame.SetPlayerTeam(slot, old, true);
            team = old;
        }
        type = 0xE;
        a = slot;
        b = team;
        break;
    }
    case 6: {
        // A player's +0x80 text (33 characters).
        const auto slot = static_cast<size_t>(stream->ReadBits(5));
        stream->ReadString(reinterpret_cast<char *>(g_netGame.LockPlayers()[slot].unknown80), 0x21);
        g_netGame.UnlockPlayers();
        return;
    }
    case 7: {
        // The admin login: the password right: admin; told to the player (0x13: 1 / 0).
        const auto slot = static_cast<size_t>(stream->ReadBits(5));
        S_NetPlayerState *players = g_netGame.LockPlayers();
        char password[0x80];
        stream->ReadString(password, 0x80);
        S_NetPlayerState &player = players[slot];
        if (player.name[0]) {
            const bool right = std::strcmp(password, g_gameProfile.m_mpSettings->adminPassword.c_str()) == 0;
            if (right)
                player.admin = true;
            SendGameMessage(0x13, player.connection, right, 0, false);
        }
        g_netGame.UnlockPlayers();
        return;
    }
    case 8: {
        // An admin's console command.
        char command[0x80];
        stream->ReadString(command, 0x80);
        GetCommandProcessor()->Execute(command);
        return;
    }
    case 9: {
        // A joining player's count of the temporary actors: the whole game sent to it (0x15: the counts agree).
        const auto slot = static_cast<size_t>(stream->ReadBits(5));
        const auto count = static_cast<int16_t>(stream->ReadBits(16));
        int actors = 0;
        for (int i = 32; i < g_actorsModule.GetActorCount(); ++i)
            actors += g_actorsModule.GetActor(i) != nullptr;
        S_NetPlayerState *players = g_netGame.LockPlayers();
        SendGameMessage(0x15, players[slot].connection, count == actors, 0, false);
        g_netGame.UnlockPlayers();
        return;
    }
    case 10: {
        // A vote asked for.
        const auto kind = static_cast<int>(stream->ReadBits(7));
        const auto arg = static_cast<int>(stream->ReadBits(10));
        g_netGame.m_vote.Start(kind, arg);
        return;
    }
    case 11:
        g_netGame.m_vote.CountYes();
        return;
    default:
        return;
    }
    SendGameMessage(type, to, a, b, false);
}

// ---- the client's ----------------------------------------------------------------------------------------------------

void C_GameApp::HandleClientCommand(int id, C_BitStream *stream)
{
    const bool server = (g_gameRole & GAME_ROLE_SERVER) != 0;
    auto *msg = static_cast<C_NetMessage *>(stream);
    switch (id) {
    case 0: {
        // The session joined: the ids, the rules, the map; the game made, the players listed on the loading screen
        // (their ranks' names); started: the player's slot and the temporary actors' count asked for (command 9);
        // failed: the main menu.
        g_netGame.OnConnected();
        GetGameApp()->m_randomId = stream->ReadBits(32);
        g_netGame.m_unknown448 = static_cast<int>(stream->ReadBits(32));
        g_gameProfile.m_mpSettings->ReadRules(stream);
        g_gameProfile.m_mpSettings->ReadCurrentMap(stream);
        if (m_state) {
            m_state->Stop();
            delete m_state;
        }
        auto *game = new C_game();
        m_state = game;
        g_gameProfile.m_campaign->SetupMultiplayerMission();
        const auto count = static_cast<int>(stream->ReadBits(8));
        for (int i = 0; i < count; ++i) {
            char name[0x34];
            stream->ReadString(name, 0x32);
            const auto team = static_cast<int>(stream->ReadBits(2));
            const auto rank = static_cast<int>(stream->ReadBits(5));
            const C_str rankName = g_texts.Get((team != 1 ? 10 : 0) + TEXT_FIRST_RANK + rank);
            char line[0x100];
            std::snprintf(line, sizeof line, "%s %s", rankName.c_str(), name);
            g_loaderScreen.AddPlayer(line, team);
        }
        if (m_state->Start(g_gameProfile.m_campaign->GetCurrentMission())) {
            g_netGame.LockPlayers();
            g_netGame.m_players.push_back(S_NetPlayerState());
            g_netGame.UnlockPlayers();
            PostCommand(9, 0, 0, false);
            return;
        }
        delete m_state;
        auto *menu = new C_MainMenu();
        m_state = menu;
        menu->Start(nullptr);
        return;
    }
    case 1:
        if (!server)
            GetGame()->EndMission(100);
        return;
    case 2: {
        // A player left (told).
        const auto connection = static_cast<int>(stream->ReadBits(32));
        if (server || !GetGame())
            return;
        if (g_netGame.GetPlayerName(g_netGame.FindPlayer(connection))) {
            const C_str text = g_texts.Get(TEXT_PLAYER_LEFT);
            const C_fstr line("%s %s", g_netGame.GetPlayerName(g_netGame.FindPlayer(connection)), text.c_str());
            GetGameApp()->PrintMessage(line.c_str());
        }
        GetGame()->OnPlayerLeft(connection);
        return;
    }
    case 3: {
        // A player respawned: its place, facing, soldier and stats.
        const auto slot = static_cast<int>(stream->ReadBits(32));
        float values[3];
        msg->ReadQuantized(values, msg->GetSender(-1), 7);
        const S_vector4 pos{values[0], values[1], values[2], 0.0f};
        S_vector facing;
        msg->ReadDirection3D(facing, -1);
        const S_vector4 dir{facing.x, facing.y, facing.z, 0.0f};
        int stats[STAT_COUNT];
        std::memcpy(stats, DEFAULT_STATS, sizeof stats);
        S_SoldierState state;
        ResetState(state);
        NetReadSoldierState(msg, state, false);
        stats[STAT_HEALTH] = static_cast<int>(stream->ReadBits(10));
        for (int stat : STAT_ORDER)
            stats[stat] = static_cast<int>(stream->ReadBits(7));
        if (!server) {
            SplitSoldierStacks(state);
            GetGame()->RespawnPlayer(slot, pos, dir, &state, stats);
        }
        return;
    }
    case 4: {
        // A chat line: shown (the in-game menu's chat, or the game's messages and over the sender) when the local
        // player may see it: its team's for a team line, both alive or both dead; without the unicode font UTF-8
        // names and text are hidden. From slot -1: the admin's.
        const auto slot = static_cast<int>(stream->ReadBits(32));
        stream->ReadBits(32);
        const bool teamLine = stream->ReadBits(1) != 0;
        char text[0x10C];
        if (slot == -1) {
            stream->ReadString(text, 0x104);
            if (!m_state)
                return;
            if (m_state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU) {
                C_GameMenu::AddChatLine(0, "ADMIN", text);
                return;
            }
            if (m_state->GetTypeId() == GAME_STATE_TYPE_GAME)
                static_cast<C_game *>(m_state)->PrintMessage(S_vector(160.0f, 160.0f, 160.0f), "ADMIN: %s", text);
            return;
        }
        char *body = text;
        if (teamLine) {
            std::strcpy(text, TEAM_PREFIX);
            body = text + std::strlen(TEAM_PREFIX);
        }
        stream->ReadString(body, 0x104);
        S_NetPlayerState *players = g_netGame.LockPlayers();
        const S_NetPlayerState &sender = players[slot];
        const S_NetPlayerState &local = players[g_localPlayerSlot];
        C_Human *senderHuman = sender.human;
        const C_Human *localHuman = local.human;
        const bool visible = !senderHuman || !localHuman || (localHuman->GetHealth() > 0) == (senderHuman->GetHealth() > 0);
        if ((!teamLine || sender.team == local.team) && visible && m_state) {
            const char *name = sender.name;
            const char *shown = text;
            if (!g_gameProfile.UsesUnicodeFont()) {
                if (HasUtf8Sequences(name))
                    name = HIDDEN_NAME;
                shown = HasUtf8Sequences(text) ? HIDDEN_TEXT : text;
            }
            if (m_state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU) {
                C_GameMenu::AddChatLine(sender.team, name, shown);
                g_netGame.UnlockPlayers();
                return;
            }
            if (m_state->GetTypeId() == GAME_STATE_TYPE_GAME) {
                static_cast<C_game *>(m_state)->PrintMessage(S_vector(255.0f, 255.0f, 255.0f), "%s: %s", name, shown);
                if (senderHuman)
                    senderHuman->SetChatText(shown);
            }
        }
        g_netGame.UnlockPlayers();
        return;
    }
    case 5: {
        // The players' places (dropped: the original hands them to empty debug drawing).
        const uint32_t count = stream->ReadBits(32);
        for (uint32_t i = 0; i < count; ++i) {
            for (int value = 0; value < 6; ++value)
                ReadFloat(stream);
        }
        return;
    }
    case 6:
        g_gameProfile.m_mpSettings->ReadCurrentMap(stream);
        if (GetGame())
            LoadNextMap();
        return;
    case 7:
        if (GetGame())
            GetGame()->RestartMission(true);
        g_temporaryActors.ReadState(stream);
        return;
    case 8:
        g_netGame.ReadScores(stream);
        return;
    case 9: {
        // A player joined (told).
        char name[0x34];
        stream->ReadString(name, 0x32);
        if (server)
            return;
        const C_str text = g_texts.Get(TEXT_PLAYER_JOINED);
        const C_fstr line("%s %s", name, text.c_str());
        GetGameApp()->PrintMessage(line.c_str());
        return;
    }
    case 10: {
        const auto objective = static_cast<int>(stream->ReadBits(5));
        const auto status = static_cast<int>(stream->ReadBits(4));
        if (!server && GetGame())
            GetGame()->ApplyObjectiveStatus(objective, status);
        return;
    }
    case 11:
        if (GetGame())
            GetGame()->OnWarmupFinished();
        return;
    case 12: {
        // The teams (each slot 6 bits, its team 2 bits; 0x21 ends): the local player's soldier moved to its new
        // team (in the game: its items gone, the in-game menu opened; in the in-game menu: its team shown).
        for (auto slot = static_cast<int>(stream->ReadBits(6)); slot != 0x21; slot = static_cast<int>(stream->ReadBits(6))) {
            const auto team = static_cast<int>(stream->ReadBits(2));
            if (slot == g_localPlayerSlot) {
                S_SquadSoldier *soldier = FirstSoldier();
                // Fix: no soldier: nothing to move (the original read null).
                if (soldier && team != soldier->team) {
                    const int kind = GetGameApp()->m_state->GetTypeId();
                    if (kind == GAME_STATE_TYPE_GAME) {
                        soldier->team = team;
                        soldier->ClearItems();
                        GetGameApp()->m_state = static_cast<C_game *>(GetGameApp()->m_state)->OpenGameMenu(-2);
                    } else if (kind == GAME_STATE_TYPE_GAME_MENU) {
                        static_cast<C_GameMenu *>(GetGameApp()->m_state)->SetTeam(team);
                    }
                }
            }
            g_netGame.SetPlayerTeam(slot, team, false);
            g_netGame.SetPlayerTeam(slot, team, true);
        }
        if (GetGame())
            GetGame()->OnWarmupStarted();
        return;
    }
    case 13: {
        const auto winner = static_cast<int>(stream->ReadBits(2));
        if (GetGame())
            GetGame()->OnRoundOver(winner);
        return;
    }
    case 14: {
        // A player's team: another's taken while unknown; the local player's pending one (the in-game menu told).
        const auto slot = static_cast<int>(stream->ReadBits(5));
        const auto team = static_cast<int>(stream->ReadBits(2));
        if (slot != g_localPlayerSlot) {
            if (g_netGame.GetPlayerTeam(slot, false) == 2)
                g_netGame.SetPlayerTeam(slot, team, false);
            return;
        }
        g_netGame.SetPlayerTeam(slot, team, true);
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU) {
            if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && !server && GetGame())
                ShowGameMenuMessage(-1);
            static_cast<C_GameMenu *>(m_state)->SetTeam(team);
        }
        return;
    }
    case 15: {
        // The respawn screen (the local player's).
        const auto slot = static_cast<int>(stream->ReadBits(5));
        const bool cancel = stream->ReadBits(1) != 0;
        if (slot == g_localPlayerSlot)
            GetGame()->OnRespawnScreen(cancel);
        return;
    }
    case 16: {
        // The respawn wave: the round time's milliseconds kept; a dead local player in a uniform respawns, else the
        // in-game menu respawns it when applied.
        C_Human *human = g_netGame.GetLocalHuman();
        if (GetGame() && !server)
            GetGame()->SetRespawnWaveTime(GetGame()->GetRoundTime() % 1000);
        if (!human || human->GetHealth() > 0)
            return;
        // Fix: no soldier: no uniform (the original read null).
        const S_SquadSoldier *soldier = FirstSoldier();
        if (soldier && soldier->uniform.typeId != -1 && soldier->uniform.present == 1) {
            human->RequestRespawn();
            return;
        }
        if (m_state && m_state->GetTypeId() == GAME_STATE_TYPE_GAME_MENU)
            static_cast<C_GameMenu *>(m_state)->SetRespawnOnApply(true);
        return;
    }
    case 17: {
        // A player kicked (`reason`): the local player's session closes.
        const auto reason = static_cast<int>(stream->ReadBits(32));
        const auto slot = static_cast<int>(stream->ReadBits(32));
        if (GetGame() && slot == g_localPlayerSlot)
            GetGame()->OnSessionClosing(reason);
        if (!server)
            g_netGame.KickPlayer(slot, reason);
        return;
    }
    case 18:
        // The rules (the server's own: read past).
        if (!server) {
            g_gameProfile.m_mpSettings->ReadRules(stream);
        } else {
            S_MultiplayerSettings scratch;
            scratch.Reset();
            scratch.ReadRules(stream);
        }
        return;
    case 19: {
        // The admin login's answer.
        const bool granted = stream->ReadBits(32) != 0;
        g_netGame.LockPlayers()[g_localPlayerSlot].admin = granted;
        g_netGame.UnlockPlayers();
        PrintMessage(granted ? "Admin rights granted!" : "Incorrect admin password!");
        return;
    }
    case 20:
        g_temporaryActors.ReadState(stream);
        return;
    case 21:
        ReadFullState(stream);
        return;
    case 22: {
        const auto kind = static_cast<int>(stream->ReadBits(7));
        const auto arg = static_cast<int>(stream->ReadBits(10));
        g_netGame.m_vote.Begin(kind, arg);
        return;
    }
    case 23:
        PrintVoteResult(stream->ReadBits(1) != 0);
        return;
    default:
        return;
    }
}

// The whole game (game command 0x15; none: back to the lobby): the players (a client's list replaced: their humans
// made with their soldiers and health; the local one in the player's team), the modules', the objectives; the camera
// free, the in-game menu opened (the lobby passed on), the join done (the local player's stage 2).
void C_GameApp::ReadFullState(C_BitStream *stream)
{
    const bool server = (g_gameRole & GAME_ROLE_SERVER) != 0;
    auto *msg = static_cast<C_NetMessage *>(stream);
    if (stream->ReadBits(1) == 0) {
        m_state->SetPendingState(GAME_STATE_LOBBY);
        return;
    }
    const auto count = static_cast<int>(stream->ReadBits(8));
    g_netGame.LockPlayers();
    if (!server)
        g_netGame.m_players.clear();
    for (int slot = 0; slot < count; ++slot) {
        S_NetPlayerState player;
        player.connection = static_cast<int>(stream->ReadBits(32));
        stream->ReadString(player.name, 0x32);
        const auto actorSlot = static_cast<int>(stream->ReadBits(32));
        player.stats.Read(stream);
        S_SoldierState state;
        ResetState(state);
        S_vector4 pos{0.0f, 0.0f, 0.0f, 0.0f};
        S_vector4 dir{0.0f, 0.0f, 1.0f, 0.0f};
        if (actorSlot != -1) {
            pos.x = ReadFloat(stream);
            pos.y = ReadFloat(stream);
            pos.z = ReadFloat(stream);
            dir.x = ReadFloat(stream);
            dir.y = ReadFloat(stream);
            dir.z = ReadFloat(stream);
            NetReadSoldierState(msg, state, false);
        }
        if (server)
            continue;
        g_netGame.m_players.push_back(player);
        if (actorSlot == -1)
            continue;
        C_Human *human = C_SpawnPoint::CreateSoldier(slot, actorSlot, pos, dir, &state, nullptr, -1, false);
        if (!human)
            continue;
        human->StartMission(false);
        const S_vector zero(0.0f, 0.0f, 0.0f);
        human->ApplyRemoteHit(0xE, 0, state.health, -1, zero, zero);
        if (slot == g_localPlayerSlot)
            g_teamManager.GetPlayerTeam()->AddMember(human);
        human->GetController()->SetSpectating(state.spectating);
        if (human->GetControllerType() == HUMAN_CONTROLLER_REMOTE)
            static_cast<C_RemoteController *>(human->GetController())->SetVip(state.vip);
    }
    g_netGame.UnlockPlayers();
    g_netGame.m_unknown8 = true;
    ModuleManager::BroadcastReadState(stream);
    GetGame()->ReadObjectiveStates(stream);
    GetGame()->SetCameraMode(GAME_CAMERA_FREE);
    m_state = static_cast<C_game *>(m_state)->OpenGameMenu(-2);
    if (GetGame()->GetState() == GAME_STATE_LOBBY || GetGame()->GetPendingState() == GAME_STATE_LOBBY)
        m_state->SetPendingState(GAME_STATE_LOBBY);
    if (!server && GetGame())
        ShowGameMenuMessage(TEXT_JOINED_GAME);
    g_netGame.LockPlayers()[g_localPlayerSlot].joinStage = 2;
    g_netGame.UnlockPlayers();
}
