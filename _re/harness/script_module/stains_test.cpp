// The blood stains on a human's body (part 11g: game/human_stains.cpp). MinGW; build_stains.bat; run stains_test.exe ("stains: N checks, 0 failures").
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/chunk.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_stains.h"
#include "game/human_weapon_pose.h"
#include "game/items.h"
#include "game/pending.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "game/stain_images.inc"

int g_missionVariant;

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)

std::string g_file;
int g_at;
}  // namespace
int RW_DATA_CALL dtaOpen(const char *, bool) { g_at = 0; return 1; }
int RW_DATA_CALL dtaOpenWrite(const char *, int) { g_file.clear(); g_at = 0; return 1; }
bool RW_DATA_CALL dtaClose(int) { return true; }
int RW_DATA_CALL dtaSeek(int, int offset, int origin)
{
    if (origin == 0) g_at = offset;
    else if (origin == 1) g_at += offset;
    else g_at = static_cast<int>(g_file.size()) + offset;
    return g_at;
}
int RW_DATA_CALL dtaRead(int, void *data, int size)
{
    int got = static_cast<int>(g_file.size()) - g_at;
    if (got > size) got = size;
    if (got < 0) got = 0;
    std::memcpy(data, g_file.data() + g_at, got);
    g_at += got;
    return got;
}
int RW_DATA_CALL dtaWrite(int, const void *data, int size)
{
    if (static_cast<size_t>(g_at) + size > g_file.size())
        g_file.resize(g_at + size);
    std::memcpy(&g_file[g_at], data, size);
    g_at += size;
    return size;
}
void I3D_frame::UpdateWMatrixProc() {}

namespace {
// ---- fake images ----
struct S_FakeImage {
    void **vtable;
    unsigned char pad[0x1C];
    uint16_t width, height;                             // +0x20, +0x22
    std::string file, alpha;
    uint32_t mode;
    int releases = 0;
    int locks = 0, unlocks = 0;
    int pitch = 64 * 2;
    uint16_t pixels[64 * 64];
};
void *g_imageVt[16];
std::vector<S_FakeImage *> g_images;
uint32_t __stdcall F_ImageRelease(S_FakeImage *self) { self->releases++; return 0; }
void *__stdcall F_ImageLock(S_FakeImage *self, int, int *pitch, int) { self->locks++; *pitch = self->pitch; return self->pixels; }
void __stdcall F_ImageUnlock(S_FakeImage *self) { self->unlocks++; }

void *g_graphVt[128];
struct { void **vtable; } g_graph{g_graphVt};
HRESULT __stdcall F_LoadTexture(void *, const char *file, const char *alpha, uint32_t mode, IImage **out)
{
    S_FakeImage *image = new S_FakeImage();
    image->vtable = g_imageVt;
    image->width = 64;
    image->height = 64;
    image->file = file;
    image->alpha = alpha;
    image->mode = mode;
    g_images.push_back(image);
    *out = reinterpret_cast<IImage *>(image);
    return 0;
}

void *g_driverVt[128];
struct { void **vtable; } g_driver{g_driverVt};
std::map<int, uint32_t> g_states;
std::vector<std::pair<int, uint32_t>> g_stateLog;
void __stdcall F_SetState(void *, int state, uint32_t value) { g_states[state] = value; g_stateLog.push_back({state, value}); }
uint32_t __stdcall F_GetState(void *, int state) { return g_states[state]; }

// ---- the fake material ----
struct S_FakeMaterial {
    void **vtable;
    IImage *image = nullptr;
    int mode30 = 0, mode34 = 0;
    int calls = 0;
};
void *g_materialVt[32];
void __stdcall F_MaterialImage(S_FakeMaterial *self, IImage *image) { self->image = image; self->calls++; }
void __stdcall F_Material30(S_FakeMaterial *self, int mode) { self->mode30 = mode; }
void __stdcall F_Material34(S_FakeMaterial *self, int mode) { self->mode34 = mode; }

// ---- a mesh of seven quads: group g at x = 10 g; 4 vertices, 2 triangles each ----
struct S_FakeGroup {
    void **vtable;
    S_FakeMaterial *material;
    int numFaces;
    void *next;
    uint16_t faces[6];
    int duplicates = 0;
};
void *g_groupVt[16];
uint16_t *__stdcall F_GroupLock(S_FakeGroup *self, uint32_t) { return self->faces; }
void __stdcall F_GroupDuplicate(S_FakeGroup *self) { self->duplicates++; }

struct S_Vertex32 { float x, y, z, nx, ny, nz, u, v; };
struct S_FakeLevel {
    void **vtable;
    unsigned char pad[0x48];
    float *uvs;                                         // +0x4C
    uint32_t pad50;
    int numVertices;                                    // +0x54
    unsigned char pad2[100 - 0x58];
    uint32_t flags;                                     // +100
    S_Vertex32 vertices[28];
    S_FakeGroup groups[7];
    int unlocks = 0, uvMakes = 0, details = 0;
};
void *g_levelVt[32];
void *__stdcall F_LevelVertices(S_FakeLevel *self, int) { return self->vertices; }
void __stdcall F_LevelUnlock(S_FakeLevel *self) { self->unlocks++; }
I3D_face_group *__stdcall F_LevelGroup(S_FakeLevel *self, int index) { return reinterpret_cast<I3D_face_group *>(&self->groups[index]); }
void __stdcall F_LevelMakeUVs(S_FakeLevel *self, int sets)
{
    CHECK(sets == 2);
    self->uvs = new float[self->numVertices * 2]();
    self->uvMakes++;
}
void __stdcall F_LevelDetail(S_FakeLevel *self, int, int, int) { self->details++; }

struct S_FakeMesh {
    void **vtable;
    S_FakeLevel *level;
    int calls18 = 0;
};
void *g_meshVt[16];
I3D_mesh_level *__stdcall F_MeshLevel(S_FakeMesh *self, int) { return reinterpret_cast<I3D_mesh_level *>(self->level); }
void __stdcall F_Mesh18(S_FakeMesh *self) { self->calls18++; }

// ---- frames ----
struct S_FakeVisual {
    alignas(16) unsigned char bytes[0x400];
    S_FakeMesh mesh;
    S_FakeLevel level;
    float positions[28][4];
    int duplicates = 0, refreshes = 0;
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(bytes); }
};
void *g_visualVt[128];
void __stdcall F_VisualDuplicate(I3D_frame *) {}
const float *__stdcall F_VisualPositions(I3D_frame *self, int)
{
    return &reinterpret_cast<S_FakeVisual *>(self)->positions[0][0];
}
void __stdcall F_VisualRefresh(I3D_frame *self) { reinterpret_cast<S_FakeVisual *>(self)->refreshes++; }
I3D_frame *g_childOf;
I3D_frame *__stdcall F_FindChild(I3D_frame *, const char *name, uint32_t) { return std::strcmp(name, "a") ? nullptr : g_childOf; }

