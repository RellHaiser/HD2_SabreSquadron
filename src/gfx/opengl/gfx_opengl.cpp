// The OpenGL 3.3 renderer of the fixed-function render device (gfx/fixed/fixed_device.h), for GPUs and
// drivers without Direct3D 12, Vulkan or Metal. Chosen with LS3DF.ini [Render] OpenGL=1 (gfx/gfx.cpp);
// SDL3 makes the core-profile context and finds the functions at run time.
//
// The shaders are the same source as SDL_GPU's (sdlgpu/shaders/fixed_function.hlsl), compiled by the
// driver. Where OpenGL differs from Direct3D:
// - The shader moves clip depth to OpenGL's -w to w and turns y over. The back buffer (a framebuffer
//   object of the device's size) therefore has Direct3D's rows: row 0 is the picture's top, viewports
//   and scissors are as Direct3D gives them, a read-back's rows are in order, and rasterising ties
//   fall as Direct3D's do. Turned over, Direct3D's clockwise front faces are counter-clockwise.
// - Present scales the back buffer into the window, turning it back over.
// Every call is on the render thread: the device only reaches a renderer from there.
#include "gfx/opengl/opengl_device.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <SDL3/SDL_opengl.h>

#include "core/enhance.h"
#include "gfx/fixed/fixed_device.h"
#include "platform/sdl3/sdl3_api.h"
#include "sdlgpu_shaders.h"

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif

namespace ls3d::gfx {

namespace {

using namespace fixed;
using sdl3::api;

// OpenGL 1.1's functions (declared by the header, found at run time like the rest).
#define LS3D_GL_11(X)                                                                                        \
    X(glEnable) X(glDisable) X(glClear) X(glClearColor) X(glClearDepth) X(glViewport) X(glScissor)           \
    X(glDepthMask) X(glDepthFunc) X(glDepthRange) X(glColorMask) X(glBlendFunc) X(glCullFace) X(glFrontFace) \
    X(glPolygonMode) X(glPolygonOffset) X(glPixelStorei) X(glReadPixels) X(glReadBuffer) X(glGenTextures)    \
    X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexParameteri) X(glDrawArrays) X(glDrawElements) \
    X(glGetIntegerv) X(glGetString) X(glFinish)

#define LS3D_GL_LATER(X)                                                                                     \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                               \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D)                                                 \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                                     \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                               \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                                     \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                                     \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange)                                                           \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                           \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                                     \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                           \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                           \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                                         \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                                   \
    X(PFNGLVERTEXATTRIB4FPROC, glVertexAttrib4f)                                                             \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                                 \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                                 \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                                 \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                               \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                                   \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                         \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                               \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                               \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                                 \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                                   \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                                 \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                                       \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                                     \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                                     \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                                       \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)                                                 \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)                                                   \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                           \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                                     \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                           \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                                 \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                           \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                             \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                                         \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                                                   \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                                         \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                                   \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                                           \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                                                   \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                                             \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                                                   \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                                                       \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf)                                                       \
    X(PFNGLGETSTRINGIPROC, glGetStringi)

struct S_gl {
#define LS3D_GL_MEMBER(name) decltype(&::name) name;
    LS3D_GL_11(LS3D_GL_MEMBER)
#undef LS3D_GL_MEMBER
#define LS3D_GL_MEMBER(type, name) type name;
    LS3D_GL_LATER(LS3D_GL_MEMBER)
#undef LS3D_GL_MEMBER
    PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;   // (4.3: only for SDLGPUDebug, when there)
} gl;

GLuint Id(const void *handle) { return static_cast<GLuint>(reinterpret_cast<uintptr_t>(handle)); }
void *Handle(GLuint id) { return reinterpret_cast<void *>(static_cast<uintptr_t>(id)); }

