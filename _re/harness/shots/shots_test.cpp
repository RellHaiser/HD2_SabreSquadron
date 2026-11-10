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
#include <map>
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
std::vector<std::string> g_chunkLog;
std::vector<uint16_t> g_chunkIds;                                         // what Descend hands out
std::vector<uint32_t> g_bits;                                             // what ReadBits hands out
S_vector g_readPos;
struct S_RayAnswer {
    bool hit = false;
    S_vector pos, normal;
    uint32_t flags = 0;
};
S_RayAnswer g_ray;
std::vector<std::string> g_rayLog;
struct S_ExplosionLog {
    S_vector pos;
    float range, damage;
    bool unknown18;
    C_Actor *source;
};
std::vector<S_ExplosionLog> g_explosions;
int g_difficulty = 2;
std::vector<std::string> g_cameraLog;
bool g_decalsOn;
float g_light = 1.0f;
std::vector<std::string> g_particleLog;
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
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *pos, S_vector *normal,
                                             uint32_t types, uint32_t flags) const
{
    char s[64];
    std::snprintf(s, sizeof s, " %x %x", types, flags);
    g_rayLog.push_back("ray " + std::to_string(from.x) + "," + std::to_string(from.y) + "," + std::to_string(from.z) + " " +
                       std::to_string(dir.x) + "," + std::to_string(dir.y) + "," + std::to_string(dir.z) + s);
    if (!g_ray.hit)
        return nullptr;
    *pos = g_ray.pos;
    *normal = g_ray.normal;
    static S_CollisionVolume volume;
    volume.flags = g_ray.flags;
    return &volume;
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
void C_CameraModule::SetSpecialMode(int mode, bool on, I3D_frame *target)
{
    g_cameraLog.push_back(std::to_string(mode) + (on ? " on " : " off ") + (target ? "frame" : "null"));
}
int C_ParticlesModule::Create(int effect, const S_matrix &, I3D_frame *frame, bool a, bool b)
{
    g_effects.push_back(effect);
    g_particleLog.push_back("create " + std::to_string(effect) + " " + std::to_string(a) + std::to_string(b) +
                            (frame ? " frame" : ""));
    return 41;
}
int C_ParticlesModule::CreateAt(int effect, const S_vector &, const S_vector &, I3D_frame *frame, bool a, bool b)
{
    g_particleLog.push_back("createat " + std::to_string(effect) + " " + std::to_string(a) + std::to_string(b) +
                            (frame ? " frame" : ""));
    g_effects.push_back(effect);
    return 1;
}
void C_ParticlesModule::Destroy(int id) { g_particleLog.push_back("destroy " + std::to_string(id)); }
void C_ParticlesModule::ToggleEmission(int id, int on) { g_particleLog.push_back("toggle " + std::to_string(id) + " " + std::to_string(on)); }
C_PlayerOptions *C_GameProfile::GetDetailSettings() { return reinterpret_cast<C_PlayerOptions *>(g_soundModuleBytes); }
bool C_PlayerOptions::IsEnabled(int option) const { return option == DETAIL_DECALS && g_decalsOn; }
int C_GameProfile::GetDifficulty() const { return g_difficulty; }
C_Human *C_Team::GetActivePlayer() const { return g_activePlayer; }
void C_Team::NotifyGrenadeThrown(const S_vector &, const S_vector &, C_Actor *) {}
namespace {
const C_Human *g_localPlayer;
bool g_friends;
struct S_ShotAt {
    const C_Human *human;
    C_Actor *shooter;
    S_vector velocity;
};
std::vector<S_ShotAt> g_shotAt;
std::map<const C_Item *, I3D_frame *> g_itemFrames;
}  // namespace
bool C_Human::IsLocalPlayer() const { return this == g_localPlayer; }
bool C_Human::IsFriendOf(const C_Actor *) const { return g_friends; }
void C_Human::NotifyShotAt(C_Actor *shooter, const S_vector &velocity) { g_shotAt.push_back({this, shooter, velocity}); }
const S_vector &C_Human::GetFacingDir() const { static S_vector dir(0, 0, 1); return dir; }
void C_Human::RemoveItem(int, bool) {}
int C_Car::GetHitMaterial(const S_vector &, const S_vector &) { return g_carMaterial; }
bool C_VersionKeeper::Damage(int damage, I3D_frame **, E_VersionEffects) { g_keeperDamage.push_back(damage); return g_keeperSurvives; }
bool C_VersionMaster::DamageFrame(I3D_frame *, int) { return true; }
namespace {
std::vector<int> g_itemFrameIndices;
}
I3D_frame *C_Item::GetFrame(int index)
{
    g_itemFrameIndices.push_back(index);
    return g_itemFrames.count(this) ? g_itemFrames[this] : nullptr;
}
I3D_frame *C_WeaponItem::GetBlastDummy() { return nullptr; }
I3D_frame *C_WeaponItem::GetCartridgeDummy() { return nullptr; }
I3D_frame *C_FirstPersonView::GetBlastDummy() { return nullptr; }
I3D_frame *C_FirstPersonView::GetCartridgeDummy() { return nullptr; }
C_Module::C_Module(int id, const char *) : m_id(id) {}
C_Module::~C_Module() {}
C_BitStream *C_NetGame::CreateMessage(int, int) { return nullptr; }
void C_NetGame::SendNetMessage(C_BitStream *) {}
void C_NetMessage::ReadPosition(S_vector &pos) { pos = g_readPos; }
void C_NetMessage::WritePosition(const S_vector &) {}
uint32_t C_BitStream::ReadBits(int bits)
{
    if (g_bits.empty())
        return 0xDEAD;
    const uint32_t v = g_bits.front();
    g_bits.erase(g_bits.begin());
    g_chunkLog.push_back("bits " + std::to_string(bits));
    return v;
}
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
void C_ProjectileWeaponComp::Save(int *, void *) {}
void C_ProjectileWeaponComp::Load(int, const void *) {}
void S_Explosion::Execute() { g_explosions.push_back({pos, range, damage, unknown18, source}); }
bool C_chunk::Begin(uint16_t id) { g_chunkLog.push_back("begin " + std::to_string(id)); return true; }
bool C_chunk::Descend(uint16_t &id)
{
    if (g_chunkIds.empty())
        return false;
    id = g_chunkIds.front();
    g_chunkIds.erase(g_chunkIds.begin());
    g_chunkLog.push_back("descend " + std::to_string(id));
    return true;
}
bool C_chunk::Ascend(uint32_t *) { g_chunkLog.push_back("ascend"); return true; }
int C_chunk::RemainingSize() const { return static_cast<int>(g_chunkIds.size()); }
void I3D_frame::UpdateWMatrixProc() {}
S_vector __stdcall S_matrix::GetScale() const
{
    return S_vector(std::sqrt(m[0][0] * m[0][0] + m[0][1] * m[0][1] + m[0][2] * m[0][2]),
                    std::sqrt(m[1][0] * m[1][0] + m[1][1] * m[1][1] + m[1][2] * m[1][2]),
                    std::sqrt(m[2][0] * m[2][0] + m[2][1] * m[2][1] + m[2][2] * m[2][2]));
}
void __stdcall S_quat::Make(const S_matrix &) {}
namespace {
void *g_driverVt[128];
struct S_FakeDriver {
    void **vtable = g_driverVt;
} g_driver;
}  // namespace
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
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
std::vector<uint32_t> g_intersectFlags;
int g_intersectExtra;                                                    // or of the three unidentified arguments
I3D_frame *__stdcall F_IntersectFrame(void *, const S_vector &, const S_vector &, I3D_frame *frame,
                                      I3D_collision_info *info, uint32_t flags, int a, int b, int c)
{
    g_intersectExtra |= a | b | c;
    g_intersectFrames.push_back(frame);
    g_intersectFlags.push_back(flags);
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

// Frames: zeroed bytes with a vtable; the engine calls the code makes are logged ("<call> <name> ...").
void *g_frameVt[128];
std::map<const I3D_frame *, std::string> g_frameNames;
std::map<std::pair<const I3D_frame *, std::string>, I3D_frame *> g_frameChildren;
std::vector<std::string> g_frameLog;
std::string FrameName(const I3D_frame *f) { return f ? (g_frameNames.count(f) ? g_frameNames[f] : "?") : "null"; }
std::string Vec(const S_vector &v)
{
    char s[96];
    std::snprintf(s, sizeof s, "%g,%g,%g", v.x, v.y, v.z);
    return s;
}
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type = FRAME_VISUAL, const char *name = nullptr);
std::vector<std::string> g_sceneLog;
float __stdcall F_GetLightIntensity(void *, const S_vector &pos, I3D_sector *sector, int a, int b)
{
    g_sceneLog.push_back("light " + Vec(pos) + (sector ? " sector " : " - ") + std::to_string(a) + std::to_string(b));
    return g_light;
}
I3D_frame *g_sectorFrame;
I3D_sector *__stdcall F_GetSector(void *, const S_vector &pos)
{
    g_sceneLog.push_back("sector " + Vec(pos));
    return reinterpret_cast<I3D_sector *>(g_sectorFrame);
}
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    g_sceneLog.push_back("createframe " + std::to_string(type));
    return MakeFrame(FRAME_VISUAL, "sound");
}
int g_visualsMade;
I3D_frame *__stdcall F_CreateVisual(void *, uint32_t type)
{
    I3D_frame *f = MakeFrame(FRAME_VISUAL, ("decal" + std::to_string(++g_visualsMade)).c_str());
    g_sceneLog.push_back("create " + std::to_string(type));
    S_vector &base = *reinterpret_cast<S_vector *>(reinterpret_cast<unsigned char *>(f) + 0x1F0);
    base = S_vector(0.5f, 0.25f, 1.0f);
    return f;
}
uint32_t __stdcall F_FrRelease(I3D_frame *self) { g_frameLog.push_back("release " + FrameName(self)); return 0; }
uint32_t g_visualType = 0;
std::map<const I3D_frame *, uint32_t> g_visualTypes;
uint32_t __stdcall F_FrGetVisualType(I3D_frame *self) { return g_visualTypes.count(self) ? g_visualTypes[self] : g_visualType; }
void __stdcall F_FrSetPos(I3D_frame *self, const S_vector &pos)
{
    g_frameLog.push_back("pos " + FrameName(self) + " " + Vec(pos));
    self->m_localMatrix(3) = pos;
    self->m_worldMatrix(3) = pos;
}
void __stdcall F_FrSetDir(I3D_frame *self, const S_vector &dir, float roll)
{
    char s[32];
    std::snprintf(s, sizeof s, " %g", roll);
    g_frameLog.push_back("dir " + FrameName(self) + " " + Vec(dir) + s);
}
void __stdcall F_FrSetWorldDir(I3D_frame *self, const S_vector &dir, float roll)
{
    char s[32];
    std::snprintf(s, sizeof s, " %g", roll);
    g_frameLog.push_back("worlddir " + FrameName(self) + " " + Vec(dir) + s);
}
void __stdcall F_FrUpdate(I3D_frame *self) { g_frameLog.push_back("update " + FrameName(self)); }
void __stdcall F_FrSetOn(I3D_frame *self, bool on) { g_frameLog.push_back(std::string(on ? "on " : "off ") + FrameName(self)); }
void __stdcall F_FrLinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    g_frameLog.push_back("link " + FrameName(self) + " " + FrameName(parent) + " " + std::to_string(flags));
}
I3D_frame *__stdcall F_FrFindChild(I3D_frame *self, const char *name, uint32_t)
{
    auto it = g_frameChildren.find({self, name});
    return it == g_frameChildren.end() ? nullptr : it->second;
}
void __stdcall F_PrSetTexture(I3D_frame *self, IImage *image)
{
    g_frameLog.push_back("texture " + FrameName(self) + " " + std::to_string(reinterpret_cast<uintptr_t>(image)));
}
void __stdcall F_PrSetOrthogonal(I3D_frame *self, int v) { g_frameLog.push_back("ortho " + FrameName(self) + " " + std::to_string(v)); }
void __stdcall F_PrSetColor(I3D_frame *self, const S_vector &c) { g_frameLog.push_back("color " + FrameName(self) + " " + Vec(c)); }
void __stdcall F_PrSetMode(I3D_frame *self, int v) { g_frameLog.push_back("mode " + FrameName(self) + " " + std::to_string(v)); }
void __stdcall F_PrSetBuffered(I3D_frame *self, int v) { g_frameLog.push_back("buffered " + FrameName(self) + " " + std::to_string(v)); }
HRESULT __stdcall F_FrDuplicate(I3D_frame *self, I3D_frame *source)
{
    g_frameLog.push_back("duplicate " + FrameName(self) + " " + FrameName(source));
    return 0;
}
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(bytes); }
};
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type, const char *name)
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
    static int count;
    g_frameNames[frame] = name ? name : "f" + std::to_string(++count);
    frame->m_name = const_cast<char *>(g_frameNames[frame].c_str());
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
    m.explosionEffect = 500 + id;
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
    g_localPlayer = nullptr;
    g_friends = false;
    g_shotAt.clear();
    g_intersectFlags.clear();
    g_frameLog.clear();
    g_visualType = 0;
    g_particleLog.clear();
    reinterpret_cast<C_game *>(g_gameBytes)->m_signalPistolFired = false;
    g_decalsOn = false;
    g_light = 1.0f;
    g_sceneLog.clear();
    g_intersectExtra = 0;
    g_itemFrameIndices.clear();
    g_explosions.clear();
    g_difficulty = 2;
    g_cameraLog.clear();
    g_chunkLog.clear();
    g_chunkIds.clear();
    g_bits.clear();
    g_ray = S_RayAnswer();
    g_rayLog.clear();
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

