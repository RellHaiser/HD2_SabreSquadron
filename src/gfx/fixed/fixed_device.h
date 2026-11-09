// Direct3D 8's fixed-function model on a modern graphics API, shared by the SDL_GPU and OpenGL render
// devices (gfx/gfx.h, docs/render_device.md).
//
// The device (fixed_device.cpp) is the part no API differs in: the states, the resources in memory
// as Direct3D's managed ones are (the engine locks and writes them at any time), the frame recorded
// as draws with their own vertices and uniforms, the rules of Direct3D 8. A Renderer does the rest:
// it makes the API's objects, uploads textures, runs a recorded frame into the back buffer, presents
// it and reads it back. Only the render thread (the one that presents) enters a Renderer; the engine's
// texture thread touches memory only.
#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "gfx/gfx.h"

namespace ls3d::gfx::fixed {

constexpr int kStages = 3;          // texture stages the shaders draw (the engine uses up to 3)
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
    I4 coverage;                    // x: the alpha test is alpha to coverage
};

// ---------------------------------------------------------------- resources

// What a texture is drawn from on the GPU.
enum E_gpuFormat {
    GF_NONE,
    GF_BGRA8,                       // 8-bit B, G, R, A in memory (formats without an equal convert to it)
    GF_BC1,
    GF_BC2,
    GF_BC3,
    GF_RG8_SNORM,
};

struct S_texture;

struct S_surface {
    ULONG refs = 1;                 // an image surface's own (a level's are its texture's)
    S_texture *texture = nullptr;   // a texture's level (null: an image surface, or the back / depth buffer)
    UINT level = 0;
    UINT width = 0, height = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    std::vector<uint8_t> data;      // an image surface's rows (trace::FormatRows)
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
    std::vector<std::vector<uint8_t>> data;    // each level's rows in `format` (trace::FormatRows)
    std::vector<S_surface> surfaces;            // each level as a surface
    void *gpu = nullptr;                        // the renderer's texture
    bool dirty = true;                          // the GPU's copy is older than `data`
};

// The GPU format of a Direct3D format (GF_NONE: none); `convert`: through BGRA8.
E_gpuFormat GpuFormat(D3DFORMAT format, bool *convert);
// A level's bytes as the GPU format wants them (`scratch` holds a conversion).
const std::vector<uint8_t> &LevelBytes(const S_texture &t, UINT level, std::vector<uint8_t> *scratch);

// ---------------------------------------------------------------- what a draw needs

enum E_element {
    EL_FLOAT1,
    EL_FLOAT2,
    EL_FLOAT3,
    EL_FLOAT4,
    EL_COLOR,                       // four bytes as 0 - 1 (D3DCOLOR: B, G, R, A)
    EL_UBYTE4,
    EL_SHORT2,
    EL_SHORT4,
};

