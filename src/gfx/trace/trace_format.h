// The frame capture format (.ls3dframe): one frame of the render device's calls with everything they
// need, written by the recorder (gfx/trace, LS3DF.ini [Render] CaptureFrames=1) and replayed by the
// tests on each backend to compare their pictures.
//
// A file is the magic "LS3DFRM1", then records: a u32 operation (E_op), a u32 payload size, the
// payload. All values are little-endian, as the engine's structures (D3DVIEWPORT8, D3DLIGHT8, ...)
// are stored in memory. A capture starts with HEADER, then the state the engine had set (each a
// SET_* record) and the resources that state names, then the frame's calls up to and including
// PRESENT. A resource is defined (DEF_*) before the first record that names it; its id is the
// handle the engine had for it during the capture (a released one is DROPped, so an id is never
// two resources at once).
#pragma once

#include <cstdint>

#include "dx/d3d8_types.h"

namespace ls3d::gfx::trace {

constexpr char kMagic[8] = {'L', 'S', '3', 'D', 'F', 'R', 'M', '1'};

enum E_op : uint32_t {
    // u32 width, height, back buffer format, depth format (0: none), multisample type, windowed,
    // flags (the present parameters' D3DPRESENTFLAG_*)
    OP_HEADER = 1,
    OP_RENDER_STATE = 2,            // u32 state, value
    OP_STAGE_STATE = 3,             // u32 stage, type, value
    OP_TEXTURE = 4,                 // u32 stage, texture id (0: none)
    OP_TRANSFORM = 5,               // u32 state, D3DMATRIX
    OP_VIEWPORT = 6,                // D3DVIEWPORT8
    OP_MATERIAL = 7,                // D3DMATERIAL8
    OP_LIGHT = 8,                   // u32 index, D3DLIGHT8
    OP_LIGHT_ENABLE = 9,            // u32 index, enable
    OP_DEF_SHADER = 10,             // u32 handle, usage, declaration count, function count, the DWORDs
    OP_VERTEX_SHADER = 11,          // u32 handle (an FVF code or a DEF_SHADER's)
    OP_SHADER_CONSTANT = 12,        // u32 register, count, count * 4 floats
    OP_STREAM = 13,                 // u32 stream, vertex buffer id (0: none), stride
    OP_INDICES = 14,                // u32 index buffer id (0: none), base vertex index
    OP_DEF_TEXTURE = 15,            // u32 id, width, height, levels, usage, format, pool
    OP_TEXTURE_DATA = 16,           // u32 id, level, left, top, right, bottom, then the rows (FormatRows)
    OP_DEF_VERTEX_BUFFER = 17,      // u32 id, length, usage, fvf, pool, then length bytes
    OP_DEF_INDEX_BUFFER = 18,       // u32 id, length, usage, format, pool, then length bytes
    OP_VERTEX_DATA = 19,            // u32 id, offset, then the bytes written there
    OP_INDEX_DATA = 20,             // u32 id, offset, then the bytes
    OP_DROP = 21,                   // u32 id: a resource released
    OP_CLEAR = 22,                  // u32 count, count * D3DRECT, u32 flags, color, float z, u32 stencil
    OP_BEGIN_SCENE = 23,
    OP_END_SCENE = 24,
    OP_DRAW_INDEXED = 25,           // u32 type, min index, vertex count, start index, primitive count
    OP_DRAW_UP = 26,                // u32 type, primitive count, stride, then the vertices
    // u32 type, min index, vertex count, primitive count, index format, stride, index bytes, then the
    // indices and the vertices (from vertex 0: (min index + vertex count) * stride bytes)
    OP_DRAW_INDEXED_UP = 27,
    OP_PRESENT = 28,
    OP_GAMMA = 29,                  // u32 flags, D3DGAMMARAMP
    // The picture the game drew (before PRESENT): u32 width, height, format, then the rows (FormatRows)
    OP_FRAME_IMAGE = 30,
};

// The bytes of a row and the number of rows of a `width` x `height` image in `format` (block rows for
// DXT); false for a format the capture does not know.
inline bool FormatRows(D3DFORMAT format, uint32_t width, uint32_t height, uint32_t *rowBytes, uint32_t *rows)
{
    uint32_t bpp = 0;                       // bytes per pixel, or per 4x4 block
    bool block = false;
    switch (format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_G16R16:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_V16U16:
    case D3DFMT_W11V11U10:
    case D3DFMT_A2W10V10U10:
        bpp = 4;
        break;
    case D3DFMT_R8G8B8:
        bpp = 3;
        break;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8P8:
    case D3DFMT_A8L8:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
        bpp = 2;
        break;
    case D3DFMT_R3G3B2:
    case D3DFMT_A8:
    case D3DFMT_P8:
    case D3DFMT_L8:
    case D3DFMT_A4L4:
        bpp = 1;
        break;
    case D3DFMT_DXT1:
        bpp = 8;
        block = true;
        break;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        bpp = 16;
        block = true;
        break;
    default:
        return false;
    }
    if (block) {
        *rowBytes = (width + 3) / 4 * bpp;
        *rows = (height + 3) / 4;
    } else {
        *rowBytes = width * bpp;
        *rows = height;
    }
    return true;
}

// The vertices a draw of `count` primitives of `type` uses.
inline uint32_t PrimitiveVertices(D3DPRIMITIVETYPE type, uint32_t count)
{
    switch (type) {
    case D3DPT_POINTLIST: return count;
    case D3DPT_LINELIST: return count * 2;
    case D3DPT_LINESTRIP: return count + 1;
    case D3DPT_TRIANGLELIST: return count * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return count + 2;
    default: return 0;
    }
}

}  // namespace ls3d::gfx::trace
