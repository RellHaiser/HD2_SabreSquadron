// Net part 5: the console's command processor (net/net_console_commands.cpp: ReadConsoleToken, Execute, ExecuteFile,
// the keyword table) against the original executable's. MinGW; build_cons.bat; runs cons_diff.exe ("cons_diff: N checks,
// 0 failures").
//
// Execute reaches into the game: the profile's multiplayer settings (the original's in a buffer of its layout: its
// maps vector MSVC6's, its map names at +1), the game app (its messages, commands and state), the console, the net
// game (its players, votes, kicks), the game (the next round, the map change, the mission's objectives), the texts,
// the window (IGraph, ShowWindow) and the files "exec" reads. Each is the test's on both sides: the port's link is
// given fakes of what it calls, the original's calls are redirected to fakes of the same names; both log alike (the
// texts printed, every call and its arguments) and answer from the round's script. Each round sets a random world (the
// role, the settings, the maps, the players, the objectives, the answers) and runs random command lines (keywords,
// values, words, quoted words, separators, files to exec) on both; after each the result, the logs, the settings, the
// server entry, the net limits and the set-up's game type are compared.
// HD2_CONS_ROUNDS: rounds (default 30,000; HD2_CONS_QUICK: 6,000); HD2_CONS_SEED; HD2_CONS_FIRST_CHANCE.
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/campaign.h"
#include "game/console.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_state.h"
#include "game/game_texts.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/controls.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

// ---- the logs (both sides) ----
int g_side;   // 0 the port, 1 the original
bool g_faking;
std::vector<std::string> g_log[2];
void Log(const std::string &s)
{
    if (g_faking)
        g_log[g_side].push_back(s);
}
std::string N(long long v) { return std::to_string(v); }
std::string Str(const char *s) { return s ? "\"" + std::string(s) + "\"" : "null"; }
// A command's text (game commands 7 and 8): its first 127 characters. (The original's copy of 0x80 bytes is not
// terminated when the text is that long: it reads on past it; the port's Fix ends it. See the notes.)
std::string CommandText(const char *text) { return Str(std::string(text, strnlen(text, 0x7F)).c_str()); }
std::string Hex(const void *p, size_t n)
{
    std::string s;
    char t[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(t, sizeof t, "%02x", static_cast<const uint8_t *>(p)[i]);
        s += t;
    }
    return s;
}

// The messages' texts given to SendGameMessage, freed after the command line (the game frees them after sending;
// Execute prints the chat line after handing it over).
std::vector<void *> g_freeLater;

// ---- the round's script (both sides read the same) ----
struct S_Script {
    bool gameExists = true;
    bool addMapAnswer = true;
    bool removeMapAnswer = true;
    int findPlayerAnswer = -1;
    bool adminAnswer = false;
    bool voteAnswer = true;
    int codecText = 0x700;
} g_script;

// ================================================================================================================
// The port's world: the symbols net_console_commands.cpp needs (its link's undefined references), faked.
// ================================================================================================================
uint32_t g_gameRole;
uint32_t g_graphFlags;
int g_localPlayerSlot;
int g_netMaxFreq, g_netMaxPing, g_netMaxInactivity;

// The fake IGraph (both programs': its vtable's slots stdcall, `this` on the stack).
struct S_FakeGraph {
    void **vtable;
} g_graph;
void *g_graphVtable[0x80];
void __stdcall GraphSetFlags(void *, uint32_t flags) { Log("IGraph::SetFlags " + N(flags)); }
HWND __stdcall GraphGetHWND(void *)
{
    Log("IGraph::GetHWND");
    return reinterpret_cast<HWND>(0x1234);
}
void __stdcall GraphInitMouse(void *, int on) { Log("IGraph::InitMouse " + N(on)); }
void __stdcall GraphUnexpected(void *)
{
    std::printf("an IGraph slot not faked was called\n");
    std::exit(4);
}
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graph); }

BOOL WINAPI FakeShowWindow(HWND window, int show)
{
    Log("ShowWindow " + N(reinterpret_cast<intptr_t>(window)) + " " + N(show));
    return TRUE;
}

// The game app (raw: its m_state is the fake state below).
alignas(16) unsigned char g_appStore[0x400];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_appStore); }
void C_GameApp::PrintMessage(const char *text) { Log("print " + Str(text)); }
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    // (Command 7 / 8: a, the text copied for it, the command's to free.)
    Log("PostCommand " + N(command) + " " + (command == 7 || command == 8 ? CommandText(reinterpret_cast<const char *>(a)) : N(a)) + " " +
        N(b) + " " + N(flush));
    if (command == 7 || command == 8)
        operator delete(reinterpret_cast<void *>(a));
}
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    // (Type 4: a, the chat line (0x110 bytes: its text, the sender at +0x104), the message's to free.)
    std::string s = "SendGameMessage " + N(type) + " " + N(to) + " ";
    if (type == 4) {
        const char *line = reinterpret_cast<const char *>(a);
        int from;
        std::memcpy(&from, line + 0x104, 4);
        s += Str(line) + " from " + N(from);
        g_freeLater.push_back(reinterpret_cast<void *>(a));   // (the console prints it after: freed after the line)
    } else {
        s += N(a);
    }
    Log(s + " " + N(b) + " " + N(reliable));
}

// The game state (its pending state logged).
struct C_FakeState : C_GameState {
    void SetPendingState(int state) override { Log("SetPendingState " + N(state)); }
    void SetState(int state) override { Log("SetState " + N(state)); }
};
C_FakeState g_portState;

