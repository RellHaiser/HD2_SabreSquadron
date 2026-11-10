#include "game/app.h"

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <process.h>

#include <objbase.h>
#include <mmsystem.h>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/game.h"
#include "game/human_anim_tables.h"
#include "game/module.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/copy_protection.h"
#include "game/crash_report.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"
#include "net/net_autorestart.h"
#include "script/script_module.h"

static void LoadStringResource(HINSTANCE module, UINT id, LPSTR buffer, int size)    // 0x006ed840 (DS 0x00738bd0)
{
    LoadStringA(module, id, buffer, size);
}

// Note: the original interleaves calls into the copy-protection layer and shuffled
// indirection tables with this code. They have no effect on the game logic and are
// not reproduced (game/copy_protection.h).

S_LS3DSetup g_setup = {                            // 0x008aedf8, defaults from 0x006db380
    0x0C, 0, 1024, 768, 32, 0,
    0, 0, 0, 0, 1, 0,
    0, 1, 1, 1,
    0, 0, 0, 1,
    1, 4, 0,
};
HINSTANCE g_instance;                              // 0x008aedf4

// Microseconds from the performance counter (exported; the meleeTracker's sighting timestamps use its low half).
uint64_t MicrosecondsNow()
{
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart) * 1000000ull / static_cast<uint64_t>(frequency.QuadPart);
}

namespace {

const char REGISTRY_KEY[] = "Software\\Illusion Softworks\\Hidden & Dangerous 2";
const char INSTANCE_GUID[] = "D54BD597-ACC7-4870-9122-C1D03E7F617D";   // 0x00843548

UINT g_activateMessage;                            // 0x008aee34  broadcast to the running instance
HANDLE g_instanceMutex;                            // 0x008aee38
HANDLE g_frameTimerEvent;                          // 0x008aee20
C_GameApp *g_gameApp;                              // 0x008aee24
bool g_lowPriority;                                // 0x008aee28
uint32_t g_lastFrameMicros;                        // 0x008aee2c
int g_timeScaleRemainder;                          // 0x008aee30
LONG g_originalWndProc;                            // 0x008aee3c
bool g_pollKeyboard = true;                        // 0x00843534
C_DtaArchive *g_archives[16];                      // 0x008aeb08

// Object at 0x008ac448: a C_chunk plus a list of entries, created at startup and
// closed straight away. Its purpose is not identified yet.
struct S_StartupChunkHolder {
    uint8_t allocator;
    void *entriesBegin = nullptr;
    void *entriesEnd = nullptr;
    void *entriesCapacity = nullptr;
    C_chunk chunk;                                 // +0x10
};
S_StartupChunkHolder *g_startupChunkHolder;

// Make the executable's directory the working directory.  0x006dc330
void SetCurrentDirectoryToExe()
{
    char path[1000];
    GetModuleFileNameA(g_instance, path, sizeof(path));
    if (char *slash = std::strrchr(path, '\\'))
        *slash = '\0';
    SetCurrentDirectoryA(path);
}

// Reads the LS3D_setup registry blob into `setup`; false if missing or invalid.
bool ReadSetupFromRegistry(S_LS3DSetup &setup)
{
    HKEY key;
    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, REGISTRY_KEY, &key) != ERROR_SUCCESS)
        return false;
    DWORD type;
    DWORD size = sizeof(setup);
    if (RegQueryValueExA(key, "LS3D_setup", nullptr, &type, reinterpret_cast<BYTE *>(&setup), &size) != ERROR_SUCCESS)
        size = 0;
    RegCloseKey(key);
    // The original compares the version byte of the *current* settings (always
    // 0x0C at this point) instead of the value just read; the intent is checked here.
    return size == sizeof(setup) && setup.version == 0x0C;
}

// (0x006db430)
void __cdecl IgnoreEngineError(uint32_t, int, const char *, uint32_t)
{
}

