// IGraph — LS3D window, Direct3D device and input front end (LS3DF.DLL, GetIGraph()).
//
// Virtual methods are __stdcall with `this` on the stack. Slots not identified yet
// are placeholders that keep the order (see i3d.h).
#pragma once

#include <cstdint>
#include <windows.h>

#include "ls3d/i3d.h"

// Parameters of IGraph::Init (0x38 bytes in the original).
struct S_IGraphInit {
    HINSTANCE instance;          // +0x00
    int adapter;                 // +0x04
    RECT windowRect;             // +0x08 window position and size
    RECT clientRect;             // +0x18
    uint32_t flags;              // +0x28 IG_INIT_* below
    uint32_t reserved[3];        // +0x2C
};

enum : uint32_t {
    IG_INIT_FULLSCREEN = 0x00000002,
    IG_INIT_TRIPLE_BUFFER = 0x00000004,  // together with fullscreen
    IG_INIT_16BIT = 0x00000010,
    IG_INIT_32BIT = 0x00000020,
    IG_INIT_DEFAULT = 0x00000080,
    IG_INIT_VSYNC = 0x00000100,
};

struct S_IGraphViewport {
    int x, y, width, height;
    float minZ, maxZ;
};

struct S_IGraphInfo {
    uint8_t unknown00[0x2C];
    uint32_t flags;              // +0x2C
};
// S_IGraphInfo::flags bit cleared while a mission loads or a saved game is written / read
// (meaning not identified).
constexpr uint32_t IGRAPH_FLAG_20 = 0x20;

// Texture image (IGraph::LoadTexture(), CreateImage()). The slots' names from the GDI fonts' use
// (part 9a-iii): an empty image made, filled, its pixels locked (512 a row) and unlocked.
class IImage {
public:
    I3DMETHOD(uint32_t, Release)() = 0;                                  // 0x00
    I3D_SLOT(04);
    // (The GDI fonts pass null, null, 0x8402, the size, 0.)
    I3DMETHOD(HRESULT, Open)(const char *fileName, const char *alphaFileName, uint32_t flags, int width,
                             int height, int unknown) = 0;              // 0x08
    I3D_SLOT(0C);
    I3D_SLOT(10);
    // The pixels (32-bit ARGB; null: none). (The GDI fonts pass 0, a pitch out, 0.)
    I3DMETHOD(void *, Lock)(int unknown1, int *pitch, int unknown2) = 0;   // 0x14
    I3DMETHOD(void, Unlock)() = 0;                                       // 0x18
    I3D_SLOT(1C);
    // The whole image `color` (the GDI fonts: 0, 0xFFFFFF, 0).
    I3DMETHOD(void, Fill)(const void *rect, uint32_t color, int unknown) = 0;   // 0x20
};

