// The shots (game/shots.cpp), part 1: a bullet's damage over distance and its flight through the collision
// world (C_BulletShot::Init / Tick: materials, penetration, ricochet, actors, gravity, the 600 m end), the
// engine and the game faked. MinGW; build.bat; run shots_test.exe ("shots: N checks, 0 failures").
//
// The expectations come from the original's assembly, not from the port's source:
//   C_BulletShot::GetDamageAt 0x0044b3b0: full damage up to the ideal range (<=), then damage - loss, 0 once
//     the loss is greater;
//   C_BulletShot::Tick 0x0044b910: dt halved for a flare while velocity.y != 0; step = dt * v * 0.001; the
//     shooter's collision off around the query while the distance is < 5; hits sorted by distance; power =
//     GetDamageAt(|hit - pos| + distance, damage); MATERIAL_PASS (77) skipped, MATERIAL_STOP (76) ends there;
//     a dynamic volume's actor hit once, the shooter's skipped, a fake shot skips actors; power < absorption:
//     a ricochet once (DeflectVector 0x00448380 with RandFloat * 0.1 + 1.1, velocity * 0.2, damage * 0.3)
//     unless dir . normal < -0.95 or one was made already, then the end; cost <= power: damage -= cost, on;
//     else the end; velocity.y -= dt * 0.001 * 9.8 after the hits; the distance += |end - pos|, > 600 ends;
//     a finished bullet returns true on a server-only machine, one tick later on a client.
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/chunk.h"
#include "game/actor.h"
#include "game/camera_module.h"
#include "game/car.h"
#include "game/collision.h"
#include "game/first_person_view.h"
#include "game/game.h"
#include "game/human.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/physics.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/rigid_body.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/team.h"
#include "game/versions.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/bitstream.h"
#include "net/net_game.h"
#include "script/script.h"
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
bool Same(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }
bool Same(const S_vector &a, const S_vector &b) { return Same(a.x, b.x) && Same(a.y, b.y) && Same(a.z, b.z); }
}  // namespace

// ============================================================ the fakes' state

uint32_t g_gameRole;
S_MaterialDef *g_materials;
uint32_t g_materialCount;
S_PhysicsHandle g_noPhysicsHandle;
T_PhysicsObject const g_noPhysicsObject = &g_noPhysicsHandle;

namespace {
// The collision query: the hits handed back (unsorted on purpose), the query seen.
std::vector<S_CollisionHit> g_queryHits;
std::vector<S_CollisionQuery> g_queries;
// The shooter's collision: every Remove / Restore with the body it was called on.
std::vector<std::string> g_bodyLog;
// Sounds played, frames hit (scripts), particle effects.
std::vector<S_SoundRequest> g_sounds;
std::vector<I3D_frame *> g_framesHit;
std::vector<int> g_effects;
float g_randFloat = 0.5f;
C_Human *g_activePlayer;
int g_carMaterial = MATERIAL_NONE;
std::vector<int> g_keeperDamage;
bool g_keeperSurvives = true;
struct S_Impulse {
    T_PhysicsObject object;
    S_vector impulse, point;
    float factor;
};
std::vector<S_Impulse> g_impulses;
}  // namespace

