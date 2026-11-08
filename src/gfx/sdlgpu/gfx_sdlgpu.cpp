// The SDL_GPU backend of the render device (gfx/gfx.h): Direct3D 8's fixed-function model drawn
// with SDL_GPU (Direct3D 12 or Vulkan on Windows, Vulkan on Linux, Metal on macOS). Chosen with
// LS3DF.ini [Render] SDLGPU=1 (gfx/gfx.cpp); SDL3.dll is loaded at run time.
//
// - The fixed-function pipeline is one vertex and one pixel shader (shaders/fixed_function.hlsl)
//   driven by uniforms the device's states fill: transforms, lights, material, fog, texture coordinate
//   generation, the texture stages, alpha test. What a GPU pipeline fixes (blending, depth, culling,
//   the vertex format, the primitive type) makes the pipeline, cached by those settings.
// - Resources live in memory as Direct3D's do: a texture's levels in their format (the engine locks
//   and writes them at any time), a buffer's bytes. A texture goes to the GPU when a frame first draws
//   with it after a change, converted to a GPU format if it has no equal (16-bit formats become 32-bit
//   BGRA; DXT stays BC).
// - A frame is recorded and run when its picture is needed (Present, a read-back): each draw keeps the
//   vertices and indices it uses, copied out of the buffers at the draw, so the engine may rewrite a
//   buffer mid-frame as Direct3D allows. At run time one copy pass uploads the frame's vertices,
//   indices and changed textures, then one render pass draws it.
// - The back buffer is a texture of the device's size; Present scales it into the window's swapchain.
// - Direct3D 8's rules kept: a draw from user memory unsets stream 0 (and the indices); a stage
//   without a texture reads white; Clear without rectangles clears the viewport.
#include "gfx/sdlgpu/sdlgpu_device.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "core/enhance.h"
#include "gfx/trace/trace_format.h"
#include "platform/sdl3/sdl3_api.h"
#include "sdlgpu_shaders.h"

namespace ls3d::gfx {

namespace {

using sdl3::api;
using trace::FormatRows;
using trace::PrimitiveVertices;

constexpr HRESULT D3D_OK = S_OK;
constexpr int kStages = 3;          // texture stages the shaders draw (the engine uses up to 3)
// The pixel shader's sampler slots: one more than the stages. SDL 3.4's Direct3D 12 backend checks
// only that a GPU descriptor heap (2048 samplers, 65536 views) is not yet full before it writes a
// draw's descriptors, not that they all fit: with 3 a draw, a write straddles the heap's end and
// fails (CopyDescriptorsSimple, an invalid handle). 4 divides both sizes.
constexpr int kSamplerSlots = 4;
constexpr int kMaxLights = 8;

// ---------------------------------------------------------------- uniforms (fixed_function.hlsl)

struct alignas(16) F4 {
    float x, y, z, w;
};
struct alignas(16) I4 {
    int32_t x, y, z, w;
};
struct S_lightUniform {
    F4 diffuse, specular, ambient, position, direction, rangeFalloffAtt0Att1, att2CosThetaCosPhiType;
};
struct S_vertexUniforms {
    float worldView[16];
    float projection[16];
    float textureMatrix[kStages][16];
    F4 rows[4];
    F4 viewport;
    F4 pixelOffset;
    I4 mode;
    I4 texGen[kStages];
    F4 fog;
    F4 globalAmbient;
    F4 materialDiffuse, materialAmbient, materialSpecular, materialEmissive;
    F4 materialPower;
    I4 materialSource;
    I4 lightCount;
    S_lightUniform lights[kMaxLights];
};
struct S_pixelUniforms {
    I4 colorStage[kStages];
    I4 alphaStage[kStages];
    F4 bumpMatrix[kStages];
    F4 textureFactor;
    F4 fogColor;
    I4 pixelFlags;
};

// mode.w flags
enum : int32_t {
    F_NORMAL = 1,
    F_DIFFUSE = 2,
    F_SPECULAR = 4,
    F_NORMALIZE = 8,
    F_LOCALVIEWER = 16,
    F_SPECULARENABLE = 32,
};

F4 Color(D3DCOLOR c)
{
    return {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f, (c >> 24) / 255.0f};
}
F4 Color(const D3DCOLORVALUE &c) { return {c.r, c.g, c.b, c.a}; }
float AsFloat(DWORD d)
{
    float f;
    std::memcpy(&f, &d, 4);
    return f;
}

// Row-vector matrices: a * b.
D3DMATRIX Multiply(const D3DMATRIX &a, const D3DMATRIX &b)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}
D3DMATRIX Identity()
{
    D3DMATRIX m = {};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1;
    return m;
}
F4 TransformPoint(const D3DVECTOR &v, const D3DMATRIX &m)
{
    return {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2], 1};
}
F4 TransformDirection(const D3DVECTOR &v, const D3DMATRIX &m)
{
    F4 d = {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0], v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2], 0};
    const float n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (n > 0)
        d = {d.x / n, d.y / n, d.z / n, 0};
    return d;
}

// ---------------------------------------------------------------- the diagnostic log

// LS3DF_sdlgpu.log next to the DLL, with [Render] SDLGPUDebug=1: what the backend asks of SDL_GPU,
// each line flushed before the call (a driver that crashes leaves the last one as the culprit).
void Trace(const char *format, ...)
{
    static FILE *log;
    static bool opened;
    if (!Enhancements().sdlGpuDebug)
        return;
    if (!opened) {
        opened = true;
#ifdef _WIN32
        HMODULE module = nullptr;
        char path[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&Trace), &module) &&
            GetModuleFileNameA(module, path, MAX_PATH)) {
            if (char *slash = std::strrchr(path, '\\')) {
                slash[1] = 0;
                if (std::strlen(path) + 20 < MAX_PATH) {
                    std::strcat(path, "LS3DF_sdlgpu.log");
                    log = std::fopen(path, "w");
                }
            }
        }
#endif
    }
    if (!log)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(log, format, args);
    va_end(args);
    std::fputc('\n', log);
    std::fflush(log);
}

// ---------------------------------------------------------------- resources

struct S_texture;

struct S_surface {
    ULONG refs = 1;                 // an image surface's own (a level's are its texture's)
    S_texture *texture = nullptr;   // a texture's level (null: an image surface, or the back / depth buffer)
    UINT level = 0;
    UINT width = 0, height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    std::vector<uint8_t> data;      // an image surface's rows (FormatRows)
    bool backBuffer = false, depthBuffer = false;
    bool locked = false;
    RECT lockedRect = {};
};

struct S_texture {
    ULONG refs = 1;
    UINT width, height, levels;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    std::vector<std::vector<uint8_t>> data;    // each level's rows in `format` (FormatRows)
    std::vector<S_surface> surfaces;            // each level as a surface
    SDL_GPUTexture *gpu = nullptr;
    SDL_GPUTextureFormat gpuFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool dirty = true;                          // the GPU's copy is older than `data`
};

struct S_buffer {
    std::vector<uint8_t> data;
    DWORD usage = 0, fvf = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    ULONG refs = 1;
};

// A vertex format's attributes (location: 0 position, 1 normal, 2 diffuse, 3 specular, 4 and 5 the
// texture coordinate sets), and what the shaders need to know about it.
struct S_layout {
    struct S_attribute {
        uint32_t location, offset;
        SDL_GPUVertexElementFormat format;
    };
    std::vector<S_attribute> attributes;
    bool pretransformed = false, programmable = false;
    int32_t flags = 0;              // F_NORMAL, F_DIFFUSE, F_SPECULAR
    int rowRegister[4] = {0, 1, 2, 3};      // programmable: the constants that make oPos.x, y, z, w
    uint32_t Key() const
    {
        uint32_t h = 2166136261u ^ (pretransformed ? 1u : 0u) ^ (programmable ? 2u : 0u);
        for (const S_attribute &a : attributes)
            h = (h ^ (a.location | a.offset << 4 | static_cast<uint32_t>(a.format) << 20)) * 16777619u;
        return h;
    }
};

S_layout LayoutOfFvf(DWORD fvf)
{
    S_layout l;
    uint32_t offset = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZRHW:
        l.attributes.push_back({0, offset, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4});
        l.pretransformed = true;
        offset += 16;
        break;
    case D3DFVF_XYZ:
        l.attributes.push_back({0, offset, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3});
        offset += 12;
        break;
    default: {
        // XYZB1 - XYZB5: the position, then blend weights the engine does not use.
        l.attributes.push_back({0, offset, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3});
        const DWORD weights = ((fvf & D3DFVF_POSITION_MASK) - D3DFVF_XYZB1) / 2 + 1;
        offset += 12 + 4 * weights;
        break;
    }
    }
    if (fvf & D3DFVF_NORMAL) {
        l.attributes.push_back({1, offset, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3});
        l.flags |= F_NORMAL;
        offset += 12;
    }
    if (fvf & D3DFVF_PSIZE)
        offset += 4;
    if (fvf & D3DFVF_DIFFUSE) {
        l.attributes.push_back({2, offset, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM});
        l.flags |= F_DIFFUSE;
        offset += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        l.attributes.push_back({3, offset, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM});
        l.flags |= F_SPECULAR;
        offset += 4;
    }
    const DWORD sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD s = 0; s < sets; ++s) {
        static const SDL_GPUVertexElementFormat kFormats[4] = {
            SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT};
        static const uint32_t kBytes[4] = {8, 12, 16, 4};
        const DWORD size = (fvf >> (16 + 2 * s)) & 3;
        if (s < 2)
            l.attributes.push_back({4 + s, offset, kFormats[size]});
        offset += kBytes[size];
    }
    return l;
}

// A vertex declaration (D3DVSD tokens; stream 0 only, as the engine's), its registers as the
// locations; a programmable shader's registers as its function reads them.
S_layout LayoutOfDeclaration(const std::vector<DWORD> &declaration, const int *registerLocation)
{
    S_layout l;
    uint32_t offset = 0;
    for (DWORD token : declaration) {
        const DWORD type = (token >> 29) & 7;
        if (token == D3DVSD_END())
            break;
        if (type == 2 && (token & 0x10000000)) {            // D3DVSD_SKIP: DWORDs
            offset += 4 * ((token >> 16) & 15);
            continue;
        }
        if (type != 2)
            continue;
        const DWORD dataType = (token >> 16) & 15;
        const DWORD reg = token & 31;
        SDL_GPUVertexElementFormat format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        uint32_t bytes = 12;
        switch (dataType) {
        case D3DVSDT_FLOAT1: format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, bytes = 4; break;
        case D3DVSDT_FLOAT2: format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, bytes = 8; break;
        case D3DVSDT_FLOAT3: format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, bytes = 12; break;
        case D3DVSDT_FLOAT4: format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, bytes = 16; break;
        case D3DVSDT_D3DCOLOR: format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, bytes = 4; break;
        case D3DVSDT_UBYTE4: format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, bytes = 4; break;
        case D3DVSDT_SHORT2: format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT2, bytes = 4; break;
        case D3DVSDT_SHORT4: format = SDL_GPU_VERTEXELEMENTFORMAT_SHORT4, bytes = 8; break;
        default: break;
        }
        const int location = reg < 16 ? registerLocation[reg] : -1;
        if (location >= 0) {
            l.attributes.push_back({static_cast<uint32_t>(location), offset, format});
            if (location == 1)
                l.flags |= F_NORMAL;
            else if (location == 2)
                l.flags |= F_DIFFUSE;
            else if (location == 3)
                l.flags |= F_SPECULAR;
        }
        offset += bytes;
    }
    return l;
}

// The fixed-function registers (D3DVSDE_*) as the shaders' locations.
const int kFixedRegisters[16] = {0, -1, -1, 1, -1, 2, 3, 4, 5, -1, -1, -1, -1, -1, -1, -1};

struct S_shader {
    std::vector<DWORD> declaration;
    S_layout layout;
};

// A vs.1.x function of the form the engine's sky uses: dp4 oPos.<c>, v0, c<n> for each component and
// mov oD0, v<k>. False for anything else.
bool ParseProgram(const DWORD *function, S_layout *layout, int *colorRegister)
{
    if (!function || (function[0] & 0xffff0000) != 0xfffe0000)
        return false;
    bool position[4] = {};
    *colorRegister = -1;
    for (const DWORD *t = function + 1; *t != 0x0000ffff; ) {
        const DWORD op = *t & 0xffff;
        if (op == 0xfffe) {                     // comment
            t += 1 + ((*t >> 16) & 0x7fff);
            continue;
        }
        if (op == 9 && (t[1] & 0x70000000) == 0x40000000) {     // dp4 oPos, ...
            const DWORD mask = (t[1] >> 16) & 15;
            const DWORD c = t[3] & 0x7ff;
            for (int i = 0; i < 4; ++i) {
                if (mask & (1u << i)) {
                    layout->rowRegister[i] = static_cast<int>(c);
                    position[i] = true;
                }
            }
            t += 4;
        } else if (op == 1 && (t[1] & 0x70000000) == 0x50000000) {   // mov oD0, v<k>
            *colorRegister = static_cast<int>(t[2] & 0x7ff);
            t += 3;
        } else {
            return false;
        }
    }
    return position[0] && position[1] && position[2] && position[3];
}

