// IGraph state shared with the rest of the engine (the I3D renderer reads the formats and the
// render-state cache directly, as the original did with IGraph.cpp's globals; it draws through the
// render device, gfx::g_pGfx).
#pragma once

#include <cstdint>

#include <windows.h>

#include "core/c_vector.h"
#include "core/i3d_result.h"
#include "dx/d3d8_types.h"
#include "igraph/igraph.h"

class ITexture;

namespace ls3d::ig {

extern S_IGraphInit g_init;                     // 0x100d1d18
extern HWND g_mainWnd;                          // 0x100d19ec
extern HWND g_childWnd;                         // 0x100d1894
extern bool g_initialized;                      // 0x100d2195
extern bool g_inScene;                          // 0x100d2194 between BeginScene and EndScene
extern bool g_noActivation;                     // 0x100d2191 ignore WM_ACTIVATE

extern uint32_t g_caps;                         // 0x100d1dac IGCAPS_*
extern uint32_t g_quirks;                       // 0x100d215c IGQ_* below
extern int g_maxTextureWidth;                   // 0x100d2128
extern int g_maxTextureHeight;                  // 0x100d1ce8

// Texture formats chosen in Init (D3DFMT_UNKNOWN: not available).
extern D3DFORMAT g_fmt16Bit;                    // 0x100d1da8 R5G6B5 / X1R5G5B5 / A1R5G5B5
extern D3DFORMAT g_fmt1BitAlpha;                // 0x100d1a74 A1R5G5B5
extern D3DFORMAT g_fmtAlpha;                    // 0x100d1a78 A4R4G4B4 / A8R8G8B8
extern D3DFORMAT g_fmtTrueColor;                // 0x100d1774 A8R8G8B8
extern D3DFORMAT g_fmtDxt1;                     // 0x100d1898
extern D3DFORMAT g_fmtDxt3;                     // 0x100d1edc
extern D3DFORMAT g_fmtDxt5;                     // 0x100d1a90
extern D3DFORMAT g_fmtBump;                     // 0x100d1d10 V8U8

// The vertex shader of IGV_COLOR_TEX (a declaration of D3DVSD_REG(0) position, ..., created by the
// device reset).
extern DWORD g_vsColorTex1;                     // 0x100d21ac position, colour, uv

// The renderer's cache of device states (IGraph::SetState's in LS3DF; -1 / unset values force the
// next set).
struct S_stateCache {
    int alpha;                                  // 0x100d1ef0 IGS_ALPHA
    int blend;                                  // 0x100d1a88 IGS_BLEND
    int cull;                                   // 0x100d1cec IGS_CULL
    int cullDefault;                            // 0x100d2130 D3DCULL that IGS_CULL 3 means
    int colorOp[3];                             // 0x100d1a70, 0x100d214c, 0x100d2150
    int alphaOp[2];                             // 0x100d2160, 0x100d1f04
    int zBias;                                  // 0x100d177c
    int addressU0;                              // 0x100d2170
    int addressV0;                              // 0x100d2174
    int alphaRef;                               // 0x100a7800 (0x20 at start)
    int zFunc;                                  // 0x100a7808 (1 at start)
    int zWrite;                                 // 0x100d1d14
    int fog;                                    // 0x100d2120 D3DRS_FOGENABLE
    int specular;                               // 0x100d19c4
    int vertexShader;                           // 0x100d1a98 the renderer's vertex format index
                                                // (DrawPrimitive: 2 colour+uv, 6 / 7 transformed)
    int fogMode;                                // 0x100d1d64 0 linear, 1 / 2 exponential
    float fogStart;                             // 0x100d2154 (density for exponential fog)
    float fogEnd;                               // 0x100d1d08
    uint32_t fogColor;                          // 0x100d1778
    int unknown19c0;                            // 0x100d19c0 (renderer, 0x10037010)
    int unknown1888;                            // 0x100d1888 (renderer, 0x10037010)
    int unknown1780;                            // 0x100d1780 (renderer, 0x10037010)
};
extern S_stateCache g_rs;

// The renderer's material for meshes without colors of their own (0x100d1ba0: white diffuse,
// ambient and specular, no emissive).
extern D3DMATERIAL8 g_defaultMaterial;

// Textures (LoadTexture shares the cached ones).
extern C_vector<ITexture *> g_cachedTextures;   // 0x100d19dc
extern C_vector<ITexture *> g_uncachedTextures; // 0x100d19cc
extern ITexture *g_defaultTexture;              // 0x100d21b8 8 x 8 white
extern uint16_t g_textureId;                    // 0x100d2192 last ITexture::id given out

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

// IGraph::ResizeImage (resize_image.cpp).
bool ResizeImage(int srcWidth, int srcHeight, int srcPitch, const uint8_t *src, int dstWidth, int dstHeight,
                 int dstPitch, uint8_t **dst, int bpp, bool filter);

}  // namespace ls3d::ig
