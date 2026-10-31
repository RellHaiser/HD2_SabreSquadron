// Part 8d-iii's decorators update (game/decorators.cpp: the light map, the models taken and given
// back, the animated meshes, the grid's refills as the viewer moves, the bends, the sway)
// (WORLD_TEST), after decorators_test.inc. g_decorators points at a test's own decorators; the
// ground is g_rayTestHook's (every ray recorded); frames have their own vtable (logging the
// direction, the bound, the level refresh and the model copy). Prints the "decorators2:" line.

#include <functional>

#include "script/script_module.h"

// (The scripts' module as bytes: the decorators read its cutscene flags.)
alignas(8) uint8_t g_dmScriptModule[sizeof(C_ScriptModule)] __asm__("_g_scriptModule");

namespace {

void *g_dmVt[SLOTS], *g_dmMatVt[SLOTS];
int g_dmCreated;
bool g_dmTactical;
alignas(16) uint8_t g_dmHumanEffects[16];

void __stdcall F_DmDir(I3D_frame *self, const S_vector &dir, float roll)
{
    GfLog("dir %s %.3f %.3f %.3f %.3f", GfName(self), dir.x, dir.y, dir.z, roll);
}
void __stdcall F_DmBound(I3D_frame *self)
{
    GfLog("bound %s", GfName(self));
    WmField<uint32_t>(self, 0x1D0) |= VISFLAGS_BOUND_VALID;
}
void __stdcall F_DmRefresh(I3D_frame *self, int level) { GfLog("refresh %s %d", GfName(self), level); }
void __stdcall F_DmDuplicate(I3D_frame *self, I3D_frame *source) { GfLog("dup %s %s", GfName(self), GfName(source)); }
I3D_frame *__stdcall F_DmCreateFrame(void *, int type)
{
    GfLog("create %d", type);
    return F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(type), "new" + std::to_string(g_dmCreated++), nullptr, g_dmVt);
}
void __stdcall F_DmMat04(S_GfMaterial *self, const S_vector &v) { GfLog("mat04 %s %.1f %.1f %.1f", self->id.c_str(), v.x, v.y, v.z); }
void __stdcall F_DmMat0C(S_GfMaterial *self, const S_vector &v) { GfLog("mat0c %s %.1f %.1f %.1f", self->id.c_str(), v.x, v.y, v.z); }
void __stdcall F_DmMat10(S_GfMaterial *self, const S_vector &v) { GfLog("mat10 %s %.1f %.1f %.1f", self->id.c_str(), v.x, v.y, v.z); }
void __stdcall F_DmMat14(S_GfMaterial *self, float v) { GfLog("mat14 %s %.1f", self->id.c_str(), v); }

// The ground: hit at g_dmGroundY with g_dmNormal and material g_dmMaterial (none while g_dmNoHit).
struct S_DmRay {
    S_vector from, dir;
    uint32_t types, flags;
};
std::vector<S_DmRay> g_dmRays;
float g_dmGroundY;
S_vector g_dmNormal(0.0f, 1.0f, 0.0f);
int g_dmMaterial;
bool g_dmNoHit;
S_CollisionVolume g_dmVolume;
S_CollisionVolume *DmRay(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal, uint32_t types,
                         uint32_t flags)
{
    g_dmRays.push_back({from, dir, types, flags});
    if (g_dmNoHit)
        return nullptr;
    *hitPos = S_vector(from.x, g_dmGroundY, from.z);
    *hitNormal = g_dmNormal;
    g_dmVolume.flags = static_cast<uint32_t>(g_dmMaterial) << 24;
    return &g_dmVolume;
}

// A model frame (the test's vtable) at `pos`.
I3D_frame *DmModel(const std::string &name, const S_vector &pos = S_vector(0, 0, 0), I3D_frame *parent = nullptr)
{
    I3D_frame *m = F_GfNewFrame(FRAME_MODEL, name, parent, g_dmVt);
    GfPlace(m, pos);
    return m;
}
// An object visual under `parent` with `mesh` and the local box (min, max; valid or not).
I3D_frame *DmVisual(const std::string &name, I3D_frame *parent, S_GfMesh *mesh, const S_vector &min, const S_vector &max,
                    bool valid = true)
{
    I3D_frame *v = F_GfNewFrame(FRAME_VISUAL, name, parent, g_dmVt);
    g_gfVisualTypes[v] = I3DVISUAL_OBJECT;
    WmField<uint32_t>(v, 0x1D0) = valid ? VISFLAGS_BOUND_VALID : 0;
    WmField<S_GfMesh *>(v, 0x1E0) = mesh;
    WmField<S_vector>(v, 0x1A0) = min;
    WmField<S_vector>(v, 0x1B0) = max;
    return v;
}
S_GfMaterial *DmMaterial(const std::string &id)
{
    S_GfMaterial *m = GfMaterial(id, nullptr);
    m->vtable = g_dmMatVt;
    return m;
}
// A level with these vertices (positions; the normal's x 0.5).
S_GfLevel *DmLevel(std::initializer_list<S_vector> positions)
{
    S_GfLevel *l = GfLevel({GfGroup("g", DmMaterial("lm"))});
    for (const S_vector &p : positions)
        l->vertices.push_back(S_GfVertex{p, S_vector(0.5f, 0, 0), 0, 0});
    l->numVertices = static_cast<int>(l->vertices.size());
    return l;
}
float DmLerp(float a, float b, float t) { return b * t + (1.0f - t) * a; }
float DmLightOf(I3D_frame *f)
{
    const S_FrameData *d = f->GetData();
    float v;
    std::memcpy(&v, &d, sizeof v);
    return v;
}
void DmSetLight(I3D_frame *f, float v)
{
    S_FrameData *d = nullptr;
    std::memcpy(&d, &v, sizeof v);
    f->SetData(d);
}
// The world cells (x, z) the recorded rays went down at (their offsets at 0: RandomFloat's half).
std::vector<std::pair<int, int>> DmCells()
{
    std::vector<std::pair<int, int>> out;
    for (const S_DmRay &r : g_dmRays)
        out.emplace_back(static_cast<int>(std::floor(r.from.x)), static_cast<int>(std::floor(r.from.z)));
    return out;
}
bool DmNear(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

struct S_DmResults {
    bool dmSlope = true, dmLight = true, dmTake = true, dmPool = true, dmPrepare = true, dmCell = true, dmRefill = true;
    bool dmRefillSeed = true, dmFollow = true, dmFollowMove = true, dmShift = true, dmBend = true, dmBendSpring = true;
    bool dmBendUpdate = true, dmBends = true, dmHuman = true, dmCar = true, dmUpdate = true, dmSway = true;
};

}  // namespace

// (The camera module's human effects and the tactical view: the update's.)
#ifndef CAMERA_TEST                                    // (the camera build links the real module, part 8e)
C_CameraHumanEffects *C_CameraModule::GetHumanEffects() { return reinterpret_cast<C_CameraHumanEffects *>(g_dmHumanEffects); }
#endif
bool C_CameraHumanEffects::IsTactical() const { return g_dmTactical; }
// (As game/human_movement.cpp has it.)
float AngleBetween2D(const float *a, const float *b)
{
    constexpr float PI = 3.1415927f;
    float angle = std::atan2(b[1], b[0]) - std::atan2(a[1], a[0]);
    if (angle < -PI)
        angle += 2.0f * PI;
    if (angle > PI)
        angle -= 2.0f * PI;
    return angle;
}

