// The Win32 platform (platform/platform.h): time, locks and the cursor clip, the Windows calls
// IGraph made itself (IGraph.cpp), moved here unchanged.
#include "platform/platform.h"

#include <mmsystem.h>

namespace ls3d::platform {

// ---------------------------------------------------------------- time

uint32_t Milliseconds() { return timeGetTime(); }
void Pause(uint32_t ms) { Sleep(ms); }

// ---------------------------------------------------------------- locks

// The handles are Windows critical sections.
Mutex *NewMutex()
{
    auto *section = new CRITICAL_SECTION;
    InitializeCriticalSection(section);
    return reinterpret_cast<Mutex *>(section);
}
void Lock(Mutex *mutex) { EnterCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(mutex)); }
void Unlock(Mutex *mutex) { LeaveCriticalSection(reinterpret_cast<CRITICAL_SECTION *>(mutex)); }

// ---------------------------------------------------------------- the mouse

void ReleaseCursorClip() { ClipCursor(nullptr); }

}  // namespace ls3d::platform
