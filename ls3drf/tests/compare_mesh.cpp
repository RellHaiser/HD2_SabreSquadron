// Differential test of the meshes (src/i3d/mesh.cpp: I3D_face_group, I3D_mesh_level, I3D_mesh)
// on the buffer managers (vb_manager.cpp, ib_manager.cpp): the original LS3DRF.dll's objects (made
// by its driver) and the port's run the same random operations - vertices and faces set, allocated,
// locked and written through the returned pointers, face groups added, removed, duplicated, their
// materials shared and duplicated (plain and animated, with environment flags), levels duplicated,
// their extra data, render vertex buffers (every layout, filled into the level's own or another
// one), bounds, statistics, meshes' levels set and cleared, meshes duplicated and compared,
// references dropped. After every operation everything must be identical: every object's fields
// (levels a mesh made itself compared by content), the allocation records and their memory's
// sizes (buffer_check.h), the vertex, extra and face data, the render data as far as fills wrote
// it, the materials' references.
//
// Left out, as the original misbehaves: removing a face group that is not a level's first (the
// original truncates the list there; mesh.cpp fixes it), a mesh's GetStats with an empty level
// below its last (reads through null), zero counts (a level's bounds read a vertex it does not
// have; the original's loop then runs 2^32 - 1 times): no empty face group joins a level or is a
// Duplicate source, no render buffer for a level without vertices. No face group is duplicated
// from itself (it frees its faces first, then copies its new, unset ones onto themselves; with
// LS3DF's pools the old ones were still there). No fill writes past its render
// buffer (a layout CreateRenderVB picks can be smaller than what FillRenderVB writes; the memory
// is a heap block). A mesh holding levels past its count is not released (the original leaks
// them, the port releases them). A level is not duplicated from one without face groups while it
// has some (the original keeps pointing at its deleted list). A level's extra data is made again
// after its vertex count changes (Duplicate copies the source's count of entries: the original
// would read past an array made for fewer vertices).
//
// Usage: compare_mesh.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/mesh.cpp src/i3d/material.cpp src/i3d/ib_manager.cpp src/i3d/vb_manager.cpp src/i3d/driver_factories.cpp src/i3d/anim.cpp src/math/math.cpp src/core/debug.cpp
#include <float.h>

#include <algorithm>
#include <cmath>

#include "../src/core/debug.h"
#include "../src/i3d/anim.h"
#include "../src/i3d/driver.h"
#include "../src/i3d/i3d_hooks.h"
#include "../src/i3d/loader.h"
#include "../src/i3d/material.h"
#include "../src/i3d/mesh.h"
#include "../src/igraph/igraph.h"
#include "../src/igraph/igraph_state.h"
#include "buffer_check.h"
#include "test_paths.h"

// ---------------------------------------------------------------- engine parts not linked

namespace ls3d::i3d {
I3D_driver *g_driver;
uint32_t g_driverStates[DRVSTATE_COUNT];
int g_loadFile;
int g_driverTime;
I3D_RESULT OpenAnimationFile(const char *, I3D_animation_set *) { return I3DERR_NOFILE; }
}  // namespace ls3d::i3d
IGraph *g_pIGraph;

I3D_driver::I3D_driver() : unknown04(0), mirrorStamp(0) {}
I3D_driver::~I3D_driver() {}
I3D_RESULT __stdcall I3D_driver::Init(const void *) { return 0; }
void __stdcall I3D_driver::Close() {}
void __stdcall I3D_driver::FreeUnusedResources() {}
void __stdcall I3D_driver::Tick(int) {}
int __stdcall I3D_driver::GetTime() { return 0; }
I3D_RESULT __stdcall I3D_driver::GetStats(int, void *) { return 0; }
I3D_RESULT __stdcall I3D_driver::SetState(int, uint32_t) { return 0; }
uint32_t __stdcall I3D_driver::GetState(int) { return 0; }
I3D_frame *__stdcall I3D_driver::CreateVisual(uint32_t) { return nullptr; }
I3D_frame *__stdcall I3D_driver::CreateFrame(int) { return nullptr; }
const char *__stdcall I3D_driver::GetFrameTypeName(int) { return nullptr; }
const char *__stdcall I3D_driver::GetVisualTypeName(uint32_t) { return nullptr; }
I3D_RESULT __stdcall I3D_driver::EnumVisualTypes(I3D_VISUALTYPEPROC *, bool) { return 0; }
I3D_frame *__stdcall I3D_driver::FindFrame(I3D_scene *, const char *) { return nullptr; }
void __stdcall I3D_driver::RebalanceNameTree() {}
I3D_RESULT __stdcall I3D_driver::EnumNamedFrames(I3D_ENUMPROC *, void *) { return 0; }
void __stdcall I3D_driver::DeleteFromSceneTickList(I3D_frame *) {}
void __stdcall I3D_driver::RemoveShadowCaster(I3D_frame *) {}
void __stdcall I3D_driver::RemoveTargetFrame(I3D_frame *) {}
void __stdcall I3D_driver::SetHierarchyLightLevel(I3D_frame *, float) {}
void __stdcall I3D_driver::SetHierarchyAlpha(I3D_frame *, float) {}
I3D_bbox *__stdcall I3D_driver::GetHierarchyBound(I3D_bbox &out, I3D_frame *) { return &out; }
I3D_bbox *__stdcall I3D_driver::GetHierarchyBBox(I3D_bbox &out, I3D_frame *) { return &out; }

