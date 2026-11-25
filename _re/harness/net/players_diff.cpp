// Net part 7: the network game's players, teams and scores (net/net_players.cpp) against the original executable's:
// the kills scored, the ranks, the kicks, the scores' message, the teams counted and balanced, the respawn rule, the
// server's status, the session parity, the replication's reset, the level's quantisation and the net library's
// quantisation table. MinGW; build_players.bat; runs players_diff.exe ("players_diff: N checks, 0 failures").
//
// Each round makes a random world in both programs' layouts: the net game's players (6 to 12 slots: their bytes alike
// on both sides, the humans and connections each side's own), the team sums, scores and wins, the multiplayer rules
// (one block both read), the role and local slot, the game (or none), the humans (controller type and object,
// spectating, health, team, role, uniform's side), the actors by index, the replication (or none) and its objects, the
// queues. Up to 8 calls follow; every fake logs its call alike (the players' and queue's locks too: the original's
// critical sections by its imports, the port's C_NetLock), and after each call the result, the logs and the whole
// state are compared. HD2_PLAYERS_ROUNDS: rounds (default 30,000; HD2_PLAYERS_QUICK: 8,000); HD2_PLAYERS_SEED;
// HD2_PLAYERS_FIRST_CHANCE.
#include <winsock2.h>
#include <windows.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
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
#include "net/bitstream.h"
#include "net/dplay8.h"
#include "net/net_channel.h"
#include "net/net_game.h"
#include "net/net_object.h"
#include "net/net_transport.h"
#undef private
#undef protected

#include "original_exe.h"

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

static_assert(sizeof(S_NetPlayerState) == 0xC4, "the player state's size");
static_assert(offsetof(S_NetPlayerState, human) == 0x3C && offsetof(S_NetPlayerState, stats) == 0x40 &&
                  offsetof(S_NetPlayerState, team) == 0x64 && offsetof(S_NetPlayerState, link) == 0x6C &&
                  offsetof(S_NetPlayerState, joinStage) == 0x74 && offsetof(S_NetPlayerState, teamKills) == 0x7C &&
                  offsetof(S_NetPlayerState, admin) == 0xA1 && offsetof(S_NetPlayerState, session) == 0xAC &&
                  offsetof(S_NetPlayerState, livesUsed) == 0xB0 && offsetof(S_NetPlayerState, voiceBuffer) == 0xB4,
              "the player state's layout");
static_assert(sizeof(S_NetStats) == 0x24, "the stats' size");
static_assert(sizeof(S_StatusPlayer) == 0x5C, "the status player's size");

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
constexpr int MAX_SLOTS = 12;
constexpr int HUMANS = 16;
constexpr int OBJECTS = 6;
struct S_HumanAnswers {
    int controllerType;
    bool controller;
    bool spectating;
    int health;
    int team;
    int pendingTeam;
    int role;
    int uniform;          // -1 none, else the uniform's side (its def's team)
};
struct S_Script {
    bool game = true;
    S_HumanAnswers humans[HUMANS];
    std::map<int, int> actors;   // actor index -> human (absent: null)
    uint32_t now = 0;            // GetPerfTimeMs
    uint32_t rtt[MAX_SLOTS + 4];
} g_script;
int RttOf(uint32_t connection) { return static_cast<int>((connection * 2654435761u) >> 22); }

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_localPlayerSlot;
C_NetReplication *g_netReplication;
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

C_NetGame &PNet() { return g_netGame; }
S_NetPlayerState::S_NetPlayerState() = default;

void C_NetLock::Enter()
{
    Log(std::string("Enter ") + (this == &g_netGame.m_playersLock ? "players" : this == &g_netGame.m_queueLock ? "queue" : "?"));
}
void C_NetLock::Leave()
{
    Log(std::string("Leave ") + (this == &g_netGame.m_playersLock ? "players" : this == &g_netGame.m_queueLock ? "queue" : "?"));
}
S_NetPlayerState *C_NetGame::LockPlayers()
{
    m_playersLock.Enter();
    return m_players.data();
}
void C_NetGame::UnlockPlayers() { m_playersLock.Leave(); }

alignas(16) unsigned char g_pGame[sizeof(C_game)];
C_game *GetGame() { return g_script.game ? reinterpret_cast<C_game *>(g_pGame) : nullptr; }
void C_game::Print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("Print " + Str(format) + " " + VFormat(format, args));
    va_end(args);
}
void C_game::PrintColored(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintColored " + F(color.x) + " " + F(color.y) + " " + F(color.z) + " " + Str(format) + " " + VFormat(format, args));
    va_end(args);
}
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color.x) + " " + F(color.y) + " " + F(color.z) + " " + VFormat(format, args));
    va_end(args);
}
int C_game::SetRoundTime(int ms)
{
    Log("SetRoundTime " + N(ms));
    return 0;
}

alignas(16) unsigned char g_pApp[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_pApp); }
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(a) + " " + N(b) + " " + N(reliable));
}
void C_GameApp::PrintMessage(const char *text) { Log("AppPrintMessage " + Str(text)); }

C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    char text[32];
    std::snprintf(text, sizeof text, "T%X %%s", id);
    return C_str(text);
}

bool C_GameProfile::IsCooperativeGame() const
{
    return m_mpSettings->sessionType != 0 && m_mpSettings->gameType == MP_GAME_COOPERATIVE;
}

// The humans, their controllers, uniforms (container, item, definition).
alignas(16) unsigned char g_pHumans[HUMANS][sizeof(C_Human)];
alignas(16) unsigned char g_pControllers[HUMANS][sizeof(C_PlayerController)];
alignas(16) unsigned char g_pContainers[HUMANS][0x40];
alignas(16) unsigned char g_pItems[HUMANS][sizeof(C_Item)];
alignas(16) unsigned char g_pDefs[HUMANS][sizeof(C_ItemDef)];
C_ItemContainer *g_pContainerTable[HUMANS][8];
void *g_pControllerVtable[0x100];
void *g_pContainerVtable[0x40];
C_Human *PHuman(int i) { return reinterpret_cast<C_Human *>(g_pHumans[i]); }
int PHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_pHumans[i])
            return i;
    return p ? 99 : -1;
}
int C_Human::GetControllerType() const
{
    const int i = PHumanIndex(this);
    Log("GetControllerType " + N(i));
    return g_script.humans[i].controllerType;
}
int C_Human::GetTeam(bool pending) const
{
    const int i = PHumanIndex(this);
    Log("GetTeam " + N(i) + " " + N(pending));
    return pending ? g_script.humans[i].pendingTeam : g_script.humans[i].team;
}
bool __attribute__((thiscall)) PIsSpectating(void *self)
{
    const int i = static_cast<int>((static_cast<unsigned char *>(self) - g_pControllers[0]) / sizeof(C_PlayerController));
    Log("IsSpectating " + N(i));
    return g_script.humans[i].spectating;
}
void C_PlayerController::SendSoldierClass(int soldierClass)
{
    const int i = static_cast<int>((reinterpret_cast<unsigned char *>(this) - g_pControllers[0]) / sizeof(C_PlayerController));
    Log("SendSoldierClass " + N(i) + " " + N(soldierClass));
}
C_Item *__attribute__((thiscall)) PGetItem(void *self)
{
    const int i = static_cast<int>((static_cast<unsigned char *>(self) - g_pContainers[0]) / 0x40);
    Log("GetItem " + N(i));
    return g_script.humans[i].uniform < 0 ? nullptr : reinterpret_cast<C_Item *>(g_pItems[i]);
}

