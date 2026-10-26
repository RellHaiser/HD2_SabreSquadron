// Helpers the interface's panel files share (the original's free functions near the panels).
#pragma once

#include "ls3d/i3d.h"

// The texture of a visual's face group by its file name (null: none).
I3D_texture *FindTexture(I3D_frame *visual, const char *fileName);         // 0x004a89e0 (__fastcall)
// The material of a visual's face group by its texture's file name, made the visual's own (its
// mesh and the material duplicated first; null: none).
I3D_material *DuplicateMaterial(I3D_frame *visual, const char *fileName);  // 0x004a8a90 (__fastcall)

// The first face group's material of a visual.
inline I3D_material *FirstMaterial(I3D_frame *visual)
{
    return static_cast<I3D_visual *>(visual)->GetVisualMesh()->GetLevel(0)->FaceGroups()[0].material;
}