using namespace ls3d::i3d;

namespace {

uint8_t *g_origDriver;

template <class R, class... A> R CallV(void *object, int slot, A... args)
{
    using F = R(__stdcall *)(void *, A...);
    return (*reinterpret_cast<F **>(object))[slot / 4](object, args...);
}
template <class T> T &Field(void *object, size_t offset) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(object) + offset); }

// ---------------------------------------------------------------- the objects

constexpr int kMats = 6, kGroups = 10, kLevels = 10, kMeshes = 5;
uint8_t *g_oMat[kMats];
I3D_material *g_pMat[kMats];
uint8_t *g_oGroup[kGroups];                 // face groups in no level
I3D_face_group *g_pGroup[kGroups];
uint8_t *g_oLevel[kLevels];                 // the test's references
I3D_mesh_level *g_pLevel[kLevels];
uint8_t *g_oMesh[kMeshes];
I3D_mesh *g_pMesh[kMeshes];
S_VBAlloc g_destAlloc[2];                   // a render buffer outside the levels
S_matrix g_matrix;

int MatIndex(const void *m, int side)
{
    if (!m)
        return -1;
    for (int k = 0; k < kMats; ++k) {
        if (m == (side ? static_cast<const void *>(g_pMat[k]) : g_oMat[k]))
            return k;
    }
    return -2;
}

// The bytes fills wrote into a render buffer from its start (by the port's memory: the original's
// wrote as many), set when CreateRenderVB makes it.
std::map<const void *, size_t> g_renderWritten;

// A face group's place in a level's list (-1 none, -2 not in it).
int GroupIndex(const uint8_t *g, const uint8_t *first, size_t next)
{
    if (!g)
        return -1;
    int i = 0;
    for (const uint8_t *x = first; x; x = *reinterpret_cast<uint8_t *const *>(x + next), ++i) {
        if (x == g)
            return i;
    }
    return -2;
}

// ---------------------------------------------------------------- comparing

void CheckBits(const std::string &what, const void *a, const void *b, size_t n)
{
    for (size_t i = 0; i < n; i += 4) {
        uint32_t x, y;
        std::memcpy(&x, static_cast<const uint8_t *>(a) + i, 4);
        std::memcpy(&y, static_cast<const uint8_t *>(b) + i, 4);
        Check(what + " +" + std::to_string(i), static_cast<long>(x), static_cast<long>(y));
    }
}

// An allocation's memory, `bytes` of it inside the block.
const uint8_t *VertexData(const S_VBAlloc &a, int side, size_t bytes)
{
    if (!a.buffer)
        return nullptr;
    const long size = side ? PortVBSize(a.buffer->data) : OrigVBSize(a.buffer->data);
    if (bytes > static_cast<size_t>(size))
        Fail("vertex data past the block", static_cast<long>(bytes), size);
    return a.buffer->data;
}
const uint8_t *FaceData(const S_IBAlloc &a, int side, size_t bytes)
{
    if (!a.buffer)
        return nullptr;
    const long size = side ? PortIBSize(a.buffer->data) : OrigIBSize(a.buffer->data);
    if (bytes > static_cast<size_t>(size))
        Fail("face data past the block", static_cast<long>(bytes), size);
    return reinterpret_cast<const uint8_t *>(a.buffer->data);
}

void CompareBytes(const std::string &what, const uint8_t *a, const uint8_t *b, size_t n)
{
    Check(what + " present", a != nullptr, b != nullptr);
    if (!a || !n)
        return;
    if (std::memcmp(a, b, n)) {
        size_t at = 0;
        while (a[at] == b[at])
            ++at;
        Fail(what + " byte", static_cast<long>(at), static_cast<long>(at));
    }
}

void CompareGroup(const std::string &what, uint8_t *o, I3D_face_group *p)
{
    Check(what, o != nullptr, p != nullptr);
    if (!o)
        return;
    Check(what + " material", MatIndex(Field<void *>(o, 4), 0), MatIndex(p->material, 1));
    Check(what + " numFaces", Field<int>(o, 8), p->numFaces);
    const auto &oa = Field<S_IBAlloc>(o, 0x10);
    CompareAlloc(what + " faces", oa, p->faces);
    if (oa.buffer && p->numFaces > 0) {
        const size_t n = static_cast<size_t>(p->numFaces) * 6;
        CompareBytes(what + " face data", FaceData(oa, 0, n), FaceData(p->faces, 1, n), n);
    }
}

