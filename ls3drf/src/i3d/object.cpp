// I3D_object (0x10035fc0 - 0x10038410).
#include "i3d/object.h"

#include <cmath>
#include <cstring>

#include "core/debug.h"
#include "core/fpu.h"
#include "i3d/billboard.h"
#include "i3d/camera.h"
#include "i3d/driver.h"
#include "i3d/i3d_hooks.h"
#include "i3d/material.h"
#include "i3d/loader.h"
#include "i3d/mesh.h"
#include "i3d/scene.h"
#include "i3d/sector.h"
#include "i3d/singlemesh.h"
#include "i3d/vb_manager.h"
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace ls3d::i3d {

float g_lodFadeScale;

void ObjectSetLightLevel(I3D_frame *visual, float level) { static_cast<I3D_object *>(visual)->SetLightLevel(level); }
void ObjectSetAlpha(I3D_frame *visual, float value) { static_cast<I3D_object *>(visual)->SetAlpha(value); }

}  // namespace ls3d::i3d

namespace {

// The identity matrix the world-space meshes are drawn with (0x1009cf40).
const S_matrix kIdentity = { { { 1.0f, 0.0f, 0.0f, 0.0f },
                               { 0.0f, 1.0f, 0.0f, 0.0f },
                               { 0.0f, 0.0f, 1.0f, 0.0f },
                               { 0.0f, 0.0f, 0.0f, 1.0f } } };

// The matrix a mesh visual's vertices are drawn with.
const S_matrix &DrawMatrix(I3D_visual *v)
{
    return (v->visualFlags & VISF_WORLD_SPACE) ? kIdentity : v->GetMatrix();
}

// A level's vertices transformed (position, normal: the 0x20-byte vertex) by `m`.
void TransformLevel(I3D_mesh_level *level, const S_matrix &m)
{
    uint8_t *v = static_cast<uint8_t *>(level->LockVertices(0));
    for (int n = level->numVertices; n != 0; --n, v += 0x20) {
        S_vector &pos = *reinterpret_cast<S_vector *>(v);
        S_vector &normal = *reinterpret_cast<S_vector *>(v + 0xC);
        pos = pos * m;
        S_vector nn = normal.RotateByMatrix(m);
        NormalizeWithSquare(nn, SquareZYX(nn));
        normal = nn;
    }
    level->FillRenderVB(true, nullptr, nullptr);
    level->ComputeBounds();
}

}  // namespace

// 0x10035fc0
void I3D_object::Init()
{
    I3D_visual::Init();
    visualFlags |= VISF_OBJECT_PARAMS;
    lightLevel = 1.0f;
    alpha = 1.0f;
    lodFade = 1.0f;
    mesh = nullptr;
    textureLevel = 0;
    std::memset(lights.indices, 0, sizeof lights.indices);
    lights.count = 0;
    lights.stamp = -1;
}

// 0x10036020 (the mesh pointer is kept: Init clears it)
void I3D_object::Uninit()
{
    if (mesh)
        mesh->Release();
    I3D_frame::Uninit();
}

// LS3DRF 0x10022160 (from LS3DF 0x10038410)
void I3D_object::Load()
{
    I3D_frame::Load();
    I3D_mesh *loaded = LoadMesh();
    SetMesh(loaded, 0);
    if (loaded)
        loaded->Release();
}

// 0x10036040
I3D_RESULT I3D_object::SetMesh(I3D_mesh *newMesh, uint32_t)
{
    auto takeBound = [this](const I3D_mesh *m) {
        std::memcpy(&localBound.bbox, &m->bboxMin, 0x20);
        localBound.center = m->center;
        localBound.radius = m->radius;
    };
    if (newMesh == mesh) {
        if (newMesh && !(visualFlags & VISF_WORLD_SPACE))
            takeBound(newMesh);
        return I3D_OK;
    }
    if (mesh)
        mesh->Release();
    mesh = newMesh;
    if (!newMesh) {
        localBound.bbox.min = S_vector(1e16f, 1e16f, 1e16f);
        localBound.bbox.max = S_vector(-1e16f, -1e16f, -1e16f);
        localBound.center = S_vector(0.0f, 0.0f, 0.0f);
        localBound.radius = 0.0f;
        return I3D_OK;
    }
    ++newMesh->refCount;
    if (!(visualFlags & VISF_WORLD_SPACE))
        takeBound(newMesh);
    textureLevel = 0;
    return I3D_OK;
}