// The console.
alignas(16) unsigned char g_consoleStore[0x100];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_consoleStore); }
void C_Console::AddLine(const char *text) { Log("console " + Str(text)); }
// The console's commands typed (each side its own copy of the round's).
std::vector<std::string> g_typed[2];
size_t g_typedAt[2];
char *C_Console::PopCommand()
{
    Log("PopCommand");
    if (g_typedAt[0] >= g_typed[0].size())
        return nullptr;
    return &g_typed[0][g_typedAt[0]++][0];
}

// The game (raw: its mission pointer at m_mission).
alignas(16) unsigned char g_gameStore[0x400];
alignas(16) unsigned char g_missionStore[0x400];
C_game *GetGame() { return g_script.gameExists ? reinterpret_cast<C_game *>(g_gameStore) : nullptr; }
void C_game::StartNextRound() { Log("StartNextRound"); }
void ChangeMap(const char *name) { Log("ChangeMap " + Str(name)); }
std::vector<uint8_t *> g_objectives;   // (each 0x40 bytes: status +0x10, the allied text +0x1C; both programs')
S_MpObjective *C_MpMissionDef::GetMpObjective(int index) const
{
    return index >= 0 && index < static_cast<int>(g_objectives.size()) ? reinterpret_cast<S_MpObjective *>(g_objectives[static_cast<size_t>(index)]) : nullptr;
}

// The texts: "T<id>".
C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    return C_fstr("T%d", id);
}
int GetVoiceCodecTextId(int codec)
{
    Log("GetVoiceCodecTextId " + N(codec));
    return g_script.codecText + codec;
}

// The net game (its players: the test's array; the vote's and the kicks' calls logged).
bool IsPlayerAdmin(int slot)
{
    Log("IsPlayerAdmin " + N(slot));
    return g_script.adminAnswer;
}
// (The player state's constructor is net_players.cpp's: its members' defaults.)
S_NetPlayerState::S_NetPlayerState() {}
void PostVote(int kind, int arg) { Log("PostVote " + N(kind) + " " + N(arg)); }
S_NetPlayerState *C_NetGame::LockPlayers()
{
    Log("LockPlayers");
    return m_players.empty() ? nullptr : m_players.data();
}
void C_NetGame::UnlockPlayers() { Log("UnlockPlayers"); }
void C_NetGame::KickPlayer(int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void C_NetGame::KickPlayerByName(const char *name, int reason) { Log("KickPlayerByName " + Str(name) + " " + N(reason)); }
int C_NetGame::FindPlayerByName(const char *name)
{
    Log("FindPlayerByName " + Str(name));
    return g_script.findPlayerAnswer;
}
int C_NetGame::Host(const S_NetSessionDesc &desc, bool firewall)
{
    Log("Host " + Hex(&desc, sizeof desc) + " " + N(firewall));
    return 0;
}
int C_NetGame::Connect(S_ServerEntry *server)
{
    Log("Connect " + Hex(server, sizeof(S_ServerEntry)));
    return 0;
}
bool S_NetVote::Vote()
{
    Log("Vote");
    return g_script.voteAnswer;
}
void S_NetVote::Start(int kind, int arg) { Log("VoteStart " + N(kind) + " " + N(arg)); }
void S_NetVote::CountYes() { Log("CountYes"); }

// The settings' set-up and maps (the calls logged; the set-up's game type stored).
void S_MultiplayerSettings::MakeSetup() { Log("MakeSetup"); }
void S_MultiplayerSettings::DeleteSetup() { Log("DeleteSetup"); }
void S_MultiplayerSettings::ApplySetupRules() { Log("ApplySetupRules " + N(gameType)); }
bool S_MultiplayerSettings::AddMap(const char *name)
{
    Log("AddMap " + Str(name));
    return g_script.addMapAnswer;
}
bool S_MultiplayerSettings::RemoveMap(const char *name)
{
    Log("RemoveMap " + Str(name));
    return g_script.removeMapAnswer;
}
// The set-ups' map groups: one per game type, each a list of names (both programs' objects made from them).
std::vector<std::string> g_setupMaps[4];
alignas(16) unsigned char g_portSetup[0x400];
S_MpMapGroup g_portGroups[4];
S_MpMapGroup &C_MpGameSetup::GetCurrentMaps()
{
    Log("GetCurrentMaps " + N(gameType));
    return g_portGroups[gameType & 3];
}

namespace {

// ================================================================================================================
// The original's world.
// ================================================================================================================
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))

const uint32_t O_EXECUTE = 0x00742850, O_READ_TOKEN = 0x007445d0, O_EXECUTE_FILE = 0x007440d0;
const uint32_t O_SETTINGS_PTR = 0x008aea30, O_ROLE = 0x00982100, O_LOCAL_SLOT = 0x00982104, O_GRAPH_FLAGS = 0x00981e6c;
const uint32_t O_MAX_PING = 0x009820d4, O_MAX_FREQ = 0x009820d8, O_MAX_INACTIVITY = 0x009820dc;
const uint32_t O_PROCESSOR = 0x0098c0fc, O_CONSOLE = 0x0098c100, O_NETGAME = 0x0097d070, O_KEYWORDS = 0x00846088;
const uint32_t O_EMPTY_TEXT = 0x00867a90, O_FSTR = 0x00405100;

alignas(16) uint8_t g_oSettings[0x300];
alignas(16) uint8_t g_oApp[0x100];
alignas(16) uint8_t g_oState[0x40];
void *g_oStateVtable[0x10];
alignas(16) uint8_t g_oConsole[0x100];
alignas(16) uint8_t g_oGame[0x200];
alignas(16) uint8_t g_oMission[0x100];
alignas(16) uint8_t g_oSetup[0x100];
uint8_t g_oPlayersVector[0x10];       // (LockPlayers' answer: an MSVC6 vector, first +4, last +8)
std::vector<uint8_t *> g_oMaps;       // (the settings' maps: the name's C_str at +1)
uint8_t g_oGroups[4][0x20];           // (GetCurrentMaps' answers: the vector first +4, last +8)
std::vector<uint8_t *> g_oGroupMaps[4];