S_FakeVisual *MakeVisual(float dx = 0)
{
    S_FakeVisual *visual = new S_FakeVisual();
    std::memset(visual->bytes, 0, sizeof(visual->bytes));
    *reinterpret_cast<void ***>(visual->bytes) = g_visualVt;
    I3D_frame *frame = visual->frame();
    frame->m_frameFlags = 0x20;
    float *matrix = reinterpret_cast<float *>(visual->bytes + 0x40);
    for (int i = 0; i < 4; ++i) matrix[i * 5] = 1.0f;
    *reinterpret_cast<uint32_t *>(visual->bytes + 0x1D0) = 0x800;
    *reinterpret_cast<S_FakeMesh **>(visual->bytes + 0x1E0) = &visual->mesh;
    visual->mesh.vtable = g_meshVt;
    visual->mesh.level = &visual->level;
    S_FakeLevel &level = visual->level;
    level.vtable = g_levelVt;
    level.numVertices = 28;
    for (int g = 0; g < 7; ++g) {
        S_FakeGroup &group = level.groups[g];
        group.vtable = g_groupVt;
        group.material = new S_FakeMaterial();
        group.material->vtable = g_materialVt;
        group.numFaces = 2;
        const uint16_t base = static_cast<uint16_t>(g * 4);
        const uint16_t faces[6] = {base, static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 2), static_cast<uint16_t>(base + 1), static_cast<uint16_t>(base + 3), static_cast<uint16_t>(base + 2)};
        std::memcpy(group.faces, faces, sizeof(faces));
        for (int v = 0; v < 4; ++v) {
            S_Vertex32 &vertex = level.vertices[base + v];
            vertex.x = g * 10.0f + (v & 1);
            vertex.y = static_cast<float>(v >> 1);
            vertex.z = 0;
            vertex.u = 0.25f + 0.25f * (v & 1);
            vertex.v = 0.25f + 0.25f * (v >> 1);
            visual->positions[base + v][0] = vertex.x;
            visual->positions[base + v][1] = vertex.y;
            visual->positions[base + v][2] = vertex.z;
        }
    }
    return visual;
}

// ---- the pose's owner ----
alignas(16) unsigned char g_gameBytes[0x400];
struct S_FakeItem { void **vtable; float weight; };
void *g_itemVt[64];
float __thiscall F_ItemWeight(S_FakeItem *self) { return self->weight; }
struct S_FakeContainer { void **vtable; S_FakeItem *item; };
void *g_containerVt[64];
S_FakeItem *__thiscall F_ContainerItem(S_FakeContainer *self) { return self->item; }

struct S_Body {
    alignas(16) unsigned char humanBytes[0x1000];
    alignas(16) unsigned char poseBytes[sizeof(C_HumanWeaponPose)];
    alignas(16) unsigned char modelBytes[0x400];
    S_FakeVisual *visual;
    S_FakeItem item{g_itemVt, 0.0f};
    S_FakeContainer container{g_containerVt, &item};
    S_FakeContainer *containers[2] = {&container, nullptr};
    C_HumanStains *stains = nullptr;
    std::string name;
    C_Human *human() { return reinterpret_cast<C_Human *>(humanBytes); }
    C_HumanWeaponPose *pose() { return reinterpret_cast<C_HumanWeaponPose *>(poseBytes); }
    I3D_frame *model() { return reinterpret_cast<I3D_frame *>(modelBytes); }
};
void *g_modelVt[128];
S_Body *MakeBody(const char *model = "soldier.i3d", int role = 3, float weight = 0.0f)
{
    S_Body *body = new S_Body();
    std::memset(body->humanBytes, 0, sizeof(body->humanBytes));
    std::memset(body->poseBytes, 0, sizeof(body->poseBytes));
    std::memset(body->modelBytes, 0, sizeof(body->modelBytes));
    body->visual = MakeVisual();
    body->name = model;
    body->item.weight = weight;
    body->human()->m_role = static_cast<E_HumanRole>(role);
    body->human()->m_containers = reinterpret_cast<C_ItemContainer **>(body->containers);
    body->pose()->m_owner = body->human();
    body->pose()->m_model = body->model();
    *reinterpret_cast<void ***>(body->modelBytes) = g_modelVt;
    *reinterpret_cast<const char **>(body->modelBytes + 0x134) = body->name.c_str();
    body->stains = new C_HumanStains(body->pose());
    return body;
}
void BindBody(S_Body *body, char mode = 0)
{
    g_childOf = body->visual->frame();
    body->stains->Bind(mode);
}
S_FakeImage *Image(IImage *image) { return reinterpret_cast<S_FakeImage *>(image); }

int CountChanged(const uint16_t *buffer, int count = 64 * 64)
{
    int changed = 0;
    for (int i = 0; i < count; ++i)
        if (buffer[i]) ++changed;
    return changed;
}

void Fresh()
{
    g_stainManager.Done();
    g_stainManager.m_null = nullptr;
    g_stainManager.m_paintCounter = 0;
    g_images.clear();
    g_stateLog.clear();
    g_missionVariant = 0;
    g_states.clear();
}

void TestManagerTextures()
{
    Fresh();
    g_states[0xD] = 77;
    g_stainManager.Init(0);
    // 2 + 8 + 8 x 7, with the loading state off meanwhile and back after.
    CHECK(g_images.size() == 66);
    CHECK(g_stateLog.size() == 2 && g_stateLog[0].second == 0 && g_stateLog[1].second == 77);
    if (g_images.size() == 66) {
        CHECK(g_images[0]->file == "stainNull.bmp" && g_images[0]->alpha == "stainNullA.bmp" && g_images[0]->mode == 0x200004);
        CHECK(g_images[1]->file == "hshadow.bmp" && g_images[1]->alpha == "hshadowA.bmp" && g_images[1]->mode == 0x200084);
        CHECK(g_images[2]->file == "camouflage.bmp" && g_images[2]->alpha == "camouflageA.bmp" && g_images[2]->mode == 0x200084);
        CHECK(g_images[5]->file == "camouflage.bmp" && g_images[5]->alpha == "camouflageD.bmp");
        CHECK(g_images[6]->file == "camouflage2.bmp" && g_images[6]->alpha == "camouflageA.bmp");
        CHECK(g_images[9]->file == "camouflage2.bmp" && g_images[9]->alpha == "camouflageD.bmp");
        CHECK(g_images[10]->file == "stainNull.bmp" && g_images[10]->mode == 0x200004 && g_images[65]->file == "stainNull.bmp");
    }
    CHECK(g_stainManager.NullTexture() == reinterpret_cast<IImage *>(g_images[0]));
    CHECK(g_stainManager.HeadTexture() == reinterpret_cast<IImage *>(g_images[1]));
    CHECK(g_stainManager.PaintTexture(7) == reinterpret_cast<IImage *>(g_images[9]));
    // Again: nothing (also through Reset).
    g_stainManager.Init(0);
    g_stainManager.Reset();
    CHECK(g_images.size() == 66);
    // Done: all released, once; again nothing.
    std::vector<S_FakeImage *> images = g_images;
    g_stainManager.Done();
    for (S_FakeImage *image : images)
        CHECK(image->releases == 1);
    g_stainManager.Done();
    for (S_FakeImage *image : images)
        CHECK(image->releases == 1);
    CHECK(g_stainManager.NullTexture() == nullptr);
    // Init again loads anew.
    g_stainManager.Init(0);
    CHECK(g_images.size() == 132);
}

