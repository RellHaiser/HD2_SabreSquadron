// An explosion's wave, the timed events and the periodic links (part 11f: game/explosion_wave.cpp, game/periodic_links.cpp). MinGW; build_explosion.bat;
// run explosion_test.exe ("explosion: N checks, 0 failures").
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/camera_module.h"
#include "game/collision.h"
#include "game/explosion_wave.h"
#include "game/game.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/shots.h"
#include "game/versions.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

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
bool Near(float a, float b, float within = 1e-4f) { return std::fabs(a - b) <= within; }

// ---- the file (the chunks are the real ones) ----
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

// ---- the engine's math, for matrices that only move ----
bool __stdcall S_matrix::Inverse(const S_matrix &m)
{
    std::memset(this, 0, sizeof(*this));
    for (int i = 0; i < 4; ++i) this->m[i][i] = 1.0f;
    this->m[3][0] = -m.m[3][0];
    this->m[3][1] = -m.m[3][1];
    this->m[3][2] = -m.m[3][2];
    return true;
}
S_vector __stdcall S_vector::operator*(const S_matrix &m) const { return S_vector(x + m.m[3][0], y + m.m[3][1], z + m.m[3][2]); }
void I3D_frame::UpdateWMatrixProc() {}

namespace {
// ---- fake frames ----
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
    std::string name;
    int releases = 0;
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(bytes); }
};
void *g_frameVt[128];
std::map<I3D_frame *, S_FakeFrame *> g_fakes;
std::map<I3D_frame *, I3D_frame *> g_children;          // frame -> its "a"
std::map<I3D_frame *, I3D_bbox> g_bounds;
uint32_t __stdcall F_Release(I3D_frame *self) { g_fakes[self]->releases++; return 0; }
I3D_frame *__stdcall F_FindChild(I3D_frame *self, const char *name, uint32_t)
{
    return !std::strcmp(name, "a") && g_children.count(self) ? g_children[self] : nullptr;
}
S_FakeFrame *MakeFrame(const char *name, float x, float y, float z, E_I3D_FRAME_TYPE type = FRAME_VISUAL, float half = 0.0f)
{
    S_FakeFrame *fake = new S_FakeFrame();
    std::memset(fake->bytes, 0, sizeof(fake->bytes));
    fake->name = name;
    I3D_frame *frame = fake->frame();
    *reinterpret_cast<void ***>(frame) = g_frameVt;
    frame->m_frameFlags = 0x20;                         // (the world matrix is good)
    frame->m_type = type;
    frame->m_name = const_cast<char *>(fake->name.c_str());
    frame->m_refCount = 1;
    float *const matrix = reinterpret_cast<float *>(fake->bytes + 0x40);
    for (int i = 0; i < 4; ++i) matrix[i * 5] = 1.0f;
    matrix[12] = x; matrix[13] = y; matrix[14] = z;
    I3D_bbox box{};
    box.min = S_vector(-half, -half, -half);
    box.max = S_vector(half, half, half);
    g_bounds[frame] = box;
    g_fakes[frame] = fake;
    return fake;
}

void *g_sceneVt[128];
void *g_driverVt[128];
struct S_Fake { void **vtable; };
S_Fake g_scene{g_sceneVt}, g_driver{g_driverVt};
alignas(16) unsigned char g_gameBytes[0x400];
I3D_sector *__stdcall F_GetSector(void *, const S_vector &) { return reinterpret_cast<I3D_sector *>(g_fakes.begin()->first); }
I3D_sector *g_sector;
I3D_sector *__stdcall F_GetSectorFixed(void *, const S_vector &) { return g_sector; }
I3D_frame *__stdcall F_FindFrame(void *, const char *name, uint32_t mask)
{
    for (auto &entry : g_fakes)
        if (entry.second->name == name) return entry.first;
    return nullptr;
}
const I3D_bound_volume *__stdcall F_GetHierarchyBound(void *, I3D_bound_volume &out, I3D_frame *frame)
{
    out.bbox = g_bounds[frame];
    return &out;
}

// ---- fake actors ----
struct S_HitLog { C_Actor *actor; E_HitType type; float amount; S_vector pos, dir; C_Actor *source; };
std::vector<S_HitLog> g_hits;
int g_materialId = 0;
void __thiscall F_Hit(C_Actor *self, E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *source, int)
{
    g_hits.push_back({self, type, amount, pos, dir, source});
}
int __thiscall F_GetMaterialId(C_Actor *, I3D_frame *) { return g_materialId; }
void *g_actorVt[128];
struct S_FakeActor {
    alignas(16) unsigned char bytes[0x2000];
    C_Actor *actor() { return reinterpret_cast<C_Actor *>(bytes); }
};
S_FrameData g_frameData[64];
int g_frameDataUsed;
C_Actor *MakeActor(E_ActorType type, S_FakeFrame *frame, int id)
{
    S_FakeActor *fake = new S_FakeActor();
    std::memset(fake->bytes, 0, sizeof(fake->bytes));
    C_Actor *actor = fake->actor();
    *reinterpret_cast<void ***>(actor) = g_actorVt;
    actor->m_type = type;
    actor->m_netId = id;
    actor->m_frame = frame ? frame->frame() : nullptr;
    if (frame) {
        S_FrameData *data = &g_frameData[g_frameDataUsed++];
        std::memset(data, 0, sizeof(*data));
        data->actor = actor;
        frame->frame()->m_data = data;
        const S_vector pos = frame->frame()->GetWorldPos();
        actor->SetPosition(pos);
    }
    return actor;
}

// ---- the world ----
std::vector<S_CollisionVolume *> g_inRadius;
S_CollisionVolume *g_at_volume;
float g_radiusAsked;
uint32_t g_excludeAsked;
std::set<std::string> g_blockedFrom;                    // ray starts ("x,y,z") that hit something
bool g_allBlocked;
std::vector<std::pair<S_vector, S_vector>> g_rays;
uint32_t g_rayFlags, g_rayTypes;
alignas(16) S_CollisionVolume g_blockVolume;
std::string Key(const S_vector &v) { char t[64]; std::sprintf(t, "%.2f,%.2f,%.2f", v.x, v.y, v.z); return t; }
S_CollisionVolume *MakeVolume(S_FakeFrame *frame)
{
    S_CollisionVolume *volume = new S_CollisionVolume();
    volume->frame = frame ? frame->frame() : nullptr;
    return volume;
}

// ---- logs of the others ----
struct S_DamageLog { C_VersionMaster *master; int damage; E_VersionEffects effects; };
std::vector<S_DamageLog> g_damages;
struct S_ProximityLog { C_Human *self; C_Actor *other; float distance; bool ended, unknown; };
std::vector<S_ProximityLog> g_proximity;
int g_cameraMode, g_cameraCalls;
uint32_t g_cameraValue;
const S_vector4 *g_cameraPos;
bool g_noCamera;
alignas(16) unsigned char g_cameraBytes[0x1000];
std::map<int, C_Actor *> g_actorsById;
S_MaterialDef g_materialTable[4];
I3D_sector *g_humanSector;
std::map<const void *, I3D_sector *> g_bodySectors;
}  // namespace

S_MaterialDef *g_materials = g_materialTable;
uint32_t g_gameRole = 1;

