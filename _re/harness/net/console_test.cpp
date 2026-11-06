// The console commands (part 10e-i: net/net_console_commands.cpp) with the game around them faked: every effect logged.
// MinGW; build_console.bat; run console_test.exe ("console: N checks, 0 failures"). Mutations: mutations/mutate_10e1.py.
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/campaign.h"
#include "game/console.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_state.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/team.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    g_log.push_back(line);
}
bool Logged(const char *line)
{
    for (const std::string &l : g_log)
        if (l == line)
            return true;
    return false;
}
// The log as one string.
std::string All()
{
    std::string all;
    for (const std::string &l : g_log)
        all += l + "|";
    return all;
}
}  // namespace

// ---- the game around ---------------------------------------------------------------------------------------------------

uint32_t g_gameRole;
int g_localPlayerSlot;
int g_netMaxFreq, g_netMaxPing, g_netMaxInactivity;
uint32_t g_graphFlags = 0xFF;
alignas(16) static unsigned char g_appStorage[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_appStorage); }
void C_GameApp::PrintMessage(const char *text) { Log("print %s", text); }
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    if (type == 4) {
        const char *line = reinterpret_cast<const char *>(a);
        Log("chat '%s' %d %d %d", line, *reinterpret_cast<const int *>(line + 0x104),
            *reinterpret_cast<const int *>(line + 0x108), line[0x10C]);
        return;                                     // (queued in the game: kept alive)
    }
    Log("send %d %d %d %d %d", type, to, static_cast<int>(a), b, reliable);
}
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    if (command == 4) {
        Log("post 4 %d %d %d", static_cast<int>(a), static_cast<int>(b), flush);
        return;
    }
    Log("post %d '%s' %d %d", command, reinterpret_cast<const char *>(a), static_cast<int>(b), flush);
    operator delete(reinterpret_cast<void *>(a));
}
C_GameTexts g_texts;
C_str C_GameTexts::Get(int id) const
{
    char text[16];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
alignas(16) static unsigned char g_consoleStorage[sizeof(C_Console)];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_consoleStorage); }
void C_Console::AddLine(const char *text) { Log("line %s", text); }
std::vector<std::string> g_typed;
char *C_Console::PopCommand()
{
    static char command[256];
    if (g_typed.empty())
        return nullptr;
    std::strcpy(command, g_typed.front().c_str());
    g_typed.erase(g_typed.begin());
    return command;
}
// The settings.
alignas(16) static unsigned char g_mpStorage[sizeof(S_MultiplayerSettings)];
S_MultiplayerSettings &g_mp = *reinterpret_cast<S_MultiplayerSettings *>(g_mpStorage);
void S_MultiplayerSettings::MakeSetup() { Log("make setup"); }
void S_MultiplayerSettings::DeleteSetup() { Log("delete setup"); }
void S_MultiplayerSettings::ApplySetupRules() { Log("apply rules %d", gameType); }
bool g_addMapResult = true;
bool S_MultiplayerSettings::AddMap(const char *name)
{
    Log("add map %s", name);
    return g_addMapResult;
}
bool S_MultiplayerSettings::RemoveMap(const char *name)
{
    Log("remove map %s", name);
    return true;
}
int GetVoiceCodecTextId(int codec) { return 0x100 + codec; }
// The game.
alignas(16) static unsigned char g_gameStorage[sizeof(C_game)];
alignas(16) static unsigned char g_missionStorage[sizeof(C_MpMissionDef)];
C_game *g_game;
C_game *GetGame() { return g_game; }
void C_game::StartNextRound() { Log("next round"); }
void ChangeMap(const char *name) { Log("change map %s", name ? name : "(next)"); }
S_MpObjective *C_MpMissionDef::GetMpObjective(int index) const
{
    return reinterpret_cast<S_MpObjective *>(m_objectives[static_cast<size_t>(index)]);
}
// The net game.
std::vector<S_NetPlayerState> *g_players = new std::vector<S_NetPlayerState>;
S_NetPlayerState::S_NetPlayerState() {}
S_NetPlayerState *C_NetGame::LockPlayers()
{
    Log("lock");
    return g_players->data();
}
void C_NetGame::UnlockPlayers() { Log("unlock"); }
int C_NetGame::FindPlayerByName(const char *name)
{
    Log("find %s", name);
    return std::strcmp(name, "Ann") == 0 ? 3 : -1;
}
void C_NetGame::KickPlayer(int slot, int reason) { Log("kick %d %d", slot, reason); }
void C_NetGame::KickPlayerByName(const char *name, int reason) { Log("kick %s %d", name, reason); }
int C_NetGame::Host(const S_NetSessionDesc &desc, bool firewall)
{
    Log("host %s %08x %08x %08x %08x %d %d %d", desc.name, desc.serviceProvider[0], desc.serviceProvider[1],
        desc.serviceProvider[2], desc.serviceProvider[3], desc.unknown15C, desc.unknown15E, firewall);
    return 0;
}
int C_NetGame::Connect(S_ServerEntry *server)
{
    Log("connect %d", server == reinterpret_cast<S_ServerEntry *>(g_mp.unknown17C));
    return 0;
}
void S_NetVote::Start(int k, int a) { Log("vote start %d %d", k, a); }
bool S_NetVote::Vote()
{
    Log("vote yes");
    return true;
}
void S_NetVote::CountYes() { Log("vote count"); }
void PostVote(int kind, int arg) { Log("post vote %d %d", kind, arg); }
bool g_admin;
bool IsPlayerAdmin(int slot)
{
    Log("admin? %d", slot);
    return g_admin;
}
S_MpMapGroup g_setupMaps[4];
S_MpMapGroup &C_MpGameSetup::GetCurrentMaps() { return g_setupMaps[gameType]; }
// The graphics: flags, DC and a real (hidden) window.
HWND g_window;
void __stdcall FakeSetFlags(void *, uint32_t flags) { Log("flags %x", flags); }
void __stdcall FakeUnknownDC(void *, int on) { Log("dc %d", on); }
HWND __stdcall FakeGetHWND(void *) { return g_window; }
void *g_graphVtable[128];
void *g_graphObject = g_graphVtable;
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graphObject); }
// The game's commands (part 10e-ii): their effects logged.
bool C_Controls::SetMouseScale(int x, int y, int z)
{
    Log("mouse %d %d %d", x, y, z);
    return true;
}
bool C_NetGame::SendChat(const char *text, bool team)
{
    Log("chat %s %d", text, team);
    return true;
}
C_Human *g_activePlayer;
C_Human *C_Team::GetActivePlayer() const { return g_activePlayer; }
void C_Team::HealMembers() { Log("heal"); }
int g_difficulty;
int C_GameProfile::GetDifficulty() const { return g_difficulty; }
C_Item *C_ItemsModule::GiveItem(int typeId, int, C_Actor *actor, int)
{
    Log("give %d %d", typeId, actor == g_activePlayer);
    return nullptr;
}
C_ItemDef *g_defs[300];
C_ItemDef *GetItemDef(int typeId) { return typeId < 300 ? g_defs[typeId] : nullptr; }
void C_game::Print(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    Log("game print %s", line);
}
void __attribute__((thiscall)) FakeHit(C_Human *self, int type, float amount, const S_vector *pos, const S_vector *dir,
                                       C_Actor *attacker, int flags)
{
    Log("hit %d %.1f %.2f %.2f %d %d", type, amount, pos->x + pos->y + pos->z, dir->x + dir->y + dir->z,
        attacker != nullptr, flags);
}
// A definition's GetBehaviour and GetAmmoItemId (g++'s vtable: one more destructor slot).
int __attribute__((thiscall)) FakeBehaviour(int *self) { return self[1]; }
int __attribute__((thiscall)) FakeAmmo(int *self) { return self[2]; }
int *MakeDef(int behaviour, int ammo)
{
    static void *vtable[16];
    vtable[0x10 / 4 + 1] = reinterpret_cast<void *>(&FakeBehaviour);
    vtable[0x18 / 4 + 1] = reinterpret_cast<void *>(&FakeAmmo);
    auto *def = new int[3];
    def[0] = reinterpret_cast<intptr_t>(vtable);
    def[1] = behaviour;
    def[2] = ammo;
    return def;
}
// The console state's state.
struct T_State : C_GameState {
    void SetPendingState(int state) override { Log("pending %d", state); }
};
T_State g_state;

