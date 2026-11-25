// Net part 9: the network game's messages (net/net_messages.cpp: the server's DirectPlay messages, the game messages
// C_GameApp queues and sends, the commands it posts, a soldier's state in a message) against the original executable's.
// MinGW; build_msgs.bat; runs msgs_diff.exe ("msgs_diff: N checks, 0 failures").
//
// Each round makes a random world in both programs' layouts: the net game (its players with the same bytes, the humans
// and connections each side's own; its message streams over the test's buffers; its CD-key checks; the transport's
// session bit), the rules (one block both read, its password a string both can read), the role and local slot, the
// game (or none) and its times, the humans (net ids, frames with a world matrix, uniforms, their soldier states), the
// actors by index, the squad's soldiers. Then a random sequence: soldier states written and read back (the bits and
// the states compared), the game messages' streams begun, game messages queued and sent (every type), commands posted
// and sent (every command), and DirectPlay's server messages (a player created, destroyed, a hosts query, a
// connection asked for or aborted, data, a buffer returned, the server's info). What they reach is the test's on both
// sides, logging alike: the locks (the players' and the app's command lock: the original's by its import slots), the
// clock, DirectPlay's GetClientInfo (one COM object), the name helpers, the connections and the transport, the key
// checks, the game, the app's lines, the texts, the modules' and the objectives' states, the scores, the rules' and
// the map's writers, the stream sends (their bits logged). After each call the result, the logs and the state are
// compared. HD2_MSGS_ROUNDS: rounds (default 20,000; HD2_MSGS_QUICK: 5,000); HD2_MSGS_SEED; HD2_MSGS_FIRST_CHANCE.
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
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/module.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/soldier.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "net/bitstream.h"
#include "net/dplay8.h"
#include "net/net_game.h"
#include "net/net_transport.h"
#undef private
#undef protected

#include "original_exe.h"

#ifdef SendMessage
#undef SendMessage
#endif

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

static_assert(sizeof(S_NetPlayerState) == 0xC4, "the player state's size");
static_assert(sizeof(S_SoldierItem) == 6, "the soldier item's size");

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

// The port's allocations zeroed, as the original's (the test's calloc): a buffer written with WriteBits keeps what was
// there before (the bits are ORed in), the same in both programs given the same memory.
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

// ---- the round's script (both sides answer from it) ----
constexpr int MAX_SLOTS = 12;
constexpr int HUMANS = 16;
struct S_HumanAnswers {
    int netId;
    int uniform;          // -1 none, else its definition's team
    std::string name;     // GetSoldierState's
    int team, profile, spawn, health;
};
struct S_Script {
    bool game = true;
    S_HumanAnswers humans[HUMANS];
    std::map<int, bool> actors;   // GetActor: index -> non-null
    uint32_t now = 0;             // timeGetTime
    int nameNumber = 1;           // FindFreeNameNumber
    int playerCount = 0;          // CountPlayers
    bool cjk = false;             // HasCjkCharacters
    uint32_t hostAddress = 0;     // GetHostAddress
    long infoHr1 = 0, infoHr2 = 0;   // GetClientInfo's answers
    std::wstring infoName;
} g_script;

// The connections: each side's objects by a serial (the ones the world gives, then the ones made).
std::map<const void *, int> g_links[2];
int g_linkSerial[2];
int LinkSerial(int side, const void *p)
{
    if (!p)
        return -1;
    const auto it = g_links[side].find(p);
    return it == g_links[side].end() ? 999 : it->second;
}

// ---- the fakes shared by both sides (C calls) ----
CRITICAL_SECTION *g_watched[2][2];   // [side][0 players, 1 the app's commands]
extern "C" void WINAPI FakeEnterCriticalSection(LPCRITICAL_SECTION section)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 2; ++k)
            if (section == g_watched[s][k]) {
                Log(std::string("Enter ") + (k ? "commands" : "players"));
                return;
            }
    ::EnterCriticalSection(section);
}
extern "C" void WINAPI FakeLeaveCriticalSection(LPCRITICAL_SECTION section)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 2; ++k)
            if (section == g_watched[s][k]) {
                Log(std::string("Leave ") + (k ? "commands" : "players"));
                return;
            }
    ::LeaveCriticalSection(section);
}
extern "C" DWORD WINAPI FakeTimeGetTime()
{
    Log("timeGetTime");
    return g_script.now;
}
// DirectPlay's server (one COM object for both: GetClientInfo is +0x24).
HRESULT __stdcall FakeGetClientInfo(void *, DWORD player, void *info, DWORD *size, DWORD flags)
{
    Log("GetClientInfo " + N(player) + " " + N(info != nullptr) + " " + N(*size) + " " + N(flags));
    const DWORD needed = 0x18 + static_cast<DWORD>((g_script.infoName.size() + 1) * 2);
    if (!info) {
        *size = needed;
        return g_script.infoHr1;
    }
    if (g_script.infoHr2 >= 0) {
        auto *d = static_cast<DWORD *>(info);
        Log("info " + N(d[0]) + " " + Hex(d[1]) + " " + Hex(d[2]) + " " + Hex(d[3]) + " " + Hex(d[4]) + " " + Hex(d[5]));
        wchar_t *name = reinterpret_cast<wchar_t *>(static_cast<uint8_t *>(info) + 0x18);
        std::memcpy(name, g_script.infoName.c_str(), (g_script.infoName.size() + 1) * 2);
        d[2] = static_cast<DWORD>(reinterpret_cast<uintptr_t>(name));
    }
    return g_script.infoHr2;
}
void *g_serverVtable[64];
void *g_serverObject = g_serverVtable;
// The players' names from DirectPlay's wide text (both: the test's narrowing).
void NarrowName(char *out, const wchar_t *text, int length, const char *what)
{
    std::string s;
    for (int i = 0; text[i] && i < length - 1; ++i)
        s += static_cast<char>(text[i] & 0x7F);
    Log(std::string(what) + " " + Str(s.c_str()) + " " + N(length));
    std::strcpy(out, s.c_str());
}

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_localPlayerSlot;
int g_versionMajor = 1;
int g_versionMinor = 2;
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

C_NetGame &PNet() { return g_netGame; }
// (net_game.cpp's, as there: the original's 0x00725e10 sets these; net part 11 compares it.)
S_NetPlayerState::S_NetPlayerState()
{
    team = 2;
    pendingTeam = 2;
    keyQuery = -1;
    unknownB8 = 50;
}

void C_NetLock::Enter() { Log(std::string("Enter ") + (this == &g_netGame.m_playersLock ? "players" : "?")); }
void C_NetLock::Leave() { Log(std::string("Leave ") + (this == &g_netGame.m_playersLock ? "players" : "?")); }
S_NetPlayerState *C_NetGame::LockPlayers()
{
    m_playersLock.Enter();
    return m_players.data();
}
void C_NetGame::UnlockPlayers() { m_playersLock.Leave(); }

int C_NetGame::FindFreeNameNumber(const char *name, int number)
{
    Log("FindFreeNameNumber " + Str(name) + " " + N(number));
    return g_script.nameNumber;
}
int C_NetGame::CountPlayers()
{
    Log("CountPlayers");
    return g_script.playerCount;
}
void C_NetGame::FillServerStatus(S_ServerStatus *status)
{
    Log("FillServerStatus");
    status->maxClients = 7;
}
void S_ServerStatus::Write(C_BitStream *stream)
{
    Log("S_ServerStatus::Write " + N(maxClients));
    stream->WriteBits(0x5A, 8);
}
void C_NetGame::QueueReceived(const void *data, int size, uint32_t flags)
{
    Log("QueueReceived " + N(reinterpret_cast<uintptr_t>(data)) + " " + N(size) + " " + Hex(flags));
}
void C_NetGame::DropKeyQuery(int index) { Log("DropKeyQuery " + N(index)); }
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
void C_NetGame::SetPlayerTeam(int slot, int team, bool pending)
{
    Log("SetPlayerTeam " + N(slot) + " " + N(team) + " " + N(pending));
}
void C_NetGame::Process(int timeDelta, bool replicate) { Log("Process " + N(timeDelta) + " " + N(replicate)); }
void C_NetGame::WriteScores(C_BitStream *stream)
{
    Log("WriteScores");
    stream->WriteBits(5, 3);
}
void S_NetStats::Write(C_BitStream *stream)
{
    Log("S_NetStats::Write " + N(values[0]) + " " + N(values[3]) + " " + N(values[8]));
    stream->WriteBits(static_cast<uint32_t>(values[3]) & 15, 4);
}
void C_NetTransport::Add(C_NetConnection *connection) { Log("Add " + N(LinkSerial(0, connection))); }
void C_NetTransport::Remove(C_NetConnection *connection) { Log("Remove " + N(LinkSerial(0, connection))); }
C_NetConnection::C_NetConnection(int id, uint32_t address, C_NetSocket *socket) : m_id(id), m_address(address), m_socket(socket)
{
    const int serial = g_linkSerial[0]++;
    g_links[0][this] = serial;
    Log("C_NetConnection " + N(serial) + " " + N(id) + " " + Hex(address) + " " +
        N(socket == &reinterpret_cast<C_NetTransport *>(g_netGame.m_transport)->m_socket));
}
C_NetConnection::~C_NetConnection()
{
    Log("~C_NetConnection " + N(LinkSerial(0, this)));
    g_links[0].erase(this);
}
uint32_t GetHostAddress(IDirectPlay8Address *address)
{
    Log("GetHostAddress " + N(reinterpret_cast<uintptr_t>(address)));
    return g_script.hostAddress;
}
void NetWideToUtf8(char *out, const wchar_t *text, int length) { NarrowName(out, text, length, "NetWideToUtf8"); }
void NetWideToAnsi(char *out, const wchar_t *text, int length) { NarrowName(out, text, length, "NetWideToAnsi"); }
bool HasCjkCharacters(const uint16_t *text)
{
    Log("HasCjkCharacters " + N(text != nullptr));
    return g_script.cjk;
}
alignas(16) unsigned char g_pMap[0x100];
C_MpMap *S_MultiplayerSettings::GetCurrentMap()
{
    Log("GetCurrentMap");
    return reinterpret_cast<C_MpMap *>(g_pMap);
}
void S_MultiplayerSettings::WriteRules(C_BitStream *stream)
{
    Log("WriteRules");
    stream->WriteBits(0x15, 5);
}
void S_MultiplayerSettings::WriteCurrentMap(C_BitStream *stream)
{
    Log("WriteCurrentMap");
    stream->WriteBits(6, 3);
}