// A render buffer's bytes the fills wrote.
void CompareRender(const std::string &what, const S_VBAlloc &o, const S_VBAlloc &p)
{
    CompareAlloc(what, o, p);
    if (!p.buffer)
        return;
    const auto it = g_renderWritten.find(p.buffer->data);
    const size_t n = it == g_renderWritten.end() ? 0 : it->second;
    CompareBytes(what + " data", VertexData(o, 0, n), VertexData(p, 1, n), n);
}

void CompareLevel(const std::string &what, uint8_t *o, I3D_mesh_level *p)
{
    Check(what, o != nullptr, p != nullptr);
    if (!o)
        return;
    CheckBits(what + " box", o + 0x10, &p->bboxMin, 0x20);
    Check(what + " refCount", Field<int>(o, 0x30), p->refCount);
    const auto &ov = Field<S_VBAlloc>(o, 0x34), &orv = Field<S_VBAlloc>(o, 0x40);
    CompareAlloc(what + " vertices", ov, p->vertices);
    CompareRender(what + " render vertices", orv, p->renderVertices);
    Check(what + " extraFormat", Field<uint32_t>(o, 0x50), p->extraFormat);
    Check(what + " numVertices", Field<int>(o, 0x54), p->numVertices);
    Check(what + " numFaceGroups", Field<int>(o, 0x58), p->numFaceGroups);
    CheckBits(what + " lodDistance", o + 0x60, &p->lodDistance, 4);
    Check(what + " flags", Field<uint32_t>(o, 0x64), p->flags);
    CheckBits(what + " envScale", o + 0x68, &p->envScale, 4);
    const size_t n = static_cast<size_t>(p->numVertices);
    CompareBytes(what + " vertex data", VertexData(ov, 0, n * 0x20), VertexData(p->vertices, 1, n * 0x20), n * 0x20);
    Check(what + " extra", Field<void *>(o, 0x4C) != nullptr, p->extraData != nullptr);
    if (p->extraData)
        CompareBytes(what + " extra data", Field<uint8_t *>(o, 0x4C), p->extraData,
                     n * static_cast<size_t>(p->ExtraStride()));
    uint8_t *og = Field<uint8_t *>(o, 0x5C);
    I3D_face_group *pg = p->faceGroups;
    for (int i = 0; og || pg; ++i) {
        CompareGroup(what + " group " + std::to_string(i), og, pg);
        og = Field<uint8_t *>(og, 0xC);
        pg = pg->next;
    }
}

// A mesh's level: the same slot of the test's, or a level of its own compared by content.
int LevelSlot(const void *l, int side)
{
    for (int k = 0; k < kLevels; ++k) {
        if (l && l == (side ? static_cast<const void *>(g_pLevel[k]) : g_oLevel[k]))
            return k;
    }
    return -1;
}

void CompareMesh(const std::string &what, uint8_t *o, I3D_mesh *p)
{
    Check(what, o != nullptr, p != nullptr);
    if (!o)
        return;
    CheckBits(what + " box and sphere", o + 0x10, &p->bboxMin, 0x30);
    Check(what + " refCount", Field<int>(o, 0x40), p->refCount);
    Check(what + " numLevels", Field<uint32_t>(o, 0x44), p->numLevels);
    for (int i = 0; i < 10; ++i) {
        uint8_t *ol = Field<uint8_t *>(o, 0x48 + 4 * i);
        I3D_mesh_level *pl = p->levels[i];
        const int ks = LevelSlot(ol, 0);
        Check(what + " level slot", ks, LevelSlot(pl, 1));
        if (ks < 0)
            CompareLevel(what + " level " + std::to_string(i), ol, pl);
    }
}

void CompareAll()
{
    for (int k = 0; k < kMats; ++k) {
        Check("material refCount", Field<int>(g_oMat[k], 4), g_pMat[k]->refCount);
        Check("material flags", Field<uint32_t>(g_oMat[k], 8), g_pMat[k]->matFlags);
    }
    for (int k = 0; k < kGroups; ++k)
        CompareGroup("group " + std::to_string(k), g_oGroup[k], g_pGroup[k]);
    for (int k = 0; k < kLevels; ++k)
        CompareLevel("level " + std::to_string(k), g_oLevel[k], g_pLevel[k]);
    for (int k = 0; k < kMeshes; ++k)
        CompareMesh("mesh " + std::to_string(k), g_oMesh[k], g_pMesh[k]);
    CompareRender("dest alloc", g_destAlloc[0], g_destAlloc[1]);
}

// ---------------------------------------------------------------- data

