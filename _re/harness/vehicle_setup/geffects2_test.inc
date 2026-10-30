// Part 8c-ii's GEffects calls (WORLD_TEST; game/geffects.cpp), after geffects_test.inc (its fakes):
// shadows (the shadow cache's Get faked), face textures, uniforms (the model cache's OpenInto filling
// the models), shattered glass (the driver's new meshes and the physics module faked). Meshes,
// levels, face groups, materials and textures are made-up objects with the engine's field offsets.
// Prints the "geffects2:" line.

namespace {

#include <array>

// --- meshes, levels, face groups, materials, textures -------------------------------------------

struct S_GfTexture {
    void *vtable;
    uint8_t unknown04[0x10];
    int refs;                                           // +0x14
    const char *name;                                   // +0x18
};
struct S_GfMaterial {
    void **vtable;
    int refs;                                           // +0x04
    uint32_t flags;                                     // +0x08
    S_GfTexture *texture;                               // +0x0C
    std::string id;
};
struct S_GfGroup {
    void **vtable;
    S_GfMaterial *material;                             // +0x04
    int numFaces;                                       // +0x08
    std::vector<uint16_t> faces;
    std::string id;
};
struct S_GfVertex {
    S_vector pos, normal;
    float u, v;
};
struct S_GfLevel {
    void **vtable;
    uint8_t unknown04[0x50];
    int numVertices;                                    // +0x54
    int numGroups;                                      // +0x58
    S_GfGroup *firstGroup;                              // +0x5C the list's head (LS3DF: faceGroups)
    std::vector<S_GfGroup *> groups;
    std::vector<S_GfVertex> vertices;
};
struct S_GfMesh {
    void **vtable;
    uint8_t unknown04[0x40];
    uint32_t numLevels;                                 // +0x44
    std::vector<S_GfLevel *> levels;
    std::string id;
};
void *g_gfMaterialVt[SLOTS], *g_gfGroupVt[SLOTS], *g_gfLevelVt[SLOTS], *g_gfMeshVt[SLOTS], *g_gfImageVt[SLOTS];
void *g_gfShadowVt[SLOTS], *g_gfTargetVt[SLOTS], *g_gfShardVt[SLOTS];
std::vector<S_GfMesh *> g_gfMeshes;                     // the driver's new ones
std::vector<S_GfLevel *> g_gfLevels;
std::vector<S_GfGroup *> g_gfGroups;
int g_gfImages;

uint32_t __stdcall F_GfMatRelease(S_GfMaterial *self)
{
    GfLog("matrelease %s", self->id.c_str());
    return static_cast<uint32_t>(--self->refs);
}
std::map<const void *, std::string> g_gfTextureNames;  // (logged by name; others "image")
void __stdcall F_GfMatTexture(S_GfMaterial *self, void *texture)
{
    auto it = g_gfTextureNames.find(texture);
    GfLog("settex %s %s", self->id.c_str(), !texture ? "0" : it != g_gfTextureNames.end() ? it->second.c_str() : "image");
}
void __stdcall F_GfMatResolve(S_GfMaterial *self) { GfLog("resolve %s", self->id.c_str()); }
void __stdcall F_GfGroupMaterial(S_GfGroup *self, S_GfMaterial *material)
{
    GfLog("setmat %s %s", self->id.c_str(), material ? material->id.c_str() : "0");
    self->material = material;
}
uint16_t *__stdcall F_GfAllocFaces(S_GfGroup *self, int count)
{
    GfLog("allocfaces %s %d", self->id.c_str(), count);
    self->faces.assign(static_cast<size_t>(count) * 3, 0xFFFF);
    self->numFaces = count;
    return self->faces.data();
}
uint16_t *__stdcall F_GfLockFaces(S_GfGroup *self, uint32_t flags)
{
    GfLog("lockfaces %s %u", self->id.c_str(), flags);
    return self->faces.data();
}
void __stdcall F_GfDupMaterial(S_GfGroup *self)
{
    GfLog("dupmat %s", self->id.c_str());
    auto *copy = new S_GfMaterial(*self->material);
    copy->refs = 1;
    copy->id = self->material->id + "copy";
    self->material = copy;
}
uint32_t __stdcall F_GfLevelRelease(S_GfLevel *) { GfLog("release level"); return 0; }
void *__stdcall F_GfLockVertices(S_GfLevel *self, int flags)
{
    GfLog("lockverts %d", flags);
    return self->vertices.data();
}
void __stdcall F_GfUnlockVertices(S_GfLevel *) { GfLog("unlockverts"); }
S_GfGroup *__stdcall F_GfGetGroup(S_GfLevel *self, int index) { return self->groups.at(static_cast<size_t>(index)); }
void __stdcall F_GfAddGroup(S_GfLevel *self, S_GfGroup *group)
{
    GfLog("addgroup %s", group->id.c_str());
    self->groups.insert(self->groups.begin(), group);
}
void *__stdcall F_GfAllocVertices(S_GfLevel *self, int count)
{
    GfLog("allocverts %d", count);
    self->vertices.assign(static_cast<size_t>(count), S_GfVertex{});
    return self->vertices.data();
}
void __stdcall F_GfBounds(S_GfLevel *) { GfLog("bounds"); }
uint32_t __stdcall F_GfMeshRelease(S_GfMesh *self) { GfLog("release %s", self->id.c_str()); return 0; }
S_GfLevel *__stdcall F_GfGetLevel(S_GfMesh *self, int index) { return self->levels.at(static_cast<size_t>(index)); }
void __stdcall F_GfSetLevel(S_GfMesh *self, S_GfLevel *level, int index)
{
    GfLog("setlevel %s %d", self->id.c_str(), index);
    if (self->levels.size() <= static_cast<size_t>(index))
        self->levels.resize(static_cast<size_t>(index) + 1);
    self->levels[static_cast<size_t>(index)] = level;
}
uint32_t __stdcall F_GfImageRelease(void *) { GfLog("imgrelease"); return 0; }

S_GfMaterial *GfMaterial(const std::string &id, const char *texture, bool pending = false)
{
    auto *m = new S_GfMaterial();
    m->vtable = g_gfMaterialVt;
    m->refs = 1;
    m->flags = pending ? I3DMATERIAL_TEXTURE_PENDING_BIT : 0;
    if (texture) {
        m->texture = new S_GfTexture();
        m->texture->name = texture;
    }
    m->id = id;
    return m;
}
S_GfGroup *GfGroup(const std::string &id, S_GfMaterial *material)
{
    auto *g = new S_GfGroup();
    g->vtable = g_gfGroupVt;
    g->material = material;
    g->id = id;
    return g;
}
S_GfLevel *GfLevel(std::initializer_list<S_GfGroup *> groups)
{
    auto *l = new S_GfLevel();
    l->vtable = g_gfLevelVt;
    l->groups = groups;
    l->numGroups = static_cast<int>(l->groups.size());
    l->firstGroup = l->groups.empty() ? nullptr : l->groups[0];
    return l;
}
S_GfMesh *GfMesh(const std::string &id, std::initializer_list<S_GfLevel *> levels)
{
    auto *m = new S_GfMesh();
    m->vtable = g_gfMeshVt;
    m->levels = levels;
    m->numLevels = static_cast<uint32_t>(m->levels.size());
    m->id = id;
    return m;
}
// A skinned visual with flag 0x800 and a mesh.
I3D_frame *GfSkinned(const std::string &name, S_GfMesh *mesh, I3D_frame *parent, uint32_t type = I3DVISUAL_SINGLEMESH,
                     bool objectParams = true)
{
    I3D_frame *f = GfVisual(name, type, parent);
    WmField<uint32_t>(f, 0x1D0) = objectParams ? VISFLAGS_OBJECT_PARAMS : 0;
    WmField<S_GfMesh *>(f, 0x1E0) = mesh;
    return f;
}

// --- frames: shadows, targets, models' stages, shards ------------------------------------------

struct S_GfStage {
    S_GfAnim *anim = nullptr;
    float weight = 0.0f;
    int flags = 0;
    int time = 0;
    bool active = false;
};
std::map<const I3D_frame *, std::array<S_GfStage, 8>> g_gfStages;
std::map<const S_GfAnim *, std::string> g_gfAnimIds;

void __stdcall F_GfSetName(I3D_frame *self, const char *name) { GfLog("name %s %s", GfName(self), name); }
void __stdcall F_GfDuplicate(I3D_frame *self, I3D_frame *source) { GfLog("duplicate %s %s", GfName(self), GfName(source)); }
HRESULT __stdcall F_GfShadowSize(I3D_frame *self, int w, int h)
{
    GfLog("size %s %d %d", GfName(self), w, h);
    return 0;
}
void __stdcall F_GfShadowDepth(I3D_frame *self, float depth) { GfLog("depth %s %.3f", GfName(self), depth); }
HRESULT __stdcall F_GfShadowMaxHeight(I3D_frame *self, float height)
{
    GfLog("maxheight %s %.3f", GfName(self), height);
    return 0;
}
void __stdcall F_GfShadowOpacity(I3D_frame *self, float opacity) { GfLog("opacity %s %.5f", GfName(self), opacity); }
void __stdcall F_GfShadowColor(I3D_frame *self, const S_vector4 &c)
{
    GfLog("color %s %.2f %.2f %.2f %.2f", GfName(self), c.x, c.y, c.z, c.w);
}
void __stdcall F_GfAddModelFrame(I3D_frame *self, I3D_frame *frame) { GfLog("addframe %s %s", GfName(self), GfName(frame)); }
void __stdcall F_GfRemoveModelFrame(I3D_frame *self, I3D_frame *frame)
{
    GfLog("removeframe %s %s", GfName(self), GfName(frame));
}
void __stdcall F_GfTargetAdd(I3D_frame *self, I3D_frame *frame) { GfLog("target %s %s", GfName(self), GfName(frame)); }
void __stdcall F_GfSetAnim(I3D_frame *self, S_GfAnim *anim, int stage, float weight, int flags)
{
    GfLog("setanim %s %s %d %.2f %d", GfName(self), g_gfAnimIds[anim].c_str(), stage, weight, flags);
    S_GfStage &s = g_gfStages[self][static_cast<size_t>(stage)];
    s.anim = anim;
    s.weight = weight;
    s.active = true;
}
void __stdcall F_GfStopAnim(I3D_frame *self, int stage)
{
    GfLog("stopanim %s %d", GfName(self), stage);
    g_gfStages[self][static_cast<size_t>(stage)].active = false;
}
void __stdcall F_GfSetWeight(I3D_frame *self, int stage, float weight)
{
    GfLog("weight %s %d %.2f", GfName(self), stage, weight);
    g_gfStages[self][static_cast<size_t>(stage)].weight = weight;
}
float __stdcall F_GfGetWeight(I3D_frame *self, int stage) { return g_gfStages[self][static_cast<size_t>(stage)].weight; }
void __stdcall F_GfSetTime(I3D_frame *self, int stage, int time)
{
    GfLog("animtime %s %d %d", GfName(self), stage, time);
    g_gfStages[self][static_cast<size_t>(stage)].time = time;
}
void __stdcall F_GfSetFlags(I3D_frame *self, int stage, int flags)
{
    GfLog("animflags %s %d %d", GfName(self), stage, flags);
    g_gfStages[self][static_cast<size_t>(stage)].flags = flags;
}
int __stdcall F_GfGetFlags(I3D_frame *self, int stage) { return g_gfStages[self][static_cast<size_t>(stage)].flags; }
// (The models with stages answer from them, the others as the 8c-i test's.)
void *__stdcall F_Gf2GetAnimation(I3D_frame *self, int stage)
{
    if (stage < 0 || stage > 7) {
        GfLog("badstage %d", stage);
        return nullptr;
    }
    auto it = g_gfStages.find(self);
    return it != g_gfStages.end() ? it->second[static_cast<size_t>(stage)].anim : F_GfGetAnimation(self, stage);
}
int __stdcall F_Gf2AnimTime(I3D_frame *self, int stage)
{
    auto it = g_gfStages.find(self);
    return it != g_gfStages.end() ? it->second[static_cast<size_t>(stage)].time : F_GfAnimTime(self, stage);
}
I3D_frame *__stdcall F_Gf2CreateFrame(void *driver, int type)
{
    if (type != FRAME_SHADOW)
        return F_GfCreateFrame(driver, type);
    GfLog("create %d", type);
    g_gfCreated.push_back(F_GfNewFrame(FRAME_SHADOW, "anim", nullptr, g_gfShadowVt));
    return g_gfCreated.back();
}
bool __stdcall F_GfIsActive(I3D_frame *self, int stage) { return g_gfStages[self][static_cast<size_t>(stage)].active; }
void __stdcall F_GfSetPos(I3D_frame *self, const S_vector &pos)
{
    GfLog("setpos %s %.3f %.3f %.3f", GfName(self), pos.x, pos.y, pos.z);
    self->m_localMatrix(3) = pos;
}
void __stdcall F_GfSetMesh(I3D_frame *self, S_GfMesh *mesh, int unknown)
{
    GfLog("setmesh %s %s %d", GfName(self), mesh ? mesh->id.c_str() : "0", unknown);
    WmField<S_GfMesh *>(self, 0x1E0) = mesh;
}
void __stdcall F_GfRefresh(I3D_frame *self) { GfLog("refresh %s", GfName(self)); }
std::vector<I3D_frame *> g_gfShards;
I3D_frame *__stdcall F_GfCreateVisual(void *, uint32_t type)
{
    GfLog("visual %x", type);
    g_gfShards.push_back(F_GfNewFrame(FRAME_VISUAL, "shard" + std::to_string(g_gfShards.size()), nullptr, g_gfShardVt));
    return g_gfShards.back();
}
S_GfMesh *__stdcall F_GfCreateMesh(void *)
{
    GfLog("mesh");
    g_gfMeshes.push_back(GfMesh("mesh", {}));
    return g_gfMeshes.back();
}
S_GfLevel *__stdcall F_GfCreateLevel(void *)
{
    GfLog("level");
    g_gfLevels.push_back(GfLevel({}));
    return g_gfLevels.back();
}
S_GfGroup *__stdcall F_GfCreateGroup(void *)
{
    GfLog("group");
    g_gfGroups.push_back(GfGroup("group", nullptr));
    return g_gfGroups.back();
}
HRESULT __stdcall F_GfLoadTexture(void *, const char *name, const char *alpha, uint32_t mode, void **out)
{
    GfLog("loadtex %s '%s' %u", name, alpha, mode);
    if (std::strstr(name, "missing"))
        return 1;
    static void *image = g_gfImageVt;
    *out = &image;
    ++g_gfImages;
    return 0;
}

// The shadow cache's files and answer; the uniforms (by id: a model's name, uniform or not).
std::map<std::string, I3D_frame *> g_gfShadowFiles;
HRESULT g_gfShadowResult;
struct S_GfItemDef {
    void **vtable;
    int behaviour;
};
std::map<int, C_ItemDef *> g_gfItemDefs;
void *g_gfItemDefVt[SLOTS];
int __thiscall F_GfBehaviour(S_GfItemDef *self) { return self->behaviour; }
C_ItemDef *GfItemDef(int behaviour, const char *model)
{
    // (Big enough for the real class's fields; every slot answers its behaviour.)
    auto *mem = static_cast<uint8_t *>(::operator new(0x200));
    std::memset(mem, 0, 0x200);
    auto *def = reinterpret_cast<C_ItemDef *>(mem);
    reinterpret_cast<S_GfItemDef *>(mem)->vtable = g_gfItemDefVt;
    def->m_modelName = const_cast<char *>(model);
    *reinterpret_cast<int *>(mem + 0x1F0) = behaviour;
    return def;
}
int __thiscall F_GfDefBehaviour(uint8_t *self) { return *reinterpret_cast<int *>(self + 0x1F0); }

void GfInitVtables2()
{
    for (int i = 0; i < SLOTS; ++i) {
        g_gfMaterialVt[i] = g_gfGroupVt[i] = g_gfLevelVt[i] = g_gfMeshVt[i] = g_gfImageVt[i] = g_frameVtable[0x10 / 4];
        g_gfItemDefVt[i] = reinterpret_cast<void *>(&F_GfDefBehaviour);
    }
    g_gfMaterialVt[0x00 / 4] = reinterpret_cast<void *>(&F_GfMatRelease);
    g_gfMaterialVt[0x1C / 4] = reinterpret_cast<void *>(&F_GfMatTexture);
    g_gfMaterialVt[0x44 / 4] = reinterpret_cast<void *>(&F_GfMatResolve);
    g_gfGroupVt[0x00 / 4] = reinterpret_cast<void *>(&F_GfGroupMaterial);
    g_gfGroupVt[0x08 / 4] = reinterpret_cast<void *>(&F_GfAllocFaces);
    g_gfGroupVt[0x0C / 4] = reinterpret_cast<void *>(&F_GfLockFaces);
    g_gfGroupVt[0x18 / 4] = reinterpret_cast<void *>(&F_GfDupMaterial);
    g_gfLevelVt[0x00 / 4] = reinterpret_cast<void *>(&F_GfLevelRelease);
    g_gfLevelVt[0x04 / 4] = reinterpret_cast<void *>(&F_GfLockVertices);
    g_gfLevelVt[0x08 / 4] = reinterpret_cast<void *>(&F_GfUnlockVertices);
    g_gfLevelVt[0x14 / 4] = reinterpret_cast<void *>(&F_GfGetGroup);
    g_gfLevelVt[0x1C / 4] = reinterpret_cast<void *>(&F_GfAddGroup);
    g_gfLevelVt[0x28 / 4] = reinterpret_cast<void *>(&F_GfAllocVertices);
    g_gfLevelVt[0x2C / 4] = reinterpret_cast<void *>(&F_GfBounds);
    g_gfMeshVt[0x00 / 4] = reinterpret_cast<void *>(&F_GfMeshRelease);
    g_gfMeshVt[0x04 / 4] = reinterpret_cast<void *>(&F_GfGetLevel);
    g_gfMeshVt[0x08 / 4] = reinterpret_cast<void *>(&F_GfSetLevel);
    g_gfImageVt[0x00 / 4] = reinterpret_cast<void *>(&F_GfImageRelease);
    for (void **vt : {g_gfFrameVt, g_gfLightVt, g_gfCameraVt, g_gfModelVt, g_gfSectorVt})
        vt[0x34 / 4] = reinterpret_cast<void *>(&F_GfSetName);
    for (int i = 0; i < SLOTS; ++i)
        g_gfShadowVt[i] = g_gfTargetVt[i] = g_gfShardVt[i] = g_gfFrameVt[i];
    g_gfShadowVt[0x4C / 4] = reinterpret_cast<void *>(&F_GfDuplicate);
    g_gfShadowVt[0x7C / 4] = reinterpret_cast<void *>(&F_GfShadowSize);
    g_gfShadowVt[0x80 / 4] = reinterpret_cast<void *>(&F_GfShadowDepth);
    g_gfShadowVt[0x84 / 4] = reinterpret_cast<void *>(&F_GfShadowMaxHeight);
    g_gfShadowVt[0x8C / 4] = reinterpret_cast<void *>(&F_GfShadowOpacity);
    g_gfShadowVt[0x90 / 4] = reinterpret_cast<void *>(&F_GfShadowColor);
    g_gfTargetVt[0x74 / 4] = reinterpret_cast<void *>(&F_GfTargetAdd);
    g_gfModelVt[0x7C / 4] = reinterpret_cast<void *>(&F_GfAddModelFrame);
    g_gfModelVt[0x80 / 4] = reinterpret_cast<void *>(&F_GfRemoveModelFrame);
    g_gfModelVt[0x84 / 4] = reinterpret_cast<void *>(&F_GfSetAnim);
    g_gfModelVt[0x8C / 4] = reinterpret_cast<void *>(&F_GfStopAnim);
    g_gfModelVt[0x94 / 4] = reinterpret_cast<void *>(&F_GfSetWeight);
    g_gfModelVt[0x98 / 4] = reinterpret_cast<void *>(&F_GfGetWeight);
    g_gfModelVt[0x9C / 4] = reinterpret_cast<void *>(&F_GfSetTime);
    g_gfModelVt[0xA4 / 4] = reinterpret_cast<void *>(&F_GfSetFlags);
    g_gfModelVt[0xA8 / 4] = reinterpret_cast<void *>(&F_GfGetFlags);
    g_gfModelVt[0xAC / 4] = reinterpret_cast<void *>(&F_GfIsActive);
    g_gfModelVt[0x88 / 4] = reinterpret_cast<void *>(&F_Gf2GetAnimation);
    g_gfModelVt[0xA0 / 4] = reinterpret_cast<void *>(&F_Gf2AnimTime);
    g_gfShardVt[0x08 / 4] = reinterpret_cast<void *>(&F_GfSetPos);
    g_gfShardVt[0x9C / 4] = reinterpret_cast<void *>(&F_GfSetMesh);
    g_gfShardVt[0xB0 / 4] = reinterpret_cast<void *>(&F_GfRefresh);
}

// The lines of `expected` in this order among the log's (others between them allowed).
bool GfHas(std::initializer_list<std::string> expected)
{
    auto it = g_gfLog.begin();
    for (const std::string &e : expected) {
        it = std::find(it, g_gfLog.end(), e);
        if (it == g_gfLog.end()) {
            if (g_wmTrace) {
                std::printf("    missing '%s' in:\n", e.c_str());
                for (const std::string &l : g_gfLog)
                    std::printf("    gflog %s\n", l.c_str());
            }
            return false;
        }
        ++it;
    }
    return true;
}

struct S_Gf2Results {
    bool gfShadow = true, gfShadowName = true, gfShadowFind = true, gfFace = true, gfFaceFind = true, gfUniform = true;
    bool gfUniformFile = true, gfGlass = true, gfShardSplit = true, gfShardPhysics = true;
};

}  // namespace