C_Actor *C_ActorsModule::GetActor(int index) const
{
    Log("GetActor " + N(index));
    const auto it = g_script.actors.find(index);
    return it == g_script.actors.end() ? nullptr : reinterpret_cast<C_Actor *>(PHuman(it->second));
}

// DirectPlay's server (both sides share it: COM, stdcall; GetConnectionInfo is +0x80).
HRESULT __stdcall FakeGetConnectionInfo(void *, DWORD player, void *info, DWORD flags)
{
    Log("GetConnectionInfo " + N(player) + " " + N(static_cast<const DWORD *>(info)[0]) + " " + N(flags));
    static_cast<DWORD *>(info)[1] = static_cast<DWORD>(RttOf(player));
    return 0;
}
void *g_serverVtable[64];
void *g_serverObject = g_serverVtable;

// The connections (each slot's, the client's link to the server) and the transport.
alignas(16) unsigned char g_pLinks[MAX_SLOTS + 1][sizeof(C_NetConnection)];
alignas(16) unsigned char g_pTransport[sizeof(C_NetTransport)];
C_NetConnection *PLink(int i) { return reinterpret_cast<C_NetConnection *>(g_pLinks[i]); }
int PLinkIndex(const void *p)
{
    for (int i = 0; i <= MAX_SLOTS; ++i)
        if (p == g_pLinks[i])
            return i;
    return p ? 99 : -1;
}

// The replication and its objects; the pool; the perf clock.
alignas(16) unsigned char g_pReplication[sizeof(C_NetReplication)];
alignas(16) unsigned char g_pObjects[OBJECTS][0x10];
void *g_pReplicationVtable[0x40];
void __attribute__((thiscall)) PSetRead(void *, S_NetQuantization *q, S_NetQuantization *d, S_NetQuantization *r)
{
    Log("SetReadQuantizations " + N(q == &g_netGame.m_quantizations[0]) + " " + N(d != nullptr) + " " + N(r != nullptr));
}
void __attribute__((thiscall)) PSetWrite(void *, S_NetQuantization *q, S_NetQuantization *d, S_NetQuantization *r)
{
    Log("SetWriteQuantizations " + N(q == &g_netGame.m_quantizations[0]) + " " + N(d != nullptr) + " " + N(r != nullptr));
}
void C_NetObject::ClearMessages(bool release, bool outgoing, bool force)
{
    Log("ClearMessages " + N((reinterpret_cast<unsigned char *>(this) - g_pObjects[0]) / 0x10) + " " + N(release) + " " +
        N(outgoing) + " " + N(force));
}
void C_NetPool::FreeBuffer(void *buffer, int size)
{
    Log("FreeBuffer " + N(reinterpret_cast<uintptr_t>(buffer)) + " " + N(size));
}
int64_t GetPerfTimeMs()
{
    Log("GetPerfTimeMs");
    return g_script.now;
}

namespace {

// ================================================================================================================
// The original's world.
// ================================================================================================================
template <class T> T &OF(void *o, int at) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
template <class T> T &G(uint32_t at) { return *reinterpret_cast<T *>(static_cast<uintptr_t>(at)); }
#define THISCALL __attribute__((thiscall))
#define FAST __attribute__((fastcall))

const uint32_t O_NET = 0x0097d070, O_ROLE = 0x00982100, O_LOCAL_SLOT = 0x00982104, O_PROFILE = 0x008aea0c;
const uint32_t O_TABLE = 0x00982de8, O_FSTR = 0x00405100;
const uint32_t O_ENTER_SLOT = 0x0080f1a8, O_LEAVE_SLOT = 0x0080f1ac;
// The functions compared.
const uint32_t O_KILLED = 0x0072f230, O_KICK_NAME = 0x0072f5e0, O_KICK = 0x0072f6c0, O_STATS = 0x0072f890,
               O_LOCAL_STATS = 0x0072f8b0, O_SET_RANK = 0x0072fe90, O_SEND_SCORE = 0x00730130, O_SEND_SCORES = 0x007301d0,
               O_WRITE_SCORES = 0x0072fc50, O_READ_SCORES = 0x0072fa50, O_LEVEL_BOUNDS = 0x0072f8d0,
               O_TABLE_CTOR = 0x0073cfa0, O_COUNT_TEAM = 0x00730230, O_COUNT_TEAMS = 0x00730320,
               O_COUNT_PLAYERS = 0x00730410, O_APPLY_PENDING = 0x007304a0, O_SET_TEAM = 0x00730510,
               O_BALANCED = 0x00730590, O_TEAM_ALIVE = 0x007305e0, O_BALANCE = 0x007306a0, O_IS_ADMIN = 0x007307e0,
               O_LOCAL_HUMAN = 0x00730830, O_LOCAL_TEAM = 0x00730880, O_SPECTATING = 0x007308a0,
               O_GET_TEAM = 0x00730900, O_RESPAWN = 0x00730970, O_ADD_SCORE = 0x00730a20, O_PRINT_TEAMS = 0x00730a50,
               O_FILL_STATUS = 0x00730be0, O_FLIP = 0x00730e10, O_RESET_REPLICATION = 0x007319e0,
               O_CONNECTION_STATE = 0x00731ac0, O_STREAM_CTOR = 0x0073d290;

volatile uintptr_t g_oNetAddress = O_NET;   // (volatile: g++ sees no object at a constant address)
uint8_t *ONet() { return reinterpret_cast<uint8_t *>(g_oNetAddress); }

alignas(16) uint8_t g_oGame[0x200], g_oApp[0x100];
alignas(16) uint8_t g_oHumans[HUMANS][0x400], g_oControllers[HUMANS][0x100], g_oContainers[HUMANS][0x40],
    g_oItems[HUMANS][0x40], g_oDefs[HUMANS][0x80];
void *g_oContainerTable[HUMANS][8];
void *g_oControllerVtable[0x80], *g_oContainerVtable[0x40];
alignas(16) uint8_t g_oLinks[MAX_SLOTS + 1][0x100];
alignas(16) uint8_t g_oReplication[0x100], g_oObjects[OBJECTS][0x10];
void *g_oReplicationVtable[0x10];
alignas(16) uint8_t g_mp[0x400];   // the multiplayer rules (one block: both programs read the same layout)

int OHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_oHumans[i])
            return i;
    return p ? 99 : -1;
}
int OLinkIndex(const void *p)
{
    for (int i = 0; i <= MAX_SLOTS; ++i)
        if (p == g_oLinks[i])
            return i;
    return p ? 99 : -1;
}

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
void *OAlignedMalloc(size_t n, size_t a) { return _aligned_malloc(n, a); }
void THISCALL ONoLock(void *) {}

// The critical sections: the net game's two logged, any other the real one.
void *g_realEnter, *g_realLeave;
void __stdcall OEnter(void *cs)
{
    if (cs == ONet() + 0x418)
        Log("Enter players");
    else if (cs == ONet() + 0x430)
        Log("Enter queue");
    else
        reinterpret_cast<void(__stdcall *)(void *)>(g_realEnter)(cs);
}
void __stdcall OLeave(void *cs)
{
    if (cs == ONet() + 0x418)
        Log("Leave players");
    else if (cs == ONet() + 0x430)
        Log("Leave queue");
    else
        reinterpret_cast<void(__stdcall *)(void *)>(g_realLeave)(cs);
}

