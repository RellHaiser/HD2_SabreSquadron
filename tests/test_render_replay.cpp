// Replays captured frames (LS3DF.ini [Render] CaptureFrames, src/gfx/trace) on a render backend and
// compares each picture with the one the game drew when it was captured.
// - On the Direct3D 8 backend (the default) the same driver must draw the same frame, which shows
//   the capture holds everything the frame needs.
// - On the SDL_GPU backend (--backend sdlgpu, on --driver direct3d12 or vulkan, SDL's choice
//   otherwise; --debug for the API's validation) the picture is Direct3D 8's model drawn by shaders:
//   how far it is from the game's is the measure.
// - On the OpenGL 3.3 backend (--backend opengl) likewise.
//
// Usage: test_render_replay.exe <capture.ls3dframe> ... [--tolerance N] [--d3d8 <d3d8.dll>]
//                               [--backend d3d8 | sdlgpu | opengl] [--driver direct3d12 | vulkan] [--debug]
//                               [--min-psnr DB] [--repeat N] [--no-vsync]
//   --repeat N draws and presents each frame N more times after it and prints the time a frame took
//   (the last one waited for): the backend's speed on the game's own frames.
//        test_render_replay.exe --selftest   (draws a scene through the recorder, captures it and
//                                              replays the capture: the two pictures must be equal)
//          [--live sdlgpu | opengl]            (the scene drawn on that backend, live: Present, and
//                                              the picture read back as IGraph's screenshots are)
//          [--present vsync | mailbox | immediate]  (LS3DF.ini's PresentMode, for SDL_GPU and OpenGL)
//          [--sdl-window LETTERS]              (with --show: SDL's window, as with [Platform] SDL3=1: see below)
//          [--show] [--work MS] [--limit MS]   (with --repeat: the window over the display, how far apart
//                                              the frames were; each frame first busy for MS, and at
//                                              least --limit MS apart, as the game's)
//          [--fullscreen]                      (with --live: the device in full screen: the window must
//                                              cover the display, and a gamma ramp must change what
//                                              it shows; it reads the display, so nothing may cover it)
//   --msaa N, --alpha-coverage: LS3DF.ini's MSAA and AlphaToCoverage, for SDL_GPU and OpenGL; the pictures
//   written are then named <capture>.msaa<N>[.a2c].<backend>.png. (A self-test's capture has no samples
//   in it and is replayed on Direct3D 8 without any, so its edges differ: a low figure there is expected.)
//   --ao, --ao-strength PERCENT, --ao-radius CM: LS3DF.ini's AmbientOcclusion, for SDL_GPU and OpenGL; the
//   pictures written are then named <capture>...ao.<backend>.png.
//   --gamma: with --repeat, those frames are shown through a ramp like the game's own.
//   --anisotropy N: anisotropic filtering as IGraph sets it for LS3DF.ini's AnisotropicFiltering. With
//   --selftest the scene is drawn so, with point and no mip filters; a capture's linear minification
//   becomes anisotropic, and its pictures are named <capture>[.msaa<N>[.a2c]].aniso.<backend>.png.
//   The game's frame was drawn by the d3d8.dll of its folder (a wrapper such as dgVoodoo's, or
//   Windows'); the replay must use the same one to draw the same picture: --d3d8 names it, else a
//   d3d8.dll in the folder above the capture's (the game's, for LS3DF_frames\) is used, else
//   Windows'.
//   Writes <capture>.d3d8.png, <capture>.game.png and <capture>.diff.png (differences x4) next to
//   each capture. Fails when a pixel's channel is more than N (default 0) apart.
//
// test-sources: src/gfx/d3d8/gfx_d3d8.cpp src/gfx/trace/*.cpp src/gfx/sdlgpu/*.cpp src/gfx/fixed/*.cpp src/gfx/opengl/*.cpp
// test-sources: src/core/enhance.cpp src/core/debug.cpp
// test-sources: src/platform/*.cpp src/platform/win32/*.cpp src/platform/sdl3/*.cpp
#include <windows.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "render_replay.h"
#include "../src/core/enhance.h"
#include "../src/gfx/fixed/fixed_device.h"
#include "../src/platform/sdl3/sdl3_api.h"
#include "../src/gfx/opengl/opengl_device.h"
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
// What the display shows of a flat frame, in the middle and in a corner, through ramps.
bool GammaOnDisplay(Device *d, HWND window)
{
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    const auto shown = [&](D3DCOLOR colour, POINT at) {
        for (int i = 0; i < 12; ++i) {
            d->Clear(0, nullptr, D3DCLEAR_TARGET, colour, 1.0f, 0);
            d->Present();
            Sleep(16);
        }
        HDC screen = GetDC(nullptr);
        const COLORREF pixel = GetPixel(screen, at.x, at.y);
        ReleaseDC(nullptr, screen);
        return pixel;
    };
    const POINT middle = {GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2};
    const POINT corner = {0, 0};
    D3DGAMMARAMP identity, inverted;
    d->GetGammaRamp(&identity);
    for (int i = 0; i < 256; ++i) {
        inverted.red[i] = static_cast<WORD>((255 - i) * 257);
        inverted.green[i] = static_cast<WORD>(i * 257 / 2);
        inverted.blue[i] = static_cast<WORD>(255 * 257);
    }
    const COLORREF plain = shown(0xff204060, middle);
    d->SetGammaRamp(D3DSGR_NO_CALIBRATION, &inverted);
    const COLORREF through = shown(0xff204060, middle);
    const COLORREF bars = GetSystemMetrics(SM_CXSCREEN) * 3 != GetSystemMetrics(SM_CYSCREEN) * 4 ? shown(0xff204060, corner) : RGB(0, 0, 0);
    d->SetGammaRamp(D3DSGR_NO_CALIBRATION, &identity);
    const COLORREF back = shown(0xff204060, middle);
    const auto isNear = [](COLORREF c, int r, int g, int b) {
        return std::abs(GetRValue(c) - r) <= 1 && std::abs(GetGValue(c) - g) <= 1 && std::abs(GetBValue(c) - b) <= 1;
    };
    const bool ok = isNear(plain, 0x20, 0x40, 0x60) && isNear(through, 0xdf, 0x20, 0xff) && isNear(bars, 0, 0, 0) && isNear(back, 0x20, 0x40, 0x60);
    std::printf("self-test: on the display: %06lx, through a ramp %06lx, its bars %06lx, back %06lx (as BGR)%s\n",
                static_cast<unsigned long>(plain), static_cast<unsigned long>(through), static_cast<unsigned long>(bars),
                static_cast<unsigned long>(back), ok ? "" : "  FAIL");
    return ok;
}

