// The console commands (part 10e): the keywords, the words of a command line (ReadConsoleToken), the command processor
// (Execute: a line of keywords with their values; a file of lines with "exec") and the console state (the command
// line's "-cmd" / "-c"; its commands, hosting or joining from it). See game/pending.h.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>

#include "net/net_game.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game/campaign.h"
#include "game/console.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_state.h"
#include "game/game_texts.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/igraph.h"

namespace {

// The keywords' ids: their entries in the table.
enum E_Keyword : int {
    KEY_MAX_CLIENTS = 0,                        // 0 .. 0x15: the session's rules (S_MultiplayerSettings +0x228 on)
    KEY_ROUND_LIMIT = 2,
    KEY_WARMUP = 4,
    KEY_RESPAWN_TIME = 5,
    KEY_SPAWN_PROTECTION = 10,
    KEY_AUTO_RESTART = 0x10,
    KEY_COOP_DIFFICULTY = 0x11,
    KEY_COOP_LIVES = 0x12,
    KEY_LAST_RULE = 0x15,
    KEY_MAP_NAME = 0x16,
    KEY_REMOVE_MAP = 0x17,
    KEY_STYLE = 0x18,
    KEY_DEATHMATCH = 0x19,
    KEY_COOPERATIVE = 0x1C,
    KEY_DOMAIN = 0x1D,
    KEY_LOCAL = 0x1E,
    KEY_INTERNET = 0x1F,
    KEY_SESSION_NAME = 0x20,
    KEY_HELP = 0x21,
    KEY_EXEC = 0x22,
    KEY_SERVER = 0x23,
    KEY_DEDICATED = 0x24,
    KEY_JOIN = 0x25,
    KEY_VOTE = 0x26,
    KEY_VOTE_YES = 0x27,
    KEY_LIST_PLAYERS = 0x28,
    KEY_KICK_PLAYER = 0x29,
    KEY_NEXT_MAP = 0x2A,
    KEY_RESTART = 0x2B,
    KEY_PORT = 0x2C,
    KEY_PASSWORD = 0x2D,
    KEY_QUIT = 0x2E,
    KEY_VOICE_CHAT = 0x2F,
    KEY_LIST_MAPS = 0x30,
    KEY_ADMIN_PASS = 0x31,
    KEY_ADMIN_LOGIN = 0x32,
    KEY_SV_CMD = 0x33,
    KEY_MAX_PING = 0x34,
    KEY_MAX_FREQ = 0x35,
    KEY_ASAY = 0x36,
    KEY_MAX_INACTIVITY = 0x37,
    KEY_LIST_OBJECTIVES = 0x38,
    KEY_VR12 = 0x39,
    KEY_NONE = 0x40,
};

// The token kinds of ReadConsoleToken.
enum { TOKEN_WORD = 0, TOKEN_NUMBER = 1, TOKEN_KEYWORD = 2, TOKEN_END = 3 };

// The texts: a word that is no keyword, a value not allowed, a file not opened.
constexpr int TEXT_UNKNOWN_KEYWORD = 0x582;
constexpr int TEXT_BAD_VALUE = 0x585;
constexpr int TEXT_NO_FILE = 0x586;

constexpr int KEYWORD_SIZE = 0x204;
constexpr int KEYWORD_ID_AT = 0x200;

const char *const KEYWORDS[] = {
    "maxclients", "pointlimit", "roundlimit", "roundcount", "warmup", "respawntime", "allowrespawn", "friendlyfire",
    "autoteambalance", "3rdpersonview", "spawnprotection", "inversedamage", "fallingdmg", "floodprotect", "",
    "allowvehicles", "autorestart", "coopdifficulty", "cooplives", "allowcrosshair", "spawnonstart", "teamlives",
    "mapname", "removemap", "style", "deathmatch", "occupation", "objectives", "cooperative", "domain", "local",
    "internet", "sessionname", "help", "exec", "server", "dedicated", "join", "vote", "voteyes", "listplayers",
    "kickplayer", "nextmap", "restart", "port", "password", "quit", "voicechat", "listmaps", "adminpass", "adminlogin",
    "svcmd", "maxping", "maxfreq", "asay", "maxinactivity", "listobjectives", "vr12", "sc03", "sc06", "truespeech",
    "gsm", "adpcm", "pcm", "none", "dumpentity"};
constexpr int KEYWORD_COUNT = sizeof KEYWORDS / sizeof KEYWORDS[0];
constexpr int EMPTY_KEYWORD = 14;                       // (its id 0)

// Commands and files read (the rest of the line, words).
constexpr int WORD_SIZE = 0x200;
constexpr int FILE_LINE_SIZE = 100;
constexpr int COMMAND_TEXT_SIZE = 0x80;
constexpr int CHAT_LINE_SIZE = 0x110;
constexpr int CHAT_TEXT_SIZE = 0x104;

C_CommandProcessor *g_commandProcessor;                 // 0x0098c0fc

S_ServerEntry &SessionServer()
{
    return *reinterpret_cast<S_ServerEntry *>(g_gameProfile.m_mpSettings->unknown17C);
}

void Print(const char *text)
{
    GetGameApp()->PrintMessage(text);
}

void PrintText(int id)
{
    const C_str text = g_texts.Get(id);
    Print(text.c_str());
}

// The spaces, tabs and the marks between commands skipped.
void SkipSeparators(const char **cursor)                // 0x00744750 (__fastcall)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '>' || **cursor == '-')
        ++*cursor;
}