alignas(16) unsigned char g_pApp[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_pApp); }
void C_GameApp::PrintMessage(const char *text) { Log("AppPrintMessage " + Str(text)); }
alignas(16) unsigned char g_pGame[sizeof(C_game)];
C_game *GetGame() { return g_script.game ? reinterpret_cast<C_game *>(g_pGame) : nullptr; }
void C_game::OnPlayerLeft(int connection) { Log("OnPlayerLeft " + N(connection)); }
void C_game::WriteObjectiveStates(C_BitStream *msg) const
{
    Log("WriteObjectiveStates");
    msg->WriteBits(9, 4);
}
namespace ModuleManager {
void BroadcastWriteState(C_BitStream *msg)
{
    Log("BroadcastWriteState");
    msg->WriteBits(3, 2);
}
}  // namespace ModuleManager
void C_TemporaryActorsList::WriteState(C_BitStream *msg)
{
    Log("TemporaryActors::WriteState");
    msg->WriteBits(0xB, 4);
}
C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    char text[32];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
C_Actor *C_ActorsModule::GetActor(int index) const
{
    Log("GetActor " + N(index));
    const auto it = g_script.actors.find(index);
    return it != g_script.actors.end() && it->second ? reinterpret_cast<C_Actor *>(g_pMap) : nullptr;
}

// The humans: their frames (one object each, both sides read it: the world matrix valid), uniforms, soldier states.
alignas(16) unsigned char g_pHumans[HUMANS][sizeof(C_Human)];
alignas(16) unsigned char g_frames[HUMANS][0x200];
alignas(16) unsigned char g_pContainers[HUMANS][0x40];
alignas(16) unsigned char g_pItems[HUMANS][sizeof(C_Item)];
alignas(16) unsigned char g_pDefs[HUMANS][sizeof(C_ItemDef)];
C_ItemContainer *g_pContainerTable[HUMANS][8];
void *g_pContainerVtable[0x40];
C_Human *PHuman(int i) { return reinterpret_cast<C_Human *>(g_pHumans[i]); }
int PHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_pHumans[i])
            return i;
    return p ? 99 : -1;
}
C_Item *__attribute__((thiscall)) PGetItem(void *self)
{
    const int i = static_cast<int>((static_cast<unsigned char *>(self) - g_pContainers[0]) / 0x40);
    Log("GetItem " + N(i));
    return g_script.humans[i].uniform < 0 ? nullptr : reinterpret_cast<C_Item *>(g_pItems[i]);
}
void C_Human::GetSoldierState(S_SoldierState &state) const
{
    const int i = PHumanIndex(this);
    const S_HumanAnswers &h = g_script.humans[i];
    Log("GetSoldierState " + N(i) + " " + N(state.unknown34) + " " + N(state.unknown38) + " " + N(state.profileIndex) + " " + N(state.team) + " " +
        N(state.spawnZone) + " " +
        N(state.deployed) + " " + N(state.speechSet));
    state.name = h.name.c_str();
    state.team = h.team;
    state.profileIndex = h.profile;
    state.spawnZone = h.spawn;
    state.health = h.health;
    state.head.uid = static_cast<int16_t>(i);
    state.head.typeId = static_cast<int16_t>(h.profile);
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
const uint32_t O_VERSION_MAJOR = 0x00845070, O_VERSION_MINOR = 0x00845074, O_ACTORS = 0x0089b4b0;
const uint32_t O_TABLE = 0x00982dec, O_COUNT = 0x00982df0;
const uint32_t O_FSTR = 0x00405100, O_STR_ASSIGN = 0x00405180, O_MSG_CTOR = 0x0073d740;
const uint32_t O_ALIGNED_MALLOC = 0x007e53c2;
const uint32_t O_ENTER_SLOT = 0x0080f1a8, O_LEAVE_SLOT = 0x0080f1ac, O_TIME_SLOT = 0x0080f3f8;
const uint32_t O_HANDLE = 0x00726470, O_SEND_GAME = 0x0072a480, O_POST = 0x0072b590, O_WRITE_STATE = 0x00727850,
               O_READ_STATE = 0x00727a30, O_BEGIN_GAME = 0x0072c1d0, O_BEGIN_COMMAND = 0x0072c190, O_BEGIN_REPLY = 0x0072c1b0;

volatile uintptr_t g_oNetAddress = O_NET;   // (volatile: g++ sees no object at a constant address)
uint8_t *ONet() { return reinterpret_cast<uint8_t *>(g_oNetAddress); }
uint8_t *OPlayers() { return OF<uint8_t *>(ONet(), 0x1E0); }
int OPlayerCount() { return OPlayers() ? static_cast<int>((OF<uint8_t *>(ONet(), 0x1E4) - OPlayers()) / 0xC4) : 0; }

alignas(16) uint8_t g_oGame[0x200], g_oApp[0x100], g_oMap[0x40];
alignas(16) uint8_t g_oHumans[HUMANS][0x400], g_oContainers[HUMANS][0x40], g_oItems[HUMANS][0x40], g_oDefs[HUMANS][0x80];
void *g_oContainerTable[HUMANS][8];
void *g_oContainerVtable[0x40];
void *g_oLinkVtable[4];
alignas(16) uint8_t g_oSquad[0x40];

int OHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_oHumans[i])
            return i;
    return p ? 99 : -1;
}

void *ONew(size_t n) { return std::calloc(n ? n : 1, 1); }
void ODelete(void *p) { std::free(p); }
void *ORealloc(void *p, size_t n) { return std::realloc(p, n); }
void THISCALL ONoLock(void *) {}
int OAtexit(void *) { return 0; }

// An original string's representation (refCount, length, characters), as C_str makes it.
void *ORep(const char *text)
{
    const size_t n = std::strlen(text);
    auto *rep = static_cast<uint8_t *>(ONew(8 + n + 1));
    OF<int>(rep, 0) = 1;
    OF<int>(rep, 4) = static_cast<int>(n);
    std::memcpy(rep + 8, text, n + 1);
    return rep;
}
const char *ORepText(void *rep) { return rep ? reinterpret_cast<const char *>(static_cast<uint8_t *>(rep) + 8) : ""; }