int g_anisotropy = 0;

std::string SelfTestCapture(Device *d, HWND window, bool fullscreen)
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
    p.Windowed = fullscreen ? FALSE : TRUE;
    if (fullscreen)
        ShowWindow(window, SW_SHOW);
    p.EnableAutoDepthStencil = TRUE;
    p.AutoDepthStencilFormat = D3DFMT_D16;
    if (FAILED(d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                               &p)) &&
        FAILED(d->CreateDevice(0, D3DDEVTYPE_HAL, window,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p)))
        return "";
    {
        // A device starts with an identity ramp of 16-bit values, so IGraph does not take it for 8-bit.
        D3DGAMMARAMP ramp;
        d->GetGammaRamp(&ramp);
        if (ramp.red[255] != 0xffff || ramp.green[128] != 128 * 257 || ramp.blue[1] != 257) {
            std::printf("self-test: the first gamma ramp: red 255 %u, green 128 %u, blue 1 %u  FAIL\n", ramp.red[255], ramp.green[128], ramp.blue[1]);
            if (fullscreen)
                return "";
        }
    }
    if (fullscreen) {
        // The window covers the display, the picture in it the back buffer's shape.
        RECT r = {};
        GetClientRect(window, &r);
        const int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
        std::printf("self-test: full screen: window %ldx%ld, display %dx%d%s\n", r.right, r.bottom, w, h,
                    r.right == w && r.bottom == h ? "" : "  FAIL");
        if (r.right != w || r.bottom != h)
            return "";
        for (const auto &c : {std::array<int, 8>{1600, 1200, 1600, 1200, 0, 0, 1600, 1200},
                              std::array<int, 8>{1920, 1080, 1600, 1200, 240, 0, 1440, 1080},
                              std::array<int, 8>{1000, 1000, 1600, 800, 0, 250, 1000, 500}}) {
            const fixed::S_rectangle a = fixed::PictureRectangle(c[0], c[1], static_cast<UINT>(c[2]), static_cast<UINT>(c[3]));
            if (a.x != c[4] || a.y != c[5] || a.w != c[6] || a.h != c[7]) {
                std::printf("self-test: the picture in %dx%d: %d,%d %dx%d  FAIL\n", c[0], c[1], a.x, a.y, a.w, a.h);
                return "";
            }
        }
    }

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
    if (g_anisotropy) {
        for (DWORD stage = 0; stage < 3; ++stage) {
            d->SetTextureStageState(stage, D3DTSS_MAXANISOTROPY, static_cast<DWORD>(g_anisotropy));
            d->SetTextureStageState(stage, D3DTSS_MINFILTER, D3DTEXF_ANISOTROPIC);
            d->SetTextureStageState(stage, D3DTSS_MIPFILTER, D3DTEXF_POINT);
        }
    }

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
            d->SetTextureStageState(0, D3DTSS_MIPFILTER, g_anisotropy ? D3DTEXF_POINT : D3DTEXF_LINEAR);
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
    if (fullscreen && !GammaOnDisplay(d, window))
        path.clear();
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
    const char *presentMode = "";
    bool show = false;
    double workMs = 0;
    int limitMs = 0;
    const char *sdlWindow = nullptr;
    bool noVsync = false, fullscreen = false;             // (the SDL_GPU backend: the least it must reach; 0: report only)
    int msaa = 0;
    bool alphaCoverage = false, gamma = false, occlusion = false;
    int occlusionStrength = 75, occlusionRadius = 60;
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
        else if (!std::strcmp(argv[i], "--fullscreen"))
            fullscreen = true;
        else if (!std::strcmp(argv[i], "--present") && i + 1 < argc)
            presentMode = argv[++i];
        else if (!std::strcmp(argv[i], "--show"))
            show = true;
        else if (!std::strcmp(argv[i], "--work") && i + 1 < argc)
            workMs = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--limit") && i + 1 < argc)
            limitMs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--sdl-window") && i + 1 < argc)
            sdlWindow = argv[++i];
        else if (!std::strcmp(argv[i], "--msaa") && i + 1 < argc)
            msaa = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--alpha-coverage"))
            alphaCoverage = true;
        else if (!std::strcmp(argv[i], "--gamma"))
            gamma = true;
        else if (!std::strcmp(argv[i], "--ao"))
            occlusion = true;
        else if (!std::strcmp(argv[i], "--ao-strength") && i + 1 < argc)
            occlusionStrength = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ao-radius") && i + 1 < argc)
            occlusionRadius = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--anisotropy") && i + 1 < argc)
            g_anisotropy = std::atoi(argv[++i]);
        else
            captures.push_back(argv[i]);
    }
    if (msaa || alphaCoverage || occlusion) {
        S_enhancements e = Enhancements();
        e.msaa = msaa;
        e.alphaToCoverage = alphaCoverage;
        e.occlusion = occlusion;
        e.occlusionStrength = occlusionStrength;
        e.occlusionRadius = occlusionRadius;
        SetEnhancements(e);
    }
    const std::string tag = (msaa ? ".msaa" + std::to_string(msaa) + (alphaCoverage ? ".a2c" : "") : "") + (g_anisotropy ? ".aniso" : "") + (occlusion ? ".ao" : "");
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
            std::snprintf(e.presentMode, sizeof e.presentMode, "%s", presentMode);
            e.sdlGpuDebug = debug;
            SetEnhancements(e);
            g_pGfx = FrameRecorder(SdlGpuDevice());
            std::printf("self-test: drawn live on SDL_GPU\n");
        } else if (live == "opengl") {
            g_pGfx = FrameRecorder(OpenGlDevice());
            std::printf("self-test: drawn live on OpenGL\n");
        } else {
            g_pGfx = FrameRecorder(direct);
        }
        const std::string path = SelfTestCapture(g_pGfx, window, fullscreen);
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
    const bool sdlGpu = backend == "sdlgpu", openGl = backend == "opengl";
    if (openGl) {
        S_enhancements e = Enhancements();
        e.sdlGpuDebug = debug;
        std::snprintf(e.presentMode, sizeof e.presentMode, "%s", presentMode);
        SetEnhancements(e);
        device = OpenGlDevice();
        std::printf("replaying on OpenGL\n");
    }
    if (sdlGpu) {
        S_enhancements e = Enhancements();
        std::snprintf(e.sdlGpuDriver, sizeof e.sdlGpuDriver, "%s", driver.c_str());
        e.sdlGpuDebug = debug;
        std::snprintf(e.presentMode, sizeof e.presentMode, "%s", presentMode);
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
        // SDL's own window, as the game's with [Platform] SDL3=1. The letters say what it has as the
        // game's has: t always on top, b borderless, x text input, p its events read each frame.
        if (sdlWindow) {
            using sdl3::api;
            if (!sdl3::StartGpu()) {
                std::printf("no SDL3\n");
                return 1;
            }
            const auto has = [&](char c) { return std::strchr(sdlWindow, c) != nullptr; };
            const SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | (has('b') ? SDL_WINDOW_BORDERLESS : 0) | (has('t') ? SDL_WINDOW_ALWAYS_ON_TOP : 0) |
                                          (openGl ? SDL_WINDOW_OPENGL : 0);
            SDL_Window *w = api.SDL_CreateWindow("replay", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), flags);
            api.SDL_SetWindowPosition(w, 0, 0);
            DestroyWindow(window);
            window = static_cast<HWND>(api.SDL_GetPointerProperty(api.SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
            api.SDL_ShowWindow(w);
            if (has('x'))
                api.SDL_StartTextInput(w);
            sdl3::g_window = w;
        }
        replay::Replayer r(device, window);
        r.noVsync = noVsync;
        r.anisotropy = g_anisotropy;
        if (sdlWindow) {
            r.fullScreen = true;
            if (std::strchr(sdlWindow, 'p')) {
                r.pump = [] {
                    SDL_Event e;
                    while (sdl3::api.SDL_PollEvent(&e)) {
                    }
                };
            }
        } else if (show) {
            // The window over the display, as a full-screen game's.
            SetWindowPos(window, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_SHOWWINDOW);
        }
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
            const bool ok = sdlGpu || openGl ? d.psnr >= minPsnr : d.pixelsOver == 0;
            std::printf("%s: %ux%u, %llu pixels over %u apart (largest %u), %.1f dB, %d device calls failed%s%s\n",
                        path.c_str(), r.picture.width, r.picture.height,
                        static_cast<unsigned long long>(d.pixelsOver), tolerance, d.maxChannel, d.psnr,
                        r.failedCalls, r.depthFallback ? " (another depth format: the driver lacks the game's)" : "",
                        ok ? "" : "  FAIL");
            if (!ok)
                ++failures;
            replay::WritePng((path + tag + (openGl ? ".opengl.png" : sdlGpu ? ".sdlgpu.png" : ".d3d8.png")).c_str(), r.picture);
            replay::WritePng((path + ".game.png").c_str(), r.reference);
            replay::WritePng((path + tag + (openGl ? ".opengl.diff.png" : sdlGpu ? ".sdlgpu.diff.png" : ".diff.png")).c_str(), diff);
        }
        if (ran && repeat > 0) {
            if (gamma) {
                // (C_PlayerProfile::ApplyGamma's ramp at the option's middle: a step of 255.)
                D3DGAMMARAMP ramp;
                for (int i = 0; i < 256; ++i)
                    ramp.red[i] = ramp.green[i] = ramp.blue[i] = static_cast<WORD>(i * 255);
                device->SetGammaRamp(D3DSGR_NO_CALIBRATION, &ramp);
            }
            std::vector<double> gaps;
            const double ms = r.Repeat(repeat, show ? &gaps : nullptr, workMs, limitMs);
            std::printf("    %d more frames: %.2f ms a frame\n", repeat, ms);
            if (show) {
                int counts[6] = {};
                for (const double g : gaps)
                    ++counts[g < 6 ? 0 : g < 14 ? 1 : g < 20 ? 2 : g < 30 ? 3 : g < 40 ? 4 : 5];
                std::printf("    apart under 6 / 14 / 20 / 30 / 40 ms / longer: %d %d %d %d %d %d\n    the last:", counts[0], counts[1],
                            counts[2], counts[3], counts[4], counts[5]);
                for (size_t i = gaps.size() > 24 ? gaps.size() - 24 : 0; i < gaps.size(); ++i)
                    std::printf(" %.1f", gaps[i]);
                std::printf("\n");
            }
        }
        DestroyWindow(window);
    }
    std::printf("test_render_replay: %zu captures, %d failed\n", captures.size(), failures);
    return failures ? 1 : 0;
}