std::vector<uint8_t> RandVertices(int n)
{
    std::vector<uint8_t> v(static_cast<size_t>(n) * 0x20);
    for (size_t i = 0; i < v.size(); i += 4) {
        float f = Rand(50) == 0 ? (Rand(2) ? -0.0f : std::nanf("")) : (static_cast<float>(Rand(20001)) - 10000.0f) / 100.0f;
        std::memcpy(&v[i], &f, 4);
    }
    return v;
}

std::vector<uint8_t> RandFaces(int n)
{
    std::vector<uint8_t> v(static_cast<size_t>(n) * 6);
    for (uint8_t &b : v)
        b = static_cast<uint8_t>(g_rng());
    return v;
}

int RandCount(int most) { return Rand(8) == 0 ? 1 + Rand(3) : 1 + Rand(most); }

// A face group of either side: standalone (k < kGroups) or the n-th of a level.
struct GroupPair {
    uint8_t *o = nullptr;
    I3D_face_group *p = nullptr;
};
GroupPair PickGroup()
{
    if (Rand(2)) {
        const int k = Rand(kGroups);
        return {g_oGroup[k], g_pGroup[k]};
    }
    const int l = Rand(kLevels);
    if (!g_pLevel[l] || !g_pLevel[l]->numFaceGroups)
        return {};
    const int i = Rand(g_pLevel[l]->numFaceGroups);
    return {CallV<uint8_t *>(g_oLevel[l], 0x14, i), g_pLevel[l]->GetFaceGroup(i)};
}

bool InLevel(const I3D_face_group *g)
{
    for (I3D_mesh_level *l : g_pLevel) {
        for (I3D_face_group *x = l ? l->faceGroups : nullptr; x; x = x->next) {
            if (x == g)
                return true;
        }
    }
    for (I3D_mesh *m : g_pMesh) {
        for (int i = 0; m && i < 10; ++i) {
            for (I3D_face_group *x = m->levels[i] ? m->levels[i]->faceGroups : nullptr; x; x = x->next) {
                if (x == g)
                    return true;
            }
        }
    }
    return false;
}

void RandMatrix()
{
    for (auto &row : g_matrix.m)
        for (float &f : row)
            f = (static_cast<float>(Rand(2001)) - 1000.0f) / 1000.0f;
}

void DestroyOrigGroup(uint8_t *g) { OrigFn<void *(__fastcall *)(void *, void *, int)>(0x1001e490)(g, nullptr, 1); }

long g_seenEnv, g_seenRender, g_seenFills, g_seenAnonymous, g_seenDuplicated;

// The bytes FillRenderVB writes from the start of its buffer for the level as it is (LS3DRF
// 0x1001edd0): with environment flags only source 5 writes (each vertex and its extra data,
// unless extra format bit 0); otherwise each position and four extra bytes, or the vertices.
size_t FillBytes(I3D_mesh_level *p)
{
    const size_t n = static_cast<size_t>(p->numVertices);
    const uint32_t source = p->flags & MLF_ENV_SOURCE_MASK;
    if ((p->flags & MLF_ENV) && source >= 2)
        return source == 5 && !(p->extraFormat & 1) ? n * (0x20 + static_cast<size_t>(p->ExtraStride())) : 0;
    return p->extraFormat & 1 ? n * 16 : n * 0x20;
}

// The stride CreateRenderVB picks (mesh.cpp's choice: the records compare it with the original's).
size_t RenderStride(I3D_mesh_level *p, uint32_t vbFlags)
{
    const bool env = (p->flags & MLF_ENV_SOURCE_MASK) > 1 && !(vbFlags & 0x10);
    if (!(vbFlags & 0x20) && !(p->extraFormat & 1))
        return env ? 0x28 : 0x20;
    return env ? 0x20 : 0x18;
}

// The constructors leave the boxes' w unset (and the mesh's box keeps it): the port's copies the
// original's.
void Birth(uint8_t *o, void *p)
{
    std::memcpy(static_cast<uint8_t *>(p) + 0x1C, o + 0x1C, 4);
    std::memcpy(static_cast<uint8_t *>(p) + 0x2C, o + 0x2C, 4);
}

// ---------------------------------------------------------------- operations

// New extra data for the level's vertex count (the array keeps the count it was made for: a
// later Duplicate would read past it).
void RefreshExtra(uint8_t *o, I3D_mesh_level *p, uint32_t format)
{
    void *ro = CallV<void *>(o, 0x3C, format);
    void *rp = p->SetExtraFormat(format);
    Check("SetExtraFormat", ro != nullptr, rp != nullptr);
    if (ro && rp) {
        const size_t n = static_cast<size_t>(p->numVertices * p->ExtraStride());
        for (size_t i = 0; i < n; ++i)
            static_cast<uint8_t *>(ro)[i] = static_cast<uint8_t *>(rp)[i] = static_cast<uint8_t>(g_rng());
    }
}