// Creates the window and Direct3D device from the setup.  0x006db440
bool InitGraphics()
{
    InitProtectionTables();
    S_IGraphInit init = {};
    init.instance = g_instance;
    init.adapter = g_setup.adapter;
    init.x = init.y = 50;
    init.width = init.childWidth = g_setup.width;
    init.height = init.childHeight = g_setup.height;
    init.errorCallback = IgnoreEngineError;

    uint32_t flags = IG_INIT_CHILD_WINDOW;
    if (g_setup.antialiasing == 4)
        flags |= IG_INIT_FSAA4;
    if (g_setup.antialiasing == 2)
        flags |= IG_INIT_FSAA2;
    if (g_setup.hardwareTnl)
        flags |= IG_INIT_HW_TNL;
    // (The port's own switch: a window whatever the setup says, without touching the registry that the installed game shares.)
    if (g_setup.fullscreen && !std::strstr(GetCommandLineA(), " -window")) {
        // Console mode ("-c") stays windowed unless it is joining a game ("-join").
        const char *commandLine = GetCommandLineA();
        const bool consoleMode = std::strstr(commandLine, "-c") && !std::strstr(commandLine, "-join");
        if (!consoleMode) {
            flags |= g_setup.tripleBuffer ? (IG_INIT_FULLSCREEN | IG_INIT_TRIPLE_BUFFER) : IG_INIT_FULLSCREEN;
            init.bitDepth = static_cast<uint8_t>(g_setup.bitsPerPixel);
            if (g_setup.vsync)
                flags |= IG_INIT_VSYNC;
            init.refreshRate = static_cast<uint8_t>(g_setup.refreshRate);
        }
    }
    // (The port's own switch " -nowait": keeps running without the focus, for unattended runs such as coverage.)
    if (g_setup.waitInactive && !std::strstr(GetCommandLineA(), " -nowait"))
        flags |= IG_INIT_WAIT_INACTIVE;
    init.flags = flags;

    IGraph *graph = GetIGraph();
    if (!graph || FAILED(graph->Init(init)))
        return false;
    g_graphFlags = graph->GetInfo()->flags;

#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    graph->Clear(0, 1.0f, 3);
    graph->Flip();
#endif
    return true;
}

// Window caption from string resource 3.  0x006db650
void SetWindowTitle()
{
    char title[0x80];
    LoadStringResource(g_instance, 3, title, 0x7F);
    GetIGraph()->SetAppName(title);
}

// Starts DirectSound unless disabled in the setup.  0x006db700
void InitSound()
{
    if (g_setup.soundDisabled)
        return;

    uint32_t flags = g_setup.soundFlag10 ? 0x12 : 0x02;
    if (g_setup.soundFlag20)
        flags |= 0x20;
    switch (g_setup.soundQuality) {
    case 0: flags |= 0x400; break;
    case 1: flags |= 0x800; break;
    case 2: flags |= 0x1000; break;
    case 3: flags |= 0x2000; break;
    default: break;
    }
#ifndef HD2_DEDICATED   // no window
    ISndGetDriver()->Init(GetIGraph()->GetHWND(), 22050, 2, 16, flags);
#else
    ISndGetDriver()->Init(nullptr, 22050, 2, 16, flags);
#endif
}

bool InitRenderer()                                // 0x006db810
{
    return SUCCEEDED(I3DGetDriver()->Init(0));
}

void ApplyRenderSettings()                         // 0x006db830
{
    I3D_driver *driver = I3DGetDriver();
    driver->SetState(0x11, g_setup.renderState11);
    driver->SetState(0x19, g_setup.renderState19);
    driver->SetState(0x0D, 1);
    driver->SetState(0x13, 1);
    driver->SetState(0x15, g_setup.renderState15);
    driver->SetState(0x22, 1);
}

