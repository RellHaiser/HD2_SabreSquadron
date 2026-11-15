// Part 30 of the human harness (HUMAN_STAINS builds, build_stains.bat; included by human_test.cpp after
// locomotion3_test.inc): the blood stains (game/human_stains.cpp) against the original's (0x00796890..0x0079ad10).
// Part 30a: the manager (Init, Reset, Done, AcquireMesh and its mesh graph, ReleaseMesh).
//
// The engine's objects are fakes in its layout (images, the IGraph's LoadTexture, the driver's states, materials, face
// groups, mesh levels, meshes, visuals, models), every call logged. Each side has its own copy of them (a world built
// twice from one seed, the same names), since the calls change them; after each call both worlds are compared. The
// original's I3DGetDriver and GetIGraph (import thunks) answer the same fakes; its manager is its singleton 0x0098adf0,
// its meshes its own allocations (MSVC6 vectors: described as text, the port's alike).
#define private public
#define protected public
#include "game/human_stains.h"
#include "ls3d/igraph.h"
#undef private
#undef protected
extern int g_missionVariant;                            // (defined below; the original's 0x0098a6b0)
namespace {

#define SN __attribute__((thiscall))

int g_sSide = 0;                                        // 0: the port's call, 1: the original's
bool g_sOn = false;
void SLog(const void *self, const std::string &what, std::initializer_list<double> nums = {})
{
    if (g_sOn)
        Log(self, what.c_str(), nums);
}

// ---- the engine's fakes (COM style: stdcall, `this` on the stack)
struct S_SImage {
    void **vt;
    uint8_t pad[0x1C];
    uint16_t width = 64, height = 64;                   // +0x20, +0x22
    int pitch = 128;
    std::vector<uint16_t> pixels = std::vector<uint16_t>(64 * 64, 0);
    int releases = 0, locks = 0, unlocks = 0;
};
void *g_sImageVt[16], *g_sGraphVt[32], *g_sDriverVt[32], *g_sMaterialVt[32], *g_sGroupVt[16], *g_sLevelVt[32],
    *g_sMeshVt[16], *g_sVisualVt[128], *g_sModelVt[128];
int g_sImageCount[2];
std::vector<S_SImage *> g_sImages[2];
uint32_t __stdcall SF_ImageRelease(S_SImage *s)
{
    ++s->releases;
    SLog(s, "Release");
    return 0;
}
void *__stdcall SF_ImageLock(S_SImage *s, int a, int *pitch, int b)
{
    ++s->locks;
    SLog(s, "Lock", {double(a), double(b)});
    *pitch = s->pitch;
    return s->pixels.data();
}
void __stdcall SF_ImageUnlock(S_SImage *s)
{
    ++s->unlocks;
    SLog(s, "Unlock");
}
HRESULT __stdcall SF_LoadTexture(void *, const char *file, const char *alpha, uint32_t mode, IImage **out)
{
    auto *image = new S_SImage();
    image->vt = g_sImageVt;
    g_names[image] = "image" + std::to_string(++g_sImageCount[g_sSide]);
    // (Now and then another pitch: PaintHit paints nothing then. A shorter one: the copies take pitch x height bytes
    // of a body's 64 x 64 picture, and a longer pitch would read past its end on both sides, at times into no memory.)
    image->pitch = g_sImageCount[g_sSide] % 11 == 5 ? 126 : 128;
    image->pixels.assign(size_t(image->pitch / 2) * image->height, 0);
    g_sImages[g_sSide].push_back(image);
    SLog(image, std::string("LoadTexture ") + file + " " + alpha, {double(mode)});
    *out = reinterpret_cast<IImage *>(image);
    return 0;
}
struct { void **vt; } g_sGraph{g_sGraphVt}, g_sDriver{g_sDriverVt};
std::map<int, uint32_t> g_sStates[2];
void __stdcall SF_SetState(void *, int state, uint32_t value)
{
    g_sStates[g_sSide][state] = value;
    SLog(nullptr, "SetState", {double(state), double(value)});
}
uint32_t __stdcall SF_GetState(void *, int state)
{
    SLog(nullptr, "GetState", {double(state)});
    return g_sStates[g_sSide][state];
}
void *SF_GetIGraph() { return &g_sGraph; }
void *SF_GetDriver() { return &g_sDriver; }

struct S_SMaterial {
    void **vt;
    IImage *image = nullptr;
    int mode30 = 0, mode34 = 0;
};
void __stdcall SF_MaterialImage(S_SMaterial *s, IImage *image)
{
    s->image = image;
    SLog(s, "SetImage " + NameOf(image));
}
void __stdcall SF_Material30(S_SMaterial *s, int mode)
{
    s->mode30 = mode;
    SLog(s, "Mode30", {double(mode)});
}
void __stdcall SF_Material34(S_SMaterial *s, int mode)
{
    s->mode34 = mode;
    SLog(s, "Mode34", {double(mode)});
}
struct S_SGroup {
    void **vt;
    S_SMaterial *material;                              // +0x04
    int numFaces;                                       // +0x08
    void *next;
    std::vector<uint16_t> faces;
    int duplicates = 0;
};
uint16_t *__stdcall SF_GroupLock(S_SGroup *s, uint32_t flags)
{
    SLog(s, "Lock", {double(flags)});
    return s->faces.data();
}
void __stdcall SF_GroupDuplicate(S_SGroup *s)
{
    ++s->duplicates;
    SLog(s, "DuplicateMaterial");
}
struct S_SVertex {
    float x, y, z, nx, ny, nz, u, v;
};
struct S_SLevel {
    void **vt;
    uint8_t pad[0x48];
    float *uvs = nullptr;                               // +0x4C
    uint32_t pad50 = 0;
    int numVertices = 0;                                // +0x54
    uint8_t pad2[100 - 0x58];
    uint32_t flags = 0;                                 // +100
    std::vector<S_SVertex> vertices;
    S_SGroup groups[STAIN_GROUPS];
    int unlocks = 0, uvMakes = 0, details = 0;
};
void *__stdcall SF_LevelVertices(S_SLevel *s, int a)
{
    SLog(s, "GetVertices", {double(a)});
    return s->vertices.data();
}
void __stdcall SF_LevelUnlock(S_SLevel *s)
{
    ++s->unlocks;
    SLog(s, "UnlockVertices");
}
I3D_face_group *__stdcall SF_LevelGroup(S_SLevel *s, int index)
{
    SLog(s, "GetFaceGroup", {double(index)});
    return reinterpret_cast<I3D_face_group *>(&s->groups[index]);
}
void __stdcall SF_LevelMakeUVs(S_SLevel *s, int sets)
{
    ++s->uvMakes;
    SLog(s, "MakeUVs", {double(sets)});
    s->uvs = new float[size_t(s->numVertices) * 2 + 2]();
}
void __stdcall SF_LevelDetail(S_SLevel *s, int a, int b, int c)
{
    ++s->details;
    SLog(s, "Detail", {double(a), double(b), double(c)});
}
struct S_SMesh {
    void **vt;
    S_SLevel *level;
    int calls18 = 0;
};
I3D_mesh_level *__stdcall SF_MeshLevel(S_SMesh *s, int index)
{
    SLog(s, "GetLevel", {double(index)});
    return reinterpret_cast<I3D_mesh_level *>(s->level);
}
void __stdcall SF_Mesh18(S_SMesh *s)
{
    ++s->calls18;
    SLog(s, "DuplicateMaterials");
}
// A visual (the model's "a") and its model: frames in the engine's layout.
struct S_SVisual {
    alignas(16) uint8_t bytes[0x400];
    S_SMesh mesh;
    S_SLevel level;
    std::vector<float> positions;                       // 4 floats a vertex
    int duplicates = 0, refreshes = 0;
};
void __stdcall SF_VisualDuplicate(uint8_t *self) { SLog(self, "DuplicateMesh"); }
const float *__stdcall SF_VisualPositions(uint8_t *self, int a)
{
    SLog(self, "Positions", {double(a)});
    return reinterpret_cast<S_SVisual *>(self)->positions.data();
}
void __stdcall SF_VisualRefresh(uint8_t *self)
{
    ++reinterpret_cast<S_SVisual *>(self)->refreshes;
    SLog(self, "Refresh");
}
// (The model's child "a": at +0x300 of the model's bytes.)
I3D_frame *__stdcall SF_FindChild(uint8_t *self, const char *name, uint32_t flags)
{
    SLog(self, std::string("FindChildFrame ") + name, {double(flags)});
    I3D_frame *child;
    std::memcpy(&child, self + 0x300, 4);
    return std::strcmp(name, "a") ? nullptr : child;
}
void __stdcall SF_Unexpected(void *self)
{
    std::printf("unexpected engine call on %s\n", NameOf(self).c_str());
    std::abort();
}

// ---- a world: models with their visuals and meshes (built alike for both sides from one seed)
const int S_MODELS = 6;
struct S_SModel {
    alignas(16) uint8_t bytes[0x400];
    std::string file;
    S_SVisual *visual = nullptr;
};
struct S_SWorld {
    S_SModel models[S_MODELS];
};
S_SWorld *g_sWorld[2];

void NameWorld(S_SWorld &w, const std::string &prefix)
{
    for (int m = 0; m < S_MODELS; ++m) {
        S_SModel &model = w.models[m];
        const std::string n = prefix + "model" + std::to_string(m);
        g_names[model.bytes] = n;
        if (S_SVisual *v = model.visual) {
            g_names[v->bytes] = n + ".a";
            g_names[&v->mesh] = n + ".mesh";
            g_names[&v->level] = n + ".level";
            for (int g = 0; g < STAIN_GROUPS; ++g) {
                g_names[&v->level.groups[g]] = n + ".group" + std::to_string(g);
                g_names[v->level.groups[g].material] = n + ".material" + std::to_string(g);
            }
        }
    }
}
// A face group's mesh: vertices of its own range of the level's, some at another one's place, triangles among them
// (some degenerate), none at all now and then.
void BuildWorld(S_SWorld &w, unsigned seed)
{
    std::mt19937 rng(seed);
    const auto rnd = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    const auto rf = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    static const char *const FILES[] = {"soldier01.i3d", "soldier02.i3d", "officer.i3d", "soldier01.i3d", "pilot.i3d", "civil.i3d"};
    for (int m = 0; m < S_MODELS; ++m) {
        S_SModel &model = w.models[m];
        std::memset(model.bytes, 0, sizeof model.bytes);
        *reinterpret_cast<void ***>(model.bytes) = g_sModelVt;
        model.file = FILES[m];
        const char *name = model.file.c_str();
        std::memcpy(model.bytes + 0x134, &name, 4);
        // (Model 5: no child "a"; model 4: a child that is not a single mesh.)
        if (m == 5)
            continue;
        auto *v = new S_SVisual();
        model.visual = v;
        std::memset(v->bytes, 0, sizeof v->bytes);
        *reinterpret_cast<void ***>(v->bytes) = g_sVisualVt;
        reinterpret_cast<I3D_frame *>(v->bytes)->m_frameFlags = 0x20;
        float *matrix = reinterpret_cast<float *>(v->bytes + 0x40);
        for (int i = 0; i < 4; ++i)
            matrix[i * 5] = 1.0f;
        *reinterpret_cast<uint32_t *>(v->bytes + 0x1D0) = m == 4 ? 0u : 0x800u;
        S_SMesh *mesh = &v->mesh;
        std::memcpy(v->bytes + 0x1E0, &mesh, 4);
        I3D_frame *child = reinterpret_cast<I3D_frame *>(v->bytes);
        std::memcpy(model.bytes + 0x300, &child, 4);
        v->mesh.vt = g_sMeshVt;
        v->mesh.level = &v->level;
        S_SLevel &level = v->level;
        level.vt = g_sLevelVt;
        // (The geometry from the world's seed and the file's name: models of one file have one mesh, as the manager,
        // which keeps meshes by file name, takes for granted.)
        uint32_t fileHash = seed;
        for (char ch : model.file)
            fileHash = (fileHash ^ uint8_t(ch)) * 16777619u;
        rng.seed(fileHash);
        int total = 0;
        int counts[STAIN_GROUPS];
        for (int g = 0; g < STAIN_GROUPS; ++g)
            total += counts[g] = rnd(0, 9) == 0 ? rnd(1, 2) : rnd(3, 40);   // (never none: the original reads the first vertex of a group without any)
        level.vertices.resize(size_t(total) + 1);
        level.numVertices = total;
        v->positions.assign(size_t(total + 1) * 4, 0.0f);
        int base = 0;
        for (int g = 0; g < STAIN_GROUPS; ++g) {
            S_SGroup &group = level.groups[g];
            group.vt = g_sGroupVt;
            group.material = new S_SMaterial();
            group.material->vt = g_sMaterialVt;
            // A surface as a body's: the group's vertices on a bent grid patch (row by row), two triangles a cell,
            // some vertices at another's place (seams: joined by BuildMesh), some whole-number places (ties), now and
            // then a degenerate triangle. (Triangles among any vertices made graphs whose searches take exponential time.)
            const int n = counts[g];
            const int columns = n < 3 ? n : std::max(2, int(std::sqrt(float(n))));
            const float cx = rf(-1, 1), cy = rf(0, 2), cz = rf(-1, 1), bend = rf(-0.5f, 0.5f), cell = rf(0.03f, 0.2f);
            for (int i = 0; i < n; ++i) {
                S_SVertex &vx = level.vertices[size_t(base + i)];
                const float gx = float(i % columns) * cell, gy = float(i / columns) * cell;
                if (i > 0 && rnd(0, 8) == 0) {
                    // (At another's place, now and then 1e-6 off along x or z: still joined, an edge of almost none.)
                    vx = level.vertices[size_t(base + rnd(0, i - 1))];
                    // (A seam: usually its own place on the picture.)
                    if (rnd(0, 3) != 0)
                        vx.u = rf(0, 1), vx.v = rf(0, 1);
                    const int off = rnd(0, 5);
                    if (off == 0)
                        vx.x += 1e-6f;
                    else if (off == 1)
                        vx.z -= 1e-6f;
                } else {
                    vx = {cx + gx, cy + gy, cz + bend * gx * gx + rf(-0.01f, 0.01f), 0, 0, 0, rf(0, 1), rf(0, 1)};
                }
                if (rnd(0, 12) == 0)
                    vx.x = float(rnd(-1, 1)), vx.y = float(rnd(0, 2)), vx.z = float(rnd(-1, 1));
                float *p = &v->positions[size_t(base + i) * 4];
                p[0] = vx.x;
                p[1] = vx.y;
                p[2] = vx.z;
            }
            int faces = 0;
            for (int i = 0; i + columns < n; ++i) {
                if (columns > 1 && i % columns == columns - 1)
                    continue;
                const int a = base + i, b = base + i + 1, c = base + i + columns, d = std::min(base + i + columns + 1, base + n - 1);
                const int tri[2][3] = {{a, b, c}, {b, d, c}};
                for (const auto &t : tri) {
                    for (int k = 0; k < 3; ++k)
                        group.faces.push_back(uint16_t(rnd(0, 20) == 0 ? t[0] : t[k]));
                    ++faces;
                }
            }
            if (faces == 0) {                               // (a group of 1..2 vertices: one degenerate triangle)
                for (int k = 0; k < 3; ++k)
                    group.faces.push_back(uint16_t(base + k % n));
                faces = 1;
            }
            group.faces.push_back(0);
            group.numFaces = faces;
            base += n;
        }
    }
}

// ---- the original's objects as text
const uint32_t O_STAIN_MANAGER = 0x0098adf0;
uint32_t SW(const uint8_t *p) { uint32_t w; std::memcpy(&w, p, 4); return w; }
template <class T> T *SP(const uint8_t *p) { return reinterpret_cast<T *>(uintptr_t(SW(p))); }
std::string EdgeText(uint16_t a, uint16_t b, int face, int other)
{
    return std::to_string(a) + "-" + std::to_string(b) + ":" + std::to_string(face) + "/" + std::to_string(other);
}
std::string PortMeshText(const S_StainMesh *m)
{
    if (!m)
        return "null";
    std::string t = std::string(m->name ? m->name : "?") + " refs " + std::to_string(m->refCount);
    for (const S_StainGroup &g : m->groups) {
        t += "\n  group " + std::to_string(g.index) + " " + std::to_string(g.numFaces) + " " + std::to_string(g.numVertices) +
             " extremes " + std::to_string(g.minX) + " " + std::to_string(g.maxX) + " " + std::to_string(g.minZ) + " " +
             std::to_string(g.maxZ) + " " + std::to_string(g.minY) + " " + std::to_string(g.maxY) + "\n   vertices";
        for (const S_StainVertex *v : g.vertices) {
            t += " " + std::to_string(v->index) + "(";
            for (const S_StainEdge *e : v->edges)
                t += EdgeText(e->a->index, e->b->index, e->face, e->otherFace) + ",";
            t += ")";
        }
        t += "\n   edges";
        for (const S_StainEdge *e : g.edges)
            t += " " + EdgeText(e->a->index, e->b->index, e->face, e->otherFace);
    }
    return t;
}
std::string OrigEdge(const uint8_t *e)
{
    int16_t face, other;
    std::memcpy(&face, e + 8, 2);
    std::memcpy(&other, e + 10, 2);
    uint16_t a, b;
    std::memcpy(&a, SP<uint8_t>(e), 2);
    std::memcpy(&b, SP<uint8_t>(e + 4), 2);
    return EdgeText(a, b, face, other);
}
std::string OrigMeshText(const uint8_t *m)
{
    if (!m)
        return "null";
    const char *name = SP<char>(m + 0x818);
    std::string t = std::string(name ? name : "?") + " refs " + std::to_string(int(SW(m + 0x81C)));
    for (int gi = 0; gi < STAIN_GROUPS; ++gi) {
        const uint8_t *g = m + 0x128 * gi;
        t += "\n  group " + std::to_string(int(SW(g))) + " " + std::to_string(int(SW(g + 4))) + " " + std::to_string(int(SW(g + 8))) +
             " extremes " + std::to_string(SW(g + 0x30)) + " " + std::to_string(SW(g + 0x34)) + " " + std::to_string(SW(g + 0x4C)) +
             " " + std::to_string(SW(g + 0x3C)) + " " + std::to_string(SW(g + 0x6C)) + " " + std::to_string(SW(g + 0xAC)) +
             "\n   vertices";
        for (uint8_t **v = SP<uint8_t *>(g + 0x10), **e = SP<uint8_t *>(g + 0x14); v && v != e; ++v) {
            uint16_t index;
            std::memcpy(&index, *v, 2);
            t += " " + std::to_string(index) + "(";
            for (uint8_t **x = SP<uint8_t *>(*v + 8), **y = SP<uint8_t *>(*v + 0xC); x && x != y; ++x)
                t += OrigEdge(*x) + ",";
            t += ")";
        }
        t += "\n   edges";
        for (uint8_t **x = SP<uint8_t *>(g + 0x20), **y = SP<uint8_t *>(g + 0x24); x && x != y; ++x)
            t += " " + OrigEdge(*x);
    }
    return t;
}
std::string PortManagerText()
{
    const C_StainManager &s = g_stainManager;
    std::string t = "init " + std::to_string(s.m_initialized) + " null " + NameOf(s.m_null) + " head " + NameOf(s.m_head) + " paint";
    for (IImage *p : s.m_paint)
        t += " " + NameOf(p);
    t += "\n lent";
    for (int b = 0; b < STAIN_LENT_BODIES; ++b)
        for (IImage *p : s.m_lent[b])
            t += " " + NameOf(p);
    t += "\n borrowers";
    for (C_HumanStains *b : s.m_borrowers)
        t += " " + NameOf(b);
    t += "\n counter " + std::to_string(s.m_paintCounter) + "\n meshes";
    for (const S_StainMesh *m : s.m_meshes)
        t += "\n " + PortMeshText(m);
    return t;
}
std::string OrigManagerText()
{
    const uint8_t *s = OriginalAt<uint8_t *>(O_STAIN_MANAGER);
    std::string t = "init " + std::to_string(int(s[0] != 0)) + " null " + NameOf(SP<void>(s + 0x14)) + " head " +
                    NameOf(SP<void>(s + 0x18)) + " paint";
    for (int i = 0; i < 8; ++i)
        t += " " + NameOf(SP<void>(s + 0x1C + 4 * i));
    t += "\n lent";
    for (int i = 0; i < STAIN_LENT_BODIES * STAIN_GROUPS; ++i)
        t += " " + NameOf(SP<void>(s + 0x3C + 4 * i));
    t += "\n borrowers";
    for (int i = 0; i < STAIN_LENT_BODIES; ++i)
        t += " " + NameOf(SP<void>(s + 0x11C + 4 * i));
    uint16_t counter;
    std::memcpy(&counter, s + 0x18C, 2);
    t += "\n counter " + std::to_string(counter) + "\n meshes";
    for (uint8_t **m = SP<uint8_t *>(s + 8), **e = SP<uint8_t *>(s + 0xC); m && m != e; ++m)
        t += "\n " + OrigMeshText(*m);
    return t;
}
// A world's state as text (the parts the calls change).
std::string WorldText(const S_SWorld &w)
{
    std::string t;
    for (const S_SModel &model : w.models) {
        const S_SVisual *v = model.visual;
        if (!v)
            continue;
        t += NameOf(model.bytes) + ": refresh " + std::to_string(v->refreshes) + " uvs " + std::to_string(v->level.uvs != nullptr) +
             " flags " + std::to_string(v->level.flags) + " unlocks " + std::to_string(v->level.unlocks) + "\n";
        for (const S_SGroup &g : v->level.groups)
            t += " " + NameOf(g.material->image) + "/" + std::to_string(g.material->mode30) + "/" + std::to_string(g.material->mode34) +
                 "/" + std::to_string(g.duplicates);
        t += "\n";
    }
    return t;
}

// Both sides' calls: the logs, the managers and the worlds compared.
bool g_sShowAll = false;
template <class P, class O> void StainCall(const char *what, P port, O original)
{
    const bool trace = std::getenv("STAIN_TRACE") != nullptr;
    g_calls.clear();
    g_sOn = true;
    g_sSide = 0;
    if (trace)
        std::printf("[%s port]", what);
    const int pr = port();
    const std::vector<S_Call> portCalls = g_calls;
    g_calls.clear();
    g_sSide = 1;
    if (trace)
        std::printf("[original]");
    const int orr = original();
    if (trace)
        std::printf("\n");
    const std::vector<S_Call> origCalls = g_calls;
    g_sOn = false;
    g_sSide = 0;
    std::string why;
    const bool calls = CallsAgree(portCalls, origCalls, why);
    if (trace)
        std::printf("[manager]");
    const std::string pm = PortManagerText(), om = OrigManagerText();
    const std::string pw = WorldText(*g_sWorld[0]), ow = WorldText(*g_sWorld[1]);
    static int shown = 0;
    if ((pr != orr || !calls || pm != om || pw != ow) && shown++ < 6) {
        std::printf("  %s: result %d / %d; %s\n", what, pr, orr, why.c_str());
        if (pm != om) {
            size_t at = 0;
            while (at < pm.size() && at < om.size() && pm[at] == om[at])
                ++at;
            const size_t from = at > 200 ? at - 200 : 0;
            std::printf("  manager differs at %zu:\n    port ...%s\n    original ...%s\n", at, pm.substr(from, 400).c_str(),
                        om.substr(from, 400).c_str());
        }
        if (pw != ow)
            std::printf("  worlds differ:\n%s\n%s\n", pw.c_str(), ow.c_str());
        if (!calls && shown < 3)
            for (size_t k = 0; k < 48 && (k < portCalls.size() || k < origCalls.size()); ++k)
                std::printf("    %2zu %-40s %s\n", k, k < portCalls.size() ? portCalls[k].what.c_str() : "-",
                            k < origCalls.size() ? origCalls[k].what.c_str() : "-");
    }
    CHECK(pr == orr);
    CHECK(calls);
    CHECK(pm == om);
    CHECK(pw == ow);
}

void *__cdecl SS_New(size_t size)
{
    void *p = std::malloc(size ? size : 1);
    std::memset(p, 0xCD, size);
    return p;
}
void __cdecl SS_Delete(void *p) { std::free(p); }

void InstallStainFakes()
{
    for (void **vt : {g_sImageVt, g_sMaterialVt, g_sGroupVt, g_sMeshVt})
        for (int i = 0; i < 16; ++i)
            vt[i] = reinterpret_cast<void *>(SF_Unexpected);
    for (void **vt : {g_sGraphVt, g_sDriverVt, g_sLevelVt})
        for (int i = 0; i < 32; ++i)
            vt[i] = reinterpret_cast<void *>(SF_Unexpected);
    for (int i = 0; i < 16; ++i)
        g_sMaterialVt[16 + i] = reinterpret_cast<void *>(SF_Unexpected);
    for (void **vt : {g_sVisualVt, g_sModelVt})
        for (int i = 0; i < 128; ++i)
            vt[i] = reinterpret_cast<void *>(SF_Unexpected);
    g_sImageVt[0] = reinterpret_cast<void *>(SF_ImageRelease);
    g_sImageVt[0x14 / 4] = reinterpret_cast<void *>(SF_ImageLock);
    g_sImageVt[0x18 / 4] = reinterpret_cast<void *>(SF_ImageUnlock);
    g_sGraphVt[0x3C / 4] = reinterpret_cast<void *>(SF_LoadTexture);
    g_sDriverVt[0x18 / 4] = reinterpret_cast<void *>(SF_SetState);
    g_sDriverVt[0x1C / 4] = reinterpret_cast<void *>(SF_GetState);
    g_sMaterialVt[0x2C / 4] = reinterpret_cast<void *>(SF_MaterialImage);
    g_sMaterialVt[0x30 / 4] = reinterpret_cast<void *>(SF_Material30);
    g_sMaterialVt[0x34 / 4] = reinterpret_cast<void *>(SF_Material34);
    g_sGroupVt[0x0C / 4] = reinterpret_cast<void *>(SF_GroupLock);
    g_sGroupVt[0x18 / 4] = reinterpret_cast<void *>(SF_GroupDuplicate);
    g_sLevelVt[0x04 / 4] = reinterpret_cast<void *>(SF_LevelVertices);
    g_sLevelVt[0x08 / 4] = reinterpret_cast<void *>(SF_LevelUnlock);
    g_sLevelVt[0x14 / 4] = reinterpret_cast<void *>(SF_LevelGroup);
    g_sLevelVt[0x38 / 4] = reinterpret_cast<void *>(SF_LevelDetail);
    g_sLevelVt[0x3C / 4] = reinterpret_cast<void *>(SF_LevelMakeUVs);
    g_sMeshVt[0x04 / 4] = reinterpret_cast<void *>(SF_MeshLevel);
    g_sMeshVt[0x18 / 4] = reinterpret_cast<void *>(SF_Mesh18);
    g_sVisualVt[0xA0 / 4] = reinterpret_cast<void *>(SF_VisualDuplicate);
    g_sVisualVt[0xBC / 4] = reinterpret_cast<void *>(SF_VisualPositions);
    g_sVisualVt[0xC4 / 4] = reinterpret_cast<void *>(SF_VisualRefresh);
    g_sVisualVt[0x48 / 4] = reinterpret_cast<void *>(SF_FindChild);
    g_sModelVt[0x48 / 4] = reinterpret_cast<void *>(SF_FindChild);
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x007e529c, reinterpret_cast<const void *>(SF_GetDriver)},      // I3DGetDriver (import thunk)
        {0x007e530e, reinterpret_cast<const void *>(SF_GetIGraph)},      // GetIGraph (import thunk)
        {0x007e566f, reinterpret_cast<const void *>(SS_New)},
        {0x007e2120, reinterpret_cast<const void *>(SS_Delete)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
}

typedef void(SN *T_SManager)(void *);
typedef void(SN *T_SManagerInt)(void *, int);
typedef void *(SN *T_SAcquire)(void *, void *);
typedef void(SN *T_SRelease)(void *, void *);

// ---- 30a: the manager and its meshes
void TestStainManager()
{
    std::printf("stains: the manager and its meshes\n");
    uint8_t *const om = OriginalAt<uint8_t *>(O_STAIN_MANAGER);
    for (int i = 0; i < 400 / QUICK; ++i) {
        const unsigned seed = unsigned(I(1, 1 << 30));
        for (int side = 0; side < 2; ++side) {
            g_sWorld[side] = new S_SWorld();
            BuildWorld(*g_sWorld[side], seed);
            NameWorld(*g_sWorld[side], "");
        }
        std::vector<std::pair<S_StainMesh *, uint8_t *>> got;
        for (int n = 0; n < 30; ++n) {
            const int kind = I(0, 9);
            if (kind == 0) {
                StainCall("Init", [&] { g_stainManager.Init(0); return 0; },
                          [&] { OriginalAt<T_SManagerInt>(0x00796890)(om, 0); return 0; });
                // (An Init of a manager not initialized deletes the meshes acquired meanwhile: their pointers are
                // stale, and a new mesh may get one of the addresses on one side only.)
                got.clear();
            } else if (kind == 1) {
                StainCall("Reset", [&] { g_stainManager.Reset(); return 0; }, [&] { OriginalAt<T_SManager>(0x00796bf0)(om); return 0; });
                got.clear();
            } else if (kind == 2) {
                StainCall("Done", [&] { g_stainManager.Done(); return 0; }, [&] { OriginalAt<T_SManager>(0x00796c00)(om); return 0; });
                got.clear();
            } else if (kind < 7) {
                const int m = I(0, S_MODELS);               // (S_MODELS: no model)
                void *pm = m < S_MODELS ? g_sWorld[0]->models[m].bytes : nullptr;
                void *orm = m < S_MODELS ? g_sWorld[1]->models[m].bytes : nullptr;
                S_StainMesh *p = nullptr;
                uint8_t *o = nullptr;
                StainCall("AcquireMesh",
                          [&] {
                              p = g_stainManager.AcquireMesh(static_cast<I3D_frame *>(pm));
                              return int(p != nullptr);
                          },
                          [&] {
                              o = static_cast<uint8_t *>(OriginalAt<T_SAcquire>(0x00796d00)(om, orm));
                              return int(o != nullptr);
                          });
                if (p && o)
                    got.push_back({p, o});
            } else if (!got.empty()) {
                const size_t k = size_t(I(0, int(got.size()) - 1));
                const auto pair = got[k];
                StainCall("ReleaseMesh", [&] { g_stainManager.ReleaseMesh(pair.first); return 0; },
                          [&] { OriginalAt<T_SRelease>(0x00796ff0)(om, pair.second); return 0; });
                got.erase(got.begin() + long(k));
                // (Gone when its last reference went: every hold of it dropped.)
                bool still = false;
                for (const S_StainMesh *s : g_stainManager.m_meshes)
                    still = still || s == pair.first;
                if (!still)
                    for (size_t j = got.size(); j-- > 0;)
                        if (got[j].first == pair.first)
                            got.erase(got.begin() + long(j));
            }
        }
        StainCall("Done", [&] { g_stainManager.Done(); return 0; }, [&] { OriginalAt<T_SManager>(0x00796c00)(om); return 0; });
    }
}

// ---- 30b: the bodies
// Each side's bodies: the port's C_HumanStains on a pose (its class's layout) of a human (role, containers), the
// original's (the same layout as the port's, by its offset comments) on a pose (owner +0, model +8) of a human (role
// +0x19C, containers +0x74). The container and item are shared fakes (every slot one function: the sides' slots
// differ): GetItem, GetWeight.
const int S_BODIES = 12;                               // (more than the 8 lent: the oldest borrower gives its textures back)
struct S_SItem {
    void **vt;
    float weight;
};
struct S_SContainer {
    void **vt;
    S_SItem *item;
};
void *g_sItemVt[96], *g_sContainerVt[96];
float SN SF_ItemWeight(S_SItem *s) { return s->weight; }
S_SItem *SN SF_ContainerItem(S_SContainer *s) { return s->item; }
struct S_SBody {
    uint8_t *human[2], *pose[2];
    void *stains[2] = {};
    S_SItem item{g_sItemVt, 0.0f};
    S_SContainer container{g_sContainerVt, &item};
    S_SContainer *containers[2] = {&container, nullptr};
    int model = 0;
};
S_SBody g_sBodies[S_BODIES];
C_HumanStains *PortStains(int b) { return static_cast<C_HumanStains *>(g_sBodies[b].stains[0]); }
uint8_t *OrigStains(int b) { return static_cast<uint8_t *>(g_sBodies[b].stains[1]); }
const uint32_t O_MISSION_VARIANT = 0x0098a6b0;

uint32_t BufferHash(const uint16_t *b, size_t n)
{
    if (!b)
        return 0;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
std::string BodyFields(const void *pose, const void *visual, const void *level, const void *mesh, const uint8_t *painted,
                       uint16_t *const *buffers, const uint16_t *clean, bool own, void *const *textures, bool visible, int paint,
                       const int *dims, uint32_t lastHit)
{
    const size_t texels = dims[0] > 0 && dims[1] > 0 ? size_t(dims[0]) * size_t(dims[1]) : 0;
    std::string t = NameOf(pose) + " " + NameOf(visual) + " " + NameOf(level) + " mesh " + (mesh ? "set" : "null") + " painted";
    for (int g = 0; g < STAIN_GROUPS; ++g)
        t += " " + std::to_string(painted[g]);
    t += " buffers";
    for (int g = 0; g < STAIN_GROUPS; ++g)
        t += " " + (buffers[g] ? std::to_string(BufferHash(buffers[g], texels)) : std::string("null"));
    t += " clean " + (clean ? std::to_string(BufferHash(clean, texels)) : std::string("null")) + " own " + std::to_string(own) + " textures";
    for (int g = 0; g < STAIN_GROUPS; ++g)
        t += " " + NameOf(textures[g]);
    t += " visible " + std::to_string(visible) + " paint " + std::to_string(paint) + " dims";
    for (int k = 0; k < 6; ++k)
        t += " " + std::to_string(dims[k]);
    return t + " hit " + std::to_string(lastHit);
}
std::string PortBodyText(const C_HumanStains *s)
{
    if (!s)
        return "none";
    const int dims[6] = {s->m_width, s->m_height, s->m_maskX, s->m_maskY, s->m_shiftX, s->m_shiftY};
    return BodyFields(s->m_pose, s->m_visual, s->m_level, s->m_mesh, s->m_painted, s->m_buffers, s->m_clean, s->m_ownTextures,
                      reinterpret_cast<void *const *>(s->m_textures), s->m_visible, s->m_paint, dims, s->m_lastHitTime);
}
std::string OrigBodyText(const uint8_t *s)
{
    if (!s)
        return "none";
    int dims[6];
    std::memcpy(dims, s + 0x5C, 24);
    return BodyFields(SP<void>(s), SP<void>(s + 4), SP<void>(s + 8), SP<void>(s + 0xC), s + 0x10,
                      reinterpret_cast<uint16_t *const *>(s + 0x18), SP<uint16_t>(s + 0x34), s[0x38] != 0,
                      reinterpret_cast<void *const *>(s + 0x3C), s[0x58] != 0, s[0x59], dims, SW(s + 0x74));
}
std::string ImagesText(int side)
{
    std::string t;
    for (const S_SImage *i : g_sImages[side])
        t += NameOf(i) + ":" + std::to_string(i->releases) + "/" + std::to_string(i->locks) + "/" + std::to_string(i->unlocks) + "/" +
             std::to_string(BufferHash(i->pixels.data(), i->pixels.size())) + " ";
    return t;
}
std::string BodiesText(int side)
{
    std::string t;
    for (int b = 0; b < S_BODIES; ++b)
        t += side == 0 ? PortBodyText(PortStains(b)) + "\n" : OrigBodyText(OrigStains(b)) + "\n";
    return t + ImagesText(side);
}
// The bodies and images compared too (StainCall's comparison of the managers and worlds, then these).
template <class P, class O> void BodyStainCall(const char *what, P port, O original)
{
    StainCall(what, port, [&] {
        // (The original reads the game's time at +0x2C, which O_GetGame does not copy.)
        *reinterpret_cast<uint32_t *>(g_origGame + 0x2C) = GetGame()->m_time;
        return original();
    });
    if (std::getenv("STAIN_TRACE"))
        std::printf("[bodies]");
    const std::string p = BodiesText(0), o = BodiesText(1);
    static int shown = 0;
    if (p != o && shown++ < 6) {
        size_t at = 0;
        while (at < p.size() && at < o.size() && p[at] == o[at])
            ++at;
        const size_t from = at > 150 ? at - 150 : 0;
        std::printf("  %s: bodies differ at %zu:\n    port ...%s\n    original ...%s\n", what, at, p.substr(from, 300).c_str(),
                    o.substr(from, 300).c_str());
    }
    CHECK(p == o);
}

int __cdecl SS_Rand() { return std::rand(); }

// A body of each side on world model `m` (models 4, 5: no mesh), its human of role `role`, its first item `weight`.
void MakeStainBody(int b, int m, int role, float weight, bool containers)
{
    S_SBody &body = g_sBodies[b];
    body.model = m;
    body.item.weight = weight;
    for (int side = 0; side < 2; ++side) {
        const size_t humanSize = side == 0 ? sizeof(C_Human) : 0x400, poseSize = side == 0 ? sizeof(C_HumanWeaponPose) : 0x6400;
        body.human[side] = static_cast<uint8_t *>(std::calloc(1, humanSize));
        body.pose[side] = static_cast<uint8_t *>(std::calloc(1, poseSize));
        g_names[body.human[side]] = "human" + std::to_string(b);
        g_names[body.pose[side]] = "pose" + std::to_string(b);
        void *model = g_sWorld[side]->models[m].bytes;
        if (side == 0) {
            C_Human *h = reinterpret_cast<C_Human *>(body.human[0]);
            h->m_role = static_cast<E_HumanRole>(role);
            h->m_containers = containers ? reinterpret_cast<C_ItemContainer **>(body.containers) : nullptr;
            C_HumanWeaponPose *p = reinterpret_cast<C_HumanWeaponPose *>(body.pose[0]);
            p->m_owner = h;
            p->m_model = static_cast<I3D_frame *>(model);
        } else {
            std::memcpy(body.human[1] + 0x19C, &role, 4);
            void *array = containers ? static_cast<void *>(body.containers) : nullptr;
            std::memcpy(body.human[1] + 0x74, &array, 4);
            std::memcpy(body.pose[1], &body.human[1], 4);
            std::memcpy(body.pose[1] + 8, &model, 4);
        }
    }
}
void DeleteStainBodies()
{
    for (int b = 0; b < S_BODIES; ++b) {
        S_SBody &body = g_sBodies[b];
        if (body.stains[0] || body.stains[1]) {
            BodyStainCall("~C_HumanStains",
                          [&] {
                              delete PortStains(b);
                              body.stains[0] = nullptr;
                              return 0;
                          },
                          [&] {
                              OriginalAt<T_SManager>(0x007986b0)(body.stains[1]);
                              std::free(body.stains[1]);
                              body.stains[1] = nullptr;
                              return 0;
                          });
        }
        for (int side = 0; side < 2; ++side) {
            std::free(body.human[side]);
            std::free(body.pose[side]);
            body.human[side] = body.pose[side] = nullptr;
        }
    }
}

// The id of the chunk where a load left the file (-1: none).
int NextChunk(C_chunk &c)
{
    uint16_t id = 0;
    return c.Descend(id) ? int(id) : -1;
}
typedef void(SN *T_SBodyChar)(void *, char);
typedef void(SN *T_SBodyBool)(void *, bool);
typedef void(SN *T_SBodyUnsigned)(void *, unsigned);
typedef void(SN *T_SBodyHit)(void *, const S_vector *, const S_vector *, uint32_t);
typedef void(SN *T_SBodyChunk)(void *, C_chunk *);
typedef void *(SN *T_SBodyNew)(void *, void *);

void TestStainBodies()
{
    std::printf("stains: the bodies\n");
    uint8_t *const om = OriginalAt<uint8_t *>(O_STAIN_MANAGER);
    const std::string portFile = TempFile("stains_port.sav"), origFile = TempFile("stains_orig.sav");
    for (int i = 0; i < 80 / QUICK; ++i) {
        const unsigned seed = unsigned(I(1, 1 << 30));
        for (int side = 0; side < 2; ++side) {
            g_sImages[side].clear();                    // (the last world's: all released by its Done)
            g_sWorld[side] = new S_SWorld();
            BuildWorld(*g_sWorld[side], seed);
            NameWorld(*g_sWorld[side], "");
            // (The visuals placed: a turn about y and a place, the same on both sides.)
            std::mt19937 rng(seed);
            for (S_SModel &model : g_sWorld[side]->models)
                if (model.visual) {
                    // (A turn about y and a tilt, as a lying or leaning body's: every term of the matrix in use.)
                    const float a = std::uniform_real_distribution<float>(0, 6.2831853f)(rng);
                    float qw = std::cos(a / 2), qy = std::sin(a / 2);
                    float qx = std::uniform_real_distribution<float>(-0.3f, 0.3f)(rng), qz = std::uniform_real_distribution<float>(-0.3f, 0.3f)(rng);
                    const float len = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
                    qw /= len, qx /= len, qy /= len, qz /= len;
                    float *m = reinterpret_cast<float *>(model.visual->bytes + 0x40);
                    m[0] = 1 - 2 * (qy * qy + qz * qz), m[1] = 2 * (qx * qy + qw * qz), m[2] = 2 * (qx * qz - qw * qy);
                    m[4] = 2 * (qx * qy - qw * qz), m[5] = 1 - 2 * (qx * qx + qz * qz), m[6] = 2 * (qy * qz + qw * qx);
                    m[8] = 2 * (qx * qz + qw * qy), m[9] = 2 * (qy * qz - qw * qx), m[10] = 1 - 2 * (qx * qx + qy * qy);
                    m[12] = std::uniform_real_distribution<float>(-5, 5)(rng), m[13] = 0.5f, m[14] = std::uniform_real_distribution<float>(-5, 5)(rng);
                }
        }
        // The manager made (its textures loaded), as the game always has it before bodies bind. (Without, the original's
        // SetPaint, SetVisible and hits find their overlay and splat tables, which Init fills, empty and do nothing;
        // the port's use the pictures directly.)
        if (true)
            StainCall("Init", [&] { g_stainManager.Init(0); return 0; }, [&] { OriginalAt<T_SManagerInt>(0x00796890)(om, 0); return 0; });
        const int variant = I(0, 2);
        g_missionVariant = variant;
        *OriginalAt<int *>(O_MISSION_VARIANT) = variant;
        for (int b = 0; b < S_BODIES; ++b) {
            MakeStainBody(b, P(0.9f) ? I(0, 3) : I(4, 5), P(0.5f) ? 1 : I(2, 3), P(0.1f) ? 0.1f : P(0.5f) ? U(0, 0.1f) : U(0.1f, 3), true);   // (containers always: without, the original's Bind reads through null)
            BodyStainCall("C_HumanStains",
                          [&] {
                              g_sBodies[b].stains[0] = new C_HumanStains(reinterpret_cast<C_HumanWeaponPose *>(g_sBodies[b].pose[0]));
                              return 0;
                          },
                          [&] {
                              void *o = SS_New(0x80);
                              g_sBodies[b].stains[1] = o;
                              OriginalAt<T_SBodyNew>(0x00798660)(o, g_sBodies[b].pose[1]);
                              // (The original's leaves the sizes, +0x5C..+0x70, unset until Bind sets them; the
                              // port's start at 0. Zeroed in the original's.)
                              std::memset(static_cast<uint8_t *>(o) + 0x5C, 0, 24);
                              g_names[g_sBodies[b].stains[0]] = g_names[o] = "stains" + std::to_string(b);
                              return 0;
                          });
        }
        // Now and then every body bound and hit on the head in turn, the times rising: from the ninth on, the manager
        // takes the textures of the one hit longest ago (AcquireTextures' eviction).
        if (P(0.5f)) {
            for (int b = 0; b < S_BODIES; ++b)
                BodyStainCall("Bind", [&] { PortStains(b)->Bind(0); return 0; },
                              [&] { OriginalAt<T_SBodyChar>(0x00798720)(OrigStains(b), 0); return 0; });
            for (int b = 0; b < S_BODIES; ++b) {
                GetGame()->m_time = uint32_t(1000 + 100 * b + I(0, 50));
                const S_vector pos(U(-6, 6), U(0, 2.5f), U(-6, 6)), dir(U(-1, 1), U(-1, 1), U(-1, 1));
                const unsigned callSeed = unsigned(I(1, 1 << 30));
                BodyStainCall("AddHit head",
                              [&] {
                                  std::srand(callSeed);
                                  PortStains(b)->AddHit(pos, dir, 1);
                                  return 0;
                              },
                              [&] {
                                  std::srand(callSeed);
                                  OriginalAt<T_SBodyHit>(0x00799bc0)(OrigStains(b), &pos, &dir, 1);
                                  return 0;
                              });
            }
        }
        for (int n = 0; n < 60; ++n) {
            const int b = I(0, S_BODIES - 1);
            GetGame()->m_time = uint32_t(I(0, 100000));
            const int kind = I(0, 11);
            const unsigned callSeed = unsigned(I(1, 1 << 30));
            if (std::getenv("STAIN_TRACE"))
                std::printf("{%d %d kind %d body %d}", i, n, kind, b);
            if (kind <= 1) {
                const char mode = char(P(0.9f) ? I(0, 2) : I(-1, 5));
                BodyStainCall("Bind", [&] { PortStains(b)->Bind(mode); return 0; },
                              [&] { OriginalAt<T_SBodyChar>(0x00798720)(OrigStains(b), mode); return 0; });
            } else if (kind == 2) {
                const bool on = P(0.5f);
                BodyStainCall("SetVisible", [&] { PortStains(b)->SetVisible(on); return 0; },
                              [&] { OriginalAt<T_SBodyBool>(0x007997b0)(OrigStains(b), on); return 0; });
            } else if (kind == 3) {
                const unsigned paint = unsigned(I(0, 8));
                BodyStainCall("SetPaint", [&] { PortStains(b)->SetPaint(paint); return 0; },
                              [&] { OriginalAt<T_SBodyUnsigned>(0x007999a0)(OrigStains(b), paint); return 0; });
            } else if (kind <= 7) {
                // A hit: at a vertex of the body's mesh in the world (moved a little), from any side, any zone bits.
                S_SModel &model = g_sWorld[0]->models[g_sBodies[b].model];
                S_vector pos(U(-6, 6), U(0, 2.5f), U(-6, 6));
                if (model.visual && !model.visual->positions.empty() && P(0.8f)) {
                    const float *m = reinterpret_cast<const float *>(model.visual->bytes + 0x40);
                    const size_t v = size_t(I(0, int(model.visual->positions.size() / 4) - 2));
                    const float *p = &model.visual->positions[v * 4];
                    pos = S_vector(p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12], p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
                                   p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]);
                    pos = pos + S_vector(U(-0.05f, 0.05f), U(-0.05f, 0.05f), U(-0.05f, 0.05f));
                }
                S_vector dir(U(-1, 1), U(-1, 1), U(-1, 1));
                if (P(0.1f))
                    dir = S_vector(P(0.5f) ? 1.0f : -1.0f, 0, 0);
                static const uint32_t ZONES[] = {0, 1, 2, 4, 8, 0x10, 0x20, 3, 0x18, 0x40, 0x81, 0x208};
                const uint32_t flags = ZONES[I(0, 11)];
                if (std::getenv("STAIN_TRACE"))
                    std::printf("(flags %x dir %g %g %g pos %g %g %g)", flags, dir.x, dir.y, dir.z, pos.x, pos.y, pos.z);
                BodyStainCall("AddHit",
                              [&] {
                                  std::srand(callSeed);
                                  PortStains(b)->AddHit(pos, dir, flags);
                                  return 0;
                              },
                              [&] {
                                  std::srand(callSeed);
                                  OriginalAt<T_SBodyHit>(0x00799bc0)(OrigStains(b), &pos, &dir, flags);
                                  return 0;
                              });
            } else if (kind == 8) {
                // Saved, then loaded into another body (on the same model) or itself; now and then skipped.
                std::remove(portFile.c_str());
                std::remove(origFile.c_str());
                BodyStainCall("SaveGame",
                              [&] {
                                  C_chunk c;
                                  c.Open(portFile.c_str(), true);
                                  PortStains(b)->SaveGame(c);
                                  c.Begin(0x0BD1);           // (a chunk after: where a load leaves the file)
                                  c.Ascend();
                                  return 0;
                              },
                              [&] {
                                  C_chunk c;
                                  c.Open(origFile.c_str(), true);
                                  OriginalAt<T_SBodyChunk>(0x00798930)(OrigStains(b), &c);
                                  c.Begin(0x0BD1);
                                  c.Ascend();
                                  return 0;
                              });
                CHECK(FileBytes(portFile) == FileBytes(origFile));
                // (Loaded into itself or another body with the same picture size: a size of 0, an unbound body's,
                // loaded into a bound one makes masks of -1 and the splats run past the pictures on both sides.)
                // (Nor into a body borrowing textures now: the file's "own textures" may say no, and its destructor
                // would not give them back, leaving the manager a freed borrower to evict later; then skipped.)
                int to = I(0, S_BODIES - 1);
                if (PortStains(to)->m_width != PortStains(b)->m_width || PortStains(to)->m_height != PortStains(b)->m_height)
                    to = b;
                if (P(0.2f) || PortStains(to)->m_ownTextures) {
                    BodyStainCall("SkipSaved",
                                  [&] {
                                      C_chunk c;
                                      c.Open(portFile.c_str(), false);
                                      C_HumanStains::SkipSaved(c);
                                      return NextChunk(c);
                                  },
                                  [&] {
                                      C_chunk c;
                                      c.Open(portFile.c_str(), false);
                                      OriginalAt<T_SManager>(0x00798f70)(&c);   // (the chunk in ECX)
                                      return NextChunk(c);
                                  });
                } else {
                    // (Now and then another chunk where the stains' should be, or an empty file: nothing loaded.)
                    if (P(0.1f)) {
                        C_chunk c;
                        c.Open(portFile.c_str(), true);
                        if (P(0.5f)) {
                            c.Begin(uint16_t(I(1, 0xB10C)));
                            const int x = I(0, 1000);
                            c.Write(&x, 4);
                            c.Ascend();
                        }
                    }
                    BodyStainCall("LoadGame",
                                  [&] {
                                      C_chunk c;
                                      c.Open(portFile.c_str(), false);
                                      PortStains(to)->LoadGame(c);
                                      return NextChunk(c);
                                  },
                                  [&] {
                                      C_chunk c;
                                      c.Open(portFile.c_str(), false);
                                      OriginalAt<T_SBodyChunk>(0x00798bc0)(OrigStains(to), &c);
                                      return NextChunk(c);
                                  });
                }
            } else if (!PortStains(b)->m_level) {
                // (The lending below only for bodies bound to a mesh level: the game lends through TakeTextures, which
                // has one; ReturnTextures on a body without crashes on both sides.)
            } else if (kind == 9) {
                BodyStainCall("ReturnTextures", [&] { PortStains(b)->ReturnTextures(); return 0; },
                              [&] { OriginalAt<T_SManager>(0x007996d0)(OrigStains(b)); return 0; });
            } else {
                // A body whose model is another: Bind mode 2 (the mesh acquired again, the stains kept).
                BodyStainCall("Bind 2", [&] { PortStains(b)->Bind(2); return 0; },
                              [&] { OriginalAt<T_SBodyChar>(0x00798720)(OrigStains(b), 2); return 0; });
            }
            // (The manager's AcquireTextures and ReleaseTextures only through the bodies: TakeTextures lends to a body
            // that owns them after, GiveBackTextures and the destructor give them back. Called directly they leave a
            // borrower that does not know it, freed later while the manager holds it.)
        }
        DeleteStainBodies();
        StainCall("Done", [&] { g_stainManager.Done(); return 0; }, [&] { OriginalAt<T_SManager>(0x00796c00)(om); return 0; });
    }
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
}

