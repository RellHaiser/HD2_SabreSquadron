// Net part 11a: the network game's core (net/net_game.cpp: the votes, the message streams and queues, the dispatch,
// the frame's sending (Process, the lags, the join data, SendMessages), the commands (kicks, the level scan), the player
// table's basics, Init, Close, Shutdown) against the original executable's. MinGW; build_ng.bat; runs ng_diff.exe
// ("ng_diff: N checks, 0 failures").
//
// The original's net game is its own global made by its constructor and Init (statics never run), the port's the
// real g_netGame. Each round sets both alike (the role, the local slot, the players with the same bytes and each
// side's humans and connections, the send period and clocks, the vote, the rules, the queues) and runs up to 10
// calls. What they reach is the test's on both sides, logging alike: the critical sections, the clocks (timeGetTime,
// clock, the perf clock and timer), the transport (FlushAll, FindById, Lock, Unlock, Stop, DeleteConnections; the
// connections' Send and SetResendTime), the pool, DirectPlay (Cancel, Close, Release, DestroyClient: one COM object
// each), the voice (SetClientConfig, Stop, the deleting destructor), the replication (Write, Read, FindObject,
// DeleteObjects; the objects' SetLag), the game's and the app's calls (the commands handled, the messages and commands
// sent), the modules' dispatch, the console, the command processor, the texts, the scene's EnumFrames (frames with
// bounds), SetLevelBounds, the firewall, IGraph's SetFlags, the GameSpy stop, the key socket, the query thread. After
// each call the result, the logs and the state are compared. HD2_NG_ROUNDS: rounds (default 20,000; HD2_NG_QUICK:
// 5,000); HD2_NG_SEED; HD2_NG_FIRST_CHANCE.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#include <winsock2.h>
#include <windows.h>

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
#include "game/console.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/module.h"
#include "game/pending.h"
#include "game/perf_timer.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "net/dplay8.h"
#include "net/net_channel.h"
#include "net/net_game.h"
#include "net/net_object.h"
#include "net/net_socket.h"
#include "net/net_transport.h"
#undef private
#undef protected

#include "original_exe.h"

// (The renames of ng_fakes.h undone here: the fakes below call the real functions by these names.)
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef InitializeCriticalSection
#undef DeleteCriticalSection
#undef timeGetTime
#undef closesocket
#undef WaitForSingleObject
#undef CloseHandle
#undef clock

#ifdef SendMessage
#undef SendMessage
#endif
#ifdef DispatchMessage
#undef DispatchMessage
#endif

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

static_assert(sizeof(S_NetPlayerState) == 0xC4, "the player state's size");

// The port's allocations zeroed, as the original's (the test's calloc).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }

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
std::string Bytes(const void *data, int size)
{
    std::string s = N(size) + ":";
    const auto *b = static_cast<const uint8_t *>(data);
    for (int i = 0; i < size && i < 200; ++i) {
        char t[4];
        std::snprintf(t, sizeof t, "%02x", b[i]);
        s += t;
    }
    return s;
}

// ---- the round's script ----
constexpr int HUMANS = 8;
struct S_Script {
    bool game = true;
    bool missionLoaded = true;
    uint32_t now = 0;                // timeGetTime
    int clockNow = 0;                // clock
    int64_t perfNow = 0;             // GetPerfTimeMs
    bool timerFires = false;         // the lags' timer
    int playerCount = 0;             // CountPlayers
    bool dispatch = true;            // ModuleManager's answers
    int figures[7];                  // TakeBandwidth
    std::vector<std::string> console;   // PopCommand
    bool replicationWrote = false;   // the replication's Write
    std::map<int, bool> objects;     // FindObject: id -> found
    int linkById = -1;               // FindById: the connection (-1: none) for any id
    bool spectating[HUMANS];
    long destroyResult = 0;
    std::vector<float> bounds;       // the scene's frames: 6 floats each
    bool voiceChat = false;
} g_script;

// The connections (each side's raw objects, by index).
constexpr int LINKS = 14;
int g_linkPort[LINKS], g_linkRoundTrip[LINKS], g_linkLastReceive[LINKS];