// Keyboard set-up, language selection and game texts.  0x006db970
void InitInputAndTexts()
{
#ifndef HD2_DEDICATED   // no input in LS3DRF.dll
    GetIGraph()->InitInput(0);
#endif
    InitControls(g_controls);
    const unsigned listenerCount = g_frameListenersBegin
        ? static_cast<unsigned>(g_frameListenersEnd - g_frameListenersBegin) : 0;
    BindControls(g_controls, GetIGraph(), g_frameListenersBegin, listenerCount);
    const C_str language = g_gameProfile.GetLanguage();
    LoadGameTexts(language.c_str());
}

// Mounts the data archives in priority order (patches first).  0x006dba30
void MountDataArchives()
{
    struct S_ArchiveKey {
        uint32_t low, high;
    };
    // Per-archive decryption keys, in g_archives order.
    static const S_ArchiveKey keys[16] = {
        {0xA0A0A0A4, 0xA0A0A0A0}, {0xA0A0A0A3, 0xA0A0A0A0}, {0xA0A0A0A2, 0xA0A0A0A0},
        {0xA0A0A0A1, 0xA0A0A0A0}, {0xA0A0A0F1, 0xA0A0A0A0}, {0xA0A0A0A0, 0xA0A0A0A0},
        {0xA0A0A0A0, 0xA0A0A0A0}, {0x5D805270, 0x10ACB252}, {0x5D805259, 0x10ACB252},
        {0x7654A3F0, 0x22BCDA98}, {0xB438D0A5, 0xF26527FA}, {0x4FE85106, 0x8D2965CA},
        {0x0AB4E72D, 0xCF761298}, {0xEA85902A, 0x65F7AB23}, {0xB038D0A0, 0xE26520FA},
        {0xB038D1A1, 0xF26520FA},
    };

    const C_str language = g_gameProfile.GetLanguage();
    const C_fstr languageArchive("Lang%s.dta", static_cast<const char *>(language));

    const char *names[16] = {
        "PatchX03.dta", "PatchX02.dta", "PatchX01.dta", "SabreSquadron.dta", nullptr,
        languageArchive, "LangEnglish.dta", "Patch.dta", "Models.dta", "Missions.dta",
        "Maps.dta", "Sounds.dta", "Scripts.dta", "Others.dta", "Maps_C.dta", "Maps_U.dta",
    };

    for (int i = 0; i < 16; ++i)
        g_archives[i] = nullptr;
    for (int i = 0; i < 16; ++i) {
        if (!names[i])
            continue;
        if (i == 6 && g_archives[5])
            continue;                              // English only when the language archive is missing
        g_archives[i] = dtaCreate(names[i]);
    }

    for (int i = 0; i < 16; ++i) {
        if (C_DtaArchive *archive = g_archives[i]) {
            // The original applies the key in two XOR steps with a random mask
            // in between; the resulting key is exactly keys[i].
            const uint32_t maskLow = (static_cast<uint32_t>(std::rand()) << 16) | (std::rand() & 0xFFFF);
            const uint32_t maskHigh = std::rand() & 0xFFFF;
            archive->XorKey(keys[i].low ^ maskLow, keys[i].high ^ maskHigh);
            const uint64_t current = archive->GetKey();
            archive->XorKey(static_cast<uint32_t>(current) ^ keys[i].low,
                            static_cast<uint32_t>(current >> 32) ^ keys[i].high);
        } else if (names[i] && !(i == 6 && g_archives[5])) {
            dbgPrintf(C_fstr("# ERROR DATAFILE : datafile '%s' missing", names[i]));
        }
    }
}

// Search directories of the resource caches.  0x006dc230
void RegisterDataDirectories()
{
    AddDataDirectory("Maps", DATA_DIR_MAPS);
    AddDataDirectory("Maps_C", DATA_DIR_MAPS);
    AddDataDirectory("Maps_U", DATA_DIR_MAPS);
    AddDataDirectory("Models", DATA_DIR_MODELS);
    AddDataDirectory("Sounds", DATA_DIR_SOUNDS);
    AddDataDirectory("Models", DATA_DIR_ANIMATIONS);
    AddDataDirectory("Models", DATA_DIR_SHADOWS);
    AddDataDirectory("Models", DATA_DIR_VERSIONS);
}