void LevelOp(int k)
{
    uint8_t *&o = g_oLevel[k];
    I3D_mesh_level *&p = g_pLevel[k];
    if (!o) {
        g_lastOp = "level create";
        o = CallV<uint8_t *>(g_origDriver, 0x38);
        p = g_driver->CreateMeshLevel();
        Birth(o, p);
        return;
    }
    switch (Rand(20)) {
    case 0: {
        if (Rand(3))
            break;
        g_lastOp = "level Release";
        Check("Release", static_cast<long>(CallV<uint32_t>(o, 0x00)), static_cast<long>(p->Release()));
        o = nullptr;
        p = nullptr;
        break;
    }
    case 1:
    case 2: {
        g_lastOp = "level SetVertices";
        const int n = RandCount(400);
        const std::vector<uint8_t> v = RandVertices(n);
        const bool none = Rand(10) == 0;
        Check("SetVertices", CallV<I3D_RESULT>(o, 0x24, none ? nullptr : v.data(), n),
              p->SetVertices(none ? nullptr : v.data(), n));
        if (p->extraFormat)
            RefreshExtra(o, p, p->extraFormat);
        break;
    }
    case 3: {
        g_lastOp = "level AllocVertices";
        const int n = RandCount(300);
        void *ro = CallV<void *>(o, 0x28, n);
        void *rp = p->AllocVertices(n);
        Check("AllocVertices", OffsetIn(ro, Field<S_VBAlloc>(o, 0x34).vb, n * 0x20),
              OffsetIn(rp, p->vertices.vb, n * 0x20));
        if (ro && rp) {
            const std::vector<uint8_t> v = RandVertices(n);
            std::memcpy(ro, v.data(), v.size());
            std::memcpy(rp, v.data(), v.size());
            if (Rand(2)) {
                CallV<void>(o, 0x2C);
                p->ComputeBounds();
            }
        }
        if (p->extraFormat)
            RefreshExtra(o, p, p->extraFormat);
        break;
    }
    case 4: {
        g_lastOp = "level LockVertices";
        const uint32_t f = static_cast<uint32_t>(Rand(2));
        void *ro = CallV<void *>(o, 0x04, f);
        void *rp = p->LockVertices(f);
        Check("LockVertices", OffsetIn(ro, Field<S_VBAlloc>(o, 0x34).vb, 0), OffsetIn(rp, p->vertices.vb, 0));
        if (ro && rp && !f && p->numVertices) {
            const int i = Rand(p->numVertices);
            const std::vector<uint8_t> v = RandVertices(1);
            std::memcpy(static_cast<uint8_t *>(ro) + 0x20 * i, v.data(), 0x20);
            std::memcpy(static_cast<uint8_t *>(rp) + 0x20 * i, v.data(), 0x20);
        }
        break;
    }
    case 5:
        g_lastOp = "level UnlockVertices";
        CallV<void>(o, 0x08);
        p->UnlockVertices();
        break;
    case 6:
        g_lastOp = "level ComputeBounds";
        CallV<void>(o, 0x2C);
        p->ComputeBounds();
        break;
    case 7: {
        g_lastOp = "level Duplicate";
        const int s = Rand(kLevels + 1);
        uint8_t *so = s < kLevels ? g_oLevel[s] : nullptr;
        I3D_mesh_level *sp = s < kLevels ? g_pLevel[s] : nullptr;
        if (sp && sp != p && !sp->faceGroups && p->faceGroups)
            break;                          // (the original keeps its deleted list)
        Check("Duplicate", CallV<I3D_RESULT>(o, 0x0C, so), p->Duplicate(sp));
        ++g_seenDuplicated;
        break;
    }
    case 8: {
        g_lastOp = "level queries";
        Check("NumFaces", CallV<int>(o, 0x10), p->NumFaces());
        S_meshStats so{}, sp{};
        Check("GetStats", CallV<I3D_RESULT>(o, 0x18, &so), p->GetStats(&sp));
        CheckBits("stats", &so, &sp, sizeof(so));
        const int i = Rand(p->numFaceGroups + 2) - 1;
        uint8_t *go = CallV<uint8_t *>(o, 0x14, i);
        I3D_face_group *gp = p->GetFaceGroup(i);
        Check("GetFaceGroup", GroupIndex(go, Field<uint8_t *>(o, 0x5C), 0xC),
              GroupIndex(reinterpret_cast<uint8_t *>(gp), reinterpret_cast<uint8_t *>(p->faceGroups), 0xC));
        Check("ExtraStride", CallV<int>(o, 0x40), p->ExtraStride());
        break;
    }
    case 9:
    case 10: {
        // A standalone group with faces joins the level.
        const int g = Rand(kGroups);
        if (!g_pGroup[g] || !g_pGroup[g]->numFaces)
            break;
        g_lastOp = "level AddFaceGroup";
        CallV<void>(o, 0x1C, g_oGroup[g]);
        p->AddFaceGroup(g_pGroup[g]);
        g_oGroup[g] = nullptr;
        g_pGroup[g] = nullptr;
        g_seenEnv += (p->flags & MLF_ENV) != 0;
        break;
    }
    case 11: {
        g_lastOp = "level RemoveFaceGroup";
        if (!p->faceGroups || Rand(4) == 0) {
            // Not in the list.
            const int g = Rand(kGroups);
            Check("RemoveFaceGroup (absent)", CallV<I3D_RESULT>(o, 0x20, g_oGroup[g]), p->RemoveFaceGroup(g_pGroup[g]));
            break;
        }
        uint8_t *go = Field<uint8_t *>(o, 0x5C);
        I3D_face_group *gp = p->faceGroups;
        Check("RemoveFaceGroup", CallV<I3D_RESULT>(o, 0x20, go), p->RemoveFaceGroup(gp));
        int free = -1;
        for (int g = 0; g < kGroups; ++g) {
            if (!g_pGroup[g])
                free = g;
        }
        if (free < 0) {
            DestroyOrigGroup(go);
            delete gp;
        } else {
            g_oGroup[free] = go;
            g_pGroup[free] = gp;
        }
        break;
    }
    case 12:
    case 13: {
        if (!p->numVertices)
            break;
        g_lastOp = "level CreateRenderVB";
        static const uint32_t kBits[] = {1, 2, 8, 0x10, 0x20};
        uint32_t f = 0;
        for (uint32_t b : kBits)
            f |= Rand(3) == 0 ? b : 0;
        const size_t fill = f & 8 ? 0 : FillBytes(p);
        if (fill > RenderStride(p, f) * static_cast<size_t>(p->numVertices))
            break;                          // (the fill would write past the buffer)
        RandMatrix();
        const bool dest = Rand(4) == 0;
        if (dest) {
            OrigFn<void(__fastcall *)(void *, void *, S_VBAlloc *)>(0x10030070)(&Orig<uint8_t>(0x100588a0), nullptr,
                                                                              &g_destAlloc[0]);
            FreeVB(&g_destAlloc[1]);
            g_destAlloc[0] = g_destAlloc[1] = S_VBAlloc{};
        } else {
            CallV<void>(o, 0x34);
            p->FreeRenderVB();
        }
        const bool ro = CallV<bool>(o, 0x30, f, &g_matrix, dest ? &g_destAlloc[0] : nullptr);
        Check("CreateRenderVB", ro, p->CreateRenderVB(f, &g_matrix, dest ? &g_destAlloc[1] : nullptr));
        if (const C_VertexBuffer *b = (dest ? g_destAlloc[1] : p->renderVertices).buffer)
            g_renderWritten[b->data] = fill;
        ++g_seenRender;
        break;
    }
    case 14: {
        g_lastOp = "level FillRenderVB";
        const bool deferred = Rand(4) == 0;
        const bool dest = Rand(4) == 0;
        const S_VBAlloc &into = dest ? g_destAlloc[1] : p->renderVertices;
        if (deferred || !into.vb) {
            // (Only marks the level stale.)
        } else if (!p->vertices.buffer) {
            break;                          // (no vertices to read: the original reads through null)
        } else if (FillBytes(p) > static_cast<size_t>(PortVBSize(into.buffer->data))) {
            break;                          // (the fill would write past the buffer)
        }
        RandMatrix();
        CallV<void>(o, 0x38, deferred ? 1 : 0, &g_matrix, dest ? &g_destAlloc[0] : nullptr);
        p->FillRenderVB(deferred, &g_matrix, dest ? &g_destAlloc[1] : nullptr);
        if (!deferred && into.vb) {
            size_t &w = g_renderWritten[into.buffer->data];
            w = std::max(w, FillBytes(p));
        }
        ++g_seenFills;
        break;
    }
    case 15:
        g_lastOp = "level FreeRenderVB";
        CallV<void>(o, 0x34);
        p->FreeRenderVB();
        break;
    case 16: {
        g_lastOp = "level SetExtraFormat";
        static const uint32_t kFormats[] = {0, 1, 2, 4, 8, 3, 5, 9, 0xF};
        RefreshExtra(o, p, kFormats[Rand(9)]);
        break;
    }
    default: {
        // Face groups of the level.
        GroupPair g;
        if (p->numFaceGroups) {
            const int i = Rand(p->numFaceGroups);
            g = {CallV<uint8_t *>(o, 0x14, i), p->GetFaceGroup(i)};
        }
        if (!g.p)
            break;
        g_lastOp = "level group DuplicateMaterial";
        Check("DuplicateMaterial", CallV<I3D_RESULT>(g.o, 0x18), g.p->DuplicateMaterial());
        break;
    }
    }
}

