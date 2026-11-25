// Net part 10: the game commands (net/net_commands.cpp: the server's HandleServerCommand, the client's
// HandleClientCommand and its whole game, ReadFullState) against the original executable's. MinGW; build_cmds.bat;
// runs cmds_diff.exe ("cmds_diff: N checks, 0 failures").
//
// Each round makes a random world in both programs' layouts: the net game's players (the same bytes; the humans each
// side's own), the role and local slot, the rules (one block both read, its admin password a string both can read),
// the app's state (a game, the in-game menu or another: the test's object with each program's vtable layout), the game
// (or none), the humans (health, controller type and controller, their uniforms), the squad's first soldier, the
// actors by index, the soldier profiles. Then commands, their bits written by the test (each command's fields, then
// random bits) and read by both: the server's 0..12 and the client's 0..24 (the whole game's too). What they reach is
// the test's on both sides, logging alike: the game's and the app's calls, the menus, the loading screen, the campaign,
// the net game's player calls, the votes, the soldier helpers, the stream readers of other modules, the clock. After
// each command the logs, the bits read and the state are compared. The port's NetReadSoldierState is net_messages.cpp's
// (compared by net part 9), copied here. HD2_CMDS_ROUNDS: rounds (default 20,000; HD2_CMDS_QUICK: 5,000);
// HD2_CMDS_SEED; HD2_CMDS_FIRST_CHANCE.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#include <winsock2.h>
#include <windows.h>
#include <malloc.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/actors_module.h"
#include "game/campaign.h"
#include "game/console.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_menu.h"
#include "game/game_state.h"
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
#include "net/bitstream.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#ifdef SendMessage
#undef SendMessage
#endif

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

static_assert(sizeof(S_NetPlayerState) == 0xC4, "the player state's size");

// The port's allocations zeroed, as the original's (the test's calloc).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new[](size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete[](void *p) noexcept { std::free(p); }

// ---- the logs (both sides) ----
int g_side;
bool g_faking;
std::vector<std::string> g_log[2];
void Log(const std::string &s)
{
    if (g_faking)
        g_log[g_side].push_back(s);
}
std::string N(long long v) { return std::to_string(v); }
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }
std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}
std::string F(float f)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", static_cast<double>(f));
    return b;
}
std::string VFormat(const char *format, va_list args)
{
    char line[1024];
    std::vsnprintf(line, sizeof line, format, args);
    return line;
}

// ---- the round's script (both sides answer from it) ----
constexpr int HUMANS = 16;
struct S_HumanAnswers {
    int health;
    int controllerType;
    bool controller;
};
struct S_StateAnswers {
    int typeId, state, pending;
    bool start;
};
struct S_Script {
    bool game = true;
    S_HumanAnswers humans[HUMANS];
    S_StateAnswers app, menu, made;     // the app's state, the menu OpenGameMenu gives, the game case 0 makes
    std::map<int, bool> actors;
    uint32_t now = 0;
    int playerHuman = -1;               // GetPlayerHuman
    int localHuman = -1;                // GetLocalHuman
    int created = -1;                   // CreateSoldier's human (-1: none)
    int countAxis = 0, countAllied = 0; // CountTeams
    bool balanced = true;               // TeamsBalanced
    int playerTeam = 0;                 // GetPlayerTeam
    int rank = 0;                       // GetPlayerStats' rank
    bool profile = true;                // the soldier profiles exist
    bool utf8 = false;                  // HasUtf8Sequences
    int squadTeam = 0;
    bool squad = true;
    int uniformType = -1, uniformPresent = 0;
    bool itemDef = true;
} g_script;

// ---- the stream readers of other modules (both: the same bits read, logged) ----
std::string ReadSome(C_BitStream *stream, int bits)
{
    return Hex(stream->ReadBits(bits));
}

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_localPlayerSlot;
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

C_NetGame &PNet() { return g_netGame; }
// (net_game.cpp's, as there: the original's 0x00725e10 sets these.)
S_NetPlayerState::S_NetPlayerState()
{
    team = 2;
    pendingTeam = 2;
    keyQuery = -1;
    unknownB8 = 50;
}

extern "C" DWORD WINAPI FakeTimeGetTime()
{
    Log("timeGetTime");
    return g_script.now;
}

void C_NetLock::Enter() { Log("Enter players"); }
void C_NetLock::Leave() { Log("Leave players"); }
S_NetPlayerState *C_NetGame::LockPlayers()
{
    m_playersLock.Enter();
    return m_players.data();
}
void C_NetGame::UnlockPlayers() { m_playersLock.Leave(); }
int *C_NetGame::GetPlayerStats(int slot)
{
    Log("GetPlayerStats " + N(slot));
    static int stats[9];
    stats[NET_STAT_RANK] = g_script.rank;
    return stats;
}
void C_NetGame::CountTeams(int *axis, int *allies, bool pending, bool inGame)
{
    Log("CountTeams " + N(pending) + " " + N(inGame));
    *axis = g_script.countAxis;
    *allies = g_script.countAllied;
}
bool C_NetGame::TeamsBalanced(int axis, int allied, bool pending)
{
    Log("TeamsBalanced " + N(axis) + " " + N(allied) + " " + N(pending));
    return g_script.balanced;
}
void C_NetGame::SetPlayerTeam(int slot, int team, bool pending) { Log("SetPlayerTeam " + N(slot) + " " + N(team) + " " + N(pending)); }
int C_NetGame::GetPlayerTeam(int slot, bool pending)
{
    Log("GetPlayerTeam " + N(slot) + " " + N(pending));
    return g_script.playerTeam;
}
int C_NetGame::FindPlayer(int connection)
{
    Log("FindPlayer " + N(connection));
    for (size_t i = 0; i < m_players.size(); ++i)
        if (m_players[i].connection == connection)
            return static_cast<int>(i);
    return -1;
}
const char *C_NetGame::GetPlayerName(int slot)
{
    Log("GetPlayerName " + N(slot));
    if (slot < 0 || static_cast<size_t>(slot) >= m_players.size() || !m_players[static_cast<size_t>(slot)].name[0])
        return nullptr;
    return m_players[static_cast<size_t>(slot)].name;
}
void C_NetGame::KickPlayer(int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void C_NetGame::ReadScores(C_BitStream *stream) { Log("ReadScores " + ReadSome(stream, 9)); }
void C_NetGame::OnConnected() { Log("OnConnected"); }
void S_NetStats::Read(C_BitStream *stream) { Log("S_NetStats::Read " + ReadSome(stream, 11)); }
void S_NetVote::Start(int kind_, int arg_) { Log("S_NetVote::Start " + N(kind_) + " " + N(arg_)); }
void S_NetVote::Begin(int kind_, int arg_) { Log("S_NetVote::Begin " + N(kind_) + " " + N(arg_)); }
void S_NetVote::CountYes() { Log("S_NetVote::CountYes"); }
void PrintVoteResult(bool passed) { Log("PrintVoteResult " + N(passed)); }

alignas(16) unsigned char g_pApp[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_pApp); }
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    std::string extra;
    if (type == 4) {
        const char *line = reinterpret_cast<const char *>(a);
        extra = " " + Str(line) + " " + N(*reinterpret_cast<const int *>(line + 0x104)) + " " + N(*reinterpret_cast<const int *>(line + 0x108)) +
                " " + N(line[0x10C]);
        operator delete(const_cast<char *>(line));
        a = 0;
    }
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(a) + " " + N(b) + " " + N(reliable) + extra);
}
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
void C_GameApp::PrintMessage(const char *text) { Log("AppPrintMessage " + Str(text)); }

// The game (GetGame): its calls logged.
alignas(16) unsigned char g_pGame[sizeof(C_game)];
C_game *GetGame() { return g_script.game ? reinterpret_cast<C_game *>(g_pGame) : nullptr; }
void C_game::EndMission(int unused) { Log("EndMission " + N(unused)); }
void C_game::OnPlayerLeft(int connection) { Log("OnPlayerLeft " + N(connection)); }
void C_game::RestartMission(bool newRound) { Log("RestartMission " + N(newRound)); }
void C_game::ApplyObjectiveStatus(int objective, int status) { Log("ApplyObjectiveStatus " + N(objective) + " " + N(status)); }
void C_game::OnWarmupFinished() { Log("OnWarmupFinished"); }
void C_game::OnWarmupStarted() { Log("OnWarmupStarted"); }
void C_game::OnRoundOver(int winner) { Log("OnRoundOver " + N(winner)); }
void C_game::OnRespawnScreen(bool cancel) { Log("OnRespawnScreen " + N(cancel)); }
void C_game::OnSessionClosing(int reason) { Log("OnSessionClosing " + N(reason)); }
void C_game::ReadObjectiveStates(C_BitStream *msg) { Log("ReadObjectiveStates " + ReadSome(msg, 5)); }
void C_game::SetCameraMode(int mode) { Log("SetCameraMode " + N(mode)); }
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color.x) + " " + F(color.y) + " " + F(color.z) + " " + VFormat(format, args));
    va_end(args);
}
void ShowGameMenuMessage(int textId) { Log("ShowGameMenuMessage " + N(textId)); }
void LoadNextMap() { Log("LoadNextMap"); }

C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    char text[32];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
bool HasUtf8Sequences(const char *text)
{
    Log("HasUtf8Sequences " + Str(text));
    return g_script.utf8;
}
void S_MultiplayerSettings::ReadRules(C_BitStream *stream) { Log("ReadRules " + ReadSome(stream, 7)); }
void S_MultiplayerSettings::ReadCurrentMap(C_BitStream *stream) { Log("ReadCurrentMap " + ReadSome(stream, 6)); }
void S_MultiplayerSettings::Reset() { Log("S_MultiplayerSettings::Reset"); }
bool C_Campaign::SetupMultiplayerMission()
{
    Log("SetupMultiplayerMission");
    return true;
}
alignas(16) unsigned char g_pMission[0x100];
C_MissionDef *C_Campaign::GetCurrentMission()
{
    Log("GetCurrentMission");
    return reinterpret_cast<C_MissionDef *>(g_pMission);
}
void C_LoaderScreen::AddPlayer(const char *text, int team) { Log("AddPlayer " + Str(text) + " " + N(team)); }
void C_GameMenu::AddChatLine(int team, const char *name, const char *text) { Log("AddChatLine " + N(team) + " " + Str(name) + " " + Str(text)); }
void C_GameMenu::SetTeam(int team) { Log("C_GameMenu::SetTeam " + N(team)); }
alignas(16) unsigned char g_pProcessor[0x10];
C_CommandProcessor *GetCommandProcessor() { return reinterpret_cast<C_CommandProcessor *>(g_pProcessor); }
bool C_CommandProcessor::Execute(const char *command)
{
    Log("Execute " + Str(command));
    return true;
}
void SplitSoldierStacks(S_SoldierState &state) { Log("SplitSoldierStacks " + Str(state.name.c_str())); }
void S_SquadSoldier::ClearItems() { Log("ClearItems"); }
namespace ModuleManager {
void BroadcastReadState(C_BitStream *msg) { Log("BroadcastReadState " + ReadSome(msg, 4)); }
}  // namespace ModuleManager
void C_TemporaryActorsList::ReadState(C_BitStream *msg) { Log("TemporaryActors::ReadState " + ReadSome(msg, 3)); }