void *OGetGame() { return g_script.game ? g_oGame : nullptr; }
void __cdecl OPrint(void *, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("Print " + Str(format) + " " + VFormat(format, args));
    va_end(args);
}
void __cdecl OPrintColored(void *, const float *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintColored " + F(color[0]) + " " + F(color[1]) + " " + F(color[2]) + " " + Str(format) + " " + VFormat(format, args));
    va_end(args);
}
void __cdecl OPrintMessage(void *, const float *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color[0]) + " " + F(color[1]) + " " + F(color[2]) + " " + VFormat(format, args));
    va_end(args);
}
int THISCALL OSetRoundTime(void *, int ms)
{
    Log("SetRoundTime " + N(ms));
    return 0;
}
void *OGetGameApp() { return g_oApp; }
void THISCALL OSendGameMessage(void *, int type, int to, intptr_t a, int b, bool reliable)
{
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(a) + " " + N(b) + " " + N(reliable));
}
void THISCALL OAppPrintMessage(void *, const char *text) { Log("AppPrintMessage " + Str(text)); }
// The texts: each id's string kept (the game's table holds its own reference), a reference given out.
std::map<int, void *> g_oTexts;
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    void *&rep = g_oTexts[id];
    if (!rep) {
        char format[32];
        std::snprintf(format, sizeof format, "T%X %%%%s", id);
        reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(&rep, format);
    }
    ++OF<int>(rep, 0);
    OF<void *>(ret, 0) = rep;
    return ret;
}
void *THISCALL OGetActor(void *, int index)
{
    Log("GetActor " + N(index));
    const auto it = g_script.actors.find(index);
    return it == g_script.actors.end() ? nullptr : g_oHumans[it->second];
}
int THISCALL OGetControllerType(void *human)
{
    const int i = OHumanIndex(human);
    Log("GetControllerType " + N(i));
    return g_script.humans[i].controllerType;
}
int THISCALL OGetTeam(void *human, bool pending)
{
    const int i = OHumanIndex(human);
    Log("GetTeam " + N(i) + " " + N(pending));
    return pending ? g_script.humans[i].pendingTeam : g_script.humans[i].team;
}
bool THISCALL OIsSpectating(void *self)
{
    const int i = static_cast<int>((static_cast<uint8_t *>(self) - g_oControllers[0]) / 0x100);
    Log("IsSpectating " + N(i));
    return g_script.humans[i].spectating;
}
void THISCALL OSendSoldierClass(void *self, int soldierClass)
{
    const int i = static_cast<int>((static_cast<uint8_t *>(self) - g_oControllers[0]) / 0x100);
    Log("SendSoldierClass " + N(i) + " " + N(soldierClass));
}
void *THISCALL OGetItem(void *self)
{
    const int i = static_cast<int>((static_cast<uint8_t *>(self) - g_oContainers[0]) / 0x40);
    Log("GetItem " + N(i));
    return g_script.humans[i].uniform < 0 ? nullptr : g_oItems[i];
}
void THISCALL OSetRead(void *, void *q, void *d, void *r)
{
    Log("SetReadQuantizations " + N(q == ONet() + 0x20) + " " + N(d != nullptr) + " " + N(r != nullptr));
}
void THISCALL OSetWrite(void *, void *q, void *d, void *r)
{
    Log("SetWriteQuantizations " + N(q == ONet() + 0x20) + " " + N(d != nullptr) + " " + N(r != nullptr));
}
void THISCALL OClearMessages(void *self, bool release, bool outgoing, bool force)
{
    Log("ClearMessages " + N((static_cast<uint8_t *>(self) - g_oObjects[0]) / 0x10) + " " + N(release) + " " + N(outgoing) + " " +
        N(force));
}
void THISCALL OFreeBuffer(void *, void *buffer, int size)
{
    Log("FreeBuffer " + N(reinterpret_cast<uintptr_t>(buffer)) + " " + N(size));
}
long long OGetPerfTimeMs()
{
    Log("GetPerfTimeMs");
    return g_script.now;
}

// ---- checks ----
int checks, failures;
int g_round;
std::string g_line;
void Fail(const std::string &what)
{
    ++failures;
    if (failures <= 40)
        std::printf("round %d, %s: %s\n", g_round, g_line.c_str(), what.c_str());
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
            s += " (call " + N(i) + ")\n     orig: " + x.substr(0, 300) + "\n     port: " + y.substr(0, 300);
            break;
        }
    }
    Check(false, s);
}

// ---- the world ----
int g_slots;
uint8_t *OPlayers() { return OF<uint8_t *>(ONet(), 0x1E0); }

// The original's queues (MSVC6 std::list: +4 the head node, +8 the size; a node: next, prev, the value at +8).
void OListInit(int at)
{
    uint8_t *head = static_cast<uint8_t *>(ONew(0x18));
    OF<void *>(head, 0) = head;
    OF<void *>(head, 4) = head;
    OF<void *>(ONet(), at + 4) = head;
    OF<int>(ONet(), at + 8) = 0;
}
void OListPush(int at, const S_NetQueued &q)
{
    uint8_t *head = OF<uint8_t *>(ONet(), at + 4);
    uint8_t *node = static_cast<uint8_t *>(ONew(0x18));
    uint8_t *last = OF<uint8_t *>(head, 4);
    OF<void *>(node, 0) = head;
    OF<void *>(node, 4) = last;
    std::memcpy(node + 8, &q, sizeof q);
    OF<void *>(last, 0) = node;
    OF<void *>(head, 4) = node;
    ++OF<int>(ONet(), at + 8);
}
void OListClear(int at)
{
    uint8_t *head = OF<uint8_t *>(ONet(), at + 4);
    for (uint8_t *n = OF<uint8_t *>(head, 0); n != head;) {
        uint8_t *next = OF<uint8_t *>(n, 0);
        ODelete(n);
        n = next;
    }
    OF<void *>(head, 0) = head;
    OF<void *>(head, 4) = head;
    OF<int>(ONet(), at + 8) = 0;
}