// An original C_str (refs, length, chars) made by the original's C_fstr.
void *OString(const char *text)
{
    void *s = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(&s, "%s", text);
    return s;
}
const char *OText(const void *rep) { return rep ? static_cast<const char *>(rep) + 8 : ""; }

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }

void *OGetGameApp() { return g_oApp; }
void THISCALL OPrint(void *, const char *text) { Log("print " + Str(text)); }
void THISCALL OPostCommand(void *, int command, intptr_t a, intptr_t b, bool flush)
{
    Log("PostCommand " + N(command) + " " + (command == 7 || command == 8 ? CommandText(reinterpret_cast<const char *>(a)) : N(a)) + " " +
        N(b) + " " + N(flush));
    if (command == 7 || command == 8)
        std::free(reinterpret_cast<void *>(a));
}
void THISCALL OSendGameMessage(void *, int type, int to, intptr_t a, int b, bool reliable)
{
    std::string s = "SendGameMessage " + N(type) + " " + N(to) + " ";
    if (type == 4) {
        const char *line = reinterpret_cast<const char *>(a);
        int from;
        std::memcpy(&from, line + 0x104, 4);
        s += Str(line) + " from " + N(from);
        g_freeLater.push_back(reinterpret_cast<void *>(a));
    } else {
        s += N(a);
    }
    Log(s + " " + N(b) + " " + N(reliable));
}
void THISCALL OSetPendingState(void *, int state) { Log("SetPendingState " + N(state)); }
void THISCALL OSetState(void *, int state) { Log("SetState " + N(state)); }
void THISCALL OAddLine(void *, const char *text) { Log("console " + Str(text)); }
void *OGetGame() { return g_script.gameExists ? g_oGame : nullptr; }
void THISCALL OStartNextRound(void *) { Log("StartNextRound"); }
void __stdcall OChangeMap(const char *name) { Log("ChangeMap " + Str(name)); }
void *OGetIGraph() { return &g_graph; }
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    OF<void *>(ret, 0) = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(ret, "T%d", id);
    return ret;
}
int __stdcall OGetVoiceCodecTextId(int codec)
{
    Log("GetVoiceCodecTextId " + N(codec));
    return g_script.codecText + codec;
}
bool __stdcall OIsPlayerAdmin(int slot)
{
    Log("IsPlayerAdmin " + N(slot));
    return g_script.adminAnswer;
}
void __stdcall OPostVote(int kind, int arg) { Log("PostVote " + N(kind) + " " + N(arg)); }
void *THISCALL OLockPlayers(void *)
{
    Log("LockPlayers");
    return g_oPlayersVector;
}
void THISCALL OUnlockPlayers(void *) { Log("UnlockPlayers"); }
void THISCALL OKickPlayer(void *, int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void THISCALL OKickPlayerByName(void *, const char *name, int reason) { Log("KickPlayerByName " + Str(name) + " " + N(reason)); }
int THISCALL OFindPlayerByName(void *, const char *name)
{
    Log("FindPlayerByName " + Str(name));
    return g_script.findPlayerAnswer;
}
bool FAST OVote(void *)
{
    Log("Vote");
    return g_script.voteAnswer;
}
void THISCALL OVoteStart(void *, int kind, int arg) { Log("VoteStart " + N(kind) + " " + N(arg)); }
void FAST OCountYes(void *) { Log("CountYes"); }
void FAST OMakeSetup(void *) { Log("MakeSetup"); }
void FAST OApplySetupRules(void *s) { Log("ApplySetupRules " + N(OF<int>(s, 0x224))); }
// (The original's AddMap takes a C_str: the word made into one, as a format.)
bool THISCALL OAddMap(void *, const void *name)
{
    Log("AddMap " + Str(OText(*static_cast<void *const *>(name))));
    return g_script.addMapAnswer;
}
bool THISCALL ORemoveMap(void *, const char *name)
{
    Log("RemoveMap " + Str(name));
    return g_script.removeMapAnswer;
}
void FAST ODeleteSetup(void *) { Log("DeleteSetup"); }
char *THISCALL OPopCommand(void *)
{
    Log("PopCommand");
    if (g_typedAt[1] >= g_typed[1].size())
        return nullptr;
    return &g_typed[1][g_typedAt[1]++][0];
}
int THISCALL OHost(void *, const void *desc, bool firewall)
{
    Log("Host " + Hex(desc, 0x168) + " " + N(firewall));
    return 0;
}
int THISCALL OConnect(void *, const void *server)
{
    Log("Connect " + Hex(server, 0x9C));
    return 0;
}
void THISCALL OSetGameType(void *setup, int type) { OF<int>(setup, 0x18) = type; }
void *FAST OGetCurrentMaps(void *setup)
{
    const int type = OF<int>(setup, 0x18);
    Log("GetCurrentMaps " + N(type));
    return g_oGroups[type & 3];
}

// ---- checks ----
int checks, failures;
int g_round;
std::string g_line;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, command %s: %s\n", g_round, Str(g_line.c_str()).c_str(), what.c_str());
}
void Check(bool ok, const std::string &what)
{
    ++checks;
    if (!ok)
        Fail(what);
}

std::mt19937 rng;
int I(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); }
bool P(double p) { return std::uniform_real_distribution<double>(0, 1)(rng) < p; }

// ---- the settings, both programs' ----
S_MultiplayerSettings *g_pSettings;
alignas(16) unsigned char g_pSettingsStore[sizeof(S_MultiplayerSettings) + 16];
std::vector<C_MpMap *> g_pMaps;   // (the port's map objects, raw)