void TestMeshBuilding()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    g_childOf = body->visual->frame();
    S_StainMesh *mesh = g_stainManager.AcquireMesh(body->model());
    CHECK(mesh != nullptr);
    if (!mesh) return;
    CHECK(mesh->refCount == 1 && !std::strcmp(mesh->name, "soldier.i3d"));
    CHECK(body->visual->level.unlocks == 1);
    for (int g = 0; g < 7; ++g) {
        const S_StainGroup &group = mesh->groups[g];
        CHECK(group.index == g && group.numFaces == 2 && group.numVertices == 28);
        CHECK(group.vertices.size() == 4);
        CHECK(group.edges.size() == 5);                 // (the quad's four and the diagonal)
        if (group.vertices.size() == 4) {
            CHECK(group.vertices[0]->index == g * 4 && group.vertices[1]->index == g * 4 + 1 && group.vertices[2]->index == g * 4 + 2);
        }
        size_t atFirst = 0;
        for (const S_StainEdge *edge : group.edges)
            if (edge->a->index == g * 4 || edge->b->index == g * 4)
                ++atFirst;
        CHECK(atFirst == 2);
        // The diagonal has both faces, the others one.
        int shared = 0;
        for (const S_StainEdge *edge : group.edges) {
            if (edge->otherFace >= 0) {
                ++shared;
                CHECK(edge->face == 0 && edge->otherFace == 1);
                CHECK(edge->a->index == g * 4 + 1 && edge->b->index == g * 4 + 2);
            }
        }
        CHECK(shared == 1);
    }
    // The extreme vertices: order in the list, not the index.
    const S_StainGroup &group = mesh->groups[1];
    const float xs[2] = {group.vertices[group.minX]->index == 4 || group.vertices[group.minX]->index == 6 ? 10.0f : 11.0f,
                         group.vertices[group.maxX]->index == 5 || group.vertices[group.maxX]->index == 7 ? 11.0f : 10.0f};
    CHECK(xs[0] == 10.0f && xs[1] == 11.0f);
    CHECK(group.vertices[group.minY]->index == 4 || group.vertices[group.minY]->index == 5);
    CHECK(group.vertices[group.maxY]->index == 6 || group.vertices[group.maxY]->index == 7);
    CHECK(group.minZ == 0 && group.maxZ == 0);

    // The same model: shared; another: its own. Released to nothing: gone.
    S_Body *same = MakeBody();
    g_childOf = same->visual->frame();
    CHECK(g_stainManager.AcquireMesh(same->model()) == mesh && mesh->refCount == 2);
    CHECK(same->visual->level.unlocks == 0);
    S_Body *other = MakeBody("other.i3d");
    g_childOf = other->visual->frame();
    S_StainMesh *otherMesh = g_stainManager.AcquireMesh(other->model());
    CHECK(otherMesh != nullptr && otherMesh != mesh);
    CHECK(g_stainManager.m_meshes.size() == 2);
    g_stainManager.ReleaseMesh(mesh);
    CHECK(mesh->refCount == 1 && g_stainManager.m_meshes.size() == 2);
    g_stainManager.ReleaseMesh(mesh);
    CHECK(g_stainManager.m_meshes.size() == 1 && g_stainManager.m_meshes[0] == otherMesh);
    int dummy = 0;
    g_stainManager.ReleaseMesh(reinterpret_cast<S_StainMesh *>(&dummy));    // (not known: nothing)
    CHECK(g_stainManager.m_meshes.size() == 1);
    // No model, no child, or not a single mesh: none.
    CHECK(g_stainManager.AcquireMesh(nullptr) == nullptr);
    S_Body *none = MakeBody("none.i3d");
    g_childOf = nullptr;
    CHECK(g_stainManager.AcquireMesh(none->model()) == nullptr);
    *reinterpret_cast<uint32_t *>(none->visual->bytes + 0x1D0) = 0;
    g_childOf = none->visual->frame();
    CHECK(g_stainManager.AcquireMesh(none->model()) == nullptr);
    // Vertices at one place are joined.
    Fresh();
    g_stainManager.Init(0);
    S_Body *welded = MakeBody("welded.i3d");
    welded->visual->level.vertices[3].x = welded->visual->level.vertices[0].x;
    welded->visual->level.vertices[3].y = welded->visual->level.vertices[0].y;
    g_childOf = welded->visual->frame();
    S_StainMesh *weldedMesh = g_stainManager.AcquireMesh(welded->model());
    CHECK(weldedMesh && weldedMesh->groups[0].edges.size() == 6);
    CHECK(weldedMesh && weldedMesh->groups[1].edges.size() == 5);
}

void TestBind()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    BindBody(body);
    C_HumanStains *stains = body->stains;
    S_FakeLevel &level = body->visual->level;
    CHECK(stains->m_mesh != nullptr);
    CHECK(stains->m_level == reinterpret_cast<I3D_mesh_level *>(&level));
    CHECK(stains->m_width == 64 && stains->m_height == 64 && stains->m_maskX == 63 && stains->m_maskY == 63 && stains->m_shiftX == 6 && stains->m_shiftY == 6);
    CHECK(body->visual->mesh.calls18 == 1);
    // UVs copied; flags; the level told.
    CHECK(level.uvMakes == 1 && level.uvs != nullptr);
    if (level.uvs) {
        CHECK(level.uvs[0] == 0.25f && level.uvs[1] == 0.25f && level.uvs[6] == 0.5f && level.uvs[7] == 0.5f);
        CHECK(level.uvs[55] == 0.5f);
    }
    CHECK(level.details == 1);
    CHECK((level.flags & 0xF) == (8 | 5));
    CHECK(body->visual->refreshes == 1);
    // Every group: its own material copy, the null texture, a clean buffer.
    for (int g = 0; g < 7; ++g) {
        CHECK(level.groups[g].duplicates == 1);
        S_FakeMaterial *material = level.groups[g].material;
        CHECK(material->image == g_stainManager.NullTexture());
        CHECK(material->mode30 == 6 && material->mode34 == 5);
        CHECK(stains->m_buffers[g] != nullptr);
        if (stains->m_buffers[g]) {
            CHECK(CountChanged(stains->m_buffers[g]) == 0);
        }
    }
    CHECK(stains->m_clean != nullptr && CountChanged(stains->m_clean) == 0);
    CHECK(!stains->m_visible && stains->m_paint == 0 && !stains->m_ownTextures);

    // Bound again: buffers kept, UVs made once; the mesh kept (mode 0), taken anew (mode 2).
    S_StainMesh *mesh = stains->m_mesh;
    uint16_t *buffer = stains->m_buffers[3];
    BindBody(body);
    CHECK(stains->m_mesh == mesh && stains->m_buffers[3] == buffer && level.uvMakes == 1);
    BindBody(body, 2);
    CHECK(stains->m_mesh != nullptr);
    // Mode 1: only the textures again. Other modes: nothing.
    const int refreshes = body->visual->refreshes;
    BindBody(body, 1);
    CHECK(body->visual->refreshes == refreshes);
    BindBody(body, 5);
    CHECK(body->visual->refreshes == refreshes);

    // A heavy first item: the overlay on the head at once.
    S_Body *heavy = MakeBody("soldier.i3d", 3, 0.2f);
    BindBody(heavy);
    CHECK(heavy->stains->m_visible);
    CHECK(heavy->visual->level.groups[0].material->image == g_stainManager.HeadTexture());
    CHECK(!std::memcmp(heavy->stains->m_buffers[0], STAIN_OVERLAYS[0], 8192));
    // At 0.1 not (only above it).
    S_Body *light = MakeBody("soldier.i3d", 3, 0.1f);
    BindBody(light);
    CHECK(!light->stains->m_visible);
    // No item: none, no crash; no container: the same.
    S_Body *empty = MakeBody();
    empty->container.item = nullptr;
    BindBody(empty);
    CHECK(!empty->stains->m_visible);
    S_Body *noContainer = MakeBody();
    noContainer->human()->m_containers = nullptr;
    BindBody(noContainer);
    CHECK(!noContainer->stains->m_visible);
    // Without a level: nothing past it.
    S_Body *noMesh = MakeBody();
    *reinterpret_cast<uint32_t *>(noMesh->visual->bytes + 0x1D0) = 0;
    BindBody(noMesh);
    CHECK(noMesh->stains->m_level == nullptr && noMesh->stains->m_buffers[0] == nullptr);

    // Paint: role 1 only, in the mission kinds; by turns; 1..4 and 5..8.
    Fresh();
    g_stainManager.Init(0);
    g_missionVariant = 1;
    int seen[4];
    for (int i = 0; i < 4; ++i) {
        S_Body *soldier = MakeBody("soldier.i3d", 1);
        BindBody(soldier);
        seen[i] = soldier->stains->m_paint;
    }
    CHECK(seen[0] == 2 && seen[1] == 3 && seen[2] == 4 && seen[3] == 1);
    g_stainManager.m_paintCounter = 0;
    g_missionVariant = 2;
    S_Body *painted = MakeBody("soldier.i3d", 1);
    BindBody(painted);
    CHECK(painted->stains->m_paint == 6);
    CHECK(painted->visual->level.groups[0].material->image == g_stainManager.PaintTexture(5));
    // (Blended onto nothing: the image, its fully transparent texels emptied.)
    bool paintSame = true;
    for (int i = 0; i < 4096; ++i) {
        const uint16_t texel = STAIN_OVERLAYS[6][i];
        if (painted->stains->m_buffers[0][i] != ((texel >> 12) ? texel : 0))
            paintSame = false;
    }
    CHECK(paintSame);
    CHECK(painted->visual->level.groups[1].material->image == g_stainManager.NullTexture());
    // Painted: the head's overlay is not asked for.
    painted->stains->SetVisible(true);
    CHECK(!painted->stains->m_visible);
    S_Body *enemy = MakeBody("soldier.i3d", 2);
    BindBody(enemy);
    CHECK(enemy->stains->m_paint == 0);
    g_missionVariant = 3;
    S_Body *otherKind = MakeBody("soldier.i3d", 1);
    BindBody(otherKind);
    CHECK(otherKind->stains->m_paint == 0);
}

