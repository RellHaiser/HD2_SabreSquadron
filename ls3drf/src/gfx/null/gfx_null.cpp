// The DLL's render device (gfx/gfx.h): there is none. LS3DRF.dll has no Direct3D (no d3d8.dll, no
// Direct3DCreate8), so the API never opens: IGraph::Init stops where LS3DF's stops without d3d8.dll
// ("Error create D3D8 object !!").
//
// Every call fails with D3DERR_INVALIDCALL; never D3DERR_DEVICELOST or D3DERR_DEVICENOTRESET, so
// Flip does not try to restore a device. Counts, reference counts and texture memory are 0. A
// handle or data pointer the call returns is set to null; structures it fills are left as they are.
// (The compare tests link the Direct3D 8 backend, tests/d3d8, instead.)
#include "gfx/gfx.h"

namespace ls3d::gfx {

namespace {

constexpr HRESULT kNoDevice = D3DERR_INVALIDCALL;

template <typename T>
HRESULT NoObject(T **out)
{
    *out = nullptr;
    return kNoDevice;
}

class DeviceNull final : public Device {
public:
    bool Open() override { return false; }
    bool IsOpen() override { return false; }
    void Close() override {}
    UINT GetAdapterModeCount(UINT) override { return 0; }
    HRESULT EnumAdapterModes(UINT, UINT, D3DDISPLAYMODE *) override { return kNoDevice; }
    HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *) override { return kNoDevice; }
    HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8 *) override { return kNoDevice; }
    HRESULT GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS8 *) override { return kNoDevice; }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) override
    {
        return kNoDevice;
    }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return kNoDevice; }
    HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE) override
    {
        return kNoDevice;
    }

    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *) override { return kNoDevice; }
    bool HasDevice() override { return false; }
    void ReleaseDevice() override {}
    HRESULT Reset(D3DPRESENT_PARAMETERS *) override { return kNoDevice; }
    HRESULT TestCooperativeLevel() override { return kNoDevice; }
    HRESULT Present() override { return kNoDevice; }
    void SetGammaRamp(DWORD, const D3DGAMMARAMP *) override {}
    void GetGammaRamp(D3DGAMMARAMP *) override {}

    HRESULT BeginScene() override { return kNoDevice; }
    HRESULT EndScene() override { return kNoDevice; }
    HRESULT Clear(DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD) override { return kNoDevice; }
    UINT GetAvailableTextureMem() override { return 0; }
    HRESULT ResourceManagerDiscardBytes(DWORD) override { return kNoDevice; }

    HRESULT SetRenderState(D3DRENDERSTATETYPE, DWORD) override { return kNoDevice; }
    HRESULT GetRenderState(D3DRENDERSTATETYPE, DWORD *) override { return kNoDevice; }
    HRESULT SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) override { return kNoDevice; }
    HRESULT SetTexture(DWORD, Texture *) override { return kNoDevice; }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override { return kNoDevice; }
    HRESULT SetViewport(const D3DVIEWPORT8 *) override { return kNoDevice; }
    HRESULT SetMaterial(const D3DMATERIAL8 *) override { return kNoDevice; }
    HRESULT SetLight(DWORD, const D3DLIGHT8 *) override { return kNoDevice; }
    HRESULT LightEnable(DWORD, BOOL) override { return kNoDevice; }
    HRESULT CreateVertexShader(const DWORD *, const DWORD *, DWORD *, DWORD) override { return kNoDevice; }
    HRESULT SetVertexShader(DWORD) override { return kNoDevice; }
    HRESULT SetVertexShaderConstant(DWORD, const void *, DWORD) override { return kNoDevice; }
    HRESULT SetStreamSource(UINT, VertexBuffer *, UINT) override { return kNoDevice; }
    HRESULT SetIndices(IndexBuffer *, UINT) override { return kNoDevice; }

    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT) override { return kNoDevice; }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *, UINT) override { return kNoDevice; }
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *,
                                   UINT) override
    {
        return kNoDevice;
    }

    HRESULT CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, Texture **texture) override
    {
        return NoObject(texture);
    }
    HRESULT CreateImageSurface(UINT, UINT, D3DFORMAT, Surface **surface) override { return NoObject(surface); }
    HRESULT UpdateTexture(Texture *, Texture *) override { return kNoDevice; }
    HRESULT CopyRects(Surface *, const RECT *, UINT, Surface *, const POINT *) override { return kNoDevice; }
    HRESULT GetRenderTarget(Surface **surface) override { return NoObject(surface); }
    HRESULT GetDepthStencilSurface(Surface **surface) override { return NoObject(surface); }

    ULONG AddRef(Texture *) override { return 0; }
    ULONG Release(Texture *) override { return 0; }
    DWORD GetLevelCount(Texture *) override { return 0; }
    HRESULT GetLevelDesc(Texture *, UINT, D3DSURFACE_DESC *) override { return kNoDevice; }
    HRESULT GetSurfaceLevel(Texture *, UINT, Surface **surface) override { return NoObject(surface); }

    ULONG Release(Surface *) override { return 0; }
    HRESULT GetDesc(Surface *, D3DSURFACE_DESC *) override { return kNoDevice; }
    HRESULT LockRect(Surface *, D3DLOCKED_RECT *, const RECT *, DWORD) override { return kNoDevice; }
    HRESULT UnlockRect(Surface *) override { return kNoDevice; }

    HRESULT CreateVertexBuffer(UINT, DWORD, DWORD, D3DPOOL, VertexBuffer **buffer) override
    {
        return NoObject(buffer);
    }
    HRESULT CreateIndexBuffer(UINT, DWORD, D3DFORMAT, D3DPOOL, IndexBuffer **buffer) override
    {
        return NoObject(buffer);
    }
    ULONG Release(VertexBuffer *) override { return 0; }
    HRESULT Lock(VertexBuffer *, UINT, UINT, BYTE **data, DWORD) override { return NoObject(data); }
    HRESULT Unlock(VertexBuffer *) override { return kNoDevice; }
    ULONG Release(IndexBuffer *) override { return 0; }
    HRESULT Lock(IndexBuffer *, UINT, UINT, BYTE **data, DWORD) override { return NoObject(data); }
    HRESULT Unlock(IndexBuffer *) override { return kNoDevice; }
};

DeviceNull s_device;

}  // namespace

Device *g_pGfx = &s_device;

}  // namespace ls3d::gfx
