// The render device shared by the SDL_GPU and OpenGL backends (gfx/fixed/fixed_device.h): Direct3D 8's
// fixed-function model on a modern API.
//
// - The fixed-function pipeline is one vertex and one pixel shader (sdlgpu/shaders/fixed_function.hlsl)
//   driven by uniforms the device's states fill: transforms, lights, material, fog, texture coordinate
//   generation, the texture stages, alpha test. What a GPU pipeline fixes (blending, depth, culling,
//   the vertex format, the primitive type) is a draw's key; the renderers make their states from it.
// - Resources live in memory as Direct3D's do: a texture's levels in their format (the engine locks
//   and writes them at any time), a buffer's bytes. A texture goes to the GPU when a frame first draws
//   with it after a change, converted to a GPU format if it has no equal (16-bit formats become 32-bit
//   BGRA; DXT stays BC).
// - A frame is recorded and run when its picture is needed (Present, a read-back): each draw keeps the
//   vertices and indices it uses, copied out of the buffers at the draw, so the engine may rewrite a
//   buffer mid-frame as Direct3D allows.
// - Direct3D 8's rules kept: a draw from user memory unsets stream 0 (and the indices); a stage
//   without a texture reads white; Clear without rectangles clears the viewport.
#include "gfx/fixed/fixed_device.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <vector>

#include "core/enhance.h"
#include "gfx/trace/trace_format.h"
#include "platform/sdl3/sdl3_api.h"

#ifdef _MSC_VER
#pragma warning(disable : 4324)     // (structures padded for the uniforms' 16-byte alignment, as meant)
#endif

