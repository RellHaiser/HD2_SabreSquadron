// Part 7c's test (the actors build: build_item.bat also links src/game/explosive_place.cpp,
// ladder.cpp, pike.cpp and fowl_generator.cpp; main runs WorldActorsTest after ObjectiveActorsTest on
// every model): the explosive place, the ladder, the pike and the fowl generator against oracles
// written from the original's code (the asm where the decompile is unreliable: the ladder's
// StartMission and CanUse, the pike's turn and volume, the birds' square). Engine and game calls are
// logged in order through item_test.inc's log; the harness's records give the activations,
// volumes, rays, sounds, explosions, messages and script uses; saved games and properties are
// compared byte for byte. Uses simple_actors_test.inc's and objective_actors_test.inc's helpers.
// check_item.py reads the "world:" line.

#include <limits>

#include "game/explosive_place.h"
#include "game/fowl_generator.h"
#include "game/ladder.h"
#include "game/pike.h"

// --- the game's and the engine's side -------------------------------------------------------------------

// Random numbers (RandomFloat): a fixed sequence of fractions of `max`, logged.
uint32_t g_waRandom = 12345;
float RandomFloat(float max)
{
    g_waRandom = g_waRandom * 1103515245u + 12345u;
    const float r = static_cast<float>((g_waRandom >> 8) & 0xFFFF) / 65536.0f * max;
    ItLog("random %.9g", max);
    return r;
}
// The dynamic ray tests (the pike's): recorded; the `g_waBlockAt`-th one (from 0) hits.
struct S_WaRay {
    S_vector from, dir;
    uint32_t types, flags;
    bool hitPos, hitNormal;
};
std::vector<S_WaRay> g_waRays;
int g_waBlockAt = -1;
S_CollisionVolume g_waWall;
S_CollisionVolume *C_CollisionWorld::RayTestDynamic(const S_vector &from, const S_vector &dir, S_vector *hitPos,
                                                     S_vector *hitNormal, uint32_t excludeTypes,
                                                     uint32_t excludeFlags) const
{
    g_waRays.push_back({from, dir, excludeTypes, excludeFlags, hitPos != nullptr, hitNormal != nullptr});
    return static_cast<int>(g_waRays.size()) - 1 == g_waBlockAt ? &g_waWall : nullptr;
}
// The sound module's loops and listeners.
std::vector<int> g_waStops;
void C_SoundModule::Stop(int *slot)
{
    ItLog("stopsound %d", *slot);
    g_waStops.push_back(*slot);
}
T_SoundHeardProc g_waListener;
I3D_frame *g_waListenerFrame;
int C_SoundModule::AddListener(I3D_frame *frame, T_SoundHeardProc callback)
{
    ItLog("addlistener %s", ItName(frame).c_str());
    g_waListener = callback;
    g_waListenerFrame = frame;
    return 77;
}
bool C_SoundModule::RemoveListener(int index)
{
    ItLog("removelistener %d", index);
    return true;
}
// The actors (the first 32: the players').
std::vector<C_Actor *> g_waActors;
const std::vector<C_Actor *> &C_ActorsModule::GetActors() const
{
    return g_waActors;
}
// The birds: the mission's, the spare models; each bird's calls logged by its model's name.
std::vector<C_Bird *> g_birds;
std::vector<I3D_frame *> g_spareBirdModels;
std::set<const C_Bird *> g_waGoneBirds;                // Update answers false for these
C_Bird::C_Bird(I3D_frame *model, bool ownModel)
{
    std::memset(m_unknown00, 0, sizeof m_unknown00);
    std::memset(m_unknown54, 0, sizeof m_unknown54);
    std::memset(m_unknown61, 0, sizeof m_unknown61);
    m_model = model;
    m_ownModel = ownModel;
    ItLog("bird %s %d", ItName(model).c_str(), ownModel);
}
C_Bird::~C_Bird()
{
    ItLog("delete bird %s", ItName(m_model).c_str());
    if (!m_ownModel)
        g_spareBirdModels.push_back(m_model);
}
bool C_Bird::Update(int time)
{
    ItLog("fly %s %d", ItName(m_model).c_str(), time);
    return !g_waGoneBirds.count(this);
}
void C_Bird::FlyAway()
{
    ItLog("flyaway %s", ItName(m_model).c_str());
}
// A model's copy (I3D_model::DuplicateModel, frame slot 0xD8).
void __stdcall F_WaDuplicateModel(I3D_frame *self, I3D_frame *source)
{
    ItLog("duplicate %s %s", ItName(self).c_str(), ItName(source).c_str());
}
// The frames' bound (slot 0x58): a sphere above the frame.
void __stdcall F_WaUpdateBound(I3D_frame *self)
{
    ItLog("bound %s", ItName(self).c_str());
    self->m_frameFlags |= FRMFLAGS_BOUND_VALID;
    self->m_bound.center = S_vector(0.25f, 0.5f, -0.75f);
    self->m_bound.radius = 1.5f;
}
// The ground below the made birds (the fowl generator's mode 2).
float g_waGround = -1000.0f;
S_CollisionVolume g_waGroundVolume;
S_CollisionVolume *WaGroundRay(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *,
                               uint32_t types, uint32_t flags)
{
    ItLog("ray %s %s %u %u", ItV(from).c_str(), ItV(dir).c_str(), types, flags);
    if (from.y < g_waGround)
        return nullptr;
    if (hitPos)
        *hitPos = S_vector(from.x, g_waGround, from.z);
    return &g_waGroundVolume;
}

