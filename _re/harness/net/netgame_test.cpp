// The network game (part 10d: net/net_game.cpp) with the game around it faked: the game app (its messages and
// commands recorded), the game, the module manager, the console, the command processor, the profile, DirectPlay's
// server. MinGW; build_game.bat; run netgame_test.exe ("netgame: N checks, 0 failures"). Mutations:
// mutations/mutate_10d1.py with the scratchpad's mutnet.py (--build build_game.bat).
#include <winsock2.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
#include "game/actors_module.h"
#include "game/campaign.h"
#include "game/console.h"
#include "game/controls.h"
#include "game/game.h"
#include "game/game_app.h"
#include "game/game_menu.h"
#include "game/game_texts.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/loader_screen.h"
#include "game/main_menu.h"
#include "game/module.h"
#include "game/mp_game_setup.h"
#include "game/pending.h"
#include "game/perf_timer.h"
#include "game/profile.h"
#include "game/spawn_point.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/dplay8.h"
#include "net/gamespy.h"
#include "net/net_channel.h"
#include "net/net_game.h"
#include "net/net_object.h"
#include "net/net_transport.h"
#undef private
#undef protected

#ifdef SendMessage
#undef SendMessage
#endif

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

}  // namespace

// ---- the fakes -----------------------------------------------------------------------------------------------------

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char line[256];
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

uint32_t g_gameRole = GAME_ROLE_LOCAL;
int g_localPlayerSlot;
char g_votableCommands[4][0x204] = {"maxclients", "kick", "map", "timelimit"};
void *g_netQueryThread;
char g_netDefaultSessionText[] = "session";
S_NetPlayer *g_netPlayerTable;
int g_netPlayerCount;

alignas(16) static unsigned char g_appStorage[sizeof(C_GameApp)];
C_GameApp *GetGameApp() { return reinterpret_cast<C_GameApp *>(g_appStorage); }
S_NetPlayerState *g_watchedPlayer;   // its join stage logged with each game message
#ifndef NET_MESSAGES_TEST
// (The messages' build links the real ones: net_messages.cpp.)
void C_GameApp::SendGameMessage(int type, int to, intptr_t a, int b, bool reliable)
{
    Log("game %d to %d a %d b %d %d", type, to, static_cast<int>(a), b, reliable);
    if (g_watchedPlayer)
        Log("stage %d", g_watchedPlayer->joinStage);
}
void C_GameApp::PostCommand(int command, intptr_t a, intptr_t b, bool flush)
{
    if (command == 3) {
        const char *line = reinterpret_cast<const char *>(a);
        Log("post 3 '%s' slot %d team %d", line, *reinterpret_cast<const int *>(line + 0x104), line[0x10C]);
        operator delete(const_cast<char *>(line));
        return;
    }
    Log("post %d %d %d %d", command, static_cast<int>(a), static_cast<int>(b), flush);
}
#endif
#ifndef NET_COMMANDS_TEST
// (The commands' build links the real ones: net_commands.cpp.)
void C_GameApp::HandleServerCommand(int id, C_BitStream *stream)
{
    Log("server command %d %u", id, stream->ReadBits(8));
}
void C_GameApp::HandleClientCommand(int id, C_BitStream *stream)
{
    Log("client command %d %u", id, stream->ReadBits(8));
}
#endif

alignas(16) static unsigned char g_gameStorage[sizeof(C_game)];
C_game *g_game;
C_game *GetGame() { return g_game; }
void C_game::PrintMessage(const S_vector &color, const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    Log("print %.0f %.0f %.0f %s", color.x, color.y, color.z, line);
}

namespace ModuleManager {
bool DispatchMessage(int moduleId, C_BitStream *msg)
{
    Log("dispatch %d %u", moduleId, msg->ReadBits(8));
    return true;
}
bool CallUnknown28(int moduleId, C_BitStream *msg)
{
    Log("call28 %d %u", moduleId, msg->ReadBits(8));
    return moduleId != 7;
}
}  // namespace ModuleManager

std::vector<std::string> g_consoleCommands;
alignas(16) static unsigned char g_consoleStorage[sizeof(C_Console)];
C_Console *GetConsole() { return reinterpret_cast<C_Console *>(g_consoleStorage); }
char *C_Console::PopCommand()
{
    static char command[64];
    if (g_consoleCommands.empty())
        return nullptr;
    std::strcpy(command, g_consoleCommands.front().c_str());
    g_consoleCommands.erase(g_consoleCommands.begin());
    return command;
}
alignas(16) static unsigned char g_processorStorage[sizeof(C_CommandProcessor)];
C_CommandProcessor *GetCommandProcessor() { return reinterpret_cast<C_CommandProcessor *>(g_processorStorage); }
bool C_CommandProcessor::Execute(const char *command)
{
    Log("execute %s", command);
    return true;
}

