// IGraph: the engine's init block and timer
// (IGraph.cpp in the original source tree, 0x100699a0 - 0x10070eb0, vtable 0x1009d840).
//
// One object exists (GetIGraph()); it holds nothing but its vtable, all state is file-level. Every
// method is __stdcall with `this` on the stack (the slot order is the DLL's ABI), except the
// printf-style SetAppNameF, which MSVC makes __cdecl.
#pragma once

#include <cstdint>

#include "platform/win_types.h"

#include "core/debug.h"
#include "core/layout.h"

// IGraph::Init parameters (0x38 bytes, copied to 0x100d1d18).
struct S_IGraphInit {
    HINSTANCE instance;                 // +0x00
    int adapter;                        // +0x04 Direct3D adapter ordinal
    int x, y;                           // +0x08 window position (windowed)
    int width, height;                  // +0x10 client size / fullscreen resolution
    int childX, childY;                 // +0x18 child window rectangle (IG_CHILD_WINDOW)
    int childWidth, childHeight;        // +0x20
    uint8_t bitDepth;                   // +0x28 fullscreen colour depth: 16 or 32
    uint8_t refreshRate;                // +0x29 fullscreen refresh rate (0: default)
    uint16_t reserved2A;
    uint32_t flags;                     // +0x2C IG_* below
    HMENU menu;                         // +0x30 window menu (IG_MENU; 0: resource 123 of LS3D.DLL)
    ls3d::ErrorCallback *errorCallback; // +0x34
};
LS3D_LAYOUT32(sizeof(S_IGraphInit) == 0x38, "S_IGraphInit layout");

// S_IGraphInit::flags.
enum : uint32_t {
    IG_FULLSCREEN = 0x00000002,
    IG_TRIPLE_BUFFER = 0x00000004,      // two back buffers (fullscreen)
    IG_MENU = 0x00000008,               // window with a menu (windowed)
    IG_HW_TNL = 0x00000010,             // use hardware transform and lighting when present
    IG_WAIT_INACTIVE = 0x00000020,      // sleep in ProcessWinMessages while the window is inactive
    IG_VERBOSE = 0x00000040,            // log display modes and FSAA attempts
    IG_CHILD_WINDOW = 0x00000080,       // render into a child window of the main one
    IG_VSYNC = 0x00000100,
    IG_LOCKABLE_ZBUFFER = 0x00000400,   // D3DFMT_D16_LOCKABLE (needed by SaveShot's depth)
    IG_REFERENCE = 0x00000800,          // reference rasterizer
    IG_FSAA2 = 0x00001000,
    IG_FSAA4 = 0x00002000,
    IG_SMALL_LIGHTMAPS = 0x00004000,    // (LS3DF) lightmap atlas textures at most 256 a side (else 512)
    IG_NO_DEBUG_KEYS = 0x00010000,      // no Ctrl+Alt+Shift+F10..F12 overlays
    IG_TEXTURE_THREAD = 0x00100000,     // (LS3DF) load textures in a background thread; ignored here
};

// (No GetCaps bits, DrawPrimitive kinds or vertex formats: LS3DRF has none.)

// The vtable is LS3DRF's (0x1003e390, 13 slots): LS3DF's slots 0x00..0x20, SetAppName,
// SetAppNameF and the two timers.
class IGraph {
public:
    // 0x00 0x1006c2d0: keeps the init block and logs the system (no window, device or input).
    virtual HRESULT __stdcall Init(const S_IGraphInit *init);
    // 0x04 0x1006da30
    virtual const S_IGraphInit *__stdcall GetInitData();
    // 0x08 0x1006c2c0: replaces S_IGraphInit::flags.
    virtual HRESULT __stdcall SetFlags(uint32_t flags);
    // 0x0C 0x1006da20
    virtual bool __stdcall IsInitialized();
    // 0x10 0x1006da40: clears the initialised flag (no return value).
    virtual void __stdcall Close();
    // 0x14 0x1006a470: does nothing (no window).
    virtual void __stdcall ProcessWinMessages();
    // 0x18 / 0x1C 0x1006de50 / 0x1006de70: render target size.
    virtual int __stdcall ScrnX();
    virtual int __stdcall ScrnY();
    // 0x20 0x1006de90: S_IGraphInit::bitDepth.
    virtual int __stdcall GetBitDepth();
    // 0x24 / 0x28 0x1006a260 / 0x1006a2f0: do nothing (no window title).
    virtual void __stdcall SetAppName(const char *name);
    virtual void __cdecl SetAppNameF(const char *format, ...);
    // 0x2C 0x1006fb70: timeGetTime(), remembered.
    virtual DWORD __stdcall ReadTimer();
    // 0x30 0x1006fb80: milliseconds since the last ReadTimer (now re-read); sleeps up to at least
    // `minMs`, reports at most `maxMs` (0: no limit).
    virtual DWORD __stdcall GetTimer(DWORD minMs, DWORD maxMs);

    // (No GetImageInfo, LoadTexture, CreateITexture, SetTexture, DrawPrimitive or ResizeImage:
    // LS3DRF has none.)

    IGraph();                           // 0x10069a00
    ~IGraph();                          // 0x10069e50
};

// The engine's IGraph (0x100d16f4), created by the DLL's start-up.
extern IGraph *g_pIGraph;
extern "C" IGraph *__cdecl GetIGraph();