class IGraph {
public:
    I3DMETHOD(HRESULT, Init)(const S_IGraphInit &init) = 0;              // 0x00
    I3DMETHOD(const S_IGraphInfo *, GetInfo)() = 0;                      // 0x04
    I3DMETHOD(void, SetFlags)(uint32_t flags) = 0;                       // 0x08
    I3D_SLOT(0C);
    I3DMETHOD(void, Close)() = 0;                                        // 0x10
    I3DMETHOD(void, ProcessWinMessages)() = 0;                           // 0x14
    I3DMETHOD(int, ScrnX)() = 0;                                         // 0x18
    I3DMETHOD(int, ScrnY)() = 0;                                         // 0x1C
    I3D_SLOT(20);
    I3D_SLOT(24);
    I3D_SLOT(28);
    I3D_SLOT(2C);
    I3D_SLOT(30);
    // Saves the back buffer to an image file.
    I3DMETHOD(bool, SaveShot)(const char *fileName, int x, int y, int flags) = 0;   // 0x34
    // New (closed) streamed sound, used for music.
    I3DMETHOD(class ISND_stream *, CreateStream)() = 0;                 // 0x38
    // Loads a texture file (and an optional alpha mask file) into an image object (0 on
    // success, `out` gets one reference).
    I3DMETHOD(HRESULT, LoadTexture)(const char *fileName, const char *alphaFileName, uint32_t mode,
                                  class IImage **out) = 0;            // 0x3C
    // A new image, empty (the GDI fonts' textures).
    I3DMETHOD(IImage *, CreateImage)() = 0;                              // 0x40
    // (The interface passes 0, 0 before it draws its lines: no texture, inferred.)
    I3DMETHOD(void, SetTexture)(void *texture, int stage) = 0;           // 0x44
    I3DMETHOD(HWND, GetHWND)() = 0;                                      // 0x48
    I3D_SLOT(4C);
    I3DMETHOD(void, SetAppName)(const char *name) = 0;                   // 0x50
    I3D_SLOT(54);
    I3D_SLOT(58);
    I3D_SLOT(5C);
    I3D_SLOT(60);
    I3DMETHOD(void, Clear)(uint32_t color, float z, uint32_t flags) = 0; // 0x64
    // Around the loading screen's text boxes (after the scene's own Render; names inferred).
    I3DMETHOD(bool, BeginScene)() = 0;                                   // 0x68
    I3DMETHOD(void, EndScene)() = 0;                                     // 0x6C
    I3DMETHOD(bool, Flip)() = 0;                                         // 0x70
    // Render states (the interface's mission information sets state 4 to 4 while it draws).
    I3DMETHOD(void, SetRenderState)(int state, int value) = 0;          // 0x74
    I3DMETHOD(int, GetRenderState)(int state) = 0;                       // 0x78
    I3DMETHOD(void, SetViewport)(const S_IGraphViewport &viewport) = 0;  // 0x7C
    I3DMETHOD(const S_IGraphViewport *, GetViewport)() = 0;              // 0x80
    // The world matrix of the next primitives given in world space (the command map passes the
    // identity before its lines; inferred).
    I3DMETHOD(void, SetWorldMatrix)(const S_matrix &matrix) = 0;   // 0x84
    // Primitives: `kind` 3 triangles, 1 lines, 2 a line strip (the count is of primitives).
    // `transformed` 1: pre-transformed vertices (S_TLVertex, 0x20 bytes); 0: world points with a
    // colour (0x18 bytes, the command map's lines; meaning inferred).
    I3DMETHOD(void, DrawPrimitives)(int kind, int count, const void *vertices, int transformed) = 0;   // 0x88
    I3D_SLOT(8C);
    I3DMETHOD(void, InitInput)() = 0;                                    // 0x90
    I3D_SLOT(94);
    I3D_SLOT(98);
    I3DMETHOD(void, ReadKeyboard)() = 0;                                 // 0x9C
    // The key pressed in this frame (its DirectInput code: 200 up, 0xD0 down ...; 0: none).
    I3DMETHOD(int, GetAsciiKey)() = 0;                                   // 0xA0
    I3D_SLOT(A4);
    // The next character typed (8 backspace, 9 tab, 13 enter; 0: none) / forgets the typed ones.
    I3DMETHOD(uint32_t, ReadChar)() = 0;                                 // 0xA8
    I3DMETHOD(void, FlushChars)() = 0;                                   // 0xAC
    // A key (DirectInput code) is held.
    I3DMETHOD(bool, IsKeyDown)(int key) = 0;                             // 0xB0
    I3D_SLOT(B4);
    I3D_SLOT(B8);
    I3D_SLOT(BC);
    I3D_SLOT(C0);
    I3D_SLOT(C4);
    I3D_SLOT(C8);
    // The character of a key (DirectInput code; the signals panel's digits; the second argument
    // 0, not identified).
    I3DMETHOD(uint32_t, KeyToChar)(int key, int unknown) = 0;           // 0xCC
    // The DirectInput name of a keyboard key.
    I3DMETHOD(const wchar_t *, GetKeyName)(int key) = 0;                // 0xD0
    I3D_SLOT(D4);
    I3D_SLOT(D8);
    // (Meaning not identified; C_GameApp: 1 when a client starts, 0 for the console.)
    I3DMETHOD(void, UnknownDC)(int on) = 0;                              // 0xDC
    I3D_SLOT(E0);
    // (Not identified; C_GameApp::Close on a client.)
    I3DMETHOD(void, UnknownE4)() = 0;                                    // 0xE4
    // Called once per frame before the game update (input/timer housekeeping).
    I3DMETHOD(void, BeginFrame)() = 0;                                   // 0xE8
    // The mouse's movement in this frame, in counts (inferred from the player-driven meleeTracker's
    // mouse look, 2 pi per 1000 counts).
    I3DMETHOD(int, GetMouseDeltaX)() = 0;                                // 0xEC
    I3DMETHOD(int, GetMouseDeltaY)() = 0;                                // 0xF0
    I3D_SLOT(F4);
    I3D_SLOT(F8);
    I3D_SLOT(FC);
    I3D_SLOT(100);
    // Mouse buttons 0 .. 4 as bits (LS3DF's name).
    I3DMETHOD(uint32_t, GetMouseButtons)() = 0;                          // 0x104
    I3DMETHOD(void, Unknown108)() = 0;                                   // 0x108
    I3D_SLOT(10C);
    // Adds a texture search directory.
    I3DMETHOD(void, AddDir)(const char *dir, uint32_t flags) = 0;        // 0x110
    I3D_SLOT(114);
    I3D_SLOT(118);
    I3D_SLOT(11C);
    I3D_SLOT(120);
    I3D_SLOT(124);
    I3D_SLOT(128);
    // Sets the display's gamma ramp: 3 x 256 words (red, green, blue).
    I3DMETHOD(void, SetGammaRamp)(const uint16_t *ramp, uint32_t flags) = 0;   // 0x12C
};

