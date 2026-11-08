// The OpenGL 3.3 render device (gfx/opengl/gfx_opengl.cpp): Direct3D 8's fixed-function model on
// OpenGL. Only the device's choice (gfx/gfx.cpp) and the render tests include this.
#pragma once

#include "gfx/gfx.h"

namespace ls3d::gfx {

// The device (its Open fails when SDL3.dll, 3.2 or later, cannot be loaded).
Device *OpenGlDevice();

}  // namespace ls3d::gfx
