// The platform: the operating system services the engine uses besides drawing - time and the mouse
// cursor.
//
// Unlike the devices (gfx), the platform is chosen when the DLL is built: one backend source is
// compiled in. Nothing needs two platforms in one process, and the engine reads the clock from
// static initialisers, before any object could be set up. The DLL has no window, message loop,
// keyboard or thread of its own (LS3DRF.dll imports none of those calls).
#pragma once

#include <cstdint>

#include <windows.h>

namespace ls3d::platform {

// ---------------------------------------------------------------- time

// A millisecond clock (wraps after 49.7 days; timeGetTime's).
uint32_t Milliseconds();
// Waits `ms` milliseconds; 0 gives up the rest of the time slice.
void Pause(uint32_t ms);

// ---------------------------------------------------------------- the mouse

// Lets the cursor leave any rectangle it was confined to.
void ReleaseCursorClip();

}  // namespace ls3d::platform
