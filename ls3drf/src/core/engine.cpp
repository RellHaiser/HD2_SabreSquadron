// The engine's start-up and shut-down (0x10068c90 - 0x10068fb0). In the original a static object
// (0x100d16f1), made by 0x10068c90 while the CRT initialises the DLL, starts the log and makes the
// three singletons, IGraph, the I3D driver and the sound driver; 0x10068eb0 (FreeEngineGlobals,
// registered with atexit) deletes them. LS3DRF has no ISND, so no sound driver. The port does both in DllMain (the original's, 0x10094655,
// only returns TRUE): after every other global is initialised, whatever the link order, and before
// the CRT's exit list runs, as the original's last-registered destructor runs first. (Off Windows,
// a static object: see the end of the file.)
#include "platform/win_types.h"

#include <cstdint>

#include "core/debug.h"
#include "i3d/driver.h"
#include "igraph/igraph.h"
#include "platform/platform.h"

namespace ls3d::i3d {

I3D_driver *g_driver = nullptr;             // 0x100d16f8

}  // namespace ls3d::i3d

namespace {

// Performance counter ticks per 16 us (0x100d1590; at least 1; nothing in the engine reads it).
uint64_t g_perfTicksPer16Us;

// LS3DRF 0x10030810 (from LS3DF 0x10068cb0): no sound driver.
void StartEngine()
{
    ls3d::StartLog();
    g_perfTicksPer16Us = (ls3d::platform::CounterFrequency() << 4) / 1000000;
    if (!g_perfTicksPer16Us)
        g_perfTicksPer16Us = 1;
    g_pIGraph = new IGraph;
    ls3d::i3d::g_driver = new I3D_driver;
}

// LS3DRF 0x100309e0 (from LS3DF 0x10068eb0): FreeEngineGlobals without the sound driver.
void FreeEngine()
{
    delete ls3d::i3d::g_driver;
    ls3d::i3d::g_driver = nullptr;
    delete g_pIGraph;
    g_pIGraph = nullptr;
}

}  // namespace

// 0x10068fb0
extern "C" I3D_driver *__cdecl I3DGetDriver()
{
    return ls3d::i3d::g_driver;
}

#if !LS3D_POSIX

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        StartEngine();
    else if (reason == DLL_PROCESS_DETACH)
        FreeEngine();
    return TRUE;
}

#else

// A shared object has no DllMain. A static object does the same, as the original's own did: an ELF
// library runs its unprioritised initialisers in link order and their destructors in reverse, so
// build.py links this file last, after every global it may use is initialised; its destructor then
// runs first.
namespace {

struct S_engineLifetime {
    S_engineLifetime() { StartEngine(); }
    ~S_engineLifetime() { FreeEngine(); }
} g_engineLifetime;

}  // namespace

#endif