// ---- humans and hit zones ----

// A container whose every virtual is GetItem (the only call the shots make on one).
C_Item *g_headItem;
C_Item *__thiscall F_GetItem(void *) { return g_headItem; }
void *g_containerVt[64];
struct S_FakeContainer {
    void **vtable = g_containerVt;
    unsigned char bytes[0x40] = {};
};
S_FakeContainer g_headContainer;
C_ItemContainer *g_containers[8];

// A human with the hit zones' joints: 22 joint frames, joint j at (j, 10 + j, 100 + j).
struct S_TestHuman {
    C_Human *human;
    I3D_frame *frame;
    I3D_frame *mesh;
    I3D_frame *joints[22];
};
S_TestHuman MakeHuman(const char *name)
{
    S_TestHuman h{};
    h.frame = MakeFrame(FRAME_MODEL, name);
    h.mesh = MakeFrame(FRAME_VISUAL, (std::string(name) + ".a").c_str());
    g_frameChildren[{h.frame, "a"}] = h.mesh;
    for (int j = 0; j < 22; ++j)
        h.joints[j] = MakeFrameAt(S_vector(static_cast<float>(j), 10.0f + j, 100.0f + j));
    *reinterpret_cast<I3D_frame ***>(reinterpret_cast<unsigned char *>(h.mesh) + 0x214) = h.joints;
    h.human = reinterpret_cast<C_Human *>(MakeActor(ACTOR_HUMAN, h.frame));
    h.human->m_stats[STAT_HEALTH] = 100;
    h.human->m_role = HUMAN_ROLE_PLAYER;
    h.human->m_containers = g_containers;
    return h;
}

// The zones model and its ten zone frames (the original's names: c f l m h i s t o p).
I3D_frame *g_zones[10];
void SetUpZones()
{
    static const char *const names[10] = {"c", "f", "l", "m", "h", "i", "s", "t", "o", "p"};
    g_hitZones.m_model = MakeFrame(FRAME_MODEL, "zones");
    for (int i = 0; i < 10; ++i)
        g_hitZones.m_zones[i] = g_zones[i] = MakeFrame(FRAME_VISUAL, names[i]);
    for (void *&slot : g_containerVt)
        slot = reinterpret_cast<void *>(F_GetItem);
    g_containers[0] = reinterpret_cast<C_ItemContainer *>(&g_headContainer);
}

// A headgear item of `type` (0: a helmet that stops bullets) with its frame.
C_Item *MakeHeadgear(int type, I3D_frame *frame)
{
    auto *comp = static_cast<C_HeadgearComp *>(::operator new(sizeof(C_HeadgearComp)));
    std::memset(static_cast<void *>(comp), 0, sizeof(C_HeadgearComp));
    comp->m_headgearType = type;
    auto *def = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(def), 0, sizeof(C_ItemDef));
    def->m_component = comp;
    auto *item = static_cast<C_Item *>(::operator new(sizeof(C_Item)));
    std::memset(static_cast<void *>(item), 0, sizeof(C_Item));
    item->m_def = def;
    g_itemFrames[item] = frame;
    return item;
}

// The answers of IntersectFrame for the zone tests: hit only `frame` (null: none).
void HitOnly(I3D_frame *frame, int calls)
{
    g_intersects.clear();
    for (int i = 0; i < calls; ++i)
        g_intersects.push_back({nullptr, 0.0f, S_vector(0, 0, 0)});
    (void)frame;
}

void TestHitZones()
{
    Reset();
    S_TestHuman h = MakeHuman("soldier");
    g_headItem = nullptr;
    const S_vector pos(0, 0, 0), dir(0, 0, 50);
    // No zones model: none, nothing asked.
    I3D_frame *model = g_hitZones.m_model;
    g_hitZones.m_model = nullptr;
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == HIT_ZONE_NONE && g_intersectFrames.empty());
    g_hitZones.m_model = model;
    // No joints: none.
    *reinterpret_cast<I3D_frame ***>(reinterpret_cast<unsigned char *>(h.mesh) + 0x214) = nullptr;
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == HIT_ZONE_NONE && g_intersectFrames.empty());
    *reinterpret_cast<I3D_frame ***>(reinterpret_cast<unsigned char *>(h.mesh) + 0x214) = h.joints;
    // Every zone in turn: the first hit wins; each zone tested so far was put on its joint (its world
    // matrix as the zone's local one), at scale 1, and updated; the later ones not touched.
    static const int joints[10] = {1, 6, 13, 14, 9, 10, 20, 21, 16, 17};
    for (int k = 0; k < 10; ++k) {
        Reset();
        for (int i = 0; i < 10; ++i)
            g_zones[i]->m_localMatrix.m[3][0] = -1.0f;
        g_intersects.clear();
        for (int i = 0; i < k; ++i)
            g_intersects.push_back({nullptr, 0.0f, S_vector(0, 0, 0)});
        g_intersects.push_back({g_zones[k], 1.0f, S_vector(0, 0, -1)});
        CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == k);
        CHECK(static_cast<int>(g_intersectFrames.size()) == k + 1 && g_intersectFrames[k] == g_zones[k]);
        CHECK(g_intersectFlags[k] == 0x08040000u);
        bool placed = true;
        for (int i = 0; i <= k; ++i)
            placed &= Same(g_zones[i]->m_localMatrix.m[3][0], static_cast<float>(joints[i])) &&
                      Same(g_zones[i]->m_localMatrix.m[3][2], 100.0f + joints[i]) &&
                      Same(g_zones[i]->m_scale, S_vector(1, 1, 1));
        for (int i = k + 1; i < 10; ++i)
            placed &= Same(g_zones[i]->m_localMatrix.m[3][0], -1.0f);
        CHECK(placed);
        int updates = 0;
        for (const std::string &line : g_frameLog)
            updates += line.rfind("update ", 0) == 0;
        CHECK(updates == k + 1);
    }
    // None hit: none, all ten tested.
    Reset();
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == HIT_ZONE_NONE && g_intersectFrames.size() == 10);
    CHECK(g_intersectExtra == 0);
    // A joint scaled 2: its zone at scale 1 all the same (the zone keeps its own size).
    Reset();
    for (int i = 0; i < 3; ++i)
        h.joints[1]->m_worldMatrix.m[i][i] = 2.0f;
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == 0 && Same(g_zones[0]->m_scale, S_vector(1, 1, 1)));
    CHECK(Same(g_zones[0]->m_localMatrix.m[0][0], 2.0f));
    for (int i = 0; i < 3; ++i)
        h.joints[1]->m_worldMatrix.m[i][i] = 1.0f;
    // A helmet (headgear type 0) is tested before the head zone, with the helmet flags: hit, HELMET.
    Reset();
    I3D_frame *helmetFrame = MakeFrame(FRAME_MODEL, "helmet");
    g_headItem = MakeHeadgear(0, helmetFrame);
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}, {helmetFrame, 1.0f, S_vector(0, 0, -1)}};
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == HIT_ZONE_HELMET);
    CHECK(g_intersectExtra == 0 && (g_itemFrameIndices == std::vector<int>{0}));
    CHECK(g_intersectFrames.size() == 2 && g_intersectFrames[0] == g_zones[0] && g_intersectFrames[1] == helmetFrame &&
          g_intersectFlags[1] == 0x04040000u);
    // The helmet missed: the head zone next.
    Reset();
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}, {nullptr, 0, S_vector(0, 0, 0)}, {g_zones[1], 1.0f, S_vector(0, 0, -1)}};
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == HIT_ZONE_HEAD && g_intersectFrames[2] == g_zones[1]);
    // Other headgear (type 1): not tested.
    Reset();
    g_headItem = MakeHeadgear(1, helmetFrame);
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}, {g_zones[1], 1.0f, S_vector(0, 0, -1)}};
    CHECK(g_hitZones.GetHitZone(h.human, pos, dir) == HIT_ZONE_HEAD && g_intersectFrames.size() == 2);
    g_headItem = nullptr;
}