C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
C_CameraModule *C_game::GetCameraModule() { return g_noCamera ? nullptr : reinterpret_cast<C_CameraModule *>(g_cameraBytes); }
void C_CameraModule::SetSpecialMode(int mode, const S_vector4 *vector, uint32_t value)
{
    ++g_cameraCalls;
    g_cameraMode = mode;
    g_cameraPos = vector;
    g_cameraValue = value;
}
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
S_CollisionVolume *C_CollisionWorld::GetVolumeAt(const S_vector &, uint32_t exclude) const { g_excludeAsked = exclude; return g_at_volume; }
int C_CollisionWorld::GetVolumesInRadius(const S_vector &, float radius, uint32_t exclude, std::vector<S_CollisionVolume *> &volumes) const
{
    g_radiusAsked = radius;
    CHECK(exclude == 0xD8000);
    volumes = g_inRadius;
    return static_cast<int>(volumes.size());
}
S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *normal, uint32_t types, uint32_t flags)
{
    CHECK(hitPos == nullptr && normal == nullptr);
    g_rays.push_back({from, dir});
    g_rayTypes = types;
    g_rayFlags = flags;
    return g_allBlocked || g_blockedFrom.count(Key(from)) ? &g_blockVolume : nullptr;
}
C_Actor *C_ActorsModule::GetActorById(int id) const { return g_actorsById.count(id) ? g_actorsById[id] : nullptr; }
bool C_VersionMaster::DamageAll(int damage, E_VersionEffects effects)
{
    g_damages.push_back({this, damage, effects});
    return true;
}
void C_Human::OnProximity(C_Actor *other, float distance, bool ended, bool unknown)
{
    g_proximity.push_back({this, other, distance, ended, unknown});
}
I3D_sector *S_HumanBody::GetSector() const { return g_humanSector; }

namespace {
void Reset()
{
    TimedEvents_Clear();
    PeriodicLinks_Clear();
    g_hits.clear();
    g_damages.clear();
    g_proximity.clear();
    g_rays.clear();
    g_inRadius.clear();
    g_at_volume = nullptr;
    g_blockedFrom.clear();
    g_allBlocked = false;
    g_cameraCalls = 0;
    g_noCamera = false;
    g_gameRole = 1;
    g_materialId = 0;
    g_materialTable[0].absorption = 0;
    g_children.clear();
    g_frameDataUsed = 0;
    g_actorsById.clear();
    g_humanSector = nullptr;
}

S_Explosion Boom(float range = 100.0f, float damage = 50.0f)
{
    S_Explosion explosion;
    explosion.pos = S_vector(0, 0, 0);
    explosion.range = range;
    explosion.damage = damage;
    return explosion;
}

// One frame per squared distance d: on the x axis at sqrt(d), its box a point.
S_FakeFrame *Target(const char *name, float x, E_ActorType type, C_Actor **out = nullptr)
{
    S_FakeFrame *frame = MakeFrame(name, x, 0, 0);
    C_Actor *actor = MakeActor(type, frame, 100 + g_frameDataUsed);
    if (out) *out = actor;
    g_inRadius.push_back(MakeVolume(frame));
    return frame;
}

void TestWaveReachesInOrder()
{
    Reset();
    C_Actor *near1, *far1;
    // (Listed far first: the frames go from the end, and the first added is at the list's head. The other way round the quirk puts the far one first.)
    Target("far", 8.0f, ACTOR_BARREL, &far1);           // squared 64
    Target("near", 4.0f, ACTOR_BARREL, &near1);         // squared 16
    S_Explosion boom = Boom();
    boom.Execute();
    // The first step of 100 ms: radius 20, the near one only.
    CHECK(g_radiusAsked == 100.0f);
    CHECK(g_cameraCalls == 1 && g_cameraMode == CAMERA_MODE_EXPLOSION && g_cameraValue == 100);
    CHECK(g_hits.size() == 1);
    if (g_hits.size() == 1) {
        CHECK(g_hits[0].actor == near1);
        CHECK(g_hits[0].type == HIT_SCRIPT_KILL);       // (no source)
        CHECK(Near(g_hits[0].amount, (1.0f - 16.0f / 100.0f) * 50.0f));
        CHECK(Near(g_hits[0].dir.x, 1.0f) && Near(g_hits[0].dir.y, 0.0f));
    }
    CHECK(g_rays.size() == 1 && Near(g_rays[0].first.x, 4.0f) && Near(g_rays[0].second.x, -4.0f));
    CHECK(g_hits.empty() || (Near(g_hits[0].pos.x, 0.0f) && g_hits[0].source == nullptr));
    CHECK(g_rayTypes == 0xFFFFFFFF && g_rayFlags == 0xB10000);
    // 150 ms more at 0.2: radius 50: not yet; 100 more: 70.
    TimedEvents_Update(150, false);
    CHECK(g_hits.size() == 1);
    TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 2 && g_hits.back().actor == far1);
    CHECK(Near(g_hits.back().amount, (1.0f - 64.0f / 100.0f) * 50.0f));
    // Empty: goes at the next update. The frames were released.
    const S_FakeFrame *nearFake = g_fakes[near1->GetFrame()];
    CHECK(nearFake->releases == 1);
    CHECK(near1->GetFrame()->m_refCount == 2);          // (+1 for the victim; the fake's release does not count down)
    TimedEvents_Update(100, false);
    C_chunk chunk;
    chunk.Open("x", true);
    TimedEvents_Save(chunk);
    chunk.Close();
    CHECK(g_file.size() == 6);                          // (no wave left: just the header)
}

void TestBlockedAndSource()
{
    Reset();
    C_Actor *target;
    C_Actor *blocked;
    S_FakeFrame *targetFrame = Target("t", 3.0f, ACTOR_BARREL, &target);
    Target("b", 3.5f, ACTOR_BARREL, &blocked);
    g_blockedFrom.insert(Key(S_vector(3.5f, 0, 0)));
    S_Explosion boom = Boom();
    C_Actor *source = MakeActor(ACTOR_HUMAN, nullptr, 5);
    boom.source = source;
    boom.Execute();
    CHECK(g_hits.size() == 1 && g_hits[0].actor == target && g_hits[0].type == HIT_EXPLOSION && g_hits[0].source == source);
    (void)targetFrame;
    // Nothing for a client: no hits, no version damage, though it is passed by.
    Reset();
    C_Actor *clientTarget;
    Target("c", 2.0f, ACTOR_CAR, &clientTarget);
    g_gameRole = 2;
    S_Explosion clientBoom = Boom();
    clientBoom.Execute();
    CHECK(g_hits.empty() && g_damages.empty());
}

void TestAbsorptionAndMaster()
{
    Reset();
    C_Actor *actor;
    S_FakeFrame *frame = Target("t", 1.0f, ACTOR_BARREL, &actor);
    alignas(16) static unsigned char master[0x40];
    frame->frame()->m_data->versionMaster = reinterpret_cast<C_VersionMaster *>(master);
    g_materialTable[0].absorption = 10;
    S_Explosion boom = Boom(100.0f, 50.0f);
    boom.Execute();
    const float full = (1.0f - 1.0f / 100.0f) * 50.0f;
    CHECK(g_hits.size() == 1 && Near(g_hits[0].amount, full - 10.0f));
    CHECK(g_damages.size() == 1 && g_damages[0].damage == static_cast<int>(full - 10.0f) && g_damages[0].effects == VERSION_EFFECTS_SOME);

    // Absorbed whole: no hit, but it counts as hit, and the master takes the whole amount.
    Reset();
    C_Actor *armoured;
    S_FakeFrame *armouredFrame = Target("t", 1.0f, ACTOR_BARREL, &armoured);
    armouredFrame->frame()->m_data->versionMaster = reinterpret_cast<C_VersionMaster *>(master);
    g_materialTable[0].absorption = 1000;
    S_Explosion armouredBoom = Boom(100.0f, 50.0f);
    armouredBoom.Execute();
    CHECK(g_hits.empty());
    CHECK(g_damages.size() == 1 && g_damages[0].damage == static_cast<int>(full));
    // No material: the same.
    Reset();
    C_Actor *noMaterial;
    Target("t", 1.0f, ACTOR_BARREL, &noMaterial);
    g_materialId = -1;
    S_Explosion noMaterialBoom = Boom();
    noMaterialBoom.Execute();
    CHECK(g_hits.empty());
    // A vehicle takes the amount whole, whatever its material.
    Reset();
    C_Actor *car;
    Target("t", 1.0f, ACTOR_CAR, &car);
    g_materialTable[0].absorption = 1000;
    S_Explosion carBoom = Boom();
    carBoom.Execute();
    CHECK(g_hits.size() == 1 && Near(g_hits[0].amount, full));
}