// --- the test ----------------------------------------------------------------------------------------------

struct S_Wa {
    bool explosive = true, explosiveState = true, explosiveNet = true, explosiveSave = true, ladder = true,
         ladderUse = true, ladderVolumes = true, ladderProps = true, pike = true, pikeTurn = true, pikeNet = true,
         pikeSave = true, fowl = true, fowlBirds = true, fowlTrigger = true, fowlSave = true;
};
#define WA(flag, ...) RbExpect(flag, (__VA_ARGS__), __LINE__)

void WorldActorsTest()
{
    const int frameSlots[] = {0x00, 0x08, 0x0C, 0x1C, 0x24, 0x30, 0x34, 0x38, 0x3C, 0x58, 0xD8};
    void *oldFrame[11];
    for (int i = 0; i < 11; ++i)
        oldFrame[i] = g_frameVtable[frameSlots[i] / 4];
    g_frameVtable[0x00 / 4] = reinterpret_cast<void *>(&F_ItRelease);
    g_frameVtable[0x08 / 4] = reinterpret_cast<void *>(&F_ObSetPos);
    g_frameVtable[0x0C / 4] = reinterpret_cast<void *>(&F_ObSetDir);
    g_frameVtable[0x1C / 4] = reinterpret_cast<void *>(&F_ObSetWorldDir);
    g_frameVtable[0x24 / 4] = reinterpret_cast<void *>(&F_ItUpdate);
    g_frameVtable[0x30 / 4] = reinterpret_cast<void *>(&F_ItSetOn);
    g_frameVtable[0x34 / 4] = reinterpret_cast<void *>(&F_AnSetName);
    g_frameVtable[0x38 / 4] = reinterpret_cast<void *>(&F_ItLinkTo);
    g_frameVtable[0x3C / 4] = reinterpret_cast<void *>(&F_AnNumChildren);
    g_frameVtable[0x58 / 4] = reinterpret_cast<void *>(&F_WaUpdateBound);
    g_frameVtable[0xD8 / 4] = reinterpret_cast<void *>(&F_WaDuplicateModel);
    const int sceneSlots[] = {0x80, 0x84, 0xE0, 0xEC};
    void *oldScene[4];
    for (int i = 0; i < 4; ++i)
        oldScene[i] = g_sceneVtable[sceneSlots[i] / 4];
    g_sceneVtable[0x80 / 4] = reinterpret_cast<void *>(&F_ObAddFrame);
    g_sceneVtable[0x84 / 4] = reinterpret_cast<void *>(&F_ObRemoveFrame);
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_ItGetSector);
    g_sceneVtable[0xEC / 4] = reinterpret_cast<void *>(&F_ItUpdateFrameSector);
    void *const oldCreateFrame = g_driverVtable[0x54 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_ObCreateFrame);
    I3D_scene *const oldScenePtr = GetGame()->m_scene;
    GetGame()->m_scene = g_carScene;
    const uint8_t oldRole = g_gameRole;
    void (*const oldFill)(I3D_model *) = g_cacheFill;
    g_cacheFill = [](I3D_model *) {};
    g_textIds = true;
    g_reliableActorStreams = true;
    S_Wa r;

    std::vector<I3D_bbox> shapes;
    for (S_StubFrame *s : g_frames) {
        const I3D_bbox b = RbBox(s->frame);
        if (shapes.size() < 3 && b.min.x <= b.max.x && b.max.x - b.min.x > 0.05f && b.max.y - b.min.y > 0.05f &&
            b.max.z - b.min.z > 0.05f)
            shapes.push_back(b);
    }
    while (shapes.size() < 3) {
        I3D_bbox b;
        b.min = S_vector(-0.3f, -0.2f, -0.9f - shapes.size());
        b.max = S_vector(0.7f, 0.8f, 1.1f + shapes.size());
        shapes.push_back(b);
    }
    int seed = static_cast<int>(g_frames.size()) * 31 + 7;
    const auto rnd = [&](float lo, float hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    int frameNo = 0;
    const auto placed = [&](E_I3D_FRAME_TYPE type, const std::string &name, bool parented) {
        I3D_frame *parent = nullptr;
        if (parented) {
            parent = AnFrame(FRAME_DUMMY, name + ".p" + std::to_string(frameNo), nullptr);
            parent->m_localMatrix.m[3][0] = rnd(-30, 30);
            parent->m_localMatrix.m[3][2] = rnd(-30, 30);
            parent->m_localMatrix.m[1][1] = rnd(0.5f, 2.0f);
        }
        I3D_frame *f = AnFrame(type, name + std::to_string(frameNo++), parent);
        RbTilt(f, rnd(-1, 1), rnd(-1, 1), rnd(0, 1) > 0.5f);
        f->m_localMatrix.m[3][0] = rnd(-10, 10);
        f->m_localMatrix.m[3][1] = rnd(-2, 4);
        f->m_localMatrix.m[3][2] = rnd(-10, 10);
        f->m_scale = S_vector(rnd(0.5f, 2), rnd(0.5f, 2), rnd(0.5f, 2));
        return f;
    };
    const auto boxFrame = [&](I3D_frame *parent, const char *name, int k) {
        const I3D_bbox &b = shapes[static_cast<size_t>(k) % shapes.size()];
        I3D_frame *f = RbBoxFrame(parent, name, b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
                                  S_vector(rnd(-1, 1), rnd(0, 1), rnd(-1, 1)));
        f->m_name = Stub(f).name.c_str();
        return f;
    };
    S_FrameData scriptData = {};
    scriptData.script = reinterpret_cast<C_Script *>(&scriptData);
    const auto reset = [&] {
        g_itLog.clear();
        g_activations.clear();
        g_deactivations.clear();
        g_volumeLog.clear();
        g_soundRequests.clear();
        g_uses.clear();
        ClearStreams();
        g_itTemps.clear();
        g_explosions.clear();
        g_explosionsShown.clear();
        g_actorLoads.clear();
        g_waRays.clear();
        g_waStops.clear();
        g_cacheLoads.clear();
    };
    const auto lastStream = [&]() -> S_FakeStream * { return g_streams.empty() ? nullptr : g_streams.back(); };
    const auto newStream = [&] {
        g_streams.push_back(new S_FakeStream{0, false, {}, false});
        return reinterpret_cast<C_BitStream *>(g_streams.back());
    };
    using T_Bits = std::vector<std::pair<uint32_t, int>>;
    // A sound request as the actors fill it (the rest the defaults).
    const auto soundIs = [](const S_SoundRequest &q, const S_vector &pos, float posW, uint32_t flags, int chapter,
                            int group) {
        return RbSame(q.pos, pos) && q.posW == posW && q.flags == flags && q.attachTo == nullptr && q.source == nullptr &&
               q.chapter == chapter && q.group == group && q.fileName == nullptr && q.channel == SOUND_CHANNEL_NONE &&
               q.volume == 1.0f && q.nearScale == 1.0f && q.farScale == 1.0f;
    };

#include "world_explosive_ladder.inc"
#include "world_pike_fowl.inc"

    g_gameRole = oldRole;
    g_textIds = false;
    g_reliableActorStreams = false;
    g_cacheFill = oldFill;
    g_soundSlot = 0;
    g_rayTestHook = nullptr;
    GetGame()->m_scene = oldScenePtr;
    for (int i = 0; i < 11; ++i)
        g_frameVtable[frameSlots[i] / 4] = oldFrame[i];
    for (int i = 0; i < 4; ++i)
        g_sceneVtable[sceneSlots[i] / 4] = oldScene[i];
    g_driverVtable[0x54 / 4] = oldCreateFrame;
    reset();
    std::printf("world: explosive %d explosiveState %d explosiveNet %d explosiveSave %d ladder %d ladderUse %d "
                "ladderVolumes %d ladderProps %d pike %d pikeTurn %d pikeNet %d pikeSave %d fowl %d fowlBirds %d "
                "fowlTrigger %d fowlSave %d\n",
                r.explosive, r.explosiveState, r.explosiveNet, r.explosiveSave, r.ladder, r.ladderUse, r.ladderVolumes,
                r.ladderProps, r.pike, r.pikeTurn, r.pikeNet, r.pikeSave, r.fowl, r.fowlBirds, r.fowlTrigger,
                r.fowlSave);
}
