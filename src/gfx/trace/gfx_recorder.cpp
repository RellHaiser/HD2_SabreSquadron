// The frame recorder (gfx/trace/recorder.h): a render device in front of the real one that saves one
// frame of the engine's drawing to a file when asked (Ctrl+Shift+F11), for the tests to replay on
// every backend and compare (gfx/trace/trace_format.h). Chosen with LS3DF.ini [Render]
// CaptureFrames=1; every call goes on to the real device unchanged.
//
// To write a frame it needs what the frame draws with, made long before. So while it runs it keeps:
// - the state the engine set (render and stage states, transforms, lights, ...), since the device
//   was made or reset;
// - a copy of every vertex and index buffer's bytes (the engine's are write-only), updated as the
//   engine unlocks them;
// - each default-pool texture's levels as UpdateTexture fills them (those cannot be locked);
// - the vertex shaders' declarations and functions.
// Managed textures are read back when the frame first uses them. The engine's texture thread makes
// device calls too: every call goes through one lock.
#include "gfx/trace/recorder.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "core/enhance.h"
#include "gfx/trace/trace_format.h"

namespace ls3d::gfx {

namespace {

using namespace trace;

struct S_textureInfo {
    UINT width, height, levels;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    std::vector<std::vector<uint8_t>> shadow;       // default pool: each level as UpdateTexture filled it
};

struct S_bufferInfo {
    UINT length;
    DWORD usage, fvf;
    D3DFORMAT format;
    D3DPOOL pool;
    std::vector<uint8_t> data;
    BYTE *locked = nullptr;                         // the engine's lock: where, from which offset, how much
    UINT lockOffset = 0, lockSize = 0;
    bool lockReadOnly = false;
};

struct S_surfaceInfo {
    Texture *texture = nullptr;                     // its texture and level (null: an image surface)
    UINT level = 0;
    D3DLOCKED_RECT locked = {};
    RECT rect = {};
    bool isLocked = false, lockReadOnly = false;
};

struct S_shader {
    DWORD usage;
    std::vector<DWORD> declaration, function;
};

std::atomic<bool> g_captureRequested{false};
char g_lastCapture[MAX_PATH];

uint32_t Id(const void *handle) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle)); }

class Recorder final : public Device {
public:
    explicit Recorder(Device *inner) : in(inner) {}