bool InitModules()                                 // 0x006dc380
{
    InitInputAndTexts();
    ModuleManager::BroadcastInit();
    return true;
}

// Restores the window position saved by ShutdownGame.
void RestoreWindowPosition()
{
    const int file = dtaOpen("window.cfg", 0);
    if (file < 0)
        return;
    int x = 0, y = 0;
    dtaRead(file, &x, sizeof(x));
    dtaRead(file, &y, sizeof(y));
    dtaClose(file);
#ifndef HD2_DEDICATED   // no window
    HWND window = GetIGraph()->GetHWND();
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetFocus(GetIGraph()->GetHWND());
#endif
}

void SaveWindowPosition()
{
#ifndef HD2_DEDICATED   // no window
    const int file = dtaOpenWrite("window.cfg", 0x10000);
    if (file < 0)
        return;
    RECT rect;
    GetWindowRect(GetIGraph()->GetHWND(), &rect);
    dtaWrite(file, &rect.left, sizeof(rect.left));
    dtaWrite(file, &rect.top, sizeof(rect.top));
    dtaClose(file);
#endif
}

// Registers the message a second instance broadcasts to bring the first one up.
struct S_RegisterActivateMessage {                 // static initializer 0x006dd8b0
    S_RegisterActivateMessage() { g_activateMessage = RegisterWindowMessageA(INSTANCE_GUID); }
} g_registerActivateMessage;

} // namespace

bool InitGame(HINSTANCE instance)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    g_instance = instance;
    SetCurrentDirectoryToExe();
    if (!TEV("Game.exe :", 0x22F))
        return false;

    S_LS3DSetup setup;
    if (!ReadSetupFromRegistry(setup)) {
        // No (valid) configuration yet: run the setup program and try again.
        _spawnl(_P_WAIT, "setup.exe", "setup.exe", nullptr);
        if (!ReadSetupFromRegistry(setup))
            return false;
    }
    g_setup = setup;

    g_netGame.Init();
    if (!InitGraphics())
        return false;

#ifndef HD2_DEDICATED   // no window
    HWND window = GetIGraph()->GetHWND();
    SetClassLongA(window, GCL_HICON, reinterpret_cast<LONG>(LoadIconA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1))));
#endif
    SetWindowTitle();
    InitSound();
    if (!InitRenderer())
        return false;
    ApplyRenderSettings();
    MountDataArchives();
    RegisterDataDirectories();
    if (!InitModules())
        return false;

    RestoreWindowPosition();
    InitUnknown8CA4C8();
    InitHumanAnimTables();
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    return true;
}

void ShutdownGame()
{
    g_netGame.Shutdown();
    g_gameProfile.FreeObjects();
    ModuleManager::BroadcastDone();
    DoneHumanAnimTables();
    FreeUnknown8CA4C8();
    SaveWindowPosition();
    FreeResourceCaches();

    if (I3DGetDriver())
        I3DGetDriver()->Close();
    if (ISndGetDriver())
        ISndGetDriver()->Close();
    if (GetIGraph())
        GetIGraph()->Close();
    for (C_DtaArchive *&archive : g_archives) {
        if (archive) {
            archive->Release();
            archive = nullptr;
        }
    }
    CoUninitialize();
}

C_GameApp *GetGameApp()                            // 0x006dcd10
{
    return g_gameApp;
}