int C_CollisionWorld::QuerySegment(S_CollisionQuery &query) const
{
    g_queries.push_back(query);
    query.hits = g_queryHits;
    return static_cast<int>(query.hits.size());
}
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &, const S_vector &, S_vector *, S_vector *, uint32_t,
                                             uint32_t) const
{
    return nullptr;
}
S_CollisionVolume *C_CollisionWorld::RayTestDynamic(const S_vector &, const S_vector &, S_vector *, S_vector *, uint32_t,
                                                    uint32_t) const
{
    return nullptr;
}
S_CollisionVolume *CollisionRayTest(const S_vector &, const S_vector &, S_vector *, S_vector *, uint32_t, uint32_t)
{
    return nullptr;
}
void S_HumanBody::RemoveCollision(bool) { g_bodyLog.push_back("remove"); }
void S_HumanBody::RestoreCollision(bool) { g_bodyLog.push_back("restore"); }
extern "C" float RandFloat() { return g_randFloat; }
void NotifyFrameHit(I3D_frame *frame) { g_framesHit.push_back(frame); }
int C_SoundModule::Play(const S_SoundRequest &request) { g_sounds.push_back(request); return 1; }
namespace {
alignas(16) unsigned char g_soundModuleBytes[0x400];
alignas(16) unsigned char g_cameraBytes[0x400];
alignas(16) unsigned char g_gameBytes[0x400];
}  // namespace
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(g_soundModuleBytes); }
namespace {
class C_FakePhysics : public C_PhysicsModuleBase {
public:
    C_FakePhysics() : C_PhysicsModuleBase(0, "phys") {}
    T_PhysicsObject CreateObject(const S_PhysicsObjectDesc &) override { return nullptr; }
    bool DestroyObject(T_PhysicsObject *) override { return true; }
    void ApplyImpulse(T_PhysicsObject object, const S_vector &impulse, const S_vector &point, float factor) override
    {
        g_impulses.push_back({object, impulse, point, factor});
    }
    bool Hit(T_PhysicsObject, int, const S_vector *, void *) override { return true; }
};
C_FakePhysics g_fakePhysics;
}  // namespace
C_PhysicsModuleBase *GetPhysicsModule() { return &g_fakePhysics; }
C_CameraModule *GetCameraModule() { return reinterpret_cast<C_CameraModule *>(g_cameraBytes); }
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
C_CameraModule *C_game::GetCameraModule() { return ::GetCameraModule(); }
float C_game::GetLightIntensity(const S_vector &, I3D_sector *, bool) { return 1.0f; }
bool C_game::IsFirstPersonView() const { return false; }
void C_game::Print(const char *, ...) {}
bool C_CameraModule::GetCameraDir(int, S_vector4 *dir) { dir->x = 0.0f; dir->y = 0.0f; dir->z = 1.0f; return true; }
bool C_CameraModule::GetCameraPos(int, S_vector4 *pos) { pos->x = 0.0f; pos->y = 0.0f; pos->z = -10.0f; return true; }
void C_CameraModule::SetSpecialMode(int, bool, I3D_frame *) {}
int C_ParticlesModule::Create(int effect, const S_matrix &, I3D_frame *, bool, bool) { g_effects.push_back(effect); return 1; }
int C_ParticlesModule::CreateAt(int effect, const S_vector &, const S_vector &, I3D_frame *, bool, bool)
{
    g_effects.push_back(effect);
    return 1;
}
void C_ParticlesModule::Destroy(int) {}
void C_ParticlesModule::ToggleEmission(int, int) {}
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return nullptr; }
bool C_PlayerOptions::IsEnabled(int) const { return false; }   // (no decals)
int C_GameProfile::GetDifficulty() const { return 2; }
C_Human *C_Team::GetActivePlayer() const { return g_activePlayer; }
void C_Team::NotifyGrenadeThrown(const S_vector &, const S_vector &, C_Actor *) {}
bool C_Human::IsLocalPlayer() const { return false; }
bool C_Human::IsFriendOf(const C_Actor *) const { return false; }
void C_Human::NotifyShotAt(C_Actor *, const S_vector &) {}
const S_vector &C_Human::GetFacingDir() const { static S_vector dir(0, 0, 1); return dir; }
void C_Human::RemoveItem(int, bool) {}
int C_Car::GetHitMaterial(const S_vector &, const S_vector &) { return g_carMaterial; }
bool C_VersionKeeper::Damage(int damage, I3D_frame **, E_VersionEffects) { g_keeperDamage.push_back(damage); return g_keeperSurvives; }
bool C_VersionMaster::DamageFrame(I3D_frame *, int) { return true; }
I3D_frame *C_Item::GetFrame(int) { return nullptr; }
I3D_frame *C_WeaponItem::GetBlastDummy() { return nullptr; }
I3D_frame *C_WeaponItem::GetCartridgeDummy() { return nullptr; }
I3D_frame *C_FirstPersonView::GetBlastDummy() { return nullptr; }
I3D_frame *C_FirstPersonView::GetCartridgeDummy() { return nullptr; }
C_Module::C_Module(int id, const char *) : m_id(id) {}
C_Module::~C_Module() {}
C_BitStream *C_NetGame::CreateMessage(int, int) { return nullptr; }
void C_NetGame::SendNetMessage(C_BitStream *) {}
void C_NetMessage::ReadPosition(S_vector &) {}
void C_NetMessage::WritePosition(const S_vector &) {}
uint32_t C_BitStream::ReadBits(int) { return 0; }
void C_BitStream::WriteBits(uint32_t, int) {}
C_RigidBody::C_RigidBody() {}
C_RigidBody::~C_RigidBody() {}
bool C_RigidBody::SetFrame(I3D_frame *) { return true; }
void C_RigidBody::Setup(const S_RigidBodyParams &) {}
void C_RigidBody::Start() {}
void C_RigidBody::Stop() {}
void C_RigidBody::Tick(uint32_t) {}
bool C_RigidBody::ApplyImpulse(int, const S_vector4 &, const S_vector &, int, int, int) { return true; }
bool C_RigidBody::Wake(int, int, int, int, int, int, int, int) { return true; }
bool C_RigidBody::SaveGame(C_chunk &) { return true; }
bool C_RigidBody::LoadGame(C_chunk &) { return true; }
void C_RigidBody::Nudge() {}
void C_RigidBody::Step(uint32_t) {}
void C_FirearmComp::Save(int *, void *) {}
void C_FirearmComp::Load(int, const void *) {}
void S_Explosion::Execute() {}
bool C_chunk::Begin(uint16_t) { return true; }
bool C_chunk::Descend(uint16_t &) { return false; }
bool C_chunk::Ascend(uint32_t *) { return true; }
int C_chunk::RemainingSize() const { return 0; }
void C_str::Release() {}
C_str &C_str::operator=(const char *) { return *this; }
void I3D_frame::UpdateWMatrixProc() {}
S_vector __stdcall S_matrix::GetScale() const { return S_vector(1.0f, 1.0f, 1.0f); }
void __stdcall S_quat::Make(const S_matrix &) {}
extern "C" I3D_driver *I3DGetDriver() { return nullptr; }
extern "C" IGraph *GetIGraph() { return nullptr; }
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *, uint32_t, void *, void *, int) { return E_FAIL; }
template <> HRESULT C_ResourceCache<I3D_sound>::OpenInto(I3D_sound *, const char *, uint32_t, void *, void *, int) { return E_FAIL; }


namespace {

// ============================================================ fake engine objects

void *g_sceneVt[128];
struct S_Fake {
    void **vtable;
    alignas(16) unsigned char bytes[0x400];
};
S_Fake g_scene{g_sceneVt, {}};

// IntersectFrame (the visuals of actors / doors): the next answer.
struct S_Intersect {
    I3D_frame *part = nullptr;
    float distance = 0.0f;
    S_vector normal{0, 0, 0};
};
std::vector<S_Intersect> g_intersects;
std::vector<I3D_frame *> g_intersectFrames;
I3D_frame *__stdcall F_IntersectFrame(void *, const S_vector &, const S_vector &, I3D_frame *frame,
                                      I3D_collision_info *info, uint32_t, int, int, int)
{
    g_intersectFrames.push_back(frame);
    if (g_intersects.empty())
        return nullptr;
    const S_Intersect answer = g_intersects.front();
    g_intersects.erase(g_intersects.begin());
    if (answer.part) {
        info->distance = answer.distance;
        info->normal = answer.normal;
    }
    return answer.part;
}

int &SceneStamp() { return *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(&g_scene) + 0x11C); }
int &FrameStamp(I3D_frame *frame) { return *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(frame) + 0x11C); }

// Frames: zeroed bytes with a vtable (only what the code reads is set).
void *g_frameVt[128];
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(bytes); }
};
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type = FRAME_VISUAL)
{
    S_FakeFrame *fake = new S_FakeFrame();
    std::memset(fake->bytes, 0, sizeof(fake->bytes));
    I3D_frame *frame = fake->frame();
    *reinterpret_cast<void ***>(frame) = g_frameVt;
    frame->m_type = type;
    frame->m_refCount = 1;
    frame->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
    for (int i = 0; i < 4; ++i)
        frame->m_worldMatrix.m[i][i] = 1.0f;
    return frame;
}
I3D_frame *MakeFrameAt(const S_vector &pos)
{
    I3D_frame *frame = MakeFrame();
    frame->m_worldMatrix.m[3][0] = pos.x;
    frame->m_worldMatrix.m[3][1] = pos.y;
    frame->m_worldMatrix.m[3][2] = pos.z;
    return frame;
}