void GroupOp()
{
    const int k = Rand(kGroups);
    if (!g_pGroup[k] && Rand(2)) {
        g_lastOp = "group create";
        g_oGroup[k] = CallV<uint8_t *>(g_origDriver, 0x34);
        g_pGroup[k] = g_driver->CreateFaceGroup();
        return;
    }
    GroupPair g = Rand(2) ? GroupPair{g_oGroup[k], g_pGroup[k]} : PickGroup();
    if (!g.p)
        return;
    const bool standalone = !InLevel(g.p);
    switch (Rand(9)) {
    case 0: {
        if (!standalone || Rand(2))
            break;
        g_lastOp = "group destroy";
        DestroyOrigGroup(g.o);
        delete g.p;
        for (int i = 0; i < kGroups; ++i) {
            if (g_pGroup[i] == g.p) {
                g_oGroup[i] = nullptr;
                g_pGroup[i] = nullptr;
            }
        }
        break;
    }
    case 1: {
        g_lastOp = "group SetMaterial";
        const int m = Rand(kMats + 1) - 1;
        CallV<void>(g.o, 0x00, m < 0 ? nullptr : g_oMat[m]);
        g.p->SetMaterial(m < 0 ? nullptr : g_pMat[m]);
        break;
    }
    case 2: {
        g_lastOp = "group SetFaces";
        const bool none = standalone && Rand(8) == 0;
        const int n = RandCount(300);
        const std::vector<uint8_t> f = RandFaces(n);
        const void *src = none ? nullptr : f.data();
        void *ro = CallV<void *>(g.o, 0x04, src, n);
        void *rp = g.p->SetFaces(static_cast<const I3D_triface *>(src), n);
        Check("SetFaces", OffsetIn(ro, Field<S_IBAlloc>(g.o, 0x10).ib, 0), OffsetIn(rp, g.p->faces.ib, 0));
        break;
    }
    case 3: {
        g_lastOp = "group AllocFaces";
        const int n = RandCount(300);
        void *ro = CallV<void *>(g.o, 0x08, n);
        void *rp = g.p->AllocFaces(n);
        Check("AllocFaces", OffsetIn(ro, Field<S_IBAlloc>(g.o, 0x10).ib, 0), OffsetIn(rp, g.p->faces.ib, 0));
        const std::vector<uint8_t> f = RandFaces(n);
        std::memcpy(ro, f.data(), f.size());
        std::memcpy(rp, f.data(), f.size());
        break;
    }
    case 4: {
        g_lastOp = "group Lock";
        const uint32_t fl = static_cast<uint32_t>(Rand(2));
        void *ro = CallV<void *>(g.o, 0x0C, fl);
        void *rp = g.p->Lock(fl);
        Check("Lock", OffsetIn(ro, Field<S_IBAlloc>(g.o, 0x10).ib, 0), OffsetIn(rp, g.p->faces.ib, 0));
        if (ro && !fl) {
            const int i = Rand(g.p->numFaces);
            const std::vector<uint8_t> f = RandFaces(1);
            std::memcpy(static_cast<uint8_t *>(ro) + 6 * i, f.data(), 6);
            std::memcpy(static_cast<uint8_t *>(rp) + 6 * i, f.data(), 6);
        }
        break;
    }
    case 5:
        g_lastOp = "group Unlock";
        CallV<void>(g.o, 0x10);
        g.p->Unlock();
        break;
    case 6: {
        GroupPair s = PickGroup();
        if (!s.p || !s.p->numFaces || s.p == g.p)
            break;                          // (from itself: copies its new, unset faces onto themselves)
        g_lastOp = "group Duplicate";
        Check("Duplicate", CallV<I3D_RESULT>(g.o, 0x14, s.o), g.p->Duplicate(s.p));
        break;
    }
    default:
        g_lastOp = "group DuplicateMaterial";
        Check("DuplicateMaterial", CallV<I3D_RESULT>(g.o, 0x18), g.p->DuplicateMaterial());
        break;
    }
}