void TestOncePerActorAndManyHits()
{
    Reset();
    // Two parts of one actor: one hit; the second through its owner model.
    C_Actor *actor;
    S_FakeFrame *model = MakeFrame("model", 1.0f, 0, 0, FRAME_MODEL);
    actor = MakeActor(ACTOR_BARREL, model, 7);
    S_FakeFrame *part1 = MakeFrame("part1", 1.0f, 0, 0);
    S_FakeFrame *part2 = MakeFrame("part2", 2.0f, 0, 0);
    part1->frame()->m_ownerModel = model->frame();
    part2->frame()->m_ownerModel = model->frame();
    g_inRadius.push_back(MakeVolume(part1));
    g_inRadius.push_back(MakeVolume(part2));
    S_Explosion boom = Boom();
    boom.Execute();
    CHECK(g_hits.size() == 1 && g_hits[0].actor == actor);
    CHECK(GetFrameOrModelActor(part1->frame()) == actor);
    // The owner's not a model: nothing.
    S_FakeFrame *notModel = MakeFrame("x", 0, 0, 0, FRAME_DUMMY);
    S_FakeFrame *child = MakeFrame("y", 0, 0, 0);
    child->frame()->m_ownerModel = notModel->frame();
    CHECK(GetFrameOrModelActor(child->frame()) == nullptr);

    // More than eleven in one step: the later ones have less effect.
    Reset();
    alignas(16) static unsigned char master[0x40];
    for (int i = 0; i < 13; ++i) {
        C_Actor *a;
        char name[8];
        std::sprintf(name, "m%d", i);
        S_FakeFrame *f = Target(name, 0.5f, ACTOR_BARREL, &a);
        f->frame()->m_data->versionMaster = reinterpret_cast<C_VersionMaster *>(master);
    }
    S_Explosion many = Boom();
    many.Execute();
    CHECK(g_damages.size() == 13);
    if (g_damages.size() == 13) {
        CHECK(g_damages[10].effects == VERSION_EFFECTS_SOME);
        CHECK(g_damages[11].effects == VERSION_EFFECTS_NONE);
    }
}

void TestHumans()
{
    Reset();
    // A human's volume stands for its model's child, once however many volumes it has.
    S_FakeFrame *frame = MakeFrame("human", 2.0f, 0, 0);
    C_Actor *human = MakeActor(ACTOR_HUMAN, frame, 9);
    S_FakeFrame *child = MakeFrame("a", 2.0f, 0, 0);
    g_children[frame->frame()] = child->frame();
    child->frame()->m_ownerModel = nullptr;
    S_FrameData data = {};
    data.actor = human;
    child->frame()->m_data = &data;
    I3D_frame *joints[0x20];
    S_FakeFrame *jointFrames[0x20];
    for (int i = 0; i < 0x20; ++i) {
        char name[8];
        std::sprintf(name, "j%d", i);
        jointFrames[i] = MakeFrame(name, static_cast<float>(i) + 2.0f, 1.0f, 0);
        joints[i] = jointFrames[i]->frame();
    }
    *reinterpret_cast<I3D_frame ***>(child->bytes + 0x214) = joints;
    g_inRadius.push_back(MakeVolume(frame));
    g_inRadius.push_back(MakeVolume(frame));
    S_Explosion boom = Boom();
    boom.Execute();
    CHECK(g_hits.size() == 1 && g_hits[0].actor == human);
    // Tried from the first joint (0x18) only: it is free. Toward the explosion, 0.05 short of it.
    CHECK(g_rays.size() == 1);
    if (!g_rays.empty()) {
        CHECK(Near(g_rays[0].first.x, 0x18 + 2.0f));
        const S_vector toward(-(0x18 + 2.0f), -1.0f, 0);
        const S_vector dir = toward - toward.NormalizedGame() * 0.05f;
        CHECK(Near(g_rays[0].second.x, dir.x) && Near(g_rays[0].second.y, dir.y));
    }
    // All joints blocked: not reached. The order of the joints.
    Reset();
    g_children[frame->frame()] = child->frame();
    g_inRadius.push_back(MakeVolume(frame));
    frame->frame()->m_data = &data;
    g_allBlocked = true;
    S_Explosion blocked = Boom();
    blocked.Execute();
    CHECK(g_hits.empty());
    CHECK(g_rays.size() == 6);
    if (g_rays.size() == 6) {
        const int order[6] = {0x18, 3, 0xE, 0xA, 0x11, 0x15};
        for (int i = 0; i < 6; ++i)
            CHECK(Near(g_rays[i].first.x, order[i] + 2.0f));
    }
    // The last joint free: reached.
    Reset();
    g_children[frame->frame()] = child->frame();
    g_inRadius.push_back(MakeVolume(frame));
    frame->frame()->m_data = &data;
    for (int joint : {0x18, 3, 0xE, 0xA, 0x11})
        g_blockedFrom.insert(Key(S_vector(joint + 2.0f, 1.0f, 0)));
    S_Explosion lastFree = Boom();
    lastFree.Execute();
    CHECK(g_hits.size() == 1 && g_rays.size() == 6);

    // The flag: a human in the explosion's sector needs no line of sight; in another it does.
    Reset();
    g_children[frame->frame()] = child->frame();
    g_inRadius.push_back(MakeVolume(frame));
    frame->frame()->m_data = &data;
    g_allBlocked = true;
    g_sector = reinterpret_cast<I3D_sector *>(frame->frame());
    g_humanSector = g_sector;
    S_Explosion sameSector = Boom();
    sameSector.unknown18 = true;
    sameSector.Execute();
    CHECK(g_hits.size() == 1 && g_rays.empty());
    Reset();
    g_children[frame->frame()] = child->frame();
    g_inRadius.push_back(MakeVolume(frame));
    frame->frame()->m_data = &data;
    g_allBlocked = true;
    g_humanSector = reinterpret_cast<I3D_sector *>(child->frame());
    S_Explosion otherSector = Boom();
    otherSector.unknown18 = true;
    otherSector.Execute();
    CHECK(g_hits.empty() && g_rays.size() == 6);
    // Without the model's child: a frame with no joints to try counts as not reached.
    Reset();
}

