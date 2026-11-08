// SDL3 for the platform's SDL3 implementation (platform/sdl3), the SDL3 input device (input/sdl3)
// and the SDL_GPU render device (gfx/sdlgpu): SDL's functions, loaded at run time from SDL3.dll, and
// what they share.
#pragma once

#include <cstdint>

#include <SDL3/SDL.h>

namespace ls3d::sdl3 {

// The functions the backends call.
#define LS3D_SDL_FUNCTIONS(X)                                                                                \
    X(SDL_Init)                                                                                          \
    X(SDL_SetHint)                                                                                       \
    X(SDL_CreateWindow)                                                                                  \
    X(SDL_GetWindowProperties)                                                                           \
    X(SDL_GetPointerProperty)                                                                            \
    X(SDL_GetWindowID)                                                                                   \
    X(SDL_SetWindowPosition)                                                                             \
    X(SDL_GetWindowPosition)                                                                             \
    X(SDL_SetWindowSize)                                                                                 \
    X(SDL_ShowWindow)                                                                                    \
    X(SDL_SyncWindow)                                                                                    \
    X(SDL_SetWindowFullscreen)                                                                           \
    X(SDL_SetWindowFullscreenMode)                                                                       \
    X(SDL_MinimizeWindow)                                                                                \
    X(SDL_SetWindowTitle)                                                                                \
    X(SDL_RaiseWindow)                                                                                   \
    X(SDL_GetWindowFlags)                                                                                \
    X(SDL_PollEvent)                                                                                     \
    X(SDL_WaitEvent)                                                                                     \
    X(SDL_StartTextInput)                                                                                \
    X(SDL_WarpMouseGlobal)                                                                               \
    X(SDL_SetWindowMouseRect)                                                                            \
    X(SDL_SetWindowRelativeMouseMode)                                                                    \
    X(SDL_GetKeyboardFocus)                                                                              \
    X(SDL_GetKeyboardState)                                                                              \
    X(SDL_GetMouseState)                                                                                 \
    X(SDL_GetRelativeMouseState)                                                                         \
    X(SDL_GetScancodeFromKey)                                                                            \
    X(SDL_GetKeyFromScancode)                                                                            \
    X(SDL_GetKeyName)                                                                                    \
    X(SDL_GetTicks)                                                                                      \
    X(SDL_Delay)                                                                                         \
    X(SDL_SetWindowsMessageHook)

// The render device's (SDL_GPU; loaded by StartGpu).
#define LS3D_SDL_GPU_FUNCTIONS(X)                                                                            \
    X(SDL_GetError)                                                                                      \
    X(SDL_free)                                                                                          \
    X(SDL_CreateProperties)                                                                              \
    X(SDL_DestroyProperties)                                                                             \
    X(SDL_SetPointerProperty)                                                                            \
    X(SDL_SetBooleanProperty)                                                                            \
    X(SDL_GetWindowSizeInPixels)                                                                         \
    X(SDL_GL_SetAttribute)                                                                               \
    X(SDL_GL_CreateContext)                                                                              \
    X(SDL_GL_DestroyContext)                                                                             \
    X(SDL_GL_MakeCurrent)                                                                                \
    X(SDL_GL_GetProcAddress)                                                                             \
    X(SDL_GL_SetSwapInterval)                                                                            \
    X(SDL_GL_SwapWindow)                                                                                 \
    X(SDL_CreateWindowWithProperties)                                                                    \
    X(SDL_DestroyWindow)                                                                                 \
    X(SDL_GetWindowFromID)                                                                               \
    X(SDL_GetPrimaryDisplay)                                                                             \
    X(SDL_GetDesktopDisplayMode)                                                                         \
    X(SDL_GetFullscreenDisplayModes)                                                                     \
    X(SDL_CreateGPUDevice)                                                                               \
    X(SDL_DestroyGPUDevice)                                                                              \
    X(SDL_GetGPUDeviceDriver)                                                                            \
    X(SDL_GetGPUShaderFormats)                                                                           \
    X(SDL_ClaimWindowForGPUDevice)                                                                       \
    X(SDL_ReleaseWindowFromGPUDevice)                                                                    \
    X(SDL_WindowSupportsGPUPresentMode)                                                                  \
    X(SDL_SetGPUSwapchainParameters)                                                                     \
    X(SDL_GPUTextureSupportsFormat)                                                                      \
    X(SDL_CreateGPUShader)                                                                               \
    X(SDL_ReleaseGPUShader)                                                                              \
    X(SDL_CreateGPUGraphicsPipeline)                                                                     \
    X(SDL_ReleaseGPUGraphicsPipeline)                                                                    \
    X(SDL_CreateGPUTexture)                                                                              \
    X(SDL_ReleaseGPUTexture)                                                                             \
    X(SDL_CreateGPUSampler)                                                                              \
    X(SDL_ReleaseGPUSampler)                                                                             \
    X(SDL_CreateGPUBuffer)                                                                               \
    X(SDL_ReleaseGPUBuffer)                                                                              \
    X(SDL_CreateGPUTransferBuffer)                                                                       \
    X(SDL_ReleaseGPUTransferBuffer)                                                                      \
    X(SDL_MapGPUTransferBuffer)                                                                          \
    X(SDL_UnmapGPUTransferBuffer)                                                                        \
    X(SDL_AcquireGPUCommandBuffer)                                                                       \
    X(SDL_SubmitGPUCommandBuffer)                                                                        \
    X(SDL_SubmitGPUCommandBufferAndAcquireFence)                                                         \
    X(SDL_WaitForGPUFences)                                                                              \
    X(SDL_ReleaseGPUFence)                                                                               \
    X(SDL_WaitForGPUIdle)                                                                                \
    X(SDL_BeginGPUCopyPass)                                                                              \
    X(SDL_UploadToGPUTexture)                                                                            \
    X(SDL_UploadToGPUBuffer)                                                                             \
    X(SDL_DownloadFromGPUTexture)                                                                        \
    X(SDL_EndGPUCopyPass)                                                                                \
    X(SDL_BeginGPURenderPass)                                                                            \
    X(SDL_EndGPURenderPass)                                                                              \
    X(SDL_BindGPUGraphicsPipeline)                                                                       \
    X(SDL_SetGPUViewport)                                                                                \
    X(SDL_SetGPUScissor)                                                                                 \
    X(SDL_BindGPUVertexBuffers)                                                                          \
    X(SDL_BindGPUIndexBuffer)                                                                            \
    X(SDL_BindGPUFragmentSamplers)                                                                       \
    X(SDL_PushGPUVertexUniformData)                                                                      \
    X(SDL_PushGPUFragmentUniformData)                                                                    \
    X(SDL_DrawGPUIndexedPrimitives)                                                                      \
    X(SDL_DrawGPUPrimitives)                                                                             \
    X(SDL_WaitAndAcquireGPUSwapchainTexture)                                                             \
    X(SDL_BlitGPUTexture)

struct S_api {
#define LS3D_SDL_MEMBER(name) decltype(&::name) name;
    LS3D_SDL_FUNCTIONS(LS3D_SDL_MEMBER)
    LS3D_SDL_GPU_FUNCTIONS(LS3D_SDL_MEMBER)
#undef LS3D_SDL_MEMBER
};
extern S_api api;

// Loads SDL3.dll and starts SDL's video (once; on the thread that will own the window). False when
// either fails.
bool Start();
// Start, and SDL_GPU's functions too (SDL 3.2 or later). False when one is missing.
bool StartGpu();

// The engine's main window (null before it is created).
extern SDL_Window *g_window;
// Wheel notches since the input device last read them (the platform's message loop adds them).
extern float g_wheel;

// DirectInput's key code (DIK_*: the PC set-1 scan code, 0x80 for the E0 keys) of an SDL scancode,
// 0 when DirectInput has none; and back (SDL_SCANCODE_UNKNOWN for none).
uint8_t DikOfScancode(SDL_Scancode scancode);
SDL_Scancode ScancodeOfDik(uint8_t dik);

}  // namespace ls3d::sdl3