// The fakes of what DressUniform calls besides the engine.
alignas(16) unsigned char g_gfShadowCacheStorage[0x80] __asm__("_g_shadowCache");
template <>
HRESULT C_ResourceCache<I3D_shadow>::Get(const char *fileName, I3D_shadow **out, T_CacheErrorProc, void *)
{
    GfLog("shadowcache %s", fileName);
    *out = nullptr;
    auto it = g_gfShadowFiles.find(fileName);
    if (it != g_gfShadowFiles.end()) {
        ++it->second->m_refCount;
        *out = static_cast<I3D_shadow *>(it->second);
    }
    return g_gfShadowResult;
}
C_ItemDef *GetItemDef(int typeId)
{
    auto it = g_gfItemDefs.find(typeId);
    return it == g_gfItemDefs.end() ? nullptr : it->second;
}
void C_Team::CollectVolumes(int index) { GfLog("volumes %d", index); }
void C_HumanMovement::OnModelChanged() { GfLog("modelchanged"); }
void C_HumanMovement::UpdateModelNames() { GfLog("modelnames"); }
bool C_CameraModule::GetCameraDir(int index, S_vector4 *dir)
{
    GfLog("camdir %d", index);
    *dir = {1.0f, 2.0f, 3.0f, 4.0f};
    return true;
}
void C_CameraModule::Unknown40(int arg) { GfLog("cam40 %d", arg); }
void C_CameraModule::SetViewDir(const S_vector4 &dir) { GfLog("viewdir %.1f %.1f %.1f", dir.x, dir.y, dir.z); }