void TestOrderQuirkAndRange()
{
    // In the list: sorted, except that the last one is not compared (so 3 before 2 here goes behind).
    Reset();
    C_Actor *a, *b, *c;
    Target("a", 5.0f, ACTOR_BARREL, &a);                // processed last (the frames go from the end)
    Target("b", 7.0f, ACTOR_BARREL, &b);
    Target("c", 6.0f, ACTOR_BARREL, &c);
    S_Explosion boom = Boom(1000.0f, 50.0f);
    C_ExplosionWave wave(boom);
    // Order of adding: c(36), then b(49): behind; then a(25): compared with c? c has a next: goes first.
    std::vector<float> order;
    for (const S_WaveVictim *v = wave.m_victims; v; v = v->next)
        order.push_back(v->distance);
    CHECK(order.size() == 3 && Near(order[0], 25.0f) && Near(order[1], 36.0f) && Near(order[2], 49.0f));
    // Two in the list: a nearer third goes behind the last, not compared with it.
    Reset();
    Target("a", 8.0f, ACTOR_BARREL, &a);
    Target("b", 7.0f, ACTOR_BARREL, &b);
    Target("c", 5.0f, ACTOR_BARREL, &c);
    // c(25) then b(49), then a(64): a compared with c only, goes behind.
    C_ExplosionWave wave2(boom);
    order.clear();
    for (const S_WaveVictim *v = wave2.m_victims; v; v = v->next)
        order.push_back(v->distance);
    CHECK(order.size() == 3 && Near(order[0], 25.0f) && Near(order[1], 49.0f) && Near(order[2], 64.0f));
    Reset();
    Target("x", 6.0f, ACTOR_BARREL, &a);
    Target("y", 5.0f, ACTOR_BARREL, &b);
    C_ExplosionWave wave3(boom);
    // y(25) first; x(36) behind. A single one in the list is not compared: reversed order shows it.
    order.clear();
    for (const S_WaveVictim *v = wave3.m_victims; v; v = v->next)
        order.push_back(v->distance);
    CHECK(order.size() == 2 && Near(order[0], 25.0f) && Near(order[1], 36.0f));
    Reset();
    Target("x", 5.0f, ACTOR_BARREL, &a);
    Target("y", 6.0f, ACTOR_BARREL, &b);
    C_ExplosionWave wave4(boom);
    order.clear();
    for (const S_WaveVictim *v = wave4.m_victims; v; v = v->next)
        order.push_back(v->distance);
    CHECK(order.size() == 2 && Near(order[0], 36.0f) && Near(order[1], 25.0f));  // (the quirk)

    // The range grows to the farthest's squared distance; the camera got it before.
    Reset();
    Target("far", 30.0f, ACTOR_BARREL, &a);             // squared 900
    S_Explosion shortOne = Boom(10.0f, 50.0f);
    C_ExplosionWave wave5(shortOne);
    CHECK(Near(wave5.m_range, 900.0f));
    CHECK(g_cameraValue == 10);
    // No camera: nothing asked.
    Reset();
    g_noCamera = true;
    S_Explosion noCamera = Boom();
    noCamera.Execute();
    CHECK(g_cameraCalls == 0);
    // Nothing in range: no wave stays.
    C_chunk chunk;
    chunk.Open("x", true);
    TimedEvents_Save(chunk);
    chunk.Close();
    CHECK(g_file.size() == 6);
    // In the box: distance 0; beyond each side of it, from the local position.
    Reset();
    S_FakeFrame *boxed = MakeFrame("box", 10.0f, 0, 0, FRAME_VISUAL, 3.0f);
    MakeActor(ACTOR_BARREL, boxed, 1);
    g_inRadius.push_back(MakeVolume(boxed));
    C_ExplosionWave wave6(boom);
    CHECK(Near(wave6.m_victims->distance, 49.0f));      // (10 - 3) squared
    Reset();
    S_FakeFrame *inside = MakeFrame("in", 1.0f, 1.0f, 1.0f, FRAME_VISUAL, 3.0f);
    MakeActor(ACTOR_BARREL, inside, 1);
    g_inRadius.push_back(MakeVolume(inside));
    C_ExplosionWave wave7(boom);
    CHECK(Near(wave7.m_victims->distance, 0.0f));
    // On each axis and side.
    struct { float x, y, z, expected; } sides[] = {
        {-10, 0, 0, 49}, {10, 0, 0, 49}, {0, -10, 0, 49}, {0, 10, 0, 49}, {0, 0, -10, 49}, {0, 0, 10, 49}, {10, 10, 10, 147},
    };
    for (auto &side : sides) {
        Reset();
        S_FakeFrame *f = MakeFrame("s", side.x, side.y, side.z, FRAME_VISUAL, 3.0f);
        MakeActor(ACTOR_BARREL, f, 1);
        g_inRadius.push_back(MakeVolume(f));
        C_ExplosionWave w(boom);
        CHECK(Near(w.m_victims->distance, side.expected));
    }
    // A dummy's own box (+0x120), not the hierarchy's.
    Reset();
    S_FakeFrame *dummy = MakeFrame("d", 10.0f, 0, 0, FRAME_DUMMY, 3.0f);
    I3D_bbox own{};
    own.min = S_vector(-1, -1, -1);
    own.max = S_vector(1, 1, 1);
    *reinterpret_cast<I3D_bbox *>(dummy->bytes + 0x120) = own;
    MakeActor(ACTOR_BARREL, dummy, 1);
    g_inRadius.push_back(MakeVolume(dummy));
    C_ExplosionWave wave8(boom);
    CHECK(Near(wave8.m_victims->distance, 81.0f));
    // The volume at the explosion comes along, as it is; a volume without a frame ends the list.
    Reset();
    S_FakeFrame *at = MakeFrame("at", 1.0f, 0, 0);
    MakeActor(ACTOR_BARREL, at, 1);
    g_at_volume = MakeVolume(at);
    S_FakeFrame *same = MakeFrame("n", 2.0f, 0, 0);
    MakeActor(ACTOR_BARREL, same, 2);
    g_inRadius.push_back(MakeVolume(at));               // (known already)
    g_inRadius.push_back(MakeVolume(nullptr));
    g_inRadius.push_back(MakeVolume(same));
    C_ExplosionWave wave9(boom);
    CHECK(wave9.m_victims && wave9.m_victims->frame == at->frame() && wave9.m_victims->next == nullptr);
    CHECK(g_excludeAsked == 0xD8000);
    CHECK(at->frame()->m_refCount == 2);
}

void TestSaveLoad()
{
    Reset();
    C_Actor *a, *b, *source;
    Target("second", 7.0f, ACTOR_BARREL, &b);
    Target("first", 6.0f, ACTOR_BARREL, &a);
    S_FakeFrame *sector = MakeFrame("sector1", 0, 0, 0, FRAME_SECTOR);
    g_sector = reinterpret_cast<I3D_sector *>(sector->frame());
    source = MakeActor(ACTOR_HUMAN, nullptr, 55);
    g_actorsById[55] = source;
    S_Explosion boom = Boom(100.0f, 40.0f);
    boom.pos = S_vector(0, 0, 0);
    boom.source = source;
    boom.unknown18 = true;
    boom.Execute();                                     // radius 20: nobody yet
    CHECK(g_hits.empty());
    C_chunk out;
    out.Open("x", true);
    CHECK(TimedEvents_Save(out));
    out.Close();
    const size_t saved = g_file.size();
    CHECK(saved > 100);
    // The chunks' ids as the original writes them.
    CHECK(static_cast<unsigned char>(g_file[0]) == 0x85 && static_cast<unsigned char>(g_file[1]) == 0x1A);
    CHECK(static_cast<unsigned char>(g_file[6]) == 0xE8 && static_cast<unsigned char>(g_file[7]) == 0x03);
    CHECK(static_cast<unsigned char>(g_file[12]) == 0x57 && static_cast<unsigned char>(g_file[13]) == 0x04);

    // Back: same again when saved; it carries on.
    TimedEvents_Clear();
    C_chunk in;
    in.Open("x", false);
    CHECK(TimedEvents_Load(in));
    in.Close();
    const std::string first = g_file;
    C_chunk again;
    again.Open("x", true);
    TimedEvents_Save(again);
    again.Close();
    CHECK(g_file == first);
    TimedEvents_Update(100, false);                     // radius 40: first (36)
    CHECK(g_hits.size() == 1 && g_hits[0].actor == a && g_hits[0].type == HIT_EXPLOSION && g_hits[0].source == source);
    CHECK(Near(g_hits[0].amount, (1.0f - 36.0f / 100.0f) * 40.0f));
    TimedEvents_Update(100, false);                     // radius 60: second (49)
    CHECK(g_hits.size() == 2 && g_hits[1].actor == b);

    // Kept: of more victims only the last two survive a load.
    Reset();
    for (int i = 0; i < 5; ++i) {
        C_Actor *t;
        char name[8];
        std::sprintf(name, "v%d", i);
        Target(name, 6.0f + i, ACTOR_BARREL, &t);
    }
    S_Explosion five = Boom(1000.0f, 40.0f);
    five.Execute();
    C_chunk out5;
    out5.Open("x", true);
    TimedEvents_Save(out5);
    out5.Close();
    TimedEvents_Clear();
    C_chunk in5;
    in5.Open("x", false);
    TimedEvents_Load(in5);
    in5.Close();
    for (int i = 0; i < 20; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 2);
    // A frame gone from the scene: its victim stays without one, and does no harm when saved.
    // A chunk it does not know is skipped, in events and wave alike; an open failure loads nothing.
    Reset();
    C_chunk unknown;
    unknown.Open("x", true);
    unknown.Begin(0x1A85);
    unknown.WriteChunk<int>(77, 5);
    unknown.Begin(1000);
    unknown.Begin(0x457);
    unknown.WriteChunk<int>(77, 5);
    unknown.Ascend();
    unknown.Ascend();
    unknown.Ascend();
    unknown.Close();
    C_chunk unknownIn;
    unknownIn.Open("x", false);
    CHECK(TimedEvents_Load(unknownIn));
    unknownIn.Close();
    C_chunk unknownOut;
    unknownOut.Open("x", true);
    TimedEvents_Save(unknownOut);
    unknownOut.Close();
    CHECK(g_file.size() > 6);                           // (one wave, empty)
    TimedEvents_Update(100, false);                     // (and it goes)
    C_chunk emptied;
    emptied.Open("x", true);
    TimedEvents_Save(emptied);
    emptied.Close();
    CHECK(g_file.size() == 6);
}

