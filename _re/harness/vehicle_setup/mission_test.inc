// Part 8f-ii's mission loading (game/mission_loader.cpp: C_game::LoadMission, the loader's
// Begin, Scene, actors.bin, sounds.bin, check2.bin), in the camera build, run by loader_test.inc's
// LoaderTest inside its fakes (the loading screen is the real one): scene2.bin's reader (part
// 8f-iii), the caches' Clear, C_game::FreeMission, the human animation tables, the checkpoint
// networks, the collision world's Load, the modules' LoadMissionData and the particles' static
// emitters faked (logged); the files in g_files. Prints "mission:" with 1 per check met.

#define private public
#include "game/mission_loader.h"
#undef private

C_ModelCache g_versionModelCache;
template <> void C_ResourceCache<I3D_model>::Clear()
{
    GfLog(this == &g_modelCache ? "clear models" : this == &g_versionModelCache ? "clear versions" : "clear ?");
}
template <> void C_ResourceCache<I3D_shadow>::Clear()
{
    GfLog(this == &g_shadowCache ? "clear shadows" : "clear ?");
}
void C_game::FreeMission() { GfLog("freemission"); }
bool g_msAnimTables = true;
bool EnsureHumanAnimTablesLoaded()
{
    GfLog("animtables");
    return g_msAnimTables;
}
alignas(16) uint8_t g_msCheckpointsMem[sizeof(C_Checkpoints)];
C_Checkpoints *MsCheckpoints() { return reinterpret_cast<C_Checkpoints *>(g_msCheckpointsMem); }
int __stdcall LoadCheckpointNetwork(C_chunk *chunk, C_CheckpointNetwork *network)
{
    uint8_t b = 0;
    chunk->Read(&b, 1);
    GfLog("cpnet %s %d", network == MsCheckpoints() ? "walk" : network == MsCheckpoints()->GetRoadNetwork() ? "roads" : "?",
          b);
    return 1;
}
void C_Checkpoints::Clear() { GfLog(this == MsCheckpoints() ? "cpclear" : "cpclear ?"); }
void C_CollisionWorld::Load(const char *fileName, bool buildGrid)
{
    GfLog("collision %s %d %d", fileName, buildGrid, this == &g_collisionWorld);
}
namespace ModuleManager {
void BroadcastLoadMissionData(bool server) { GfLog("loaddata %d", server); }
}  // namespace ModuleManager
void C_ParticlesModule::LoadStaticEmitters()
{
    GfLog("emitters %d %d", this == &g_particlesModule, m_scene == GetGame()->GetScene());
}
void C_game::AddAnimatedModel(I3D_frame *model) { GfLog("animated %s", GfName(model)); }
bool LoadTyres()
{
    GfLog("tyres");
    return true;
}
bool InitCarSurfaces()
{
    GfLog("surfaces");
    return true;
}

// The actors scene2.bin makes (part 8f-iii): logged; SetFrame fails for type 9, CreateActor gives
// none for 99.
class C_MsActor : public C_Actor {
public:
    explicit C_MsActor(E_ActorType type) : C_Actor(type) {}
    void CopyFrom(const C_Actor *) override {}
    void StartMission(bool) override {}
    void StopMission(bool) override {}
    void RestartMission(bool) override {}
    bool SetFrame(I3D_frame *frame) override
    {
        GfLog("setframe %d %s", GetType(), frame ? GfName(frame) : "-");
        return GetType() != 9;
    }
    bool ReadProperties(C_chunk &chunk) override
    {
        uint8_t b = 0;
        chunk.Read(&b, 1);
        GfLog("props %d %d", GetType(), b);
        return true;
    }
};
C_Actor *C_ActorsModule::CreateActor(E_ActorType type, I3D_frame *frame, bool active, int slot)
{
    GfLog("createactor %d %s %d %d", type, frame ? GfName(frame) : "-", active, slot);
    return type == 99 ? nullptr : new C_MsActor(type);
}
void C_ActorsModule::DestroyActor(C_Actor *actor, bool immediately)
{
    GfLog("destroyactor %d %d", actor->GetType(), immediately);
    // (Not deleted: a use after this logs as the actor's own.)
}

namespace {

struct S_MissionResults {
    bool msBegin = true, msScene = true, msObjects = true, msCheckpoints = true, msLoad = true, msFail = true;
    bool msModules = true;
};

// The scene file's progress callback, as the scene's Open got it.
using T_MsProgress = void(__stdcall *)(int message, float fraction, uint32_t, void *);
T_MsProgress g_msProgress;
HRESULT __stdcall F_MsOpen(void *self, const char *name, uint32_t flags, void *callback, void *context)
{
    if (self == GetGame()->GetScene())
        g_msProgress = reinterpret_cast<T_MsProgress>(callback);
    return F_LsOpen(self, name, flags, callback, context);
}
void __stdcall F_MsF0(void *self) { GfLog(self == GetGame()->GetScene() ? "f0" : "f0 ?"); }

// --- part 8f-iv: the objects' engine ------------------------------------------------------------

// The objects' frames, the scene's and the driver's slots LoadObject calls: logged (quiet while
// g_moQuiet, for the earlier tests' stand-in objects: then only SetName logs, as "object NAME").
bool g_moQuiet = true;
#define MO_LOG(...)                                                                                                    \
    do {                                                                                                               \
        if (!g_moQuiet)                                                                                                \
            GfLog(__VA_ARGS__);                                                                                        \
    } while (0)
void *g_moFrameVt[SLOTS], *g_moLightVt[SLOTS], *g_moSoundVt[SLOTS], *g_moSectorVt[SLOTS], *g_moModelVt[SLOTS];
void *g_moMeshVt[SLOTS], *g_moAreaVt[SLOTS], *g_moLmapVt[SLOTS], *g_moProjVt[SLOTS];
HRESULT g_moDupResult = 0;
std::map<const I3D_frame *, std::vector<I3D_sector *>> g_moLightSectors;
int g_moMade = 0;
void *g_moOldCreateFrame, *g_moOldCreateVisual;

uint32_t __stdcall F_MoRelease(I3D_frame *self)
{
    MO_LOG("release %s", GfName(self));
    return static_cast<uint32_t>(--self->m_refCount);
}
void __stdcall F_MoUpdate(I3D_frame *self) { MO_LOG("update %s", GfName(self)); }
void __stdcall F_MoSetOn(I3D_frame *self, bool on) { MO_LOG("on %s %d", GfName(self), on); }
void __stdcall F_MoSetName(I3D_frame *self, const char *name)
{
    GfLog("object %s", name);
    g_stubs[self]->name = name;
    self->m_name = g_stubs[self]->name.c_str();
}
I3D_frame *__stdcall F_MoGetChild(I3D_frame *self, int index)
{
    I3D_frame *c = self->m_firstChild;
    while (c && index-- > 0)
        c = c->m_nextSibling;
    return c;
}
// The children of the types in `mask` (not deeper).
void __stdcall F_MoEnum(I3D_frame *self, I3D_ENUMPROC *callback, void *context, uint32_t mask, const char *)
{
    MO_LOG("menum %s %x", GfName(self), mask);
    for (I3D_frame *c = self->m_firstChild; c; c = c->m_nextSibling)
        if ((1u << (c->GetType() - 1)) & mask)
            if (callback(c, context) == 2)
                return;
}
HRESULT __stdcall F_MoDuplicate(I3D_frame *self, I3D_frame *source)
{
    MO_LOG("dup %s %s", GfName(self), GfName(source));
    return g_moDupResult;
}
// The vectors' and the floats' slots by their tags.
template <int TAG> void __stdcall F_MoVec(I3D_frame *self, const S_vector &v)
{
    static const char *const tags[] = {"lcolor", "lspec", "lamb", "pcolor"};
    MO_LOG("%s %s %.2f %.2f %.2f", tags[TAG], GfName(self), v.x, v.y, v.z);
}
template <int TAG> void __stdcall F_MoFloat(I3D_frame *self, float f)
{
    static const char *const tags[] = {"lpower", "lspow", "lapow", "lrange", "sout", "svol", "sfreq", "pint"};
    MO_LOG("%s %s %.4f", tags[TAG], GfName(self), f);
}
template <int TAG> void __stdcall F_MoInt(I3D_frame *self, int i)
{
    static const char *const tags[] = {"ltype", "lmode", "stype", "portho", "pmode", "pbuf", "lmsets"};
    MO_LOG("%s %s %d", tags[TAG], GfName(self), i);
}
template <int TAG> void __stdcall F_MoFloat2(I3D_frame *self, float a, float b)
{
    static const char *const tags[] = {"lcone", "scone"};
    MO_LOG("%s %s %.2f %.2f", tags[TAG], GfName(self), a, b);
}
void __stdcall F_MoAtten(I3D_frame *self, float a0, float a1, float a2)
{
    MO_LOG("latten %s %.8f %.8f %.8f", GfName(self), a0, a1, a2);
    WmField<float>(self, 0x17C) = a0;
    WmField<float>(self, 0x180) = a1;
    WmField<float>(self, 0x184) = a2;
}
// The sound: Open fails for names with "missing".
HRESULT __stdcall F_MoSoundOpen(I3D_frame *self, const char *file, uint32_t flags)
{
    MO_LOG("sopen %s %s %x", GfName(self), file, flags);
    return std::strstr(file, "missing") ? -1 : std::strstr(file, "one") ? 1 : 0;
}
void __stdcall F_MoSoundRange(I3D_frame *self, float a, float b, float c, float d)
{
    MO_LOG("srange %s %.2f %.2f %.2f %.2f", GfName(self), a, b, c, d);
}
void __stdcall F_MoLoop(I3D_frame *self, bool loop) { MO_LOG("sloop %s %d", GfName(self), loop); }
// A light's sectors (its fields +0x120 / +0x128 as the engine's).
void MoSyncSectors(I3D_frame *light)
{
    std::vector<I3D_sector *> &v = g_moLightSectors[light];
    WmField<I3D_sector **>(light, 0x120) = v.data();
    WmField<int>(light, 0x128) = static_cast<int>(v.size());
}
void __stdcall F_MoAddLight(I3D_frame *self, I3D_frame *light)
{
    MO_LOG("addlight %s %s", GfName(self), GfName(light));
    g_moLightSectors[light].push_back(reinterpret_cast<I3D_sector *>(self));
    MoSyncSectors(light);
}
void __stdcall F_MoRemoveLight(I3D_frame *self, I3D_frame *light)
{
    MO_LOG("removelight %s %s", GfName(self), GfName(light));
    std::vector<I3D_sector *> &v = g_moLightSectors[light];
    v.erase(std::find(v.begin(), v.end(), reinterpret_cast<I3D_sector *>(self)));
    MoSyncSectors(light);
}
void __stdcall F_MoAddSound(I3D_frame *self, I3D_frame *sound) { MO_LOG("addsound %s %s", GfName(self), GfName(sound)); }
HRESULT __stdcall F_MoEnv(I3D_frame *self, int index, uintptr_t value)
{
    if (index >= 20) {
        const auto *v = reinterpret_cast<const float *>(value);
        MO_LOG("env %s %d %.1f %.1f %.1f", GfName(self), index, v[0], v[1], v[2]);
    } else {
        MO_LOG("env %s %d %u", GfName(self), index, static_cast<unsigned>(value));
    }
    return 0;
}
// An occluder's or an area's mesh: the counts, the stride, the first and last vertex and index.
HRESULT __stdcall F_MoBuild(I3D_frame *self, const void *verts, int numVerts, const void *tris, int numTris,
                            uint32_t stride)
{
    std::string text = CmText("build %s %d %d %u", GfName(self), numVerts, numTris, stride);
    if (verts && numVerts > 0) {
        const auto *v = static_cast<const S_vector4 *>(verts);
        text += CmText(" v %.1f %.1f %.1f %.1f / %.1f %.1f %.1f %.1f", v[0].x, v[0].y, v[0].z, v[0].w,
                       v[numVerts - 1].x, v[numVerts - 1].y, v[numVerts - 1].z, v[numVerts - 1].w);
    } else {
        text += verts ? " v ?" : " v -";
    }
    if (tris && numTris > 0) {
        const auto *t = static_cast<const uint16_t *>(tris);
        text += CmText(" t %d %d", t[0], t[numTris * 3 - 1]);
    }
    MO_LOG("%s", text.c_str());
    return 0;
}
void __stdcall F_MoAddModelFrame(I3D_frame *self, I3D_frame *f) { MO_LOG("addmf %s %s", GfName(self), GfName(f)); }
void __stdcall F_MoRemoveModelFrame(I3D_frame *self, I3D_frame *f) { MO_LOG("rmmf %s %s", GfName(self), GfName(f)); }
void __stdcall F_MoTexture(I3D_frame *self, void *image) { MO_LOG("ptex %s %d", GfName(self), image != nullptr); }
void __stdcall F_MoReproject(I3D_frame *self, int a, int b) { MO_LOG("preproj %s %d %d", GfName(self), a, b); }
// The scene's RemoveFrame and UpdateFrameSector; the driver's CreateVisual (none for 0xDEAD) and
// CreateFrame (a camera: the camera tests' fake); IGraph's LoadTexture ("missing": failing,
// "null": none).
int __stdcall F_MoRemoveFrame(void *, I3D_frame *frame)
{
    MO_LOG("rmframe %s", GfName(frame));
    return 0;
}
void __stdcall F_MoFrameSector(void *, I3D_frame *frame, const S_vector &pos)
{
    MO_LOG("fsector %s %.1f %.1f %.1f", GfName(frame), pos.x, pos.y, pos.z);
}
void **MoVtable(int type, uint32_t visualType)
{
    switch (type) {
    case FRAME_VISUAL:
        return visualType == I3DVISUAL_LMAP ? g_moLmapVt : visualType == I3DVISUAL_PROJECTOR ? g_moProjVt : g_moFrameVt;
    case FRAME_LIGHT: return g_moLightVt;
    case FRAME_SOUND: return g_moSoundVt;
    case FRAME_SECTOR: return g_moSectorVt;
    case FRAME_MODEL: return g_moModelVt;
    case FRAME_OCCLUDER: return g_moMeshVt;
    case FRAME_AREA: return g_moAreaVt;
    default: return g_moFrameVt;
    }
}
I3D_frame *MoFrame(int type, const std::string &name, uint32_t visualType = 0, I3D_frame *parent = nullptr)
{
    I3D_frame *f = F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(type), name, parent, MoVtable(type, visualType));
    if (visualType)
        g_gfVisualTypes[f] = visualType;
    return f;
}
I3D_frame *__stdcall F_MoCreateVisual(void *, uint32_t type)
{
    MO_LOG("cvisual %x", type);
    return type == 0xDEAD ? nullptr : MoFrame(FRAME_VISUAL, CmText("new%d", ++g_moMade), type);
}
I3D_frame *__stdcall F_MoCreateFrame(void *driver, int type)
{
    // (Quiet: the stand-in objects' only, the loading screen's own frames the earlier fakes'.)
    if (type == FRAME_CAMERA || (g_moQuiet && type != FRAME_USER))
        return reinterpret_cast<I3D_frame *(__stdcall *)(void *, int)>(g_moOldCreateFrame)(driver, type);
    MO_LOG("cframe %d", type);
    return MoFrame(type, CmText("new%d", ++g_moMade));
}
HRESULT __stdcall F_MoLoadTexture(void *self, const char *name, const char *alpha, uint32_t mode, S_Image **out)
{
    MO_LOG("texture %s %d %u", name, alpha != nullptr, mode);
    if (std::strstr(name, "missing")) {
        *out = reinterpret_cast<S_Image *>(0x1);        // (not used: the call failed)
        return 1;
    }
    if (std::strstr(name, "null")) {
        *out = nullptr;
        return 0;
    }
    return F_LoadTexture(self, name, alpha, mode, out);
}
// The model cache's copy: a child "part" of each model opened.
void MoFill(I3D_model *model) { MoFrame(FRAME_DUMMY, "part", 0, reinterpret_cast<I3D_frame *>(model)); }