void SetWorld()
{
    C_NetGame &net = PNet();
    static const uint32_t roles[] = {7, 1, 2, 3, 0, 5, 1, 3};
    g_gameRole = roles[I(0, 7)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    g_slots = I(6, MAX_SLOTS);
    g_localPlayerSlot = P(0.9) ? I(0, g_slots - 1) : I(-1, g_slots + 1);
    G<int>(O_LOCAL_SLOT) = g_localPlayerSlot;
    g_script.game = P(0.9);
    g_script.now = static_cast<uint32_t>(I(0, 100000));

    // The rules.
    std::memset(g_mp, 0, sizeof g_mp);
    S_MultiplayerSettings &mp = *reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    mp.sessionType = P(0.85) ? I(1, 2) : 0;
    mp.gameType = I(0, 3);
    mp.teamLives = P(0.5);
    mp.coopLives = P(0.2) ? -1 : I(0, 6);
    mp.allowRespawn = I(0, 1);
    mp.maxClients = I(0, 32);
    mp.pointLimit = I(0, 100);
    mp.roundLimit = I(0, 30);
    mp.roundCount = I(0, 5);
    mp.warmup = I(0, 60);
    mp.respawnTime = I(0, 30);
    mp.friendlyFire = I(0, 1);
    mp.autoTeamBalance = I(0, 1);
    mp.thirdPersonView = I(0, 1);
    mp.spawnProtection = I(0, 10);
    mp.inverseDamage = I(0, 100);
    mp.fallingDamage = I(0, 1);
    mp.unknown25C = I(-5, 5);
    mp.allowVehicles = I(0, 1);
    mp.coopDifficulty = I(1, 4);
    mp.allowCrosshair = I(0, 1);
    mp.spawnOnStart = I(0, 1);

    // The game.
    C_game *game = reinterpret_cast<C_game *>(g_pGame);
    game->m_missionTime = static_cast<uint32_t>(P(0.2) ? 0 : I(0, 1000000));
    game->m_time = static_cast<uint32_t>(I(0, 1000000));
    game->m_roundTime = P(0.2) ? 0 : I(0, 1000000);
    game->m_teamLivesUsed = I(0, 9);
    OF<uint32_t>(g_oGame, 0x28) = game->m_missionTime;
    OF<uint32_t>(g_oGame, 0x2C) = game->m_time;
    OF<int>(g_oGame, 0x30) = game->m_roundTime;
    OF<int>(g_oGame, 0x84) = game->m_teamLivesUsed;

    // The humans.
    for (int i = 0; i < HUMANS; ++i) {
        S_HumanAnswers &h = g_script.humans[i];
        const int kind = I(0, 9);
        h.controllerType = kind < 6 ? 1 : kind < 8 ? 7 : I(0, 9);
        h.controller = P(0.9);
        h.spectating = P(0.3);
        h.health = P(0.2) ? I(-5, 0) : I(1, 100);
        h.team = I(0, 2);
        h.pendingTeam = I(0, 2);
        h.role = I(0, 6);
        h.uniform = P(0.2) ? -1 : I(0, 3);
        C_Human *ph = PHuman(i);
        ph->m_role = static_cast<E_HumanRole>(h.role);
        ph->m_stats[STAT_HEALTH] = h.health;
        ph->m_controller = h.controller ? reinterpret_cast<C_HumanController *>(g_pControllers[i]) : nullptr;
        ph->m_containers = g_pContainerTable[i];
        reinterpret_cast<C_Item *>(g_pItems[i])->m_def = reinterpret_cast<C_ItemDef *>(g_pDefs[i]);
        reinterpret_cast<C_ItemDef *>(g_pDefs[i])->m_team = h.uniform;
        OF<int>(g_oHumans[i], 0x19C) = h.role;
        OF<int>(g_oHumans[i], 0xEC) = h.health;
        OF<void *>(g_oHumans[i], 0xB0) = h.controller ? g_oControllers[i] : nullptr;
        OF<void *>(g_oHumans[i], 0x74) = g_oContainerTable[i];
        OF<void *>(g_oItems[i], 4) = g_oDefs[i];
        OF<int>(g_oDefs[i], 0x44) = h.uniform;
    }
    g_script.actors.clear();
    for (int a = -1; a < MAX_SLOTS + 3; ++a)
        if (P(0.7))
            g_script.actors[a] = I(0, HUMANS - 1);

    // The players: random bytes, then the fields that matter set.
    net.m_players.assign(static_cast<size_t>(g_slots), S_NetPlayerState());
    std::free(OPlayers());
    uint8_t *op = static_cast<uint8_t *>(std::calloc(static_cast<size_t>(g_slots), 0xC4));
    OF<uint8_t *>(ONet(), 0x1E0) = op;
    OF<uint8_t *>(ONet(), 0x1E4) = op + g_slots * 0xC4;
    OF<uint8_t *>(ONet(), 0x1E8) = op + g_slots * 0xC4;
    for (int s = 0; s < g_slots; ++s) {
        S_NetPlayerState &p = net.m_players[static_cast<size_t>(s)];
        uint8_t *raw = reinterpret_cast<uint8_t *>(&p);
        for (int k = 0; k < 0xC4; ++k)
            raw[k] = static_cast<uint8_t>(I(0, 255));
        p.connection = P(0.2) ? 0 : I(1, 5000);
        p.address = static_cast<uint32_t>(I(0, 0x7FFFFFFF));
        std::memset(p.name, 0, sizeof p.name);
        if (P(0.8)) {
            const int k = P(0.15) ? I(0, 3) : s;
            std::snprintf(p.name, sizeof p.name, "P%d", k);
            if (P(0.05))
                std::memset(p.name, 'n', sizeof p.name - 1);
            if (P(0.05))
                p.name[1] = 0;   // (one letter)
        } else if (P(0.5)) {
            // A freed slot: only its first character cleared (ClearSlot), the rest left.
            std::snprintf(p.name, sizeof p.name, "xP%d", s);
            p.name[0] = 0;
        }
        const int h = P(0.8) ? I(0, HUMANS - 1) : -1;
        p.human = h < 0 ? nullptr : PHuman(h);
        int *v = p.stats.values;
        for (int k = 0; k < 9; ++k)
            v[k] = I(-20, 90);
        if (P(0.1)) {
            // (The scores message's 11-bit sign: 1024 reads back as 1024, 1025 as -1023.)
            static const int edges[] = {1023, 1024, 1025, -1023, -1024, -1025, 0, -1};
            v[NET_STAT_SCORE] = edges[I(0, 7)];
        }
        if (P(0.1))
            v[NET_STAT_PING] = P(0.5) ? I(0x3F0, 0x500) : I(-5, -1);
        v[NET_STAT_RANK] = I(0, 7);
        p.team = I(0, 2);
        p.pendingTeam = I(0, 2);
        p.link = P(0.9) ? PLink(s) : nullptr;
        p.joinStage = I(0, 3);
        p.teamKills = I(0, 3);
        p.admin = P(0.3);
        p.left = P(0.2);
        p.session = static_cast<uint32_t>(P(0.8) ? I(0, 1) : I(0, 1000));
        p.livesUsed = P(0.1) ? I(-3, 200) : P(0.05) ? I(126, 128) : I(-1, 6);
        p.voiceBuffer = nullptr;
        uint8_t *o = op + s * 0xC4;
        std::memcpy(o, raw, 0xC4);
        OF<void *>(o, 0x3C) = h < 0 ? nullptr : g_oHumans[h];
        OF<void *>(o, 0x6C) = p.link ? g_oLinks[s] : nullptr;
    }
    // The connections' session copies.
    for (int l = 0; l <= MAX_SLOTS; ++l) {
        const uint32_t session = static_cast<uint32_t>(I(0, 3));
        PLink(l)->m_session = session;
        OF<uint32_t>(g_oLinks[l], 8) = session;
    }
    net.m_serverLink = PLink(MAX_SLOTS);
    OF<void *>(ONet(), 0x334) = g_oLinks[MAX_SLOTS];

    // The teams.
    for (int t = 0; t < 3; ++t) {
        const float score = P(0.5) ? static_cast<float>(I(-50, 500)) : static_cast<float>(I(-5000, 50000)) / 64.0f;
        net.m_teamScores[t] = score;
        OF<float>(ONet(), 0x44C + 4 * t) = score;
        net.m_teamWins[t] = I(0, 300);
        OF<int>(ONet(), 0x458 + 4 * t) = net.m_teamWins[t];
        for (int k = 0; k < 9; ++k) {
            net.m_teamStats[t].values[k] = I(-100, 900);
            OF<int>(ONet(), 0x464 + 0x24 * t + 4 * k) = net.m_teamStats[t].values[k];
        }
    }

    // The server, the transport, the join status.
    net.m_server = reinterpret_cast<IDirectPlay8Server *>(&g_serverObject);
    OF<void *>(ONet(), 0) = &g_serverObject;
    const int last = static_cast<int>(g_script.now) - (P(0.1) ? I(599, 602) : P(0.5) ? I(0, 600) : I(601, 5000));
    reinterpret_cast<C_NetTransport *>(g_pTransport)->m_lastReceive = last;
    OF<int>(ONet(), 0x40C) = last;
    net.m_joinStatus = I(0, 9);
    OF<int>(ONet(), 0x5044) = net.m_joinStatus;

    // The replication (or none) and its objects; the queues.
    const bool replication = P(0.85);
    auto *pr = reinterpret_cast<C_NetReplication *>(g_pReplication);
    g_netReplication = replication ? pr : nullptr;
    OF<void *>(ONet(), 0x14) = replication ? g_oReplication : nullptr;
    const int objects = I(0, OBJECTS);
    pr->m_objects.clear();
    for (int k = 0; k < objects; ++k)
        pr->m_objects.push_back(reinterpret_cast<C_NetObject *>(g_pObjects[k]));
    std::free(OF<void *>(g_oReplication, 0x20));
    void **ov = static_cast<void **>(std::calloc(OBJECTS + 1, sizeof(void *)));
    for (int k = 0; k < objects; ++k)
        ov[k] = g_oObjects[k];
    OF<void **>(g_oReplication, 0x20) = ov;
    OF<void **>(g_oReplication, 0x24) = ov + objects;
    OF<void **>(g_oReplication, 0x28) = ov + objects;
    pr->m_enabled = false;
    OF<uint8_t>(g_oReplication, 0xD4) = 0;
    net.m_received.clear();
    net.m_outgoing.clear();
    OListClear(0x1F0);
    OListClear(0x1FC);
    for (int list = 0; list < 2; ++list)
        for (int k = I(0, 4); k > 0; --k) {
            const S_NetQueued q = {I(0, 1000), static_cast<uint32_t>(I(0, 0x7F)), I(0, 1200),
                                   reinterpret_cast<void *>(static_cast<uintptr_t>(I(1, 100000)))};
            (list ? net.m_outgoing : net.m_received).push_back(q);
            OListPush(list ? 0x1FC : 0x1F0, q);
        }
}

// The whole state compared: the players (their bytes, the humans and connections by index), the connections'
// sessions, the teams, the game's times, the queues' sizes, the replication's flag.
void CompareState()
{
    C_NetGame &net = PNet();
    std::string s;
    const uint8_t *op = OPlayers();
    if (OF<uint8_t *>(ONet(), 0x1E4) != op + g_slots * 0xC4 || net.m_players.size() != static_cast<size_t>(g_slots))
        s += "the players' count; ";
    for (int i = 0; i < g_slots && s.empty(); ++i) {
        const uint8_t *a = reinterpret_cast<const uint8_t *>(&net.m_players[static_cast<size_t>(i)]);
        const uint8_t *b = op + i * 0xC4;
        for (int k = 0; k < 0xC4; ++k) {
            if ((k >= 0x3C && k < 0x40) || (k >= 0x6C && k < 0x70))
                continue;
            if (a[k] != b[k]) {
                s += "slot " + N(i) + " byte +" + N(k) + " " + N(b[k]) + " / " + N(a[k]) + "; ";
                break;
            }
        }
        if (PHumanIndex(net.m_players[static_cast<size_t>(i)].human) != OHumanIndex(OF<void *>(const_cast<uint8_t *>(b), 0x3C)))
            s += "slot " + N(i) + " human; ";
        if (PLinkIndex(net.m_players[static_cast<size_t>(i)].link) != OLinkIndex(OF<void *>(const_cast<uint8_t *>(b), 0x6C)))
            s += "slot " + N(i) + " link; ";
    }
    for (int l = 0; l <= MAX_SLOTS; ++l)
        if (PLink(l)->m_session != OF<uint32_t>(g_oLinks[l], 8))
            s += "link " + N(l) + " session " + N(OF<uint32_t>(g_oLinks[l], 8)) + " / " + N(PLink(l)->m_session) + "; ";
    for (int t = 0; t < 3; ++t) {
        if (std::memcmp(&net.m_teamScores[t], ONet() + 0x44C + 4 * t, 4) != 0)
            s += "team score " + N(t) + " " + F(OF<float>(ONet(), 0x44C + 4 * t)) + " / " + F(net.m_teamScores[t]) + "; ";
        if (net.m_teamWins[t] != OF<int>(ONet(), 0x458 + 4 * t))
            s += "team wins " + N(t) + "; ";
        for (int k = 0; k < 9; ++k)
            if (net.m_teamStats[t].values[k] != OF<int>(ONet(), 0x464 + 0x24 * t + 4 * k))
                s += "team stats " + N(t) + "." + N(k) + " " + N(OF<int>(ONet(), 0x464 + 0x24 * t + 4 * k)) + " / " +
                     N(net.m_teamStats[t].values[k]) + "; ";
    }
    const C_game *game = reinterpret_cast<const C_game *>(g_pGame);
    if (game->m_missionTime != OF<uint32_t>(g_oGame, 0x28) || game->m_time != OF<uint32_t>(g_oGame, 0x2C) ||
        game->m_roundTime != OF<int>(g_oGame, 0x30) || game->m_teamLivesUsed != OF<int>(g_oGame, 0x84))
        s += "the game's times; ";
    if (net.m_received.size() != static_cast<size_t>(OF<int>(ONet(), 0x1F8)) ||
        net.m_outgoing.size() != static_cast<size_t>(OF<int>(ONet(), 0x204)))
        s += "the queues; ";
    if (reinterpret_cast<C_NetReplication *>(g_pReplication)->m_enabled != (OF<uint8_t>(g_oReplication, 0xD4) != 0))
        s += "the replication's flag; ";
    Check(s.empty(), s);
}

// The quantisations: the net game's first (+0x20) and the table's first four (0x80 bytes each).
void CompareQuantizations(const char *when)
{
    std::string s;
    // (Only the defined words: x, y, z of the five vectors, the bits, the scales, `unknown`. The original's w
    // components and the padding words hold its stack's leftovers: the callers' arrays have three floats, and the
    // table's entries are copies of a stack temporary.)
    const auto differs = [](const void *x, const void *y) {
        const auto *a = static_cast<const uint32_t *>(x), *b = static_cast<const uint32_t *>(y);
        for (int w = 0; w < 29; ++w)
            if ((w & 3) != 3 && a[w] != b[w])
                return w;
        return a[28] != b[28] ? 28 : -1;
    };
    const int level = differs(&PNet().m_quantizations[0], ONet() + 0x20);
    if (level >= 0)
        s += "the level's quantisation (word " + N(level) + "); ";
    const uint8_t *table = OF<uint8_t *>(reinterpret_cast<void *>(static_cast<uintptr_t>(O_TABLE)), 4);
    if (OF<int>(reinterpret_cast<void *>(static_cast<uintptr_t>(O_TABLE)), 8) != static_cast<int>(g_netQuantizations.m_entries.size()))
        s += "the table's size; ";
    // (The net messages find the table through these: the original's are the table's own members.)
    if (g_netPlayerTable != reinterpret_cast<S_NetPlayer *>(g_netQuantizations.m_entries.data()) ||
        g_netPlayerCount != OF<int>(reinterpret_cast<void *>(static_cast<uintptr_t>(O_TABLE)), 8))
        s += "the table's globals; ";
    for (int k = 0; k < 4; ++k) {
        const int w = differs(&g_netQuantizations.m_entries[static_cast<size_t>(k)], table + 0x80 * k);
        if (w >= 0)
            s += "table entry " + N(k) + " (word " + N(w) + "); ";
    }
    Check(s.empty(), std::string(when) + ": " + s);
}

// ---- the calls ----
template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_log[0].clear();
    g_log[1].clear();
    g_faking = true;
    g_side = 0;
    const long long x = port();
    g_side = 1;
    const long long y = orig();
    g_side = 0;
    g_faking = false;
    Check(x == y, "the result " + N(y) + " / " + N(x));
    CompareLogs();
    CompareState();
}