// LS3D sound driver (ISndGetDriver()).
class ISND_driver {
public:
    I3D_SLOT(00);
    I3DMETHOD(HRESULT, Init)(HWND window, int frequency, int channels, int bits, uint32_t flags) = 0;  // 0x04
    I3DMETHOD(void, Close)() = 0;                                        // 0x08
    // False when there is no sound device (sounds are not created then).
    I3DMETHOD(bool, IsInitialized)() = 0;                                // 0x0C
    I3D_SLOT(10);
    I3DMETHOD(void, SetVolume)(float volume) = 0;                        // 0x14 master volume
};

// Streamed sound file (music), created by IGraph::CreateStream().
class ISND_stream {
public:
    I3D_SLOT(00);
    I3DMETHOD(uint32_t, Release)() = 0;                                  // 0x04
    I3DMETHOD(bool, IsPlaying)() = 0;                                    // 0x08
    // flags: ISNDSTREAM_LOOP.
    I3DMETHOD(HRESULT, Open)(const char *fileName, uint32_t flags) = 0;  // 0x0C
    I3DMETHOD(void, Close)() = 0;                                        // 0x10
    I3DMETHOD(void, Play)() = 0;                                         // 0x14
    I3DMETHOD(void, Stop)() = 0;                                         // 0x18
    I3D_SLOT(1C);
    I3D_SLOT(20);
    I3D_SLOT(24);
    I3D_SLOT(28);
    // 1 once a non-looping stream has played to its end.
    I3DMETHOD(int, GetState)() = 0;                                      // 0x2C
    I3DMETHOD(void, SetPosition)(int position) = 0;                      // 0x30
    I3D_SLOT(34);
    I3DMETHOD(void, GetPosition)(int *position) = 0;                     // 0x38
    I3DMETHOD(void, SetVolume)(float volume) = 0;                        // 0x3C
    I3D_SLOT(40);
    I3D_SLOT(44);
    // A video's (.bik) picture: a pointer to its texture (the HUD's injury flash).
    I3DMETHOD(class I3D_texture **, GetVideoTexture)() = 0;            // 0x48
    // Decodes the video's next picture (the injury flash passes 0; inferred).
    I3DMETHOD(void, UpdateVideo)(int unknown) = 0;                       // 0x4C
};

enum : uint32_t {
    ISNDSTREAM_LOOP = 0x02,
};
enum {
    ISNDSTREAM_STATE_FINISHED = 1,
};

extern "C" LS3D_API IGraph *GetIGraph();
extern "C" LS3D_API ISND_driver *ISndGetDriver();
// Debug output to the engine log (printf style).
extern "C" LS3D_API void dbgPrintf(const char *format, ...);
extern "C" LS3D_API void dbgPrintf2(const char *format, ...);
// Checks the engine DLL version against the one the game was built for.
extern "C" LS3D_API bool TEV(const char *who, int version);
extern "C" LS3D_API uint32_t GetEngineVersion();