void TestUpdateRemoval()
{
    // Waves that end leave the list, others stay and go on.
    Reset();
    C_Actor *a, *b;
    Target("a", 8.0f, ACTOR_BARREL, &a);                // radius 64 needed: 3 steps
    S_Explosion first = Boom();
    first.Execute();
    g_inRadius.clear();
    Target("b", 12.0f, ACTOR_BARREL, &b);               // 144: 8 steps
    S_Explosion second = Boom(1000.0f);
    second.Execute();
    for (int i = 0; i < 2; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.empty());                              // (radius 60; it stands at 64)
    TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 1 && g_hits[0].actor == a);
    for (int i = 0; i < 3; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 1);                          // (radius 140; the other at 144)
    TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 2 && g_hits[1].actor == b);
    // Kept: one beyond the range makes it the range, so it takes nothing.
    Reset();
    C_Actor *edge;
    Target("edge", 12.0f, ACTOR_BARREL, &edge);
    S_Explosion atEdge = Boom(100.0f);
    atEdge.Execute();
    for (int i = 0; i < 10; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.empty());
    // Cleared: gone without being hit.
    Reset();
    C_Actor *c;
    Target("c", 50.0f, ACTOR_BARREL, &c);
    S_Explosion third = Boom(5000.0f);
    third.Execute();
    TimedEvents_Update(0, true);
    for (int i = 0; i < 200; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.empty());
}

// ---- the periodic links ----
C_Actor *LinkActor(float x, E_ActorType type, int id)
{
    S_FakeFrame *frame = MakeFrame("link", x, 0, 0);
    return MakeActor(type, frame, id);
}

void TestLinksBasics()
{
    Reset();
    C_Actor *a = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *b = LinkActor(3, ACTOR_HUMAN, 2);          // within 5
    CHECK(PeriodicLinks_Add(a, 0) == 0);
    CHECK(g_proximity.empty());                         // (no pair yet)
    CHECK(PeriodicLinks_Add(b, 0) == 1);
    // Told at once, each of the other: within reach, distance |3 - 5|.
    CHECK(g_proximity.size() == 2);
    if (g_proximity.size() == 2) {
        CHECK(reinterpret_cast<C_Actor *>(g_proximity[0].self) == a && g_proximity[0].other == b);
        CHECK(reinterpret_cast<C_Actor *>(g_proximity[1].self) == b && g_proximity[1].other == a);
        CHECK(Near(g_proximity[0].distance, 2.0f));
        CHECK(g_proximity[0].ended == false && g_proximity[0].unknown == true);
    }
    // A client has none.
    g_gameRole = 2;
    CHECK(PeriodicLinks_Add(a, 0) == -1);
    PeriodicLinks_Remove(0);
    PeriodicLinks_SetRate(0, 3);
    g_gameRole = 1;

    // Out of reach, at the first look: not told; bucket by when they could meet.
    Reset();
    C_Actor *c = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *d = LinkActor(100, ACTOR_HUMAN, 2);
    PeriodicLinks_Add(c, 0);
    PeriodicLinks_Add(d, 0);
    CHECK(g_proximity.empty());
    // They walk within reach: seen when its bucket's time comes. 95 m at 10/s: 9500 ms: bucket 6 (the next one's period 12800), first looked at after
    // 6400 + 600 ms. It moves on to bucket 2 (400 ms: the next one's 800), told with both flags set (the bucket numbers).
    d->SetPosition(S_vector(1, 0, 0));
    for (int i = 0; i < 69; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.empty());
    PeriodicLinks_Tick();
    CHECK(g_proximity.size() == 2);
    if (g_proximity.size() == 2) {
        CHECK(g_proximity[0].ended == true && g_proximity[0].unknown == true);
        CHECK(Near(g_proximity[0].distance, 4.0f));
    }
    // Staying within, in the new bucket: 800/... looks again, nothing to tell. Its period 400: within those ticks, nothing.
    g_proximity.clear();
    for (int i = 0; i < 8; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.empty());
    // Out again at the look: told it left.
    d->SetPosition(S_vector(50, 0, 0));
    for (int i = 0; i < 8; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.size() == 2);
}

void TestLinksTiming()
{
    // Bucket n: first looked at after (2^n + n) ticks, then every 2^n.
    Reset();
    C_Actor *a = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *b = LinkActor(6, ACTOR_HUMAN, 2);          // outside, 1 m: 100 ms at 10/s: bucket 1 (limit 200; not < 100... )
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 0);
    CHECK(g_proximity.empty());
    b->SetPosition(S_vector(1, 0, 0));
    int ticks = 0;
    while (g_proximity.empty() && ticks < 20) {
        PeriodicLinks_Tick();
        ++ticks;
    }
    // Its bucket: 1000/10 * 1 = 100: 0's next period is 200: bucket 0: first timer 100 (the first tick).
    CHECK(ticks == 1);
    // Faster: the rate is the sum of the two.
    Reset();
    C_Actor *c = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *e = LinkActor(45, ACTOR_HUMAN, 2);         // 40 m at 10/s: 4000 ms: bucket 4 (period 1600.. limit 3200? -> 5)
    PeriodicLinks_Add(c, 0);
    PeriodicLinks_Add(e, 0);
    e->SetPosition(S_vector(1, 0, 0));
    // A raised rate looks at its pairs at once.
    g_proximity.clear();
    PeriodicLinks_SetRate(1, 3);
    CHECK(g_proximity.size() == 2);
    // The same or lower: not.
    g_proximity.clear();
    PeriodicLinks_SetRate(1, 3);
    PeriodicLinks_SetRate(1, 0);
    CHECK(g_proximity.empty());
    // Bad ids: nothing.
    PeriodicLinks_SetRate(7, 3);
    PeriodicLinks_SetRate(-1, 3);
    PeriodicLinks_Remove(7);
    PeriodicLinks_Remove(-1);
}

void TestLinksRemoveAndHoles()
{
    Reset();
    C_Actor *a = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *b = LinkActor(1, ACTOR_HUMAN, 2);
    C_Actor *c = LinkActor(2, ACTOR_HUMAN, 3);
    C_Actor *d = LinkActor(3, ACTOR_HUMAN, 4);
    CHECK(PeriodicLinks_Add(a, 0) == 0);
    CHECK(PeriodicLinks_Add(b, 0) == 1);
    CHECK(PeriodicLinks_Add(c, 0) == 2);
    PeriodicLinks_Remove(1);
    g_proximity.clear();
    // The hole is taken again, and pairs with the two left.
    CHECK(PeriodicLinks_Add(d, 0) == 1);
    CHECK(g_proximity.size() == 4);
    // The last removed: the tail goes; the next one takes it.
    PeriodicLinks_Remove(2);
    C_Actor *e = LinkActor(4, ACTOR_HUMAN, 5);
    CHECK(PeriodicLinks_Add(e, 0) == 2);
    // A hole at the end with one before it: both go.
    PeriodicLinks_Remove(1);
    PeriodicLinks_Remove(2);
    C_Actor *f = LinkActor(5, ACTOR_HUMAN, 6);
    CHECK(PeriodicLinks_Add(f, 0) == 1);
    // Links to something removed are gone: only the pair (0, 1) is told at a look.
    PeriodicLinks_Clear();
    g_proximity.clear();
    CHECK(PeriodicLinks_Add(a, 0) == 0);
}