int Slot() { return I(0, g_slots - 1); }
int AnySlot() { return P(0.8) ? Slot() : I(-2, g_slots + 2); }
void *ON() { return ONet(); }

// The scores' message: written by both, the bits compared; or read by both from the same bits.
constexpr int STREAM_WORDS = 256;
void RunWriteScores()
{
    uint32_t a[STREAM_WORDS] = {}, b[STREAM_WORDS] = {};
    C_BitStream ps(a, STREAM_WORDS * 32);
    alignas(16) uint8_t os[0x40] = {};
    reinterpret_cast<void(THISCALL *)(void *, void *, uint32_t)>(O_STREAM_CTOR)(os, b, STREAM_WORDS * 32);
    uint32_t pos[2] = {};
    Run("WriteScores",
        [&] {
            PNet().WriteScores(&ps);
            pos[0] = ps.BitPosition();
            return 0;
        },
        [&] {
            reinterpret_cast<void(THISCALL *)(void *, void *)>(O_WRITE_SCORES)(ON(), os);
            pos[1] = OF<uint32_t>(os, 8) * 32 + OF<uint32_t>(os, 0xC);
            return 0;
        });
    Check(pos[0] == pos[1] && std::memcmp(a, b, sizeof a) == 0, "the scores' bits (" + N(pos[1]) + " / " + N(pos[0]) + ")");
}
void RunReadScores()
{
    // The bits: a message the port writes in another world's role (a server's), changed a little now and then.
    uint32_t a[STREAM_WORDS] = {}, b[STREAM_WORDS] = {};
    {
        // (WriteScores sets the port's pings, the players' and the teams' (clamped): they are put back.)
        const std::vector<S_NetPlayerState> players = PNet().m_players;
        S_NetStats teams[3];
        std::memcpy(teams, PNet().m_teamStats, sizeof teams);
        const uint32_t role = g_gameRole;
        g_gameRole = 1;
        C_BitStream w(a, STREAM_WORDS * 32);
        PNet().WriteScores(&w);
        g_gameRole = role;
        PNet().m_players = players;
        std::memcpy(PNet().m_teamStats, teams, sizeof teams);
        if (P(0.3))
            for (int k = I(1, 3); k > 0; --k)
                a[I(0, static_cast<int>(w.BitPosition() / 32))] ^= 1u << I(0, 31);
        if (P(0.2)) {
            // Slots past the players, and their stats.
            C_BitStream x(a, STREAM_WORDS * 32);
            for (int k = I(0, 4); k > 0; --k) {
                x.WriteBits(static_cast<uint32_t>(I(0, 0x20)), 6);
                S_NetStats st;
                for (int &v : st.values)
                    v = I(-1100, 1100);
                st.Write(&x);
            }
            x.WriteBits(0x21, 6);
            for (int k = 0; k < 64; ++k)
                x.WriteBits(static_cast<uint32_t>(I(0, 0x7FFFFFFF)), 31);
        }
    }
    std::memcpy(b, a, sizeof a);
    C_BitStream ps(a, STREAM_WORDS * 32);
    alignas(16) uint8_t os[0x40] = {};
    reinterpret_cast<void(THISCALL *)(void *, void *, uint32_t)>(O_STREAM_CTOR)(os, b, STREAM_WORDS * 32);
    uint32_t pos[2] = {};
    // (A random slot list may not end before the bits do: such a message is not given.)
    {
        C_BitStream t(a, STREAM_WORDS * 32);
        int n = 0;
        while (t.ReadBits(6) != 0x21) {
            S_NetStats st;
            st.Read(&t);
            if (++n > 40)
                return;
        }
    }
    Run("ReadScores",
        [&] {
            PNet().ReadScores(&ps);
            pos[0] = ps.BitPosition();
            return 0;
        },
        [&] {
            reinterpret_cast<void(THISCALL *)(void *, void *)>(O_READ_SCORES)(ON(), os);
            pos[1] = OF<uint32_t>(os, 8) * 32 + OF<uint32_t>(os, 0xC);
            return 0;
        });
    Check(pos[0] == pos[1], "the bits read " + N(pos[1]) + " / " + N(pos[0]));
}