// ---------------------------------------------------------------- pipelines

struct S_pipelineKey {
    uint32_t layout, stride;
    uint8_t primitive, blend, srcBlend, dstBlend, writeMask, depthTest, depthWrite, depthFunc, cull, fill,
        zbias, pad;
    bool operator<(const S_pipelineKey &o) const { return std::memcmp(this, &o, sizeof *this) < 0; }
};

struct S_samplerKey {
    uint8_t minFilter, magFilter, mipFilter, addressU, addressV, anisotropy;
    int16_t lodBias;
    bool operator<(const S_samplerKey &o) const { return std::memcmp(this, &o, sizeof *this) < 0; }
};

// One recorded draw (or clear) of the frame.
struct S_command {
    bool clear = false;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUViewport viewport = {};
    SDL_Rect scissor = {};
    uint32_t vertexOffset = 0;      // in the frame's vertices
    uint32_t indexOffset = 0;       // in the frame's indices (16-bit)
    uint32_t count = 0;             // indices (indexed) or vertices
    bool indexed = false;
    uint32_t vertexUniforms = 0, pixelUniforms = 0;     // in the frame's blocks
    S_texture *textures[kStages] = {};
    SDL_GPUSampler *samplers[kStages] = {};
};

SDL_GPUBlendFactor Blend(DWORD d3d)
{
    switch (d3d) {
    case D3DBLEND_ZERO: return SDL_GPU_BLENDFACTOR_ZERO;
    case D3DBLEND_ONE: return SDL_GPU_BLENDFACTOR_ONE;
    case D3DBLEND_SRCCOLOR: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}

SDL_GPUSamplerAddressMode Address(DWORD d3d)
{
    switch (d3d) {
    case D3DTADDRESS_MIRROR: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP:
    case D3DTADDRESS_BORDER:
    case D3DTADDRESS_MIRRORONCE: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    default: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    }
}

// The GPU format a Direct3D format is drawn from (INVALID: none); `convert`: through BGRA8.
SDL_GPUTextureFormat GpuFormat(D3DFORMAT format, bool *convert)
{
    *convert = false;
    switch (format) {
    case D3DFMT_A8R8G8B8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case D3DFMT_DXT1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case D3DFMT_V8U8: return SDL_GPU_TEXTUREFORMAT_R8G8_SNORM;
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_R8G8B8:
    case D3DFMT_A8:
    case D3DFMT_L8:
    case D3DFMT_A8L8:
    case D3DFMT_A4L4:
        *convert = true;
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

// One row of `format` as BGRA8.
void ToBgra(D3DFORMAT format, const uint8_t *src, uint32_t width, uint8_t *dst)
{
    auto expand = [](uint32_t v, int bits) { return static_cast<uint8_t>(bits ? (v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1) : 255); };
    for (uint32_t x = 0; x < width; ++x, dst += 4) {
        uint32_t b = 0, g = 0, r = 0, a = 255;
        switch (format) {
        case D3DFMT_X8R8G8B8: b = src[x * 4], g = src[x * 4 + 1], r = src[x * 4 + 2]; break;
        case D3DFMT_R8G8B8: b = src[x * 3], g = src[x * 3 + 1], r = src[x * 3 + 2]; break;
        case D3DFMT_R5G6B5: {
            const uint32_t v = src[x * 2] | src[x * 2 + 1] << 8;
            b = expand(v & 31, 5), g = expand(v >> 5 & 63, 6), r = expand(v >> 11 & 31, 5);
            break;
        }
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5: {
            const uint32_t v = src[x * 2] | src[x * 2 + 1] << 8;
            b = expand(v & 31, 5), g = expand(v >> 5 & 31, 5), r = expand(v >> 10 & 31, 5);
            a = format == D3DFMT_A1R5G5B5 ? (v & 0x8000 ? 255 : 0) : 255;
            break;
        }
        case D3DFMT_A4R4G4B4:
        case D3DFMT_X4R4G4B4: {
            const uint32_t v = src[x * 2] | src[x * 2 + 1] << 8;
            b = expand(v & 15, 4), g = expand(v >> 4 & 15, 4), r = expand(v >> 8 & 15, 4);
            a = format == D3DFMT_A4R4G4B4 ? expand(v >> 12, 4) : 255;
            break;
        }
        case D3DFMT_A8: b = g = r = 0, a = src[x]; break;
        case D3DFMT_L8: b = g = r = src[x]; break;
        case D3DFMT_A8L8: b = g = r = src[x * 2], a = src[x * 2 + 1]; break;
        case D3DFMT_A4L4: b = g = r = expand(src[x] & 15, 4), a = expand(src[x] >> 4, 4); break;
        default: break;
        }
        dst[0] = static_cast<uint8_t>(b);
        dst[1] = static_cast<uint8_t>(g);
        dst[2] = static_cast<uint8_t>(r);
        dst[3] = static_cast<uint8_t>(a);
    }
}

// The defaults Direct3D 8 starts a device with (render and stage states).
void DefaultStates(DWORD *rs, DWORD (*tss)[32], bool depth)
{
    std::fill(rs, rs + 256, 0u);
    rs[D3DRS_ZENABLE] = depth ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_FOGEND] = 0x3f800000;          // 1.0
    rs[D3DRS_FOGDENSITY] = 0x3f800000;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILMASK] = rs[D3DRS_STENCILWRITEMASK] = 0xffffffff;
    rs[D3DRS_TEXTUREFACTOR] = 0xffffffff;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xffffffff;
    rs[D3DRS_COLORWRITEENABLE] = 15;
    rs[D3DRS_POINTSIZE] = 0x3f800000;
    for (int s = 0; s < 8; ++s) {
        std::fill(tss[s], tss[s] + 32, 0u);
        tss[s][D3DTSS_COLOROP] = s == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        tss[s][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        tss[s][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        tss[s][D3DTSS_ALPHAOP] = s == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        tss[s][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        tss[s][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        tss[s][D3DTSS_TEXCOORDINDEX] = static_cast<DWORD>(s);
        tss[s][D3DTSS_ADDRESSU] = tss[s][D3DTSS_ADDRESSV] = tss[s][D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
        tss[s][D3DTSS_MAGFILTER] = tss[s][D3DTSS_MINFILTER] = D3DTEXF_POINT;
        tss[s][D3DTSS_MIPFILTER] = D3DTEXF_NONE;
        tss[s][D3DTSS_MAXANISOTROPY] = 1;
    }
}

class DeviceSdlGpu final : public Device {
public:
    // ---- the API and its adapters
    bool Open() override
    {
        Guard g(m);
        if (open)
            return true;
        open = sdl3::StartGpu();
        return open;
    }
    bool IsOpen() override { return open; }
    void Close() override { open = false; }
    UINT GetAdapterModeCount(UINT) override
    {
        Guard g(m);
        Modes();
        return static_cast<UINT>(modes.size());
    }
    HRESULT EnumAdapterModes(UINT, UINT mode, D3DDISPLAYMODE *displayMode) override
    {
        Guard g(m);
        Modes();
        if (mode >= modes.size())
            return D3DERR_INVALIDCALL;
        *displayMode = modes[mode];
        return D3D_OK;
    }
    HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *displayMode) override
    {
        displayMode->Width = 1024;
        displayMode->Height = 768;
        displayMode->RefreshRate = 60;
        displayMode->Format = D3DFMT_X8R8G8B8;
        if (open) {
            if (const SDL_DisplayMode *d = api.SDL_GetDesktopDisplayMode(api.SDL_GetPrimaryDisplay())) {
                displayMode->Width = static_cast<UINT>(d->w);
                displayMode->Height = static_cast<UINT>(d->h);
                displayMode->RefreshRate = static_cast<UINT>(d->refresh_rate + 0.5f);
            }
        }
        return D3D_OK;
    }
    HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8 *identifier) override
    {
        std::memset(identifier, 0, sizeof *identifier);
        std::strcpy(identifier->Driver, "SDL3.dll");
        std::strcpy(identifier->Description, "SDL_GPU");
        return D3D_OK;
    }
    HRESULT GetDeviceCaps(UINT, D3DDEVTYPE type, D3DCAPS8 *caps) override
    {
        std::memset(caps, 0, sizeof *caps);
        caps->DeviceType = type;
        caps->Caps2 = D3DCAPS2_FULLSCREENGAMMA;
        caps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT;
        caps->PrimitiveMiscCaps = 0x2 | 0x4 | 0x8 | 0x20 | 0x40 | 0x80;   // mask z, cull none/cw/ccw, colour write
        caps->RasterCaps = D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE | 0x04000000 /* ZBIAS */ |
                           0x20000 /* MIPMAPLODBIAS */ | 0x10 /* ZTEST */ | 0x20000000 /* ANISOTROPY */;
        caps->ZCmpCaps = caps->AlphaCmpCaps = 0xff;
        caps->SrcBlendCaps = caps->DestBlendCaps = 0x1fff;
        caps->ShadeCaps = 0x8 | 0x200 | 0x4000 | 0x80000;    // Gouraud colour, specular, alpha, fog
        caps->TextureCaps = D3DPTEXTURECAPS_POW2 | 0x4 /* ALPHA */ | 0x2000 /* MIPMAP */ | 0x10000 /* PROJECTED */;
        caps->TextureFilterCaps = 0x100 | 0x200 | D3DPTFILTERCAPS_MINFANISOTROPIC | 0x10000 | 0x20000 |
                                  0x1000000 | 0x2000000;     // min / mip / mag point and linear
        caps->TextureAddressCaps = 0x1 | 0x2 | 0x4 | 0x8 | 0x20;
        caps->MaxTextureWidth = caps->MaxTextureHeight = 4096;
        caps->MaxTextureRepeat = 8192;
        caps->MaxTextureAspectRatio = 4096;
        caps->MaxAnisotropy = 16;
        caps->MaxVertexW = 1e10f;
        caps->GuardBandLeft = caps->GuardBandTop = -8192;
        caps->GuardBandRight = caps->GuardBandBottom = 8192;
        caps->FVFCaps = 8;                                    // texture coordinate sets
        caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 |
                              D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X |
                              D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_ADDSIGNED2X |
                              D3DTEXOPCAPS_SUBTRACT | D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA |
                              D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA |
                              D3DTEXOPCAPS_BLENDTEXTUREALPHAPM | D3DTEXOPCAPS_BLENDCURRENTALPHA |
                              D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA |
                              D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR | D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA |
                              D3DTEXOPCAPS_BUMPENVMAP | D3DTEXOPCAPS_DOTPRODUCT3;
        caps->MaxTextureBlendStages = kStages;
        caps->MaxSimultaneousTextures = kStages;
        caps->VertexProcessingCaps = 0x1 | 0x2 | 0x8 | 0x10 | 0x20;   // texgen, colour material, local viewer
        caps->MaxActiveLights = kMaxLights;
        caps->MaxPointSize = 1;
        caps->MaxPrimitiveCount = 0xfffff;
        caps->MaxVertexIndex = 0xffff;
        caps->MaxStreams = 1;
        caps->MaxStreamStride = 256;
        caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
        caps->MaxVertexShaderConst = 96;
        return D3D_OK;
    }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE resourceType,
                              D3DFORMAT format) override
    {
        if (usage & D3DUSAGE_DEPTHSTENCIL)
            return format == D3DFMT_D16 || format == D3DFMT_D24S8 || format == D3DFMT_D24X8 || format == D3DFMT_D32
                       ? D3D_OK : D3DERR_NOTAVAILABLE;
        if (usage & D3DUSAGE_RENDERTARGET)
            return format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8 ? D3D_OK : D3DERR_NOTAVAILABLE;
        if (resourceType != D3DRTYPE_TEXTURE && resourceType != D3DRTYPE_SURFACE)
            return D3DERR_NOTAVAILABLE;
        bool convert;
        const SDL_GPUTextureFormat f = GpuFormat(format, &convert);
        if (f == SDL_GPU_TEXTUREFORMAT_INVALID)
            return D3DERR_NOTAVAILABLE;
        if (gpu && !api.SDL_GPUTextureSupportsFormat(gpu, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER))
            return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE samples) override
    {
        return samples == D3DMULTISAMPLE_NONE ? D3D_OK : D3DERR_NOTAVAILABLE;
    }

    // ---- the device
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND focusWindow, DWORD, D3DPRESENT_PARAMETERS *params) override
    {
        Guard g(m);
        if (!open || gpu)
            return D3DERR_INVALIDCALL;
        if (!MakeDevice(params->hDeviceWindow ? params->hDeviceWindow : focusWindow))
            return D3DERR_NOTAVAILABLE;
        if (!MakeTargets(*params)) {
            ReleaseDevice();
            return D3DERR_OUTOFVIDEOMEMORY;
        }
        ResetState();
        return D3D_OK;
    }
    bool HasDevice() override { return gpu != nullptr; }
    void ReleaseDevice() override
    {
        Guard g(m);
        if (!gpu)
            return;
        api.SDL_WaitForGPUIdle(gpu);
        frame.clear();
        for (auto &p : pipelines)
            api.SDL_ReleaseGPUGraphicsPipeline(gpu, p.second);
        pipelines.clear();
        for (auto &s : samplers)
            api.SDL_ReleaseGPUSampler(gpu, s.second);
        samplers.clear();
        ReleaseTargets();
        for (SDL_GPUBuffer *b : {vertexBuffer, indexBuffer, defaultsBuffer})
            if (b)
                api.SDL_ReleaseGPUBuffer(gpu, b);
        vertexBuffer = indexBuffer = defaultsBuffer = nullptr;
        vertexCapacity = indexCapacity = 0;
        if (stagingMap)
            api.SDL_UnmapGPUTransferBuffer(gpu, staging);
        stagingMap = nullptr;
        staged.clear();
        for (SDL_GPUTransferBuffer *t : {frameTransfer, staging})
            if (t)
                api.SDL_ReleaseGPUTransferBuffer(gpu, t);
        frameTransfer = staging = nullptr;
        frameTransferBytes = 0;
        if (whiteTexture)
            api.SDL_ReleaseGPUTexture(gpu, whiteTexture);
        whiteTexture = nullptr;
        for (SDL_GPUShader *s : {vertexShader, pixelShader})
            if (s)
                api.SDL_ReleaseGPUShader(gpu, s);
        vertexShader = pixelShader = nullptr;
        // (Textures keep their bytes; their GPU copies go with the device.)
        for (S_texture *t : liveTextures) {
            if (t->gpu)
                api.SDL_ReleaseGPUTexture(gpu, t->gpu);
            t->gpu = nullptr;
            t->dirty = true;
        }
        if (window)
            api.SDL_ReleaseWindowFromGPUDevice(gpu, window);
        api.SDL_DestroyGPUDevice(gpu);
        gpu = nullptr;
        if (ownWindow)
            api.SDL_DestroyWindow(window);
        window = nullptr;
        ownWindow = false;
    }
    HRESULT Reset(D3DPRESENT_PARAMETERS *params) override
    {
        Guard g(m);
        if (!gpu)
            return D3DERR_INVALIDCALL;
        api.SDL_WaitForGPUIdle(gpu);
        frame.clear();
        ReleaseTargets();
        if (!MakeTargets(*params))
            return D3DERR_OUTOFVIDEOMEMORY;
        ResetState();
        return D3D_OK;
    }
    HRESULT TestCooperativeLevel() override { return gpu ? D3D_OK : D3DERR_DEVICELOST; }
    HRESULT Present() override
    {
        Guard g(m);
        if (!gpu)
            return D3DERR_INVALIDCALL;
        SDL_GPUCommandBuffer *cmd = Execute();
        if (!cmd)
            return D3DERR_DRIVERINTERNALERROR;
        SDL_GPUTexture *swapchain = nullptr;
        Uint32 w = 0, h = 0;
        if (window && api.SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h) && swapchain) {
            SDL_GPUBlitInfo blit = {};
            blit.source.texture = backBuffer;
            blit.source.w = present.BackBufferWidth;
            blit.source.h = present.BackBufferHeight;
            blit.destination.texture = swapchain;
            blit.destination.w = w;
            blit.destination.h = h;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_LINEAR;
            api.SDL_BlitGPUTexture(cmd, &blit);
        }
        api.SDL_SubmitGPUCommandBuffer(cmd);
        return D3D_OK;
    }
    void SetGammaRamp(DWORD, const D3DGAMMARAMP *ramp) override { gamma = *ramp; }
    void GetGammaRamp(D3DGAMMARAMP *ramp) override { *ramp = gamma; }

    // ---- frame
    HRESULT BeginScene() override { return D3D_OK; }
    HRESULT EndScene() override { return D3D_OK; }
    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD) override
    {
        Guard g(m);
        if (!gpu)
            return D3DERR_INVALIDCALL;
        // Each rectangle (none: the viewport), within the viewport.
        std::vector<D3DRECT> areas;
        const LONG vx = static_cast<LONG>(viewport.X), vy = static_cast<LONG>(viewport.Y);
        const D3DRECT whole = {vx, vy, vx + static_cast<LONG>(viewport.Width), vy + static_cast<LONG>(viewport.Height)};
        if (!count || !rects)
            areas.push_back(whole);
        for (DWORD i = 0; rects && i < count; ++i) {
            D3DRECT r = {std::max(rects[i].x1, whole.x1), std::max(rects[i].y1, whole.y1),
                         std::min(rects[i].x2, whole.x2), std::min(rects[i].y2, whole.y2)};
            if (r.x2 > r.x1 && r.y2 > r.y1)
                areas.push_back(r);
        }
        const bool target = (flags & D3DCLEAR_TARGET) != 0, depthClear = (flags & D3DCLEAR_ZBUFFER) && depth;
        if (!target && !depthClear)
            return D3D_OK;
        for (const D3DRECT &r : areas) {
            struct V {
                float x, y, z, rhw;
                DWORD color;
            } quad[6];
            const float x1 = static_cast<float>(r.x1) - 0.5f, y1 = static_cast<float>(r.y1) - 0.5f;
            const float x2 = static_cast<float>(r.x2) - 0.5f, y2 = static_cast<float>(r.y2) - 0.5f;
            const float zz = viewport.MinZ + z * (viewport.MaxZ - viewport.MinZ);
            quad[0] = {x1, y1, zz, 1, color};
            quad[1] = {x2, y1, zz, 1, color};
            quad[2] = {x1, y2, zz, 1, color};
            quad[3] = {x2, y1, zz, 1, color};
            quad[4] = {x2, y2, zz, 1, color};
            quad[5] = {x1, y2, zz, 1, color};
            S_command c;
            c.clear = true;
            c.vertexOffset = AddVertices(quad, sizeof quad, sizeof quad[0]);
            c.count = 6;
            S_pipelineKey key = {};
            static const S_layout kClearLayout = LayoutOfFvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            key.layout = kClearLayout.Key();
            key.stride = sizeof quad[0];
            key.primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            key.writeMask = target ? 15 : 0;
            key.depthTest = depth ? 1 : 0;
            key.depthWrite = depthClear ? 1 : 0;
            key.depthFunc = SDL_GPU_COMPAREOP_ALWAYS;
            key.cull = SDL_GPU_CULLMODE_NONE;
            key.fill = SDL_GPU_FILLMODE_FILL;
            c.pipeline = Pipeline(key, kClearLayout);
            c.viewport = {0, 0, static_cast<float>(present.BackBufferWidth), static_cast<float>(present.BackBufferHeight), 0, 1};
            c.scissor = {0, 0, static_cast<int>(present.BackBufferWidth), static_cast<int>(present.BackBufferHeight)};
            S_vertexUniforms vu = {};
            vu.mode = {1, 0, 0, F_DIFFUSE};
            vu.viewport = {0, 0, 2.0f / present.BackBufferWidth, -2.0f / present.BackBufferHeight};
            vu.pixelOffset = {0, 0, 0, 1};
            S_pixelUniforms pu = {};
            pu.colorStage[0] = {D3DTOP_SELECTARG1, D3DTA_DIFFUSE, 0, 0};
            pu.alphaStage[0] = {D3DTOP_SELECTARG1, D3DTA_DIFFUSE, 0, 0};
            pu.colorStage[1] = {D3DTOP_DISABLE, 0, 0, 0};
            c.vertexUniforms = AddUniforms(vertexBlocks, vu);
            c.pixelUniforms = AddUniforms(pixelBlocks, pu);
            frame.push_back(c);
        }
        return D3D_OK;
    }
    UINT GetAvailableTextureMem() override { return 512u << 20; }
    HRESULT ResourceManagerDiscardBytes(DWORD) override { return D3D_OK; }

    // ---- states
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override
    {
        if (static_cast<DWORD>(state) < 256)
            rs[state] = value;
        return D3D_OK;
    }
    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) override
    {
        *value = static_cast<DWORD>(state) < 256 ? rs[state] : 0;
        return D3D_OK;
    }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override
    {
        if (stage < 8 && static_cast<DWORD>(type) < 32)
            tss[stage][type] = value;
        return D3D_OK;
    }
    HRESULT SetTexture(DWORD stage, Texture *texture) override
    {
        Guard g(m);
        if (stage >= 8)
            return D3DERR_INVALIDCALL;
        S_texture *t = reinterpret_cast<S_texture *>(texture);
        if (t)
            ++t->refs;
        if (textures[stage])
            Release(reinterpret_cast<Texture *>(textures[stage]));
        textures[stage] = t;
        return D3D_OK;
    }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override
    {
        const uint32_t s = static_cast<uint32_t>(state);
        if (s == D3DTS_VIEW)
            view = *matrix;
        else if (s == D3DTS_PROJECTION)
            projection = *matrix;
        else if (s >= D3DTS_TEXTURE0 && s < D3DTS_TEXTURE0 + 8)
            textureMatrix[s - D3DTS_TEXTURE0] = *matrix;
        else if (s == 256)
            world = *matrix;
        return D3D_OK;
    }
    HRESULT SetViewport(const D3DVIEWPORT8 *v) override
    {
        viewport = *v;
        return D3D_OK;
    }
    HRESULT SetMaterial(const D3DMATERIAL8 *material) override
    {
        currentMaterial = *material;
        return D3D_OK;
    }
    HRESULT SetLight(DWORD index, const D3DLIGHT8 *light) override
    {
        Guard g(m);
        lights[index].light = *light;
        return D3D_OK;
    }
    HRESULT LightEnable(DWORD index, BOOL enable) override
    {
        Guard g(m);
        auto l = lights.find(index);
        if (l == lights.end()) {
            // A light never set: Direct3D's default, white and directional along +z.
            D3DLIGHT8 d = {};
            d.Type = D3DLIGHT_DIRECTIONAL;
            d.Diffuse = {1, 1, 1, 0};
            d.Direction = {0, 0, 1};
            l = lights.emplace(index, S_light{d, false, 0}).first;
        }
        if (enable && !l->second.on)
            l->second.order = ++lightOrder;
        l->second.on = enable != FALSE;
        return D3D_OK;
    }
    HRESULT CreateVertexShader(const DWORD *declaration, const DWORD *function, DWORD *handle, DWORD) override
    {
        Guard g(m);
        S_shader s;
        if (declaration) {
            for (const DWORD *d = declaration;; ++d) {
                s.declaration.push_back(*d);
                if (*d == D3DVSD_END())
                    break;
            }
        }
        if (function) {
            int colorRegister = -1;
            if (!ParseProgram(function, &s.layout, &colorRegister))
                return D3DERR_INVALIDCALL;
            int map[16];
            std::fill(map, map + 16, -1);
            map[0] = 0;
            if (colorRegister >= 0 && colorRegister < 16)
                map[colorRegister] = 2;
            S_layout parsed = LayoutOfDeclaration(s.declaration, map);
            std::copy(s.layout.rowRegister, s.layout.rowRegister + 4, parsed.rowRegister);
            parsed.programmable = true;
            s.layout = parsed;
        } else {
            s.layout = LayoutOfDeclaration(s.declaration, kFixedRegisters);
        }
        nextShader += 2;
        *handle = nextShader;               // (odd: Direct3D 8's shader handles are; FVF codes are even)
        shaders[*handle] = s;
        return D3D_OK;
    }
    HRESULT SetVertexShader(DWORD handle) override
    {
        vertexFormat = handle;
        return D3D_OK;
    }
    HRESULT SetVertexShaderConstant(DWORD reg, const void *data, DWORD count) override
    {
        for (DWORD i = 0; i < count && reg + i < 96; ++i)
            std::memcpy(&constants[reg + i], static_cast<const float *>(data) + 4 * i, 16);
        return D3D_OK;
    }
    HRESULT SetStreamSource(UINT stream, VertexBuffer *buffer, UINT stride) override
    {
        Guard g(m);
        if (stream != 0)
            return D3D_OK;
        Hold(stream0, reinterpret_cast<S_buffer *>(buffer));
        streamStride = stride;
        return D3D_OK;
    }
    HRESULT SetIndices(IndexBuffer *buffer, UINT baseVertexIndex) override
    {
        Guard g(m);
        Hold(indices, reinterpret_cast<S_buffer *>(buffer));
        baseVertex = baseVertexIndex;
        return D3D_OK;
    }

    // ---- drawing
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices, UINT startIndex,
                                 UINT primitiveCount) override
    {
        Guard g(m);
        if (!gpu || !stream0 || !indices || !streamStride)
            return D3DERR_INVALIDCALL;
        const uint32_t n = PrimitiveVertices(type, primitiveCount);
        const size_t indexBytes = indices->format == D3DFMT_INDEX32 ? 4 : 2;
        if ((static_cast<size_t>(startIndex) + n) * indexBytes > indices->data.size())
            return D3DERR_INVALIDCALL;
        const size_t first = static_cast<size_t>(baseVertex + minIndex) * streamStride;
        const size_t bytes = static_cast<size_t>(numVertices) * streamStride;
        if (first + bytes > stream0->data.size())
            return D3DERR_INVALIDCALL;
        std::vector<uint16_t> list;
        list.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t v;
            if (indexBytes == 4)
                std::memcpy(&v, indices->data.data() + (startIndex + i) * 4, 4);
            else
                v = reinterpret_cast<const uint16_t *>(indices->data.data())[startIndex + i];
            list.push_back(static_cast<uint16_t>(v - minIndex));
        }
        return Draw(type, stream0->data.data() + first, bytes, streamStride, &list);
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitiveCount, const void *vertices, UINT stride) override
    {
        Guard g(m);
        const HRESULT hr = gpu ? Draw(type, vertices, static_cast<size_t>(PrimitiveVertices(type, primitiveCount)) * stride,
                                      stride, nullptr)
                               : D3DERR_INVALIDCALL;
        // (Direct3D 8 leaves stream 0 unset after a draw from user memory.)
        Hold(stream0, nullptr);
        streamStride = 0;
        return hr;
    }
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices, UINT primitiveCount,
                                   const void *indexData, D3DFORMAT indexFormat, const void *vertices,
                                   UINT stride) override
    {
        Guard g(m);
        HRESULT hr = D3DERR_INVALIDCALL;
        if (gpu) {
            const uint32_t n = PrimitiveVertices(type, primitiveCount);
            std::vector<uint16_t> list;
            list.reserve(n);
            for (uint32_t i = 0; i < n; ++i) {
                const uint32_t v = indexFormat == D3DFMT_INDEX32 ? static_cast<const uint32_t *>(indexData)[i]
                                                                 : static_cast<const uint16_t *>(indexData)[i];
                list.push_back(static_cast<uint16_t>(v - minIndex));
            }
            hr = Draw(type, static_cast<const uint8_t *>(vertices) + static_cast<size_t>(minIndex) * stride,
                      static_cast<size_t>(numVertices) * stride, stride, &list);
        }
        // (And the index buffer, after an indexed one.)
        Hold(stream0, nullptr);
        Hold(indices, nullptr);
        streamStride = 0;
        baseVertex = 0;
        return hr;
    }

    // ---- textures and surfaces
    HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                          Texture **texture) override
    {
        Guard g(m);
        *texture = nullptr;
        bool convert;
        uint32_t rowBytes, rows;
        if (!width || !height || GpuFormat(format, &convert) == SDL_GPU_TEXTUREFORMAT_INVALID ||
            !FormatRows(format, 1, 1, &rowBytes, &rows))
            return D3DERR_INVALIDCALL;
        UINT full = 1;
        for (UINT w = width, h = height; w > 1 || h > 1; w = std::max(1u, w / 2), h = std::max(1u, h / 2))
            ++full;
        if (!levels || levels > full)
            levels = full;
        auto *t = new S_texture{1, width, height, levels, usage, format, pool};
        t->data.resize(levels);
        t->surfaces.resize(levels);
        for (UINT l = 0; l < levels; ++l) {
            const UINT w = std::max(1u, width >> l), h = std::max(1u, height >> l);
            FormatRows(format, w, h, &rowBytes, &rows);
            t->data[l].assign(static_cast<size_t>(rowBytes) * rows, 0);
            S_surface &s = t->surfaces[l];
            s.texture = t;
            s.level = l;
            s.width = w;
            s.height = h;
            s.format = format;
        }
        liveTextures.push_back(t);
        *texture = reinterpret_cast<Texture *>(t);
        return D3D_OK;
    }
    HRESULT CreateImageSurface(UINT width, UINT height, D3DFORMAT format, Surface **surface) override
    {
        uint32_t rowBytes, rows;
        if (!FormatRows(format, width, height, &rowBytes, &rows))
            return D3DERR_INVALIDCALL;
        auto *s = new S_surface;
        s->width = width;
        s->height = height;
        s->format = format;
        s->data.assign(static_cast<size_t>(rowBytes) * rows, 0);
        *surface = reinterpret_cast<Surface *>(s);
        return D3D_OK;
    }
    HRESULT UpdateTexture(Texture *source, Texture *destination) override
    {
        Guard g(m);
        auto *s = reinterpret_cast<S_texture *>(source);
        auto *d = reinterpret_cast<S_texture *>(destination);
        if (!s || !d || s->format != d->format)
            return D3DERR_INVALIDCALL;
        // Level by level from the top level of the same size.
        for (UINT dl = 0; dl < d->levels; ++dl) {
            for (UINT sl = 0; sl < s->levels; ++sl) {
                if (s->surfaces[sl].width == d->surfaces[dl].width && s->surfaces[sl].height == d->surfaces[dl].height) {
                    d->data[dl] = s->data[sl];
                    break;
                }
            }
        }
        d->dirty = true;
        return D3D_OK;
    }
    HRESULT CopyRects(Surface *source, const RECT *sourceRects, UINT count, Surface *destination,
                      const POINT *destinationPoints) override
    {
        Guard g(m);
        auto *s = reinterpret_cast<S_surface *>(source);
        auto *d = reinterpret_cast<S_surface *>(destination);
        if (!s || !d || s->depthBuffer)
            return D3DERR_INVALIDCALL;
        std::vector<uint8_t> backRows;
        const std::vector<uint8_t> *from = SurfaceBytes(s);
        D3DFORMAT fromFormat = s->format;
        if (s->backBuffer) {
            if (!ReadBackBuffer(&backRows))
                return D3DERR_DRIVERINTERNALERROR;
            from = &backRows;
            fromFormat = D3DFMT_A8R8G8B8;
        }
        std::vector<uint8_t> *to = SurfaceBytes(d);
        if (!from || !to)
            return D3DERR_INVALIDCALL;
        // A whole surface into one like it: its bytes (DXT too).
        if (!sourceRects && fromFormat == d->format && s->width == d->width && s->height == d->height &&
            from->size() == to->size()) {
            *to = *from;
            if (d->texture)
                d->texture->dirty = true;
            return D3D_OK;
        }
        const RECT all = {0, 0, static_cast<LONG>(s->width), static_cast<LONG>(s->height)};
        const POINT origin = {0, 0};
        for (UINT i = 0; i < (sourceRects ? count : 1); ++i) {
            const RECT r = sourceRects ? sourceRects[i] : all;
            const POINT p = destinationPoints ? destinationPoints[i] : (sourceRects ? POINT{r.left, r.top} : origin);
            CopyRect(*from, fromFormat, s->width, r, to, d->format, d->width, d->height, p);
        }
        if (d->texture)
            d->texture->dirty = true;
        return D3D_OK;
    }
    HRESULT GetRenderTarget(Surface **surface) override
    {
        ++backSurface.refs;
        *surface = reinterpret_cast<Surface *>(&backSurface);
        return D3D_OK;
    }
    HRESULT GetDepthStencilSurface(Surface **surface) override
    {
        if (!depth) {
            *surface = nullptr;
            return D3DERR_NOTFOUND;
        }
        ++depthSurface.refs;
        *surface = reinterpret_cast<Surface *>(&depthSurface);
        return D3D_OK;
    }

    ULONG AddRef(Texture *texture) override
    {
        Guard g(m);
        return ++reinterpret_cast<S_texture *>(texture)->refs;
    }
    ULONG Release(Texture *texture) override
    {
        Guard g(m);
        auto *t = reinterpret_cast<S_texture *>(texture);
        const ULONG left = --t->refs;
        if (!left) {
            for (S_command &c : frame) {
                for (S_texture *&used : c.textures)
                    if (used == t)
                        used = nullptr;
            }
            if (t->gpu && gpu)
                api.SDL_ReleaseGPUTexture(gpu, t->gpu);
            liveTextures.erase(std::remove(liveTextures.begin(), liveTextures.end(), t), liveTextures.end());
            delete t;
        }
        return left;
    }
    DWORD GetLevelCount(Texture *texture) override { return reinterpret_cast<S_texture *>(texture)->levels; }
    HRESULT GetLevelDesc(Texture *texture, UINT level, D3DSURFACE_DESC *desc) override
    {
        auto *t = reinterpret_cast<S_texture *>(texture);
        if (level >= t->levels)
            return D3DERR_INVALIDCALL;
        Describe(t->surfaces[level], desc);
        return D3D_OK;
    }
    HRESULT GetSurfaceLevel(Texture *texture, UINT level, Surface **surface) override
    {
        Guard g(m);
        auto *t = reinterpret_cast<S_texture *>(texture);
        if (level >= t->levels)
            return D3DERR_INVALIDCALL;
        ++t->refs;
        *surface = reinterpret_cast<Surface *>(&t->surfaces[level]);
        return D3D_OK;
    }

    ULONG Release(Surface *surface) override
    {
        Guard g(m);
        auto *s = reinterpret_cast<S_surface *>(surface);
        if (s->texture)
            return Release(reinterpret_cast<Texture *>(s->texture));
        const ULONG left = --s->refs;
        if (!left && !s->backBuffer && !s->depthBuffer)
            delete s;
        return left;
    }
    HRESULT GetDesc(Surface *surface, D3DSURFACE_DESC *desc) override
    {
        Describe(*reinterpret_cast<S_surface *>(surface), desc);
        return D3D_OK;
    }
    HRESULT LockRect(Surface *surface, D3DLOCKED_RECT *locked, const RECT *rect, DWORD) override
    {
        Guard g(m);
        auto *s = reinterpret_cast<S_surface *>(surface);
        std::vector<uint8_t> *bytes = SurfaceBytes(s);
        if (!bytes || s->locked)
            return D3DERR_INVALIDCALL;
        uint32_t rowBytes, rows;
        FormatRows(s->format, s->width, s->height, &rowBytes, &rows);
        const RECT r = rect ? *rect : RECT{0, 0, static_cast<LONG>(s->width), static_cast<LONG>(s->height)};
        // (DXT: whole blocks.)
        const bool block = rows != s->height || (s->height == 1 && rowBytes != s->width);
        uint32_t unitBytes, unitRows;
        FormatRows(s->format, 1, 1, &unitBytes, &unitRows);
        const size_t x = block ? static_cast<size_t>(r.left / 4) * unitBytes : static_cast<size_t>(r.left) * unitBytes;
        const size_t y = block ? static_cast<size_t>(r.top / 4) : static_cast<size_t>(r.top);
        locked->Pitch = static_cast<INT>(rowBytes);
        locked->pBits = bytes->data() + y * rowBytes + x;
        s->locked = true;
        s->lockedRect = r;
        return D3D_OK;
    }
    HRESULT UnlockRect(Surface *surface) override
    {
        Guard g(m);
        auto *s = reinterpret_cast<S_surface *>(surface);
        if (!s->locked)
            return D3DERR_INVALIDCALL;
        s->locked = false;
        if (s->texture)
            s->texture->dirty = true;
        return D3D_OK;
    }

    // ---- vertex and index buffers
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL, VertexBuffer **buffer) override
    {
        auto *b = new S_buffer;
        b->data.assign(length, 0);
        b->usage = usage;
        b->fvf = fvf;
        *buffer = reinterpret_cast<VertexBuffer *>(b);
        return D3D_OK;
    }
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL, IndexBuffer **buffer) override
    {
        auto *b = new S_buffer;
        b->data.assign(length, 0);
        b->usage = usage;
        b->format = format;
        *buffer = reinterpret_cast<IndexBuffer *>(b);
        return D3D_OK;
    }
    ULONG Release(VertexBuffer *buffer) override { return ReleaseBuffer(reinterpret_cast<S_buffer *>(buffer)); }
    HRESULT Lock(VertexBuffer *buffer, UINT offset, UINT, BYTE **data, DWORD) override
    {
        return LockBuffer(reinterpret_cast<S_buffer *>(buffer), offset, data);
    }
    HRESULT Unlock(VertexBuffer *) override { return D3D_OK; }
    ULONG Release(IndexBuffer *buffer) override { return ReleaseBuffer(reinterpret_cast<S_buffer *>(buffer)); }
    HRESULT Lock(IndexBuffer *buffer, UINT offset, UINT, BYTE **data, DWORD) override
    {
        return LockBuffer(reinterpret_cast<S_buffer *>(buffer), offset, data);
    }
    HRESULT Unlock(IndexBuffer *) override { return D3D_OK; }

