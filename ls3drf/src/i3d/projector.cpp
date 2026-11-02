// I3D_projector (0x10043730 - 0x100462b0). LS3DRF's projector draws nothing: LS3DF's Prepare /
// Draw (0x10043fe0 / 0x10045dd0), the projected geometry (0x10043d80, 0x100441c0, 0x10045860,
// 0x10044190) and its pending list (0x10043f90 / 0x10043fc0, static initialisers 0x100461b0 -
// 0x100462a0) are not ported.
#include "i3d/projector.h"

#include "external/rw_data.h"
#include "i3d/loader.h"
#include "i3d/material.h"
#include "i3d/mesh.h"
#include "i3d/object.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace {

float Clamp01(float f)
{
    if (f > 1.0f)
        return 1.0f;
    if (f < 0.0f)
        return 0.0f;
    return f;
}

}  // namespace

// LS3DRF 0x10025240 (from LS3DF 0x10043730): no projected geometry to clear, and sets no
// VISF_NO_ZWRITE.
void I3D_projector::Init()
{
    I3D_visual::Init();
    texture = nullptr;
    material = nullptr;
    orthogonal = true;
    range = 100.0f;
    intensity = 1.0f;
    opacity = 0.117f;
    // (The box's w values come from an unset local.)
    localBound.bbox.min = S_vector(-1.0f, -1.0f, 0.0f);
    localBound.bbox.max = S_vector(1.0f, 1.0f, 1.0f);
    localBound.center = S_vector(0.0f, 0.0f, 0.5f);
    localBound.radius = 1.5f;
    buffered = 0;
    projFlags = 0;
    color = S_vector4{1.0f, 1.0f, 1.0f, 0.0f};
    mode = 0;
    receiverSum = 0;
}

// LS3DRF 0x10025370 (from LS3DF 0x100438f0)
void I3D_projector::Uninit()
{
    // (No texture: SetTexture keeps none. No projected vertices, faces or buffers to free: LS3DRF's
    // projector has none.)
    if (material)
        material->Release();
    // (Fix: the original leaves the released pointer.)
    material = nullptr;
    I3D_frame::Uninit();
}

// LS3DRF 0x10025390 (from LS3DF 0x10043960)
I3D_RESULT I3D_projector::Duplicate(I3D_frame *source)
{
    if (!source || source == this)
        return I3DERR_INVALIDPARAMS;
    I3D_frame::Duplicate(source);
    if (source->type != FRAME_VISUAL)
        return I3D_OK;
    switch (static_cast<I3D_visual *>(source)->GetVisualType()) {
    case FourCC("PROJ"): {
        auto *src = static_cast<I3D_projector *>(source);
        SetTexture(src->texture);
        SetMaterial(src->material);
        SetOrthogonal(src->orthogonal);
        range = src->range;
        SetColor(src->color);
        SetIntensity(src->intensity);
        SetOpacity(src->opacity);
        SetMode(src->mode);
        SetBuffered(src->buffered);
        return I3D_OK;
    }
    case FourCC("LENS"):
        SetMaterial(nullptr);
        return I3D_OK;
    case FourCC("LMAP"):
    case FourCC("BBRD"):
    case FourCC("MRPH"):
    case FourCC("SNGM"):
    case FourCC("SMMR"):
    case FourCC("OBJ_"): {
        I3D_mesh *m = static_cast<I3D_object *>(source)->mesh;
        if (m) {
            if (I3D_mesh_level *level = m->GetLevel(0)) {
                if (I3D_face_group *fg = level->GetFaceGroup(0))
                    SetMaterial(fg->material);
            }
        }
        return I3D_OK;
    }
    default:
        return I3D_OK;
    }
}

// 0x10043af0. In LS3DRF the `ret 8` stub shared with other slots: no texture is kept.
void I3D_projector::SetTexture(ITexture *) {}

// 0x10043b20
void I3D_projector::SetMaterial(I3D_material *m)
{
    if (material == m)
        return;
    if (material)
        material->Release();
    material = m;
    if (m)
        ++m->refCount;
}

// LS3DRF 0x10025540 (from LS3DF 0x10043b50)
void I3D_projector::SetOrthogonal(bool on)
{
    orthogonal = on;
    projFlags = 0;
    receiverSum = 0;
}

// LS3DRF 0x10025640 (from LS3DF 0x10043c50)
void I3D_projector::SetIntensity(float value)
{
    intensity = value < 0.0f ? 0.0f : value;
    projFlags = 0;
    receiverSum = 0;
}

// LS3DRF 0x10025690 (from LS3DF 0x10043ca0)
void I3D_projector::SetOpacity(float value)
{
    opacity = Clamp01(value);
    projFlags = 0;
    receiverSum = 0;
}

// LS3DRF 0x10025560 (from LS3DF 0x10043b70)
void I3D_projector::SetColor(const S_vector4 &c)
{
    color = c;
    color.x = Clamp01(color.x);
    color.y = Clamp01(color.y);
    color.z = Clamp01(color.z);
    projFlags = 0;
    receiverSum = 0;
}

// LS3DRF 0x10025700 (from LS3DF 0x10043d10)
void I3D_projector::SetMode(int newMode)
{
    mode = newMode;
    projFlags = 0;
    receiverSum = 0;
}

// LS3DRF 0x10025720 (from LS3DF 0x10043d30)
void I3D_projector::SetBuffered(uint32_t value)
{
    buffered = value;
    projFlags = 0;
    receiverSum = 0;
}

// 0x10046130
void I3D_projector::Load()
{
    I3D_frame::Load();
    dtaRead(g_loadFile, &orthogonal, 1);
    dtaRead(g_loadFile, &mode, 1);
    int id = 0;
    dtaRead(g_loadFile, &id, 2);
    if (id > g_loadNumMaterials) {
        g_loadResult = I3DERR_FILECORRUPTED;
        return;
    }
    if (id > 0) {
        material = g_loadMaterials[id - 1];
        ++material->refCount;
    }
}

// LS3DRF's shared empty stub 0x10011b40 (marked at the scene's SetDir; from LS3DF 0x10045ab0): no
// projected geometry is built.
void I3D_projector::Build(uint32_t, I3D_frame *) {}