void Decorators2Test()
{
    S_DmResults r;
    static bool once;
    if (!once) {
        once = true;
        for (int i = 0; i < SLOTS; ++i) {
            g_dmVt[i] = g_gfFrameVt[i];
            g_dmMatVt[i] = g_gfMaterialVt[i];
        }
        g_dmVt[0x1C / 4] = reinterpret_cast<void *>(&F_DmDir);
        g_dmVt[0x90 / 4] = reinterpret_cast<void *>(&F_DmBound);
        g_dmVt[0xAC / 4] = reinterpret_cast<void *>(&F_DmRefresh);
        g_dmVt[0xD8 / 4] = reinterpret_cast<void *>(&F_DmDuplicate);
        g_dmMatVt[0x04 / 4] = reinterpret_cast<void *>(&F_DmMat04);
        g_dmMatVt[0x0C / 4] = reinterpret_cast<void *>(&F_DmMat0C);
        g_dmMatVt[0x10 / 4] = reinterpret_cast<void *>(&F_DmMat10);
        g_dmMatVt[0x14 / 4] = reinterpret_cast<void *>(&F_DmMat14);
    }
    void *const oldCreate = g_driverVtable[0x54 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_DmCreateFrame);
    auto *const oldHook = g_rayTestHook;
    g_rayTestHook = &DmRay;
    C_Decorators *const oldGlobal = g_decorators;
    uint8_t *scene = reinterpret_cast<uint8_t *>(GetGame()->GetScene());
    I3D_frame *const oldPrimary = WmField<I3D_frame *>(scene, 0x1F8);
    I3D_frame *primary = GfFrame(FRAME_SECTOR, "primary");
    WmField<I3D_frame *>(scene, 0x1F8) = primary;
    S_DetailSettings &details = *g_gameProfile.GetDetailSettings();
    const bool oldSwitch = details.switches[DETAIL_DECORATORS];
    g_gfRandoms.clear();
    g_gfActive = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
    C_Decorators d;
    g_decorators = &d;

    // --- the sets' minimum slope; the light map ---
    {
        d.m_sets = {nullptr, new S_DecoratorSet()};
        d.m_sets[1]->minSlope = 0.7f;
        WM(r.dmSlope, d.GetMinSlope(1) == 0.7f && d.GetMinSlope(0) == 1000.0f && d.GetMinSlope(2) == 1000.0f &&
                          d.GetMinSlope(static_cast<unsigned>(-1)) == 1000.0f);
        WM(r.dmLight, d.SampleLight(1.0f, 1.0f) == 0.0f);
        const uint8_t bytes[9] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
        d.m_light = new uint8_t[9];
        std::memcpy(d.m_light, bytes, 9);
        d.m_lightWidth = 3;
        d.m_lightHeight = 3;
        d.m_lightCellSize = 2.0f;
        d.m_lightX = 10.0f;
        d.m_lightZ = 20.0f;
        auto expect = [&](float x, float z) {
            const float u = x - 10.0f, v = z - 20.0f, inv = 1.0f / 2.0f;
            const int c = static_cast<int>(inv * u), w = static_cast<int>(inv * v);
            const float fu = (u - static_cast<float>(c) * 2.0f) * inv, fv = (v - static_cast<float>(w) * 2.0f) * inv;
            const float a = DmLerp(bytes[w * 3 + c], bytes[w * 3 + c + 1], fu);
            const float b = DmLerp(bytes[(w + 1) * 3 + c], bytes[(w + 1) * 3 + c + 1], fu);
            return DmLerp(a, b, fv) * 0.00390625f;
        };
        WM(r.dmLight, d.SampleLight(10.0f, 20.0f) == 10.0f / 256.0f && d.SampleLight(11.0f, 21.0f) == 30.0f / 256.0f &&
                          d.SampleLight(12.5f, 23.0f) == expect(12.5f, 23.0f) && expect(12.5f, 23.0f) != expect(12.5f, 21.0f) &&
                          d.SampleLight(9.0f, 21.5f) == expect(9.0f, 21.5f) && d.SampleLight(13.0f, 20.0f) == expect(13.0f, 20.0f));
        // (Bilinear: x along a row, z between rows.)
        WM(r.dmLight, d.SampleLight(11.0f, 20.0f) == 15.0f / 256.0f && d.SampleLight(10.0f, 21.0f) == 25.0f / 256.0f);
    }

    // --- models taken: picked by chance, loaded (a copy kept), copied, the spares, failures ---
    I3D_frame *loaded = nullptr;
    S_Decorator *da = nullptr;
    {
        S_DecoratorSet *set = d.m_sets[1];
        auto *a = new S_Decorator(), *b = new S_Decorator(), *c = new S_Decorator();
        a->model = "grassA";
        a->chance = 30.0f;
        a->height = 1.5f;
        a->randomRotation = false;
        a->alignToGround = true;
        b->model = "grassB";
        b->chance = 60.0f;
        b->loadFailed = true;
        c->model = "missing";
        c->chance = 90.0f;
        set->decorators = {a, b, c};
        da = a;
        for (S_Decorator *x : set->decorators) {
            x->pool = new S_DecoratorPool(x->model.c_str());
            d.m_pools.push_back(x->pool);
        }
        int index = 77;
        bool rr = true, align = false;
        float height = 0.0f;
        g_gfLog.clear();
        WM(r.dmTake, !d.TakeModel(5, 0, &index, &rr, &height, &align) && !d.TakeModel(0, 0, &index, &rr, &height, &align) &&
                         g_gfLog.empty() && index == 77);
        g_gfRandoms = {95.0f};
        WM(r.dmTake, !d.TakeModel(1, 0, &index, &rr, &height, &align) && GfLogIs({"rand 100.0"}) && index == 77 && rr && !align);
        // Exactly at a's chance: b's (not under).
        g_gfRandoms = {30.0f};
        WM(r.dmTake, !d.TakeModel(1, 0, &index, &rr, &height, &align) && index == 1 && rr && height == 0.0f);
        // a: loaded, linked, its material lit, a copy kept in the pool.
        g_cacheFill = [](I3D_model *object) {
            DmVisual("va", reinterpret_cast<I3D_frame *>(object),
                     GfMesh("ma", {GfLevel({GfGroup("ga", DmMaterial("mata")), GfGroup("gb", DmMaterial("matb"))})}),
                     S_vector(0, 0, 0), S_vector(0, 0, 0));
        };
        g_dmCreated = 0;
        g_cacheLoads.clear();
        g_gfRandoms = {29.9f};
        g_gfLog.clear();
        loaded = d.TakeModel(1, 0, &index, &rr, &height, &align);
        S_GfMaterial *mata = loaded ? reinterpret_cast<S_GfMaterial *>(
                                          WmField<S_GfMesh *>(loaded->GetChild(0), 0x1E0)->levels[0]->groups[0]->material) : nullptr;
        WM(r.dmTake, loaded && GfName(loaded) == std::string("new0") && index == 0 && !rr && align && height == 1.5f &&
                         g_cacheLoads == std::vector<std::string>({"grassA"}) && loaded->GetParent() == primary &&
                         GfLogIs({"rand 100.0", "create 9", "link new0 primary 0", "mat0c mata 1.0 1.0 1.0", "mat04 mata 0.0 0.0 0.0",
                                  "mat10 mata 0.0 0.0 0.0", "mat14 mata 0.0", "create 9", "dup new1 new0"}) &&
                         mata && (mata->flags & 0x08000000) && a->pool->models.size() == 1 &&
                         GfName(a->pool->models[0]) == std::string("new1") && !a->mesh);
        g_cacheFill = nullptr;
        // Again: a copy of the pool's original, linked.
        g_gfRandoms = {0.0f};
        g_gfLog.clear();
        I3D_frame *copy = d.TakeModel(1, 0, &index, &rr, &height, &align);
        WM(r.dmPool, copy && GfLogIs({"rand 100.0", "create 9", "dup new2 new1", "link new2 primary 0"}) &&
                         a->pool->models.size() == 1 && index == 0);
        // Given back: the spare taken (the last first).
        d.ReturnModel(1, loaded, 0);
        d.ReturnModel(1, copy, 0);
        WM(r.dmPool, a->pool->models == std::vector<I3D_frame *>({a->pool->models[0], loaded, copy}));
        g_gfRandoms = {0.0f};
        g_gfLog.clear();
        WM(r.dmPool, d.TakeModel(1, 0, &index, &rr, &height, &align) == copy && GfLogIs({"rand 100.0"}) &&
                         a->pool->models.size() == 2);
        g_gfRandoms = {0.0f};
        WM(r.dmPool, d.TakeModel(1, 0, &index, &rr, &height, &align) == loaded && a->pool->models.size() == 1);
        // b failed before: none; c fails now (marked, released).
        g_gfRandoms = {45.0f};
        g_gfLog.clear();
        WM(r.dmTake, !d.TakeModel(1, 0, &index, &rr, &height, &align) && index == 1 && GfLogIs({"rand 100.0"}));
        g_cacheFailName = "missing";
        g_gfRandoms = {89.0f};
        g_gfLog.clear();
        index = 77;
        WM(r.dmTake, !d.TakeModel(1, 0, &index, &rr, &height, &align) && index == 2 && c->loadFailed &&
                         GfLogIs({"rand 100.0", "create 9", "release new3"}) && c->pool->models.empty());
        g_cacheFailName.clear();
        // An animated one gets its mesh first (before its link).
        auto *anim = new S_Decorator();
        anim->model = "swayer";
        anim->chance = 100.0f;
        anim->animation = 1;
        anim->pool = new S_DecoratorPool("swayer");
        d.m_pools.push_back(anim->pool);
        set->decorators.push_back(anim);
        g_cacheFill = [](I3D_model *object) {
            DmVisual("vs", reinterpret_cast<I3D_frame *>(object),
                     GfMesh("ms", {DmLevel({S_vector(0, 1, 0)})}), S_vector(0, 0, 0), S_vector(2, 1, 0));
        };
        g_gfRandoms = {99.0f};
        g_gfLog.clear();
        I3D_frame *swayer = d.TakeModel(1, 0, &index, &rr, &height, &align);
        WM(r.dmPrepare, swayer && anim->mesh && GfHas({"lockverts 0", "unlockverts", "bounds", "refresh vs 0", "update vs",
                                                       "link new" + std::to_string(g_dmCreated - 2) + " primary 0"}) &&
                            anim->vertices.size() == 1);
        // Already with its mesh: not again.
        S_GfMesh *had = reinterpret_cast<S_GfMesh *>(anim->mesh);
        const int refs = WmField<int>(had, 0x40);
        anim->pool->models.clear();
        g_gfRandoms = {99.0f};
        g_gfLog.clear();
        d.TakeModel(1, 0, &index, &rr, &height, &align);
        WM(r.dmPrepare, reinterpret_cast<S_GfMesh *>(anim->mesh) == had && WmField<int>(had, 0x40) == refs &&
                            anim->vertices.size() == 1 && !GfHas({"lockverts 0"}));
        g_cacheFill = nullptr;
        anim->mesh = nullptr;
        anim->vertices.clear();
        anim->restPositions.clear();
    }

    // --- an animated mesh prepared: the vertices near the line along the box's top diagonal ---
    {
        S_Decorator dec;
        I3D_frame *m = DmModel("pm");
        S_GfMesh *mesh = GfMesh("pmesh", {DmLevel({S_vector(9, 9, 9)}),
                                          DmLevel({S_vector(0, 1, 0), S_vector(1, 1, 0.05f), S_vector(1, 0.85f, 0),
                                                   S_vector(1, 1, 0.1f), S_vector(3, 1, -0.02f)})});
        I3D_frame *v = DmVisual("pv", m, mesh, S_vector(0, 0, 0), S_vector(2, 1, 0), false);
        (void)v;
        const int refs = WmField<int>(mesh, 0x40);
        g_gfLog.clear();
        dec.PrepareAnimated(m);
        S_GfLevel *l1 = mesh->levels[1];
        WM(r.dmPrepare, dec.mesh == reinterpret_cast<I3D_mesh *>(mesh) && WmField<int>(mesh, 0x40) == refs + 1 &&
                            dec.vertices == std::vector<float *>({&l1->vertices[4].pos.x, &l1->vertices[1].pos.x,
                                                                  &l1->vertices[0].pos.x}) &&
                            dec.restPositions.size() == 3 && dec.restPositions[0].x == 3.0f && dec.restPositions[0].z == -0.02f &&
                            dec.restPositions[1].z == 0.05f && dec.restPositions[2].w == 0.5f &&
                            GfLogIs({"bound pv", "lockverts 0", "unlockverts", "bounds", "refresh pv 1", "lockverts 0", "unlockverts",
                                     "bounds", "refresh pv 0", "update pv"}));
        dec.mesh = nullptr;
        // A box without width in x and z: every vertex.
        S_Decorator flat;
        I3D_frame *m2 = DmModel("pm2");
        S_GfMesh *mesh2 = GfMesh("pmesh2", {DmLevel({S_vector(5, 5, 5), S_vector(-3, 0, 2)})});
        DmVisual("pv2", m2, mesh2, S_vector(1, 0, 1), S_vector(1, 4, 1));
        flat.PrepareAnimated(m2);
        WM(r.dmPrepare, flat.vertices.size() == 2 && flat.restPositions[0].x == -3.0f);
        flat.mesh = nullptr;
        // A box along z: the line along z through the top.
        S_Decorator alongZ;
        I3D_frame *m3 = DmModel("pm3");
        S_GfMesh *mesh3 = GfMesh("pmesh3", {DmLevel({S_vector(0, 2, 1), S_vector(0.05f, 2, 3), S_vector(0, 1.95f, 0.5f),
                                                     S_vector(0.2f, 2, 1)})});
        DmVisual("pv3", m3, mesh3, S_vector(0, 0, 0), S_vector(0, 2, 2));
        alongZ.PrepareAnimated(m3);
        WM(r.dmPrepare, alongZ.vertices.size() == 3 && alongZ.restPositions[0].y == 1.95f);
        alongZ.mesh = nullptr;
        // Not an object visual; no child.
        S_Decorator none;
        I3D_frame *m4 = DmModel("pm4");
        I3D_frame *other = F_GfNewFrame(FRAME_VISUAL, "pv4", m4, g_dmVt);
        g_gfVisualTypes[other] = I3DVISUAL_SINGLEMESH;
        g_gfLog.clear();
        none.PrepareAnimated(m4);
        none.PrepareAnimated(DmModel("pm5"));
        WM(r.dmPrepare, !none.mesh && none.vertices.empty() && g_gfLog.empty());
    }

    // --- a cell placed, cleared ---
    {
        S_DecoratorCell cell;
        da->pool->models.push_back(loaded);
        g_gfRandoms = {0.0f};
        g_gfLog.clear();
        bool rr = true, align = true;
        float height = 3.0f;
        I3D_frame *m = cell.Place(1, 0, &rr, &height, &align);
        WM(r.dmCell, m == loaded && cell.model == m && cell.set == 1 && cell.decorator == 0 && !rr && align && height == 1.5f &&
                         GfLogIs({"rand 100.0", "on new0 1"}));
        g_gfLog.clear();
        cell.Clear();
        WM(r.dmCell, !cell.model && GfLogIs({"on new0 0"}) && da->pool->models.back() == m);
        cell.Clear();
        WM(r.dmCell, GfLogIs({"on new0 0"}));
        WM(r.dmCell, !cell.Place(5, 0, &rr, &height, &align) && !cell.model && cell.set == 5);
    }

    // --- a cell refilled: its seed, the ray down, the ground's slope, the model placed ---
    {
        C_DecoratorGrid &g = d.m_grid;
        g.m_sizeX = g.m_sizeZ = 4;
        g.m_cellSize = 1.0f;
        g.Init();
        g.m_originX = 10;
        g.m_originZ = 20;
        g.m_shiftX = 1;
        g.m_shiftZ = 2;
        // (Only the plain decorator: a has height 1.5, no rotation, aligned; a fresh one.)
        S_DecoratorSet *set = d.m_sets[1];
        auto *plain = new S_Decorator();
        plain->model = "plain";
        plain->chance = 100.0f;
        plain->pool = da->pool;
        set->decorators.insert(set->decorators.begin(), plain);
        g_dmGroundY = 0.25f;
        g_dmNormal = S_vector(0.2f, 0.98f, 0.1f);
        g_dmMaterial = 1;
        g_dmRays.clear();
        g_gfRandoms = {0.2f, 0.9f, 10.0f, 0.75f, 1.5f, 1.0f};
        g_gfLog.clear();
        // Cell (z 1, x 2): the world's (11, 21), the array's [3][3].
        const float hash = std::sin(static_cast<float>(21)) * static_cast<float>(11);
        const int seed = static_cast<int>(std::cos(hash + 1.0f) * 10000.0f + 123456.0f);
        std::srand(static_cast<unsigned>(seed));
        const int wantRand = std::rand();
        g.Refill(1, 2);
        const int gotRand = std::rand();
        S_DecoratorCell &cell = g.m_cells[3][3];
        I3D_frame *m = cell.model;
        const float px = ((0.9f - 0.5f) * 0.75f + 11.0f) * 1.0f, pz = ((0.2f - 0.5f) * 0.75f + 21.0f) * 1.0f;
        const float light = d.SampleLight(px, pz) * 0.5f;
        WM(r.dmRefillSeed, gotRand == wantRand);
        WM(r.dmRefill, g_dmRays.size() == 1 && g_dmRays[0].from == S_vector(px, 100.0f, pz) &&
                           g_dmRays[0].dir == S_vector(0.0f, -500.0f, 0.0f) && g_dmRays[0].types == 0xFFFFFFFFu &&
                           g_dmRays[0].flags == 0x210000 && m && cell.set == 1 && cell.decorator == 0);
        char lightLog[64];
        std::snprintf(lightLog, sizeof lightLog, "lightlevel %s %.3f", m ? GfName(m) : "-", light);
        char dirLog[96];
        std::snprintf(dirLog, sizeof dirLog, "dir %s %.3f %.3f %.3f %.3f", m ? GfName(m) : "-", 0.75f - 1.0f, 0.0f, 1.5f - 1.0f, 0.0f);
        const float scale = (1.0f - 1.0f) * 0.2f + 0.8f;
        WM(r.dmRefill, m && GfHas({"rand 1.0", "rand 1.0", "rand 100.0", lightLog, "rand 2.0", "rand 2.0", dirLog, "rand 2.0", std::string("update ") + GfName(m)}) &&
                           DmLightOf(m) == light && m->m_localMatrix(3) == S_vector(px, 0.25f, pz) &&
                           m->m_scale == S_vector(scale, scale, scale));
        // Again with a bend there (gone, the last into its place); no rotation, a height.
        auto *b1 = new S_DecoratorBend(), *b2 = new S_DecoratorBend(), *b3 = new S_DecoratorBend();
        b2->cell = &cell;
        cell.bend = b2;
        cell.bending = true;
        g.m_bends = {b1, b2, b3};
        plain->randomRotation = false;
        plain->height = 7.0f;
        g_gfRandoms = {0.5f, 0.5f, 10.0f, 2.0f};
        g_gfLog.clear();
        g.Refill(1, 2);
        WM(r.dmRefill, g.m_bends == std::vector<S_DecoratorBend *>({b1, b3}) && !cell.bend && !cell.bending && cell.model &&
                           cell.model->m_localMatrix(3).y == 7.0f && !GfHas({"rand 2.0", "rand 2.0"}) &&
                           cell.model->m_scale.x == 1.0f);
        // Aligned to the ground (with a random roll), at the hit's height.
        plain->alignToGround = true;
        plain->randomRotation = true;
        plain->height = 0.0f;
        g_gfRandoms = {0.5f, 0.5f, 10.0f, 1.25f, 0.0f};
        g_gfLog.clear();
        g.Refill(1, 2);
        std::snprintf(dirLog, sizeof dirLog, "dir %s %.3f %.3f %.3f %.3f", GfName(cell.model), 0.2f, 0.98f, 0.1f, 1.25f);
        WM(r.dmRefill, GfHas({"rand 6.3", dirLog, "rand 2.0"}) && cell.model->m_localMatrix(3).y == 0.25f &&
                           cell.model->m_scale.x == 0.6f);
        plain->randomRotation = false;
        g_gfRandoms = {0.5f, 0.5f, 10.0f, 1.0f};
        g_gfLog.clear();
        g.Refill(1, 2);
        std::snprintf(dirLog, sizeof dirLog, "dir %s %.3f %.3f %.3f %.3f", GfName(cell.model), 0.2f, 0.98f, 0.1f, 0.0f);
        WM(r.dmRefill, GfHas({dirLog}) && !GfHas({"rand 6.3"}));
        plain->alignToGround = false;
        // Too steep (the normal's y not above the minimum; x and z do not count); no ground.
        g_dmNormal = S_vector(5.0f, 0.7f, 5.0f);
        g_gfLog.clear();
        g.Refill(1, 2);
        WM(r.dmRefill, !cell.model && GfLogIs({"rand 1.0", "rand 1.0", "on " + std::string(GfName(da->pool->models.back())) + " 0"}));
        g_dmNormal = S_vector(-5.0f, 0.71f, -5.0f);
        g_gfRandoms = {0.5f, 0.5f, 10.0f};
        g.Refill(1, 2);
        WM(r.dmRefill, cell.model != nullptr);
        g_dmNoHit = true;
        g.Refill(1, 2);
        WM(r.dmRefill, !cell.model);
        g_dmNoHit = false;
        // Another material's set: its slope; none: 1000.
        g_dmMaterial = 0;
        g_dmNormal = S_vector(0, 1, 0);
        g.Refill(1, 2);
        WM(r.dmRefill, !cell.model);
        g_dmMaterial = 1;
        // The seed off the world's axes: x 0 or z 0.
        g.m_originX = 1;
        g.m_shiftX = 0;
        g_dmRays.clear();
        {
            // worldX = 1 - 2 + 0 + x = -1 + x: x 1 -> 0; baseZ = 20 - 2 + 2 = 20.
            const float h = std::sin(static_cast<float>(20 - 1 + 1)) * static_cast<float>(-1 - 1 + 1);
            const int s = static_cast<int>(std::cos(h + 1.0f) * 10000.0f + 123456.0f);
            std::srand(static_cast<unsigned>(s));
            const int want = std::rand();
            g_gfRandoms = {0.5f, 0.5f, 95.0f};
            g.Refill(1, 1);
            WM(r.dmRefillSeed, std::rand() == want);
        }
        g.m_originX = 10;
        g.m_originZ = -1;
        g.m_shiftZ = 3;
        {
            // baseZ = -1 - 2 + 3 = 0: z 0; baseX = 10 - 2 + 0 = 8.
            const float h = std::sin(static_cast<float>(0 - 3 + 0)) * static_cast<float>(8 - 0 + 3);
            const int s = static_cast<int>(std::cos(h + 1.0f) * 10000.0f + 123456.0f);
            std::srand(static_cast<unsigned>(s));
            const int want = std::rand();
            g_gfRandoms = {0.5f, 0.5f, 95.0f};
            g.Refill(0, 3);
            // (The array's cell: column 3, row 0 + 3.)
            WM(r.dmRefillSeed, std::rand() == want && g_dmRays.back().from.x == 11.0f && g_dmRays.back().from.z == 0.0f);
        }
        set->decorators.erase(set->decorators.begin());
        delete plain;
        g.Free();
    }

    // --- the grid following the viewer: all anew when far, cells as it gets on, the shifts ---
    {
        C_DecoratorGrid &g = d.m_grid;
        g.m_sizeX = g.m_sizeZ = 4;
        g.m_cellSize = 1.0f;
        g.Init();
        g_dmNoHit = true;
        g_gfRandoms.clear();
        g_dmRays.clear();
        using V = std::vector<std::pair<int, int>>;
        g.Follow(10.5f, 20.5f);
        V all;
        for (int x = 0; x < 4; ++x)
            for (int z = 0; z < 4; ++z)
                all.emplace_back(8 + x, 18 + z);
        WM(r.dmFollow, DmCells() == all && g.m_originX == 10 && g.m_originZ == 20 && g.m_shiftX == 0 && g.m_shiftZ == 0 &&
                           g.m_subX == 2 && g.m_subZ == 2);
        // On within the cell: x to 0.8 (row 3 of the last column), back to 0.1 (rows 1..3 of the first).
        g_dmRays.clear();
        g.Follow(10.8f, 20.5f);
        WM(r.dmFollow, DmCells() == V({{11, 21}}) && g.m_subX == 3 && g.m_subZ == 2);
        g_dmRays.clear();
        g.Follow(10.1f, 20.5f);
        WM(r.dmFollow, DmCells() == V({{8, 19}, {8, 20}, {8, 21}}) && g.m_subX == 0);
        g_dmRays.clear();
        g.Follow(10.1f, 20.9f);
        WM(r.dmFollow, DmCells() == V({{11, 21}}) && g.m_subZ == 3);
        g_dmRays.clear();
        g.Follow(10.1f, 20.0f);
        WM(r.dmFollow, DmCells() == V({{9, 18}, {10, 18}, {11, 18}}) && g.m_subZ == 0);
        g_dmRays.clear();
        g.Follow(10.1f, 20.0f);
        WM(r.dmFollow, g_dmRays.empty());
        // A cell on in x: the column finished, shifted, the next one begun.
        g.m_subZ = 3;
        g_dmRays.clear();
        g.Follow(11.2f, 20.9f);
        WM(r.dmFollowMove, DmCells() == V({{11, 19}, {11, 20}, {11, 21}, {12, 21}, {11, 21}, {12, 18}}) && g.m_shiftX == 1 &&
                               g.m_originX == 10 && g.m_subX == 0);
        // Back: the first column's first row finished, shifted back, the rest of the new one.
        g_dmRays.clear();
        g.Follow(10.2f, 20.9f);
        WM(r.dmFollowMove, DmCells() == V({{9, 18}, {8, 19}, {8, 20}, {8, 21}}) && g.m_shiftX == 0 && g.m_subX == 0);
        // Exactly half the grid off: all anew; negative coordinates count down.
        g_dmRays.clear();
        g.Follow(12.6f, 20.3f);
        WM(r.dmFollow, g_dmRays.size() == 16 && g.m_originX == 12 && g.m_subX == 2);
        g_dmRays.clear();
        g.Follow(-0.5f, -3.25f);
        WM(r.dmFollow, g_dmRays.size() == 16 && g.m_originX == -1 && g.m_originZ == -4 && g.m_subX == 2 && g.m_subZ == 3);
        // No grid: nothing.
        g.m_sizeX = 0;
        g_dmRays.clear();
        g.Follow(100.0f, 100.0f);
        WM(r.dmFollow, g_dmRays.empty());
        g.m_sizeX = 4;
        g.Free();
        // An 8 x 8 grid: two cells on and back in x, three on and two back in z (the cells as the
        // original's code goes, from a model of it: _re/tools/decorator_follow_sim.py).
        g.m_sizeX = g.m_sizeZ = 8;
        g.Init();
        g.Follow(10.5f, 20.5f);
        g.m_subZ = 5;
        g_dmRays.clear();
        g.Follow(12.3f, 20.7f);
        WM(r.dmFollowMove, DmCells() == V({{13, 21}, {13, 22}, {13, 23}, {14, 16}, {14, 17}, {14, 18}, {14, 19}, {14, 20},
                                            {14, 21}, {14, 22}, {14, 23}, {13, 23}, {12, 23}, {15, 16}, {15, 17}, {15, 18}}) &&
                               g.m_shiftX == 2 && g.m_subX == 2 && g.m_subZ == 5);
        g.m_subZ = 2;
        g_dmRays.clear();
        g.Follow(10.6f, 20.7f);
        WM(r.dmFollowMove, DmCells() == V({{11, 23}, {12, 23}, {13, 23}, {8, 16}, {8, 17}, {8, 18}, {7, 16}, {7, 17}, {7, 18},
                                            {7, 19}, {7, 20}, {7, 21}, {7, 22}, {7, 23}, {13, 16}, {12, 16}, {6, 21}, {6, 22},
                                            {6, 23}}) &&
                               g.m_shiftX == 0 && g.m_subX == 4 && g.m_subZ == 5);
        g.m_subX = 6;
        g_dmRays.clear();
        g.Follow(10.6f, 23.2f);
        WM(r.dmFollowMove, DmCells() == V({{6, 21}, {6, 22}, {12, 23}, {13, 23}, {6, 24}, {7, 24}, {8, 24}, {9, 24}, {10, 24},
                                            {11, 24}, {12, 24}, {13, 24}, {6, 25}, {7, 25}, {8, 25}, {9, 25}, {10, 25}, {11, 25},
                                            {12, 25}, {13, 25}, {13, 23}, {13, 22}, {6, 26}, {7, 26}}) &&
                               g.m_shiftZ == 3 && g.m_subX == 4 && g.m_subZ == 1);
        g.m_subX = 1;
        g_dmRays.clear();
        g.Follow(10.6f, 21.1f);
        WM(r.dmFollowMove, DmCells() == V({{13, 21}, {13, 22}, {13, 23}, {6, 19}, {7, 19}, {6, 18}, {7, 18}, {8, 18}, {9, 18},
                                            {10, 18}, {11, 18}, {12, 18}, {13, 18}, {6, 23}, {6, 22}, {7, 17}, {8, 17}, {9, 17},
                                            {10, 17}, {11, 17}, {12, 17}, {13, 17}}) &&
                               g.m_shiftZ == 1 && g.m_subX == 4 && g.m_subZ == 0);
        g.Free();
        g_dmNoHit = false;
        // The shifts wrap the window, the origin a whole grid on.
        C_DecoratorGrid s;
        s.m_sizeX = 3;
        s.m_sizeZ = 2;
        s.m_originX = s.m_originZ = 0;
        s.ShiftX(false);
        s.ShiftX(false);
        const bool twoX = s.m_shiftX == 2 && s.m_originX == 0;
        s.ShiftX(false);
        const bool wrapX = s.m_shiftX == 0 && s.m_originX == 3;
        s.ShiftX(true);
        const bool backX = s.m_shiftX == 2 && s.m_originX == 0;
        s.ShiftZ(false);
        s.ShiftZ(false);
        const bool wrapZ = s.m_shiftZ == 0 && s.m_originZ == 2;
        s.ShiftZ(true);
        WM(r.dmShift, twoX && wrapX && backX && wrapZ && s.m_shiftZ == 1 && s.m_originZ == 0);
    }

    // --- bending: the bendable models near a point, away from its move ---
    I3D_frame *bm[4] = {};
    {
        C_DecoratorGrid &g = d.m_grid;
        g.m_sizeX = g.m_sizeZ = 8;
        g.m_cellSize = 1.0f;
        g.Init();
        g.m_originX = g.m_originZ = 0;
        S_DecoratorSet *set = d.m_sets[1];
        set->decorators[0]->bendable = true;
        set->decorators[1]->bendable = false;
        // (Cell [4][4] is the world's (0, 0).)
        auto put = [&](int row, int column, const std::string &name, const S_vector &pos, int decorator) {
            S_DecoratorCell &c = g.m_cells[row][column];
            c.model = DmModel(name, pos);
            c.set = 1;
            c.decorator = decorator;
            DmSetLight(c.model, 0.8f);
            return c.model;
        };
        bm[0] = put(4, 4, "b0", S_vector(0.5f, 0, 0.5f), 0);      // at the point
        bm[1] = put(4, 5, "b1", S_vector(1.4f, 0, 0.5f), 0);      // between the radii
        bm[2] = put(5, 4, "b2", S_vector(0.5f, 0, 2.6f), 0);      // beyond the outer
        bm[3] = put(4, 3, "b3", S_vector(0.0f, 0, 0.5f), 1);      // not bendable
        g_gfRandoms = {0.2f};
        g_gfLog.clear();
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), 1.0f, 0.0f, 0.25f, 1.0f, 3.0f, 0.5f, false);
        S_DecoratorBend *near0 = g.m_cells[4][4].bend, *swing = g.m_cells[4][5].bend;
        WM(r.dmBend, g.m_bends.size() == 2 && near0 && swing && !g.m_cells[5][4].bend && !g.m_cells[4][3].bend &&
                         near0->model == bm[0] && near0->cell == &g.m_cells[4][4] && near0->springing == 0 &&
                         near0->speed == 1.0f && near0->angle == 0.0f && near0->light == 0.8f && near0->lightTarget == 0.8f * 0.5f &&
                         g.m_cells[4][4].bending && g.m_cells[4][5].bending && !g.m_cells[4][3].bending);
        // Facing z (the identity), the move along x: AngleBetween2D((0, 1), (1, 0)) < 0: the target kept.
        WM(r.dmBendSpring, near0->target == 1.2566371f && swing->springing == 1 &&
                               swing->target == (0.2f + 1.0f) * (1.2566371f * 0.1f) && GfHas({"rand 0.3"}));
        // Bending already (not springing): left alone; springing: taken on again (speed 1).
        swing->speed = 0.3f;
        near0->speed = 0.3f;
        g_gfRandoms = {0.0f};
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), -1.0f, 0.0f, 0.25f, 1.0f, 3.0f, 0.5f, false);
        WM(r.dmBendSpring, g.m_bends.size() == 2 && near0->speed == 0.3f && swing->speed == 1.0f &&
                               swing->target == -(1.2566371f * 0.1f));
        // Moving along -x: the other way.
        g.m_cells[4][4].bending = false;
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), -1.0f, 0.0f, 0.25f, 1.0f, 3.0f, 0.5f, false);
        WM(r.dmBend, g.m_bends.size() == 3 && g.m_cells[4][4].bend != near0 && g.m_cells[4][4].bend->target == -1.2566371f);
        // Exactly at the inner square: a swing; at the outer: none.
        S_DecoratorCell &c1 = g.m_cells[4][5];
        c1.bending = false;
        c1.bend = nullptr;
        g_gfRandoms = {0.0f};
        const float dd = (1.4f - 0.5f) * (1.4f - 0.5f);
        g.Bend(S_vector(0.5f, 0.0f, 0.5f), 1.0f, 0.0f, dd, 1.0f, 3.0f, 0.5f, false);
        WM(r.dmBendSpring, c1.bend && c1.bend->springing == 1);
        c1.bending = false;
        c1.bend = nullptr;
        const size_t had = g.m_bends.size();
        g.Bend(S_vector(0.5f, 0.0f, 0.5f), 1.0f, 0.0f, 0.25f, dd, 3.0f, 0.5f, false);
        WM(r.dmBend, !c1.bend && g.m_bends.size() == had);
        // The fade: each model's alpha by its distance in cells (/ the reach's two, + the alpha).
        g.m_fadeAlpha = 0.25f;
        char a1[48];
        std::snprintf(a1, sizeof a1, "alpha ch1 %.3f", 1.0f / 4.0f + 0.25f);
        // (The models' first child is their visual: the alpha goes to it.)
        for (int k = 0; k < 4; ++k)
            F_GfNewFrame(FRAME_VISUAL, "ch" + std::to_string(k), bm[k], g_dmVt);
        g_gfLog.clear();
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.5f, true);
        char a0[48], a2[48];
        std::snprintf(a0, sizeof a0, "alpha ch0 %.3f", 0.25f);
        std::snprintf(a2, sizeof a2, "alpha ch2 %.3f", 1.0f / 4.0f + 0.25f);
        WM(r.dmBend, GfHas({a1}) && GfHas({a0}) && GfHas({a2}) && !GfHas({"alpha ch3"}));
        g.m_fadeAlpha = 0.9f;
        g_gfLog.clear();
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.5f, true);
        WM(r.dmBend, GfHas({"alpha ch1 1.000"}) && GfHas({"alpha ch0 0.900"}));
        // Cells off the window's edge wrap (the point at the far corner).
        S_DecoratorCell &corner = g.m_cells[0][0];
        corner.model = DmModel("corner", S_vector(-3.5f, 0, -3.5f));
        corner.set = 1;
        corner.decorator = 0;
        g.Bend(S_vector(3.4f, 0.0f, 3.4f), 1.0f, 0.0f, 0.0f, 1e6f, 2.0f, 0.5f, false);
        WM(r.dmBend, corner.bend != nullptr);
        // Facing x: the target turned round for a move along z.
        S_DecoratorCell &fx = g.m_cells[6][6];
        fx.model = DmModel("fx", S_vector(2.5f, 0, 2.5f));
        fx.model->m_localMatrix(2) = S_vector(2, 0, 0);
        fx.model->m_localMatrix(0) = S_vector(0, 0, -1);
        fx.model->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
        fx.set = 1;
        fx.decorator = 0;
        DmSetLight(fx.model, 0.5f);
        g.Bend(S_vector(2.5f, 0.0f, 2.5f), 0.0f, 1.0f, 1.0f, 2.0f, 2.0f, 0.5f, false);
        WM(r.dmBend, fx.bend && fx.bend->target == -1.2566371f);
        // No model, no decorator: none.
        S_DecoratorCell &empty = g.m_cells[2][2];
        empty.set = 1;
        empty.decorator = 0;
        g.Bend(S_vector(-1.5f, 0.0f, -1.5f), 1.0f, 0.0f, 1.0f, 2.0f, 2.0f, 0.5f, false);
        WM(r.dmBend, !empty.bend);
        g.m_cells[4][4].decorator = -1;
        g.m_cells[4][4].bending = false;
        g.m_cells[4][4].bend = nullptr;
        const size_t before = g.m_bends.size();
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), 1.0f, 0.0f, 0.25f, 0.0f + 0.04f, 3.0f, 0.5f, false);
        WM(r.dmBend, g.m_bends.size() == before);
        g.m_cells[4][4].decorator = 0;
        g.m_cells[4][4].set = 3;
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), 1.0f, 0.0f, 0.25f, 1.0f, 3.0f, 0.5f, false);
        WM(r.dmBend, !g.m_cells[4][4].bend);
        g.m_cells[4][4].set = 1;
        g.m_cells[4][4].decorator = 9;
        g.Bend(S_vector(0.6f, 0.0f, 0.5f), 1.0f, 0.0f, 0.25f, 1.0f, 3.0f, 0.5f, false);
        WM(r.dmBend, !g.m_cells[4][4].bend);
        g.m_cells[4][4].decorator = 0;
    }

    // --- a bend's update: on to the target, the springs back, the light back ---
    {
        I3D_frame *m = DmModel("bu");
        S_DecoratorBend b;
        b.model = m;
        b.target = 1.0f;
        b.speed = 2.0f;
        b.light = 0.5f;
        b.lightTarget = 0.5f;
        g_gfLog.clear();
        WM(r.dmBendUpdate, b.Update(100) && b.angle == 100.0f * 2.0f * 3.0f * 0.001f &&
                               GfLogIs({"dir bu 0.000 0.000 1.000 0.600", "update bu"}));
        WM(r.dmBendUpdate, !b.Update(100) && b.angle == 1.0f);
        // Down.
        b.target = -0.5f;
        b.speed = 1.0f;
        WM(r.dmBendUpdate, b.Update(100) && DmNear(b.angle, 0.7f) && b.Update(250) && DmNear(b.angle, -0.05f));
        WM(r.dmBendUpdate, b.Update(100) && !b.Update(100) && b.angle == -0.5f);
        // Exactly onto the target: done.
        b.angle = 0.0f;
        b.target = 0.3f;
        WM(r.dmBendUpdate, !b.Update(100) && b.angle == 0.3f);
        b.angle = 0.0f;
        b.target = -0.3f;
        WM(r.dmBendUpdate, !b.Update(100) && b.angle == -0.3f);
        // Springing (0.3 the rate): reached, back at -0.9 the target, 0.9 the speed.
        b.springing = 1;
        b.angle = 0.0f;
        b.target = 0.1f;
        b.speed = 1.0f;
        WM(r.dmBendUpdate, b.Update(100) && DmNear(b.angle, 0.03f));
        WM(r.dmBendUpdate, b.Update(300) && b.angle == 0.1f && b.target == 0.1f * -0.9f && b.speed == 0.9f);
        // Under a degree: done, the angle 0.
        b.angle = 0.0f;
        b.target = 0.0174f;
        WM(r.dmBendUpdate, !b.Update(1000) && b.angle == 0.0f && b.target == 0.0174f);
        b.angle = 0.0f;
        b.target = -0.0175f;
        WM(r.dmBendUpdate, b.Update(1000) && b.target == -0.0175f * -0.9f);
        // Not springing: the light back down (0.0003 per ms) to its target, on until there.
        b.springing = 0;
        b.angle = 1.0f;
        b.target = 1.0f;
        b.light = 0.5f;
        b.lightTarget = 0.4f;
        g_gfLog.clear();
        WM(r.dmBendUpdate, b.Update(100) && DmNear(b.light, 0.47f) && GfHas({"lightlevel bu 0.470"}));
        WM(r.dmBendUpdate, b.Update(1000) && b.light == 0.4f);
        WM(r.dmBendUpdate, !b.Update(100));
        // The model's direction (normalized, its roll the angle).
        m->m_localMatrix(2) = S_vector(0.0f, 0.0f, 2.0f);
        m->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
        g_gfLog.clear();
        b.Update(1);
        WM(r.dmBendUpdate, GfHas({"dir bu 0.000 0.000 1.000 1.000"}));
        m->m_localMatrix(2) = S_vector(0.0f, 0.00001f, 0.0f);
        m->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
        g_gfLog.clear();
        b.Update(1);
        WM(r.dmBendUpdate, GfHas({"dir bu 0.000 1.000 0.000 1.000"}));
        m->m_localMatrix(2) = S_vector(-0.00001f, 0.0f, 0.00001f);
        m->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
        g_gfLog.clear();
        b.Update(1);
        WM(r.dmBendUpdate, GfHas({"dir bu -1.000 0.000 0.000 1.000"}));
        m->m_localMatrix(2) = S_vector(0.0f, 0.0f, -0.00001f);
        m->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
        g_gfLog.clear();
        b.Update(1);
        WM(r.dmBendUpdate, GfHas({"dir bu 0.000 0.000 -1.000 1.000"}));
        m->m_localMatrix(2) = S_vector(0.6f, 0.0f, 0.8f);
        m->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
        g_gfLog.clear();
        b.Update(1);
        WM(r.dmBendUpdate, GfHas({"dir bu 0.600 0.000 0.800 1.000"}));

        // --- the bends updated: those done deleted (the last into their place, the cells told) ---
        C_DecoratorGrid &g = d.m_grid;
        for (S_DecoratorBend *x : g.m_bends) {
            if (x->cell)
                x->cell->bend = nullptr;
            delete x;
        }
        g.m_bends.clear();
        S_DecoratorCell c1, c2;
        c1.bending = c2.bending = true;
        auto *done = new S_DecoratorBend(), *going = new S_DecoratorBend(), *last = new S_DecoratorBend();
        done->model = going->model = last->model = m;
        done->target = done->angle = 1.0f;
        done->cell = &c1;
        c1.bend = done;
        going->target = 1.0f;
        going->speed = 1.0f;
        last->springing = 1;
        last->target = last->angle = 0.001f;
        last->cell = &c2;
        c2.bend = last;
        g.m_bends = {done, going, last};
        g.UpdateBends(10);
        WM(r.dmBends, g.m_bends == std::vector<S_DecoratorBend *>({going}) && !c1.bend && c1.bending && !c2.bend && !c2.bending);
        delete going;
        g.m_bends.clear();
    }

    // --- around the player's soldier: the stance's radii, the crouch's fade ---
    {
        C_DecoratorGrid &g = d.m_grid;
        C_Human h;
        I3D_frame *body = DmModel("body", S_vector(0.6f, 0.0f, 0.5f));
        h.m_frame = body;
        h.velocity = S_vector(3.0f, 0.0f, 4.0f);
        g_firstPerson = false;
        g.m_firstPerson = false;
        g.m_stance = 0;
        g.m_fading = false;
        g.m_time = 100;
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 8; ++column)
                g.m_cells[row][column].bending = false, g.m_cells[row][column].bend = nullptr;
        // Standing: within 0.4 bent (0.25 inside darkened).
        g_gfRandoms = {0.0f, 0.0f, 0.0f};
        g.BendAround(&h);
        WM(r.dmHuman, g.m_cells[4][4].bend && g.m_cells[4][4].bend->springing == 0 && !g.m_cells[4][5].bend &&
                          !g.m_fading && g.m_stance == 0);
        const float lightTarget0 = g.m_cells[4][4].bend->lightTarget;
        WM(r.dmHuman, lightTarget0 == 0.8f * 0.9f);
        // Not moving: none.
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        h.velocity = S_vector(0, 0, 0);
        g.BendAround(&h);
        WM(r.dmHuman, g.m_bends.empty());
        // Crouching (stance 2): the fade starts (1 s), the alpha from 1 down; radii 0.75 / 1.
        h.velocity = S_vector(0, 0, 1.0f);
        h.stance = 2;
        g_firstPerson = true;
        // (Every cell's model with its visual, as the game's have.)
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 8; ++column)
                if (I3D_frame *cm = g.m_cells[row][column].model)
                    if (!cm->GetChild(0))
                        F_GfNewFrame(FRAME_VISUAL, std::string("v") + GfName(cm), cm, g_dmVt);
        g_gfRandoms = {0.0f, 0.0f, 0.0f, 0.0f};
        g_gfLog.clear();
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fading && g.m_fadeTime == 900 && g.m_stance == 2 && g.m_firstPerson &&
                          DmNear(g.m_fadeAlpha, 0.9f + 0.05f) && g.m_cells[4][5].bend && GfHas({"alpha ch0 0.950"}));
        // Out of the crouch: the alpha up from 0.05; third person: 1.
        h.stance = 1;
        g.m_time = 300;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fading && g.m_fadeTime == 700 && DmNear(g.m_fadeAlpha, 1.0f - 0.7f + 0.05f));
        g_firstPerson = false;
        g.m_time = 100;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fadeAlpha == 1.0f && g.m_fadeTime == 600 && !g.m_firstPerson);
        // The fade over: off again; standing still while fading: still bending (no direction).
        g.m_time = 700;
        h.velocity = S_vector(0, 0, 0);
        h.stance = 2;
        g.m_stance = 2;
        g_firstPerson = true;
        g.m_firstPerson = true;
        g.BendAround(&h);
        WM(r.dmHuman, !g.m_fading && g.m_fadeTime == 0 && DmNear(g.m_fadeAlpha, 0.05f));
        // A view change while crouching: fading (the time left as it was).
        g_firstPerson = false;
        g.m_time = 0;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fading && g.m_fadeTime == 0 && g.m_fadeAlpha == 1.0f && !g.m_firstPerson);
        g_firstPerson = true;
        g.m_fadeTime = 400;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fading && g.m_fadeTime == 400 && DmNear(g.m_fadeAlpha, 0.4f + 0.05f) && g.m_firstPerson);
        g.m_firstPerson = false;
        g.m_fadeTime = 400;
        g.m_fading = false;
        g.m_time = 100;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_firstPerson && g.m_fadeTime == 300);
        // Standing up from a crouch only fades in the first person (third: alpha 1, clamped).
        h.stance = 0;
        g.m_fading = false;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fading && g.m_fadeTime == 900 && DmNear(g.m_fadeAlpha, 1.0f - 0.9f + 0.05f));
        g.m_fading = false;
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        // The radii per stance: 0.6 m off (between 0.5 and 0.75's 1).
        I3D_frame *off = g.m_cells[4][4].model;
        GfPlace(off, S_vector(0.6f, 0.0f, 0.5f + 0.6f));
        GfPlace(g.m_cells[4][5].model, S_vector(100, 0, 100));
        h.velocity = S_vector(1, 0, 0);
        auto bentAt = [&](int stance) {
            h.stance = stance;
            g.m_stance = stance;
            g.m_fading = false;
            g_gfRandoms = {0.0f, 0.0f, 0.0f};
            g.BendAround(&h);
            S_DecoratorBend *b = g.m_cells[4][4].bend;
            const int kind = b ? (b->springing ? 2 : 1) : 0;
            for (S_DecoratorBend *x : g.m_bends) {
                x->cell->bend = nullptr;
                x->cell->bending = false;
                delete x;
            }
            g.m_bends.clear();
            return kind;
        };
        WM(r.dmHuman, bentAt(0) == 0 && bentAt(1) == 0 && bentAt(3) == 0 && bentAt(2) == 1);
        GfPlace(off, S_vector(0.6f, 0.0f, 0.5f + 0.45f));
        WM(r.dmHuman, bentAt(0) == 0 && bentAt(1) == 2 && bentAt(2) == 1);
        GfPlace(off, S_vector(0.6f, 0.0f, 0.5f + 0.27f));
        WM(r.dmHuman, bentAt(0) == 2 && bentAt(1) == 1);
        GfPlace(off, S_vector(0.6f, 0.0f, 0.5f));
        GfPlace(g.m_cells[4][5].model, S_vector(1.4f, 0, 0.5f));
        h.m_frame = nullptr;
    }

    // --- around a car: each wheel (on wheels), each axle's middle (on tracks) ---
    {
        C_DecoratorGrid &g = d.m_grid;
        g.m_cellSize = 0.5f;
        auto *car = new C_Car();
        C_Vehicle &v = car->m_vehicle;
        S_VehicleWheel w[4] = {};
        S_VehicleWheel *wheels[4] = {&w[0], &w[1], &w[2], &w[3]};
        v.m_wheels = wheels;
        v.m_wheelCount = 2;
        w[0].unknown050 = S_vector4{0.4f, 0.0f, 0.5f, 0.0f};
        w[1].worldPos = S_vector4{3.4f, 0.0f, 0.5f, 0.0f};
        w[1].unknown050 = S_vector4{9.0f, 0.0f, 9.0f, 0.0f};
        w[0].worldPos = S_vector4{9.0f, 0.0f, 5.0f, 0.0f};
        w[0].unknown0B0 = S_vector(1.0f, 0.0f, 0.0f);
        w[1].unknown0B0 = S_vector(1.0f, 0.0f, 0.0f);
        v.m_speed = 0.05f;
        g_gfRandoms = {0.0f, 0.0f};
        g.BendAround(car);
        WM(r.dmCar, g.m_bends.empty());
        // At speed: wheel 0 0.2 towards wheel 1 (0.6, 0, 0.5): the model there within 0.5 (0.25 inside).
        v.m_speed = 0.1f;
        g.BendAround(car);
        WM(r.dmCar, g.m_cells[4][4].bend && g.m_cells[4][4].bend->springing == 0 &&
                        g.m_cells[4][4].bend->lightTarget == 0.8f * 0.75f && !g.m_cells[4][5].bend);
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        // Wheel 1 towards wheel 0 (the other of its axle).
        w[1].unknown050 = S_vector4{1.6f, 0.0f, 0.5f, 0.0f};
        w[0].worldPos = S_vector4{-5.0f, 0.0f, 0.5f, 0.0f};
        w[0].unknown050 = S_vector4{50.0f, 0.0f, 50.0f, 0.0f};
        g.BendAround(car);
        WM(r.dmCar, g.m_cells[4][5].bend && g.m_cells[4][5].bend->springing == 0 && !g.m_cells[4][4].bend);
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        // Tracks: the axle's middle, 2.4 m to 2.5 m: swung.
        v.m_tracksL.resize(1);
        w[0].unknown050 = S_vector4{0.6f - 3.0f, 0.0f, 0.5f + 2.45f, 0.0f};
        w[1].unknown050 = S_vector4{0.6f + 3.0f, 0.0f, 0.5f + 2.45f, 0.0f};
        v.m_velocity = S_vector4{1.0f, 0.0f, 0.0f, 0.0f};
        g_gfRandoms = {0.0f, 0.0f, 0.0f};
        g.BendAround(car);
        WM(r.dmCar, g.m_cells[4][4].bend && g.m_cells[4][4].bend->springing == 1);
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        v.m_tracksL.clear();
        v.m_tracksR.resize(1);
        w[0].unknown050 = S_vector4{0.6f - 3.0f, 0.0f, 0.5f + 2.3f, 0.0f};
        w[1].unknown050 = S_vector4{0.6f + 3.0f, 0.0f, 0.5f + 2.3f, 0.0f};
        g.BendAround(car);
        WM(r.dmCar, g.m_cells[4][4].bend && g.m_cells[4][4].bend->springing == 0);
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        v.m_tracksR.clear();
        // Through Follow: the cars within the grid's half width of its centre.
        v.m_speed = 0.1f;
        w[0].unknown050 = S_vector4{0.4f, 0.0f, 0.5f, 0.0f};
        w[1].worldPos = S_vector4{3.4f, 0.0f, 0.5f, 0.0f};
        w[1].unknown050 = S_vector4{90.0f, 0.0f, 90.0f, 0.0f};
        w[0].worldPos = S_vector4{90.0f, 0.0f, 50.0f, 0.0f};
        car->m_position = S_vector4{1.9f, 0.0f, 0.0f, 0.0f};
        g_dmNoHit = true;
        g.Follow(0.25f, 0.25f);
        WM(r.dmCar, g.m_cells[4][4].bend != nullptr);
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        car->m_position = S_vector4{2.0f, 0.0f, 0.0f, 0.0f};
        g.Follow(0.25f, 0.25f);
        WM(r.dmCar, g.m_bends.empty());
        car->m_position = S_vector4{0.5f, 0.0f, -1.9f, 0.0f};
        g.Follow(0.25f, 0.25f);
        WM(r.dmCar, !g.m_bends.empty());
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        // The player's soldier on foot too (in a vehicle: not).
        C_Human h;
        h.m_frame = DmModel("walker", S_vector(0.6f, 0.0f, 0.5f));
        h.velocity = S_vector(1, 0, 0);
        h.stance = 0;
        g.m_stance = 0;
        car->m_position = S_vector4{100.0f, 0.0f, 0.0f, 0.0f};
        g_gfActive = &h;
        g.Follow(0.25f, 0.25f);
        WM(r.dmCar, g.m_cells[4][4].bend != nullptr);
        for (S_DecoratorBend *x : g.m_bends) {
            x->cell->bend = nullptr;
            x->cell->bending = false;
            delete x;
        }
        g.m_bends.clear();
        h.vehicle = car;
        g.Follow(0.25f, 0.25f);
        WM(r.dmCar, g.m_bends.empty());
        h.vehicle = nullptr;
        g_gfActive = nullptr;
        h.m_frame = nullptr;
        g_dmNoHit = false;
        v.m_wheels = nullptr;
        v.m_wheelCount = 0;
        delete car;
        g.m_cellSize = 1.0f;
    }

    // --- the update: the detail option followed, the sway, the grid to the camera or the soldier ---
    {
        C_DecoratorGrid &g = d.m_grid;
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 8; ++column)
                g.m_cells[row][column].model = nullptr;
        g.Free();
        g.m_sizeX = g.m_sizeZ = 4;
        g.m_cellSize = 1.0f;
        g.Init();
        I3D_frame *cam = GfFrame(FRAME_CAMERA, "dcam");
        WmField<I3D_frame *>(scene, 0x15C) = cam;
        GfPlace(cam, S_vector(30.5f, 5.0f, -20.5f));
        I3D_frame *cm = DmModel("cm");
        g.m_cells[0][0].model = cm;
        g.m_cells[0][0].set = 1;
        g.m_cells[0][0].decorator = 0;
        // Off by the option: switched off (once); on again: switched on.
        d.m_on = true;
        g_decoratorsEnabled = true;
        details.switches[DETAIL_DECORATORS] = false;
        g_gfLog.clear();
        g_dmNoHit = true;
        d.Update(16);
        WM(r.dmUpdate, !g_decoratorsEnabled && !d.m_on && GfLogIs({"on cm 0", "update cm"}));
        g_gfLog.clear();
        d.Update(16);
        WM(r.dmUpdate, g_gfLog.empty());
        details.switches[DETAIL_DECORATORS] = true;
        d.Update(16);
        WM(r.dmUpdate, g_decoratorsEnabled && d.m_on && GfHas({"on cm 1", "update cm"}));
        g.m_cells[0][0].model = nullptr;
        // To the camera (the grid there); the player's soldier unless tactical or in a cutscene.
        g.m_originX = g.m_originZ = 1000;
        g_dmRays.clear();
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == 30 && g.m_originZ == -21 && g.m_time == 16 && g_dmRays.size() == 16);
        C_Human h;
        I3D_frame *body = DmModel("ubody", S_vector(-40.5f, 0.0f, 60.5f));
        h.m_frame = body;
        g_gfActive = &h;
        d.Update(20);
        WM(r.dmUpdate, g.m_originX == -41 && g.m_originZ == 60 && g.m_time == 20);
        g_dmTactical = true;
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == 30);
        g_dmTactical = false;
        size_t running = 0;
        for (size_t o = 0; o < sizeof g_dmScriptModule && !running; ++o) {
            g_dmScriptModule[o] = 1;
            if (reinterpret_cast<C_ScriptModule *>(g_dmScriptModule)->IsCutsceneRunning())
                running = o;
            g_dmScriptModule[o] = 0;
        }
        WmField<bool>(g_dmScriptModule, running) = true;
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == 30);
        WmField<bool>(g_dmScriptModule, running) = false;
        reinterpret_cast<C_ScriptModule *>(g_dmScriptModule)->SetFakeCutscene(true);
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == 30);
        reinterpret_cast<C_ScriptModule *>(g_dmScriptModule)->SetFakeCutscene(false);
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == -41);
        g_gfActive = nullptr;
        h.m_frame = nullptr;
        // Off, or no sets: no move.
        g.m_originX = 7;
        d.m_on = false;
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == 7);
        d.m_on = true;
        std::vector<S_DecoratorSet *> sets;
        sets.swap(d.m_sets);
        d.Update(16);
        WM(r.dmUpdate, g.m_originX == 7);
        sets.swap(d.m_sets);
        // The bends updated with the time.
        auto *done = new S_DecoratorBend();
        done->model = cm;
        done->target = done->angle = 1.0f;
        g.m_bends = {done};
        d.Update(16);
        WM(r.dmUpdate, g.m_bends.empty());

        // --- the sway: each animated decorator's vertices from rest by the time's sine ---
        S_Decorator &sw = *da;
        S_GfMesh *mesh = GfMesh("swm", {DmLevel({S_vector(0, 0, 0), S_vector(0, 0, 0)}), DmLevel({S_vector(0, 0, 0)})});
        sw.mesh = reinterpret_cast<I3D_mesh *>(mesh);
        float *v0 = &mesh->levels[0]->vertices[0].pos.x, *v1 = &mesh->levels[0]->vertices[1].pos.x;
        sw.vertices = {v0, v1};
        sw.restPositions = {S_vector4{1.0f, 2.0f, 3.0f, 0.0f}, S_vector4{-1.0f, 0.5f, 0.25f, 0.0f}};
        sw.animation = 1;
        sw.amplitude = 0.3f;
        sw.frequency = 2.0f;
        auto *other = new S_Decorator();
        d.m_animated = {other, &sw};
        GetGame()->m_time = 1234;
        I3D_frame *orig = da->pool->models[0];
        if (!orig->GetChild(0))
            F_GfNewFrame(FRAME_VISUAL, "origv", orig, g_dmVt);
        g_gfLog.clear();
        d.Update(16);
        const float phase = static_cast<float>(1234u) * 2.0f * 0.001f + 1.0f;
        const float s = std::sin(phase) * 0.3f;
        const float c = std::cos(phase) * s;
        WM(r.dmSway, v0[0] == 1.0f && v0[1] == std::sqrt((2.0f * 2.0f + 2.0f * 2.0f) - s * s) && v0[2] == s + 3.0f &&
                         v1[0] == -1.0f && v1[1] == std::sqrt((0.5f * 0.5f + 0.5f * 0.5f) - s * s) && v1[2] == s + 0.25f &&
                         GfHas({"lockverts 0", "lockverts 0", "unlockverts", "bounds", "unlockverts", "bounds",
                                std::string("refresh ") + GfName(orig->GetChild(0)) + " 0"}));
        sw.animation = 2;
        v0[0] = 0.0f;
        d.Update(16);
        WM(r.dmSway, v0[0] == 1.0f && v0[1] == std::sqrt((2.0f * 2.0f + 2.0f * 2.0f) - c * c) && v0[2] == c + 3.0f);
        sw.animation = 3;
        v0[2] = 99.0f;
        d.Update(16);
        WM(r.dmSway, v0[2] == 99.0f);
        // Another time, another index: the phase moves.
        sw.animation = 1;
        d.m_animated = {&sw};
        GetGame()->m_time = 5000;
        d.Update(16);
        const float phase2 = static_cast<float>(5000u) * 2.0f * 0.001f + 0.0f;
        WM(r.dmSway, v0[2] == std::sin(phase2) * 0.3f + 3.0f);
        // Without its pool's models: no refresh.
        std::vector<I3D_frame *> keep;
        keep.swap(da->pool->models);
        g_gfLog.clear();
        d.Update(16);
        WM(r.dmSway, !GfHas({"refresh"}));
        keep.swap(da->pool->models);
        d.m_animated.clear();
        delete other;
        sw.mesh = nullptr;
        sw.vertices.clear();
        sw.restPositions.clear();
        sw.animation = 0;
        WmField<I3D_frame *>(scene, 0x15C) = nullptr;
        g_dmNoHit = false;
        GetGame()->m_time = 0;
    }

    // ==== more cases (mutation run 1's survivors) ====
    {
        C_DecoratorGrid &g = d.m_grid;
        using V = std::vector<std::pair<int, int>>;
        S_DecoratorSet *set = d.m_sets[1];
        S_Decorator *bendable = set->decorators[0];
        bendable->bendable = true;
        auto fresh = [&](int size, float cellSize) {
            g.Free();
            for (S_DecoratorBend *x : g.m_bends)
                delete x;
            g.m_bends.clear();
            g.m_sizeX = g.m_sizeZ = size;
            g.m_cellSize = cellSize;
            g.Init();
        };
        auto clearBends = [&]() {
            for (S_DecoratorBend *x : g.m_bends) {
                if (x->cell) {
                    x->cell->bend = nullptr;
                    x->cell->bending = false;
                }
                delete x;
            }
            g.m_bends.clear();
        };
        // A model in cell (row, column), bendable, lit 0.8.
        auto put = [&](int row, int column, const std::string &name, const S_vector &pos) {
            S_DecoratorCell &c = g.m_cells[row][column];
            c.model = DmModel(name, pos);
            c.set = 1;
            c.decorator = 0;
            DmSetLight(c.model, 0.8f);
            return c.model;
        };
        auto kind = [&](int row, int column) {
            const S_DecoratorBend *b = g.m_cells[row][column].bend;
            return b ? (b->springing ? 2 : 1) : 0;
        };
        g_dmNoHit = true;
        g_gfActive = nullptr;

        // --- Follow: the sub indices at their edges, the shifts in, a far jump with a shift ---
        fresh(8, 1.0f);
        g.m_originX = g.m_originZ = 1000;
#include "decorators2_follow.inc"
        // A one-cell grid: always all anew.
        fresh(1, 1.0f);
        g_dmRays.clear();
        g.Follow(5.5f, 5.5f);
        WM(r.dmFollow, g_dmRays.size() == 1 && g.m_originX == 5);
        // 10 cells on in x, then in z, on a 64-cell grid from (-5, -5) (642 cells each, by the model).
        fresh(64, 1.0f);
        g.m_originX = g.m_originZ = -5;
        g.m_subX = g.m_subZ = 32;
        g_dmRays.clear();
        g.Follow(5.5f, -4.5f);
        const size_t alongX = g_dmRays.size();
        g_dmRays.clear();
        g.Follow(5.5f, 5.5f);
        WM(r.dmFollowMove, alongX == 642 && g_dmRays.size() == 642 && g.m_shiftX == 10 && g.m_shiftZ == 10 &&
                               g.m_originX == -5 && g.m_originZ == -5);
        // All anew: no bends then (the soldier there).
        fresh(8, 1.0f);
        g.Follow(0.5f, 0.5f);
        C_Human h;
        h.m_frame = DmModel("sbody", S_vector(5.6f, 0.0f, 0.5f));
        h.velocity = S_vector(1, 0, 0);
        put(4, 4, "s0", S_vector(5.6f, 0.0f, 0.5f));
        g_gfActive = &h;
        g.Follow(5.5f, 0.5f);
        WM(r.dmFollow, g.m_originX == 5 && g.m_bends.empty());
        g_gfActive = nullptr;
        // Shifting z back to 0: no wrap.
        C_DecoratorGrid s;
        s.m_sizeZ = 3;
        s.m_shiftZ = 1;
        s.m_originZ = 0;
        s.ShiftZ(true);
        WM(r.dmShift, s.m_shiftZ == 0 && s.m_originZ == 0);

        // --- the cars over the grid: off its centre (origin 6, 6 of half-metre cells: 3, 3) ---
        fresh(8, 0.5f);
        g.m_originX = g.m_originZ = 6;
        g.m_subX = g.m_subZ = 4;
        {
            auto *car = new C_Car();
            C_Vehicle &v = car->m_vehicle;
            S_VehicleWheel w[2] = {};
            S_VehicleWheel *wheels[2] = {&w[0], &w[1]};
            v.m_wheels = wheels;
            v.m_wheelCount = 2;
            v.m_speed = 1.0f;
            w[0].unknown050 = S_vector4{3.2f, 0.0f, 3.3f, 0.0f};
            w[1].worldPos = S_vector4{6.2f, 0.0f, 3.3f, 0.0f};
            w[1].unknown050 = w[0].worldPos = S_vector4{90.0f, 0.0f, 90.0f, 0.0f};
            w[0].unknown0B0 = w[1].unknown0B0 = S_vector(1, 0, 0);
            I3D_frame *cm = put(4, 4, "cc", S_vector(3.4f, 0.0f, 3.3f));
            (void)cm;
            auto bendsWith = [&](float x, float z) {
                car->m_position = S_vector4{x, 0.0f, z, 0.0f};
                g.Follow(3.25f, 3.25f);
                const bool any = g.m_cells[4][4].bend != nullptr;
                clearBends();
                return any;
            };
            WM(r.dmCar, bendsWith(4.5f, 4.2f) && !bendsWith(4.5f, 4.6f) && bendsWith(1.5f, 1.8f) && !bendsWith(1.5f, 1.4f) &&
                            !bendsWith(7.5f, 3.0f) && g.m_originX == 6);
            v.m_wheels = nullptr;
            v.m_wheelCount = 0;
            delete car;
        }

        // --- a wheel's point and the tracks' middle, probed: rings of models around where it should be ---
        // (8 models each at the radii, in a 64 x 64 grid of 0.1 m cells centred there.)
        auto probe = [&](const S_vector &p, std::initializer_list<float> radii, const std::function<void()> &run) {
            fresh(64, 0.1f);
            g.m_originX = static_cast<int>(p.x * 10.0f);
            g.m_originZ = static_cast<int>(p.z * 10.0f);
            int k = 0;
            for (float radius : radii) {
                for (int n = 0; n < 8; ++n) {
                    const float a = static_cast<float>(n) * 0.7853982f;
                    put(28 + k, 28 + n, "r" + std::to_string(k) + std::to_string(n),
                        S_vector(p.x + radius * std::cos(a), p.y, p.z + radius * std::sin(a)));
                }
                ++k;
            }
            g_gfLog.clear();
            run();
            std::vector<int> kinds;
            for (int i = 0; i < k; ++i)
                for (int n = 0; n < 8; ++n)
                    kinds.push_back(kind(28 + i, 28 + n));
            clearBends();
            return kinds;
        };
        auto rings = [](std::initializer_list<int> perRing) {
            std::vector<int> out;
            for (int v : perRing)
                out.insert(out.end(), 8, v);
            return out;
        };
        {
            auto *car = new C_Car();
            C_Vehicle &v = car->m_vehicle;
            S_VehicleWheel w[4] = {};
            S_VehicleWheel *wheels[4] = {&w[0], &w[1], &w[2], &w[3]};
            v.m_wheels = wheels;
            v.m_wheelCount = 4;
            v.m_speed = 1.0f;
            for (int i = 0; i < 4; ++i) {
                w[i].unknown050 = w[i].worldPos = S_vector4{50.0f + 10.0f * i, 0.0f, 50.0f, 0.0f};
                w[i].unknown0B0 = S_vector(1, 0, 0);
            }
            // Wheel 2 (an axle's left: towards wheel 3), its point 0.2 along (3, 2, 4) normalized.
            const S_vector W(1.0f, 0.5f, 2.0f);
            w[2].unknown050 = S_vector4{W.x, W.y, W.z, 0.0f};
            w[3].worldPos = S_vector4{W.x + 3.0f, W.y + 2.0f, W.z + 4.0f, 0.0f};
            S_vector dd(w[3].worldPos.x - W.x, w[3].worldPos.y - W.y, w[3].worldPos.z - W.z);
            const float kk = 1.0f / std::sqrt((dd.z * dd.z + dd.y * dd.y) + dd.x * dd.x);
            const S_vector p(dd.x * kk * 0.2f + W.x, dd.y * kk * 0.2f + W.y, dd.z * kk * 0.2f + W.z);
            auto bend = [&]() { g.BendAround(car); };
            WM(r.dmCar, probe(p, {0.49f, 0.55f, 0.61f}, bend) == rings({1, 2, 0}) && !GfHas({"alpha"}));
            // A square of exactly 1.5: normalized.
            w[3].worldPos = S_vector4{W.x + 1.0f, W.y + 0.5f, W.z + 0.5f, 0.0f};
            {
                const float k15 = 1.0f / std::sqrt(1.5f);
                const S_vector q(1.0f * k15 * 0.2f + W.x, 0.5f * k15 * 0.2f + W.y, 0.5f * k15 * 0.2f + W.z);
                WM(r.dmCar, probe(q, {0.49f, 0.55f, 0.61f}, bend) == rings({1, 2, 0}));
            }
            // Short directions: x, z, y (the first not 0) made 1; none: y 1.
            const S_vector tiny[4] = {S_vector(1e-5f, 0, 0), S_vector(0, 0, 1e-5f), S_vector(0, 1e-5f, 0), S_vector(0, 0, 0)};
            const S_vector unit[4] = {S_vector(1, 0, 0), S_vector(0, 0, 1), S_vector(0, 1, 0), S_vector(0, 1, 0)};
            for (int t = 0; t < 4; ++t) {
                w[3].worldPos = S_vector4{W.x + tiny[t].x, W.y + tiny[t].y, W.z + tiny[t].z, 0.0f};
                const S_vector q(unit[t].x * 0.2f + W.x, unit[t].y * 0.2f + W.y, unit[t].z * 0.2f + W.z);
                const bool ok = probe(q, {0.49f, 0.55f, 0.61f}, bend) == rings({1, 2, 0});
                if (!ok && g_wmTrace)
                    std::printf("  short direction %d\n", t);
                WM(r.dmCar, ok);
            }
            // Tracks: the middle of wheels 0 and 1 (the wheels' own points 3 m off), along the velocity.
            v.m_wheelCount = 2;
            v.m_tracksL.resize(1);
            v.m_velocity = S_vector4{0.0f, 0.0f, 1.0f, 0.0f};
            const S_vector mid(5.0f, 0.5f, 5.0f);
            w[0].unknown050 = S_vector4{mid.x - 3.0f, mid.y, mid.z + 0.4f, 0.0f};
            w[1].unknown050 = S_vector4{mid.x + 3.0f, mid.y, mid.z - 0.4f, 0.0f};
            WM(r.dmCar, probe(mid, {2.39f, 2.45f, 2.51f}, bend) == rings({1, 2, 0}) && !GfHas({"alpha"}));
            v.m_tracksL.clear();
            // On wheels, the tracks' Bend not done too (wheels 0 and 1 3 m either side of the probe).
            v.m_wheelCount = 4;
            w[0].unknown050 = w[0].worldPos = S_vector4{p.x - 3.0f, p.y, p.z, 0.0f};
            w[1].unknown050 = w[1].worldPos = S_vector4{p.x + 3.0f, p.y, p.z, 0.0f};
            w[3].worldPos = S_vector4{W.x + 3.0f, W.y + 2.0f, W.z + 4.0f, 0.0f};
            WM(r.dmCar, probe(p, {0.49f, 0.55f, 0.61f}, bend) == rings({1, 2, 0}));
            v.m_wheels = nullptr;
            v.m_wheelCount = 0;
            delete car;
        }

        // --- Bend: the window's edges (the point's cell exactly at 0, below 1), y, the sets' ends ---
        fresh(8, 1.0f);
        g.m_originX = g.m_originZ = 0;
        // (The window is centre - reach .. centre + reach - 1; centre 4 + the point's cell.)
        auto bendAt = [&](const S_vector &pos, int row, int column, const S_vector &modelPos) {
            put(row, column, "e" + std::to_string(row) + std::to_string(column), modelPos);
            g.Bend(pos, 1.0f, 0.0f, 1.0f, 2.0f, 2.0f, 0.5f, false);
            const int k = kind(row, column);
            clearBends();
            g.m_cells[row][column].model = nullptr;
            return k;
        };
        WM(r.dmBend, bendAt(S_vector(0.0f, 0, 0.5f), 4, 5, S_vector(0.0f, 0, 0.5f)) == 1 &&
                         bendAt(S_vector(0.5f, 0, 0.0f), 5, 4, S_vector(0.5f, 0, 0.0f)) == 1 &&
                         bendAt(S_vector(0.3f, 0, 0.5f), 4, 5, S_vector(0.3f, 0, 0.5f)) == 1 &&
                         bendAt(S_vector(0.5f, 0, 0.3f), 5, 4, S_vector(0.5f, 0, 0.3f)) == 1 &&
                         bendAt(S_vector(0.5f, 0, 0.5f), 4, 6, S_vector(0.5f, 0, 0.5f)) == 0 &&
                         bendAt(S_vector(0.5f, 0, 0.5f), 6, 4, S_vector(0.5f, 0, 0.5f)) == 0);
        // Below the window's start: wrapped (-1 is column 7).
        WM(r.dmBend, bendAt(S_vector(-3.5f, 0, 0.5f), 4, 7, S_vector(-3.5f, 0, 0.5f)) == 1 &&
                         bendAt(S_vector(0.5f, 0, -3.5f), 7, 4, S_vector(0.5f, 0, -3.5f)) == 1);
        // The height counts: 0.6 above (close), 1.6 below (none).
        WM(r.dmBend, bendAt(S_vector(0.5f, 1.0f, 0.5f), 4, 4, S_vector(0.5f, 1.6f, 0.5f)) == 1 &&
                         bendAt(S_vector(0.5f, 1.0f, 0.5f), 4, 4, S_vector(0.5f, -0.6f, 0.5f)) == 0);
        // A cell whose set or decorator is just past the end: none.
        put(4, 4, "end", S_vector(0.5f, 0, 0.5f));
        g.m_cells[4][4].set = static_cast<int>(d.m_sets.size());
        g.Bend(S_vector(0.5f, 0, 0.5f), 1.0f, 0.0f, 1.0f, 2.0f, 2.0f, 0.5f, false);
        WM(r.dmBend, !g.m_cells[4][4].bend);
        g.m_cells[4][4].set = 1;
        g.m_cells[4][4].decorator = static_cast<int>(set->decorators.size());
        g.Bend(S_vector(0.5f, 0, 0.5f), 1.0f, 0.0f, 1.0f, 2.0f, 2.0f, 0.5f, false);
        WM(r.dmBend, !g.m_cells[4][4].bend);
        g.m_cells[4][4].decorator = 0;
        // A spring taken on close: no longer springing.
        g.Bend(S_vector(0.5f, 0, 1.5f), 1.0f, 0.0f, 0.25f, 2.0f, 2.0f, 0.5f, false);
        const bool swung = kind(4, 4) == 2;
        g.Bend(S_vector(0.5f, 0, 0.5f), 1.0f, 0.0f, 0.25f, 2.0f, 2.0f, 0.5f, false);
        WM(r.dmBendSpring, swung && kind(4, 4) == 1 && g.m_cells[4][4].bend->speed == 1.0f);
        clearBends();
        // The move's side: exactly along the model (0: kept), 0.3 rad off it (turned).
        auto target = [&](float dirX, float dirZ, const S_vector &facing) {
            I3D_frame *m = g.m_cells[4][4].model;
            m->m_localMatrix(2) = facing;
            m->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            g.Bend(S_vector(0.5f, 0, 0.5f), dirX, dirZ, 1.0f, 2.0f, 2.0f, 0.5f, false);
            const float t = g.m_cells[4][4].bend ? g.m_cells[4][4].bend->target : 0.0f;
            clearBends();
            return t;
        };
        WM(r.dmBendSpring, target(0.0f, 1.0f, S_vector(0, 0, 1)) > 0.0f &&
                               target(-std::sin(0.3f), std::cos(0.3f), S_vector(0, 0, 1)) < 0.0f &&
                               target(-std::sin(0.6f), std::cos(0.6f), S_vector(0, 0, 1)) < 0.0f);
        // A short facing: its x (not 0) made 1 (z kept), its z made 1 when x is 0.
        WM(r.dmBendSpring, target(std::cos(0.5f), std::sin(0.5f), S_vector(1e-5f, 0, 1e-5f)) < 0.0f &&
                               target(1.0f, 0.0f, S_vector(0, 0, 1e-5f)) > 0.0f &&
                               target(std::cos(1.0f), std::sin(1.0f), S_vector(0, 0, 1e-5f)) > 0.0f &&
                               target(0.0f, 1.0f, S_vector(1e-5f, 0, 0)) < 0.0f);
        g.m_cells[4][4].model->m_localMatrix(2) = S_vector(0, 0, 1);
        g.m_cells[4][4].model->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;

        // --- around the soldier: the crouch's radii, its alpha at rest, the move's side ---
        h.m_frame = DmModel("sbody2", S_vector(0.5f, 0.0f, 0.5f));
        put(4, 5, "side", S_vector(5.0f, 0.0f, 5.0f));
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 8; ++column)
                if (I3D_frame *cm = g.m_cells[row][column].model)
                    if (!cm->GetChild(0))
                        F_GfNewFrame(FRAME_VISUAL, std::string("v") + GfName(cm), cm, g_dmVt);
        auto around = [&](int stance, float off, const S_vector &velocity) {
            I3D_frame *m = g.m_cells[4][4].model;
            GfPlace(m, S_vector(0.5f, 0.0f, 0.5f + off));
            h.stance = stance;
            g.m_stance = stance;
            g.m_fading = false;
            h.velocity = velocity;
            g_gfRandoms = {0.0f, 0.0f, 0.0f};
            g.BendAround(&h);
            const int k = kind(4, 4);
            clearBends();
            return k;
        };
        g_firstPerson = g.m_firstPerson = true;
        g.m_time = 0;
        WM(r.dmHuman, around(2, 0.85f, S_vector(1, 0, 0)) == 2 && around(2, 1.2f, S_vector(1, 0, 0)) == 0 &&
                          around(1, 0.4f, S_vector(1, 0, 0)) == 2 && around(1, 0.28f, S_vector(1, 0, 0)) == 1);
        // Crouching at rest (not fading): the alpha by distance all the same (reach 3: / 6).
        g.m_fadeAlpha = 0.3f;
        g_gfLog.clear();
        around(2, 0.0f, S_vector(1, 0, 0));
        char a1[48];
        std::snprintf(a1, sizeof a1, "alpha v%s %.3f", GfName(g.m_cells[4][5].model), 1.0f / 6.0f + 0.3f);
        WM(r.dmHuman, GfHas({a1}) && !g.m_fading);
        // The fade at its start crouching: 1 + 0.05, clamped.
        h.stance = 2;
        g.m_stance = 0;
        g.m_fading = false;
        g.BendAround(&h);
        WM(r.dmHuman, g.m_fading && g.m_fadeTime == 1000 && g.m_fadeAlpha == 1.0f);
        clearBends();
        // The move's side counts (fading or not): along -x turns the target (facing z).
        auto side = [&](bool fading, const S_vector &velocity, const S_vector &facing) {
            I3D_frame *m = g.m_cells[4][4].model;
            GfPlace(m, S_vector(0.5f, 0.0f, 0.5f));
            m->m_localMatrix(2) = facing;
            m->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
            h.stance = 0;
            g.m_stance = 0;
            g.m_fading = fading;
            g.m_fadeTime = 500;
            h.velocity = velocity;
            g.BendAround(&h);
            const float t = g.m_cells[4][4].bend ? g.m_cells[4][4].bend->target : 0.0f;
            clearBends();
            return t;
        };
        WM(r.dmHuman, side(true, S_vector(-1, 0, 0), S_vector(0, 0, 1)) < 0.0f && side(false, S_vector(-1, 0, 0), S_vector(0, 0, 1)) < 0.0f &&
                          side(true, S_vector(0.6f, 0, 0.8f), S_vector(1, 0, 0)) < 0.0f &&
                          side(false, S_vector(0.6f, 0, 0.8f), S_vector(1, 0, 0)) < 0.0f &&
                          side(false, S_vector(0.6f, 0, -0.8f), S_vector(1, 0, 0)) > 0.0f);
        g.m_cells[4][4].model->m_localMatrix(2) = S_vector(0, 0, 1);
        h.m_frame = nullptr;

        // --- a bend's direction: a square of 1.5 normalized (x and y too); short ones: +x, +z, none ---
        {
            I3D_frame *m = DmModel("bd");
            S_DecoratorBend b;
            b.model = m;
            b.target = b.angle = 1.0f;
            auto dirOf = [&](const S_vector &z) {
                m->m_localMatrix(2) = z;
                m->m_frameFlags |= FRMFLAGS_LMATRIX_VALID;
                g_gfLog.clear();
                b.Update(1);
                return g_gfLog.empty() ? std::string() : g_gfLog[0];
            };
            const float k = 1.0f / std::sqrt(1.5f);
            char want[64];
            std::snprintf(want, sizeof want, "dir bd %.3f %.3f %.3f 1.000", k, 0.5f * k, 0.5f * k);
            WM(r.dmBendUpdate, dirOf(S_vector(1.0f, 0.5f, 0.5f)) == want &&
                                   dirOf(S_vector(1e-5f, 0, 1e-5f)) == "dir bd 1.000 0.000 0.000 1.000" &&
                                   dirOf(S_vector(0, 0, 1e-5f)) == "dir bd 0.000 0.000 1.000 1.000" &&
                                   dirOf(S_vector(0, 0, 0)) == "dir bd 0.000 1.000 0.000 1.000");
        }

        // --- the rest: a set index at the end, a diagonal box, the seed off z, the bends' list ---
        {
            int index = 77;
            bool rr = true, align = false;
            float height = 0.0f;
            g_gfLog.clear();
            WM(r.dmTake, !d.TakeModel(static_cast<unsigned>(d.m_sets.size()), 0, &index, &rr, &height, &align) && g_gfLog.empty());
            // A diagonal box: the vertices near the line along (-2, 0, -2) through (2, 1, 2).
            S_Decorator dec;
            I3D_frame *m = DmModel("dg");
            S_GfMesh *mesh = GfMesh("dgm", {DmLevel({S_vector(1, 1, 1), S_vector(1.05f, 1, 0.95f), S_vector(1.3f, 1, 1),
                                                     S_vector(3, 1, 3.05f)})});
            DmVisual("dgv", m, mesh, S_vector(0, 0, 0), S_vector(2, 1, 2));
            dec.PrepareAnimated(m);
            WM(r.dmPrepare, dec.vertices.size() == 3 && dec.restPositions[0].x == 3.0f && dec.restPositions[1].x == 1.05f &&
                                dec.restPositions[2].x == 1.0f);
            dec.mesh = nullptr;
        }
        fresh(4, 1.0f);
        {
            // baseZ + z == 0 with z 1 (baseZ -1): the other seed.
            g.m_originX = 10;
            g.m_originZ = 1;
            g.m_shiftX = 0;
            g.m_shiftZ = 0;
            const float hh = std::sin(static_cast<float>(-1 - 2 + 1)) * static_cast<float>(8 - 1 + 2);
            const int seed = static_cast<int>(std::cos(hh + 1.0f) * 10000.0f + 123456.0f);
            std::srand(static_cast<unsigned>(seed));
            const int want = std::rand();
            g.Refill(1, 2);
            WM(r.dmRefillSeed, std::rand() == want);
            // The cell's bend first in the list: found; one not in the list: left.
            auto *b0 = new S_DecoratorBend(), *b1 = new S_DecoratorBend(), *stray = new S_DecoratorBend();
            S_DecoratorCell &cell = g.m_cells[1][2];
            b0->cell = &cell;
            cell.bend = b0;
            g.m_bends = {b0, b1};
            g.Refill(1, 2);
            WM(r.dmRefill, g.m_bends == std::vector<S_DecoratorBend *>({b1}) && !cell.bend);
            cell.bend = stray;
            g.Refill(1, 2);
            WM(r.dmRefill, g.m_bends == std::vector<S_DecoratorBend *>({b1}) && cell.bend == stray);
            cell.bend = nullptr;
            delete stray;
            delete b1;
            g.m_bends.clear();
        }
        g_dmNoHit = false;
        fresh(4, 1.0f);
        g.Free();
    }

    // (The test's decorators go; the pools' models are the test's frames.)
    for (S_DecoratorPool *p : d.m_pools)
        p->models.clear();
    d.m_grid.Free();
    d.Clear();
    g_decorators = oldGlobal;
    g_driverVtable[0x54 / 4] = oldCreate;
    g_rayTestHook = oldHook;
    WmField<I3D_frame *>(scene, 0x1F8) = oldPrimary;
    details.switches[DETAIL_DECORATORS] = oldSwitch;
    g_decoratorsEnabled = false;
    g_firstPerson = false;
    g_gfRandoms.clear();
    std::printf("decorators2: dmSlope %d dmLight %d dmTake %d dmPool %d dmPrepare %d dmCell %d dmRefill %d dmRefillSeed %d "
                "dmFollow %d dmFollowMove %d dmShift %d dmBend %d dmBendSpring %d dmBendUpdate %d dmBends %d dmHuman %d "
                "dmCar %d dmUpdate %d dmSway %d\n",
                r.dmSlope, r.dmLight, r.dmTake, r.dmPool, r.dmPrepare, r.dmCell, r.dmRefill, r.dmRefillSeed, r.dmFollow,
                r.dmFollowMove, r.dmShift, r.dmBend, r.dmBendSpring, r.dmBendUpdate, r.dmBends, r.dmHuman, r.dmCar, r.dmUpdate,
                r.dmSway);
}
