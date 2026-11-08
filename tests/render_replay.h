// Replays a captured frame (src/gfx/trace/trace_format.h) on a render device and reads back the
// picture it draws, for the render tests. The device is any gfx::Device: the Direct3D 8 backend's
// now, the SDL_GPU backend's to compare with it.
#pragma once

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../src/gfx/gfx.h"
#include "../src/gfx/trace/trace_format.h"

namespace replay {

using namespace ls3d;
using namespace ls3d::gfx;
using namespace ls3d::gfx::trace;

// A picture as 8-bit B, G, R (A ignored) per pixel.
struct S_image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bgr;           // width * height * 3
};

// The rows of `format` (FormatRows) as BGR; false for a format it cannot show.
inline bool ToBgr(D3DFORMAT format, uint32_t width, uint32_t height, const uint8_t *rows, uint32_t rowBytes,
                  S_image *image)
{
    image->width = width;
    image->height = height;
    image->bgr.assign(static_cast<size_t>(width) * height * 3, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = rows + static_cast<size_t>(y) * rowBytes;
        uint8_t *dst = image->bgr.data() + static_cast<size_t>(y) * width * 3;
        for (uint32_t x = 0; x < width; ++x, dst += 3) {
            switch (format) {
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8:
                dst[0] = row[x * 4];
                dst[1] = row[x * 4 + 1];
                dst[2] = row[x * 4 + 2];
                break;
            case D3DFMT_R8G8B8:
                std::memcpy(dst, row + x * 3, 3);
                break;
            case D3DFMT_R5G6B5: {
                const uint32_t v = row[x * 2] | row[x * 2 + 1] << 8;
                dst[0] = static_cast<uint8_t>((v & 31) * 255 / 31);
                dst[1] = static_cast<uint8_t>((v >> 5 & 63) * 255 / 63);
                dst[2] = static_cast<uint8_t>((v >> 11 & 31) * 255 / 31);
                break;
            }
            case D3DFMT_X1R5G5B5:
            case D3DFMT_A1R5G5B5: {
                const uint32_t v = row[x * 2] | row[x * 2 + 1] << 8;
                dst[0] = static_cast<uint8_t>((v & 31) * 255 / 31);
                dst[1] = static_cast<uint8_t>((v >> 5 & 31) * 255 / 31);
                dst[2] = static_cast<uint8_t>((v >> 10 & 31) * 255 / 31);
                break;
            }
            default:
                return false;
            }
        }
    }
    return true;
}

inline bool WriteBmp(const char *path, const S_image &image)
{
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    const uint32_t rowBytes = (image.width * 3 + 3) & ~3u;
    const uint32_t dataBytes = rowBytes * image.height;
    uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { std::memcpy(header + at, &v, 4); };
    put32(2, 54 + dataBytes);
    put32(10, 54);
    put32(14, 40);
    put32(18, image.width);
    put32(22, image.height);
    header[26] = 1;
    header[28] = 24;
    put32(34, dataBytes);
    std::fwrite(header, sizeof header, 1, f);
    std::vector<uint8_t> row(rowBytes, 0);
    for (uint32_t y = image.height; y-- > 0;) {
        std::memcpy(row.data(), image.bgr.data() + static_cast<size_t>(y) * image.width * 3, image.width * 3);
        std::fwrite(row.data(), rowBytes, 1, f);
    }
    std::fclose(f);
    return true;
}

