// The Win32 platform (platform/platform.h): time and the cursor clip, the Windows calls
// IGraph made itself (IGraph.cpp), moved here unchanged.
#include "platform/platform.h"

#include <mmsystem.h>

namespace ls3d::platform {

// ---------------------------------------------------------------- time

uint32_t Milliseconds() { return timeGetTime(); }
void Pause(uint32_t ms) { Sleep(ms); }

// ---------------------------------------------------------------- the mouse

void ReleaseCursorClip() { ClipCursor(nullptr); }

}  // namespace ls3d::platform