// Actors: zeroed bytes as big as a human, a vtable with Hit and GetMaterialId logged.
struct S_HitLog {
    C_Actor *actor;
    E_HitType type;
    float amount;
    S_vector pos, dir;
    C_Actor *source;
    int part;
};
std::vector<S_HitLog> g_hits;
int g_actorMaterial = 0;
void __thiscall F_Hit(C_Actor *self, E_HitType type, float amount, const S_vector &pos, const S_vector &dir,
                      C_Actor *source, int part)
{
    g_hits.push_back({self, type, amount, pos, dir, source, part});
}
int __thiscall F_GetMaterialId(C_Actor *, I3D_frame *) { return g_actorMaterial; }
void *g_actorVt[128];
C_Actor *MakeActor(E_ActorType type, I3D_frame *frame = nullptr)
{
    const size_t size = sizeof(C_Human) + 0x100;
    auto *bytes = static_cast<unsigned char *>(::operator new(size, std::align_val_t(16)));
    std::memset(bytes, 0, size);
    C_Actor *actor = reinterpret_cast<C_Actor *>(bytes);
    *reinterpret_cast<void ***>(actor) = g_actorVt;
    actor->m_type = type;
    actor->m_frame = frame;
    return actor;
}

// ============================================================ the world

// Materials: index = id. 1 soft (absorbs 10, costs 20 to pass), 2 hard (absorbs 50, impenetrable),
// 3 steel (absorbs 200), 4 paper (absorbs 0, costs 1), 76 / 77 stop / pass; hit sounds 100 + id.
std::vector<S_MaterialDef> g_materialTable(80);
void SetMaterial(int id, uint32_t absorption, uint32_t cost)
{
    S_MaterialDef &m = g_materialTable[id];
    std::memset(&m, 0, sizeof(m));
    m.absorption = absorption;
    m.penetrationCost = cost;
    m.hitSound = 100 + id;
    m.hitSoundAlternate = 200 + id;
    m.hitEffect = 300 + id;
    m.decal = -1;
}

S_CollisionVolume *StaticVolume(int material, uint32_t flags = 0)
{
    auto *v = new S_CollisionVolume();
    v->flags = flags | (static_cast<uint32_t>(material) << 24);
    return v;
}
S_CollisionVolume *DynamicVolume(C_Actor *actor, I3D_frame *frame = nullptr)
{
    auto *v = new S_CollisionVolume();
    v->flags = COLLISION_FLAG_DYNAMIC;
    v->actor = actor;
    v->frame = frame;
    return v;
}
S_CollisionHit Hit(const S_vector &pos, const S_vector &normal, S_CollisionVolume *volume, float distance)
{
    S_CollisionHit h{};
    h.pos = pos;
    h.normal = normal;
    h.volume = volume;
    h.distance = distance;
    return h;
}

// The weapon: 100 damage, 800 m/s, full power to 100 m, then 0.5 per metre.
C_FirearmComp g_rifle;
C_Actor *g_shooter;

void Reset(uint32_t role = GAME_ROLE_LOCAL)
{
    g_gameRole = role;
    g_queryHits.clear();
    g_queries.clear();
    g_bodyLog.clear();
    g_sounds.clear();
    g_framesHit.clear();
    g_effects.clear();
    g_hits.clear();
    g_intersects.clear();
    g_intersectFrames.clear();
    g_randFloat = 0.5f;
    g_activePlayer = nullptr;
    g_actorMaterial = 0;
    g_carMaterial = MATERIAL_NONE;
    g_keeperDamage.clear();
    g_keeperSurvives = true;
    g_impulses.clear();
    SceneStamp() = 7;
}

C_BulletShot *Fire(const S_vector &pos, const S_vector &dir, bool fake = false, bool flare = false)
{
    auto *bullet = new C_BulletShot();
    bullet->Init(g_shooter, pos, dir, &g_rifle, nullptr, fake, flare);
    return bullet;
}

// The original's DeflectVector (0x00448380), written from its assembly: the length (1 when its square
// is within 1e-8 of 1), the unit direction, d = (y*ny + z*nz + x*nx) * factor, dir -= n * d, normalised
// again, times the length.
S_vector DeflectRef(S_vector dir, const S_vector &n, float factor)
{
    const float sq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
    const float length = std::fabs(sq - 1.0f) < 1e-8f ? 1.0f : std::sqrt(sq);
    dir.Normalize();
    const float d = (dir.y * n.y + dir.z * n.z + dir.x * n.x) * factor;
    dir.x -= n.x * d;
    dir.y -= n.y * d;
    dir.z -= n.z * d;
    dir.Normalize();
    return S_vector(dir.x * length, dir.y * length, dir.z * length);
}

// ============================================================ tests

void TestDamageAt()
{
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    CHECK(Same(b->GetDamageAt(0.0f, 100.0f), 100.0f));
    CHECK(Same(b->GetDamageAt(100.0f, 100.0f), 100.0f));                  // the ideal range itself: full
    CHECK(Same(b->GetDamageAt(150.0f, 100.0f), 75.0f));                   // 50 m * 0.5
    CHECK(Same(b->GetDamageAt(300.0f, 100.0f), 0.0f));                    // loss 100 == damage: 0
    CHECK(Same(b->GetDamageAt(301.0f, 100.0f), 0.0f));                    // loss 100.5 > damage: 0
    CHECK(Same(b->GetDamageAt(299.0f, 100.0f), 0.5f));
    CHECK(Same(b->GetDamageAt(120.0f, 40.0f), 30.0f));                    // the damage passed, not the weapon's
}

void TestInit()
{
    Reset();
    C_BulletShot *b = Fire(S_vector(1, 2, 3), S_vector(0, 0, 1));
    CHECK(Same(b->m_pos, S_vector(1, 2, 3)) && Same(b->m_prevPos, S_vector(1, 2, 3)));
    CHECK(Same(b->m_velocity, S_vector(0, 0, 800)));                     // the direction times the muzzle velocity
    CHECK(Same(b->m_damage, 100.0f) && b->m_distance == 0.0f && !b->m_finished && !b->m_ricocheted);
    CHECK(b->m_shooter == g_shooter && b->m_weapon == &g_rifle && b->m_visual == nullptr && b->m_flareSparks == -1);
}