void TestBulletsAndHumans()
{
    S_TestHuman target = MakeHuman("target");
    g_headItem = nullptr;
    target.human->m_role = HUMAN_ROLE_ENEMY;                              // (the squad rule below)
    // A zone hit (server): Hit with the power above the human's material's absorption, at the hit along the
    // direction, the zone as the part; no sound, no effect (humans bleed elsewhere). Soft flesh lets it on.
    Reset();
    g_actorMaterial = 1;
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}, {nullptr, 0, S_vector(0, 0, 0)}, {nullptr, 0, S_vector(0, 0, 0)},
                    {g_zones[3], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(target.human), 0.2f)};
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.size() == 1 && g_hits[0].actor == target.human && g_hits[0].type == HIT_BULLET &&
          Same(g_hits[0].amount, 90.0f) && Same(g_hits[0].pos, S_vector(0, 0, 14)) &&
          Same(g_hits[0].dir, S_vector(0, 0, 1)) && g_hits[0].source == g_shooter && g_hits[0].part == 3);
    CHECK(g_sounds.empty() && g_effects.empty() && Same(b->m_damage, 80.0f) && !b->m_finished);
    // The zone test is along the step from the bullet's position.
    // The helmet: MATERIAL_HELMET's absorption (here 12: 20, impenetrable): stopped, hit for 80.
    Reset();
    SetMaterial(MATERIAL_HELMET, 20, MATERIAL_IMPENETRABLE);
    I3D_frame *helmetFrame = MakeFrame(FRAME_MODEL, "helmet2");
    g_headItem = MakeHeadgear(0, helmetFrame);
    g_actorMaterial = 1;
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}, {helmetFrame, 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.size() == 1 && Same(g_hits[0].amount, 80.0f) && g_hits[0].part == HIT_ZONE_HELMET && b->m_finished);
    g_headItem = nullptr;
    // Power not above the absorption: no Hit.
    Reset();
    SetMaterial(8, 100, MATERIAL_IMPENETRABLE);
    g_actorMaterial = 8;
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.empty());
    // A client alone: no Hit (the material still stops or lets the bullet on).
    Reset(GAME_ROLE_CLIENT);
    g_actorMaterial = 1;
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    b->Tick(100);
    CHECK(g_hits.empty() && g_sounds.empty() && Same(b->m_damage, 80.0f));
    // A miss (no zone): the target, hostile, alive, is told it is shot at with the bullet's velocity (server,
    // not a fake shot); the bullet goes on.
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(0, 0, 1));
    const S_vector v0 = b->m_velocity;
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    b->Tick(100);
    CHECK(g_hits.empty() && g_shotAt.size() == 1 && g_shotAt[0].human == target.human &&
          g_shotAt[0].shooter == g_shooter && Same(g_shotAt[0].velocity, v0) && !b->m_finished);
    // ...health 1 is alive,
    Reset();
    target.human->m_stats[STAT_HEALTH] = 1;
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_shotAt.size() == 1);
    target.human->m_stats[STAT_HEALTH] = 100;
    // ...not when a friend, dead, on a client alone, or for a fake shot (which skips the actors altogether).
    for (int variant = 0; variant < 4; ++variant) {
        Reset(variant == 2 ? GAME_ROLE_CLIENT : GAME_ROLE_LOCAL);
        g_friends = variant == 0;
        target.human->m_stats[STAT_HEALTH] = variant == 1 ? 0 : 100;
        g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
        Fire(S_vector(0, 0, 0), S_vector(0, 0, 1), variant == 3)->Tick(100);
        CHECK(g_shotAt.empty() && g_hits.empty());
    }
    target.human->m_stats[STAT_HEALTH] = 100;
    target.human->m_role = HUMAN_ROLE_PLAYER;
    // In a local game the player's squad does not hit itself: the shooter (a player-side AI, not the local
    // player) and the target both HUMAN_ROLE_PLAYER: passed over, no zone test.
    Reset();
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.empty() && g_intersectFrames.empty() && g_shotAt.empty());
    // ...the local player himself does hit them,
    Reset();
    g_localPlayer = reinterpret_cast<C_Human *>(g_shooter);
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.size() == 1);
    // ...an enemy is hit, and a squad member shot by an enemy,
    for (int variant = 0; variant < 2; ++variant) {
        Reset();
        (variant == 0 ? target.human : reinterpret_cast<C_Human *>(g_shooter))->m_role = HUMAN_ROLE_ENEMY;
        g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
        g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
        Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
        CHECK(g_hits.size() == 1);
        target.human->m_role = HUMAN_ROLE_PLAYER;
        reinterpret_cast<C_Human *>(g_shooter)->m_role = HUMAN_ROLE_PLAYER;
    }
    // ...and in a network game (a server alone) all are.
    Reset(GAME_ROLE_SERVER);
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 0, -1), DynamicVolume(target.human), 0.2f)};
    Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
    CHECK(g_hits.size() == 1);
}

// ---- the trace and the flare (C_BulletShot::Update) ----

float &FlareScale(I3D_frame *glow) { return *reinterpret_cast<float *>(reinterpret_cast<unsigned char *>(glow) + 0x1E4); }
std::vector<std::string> Without(const std::vector<std::string> &log, const char *prefix)
{
    std::vector<std::string> out;
    for (const std::string &line : log)
        if (line.rfind(prefix, 0) != 0)
            out.push_back(line);
    return out;
}

// Update's arithmetic as the asm does it (0x0044b400), in float: the part of the step shown so far
// (interpolation * 0.01), this frame's length |step * (dt * 0.01)|, the traced distance kept as an int.
struct S_TraceRef {
    int interpolation = 0;
    int traced = 0;
    float length = 0.0f;
    S_vector shown{0, 0, 0};
    void Step(const S_vector &step, int dt)
    {
        if (interpolation + dt > 100)
            dt = 100 - interpolation;
        interpolation += dt;
        const float f = static_cast<float>(interpolation) * 0.01f;
        shown = S_vector(step.x * f, step.y * f, step.z * f);
        const float g = static_cast<float>(dt) * 0.01f;
        const S_vector part(step.x * g, step.y * g, step.z * g);
        length = std::sqrt(part.x * part.x + part.y * part.y + part.z * part.z);
        traced = static_cast<int>(length + static_cast<float>(traced));
    }
};

void TestTraceUpdate()
{
    // A bullet with a trace visual, after one 100 ms step of 80 m along +x from (0, 1, 0).
    Reset();
    C_BulletShot *b = Fire(S_vector(0, 1, 0), S_vector(1, 0, 0));
    I3D_frame *trace = MakeFrame(FRAME_VISUAL, "trace");
    b->m_visual = trace;
    b->Tick(100);
    CHECK(Same(b->m_step, S_vector(80, 0, 0)) && b->m_interpolation == 0 && b->m_traceDistance == 0);
    g_frameLog.clear();
    S_TraceRef ref;
    // 30 ms: 23 m drawn so far (30 * 0.01 is just under 0.3 in float: 23.999998 truncated); the first 30 m are
    // not shown.
    b->Update(30);
    ref.Step(b->m_step, 30);
    CHECK(ref.traced == 23);
    CHECK((g_frameLog == std::vector<std::string>{"off trace", "update trace"}));
    CHECK(b->m_interpolation == 30 && b->m_traceDistance == ref.traced);
    // 40 ms more: 55 m; shown: placed at the start plus this much of the step, turned along it, stretched to
    // this frame's length, on.
    g_frameLog.clear();
    b->Update(40);
    ref.Step(b->m_step, 40);
    CHECK(ref.traced == 55);
    CHECK((g_frameLog == std::vector<std::string>{"pos trace " + Vec(S_vector(0, 1, 0) + ref.shown), "worlddir trace 1,0,0 0",
                                                  "on trace", "update trace"}));
    CHECK(Same(trace->m_scale, S_vector(1, 1, ref.length)) && b->m_traceDistance == ref.traced);
    // 50 ms more would pass the step's 100: cut to 30.
    g_frameLog.clear();
    b->Update(50);
    ref.Step(b->m_step, 50);
    CHECK(b->m_interpolation == 100 && b->m_traceDistance == ref.traced && Same(trace->m_scale, S_vector(1, 1, ref.length)));
    CHECK(!g_frameLog.empty() && g_frameLog[0] == "pos trace 80,1,0");
    // At 100 already: 0 ms, the length 0 (still drawn: the distance is past 30 m).
    g_frameLog.clear();
    const int before = b->m_traceDistance;
    b->Update(20);
    CHECK(b->m_interpolation == 100 && b->m_traceDistance == before && Same(trace->m_scale, S_vector(1, 1, 0)));
    // Exactly 30 m: shown.
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_visual = trace;
    b->m_step = S_vector(50, 0, 0);
    b->m_traceDistance = 25;
    b->Update(10);
    CHECK(b->m_traceDistance == 30 && std::find(g_frameLog.begin(), g_frameLog.end(), "on trace") != g_frameLog.end());
    // The traced distance is kept as an int: fractions are dropped every frame.
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_visual = trace;
    b->Tick(100);
    for (int i = 0; i < 10; ++i)
        b->Update(1);                                                    // 0.8 m a frame: truncated to 0 each time
    CHECK(b->m_traceDistance == 0 && b->m_interpolation == 10);
    // No visual, a step too short (its square below 1e-16), or burnt out: nothing.
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->Tick(100);
    b->Update(30);
    CHECK(b->m_interpolation == 0);
    b->m_visual = trace;
    b->m_burnedOut = true;
    b->Update(30);
    CHECK(b->m_interpolation == 0 && g_frameLog.empty());
    b->m_burnedOut = false;
    b->m_step = S_vector(1e-9f, 0, 0);
    b->Update(30);
    CHECK(b->m_interpolation == 0 && g_frameLog.empty());
    b->m_step = S_vector(1e-7f, 0, 0);                                   // square 1e-14: drawn
    b->Update(30);
    CHECK(b->m_interpolation == 30);
}

