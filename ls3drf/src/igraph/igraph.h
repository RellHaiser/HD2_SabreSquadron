// IGraph: the engine's window, Direct3D 8 device, input and texture front end
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
class IShow;

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

// SetState / GetState indices and their values.
enum {
    IGS_ALPHA = 0,                      // 0 opaque, 1 blend + alpha test >= 1, 2 blend + test >= 0, 3 test only
    IGS_BLEND = 1,                      // 0 alpha, 1 additive, 2 one+one, 3 modulate, 4 modulate 2x
    IGS_CULL = 3,                       // D3DCULL; 3 = the default
    IGS_COLOROP0 = 4,                   // D3DTOP of texture stages 0..2
    IGS_COLOROP1 = 5,
    IGS_COLOROP2 = 6,
    IGS_ALPHAOP0 = 7,                   // D3DTOP of the alpha of stages 0, 1
    IGS_ALPHAOP1 = 8,
    IGS_ZBIAS = 9,
    IGS_ADDRESSU0 = 10,                 // D3DTADDRESS of stage 0 (11 sets the same state)
    IGS_ADDRESSV0 = 11,
    IGS_ALPHAREF = 12,
    IGS_ZFUNC = 13,                     // 0 always, else less-equal
    IGS_ZWRITE = 14,
    IGS_ZENABLE = 15,
    IGS_CLAMP0 = 16,                    // stage 0 / 1 texture coordinates clamped (1) or wrapped
    IGS_CLAMP1 = 17,
    IGS_FOG = 18,
    IGS_SPECULAR = 19,
    IGS_COLORWRITE = 20,
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

struct S_IGraphViewport {               // = D3DVIEWPORT8
    int x, y, width, height;
    float minZ, maxZ;
};

// The vtable is LS3DRF's (0x1003e390, 13 slots): LS3DF's slots 0x00..0x20, SetAppName,
// SetAppNameF and the two timers. LS3DF's other methods follow as plain members, for the engine's
// own calls; the DLL's users cannot reach them.
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

