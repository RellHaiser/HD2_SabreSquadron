// I3D_lit_object and its lightmap levels (0x100232d0 - 0x1002b030, but Construct and its helpers:
// lightmap_build.cpp). 0x10023390 is the static initialiser of this file's copy of a header's
// (0, 0, 0, 1); 0x1002af30 / 0x1002af60 are vector constructors (grow 0x20 / 2) of the visual
// factory.
#include "i3d/litobject.h"

#include <cmath>
#include <cstring>
#include <new>

#include "core/debug.h"
#include "core/fpu.h"
#include "external/rw_data.h"
#include "i3d/driver.h"
#include "i3d/material.h"
#include "i3d/mesh.h"
#include "i3d/scene.h"
#include "i3d/sector.h"
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"

using namespace ls3d;
using namespace ls3d::i3d;

// ---------------------------------------------------------------- S_lightmapLevel

// (The original leaves the pointers freed here as they were, and the part count at -1: its
// callers delete the level.)
void S_lightmapLevel::Free()
{
    ::operator delete(uvs);
    uvs = nullptr;
    ::operator delete(unknown08);
    unknown08 = nullptr;
    delete lightmap;
    lightmap = nullptr;
    if (faceParts) {
        for (int i = 0; i < numFaceGroups; ++i)
            ::operator delete(faceParts[i]);
        ::operator delete(faceParts);
    }
    faceParts = nullptr;
    for (int i = 0; i < numParts; ++i) {
        S_lightmapPart &p = parts[i];
        // (Fix: the original frees only patches wider than 3, so narrower ones' pixels leak.)
        if (!p.IsUniform()) {
            ::operator delete(p.pixels);
            ::operator delete(p.texels);
        }
    }
    numParts = 0;
    ::operator delete(parts);
    parts = nullptr;
    FreeIB(&faces);
    faces = {};
    FreeVB(&vertices);
    vertices = {};
    ::operator delete(vertexMap);
    vertexMap = nullptr;
    ::operator delete(colors);
    colors = nullptr;
    textureBytes = 0;
}

void S_lightmapLevel::SetFaceParts(uint16_t *p, int faceGroup)
{
    if (faceGroup < numFaceGroups)
        faceParts[faceGroup] = p;
}