// The soldier profiles (Get by index; their stats), the stats by rank.
alignas(16) unsigned char g_pProfile[sizeof(S_SoldierProfile)];
S_SoldierProfile *C_HumanStore::Get(int index) const
{
    Log("C_HumanStore::Get " + N(index));
    return g_script.profile ? reinterpret_cast<S_SoldierProfile *>(g_pProfile) : nullptr;
}
void C_HumanStore::GetStats(int *stats, int index, int level) const
{
    Log("C_HumanStore::GetStats " + N(index) + " " + N(level) + " " + N(stats[0]) + " " + N(stats[1]));
    stats[2] += level;
}
// The items' definitions (a VIP's uniform weight).
alignas(16) unsigned char g_pDef[sizeof(C_ItemDef)];
C_ItemDef *GetItemDef(int typeId)
{
    // (The original's is inline: the table by index, 0..499.)
    return typeId >= 0 && typeId < 500 && g_script.itemDef ? reinterpret_cast<C_ItemDef *>(g_pDef) : nullptr;
}

// The humans and their controllers.
alignas(16) unsigned char g_pHumans[HUMANS][sizeof(C_Human)];
alignas(16) unsigned char g_pControllers[HUMANS][sizeof(C_RemoteController)];
void *g_pHumanVtable[0x80], *g_pControllerVtable[0x80];
C_Human *PHuman(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_pHumans[i]); }
int PHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_pHumans[i])
            return i;
    return p ? 99 : -1;
}
C_Human *C_NetGame::GetPlayerHuman(int slot)
{
    Log("GetPlayerHuman " + N(slot));
    return PHuman(g_script.playerHuman);
}
C_Human *C_NetGame::GetLocalHuman()
{
    Log("GetLocalHuman");
    return PHuman(g_script.localHuman);
}
int C_Human::GetControllerType() const
{
    const int i = PHumanIndex(this);
    Log("GetControllerType " + N(i));
    return g_script.humans[i].controllerType;
}
void C_Human::SetChatText(const char *text) { Log("SetChatText " + N(PHumanIndex(this)) + " " + Str(text)); }
void C_Human::RequestRespawn() { Log("RequestRespawn " + N(PHumanIndex(this))); }
void C_Human::ApplyRemoteHit(int type, int zone, int health, int killerId, const S_vector &pos, const S_vector &dir)
{
    Log("ApplyRemoteHit " + N(PHumanIndex(this)) + " " + N(type) + " " + N(zone) + " " + N(health) + " " + N(killerId) + " " + F(pos.x) + " " +
        F(dir.z));
}
void __attribute__((thiscall)) PHumanHit(void *self, int type, float amount, const S_vector &pos, const S_vector &dir, void *by, int part)
{
    Log("Hit " + N(PHumanIndex(self)) + " " + N(type) + " " + F(amount) + " " + F(pos.x) + " " + F(dir.z) + " " + N(PHumanIndex(by)) + " " + N(part));
}
void __attribute__((thiscall)) PHumanStartMission(void *self, bool server) { Log("StartMission " + N(PHumanIndex(self)) + " " + N(server)); }
void __attribute__((thiscall)) PSetSpectating(void *self, bool on)
{
    Log("SetSpectating " + N((static_cast<unsigned char *>(self) - g_pControllers[0]) / sizeof(C_RemoteController)) + " " + N(on));
}
C_Human *C_SpawnPoint::CreateSoldier(int slot, int actorSlot, const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state,
                                     const int *stats, int uniform, bool npc)
{
    Log("CreateSoldier " + N(slot) + " " + N(actorSlot) + " " + F(pos.x) + " " + F(pos.z) + " " + F(dir.x) + " " + F(dir.z) + " " +
        Str(state->name.c_str()) + " " + N(state->health) + " " + N(stats != nullptr) + " " + N(uniform) + " " + N(npc));
    return PHuman(g_script.created);
}
alignas(16) unsigned char g_pTeamManager[sizeof(C_TeamManager)];
void *g_pTeamVtable[0x40];
bool __attribute__((thiscall)) PAddMember(void *, C_Human *human)
{
    Log("AddMember " + N(PHumanIndex(human)));
    return true;
}
C_Actor *C_ActorsModule::GetActor(int index) const
{
    Log("GetActor " + N(index));
    const auto it = g_script.actors.find(index);
    return it != g_script.actors.end() && it->second ? reinterpret_cast<C_Actor *>(g_pHumans[0]) : nullptr;
}

// The app's states: the test's objects (a vtable of the test's; their calls logged by role).
alignas(16) unsigned char g_pStates[3][0x400];   // 0 the app's, 1 the menu OpenGameMenu gives, 2 case 0's game
void *g_pStateVtable[0x80];
int PStateIndex(const void *p)
{
    for (int k = 0; k < 3; ++k)
        if (p == g_pStates[k])
            return k;
    return p ? 9 : -1;
}
const S_StateAnswers &StateAnswers(int k) { return k == 0 ? g_script.app : k == 1 ? g_script.menu : g_script.made; }
int __attribute__((thiscall)) PGetTypeId(void *self)
{
    const int k = PStateIndex(self);
    Log("GetTypeId " + N(k));
    return StateAnswers(k).typeId;
}
int __attribute__((thiscall)) PGetState(void *self)
{
    const int k = PStateIndex(self);
    Log("GetState " + N(k));
    return StateAnswers(k).state;
}
int __attribute__((thiscall)) PGetPendingState(void *self)
{
    const int k = PStateIndex(self);
    Log("GetPendingState " + N(k));
    return StateAnswers(k).pending;
}
void __attribute__((thiscall)) PSetPendingState(void *self, int state) { Log("SetPendingState " + N(PStateIndex(self)) + " " + N(state)); }
bool __attribute__((thiscall)) PStart(void *self, void *arg)
{
    const int k = PStateIndex(self);
    Log("Start " + N(k) + " " + N(arg == g_pMission));
    return StateAnswers(k).start;
}
bool __attribute__((thiscall)) PStop(void *self)
{
    Log("Stop " + N(PStateIndex(self)));
    return true;
}
void __attribute__((thiscall)) PDeleteState(void *self) { Log("delete state " + N(PStateIndex(self))); }
C_GameState *C_game::OpenGameMenu(int arg)
{
    Log("OpenGameMenu " + N(PStateIndex(this)) + " " + N(arg));
    return reinterpret_cast<C_GameState *>(g_pStates[1]);
}
// The game case 0 makes, the main menu it falls back to: the test's (their constructors and virtuals logging; the
// original's constructors faked alike, the main menu's Start too).
C_game::C_game() { Log("C_game"); }
C_game::~C_game() { Log("delete state " + N(PStateIndex(this))); }
bool C_game::Start(void *mission) { return PStart(this, mission); }
bool C_game::Stop() { return PStop(this); }
bool C_game::HandleInput(float *, float *) { return false; }
void C_game::Tick(int) {}
void C_game::Update(int) {}
void C_game::Render() {}
C_GameState::~C_GameState() = default;
C_MenuState::C_MenuState() { Log("C_MainMenu"); }
C_MenuState::~C_MenuState() = default;
bool C_MenuState::Start(void *arg)
{
    Log("C_MainMenu::Start " + N(arg != nullptr));
    return true;
}
// (The rest of their classes' virtuals and members: not called.)
bool C_GameState::Start(void *) { return false; }
bool C_GameState::Stop() { return false; }
bool C_GameState::HandleInput(float *, float *) { return false; }
void C_GameState::Tick(int) {}
void C_GameState::Update(int) {}
void C_GameState::Render() {}
int C_GameState::GetTypeId() const { return 0; }
int C_GameState::GetState() const { return m_state; }
void C_GameState::SetState(int state) { m_state = state; }
int C_GameState::GetPendingState() const { return m_pendingState; }
void C_GameState::SetPendingState(int state) { m_pendingState = state; }
void C_GameState::CommitPendingState() {}
bool C_MenuState::Stop() { return false; }
bool C_MenuState::HandleInput(float *, float *) { return false; }
void C_MenuState::Update(int) {}
void C_MenuState::Render() {}
void C_MenuState::DrawScreens() {}
void C_MainMenu::OnMessage(S_ControlMessage *) {}
bool C_MainMenu::BuildScreens() { return false; }
void C_MainMenu::OnStop() {}
bool C_MainMenu::OnStart(void *) { return false; }
void C_MainMenu::OnUpdate(int) {}
C_TextBox::C_TextBox() = default;
C_TextBox::~C_TextBox() = default;
void C_TextBox::UpdateScrollRange() {}
void C_TextBox::Render() {}
bool C_TextBox::ScrollTo(int) { return false; }
void S_MenuHint::Reset() {}
// (The original builds and drops its scratch rules inline: not logged.)
S_MultiplayerSettings::S_MultiplayerSettings() = default;
S_MultiplayerSettings::~S_MultiplayerSettings() = default;

namespace {

// ================================================================================================================
// The original's world.
// ================================================================================================================
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))

const uint32_t O_NET = 0x0097d070, O_ROLE = 0x00982100, O_LOCAL_SLOT = 0x00982104, O_PROFILE = 0x008aea0c;
const uint32_t O_ACTORS = 0x0089b4b0, O_TABLE = 0x00982dec, O_COUNT = 0x00982df0, O_DEFS = 0x0098b030, O_TEAM_MANAGER = 0;
const uint32_t O_FSTR = 0x00405100, O_MSG_CTOR = 0x0073d740, O_TIME_SLOT = 0x0080f3f8, O_ENTER_SLOT = 0x0080f1a8,
               O_LEAVE_SLOT = 0x0080f1ac;