// A PNG (deflate's stored blocks: no compression library needed).
inline bool WritePng(const char *path, const S_image &image)
{
    std::vector<uint8_t> raw;           // filter byte 0, then RGB
    raw.reserve((image.width * 3 + 1) * static_cast<size_t>(image.height));
    for (uint32_t y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const uint8_t *row = image.bgr.data() + static_cast<size_t>(y) * image.width * 3;
        for (uint32_t x = 0; x < image.width; ++x) {
            raw.push_back(row[x * 3 + 2]);
            raw.push_back(row[x * 3 + 1]);
            raw.push_back(row[x * 3]);
        }
    }
    uint32_t crcTable[256];
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
    auto be32 = [](std::vector<uint8_t> &v, uint32_t x) {
        for (int i = 3; i >= 0; --i)
            v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    };
    std::vector<uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto chunk = [&](const char *type, const std::vector<uint8_t> &body) {
        be32(file, static_cast<uint32_t>(body.size()));
        const size_t start = file.size();
        file.insert(file.end(), type, type + 4);
        file.insert(file.end(), body.begin(), body.end());
        uint32_t c = 0xffffffffu;
        for (size_t i = start; i < file.size(); ++i)
            c = crcTable[(c ^ file[i]) & 0xff] ^ (c >> 8);
        be32(file, c ^ 0xffffffffu);
    };
    std::vector<uint8_t> ihdr;
    be32(ihdr, image.width);
    be32(ihdr, image.height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr);
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    size_t at = 0;
    do {
        const size_t n = raw.size() - at < 65535 ? raw.size() - at : 65535;
        z.push_back(at + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n));
        z.push_back(static_cast<uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
    } while (at < raw.size());
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    std::fwrite(file.data(), file.size(), 1, f);
    std::fclose(f);
    return true;
}

// How far two pictures of one size are apart.
struct S_difference {
    uint32_t maxChannel = 0;            // the largest channel difference
    uint64_t pixelsOver = 0;            // pixels with a channel more than `tolerance` apart
    double psnr = 0;                    // dB (1000: identical)
};
inline S_difference Compare(const S_image &a, const S_image &b, uint32_t tolerance, S_image *diff = nullptr)
{
    S_difference d;
    double squares = 0;
    if (diff) {
        diff->width = a.width;
        diff->height = a.height;
        diff->bgr.assign(a.bgr.size(), 0);
    }
    for (size_t p = 0; p + 2 < a.bgr.size() && p + 2 < b.bgr.size(); p += 3) {
        uint32_t worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int e = static_cast<int>(a.bgr[p + c]) - static_cast<int>(b.bgr[p + c]);
            const uint32_t m = static_cast<uint32_t>(e < 0 ? -e : e);
            squares += static_cast<double>(e) * e;
            worst = m > worst ? m : worst;
        }
        if (worst > d.maxChannel)
            d.maxChannel = worst;
        if (worst > tolerance)
            ++d.pixelsOver;
        if (diff) {
            const uint8_t v = static_cast<uint8_t>(worst * 4 > 255 ? 255 : worst * 4);
            diff->bgr[p] = diff->bgr[p + 1] = diff->bgr[p + 2] = v;
        }
    }
    const double mse = squares / static_cast<double>(a.bgr.size() ? a.bgr.size() : 1);
    d.psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 1000.0;
    return d;
}

// One capture's records, read one at a time (a capture can be hundreds of MB: more than a 32-bit
// process has room for beside the device).
class Capture {
public:
    ~Capture() { Close(); }
    bool Open(const char *path)
    {
        Close();
        file = std::fopen(path, "rb");
        char magic[sizeof kMagic];
        return file && std::fread(magic, sizeof magic, 1, file) == 1 && std::memcmp(magic, kMagic, sizeof kMagic) == 0;
    }
    void Close()
    {
        if (file)
            std::fclose(file);
        file = nullptr;
    }
    struct S_record {
        E_op op;
        const uint8_t *payload;
        uint32_t size;
    };
    // The next record (valid until the next call); false at the end or when the file is cut short
    // (`cutShort`).
    bool Next(S_record *record)
    {
        uint32_t head[2];
        if (!file || std::fread(head, sizeof head, 1, file) != 1)
            return false;
        payload.resize(head[1] ? head[1] : 1);
        if (head[1] && std::fread(payload.data(), head[1], 1, file) != 1) {
            cutShort = true;
            return false;
        }
        *record = {static_cast<E_op>(head[0]), payload.data(), head[1]};
        return true;
    }
    bool cutShort = false;

private:
    FILE *file = nullptr;
    std::vector<uint8_t> payload;
};

// Reads a payload's fields in order.
class Reader {
public:
    Reader(const uint8_t *p, uint32_t n) : at(p), end(p + n) {}
    uint32_t U()
    {
        uint32_t v = 0;
        Bytes(&v, 4);
        return v;
    }
    float F()
    {
        float v = 0;
        Bytes(&v, 4);
        return v;
    }
    void Bytes(void *to, size_t n)
    {
        const size_t have = static_cast<size_t>(end - at);
        std::memcpy(to, at, n < have ? n : have);
        at += n < have ? n : have;
    }
    const uint8_t *Here() const { return at; }
    size_t Left() const { return static_cast<size_t>(end - at); }
    void Skip(size_t n) { at += n < Left() ? n : Left(); }

private:
    const uint8_t *at, *end;
};