uint32_t LimitFrameTime(uint32_t minMs, uint32_t maxMs)
{
    const uint32_t elapsed = static_cast<uint32_t>(MicrosecondsNow()) - g_lastFrameMicros;
    const uint32_t elapsedMs = elapsed / 1000;
    if (elapsedMs > maxMs) {
        g_lastFrameMicros = static_cast<uint32_t>(MicrosecondsNow());
        return maxMs;
    }
    if (elapsedMs < minMs) {
        // Sleep the rest of the minimum frame time on a one-shot multimedia timer.
        timeSetEvent(minMs - elapsedMs, 0, reinterpret_cast<LPTIMECALLBACK>(g_frameTimerEvent), 0,
                     TIME_ONESHOT | TIME_CALLBACK_EVENT_PULSE);
        WaitForSingleObject(g_frameTimerEvent, 100);
        g_lastFrameMicros = static_cast<uint32_t>(MicrosecondsNow());
        return minMs;
    }
    g_lastFrameMicros = static_cast<uint32_t>(MicrosecondsNow());
    return elapsedMs;
}

void SetKeyboardPolling(bool enable)
{
    g_pollKeyboard = enable;
}

namespace {

// Lowers the thread priority while the game window is in the background, except
// for a server that is not also the local client.
void UpdateThreadPriority()
{
#ifndef HD2_DEDICATED   // no window
    const bool background = GetIGraph()->GetHWND() != GetForegroundWindow();
#else
    const bool background = false;
#endif
    const bool mayIdle = (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL || !(g_gameRole & GAME_ROLE_SERVER);
    if (mayIdle && background) {
        if (!g_lowPriority) {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
            g_lowPriority = true;
        }
    } else if (g_lowPriority) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        g_lowPriority = false;
    }
}

// Applies the game speed (slow motion etc.) to a frame time, carrying the rounding.
int ScaleFrameTime(int timeDelta)
{
    const float speed = GetGEffects()->GetTimeScale();
    if (speed == 1.0f)
        return timeDelta;
    const int total = timeDelta + g_timeScaleRemainder;
    const int scaled = static_cast<int>(static_cast<float>(total) * speed);
    g_timeScaleRemainder = total - static_cast<int>(static_cast<float>(scaled) / speed);
    return scaled;
}

// Writes <computer>[NNNN].tga with the first free number.
void SaveScreenshot()
{
    for (int i = 0; i < 10000; ++i) {
        char computer[256];
        DWORD size = 0xFF;
        GetComputerNameA(computer, &size);
        char fileName[128];
        std::sprintf(fileName, "%s[%04i].tga", computer, i);
        WIN32_FIND_DATAA found;
        const HANDLE search = FindFirstFileA(fileName, &found);
        if (search == INVALID_HANDLE_VALUE) {
            FindClose(search);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
            GetIGraph()->SaveShot(fileName, 0, 0, 0);
#endif
            return;
        }
        FindClose(search);
    }
}

// Executes the command queued on the application (local game only).
void ExecutePendingCommand(C_GameApp &app)
{
    if (app.m_pendingCommand == APP_COMMAND_NONE)
        return;
    IGraph *graph = GetIGraph();
    const uint32_t savedFlags = graph->GetInfo()->flags;
    graph->SetFlags(savedFlags & ~IGRAPH_FLAG_20);
    switch (app.m_pendingCommand) {
    case APP_COMMAND_RESTART:
        app.RestartMission();
        break;
    case APP_COMMAND_QUICK_SAVE:
    case APP_COMMAND_SAVE:
        app.SaveCommand();
        break;
    case APP_COMMAND_QUICK_LOAD:
    case APP_COMMAND_LOAD:
        app.LoadCommand();
        break;
    default:
        break;
    }
    graph->SetFlags(savedFlags);
    app.m_pendingCommand = APP_COMMAND_NONE;
}

} // namespace

