// The one piece of LS3DF's render state left (I3D_driver::Render's; LS3DRF draws nothing).
#pragma once

class I3D_camera;

namespace ls3d::i3d {

// The camera of the frame being rendered (0x100d127c; 0x10058874 in LS3DRF). Nothing sets it: only the
// scene's Close clears it, as LS3DRF's does.
extern I3D_camera *g_renderCamera;

}  // namespace ls3d::i3d