C_PlayerOptions g_options;
alignas(16) static unsigned char g_mpStorage[sizeof(S_MultiplayerSettings)];
S_MultiplayerSettings &g_testMp = *reinterpret_cast<S_MultiplayerSettings *>(g_mpStorage);
// (g_gameProfile itself: raw storage in netgame_globals.cpp.)
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return &g_options; }

C_GameTexts g_texts;
C_str C_GameTexts::Get(int id) const
{
    char text[16];
    std::snprintf(text, sizeof text, "T%X", id);
    return C_str(text);
}

// IGraph: a vtable of its own whose SetFlags (+0x08) records the flags.
void __stdcall FakeSetFlags(void *, uint32_t flags)
{
    Log("graph flags %u", flags);
}
void *g_graphVtable[64] = {nullptr, nullptr, reinterpret_cast<void *>(&FakeSetFlags)};
void *g_graphObject = g_graphVtable;
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graphObject); }

void S_ServerEntry::Reset()
{
    std::memset(this, 0xEE, sizeof *this);
    Log("server entry reset");
}

// The game around the players (part 10d-ii): the humans' controller types and teams by human (the player's and
// team 0 unless set), the actors by index, the ranks sent, the round time, the prints.
std::map<const C_Human *, int> g_controllerTypes, g_humanTeams;
int C_Human::GetControllerType() const
{
    auto it = g_controllerTypes.find(this);
    return it == g_controllerTypes.end() ? HUMAN_CONTROLLER_PLAYER : it->second;
}
int C_Human::GetTeam(bool) const
{
    auto it = g_humanTeams.find(this);
    return it == g_humanTeams.end() ? 0 : it->second;
}
void C_PlayerController::SendSoldierClass(int soldierClass) { Log("rank %d", soldierClass); }
std::map<int, C_Actor *> g_actors;
C_Actor *C_ActorsModule::GetActor(int index) const
{
    auto it = g_actors.find(index);
    return it == g_actors.end() ? nullptr : it->second;
}
bool g_cooperative;
bool C_GameProfile::IsCooperativeGame() const { return g_cooperative; }
int C_game::SetRoundTime(int ms)
{
    Log("round time %d", ms);
    return ms / 1000;
}
void C_game::Print(const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    Log("game print %s", line);
}
void C_game::PrintColored(const S_vector &color, const char *format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    Log("colored %.0f %s", color.x, line);
}
void C_GameApp::PrintMessage(const char *text) { Log("app print %s", text); }

// The other sub-parts' C_NetGame members.
void C_NetGame::ConfigureFirewall(int port, bool open) { Log("firewall %d %d", port, open); }
#ifndef NET_SESSION_TEST
int C_NetGame::EnumHosts(S_ServerEntry *server, int)
{
    Log("enum hosts %d", server ? 1 : 0);
    return 0;
}
#endif

