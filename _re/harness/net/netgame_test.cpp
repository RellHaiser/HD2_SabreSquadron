// The network game (part 10d: net/net_game.cpp) with the game around it faked: the game app (its messages and
// commands recorded), the game, the module manager, the console, the command processor, the profile, DirectPlay's
// server. MinGW; build_game.bat; run netgame_test.exe ("netgame: N checks, 0 failures"). Mutations:
// mutations/mutate_10d1.py with the scratchpad's mutnet.py (--build build_game.bat).
#include <winsock2.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define protected public
#define private public
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
#include "net/dplay8.h"
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
void C_GameApp::HandleServerCommand(int id, C_BitStream *stream)
{
    Log("server command %d %u", id, stream->ReadBits(8));
}
void C_GameApp::HandleClientCommand(int id, C_BitStream *stream)
{
    Log("client command %d %u", id, stream->ReadBits(8));
}

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
C_CommandProcessor g_processor;
C_CommandProcessor *GetCommandProcessor() { return &g_processor; }
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

void S_ServerEntry::Reset() { Log("server entry reset"); }

// The other sub-parts' C_NetGame members.
int C_NetGame::CountPlayers() { return 4; }
void C_NetGame::SendPlayerScore(int slot, bool all) { Log("send score %d %d", slot, all); }
void C_NetGame::KickPlayer(int slot, int reason) { Log("kick %d %d", slot, reason); }
void C_NetGame::SetLevelBounds(const float *min, const float *size)
{
    Log("level bounds %.2f %.2f %.2f %.2f %.2f %.2f", min[0], min[1], min[2], size[0], size[1], size[2]);
}
void C_NetGame::StopReporting(int *, bool) { Log("stop reporting"); }
void C_NetGame::AnswerQueries() { Log("answer queries"); }
void C_NetGame::StopVoiceClient() { Log("stop voice client"); }
void C_NetGame::StopVoiceServer() { Log("stop voice server"); }
void C_NetGame::DeleteReplication() { Log("delete replication"); }
int g_bandwidth[7];
void C_NetGame::TakeBandwidth(int *figures) { std::memcpy(figures, g_bandwidth, sizeof g_bandwidth); }
void StopGameSpyQueries(uintptr_t *) { Log("stop gamespy"); }
C_NetVoice::~C_NetVoice() { Log("voice deleted"); }
int C_NetVoice::SetClientConfig(void *config)
{
    const auto *c = static_cast<const uint32_t *>(config);
    Log("voice config %u %u %u %d %d %u %u %u", c[0], c[1], c[2], static_cast<int>(c[3]), static_cast<int>(c[4]), c[5],
        c[6], c[7]);
    return 0;
}

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
    HRESULT STDMETHODCALLTYPE GetConnectionInfo(DPNID, void *, DWORD) override { return 0; }
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

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    RunNetGameTests();
    std::printf("netgame: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