namespace ls3d::gfx::fixed {

using sdl3::api;
using trace::FormatRows;
using trace::PrimitiveVertices;

void Trace(const char *format, ...)
{
    static FILE *log;
    static bool opened;
    if (!Enhancements().sdlGpuDebug && !Enhancements().sdlGpuStats)
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
                    std::strcat(path, "LS3DF_render.log");
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

bool S_pipelineKey::operator<(const S_pipelineKey &o) const { return std::memcmp(this, &o, sizeof *this) < 0; }
bool S_pipelineKey::operator==(const S_pipelineKey &o) const { return std::memcmp(this, &o, sizeof *this) == 0; }
bool S_samplerKey::operator<(const S_samplerKey &o) const { return std::memcmp(this, &o, sizeof *this) < 0; }

int SamplesWanted(const D3DPRESENT_PARAMETERS &present)
{
    const int wanted = std::max(static_cast<int>(present.MultiSampleType), Enhancements().msaa);
    return wanted >= 8 ? 8 : wanted >= 4 ? 4 : wanted >= 2 ? 2 : 1;
}

void RampTexels(const D3DGAMMARAMP &ramp, uint8_t *bgra)
{
    const auto byte = [](WORD v) { return static_cast<uint8_t>((v + 128u) / 257u); };
    for (int i = 0; i < 256; ++i) {
        bgra[i * 4] = byte(ramp.blue[i]);
        bgra[i * 4 + 1] = byte(ramp.green[i]);
        bgra[i * 4 + 2] = byte(ramp.red[i]);
        bgra[i * 4 + 3] = 255;
    }
}

void *EngineWindow(HWND hwnd, bool opengl, bool *own)
{
    *own = false;
    if (sdl3::g_window &&
        api.SDL_GetPointerProperty(api.SDL_GetWindowProperties(sdl3::g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                   nullptr) == hwnd)
        return sdl3::g_window;
    const SDL_PropertiesID props = api.SDL_CreateProperties();
    api.SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, hwnd);
    if (opengl)
        api.SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
    SDL_Window *window = api.SDL_CreateWindowWithProperties(props);
    api.SDL_DestroyProperties(props);
    *own = window != nullptr;
    return window;
}

void FitWindow(void *window, const D3DPRESENT_PARAMETERS &present)
{
    // SDL's full screen, not its borderless window moved over the display: that one keeps a caption in
    // its style, and on Windows frames then reach the display unevenly (with vsync, a frame every 33,
    // 3 and 13 ms; Direct3D 12 at 40 frames a second).
    auto *w = static_cast<SDL_Window *>(window);
    if (present.Windowed && !(api.SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN))
        return;
    api.SDL_SetWindowFullscreenMode(w, nullptr);        // (the desktop's mode)
    if (!api.SDL_SetWindowFullscreen(w, !present.Windowed))
        Trace("full screen: %s", api.SDL_GetError());
    api.SDL_SyncWindow(w);
}

E_presentMode ChoosePresentMode(const D3DPRESENT_PARAMETERS &present)
{
    const char *asked = Enhancements().presentMode;
    E_presentMode mode = present.FullScreen_PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE ? PRESENT_IMMEDIATE : PRESENT_VSYNC;
    if (!_stricmp(asked, "vsync"))
        mode = PRESENT_VSYNC;
    else if (!_stricmp(asked, "mailbox"))
        mode = PRESENT_MAILBOX;
    else if (!_stricmp(asked, "immediate"))
        mode = PRESENT_IMMEDIATE;
    return mode;
}

S_rectangle PictureRectangle(int windowWidth, int windowHeight, UINT backWidth, UINT backHeight)
{
    if (windowWidth <= 0 || windowHeight <= 0 || !backWidth || !backHeight)
        return {0, 0, windowWidth, windowHeight};
    // (In whole pixels: the side that fills the window exactly, the other rounded.)
    const int64_t a = static_cast<int64_t>(windowWidth) * backHeight, b = static_cast<int64_t>(windowHeight) * backWidth;
    int w = windowWidth, h = windowHeight;
    if (a > b)
        w = static_cast<int>((b + backHeight / 2) / backHeight);
    else if (a < b)
        h = static_cast<int>((a + backWidth / 2) / backWidth);
    return {(windowWidth - w) / 2, (windowHeight - h) / 2, w, h};
}

E_gpuFormat GpuFormat(D3DFORMAT format, bool *convert)
{
    *convert = false;
    switch (format) {
    case D3DFMT_A8R8G8B8: return GF_BGRA8;
    case D3DFMT_DXT1: return GF_BC1;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3: return GF_BC2;
    case D3DFMT_DXT4:
    case D3DFMT_DXT5: return GF_BC3;
    case D3DFMT_V8U8: return GF_RG8_SNORM;
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
        return GF_BGRA8;
    default: return GF_NONE;
    }
}

namespace {

constexpr HRESULT D3D_OK = S_OK;

struct S_buffer {
    std::vector<uint8_t> data;
    DWORD usage = 0, fvf = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    ULONG refs = 1;
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
// What a device starts with: each value as it is, in 16 bits.
D3DGAMMARAMP IdentityRamp()
{
    D3DGAMMARAMP ramp;
    for (int i = 0; i < 256; ++i)
        ramp.red[i] = ramp.green[i] = ramp.blue[i] = static_cast<WORD>(i * 257);
    return ramp;
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

S_layout LayoutOfFvf(DWORD fvf)
{
    S_layout l;
    uint32_t offset = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZRHW:
        l.attributes.push_back({0, offset, EL_FLOAT4});
        l.pretransformed = true;
        offset += 16;
        break;
    case D3DFVF_XYZ:
        l.attributes.push_back({0, offset, EL_FLOAT3});
        offset += 12;
        break;
    default: {
        // XYZB1 - XYZB5: the position, then blend weights the engine does not use.
        l.attributes.push_back({0, offset, EL_FLOAT3});
        const DWORD weights = ((fvf & D3DFVF_POSITION_MASK) - D3DFVF_XYZB1) / 2 + 1;
        offset += 12 + 4 * weights;
        break;
    }
    }
    if (fvf & D3DFVF_NORMAL) {
        l.attributes.push_back({1, offset, EL_FLOAT3});
        l.flags |= F_NORMAL;
        offset += 12;
    }
    if (fvf & D3DFVF_PSIZE)
        offset += 4;
    if (fvf & D3DFVF_DIFFUSE) {
        l.attributes.push_back({2, offset, EL_COLOR});
        l.flags |= F_DIFFUSE;
        offset += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        l.attributes.push_back({3, offset, EL_COLOR});
        l.flags |= F_SPECULAR;
        offset += 4;
    }
    const DWORD sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (DWORD s = 0; s < sets; ++s) {
        static const E_element kFormats[4] = {
            EL_FLOAT2, EL_FLOAT3,
            EL_FLOAT4, EL_FLOAT1};
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
        E_element format = EL_FLOAT3;
        uint32_t bytes = 12;
        switch (dataType) {
        case D3DVSDT_FLOAT1: format = EL_FLOAT1, bytes = 4; break;
        case D3DVSDT_FLOAT2: format = EL_FLOAT2, bytes = 8; break;
        case D3DVSDT_FLOAT3: format = EL_FLOAT3, bytes = 12; break;
        case D3DVSDT_FLOAT4: format = EL_FLOAT4, bytes = 16; break;
        case D3DVSDT_D3DCOLOR: format = EL_COLOR, bytes = 4; break;
        case D3DVSDT_UBYTE4: format = EL_UBYTE4, bytes = 4; break;
        case D3DVSDT_SHORT2: format = EL_SHORT2, bytes = 4; break;
        case D3DVSDT_SHORT4: format = EL_SHORT4, bytes = 8; break;
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

class DeviceFixed final : public Device {
public:
    explicit DeviceFixed(Renderer *r) : renderer(r) {}

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
        std::strcpy(identifier->Description, "LS3DF fixed-function renderer");
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
        const E_gpuFormat f = GpuFormat(format, &convert);
        if (f == GF_NONE || (made && !renderer->SupportsFormat(f)))
            return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE samples) override
    {
        // (A renderer settles on what the GPU can do when the device is made.)
        return samples == D3DMULTISAMPLE_NONE || samples == D3DMULTISAMPLE_2_SAMPLES || samples == D3DMULTISAMPLE_4_SAMPLES ||
                       samples == D3DMULTISAMPLE_8_SAMPLES
                   ? D3D_OK : D3DERR_NOTAVAILABLE;
    }

    // ---- the device
    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND focusWindow, DWORD, D3DPRESENT_PARAMETERS *params) override
    {
        Guard g(m);
        if (!open || made)
            return D3DERR_INVALIDCALL;
        renderer->stats = &stats;
        if (!renderer->MakeDevice(params->hDeviceWindow ? params->hDeviceWindow : focusWindow))
            return D3DERR_NOTAVAILABLE;
        made = true;
        if (!MakeTargets(*params)) {
            ReleaseDevice();
            return D3DERR_OUTOFVIDEOMEMORY;
        }
        ResetState();
        return D3D_OK;
    }
    bool HasDevice() override { return made; }
    void ReleaseDevice() override
    {
        Guard g(m);
        if (!made)
            return;
        renderer->WaitIdle();
        DropFrame();
        renderer->ReleaseTargets();
        // (Textures keep their bytes; their GPU copies go with the device.)
        DropDeadTextures();
        for (S_texture *t : liveTextures) {
            if (t->gpu)
                renderer->ReleaseTexture(t->gpu);
            t->gpu = nullptr;
            t->dirty = true;
        }
        renderer->ReleaseDevice();
        made = false;
    }
    HRESULT Reset(D3DPRESENT_PARAMETERS *params) override
    {
        Guard g(m);
        if (!made)
            return D3DERR_INVALIDCALL;
        renderer->WaitIdle();
        DropFrame();
        renderer->ReleaseTargets();
        if (!MakeTargets(*params))
            return D3DERR_OUTOFVIDEOMEMORY;
        ResetState();
        return D3D_OK;
    }
    HRESULT TestCooperativeLevel() override { return made ? D3D_OK : D3DERR_DEVICELOST; }
    bool BorderlessFullScreen() override { return made && !present.Windowed; }
    HRESULT Present() override
    {
        Guard g(m);
        if (!made)
            return D3DERR_INVALIDCALL;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        stats.draws += frame.commands.size();
        stats.vertexBytes += frame.vertices.size() + frame.indices.size() * 2;
        if (!RunFrame())
            return D3DERR_DRIVERINTERNALERROR;
        const auto built = Clock::now();
        stats.build += built - start;
        const auto acquired = stats.acquire;
        // (Direct3D has a gamma ramp in full screen only.)
        renderer->Present(rampActive && !present.Windowed ? &gamma : nullptr);
        // Every 5 seconds, what a frame cost (LS3DF.ini SDLGPUStats).
        const auto now = Clock::now();
        const auto toMs = [](Clock::duration d) { return std::chrono::duration<float, std::milli>(d).count(); };
        if (stats.frames < std::size(stats.samples))
            stats.samples[stats.frames] = {stats.frames ? toMs(start - stats.last) : 0, toMs(built - start), toMs(now - built),
                                           toMs(stats.acquire - acquired)};
        if (stats.frames++ == 0) {
            stats.since = now;
        } else {
            const auto gap = now - stats.last;
            stats.worst = std::max(stats.worst, gap);
            if (gap > std::chrono::milliseconds(20))
                ++stats.slow;
            const float ms = toMs(gap);
            ++stats.gaps[ms < 6 ? 0 : ms < 14 ? 1 : ms < 20 ? 2 : ms < 30 ? 3 : ms < 40 ? 4 : 5];
        }
        stats.last = now;
        if (now - stats.since >= std::chrono::seconds(5)) {
            const double n = static_cast<double>(stats.frames);
            auto ms = [&](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count() / n; };
            Trace("frames: %.0f, %.2f ms apart, worst %.1f ms, %zu over 20 ms; building %.2f ms, waiting for the swapchain %.2f ms, for uploads %.2f ms; "
                  "%.0f draws, %.2f MB of vertices; %.1f upload batches, %.2f MB",
                  n, ms(now - stats.since), std::chrono::duration<double, std::milli>(stats.worst).count(), stats.slow, ms(stats.build), ms(stats.acquire), ms(stats.uploadWait), stats.draws / n,
                  stats.vertexBytes / n / 1048576.0, stats.uploadBatches / n, stats.uploadBytes / n / 1048576.0);
            Trace("  frames apart under 6 / 14 / 20 / 30 / 40 ms / longer: %zu %zu %zu %zu %zu %zu", stats.gaps[0], stats.gaps[1],
                  stats.gaps[2], stats.gaps[3], stats.gaps[4], stats.gaps[5]);
            char line[1024];
            int used = 0;
            for (const auto &s : stats.samples)
                used += std::snprintf(line + used, sizeof line - used, " %.1f+%.1f+%.1f(%.1f)", s.work, s.build, s.present, s.acquire);
            Trace("  the first frames, game+building+present(of it the swapchain):%s", line);
            stats = {};
        }
        return D3D_OK;
    }
    void SetGammaRamp(DWORD, const D3DGAMMARAMP *ramp) override
    {
        Guard g(m);
        gamma = *ramp;
        // (An identity shows the picture as it is, with no pass through the table.)
        uint8_t texels[1024];
        RampTexels(gamma, texels);
        rampActive = false;
        for (int i = 0; i < 256; ++i)
            rampActive = rampActive || texels[i * 4] != i || texels[i * 4 + 1] != i || texels[i * 4 + 2] != i;
    }
    void GetGammaRamp(D3DGAMMARAMP *ramp) override
    {
        Guard g(m);
        *ramp = gamma;
    }

    // ---- frame
    HRESULT BeginScene() override { return D3D_OK; }
    HRESULT EndScene() override { return D3D_OK; }
    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD) override
    {
        Guard g(m);
        if (!made)
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
            c.vertexOffset = AddVertices(quad, sizeof quad, sizeof quad[0]);
            c.count = 6;
            static const S_layout kClearLayout = LayoutOfFvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            c.layout = &kClearLayout;
            c.key.layout = kClearLayout.Key();
            c.key.stride = sizeof quad[0];
            c.key.primitive = D3DPT_TRIANGLELIST;
            c.key.writeMask = target ? 15 : 0;
            c.key.depthTest = depth ? 1 : 0;
            c.key.depthWrite = depthClear ? 1 : 0;
            c.key.depthFunc = D3DCMP_ALWAYS;
            c.key.cull = D3DCULL_NONE;
            c.key.fill = D3DFILL_SOLID;
            for (int s = 0; s < kStages; ++s)
                c.samplers[s] = Sampler(s);
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
            c.vertexUniforms = AddUniforms(frame.vertexBlocks, vu);
            c.pixelUniforms = AddUniforms(frame.pixelBlocks, pu);
            frame.commands.push_back(c);
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
        if (!made || !stream0 || !indices || !streamStride)
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
        const HRESULT hr = made ? Draw(type, vertices, static_cast<size_t>(PrimitiveVertices(type, primitiveCount)) * stride,
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
        if (made) {
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
        if (!width || !height || GpuFormat(format, &convert) == GF_NONE ||
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
            if (!RunFrame() || !renderer->ReadBack(&backRows))
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
            // (The texture thread may release textures: the render thread lets their GPU copies go.)
            if (t->gpu)
                deadTextures.push_back(t->gpu);
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
        depth = present.EnableAutoDepthStencil != FALSE;
        backSurface.backBuffer = true;
        backSurface.width = present.BackBufferWidth;
        backSurface.height = present.BackBufferHeight;
        backSurface.format = present.BackBufferFormat;
        depthSurface.depthBuffer = true;
        depthSurface.width = present.BackBufferWidth;
        depthSurface.height = present.BackBufferHeight;
        depthSurface.format = present.AutoDepthStencilFormat;
        if (!renderer->MakeTargets(present))
            return false;
        Trace("back buffer %ux%u, %d samples a pixel, alpha to coverage %s", present.BackBufferWidth, present.BackBufferHeight,
              renderer->samples, renderer->samples > 1 && Enhancements().alphaToCoverage ? "on" : "off");
        return true;
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

    // ---- the frame's recording
    uint32_t AddVertices(const void *data, size_t bytes, uint32_t stride)
    {
        (void)stride;
        const size_t at = (frame.vertices.size() + 15) & ~static_cast<size_t>(15);   // (each draw binds its own start)
        frame.vertices.resize(at + bytes);
        std::memcpy(frame.vertices.data() + at, data, bytes);
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
        D3DPRIMITIVETYPE primitive = type;
        switch (type) {
        case D3DPT_TRIANGLEFAN: {
            primitive = D3DPT_TRIANGLELIST;
            const size_t n = indexList ? indexList->size() : bytes / stride;
            for (size_t i = 1; i + 1 < n; ++i) {
                fan.push_back(indexList ? (*indexList)[0] : 0);
                fan.push_back(indexList ? (*indexList)[i] : static_cast<uint16_t>(i));
                fan.push_back(indexList ? (*indexList)[i + 1] : static_cast<uint16_t>(i + 1));
            }
            indexList = &fan;
            break;
        }
        default: break;
        }
        S_command c;
        c.vertexOffset = AddVertices(vertexData, bytes, stride);
        if (indexList) {
            c.indexed = true;
            c.indexOffset = static_cast<uint32_t>(frame.indices.size());
            frame.indices.insert(frame.indices.end(), indexList->begin(), indexList->end());
            if (frame.indices.size() & 1)
                frame.indices.push_back(0);             // (4-byte aligned starts)
            c.count = static_cast<uint32_t>(indexList->size());
        } else {
            c.count = static_cast<uint32_t>(bytes / stride);
        }
        if (!c.count)
            return D3D_OK;

        // The pipeline.
        S_pipelineKey &key = c.key;
        c.layout = layout;
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
        key.cull = static_cast<uint8_t>(rs[D3DRS_CULLMODE]);
        key.fill = static_cast<uint8_t>(rs[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
        key.zbias = static_cast<uint8_t>(std::min<DWORD>(rs[D3DRS_ZBIAS], 16));
        // (LS3DF.ini AlphaToCoverage: 3D only, so the 2D drawing's edges stay what the game drew. A blended
        // draw's alpha is its blend's. A test for less than the reference has no edge to smooth this way.)
        key.alphaToCoverage = renderer->samples > 1 && Enhancements().alphaToCoverage && rs[D3DRS_ALPHATESTENABLE] && !key.blend &&
                              !layout->pretransformed && (rs[D3DRS_ALPHAFUNC] == D3DCMP_GREATER || rs[D3DRS_ALPHAFUNC] == D3DCMP_GREATEREQUAL);
        c.viewport = {static_cast<float>(viewport.X), static_cast<float>(viewport.Y), static_cast<float>(viewport.Width),
                      static_cast<float>(viewport.Height), viewport.MinZ, viewport.MaxZ};
        c.scissor = {static_cast<int>(viewport.X), static_cast<int>(viewport.Y), static_cast<int>(viewport.Width),
                     static_cast<int>(viewport.Height)};
        c.vertexUniforms = AddUniforms(frame.vertexBlocks, VertexUniforms(*layout));
        c.pixelUniforms = AddUniforms(frame.pixelBlocks, PixelUniforms(c));
        frame.commands.push_back(c);
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
        u.coverage = {c.key.alphaToCoverage, 0, 0, 0};
        return u;
    }

    S_samplerKey Sampler(int stage) const
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
        // Anisotropic filtering is of all three filters: the only form Direct3D 12 takes on every driver
        // (with a point mip filter, or a point filter for minification or magnification, CreateSampler
        // refuses the filter and removes the device). The other APIs draw the same, for the same pictures.
        if ((key.minFilter == D3DTEXF_ANISOTROPIC || key.magFilter == D3DTEXF_ANISOTROPIC) && key.anisotropy > 1) {
            key.minFilter = key.magFilter = D3DTEXF_ANISOTROPIC;
            if (key.mipFilter == D3DTEXF_POINT)
                key.mipFilter = D3DTEXF_LINEAR;
        }
        return key;
    }

    // ---- the frame's run
    // The frame's textures to the GPU and its draws into the back buffer; the frame is then done with.
    bool RunFrame()
    {
        DropDeadTextures();
        for (S_command &c : frame.commands)
            for (S_texture *t : c.textures)
                if (t && (t->dirty || !t->gpu)) {
                    renderer->UploadTexture(t);
                    t->dirty = false;
                }
        const bool ok = renderer->Run(frame);
        DropFrame();
        return ok;
    }
    void DropFrame()
    {
        for (S_command &c : frame.commands)
            for (S_texture *t : c.textures)
                if (t)
                    Release(reinterpret_cast<Texture *>(t));
        frame.commands.clear();
        frame.vertices.clear();
        frame.indices.clear();
        frame.vertexBlocks.clear();
        frame.pixelBlocks.clear();
    }
    void DropDeadTextures()
    {
        for (void *dead : deadTextures)
            renderer->ReleaseTexture(dead);
        deadTextures.clear();
    }

    Renderer *renderer;
    S_stats stats;
    std::recursive_mutex m;
    bool open = false, made = false, depth = false;
    D3DPRESENT_PARAMETERS present = {};
    S_surface backSurface, depthSurface;
    std::vector<D3DDISPLAYMODE> modes;
    D3DGAMMARAMP gamma = IdentityRamp();
    bool rampActive = false;
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

    std::vector<void *> deadTextures;

    // The frame being recorded.
    S_frame frame;
};

}  // namespace

const std::vector<uint8_t> &LevelBytes(const S_texture &t, UINT level, std::vector<uint8_t> *scratch)
{
    bool convert;
    GpuFormat(t.format, &convert);
    if (!convert)
        return t.data[level];
    const S_surface &s = t.surfaces[level];
    uint32_t rowBytes, rowCount;
    FormatRows(t.format, s.width, s.height, &rowBytes, &rowCount);
    scratch->resize(static_cast<size_t>(s.width) * s.height * 4);
    for (UINT y = 0; y < s.height; ++y)
        ToBgra(t.format, t.data[level].data() + static_cast<size_t>(y) * rowBytes, s.width,
               scratch->data() + static_cast<size_t>(y) * s.width * 4);
    return *scratch;
}

Device *MakeDevice(Renderer *renderer) { return new DeviceFixed(renderer); }

}  // namespace ls3d::gfx::fixed