// ---- the tests ---------------------------------------------------------------------------------------------------------

namespace {

void Reset(uint32_t role)
{
    g_log.clear();
    g_gameRole = role;
    g_gameProfile.m_mpSettings = &g_mp;
    g_mp.maps.clear();
    g_mp.gameType = 0;
    g_mp.dedicated = false;
    g_mp.unknown174 = 0;
    g_mp.unknown178 = 0;
    g_mp.password = C_str();
    g_mp.adminPassword = C_str();
    g_mp.port = 0;
    std::memset(g_mp.unknown17C, 0, sizeof g_mp.unknown17C);
    std::memset(g_mp.sessionName, 0, sizeof g_mp.sessionName);
    g_addMapResult = true;
    g_admin = false;
    g_game = reinterpret_cast<C_game *>(g_gameStorage);
    GetGameApp()->m_state = &g_state;
    g_localPlayerSlot = 2;
}
bool Run(const char *command)
{
    GetCommandProcessor();
    g_log.clear();
    return GetCommandProcessor()->Execute(command);
}
S_ServerEntry &Entry() { return *reinterpret_cast<S_ServerEntry *>(g_mp.unknown17C); }

void TestTokens()
{
    char word[256];
    int value = -7;
    const char *c = "  \t>hello -x -7 - \"two words\"  12x  MAXclients zzz";
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 0 && std::strcmp(word, "hello") == 0 && value == -7);
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 0 && std::strcmp(word, "x") == 0);
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 1 && value == 7);       // ('-' before a digit skipped)
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 1 && std::strcmp(word, "-") == 0 && value == 0);
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 0 && std::strcmp(word, "two words") == 0);
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 1 && value == 12);
    CHECK(ReadConsoleToken(&c, word, &value, g_votableCommands[0]) == 2 && value == 0 &&
          std::strcmp(word, "MAXclients") == 0);
    value = 5;
    CHECK(ReadConsoleToken(&c, word, &value, g_votableCommands[0]) == 0 && value == 5);
    CHECK(ReadConsoleToken(&c, word, &value, nullptr) == 3);
    const char *last = "dumpentity quit \"open";
    CHECK(ReadConsoleToken(&last, word, &value, g_votableCommands[0]) == 2 && value == 65);
    CHECK(ReadConsoleToken(&last, word, &value, g_votableCommands[0]) == 2 && value == 46);
    last = "\"open";
    const char *past = last;
    CHECK(ReadConsoleToken(&past, word, &value, nullptr) == 0 && std::strcmp(word, "open") == 0);
    // The table: names at 0x204 steps, the id at +0x200, the end -1.
    CHECK(std::strcmp(g_votableCommands[41], "kickplayer") == 0 && *reinterpret_cast<int *>(g_votableCommands[41] + 0x200) == 41);
    CHECK(*reinterpret_cast<int *>(g_votableCommands[14] + 0x200) == 0 && g_votableCommands[14][0] == 0);
    CHECK(*reinterpret_cast<int *>(g_votableCommands[66] + 0x200) == -1);
    const char *empty = "\"\"";
    CHECK(ReadConsoleToken(&empty, word, &value, g_votableCommands[0]) == 2 && value == 0);   // (the empty entry: id 0)
}

