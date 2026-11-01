// IGraph: the init block, timing and textures (IGraph.cpp, 0x100699a0 - 0x10070eb0). Image files are
// in igraph_image.cpp.
#include "igraph/igraph.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <objbase.h>

#include "core/cpu.h"
#include "core/debug.h"
#include "i3d/i3d_links.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"
#include "math/ls3d_math.h"
#include "platform/platform.h"

using namespace ls3d;
using namespace ls3d::ig;

IGraph *g_pIGraph = nullptr;                    // 0x100d16f4

namespace ls3d::ig {

S_IGraphInit g_init;
HWND g_mainWnd = nullptr;
HWND g_childWnd = nullptr;
bool g_initialized = false;
bool g_inScene = false;
bool g_noActivation = false;

uint32_t g_caps = 0;
uint32_t g_quirks = 0;
int g_maxTextureWidth = 0;
int g_maxTextureHeight = 0;

D3DFORMAT g_fmt16Bit = D3DFMT_UNKNOWN;
D3DFORMAT g_fmt1BitAlpha = D3DFMT_UNKNOWN;
D3DFORMAT g_fmtAlpha = D3DFMT_UNKNOWN;
D3DFORMAT g_fmtTrueColor = D3DFMT_UNKNOWN;
D3DFORMAT g_fmtDxt1 = D3DFMT_UNKNOWN;
D3DFORMAT g_fmtDxt3 = D3DFMT_UNKNOWN;
D3DFORMAT g_fmtDxt5 = D3DFMT_UNKNOWN;
D3DFORMAT g_fmtBump = D3DFMT_UNKNOWN;

DWORD g_vsNormalTex1 = 0;
DWORD g_vsNormalTex2 = 0;
DWORD g_vsColorTex1 = 0;
DWORD g_vsColorTex2 = 0;
DWORD g_vsFarPlane = 0;

// (LS3DF's IGraph constructor built these and the default material below; LS3DRF's does not. The
// original builds the matrix rows from 4-float temporaries whose last element it never sets: stack
// garbage in column 3; the port uses 0, which D3DTTFF_COUNT2 ignores.)
D3DMATRIX g_texProjectXY = {{{0.5f, 0, 0, 0}, {0, -0.5f, 0, 0}, {0, 0, 0, 0}, {0.5f, -0.5f, 0, 0}}};
D3DMATRIX g_texProjectXZ = {{{0.5f, 0, 0, 0}, {0, 0, 0, 0}, {0, -0.5f, 0, 0}, {0.5f, -0.5f, 0, 0}}};

S_stateCache g_rs = {0, 0, 0, 0, {}, {}, 0, 0, 0, 0x20};   // alphaRef 0x20 (LS3DF's constructor set it)
int g_zBufferType = D3DZB_TRUE;

C_vector<ITexture *> g_cachedTextures(0x400);
C_vector<ITexture *> g_uncachedTextures(0x400);
ITexture *g_defaultTexture = nullptr;
ITexture *g_bumpAnimTextures[17] = {};
uint16_t g_textureId = 0;

D3DVIEWPORT8 g_viewport;

}  // namespace ls3d::ig

namespace {

// Window title.
char g_appName[0x100];                          // 0x100d18b0

DWORD g_lastTime = 0;                           // 0x100d1738 ReadTimer

}  // namespace

namespace ls3d::ig {
// 0x100d1ba0: diffuse, ambient and specular white, no emissive, power 0. (D3DMATERIAL8 keeps
// specular at +0x20 and emissive at +0x30: a white emissive washes every lit vertex out.)
D3DMATERIAL8 g_defaultMaterial = {{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, 0};
}

// ---------------------------------------------------------------- construction

// 0x100699a0: the 17 frames of the animated bump map (called by the renderer on first use).
bool LoadBumpAnimTextures(IGraph *igraph)
{
    for (int i = 0; i < 17; ++i) {
        char name[100];
        std::sprintf(name, "BumpAnim_%02i.dds", i);
        if (igraph->LoadTexture(name, nullptr, 0, &g_bumpAnimTextures[i]) < 0)
            return false;
    }
    return true;
}

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

// 0x10069ee0
ITexture *__stdcall IGraph::CreateITexture()
{
    ITexture *texture = new ITexture;
    texture->id = ++g_textureId;
    if (g_textureId > 0x7ffe)
        g_textureId = 0;
    return texture;
}

// Finds a texture in the search directories (0x1006fbd0); defined in igraph_image.cpp.
void FindTextureFile(const char *fileName, char *path);

// 0x10069f70
HRESULT __stdcall IGraph::LoadTexture(const char *fileName, const char *alphaFileName, uint32_t flags,
                                      ITexture **out)
{
    // DDS files carry their own format and mip levels: the conversion options do not apply.
    if (fileName && *fileName) {                // (the original also reads fileName[-1] of "")
        const char last = fileName[std::strlen(fileName) - 1];
        if (last == 's' || last == 'S')
            flags &= ~ITC_DDS_IGNORED;
    }
    *out = nullptr;
    if (flags & ITC_DEFAULT) {
        ++g_defaultTexture->refCount;
        *out = g_defaultTexture;
        return 0;
    }
    if (alphaFileName && *alphaFileName)
        flags |= ITC_USE_ALPHA;

    if (!(flags & (ITC_FROM_MEMORY | ITC_NOCACHE))) {
        if (!fileName || !*fileName)
            return I3DERR_INVALIDPARAMS;
        for (int i = g_cachedTextures.Size(); i--;) {
            ITexture *texture = g_cachedTextures[i];
            if (texture->requestedFlags != flags && texture->flags != flags)
                continue;
            if (_stricmp(texture->fileName ? texture->fileName : "", fileName))
                continue;
            if ((flags & ITC_USE_ALPHA) &&
                _stricmp(texture->alphaFileName ? texture->alphaFileName : "", alphaFileName))
                continue;
            ++texture->refCount;
            *out = texture;
            return 0;
        }
    }

    ITexture *texture = CreateITexture();
    if (!texture)
        return I3DERR_OUTOFMEM;

    char path[0x100];
    if (fileName && !(flags & ITC_FROM_MEMORY)) {
        FindTextureFile(fileName, path);
        if (!path[0])
            std::sprintf(path, "Maps\\%s", fileName);
    } else {
        path[0] = 0;
    }
    char alphaPath[0x100];
    if (alphaFileName && *alphaFileName) {
        FindTextureFile(alphaFileName, alphaPath);
        if (!alphaPath[0])
            std::sprintf(alphaPath, "Maps\\%s", alphaFileName);
    } else {
        alphaPath[0] = 0;
    }

    const HRESULT hr = texture->Create((flags & ITC_FROM_MEMORY) ? fileName : path,
                                     ((flags & ITC_USE_ALPHA) && alphaPath[0]) ? alphaPath : nullptr, flags, 0,
                                     0, 0);
    if (hr < 0) {
        texture->Release();
        *out = nullptr;
        return hr;
    }
    *out = texture;
    return 0;
}

// 0x1006a1f0
bool __stdcall IGraph::SetTexture(ITexture *texture, int stage)
{
    return gfx::g_pGfx->SetTexture(stage, texture ? texture->deviceTexture : nullptr) >= 0;
}

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

// 0x1006b360
HRESULT __stdcall IGraph::SetWorldMatrix(const S_matrix *matrix)
{
    return gfx::g_pGfx->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX *>(matrix));
}