void TestVisible()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    BindBody(body);
    C_HumanStains *stains = body->stains;
    S_FakeMaterial *material = body->visual->level.groups[0].material;
    // A stain under it: blended by the alphas; off: back to the clean copy.
    stains->m_buffers[0][10] = 0xF123;
    stains->m_clean[10] = 0xF123;
    stains->SetVisible(true);
    CHECK(stains->m_visible);
    CHECK(material->image == g_stainManager.HeadTexture());
    const uint16_t over = STAIN_OVERLAYS[0][10];
    const unsigned oa = over >> 12, ua = 0xF, sum = oa + ua;
    const uint16_t expected = sum == 0 ? 0 : static_cast<uint16_t>(((oa > ua ? oa : ua) << 12) | (((0x100u * ua + (over & 0xF00u) * oa) / sum) & 0xF00u) | (((0x20u * ua + (over & 0xF0u) * oa) / sum) & 0xF0u) | (((0x3u * ua + (over & 0xFu) * oa) / sum) & 0xFu));
    CHECK(stains->m_buffers[0][10] == expected);
    // On again: nothing.
    const uint16_t kept = stains->m_buffers[0][20];
    stains->SetVisible(true);
    CHECK(stains->m_buffers[0][20] == kept);
    stains->SetVisible(false);
    CHECK(!stains->m_visible);
    CHECK(stains->m_buffers[0][10] == 0xF123 && stains->m_buffers[0][11] == 0);
    CHECK(material->image == g_stainManager.NullTexture());
    // With a lent texture: its pixels instead of the material.
    stains->TakeTextures();
    CHECK(stains->m_ownTextures);
    IImage *const lent = stains->m_textures[0];
    CHECK(lent != nullptr && material->image == lent);
    Image(lent)->pixels[11] = 0;
    stains->SetVisible(true);
    CHECK(material->image == lent);
    CHECK(Image(lent)->pixels[11] == stains->m_buffers[0][11] && Image(lent)->unlocks >= 2);
    delete stains;
    CHECK(g_stainManager.m_borrowers[0] == nullptr);
}

void TestPaintSplat()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    BindBody(body);
    C_HumanStains *stains = body->stains;
    uint16_t buffer[64 * 64] = {};
    uint16_t image[4 * 4];
    for (uint16_t &texel : image) texel = 0xF111;
    // Centered: from x - w/2, y - h/2.
    stains->PaintSplat(buffer, nullptr, 10, 20, image, 4, 4);
    CHECK(buffer[(18 << 6) + 8] == 0xF111 && buffer[(21 << 6) + 11] == 0xF111);
    CHECK(buffer[(17 << 6) + 8] == 0 && buffer[(18 << 6) + 7] == 0 && buffer[(22 << 6) + 11] == 0 && buffer[(21 << 6) + 12] == 0);
    CHECK(CountChanged(buffer) == 16);
    // Over the edges: only what falls inside.
    std::memset(buffer, 0, sizeof(buffer));
    stains->PaintSplat(buffer, nullptr, 0, 0, image, 4, 4);
    CHECK(CountChanged(buffer) == 4 && buffer[0] == 0xF111 && buffer[(1 << 6) + 1] == 0xF111);
    std::memset(buffer, 0, sizeof(buffer));
    stains->PaintSplat(buffer, nullptr, 63, 63, image, 4, 4);
    CHECK(CountChanged(buffer) == 9 && buffer[(61 << 6) + 61] == 0xF111 && buffer[(63 << 6) + 63] == 0xF111);
    std::memset(buffer, 0, sizeof(buffer));
    stains->PaintSplat(buffer, nullptr, 100, 10, image, 4, 4);
    CHECK(CountChanged(buffer) == 0);
    // The image's order: rows, then columns.
    std::memset(buffer, 0, sizeof(buffer));
    uint16_t ramp[4];
    ramp[0] = 0xF001; ramp[1] = 0xF002; ramp[2] = 0xF003; ramp[3] = 0xF004;
    stains->PaintSplat(buffer, nullptr, 1, 1, ramp, 2, 2);
    CHECK(buffer[0] == 0xF001 && buffer[1] == 0xF002 && buffer[64] == 0xF003 && buffer[65] == 0xF004);
    // Over a stain: by the alphas; both transparent: 0.
    uint16_t half = 0x8880;
    buffer[0] = 0x8000;
    stains->PaintSplat(buffer, nullptr, 0, 0, &half, 1, 1);
    CHECK(buffer[0] == 0x8440);
    uint16_t clear = 0x0FFF;
    buffer[0] = 0x0EEE;
    stains->PaintSplat(buffer, nullptr, 0, 0, &clear, 1, 1);
    CHECK(buffer[0] == 0);
    uint16_t strong = 0xC0F0;
    buffer[0] = 0x4F00;
    stains->PaintSplat(buffer, nullptr, 0, 0, &strong, 1, 1);
    CHECK(buffer[0] == 0xC3B0);
    // To the texture as well: all of it.
    uint16_t texture[64 * 64] = {};
    buffer[1000] = 0x1234;
    stains->PaintSplat(buffer, texture, 30, 30, image, 2, 2);
    CHECK(texture[1000] == 0x1234 && texture[(29 << 6) + 29] == 0xF111);
}