void RunFillStatus()
{
    S_ServerStatus ps = {};
    alignas(16) uint8_t os[0x80] = {};
    if (P(0.3)) {
        // A status already holding players (cleared first).
        S_StatusPlayer e = {};
        ps.players.push_back(e);
    }
    Run("FillServerStatus",
        [&] {
            PNet().FillServerStatus(&ps);
            return 0;
        },
        [&] {
            reinterpret_cast<void(THISCALL *)(void *, void *)>(O_FILL_STATUS)(ON(), os);
            return 0;
        });
    std::string s;
    const uint8_t *first = OF<uint8_t *>(os, 4), *last = OF<uint8_t *>(os, 8);
    const size_t count = first ? static_cast<size_t>(last - first) / 0x5C : 0;
    if (count != ps.players.size())
        s += "the players " + N(static_cast<long long>(count)) + " / " + N(static_cast<long long>(ps.players.size())) + "; ";
    for (size_t i = 0; i < count && i < ps.players.size(); ++i) {
        const uint8_t *a = reinterpret_cast<const uint8_t *>(&ps.players[i]), *b = first + 0x5C * i;
        if (std::memcmp(a, b, 0x28) != 0 || std::strcmp(reinterpret_cast<const char *>(a + 0x28), reinterpret_cast<const char *>(b + 0x28)) != 0)
            s += "player " + N(static_cast<long long>(i)) + "; ";
    }
    const uint8_t *pf = reinterpret_cast<const uint8_t *>(&ps.maxClients);
    if (std::memcmp(pf, os + 0x10, 0x68 - 0x10) != 0)
        s += "the rules; ";
    Check(s.empty(), s);
    std::free(OF<void *>(os, 4));
}