bool S_lightmapLevel::Build(I3D_mesh_level *level, bool keepFaces, const I3D_frame *owner)
{
    lightmap = new (std::nothrow) C_lightmap;
    if (!lightmap) {
        ReportError(I3DERR_OUTOFMEM, kSevFatal);
        return false;
    }
    auto *indices = static_cast<uint16_t *>(::operator new(sizeof(uint16_t) * static_cast<size_t>(numParts)));
    for (int i = 0; i < numParts; ++i)
        indices[i] = static_cast<uint16_t>(i);
    textureBytes = 0;
    if (!lightmap->Create(indices, parts, numParts, &textureBytes, owner)) {
        ::operator delete(indices);
        return false;
    }
    ::operator delete(indices);
    if (!uvs)
        return false;
    // Each vertex's uv in its patch to the texture: the patch's texel centers, turned for a
    // turned patch (u + 2).
    const ITexture *t = lightmap->texture;
    const float invWidth = 1.0f / static_cast<float>(t->width);
    const float invHeight = 1.0f / static_cast<float>(t->height);
    S_lightmapUV *uv = uvs;
    for (int i = 0; i < numVertices; ++i, ++uv) {
        level->LockVertices(0);             // (every vertex; the result is not used)
        const uint16_t partIndex = vertexMap[i].part;
        if (partIndex >= numParts)
            return false;
        const S_lightmapPart &p = parts[partIndex];
        const float u0 = uv->u;
        const float v0 = uv->v;
        if (!(p.u >= 2.0f)) {
            uv->u = (static_cast<float>(p.width - 1) * u0 + 0.5f) * invWidth + p.u;
            uv->v = (static_cast<float>(p.height - 1) * v0 + 0.5f) * invHeight + p.v;
        } else {
            uv->u = ((static_cast<float>(p.height - 1) * v0 * invWidth + p.u) - 2.0f) + invWidth * 0.5f;
            uv->v = (static_cast<float>(p.width - 1) * u0 + 0.5f) * invHeight + p.v;
        }
    }
    if (keepFaces)
        return true;
    // The faces: each corner the lightmap vertex of its vertex in the face's patch (0 when none).
    if (!g_ibManager.Alloc(D3DUSAGE_SOFTWAREPROCESSING, static_cast<uint32_t>(level->NumFaces() * 3), &faces)) {
        ReportError(I3DERR_OUTOFMEM, kSevFatal);
        return false;
    }
    uint16_t *dst = faces.buffer->Lock(0) + faces.offset;
    int groupIndex = 0;
    for (I3D_face_group *fg = level->faceGroups; fg; fg = fg->next, ++groupIndex) {
        const uint16_t *facePart = faceParts[groupIndex];
        const int n = fg->numFaces;
        const uint16_t *tri = reinterpret_cast<const uint16_t *>(fg->Lock(1));
        for (int f = 0; f < n; ++f, tri += 3, ++facePart) {
            const uint16_t part = *facePart;
            for (int c = 0; c < 3; ++c) {
                uint16_t found = 0;
                for (uint16_t k = 0; k < numVertices; ++k) {
                    if (vertexMap[k].vertex == tri[c] && vertexMap[k].part == part) {
                        found = k;
                        break;
                    }
                }
                *dst++ = found;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------- I3D_lit_object

void I3D_lit_object::Init()
{
    I3D_object::Init();
    currentSet = 0;
    for (S_lightmapSet &s : sets) {
        s.loaded = false;
        s.flags = LMSET_TEXTURES;
        s.resolution = 5.0f;
        s.smoothAngle = 45.0f;
        for (S_lightmapLevel *&l : s.levels)
            l = nullptr;
    }
}

// LS3DRF 0x10018f10 (from LS3DF 0x10023320): the object's part only; no levels to delete (Construct
// and LoadLightmaps make none).
void I3D_lit_object::Uninit()
{
    I3D_object::Uninit();
}

// LS3DRF 0x10018f60 (from LS3DF 0x100237c0): no lightmap baker, so I3DERR_UNSUPPORTED.
I3D_RESULT I3D_lit_object::Construct(I3D_scene *, const S_vector *, float, uint32_t, I3D_LIGHTMAP_PROGRESS *,
                                     void *)
{
    return I3DERR_UNSUPPORTED;
}

// LS3DRF 0x10018f70 (from LS3DF 0x10028a00): the current set marked not loaded; it has no levels.
I3D_RESULT I3D_lit_object::FreeLightmaps(int)
{
    CurrentSet().loaded = false;
    return I3D_OK;
}

// LS3DRF 0x10018fa0 (from LS3DF 0x100292f0): I3DERR_UNSUPPORTED, with no message.
I3D_RESULT I3D_lit_object::SaveLightmaps(int) { return I3DERR_UNSUPPORTED; }

// LS3DRF 0x10018fb0 (from LS3DF 0x10029310): the current set freed (FreeLightmaps through the vtable),
// its flags set.
I3D_RESULT I3D_lit_object::SetSetFlags(uint32_t flags)
{
    FreeLightmaps(0);
    CurrentSet().flags = flags;
    return I3D_OK;
}

// LS3DRF 0x10018f20 (from LS3DF 0x100233c0): the set stored; no levels to make again.
void I3D_lit_object::SetCurrentSet(int set)
{
    // (Fix: the original takes any number, and later indexes past the sets with it.)
    if (set < 0 || set >= 6)
        return;
    currentSet = set;
}

int I3D_lit_object::GetTextureBytes()
{
    int bytes = 0;
    if (!mesh)
        return 0;
    for (const S_lightmapSet &s : sets) {
        for (uint32_t i = 0; i < mesh->numLevels; ++i) {
            if (s.levels[i])
                bytes += s.levels[i]->textureBytes;
        }
    }
    return bytes;
}

S_lightmapLevel *I3D_lit_object::LevelAt(int level, int set)
{
    // (Fix: the original indexes the sets and levels with any numbers.)
    if (set < 0 || set >= 6 || level < 0 || level >= I3D_mesh::kMaxLevels)
        return nullptr;
    return sets[set].levels[level];
}

int I3D_lit_object::GetNumParts(int level, int set)
{
    const S_lightmapLevel *l = LevelAt(level, set);
    return l ? l->numParts : 0;
}

S_lightmapPart *I3D_lit_object::GetParts(int level, int set)
{
    S_lightmapLevel *l = LevelAt(level, set);
    return l ? l->parts : nullptr;
}

// LS3DRF 0x10018f40 (from LS3DF 0x10023460): a no-op (no lightmap textures).
void I3D_lit_object::RewriteParts(int, int) {}

// LS3DRF 0x10018f50 (from LS3DF 0x100236e0): I3D_OK; no level to lock.
I3D_RESULT I3D_lit_object::LockFaces(int, int, uint16_t **) { return I3D_OK; }

void I3D_lit_object::RefreshLevel(int level)
{
    if (level < 0 || level >= I3D_mesh::kMaxLevels)     // (fix: the original reads past the levels)
        return;
    for (S_lightmapSet &s : sets) {
        if (s.levels[level])
            s.levels[level]->dirty = true;
    }
}

void I3D_lit_object::RefreshLevels()
{
    if (!mesh)                              // (fix: the original reads the missing mesh)
        return;
    for (S_lightmapSet &s : sets) {
        for (uint32_t i = 0; i < mesh->numLevels; ++i) {
            if (s.levels[i])
                s.levels[i]->dirty = true;
        }
    }
}

// LS3DRF 0x100191a0 (from LS3DF 0x10029500)
I3D_RESULT I3D_lit_object::SetMesh(I3D_mesh *newMesh, uint32_t keep)
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
    if (!static_cast<uint8_t>(keep))
        FreeLightmaps(0);
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
    // (The original returns whatever eax held.)
    return I3D_OK;
}

// LS3DRF 0x10018fe0 (from LS3DF 0x10029340)
I3D_RESULT I3D_lit_object::Duplicate(I3D_frame *source)
{
    if (!source || source == this)
        return I3DERR_INVALIDPARAMS;
    FreeLightmaps(0);
    I3D_frame::Duplicate(source);
    if (source->type != FRAME_VISUAL)
        return I3D_OK;
    auto *src = static_cast<I3D_object *>(source);
    switch (src->GetVisualType()) {
    case FourCC("LMAP"): {
        const auto *ls = static_cast<const I3D_lit_object *>(source);
        for (int i = 0; i < 6; ++i) {
            sets[i].flags = ls->sets[i].flags;
            sets[i].resolution = ls->sets[i].resolution;
            sets[i].smoothAngle = ls->sets[i].smoothAngle;
        }
        [[fallthrough]];
    }
    case FourCC("BBRD"):
    case FourCC("MRPH"):
    case FourCC("SNGM"):
    case FourCC("SMMR"):
    case FourCC("OBJ_"):
        SetMesh(src->mesh, 1);
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
    if (src->visualFlags & VISF_WORLD_SPACE) {
        if (!(src->visualFlags & VISF_WBOUND_VALID))
            src->UpdateWBoundProc();
        worldBound = src->worldBound;
    } else {
        visualFlags &= ~VISF_WBOUND_VALID;
    }
    return I3D_OK;
}

// LS3DRF's shared return-0 stub 0x10018f90 (marked at sound SetCurrTime; from LS3DF 0x10028a60): no
// lightmaps, nothing read from `file`.
I3D_RESULT I3D_lit_object::LoadLightmaps(int) { return I3D_OK; }