void TestHits()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    BindBody(body);
    C_HumanStains *stains = body->stains;
    *reinterpret_cast<uint32_t *>(g_gameBytes + 0x2C) = 12345;
    // No flags, no mesh: nothing.
    const S_vector front(0, 0, 1), negFront(0, 0, -1), right(1, 0, 0);
    stains->AddHit(S_vector(0, 0, 0), front, 0);
    CHECK(stains->m_lastHitTime == 0);

    struct { uint32_t flags; S_vector dir; float x; int group1, group2; } cases[] = {
        {1, front, 0.4f, 0, -1},
        {2, front, 50.4f, 5, -1},
        {4, front, 60.4f, 6, -1},
        {8, negFront, 10.4f, 1, -1},
        {8, front, 20.4f, 2, -1},
        {8, right, 10.4f, 1, 2},
        {0x10, negFront, 30.4f, 3, -1},
        {0x10, front, 40.4f, 4, -1},
        {0x10, right, 30.4f, 3, 4},
        {0x20, negFront, 30.4f, 3, -1},
        {0x20, front, 40.4f, 4, -1},
        {0x20, S_vector(-1, 0, 0), 30.4f, 3, 4},
    };
    for (auto &one : cases) {
        S_Body *b = MakeBody();
        BindBody(b);
        b->stains->AddHit(S_vector(one.x, 0.4f, 0), one.dir, one.flags);
        for (int g = 0; g < 7; ++g) {
            const bool expected = g == one.group1 || g == one.group2;
            CHECK((b->stains->m_painted[g] != 0) == expected);
            // (The second group: its nearest vertex is far from the point here, and the splat lands outside the picture.)
            if (g != one.group2)
                CHECK((CountChanged(b->stains->m_buffers[g]) != 0) == expected);
        }
        CHECK(b->stains->m_lastHitTime == 12345);
        CHECK(b->stains->m_ownTextures);
        CHECK(b->visual->level.unlocks >= 1);
        // The head's hit goes on the clean copy too, others not.
        CHECK((CountChanged(b->stains->m_clean) != 0) == (one.group1 == 0));
        delete b->stains;
    }
    // Other bits: nothing painted, but the time not touched either.
    S_Body *none = MakeBody();
    BindBody(none);
    none->stains->AddHit(S_vector(1, 0, 0), front, 3);
    CHECK(none->stains->m_lastHitTime == 0);
    none->stains->AddHit(S_vector(1, 0, 0), front, 0x40);
    CHECK(none->stains->m_lastHitTime == 0);
    delete none->stains;

    // The splat around the UV of the point: nearest the second vertex (11, 0): u 0.5, v 0.25 -> (32, 16).
    S_Body *where = MakeBody();
    BindBody(where);
    where->stains->AddHit(S_vector(11.0f, 0.0f, 0), negFront, 8);
    const uint16_t *paintBuffer = where->stains->m_buffers[1];
    bool inside = true;
    int count = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (paintBuffer[(y << 6) + x]) {
                ++count;
                if (x < 32 - 8 || x > 32 + 7 || y < 16 - 8 || y > 16 + 7)
                    inside = false;
            }
        }
    }
    CHECK(count > 0 && inside);
    // On the lent texture as well; unlocked.
    S_FakeImage *lent = Image(where->stains->m_textures[1]);
    CHECK(!std::memcmp(lent->pixels, paintBuffer, 8192));
    CHECK(lent->unlocks == lent->locks);
    delete where->stains;

    // Local to the model: the point is taken into its frame.
    S_Body *moved = MakeBody();
    float *matrix = reinterpret_cast<float *>(moved->visual->bytes + 0x40);
    matrix[12] = 100.0f;
    BindBody(moved);
    moved->stains->AddHit(S_vector(111.0f, 0.0f, 0), negFront, 8);
    S_Body *unmoved = MakeBody();
    BindBody(unmoved);
    std::srand(3);
    moved->stains->AddHit(S_vector(111.0f, 0.0f, 0), negFront, 8);
    std::srand(3);
    unmoved->stains->AddHit(S_vector(11.0f, 0.0f, 0), negFront, 8);
    CHECK(CountChanged(unmoved->stains->m_buffers[1]) > 0);
    delete moved->stains;
    delete unmoved->stains;

    // Along the edges: at a point off the vertex the UV moves along the two edges' directions: u + 0.25 * d1, v + 0.25 * d2 (unit-long edges here).
    S_Body *along = MakeBody();
    BindBody(along);
    const S_StainVertex *vertex = nullptr;
    for (const S_StainVertex *v : along->stains->m_mesh->groups[1].vertices)
        if (v->index == 4) vertex = v;
    CHECK(vertex != nullptr && vertex->edges.size() >= 2);
    if (vertex) {
        along->stains->PaintHit(*vertex, S_vector(10.5f, 0.5f, 0), &along->visual->positions[0][0], 1);
        const uint16_t *buffer = along->stains->m_buffers[1];
        int minX = 64, maxX = -1, minY = 64, maxY = -1;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (buffer[(y << 6) + x]) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
        // The splat's middle is near (0.25 + 0.5 * 0.25 ...) * 64 = 24, in both, within its 16 texels.
        CHECK(minX >= 24 - 8 && maxX <= 24 + 7 && minY >= 24 - 8 && maxY <= 24 + 7);
        // One edge only: the vertex's own UV.
        S_StainVertex single;
        single.index = 4;
        S_Body *one = MakeBody();
        BindBody(one);
        one->stains->PaintHit(single, S_vector(50, 50, 0), &one->visual->positions[0][0], 1);
        const uint16_t *oneBuffer = one->stains->m_buffers[1];
        bool around = CountChanged(oneBuffer) > 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (oneBuffer[(y << 6) + x] && (x < 16 - 8 || x > 16 + 7 || y < 16 - 8 || y > 16 + 7))
                    around = false;
        CHECK(around);
    }

    // Another pitch: it stays locked and nothing goes on the texture or in the buffer (kept).
    S_Body *odd = MakeBody();
    BindBody(odd);
    odd->stains->TakeTextures();
    S_FakeImage *image = Image(odd->stains->m_textures[1]);
    image->pitch = 100;
    const int unlocks = image->unlocks;
    odd->stains->AddHit(S_vector(10.4f, 0.4f, 0), negFront, 8);
    CHECK(image->unlocks == unlocks && CountChanged(odd->stains->m_buffers[1]) == 0);
    CHECK(odd->stains->m_painted[1] == 1);
    image->pitch = 64 * 2;
    delete odd->stains;
}