// LS3DRF 0x10021930 (from LS3DF 0x10036160)
I3D_RESULT I3D_object::DuplicateMesh()
{
    if (!mesh || mesh->refCount <= 1)
        return I3D_OK;
    I3D_mesh *copy = g_driver->CreateMesh();
    if (!copy) {
        ReportError(I3DERR_OUTOFMEM, kSevFatal);
        return I3DERR_OUTOFMEM;
    }
    const I3D_RESULT r = copy->Duplicate(mesh);
    if (r < 0) {
        copy->Release();
        return r;
    }
    mesh->Release();
    mesh = copy;
    return I3D_OK;
}

// 0x100368d0 / 0x100368f0
void I3D_object::SetLightLevel(float level) { lightLevel = level; }
void I3D_object::SetAlpha(float value) { alpha = value; }

// 0x10036960
void I3D_object::RefreshLevel(int level)
{
    if (mesh && mesh->levels[level])
        mesh->levels[level]->FillRenderVB(true, nullptr, nullptr);
}

// 0x10036910
void I3D_object::RefreshLevels()
{
    if (!mesh)
        return;
    for (uint32_t i = mesh->numLevels; i != 0; --i) {
        if (mesh->levels[i - 1])
            mesh->levels[i - 1]->FillRenderVB(true, nullptr, nullptr);
    }
}

// LS3DRF 0x100219b0 (from LS3DF 0x100361e0)
I3D_RESULT I3D_object::Duplicate(I3D_frame *source)
{
    if (!source || source == this)
        return I3DERR_INVALIDPARAMS;
    I3D_frame::Duplicate(source);
    if (source->type != FRAME_VISUAL)
        return I3D_OK;
    I3D_object *src = static_cast<I3D_object *>(source);
    switch (src->GetVisualType()) {
    case FourCC("LMAP"):
    case FourCC("BBRD"):
    case FourCC("MRPH"):
    case FourCC("SNGM"):
    case FourCC("SMMR"):
    case FourCC("OBJ_"):
        SetMesh(src->mesh, 0);
        lightLevel = src->lightLevel;
        alpha = src->alpha;
        break;
    case FourCC("LENS"):
        SetMesh(nullptr, 0);
        break;
    default:
        return I3DERR_GENERIC;
    }
    visualFlags = src->visualFlags & 0x2ff52;
    localBound = src->localBound;
    if (!(src->visualFlags & VISF_WORLD_SPACE)) {
        visualFlags &= ~VISF_WBOUND_VALID;
    } else {
        if (!(src->visualFlags & VISF_WBOUND_VALID))
            src->UpdateWBoundProc();
        worldBound = src->worldBound;
    }
    visualFlags |= VISF_OBJECT_PARAMS;
    return I3D_OK;
}

// LS3DRF 0x10021b20 (from LS3DF 0x10036350)
void I3D_object::SetWorldSpace(bool on)
{
    if (visualFlags & VISF_WORLD_SPACE) {
        if (on)
            return;
        visualFlags &= ~VISF_WORLD_SPACE;
        if (!mesh)
            return;
        DuplicateMesh();
        const S_matrix *m;
        if (visualFlags & VISF_WORLD_SPACE) {
            m = &kIdentity;
        } else {
            if (!(visualFlags & VISF_INVMATRIX_VALID)) {
                visualFlags |= VISF_INVMATRIX_VALID;
                invWorldMatrix.Inverse(GetMatrix());
            }
            m = &invWorldMatrix;
        }
        for (uint32_t i = 0; i < mesh->numLevels; ++i) {
            if (I3D_mesh_level *level = mesh->GetLevel(static_cast<int>(i)))
                TransformLevel(level, *m);
        }
        mesh->ComputeBounds();
        RefreshLevels();
        return;
    }
    if (!on)
        return;
    if (!(visualFlags & VISF_WBOUND_VALID))
        UpdateWBoundProc();
    if (!mesh) {
        visualFlags = (visualFlags & ~VISF_WBOUND_VALID) | VISF_WORLD_SPACE;
        return;
    }
    DuplicateMesh();
    const S_matrix &m = DrawMatrix(this);
    for (uint32_t i = 0; i < mesh->numLevels; ++i) {
        if (I3D_mesh_level *level = mesh->GetLevel(static_cast<int>(i)))
            TransformLevel(level, m);
    }
    mesh->ComputeBounds();
    visualFlags = (visualFlags & ~VISF_WBOUND_VALID) | VISF_WORLD_SPACE;
    RefreshLevels();
}