void MeshOp(int k)
{
    uint8_t *&o = g_oMesh[k];
    I3D_mesh *&p = g_pMesh[k];
    if (!o) {
        g_lastOp = "mesh create";
        o = CallV<uint8_t *>(g_origDriver, 0x3C);
        p = g_driver->CreateMesh();
        Birth(o, p);
        return;
    }
    switch (Rand(9)) {
    case 0: {
        if (Rand(3))
            break;
        bool beyond = false;
        for (int i = static_cast<int>(p->numLevels); i < 10; ++i)
            beyond |= p->levels[i] != nullptr;
        if (beyond && p->refCount == 1)
            break;                          // (the original leaks those levels; mesh.cpp releases them)
        g_lastOp = "mesh Release";
        Check("Release", static_cast<long>(CallV<uint32_t>(o, 0x00)), static_cast<long>(p->Release()));
        o = nullptr;
        p = nullptr;
        break;
    }
    case 1:
    case 2: {
        g_lastOp = "mesh SetLevel";
        const int l = Rand(kLevels + 2) - 2;
        const int i = Rand(11);
        uint8_t *lo = l >= 0 ? g_oLevel[l] : nullptr;
        I3D_mesh_level *lp = l >= 0 ? g_pLevel[l] : nullptr;
        Check("SetLevel", CallV<I3D_RESULT>(o, 0x08, lo, i), p->SetLevel(lp, i));
        break;
    }
    case 3: {
        g_lastOp = "mesh GetLevel";
        const int i = Rand(10);
        Check("GetLevel", LevelSlot(CallV<void *>(o, 0x04, i), 0), LevelSlot(p->GetLevel(i), 1));
        break;
    }
    case 4: {
        g_lastOp = "mesh Duplicate";
        const int s = Rand(kMeshes + 1);
        uint8_t *so = s < kMeshes ? g_oMesh[s] : nullptr;
        I3D_mesh *sp = s < kMeshes ? g_pMesh[s] : nullptr;
        Check("Duplicate", CallV<I3D_RESULT>(o, 0x0C, so), p->Duplicate(sp));
        for (int i = 0; i < 10; ++i)
            g_seenAnonymous += p->levels[i] && LevelSlot(p->levels[i], 1) < 0;
        break;
    }
    case 5: {
        bool gap = false;
        for (uint32_t i = 0; i < p->numLevels; ++i)
            gap |= !p->levels[i];
        if (gap)
            break;                          // (the original reads through the empty level)
        g_lastOp = "mesh GetStats";
        S_meshStats so{}, sp{};
        Check("GetStats", CallV<I3D_RESULT>(o, 0x10, &so), p->GetStats(&sp));
        CheckBits("stats", &so, &sp, sizeof(so));
        break;
    }
    case 6:
        g_lastOp = "mesh ComputeBounds";
        CallV<void>(o, 0x14);
        p->ComputeBounds();
        break;
    case 7:
        g_lastOp = "mesh DuplicateMaterials";
        Check("DuplicateMaterials", CallV<I3D_RESULT>(o, 0x18), p->DuplicateMaterials());
        break;
    default: {
        const int s = Rand(kMeshes);
        if (!g_pMesh[s])
            break;
        g_lastOp = "mesh SameLayout";
        Check("SameLayout", CallV<bool>(o, 0x1C, g_oMesh[s]), p->SameLayout(g_pMesh[s]));
        break;
    }
    }
}