// A copy for the commands' messages (Fix: terminated; the original's strncpy may not).
char *CommandText(const char *text)
{
    auto *copy = static_cast<char *>(operator new(COMMAND_TEXT_SIZE));
    std::strncpy(copy, text, COMMAND_TEXT_SIZE);
    copy[COMMAND_TEXT_SIZE - 1] = 0;
    return copy;
}

// A rule's value within what the game takes.
int ClampRule(int keyword, int value)
{
    const auto atLeastOne = [](int v) { return v < 1 ? 1 : v; };
    switch (keyword) {
    case KEY_COOP_LIVES:
        return value > 99 ? 99 : value;
    case KEY_ROUND_LIMIT:
        return value < 100 ? atLeastOne(value) : 99;
    case KEY_RESPAWN_TIME:
        return value < 0x1735 ? (value < 3 ? 3 : value) : 0x1734;
    case KEY_WARMUP:
        return value < 0x1735 ? atLeastOne(value) : 0x1734;
    case KEY_MAX_CLIENTS:
        if (value > 0x20)
            value = 0x20;
        if (g_gameProfile.m_mpSettings->gameType == MP_GAME_COOPERATIVE && value > 6)
            value = 6;
        return value;
    case KEY_SPAWN_PROTECTION:
        return value > 0xF ? 0xF : value;
    case KEY_COOP_DIFFICULTY:
        if (g_gameProfile.m_mpSettings->gameType != MP_GAME_COOPERATIVE)
            return value;
        return atLeastOne(value > 4 ? 4 : value);
    default:
        return value;
    }
}

// A map list of the set-up's game type, the last first.
void PrintSetupMaps(C_MpGameSetup *setup, const char *title, int gameType)
{
    Print(title);
    setup->SetGameType(gameType);
    const std::vector<C_MpMap *> &maps = setup->GetCurrentMaps().maps;
    for (size_t i = maps.size(); i-- > 0;)
        Print(maps[i]->GetName());
}

}  // namespace

void C_CommandProcessor::ExecuteFile(const char *name)
{
    FILE *file = std::fopen(name, "rt");
    if (!file) {
        const C_str text = g_texts.Get(TEXT_NO_FILE);
        Print(C_fstr("%s %s", text.c_str(), name).c_str());
        return;
    }
    char line[FILE_LINE_SIZE];
    // (Fix: a long line is cut; the original's %[^\n] overran the buffer. A last line without its newline is not
    // run, a file starting with an empty line not at all: as in the original.)
    int read = std::fscanf(file, "%99[^\n]s", line);
    while (read != 0 && !std::feof(file)) {
        std::fscanf(file, "\n");
        Execute(line);
        read = std::fscanf(file, "%99[^\n]s", line);
    }
    std::fclose(file);
}

char g_votableCommands[KEYWORD_COUNT + 1][KEYWORD_SIZE];   // 0x00846088