C_MpMap *PortMap(const std::string &name)
{
    auto *m = static_cast<C_MpMap *>(std::calloc(1, sizeof(C_MpMap) + 16));
    new (&m->name) C_str(name.c_str());
    return m;
}
uint8_t *OrigMap(const std::string &name)
{
    auto *m = static_cast<uint8_t *>(std::calloc(1, 0x80));
    void *rep = name.empty() ? nullptr : OString(name.c_str());
    std::memcpy(m + 1, &rep, 4);
    return m;
}
void OVector(uint8_t *at, std::vector<uint8_t *> &items)
{
    // An MSVC6 vector's first and last (+4, +8).
    uint8_t **first = items.empty() ? nullptr : items.data();
    std::memcpy(at + 4, &first, 4);
    uint8_t **last = first ? first + items.size() : nullptr;
    std::memcpy(at + 8, &last, 4);
    std::memcpy(at + 12, &last, 4);
}

const char *const NAMES[] = {"Africa1", "Normandy", "", "x", "Map With Spaces", "abcdefghijklmnopqrstuvwxyz0123456789"};
std::string RandomName() { return NAMES[I(0, 5)]; }

void SetWorld()
{
    // The role (local: 7; a server 1, a client 2, a dedicated server's, none).
    static const int roles[] = {7, 1, 2, 3, 0, 5};
    const int role = roles[I(0, 5)];
    g_gameRole = role;
    G<int>(O_ROLE) = role;
    const int slot = I(0, 5);
    g_localPlayerSlot = slot;
    G<int>(O_LOCAL_SLOT) = slot;
    g_netMaxPing = G<int>(O_MAX_PING) = I(0, 1000);
    g_netMaxFreq = G<int>(O_MAX_FREQ) = I(1, 50);
    g_netMaxInactivity = G<int>(O_MAX_INACTIVITY) = I(0, 600) * 1000;
    // The script.
    g_script.gameExists = P(0.85);
    g_script.addMapAnswer = P(0.6);
    g_script.removeMapAnswer = P(0.6);
    g_script.findPlayerAnswer = P(0.5) ? -1 : I(0, 15);
    g_script.adminAnswer = P(0.5);
    g_script.voteAnswer = P(0.5);
    // The settings: the port's (raw: its C_strs and vector empty when zeroed) and the original's buffer.
    for (C_MpMap *m : g_pMaps) {
        m->name.~C_str();
        std::free(m);
    }
    g_pMaps.clear();
    for (uint8_t *m : g_oMaps)
        std::free(m);
    g_oMaps.clear();
    if (g_pSettings) {
        g_pSettings->password.~C_str();
        g_pSettings->adminPassword.~C_str();
    }
    std::memset(g_pSettingsStore, 0, sizeof g_pSettingsStore);
    g_pSettings = reinterpret_cast<S_MultiplayerSettings *>(g_pSettingsStore);
    std::memset(g_oSettings, 0, sizeof g_oSettings);
    uint8_t *o = g_oSettings;
    S_MultiplayerSettings *p = g_pSettings;
    p->sessionType = OF<int>(o, 4) = I(0, 2);
    const std::string session = RandomName();
    std::strcpy(p->sessionName, session.c_str());
    std::strcpy(reinterpret_cast<char *>(o + 8), session.c_str());
    p->dedicated = P(0.3);
    OF<uint8_t>(o, 0x170) = p->dedicated ? 1 : 0;
    p->unknown174 = OF<int>(o, 0x174) = I(0, 1);
    p->unknown178 = OF<int>(o, 0x178) = I(0, 7);
    for (int i = 0; i < 0x9C; ++i)
        p->unknown17C[i] = o[0x17C + i] = static_cast<uint8_t>(P(0.5) ? 0 : I(0, 255));
    if (P(0.4)) {
        const std::string pw = RandomName();
        new (&p->password) C_str(pw.c_str());
        OF<void *>(o, 0x218) = OString(pw.c_str());
    }
    p->port = OF<int>(o, 0x220) = I(0, 70000);
    p->gameType = OF<int>(o, 0x224) = I(0, 3);
    for (int i = 0; i < 22; ++i)
        (&p->maxClients)[i] = OF<int>(o, 0x228 + 4 * i) = P(0.3) ? 0 : I(-5, 200);
    const int maps = P(0.25) ? 0 : I(1, 4);
    for (int i = 0; i < maps; ++i) {
        const std::string name = RandomName();
        g_pMaps.push_back(PortMap(name));
        g_oMaps.push_back(OrigMap(name));
    }
    p->maps.reserve(8);
    p->maps.assign(g_pMaps.begin(), g_pMaps.end());
    OVector(o + 0x280, g_oMaps);
    p->currentMap = OF<int>(o, 0x290) = 0;
    // The set-up (its game type) and its map groups.
    std::memset(g_portSetup, 0, sizeof g_portSetup);
    std::memset(g_oSetup, 0, sizeof g_oSetup);
    p->setup = reinterpret_cast<C_MpGameSetup *>(g_portSetup);
    OF<void *>(o, 0x294) = g_oSetup;
    for (int t = 0; t < 4; ++t) {
        for (C_MpMap *m : g_portGroups[t].maps) {
            m->name.~C_str();
            std::free(m);
        }
        g_portGroups[t].maps.clear();
        for (uint8_t *m : g_oGroupMaps[t])
            std::free(m);
        g_oGroupMaps[t].clear();
        for (int k = I(0, 3); k > 0; --k) {
            const std::string name = RandomName();
            g_portGroups[t].maps.push_back(PortMap(name));
            g_oGroupMaps[t].push_back(OrigMap(name));
        }
        std::memset(g_oGroups[t], 0, sizeof g_oGroups[t]);
        OVector(g_oGroups[t], g_oGroupMaps[t]);
    }
    // The players: one array both programs read (the same layout: checked in main); the original's vector's first and
    // last point into the port's.
    std::vector<S_NetPlayerState> &players = g_netGame.m_players;
    players.assign(static_cast<size_t>(I(0, 6)), S_NetPlayerState());
    for (S_NetPlayerState &pl : players) {
        pl.connection = P(0.8) ? I(1, 99) : 0;
        if (P(0.8))
            std::strcpy(pl.name, RandomName().c_str());
    }
    uint8_t *first = players.empty() ? nullptr : reinterpret_cast<uint8_t *>(players.data());
    std::memset(g_oPlayersVector, 0, sizeof g_oPlayersVector);
    OF<uint8_t *>(g_oPlayersVector, 4) = first;
    OF<uint8_t *>(g_oPlayersVector, 8) = first ? first + players.size() * sizeof(S_NetPlayerState) : nullptr;
    // The mission's objectives (one set both programs read).
    for (uint8_t *x : g_objectives)
        std::free(x);
    g_objectives.clear();
    for (int k = I(0, 4); k > 0; --k) {
        auto *x = static_cast<uint8_t *>(std::calloc(1, 0x40));
        OF<int>(x, 0x10) = I(0, 2);
        OF<int>(x, 0x1C) = I(0x100, 0x110);
        g_objectives.push_back(x);
    }
    auto &mission = *reinterpret_cast<C_MissionDef *>(g_missionStore);
    mission.m_objectives.assign(reinterpret_cast<S_MissionObjective **>(g_objectives.data()),
                                reinterpret_cast<S_MissionObjective **>(g_objectives.data() + g_objectives.size()));
    std::vector<uint8_t *> &oo = g_objectives;
    OF<uint8_t *>(g_oMission, 0x34) = oo.empty() ? nullptr : reinterpret_cast<uint8_t *>(oo.data());
    OF<uint8_t *>(g_oMission, 0x38) = oo.empty() ? nullptr : reinterpret_cast<uint8_t *>(oo.data() + oo.size());
    g_gameProfile.m_mpSettings = g_pSettings;
    // The command processor made anew (its constructor's MakeSetup logged on both sides).
    ClearCommandProcessor();
    G<void *>(O_PROCESSOR) = nullptr;
}

