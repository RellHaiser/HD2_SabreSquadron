// Port enhancements: options the original engine does not have, read from LS3DF.ini next to the
// DLL. Each one is off when the file or its key is missing; the engine then works as the original
// (the compare_* tests run that way).
//
//   [Graphics]
//   AnisotropicFiltering=16    ; 2 - 16: anisotropic texture filtering (capped by the device); 0: off
//   TrilinearFiltering=1       ; mip levels always blended (driver state 2 raised to linear)
//   ImprovedMipmaps=1          ; gamma-correct, alpha-weighted mip maps; colour-keyed textures keep
//                              ; their alpha-tested coverage at every level
//   TrueColorTextures=1        ; textures from image files in A8R8G8B8 (not 16-bit or DXT)
//   TrueColorLightmaps=1       ; lightmap atlases in 32-bit whatever driver state 0x22 says
//   NearPortalDistance=100     ; centimetres: a camera this close to a portal of its sector that is
//                              ; behind it still draws the sector beyond (a gun's camera just
//                              ; outside a bunker's embrasure sees the gun's sight); 0: off
//
//   [Sound]
//   OpenAL=1                   ; sound through OpenAL Soft (soft_oal.dll next to the DLL) instead of
//                              ; DirectSound (audio/openal); DirectSound when the library is missing
//
//   [Video]
//   FFmpeg=1                   ; movies and videos decoded by FFmpeg (avcodec-61.dll ... next to the
//                              ; DLL) instead of Bink and DirectShow (video/ffmpeg); the movies' sound
//                              ; through the sound device; Bink and DirectShow when it is missing
//
//   [Platform]
//   SDL3=1                     ; the window, its keyboard and mouse and the clock through SDL3
//                              ; (SDL3.dll next to the DLL; platform/sdl3, input/sdl3) instead of
//                              ; Win32 and DirectInput; Win32 and DirectInput when it is missing
//
//   [Render]
//   CaptureFrames=1            ; Ctrl+Shift+F11 saves the next frame's drawing to LS3DF_frames\ next
//                              ; to the DLL, for the render tests to replay (gfx/trace)
//   SDLGPU=1                   ; drawing through SDL_GPU (SDL3.dll next to the DLL; gfx/sdlgpu)
//                              ; instead of Direct3D 8; Direct3D 8 when SDL3 is missing
//   SDLGPUDriver=vulkan        ; SDL_GPU's API: direct3d12 or vulkan (empty: SDL's choice)
//   PresentMode=vsync          ; how SDL_GPU and OpenGL show frames: vsync, mailbox or immediate
//                              ; (empty: what the game asks for)
//   MSAA=4                     ; SDL_GPU and OpenGL: that many samples a pixel (2, 4 or 8), at least
//   AlphaToCoverage=1          ; SDL_GPU and OpenGL with MSAA: alpha-tested 3D edges smoothed too
//   AmbientOcclusion=1         ; SDL_GPU and OpenGL: darkening where the 3D scene's surfaces meet
//   AmbientOcclusionStrength=75   ; how dark, in percent (default 75)
//   AmbientOcclusionRadius=60  ; how far it reaches, in centimetres (default 60)
//   OpenGL=1                   ; drawing through OpenGL 3.3 (SDL3.dll next to the DLL; gfx/opengl)
//                              ; instead of Direct3D 8; Direct3D 8 when SDL3 is missing
//   SDLGPUDebug=1              ; the API's validation (the Vulkan SDK's layers, Direct3D 12's debug
//                              ; layer, OpenGL's debug output), and what is asked of it in
//                              ; LS3DF_render.log
//   SDLGPUStats=1              ; what the frames cost, every 5 seconds, in LS3DF_render.log
#pragma once

namespace ls3d {

struct S_enhancements {
    int anisotropy;                         // AnisotropicFiltering (0: off, else 2 - 16)
    bool trilinear;                         // TrilinearFiltering
    bool mipmaps;                           // ImprovedMipmaps
    bool colorKeyMipmaps;                   // ColorKeyMipmaps (with ImprovedMipmaps)
    bool trueColorTextures;                 // TrueColorTextures
    bool trueColorLightmaps;                // TrueColorLightmaps
    float nearPortalDistance;               // NearPortalDistance, in metres (0: off)
    bool openal;                            // [Sound] OpenAL
    bool ffmpeg;                            // [Video] FFmpeg
    bool sdl3;                              // [Platform] SDL3
    bool captureFrames;                     // [Render] CaptureFrames
    bool sdlGpu;                            // [Render] SDLGPU
    bool openGl;                            // [Render] OpenGL
    char sdlGpuDriver[16];                  // [Render] SDLGPUDriver ("": SDL's choice)
    bool sdlGpuDebug;                       // [Render] SDLGPUDebug
    bool sdlGpuStats;                       // [Render] SDLGPUStats
    char presentMode[16];                   // [Render] PresentMode ("": what the game asks for)
    int msaa;                               // [Render] MSAA (0: what the game asks for)
    bool alphaToCoverage;                   // [Render] AlphaToCoverage
    bool occlusion;                         // [Render] AmbientOcclusion
    int occlusionStrength;                  // [Render] AmbientOcclusionStrength (percent)
    int occlusionRadius;                    // [Render] AmbientOcclusionRadius (centimetres)
};

// The options (LS3DF.ini is read on the first call).
const S_enhancements &Enhancements();
// Replaces the options (tests, or an application that embeds the engine).
void SetEnhancements(const S_enhancements &options);
// A line in the engine log, written even after the driver turned the log off: the options'
// messages (what they changed) reach the log whether or not the game runs verbose.
void EnhancementLog(const char *format, ...);

}  // namespace ls3d
