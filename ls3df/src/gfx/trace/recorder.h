// The frame recorder: a render device in front of `inner` that passes every call on and saves one
// frame of them to LS3DF_frames\frame_<n>.ls3dframe next to the DLL when Ctrl+Shift+F11 is pressed
// (gfx/trace/trace_format.h). LS3DF.ini [Render] CaptureFrames=1 puts it in (gfx::ChooseDevice).
#pragma once

#include "gfx/gfx.h"

namespace ls3d::gfx {

Device *FrameRecorder(Device *inner);
// The frame after the next Present is saved as if Ctrl+Shift+F11 had been pressed (tests); the
// file's path is returned once written (empty until then).
void RequestFrameCapture();
const char *LastFrameCapture();

}  // namespace ls3d::gfx