const uint32_t O_SERVER_COMMAND = 0x00727bd0, O_CLIENT_COMMAND = 0x00728820;

volatile uintptr_t g_oNetAddress = O_NET;
uint8_t *ONet() { return reinterpret_cast<uint8_t *>(g_oNetAddress); }
uint8_t *OPlayers() { return OF<uint8_t *>(ONet(), 0x1E0); }
int OPlayerCount() { return OPlayers() ? static_cast<int>((OF<uint8_t *>(ONet(), 0x1E4) - OPlayers()) / 0xC4) : 0; }

alignas(16) uint8_t g_oGame[0x200], g_oApp[0x100], g_oMission[0x100], g_oProcessor[0x10], g_oProfile[0x100], g_oDef[0x80];
alignas(16) uint8_t g_oHumans[HUMANS][0x400], g_oControllers[HUMANS][0x400], g_oTeam[0x40], g_oSquad[0x40], g_oSoldier[0xB0];
alignas(16) uint8_t g_oStates[3][0x400];
void *g_oHumanVtable[0x80], *g_oControllerVtable[0x80], *g_oTeamVtable[0x20], *g_oStateVtable[0x20];

int OHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_oHumans[i])
            return i;
    return p ? 99 : -1;
}
void *OHuman(int i) { return i < 0 ? nullptr : g_oHumans[i]; }
int OStateIndex(const void *p)
{
    for (int k = 0; k < 3; ++k)
        if (p == g_oStates[k])
            return k;
    return p ? 9 : -1;
}

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
void THISCALL ONoLock(void *) {}
int OAtexit(void *) { return 0; }
void __stdcall OEnter(void *cs)
{
    if (cs == ONet() + 0x418)
        Log("Enter players");
    else
        EnterCriticalSection(static_cast<CRITICAL_SECTION *>(cs));
}
void __stdcall OLeave(void *cs)
{
    if (cs == ONet() + 0x418)
        Log("Leave players");
    else
        LeaveCriticalSection(static_cast<CRITICAL_SECTION *>(cs));
}
const uint32_t O_READ_BITS = 0x0073d3d0;
std::string OReadSome(void *stream, int bits) { return Hex(reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(O_READ_BITS)(stream, bits)); }

int *THISCALL OGetPlayerStats(void *, int slot)
{
    Log("GetPlayerStats " + N(slot));
    static int stats[9];
    stats[NET_STAT_RANK] = g_script.rank;
    return stats;
}
void *THISCALL OGetPlayerHuman(void *, int slot)
{
    Log("GetPlayerHuman " + N(slot));
    return OHuman(g_script.playerHuman);
}
void *FAST OGetLocalHuman(void *)
{
    Log("GetLocalHuman");
    return OHuman(g_script.localHuman);
}
void THISCALL OCountTeams(void *, int *axis, int *allies, bool pending, bool inGame)
{
    Log("CountTeams " + N(pending) + " " + N(inGame));
    *axis = g_script.countAxis;
    *allies = g_script.countAllied;
}
bool THISCALL OTeamsBalanced(void *, int axis, int allied, bool pending)
{
    Log("TeamsBalanced " + N(axis) + " " + N(allied) + " " + N(pending));
    return g_script.balanced;
}
void THISCALL OSetPlayerTeam(void *, int slot, int team, bool pending) { Log("SetPlayerTeam " + N(slot) + " " + N(team) + " " + N(pending)); }
int THISCALL OGetPlayerTeam(void *, int slot, bool pending)
{
    Log("GetPlayerTeam " + N(slot) + " " + N(pending));
    return g_script.playerTeam;
}
int THISCALL OFindPlayer(void *, int connection)
{
    Log("FindPlayer " + N(connection));
    for (int i = 0; i < OPlayerCount(); ++i)
        if (OF<int>(OPlayers() + 0xC4 * i, 0) == connection)
            return i;
    return -1;
}
const char *THISCALL OGetPlayerName(void *, int slot)
{
    Log("GetPlayerName " + N(slot));
    if (slot < 0 || slot >= OPlayerCount() || !OPlayers()[0xC4 * slot + 8])
        return nullptr;
    return reinterpret_cast<const char *>(OPlayers() + 0xC4 * slot + 8);
}
void THISCALL OKickPlayer(void *, int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void THISCALL OReadScores(void *, void *stream) { Log("ReadScores " + OReadSome(stream, 9)); }
void FAST OOnConnected(void *) { Log("OnConnected"); }
void THISCALL OStatsRead(void *, void *stream) { Log("S_NetStats::Read " + OReadSome(stream, 11)); }
void THISCALL OVoteStart(void *, int kind, int arg) { Log("S_NetVote::Start " + N(kind) + " " + N(arg)); }
void THISCALL OVoteBegin(void *, int kind, int arg) { Log("S_NetVote::Begin " + N(kind) + " " + N(arg)); }
void FAST OVoteCountYes(void *) { Log("S_NetVote::CountYes"); }
void __stdcall OPrintVoteResult(bool passed) { Log("PrintVoteResult " + N(passed)); }
void *OGetGameApp() { return g_oApp; }
void THISCALL OSendGameMessage(void *, int type, int to, intptr_t a, int b, bool reliable)
{
    std::string extra;
    if (type == 4) {
        const char *line = reinterpret_cast<const char *>(a);
        extra = " " + Str(line) + " " + N(*reinterpret_cast<const int *>(line + 0x104)) + " " + N(*reinterpret_cast<const int *>(line + 0x108)) +
                " " + N(line[0x10C]);
        ODelete(const_cast<char *>(line));
        a = 0;
    }
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(a) + " " + N(b) + " " + N(reliable) + extra);
}
void THISCALL OPostCommand(void *, int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
void THISCALL OAppPrintMessage(void *, const char *text) { Log("AppPrintMessage " + Str(text)); }
void *OGetGame() { return g_script.game ? g_oGame : nullptr; }
void THISCALL OEndMission(void *, int unused) { Log("EndMission " + N(unused)); }
void THISCALL OOnPlayerLeft(void *, int connection) { Log("OnPlayerLeft " + N(connection)); }
void THISCALL ORestartMission(void *, bool newRound) { Log("RestartMission " + N(newRound)); }
void THISCALL OApplyObjectiveStatus(void *, int objective, int status) { Log("ApplyObjectiveStatus " + N(objective) + " " + N(status)); }
void FAST OOnWarmupFinished(void *) { Log("OnWarmupFinished"); }
void FAST OOnWarmupStarted(void *) { Log("OnWarmupStarted"); }
void THISCALL OOnRoundOver(void *, int winner) { Log("OnRoundOver " + N(winner)); }
void THISCALL OOnRespawnScreen(void *, bool cancel) { Log("OnRespawnScreen " + N(cancel)); }
void THISCALL OOnSessionClosing(void *, int reason) { Log("OnSessionClosing " + N(reason)); }
void THISCALL OReadObjectiveStates(void *, void *msg) { Log("ReadObjectiveStates " + OReadSome(msg, 5)); }
void THISCALL OSetCameraMode(void *, int mode) { Log("SetCameraMode " + N(mode)); }
void *THISCALL OOpenGameMenu(void *self, int arg)
{
    Log("OpenGameMenu " + N(OStateIndex(self)) + " " + N(arg));
    return g_oStates[1];
}
void __cdecl OPrintMessage(void *, const float *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color[0]) + " " + F(color[1]) + " " + F(color[2]) + " " + VFormat(format, args));
    va_end(args);
}
// (These two pop their argument: the first called with the game in ECX, unused.)
void __stdcall OShowGameMenuMessage(int textId) { Log("ShowGameMenuMessage " + N(textId)); }
void OLoadNextMap() { Log("LoadNextMap"); }
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    OF<void *>(ret, 0) = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(ret, "T%X", id);
    return ret;
}
bool FAST OHasUtf8(const char *text)
{
    Log("HasUtf8Sequences " + Str(text));
    return g_script.utf8;
}
void THISCALL OReadRules(void *, void *stream) { Log("ReadRules " + OReadSome(stream, 7)); }
void THISCALL OReadCurrentMap(void *, void *stream) { Log("ReadCurrentMap " + OReadSome(stream, 6)); }
void FAST OSettingsReset(void *) { Log("S_MultiplayerSettings::Reset"); }
bool FAST OSetupMultiplayerMission(void *)
{
    Log("SetupMultiplayerMission");
    return true;
}
void *THISCALL OGetCurrentMission(void *)
{
    Log("GetCurrentMission");
    return g_oMission;
}
void THISCALL OAddPlayer(void *, const char *text, int team) { Log("AddPlayer " + Str(text) + " " + N(team)); }
void __stdcall OAddChatLine(int team, const char *name, const char *text) { Log("AddChatLine " + N(team) + " " + Str(name) + " " + Str(text)); }
void THISCALL OSetTeam(void *, int team) { Log("C_GameMenu::SetTeam " + N(team)); }
bool THISCALL OExecute(void *, const char *command)
{
    Log("Execute " + Str(command));
    return true;
}
void FAST OSplitStacks(void *state)
{
    void *rep = OF<void *>(state, 0);
    Log("SplitSoldierStacks " + Str(rep ? reinterpret_cast<const char *>(static_cast<uint8_t *>(rep) + 8) : ""));
}
void FAST OClearItems(void *) { Log("ClearItems"); }
void FAST OBroadcastReadState(void *msg) { Log("BroadcastReadState " + OReadSome(msg, 4)); }
void THISCALL OTemporaryReadState(void *, void *msg) { Log("TemporaryActors::ReadState " + OReadSome(msg, 3)); }
void *THISCALL OHumanStoreGet(void *, int index)
{
    Log("C_HumanStore::Get " + N(index));
    return g_script.profile ? g_oProfile : nullptr;
}
void THISCALL OHumanStoreGetStats(void *, int *stats, int index, int level)
{
    Log("C_HumanStore::GetStats " + N(index) + " " + N(level) + " " + N(stats[0]) + " " + N(stats[1]));
    stats[2] += level;
}
int THISCALL OGetControllerType(void *human)
{
    const int i = OHumanIndex(human);
    Log("GetControllerType " + N(i));
    return g_script.humans[i].controllerType;
}
void THISCALL OSetChatText(void *human, const char *text) { Log("SetChatText " + N(OHumanIndex(human)) + " " + Str(text)); }
void FAST ORequestRespawn(void *human) { Log("RequestRespawn " + N(OHumanIndex(human))); }
void THISCALL OApplyRemoteHit(void *human, int type, int zone, int health, int killerId, const float *pos, const float *dir)
{
    Log("ApplyRemoteHit " + N(OHumanIndex(human)) + " " + N(type) + " " + N(zone) + " " + N(health) + " " + N(killerId) + " " + F(pos[0]) + " " +
        F(dir[2]));
}
void THISCALL OHumanHit(void *self, int type, float amount, const float *pos, const float *dir, void *by, int part)
{
    Log("Hit " + N(OHumanIndex(self)) + " " + N(type) + " " + F(amount) + " " + F(pos[0]) + " " + F(dir[2]) + " " + N(OHumanIndex(by)) + " " + N(part));
}
void THISCALL OHumanStartMission(void *self, bool server) { Log("StartMission " + N(OHumanIndex(self)) + " " + N(server)); }
void THISCALL OSetSpectating(void *self, bool on)
{
    Log("SetSpectating " + N((static_cast<uint8_t *>(self) - g_oControllers[0]) / 0x400) + " " + N(on));
}
void *FAST OCreateSoldier(int slot, int actorSlot, const float *pos, const float *dir, void *state, const int *stats, int uniform, bool npc)
{
    void *rep = OF<void *>(state, 0);
    Log("CreateSoldier " + N(slot) + " " + N(actorSlot) + " " + F(pos[0]) + " " + F(pos[2]) + " " + F(dir[0]) + " " + F(dir[2]) + " " +
        Str(rep ? reinterpret_cast<const char *>(static_cast<uint8_t *>(rep) + 8) : "") + " " + N(OF<int>(state, 0xC)) + " " + N(stats != nullptr) +
        " " + N(uniform) + " " + N(npc));
    return OHuman(g_script.created);
}
void *THISCALL OGetPlayerTeamOfManager(void *) { return g_oTeam; }
bool THISCALL OAddMember(void *, void *human)
{
    Log("AddMember " + N(OHumanIndex(human)));
    return true;
}
void *THISCALL OGetActor(void *, int index)
{
    Log("GetActor " + N(index));
    const auto it = g_script.actors.find(index);
    return it != g_script.actors.end() && it->second ? g_oHumans[0] : nullptr;
}
int THISCALL OGetTypeId(void *self)
{
    const int k = OStateIndex(self);
    Log("GetTypeId " + N(k));
    return StateAnswers(k).typeId;
}
int THISCALL OGetState(void *self)
{
    const int k = OStateIndex(self);
    Log("GetState " + N(k));
    return StateAnswers(k).state;
}
int THISCALL OGetPendingState(void *self)
{
    const int k = OStateIndex(self);
    Log("GetPendingState " + N(k));
    return StateAnswers(k).pending;
}
void THISCALL OSetPendingState(void *self, int state) { Log("SetPendingState " + N(OStateIndex(self)) + " " + N(state)); }
bool THISCALL OStart(void *self, void *arg)
{
    const int k = OStateIndex(self);
    Log("Start " + N(k) + " " + N(arg == g_oMission));
    return StateAnswers(k).start;
}
bool THISCALL OStop(void *self)
{
    Log("Stop " + N(OStateIndex(self)));
    return true;
}
void *THISCALL ODeleteState(void *self, unsigned)
{
    Log("delete state " + N(OStateIndex(self)));
    return self;
}
// The main menu case 0 falls back to: its base's constructor and Start faked (its vtable is the original's own).
void *THISCALL OMenuStateCtor(void *self)
{
    Log("C_MainMenu");
    return self;
}
bool THISCALL OMenuStateStart(void *, void *arg)
{
    Log("C_MainMenu::Start " + N(arg != nullptr));
    return true;
}
// The main menu's Stop and deleting destructor (the port's are the test's stubs: not logged).
bool THISCALL OMenuStateStop(void *) { return false; }
void *THISCALL OMainMenuDelete(void *self, unsigned) { return self; }
// The game case 0 makes: a vtable like the port's C_game (the test's): its type the game's, not logged; Start, Stop and
// the destructor logged; its state and pending state its own fields.
void *g_oMadeVtable[0x20];
int THISCALL OMadeTypeId(void *) { return GAME_STATE_TYPE_GAME; }
int THISCALL OMadeGetState(void *self) { return OF<int>(self, 4); }
int THISCALL OMadeGetPending(void *self) { return OF<int>(self, 8); }
void THISCALL OMadeSetPending(void *self, int state) { OF<int>(self, 8) = state; }
void *THISCALL OGameCtor(void *self)
{
    Log("C_game");
    OF<void *>(self, 0) = g_oMadeVtable;
    OF<int>(self, 4) = 0;
    OF<int>(self, 8) = GAME_STATE_NO_PENDING;
    return self;
}