// (The table's static data in the original.)
static const bool g_keywordsMade = [] {
    for (int i = 0; i < KEYWORD_COUNT; ++i) {
        std::strcpy(g_votableCommands[i], KEYWORDS[i]);
        const int id = i == EMPTY_KEYWORD ? 0 : i;
        std::memcpy(g_votableCommands[i] + KEYWORD_ID_AT, &id, sizeof id);
    }
    const int end = -1;
    std::memcpy(g_votableCommands[KEYWORD_COUNT] + KEYWORD_ID_AT, &end, sizeof end);
    return true;
}();

int ReadConsoleToken(const char **cursor, char *out, int *value, char *keywords)
{
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '>' ||
           (**cursor == '-' && std::isalnum(static_cast<unsigned char>((*cursor)[1]))))
        ++*cursor;
    if (**cursor == 0)
        return TOKEN_END;
    char *to = out;
    if (**cursor == '"') {
        ++*cursor;
        while (**cursor != '"' && **cursor != 0)
            *to++ = *(*cursor)++;
        *to = 0;
        ++*cursor;
    } else {
        while (**cursor != ' ' && **cursor != '\t' && **cursor != 0)
            *to++ = *(*cursor)++;
        *to = 0;
    }
    if (keywords) {
        char lower[WORD_SIZE];
        std::strcpy(lower, out);
        _strlwr(lower);
        for (const char *entry = keywords;; entry += KEYWORD_SIZE) {
            int id;
            std::memcpy(&id, entry + KEYWORD_ID_AT, sizeof id);
            if (id == -1)
                break;
            if (std::strcmp(entry, lower) == 0) {
                *value = id;
                return TOKEN_KEYWORD;
            }
        }
    }
    if (!std::isdigit(static_cast<unsigned char>(*out)) && *out != '-')
        return TOKEN_WORD;
    *value = std::atoi(out);
    return TOKEN_NUMBER;
}

C_CommandProcessor::C_CommandProcessor()                // 0x007426a0
{
    g_gameProfile.m_mpSettings->MakeSetup();
}

C_CommandProcessor::~C_CommandProcessor()               // 0x007426e0
{
    g_gameProfile.m_mpSettings->DeleteSetup();
}

C_CommandProcessor *GetCommandProcessor()
{
    if (!g_commandProcessor)
        g_commandProcessor = new C_CommandProcessor;
    return g_commandProcessor;
}

void ClearCommandProcessor()
{
    g_commandProcessor = nullptr;
}

