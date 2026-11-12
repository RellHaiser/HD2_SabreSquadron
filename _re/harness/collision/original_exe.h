// The original game executable mapped into the test process, to call its functions next to the port's (a
// differential test). The unpacked executable (env HD2_ORIGINAL_EXE, default
// "H:\Other HD2 Files\HD2_SabreSquadron_unpacked.exe") is copied to its own base (0x400000: the test is linked
// elsewhere) without running anything; its imports are resolved from the game's DLLs (env HD2_GAME_DIR, default H:\HD2_Unmodified, for
// LS3DF.dll) and the system's. Only functions that need no started CRT or game state can be called.
#pragma once

#include <cstdint>

// First thing in main: low memory fills from the bottom while a process starts (its stack, heaps, mapped files),
// so 0x400000 is seldom free by main. This starts a suspended copy of the test, holds the original's range in it
// before it runs, and waits for it (killed with this process); true with its exit code here, false in the copy
// (or without the executable: run as is).
bool RunReservingOriginalBase(int &exitCode);
// Maps the executable once; false (with a message) when it or a DLL is missing or the base is taken.
bool LoadOriginalExe();
bool OriginalExeLoaded();
// An address of the original (its own addresses: the image is at its preferred base).
template <class T> T OriginalAt(uint32_t address) { return reinterpret_cast<T>(static_cast<uintptr_t>(address)); }
// The original's function at `at` goes to `to` instead (in the loaded copy only): an import's thunk (jmp [slot]) by
// its slot, a function by a 5-byte jmp rel32 (none shorter is patched). For code the original must not run (it
// allocates) or that the test fakes.
void RedirectOriginal(uint32_t at, const void *to);
// Undoes RedirectOriginal on a function entry (its first five bytes back); false if it was never patched.
bool RestoreOriginal(uint32_t at);
