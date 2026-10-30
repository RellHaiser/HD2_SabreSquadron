// Part 8c-i's GEffects module (WORLD_TEST; game/geffects.cpp), after sight_test.inc. The engine's
// frames, scene, driver and IGraph slots it calls log what they get; the objects of part 8d
// (decorators, volumetrics, mesh player, birds, clouds), the team, the camera module, the head's
// water test, the rays and the particles are faked here. Prints the "geffects:" line.

#include <deque>

#include "game/air_attack.h"
#include "game/collision.h"
#include "game/geffects.h"
#include "game/human_movement.h"
#include "game/interface.h"

namespace {

std::vector<std::string> g_gfLog;

void GfLog(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_gfLog.push_back(text);
}
bool GfLogIs(std::initializer_list<std::string> expected)
{
    const bool same = std::vector<std::string>(expected) == g_gfLog;
    if (!same && g_wmTrace) {
        for (const std::string &l : g_gfLog)
            std::printf("    gflog %s\n", l.c_str());
    }
    return same;
}
bool GfVecIs(const std::vector<std::string> &actual, const std::vector<std::string> &expected)
{
    if (actual != expected && g_wmTrace) {
        for (const std::string &l : actual)
            std::printf("    gfvec %s\n", l.c_str());
    }
    return actual == expected;
}
const char *GfName(const I3D_frame *f)
{
    if (!f)
        return "0";
    auto it = g_stubs.find(f);
    return it == g_stubs.end() ? "?" : it->second->name.c_str();
}

// --- the engine --------------------------------------------------------------------------------

void *g_gfFrameVt[SLOTS], *g_gfLightVt[SLOTS], *g_gfCameraVt[SLOTS], *g_gfModelVt[SLOTS], *g_gfSectorVt[SLOTS];
void *g_gfAnimVt[SLOTS];
std::map<const I3D_frame *, uint32_t> g_gfVisualTypes;
// A model's animation (stage 0): its end and the time played.
struct S_GfAnim {
    void **vtable;
    int end;
};
std::map<const I3D_frame *, S_GfAnim *> g_gfAnims;
std::map<const I3D_frame *, int> g_gfAnimTimes;

uint32_t __stdcall F_GfRelease(I3D_frame *self)
{
    GfLog("release %s", GfName(self));
    return static_cast<uint32_t>(--self->m_refCount);
}
uint32_t __stdcall F_GfVisualType(I3D_frame *self)
{
    auto it = g_gfVisualTypes.find(self);
    return it == g_gfVisualTypes.end() ? 0 : it->second;
}
void __stdcall F_GfUpdate(I3D_frame *self) { GfLog("update %s", GfName(self)); }
void __stdcall F_GfSetOn(I3D_frame *self, bool on)
{
    GfLog("on %s %d", GfName(self), on);
    self->m_frameFlags = on ? self->m_frameFlags | 1 : self->m_frameFlags & ~1u;
}
void __stdcall F_GfLinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    GfLog("link %s %s %u", GfName(self), GfName(parent), flags);
    if (self->m_parent) {
        I3D_frame **link = &self->m_parent->m_firstChild;
        while (*link && *link != self)
            link = &(*link)->m_nextSibling;
        if (*link)
            *link = self->m_nextSibling;
    }
    self->m_parent = parent;
    self->m_nextSibling = nullptr;
    if (parent) {
        I3D_frame **link = &parent->m_firstChild;
        while (*link)
            link = &(*link)->m_nextSibling;
        *link = self;
    }
}
int __stdcall F_GfNumChildren(I3D_frame *self)
{
    int n = 0;
    for (I3D_frame *c = self->m_firstChild; c; c = c->m_nextSibling)
        ++n;
    return n;
}
I3D_frame *__stdcall F_GfFind(I3D_frame *self, const char *name, uint32_t mask)
{
    GfLog("find %s %s %x", GfName(self), name, mask);
    return Find(self, name, mask);
}
void __stdcall F_GfLightLevel(I3D_frame *self, float level) { GfLog("lightlevel %s %.3f", GfName(self), level); }
void __stdcall F_GfHierarchyAlpha(I3D_frame *self, float alpha) { GfLog("halpha %s %.3f", GfName(self), alpha); }
void __stdcall F_GfAlpha(I3D_frame *self, float alpha) { GfLog("alpha %s %.3f", GfName(self), alpha); }
void __stdcall F_GfActive(I3D_frame *self, bool on, bool immediate)
{
    GfLog("active %s %d %d", GfName(self), on, immediate);
}
void __stdcall F_GfTick(I3D_frame *self, int time)
{
    GfLog("tick %s %d", GfName(self), time);
    g_gfAnimTimes[self] += time;
}
void __stdcall F_GfFov(I3D_frame *self, float fov)
{
    GfLog("fov %s %.4f", GfName(self), fov);
    WmField<float>(self, 0x120) = fov;
}
void __stdcall F_GfRange(I3D_frame *self, float nearRange, float farRange)
{
    GfLog("range %s %.1f %.1f", GfName(self), nearRange, farRange);
    WmField<float>(self, 0x124) = nearRange;
    WmField<float>(self, 0x128) = farRange;
}
void __stdcall F_GfPower(I3D_frame *self, float power)
{
    GfLog("power %s %.3f", GfName(self), power);
    WmField<float>(self, 0x168) = power;
}
void __stdcall F_GfMode(I3D_frame *self, uint32_t mode)
{
    GfLog("mode %s %x", GfName(self), mode);
    WmField<uint32_t>(self, 0x160) = mode;
}
void __stdcall F_GfEnvironment(I3D_frame *self, int id) { GfLog("env %s %d", GfName(self), id); }
void *__stdcall F_GfGetAnimation(I3D_frame *self, int stage)
{
    auto it = g_gfAnims.find(self);
    return stage == 0 && it != g_gfAnims.end() ? it->second : nullptr;
}
int __stdcall F_GfAnimTime(I3D_frame *self, int stage) { return stage == 0 ? g_gfAnimTimes[self] : -5000; }
int __stdcall F_GfEndTime(S_GfAnim *self) { return self->end; }

// The scene: frames by name, the weather, the dummies drawn.
std::map<std::string, I3D_frame *> g_gfScene, g_gfDriverFinds;
std::vector<S_dummyBox> g_gfBoxes;
I3D_frame *__stdcall F_GfSceneFind(void *, const char *name, uint32_t mask)
{
    GfLog("sfind %s %x", name, mask);
    auto it = g_gfScene.find(name);
    return it == g_gfScene.end() ? nullptr : it->second;
}
void __stdcall F_GfWeather(void *, int param, uint32_t value) { GfLog("weather %d %u", param, value); }
int __stdcall F_GfNumDummies(void *) { return static_cast<int>(g_gfBoxes.size()); }
const S_dummyBox *__stdcall F_GfDummies(void *) { return g_gfBoxes.data(); }

// The driver: its states, new models, the frames it finds; IGraph: the screen, render states, the
// quad drawn.
uint32_t g_gfDriverState3, g_gfRenderState14;
std::vector<I3D_frame *> g_gfCreated;
S_TLVertex g_gfQuad[4];
void __stdcall F_GfSetState(void *, int state, uint32_t value) { GfLog("dset %d %u", state, value); }
uint32_t __stdcall F_GfGetState(void *, int state)
{
    GfLog("dget %d", state);
    return state == 3 ? g_gfDriverState3 : 0;
}
I3D_frame *F_GfNewFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent, void **vtable);
I3D_frame *__stdcall F_GfCreateFrame(void *, int type)
{
    GfLog("create %d", type);
    g_gfCreated.push_back(F_GfNewFrame(static_cast<E_I3D_FRAME_TYPE>(type), "anim", nullptr, g_gfModelVt));
    g_gfCreated.back()->m_localMatrix(3) = S_vector(9.0f, 9.0f, 9.0f);
    g_gfCreated.back()->m_rot = S_quat(0.5f, 0.5f, 0.5f, 0.5f);
    return g_gfCreated.back();
}
I3D_frame *__stdcall F_GfDriverFind(void *, I3D_scene *, const char *name)
{
    GfLog("dfind %s", name);
    auto it = g_gfDriverFinds.find(name);
    return it == g_gfDriverFinds.end() ? nullptr : it->second;
}
int __stdcall F_GfScrnX(void *) { return 640; }
int __stdcall F_GfScrnY(void *) { return 480; }
void __stdcall F_GfSetTexture(void *, void *texture, int stage) { GfLog("tex %d %d", texture != nullptr, stage); }
void __stdcall F_GfSetRenderState(void *, int state, int value) { GfLog("rset %d %d", state, value); }
int __stdcall F_GfGetRenderState(void *, int state)
{
    GfLog("rget %d", state);
    return state == 14 ? static_cast<int>(g_gfRenderState14) : 0;
}
void __stdcall F_GfDraw(void *, int kind, int count, const void *vertices, int transformed)
{
    GfLog("draw %d %d %d", kind, count, transformed);
    std::memcpy(g_gfQuad, vertices, sizeof g_gfQuad);
}

I3D_frame *F_GfNewFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent, void **vtable)
{
    I3D_frame *f = NewFrame(type, name, parent);
    *reinterpret_cast<void ***>(f) = vtable;
    f->m_name = g_stubs[f]->name.c_str();
    return f;
}
I3D_frame *GfFrame(E_I3D_FRAME_TYPE type, const std::string &name, I3D_frame *parent = nullptr)
{
    void **vtable = type == FRAME_LIGHT ? g_gfLightVt : type == FRAME_CAMERA ? g_gfCameraVt
                    : type == FRAME_MODEL ? g_gfModelVt : type == FRAME_SECTOR ? g_gfSectorVt : g_gfFrameVt;
    return F_GfNewFrame(type, name, parent, vtable);
}
I3D_frame *GfVisual(const std::string &name, uint32_t visualType, I3D_frame *parent = nullptr)
{
    I3D_frame *f = GfFrame(FRAME_VISUAL, name, parent);
    g_gfVisualTypes[f] = visualType;
    return f;
}
void GfPlace(I3D_frame *f, const S_vector &pos)
{
    f->m_localMatrix(3) = pos;
    f->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
}

void GfInitVtables()
{
    for (int i = 0; i < SLOTS; ++i)
        g_gfFrameVt[i] = g_frameVtable[i];
    g_gfFrameVt[0x00 / 4] = reinterpret_cast<void *>(&F_GfRelease);
    g_gfFrameVt[0x04 / 4] = reinterpret_cast<void *>(&F_GfVisualType);
    g_gfFrameVt[0x24 / 4] = reinterpret_cast<void *>(&F_GfUpdate);
    g_gfFrameVt[0x30 / 4] = reinterpret_cast<void *>(&F_GfSetOn);
    g_gfFrameVt[0x38 / 4] = reinterpret_cast<void *>(&F_GfLinkTo);
    g_gfFrameVt[0x3C / 4] = reinterpret_cast<void *>(&F_GfNumChildren);
    g_gfFrameVt[0x48 / 4] = reinterpret_cast<void *>(&F_GfFind);
    g_gfFrameVt[0x50 / 4] = reinterpret_cast<void *>(&F_GfLightLevel);
    g_gfFrameVt[0x54 / 4] = reinterpret_cast<void *>(&F_GfHierarchyAlpha);
    g_gfFrameVt[0xA8 / 4] = reinterpret_cast<void *>(&F_GfAlpha);
    g_gfFrameVt[0xAC / 4] = reinterpret_cast<void *>(&F_GfActive);
    for (int i = 0; i < SLOTS; ++i)
        g_gfLightVt[i] = g_gfCameraVt[i] = g_gfModelVt[i] = g_gfSectorVt[i] = g_gfFrameVt[i];
    g_gfLightVt[0x88 / 4] = reinterpret_cast<void *>(&F_GfPower);
    g_gfLightVt[0x94 / 4] = reinterpret_cast<void *>(&F_GfMode);
    g_gfCameraVt[0x74 / 4] = reinterpret_cast<void *>(&F_GfFov);
    g_gfCameraVt[0x7C / 4] = reinterpret_cast<void *>(&F_GfRange);
    g_gfModelVt[0x20 / 4] = reinterpret_cast<void *>(&F_GfTick);
    g_gfModelVt[0x88 / 4] = reinterpret_cast<void *>(&F_GfGetAnimation);
    g_gfModelVt[0xA0 / 4] = reinterpret_cast<void *>(&F_GfAnimTime);
    g_gfSectorVt[0x94 / 4] = reinterpret_cast<void *>(&F_GfEnvironment);
    for (int i = 0; i < SLOTS; ++i)
        g_gfAnimVt[i] = g_frameVtable[0x10 / 4];        // (a trap)
    g_gfAnimVt[0x08 / 4] = reinterpret_cast<void *>(&F_GfEndTime);
}

// --- the rest of the game ----------------------------------------------------------------------

