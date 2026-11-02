// I3D_mirror (0x10032280 - 0x10033fb0). (0x10033f90 is the static initialiser of this file's copy
// of a header's zero vector; 0x10033f70 the vertex buffer manager's locked-list removal, inlined.)
#include "i3d/mirror.h"

#include <cmath>
#include <new>

#include "core/debug.h"
#include "core/fpu.h"
#include "external/rw_data.h"
#include "i3d/camera.h"
#include "i3d/driver.h"
#include "i3d/loader.h"
#include "i3d/object.h"
#include "i3d/scene.h"
#include "i3d/sector.h"
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"

using namespace ls3d;
using namespace ls3d::i3d;

// I3D_mirror ----------------------------------------------------------------------------------------------

// LS3DRF 0x1001f4c0 (from LS3DF 0x10032280)
void I3D_mirror::Init()
{
    I3D_visual::Init();
    color.x = color.y = color.z = 0.0f;
    planeMatrix.Identity();
    numVertices = 0;
    numFaces = 0;
    vertices = nullptr;
    mirrorFlags = 0;
    // (No stamp, scene, plane sphere, range or colored vertices: LS3DRF's mirror has none.)
    faces = S_IBAlloc{};
}

// LS3DRF 0x1001f520 (from LS3DF 0x10032330)
void I3D_mirror::Uninit()
{
    // (In no scene, with no copies or colored vertices: LS3DRF's mirror has none.)
    AlignedFree(vertices);
    // (Fix: the original leaves the pointer to the freed vertices.)
    vertices = nullptr;
    FreeIB(&faces);
    I3D_frame::Uninit();
}

// LS3DRF 0x1001f550 (from LS3DF 0x100323c0)
bool I3D_mirror::SetColor(const S_vector4 &c)
{
    if (IsTinyBits(std::fabs(color.x - c.x)) && IsTinyBits(std::fabs(color.y - c.y)) &&
        IsTinyBits(std::fabs(color.z - c.z)))
        return true;
    color = c;
    auto clamp = [](float &v) {
        if (v < 0.0f)
            v = 0.0f;
        else if (v > 1.0f)
            v = 1.0f;
    };
    clamp(color.x);
    clamp(color.y);
    clamp(color.z);
    // (No colored vertices to refill: LS3DRF's mirror has none.)
    return false;
}

// LS3DRF 0x1001f6a0 (from LS3DF 0x10032520)
I3D_triface *I3D_mirror::LockFaces(uint32_t)
{
    if (!numFaces)
        return nullptr;
    return reinterpret_cast<I3D_triface *>(faces.buffer->data);
}

// LS3DRF 0x1001f6c0 (from LS3DF 0x10033d50)
void I3D_mirror::SetPlane(const I3D_bbox &box, const S_matrix &matrix)
{
    planeMatrix = matrix;
    for (int r = 0; r < 3; ++r) {
        const float h = ((&box.max.x)[r] - (&box.min.x)[r]) * 0.5f;
        planeMatrix.m[r][0] = h * planeMatrix.m[r][0];
        planeMatrix.m[r][1] = h * planeMatrix.m[r][1];
        planeMatrix.m[r][2] = h * planeMatrix.m[r][2];
    }
    const S_vector center((box.max.x + box.min.x) * 0.5f, (box.max.y + box.min.y) * 0.5f,
                          (box.max.z + box.min.z) * 0.5f);
    // (By the scaled matrix: the original's.)
    planeMatrix(3) = center * planeMatrix;
    mirrorFlags = (mirrorFlags & ~MIRF_REFLECTION_VALID) | MIRF_PLANE;
}

// LS3DRF 0x1001f7f0 (from LS3DF 0x10033e80)
void I3D_mirror::Load()
{
    I3D_frame::Load();
    dtaRead(g_loadFile, &localBound, 0x30);
    visualFlags = (visualFlags & ~VISF_WBOUND_VALID) | VISF_BOUND_VALID;
    // The plane matrix, color, range and the vertex and face counts.
    dtaRead(g_loadFile, &planeMatrix, 0x5c);
    mirrorFlags = (mirrorFlags & ~MIRF_REFLECTION_VALID) | MIRF_PLANE;
    vertices = static_cast<S_vector4 *>(AlignedAlloc(sizeof(S_vector4) * numVertices, 16));
    // (Fix: the original reads into a failed allocation.)
    if (!vertices) {
        g_loadResult = I3DERR_OUTOFMEM;
        ReportError(I3DERR_OUTOFMEM, kSevFatal);
        return;
    }
    dtaRead(g_loadFile, vertices, numVertices * 16);
    if (!g_ibManager.Alloc(D3DUSAGE_SOFTWAREPROCESSING, numFaces * 3, &faces)) {
        g_loadResult = I3DERR_OUTOFMEM;
        ReportError(I3DERR_OUTOFMEM, kSevFatal);
        return;
    }
    dtaRead(g_loadFile, LockFaces(0), numFaces * 6);
}