void TestProcessor()
{
    // Made once, the settings' set-up with it; deleted: the set-up too.
    Reset(GAME_ROLE_SERVER);
    C_CommandProcessor *first = GetCommandProcessor();
    CHECK(Logged("make setup") && GetCommandProcessor() == first);
    g_log.clear();
    {
        C_CommandProcessor other;
        CHECK(Logged("make setup"));
    }
    CHECK(Logged("delete setup"));
    // Not a keyword: the text, false; several on a line; the separators between them.
    CHECK(!Run("nonsense") && All() == "print T582|");
    CHECK(!Run("12") && All() == "print T582|");
    CHECK(Run("maxping 5 -maxfreq 6 > maxinactivity 7") && g_netMaxPing == 5 && g_netMaxFreq == 6 &&
          g_netMaxInactivity == 7000 && g_log.empty());
    CHECK(!Run("maxping 8 bad maxping 9") && g_netMaxPing == 8);
    CHECK(Run("dumpentity") && g_log.empty());
    CHECK(Run("vr12") && g_log.empty());
}

void TestRules()
{
    // A rule's value within limits, told to the clients; a client: nothing; no number: the value shown.
    Reset(GAME_ROLE_SERVER);
    struct {
        const char *word;
        int field, value, stored;
    } rules[] = {{"maxclients", 0, 33, 32},   {"maxclients", 0, 32, 32},  {"maxclients", 0, 7, 7},
                 {"pointlimit", 1, 12345, 12345}, {"roundlimit", 2, 100, 99},  {"roundlimit", 2, 99, 99},
                 {"roundlimit", 2, 0, 1},     {"roundlimit", 2, 1, 1},    {"roundcount", 3, -5, 5},
                 {"warmup", 4, 5941, 5940},   {"warmup", 4, 5940, 5940},  {"warmup", 4, 0, 1},
                 {"respawntime", 5, 5941, 5940}, {"respawntime", 5, 5940, 5940}, {"respawntime", 5, 2, 3},
                 {"respawntime", 5, 3, 3},    {"spawnprotection", 10, 16, 15}, {"spawnprotection", 10, 15, 15},
                 {"cooplives", 18, 100, 99},  {"cooplives", 18, 99, 99},  {"teamlives", 21, 4, 4},
                 {"coopdifficulty", 17, 9, 9}};
    for (const auto &r : rules) {
        char line[64];
        std::snprintf(line, sizeof line, "%s %d", r.word, r.value);
        CHECK(Run(line) && All() == "send 18 0 0 0 0|");
        CHECK((&g_mp.maxClients)[r.field] == r.stored);
    }
    // Cooperative: 6 players, difficulty 1 .. 4.
    g_mp.gameType = MP_GAME_COOPERATIVE;
    Run("maxclients 7");
    CHECK(g_mp.maxClients == 6);
    Run("maxclients 6");
    CHECK(g_mp.maxClients == 6);
    Run("coopdifficulty 5");
    CHECK(g_mp.coopDifficulty == 4);
    Run("coopdifficulty 4");
    CHECK(g_mp.coopDifficulty == 4);
    Run("coopdifficulty 0");
    CHECK(g_mp.coopDifficulty == 1);
    g_mp.gameType = 0;
    // Autorestart: only in a local game (no limit; sent).
    g_mp.autoRestart = 0;
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(Run("autorestart 3") && g_mp.autoRestart == 3 && All() == "send 18 0 0 0 0|");
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("autorestart 4") && g_mp.autoRestart == 3 && g_log.empty());
    // (Not local: the separators are not skipped after it: " -" is read as the next word's.)
    CHECK(Run("autorestart 4 maxping 44") && g_netMaxPing == 44);
    // A client: nothing; no number: the last value shown.
    g_gameRole = GAME_ROLE_CLIENT;
    g_mp.pointLimit = 77;
    CHECK(Run("pointlimit 5") && g_mp.pointLimit == 77 && g_log.empty());
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("pointlimit") && All() == "print pointlimit  0|" && g_mp.pointLimit == 77);
    CHECK(Run("maxping 9 pointlimit") && All() == "print pointlimit  9|");
}

void TestMaps()
{
    Reset(GAME_ROLE_SERVER);
    CHECK(Run("mapname Arnhem") && All() == "add map Arnhem|");
    g_addMapResult = false;
    CHECK(Run("mapname Oops") && All() == "add map Oops|print Map Oops doesn't exist!|");
    C_MpMap a(C_str("One"), C_str("d")), b(C_str("Two"), C_str("d"));
    g_mp.maps.push_back(&a);
    g_mp.maps.push_back(&b);
    CHECK(Run("mapname") && All() == "print maps: |print One|print Two|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("mapname Arnhem") && g_log.empty());
    // (Not read: the word is taken for the next keyword.)
    CHECK(!Run("removemap Arnhem") && All() == "print T582|");
    CHECK(Run("removemap") && g_log.empty());
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("removemap Arnhem") && All() == "remove map Arnhem|");
    CHECK(Run("removemap 7") && g_log.empty());
    g_game = nullptr;
    CHECK(Run("removemap Arnhem") && g_log.empty());
    g_game = reinterpret_cast<C_game *>(g_gameStorage);
    // The set-up's lists (the last first), blank lines between.
    alignas(16) static unsigned char setupStorage[sizeof(C_MpGameSetup)];
    g_mp.setup = reinterpret_cast<C_MpGameSetup *>(setupStorage);
    C_MpMap d1(C_str("D1"), C_str("d")), d2(C_str("D2"), C_str("d")), o(C_str("O"), C_str("d")), c(C_str("C"), C_str("d")),
        j(C_str("J"), C_str("d"));
    g_setupMaps[0].maps = {&d1, &d2};
    g_setupMaps[1].maps = {&o};
    g_setupMaps[2].maps = {&j};
    g_setupMaps[3].maps = {&c};
    CHECK(Run("listmaps") && All() == "print Deathmatch maps:|print D2|print D1|print |print Occupation maps:|print O|print |"
                                      "print Objectives maps:|print J|print |print Cooperative maps:|print C|");
    g_mp.maps.clear();
}