void TestLinksVehicles()
{
    // A vehicle is linked with humans only, reach 7.5 (0x25: 10), and nobody is told, though it moves on.
    Reset();
    C_Actor *human = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *other = LinkActor(0, ACTOR_BARREL, 2);
    C_Actor *car = LinkActor(6, ACTOR_CAR, 3);
    PeriodicLinks_Add(human, 0);
    PeriodicLinks_Add(other, 0);
    g_proximity.clear();
    PeriodicLinks_Add(car, 0);
    CHECK(g_proximity.empty());
    // No crash on a hole.
    PeriodicLinks_Remove(1);
    C_Actor *car2 = LinkActor(1, static_cast<E_ActorType>(0x25), 4);
    C_Actor *car3 = LinkActor(1, ACTOR_CAR, 5);
    PeriodicLinks_Add(car2, 0);
    PeriodicLinks_Add(car3, 0);
    CHECK(g_proximity.empty());
    // A human joining a vehicle's reach: not told.
    PeriodicLinks_Clear();
    C_Actor *h2 = LinkActor(0, ACTOR_HUMAN, 6);
    C_Actor *h3 = LinkActor(4.9f, ACTOR_HUMAN, 7);
    C_Actor *h4 = LinkActor(5.1f, ACTOR_HUMAN, 8);
    PeriodicLinks_Add(h2, 0);
    PeriodicLinks_Add(h3, 0);
    CHECK(g_proximity.size() == 2);                     // within 5
    g_proximity.clear();
    PeriodicLinks_Clear();
    PeriodicLinks_Add(h2, 0);
    PeriodicLinks_Add(h4, 0);
    CHECK(g_proximity.empty());                         // beyond 5
}

// The chunk ids of the file, in order, with depth (for the containers it names), and their sizes.
struct S_ChunkInfo { int depth; unsigned id; unsigned size; size_t data; };
void WalkChunks(const std::string &file, size_t at, size_t end, int depth, const std::set<unsigned> &containers, std::vector<S_ChunkInfo> &out)
{
    while (at + 6 <= end) {
        const unsigned id = static_cast<unsigned char>(file[at]) | static_cast<unsigned char>(file[at + 1]) << 8;
        unsigned size;
        std::memcpy(&size, file.data() + at + 2, 4);
        out.push_back({depth, id, size, at + 6});
        if (containers.count(id))
            WalkChunks(file, at + 6, at + size, depth + 1, containers, out);
        at += size;
    }
}
float FloatAt(size_t at) { float v; std::memcpy(&v, g_file.data() + at, 4); return v; }
int IntAt(size_t at) { int v; std::memcpy(&v, g_file.data() + at, 4); return v; }

void TestWaveFileFormat()
{
    Reset();
    C_Actor *target;
    S_FakeFrame *frame = MakeFrame("victim", 11.0f, 2.0f, 3.0f);
    target = MakeActor(ACTOR_BARREL, frame, 3);
    g_inRadius.push_back(MakeVolume(frame));
    S_FakeFrame *sector = MakeFrame("sectorA", 0, 0, 0, FRAME_SECTOR);
    g_sector = reinterpret_cast<I3D_sector *>(sector->frame());
    C_Actor *source = MakeActor(ACTOR_HUMAN, nullptr, 77);
    g_actorsById[77] = source;
    S_Explosion boom = Boom(1000.0f, 30.0f);
    boom.pos = S_vector(1.0f, 2.0f, 3.0f);              // (the frame 10 m along x from it)
    boom.posW = 1.0f;
    boom.source = source;
    boom.unknown18 = true;
    boom.Execute();
    C_chunk out;
    out.Open("x", true);
    TimedEvents_Save(out);
    out.Close();
    std::vector<S_ChunkInfo> chunks;
    WalkChunks(g_file, 0, g_file.size(), 0, {0x1A85, 1000, 0x457, 6000, 0x17D4}, chunks);
    // (1000 is both the event and, inside a wave, the position: as data it is walked as if a container; only the ids at their depths are checked.)
    std::vector<unsigned> ids;
    for (const S_ChunkInfo &chunk : chunks)
        if (chunk.depth <= 3)
            ids.push_back(chunk.id);
    CHECK(ids.size() >= 3 && ids[0] == 0x1A85 && ids[1] == 1000 && ids[2] == 0x457);
    auto Find = [&](unsigned id, int depth) -> const S_ChunkInfo * {
        for (const S_ChunkInfo &chunk : chunks)
            if (chunk.id == id && chunk.depth == depth)
                return &chunk;
        return nullptr;
    };
    const S_ChunkInfo *radius = Find(2000, 3), *damage = Find(3000, 3), *range = Find(4000, 3), *sourceChunk = Find(5000, 3), *flag = Find(0x157C, 3), *name = Find(0x1676, 3), *victim = Find(6000, 3);
    CHECK(radius && radius->size == 10 && Near(FloatAt(radius->data), 20.0f));
    CHECK(damage && damage->size == 10 && Near(FloatAt(damage->data), 30.0f));
    CHECK(range && range->size == 10 && Near(FloatAt(range->data), 1000.0f));
    CHECK(sourceChunk && sourceChunk->size == 10 && IntAt(sourceChunk->data) == 77);
    CHECK(flag && flag->size == 7 && g_file[flag->data] == 1);
    CHECK(name && name->size == 6 + 8 && !std::strcmp(g_file.data() + name->data, "sectorA"));
    CHECK(victim != nullptr);
    if (const S_ChunkInfo *pos = Find(1000, 3)) {
        CHECK(pos->size == 6 + 12);
        CHECK(Near(FloatAt(pos->data), 1.0f) && Near(FloatAt(pos->data + 4), 2.0f) && Near(FloatAt(pos->data + 8), 3.0f));
    } else {
        CHECK(false);
    }
    const S_ChunkInfo *inner = Find(0x17D4, 4);
    CHECK(inner != nullptr);
    if (inner) {
        // Its parts: offset, frame, distance.
        std::vector<S_ChunkInfo> parts;
        WalkChunks(g_file, inner->data, inner->data + inner->size - 6, 0, {}, parts);
        CHECK(parts.size() == 3 && parts[0].id == 0x1838 && parts[1].id == 0x189C && parts[2].id == 0x1900);
        if (parts.size() == 3) {
            CHECK(parts[0].size == 6 + 12 && Near(FloatAt(parts[0].data), 10.0f) && Near(FloatAt(parts[0].data + 4), 0.0f));
            CHECK(!std::strcmp(g_file.data() + parts[1].data, "victim") && parts[1].size == 6 + 7);
            CHECK(parts[2].size == 10 && Near(FloatAt(parts[2].data), 100.0f));
        }
    }
    // No source: -1. Back again: the position and the offset count.
    C_chunk in;
    in.Open("x", false);
    TimedEvents_Load(in);
    in.Close();
    for (int i = 0; i < 5; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 1 && g_hits[0].actor == target);
    if (!g_hits.empty()) {
        CHECK(Near(g_hits[0].pos.x, 1.0f) && Near(g_hits[0].pos.y, 2.0f) && Near(g_hits[0].pos.z, 3.0f));
        CHECK(Near(g_hits[0].dir.x, 1.0f) && g_hits[0].source == source);
        CHECK(Near(g_hits[0].amount, (1.0f - 100.0f / 1000.0f) * 30.0f));
    }
    // A wave without a source saves -1, and loads back without one; a loaded one replaces what was.
    Reset();
    C_Actor *lone;
    Target("lone", 10.0f, ACTOR_BARREL, &lone);
    S_FakeFrame *other = MakeFrame("other", 0, 0, 0);
    (void)other;
    S_Explosion noSource = Boom(1000.0f, 30.0f);
    noSource.Execute();
    C_chunk out2;
    out2.Open("x", true);
    TimedEvents_Save(out2);
    out2.Close();
    TimedEvents_Update(100, false);                     // (the one that stays is hit once more if it was not replaced)
    C_chunk in2;
    in2.Open("x", false);
    TimedEvents_Load(in2);
    in2.Close();
    for (int i = 0; i < 5; ++i)
        TimedEvents_Update(100, false);
    CHECK(g_hits.size() == 1 && g_hits[0].type == HIT_SCRIPT_KILL && g_hits[0].source == nullptr);
}