bool C_CommandProcessor::Execute(const char *command)
{
    GetCommandProcessor();
    S_MultiplayerSettings *settings = g_gameProfile.m_mpSettings;
    char *const keywords = g_votableCommands[0];
    const bool server = (g_gameRole & GAME_ROLE_SERVER) != 0;
    const bool client = (g_gameRole & GAME_ROLE_CLIENT) != 0;
    const bool local = (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL;
    char word[WORD_SIZE];
    // (Fix: 0 until a number is read; the original printed an uninitialised value for a rule without one.)
    int value = 0;
    for (;;) {
        int keyword;
        if (ReadConsoleToken(&command, word, &keyword, keywords) != TOKEN_KEYWORD) {
            PrintText(TEXT_UNKNOWN_KEYWORD);
            return false;
        }
        bool skipSeparators = true;
        switch (keyword) {
        case KEY_MAP_NAME:
            if (ReadConsoleToken(&command, word, &value, nullptr) == TOKEN_WORD) {
                // (Fix: the name not used as a format.)
                if (server && !settings->AddMap(word))
                    Print(C_fstr("Map %s doesn't exist!", word).c_str());
            } else {
                Print("maps: ");
                for (const C_MpMap *map : settings->maps)
                    Print(map->GetName());
            }
            break;
        case KEY_REMOVE_MAP:
            if (server && ReadConsoleToken(&command, word, &value, nullptr) == TOKEN_WORD && GetGame())
                settings->RemoveMap(word);
            break;
        case KEY_STYLE:
            if (ReadConsoleToken(&command, word, &value, keywords) == TOKEN_KEYWORD && local) {
                if (value >= KEY_DEATHMATCH && value <= KEY_COOPERATIVE) {
                    if (server) {
                        settings->gameType = value - KEY_DEATHMATCH;
                        settings->ApplySetupRules();
                    }
                } else {
                    PrintText(TEXT_BAD_VALUE);
                }
            } else {
                Print(C_fstr("style: %s", g_votableCommands[KEY_DEATHMATCH + settings->gameType]).c_str());
            }
            break;
        case KEY_DOMAIN:
            if (ReadConsoleToken(&command, word, &value, keywords) == TOKEN_KEYWORD) {
                if (value == KEY_LOCAL || value == KEY_INTERNET) {
                    if (server)
                        settings->unknown174 = value - KEY_LOCAL;
                } else {
                    PrintText(TEXT_BAD_VALUE);
                }
            } else {
                Print(C_fstr("domain: %s", g_votableCommands[KEY_LOCAL + settings->unknown174]).c_str());
            }
            break;
        case KEY_SESSION_NAME:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_WORD)
                Print(C_fstr("sessionname: %s", settings->sessionName).c_str());
            else if (server && local)
                std::strcpy(settings->sessionName, word);
            break;
        case KEY_HELP: {
            // Four to a line after the first five (a last short line not shown, as in the original).
            Print("Console keywords:");
            std::string line;
            for (int i = 0; i < KEYWORD_COUNT; ++i) {
                line += g_votableCommands[i];
                line += ' ';
                if (i % 4 == 0 && i > 3) {
                    Print(line.c_str());
                    line.clear();
                }
            }
            break;
        }
        case KEY_EXEC:
            SkipSeparators(&command);
            ExecuteFile(command);
            return true;
        case KEY_SERVER:
            if (!local)
                return true;
            if (settings->maps.empty()) {
                Print("Cannot create server! No map!");
                return true;
            }
#ifndef HD2_DEDICATED   // no window or input in LS3DRF.dll
            GetIGraph()->InitMouse(settings->dedicated ? 0 : 1);
            ShowWindow(GetIGraph()->GetHWND(), settings->dedicated ? SW_HIDE : SW_SHOW);
#endif
            settings->sessionType = 2;
            GetGameApp()->m_state->SetPendingState(GAME_STATE_HOST_SESSION);
            break;
        case KEY_DEDICATED:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_NUMBER)
                Print(C_fstr("dedicated  %d", settings->dedicated ? 1 : 0).c_str());
            else if (server)
                settings->dedicated = value != 0;
            break;
        case KEY_JOIN:
            if (!local)
                return true;
#ifndef HD2_DEDICATED   // no window or input in LS3DRF.dll
            GetIGraph()->InitMouse(1);
            ShowWindow(GetIGraph()->GetHWND(), SW_SHOW);
#endif
            ReadConsoleToken(&command, word, &value, nullptr);
            SessionServer().address = inet_addr(word);
            settings->sessionType = 1;
            GetGameApp()->m_state->SetPendingState(GAME_STATE_JOIN_SESSION);
            break;
        case KEY_VOTE:
            if (!local) {
                const int kind = ReadConsoleToken(&command, word, &value, keywords) == TOKEN_KEYWORD ? value : -1;
                if (kind >= KEY_KICK_PLAYER && kind <= KEY_RESTART) {
                    const int token = ReadConsoleToken(&command, word, &value, nullptr);
                    if (kind == KEY_KICK_PLAYER && token != TOKEN_NUMBER)
                        value = g_netGame.FindPlayerByName(word);
                    if (kind != KEY_KICK_PLAYER || token == TOKEN_NUMBER || value != -1) {
                        if (!server)
                            PostVote(kind, value);
                        else
                            g_netGame.m_vote.Start(kind, value);
                    }
                } else {
                    Print("Not valid voting command");
                }
            }
            break;
        case KEY_VOTE_YES:
            if (!server)
                g_netGame.m_vote.Vote();
            else
                g_netGame.m_vote.CountYes();
            break;
        case KEY_LIST_PLAYERS: {
            const S_NetPlayerState *players = g_netGame.LockPlayers();
            for (int i = 0; i < g_netGame.GetPlayerCount(); ++i) {
                if (players[i].name[0] && players[i].connection != 0)
                    Print(C_fstr("%d. %s", i, players[i].name).c_str());
            }
            g_netGame.UnlockPlayers();
            break;
        }
        case KEY_KICK_PLAYER:
            if (server) {
                const int token = ReadConsoleToken(&command, word, &value, nullptr);
                if (token == TOKEN_NUMBER)
                    g_netGame.KickPlayer(value, 0);
                else if (token == TOKEN_WORD)
                    g_netGame.KickPlayerByName(word, 0);
            }
            break;
        case KEY_NEXT_MAP:
            if (server && !local) {
                const bool named = ReadConsoleToken(&command, word, &value, nullptr) == TOKEN_WORD;
                if (GetGame())
                    ChangeMap(named ? word : nullptr);
            }
            break;
        case KEY_RESTART:
            if (!local && server)
                GetGame()->StartNextRound();
            break;
        case KEY_PORT:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_NUMBER || value > 0xFFFF) {
                Print(C_fstr("port  %d", SessionServer().port).c_str());
            } else {
                SessionServer().port = value;
                settings->port = value;
                settings->unknown174 = 1;
            }
            break;
        case KEY_PASSWORD:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_WORD) {
                Print((std::string("password: ") + settings->password.c_str()).c_str());
            } else {
                SessionServer().password = true;
                settings->password = word;
            }
            break;
        case KEY_QUIT:
            if (!client || local) {
                Print("Closing game. Wait please.");
                GetGameApp()->m_state->SetPendingState(GAME_STATE_QUIT);
            }
            break;
        case KEY_VOICE_CHAT:
            if (ReadConsoleToken(&command, word, &value, keywords) != TOKEN_KEYWORD) {
                const C_str codec = g_texts.Get(GetVoiceCodecTextId(settings->unknown178));
                Print(C_fstr("voicechat %s", codec.c_str()).c_str());
            } else if (server) {
                // vr12 .. pcm: 1 .. 7; none: 0.
                if (value >= KEY_VR12 && value < KEY_NONE)
                    settings->unknown178 = value - KEY_VR12 + 1;
                else if (value == KEY_NONE)
                    settings->unknown178 = 0;
                else
                    Print("Not valid voice codec");
                SessionServer().voice = settings->unknown178 != 0;
            }
            break;
        case KEY_LIST_MAPS: {
            C_MpGameSetup *setup = settings->setup;
            PrintSetupMaps(setup, "Deathmatch maps:", 0);
            Print("");
            PrintSetupMaps(setup, "Occupation maps:", 1);
            Print("");
            PrintSetupMaps(setup, "Objectives maps:", 2);
            Print("");
            PrintSetupMaps(setup, "Cooperative maps:", 3);
            break;
        }
        case KEY_ADMIN_PASS:
            if (ReadConsoleToken(&command, word, &value, nullptr) == TOKEN_WORD)
                settings->adminPassword = word;
            break;
        case KEY_ADMIN_LOGIN:
            if (ReadConsoleToken(&command, word, &value, nullptr) == TOKEN_WORD)
                GetGameApp()->PostCommand(7, reinterpret_cast<intptr_t>(CommandText(word)), 0, false);
            break;
        case KEY_SV_CMD:
            // (The rest of the line goes to the server and is still read here, as in the original.)
            if (IsPlayerAdmin(g_localPlayerSlot))
                GetGameApp()->PostCommand(8, reinterpret_cast<intptr_t>(CommandText(command)), 0, false);
            else
                Print("You don't have admin rights!");
            break;
        case KEY_MAX_PING:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_NUMBER)
                Print(C_fstr("maxping:  %d", g_netMaxPing).c_str());
            else
                g_netMaxPing = value;
            break;
        case KEY_MAX_FREQ:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_NUMBER)
                Print(C_fstr("maxfreq:  %d", g_netMaxFreq).c_str());
            else if (value != 0)
                g_netMaxFreq = value;
            break;
        case KEY_ASAY:
            if (server) {
                // The rest of the line as the admin's chat (Fix: cut to the chat's size).
                auto *line = static_cast<char *>(operator new(CHAT_LINE_SIZE));
                std::memset(line, 0, CHAT_LINE_SIZE);
                const int from = -1;
                std::memcpy(line + 0x104, &from, sizeof from);
                std::strncpy(line, command, CHAT_TEXT_SIZE - 1);
                command += std::strlen(command);
                GetGameApp()->SendGameMessage(4, 0, reinterpret_cast<intptr_t>(line), 0, false);
                if (!client)
                    GetConsole()->AddLine(C_fstr("ADMIN: %s", line).c_str());
            }
            break;
        case KEY_MAX_INACTIVITY:
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_NUMBER)
                Print(C_fstr("maxinactivity:  %d", g_netMaxInactivity / 1000).c_str());
            else
                g_netMaxInactivity = value * 1000;
            break;
        case KEY_LIST_OBJECTIVES: {
            const auto *mission = static_cast<const C_MpMissionDef *>(GetGame()->GetMission());
            for (int i = 0; i < mission->GetObjectiveCount(); ++i) {
                const S_MpObjective *objective = mission->GetMpObjective(i);
                const C_str text = g_texts.Get(objective->alliedText);
                Print(C_fstr("%s - %s", objective->status == OBJECTIVE_COMPLETED ? "OK" : "  ", text.c_str()).c_str());
            }
            break;
        }
        default:
            if (keyword > KEY_LAST_RULE)
                break;
            // A rule: its value (a server's: within limits, told to the clients); without one the last value shown.
            if (ReadConsoleToken(&command, word, &value, nullptr) != TOKEN_NUMBER) {
                Print(C_fstr("%s  %d", g_votableCommands[keyword], value).c_str());
                break;
            }
            if (!server)
                break;
            // (autorestart: only before a session; read on without the separators otherwise.)
            if (keyword == KEY_AUTO_RESTART && !local) {
                skipSeparators = false;
                break;
            }
            (&settings->maxClients)[keyword] = ClampRule(keyword, value);
            GetGameApp()->SendGameMessage(0x12, 0, 0, 0, false);
            break;
        }
        if (skipSeparators)
            SkipSeparators(&command);
        if (*command == 0)
            return true;
    }
}