// 0x1006b380
HRESULT __stdcall IGraph::BeginScene()
{
    if (g_inScene)
        return 1;
    g_inScene = true;
    return gfx::g_pGfx->BeginScene();
}

// 0x1006b3b0
HRESULT __stdcall IGraph::EndScene()
{
    if (!g_inScene)
        return 0;
    g_inScene = false;
    return gfx::g_pGfx->EndScene();
}

// 0x1006b5a0 / 0x1006b6e0: the primitive kinds and vertex formats of DrawPrimitive.
static bool PrimitiveType(int kind, D3DPRIMITIVETYPE *type)
{
    switch (kind) {
    case IGP_POINTS:
    case 6: *type = D3DPT_POINTLIST; return true;
    case IGP_LINES: *type = D3DPT_LINELIST; return true;
    case IGP_LINESTRIP: *type = D3DPT_LINESTRIP; return true;
    case IGP_TRIANGLES: *type = D3DPT_TRIANGLELIST; return true;
    case IGP_TRIANGLEFAN: *type = D3DPT_TRIANGLEFAN; return true;
    case IGP_TRIANGLESTRIP: *type = D3DPT_TRIANGLESTRIP; return true;
    }
    return false;
}

static bool SelectVertexFormat(int format, UINT *stride)
{
    switch (format) {
    case IGV_COLOR_TEX:
        if (g_rs.vertexShader != 2) {
            g_rs.vertexShader = 2;
            gfx::g_pGfx->SetVertexShader(g_vsColorTex1);
        }
        *stride = 0x18;
        return true;
    case IGV_TRANSFORMED:
        if (g_rs.vertexShader != 6) {
            g_rs.vertexShader = 6;
            gfx::g_pGfx->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1);
        }
        *stride = 0x20;
        return true;
    case IGV_TRANSFORMED_TEX2:
        if (g_rs.vertexShader != 7) {
            g_rs.vertexShader = 7;
            gfx::g_pGfx->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2);
        }
        *stride = 0x28;
        return true;
    }
    return false;
}

// 0x1006b5a0
HRESULT __stdcall IGraph::DrawPrimitive(int kind, int count, const void *vertices, int vertexFormat)
{
    if (!count)
        return 0;
    D3DPRIMITIVETYPE type;
    UINT stride;
    if (!PrimitiveType(kind, &type) || !SelectVertexFormat(vertexFormat, &stride))
        return I3DERR_INVALIDPARAMS;
    return gfx::g_pGfx->DrawPrimitiveUP(type, count, vertices, stride) < 0 ? I3DERR_GENERIC : 0;
}

// 0x1006b6e0
HRESULT __stdcall IGraph::DrawIndexedPrimitive(int kind, int numVertices, const uint16_t *indices, int count,
                                               const void *vertices, int vertexFormat)
{
    if (!count)
        return 0;
    D3DPRIMITIVETYPE type;
    UINT stride;
    if (!PrimitiveType(kind, &type) || !SelectVertexFormat(vertexFormat, &stride))
        return I3DERR_INVALIDPARAMS;
    return gfx::g_pGfx->DrawIndexedPrimitiveUP(type, 0, numVertices, count, indices, D3DFMT_INDEX16, vertices,
                                               stride) < 0
               ? I3DERR_GENERIC
               : 0;
}

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
    DetectCpu(&cpu);
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
    const uint32_t mhz = MeasureCpuMhz();
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