void TestLending()
{
    Fresh();
    g_stainManager.Init(0);
    *reinterpret_cast<uint32_t *>(g_gameBytes + 0x2C) = 1000;
    // Eight bodies have textures; the ninth takes those of the one hit longest ago.
    S_Body *bodies[9];
    for (int i = 0; i < 9; ++i) {
        bodies[i] = MakeBody();
        BindBody(bodies[i]);
    }
    for (int i = 0; i < 8; ++i) {
        *reinterpret_cast<uint32_t *>(g_gameBytes + 0x2C) = 2000 + (i == 3 ? 0 : 100 * i + 100);
        bodies[i]->stains->AddHit(S_vector(10.4f, 0.4f, 0), S_vector(0, 0, -1), 8);
        CHECK(bodies[i]->stains->m_ownTextures);
        CHECK(g_stainManager.m_borrowers[i] == bodies[i]->stains);
    }
    IImage *const fourth = bodies[3]->stains->m_textures[1];
    CHECK(fourth != nullptr);
    *reinterpret_cast<uint32_t *>(g_gameBytes + 0x2C) = 9000;
    bodies[8]->stains->AddHit(S_vector(10.4f, 0.4f, 0), S_vector(0, 0, -1), 8);
    CHECK(bodies[8]->stains->m_ownTextures);
    CHECK(g_stainManager.m_borrowers[3] == bodies[8]->stains);
    CHECK(bodies[8]->stains->m_textures[1] == fourth);
    // The one it took them from is back to the plain ones, its stains kept in memory.
    CHECK(!bodies[3]->stains->m_ownTextures && bodies[3]->stains->m_textures[1] == nullptr);
    CHECK(bodies[3]->visual->level.groups[1].material->image == g_stainManager.NullTexture());
    CHECK(CountChanged(bodies[3]->stains->m_buffers[1]) > 0);
    // Its next hit takes them back from the next oldest; its picture goes with it.
    *reinterpret_cast<uint32_t *>(g_gameBytes + 0x2C) = 9100;
    bodies[3]->stains->AddHit(S_vector(10.4f, 0.4f, 0), S_vector(0, 0, -1), 8);
    CHECK(bodies[3]->stains->m_ownTextures);
    CHECK(g_stainManager.m_borrowers[0] == bodies[3]->stains);
    CHECK(!std::memcmp(Image(bodies[3]->stains->m_textures[1])->pixels, bodies[3]->stains->m_buffers[1], 8192));
    // Asked again: the same row.
    IImage **row = g_stainManager.AcquireTextures(bodies[3]->stains);
    CHECK(row == g_stainManager.m_lent[0]);
    CHECK(g_stainManager.AcquireTextures(nullptr) == nullptr);
    // Released: a free place is taken first.
    g_stainManager.ReleaseTextures(bodies[5]->stains);
    CHECK(g_stainManager.m_borrowers[5] == nullptr);
    CHECK(g_stainManager.AcquireTextures(bodies[1]->stains) == g_stainManager.m_lent[1]);
    C_HumanStains *const borrowedFirst = g_stainManager.m_borrowers[2];
    S_Body *extra = MakeBody();
    BindBody(extra);
    CHECK(g_stainManager.AcquireTextures(extra->stains) == g_stainManager.m_lent[5]);
    CHECK(g_stainManager.m_borrowers[2] == borrowedFirst);
    // A manager Init on a borrower: it gives them back.
    g_stainManager.m_initialized = false;
    g_stainManager.Init(0);
    CHECK(!bodies[2]->stains->m_ownTextures);
    // Bound in mode 1: takes them anew.
    S_Body *mode = MakeBody();
    BindBody(mode);
    mode->stains->TakeTextures();
    const int locks = Image(mode->stains->m_textures[0])->locks;
    BindBody(mode, 1);
    CHECK(mode->stains->m_ownTextures && Image(mode->stains->m_textures[0])->locks == locks + 1);
    // The base textures: painted head stays painted, others null.
    Fresh();
    g_stainManager.Init(0);
    g_missionVariant = 1;
    S_Body *painted = MakeBody("soldier.i3d", 1);
    BindBody(painted);
    painted->stains->TakeTextures();
    painted->stains->GiveBackTextures();
    CHECK(!painted->stains->m_ownTextures);
    CHECK(painted->visual->level.groups[0].material->image == g_stainManager.PaintTexture(painted->stains->m_paint - 1));
    CHECK(painted->visual->level.groups[2].material->image == g_stainManager.NullTexture());
    CHECK(g_stainManager.m_borrowers[0] == nullptr);
    g_missionVariant = 0;
    S_Body *visible = MakeBody();
    BindBody(visible);
    visible->stains->SetVisible(true);
    visible->stains->TakeTextures();
    visible->stains->ReturnTextures();
    CHECK(visible->visual->level.groups[0].material->image == g_stainManager.HeadTexture());
    CHECK(!visible->stains->m_ownTextures);
}

void TestSaveLoad()
{
    Fresh();
    g_stainManager.Init(0);
    *reinterpret_cast<uint32_t *>(g_gameBytes + 0x2C) = 777;
    S_Body *body = MakeBody();
    BindBody(body);
    C_HumanStains *stains = body->stains;
    stains->AddHit(S_vector(0.4f, 0.4f, 0), S_vector(0, 0, 1), 1);
    stains->AddHit(S_vector(20.4f, 0.4f, 0), S_vector(0, 0, 1), 8);
    C_chunk out;
    out.Open("x", true);
    stains->SaveGame(out);
    out.Close();
    // Id, ten values, then per group a flag and its picture if painted, then the clean copy.
    CHECK(static_cast<unsigned char>(g_file[0]) == 0x0D && static_cast<unsigned char>(g_file[1]) == 0xB1);
    CHECK(g_file.size() == 6 + 10 * 4 + 7 * 4 + 2 * 8192 + 8192);
    int width;
    std::memcpy(&width, g_file.data() + 6 + 3 * 4, 4);
    CHECK(width == 64);
    uint32_t time;
    std::memcpy(&time, g_file.data() + 6 + 9 * 4, 4);
    CHECK(time == 777);
    const std::string saved = g_file;

    S_Body *back = MakeBody();
    BindBody(back);
    C_chunk in;
    in.Open("x", false);
    back->stains->LoadGame(in);
    in.Close();
    CHECK(back->stains->m_ownTextures == stains->m_ownTextures);
    CHECK(back->stains->m_lastHitTime == 777);
    CHECK(back->stains->m_painted[0] == 1 && back->stains->m_painted[2] == 1 && back->stains->m_painted[1] == 0);
    CHECK(!std::memcmp(back->stains->m_buffers[0], stains->m_buffers[0], 8192));
    CHECK(!std::memcmp(back->stains->m_buffers[2], stains->m_buffers[2], 8192));
    CHECK(!std::memcmp(back->stains->m_clean, stains->m_clean, 8192));
    CHECK(g_at == static_cast<int>(saved.size()));
    C_chunk again;
    again.Open("x", true);
    back->stains->SaveGame(again);
    again.Close();
    CHECK(g_file == saved);

    // Flags: the overlay, the paint.
    S_Body *flags = MakeBody("soldier.i3d", 3, 1.0f);
    BindBody(flags);
    C_chunk flagsOut;
    flagsOut.Open("x", true);
    flags->stains->SaveGame(flagsOut);
    flagsOut.Close();
    S_Body *flagsBack = MakeBody();
    BindBody(flagsBack);
    C_chunk flagsIn;
    flagsIn.Open("x", false);
    flagsBack->stains->LoadGame(flagsIn);
    flagsIn.Close();
    CHECK(flagsBack->stains->m_visible);

    // Skipped: past it whole, whoever reads on.
    g_file = saved;
    g_file += "after";
    C_chunk skip;
    skip.Open("x", false);
    C_HumanStains::SkipSaved(skip);
    CHECK(g_at == static_cast<int>(saved.size()));
    skip.Close();
    // Another chunk: passed over, nothing taken.
    C_chunk other;
    other.Open("x", true);
    other.WriteChunk<int>(0x1234, 5);
    other.Close();
    S_Body *untouched = MakeBody();
    BindBody(untouched);
    untouched->stains->m_lastHitTime = 5;
    C_chunk otherIn;
    otherIn.Open("x", false);
    untouched->stains->LoadGame(otherIn);
    CHECK(untouched->stains->m_lastHitTime == 5);
    CHECK(g_at == 10);
    otherIn.Close();
}

// The middle of what was painted, in texels.
bool PaintedAround(const uint16_t *buffer, int centerX, int centerY)
{
    bool any = false;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            if (!buffer[(y << 6) + x])
                continue;
            any = true;
            if (x < centerX - 8 || x > centerX + 7 || y < centerY - 8 || y > centerY + 7)
                return false;
        }
    }
    return any;
}