std::vector<C_Human *> g_gfMembers;
C_Human *g_gfActive;
std::map<int, I3D_frame *> g_gfVolumes;
bool g_gfGameCamera = true, g_gfHeadUnder, g_gfMapOpen;
float g_gfSurface;
std::deque<float> g_gfRandoms;
struct S_GfRay {
    S_CollisionVolume *volume;
    S_vector hit;
};
std::deque<S_GfRay> g_gfRays;
alignas(16) uint8_t g_gfCameraEffects[16], g_gfInterface[16];

}  // namespace

// The objects of part 8d.
C_Decorators *g_decorators;
C_Decorators::C_Decorators() {}
C_Decorators::~C_Decorators() {}
void C_Decorators::SetOn(bool on) { GfLog("dec on %d", on); }
void C_Decorators::Clear() { GfLog("dec clear"); }
void C_Decorators::Free() { GfLog("dec free"); }
void C_Decorators::Load() { GfLog("dec load"); }
void C_Decorators::Start() { GfLog("dec start"); }
void C_Decorators::Update(int timeDelta) { GfLog("dec update %d", timeDelta); }
float C_Decorators::GetGroundCover(int material)
{
    GfLog("dec cover %d", material);
    return 0.25f * static_cast<float>(material);
}
C_Volumetrics::C_Volumetrics() {}
C_Volumetrics::~C_Volumetrics() {}
void C_Volumetrics::Render(int time) { GfLog("vol render %d", time); }
void C_Volumetrics::Load() { GfLog("vol load"); }
void C_Volumetrics::Prune(bool unknown) { GfLog("vol prune %d", unknown); }
void C_Volumetrics::Free() { GfLog("vol free"); }
C_MeshPlayer::C_MeshPlayer() {}
C_MeshPlayer::~C_MeshPlayer() {}
void C_MeshPlayer::Load() { GfLog("mesh load"); }
void C_MeshPlayer::Update(int timeDelta) { GfLog("mesh update %d", timeDelta); }
void C_MeshPlayer::Free() { GfLog("mesh free"); }
void UpdateBirds(int timeDelta) { GfLog("birds update %d", timeDelta); }
void CollectBirds() { GfLog("birds collect"); }
void FreeBirds() { GfLog("birds free"); }
void FreeClouds() { GfLog("clouds free"); }
void LoadClouds() { GfLog("clouds load"); }

// RandomFloat: the next of g_gfRandoms (else half of max).
float RandomFloat(float max)
{
    GfLog("rand %.1f", max);
    if (g_gfRandoms.empty())
        return max * 0.5f;
    const float v = g_gfRandoms.front();
    g_gfRandoms.pop_front();
    return v;
}
S_CollisionVolume *RayTestPastBorders(const S_vector4 &from, const S_vector4 &dir, S_vector4 *hitPos, S_vector4 *hitNormal,
                                      uint32_t excludeTypes, uint32_t excludeFlags)
{
    GfLog("ray %.3f %.3f %.3f dir %.1f %.1f %.1f %d %d %x %x", from.x, from.y, from.z, dir.x, dir.y, dir.z, hitPos != nullptr,
          hitNormal != nullptr, excludeTypes, excludeFlags);
    if (g_gfRays.empty())
        return nullptr;
    const S_GfRay ray = g_gfRays.front();
    g_gfRays.pop_front();
    if (hitPos)
        *hitPos = {ray.hit.x, ray.hit.y, ray.hit.z, 1.0f};
    return ray.volume;
}
int C_ParticlesModule::CreateAt(int effect, const S_vector &pos, const S_vector &dir, I3D_frame *parent, bool faceCamera,
                                bool transient)
{
    GfLog("fx %d %.3f %.3f %.3f dir %.1f %.1f %.1f %s %d %d", effect, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z, GfName(parent),
          faceCamera, transient);
    return 0;
}
C_Human *C_Team::GetActivePlayer() const { return g_gfActive; }
int C_TeamBase::GetMemberCount() const { return static_cast<int>(g_gfMembers.size()); }
C_Human *C_TeamBase::GetMember(int index) const { return g_gfMembers.at(static_cast<size_t>(index)); }
I3D_frame *C_Team::GetRandomVolume(int index) const
{
    GfLog("volume %d", index);
    return g_gfVolumes.at(index);
}
C_CameraModule *GetCameraModule()
{
    alignas(16) static uint8_t module[16];
    return reinterpret_cast<C_CameraModule *>(module);
}
bool C_CameraModule::IsGameCamera(I3D_frame *camera)
{
    GfLog("isgame %s", GfName(camera));
    return g_gfGameCamera;
}
void C_CameraModule::SetSpecialMode(int mode, bool on, float height) { GfLog("special %d %d %.3f", mode, on, height); }
C_CameraHumanEffects *g_cameraHumanEffects;
void C_CameraHumanEffects::SetTactical(bool on) { GfLog("tactical %d", on); }
bool C_HumanMovement::GetHeadWater(bool *underWater, float *surface) const
{
    GfLog("headwater");
    *underWater = g_gfHeadUnder;
    *surface = g_gfSurface;
    return g_gfHeadUnder;
}
bool C_CommandPanel::IsOpen() const { return g_gfMapOpen; }

namespace {

struct S_GfResults {
    bool gfModule = true, gfQuad = true, gfFade = true, gfModelFx = true, gfHierarchy = true, gfFades = true;
    bool gfHumans = true, gfStorm = true, gfWater = true, gfUnderwater = true, gfRain = true, gfTeamRain = true;
    bool gfCamera = true, gfMission = true;
};

// The module as after its constructor (the fields the tests read).
void GfClear(C_GEffectsModule &m)
{
    m.Reset();
    m.m_waterCamera = {};
    m.m_waterCameraFrame = nullptr;
    m.m_sun = nullptr;
    m.m_sunPos = S_vector(0.0f, 0.0f, 0.0f);
    m.m_started = false;
    m.m_rain = false;
    m.m_rainRate = 200;
    m.m_rainArea = 8.0f;
    m.m_fadeColor = S_vector(0.0f, 0.0f, 0.0f);
    m.m_fadeLevel = m.m_fadeTarget = m.m_fadeRate = 0.0f;
    m.m_lightning = nullptr;
    m.m_lightningMode = 0;
    m.m_lightningPower = 0.0f;
    m.m_lightningStop = false;
    m.m_lightningTime = -1;
    m.m_time = 0;
    m.m_seaSound = m.m_underwaterSound = m.m_ambientSound = m.m_seaAmbientSound = nullptr;
    g_rainTime = 0;
    g_rainTeamSteps = 3;
    g_gfLog.clear();
}

// A watercam.set: a version chunk, then the block (`size` bytes of it).
void GfWaterFile(const std::string &name, int version, const S_WaterCamera &block, int size)
{
    std::vector<uint8_t> &f = g_files[name];
    f.clear();
    auto put = [&f](const void *p, size_t n) {
        const auto *b = static_cast<const uint8_t *>(p);
        f.insert(f.end(), b, b + n);
    };
    const uint16_t id1 = 0x10, id2 = 0x20;
    const uint32_t size1 = 6 + 4, size2 = 6 + static_cast<uint32_t>(size);
    put(&id1, 2);
    put(&size1, 4);
    put(&version, 4);
    put(&id2, 2);
    put(&size2, 4);
    put(&block, static_cast<size_t>(size));
}

}  // namespace