void TestFreeFlight()
{
    Reset();
    C_BulletShot *b = Fire(S_vector(0, 10, 0), S_vector(0.6f, 0, 0.8f));
    CHECK(!b->Tick(100));
    // step = dt * v * 0.001; nothing hit: the end is pos + step.
    const S_vector v0 = S_vector(0.6f, 0, 0.8f) * 800.0f;
    const S_vector step(100.0f * v0.x * 0.001f, 100.0f * v0.y * 0.001f, 100.0f * v0.z * 0.001f);
    CHECK(Same(b->m_pos, S_vector(0, 10, 0) + step));
    CHECK(Same(b->m_prevPos, S_vector(0, 10, 0)));
    CHECK(Same(b->m_step, b->m_pos - S_vector(0, 10, 0)));
    // Gravity after the step: the next one falls.
    CHECK(Same(b->m_velocity.y, -(100.0f * 0.001f * 9.8f)) && Same(b->m_velocity.x, v0.x) && Same(b->m_velocity.z, v0.z));
    CHECK(Same(b->m_distance, step.Magnitude()));
    // The query: from the position along the step, the shots' filters.
    CHECK(g_queries.size() == 1 && Same(g_queries[0].from, S_vector(0, 10, 0)) && Same(g_queries[0].dir, step));
    CHECK(g_queries[0].excludeTypes == 0x48001 && g_queries[0].excludeFlags == 0x200000);
    // Within 5 m of the muzzle the shooter's body is out of the world around the query, and only then.
    CHECK((g_bodyLog == std::vector<std::string>{"remove", "restore"}));
    g_bodyLog.clear();
    CHECK(!b->Tick(100));                                                 // 80 m out now
    CHECK(g_bodyLog.empty());
    CHECK(b->m_velocity.y < -(100.0f * 0.001f * 9.8f) * 1.5f);
    CHECK(b->m_interpolation == 0);
}

void TestFlareHalfTime()
{
    Reset();
    // (A flare's Init makes its model through the driver; the halving reads only m_flare.)
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->m_flare = true;
    CHECK(!b->Tick(100));                                                 // velocity.y == 0: the full step
    CHECK(Same(b->m_pos.z, 100.0f * 800.0f * 0.001f));
    const float vy = b->m_velocity.y;                                     // now falling: half time
    CHECK(Same(vy, -(100.0f * 0.001f * 9.8f)));
    const S_vector before = b->m_pos;
    CHECK(!b->Tick(100));
    CHECK(Same(b->m_pos.z, before.z + 50.0f * 800.0f * 0.001f));
    CHECK(Same(b->m_velocity.y, vy - 50.0f * 0.001f * 9.8f));
}

void TestRangeEnd()
{
    Reset();
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    int ticks = 0;
    while (!b->m_finished && ticks < 20) {
        CHECK(!b->Tick(100));                                             // 80 m a tick; the end is still drawn
        ++ticks;
    }
    CHECK(ticks == 8);                                                    // 8 * 80 = 640 > 600 (7 * 80 = 560)
    CHECK(b->Tick(100));                                                  // then gone
    // A server alone ends it at once.
    Reset(GAME_ROLE_SERVER);
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    for (int i = 0; i < 7; ++i)
        CHECK(!b->Tick(100));
    CHECK(!b->Tick(100) && b->m_finished);                                // (the distance check comes after the step)
    CHECK(b->Tick(100));
}

void TestBoundaries()
{
    // 600 m exactly does not end it (> 600).
    Reset();
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_distance = 520.0f;
    b->Tick(100);                                                         // + 80
    CHECK(Same(b->m_distance, 600.0f) && !b->m_finished);
    b->Tick(1);
    CHECK(b->m_finished);
    // Power equal to the absorption: not weaker (<), so no ricochet: the impenetrable surface stops it.
    Reset();
    SetMaterial(7, 100, MATERIAL_IMPENETRABLE);
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 1, 0), StaticVolume(7), 0.25f)};
    b = Fire(S_vector(0, 1, 0), S_vector(0, -0.6f, 0.8f));
    b->Tick(100);
    CHECK(b->m_finished && !b->m_ricocheted);
    // One above it: a ricochet.
    Reset();
    SetMaterial(7, 101, MATERIAL_IMPENETRABLE);
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 1, 0), StaticVolume(7), 0.25f)};
    b = Fire(S_vector(0, 1, 0), S_vector(0, -0.6f, 0.8f));
    b->Tick(100);
    CHECK(!b->m_finished && b->m_ricocheted);
    // The shooter's collision is left alone from 5 m on (< 5).
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_distance = 5.0f;
    b->Tick(10);
    CHECK(g_bodyLog.empty());
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_distance = 4.99f;
    b->Tick(10);
    CHECK(g_bodyLog.size() == 2);
}