void MoInitVtables()
{
    for (int i = 0; i < SLOTS; ++i)
        g_moFrameVt[i] = g_gfFrameVt[i];
    g_moFrameVt[0x00 / 4] = reinterpret_cast<void *>(&F_MoRelease);
    g_moFrameVt[0x24 / 4] = reinterpret_cast<void *>(&F_MoUpdate);
    g_moFrameVt[0x30 / 4] = reinterpret_cast<void *>(&F_MoSetOn);
    g_moFrameVt[0x34 / 4] = reinterpret_cast<void *>(&F_MoSetName);
    g_moFrameVt[0x40 / 4] = reinterpret_cast<void *>(&F_MoGetChild);
    g_moFrameVt[0x44 / 4] = reinterpret_cast<void *>(&F_MoEnum);
    g_moFrameVt[0x4C / 4] = reinterpret_cast<void *>(&F_MoDuplicate);
    for (void **vt : {g_moLightVt, g_moSoundVt, g_moSectorVt, g_moModelVt, g_moMeshVt, g_moAreaVt, g_moLmapVt, g_moProjVt})
        for (int i = 0; i < SLOTS; ++i)
            vt[i] = g_moFrameVt[i];
    g_moLightVt[0x74 / 4] = reinterpret_cast<void *>(&F_MoInt<0>);
    g_moLightVt[0x7C / 4] = reinterpret_cast<void *>(&F_MoVec<0>);
    g_moLightVt[0x80 / 4] = reinterpret_cast<void *>(&F_MoVec<1>);
    g_moLightVt[0x84 / 4] = reinterpret_cast<void *>(&F_MoVec<2>);
    g_moLightVt[0x88 / 4] = reinterpret_cast<void *>(&F_MoFloat<0>);
    g_moLightVt[0x8C / 4] = reinterpret_cast<void *>(&F_MoFloat<1>);
    g_moLightVt[0x90 / 4] = reinterpret_cast<void *>(&F_MoFloat<2>);
    g_moLightVt[0x94 / 4] = reinterpret_cast<void *>(&F_MoInt<1>);
    g_moLightVt[0x98 / 4] = reinterpret_cast<void *>(&F_MoFloat<3>);
    g_moLightVt[0x9C / 4] = reinterpret_cast<void *>(&F_MoAtten);
    g_moLightVt[0xA4 / 4] = reinterpret_cast<void *>(&F_MoFloat2<0>);
    g_moSoundVt[0x74 / 4] = reinterpret_cast<void *>(&F_MoSoundOpen);
    g_moSoundVt[0x7C / 4] = reinterpret_cast<void *>(&F_MoInt<2>);
    g_moSoundVt[0x80 / 4] = reinterpret_cast<void *>(&F_MoSoundRange);
    g_moSoundVt[0x84 / 4] = reinterpret_cast<void *>(&F_MoFloat2<1>);
    g_moSoundVt[0x88 / 4] = reinterpret_cast<void *>(&F_MoFloat<4>);
    g_moSoundVt[0x8C / 4] = reinterpret_cast<void *>(&F_MoFloat<5>);
    g_moSoundVt[0x90 / 4] = reinterpret_cast<void *>(&F_MoLoop);
    g_moSoundVt[0x98 / 4] = reinterpret_cast<void *>(&F_MoFloat<6>);
    g_moSectorVt[0x74 / 4] = reinterpret_cast<void *>(&F_MoAddLight);
    g_moSectorVt[0x78 / 4] = reinterpret_cast<void *>(&F_MoRemoveLight);
    g_moSectorVt[0x7C / 4] = reinterpret_cast<void *>(&F_MoAddSound);
    g_moSectorVt[0x84 / 4] = reinterpret_cast<void *>(&F_MoEnv);
    g_moModelVt[0x7C / 4] = reinterpret_cast<void *>(&F_MoAddModelFrame);
    g_moModelVt[0x80 / 4] = reinterpret_cast<void *>(&F_MoRemoveModelFrame);
    g_moModelVt[0x88 / 4] = reinterpret_cast<void *>(&F_GfGetAnimation);
    g_moMeshVt[0x74 / 4] = g_moAreaVt[0x74 / 4] = reinterpret_cast<void *>(&F_MoBuild);
    g_moAreaVt[0x78 / 4] = reinterpret_cast<void *>(&F_MoAddSound);
    g_moLmapVt[0xBC / 4] = reinterpret_cast<void *>(&F_MoInt<6>);
    g_moProjVt[0x9C / 4] = reinterpret_cast<void *>(&F_MoTexture);
    g_moProjVt[0xA4 / 4] = reinterpret_cast<void *>(&F_MoInt<3>);
    g_moProjVt[0xA8 / 4] = reinterpret_cast<void *>(&F_MoFloat<7>);
    g_moProjVt[0xB0 / 4] = reinterpret_cast<void *>(&F_MoVec<3>);
    g_moProjVt[0xB4 / 4] = reinterpret_cast<void *>(&F_MoInt<4>);
    g_moProjVt[0xB8 / 4] = reinterpret_cast<void *>(&F_MoInt<5>);
    g_moProjVt[0xBC / 4] = reinterpret_cast<void *>(&F_MoReproject);
}

// The scene's EnumFrames (its frames: g_msNamed), slots 0xE4 / 0x128, AddFrame; the driver's
// FindFrame (g_msDriverFrames).
std::vector<I3D_frame *> g_msNamed;
void __stdcall F_MsEnum(void *, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *)
{
    GfLog("enum %x", typeMask);
    for (I3D_frame *f : g_msNamed)
        if (callback(f, context) != 0)                  // (non-zero: stop)
            break;
}
void __stdcall F_MsE4(void *, float a, float b) { GfLog("e4 %.3f %.3f", a, b); }
void __stdcall F_Ms128(void *, I3D_frame *frame) { GfLog("s128 %s", GfName(frame)); }
void __stdcall F_MsAddFrame(void *, I3D_frame *frame)
{
    if (!g_moQuiet || frame->GetType() != FRAME_USER)   // (not the earlier tests' stand-in objects)
        GfLog("addframe %s", GfName(frame));
}
std::map<std::string, I3D_frame *> g_msDriverFrames;
I3D_frame *__stdcall F_MsDriverFind(void *, I3D_scene *, const char *name)
{
    const auto it = g_msDriverFrames.find(name);
    GfLog("dfind %s", name);
    return it == g_msDriverFrames.end() ? nullptr : it->second;
}

// A chunk file: `id`, its size, then `body`.
std::vector<uint8_t> MsChunkFile(uint16_t id, const std::vector<uint8_t> &body)
{
    std::vector<uint8_t> f(6);
    std::memcpy(f.data(), &id, 2);
    const uint32_t size = static_cast<uint32_t>(6 + body.size());
    std::memcpy(f.data() + 2, &size, 4);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

// A chunk: `id`, its size, `body` (MsChunkFile's, for nesting).
std::vector<uint8_t> MsChunk(uint16_t id, const std::vector<uint8_t> &body) { return MsChunkFile(id, body); }
std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
std::vector<uint8_t> MsObject(uint8_t b);
// An object file: its object chunk with one object (logged "object B").
std::vector<uint8_t> MsObjectsFile(uint8_t b)
{
    return MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0x4000, MsObject(b)));
}
// The log without the loading screen's redraws.
std::vector<std::string> MsLog()
{
    std::vector<std::string> log;
    for (const std::string &l : g_gfLog)
        if (l != "winmsg")
            log.push_back(l);
    return log;
}
// scene2.bin's start with the scene's camera "mscam".
std::vector<std::string> MsScene2Prologue()
{
    return {"tyres", "surfaces", "enum 4ffff", "ortho mscam 0", "range mscam 0.1 500.0", "fov mscam 1.3963",
            "active mscam", "update mscam"};
}

void MsScene2Test();
void MsObjectTest();

