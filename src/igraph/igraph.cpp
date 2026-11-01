// IGraph: window, Direct3D 8 device, render states and textures (IGraph.cpp, 0x100699a0 - 0x10070eb0).
// Input and timing are in igraph_input.cpp, image files in igraph_image.cpp.
#include "igraph/igraph.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <objbase.h>

#include "core/cpu.h"
#include "core/debug.h"
#include "dx/dxerr8.h"
#include "i3d/i3d_links.h"
#include "i3d/vb_manager.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"
#include "igraph/texture_stream.h"
#include "math/ls3d_math.h"
#include "platform/platform.h"

using namespace ls3d;
using namespace ls3d::ig;

IGraph *g_pIGraph = nullptr;                    // 0x100d16f4

namespace ls3d::ig {

S_IGraphInit g_init;
D3DPRESENT_PARAMETERS g_presentParams;
D3DDEVTYPE g_deviceType = D3DDEVTYPE_HAL;
HWND g_mainWnd = nullptr;
HWND g_childWnd = nullptr;
bool g_initialized = false;
bool g_inScene = false;
bool g_inactive = false;
bool g_noActivation = false;

uint32_t g_caps = 0;
uint32_t g_quirks = 0;
int g_maxTextureWidth = 0;
int g_maxTextureHeight = 0;
int g_anisotropy = 0;

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

D3DMATRIX g_texProjectXY;
D3DMATRIX g_texProjectXZ;

S_stateCache g_rs = {};
int g_zBufferType = D3DZB_TRUE;

C_vector<ITexture *> g_cachedTextures(0x400);
C_vector<ITexture *> g_uncachedTextures(0x400);
ITexture *g_defaultTexture = nullptr;
ITexture *g_bumpAnimTextures[17] = {};
uint16_t g_textureId = 0;

platform::Mutex *g_textureLock = nullptr;
volatile int g_frameCounter = 0;
platform::Thread *g_textureThread = nullptr;
bool g_textureThreadRun = false;

D3DVIEWPORT8 g_viewport;

}  // namespace ls3d::ig

namespace {

const char kSourceFile[] = "C:\\MyProjects\\Engine8_5\\IGraph\\IGraph.cpp";

// Window title, and its copy while the application is inactive.
char g_appName[0x100];                          // 0x100d18b0
char g_savedAppName[0x100];                     // 0x100d1aa0

uint8_t g_bitDepth = 0;                         // 0x100d1d40 (16 / 32; Init's request in fullscreen)

// IGraph's decisions about its window (platform/platform.h; below the device code).
class IGraphWindowEvents final : public platform::WindowEvents {
public:
    bool OnClose(int code) override;
    bool OnDestroy() override;
    void OnActivate(bool active, bool minimized) override;
    bool WaitWhileIdle() override;
    void OnChar(int ch) override;
    bool KeyboardFromMessages() override;
    void OnKeyMessage(int key, int ch) override;
};
IGraphWindowEvents g_windowEvents;

}  // namespace

namespace ls3d::ig {
D3DMATERIAL8 g_defaultMaterial;
}

namespace ls3d::ig {

// 0x1007d466 is DXGetErrorString8 (dxerr8.lib, linked into the original).
void LogDxError(HRESULT hr, int line)
{
    dbgPrintf("!! Error detected: %s, File: %s, Line: %i !!", DXGetErrorString8A(hr), kSourceFile, line);
}

}  // namespace ls3d::ig

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