int THISCALL OFindFreeNameNumber(void *, const char *name, int number)
{
    Log("FindFreeNameNumber " + Str(name) + " " + N(number));
    return g_script.nameNumber;
}
int FAST OCountPlayers(void *)
{
    Log("CountPlayers");
    return g_script.playerCount;
}
void THISCALL OFillServerStatus(void *, void *status)
{
    Log("FillServerStatus");
    OF<int>(status, 0x10) = 7;
}
void THISCALL OStatusWrite(void *status, void *stream);
void THISCALL OQueueReceived(void *, const void *data, int size, uint32_t flags)
{
    Log("QueueReceived " + N(reinterpret_cast<uintptr_t>(data)) + " " + N(size) + " " + Hex(flags));
}
void THISCALL ODropKeyQuery(void *, int index) { Log("DropKeyQuery " + N(index)); }
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
void THISCALL OSetPlayerTeam(void *, int slot, int team, bool pending)
{
    Log("SetPlayerTeam " + N(slot) + " " + N(team) + " " + N(pending));
}
void THISCALL OProcess(void *, int timeDelta, bool replicate) { Log("Process " + N(timeDelta) + " " + N(replicate)); }
const uint32_t O_WRITE_BITS = 0x0073d320;
void OWrite(void *stream, uint32_t value, int bits)
{
    reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(O_WRITE_BITS)(stream, value, bits);
}
void THISCALL OStatusWrite(void *status, void *stream)
{
    Log("S_ServerStatus::Write " + N(OF<int>(status, 0x10)));
    OWrite(stream, 0x5A, 8);
}
void THISCALL OWriteScores(void *, void *stream)
{
    Log("WriteScores");
    OWrite(stream, 5, 3);
}
void THISCALL OStatsWrite(void *stats, void *stream)
{
    Log("S_NetStats::Write " + N(OF<int>(stats, 0)) + " " + N(OF<int>(stats, 12)) + " " + N(OF<int>(stats, 32)));
    OWrite(stream, static_cast<uint32_t>(OF<int>(stats, 12)) & 15, 4);
}
void THISCALL OAdd(void *, void *connection) { Log("Add " + N(LinkSerial(1, connection))); }
void THISCALL ORemove(void *, void *connection) { Log("Remove " + N(LinkSerial(1, connection))); }
void *THISCALL OLinkDelete(void *self, unsigned flags)
{
    Log("~C_NetConnection " + N(LinkSerial(1, self)));
    g_links[1].erase(self);
    if (flags & 1)
        ODelete(self);
    return self;
}
void *THISCALL OLinkCtor(void *self, int id, uint32_t address, void *socket)
{
    OF<void *>(self, 0) = g_oLinkVtable;
    OF<int>(self, 4) = id;
    OF<uint32_t>(self, 0xC) = address;
    const int serial = g_linkSerial[1]++;
    g_links[1][self] = serial;
    Log("C_NetConnection " + N(serial) + " " + N(id) + " " + Hex(address) + " " + N(socket == ONet() + 0x34C));
    return self;
}
uint32_t __stdcall OGetHostAddress(void *address)
{
    Log("GetHostAddress " + N(reinterpret_cast<uintptr_t>(address)));
    return g_script.hostAddress;
}
// (The original's NetWideTo* and GetHostAddress pop their arguments.)
void __stdcall OWideToUtf8(char *out, const wchar_t *text, int length) { NarrowName(out, text, length, "NetWideToUtf8"); }
void __stdcall OWideToAnsi(char *out, const wchar_t *text, int length) { NarrowName(out, text, length, "NetWideToAnsi"); }
bool FAST OHasCjk(const uint16_t *text)
{
    Log("HasCjkCharacters " + N(text != nullptr));
    return g_script.cjk;
}
void *THISCALL OGetCurrentMap(void *)
{
    Log("GetCurrentMap");
    return g_oMap;
}
void THISCALL OWriteRules(void *, void *stream)
{
    Log("WriteRules");
    OWrite(stream, 0x15, 5);
}
void THISCALL OWriteCurrentMap(void *, void *stream)
{
    Log("WriteCurrentMap");
    OWrite(stream, 6, 3);
}
void *OGetGameApp() { return g_oApp; }
void THISCALL OAppPrintMessage(void *, const char *text) { Log("AppPrintMessage " + Str(text)); }
void *OGetGame() { return g_script.game ? g_oGame : nullptr; }
void THISCALL OOnPlayerLeft(void *, int connection) { Log("OnPlayerLeft " + N(connection)); }
void THISCALL OWriteObjectiveStates(void *, void *msg)
{
    Log("WriteObjectiveStates");
    OWrite(msg, 9, 4);
}
void FAST OBroadcastWriteState(void *msg)
{
    Log("BroadcastWriteState");
    OWrite(msg, 3, 2);
}
void THISCALL OTemporaryWriteState(void *, void *msg)
{
    Log("TemporaryActors::WriteState");
    OWrite(msg, 0xB, 4);
}
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    OF<void *>(ret, 0) = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(ret, "T%X", id);
    return ret;
}
void *THISCALL OGetActor(void *, int index)
{
    Log("GetActor " + N(index));
    const auto it = g_script.actors.find(index);
    return it != g_script.actors.end() && it->second ? g_oMap : nullptr;
}
void *THISCALL OGetItem(void *self)
{
    const int i = static_cast<int>((static_cast<uint8_t *>(self) - g_oContainers[0]) / 0x40);
    Log("GetItem " + N(i));
    return g_script.humans[i].uniform < 0 ? nullptr : g_oItems[i];
}
void THISCALL OGetSoldierState(void *human, void *state)
{
    const int i = OHumanIndex(human);
    const S_HumanAnswers &h = g_script.humans[i];
    Log("GetSoldierState " + N(i) + " " + N(OF<int>(state, 0x34)) + " " + N(OF<int>(state, 0x38)) + " " + N(OF<int>(state, 0x3C)) + " " +
        N(OF<int>(state, 0x40)) + " " + N(OF<int>(state, 4)) + " " +
        N(OF<uint8_t>(state, 0x44)) + " " + N(OF<int>(state, 0x48)));
    reinterpret_cast<void *(THISCALL *)(void *, const char *)>(O_STR_ASSIGN)(state, h.name.c_str());
    OF<int>(state, 0x40) = h.team;
    OF<int>(state, 0x3C) = h.profile;
    OF<int>(state, 4) = h.spawn;
    OF<int>(state, 0xC) = h.health;
    OF<int16_t>(state, 0x4C) = static_cast<int16_t>(i);
    OF<int16_t>(state, 0x4E) = static_cast<int16_t>(h.profile);
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

// ---- the message streams (the net game's five: messages[0..2], unreliable, direct) over the test's buffers ----
constexpr int WORDS = 4096;
const int O_STREAMS[5] = {0x208, 0x244, 0x280, 0x2BC, 0x2F8};
uint32_t g_pBuf[5][WORDS], g_oBuf[5][WORDS];
C_NetMessage *PStream(int k)
{
    C_NetGame &net = PNet();
    return k < 3 ? &net.m_messages[k] : k == 3 ? &net.m_unreliable : &net.m_direct;
}
void *OStream(int k) { return ONet() + O_STREAMS[k]; }
int PStreamIndex(const void *p)
{
    for (int k = 0; k < 5; ++k)
        if (p == PStream(k))
            return k;
    return -1;
}
int OStreamIndex(const void *p)
{
    for (int k = 0; k < 5; ++k)
        if (p == OStream(k))
            return k;
    return -1;
}
std::string StreamBits(const uint32_t *words, uint32_t bits)
{
    std::string s = N(bits) + ":";
    for (uint32_t w = 0; w < (bits + 31) / 32; ++w)
        s += Hex(w == bits / 32 && (bits & 31) ? words[w] & ((1u << (bits & 31)) - 1) : words[w]);
    return s;
}
void ResetStream(int k)
{
    C_NetMessage *p = PStream(k);
    std::memset(g_pBuf[k], 0, sizeof g_pBuf[k]);
    std::memset(g_oBuf[k], 0, sizeof g_oBuf[k]);
    p->Attach(g_pBuf[k], WORDS * 32);
    p->ResetPosition();
    const int channel = I(0, 2);
    p->m_channel = channel;
    void *o = OStream(k);
    OF<int>(o, 0x20) = channel;
    OF<uint32_t *>(o, 4) = g_oBuf[k];
    OF<uint32_t>(o, 8) = 0;
    OF<uint32_t>(o, 0xC) = 0;
    OF<uint32_t>(o, 0x10) = WORDS * 32;
}
// The stream sent: its fields and bits logged, then emptied (as the send would leave it).
void C_NetGameSendLog(int k, bool unreliable, int pending, int channel, int target, uint32_t type, const uint32_t *words, uint32_t bits)
{
    Log("SendNetMessage " + N(k) + " " + N(unreliable) + " " + N(pending) + " " + N(channel) + " " + N(target) + " " + N(type) + " " +
        StreamBits(words, bits));
}
uint32_t OPosition(void *o) { return OF<uint32_t>(o, 8) * 32 + OF<uint32_t>(o, 0xC); }
void THISCALL OSendNetMessage(void *, void *msg)
{
    const int k = OStreamIndex(msg);
    C_NetGameSendLog(k, OF<uint8_t>(msg, 0x18) != 0, OF<int>(msg, 0x1C), OF<int>(msg, 0x20), OF<int>(msg, 0x24), OF<uint32_t>(msg, 0x28),
                     OF<uint32_t *>(msg, 4), OPosition(msg));
    if (k >= 0) {
        std::memset(g_oBuf[k], 0, sizeof g_oBuf[k]);
        OF<uint32_t>(msg, 8) = 0;
        OF<uint32_t>(msg, 0xC) = 0;
        OF<int>(msg, 0x1C) = 0;
    }
}

// ---- the soldier states: a model, made in each program's layout ----
struct S_StateModel {
    std::string name;
    int spawnZone, health, profileIndex, team, unknown34, unknown38, speechSet;
    bool spectating, vip, deployed;
    S_SoldierItem items[5];
    std::vector<S_SoldierItem> belt, backpack;
    uint32_t load[8];
};
S_SoldierItem RandomItem()
{
    S_SoldierItem it;
    it.uid = static_cast<int16_t>(P(0.2) ? -1 : I(0, 0x7FFF));
    it.typeId = static_cast<int16_t>(P(0.2) ? -1 : I(0, 300));
    it.present = static_cast<int16_t>(P(0.2) ? I(-300, 300) : I(0, 3));
    return it;
}
S_StateModel RandomState()
{
    S_StateModel m;
    static const char *const names[] = {"", "Bob", "Soldier X", "a very long soldier name for the test of strings", "N"};
    m.name = names[I(0, 4)];
    if (P(0.1))
        m.name = std::string(static_cast<size_t>(I(250, 300)), 'q');
    m.spawnZone = P(0.2) ? -1 : I(0, 0x500);
    m.health = I(0, 1100);
    m.profileIndex = P(0.2) ? -1 : I(0, 300);
    m.team = I(0, 3);
    m.unknown34 = I(-1, 5);
    m.unknown38 = I(-1, 5);
    m.speechSet = I(-1, 4);
    m.spectating = P(0.5);
    m.vip = P(0.5);
    m.deployed = P(0.5);
    for (S_SoldierItem &it : m.items)
        it = RandomItem();
    for (int k = P(0.3) ? 0 : I(0, 6); k > 0; --k)
        m.belt.push_back(RandomItem());
    for (int k = P(0.3) ? 0 : I(0, 6); k > 0; --k)
        m.backpack.push_back(RandomItem());
    if (P(0.05))
        for (int k = 0; k < 260; ++k)
            m.belt.push_back(RandomItem());
    for (uint32_t &w : m.load)
        w = static_cast<uint32_t>(rng());
    return m;
}
void ToPort(const S_StateModel &m, S_SoldierState &s)
{
    s.name = m.name.c_str();
    s.spawnZone = m.spawnZone;
    s.spectating = m.spectating;
    s.vip = m.vip;
    s.health = m.health;
    s.unknown34 = m.unknown34;
    s.unknown38 = m.unknown38;
    s.profileIndex = m.profileIndex;
    s.team = m.team;
    s.deployed = m.deployed;
    s.speechSet = m.speechSet;
    s.head = m.items[0];
    s.uniform = m.items[1];
    s.backpack = m.items[2];
    s.longGun = m.items[3];
    s.hands = m.items[4];
    s.belt = m.belt;
    s.backpackContents = m.backpack;
    std::memcpy(&s.load, m.load, sizeof m.load);
}
void OVector(void *o, int at, const std::vector<S_SoldierItem> &v)
{
    if (v.empty()) {
        OF<void *>(o, at) = nullptr;
        OF<void *>(o, at + 4) = nullptr;
        OF<void *>(o, at + 8) = nullptr;
        return;
    }
    auto *b = static_cast<uint8_t *>(ONew(v.size() * 6));
    std::memcpy(b, v.data(), v.size() * 6);
    OF<void *>(o, at) = b;
    OF<void *>(o, at + 4) = b + v.size() * 6;
    OF<void *>(o, at + 8) = b + v.size() * 6;
}
void *ToOrig(const S_StateModel &m)
{
    auto *o = static_cast<uint8_t *>(ONew(0xAC));
    OF<void *>(o, 0) = m.name.empty() && P(0.5) ? nullptr : ORep(m.name.c_str());
    OF<int>(o, 4) = m.spawnZone;
    OF<uint8_t>(o, 8) = m.spectating;
    OF<uint8_t>(o, 9) = m.vip;
    OF<int>(o, 0xC) = m.health;
    OF<int>(o, 0x34) = m.unknown34;
    OF<int>(o, 0x38) = m.unknown38;
    OF<int>(o, 0x3C) = m.profileIndex;
    OF<int>(o, 0x40) = m.team;
    OF<uint8_t>(o, 0x44) = m.deployed;
    OF<int>(o, 0x48) = m.speechSet;
    for (int k = 0; k < 5; ++k)
        std::memcpy(o + 0x4C + 6 * k, &m.items[k], 6);
    OVector(o, 0x70, m.belt);
    OVector(o, 0x80, m.backpack);
    std::memcpy(o + 0x8C, m.load, sizeof m.load);
    return o;
}
std::string ItemsText(const S_SoldierItem *items, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; ++i)
        s += N(items[i].uid) + "/" + N(items[i].typeId) + "/" + N(items[i].present) + " ";
    return s;
}
std::string PortStateText(const S_SoldierState &s)
{
    std::string t = Str(s.name.c_str()) + " " + N(s.spawnZone) + " " + N(s.spectating) + N(s.vip) + " " + N(s.health) + " " +
                    N(s.profileIndex) + " " + N(s.team) + " [";
    const S_SoldierItem items[5] = {s.head, s.uniform, s.backpack, s.longGun, s.hands};
    t += ItemsText(items, 5) + "] [" + ItemsText(s.belt.data(), s.belt.size()) + "] [" +
         ItemsText(s.backpackContents.data(), s.backpackContents.size()) + "]";
    uint32_t load[8];
    std::memcpy(load, &s.load, sizeof load);
    for (uint32_t w : load)
        t += " " + Hex(w);
    return t;
}
std::string OrigStateText(void *o)
{
    std::string t = Str(ORepText(OF<void *>(o, 0))) + " " + N(OF<int>(o, 4)) + " " + N(OF<uint8_t>(o, 8)) + N(OF<uint8_t>(o, 9)) + " " +
                    N(OF<int>(o, 0xC)) + " " + N(OF<int>(o, 0x3C)) + " " + N(OF<int>(o, 0x40)) + " [";
    t += ItemsText(reinterpret_cast<const S_SoldierItem *>(static_cast<uint8_t *>(o) + 0x4C), 5) + "] [";
    for (int at : {0x70, 0x80}) {
        const uint8_t *first = OF<uint8_t *>(o, at), *last = OF<uint8_t *>(o, at + 4);
        if (first)
            t += ItemsText(reinterpret_cast<const S_SoldierItem *>(first), static_cast<size_t>(last - first) / 6);
        t += at == 0x70 ? "] [" : "]";
    }
    for (int k = 0; k < 8; ++k)
        t += " " + Hex(OF<uint32_t>(o, 0x8C + 4 * k));
    return t;
}
void FreeOrigState(void *o)
{
    void *rep = OF<void *>(o, 0);
    if (rep && --OF<int>(rep, 0) == 0)
        ODelete(rep);
    ODelete(OF<void *>(o, 0x70));
    ODelete(OF<void *>(o, 0x80));
    ODelete(o);
}