int RunGame()
{
    g_frameTimerEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    C_GameApp *app = new C_GameApp();
    g_gameApp = app;

    if (!InitGame(GetModuleHandleA(nullptr))) {
        delete app;
        return -1;
    }
    GetIGraph()->ReadTimer();
    if (!app->Init(false))
        return -1;

    if (!g_startupChunkHolder)
        g_startupChunkHolder = new S_StartupChunkHolder();
    g_startupChunkHolder->chunk.Close();

    // Route the window messages through the game first.
#ifndef HD2_DEDICATED   // no window
    HWND window = GetIGraph()->GetHWND();
    g_originalWndProc = GetWindowLongA(window, GWL_WNDPROC);
    if (g_originalWndProc)
        SetWindowLongA(window, GWL_WNDPROC, reinterpret_cast<LONG>(GameWindowProc));
#endif

    for (;;) {
#ifdef HD2_DEDICATED
        // (DS 0x00725d8c) The server's own message loop, for its console: LS3DRF.dll's ProcessWinMessages does
        // nothing. The frame is at most net rate `maxfreq` a second.
        const char key = 0;
        MSG message;
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        int timeDelta = static_cast<int>(LimitFrameTime(1000 / g_netMaxFreq, 100));
#else
        UpdateThreadPriority();

        char key = 0;
        if (g_pollKeyboard) {
            IGraph *graph = GetIGraph();
            graph->ProcessWinMessages();
            graph->ReadKeyboard();
            key = graph->GetAsciiKey();
        }

        int timeDelta = static_cast<int>(LimitFrameTime(10, 100));
#endif
        if (app->IsStateActive())
            timeDelta = ScaleFrameTime(timeDelta);

#ifndef HD2_DEDICATED   // no input in LS3DRF.dll; the server's loop has no frame listeners or controls
        GetIGraph()->BeginFrame();
        if (g_frameListenersBegin) {
            for (C_FrameListener **it = g_frameListenersEnd; it != g_frameListenersBegin;)
                (*--it)->OnFrame(true);
        }
        g_controls.Update(false);
#endif
        I3DGetDriver()->Tick(timeDelta);

#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll; (DS 0x00725e71) the server goes on to the messages
        if (g_gameRole & GAME_ROLE_CLIENT) {
            if (g_scriptModule.IsCutsceneRunning()) {
                // Full-screen pass (the cutscene's letterbox bars): temporarily widen the viewport
                // and clear it.
                IGraph *graph = GetIGraph();
                S_IGraphViewport viewport = *graph->GetViewport();
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = graph->ScrnX();
                viewport.height = graph->ScrnY();
                graph->SetViewport(viewport);
                graph->Clear(0, 1.0f, 3);
            }
            app->RenderState();
        }
#endif

        g_netGame.ReceiveMessages(timeDelta, true);
        bool keepRunning = true;
        if ((g_gameRole & GAME_ROLE_SERVER) || g_gameRole == 0x12)
            keepRunning = app->UpdateServer(timeDelta);
        if (keepRunning) {
            g_netGame.ReceiveMessages(timeDelta, false);
#ifdef HD2_DEDICATED
            keepRunning = app->UpdateClient(timeDelta);     // (DS 0x00725eab) whatever the role
#else
            const bool runClient = (g_gameRole & GAME_ROLE_CLIENT) ||
                                   ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL && g_mpSettings->dedicated);
            if (runClient)
                keepRunning = app->UpdateClient(timeDelta);
#endif
        }
        if (!keepRunning)
            break;
        g_netGame.SendMessages(timeDelta);

        if (app->IsStateActive() && key == 'W')
            SaveScreenshot();

        if ((g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL && app->IsStateActive()) {
            // In a game the command waits until the player's controls are on again; in the menus (no game) only a load from the menu is done.
            if (GetGame()) {
                if (GetGame()->IsPlayerInputEnabled())
                    ExecutePendingCommand(*app);
            } else if (app->m_pendingCommand != APP_COMMAND_NONE) {
                if (app->m_pendingCommand == APP_COMMAND_LOAD_FROM_MENU)
                    app->LoadFromMenu();
                app->m_pendingCommand = APP_COMMAND_NONE;
            }
        }

#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
        if (g_gameRole & GAME_ROLE_CLIENT)
            GetIGraph()->Flip();
#endif
        _controlfp(_PC_53, _MCW_PC);
    }

#ifndef HD2_DEDICATED   // no window
    if (g_originalWndProc)
        SetWindowLongA(GetIGraph()->GetHWND(), GWL_WNDPROC, g_originalWndProc);
#endif
    if (app->IsStateActive() && (g_gameRole & GAME_ROLE_SERVER))
        app->Shutdown();
    delete app;

    if (S_StartupChunkHolder *holder = g_startupChunkHolder) {
        holder->chunk.Close();
        delete holder;
        g_startupChunkHolder = nullptr;
    }

    RunExitSequence();
    ShutdownGame();
    CloseHandle(g_frameTimerEvent);
    return 1;
}