// (First chance: the original's own exception frames, its vector constructors', take an access violation before the
// harness's handler; this prints where it happened.)
LONG CALLBACK StainFirstChance(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == 0xC0000005 && std::getenv("STAIN_TRACE")) {
        const uint32_t *sp = reinterpret_cast<const uint32_t *>(e->ContextRecord->Esp);
        std::printf("\naccess violation at %08x (reading %08x); stack:", static_cast<unsigned>(e->ContextRecord->Eip),
                    static_cast<unsigned>(e->ExceptionRecord->ExceptionInformation[1]));
        for (int i = 0; i < 12 && !IsBadReadPtr(sp + i, 4); ++i)
            std::printf(" %08x", sp[i]);
        std::printf("\n");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void TestStains()
{
    AddVectoredExceptionHandler(1, StainFirstChance);
    InstallStainFakes();
    for (void *&slot : g_sItemVt)
        slot = reinterpret_cast<void *>(SF_ItemWeight);
    for (void *&slot : g_sContainerVt)
        slot = reinterpret_cast<void *>(SF_ContainerItem);
    RedirectOriginal(0x007e6393, reinterpret_cast<const void *>(SS_Rand));   // _rand: the same rand() on both sides
    TestStainManager();
    TestStainBodies();
}
}  // namespace

// (The port's engine entry points in this build: the same fakes.)
extern "C" {
I3D_driver *__wrap_I3DGetDriver() { return reinterpret_cast<I3D_driver *>(SF_GetDriver()); }
}
IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(SF_GetIGraph()); }
int g_missionVariant = 0;