void TestStyleDomain()
{
    // The style: a local server's; else shown.
    Reset(GAME_ROLE_LOCAL | GAME_ROLE_SERVER);
    CHECK(Run("style cooperative") && g_mp.gameType == 3 && All() == "apply rules 3|");
    CHECK(Run("style deathmatch") && g_mp.gameType == 0);
    CHECK(Run("style occupation") && g_mp.gameType == 1);
    CHECK(Run("style") && All() == "print style: occupation|");
    CHECK(Run("style mapname") && All() == "print T585|" && g_mp.gameType == 1);
    CHECK(Run("style domain") && All() == "print T585|");
    CHECK(Run("style sessionname") && All() == "print T585|");
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("style objectives") && All() == "print style: occupation|" && g_mp.gameType == 1);
    // The domain.
    Reset(GAME_ROLE_SERVER);
    CHECK(Run("domain internet") && g_mp.unknown174 == 1 && g_log.empty());
    CHECK(Run("domain") && All() == "print domain: internet|");
    CHECK(Run("domain local") && g_mp.unknown174 == 0);
    CHECK(Run("domain") && All() == "print domain: local|");
    CHECK(Run("domain quit") && All() == "print T585|");
    CHECK(Run("domain objectives") && All() == "print T585|");
    CHECK(Run("domain sessionname") && All() == "print T585|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("domain internet") && g_mp.unknown174 == 0);
    // The session's name: a local server's.
    Reset(GAME_ROLE_LOCAL);
    CHECK(Run("sessionname \"My game\"") && std::strcmp(g_mp.sessionName, "My game") == 0);
    CHECK(Run("sessionname") && All() == "print sessionname: My game|");
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("sessionname Other") && std::strcmp(g_mp.sessionName, "My game") == 0);
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("sessionname Other") && std::strcmp(g_mp.sessionName, "My game") == 0);
}

void TestHelp()
{
    // Four keywords a line after the first five; the last short line lost.
    Reset(GAME_ROLE_LOCAL);
    CHECK(Run("help"));
    CHECK(g_log.size() == 17 && g_log[0] == "print Console keywords:");
    CHECK(g_log[1] == "print maxclients pointlimit roundlimit roundcount warmup ");
    CHECK(g_log[2] == "print respawntime allowrespawn friendlyfire autoteambalance ");
    CHECK(g_log.back() == "print gsm adpcm pcm none ");
}

void TestExec()
{
    Reset(GAME_ROLE_SERVER);
    const char *name = "console_test_exec.txt";
    // Each line run (blank lines passed over); a last line without its newline not run.
    FILE *f = std::fopen(name, "wt");
    std::fputs("maxping 11\nmaxfreq 12\n\nmaxping 13\n", f);
    std::fclose(f);
    CHECK(Run("exec console_test_exec.txt") && g_netMaxPing == 13 && g_netMaxFreq == 12);
    f = std::fopen(name, "wt");
    std::fputs("maxping 21\nmaxfreq 22", f);
    std::fclose(f);
    CHECK(Run("exec > console_test_exec.txt") && g_netMaxPing == 21 && g_netMaxFreq == 12);
    // A long line cut (Fix); no file: told.
    f = std::fopen(name, "wt");
    std::fputs("maxping 31 ", f);
    for (int i = 0; i < 200; ++i)
        std::fputc(' ', f);
    std::fputs("\nmaxfreq 32\n", f);
    std::fclose(f);
    Run("exec console_test_exec.txt");
    CHECK(g_netMaxPing == 31);
    std::remove(name);
    CHECK(Run("exec console_test_exec.txt") && All() == "print T586 console_test_exec.txt|");
}