// 0x10069a00
IGraph::IGraph()
{
    std::memset(&g_init, 0, sizeof g_init);
    g_initialized = false;
    g_mainWnd = nullptr;
    g_childWnd = nullptr;
    // (The original also cleared its Direct3D pointers here; the render device holds them now, and
    // they start null.)
    std::memset(&g_viewport, 0, sizeof g_viewport);

    // 0x100d1ba0: diffuse, ambient and specular white, no emissive, power 0. (D3DMATERIAL8 keeps
    // specular at +0x20 and emissive at +0x30: a white emissive washes every lit vertex out.)
    float *m = reinterpret_cast<float *>(&g_defaultMaterial);
    for (int i = 0; i < 12; ++i)
        m[i] = 1.0f;
    for (int i = 12; i < 17; ++i)
        m[i] = 0.0f;

    g_maxTextureWidth = 0;
    g_maxTextureHeight = 0;
    std::strcpy(g_appName, "LS3D");                  // (the window class's name)

    // Texture projection matrices. The original builds the rows from 4-float temporaries whose last
    // element it never sets (stack garbage in column 3); the port uses 0 (D3DTTFF_COUNT2 ignores it).
    std::memset(&g_texProjectXY, 0, sizeof g_texProjectXY);
    g_texProjectXY.m[0][0] = 0.5f;
    g_texProjectXY.m[1][1] = -0.5f;
    g_texProjectXY.m[3][0] = 0.5f;
    g_texProjectXY.m[3][1] = -0.5f;
    std::memset(&g_texProjectXZ, 0, sizeof g_texProjectXZ);
    g_texProjectXZ.m[0][0] = 0.5f;
    g_texProjectXZ.m[2][1] = -0.5f;
    g_texProjectXZ.m[3][0] = 0.5f;
    g_texProjectXZ.m[3][1] = -0.5f;

    std::memset(&g_presentParams, 0, sizeof g_presentParams);
    ResetInputState();
    g_rs.alphaRef = 0x20;

    g_cachedTextures.Reserve(0x1000);
    g_uncachedTextures.Reserve(0x1000);
    g_textureLock = platform::NewMutex();
    platform::SetWindowEvents(&g_windowEvents);
}

// 0x10069e50
IGraph::~IGraph()
{
    if (g_initialized)
        dbgPrintf("!! WARNING  IGraph was initialised, but not closed");
    platform::ReleaseCursorClip();
    platform::DeleteMutex(g_textureLock);
}

// ---------------------------------------------------------------- object factories

// 0x10069e80
IShow *__stdcall IGraph::CreateIShow()
{
    return nullptr;                     // (no IShow: no video player to create)
}

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

// 0x1006a240
HWND __stdcall IGraph::GetMainHWND()
{
    return g_mainWnd;
}