    // ---- the API and its adapters
    bool Open() override { return in->Open(); }
    bool IsOpen() override { return in->IsOpen(); }
    void Close() override { in->Close(); }
    UINT GetAdapterModeCount(UINT adapter) override { return in->GetAdapterModeCount(adapter); }
    HRESULT EnumAdapterModes(UINT adapter, UINT mode, D3DDISPLAYMODE *displayMode) override
    {
        return in->EnumAdapterModes(adapter, mode, displayMode);
    }
    HRESULT GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *displayMode) override
    {
        return in->GetAdapterDisplayMode(adapter, displayMode);
    }
    HRESULT GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER8 *identifier) override
    {
        return in->GetAdapterIdentifier(adapter, flags, identifier);
    }
    HRESULT GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS8 *caps) override
    {
        return in->GetDeviceCaps(adapter, type, caps);
    }
    HRESULT CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat, DWORD usage,
                              D3DRESOURCETYPE resourceType, D3DFORMAT format) override
    {
        return in->CheckDeviceFormat(adapter, type, adapterFormat, usage, resourceType, format);
    }
    HRESULT CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat,
                                   D3DFORMAT renderTargetFormat, D3DFORMAT depthStencilFormat) override
    {
        return in->CheckDepthStencilMatch(adapter, type, adapterFormat, renderTargetFormat, depthStencilFormat);
    }
    HRESULT CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT surfaceFormat, BOOL windowed,
                                       D3DMULTISAMPLE_TYPE samples) override
    {
        return in->CheckDeviceMultiSampleType(adapter, type, surfaceFormat, windowed, samples);
    }

    // ---- the device
    HRESULT CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focusWindow, DWORD behaviorFlags,
                         D3DPRESENT_PARAMETERS *params) override
    {
        Guard g(m);
        const HRESULT hr = in->CreateDevice(adapter, type, focusWindow, behaviorFlags, params);
        if (SUCCEEDED(hr)) {
            present = *params;
            ForgetState();
        }
        return hr;
    }
    bool HasDevice() override { return in->HasDevice(); }
    void ReleaseDevice() override
    {
        Guard g(m);
        StopCapture();
        in->ReleaseDevice();
    }
    HRESULT Reset(D3DPRESENT_PARAMETERS *params) override
    {
        Guard g(m);
        StopCapture();
        const HRESULT hr = in->Reset(params);
        if (SUCCEEDED(hr)) {
            present = *params;
            ForgetState();             // (a reset device starts from Direct3D's defaults)
        }
        return hr;
    }
    HRESULT TestCooperativeLevel() override { return in->TestCooperativeLevel(); }
    bool BorderlessFullScreen() override { return in->BorderlessFullScreen(); }
    HRESULT Present() override
    {
        Guard g(m);
        if (out) {
            EmitFrameImage();
            Emit(OP_PRESENT);
            StopCapture();
        }
        const HRESULT hr = in->Present();
        ++presents;
        if (CaptureAsked())
            StartCapture();
        return hr;
    }
    void SetGammaRamp(DWORD flags, const D3DGAMMARAMP *ramp) override
    {
        Guard g(m);
        if (out) {
            P p;
            p.U(flags).Bytes(ramp, sizeof *ramp);
            Emit(OP_GAMMA, p);
        }
        in->SetGammaRamp(flags, ramp);
    }
    void GetGammaRamp(D3DGAMMARAMP *ramp) override { in->GetGammaRamp(ramp); }

    // ---- frame
    HRESULT BeginScene() override
    {
        Guard g(m);
        if (out)
            Emit(OP_BEGIN_SCENE);
        return in->BeginScene();
    }
    HRESULT EndScene() override
    {
        Guard g(m);
        if (out)
            Emit(OP_END_SCENE);
        return in->EndScene();
    }
    HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override
    {
        Guard g(m);
        if (out) {
            P p;
            p.U(rects ? count : 0);
            if (rects)
                p.Bytes(rects, count * sizeof *rects);
            p.U(flags).U(color).F(z).U(stencil);
            Emit(OP_CLEAR, p);
        }
        return in->Clear(count, rects, flags, color, z, stencil);
    }
    UINT GetAvailableTextureMem() override { return in->GetAvailableTextureMem(); }
    HRESULT ResourceManagerDiscardBytes(DWORD bytes) override { return in->ResourceManagerDiscardBytes(bytes); }

    // ---- states
    HRESULT SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override
    {
        Guard g(m);
        renderStates[static_cast<uint32_t>(state)] = value;
        if (out)
            EmitRenderState(state, value);
        return in->SetRenderState(state, value);
    }
    HRESULT GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) override { return in->GetRenderState(state, value); }
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override
    {
        Guard g(m);
        stageStates[{stage, static_cast<uint32_t>(type)}] = value;
        if (out)
            EmitStageState(stage, type, value);
        return in->SetTextureStageState(stage, type, value);
    }
    HRESULT SetTexture(DWORD stage, Texture *texture) override
    {
        Guard g(m);
        textureOf[stage] = texture;
        if (out)
            EmitTexture(stage, texture);
        return in->SetTexture(stage, texture);
    }
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override
    {
        Guard g(m);
        transforms[static_cast<uint32_t>(state)] = *matrix;
        if (out)
            EmitTransform(state, *matrix);
        return in->SetTransform(state, matrix);
    }
    HRESULT SetViewport(const D3DVIEWPORT8 *viewport) override
    {
        Guard g(m);
        currentViewport = *viewport;
        hasViewport = true;
        if (out)
            Emit(OP_VIEWPORT, P().Bytes(viewport, sizeof *viewport));
        return in->SetViewport(viewport);
    }
    HRESULT SetMaterial(const D3DMATERIAL8 *material) override
    {
        Guard g(m);
        currentMaterial = *material;
        hasMaterial = true;
        if (out)
            Emit(OP_MATERIAL, P().Bytes(material, sizeof *material));
        return in->SetMaterial(material);
    }
    HRESULT SetLight(DWORD index, const D3DLIGHT8 *light) override
    {
        Guard g(m);
        lights[index] = *light;
        if (out)
            Emit(OP_LIGHT, P().U(index).Bytes(light, sizeof *light));
        return in->SetLight(index, light);
    }
    HRESULT LightEnable(DWORD index, BOOL enable) override
    {
        Guard g(m);
        lightOn[index] = enable;
        if (out)
            Emit(OP_LIGHT_ENABLE, P().U(index).U(enable ? 1u : 0u));
        return in->LightEnable(index, enable);
    }
    HRESULT CreateVertexShader(const DWORD *declaration, const DWORD *function, DWORD *handle, DWORD usage) override
    {
        Guard g(m);
        const HRESULT hr = in->CreateVertexShader(declaration, function, handle, usage);
        if (SUCCEEDED(hr)) {
            S_shader &s = shaders[*handle];
            s.usage = usage;
            s.declaration.clear();
            s.function.clear();
            if (declaration) {
                const DWORD *d = declaration;
                while (*d != D3DVSD_END())
                    s.declaration.push_back(*d++);
                s.declaration.push_back(*d);
            }
            if (function) {
                const DWORD *f = function;
                while (*f != 0x0000ffff)            // the end token
                    s.function.push_back(*f++);
                s.function.push_back(*f);
            }
        }
        return hr;
    }
    HRESULT SetVertexShader(DWORD handle) override
    {
        Guard g(m);
        vertexShader = handle;
        hasVertexShader = true;
        if (out)
            EmitVertexShader(handle);
        return in->SetVertexShader(handle);
    }
    HRESULT SetVertexShaderConstant(DWORD reg, const void *data, DWORD count) override
    {
        Guard g(m);
        const auto *f = static_cast<const float *>(data);
        for (DWORD i = 0; i < count; ++i)
            std::memcpy(constants[reg + i].data(), f + 4 * i, 16);
        if (out)
            Emit(OP_SHADER_CONSTANT, P().U(reg).U(count).Bytes(data, count * 16));
        return in->SetVertexShaderConstant(reg, data, count);
    }
    HRESULT SetStreamSource(UINT stream, VertexBuffer *buffer, UINT stride) override
    {
        Guard g(m);
        streams[stream] = {buffer, stride};
        if (out)
            EmitStream(stream, buffer, stride);
        return in->SetStreamSource(stream, buffer, stride);
    }
    HRESULT SetIndices(IndexBuffer *buffer, UINT baseVertexIndex) override
    {
        Guard g(m);
        indices = buffer;
        baseVertex = baseVertexIndex;
        hasIndices = true;
        if (out)
            EmitIndices(buffer, baseVertexIndex);
        return in->SetIndices(buffer, baseVertexIndex);
    }

    // ---- drawing
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices, UINT startIndex,
                                 UINT primitiveCount) override
    {
        Guard g(m);
        if (out)
            Emit(OP_DRAW_INDEXED, P().U(type).U(minIndex).U(numVertices).U(startIndex).U(primitiveCount));
        return in->DrawIndexedPrimitive(type, minIndex, numVertices, startIndex, primitiveCount);
    }
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitiveCount, const void *vertices, UINT stride) override
    {
        Guard g(m);
        if (out) {
            const uint32_t bytes = PrimitiveVertices(type, primitiveCount) * stride;
            Emit(OP_DRAW_UP, P().U(type).U(primitiveCount).U(stride).Bytes(vertices, bytes));
        }
        // (Direct3D 8 leaves stream 0 unset after a draw from user memory.)
        streams[0] = {nullptr, 0};
        return in->DrawPrimitiveUP(type, primitiveCount, vertices, stride);
    }
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices, UINT primitiveCount,
                                   const void *indexData, D3DFORMAT indexFormat, const void *vertices,
                                   UINT stride) override
    {
        Guard g(m);
        if (out) {
            const uint32_t indexBytes =
                PrimitiveVertices(type, primitiveCount) * (indexFormat == D3DFMT_INDEX32 ? 4u : 2u);
            P p;
            p.U(type).U(minIndex).U(numVertices).U(primitiveCount).U(indexFormat).U(stride).U(indexBytes);
            p.Bytes(indexData, indexBytes).Bytes(vertices, (minIndex + numVertices) * stride);
            Emit(OP_DRAW_INDEXED_UP, p);
        }
        // (And the index buffer, after an indexed one.)
        streams[0] = {nullptr, 0};
        indices = nullptr;
        baseVertex = 0;
        hasIndices = true;
        return in->DrawIndexedPrimitiveUP(type, minIndex, numVertices, primitiveCount, indexData, indexFormat,
                                          vertices, stride);
    }

    // ---- textures and surfaces
    HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                          Texture **texture) override
    {
        Guard g(m);
        const HRESULT hr = in->CreateTexture(width, height, levels, usage, format, pool, texture);
        if (SUCCEEDED(hr) && *texture) {
            ForgetTexture(*texture);
            S_textureInfo &t = textures[*texture];
            t = {width, height, in->GetLevelCount(*texture), usage, format, pool, {}};
        }
        return hr;
    }
    HRESULT CreateImageSurface(UINT width, UINT height, D3DFORMAT format, Surface **surface) override
    {
        Guard g(m);
        const HRESULT hr = in->CreateImageSurface(width, height, format, surface);
        if (SUCCEEDED(hr) && *surface)
            surfaces[*surface] = S_surfaceInfo{};
        return hr;
    }
    HRESULT UpdateTexture(Texture *source, Texture *destination) override
    {
        Guard g(m);
        const HRESULT hr = in->UpdateTexture(source, destination);
        auto d = textures.find(destination);
        if (SUCCEEDED(hr) && d != textures.end() && d->second.pool == D3DPOOL_DEFAULT) {
            // The destination's levels as the source has them (it is lockable).
            S_textureInfo &t = d->second;
            t.shadow.assign(t.levels, {});
            for (UINT level = 0; level < t.levels; ++level)
                ReadLevel(source, level, &t.shadow[level]);
            if (out && defined.count(Id(destination)))
                EmitTextureLevels(destination, t);
        }
        return hr;
    }
    HRESULT CopyRects(Surface *source, const RECT *sourceRects, UINT count, Surface *destination,
                      const POINT *destinationPoints) override
    {
        return in->CopyRects(source, sourceRects, count, destination, destinationPoints);
    }
    HRESULT GetRenderTarget(Surface **surface) override { return in->GetRenderTarget(surface); }
    HRESULT GetDepthStencilSurface(Surface **surface) override { return in->GetDepthStencilSurface(surface); }

    ULONG AddRef(Texture *texture) override { return in->AddRef(texture); }
    ULONG Release(Texture *texture) override
    {
        Guard g(m);
        const ULONG left = in->Release(texture);
        if (!left)
            Dropped(texture);
        return left;
    }
    DWORD GetLevelCount(Texture *texture) override { return in->GetLevelCount(texture); }
    HRESULT GetLevelDesc(Texture *texture, UINT level, D3DSURFACE_DESC *desc) override
    {
        return in->GetLevelDesc(texture, level, desc);
    }
    HRESULT GetSurfaceLevel(Texture *texture, UINT level, Surface **surface) override
    {
        Guard g(m);
        const HRESULT hr = in->GetSurfaceLevel(texture, level, surface);
        if (SUCCEEDED(hr) && *surface) {
            S_surfaceInfo &s = surfaces[*surface];
            s.texture = texture;
            s.level = level;
        }
        return hr;
    }

    ULONG Release(Surface *surface) override
    {
        Guard g(m);
        auto s = surfaces.find(surface);
        Texture *texture = s != surfaces.end() ? s->second.texture : nullptr;
        const ULONG left = in->Release(surface);
        // (A texture's level shares the texture's count: the last release can be the level's.)
        if (!left) {
            if (texture)
                Dropped(texture);
            else if (s != surfaces.end())
                surfaces.erase(s);
        }
        return left;
    }
    HRESULT GetDesc(Surface *surface, D3DSURFACE_DESC *desc) override { return in->GetDesc(surface, desc); }
    HRESULT LockRect(Surface *surface, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override
    {
        Guard g(m);
        const HRESULT hr = in->LockRect(surface, locked, rect, flags);
        auto s = surfaces.find(surface);
        if (SUCCEEDED(hr) && s != surfaces.end()) {
            S_surfaceInfo &i = s->second;
            i.isLocked = true;
            i.lockReadOnly = (flags & D3DLOCK_READONLY) != 0;
            i.locked = *locked;
            D3DSURFACE_DESC desc = {};
            in->GetDesc(surface, &desc);
            i.rect = rect ? *rect : RECT{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
        }
        return hr;
    }
    HRESULT UnlockRect(Surface *surface) override
    {
        Guard g(m);
        auto s = surfaces.find(surface);
        if (s != surfaces.end() && s->second.isLocked) {
            S_surfaceInfo &i = s->second;
            i.isLocked = false;
            // Written into a texture the capture has: the new bytes.
            if (out && !i.lockReadOnly && i.texture && defined.count(Id(i.texture)))
                EmitRect(i.texture, i.level, i.rect, static_cast<const uint8_t *>(i.locked.pBits), i.locked.Pitch);
        }
        return in->UnlockRect(surface);
    }

    // ---- vertex and index buffers
    HRESULT CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, VertexBuffer **buffer) override
    {
        Guard g(m);
        const HRESULT hr = in->CreateVertexBuffer(length, usage, fvf, pool, buffer);
        if (SUCCEEDED(hr) && *buffer) {
            S_bufferInfo &b = vertexBuffers[*buffer];
            b = {length, usage, fvf, D3DFMT_UNKNOWN, pool};
            b.data.assign(length, 0);
        }
        return hr;
    }
    HRESULT CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                              IndexBuffer **buffer) override
    {
        Guard g(m);
        const HRESULT hr = in->CreateIndexBuffer(length, usage, format, pool, buffer);
        if (SUCCEEDED(hr) && *buffer) {
            S_bufferInfo &b = indexBuffers[*buffer];
            b = {length, usage, 0, format, pool};
            b.data.assign(length, 0);
        }
        return hr;
    }
    ULONG Release(VertexBuffer *buffer) override
    {
        Guard g(m);
        const ULONG left = in->Release(buffer);
        if (!left) {
            vertexBuffers.erase(buffer);
            Drop(buffer);
        }
        return left;
    }
    HRESULT Lock(VertexBuffer *buffer, UINT offset, UINT size, BYTE **data, DWORD flags) override
    {
        Guard g(m);
        const HRESULT hr = in->Lock(buffer, offset, size, data, flags);
        if (SUCCEEDED(hr))
            Locked(vertexBuffers, buffer, offset, size, *data, flags);
        return hr;
    }
    HRESULT Unlock(VertexBuffer *buffer) override
    {
        Guard g(m);
        Unlocked(vertexBuffers, buffer, OP_VERTEX_DATA);
        return in->Unlock(buffer);
    }
    ULONG Release(IndexBuffer *buffer) override
    {
        Guard g(m);
        const ULONG left = in->Release(buffer);
        if (!left) {
            indexBuffers.erase(buffer);
            Drop(buffer);
        }
        return left;
    }
    HRESULT Lock(IndexBuffer *buffer, UINT offset, UINT size, BYTE **data, DWORD flags) override
    {
        Guard g(m);
        const HRESULT hr = in->Lock(buffer, offset, size, data, flags);
        if (SUCCEEDED(hr))
            Locked(indexBuffers, buffer, offset, size, *data, flags);
        return hr;
    }
    HRESULT Unlock(IndexBuffer *buffer) override
    {
        Guard g(m);
        Unlocked(indexBuffers, buffer, OP_INDEX_DATA);
        return in->Unlock(buffer);
    }