void TestServerJoin()
{
    // Hosting: a local game's with maps: shown (dedicated: hidden), the state's pending host; joining too.
    Reset(GAME_ROLE_SERVER);
    CHECK(Run("server maxping 99") && g_netMaxPing != 99 && g_log.empty());
    CHECK(Run("join 1.2.3.4 maxping 99") && g_netMaxPing != 99 && g_log.empty());
    Reset(GAME_ROLE_LOCAL);
    CHECK(Run("server") && All() == "print Cannot create server! No map!|" && g_mp.sessionType != 2);
    C_MpMap a(C_str("One"), C_str("d"));
    g_mp.maps.push_back(&a);
    ShowWindow(g_window, SW_HIDE);
    CHECK(Run("server") && All() == "dc 1|pending 7|" && g_mp.sessionType == 2 && IsWindowVisible(g_window));
    g_mp.dedicated = true;
    CHECK(Run("server") && All() == "dc 0|pending 7|" && !IsWindowVisible(g_window));
    g_mp.dedicated = false;
    g_mp.maps.clear();
    CHECK(Run("join 1.2.3.4") && All() == "dc 1|pending 8|" && g_mp.sessionType == 1 && Entry().address == 0x04030201 &&
          IsWindowVisible(g_window));
    ShowWindow(g_window, SW_HIDE);
    // Dedicated: shown; a client's not changed.
    Reset(GAME_ROLE_SERVER);
    CHECK(Run("dedicated 1") && g_mp.dedicated);
    CHECK(Run("dedicated") && All() == "print dedicated  1|");
    CHECK(Run("dedicated 0") && !g_mp.dedicated);
    CHECK(Run("dedicated") && All() == "print dedicated  0|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("dedicated 1") && !g_mp.dedicated);
    // Port, password: the session's; shown.
    CHECK(Run("port 2302") && Entry().port == 2302 && g_mp.port == 2302 && g_mp.unknown174 == 1);
    CHECK(Run("port") && All() == "print port  2302|");
    CHECK(Run("port 65536") && All() == "print port  2302|" && g_mp.port == 2302);
    CHECK(Run("port 65535") && g_mp.port == 65535);
    CHECK(Run("password hush") && std::strcmp(g_mp.password.c_str(), "hush") == 0 && Entry().password);
    CHECK(Run("password") && All() == "print password: hush|");
    CHECK(Run("adminpass boss") && std::strcmp(g_mp.adminPassword.c_str(), "boss") == 0);
    CHECK(Run("adminpass 12") && std::strcmp(g_mp.adminPassword.c_str(), "boss") == 0);
}

void TestVotes()
{
    // A session's votes: kick (a slot or a name), nextmap, restart.
    Reset(GAME_ROLE_CLIENT);
    CHECK(Run("vote kickplayer 4") && All() == "post vote 41 4|");
    CHECK(Run("vote kickplayer Ann") && All() == "find Ann|post vote 41 3|");
    CHECK(Run("vote kickplayer Nobody") && All() == "find Nobody|");
    CHECK(Run("vote nextmap 2") && All() == "post vote 42 2|");
    CHECK(Run("vote restart 2") && All() == "post vote 43 2|");
    CHECK(Run("vote quit") && All() == "print Not valid voting command|");
    CHECK(Run("vote listplayers") && All() == "print Not valid voting command|");
    CHECK(Run("vote port") && All() == "print Not valid voting command|");
    CHECK(Run("vote 5") && All() == "print Not valid voting command|");
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("vote nextmap 2") && All() == "vote start 42 2|");
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(!Run("vote nextmap 2") && All() == "print T582|");
    CHECK(Run("vote") && g_log.empty());
    CHECK(Run("voteyes") && All() == "vote count|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("voteyes") && All() == "vote yes|");
}

void TestPlayers()
{
    Reset(GAME_ROLE_SERVER);
    // The named, connected players.
    C_NetGame &net = g_netGame;
    S_NetPlayerState p;
    std::memset(static_cast<void *>(&p), 0, sizeof p);
    g_players->assign(4, p);
    std::strcpy((*g_players)[0].name, "Ann");
    (*g_players)[0].connection = 5;
    std::strcpy((*g_players)[1].name, "Gone");
    (*g_players)[3].connection = 7;
    std::strcpy((*g_players)[3].name, "Dee");
    net.m_players = *g_players;
    CHECK(Run("listplayers") && All() == "lock|print 0. Ann|print 3. Dee|unlock|");
    // Kicks; a client's: nothing.
    CHECK(Run("kickplayer 2") && All() == "kick 2 0|");
    CHECK(Run("kickplayer Ann") && All() == "kick Ann 0|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(!Run("kickplayer 2") && All() == "print T582|");
    CHECK(Run("kickplayer") && g_log.empty());
    // Next map, restart: a session's server.
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("nextmap Arnhem") && All() == "change map Arnhem|");
    CHECK(Run("nextmap") && All() == "change map (next)|");
    g_game = nullptr;
    CHECK(Run("nextmap Arnhem") && g_log.empty());
    g_game = reinterpret_cast<C_game *>(g_gameStorage);
    CHECK(Run("restart") && All() == "next round|");
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(!Run("nextmap Arnhem") && All() == "print T582|");
    CHECK(Run("nextmap") && g_log.empty());
    CHECK(Run("restart") && g_log.empty());
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(!Run("nextmap Arnhem") && All() == "print T582|");
    CHECK(Run("nextmap") && g_log.empty());
    CHECK(Run("restart") && g_log.empty());
    // Quit: not a session's client.
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("quit") && All() == "print Closing game. Wait please.|pending 4|");
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(Run("quit") && All() == "print Closing game. Wait please.|pending 4|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("quit") && g_log.empty());
}

void TestVoiceAdmin()
{
    Reset(GAME_ROLE_SERVER);
    const char *codecs[] = {"vr12", "sc03", "sc06", "truespeech", "gsm", "adpcm", "pcm"};
    for (int i = 0; i < 7; ++i) {
        char line[32];
        std::snprintf(line, sizeof line, "voicechat %s", codecs[i]);
        Entry().voice = false;
        CHECK(Run(line) && g_mp.unknown178 == i + 1 && Entry().voice && g_log.empty());
    }
    CHECK(Run("voicechat none") && g_mp.unknown178 == 0 && !Entry().voice);
    CHECK(Run("voicechat") && All() == "print voicechat T100|");
    g_mp.unknown178 = 3;
    CHECK(Run("voicechat") && All() == "print voicechat T103|");
    CHECK(Run("voicechat quit") && All() == "print Not valid voice codec|" && g_mp.unknown178 == 3 && Entry().voice);
    CHECK(Run("voicechat dumpentity") && All() == "print Not valid voice codec|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Run("voicechat gsm") && g_mp.unknown178 == 3 && g_log.empty());
    // The admin login's and commands' texts; only an admin's commands.
    CHECK(Run("adminlogin hush") && All() == "post 7 'hush' 0 0|");
    CHECK(Run("adminlogin 12") && g_log.empty());
    CHECK(Run("svcmd") && All() == "admin? 2|print You don't have admin rights!|");
    g_admin = true;
    CHECK(Run("svcmd maxping 41") && All() == "admin? 2|post 8 ' maxping 41' 0 0|" && g_netMaxPing == 41);
    std::string longer = "adminlogin ";
    longer.append(200, 'x');
    CHECK(Run(longer.c_str()) && g_log[0].size() == std::strlen("post 7 '") + 0x7F + std::strlen("' 0 0"));
    // The limits shown.
    g_netMaxPing = 1;
    g_netMaxFreq = 2;
    g_netMaxInactivity = 3000;
    CHECK(Run("maxping") && All() == "print maxping:  1|");
    CHECK(Run("maxfreq") && All() == "print maxfreq:  2|");
    CHECK(Run("maxinactivity") && All() == "print maxinactivity:  3|");
    CHECK(Run("maxfreq 0") && g_netMaxFreq == 2);
    // The admin's chat: the rest of the line; a server without a player: on its console.
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Run("asay hello there") && All() == "chat ' hello there' -1 0 0|line ADMIN:  hello there|");
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    CHECK(Run("asay hi") && All() == "chat ' hi' -1 0 0|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(!Run("asay hi") && All() == "print T582|");
    CHECK(Run("asay") && g_log.empty());
    longer = "asay ";
    longer.append(400, 'y');
    g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
    CHECK(Run(longer.c_str()) && g_log[0].size() == std::strlen("chat ' ") + 0x102 + std::strlen("' -1 0 0"));
}

void TestObjectives()
{
    Reset(GAME_ROLE_SERVER);
    auto *mission = reinterpret_cast<C_MpMissionDef *>(g_missionStorage);
    alignas(16) static unsigned char o1[sizeof(S_MpObjective)], o2[sizeof(S_MpObjective)];
    auto *done = reinterpret_cast<S_MpObjective *>(o1);
    auto *open = reinterpret_cast<S_MpObjective *>(o2);
    done->status = OBJECTIVE_COMPLETED;
    done->alliedText = 0x11;
    open->status = OBJECTIVE_OPEN;
    open->alliedText = 0x22;
    mission->m_objectives = {done, open};
    g_game->m_mission = mission;
    CHECK(Run("listobjectives") && All() == "print OK - T11|print    - T22|");
}

// 1 in, 0 out, -1 not a range.
int Range(int number, const char *range)
{
    bool in = false;
    return ConsoleValueInRange(number, range, &in) ? in : -1;
}

void TestRanges()
{
    CHECK(Range(5, "*") == 1 && Range(-5, " * ") == 1);
    CHECK(Range(1, "0,1") == 1 && Range(0, "0,1") == 1 && Range(2, "0,1") == 0 && Range(-1, "0,1") == 0);
    CHECK(Range(12, " 12 ") == 1 && Range(123, "123") == 1 && Range(13, "12") == 0 && Range(9, "9") == 1 && Range(0, "0") == 1);
    CHECK(Range(-12, "-12") == 1 && Range(12, "-12") == 0);
    CHECK(Range(3, "1:5") == 1 && Range(1, "1:5") == 1 && Range(5, "1:5") == 1 && Range(0, "1:5") == 0 && Range(6, "1:5") == 0);
    CHECK(Range(3, "1:") == -1 && Range(3, "1:*") == -1 && Range(3, "") == -1 && Range(3, "x") == -1 && Range(3, ",") == -1);
    CHECK(Range(3, "<4") == 1 && Range(4, "<4") == 0 && Range(4, "<=4") == 1 && Range(5, "<=4") == 0);
    CHECK(Range(5, ">4") == 1 && Range(4, ">4") == 0 && Range(4, ">=4") == 1 && Range(3, ">=4") == 0);
    CHECK(Range(9, "%3") == 1 && Range(10, "%3") == 0);
    CHECK(Range(3, "!3") == 0 && Range(4, "!3") == 1 && Range(3, "!!3") == 1);
    CHECK(Range(5, ">3&<7") == 1 && Range(2, ">3&<7") == 0 && Range(8, ">3&<7") == 0 && Range(5, "!5&<7") == 0);
    CHECK(Range(3, "!5&<7") == 1 && Range(8, "!5&<7") == 0 && Range(3, ">1&<7&!3") == 0 && Range(4, ">1&<7&!3") == 1);
    CHECK(Range(5, ">3&") == -1 && Range(5, "!") == -1);
    CHECK(Range(7, "1,2,7") == 1 && Range(2, "1,2,x") == 1 && Range(9, "1,2,x") == -1 && Range(9, "1 2") == -1);
    CHECK(Range(1, "1:2,5") == 1 && Range(5, "1:2,5") == 1 && Range(4, "1:2,5") == 0);
    CHECK(Range(4, "*,x") == 1);
}

int Line(const char *line)
{
    g_log.clear();
    return ExecuteConsoleLine(line);
}

C_Human *HitHuman(int netId)
{
    static void *vtable[128];
    alignas(16) static unsigned char storage[sizeof(C_Human)];
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&FakeHit);
    *reinterpret_cast<void ***>(storage) = vtable;
    auto *human = reinterpret_cast<C_Human *>(storage);
    human->m_netId = netId;
    return human;
}

void TestGameLines()
{
    Reset(GAME_ROLE_LOCAL);
    g_difficulty = 1;
    g_activePlayer = nullptr;
    // Chat: ".text", "x,text", say, sayteam (spaces trimmed; quotes dropped; cut to 100).
    CHECK(Line(".hello ") == 0 && All() == "chat hello 0|");
    CHECK(Line("x,hello") == 0 && All() == "chat ,hello 1|");
    CHECK(Line("say  \"a b\"  ") == 0 && All() == "chat a b 0|");
    CHECK(Line("sayteam a \"b") == 0 && All() == "chat a \"b 1|");
    CHECK(Line("say \"") == 0 && All() == "chat  0|");
    std::string longer = "say ";
    longer.append(300, 'z');
    CHECK(Line(longer.c_str()) == 0 && g_log[0].size() == std::strlen("chat ") + 100 + std::strlen(" 0"));
    // The sensitivity (a number).
    CHECK(Line("sensitivity 7") == 0 && All() == "mouse 7 7 1|");
    CHECK(Line("sensitivity x") == 4 && g_log.empty());
    CHECK(Line("sensitivity") == 4 && g_log.empty());
    // Not one of them: the command processor's (the line rebuilt), not too long.
    CHECK(Line("maxping   71") == 0 && g_netMaxPing == 71);
    CHECK(Line("Say x") == 0 && All() == "print T582|");
    CHECK(Line("") == 0 && All() == "print T582|");
    longer = "maxping 72";
    longer.append(489, ' ');
    CHECK(Line(longer.c_str()) == 0 && g_netMaxPing == 72);
    longer.append(1, ' ');
    g_netMaxPing = 0;
    CHECK(Line(longer.c_str()) == 0 && g_netMaxPing == 0);
    // The variables (0, 1): set, shown; another value refused; read back; others -1.
    CHECK(GetCheatValue(CHEAT_SNIPER) == 0 && GetCheatValue(CHEAT_HYDROSHOCK) == 0 && GetCheatValue(CONSOLE_NO_GI) == 0);
    CHECK(GetCheatValue(CONSOLE_HEAL) == -1 && GetCheatValue(CONSOLE_SAY) == -1);
    CHECK(Line("sniper 1") == 0 && GetCheatValue(CHEAT_SNIPER) == 1 && g_log.empty());
    CHECK(Line("sniper 2") == 5 && GetCheatValue(CHEAT_SNIPER) == 1);
    CHECK(Line("sniper") == 0 && All() == "game print sniper = 1|");
    CHECK(Line("sniper 0") == 0 && GetCheatValue(CHEAT_SNIPER) == 0);
    CHECK(Line("no_gi 1") == 0 && GetCheatValue(CONSOLE_NO_GI) == 1 && GetCheatValue(CHEAT_HYDROSHOCK) == 0);
    CHECK(Line("hydroshock 1") == 0 && GetCheatValue(CHEAT_HYDROSHOCK) == 1);
    CHECK(Line("no_gi 0") == 0 && Line("hydroshock 0") == 0);
    // The cheats: a local game below the top difficulty.
    g_itemsModule.m_infiniteAmmo = false;
    CHECK(Line("happyammo 1") == 0 && g_itemsModule.m_infiniteAmmo);
    CHECK(Line("happyammo 0") == 0 && !g_itemsModule.m_infiniteAmmo);
    CHECK(Line("heal 1") == 0 && All() == "heal|");
    CHECK(Line("anorexy 1") == 0 && g_log.empty());
    g_difficulty = 4;
    CHECK(Line("happyammo 1") == 0 && !g_itemsModule.m_infiniteAmmo);
    CHECK(Line("heal 1") == 0 && g_log.empty());
    g_difficulty = 1;
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(Line("happyammo 1") == 0 && !g_itemsModule.m_infiniteAmmo);
    CHECK(Line("heal 1") == 0 && g_log.empty());
    CHECK(Line("giveitem MP40") == 0 && g_log.empty());
    g_gameRole = GAME_ROLE_LOCAL;
    // Kill: the local game's player hit; a session's: asked of the server when it is the local slot.
    CHECK(Line("kill") == 0 && g_log.empty());
    g_activePlayer = HitHuman(2);
    CHECK(Line("kill") == 0 && All() == "hit 6 -1.0 0.00 0.00 0 0|");
    g_gameRole = GAME_ROLE_CLIENT;
    CHECK(Line("kill") == 0 && g_log.size() == 1 && g_log[0].compare(0, 7, "post 4 ") == 0);
    g_activePlayer->m_netId = 3;
    CHECK(Line("kill") == 0 && g_log.empty());
    g_activePlayer->m_netId = -1;
    g_localPlayerSlot = -1;
    CHECK(Line("kill") == 0 && g_log.empty());
    g_localPlayerSlot = 2;
    g_gameRole = GAME_ROLE_LOCAL;
    // Items: by name (any case); a weapon with 6 of its ammo, thrown things 6 in all, others 1.
    int *mp40 = MakeDef(1, 77);
    int *grenade = MakeDef(ITEM_BEHAVIOUR_THROWN, -1);
    int *charge = MakeDef(ITEM_BEHAVIOUR_STICKY_CHARGE, -1);
    int *medikit = MakeDef(3, -1);
    g_defs[18] = reinterpret_cast<C_ItemDef *>(mp40);
    g_defs[60] = reinterpret_cast<C_ItemDef *>(grenade);
    g_defs[67] = reinterpret_cast<C_ItemDef *>(charge);
    g_defs[54] = reinterpret_cast<C_ItemDef *>(medikit);
    g_defs[231] = reinterpret_cast<C_ItemDef *>(medikit);
    CHECK(Line("giveitem mp40") == 0 && All() == "give 18 1|give 77 1|give 77 1|give 77 1|give 77 1|give 77 1|give 77 1|");
    CHECK(Line("giveitem GRENADE36") == 0 && All() == "give 60 1|give 60 1|give 60 1|give 60 1|give 60 1|give 60 1|");
    CHECK(Line("giveitem explosivesbag") == 0 && g_log.size() == 6);
    CHECK(Line("giveitem MEDIKIT") == 0 && All() == "give 54 1|");
    CHECK(Line("giveitem AQUALUNG") == 0 && All() == "give 231 1|");
    CHECK(Line("giveitem PARABELLUM") == 0 && g_log.empty());            // (no definition)
    CHECK(Line("giveitem nonsense") == 0 && g_log.empty());
    // Every name's type; an ammo of type 0 counts; a thrown item's ammo -1.
    const struct {
        int id;
        const char *name;
    } items[] = {{0, "PARABELLUM"}, {1, "ENFIELD"}, {2, "COLT"}, {3, "TAISHO"}, {5, "TOKAREV"}, {4, "K98"}, {6, "LEE"},
                 {7, "DELISLE"}, {8, "MOSIN"}, {10, "GARAND"}, {11, "CARBINE"}, {12, "ARISAKA"}, {14, "SPRINGFIELD"},
                 {13, "K98OPT"}, {15, "SPRINGFIELDOPT"}, {16, "ARISAKAOPT"}, {17, "LEEOPT"}, {18, "MP40"}, {19, "ZB26"},
                 {20, "THOMPSON"}, {21, "STENGUN"}, {22, "STENGUNSIL"}, {23, "SHOTGUN"}, {24, "SPAGIN"}, {25, "PPS"},
                 {26, "MP44"}, {28, "BAR"}, {29, "BRENGUN"}, {31, "DEGTYAREV"}, {38, "PANZERFAUST"}, {39, "BAZOOKA"},
                 {53, "MEDIKITBIG"}, {54, "MEDIKIT"}, {99, "BINOCULARS"}, {229, "CAMERA"}, {56, "KNIFEGER"},
                 {57, "KNIFESAS"}, {59, "WIRECUTTER"}, {60, "GRENADE36"}, {62, "GRENADE69"}, {61, "GRENADEGER"},
                 {63, "GRENADEJAP"}, {66, "CLUMMYBOMB"}, {67, "EXPLOSIVESBAG"}, {68, "DYNAMITE"}, {70, "MINE"},
                 {71, "TANKMINE"}, {251, "MAGNETICMINE"}, {95, "BACKPACKRUS"}, {97, "BACKPACKGER"}, {101, "BACKPACKGB1"},
                 {222, "BACKPACKGB2"}, {228, "BACKPACKGB3"}, {231, "AQUALUNG"}};
    for (const auto &item : items) {
        g_defs[item.id] = reinterpret_cast<C_ItemDef *>(medikit);
        char line[64], expected[32];
        std::snprintf(line, sizeof line, "giveitem %s", item.name);
        std::snprintf(expected, sizeof expected, "give %d 1|", item.id);
        CHECK(Line(line) == 0 && All() == expected);
        g_defs[item.id] = nullptr;
    }
    int *ammoZero = MakeDef(1, 0);
    g_defs[10] = reinterpret_cast<C_ItemDef *>(ammoZero);
    CHECK(Line("giveitem GARAND") == 0 && g_log.size() == 7 && g_log[1] == "give 0 1");
    g_defs[10] = nullptr;
    // One-letter lines: not ".x" nor "x,".
    CHECK(Line(",") == 0 && All() == "print T582|");
    CHECK(Line("say \"\"") == 0 && All() == "chat  0|");
    g_activePlayer = nullptr;
}

void TestConsoleState()
{
    Reset(GAME_ROLE_LOCAL);
    // The command kept (cut to its buffer); the type; started: running, the graphics' flag 0x20 off.
    C_ConsoleState none(nullptr);
    CHECK(none.m_command[0] == 0 && !none.IsShown() && none.GetTypeId() == GAME_STATE_TYPE_CONSOLE && none.Stop());
    std::string longer(0x400, 'c');
    C_ConsoleState cut(longer.c_str());
    CHECK(std::strlen(cut.m_command) == 0x1FF);
    CHECK(none.Start(nullptr) && none.GetState() == GAME_STATE_RUNNING && none.m_mode == GAME_STATE_RUNNING &&
          All() == "flags df|");
    // The banner: shown; the typed commands then only run.
    g_log.clear();
    g_typed = {"maxping 51", "maxfreq 52"};
    none.Update(0);
    CHECK(g_netMaxPing != 51 && g_typed.size() == 2);
    none.ShowBanner();
    CHECK(none.IsShown() && All() == "line HD2 console|line Type 'help' for keywords list|");
    none.Update(0);
    CHECK(g_netMaxPing == 51 && g_netMaxFreq == 52 && g_typed.empty());
    // The first command, once; failing: the game ends.
    C_ConsoleState first("maxping 61");
    first.Update(0);
    CHECK(g_netMaxPing == 61 && first.m_command[0] == 0);
    C_ConsoleState bad("wrong");
    g_log.clear();
    bad.Update(0);
    CHECK(Logged("print T582") && Logged("pending 4") && bad.m_command[0] == 0);
    // Hosting: a dedicated server hosts (the session's name, TCP/IP, 100, 2000, the firewall), the next map's state;
    // else the state goes on.
    C_ConsoleState host(nullptr);
    host.SetState(GAME_STATE_HOST_SESSION);
    g_log.clear();
    host.Update(0);
    CHECK(g_log.empty() && host.GetState() == GAME_STATE_HOST_SESSION && host.m_mode == GAME_STATE_HOST_SESSION);
    g_mp.dedicated = true;
    std::strcpy(g_mp.sessionName, "Srv");
    host.Update(0);
    CHECK(All() == "host Srv ebfe7ba0 11d2628d 60000fae 1114b097 100 2000 1|" && host.GetState() == GAME_STATE_NEXT_MAP);
    // Joining: a dedicated client connects, running; else the state stays.
    C_ConsoleState join(nullptr);
    join.SetState(GAME_STATE_JOIN_SESSION);
    g_log.clear();
    join.Update(0);
    CHECK(All() == "connect 1|" && join.GetState() == GAME_STATE_RUNNING && join.m_mode == GAME_STATE_JOIN_SESSION);
    g_mp.dedicated = false;
    C_ConsoleState join2(nullptr);
    join2.SetState(GAME_STATE_JOIN_SESSION);
    g_log.clear();
    join2.Update(0);
    CHECK(g_log.empty() && join2.GetState() == GAME_STATE_JOIN_SESSION);
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (void *&slot : g_graphVtable)
        slot = nullptr;
    g_graphVtable[2] = reinterpret_cast<void *>(&FakeSetFlags);
    g_graphVtable[0x48 / 4] = reinterpret_cast<void *>(&FakeGetHWND);
    g_graphVtable[0xDC / 4] = reinterpret_cast<void *>(&FakeUnknownDC);
    g_window = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    TestTokens();
    TestProcessor();
    TestRules();
    TestMaps();
    TestStyleDomain();
    TestHelp();
    TestExec();
    TestServerJoin();
    TestVotes();
    TestPlayers();
    TestVoiceAdmin();
    TestObjectives();
    TestRanges();
    TestGameLines();
    TestConsoleState();
    DestroyWindow(g_window);
    std::printf("console: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
