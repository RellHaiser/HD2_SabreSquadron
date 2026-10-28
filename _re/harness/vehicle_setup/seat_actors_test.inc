// Part 7e's test (the actors build: build_item.bat also links src/game/static_weapon.cpp, bike.cpp and
// fish.cpp; main runs SeatActorsTest after MovingActorsTest on every model): the static weapon, the
// bike and the fish against oracles written from the original's code (the asm where the decompile
// is unreliable: the bike's and the fish's movement, the static weapon's exits, hits and boxes).
// Engine and game calls are logged in order through item_test.inc's log; the harness's records give
// the activations, volumes and messages; saved games and properties are compared byte for byte.
// Uses the earlier parts' helpers. check_item.py reads the "seat:" line.

#include "game/bike.h"
#include "game/fish.h"
#include "game/static_weapon.h"
#include "script/script.h"

// --- the caches: the fish's model ("ryba", made by the test; g_seModelFail: none) and the bike's
// animations ("bike_legs", "bike_ride"; g_seAnimMissing: not found) ------------------------------------

std::vector<std::string> g_seModelGets;
bool g_seModelFail;
I3D_frame *g_seModel;
template <>
HRESULT C_ResourceCache<I3D_model>::Get(const char *fileName, I3D_model **out, T_CacheErrorProc, void *)
{
    g_seModelGets.push_back(fileName);
    ItLog("getmodel %s", fileName);
    *out = nullptr;
    if (g_seModelFail || !g_seModel)
        return I3DERR_NOFILE;
    ++g_seModel->m_refCount;
    *out = reinterpret_cast<I3D_model *>(g_seModel);
    return I3D_OK;
}
struct S_SeAnim {
    void **vtable;
    std::string name;
    int releases = 0;
};
void *g_seAnimVtable[64];
uint32_t __stdcall F_SeAnimRelease(S_SeAnim *self)
{
    ItLog("animrelease %s", self->name.c_str());
    ++self->releases;
    return 0;
}
std::map<std::string, S_SeAnim *> g_seAnims;
std::string g_seAnimMissing;
template <>
C_ResourceCache<I3D_animation_set>::~C_ResourceCache()
{
}
template <>
HRESULT C_ResourceCache<I3D_animation_set>::OpenInto(I3D_animation_set *, const char *, uint32_t, void *, void *, int)
{
    return I3D_OK;
}
template <>
HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *fileName, I3D_animation_set **out, T_CacheErrorProc,
                                                void *)
{
    ItLog("getanim %s", fileName);
    *out = nullptr;
    if (g_seAnimMissing == fileName)
        return I3DERR_NOFILE;
    S_SeAnim *&anim = g_seAnims[fileName];
    if (!anim) {
        g_seAnimVtable[0] = reinterpret_cast<void *>(&F_SeAnimRelease);
        anim = new S_SeAnim{g_seAnimVtable, fileName};
    }
    *out = reinterpret_cast<I3D_animation_set *>(anim);
    return I3D_OK;
}
I3D_animation_set *C_AnimationCache::Create()
{
    return nullptr;
}
C_AnimationCache g_animationCache;

// --- the shadows, the hit zones, the frames hit, the scripts advanced, the actor's store saved ------------

I3D_frame *g_seShadow;
I3D_frame *C_GEffectsModule::CreateShadow(I3D_frame *model)
{
    ItLog("createshadow %s", ItName(model).c_str());
    return nullptr;
}
I3D_frame *C_GEffectsModule::GetShadow(I3D_frame *model)
{
    ItLog("getshadow %s", ItName(model).c_str());
    if (g_seShadow)
        ++g_seShadow->m_refCount;
    return g_seShadow;
}
C_HitZones::C_HitZones() {}
C_HitZones g_hitZones;
int g_seZone;
int C_HitZones::GetHitZone(C_Human *human, const S_vector &pos, const S_vector &dir)
{
    ItLog("hitzone %s %s %s", ItName(human->GetFrame()).c_str(), ItV(pos).c_str(), ItV(dir).c_str());
    return g_seZone;
}
void NotifyFrameHit(I3D_frame *frame)
{
    ItLog("framehit %s", ItName(frame).c_str());
}
void C_Script::Advance()
{
    ItLog("advance");
}
bool SaveActorItems(C_Actor *, C_chunk &chunk)
{
    chunk.Write(0x5151);
    return true;
}
std::vector<int> g_seItemLoads;
bool LoadActorItems(C_Actor *, C_chunk &chunk)
{
    int tag = -1;
    chunk.Read(tag);
    g_seItemLoads.push_back(tag);
    return true;
}
// (game/human_movement.cpp's, which the actors build does not link.)
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