void TestStopPassNone()
{
    // PASS skipped, STOP ends at its point; nothing played for either.
    Reset();
    g_queryHits = {Hit(S_vector(0, 0, 30), S_vector(0, 0, -1), StaticVolume(MATERIAL_STOP), 0.4f),
                   Hit(S_vector(0, 0, 10), S_vector(0, 0, -1), StaticVolume(MATERIAL_PASS), 0.1f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    CHECK(!b->Tick(100));                                                 // a client keeps it one more tick
    CHECK(b->m_finished && Same(b->m_pos, S_vector(0, 0, 30)) && g_sounds.empty() && g_hits.empty());
    CHECK(Same(b->m_distance, 0.0f));                                     // (not added once finished)
    g_queryHits.clear();
    CHECK(b->Tick(100));
    // A server-only machine ends it in the same tick.
    Reset(GAME_ROLE_SERVER);
    g_queryHits = {Hit(S_vector(0, 0, 30), S_vector(0, 0, -1), StaticVolume(MATERIAL_STOP), 0.4f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    CHECK(b->Tick(100));
}

void TestSortedAndSounds()
{
    // Two soft walls handed back in the wrong order: met nearest first, both played (client), both passed.
    Reset();
    g_queryHits = {Hit(S_vector(0, 0, 40), S_vector(0, 0, -1), StaticVolume(1), 0.5f),
                   Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(4), 0.25f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    CHECK(!b->Tick(100));
    CHECK(g_sounds.size() == 2 && g_sounds[0].group == 104 && g_sounds[1].group == 101);
    CHECK(Same(g_sounds[0].pos, S_vector(0, 0, 20)) && g_sounds[0].chapter == SOUND_CHAPTER_BULLET_HIT &&
          g_sounds[0].channel == 1 && g_sounds[0].flags == 0 && g_sounds[0].source == g_shooter);
    // Damage: 100 - 1 (paper) - 20 (soft); the bullet flies on to the end of its step.
    CHECK(Same(b->m_damage, 79.0f) && !b->m_finished && Same(b->m_pos, S_vector(0, 0, 80)));
    // The hit effects face the camera (it looks along +z from z = -10): played.
    CHECK((g_effects == std::vector<int>{304, 301}));
    // A server-only machine plays nothing.
    Reset(GAME_ROLE_SERVER);
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(4), 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_sounds.empty() && g_effects.empty() && Same(b->m_damage, 99.0f));
    // A fake shot's sounds are silent to the AI.
    Reset();
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(4), 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1), true);
    b->Tick(100);
    CHECK(g_sounds.size() == 1 && g_sounds[0].flags == SOUND_SILENT_TO_AI);
}

void TestPenetration()
{
    // Impenetrable but weaker than the bullet: stops there (no ricochet: the power is above the absorption).
    Reset();
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(2), 0.25f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    CHECK(!b->Tick(100) && b->m_finished && !b->m_ricocheted && Same(b->m_pos, S_vector(0, 0, 20)));
    CHECK(Same(b->m_damage, 100.0f));
    // Cost above the power: stops too.
    Reset();
    SetMaterial(5, 10, 150);
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(5), 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(b->m_finished && Same(b->m_damage, 100.0f));
    // Cost equal to the power: passes (<=), the damage spent.
    Reset();
    SetMaterial(5, 10, 100);
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(5), 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(!b->m_finished && Same(b->m_damage, 0.0f));
    // The power is the damage at the hit's distance (from the shot's start): beyond the ideal range less passes.
    Reset();
    SetMaterial(5, 10, 80);
    C_BulletShot *distant = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    distant->m_distance = 130.0f;                                             // + 20 m to the wall: 150 m, power 75
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(5), 0.25f)};
    distant->Tick(100);
    CHECK(distant->m_finished && Same(distant->m_damage, 100.0f));                // 80 > 75: stopped
    SetMaterial(5, 10, 75);
    distant = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    distant->m_distance = 130.0f;
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(5), 0.25f)};
    distant->Tick(100);
    CHECK(!distant->m_finished && Same(distant->m_damage, 25.0f));                // 75 <= 75: through, 100 - 75
}

void TestRicochet()
{
    // Steel absorbs more than the power: a glancing hit ricochets once.
    Reset();
    const S_vector normal(0, 1, 0);
    g_queryHits = {Hit(S_vector(0, 0, 20), normal, StaticVolume(3), 0.25f)};
    C_BulletShot *b = Fire(S_vector(0, 1, 0), S_vector(0, -0.6f, 0.8f));
    const S_vector v0 = b->m_velocity;
    g_randFloat = 0.25f;
    CHECK(!b->Tick(100));
    CHECK(b->m_ricocheted && !b->m_finished && Same(b->m_pos, S_vector(0, 0, 20)));
    S_vector want = DeflectRef(v0, normal, 0.25f * 0.1f + 1.1f);
    want.x *= 0.2f;
    want.y *= 0.2f;
    want.z *= 0.2f;
    want.y -= 100.0f * 0.001f * 9.8f;                                     // gravity after the hits
    CHECK(Same(b->m_velocity, want));
    CHECK(Same(b->m_damage, 100.0f * 0.3f));
    CHECK(b->m_velocity.y > 0.0f);                                        // reflected upwards
    // The distance counts to the ricochet point.
    CHECK(Same(b->m_distance, (S_vector(0, 0, 20) - S_vector(0, 1, 0)).Magnitude()));
    // A second one ends it.
    g_queryHits = {Hit(b->m_pos + S_vector(0, 0, 1), S_vector(0, 0, -1), StaticVolume(3), 0.1f)};
    b->Tick(100);
    CHECK(b->m_finished);
    // Head-on (dir . normal < -0.95): no ricochet.
    Reset();
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 0, -1), StaticVolume(3), 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(b->m_finished && !b->m_ricocheted && Same(b->m_damage, 100.0f));
    // dir . normal == -0.95 exactly is not below it: a ricochet. (0.95 is not exact in float: the normal's
    // z is the float nearest -0.95 and dir is +z.)
    Reset();
    const float nz = -0.95f;
    const float ny = std::sqrt(1.0f - nz * nz);
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, ny, nz), StaticVolume(3), 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(b->m_ricocheted && !b->m_finished);
}

