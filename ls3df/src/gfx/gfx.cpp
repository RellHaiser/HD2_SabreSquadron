// The render device's choice (gfx/gfx.h): the Direct3D 8 backend, or the SDL_GPU one ([Render]
// SDLGPU) or the OpenGL 3.3 one ([Render] OpenGL), behind the frame recorder when LS3DF.ini asks for it ([Render] CaptureFrames).
#include "gfx/gfx.h"

#include "core/enhance.h"
#include "gfx/opengl/opengl_device.h"
#include "gfx/sdlgpu/sdlgpu_device.h"
#include "gfx/trace/recorder.h"

namespace ls3d::gfx {

void ChooseDevice()
{
    static bool chosen;
    if (chosen)
        return;
    chosen = true;
    if (Enhancements().sdlGpu) {
        Device *sdlGpu = SdlGpuDevice();
        if (sdlGpu->Open()) {
            g_pGfx = sdlGpu;
            EnhancementLog("LS3DF.ini SDLGPU: drawing through SDL_GPU");
        } else {
            EnhancementLog("LS3DF.ini SDLGPU: SDL3.dll (3.2 or later) not found, drawing with Direct3D 8");
        }
    }
    if (Enhancements().openGl && g_pGfx != SdlGpuDevice()) {
        Device *openGl = OpenGlDevice();
        if (openGl->Open()) {
            g_pGfx = openGl;
            EnhancementLog("LS3DF.ini OpenGL: drawing through OpenGL 3.3");
        } else {
            EnhancementLog("LS3DF.ini OpenGL: SDL3.dll (3.2 or later) not found, drawing with Direct3D 8");
        }
    }
    if (Enhancements().captureFrames) {
        g_pGfx = FrameRecorder(g_pGfx);
        EnhancementLog("LS3DF.ini CaptureFrames: Ctrl+Shift+F11 saves the next frame to LS3DF_frames\\");
    }
}

}  // namespace ls3d::gfx