    // 0x1006deb0: IGCAPS_* bits.
    uint32_t __stdcall GetCaps();
    // 0x1006dea0
    D3DFORMAT __stdcall GetBackBufferFormat();
    // 0x1006bb80: "D3DFMT_..." text of a format.
    const char *__stdcall GetFormatName(D3DFORMAT format);
    // 0x1006dec0: size and depth of a .bmp / .tga file; `handle` (if given) keeps the file open
    // at its start.
    HRESULT __stdcall GetImageInfo(const char *fileName, uint16_t *width, uint16_t *height,
                                   uint8_t *bitDepth, uint32_t flags, int *handle);
    // 0x1006e210: the back buffer to a .tga file, or to memory (IGSHOT_MEMORY); with
    // IGSHOT_DEPTH the depth buffer too (an .rla file next to the .tga).
    bool __stdcall SaveShot(void *fileNameOrBuffer, const RECT *rect, uint32_t flags, void *depthBuffer);
    // 0x10069e80: a video player; null (no IShow in this build).
    IShow *__stdcall CreateIShow();
    // 0x10069f70: loads a texture (shared with earlier loads of the same files and flags).
    HRESULT __stdcall LoadTexture(const char *fileName, const char *alphaFileName, uint32_t flags,
                                  ITexture **out);
    // 0x10069ee0: an empty texture object.
    ITexture *__stdcall CreateITexture();
    // 0x1006a1f0
    bool __stdcall SetTexture(ITexture *texture, int stage);
    // 0x1006a240 / 0x1006a250
    HWND __stdcall GetMainHWND();
    HWND __stdcall GetChildHWND();
    // 0x1006a330
    const char *__stdcall GetAppName();
    // 0x1006a340 / 0x1006a3d0: modeless dialogs that get their messages
    // (IsDialogMessage) in ProcessWinMessages.
    bool __stdcall AddDlg(HWND dialog);
    bool __stdcall DelDlg(HWND dialog);
    // 0x1006a6c0: flags 1 colour, 2 depth (and stencil when present).
    HRESULT __stdcall Clear(D3DCOLOR color, float z, uint32_t flags);
    // 0x1006b380 / 0x1006b3b0
    HRESULT __stdcall BeginScene();
    HRESULT __stdcall EndScene();
    // 0x1006b3e0: presents the frame; restores a lost device.
    HRESULT __stdcall Flip();
    // 0x1006abc0 / 0x1006b220: cached render states (IGS_*).
    HRESULT __stdcall SetState(int state, int value);
    int __stdcall GetState(int state);
    // 0x1006c270 / 0x1006c2b0
    HRESULT __stdcall SetViewport(const S_IGraphViewport *viewport);
    const S_IGraphViewport *__stdcall GetViewport();
    // 0x1006b360
    HRESULT __stdcall SetWorldMatrix(const S_matrix *matrix);
    // 0x1006b5a0 / 0x1006b6e0: user-memory primitives (IGP_*, IGV_*).
    HRESULT __stdcall DrawPrimitive(int kind, int count, const void *vertices, int vertexFormat);
    HRESULT __stdcall DrawIndexedPrimitive(int kind, int numVertices, const uint16_t *indices, int count,
                                           const void *vertices, int vertexFormat);
    // 0x1006f040: keyboard; flags IGKEY_* below.
    HRESULT __stdcall InitKeyboard(uint32_t flags);
    // 0x1006f150
    bool __stdcall IsKeyboardInit();
    // 0x1006f160
    HRESULT __stdcall CloseKeyboard();
    // 0x1006f190: polls the keyboard (once per frame).
    HRESULT __stdcall ReadKeyboard();
    // 0x1006f320: the key pressed in this frame (DIK_ code; repeats with IGKEY_REPEAT).
    int __stdcall GetKey();
    // 0x1006f470: the character of the last GetKey(), once.
    int __stdcall GetKeyChar();
    // 0x1006f2d0: the next typed character (WM_CHAR), 0 if none.
    int __stdcall ReadChar();
    // 0x1006f300
    HRESULT __stdcall ClearChars();
    // 0x1006f490
    bool __stdcall IsKeyDown(int key);
    // 0x1006f4b0 / 0x1006f500: up to 64 keys whose state GetMappedKeys() returns as bits
    // (a 64-bit mask in EDX:EAX: the game reads both halves).
    HRESULT __stdcall SetKeyMap(const uint8_t *keys, uint32_t count);
    uint64_t __stdcall GetMappedKeys();
    // 0x1006f550..: either shift / control / alt key down.
    bool __stdcall IsShiftDown();
    bool __stdcall IsCtrlDown();
    bool __stdcall IsAltDown();
    // 0x1006f5b0: IGSHIFT_* bits; `read` polls the keyboard first.
    uint32_t __stdcall GetShiftKeys(bool read);
    // 0x1006f660: the character of a key (shift bit 1 of `shiftKeys`: the shifted one).
    uint8_t __stdcall KeyToChar(int key, uint32_t shiftKeys);
    // 0x1006f6a0: DirectInput's name of a key (DIPROP_KEYNAME; 0 without a keyboard).
    const wchar_t *__stdcall GetKeyName(int key);
    // 0x10057630 (an engine-wide "return 0" stub)
    int __stdcall ReservedD4();
    // 0x1006f700 (does nothing)
    bool __stdcall ReservedD8(int, int);
    // 0x1006f710: mouse; mode 0 absolute (cursor), 1 exclusive relative; -1 queries the mode.
    int __stdcall InitMouse(int mode);
    // 0x1006f840
    bool __stdcall IsMouseInit();
    // 0x1006f850
    HRESULT __stdcall CloseMouse();
    // 0x1006f8a0: polls the mouse (once per frame).
    HRESULT __stdcall ReadMouse();
    // 0x1006fa10..: movement since the last ReadMouse (DZ: wheel).
    int __stdcall GetMouseDX();
    int __stdcall GetMouseDY();
    int __stdcall GetMouseDZ();
    // 0x1006fa40: the position (moves the cursor in absolute mode).
    int __stdcall SetMousePos(int x, int y);
    // 0x1006fb10 / 0x1006fb20
    int __stdcall GetMouseX();
    int __stdcall GetMouseY();
    // 0x1006fb30: buttons 0..4 as bits.
    uint32_t __stdcall GetMouseButtons();
    // 0x1006ff80: a texture search directory (with its subdirectories); created if missing.
    HRESULT __stdcall AddDir(const char *dir, bool recursive);
    // 0x1006ffa0: removes the directories whose path contains `text`.
    HRESULT __stdcall DelDir(const char *text);
    // 0x10070000
    void __stdcall DelAllDirs();
    // 0x10070060..: the texture lists (cached = shared by LoadTexture).
    ITexture **__stdcall GetCachedTextures();
    int __stdcall NumCachedTextures();
    ITexture **__stdcall GetUncachedTextures();
    int __stdcall NumUncachedTextures();
    // 0x100700a0 / 0x100700e0: 3 x 256 words, red, green, blue (IGCAPS_GAMMA).
    bool __stdcall SetGammaRamp(const uint16_t *ramp, bool calibrate);
    bool __stdcall GetGammaRamp(uint16_t *ramp);
    // 0x10070160: resamples a 24 / 32-bit image (box filter when shrinking and `filter`).
    bool __stdcall ResizeImage(int srcWidth, int srcHeight, int srcPitch, const uint8_t *src, int dstWidth,
                               int dstHeight, int dstPitch, uint8_t **dst, int bytesPerPixel, bool filter);

    IGraph();                           // 0x10069a00
    ~IGraph();                          // 0x10069e50
};

// SaveShot flags.
enum : uint32_t {
    IGSHOT_MEMORY = 0x00000001,         // copy into the caller's buffer instead of writing a file
    IGSHOT_RGB24 = 0x00020000,          // memory: convert to 24-bit BGR
    IGSHOT_DEPTH = 0x00100000,          // the depth buffer too (needs IG_LOCKABLE_ZBUFFER)
};

// InitKeyboard flags.
enum : uint32_t {
    IGKEY_EXCLUSIVE = 0x1,
    IGKEY_WINDOW_MESSAGES = 0x2,        // keys from WM_KEYDOWN / WM_CHAR instead of DirectInput
    IGKEY_REPEAT = 0x4,                 // GetKey repeats a held key (350 ms, then every 34 ms)
};

// GetShiftKeys bits.
enum : uint32_t {
    IGSHIFT_SHIFT = 0x001,
    IGSHIFT_CTRL = 0x004,
    IGSHIFT_ALT = 0x008,
    IGSHIFT_LSHIFT = 0x020,
    IGSHIFT_RSHIFT = 0x040,
    IGSHIFT_LCTRL = 0x080,
    IGSHIFT_RCTRL = 0x100,
    IGSHIFT_LALT = 0x200,
    IGSHIFT_RALT = 0x400,
};

// The engine's IGraph (0x100d16f4), created by the DLL's start-up.
extern IGraph *g_pIGraph;
extern "C" IGraph *__cdecl GetIGraph();
