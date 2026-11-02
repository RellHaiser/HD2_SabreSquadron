// I3D_lens (0x10007dd0 - 0x10009610; 0x100097f0 / 0x10009810 are the static initialisers of this
// file's copies of a header's zero vector and identity quaternion).
#include "i3d/lens.h"

#include <cmath>
#include <cstdlib>

#include "core/fpu.h"
#include "external/rw_data.h"
#include "i3d/camera.h"
#include "i3d/collision.h"
#include "i3d/loader.h"
#include "i3d/material.h"
#include "i3d/scene.h"
#include "i3d/sector.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"
#include "platform/platform.h"

using namespace ls3d;
using namespace ls3d::i3d;

// 0x10007dd0
void I3D_lens::Init()
{
    I3D_visual::Init();
    // (The colors' w stay as they were: the original writes an unset local.)
    transmit.x = transmit.y = transmit.z = 1.0f;
    color.x = color.y = color.z = 1.0f;
    lensFlags = 0;
    testInterval = 150;
    testTime = 0;
    updateTime = 0;
    brightness = 0.0f;
    target = 1.0f;
    sizeScale = 1.0f;
    intensity = 1.0f;
    fadeIn = 15.0f;
    fadeOut = 8.0f;
    range = 300.0f;
    nearRange = 0.0f;
    farStart = 0.0f;
    nearScale = 1.0f;
}

// 0x10007ea0
void I3D_lens::Uninit()
{
    for (S_lensElement &e : elements) {
        if (e.material)
            e.material->Release();
    }
    elements.Clear();
    I3D_frame::Uninit();
}

// LS3DRF 0x10003d70 (from LS3DF 0x10007ef0)
I3D_RESULT I3D_lens::Duplicate(I3D_frame *source)
{
    if (!source || source == this)
        return I3DERR_INVALIDPARAMS;
    I3D_frame::Duplicate(source);
    if (source->type != FRAME_VISUAL)
        return I3D_OK;
    auto *srcVisual = static_cast<I3D_visual *>(source);
    if (srcVisual->visualFlags & VISF_WORLD_SPACE)
        visualFlags |= VISF_WORLD_SPACE;
    if (srcVisual->GetVisualType() != FourCC("LENS"))
        return I3D_OK;
    auto *src = static_cast<I3D_lens *>(source);
    testInterval = src->testInterval;
    nearScale = src->nearScale;
    sizeScale = src->sizeScale;
    fadeIn = src->fadeIn;
    fadeOut = src->fadeOut;
    range = src->range;
    nearRange = src->nearRange;
    farStart = src->farStart;
    // (Fix: the original drops the old elements without releasing their materials.)
    for (S_lensElement &e : elements) {
        if (e.material)
            e.material->Release();
    }
    elements.Free();
    elements.ResizeDiscard(src->elements.Size());
    for (int i = elements.Size() - 1; i >= 0; --i)
        elements[i] = src->elements[i];
    visualFlags = src->visualFlags & 0x2ff52;
    localBound = src->localBound;
    if (!(src->visualFlags & VISF_WORLD_SPACE)) {
        visualFlags &= ~VISF_WBOUND_VALID;
    } else {
        src->GetWorldBound();
        worldBound = src->worldBound;
    }
    for (S_lensElement &e : elements) {
        if (e.material)
            ++e.material->refCount;
    }
    return I3D_OK;
}

// LS3DRF 0x10003f50 (from LS3DF 0x10009610)
void I3D_lens::Load()
{
    I3D_frame::Load();
    int count = 0;
    dtaRead(g_loadFile, &count, 1);
    if (!count)
        return;
    // (Fix: an element without a material ID takes the previous one's, which the original leaves
    // unset for the first and shares without a reference.)
    I3D_material *material = nullptr;
    while (count--) {
        S_lensElement e;
        dtaRead(g_loadFile, &e, 8);
        int id = 0;
        dtaRead(g_loadFile, &id, 2);
        if (id > g_loadNumMaterials) {
            g_loadResult = I3DERR_FILECORRUPTED;
            material = nullptr;
        } else if (id > 0) {
            material = g_loadMaterials[id - 1];
        }
        if (material)
            ++material->refCount;
        e.material = material;
        elements.PushBack(e);
    }
    // A small box; its center stays where it was. (The original also writes unset w values.)
    localBound.bbox.min = S_vector(-0.05f, -0.05f, -0.05f);
    localBound.bbox.max = S_vector(0.05f, 0.05f, 0.05f);
    localBound.radius = 0.01f;
}

// 0x100080d0 (LS3DRF's lens has no DrawFlare slot: no flare drawn; the slot stays for the
// LS3DF layout.)
void I3D_lens::DrawFlare(bool) {}