// (0x006dd8d0) The running instance woken by another; everything else to the window's own.
LRESULT CALLBACK GameWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_activateMessage) {
        ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
    }
    return CallWindowProcA(reinterpret_cast<WNDPROC>(g_originalWndProc), hwnd, msg, wParam, lParam);
}

// (The end of 0x006dceb0.) Made if it was not: the set-up it makes is deleted with it.
void RunExitSequence()
{
    delete GetCommandProcessor();
    ClearCommandProcessor();
}

// (The middle of 0x006dc770.) The caches, not their objects.
void FreeResourceCaches()
{
    g_modelCache.Forget();
    g_soundCache.Forget();
    g_shadowCache.Forget();
    g_versionModelCache.Forget();
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE /*prevInstance*/, LPSTR /*cmdLine*/, int /*showCmd*/)
{
#ifdef HD2_DEDICATED
    // (DS 0x007263d0) Any number of servers may run, beside a client too; the key is read but not checked.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    // (DS 0x007263fa)
    if (std::strstr(GetCommandLineA(), "-autorestart")) {
        RunAutoRestartWatchdog();
        return 0;
    }
    g_fatalErrorHandler = OnFatalError;
    GetCDKey();
#else
    // Only one instance: wake the running one instead.
    g_instanceMutex = CreateMutexA(nullptr, FALSE, INSTANCE_GUID);
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED) {
        CloseHandle(g_instanceMutex);
        DWORD_PTR result;
        SendMessageTimeoutA(HWND_BROADCAST, g_activateMessage, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
        return 0;
    }

    g_fatalErrorHandler = OnFatalError;

    const char *cdKey = GetCDKey();
    if (!cdKey || !ValidateCDKey(cdKey)) {
        ShowMessage("Wrong CDKey. Please contact Illusion Softworks.", "Security", 0);
        return -3000;
    }
#endif

    // Disable the Sticky Keys hot key while the game runs.
    STICKYKEYS stickyKeys = {sizeof(STICKYKEYS), 0};
    SystemParametersInfoA(SPI_GETSTICKYKEYS, sizeof(stickyKeys), &stickyKeys, 0);
    if (stickyKeys.dwFlags & SKF_AVAILABLE) {
        STICKYKEYS disabled = {sizeof(STICKYKEYS), 0};
        SystemParametersInfoA(SPI_SETSTICKYKEYS, sizeof(disabled), &disabled, 0);
    }
    const int result = RunGame();
    SystemParametersInfoA(SPI_SETSTICKYKEYS, sizeof(stickyKeys), &stickyKeys, 0);

    if (result == 2) {
        // Restart requested (e.g. after changing the video setup).
        char exePath[MAX_PATH + 4];
        GetModuleFileNameA(instance, exePath, MAX_PATH);
        const C_fstr command("%s %s", exePath, GetCommandLineA());
        _spawnl(_P_NOWAIT, exePath, static_cast<const char *>(command), nullptr);
    }
    if (g_instanceMutex && !CloseHandle(g_instanceMutex))
        GetLastError();
    return result;
}