// ---- checks ----
int checks, failures;
int g_round;
std::string g_line, g_trace;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, %s: %s\n   (ops:%s)\n", g_round, g_line.c_str(), what.c_str(), g_trace.c_str());
}
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok)
        Fail(what);
}

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
float R(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

void CompareLogs()
{
    if (g_log[0] == g_log[1]) {
        ++checks;
        return;
    }
    std::string s = "the calls differ:";
    const size_t n = std::max(g_log[0].size(), g_log[1].size());
    for (size_t i = 0; i < n; ++i) {
        const std::string x = i < g_log[1].size() ? g_log[1][i] : "-", y = i < g_log[0].size() ? g_log[0][i] : "-";
        if (x != y) {
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 300) + "\n     port: " + y.substr(0, 300);
            break;
        }
    }
    Check(false, s);
}

}   // namespace

// ---- the port's NetReadSoldierState (net_messages.cpp's, compared by net part 9) ----
namespace {
void ReadItem(C_BitStream *stream, S_SoldierItem &item)
{
    item.uid = static_cast<int16_t>(stream->ReadBits(16));
    item.typeId = static_cast<int16_t>(stream->ReadBits(16));
    item.present = static_cast<int16_t>(stream->ReadBits(8));
}
}  // namespace
void NetReadSoldierState(C_NetMessage *msg, S_SoldierState &state, bool itemsOnly)
{
    if (!itemsOnly) {
        char name[256];
        msg->ReadString(name, 0xFF);
        state.name = name;
        state.team = static_cast<int>(msg->ReadBits(1));
        state.profileIndex = static_cast<int>(msg->ReadBits(8));
        state.spawnZone = static_cast<int>(msg->ReadBits(10));
        state.spectating = msg->ReadBits(1) != 0;
        state.vip = msg->ReadBits(1) != 0;
        state.health = static_cast<int>(msg->ReadBits(10));
    }
    ReadItem(msg, state.head);
    ReadItem(msg, state.uniform);
    ReadItem(msg, state.backpack);
    ReadItem(msg, state.longGun);
    ReadItem(msg, state.hands);
    for (std::vector<S_SoldierItem> *list : {&state.belt, &state.backpackContents}) {
        const auto count = static_cast<int>(msg->ReadBits(8));
        for (int i = 0; i < count; ++i) {
            S_SoldierItem item;
            ReadItem(msg, item);
            list->push_back(item);
        }
    }
    if (!itemsOnly) {
        uint32_t words[8];
        for (uint32_t &word : words)
            word = msg->ReadBits(32);
        std::memcpy(&state.load, words, sizeof words);
    }
}

// The game's calls that take a soldier state (each side's layout read into the same log line).
std::string PortStateText(const S_SoldierState *s)
{
    if (!s)
        return "null";
    std::string t = Str(s->name.c_str()) + " " + N(s->team) + " " + N(s->profileIndex) + " " + N(s->spawnZone) + " " + N(s->spectating) +
                    N(s->vip) + " " + N(s->health) + " " + N(s->unknown34) + " " + N(s->unknown38) + " " + N(s->speechSet) + " " +
                    N(s->deployed) + " " + N(s->uniform.typeId) + " " + N(static_cast<long long>(s->belt.size())) + " " +
                    N(static_cast<long long>(s->backpackContents.size()));
    return t;
}
std::string OrigStateText(void *o)
{
    if (!o)
        return "null";
    void *rep = OF<void *>(o, 0);
    auto count = [&](int at) {
        const uint8_t *first = OF<uint8_t *>(o, at), *last = OF<uint8_t *>(o, at + 4);
        return first ? static_cast<long long>((last - first) / 6) : 0LL;
    };
    return Str(rep ? reinterpret_cast<const char *>(static_cast<uint8_t *>(rep) + 8) : "") + " " + N(OF<int>(o, 0x40)) + " " + N(OF<int>(o, 0x3C)) +
           " " + N(OF<int>(o, 4)) + " " + N(OF<uint8_t>(o, 8)) + N(OF<uint8_t>(o, 9)) + " " + N(OF<int>(o, 0xC)) + " " + N(OF<int>(o, 0x34)) +
           " " + N(OF<int>(o, 0x38)) + " " + N(OF<int>(o, 0x48)) + " " + N(OF<uint8_t>(o, 0x44)) + " " + N(OF<int16_t>(o, 0x54)) + " " +
           N(count(0x70)) + " " + N(count(0x80));
}
std::string StatsText(const int *stats)
{
    if (!stats)
        return "null";
    std::string s;
    for (int i = 0; i < STAT_COUNT; ++i)
        s += N(stats[i]) + ",";
    return s;
}
void C_game::OnPlayerJoined(int slot, S_SoldierState *state, int *stats)
{
    Log("OnPlayerJoined " + N(slot) + " " + PortStateText(state) + " " + StatsText(stats));
}
void C_game::OnRespawnRequest(int slot, S_SoldierState *state, int *stats)
{
    Log("OnRespawnRequest " + N(slot) + " " + PortStateText(state) + " " + StatsText(stats));
    delete state;
    delete[] stats;
}
void C_game::RespawnPlayer(int slot, const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state, const int *stats)
{
    Log("RespawnPlayer " + N(slot) + " " + F(pos.x) + " " + F(pos.y) + " " + F(pos.z) + " " + F(dir.x) + " " + F(dir.y) + " " + F(dir.z) + " " +
        PortStateText(state) + " " + StatsText(stats));
}
namespace {
void THISCALL OOnPlayerJoined(void *, int slot, void *state, int *stats)
{
    Log("OnPlayerJoined " + N(slot) + " " + OrigStateText(state) + " " + StatsText(stats));
}
void THISCALL OOnRespawnRequest(void *, int slot, void *state, int *stats)
{
    Log("OnRespawnRequest " + N(slot) + " " + OrigStateText(state) + " " + StatsText(stats));
}
void THISCALL ORespawnPlayer(void *, int slot, const float *pos, const float *dir, void *state, int *stats)
{
    Log("RespawnPlayer " + N(slot) + " " + F(pos[0]) + " " + F(pos[1]) + " " + F(pos[2]) + " " + F(dir[0]) + " " + F(dir[1]) + " " + F(dir[2]) + " " +
        OrigStateText(state) + " " + StatsText(stats));
}
}  // namespace