// ---- the comparison ----
void CompareSettings()
{
    const S_MultiplayerSettings *p = g_pSettings;
    const uint8_t *o = g_oSettings;
    std::string s;
    if (p->sessionType != OF<int>(const_cast<uint8_t *>(o), 4))
        s += "sessionType " + N(OF<int>(const_cast<uint8_t *>(o), 4)) + " / " + N(p->sessionType) + "; ";
    if (std::strcmp(p->sessionName, reinterpret_cast<const char *>(o + 8)) != 0)
        s += "sessionName " + Str(reinterpret_cast<const char *>(o + 8)) + " / " + Str(p->sessionName) + "; ";
    if (p->dedicated != (o[0x170] != 0))
        s += "dedicated; ";
    if (p->unknown174 != OF<int>(const_cast<uint8_t *>(o), 0x174) || p->unknown178 != OF<int>(const_cast<uint8_t *>(o), 0x178))
        s += "domain / codec " + N(OF<int>(const_cast<uint8_t *>(o), 0x174)) + " " + N(OF<int>(const_cast<uint8_t *>(o), 0x178)) +
             " / " + N(p->unknown174) + " " + N(p->unknown178) + "; ";
    if (std::memcmp(p->unknown17C, o + 0x17C, 0x9C) != 0)
        s += "the server entry; ";
    if (std::strcmp(p->password.c_str(), OText(OF<void *>(const_cast<uint8_t *>(o), 0x218))) != 0)
        s += "password " + Str(OText(OF<void *>(const_cast<uint8_t *>(o), 0x218))) + " / " + Str(p->password.c_str()) + "; ";
    if (std::strcmp(p->adminPassword.c_str(), OText(OF<void *>(const_cast<uint8_t *>(o), 0x21C))) != 0)
        s += "admin password " + Str(OText(OF<void *>(const_cast<uint8_t *>(o), 0x21C))) + " / " + Str(p->adminPassword.c_str()) + "; ";
    if (p->port != OF<int>(const_cast<uint8_t *>(o), 0x220) || p->gameType != OF<int>(const_cast<uint8_t *>(o), 0x224))
        s += "port / game type " + N(OF<int>(const_cast<uint8_t *>(o), 0x220)) + " " + N(OF<int>(const_cast<uint8_t *>(o), 0x224)) +
             " / " + N(p->port) + " " + N(p->gameType) + "; ";
    for (int i = 0; i < 22; ++i)
        if ((&p->maxClients)[i] != OF<int>(const_cast<uint8_t *>(o), 0x228 + 4 * i))
            s += "rule " + N(i) + " " + N(OF<int>(const_cast<uint8_t *>(o), 0x228 + 4 * i)) + " / " + N((&p->maxClients)[i]) + "; ";
    if (reinterpret_cast<const uint8_t *>(p->setup) != g_portSetup || OF<void *>(const_cast<uint8_t *>(o), 0x294) != g_oSetup)
        s += "setup pointer; ";
    else if (reinterpret_cast<const C_MpGameSetup *>(g_portSetup)->gameType != OF<int>(g_oSetup, 0x18))
        s += "the set-up's game type; ";
    Check(s.empty(), s);
    Check(g_netMaxPing == G<int>(O_MAX_PING) && g_netMaxFreq == G<int>(O_MAX_FREQ) && g_netMaxInactivity == G<int>(O_MAX_INACTIVITY),
          "the net limits " + N(G<int>(O_MAX_PING)) + " " + N(G<int>(O_MAX_FREQ)) + " " + N(G<int>(O_MAX_INACTIVITY)) + " / " +
              N(g_netMaxPing) + " " + N(g_netMaxFreq) + " " + N(g_netMaxInactivity));
}

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
            s += " (call " + N(i) + ")\n     orig: " + x.substr(0, 200) + "\n     port: " + y.substr(0, 200);
            break;
        }
    }
    Check(false, s);
}