void TestFlareUpdate()
{
    // A flare: its glow ("flare" child) lights up between 30 and 100 m (+ 2 per second, at most 1), with sparks
    // made once; past 100 m it dims (0.5 per second) and burns out at 0.
    Reset();
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    I3D_frame *model = MakeFrame(FRAME_MODEL, "flaremodel");
    I3D_frame *glow = MakeFrame(FRAME_VISUAL, "glow");
    g_frameChildren[{model, "flare"}] = glow;
    FlareScale(glow) = 0.1f;
    b->m_visual = model;
    b->m_flare = true;
    b->m_step = S_vector(100, 0, 0);                                      // (as if a step was flown)
    b->m_interpolation = 0;
    b->Update(20);                                                       // 20 m: not shown
    CHECK(g_particleLog.empty() && Same(FlareScale(glow), 0.1f) && b->m_flareSparks == -1);
    b->Update(20);                                                       // 40 m: sparks, brighter
    CHECK((g_particleLog == std::vector<std::string>{"create 23 11 frame"}) && b->m_flareSparks == 41);
    CHECK(Same(FlareScale(glow), 0.1f + 40.0f * 0.001f));
    CHECK(std::find(g_frameLog.begin(), g_frameLog.end(), "update glow") != g_frameLog.end());
    b->Update(20);                                                       // 60 m: no second sparks
    CHECK(g_particleLog.size() == 1 && Same(FlareScale(glow), (0.1f + 40.0f * 0.001f) + 40.0f * 0.001f));
    FlareScale(glow) = 0.99f;
    b->Update(20);                                                       // 80 m: capped at 1
    CHECK(Same(FlareScale(glow), 1.0f));
    b->Update(20);                                                       // 100 m: still lighting (<= 100)
    CHECK(Same(FlareScale(glow), 1.0f));
    // Past 100 m: dims.
    b->m_step = S_vector(100, 0, 0);
    b->m_interpolation = 0;
    b->m_traceDistance = 100;
    b->Update(40);                                                       // 140 m
    CHECK(Same(FlareScale(glow), 1.0f - 40.0f * 0.0005f) && !b->m_burnedOut);
    // Burns out once at 0 (<= 0): the sparks stop, the mission is told the flare was fired (local game, server),
    // the model shrinks to nothing, updated, and is not switched on.
    FlareScale(glow) = 0.01f;
    g_particleLog.clear();
    g_frameLog.clear();
    b->m_interpolation = 0;
    b->Update(20);                                                       // 0.01 - 0.01 = 0
    CHECK(b->m_burnedOut && (g_particleLog == std::vector<std::string>{"toggle 41 0"}));
    CHECK(reinterpret_cast<C_game *>(g_gameBytes)->m_signalPistolFired);
    CHECK(Same(model->m_scale, S_vector(0, 0, 0)) && (g_frameLog == std::vector<std::string>{"update flaremodel"}));
    // On a client alone the mission is not told; without sparks neither (nor toggled).
    for (int variant = 0; variant < 2; ++variant) {
        Reset(variant == 0 ? GAME_ROLE_CLIENT : GAME_ROLE_LOCAL);
        b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
        b->m_visual = model;
        b->m_flare = true;
        b->m_step = S_vector(100, 0, 0);
        b->m_traceDistance = 150;
        b->m_flareSparks = variant == 0 ? 41 : -1;
        FlareScale(glow) = 0.005f;
        b->Update(20);
        CHECK(b->m_burnedOut && !reinterpret_cast<C_game *>(g_gameBytes)->m_signalPistolFired);
        CHECK(g_particleLog.size() == (variant == 0 ? 1u : 0u));
    }
    // Exactly 30 m traced: shown (>= 30); exactly 100 m: still lighting (<= 100). (0.1 * 50 is 5 in float.)
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_visual = model;
    b->m_flare = true;
    b->m_step = S_vector(50, 0, 0);
    b->m_traceDistance = 25;
    FlareScale(glow) = 0.5f;
    b->Update(10);
    CHECK(b->m_traceDistance == 30 && g_particleLog.size() == 1 && Same(FlareScale(glow), 0.5f + 20.0f * 0.001f));
    b->m_traceDistance = 95;
    b->Update(10);
    CHECK(b->m_traceDistance == 100 && FlareScale(glow) > 0.5f + 20.0f * 0.001f);
    // A glow above 1 already is not grown (nor cut back) while lighting.
    FlareScale(glow) = 1.25f;
    b->m_traceDistance = 50;
    b->m_interpolation = 0;
    b->Update(10);
    CHECK(Same(FlareScale(glow), 1.25f));
    // Dimming to exactly 0 burns it out (<= 0).
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_visual = model;
    b->m_flare = true;
    b->m_step = S_vector(100, 0, 0);
    b->m_traceDistance = 150;
    FlareScale(glow) = static_cast<float>(20) * 0.0005f;
    b->Update(20);
    CHECK(b->m_burnedOut);
    // A flare without its glow child: sparks all the same, no scaling.
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    I3D_frame *bare = MakeFrame(FRAME_MODEL, "bare");
    b->m_visual = bare;
    b->m_flare = true;
    b->m_step = S_vector(100, 0, 0);
    b->m_traceDistance = 40;
    b->Update(10);
    CHECK(g_particleLog.size() == 1 && !b->m_burnedOut);
    b->m_traceDistance = 150;
    b->Update(10);
    CHECK(!b->m_burnedOut);                                              // (only a glow burns out)
    // Not a flare: no glow looked for, no sparks.
    Reset();
    b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_visual = model;
    b->m_step = S_vector(100, 0, 0);
    b->m_traceDistance = 40;
    FlareScale(glow) = 0.5f;
    b->Update(10);
    CHECK(g_particleLog.empty() && Same(FlareScale(glow), 0.5f));
}

void TestRecycleRelease()
{
    // Recycle and Release free the visual and the sparks (Destroy), and forget both.
    Reset();
    C_BulletShot *b = Fire(S_vector(0, 0, 0), S_vector(1, 0, 0));
    b->m_visual = MakeFrame(FRAME_VISUAL, "v1");
    b->m_flareSparks = 7;
    b->Recycle();
    CHECK((g_frameLog == std::vector<std::string>{"release v1"}) && (g_particleLog == std::vector<std::string>{"destroy 7"}));
    CHECK(b->m_visual == nullptr && b->m_flareSparks == -1);
    Reset();
    b->m_visual = MakeFrame(FRAME_VISUAL, "v2");
    b->m_flareSparks = 8;
    b->Release();
    CHECK((g_frameLog == std::vector<std::string>{"release v2"}) && (g_particleLog == std::vector<std::string>{"destroy 8"}));
    CHECK(b->m_visual == nullptr && b->m_flareSparks == -1);
    Reset();
    b->Recycle();
    b->Release();
    CHECK(g_frameLog.empty() && g_particleLog.empty());
}

// ---- bullet decals ----

const S_vector kBase(0.5f, 0.25f, 1.0f);
// S_ShotDecal is shots.cpp's own: its layout (the projector, the parent's name).
struct S_DecalView {
    I3D_projector_visual *visual;
    C_str parentName;
};
const S_DecalView *View(const S_ShotDecal *d) { return reinterpret_cast<const S_DecalView *>(d); }                                 // the fake projector's base colour

void TestBulletDecals()
{
    C_ShotManager &m = g_shotManager;
    for (int i = 0; i < 9; ++i)
        m.m_decalTextures[i] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x1000 + i));
    m.m_maxDecals = 2;
    g_sectorFrame = MakeFrame(FRAME_SECTOR, "sector");
    SetMaterial(20, 0, 1);
    g_materialTable[20].decal = 3;
    I3D_frame *wall = MakeFrame(FRAME_VISUAL, "wall");
    g_visualTypes[wall] = I3DVISUAL_OBJECT;
    const S_vector pos(1, 2, 3), normal(0, 2, 0);                         // (not unit: normalised)
    // Disabled: nothing.
    Reset();
    m.AddBulletDecal(20, pos, normal, wall);
    CHECK(g_sceneLog.empty() && m.m_decals.empty());
    // On a static object visual: a projector (buffered 3, mode 1, orthogonal), facing into the surface turned by
    // (r - 0.5) * pi, sized r * 0.01 + 0.015 (depth 0.03), 1 cm off the surface, the material's texture,
    // coloured by the light there, linked to the frame keeping its world place, orthogonal again, updated.
    Reset();
    g_decalsOn = true;
    g_randFloat = 0.25f;
    g_light = 0.7f;
    m.AddBulletDecal(20, pos, normal, wall);
    CHECK(m.m_decals.size() == 1 && View(m.m_decals[0])->parentName == C_str("wall"));
    I3D_frame *v = reinterpret_cast<I3D_frame *>(View(m.m_decals[0])->visual);
    const std::string n = FrameName(v);
    char roll[32];
    std::snprintf(roll, sizeof roll, "%g", (0.25f - 0.5f) * 3.14159274f);
    const float size = 0.25f * 0.01f + 0.015f;
    const S_vector at = S_vector(0, 1, 0) * 0.01f + pos;
    const S_vector color(kBase.x * 0.7f, kBase.y * 0.7f, kBase.z * 0.7f);
    CHECK((g_frameLog == std::vector<std::string>{"buffered " + n + " 3", "mode " + n + " 1", "ortho " + n + " 1",
                                                  "dir " + n + " -0,-1,-0 " + roll, "pos " + n + " " + Vec(at),
                                                  "texture " + n + " 4099", "color " + n + " " + Vec(color),
                                                  "link " + n + " wall 1", "ortho " + n + " 1", "update " + n}));
    CHECK(Same(v->m_scale, S_vector(size, size, 0.03f)));
    CHECK((g_sceneLog == std::vector<std::string>{"create " + std::to_string(I3DVISUAL_PROJECTOR),
                                                  "light " + Vec(at) + " - 10"}));
    // The light below 0.4: 0.4 of the base colour; 1 and above: white.
    for (float light : {0.39f, 0.4f, 0.5f, 0.99f, 1.0f, 3.0f}) {
        Reset();
        g_decalsOn = true;
        g_light = light;
        m.AddBulletDecal(20, pos, normal, wall);
        const float f = light < 0.4f ? 0.4f : light;
        const S_vector want = light < 1.0f ? S_vector(kBase.x * f, kBase.y * f, kBase.z * f) : S_vector(1, 1, 1);
        bool found = false;
        for (const std::string &line : g_frameLog)
            found |= line.rfind("color ", 0) == 0 && line.substr(line.rfind(' ') + 1) == Vec(want);
        CHECK(found);
    }
    // Two shown at most: the oldest is moved (its projector reused: not created, not set up again).
    CHECK(m.m_decals.size() == 2);
    S_ShotDecal *oldest = m.m_decals[0];
    Reset();
    g_decalsOn = true;
    m.AddBulletDecal(20, pos, normal, wall);
    CHECK(m.m_decals.size() == 2 && m.m_decals[1] == oldest && g_sceneLog.size() == 1);   // (only the light)
    CHECK(g_frameLog.size() == 7 && g_frameLog[0].rfind("dir ", 0) == 0);
    // No frame: linked to the sector at the position, "No Parent".
    Reset();
    g_decalsOn = true;
    m.AddBulletDecal(20, pos, normal, nullptr);
    CHECK(View(m.m_decals.back())->parentName == C_str("No Parent"));
    CHECK(g_sceneLog.size() == 2 && g_sceneLog[1] == "sector " + Vec(pos));
    CHECK(std::find(g_frameLog.begin(), g_frameLog.end(), "link " + FrameName(reinterpret_cast<I3D_frame *>(View(m.m_decals.back())->visual)) +
                    " sector 1") != g_frameLog.end());
    // Refused: no decal for the material (-1, 9 and up), a material past the table, a texture not loaded, a
    // frame of another visual type, a skinned part's flags (0x3000), a parent that is a skinned mesh.
    auto refused = [&](int material, I3D_frame *frame) {
        Reset();
        g_decalsOn = true;
        m.AddBulletDecal(material, pos, normal, frame);
        return g_sceneLog.empty() && g_frameLog.empty();
    };
    g_materialTable[20].decal = -1;
    CHECK(refused(20, wall));
    g_materialTable[20].decal = 9;
    CHECK(refused(20, wall));
    g_materialTable[20].decal = 8;
    CHECK(!refused(20, wall));
    g_materialTable[20].decal = 0;
    CHECK(!refused(20, wall));
    g_materialCount = 20;
    CHECK(refused(20, wall));
    g_materialCount = 80;
    g_materialTable[20].decal = 8;
    m.m_decalTextures[8] = nullptr;
    CHECK(refused(20, wall));
    g_materialTable[20].decal = 3;
    for (uint32_t type : {I3DVISUAL_LMAP, I3DVISUAL_MIRROR}) {
        g_visualTypes[wall] = type;
        bool ok = !refused(20, wall);
        g_visualTypes[wall] = type;
        CHECK(ok);
    }
    g_visualTypes[wall] = I3DVISUAL_PROJECTOR;
    CHECK(refused(20, wall));
    g_visualTypes[wall] = I3DVISUAL_OBJECT;
    uint32_t &flags = *reinterpret_cast<uint32_t *>(reinterpret_cast<unsigned char *>(wall) + 0x1D0);
    flags = 0x1000;
    CHECK(refused(20, wall));
    flags = 0x2000;
    CHECK(refused(20, wall));
    flags = 0x4000;
    CHECK(!refused(20, wall));
    flags = 0;
    I3D_frame *body = MakeFrame(FRAME_VISUAL, "body");
    I3D_frame *root = MakeFrame(FRAME_MODEL, "root");
    wall->m_parent = body;
    body->m_parent = root;
    for (uint32_t type : {I3DVISUAL_SINGLEMESH, I3DVISUAL_SHD, I3DVISUAL_SINGLEMORPH}) {
        g_visualTypes[root] = type;                                       // (the grandparent counts too)
        bool no = refused(20, wall);
        g_visualTypes[root] = type;
        CHECK(no);
    }
    g_visualTypes[root] = I3DVISUAL_OBJECT;
    CHECK(!refused(20, wall));
    wall->m_parent = nullptr;
    // Through a bullet (client): the hit part as the frame; a destructible part the server destroyed, or a
    // lying item, get theirs on no frame (the sector).
    I3D_frame *door = MakeFrame(FRAME_VISUAL, "door");
    I3D_frame *panel = MakeFrame(FRAME_VISUAL, "panel");
    g_visualTypes[panel] = I3DVISUAL_OBJECT;
    static S_FrameData panelData{};
    alignas(16) static unsigned char keeperBytes[sizeof(C_VersionKeeper)];
    auto *keeper = reinterpret_cast<C_VersionKeeper *>(keeperBytes);
    keeper->m_materialId = 20;
    panelData.versionKeeper = keeper;
    panel->m_data = &panelData;
    for (int variant = 0; variant < 2; ++variant) {
        Reset();
        g_decalsOn = true;
        g_keeperSurvives = variant == 0;
        g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
        g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(nullptr, door), 0.2f)};
        Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
        CHECK(View(m.m_decals.back())->parentName == C_str(variant == 0 ? "panel" : "No Parent"));
    }
    panel->m_data = nullptr;
    C_Actor *item = MakeActor(ACTOR_ITEM, MakeFrame(FRAME_MODEL, "item"));
    C_Actor *barrel = MakeActor(ACTOR_BARREL, MakeFrame(FRAME_MODEL, "barrel"));
    for (C_Actor *actor : {item, barrel}) {
        Reset();
        g_decalsOn = true;
        g_actorMaterial = 20;
        g_intersects = {{panel, 15.0f, S_vector(0, 0, -1)}};
        g_queryHits = {Hit(S_vector(0, 0, 14), S_vector(0, 1, 0), DynamicVolume(actor), 0.2f)};
        Fire(S_vector(0, 0, 0), S_vector(0, 0, 1))->Tick(100);
        CHECK(View(m.m_decals.back())->parentName == C_str(actor == item ? "No Parent" : "panel"));
    }
}