// 0x1006a250
HWND __stdcall IGraph::GetChildHWND()
{
    return g_childWnd;
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

// 0x1006a330
const char *__stdcall IGraph::GetAppName()
{
    return g_appName;
}

// 0x1006a340
bool __stdcall IGraph::AddDlg(HWND dialog)
{
    return platform::AddDialog(dialog);
}

// 0x1006a3d0
bool __stdcall IGraph::DelDlg(HWND dialog)
{
    return platform::RemoveDialog(dialog);
}

// 0x1006a470. LS3DRF's slot 0x14 is a shared `ret 4` stub (0x1002b0a0, marked at
// I3D_sound::UpdateSound): no message loop.
void __stdcall IGraph::ProcessWinMessages()
{
}

// ---------------------------------------------------------------- device

// 0x1006a6c0
HRESULT __stdcall IGraph::Clear(D3DCOLOR color, float z, uint32_t flags)
{
    DWORD d3dFlags = (flags & 1) ? D3DCLEAR_TARGET : 0;
    if (flags & 2)
        d3dFlags |= D3DCLEAR_ZBUFFER;
    if (g_caps & IGCAPS_STENCIL)
        d3dFlags |= D3DCLEAR_STENCIL;
    return gfx::g_pGfx->Clear(0, nullptr, d3dFlags, color, z, 0);
}

namespace ls3d::ig {

// 0x1006a710
void ResetDeviceStates()
{
    gfx::Device *dev = gfx::g_pGfx;
    dev->SetRenderState(D3DRS_AMBIENT, 0xff00ff);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_LOCALVIEWER, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    dev->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGEND, 0x43960000);  // 300.0f
    dev->SetRenderState(D3DRS_FOGSTART, 0);
    dev->SetRenderState(D3DRS_FOGCOLOR, 0xffffffff);
    dev->SetRenderState((g_caps & IGCAPS_TABLE_FOG) ? D3DRS_FOGTABLEMODE : D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    dev->SetRenderState(D3DRS_CLIPPING, TRUE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    dev->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(2, D3DTSS_TEXCOORDINDEX, 2);
    dev->SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_CURRENT);
    dev->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(2, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(2, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    dev->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetRenderState(D3DRS_ZBIAS, 0);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetTextureStageState(1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetTextureStageState(1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetTransform(D3DTS_TEXTURE0, &g_texProjectXY);
    dev->SetTransform(D3DTS_TEXTURE1, &g_texProjectXY);
    dev->SetVertexShader(g_vsNormalTex1);

    g_rs.colorOp[1] = D3DTOP_DISABLE;
    g_rs.colorOp[2] = D3DTOP_DISABLE;
    g_rs.zFunc = 1;
    g_rs.zWrite = 1;
    g_rs.addressU0 = D3DTADDRESS_WRAP;
    g_rs.addressV0 = D3DTADDRESS_WRAP;
    g_rs.specular = 0;
    g_rs.fog = 0;
    g_rs.fogMode = 0;
    g_rs.fogStart = 0.0f;
    g_rs.alpha = 0;
    g_rs.blend = 0;
    g_rs.zBias = 0;
    g_rs.unknown19c0 = 0;
    g_rs.unknown1888 = 0;
    g_rs.unknown1780 = 0;
    g_rs.vertexShader = 0;
    g_rs.fogEnd = 300.0f;
    g_rs.fogColor = 0xffffffff;
    g_rs.cull = D3DCULL_CCW;
    g_rs.cullDefault = D3DCULL_CCW;
    g_rs.colorOp[0] = D3DTOP_MODULATE;
    g_rs.alphaOp[0] = D3DTOP_MODULATE;
    g_rs.alphaOp[1] = D3DTOP_MODULATE;
}

}  // namespace ls3d::ig

// 0x1006abc0
HRESULT __stdcall IGraph::SetState(int state, int value)
{
    gfx::Device *dev = gfx::g_pGfx;
    switch (state) {
    case IGS_ALPHA:
        if (g_rs.alpha == value)
            break;
        g_rs.alpha = value;
        switch (value) {
        case 0:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            return dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        case 1:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            return dev->SetRenderState(D3DRS_ALPHAREF, 1);
        case 2:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            return dev->SetRenderState(D3DRS_ALPHAREF, 0);
        case 3:
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            return dev->SetRenderState(D3DRS_ALPHAREF, g_rs.alphaRef);
        }
        break;
    case IGS_BLEND:
        if (g_rs.blend == value)
            break;
        g_rs.blend = value;
        switch (value) {
        case 0:
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            return dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        case 1:
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            return dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        case 2:
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
            return dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        case 3:
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
            return dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
        case 4:
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
            return dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
        }
        break;
    case IGS_CULL:
        if (g_rs.cull == value)
            break;
        g_rs.cull = value;
        return dev->SetRenderState(D3DRS_CULLMODE, value == 3 ? g_rs.cullDefault : value);
    case IGS_COLOROP0:
    case IGS_COLOROP1:
    case IGS_COLOROP2:
        if (g_rs.colorOp[state - IGS_COLOROP0] == value)
            break;
        g_rs.colorOp[state - IGS_COLOROP0] = value;
        return dev->SetTextureStageState(state - IGS_COLOROP0, D3DTSS_COLOROP, value);
    case IGS_ALPHAOP0:
    case IGS_ALPHAOP1:
        if (g_rs.alphaOp[state - IGS_ALPHAOP0] == value)
            break;
        g_rs.alphaOp[state - IGS_ALPHAOP0] = value;
        return dev->SetTextureStageState(state - IGS_ALPHAOP0, D3DTSS_ALPHAOP, value);
    case IGS_ZBIAS:
        if (g_rs.zBias == value)
            break;
        g_rs.zBias = value;
        return dev->SetRenderState(D3DRS_ZBIAS, value);
    case IGS_ADDRESSU0:
        if (g_rs.addressU0 == value)
            break;
        g_rs.addressU0 = value;
        return dev->SetTextureStageState(0, D3DTSS_ADDRESSU, value);
    case IGS_ADDRESSV0:
        // The original sets ADDRESSU here as well (and GetState(IGS_ADDRESSV0) returns the U value).
        if (g_rs.addressV0 == value)
            break;
        g_rs.addressV0 = value;
        return dev->SetTextureStageState(0, D3DTSS_ADDRESSU, value);
    case IGS_ALPHAREF:
        g_rs.alphaRef = value;
        return dev->SetRenderState(D3DRS_ALPHAREF, value);
    case IGS_ZFUNC:
        if (g_rs.zFunc == value)
            break;
        g_rs.zFunc = value;
        return dev->SetRenderState(D3DRS_ZFUNC, value ? D3DCMP_LESSEQUAL : D3DCMP_ALWAYS);
    case IGS_ZWRITE:
        SetZWrite(value);
        return value;   // 0x10063720 returns nothing; this is what was in EAX in most cases
    case IGS_ZENABLE:
        return dev->SetRenderState(D3DRS_ZENABLE, value ? g_zBufferType : D3DZB_FALSE);
    case IGS_CLAMP0:
        if (value) {
            dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
            return dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        }
        dev->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
        return dev->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    case IGS_CLAMP1:
        if (value) {
            dev->SetTextureStageState(1, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
            return dev->SetTextureStageState(1, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        }
        dev->SetTextureStageState(1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
        return dev->SetTextureStageState(1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    case IGS_FOG: {
        const HRESULT hr = dev->SetRenderState(D3DRS_FOGENABLE, value ? TRUE : FALSE);
        g_rs.fog = value ? 1 : 0;
        return hr;
    }
    case IGS_SPECULAR:
        SetSpecular(value);
        return value;
    case IGS_COLORWRITE:
        return dev->SetRenderState(D3DRS_COLORWRITEENABLE, value);
    }
    return value;
}

// 0x1006b220
int __stdcall IGraph::GetState(int state)
{
    DWORD value = 0;
    switch (state) {
    case IGS_ALPHA: return g_rs.alpha;
    case IGS_BLEND: return g_rs.blend;
    case IGS_CULL: return g_rs.cull;
    case IGS_COLOROP0: return g_rs.colorOp[0];
    case IGS_COLOROP1: return g_rs.colorOp[1];
    case IGS_COLOROP2: return g_rs.colorOp[2];
    case IGS_ALPHAOP0: return g_rs.alphaOp[0];
    case IGS_ALPHAOP1: return g_rs.alphaOp[1];
    case IGS_ZBIAS: return g_rs.zBias;
    case IGS_ADDRESSU0:
    case IGS_ADDRESSV0: return g_rs.addressU0;
    case IGS_ALPHAREF: return g_rs.alphaRef;
    case IGS_ZFUNC: return g_rs.zFunc;
    case IGS_ZWRITE: return g_rs.zWrite;
    case IGS_ZENABLE:
        gfx::g_pGfx->GetRenderState(D3DRS_ZENABLE, &value);
        return static_cast<int>(value);
    case IGS_FOG: return g_rs.fog;
    case IGS_SPECULAR: return g_rs.specular;
    case IGS_COLORWRITE:
        gfx::g_pGfx->GetRenderState(D3DRS_COLORWRITEENABLE, &value);
        return static_cast<int>(value);
    }
    return -1;
}

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

// 0x1006b3e0
HRESULT __stdcall IGraph::Flip()
{
    // (No ISND: no voices to update. No FPS or sound-statistics overlay: their Ctrl+Alt+Shift keys were
    // read through DirectInput.)
    if (g_init.flags & IG_TEXTURE_THREAD) {
        platform::Lock(g_textureLock);
        ++g_frameCounter;
        platform::Unlock(g_textureLock);
    }
    platform::Pause(0);
    HRESULT hr = gfx::g_pGfx->Present();
    if (hr == D3DERR_DEVICELOST) {
        // (No mouse to close and reopen: no DirectInput.)
        if (!g_inactive && gfx::g_pGfx->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            dbgPrintf("DEVICE LOST - 1");
            if (RestoreDevice(true) >= 0 && (g_init.flags & IG_TEXTURE_THREAD))
                tex::StreamInit(g_init.flags);
            ResetDeviceStates();
            dbgPrintf("DEVICE LOST - 2");
        }
    }
    return hr;
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

// The decisions of IGraph's window procedure (0x1006b820) and message loop (0x1006a470); the
// platform does the rest (platform/win32).
bool IGraphWindowEvents::OnClose(int code)
{
    if (!g_initialized)
        return false;
    g_pIGraph->Close();
    std::exit(code);
}

bool IGraphWindowEvents::OnDestroy()
{
    return g_initialized;
}

void IGraphWindowEvents::OnActivate(bool active, bool minimized)
{
    bool deactivate = false;
    bool reactivate = false;
    if (g_noActivation) {
        reactivate = g_inactive;
    } else if (minimized) {
        deactivate = !g_inactive;
    } else if (g_inactive) {
        reactivate = active;
    } else if (!active) {
        // Losing the focus to one of our own windows does not count.
        deactivate = !platform::ForegroundIsOwn(g_mainWnd);
    }
    if (deactivate) {
        std::strcpy(g_savedAppName, g_appName);
        if (g_init.flags & IG_WAIT_INACTIVE) {
            if (std::strlen(g_appName) < 0xf6)
                std::strcat(g_appName, " ..zzZZ");
            platform::SetTitle(g_mainWnd, g_appName);
            if (g_textureThread)
                platform::Suspend(g_textureThread);
        }
        g_inactive = true;
    } else if (reactivate) {
        g_pIGraph->SetAppName(g_savedAppName);
        g_inactive = false;
        if (gfx::g_pGfx->HasDevice())
            i3d::ReapplyDriverStates();
        if (g_textureThread)
            platform::Resume(g_textureThread);
    }
}

bool IGraphWindowEvents::WaitWhileIdle()
{
    return g_inactive && !g_noActivation && (g_init.flags & IG_WAIT_INACTIVE);
}

// Typed characters (not repeats of a held key) queue for ReadChar.
void IGraphWindowEvents::OnChar(int ch) { QueueChar(ch); }
bool IGraphWindowEvents::KeyboardFromMessages() { return KeyboardUsesMessages(); }
void IGraphWindowEvents::OnKeyMessage(int key, int ch) { KeyMessage(key, ch); }

// 0x1006bb80
const char *__stdcall IGraph::GetFormatName(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_UNKNOWN: return "D3DFMT_UNKNOWN";
    case D3DFMT_R8G8B8: return "D3DFMT_R8G8B8";
    case D3DFMT_A8R8G8B8: return "D3DFMT_A8R8G8B8";
    case D3DFMT_X8R8G8B8: return "D3DFMT_X8R8G8B8";
    case D3DFMT_R5G6B5: return "D3DFMT_R5G6B5";
    case D3DFMT_X1R5G5B5: return "D3DFMT_X1R5G5B5";
    case D3DFMT_A1R5G5B5: return "D3DFMT_A1R5G5B5";
    case D3DFMT_A4R4G4B4: return "D3DFMT_A4R4G4B4";
    case D3DFMT_R3G3B2: return "D3DFMT_R3G3B2";
    case D3DFMT_A8: return "D3DFMT_A8";
    case D3DFMT_A8R3G3B2: return "D3DFMT_A8R3G3B2";
    case D3DFMT_X4R4G4B4: return "D3DFMT_X4R4G4B4";
    case D3DFMT_A8P8: return "D3DFMT_A8P8";
    case D3DFMT_P8: return "D3DFMT_P8";
    case D3DFMT_L8: return "D3DFMT_L8";
    case D3DFMT_A8L8: return "D3DFMT_A8L8";
    case D3DFMT_A4L4: return "D3DFMT_A4L4";
    case D3DFMT_V8U8: return "D3DFMT_V8U8";
    case D3DFMT_L6V5U5: return "D3DFMT_L6V5U5";
    case D3DFMT_X8L8V8U8: return "D3DFMT_X8L8V8U8";
    case D3DFMT_Q8W8V8U8: return "D3DFMT_Q8W8V8U8";
    case D3DFMT_V16U16: return "D3DFMT_V16U16";
    case D3DFMT_W11V11U10: return "D3DFMT_W11V11U10";
    case D3DFMT_D16_LOCKABLE: return "D3DFMT_D16_LOCKABLE";
    case D3DFMT_D32: return "D3DFMT_D32";
    case D3DFMT_D15S1: return "D3DFMT_D15S1";
    case D3DFMT_D24S8: return "D3DFMT_D24S8";
    case D3DFMT_D24X8: return "D3DFMT_D24X8";
    case D3DFMT_D24X4S4: return "D3DFMT_D24X4S4";
    case D3DFMT_D16: return "D3DFMT_D16";
    case D3DFMT_VERTEXDATA: return "D3DFMT_VERTEXDATA";
    case D3DFMT_INDEX16: return "D3DFMT_INDEX16";
    case D3DFMT_INDEX32: return "D3DFMT_INDEX32";
    case D3DFMT_DXT1: return "D3DFMT_DXT1";
    case D3DFMT_DXT2: return "D3DFMT_DXT2";
    case D3DFMT_DXT3: return "D3DFMT_DXT3";
    case D3DFMT_DXT4: return "D3DFMT_DXT4";
    case D3DFMT_DXT5: return "D3DFMT_DXT5";
    case D3DFMT_YUY2: return "D3DFMT_YUY2";
    case D3DFMT_UYVY: return "D3DFMT_UYVY";
    case D3DFMT_FORCE_DWORD: return "D3DFMT_FORCE_DWORD";
    default: return "UNSUPPORTED_FORMAT";
    }
}

// The depth formats tried for a 16 / 32-bit display, from the last entry to the first.
static const D3DFORMAT kDepthFormats16[4] = {D3DFMT_D15S1, D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D32};
static const D3DFORMAT kDepthFormats32[4] = {D3DFMT_D24X4S4, D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D32};

static bool Is16BitFormat(D3DFORMAT f) { return f == D3DFMT_R5G6B5 || f == D3DFMT_X1R5G5B5 || f == D3DFMT_A1R5G5B5; }
static bool Is32BitFormat(D3DFORMAT f) { return f == D3DFMT_R8G8B8 || f == D3DFMT_X8R8G8B8 || f == D3DFMT_A8R8G8B8; }

// Picks the depth buffer format for the back buffer format (part of Init and RestoreDevice).
static HRESULT ChooseDepthFormat()
{
    const D3DFORMAT *list = g_bitDepth == 16 ? kDepthFormats16 : kDepthFormats32;
    const D3DFORMAT display = g_presentParams.BackBufferFormat;
    for (int i = 4; i--;) {
        if (gfx::g_pGfx->CheckDeviceFormat(g_init.adapter, g_deviceType, display, D3DUSAGE_DEPTHSTENCIL,
                                           D3DRTYPE_SURFACE, list[i]) >= 0 &&
            gfx::g_pGfx->CheckDepthStencilMatch(g_init.adapter, g_deviceType, display, display, list[i]) >= 0) {
            g_presentParams.AutoDepthStencilFormat = list[i];
            break;
        }
    }
    if (g_init.flags & IG_LOCKABLE_ZBUFFER) {
        g_presentParams.AutoDepthStencilFormat = D3DFMT_D16_LOCKABLE;
        return 0;
    }
    const D3DFORMAT depth = g_presentParams.AutoDepthStencilFormat;
    if (!depth) {
        dbgPrintf("IGraph::Init() - Error. Useful Z-Buffer format not found !!");
        return I3DERR_GENERIC;
    }
    if (depth == D3DFMT_D15S1 || depth == D3DFMT_D24X4S4 || depth == D3DFMT_D24S8)
        g_caps |= IGCAPS_STENCIL;
    return 0;
}

static void SetBitDepthFromFormat(D3DFORMAT format)
{
    if (Is16BitFormat(format))
        g_bitDepth = 16;
    if (Is32BitFormat(format))
        g_bitDepth = 32;
}

namespace ls3d::ig {

// 0x1006be50
HRESULT RestoreDevice(bool reset)
{
    g_vbManager.ReleaseDefaultPool();
    g_ibManager.ReleaseDefaultPool();
    if (g_init.flags & IG_TEXTURE_THREAD)
        tex::StreamFlush();
    if (reset) {
        if (!(g_init.flags & IG_FULLSCREEN)) {
            // The desktop format may have changed while the device was lost.
            D3DDISPLAYMODE mode;
            gfx::g_pGfx->GetAdapterDisplayMode(g_init.adapter, &mode);
            g_presentParams.BackBufferFormat = mode.Format;
            SetBitDepthFromFormat(mode.Format);
            const HRESULT hr = ChooseDepthFormat();
            if (hr < 0)
                return hr;
        }
        const HRESULT hr = gfx::g_pGfx->Reset(&g_presentParams);
        if (hr < 0) {
            LogDxError(hr, 0x7af);
            return I3DERR_GENERIC;
        }
        if (gfx::g_pGfx->HasDevice())
            i3d::ReapplyDriverStates();
        i3d::InvalidateLights();
        i3d::InitParticleBuffers();
    }

    struct Shader {
        const DWORD *decl;
        const DWORD *function;
        DWORD *handle;
        int line;
    };
    static const DWORD kDeclNormalTex1[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
                                            D3DVSD_REG(3, D3DVSDT_FLOAT3), D3DVSD_REG(7, D3DVSDT_FLOAT2),
                                            D3DVSD_END()};
    static const DWORD kDeclNormalTex2[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
                                            D3DVSD_REG(3, D3DVSDT_FLOAT3), D3DVSD_REG(7, D3DVSDT_FLOAT2),
                                            D3DVSD_REG(8, D3DVSDT_FLOAT2), D3DVSD_END()};
    static const DWORD kDeclColorTex1[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
                                           D3DVSD_REG(5, D3DVSDT_D3DCOLOR), D3DVSD_REG(7, D3DVSDT_FLOAT2),
                                           D3DVSD_END()};
    static const DWORD kDeclColorTex2[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
                                           D3DVSD_REG(5, D3DVSDT_D3DCOLOR), D3DVSD_REG(7, D3DVSDT_FLOAT2),
                                           D3DVSD_REG(8, D3DVSDT_FLOAT2), D3DVSD_END()};
    static const DWORD kDeclFarPlane[] = {D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
                                          D3DVSD_REG(1, D3DVSDT_D3DCOLOR), D3DVSD_REG(2, D3DVSDT_FLOAT2),
                                          D3DVSD_END()};
    // vs.1.0: dp4 oPos.x, v0, c0 / dp4 oPos.y, v0, c1 / dp4 oPos.z, v0, c3 / dp4 oPos.w, v0, c3 /
    // mov oD0, v1 (0x100a7810).
    static const DWORD kFarPlaneCode[] = {0xfffe0100, 0x9, 0xc0010000, 0x90e40000, 0xa0e40000, 0x9, 0xc0020000,
                                          0x90e40000, 0xa0e40001, 0x9, 0xc0040000, 0x90e40000, 0xa0e40003,
                                          0x9, 0xc0080000, 0x90e40000, 0xa0e40003, 0x1, 0xd00f0000, 0x90e40001,
                                          0xffff};
    const DWORD swUsage = (g_caps & IGCAPS_HW_TNL) ? 0 : D3DUSAGE_SOFTWAREPROCESSING;
    const Shader shaders[] = {
        {kDeclNormalTex1, nullptr, &g_vsNormalTex1, 0x7d4},
        {kDeclNormalTex2, nullptr, &g_vsNormalTex2, 0x7de},
        {kDeclColorTex1, nullptr, &g_vsColorTex1, 0x809},
        {kDeclColorTex2, nullptr, &g_vsColorTex2, 0x813},
        {kDeclFarPlane, kFarPlaneCode, &g_vsFarPlane, 0x81d},
    };
    for (const Shader &s : shaders) {
        const DWORD usage = s.function ? ((g_caps & IGCAPS_HW_VERTEX_SHADERS) ? 0 : D3DUSAGE_SOFTWAREPROCESSING)
                                       : swUsage;
        const HRESULT hr = gfx::g_pGfx->CreateVertexShader(s.decl, s.function, s.handle, usage);
        if (hr < 0) {
            LogDxError(hr, s.line);
            return I3DERR_GENERIC;
        }
    }
    return 0;
}

}  // namespace ls3d::ig

// 0x1006c270
HRESULT __stdcall IGraph::SetViewport(const S_IGraphViewport *viewport)
{
    std::memcpy(&g_viewport, viewport, sizeof g_viewport);
    return gfx::g_pGfx->SetViewport(&g_viewport) < 0 ? I3DERR_GENERIC : 0;
}

// 0x1006c2b0
const S_IGraphViewport *__stdcall IGraph::GetViewport()
{
    return reinterpret_cast<const S_IGraphViewport *>(&g_viewport);
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

// 0x1006deb0
uint32_t __stdcall IGraph::GetCaps()
{
    return g_caps;
}

// 0x1006dea0
D3DFORMAT __stdcall IGraph::GetBackBufferFormat()
{
    return g_presentParams.BackBufferFormat;
}

// 0x10057630
int __stdcall IGraph::ReservedD4()
{
    return 0;
}

// 0x1006f700
bool __stdcall IGraph::ReservedD8(int, int)
{
    return true;
}

// ---------------------------------------------------------------- texture lists, gamma

// 0x10070060
ITexture **__stdcall IGraph::GetCachedTextures()
{
    return g_cachedTextures.Data();
}

// 0x10070070
int __stdcall IGraph::NumCachedTextures()
{
    return g_cachedTextures.Size();
}

// 0x10070080
ITexture **__stdcall IGraph::GetUncachedTextures()
{
    return g_uncachedTextures.Data();
}

// 0x10070090
int __stdcall IGraph::NumUncachedTextures()
{
    return g_uncachedTextures.Size();
}

// 0x100700a0
bool __stdcall IGraph::SetGammaRamp(const uint16_t *ramp, bool calibrate)
{
    if (!(g_caps & IGCAPS_GAMMA))
        return false;
    gfx::g_pGfx->SetGammaRamp(calibrate ? D3DSGR_CALIBRATE : D3DSGR_NO_CALIBRATION,
                              reinterpret_cast<const D3DGAMMARAMP *>(ramp));
    return true;
}

// 0x100700e0
bool __stdcall IGraph::GetGammaRamp(uint16_t *ramp)
{
    if (!(g_caps & IGCAPS_GAMMA))
        return false;
    gfx::g_pGfx->GetGammaRamp(reinterpret_cast<D3DGAMMARAMP *>(ramp));
    if (g_caps & IGCAPS_GAMMA_8BIT) {
        for (int i = 0x300; i--;)
            ramp[i] = static_cast<uint16_t>(ramp[i] << 8 | ramp[i]);
    }
    return true;
}

// 0x10069e50 / 0x10069a00 run from the engine start-up (core/engine.cpp).
extern "C" IGraph *__cdecl GetIGraph()
{
    return g_pIGraph;
}
