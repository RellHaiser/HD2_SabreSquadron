// I3D_occluder (0x10038490 - 0x1003ab60; 0x1003aa80 is the static initialiser of this file's copy
// of a header's zero vector).
#include "i3d/occluder.h"

#include <cstring>
#include <map>
#include <new>
#include <vector>

#include "external/rw_data.h"
#include "i3d/loader.h"
#include "i3d/sector.h"

using namespace ls3d;
using namespace ls3d::i3d;

// ---------------------------------------------------------------- the frame slots

void I3D_occluder::Init()
{
    I3D_frame::Init();
    frameFlags |= 0x08100000;
    numSilhouettePlanes = 0;
    numFrontPlanes = 0;
    occluderFlags = 0;
    nextInScene = nullptr;
    prevInScene = nullptr;
    numVertices = 0;
    numFaces = 0;
    nextInSector = nullptr;
    ownerSector = nullptr;
    faceFlags = nullptr;
    worldVertices = nullptr;
    vertices = nullptr;
    faces = nullptr;
    neighborEdges = nullptr;
    neighbors = nullptr;
    facePlanes = nullptr;
    scene = nullptr;
    center = S_vector(0.0f, 0.0f, 0.0f);
    centerW = 0.0f;
    radius = 0.0f;
    radiusSquared = 0.0f;
    viewDepth = 0.0f;
}

// LS3DRF 0x100222a0 (from LS3DF 0x10038550)
void I3D_occluder::Uninit()
{
    // (The pointers stay until Init.)
    ::operator delete(faceFlags);
    AlignedFree(worldVertices);
    AlignedFree(vertices);
    ::operator delete(faces);
    ::operator delete(neighborEdges);
    ::operator delete(neighbors);
    AlignedFree(facePlanes);
    if (ownerSector) {
        ownerSector->occluder = nullptr;
        ownerSector = nullptr;
    }
    I3D_frame::Uninit();
}

I3D_RESULT I3D_occluder::Duplicate(I3D_frame *source)
{
    if (!source)
        return I3DERR_INVALIDPARAMS;
    const I3D_RESULT result = I3D_frame::Duplicate(source);
    if (source->GetType() == FRAME_OCCLUDER) {
        const auto *src = static_cast<const I3D_occluder *>(source);
        Build(src->vertices, src->numVertices, src->faces, src->numFaces, sizeof(S_vector4));
    }
    return result;
}

void I3D_occluder::Load()
{
    I3D_frame::Load();
    dtaRead(g_loadFile, &numVertices, 8);           // numVertices, numFaces
    vertices = static_cast<S_vector4 *>(AlignedAlloc(sizeof(S_vector4) * numVertices, 16));
    worldVertices = static_cast<S_vector4 *>(AlignedAlloc(sizeof(S_vector4) * numVertices, 16));
    faces = static_cast<I3D_triface *>(::operator new(sizeof(I3D_triface) * numFaces, std::nothrow));
    if (!vertices || !faces || !worldVertices) {
        AlignedFree(vertices);
        vertices = nullptr;
        numVertices = 0;
        AlignedFree(worldVertices);
        worldVertices = nullptr;
        ::operator delete(faces);
        faces = nullptr;
        numFaces = 0;
        g_loadResult = I3DERR_OUTOFMEM;
        return;
    }
    dtaRead(g_loadFile, vertices, sizeof(S_vector4) * numVertices);
    dtaRead(g_loadFile, faces, sizeof(I3D_triface) * numFaces);
    const I3D_RESULT result = BuildEdges();
    if (result != I3D_OK)
        g_loadResult = result;
}