void GEffectsTest()
{
    S_GfResults r;
    C_GEffectsModule &m = g_geffectsModule;
    static bool once;
    if (!once) {
        once = true;
        GfInitVtables();
        // The constructor's state.
        WM(r.gfModule, GetGEffects() == &m && g_decorators == &m.m_decorators && m.GetId() == MODULE_GEFFECTS &&
                           std::strcmp(m.GetName(), "GEffects") == 0);
        WM(r.gfModule, m.m_rainRate == 200 && m.m_rainArea == 8.0f && !m.m_rain && !m.m_started && m.m_lightningTime == -1 &&
                           m.m_timeScale == 1.0f && m.m_timeScaleTarget == 1.0f && m.m_timeScaleRate == 1.0f);
        WM(r.gfModule, m.m_shadowParams.maxHeight == 1.0f && m.m_shadowParams.depth == 8.0f &&
                           m.m_shadowParams.range == 100.0f && m.m_shadowParams.width == 64 &&
                           m.m_shadowParams.height == 64 && m.m_shadowParams.color.x == 0.0f &&
                           m.m_shadowParams.color.w == 0.0f && !m.m_waterCamera.enabled && g_rainTeamSteps == 3);
        WM(r.gfModule, g_rainTime == 0 && g_waterLevel == 0.0f && m.m_time == 0 && m.m_sunPos.x == 0.0f &&
                           m.m_fadeColor.x == 0.0f && m.m_fadeLevel == 0.0f && m.m_fadeTarget == 0.0f && m.m_fadeRate == 0.0f &&
                           m.m_lightningMode == 0 && m.m_lightningPower == 0.0f && !m.m_lightning && !m.m_unknown220);
    }

    // The engine's slots for the test (put back at the end).
    void *sceneSlots[SCENE_SLOTS], *driverSlots[SLOTS], *igraphSlots[SLOTS];
    std::memcpy(sceneSlots, g_sceneVtable, sizeof sceneSlots);
    std::memcpy(driverSlots, g_driverVtable, sizeof driverSlots);
    std::memcpy(igraphSlots, g_igraphVtable, sizeof igraphSlots);
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_GfSceneFind);
    g_sceneVtable[0x11C / 4] = reinterpret_cast<void *>(&F_GfWeather);
    g_sceneVtable[0x130 / 4] = reinterpret_cast<void *>(&F_GfNumDummies);
    g_sceneVtable[0x134 / 4] = reinterpret_cast<void *>(&F_GfDummies);
    g_driverVtable[0x18 / 4] = reinterpret_cast<void *>(&F_GfSetState);
    g_driverVtable[0x1C / 4] = reinterpret_cast<void *>(&F_GfGetState);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_GfCreateFrame);
    g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_GfDriverFind);
    g_igraphVtable[0x18 / 4] = reinterpret_cast<void *>(&F_GfScrnX);
    g_igraphVtable[0x1C / 4] = reinterpret_cast<void *>(&F_GfScrnY);
    g_igraphVtable[0x44 / 4] = reinterpret_cast<void *>(&F_GfSetTexture);
    g_igraphVtable[0x74 / 4] = reinterpret_cast<void *>(&F_GfSetRenderState);
    g_igraphVtable[0x78 / 4] = reinterpret_cast<void *>(&F_GfGetRenderState);
    g_igraphVtable[0x88 / 4] = reinterpret_cast<void *>(&F_GfDraw);
    uint8_t *scene = reinterpret_cast<uint8_t *>(GetGame()->GetScene());
    I3D_frame *const oldPrimary = WmField<I3D_frame *>(scene, 0x1F8);
    I3D_frame *primary = GfFrame(FRAME_SECTOR, "primary");
    I3D_frame *backdrop = GfFrame(FRAME_SECTOR, "backdrop");
    I3D_frame *omni = GfFrame(FRAME_SECTOR, "omni");
    I3D_frame *flare = GfVisual("lensflare", I3DVISUAL_LENSFLARE, backdrop);
    I3D_frame *sky = GfVisual("sky1", I3DVISUAL_OBJECT, backdrop);
    I3D_frame *cam = GfFrame(FRAME_CAMERA, "cam");
    WmField<float>(cam, 0x120) = 1.2f;
    WmField<float>(cam, 0x124) = 0.5f;
    WmField<float>(cam, 0x128) = 300.0f;
    GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
    WmField<I3D_frame *>(scene, 0x1F8) = primary;
    WmField<I3D_frame *>(scene, 0x1FC) = backdrop;
    WmField<I3D_frame *>(scene, 0x200) = omni;
    WmField<I3D_frame *>(scene, 0x15C) = cam;
    WmField<int>(scene, 0x11C) = 77;
    C_game *game = GetGame();
    game->m_state = GAME_STATE_RUNNING;
    game->m_paused = false;
    game->m_interface = reinterpret_cast<C_Interface *>(g_gfInterface);
    game->m_missionName = "GFX";
    g_cameraHumanEffects = reinterpret_cast<C_CameraHumanEffects *>(g_gfCameraEffects);
    g_gameRole = GAME_ROLE_LOCAL;
    g_gfScene.clear();
    g_gfDriverFinds.clear();
    g_gfBoxes.clear();
    g_gfMembers.clear();
    g_gfActive = nullptr;
    g_gfRandoms.clear();
    g_gfRays.clear();
    g_gfHeadUnder = g_gfMapOpen = false;
    g_gfGameCamera = true;

    // --- the module's small calls ---
    {
        GfClear(m);
        m.m_timeScale = 3.0f;
        m.m_timeScaleTarget = 4.0f;
        m.m_timeScaleRate = 5.0f;
        m.m_unknown220 = true;
        std::memset(m.m_unknown258, 0xFF, sizeof m.m_unknown258);
        const S_vector colorBefore = m.m_fadeColor;
        m.Reset();
        bool cleared = m.m_unknown258[0] == 0xFF && m.m_unknown258[3] == 0xFF;
        for (size_t i = 4; i < sizeof m.m_unknown258; ++i)
            cleared = cleared && m.m_unknown258[i] == 0;
        WM(r.gfModule, cleared && m.m_timeScale == 1.0f && m.m_timeScaleTarget == 1.0f && m.m_timeScaleRate == 1.0f &&
                           !m.m_unknown220 && m.m_fadeColor.x == colorBefore.x);
        C_chunk chunk;
        WM(r.gfModule, m.SaveGame(chunk) && m.LoadGame(chunk) && m.m_time == 2500 && g_gfLog.empty());
        m.SetRain(true);
        WM(r.gfModule, m.m_rain);
        m.SetRain(false);
        WM(r.gfModule, !m.m_rain);
        WM(r.gfModule, m.GetGroundCover(3) == 0.75f && GfLogIs({"dec cover 3"}));
        g_gfLog.clear();
        m.m_time = 1234;
        m.RenderVolumetrics();
        m.SetCloudsOn(true);
        m.SetCloudsOn(false);
        WM(r.gfModule, GfLogIs({"vol render 1234", "clouds load", "clouds free"}));
        g_gfLog.clear();
        m.SetLensFlareOn(false);
        WM(r.gfModule, GfLogIs({"find backdrop lensflare ffff", "on lensflare 0"}));
        g_gfLog.clear();
        m.SetWeatherOn(true);
        g_gameRole = GAME_ROLE_SERVER;
        m.SetWeatherOn(true);
        g_gameRole = GAME_ROLE_CLIENT;
        m.SetWeatherOn(false);
        g_gameRole = GAME_ROLE_LOCAL;
        WM(r.gfModule, GfLogIs({"weather 0 1", "weather 0 0"}));
        m.SetTimeScale(0.5f, -2.0f);
        WM(r.gfModule, m.m_timeScaleTarget == 0.5f && m.m_timeScaleRate == 2.0f && m.GetTimeScale() == 1.0f);
        m.m_timeScale = 0.25f;
        WM(r.gfModule, m.GetTimeScale() == 0.25f);
        // The time scale moves towards its target, down and up, stopping there.
        m.m_timeScale = 1.0f;
        m.Update(100);
        WM(r.gfModule, std::fabs(m.m_timeScale - 0.8f) < 1e-6f);
        m.Update(200);
        WM(r.gfModule, m.m_timeScale == 0.5f);
        m.SetTimeScale(2.0f, 1.0f);
        m.Update(500);
        WM(r.gfModule, m.m_timeScale == 1.0f);
        m.Update(1500);
        WM(r.gfModule, m.m_timeScale == 2.0f);
        m.Update(100);
        WM(r.gfModule, m.m_timeScale == 2.0f);
        m.SetTimeScale(1.0f, 4.0f);
        m.Update(250);
        WM(r.gfModule, m.m_timeScale == 1.0f);
        // Update's own calls without any effect: the birds, the mesh player (the decorators while started).
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfModule, GfLogIs({"birds update 16", "mesh update 16"}));
        g_gfLog.clear();
        m.m_started = true;
        m.Update(17);
        WM(r.gfModule, GfLogIs({"birds update 17", "dec update 17", "mesh update 17"}) && m.m_time == 1234 + 16 + 17 + 100 + 200 +
                                                                                             500 + 1500 + 100 + 250);
    }

    // --- the fade's quad ---
    {
        GfClear(m);
        g_gfDriverState3 = 7;
        g_gfRenderState14 = 0;
        const S_vector color(1.0f, 0.5f, 0.0f);
        m.SetFadeLevel(0.5f, &color);
        WM(r.gfQuad, GfLogIs({"dget 3", "rget 14", "dset 3 0", "rset 14 0", "rset 0 1", "tex 0 0", "draw 5 2 1", "dset 3 1",
                              "rset 14 0"}));
        bool quad = true;
        const float xs[4] = {0, 640, 0, 640}, ys[4] = {0, 0, 480, 480};
        for (int i = 0; i < 4; ++i)
            quad = quad && g_gfQuad[i].x == xs[i] && g_gfQuad[i].y == ys[i] && g_gfQuad[i].color == 0x80FF8000 &&
                   g_gfQuad[i].z == 0.0f && g_gfQuad[i].rhw == 0.0f && g_gfQuad[i].u == 0.0f && g_gfQuad[i].v == 0.0f;
        WM(r.gfQuad, quad);
        g_gfLog.clear();
        g_gfDriverState3 = 0;
        g_gfRenderState14 = 3;
        m.SetFadeLevel(1.0f, nullptr);
        WM(r.gfQuad, GfLogIs({"dget 3", "rget 14", "dset 3 0", "rset 14 0", "rset 0 1", "tex 0 0", "draw 5 2 1", "dset 3 0",
                              "rset 14 1"}) && g_gfQuad[0].color == 0xFF000000 && g_gfQuad[3].color == 0xFF000000);
        const S_vector blue(0.0f, 0.2f, 1.0f);
        m.SetFadeLevel(0.0f, &blue);
        WM(r.gfQuad, g_gfQuad[1].color == 0x000033FF && g_gfQuad[2].color == 0x000033FF);
    }

    // --- the screen fade ---
    {
        GfClear(m);
        m.m_fadeLevel = -1.0f;
        const S_vector red(1.0f, 0.0f, 0.0f);
        m.Fade(500, FADE_OUT, &red);
        WM(r.gfFade, m.m_fadeTarget == 1.0f && m.m_fadeRate == 2.0f && m.m_fadeLevel == 0.0f && m.m_fadeColor.x == 1.0f &&
                         m.m_fadeColor.y == 0.0f && m.m_fadeColor.z == 0.0f);
        m.Update(100);
        WM(r.gfFade, std::fabs(m.m_fadeLevel - 0.2f) < 1e-6f);
        m.Update(500);
        WM(r.gfFade, m.m_fadeLevel == -1.0f && m.m_fadeTarget == 1.0f);
        // Rendering then holds the target.
        m.m_time = 5000;
        g_gfLog.clear();
        m.RenderFade();
        WM(r.gfFade, m.m_fadeLevel == 1.0f && WmLogged("") == 0 && g_gfLog.size() == 9 && g_gfQuad[0].color == 0xFFFF0000);
        m.Update(100);
        WM(r.gfFade, m.m_fadeLevel == 1.0f && m.m_fadeTarget == 1.0f);
        // Back in.
        m.Fade(1000, FADE_IN, nullptr);
        WM(r.gfFade, m.m_fadeTarget == 0.0f && m.m_fadeRate == 1.0f && m.m_fadeLevel == 1.0f && m.m_fadeColor.x == 0.0f);
        m.Update(250);
        WM(r.gfFade, m.m_fadeLevel == 0.75f);
        m.Update(1000);
        WM(r.gfFade, m.m_fadeLevel == -1.0f);
        // Past it without a render between: level and target 0.
        m.m_fadeTarget = 1.0f;
        m.Update(10);
        WM(r.gfFade, m.m_fadeLevel == 0.0f && m.m_fadeTarget == 0.0f);
        // At once (rate FLT_MAX): past the target on the first update.
        m.Fade(0, FADE_OUT, &red);
        WM(r.gfFade, m.m_fadeRate == FLT_MAX && m.m_fadeLevel == 0.0f);
        m.Update(16);
        WM(r.gfFade, m.m_fadeLevel == -1.0f);
        // The level reached exactly is not past it.
        m.Fade(100, FADE_OUT, nullptr);
        m.m_fadeLevel = 0.5f;
        m.Update(50);
        WM(r.gfFade, m.m_fadeLevel == 1.0f);
        m.m_fadeTarget = 0.0f;
        m.m_fadeRate = 4.0f;
        m.Update(250);
        WM(r.gfFade, m.m_fadeLevel == 0.0f);
        // Fade keeps a level of 0 or more.
        m.m_fadeLevel = 0.25f;
        m.Fade(100, FADE_IN, nullptr);
        WM(r.gfFade, m.m_fadeLevel == 0.25f);

        // The start's black screen: 3 s, then gone in 1 s; a higher fade level shows instead.
        auto alphaOf = [&](int time, float level, bool started) {
            m.m_time = time;
            m.m_fadeLevel = level;
            m.m_fadeTarget = level;
            m.m_started = started;
            g_gfLog.clear();
            std::memset(g_gfQuad, 0, sizeof g_gfQuad);
            m.RenderFade();
            return g_gfLog.empty() ? -1 : static_cast<int>(g_gfQuad[0].color >> 24);
        };
        m.m_fadeColor = S_vector(0.0f, 1.0f, 0.0f);
        WM(r.gfFade, alphaOf(0, 0.0f, true) == 255 && g_gfQuad[0].color == 0xFF000000);
        WM(r.gfFade, alphaOf(3000, 0.0f, true) == 255);
        WM(r.gfFade, alphaOf(3500, 0.0f, true) == 128 && g_gfQuad[0].color == 0x80000000);
        WM(r.gfFade, alphaOf(3500, 0.8f, true) == 204 && g_gfQuad[0].color == 0xCC000000);
        WM(r.gfFade, alphaOf(3500, 0.2f, true) == 128);
        WM(r.gfFade, alphaOf(3800, 0.5f, true) == 128);
        WM(r.gfFade, alphaOf(3999, 0.0f, true) == 0);
        WM(r.gfFade, alphaOf(4000, 0.0f, true) == -1);
        WM(r.gfFade, alphaOf(4000, 0.4f, true) == 102 && g_gfQuad[0].color == 0x6600FF00);
        WM(r.gfFade, alphaOf(1000, 0.4f, false) == 102 && g_gfQuad[0].color == 0x6600FF00);
        WM(r.gfFade, alphaOf(1000, 0.0f, false) == -1);
        // A level below 0 takes the target.
        m.m_time = 5000;
        m.m_fadeLevel = -1.0f;
        m.m_fadeTarget = 0.6f;
        m.RenderFade();
        WM(r.gfFade, m.m_fadeLevel == 0.6f);
        m.m_fadeLevel = 0.3f;
        m.RenderFade();
        WM(r.gfFade, m.m_fadeLevel == 0.3f);
        m.m_fadeLevel = -1.0f;
        m.m_fadeTarget = -1.0f;
        g_gfLog.clear();
        m.RenderFade();
        WM(r.gfFade, m.m_fadeLevel == -1.0f && g_gfLog.size() == 9);
    }

    // --- a model's alpha, transparency, light level, visual on / off ---
    {
        GfClear(m);
        I3D_frame *obj = GfVisual("obj", I3DVISUAL_OBJECT);
        WM(r.gfModelFx, !m.SetModelAlpha(nullptr, 0.5f) && m.SetModelAlpha(obj, 0.25f));
        WM(r.gfModelFx, !m.SetModelTransparency(nullptr, 0.5f) && m.SetModelTransparency(obj, 0.25f));
        WM(r.gfModelFx, !m.SetModelLightLevel(nullptr, 0.5f) && m.SetModelLightLevel(obj, 0.4f));
        g_gameRole = GAME_ROLE_SERVER;
        WM(r.gfModelFx, !m.SetModelLightLevel(obj, 0.3f));
        g_gameRole = GAME_ROLE_CLIENT;
        WM(r.gfModelFx, m.SetModelLightLevel(obj, 0.2f));
        g_gameRole = GAME_ROLE_LOCAL;
        WM(r.gfModelFx, !m.ClearModelTransparency(nullptr) && m.ClearModelTransparency(obj));
        m.HideVisual(obj, false);
        m.HideVisual(obj, true);
        WM(r.gfModelFx, GfLogIs({"alpha obj 0.750", "halpha obj 0.750", "lightlevel obj 0.400", "lightlevel obj 0.200",
                                 "halpha obj 1.000", "on obj 0", "update obj", "on obj 1", "update obj"}));
    }

    // --- SetHierarchyOn ---
    {
        GfClear(m);
        I3D_frame *root = GfFrame(FRAME_DUMMY, "root");
        const struct {
            int type;
            const char *name;
            uint32_t bit;
        } kinds[] = {{1, "v", 0x1},       {2, "l", 0x2},       {3, "c", 0x4},       {4, "s", 0x8},       {5, "sec", 0x10},
                     {7, "t", 0x40},      {8, "u", 0x100},     {9, "mdl", 0x80},    {10, "j", 0x400},    {11, "vol", 0x200},
                     {12, "x12", 0x800},  {13, "x13", 0},      {14, "x14", 0x1000}, {15, "x15", 0x2000}, {16, "x16", 0x4000},
                     {17, "x17", 0}};
        std::vector<I3D_frame *> frames;
        for (const auto &k : kinds)
            frames.push_back(GfFrame(static_cast<E_I3D_FRAME_TYPE>(k.type), k.name, root));
        I3D_frame *inner = GfFrame(FRAME_USER, "inner", frames[7]);     // under the model
        // Each bit switches its type only.
        for (const auto &k : kinds) {
            if (!k.bit)
                continue;
            g_gfLog.clear();
            m.SetHierarchyOn(root, true, k.bit);
            std::vector<std::string> expected;
            if (k.bit == 0x100)
                expected = {"on inner 1", "update inner"};
            expected.push_back(std::string("on ") + k.name + " 1");
            if (k.type == FRAME_SOUND)
                expected.push_back(std::string("active ") + k.name + " 1 0");
            expected.push_back(std::string("update ") + k.name);
            WM(r.gfHierarchy, g_gfLog == expected);
        }
        // Every type, the children first, the last child first; the root itself (a dummy).
        g_gfLog.clear();
        m.SetHierarchyOn(root, false, 0xFFFF);
        std::vector<std::string> expected;
        for (size_t i = frames.size(); i-- > 0;) {
            if (!kinds[i].bit)
                continue;
            if (frames[i] == frames[7])
                expected.insert(expected.end(), {"on inner 0", "update inner"});
            expected.push_back(std::string("on ") + kinds[i].name + " 0");
            if (kinds[i].type == FRAME_SOUND)
                expected.push_back(std::string("active ") + kinds[i].name + " 0 0");
            expected.push_back(std::string("update ") + kinds[i].name);
        }
        expected.insert(expected.end(), {"on root 0", "update root"});
        WM(r.gfHierarchy, g_gfLog == expected);
        g_gfLog.clear();
        m.SetHierarchyOn(root, true, 0x20);
        WM(r.gfHierarchy, GfLogIs({"on root 1", "update root"}));
        g_gfLog.clear();
        m.SetHierarchyOn(root, true, 0x8000);
        WM(r.gfHierarchy, g_gfLog.empty() && inner);
    }

    // --- visuals fading out and in ---
    {
        GfClear(m);
        I3D_frame *a = GfVisual("a", I3DVISUAL_OBJECT), *b = GfVisual("b", I3DVISUAL_OBJECT), *c = GfVisual("c", I3DVISUAL_OBJECT);
        m.FadeOutModel(nullptr, 100, true);
        m.FadeIn(nullptr, 100, true);
        WM(r.gfFades, g_gfLog.empty());
        m.FadeOutModel(a, 0, true);
        m.FadeOutModel(a, -5, false);
        WM(r.gfFades, GfLogIs({"alpha a 0.000", "on a 0", "alpha a 0.000"}) && m.m_fadeOuts.empty());
        g_gfLog.clear();
        m.FadeOutModel(a, 400, true);
        WM(r.gfFades, g_gfLog.empty() && m.m_fadeOuts.size() == 1 && m.m_fadeOuts[0]->model == a && m.m_fadeOuts[0]->time == 400 &&
                          m.m_fadeOuts[0]->timeLeft == 400 && m.m_fadeOuts[0]->switchOff && a->m_refCount == 2);
        m.Update(100);
        m.Update(299);
        WM(r.gfFades, m.m_fadeOuts.size() == 1 && m.m_fadeOuts[0]->timeLeft == 1);
        m.Update(1);
        WM(r.gfFades, WmLogged("") == 0 && m.m_fadeOuts.empty() && a->m_refCount == 1);
        {
            std::vector<std::string> fx;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 6, "birds ") && l.compare(0, 12, "mesh update "))
                    fx.push_back(l);
            WM(r.gfFades, GfVecIs(fx, {"alpha a 0.750", "alpha a 0.002", "alpha a 0.000", "on a 0", "release a"}));
        }
        // A switchOff false fade, the time 0 kept (level 0).
        g_gfLog.clear();
        m.FadeOutModel(a, 50, false);
        m.m_fadeOuts[0]->time = 0;
        m.Update(10);
        m.Update(40);
        WM(r.gfFades, WmLogged("") == 0 && std::count(g_gfLog.begin(), g_gfLog.end(), "alpha a 1.000") == 1 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "alpha a 0.000") == 1 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "on a 0") == 0 && m.m_fadeOuts.empty());
        // In.
        g_gfLog.clear();
        m.FadeIn(b, 0, true);
        m.FadeIn(b, 0, false);
        WM(r.gfFades, GfLogIs({"alpha b 1.000", "on b 1", "alpha b 1.000"}) && m.m_fadeIns.empty());
        g_gfLog.clear();
        m.FadeIn(b, 200, false);
        WM(r.gfFades, GfLogIs({"alpha b 0.000"}) && m.m_fadeIns.size() == 1 && b->m_refCount == 2 && m.m_fadeIns[0]->time == 200 &&
                          m.m_fadeIns[0]->timeLeft == 200 && !m.m_fadeIns[0]->switchOff);
        m.FadeIn(b, 300, true);
        WM(r.gfFades, m.m_fadeIns.size() == 2 && m.m_fadeIns[1]->switchOff && b->m_refCount == 3);
        m.m_fadeIns.pop_back();
        --b->m_refCount;
        g_gfLog.clear();
        m.Update(50);
        m.m_fadeIns[0]->time = 0;
        m.Update(100);
        m.Update(50);
        {
            std::vector<std::string> fx;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 6, "birds ") && l.compare(0, 12, "mesh update "))
                    fx.push_back(l);
            WM(r.gfFades, fx == std::vector<std::string>({"alpha b 0.250", "alpha b 0.000", "alpha b 1.000", "release b"}) &&
                              m.m_fadeIns.empty() && b->m_refCount == 1);
        }
        // One fade drops the other (Fix: the record and the reference freed).
        g_gfLog.clear();
        m.FadeIn(c, 200, true);
        m.FadeOutModel(c, 400, false);
        WM(r.gfFades, GfLogIs({"alpha c 0.000", "on c 1", "release c"}) && m.m_fadeIns.empty() && m.m_fadeOuts.size() == 1 &&
                          c->m_refCount == 2);
        g_gfLog.clear();
        m.FadeIn(c, 100, true);
        WM(r.gfFades, GfLogIs({"alpha c 0.000", "release c", "on c 1"}) && m.m_fadeOuts.empty() && m.m_fadeIns.size() == 1 &&
                          c->m_refCount == 2);
        // At once: the other kind's fade goes on.
        g_gfLog.clear();
        m.FadeOutModel(c, 0, false);
        WM(r.gfFades, GfLogIs({"alpha c 0.000"}) && m.m_fadeIns.size() == 1);
        m.FadeIn(a, 0, false);
        m.FadeOutModel(a, 100, false);
        m.FadeIn(a, 0, false);
        WM(r.gfFades, m.m_fadeOuts.size() == 1);
        // Two fades of the same visual; another visual's record is not touched.
        m.FadeIn(b, 100, false);
        m.FadeOutModel(a, 100, false);
        WM(r.gfFades, m.m_fadeOuts.size() == 2 && m.m_fadeIns.size() == 2);
        m.FadeOutModel(c, 200, false);
        WM(r.gfFades, m.m_fadeIns.size() == 1 && m.m_fadeIns[0]->model == b && m.m_fadeOuts.size() == 3);
        m.FadeIn(a, 100, false);
        WM(r.gfFades, m.m_fadeOuts.size() == 2 && m.m_fadeOuts[0]->model == a && m.m_fadeOuts[1]->model == c);
        GfClear(m);
        m.FadeIn(a, 100, false);
        m.FadeIn(b, 100, false);
        m.FadeIn(b, 100, false);
        m.FadeOutModel(b, 100, false);
        WM(r.gfFades, m.m_fadeIns.size() == 2 && m.m_fadeIns[0]->model == a && m.m_fadeIns[1]->model == b);
        m.FadeOutModel(b, 100, false);
        WM(r.gfFades, m.m_fadeIns.size() == 1 && m.m_fadeIns[0]->model == a);
        GfClear(m);
        m.FadeOutModel(a, 100, false);
        m.FadeOutModel(b, 100, false);
        m.FadeOutModel(c, 100, false);
        m.FadeIn(b, 100, false);
        WM(r.gfFades, m.m_fadeOuts.size() == 2 && m.m_fadeOuts[0]->model == a && m.m_fadeOuts[1]->model == c);
        // A finished fade swaps the last one into its place.
        GfClear(m);
        I3D_frame *x = GfVisual("x", I3DVISUAL_OBJECT), *y = GfVisual("y", I3DVISUAL_OBJECT), *z = GfVisual("z", I3DVISUAL_OBJECT);
        m.FadeOutModel(x, 100, false);
        m.FadeOutModel(y, 300, false);
        m.FadeOutModel(z, 300, false);
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfFades, m.m_fadeOuts.size() == 2 && m.m_fadeOuts[0]->model == z && m.m_fadeOuts[1]->model == y);
        WM(r.gfFades, GfLogIs({"alpha z 0.667", "alpha y 0.667", "alpha x 0.000", "release x", "birds update 100", "mesh update 100"}));
        GfClear(m);
        m.FadeIn(x, 100, false);
        m.FadeIn(y, 300, false);
        m.FadeIn(z, 300, false);
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfFades, m.m_fadeIns.size() == 2 && m.m_fadeIns[0]->model == z && m.m_fadeIns[1]->model == y);
        WM(r.gfFades, GfLogIs({"alpha z 0.333", "alpha y 0.333", "alpha x 1.000", "release x", "birds update 100", "mesh update 100"}));

        // The hierarchies: which visuals, the children first, the last first.
        GfClear(m);
        I3D_frame *root = GfFrame(FRAME_DUMMY, "hroot");
        const struct {
            uint32_t type;
            const char *name;
        } visuals[] = {{I3DVISUAL_LMAP, "lmap"},   {I3DVISUAL_BILLBOARD, "bbrd"},  {I3DVISUAL_MORPH, "mrph"},
                       {I3DVISUAL_SINGLEMESH, "sngm"}, {I3DVISUAL_SINGLEMORPH, "smmr"}, {I3DVISUAL_MIRROR, "mirr"},
                       {I3DVISUAL_OBJECT, "obj_"}, {I3DVISUAL_PROJECTOR, "proj"},  {I3DVISUAL_LENSFLARE, "lens"},
                       {I3DVISUAL_EMITTER, "emtr"}, {I3DVISUAL_SHD, "shd"}};
        for (const auto &v : visuals)
            GfVisual(v.name, v.type, root);
        I3D_frame *sub = GfFrame(FRAME_DUMMY, "sub", root);
        GfVisual("deep", I3DVISUAL_OBJECT, sub);
        g_gfVisualTypes[root] = I3DVISUAL_OBJECT;       // (the root itself is faded too)
        m.FadeInModel(root, 0, true);
        WM(r.gfFades, GfLogIs({"alpha deep 1.000", "on deep 1", "alpha obj_ 1.000", "on obj_ 1", "alpha mirr 1.000", "on mirr 1",
                               "alpha smmr 1.000", "on smmr 1", "alpha sngm 1.000", "on sngm 1", "alpha mrph 1.000",
                               "on mrph 1", "alpha bbrd 1.000", "on bbrd 1", "alpha lmap 1.000", "on lmap 1",
                               "alpha hroot 1.000", "on hroot 1"}));
        g_gfLog.clear();
        m.FadeOutHierarchy(root, 0, true);
        WM(r.gfFades, GfLogIs({"alpha deep 0.000", "on deep 0", "alpha obj_ 0.000", "on obj_ 0", "alpha mirr 0.000", "on mirr 0",
                               "alpha smmr 0.000", "on smmr 0", "alpha sngm 0.000", "on sngm 0", "alpha mrph 0.000",
                               "on mrph 0", "alpha bbrd 0.000", "on bbrd 0", "alpha lmap 0.000", "on lmap 0",
                               "alpha hroot 0.000", "on hroot 0"}));
        g_gfLog.clear();
        m.FadeInModel(sub, 300, false);
        m.FadeOutHierarchy(sub, 200, true);
        WM(r.gfFades, GfLogIs({"alpha deep 0.000", "release deep"}) && m.m_fadeIns.empty() && m.m_fadeOuts.size() == 1 &&
                          m.m_fadeOuts[0]->time == 200 && m.m_fadeOuts[0]->switchOff);
        GfClear(m);
    }

    // --- humans fading ---
    {
        GfClear(m);
        auto *h = new C_Human();
        I3D_frame *hm = GfFrame(FRAME_MODEL, "hm");
        I3D_frame *body = GfVisual("a", I3DVISUAL_SINGLEMESH, hm);
        WmField<float>(body, 0x1EC) = 0.6f;
        h->m_frame = hm;
        m.FadeHuman(h, 0, true);
        m.FadeHuman(h, 0, false);
        WM(r.gfHumans, GfLogIs({"halpha hm 1.000", "halpha hm 0.000"}) && m.m_humanFades.empty());
        g_gfLog.clear();
        m.FadeHuman(h, 500, false);
        WM(r.gfHumans, GfLogIs({"find hm a 1"}) && m.m_humanFades.size() == 1 && m.m_humanFades[0].human == h &&
                           !m.m_humanFades[0].in && m.m_humanFades[0].rate == 1.0f / 500.0f && m.m_humanFades[0].alpha == 0.6f);
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfHumans, g_gfLog.size() == 3 && g_gfLog[0] == "halpha hm 0.400" && m.m_humanFades.size() == 1);
        g_gfLog.clear();
        m.Update(250);
        WM(r.gfHumans, g_gfLog[0] == "halpha hm 0.000" && m.m_humanFades.empty());
        // Again for the same human: the record changed (the alpha read from "a" again).
        m.FadeHuman(h, 1000, true);
        WmField<float>(body, 0x1EC) = 0.3f;
        m.FadeHuman(h, 250, false);
        WM(r.gfHumans, m.m_humanFades.size() == 1 && !m.m_humanFades[0].in && m.m_humanFades[0].rate == 1.0f / 250.0f &&
                           m.m_humanFades[0].alpha == 0.3f);
        m.FadeHuman(h, 500, true);
        g_gfLog.clear();
        m.Update(200);
        WM(r.gfHumans, g_gfLog[0] == "halpha hm 0.700" && m.m_humanFades.size() == 1);
        g_gfLog.clear();
        m.Update(150);
        WM(r.gfHumans, g_gfLog[0] == "halpha hm 1.000" && m.m_humanFades.empty());
        // Reaching 1 exactly ends it; 0 exactly too.
        m.FadeHuman(h, 500, true);
        m.m_humanFades[0].alpha = 0.5f;
        g_gfLog.clear();
        m.Update(250);
        WM(r.gfHumans, g_gfLog[0] == "halpha hm 1.000" && m.m_humanFades.empty());
        m.FadeHuman(h, 500, false);
        m.m_humanFades[0].alpha = 0.5f;
        g_gfLog.clear();
        m.Update(250);
        WM(r.gfHumans, g_gfLog[0] == "halpha hm 0.000" && m.m_humanFades.empty());
        // No "a": alpha 1; two humans, the one done erased, the other kept in its order.
        auto *h2 = new C_Human();
        I3D_frame *hm2 = GfFrame(FRAME_MODEL, "hm2");
        h2->m_frame = hm2;
        auto *h3 = new C_Human();
        I3D_frame *hm3 = GfFrame(FRAME_MODEL, "hm3");
        h3->m_frame = hm3;
        m.FadeHuman(h2, 100, false);
        m.FadeHuman(h, 1000, false);
        m.FadeHuman(h3, 1000, false);
        WM(r.gfHumans, m.m_humanFades.size() == 3 && m.m_humanFades[0].alpha == 1.0f);
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfHumans, m.m_humanFades.size() == 2 && m.m_humanFades[0].human == h && m.m_humanFades[1].human == h3 &&
                           g_gfLog.size() == 5 && g_gfLog[0] == "halpha hm3 0.900" && g_gfLog[1] == "halpha hm 0.200" &&
                           g_gfLog[2] == "halpha hm2 0.000");
        GfClear(m);
        for (int in = 0; in < 2; ++in) {
            m.FadeHuman(h3, 1000, in != 0);
            m.FadeHuman(h2, 100, in != 0);
            m.FadeHuman(h, 1000, in != 0);
            m.m_humanFades[0].alpha = 0.5f;
            m.m_humanFades[1].alpha = 0.5f;
            m.m_humanFades[2].alpha = 0.5f;
            m.Update(100);
            WM(r.gfHumans, m.m_humanFades.size() == 2 && m.m_humanFades[0].human == h3 && m.m_humanFades[1].human == h);
            GfClear(m);
        }
        WM(r.gfHumans, m.m_humanFades.empty());
    }

    // --- the thunderstorm ---
    {
        GfClear(m);
        I3D_frame *l = GfFrame(FRAME_LIGHT, "storm");
        WmField<uint32_t>(l, 0x160) = 5;
        WmField<float>(l, 0x168) = 0.9f;
        auto *light = static_cast<I3D_light *>(l);
        m.SetThunderstorm(true, nullptr, false);
        m.SetThunderstorm(false, nullptr, true);
        WM(r.gfStorm, g_gfLog.empty() && !m.m_lightning);
        m.m_lightningStop = true;
        m.m_lightningTime = 5;
        m.SetThunderstorm(true, light, false);
        WM(r.gfStorm, GfLogIs({"mode storm 11", "on storm 1", "update storm"}) && m.m_lightning == light && l->m_refCount == 2 &&
                          m.m_lightningMode == 5 && m.m_lightningPower == 0.9f && !m.m_lightningStop && m.m_lightningTime == -1);
        I3D_frame *l2 = GfFrame(FRAME_LIGHT, "other");
        g_gfLog.clear();
        m.SetThunderstorm(true, static_cast<I3D_light *>(l2), false);
        WM(r.gfStorm, g_gfLog.empty() && m.m_lightning == light && l2->m_refCount == 1);
        // The flicker: RandFloat times the power; above 0.5 on at that power, else off.
        auto seedFor = [](bool above) {
            for (unsigned seed = 1;; ++seed) {
                std::srand(seed);
                if ((RandFloat() * 0.9f > 0.5f) == above)
                    return seed;
            }
        };
        auto flick = [&](unsigned seed, bool on) {
            std::srand(seed);
            const float power = RandFloat() * 0.9f;
            if (on)
                l->m_frameFlags |= 1;
            else
                l->m_frameFlags &= ~1u;
            std::srand(seed);
            g_gfLog.clear();
            m.Update(16);
            char text[64];
            std::snprintf(text, sizeof text, "power storm %.3f", power);
            return std::string(text);
        };
        const unsigned high = seedFor(true), low = seedFor(false);
        std::string power = flick(high, true);
        WM(r.gfStorm, GfLogIs({power, "update storm", "birds update 16", "mesh update 16"}));
        power = flick(high, false);
        WM(r.gfStorm, GfLogIs({"on storm 1", power, "update storm", "birds update 16", "mesh update 16"}));
        flick(low, true);
        WM(r.gfStorm, GfLogIs({"on storm 0", "update storm", "birds update 16", "mesh update 16"}));
        flick(low, false);
        WM(r.gfStorm, GfLogIs({"birds update 16", "mesh update 16"}));
        {
            unsigned half = 1;
            for (;; ++half) {
                std::srand(half);
                if (std::rand() == 16384)
                    break;
            }
            const float own = m.m_lightningPower;
            m.m_lightningPower = 1.0f - 1.0f / 32768.0f;   // (RandFloat: rand() * 2^-15 * (1 + 2^-15))
            l->m_frameFlags |= 1;
            std::srand(half);
            g_gfLog.clear();
            m.Update(16);
            WM(r.gfStorm, GfLogIs({"on storm 0", "update storm", "birds update 16", "mesh update 16"}));
            m.m_lightningPower = own;
        }
        // Off at the next update, or at once.
        m.SetThunderstorm(false, nullptr, false);
        WM(r.gfStorm, m.m_lightningStop && m.m_lightning == light);
        flick(low, false);
        WM(r.gfStorm, GfLogIs({"mode storm 5", "power storm 0.900", "on storm 0", "release storm", "birds update 16", "mesh update 16"}) &&
                          !m.m_lightning && l->m_refCount == 1);
        m.SetThunderstorm(true, light, false);
        g_gfLog.clear();
        m.SetThunderstorm(false, nullptr, true);
        WM(r.gfStorm, GfLogIs({"mode storm 5", "power storm 0.900", "on storm 0", "release storm"}) && !m.m_lightning);
        // The multiplayer lightning: for a time.
        I3D_frame *mp = GfFrame(FRAME_LIGHT, "mpl");
        WmField<uint32_t>(mp, 0x160) = 2;
        WmField<float>(mp, 0x168) = 0.7f;
        g_gfScene["MP_Lightning"] = mp;
        g_gfLog.clear();
        m.m_lightningStop = true;
        m.StartMpLightning(300);
        WM(r.gfStorm, GfLogIs({"sfind MP_Lightning ffff", "mode mpl 11", "on mpl 1", "update mpl"}) && m.m_lightningTime == 300 &&
                          m.m_lightningMode == 2 && m.m_lightningPower == 0.7f && !m.m_lightningStop && mp->m_refCount == 2);
        m.Update(200);
        WM(r.gfStorm, m.m_lightningTime == 100 && !m.m_lightningStop);
        m.Update(99);
        WM(r.gfStorm, m.m_lightningTime == 1 && !m.m_lightningStop);
        m.Update(1);
        WM(r.gfStorm, m.m_lightningTime == 0 && m.m_lightningStop && m.m_lightning);
        m.Update(1);
        WM(r.gfStorm, !m.m_lightning && mp->m_refCount == 1 && m.m_lightningTime == 0);
        m.StartMpLightning(0);
        m.Update(1000);
        WM(r.gfStorm, m.m_lightning && !m.m_lightningStop && m.m_lightningTime == 0);
        m.SetThunderstorm(false, nullptr, true);
        // Not a light; none.
        g_gfScene["MP_Lightning"] = GfFrame(FRAME_DUMMY, "notlight");
        m.StartMpLightning(100);
        WM(r.gfStorm, !m.m_lightning);
        g_gfScene.erase("MP_Lightning");
        g_gfLog.clear();
        m.StartMpLightning(100);
        WM(r.gfStorm, !m.m_lightning && GfLogIs({"sfind MP_Lightning ffff"}));
    }

    // --- the water camera's file ---
    {
        GfClear(m);
        S_WaterCamera block = {};
        block.enabled = true;
        block.fovWobble = true;
        block.weather = true;
        block.nearRange = 0.25f;
        block.farRange = 60.0f;
        block.fov = 30.0f;
        block.savedFov = 9.0f;
        std::strcpy(block.aboveWater, "hill");
        std::strcpy(block.underWater, "reef");
        block.underwaterNow = true;
        block.switchBackdrop = true;
        const std::string name = "missions\\GFX\\watercam.set";
        GfWaterFile(name, 1, block, sizeof block);
        m.m_waterCameraFrame = static_cast<I3D_camera *>(cam);
        m.LoadWaterCamera();
        WM(r.gfWater, std::memcmp(&m.m_waterCamera, &block, sizeof block) == 0 && !m.m_waterCameraFrame);
        GfWaterFile(name, 2, block, sizeof block);
        m.LoadWaterCamera();
        WM(r.gfWater, !m.m_waterCamera.enabled && m.m_waterCamera.fov == 0.0f);
        GfWaterFile(name, 1, block, sizeof block - 1);
        m.LoadWaterCamera();
        WM(r.gfWater, !m.m_waterCamera.enabled);
        // (A longer block: its first 0xA0 bytes.)
        GfWaterFile(name, 1, block, sizeof block);
        g_files[name].insert(g_files[name].end(), {1, 2, 3, 4});
        uint32_t &size2 = *reinterpret_cast<uint32_t *>(&g_files[name][12]);
        size2 += 4;
        m.LoadWaterCamera();
        WM(r.gfWater, std::memcmp(&m.m_waterCamera, &block, sizeof block) == 0);
        g_files.erase(name);
        m.LoadWaterCamera();
        WM(r.gfWater, !m.m_waterCamera.enabled);
        GfWaterFile(name, 1, block, sizeof block);
        game->m_missionName = "";
        m.LoadWaterCamera();
        WM(r.gfWater, !m.m_waterCamera.enabled);
        game->m_missionName = "GFX";
        m.LoadWaterCamera();
        WM(r.gfWater, m.m_waterCamera.enabled);
        // A version chunk too short for the version: none read (0).
        {
            std::vector<uint8_t> &f = g_files[name];
            GfWaterFile(name, 1, block, sizeof block);
            f.erase(f.begin() + 6, f.begin() + 10);
            f[2] = 6;
            m.LoadWaterCamera();
            WM(r.gfWater, !m.m_waterCamera.enabled);
            // More in the version chunk: skipped to its end.
            GfWaterFile(name, 1, block, sizeof block);
            f.insert(f.begin() + 10, {7, 7, 7, 7, 7, 7, 7, 7});
            f[2] = 18;
            m.LoadWaterCamera();
            WM(r.gfWater, std::memcmp(&m.m_waterCamera, &block, sizeof block) == 0);
        }
        // A file of the version chunk only.
        g_files[name].resize(10);
        m.LoadWaterCamera();
        WM(r.gfWater, !m.m_waterCamera.enabled);

        // The mission's start with it: the camera's own fov and range kept, the sounds found (the
        // scene's, else the driver's) and switched for above water.
        GfWaterFile(name, 1, block, sizeof block);
        I3D_frame *sea = GfFrame(FRAME_SOUND, "sea"), *undw = GfFrame(FRAME_SOUND, "undw");
        I3D_frame *amb = GfFrame(FRAME_SOUND, "amb"), *seaAmb = GfFrame(FRAME_SOUND, "seaamb");
        I3D_frame *sun = GfFrame(FRAME_DUMMY, "mirasun");
        GfPlace(sun, S_vector(100.0f, 50.0f, -30.0f));
        g_gfScene["a_nsea_"] = sea;
        g_gfDriverFinds["a_undw_"] = undw;
        g_gfScene["amb"] = amb;
        g_gfDriverFinds["sea_amb"] = seaAmb;
        g_gfScene["mirasun"] = sun;
        GfClear(m);
        m.m_time = 50;
        m.m_fadeLevel = 0.5f;
        m.m_fadeTarget = 0.5f;
        I3D_frame *faded = GfVisual("faded", I3DVISUAL_OBJECT);
        m.FadeOutModel(faded, 100, false);
        g_gfLog.clear();
        m.StartMission(false);
        WM(r.gfWater, m.m_fadeOuts.empty() && g_gfLog.size() > 2 && g_gfLog[2] == "release faded");
        g_gfLog.erase(g_gfLog.begin() + 2);
        WM(r.gfWater, GfLogIs({"env primary 1", "env omni 1", "sfind mirasun ffff", "link mirasun primary 1", "update mirasun",
                               "weather 0 0", "sfind a_nsea_ 4", "sfind a_undw_ 4", "dfind a_undw_", "on sea 1",
                               "active sea 1 0", "update sea", "on undw 0", "active undw 0 0", "update undw", "sfind amb 2",
                               "sfind sea_amb 2", "dfind sea_amb", "on amb 1", "active amb 1 0", "update amb", "on seaamb 0",
                               "active seaamb 0 0", "update seaamb", "dec load", "dec start"}));
        WM(r.gfWater, m.m_waterCamera.enabled && !m.m_waterCamera.underwaterNow && m.m_waterCamera.savedFov == 1.2f &&
                          m.m_waterCamera.savedNear == 0.5f && m.m_waterCamera.savedFar == 300.0f && !m.m_waterCameraFrame &&
                          m.m_seaSound == sea && m.m_underwaterSound == undw && m.m_ambientSound == amb &&
                          m.m_seaAmbientSound == seaAmb && m.m_sun == sun && m.m_sunPos.x == 100.0f && m.m_sunPos.y == 50.0f &&
                          m.m_sunPos.z == -30.0f && m.m_started && m.m_time == 0 && m.m_fadeLevel == 0.0f &&
                          m.m_fadeTarget == 0.0f);
        // Not found anywhere; the weather left alone without the flag.
        g_gfScene.erase("a_nsea_");
        g_gfDriverFinds.erase("a_undw_");
        g_gfScene.erase("amb");
        g_gfDriverFinds.erase("sea_amb");
        g_gfScene.erase("mirasun");
        block.weather = false;
        GfWaterFile(name, 1, block, sizeof block);
        GfClear(m);
        m.StartMission(false);
        WM(r.gfWater, GfLogIs({"env primary 1", "env omni 1", "sfind mirasun ffff", "sfind a_nsea_ 4", "dfind a_nsea_",
                               "sfind a_undw_ 4", "dfind a_undw_", "sfind amb 2", "dfind amb", "sfind sea_amb 2", "dfind sea_amb",
                               "dec load", "dec start"}) &&
                          !m.m_seaSound && !m.m_underwaterSound && !m.m_ambientSound && !m.m_seaAmbientSound && !m.m_sun);
        // A client only gathers the birds; the server side only that.
        g_files.erase(name);
        GfClear(m);
        g_gameRole = GAME_ROLE_CLIENT;
        m.StartMission(false);
        g_gameRole = GAME_ROLE_LOCAL;
        WM(r.gfWater, GfLogIs({"env primary 1", "env omni 1", "sfind mirasun ffff", "birds collect", "dec load", "dec start"}) &&
                          !m.m_waterCamera.enabled);
        GfClear(m);
        m.m_time = 50;
        m.StartMission(true);
        WM(r.gfWater, GfLogIs({"birds collect"}) && m.m_time == 50 && !m.m_started);
    }

    // --- the underwater view ---
    {
        auto setup = [&]() {
            GfClear(m);
            m.m_waterCamera.enabled = true;
            m.m_waterCamera.fovWobble = false;
            m.m_waterCamera.weather = true;
            m.m_waterCamera.nearRange = 0.25f;
            m.m_waterCamera.farRange = 60.0f;
            m.m_waterCamera.fov = 30.0f;
            std::strcpy(m.m_waterCamera.aboveWater, "hill");
            std::strcpy(m.m_waterCamera.underWater, "reef");
            m.m_waterCamera.switchBackdrop = true;
            m.m_seaSound = GfFrame(FRAME_DUMMY, "sea");
            m.m_underwaterSound = GfFrame(FRAME_DUMMY, "undw");
            m.m_ambientSound = GfFrame(FRAME_DUMMY, "amb");
            m.m_seaAmbientSound = GfFrame(FRAME_DUMMY, "seaamb");
            g_gfScene["hill"] = GfFrame(FRAME_DUMMY, "hill");
            g_gfScene["reef"] = GfFrame(FRAME_DUMMY, "reef");
            WmField<float>(cam, 0x120) = 1.2f;
            WmField<float>(cam, 0x124) = 0.5f;
            WmField<float>(cam, 0x128) = 300.0f;
            cam->m_refCount = 1;
            g_gfLog.clear();
        };
        // Only the effects' part of the log.
        auto fx = [&]() {
            std::vector<std::string> out;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 6, "birds ") && l.compare(0, 12, "mesh update "))
                    out.push_back(l);
            return out;
        };
        const std::vector<std::string> under = {
            "headwater", "isgame cam", "special 4 1 5.000", "fov cam 0.5236", "range cam 0.2 60.0", "update cam", "sfind hill ffff",
            "on hill 0", "update hill", "sfind reef ffff", "on reef 1", "update reef", "on sky1 0", "on lensflare 0", "on sea 0",
            "update sea", "on undw 1", "update undw", "on amb 0", "update amb", "on seaamb 1", "update seaamb", "weather 0 1"};
        auto *player = new C_Human();
        player->movement = reinterpret_cast<C_HumanMovement *>(g_gfInterface);
        g_gfActive = player;
        g_gfSurface = 5.0f;
        setup();
        g_gfHeadUnder = true;
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), under));
        WM(r.gfUnderwater, m.m_waterCamera.underwaterNow && m.m_waterCameraFrame == cam && cam->m_refCount == 2 &&
                               m.m_waterCamera.savedFov == 1.2f && m.m_waterCamera.savedNear == 0.5f &&
                               m.m_waterCamera.savedFar == 300.0f && g_waterLevel == 5.0f);
        // Still under: nothing switched; the fov sways when asked to (the game camera's only).
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater", "isgame cam"}));
        m.m_waterCamera.fovWobble = true;
        m.m_time = 2000;
        g_gfLog.clear();
        m.Update(16);
        {
            const float t = 2016.0f;
            char text[64];
            std::snprintf(text, sizeof text, "fov cam %.4f",
                          std::sin(0.001f * t) * 0.008f + std::cos(t * 0.002f) * 0.01f + 30.0f * 0.0174533f);
            WM(r.gfUnderwater, GfVecIs(fx(), {"isgame cam", text, "update cam", "headwater", "isgame cam"}));
        }
        g_gfGameCamera = false;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"isgame cam", "headwater", "isgame cam"}));
        g_gfGameCamera = true;
        m.m_waterCamera.fovWobble = false;
        // Surfacing (the head out; still under water until the camera is up).
        g_gfHeadUnder = false;
        GfPlace(cam, S_vector(10.0f, 5.0f, 20.0f));
        WmField<float>(cam, 0x120) = 0.5f;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater", "isgame cam", "fov cam 1.2000", "range cam 0.5 300.0",
                                                             "update cam", "release cam", "special 4 0 5.000", "sfind reef ffff",
                                                             "on reef 0", "update reef", "sfind hill ffff", "on hill 1",
                                                             "update hill", "on sky1 1", "on lensflare 1", "on sea 1", "update sea",
                                                             "on undw 0", "update undw", "on amb 1", "update amb", "on seaamb 0",
                                                             "update seaamb", "weather 0 0"}));
        // (Quirk kept: the camera taken again above water.)
        WM(r.gfUnderwater, !m.m_waterCamera.underwaterNow && m.m_waterCameraFrame == cam && cam->m_refCount == 2 &&
                               m.m_waterCamera.savedFov == 1.2f);
        // Above water with the head dry: no test of the camera.
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater"}));
        // The head under, the camera above: nothing (another camera: switched to it, above water).
        g_gfHeadUnder = true;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater", "isgame cam"}));
        I3D_frame *cam2 = GfFrame(FRAME_CAMERA, "cam2");
        WmField<float>(cam2, 0x120) = 0.9f;
        GfPlace(cam2, S_vector(0.0f, 6.0f, 0.0f));
        WmField<I3D_frame *>(scene, 0x15C) = cam2;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, fx().size() == 24 && fx()[2] == "fov cam 1.2000" && fx()[5] == "release cam" &&
                               fx()[6] == "special 4 0 5.000" && m.m_waterCameraFrame == cam2 && m.m_waterCamera.savedFov == 0.9f &&
                               cam->m_refCount == 1 && cam2->m_refCount == 2);
        // Exactly at the surface: above water.
        GfPlace(cam2, S_vector(0.0f, 5.0f, 0.0f));
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater", "isgame cam2"}));
        WmField<I3D_frame *>(scene, 0x15C) = cam;
        // In a submarine (the head dry), or under water already: the camera is tested.
        setup();
        g_gfHeadUnder = false;
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        C_Human sub;
        sub.m_type = ACTOR_SUBMARINE;
        player->vehicle = &sub;
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), under));
        C_Human car;
        car.m_type = ACTOR_CAR;
        player->vehicle = &car;
        setup();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater"}));
        player->vehicle = nullptr;
        g_gfHeadUnder = true;
        // Under water already with another camera there: it takes that one.
        setup();
        m.m_waterCamera.underwaterNow = true;
        m.m_waterCameraFrame = static_cast<I3D_camera *>(cam2);
        m.m_waterCamera.savedFov = 0.9f;
        m.m_waterCamera.savedNear = 1.0f;
        m.m_waterCamera.savedFar = 2.0f;
        cam2->m_refCount = 2;
        WmField<float>(cam2, 0x120) = 0.4f;
        g_gfLog.clear();
        m.Update(16);
        {
            std::vector<std::string> expected = {"headwater", "isgame cam", "fov cam2 0.9000", "range cam2 1.0 2.0", "update cam2",
                                                 "release cam2"};
            expected.insert(expected.end(), under.begin() + 2, under.end());
            WM(r.gfUnderwater, GfVecIs(fx(), expected) && m.m_waterCameraFrame == cam && cam2->m_refCount == 1);
        }
        // Without the weather flag, the hierarchies' names or the backdrop's switch.
        setup();
        m.m_waterCamera.weather = false;
        m.m_waterCamera.aboveWater[0] = '\0';
        m.m_waterCamera.switchBackdrop = false;
        m.m_seaAmbientSound = nullptr;
        m.m_seaSound = nullptr;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfUnderwater, GfVecIs(fx(), {"headwater", "isgame cam", "special 4 1 5.000", "fov cam 0.5236",
                                                             "range cam 0.2 60.0", "update cam", "sfind reef ffff", "on reef 1",
                                                             "update reef", "on undw 1", "update undw", "on amb 0", "update amb"}));
        // The hierarchy not in the scene.
        setup();
        g_gfScene.erase("reef");
        m.m_waterCamera.underWater[1] = '\0';
        m.Update(16);
        WM(r.gfUnderwater, fx().size() == under.size() - 2 && fx()[9] == "sfind r ffff");
        // No running mission, the water camera off, no active player: nothing.
        setup();
        game->m_state = GAME_STATE_GAME_MENU;
        m.Update(16);
        game->m_state = GAME_STATE_STARTING;
        m.Update(16);
        WM(r.gfUnderwater, fx().size() == under.size());
        setup();
        game->m_state = GAME_STATE_ENDING;
        m.Update(16);
        WM(r.gfUnderwater, fx().size() == under.size());
        game->m_state = GAME_STATE_RUNNING;
        setup();
        m.m_waterCamera.enabled = false;
        m.Update(16);
        g_gfActive = nullptr;
        m.m_waterCamera.enabled = true;
        m.Update(16);
        WM(r.gfUnderwater, fx().empty());
        g_gfActive = player;

        // The mission's end: the camera given back, above water, the sounds forgotten.
        setup();
        g_gfHeadUnder = true;
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        m.Update(16);
        I3D_frame *sun = GfFrame(FRAME_DUMMY, "sun");
        GfPlace(sun, S_vector(1.0f, 2.0f, 3.0f));
        m.m_sun = sun;
        m.m_sunPos = S_vector(7.0f, 8.0f, 9.0f);
        m.m_started = true;
        m.m_rain = true;
        m.m_fadeLevel = 0.5f;
        m.m_fadeTarget = 1.0f;
        g_gfLog.clear();
        m.StopMission(false);
        WM(r.gfUnderwater, GfLogIs({"tactical 0", "find backdrop lensflare ffff", "on lensflare 1", "update sun", "birds free",
                                    "fov cam 1.2000", "range cam 0.5 300.0", "update cam", "release cam", "weather 0 0",
                                    "sfind hill ffff", "on hill 1", "update hill", "sfind reef ffff", "on reef 0", "update reef",
                                    "on sea 1", "update sea", "on undw 0", "update undw", "on amb 1", "update amb", "on seaamb 0",
                                    "update seaamb", "on sky1 1", "on lensflare 1", "dec free", "weather 0 0"}));
        WM(r.gfUnderwater, !m.m_waterCameraFrame && !m.m_seaSound && !m.m_underwaterSound && !m.m_ambientSound &&
                               !m.m_seaAmbientSound && !m.m_sun && sun->GetPos().x == 7.0f && sun->GetPos().y == 8.0f &&
                               sun->GetPos().z == 9.0f && !m.m_started && !m.m_rain && m.m_fadeLevel == -1000.0f &&
                               m.m_fadeTarget == 0.0f);
        {
            I3D_frame *storm = GfFrame(FRAME_LIGHT, "stopstorm");
            m.SetThunderstorm(true, static_cast<I3D_light *>(storm), false);
            g_gfLog.clear();
            m.StopMission(false);
            WM(r.gfUnderwater, !m.m_lightning && std::count(g_gfLog.begin(), g_gfLog.end(), "release stopstorm") == 1);
        }
        // The server side: only the tactical camera off; without the water camera.
        g_gfLog.clear();
        m.m_started = true;
        m.StopMission(true);
        WM(r.gfUnderwater, GfLogIs({"tactical 0"}) && m.m_started);
        setup();
        m.m_waterCamera.enabled = false;
        g_cameraHumanEffects = nullptr;
        m.StopMission(false);
        g_cameraHumanEffects = reinterpret_cast<C_CameraHumanEffects *>(g_gfCameraEffects);
        WM(r.gfUnderwater, GfLogIs({"find backdrop lensflare ffff", "on lensflare 1", "birds free", "dec free", "weather 0 0"}) &&
                               m.m_seaSound);
        // The lens flare without its frame.
        I3D_frame *flareFrame = flare;
        flareFrame->m_name = "flare2";
        g_stubs[flareFrame]->name = "flare2";
        g_gfLog.clear();
        m.SetLensFlareOn(true);
        WM(r.gfUnderwater, GfLogIs({"find backdrop lensflare ffff"}));
        g_stubs[flareFrame]->name = "lensflare";
        flareFrame->m_name = g_stubs[flareFrame]->name.c_str();
        g_gfHeadUnder = false;
        g_gfActive = nullptr;
        WmField<float>(cam, 0x120) = 1.2f;
        WmField<float>(cam, 0x124) = 0.5f;
        WmField<float>(cam, 0x128) = 300.0f;
        cam->m_refCount = 1;
        (void)sky;
    }

    // --- rain ---
    {
        GfClear(m);
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        auto fx = [&]() {
            std::vector<std::string> out;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 6, "birds ") && l.compare(0, 12, "mesh update "))
                    out.push_back(l);
            return out;
        };
        S_CollisionVolume ground, water;
        water.flags = COLLISION_FLAG_WATER | 1;
        ground.flags = 2;
        m.Update(250);
        WM(r.gfRain, fx().empty() && g_rainTime == 0);
        m.SetRain(true);
        m.m_rainRate = 20;
        m.Update(99);
        WM(r.gfRain, fx().empty() && g_rainTime == 99);
        g_gfRandoms = {1.0f, 3.0f, 5.0f, 7.0f};
        g_gfRays = {{&ground, S_vector(5.0f, 0.0f, 13.0f)}, {&water, S_vector(7.0f, 1.0f, 15.0f)}};
        m.Update(51);
        WM(r.gfRain, GfVecIs(fx(), {"rand 16.0", "rand 16.0",
                                                       "ray 5.000 102.000 13.000 dir 0.0 -200.0 0.0 1 0 ffffffff 0",
                                                       "fx 44 5.000 0.000 13.000 dir 0.0 1.0 0.0 0 1 0", "rand 16.0", "rand 16.0",
                                                       "ray 9.000 102.000 17.000 dir 0.0 -200.0 0.0 1 0 ffffffff 0",
                                                       "fx 45 7.000 1.020 15.000 dir 0.0 1.0 0.0 0 0 0"}) &&
                         g_rainTime == 50 && g_rainTeamSteps == 2);
        // Two steps in one update; no hit.
        g_gfLog.clear();
        m.m_rainRate = 10;
        m.m_rainArea = 2.0f;
        g_gfRandoms = {0.0f, 4.0f, 1.0f, 1.0f};
        m.Update(160);
        WM(r.gfRain, GfVecIs(fx(), {"rand 4.0", "rand 4.0", "ray 12.000 102.000 18.000 dir 0.0 -200.0 0.0 1 0 ffffffff 0",
                                                       "rand 4.0", "rand 4.0", "ray 9.000 102.000 19.000 dir 0.0 -200.0 0.0 1 0 ffffffff 0"}) &&
                         g_rainTime == 10 && g_rainTeamSteps == 3);
        // A rate under 10 per second: no drops.
        g_gfLog.clear();
        m.m_rainRate = 9;
        m.Update(90);
        WM(r.gfRain, fx().empty() && g_rainTime == 0 && g_rainTeamSteps == 2);
        m.m_rainRate = 39;
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfRain, std::count_if(g_gfLog.begin(), g_gfLog.end(), [](const std::string &l) { return l.compare(0, 4, "ray ") == 0; }) == 3);
        // Paused: rain only while the command map is open in a running mission.
        m.m_rainRate = 10;
        game->m_paused = true;
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfRain, fx().empty() && g_rainTime == 0);
        g_gfMapOpen = true;
        m.Update(100);
        WM(r.gfRain, fx().size() == 3 && g_rainTime == 0);
        game->m_state = GAME_STATE_GAME_MENU;
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfRain, fx().empty());
        game->m_state = GAME_STATE_STARTING;
        m.Update(100);
        game->m_state = GAME_STATE_ENDING;
        m.Update(100);
        WM(r.gfRain, fx().size() == 6);
        game->m_state = GAME_STATE_RUNNING;
        g_gfMapOpen = false;
        game->m_paused = false;
        // The decorators go on while paused.
        m.m_started = true;
        game->m_paused = true;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfRain, std::count(g_gfLog.begin(), g_gfLog.end(), "dec update 16") == 1);
        g_gfMapOpen = true;
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfRain, std::count(g_gfLog.begin(), g_gfLog.end(), "dec update 16") == 1);
        g_gfMapOpen = false;
        game->m_paused = false;
        GfClear(m);
    }

    // --- rain on the team ---
    {
        GfClear(m);
        GfPlace(cam, S_vector(0.0f, 0.0f, 0.0f));
        auto makeMember = [&](const char *name, const S_vector &pos, bool primarySector, int stamp) {
            auto *h = new C_Human();
            h->m_frame = GfFrame(FRAME_MODEL, std::string(name) + "_m");
            GfPlace(h->m_frame, pos);
            WmField<int>(h->m_frame, 0x11C) = stamp;
            h->bodyFrame = GfFrame(FRAME_DUMMY, std::string(name) + "_b");
            GfPlace(h->bodyFrame, pos + S_vector(0.0f, 1.0f, 0.0f));
            h->bodySector = reinterpret_cast<I3D_sector *>(primarySector ? primary : omni);
            g_gfVolumes[static_cast<int>(g_gfMembers.size())] = GfFrame(FRAME_VOLUME, std::string(name) + "_v");
            GfPlace(g_gfVolumes[static_cast<int>(g_gfMembers.size())], pos + S_vector(0.0f, 2.0f, 0.0f));
            g_gfMembers.push_back(h);
        };
        makeMember("m0", S_vector(3.0f, 0.0f, 4.0f), true, 77);
        makeMember("m1", S_vector(3.0f, 0.0f, 4.0f), false, 77);
        makeMember("m2", S_vector(8.0f, 0.0f, 6.1f), true, 77);
        makeMember("m3", S_vector(1.0f, 0.0f, 1.0f), true, 76);
        makeMember("m4", S_vector(6.0f, 0.0f, 8.0f), true, 77);       // at 10 m
        makeMember("m5", S_vector(-2.0f, 0.0f, 0.0f), true, 77);      // under a dummy
        makeMember("m6", S_vector(0.0f, 0.0f, -2.0f), true, 77);      // near one, outside its box
        // The dummies: their sphere and their box in their own space.
        auto box = [&](const char *name, const S_vector &at, float radiusSquare, const S_vector &mn, const S_vector &mx) {
            I3D_frame *d = GfFrame(FRAME_DUMMY, name);
            GfPlace(d, at);
            WmField<I3D_bbox>(d, 0x120) = {mn, 0.0f, mx, 0.0f};
            S_dummyBox b = {};
            b.frame = d;
            b.center = at;
            b.radiusSquare = radiusSquare;
            g_gfBoxes.push_back(b);
        };
        box("d0", S_vector(-2.0f, 1.0f, 0.0f), 4.0f, S_vector(-1.0f, -1.0f, -1.0f), S_vector(1.0f, 1.0f, 1.0f));
        box("d1", S_vector(0.0f, 1.0f, -2.0f), 4.0f, S_vector(0.5f, -1.0f, -1.0f), S_vector(1.0f, 1.0f, 1.0f));
        box("d2", S_vector(3.0f, 1.0f, 4.0f), 0.0f, S_vector(-1.0f, -1.0f, -1.0f), S_vector(1.0f, 1.0f, 1.0f));
        m.RainOnTeam();
        WM(r.gfTeamRain, GfLogIs({"volume 6", "fx 44 0.000 2.100 -2.000 dir 0.0 1.0 0.0 m6_v 1 0", "volume 4",
                                  "fx 44 6.000 2.100 8.000 dir 0.0 1.0 0.0 m4_v 1 0", "volume 0",
                                  "fx 44 3.000 2.100 4.000 dir 0.0 1.0 0.0 m0_v 1 0"}));
        // The box's edges: the minimum inside, the maximum outside.
        g_gfLog.clear();
        g_gfBoxes[0].frame->m_localMatrix(3) = S_vector(-1.0f, 1.0f, 0.0f);   // m5's body at its min x
        g_gfBoxes[1].frame->m_localMatrix(3) = S_vector(-1.0f, 1.0f, -2.0f);  // m6's body at its max x
        WmField<I3D_bbox>(g_gfBoxes[1].frame, 0x120).min.x = -1.0f;
        m.RainOnTeam();
        WM(r.gfTeamRain, g_gfLog.size() == 6 && g_gfLog[0] == "volume 6");
        g_gfLog.clear();
        WmField<I3D_bbox>(g_gfBoxes[1].frame, 0x120).max.x = 1.5f;
        m.RainOnTeam();
        WM(r.gfTeamRain, g_gfLog.size() == 4 && g_gfLog[0] == "volume 4");
        // Each axis of the box: out in y, out in z.
        for (int axis = 1; axis < 3; ++axis) {
            for (int side = 0; side < 2; ++side) {
                I3D_bbox &b = WmField<I3D_bbox>(g_gfBoxes[1].frame, 0x120);
                b = {S_vector(-1.0f, -1.0f, -1.0f), 0.0f, S_vector(1.5f, 1.0f, 1.0f), 0.0f};
                float *edge = side ? &b.max.x : &b.min.x;
                edge[axis] = side ? -0.5f : 0.5f;
                g_gfLog.clear();
                m.RainOnTeam();
                WM(r.gfTeamRain, g_gfLog.size() == 6 && g_gfLog[0] == "volume 6");
            }
        }
        // A sphere's radius exactly at the body: not under it.
        g_gfBoxes[0].center = S_vector(-1.0f, 1.0f, 0.0f);
        g_gfBoxes[0].radiusSquare = 1.0f;
        g_gfLog.clear();
        m.RainOnTeam();
        WM(r.gfTeamRain, g_gfLog.size() == 8 && g_gfLog[2] == "volume 5");
        g_gfBoxes[0].radiusSquare = 1.0001f;
        g_gfLog.clear();
        m.RainOnTeam();
        WM(r.gfTeamRain, g_gfLog.size() == 6 && g_gfLog[2] == "volume 4");
        // The distance in each axis.
        g_gfLog.clear();
        GfPlace(cam, S_vector(0.0f, 9.0f, 0.0f));
        m.RainOnTeam();
        WM(r.gfTeamRain, g_gfLog.size() == 2 && g_gfLog[0] == "volume 6");
        g_gfLog.clear();
        GfPlace(cam, S_vector(0.0f, 0.0f, 8.0f));
        m.RainOnTeam();
        WM(r.gfTeamRain, g_gfLog.size() == 8 && g_gfLog[0] == "volume 6" && g_gfLog[2] == "volume 4" && g_gfLog[4] == "volume 2" &&
                             g_gfLog[6] == "volume 0");
        {
            GfPlace(cam, S_vector(4.0f, 0.0f, 0.0f));
            g_gfLog.clear();
            m.RainOnTeam();
            WM(r.gfTeamRain, std::count(g_gfLog.begin(), g_gfLog.end(), "volume 4") == 1);
            makeMember("m7", S_vector(0.0f, 7.0f, 0.0f), true, 77);
            GfPlace(cam, S_vector(0.0f, 4.0f, 0.0f));
            g_gfLog.clear();
            m.RainOnTeam();
            WM(r.gfTeamRain, std::count(g_gfLog.begin(), g_gfLog.end(), "volume 7") == 1);
            g_gfMembers.pop_back();
            GfPlace(cam, S_vector(0.0f, 0.0f, 0.0f));
            const S_dummyBox keep = g_gfBoxes[0];
            g_gfBoxes[0].center = S_vector(-2.0f, 2.5f, 0.0f);
            g_gfBoxes[0].radiusSquare = 2.0f;
            g_gfLog.clear();
            m.RainOnTeam();
            WM(r.gfTeamRain, std::count(g_gfLog.begin(), g_gfLog.end(), "volume 5") == 1);
            g_gfBoxes[0] = keep;
            I3D_bbox &b1 = WmField<I3D_bbox>(g_gfBoxes[1].frame, 0x120);
            b1 = {S_vector(-1.0f, -1.0f, 0.0f), 0.0f, S_vector(1.5f, 1.0f, 1.0f), 0.0f};
            g_gfLog.clear();
            m.RainOnTeam();
            WM(r.gfTeamRain, std::count(g_gfLog.begin(), g_gfLog.end(), "volume 6") == 0);
        }
        // Through Update every third step.
        GfPlace(cam, S_vector(0.0f, 0.0f, 0.0f));
        g_gfBoxes.clear();
        m.SetRain(true);
        m.m_rainRate = 0;
        g_gfLog.clear();
        m.Update(200);
        WM(r.gfTeamRain, g_gfLog.size() == 2 && g_rainTeamSteps == 1);
        m.Update(100);
        WM(r.gfTeamRain, g_rainTeamSteps == 3 &&
                             std::count_if(g_gfLog.begin(), g_gfLog.end(), [](const std::string &l) { return l.compare(0, 3, "fx ") == 0; }) == 4);
        g_gfMembers.clear();
        g_gfVolumes.clear();
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        GfClear(m);
    }

    // --- camera animations ---
    {
        GfClear(m);
        I3D_frame *owner = GfFrame(FRAME_DUMMY, "owner");
        I3D_frame *c2 = GfFrame(FRAME_CAMERA, "c2", owner);
        GfPlace(c2, S_vector(1.0f, 2.0f, 3.0f));
        m.SetCameraAnimation(c2, "");
        WM(r.gfCamera, g_gfLog.empty() && m.m_cameraAnimations.empty());
        static I3D_frame *camFrame;
        static int fillKind;
        g_cacheFill = [](I3D_model *object) {
            I3D_frame *model = reinterpret_cast<I3D_frame *>(object);
            camFrame = nullptr;
            if (fillKind == 0) {
                GfFrame(FRAME_DUMMY, "body", model);
                camFrame = GfFrame(FRAME_DUMMY, "x_cam_1", model);
            } else if (fillKind == 1) {
                camFrame = GfFrame(FRAME_DUMMY, "first", model);
                GfFrame(FRAME_DUMMY, "second", model);
            } else if (fillKind == 3) {
                camFrame = GfFrame(FRAME_DUMMY, "only", model);
            }
        };
        fillKind = 0;
        g_cacheLoads.clear();
        S_quat rot(0.5f, 0.5f, 0.5f, 0.5f);
        c2->m_rot = rot;
        m.SetCameraAnimation(c2, "anims\\fly.i3d");
        WM(r.gfCamera, GfLogIs({"link c2 primary 1", "update c2", "create 9", "find anim *cam* 1ffff", "update anim"}) &&
                           g_cacheLoads == std::vector<std::string>({"anims\\fly.i3d"}) && g_cacheLoadFlags == 0);
        WM(r.gfCamera, m.m_cameraAnimations.size() == 1);
        S_CameraAnimation *a = m.m_cameraAnimations[0];
        I3D_frame *model = g_gfCreated.back();
        WM(r.gfCamera, a->camera == c2 && c2->m_refCount == 2 && a->model == reinterpret_cast<I3D_model *>(model) &&
                           a->cameraFrame == camFrame && a->parent == owner && a->pos.x == 1.0f && a->pos.y == 2.0f &&
                           a->pos.z == 3.0f && a->rot.s == 0.5f && a->rot.v.x == 0.5f && model->GetPos().x == 0.0f &&
                           model->m_rot.s == 1.0f && model->m_rot.v.x == 0.0f && c2->m_parent == primary);
        // Played: the camera at its frame; the last key held.
        auto *anim = new S_GfAnim{g_gfAnimVt, 1000};
        g_gfAnims[model] = anim;
        g_gfAnimTimes[model] = 0;
        GfPlace(camFrame, S_vector(4.0f, 5.0f, 6.0f));
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfCamera, g_gfLog.size() >= 3 && g_gfLog[0] == "tick anim 100" && g_gfLog[1] == "update anim" &&
                           g_gfLog[2] == "update c2" && c2->GetPos().x == 4.0f && c2->GetPos().y == 5.0f && c2->GetPos().z == 6.0f &&
                           c2->m_rot.s == 1.0f);
        g_gfAnimTimes[model] = 899;
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfCamera, g_gfLog[0] == "tick anim 100");
        g_gfAnimTimes[model] = 900;
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfCamera, g_gfLog[0] == "tick anim 99");
        g_gfAnimTimes[model] = 950;
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfCamera, g_gfLog[0] == "tick anim 49");
        // A locked camera keeps its place; no animation: not played.
        c2->m_frameFlags |= FRMFLAGS_POS_LOCKED;
        GfPlace(camFrame, S_vector(7.0f, 7.0f, 7.0f));
        m.Update(1);
        WM(r.gfCamera, c2->GetPos().x == 4.0f);
        c2->m_frameFlags &= ~FRMFLAGS_POS_LOCKED;
        g_gfAnims.erase(model);
        g_gfLog.clear();
        m.Update(100);
        WM(r.gfCamera, GfLogIs({"birds update 100", "mesh update 100"}));
        g_gfAnims[model] = anim;
        // Another animation for the camera: the first one restored.
        fillKind = 1;
        g_gfLog.clear();
        m.SetCameraAnimation(c2, "two");
        WM(r.gfCamera, GfLogIs({"link c2 owner 0", "update c2", "release c2", "release anim", "link c2 primary 1", "update c2",
                                "create 9", "find anim *cam* 1ffff", "update anim"}) &&
                           m.m_cameraAnimations.size() == 1 && m.m_cameraAnimations[0]->cameraFrame == camFrame &&
                           c2->m_refCount == 2);
        WM(r.gfCamera, m.m_cameraAnimations[0]->pos.x == 1.0f && m.m_cameraAnimations[0]->pos.y == 2.0f &&
                           m.m_cameraAnimations[0]->pos.z == 3.0f && c2->m_parent == primary &&
                           m.m_cameraAnimations[0]->rot.s == 0.5f && m.m_cameraAnimations[0]->rot.v.y == 0.5f);
        {
            I3D_frame *c5 = GfFrame(FRAME_CAMERA, "c5");
            fillKind = 3;
            m.SetCameraAnimation(c5, "one");
            WM(r.gfCamera, m.m_cameraAnimations.size() == 2 && m.m_cameraAnimations[1]->cameraFrame == camFrame && camFrame);
            m.SetCameraAnimation(c5, "");
        }
        // A model without frames: none followed (Fix).
        I3D_frame *c3 = GfFrame(FRAME_CAMERA, "c3");
        fillKind = 2;
        m.SetCameraAnimation(c3, "three");
        WM(r.gfCamera, m.m_cameraAnimations.size() == 2 && !m.m_cameraAnimations[1]->cameraFrame);
        I3D_frame *model3 = g_gfCreated.back();
        g_gfAnims[model3] = anim;
        g_gfAnimTimes[model3] = 0;
        g_gfLog.clear();
        m.Update(10);
        WM(r.gfCamera, g_gfLog.size() >= 2 && g_gfLog[0] == "tick anim 10" && g_gfLog[1] == "update anim" &&
                           std::count(g_gfLog.begin(), g_gfLog.end(), "update c3") == 0);
        // Another camera's animation dropped: only it, the last moved into its place.
        I3D_frame *c4 = GfFrame(FRAME_CAMERA, "c4");
        fillKind = 0;
        m.SetCameraAnimation(c4, "four");
        g_gfLog.clear();
        m.SetCameraAnimation(c2, "");
        WM(r.gfCamera, GfLogIs({"link c2 owner 0", "update c2", "release c2", "release anim"}) &&
                           m.m_cameraAnimations.size() == 2 && m.m_cameraAnimations[0]->camera == c4 &&
                           m.m_cameraAnimations[1]->camera == c3);
        // The mission's end (Reset): every one restored.
        g_gfLog.clear();
        m.Reset();
        WM(r.gfCamera, GfLogIs({"link c3 0 0", "update c3", "release c3", "release anim", "link c4 0 0", "update c4", "release c4",
                                "release anim"}) &&
                           m.m_cameraAnimations.empty());
        m.SetCameraAnimation(c4, "four");
        g_gfLog.clear();
        m.StopMission(false);
        WM(r.gfCamera, g_gfLog.size() > 4 && g_gfLog[1] == "link c4 0 0" && m.m_cameraAnimations.empty());
        g_cacheFill = nullptr;
    }

    // --- the mission's flow, the sun ---
    {
        GfClear(m);
        m.LoadMissionData(true);
        m.FreeMissionData(true);
        WM(r.gfMission, g_gfLog.empty());
        I3D_frame *v = GfVisual("fv", I3DVISUAL_OBJECT);
        m.FadeOutModel(v, 100, false);
        m.LoadMissionData(false);
        WM(r.gfMission, GfLogIs({"release fv", "vol load", "vol prune 1", "mesh load"}) && m.m_fadeOuts.empty());
        g_gfLog.clear();
        m.FadeIn(v, 100, false);
        m.m_timeScale = 0.5f;
        m.FreeMissionData(false);
        WM(r.gfMission, GfLogIs({"alpha fv 0.000", "release fv", "vol free", "mesh free"}) && m.m_fadeIns.empty() &&
                            m.m_timeScale == 1.0f);
        // Restart: the decorators and birds again, the time from 0, no rain, the storm and the
        // tactical camera off.
        I3D_frame *l = GfFrame(FRAME_LIGHT, "rl");
        m.SetThunderstorm(true, static_cast<I3D_light *>(l), false);
        m.m_rain = true;
        m.m_time = 900;
        m.m_humanFades.push_back({nullptr, true, 1.0f, 0.5f});
        g_gfLog.clear();
        m.RestartMission(false);
        WM(r.gfMission, GfLogIs({"dec free", "dec load", "dec start", "birds free", "birds collect", "tactical 0", "mode rl 0",
                                 "power rl 0.000", "on rl 0", "release rl"}) &&
                            m.m_time == 0 && !m.m_rain && !m.m_lightning && m.m_humanFades.empty());
        g_cameraHumanEffects = nullptr;
        g_gfLog.clear();
        m.RestartMission(true);
        g_cameraHumanEffects = reinterpret_cast<C_CameraHumanEffects *>(g_gfCameraEffects);
        WM(r.gfMission, GfLogIs({"dec free", "dec load", "dec start", "birds free", "birds collect"}));
        // The sun with the camera; a locked one stays.
        I3D_frame *sun = GfFrame(FRAME_DUMMY, "sun2");
        m.m_sun = sun;
        m.m_sunPos = S_vector(100.0f, 50.0f, -30.0f);
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfMission, GfLogIs({"update sun2", "birds update 16", "mesh update 16"}) && sun->GetPos().x == 110.0f &&
                            sun->GetPos().y == 52.0f && sun->GetPos().z == -10.0f);
        sun->m_frameFlags |= FRMFLAGS_POS_LOCKED;
        GfPlace(cam, S_vector(0.0f, 0.0f, 0.0f));
        g_gfLog.clear();
        m.Update(16);
        WM(r.gfMission, GfLogIs({"update sun2", "birds update 16", "mesh update 16"}) && sun->GetPos().x == 110.0f);
        sun->m_frameFlags &= ~FRMFLAGS_POS_LOCKED;
        WmField<I3D_frame *>(scene, 0x15C) = nullptr;
        g_gfLog.clear();
        m.Update(16);
        WmField<I3D_frame *>(scene, 0x15C) = cam;
        GfPlace(cam, S_vector(10.0f, 2.0f, 20.0f));
        WM(r.gfMission, GfLogIs({"birds update 16", "mesh update 16"}));
        GfClear(m);
    }

    std::memcpy(g_sceneVtable, sceneSlots, sizeof sceneSlots);
    std::memcpy(g_driverVtable, driverSlots, sizeof driverSlots);
    std::memcpy(g_igraphVtable, igraphSlots, sizeof igraphSlots);
    WmField<I3D_frame *>(scene, 0x1F8) = oldPrimary;
    WmField<I3D_frame *>(scene, 0x15C) = nullptr;
    WmField<I3D_frame *>(scene, 0x1FC) = nullptr;
    WmField<I3D_frame *>(scene, 0x200) = nullptr;
    (void)flare;
    std::printf("geffects: gfModule %d gfQuad %d gfFade %d gfModelFx %d gfHierarchy %d gfFades %d gfHumans %d gfStorm %d "
                "gfWater %d gfUnderwater %d gfRain %d gfTeamRain %d gfCamera %d gfMission %d\n",
                r.gfModule, r.gfQuad, r.gfFade, r.gfModelFx, r.gfHierarchy, r.gfFades, r.gfHumans, r.gfStorm, r.gfWater,
                r.gfUnderwater, r.gfRain, r.gfTeamRain, r.gfCamera, r.gfMission);
}