void MissionLoaderTest()
{
    S_MissionResults r;
    C_game *game = GetGame();
    C_Checkpoints *const oldCheckpoints = game->m_checkpoints;
    game->m_checkpoints = MsCheckpoints();
    void *const oldOpen = g_sceneVtable[0xF4 / 4], *const oldF0 = g_sceneVtable[0xF0 / 4];
    g_sceneVtable[0xF4 / 4] = reinterpret_cast<void *>(&F_MsOpen);
    g_sceneVtable[0xF0 / 4] = reinterpret_cast<void *>(&F_MsF0);
    const uint32_t oldRole = g_gameRole;
    I3D_scene *const oldCarScene = g_carScene;
    I3D_frame *const oldActive = CmActive();
    I3D_frame *oldSectors[3];
    for (int i = 0; i < 3; ++i) {
        oldSectors[i] = WmField<I3D_frame *>(game->GetScene(), 0x1F8 + 4 * i);
        WmField<I3D_frame *>(game->GetScene(), 0x1F8 + 4 * i) = GfFrame(FRAME_SECTOR, CmText("sector%d", i));
    }
    CmActive() = CmCamera("mscam", S_vector(0.0f, 0.0f, 0.0f));
    void *const oldSlots[] = {g_sceneVtable[0x44 / 4], g_sceneVtable[0xE4 / 4], g_sceneVtable[0x128 / 4],
                              g_sceneVtable[0x80 / 4], g_driverVtable[0x88 / 4]};
    g_sceneVtable[0x44 / 4] = reinterpret_cast<void *>(&F_MsEnum);
    g_sceneVtable[0xE4 / 4] = reinterpret_cast<void *>(&F_MsE4);
    g_sceneVtable[0x128 / 4] = reinterpret_cast<void *>(&F_Ms128);
    g_sceneVtable[0x80 / 4] = reinterpret_cast<void *>(&F_MsAddFrame);
    g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_MsDriverFind);
    // The objects' engine (part 8f-iv; a camera: the camera tests' CreateFrame).
    MoInitVtables();
    g_moOldCreateFrame = g_driverVtable[0x54 / 4];
    g_moOldCreateVisual = g_driverVtable[0x50 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_MoCreateFrame);
    g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&F_MoCreateVisual);
    const bool oldLoaded = game->m_missionLoaded;
    const auto files = [](const char *m, bool actors, bool sounds, int check) {
        for (const char *f : {"actors.bin", "sounds.bin", "check2.bin", "check.bin", "scene2.bin"})
            g_files.erase(CmText("missions\\%s\\%s", m, f));
        g_files[CmText("missions\\%s\\scene2.bin", m)] = MsChunkFile(0x1234, {});
        if (actors)
            g_files[CmText("missions\\%s\\actors.bin", m)] = MsObjectsFile(0x21);
        if (sounds)
            g_files[CmText("missions\\%s\\sounds.bin", m)] = MsObjectsFile(0x31);
        if (check == 2)
            g_files[CmText("missions\\%s\\check2.bin", m)] = {0x41, 0x42};
        if (check == 1)
            g_files[CmText("missions\\%s\\check.bin", m)] = {0x51};
    };

    // --- a server's whole loading: the files, the screen's progress, the modules' data ---
    {
        g_gameRole = GAME_ROLE_SERVER;
        files("m7", true, true, 2);
        g_msAnimTables = true;
        game->m_missionLoaded = false;
        game->m_unknown66 = game->m_unknown68 = 5;
        game->m_unknownD8 = 6;
        g_carScene = nullptr;
        g_gfLog.clear();
        const int result = game->LoadMission("m7", nullptr);
        std::vector<std::string> want = {"freemission", "clear models", "clear shadows", "clear versions", "animtables",
                                         "open missions\\m7\\Scene 7 1 0", "f0"};
        for (const std::string &l : MsScene2Prologue())
            want.push_back(l);
        for (const char *l : {"object 33", "object 49", "cpnet walk 65", "cpnet roads 66",
                              "collision missions\\m7\\tree.klz 1 1", "emitters 1 1", "loaddata 1"})
            want.push_back(l);
        WM(r.msLoad, result == 0 && GfVecIs(MsLog(), want) && game->m_missionLoaded && game->m_unknown66 == 0 &&
                         game->m_unknown68 == 0 && game->m_unknownD8 == 0);
        WM(r.msBegin, game->m_missionName == C_str("m7") && g_carScene == game->GetScene() && g_loaderScreen.m_shown &&
                          !g_loaderScreen.m_scene && g_loaderScreen.m_twoParts && !g_loaderScreen.m_waitForKey);
        // The stuff stage's three steps (two-part table: 0.5 + 0.05 x).
        WM(r.msLoad, g_loaderScreen.m_progress == 0.05f * 1.0f + 0.5f);
        // The scene's progress: the first stage (not messages 0x10, 0x20).
        g_loaderScreen.m_progress = 0.0f;
        g_msProgress(0x10, 0.5f, 0, nullptr);
        g_msProgress(0x20, 0.5f, 0, nullptr);
        WM(r.msScene, g_loaderScreen.m_progress == 0.0f);
        g_msProgress(0x11, 0.5f, 0, nullptr);
        WM(r.msScene, g_loaderScreen.m_progress == 0.15f * 0.5f + 0.0f);
        g_msProgress(0, 1.0f, 0, nullptr);
        WM(r.msScene, g_loaderScreen.m_progress == 0.15f * 1.0f + 0.0f);
        g_loaderScreen.Finish();
        // The name as text.
        files("m%d", false, false, 0);
        g_gfLog.clear();
        game->LoadMission("m%d", nullptr);
        WM(r.msBegin, game->m_missionName == C_str("m%d") && g_gfLog.size() > 5 && g_gfLog[5] == "open missions\\m%d\\Scene 7 1 0");
        g_loaderScreen.Finish();
    }

    // --- the object files: their object chunk only ---
    {
        files("m8", false, false, 2);
        g_files["missions\\m8\\actors.bin"] = MsChunkFile(0x4C54, MsChunk(0x4000, MsObject(0x21)));
        g_files["missions\\m8\\sounds.bin"] = {0x53};   // (too short for a chunk)
        g_gfLog.clear();
        game->LoadMission("m8", nullptr);
        bool objects = false;
        for (const std::string &l : g_gfLog)
            objects = objects || l.compare(0, 6, "object") == 0;
        WM(r.msObjects, !objects);
        g_loaderScreen.Finish();
        // (After the chunk: the file's next chunk not read.)
        std::vector<uint8_t> two = MsObjectsFile(0x61);
        const std::vector<uint8_t> second = MsObjectsFile(0x62);
        two.insert(two.end(), second.begin(), second.end());
        g_files["missions\\m8\\actors.bin"] = two;
        g_gfLog.clear();
        game->LoadMission("m8", nullptr);
        int count = 0;
        for (const std::string &l : g_gfLog)
            count += l == "object 97";
        WM(r.msObjects, count == 1 && std::find(g_gfLog.begin(), g_gfLog.end(), "object 98") == g_gfLog.end());
        g_loaderScreen.Finish();
    }

    // --- the checkpoints: check2.bin, else cleared (check.bin only opened) ---
    {
        for (int check = 0; check < 3; ++check) {
            files("m9", false, false, check);
            g_gfLog.clear();
            game->LoadMission("m9", nullptr);
            std::vector<std::string> cp;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 2, "cp") == 0)
                    cp.push_back(l);
            const std::vector<std::string> want = check == 2   ? std::vector<std::string>{"cpnet walk 65", "cpnet roads 66"}
                                                  : check == 1 ? std::vector<std::string>{"cpclear"}
                                                               : std::vector<std::string>{"cpclear", "cpclear"};
            WM(r.msCheckpoints, GfVecIs(cp, want));
            g_loaderScreen.Finish();
        }
    }

    // --- failures: the animation tables (nothing more), scene2.bin (the rest of its stage first) ---
    {
        files("m10", true, true, 2);
        g_msAnimTables = false;
        game->m_missionLoaded = false;
        g_gfLog.clear();
        int result = game->LoadMission("m10", nullptr);
        WM(r.msFail, result == 1 && GfLogIs({"freemission", "clear models", "clear shadows", "clear versions", "animtables",
                                             "loaddata 1"}) &&
                         !game->m_missionLoaded);
        g_loaderScreen.Finish();
        g_msAnimTables = true;
        g_files["missions\\m10\\scene2.bin"] = {0x53};   // (its header does not read)
        g_gfLog.clear();
        result = game->LoadMission("m10", nullptr);
        const std::vector<std::string> log = MsLog();
        WM(r.msFail, result == 1 && log.size() > 3 && log.back() == "loaddata 1" && log[log.size() - 2] == "cpnet roads 66" &&
                         !game->m_missionLoaded && g_loaderScreen.m_progress == 0.05f * 0.3f + 0.5f &&
                         std::find(log.begin(), log.end(), "object 33") != log.end());
        g_loaderScreen.Finish();
    }

    // --- the modules' data by the role: a client's, a single player's both (the loading screen
    // shown: its scene not opened here) ---
    {
        files("m11", false, false, 0);
        g_lsOpenResult = -5;
        const uint32_t roles[] = {GAME_ROLE_CLIENT, GAME_ROLE_LOCAL, 0};
        for (uint32_t role : roles) {
            g_gameRole = role;
            LsVisuals({});
            LsQueueFrames();
            g_gfLog.clear();
            game->LoadMission("m11", nullptr);
            std::vector<std::string> data;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 8, "loaddata") == 0)
                    data.push_back(l);
            const std::vector<std::string> want = role == GAME_ROLE_CLIENT  ? std::vector<std::string>{"loaddata 0"}
                                                  : role == GAME_ROLE_LOCAL ? std::vector<std::string>{"loaddata 1", "loaddata 0"}
                                                                            : std::vector<std::string>{};
            // (The screen: shown on a client, waiting in single player, in two parts.)
            WM(r.msModules, GfVecIs(data, want) && g_gfLog.size() > 1 && (g_gfLog[0] == "create 13") == (role != 0) &&
                                g_loaderScreen.m_waitForKey == (role == GAME_ROLE_LOCAL) && g_loaderScreen.m_twoParts);
            g_loaderScreen.Finish();
        }
        g_lsOpenResult = 0;
        // A mission's definition reaches the screen (its briefing).
        g_gameRole = GAME_ROLE_CLIENT;
        LsVisuals({"briefing"});
        LsQueueFrames();
        g_coop = false;
        C_MpMissionDef *mission = LsMission(0);
        g_gfLog.clear();
        game->LoadMission("m11", mission);
        WM(r.msBegin, std::find(g_gfLog.begin(), g_gfLog.end(), "tb briefing addid 11 84") != g_gfLog.end());
        g_loaderScreen.Finish();
    }

    MsScene2Test();
    MsObjectTest();
    g_driverVtable[0x54 / 4] = g_moOldCreateFrame;
    g_driverVtable[0x50 / 4] = g_moOldCreateVisual;
    g_gameRole = oldRole;
    g_carScene = oldCarScene;
    CmActive() = oldActive;
    for (int i = 0; i < 3; ++i)
        WmField<I3D_frame *>(game->GetScene(), 0x1F8 + 4 * i) = oldSectors[i];
    g_sceneVtable[0x44 / 4] = oldSlots[0];
    g_sceneVtable[0xE4 / 4] = oldSlots[1];
    g_sceneVtable[0x128 / 4] = oldSlots[2];
    g_sceneVtable[0x80 / 4] = oldSlots[3];
    g_driverVtable[0x88 / 4] = oldSlots[4];
    game->m_missionLoaded = oldLoaded;
    game->m_checkpoints = oldCheckpoints;
    g_sceneVtable[0xF4 / 4] = oldOpen;
    g_sceneVtable[0xF0 / 4] = oldF0;
    std::printf("mission: msBegin %d msScene %d msObjects %d msCheckpoints %d msLoad %d msFail %d msModules %d\n",
                r.msBegin, r.msScene, r.msObjects, r.msCheckpoints, r.msLoad, r.msFail, r.msModules);
}

// --- part 8f-iii: scene2.bin's reader -----------------------------------------------------------

struct S_Scene2Results {
    bool s2File = true, s2Camera = true, s2Settings = true, s2List = true, s2TShift = true, s2Actors = true;
    bool s2States = true, s2Shadows = true, s2Groups = true, s2B820 = true, s2Music = true, s2Progress = true;
    bool s2Fail = true;
};

std::vector<uint8_t> MsBytes(const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    return std::vector<uint8_t>(p, p + size);
}
template <class T> std::vector<uint8_t> MsValue(const T &v) { return MsBytes(&v, sizeof v); }
std::vector<uint8_t> MsText(const char *text) { return MsBytes(text, std::strlen(text) + 1); }
// The earlier tests' stand-in object: a user frame named `b` (its SetName logged "object B"); 0xFF
// a header that does not read (the object fails).
std::vector<uint8_t> MsObject(uint8_t b)
{
    if (b == 0xFF)
        return MsChunk(0x4010, {1});
    return MsChunk(0x4010, MsChunk(0x4011, MsValue(static_cast<int>(FRAME_USER))) +
                               MsChunk(0x0010, MsText(std::to_string(b).c_str())));
}
// A shifting texture's entry: the two values, the name's length and the name.
std::vector<uint8_t> MsShift(int a, int b, const char *name)
{
    const int length = static_cast<int>(std::strlen(name));
    return MsChunk(0xAE01, MsValue(a) + MsValue(b) + MsValue(length) + MsBytes(name, length));
}
// An actor: its parts in order.
std::vector<uint8_t> MsActor(const std::vector<uint8_t> &parts) { return MsChunk(0xAE21, parts); }

