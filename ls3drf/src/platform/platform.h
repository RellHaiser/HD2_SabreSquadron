// The platform: the operating system services the engine uses besides drawing - time, the mouse
// cursor, the log's directory, the version check's message box and the OS banner in the log.
//
// The platform is chosen when the DLL is built: one backend source is compiled in (platform/win32/
// on Windows, platform/posix/ elsewhere). Nothing needs two platforms in one process, and the engine
// reads the clock from static initialisers, before any object could be set up. The DLL has no
// window, message loop, keyboard or thread of its own (LS3DRF.dll imports none of those calls).
#pragma once

#include <cstdint>
#include <string>

#include "platform/win_types.h"

namespace ls3d::platform {

// ---------------------------------------------------------------- time

// A millisecond clock (wraps after 49.7 days; timeGetTime's).
uint32_t Milliseconds();
// Waits `ms` milliseconds; 0 gives up the rest of the time slice.
void Pause(uint32_t ms);
// Ticks per second of the high-resolution counter (QueryPerformanceFrequency's).
uint64_t CounterFrequency();

// ---------------------------------------------------------------- the mouse

// Lets the cursor leave any rectangle it was confined to.
void ReleaseCursorClip();

// ---------------------------------------------------------------- the process

// The directory the engine writes its log to, ending in a separator (or empty: the current
// directory). On Windows the original's: drive and directory of the whole command line.
std::string LogDirectory();

// Shows a stop message (Windows: a system-modal message box) and returns the box's result
// (IDOK = 1 where there is no box).
int StopMessage(const char *caption, const char *text);

// What IGraph::Init needs of the system before it starts (Windows: COM on this thread).
void InitSystem();

// The operating system for the log's "Detected OS" line.
struct OsVersion {
    std::string name;                   // "Windows XP", "Linux", ...
    int major = 0;
    int minor = 0;
    int build = 0;
    std::string extra;                  // the service pack, the kernel's build string, ...
};
OsVersion GetOsVersion();

}  // namespace ls3d::platform