// ---- projectiles (C_ProjectileShot) ----

// A rocket: 50 damage (the penetration budget), 100 m/s, an explosion of 200 within 8 m, sound 9, effect 33.
C_ProjectileWeaponComp g_rocket;
void SetUpRocket()
{
    g_rocket.m_damage = 50.0f;
    g_rocket.m_muzzleVelocity = 100.0f;
    g_rocket.m_explosionDamage = 200.0f;
    g_rocket.m_explosionRadius = 8.0f;
    g_rocket.m_explosionSound = 9;
    g_rocket.m_explosionEffect = 33;
    for (int i = 0; i < 80; ++i)
        g_materialTable[i].explosionEffect = 500 + i;
}
C_ProjectileShot *Launch(const S_vector &pos, const S_vector &dir, I3D_frame *model = nullptr, int trail = -1,
                         bool flySound = false, bool camera = false, bool fake = false)
{
    auto *p = new C_ProjectileShot();
    p->Init(g_shooter, pos, dir, &g_rocket, model, trail, flySound, camera, fake);
    return p;
}
int Count(const std::vector<std::string> &log, const std::string &line) { return static_cast<int>(std::count(log.begin(), log.end(), line)); }

void TestProjectileInit()
{
    Reset();
    C_ProjectileShot *p = Launch(S_vector(1, 2, 3), S_vector(0, 0, 1));
    CHECK(Same(p->m_velocity, S_vector(0, 0, 100)) && Same(p->m_damage, 50.0f) && p->m_trail == -1 && !p->m_exploded);
    CHECK(p->m_visual == nullptr && p->m_flySound == nullptr && g_sceneLog.empty() && p->m_removeDelay == 0);
    CHECK(Same(p->m_prevPos, S_vector(1, 2, 3)) && Same(p->m_step, S_vector(0, 0, 0)) && p->m_flightTime == 0);
    // With a model: a copy, linked into the dynamic root, aimed along the velocity, placed, on; the trail made
    // on it (backwards); a client with the fly sound makes one (its file not found here: released).
    Reset();
    I3D_frame *model = MakeFrame(FRAME_MODEL, "rocketmodel");
    p = Launch(S_vector(1, 2, 3), S_vector(0, 0, 1), model, 77, true);
    I3D_frame *v = p->m_visual;
    const std::string n = FrameName(v);
    CHECK(v != nullptr && (g_sceneLog == std::vector<std::string>{"create " + std::to_string(I3DVISUAL_OBJECT),
                                                                   "createframe " + std::to_string(FRAME_SOUND)}));
    CHECK((g_frameLog == std::vector<std::string>{"duplicate " + n + " rocketmodel", "link " + n + " null 0",
                                                  "link " + n + " null 0", "dir " + n + " 0,0,100 0", "pos " + n + " 1,2,3",
                                                  "on " + n, "update " + n, "release sound"}));
    CHECK(p->m_trail == 1);
    CHECK(p->m_flySound == nullptr);
    // Trail effect 0 is one (>= 0), made on the model, both flags set.
    Reset();
    p = Launch(S_vector(1, 2, 3), S_vector(0, 0, 1), model, 0);
    CHECK(p->m_trail == 1 && (g_particleLog == std::vector<std::string>{"createat 0 11 frame"}));
    // A server alone, or no fly sound asked: none made.
    Reset(GAME_ROLE_SERVER);
    Launch(S_vector(1, 2, 3), S_vector(0, 0, 1), model, -1, true);
    CHECK(Count(g_sceneLog, "createframe " + std::to_string(FRAME_SOUND)) == 0);
    Reset();
    p = Launch(S_vector(1, 2, 3), S_vector(0, 0, 1), model, -1, false);
    CHECK(Count(g_sceneLog, "createframe " + std::to_string(FRAME_SOUND)) == 0 && p->m_trail == -1);
}

void TestProjectileFlight()
{
    // Free flight: the bullet's step and gravity; 600 m ends it without an explosion.
    Reset();
    C_ProjectileShot *p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    CHECK(!p->Tick(100));
    CHECK(Same(p->m_pos, S_vector(10, 10, 0)) && Same(p->m_velocity.y, -(100.0f * 0.001f * 9.8f)) && Same(p->m_distance, 10.0f));
    CHECK((g_bodyLog == std::vector<std::string>{"remove", "restore"}) && g_queries.size() == 1 &&
          g_queries[0].excludeTypes == 0x48001 && g_queries[0].excludeFlags == 0x200000);
    p->m_distance = 591.0f;
    CHECK(!p->Tick(100));                                                 // 601 m: gone, no explosion
    CHECK(p->m_exploded && p->m_removeDelay == 0 && g_explosions.empty() && g_sounds.empty());
    CHECK(p->Tick(100));
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->m_distance = 590.0f;
    p->Tick(100);                                                         // 600 exactly: on
    CHECK(!p->m_exploded);
}