// ---- the fakes shared by both sides (C calls) ----
CRITICAL_SECTION *g_watched[2][2];   // [side][0 players, 1 queue]
std::string Watched(void *cs)
{
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 2; ++k)
            if (cs == g_watched[s][k])
                return k ? "queue" : "players";
    return "";
}
extern "C" void WINAPI FakeEnterCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Enter " + w);
    else
        ::EnterCriticalSection(section);
}
extern "C" void WINAPI FakeLeaveCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Leave " + w);
    else
        ::LeaveCriticalSection(section);
}
extern "C" void WINAPI FakeInitializeCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Initialize " + w);
    else
        ::InitializeCriticalSection(section);
}
extern "C" void WINAPI FakeDeleteCriticalSection(LPCRITICAL_SECTION section)
{
    const std::string w = Watched(section);
    if (!w.empty())
        Log("Delete " + w);
    else
        ::DeleteCriticalSection(section);
}
extern "C" DWORD WINAPI FakeTimeGetTime()
{
    Log("timeGetTime");
    return g_script.now;
}
extern "C" int WSAAPI FakeClosesocket(SOCKET s)
{
    Log("closesocket " + N(static_cast<long long>(s)));
    return 0;
}
HANDLE g_queryThread = reinterpret_cast<HANDLE>(0x7777);
extern "C" DWORD WINAPI FakeWaitForSingleObject(HANDLE handle, DWORD ms)
{
    if (handle != g_queryThread)
        return ::WaitForSingleObject(handle, ms);
    Log("WaitForSingleObject " + N(ms));
    return 0;
}
extern "C" BOOL WINAPI FakeCloseHandle(HANDLE handle)
{
    if (handle != g_queryThread)
        return ::CloseHandle(handle);
    Log("CloseHandle");
    return TRUE;
}
extern "C" clock_t FakeClock()
{
    Log("clock");
    return g_script.clockNow;
}
// DirectPlay's server and client (one COM object each, both sides': stdcall).
HRESULT __stdcall FakeServerCancel(void *, DWORD handle, DWORD flags)
{
    Log("server Cancel " + N(handle) + " " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeServerClose(void *, DWORD flags)
{
    Log("server Close " + N(flags));
    return 0;
}
ULONG __stdcall FakeServerRelease(void *)
{
    Log("server Release");
    return 0;
}
HRESULT __stdcall FakeDestroyClient(void *, DWORD player, const void *data, DWORD size, DWORD flags)
{
    Log("DestroyClient " + N(player) + " " + N(data != nullptr) + " " + N(size) + " " + N(flags));
    return g_script.destroyResult;
}
HRESULT __stdcall FakeClientCancel(void *, DWORD handle, DWORD flags)
{
    Log("client Cancel " + N(handle) + " " + Hex(flags));
    return 0;
}
HRESULT __stdcall FakeClientClose(void *, DWORD flags)
{
    Log("client Close " + N(flags));
    return 0;
}
ULONG __stdcall FakeClientRelease(void *)
{
    Log("client Release");
    return 0;
}
void *g_serverVtable[64], *g_clientVtable[64];
void *g_serverObject = g_serverVtable, *g_clientObject = g_clientVtable;
// IGraph (SetFlags at +0x08).
void __stdcall FakeSetFlags(void *, uint32_t flags) { Log("IGraph SetFlags " + Hex(flags)); }
void *g_graphVtable[64];
void *g_graphObject = g_graphVtable;
// The scene's sector: EnumFrames (+0x44) calls back for the test's frames (both programs read their bounds).
alignas(16) unsigned char g_frames[16][0x200];
uint32_t __stdcall FakeEnumFrames(void *, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *mask)
{
    Log("EnumFrames " + N(context != nullptr) + " " + Hex(typeMask) + " " + Str(mask));
    const int n = static_cast<int>(g_script.bounds.size() / 6);
    for (int i = 0; i < n; ++i) {
        float *box = reinterpret_cast<float *>(g_frames[i] + 0x10);
        box[0] = g_script.bounds[6 * i];
        box[1] = g_script.bounds[6 * i + 1];
        box[2] = g_script.bounds[6 * i + 2];
        box[4] = g_script.bounds[6 * i + 3];
        box[5] = g_script.bounds[6 * i + 4];
        box[6] = g_script.bounds[6 * i + 5];
        *reinterpret_cast<uint32_t *>(g_frames[i] + 0xE0) = 0x100;   // (the bound valid)
        callback(reinterpret_cast<I3D_frame *>(g_frames[i]), context);
    }
    return 0;
}
void *g_sectorVtable[64];
void *g_sectorObject = g_sectorVtable;
alignas(16) unsigned char g_scene[0x400];

// ================================================================================================================
// The port's world.
// ================================================================================================================
uint32_t g_gameRole;
int g_localPlayerSlot;
char g_votableCommands[4][0x204] = {"maxclients", "kick", "map", "timelimit"};
void *g_netQueryThread;
char g_netDefaultSessionText[] = "";   // (the original's 0x00867a90: empty)
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;
unsigned g_graphFlags;
int g_netMaxFreq, g_netMaxPing, g_netMaxInactivity;

C_NetGame &PNet() { return g_netGame; }

// The transport (the port's own object: its constructor's members; its calls logged).
C_NetSocket::C_NetSocket() = default;
C_NetSocket::~C_NetSocket() = default;
C_NetTransport::C_NetTransport() = default;
C_NetTransport::~C_NetTransport() = default;
alignas(16) unsigned char g_pLinks[LINKS][sizeof(C_NetConnection)];
int PLinkIndex(const void *p)
{
    for (int i = 0; i < LINKS; ++i)
        if (p == g_pLinks[i])
            return i;
    return p ? 99 : -1;
}
void C_NetTransport::Stop() { Log("Transport Stop"); }
void C_NetTransport::DeleteConnections() { Log("Transport DeleteConnections"); }
C_NetConnection *C_NetTransport::FindById(int id)
{
    Log("FindById " + N(id));
    return g_script.linkById < 0 ? nullptr : reinterpret_cast<C_NetConnection *>(g_pLinks[g_script.linkById]);
}
C_NetTransport *C_NetTransport::Lock()
{
    Log("Transport Lock");
    return this;
}
void C_NetTransport::Unlock() { Log("Transport Unlock"); }
void C_NetTransport::FlushAll(bool force) { Log("FlushAll " + N(force)); }
C_NetPacket *C_NetConnection::Send(const void *data, int size, int kind, int channel)
{
    Log("Send " + N(PLinkIndex(this)) + " " + Bytes(data, size) + " " + N(kind) + " " + N(channel));
    return nullptr;
}
void C_NetConnection::SetResendTime(int ms) { Log("SetResendTime " + N(PLinkIndex(this)) + " " + N(ms)); }
// The pool (its buffers the test's: logged by size).
void *C_NetPool::AllocBuffer(size_t size)
{
    Log("AllocBuffer " + N(static_cast<long long>(size)));
    return std::calloc(size + 4, 1);
}
void C_NetPool::FreeBuffer(void *buffer, int size)
{
    Log("FreeBuffer " + N(size));
    std::free(buffer);
}
C_NetPool g_netPool;
C_NetPool::C_NetPool() = default;
C_NetPool::~C_NetPool() = default;
// The clocks.
int64_t GetPerfTimeMs()
{
    Log("GetPerfTimeMs");
    return g_script.perfNow;
}
void InitPerfTime() { Log("InitPerfTime"); }
S_PerfTimer::S_PerfTimer(int periodMs) : period(periodMs) { Log("S_PerfTimer " + N(periodMs)); }
bool S_PerfTimer::Fire()
{
    Log("Fire " + N(running));
    return g_script.timerFires;
}
// The replication (the port's raw object: its virtual Write and Read the test's).
alignas(16) unsigned char g_pReplication[sizeof(C_NetReplication)];
alignas(16) unsigned char g_pObjects[4][sizeof(C_NetObject)];
void *g_pReplicationVtable[0x40];
C_NetReplication *g_netReplication;
bool __attribute__((thiscall)) PRepWrite(void *, C_NetMessage *stream, int player, bool fromClient)
{
    Log("Replication Write " + N(player) + " " + N(fromClient));
    stream->WriteBits(0x2D, 6);
    return g_script.replicationWrote;
}
void __attribute__((thiscall)) PRepRead(void *, C_NetMessage *stream, uint32_t players, bool fromClient)
{
    Log("Replication Read " + Hex(players) + " " + N(fromClient) + " " + N(stream->CapacityBits()) + " " + Hex(stream->ReadBits(8)));
}
C_NetObject *C_NetReplication::FindObject(int id, bool create)
{
    Log("FindObject " + N(id) + " " + N(create));
    const auto it = g_script.objects.find(id);
    return it != g_script.objects.end() && it->second ? reinterpret_cast<C_NetObject *>(g_pObjects[id & 3]) : nullptr;
}
void C_NetReplication::DeleteObjects() { Log("DeleteObjects"); }
void C_NetObject::SetLag(float lag)
{
    const auto *at = reinterpret_cast<unsigned char *>(this);
    const bool local = at >= g_pReplication && at < g_pReplication + sizeof g_pReplication;   // (its local object)
    // (No replication: the original's local object is read through null, the port's too.)
    const int i = reinterpret_cast<uintptr_t>(at) < 0x10000 ? 99 : local ? 9 : static_cast<int>((at - g_pObjects[0]) / sizeof(C_NetObject));
    Log("SetLag " + N(i) + " " + F(lag));
}
// The app, the game, the modules, the console, the processor, the texts.
alignas(16) unsigned char g_pApp[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_pApp); }
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    std::string extra = type == 9 ? " " + Str(reinterpret_cast<const char *>(a)) : "";
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(type == 9 ? 0 : a) + " " + N(b) + " " + N(reliable) + extra);
}
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    if (command == 3) {
        const char *line = reinterpret_cast<const char *>(a);
        Log("PostCommand 3 " + Str(line) + " " + N(*reinterpret_cast<const int *>(line + 0x104)) + " " + N(line[0x10C]) + " " + N(b) + " " +
            N(flush));
        std::free(const_cast<char *>(line));
        return;
    }
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
void C_GameApp::HandleClientCommand(int id, C_BitStream *stream) { Log("HandleClientCommand " + N(id) + " " + Hex(stream->ReadBits(8))); }
void C_GameApp::HandleServerCommand(int id, C_BitStream *stream) { Log("HandleServerCommand " + N(id) + " " + Hex(stream->ReadBits(8))); }
alignas(16) unsigned char g_pGame[sizeof(C_game)];
C_game *GetGame() { return g_script.game ? reinterpret_cast<C_game *>(g_pGame) : nullptr; }
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color.x) + " " + F(color.y) + " " + F(color.z) + " " + VFormat(format, args));
    va_end(args);
}
namespace ModuleManager {
bool DispatchMessage(int moduleId, C_BitStream *msg)
{
    Log("DispatchMessage " + N(moduleId) + " " + Hex(msg->ReadBits(8)));
    return g_script.dispatch;
}
bool CallUnknown28(int moduleId, C_BitStream *msg)
{
    Log("CallUnknown28 " + N(moduleId) + " " + Hex(msg->ReadBits(8)));
    return g_script.dispatch;
}
}  // namespace ModuleManager
alignas(16) unsigned char g_pConsole[sizeof(C_Console)];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_pConsole); }
char *C_Console::PopCommand()
{
    Log("PopCommand");
    static char command[64];
    if (g_script.console.empty())
        return nullptr;
    std::strcpy(command, g_script.console.front().c_str());
    g_script.console.erase(g_script.console.begin());
    return command;
}
alignas(16) unsigned char g_pProcessor[0x10];
C_CommandProcessor *GetCommandProcessor() { return reinterpret_cast<C_CommandProcessor *>(g_pProcessor); }
bool C_CommandProcessor::Execute(const char *command)
{
    Log("Execute " + Str(command));
    return true;
}
C_str C_GameTexts::Get(int id) const
{
    Log("texts " + N(id));
    char text[32];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}