void TestActors()
{
    // A barrel (the "other actors" branch): its visual tested, the hit at the visual's point, Hit with the power
    // above its material's absorption and the part hit, the script told, the bullet through it (soft).
    Reset();
    I3D_frame *barrelFrame = MakeFrame();
    I3D_frame *part = MakeFrame();
    C_Actor *barrel = MakeActor(ACTOR_BARREL, barrelFrame);
    g_actorMaterial = 1;
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_intersectFrames.size() == 1 && g_intersectFrames[0] == barrelFrame);
    CHECK(g_hits.size() == 1 && g_hits[0].actor == barrel && g_hits[0].type == HIT_BULLET);
    CHECK(Same(g_hits[0].amount, 90.0f) && g_hits[0].source == g_shooter);
    CHECK(Same(g_hits[0].pos, S_vector(0, 0, 15)));                     // moved to the visual's point
    CHECK(g_hits[0].part == static_cast<int>(reinterpret_cast<intptr_t>(part)));
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == barrelFrame);
    CHECK(Same(b->m_damage, 80.0f) && !b->m_finished);
    CHECK(g_sounds.size() == 1 && g_sounds[0].group == 101 && Same(g_sounds[0].pos, S_vector(0, 0, 15)));
    // Its visual missed: nothing.
    Reset();
    g_intersects = {{nullptr, 0.0f, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.empty() && g_sounds.empty() && Same(b->m_damage, 100.0f));
    // Two volumes of one actor in a row: one hit. The shooter's own volume: skipped.
    Reset();
    g_actorMaterial = 4;
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}, {part, 16.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f),
                   Hit(S_vector(0, 0, 16), S_vector(0, 1, 0), DynamicVolume(barrel), 0.21f),
                   Hit(S_vector(0, 0, 5), S_vector(0, 1, 0), DynamicVolume(g_shooter), 0.05f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.size() == 1 && g_intersectFrames.size() == 1);
    // ...but the same actor again after another one is hit again.
    Reset();
    C_Actor *other = MakeActor(ACTOR_BARREL, MakeFrame());
    g_actorMaterial = 4;
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}, {part, 16.0f, S_vector(0, 0, -1)}, {part, 17.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f),
                   Hit(S_vector(0, 0, 16), S_vector(0, 1, 0), DynamicVolume(other), 0.21f),
                   Hit(S_vector(0, 0, 18), S_vector(0, 1, 0), DynamicVolume(barrel), 0.22f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.size() == 3 && g_hits[2].actor == barrel);
    // A fake shot skips the actors.
    Reset();
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1), true);
    b->Tick(100);
    CHECK(g_hits.empty() && g_intersectFrames.empty() && g_sounds.empty());
    // Absorption not below the power: no Hit (strictly below).
    Reset();
    SetMaterial(6, 100, MATERIAL_IMPENETRABLE);
    g_actorMaterial = 6;
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.empty() && b->m_finished);                               // and stopped (100 is not below 100)
    // A server-only machine hits without the sounds; a client-only one plays without hitting.
    Reset(GAME_ROLE_SERVER);
    g_actorMaterial = 1;
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.size() == 1 && g_sounds.empty() && g_framesHit.size() == 1);
    Reset(GAME_ROLE_CLIENT);
    g_actorMaterial = 1;
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(barrel), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.empty() && g_sounds.size() == 1 && g_framesHit.empty());
}

void TestNearMiss()
{
    // The listener at (10, 5, 100): a bullet along +z at x = 11 passes it at 1 m (heard), at x = 13.5 at
    // 3.5 m (not), one stopping short (going away is not "passing": the segment must pass its foot).
    {
        Reset();
        C_Actor *listener = MakeActor(ACTOR_HUMAN);
        reinterpret_cast<C_Human *>(listener)->m_bodyFrame = MakeFrameAt(S_vector(10, 5, 100));
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        C_BulletShot *b = Fire(S_vector(11, 5, 60), S_vector(0, 0, 1));
        b->Tick(100);                                                     // z 60 -> 140
        CHECK(g_sounds.size() == 1 && g_sounds[0].group == 11 && Same(g_sounds[0].pos, S_vector(10, 5, 100)));
        CHECK(g_sounds.size() == 1 && g_sounds[0].source == nullptr && g_sounds[0].chapter == SOUND_CHAPTER_BULLET_HIT);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(12.9f, 5, 60), S_vector(0, 0, 1));             // 2.9 m: heard (9 > 8.41)
        b->Tick(100);
        CHECK(g_sounds.size() == 1);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(13.1f, 5, 60), S_vector(0, 0, 1));             // 3.1 m: not (9.61 >= 9)
        b->Tick(100);
        CHECK(g_sounds.empty());
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(13, 5, 60), S_vector(0, 0, 1));                // 3 m exactly: not (9 >= 9)
        b->Tick(100);
        CHECK(g_sounds.empty());
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(11, 5, 100), S_vector(0, 0, 1));               // starting level with it: passing (0 > 0 fails)
        b->Tick(100);
        CHECK(g_sounds.size() == 1);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(11, 5, 20), S_vector(0, 0, 1));                // ending level with it: passing (0 < 0 fails)
        b->Tick(100);
        CHECK(g_sounds.size() == 1);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(10, 7.9f, 60), S_vector(0, 0, 1));             // 2.9 m above: heard (all three axes count)
        b->Tick(100);
        CHECK(g_sounds.size() == 1);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(10, 5, 10), S_vector(0, 0, 1));                // z 10 -> 90: not yet past
        b->Tick(100);
        CHECK(g_sounds.empty() && !b->m_nearMissPlayed);
        b->Tick(100);                                                     // 90 -> 170: past
        CHECK(g_sounds.size() == 1 && b->m_nearMissPlayed);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(10, 5, 110), S_vector(0, 0, 1));               // starting past it, going away
        b->Tick(100);
        CHECK(g_sounds.empty());
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = Fire(S_vector(10, 5, 140), S_vector(0, 0, -1));              // coming back towards it: passes
        b->Tick(100);
        CHECK(g_sounds.size() == 1);
        // A server-only machine hears nothing.
        Reset(GAME_ROLE_SERVER);
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        Fire(S_vector(11, 5, 60), S_vector(0, 0, 1))->Tick(100);
        CHECK(g_sounds.empty());
        // A stopped bullet still checks its last segment (client): heard when it ends past the listener.
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        g_queryHits = {Hit(S_vector(11, 5, 120), S_vector(0, 0, -1), StaticVolume(MATERIAL_STOP), 0.75f)};
        b = Fire(S_vector(11, 5, 60), S_vector(0, 0, 1));
        b->Tick(100);
        CHECK(b->m_finished && g_sounds.size() == 1);
        Reset();
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        g_queryHits = {Hit(S_vector(11, 5, 90), S_vector(0, 0, -1), StaticVolume(MATERIAL_STOP), 0.375f)};
        b = Fire(S_vector(11, 5, 60), S_vector(0, 0, 1));
        b->Tick(100);
        CHECK(b->m_finished && g_sounds.empty());                         // stopped before passing it
    }
    // A client hears a bullet pass within 3 m of the active player (not the shooter): once.
    Reset();
    C_Actor *player = MakeActor(ACTOR_HUMAN);
    I3D_frame *body = MakeFrame();
    reinterpret_cast<C_Human *>(player)->m_bodyFrame = body;
    g_activePlayer = reinterpret_cast<C_Human *>(player);
    // (The listener is the body frame's world position: the fake frame's is 0, the origin.)
    C_BulletShot *b = Fire(S_vector(1, 0, -40), S_vector(0, 0, 1));
    b->Tick(100);                                                         // passes the origin at 1 m
    int nearMiss = 0;
    for (const S_SoundRequest &s : g_sounds)
        nearMiss += s.group == 11 && s.channel == 0 && s.flags == SOUND_SILENT_TO_AI;
    CHECK(nearMiss == 1 && b->m_nearMissPlayed);
    g_sounds.clear();
    b->Tick(100);
    CHECK(g_sounds.empty());
    // Far away (5 m): none.
    Reset();
    g_activePlayer = reinterpret_cast<C_Human *>(player);
    b = Fire(S_vector(5, 0, -40), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_sounds.empty() && !b->m_nearMissPlayed);
    // The shooter is the player: none.
    Reset();
    g_activePlayer = reinterpret_cast<C_Human *>(g_shooter);
    b = Fire(S_vector(1, 0, -40), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_sounds.empty());
}