GLenum BlendFactor(uint8_t d3d)
{
    switch (d3d) {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

GLenum Primitive(uint8_t d3d)
{
    switch (d3d) {
    case D3DPT_POINTLIST: return GL_POINTS;
    case D3DPT_LINELIST: return GL_LINES;
    case D3DPT_LINESTRIP: return GL_LINE_STRIP;
    case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
    default: return GL_TRIANGLES;
    }
}

GLint Address(uint8_t d3d)
{
    switch (d3d) {
    case D3DTADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP:
    case D3DTADDRESS_BORDER:
    case D3DTADDRESS_MIRRORONCE: return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

void APIENTRY DebugMessage(GLenum, GLenum type, GLuint, GLenum, GLsizei, const GLchar *message, const void *)
{
    if (type == GL_DEBUG_TYPE_ERROR)
        Trace("OpenGL error: %s", message);
}

class RendererOpenGL final : public Renderer {
public:
    bool MakeDevice(HWND hwnd) override
    {
        window = static_cast<SDL_Window *>(EngineWindow(hwnd, true, &ownWindow));
        if (!window) {
            EnhancementLog("LS3DF.ini OpenGL: no SDL window for the engine's (%s)", api.SDL_GetError());
            return false;
        }
        api.SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        api.SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        api.SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        api.SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, Enhancements().sdlGpuDebug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);
        context = api.SDL_GL_CreateContext(window);
        if (!context) {
            EnhancementLog("LS3DF.ini OpenGL: no OpenGL 3.3 context (%s)", api.SDL_GetError());
            ReleaseDevice();
            return false;
        }
        bool found = true;
#define LS3D_GL_LOAD(name) \
        found &= (gl.name = reinterpret_cast<decltype(gl.name)>(api.SDL_GL_GetProcAddress(#name))) != nullptr;
        LS3D_GL_11(LS3D_GL_LOAD)
#undef LS3D_GL_LOAD
#define LS3D_GL_LOAD(type, name) \
        found &= (gl.name = reinterpret_cast<type>(api.SDL_GL_GetProcAddress(#name))) != nullptr;
        LS3D_GL_LATER(LS3D_GL_LOAD)
#undef LS3D_GL_LOAD
        if (!found) {
            EnhancementLog("LS3DF.ini OpenGL: the driver lacks OpenGL 3.3's functions");
            ReleaseDevice();
            return false;
        }
        gl.glDebugMessageCallback = reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(api.SDL_GL_GetProcAddress("glDebugMessageCallback"));
        if (Enhancements().sdlGpuDebug && gl.glDebugMessageCallback) {
            gl.glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            gl.glDebugMessageCallback(DebugMessage, nullptr);
        }
        // The extensions used.
        GLint extensions = 0;
        gl.glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint i = 0; i < extensions; ++i) {
            const char *name = reinterpret_cast<const char *>(gl.glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (!std::strcmp(name, "GL_EXT_texture_compression_s3tc"))
                s3tc = true;
            else if (!std::strcmp(name, "GL_EXT_texture_filter_anisotropic") || !std::strcmp(name, "GL_ARB_texture_filter_anisotropic"))
                anisotropic = true;
        }
        if (!MakeProgram()) {
            ReleaseDevice();
            return false;
        }
        gl.glGenVertexArrays(1, &vao);
        gl.glBindVertexArray(vao);
        gl.glGenBuffers(1, &vertexBuffer);
        gl.glGenBuffers(1, &indexBuffer);
        gl.glGenBuffers(1, &uniformBuffer);
        gl.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        gl.glBindBuffer(GL_UNIFORM_BUFFER, uniformBuffer);
        gl.glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniformAlignment);
        // The vertex inputs a format lacks: normal 0, diffuse white, specular 0, coordinates 0.
        gl.glVertexAttrib4f(1, 0, 0, 0, 1);
        gl.glVertexAttrib4f(2, 1, 1, 1, 1);
        gl.glVertexAttrib4f(3, 0, 0, 0, 0);
        gl.glVertexAttrib4f(4, 0, 0, 0, 1);
        gl.glVertexAttrib4f(5, 0, 0, 0, 1);
        // A stage without a texture reads white.
        gl.glGenTextures(1, &whiteTexture);
        gl.glBindTexture(GL_TEXTURE_2D, whiteTexture);
        const uint8_t white[4] = {255, 255, 255, 255};
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.glFrontFace(GL_CCW);     // (Direct3D's clockwise, turned over: the module's comment)
        const char *version = reinterpret_cast<const char *>(gl.glGetString(GL_VERSION));
        EnhancementLog("LS3DF.ini OpenGL: drawing with OpenGL %s", version ? version : "?");
        Trace("device: OpenGL %s", version ? version : "?");
        return true;
    }

    void ReleaseDevice() override
    {
        if (context) {
            for (auto &s : samplers)
                gl.glDeleteSamplers(1, &s.second);
            samplers.clear();
            ReleaseTargets();
            for (GLuint *b : {&vertexBuffer, &indexBuffer, &uniformBuffer})
                if (*b)
                    gl.glDeleteBuffers(1, b), *b = 0;
            if (vao)
                gl.glDeleteVertexArrays(1, &vao), vao = 0;
            if (whiteTexture)
                gl.glDeleteTextures(1, &whiteTexture), whiteTexture = 0;
            if (program)
                gl.glDeleteProgram(program), program = 0;
            api.SDL_GL_DestroyContext(context);
            context = nullptr;
        }
        if (ownWindow)
            api.SDL_DestroyWindow(window);
        window = nullptr;
        ownWindow = false;
        s3tc = anisotropic = false;
    }

    bool MakeTargets(const D3DPRESENT_PARAMETERS &p) override
    {
        present = p;
        FitWindow(window, p);
        const GLsizei w = static_cast<GLsizei>(p.BackBufferWidth), h = static_cast<GLsizei>(p.BackBufferHeight);
        gl.glGenFramebuffers(1, &framebuffer);
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl.glActiveTexture(GL_TEXTURE0);
        gl.glGenTextures(1, &colorTarget);
        gl.glBindTexture(GL_TEXTURE_2D, colorTarget);
        gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget, 0);
        if (p.EnableAutoDepthStencil) {
            gl.glGenRenderbuffers(1, &depthTarget);
            gl.glBindRenderbuffer(GL_RENDERBUFFER, depthTarget);
            gl.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            gl.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthTarget);
        }
        if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            EnhancementLog("LS3DF.ini OpenGL: the back buffer cannot be made");
            return false;
        }
        gl.glDisable(GL_SCISSOR_TEST);
        gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl.glDepthMask(GL_TRUE);
        gl.glClearColor(0, 0, 0, 0);
        gl.glClearDepth(1);
        gl.glClear(GL_COLOR_BUFFER_BIT | (p.EnableAutoDepthStencil ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT : 0));
        // (OpenGL has no mailbox: without waiting, the driver and the compositor decide.)
        const E_presentMode mode = ChoosePresentMode(p);
        api.SDL_GL_SetSwapInterval(mode == PRESENT_VSYNC ? 1 : 0);
        Trace("present mode: %s", mode == PRESENT_VSYNC ? "vsync" : "no waiting");
        return true;
    }
    void ReleaseTargets() override
    {
        if (framebuffer)
            gl.glDeleteFramebuffers(1, &framebuffer), framebuffer = 0;
        if (colorTarget)
            gl.glDeleteTextures(1, &colorTarget), colorTarget = 0;
        if (depthTarget)
            gl.glDeleteRenderbuffers(1, &depthTarget), depthTarget = 0;
    }
    void WaitIdle() override { gl.glFinish(); }
    bool SupportsFormat(E_gpuFormat f) override { return (f != GF_BC1 && f != GF_BC2 && f != GF_BC3) || s3tc; }
    void ReleaseTexture(void *texture) override
    {
        const GLuint id = Id(texture);
        gl.glDeleteTextures(1, &id);
    }

    void UploadTexture(S_texture *t) override
    {
        bool convert;
        const E_gpuFormat format = GpuFormat(t->format, &convert);
        gl.glActiveTexture(GL_TEXTURE0);
        if (!t->gpu) {
            GLuint id = 0;
            gl.glGenTextures(1, &id);
            t->gpu = Handle(id);
            gl.glBindTexture(GL_TEXTURE_2D, id);
            gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(t->levels) - 1);
            Trace("texture %ux%u, %u levels, D3D format %u -> %d", t->width, t->height, t->levels,
                  static_cast<unsigned>(t->format), static_cast<int>(format));
        } else {
            gl.glBindTexture(GL_TEXTURE_2D, Id(t->gpu));
        }
        std::vector<uint8_t> scratch;
        for (UINT l = 0; l < t->levels; ++l) {
            const S_surface &s = t->surfaces[l];
            const std::vector<uint8_t> &bytes = LevelBytes(*t, l, &scratch);
            const GLsizei w = static_cast<GLsizei>(s.width), h = static_cast<GLsizei>(s.height);
            const GLint level = static_cast<GLint>(l);
            switch (format) {
            case GF_BC1:
            case GF_BC2:
            case GF_BC3:
                gl.glCompressedTexImage2D(GL_TEXTURE_2D, level,
                                          format == GF_BC1   ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                                          : format == GF_BC2 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                                                             : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                                          w, h, 0, static_cast<GLsizei>(bytes.size()), bytes.data());
                break;
            case GF_RG8_SNORM:
                gl.glTexImage2D(GL_TEXTURE_2D, level, GL_RG8_SNORM, w, h, 0, GL_RG, GL_BYTE, bytes.data());
                break;
            default:
                gl.glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bytes.data());
                break;
            }
            stats->uploadBytes += bytes.size();
        }
        stats->uploadBatches += 1;
    }

    bool Run(const S_frame &frame) override
    {
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        gl.glUseProgram(program);
        gl.glEnable(GL_SCISSOR_TEST);
        gl.glDepthRange(0, 1);
        // The frame's vertices, indices and uniform blocks (each its own range of one buffer).
        gl.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(frame.vertices.size()), frame.vertices.data(), GL_STREAM_DRAW);
        gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(frame.indices.size() * 2), frame.indices.data(), GL_STREAM_DRAW);
        const auto align = [&](size_t n) { return (n + uniformAlignment - 1) / uniformAlignment * uniformAlignment; };
        const size_t vertexStride = align(sizeof(S_vertexUniforms)), pixelStride = align(sizeof(S_pixelUniforms));
        const size_t pixelStart = vertexStride * frame.vertexBlocks.size();
        uniformBytes.resize(pixelStart + pixelStride * frame.pixelBlocks.size());
        for (size_t i = 0; i < frame.vertexBlocks.size(); ++i)
            std::memcpy(uniformBytes.data() + i * vertexStride, &frame.vertexBlocks[i], sizeof(S_vertexUniforms));
        for (size_t i = 0; i < frame.pixelBlocks.size(); ++i)
            std::memcpy(uniformBytes.data() + pixelStart + i * pixelStride, &frame.pixelBlocks[i], sizeof(S_pixelUniforms));
        gl.glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(uniformBytes.size()), uniformBytes.data(), GL_STREAM_DRAW);

        bool haveKey = false;
        S_pipelineKey bound = {};
        const S_layout *boundLayout = nullptr;
        S_viewport boundViewport = {-1, -1, -1, -1, -1, -1};
        S_scissor boundScissor = {-1, -1, -1, -1};
        uint32_t boundVertexUniforms = ~0u, boundPixelUniforms = ~0u;
        GLuint boundTextures[kStages] = {~0u, ~0u, ~0u}, boundSamplers[kStages] = {~0u, ~0u, ~0u};
        for (const S_command &c : frame.commands) {
            if (!haveKey || !(c.key == bound)) {
                ApplyKey(c.key);
                bound = c.key;
                haveKey = true;
            }
            if (std::memcmp(&c.viewport, &boundViewport, sizeof boundViewport) != 0) {
                gl.glViewport(static_cast<GLint>(c.viewport.x), static_cast<GLint>(c.viewport.y),
                              static_cast<GLsizei>(c.viewport.w), static_cast<GLsizei>(c.viewport.h));
                gl.glDepthRange(c.viewport.minZ, c.viewport.maxZ);
                boundViewport = c.viewport;
            }
            if (std::memcmp(&c.scissor, &boundScissor, sizeof boundScissor) != 0) {
                gl.glScissor(c.scissor.x, c.scissor.y, c.scissor.w, c.scissor.h);
                boundScissor = c.scissor;
            }
            for (int s = 0; s < kStages; ++s) {
                const GLuint texture = c.textures[s] && c.textures[s]->gpu ? Id(c.textures[s]->gpu) : whiteTexture;
                if (texture != boundTextures[s]) {
                    gl.glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(s));
                    gl.glBindTexture(GL_TEXTURE_2D, texture);
                    boundTextures[s] = texture;
                }
                const GLuint sampler = Sampler(c.samplers[s]);
                if (sampler != boundSamplers[s]) {
                    gl.glBindSampler(static_cast<GLuint>(s), sampler);
                    boundSamplers[s] = sampler;
                }
            }
            if (c.vertexUniforms != boundVertexUniforms) {
                gl.glBindBufferRange(GL_UNIFORM_BUFFER, 0, uniformBuffer, static_cast<GLintptr>(c.vertexUniforms * vertexStride),
                                     sizeof(S_vertexUniforms));
                boundVertexUniforms = c.vertexUniforms;
            }
            if (c.pixelUniforms != boundPixelUniforms) {
                gl.glBindBufferRange(GL_UNIFORM_BUFFER, 1, uniformBuffer, static_cast<GLintptr>(pixelStart + c.pixelUniforms * pixelStride),
                                     sizeof(S_pixelUniforms));
                boundPixelUniforms = c.pixelUniforms;
            }
            SetAttributes(*c.layout, c.key.stride, c.vertexOffset, c.layout != boundLayout);
            boundLayout = c.layout;
            if (c.indexed)
                gl.glDrawElements(Primitive(c.key.primitive), static_cast<GLsizei>(c.count), GL_UNSIGNED_SHORT,
                                  reinterpret_cast<const void *>(static_cast<uintptr_t>(c.indexOffset) * 2));
            else
                gl.glDrawArrays(Primitive(c.key.primitive), 0, static_cast<GLsizei>(c.count));
        }
        return true;
    }

    void Present() override
    {
        int w = 0, h = 0;
        api.SDL_GetWindowSizeInPixels(window, &w, &h);
        gl.glDisable(GL_SCISSOR_TEST);
        gl.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl.glClearColor(0, 0, 0, 1);            // (black bars, a picture's shape kept)
        gl.glClear(GL_COLOR_BUFFER_BIT);
        const S_rectangle picture = PictureRectangle(w, h, present.BackBufferWidth, present.BackBufferHeight);
        gl.glBlitFramebuffer(0, 0, static_cast<GLint>(present.BackBufferWidth), static_cast<GLint>(present.BackBufferHeight),
                             picture.x, h - picture.y, picture.x + picture.w, h - picture.y - picture.h, GL_COLOR_BUFFER_BIT,
                             GL_LINEAR);
        const auto start = std::chrono::steady_clock::now();
        api.SDL_GL_SwapWindow(window);
        stats->acquire += std::chrono::steady_clock::now() - start;
        gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    }

    bool ReadBack(std::vector<uint8_t> *rows) override
    {
        rows->resize(static_cast<size_t>(present.BackBufferWidth) * present.BackBufferHeight * 4);
        gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        gl.glReadBuffer(GL_COLOR_ATTACHMENT0);
        gl.glReadPixels(0, 0, static_cast<GLsizei>(present.BackBufferWidth), static_cast<GLsizei>(present.BackBufferHeight),
                        GL_BGRA, GL_UNSIGNED_BYTE, rows->data());
        return true;
    }

