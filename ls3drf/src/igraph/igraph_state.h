// IGraph state shared with the rest of the engine (the I3D driver reads the init block and the
// quirks directly, as the original did with IGraph.cpp's globals).
#pragma once

#include <cstdint>

#include "platform/win_types.h"

#include "igraph/igraph.h"

namespace ls3d::ig {

extern S_IGraphInit g_init;                     // 0x100d1d18
extern HWND g_mainWnd;                          // 0x100d19ec
extern HWND g_childWnd;                         // 0x100d1894
extern bool g_initialized;                      // 0x100d2195
extern bool g_noActivation;                     // 0x100d2191 ignore WM_ACTIVATE

extern uint32_t g_quirks;                       // 0x100d215c IGQ_* below

// (No scene flag, caps, texture formats or sizes, vertex shader, render-state cache, default
// material or texture lists: LS3DRF has none.)

// Hardware quirks (g_quirks), from the device caps and known adapters.
enum : uint32_t {
    IGQ_NO_BLENDFACTORALPHA = 0x010,
    IGQ_NO_BLENDCURRENTALPHA = 0x020,
    IGQ_NO_TEXOP_ADD = 0x040,
    IGQ_MATROX = 0x080,
    IGQ_ATI = 0x100,
    IGQ_LIMITED = 0x200,                        // no multitexturing / 256-pixel textures / old chips
    IGQ_NO_DXT = 0x008,
};

}  // namespace ls3d::ig
