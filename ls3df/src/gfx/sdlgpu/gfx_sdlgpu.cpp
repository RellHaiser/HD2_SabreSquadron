// The SDL_GPU renderer of the fixed-function render device (gfx/fixed/fixed_device.h): Direct3D 12 or
// Vulkan on Windows, Vulkan on Linux, Metal on macOS. Chosen with LS3DF.ini [Render] SDLGPU=1
// (gfx/gfx.cpp); SDL3.dll is loaded at run time.
//
// A draw's fixed state makes a pipeline, cached by it. One copy pass uploads a frame's vertices and
// indices, then one render pass draws it into the back buffer, a texture of the device's size that
// Present scales into the window's swapchain. With multisampling the pass draws into a multisampled
// target, kept from pass to pass, and resolves into the back buffer at its end. With ambient occlusion
// the frame's scene first draws its depth alone into a plain depth texture (a multisampled one cannot be
// read), and the render pass ends after the scene for the occlusion's passes, then goes on.
#include "gfx/sdlgpu/sdlgpu_device.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <map>
#include <vector>

#include "core/enhance.h"
#include "diag/events.h"
#include "gfx/fixed/fixed_device.h"
#include "platform/sdl3/sdl3_api.h"
#include "sdlgpu_shaders.h"

namespace ls3d::gfx {

namespace {

using namespace fixed;
using sdl3::api;

// The pixel shader's sampler slots: one more than the stages. SDL 3.4's Direct3D 12 backend checks
// only that a GPU descriptor heap (2048 samplers, 65536 views) is not yet full before it writes a
// draw's descriptors, not that they all fit: with 3 a draw, a write straddles the heap's end and
// fails (CopyDescriptorsSimple, an invalid handle). 4 divides both sizes.
constexpr int kSamplerSlots = 4;

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

SDL_GPUTextureFormat TextureFormat(E_gpuFormat f)
{
    switch (f) {
    case GF_BGRA8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case GF_BC1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case GF_BC2: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case GF_BC3: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case GF_RG8_SNORM: return SDL_GPU_TEXTUREFORMAT_R8G8_SNORM;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

SDL_GPUVertexElementFormat ElementFormat(E_element e)
{
    switch (e) {
    case EL_FLOAT1: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case EL_FLOAT2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case EL_FLOAT3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case EL_FLOAT4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case EL_COLOR: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    case EL_UBYTE4: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
    case EL_SHORT2: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
    case EL_SHORT4: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
}

SDL_GPUPrimitiveType Primitive(uint8_t d3d)
{
    switch (d3d) {
    case D3DPT_POINTLIST: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
    case D3DPT_LINELIST: return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case D3DPT_LINESTRIP: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
    case D3DPT_TRIANGLESTRIP: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    default: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }
}

class RendererSdlGpu final : public Renderer {
public:
    bool MakeDevice(HWND hwnd) override
    {
        window = static_cast<SDL_Window *>(EngineWindow(hwnd, false, &ownWindow));
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
                          const char *entry, SDL_GPUShaderStage stage, Uint32 samplerCount, Uint32 uniformBuffers = 1) {
            SDL_GPUShaderCreateInfo info = {};
            info.code = dxil ? dxilCode : spirvCode;
            info.code_size = dxil ? dxilSize : spirvSize;
            info.entrypoint = entry;
            info.format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
            info.stage = stage;
            info.num_samplers = samplerCount;
            info.num_uniform_buffers = uniformBuffers;
            return api.SDL_CreateGPUShader(gpu, &info);
        };
        vertexShader = shader(kFixedFunctionVSDxil, kFixedFunctionVSDxilSize, kFixedFunctionVSSpirv,
                              kFixedFunctionVSSpirvSize, "VSMain", SDL_GPU_SHADERSTAGE_VERTEX, 0);
        pixelShader = shader(kFixedFunctionPSDxil, kFixedFunctionPSDxilSize, kFixedFunctionPSSpirv,
                             kFixedFunctionPSSpirvSize, "PSMain", SDL_GPU_SHADERSTAGE_FRAGMENT, kSamplerSlots);
        presentVS = shader(kPresentVSDxil, kPresentVSDxilSize, kPresentVSSpirv, kPresentVSSpirvSize, "VSMain",
                           SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
        presentPS = shader(kPresentPSDxil, kPresentPSDxilSize, kPresentPSSpirv, kPresentPSSpirvSize, "PSMain",
                           SDL_GPU_SHADERSTAGE_FRAGMENT, kSamplerSlots, 0);
        if (Enhancements().occlusion) {
            occlusionVS = shader(kOcclusionVSDxil, kOcclusionVSDxilSize, kOcclusionVSSpirv, kOcclusionVSSpirvSize, "VSMain",
                                 SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
            occlusionPS[0] = shader(kOcclusionPSDxil, kOcclusionPSDxilSize, kOcclusionPSSpirv, kOcclusionPSSpirvSize, "PSOcclusion",
                                    SDL_GPU_SHADERSTAGE_FRAGMENT, kSamplerSlots, 1);
            occlusionPS[1] = shader(kOcclusionBlurPSDxil, kOcclusionBlurPSDxilSize, kOcclusionBlurPSSpirv, kOcclusionBlurPSSpirvSize, "PSBlur",
                                    SDL_GPU_SHADERSTAGE_FRAGMENT, kSamplerSlots, 1);
            occlusionPS[2] = shader(kOcclusionApplyPSDxil, kOcclusionApplyPSDxilSize, kOcclusionApplyPSSpirv, kOcclusionApplyPSSpirvSize, "PSApply",
                                    SDL_GPU_SHADERSTAGE_FRAGMENT, kSamplerSlots, 1);
            if (!occlusionVS || !occlusionPS[0] || !occlusionPS[1] || !occlusionPS[2])
                EnhancementLog("LS3DF.ini AmbientOcclusion: the shaders do not load (%s), drawing without", api.SDL_GetError());
        }
        if (!vertexShader || !pixelShader || !presentVS || !presentPS) {
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
        // The gamma ramp's table and what reads it and the picture.
        {
            SDL_GPUTextureCreateInfo info = {};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
            info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            info.width = 256;
            info.height = info.layer_count_or_depth = info.num_levels = 1;
            rampTexture = api.SDL_CreateGPUTexture(gpu, &info);
            SDL_GPUSamplerCreateInfo s = {};
            s.min_filter = s.mag_filter = SDL_GPU_FILTER_LINEAR;
            s.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            s.address_mode_u = s.address_mode_v = s.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            presentSampler = api.SDL_CreateGPUSampler(gpu, &s);
            s.min_filter = s.mag_filter = SDL_GPU_FILTER_NEAREST;
            nearestSampler = api.SDL_CreateGPUSampler(gpu, &s);
            rampLoaded = false;
        }
        EnhancementLog("LS3DF.ini SDLGPU: drawing with SDL_GPU on %s", api.SDL_GetGPUDeviceDriver(gpu));
        Trace("device: %s", api.SDL_GetGPUDeviceDriver(gpu));
        return true;
    }

    void ReleaseDevice() override
    {
        if (!gpu)
            return;
        api.SDL_WaitForGPUIdle(gpu);
        for (auto &p : pipelines)
            api.SDL_ReleaseGPUGraphicsPipeline(gpu, p.second);
        pipelines.clear();
        for (auto &s : samplers)
            api.SDL_ReleaseGPUSampler(gpu, s.second);
        samplers.clear();
        for (auto &p : depthPipelines)
            if (p.second)
                api.SDL_ReleaseGPUGraphicsPipeline(gpu, p.second);
        depthPipelines.clear();
        if (nearestSampler)
            api.SDL_ReleaseGPUSampler(gpu, nearestSampler);
        nearestSampler = nullptr;
        if (presentPipeline)
            api.SDL_ReleaseGPUGraphicsPipeline(gpu, presentPipeline);
        presentPipeline = nullptr;
        if (presentSampler)
            api.SDL_ReleaseGPUSampler(gpu, presentSampler);
        presentSampler = nullptr;
        if (rampTexture)
            api.SDL_ReleaseGPUTexture(gpu, rampTexture);
        rampTexture = nullptr;
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
        for (SDL_GPUFence *f : stagingFences)
            api.SDL_ReleaseGPUFence(gpu, f);
        stagingFences.clear();
        for (SDL_GPUTransferBuffer *t : {frameTransfer, staging})
            if (t)
                api.SDL_ReleaseGPUTransferBuffer(gpu, t);
        frameTransfer = staging = nullptr;
        frameTransferBytes = 0;
        if (whiteTexture)
            api.SDL_ReleaseGPUTexture(gpu, whiteTexture);
        whiteTexture = nullptr;
        for (SDL_GPUShader *s : {vertexShader, pixelShader, presentVS, presentPS, occlusionVS, occlusionPS[0], occlusionPS[1], occlusionPS[2]})
            if (s)
                api.SDL_ReleaseGPUShader(gpu, s);
        vertexShader = pixelShader = presentVS = presentPS = occlusionVS = occlusionPS[0] = occlusionPS[1] = occlusionPS[2] = nullptr;
        if (window)
            api.SDL_ReleaseWindowFromGPUDevice(gpu, window);
        api.SDL_DestroyGPUDevice(gpu);
        gpu = nullptr;
        if (ownWindow)
            api.SDL_DestroyWindow(window);
        window = nullptr;
        ownWindow = false;
    }

    bool MakeTargets(const D3DPRESENT_PARAMETERS &p) override
    {
        present = p;
        FitWindow(window, p);
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = present.BackBufferWidth;
        info.height = present.BackBufferHeight;
        info.layer_count_or_depth = info.num_levels = 1;
        backBuffer = api.SDL_CreateGPUTexture(gpu, &info);
        depth = multisampled = nullptr;
        if (present.EnableAutoDepthStencil) {
            for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                           SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D16_UNORM}) {
                if (api.SDL_GPUTextureSupportsFormat(gpu, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
                    depthFormat = f;
                    break;
                }
            }
        }
        // As many samples as were wanted and the GPU can do, the targets made with them or else without.
        SDL_GPUSampleCount count = SDL_GPU_SAMPLECOUNT_1;
        for (const auto &c : {std::pair<int, SDL_GPUSampleCount>{8, SDL_GPU_SAMPLECOUNT_8}, std::pair<int, SDL_GPUSampleCount>{4, SDL_GPU_SAMPLECOUNT_4},
                              std::pair<int, SDL_GPUSampleCount>{2, SDL_GPU_SAMPLECOUNT_2}}) {
            if (c.first <= SamplesWanted(p) && api.SDL_GPUTextureSupportsSampleCount(gpu, info.format, c.second) &&
                (!present.EnableAutoDepthStencil || api.SDL_GPUTextureSupportsSampleCount(gpu, depthFormat, c.second))) {
                count = c.second;
                break;
            }
        }
        if (count != SDL_GPU_SAMPLECOUNT_1) {
            info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            info.sample_count = count;
            multisampled = api.SDL_CreateGPUTexture(gpu, &info);
            if (!multisampled) {
                EnhancementLog("LS3DF.ini MSAA: the multisampled back buffer cannot be made (%s), drawing without", api.SDL_GetError());
                count = SDL_GPU_SAMPLECOUNT_1;
            }
        }
        if (present.EnableAutoDepthStencil) {
            info.format = depthFormat;
            info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
            info.sample_count = count;
            depth = api.SDL_CreateGPUTexture(gpu, &info);
            if (!depth)
                return false;
        }
        // (A pipeline is made for one sample count.)
        const int settled = count == SDL_GPU_SAMPLECOUNT_8 ? 8 : count == SDL_GPU_SAMPLECOUNT_4 ? 4 : count == SDL_GPU_SAMPLECOUNT_2 ? 2 : 1;
        if (settled != samples) {
            for (auto &m : pipelines)
                if (m.second)
                    api.SDL_ReleaseGPUGraphicsPipeline(gpu, m.second);
            pipelines.clear();
        }
        samples = settled;
        MakeOcclusionTargets();
        cleared = false;
        // (What this GPU cannot do falls back to vsync, always there.)
        const E_presentMode asked = ChoosePresentMode(p);
        SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
        if (asked == PRESENT_IMMEDIATE && api.SDL_WindowSupportsGPUPresentMode(gpu, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
            mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        else if (asked == PRESENT_MAILBOX && api.SDL_WindowSupportsGPUPresentMode(gpu, window, SDL_GPU_PRESENTMODE_MAILBOX))
            mode = SDL_GPU_PRESENTMODE_MAILBOX;
        api.SDL_SetGPUSwapchainParameters(gpu, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
        Trace("present mode: %s", mode == SDL_GPU_PRESENTMODE_VSYNC ? "vsync" : mode == SDL_GPU_PRESENTMODE_MAILBOX ? "mailbox" : "immediate");
        return backBuffer != nullptr;
    }
    void ReleaseTargets() override
    {
        if (backBuffer)
            api.SDL_ReleaseGPUTexture(gpu, backBuffer);
        if (depth)
            api.SDL_ReleaseGPUTexture(gpu, depth);
        if (multisampled)
            api.SDL_ReleaseGPUTexture(gpu, multisampled);
        backBuffer = depth = multisampled = nullptr;
        ReleaseOcclusionTargets();
    }
    void WaitIdle() override { api.SDL_WaitForGPUIdle(gpu); }
    bool SupportsFormat(E_gpuFormat f) override
    {
        return api.SDL_GPUTextureSupportsFormat(gpu, TextureFormat(f), SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    }
    void ReleaseTexture(void *texture) override { api.SDL_ReleaseGPUTexture(gpu, static_cast<SDL_GPUTexture *>(texture)); }

    void UploadTexture(S_texture *t) override
    {
        bool convert;
        const E_gpuFormat format = GpuFormat(t->format, &convert);
        if (!t->gpu) {
            SDL_GPUTextureCreateInfo info = {};
            info.type = SDL_GPU_TEXTURETYPE_2D;
            info.format = TextureFormat(format);
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
            if (!t->gpu)
                return;
        }
        std::vector<uint8_t> scratch;
        for (UINT l = 0; l < t->levels; ++l) {
            const S_surface &s = t->surfaces[l];
            const std::vector<uint8_t> &bytes = LevelBytes(*t, l, &scratch);
            StageLevel(static_cast<SDL_GPUTexture *>(t->gpu), l, s.width, s.height, bytes.data(),
                       static_cast<Uint32>(bytes.size()));
        }
    }

    bool Run(const S_frame &frame) override
    {
        FlushStaged();              // (the textures, uploaded before the frame's commands)
        cmd = api.SDL_AcquireGPUCommandBuffer(gpu);
        if (!cmd)
            return false;
        // The frame's vertices and indices.
        const Uint32 vertexBytes = static_cast<Uint32>(frame.vertices.size());
        const Uint32 indexBytes = static_cast<Uint32>(frame.indices.size() * 2);
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
                std::memcpy(p, frame.vertices.data(), vertexBytes);
            if (indexBytes)
                std::memcpy(p + vertexBytes, frame.indices.data(), indexBytes);
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
        // The scene's depth alone, for the ambient occlusion.
        const bool occlusion = frame.occlusion.active && OcclusionReady();
        const uint32_t split = occlusion ? frame.occlusion.split : static_cast<uint32_t>(frame.commands.size());
        if (occlusion) {
            SDL_GPUDepthStencilTargetInfo only = {};
            only.texture = occlusionDepth;
            only.load_op = SDL_GPU_LOADOP_CLEAR;
            only.clear_depth = 1;
            only.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = api.SDL_BeginGPURenderPass(cmd, nullptr, 0, &only);
            DrawCommands(pass, frame, 0, split, true);
            api.SDL_EndGPURenderPass(pass);
        }
        // The draws.
        SDL_GPUColorTargetInfo color = {};
        color.texture = multisampled ? multisampled : backBuffer;
        color.load_op = cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        if (multisampled) {
            // (Kept: a read-back in the middle of a frame runs it in parts.)
            color.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
            color.resolve_texture = backBuffer;
        }
        SDL_GPUDepthStencilTargetInfo depthTarget = {};
        depthTarget.texture = depth;
        depthTarget.load_op = cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        depthTarget.clear_depth = 1;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        depthTarget.stencil_load_op = cleared ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        cleared = true;
        SDL_GPUColorTargetInfo sceneColor = color;
        if (occlusion)                  // (the pass that ends the frame resolves)
            sceneColor.store_op = SDL_GPU_STOREOP_STORE, sceneColor.resolve_texture = nullptr;
        SDL_GPURenderPass *pass = api.SDL_BeginGPURenderPass(cmd, &sceneColor, 1, depth ? &depthTarget : nullptr);
        DrawCommands(pass, frame, 0, split, false);
        if (occlusion) {
            api.SDL_EndGPURenderPass(pass);
            RunOcclusionPasses(frame.occlusion);
            color.load_op = SDL_GPU_LOADOP_LOAD;
            depthTarget.load_op = depthTarget.stencil_load_op = SDL_GPU_LOADOP_LOAD;
            pass = api.SDL_BeginGPURenderPass(cmd, &color, 1, depth ? &depthTarget : nullptr);
            const SDL_GPUViewport whole = {0, 0, static_cast<float>(present.BackBufferWidth), static_cast<float>(present.BackBufferHeight), 0, 1};
            const SDL_Rect wholeRect = {0, 0, static_cast<int>(present.BackBufferWidth), static_cast<int>(present.BackBufferHeight)};
            api.SDL_SetGPUViewport(pass, &whole);
            api.SDL_SetGPUScissor(pass, &wholeRect);
            api.SDL_BindGPUGraphicsPipeline(pass, occlusionApply);
            const S_occlusionUniforms u = OcclusionUniforms(frame.occlusion, present.BackBufferWidth, present.BackBufferHeight, 0, 0);
            api.SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            BindOcclusionSamplers(pass, occlusionBlurred[1]);
            api.SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            DrawCommands(pass, frame, split, static_cast<uint32_t>(frame.commands.size()), false);
        }
        api.SDL_EndGPURenderPass(pass);
        return true;
    }

    // Commands [first, last) of a frame; `depthOnly`: those that write depth, into the plain depth texture.
    void DrawCommands(SDL_GPURenderPass *pass, const S_frame &frame, uint32_t first, uint32_t last, bool depthOnly)
    {
        SDL_GPUGraphicsPipeline *boundPipeline = nullptr;
        S_pipelineKey boundKey = {};
        uint32_t boundVertexUniforms = ~0u, boundPixelUniforms = ~0u;
        SDL_GPUTextureSamplerBinding boundSamplers[kSamplerSlots] = {};
        for (uint32_t index = first; index < last; ++index) {
            const S_command &c = frame.commands[index];
            if (depthOnly && !(c.key.depthTest && c.key.depthWrite))
                continue;
            if (!boundPipeline || !(c.key == boundKey)) {
                SDL_GPUGraphicsPipeline *pipeline = Pipeline(c.key, *c.layout, depthOnly);
                if (!pipeline)
                    continue;
                api.SDL_BindGPUGraphicsPipeline(pass, pipeline);
                boundPipeline = pipeline;
                boundKey = c.key;
            }
            const SDL_GPUViewport viewport = {c.viewport.x, c.viewport.y, c.viewport.w, c.viewport.h, c.viewport.minZ, c.viewport.maxZ};
            const SDL_Rect scissor = {c.scissor.x, c.scissor.y, c.scissor.w, c.scissor.h};
            api.SDL_SetGPUViewport(pass, &viewport);
            api.SDL_SetGPUScissor(pass, &scissor);
            SDL_GPUBufferBinding streams[2] = {{vertexBuffer, c.vertexOffset}, {defaultsBuffer, 0}};
            api.SDL_BindGPUVertexBuffers(pass, 0, streams, 2);
            // The stages' textures (re-bound only when they change: each binding takes descriptors).
            SDL_GPUTextureSamplerBinding bindings[kSamplerSlots];
            for (int s = 0; s < kSamplerSlots; ++s) {
                bindings[s].texture = s < kStages && c.textures[s] && c.textures[s]->gpu
                                          ? static_cast<SDL_GPUTexture *>(c.textures[s]->gpu) : whiteTexture;
                bindings[s].sampler = Sampler(c.samplers[s < kStages ? s : 0]);
            }
            if (std::memcmp(bindings, boundSamplers, sizeof bindings) != 0) {
                api.SDL_BindGPUFragmentSamplers(pass, 0, bindings, kSamplerSlots);
                std::memcpy(boundSamplers, bindings, sizeof bindings);
            }
            if (c.vertexUniforms != boundVertexUniforms) {
                api.SDL_PushGPUVertexUniformData(cmd, 0, &frame.vertexBlocks[c.vertexUniforms], sizeof(S_vertexUniforms));
                boundVertexUniforms = c.vertexUniforms;
            }
            if (c.pixelUniforms != boundPixelUniforms) {
                api.SDL_PushGPUFragmentUniformData(cmd, 0, &frame.pixelBlocks[c.pixelUniforms], sizeof(S_pixelUniforms));
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
    }

    void Present(const D3DGAMMARAMP *ramp) override
    {
        if (ramp)
            LoadRamp(*ramp);
        const auto start = std::chrono::steady_clock::now();
        SDL_GPUTexture *swapchain = nullptr;
        Uint32 w = 0, h = 0;
        diag::Mark(diag::EV_ACQUIRE_BEGIN);
        const bool acquired = window && api.SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h);
        diag::Mark(diag::EV_ACQUIRE_END);
        stats->acquire += std::chrono::steady_clock::now() - start;
        SDL_GPUGraphicsPipeline *throughRamp = acquired && swapchain && ramp ? PresentPipeline() : nullptr;
        const S_rectangle picture = PictureRectangle(static_cast<int>(w), static_cast<int>(h), present.BackBufferWidth,
                                                     present.BackBufferHeight);
        if (throughRamp) {
            SDL_GPUColorTargetInfo target = {};
            target.texture = swapchain;
            target.load_op = SDL_GPU_LOADOP_CLEAR;      // (black bars, a picture's shape kept)
            target.clear_color = {0, 0, 0, 1};
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = api.SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            api.SDL_BindGPUGraphicsPipeline(pass, throughRamp);
            const SDL_GPUViewport viewport = {static_cast<float>(picture.x), static_cast<float>(picture.y),
                                              static_cast<float>(picture.w), static_cast<float>(picture.h), 0, 1};
            api.SDL_SetGPUViewport(pass, &viewport);
            SDL_GPUTextureSamplerBinding bindings[kSamplerSlots];
            for (int s = 0; s < kSamplerSlots; ++s)
                bindings[s] = {s == 1 ? rampTexture : backBuffer, presentSampler};
            api.SDL_BindGPUFragmentSamplers(pass, 0, bindings, kSamplerSlots);
            api.SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            api.SDL_EndGPURenderPass(pass);
        } else if (acquired && swapchain) {
            SDL_GPUBlitInfo blit = {};
            blit.source.texture = backBuffer;
            blit.source.w = present.BackBufferWidth;
            blit.source.h = present.BackBufferHeight;
            blit.destination.texture = swapchain;
            blit.destination.x = static_cast<Uint32>(picture.x);
            blit.destination.y = static_cast<Uint32>(picture.y);
            blit.destination.w = static_cast<Uint32>(picture.w);
            blit.destination.h = static_cast<Uint32>(picture.h);
            blit.load_op = SDL_GPU_LOADOP_CLEAR;        // (black bars, a picture's shape kept)
            blit.clear_color = {0, 0, 0, 1};
            blit.filter = SDL_GPU_FILTER_LINEAR;
            api.SDL_BlitGPUTexture(cmd, &blit);
        }
        diag::Mark(diag::EV_SUBMIT_BEGIN);
        api.SDL_SubmitGPUCommandBuffer(cmd);
        diag::Mark(diag::EV_SUBMIT_END);
        cmd = nullptr;
    }

    bool ReadBack(std::vector<uint8_t> *rows) override
    {
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
        cmd = nullptr;
        const auto *p = static_cast<const uint8_t *>(api.SDL_MapGPUTransferBuffer(gpu, transfer, false));
        if (p)
            rows->assign(p, p + size);
        api.SDL_UnmapGPUTransferBuffer(gpu, transfer);
        api.SDL_ReleaseGPUTransferBuffer(gpu, transfer);
        return p != nullptr;
    }

private:
    // ---- ambient occlusion
    void MakeOcclusionTargets()
    {
        if (!occlusionVS || !occlusionPS[0] || !occlusionPS[1] || !occlusionPS[2] || !present.EnableAutoDepthStencil)
            return;
        const auto fail = [&](const char *what) {
            EnhancementLog("LS3DF.ini AmbientOcclusion: %s cannot be made (%s), drawing without", what, api.SDL_GetError());
            ReleaseOcclusionTargets();
        };
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.width = present.BackBufferWidth;
        info.height = present.BackBufferHeight;
        info.layer_count_or_depth = info.num_levels = 1;
        info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        occlusionDepth = api.SDL_CreateGPUTexture(gpu, &info);
        if (!occlusionDepth)
            return fail("its depth texture");
        info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        for (SDL_GPUTexture **t : {&occlusionRaw, &occlusionBlurred[0], &occlusionBlurred[1]}) {
            *t = api.SDL_CreateGPUTexture(gpu, &info);
            if (!*t)
                return fail("its target");
        }
        const auto pipeline = [&](SDL_GPUShader *ps, SDL_GPUTextureFormat format, SDL_GPUSampleCount count, bool multiply) {
            SDL_GPUColorTargetDescription target = {};
            target.format = format;
            if (multiply) {
                target.blend_state.enable_blend = true;
                target.blend_state.src_color_blendfactor = target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
                target.blend_state.dst_color_blendfactor = target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
                target.blend_state.color_blend_op = target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            }
            SDL_GPUGraphicsPipelineCreateInfo p = {};
            p.vertex_shader = occlusionVS;
            p.fragment_shader = ps;
            p.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            p.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
            p.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
            p.multisample_state.sample_count = count;
            p.target_info.color_target_descriptions = &target;
            p.target_info.num_color_targets = 1;
            // (Drawn in the frame's pass, which has the depth target: tested against nothing.)
            if (multiply && depth) {
                p.target_info.has_depth_stencil_target = true;
                p.target_info.depth_stencil_format = depthFormat;
            }
            return api.SDL_CreateGPUGraphicsPipeline(gpu, &p);
        };
        occlusionMake = pipeline(occlusionPS[0], SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_SAMPLECOUNT_1, false);
        occlusionBlur = pipeline(occlusionPS[1], SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_SAMPLECOUNT_1, false);
        occlusionApply = pipeline(occlusionPS[2], SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  samples == 8   ? SDL_GPU_SAMPLECOUNT_8
                                  : samples == 4 ? SDL_GPU_SAMPLECOUNT_4
                                  : samples == 2 ? SDL_GPU_SAMPLECOUNT_2
                                                 : SDL_GPU_SAMPLECOUNT_1,
                                  true);
        if (!occlusionMake || !occlusionBlur || !occlusionApply)
            fail("a pipeline");
    }
    void ReleaseOcclusionTargets()
    {
        for (SDL_GPUGraphicsPipeline **p : {&occlusionMake, &occlusionBlur, &occlusionApply}) {
            if (*p)
                api.SDL_ReleaseGPUGraphicsPipeline(gpu, *p);
            *p = nullptr;
        }
        for (SDL_GPUTexture **t : {&occlusionDepth, &occlusionRaw, &occlusionBlurred[0], &occlusionBlurred[1]}) {
            if (*t)
                api.SDL_ReleaseGPUTexture(gpu, *t);
            *t = nullptr;
        }
    }
    bool OcclusionReady() const { return occlusionApply != nullptr; }
    // Slot 0 the plain depth, slot 1 `occlusion`. (All the slots: kSamplerSlots.)
    void BindOcclusionSamplers(SDL_GPURenderPass *pass, SDL_GPUTexture *occlusion)
    {
        SDL_GPUTextureSamplerBinding bindings[kSamplerSlots];
        for (int s = 0; s < kSamplerSlots; ++s)
            bindings[s] = {s == 0 ? occlusionDepth : occlusion ? occlusion : occlusionDepth, nearestSampler};
        api.SDL_BindGPUFragmentSamplers(pass, 0, bindings, kSamplerSlots);
    }
    void RunOcclusionPasses(const S_occlusion &o)
    {
        const auto fullPass = [&](SDL_GPUTexture *target, SDL_GPUGraphicsPipeline *pipeline, SDL_GPUTexture *source, float stepX, float stepY) {
            SDL_GPUColorTargetInfo info = {};
            info.texture = target;
            info.load_op = SDL_GPU_LOADOP_DONT_CARE;
            info.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass = api.SDL_BeginGPURenderPass(cmd, &info, 1, nullptr);
            api.SDL_BindGPUGraphicsPipeline(pass, pipeline);
            const S_occlusionUniforms u = OcclusionUniforms(o, present.BackBufferWidth, present.BackBufferHeight, stepX, stepY);
            api.SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof u);
            BindOcclusionSamplers(pass, source);
            api.SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            api.SDL_EndGPURenderPass(pass);
        };
        fullPass(occlusionRaw, occlusionMake, nullptr, 0, 0);
        fullPass(occlusionBlurred[0], occlusionBlur, occlusionRaw, 1, 0);
        fullPass(occlusionBlurred[1], occlusionBlur, occlusionBlurred[0], 0, 1);
    }

    // The ramp's table for this frame's command buffer, when it has changed.
    void LoadRamp(const D3DGAMMARAMP &ramp)
    {
        uint8_t texels[sizeof rampTexels];
        RampTexels(ramp, texels);
        if (rampLoaded && !std::memcmp(texels, rampTexels, sizeof texels))
            return;
        SDL_GPUTransferBufferCreateInfo t = {};
        t.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        t.size = sizeof texels;
        SDL_GPUTransferBuffer *transfer = api.SDL_CreateGPUTransferBuffer(gpu, &t);
        if (!transfer)
            return;
        std::memcpy(api.SDL_MapGPUTransferBuffer(gpu, transfer, false), texels, sizeof texels);
        api.SDL_UnmapGPUTransferBuffer(gpu, transfer);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo from = {};
        from.transfer_buffer = transfer;
        SDL_GPUTextureRegion to = {};
        to.texture = rampTexture;
        to.w = 256;
        to.h = to.d = 1;
        api.SDL_UploadToGPUTexture(copy, &from, &to, false);
        api.SDL_EndGPUCopyPass(copy);
        api.SDL_ReleaseGPUTransferBuffer(gpu, transfer);
        std::memcpy(rampTexels, texels, sizeof texels);
        rampLoaded = true;
    }

    SDL_GPUGraphicsPipeline *PresentPipeline()
    {
        if (presentPipeline)
            return presentPipeline;
        SDL_GPUColorTargetDescription target = {};
        target.format = api.SDL_GetGPUSwapchainTextureFormat(gpu, window);
        SDL_GPUGraphicsPipelineCreateInfo info = {};
        info.vertex_shader = presentVS;
        info.fragment_shader = presentPS;
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        presentPipeline = api.SDL_CreateGPUGraphicsPipeline(gpu, &info);
        if (!presentPipeline) {
            EnhancementLog("LS3DF.ini SDLGPU: the gamma ramp's pipeline cannot be made (%s)", api.SDL_GetError());
            Trace("gamma ramp's pipeline: %s", api.SDL_GetError());
        }
        return presentPipeline;
    }

    SDL_GPUSampler *Sampler(const S_samplerKey &key)
    {
        auto s = samplers.find(key);
        if (s != samplers.end())
            return s->second;
        SDL_GPUSamplerCreateInfo info = {};
        const bool anisotropic = key.minFilter == D3DTEXF_ANISOTROPIC || key.magFilter == D3DTEXF_ANISOTROPIC;
        info.min_filter = key.minFilter >= D3DTEXF_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mag_filter = key.magFilter >= D3DTEXF_LINEAR ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        // (Anisotropic with no mip filter: linear too for Direct3D 12, on level 0 alone by max_lod below.)
        info.mipmap_mode = key.mipFilter == D3DTEXF_LINEAR || (anisotropic && key.anisotropy > 1) ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                                                                                  : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
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

    SDL_GPUGraphicsPipeline *Pipeline(const S_pipelineKey &key, const S_layout &layout, bool depthOnly = false)
    {
        std::map<S_pipelineKey, SDL_GPUGraphicsPipeline *> &cache = depthOnly ? depthPipelines : pipelines;
        auto p = cache.find(key);
        if (p != cache.end())
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
        // (Offsets in the defaults: normal 0, diffuse white at 12, specular at 16, coordinates at 20, 28.)
        static const SDL_GPUVertexAttribute kDefaults[6] = {
            {0, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},       {1, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
            {2, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 12}, {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16},
            {4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 20},      {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 28}};
        for (int i = 0; i < 6; ++i)
            attributes[i] = kDefaults[i];
        for (const S_layout::S_attribute &a : layout.attributes) {
            if (a.location < 6)
                attributes[a.location] = {a.location, 0, ElementFormat(a.format), a.offset};
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
        info.primitive_type = Primitive(key.primitive);
        info.rasterizer_state.fill_mode = key.fill == D3DFILL_WIREFRAME ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = key.cull == D3DCULL_CCW  ? SDL_GPU_CULLMODE_BACK
                                          : key.cull == D3DCULL_CW ? SDL_GPU_CULLMODE_FRONT
                                                                   : SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        if (key.zbias) {
            // Direct3D 8's ZBIAS n (0 - 16), as its runtime draws it (Windows' d3d8.dll and dgVoodoo's,
            // measured): nearer by 2n depth-buffer steps and (n + 2) / 8 of the triangle's depth slope.
            // The slope's part is what keeps a decal on sloping ground in front of it.
            info.rasterizer_state.enable_depth_bias = true;
            info.rasterizer_state.depth_bias_constant_factor = -2.0f * static_cast<float>(key.zbias);
            info.rasterizer_state.depth_bias_slope_factor = -(static_cast<float>(key.zbias) + 2.0f) / 8.0f;
        }
        info.rasterizer_state.enable_depth_clip = true;
        info.multisample_state.sample_count = samples == 8   ? SDL_GPU_SAMPLECOUNT_8
                                              : samples == 4 ? SDL_GPU_SAMPLECOUNT_4
                                              : samples == 2 ? SDL_GPU_SAMPLECOUNT_2
                                                             : SDL_GPU_SAMPLECOUNT_1;
        info.multisample_state.enable_alpha_to_coverage = key.alphaToCoverage != 0;
        info.depth_stencil_state.enable_depth_test = key.depthTest != 0;
        info.depth_stencil_state.enable_depth_write = key.depthWrite != 0;
        info.depth_stencil_state.compare_op = static_cast<SDL_GPUCompareOp>(key.depthFunc);
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        info.target_info.has_depth_stencil_target = depth != nullptr;
        info.target_info.depth_stencil_format = depthFormat;
        if (depthOnly) {
            info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
            info.multisample_state.enable_alpha_to_coverage = false;
            info.target_info.color_target_descriptions = nullptr;
            info.target_info.num_color_targets = 0;
            info.target_info.has_depth_stencil_target = true;
            info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        }
        Trace("pipeline %zu: stride %u primitive %u blend %u %u/%u mask %u depth %u/%u func %u cull %u fill %u zbias %u coverage %u%s%s",
              pipelines.size(), key.stride, key.primitive, key.blend, key.srcBlend, key.dstBlend, key.writeMask,
              key.depthTest, key.depthWrite, key.depthFunc, key.cull, key.fill, key.zbias, key.alphaToCoverage,
              layout.pretransformed ? " pretransformed" : "", layout.programmable ? " programmable" : "");
        for (const S_layout::S_attribute &a : layout.attributes)
            Trace("    location %u offset %u format %d", a.location, a.offset, static_cast<int>(a.format));
        diag::Mark(diag::EV_PIPELINE_CREATE_BEGIN);
        SDL_GPUGraphicsPipeline *made = api.SDL_CreateGPUGraphicsPipeline(gpu, &info);
        diag::Mark(diag::EV_PIPELINE_CREATE_END);
        Trace("    -> %s", made ? "made" : api.SDL_GetError());
        if (!made)
            EnhancementLog("LS3DF.ini SDLGPU: a pipeline cannot be made (%s)", api.SDL_GetError());
        cache[key] = made;
        return made;
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
        SDL_GPUCommandBuffer *uploads = api.SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(uploads);
        SDL_GPUTransferBufferLocation from = {transfer, 0};
        SDL_GPUBufferRegion to = {buffer, 0, size};
        api.SDL_UploadToGPUBuffer(copy, &from, &to, false);
        api.SDL_EndGPUCopyPass(copy);
        api.SDL_SubmitGPUCommandBuffer(uploads);
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
        SDL_GPUCommandBuffer *uploads = api.SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(uploads);
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
        api.SDL_SubmitGPUCommandBuffer(uploads);
        api.SDL_ReleaseGPUTransferBuffer(gpu, transfer);
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
        if (!stagingMap)       // (cycled: the batches still in flight keep their own)
            stagingMap = static_cast<uint8_t *>(api.SDL_MapGPUTransferBuffer(gpu, staging, true));
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
        SDL_GPUCommandBuffer *uploads = api.SDL_AcquireGPUCommandBuffer(gpu);
        SDL_GPUCopyPass *copy = api.SDL_BeginGPUCopyPass(uploads);
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
        // Not waited for: only when more than kStagingInFlight batches are (each holds its own
        // 16 MB until the GPU is done with it, which a 32-bit process must keep bounded).
        if (SDL_GPUFence *fence = api.SDL_SubmitGPUCommandBufferAndAcquireFence(uploads))
            stagingFences.push_back(fence);
        while (stagingFences.size() > kStagingInFlight) {
            const auto start = std::chrono::steady_clock::now();
            SDL_GPUFence *oldest = stagingFences.front();
            diag::Mark(diag::EV_UPLOAD_WAIT_BEGIN);
            api.SDL_WaitForGPUFences(gpu, true, &oldest, 1);
            diag::Mark(diag::EV_UPLOAD_WAIT_END);
            api.SDL_ReleaseGPUFence(gpu, oldest);
            stagingFences.pop_front();
            stats->uploadWait += std::chrono::steady_clock::now() - start;
        }
        stats->uploadBatches += 1;
        stats->uploadBytes += stagingUsed;
        staged.clear();
        stagingUsed = 0;
    }
    static constexpr size_t kStagingInFlight = 4;
    std::deque<SDL_GPUFence *> stagingFences;

    SDL_GPUDevice *gpu = nullptr;
    SDL_Window *window = nullptr;
    bool ownWindow = false;
    D3DPRESENT_PARAMETERS present = {};
    SDL_GPUCommandBuffer *cmd = nullptr;    // Run's, until Present or ReadBack submits it
    SDL_GPUTexture *backBuffer = nullptr, *depth = nullptr, *whiteTexture = nullptr;
    SDL_GPUTexture *multisampled = nullptr;     // what is drawn into with multisampling, resolved into backBuffer
    SDL_GPUTexture *rampTexture = nullptr;
    SDL_GPUSampler *presentSampler = nullptr, *nearestSampler = nullptr;
    SDL_GPUShader *occlusionVS = nullptr, *occlusionPS[3] = {};
    SDL_GPUTexture *occlusionDepth = nullptr, *occlusionRaw = nullptr, *occlusionBlurred[2] = {};
    SDL_GPUGraphicsPipeline *occlusionMake = nullptr, *occlusionBlur = nullptr, *occlusionApply = nullptr;
    std::map<S_pipelineKey, SDL_GPUGraphicsPipeline *> depthPipelines;
    SDL_GPUShader *presentVS = nullptr, *presentPS = nullptr;
    SDL_GPUGraphicsPipeline *presentPipeline = nullptr;
    uint8_t rampTexels[1024] = {};
    bool rampLoaded = false;
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    bool cleared = false;
    SDL_GPUShader *vertexShader = nullptr, *pixelShader = nullptr;
    SDL_GPUBuffer *vertexBuffer = nullptr, *indexBuffer = nullptr, *defaultsBuffer = nullptr;
    Uint32 vertexCapacity = 0, indexCapacity = 0;
    SDL_GPUTransferBuffer *frameTransfer = nullptr, *staging = nullptr;
    Uint32 frameTransferBytes = 0, stagingUsed = 0;
    uint8_t *stagingMap = nullptr;
    std::vector<S_staged> staged;
    std::map<S_pipelineKey, SDL_GPUGraphicsPipeline *> pipelines;
    std::map<S_samplerKey, SDL_GPUSampler *> samplers;
};

}  // namespace

Device *SdlGpuDevice()
{
    static Device *device;
    if (!device) {
        static RendererSdlGpu renderer;
        device = fixed::MakeDevice(&renderer);
    }
    return device;
}

}  // namespace ls3d::gfx