// The capture's frame drawn on `device` (made for it, on `window`); the picture it drew. `log`
// counts what failed.
class Replayer {
public:
    Replayer(Device *d, HWND w) : device(d), window(w) {}
    ~Replayer() { Release(); }

    std::string error;                  // the first failure that stopped the replay
    int failedCalls = 0;                // device calls that failed (the replay goes on)
    S_image reference;                  // the picture the game drew, from the capture (none: empty)
    S_image picture;                    // what the device drew

    bool Run(Capture &capture)
    {
        Capture::S_record r;
        while (capture.Next(&r)) {
            if (!Do(r))
                return false;
        }
        return capture.cutShort ? Fail("the capture is cut short") : true;
    }

    // The depth format the device was made with when the capture's could not be (0: the capture's).
    D3DFORMAT depthFallback = D3DFMT_UNKNOWN;

private:
    bool Fail(const std::string &what)
    {
        if (error.empty())
            error = what;
        return false;
    }
    void Check(HRESULT hr)
    {
        if (FAILED(hr))
            ++failedCalls;
    }

    bool Do(const Capture::S_record &r)
    {
        Reader in(r.payload, r.size);
        switch (r.op) {
        case OP_HEADER: return Create(in);
        case OP_RENDER_STATE: {
            const uint32_t state = in.U();
            Check(device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), in.U()));
            return true;
        }
        case OP_STAGE_STATE: {
            const uint32_t stage = in.U(), type = in.U();
            Check(device->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), in.U()));
            return true;
        }
        case OP_TEXTURE: {
            const uint32_t stage = in.U(), id = in.U();
            Check(device->SetTexture(stage, id ? TextureOf(id) : nullptr));
            return true;
        }
        case OP_TRANSFORM: {
            const uint32_t state = in.U();
            D3DMATRIX m;
            in.Bytes(&m, sizeof m);
            Check(device->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(state), &m));
            return true;
        }
        case OP_VIEWPORT: {
            D3DVIEWPORT8 v;
            in.Bytes(&v, sizeof v);
            Check(device->SetViewport(&v));
            return true;
        }
        case OP_MATERIAL: {
            D3DMATERIAL8 m;
            in.Bytes(&m, sizeof m);
            Check(device->SetMaterial(&m));
            return true;
        }
        case OP_LIGHT: {
            const uint32_t index = in.U();
            D3DLIGHT8 l;
            in.Bytes(&l, sizeof l);
            Check(device->SetLight(index, &l));
            return true;
        }
        case OP_LIGHT_ENABLE: {
            const uint32_t index = in.U();
            Check(device->LightEnable(index, in.U() != 0));
            return true;
        }
        case OP_DEF_SHADER: {
            const uint32_t handle = in.U(), usage = in.U(), declarations = in.U(), functions = in.U();
            std::vector<DWORD> declaration(declarations), function(functions);
            in.Bytes(declaration.data(), declarations * 4);
            in.Bytes(function.data(), functions * 4);
            DWORD made = 0;
            Check(device->CreateVertexShader(declarations ? declaration.data() : nullptr,
                                             functions ? function.data() : nullptr, &made, usage));
            shaders[handle] = made;
            return true;
        }
        case OP_VERTEX_SHADER: {
            const uint32_t handle = in.U();
            auto s = shaders.find(handle);
            Check(device->SetVertexShader(s != shaders.end() ? s->second : handle));
            return true;
        }
        case OP_SHADER_CONSTANT: {
            const uint32_t reg = in.U(), count = in.U();
            Check(device->SetVertexShaderConstant(reg, in.Here(), count));
            return true;
        }
        case OP_STREAM: {
            const uint32_t stream = in.U(), id = in.U(), stride = in.U();
            auto b = vertexBuffers.find(id);
            Check(device->SetStreamSource(stream, b != vertexBuffers.end() ? b->second : nullptr, stride));
            return true;
        }
        case OP_INDICES: {
            const uint32_t id = in.U(), base = in.U();
            auto b = indexBuffers.find(id);
            Check(device->SetIndices(b != indexBuffers.end() ? b->second : nullptr, base));
            return true;
        }
        case OP_DEF_TEXTURE: return DefineTexture(in);
        case OP_TEXTURE_DATA: return TextureData(in);
        case OP_DEF_VERTEX_BUFFER: {
            const uint32_t id = in.U(), length = in.U(), usage = in.U(), fvf = in.U(), pool = in.U();
            VertexBuffer *b = nullptr;
            if (FAILED(device->CreateVertexBuffer(length, usage, fvf, static_cast<D3DPOOL>(pool), &b)) || !b)
                return Fail("CreateVertexBuffer");
            vertexBuffers[id] = b;
            WriteBuffer(b, 0, in.Here(), in.Left());
            return true;
        }
        case OP_DEF_INDEX_BUFFER: {
            const uint32_t id = in.U(), length = in.U(), usage = in.U(), format = in.U(), pool = in.U();
            IndexBuffer *b = nullptr;
            if (FAILED(device->CreateIndexBuffer(length, usage, static_cast<D3DFORMAT>(format),
                                                 static_cast<D3DPOOL>(pool), &b)) || !b)
                return Fail("CreateIndexBuffer");
            indexBuffers[id] = b;
            WriteBuffer(b, 0, in.Here(), in.Left());
            return true;
        }
        case OP_VERTEX_DATA: {
            const uint32_t id = in.U(), offset = in.U();
            auto b = vertexBuffers.find(id);
            if (b != vertexBuffers.end())
                WriteBuffer(b->second, offset, in.Here(), in.Left());
            return true;
        }
        case OP_INDEX_DATA: {
            const uint32_t id = in.U(), offset = in.U();
            auto b = indexBuffers.find(id);
            if (b != indexBuffers.end())
                WriteBuffer(b->second, offset, in.Here(), in.Left());
            return true;
        }
        case OP_DROP: Drop(in.U()); return true;
        case OP_CLEAR: {
            const uint32_t count = in.U();
            std::vector<D3DRECT> rects(count);
            in.Bytes(rects.data(), count * sizeof(D3DRECT));
            const uint32_t flags = in.U(), color = in.U();
            const float z = in.F();
            const uint32_t stencil = in.U();
            Check(device->Clear(count, count ? rects.data() : nullptr, flags, color, z, stencil));
            return true;
        }
        case OP_BEGIN_SCENE: Check(device->BeginScene()); return true;
        case OP_END_SCENE: Check(device->EndScene()); return true;
        case OP_DRAW_INDEXED: {
            const uint32_t type = in.U(), minIndex = in.U(), vertices = in.U(), start = in.U(), count = in.U();
            Check(device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(type), minIndex, vertices, start, count));
            return true;
        }
        case OP_DRAW_UP: {
            const uint32_t type = in.U(), count = in.U(), stride = in.U();
            Check(device->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), count, in.Here(), stride));
            return true;
        }
        case OP_DRAW_INDEXED_UP: {
            const uint32_t type = in.U(), minIndex = in.U(), vertices = in.U(), count = in.U(), format = in.U();
            const uint32_t stride = in.U(), indexBytes = in.U();
            const uint8_t *indices = in.Here();
            in.Skip(indexBytes);
            Check(device->DrawIndexedPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), minIndex, vertices, count,
                                                 indices, static_cast<D3DFORMAT>(format), in.Here(), stride));
            return true;
        }
        case OP_GAMMA: return true;     // (the picture is compared before the gamma ramp)
        case OP_FRAME_IMAGE: {
            const uint32_t width = in.U(), height = in.U(), format = in.U();
            uint32_t rowBytes, rows;
            if (FormatRows(static_cast<D3DFORMAT>(format), width, height, &rowBytes, &rows))
                ToBgr(static_cast<D3DFORMAT>(format), width, height, in.Here(), rowBytes, &reference);
            return true;
        }
        case OP_PRESENT: return ReadBack();
        default: return true;
        }
    }

    bool Create(Reader &in)
    {
        const uint32_t width = in.U(), height = in.U(), format = in.U(), depth = in.U(), samples = in.U();
        in.U();                         // (windowed: the replay is)
        const uint32_t flags = in.U();
        if (!device->Open())
            return Fail("the device's API does not open");
        D3DPRESENT_PARAMETERS p = {};
        p.BackBufferWidth = width;
        p.BackBufferHeight = height;
        p.BackBufferFormat = static_cast<D3DFORMAT>(format);
        p.BackBufferCount = 1;
        p.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(samples);
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        p.hDeviceWindow = window;
        p.Windowed = TRUE;
        p.EnableAutoDepthStencil = depth != 0;
        p.AutoDepthStencilFormat = static_cast<D3DFORMAT>(depth);
        p.Flags = flags;
        // (Windowed, the back buffer is in the desktop's format.)
        D3DDISPLAYMODE mode = {};
        device->GetAdapterDisplayMode(0, &mode);
        HRESULT hr = device->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                          D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p);
        if (FAILED(hr) && mode.Format != p.BackBufferFormat) {
            p.BackBufferFormat = mode.Format;
            hr = device->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                      D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p);
        }
        if (FAILED(hr))
            hr = device->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                      D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p);
        // A depth format this driver does not have (the game's may have been a wrapper's, such as
        // dgVoodoo's D32): the nearest it has.
        for (D3DFORMAT depthFormat : {D3DFMT_D24X8, D3DFMT_D24S8, D3DFMT_D16}) {
            if (SUCCEEDED(hr) || !p.EnableAutoDepthStencil)
                break;
            p.AutoDepthStencilFormat = depthFormat;
            hr = device->CreateDevice(0, D3DDEVTYPE_HAL, window,
                                      D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p);
            if (SUCCEEDED(hr))
                depthFallback = depthFormat;
        }
        if (FAILED(hr)) {
            char text[64];
            std::snprintf(text, sizeof text, "CreateDevice: %08lx", static_cast<unsigned long>(hr));
            return Fail(text);
        }
        created = true;
        return true;
    }

    Texture *TextureOf(uint32_t id)
    {
        auto t = textures.find(id);
        return t != textures.end() ? t->second.texture : nullptr;
    }

    bool DefineTexture(Reader &in)
    {
        const uint32_t id = in.U(), width = in.U(), height = in.U(), levels = in.U(), usage = in.U();
        const uint32_t format = in.U(), pool = in.U();
        S_texture t;
        t.pool = static_cast<D3DPOOL>(pool);
        if (FAILED(device->CreateTexture(width, height, levels, usage, static_cast<D3DFORMAT>(format), t.pool,
                                         &t.texture)) || !t.texture)
            return Fail("CreateTexture");
        // A default-pool texture is filled from a lockable copy.
        if (t.pool == D3DPOOL_DEFAULT &&
            (FAILED(device->CreateTexture(width, height, levels, 0, static_cast<D3DFORMAT>(format),
                                          D3DPOOL_SYSTEMMEM, &t.staging)) || !t.staging))
            return Fail("CreateTexture (staging)");
        textures[id] = t;
        return true;
    }
    bool TextureData(Reader &in)
    {
        const uint32_t id = in.U(), level = in.U();
        RECT rect;
        rect.left = static_cast<LONG>(in.U());
        rect.top = static_cast<LONG>(in.U());
        rect.right = static_cast<LONG>(in.U());
        rect.bottom = static_cast<LONG>(in.U());
        auto t = textures.find(id);
        if (t == textures.end())
            return true;
        Texture *target = t->second.staging ? t->second.staging : t->second.texture;
        D3DSURFACE_DESC desc = {};
        uint32_t rowBytes, rows;
        if (FAILED(device->GetLevelDesc(target, level, &desc)) ||
            !FormatRows(desc.Format, static_cast<uint32_t>(rect.right - rect.left),
                        static_cast<uint32_t>(rect.bottom - rect.top), &rowBytes, &rows))
            return true;
        Surface *surface = nullptr;
        if (FAILED(device->GetSurfaceLevel(target, level, &surface)) || !surface) {
            ++failedCalls;
            return true;
        }
        D3DLOCKED_RECT locked = {};
        const bool whole = rect.left == 0 && rect.top == 0 && static_cast<UINT>(rect.right) == desc.Width &&
                           static_cast<UINT>(rect.bottom) == desc.Height;
        if (SUCCEEDED(device->LockRect(surface, &locked, whole ? nullptr : &rect, 0))) {
            for (uint32_t r = 0; r < rows && in.Left() >= rowBytes; ++r) {
                std::memcpy(static_cast<uint8_t *>(locked.pBits) + static_cast<size_t>(r) * locked.Pitch, in.Here(),
                            rowBytes);
                in.Skip(rowBytes);
            }
            device->UnlockRect(surface);
        } else {
            ++failedCalls;
        }
        device->Release(surface);
        if (t->second.staging)
            Check(device->UpdateTexture(t->second.staging, t->second.texture));
        return true;
    }

    template <class H>
    void WriteBuffer(H *buffer, uint32_t offset, const uint8_t *bytes, size_t n)
    {
        if (!n)
            return;
        BYTE *p = nullptr;
        if (SUCCEEDED(device->Lock(buffer, offset, static_cast<UINT>(n), &p, 0)) && p) {
            std::memcpy(p, bytes, n);
            device->Unlock(buffer);
        } else {
            ++failedCalls;
        }
    }

    void Drop(uint32_t id)
    {
        if (auto t = textures.find(id); t != textures.end()) {
            device->Release(t->second.texture);
            if (t->second.staging)
                device->Release(t->second.staging);
            textures.erase(t);
        } else if (auto v = vertexBuffers.find(id); v != vertexBuffers.end()) {
            device->Release(v->second);
            vertexBuffers.erase(v);
        } else if (auto i = indexBuffers.find(id); i != indexBuffers.end()) {
            device->Release(i->second);
            indexBuffers.erase(i);
        }
    }

    // The back buffer, copied out as IGraph's screenshots are.
    bool ReadBack()
    {
        Surface *target = nullptr, *image = nullptr;
        if (FAILED(device->GetRenderTarget(&target)) || !target)
            return Fail("GetRenderTarget");
        D3DSURFACE_DESC desc = {};
        uint32_t rowBytes, rows;
        bool ok = false;
        if (SUCCEEDED(device->GetDesc(target, &desc)) && FormatRows(desc.Format, desc.Width, desc.Height, &rowBytes, &rows) &&
            SUCCEEDED(device->CreateImageSurface(desc.Width, desc.Height, desc.Format, &image)) && image) {
            D3DLOCKED_RECT locked = {};
            if (SUCCEEDED(device->CopyRects(target, nullptr, 0, image, nullptr)) &&
                SUCCEEDED(device->LockRect(image, &locked, nullptr, D3DLOCK_READONLY))) {
                std::vector<uint8_t> packed(static_cast<size_t>(rowBytes) * rows);
                for (uint32_t r = 0; r < rows; ++r)
                    std::memcpy(packed.data() + static_cast<size_t>(r) * rowBytes,
                                static_cast<const uint8_t *>(locked.pBits) + static_cast<size_t>(r) * locked.Pitch, rowBytes);
                ok = ToBgr(desc.Format, desc.Width, desc.Height, packed.data(), rowBytes, &picture);
                device->UnlockRect(image);
            }
            device->Release(image);
        }
        device->Release(target);
        return ok ? true : Fail("reading the back buffer back");
    }

    void Release()
    {
        for (auto &t : textures) {
            device->Release(t.second.texture);
            if (t.second.staging)
                device->Release(t.second.staging);
        }
        for (auto &b : vertexBuffers)
            device->Release(b.second);
        for (auto &b : indexBuffers)
            device->Release(b.second);
        textures.clear();
        vertexBuffers.clear();
        indexBuffers.clear();
        if (created) {
            device->ReleaseDevice();
            device->Close();
            created = false;
        }
    }

    struct S_texture {
        Texture *texture = nullptr, *staging = nullptr;
        D3DPOOL pool = D3DPOOL_MANAGED;
    };

    Device *device;
    HWND window;
    bool created = false;
    std::map<uint32_t, S_texture> textures;
    std::map<uint32_t, VertexBuffer *> vertexBuffers;
    std::map<uint32_t, IndexBuffer *> indexBuffers;
    std::map<uint32_t, DWORD> shaders;
};

}  // namespace replay