private:
    bool MakeProgram()
    {
        const auto compile = [&](GLenum stage, const char *defines) -> GLuint {
            const GLuint shader = gl.glCreateShader(stage);
            const char *sources[2] = {defines, kFixedFunctionSource};
            gl.glShaderSource(shader, 2, sources, nullptr);
            gl.glCompileShader(shader);
            GLint ok = 0;
            gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[2048] = {};
                gl.glGetShaderInfoLog(shader, sizeof log, nullptr, log);
                EnhancementLog("LS3DF.ini OpenGL: a shader does not compile: %s", log);
                Trace("shader: %s", log);
                gl.glDeleteShader(shader);
                return 0;
            }
            return shader;
        };
        const GLuint vs = compile(GL_VERTEX_SHADER, "#version 330 core\n#define GLSL\n#define VERTEX_STAGE\n");
        const GLuint ps = compile(GL_FRAGMENT_SHADER, "#version 330 core\n#define GLSL\n#define PIXEL_STAGE\n");
        if (!vs || !ps)
            return false;
        program = gl.glCreateProgram();
        gl.glAttachShader(program, vs);
        gl.glAttachShader(program, ps);
        gl.glLinkProgram(program);
        gl.glDeleteShader(vs);
        gl.glDeleteShader(ps);
        GLint ok = 0;
        gl.glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            gl.glGetProgramInfoLog(program, sizeof log, nullptr, log);
            EnhancementLog("LS3DF.ini OpenGL: the shaders do not link: %s", log);
            Trace("program: %s", log);
            return false;
        }
        gl.glUseProgram(program);
        gl.glUniformBlockBinding(program, gl.glGetUniformBlockIndex(program, "VertexUniforms"), 0);
        gl.glUniformBlockBinding(program, gl.glGetUniformBlockIndex(program, "PixelUniforms"), 1);
        gl.glUniform1i(gl.glGetUniformLocation(program, "texture0"), 0);
        gl.glUniform1i(gl.glGetUniformLocation(program, "texture1"), 1);
        gl.glUniform1i(gl.glGetUniformLocation(program, "texture2"), 2);
        return true;
    }

    void ApplyKey(const S_pipelineKey &key)
    {
        if (key.blend) {
            gl.glEnable(GL_BLEND);
            gl.glBlendFunc(BlendFactor(key.srcBlend), BlendFactor(key.dstBlend));
        } else {
            gl.glDisable(GL_BLEND);
        }
        gl.glColorMask((key.writeMask & 1) != 0, (key.writeMask & 2) != 0, (key.writeMask & 4) != 0, (key.writeMask & 8) != 0);
        // (OpenGL writes depth only with its test on, as a Direct3D device with ZENABLE does.)
        if (key.depthTest)
            gl.glEnable(GL_DEPTH_TEST);
        else
            gl.glDisable(GL_DEPTH_TEST);
        gl.glDepthMask(key.depthWrite != 0);
        gl.glDepthFunc(GL_NEVER + (key.depthFunc - D3DCMP_NEVER));
        if (key.cull == D3DCULL_NONE) {
            gl.glDisable(GL_CULL_FACE);
        } else {
            gl.glEnable(GL_CULL_FACE);
            gl.glCullFace(key.cull == D3DCULL_CCW ? GL_BACK : GL_FRONT);
        }
        gl.glPolygonMode(GL_FRONT_AND_BACK, key.fill == D3DFILL_WIREFRAME ? GL_LINE : GL_FILL);
        if (key.zbias) {
            // Direct3D 8's ZBIAS (0 - 16): nearer by that many depth-buffer steps.
            gl.glEnable(GL_POLYGON_OFFSET_FILL);
            gl.glEnable(GL_POLYGON_OFFSET_LINE);
            gl.glPolygonOffset(0, -static_cast<float>(key.zbias));
        } else {
            gl.glDisable(GL_POLYGON_OFFSET_FILL);
            gl.glDisable(GL_POLYGON_OFFSET_LINE);
        }
    }

    // The vertex format at this draw's start in the frame's vertices.
    void SetAttributes(const S_layout &layout, uint32_t stride, uint32_t start, bool newLayout)
    {
        if (newLayout) {
            for (GLuint i = 0; i < 6; ++i)
                gl.glDisableVertexAttribArray(i);
            for (const S_layout::S_attribute &a : layout.attributes)
                gl.glEnableVertexAttribArray(a.location);
        }
        for (const S_layout::S_attribute &a : layout.attributes) {
            GLint size = 3;
            GLenum type = GL_FLOAT;
            GLboolean normalized = GL_FALSE;
            switch (a.format) {
            case EL_FLOAT1: size = 1; break;
            case EL_FLOAT2: size = 2; break;
            case EL_FLOAT3: size = 3; break;
            case EL_FLOAT4: size = 4; break;
            case EL_COLOR: size = 4, type = GL_UNSIGNED_BYTE, normalized = GL_TRUE; break;
            case EL_UBYTE4: size = 4, type = GL_UNSIGNED_BYTE; break;
            case EL_SHORT2: size = 2, type = GL_SHORT; break;
            case EL_SHORT4: size = 4, type = GL_SHORT; break;
            }
            gl.glVertexAttribPointer(a.location, size, type, normalized, static_cast<GLsizei>(stride),
                                     reinterpret_cast<const void *>(static_cast<uintptr_t>(start) + a.offset));
        }
    }

    GLuint Sampler(const S_samplerKey &key)
    {
        auto s = samplers.find(key);
        if (s != samplers.end())
            return s->second;
        GLuint sampler = 0;
        gl.glGenSamplers(1, &sampler);
        const bool minLinear = key.minFilter >= D3DTEXF_LINEAR;
        const GLint minFilter = key.mipFilter == D3DTEXF_NONE    ? (minLinear ? GL_LINEAR : GL_NEAREST)
                                : key.mipFilter == D3DTEXF_LINEAR ? (minLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR)
                                                                  : (minLinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST);
        gl.glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
        gl.glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, key.magFilter >= D3DTEXF_LINEAR ? GL_LINEAR : GL_NEAREST);
        gl.glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, Address(key.addressU));
        gl.glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, Address(key.addressV));
        gl.glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, key.lodBias / 256.0f);
        if (anisotropic && (key.minFilter == D3DTEXF_ANISOTROPIC || key.magFilter == D3DTEXF_ANISOTROPIC) && key.anisotropy > 1)
            gl.glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY, static_cast<float>(key.anisotropy));
        samplers[key] = sampler;
        return sampler;
    }

    SDL_Window *window = nullptr;
    bool ownWindow = false;
    SDL_GLContext context = nullptr;
    D3DPRESENT_PARAMETERS present = {};
    bool s3tc = false, anisotropic = false;
    GLuint program = 0, vao = 0, vertexBuffer = 0, indexBuffer = 0, uniformBuffer = 0, whiteTexture = 0;
    GLuint framebuffer = 0, colorTarget = 0, depthTarget = 0;
    GLint uniformAlignment = 256;
    std::vector<uint8_t> uniformBytes;
    std::map<S_samplerKey, GLuint> samplers;
};

}  // namespace

Device *OpenGlDevice()
{
    static Device *device;
    if (!device) {
        static RendererOpenGL renderer;
        device = fixed::MakeDevice(&renderer);
    }
    return device;
}

}  // namespace ls3d::gfx