void Call()
{
    C_NetGame &net = PNet();
    switch (I(0, 33)) {
    case 0:
    case 1:
    case 2: {
        int victim = P(0.7) ? Slot() : I(-1, g_slots + 2);
        int killer = P(0.15) ? -1 : (P(0.2) ? victim : (P(0.9) ? Slot() : I(-1, g_slots + 1)));
        if (P(0.05))
            victim = I(100, 1000);
        // (Not a player killed in co-op by the slot one past the end: the original reads past the players, the
        // port's Fix stops it. See the notes.)
        const bool notPlayer = victim == -1 || victim >= g_slots;
        if (notPlayer && killer == g_slots)
            killer = Slot();
        Run("OnPlayerKilled " + N(victim) + " " + N(killer), [&] { net.OnPlayerKilled(victim, killer); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, int)>(O_KILLED)(ON(), victim, killer); return 0; });
        break;
    }
    case 3: {
        char name[0x40];
        if (P(0.8))
            std::strcpy(name, net.m_players[static_cast<size_t>(Slot())].name);
        else
            std::snprintf(name, sizeof name, "P%d", I(0, 20));
        const int reason = I(-1, 4);
        Run("KickPlayerByName " + Str(name) + " " + N(reason), [&] { net.KickPlayerByName(name, reason); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, const char *, int)>(O_KICK_NAME)(ON(), name, reason); return 0; });
        break;
    }
    case 4: {
        const int slot = Slot(), reason = I(-1, 4);
        Run("KickPlayer " + N(slot) + " " + N(reason), [&] { net.KickPlayer(slot, reason); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, int)>(O_KICK)(ON(), slot, reason); return 0; });
        break;
    }
    case 5: {
        const int slot = Slot();
        Run("GetPlayerStats " + N(slot),
            [&] { return reinterpret_cast<uint8_t *>(net.GetPlayerStats(slot)) - reinterpret_cast<uint8_t *>(net.m_players.data()); },
            [&] { return reinterpret_cast<uint8_t *>(reinterpret_cast<int *(THISCALL *)(void *, int)>(O_STATS)(ON(), slot)) - OPlayers(); });
        if (g_localPlayerSlot >= 0 && g_localPlayerSlot < g_slots)
            Run("GetLocalPlayerStats",
                [&] { return reinterpret_cast<uint8_t *>(net.GetLocalPlayerStats()) - reinterpret_cast<uint8_t *>(net.m_players.data()); },
                [&] { return reinterpret_cast<uint8_t *>(reinterpret_cast<int *(FAST *)(void *)>(O_LOCAL_STATS)(ON())) - OPlayers(); });
        break;
    }
    case 6:
    case 7: {
        if (!g_script.game)
            return;
        const int slot = P(0.3) && g_localPlayerSlot >= 0 && g_localPlayerSlot < g_slots ? g_localPlayerSlot : Slot();
        const int rank = I(0, 9);
        Run("SetPlayerRank " + N(rank) + " " + N(slot), [&] { net.SetPlayerRank(rank, slot); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, int)>(O_SET_RANK)(ON(), rank, slot); return 0; });
        break;
    }
    case 8:
    case 9: {
        const int slot = Slot();
        const bool lock = P(0.5);
        Run("SendPlayerScore " + N(slot) + " " + N(lock), [&] { net.SendPlayerScore(slot, lock); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, bool)>(O_SEND_SCORE)(ON(), slot, lock); return 0; });
        break;
    }
    case 10:
        Run("SendScores", [&] { net.SendScores(); return 0; },
            [&] { reinterpret_cast<void(FAST *)(void *)>(O_SEND_SCORES)(ON()); return 0; });
        break;
    case 11:
    case 12:
        RunWriteScores();
        break;
    case 13:
    case 14:
        RunReadScores();
        break;
    case 15: {
        float minv[4], size[4];
        for (int k = 0; k < 4; ++k) {
            minv[k] = static_cast<float>(I(-300000, 0)) / 100.0f;
            size[k] = static_cast<float>(I(1, 600000)) / 100.0f;
        }
        Run("SetLevelBounds", [&] { net.SetLevelBounds(minv, size); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, const float *, const float *)>(O_LEVEL_BOUNDS)(ON(), minv, size); return 0; });
        CompareQuantizations("SetLevelBounds");
        break;
    }
    case 16: {
        int a[4] = {-7, -7, -7, -7};
        Run("CountTeamPlayers",
            [&] { net.CountTeamPlayers(&a[0], &a[1]); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int *, int *)>(O_COUNT_TEAM)(ON(), &a[2], &a[3]); return 0; });
        Check(a[0] == a[2] && a[1] == a[3], "the counts");
        break;
    }
    case 17: {
        int a[4] = {-7, -7, -7, -7};
        const bool pending = P(0.5), inGame = P(0.5);
        Run("CountTeams " + N(pending) + " " + N(inGame),
            [&] { net.CountTeams(&a[0], &a[1], pending, inGame); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int *, int *, bool, bool)>(O_COUNT_TEAMS)(ON(), &a[2], &a[3], pending, inGame); return 0; });
        Check(a[0] == a[2] && a[1] == a[3], "the counts");
        break;
    }
    case 18:
        Run("CountPlayers", [&] { return net.CountPlayers(); },
            [&] { return reinterpret_cast<int(FAST *)(void *)>(O_COUNT_PLAYERS)(ON()); });
        break;
    case 19:
        Run("ApplyPendingTeams", [&] { net.ApplyPendingTeams(); return 0; },
            [&] { reinterpret_cast<void(FAST *)(void *)>(O_APPLY_PENDING)(ON()); return 0; });
        break;
    case 20: {
        const int slot = AnySlot(), team = I(0, 2);
        const bool pending = P(0.5);
        Run("SetPlayerTeam " + N(slot) + " " + N(team) + " " + N(pending), [&] { net.SetPlayerTeam(slot, team, pending); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, int, bool)>(O_SET_TEAM)(ON(), slot, team, pending); return 0; });
        break;
    }
    case 21: {
        const int axis = I(0, 6), allied = P(0.1) ? 0 : I(1, 6);
        const bool pending = P(0.5);
        Run("TeamsBalanced " + N(axis) + " " + N(allied) + " " + N(pending), [&] { return net.TeamsBalanced(axis, allied, pending); },
            [&] { return reinterpret_cast<bool(THISCALL *)(void *, int, int, bool)>(O_BALANCED)(ON(), axis, allied, pending); });
        break;
    }
    case 22: {
        const int team = I(0, 2);
        Run("IsTeamAlive " + N(team), [&] { return net.IsTeamAlive(team); },
            [&] { return reinterpret_cast<bool(THISCALL *)(void *, int)>(O_TEAM_ALIVE)(ON(), team); });
        break;
    }
    case 23: {
        const int percent = P(0.7) ? I(0, 100) : I(-20, 150);
        Run("BalanceTeams " + N(percent), [&] { net.BalanceTeams(0, percent); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, int)>(O_BALANCE)(ON(), 0, percent); return 0; });
        break;
    }
    case 24: {
        const int slot = Slot();
        Run("IsPlayerAdmin " + N(slot), [&] { return IsPlayerAdmin(slot); },
            [&] { return reinterpret_cast<bool(__cdecl *)(int)>(O_IS_ADMIN)(slot); });
        break;
    }
    case 25: {
        Run("GetLocalHuman", [&] { return PHumanIndex(net.GetLocalHuman()); },
            [&] { return OHumanIndex(reinterpret_cast<void *(FAST *)(void *)>(O_LOCAL_HUMAN)(ON())); });
        if (g_localPlayerSlot >= 0 && g_localPlayerSlot < g_slots)
            Run("GetLocalTeam", [&] { return net.GetLocalTeam(); },
                [&] { return reinterpret_cast<int(FAST *)(void *)>(O_LOCAL_TEAM)(ON()); });
        break;
    }
    case 26: {
        const int slot = AnySlot();
        // (A player's or remote human always has its controller: both programs call it unchecked.)
        bool callable = true;
        if (slot >= 0 && slot < g_slots) {
            const int h = PHumanIndex(net.m_players[static_cast<size_t>(slot)].human);
            if (h >= 0 && !g_script.humans[h].controller &&
                (g_script.humans[h].controllerType == 1 || g_script.humans[h].controllerType == 7))
                callable = false;
        }
        if (callable)
            Run("IsPlayerSpectating " + N(slot), [&] { return net.IsPlayerSpectating(slot); },
                [&] { return reinterpret_cast<bool(THISCALL *)(void *, int)>(O_SPECTATING)(ON(), slot); });
        const bool pending = P(0.5);
        Run("GetPlayerTeam " + N(slot) + " " + N(pending), [&] { return net.GetPlayerTeam(slot, pending); },
            [&] { return reinterpret_cast<int(THISCALL *)(void *, int, bool)>(O_GET_TEAM)(ON(), slot, pending); });
        break;
    }
    case 27: {
        if (!g_script.game)
            return;
        const int slot = Slot();
        Run("CanPlayerRespawn " + N(slot), [&] { return CanPlayerRespawn(slot); },
            [&] { return reinterpret_cast<bool(__cdecl *)(int)>(O_RESPAWN)(slot); });
        break;
    }
    case 28: {
        const int team = I(0, 2);
        const float points = P(0.5) ? static_cast<float>(I(-10, 10)) : static_cast<float>(I(-1000, 1000)) / 7.0f;
        Run("AddTeamScore " + N(team) + " " + F(points), [&] { net.AddTeamScore(team, points); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, float)>(O_ADD_SCORE)(ON(), team, points); return 0; });
        break;
    }
    case 29:
        if (!g_script.game)
            return;
        Run("PrintTeams", [&] { net.PrintTeams(); return 0; },
            [&] { reinterpret_cast<void(FAST *)(void *)>(O_PRINT_TEAMS)(ON()); return 0; });
        break;
    case 30:
        RunFillStatus();
        break;
    case 31:
        if (!(g_gameRole & GAME_ROLE_SERVER) && (g_localPlayerSlot < 0 || g_localPlayerSlot >= g_slots))
            return;
        // (The server's players in join stage 2 have a connection.)
        for (int i = 0; i < g_slots; ++i)
            if (!net.m_players[static_cast<size_t>(i)].link && net.m_players[static_cast<size_t>(i)].joinStage == 2) {
                net.m_players[static_cast<size_t>(i)].joinStage = 1;
                OPlayers()[i * 0xC4 + 0x74] = 1;
            }
        Run("FlipSessionParity", [&] { net.FlipSessionParity(); return 0; },
            [&] { reinterpret_cast<void(FAST *)(void *)>(O_FLIP)(ON()); return 0; });
        break;
    case 32: {
        const bool all = P(0.5);
        if (all && !g_netReplication)
            return;
        Run("ResetReplication " + N(all), [&] { net.ResetReplication(all); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, bool)>(O_RESET_REPLICATION)(ON(), all); return 0; });
        break;
    }
    default:
        Run("GetConnectionState", [&] { return net.GetConnectionState(); },
            [&] { return reinterpret_cast<int(FAST *)(void *)>(O_CONNECTION_STATE)(ON()); });
        break;
    }
}