void TestNearestVertex()
{
    Fresh();
    g_stainManager.Init(0);
    // A hit right at each corner paints around that corner's UV: searched (group 1) and walked (group 0, from random vertices).
    const int centers[4][2] = {{16, 16}, {32, 16}, {16, 32}, {32, 32}};
    for (int corner = 0; corner < 4; ++corner) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::srand(attempt * 7 + corner + 1);
            S_Body *search = MakeBody();
            BindBody(search);
            search->stains->AddHit(S_vector(10.0f + (corner & 1), static_cast<float>(corner >> 1), 0), S_vector(0, 0, -1), 8);
            CHECK(PaintedAround(search->stains->m_buffers[1], centers[corner][0], centers[corner][1]));
            delete search->stains;
            S_Body *walk = MakeBody();
            BindBody(walk);
            walk->stains->AddHit(S_vector(0.0f + (corner & 1), static_cast<float>(corner >> 1), 0), S_vector(0, 0, 1), 1);
            CHECK(PaintedAround(walk->stains->m_buffers[0], centers[corner][0], centers[corner][1]));
            CHECK(PaintedAround(walk->stains->m_clean, centers[corner][0], centers[corner][1]));
            delete walk->stains;
        }
    }
    // Edges of length 2 count as unit axes: 1 m along the edge moves the UV by its whole difference.
    S_Body *scaled = MakeBody();
    BindBody(scaled);
    for (int v = 4; v < 8; ++v) {
        scaled->visual->positions[v][0] = 10.0f + 2.0f * (v & 1);
        scaled->visual->positions[v][1] = 2.0f * ((v - 4) >> 1);
    }
    const S_StainVertex *vertex = nullptr;
    for (const S_StainVertex *v : scaled->stains->m_mesh->groups[1].vertices)
        if (v->index == 4) vertex = v;
    CHECK(vertex != nullptr);
    if (vertex) {
        // One metre along the first edge (toward 5: +u), two along the second (toward 6: +v).
        scaled->stains->PaintHit(*vertex, S_vector(11.0f, 2.0f, 0), &scaled->visual->positions[0][0], 1);
        CHECK(PaintedAround(scaled->stains->m_buffers[1], 32, 48));
    }
    delete scaled->stains;

    // The vertex's own UV counts for u and for v alike.
    S_Body *offset = MakeBody();
    BindBody(offset);
    S_FakeLevel &level = offset->visual->level;
    level.uvs[4 * 2] = 0.75f;
    level.uvs[4 * 2 + 1] = 0.125f;
    S_StainVertex single;
    single.index = 4;
    offset->stains->PaintHit(single, S_vector(0, 0, 0), &offset->visual->positions[0][0], 1);
    CHECK(PaintedAround(offset->stains->m_buffers[1], 48, 8));
    delete offset->stains;

    // A rotated model: the point is in its axes, and the side of the hit too.
    S_Body *turned = MakeBody();
    float *matrix = reinterpret_cast<float *>(turned->visual->bytes + 0x40);
    matrix[0] = 0; matrix[1] = 1; matrix[2] = 0;        // (x axis along world y)
    matrix[4] = -1; matrix[5] = 0; matrix[6] = 0;       // (y axis along -x)
    BindBody(turned);
    // Local (11, 1, 0) is world (-1, 11, 0).
    turned->stains->AddHit(S_vector(-1.0f, 11.0f, 0), S_vector(0, 0, -1), 8);
    CHECK(PaintedAround(turned->stains->m_buffers[1], 32, 32));
    // Along world y is its x axis, so a side: both groups marked.
    S_Body *sided = MakeBody();
    float *sidedMatrix = reinterpret_cast<float *>(sided->visual->bytes + 0x40);
    sidedMatrix[0] = 0; sidedMatrix[1] = 1; sidedMatrix[2] = 0;
    sidedMatrix[4] = -1; sidedMatrix[5] = 0; sidedMatrix[6] = 0;
    BindBody(sided);
    sided->stains->AddHit(S_vector(0, 10.0f, 0), S_vector(0, 1, 0), 8);
    CHECK(sided->stains->m_painted[1] == 1 && sided->stains->m_painted[2] == 1);
    S_Body *zAxis = MakeBody();
    float *zMatrix = reinterpret_cast<float *>(zAxis->visual->bytes + 0x40);
    zMatrix[8] = 0; zMatrix[9] = 1; zMatrix[10] = 0;    // (z axis along world y)
    BindBody(zAxis);
    zAxis->stains->AddHit(S_vector(10, 0, 0), S_vector(0, 1, 0), 8);
    CHECK(zAxis->stains->m_painted[2] == 1 && zAxis->stains->m_painted[1] == 0);
    // Equal on both axes: a side.
    S_Body *equal = MakeBody();
    BindBody(equal);
    equal->stains->AddHit(S_vector(10, 0, 0), S_vector(1, 0, 1), 8);
    CHECK(equal->stains->m_painted[1] == 1 && equal->stains->m_painted[2] == 1);

    // The first found keeps its place in the mesh: a second vertex welded at another's place joins the two.
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody("edges.i3d");
    g_childOf = body->visual->frame();
    S_StainMesh *mesh = g_stainManager.AcquireMesh(body->model());
    const S_StainGroup &group = mesh->groups[2];
    CHECK(group.vertices.size() == 4 && group.vertices[3]->index == 11);
    int edgesAt[4] = {};
    for (int i = 0; i < 4; ++i)
        edgesAt[i] = static_cast<int>(group.vertices[i]->edges.size());
    CHECK(edgesAt[0] == 2 && edgesAt[1] == 3 && edgesAt[2] == 3 && edgesAt[3] == 2);
    for (const S_StainEdge *edge : group.edges) {
        CHECK(edge->otherFace == -1 || edge->otherFace == 1);
        CHECK(edge->face == 0 || edge->face == 1);
    }
    // Extremes each in their place, for all axes of a vertex list in which they differ.
    S_Body *extreme = MakeBody("extreme.i3d");
    S_Vertex32 *vertices = extreme->visual->level.vertices;
    vertices[0].x = 0; vertices[0].y = 5; vertices[0].z = 2;
    vertices[1].x = 3; vertices[1].y = 1; vertices[1].z = -2;
    vertices[2].x = -4; vertices[2].y = 0; vertices[2].z = 7;
    g_childOf = extreme->visual->frame();
    S_StainMesh *extremeMesh = g_stainManager.AcquireMesh(extreme->model());
    const S_StainGroup &first = extremeMesh->groups[0];
    auto At = [&](unsigned position) { return first.vertices[position]->index; };
    CHECK(At(first.minX) == 2 && At(first.minY) == 2 && At(first.maxY) == 0);
    CHECK(At(first.minZ) == 1 && At(first.maxZ) == 2);
}