namespace {

// ---- the world ----
int g_slots;
alignas(16) uint8_t g_mp[0x400];
void *g_adminPassword;
alignas(16) S_NetQuantization g_quantization[2];

void *ORep(const char *text)
{
    const size_t n = std::strlen(text);
    auto *rep = static_cast<uint8_t *>(ONew(8 + n + 1));
    OF<int>(rep, 0) = 1;
    OF<int>(rep, 4) = static_cast<int>(n);
    std::memcpy(rep + 8, text, n + 1);
    return rep;
}

void SetStateAnswers(S_StateAnswers &a)
{
    static const int types[] = {GAME_STATE_TYPE_GAME, GAME_STATE_TYPE_GAME_MENU, 3, 0};
    a.typeId = types[I(0, 3)];
    a.state = P(0.2) ? GAME_STATE_LOBBY : I(0, 12);
    a.pending = P(0.2) ? GAME_STATE_LOBBY : I(-1, 12);
    a.start = P(0.7);
}

void SetWorld()
{
    C_NetGame &net = PNet();
    static const uint32_t roles[] = {7, 1, 2, 3, 0, 5, 1, 2};
    g_gameRole = roles[I(0, 7)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    g_slots = I(1, 12);
    g_localPlayerSlot = I(0, g_slots - 1);
    G<int>(O_LOCAL_SLOT) = g_localPlayerSlot;
    g_script.game = P(0.9);
    g_script.now = static_cast<uint32_t>(I(0, 1000000));
    SetStateAnswers(g_script.app);
    SetStateAnswers(g_script.menu);
    SetStateAnswers(g_script.made);
    g_script.playerHuman = P(0.8) ? I(0, HUMANS - 1) : -1;
    g_script.localHuman = P(0.8) ? I(0, HUMANS - 1) : -1;
    g_script.created = P(0.8) ? I(0, HUMANS - 1) : -1;
    g_script.countAxis = I(0, 6);
    g_script.countAllied = I(0, 6);
    g_script.balanced = P(0.6);
    g_script.playerTeam = I(0, 2);
    g_script.rank = I(0, 7);
    g_script.profile = P(0.85);
    g_script.utf8 = P(0.2);
    g_script.squad = P(0.9);
    g_script.squadTeam = I(0, 1);
    g_script.uniformType = P(0.3) ? -1 : I(0, 100);
    g_script.uniformPresent = I(0, 2);
    g_script.itemDef = true;
    g_script.actors.clear();
    for (int a = 0; a < 48; ++a)
        g_script.actors[a] = P(0.5);

    // The rules (one block): the automatic balance, the admin password.
    std::memset(g_mp, 0, sizeof g_mp);
    S_MultiplayerSettings &mp = *reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    mp.autoTeamBalance = I(0, 1);
    static const char *const passwords[] = {"", "admin", "root"};
    g_adminPassword = P(0.2) ? nullptr : ORep(passwords[I(0, 2)]);
    OF<void *>(g_mp, 0x21C) = g_adminPassword;
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_unicodeFont = P(0.3);
    G<uint8_t>(O_PROFILE + 0x30) = reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_unicodeFont;

    // The game: its mission's balance, the round time, the respawn wave time.
    C_game *game = reinterpret_cast<C_game *>(g_pGame);
    game->m_roundTime = I(0, 1000000);
    OF<int>(g_oGame, 0x30) = game->m_roundTime;
    game->m_respawnWaveTime = I(0, 999);
    OF<int>(g_oGame, 0x7C) = game->m_respawnWaveTime;
    game->m_mission = reinterpret_cast<C_MissionDef *>(g_pMission);
    OF<void *>(g_oGame, 0xA8) = g_oMission;
    auto *mission = reinterpret_cast<C_MpMissionDef *>(g_pMission);
    mission->m_balanceAllied = I(-1, 9);
    mission->m_balanceAxis = I(-1, 9);
    OF<int>(g_oMission, 0x58) = mission->m_balanceAllied;
    OF<int>(g_oMission, 0x5C) = mission->m_balanceAxis;
    // The app's state.
    GetGameApp()->m_state = reinterpret_cast<C_GameState *>(P(0.9) ? g_pStates[0] : nullptr);
    OF<void *>(g_oApp, 0x14) = GetGameApp()->m_state ? g_oStates[0] : nullptr;
    // The profiles' stats, the uniform's weight.
    auto *profile = reinterpret_cast<S_SoldierProfile *>(g_pProfile);
    for (int k = 0; k < 9; ++k) {
        profile->stats[k] = P(0.5) ? static_cast<float>(I(0, 120)) : R(0.0f, 120.0f);
        OF<float>(g_oProfile, 0x48 + 4 * k) = profile->stats[k];
    }
    const float weight = P(0.5) ? static_cast<float>(I(0, 20)) : R(0.0f, 20.0f);
    reinterpret_cast<C_ItemDef *>(g_pDef)->m_weight = weight;
    OF<float>(g_oDef, 0x18) = weight;
    for (int t = 0; t < 512; ++t)
        G<void *>(O_DEFS + 4 * static_cast<uint32_t>(t)) = g_script.itemDef ? g_oDef : nullptr;
    // The squad's first soldier: its team, uniform.
    // (Its other fields are the test's: ClearItems is faked.)
    // The humans: their health, controllers.
    for (int i = 0; i < HUMANS; ++i) {
        S_HumanAnswers &h = g_script.humans[i];
        h.health = P(0.3) ? I(-5, 0) : P(0.2) ? I(0, 1) : I(1, 100);
        h.controllerType = P(0.5) ? HUMAN_CONTROLLER_REMOTE : I(0, 8);
        h.controller = true;   // (a soldier made always has one: both call it unchecked)
        C_Human *ph = PHuman(i);
        ph->m_stats[STAT_HEALTH] = h.health;
        OF<int>(g_oHumans[i], 0xEC) = h.health;
        ph->m_controller = h.controller ? reinterpret_cast<C_HumanController *>(g_pControllers[i]) : nullptr;
        OF<void *>(g_oHumans[i], 0xB0) = h.controller ? g_oControllers[i] : nullptr;
        reinterpret_cast<C_RemoteController *>(g_pControllers[i])->m_vip = false;
        OF<uint8_t>(g_oControllers[i], 0x2C0) = 0;
    }
    // The actors module's count.
    auto *am = reinterpret_cast<C_ActorsModule *>(&g_actorsModule);
    const int actors = P(0.3) ? I(0, 31) : I(32, 47);
    am->m_actors.assign(static_cast<size_t>(actors), nullptr);
    static void *oActors[64];
    G<void **>(O_ACTORS + 0x10) = oActors;
    G<void **>(O_ACTORS + 0x14) = oActors + actors;

    // The players.
    net.m_players.assign(static_cast<size_t>(g_slots), S_NetPlayerState());
    ODelete(OPlayers());
    uint8_t *op = static_cast<uint8_t *>(ONew(static_cast<size_t>(g_slots) * 0xC4));
    OF<uint8_t *>(ONet(), 0x1E0) = op;
    OF<uint8_t *>(ONet(), 0x1E4) = op + g_slots * 0xC4;
    OF<uint8_t *>(ONet(), 0x1E8) = op + g_slots * 0xC4;
    for (int s = 0; s < g_slots; ++s) {
        S_NetPlayerState &p = net.m_players[static_cast<size_t>(s)];
        uint8_t *raw = reinterpret_cast<uint8_t *>(&p);
        for (int k = 0; k < 0xC4; ++k)
            raw[k] = static_cast<uint8_t>(I(0, 255));
        p.connection = P(0.25) ? 0 : I(1, 60);
        std::memset(p.name, 0, sizeof p.name);
        if (P(0.8)) {
            std::snprintf(p.name, sizeof p.name, P(0.1) ? "P\xC3\xA9%d" : "P%d", s);
        } else if (P(0.5)) {
            // A freed slot: its first character cleared, the rest left.
            std::snprintf(p.name, sizeof p.name, "xP%d", s);
            p.name[0] = 0;
        }
        const int h = P(0.85) ? I(0, HUMANS - 1) : -1;
        p.human = PHuman(h);
        p.team = I(0, 2);
        p.link = nullptr;
        p.voiceBuffer = nullptr;
        uint8_t *o = op + s * 0xC4;
        std::memcpy(o, raw, 0xC4);
        OF<void *>(o, 0x3C) = OHuman(h);
        OF<void *>(o, 0x6C) = nullptr;
    }
    net.m_unknown8 = false;
    OF<uint8_t>(ONet(), 8) = 0;
    net.m_unknown448 = 0;
    OF<int>(ONet(), 0x448) = 0;
    GetGameApp()->m_randomId = 0;
    OF<uint32_t>(g_oApp, 0x1C) = 0;
}

// The squad (each side's layout): its first soldier, the team and the uniform read.
alignas(16) unsigned char g_pSquadStore[sizeof(S_MissionSquad)];
S_SquadSoldier *g_pSoldier;
void SetSquad()
{
    auto *squad = reinterpret_cast<S_MissionSquad *>(g_pSquadStore);
    delete reinterpret_cast<S_SoldierState *>(g_pSoldier);
    g_pSoldier = reinterpret_cast<S_SquadSoldier *>(new S_SoldierState);
    g_pSoldier->team = g_script.squadTeam;
    g_pSoldier->uniform.typeId = static_cast<int16_t>(g_script.uniformType);
    g_pSoldier->uniform.present = static_cast<int16_t>(g_script.uniformPresent);
    squad->states.assign(g_script.squad ? 1 : 0, g_pSoldier);
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_missionSquad = squad;
    std::memset(g_oSoldier, 0, sizeof g_oSoldier);
    OF<int>(g_oSoldier, 0x40) = g_script.squadTeam;
    OF<int16_t>(g_oSoldier, 0x54) = static_cast<int16_t>(g_script.uniformType);
    OF<int16_t>(g_oSoldier, 0x56) = static_cast<int16_t>(g_script.uniformPresent);
    static void *oStates[2];
    oStates[0] = g_oSoldier;
    OF<void **>(g_oSquad, 8) = g_script.squad ? oStates : nullptr;
    OF<void **>(g_oSquad, 0xC) = g_script.squad ? oStates + 1 : nullptr;
    G<void *>(O_PROFILE + 0x14) = g_oSquad;
}

// Slots a call appends from a state constructed on the stack (the session joined: one; the whole game: all of a
// client's): the bytes the original's constructor leaves as the stack had them are not compared in that call, then
// the port's take the original's.
int g_freshFrom = 1 << 30;
bool Indeterminate(int k)
{
    return (k >= 4 && k < 8) || (k >= 0x78 && k < 0x7C) || (k >= 0x80 && k < 0xA1) || k == 0xA3 || (k >= 0xA8 && k < 0xAC) ||
           (k >= 0xC0 && k < 0xC4) || (k >= 0x6C && k < 0x70);
}
void CarryFreshSlots()
{
    for (int i = g_freshFrom; i < OPlayerCount() && i < static_cast<int>(PNet().m_players.size()); ++i) {
        auto *a = reinterpret_cast<uint8_t *>(&PNet().m_players[static_cast<size_t>(i)]);
        const uint8_t *b = OPlayers() + i * 0xC4;
        for (int k = 0; k < 0xC4; ++k)
            if (Indeterminate(k))
                a[k] = b[k];
    }
    g_freshFrom = 1 << 30;
}

// The state compared: the players (the same bytes; the humans by index; the name as a string), the net game's +8, the
// app's state and id, the squad soldier's team, the in-game menu's respawn flag, the controllers' VIP flags.
void CompareState()
{
    C_NetGame &net = PNet();
    std::string s;
    if (OPlayerCount() != static_cast<int>(net.m_players.size()))
        s += "the players' count " + N(OPlayerCount()) + " / " + N(static_cast<long long>(net.m_players.size())) + "; ";
    for (int i = 0; i < OPlayerCount() && i < static_cast<int>(net.m_players.size()) && s.empty(); ++i) {
        const uint8_t *a = reinterpret_cast<const uint8_t *>(&net.m_players[static_cast<size_t>(i)]);
        uint8_t *b = OPlayers() + i * 0xC4;
        if (std::strcmp(reinterpret_cast<const char *>(a + 8), reinterpret_cast<const char *>(b + 8)) != 0)
            s += "slot " + N(i) + " name; ";
        for (int k = 0; k < 0xC4; ++k) {
            if ((k >= 0x3C && k < 0x40) || (k >= 8 && k < 0x3C) || (i >= g_freshFrom && Indeterminate(k)))
                continue;
            if (a[k] != b[k]) {
                s += "slot " + N(i) + " byte +" + N(k) + " " + N(b[k]) + " / " + N(a[k]) + "; ";
                break;
            }
        }
        if (PHumanIndex(net.m_players[static_cast<size_t>(i)].human) != OHumanIndex(OF<void *>(b, 0x3C)))
            s += "slot " + N(i) + " human; ";
    }
    if (net.m_unknown8 != (OF<uint8_t>(ONet(), 8) != 0) || net.m_unknown448 != OF<int>(ONet(), 0x448) ||
        GetGameApp()->m_randomId != OF<uint32_t>(g_oApp, 0x1C))
        s += "the net game's fields; ";
    if (PStateIndex(GetGameApp()->m_state) != OStateIndex(OF<void *>(g_oApp, 0x14)))
        s += "the app's state " + N(OStateIndex(OF<void *>(g_oApp, 0x14))) + " / " + N(PStateIndex(GetGameApp()->m_state)) + "; ";
    if (g_pSoldier->team != OF<int>(g_oSoldier, 0x40))
        s += "the soldier's team; ";
    const C_game *game = reinterpret_cast<const C_game *>(g_pGame);
    if (game->m_respawnWaveTime != OF<int>(g_oGame, 0x7C))
        s += "the respawn wave time; ";
    for (int k = 0; k < 3; ++k)
        if (reinterpret_cast<C_GameMenu *>(g_pStates[k])->m_respawnOnApply != (OF<uint8_t>(g_oStates[k], 0x1D1) != 0))
            s += "state " + N(k) + "'s respawn on apply; ";
    for (int i = 0; i < HUMANS; ++i)
        if (reinterpret_cast<C_RemoteController *>(g_pControllers[i])->m_vip != (OF<uint8_t>(g_oControllers[i], 0x2C0) != 0))
            s += "controller " + N(i) + "'s VIP; ";
    Check(s.empty(), s);
}

// ---- the commands' bits ----
constexpr int WORDS = 16384;
struct S_Writer {
    uint32_t words[WORDS];
    C_BitStream s;
    S_Writer() : s(words, WORDS * 32) { std::memset(words, 0, sizeof words); }
};
std::string RandomText(int maxLength)
{
    static const char *const texts[] = {"hello", "", "admin", "root", "maxclients 8", "a much longer line of text for the chat window",
                                        "\xC3\xA9t\xC3\xA9", "kick P1"};
    std::string t = texts[I(0, 7)];
    if (P(0.1))
        t = std::string(static_cast<size_t>(I(1, maxLength + 20)), 'w');
    return t;
}
void WriteState(C_BitStream &s)
{
    s.WriteString(RandomText(40).c_str());
    s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
    s.WriteBits(static_cast<uint32_t>(P(0.2) ? 0xFF : I(0, 40)), 8);
    s.WriteBits(static_cast<uint32_t>(I(0, 0x3FF)), 10);
    s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
    // (A VIP's uniform has a definition: the original reads its weight unchecked; the port's Fix takes none as 0.)
    const bool vip = P(0.4);
    s.WriteBits(vip, 1);
    s.WriteBits(static_cast<uint32_t>(I(0, 1023)), 10);
    for (int k = 0; k < 5; ++k) {
        s.WriteBits(static_cast<uint32_t>(I(0, 0xFFFF)), 16);
        s.WriteBits(static_cast<uint32_t>(k == 1 && vip ? I(0, 100) : P(0.2) ? 0xFFFF : I(0, 600)), 16);
        s.WriteBits(static_cast<uint32_t>(I(0, 3)), 8);
    }
    for (int list = 0; list < 2; ++list) {
        const int n = I(0, 5);
        s.WriteBits(static_cast<uint32_t>(n), 8);
        for (int k = 0; k < n; ++k) {
            s.WriteBits(static_cast<uint32_t>(I(0, 0xFFFF)), 16);
            s.WriteBits(static_cast<uint32_t>(I(0, 100)), 16);
            s.WriteBits(static_cast<uint32_t>(I(0, 3)), 8);
        }
    }
    for (int k = 0; k < 8; ++k)
        s.WriteBits(static_cast<uint32_t>(rng()), 32);
}
uint32_t RandomFloatBits() { float f = R(-100.0f, 100.0f); uint32_t u; std::memcpy(&u, &f, 4); return u; }
// A command's fields (`server`: the server's), then random bits.
void WriteCommand(C_BitStream &s, bool server, int id)
{
    const int slot = P(0.85) ? I(0, g_slots - 1) : I(-1, g_slots + 1);
    if (server) {
        switch (id) {
        case 0:
        case 1:
            s.WriteBits(static_cast<uint32_t>(I(0, g_slots - 1)), 32);
            WriteState(s);
            break;
        case 2:
            s.WriteBits(static_cast<uint32_t>(I(0, g_slots - 1)), 32);
            break;
        case 3:
            s.WriteBits(static_cast<uint32_t>(P(0.2) ? -1 : I(0, g_slots - 1)), 32);
            s.WriteBits(static_cast<uint32_t>(rng()), 32);
            s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
            s.WriteString(RandomText(0x103).c_str());
            break;
        case 4:
            s.WriteBits(static_cast<uint32_t>(slot), 32);
            break;
        case 5:
            s.WriteBits(static_cast<uint32_t>(I(0, g_slots - 1)), 32);
            s.WriteBits(static_cast<uint32_t>(I(0, 2)), 32);
            break;
        case 6:
        case 7:
        case 9:
            s.WriteBits(static_cast<uint32_t>(I(0, g_slots - 1)), 5);
            if (id == 9)
                s.WriteBits(static_cast<uint32_t>(P(0.5) ? I(0, 15) : I(0, 0xFFFF)), 16);
            else
                s.WriteString(RandomText(id == 6 ? 0x21 : 0x80).c_str());
            break;
        case 8:
            s.WriteString(RandomText(0x80).c_str());
            break;
        case 10:
            s.WriteBits(static_cast<uint32_t>(I(0, 127)), 7);
            s.WriteBits(static_cast<uint32_t>(I(0, 1023)), 10);
            break;
        default:
            break;
        }
    } else {
        switch (id) {
        case 0: {
            s.WriteBits(static_cast<uint32_t>(rng()), 32);
            s.WriteBits(static_cast<uint32_t>(rng()), 32);
            s.WriteBits(static_cast<uint32_t>(rng()), 7);   // ReadRules (the test's: 7 bits)
            s.WriteBits(static_cast<uint32_t>(rng()), 6);   // ReadCurrentMap (6)
            const int n = I(0, 5);
            s.WriteBits(static_cast<uint32_t>(n), 8);
            for (int k = 0; k < n; ++k) {
                s.WriteString(RandomText(0x32).c_str());
                s.WriteBits(static_cast<uint32_t>(I(0, 3)), 2);
                s.WriteBits(static_cast<uint32_t>(I(0, 31)), 5);
            }
            break;
        }
        case 2:
            s.WriteBits(static_cast<uint32_t>(P(0.7) ? PNet().m_players[static_cast<size_t>(I(0, g_slots - 1))].connection : I(0, 99)), 32);
            break;
        case 3:
            s.WriteBits(static_cast<uint32_t>(I(0, g_slots - 1)), 32);
            for (int k = 0; k < 6; ++k)
                s.WriteBits(static_cast<uint32_t>(rng()), 16);   // the place and facing (read with the table's bits)
            WriteState(s);
            s.WriteBits(static_cast<uint32_t>(I(0, 1023)), 10);
            for (int k = 0; k < 11; ++k)
                s.WriteBits(static_cast<uint32_t>(I(0, 127)), 7);
            break;
        case 4:
            s.WriteBits(static_cast<uint32_t>(P(0.2) ? -1 : I(0, g_slots - 1)), 32);
            s.WriteBits(static_cast<uint32_t>(rng()), 32);
            s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
            s.WriteString(RandomText(0x104).c_str());
            break;
        case 5: {
            const int n = I(0, 3);
            s.WriteBits(static_cast<uint32_t>(n), 32);
            for (int k = 0; k < n * 6; ++k)
                s.WriteBits(RandomFloatBits(), 32);
            break;
        }
        case 9:
            s.WriteString(RandomText(0x32).c_str());
            break;
        case 10:
            s.WriteBits(static_cast<uint32_t>(I(0, 31)), 5);
            s.WriteBits(static_cast<uint32_t>(I(0, 15)), 4);
            break;
        case 12: {
            for (int k = I(0, 5); k > 0; --k) {
                s.WriteBits(static_cast<uint32_t>(P(0.4) ? g_localPlayerSlot : I(0, g_slots - 1)), 6);
                s.WriteBits(static_cast<uint32_t>(I(0, 2)), 2);
            }
            s.WriteBits(0x21, 6);
            break;
        }
        case 13:
            s.WriteBits(static_cast<uint32_t>(I(0, 3)), 2);
            break;
        case 14:
        case 15:
            s.WriteBits(static_cast<uint32_t>(P(0.4) ? g_localPlayerSlot : I(0, g_slots - 1)), 5);
            s.WriteBits(static_cast<uint32_t>(I(0, 2)), id == 14 ? 2 : 1);
            break;
        case 17:
            s.WriteBits(static_cast<uint32_t>(I(-1, 4)), 32);
            s.WriteBits(static_cast<uint32_t>(P(0.4) ? g_localPlayerSlot : I(0, g_slots - 1)), 32);
            break;
        case 19:
            s.WriteBits(static_cast<uint32_t>(I(0, 1)), 32);
            break;
        case 21: {
            if (P(0.1)) {
                s.WriteBits(0, 1);
                break;
            }
            s.WriteBits(1, 1);
            const int n = I(1, 6);
            s.WriteBits(static_cast<uint32_t>(n), 8);
            for (int k = 0; k < n; ++k) {
                s.WriteBits(static_cast<uint32_t>(I(0, 99)), 32);
                s.WriteString(RandomText(0x32).c_str());
                const int actor = P(0.3) ? -1 : I(0, 200);
                s.WriteBits(static_cast<uint32_t>(actor), 32);
                s.WriteBits(static_cast<uint32_t>(rng()), 11);   // S_NetStats::Read (the test's: 11 bits)
                if (actor != -1) {
                    for (int v = 0; v < 6; ++v)
                        s.WriteBits(RandomFloatBits(), 32);
                    WriteState(s);
                }
            }
            break;
        }
        case 22:
            s.WriteBits(static_cast<uint32_t>(I(0, 127)), 7);
            s.WriteBits(static_cast<uint32_t>(I(0, 1023)), 10);
            break;
        case 23:
            s.WriteBits(static_cast<uint32_t>(I(0, 1)), 1);
            break;
        default:
            break;
        }
    }
    for (int k = 0; k < 64; ++k)
        s.WriteBits(static_cast<uint32_t>(rng()), 32);
}

alignas(16) uint8_t g_pmStore[sizeof(C_NetMessage)];
alignas(16) uint8_t g_om[0x40];
uint32_t g_pBuf[WORDS], g_oBuf[WORDS];

void RunCommand(bool server)
{
    const int id = server ? (P(0.95) ? I(0, 11) : I(12, 14)) : (P(0.95) ? I(0, 23) : I(24, 26));
    // (What the original reads unchecked: the game for the client's 15, 3's game for the server's 0, 1 and 5; a
    // player's slot in range; the app's state for the client's 12 (with a soldier) and 21; the local slot's player for
    // the client's 4.)
    if (!g_script.game && ((!server && (id == 15 || id == 21)) || (server && (id == 0 || id == 1 || id == 5))))
        return;
    if (!server && id == 21 && !GetGameApp()->m_state)
        return;
    if (!server && id == 12 && !GetGameApp()->m_state)
        return;
    // (An empty squad's soldier: the original reads null in the client's 12 and 16; the port's Fixes skip it.)
    if (!server && (id == 12 || id == 16) && !g_script.squad)
        return;
    S_Writer w;
    WriteCommand(w.s, server, id);
    std::memcpy(g_pBuf, w.words, sizeof g_pBuf);
    std::memcpy(g_oBuf, w.words, sizeof g_oBuf);
    C_NetMessage *pm = new (g_pmStore) C_NetMessage();
    pm->Attach(g_pBuf, WORDS * 32);
    reinterpret_cast<void *(THISCALL *)(void *)>(O_MSG_CTOR)(g_om);
    OF<uint32_t *>(g_om, 4) = g_oBuf;
    OF<uint32_t>(g_om, 8) = 0;
    OF<uint32_t>(g_om, 0xC) = 0;
    OF<uint32_t>(g_om, 0x10) = WORDS * 32;
    SetSquad();
    if (!server && id == 0)
        g_freshFrom = static_cast<int>(PNet().m_players.size());
    else if (!server && id == 21)
        g_freshFrom = (g_gameRole & GAME_ROLE_SERVER) ? static_cast<int>(PNet().m_players.size()) : 0;
    g_line = std::string(server ? "server " : "client ") + N(id);
    g_trace += " " + g_line;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    if (server)
        GetGameApp()->HandleServerCommand(id, pm);
    else
        GetGameApp()->HandleClientCommand(id, pm);
    g_side = 1;
    reinterpret_cast<void(THISCALL *)(void *, int, void *)>(server ? O_SERVER_COMMAND : O_CLIENT_COMMAND)(g_oApp, id, g_om);
    g_side = 0;
    g_faking = false;
    CompareLogs();
    Check(pm->BitPosition() == OF<uint32_t>(g_om, 8) * 32 + OF<uint32_t>(g_om, 0xC),
          "the bits read " + N(OF<uint32_t>(g_om, 8) * 32 + OF<uint32_t>(g_om, 0xC)) + " / " + N(pm->BitPosition()));
    CompareState();
    CarryFreshSlots();
    pm->Attach(nullptr, 0);
    pm->~C_NetMessage();
    OF<uint32_t *>(g_om, 4) = nullptr;
}

void Round()
{
    g_trace.clear();
    SetWorld();
    SetSquad();
    CompareState();
    for (int k = I(1, 6); k > 0; --k)
        RunCommand(P(0.4));
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p (the test's 0x20000000 base: %08x) in round %d, %s, %s's side\n   (ops:%s)\n",
                e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) -
                                      reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) + 0x20000000u),
                g_round, g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    std::printf("  ebx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx |", e->ContextRecord->Ebx, e->ContextRecord->Esi, e->ContextRecord->Edi,
                e->ContextRecord->Ebp, e->ContextRecord->Esp);
    const auto *sp = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(e->ContextRecord->Esp));
    std::printf("  the original's code on the stack:");
    for (int i = 0, n = 0; i < 256 && n < 12; ++i)
        if (sp[i] >= 0x401000 && sp[i] < 0x80d000) {
            std::printf(" %08x", sp[i]);
            ++n;
        }
    std::printf("\n");
    std::fflush(stdout);
    ExitProcess(3);
}
LONG WINAPI FirstChance(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        OnCrash(e);
    return EXCEPTION_CONTINUE_SEARCH;
}

