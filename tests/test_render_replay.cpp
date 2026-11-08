// Replays captured frames (LS3DF.ini [Render] CaptureFrames, src/gfx/trace) on a render backend and
// compares each picture with the one the game drew when it was captured.
// - On the Direct3D 8 backend (the default) the same driver must draw the same frame, which shows
//   the capture holds everything the frame needs.
// - On the SDL_GPU backend (--backend sdlgpu, on --driver direct3d12 or vulkan, SDL's choice
//   otherwise; --debug for the API's validation) the picture is Direct3D 8's model drawn by shaders:
//   how far it is from the game's is the measure.
//
// Usage: test_render_replay.exe <capture.ls3dframe> ... [--tolerance N] [--d3d8 <d3d8.dll>]
//                               [--backend d3d8 | sdlgpu] [--driver direct3d12 | vulkan] [--debug]
//                               [--min-psnr DB] [--repeat N] [--no-vsync]
//   --repeat N draws and presents each frame N more times after it and prints the time a frame took
//   (the last one waited for): the backend's speed on the game's own frames.
//        test_render_replay.exe --selftest   (draws a scene through the recorder, captures it and
//                                              replays the capture: the two pictures must be equal)
//          [--live sdlgpu]                     (the scene drawn on SDL_GPU, live: Present, and the
//                                              picture read back as IGraph's screenshots are)
//   The game's frame was drawn by the d3d8.dll of its folder (a wrapper such as dgVoodoo's, or
//   Windows'); the replay must use the same one to draw the same picture: --d3d8 names it, else a
//   d3d8.dll in the folder above the capture's (the game's, for LS3DF_frames\) is used, else
//   Windows'.
//   Writes <capture>.d3d8.png, <capture>.game.png and <capture>.diff.png (differences x4) next to
//   each capture. Fails when a pixel's channel is more than N (default 0) apart.
//
// test-sources: src/gfx/d3d8/gfx_d3d8.cpp src/gfx/trace/*.cpp src/gfx/sdlgpu/*.cpp
// test-sources: src/core/enhance.cpp src/core/debug.cpp
// test-sources: src/platform/*.cpp src/platform/win32/*.cpp src/platform/sdl3/*.cpp
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "render_replay.h"
#include "../src/core/enhance.h"
#include "../src/gfx/sdlgpu/sdlgpu_device.h"
#include "../src/gfx/trace/recorder.h"

namespace {

using namespace ls3d;
using namespace ls3d::gfx;

struct S_litVertex {
    float x, y, z, nx, ny, nz, u, v;
};
struct S_screenVertex {
    float x, y, z, rhw;
    DWORD diffuse;
};
struct S_texturedScreen {
    S_screenVertex v;
    float u, t;
};

DWORD FloatBits(float f)
{
    DWORD d;
    std::memcpy(&d, &f, 4);
    return d;
}

// A scene drawn through `d` on `window` for two frames, the second captured: what the recorder must
// carry over from before the capture (states, a write-only vertex buffer, a default-pool texture
// filled by UpdateTexture, lights, fog) and what changes during it (a buffer and a texture written
// mid-frame, user-memory draws). The capture's path (empty: none).
std::string SelfTestCapture(Device *d, HWND window)
{
    if (!d->Open())
        return "";
    D3DDISPLAYMODE mode = {};
    d->GetAdapterDisplayMode(0, &mode);
    D3DPRESENT_PARAMETERS p = {};
    p.BackBufferWidth = 256;
    p.BackBufferHeight = 192;
    p.BackBufferFormat = mode.Format;
    p.BackBufferCount = 1;
    p.SwapEffect = D3DSWAPEFFECT_DISCARD;
    p.hDeviceWindow = window;
    p.Windowed = TRUE;
    p.EnableAutoDepthStencil = TRUE;
    p.AutoDepthStencilFormat = D3DFMT_D16;
    if (FAILED(d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                               &p)) &&
        FAILED(d->CreateDevice(0, D3DDEVTYPE_HAL, window,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p)))
        return "";

