// The Win32 platform (platform/platform.h): time and the cursor clip, the Windows calls
// IGraph made itself (IGraph.cpp), and the engine's other Windows calls (the log's directory, the
// version check's message box, COM and the OS banner), moved here unchanged.
#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>

#include <mmsystem.h>
#include <objbase.h>

namespace ls3d::platform {

// ---------------------------------------------------------------- time

uint32_t Milliseconds() { return timeGetTime(); }
void Pause(uint32_t ms) { Sleep(ms); }

uint64_t CounterFrequency()
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<uint64_t>(frequency.QuadPart);
}

// ---------------------------------------------------------------- the mouse

void ReleaseCursorClip() { ClipCursor(nullptr); }

// ---------------------------------------------------------------- the process

// (Was core/debug.cpp's LogPath.) As in the original, the directory is whatever _splitpath takes
// from the whole command line, arguments included, its leading quote skipped.
std::string LogDirectory()
{
    char drive[_MAX_DRIVE] = {}, dir[_MAX_DIR] = {};
    char line[_MAX_PATH * 4];
    std::snprintf(line, sizeof line, "%s", GetCommandLineA());
    _splitpath_s(line, drive, sizeof drive, dir, sizeof dir, nullptr, 0, nullptr, 0);
    std::string path = drive;
    path += dir[0] == '"' ? dir + 1 : dir;
    return path;
}

// (Was core/version.cpp's TEV.)
int StopMessage(const char *caption, const char *text)
{
    return MessageBoxA(nullptr, text, caption, MB_ICONHAND | MB_SYSTEMMODAL);
}

// (Was IGraph::Init's.)
void InitSystem() { CoInitialize(nullptr); }

// (Was IGraph::Init's: the original's names, which stop at Windows XP.)
OsVersion GetOsVersion()
{
    OSVERSIONINFOA os = {};
    os.dwOSVersionInfoSize = sizeof os;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)  // GetVersionExA is deprecated (and lies about newer Windows)
#endif
    GetVersionExA(&os);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    OsVersion v;
    v.name = "Windows ??";
    if (os.dwMajorVersion == 3) {
        if (os.dwMinorVersion == 0)
            v.name = "Windows NT 3.0";
        else if (os.dwMinorVersion == 1)
            v.name = "Windows 3.1";
    } else if (os.dwMajorVersion == 4) {
        v.name = os.dwMinorVersion == 0 ? "Windows NT 4.0" : "Windows 9x/ME";
    } else if (os.dwMajorVersion == 5) {
        if (os.dwMinorVersion == 0)
            v.name = "Windows 2000";
        else if (os.dwMinorVersion == 1)
            v.name = "Windows XP";
    }
    v.major = static_cast<int>(os.dwMajorVersion);
    v.minor = static_cast<int>(os.dwMinorVersion);
    v.build = static_cast<int>(os.dwBuildNumber & 0xffff);
    v.extra = os.szCSDVersion;
    return v;
}

}  // namespace ls3d::platform
