// IGraph: the init block and timing (IGraph.cpp, 0x100699a0 - 0x10070eb0). (No textures or image
// files: LS3DRF has none.)
#include "igraph/igraph.h"

#include <cstdlib>
#include <cstring>
#include <objbase.h>

#include "core/cpu.h"
#include "core/debug.h"
#include "core/i3d_result.h"
#include "i3d/i3d_links.h"
#include "igraph/igraph_state.h"
#include "platform/platform.h"

using namespace ls3d;
using namespace ls3d::ig;

IGraph *g_pIGraph = nullptr;                    // 0x100d16f4

namespace ls3d::ig {

S_IGraphInit g_init;
HWND g_mainWnd = nullptr;
HWND g_childWnd = nullptr;
bool g_initialized = false;
bool g_noActivation = false;

uint32_t g_quirks = 0;
// (No scene flag, caps, texture formats or sizes, vertex shaders, texture projection matrices, z-buffer
// type, viewport, render-state cache, default material or texture lists: LS3DRF has none.)

}  // namespace ls3d::ig

namespace {

// Window title.
char g_appName[0x100];                          // 0x100d18b0

DWORD g_lastTime = 0;                           // 0x100d1738 ReadTimer

}  // namespace

// ---------------------------------------------------------------- construction

// (No animated bump map: LS3DRF has none.)

// LS3DRF 0x10031110 (from LS3DF 0x10069a00): the init block, two handles and the name only; no
// viewport, material, matrices, input state, texture list reserves or texture lock.
IGraph::IGraph()
{
    std::memset(&g_init, 0, sizeof g_init);
    g_initialized = false;
    g_mainWnd = nullptr;
    g_childWnd = nullptr;
    std::strcpy(g_appName, "LS3D");                  // (the window class's name)
}

// LS3DRF 0x10031160 (from LS3DF 0x10069e50): no texture lock to delete.
IGraph::~IGraph()
{
    if (g_initialized)
        dbgPrintf("!! WARNING  IGraph was initialised, but not closed");
    platform::ReleaseCursorClip();
}

// ---------------------------------------------------------------- object factories

// (No CreateITexture, LoadTexture or SetTexture: LS3DRF has none.)

// 0x1006a260. LS3DRF's slot 0x24 is a shared `ret 8` stub (0x10031490, marked at I3D_frame::Tick):
// no title, and the name is not kept.
void __stdcall IGraph::SetAppName(const char *)
{
}

// LS3DRF 0x100314a0 (from LS3DF 0x1006a2f0): does nothing.
void __cdecl IGraph::SetAppNameF(const char *, ...)
{
}

// 0x1006a470. LS3DRF's slot 0x14 is a shared `ret 4` stub (0x1002b0a0, marked at
// I3D_sound::UpdateSound): no message loop.
void __stdcall IGraph::ProcessWinMessages()
{
}

// ---------------------------------------------------------------- device

// (No world matrix, BeginScene, EndScene, DrawPrimitive or DrawIndexedPrimitive: LS3DRF has
// none.)

// LS3DRF 0x10031190 (from LS3DF 0x1006c2c0)
HRESULT __stdcall IGraph::SetFlags(uint32_t flags)
{
    g_init.flags = flags;
    return 0;
}

// LS3DRF 0x100311a0 (from LS3DF 0x1006c2d0): keeps the init block and logs the OS and CPU; no
// window, device, display modes or textures, and no DirectX 8.1 check.
HRESULT __stdcall IGraph::Init(const S_IGraphInit *init)
{
    CoInitialize(nullptr);
    if (!init)
        return I3DERR_INVALIDPARAMS;
    g_init = *init;
    // ReportError reads the callback from the init block (0x100d1d4c = S_IGraphInit::errorCallback).
    g_errorCallback = g_init.errorCallback;
    Close();
    std::memset(i3d::g_renderStats, 0, sizeof i3d::g_renderStats);

    S_cpuInfo cpu = {};
    _cpuid(&cpu);
    OSVERSIONINFOA os = {};
    os.dwOSVersionInfoSize = sizeof os;
#pragma warning(suppress : 4996)  // GetVersionExA is deprecated (and lies about newer Windows)
    GetVersionExA(&os);
    const char *osName = "Windows ??";
    if (os.dwMajorVersion == 3) {
        if (os.dwMinorVersion == 0)
            osName = "Windows NT 3.0";
        else if (os.dwMinorVersion == 1)
            osName = "Windows 3.1";
    } else if (os.dwMajorVersion == 4) {
        osName = os.dwMinorVersion == 0 ? "Windows NT 4.0" : "Windows 9x/ME";
    } else if (os.dwMajorVersion == 5) {
        if (os.dwMinorVersion == 0)
            osName = "Windows 2000";
        else if (os.dwMinorVersion == 1)
            osName = "Windows XP";
    }
    dbgPrintf("\nDetected OS: %s ver. %i.%i.%i %s", osName, os.dwMajorVersion, os.dwMinorVersion,
              os.dwBuildNumber & 0xffff, os.szCSDVersion);
    const uint32_t mhz = GetCpuSpeed();
    dbgPrintf("Detected CPU: %s, %s %iMHz, family:%i, model:%i, stepping:%i", cpu.vendor, cpu.name, mhz,
              cpu.family, cpu.model, cpu.stepping);
    g_initialized = true;
    return 0;
}

// LS3DRF 0x10031360 (from LS3DF 0x1006da20)
bool __stdcall IGraph::IsInitialized()
{
    return g_initialized;
}

// LS3DRF 0x10031370 (from LS3DF 0x1006da30)
const S_IGraphInit *__stdcall IGraph::GetInitData()
{
    return &g_init;
}

// LS3DRF 0x10031380 (from LS3DF 0x1006da40): clears the initialised flag only (Init made nothing to
// release); EAX stays as the caller had it.
void __stdcall IGraph::Close()
{
    g_initialized = false;
}

// LS3DRF 0x10031390 (from LS3DF 0x1006de50)
int __stdcall IGraph::ScrnX()
{
    return (g_init.flags & IG_CHILD_WINDOW) ? g_init.childWidth : g_init.width;
}

// LS3DRF 0x100313b0 (from LS3DF 0x1006de70)
int __stdcall IGraph::ScrnY()
{
    return (g_init.flags & IG_CHILD_WINDOW) ? g_init.childHeight : g_init.height;
}

// LS3DRF 0x100313d0 (from LS3DF 0x1006de90): the init block's depth, not the device's.
int __stdcall IGraph::GetBitDepth()
{
    return g_init.bitDepth;
}

// LS3DRF 0x100313e0 (from LS3DF 0x1006fb70)
DWORD __stdcall IGraph::ReadTimer()
{
    g_lastTime = platform::Milliseconds();
    return g_lastTime;
}

// LS3DRF 0x100313f0 (from LS3DF 0x1006fb80)
DWORD __stdcall IGraph::GetTimer(DWORD minMs, DWORD maxMs)
{
    const DWORD before = g_lastTime;
    ReadTimer();
    const DWORD elapsed = g_lastTime - before;
    if (elapsed < minMs) {
        platform::Pause(minMs - elapsed);
        g_lastTime += minMs - elapsed;
        return minMs;
    }
    if (maxMs && elapsed > maxMs)
        return maxMs;
    return elapsed;
}

// 0x10069e50 / 0x10069a00 run from the engine start-up (core/engine.cpp).
extern "C" IGraph *__cdecl GetIGraph()
{
    return g_pIGraph;
}