void TestPicturesKept()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    BindBody(body);
    C_HumanStains *stains = body->stains;
    // A texel the overlay does colour.
    int texel = 0;
    while (texel < 4096 && (STAIN_OVERLAYS[0][texel] >> 12) == 0)
        ++texel;
    CHECK(texel < 4096);
    stains->m_clean[texel] = 0x1234;
    stains->m_buffers[0][texel] = 0x1234;
    stains->m_buffers[0][4095] = 0x7777;
    stains->m_clean[4095] = 0x7777;
    stains->SetVisible(true);
    CHECK(stains->m_buffers[0][texel] != 0x1234);
    CHECK(stains->m_buffers[3][texel] == 0);
    stains->SetVisible(false);
    CHECK(stains->m_buffers[0][texel] == 0x1234 && stains->m_buffers[0][4095] == 0x7777);
    // The whole picture goes to a lent texture.
    stains->TakeTextures();
    for (int g = 0; g < 7; ++g)
        CHECK(!std::memcmp(Image(stains->m_textures[g])->pixels, stains->m_buffers[g], 8192));
    // From first to last texel, while on.
    stains->SetVisible(true);
    CHECK(!std::memcmp(Image(stains->m_textures[0])->pixels, stains->m_buffers[0], 8192));
    CHECK(stains->m_buffers[0][0] == (((STAIN_OVERLAYS[0][0] >> 12) != 0) ? STAIN_OVERLAYS[0][0] : 0));
    stains->SetVisible(false);
    CHECK(!std::memcmp(Image(stains->m_textures[0])->pixels, stains->m_clean, 8192));
    // Paint changes: from the clean copy; 0 back to it.
    stains->SetPaint(3);
    CHECK(stains->m_paint == 3);
    CHECK(stains->m_buffers[0][texel] != 0x1234 || (STAIN_OVERLAYS[3][texel] >> 12) == 0);
    CHECK(!std::memcmp(Image(stains->m_textures[0])->pixels, stains->m_buffers[0], 8192));
    stains->SetPaint(5);
    S_Body *fresh = MakeBody();
    BindBody(fresh);
    std::memcpy(fresh->stains->m_clean, stains->m_clean, 8192);
    fresh->stains->SetPaint(5);
    CHECK(!std::memcmp(fresh->stains->m_buffers[0], stains->m_buffers[0], 8192));
    stains->SetPaint(0);
    CHECK(stains->m_paint == 0 && !std::memcmp(stains->m_buffers[0], stains->m_clean, 8192));
    // Without own textures: the material follows.
    S_Body *plain = MakeBody();
    BindBody(plain);
    plain->stains->SetPaint(2);
    CHECK(plain->visual->level.groups[0].material->image == g_stainManager.PaintTexture(1));
    plain->stains->SetPaint(0);
    CHECK(plain->visual->level.groups[0].material->image == g_stainManager.NullTexture());
    // A new bind starts clean: its paint marks and time gone, the head's picture rewritten.
    stains->m_lastHitTime = 9;
    std::memset(stains->m_painted, 1, sizeof(stains->m_painted));
    stains->m_visible = true;
    BindBody(body);
    CHECK(stains->m_lastHitTime == 0 && stains->m_painted[0] == 0 && stains->m_painted[6] == 0);
    CHECK(!stains->m_visible);
    CHECK(CountChanged(stains->m_clean) == 0);
    // Rebound while the head has its overlay: the buffer starts from it.
    S_Body *over = MakeBody();
    BindBody(over);
    over->stains->m_visible = true;
    std::memset(over->stains->m_buffers[0], 0xFF, 8192);
    over->stains->PrepareBuffers();
    CHECK(!std::memcmp(over->stains->m_buffers[0], STAIN_OVERLAYS[0], 8192));
    CHECK(CountChanged(over->stains->m_buffers[1]) == 0);
    // Rebound painted: from the paint.
    over->stains->m_paint = 4;
    over->stains->PrepareBuffers();
    CHECK(!std::memcmp(over->stains->m_buffers[0], STAIN_OVERLAYS[4], 8192));
    // Saved: paint and overlay; the clean copy read back.
    S_Body *saved = MakeBody();
    BindBody(saved);
    saved->stains->SetPaint(7);
    saved->stains->AddHit(S_vector(0.4f, 0.4f, 0), S_vector(0, 0, 1), 1);
    saved->stains->m_clean[77] = 0x4321;
    C_chunk out;
    out.Open("x", true);
    saved->stains->SaveGame(out);
    out.Close();
    S_Body *loaded = MakeBody();
    BindBody(loaded);
    C_chunk in;
    in.Open("x", false);
    loaded->stains->LoadGame(in);
    in.Close();
    CHECK(loaded->stains->m_paint == 7 && loaded->stains->m_clean[77] == 0x4321);
    CHECK(loaded->stains->m_painted[1] == 0);
    // Init on a body's textures: the old ones released, a mesh list emptied, the counter back.
    g_stainManager.m_paintCounter = 9;
    g_stainManager.Done();
    CHECK(g_stainManager.m_meshes.empty());
    for (int b = 0; b < STAIN_LENT_BODIES; ++b)
        CHECK(g_stainManager.m_borrowers[b] == nullptr);
    g_stainManager.Init(0);
    CHECK(g_stainManager.m_paintCounter == 0);
}

void TestDestruction()
{
    Fresh();
    g_stainManager.Init(0);
    S_Body *body = MakeBody();
    BindBody(body);
    S_StainMesh *mesh = body->stains->m_mesh;
    S_Body *second = MakeBody();
    BindBody(second);
    CHECK(mesh->refCount == 2);
    body->stains->TakeTextures();
    delete body->stains;
    CHECK(mesh->refCount == 1);
    CHECK(g_stainManager.m_borrowers[0] == nullptr);
    delete second->stains;
    CHECK(g_stainManager.m_meshes.empty());
    // Mode 2 releases the old mesh before taking.
    S_Body *rebound = MakeBody();
    BindBody(rebound);
    S_StainMesh *first = rebound->stains->m_mesh;
    rebound->name = "new.i3d";
    *reinterpret_cast<const char **>(rebound->modelBytes + 0x134) = rebound->name.c_str();
    BindBody(rebound, 2);
    (void)first;
    CHECK(g_stainManager.m_meshes.size() == 1 && !std::strcmp(g_stainManager.m_meshes[0]->name, "new.i3d"));
}
}  // namespace

C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_graph); }
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_imageVt[0] = reinterpret_cast<void *>(F_ImageRelease);
    g_imageVt[0x14 / 4] = reinterpret_cast<void *>(F_ImageLock);
    g_imageVt[0x18 / 4] = reinterpret_cast<void *>(F_ImageUnlock);
    g_graphVt[0x3C / 4] = reinterpret_cast<void *>(F_LoadTexture);
    g_driverVt[0x18 / 4] = reinterpret_cast<void *>(F_SetState);
    g_driverVt[0x1C / 4] = reinterpret_cast<void *>(F_GetState);
    g_materialVt[0x2C / 4] = reinterpret_cast<void *>(F_MaterialImage);
    g_materialVt[0x30 / 4] = reinterpret_cast<void *>(F_Material30);
    g_materialVt[0x34 / 4] = reinterpret_cast<void *>(F_Material34);
    g_groupVt[0x0C / 4] = reinterpret_cast<void *>(F_GroupLock);
    g_groupVt[0x18 / 4] = reinterpret_cast<void *>(F_GroupDuplicate);
    g_levelVt[0x04 / 4] = reinterpret_cast<void *>(F_LevelVertices);
    g_levelVt[0x08 / 4] = reinterpret_cast<void *>(F_LevelUnlock);
    g_levelVt[0x14 / 4] = reinterpret_cast<void *>(F_LevelGroup);
    g_levelVt[0x38 / 4] = reinterpret_cast<void *>(F_LevelDetail);
    g_levelVt[0x3C / 4] = reinterpret_cast<void *>(F_LevelMakeUVs);
    g_meshVt[0x04 / 4] = reinterpret_cast<void *>(F_MeshLevel);
    g_meshVt[0x18 / 4] = reinterpret_cast<void *>(F_Mesh18);
    g_visualVt[0xA0 / 4] = reinterpret_cast<void *>(F_VisualDuplicate);
    g_visualVt[0xBC / 4] = reinterpret_cast<void *>(F_VisualPositions);
    g_visualVt[0xC4 / 4] = reinterpret_cast<void *>(F_VisualRefresh);
    g_visualVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    g_modelVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    for (void *&slot : g_itemVt) slot = reinterpret_cast<void *>(F_ItemWeight);
    for (void *&slot : g_containerVt) slot = reinterpret_cast<void *>(F_ContainerItem);

    TestManagerTextures();
    TestMeshBuilding();
    TestBind();
    TestVisible();
    TestPaintSplat();
    TestHits();
    TestLending();
    TestSaveLoad();
    TestDestruction();
    TestNearestVertex();
    TestPicturesKept();
    std::printf("stains: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