void Round()
{
    SetWorld();
    for (int k = I(1, 8); k > 0; --k)
        Call();
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p in round %d, %s, %s's side\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                g_round, g_line.c_str(), g_side ? "the original" : "the port");
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

// A virtual function's vtable slot in the port (g++: a member pointer to a virtual holds its offset + 1).
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
    if (std::getenv("HD2_PLAYERS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("players_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);

    // The port's objects: the net game's containers made (its storage is raw), the vtables' fakes.
    C_NetGame &net = PNet();
    new (&net.m_players) std::vector<S_NetPlayerState>();
    new (&net.m_received) std::list<S_NetQueued>();
    new (&net.m_outgoing) std::list<S_NetQueued>();
    net.m_transport = reinterpret_cast<C_NetTransport *>(g_pTransport);
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    auto *pr = reinterpret_cast<C_NetReplication *>(g_pReplication);
    new (&pr->m_objects) std::vector<C_NetObject *>();
    const int spect = VSlot(&C_HumanController::IsSpectating), item = VSlot(&C_ItemContainer::GetItem),
              read = VSlot(&C_NetReplication::SetReadQuantizations), write = VSlot(&C_NetReplication::SetWriteQuantizations);
    if (spect < 0 || item < 0 || read < 0 || write < 0) {
        std::printf("players_diff: a virtual's slot not found\n");
        return 1;
    }
    g_pControllerVtable[spect] = reinterpret_cast<void *>(&PIsSpectating);
    g_pContainerVtable[item] = reinterpret_cast<void *>(&PGetItem);
    g_pReplicationVtable[read] = reinterpret_cast<void *>(&PSetRead);
    g_pReplicationVtable[write] = reinterpret_cast<void *>(&PSetWrite);
    OF<void *>(g_pReplication, 0) = g_pReplicationVtable;
    g_serverVtable[0x80 / 4] = reinterpret_cast<void *>(&FakeGetConnectionInfo);
    for (int i = 0; i < HUMANS; ++i) {
        OF<void *>(g_pControllers[i], 0) = g_pControllerVtable;
        OF<void *>(g_pContainers[i], 0) = g_pContainerVtable;
        for (int k = 0; k < 8; ++k)
            g_pContainerTable[i][k] = reinterpret_cast<C_ItemContainer *>(g_pContainers[i]);
        OF<void *>(g_oControllers[i], 0) = g_oControllerVtable;
        OF<void *>(g_oContainers[i], 0) = g_oContainerVtable;
        for (int k = 0; k < 8; ++k)
            g_oContainerTable[i][k] = g_oContainers[i];
    }
    g_oControllerVtable[0x64 / 4] = reinterpret_cast<void *>(&OIsSpectating);
    g_oContainerVtable[0x40 / 4] = reinterpret_cast<void *>(&OGetItem);
    g_oReplicationVtable[0] = reinterpret_cast<void *>(&OSetRead);
    g_oReplicationVtable[1] = reinterpret_cast<void *>(&OSetWrite);
    OF<void *>(g_oReplication, 0) = g_oReplicationVtable;

    // The original's.
    RedirectOriginal(0x007e566f, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e2120, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e8627, reinterpret_cast<const void *>(ONew));
    RedirectOriginal(0x007e8761, reinterpret_cast<const void *>(ODelete));
    RedirectOriginal(0x007e884a, reinterpret_cast<const void *>(ORealloc));
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(OAlignedMalloc));
    RedirectOriginal(0x007e42d9, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x007e4375, reinterpret_cast<const void *>(ONoLock));
    RedirectOriginal(0x006987d0, reinterpret_cast<const void *>(OGetGame));
    RedirectOriginal(0x0069c2b0, reinterpret_cast<const void *>(OPrint));
    RedirectOriginal(0x0069c2a0, reinterpret_cast<const void *>(OPrintColored));
    RedirectOriginal(0x0069c240, reinterpret_cast<const void *>(OPrintMessage));
    RedirectOriginal(0x0069b1a0, reinterpret_cast<const void *>(OSetRoundTime));
    RedirectOriginal(0x006dcd10, reinterpret_cast<const void *>(OGetGameApp));
    RedirectOriginal(0x0072a480, reinterpret_cast<const void *>(OSendGameMessage));
    RedirectOriginal(0x006c4e80, reinterpret_cast<const void *>(OAppPrintMessage));
    RedirectOriginal(0x006e5380, reinterpret_cast<const void *>(OTextsGet));
    RedirectOriginal(0x00475dd0, reinterpret_cast<const void *>(OGetActor));
    RedirectOriginal(0x005a5fe0, reinterpret_cast<const void *>(OGetControllerType));
    RedirectOriginal(0x005a8820, reinterpret_cast<const void *>(OGetTeam));
    RedirectOriginal(0x005470b0, reinterpret_cast<const void *>(OSendSoldierClass));
    RedirectOriginal(0x0073ea30, reinterpret_cast<const void *>(OClearMessages));
    RedirectOriginal(0x00749b20, reinterpret_cast<const void *>(OFreeBuffer));
    RedirectOriginal(0x00748dc0, reinterpret_cast<const void *>(OGetPerfTimeMs));
    g_realEnter = G<void *>(O_ENTER_SLOT);
    g_realLeave = G<void *>(O_LEAVE_SLOT);
    G<void *>(O_ENTER_SLOT) = reinterpret_cast<void *>(&OEnter);
    G<void *>(O_LEAVE_SLOT) = reinterpret_cast<void *>(&OLeave);
    G<void *>(O_PROFILE + 0x24) = g_mp;
    std::memset(ONet(), 0, 0x5080);
    OListInit(0x1F0);
    OListInit(0x1FC);
    // The original's quantisation table: its constructor (a static's, never run), compared with the port's.
    reinterpret_cast<void *(FAST *)(void *)>(O_TABLE_CTOR)(reinterpret_cast<void *>(static_cast<uintptr_t>(O_TABLE)));
    CompareQuantizations("the table's constructor");

    int rounds = std::getenv("HD2_PLAYERS_QUICK") ? 8000 : 30000;
    if (const char *r = std::getenv("HD2_PLAYERS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_PLAYERS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("players_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