// --- the road network's paths and the walking points at actors -------------------------------------------

std::vector<uint16_t> g_sePath;
bool g_sePathFound = true;
bool C_CheckpointNetwork::FindPath(int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags)
{
    ItLog("findpath %d %d %u", from, to, excludeFlags);
    if (!g_sePathFound)
        return false;
    *path = g_sePath;
    return true;
}
int g_seActorPoint = CHECKPOINT_NONE;
int C_CheckpointNetwork::FindActorPoint(const C_Actor *) const
{
    ItLog("actorpoint");
    return g_seActorPoint;
}

// --- the fish's sliding sphere: recorded; it meets something while g_seSlide is set ----------------------

struct S_SeSlide {
    S_vector from, move;
    float radius;
    uint32_t types, flags;
    bool normal;
};
std::vector<S_SeSlide> g_seSlides;
bool g_seSlide;
S_vector g_seSlideEnd, g_seSlideNormal;
S_CollisionVolume g_seSlideVolume;
S_CollisionVolume *CollisionSlideSphere(const S_vector &from, const S_vector &move, float radius, S_vector &endPos,
                                        S_vector *endNormal, uint32_t excludeTypes, uint32_t excludeFlags)
{
    g_seSlides.push_back({from, move, radius, excludeTypes, excludeFlags, endNormal != nullptr});
    if (!g_seSlide)
        return nullptr;
    endPos = g_seSlideEnd;
    if (endNormal)
        *endNormal = g_seSlideNormal;
    return &g_seSlideVolume;
}

// --- the engine slots the new actors call --------------------------------------------------------------

// The model's animation slots (the bike's rider): SetAnimation (0x84), SetAnimTime / a visual's SetMesh
// (0x9C, logged by what the first argument is), AdvanceStages (0xB0), the frame's Tick (0x20).
void __stdcall F_SeSetAnimation(I3D_frame *self, I3D_animation_set *anim, int stage, float weight, int flags)
{
    ItLog("setanim %s %s %d %.9g %d", ItName(self).c_str(),
          anim ? reinterpret_cast<S_SeAnim *>(anim)->name.c_str() : "0", stage, weight, flags);
}
std::map<const void *, std::string> g_seMeshNames;
void __stdcall F_Se9C(I3D_frame *self, uintptr_t a, int b)
{
    if (self->GetType() == FRAME_MODEL) {
        ItLog("animtime %s %d %d", ItName(self).c_str(), static_cast<int>(a), b);
        return;
    }
    auto it = g_seMeshNames.find(reinterpret_cast<const void *>(a));
    ItLog("setmesh %s %s %d", ItName(self).c_str(), it == g_seMeshNames.end() ? "?" : it->second.c_str(), b);
    *reinterpret_cast<uintptr_t *>(reinterpret_cast<uint8_t *>(self) + 0x1E0) = a;
}
void __stdcall F_SeAdvanceStages(I3D_frame *self, const int *times)
{
    ItLog("advancestages %s %d %d %d %d %d %d %d %d", ItName(self).c_str(), times[0], times[1], times[2], times[3],
          times[4], times[5], times[6], times[7]);
}
void __stdcall F_SeTick(I3D_frame *self, int time)
{
    ItLog("tick %s %d", ItName(self).c_str(), time);
}
void __stdcall F_SeSetName(I3D_frame *self, const char *name)
{
    ItLog("setname %s %s", ItName(self).c_str(), name);
    g_itNames[self] = name;
}
// A visual's own bound (slot 0x90): its stub's vertices' box (none: empty), valid then.
void __stdcall F_SeLocalBound(I3D_frame *self)
{
    I3D_bound_volume &b = *reinterpret_cast<I3D_bound_volume *>(reinterpret_cast<uint8_t *>(self) + 0x1A0);
    b.bbox = I3D_bbox::Empty();
    auto it = g_stubs.find(self);
    if (it != g_stubs.end()) {
        const std::vector<float> &v = it->second->vertices;
        for (size_t i = 0; i + 2 < v.size(); i += 8)
            b.bbox.Expand(S_vector(v[i], v[i + 1], v[i + 2]));
    }
    static_cast<I3D_visual *>(self)->VisualFlags() |= VISFLAGS_BOUND_VALID;
}
// The driver's visuals (the fish: "fish<n>" by creation; none once g_seVisualsLeft is 0).
int g_seVisualsLeft = -1, g_seVisualsMade;
I3D_frame *__stdcall F_SeCreateVisual(void *, uint32_t type)
{
    ItLog("createvisual %x", type);
    if (g_seVisualsLeft == 0)
        return nullptr;
    if (g_seVisualsLeft > 0)
        --g_seVisualsLeft;
    I3D_frame *f = MakeFrame(g_frameVtable, FRAME_VISUAL, false);
    g_itNames[f] = "fish" + std::to_string(g_seVisualsMade++);
    return f;
}

