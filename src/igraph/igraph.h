// IGraph: the engine's init block, timer and texture front end
// (IGraph.cpp in the original source tree, 0x100699a0 - 0x10070eb0, vtable 0x1009d840).
//
// One object exists (GetIGraph()); it holds nothing but its vtable, all state is file-level. Every
// method is __stdcall with `this` on the stack (the slot order is the DLL's ABI), except the
// printf-style SetAppNameF, which MSVC makes __cdecl.
#pragma once

#include <cstdint>

#include <windows.h>

#include "core/debug.h"
#include "dx/d3d8_types.h"

struct S_matrix;
class ITexture;

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
static_assert(sizeof(S_IGraphInit) == 0x38, "S_IGraphInit layout");

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
    IG_TEXTURE_THREAD = 0x00100000,     // load textures in a background thread
};

// IGraph::GetCaps() bits (0x100d1dac).
enum : uint32_t {
    IGCAPS_HW_TNL = 0x001,
    IGCAPS_TABLE_FOG = 0x004,
    IGCAPS_DXT = 0x008,                 // at least one DXTn texture format
    IGCAPS_STENCIL = 0x010,             // the depth buffer has stencil bits
    IGCAPS_GAMMA = 0x020,               // fullscreen gamma ramp
    IGCAPS_GAMMA_8BIT = 0x040,          // the driver's ramp holds 8-bit values (GetGammaRamp widens)
    IGCAPS_HW_VERTEX_SHADERS = 0x080,
    IGCAPS_BUMPMAP = 0x100,             // D3DFMT_V8U8 textures
};

// DrawPrimitive kinds and vertex formats.
enum {
    IGP_POINTS = 0,                     // (6 too)
    IGP_LINES = 1,
    IGP_LINESTRIP = 2,
    IGP_TRIANGLES = 3,
    IGP_TRIANGLEFAN = 4,
    IGP_TRIANGLESTRIP = 5,
};
enum {
    IGV_COLOR_TEX = 0,                  // position, colour, uv (0x18 bytes, vertex shader 0x100d21ac)
    IGV_TRANSFORMED = 1,                // D3DFVF_XYZRHW | DIFFUSE | SPECULAR | TEX1 (0x20 bytes)
    IGV_TRANSFORMED_TEX2 = 2,           // D3DFVF_XYZRHW | DIFFUSE | SPECULAR | TEX2 (0x28 bytes)
};

// The vtable is LS3DRF's (0x1003e390, 13 slots): LS3DF's slots 0x00..0x20, SetAppName,
// SetAppNameF and the two timers. The LS3DF methods the engine itself still calls follow as plain
// members; the DLL's users cannot reach them.
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

    // 0x1006dec0: size and depth of a .bmp / .tga file; `handle` (if given) keeps the file open
    // at its start.
    HRESULT __stdcall GetImageInfo(const char *fileName, uint16_t *width, uint16_t *height,
                                   uint8_t *bitDepth, uint32_t flags, int *handle);
    // 0x10069f70: loads a texture (shared with earlier loads of the same files and flags).
    HRESULT __stdcall LoadTexture(const char *fileName, const char *alphaFileName, uint32_t flags,
                                  ITexture **out);
    // 0x10069ee0: an empty texture object.
    ITexture *__stdcall CreateITexture();
    // 0x1006a1f0
    bool __stdcall SetTexture(ITexture *texture, int stage);
    // 0x1006b380 / 0x1006b3b0
    HRESULT __stdcall BeginScene();
    HRESULT __stdcall EndScene();
    // 0x1006b360
    HRESULT __stdcall SetWorldMatrix(const S_matrix *matrix);
    // 0x1006b5a0 / 0x1006b6e0: user-memory primitives (IGP_*, IGV_*).
    HRESULT __stdcall DrawPrimitive(int kind, int count, const void *vertices, int vertexFormat);
    HRESULT __stdcall DrawIndexedPrimitive(int kind, int numVertices, const uint16_t *indices, int count,
                                           const void *vertices, int vertexFormat);
    // 0x10070160: resamples a 24 / 32-bit image (box filter when shrinking and `filter`).
    bool __stdcall ResizeImage(int srcWidth, int srcHeight, int srcPitch, const uint8_t *src, int dstWidth,
                               int dstHeight, int dstPitch, uint8_t **dst, int bytesPerPixel, bool filter);

    IGraph();                           // 0x10069a00
    ~IGraph();                          // 0x10069e50
};

// The engine's IGraph (0x100d16f4), created by the DLL's start-up.
extern IGraph *g_pIGraph;
extern "C" IGraph *__cdecl GetIGraph();