// ---- the world ----
int g_slots;
// Slots whose link an abort deleted and left (both programs): a new link may take its address on one side only.
std::set<int> g_dangling;
alignas(16) uint8_t g_pTransport[sizeof(C_NetTransport)];
void *g_password;   // the rules' password (a string's representation both programs read)
alignas(16) uint8_t g_mp[0x400];
alignas(16) S_NetQuantization g_quantization[2];

void MakeLinks()
{
    for (int side = 0; side < 2; ++side) {
        for (auto &l : g_links[side])
            (void)l;
        g_links[side].clear();
        g_linkSerial[side] = 0;
    }
}

void SetWorld()
{
    C_NetGame &net = PNet();
    static const uint32_t roles[] = {7, 1, 2, 3, 0, 5, 1, 3};
    g_gameRole = roles[I(0, 7)];
    G<uint32_t>(O_ROLE) = g_gameRole;
    g_slots = P(0.05) ? 32 : I(1, MAX_SLOTS);
    g_localPlayerSlot = I(0, g_slots - 1);
    G<int>(O_LOCAL_SLOT) = g_localPlayerSlot;
    g_script.game = P(0.9);
    g_script.now = static_cast<uint32_t>(I(0, 1000000));
    g_script.nameNumber = P(0.6) ? 1 : I(0, 3);
    g_script.playerCount = I(0, 33);
    g_script.cjk = P(0.3);
    g_script.hostAddress = static_cast<uint32_t>(rng());
    static const long hrs[] = {0, 0, static_cast<long>(0x80158100), static_cast<long>(0x80158420), static_cast<long>(0x80004005)};
    g_script.infoHr1 = hrs[I(0, 4)];
    g_script.infoHr2 = P(0.85) ? 0 : static_cast<long>(0x80004005);
    static const wchar_t *const wnames[] = {L"Alice", L"Bob", L"P3", L"", L"averyveryverylongnameofaplayerthatgoesonandonandonandon"};
    g_script.infoName = wnames[I(0, 4)];

    // The rules (one block), the profile's.
    std::memset(g_mp, 0, sizeof g_mp);
    S_MultiplayerSettings &mp = *reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    mp.sessionType = I(0, 2);
    mp.gameType = I(0, 4);
    mp.dedicated = P(0.5);
    mp.maxClients = I(0, 40);
    mp.unknown174 = P(0.5) ? 1 : I(0, 2);
    mp.unknown178 = I(0, 2);
    mp.port = I(0, 0xFFFF);
    static const char *const passwords[] = {"", "secret", "pw"};
    if (g_password && --OF<int>(g_password, 0) == 0)
        ODelete(g_password);
    g_password = P(0.3) ? nullptr : ORep(passwords[I(0, 2)]);
    OF<void *>(g_mp, 0x218) = g_password;
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_unicodeFont = P(0.3);
    G<uint8_t>(O_PROFILE + 0x30) = reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_unicodeFont;

    // The game's times.
    C_game *game = reinterpret_cast<C_game *>(g_pGame);
    game->m_missionTime = static_cast<uint32_t>(P(0.2) ? I(0, 120) : I(0, 1000000));
    OF<uint32_t>(g_oGame, 0x28) = game->m_missionTime;
    // The app's id.
    GetGameApp()->m_randomId = static_cast<uint32_t>(rng());
    OF<uint32_t>(g_oApp, 0x1C) = GetGameApp()->m_randomId;
    net.m_unknown448 = static_cast<int>(rng());
    OF<int>(ONet(), 0x448) = net.m_unknown448;
    net.m_unknown8 = false;
    OF<uint8_t>(ONet(), 8) = 0;
    net.m_lastError = 0;
    net.m_lastErrorText = nullptr;
    OF<int>(ONet(), 0x504C) = 0;
    OF<const char *>(ONet(), 0x5050) = nullptr;
    // The transport's session bit.
    const uint32_t session = static_cast<uint32_t>(I(0, 3));
    reinterpret_cast<C_NetTransport *>(g_pTransport)->m_session = session;
    OF<uint32_t>(ONet(), 0x410) = session;

    // The humans.
    for (int i = 0; i < HUMANS; ++i) {
        S_HumanAnswers &h = g_script.humans[i];
        h.netId = P(0.2) ? -1 : I(0, 200);
        h.uniform = P(0.2) ? -1 : I(0, 3);
        h.name = "S" + std::to_string(i);
        h.team = I(0, 1);
        h.profile = I(-1, 40);
        h.spawn = I(-1, 50);
        h.health = I(0, 100);
        C_Human *ph = PHuman(i);
        ph->m_netId = h.netId;
        OF<int>(g_oHumans[i], 0x10) = h.netId;
        ph->m_frame = reinterpret_cast<I3D_frame *>(g_frames[i]);
        OF<void *>(g_oHumans[i], 0x20) = g_frames[i];
        ph->m_containers = g_pContainerTable[i];
        OF<void *>(g_oHumans[i], 0x74) = g_oContainerTable[i];
        reinterpret_cast<C_Item *>(g_pItems[i])->m_def = reinterpret_cast<C_ItemDef *>(g_pDefs[i]);
        reinterpret_cast<C_ItemDef *>(g_pDefs[i])->m_team = h.uniform;
        OF<void *>(g_oItems[i], 4) = g_oDefs[i];
        OF<int>(g_oDefs[i], 0x44) = h.uniform;
        // The frame: its world matrix valid (flag 0x20 at +0xE0), the matrix at +0x40.
        float *m = reinterpret_cast<float *>(g_frames[i] + 0x40);
        for (int k = 0; k < 16; ++k)
            m[k] = P(0.1) ? 0.0f : R(-100.0f, 100.0f);
        if (P(0.2))
            m[8] = m[9] = m[10] = 0.0f;
        if (P(0.1)) {
            m[8] = 0.6f;
            m[9] = 0.0f;
            m[10] = 0.8f;
        }
        if (P(0.1)) {
            // (A facing too short to normalise: its sign taken, x first, then z, then y.)
            const int k = I(0, 3);
            m[8] = k == 0 ? (P(0.5) ? 5e-5f : -5e-5f) : 0.0f;
            m[9] = P(0.5) ? 5e-5f : -5e-5f;
            m[10] = k == 1 ? (P(0.5) ? 5e-5f : -5e-5f) : 0.0f;
        }
        OF<uint32_t>(g_frames[i], 0xE0) = 0x20;
    }
    g_script.actors.clear();
    for (int a = 0; a < 48; ++a)
        g_script.actors[a] = P(0.5);
    // The actors module's count (the port's vector, the original's first / last).
    auto *am = reinterpret_cast<C_ActorsModule *>(&g_actorsModule);
    const int actors = P(0.3) ? I(0, 31) : I(32, 47);
    am->m_actors.assign(static_cast<size_t>(actors), nullptr);
    static void *oActors[64];
    G<void **>(O_ACTORS + 0x10) = oActors;
    G<void **>(O_ACTORS + 0x14) = oActors + actors;

    // The players.
    MakeLinks();
    g_dangling.clear();
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
        p.connection = P(g_slots == 32 ? 0.0 : 0.25) ? 0 : I(1, 60);
        std::memset(p.name, 0, sizeof p.name);
        if (P(0.8))
            std::snprintf(p.name, sizeof p.name, "P%d", s);
        const int h = P(0.85) ? I(0, HUMANS - 1) : -1;
        p.human = h < 0 ? nullptr : PHuman(h);
        p.team = I(0, 2);
        p.stats.values[NET_STAT_RANK] = I(0, 7);
        p.left = P(0.2);
        p.joinStage = I(0, 3);
        p.keyQuery = I(-1, 99);
        p.voiceBuffer = nullptr;
        uint8_t *o = op + s * 0xC4;
        std::memcpy(o, raw, 0xC4);
        OF<void *>(o, 0x3C) = h < 0 ? nullptr : g_oHumans[h];
        // The connections (made by the test, each side's).
        if (P(0.8)) {
            g_faking = false;
            p.link = new C_NetConnection(s, 0, nullptr);
            void *ol = ONew(0x50);
            OLinkCtor(ol, s, 0, nullptr);
            OF<void *>(o, 0x6C) = ol;
        } else {
            p.link = nullptr;
            OF<void *>(o, 0x6C) = nullptr;
        }
    }

    // The CD-key checks: some of the players' names and addresses, of any state.
    for (int i = 0; i < 100; ++i) {
        S_KeyQuery &q = net.m_keyQueries[i];
        uint8_t *o = ONet() + 0x55C + 0xB8 * i;
        const bool used = i < 8 || P(0.05);
        q.address = used ? (P(0.6) ? g_script.hostAddress : static_cast<uint32_t>(I(0, 5))) : 0;
        std::memset(q.name, 0, sizeof q.name);
        if (used) {
            static const char *const names[] = {"Joiner", "Joiner2", "Joiner3", "Other"};
            std::strcpy(q.name, names[I(0, 3)]);
        }
        q.state = used ? I(0, 7) : 0;
        OF<uint32_t>(o, 0x6C) = q.address;
        std::memcpy(o + 0x78, q.name, sizeof q.name);
        OF<int>(o, 0xAC) = q.state;
    }
    // The squad's soldiers (the posted commands 0 and 1: the first one's state; the original reads an empty squad's
    // null: always one).
    // (Made per command: see PostSoldier.)
}