void MsScene2Test()
{
    S_Scene2Results r;
    C_game *game = GetGame();
    g_gameRole = GAME_ROLE_SERVER;
    C_MissionLoader loader;
    loader.Begin("s2", nullptr);
    const char *const path = "missions\\s2\\scene2.bin";

    // --- the file: none (3), another chunk (0), a header that does not read (1) ---
    {
        g_msNamed.clear();
        g_files.erase(path);
        g_gfLog.clear();
        WM(r.s2File, loader.LoadScene2() == 3 && GfLogIs({"tyres", "surfaces"}));
        g_files[path] = MsChunkFile(0x1234, {});
        g_gfLog.clear();
        WM(r.s2File, loader.LoadScene2() == 0 && GfVecIs(MsLog(), MsScene2Prologue()));
        g_files[path] = {0x53, 0x4C};
        g_gfLog.clear();
        WM(r.s2File, loader.LoadScene2() == 1);
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, {});
        WM(r.s2File, loader.LoadScene2() == 0);
    }

    // --- the scene's camera made without one ---
    {
        I3D_frame *const old = CmActive();
        CmActive() = nullptr;
        g_cmCreate.clear();
        I3D_frame *made = CmCamera("newcam", S_vector(0.0f, 0.0f, 0.0f));
        g_cmCreate.push_back(made);
        g_files[path] = MsChunkFile(0x1234, {});
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> want = {
            "tyres", "surfaces", "enum 4ffff", "create 3", "active newcam", "addframe newcam",
            "worlddir newcam -1.00000 -0.10000 -1.00000 0.000000", "update newcam", "release newcam", "ortho newcam 0",
            "range newcam 0.1 500.0", "fov newcam 1.3963", "active newcam", "update newcam"};
        const S_vector &at = made->m_localMatrix(3);
        WM(r.s2Camera, GfVecIs(MsLog(), want) && at.x == 10.0f && at.y == 5.0f && at.z == 10.0f && CmActive() == made);
        CmActive() = old;
    }

    // --- the object chunk's entries ---
    {
        I3D_frame *lm = GfVisual("lm1", I3DVISUAL_LMAP);
        WmField<I3D_mesh *>(lm, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x1234);
        I3D_frame *obj = GfVisual("obj1", I3DVISUAL_OBJECT);
        WmField<I3D_mesh *>(obj, 0x1E0) = nullptr;
        I3D_frame *plain = GfFrame(FRAME_DUMMY, "plain");
        WmField<I3D_mesh *>(plain, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x5678);
        g_msNamed = {lm, obj, plain};
        g_plScene.clear();
        g_plScene["scenefound"] = GfFrame(FRAME_DUMMY, "scenefound");
        g_plScene["plain"] = plain;
        g_msDriverFrames.clear();
        g_msDriverFrames["driverfound"] = GfFrame(FRAME_DUMMY, "driverfound");
        g_lsStates[1] = 0;
        g_tshiftModule.m_entries.clear();
        game->m_unknown66 = game->m_unknown68 = 0;
        const uint32_t shadow[11] = {0x3FA00000, 0x40200000, 0x41F00000, 128, 256, 0x3DCCCCCD, 0x3E4CCCCD, 0x3E99999A, 7, 8, 9};
        const std::vector<uint8_t> body =
            MsChunk(1, {0x34, 0x12, 0x78, 0x56}) + MsChunk(0x3011, MsValue(300.0f)) + MsChunk(0x3010, {1, 2}) +
            MsChunk(0x3013, MsValue(7.0f)) + MsChunk(0x3012, {}) + MsChunk(0x3200, MsValue(1.0f) + MsValue(2.0f) + MsValue(3.0f)) +
            MsChunk(0x3211, MsValue(0.05f) + MsValue(7.0f)) + MsChunk(0x3211, MsValue(0.5f) + MsValue(2.0f)) +
            MsChunk(0x7777, {9, 9}) +
            MsChunk(0x4000, MsObject(1) + MsChunk(0x4011, {9}) + MsObject(2)) +
            MsChunk(0xAE00, MsShift(5, 6, "lm1") + MsShift(7, 8, "obj1") + MsShift(1, 1, "nothere") + MsShift(2, 2, "plain") +
                                MsChunk(0xAE02, {})) +
            MsChunk(0xAE20, MsActor(MsChunk(0xAE23, MsText("plain")) + MsChunk(0xAE22, MsValue(7)) + MsChunk(0xAE24, {9})) +
                                MsActor(MsChunk(0xAE23, MsText("scenefound")) + MsChunk(0xAE22, MsValue(8))) +
                                MsActor(MsChunk(0xAE23, MsText("driverfound")) + MsChunk(0xAE22, MsValue(9))) +
                                MsActor(MsChunk(0xAE22, MsValue(99)) + MsChunk(0xAE24, {5})) + MsChunk(0xAE2F, {})) +
            MsChunk(0xAE40, {0x41, 0x42}) + MsChunk(0xB004, {1}) + MsChunk(0xB004, {0}) +
            MsChunk(0xB600, MsValue(0x1234)) + MsChunk(0xB650, MsValue(77)) + MsChunk(0xB700, MsBytes(shadow, sizeof shadow)) +
            MsChunk(0xB810, MsChunk(0x100, MsText("grp")) + MsChunk(0x110, MsText("f1"))) +
            MsChunk(0xB820, MsChunk(0xB820, MsText("plain")) + MsChunk(0xB820, MsText("nothere")) + MsChunk(0x1111, {}) +
                                MsChunk(0xB820, MsText("after"))) +
            MsChunk(0xB840, MsValue(3)) + MsChunk(0xB880, MsValue(4));
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, body);
        g_gfLog.clear();
        const int result = loader.LoadScene2();
        std::vector<std::string> want = MsScene2Prologue();
        for (const char *l : {"ortho mscam 1", "update mscam", "e4 0.100 7.000", "e4 0.500 2.000", "object 1", "object 2",
                              "createactor 7 plain 1 -1", "setframe 7 plain", "props 7 9", "sfind scenefound cffff",
                              "createactor 8 scenefound 1 -1", "setframe 8 scenefound", "dfind driverfound",
                              "createactor 9 driverfound 1 -1", "setframe 9 driverfound", "destroyactor 9 1",
                              "createactor 99 - 1 -1", "cpnet walk 65", "cpnet roads 66", "getstate 1", "setstate 1 1",
                              "getstate 1", "rstate 12 4660", "sfind plain ffff", "s128 plain"})
            want.push_back(l);
        const std::vector<std::string> log = MsLog();
        WM(r.s2List, result == 0 && GfVecIs(log, want));
        WM(r.s2Settings, game->m_unknown66 == 0x1234 && game->m_unknown68 == 0x5678 && game->m_fov == 300.0f &&
                             game->m_nearClip == 0.1f && game->m_farClip == 300.0f &&
                             WmField<float>(game->GetScene(), 0x210) == 1.0f && WmField<float>(game->GetScene(), 0x214) == 2.0f &&
                             WmField<float>(game->GetScene(), 0x218) == 3.0f);
        const std::vector<S_TShiftEntry> &shifts = g_tshiftModule.m_entries;
        WM(r.s2TShift, shifts.size() == 1 && shifts[0].mesh == reinterpret_cast<I3D_mesh *>(0x1234) && shifts[0].frame == lm &&
                           shifts[0].on && !shifts[0].reverse && shifts[0].unknown0C == 5 && shifts[0].unknown10 == 6 &&
                           shifts[0].unknown14 == 0 && shifts[0].unknown18 == 0);
        WM(r.s2States, game->m_unknownD8 == 77);
        const S_ShadowParams *params = GetGEffects()->GetShadowParams();
        WM(r.s2Shadows, params->maxHeight == 1.25f && params->depth == 2.5f && params->range == 30.0f && params->width == 128 &&
                            params->height == 256 && params->color.x == 0.1f && params->color.y == 0.2f &&
                            params->color.z == 0.3f && params->unknown10[0] == 7 && params->unknown10[1] == 8 &&
                            params->unknown10[2] == 9);
        const S_LightmapGroup *group = GetLightMapGroups()->FindGroup("grp");
        WM(r.s2Groups, group && group->frames.size() == 1 && group->frames[0] == C_str("f1"));
        WM(r.s2B820, std::find(log.begin(), log.end(), "s128 after") == log.end());
        WM(r.s2Music, game->m_music == 3 && game->m_alarmMusic == 4);
        // A frame looked up stays (null): a second actor of an unknown name asks the scene again.
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS,
                                    MsChunk(0xAE20, MsActor(MsChunk(0xAE23, MsText("nothere")) + MsChunk(0xAE22, MsValue(8)))));
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> again = MsLog();
        WM(r.s2Actors, std::find(again.begin(), again.end(), "dfind nothere") != again.end() &&
                           std::find(again.begin(), again.end(), "createactor 8 - 1 -1") != again.end());
        // The actor's frame from the names the scene enumerated (a sector too).
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS,
                                    MsChunk(0xAE20, MsActor(MsChunk(0xAE23, MsText("sector1")) + MsChunk(0xAE22, MsValue(8)))));
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> sector = MsLog();
        WM(r.s2Actors, std::find(sector.begin(), sector.end(), "createactor 8 sector1 1 -1") != sector.end() &&
                           std::find(sector.begin(), sector.end(), "dfind sector1") == sector.end());
        // Short entries: their values as they start (the first of their kind).
        game->m_fov = 9.0f;
        game->m_unknownD8 = 9;
        WmField<float>(game->GetScene(), 0x210) = 9.0f;
        g_lsStates[1] = 1;
        I3D_frame *empty = GfVisual("", I3DVISUAL_LMAP);
        WmField<I3D_mesh *>(empty, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x999);
        const std::string a255(255, 'a');
        I3D_frame *longName = GfVisual(a255, I3DVISUAL_LMAP);
        WmField<I3D_mesh *>(longName, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x888);
        g_msNamed = {lm, empty, longName};
        g_tshiftModule.m_entries.clear();
        const int len256 = 256, len300 = 300;
        g_files[path] = MsChunkFile(
            MISSION_CHUNK_OBJECTS,
            MsChunk(0x3010, {1, 2}) + MsChunk(0x3200, {1}) + MsChunk(0xB004, {}) + MsChunk(0xB600, {1}) + MsChunk(0xB650, {1}) +
                MsChunk(0xAE00, MsChunk(0xAE01, {}) + MsChunk(0xAE01, MsValue(9) + MsValue(9) + MsValue(0) +
                                                                 std::vector<uint8_t>(255, 'b')) +
                                    MsChunk(0xAE01, MsValue(3) + MsValue(4) + MsValue(len256) +
                                                                 std::vector<uint8_t>(256, 'a')) +
                                    MsChunk(0xAE01, MsValue(5) + MsValue(6) + MsValue(len300) + std::vector<uint8_t>(300, 'a'))) +
                MsChunk(0xAE20, MsActor(MsChunk(0xAE22, {})) +
                                    MsActor(MsChunk(0xAE23, MsText("driverfound")) + MsChunk(0xAE22, MsValue(9)) + MsChunk(0xAE24, {3}))));
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> shortLog = MsLog();
        const std::vector<S_TShiftEntry> &entries = g_tshiftModule.m_entries;
        WM(r.s2Settings, game->m_fov == 0.0f && WmField<float>(game->GetScene(), 0x210) == 0.0f && game->m_unknownD8 == 0);
        WM(r.s2States, std::find(shortLog.begin(), shortLog.end(), "setstate 1 0") != shortLog.end() &&
                           std::find(shortLog.begin(), shortLog.end(), "rstate 12 0") != shortLog.end());
        WM(r.s2TShift, entries.size() == 4 && entries[0].frame == empty && entries[0].unknown0C == 0 && entries[0].unknown10 == 0 &&
                           entries[1].frame == empty && entries[1].unknown0C == 9 && entries[2].frame == longName &&
                           entries[2].unknown0C == 3 && entries[3].frame == longName && entries[3].unknown0C == 5);
        WM(r.s2Actors, std::find(shortLog.begin(), shortLog.end(), "createactor 0 - 1 -1") != shortLog.end() &&
                           std::none_of(shortLog.begin(), shortLog.end(),
                                        [](const std::string &l) { return l.compare(0, 5, "props") == 0; }) &&
                           std::find(shortLog.begin(), shortLog.end(), "destroyactor 9 1") != shortLog.end());
        g_msNamed = {lm, obj, plain};
        // An entry list whose header does not read leaves the object chunk too: the end (loaded).
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0xB820, {1, 2}));
        WM(r.s2B820, loader.LoadScene2() == 0);
        // A matching driver state: not set.
        g_lsStates[1] = 1;
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0xB004, {1}));
        g_gfLog.clear();
        loader.LoadScene2();
        WM(r.s2States, std::find(g_gfLog.begin(), g_gfLog.end(), "setstate 1 1") == g_gfLog.end());
        g_lsStates[1] = 0;
    }

    // --- the progress: the file's position over its size (stage 1, the second table) ---
    {
        const std::vector<uint8_t> pad(100, 0), pad10(10, 0), pad40(40, 0);
        const std::vector<uint8_t> file =
            MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0x7777, pad) + MsChunk(0x4000, MsChunk(0x4011, pad10) + MsChunk(0x4011, pad40)));
        g_files[path] = file;
        g_loaderScreen.m_progress = 0.0f;
        loader.LoadScene2();
        // (The list's second entry, before its header.)
        const float last = 0.25f * (static_cast<float>(6 + 106 + 6 + 16) / static_cast<float>(file.size())) + 0.15f;
        WM(r.s2Progress, g_loaderScreen.m_progress == last && file.size() == 180);
        const std::vector<uint8_t> tail = MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0x7777, pad) + MsChunk(0x7778, pad40));
        g_files[path] = tail;
        g_loaderScreen.m_progress = 0.0f;
        loader.LoadScene2();
        WM(r.s2Progress, g_loaderScreen.m_progress == 0.25f * (118.0f / static_cast<float>(tail.size())) + 0.15f);
    }

    // --- failures: an object, an entry's header, an actor's part's header ---
    {
        const std::vector<std::vector<uint8_t>> bodies = {
            MsChunk(0x4000, MsObject(0xFF)), MsChunk(0x3010, MsValue(1.0f)) + std::vector<uint8_t>{1, 2},
            MsChunk(0xAE20, MsActor({1, 2})), MsChunk(0xAE00, {1, 2}), MsChunk(0x4000, {1, 2})};
        for (const std::vector<uint8_t> &body : bodies) {
            g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, body);
            WM(r.s2Fail, loader.LoadScene2() == 1);
        }
    }

    g_loaderScreen.Finish();
    g_msNamed.clear();
    g_plScene.clear();
    std::printf("scene2: s2File %d s2Camera %d s2Settings %d s2List %d s2TShift %d s2Actors %d s2States %d s2Shadows %d "
                "s2Groups %d s2B820 %d s2Music %d s2Progress %d s2Fail %d\n",
                r.s2File, r.s2Camera, r.s2Settings, r.s2List, r.s2TShift, r.s2Actors, r.s2States, r.s2Shadows, r.s2Groups,
                r.s2B820, r.s2Music, r.s2Progress, r.s2Fail);
}