// ---- the console state ---------------------------------------------------------------------------------------------

C_ConsoleState::C_ConsoleState(const char *command)
{
    // (Fix: cut to the buffer.)
    if (command)
        std::strncpy(m_command, command, sizeof m_command - 1);
}

bool C_ConsoleState::Start(void *)                      // 0x00744360
{
    m_state = GAME_STATE_RUNNING;
    m_mode = GAME_STATE_RUNNING;
    GetIGraph()->SetFlags(g_graphFlags & ~0x20u);
    return true;
}

void C_ConsoleState::ShowBanner()
{
    GetConsole()->AddLine("HD2 console");
    GetConsole()->AddLine("Type 'help' for keywords list");
    m_shown = true;
}

void C_ConsoleState::Update(int)                        // 0x007443d0
{
    if (m_shown) {
        while (const char *command = GetConsole()->PopCommand())
            GetCommandProcessor()->Execute(command);
    }
    if (m_command[0]) {
        if (!GetCommandProcessor()->Execute(m_command))
            GetGameApp()->m_state->SetPendingState(GAME_STATE_QUIT);
        m_command[0] = 0;
    }
    const int state = GetState();
    if (state == GAME_STATE_HOST_SESSION) {
        m_mode = GAME_STATE_HOST_SESSION;
        StartHosting();
    } else if (state == GAME_STATE_JOIN_SESSION) {
        SetState(GAME_STATE_RUNNING);
        StartJoining();
        m_mode = GAME_STATE_JOIN_SESSION;
    }
}

void C_ConsoleState::StartHosting()                     // 0x00744490
{
    S_MultiplayerSettings *settings = g_gameProfile.m_mpSettings;
    if (!settings->dedicated) {
        SetState(GAME_STATE_HOST_SESSION);
        return;
    }
    S_NetSessionDesc desc;
    std::memcpy(&desc, settings->sessionName, sizeof desc);
    // (DirectPlay's TCP/IP service provider.)
    const uint32_t provider[4] = {0xEBFE7BA0, 0x11D2628D, 0x60000FAE, 0x1114B097};
    std::memcpy(desc.serviceProvider, provider, sizeof provider);
    desc.unknown15C = 100;
    desc.unknown15E = 2000;
    g_netGame.Host(desc, true);
    SetState(GAME_STATE_NEXT_MAP);
}

void C_ConsoleState::StartJoining()                     // 0x00744540
{
    if (!g_gameProfile.m_mpSettings->dedicated) {
        SetState(GAME_STATE_JOIN_SESSION);
        return;
    }
    g_netGame.Connect(&SessionServer());
}