// A slot filled from the original's stack-constructed state (a joining player's, the server's own): the bytes its
// constructor leaves as the stack had them (the address, +0x78, +0x80..+0xA0, +0xA3, the activity time, +0xC0) are
// not compared in that call, then the port's take the original's.
int g_freshSlot = -1;
bool Indeterminate(int k)
{
    return (k >= 4 && k < 8) || (k >= 0x78 && k < 0x7C) || (k >= 0x80 && k < 0xA1) || k == 0xA3 || (k >= 0xA8 && k < 0xAC) ||
           (k >= 0xC0 && k < 0xC4);
}
void CarryFreshSlot()
{
    if (g_freshSlot >= 0 && g_freshSlot < OPlayerCount() && static_cast<size_t>(g_freshSlot) < PNet().m_players.size()) {
        auto *a = reinterpret_cast<uint8_t *>(&PNet().m_players[static_cast<size_t>(g_freshSlot)]);
        const uint8_t *b = OPlayers() + g_freshSlot * 0xC4;
        for (int k = 0; k < 0xC4; ++k)
            if (Indeterminate(k))
                a[k] = b[k];
    }
    g_freshSlot = -1;
}

// The whole state compared: the players (their bytes, the humans and connections by index), the net game's fields,
// the key checks' states, the transport's session.
void CompareState()
{
    C_NetGame &net = PNet();
    std::string s;
    if (OPlayerCount() != static_cast<int>(net.m_players.size()))
        s += "the players' count " + N(OPlayerCount()) + " / " + N(static_cast<long long>(net.m_players.size())) + "; ";
    for (int i = 0; i < OPlayerCount() && i < static_cast<int>(net.m_players.size()) && s.empty(); ++i) {
        const uint8_t *a = reinterpret_cast<const uint8_t *>(&net.m_players[static_cast<size_t>(i)]);
        uint8_t *b = OPlayers() + i * 0xC4;
        // (The name as a string: a joining player's slot takes the original's stack state, its name's bytes after the
        // first left as the stack had them; nothing reads past the end.)
        if (std::strcmp(reinterpret_cast<const char *>(a + 8), reinterpret_cast<const char *>(b + 8)) != 0)
            s += "slot " + N(i) + " name " + Str(reinterpret_cast<const char *>(b + 8)) + " / " + Str(reinterpret_cast<const char *>(a + 8)) + "; ";
        for (int k = 0; k < 0xC4; ++k) {
            if ((k >= 0x3C && k < 0x40) || (k >= 0x6C && k < 0x70) || (k >= 8 && k < 0x3C) || (i == g_freshSlot && Indeterminate(k)))
                continue;
            if (a[k] != b[k]) {
                s += "slot " + N(i) + " byte +" + N(k) + " " + N(b[k]) + " / " + N(a[k]) + "; ";
                break;
            }
        }
        if (PHumanIndex(net.m_players[static_cast<size_t>(i)].human) != OHumanIndex(OF<void *>(b, 0x3C)))
            s += "slot " + N(i) + " human; ";
        if (!g_dangling.count(i) && LinkSerial(0, net.m_players[static_cast<size_t>(i)].link) != LinkSerial(1, OF<void *>(b, 0x6C)))
            s += "slot " + N(i) + " link " + N(LinkSerial(1, OF<void *>(b, 0x6C))) + " / " +
                 N(LinkSerial(0, net.m_players[static_cast<size_t>(i)].link)) + "; ";
    }
    if (net.m_unknown8 != (OF<uint8_t>(ONet(), 8) != 0))
        s += "+8; ";
    if (net.m_lastError != OF<long>(ONet(), 0x504C) ||
        std::string(net.m_lastErrorText ? net.m_lastErrorText : "-") != (OF<const char *>(ONet(), 0x5050) ? OF<const char *>(ONet(), 0x5050) : "-"))
        s += "the last error; ";
    for (int i = 0; i < 100; ++i)
        if (net.m_keyQueries[i].state != OF<int>(ONet(), 0x55C + 0xB8 * i + 0xAC)) {
            s += "key check " + N(i) + "; ";
            break;
        }
    for (int k = 0; k < 5; ++k) {
        C_NetMessage *p = PStream(k);
        void *o = OStream(k);
        if (p->BitPosition() != OPosition(o) || p->m_unreliable != (OF<uint8_t>(o, 0x18) != 0) || p->m_pending != OF<int>(o, 0x1C) ||
            p->m_channel != OF<int>(o, 0x20) || p->m_target != OF<int>(o, 0x24) || p->m_type != OF<uint32_t>(o, 0x28) ||
            std::memcmp(g_pBuf[k], g_oBuf[k], (p->BitPosition() + 31) / 32 * 4) != 0)
            s += "stream " + N(k) + " (" + StreamBits(g_oBuf[k], OPosition(o)) + " / " + StreamBits(g_pBuf[k], p->BitPosition()) + "); ";
    }
    Check(s.empty(), s);
}