void TestLevelMesh()
{
    // A static volume with a visible model (flags & 0xC0, a model or visual frame drawn in the last frame):
    // the bullet tested against its mesh; the hit moved to the mesh's point, the script told (server).
    Reset();
    I3D_frame *wall = MakeFrame(FRAME_VISUAL);
    I3D_frame *piece = MakeFrame(FRAME_VISUAL);
    FrameStamp(wall) = 7;
    S_CollisionVolume *v = StaticVolume(4, COLLISION_FLAG_PRIMITIVE);
    v->frame = wall;
    g_intersects = {{piece, 25.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 1, 0), v, 0.25f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_intersectFrames.size() == 1 && g_intersectFrames[0] == wall);
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == piece);
    CHECK(g_sounds.size() == 1 && Same(g_sounds[0].pos, S_vector(0, 0, 25)) && Same(b->m_damage, 99.0f));
    // The mesh missed: the volume is passed over.
    Reset();
    FrameStamp(wall) = 7;
    g_intersects = {{nullptr, 0.0f, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 1, 0), v, 0.25f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_sounds.empty() && g_framesHit.empty() && Same(b->m_damage, 100.0f));
    // Not drawn in the last frame, or a dummy frame, or no 0xC0 flag: the volume itself counts, untested.
    for (int variant = 0; variant < 3; ++variant) {
        Reset();
        FrameStamp(wall) = variant == 0 ? 6 : 7;
        wall->m_type = variant == 1 ? FRAME_DUMMY : FRAME_VISUAL;
        v->flags = variant == 2 ? (4u << 24) : (4u << 24) | COLLISION_FLAG_PRIMITIVE;
        g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 1, 0), v, 0.25f)};
        b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
        b->Tick(100);
        CHECK(g_intersectFrames.empty() && g_sounds.size() == 1 && Same(g_sounds[0].pos, S_vector(0, 0, 20)));
        CHECK(g_framesHit.empty());
    }
    // A model frame counts as a visual; a client-only machine does not tell the scripts.
    Reset(GAME_ROLE_CLIENT);
    wall->m_type = FRAME_MODEL;
    FrameStamp(wall) = 7;
    v->flags = (4u << 24) | COLLISION_FLAG_PRIMITIVE;
    g_intersects = {{piece, 25.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 20), S_vector(0, 1, 0), v, 0.25f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_intersectFrames.size() == 1 && g_framesHit.empty() && g_sounds.size() == 1);
    // The dynamic flag (0x40) alone is the dynamic branch, not this one: 0x80 is enough here.
    wall->m_type = FRAME_VISUAL;
}

