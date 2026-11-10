#include "game/app.h"

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <process.h>
#include <algorithm>
#include <string>
#include <vector>

#include <objbase.h>
#include <mmsystem.h>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/game.h"
#include "common/os_files.h"
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
std::vector<C_DtaArchive *> g_modArchives;         // (port) Mods\*.dta, searched before g_archives

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
    if (g_setup.waitInactive)
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
    LoadStringA(g_instance, 3, title, 0x7F);
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

// (port) Mod archives --------------------------------------------------------------------
//
// Every Mods\*.dta is mounted before the game's archives, so a file in a mod replaces the
// game's own. Mod archives are not encrypted (packed with a blank key:
// `dta_pack.py MyMod Mods\MyMod.dta --key blank`).
//
// The first archive mounted is searched first, so it wins when two mods hold the same file.
// The order is alphabetical (ignoring case) unless Mods\load_order.txt says otherwise:
//
//     # Comments start with '#'; blank lines are skipped.
//     prefer_archives = yes      setting: archives are searched before loose files on disk
//     HighestPriority.dta        archives in search order, the first one wins
//     Second.dta                 (".dta" may be left out; names ignore case)
//     -Disabled.dta              not mounted at all
//
// Archives in the folder that the file does not name are mounted after the named ones, in
// alphabetical order. `prefer_archives` is rw_data's global switch (C_DtaArchive::
// SetPreferArchives): it applies to every archive, the game's own included; without it a
// loose file on disk overrides any archive, as in the original game.
namespace {

const char MODS_DIRECTORY[] = "Mods";
const char MODS_LOAD_ORDER[] = "Mods\\load_order.txt";

struct S_ModLoadOrder {
    std::vector<std::string> order;       // named archives, in search order
    std::vector<std::string> disabled;    // archives named with a leading '-'
    bool preferArchives = false;
    bool preferArchivesSet = false;
};

std::string TrimText(const std::string &text)
{
    const size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    const size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool SameName(const std::string &a, const std::string &b)
{
    return _stricmp(a.c_str(), b.c_str()) == 0;
}

bool ContainsName(const std::vector<std::string> &names, const std::string &name)
{
    for (const std::string &other : names)
        if (SameName(other, name))
            return true;
    return false;
}

// "Name" -> "Name.dta"; a name that already ends with ".dta" is kept.
std::string WithDtaExtension(const std::string &name)
{
    if (name.size() >= 4 && _stricmp(name.c_str() + name.size() - 4, ".dta") == 0)
        return name;
    return name + ".dta";
}

// Reads the load order file from disk (not through rw_data, so no archive can supply it).
// A missing file leaves `config` empty.
void ReadModLoadOrder(S_ModLoadOrder &config)
{
    FILE *file = std::fopen(MODS_LOAD_ORDER, "rt");
    if (!file)
        return;
    char buffer[1024];
    int lineNumber = 0;
    while (std::fgets(buffer, sizeof(buffer), file)) {
        ++lineNumber;
        std::string line = buffer;
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);
        line = TrimText(line);
        if (line.empty())
            continue;

        // Settings: "name = value". Only known setting names are taken as settings, so an
        // archive whose name holds '=' still works.
        const size_t equals = line.find('=');
        if (equals != std::string::npos && SameName(TrimText(line.substr(0, equals)), "prefer_archives")) {
            const std::string value = TrimText(line.substr(equals + 1));
            if (SameName(value, "yes") || SameName(value, "true") || SameName(value, "on") || value == "1") {
                config.preferArchives = true;
            } else if (SameName(value, "no") || SameName(value, "false") || SameName(value, "off") || value == "0") {
                config.preferArchives = false;
            } else {
                dbgPrintf("# ERROR MODS : %s line %d: prefer_archives must be yes or no, not '%s'",
                                 MODS_LOAD_ORDER, lineNumber, value.c_str());
                continue;
            }
            config.preferArchivesSet = true;
            continue;
        }

        if (line[0] == '-') {
            const std::string name = TrimText(line.substr(1));
            if (!name.empty() && !ContainsName(config.disabled, WithDtaExtension(name)))
                config.disabled.push_back(WithDtaExtension(name));
            continue;
        }
        const std::string name = WithDtaExtension(line);
        if (!ContainsName(config.order, name))
            config.order.push_back(name);
    }
    std::fclose(file);
}

// Set by ModKeyErrorCallback while a mod archive's table is read.
bool g_modKeyError;

// rw_data reports a wrong key only through the error callback (the ISD1 checksums do not
// match); SetKey itself succeeds.
void __cdecl ModKeyErrorCallback(C_DtaArchive *, const char *, int)
{
    g_modKeyError = true;
}

// The names of the .dta files in the Mods folder, alphabetical ignoring case.
std::vector<std::string> FindModArchives()
{
    std::vector<std::string> names;
    _finddata_t data;
    const intptr_t find = FileFindFirst(C_fstr("%s\\*.dta", MODS_DIRECTORY), &data);
    if (find == -1)
        return names;
    do {
        if ((data.attrib & _A_SUBDIR) == 0)
            names.push_back(data.name);
    } while (FileFindNext(find, &data) == 0);
    FileFindClose(find);
    std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
    return names;
}

// Mounts the Mods folder's archives (see above). Called before the game's archives are
// mounted, so they are searched first. Returns the load order file's settings.
S_ModLoadOrder MountModArchives()
{
    S_ModLoadOrder config;
    ReadModLoadOrder(config);
    const std::vector<std::string> found = FindModArchives();

    // Named archives first, in the file's order, then the rest alphabetically.
    std::vector<std::string> order;
    for (const std::string &name : config.order) {
        if (ContainsName(config.disabled, name))
            continue;
        bool present = false;
        for (const std::string &file : found) {
            if (SameName(file, name)) {
                order.push_back(file);
                present = true;
                break;
            }
        }
        if (!present)
            dbgPrintf("# ERROR MODS : '%s' is in %s but not in the Mods folder", name.c_str(), MODS_LOAD_ORDER);
    }
    for (const std::string &file : found)
        if (!ContainsName(order, file) && !ContainsName(config.disabled, file))
            order.push_back(file);

    for (const std::string &file : order) {
        const C_fstr path("%s\\%s", MODS_DIRECTORY, file.c_str());
        C_DtaArchive *archive = dtaCreate(path);
        if (!archive) {
            dbgPrintf("# ERROR MODS : '%s' is not a .dta archive", static_cast<const char *>(path));
            continue;
        }
        // Mod archives are packed with a blank key; the header and table of an encrypted
        // (ISD1) archive fail their checksums with it. (An ISD0 archive has no checksums,
        // so a wrong key goes unnoticed there; dta_pack.py writes ISD1.)
        g_modKeyError = false;
        const dtaErrorCallback previous = dtaSetCallbackERR(ModKeyErrorCallback);
        const bool keyed = archive->SetKey(0, 0);
        dtaSetCallbackERR(previous);
        if (!keyed || g_modKeyError) {
            dbgPrintf("# ERROR MODS : '%s' could not be read (it must be packed with a blank key)",
                             static_cast<const char *>(path));
            archive->Release();
            continue;
        }
        g_modArchives.push_back(archive);
        dbgPrintf("Mod archive %u: %s", static_cast<unsigned>(g_modArchives.size()),
                         static_cast<const char *>(path));
    }
    for (const std::string &file : found)
        if (ContainsName(config.disabled, file))
            dbgPrintf("Mod archive disabled: %s\\%s", MODS_DIRECTORY, file.c_str());

    return config;
}

// Applies the load order file's prefer_archives. The switch is global in rw_data but is
// reached through an archive object, so any mounted archive will do.
void ApplyModSettings(const S_ModLoadOrder &config)
{
    if (!config.preferArchivesSet)
        return;
    C_DtaArchive *any = !g_modArchives.empty() ? g_modArchives.front() : nullptr;
    for (int i = 0; !any && i < 16; ++i)
        any = g_archives[i];
    if (!any) {
        dbgPrintf("# ERROR MODS : prefer_archives not applied: no archive is mounted");
        return;
    }
    any->SetPreferArchives(config.preferArchives);
    dbgPrintf("Mods: prefer_archives = %s", config.preferArchives ? "yes" : "no");
}

} // namespace

// Mounts the data archives in priority order (patches first).  0x006dba30
// (port) The archives of the Mods folder are mounted before them (MountModArchives).
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
    const S_ModLoadOrder modSettings = MountModArchives();   // (port) searched before the game's archives
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
    ApplyModSettings(modSettings);                 // (port)
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
    for (C_DtaArchive *archive : g_modArchives)    // (port)
        archive->Release();
    g_modArchives.clear();
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