// ---- the command lines ----
const char *const KEYWORD_WORDS[] = {
    "maxclients", "pointlimit", "roundlimit", "roundcount", "warmup", "respawntime", "allowrespawn", "friendlyfire",
    "autoteambalance", "3rdpersonview", "spawnprotection", "inversedamage", "fallingdmg", "floodprotect", "allowvehicles",
    "autorestart", "coopdifficulty", "cooplives", "allowcrosshair", "spawnonstart", "teamlives", "mapname", "removemap",
    "style", "deathmatch", "occupation", "objectives", "cooperative", "domain", "local", "internet", "sessionname", "help",
    "server", "dedicated", "join", "vote", "voteyes", "listplayers", "kickplayer", "nextmap", "restart", "port",
    "password", "quit", "voicechat", "listmaps", "adminpass", "adminlogin", "svcmd", "maxping", "maxfreq", "asay",
    "maxinactivity", "listobjectives", "vr12", "sc03", "sc06", "truespeech", "gsm", "adpcm", "pcm", "none", "dumpentity",
    "MaxClients", "PORT"};
std::string RandomValue()
{
    switch (I(0, 9)) {
    case 0: return N(I(-20, 20));
    case 1: return N(I(0, 120));
    case 2: return N(I(0, 7000));
    case 3: return N(I(60000, 70000));
    case 4: return "-" + N(I(1, 9));
    case 5: return "0";
    case 6: return "\"" + RandomName() + "\"";
    case 7: return "127.0.0.1";
    case 8: return KEYWORD_WORDS[I(0, static_cast<int>(sizeof KEYWORD_WORDS / sizeof KEYWORD_WORDS[0]) - 1)];
    default: return RandomName().empty() ? "word" : RandomName();
    }
}
const char *SEPARATORS[] = {" ", "  ", "\t", " > ", " -", " - ", ">", " -->"};
std::string RandomLine()
{
    std::string line;
    if (P(0.2))
        line += SEPARATORS[I(0, 7)];
    const int parts = I(1, 4);
    for (int k = 0; k < parts; ++k) {
        if (P(0.9))
            line += KEYWORD_WORDS[I(0, static_cast<int>(sizeof KEYWORD_WORDS / sizeof KEYWORD_WORDS[0]) - 1)];
        else
            line += RandomName().empty() ? "nokeyword" : RandomName();
        for (int v = I(0, 2); v > 0; --v)
            line += std::string(SEPARATORS[I(0, 2)]) + RandomValue();
        if (k + 1 < parts)
            line += SEPARATORS[I(0, 7)];
    }
    if (P(0.1))
        line += SEPARATORS[I(0, 7)];
    if (P(0.05))
        line = "";
    return line;
}

// A file of command lines for "exec" (short lines: the original's %[^\n] has no width, the port's Fix cuts at 99).
std::string g_execFile;
void WriteExecFile()
{
    FILE *f = std::fopen(g_execFile.c_str(), "wb");
    const int lines = I(0, 4);
    for (int i = 0; i < lines; ++i) {
        std::string l = RandomLine();
        if (l.find("exec") != std::string::npos || l.size() > 90)
            l = "help";
        if (l.find("listobjectives") != std::string::npos)
            g_script.gameExists = true;   // (read unchecked on both sides)
        std::fputs(l.c_str(), f);
        if (i + 1 < lines || P(0.5))
            std::fputs(P(0.2) ? "\r\n" : "\n", f);
    }
    std::fclose(f);
}

using FExecute = bool(THISCALL *)(void *, const char *);

void RunLine(const std::string &line)
{
    g_line = line;
    // (listobjectives reads the game's mission unchecked on both sides: a game is there.)
    if (line.find("listobjectives") != std::string::npos)
        g_script.gameExists = true;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const bool a = GetCommandProcessor()->Execute(line.c_str());
    g_side = 1;
    // (The original's Execute takes no `this` it uses but its processor's: the global's, made on the first call.)
    const bool b = OriginalAt<FExecute>(O_EXECUTE)(G<void *>(O_PROCESSOR), line.c_str());
    g_side = 0;
    g_faking = false;
    Check(a == b, "the result " + N(b) + " / " + N(a));
    for (void *x : g_freeLater)
        std::free(x);
    g_freeLater.clear();
    CompareLogs();
    CompareSettings();
}

// The console state (C_ConsoleState): made with a command or none, then started, its banner shown, updated (the
// console's typed commands run, its first command, hosting or joining from a dedicated server or not), compared after
// each: its state, pending state, command, mode, shown mark and the logs.
const uint32_t O_CS_CTOR = 0x007442e0, O_CS_START = 0x00744360, O_CS_BANNER = 0x007443a0, O_CS_UPDATE = 0x007443d0;
void CompareConsoleState(const C_ConsoleState *p, uint8_t *o, const char *what)
{
    std::string s;
    if (p->m_state != OF<int>(o, 4) || p->m_pendingState != OF<int>(o, 8))
        s += "state / pending " + N(OF<int>(o, 4)) + " " + N(OF<int>(o, 8)) + " / " + N(p->m_state) + " " + N(p->m_pendingState) + "; ";
    if (std::strcmp(p->m_command, reinterpret_cast<const char *>(o + 0xC)) != 0)
        s += "command " + Str(reinterpret_cast<const char *>(o + 0xC)) + " / " + Str(p->m_command) + "; ";
    if (p->m_mode != OF<int>(o, 0x20C) || p->m_shown != (o[0x210] != 0))
        s += "mode / shown " + N(OF<int>(o, 0x20C)) + " " + N(o[0x210]) + " / " + N(p->m_mode) + " " + N(p->m_shown) + "; ";
    Check(s.empty(), std::string(what) + ": " + s);
}