void TestProjectileWalls()
{
    // (The manager's shared sound request left with another source and frame.)
    g_shotManager.m_sound.source = MakeActor(ACTOR_BARREL);
    g_shotManager.m_sound.attachTo = MakeFrame(FRAME_VISUAL, "stale");
    // An impenetrable wall: it explodes at the hit, 0.2 m off the surface, half its explosion damage (a static
    // surface), the AI told (local game); the client plays it: the explosion sound at the hit, silent to the AI,
    // the weapon's effect and the material's.
    Reset();
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(2), 0.4f)};
    C_ProjectileShot *p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    CHECK(!p->Tick(100) && p->m_exploded && Same(p->m_pos, S_vector(4, 10, 0)));
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].pos, S_vector(4.0f - 0.2f, 10, 0)) &&
          Same(g_explosions[0].range, 8.0f) && Same(g_explosions[0].damage, 100.0f) && !g_explosions[0].unknown18 &&
          g_explosions[0].source == g_shooter);
    CHECK(g_sounds.size() == 2 && g_sounds[0].chapter == SOUND_CHAPTER_EXPLOSION && g_sounds[0].group == 9 &&
          g_sounds[0].flags == (SOUND_AI_ONLY | SOUND_AI_OCCLUSION) && g_sounds[0].source == g_shooter &&
          Same(g_sounds[0].pos, S_vector(4.0f - 0.2f, 10, 0)));
    CHECK(g_sounds[1].chapter == SOUND_CHAPTER_EXPLOSION && g_sounds[1].flags == SOUND_SILENT_TO_AI &&
          g_sounds[1].source == nullptr && Same(g_sounds[1].pos, S_vector(4, 10, 0)));
    CHECK(g_sounds[0].attachTo == nullptr && g_sounds[1].attachTo == nullptr);
    // No range or no damage: no explosion executed (the AI still hears it).
    for (int variant = 0; variant < 2; ++variant) {
        Reset();
        g_rocket.m_explosionRadius = variant == 0 ? 0.0f : 8.0f;
        // (and a radius of 0.5 does explode: > 0)
        g_rocket.m_explosionDamage = variant == 1 ? 0.0f : 200.0f;
        g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(2), 0.4f)};
        Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
        CHECK(g_explosions.empty() && g_sounds.size() == 2);
    }
    Reset();
    g_rocket.m_explosionRadius = 0.5f;
    g_rocket.m_explosionDamage = 200.0f;
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(2), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].range, 0.5f));
    g_rocket.m_explosionRadius = 8.0f;
    g_rocket.m_explosionDamage = 200.0f;
    CHECK((g_effects == std::vector<int>{33, 502}));
    CHECK((g_particleLog == std::vector<std::string>{"createat 33 10", "createat 502 10"}));
    CHECK(Same(p->m_velocity.y, -(100.0f * 0.001f * 9.8f)) && Same(p->m_distance, 4.0f));
    // Gone the next tick (no camera).
    CHECK(p->Tick(100));
    // A server alone: exploded without playing; a client alone: played, no damage.
    Reset(GAME_ROLE_SERVER);
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(2), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_explosions.size() == 1 && g_sounds.empty() && g_effects.empty());
    Reset(GAME_ROLE_CLIENT);
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(2), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_explosions.empty() && g_sounds.size() == 1 && g_effects.size() == 2);
    // STOP: gone without an explosion (a server alone returns at once).
    Reset();
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(MATERIAL_STOP), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    CHECK(!p->Tick(100) && p->m_exploded && g_explosions.empty() && g_sounds.empty() && Same(p->m_pos, S_vector(4, 10, 0)));
    Reset(GAME_ROLE_SERVER);
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(MATERIAL_STOP), 0.4f)};
    CHECK(Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100));
    // PASS: through.
    Reset();
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), StaticVolume(MATERIAL_PASS), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded && Same(p->m_pos, S_vector(10, 10, 0)));
    // Penetrable: cost 30 of 50 passes (20 left), a cost equal to what is left passes, above it explodes there.
    // Passing a hit-effect material (31..34, 41, 43, 62, 70) plays its sound and effect (client); others not.
    Reset();
    SetMaterial(32, 0, 30);
    SetMaterial(33, 0, 20);
    g_queryHits = {Hit(S_vector(2, 10, 0), S_vector(-1, 0, 0), StaticVolume(32), 0.2f),
                   Hit(S_vector(5, 10, 0), S_vector(-1, 0, 0), StaticVolume(33), 0.5f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded && Same(p->m_damage, 0.0f) && g_sounds.size() == 2 && (g_effects == std::vector<int>{332, 333}));
    Reset();
    SetMaterial(5, 0, 30);
    SetMaterial(6, 0, 21);
    g_queryHits = {Hit(S_vector(2, 10, 0), S_vector(-1, 0, 0), StaticVolume(5), 0.2f),
                   Hit(S_vector(5, 10, 0), S_vector(-1, 0, 0), StaticVolume(6), 0.5f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(p->m_exploded && Same(p->m_damage, 20.0f) && Same(p->m_pos, S_vector(5, 10, 0)));
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].damage, 100.0f));   // (a static surface: half)
    CHECK(g_sounds.size() == 2 && (g_effects == std::vector<int>{33, 506}));    // (no hit sounds for 5)
    for (int m : {31, 34, 41, 43, 62, 70, 30, 35, 42, 44, 61, 63, 69, 71}) {
        Reset();
        SetMaterial(m, 0, 1);
        g_queryHits = {Hit(S_vector(2, 10, 0), S_vector(-1, 0, 0), StaticVolume(m), 0.2f)};
        Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
        const bool effect = (m > 30 && m < 35) || m == 41 || m == 43 || m == 62 || m == 70;
        CHECK(g_sounds.size() == (effect ? 1u : 0u));
    }
    // A level mesh tested (0xC0): told to the scripts when hit (server).
    Reset();
    I3D_frame *wall = MakeFrame(FRAME_VISUAL, "pwall");
    I3D_frame *piece = MakeFrame(FRAME_VISUAL, "ppiece");
    S_CollisionVolume *v = StaticVolume(2, COLLISION_FLAG_PRIMITIVE);
    v->frame = wall;
    g_intersects = {{piece, 3.5f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(0, 1, 0), v, 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == piece && Same(p->m_pos, S_vector(3.5f, 10, 0)));
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].pos, S_vector(3.5f + 0.2f * -1.0f, 10, 0)));
}

void TestProjectileActors()
{
    S_TestHuman target = MakeHuman("ptarget");
    g_headItem = nullptr;
    // A human in a zone: explodes at full damage, at the hit (no Hit of its own: the explosion does it).
    Reset();
    g_actorMaterial = 1;
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(target.human), 0.4f)};
    C_ProjectileShot *p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(p->m_exploded && g_hits.empty() && g_explosions.size() == 1 && Same(g_explosions[0].damage, 200.0f));
    CHECK((g_effects == std::vector<int>{33, 501}));
    // ...after passing a wall (the share back to full).
    Reset();
    SetMaterial(5, 0, 10);
    g_actorMaterial = 1;
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(2, 10, 0), S_vector(-1, 0, 0), StaticVolume(5), 0.2f),
                   Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(target.human), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].damage, 200.0f));
    // No zone: told it is shot at (server, alive; no hostility test), flies on.
    Reset();
    g_friends = true;
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(target.human), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded && g_shotAt.size() == 1 && Same(g_shotAt[0].velocity, S_vector(100, 0, 0)));
    for (int variant = 0; variant < 2; ++variant) {
        Reset(variant == 0 ? GAME_ROLE_CLIENT : GAME_ROLE_LOCAL);
        target.human->m_stats[STAT_HEALTH] = variant == 1 ? 0 : 100;
        g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(target.human), 0.4f)};
        Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
        CHECK(g_shotAt.empty());
    }
    target.human->m_stats[STAT_HEALTH] = 1;
    Reset();
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(target.human), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_shotAt.size() == 1);
    target.human->m_stats[STAT_HEALTH] = 100;
    // A fake projectile skips actors.
    Reset();
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(target.human), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0), nullptr, -1, false, false, true);
    p->Tick(100);
    CHECK(!p->m_exploded && g_intersectFrames.empty());
    // A car: hit directly with the explosion damage 0.1 m before the hit along the direction (server); its hit
    // material; the explosion at a tenth.
    I3D_frame *carFrame = MakeFrame(FRAME_MODEL, "pcar");
    C_Actor *car = MakeActor(ACTOR_CAR, carFrame);
    Reset();
    g_carMaterial = 2;
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(car), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(g_hits.size() == 1 && g_hits[0].type == HIT_PROJECTILE && Same(g_hits[0].amount, 200.0f) &&
          Same(g_hits[0].pos, S_vector(4.0f - 0.1f, 10, 0)) && Same(g_hits[0].dir, S_vector(1, 0, 0)) && g_hits[0].part == 0);
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == carFrame && g_explosions.size() == 1 &&
          Same(g_explosions[0].damage, 20.0f) && p->m_exploded);
    // Single player, difficulty 1, the player's side, a projectile with a model: four times.
    I3D_frame *model = MakeFrame(FRAME_MODEL, "pmodel");
    for (int variant = 0; variant < 5; ++variant) {
        Reset(variant == 1 ? GAME_ROLE_SERVER : GAME_ROLE_LOCAL);
        g_difficulty = variant == 2 ? 2 : 1;
        reinterpret_cast<C_Human *>(g_shooter)->m_role = variant == 3 ? HUMAN_ROLE_ENEMY : HUMAN_ROLE_PLAYER;
        g_carMaterial = 2;
        g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(car), 0.4f)};
        Launch(S_vector(0, 10, 0), S_vector(1, 0, 0), variant == 4 ? nullptr : model)->Tick(100);
        CHECK(g_hits.size() == 1 && Same(g_hits[0].amount, variant == 0 ? 800.0f : 200.0f));
    }
    reinterpret_cast<C_Human *>(g_shooter)->m_role = HUMAN_ROLE_PLAYER;
    // No hit material: its visual tested along the step; no part, or a part without material: flies on (hit
    // all the same, server).
    Reset();
    g_carMaterial = MATERIAL_NONE;
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(car), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded && g_hits.size() == 1 && g_intersectFrames.size() == 1 && g_intersectFrames[0] == carFrame &&
          g_intersectFlags[0] == I3DCOL_VISUAL_GEOMETRY && g_intersectExtra == 0);
    I3D_frame *part = MakeFrame(FRAME_VISUAL, "ppart");
    Reset();
    g_carMaterial = MATERIAL_NONE;
    g_actorMaterial = MATERIAL_NONE;
    g_intersects = {{part, 4.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(car), 0.4f)};
    CHECK(!Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100) && g_explosions.empty());
    Reset();
    g_carMaterial = MATERIAL_NONE;
    g_actorMaterial = 2;
    g_intersects = {{part, 4.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(car), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].damage, 20.0f));
    // The shooter's own vehicle: passed.
    Reset();
    g_carMaterial = 2;
    reinterpret_cast<C_Human *>(g_shooter)->m_vehicle = car;
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(car), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded && g_hits.empty());
    reinterpret_cast<C_Human *>(g_shooter)->m_vehicle = nullptr;
    // Another actor: its visual; explodes at full damage.
    Reset();
    C_Actor *barrel = MakeActor(ACTOR_BARREL, MakeFrame(FRAME_MODEL, "pbarrel"));
    g_actorMaterial = 4;
    g_intersects = {{part, 3.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(barrel), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(p->m_exploded && Same(p->m_pos, S_vector(3, 10, 0)) && g_explosions.size() == 1 && Same(g_explosions[0].damage, 200.0f));
    CHECK(g_framesHit.size() == 1 && g_hits.empty());
    // A destructible part: damaged by the projectile's own damage (truncated), its material decides.
    Reset();
    I3D_frame *door = MakeFrame(FRAME_VISUAL, "pdoor");
    static S_FrameData data{};
    alignas(16) static unsigned char keeperBytes[sizeof(C_VersionKeeper)];
    auto *keeper = reinterpret_cast<C_VersionKeeper *>(keeperBytes);
    keeper->m_materialId = 5;
    SetMaterial(5, 0, 10);
    data.versionKeeper = keeper;
    part->m_data = &data;
    g_intersects = {{part, 3.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(nullptr, door), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->m_damage = 49.9f;
    p->Tick(100);
    CHECK((g_keeperDamage == std::vector<int>{49}) && !p->m_exploded && Same(p->m_damage, 39.9f));
    part->m_data = nullptr;
}

void TestProjectileCamera()
{
    // Update: the model shown along the step; the camera rides along with the local player's projectile once it
    // has flown more than 300 ms (asked for at launch); after the explosion it lingers 1.5 s.
    Reset();
    g_localPlayer = reinterpret_cast<C_Human *>(g_shooter);
    I3D_frame *model = MakeFrame(FRAME_MODEL, "cmodel");
    C_ProjectileShot *p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0), model, -1, false, true);
    const std::string n = FrameName(p->m_visual);
    p->Tick(100);
    g_frameLog.clear();
    p->Update(50);
    CHECK((g_frameLog == std::vector<std::string>{"pos " + n + " 5,10,0", "worlddir " + n + " 1,0,0 0", "on " + n,
                                                  "update " + n}));
    CHECK(p->m_flightTime == 50 && g_cameraLog.empty());
    for (int i = 0; i < 5; ++i) {
        p->m_interpolation = 0;
        p->Update(50);
    }
    CHECK(p->m_flightTime == 300 && g_cameraLog.empty());                // (> 300)
    p->Update(1);
    CHECK((g_cameraLog == std::vector<std::string>{"9 on frame"}) && p->m_cameraFollows);
    p->Update(1);
    CHECK(g_cameraLog.size() == 1);
    g_queryHits = {Hit(S_vector(14, 10, 0), S_vector(-1, 0, 0), StaticVolume(2), 0.4f)};
    p->Tick(100);
    CHECK(p->m_exploded && p->m_removeDelay == 1500);
    // Exploded: hidden, the delay counted down, gone below 1.
    g_frameLog.clear();
    model->m_frameFlags |= 1;
    p->m_visual->m_frameFlags |= 1;
    CHECK(!p->Tick(1000) && p->m_removeDelay == 500 && Count(g_frameLog, "off " + n) == 1);
    CHECK(!p->Tick(499) && p->m_removeDelay == 1);
    CHECK(p->Tick(1) && p->m_removeDelay == 0);
    // Release turns the camera back.
    g_cameraLog.clear();
    p->Release();
    CHECK((g_cameraLog == std::vector<std::string>{"9 off null"}) && p->m_visual == nullptr);
    // Not asked, not the local player, or no model: no camera.
    for (int variant = 0; variant < 2; ++variant) {
        Reset();
        g_localPlayer = variant == 0 ? reinterpret_cast<C_Human *>(g_shooter) : nullptr;
        p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0), model, -1, false, variant == 1);
        p->Tick(100);
        for (int i = 0; i < 8; ++i) {
            p->m_interpolation = 0;
            p->Update(50);
        }
        CHECK(g_cameraLog.empty() && !p->m_cameraFollows);
    }
    // Update without a step (or a visual): nothing, the flight time not counted.
    Reset();
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0), model);
    p->Update(50);
    CHECK(p->m_flightTime == 0 && g_frameLog.size() == 7);               // (Init's own calls only)
    // Release frees the trail and the model.
    Reset();
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0), model, 12);
    const std::string n2 = FrameName(p->m_visual);
    g_frameLog.clear();
    g_particleLog.clear();
    p->Release();
    CHECK(Count(g_frameLog, "release " + n2) == 1 && (g_particleLog == std::vector<std::string>{"destroy 1"}) &&
          p->m_trail == -1 && g_cameraLog.empty());
}