template <class A, class B> void Run(const std::string &what, A port, B orig)
{
    g_line = what;
    g_trace += " " + what;
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

// ---- the soldier states written and read ----
void RunSoldierState()
{
    const S_StateModel m = RandomState();
    const bool itemsOnly = P(0.3);
    S_SoldierState ps;
    ToPort(m, ps);
    void *os = ToOrig(m);
    static uint32_t pb[WORDS], ob[WORDS];
    std::memset(pb, 0, sizeof pb);
    std::memset(ob, 0, sizeof ob);
    alignas(16) uint8_t pms[sizeof(C_NetMessage)];
    C_NetMessage *pm = new (pms) C_NetMessage();
    pm->Attach(pb, WORDS * 32);
    alignas(16) uint8_t oms[0x40];
    reinterpret_cast<void *(THISCALL *)(void *)>(O_MSG_CTOR)(oms);
    OF<uint32_t *>(oms, 4) = ob;
    OF<uint32_t>(oms, 0x10) = WORDS * 32;
    Run("NetWriteSoldierState " + N(itemsOnly), [&] { NetWriteSoldierState(pm, ps, itemsOnly); return 0; },
        [&] { reinterpret_cast<void(FAST *)(void *, void *, bool)>(O_WRITE_STATE)(oms, os, itemsOnly); return 0; });
    Check(pm->BitPosition() == OPosition(oms) && std::memcmp(pb, ob, sizeof pb) == 0, "the state's bits");
    Check(PortStateText(ps) == OrigStateText(os), "the state written: " + OrigStateText(os) + "\n     / " + PortStateText(ps));
    // Read back into another state (its vectors may already hold items: read ones are added).
    const S_StateModel m2 = RandomState();
    S_SoldierState ps2;
    ToPort(m2, ps2);
    void *os2 = ToOrig(m2);
    pm->ResetPosition();
    OF<uint32_t>(oms, 8) = 0;
    OF<uint32_t>(oms, 0xC) = 0;
    const bool itemsOnly2 = P(0.85) ? itemsOnly : !itemsOnly;
    Run("NetReadSoldierState " + N(itemsOnly2), [&] { NetReadSoldierState(pm, ps2, itemsOnly2); return 0; },
        [&] { reinterpret_cast<void(FAST *)(void *, void *, bool)>(O_READ_STATE)(oms, os2, itemsOnly2); return 0; });
    Check(pm->BitPosition() == OPosition(oms), "the bits read");
    Check(PortStateText(ps2) == OrigStateText(os2), "the state read: " + OrigStateText(os2) + "\n     / " + PortStateText(ps2));
    pm->Attach(nullptr, 0);
    pm->~C_NetMessage();
    OF<uint32_t *>(oms, 4) = nullptr;
    FreeOrigState(os);
    FreeOrigState(os2);
}

void RunBegin()
{
    const int kind = I(0, 2);
    const int to = P(0.5) ? 0 : I(1, 100), id = I(0, 0x30);
    const bool reliable = P(0.5);
    if (P(0.3))
        for (int k = 0; k < 5; ++k)
            ResetStream(k);
    Run("Begin " + N(kind) + " " + N(to) + " " + N(id) + " " + N(reliable),
        [&] {
            C_BitStream *s = kind == 0 ? PNet().BeginGameMessage(to, id, reliable) : kind == 1 ? PNet().BeginCommand(id) : PNet().BeginReply(to, id);
            return static_cast<long long>(PStreamIndex(s));
        },
        [&] {
            void *s = kind == 0 ? reinterpret_cast<void *(THISCALL *)(void *, int, int, bool)>(O_BEGIN_GAME)(ONet(), to, id, reliable)
                      : kind == 1 ? reinterpret_cast<void *(THISCALL *)(void *, int)>(O_BEGIN_COMMAND)(ONet(), id)
                                  : reinterpret_cast<void *(THISCALL *)(void *, int, int)>(O_BEGIN_REPLY)(ONet(), to, id);
            return static_cast<long long>(OStreamIndex(s));
        });
}

// ---- the game messages: queued (each side's payload), then sent by a reliable one ----
std::vector<void *> g_pFree, g_oFree;   // (blocks a queued type takes, freed by the send)
struct S_Payload {
    intptr_t p, o;
};
S_Payload MakePayload(int type, int &b)
{
    switch (type) {
    case 3: {
        // A spawn: the place (4 floats), the facing (3), the state, the stats, the slot.
        auto *ps = static_cast<float *>(_aligned_malloc(0x40, 16));
        auto *os = static_cast<float *>(reinterpret_cast<void *(__cdecl *)(size_t, size_t)>(O_ALIGNED_MALLOC)(0x40, 16));
        for (int k = 0; k < 7; ++k)
            ps[k] = os[k] = k < 3 ? R(-100.0f, 100.0f) : k == 3 ? 0.0f : R(-1.0f, 1.0f);
        const S_StateModel m = RandomState();
        auto *state = new S_SoldierState;
        ToPort(m, *state);
        reinterpret_cast<intptr_t *>(ps)[8] = reinterpret_cast<intptr_t>(state);
        reinterpret_cast<intptr_t *>(os)[8] = reinterpret_cast<intptr_t>(ToOrig(m));
        auto *pst = static_cast<int *>(operator new(48));
        auto *ost = static_cast<int *>(ONew(48));
        for (int k = 0; k < 12; ++k)
            pst[k] = ost[k] = I(0, 120);
        reinterpret_cast<intptr_t *>(ps)[9] = reinterpret_cast<intptr_t>(pst);
        reinterpret_cast<intptr_t *>(os)[9] = reinterpret_cast<intptr_t>(ost);
        reinterpret_cast<int *>(ps)[10] = reinterpret_cast<int *>(os)[10] = I(0, 31);
        return {reinterpret_cast<intptr_t>(ps), reinterpret_cast<intptr_t>(os)};
    }
    case 4: {
        auto *p = static_cast<char *>(operator new(0x110));
        auto *o = static_cast<char *>(ONew(0x110));
        std::memset(p, 0, 0x110);
        std::snprintf(p, 0x100, "chat line %d", I(0, 99));
        OF<int>(p, 0x104) = I(-1, 40);
        OF<int>(p, 0x108) = I(0, 3);
        p[0x10C] = static_cast<char>(I(0, 2));
        std::memcpy(o, p, 0x110);
        return {reinterpret_cast<intptr_t>(p), reinterpret_cast<intptr_t>(o)};
    }
    case 9: {
        static char names[8][16];
        char *n = names[I(0, 7)];
        std::snprintf(n, 16, "Newbie%d", I(0, 9));
        return {reinterpret_cast<intptr_t>(n), reinterpret_cast<intptr_t>(n)};
    }
    case 0x15:
        b = I(0, 2);
        return {P(0.8), 0};
    default: {
        const intptr_t v = P(0.5) ? I(0, 40) : static_cast<intptr_t>(rng());
        return {v, v};
    }
    }
}

// Whether a queued type can be sent in this world (what the original reads unchecked: type 5's humans, type 2's
// game).
bool Sendable(int type)
{
    if (type == 2 || type == 0x15)
        return g_script.game;
    if (type == 5) {
        for (const S_NetPlayerState &p : PNet().m_players)
            if (!p.human)
                return false;
    }
    return true;
}

void RunSendGameMessage()
{
    static const int types[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xC, 0xD, 0xE, 0xF, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 1, 0x18};
    for (int k = I(1, 4); k > 0; --k) {
        const int type = types[I(0, static_cast<int>(sizeof types / sizeof types[0]) - 1)];
        if (!Sendable(type))
            continue;
        int b = I(0, 1000);
        const S_Payload pay = type == 0x15 ? S_Payload{P(0.8), 0} : MakePayload(type, b);
        const intptr_t oa = type == 0x15 ? pay.p : pay.o;
        const int to = type == 0x15 ? (P(0.7) ? PNet().m_players[static_cast<size_t>(I(0, g_slots - 1))].connection : I(0, 60))
                                    : (P(0.5) ? 0 : I(1, 60));
        Run("SendGameMessage queued " + N(type) + " " + N(to),
            [&] { GetGameApp()->SendGameMessage(type, to, pay.p, b, false); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, int, intptr_t, int, bool)>(O_SEND_GAME)(g_oApp, type, to, oa, b, false); return 0; });
    }
    if (std::getenv("HD2_MSGS_DEBUG"))
        std::printf("senders %p %p table %p %d\n", static_cast<void *>(PStream(0)->GetSender(-1)), static_cast<void *>(PStream(4)->GetSender(-1)),
                    static_cast<void *>(g_netPlayerTable), g_netPlayerCount);
    // Sent: by a reliable call (its own type not queued).
    Run("SendGameMessage sent", [&] { GetGameApp()->SendGameMessage(0x13, 0, 0, 0, true); return 0; },
        [&] { reinterpret_cast<void(THISCALL *)(void *, int, int, intptr_t, int, bool)>(O_SEND_GAME)(g_oApp, 0x13, 0, 0, 0, true); return 0; });
}

// The squad's first soldier (each side's layout): the posted commands 0 and 1.
S_SoldierState *g_pSoldier;
void *g_oSoldier;
alignas(16) uint8_t g_pSquad[sizeof(S_MissionSquad)];
void SetSquad()
{
    const S_StateModel m = RandomState();
    delete g_pSoldier;
    g_pSoldier = new S_SoldierState;
    ToPort(m, *g_pSoldier);
    if (g_oSoldier)
        FreeOrigState(g_oSoldier);
    g_oSoldier = ToOrig(m);
    auto *squad = reinterpret_cast<S_MissionSquad *>(g_pSquad);
    squad->states.assign(1, reinterpret_cast<S_SquadSoldier *>(g_pSoldier));
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_missionSquad = squad;
    static void *oStates[2];
    oStates[0] = g_oSoldier;
    OF<void **>(g_oSquad, 8) = oStates;
    OF<void **>(g_oSquad, 0xC) = oStates + 1;
    G<void *>(O_PROFILE + 0x14) = g_oSquad;
}