private:
    using Guard = std::lock_guard<std::recursive_mutex>;

    // A record's payload.
    struct P {
        std::vector<uint8_t> bytes;
        P &U(uint32_t v) { return Bytes(&v, 4); }
        P &F(float v) { return Bytes(&v, 4); }
        P &Bytes(const void *data, size_t n)
        {
            const auto *b = static_cast<const uint8_t *>(data);
            bytes.insert(bytes.end(), b, b + n);
            return *this;
        }
    };

    void Emit(E_op op, const P &p = P())
    {
        const uint32_t head[2] = {op, static_cast<uint32_t>(p.bytes.size())};
        std::fwrite(head, sizeof head, 1, out);
        if (!p.bytes.empty())
            std::fwrite(p.bytes.data(), p.bytes.size(), 1, out);
    }

    // ---- what the engine set
    void ForgetState()
    {
        renderStates.clear();
        stageStates.clear();
        textureOf.clear();
        transforms.clear();
        lights.clear();
        lightOn.clear();
        constants.clear();
        streams.clear();
        hasViewport = hasMaterial = hasVertexShader = hasIndices = false;
    }

    // ---- resources
    template <class H, class Info>
    void Locked(std::unordered_map<H *, Info> &map, H *buffer, UINT offset, UINT size, BYTE *data, DWORD flags)
    {
        auto b = map.find(buffer);
        if (b == map.end())
            return;
        S_bufferInfo &i = b->second;
        i.locked = data;
        i.lockOffset = offset < i.length ? offset : i.length;
        i.lockSize = size ? size : i.length - i.lockOffset;
        if (i.lockOffset + i.lockSize > i.length)
            i.lockSize = i.length - i.lockOffset;
        i.lockReadOnly = (flags & D3DLOCK_READONLY) != 0;
    }
    template <class H, class Info>
    void Unlocked(std::unordered_map<H *, Info> &map, H *buffer, E_op op)
    {
        auto b = map.find(buffer);
        if (b == map.end() || !b->second.locked)
            return;
        S_bufferInfo &i = b->second;
        if (!i.lockReadOnly) {
            std::memcpy(i.data.data() + i.lockOffset, i.locked, i.lockSize);
            if (out && defined.count(Id(buffer)))
                Emit(op, P().U(Id(buffer)).U(i.lockOffset).Bytes(i.locked, i.lockSize));
        }
        i.locked = nullptr;
    }

    // A texture gone: forgotten (and dropped from the capture).
    void Dropped(Texture *texture)
    {
        ForgetTexture(texture);
        Drop(texture);
    }
    void ForgetTexture(Texture *texture)
    {
        textures.erase(texture);
        for (auto s = surfaces.begin(); s != surfaces.end();) {
            if (s->second.texture == texture)
                s = surfaces.erase(s);
            else
                ++s;
        }
    }
    void Drop(const void *handle)
    {
        if (out && defined.erase(Id(handle)))
            Emit(OP_DROP, P().U(Id(handle)));
    }

    // A level's rows as the texture has them (false: it cannot be locked).
    bool ReadLevel(Texture *texture, UINT level, std::vector<uint8_t> *rows)
    {
        D3DSURFACE_DESC desc = {};
        uint32_t rowBytes, rowCount;
        if (FAILED(in->GetLevelDesc(texture, level, &desc)) ||
            !FormatRows(desc.Format, desc.Width, desc.Height, &rowBytes, &rowCount))
            return false;
        Surface *surface = nullptr;
        if (FAILED(in->GetSurfaceLevel(texture, level, &surface)) || !surface)
            return false;
        D3DLOCKED_RECT locked = {};
        bool ok = false;
        if (SUCCEEDED(in->LockRect(surface, &locked, nullptr, D3DLOCK_READONLY))) {
            rows->resize(static_cast<size_t>(rowBytes) * rowCount);
            for (uint32_t r = 0; r < rowCount; ++r)
                std::memcpy(rows->data() + static_cast<size_t>(r) * rowBytes,
                            static_cast<const uint8_t *>(locked.pBits) + static_cast<size_t>(r) * locked.Pitch, rowBytes);
            in->UnlockRect(surface);
            ok = true;
        }
        in->Release(surface);
        return ok;
    }

    // ---- the capture
    bool CaptureAsked()
    {
#ifdef _WIN32
        const bool down = (GetAsyncKeyState(VK_F11) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                          (GetAsyncKeyState(VK_SHIFT) & 0x8000);
#else
        const bool down = false;
#endif
        const bool asked = (down && !keyWasDown) || g_captureRequested.exchange(false);
        keyWasDown = down;
        return asked;
    }

    void StartCapture()
    {
        if (out || !in->HasDevice())
            return;
        char path[MAX_PATH];
        if (!CapturePath(path))
            return;
        out = std::fopen(path, "wb");
        if (!out) {
            EnhancementLog("LS3DF.ini CaptureFrames: cannot write %s", path);
            return;
        }
        std::strncpy(fileName, path, sizeof fileName - 1);
        std::fwrite(kMagic, sizeof kMagic, 1, out);
        defined.clear();
        definedShaders.clear();
        P h;
        h.U(present.BackBufferWidth).U(present.BackBufferHeight).U(present.BackBufferFormat);
        h.U(present.EnableAutoDepthStencil ? present.AutoDepthStencilFormat : 0).U(present.MultiSampleType);
        h.U(present.Windowed ? 1u : 0u).U(present.Flags);
        Emit(OP_HEADER, h);
        // The state the frame starts with.
        for (const auto &s : renderStates)
            EmitRenderState(static_cast<D3DRENDERSTATETYPE>(s.first), s.second);
        for (const auto &s : stageStates)
            EmitStageState(s.first.first, static_cast<D3DTEXTURESTAGESTATETYPE>(s.first.second), s.second);
        for (const auto &t : transforms)
            EmitTransform(static_cast<D3DTRANSFORMSTATETYPE>(t.first), t.second);
        if (hasViewport)
            Emit(OP_VIEWPORT, P().Bytes(&currentViewport, sizeof currentViewport));
        if (hasMaterial)
            Emit(OP_MATERIAL, P().Bytes(&currentMaterial, sizeof currentMaterial));
        for (const auto &l : lights)
            Emit(OP_LIGHT, P().U(l.first).Bytes(&l.second, sizeof l.second));
        for (const auto &l : lightOn)
            Emit(OP_LIGHT_ENABLE, P().U(l.first).U(l.second ? 1u : 0u));
        for (const auto &c : constants)
            Emit(OP_SHADER_CONSTANT, P().U(c.first).U(1).Bytes(c.second.data(), 16));
        if (hasVertexShader)
            EmitVertexShader(vertexShader);
        for (const auto &t : textureOf)
            EmitTexture(t.first, t.second);
        for (const auto &s : streams)
            EmitStream(s.first, s.second.first, s.second.second);
        if (hasIndices)
            EmitIndices(indices, baseVertex);
        EnhancementLog("LS3DF.ini CaptureFrames: frame %u to %s", presents, fileName);
    }
    void StopCapture()
    {
        if (!out)
            return;
        std::fclose(out);
        out = nullptr;
        defined.clear();
        std::strcpy(g_lastCapture, fileName);
    }
    // LS3DF_frames\frame_<n>.ls3dframe next to the DLL.
    bool CapturePath(char *path)
    {
#ifdef _WIN32
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(&FrameRecorder), &module))
            return false;
        char dir[MAX_PATH];
        const DWORD n = GetModuleFileNameA(module, dir, MAX_PATH);
        if (!n || n >= MAX_PATH)
            return false;
        char *slash = std::strrchr(dir, '\\');
        if (!slash)
            return false;
        slash[1] = 0;
        if (std::strlen(dir) + 40 >= MAX_PATH)
            return false;
        std::strcat(dir, "LS3DF_frames");
        CreateDirectoryA(dir, nullptr);
        for (unsigned i = 1; i < 100000; ++i) {
            std::snprintf(path, MAX_PATH, "%s\\frame_%05u.ls3dframe", dir, i);
            if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
                return true;
        }
