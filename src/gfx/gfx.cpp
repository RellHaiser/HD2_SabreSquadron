// The render device's choice (gfx/gfx.h): the Direct3D 8 backend, behind the frame recorder when
// LS3DF.ini asks for it ([Render] CaptureFrames).
#include "gfx/gfx.h"

#include "core/enhance.h"
#include "gfx/trace/recorder.h"

namespace ls3d::gfx {

void ChooseDevice()
{
    static bool chosen;
    if (chosen)
        return;
    chosen = true;
    if (Enhancements().captureFrames) {
        g_pGfx = FrameRecorder(g_pGfx);
        EnhancementLog("LS3DF.ini CaptureFrames: Ctrl+Shift+F11 saves the next frame to LS3DF_frames\\");
    }
}

}  // namespace ls3d::gfx