    // A managed checker texture with its mip levels.
    Texture *checker = nullptr;
    d->CreateTexture(64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &checker);
    for (DWORD level = 0; level < d->GetLevelCount(checker); ++level) {
        Surface *s = nullptr;
        d->GetSurfaceLevel(checker, level, &s);
        D3DSURFACE_DESC desc;
        d->GetDesc(s, &desc);
        D3DLOCKED_RECT r;
        d->LockRect(s, &r, nullptr, 0);
        for (UINT y = 0; y < desc.Height; ++y)
            for (UINT x = 0; x < desc.Width; ++x)
                reinterpret_cast<DWORD *>(static_cast<BYTE *>(r.pBits) + y * r.Pitch)[x] =
                    ((x / 8 + y / 8) & 1) ? 0xffe0c040u : 0x80304080u + level * 0x101010u;
        d->UnlockRect(s);
        d->Release(s);
    }
    // A default-pool texture filled from system memory.
    Texture *stage = nullptr, *streamed = nullptr;
    d->CreateTexture(32, 32, 1, 0, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &stage);
    d->CreateTexture(32, 32, 1, 0, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, &streamed);
    {
        Surface *s = nullptr;
        d->GetSurfaceLevel(stage, 0, &s);
        D3DLOCKED_RECT r;
        d->LockRect(s, &r, nullptr, 0);
        for (UINT y = 0; y < 32; ++y)
            for (UINT x = 0; x < 32; ++x)
                reinterpret_cast<WORD *>(static_cast<BYTE *>(r.pBits) + y * r.Pitch)[x] =
                    static_cast<WORD>((x << 11) | (y << 6) | ((x ^ y) & 31));
        d->UnlockRect(s);
        d->Release(s);
        d->UpdateTexture(stage, streamed);
    }
    // A write-only vertex buffer and an index buffer: a lit, textured grid.
    const int n = 8;
    VertexBuffer *vb = nullptr;
    IndexBuffer *ib = nullptr;
    d->CreateVertexBuffer((n + 1) * (n + 1) * sizeof(S_litVertex), D3DUSAGE_WRITEONLY, 0x112, D3DPOOL_DEFAULT, &vb);
    d->CreateIndexBuffer(n * n * 6 * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib);
    auto fillGrid = [&](float bend) {
        BYTE *data = nullptr;
        d->Lock(vb, 0, 0, &data, 0);
        auto *v = reinterpret_cast<S_litVertex *>(data);
        for (int y = 0; y <= n; ++y)
            for (int x = 0; x <= n; ++x) {
                const float fx = x / float(n) - 0.5f, fy = y / float(n) - 0.5f;
                *v++ = {fx * 3, fy * 3, bend * (fx * fx + fy * fy), -fx * bend, -fy * bend, -1, x / 2.0f, y / 2.0f};
            }
        d->Unlock(vb);
    };
    fillGrid(1.0f);
    BYTE *data = nullptr;
    d->Lock(ib, 0, 0, &data, 0);
    auto *index = reinterpret_cast<WORD *>(data);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const WORD a = static_cast<WORD>(y * (n + 1) + x), b = static_cast<WORD>(a + 1),
                       c = static_cast<WORD>(a + n + 1), e = static_cast<WORD>(c + 1);
            *index++ = a;
            *index++ = c;
            *index++ = b;
            *index++ = b;
            *index++ = c;
            *index++ = e;
        }
    d->Unlock(ib);

    // States set before the captured frame.
    D3DMATRIX world = {};
    world.m[0][0] = world.m[1][1] = world.m[2][2] = world.m[3][3] = 1;
    world.m[3][2] = 4;
    D3DMATRIX view = {};
    view.m[0][0] = view.m[1][1] = view.m[2][2] = view.m[3][3] = 1;
    D3DMATRIX projection = {};
    projection.m[0][0] = 1.5f;
    projection.m[1][1] = 2.0f;
    projection.m[2][2] = 1.01f;
    projection.m[2][3] = 1;
    projection.m[3][2] = -0.101f;
    d->SetTransform(D3DTS_WORLD, &world);
    d->SetTransform(D3DTS_VIEW, &view);
    d->SetTransform(D3DTS_PROJECTION, &projection);
    D3DMATERIAL8 material = {};
    material.Diffuse = {1, 1, 1, 1};
    material.Ambient = {0.3f, 0.3f, 0.3f, 1};
    d->SetMaterial(&material);
    D3DLIGHT8 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = {1.0f, 0.9f, 0.7f, 1};
    light.Direction = {0.4f, -0.5f, 1};
    d->SetLight(0, &light);
    d->LightEnable(0, TRUE);
    d->SetRenderState(D3DRS_LIGHTING, TRUE);
    d->SetRenderState(D3DRS_AMBIENT, 0xff202020);
    d->SetRenderState(D3DRS_FOGENABLE, TRUE);
    d->SetRenderState(D3DRS_FOGCOLOR, 0xff406080);
    d->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
    d->SetRenderState(D3DRS_FOGSTART, FloatBits(3.5f));
    d->SetRenderState(D3DRS_FOGEND, FloatBits(6.0f));
    d->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    d->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    d->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTexture(0, checker);
    d->SetStreamSource(0, vb, sizeof(S_litVertex));
    d->SetIndices(ib, 0);
    d->SetVertexShader(0x112);

    for (int frame = 0; frame < 2; ++frame) {
        d->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff101828, 1.0f, 0);
        d->BeginScene();
        // The draws from user memory below leave stream 0 and the indices unset: a draw now draws
        // nothing (Direct3D refuses it), and the capture must say so.
        if (frame == 1) {
            D3DMATRIX left = world;
            left.m[3][0] = -1.5f;
            d->SetTransform(D3DTS_WORLD, &left);
            d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, (n + 1) * (n + 1), 0, n * n * 2);
            d->SetTransform(D3DTS_WORLD, &world);
        }
        d->SetStreamSource(0, vb, sizeof(S_litVertex));
        d->SetIndices(ib, 0);
        d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, (n + 1) * (n + 1), 0, n * n * 2);
        if (frame == 1) {
            // Mid-frame: the grid re-bent and texel rows rewritten, then drawn again, moved.
            fillGrid(-1.5f);
            Surface *s = nullptr;
            d->GetSurfaceLevel(checker, 0, &s);
            RECT rows = {0, 20, 64, 28};
            D3DLOCKED_RECT r;
            d->LockRect(s, &r, &rows, 0);
            for (UINT y = 0; y < 8; ++y)
                for (UINT x = 0; x < 64; ++x)
                    reinterpret_cast<DWORD *>(static_cast<BYTE *>(r.pBits) + y * r.Pitch)[x] = 0xff20ff20u;
            d->UnlockRect(s);
            d->Release(s);
            D3DMATRIX moved = world;
            moved.m[3][0] = 1.2f;
            moved.m[3][2] = 5.0f;
            d->SetTransform(D3DTS_WORLD, &moved);
            d->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);   // (level 0, rewritten, shows)
            d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, (n + 1) * (n + 1), 0, n * n * 2);
            d->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
            d->SetTransform(D3DTS_WORLD, &world);
        }
        // Pre-transformed, blended quads from user memory: the streamed texture, then plain colour.
        d->SetRenderState(D3DRS_LIGHTING, FALSE);
        d->SetRenderState(D3DRS_FOGENABLE, FALSE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetTexture(0, streamed);
        d->SetVertexShader(0x144);
        const S_screenVertex quad[4] = {{10, 10, 0.1f, 1, 0xc0ffffff}, {90, 10, 0.1f, 1, 0xc0ffffff},
                                        {10, 70, 0.1f, 1, 0xc0ffffff}, {90, 70, 0.1f, 1, 0xc0ffffff}};
        const S_texturedScreen textured[4] = {{quad[0], 0, 0}, {quad[1], 1, 0}, {quad[2], 0, 1}, {quad[3], 1, 1}};
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, textured, sizeof textured[0]);
        d->SetTexture(0, nullptr);
        d->SetVertexShader(0x44);
        const S_screenVertex bar[3] = {{150, 20, 0.2f, 1, 0x80ff4040}, {240, 60, 0.2f, 1, 0x8040ff40},
                                       {170, 120, 0.2f, 1, 0x804040ff}};
        const WORD order[3] = {0, 1, 2};
        d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 3, 1, order, D3DFMT_INDEX16, bar, sizeof bar[0]);
        // Back to the grid's states.
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_LIGHTING, TRUE);
        d->SetRenderState(D3DRS_FOGENABLE, TRUE);
        d->SetTexture(0, checker);
        d->SetVertexShader(0x112);
        d->EndScene();
        if (frame == 0)
            RequestFrameCapture();
        d->Present();
    }
    std::string path = LastFrameCapture();
    d->SetTexture(0, nullptr);
    d->SetStreamSource(0, nullptr, 0);
    d->SetIndices(nullptr, 0);
    d->Release(vb);
    d->Release(ib);
    d->Release(checker);
    d->Release(streamed);
    d->Release(stage);
    d->ReleaseDevice();
    d->Close();
    return path;
}

}  // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    uint32_t tolerance = 0;
    std::vector<std::string> captures;
    bool selfTest = false;
    std::string d3d8Path, backend = "d3d8", driver, live = "d3d8";
    bool debug = false;
    double minPsnr = 0;
    int repeat = 0;
    bool noVsync = false;             // (the SDL_GPU backend: the least it must reach; 0: report only)
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--selftest"))
            selfTest = true;
        else if (!std::strcmp(argv[i], "--tolerance") && i + 1 < argc)
            tolerance = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--d3d8") && i + 1 < argc)
            d3d8Path = argv[++i];
        else if (!std::strcmp(argv[i], "--backend") && i + 1 < argc)
            backend = argv[++i];
        else if (!std::strcmp(argv[i], "--driver") && i + 1 < argc)
            driver = argv[++i];
        else if (!std::strcmp(argv[i], "--debug"))
            debug = true;
        else if (!std::strcmp(argv[i], "--live") && i + 1 < argc)
            live = argv[++i];
        else if (!std::strcmp(argv[i], "--min-psnr") && i + 1 < argc)
            minPsnr = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--repeat") && i + 1 < argc)
            repeat = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--no-vsync"))
            noVsync = true;
        else
            captures.push_back(argv[i]);
    }
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "LS3DFReplay";
    RegisterClassA(&wc);
    if (selfTest) {
        HWND window = CreateWindowA("LS3DFReplay", "self-test", WS_POPUP, 0, 0, 256, 192, nullptr, nullptr,
                                    GetModuleHandleA(nullptr), nullptr);
        Device *direct = g_pGfx;
        if (live == "sdlgpu") {
            S_enhancements e = Enhancements();
            std::snprintf(e.sdlGpuDriver, sizeof e.sdlGpuDriver, "%s", driver.c_str());
            SetEnhancements(e);
            g_pGfx = FrameRecorder(SdlGpuDevice());
            std::printf("self-test: drawn live on SDL_GPU\n");
        } else {
            g_pGfx = FrameRecorder(direct);
        }
        const std::string path = SelfTestCapture(g_pGfx, window);
        g_pGfx = direct;
        DestroyWindow(window);
        if (path.empty()) {
            std::printf("self-test: no capture written\n");
            return 1;
        }
        std::printf("self-test: captured %s\n", path.c_str());
        captures.push_back(path);
    }
    if (captures.empty()) {
        std::printf("usage: test_render_replay <capture.ls3dframe> ... [--tolerance N] | --selftest\n");
        return 2;
    }

    // (Absolute paths: the current directory changes below.)
    for (std::string &path : captures) {
        char full[MAX_PATH];
        if (GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr))
            path = full;
    }
    // The game's d3d8.dll (the backend loads "d3d8.dll", which is then this one).
    if (d3d8Path.empty() && !selfTest) {
        std::string dir = captures[0];
        for (int up = 0; up < 2; ++up) {
            const size_t slash = dir.find_last_of("\\/");
            dir = slash == std::string::npos ? "." : dir.substr(0, slash);
        }
        const std::string candidate = dir + "\\d3d8.dll";
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            d3d8Path = candidate;
    }
    if (!d3d8Path.empty()) {
        // (Its own settings, a wrapper's, are found next to it.)
        const size_t slash = d3d8Path.find_last_of("\\/");
        if (slash != std::string::npos)
            SetCurrentDirectoryA(d3d8Path.substr(0, slash).c_str());
        if (!LoadLibraryA(d3d8Path.c_str())) {
            std::printf("cannot load %s\n", d3d8Path.c_str());
            return 2;
        }
        std::printf("drawing with %s\n", d3d8Path.c_str());
    } else {
        std::printf("drawing with Windows' d3d8.dll\n");
    }

    // The backend.
    Device *device = g_pGfx;
    const bool sdlGpu = backend == "sdlgpu";
    if (sdlGpu) {
        S_enhancements e = Enhancements();
        std::snprintf(e.sdlGpuDriver, sizeof e.sdlGpuDriver, "%s", driver.c_str());
        e.sdlGpuDebug = debug;
        SetEnhancements(e);
        device = SdlGpuDevice();
        std::printf("replaying on SDL_GPU%s%s\n", driver.empty() ? "" : " / ", driver.c_str());
    }

    int failures = 0;
    for (const std::string &path : captures) {
        replay::Capture capture;
        if (!capture.Open(path.c_str())) {
            std::printf("%s: not a capture\n", path.c_str());
            ++failures;
            continue;
        }
        // A hidden window: Direct3D draws into the back buffer anyway.
        HWND window = CreateWindowA("LS3DFReplay", "replay", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                    GetModuleHandleA(nullptr), nullptr);
        replay::Replayer r(device, window);
        r.noVsync = noVsync;
        const bool ran = r.Run(capture);
        if (!ran) {
            std::printf("%s: %s\n", path.c_str(), r.error.c_str());
            ++failures;
        } else if (r.reference.bgr.empty()) {
            std::printf("%s: no picture of the game's in the capture\n", path.c_str());
            ++failures;
        } else if (r.reference.width != r.picture.width || r.reference.height != r.picture.height) {
            std::printf("%s: %ux%u drawn, the game's is %ux%u\n", path.c_str(), r.picture.width, r.picture.height,
                        r.reference.width, r.reference.height);
            ++failures;
        } else {
            replay::S_image diff;
            const replay::S_difference d = replay::Compare(r.reference, r.picture, tolerance, &diff);
            const bool ok = sdlGpu ? d.psnr >= minPsnr : d.pixelsOver == 0;
            std::printf("%s: %ux%u, %llu pixels over %u apart (largest %u), %.1f dB, %d device calls failed%s%s\n",
                        path.c_str(), r.picture.width, r.picture.height,
                        static_cast<unsigned long long>(d.pixelsOver), tolerance, d.maxChannel, d.psnr,
                        r.failedCalls, r.depthFallback ? " (another depth format: the driver lacks the game's)" : "",
                        ok ? "" : "  FAIL");
            if (!ok)
                ++failures;
            replay::WritePng((path + (sdlGpu ? ".sdlgpu.png" : ".d3d8.png")).c_str(), r.picture);
            replay::WritePng((path + ".game.png").c_str(), r.reference);
            replay::WritePng((path + (sdlGpu ? ".sdlgpu.diff.png" : ".diff.png")).c_str(), diff);
        }
        if (ran && repeat > 0) {
            const double ms = r.Repeat(repeat);
            std::printf("    %d more frames: %.2f ms a frame\n", repeat, ms);
        }
        DestroyWindow(window);
    }
    std::printf("test_render_replay: %zu captures, %d failed\n", captures.size(), failures);
    return failures ? 1 : 0;
}