template <class FP, class FO> void BothCalls(FP port, FO orig)
{
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    port();
    g_side = 1;
    orig();
    g_side = 0;
    g_faking = false;
    for (void *x : g_freeLater)
        std::free(x);
    g_freeLater.clear();
}

void OpConsoleState()
{
    std::string command = P(0.2) ? std::string() : RandomLine();
    const bool none = command.empty() && P(0.5);
    g_line = "(console state) " + command;
    alignas(16) static uint8_t o[0x220];
    std::memset(o, 0xEE, sizeof o);
    C_ConsoleState *p = nullptr;
    BothCalls([&] { p = new C_ConsoleState(none ? nullptr : command.c_str()); },
              [&] { reinterpret_cast<void *(THISCALL *)(void *, const char *)>(O_CS_CTOR)(o, none ? nullptr : command.c_str()); });
    // (The original's constructor leaves the mode as the memory was: see the notes.)
    OF<int>(o, 0x20C) = 0;
    CompareLogs();
    CompareConsoleState(p, o, "constructed");
    for (int k = I(1, 5); k > 0; --k) {
        const int what = I(0, 3);
        if (what == 0) {
            BothCalls([&] { p->Start(nullptr); }, [&] { reinterpret_cast<bool(THISCALL *)(void *, void *)>(O_CS_START)(o, nullptr); });
        } else if (what == 1) {
            BothCalls([&] { p->ShowBanner(); }, [&] { OriginalAt<void(THISCALL *)(void *)>(O_CS_BANNER)(o); });
        } else {
            // The state it finds (hosting or joining asked for, or another), the commands typed, a dedicated server or not.
            static const int states[] = {GAME_STATE_HOST_SESSION, GAME_STATE_JOIN_SESSION, GAME_STATE_RUNNING, GAME_STATE_QUIT, 0};
            const int state = states[I(0, 4)];
            p->m_state = state;
            OF<int>(o, 4) = state;
            const bool dedicated = P(0.5);
            g_pSettings->dedicated = dedicated;
            g_oSettings[0x170] = dedicated ? 1 : 0;
            g_typed[0].clear();
            for (int t = I(0, 2); t > 0; --t)
                g_typed[0].push_back(RandomLine());
            g_typed[1] = g_typed[0];
            g_typedAt[0] = g_typedAt[1] = 0;
            if (P(0.2)) {
                p->m_shown = !p->m_shown;
                o[0x210] = p->m_shown ? 1 : 0;
            }
            if (P(0.3)) {
                const std::string c = RandomLine();
                std::strcpy(p->m_command, c.c_str());
                std::strcpy(reinterpret_cast<char *>(o + 0xC), c.c_str());
            }
            // (listobjectives reads the game's mission unchecked on both sides: a game is there for it.)
            for (const std::string &t : g_typed[0])
                if (t.find("listobjectives") != std::string::npos)
                    g_script.gameExists = true;
            if (std::string(p->m_command).find("listobjectives") != std::string::npos)
                g_script.gameExists = true;
            BothCalls([&] { p->Update(100); }, [&] { reinterpret_cast<void(THISCALL *)(void *, int)>(O_CS_UPDATE)(o, 100); });
        }
        CompareLogs();
        CompareConsoleState(p, o, "after a call");
        CompareSettings();
    }
    delete p;
}

// The command processor made and destroyed (its set-up made, then deleted).
void OpProcessor()
{
    g_line = "(the command processor's life)";
    alignas(16) static uint8_t o[0x10];
    BothCalls([&] { delete new C_CommandProcessor; },
              [&] {
                  OriginalAt<void *(THISCALL *)(void *)>(0x007426a0)(o);
                  OriginalAt<void(THISCALL *)(void *)>(0x007426e0)(o);
              });
    CompareLogs();
}