template <class M> int VSlot(M member)
{
    uint32_t raw;
    std::memcpy(&raw, &member, 4);
    return (raw & 1) ? static_cast<int>((raw - 1) / 4) : -1;
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_CMDS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("cmds_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);

    // The port's objects (their storage raw), the vtables' fakes.
    C_NetGame &net = PNet();
    new (&net.m_players) std::vector<S_NetPlayerState>();
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    alignas(16) static unsigned char campaign[0x400], humanStore[0x100];
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_campaign = reinterpret_cast<C_Campaign *>(campaign);
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_humanStore = reinterpret_cast<C_HumanStore *>(humanStore);
    new (&reinterpret_cast<C_ActorsModule *>(&g_actorsModule)->m_actors) std::vector<C_Actor *>();
    new (&reinterpret_cast<S_MissionSquad *>(g_pSquadStore)->states) std::vector<S_SquadSoldier *>();
    const int typeId = VSlot(&C_GameState::GetTypeId), state = VSlot(&C_GameState::GetState), pending = VSlot(&C_GameState::GetPendingState),
              setPending = VSlot(&C_GameState::SetPendingState), start = VSlot(&C_GameState::Start), stop = VSlot(&C_GameState::Stop),
              hit = VSlot(&C_Actor::Hit), startMission = VSlot(&C_Actor::StartMission), spectating = VSlot(&C_HumanController::SetSpectating),
              addMember = VSlot(&C_TeamBase::AddMember);
    if (typeId < 0 || state < 0 || pending < 0 || setPending < 0 || start < 0 || stop < 0 || hit < 0 || startMission < 0 || spectating < 0 ||
        addMember < 0) {
        std::printf("cmds_diff: a virtual's slot not found\n");
        return 1;
    }
    g_pStateVtable[typeId] = reinterpret_cast<void *>(&PGetTypeId);
    g_pStateVtable[state] = reinterpret_cast<void *>(&PGetState);
    g_pStateVtable[pending] = reinterpret_cast<void *>(&PGetPendingState);
    g_pStateVtable[setPending] = reinterpret_cast<void *>(&PSetPendingState);
    g_pStateVtable[start] = reinterpret_cast<void *>(&PStart);
    g_pStateVtable[stop] = reinterpret_cast<void *>(&PStop);
    g_pStateVtable[0] = g_pStateVtable[1] = reinterpret_cast<void *>(&PDeleteState);
    for (auto &st : g_pStates)
        *reinterpret_cast<void **>(st) = g_pStateVtable;
    g_pHumanVtable[hit] = reinterpret_cast<void *>(&PHumanHit);
    g_pHumanVtable[startMission] = reinterpret_cast<void *>(&PHumanStartMission);
    g_pControllerVtable[spectating] = reinterpret_cast<void *>(&PSetSpectating);
    for (int i = 0; i < HUMANS; ++i) {
        *reinterpret_cast<void **>(g_pHumans[i]) = g_pHumanVtable;
        *reinterpret_cast<void **>(g_pControllers[i]) = g_pControllerVtable;
        *reinterpret_cast<void **>(g_oHumans[i]) = g_oHumanVtable;
        *reinterpret_cast<void **>(g_oControllers[i]) = g_oControllerVtable;
    }
    g_pTeamVtable[addMember] = reinterpret_cast<void *>(&PAddMember);
    *reinterpret_cast<void **>(&reinterpret_cast<C_TeamManager *>(&g_teamManager)->m_playerTeam) = g_pTeamVtable;
    g_oHumanVtable[0xD4 / 4] = reinterpret_cast<void *>(&OHumanHit);
    g_oHumanVtable[0x3C / 4] = reinterpret_cast<void *>(&OHumanStartMission);
    g_oControllerVtable[0x60 / 4] = reinterpret_cast<void *>(&OSetSpectating);
    g_oTeamVtable[0x08 / 4] = reinterpret_cast<void *>(&OAddMember);
    OF<void *>(g_oTeam, 0) = g_oTeamVtable;
    g_oStateVtable[0] = reinterpret_cast<void *>(&ODeleteState);
    g_oStateVtable[0x04 / 4] = reinterpret_cast<void *>(&OStart);
    g_oStateVtable[0x08 / 4] = reinterpret_cast<void *>(&OStop);
    g_oStateVtable[0x1C / 4] = reinterpret_cast<void *>(&OGetTypeId);
    g_oStateVtable[0x20 / 4] = reinterpret_cast<void *>(&OGetState);
    g_oStateVtable[0x28 / 4] = reinterpret_cast<void *>(&OGetPendingState);
    g_oStateVtable[0x2C / 4] = reinterpret_cast<void *>(&OSetPendingState);
    for (auto &st : g_oStates)
        OF<void *>(st, 0) = g_oStateVtable;
    g_oMadeVtable[0] = reinterpret_cast<void *>(&ODeleteState);
    g_oMadeVtable[0x04 / 4] = reinterpret_cast<void *>(&OStart);
    g_oMadeVtable[0x08 / 4] = reinterpret_cast<void *>(&OStop);
    g_oMadeVtable[0x1C / 4] = reinterpret_cast<void *>(&OMadeTypeId);
    g_oMadeVtable[0x20 / 4] = reinterpret_cast<void *>(&OMadeGetState);
    g_oMadeVtable[0x28 / 4] = reinterpret_cast<void *>(&OMadeGetPending);
    g_oMadeVtable[0x2C / 4] = reinterpret_cast<void *>(&OMadeSetPending);
    // (The game too: its state read through its vtable; logged as state 9.)
    *reinterpret_cast<void **>(g_pGame) = g_pStateVtable;
    OF<void *>(g_oGame, 0) = g_oStateVtable;
    // The quantisations the messages' place and facing are read with (the player table's first two entries).
    g_quantization[0].Setup(S_vector4{400.0f, 400.0f, 400.0f, 0.0f}, S_vector4{-200.0f, -200.0f, -200.0f, 0.0f},
                            S_vector4{0.01f, 0.01f, 0.01f, 0.0f}, 100);
    g_quantization[1].Setup(S_vector4{6.29f, 6.29f, 6.29f, 0.0f}, S_vector4{-3.145f, -3.145f, -3.145f, 0.0f},
                            S_vector4{0.025f, 0.025f, 0.025f, 0.0f}, 100);
    g_netPlayerTable = reinterpret_cast<S_NetPlayer *>(g_quantization);
    g_netPlayerCount = 2;
    G<void *>(O_TABLE) = g_quantization;
    G<int>(O_COUNT) = 2;

    // The original's.
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(OAtexit));
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x0072f890, reinterpret_cast<const void *>(OGetPlayerStats)},
        {0x0072eff0, reinterpret_cast<const void *>(OGetPlayerHuman)},
        {0x00730830, reinterpret_cast<const void *>(OGetLocalHuman)},
        {0x00730320, reinterpret_cast<const void *>(OCountTeams)},
        {0x00730590, reinterpret_cast<const void *>(OTeamsBalanced)},
        {0x00730510, reinterpret_cast<const void *>(OSetPlayerTeam)},
        {0x00730900, reinterpret_cast<const void *>(OGetPlayerTeam)},
        {0x0072f060, reinterpret_cast<const void *>(OFindPlayer)},
        {0x0072efa0, reinterpret_cast<const void *>(OGetPlayerName)},
        {0x0072f6c0, reinterpret_cast<const void *>(OKickPlayer)},
        {0x0072fa50, reinterpret_cast<const void *>(OReadScores)},
        {0x007244e0, reinterpret_cast<const void *>(OOnConnected)},
        {0x00749f40, reinterpret_cast<const void *>(OStatsRead)},
        {0x0072dd70, reinterpret_cast<const void *>(OVoteStart)},
        {0x0072ddb0, reinterpret_cast<const void *>(OVoteBegin)},
        {0x0072dfe0, reinterpret_cast<const void *>(OVoteCountYes)},
        {0x0072de90, reinterpret_cast<const void *>(OPrintVoteResult)},
        {0x006dcd10, reinterpret_cast<const void *>(OGetGameApp)},
        {0x0072a480, reinterpret_cast<const void *>(OSendGameMessage)},
        {0x0072b590, reinterpret_cast<const void *>(OPostCommand)},
        {0x006c4e80, reinterpret_cast<const void *>(OAppPrintMessage)},
        {0x006987d0, reinterpret_cast<const void *>(OGetGame)},
        {0x0069a7d0, reinterpret_cast<const void *>(OEndMission)},
        {0x0069e9f0, reinterpret_cast<const void *>(OOnPlayerLeft)},
        {0x0069c660, reinterpret_cast<const void *>(ORestartMission)},
        {0x0069bf10, reinterpret_cast<const void *>(OApplyObjectiveStatus)},
        {0x00699bd0, reinterpret_cast<const void *>(OOnWarmupFinished)},
        {0x00699ae0, reinterpret_cast<const void *>(OOnWarmupStarted)},
        {0x00699860, reinterpret_cast<const void *>(OOnRoundOver)},
        {0x006998f0, reinterpret_cast<const void *>(OOnRespawnScreen)},
        {0x00699cf0, reinterpret_cast<const void *>(OOnSessionClosing)},
        {0x006994f0, reinterpret_cast<const void *>(OReadObjectiveStates)},
        {0x0069ad20, reinterpret_cast<const void *>(OSetCameraMode)},
        {0x0069e8f0, reinterpret_cast<const void *>(OOpenGameMenu)},
        {0x0069c240, reinterpret_cast<const void *>(OPrintMessage)},
        {0x0069e9b0, reinterpret_cast<const void *>(OShowGameMenuMessage)},
        {0x0069bca0, reinterpret_cast<const void *>(OLoadNextMap)},
        {0x006e5380, reinterpret_cast<const void *>(OTextsGet)},
        {0x0064d9e0, reinterpret_cast<const void *>(OHasUtf8)},
        {0x006ada30, reinterpret_cast<const void *>(OReadRules)},
        {0x006ad860, reinterpret_cast<const void *>(OReadCurrentMap)},
        {0x006ad600, reinterpret_cast<const void *>(OSettingsReset)},
        {0x006b8a20, reinterpret_cast<const void *>(OSetupMultiplayerMission)},
        {0x006b94b0, reinterpret_cast<const void *>(OGetCurrentMission)},
        {0x006ca920, reinterpret_cast<const void *>(OAddPlayer)},
        {0x006828b0, reinterpret_cast<const void *>(OAddChatLine)},
        {0x00685550, reinterpret_cast<const void *>(OSetTeam)},
        {0x00742850, reinterpret_cast<const void *>(OExecute)},
        {0x006a2f60, reinterpret_cast<const void *>(OSplitStacks)},
        {0x006a2a90, reinterpret_cast<const void *>(OClearItems)},
        {0x0048db60, reinterpret_cast<const void *>(OBroadcastReadState)},
        {0x00478520, reinterpret_cast<const void *>(OTemporaryReadState)},
        {0x006a1f50, reinterpret_cast<const void *>(OHumanStoreGet)},
        {0x006a1f80, reinterpret_cast<const void *>(OHumanStoreGetStats)},
        {0x005a5fe0, reinterpret_cast<const void *>(OGetControllerType)},
        {0x005a9b30, reinterpret_cast<const void *>(OSetChatText)},
        {0x0059f010, reinterpret_cast<const void *>(ORequestRespawn)},
        {0x005a15d0, reinterpret_cast<const void *>(OApplyRemoteHit)},
        {0x00612a60, reinterpret_cast<const void *>(OCreateSoldier)},
        {0x00461410, reinterpret_cast<const void *>(OGetPlayerTeamOfManager)},
        {0x00475dd0, reinterpret_cast<const void *>(OGetActor)},
        {0x0069e610, reinterpret_cast<const void *>(OOnPlayerJoined)},
        {0x0069dc90, reinterpret_cast<const void *>(OOnRespawnRequest)},
        {0x0069da50, reinterpret_cast<const void *>(ORespawnPlayer)},
        {0x006987e0, reinterpret_cast<const void *>(OGameCtor)},
        {0x0067edd0, reinterpret_cast<const void *>(OMenuStateCtor)},
        {0x0066b010, reinterpret_cast<const void *>(OMenuStateStart)},
        {0x0066b440, reinterpret_cast<const void *>(OMenuStateStop)},
        {0x006c1980, reinterpret_cast<const void *>(OMainMenuDelete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    G<void *>(O_TIME_SLOT) = reinterpret_cast<void *>(&FakeTimeGetTime);
    G<void *>(O_ENTER_SLOT) = reinterpret_cast<void *>(&OEnter);
    G<void *>(O_LEAVE_SLOT) = reinterpret_cast<void *>(&OLeave);
    G<void *>(O_PROFILE + 0x24) = g_mp;
    G<void *>(0x0098c0fc) = g_oProcessor;   // (the command processor: made on first use when null)
    std::memset(ONet(), 0, 0x5080);
    (void)O_TEAM_MANAGER;

    int rounds = std::getenv("HD2_CMDS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_CMDS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_CMDS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("cmds_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