// The GameSpy SDK (part 10f): its calls recorded; a browser's server is a key -> value map; gcd's check answers
// g_gcdAuthenticated / g_gcdMessage at once.
struct S_SbServer {
    std::map<std::string, std::string> values;
    uint32_t ping = 0, address = 0;
};
struct S_Qr2Buffer {
    std::vector<std::string> added;
};
struct S_Qr2KeyBuffer {
    std::vector<int> keys;
};
void qr2_register_key(int key, const char *name) { Log("qr2 key %d %s", key, name); }
Qr2ServerKeyCallback g_serverKey;
Qr2PlayerKeyCallback g_playerKey;
Qr2TeamKeyCallback g_teamKey;
Qr2KeyListCallback g_keyList;
Qr2CountCallback g_count;
Qr2AddErrorCallback g_addError;
int qr2_init(void *record, const char *ip, int basePort, const char *gameName, const char *secretKey, int isPublic,
             int natNegotiate, Qr2ServerKeyCallback serverKey, Qr2PlayerKeyCallback playerKey,
             Qr2TeamKeyCallback teamKey, Qr2KeyListCallback keyList, Qr2CountCallback count,
             Qr2AddErrorCallback addError, void *userData)
{
    Log("qr2 init %d %d %d %s %s %d %d %d", record != nullptr, ip != nullptr, basePort, gameName, secretKey, isPublic,
        natNegotiate, userData != nullptr);
    g_serverKey = serverKey;
    g_playerKey = playerKey;
    g_teamKey = teamKey;
    g_keyList = keyList;
    g_count = count;
    g_addError = addError;
    return 0;
}
void qr2_think(void *record) { Log("qr2 think %d", record != nullptr); }
void qr2_buffer_add(S_Qr2Buffer *buffer, const char *value) { buffer->added.push_back(value); }
void qr2_buffer_add_int(S_Qr2Buffer *buffer, int value) { buffer->added.push_back("#" + std::to_string(value)); }
void qr2_keybuffer_add(S_Qr2KeyBuffer *keys, int key) { keys->keys.push_back(key); }
int gcd_init(void *record, int gameId)
{
    Log("gcd init %d %d", record != nullptr, gameId);
    return 0;
}
void gcd_think() { Log("gcd think"); }
int g_gcdAuthenticated = 1;
char g_gcdMessage[32] = "";
void gcd_authenticate_user(int gameId, int localId, uint32_t address, const char *challenge, const char *response,
                           GcdAuthCallback callback, void *instance)
{
    Log("gcd check %d %d %08x %s %s", gameId, localId, address, challenge, response);
    callback(gameId, localId, g_gcdAuthenticated, g_gcdMessage, instance);
}
void gcd_disconnect_user(int gameId, int localId) { Log("gcd drop %d %d", gameId, localId); }
void gcd_compute_response(const char *cdKey, const char *challenge, char *response)
{
    std::sprintf(response, "%s+%s", cdKey, challenge);
}
S_ServerBrowser *g_browser = reinterpret_cast<S_ServerBrowser *>(0x5B);
S_ServerBrowser *ServerBrowserNew(const char *queryForGame, const char *queryFromGame, const char *queryFromKey,
                                  int queryFromVersion, int maxUpdates, int queryVersion,
                                  ServerBrowserCallback callback, void *instance)
{
    Log("sb new %s %s %s %d %d %d %d %d", queryForGame, queryFromGame, queryFromKey, queryFromVersion, maxUpdates,
        queryVersion, callback == OnBrowserCallback, instance != nullptr);
    return g_browser;
}
void ServerBrowserUpdate(S_ServerBrowser *browser, int async, int disconnectOnComplete, const uint8_t *fields,
                         int fieldCount, const char *filter)
{
    std::string list;
    for (int i = 0; i < fieldCount; ++i)
        list += " " + std::to_string(fields[i]);
    Log("sb update %d %d %d%s %d", browser == g_browser, async, disconnectOnComplete, list.c_str(), filter != nullptr);
}
int ServerBrowserThink(S_ServerBrowser *browser) { Log("sb think %d", browser == g_browser); return 0; }
void ServerBrowserClear(S_ServerBrowser *browser) { Log("sb clear %d", browser == g_browser); }
const char *SBServerGetStringValue(S_SbServer *server, const char *key, const char *fallback)
{
    auto it = server->values.find(key);
    return it == server->values.end() ? fallback : it->second.c_str();
}
int SBServerGetIntValue(S_SbServer *server, const char *key, int fallback)
{
    auto it = server->values.find(key);
    return it == server->values.end() ? fallback : std::atoi(it->second.c_str());
}
uint32_t SBServerGetPing(S_SbServer *server) { return server->ping; }
uint32_t SBServerGetPublicInetAddress(S_SbServer *server) { return server->address; }

// The main menu's news of a server (the entries kept), the CD key, the current map.
std::vector<S_ServerEntry *> g_foundServers;
void __fastcall C_MainMenu::OnServerFound(S_ServerEntry *server) { g_foundServers.push_back(server); }
const char *GetCDKey() { return "KEY"; }
C_MpMap *g_currentMap;
C_MpMap *S_MultiplayerSettings::GetCurrentMap() { return g_currentMap; }
int g_bandwidth[7];
void StopGameSpyQueries(uintptr_t *) { Log("stop gamespy"); }
#ifndef NET_SESSION_TEST
// (The session build links the real ones: net_session.cpp, net_voice.cpp.)
void C_NetGame::StopVoiceClient() { Log("stop voice client"); }
void C_NetGame::StopVoiceServer() { Log("stop voice server"); }
void C_NetGame::DeleteReplication() { Log("delete replication"); }
void C_NetGame::TakeBandwidth(int *figures) { std::memcpy(figures, g_bandwidth, sizeof g_bandwidth); }
C_NetVoice::C_NetVoice(NetVoiceHandler client, NetVoiceHandler server) : m_clientHandler(client), m_serverHandler(server) {}
C_NetVoice::~C_NetVoice() { Log("voice deleted"); }
long C_NetVoice::SetClientConfig(void *config)
{
    const auto *c = static_cast<const uint32_t *>(config);
    Log("voice config %u %u %u %d %d %u %u %u", c[0], c[1], c[2], static_cast<int>(c[3]), static_cast<int>(c[4]), c[5],
        c[6], c[7]);
    return 0;
}
#endif