void Round()
{
    SetWorld();
    if (P(0.15))
        OpConsoleState();
    if (P(0.03))
        OpProcessor();
    const int lines = I(1, 6);
    for (int k = 0; k < lines; ++k) {
        std::string line = RandomLine();
        if (P(0.06)) {
            WriteExecFile();
            line = std::string("exec ") + (P(0.9) ? g_execFile : "nosuchfile.txt");
        }
        RunLine(line);
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, command %s, %s's side\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, g_round, Str(g_line.c_str()).c_str(), g_side ? "the original" : "the port");
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

// The original's import of user32's ShowWindow pointed at the fake.
bool RedirectShowWindow()
{
    const void *real = reinterpret_cast<const void *>(GetProcAddress(LoadLibraryA("user32.dll"), "ShowWindow"));
    const auto *base = reinterpret_cast<const uint8_t *>(0x400000);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int n = 0;
    for (auto *imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); imp->Name; ++imp)
        for (auto *t = reinterpret_cast<IMAGE_THUNK_DATA32 *>(const_cast<uint8_t *>(base) + imp->FirstThunk); t->u1.Function; ++t)
            if (t->u1.Function == static_cast<DWORD>(reinterpret_cast<uintptr_t>(real))) {
                t->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(&FakeShowWindow));
                ++n;
            }
    return n > 0;
}

}   // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_CONS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("cons_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    // The layouts both programs share: the players, the objectives' status and text.
    if (sizeof(S_NetPlayerState) != 0xC4 || offsetof(S_NetPlayerState, name) != 8 || offsetof(S_MpObjective, alliedText) != 0x1C ||
        offsetof(S_MissionObjective, status) != 0x10) {
        std::printf("cons_diff: a shared layout is not the original's\n");
        return 1;
    }
    // The fake IGraph's vtable.
    for (void *&slot : g_graphVtable)
        slot = reinterpret_cast<void *>(&GraphUnexpected);
    g_graphVtable[0x08 / 4] = reinterpret_cast<void *>(&GraphSetFlags);
    g_graphVtable[0x48 / 4] = reinterpret_cast<void *>(&GraphGetHWND);
    g_graphVtable[0xDC / 4] = reinterpret_cast<void *>(&GraphInitMouse);
    g_graph.vtable = g_graphVtable;
    // The port's world: the profile's settings, the app's state, the game's mission.
    reinterpret_cast<C_GameApp *>(g_appStore)->m_state = &g_portState;
    reinterpret_cast<C_game *>(g_gameStore)->m_mission = reinterpret_cast<C_MissionDef *>(g_missionStore);
    // The original's world.
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e7092, reinterpret_cast<const void *>(&std::fopen));
    RedirectOriginal(0x007e9000, reinterpret_cast<const void *>(&std::fscanf));
    RedirectOriginal(0x007e6ecd, reinterpret_cast<const void *>(&std::fclose));
    RedirectOriginal(0x006dcd10, reinterpret_cast<const void *>(OGetGameApp));
    RedirectOriginal(0x006c4e80, reinterpret_cast<const void *>(OPrint));
    RedirectOriginal(0x0072a480, reinterpret_cast<const void *>(OSendGameMessage));
    RedirectOriginal(0x0072b590, reinterpret_cast<const void *>(OPostCommand));
    RedirectOriginal(0x006987d0, reinterpret_cast<const void *>(OGetGame));
    RedirectOriginal(0x00699a80, reinterpret_cast<const void *>(OStartNextRound));
    RedirectOriginal(0x0069bcc0, reinterpret_cast<const void *>(OChangeMap));
    RedirectOriginal(0x007e530e, reinterpret_cast<const void *>(OGetIGraph));
    RedirectOriginal(0x006e5380, reinterpret_cast<const void *>(OTextsGet));
    RedirectOriginal(0x00619720, reinterpret_cast<const void *>(OSetGameType));
    RedirectOriginal(0x00617da0, reinterpret_cast<const void *>(OGetCurrentMaps));
    RedirectOriginal(0x007307e0, reinterpret_cast<const void *>(OIsPlayerAdmin));
    RedirectOriginal(0x0072f6c0, reinterpret_cast<const void *>(OKickPlayer));
    RedirectOriginal(0x0072f5e0, reinterpret_cast<const void *>(OKickPlayerByName));
    RedirectOriginal(0x0072eeb0, reinterpret_cast<const void *>(OFindPlayerByName));
    RedirectOriginal(0x0072dfe0, reinterpret_cast<const void *>(OCountYes));
    RedirectOriginal(0x0072dfb0, reinterpret_cast<const void *>(OVote));
    RedirectOriginal(0x0072dd70, reinterpret_cast<const void *>(OVoteStart));
    RedirectOriginal(0x0072dd50, reinterpret_cast<const void *>(OPostVote));
    RedirectOriginal(0x0072f040, reinterpret_cast<const void *>(OLockPlayers));
    RedirectOriginal(0x0072f100, reinterpret_cast<const void *>(OUnlockPlayers));
    RedirectOriginal(0x006ace20, reinterpret_cast<const void *>(OAddMap));
    RedirectOriginal(0x006acd60, reinterpret_cast<const void *>(ORemoveMap));
    RedirectOriginal(0x006acc10, reinterpret_cast<const void *>(OApplySetupRules));
    RedirectOriginal(0x006acc50, reinterpret_cast<const void *>(OMakeSetup));
    RedirectOriginal(0x006acb60, reinterpret_cast<const void *>(OGetVoiceCodecTextId));
    RedirectOriginal(0x00744cd0, reinterpret_cast<const void *>(OAddLine));
    RedirectOriginal(0x006acd30, reinterpret_cast<const void *>(ODeleteSetup));
    RedirectOriginal(0x00744c10, reinterpret_cast<const void *>(OPopCommand));
    RedirectOriginal(0x00722b60, reinterpret_cast<const void *>(OHost));
    RedirectOriginal(0x007240f0, reinterpret_cast<const void *>(OConnect));
    if (!RedirectShowWindow()) {
        std::printf("cons_diff: the original does not import ShowWindow\n");
        return 1;
    }
    G<void *>(O_SETTINGS_PTR) = g_oSettings;
    OF<void *>(g_oApp, 0x14) = g_oState;
    for (void *&slot : g_oStateVtable)
        slot = reinterpret_cast<void *>(&GraphUnexpected);
    g_oStateVtable[0x24 / 4] = reinterpret_cast<void *>(OSetState);
    g_oStateVtable[0x2C / 4] = reinterpret_cast<void *>(OSetPendingState);
    OF<void *>(g_oState, 0) = g_oStateVtable;
    G<void *>(O_CONSOLE) = g_oConsole;
    OF<void *>(g_oGame, 0xA8) = g_oMission;
    char temp[MAX_PATH];
    GetTempPathA(sizeof temp, temp);
    g_execFile = std::string(temp) + "cons_diff_exec.txt";
    int rounds = std::getenv("HD2_CONS_QUICK") ? 6000 : 30000;
    if (const char *r = std::getenv("HD2_CONS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_CONS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("cons_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