void RunPostCommand()
{
    SetSquad();
    for (int k = I(1, 4); k > 0; --k) {
        const int command = I(0, 11);
        intptr_t pa = I(0, 100), oa = pa;
        const intptr_t b = I(0, 1000);
        if (command == 3 || command == 6 || command == 7 || command == 8) {
            auto *p = static_cast<char *>(operator new(0x110));
            auto *o = static_cast<char *>(ONew(0x110));
            std::memset(p, 0, 0x110);
            std::snprintf(p, 0x100, "text %d", I(0, 999));
            OF<int>(p, 0x104) = I(-1, 40);
            OF<int>(p, 0x108) = I(0, 3);
            p[0x10C] = static_cast<char>(I(0, 1));
            std::memcpy(o, p, 0x110);
            pa = reinterpret_cast<intptr_t>(p);
            oa = reinterpret_cast<intptr_t>(o);
        }
        Run("PostCommand queued " + N(command),
            [&] { GetGameApp()->PostCommand(command, pa, b, false); return 0; },
            [&] { reinterpret_cast<void(THISCALL *)(void *, int, intptr_t, intptr_t, bool)>(O_POST)(g_oApp, command, oa, b, false); return 0; });
    }
    Run("PostCommand sent", [&] { GetGameApp()->PostCommand(0, 0, 0, true); return 0; },
        [&] { reinterpret_cast<void(THISCALL *)(void *, int, intptr_t, intptr_t, bool)>(O_POST)(g_oApp, 0, 0, 0, true); return 0; });
}

// ---- DirectPlay's server messages ----
// (An aborted connection's link is deleted and left in its slot (both programs): a second abort or a destroy of that
// slot deletes it again. The game reuses the slot first: not given.)
bool DanglingLink(const S_NetPlayerState &p)
{
    const int slot = static_cast<int>(&p - PNet().m_players.data());
    return g_dangling.count(slot) || (p.link && LinkSerial(0, p.link) == 999);
}
long CallHandle(uint32_t type, void *portMessage, void *origMessage, const std::string &what)
{
    // (The slot a join or the server's own player takes.)
    const std::vector<S_NetPlayerState> &players = PNet().m_players;
    if (type == 0xFFFF000E) {
        size_t slot = 0;
        while (slot < players.size() && !(players[slot].connection == 0 && !players[slot].left))
            ++slot;
        g_freshSlot = static_cast<int>(slot);
    } else if (type == 0xFFFF0007) {
        g_freshSlot = static_cast<int>(players.size());
    }
    long result[2] = {};
    Run(what, [&] { return result[0] = C_NetGame::HandleServerMessage(&PNet(), nullptr, type, portMessage); },
        [&] {
            return result[1] = reinterpret_cast<long(__stdcall *)(void *, void *, uint32_t, void *)>(O_HANDLE)(ONet(), nullptr, type, origMessage);
        });
    // (A slot the join refreshed has no link again; one it refused before keeps what it had.)
    if (g_freshSlot >= 0 && static_cast<size_t>(g_freshSlot) < PNet().m_players.size() &&
        !PNet().m_players[static_cast<size_t>(g_freshSlot)].link)
        g_dangling.erase(g_freshSlot);
    CarryFreshSlot();
    return result[0];
}

void RunServerMessage()
{
    const int kind = I(0, 9);
    switch (kind) {
    case 0:
    case 1: {
        // A player created (its slot as the context).
        DWORD m[3] = {0xC, static_cast<DWORD>(I(1, 5000)), static_cast<DWORD>(I(0, g_slots - 1))};
        DWORD o[3] = {m[0], m[1], m[2]};
        CallHandle(0xFFFF0007, m, o, "CREATE_PLAYER " + N(m[1]) + " slot " + N(m[2]));
        break;
    }
    case 2: {
        // A player destroyed (one of the players' ids, or another).
        const DWORD id = P(0.8) ? static_cast<DWORD>(PNet().m_players[static_cast<size_t>(I(0, g_slots - 1))].connection)
                                : static_cast<DWORD>(I(0, 5000));
        DWORD m[2] = {8, id}, o[2] = {8, id};
        for (const S_NetPlayerState &p : PNet().m_players)
            if (static_cast<DWORD>(p.connection) == id) {
                if (DanglingLink(p))
                    return;
                break;
            }
        CallHandle(0xFFFF0009, m, o, "DESTROY_PLAYER " + N(id));
        break;
    }
    case 3: {
        // A hosts query (a search's 0, a status query's 1, others).
        int request = I(0, 3);
        uint32_t pm[9] = {0x24, 0, 0, 0, 4, 1000, 0, 0, 0}, om[9];
        pm[3] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&request));
        std::memcpy(om, pm, sizeof pm);
        const long r = CallHandle(0xFFFF000A, pm, om, "ENUM_HOSTS_QUERY " + N(request));
        Check(pm[7] == om[7], "the response's size " + N(om[7]) + " / " + N(pm[7]));
        Check((pm[6] != 0) == (om[6] != 0), "the response handed over");
        if (r == 0 && pm[6] && om[6])
            Check(std::memcmp(reinterpret_cast<void *>(static_cast<uintptr_t>(pm[6])), reinterpret_cast<void *>(static_cast<uintptr_t>(om[6])),
                              pm[7] > 1 ? pm[7] - 1 : 0) == 0,
                  "the response's bytes " + StreamBits(reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(om[6])), (om[7] - 1) * 8) + " / " +
                      StreamBits(reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(pm[6])), (pm[7] - 1) * 8));
        if (pm[6])
            operator delete(reinterpret_cast<void *>(static_cast<uintptr_t>(pm[6])));
        if (om[6])
            ODelete(reinterpret_cast<void *>(static_cast<uintptr_t>(om[6])));
        break;
    }
    case 4:
    case 5:
    case 6: {
        // A connection asked for: the join request (the version, a password, a name).
        S_JoinRequest req = {};
        static const char *const passwords[] = {"", "secret", "pw", "wrong"};
        std::strcpy(req.password, passwords[I(0, 3)]);
        req.versionMajor = P(0.9) ? 1 : I(0, 3);
        req.versionMinor = P(0.9) ? 2 : I(0, 3);
        req.unknown1C = P(0.9) ? 2 : I(0, 2);
        static const char *const names[] = {"Joiner", "Joiner2", "Joiner3", "Other", "Nobody"};
        std::strcpy(req.name, names[I(0, 4)]);
        const bool none = P(0.05);
        uint32_t pm[8] = {0x20, none ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&req)),
                          static_cast<uint32_t>(P(0.95) ? 0x54 : 0x50), 0, 0, 0, 0, static_cast<uint32_t>(I(1, 9999))};
        uint32_t om[8];
        std::memcpy(om, pm, sizeof pm);
        CallHandle(0xFFFF000E, pm, om, "INDICATE_CONNECT " + Str(req.name) + " " + Str(req.password));
        Check(pm[4] == om[4] && pm[6] == om[6], "the reply's size / the context " + N(om[6]) + " / " + N(pm[6]));
        const uint8_t *pr = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(pm[3]));
        const uint8_t *orr = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(om[3]));
        Check(pr && orr && *pr == *orr, "the reply " + N(orr ? *orr : -1) + " / " + N(pr ? *pr : -1));
        operator delete(const_cast<uint8_t *>(pr));
        ODelete(const_cast<uint8_t *>(orr));
        break;
    }
    case 7: {
        // A connection aborted (its slot).
        DWORD m[2] = {8, static_cast<DWORD>(I(0, static_cast<int>(PNet().m_players.size()) - 1))}, o[2] = {m[0], m[1]};
        if (DanglingLink(PNet().m_players[m[1]]))
            return;
        const bool hadLink = PNet().m_players[m[1]].link != nullptr;
        CallHandle(0xFFFF000F, m, o, "INDICATED_CONNECT_ABORTED " + N(m[1]));
        if (hadLink)
            g_dangling.insert(static_cast<int>(m[1]));
        break;
    }
    case 8: {
        // Data received; a buffer returned; the server's info; another message.
        const int which = I(0, 3);
        if (which == 0) {
            DWORD m[5] = {0x14, static_cast<DWORD>(I(1, 99)), static_cast<DWORD>(I(0, 31)), static_cast<DWORD>(I(1, 99999)),
                          static_cast<DWORD>(P(0.2) ? 0 : I(1, 900))};
            DWORD o[5];
            std::memcpy(o, m, sizeof m);
            CallHandle(0xFFFF0011, m, o, "RECEIVE");
        } else if (which == 1) {
            void *pb = operator new(16);
            void *ob = ONew(16);
            DWORD m[4] = {0x10, 0, static_cast<DWORD>(reinterpret_cast<uintptr_t>(pb)), 0};
            DWORD o[4] = {0x10, 0, static_cast<DWORD>(reinterpret_cast<uintptr_t>(ob)), 0};
            CallHandle(0xFFFF0013, m, o, "RETURN_BUFFER");
        } else if (which == 2) {
            DWORD m[2] = {8, static_cast<DWORD>(I(1, 9999))}, o[2] = {m[0], m[1]};
            CallHandle(0xFFFF0015, m, o, "SERVER_INFO");
        } else {
            static const uint32_t others[] = {0xFFFF0001, 0xFFFF0002, 0xFFFF0003, 0xFFFF0016, 0x12345678};
            DWORD m[4] = {0x10, 1, 2, 3}, o[4] = {0x10, 1, 2, 3};
            const uint32_t type = others[I(0, 4)];
            CallHandle(type, m, o, "other " + Hex(type));
        }
        break;
    }
    default:
        RunSendGameMessage();
        break;
    }
}

void Call()
{
    switch (I(0, 9)) {
    case 0:
    case 1:
        RunSoldierState();
        break;
    case 2:
        RunBegin();
        break;
    case 3:
    case 4:
        RunSendGameMessage();
        break;
    case 5:
        RunPostCommand();
        break;
    default:
        RunServerMessage();
        break;
    }
}

