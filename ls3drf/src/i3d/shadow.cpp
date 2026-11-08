// Projected shadows: I3D_shadow (0x1004e5d0 - 0x10050770). LS3DRF has no shadow visual and no
// shadow map rasterizer, so nothing here renders a shadow.
#include "i3d/shadow.h"

#include "i3d/i3d_hooks.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace ls3d::i3d {

C_vector<I3D_shadow *> g_shadows;

}  // namespace ls3d::i3d

// I3D_shadow --------------------------------------------------------------------------------------

// LS3DRF 0x1002ad90 (from LS3DF 0x1004e5d0): no shadow visual is made.
void I3D_shadow::Init()
{
    I3D_frame::Init();
    mapHeight = 0x40;
    mapWidth = 0x40;
    orthogonal = true;
    unknown124 = 0;
    scene = nullptr;
    lightSector = nullptr;
    localVertices = nullptr;
    worldVertices = nullptr;
    points = nullptr;
    numVertices = 0;
    faces = nullptr;
    numFaces = 0;
    casters = nullptr;
    numCasters = 0;
    texture = nullptr;
    depth = 8.0f;
    opacity = 0.117f;
    lightMatrix.Identity();
    intensity = 1.0f;
    maxHeight = 1.0f;
    mode = 0;
    range = 100.0f;
    color.x = 0.0f;
    color.y = 0.0f;
    color.z = 0.0f;
    casterStamp = -1;
    lightStamp = -1;
    // (w: an unset value in the original.)
    lightDir = {0.0f, 0.0f, 1.0f, 0.0f};
    shadowFlags = 0;
    g_shadows.PushBack(this);
}

// LS3DRF 0x1002af40 (from LS3DF 0x1004e7a0): no scene to leave and no visual to release.
void I3D_shadow::Uninit()
{
    Close();
    g_shadows.RemoveSwap(this);
    I3D_frame::Uninit();
}

// LS3DRF 0x1002afd0 (from LS3DF 0x1004e840): no shadow file is shared.
I3D_RESULT I3D_shadow::Duplicate(I3D_frame *source)
{
    if (!source || source == this)
        return I3DERR_INVALIDPARAMS;
    I3D_frame::Duplicate(source);
    Close();
    if (source->type != FRAME_SHADOW)
        return I3D_OK;
    auto *src = static_cast<I3D_shadow *>(source);
    maxHeight = src->maxHeight;
    depth = src->depth;
    mapWidth = src->mapWidth;
    mapHeight = src->mapHeight;
    range = src->range;
    SetOpacity(src->opacity);
    SetColor(src->color);
    SetMode(src->mode);
    SetOrthogonal(src->orthogonal);
    return I3D_OK;
}

void I3D_shadow::SetSource(I3D_frame *) {}

// LS3DRF 0x1002b0b0 (from LS3DF 0x1004ef20): the frame's LinkTo only; the casters stay as they were.
I3D_RESULT I3D_shadow::LinkTo(I3D_frame *newParent, uint32_t flags)
{
    return I3D_frame::LinkTo(newParent, flags);
}

// 0x1004ef50
void I3D_shadow::SetMode(int newMode)
{
    mode = newMode;
    shadowFlags = 0;
    lightStamp = -1;
}

void I3D_shadow::MatrixChanged() {}

// LS3DRF's shared empty stub 0x1002b0a0 (marked at sound UpdateSound; from LS3DF 0x1004f030): no
// shadow file, casters or map to free.
void I3D_shadow::Close() {}

// 0x1004f120. In LS3DRF the return-0 stub shared with sound Open: no file read, nothing closed.
I3D_RESULT I3D_shadow::Open(const char *, uint32_t) { return I3D_OK; }

// 0x1004f340
void I3D_shadow::SetDepth(float newDepth)
{
    if (newDepth > 0.0f)
        depth = newDepth;
    lightDir.x = 0.0f;
    lightDir.y = 0.0f;
    lightDir.z = 0.0f;
    shadowFlags = 0;
}

// 0x1004f380
I3D_RESULT I3D_shadow::SetMaxHeight(float height)
{
    if (!(height >= 0.0f) || !(height <= 300.0f))
        return I3DERR_INVALIDPARAMS;
    maxHeight = height;
    lightDir.x = 0.0f;
    lightDir.y = 0.0f;
    lightDir.z = 0.0f;
    lightStamp = -1;
    shadowFlags = 0;
    return I3D_OK;
}

// 0x1004f3f0
void I3D_shadow::SetColor(const S_vector4 &c)
{
    color = c;
    if (color.x < 0.0f)
        color.x = 0.0f;
    else if (color.x > 1.0f)
        color.x = 1.0f;
    if (color.y < 0.0f)
        color.y = 0.0f;
    else if (color.y > 1.0f)
        color.y = 1.0f;
    if (color.z < 0.0f)
        color.z = 0.0f;
    else if (color.z > 1.0f)
        color.z = 1.0f;
    shadowFlags = 0;
    lightStamp = -1;
}

// 0x1004f4c0
void I3D_shadow::SetOpacity(float value)
{
    opacity = value;
    if (value < 0.0f) {
        opacity = 0.0f;
        shadowFlags = 0;
        return;
    }
    shadowFlags = 0;
    if (value > 1.0f)
        opacity = 1.0f;
}

// 0x1004f520
void I3D_shadow::SetOrthogonal(bool on)
{
    orthogonal = on;
    lightStamp = -1;
    lightDir.x = 0.0f;
    lightDir.y = 0.0f;
    lightDir.z = 0.0f;
    shadowFlags = 0;
}

// LS3DRF 0x1002b380 (from LS3DF 0x1004f560): the map is not released (LS3DRF's shadow has none).
I3D_RESULT I3D_shadow::SetSize(int width, int height)
{
    if (width > 0x80 || height > 0x80)
        return I3DERR_INVALIDPARAMS;
    mapWidth = 4;
    while (mapWidth < width)
        mapWidth *= 2;
    // (The height follows the width too.)
    mapHeight = 4;
    while (mapHeight < width)
        mapHeight *= 2;
    lightStamp = -1;
    shadowFlags = 0;
    return I3D_OK;
}

namespace ls3d::i3d {

void ShadowSetSource(I3D_frame *shadow, I3D_frame *source) { static_cast<I3D_shadow *>(shadow)->SetSource(source); }

// LS3DRF 0x1002b0f0 (from LS3DF 0x1004efd0)
void DeleteFromAnyShadow(I3D_frame *frame)
{
    for (I3D_shadow *s : g_shadows) {
        for (int i = 0; i < s->numCasters; ++i) {
            if (s->casters[i].frame && s->casters[i].frame == frame) {
                frame->frameFlags &= ~FRMFLAGS_SHADOW_CASTER;
                s->casters[i].frame = nullptr;
            }
        }
    }
}

// LS3DRF 0x1000f9b0 (from LS3DF 0x100193e0): I3D_shadow's constructor (the vtable only) is called here.
I3D_frame *NewShadowFrame() { return new I3D_shadow; }

}  // namespace ls3d::i3d