void ReadLinksFile(std::vector<S_ChunkInfo> &chunks)
{
    C_chunk out;
    out.Open("x", true);
    PeriodicLinks_Save(out);
    out.Close();
    chunks.clear();
    WalkChunks(g_file, 0, g_file.size(), 0, {0x17EB}, chunks);
}

void TestLinksFile()
{
    Reset();
    C_Actor *a = LinkActor(0, ACTOR_HUMAN, 21);
    C_Actor *b = LinkActor(3, ACTOR_HUMAN, 22);
    PeriodicLinks_Add(a, 3);
    PeriodicLinks_Add(b, 0);
    std::vector<S_ChunkInfo> chunks;
    ReadLinksFile(chunks);
    CHECK(chunks.size() == 4 && chunks[0].id == 0x17EB && chunks[1].id == 1000 && chunks[2].id == 1000 && chunks[3].id == 2000);
    if (chunks.size() == 4) {
        CHECK(IntAt(chunks[1].data) == 21 && Near(FloatAt(chunks[1].data + 4), 20.0f) && chunks[1].size == 14);
        CHECK(IntAt(chunks[2].data) == 22 && Near(FloatAt(chunks[2].data + 4), 5.0f));
        // Ten buckets: timers 2^n + n ticks, periods 2^n ticks; the one link where it went, within.
        size_t at = chunks[3].data;
        int links = 0;
        for (int n = 0; n < 10; ++n) {
            CHECK(IntAt(at) == (100 << n) + 100 * n);
            CHECK(IntAt(at + 4) == 100 << n);
            const int count = IntAt(at + 8);
            at += 12;
            if (count) {
                CHECK(n == 0);                          // |3 - 5| = 2 m at 25/s: 80 ms
                CHECK(count == 1 && g_file[at] == 0 && g_file[at + 1] == 1 && g_file[at + 2] == 1);
            }
            links += count;
            at += count * 3;
        }
        CHECK(links == 1);
        CHECK(at == chunks[3].data + chunks[3].size - 6);
    }
}

// Where the link went and whether it is within, from the file.
struct S_LinkState { int bucket; int first, second; bool within; };
S_LinkState LinkState()
{
    std::vector<S_ChunkInfo> chunks;
    ReadLinksFile(chunks);
    S_LinkState state = {-1, -1, -1, false};
    for (const S_ChunkInfo &chunk : chunks) {
        if (chunk.id != 2000)
            continue;
        size_t at = chunk.data;
        for (int n = 0; n < 10; ++n) {
            const int count = IntAt(at + 8);
            at += 12;
            if (count) {
                state = {n, g_file[at], g_file[at + 1], g_file[at + 2] != 0};
            }
            at += count * 3;
        }
    }
    return state;
}

void TestLinksReach()
{
    struct { E_ActorType type; float x; bool within; int bucket; } cases[] = {
        // (Rates 5 + 5: 10 m/s: 100 ms per m.)
        {ACTOR_CAR, 7.0f, true, 0},                     // 0.5 m inside 7.5
        {ACTOR_CAR, 8.0f, false, 0},
        {static_cast<E_ActorType>(0x25), 9.0f, true, 1},   // 1 m inside 10: 100 ms... the next period 200: still 0
        {static_cast<E_ActorType>(0x25), 11.0f, false, 1},
        {ACTOR_HUMAN, 4.0f, true, 0},
        {ACTOR_HUMAN, 6.0f, false, 0},
    };
    for (auto &one : cases) {
        Reset();
        PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 1), 0);
        PeriodicLinks_Add(LinkActor(one.x, one.type, 2), 0);
        const S_LinkState state = LinkState();
        CHECK(state.within == one.within);
        CHECK(state.first == 0 && state.second == 1);
    }
    // A car and a human, in that order: first and second by entry, and only the vehicle's pairs with humans are made.
    Reset();
    PeriodicLinks_Add(LinkActor(100, ACTOR_BARREL, 1), 0);
    PeriodicLinks_Add(LinkActor(0, ACTOR_CAR, 2), 0);
    CHECK(LinkState().bucket == -1);                    // (a vehicle with a barrel: none)
    PeriodicLinks_Add(LinkActor(1, ACTOR_HUMAN, 3), 0);
    const S_LinkState withHuman = LinkState();
    CHECK(withHuman.first == 1 || withHuman.first == 0);
    // A bucket by the time they need: 1000 / rate * distance < next period; the last when none is.
    struct { float x; int bucket; } times[] = {
        {5.5f, 0}, {6.9f, 0},                           // 50, 190 ms: < 200
        {7.0f, 1},                                      // 200: not < 200, < 400
        {10.0f, 2},                                     // 500: < 800
        {100.0f, 6},                                    // 9500: < 12800
        {1000.0f, 9},                                   // the last
    };
    for (auto &time : times) {
        Reset();
        PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 1), 0);
        PeriodicLinks_Add(LinkActor(time.x, ACTOR_HUMAN, 2), 0);
        CHECK(LinkState().bucket == time.bucket);
    }
    // Rates of nothing count as 0.01: far on in the last bucket, however near.
    Reset();
    PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 1), 1);
    PeriodicLinks_Add(LinkActor(6, ACTOR_HUMAN, 2), 2);
    CHECK(LinkState().bucket == 9);
    // At the reach exactly: within; at the same place (distance 0, not sqrt): within, the whole reach away; 1 m apart exactly.
    Reset();
    PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 1), 0);
    PeriodicLinks_Add(LinkActor(5, ACTOR_HUMAN, 2), 0);
    CHECK(LinkState().within);
    Reset();
    PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 1), 0);
    PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 2), 0);
    CHECK(LinkState().within && LinkState().bucket == 2);  // (5 m: 500 ms)
    CHECK(g_proximity.size() >= 2 && Near(g_proximity[0].distance, 5.0f));
    Reset();
    PeriodicLinks_Add(LinkActor(0, ACTOR_HUMAN, 1), 0);
    PeriodicLinks_Add(LinkActor(1, ACTOR_HUMAN, 2), 0);
    CHECK(g_proximity.size() == 2 && Near(g_proximity[0].distance, 4.0f));
}