// ---- the manager ----

// A shot that finishes when told, logging its calls.
std::vector<std::string> g_shotLog;
struct C_TestShot : C_Shot {
    explicit C_TestShot(E_ShotType type, const char *name, int ticks) : C_Shot(type), m_name(name), m_ticks(ticks) {}
    ~C_TestShot() override { g_shotLog.push_back("delete " + m_name); }
    bool Tick(int dt) override
    {
        g_shotLog.push_back("tick " + m_name + " " + std::to_string(dt));
        return --m_ticks <= 0;
    }
    void Update(int dt) override { g_shotLog.push_back("update " + m_name + " " + std::to_string(dt)); }
    void SaveGame(C_chunk &) override { g_shotLog.push_back("save " + m_name); }
    std::string m_name;
    int m_ticks;
};

void TestManagerTick()
{
    C_ShotManager &m = g_shotManager;
    // Tick: every shot ticked once, in order; the finished ones removed (the next one still ticked): a
    // projectile or a thrown item deleted.
    Reset();
    g_shotLog.clear();
    m.m_shots = {new C_TestShot(SHOT_PROJECTILE, "a", 1), new C_TestShot(SHOT_THROWN, "b", 2),
                 new C_TestShot(SHOT_PROJECTILE, "c", 1), new C_TestShot(SHOT_PROJECTILE, "d", 4)};
    m.Tick(100);
    CHECK((g_shotLog == std::vector<std::string>{"tick a 100", "delete a", "tick b 100", "tick c 100", "delete c",
                                                 "tick d 100"}));
    CHECK(m.m_shots.size() == 2);
    g_shotLog.clear();
    m.Tick(50);
    CHECK((g_shotLog == std::vector<std::string>{"tick b 50", "delete b", "tick d 50"}) && m.m_shots.size() == 1);
    // Update (a server): every shot updated with the frame's time, no ticks.
    g_shotLog.clear();
    m.Update(16);
    CHECK((g_shotLog == std::vector<std::string>{"update d 16"}));
    // A pure client: its own 100 ms ticks, at most one a frame, once past 100 (the rest kept).
    Reset(GAME_ROLE_CLIENT);
    g_shotLog.clear();
    m.m_clientTickTime = 0;
    m.Update(100);
    CHECK((g_shotLog == std::vector<std::string>{"update d 100"}) && m.m_clientTickTime == 100);
    g_shotLog.clear();
    m.Update(250);
    CHECK((g_shotLog == std::vector<std::string>{"tick d 100", "update d 250"}) && m.m_clientTickTime == 250);
    g_shotLog.clear();
    m.Update(1);
    CHECK((g_shotLog == std::vector<std::string>{"tick d 100", "delete d"}) && m.m_clientTickTime == 151 && m.m_shots.empty());
    // 101 ms: a tick (> 100).
    m.m_shots = {new C_TestShot(SHOT_PROJECTILE, "e", 9)};
    m.m_clientTickTime = 0;
    g_shotLog.clear();
    m.Update(101);
    CHECK((g_shotLog == std::vector<std::string>{"tick e 100", "update e 101"}) && m.m_clientTickTime == 1);
    m.RemoveShots();
    // A finished bullet goes back to the pool (recycled), not deleted.
    Reset();
    C_BulletShot *b = m.m_bulletPool.Get();
    b->Init(g_shooter, S_vector(0, 0, 0), S_vector(1, 0, 0), &g_rifle, nullptr, false, false);
    b->m_flareSparks = 5;
    b->m_finished = true;
    m.m_shots = {b};
    m.Tick(100);
    CHECK(m.m_shots.empty() && m.m_bulletPool.Get() == b && b->m_flareSparks == -1);
}

void TestManagerSaveLoad()
{
    C_ShotManager &m = g_shotManager;
    // Save: an empty placeholder chunk per decal, blood drop and pool, then per shot (last first, its own save
    // inside).
    Reset();
    g_shotLog.clear();
    for (S_ShotDecal *d : m.m_decals)
        (void)d;
    const size_t decals = m.m_decals.size();
    m.m_shots = {new C_TestShot(SHOT_PROJECTILE, "s1", 1), new C_TestShot(SHOT_PROJECTILE, "s2", 1)};
    CHECK(m.SaveGame(*reinterpret_cast<C_chunk *>(g_gameBytes)));
    std::vector<std::string> want;
    for (size_t i = 0; i < decals; ++i) {
        want.push_back("begin " + std::to_string(0x1A0A));
        want.push_back("ascend");
    }
    want.insert(want.end(), {"begin " + std::to_string(0x1A0D), "ascend", "begin " + std::to_string(0x1A0D), "ascend"});
    CHECK(g_chunkLog == want);
    CHECK((g_shotLog == std::vector<std::string>{"save s2", "save s1"}));
    for (C_Shot *s : m.m_shots)
        delete s;
    m.m_shots.clear();
    // Load: the four placeholders skipped (ascended), others only entered.
    Reset();
    g_chunkIds = {0x1A0A, 0x1A0B, 0x1234, 0x1A0C, 0x1A0D};
    CHECK(m.LoadGame(*reinterpret_cast<C_chunk *>(g_gameBytes)));
    CHECK((g_chunkLog == std::vector<std::string>{"descend 6666", "ascend", "descend 6667", "ascend", "descend 4660",
                                                  "descend 6668", "ascend", "descend 6669", "ascend"}));
}

void TestManagerMessage()
{
    C_ShotManager &m = g_shotManager;
    // The explosion message (type 0 in 1 bit): its position, its material (8 bits); played with one of the
    // three effects, upwards, sound 30.
    for (int round = 0; round < 60; ++round) {
        Reset(GAME_ROLE_CLIENT);
        g_bits = {0, 7};
        g_readPos = S_vector(1, 2, 3);
        CHECK(m.OnMessage(reinterpret_cast<C_BitStream *>(g_gameBytes)));
        CHECK((g_chunkLog == std::vector<std::string>{"bits 1", "bits 8"}));
        CHECK(g_sounds.size() == 1 && g_sounds[0].group == 30 && g_sounds[0].chapter == SOUND_CHAPTER_EXPLOSION &&
              Same(g_sounds[0].pos, S_vector(1, 2, 3)));
        CHECK(g_effects.size() == 2 && (g_effects[0] == 43 || g_effects[0] == 48 || g_effects[0] == 49) && g_effects[1] == 507);
    }
    // Another type: nothing more read.
    Reset(GAME_ROLE_CLIENT);
    g_bits = {1, 7};
    CHECK(m.OnMessage(reinterpret_cast<C_BitStream *>(g_gameBytes)) && g_chunkLog.size() == 1 && g_sounds.empty());
}