// --- the test ----------------------------------------------------------------------------------------------

struct S_Se {
    bool fish = true, fishProps = true, fishStart = true, fishTick = true, fishUpdate = true, fishSave = true,
         fishStop = true, bike = true, bikeProps = true, bikeStart = true, bikeDrive = true, bikeUpdate = true,
         bikeSave = true, bikeStop = true, sw = true, swStart = true, swState = true, swNet = true, swSeats = true,
         swExit = true, swHit = true, swTick = true, swSave = true, swBox = true;
};
#define SE(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

// S_vector::Normalize as the game's inline copies compile (the square summed by the caller).
void SeNormalize(S_vector &v, float sq)
{
    if (std::fabs(sq - 1.0f) < 1e-08f)
        return;
    if (!(sq < 1e-08f)) {
        const float k = 1.0f / std::sqrt(sq);
        v = S_vector(v.x * k, v.y * k, v.z * k);
    } else if (v.x != 0.0f) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (v.z != 0.0f) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}
// RandomFloat's next values (world_actors_test.inc's fake), without moving it on.
struct S_SeRandom {
    uint32_t state;
    float Next(float max)
    {
        state = state * 1103515245u + 12345u;
        return static_cast<float>((state >> 8) & 0xFFFF) / 65536.0f * max;
    }
};
std::string SeLine(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    return text;
}