// --- part 8f-iv: scene2.bin's objects -----------------------------------------------------------

struct S_ObjectResults {
    bool moName = true, moCreate = true, moPlace = true, moParent = true, moModel = true, moCamera = true;
    bool moLight = true, moLightOld = true, moBox = true, moSound = true, moMesh = true, moVisual = true;
    bool moLightmaps = true, moProjector = true, moB110 = true, moSector = true, moFail = true;
};

int g_moHandle;
// One object (chunk 0x4010 of `parts`) read; the log from its start.
bool MoLoad(C_MissionLoader &loader, const std::vector<uint8_t> &parts)
{
    g_files["mo.bin"] = MsChunkFile(0x4010, parts);
    C_chunk chunk;
    chunk.Open("mo.bin", false);
    uint16_t id = 0;
    chunk.Descend(id);
    g_moHandle = chunk.GetHandle();
    g_gfLog.clear();
    const bool ok = loader.LoadObject(chunk);
    chunk.Close();
    return ok;
}
std::vector<uint8_t> MoName(const std::string &name) { return MsChunk(0x0010, MsText(name.c_str())); }
std::vector<uint8_t> MoType(int type) { return MsChunk(0x4011, MsValue(type)); }
template <class... T> std::vector<uint8_t> MoFloats(T... f)
{
    std::vector<uint8_t> out;
    for (float v : {static_cast<float>(f)...})
        out = out + MsValue(v);
    return out;
}
bool MoHas(const std::string &line) { return std::find(g_gfLog.begin(), g_gfLog.end(), line) != g_gfLog.end(); }
// The frame's `name` in the loader's names.
I3D_frame *MoNamed(C_MissionLoader &loader, const char *name)
{
    const auto it = loader.m_frames.find(C_str(name));
    return it == loader.m_frames.end() ? nullptr : it->second;
}

// The last stub frame named `name`.
I3D_frame *MoLastNamed(const char *name)
{
    for (auto it = g_frames.rbegin(); it != g_frames.rend(); ++it)
        if ((*it)->name == name)
            return (*it)->frame;
    return nullptr;
}