private:
    using Guard = std::lock_guard<std::recursive_mutex>;

    struct S_light {
        D3DLIGHT8 light;
        bool on;
        unsigned order;
    };

    // ---- the device's making
    bool MakeDevice(HWND hwnd)
    {
        // The engine's window as SDL's: the SDL3 platform's own, else the Win32 window wrapped.
        if (sdl3::g_window &&
            api.SDL_GetPointerProperty(api.SDL_GetWindowProperties(sdl3::g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                       nullptr) == hwnd) {
            window = sdl3::g_window;
        } else {
            const SDL_PropertiesID props = api.SDL_CreateProperties();
            api.SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, hwnd);
            window = api.SDL_CreateWindowWithProperties(props);
            api.SDL_DestroyProperties(props);
            ownWindow = window != nullptr;
        }
        if (!window) {
            EnhancementLog("LS3DF.ini SDLGPU: no SDL window for the engine's (%s)", api.SDL_GetError());
            return false;
        }
        SDL_GPUShaderFormat formats = 0;
        if (kFixedFunctionVSDxilSize > 1)
            formats |= SDL_GPU_SHADERFORMAT_DXIL;
        if (kFixedFunctionVSSpirvSize > 1)
            formats |= SDL_GPU_SHADERFORMAT_SPIRV;
        const char *driver = Enhancements().sdlGpuDriver[0] ? Enhancements().sdlGpuDriver : nullptr;
        gpu = api.SDL_CreateGPUDevice(formats, Enhancements().sdlGpuDebug, driver);
        if (!gpu && Enhancements().sdlGpuDebug) {
            // (No validation layer for this 32-bit process, as the Vulkan SDK installs only 64-bit
            // ones by default: without it.)
            EnhancementLog("LS3DF.ini SDLGPUDebug: no validation layer (%s), going on without", api.SDL_GetError());
            Trace("no validation layer: %s", api.SDL_GetError());
            gpu = api.SDL_CreateGPUDevice(formats, false, driver);
        }
        if (!gpu) {
            EnhancementLog("LS3DF.ini SDLGPU: no GPU device (%s)", api.SDL_GetError());
            if (ownWindow)
                api.SDL_DestroyWindow(window);
            window = nullptr;
            ownWindow = false;
            return false;
        }
        if (!api.SDL_ClaimWindowForGPUDevice(gpu, window)) {
            EnhancementLog("LS3DF.ini SDLGPU: the window cannot be drawn to (%s)", api.SDL_GetError());
            api.SDL_DestroyGPUDevice(gpu);
            gpu = nullptr;
            if (ownWindow)
                api.SDL_DestroyWindow(window);
            window = nullptr;
            ownWindow = false;
            return false;
        }
        const bool dxil = (api.SDL_GetGPUShaderFormats(gpu) & SDL_GPU_SHADERFORMAT_DXIL) != 0;
        auto shader = [&](const uint8_t *dxilCode, size_t dxilSize, const uint8_t *spirvCode, size_t spirvSize,
                          const char *entry, SDL_GPUShaderStage stage, Uint32 samplerCount) {
            SDL_GPUShaderCreateInfo info = {};
            info.code = dxil ? dxilCode : spirvCode;
            info.code_size = dxil ? dxilSize : spirvSize;
            info.entrypoint = entry;
            info.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
            info.stage = stage;
            info.num_samplers = samplerCount;
            info.num_uniform_buffers = 1;
            return api.SDL_CreateGPUShader(gpu, &info);
        };
        vertexShader = shader(kFixedFunctionVSDxil, kFixedFunctionVSDxilSize, kFixedFunctionVSSpirv,
                              kFixedFunctionVSSpirvSize, "VSMain", SDL_GPU_SHADERSTAGE_VERTEX, 0);
        pixelShader = shader(kFixedFunctionPSDxil, kFixedFunctionPSDxilSize, kFixedFunctionPSSpirv,
                             kFixedFunctionPSSpirvSize, "PSMain", SDL_GPU_SHADERSTAGE_FRAGMENT, kSamplerSlots);
        if (!vertexShader || !pixelShader) {
            EnhancementLog("LS3DF.ini SDLGPU: the shaders do not load (%s)", api.SDL_GetError());
            ReleaseDevice();
            return false;
        }
        // The vertex inputs a format lacks: normal 0, diffuse white, specular 0, coordinates 0.
        {
            uint8_t defaults[64] = {};
            const DWORD white = 0xffffffff;
            std::memcpy(defaults + 12, &white, 4);
            defaultsBuffer = MakeBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, sizeof defaults);
            Upload(defaultsBuffer, defaults, sizeof defaults);
        }
        // A stage without a texture reads white.
        {
            SDL_GPUTextureCreateInfo info = {};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = info.height = info.layer_count_or_depth = info.num_levels = 1;
            whiteTexture = api.SDL_CreateGPUTexture(gpu, &info);
            const uint8_t white[4] = {255, 255, 255, 255};
            UploadTexture(whiteTexture, 0, 1, 1, white, 4);
        }
        EnhancementLog("LS3DF.ini SDLGPU: drawing with SDL_GPU on %s", api.SDL_GetGPUDeviceDriver(gpu));
        Trace("device: %s", api.SDL_GetGPUDeviceDriver(gpu));
        return true;
    }

    bool MakeTargets(const D3DPRESENT_PARAMETERS &p)
    {
        present = p;
        if (!present.BackBufferWidth || !present.BackBufferHeight) {
            int w = 0, h = 0;
#ifdef _WIN32
            RECT r = {};
            if (GetClientRect(p.hDeviceWindow, &r))
                w = r.right - r.left, h = r.bottom - r.top;
#endif
            present.BackBufferWidth = static_cast<UINT>(std::max(w, 1));
            present.BackBufferHeight = static_cast<UINT>(std::max(h, 1));
        }
        if (present.BackBufferFormat == D3DFMT_UNKNOWN)
            present.BackBufferFormat = D3DFMT_X8R8G8B8;
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = present.BackBufferWidth;
        info.height = present.BackBufferHeight;
        info.layer_count_or_depth = info.num_levels = 1;
        backBuffer = api.SDL_CreateGPUTexture(gpu, &info);
        depth = nullptr;
        if (present.EnableAutoDepthStencil) {
            for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                           SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D16_UNORM}) {
                if (api.SDL_GPUTextureSupportsFormat(gpu, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
                    depthFormat = f;
                    break;
                }
            }
            info.format = depthFormat;
            info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            depth = api.SDL_CreateGPUTexture(gpu, &info);
            if (!depth)
                return false;
        }
        backSurface.backBuffer = true;
        backSurface.width = present.BackBufferWidth;
        backSurface.height = present.BackBufferHeight;
        backSurface.format = present.BackBufferFormat;
        depthSurface.depthBuffer = true;
        depthSurface.width = present.BackBufferWidth;
        depthSurface.height = present.BackBufferHeight;
        depthSurface.format = present.AutoDepthStencilFormat;
        cleared = false;
        SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
        if (present.FullScreen_PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE &&
            api.SDL_WindowSupportsGPUPresentMode(gpu, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        api.SDL_SetGPUSwapchainParameters(gpu, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
        return backBuffer != nullptr;
    }
    void ReleaseTargets()
    {
        if (backBuffer)
            api.SDL_ReleaseGPUTexture(gpu, backBuffer);
        if (depth)
            api.SDL_ReleaseGPUTexture(gpu, depth);
        backBuffer = depth = nullptr;
    }

    void ResetState()
    {
        DefaultStates(rs, tss, present.EnableAutoDepthStencil != FALSE);
        for (S_texture *&t : textures) {
            if (t)
                Release(reinterpret_cast<Texture *>(t));
            t = nullptr;
        }
        Hold(stream0, nullptr);
        Hold(indices, nullptr);
        streamStride = baseVertex = 0;
        world = view = projection = Identity();
        for (D3DMATRIX &t : textureMatrix)
            t = Identity();
        viewport = {0, 0, present.BackBufferWidth, present.BackBufferHeight, 0, 1};
        currentMaterial = {};
        lights.clear();
        vertexFormat = 0;
        std::memset(constants, 0, sizeof constants);
    }

    void Modes()
    {
        if (!modes.empty() || !open)
            return;
        int count = 0;
        SDL_DisplayMode **list = api.SDL_GetFullscreenDisplayModes(api.SDL_GetPrimaryDisplay(), &count);
        for (int i = 0; list && i < count; ++i) {
            for (D3DFORMAT f : {D3DFMT_X8R8G8B8, D3DFMT_R5G6B5}) {
                D3DDISPLAYMODE d = {static_cast<UINT>(list[i]->w), static_cast<UINT>(list[i]->h),
                                    static_cast<UINT>(list[i]->refresh_rate + 0.5f), f};
                modes.push_back(d);
            }
        }
        if (list)
            api.SDL_free(list);
        if (modes.empty()) {
            D3DDISPLAYMODE d;
            GetAdapterDisplayMode(0, &d);
            modes.push_back(d);
        }
    }

    // ---- resources
    void Hold(S_buffer *&slot, S_buffer *b)
    {
        if (b)
            ++b->refs;
        if (slot)
            ReleaseBuffer(slot);
        slot = b;
    }
    ULONG ReleaseBuffer(S_buffer *b)
    {
        Guard g(m);
        const ULONG left = --b->refs;
        if (!left)
            delete b;
        return left;
    }
    HRESULT LockBuffer(S_buffer *b, UINT offset, BYTE **data)
    {
        if (offset > b->data.size())
            return D3DERR_INVALIDCALL;
        *data = b->data.data() + offset;
        return D3D_OK;
    }
    std::vector<uint8_t> *SurfaceBytes(S_surface *s)
    {
        if (s->texture)
            return &s->texture->data[s->level];
        if (s->backBuffer || s->depthBuffer)
            return nullptr;
        return &s->data;
    }
    void Describe(const S_surface &s, D3DSURFACE_DESC *desc)
    {
        std::memset(desc, 0, sizeof *desc);
        desc->Format = s.format;
        desc->Type = D3DRTYPE_SURFACE;
        desc->Pool = s.texture ? s.texture->pool : D3DPOOL_SYSTEMMEM;
        desc->Usage = s.backBuffer ? D3DUSAGE_RENDERTARGET : s.depthBuffer ? D3DUSAGE_DEPTHSTENCIL : 0;
        desc->Width = s.width;
        desc->Height = s.height;
        uint32_t rowBytes = 0, rows = 0;
        FormatRows(s.format, s.width, s.height, &rowBytes, &rows);
        desc->Size = rowBytes * rows;
    }
    // A rectangle of `from` (in `fromFormat`, `fromWidth` wide) to `to` at `at`; formats must match,
    // but for the back buffer's BGRA8 into X8R8G8B8 / A8R8G8B8 / R5G6B5.
    void CopyRect(const std::vector<uint8_t> &from, D3DFORMAT fromFormat, UINT fromWidth, RECT r,
                  std::vector<uint8_t> *to, D3DFORMAT toFormat, UINT toWidth, UINT toHeight, POINT at)
    {
        uint32_t fromRow, fromRows, toRow, toRows;
        FormatRows(fromFormat, fromWidth, 1, &fromRow, &fromRows);
        FormatRows(toFormat, toWidth, 1, &toRow, &toRows);
        const int w = std::min<int>(r.right - r.left, static_cast<int>(toWidth) - at.x);
        const int h = std::min<int>(r.bottom - r.top, static_cast<int>(toHeight) - at.y);
        for (int y = 0; y < h; ++y) {
            const uint8_t *src = from.data() + static_cast<size_t>(r.top + y) * fromRow;
            uint8_t *dst = to->data() + static_cast<size_t>(at.y + y) * toRow;
            for (int x = 0; x < w; ++x) {
                const uint8_t *p = src + static_cast<size_t>(r.left + x) * 4;
                if (fromFormat == toFormat && toFormat != D3DFMT_A8R8G8B8 && toFormat != D3DFMT_X8R8G8B8) {
                    uint32_t unit, unitRows;
                    FormatRows(toFormat, 1, 1, &unit, &unitRows);
                    std::memcpy(dst + static_cast<size_t>(at.x + x) * unit, src + static_cast<size_t>(r.left + x) * unit, unit);
                } else if (toFormat == D3DFMT_R5G6B5) {
                    const uint16_t v = static_cast<uint16_t>((p[2] >> 3) << 11 | (p[1] >> 2) << 5 | p[0] >> 3);
                    std::memcpy(dst + static_cast<size_t>(at.x + x) * 2, &v, 2);
                } else {
                    uint8_t *q = dst + static_cast<size_t>(at.x + x) * 4;
                    std::memcpy(q, p, 4);
                    if (toFormat == D3DFMT_X8R8G8B8 || fromFormat == D3DFMT_A8R8G8B8 && present.BackBufferFormat == D3DFMT_X8R8G8B8)
                        q[3] = 255;
                }
            }
        }
    }

    SDL_GPUBuffer *MakeBuffer(SDL_GPUBufferUsageFlags usage, Uint32 size)
    {
        SDL_GPUBufferCreateInfo info = {};
        info.usage = usage;
        info.size = size;
        return api.SDL_CreateGPUBuffer(gpu, &info);
    }
    // A buffer's bytes uploaded now (its own command buffer).
    void Upload(SDL_GPUBuffer *buffer, const void *data, Uint32 size)
    {
        SDL_GPUTransferBufferCreateInfo t = {};
        t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        t.size = size;
        SDL_GPUTransferBuffer *transfer = api.SDL_CreateGPUTransferBuffer(gpu, &t);
        std::memcpy(api.SDL_MapGPUTransferBuffer(gpu, transfer, false), data, size);
        api.SDL_UnmapGPUTransferBuffer(gpu, transfer);
        SDL_GPUCommandBuffer *cmd = api.SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation from = {transfer, 0};
        SDL_GPUBufferRegion to = {buffer, 0, size};
        api.SDL_UploadToGPUBuffer(copy, &from, &to, false);
        api.SDL_EndGPUCopyPass(copy);
        api.SDL_SubmitGPUCommandBuffer(cmd);
        api.SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    }
    void UploadTexture(SDL_GPUTexture *texture, Uint32 level, Uint32 w, Uint32 h, const void *data, Uint32 size)
    {
        SDL_GPUTransferBufferCreateInfo t = {};
        t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        t.size = size;
        SDL_GPUTransferBuffer *transfer = api.SDL_CreateGPUTransferBuffer(gpu, &t);
        std::memcpy(api.SDL_MapGPUTransferBuffer(gpu, transfer, false), data, size);
        api.SDL_UnmapGPUTransferBuffer(gpu, transfer);
        SDL_GPUCommandBuffer *cmd = api.SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo from = {};
        from.transfer_buffer = transfer;
        SDL_GPUTextureRegion to = {};
        to.texture = texture;
        to.mip_level = level;
        to.w = w;
        to.h = h;
        to.d = 1;
        api.SDL_UploadToGPUTexture(copy, &from, &to, false);
        api.SDL_EndGPUCopyPass(copy);
        api.SDL_SubmitGPUCommandBuffer(cmd);
        api.SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    }

    // ---- the frame's recording
    uint32_t AddVertices(const void *data, size_t bytes, uint32_t stride)
    {
        (void)stride;
        const size_t at = (frameVertices.size() + 15) & ~static_cast<size_t>(15);   // (each draw binds its own start)
        frameVertices.resize(at + bytes);
        std::memcpy(frameVertices.data() + at, data, bytes);
        return static_cast<uint32_t>(at);
    }
    template <class U>
    static uint32_t AddUniforms(std::vector<U> &blocks, const U &u)
    {
        if (!blocks.empty() && std::memcmp(&blocks.back(), &u, sizeof u) == 0)
            return static_cast<uint32_t>(blocks.size() - 1);
        blocks.push_back(u);
        return static_cast<uint32_t>(blocks.size() - 1);
    }

    const S_layout *CurrentLayout()
    {
        if (vertexFormat & 1) {
            auto s = shaders.find(vertexFormat);
            return s != shaders.end() ? &s->second.layout : nullptr;
        }
        auto l = fvfLayouts.find(vertexFormat);
        if (l == fvfLayouts.end())
            l = fvfLayouts.emplace(vertexFormat, LayoutOfFvf(vertexFormat)).first;
        return &l->second;
    }

    HRESULT Draw(D3DPRIMITIVETYPE type, const void *vertexData, size_t bytes, UINT stride,
                 const std::vector<uint16_t> *indexList)
    {
        const S_layout *layout = CurrentLayout();
        if (!layout || !stride || !bytes)
            return D3DERR_INVALIDCALL;
        // Fans become lists (SDL_GPU has none).
        std::vector<uint16_t> fan;
        SDL_GPUPrimitiveType primitive;
        switch (type) {
        case D3DPT_POINTLIST: primitive = SDL_GPU_PRIMITIVETYPE_POINTLIST; break;
        case D3DPT_LINELIST: primitive = SDL_GPU_PRIMITIVETYPE_LINELIST; break;
        case D3DPT_LINESTRIP: primitive = SDL_GPU_PRIMITIVETYPE_LINESTRIP; break;
        case D3DPT_TRIANGLESTRIP: primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; break;
        case D3DPT_TRIANGLEFAN: {
            primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            const size_t n = indexList ? indexList->size() : bytes / stride;
            for (size_t i = 1; i + 1 < n; ++i) {
                fan.push_back(indexList ? (*indexList)[0] : 0);
                fan.push_back(indexList ? (*indexList)[i] : static_cast<uint16_t>(i));
                fan.push_back(indexList ? (*indexList)[i + 1] : static_cast<uint16_t>(i + 1));
            }
            indexList = &fan;
            break;
        }
        default: primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;
        }
        S_command c;
        c.vertexOffset = AddVertices(vertexData, bytes, stride);
        if (indexList) {
            c.indexed = true;
            c.indexOffset = static_cast<uint32_t>(frameIndices.size());
            frameIndices.insert(frameIndices.end(), indexList->begin(), indexList->end());
            if (frameIndices.size() & 1)
                frameIndices.push_back(0);             // (4-byte aligned starts)
            c.count = static_cast<uint32_t>(indexList->size());
        } else {
            c.count = static_cast<uint32_t>(bytes / stride);
        }
        if (!c.count)
            return D3D_OK;

        // The pipeline.
        S_pipelineKey key = {};
        key.layout = layout->Key();
        key.stride = stride;
        key.primitive = static_cast<uint8_t>(primitive);
        key.blend = rs[D3DRS_ALPHABLENDENABLE] ? 1 : 0;
        key.srcBlend = static_cast<uint8_t>(rs[D3DRS_SRCBLEND]);
        key.dstBlend = static_cast<uint8_t>(rs[D3DRS_DESTBLEND]);
        if (rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHSRCALPHA)
            key.srcBlend = D3DBLEND_SRCALPHA, key.dstBlend = D3DBLEND_INVSRCALPHA;
        else if (rs[D3DRS_SRCBLEND] == D3DBLEND_BOTHINVSRCALPHA)
            key.srcBlend = D3DBLEND_INVSRCALPHA, key.dstBlend = D3DBLEND_SRCALPHA;
        key.writeMask = static_cast<uint8_t>(rs[D3DRS_COLORWRITEENABLE] & 15);
        key.depthTest = depth && rs[D3DRS_ZENABLE] ? 1 : 0;
        key.depthWrite = depth && rs[D3DRS_ZENABLE] && rs[D3DRS_ZWRITEENABLE] ? 1 : 0;
        key.depthFunc = static_cast<uint8_t>(rs[D3DRS_ZFUNC] >= 1 && rs[D3DRS_ZFUNC] <= 8 ? rs[D3DRS_ZFUNC] : D3DCMP_LESSEQUAL);
        key.cull = static_cast<uint8_t>(rs[D3DRS_CULLMODE] == D3DCULL_CCW  ? SDL_GPU_CULLMODE_BACK
                                        : rs[D3DRS_CULLMODE] == D3DCULL_CW ? SDL_GPU_CULLMODE_FRONT
                                                                           : SDL_GPU_CULLMODE_NONE);
        key.fill = static_cast<uint8_t>(rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL);
        key.zbias = static_cast<uint8_t>(std::min<DWORD>(rs[D3DRS_ZBIAS], 16));
        c.pipeline = Pipeline(key, *layout);
        if (!c.pipeline)
            return D3DERR_DRIVERINTERNALERROR;
        c.viewport = {static_cast<float>(viewport.X), static_cast<float>(viewport.Y), static_cast<float>(viewport.Width),
                      static_cast<float>(viewport.Height), viewport.MinZ, viewport.MaxZ};
        c.scissor = {static_cast<int>(viewport.X), static_cast<int>(viewport.Y), static_cast<int>(viewport.Width),
                     static_cast<int>(viewport.Height)};
        c.vertexUniforms = AddUniforms(vertexBlocks, VertexUniforms(*layout));
        c.pixelUniforms = AddUniforms(pixelBlocks, PixelUniforms(c));
        frame.push_back(c);
        return D3D_OK;
    }

    S_vertexUniforms VertexUniforms(const S_layout &layout)
    {
        S_vertexUniforms u = {};
        const D3DMATRIX worldView = Multiply(world, view);
        std::memcpy(u.worldView, &worldView, 64);
        std::memcpy(u.projection, &projection, 64);
        for (int s = 0; s < kStages; ++s)
            std::memcpy(u.textureMatrix[s], &textureMatrix[s], 64);
        for (int i = 0; i < 4; ++i) {
            const int r = layout.rowRegister[i];
            u.rows[i] = r >= 0 && r < 96 ? constants[r] : F4{};
        }
        const float vw = static_cast<float>(std::max<DWORD>(viewport.Width, 1)),
                    vh = static_cast<float>(std::max<DWORD>(viewport.Height, 1));
        u.viewport = {static_cast<float>(viewport.X), static_cast<float>(viewport.Y), 2.0f / vw, -2.0f / vh};
        // Half a pixel right and down (Direct3D 9's pixel centres); pre-transformed z within MinZ - MaxZ.
        u.pixelOffset = {1.0f / vw, -1.0f / vh, viewport.MinZ, viewport.MaxZ - viewport.MinZ};
        int32_t fogMode = 0;
        if (rs[D3DRS_FOGENABLE]) {
            if (layout.pretransformed)
                fogMode = rs[D3DRS_FOGTABLEMODE] == D3DFOG_NONE ? 4 : 0;
            else
                fogMode = static_cast<int32_t>(rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE ? rs[D3DRS_FOGTABLEMODE]
                                                                                     : rs[D3DRS_FOGVERTEXMODE]);
        }
        int32_t flags = layout.flags;
        if (rs[D3DRS_NORMALIZENORMALS])
            flags |= F_NORMALIZE;
        if (rs[D3DRS_LOCALVIEWER])
            flags |= F_LOCALVIEWER;
        if (rs[D3DRS_SPECULARENABLE])
            flags |= F_SPECULARENABLE;
        u.mode = {layout.pretransformed ? 1 : layout.programmable ? 2 : 0, rs[D3DRS_LIGHTING] ? 1 : 0, fogMode, flags};
        if (!rs[D3DRS_COLORVERTEX]) {
            // (Lit, the material alone; unlit, the vertex colours still pass.)
            u.materialSource = {0, 0, 0, 0};
        } else {
            u.materialSource = {static_cast<int32_t>(rs[D3DRS_DIFFUSEMATERIALSOURCE]),
                                static_cast<int32_t>(rs[D3DRS_SPECULARMATERIALSOURCE]),
                                static_cast<int32_t>(rs[D3DRS_AMBIENTMATERIALSOURCE]),
                                static_cast<int32_t>(rs[D3DRS_EMISSIVEMATERIALSOURCE])};
        }
        for (int s = 0; s < kStages; ++s) {
            const DWORD index = tss[s][D3DTSS_TEXCOORDINDEX];
            const DWORD flagsT = tss[s][D3DTSS_TEXTURETRANSFORMFLAGS];
            u.texGen[s] = {static_cast<int32_t>(index & 0xffff), static_cast<int32_t>((index >> 16) & 3),
                           static_cast<int32_t>(flagsT & 0x1ff), 0};
        }
        const float fogStart = AsFloat(rs[D3DRS_FOGSTART]), fogEnd = AsFloat(rs[D3DRS_FOGEND]);
        u.fog = {fogStart, fogEnd, AsFloat(rs[D3DRS_FOGDENSITY]), fogEnd != fogStart ? 1.0f / (fogEnd - fogStart) : 0.0f};
        u.globalAmbient = Color(static_cast<D3DCOLOR>(rs[D3DRS_AMBIENT]));
        u.materialDiffuse = Color(currentMaterial.Diffuse);
        u.materialAmbient = Color(currentMaterial.Ambient);
        u.materialSpecular = Color(currentMaterial.Specular);
        u.materialEmissive = Color(currentMaterial.Emissive);
        u.materialPower = {currentMaterial.Power, 0, 0, 0};
        // The enabled lights (the first 8, in the order they were enabled).
        std::vector<const S_light *> on;
        for (const auto &l : lights)
            if (l.second.on)
                on.push_back(&l.second);
        std::sort(on.begin(), on.end(), [](const S_light *a, const S_light *b) { return a->order < b->order; });
        int n = 0;
        for (const S_light *l : on) {
            if (n == kMaxLights)
                break;
            const D3DLIGHT8 &d = l->light;
            S_lightUniform &o = u.lights[n++];
            o.diffuse = Color(d.Diffuse);
            o.specular = Color(d.Specular);
            o.ambient = Color(d.Ambient);
            o.position = TransformPoint(d.Position, view);
            o.direction = TransformDirection(d.Direction, view);
            o.rangeFalloffAtt0Att1 = {d.Range, d.Falloff, d.Attenuation0, d.Attenuation1};
            o.att2CosThetaCosPhiType = {d.Attenuation2, std::cos(d.Theta * 0.5f), std::cos(d.Phi * 0.5f),
                                        static_cast<float>(d.Type)};
        }
        u.lightCount = {n, 0, 0, 0};
        return u;
    }

    S_pixelUniforms PixelUniforms(S_command &c)
    {
        S_pixelUniforms u = {};
        for (int s = 0; s < kStages; ++s) {
            S_texture *t = textures[s];
            c.textures[s] = t;
            if (t && t->refs)
                ++t->refs;                  // (held until the frame runs)
            else
                c.textures[s] = nullptr;
            u.colorStage[s] = {static_cast<int32_t>(tss[s][D3DTSS_COLOROP]), static_cast<int32_t>(tss[s][D3DTSS_COLORARG1]),
                               static_cast<int32_t>(tss[s][D3DTSS_COLORARG2]), c.textures[s] ? 1 : 0};
            u.alphaStage[s] = {static_cast<int32_t>(tss[s][D3DTSS_ALPHAOP]), static_cast<int32_t>(tss[s][D3DTSS_ALPHAARG1]),
                               static_cast<int32_t>(tss[s][D3DTSS_ALPHAARG2]), 0};
            u.bumpMatrix[s] = {AsFloat(tss[s][D3DTSS_BUMPENVMAT00]), AsFloat(tss[s][D3DTSS_BUMPENVMAT01]),
                               AsFloat(tss[s][D3DTSS_BUMPENVMAT10]), AsFloat(tss[s][D3DTSS_BUMPENVMAT11])};
            c.samplers[s] = Sampler(s);
        }
        u.textureFactor = Color(static_cast<D3DCOLOR>(rs[D3DRS_TEXTUREFACTOR]));
        u.fogColor = Color(static_cast<D3DCOLOR>(rs[D3DRS_FOGCOLOR]));
        u.pixelFlags = {rs[D3DRS_FOGENABLE] ? 1 : 0, rs[D3DRS_SPECULARENABLE] ? 1 : 0,
                        rs[D3DRS_ALPHATESTENABLE] ? static_cast<int32_t>(rs[D3DRS_ALPHAFUNC]) : 0,
                        static_cast<int32_t>(rs[D3DRS_ALPHAREF] & 255)};
        return u;
    }

    SDL_GPUSampler *Sampler(int stage)
    {
        const DWORD *t = tss[stage];
        S_samplerKey key = {};
        key.minFilter = static_cast<uint8_t>(t[D3DTSS_MINFILTER]);
        key.magFilter = static_cast<uint8_t>(t[D3DTSS_MAGFILTER]);
        key.mipFilter = static_cast<uint8_t>(t[D3DTSS_MIPFILTER]);
        key.addressU = static_cast<uint8_t>(t[D3DTSS_ADDRESSU]);
        key.addressV = static_cast<uint8_t>(t[D3DTSS_ADDRESSV]);
        key.anisotropy = static_cast<uint8_t>(std::min<DWORD>(t[D3DTSS_MAXANISOTROPY], 16));
        key.lodBias = static_cast<int16_t>(AsFloat(t[D3DTSS_MIPMAPLODBIAS]) * 256);
        auto s = samplers.find(key);
        if (s != samplers.end())
            return s->second;
        SDL_GPUSamplerCreateInfo info = {};
        const bool anisotropic = key.minFilter == D3DTEXF_ANISOTROPIC || key.magFilter == D3DTEXF_ANISOTROPIC;
        info.min_filter = key.minFilter >= D3DTEXF_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mag_filter = key.magFilter >= D3DTEXF_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = key.mipFilter == D3DTEXF_LINEAR ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = Address(key.addressU);
        info.address_mode_v = Address(key.addressV);
        info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.mip_lod_bias = key.lodBias / 256.0f;
        info.min_lod = 0;
        info.max_lod = key.mipFilter == D3DTEXF_NONE ? 0.0f : 1000.0f;
        info.enable_anisotropy = anisotropic && key.anisotropy > 1;
        info.max_anisotropy = key.anisotropy > 1 ? static_cast<float>(key.anisotropy) : 1.0f;
        SDL_GPUSampler *made = api.SDL_CreateGPUSampler(gpu, &info);
        samplers[key] = made;
        return made;
    }

    SDL_GPUGraphicsPipeline *Pipeline(const S_pipelineKey &key, const S_layout &layout)
    {
        auto p = pipelines.find(key);
        if (p != pipelines.end())
            return p->second;
        SDL_GPUVertexBufferDescription buffers[2] = {};
        buffers[0].slot = 0;
        buffers[0].pitch = key.stride;
        buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        buffers[1].slot = 1;
        buffers[1].pitch = 64;
        buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        // Every location the shader reads: the format's, else the defaults buffer's.
        SDL_GPUVertexAttribute attributes[6];
        static const SDL_GPUVertexAttribute kDefaults[6] = {
            {0, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},       {1, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {2, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 12}, {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16},
            {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 20},      {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 28}};
        for (int i = 0; i < 6; ++i)
            attributes[i] = kDefaults[i];
        for (const S_layout::S_attribute &a : layout.attributes) {
            if (a.location < 6)
                attributes[a.location] = {a.location, 0, a.format, a.offset};
        }
        SDL_GPUColorTargetDescription target = {};
        target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        target.blend_state.enable_blend = key.blend != 0;
        target.blend_state.src_color_blendfactor = target.blend_state.src_alpha_blendfactor = Blend(key.srcBlend);
        target.blend_state.dst_color_blendfactor = target.blend_state.dst_alpha_blendfactor = Blend(key.dstBlend);
        target.blend_state.color_blend_op = target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        target.blend_state.enable_color_write_mask = true;
        target.blend_state.color_write_mask = key.writeMask;
        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = vertexShader;
        info.fragment_shader = pixelShader;
        info.vertex_input_state.vertex_buffer_descriptions = buffers;
        info.vertex_input_state.num_vertex_buffers = 2;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 6;
        info.primitive_type = static_cast<SDL_GPUPrimitiveType>(key.primitive);
        info.rasterizer_state.fill_mode = static_cast<SDL_GPUFillMode>(key.fill);
        info.rasterizer_state.cull_mode = static_cast<SDL_GPUCullMode>(key.cull);
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        if (key.zbias) {
            // Direct3D 8's ZBIAS (0 - 16): nearer by that many depth-buffer steps.
            info.rasterizer_state.enable_depth_bias = true;
            info.rasterizer_state.depth_bias_constant_factor = -static_cast<float>(key.zbias);
        }
        info.rasterizer_state.enable_depth_clip = true;
        info.depth_stencil_state.enable_depth_test = key.depthTest != 0;
        info.depth_stencil_state.enable_depth_write = key.depthWrite != 0;
        info.depth_stencil_state.compare_op = static_cast<SDL_GPUCompareOp>(key.depthFunc);
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = depth != nullptr;
        info.target_info.depth_stencil_format = depthFormat;
        Trace("pipeline %zu: stride %u primitive %u blend %u %u/%u mask %u depth %u/%u func %u cull %u fill %u zbias %u%s%s",
              pipelines.size(), key.stride, key.primitive, key.blend, key.srcBlend, key.dstBlend, key.writeMask,
              key.depthTest, key.depthWrite, key.depthFunc, key.cull, key.fill, key.zbias,
              layout.pretransformed ? " pretransformed" : "", layout.programmable ? " programmable" : "");
        for (const S_layout::S_attribute &a : layout.attributes)
            Trace("    location %u offset %u format %d", a.location, a.offset, static_cast<int>(a.format));
        SDL_GPUGraphicsPipeline *made = api.SDL_CreateGPUGraphicsPipeline(gpu, &info);
        Trace("    -> %s", made ? "made" : api.SDL_GetError());
        if (!made)
            EnhancementLog("LS3DF.ini SDLGPU: a pipeline cannot be made (%s)", api.SDL_GetError());
        pipelines[key] = made;
        return made;
    }

    // ---- the frame's run
    // Uploads what the frame needs and draws it into the back buffer; the command buffer is returned
    // unsubmitted (null: nothing could be acquired).
    SDL_GPUCommandBuffer *Execute()
    {
        // The textures the frame draws with, made or changed (uploaded before the frame's commands).
        for (S_command &c : frame)
            for (S_texture *t : c.textures)
                if (t && (t->dirty || !t->gpu))
                    MakeGpuTexture(t);
        FlushStaged();
        SDL_GPUCommandBuffer *cmd = api.SDL_AcquireGPUCommandBuffer(gpu);
        if (!cmd)
            return nullptr;
        // The frame's vertices and indices.
        const Uint32 vertexBytes = static_cast<Uint32>(frameVertices.size());
        const Uint32 indexBytes = static_cast<Uint32>(frameIndices.size() * 2);
        if (vertexBytes > vertexCapacity) {
            if (vertexBuffer)
                api.SDL_ReleaseGPUBuffer(gpu, vertexBuffer);
            vertexCapacity = std::max<Uint32>(vertexBytes * 3 / 2, 1 << 20);
            vertexBuffer = MakeBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, vertexCapacity);
        }
        if (indexBytes > indexCapacity) {
            if (indexBuffer)
                api.SDL_ReleaseGPUBuffer(gpu, indexBuffer);
            indexCapacity = std::max<Uint32>(indexBytes * 3 / 2, 1 << 18);
            indexBuffer = MakeBuffer(SDL_GPU_BUFFERUSAGE_INDEX, indexCapacity);
        }
        if (vertexBytes || indexBytes) {
            // One transfer buffer for every frame (SDL cycles it while frames are in flight), grown
            // as a frame needs.
            if (vertexBytes + indexBytes > frameTransferBytes) {
                if (frameTransfer)
                    api.SDL_ReleaseGPUTransferBuffer(gpu, frameTransfer);
                frameTransferBytes = std::max<Uint32>((vertexBytes + indexBytes) * 3 / 2, 4u << 20);
                SDL_GPUTransferBufferCreateInfo t = {};
                t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                t.size = frameTransferBytes;
                frameTransfer = api.SDL_CreateGPUTransferBuffer(gpu, &t);
            }
            SDL_GPUTransferBuffer *transfer = frameTransfer;
            auto *p = static_cast<uint8_t *>(api.SDL_MapGPUTransferBuffer(gpu, transfer, true));
            if (vertexBytes)
                std::memcpy(p, frameVertices.data(), vertexBytes);
            if (indexBytes)
                std::memcpy(p + vertexBytes, frameIndices.data(), indexBytes);
            api.SDL_UnmapGPUTransferBuffer(gpu, transfer);
            SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(cmd);
            if (vertexBytes) {
                SDL_GPUTransferBufferLocation from = {transfer, 0};
                SDL_GPUBufferRegion to = {vertexBuffer, 0, vertexBytes};
                api.SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            if (indexBytes) {
                SDL_GPUTransferBufferLocation from = {transfer, vertexBytes};
                SDL_GPUBufferRegion to = {indexBuffer, 0, indexBytes};
                api.SDL_UploadToGPUBuffer(copy, &from, &to, true);
            }
            api.SDL_EndGPUCopyPass(copy);
        }
        // The draws.
        SDL_GPUColorTargetInfo color = {};
        color.texture = backBuffer;
        color.load_op = cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depthTarget = {};
        depthTarget.texture = depth;
        depthTarget.load_op = cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        depthTarget.clear_depth = 1;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        depthTarget.stencil_load_op = cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        cleared = true;
        SDL_GPURenderPass *pass = api.SDL_BeginGPURenderPass(cmd, &color, 1, depth ? &depthTarget : nullptr);
        SDL_GPUGraphicsPipeline *boundPipeline = nullptr;
        uint32_t boundVertexUniforms = ~0u, boundPixelUniforms = ~0u;
        SDL_GPUTextureSamplerBinding boundSamplers[kSamplerSlots] = {};
        for (const S_command &c : frame) {
            if (!c.pipeline)
                continue;
            if (c.pipeline != boundPipeline) {
                api.SDL_BindGPUGraphicsPipeline(pass, c.pipeline);
                boundPipeline = c.pipeline;
            }
            api.SDL_SetGPUViewport(pass, &c.viewport);
            api.SDL_SetGPUScissor(pass, &c.scissor);
            SDL_GPUBufferBinding streams[2] = {{vertexBuffer, c.vertexOffset}, {defaultsBuffer, 0}};
            api.SDL_BindGPUVertexBuffers(pass, 0, streams, 2);
            // The stages' textures (re-bound only when they change: each binding takes descriptors).
            SDL_GPUTextureSamplerBinding bindings[kSamplerSlots];
            for (int s = 0; s < kSamplerSlots; ++s) {
                bindings[s].texture = s < kStages && c.textures[s] && c.textures[s]->gpu ? c.textures[s]->gpu : whiteTexture;
                bindings[s].sampler = s < kStages && c.samplers[s] ? c.samplers[s] : Sampler(0);
            }
            if (std::memcmp(bindings, boundSamplers, sizeof bindings) != 0) {
                api.SDL_BindGPUFragmentSamplers(pass, 0, bindings, kSamplerSlots);
                std::memcpy(boundSamplers, bindings, sizeof bindings);
            }
            if (c.vertexUniforms != boundVertexUniforms) {
                api.SDL_PushGPUVertexUniformData(cmd, 0, &vertexBlocks[c.vertexUniforms], sizeof(S_vertexUniforms));
                boundVertexUniforms = c.vertexUniforms;
            }
            if (c.pixelUniforms != boundPixelUniforms) {
                api.SDL_PushGPUFragmentUniformData(cmd, 0, &pixelBlocks[c.pixelUniforms], sizeof(S_pixelUniforms));
                boundPixelUniforms = c.pixelUniforms;
            }
            if (c.indexed) {
                SDL_GPUBufferBinding ib = {indexBuffer, c.indexOffset * 2};
                api.SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_16BIT);
                api.SDL_DrawGPUIndexedPrimitives(pass, c.count, 1, 0, 0, 0);
            } else {
                api.SDL_DrawGPUPrimitives(pass, c.count, 1, 0, 0);
            }
        }
        api.SDL_EndGPURenderPass(pass);
        // The frame is done with: its textures let go.
        for (S_command &c : frame)
            for (S_texture *t : c.textures)
                if (t)
                    Release(reinterpret_cast<Texture *>(t));
        frame.clear();
        frameVertices.clear();
        frameIndices.clear();
        vertexBlocks.clear();
        pixelBlocks.clear();
        return cmd;
    }

    void MakeGpuTexture(S_texture *t)
    {
        bool convert;
        const SDL_GPUTextureFormat format = GpuFormat(t->format, &convert);
        if (!t->gpu) {
            SDL_GPUTextureCreateInfo info = {};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = format;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = t->width;
            info.height = t->height;
            info.layer_count_or_depth = 1;
            info.num_levels = t->levels;
            unsigned freeMb = 0;
#ifdef _WIN32
            MEMORYSTATUSEX memory = {};
            memory.dwLength = sizeof memory;
            if (GlobalMemoryStatusEx(&memory))
                freeMb = static_cast<unsigned>(memory.ullAvailVirtual >> 20);
#endif
            Trace("texture %ux%u, %u levels, D3D format %u -> %d (address space free: %u MB)", t->width, t->height,
                  t->levels, static_cast<unsigned>(t->format), static_cast<int>(format), freeMb);
            t->gpu = api.SDL_CreateGPUTexture(gpu, &info);
            t->gpuFormat = format;
            if (!t->gpu)
                return;
        }
        std::vector<uint8_t> rows;
        for (UINT l = 0; l < t->levels; ++l) {
            const S_surface &s = t->surfaces[l];
            const std::vector<uint8_t> *bytes = &t->data[l];
            if (convert) {
                uint32_t rowBytes, rowCount;
                FormatRows(t->format, s.width, s.height, &rowBytes, &rowCount);
                rows.resize(static_cast<size_t>(s.width) * s.height * 4);
                for (UINT y = 0; y < s.height; ++y)
                    ToBgra(t->format, t->data[l].data() + static_cast<size_t>(y) * rowBytes, s.width,
                           rows.data() + static_cast<size_t>(y) * s.width * 4);
                bytes = &rows;
            }
            StageLevel(t->gpu, l, s.width, s.height, bytes->data(), static_cast<Uint32>(bytes->size()));
        }
        t->dirty = false;
    }

    // Texture levels go to the GPU through one staging buffer, a batch at a time: each batch is
    // waited for before the buffer is filled again. (An upload buffer is memory the process maps: a
    // mission's hundreds of textures each with their own would leave a 32-bit process without
    // address space.)
    static constexpr Uint32 kStagingBytes = 16u << 20;
    struct S_staged {
        SDL_GPUTexture *texture;
        Uint32 level, w, h, offset;
    };
    void StageLevel(SDL_GPUTexture *texture, Uint32 level, Uint32 w, Uint32 h, const void *data, Uint32 size)
    {
        if (size > kStagingBytes) {
            FlushStaged();
            UploadTexture(texture, level, w, h, data, size);
            api.SDL_WaitForGPUIdle(gpu);
            return;
        }
        if (staged.empty())
            stagingUsed = 0;
        const Uint32 at = (stagingUsed + 511) & ~511u;          // (placement alignment)
        if (at + size > kStagingBytes) {
            FlushStaged();
            StageLevel(texture, level, w, h, data, size);
            return;
        }
        if (!staging) {
            SDL_GPUTransferBufferCreateInfo t = {};
            t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            t.size = kStagingBytes;
            staging = api.SDL_CreateGPUTransferBuffer(gpu, &t);
            if (!staging)
                return;
        }
        if (!stagingMap)
            stagingMap = static_cast<uint8_t *>(api.SDL_MapGPUTransferBuffer(gpu, staging, false));
        if (!stagingMap)
            return;
        std::memcpy(stagingMap + at, data, size);
        staged.push_back({texture, level, w, h, at});
        stagingUsed = at + size;
    }
    void FlushStaged()
    {
        if (stagingMap) {
            api.SDL_UnmapGPUTransferBuffer(gpu, staging);
            stagingMap = nullptr;
        }
        if (staged.empty())
            return;
        SDL_GPUCommandBuffer *cmd = api.SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(cmd);
        for (const S_staged &u : staged) {
            SDL_GPUTextureTransferInfo from = {};
            from.transfer_buffer = staging;
            from.offset = u.offset;
            SDL_GPUTextureRegion to = {};
            to.texture = u.texture;
            to.mip_level = u.level;
            to.w = u.w;
            to.h = u.h;
            to.d = 1;
            api.SDL_UploadToGPUTexture(copy, &from, &to, false);
        }
        api.SDL_EndGPUCopyPass(copy);
        SDL_GPUFence *fence = api.SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence) {
            api.SDL_WaitForGPUFences(gpu, true, &fence, 1);
            api.SDL_ReleaseGPUFence(gpu, fence);
        }
        staged.clear();
        stagingUsed = 0;
    }

    // The back buffer as BGRA8 rows (the frame so far drawn first).
    bool ReadBackBuffer(std::vector<uint8_t> *rows)
    {
        SDL_GPUCommandBuffer *cmd = Execute();
        if (!cmd)
            return false;
        const Uint32 size = present.BackBufferWidth * present.BackBufferHeight * 4;
        SDL_GPUTransferBufferCreateInfo t = {};
        t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        t.size = size;
        SDL_GPUTransferBuffer *transfer = api.SDL_CreateGPUTransferBuffer(gpu, &t);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion from = {};
        from.texture = backBuffer;
        from.w = present.BackBufferWidth;
        from.h = present.BackBufferHeight;
        from.d = 1;
        SDL_GPUTextureTransferInfo to = {};
        to.transfer_buffer = transfer;
        api.SDL_DownloadFromGPUTexture(copy, &from, &to);
        api.SDL_EndGPUCopyPass(copy);
        SDL_GPUFence *fence = api.SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence) {
            api.SDL_WaitForGPUFences(gpu, true, &fence, 1);
            api.SDL_ReleaseGPUFence(gpu, fence);
        }
        const auto *p = static_cast<const uint8_t *>(api.SDL_MapGPUTransferBuffer(gpu, transfer, false));
        if (p)
            rows->assign(p, p + size);
        api.SDL_UnmapGPUTransferBuffer(gpu, transfer);
        api.SDL_ReleaseGPUTransferBuffer(gpu, transfer);
        return p != nullptr;
    }

    std::recursive_mutex m;
    bool open = false;
    SDL_GPUDevice *gpu = nullptr;
    SDL_Window *window = nullptr;
    bool ownWindow = false;
    D3DPRESENT_PARAMETERS present = {};
    SDL_GPUTexture *backBuffer = nullptr, *depth = nullptr, *whiteTexture = nullptr;
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    bool cleared = false;
    S_surface backSurface, depthSurface;
    SDL_GPUShader *vertexShader = nullptr, *pixelShader = nullptr;
    SDL_GPUBuffer *vertexBuffer = nullptr, *indexBuffer = nullptr, *defaultsBuffer = nullptr;
    Uint32 vertexCapacity = 0, indexCapacity = 0;
    SDL_GPUTransferBuffer *frameTransfer = nullptr, *staging = nullptr;
    Uint32 frameTransferBytes = 0, stagingUsed = 0;
    uint8_t *stagingMap = nullptr;
    std::vector<S_staged> staged;
    std::map<S_pipelineKey, SDL_GPUGraphicsPipeline *> pipelines;
    std::map<S_samplerKey, SDL_GPUSampler *> samplers;
    std::vector<D3DDISPLAYMODE> modes;
    D3DGAMMARAMP gamma = {};

    // The device's state.
    DWORD rs[256] = {};
    DWORD tss[8][32] = {};
    S_texture *textures[8] = {};
    S_buffer *stream0 = nullptr, *indices = nullptr;
    UINT streamStride = 0, baseVertex = 0;
    D3DMATRIX world = Identity(), view = Identity(), projection = Identity(), textureMatrix[8];
    D3DVIEWPORT8 viewport = {};
    D3DMATERIAL8 currentMaterial = {};
    std::map<DWORD, S_light> lights;
    unsigned lightOrder = 0;
    DWORD vertexFormat = 0;
    F4 constants[96] = {};
    std::map<DWORD, S_shader> shaders;
    DWORD nextShader = 1;
    std::map<DWORD, S_layout> fvfLayouts;
    std::vector<S_texture *> liveTextures;

    // The frame being recorded.
    std::vector<S_command> frame;
    std::vector<uint8_t> frameVertices;
    std::vector<uint16_t> frameIndices;
    std::vector<S_vertexUniforms> vertexBlocks;
    std::vector<S_pixelUniforms> pixelBlocks;
};

DeviceSdlGpu *s_device;

}  // namespace

Device *SdlGpuDevice()
{
    if (!s_device)
        s_device = new DeviceSdlGpu;
    return s_device;
}

}  // namespace ls3d::gfx