void GEffects2Test()
{
    S_Gf2Results r;
    C_GEffectsModule &m = g_geffectsModule;
    static bool once;
    if (!once) {
        once = true;
        GfInitVtables2();
    }
    void *driverSlots[SLOTS], *igraphSlots[SLOTS];
    std::memcpy(driverSlots, g_driverVtable, sizeof driverSlots);
    std::memcpy(igraphSlots, g_igraphVtable, sizeof igraphSlots);
    g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&F_GfCreateVisual);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_Gf2CreateFrame);
    g_driverVtable[0x64 / 4] = reinterpret_cast<void *>(&F_GfCreateGroup);
    g_driverVtable[0x68 / 4] = reinterpret_cast<void *>(&F_GfCreateLevel);
    g_driverVtable[0x6C / 4] = reinterpret_cast<void *>(&F_GfCreateMesh);
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_GfLoadTexture);
    uint8_t *scene = reinterpret_cast<uint8_t *>(GetGame()->GetScene());
    I3D_frame *const oldPrimary = WmField<I3D_frame *>(scene, 0x1F8);
    I3D_frame *primary = GfFrame(FRAME_SECTOR, "primary");
    WmField<I3D_frame *>(scene, 0x1F8) = primary;
    g_gameRole = GAME_ROLE_LOCAL;
    GfClear(m);

    // --- shadows ---
    {
        I3D_frame *model = GfFrame(FRAME_MODEL, "tank");
        static const char *file = "models\\Tank.i3d";
        WmField<const char *>(model, 0x134) = file;
        I3D_frame *body = GfFrame(FRAME_DUMMY, "a", model);
        body->m_ownerModel = model;
        I3D_frame *cached = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "cached", nullptr, g_gfShadowVt);
        g_gfShadowFiles.clear();
        g_gfShadowFiles["sTank"] = cached;
        g_gfShadowResult = I3D_OK;
        m.m_shadowParams.color = {0.25f, 0.5f, 0.75f, 1.0f};
        m.m_shadowParams.width = 32;
        m.m_shadowParams.height = 16;
        m.m_shadowParams.range = 50.0f;
        m.m_shadowParams.depth = 4.0f;
        m.m_shadowParams.maxHeight = 2.0f;
        // (The driver's new shadows: of type 14 here, as GetShadow's search mask has it.)
        g_gfCreated.clear();
        I3D_frame *shadow = m.CreateShadow(model);
        WM(r.gfShadow, shadow && shadow == g_gfCreated.back() && shadow->m_parent == body && shadow->m_refCount == 0 &&
                           cached->m_refCount == 1 && WmField<float>(shadow, 0x1FC) == 50.0f);
        WM(r.gfShadow, GfLogIs({"shadowcache sTank", "create 15", "duplicate anim cached", "release cached", "name anim ._shad.tank",
                                "find tank a ffff", "link anim a 0", "addframe tank anim", "opacity anim 0.03015",
                                "color anim 0.25 0.50 0.75 1.00", "size anim 32 16", "depth anim 4.000",
                                "maxheight anim 2.000", "update anim", "release anim"}));
        // Not the model's own "a": under the model.
        body->m_ownerModel = nullptr;
        g_gfLog.clear();
        shadow = m.CreateShadow(model);
        WM(r.gfShadow, shadow && shadow->m_parent == model && GfHas({"link anim tank 0"}));
        GfFrame(FRAME_DUMMY, "x", model);                   // (not "a")
        body->m_name = "b";
        g_stubs[body]->name = "b";
        g_gfLog.clear();
        shadow = m.CreateShadow(model);
        WM(r.gfShadow, shadow && shadow->m_parent == model);
        // A shadow there before: taken off the model first.
        I3D_frame *old = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "old", body, g_gfShadowVt);
        old->m_ownerModel = model;
        g_gfLog.clear();
        m.CreateShadow(model);
        WM(r.gfShadow, g_gfLog.size() > 3 && g_gfLog[0] == "link old 0 0" && g_gfLog[1] == "removeframe tank old" &&
                           g_gfLog[2] == "shadowcache sTank");
        old->m_ownerModel = nullptr;
        // No file, a code above 0 (the reference kept), the server, no model.
        g_gfShadowResult = I3DERR_NOFILE;
        g_gfLog.clear();
        WM(r.gfShadow, !m.CreateShadow(model) && GfLogIs({"shadowcache sTank"}));
        g_gfShadowResult = 1;
        const int refs = cached->m_refCount;
        WM(r.gfShadow, !m.CreateShadow(model) && cached->m_refCount == refs + 1);
        g_gfShadowResult = I3D_OK;
        g_gfShadowFiles.clear();
        WM(r.gfShadow, !m.CreateShadow(model));
        g_gfShadowFiles["sTank"] = cached;
        g_gameRole = GAME_ROLE_SERVER;
        g_gfLog.clear();
        WM(r.gfShadow, !m.CreateShadow(model) && g_gfLog.empty());
        g_gameRole = GAME_ROLE_CLIENT;
        WM(r.gfShadow, m.CreateShadow(model) && !m.CreateShadow(nullptr));
        g_gameRole = GAME_ROLE_LOCAL;

        // The file's name: folders and what follows the first '.' dropped; a human's "shdpuppet".
        auto nameOf = [&](const char *path) {
            WmField<const char *>(model, 0x134) = path;
            g_gfLog.clear();
            m.CreateShadow(model);
            return g_gfLog.empty() ? std::string() : g_gfLog[0];
        };
        WM(r.gfShadowName, nameOf("Tank.i3d") == "shadowcache sTank");
        WM(r.gfShadowName, nameOf("a.b\\c\\Tank.x.i3d") == "shadowcache sa");
        WM(r.gfShadowName, nameOf("dir\\sub\\Jeep.i3d") == "shadowcache sJeep");
        WM(r.gfShadowName, nameOf("\\Jeep") == "shadowcache sJeep");
        WM(r.gfShadowName, nameOf(nullptr) == "shadowcache s");
        WM(r.gfShadowName, nameOf("") == "shadowcache s");
        S_FrameData data = {};
        model->m_data = &data;
        WM(r.gfShadowName, nameOf("Tank.i3d") == "shadowcache sTank");
        auto *human = new C_Human();
        data.actor = human;
        WM(r.gfShadowName, nameOf("Tank.i3d") == "shadowcache shdpuppet");
        C_Human car;
        car.m_type = ACTOR_CAR;
        data.actor = &car;
        WM(r.gfShadowName, nameOf("Tank.i3d") == "shadowcache sTank");
        model->m_data = nullptr;
        WmField<const char *>(model, 0x134) = file;

        // GetShadow: the first frame of the search's type owned by the model.
        I3D_frame *m2 = GfFrame(FRAME_MODEL, "m2");
        WM(r.gfShadowFind, !m.GetShadow(nullptr) && !m.GetShadow(m2));
        I3D_frame *part = GfFrame(FRAME_DUMMY, "part", m2);
        I3D_frame *foreign = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "foreign", part, g_gfShadowVt);
        WM(r.gfShadowFind, !m.GetShadow(m2));
        I3D_frame *own = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "own", part, g_gfShadowVt);
        own->m_ownerModel = m2;
        I3D_frame *own2 = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "own2", m2, g_gfShadowVt);
        own2->m_ownerModel = m2;
        GfFrame(FRAME_DUMMY, "notshadow", m2)->m_ownerModel = m2;
        const int ownRefs = own->m_refCount;
        WM(r.gfShadowFind, m.GetShadow(m2) == own && own->m_refCount == ownRefs && foreign);
        I3D_frame *m3 = GfFrame(FRAME_MODEL, "m3");
        I3D_frame *outer = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "outer", m3, g_gfShadowVt);
        I3D_frame *inner = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(14), "inner", outer, g_gfShadowVt);
        inner->m_ownerModel = m3;
        WM(r.gfShadowFind, m.GetShadow(m3) == inner);
        // Set / get the parameters.
        S_ShadowParams p;
        p.depth = 3.0f;
        p.unknown10[2] = 7;
        m.SetShadowParams(&p);
        WM(r.gfShadowFind, m.GetShadowParams() == &m.m_shadowParams && m.m_shadowParams.depth == 3.0f &&
                               m.m_shadowParams.unknown10[2] == 7 && m.m_shadowParams.range == 100.0f);
        m.m_shadowParams = S_ShadowParams();
    }

    // --- face textures ---
    S_GfMaterial *m0 = GfMaterial("m0", "Body.bmp"), *m1 = GfMaterial("m1", "Head_E_F01.BMP");
    S_GfMaterial *m2 = GfMaterial("m2", "heade_f01.bmp", true);
    {
        GfClear(m);
        I3D_frame *fm = GfFrame(FRAME_DUMMY, "fm");
        S_GfGroup *g0 = GfGroup("g0", m0), *g1 = GfGroup("g1", m1), *g2 = GfGroup("g2", m2), *g3 = GfGroup("g3", nullptr);
        S_GfGroup *g4 = GfGroup("g4", GfMaterial("m4", nullptr));
        GfSkinned("face", GfMesh("facemesh", {GfLevel({g0, g1, g3}), GfLevel({g2, g4})}), fm);
        GfSkinned("plain", GfMesh("plainmesh", {GfLevel({GfGroup("p0", GfMaterial("pm", "xe_f0.bmp"))})}), fm,
                  I3DVISUAL_SINGLEMESH, false);
        GfSkinned("obj", GfMesh("objmesh", {GfLevel({GfGroup("o0", GfMaterial("om", "ye_f0.bmp"))})}), fm, I3DVISUAL_OBJECT);
        WM(r.gfFace, m.SwitchFaceTexture(fm, "Some\\NewE_F02.tga"));
        WM(r.gfFace, GfLogIs({"resolve m2", "resolve m2", "resolve m2", "loadtex some\\newe_f02.bmp '' 1024", "dupmat g1",
                              "settex m1copy image", "setmat g2 m1copy", "matrelease m1copy", "imgrelease"}));
        WM(r.gfFace, g1->material != m1 && g2->material == g1->material && g1->material->refs == 1 && g0->material == m0);
        // Without the mark, the face, the file; no model.
        g_gfLog.clear();
        WM(r.gfFace, !m.SwitchFaceTexture(fm, "plain.bmp") && g_gfLog.empty());
        WM(r.gfFace, !m.SwitchFaceTexture(fm, "missinge_f0") && GfHas({"loadtex missinge_f0.bmp '' 1024"}) &&
                         !GfHas({"dupmat g1"}));
        WM(r.gfFace, !m.SwitchFaceTexture(nullptr, "e_f0"));
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "bare");
        g_gfLog.clear();
        WM(r.gfFace, !m.SwitchFaceTexture(bare, "e_f0.bmp") && g_gfLog.empty());
        // A name without an extension; the last '.' cut.
        g_gfLog.clear();
        m.SwitchFaceTexture(fm, "x.ye_f03");
        WM(r.gfFace, GfHas({"loadtex x.bmp '' 1024"}));
        g_gfLog.clear();
        m.SwitchFaceTexture(fm, "a.b_e_f0.c.tga");
        WM(r.gfFace, GfHas({"loadtex a.b_e_f0.c.bmp '' 1024"}));
        g_gfLog.clear();
        m.SwitchFaceTexture(fm, "noext_e_f0");
        WM(r.gfFace, GfHas({"loadtex noext_e_f0.bmp '' 1024"}));

        // The search: the frame itself first, then its children from the last; skinned morphs too;
        // the found visual's groups only (the list cleared for each).
        auto firstDup = [&](I3D_frame *frame) {
            g_gfLog.clear();
            m.SwitchFaceTexture(frame, "e_f05");
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 7, "dupmat ") == 0)
                    return l.substr(7);
            return std::string();
        };
        I3D_frame *root = GfSkinned("root", GfMesh("rootmesh", {GfLevel({GfGroup("r0", GfMaterial("rm", "e_f0"))})}), nullptr,
                                    I3DVISUAL_SINGLEMORPH);
        GfSkinned("k1", GfMesh("k1mesh", {GfLevel({GfGroup("k10", GfMaterial("k1m", "e_f0"))})}), root);
        GfSkinned("k2", GfMesh("k2mesh", {GfLevel({GfGroup("k20", GfMaterial("k2m", "e_f0")), GfGroup("k21", GfMaterial("k2n", "x"))})}),
                  root);
        WM(r.gfFaceFind, firstDup(root) == "r0" && !GfHas({"setmat k20 rmcopy"}));
        WmField<uint32_t>(root, 0x1D0) = 0;
        WM(r.gfFaceFind, firstDup(root) == "k20" && !GfHas({"setmat k21 k2mcopy"}) && !GfHas({"setmat k10 k2mcopy"}));
        g_gfVisualTypes[root] = I3DVISUAL_SINGLEMESH;
        WmField<uint32_t>(root, 0x1D0) = VISFLAGS_OBJECT_PARAMS;
        WmField<S_GfMesh *>(root, 0x1E0) = nullptr;
        WM(r.gfFaceFind, firstDup(root) == "k20");
        root->m_type = FRAME_DUMMY;
        WmField<S_GfMesh *>(root, 0x1E0) = GfMesh("again", {GfLevel({GfGroup("r1", GfMaterial("rm1", "e_f0"))})});
        WM(r.gfFaceFind, firstDup(root) == "k20");
        // Two levels, the matches of both; a visual of no match: the next one.
        I3D_frame *two = GfFrame(FRAME_DUMMY, "two");
        GfSkinned("t1v", GfMesh("t1mesh", {GfLevel({GfGroup("t10", GfMaterial("t10m", "e_f0"))})}), two);
        GfSkinned("t2v", GfMesh("t2mesh", {GfLevel({GfGroup("t20", GfMaterial("t20m", "none"))})}), two);
        WM(r.gfFaceFind, firstDup(two) == "t10");
    }

    // --- uniforms ---
    {
        GfClear(m);
        I3D_frame *world = GfFrame(FRAME_DUMMY, "world");
        I3D_frame *model = GfFrame(FRAME_MODEL, "soldier", world);
        GfPlace(model, S_vector(10.0f, 1.0f, 20.0f));
        model->m_rot = S_quat(0.70710678f, 0.0f, 0.70710678f, 0.0f);
        model->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
        I3D_frame *firstTarget = F_GfNewFrame(FRAME_TARGET, "t0", model, g_gfTargetVt);
        firstTarget->m_ownerModel = model;
        static I3D_frame *aimed0[1];
        aimed0[0] = GfFrame(FRAME_DUMMY, "y0");
        WmField<I3D_frame **>(firstTarget, 0x120) = aimed0;
        WmField<int>(firstTarget, 0x128) = 1;
        auto own = [](I3D_frame *f, I3D_frame *owner) {
            f->m_ownerModel = owner;
            return f;
        };
        // Its face, a hand holding a gun (not its own), a target with two frames.
        own(GfSkinned("a", GfMesh("am", {GfLevel({GfGroup("ag", GfMaterial("af", "HeadE_F01.bmp"))})}), model), model);
        I3D_frame *hand = own(GfFrame(FRAME_DUMMY, "hand", model), model);
        I3D_frame *gun = GfFrame(FRAME_DUMMY, "gun", hand);
        I3D_frame *nested = F_GfNewFrame(FRAME_TARGET, "tn", hand, g_gfTargetVt);
        nested->m_ownerModel = model;
        static I3D_frame *aimedN[1];
        aimedN[0] = GfFrame(FRAME_DUMMY, "z0");
        WmField<I3D_frame **>(nested, 0x120) = aimedN;
        WmField<int>(nested, 0x128) = 1;
        GfPlace(gun, S_vector(0.5f, 0.25f, 0.125f));
        gun->m_rot = S_quat(0.0f, 1.0f, 0.0f, 0.0f);
        gun->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
        I3D_frame *target = F_GfNewFrame(FRAME_TARGET, "t1", model, g_gfTargetVt);
        own(target, model);
        static I3D_frame *aimed[2];
        aimed[0] = GfFrame(FRAME_DUMMY, "x1");
        aimed[1] = GfFrame(FRAME_DUMMY, "x2");
        WmField<I3D_frame **>(target, 0x120) = aimed;
        WmField<int>(target, 0x128) = 2;
        // Stages 6 (active) and 2 (stopped).
        auto *walk = new S_GfAnim{g_gfAnimVt, 1000};
        auto *aim = new S_GfAnim{g_gfAnimVt, 500};
        g_gfAnimIds[walk] = "walk";
        g_gfAnimIds[aim] = "aim";
        g_gfStages[model] = {};
        g_gfStages[model][6] = {walk, 0.75f, 3, 120, true};
        g_gfStages[model][2] = {aim, 0.5f, 1, 40, false};
        // The models loaded: "a" (a new face), "hand", "t1" (no frames), each the model's own.
        g_cacheFill = [](I3D_model *object) {
            auto *loaded = reinterpret_cast<I3D_frame *>(object);
            loaded->m_firstChild = nullptr;
            const int n = static_cast<int>(g_gfMeshes.size());
            GfSkinned("a", GfMesh("loadedface" + std::to_string(n), {GfLevel({GfGroup("lg", GfMaterial("lm", "neue_f0.bmp"))})}),
                      loaded)->m_ownerModel = loaded;
            GfFrame(FRAME_DUMMY, "hand", loaded)->m_ownerModel = loaded;
            F_GfNewFrame(FRAME_TARGET, "t1", loaded, g_gfTargetVt)->m_ownerModel = loaded;
        };
        auto *human = new C_Human();
        human->m_frame = model;
        human->movement = reinterpret_cast<C_HumanMovement *>(g_gfInterface);
        human->role = HUMAN_ROLE_PLAYER;
        auto *other = new C_Human();
        g_gfMembers = {other, human, other};
        g_gfActive = human;
        g_gfItemDefs.clear();
        g_gfItemDefs[7] = GfItemDef(ITEM_BEHAVIOUR_UNIFORM, "uniforms\\SS.Officer.i3d");
        g_gfItemDefs[8] = GfItemDef(3, "weapons\\gun.i3d");
        g_gfRandoms = {1.0f, 5.0f};
        g_cacheLoads.clear();
        g_gfCreated.clear();
        g_gfLog.clear();
        g_cacheLoadFlags = 9;
        const bool dressed = m.DressUniform(human, 7);
        I3D_frame *copy = g_gfCreated.at(0);
        WM(r.gfUniform, dressed && g_cacheLoads == std::vector<std::string>({"uniforms\\SS.Officer.", "uniforms\\SS.Officer."}) &&
                            g_cacheLoadFlags == 0);
        WM(r.gfUniform, GfHas({"create 9", "rand 6.0", "rand 6.0", "name anim soldier", "update anim", "setanim anim walk 6 0.75 0",
                               "animflags anim 6 3", "animtime anim 6 120", "setanim anim aim 2 0.50 0", "animflags anim 2 1",
                               "animtime anim 2 40", "stopanim anim 2", "weight anim 2 0.00", "find soldier t1 ffff",
                               "target t1 x1", "target t1 x2", "find anim hand ffff", "link gun hand 0", "update gun",
                               "update anim", "setanim soldier walk 6 0.75 0", "animtime soldier 6 120",
                               "animflags soldier 6 3", "setanim soldier aim 2 0.00 0", "animtime soldier 2 40",
                               "animflags soldier 2 1", "stopanim soldier 2", "weight soldier 2 0.00", "find anim t1 ffff",
                               "find soldier hand ffff", "link gun hand 0", "update gun", "link anim 0 0", "release anim",
                               "update soldier", "modelchanged", "modelnames", "update soldier", "volumes 1", "camdir 3",
                               "cam40 0", "viewdir 1.0 2.0 3.0", "loadtex heade_f01.bmp '' 1024", "dupmat lg"}));
        WM(r.gfUniform, GfHas({"target t0 y0"}) && GfHas({"target tn z0"}) &&
                            std::none_of(g_gfLog.begin(), g_gfLog.end(),
                                         [](const std::string &l) { return l.compare(0, 9, "badstage ") == 0; }));
        WM(r.gfUniform, GfHas({"link anim world 0"}) && gun->m_parent && gun->m_parent->m_parent == model &&
                            gun->GetPos().x == 0.5f && gun->GetPos().z == 0.125f && gun->m_rot.v.x == 1.0f &&
                            copy->GetPos().x == 11.0f && copy->GetPos().y == 1.0f && copy->GetPos().z == 17.0f &&
                            std::fabs(copy->m_rot.s - 0.70710678f) < 1e-6f && std::fabs(copy->m_rot.v.y - 0.70710678f) < 1e-6f &&
                            copy->m_rot.v.x == 0.0f &&
                            copy->m_parent == nullptr);
        WM(r.gfUniform, g_gfStages[model][6].active && !g_gfStages[model][2].active && g_gfStages[model][6].time == 120);
        // A frame of no namesake goes under the model itself; another human, not the active one.
        g_gfLog.clear();
        g_gfActive = other;
        I3D_frame *loose = GfFrame(FRAME_DUMMY, "loose", Find(model, "hand", FRAME_MASK_ANY));
        g_cacheFill = [](I3D_model *object) {
            auto *loaded = reinterpret_cast<I3D_frame *>(object);
            loaded->m_firstChild = nullptr;
            GfSkinned("a", GfMesh("refill", {GfLevel({GfGroup("rg", GfMaterial("rgm", "e_f0.bmp"))})}), loaded)->m_ownerModel =
                loaded;
        };
        WM(r.gfUniform, m.DressUniform(human, -1) && loose->m_parent == model && GfHas({"volumes 1"}) && !GfHas({"camdir 3"}));
        WM(r.gfUniformFile, g_cacheLoads.back() == "NA01");
        // Not a uniform, no item: "NA01"; not the player's side: no volumes.
        human->role = HUMAN_ROLE_ENEMY;
        g_gfLog.clear();
        WM(r.gfUniformFile, m.DressUniform(human, 8) && g_cacheLoads.back() == "NA01" && !GfHas({"volumes 1"}));
        WM(r.gfUniformFile, m.DressUniform(human, 9) && g_cacheLoads.back() == "NA01");
        human->role = HUMAN_ROLE_PLAYER;
        g_gfMembers = {human, other};
        g_gfLog.clear();
        WM(r.gfUniformFile, m.DressUniform(human, -1) && GfHas({"volumes 0"}));
        g_gfMembers = {other};
        g_gfLog.clear();
        WM(r.gfUniformFile, m.DressUniform(human, 7) && !GfHas({"volumes 0"}) && g_cacheLoads.back() == "uniforms\\SS.Officer.");
        g_gfItemDefs[10] = GfItemDef(ITEM_BEHAVIOUR_UNIFORM, "plain");
        WM(r.gfUniformFile, m.DressUniform(human, 10) && g_cacheLoads.back() == "plain");
        // No movement; the file failing (nothing changed); no face.
        human->movement = nullptr;
        g_gfLog.clear();
        WM(r.gfUniformFile, m.DressUniform(human, -1) && !GfHas({"modelchanged"}) && !GfHas({"modelnames"}));
        g_cacheFailName = "NA01";
        g_gfLog.clear();
        const size_t loads = g_cacheLoads.size();
        g_cacheLoadFlags = 9;
        WM(r.gfUniformFile, !m.DressUniform(human, -1) && g_cacheLoads.size() == loads + 1 && g_cacheLoadFlags == 0 &&
                                GfLogIs({"create 9", "release anim"}));
        g_cacheFailName.clear();
        I3D_frame *faceless = GfFrame(FRAME_MODEL, "faceless");
        human->m_frame = faceless;
        g_gfLog.clear();
        WM(r.gfUniformFile, !m.DressUniform(human, -1) && GfLogIs({"create 9", "release anim"}) && g_cacheLoads.size() == loads + 1);
        GfSkinned("a", GfMesh("nt", {GfLevel({GfGroup("ntg", GfMaterial("ntm", nullptr))})}), faceless);
        WM(r.gfUniformFile, !m.DressUniform(human, -1));
        g_cacheFill = nullptr;
        g_gfActive = nullptr;
        g_gfMembers.clear();
    }

    // --- shattered glass ---
    {
        GfClear(m);
        I3D_frame *glass = GfFrame(FRAME_VISUAL, "glass");
        GfPlace(glass, S_vector(5.0f, 6.0f, 7.0f));
        S_GfMaterial *gm = GfMaterial("gm", "glass.bmp");
        S_GfGroup *gg = GfGroup("gg", gm);
        S_GfLevel *gl = GfLevel({gg});
        WmField<S_GfMesh *>(glass, 0x1E0) = GfMesh("glassmesh", {gl});
        auto setFaces = [&](std::initializer_list<S_GfVertex> verts, std::initializer_list<uint16_t> faces) {
            gl->vertices = verts;
            gg->faces = faces;
            gg->numFaces = static_cast<int>(gg->faces.size() / 3);
        };
        auto shardVerts = [&](size_t shard) { return g_gfLevels.at(shard)->vertices; };
        // Not an object visual: nothing.
        setFaces({{S_vector(0, 0, 0), S_vector(0, 0, 1), 0, 0}, {S_vector(0.1f, 0, 0), {}, 1, 0}, {S_vector(0, 0.1f, 0), {}, 0, 1}},
                 {0, 1, 2});
        g_gfLog.clear();
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, g_gfLog.empty());
        WmField<uint32_t>(glass, 0x1D0) = VISFLAGS_OBJECT_PARAMS;
        // One small face: one shard, the triangle and its back.
        g_gfLevels.clear();
        g_gfShards.clear();
        g_physicsMade.clear();
        g_physicsPushes.clear();
        std::srand(5);
        float rnd[6];
        for (float &v : rnd)
            v = RandFloat();
        std::srand(5);
        g_gfLog.clear();
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, GfLogIs({"lockverts 0", "lockfaces gg 0", "visual 5f4a424f", "mesh", "level", "allocverts 6", "unlockverts",
                               "group", "setmat group gm", "allocfaces group 2", "addgroup group", "bounds", "setlevel mesh 0",
                               "setmesh shard0 mesh 0", "link shard0 primary 0", "setpos shard0 5.000 6.000 7.000",
                               "refresh shard0", "update shard0", "release level", "release mesh", "release shard0"}));
        {
            const std::vector<S_GfVertex> v = shardVerts(0);
            const float px[6] = {0, 0.1f, 0, 0, 0.1f, 0}, py[6] = {0, 0, 0.1f, 0.1f, 0, 0};
            const float us[6] = {0, 1, 0, 0, 1, 0}, vs[6] = {0, 0, 1, 1, 0, 0};
            bool ok = v.size() == 6;
            for (size_t i = 0; ok && i < 6; ++i)
                ok = v[i].pos.x == px[i] && v[i].pos.y == py[i] && v[i].pos.z == 0.0f && v[i].normal.x == 0.0f &&
                     v[i].normal.y == 0.0f && v[i].normal.z == 1.0f && v[i].u == us[i] && v[i].v == vs[i];
            const auto &faces = g_gfGroups.back()->faces;
            WM(r.gfGlass, ok && faces == std::vector<uint16_t>({0, 1, 2, 3, 4, 5}) && g_gfShards.size() == 1);
        }
        WM(r.gfShardPhysics, g_physicsMade.size() == 1 && g_physicsMade[0].frame == g_gfShards[0] &&
                                 g_physicsMade[0].type == PHYS_FALLING && g_physicsMade[0].startActive &&
                                 g_physicsMade[0].shootThrough && g_physicsMade[0].radius == -1.0f &&
                                 !g_physicsMade[0].collideWithBoxes && !g_physicsMade[0].unknown14 &&
                                 !g_physicsMade[0].callback && !g_physicsMade[0].context && g_physicsMade[0].material == 0 &&
                                 g_physicsMade[0].unknown18 == 0);
        WM(r.gfShardPhysics, g_physicsPushes.size() == 1 && g_physicsPushes[0].factor == 0.1f &&
                                 g_physicsPushes[0].point.z == (rnd[0] - 0.5f) + 7.0f &&
                                 g_physicsPushes[0].point.y == (rnd[1] - 0.5f) + 6.0f &&
                                 g_physicsPushes[0].point.x == (rnd[2] - 0.5f) + 5.0f &&
                                 g_physicsPushes[0].impulse.x == (rnd[3] - 0.5f) * 10.0f &&
                                 g_physicsPushes[0].impulse.y == (rnd[4] - 0.5f) * 10.0f &&
                                 g_physicsPushes[0].impulse.z == (rnd[5] - 0.5f) * 10.0f);
        // The glass turned: the shard too; no physics object: no push.
        glass->m_rot = S_quat(0.70710678f, 0.0f, 0.70710678f, 0.0f);   // (90 degrees about y)
        glass->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
        glass->m_localMatrix.SetRot3(glass->m_rot);
        g_physicsNoObject = true;
        g_physicsPushes.clear();
        m.ShatterGlass(glass, 0.01f);
        g_physicsNoObject = false;
        WM(r.gfShardPhysics, g_physicsPushes.empty() && std::fabs(g_gfShards.back()->m_rot.v.y - 0.70710678f) < 1e-5f &&
                                 std::fabs(g_gfShards.back()->m_rot.s - 0.70710678f) < 1e-5f);
        glass->m_rot.Identity();
        glass->m_localMatrix.Identity();
        GfPlace(glass, S_vector(5.0f, 6.0f, 7.0f));
        // The normal of a face turned the other way; of a degenerate face (along z).
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0, 0.1f, 0), {}, 0, 1}, {S_vector(0.1f, 0, 0), {}, 1, 0}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.z == -1.0f && shardVerts(0)[3].normal.z == -1.0f);
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0, 0, 0.1f), {}, 0, 1}, {S_vector(0, 0, 0.2f), {}, 1, 0}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, g_gfLevels.size() == 1 && shardVerts(0)[0].normal.x == 0.0f && shardVerts(0)[0].normal.y == 1.0f &&
                          shardVerts(0)[0].normal.z == 0.0f);
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0, 1e-7f, 0), {}, 0, 1}, {S_vector(0, 0, 1e-7f), {}, 1, 0}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.x == 1.0f && shardVerts(0)[0].normal.y != 1.0f && shardVerts(0)[0].normal.z != 1.0f);
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0, 0, 1e-7f), {}, 0, 1}, {S_vector(0, 1e-7f, 0), {}, 1, 0}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.x == -1.0f);
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0, 0.1f, 0.1f), {}, 0, 1}, {S_vector(0, 0.2f, 0.25f), {}, 1, 0}},
                 {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.x == 1.0f && shardVerts(0)[0].normal.y == 0.0f && shardVerts(0)[0].normal.z == 0.0f);

        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0.1f, 0, 0), {}, 1, 0}, {S_vector(0, 0.1f, 0.1f), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.x == 0.0f && std::fabs(shardVerts(0)[0].normal.y + 0.70710678f) < 1e-5f &&
                          std::fabs(shardVerts(0)[0].normal.z - 0.70710678f) < 1e-5f);
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(1e-7f, 0, 0), {}, 0, 1}, {S_vector(0, 1e-7f, 0), {}, 1, 0}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.z == 1.0f && shardVerts(0)[0].normal.x == 0.0f);
        g_gfLevels.clear();
        setFaces({{S_vector(-1e-9f, 0, 0.8e-8f), {}, 0, 0}, {S_vector(0, 0, 0), {}, 0, 1}, {S_vector(0, 1e-4f, 0), {}, 1, 0}},
                 {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, shardVerts(0)[0].normal.x == 1.0f && shardVerts(0)[0].normal.z < 0.01f);
        // Off the origin.
        g_gfLevels.clear();
        setFaces({{S_vector(1, 1, 1), {}, 0, 0}, {S_vector(1.1f, 1, 1), {}, 1, 0}, {S_vector(1, 1.1f, 1), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfGlass, std::fabs(shardVerts(0)[0].normal.z - 1.0f) < 1e-5f);
        // Faces from the last, each locked again; the face's three vertices by its indices.
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(0.1f, 0, 0), {}, 1, 0}, {S_vector(0, 0.1f, 0), {}, 0, 1},
                  {S_vector(0, 0, 0.05f), {}, 0.5f, 0.5f}},
                 {0, 1, 2, 3, 2, 1});
        g_gfLog.clear();
        m.ShatterGlass(glass, 0.01f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2 && shardVerts(0)[0].pos.z == 0.05f && shardVerts(0)[0].u == 0.5f &&
                               shardVerts(0)[1].pos.y == 0.1f && shardVerts(0)[2].pos.x == 0.1f && shardVerts(1)[0].pos.x == 0.0f &&
                               std::count(g_gfLog.begin(), g_gfLog.end(), "lockfaces gg 0") == 2 &&
                               std::count(g_gfLog.begin(), g_gfLog.end(), "lockverts 0") == 1);
        // A big face split at its longest edge's middle until each part is at most the area.
        auto pos = [&](size_t shard, size_t i) { return shardVerts(shard).at(i).pos; };
        auto uv = [&](size_t shard, size_t i) { return std::make_pair(shardVerts(shard).at(i).u, shardVerts(shard).at(i).v); };
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(2, 0, 0), {}, 1, 0}, {S_vector(0, 2, 0), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.0f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2 && pos(0, 0) == S_vector(0, 0, 0) && pos(0, 1) == S_vector(1, 1, 0) &&
                               pos(0, 2) == S_vector(2, 0, 0) && pos(1, 2) == S_vector(0, 2, 0) &&
                               uv(0, 1) == std::make_pair(0.5f, 0.5f) && uv(0, 2) == std::make_pair(1.0f, 0.0f) &&
                               uv(1, 2) == std::make_pair(0.0f, 1.0f) && uv(1, 0) == std::make_pair(0.0f, 0.0f));
        g_gfLevels.clear();
        setFaces({{S_vector(10, 0, 0), {}, 0, 0.2f}, {S_vector(12, 0, 0), {}, 1, 0.4f}, {S_vector(10, 2, 0), {}, 0.6f, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.0f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2 && pos(0, 1) == S_vector(11, 1, 0) && pos(0, 0) == S_vector(10, 0, 0) &&
                               std::fabs(uv(0, 1).first - 0.8f) < 1e-6f && std::fabs(uv(0, 1).second - 0.7f) < 1e-6f);
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(2, 0, 0), {}, 1, 0}, {S_vector(0, 2, 0), {}, 0, 1}}, {0, 1, 2});
        g_gfLevels.clear();
        m.ShatterGlass(glass, 0.99f);
        WM(r.gfShardSplit, g_gfLevels.size() == 4);
        g_gfLevels.clear();
        m.ShatterGlass(glass, 2.5f);
        WM(r.gfShardSplit, g_gfLevels.size() == 1);
        // Ties: the first longest edge (0-1 before 1-2, 1-2 before 2-0).
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(1, 0, 2), {}, 1, 0}, {S_vector(2, 0, 0), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.5f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2 && pos(0, 0) == S_vector(2, 0, 0) && pos(0, 1) == S_vector(0.5f, 0, 1) &&
                               pos(0, 2) == S_vector(0, 0, 0) && uv(0, 0) == std::make_pair(0.0f, 1.0f) &&
                               uv(0, 1) == std::make_pair(0.5f, 0.0f) && uv(1, 2) == std::make_pair(1.0f, 0.0f));
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(2, 0, 0), {}, 1, 0}, {S_vector(1, 0, 2), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.5f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2 && pos(0, 0) == S_vector(0, 0, 0) && pos(0, 1) == S_vector(1.5f, 0, 1) &&
                               pos(0, 2) == S_vector(2, 0, 0));
        g_gfLevels.clear();
        setFaces({{S_vector(1, 0, 2), {}, 0, 0}, {S_vector(0, 0, 0), {}, 1, 0}, {S_vector(2, 0, 0), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.5f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2 && pos(0, 0) == S_vector(2, 0, 0) && pos(0, 1) == S_vector(0.5f, 0, 1) &&
                               pos(0, 2) == S_vector(1, 0, 2));
        // A degenerate face (no edge): one shard.
        g_gfLevels.clear();
        setFaces({{S_vector(1, 1, 1), {}, 0, 0}, {S_vector(1, 1, 1), {}, 1, 0}, {S_vector(1, 1, 1), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 0.0f);
        WM(r.gfShardSplit, g_gfLevels.size() == 1);
        // A thin face: its height (not its edge) decides.
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(4, 0, 0), {}, 1, 0}, {S_vector(2, 0.5f, 0), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.0f);
        WM(r.gfShardSplit, g_gfLevels.size() == 1);
        g_gfLevels.clear();
        setFaces({{S_vector(0, 0, 0), {}, 0, 0}, {S_vector(4, 0, 0), {}, 1, 0}, {S_vector(1, 0, 0.75f), {}, 0, 1}}, {0, 1, 2});
        m.ShatterGlass(glass, 1.0f);
        WM(r.gfShardSplit, g_gfLevels.size() == 2);
    }

    std::memcpy(g_driverVtable, driverSlots, sizeof driverSlots);
    std::memcpy(g_igraphVtable, igraphSlots, sizeof igraphSlots);
    WmField<I3D_frame *>(scene, 0x1F8) = oldPrimary;
    std::printf("geffects2: gfShadow %d gfShadowName %d gfShadowFind %d gfFace %d gfFaceFind %d gfUniform %d gfUniformFile %d "
                "gfGlass %d gfShardSplit %d gfShardPhysics %d\n",
                r.gfShadow, r.gfShadowName, r.gfShadowFind, r.gfFace, r.gfFaceFind, r.gfUniform, r.gfUniformFile, r.gfGlass,
                r.gfShardSplit, r.gfShardPhysics);
}