#endif
        return false;
    }

    // The back buffer as the game drew it, copied out as IGraph's screenshots are.
    void EmitFrameImage()
    {
        Surface *target = nullptr, *image = nullptr;
        if (FAILED(in->GetRenderTarget(&target)) || !target)
            return;
        D3DSURFACE_DESC desc = {};
        uint32_t rowBytes, rowCount;
        if (SUCCEEDED(in->GetDesc(target, &desc)) && FormatRows(desc.Format, desc.Width, desc.Height, &rowBytes, &rowCount) &&
            SUCCEEDED(in->CreateImageSurface(desc.Width, desc.Height, desc.Format, &image)) && image) {
            D3DLOCKED_RECT locked = {};
            if (SUCCEEDED(in->CopyRects(target, nullptr, 0, image, nullptr)) &&
                SUCCEEDED(in->LockRect(image, &locked, nullptr, D3DLOCK_READONLY))) {
                P p;
                p.U(desc.Width).U(desc.Height).U(desc.Format);
                for (uint32_t r = 0; r < rowCount; ++r)
                    p.Bytes(static_cast<const uint8_t *>(locked.pBits) + static_cast<size_t>(r) * locked.Pitch, rowBytes);
                Emit(OP_FRAME_IMAGE, p);
                in->UnlockRect(image);
            }
            in->Release(image);
        }
        in->Release(target);
    }

    void EmitRenderState(D3DRENDERSTATETYPE state, DWORD value) { Emit(OP_RENDER_STATE, P().U(state).U(value)); }
    void EmitStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
    {
        Emit(OP_STAGE_STATE, P().U(stage).U(type).U(value));
    }
    void EmitTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX &matrix)
    {
        Emit(OP_TRANSFORM, P().U(state).Bytes(&matrix, sizeof matrix));
    }
    void EmitVertexShader(DWORD handle)
    {
        auto s = shaders.find(handle);
        if (s != shaders.end() && !definedShaders.count(handle)) {
            definedShaders.insert(handle);
            P p;
            p.U(handle).U(s->second.usage).U(static_cast<uint32_t>(s->second.declaration.size()));
            p.U(static_cast<uint32_t>(s->second.function.size()));
            p.Bytes(s->second.declaration.data(), s->second.declaration.size() * 4);
            p.Bytes(s->second.function.data(), s->second.function.size() * 4);
            Emit(OP_DEF_SHADER, p);
        }
        Emit(OP_VERTEX_SHADER, P().U(handle));
    }
    void EmitTexture(DWORD stage, Texture *texture)
    {
        if (texture)
            DefineTexture(texture);
        Emit(OP_TEXTURE, P().U(stage).U(Id(texture)));
    }
    void EmitStream(UINT stream, VertexBuffer *buffer, UINT stride)
    {
        if (buffer)
            DefineBuffer(vertexBuffers, buffer, OP_DEF_VERTEX_BUFFER);
        Emit(OP_STREAM, P().U(stream).U(Id(buffer)).U(stride));
    }
    void EmitIndices(IndexBuffer *buffer, UINT base)
    {
        if (buffer)
            DefineBuffer(indexBuffers, buffer, OP_DEF_INDEX_BUFFER);
        Emit(OP_INDICES, P().U(Id(buffer)).U(base));
    }

    void DefineTexture(Texture *texture)
    {
        if (defined.count(Id(texture)))
            return;
        auto t = textures.find(texture);
        if (t == textures.end())
            return;
        defined.insert(Id(texture));
        const S_textureInfo &i = t->second;
        Emit(OP_DEF_TEXTURE, P().U(Id(texture)).U(i.width).U(i.height).U(i.levels).U(i.usage).U(i.format).U(i.pool));
        if (i.pool == D3DPOOL_DEFAULT) {
            EmitTextureLevels(texture, i);
            return;
        }
        std::vector<uint8_t> rows;
        for (UINT level = 0; level < i.levels; ++level) {
            if (ReadLevel(texture, level, &rows))
                EmitLevel(texture, level, rows);
        }
    }
    void EmitTextureLevels(Texture *texture, const S_textureInfo &i)
    {
        for (UINT level = 0; level < i.shadow.size(); ++level) {
            if (!i.shadow[level].empty())
                EmitLevel(texture, level, i.shadow[level]);
        }
    }
    void EmitLevel(Texture *texture, UINT level, const std::vector<uint8_t> &rows)
    {
        D3DSURFACE_DESC desc = {};
        in->GetLevelDesc(texture, level, &desc);
        P p;
        p.U(Id(texture)).U(level).U(0).U(0).U(desc.Width).U(desc.Height).Bytes(rows.data(), rows.size());
        Emit(OP_TEXTURE_DATA, p);
    }
    // A locked rectangle's new bytes (DXT: whole blocks).
    void EmitRect(Texture *texture, UINT level, const RECT &rect, const uint8_t *bits, INT pitch)
    {
        D3DSURFACE_DESC desc = {};
        in->GetLevelDesc(texture, level, &desc);
        uint32_t rowBytes, rowCount;
        const uint32_t width = static_cast<uint32_t>(rect.right - rect.left);
        const uint32_t height = static_cast<uint32_t>(rect.bottom - rect.top);
        if (!FormatRows(desc.Format, width, height, &rowBytes, &rowCount))
            return;
        P p;
        p.U(Id(texture)).U(level).U(rect.left).U(rect.top).U(rect.right).U(rect.bottom);
        for (uint32_t r = 0; r < rowCount; ++r)
            p.Bytes(bits + static_cast<size_t>(r) * pitch, rowBytes);
        Emit(OP_TEXTURE_DATA, p);
    }
    template <class H>
    void DefineBuffer(std::unordered_map<H *, S_bufferInfo> &map, H *buffer, E_op op)
    {
        if (defined.count(Id(buffer)))
            return;
        auto b = map.find(buffer);
        if (b == map.end())
            return;
        defined.insert(Id(buffer));
        const S_bufferInfo &i = b->second;
        P p;
        p.U(Id(buffer)).U(i.length).U(i.usage).U(op == OP_DEF_VERTEX_BUFFER ? i.fvf : static_cast<uint32_t>(i.format));
        p.U(i.pool).Bytes(i.data.data(), i.data.size());
        Emit(op, p);
    }

    Device *in;
    std::recursive_mutex m;

    // What the engine set since the device was made or reset.
    std::map<uint32_t, DWORD> renderStates;
    std::map<std::pair<uint32_t, uint32_t>, DWORD> stageStates;
    std::map<DWORD, Texture *> textureOf;
    std::map<uint32_t, D3DMATRIX> transforms;
    D3DVIEWPORT8 currentViewport = {};
    D3DMATERIAL8 currentMaterial = {};
    std::map<DWORD, D3DLIGHT8> lights;
    std::map<DWORD, BOOL> lightOn;
    std::map<DWORD, std::array<float, 4>> constants;
    std::map<UINT, std::pair<VertexBuffer *, UINT>> streams;
    IndexBuffer *indices = nullptr;
    UINT baseVertex = 0;
    DWORD vertexShader = 0;
    bool hasViewport = false, hasMaterial = false, hasVertexShader = false, hasIndices = false;

    // The resources.
    std::unordered_map<Texture *, S_textureInfo> textures;
    std::unordered_map<Surface *, S_surfaceInfo> surfaces;
    std::unordered_map<VertexBuffer *, S_bufferInfo> vertexBuffers;
    std::unordered_map<IndexBuffer *, S_bufferInfo> indexBuffers;
    std::map<DWORD, S_shader> shaders;

    // The capture.
    D3DPRESENT_PARAMETERS present = {};
    FILE *out = nullptr;
    char fileName[MAX_PATH] = {};
    std::set<uint32_t> defined;
    std::set<DWORD> definedShaders;
    unsigned presents = 0;
    bool keyWasDown = false;
};

}  // namespace

Device *FrameRecorder(Device *inner) { return new Recorder(inner); }
void RequestFrameCapture()
{
    g_lastCapture[0] = 0;
    g_captureRequested = true;
}
const char *LastFrameCapture() { return g_lastCapture; }

}  // namespace ls3d::gfx