// A vertex format's attributes (location: 0 position, 1 normal, 2 diffuse, 3 specular, 4 and 5 the
// texture coordinate sets), and what the shaders need to know about it.
struct S_layout {
    struct S_attribute {
        uint32_t location, offset;
        E_element format;
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

// What a draw's fixed state is, in Direct3D's values.
struct S_pipelineKey {
    uint32_t layout, stride;
    uint8_t primitive, blend, srcBlend, dstBlend, writeMask, depthTest, depthWrite, depthFunc, cull, fill,
        zbias, alphaToCoverage;
    bool operator<(const S_pipelineKey &o) const;
    bool operator==(const S_pipelineKey &o) const;
};

struct S_samplerKey {
    uint8_t minFilter, magFilter, mipFilter, addressU, addressV, anisotropy;
    int16_t lodBias;
    bool operator<(const S_samplerKey &o) const;
};

struct S_viewport {
    float x, y, w, h, minZ, maxZ;
};
struct S_scissor {
    int x, y, w, h;
};

// One recorded draw (or clear) of the frame.
struct S_command {
    S_pipelineKey key = {};
    const S_layout *layout = nullptr;
    S_viewport viewport = {};
    S_scissor scissor = {};
    uint32_t vertexOffset = 0;      // bytes in the frame's vertices
    uint32_t indexOffset = 0;       // 16-bit indices in the frame's indices
    uint32_t count = 0;             // indices (indexed) or vertices
    bool indexed = false;
    uint32_t vertexUniforms = 0, pixelUniforms = 0;     // in the frame's blocks
    S_texture *textures[kStages] = {};
    S_samplerKey samplers[kStages] = {};
};

struct S_frame {
    std::vector<S_command> commands;
    std::vector<uint8_t> vertices;
    std::vector<uint16_t> indices;
    std::vector<S_vertexUniforms> vertexBlocks;
    std::vector<S_pixelUniforms> pixelBlocks;
};

// What the frames cost since the last report (LS3DF.ini SDLGPUStats).
struct S_stats {
    std::chrono::steady_clock::time_point since;
    std::chrono::steady_clock::time_point last;     // the last frame's end
    std::chrono::steady_clock::duration build{}, acquire{}, uploadWait{}, worst{};
    size_t frames = 0, draws = 0, vertexBytes = 0, uploadBatches = 0, uploadBytes = 0, slow = 0;
    size_t gaps[6] = {};                            // frames apart: under 6, 14, 20, 30, 40 ms, longer
    struct S_sample {
        float work, build, present, acquire;        // ms: the game's, RunFrame, renderer's Present, of it the swapchain
    } samples[24] = {};                             // the first frames, one by one
};

// ---------------------------------------------------------------- the API's part

class Renderer {
public:
    virtual ~Renderer() = default;
    // The API's device on the engine's window (false: none; the reason is logged).
    virtual bool MakeDevice(HWND window) = 0;
    virtual void ReleaseDevice() = 0;
    // The back and depth buffers (`present`: the sizes and formats settled).
    virtual bool MakeTargets(const D3DPRESENT_PARAMETERS &present) = 0;
    virtual void ReleaseTargets() = 0;
    virtual void WaitIdle() = 0;
    virtual bool SupportsFormat(E_gpuFormat format) = 0;
    // A texture's levels to the GPU (t->gpu made if it is not yet).
    virtual void UploadTexture(S_texture *t) = 0;
    virtual void ReleaseTexture(void *gpu) = 0;
    // The frame drawn into the back buffer. A Present or a ReadBack follows, once.
    virtual bool Run(const S_frame &frame) = 0;
    // The back buffer shown in the window, scaled to it, through `ramp` if there is one (the ramp changes
    // only what is shown: ReadBack never sees it).
    virtual void Present(const D3DGAMMARAMP *ramp) = 0;
    // The back buffer as BGRA8 rows, top first.
    virtual bool ReadBack(std::vector<uint8_t> *rows) = 0;

    S_stats *stats = nullptr;       // (the device's: uploads and waits are added here)
    int samples = 1;                // the samples a pixel MakeTargets settled on (1: no multisampling)
};

// The render device on a renderer (kept for the process's life).
Device *MakeDevice(Renderer *renderer);

// The engine's window as an SDL window: the SDL3 platform's own, else the window wrapped (`own`:
// the caller destroys it). `opengl`: a window OpenGL can draw on. Null when SDL cannot.
void *EngineWindow(HWND hwnd, bool opengl, bool *own);
// Direct3D's full screen on these APIs: SDL's full screen in the desktop's mode, with no change of
// display mode; back in a window when the device is windowed again.
void FitWindow(void *window, const D3DPRESENT_PARAMETERS &present);
// How frames reach the display: LS3DF.ini's PresentMode, otherwise what the game asked for.
enum E_presentMode {
    PRESENT_VSYNC,          // each frame shown whole, one per refresh; the game waits for it
    PRESENT_MAILBOX,        // no waiting: each refresh shows the newest frame whole
    PRESENT_IMMEDIATE,      // no waiting: shown at once, tearing if need be
};
E_presentMode ChoosePresentMode(const D3DPRESENT_PARAMETERS &present);
// The samples a pixel wanted, 1, 2, 4 or 8: what the game asks for or LS3DF.ini's MSAA, the more of them.
// (A renderer settles on as many as the GPU can do.)
int SamplesWanted(const D3DPRESENT_PARAMETERS &present);
// A gamma ramp as a 256 x 1 BGRA8 texture's 1024 bytes.
void RampTexels(const D3DGAMMARAMP &ramp, uint8_t *bgra);
// Where a back buffer goes in a window of that many pixels: as large as fits, its shape kept, centred.
struct S_rectangle {
    int x, y, w, h;
};
S_rectangle PictureRectangle(int windowWidth, int windowHeight, UINT backWidth, UINT backHeight);

// A line in LS3DF_render.log next to the DLL, flushed before the caller goes on (a driver that crashes
// leaves the last line as the culprit), with [Render] SDLGPUDebug=1 or SDLGPUStats=1.
void Trace(const char *format, ...);

}  // namespace ls3d::gfx::fixed