void Round()
{
    g_trace.clear();
    SetWorld();
    for (int k = 0; k < 5; ++k)
        ResetStream(k);
    CompareState();
    for (int k = I(1, 8); k > 0; --k)
        Call();
    // (The queues flushed: nothing carried to the next world.)
    RunSendGameMessage();
    SetSquad();
    Run("PostCommand flush", [&] { GetGameApp()->PostCommand(0, 0, 0, true); return 0; },
        [&] { reinterpret_cast<void(THISCALL *)(void *, int, intptr_t, intptr_t, bool)>(O_POST)(g_oApp, 0, 0, 0, true); return 0; });
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *e)
{
    std::printf("CRASH %08lx at %p (the test's 0x20000000 base: %08x) in round %d, %s, %s's side\n   (ops:%s)\n",
                e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress) -
                                      reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) + 0x20000000u),
                g_round, g_line.c_str(), g_side ? "the original" : "the port", g_trace.c_str());
    const auto *sp = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(e->ContextRecord->Esp));
    std::printf("  ebx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx |", e->ContextRecord->Ebx, e->ContextRecord->Esi, e->ContextRecord->Edi, e->ContextRecord->Ebp, e->ContextRecord->Esp);
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

// (The port's: the stream sent logged as the original's fake logs it, then emptied.)
void C_NetGame::SendNetMessage(C_BitStream *stream)
{
    auto *msg = static_cast<C_NetMessage *>(stream);
    const int k = PStreamIndex(msg);
    C_NetGameSendLog(k, msg->m_unreliable, msg->m_pending, msg->m_channel, msg->m_target, msg->m_type, msg->Data(), msg->BitPosition());
    if (k >= 0) {
        std::memset(g_pBuf[k], 0, sizeof g_pBuf[k]);
        msg->ResetPosition();
        msg->m_pending = 0;
    }
}
void I3D_frame::UpdateWMatrixProc() { Log("UpdateWMatrixProc"); }

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_MSGS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("msgs_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);

    // The port's objects: the net game's containers and streams made (its storage is raw), the vtables' fakes.
    C_NetGame &net = PNet();
    new (&net.m_players) std::vector<S_NetPlayerState>();
    for (int k = 0; k < 3; ++k)
        new (&net.m_messages[k]) C_NetMessage();
    new (&net.m_unreliable) C_NetMessage();
    new (&net.m_direct) C_NetMessage();
    net.m_transport = reinterpret_cast<C_NetTransport *>(g_pTransport);
    net.m_server = reinterpret_cast<IDirectPlay8Server *>(&g_serverObject);
    g_serverVtable[0x24 / 4] = reinterpret_cast<void *>(&FakeGetClientInfo);
    reinterpret_cast<C_GameProfile *>(&g_gameProfile)->m_mpSettings = reinterpret_cast<S_MultiplayerSettings *>(g_mp);
    new (&reinterpret_cast<C_ActorsModule *>(&g_actorsModule)->m_actors) std::vector<C_Actor *>();
    new (&reinterpret_cast<S_MissionSquad *>(g_pSquad)->states) std::vector<S_SquadSoldier *>();
    new (&reinterpret_cast<C_MpMap *>(g_pMap)->name) C_str("Map_One");
    InitializeCriticalSection(&GetGameApp()->m_commandLock);
    g_watched[0][0] = nullptr;
    g_watched[0][1] = &GetGameApp()->m_commandLock;
    const int item = VSlot(&C_ItemContainer::GetItem);
    if (item < 0) {
        std::printf("msgs_diff: a virtual's slot not found\n");
        return 1;
    }
    g_pContainerVtable[item] = reinterpret_cast<void *>(&PGetItem);
    for (int i = 0; i < HUMANS; ++i) {
        OF<void *>(g_pContainers[i], 0) = g_pContainerVtable;
        for (int k = 0; k < 8; ++k)
            g_pContainerTable[i][k] = reinterpret_cast<C_ItemContainer *>(g_pContainers[i]);
        OF<void *>(g_oContainers[i], 0) = g_oContainerVtable;
        for (int k = 0; k < 8; ++k)
            g_oContainerTable[i][k] = g_oContainers[i];
    }
    g_oContainerVtable[0x40 / 4] = reinterpret_cast<void *>(&OGetItem);
    g_oLinkVtable[0] = reinterpret_cast<void *>(&OLinkDelete);
    // The quantisation the messages' place is written with (the player table's first entry, both programs').
    // (Entry 0 the places, the senders'; entry 1 the angles, the recipients': a direction's heading and pitch.)
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
    RedirectOriginal(0x00724c20, reinterpret_cast<const void *>(OFindFreeNameNumber));
    RedirectOriginal(0x00730410, reinterpret_cast<const void *>(OCountPlayers));
    RedirectOriginal(0x00730be0, reinterpret_cast<const void *>(OFillServerStatus));
    RedirectOriginal(0x007263c0, reinterpret_cast<const void *>(OStatusWrite));
    RedirectOriginal(0x0072c540, reinterpret_cast<const void *>(OQueueReceived));
    RedirectOriginal(0x00731990, reinterpret_cast<const void *>(ODropKeyQuery));
    RedirectOriginal(0x0072f060, reinterpret_cast<const void *>(OFindPlayer));
    RedirectOriginal(0x0072efa0, reinterpret_cast<const void *>(OGetPlayerName));
    RedirectOriginal(0x00730510, reinterpret_cast<const void *>(OSetPlayerTeam));
    RedirectOriginal(0x0072c9a0, reinterpret_cast<const void *>(OProcess));
    RedirectOriginal(0x0072c360, reinterpret_cast<const void *>(OSendNetMessage));
    RedirectOriginal(0x0072fc50, reinterpret_cast<const void *>(OWriteScores));
    RedirectOriginal(0x00749ed0, reinterpret_cast<const void *>(OStatsWrite));
    RedirectOriginal(0x00746fb0, reinterpret_cast<const void *>(OAdd));
    RedirectOriginal(0x00747180, reinterpret_cast<const void *>(ORemove));
    RedirectOriginal(0x00746180, reinterpret_cast<const void *>(OLinkCtor));
    RedirectOriginal(0x00725190, reinterpret_cast<const void *>(OGetHostAddress));
    RedirectOriginal(0x00724bd0, reinterpret_cast<const void *>(OWideToUtf8));
    RedirectOriginal(0x00724b30, reinterpret_cast<const void *>(OWideToAnsi));
    RedirectOriginal(0x0064da80, reinterpret_cast<const void *>(OHasCjk));
    RedirectOriginal(0x006ad830, reinterpret_cast<const void *>(OGetCurrentMap));
    RedirectOriginal(0x006ad990, reinterpret_cast<const void *>(OWriteRules));
    RedirectOriginal(0x006ad840, reinterpret_cast<const void *>(OWriteCurrentMap));
    RedirectOriginal(0x006dcd10, reinterpret_cast<const void *>(OGetGameApp));
    RedirectOriginal(0x006c4e80, reinterpret_cast<const void *>(OAppPrintMessage));
    RedirectOriginal(0x006987d0, reinterpret_cast<const void *>(OGetGame));
    RedirectOriginal(0x0069e9f0, reinterpret_cast<const void *>(OOnPlayerLeft));
    RedirectOriginal(0x00699480, reinterpret_cast<const void *>(OWriteObjectiveStates));
    RedirectOriginal(0x0048db30, reinterpret_cast<const void *>(OBroadcastWriteState));
    RedirectOriginal(0x004782c0, reinterpret_cast<const void *>(OTemporaryWriteState));
    RedirectOriginal(0x006e5380, reinterpret_cast<const void *>(OTextsGet));
    RedirectOriginal(0x00475dd0, reinterpret_cast<const void *>(OGetActor));
    RedirectOriginal(0x005a9040, reinterpret_cast<const void *>(OGetSoldierState));
    G<void *>(O_ENTER_SLOT) = reinterpret_cast<void *>(&FakeEnterCriticalSection);
    G<void *>(O_LEAVE_SLOT) = reinterpret_cast<void *>(&FakeLeaveCriticalSection);
    G<void *>(O_TIME_SLOT) = reinterpret_cast<void *>(&FakeTimeGetTime);
    G<void *>(O_PROFILE + 0x24) = g_mp;
    G<int>(O_VERSION_MAJOR) = g_versionMajor;
    G<int>(O_VERSION_MINOR) = g_versionMinor;
    std::memset(ONet(), 0, 0x5080);
    OF<void *>(ONet(), 0) = &g_serverObject;
    for (int k = 0; k < 5; ++k)
        reinterpret_cast<void *(THISCALL *)(void *)>(O_MSG_CTOR)(OStream(k));
    g_watched[1][0] = reinterpret_cast<CRITICAL_SECTION *>(ONet() + 0x418);
    g_watched[1][1] = reinterpret_cast<CRITICAL_SECTION *>(g_oApp + 0x2C);
    // The map's name (the original's string at +1).
    OF<void *>(g_oMap, 1) = ORep("Map_One");

    int rounds = std::getenv("HD2_MSGS_QUICK") ? 5000 : 20000;
    if (const char *r = std::getenv("HD2_MSGS_ROUNDS"))
        rounds = std::atoi(r);
    uint32_t seed = 1;
    if (const char *s = std::getenv("HD2_MSGS_SEED"))
        seed = static_cast<uint32_t>(std::strtoul(s, nullptr, 0));
    for (g_round = 0; g_round < rounds; ++g_round) {
        rng.seed(seed * 1000003u + static_cast<uint32_t>(g_round));
        Round();
    }
    std::printf("msgs_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
