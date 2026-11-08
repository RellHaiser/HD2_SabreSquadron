// The dedicated server's `-autorestart` (only in HD2_DEDICATED). A server hosting with `autorestart 1` starts a copy of
// itself with that switch and keeps a mutex named after its session, telling the copy the name over a named pipe. Every
// 10 s the copy tries the mutexes of its sessions: one no process holds is a server that has gone, and is started again.
#ifdef HD2_DEDICATED

#include "net/net_autorestart.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "game/pending.h"
#include "game/profile.h"

namespace {

const char kPipeName[] = "\\\\.\\pipe\\autorestart";
const char kWatchdogMutex[] = "HD2AutoRestart";
const char kServerExe[] = "hd2ds_sabresquadron.exe";

constexpr DWORD kCheckInterval = 10000;
constexpr DWORD kPipeWait = 3000;
constexpr int kIdleRounds = 10;                     // rounds without a session before the watchdog gives up
constexpr int kMessageSize = 0x32;                  // a pipe message: 'A' or 'R', the name and its 0

struct S_WatchedSession {
    char name[kMessageSize];
};

bool g_watchStopping;                               // (DS 0x009ddd54) the last session has said it has ended
HANDLE g_watchPipe;                                 // (DS 0x009ddd58)
std::vector<S_WatchedSession> g_watchedSessions;    // (DS 0x009ddd5c)
CRITICAL_SECTION g_watchedLock;                     // (DS 0x009ddd6c)
HANDLE g_watchMutex;                                // (DS 0x009ddd84) kept open while the process lives

bool MutexAlreadyExisted()
{
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED;
}

int FindSession(const char *name)
{
    for (int i = 0; i < static_cast<int>(g_watchedSessions.size()); ++i) {
        if (std::strcmp(g_watchedSessions[i].name, name) == 0)
            return i;
    }
    return -1;
}

// The messages of the servers, one client at a time.
DWORD WINAPI WatchPipeThread(void *)                // (DS 0x007aaeb0)
{
    for (;;) {
        while (!ConnectNamedPipe(g_watchPipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
        }
        char message[kMessageSize + 2];
        DWORD read = 0;
        if (!ReadFile(g_watchPipe, message, kMessageSize, &read, nullptr) || read == 0)
            return 0;
        // (Fix: a message without its 0 stops here; the original read on.)
        message[kMessageSize] = 0;
        const char *const name = message + 1;
        EnterCriticalSection(&g_watchedLock);
        if (message[0] == 'A') {
            if (FindSession(name) < 0) {
                S_WatchedSession session;
                std::strcpy(session.name, name);
                g_watchedSessions.push_back(session);
            }
        } else if (message[0] == 'R') {
            const int index = FindSession(name);
            if (index >= 0)
                g_watchedSessions.erase(g_watchedSessions.begin() + index);
            if (g_watchedSessions.empty())
                g_watchStopping = true;
        }
        LeaveCriticalSection(&g_watchedLock);
        FlushFileBuffers(g_watchPipe);
        DisconnectNamedPipe(g_watchPipe);
    }
}

bool StartServerProcess(char *commandLine)
{
    STARTUPINFOA startup;
    GetStartupInfoA(&startup);
    PROCESS_INFORMATION process;
    if (!CreateProcessA(kServerExe, commandLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
        return false;
    // (Fix: the handles closed; the original leaked them.)
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

// A session's message: 'A' / 'R' and its name, cut to the size of a pipe message.
void MakeMessage(char (&message)[kMessageSize], char kind, const char *session)
{
    // (Fix: cut to the size; the original's sprintf wrote past 52 bytes, and a longer message stops the watchdog's pipe.)
    std::snprintf(message, sizeof message, "%c%s", kind, session);
}

void SendMessageToWatchdog(const char (&message)[kMessageSize])     // (DS 0x007aad60)
{
    WaitNamedPipeA(kPipeName, kPipeWait);
    HANDLE pipe = CreateFileA(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(pipe, message, static_cast<DWORD>(std::strlen(message)) + 1, &written, nullptr);
    CloseHandle(pipe);
}

}  // namespace

void RunAutoRestartWatchdog()                       // (DS 0x007ab1a0)
{
    g_watchStopping = false;
    g_watchMutex = CreateMutexA(nullptr, FALSE, kWatchdogMutex);
    InitializeCriticalSection(&g_watchedLock);
    g_watchedSessions.clear();
    g_watchPipe = CreateNamedPipeA(kPipeName, PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
                                   PIPE_UNLIMITED_INSTANCES, 1000, 1000, 100, nullptr);
    DWORD threadId;
    HANDLE thread = CreateThread(nullptr, 0x2000, WatchPipeThread, nullptr, 0, &threadId);
    int idleRounds = 0;
    size_t sessions;
    do {
        Sleep(kCheckInterval);
        EnterCriticalSection(&g_watchedLock);
        for (size_t i = 0; i < g_watchedSessions.size(); ++i) {
            idleRounds = 0;
            HANDLE mutex = CreateMutexA(nullptr, FALSE, g_watchedSessions[i].name);
            const bool held = MutexAlreadyExisted();
            CloseHandle(mutex);
            if (held)
                continue;
            // HIGH (seen running the original): its format is "...-exec %DServer_%5d.txt" and its sprintf prints "%D" as
            // "D", so it starts a server on DServer_<port>.txt while Host wrote XDServer_: that server hosts nothing and
            // takes no mutex, and another is started every round. <port> is this process's, the default, not the session's.
            char command[256];
            std::sprintf(command, "%s -cmd -exec DServer_%5d.txt", kServerExe, g_gameProfile.m_mpSettings->port);
            LeaveCriticalSection(&g_watchedLock);
            StartServerProcess(command);
            EnterCriticalSection(&g_watchedLock);
        }
        ++idleRounds;
        sessions = g_watchedSessions.size();
        LeaveCriticalSection(&g_watchedLock);
    } while (sessions != 0 || (idleRounds < kIdleRounds && !g_watchStopping));
    TerminateThread(thread, 0);
    CloseHandle(g_watchPipe);
    DeleteCriticalSection(&g_watchedLock);
}

void StartAutoRestartWatchdog()                     // (DS 0x007ab470)
{
    g_watchMutex = CreateMutexA(nullptr, FALSE, kWatchdogMutex);
    const bool running = MutexAlreadyExisted();
    CloseHandle(g_watchMutex);
    if (running)
        return;
    char command[64];
    std::sprintf(command, "%s -autorestart", kServerExe);
    StartServerProcess(command);
}

void WatchSession(const char *session)              // (DS 0x007aae40)
{
    char message[kMessageSize];
    MakeMessage(message, 'A', session);
    g_watchMutex = CreateMutexA(nullptr, FALSE, message + 1);
    SendMessageToWatchdog(message);
}

void UnwatchSession(const char *session)            // (DS 0x007aadf0)
{
    char message[kMessageSize];
    MakeMessage(message, 'R', session);
    SendMessageToWatchdog(message);
}

#endif