I3D_RESULT I3D_occluder::Build(const void *verts, int numVerts, const I3D_triface *tris, int numTris, int stride)
{
    // (The original frees its arrays first: duplicating itself read them freed. They go after
    // the copy.)
    S_vector4 *const oldVertices = vertices;
    S_vector4 *const oldWorldVertices = worldVertices;
    I3D_triface *const oldFaces = faces;
    vertices = static_cast<S_vector4 *>(AlignedAlloc(sizeof(S_vector4) * numVerts, 16));
    worldVertices = static_cast<S_vector4 *>(AlignedAlloc(sizeof(S_vector4) * numVerts, 16));
    faces = static_cast<I3D_triface *>(::operator new(sizeof(I3D_triface) * numTris, std::nothrow));
    if (!vertices || !worldVertices || !faces) {
        AlignedFree(oldVertices);
        AlignedFree(oldWorldVertices);
        ::operator delete(oldFaces);
        return I3DERR_OUTOFMEM;
    }
    numVertices = numVerts;
    const auto *src = static_cast<const uint8_t *>(verts);
    for (int i = 0; i < numVerts; ++i, src += stride) {
        // (the fourth dword stays unset)
        const auto *v = reinterpret_cast<const S_vector *>(src);
        vertices[i].x = v->x;
        vertices[i].y = v->y;
        vertices[i].z = v->z;
    }
    numFaces = numTris;
    std::memcpy(faces, tris, sizeof(I3D_triface) * numTris);
    AlignedFree(oldVertices);
    AlignedFree(oldWorldVertices);
    ::operator delete(oldFaces);
    return BuildEdges();
}

I3D_RESULT I3D_occluder::BuildEdges()
{
    // (The original sizes its edge pool with a 16-bit count, and an occluder built again keeps
    // the arrays of its earlier mesh allocated: both fixed.)
    ::operator delete(neighbors);
    ::operator delete(neighborEdges);
    AlignedFree(facePlanes);
    ::operator delete(faceFlags);
    neighbors = static_cast<uint16_t *>(::operator new(sizeof(uint16_t) * 3 * numFaces, std::nothrow));
    neighborEdges = static_cast<uint8_t *>(::operator new(3 * static_cast<size_t>(numFaces), std::nothrow));
    facePlanes = static_cast<S_plane *>(AlignedAlloc(sizeof(S_plane) * numFaces, 16));
    faceFlags = static_cast<uint16_t *>(::operator new(sizeof(uint16_t) * numFaces, std::nothrow));
    if (!neighbors || !neighborEdges || !facePlanes || !faceFlags)
        return I3DERR_OUTOFMEM;

    // Every face edge keyed by its vertex pair (smaller index high); the edges of one key form a
    // ring in the order they come, and each edge's neighbour is the next in its ring (itself for
    // an open edge). (The original keeps the keys in a binary tree, 0x1003aaa0 inserting; the rings
    // are the same.)
    struct Edge {
        uint16_t face, edge;
        int next;
    };
    std::vector<Edge> edges;
    edges.reserve(3 * static_cast<size_t>(numFaces));
    std::map<uint32_t, std::pair<int, int>> rings;      // key -> first, last
    for (int k = 0; k < numFaces; ++k) {
        for (int e = 0; e < 3; ++e) {
            const uint16_t a = faces[k].v[e], b = faces[k].v[(e + 1) % 3];
            const uint32_t key = a < b ? (uint32_t(a) << 16 | b) : (uint32_t(b) << 16 | a);
            const int index = static_cast<int>(edges.size());
            edges.push_back({static_cast<uint16_t>(k), static_cast<uint16_t>(e), index});
            auto it = rings.find(key);
            if (it == rings.end()) {
                rings.emplace(key, std::make_pair(index, index));
            } else {
                edges[it->second.second].next = index;
                edges[index].next = it->second.first;
                it->second.second = index;
            }
        }
    }
    for (const Edge &edge : edges) {
        const Edge &across = edges[edge.next];
        neighbors[edge.face * 3 + edge.edge] = across.face;
        neighborEdges[edge.face * 3 + edge.edge] = static_cast<uint8_t>(across.edge);
    }
    std::memset(faceFlags, 0, sizeof(uint16_t) * numFaces);
    radius = 1.0f;
    return I3D_OK;
}