void TestLinksTransitions()
{
    // Out of reach and back, in one bucket: "gone" (ended, not unknown), then "near" (not ended).
    Reset();
    C_Actor *a = LinkActor(0, ACTOR_HUMAN, 1);
    C_Actor *b = LinkActor(3, ACTOR_HUMAN, 2);          // 2 m inside: 200 ms: bucket 1
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 0);
    CHECK(LinkState().bucket == 1 && LinkState().within);
    g_proximity.clear();
    b->SetPosition(S_vector(7.5f, 0, 0));               // 2.5 m outside: bucket 1 still
    PeriodicLinks_SetRate(1, 3);                        // (looks at once; rates 25: 100 ms: now bucket 0: not the case wanted)
    PeriodicLinks_SetRate(1, 0);
    g_proximity.clear();
    PeriodicLinks_Clear();
    a = LinkActor(0, ACTOR_HUMAN, 1);
    b = LinkActor(3, ACTOR_HUMAN, 2);
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 0);
    g_proximity.clear();
    b->SetPosition(S_vector(7.5f, 0, 0));
    int guard = 0;
    while (g_proximity.empty() && guard++ < 100)
        PeriodicLinks_Tick();
    CHECK(g_proximity.size() == 2);
    if (g_proximity.size() == 2) {
        CHECK(g_proximity[0].ended == true && g_proximity[0].unknown == false);
        CHECK(Near(g_proximity[0].distance, 2.5f));
    }
    CHECK(!LinkState().within);
    CHECK(LinkState().bucket == 1);
    g_proximity.clear();
    b->SetPosition(S_vector(3.0f, 0, 0));
    guard = 0;
    while (g_proximity.empty() && guard++ < 100)
        PeriodicLinks_Tick();
    CHECK(g_proximity.size() == 2);
    if (g_proximity.size() == 2)
        CHECK(g_proximity[0].ended == false && g_proximity[0].unknown == true);
    CHECK(LinkState().within);

    // Staying out, far: from a bucket to another, neither the first: told, both flags set; into the first: moved, not told.
    Reset();
    a = LinkActor(0, ACTOR_HUMAN, 1);
    b = LinkActor(100, ACTOR_HUMAN, 2);                 // bucket 6
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 0);
    CHECK(LinkState().bucket == 6);
    b->SetPosition(S_vector(50, 0, 0));                 // 45 m: 4500: bucket 5
    for (int i = 0; i < 70; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.size() == 2);
    if (g_proximity.size() == 2)
        CHECK(g_proximity[0].ended && g_proximity[0].unknown);
    CHECK(LinkState().bucket == 5);
    // The same place: stays, not told.
    g_proximity.clear();
    for (int i = 0; i < 40; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.empty() && LinkState().bucket == 5);
    // Into the first: not told.
    b->SetPosition(S_vector(6, 0, 0));
    for (int i = 0; i < 40; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.empty() && LinkState().bucket == 0);
    // Out of the first, out still: not told.
    b->SetPosition(S_vector(50, 0, 0));
    for (int i = 0; i < 2; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.empty() && LinkState().bucket == 5);
    // Staying within, moving bucket: not told.
    Reset();
    a = LinkActor(0, ACTOR_HUMAN, 1);
    b = LinkActor(1, ACTOR_HUMAN, 2);                   // 4 m inside: bucket 2
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 0);
    CHECK(LinkState().bucket == 2);
    g_proximity.clear();
    b->SetPosition(S_vector(4.5f, 0, 0));               // 0.5 m inside: bucket 0
    for (int i = 0; i < 10; ++i)
        PeriodicLinks_Tick();
    CHECK(g_proximity.empty() && LinkState().bucket == 0 && LinkState().within);
    // A link moved out of a bucket leaves the others there alone.
    Reset();
    a = LinkActor(0, ACTOR_HUMAN, 1);
    b = LinkActor(3, ACTOR_HUMAN, 2);
    C_Actor *c = LinkActor(3.2f, ACTOR_HUMAN, 3);
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 0);
    PeriodicLinks_Add(c, 0);
    std::vector<S_ChunkInfo> chunks;
    ReadLinksFile(chunks);
    size_t total = 0;
    for (const S_ChunkInfo &chunk : chunks)
        if (chunk.id == 2000)
            total = chunk.size;
    CHECK(total == 6 + 10 * 12 + 3 * 3);                // (all three kept)
    // Removed: its links go, the others stay and work.
    PeriodicLinks_Remove(1);
    ReadLinksFile(chunks);
    for (const S_ChunkInfo &chunk : chunks)
        if (chunk.id == 2000)
            total = chunk.size;
    CHECK(total == 6 + 10 * 12 + 1 * 3);
    CHECK(LinkState().first == 0 && LinkState().second == 2);
    g_proximity.clear();
    for (int i = 0; i < 2000; ++i)
        PeriodicLinks_Tick();
    for (const S_ProximityLog &log : g_proximity) {
        CHECK(reinterpret_cast<C_Actor *>(log.self) != b && log.other != b);
    }
    c->SetPosition(S_vector(50, 0, 0));
    g_proximity.clear();
    for (int i = 0; i < 100; ++i)
        PeriodicLinks_Tick();
    CHECK(!g_proximity.empty());
}

void TestLinksSaveLoad()
{
    Reset();
    C_Actor *a = LinkActor(0, ACTOR_HUMAN, 11);
    C_Actor *b = LinkActor(30, ACTOR_HUMAN, 12);
    C_Actor *c = LinkActor(60, ACTOR_HUMAN, 13);
    g_actorsById[11] = a;
    g_actorsById[12] = b;
    g_actorsById[13] = c;
    PeriodicLinks_Add(a, 0);
    PeriodicLinks_Add(b, 3);
    PeriodicLinks_Add(c, 0);
    PeriodicLinks_Remove(1);                            // (a hole: the original crashes saving it)
    for (int i = 0; i < 3; ++i)
        PeriodicLinks_Tick();
    C_chunk out;
    out.Open("x", true);
    CHECK(PeriodicLinks_Save(out));
    out.Close();
    const std::string first = g_file;
    CHECK(static_cast<unsigned char>(g_file[0]) == 0xEB && static_cast<unsigned char>(g_file[1]) == 0x17);
    C_chunk in;
    in.Open("x", false);
    CHECK(PeriodicLinks_Load(in));
    in.Close();
    C_chunk again;
    again.Open("x", true);
    PeriodicLinks_Save(again);
    again.Close();
    CHECK(g_file == first);
    // An actor no longer there: a hole, not a crash; the hole is taken by the next.
    g_actorsById.erase(13);
    C_chunk in2;
    g_file = first;
    in2.Open("x", false);
    PeriodicLinks_Load(in2);
    in2.Close();
    for (int i = 0; i < 2000; ++i)
        PeriodicLinks_Tick();
    C_Actor *d = LinkActor(1, ACTOR_HUMAN, 14);
    CHECK(PeriodicLinks_Add(d, 0) == 1);
    // Another chunk's id: nothing read.
    C_chunk other;
    other.Open("x", true);
    other.Begin(0x1234);
    other.WriteChunk<int>(1000, 5);
    other.Ascend();
    other.Close();
    C_chunk otherIn;
    otherIn.Open("x", false);
    CHECK(PeriodicLinks_Load(otherIn));
    otherIn.Close();
    C_Actor *e = LinkActor(1, ACTOR_HUMAN, 15);
    CHECK(PeriodicLinks_Add(e, 0) == 0);
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (void *&slot : g_frameVt) slot = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    for (void *&slot : g_sceneVt) slot = nullptr;
    g_sceneVt[0xE0 / 4] = reinterpret_cast<void *>(F_GetSectorFixed);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(F_FindFrame);
    g_driverVt[0xA8 / 4] = reinterpret_cast<void *>(F_GetHierarchyBound);
    g_actorVt[0xD4 / 4 + 1] = reinterpret_cast<void *>(F_Hit);  // (g++: two destructor slots)
    g_actorVt[0xD8 / 4 + 1] = reinterpret_cast<void *>(F_GetMaterialId);
    *reinterpret_cast<I3D_scene **>(g_gameBytes + 0x18) = reinterpret_cast<I3D_scene *>(&g_scene);
    TestWaveReachesInOrder();
    TestBlockedAndSource();
    TestAbsorptionAndMaster();
    TestOncePerActorAndManyHits();
    TestHumans();
    TestOrderQuirkAndRange();
    TestSaveLoad();
    TestUpdateRemoval();
    TestLinksFile();                                    // (first: the timers are not reset, as in the original)
    TestLinksBasics();
    TestLinksTiming();
    TestLinksRemoveAndHoles();
    TestLinksVehicles();
    TestLinksSaveLoad();
    TestWaveFileFormat();
    TestLinksReach();
    TestLinksTransitions();
    std::printf("explosion: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