// DirectPlay's server and client: their calls recorded.
struct T_Server : IDirectPlay8Server {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        Log("server release");
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Initialize(void *, PFNDPNMESSAGEHANDLER, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID *, const GUID *, void *, DWORD *, DWORD *, DWORD) override
    {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE CancelAsyncOperation(DPNHANDLE h, DWORD f) override
    {
        Log("server cancel %u %u", h, f);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE GetSendQueueInfo(DPNID, DWORD *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetApplicationDesc(void *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetServerInfo(const void *, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetClientInfo(DPNID, void *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetClientAddress(DPNID, void **, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetLocalHostAddresses(void **, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetApplicationDesc(const void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Host(const void *, void **, DWORD, const void *, const void *, void *, DWORD) override
    {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE SendTo(DPNID, const void *, DWORD, DWORD, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE CreateGroup(const void *, void *, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE DestroyGroup(DPNID, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE AddPlayerToGroup(DPNID, DPNID, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE RemovePlayerFromGroup(DPNID, DPNID, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetGroupInfo(DPNID, void *, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetGroupInfo(DPNID, void *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE EnumPlayersAndGroups(DPNID *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE EnumGroupMembers(DPNID, DPNID *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Close(DWORD f) override
    {
        Log("server close %u", f);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE DestroyClient(DPNID id, const void *data, DWORD size, DWORD flags) override
    {
        Log("destroy client %u", id);
        lastData = data;
        lastSize = size;
        lastFlags = flags;
        return destroyResult;
    }
    HRESULT STDMETHODCALLTYPE ReturnBuffer(DPNHANDLE, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetPlayerContext(DPNID, void **, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetGroupContext(DPNID, void **, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetCaps(void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetCaps(const void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetSPCaps(const GUID *, const void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetSPCaps(const GUID *, void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetConnectionInfo(DPNID id, void *info, DWORD) override
    {
        if (static_cast<DWORD *>(info)[0] == 0x5C)   // (its size set)
            static_cast<DWORD *>(info)[1] = id * 10;   // the round trip
        return 0;
    }
    HRESULT STDMETHODCALLTYPE RegisterLobby(void *, void *, DWORD) override { return 0; }
    HRESULT destroyResult = 0;
    const void *lastData = reinterpret_cast<void *>(1);
    DWORD lastSize = 1, lastFlags = 1;
};
struct T_Client : IDirectPlay8Client {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        Log("client release");
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Initialize(void *, PFNDPNMESSAGEHANDLER, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE EnumServiceProviders(const GUID *, const GUID *, void *, DWORD *, DWORD *, DWORD) override
    {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE EnumHosts(void *, void *, void *, const void *, DWORD, DWORD, DWORD, DWORD, void *,
                                        DPNHANDLE *, DWORD) override
    {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE CancelAsyncOperation(DPNHANDLE h, DWORD f) override
    {
        Log("client cancel %u %u", h, f);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Connect(const void *, void *, void *, const void *, const void *, const void *, DWORD,
                                      void *, DPNHANDLE *, DWORD) override
    {
        return 0;
    }
    HRESULT STDMETHODCALLTYPE Send(const void *, DWORD, DWORD, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetSendQueueInfo(DWORD *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetApplicationDesc(void *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetClientInfo(const void *, void *, DPNHANDLE *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetServerInfo(void *, DWORD *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetServerAddress(void **, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE Close(DWORD f) override
    {
        Log("client close %u", f);
        return 0;
    }
    HRESULT STDMETHODCALLTYPE ReturnBuffer(DPNHANDLE, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetCaps(void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetCaps(const void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE SetSPCaps(const GUID *, const void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetSPCaps(const GUID *, void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE GetConnectionInfo(void *, DWORD) override { return 0; }
    HRESULT STDMETHODCALLTYPE RegisterLobby(void *, void *, DWORD) override { return 0; }
};

#include "netgame_tests.inc"
#include "netplayers_tests.inc"
#include "netsearch_tests.inc"
#ifdef NET_SESSION_TEST
#include "netsession_tests.inc"
#endif
#ifdef NET_MESSAGES_TEST
#include "netmessages_tests.inc"
#endif
#ifdef NET_COMMANDS_TEST
#include "netcommands_tests.inc"
#endif

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(NET_COMMANDS_TEST)
    RunNetCommandsTests();
#elif defined(NET_MESSAGES_TEST)
    RunNetMessagesTests();
#elif defined(NET_SESSION_TEST)
    RunNetSessionTests();
#else
    RunNetGameTests();
    RunNetPlayersTests();
    RunNetSearchTests();
#endif
    std::printf("netgame: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