void MsObjectTest()
{
    S_ObjectResults r;
    C_game *game = GetGame();
    g_moQuiet = false;
    C_MissionLoader loader;
    void *const oldSlots[] = {g_sceneVtable[0x84 / 4], g_sceneVtable[0xEC / 4], g_igraphVtable[0x3C / 4]};
    g_sceneVtable[0x84 / 4] = reinterpret_cast<void *>(&F_MoRemoveFrame);
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_MoFrameSector);
    g_igraphVtable[0x3C / 4] = reinterpret_cast<void *>(&F_MoLoadTexture);
    const uint32_t oldRole = g_gameRole;
    g_gameRole = GAME_ROLE_SERVER;
    game->m_unknown66 = 7;

    // --- the frame: by its name (type 0), else a new one by the type ---
    {
        I3D_frame *exist = MoFrame(FRAME_DUMMY, "exist");
        loader.m_frames[C_str("exist")] = exist;
        WM(r.moName, MoLoad(loader, MoName("exist")) && GfLogIs({"update exist", "update exist"}));
        // Not named: none (the name stays, null).
        WM(r.moName, MoLoad(loader, MoName("nobody")) && g_gfLog.empty() && loader.m_frames.count(C_str("nobody")) == 1);
        // A name taken: not made, the frame none.
        WM(r.moName, MoLoad(loader, MoType(6) + MoName("exist")) && g_gfLog.empty());
        // The type after the name: the scene's frame, then nothing new.
        WM(r.moName, MoLoad(loader, MoName("exist") + MoType(6) + MoName("other")) &&
                         GfLogIs({"cframe 6", "object other", "addframe other", "release other", "update other",
                                  "fsector other 0.0 0.0 0.0", "update other"}));
        // Made: named, added, released, in the names (by its name), placed in its sector.
        g_moMade = 0;
        WM(r.moCreate, MoLoad(loader, MoType(6) + MoName("d1")) &&
                           GfLogIs({"cframe 6", "object d1", "addframe d1", "release d1", "update d1",
                                    "fsector d1 0.0 0.0 0.0", "update d1"}) &&
                           MoNamed(loader, "d1") && MoNamed(loader, "d1")->GetType() == FRAME_DUMMY);
        // The types made: 2, 4, 6, 8, 9, 12, 14 by CreateFrame, 1 by CreateVisual, 3 a camera.
        for (int type : {2, 4, 8, 9, 12, 14}) {
            const std::string n = CmText("t%d", type);
            WM(r.moCreate, MoLoad(loader, MoType(type) + MoName(n.c_str())) && g_gfLog.size() > 2 &&
                               g_gfLog[0] == CmText("cframe %d", type) && MoNamed(loader, n.c_str()) &&
                               MoNamed(loader, n.c_str())->GetType() == type);
        }
        for (int type : {5, 7, 10, 11, 13})
            WM(r.moCreate, MoLoad(loader, MoType(type) + MoName(CmText("u%d", type))) && g_gfLog.empty());
        // (Another type keeps the last frame: named again, added again.)
        WM(r.moCreate, MoLoad(loader, MoType(6) + MoName("k1") + MoType(5) + MoName("k2")) &&
                           GfLogIs({"cframe 6", "object k1", "addframe k1", "release k1", "object k2", "addframe k2",
                                    "release k2", "update k2", "fsector k2 0.0 0.0 0.0", "update k2"}) &&
                           MoNamed(loader, "k2") == MoNamed(loader, "k1"));
        WM(r.moCreate, MoLoad(loader, MoType(1) + MsChunk(0x4012, MsValue(I3DVISUAL_LMAP)) + MoName("vis")) &&
                           g_gfLog.size() > 2 && g_gfLog[0] == "cvisual 50414d4c" && MoNamed(loader, "vis") &&
                           MoNamed(loader, "vis")->GetVisualType() == I3DVISUAL_LMAP);
        WM(r.moCreate, MoLoad(loader, MoType(1) + MsChunk(0x4012, MsValue(0xDEAD)) + MoName("novis")) &&
                           GfLogIs({"cvisual dead"}));
        WM(r.moCreate, MoLoad(loader, MoType(1) + MoName("vis0")) && g_gfLog.size() > 1 && g_gfLog[0] == "cvisual 0");
        g_cmCreate.clear();
        g_cmCreate.push_back(CmCamera("cam1", S_vector(0.0f, 0.0f, 0.0f)));
        WM(r.moCreate, MoLoad(loader, MoType(3) + MoName("cam1")) && g_gfLog.size() > 2 && g_gfLog[0] == "create 3" &&
                           g_gfLog[1] == "fov cam1 1.3963" && MoNamed(loader, "cam1") && MoHas("addframe cam1"));
        // The name's own frame name (a '%' in it: as text).
        WM(r.moCreate, MoLoad(loader, MoType(6) + MoName("p%d")) && MoNamed(loader, "p%d"));
    }

    // --- the place: the position, the rotation (the file's order: s, x, y, z), the scale, hidden ---
    {
        WM(r.moPlace, MoLoad(loader, MoType(6) + MoName("pl") + MsChunk(0x0020, MoFloats(1, 2, 3)) +
                                         MsChunk(0x0022, MoFloats(0.5f, 0.5f, 0.5f, 0.5f)) + MsChunk(0x002D, MoFloats(2, 3, 4)) +
                                         MsChunk(0x4033, {})));
        I3D_frame *pl = MoNamed(loader, "pl");
        const S_vector &p = pl->m_localMatrix(3);
        WM(r.moPlace, p.x == 1.0f && p.y == 2.0f && p.z == 3.0f && pl->m_scale.x == 2.0f && pl->m_scale.z == 4.0f &&
                          GfLogIs({"cframe 6", "object pl", "addframe pl", "release pl", "update pl",
                                   "fsector pl 1.0 2.0 3.0", "on pl 0", "update pl"}));
        WM(r.moPlace, pl->m_rot.s == 0.5f && pl->m_rot.v.x == 0.5f);
        WM(r.moPlace, MoLoad(loader, MoType(6) + MoName("pl2") + MsChunk(0x0022, MoFloats(1, 2, 3, 4))));
        I3D_frame *pl2 = MoNamed(loader, "pl2");
        // (SetRot normalizes: s 1, then x, y, z.)
        const float n = std::sqrt(30.0f);
        WM(r.moPlace, std::fabs(pl2->m_rot.s - 1.0f / n) < 1e-6f && std::fabs(pl2->m_rot.v.x - 2.0f / n) < 1e-6f &&
                          std::fabs(pl2->m_rot.v.z - 4.0f / n) < 1e-6f && pl2->m_scale.x == 1.0f);
        // Short entries: the defaults (the position untouched, no rotation).
        WM(r.moPlace, MoLoad(loader, MoType(6) + MoName("pl3") + MsChunk(0x0020, MoFloats(1)) + MsChunk(0x0022, MoFloats(1)) +
                                         MsChunk(0x002D, MoFloats(1))));
        I3D_frame *pl3 = MoNamed(loader, "pl3");
        WM(r.moPlace, pl3->m_localMatrix(3).x == 0.0f && pl3->m_rot.s == 1.0f && pl3->m_rot.v.x == 0.0f &&
                          pl3->m_scale.x == 1.0f && pl3->m_scale.y == 1.0f);
        // Hidden: the entry left (the next ones read).
        WM(r.moPlace, MoLoad(loader, MoType(6) + MoName("pl4") + MsChunk(0x4033, {}) + MsChunk(0x0020, MoFloats(6, 6, 6))) &&
                          MoNamed(loader, "pl4")->m_localMatrix(3).x == 6.0f && MoHas("on pl4 0"));
        // The scene's frame: its position kept without one.
        I3D_frame *kept = MoFrame(FRAME_DUMMY, "kept");
        GfPlace(kept, S_vector(7.0f, 8.0f, 9.0f));
        loader.m_frames[C_str("kept")] = kept;
        WM(r.moPlace, MoLoad(loader, MoName("kept") + MsChunk(0x002C, MoFloats(1, 1, 1))) && kept->m_localMatrix(3).x == 7.0f &&
                          GfLogIs({"update kept", "update kept"}));
    }

    // --- the parent: linked by its name; without it the position without one ---
    {
        I3D_frame *par = MoFrame(FRAME_DUMMY, "par");
        loader.m_frames[C_str("par")] = par;
        const auto parent = [](const char *name) { return MsChunk(0x4020, MoName(name)); };
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch") + MsChunk(0x0020, MoFloats(1, 2, 3)) + parent("par")) &&
                           GfLogIs({"cframe 6", "object ch", "addframe ch", "release ch", "update ch", "link ch par 0",
                                    "update ch", "update ch"}) &&
                           MoNamed(loader, "ch")->GetParent() == par);
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch2") + MsChunk(0x0020, MoFloats(1, 2, 3)) +
                                          MsChunk(0x002C, MoFloats(4, 5, 6)) + parent("nopar")) &&
                           MoNamed(loader, "ch2")->m_localMatrix(3).x == 4.0f && MoHas("fsector ch2 4.0 5.0 6.0"));
        // The position without one before the place's: the place's.
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch3") + MsChunk(0x002C, MoFloats(4, 5, 6)) + parent("nopar") +
                                          MsChunk(0x0020, MoFloats(1, 2, 3))) &&
                           MoNamed(loader, "ch3")->m_localMatrix(3).x == 1.0f);
        // Without the position without one: the place's.
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch4") + MsChunk(0x0020, MoFloats(1, 2, 3)) + parent("nopar")) &&
                           MoNamed(loader, "ch4")->m_localMatrix(3).x == 1.0f);
        // No parent's position alone: not placed.
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch5") + MsChunk(0x002C, MoFloats(4, 5, 6)) + parent("nopar")) &&
                           MoNamed(loader, "ch5")->m_localMatrix(3).x == 0.0f);
        // Found then not found: linked to none (not linked), its sector by its place.
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch6") + parent("par") + parent("nopar")) &&
                           GfLogIs({"cframe 6", "object ch6", "addframe ch6", "release ch6", "update ch6",
                                    "fsector ch6 0.0 0.0 0.0", "update ch6", "update ch6"}));
        // ... with a parent already: none.
        I3D_frame *had = MoFrame(FRAME_DUMMY, "had", 0, par);
        loader.m_frames[C_str("had")] = had;
        WM(r.moParent, MoLoad(loader, MoName("had") + parent("par") + parent("nopar")) &&
                           GfLogIs({"update had", "update had", "update had"}));
        // Another entry inside: left (the next entries the object's); no frame: the entry skipped.
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch7") + MsChunk(0x4020, MsChunk(0x0011, MsText("par"))) +
                                          MsChunk(0x0020, MoFloats(5, 5, 5))) &&
                           MoNamed(loader, "ch7")->GetParent() == nullptr && MoNamed(loader, "ch7")->m_localMatrix(3).x == 5.0f);
        WM(r.moParent, MoLoad(loader, parent("par") + MoName("exist")) && GfLogIs({"update exist", "update exist"}));
        // A name of the parent's that is not the first: the first one's.
        WM(r.moParent, MoLoad(loader, MoType(6) + MoName("ch9") + MsChunk(0x4020, MoName("par") + MoName("x"))) &&
                           MoNamed(loader, "ch9")->GetParent() == par);
    }

    // --- a model's file: opened (its frames named, its shadow), none: removed ---
    {
        g_cacheFill = &MoFill;
        g_cacheLoads.clear();
        g_gameRole = GAME_ROLE_SERVER | GAME_ROLE_CLIENT;
        WM(r.moModel, MoLoad(loader, MoType(9) + MoName("m1") + MsChunk(0x2012, MsText("models\\car.v2.i3d"))) &&
                          g_cacheLoads.size() == 1 && g_cacheLoads[0] == "models\\car.v2" && MoNamed(loader, "part") &&
                          MoHas("menum m1 ffff") && MoHas("shadowcache s"));
        g_gameRole = GAME_ROLE_SERVER;
        g_cacheFailName = "gone";
        g_dbgPrints.clear();
        WM(r.moModel, MoLoad(loader, MoType(9) + MoName("m2") + MsChunk(0x2012, MsText("gone"))) &&
                          g_dbgPrints.size() == 1 && g_dbgPrints[0] == "# ERROR LOADER : modelfile 'gone' not found" &&
                          !MoHas("menum m2 ffff"));
        g_cacheFailName.clear();
        // None: out of the names and the scene (the object left without a frame).
        WM(r.moModel, MoLoad(loader, MoType(9) + MoName("m3") + MsChunk(0x2012, MsText("")) + MsChunk(0x4033, {})) &&
                          GfLogIs({"cframe 9", "object m3", "addframe m3", "release m3", "rmframe m3", "release m3"}) &&
                          loader.m_frames.count(C_str("m3")) == 1 && !MoNamed(loader, "m3"));
        // (Its reference taken across the removal: none lost.)
        WM(r.moModel, MoLastNamed("m3") && MoLastNamed("m3")->m_refCount == 0);
        // Not a model: skipped (the file's name not read).
        g_cacheLoads.clear();
        WM(r.moModel, MoLoad(loader, MoType(6) + MoName("m4") + MsChunk(0x2012, MsText("car")) + MoName("m5")) &&
                          g_cacheLoads.empty() && MoNamed(loader, "m4"));
        WM(r.moModel, MoLoad(loader, MsChunk(0x2012, MsText("car")) + MoName("exist")) && g_cacheLoads.empty());
        // Animated: the game's list.
        I3D_frame *anim = MoFrame(FRAME_MODEL, "anim1");
        static S_GfAnim set = {g_gfAnimVt, 100};
        g_gfAnims[anim] = &set;
        loader.m_frames[C_str("anim1")] = anim;
        WM(r.moModel, MoLoad(loader, MoName("anim1")) && GfLogIs({"update anim1", "update anim1", "animated anim1"}));
        g_gfAnims.erase(anim);
        WM(r.moModel, MoLoad(loader, MoName("anim1")) && !MoHas("animated anim1"));
        g_cacheFill = nullptr;
    }

    // --- a camera's field of view and far clip ---
    {
        I3D_frame *cam = CmCamera("cam2", S_vector(0.0f, 0.0f, 0.0f));
        loader.m_frames[C_str("cam2")] = cam;
        WM(r.moCamera, MoLoad(loader, MoName("cam2") + MsChunk(0x4036, MsChunk(0x3010, MoFloats(1.5f)) +
                                                                           MsChunk(0x3011, MoFloats(300)))) &&
                           GfLogIs({"fov cam2 1.5000", "update cam2", "range cam2 0.1 300.0", "update cam2", "update cam2"}));
        // Another entry: not left (the next read inside it; the camera's entry left for it, the rest
        // the object's: a header there that does not read fails it); a header that does not read:
        // skipped.
        WM(r.moCamera, MoLoad(loader, MoName("cam2") + MsChunk(0x4036, MsChunk(0x3012, MsChunk(0x3010, MoFloats(2.0f))))) &&
                           GfLogIs({"fov cam2 2.0000", "update cam2", "update cam2", "update cam2"}));
        WM(r.moCamera, !MoLoad(loader, MoName("cam2") + MsChunk(0x4036, MsChunk(0x3012, MsChunk(0x3010, MoFloats(2.0f))) +
                                                                            std::vector<uint8_t>{1, 2})));
        WM(r.moCamera, MoLoad(loader, MoName("cam2") + MsChunk(0x4036, MsChunk(0x3010, MoFloats(3.0f)) + std::vector<uint8_t>{1, 2})) &&
                           GfLogIs({"fov cam2 3.0000", "update cam2", "update cam2", "update cam2"}));
        // Short: the last value read (the object's start: 0).
        WM(r.moCamera, MoLoad(loader, MoName("cam2") + MsChunk(0x4036, MsChunk(0x3011, {1}))) && MoHas("range cam2 0.1 0.0"));
        WM(r.moCamera, MoLoad(loader, MoName("cam2") + MsChunk(0x4036, MsChunk(0x3010, MoFloats(2.5f)) + MsChunk(0x3011, {1}))) &&
                           MoHas("range cam2 0.1 2.5"));
        WM(r.moCamera, MoLoad(loader, MoName("exist") + MsChunk(0x4036, MsChunk(0x3010, MoFloats(2.0f))) + MoName("cam2")) &&
                           GfLogIs({"update cam2", "update cam2"}));
    }

    // --- a light (scene version 7): out of its sectors, its parts, into the sector named ---
    {
        I3D_frame *light = MoFrame(FRAME_LIGHT, "l1");
        I3D_frame *s1 = MoFrame(FRAME_SECTOR, "s1"), *s2 = MoFrame(FRAME_SECTOR, "s2");
        loader.m_frames[C_str("l1")] = light;
        loader.m_frames[C_str("s2")] = s2;
        loader.m_frames[C_str("notsector")] = MoFrame(FRAME_DUMMY, "notsector");
        g_moLightSectors[light] = {reinterpret_cast<I3D_sector *>(s1), reinterpret_cast<I3D_sector *>(s2)};
        MoSyncSectors(light);
        const std::vector<uint8_t> parts =
            MsChunk(0x4041, MsValue(2)) + MsChunk(0x4042, MoFloats(1.5f, 2.5f, 3.5f)) +
            MsChunk(0x0026, MoFloats(0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f)) + MsChunk(0x4043, MoFloats(0.5f, 1.0f)) +
            MsChunk(0x4044, MoFloats(10, 1, 2, 3, 0, 0)) + MsChunk(0x4045, MsValue(0x11)) + MsChunk(0x4046, MsText("s2")) +
            MsChunk(0x4046, MsText("notsector")) + MsChunk(0x4046, MsText("nobody")) + MsChunk(0x4099, {1, 2, 3});
        WM(r.moLight, MoLoad(loader, MoName("l1") + MsChunk(0x4040, parts)) &&
                          GfLogIs({"removelight s1 l1", "removelight s2 l1", "ltype l1 2", "lpower l1 1.5000", "lspow l1 2.5000",
                                   "lapow l1 3.5000", "lcolor l1 0.10 0.20 0.30", "lspec l1 0.40 0.50 0.60",
                                   "lamb l1 0.70 0.80 0.90", "lcone l1 0.50 1.00", "lrange l1 10.0000",
                                   "latten l1 1.00000000 2.00000000 3.00000000", "lmode l1 17", "addlight s2 l1",
                                   "update l1", "update l1", "update l1"}) &&
                          g_moLightSectors[light].size() == 1);
        // Version 6: the power and the colour alone, the range's second value, the default
        // attenuation unless linear-free and quadratic; the modes' bits below 5 and 6.
        game->m_unknown66 = 6;
        WmField<float>(light, 0x180) = 0.0f;
        WmField<float>(light, 0x184) = 0.5f;
        WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4042, MoFloats(1.5f, 2.5f)) +
                                                                          MsChunk(0x0026, MoFloats(0.1f, 0.2f, 0.3f, 0.4f)) +
                                                                          MsChunk(0x4044, MoFloats(5, 6)) +
                                                                          MsChunk(0x4045, MsValue(1)))) &&
                             GfLogIs({"removelight s2 l1", "lpower l1 1.5000", "lcolor l1 0.10 0.20 0.30", "lrange l1 6.0000",
                                      "lmode l1 1", "update l1", "update l1", "update l1"}));
        for (const float a1 : {1.0f, -1.0f}) {
            WmField<float>(light, 0x180) = a1;
            WmField<float>(light, 0x184) = 0.5f;
            WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4044, MoFloats(5, 6)))) &&
                                 MoHas("latten l1 0.99009901 0.00000000 0.00990099"));
        }
        for (const float a2 : {0.0f, -0.5f, std::nanf("")}) {
            WmField<float>(light, 0x180) = 0.0f;
            WmField<float>(light, 0x184) = a2;
            WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4044, MoFloats(5, 6)))) &&
                                 MoHas("latten l1 0.99009901 0.00000000 0.00990099"));
        }
        WmField<float>(light, 0x180) = std::nanf("");
        WmField<float>(light, 0x184) = 0.5f;
        WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4044, MoFloats(5, 6)))) &&
                             MoHas("latten l1 0.99009901 0.00000000 0.00990099"));
        game->m_unknown66 = 5;
        WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4045, MsValue(1)))) && MoHas("lmode l1 33"));
        game->m_unknown66 = 4;
        WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4045, MsValue(1)))) && MoHas("lmode l1 97"));
        game->m_unknown66 = 7;
        WM(r.moLightOld, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4045, MsValue(1)))) && MoHas("lmode l1 1"));
        // Short parts: the last values read.
        WM(r.moLight, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4042, MoFloats(4.0f)) + MsChunk(0x4043, MoFloats(1)) +
                                                                          MsChunk(0x0026, MoFloats(1)) + MsChunk(0x4041, {1}))) &&
                          GfLogIs({"lpower l1 4.0000", "lspow l1 4.0000", "lapow l1 4.0000", "lcone l1 0.00 0.00",
                                   "lcolor l1 0.00 0.00 0.00", "lspec l1 0.00 0.00 0.00", "lamb l1 0.00 0.00 0.00",
                                   "ltype l1 0", "update l1", "update l1", "update l1"}));
        // Not a light: skipped; a part's header that does not read: the object fails.
        WM(r.moLight, MoLoad(loader, MoName("exist") + MsChunk(0x4040, MsChunk(0x4041, MsValue(2))) + MsChunk(0x4033, {})) &&
                          GfLogIs({"update exist", "on exist 0", "update exist"}));
        WM(r.moLight, MoLoad(loader, MoName("l1") + MsChunk(0x4040, MsChunk(0x4041, MsValue(2))) + MsChunk(0x4033, {})) &&
                          MoHas("on l1 0"));
        WM(r.moLight, !MoLoad(loader, MoName("l1") + MsChunk(0x4040, {1, 2})));
    }

    // --- a dummy's box ---
    {
        I3D_frame *dummy = MoFrame(FRAME_DUMMY, "box");
        loader.m_frames[C_str("box")] = dummy;
        WM(r.moBox, MoLoad(loader, MoName("box") + MsChunk(0x4050, MoFloats(1, 2, 3, 4, 5, 6))));
        const I3D_bbox &b = static_cast<I3D_dummy *>(dummy)->GetBBox();
        WM(r.moBox, b.min.x == 1.0f && b.min.z == 3.0f && b.max.x == 4.0f && b.max.z == 6.0f);
        // Short: the last box read in the object; another type: not set.
        WM(r.moBox, MoLoad(loader, MoName("box") + MsChunk(0x4050, MoFloats(7, 8, 9, 10, 11, 12)) + MsChunk(0x4050, MoFloats(1))) &&
                        b.min.x == 7.0f && b.max.z == 12.0f);
        WM(r.moBox, MoLoad(loader, MoName("box") + MsChunk(0x4050, MoFloats(1))) && b.min.x == 0.0f && b.max.z == 0.0f);
        I3D_frame *user = MoFrame(FRAME_USER, "user1");
        loader.m_frames[C_str("user1")] = user;
        WmField<float>(user, 0x120) = 5.0f;
        WM(r.moBox, MoLoad(loader, MoName("user1") + MsChunk(0x4050, MoFloats(1, 2, 3, 4, 5, 6)) + MoName("box")) &&
                        WmField<float>(user, 0x120) == 5.0f && b.min.x == 0.0f);
    }

    // --- a sound: off and not looping first, its parts, its file, its sector and area ---
    {
        I3D_frame *sound = MoFrame(FRAME_SOUND, "snd");
        loader.m_frames[C_str("snd")] = sound;
        loader.m_frames[C_str("s2")] = MoNamed(loader, "s2");
        loader.m_frames[C_str("area1")] = MoFrame(FRAME_AREA, "area1");
        const std::vector<uint8_t> parts =
            MsChunk(0x4061, MsValue(3)) + MsChunk(0x4062, MoFloats(0.5f)) + MsChunk(0x4063, MoFloats(0.25f)) +
            MsChunk(0x4064, MoFloats(1, 2)) + MsChunk(0x4065, MoFloats(3, 4, 5)) + MsChunk(0x4068, MoFloats(6, 7, 8, 9)) +
            MsChunk(0x4066, {}) + MsChunk(0x4067, {}) + MsChunk(0x0010, MsText("birds")) +
            MsChunk(0x0010, MsText("sea.ogg")) + MsChunk(0xB300, MsText("area1")) + MsChunk(0xB200, MsText("s2")) +
            MsChunk(0xB200, MsText("area1")) + MsChunk(0xB800, MoFloats(1.25f)) +
            MsChunk(0x4099, {1});
        g_dbgPrints.clear();
        WM(r.moSound, MoLoad(loader, MoName("snd") + MsChunk(0x4060, parts)) && g_dbgPrints.empty() &&
                          GfLogIs({"sloop snd 0", "on snd 0", "stype snd 3", "svol snd 0.5000", "sout snd 0.2500",
                                   "scone snd 1.00 2.00", "srange snd 3.00 4.00 5.00 0.00", "srange snd 6.00 7.00 8.00 9.00",
                                   "sloop snd 1", "sopen snd birds.wav 10", "sopen snd sea.ogg 10", "addsound area1 snd",
                                   "addsound s2 snd", "sfreq snd 1.2500", "on snd 1", "update snd", "update snd"}));
        g_dbgPrints.clear();
        WM(r.moSound, MoLoad(loader, MoName("snd") + MsChunk(0x4060, MsChunk(0x0010, MsText("missing.wav")))) &&
                          GfLogIs({"sloop snd 0", "on snd 0", "sopen snd missing.wav 10", "on snd 0", "update snd",
                                   "update snd"}) &&
                          g_dbgPrints.size() == 1 && g_dbgPrints[0] == "# ERROR LOADER : soundfile 'missing.wav' not found");
        // An area's entry naming a sector: none.
        WM(r.moSound, MoLoad(loader, MoName("snd") + MsChunk(0x4060, MsChunk(0xB300, MsText("s2")))) &&
                          GfLogIs({"sloop snd 0", "on snd 0", "on snd 0", "update snd", "update snd"}));
        // Open above 0: loaded; the entry left; the first value short: 0.
        g_dbgPrints.clear();
        WM(r.moSound, MoLoad(loader, MoName("snd") + MsChunk(0x4060, MsChunk(0x0010, MsText("one.wav"))) + MsChunk(0x4033, {})) &&
                          g_dbgPrints.empty() && MoHas("sopen snd one.wav 10") && g_gfLog.size() == 7);
        WM(r.moSound, MoLoad(loader, MoName("snd") + MsChunk(0x4060, MsChunk(0x4062, {1}))) && MoHas("svol snd 0.0000"));
        // Short parts: the last values (0 first).
        WM(r.moSound, MoLoad(loader, MoName("snd") + MsChunk(0x4060, MsChunk(0x4062, MoFloats(0.75f)) + MsChunk(0x4063, {1}) +
                                                                          MsChunk(0x4065, MoFloats(1)) + MsChunk(0x4064, {1}) +
                                                                          MsChunk(0x4068, {1}) + MsChunk(0xB800, {1}) +
                                                                          MsChunk(0x4061, {1}))) &&
                          GfLogIs({"sloop snd 0", "on snd 0", "svol snd 0.7500", "sout snd 0.7500",
                                   "srange snd 0.00 0.00 0.00 0.00", "scone snd 0.00 0.00", "srange snd 0.00 0.00 0.00 0.00",
                                   "sfreq snd 0.7500", "stype snd 0", "on snd 0", "update snd", "update snd"}));
        WM(r.moSound, MoLoad(loader, MoName("exist") + MsChunk(0x4060, MsChunk(0x4067, {})) + MoName("snd")) &&
                          GfLogIs({"update snd", "update snd"}));
        WM(r.moSound, !MoLoad(loader, MoName("snd") + MsChunk(0x4060, {1})));
    }

    // --- an occluder's and an area's meshes ---
    {
        loader.m_frames[C_str("occ")] = MoFrame(FRAME_OCCLUDER, "occ");
        const std::vector<uint8_t> verts = MsValue(2) + MoFloats(1, 2, 3, 4, 5, 6);
        const std::vector<uint8_t> faces = MsValue(2) + MsValue(static_cast<uint16_t>(0)) + MsValue(static_cast<uint16_t>(1)) +
                                           MsValue(static_cast<uint16_t>(1)) + MsValue(static_cast<uint16_t>(1)) +
                                           MsValue(static_cast<uint16_t>(0)) + MsValue(static_cast<uint16_t>(9));
        WM(r.moMesh, MoLoad(loader, MoName("occ") + MsChunk(0x4080, MsChunk(0x4083, MsValue(0)) + MsChunk(0x4082, verts) +
                                                                       MsChunk(0x4083, faces))) &&
                         GfLogIs({"build occ 0 0 16 v -", "build occ 2 2 16 v 1.0 2.0 3.0 0.0 / 4.0 5.0 6.0 0.0 t 0 9",
                                  "update occ", "update occ"}));
        // Another entry: not left (the next read inside it).
        WM(r.moMesh, MoLoad(loader, MoName("occ") + MsChunk(0x4080, MsChunk(0x4084, MsChunk(0x4083, MsValue(0))))) &&
                         GfLogIs({"build occ 0 0 16 v -", "update occ", "update occ"}));
        // One vertex, one face.
        WM(r.moMesh, MoLoad(loader, MoName("occ") + MsChunk(0x4080, MsChunk(0x4082, MsValue(1) + MoFloats(7, 8, 9)) +
                                                                       MsChunk(0x4083, MsValue(1) + MsValue(static_cast<uint16_t>(3)) +
                                                                                           MsValue(static_cast<uint16_t>(4)) +
                                                                                           MsValue(static_cast<uint16_t>(5))))) &&
                         GfLogIs({"build occ 1 1 16 v 7.0 8.0 9.0 0.0 / 7.0 8.0 9.0 0.0 t 3 5", "update occ", "update occ"}));
        // Short: no faces.
        WM(r.moMesh, MoLoad(loader, MoName("occ") + MsChunk(0x4080, MsChunk(0x4083, {1}))) &&
                         GfLogIs({"build occ 0 0 16 v -", "update occ", "update occ"}));
        loader.m_frames[C_str("area2")] = MoFrame(FRAME_AREA, "area2");
        WM(r.moMesh, MoLoad(loader, MoName("area2") + MsChunk(0xB150, MsChunk(0xB170, verts) + MsChunk(0xB180, faces))) &&
                         GfLogIs({"build area2 2 2 16 v 1.0 2.0 3.0 0.0 / 4.0 5.0 6.0 0.0 t 0 9", "update area2",
                                  "update area2"}));
        // Not theirs: skipped (each by its own type).
        WM(r.moMesh, MoLoad(loader, MoName("area2") + MsChunk(0x4080, MsChunk(0x4083, MsValue(0))) + MoName("occ") +
                                        MsChunk(0xB150, MsChunk(0xB180, MsValue(0)))) &&
                         GfLogIs({"update occ", "update occ"}));
        WM(r.moMesh, !MoLoad(loader, MoName("occ") + MsChunk(0x4080, {1})));
        WM(r.moMesh, !MoLoad(loader, MoName("area2") + MsChunk(0xB150, {1})));
    }

    // --- a visual's type: another one takes its place ---
    {
        I3D_frame *holder = MoFrame(FRAME_DUMMY, "holder");
        I3D_frame *old = MoFrame(FRAME_VISUAL, "old", I3DVISUAL_OBJECT, holder);
        I3D_frame *kid1 = MoFrame(FRAME_DUMMY, "kid1", 0, old);
        MoFrame(FRAME_DUMMY, "kid2", 0, old);
        loader.m_frames[C_str("old")] = old;
        g_moMade = 0;
        WM(r.moVisual, MoLoad(loader, MoName("old") + MsChunk(0x4090, MsValue(I3DVISUAL_LMAP))) &&
                           GfLogIs({"cvisual 50414d4c", "dup new1 old", "link new1 holder 0", "link kid1 new1 0",
                                    "link kid2 new1 0", "update new1", "link old 0 0", "rmframe old", "addframe new1",
                                    "release new1", "update new1", "update new1"}) &&
                           MoNamed(loader, "old") && MoNamed(loader, "old")->GetVisualType() == I3DVISUAL_LMAP &&
                           kid1->GetParent() == MoNamed(loader, "old"));
        // Under a model: the model's frames.
        I3D_frame *model = MoFrame(FRAME_MODEL, "mdl");
        I3D_frame *mid = MoFrame(FRAME_DUMMY, "mid", 0, model);
        I3D_frame *vis = MoFrame(FRAME_VISUAL, "mv", I3DVISUAL_OBJECT, mid);
        loader.m_frames[C_str("mv")] = vis;
        WM(r.moVisual, MoLoad(loader, MoName("mv") + MsChunk(0x4090, MsValue(I3DVISUAL_LMAP))) &&
                           GfLogIs({"cvisual 50414d4c", "dup new2 mv", "link new2 mid 0", "update new2", "link mv 0 0",
                                    "rmmf mdl mv", "addmf mdl new2", "release new2", "update new2", "update new2"}));
        // The same type, none made, the copy failing; not a visual.
        WM(r.moVisual, MoLoad(loader, MoName("mv") + MsChunk(0x4090, MsValue(I3DVISUAL_LMAP))) &&
                           GfLogIs({"update new2", "update new2"}));
        WM(r.moVisual, MoLoad(loader, MoName("mv") + MsChunk(0x4090, MsValue(0xDEAD))) &&
                           GfLogIs({"cvisual dead", "update new2", "update new2"}));
        g_moDupResult = -1;
        WM(r.moVisual, MoLoad(loader, MoName("mv") + MsChunk(0x4090, MsValue(I3DVISUAL_OBJECT))) &&
                           GfLogIs({"cvisual 5f4a424f", "dup new3 new2", "release new3", "update new2", "update new2"}));
        g_moDupResult = 1;
        WM(r.moVisual, MoLoad(loader, MoName("mv") + MsChunk(0x4090, MsValue(I3DVISUAL_OBJECT))) && MoHas("addmf mdl new4"));
        g_moDupResult = 0;
        WM(r.moVisual, MoLoad(loader, MoName("exist") + MsChunk(0x4090, MsValue(I3DVISUAL_LMAP))) &&
                           GfLogIs({"update exist", "update exist"}));
        // Short: the object's visual type.
        WM(r.moVisual, MoLoad(loader, MoName("mv") + MsChunk(0x4012, MsValue(I3DVISUAL_PROJECTOR)) + MsChunk(0x4090, {1})) &&
                           MoHas("cvisual 4a4f5250"));
    }

    // --- a lightmapped visual's sets (from scene version 3) ---
    {
        loader.m_frames[C_str("lmv")] = MoFrame(FRAME_VISUAL, "lmv", I3DVISUAL_LMAP);
        loader.m_frames[C_str("objv")] = MoFrame(FRAME_VISUAL, "objv", I3DVISUAL_OBJECT);
        game->m_unknown66 = 3;
        WM(r.moLightmaps, MoLoad(loader, MoName("lmv") + MsChunk(0x40A0, {1, 2, 3}) + MsChunk(0x4033, {})) &&
                              GfLogIs({CmText("lmsets lmv %d", g_moHandle), "update lmv", "on lmv 0", "update lmv"}));
        game->m_unknown66 = 2;
        WM(r.moLightmaps, MoLoad(loader, MoName("lmv") + MsChunk(0x40A0, {1, 2, 3})) && GfLogIs({"update lmv", "update lmv"}));
        game->m_unknown66 = 7;
        WM(r.moLightmaps, MoLoad(loader, MoName("objv") + MsChunk(0x40A0, {1, 2, 3})) && GfLogIs({"update objv", "update objv"}));
        WM(r.moLightmaps, MoLoad(loader, MsChunk(0x40A0, {1, 2, 3}) + MoName("lmv")) && GfLogIs({"update lmv", "update lmv"}));
    }

    // --- a projector: its parts, its texture ---
    {
        loader.m_frames[C_str("proj")] = MoFrame(FRAME_VISUAL, "proj", I3DVISUAL_PROJECTOR);
        const std::vector<uint8_t> parts =
            MsChunk(0x40F1, MoFloats(22)) + MsChunk(0x40F2, MoFloats(0.5f)) + MsChunk(0x40F3, MoFloats(0.1f, 0.2f, 0.3f)) +
            MsChunk(0x40F4, MsValue(3)) + MsChunk(0x40F5, MsValue(1)) + MsChunk(0x40F6, {1}) + MsChunk(0x40F7, MsText("hole.bmp")) +
            MsChunk(0x40F7, MsText("missing.bmp")) + MsChunk(0x40F7, MsText("null.bmp")) + MsChunk(0x40F9, {1});
        I3D_frame *proj = MoNamed(loader, "proj");
        WM(r.moProjector, MoLoad(loader, MoName("proj") + MsChunk(0x40F0, parts)) &&
                              GfLogIs({"pint proj 0.5000", "pcolor proj 0.10 0.20 0.30", "pmode proj 3", "pbuf proj 1",
                                       "portho proj 1", "texture hole.bmp 0 0", "ptex proj 1", "update proj", "preproj proj 0 0",
                                       "update proj", "texture missing.bmp 0 0", "texture null.bmp 0 0", "update proj",
                                       "update proj"}) &&
                              WmField<float>(proj, 0x1E4) == 22.0f && g_images.back()->name == "hole.bmp" &&
                              g_images.back()->refs == 0);
        // Short parts: the last values (0 first).
        WM(r.moProjector, MoLoad(loader, MoName("proj") + MsChunk(0x40F0, MsChunk(0x40F2, MoFloats(0.25f)) + MsChunk(0x40F1, {1}) +
                                                                              MsChunk(0x40F3, {1}) + MsChunk(0x40F4, MsValue(5)) +
                                                                              MsChunk(0x40F5, {1}) + MsChunk(0x40F6, {}))) &&
                              GfLogIs({"pint proj 0.2500", "pcolor proj 0.00 0.00 0.00", "pmode proj 5", "pbuf proj 5",
                                       "portho proj 0", "update proj", "update proj"}) &&
                              WmField<float>(proj, 0x1E4) == 0.25f);
        WM(r.moProjector, MoLoad(loader, MoName("lmv") + MsChunk(0x40F0, MsChunk(0x40F2, MoFloats(0.5f)))) &&
                              GfLogIs({"update lmv", "update lmv"}));
        WM(r.moProjector, MoLoad(loader, MsChunk(0x40F0, MsChunk(0x40F2, MoFloats(0.5f))) + MoName("lmv")) &&
                              GfLogIs({"update lmv", "update lmv"}));
        WM(r.moProjector, !MoLoad(loader, MoName("proj") + MsChunk(0x40F0, {1})));
    }

    // --- chunk 0xB110: six fields (any frame) ---
    {
        I3D_frame *flare = MoFrame(FRAME_VISUAL, "flare", I3DVISUAL_LENSFLARE);
        loader.m_frames[C_str("flare")] = flare;
        const std::vector<uint8_t> parts = MsChunk(0xB112, MoFloats(1)) + MsChunk(0xB113, MoFloats(2)) +
                                           MsChunk(0xB114, MoFloats(3.75f)) + MsChunk(0xB115, MoFloats(4)) +
                                           MsChunk(0xB116, MoFloats(5)) + MsChunk(0xB117, MoFloats(6));
        WM(r.moB110, MoLoad(loader, MoName("flare") + MsChunk(0xB110, parts)) && WmField<float>(flare, 0x1E4) == 1.0f &&
                         WmField<float>(flare, 0x1E0) == 2.0f && WmField<int>(flare, 0x1EC) == 3 &&
                         WmField<float>(flare, 0x220) == 4.0f && WmField<float>(flare, 0x224) == 5.0f &&
                         WmField<float>(flare, 0x240) == 6.0f && GfLogIs({"update flare", "update flare"}));
        WM(r.moB110, MoLoad(loader, MoName("flare") + MsChunk(0xB110, MsChunk(0xB114, MoFloats(-2.75f)))) &&
                         WmField<int>(flare, 0x1EC) == -2);
        // Another entry: not left (the next read inside it; the rest the object's); short: the last
        // value.
        WM(r.moB110, MoLoad(loader, MoName("flare") + MsChunk(0xB110, MsChunk(0xB118, MsChunk(0xB112, MoFloats(7))) +
                                                                          MsChunk(0xB113, MoFloats(8)))) &&
                         WmField<float>(flare, 0x1E4) == 7.0f && WmField<float>(flare, 0x1E0) == 2.0f);
        WM(r.moB110, MoLoad(loader, MoName("flare") + MsChunk(0xB110, MsChunk(0xB112, MoFloats(9)) + MsChunk(0xB113, {1}))) &&
                         WmField<float>(flare, 0x1E4) == 9.0f && WmField<float>(flare, 0x1E0) == 9.0f);
        // A header that does not read: the last entry's id again (its value read again: none).
        WM(r.moB110, MoLoad(loader, MoName("flare") + MsChunk(0xB110, MsChunk(0xB116, MoFloats(8)) + std::vector<uint8_t>{9, 0, 0, 65})) &&
                         WmField<float>(flare, 0x224) == 8.0f);
        WM(r.moB110, MoLoad(loader, MsChunk(0xB110, MsChunk(0xB112, MoFloats(10))) + MoName("flare")) &&
                         WmField<float>(flare, 0x1E4) == 9.0f);
    }

    // --- a sector's sound environment ---
    {
        loader.m_frames[C_str("sec")] = MoFrame(FRAME_SECTOR, "sec");
        std::vector<uint8_t> values;
        for (int i = 0; i < 20; ++i)
            values = values + MsValue(100 + i);
        values = values + MoFloats(1, 2, 3, 4, 5, 6);
        std::vector<std::string> want;
        const int order[] = {11, 0, 1, 6, 7, 4, 5, 8, 2, 3, 10, 12, 13, 14, 15, 17, 16, 18, 19, 9};
        for (int i = 0; i < 20; ++i)
            want.push_back(CmText("env sec %d %d", order[i], 100 + i));
        want.push_back("env sec 21 1.0 2.0 3.0");
        want.push_back("env sec 20 4.0 5.0 6.0");
        want.push_back("update sec");
        want.push_back("update sec");
        WM(r.moSector, MoLoad(loader, MoName("sec") + MsChunk(0xB401, values)) && GfVecIs(g_gfLog, want));
        // Short: the last values.
        WM(r.moSector, MoLoad(loader, MoName("sec") + MsChunk(0xB401, MsValue(5))) && g_gfLog.size() == 24 &&
                           g_gfLog[0] == "env sec 11 5" && g_gfLog[1] == "env sec 0 5" && g_gfLog[19] == "env sec 9 5" &&
                           g_gfLog[20] == "env sec 21 0.0 0.0 0.0");
        WM(r.moSector, MoLoad(loader, MoName("exist") + MsChunk(0xB401, values)) && GfLogIs({"update exist", "update exist"}));
        WM(r.moSector, MoLoad(loader, MsChunk(0xB400, values) + MoName("sec") + MsChunk(0x7777, values)) &&
                           GfLogIs({"update sec", "update sec"}));
    }

    // --- failures: a header, the parent's part's ---
    {
        WM(r.moFail, !MoLoad(loader, MoName("exist") + std::vector<uint8_t>{1, 2, 3}));
        WM(r.moFail, !MoLoad(loader, MoName("exist") + MsChunk(0x4020, {1, 2})));
        WM(r.moFail, MoLoad(loader, MsChunk(0x4020, {1, 2})));
        WM(r.moFail, MoLoad(loader, {}) && g_gfLog.empty());
    }

    g_sceneVtable[0x84 / 4] = oldSlots[0];
    g_sceneVtable[0xEC / 4] = oldSlots[1];
    g_igraphVtable[0x3C / 4] = oldSlots[2];
    g_gameRole = oldRole;
    game->m_unknown66 = 0;
    g_moQuiet = true;
    std::printf("objects: moName %d moCreate %d moPlace %d moParent %d moModel %d moCamera %d moLight %d moLightOld %d "
                "moBox %d moSound %d moMesh %d moVisual %d moLightmaps %d moProjector %d moB110 %d moSector %d moFail %d\n",
                r.moName, r.moCreate, r.moPlace, r.moParent, r.moModel, r.moCamera, r.moLight, r.moLightOld, r.moBox,
                r.moSound, r.moMesh, r.moVisual, r.moLightmaps, r.moProjector, r.moB110, r.moSector, r.moFail);
}

}  // namespace