void RunOp()
{
    const int r = Rand(20);
    if (r < 9) {
        LevelOp(Rand(kLevels));
    } else if (r < 14) {
        GroupOp();
    } else if (r < 18) {
        MeshOp(Rand(kMeshes));
    } else {
        // A material's environment flags.
        g_lastOp = "material flags";
        const int m = Rand(kMats);
        uint32_t f = (Field<uint32_t>(g_oMat[m], 8) & ~0x7f07fu) | static_cast<uint32_t>(Rand(Rand(8) ? 127 : 3) + 1) % 128;
        f = (f & ~(7u << 12)) | static_cast<uint32_t>(Rand(8)) << 12;
        f = Rand(3) ? f | MATF_ENV_TEXTURE : f & ~MATF_ENV_TEXTURE;
        Field<uint32_t>(g_oMat[m], 8) = f;
        g_pMat[m]->matFlags = f;
    }
}

}  // namespace

int main(int argc, char **argv)
{
    SetUnhandledExceptionFilter(&CrashFilter);
    const char *origPath = OriginalDllPath(argc > 1 ? argv[1] : nullptr, "LS3DRF.dll");
    const long ops = argc > 2 ? std::atol(argv[2]) : 200000;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 1;
    if (!SetUpBuffers(origPath))
        return 2;
    g_logDisabled = true;
    _controlfp(_PC_24, _MCW_PC);
    g_rng.seed(seed);
    g_origDriver = Orig<uint8_t *>(0x10058b78);
    g_driver = new I3D_driver;
    for (int k = 0; k < kMats; ++k) {
        const bool anim = k % 3 == 2;
        g_oMat[k] = CallV<uint8_t *>(g_origDriver, anim ? 0x2C : 0x28);
        g_pMat[k] = anim ? g_driver->CreateAnimMaterial() : g_driver->CreateMaterial();
    }

    CompareAll();
    for (g_op = 0; g_op < ops; ++g_op) {
        RunOp();
        CompareAll();
    }
    std::printf("coverage: levels with environment flags %ld, render buffers %ld, fills %ld, duplicated levels %ld, "
                "mesh-made levels %ld\n", g_seenEnv, g_seenRender, g_seenFills, g_seenDuplicated, g_seenAnonymous);
    std::printf("compare_mesh: %ld operations, seed %u: identical\n", ops, seed);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