void SeatActorsTest()
{
    const int frameSlots[] = {0x00, 0x08, 0x0C, 0x1C, 0x20, 0x24, 0x30, 0x34, 0x38, 0x3C, 0x4C, 0x54, 0x58, 0x84, 0x90, 0x9C, 0xB0};
    constexpr int FRAME_SLOTS = sizeof frameSlots / sizeof frameSlots[0];
    void *oldFrame[FRAME_SLOTS];
    for (int i = 0; i < FRAME_SLOTS; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x20 / 4] = reinterpret_cast<void *>(&F_SeTick);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_SeSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x4C / 4] = reinterpret_cast<void *>(&F_ObDuplicate);
    g_frameVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ItSetHierarchyAlpha);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_WaUpdateBound);
    g_frameVtable[0x84 / 4] = reinterpret_cast<void *>(&F_SeSetAnimation);
    g_frameVtable[0x90 / 4] = reinterpret_cast<void *>(&F_SeLocalBound);
    g_frameVtable[0x9C / 4] = reinterpret_cast<void *>(&F_Se9C);
    g_frameVtable[0xB0 / 4] = reinterpret_cast<void *>(&F_SeAdvanceStages);
    void *const oldCreateFrame = g_driverVtable[0x54 / 4];
    void *const oldCreateVisual = g_driverVtable[0x50 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ObCreateFrame);
    g_driverVtable[0x50 / 4] = reinterpret_cast<void *>(&F_SeCreateVisual);
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    void *const oldFind = g_sceneVtable[0x7C / 4];
    void *const oldSector = g_sceneVtable[0xEC / 4];
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_MaFindFrame);
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_ItUpdateFrameSector);
    I3D_camera *const oldCamera = *reinterpret_cast<I3D_camera **>(reinterpret_cast<uint8_t *>(g_carScene) + 0x15C);
    const uint8_t oldRole = g_gameRole;
    const uint32_t oldMissionTime = GetGame()->m_missionTime;
    const bool oldCoop = g_coop, oldFirstPerson = g_firstPerson, oldTextIds = g_textIds;
    C_Checkpoints *const oldCheckpoints = GetGame()->m_checkpoints;
    const uint32_t oldRandom = g_waRandom;
    S_Se r;

    int seed = static_cast<int>(g_frames.size()) * 23 + 5;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_seSlides.clear();
        ClearStreams();
    };
    const auto setCamera = [&](I3D_frame *camera) {
        *reinterpret_cast<I3D_frame **>(reinterpret_cast<uint8_t *>(g_carScene) + 0x15C) = camera;
    };

#include "seat_fish.inc"
#include "seat_bike.inc"
#include "seat_static_weapon.inc"

    g_gameRole = oldRole;
    GetGame()->m_missionTime = oldMissionTime;
    g_coop = oldCoop;
    g_firstPerson = oldFirstPerson;
    g_textIds = oldTextIds;
    g_waRandom = oldRandom;
    g_seModelFail = false;
    g_seModel = nullptr;
    g_seAnimMissing.clear();
    g_seShadow = nullptr;
    g_seSlide = false;
    g_seVisualsLeft = -1;
    g_seActorPoint = CHECKPOINT_NONE;
    g_cacheFill = nullptr;
    g_cacheFailName.clear();
    g_netFindNone = false;
    g_readBits.clear();
    g_readPos = 0;
    g_sePathFound = true;
    g_sePath.clear();
    g_maSceneFrames.clear();
    *reinterpret_cast<I3D_camera **>(reinterpret_cast<uint8_t *>(g_carScene) + 0x15C) = oldCamera;
    // (The next model frees this one's frames: none left in the primary sector's children, none of
    // their names kept for the addresses.)
    g_sector->m_firstChild = nullptr;
    for (S_StubFrame *stub : g_frames)
        g_itNames.erase(stub->frame);
    GetGame()->m_checkpoints = oldCheckpoints;
    GetGame()->m_scene = oldScenePtr;
    g_sceneVtable[0x7C / 4] = oldFind;
    g_sceneVtable[0xEC / 4] = oldSector;
    for (int i = 0; i < FRAME_SLOTS; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    g_driverVtable[0x54 / 4] = oldCreateFrame;
    g_driverVtable[0x50 / 4] = oldCreateVisual;
    reset();
    std::printf("seat: fish %d fishProps %d fishStart %d fishTick %d fishUpdate %d fishSave %d fishStop %d bike %d "
                "bikeProps %d bikeStart %d bikeDrive %d bikeUpdate %d bikeSave %d bikeStop %d sw %d swStart %d "
                "swState %d swNet %d swSeats %d swExit %d swHit %d swTick %d swSave %d swBox %d\n",
                r.fish, r.fishProps, r.fishStart, r.fishTick, r.fishUpdate, r.fishSave, r.fishStop, r.bike, r.bikeProps,
                r.bikeStart, r.bikeDrive, r.bikeUpdate, r.bikeSave, r.bikeStop, r.sw, r.swStart, r.swState, r.swNet,
                r.swSeats, r.swExit, r.swHit, r.swTick, r.swSave, r.swBox);
}