void TestClientBullets()
{
    // InitClient: the bullet keeps the unit direction; its end is where a 500 m ray from the muzzle hits (the
    // ray's filters 0x2DC100 / 0x900000), with that surface's material, or the 500 m point, no material.
    Reset(GAME_ROLE_CLIENT);
    g_ray.hit = true;
    g_ray.pos = S_vector(0, 0, 40);
    g_ray.normal = S_vector(0, 0, -1);
    g_ray.flags = 4u << 24;
    auto *b = new C_BulletShot();
    b->InitClient(g_shooter, S_vector(0, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, true);
    CHECK(Same(b->m_velocity, S_vector(0, 0, 1)) && Same(b->m_target, S_vector(0, 0, 40)) && b->m_targetMaterial == 4);
    CHECK(!b->m_flare && b->m_flareSparks == -1 && Same(b->m_damage, 100.0f));
    CHECK(g_rayLog.size() == 1 && g_rayLog[0] == "ray 0.000000,0.000000,0.000000 0.000000,0.000000,500.000000 2dc100 900000");
    // TickClient: at the muzzle velocity towards the end; there: the end's sound and hit (fake: silent to the AI).
    CHECK(!b->TickClient(25) && Same(b->m_pos, S_vector(0, 0, 20)));
    CHECK(!b->TickClient(24) && Same(b->m_pos.z, 20.0f + 800.0f * 24.0f * 0.001f));
    CHECK(g_sounds.empty());
    CHECK(b->TickClient(25) && Same(b->m_pos, S_vector(0, 0, 40)) && b->m_finished);
    CHECK(g_sounds.size() == 1 && g_sounds[0].group == 104 && g_sounds[0].flags == SOUND_SILENT_TO_AI &&
          Same(g_sounds[0].pos, S_vector(0, 0, 40)) && g_effects.size() == 1);
    // A reused bullet: its flare flag and its old target material are not kept.
    Reset(GAME_ROLE_CLIENT);
    b = new C_BulletShot();
    b->m_flare = true;
    b->m_targetMaterial = 4;
    b->InitClient(g_shooter, S_vector(0, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, false);
    CHECK(!b->m_flare && b->m_targetMaterial == MATERIAL_NONE);
    // Material 0 is one (>= 0): played at the end.
    Reset(GAME_ROLE_CLIENT);
    g_ray.hit = true;
    g_ray.pos = S_vector(0, 0, 8);
    g_ray.normal = S_vector(0, 0, -1);
    g_ray.flags = 0;
    SetMaterial(0, 0, MATERIAL_IMPENETRABLE);
    b = new C_BulletShot();
    b->InitClient(g_shooter, S_vector(0, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, false);
    CHECK(b->m_targetMaterial == 0 && b->TickClient(10) && g_sounds.size() == 1 && g_sounds[0].group == 100);
    // The whole path is checked for a near miss at once.
    Reset(GAME_ROLE_CLIENT);
    {
        C_Actor *listener = MakeActor(ACTOR_HUMAN);
        reinterpret_cast<C_Human *>(listener)->m_bodyFrame = MakeFrameAt(S_vector(1, 0, 100));
        g_activePlayer = reinterpret_cast<C_Human *>(listener);
        b = new C_BulletShot();
        b->InitClient(g_shooter, S_vector(0, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, false);
        CHECK(g_sounds.size() == 1 && g_sounds[0].group == 11 && b->m_nearMissPlayed);
    }
    // Exactly the speed's distance away: arrives (<=).
    Reset(GAME_ROLE_CLIENT);
    b = new C_BulletShot();
    g_ray.hit = true;
    g_ray.pos = S_vector(0, 0, 8);
    g_ray.normal = S_vector(0, 0, -1);
    g_ray.flags = 4u << 24;
    b->InitClient(g_shooter, S_vector(0, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, false);
    CHECK(b->TickClient(10));
    // Nothing hit: the 500 m point, no sound at the end.
    Reset(GAME_ROLE_CLIENT);
    b = new C_BulletShot();
    b->InitClient(g_shooter, S_vector(1, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, false);
    CHECK(Same(b->m_target, S_vector(1, 0, 500)) && b->m_targetMaterial == MATERIAL_NONE);
    int ticks = 0;
    while (!b->TickClient(100) && ticks < 20)
        ++ticks;
    CHECK(ticks == 6 && g_sounds.empty() && g_effects.empty());           // (80 m a tick: 7 ticks)
    // A trace: shown at each step, stretched to it.
    Reset(GAME_ROLE_CLIENT);
    b = new C_BulletShot();
    b->InitClient(g_shooter, S_vector(0, 0, 0), S_vector(0, 0, 1), &g_rifle, nullptr, false, false);
    b->m_visual = MakeFrame(FRAME_VISUAL, "ctrace");
    b->TickClient(10);
    CHECK((g_frameLog == std::vector<std::string>{"pos ctrace 0,0,8", "worlddir ctrace 0,0,8 0", "on ctrace", "update ctrace"}));
    CHECK(Same(b->m_visual->m_scale, S_vector(1, 1, 8)));
}

void TestFireBullet()
{
    C_ShotManager &m = g_shotManager;
    // The rifle as an item: its component the firearm.
    static C_ItemDef *def = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(def), 0, sizeof(C_ItemDef));
    def->m_component = &g_rifle;
    static C_WeaponItem *gun = static_cast<C_WeaponItem *>(::operator new(sizeof(C_WeaponItem)));
    std::memset(static_cast<void *>(gun), 0, sizeof(C_WeaponItem));
    gun->m_def = def;
    C_Human *shooter = reinterpret_cast<C_Human *>(g_shooter);
    shooter->m_frame = MakeFrame(FRAME_MODEL, "shooterframe");
    // Local game: a bullet from the pool, initialised, in the shots; not stepped yet.
    Reset();
    m.m_shots.clear();
    m.m_clientBullets.clear();
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_shots.size() == 1 && m.m_shots[0]->GetType() == SHOT_BULLET);
    auto *b = static_cast<C_BulletShot *>(m.m_shots[0]);
    CHECK(Same(b->m_pos, S_vector(0, 0, 0)) && !b->m_fake && !b->m_flare && b->m_visual == nullptr && g_queries.empty());
    // From a vehicle: its first step at once.
    Reset();
    m.m_shots.clear();
    shooter->m_vehicle = shooter;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(g_queries.size() == 1 && Same(static_cast<C_BulletShot *>(m.m_shots[0])->m_pos, S_vector(80, 0, 0)));
    shooter->m_vehicle = nullptr;
    // Flags' low byte: a fake bullet; the trace model when asked for a tracer; the signal pistol: a flare
    // (its model needs the driver: the flag only checked).
    Reset();
    m.m_shots.clear();
    I3D_frame *traceModel = MakeFrame(FRAME_VISUAL, "tracemodel");
    m.m_traceModel = nullptr;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0x100, -1);
    CHECK(!static_cast<C_BulletShot *>(m.m_shots[0])->m_fake);
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0x1, -1);
    CHECK(static_cast<C_BulletShot *>(m.m_shots[1])->m_fake);
    (void)traceModel;
    // A network server: in the shots too; own shots of a network client: stepped at once.
    Reset(GAME_ROLE_SERVER);
    m.m_shots.clear();
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_shots.size() == 1 && g_queries.empty());
    Reset(GAME_ROLE_CLIENT);
    m.m_shots.clear();
    g_localPlayer = shooter;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_shots.size() == 1 && g_queries.size() == 1);
    // A remote player's shot on a client: a client bullet, when the shooter was drawn in the last frame.
    Reset(GAME_ROLE_CLIENT);
    m.m_shots.clear();
    m.m_clientBullets.clear();
    FrameStamp(shooter->m_frame) = 7;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_shots.empty() && m.m_clientBullets.size() == 1 && g_rayLog.size() == 1);
    // Update moves the client bullets (TickClient) and recycles the arrived ones (two in flight: one 80 m away,
    // one 500 m).
    g_ray = S_RayAnswer();
    g_ray.hit = true;
    g_ray.pos = S_vector(80, 0, 0);
    g_ray.normal = S_vector(-1, 0, 0);
    g_ray.flags = 4u << 24;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_clientBullets.size() == 2);
    C_BulletShot *far500 = m.m_clientBullets[0];
    C_BulletShot *near80 = m.m_clientBullets[1];
    m.m_clientBullets = {near80, far500};                                 // (the arriving one first: the next still moves)
    m.Update(50);
    CHECK(m.m_clientBullets.size() == 2 && Same(near80->m_pos, S_vector(40, 0, 0)));
    m.Update(50);
    CHECK(m.m_clientBullets.size() == 1 && m.m_clientBullets[0] == far500 && Same(far500->m_pos, S_vector(80, 0, 0)));
    C_BulletShot *again = m.m_bulletPool.Get();                           // the arrived one back in the pool
    CHECK(again == near80);
    m.m_bulletPool.Put(again);
    for (int i = 0; i < 8; ++i)
        m.Update(100);
    CHECK(m.m_clientBullets.empty());
    // RemoveShots takes the client bullets back too.
    FrameStamp(shooter->m_frame) = 7;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_clientBullets.size() == 1);
    m.RemoveShots();
    CHECK(m.m_clientBullets.empty());
    // FireProjectile: a projectile from the weapon's component, fake by the flags' low byte.
    Reset();
    m.m_shots.clear();
    static C_ItemDef *rdef = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(rdef), 0, sizeof(C_ItemDef));
    rdef->m_component = &g_rocket;
    static C_Item *launcher = static_cast<C_Item *>(::operator new(sizeof(C_Item)));
    std::memset(static_cast<void *>(launcher), 0, sizeof(C_Item));
    launcher->m_def = rdef;
    m.FireProjectile(shooter, launcher, S_vector(0, 0, 0), S_vector(0, 1, 0), nullptr, -1, false, false, 0x200);
    m.FireProjectile(shooter, launcher, S_vector(0, 0, 0), S_vector(0, 1, 0), nullptr, -1, false, false, 0x2);
    CHECK(m.m_shots.size() == 2 && m.m_shots[0]->GetType() == SHOT_PROJECTILE && !m.m_shots[0]->m_fake &&
          m.m_shots[1]->m_fake && Same(m.m_shots[0]->m_velocity, S_vector(0, 100, 0)));
    // RemoveShots: projectiles deleted, bullets and client bullets back to the pool.
    m.RemoveShots();
    CHECK(m.m_shots.empty() && m.m_clientBullets.empty());
    g_localPlayer = nullptr;
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (void *&slot : g_sceneVt) slot = nullptr;
    g_sceneVt[0x104 / 4] = reinterpret_cast<void *>(F_IntersectFrame);
    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_FrRelease);
    g_frameVt[0x04 / 4] = reinterpret_cast<void *>(F_FrGetVisualType);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_FrSetPos);
    g_frameVt[0x0C / 4] = reinterpret_cast<void *>(F_FrSetDir);
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_FrSetWorldDir);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_FrUpdate);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_FrSetOn);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_FrLinkTo);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FrFindChild);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_FrDuplicate);
    g_frameVt[0x9C / 4] = reinterpret_cast<void *>(F_PrSetTexture);
    g_frameVt[0xA4 / 4] = reinterpret_cast<void *>(F_PrSetOrthogonal);
    g_frameVt[0xB0 / 4] = reinterpret_cast<void *>(F_PrSetColor);
    g_frameVt[0xB4 / 4] = reinterpret_cast<void *>(F_PrSetMode);
    g_frameVt[0xB8 / 4] = reinterpret_cast<void *>(F_PrSetBuffered);
    g_sceneVt[0x8C / 4] = reinterpret_cast<void *>(F_GetLightIntensity);
    g_sceneVt[0xE0 / 4] = reinterpret_cast<void *>(F_GetSector);
    g_driverVt[0x50 / 4] = reinterpret_cast<void *>(F_CreateVisual);
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
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
    SetUpZones();
    TestHitZones();
    TestBulletsAndHumans();
    TestTraceUpdate();
    TestFlareUpdate();
    TestRecycleRelease();
    TestBulletDecals();
    SetUpRocket();
    TestProjectileInit();
    TestProjectileFlight();
    TestProjectileWalls();
    TestProjectileActors();
    TestProjectileCamera();
    TestManagerTick();
    TestManagerSaveLoad();
    TestManagerMessage();
    TestClientBullets();
    TestFireBullet();
    std::printf("shots: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