void TestDoorsAndPhysics()
{
    // A dynamic volume without an actor (a door, a physics object, a destructible part).
    I3D_frame *door = MakeFrame();
    I3D_frame *panel = MakeFrame();
    static S_FrameData doorData{};
    static S_FrameData panelData{};
    static S_PhysicsHandle handle{};
    // A destructible part: its keeper's material, damaged by the power above the absorption (server).
    Reset();
    alignas(16) static unsigned char keeperBytes[sizeof(C_VersionKeeper)];
    auto *keeper = reinterpret_cast<C_VersionKeeper *>(keeperBytes);
    keeper->m_materialId = 1;
    panelData.versionKeeper = keeper;
    panelData.physicsObject = g_noPhysicsObject;
    panel->m_data = &panelData;
    door->m_data = nullptr;
    g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_intersectFrames.size() == 1 && g_intersectFrames[0] == door);
    CHECK((g_keeperDamage == std::vector<int>{90}));                     // (int)(100 - 10)
    CHECK(g_framesHit.empty() && g_impulses.empty());                    // (a keeper's part: no script, no push)
    CHECK(g_sounds.size() == 1 && g_sounds[0].group == 101 && Same(b->m_damage, 80.0f));
    // The power beyond the ideal range, less the absorption, truncated.
    Reset();
    g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->m_distance = 100.5f;                                               // + 14 m: 114.5 m, power 92.75
    b->Tick(100);
    CHECK((g_keeperDamage == std::vector<int>{82}));
    // The client alone does not damage it.
    Reset(GAME_ROLE_CLIENT);
    g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_keeperDamage.empty() && g_sounds.size() == 1);
    // No keeper: the part's "MatId" property; the volume frame's physics object pushed with the bullet's
    // velocity at the hit, factor 0.01; the part told to the scripts.
    Reset();
    panelData.versionKeeper = nullptr;
    static char panelProps[] = "Foo 1\r\nMatID 4\r\n";
    panel->m_userProps = panelProps;
    doorData.physicsObject = &handle;
    door->m_data = &doorData;
    g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    const S_vector v0 = b->m_velocity;
    b->Tick(100);
    CHECK(g_impulses.size() == 1 && g_impulses[0].object == &handle && Same(g_impulses[0].impulse, v0) &&
          Same(g_impulses[0].point, S_vector(0, 0, 15)) && Same(g_impulses[0].factor, 0.01f));
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == panel);
    CHECK(g_sounds.size() == 1 && g_sounds[0].group == 104 && Same(b->m_damage, 99.0f));
    // The part has no material but the volume's frame has: that one.
    Reset();
    panel->m_userProps = nullptr;
    static char doorProps[] = "matid1";
    door->m_userProps = doorProps;
    g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_sounds.size() == 1 && g_sounds[0].group == 101 && Same(b->m_damage, 80.0f));
    // Without a physics object the volume's frame is not asked: no material at all ends the bullet there,
    // silently (MATERIAL_NONE).
    Reset();
    doorData.physicsObject = g_noPhysicsObject;
    g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(b->m_finished && Same(b->m_pos, S_vector(0, 0, 15)) && g_sounds.empty() && g_impulses.empty());
    CHECK(g_framesHit.size() == 1);                                      // (told before the material is checked)
    // Its visual missed: passed over.
    Reset();
    g_intersects = {{nullptr, 0.0f, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(!b->m_finished && g_framesHit.empty());
    door->m_userProps = nullptr;
    door->m_data = nullptr;
    panel->m_data = nullptr;
}

void TestGetFrameMaterial()
{
    CHECK(GetFrameMaterial(nullptr) == MATERIAL_NONE);
    I3D_frame *f = MakeFrame();
    CHECK(GetFrameMaterial(f) == MATERIAL_NONE);                         // no properties
    static char a[] = "MATID 12";
    f->m_userProps = a;
    CHECK(GetFrameMaterial(f) == 12);                                    // any case
    static char b[] = "colour 3 matid=7";
    f->m_userProps = b;
    CHECK(GetFrameMaterial(f) == 0);                                     // atoi of "=7"
    static char c[] = "x\r\nMatId    33\r\nMatId 2";
    f->m_userProps = c;
    CHECK(GetFrameMaterial(f) == 33);                                    // the first
    static char d[] = "material 5";
    f->m_userProps = d;
    CHECK(GetFrameMaterial(f) == MATERIAL_NONE);
}

void TestCarsAndStaticWeapons()
{
    // A car: hit (server) with the full power at the hit point along the direction, then its material asked;
    // none: passed over.
    Reset();
    I3D_frame *carFrame = MakeFrame();
    C_Actor *car = MakeActor(ACTOR_CAR, carFrame);
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(car), 0.2f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.size() == 1 && g_hits[0].actor == car && Same(g_hits[0].amount, 100.0f) &&
          Same(g_hits[0].pos, S_vector(0, 0, 14)) && Same(g_hits[0].dir, S_vector(0, 0, 1)) && g_hits[0].part == 0);
    CHECK(g_sounds.empty() && g_framesHit.empty() && !b->m_finished);
    // With a material: the script told about the car's frame, the bullet meets it.
    Reset();
    g_carMaterial = 2;
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(car), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == carFrame && g_sounds.size() == 1 && b->m_finished);
    // A client alone: no Hit, no script.
    Reset(GAME_ROLE_CLIENT);
    g_carMaterial = 2;
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(car), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.empty() && g_framesHit.empty() && g_sounds.size() == 1);
    // The shooter's own vehicle: passed over, unhurt.
    Reset();
    g_carMaterial = 2;
    reinterpret_cast<C_Human *>(g_shooter)->m_vehicle = car;
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(car), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.empty() && g_sounds.empty() && !b->m_finished);
    reinterpret_cast<C_Human *>(g_shooter)->m_vehicle = nullptr;
    // A static weapon: hit (server) with the full power at the bullet's position along its step, before its
    // visual is tested; the visual missed: passed over (hit all the same).
    Reset();
    I3D_frame *gunFrame = MakeFrame();
    C_Actor *gun = MakeActor(ACTOR_STATIC_WEAPON, gunFrame);
    g_intersects = {{nullptr, 0.0f, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(gun), 0.2f)};
    b = Fire(S_vector(0, 0, 2), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.size() == 1 && Same(g_hits[0].amount, 100.0f) && Same(g_hits[0].pos, S_vector(0, 0, 2)) &&
          Same(g_hits[0].dir, S_vector(0, 0, 80)) && g_hits[0].part == 0);
    CHECK(g_intersectFrames.size() == 1 && g_intersectFrames[0] == gunFrame && g_sounds.empty() && !b->m_finished);
    // Its visual hit: its material for that part; a client alone does not hit it.
    Reset(GAME_ROLE_CLIENT);
    g_actorMaterial = 2;
    I3D_frame *part = MakeFrame();
    g_intersects = {{part, 15.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(gun), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.empty() && g_sounds.size() == 1 && g_sounds[0].group == 102 && b->m_finished);
    CHECK(Same(b->m_pos, S_vector(0, 0, 15)) && g_framesHit.empty());
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (void *&slot : g_sceneVt) slot = nullptr;
    g_sceneVt[0x104 / 4] = reinterpret_cast<void *>(F_IntersectFrame);
    g_actorVt[0xD4 / 4 + 1] = reinterpret_cast<void *>(F_Hit);            // (g++: two destructor slots)
    g_actorVt[0xD8 / 4 + 1] = reinterpret_cast<void *>(F_GetMaterialId);
    reinterpret_cast<C_game *>(g_gameBytes)->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
    for (int i = 0; i < 80; ++i)
        SetMaterial(i, 0, MATERIAL_IMPENETRABLE);
    SetMaterial(1, 10, 20);
    SetMaterial(2, 50, MATERIAL_IMPENETRABLE);
    SetMaterial(3, 200, MATERIAL_IMPENETRABLE);
    SetMaterial(4, 0, 1);
    g_materials = g_materialTable.data();
    g_materialCount = 80;
    g_rifle.m_damage = 100.0f;
    g_rifle.m_muzzleVelocity = 800.0f;
    g_rifle.m_idealRange = 100.0f;
    g_rifle.m_damageFalloff = 0.5f;
    g_shooter = MakeActor(ACTOR_HUMAN);
    reinterpret_cast<C_Human *>(g_shooter)->m_role = HUMAN_ROLE_PLAYER;

    TestDamageAt();
    TestInit();
    TestFreeFlight();
    TestFlareHalfTime();
    TestRangeEnd();
    TestStopPassNone();
    TestSortedAndSounds();
    TestPenetration();
    TestRicochet();
    TestActors();
    TestNearMiss();
    TestBoundaries();
    TestLevelMesh();
    TestDoorsAndPhysics();
    TestGetFrameMaterial();
    TestCarsAndStaticWeapons();
    std::printf("shots: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