C_PlayerOptions g_options;
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return &g_options; }
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graphObject); }
// The net game's other files' calls.
int C_NetGame::CountPlayers()
{
    Log("CountPlayers");
    return g_script.playerCount;
}
void C_NetGame::KickPlayer(int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void C_NetGame::SendPlayerScore(int slot, bool lock) { Log("SendPlayerScore " + N(slot) + " " + N(lock)); }
void C_NetGame::SetLevelBounds(const float *min, const float *size)
{
    Log("SetLevelBounds " + F(min[0]) + " " + F(min[1]) + " " + F(min[2]) + " " + F(size[0]) + " " + F(size[1]) + " " + F(size[2]));
}
void C_NetGame::TakeBandwidth(int *figures)
{
    Log("TakeBandwidth");
    std::memcpy(figures, g_script.figures, sizeof g_script.figures);
}
void C_NetGame::AnswerQueries() { Log("AnswerQueries"); }
void C_NetGame::ConfigureFirewall(int port, bool open) { Log("ConfigureFirewall " + N(port) + " " + N(open)); }
void C_NetGame::StopVoiceClient() { Log("StopVoiceClient"); }
void C_NetGame::StopVoiceServer() { Log("StopVoiceServer"); }
void C_NetGame::DeleteReplication() { Log("DeleteReplication"); }
void StopGameSpyQueries(uintptr_t *socket) { Log("StopGameSpyQueries " + N(socket != nullptr)); }
void S_ServerEntry::Reset()
{
    Log("S_ServerEntry::Reset");
    std::memset(this, 0x5A, sizeof *this);
}
// The voice (its objects the test's: SetClientConfig and the deleting destructor logged).
alignas(16) unsigned char g_pVoices[2][sizeof(C_NetVoice)];
long C_NetVoice::SetClientConfig(void *config)
{
    const auto *c = static_cast<const uint32_t *>(config);
    Log("SetClientConfig " + N(this == reinterpret_cast<C_NetVoice *>(g_pVoices[0])) + " " + Hex(c[0]) + " " + Hex(c[1]) + " " + Hex(c[2]) + " " +
        Hex(c[3]) + " " + Hex(c[4]) + " " + Hex(c[5]) + " " + Hex(c[6]) + " " + Hex(c[7]));
    return 0;
}
C_NetVoice::~C_NetVoice() { Log("~C_NetVoice " + N(this == reinterpret_cast<C_NetVoice *>(g_pVoices[0]))); }
C_NetVoice::C_NetVoice(NetVoiceHandler clientHandler, NetVoiceHandler serverHandler)
    : m_clientHandler(clientHandler), m_serverHandler(serverHandler)
{
}
// The unidentified object's Destroy (+0x0C).
void __attribute__((thiscall)) PDestroy(void *, bool free) { Log("Destroy " + N(free)); }
void *g_pUnknownVtable[8];
alignas(16) unsigned char g_pUnknown[0x10];
// The humans (their controllers' IsSpectating).
alignas(16) unsigned char g_pHumans[HUMANS][sizeof(C_Human)];
alignas(16) unsigned char g_pControllers[HUMANS][sizeof(C_PlayerController)];
void *g_pControllerVtable[0x80];
C_Human *PHuman(int i) { return i < 0 ? nullptr : reinterpret_cast<C_Human *>(g_pHumans[i]); }
int PHumanIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_pHumans[i])
            return i;
    return p ? 99 : -1;
}
bool __attribute__((thiscall)) PIsSpectating(void *self)
{
    const int i = static_cast<int>((static_cast<unsigned char *>(self) - g_pControllers[0]) / sizeof(C_PlayerController));
    Log("IsSpectating " + N(i));
    return g_script.spectating[i];
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
const uint32_t O_REPLICATION = 0x0097d084, O_QUERY_THREAD = 0x009820f8, O_GRAPH_FLAGS = 0x00981e6c;
const uint32_t O_MAX_PING = 0x009820d4, O_MAX_FREQ = 0x009820d8, O_MAX_INACTIVITY = 0x009820dc;
const uint32_t O_SCAN_MIN = 0x0097ca30, O_SCAN_MAX = 0x0097ca40, O_VOTABLE = 0x00846088;
const uint32_t O_FSTR = 0x00405100, O_MSG_CTOR = 0x0073d740;

volatile uintptr_t g_oNetAddress = O_NET;
uint8_t *ONet() { return reinterpret_cast<uint8_t *>(g_oNetAddress); }
uint8_t *OPlayers() { return OF<uint8_t *>(ONet(), 0x1E0); }
int OPlayerCount() { return OPlayers() ? static_cast<int>((OF<uint8_t *>(ONet(), 0x1E4) - OPlayers()) / 0xC4) : 0; }

alignas(16) uint8_t g_oGame[0x200], g_oApp[0x100], g_oConsole[0x100], g_oProcessor[0x10], g_oLinks[LINKS][0x80];
alignas(16) uint8_t g_oReplication[0x100], g_oObjects[4][0x80], g_oVoices[2][0x20], g_oUnknown[0x10];
alignas(16) uint8_t g_oHumans[HUMANS][0x400], g_oControllers[HUMANS][0x400];
void *g_oReplicationVtable[0x10], *g_oVoiceVtable[4], *g_oUnknownVtable[8], *g_oControllerVtable[0x40];
int OLinkIndex(const void *p)
{
    for (int i = 0; i < LINKS; ++i)
        if (p == g_oLinks[i])
            return i;
    return p ? 99 : -1;
}
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

void FAST OTransportStop(void *) { Log("Transport Stop"); }
void FAST OTransportDeleteConnections(void *) { Log("Transport DeleteConnections"); }
void *THISCALL OFindById(void *, int id)
{
    Log("FindById " + N(id));
    return g_script.linkById < 0 ? nullptr : g_oLinks[g_script.linkById];
}
void *FAST OTransportLock(void *self)
{
    Log("Transport Lock");
    return self;
}
void FAST OTransportUnlock(void *) { Log("Transport Unlock"); }
void THISCALL OFlushAll(void *, bool force) { Log("FlushAll " + N(force)); }
void *THISCALL OSend(void *self, const void *data, int size, int kind, int channel)
{
    Log("Send " + N(OLinkIndex(self)) + " " + Bytes(data, size) + " " + N(kind) + " " + N(channel));
    return nullptr;
}
void THISCALL OSetResendTime(void *self, int ms) { Log("SetResendTime " + N(OLinkIndex(self)) + " " + N(ms)); }
void *THISCALL OAllocBuffer(void *, size_t size)
{
    Log("AllocBuffer " + N(static_cast<long long>(size)));
    return std::calloc(size + 4, 1);
}
void THISCALL OFreeBuffer(void *, void *buffer, int size)
{
    Log("FreeBuffer " + N(size));
    std::free(buffer);
}
long long OGetPerfTimeMs()
{
    Log("GetPerfTimeMs");
    return g_script.perfNow;
}
void OInitPerfTime() { Log("InitPerfTime"); }
void *FAST OServerEntryReset(void *self)
{
    Log("S_ServerEntry::Reset");
    std::memset(self, 0x5A, 0x9C);
    return self;
}
// The original's MSVC6 strings (+4 the characters, a reference count before them; +8 the length; +0xC the room).
const uint32_t O_TRANSPORT_CALLBACK = 0x0072c730;
std::string OGetString(int at)
{
    const char *p = OF<const char *>(ONet(), at + 4);
    return p ? std::string(p, OF<size_t>(ONet(), at + 8)) : std::string();
}
void OSetString(int at, const std::string &text)
{
    char *&p = OF<char *>(ONet(), at + 4);
    if (p && p[-1] == 0)
        ODelete(p - 1);
    p = nullptr;
    OF<size_t>(ONet(), at + 8) = 0;
    OF<size_t>(ONet(), at + 0xC) = 0;
    if (text.empty())
        return;
    auto *buffer = static_cast<char *>(ONew(text.size() + 2));
    p = buffer + 1;
    std::memcpy(p, text.c_str(), text.size() + 1);
    OF<size_t>(ONet(), at + 8) = text.size();
    OF<size_t>(ONet(), at + 0xC) = text.size();
}
void *THISCALL OPerfTimerCtor(void *self, int periodMs)
{
    Log("S_PerfTimer " + N(periodMs));
    OF<int>(self, 4) = periodMs;
    return self;
}
bool THISCALL OPerfTimerFire(void *self)
{
    Log("Fire " + N(OF<uint8_t>(self, 8)));
    return g_script.timerFires;
}
bool THISCALL ORepWrite(void *, void *stream, int player, bool fromClient)
{
    Log("Replication Write " + N(player) + " " + N(fromClient));
    reinterpret_cast<void(THISCALL *)(void *, uint32_t, int)>(0x0073d320)(stream, 0x2D, 6);
    return g_script.replicationWrote;
}
void THISCALL ORepRead(void *, void *stream, uint32_t players, bool fromClient)
{
    Log("Replication Read " + Hex(players) + " " + N(fromClient) + " " + N(OF<uint32_t>(stream, 0x10)) + " " +
        Hex(reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(0x0073d3d0)(stream, 8)));
}
void *THISCALL OFindObject(void *, int id, bool create)
{
    Log("FindObject " + N(id) + " " + N(create));
    const auto it = g_script.objects.find(id);
    return it != g_script.objects.end() && it->second ? g_oObjects[id & 3] : nullptr;
}
void FAST ODeleteObjects(void *) { Log("DeleteObjects"); }
void THISCALL OObjectSetLag(void *self, float lag)
{
    const auto *at = static_cast<uint8_t *>(self);
    const bool local = at >= g_oReplication && at < g_oReplication + sizeof g_oReplication;   // (its local object)
    Log("SetLag " + N(reinterpret_cast<uintptr_t>(at) < 0x10000 ? 99 : local ? 9 : (at - g_oObjects[0]) / 0x80) + " " + F(lag));
}
void *OGetGameApp() { return g_oApp; }
void THISCALL OSendGameMessage(void *, int type, int to, intptr_t a, int b, bool reliable)
{
    std::string extra = type == 9 ? " " + Str(reinterpret_cast<const char *>(a)) : "";
    Log("SendGameMessage " + N(type) + " " + N(to) + " " + N(type == 9 ? 0 : a) + " " + N(b) + " " + N(reliable) + extra);
}
void THISCALL OPostCommand(void *, int command, intptr_t a, intptr_t b, bool flush)
{
    if (command == 3) {
        const char *line = reinterpret_cast<const char *>(a);
        Log("PostCommand 3 " + Str(line) + " " + N(*reinterpret_cast<const int *>(line + 0x104)) + " " + N(line[0x10C]) + " " + N(b) + " " +
            N(flush));
        ODelete(const_cast<char *>(line));
        return;
    }
    Log("PostCommand " + N(command) + " " + N(a) + " " + N(b) + " " + N(flush));
}
const uint32_t O_READ_BITS = 0x0073d3d0;
uint32_t ORead(void *stream, int bits) { return reinterpret_cast<uint32_t(THISCALL *)(void *, int)>(O_READ_BITS)(stream, bits); }
void THISCALL OHandleClientCommand(void *, int id, void *stream) { Log("HandleClientCommand " + N(id) + " " + Hex(ORead(stream, 8))); }
void THISCALL OHandleServerCommand(void *, int id, void *stream) { Log("HandleServerCommand " + N(id) + " " + Hex(ORead(stream, 8))); }
void *OGetGame() { return g_script.game ? g_oGame : nullptr; }
void __cdecl OPrintMessage(void *, const float *color, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Log("PrintMessage " + F(color[0]) + " " + F(color[1]) + " " + F(color[2]) + " " + VFormat(format, args));
    va_end(args);
}
bool FAST ODispatchMessage(int moduleId, void *msg)
{
    Log("DispatchMessage " + N(moduleId) + " " + Hex(ORead(msg, 8)));
    return g_script.dispatch;
}
bool FAST OCallUnknown28(int moduleId, void *msg)
{
    Log("CallUnknown28 " + N(moduleId) + " " + Hex(ORead(msg, 8)));
    return g_script.dispatch;
}
void *OGetConsole() { return g_oConsole; }
char *THISCALL OPopCommand(void *)
{
    Log("PopCommand");
    static char command[64];
    if (g_script.console.empty())
        return nullptr;
    std::strcpy(command, g_script.console.front().c_str());
    g_script.console.erase(g_script.console.begin());
    return command;
}
void *OGetCommandProcessor() { return g_oProcessor; }
bool THISCALL OExecute(void *, const char *command)
{
    Log("Execute " + Str(command));
    return true;
}
void *THISCALL OTextsGet(void *, void *ret, int id)
{
    Log("texts " + N(id));
    OF<void *>(ret, 0) = nullptr;
    reinterpret_cast<void *(__cdecl *)(void *, const char *, ...)>(O_FSTR)(ret, "T%X", id);
    return ret;
}
int FAST OCountPlayers(void *)
{
    Log("CountPlayers");
    return g_script.playerCount;
}
void THISCALL OKickPlayer(void *, int slot, int reason) { Log("KickPlayer " + N(slot) + " " + N(reason)); }
void THISCALL OSendPlayerScore(void *, int slot, bool lock) { Log("SendPlayerScore " + N(slot) + " " + N(lock)); }
void THISCALL OSetLevelBounds(void *, const float *min, const float *size)
{
    Log("SetLevelBounds " + F(min[0]) + " " + F(min[1]) + " " + F(min[2]) + " " + F(size[0]) + " " + F(size[1]) + " " + F(size[2]));
}
void THISCALL OTakeBandwidth(void *, int *figures)
{
    Log("TakeBandwidth");
    std::memcpy(figures, g_script.figures, sizeof g_script.figures);
}
void FAST OAnswerQueries(void *) { Log("AnswerQueries"); }
void THISCALL OConfigureFirewall(void *, int port, bool open) { Log("ConfigureFirewall " + N(port) + " " + N(open)); }
void FAST OStopVoiceClient(void *) { Log("StopVoiceClient"); }
void FAST OStopVoiceServer(void *) { Log("StopVoiceServer"); }
void FAST ODeleteReplication(void *) { Log("DeleteReplication"); }
void FAST OStopGameSpyQueries(uintptr_t *socket) { Log("StopGameSpyQueries " + N(socket != nullptr)); }
void *OGetIGraph() { return &g_graphObject; }
// The profile's detail settings (the voice chat at +0x38).
alignas(16) uint8_t g_oOptions[0x100];
void *THISCALL OGetDetailSettings(void *)
{
    g_oOptions[0x38] = g_script.voiceChat;
    return g_oOptions;
}
long THISCALL OSetClientConfig(void *self, const uint32_t *c)
{
    Log("SetClientConfig " + N(self == g_oVoices[0]) + " " + Hex(c[0]) + " " + Hex(c[1]) + " " + Hex(c[2]) + " " + Hex(c[3]) + " " + Hex(c[4]) +
        " " + Hex(c[5]) + " " + Hex(c[6]) + " " + Hex(c[7]));
    return 0;
}
void *THISCALL OVoiceDelete(void *self, unsigned)
{
    Log("~C_NetVoice " + N(self == g_oVoices[0]));
    return self;
}
void THISCALL ODestroy(void *, bool free) { Log("Destroy " + N(free)); }
bool THISCALL OIsSpectating(void *self)
{
    const int i = static_cast<int>((static_cast<uint8_t *>(self) - g_oControllers[0]) / 0x400);
    Log("IsSpectating " + N(i));
    return g_script.spectating[i];
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
            s += " (call " + N(static_cast<long long>(i)) + ")\n     orig: " + x.substr(0, 400) + "\n     port: " + y.substr(0, 400);
            break;
        }
    }
    Check(false, s);
}

}   // namespace

#include "ng_diff_calls.inc"
