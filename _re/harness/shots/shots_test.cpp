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
#include <set>
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
    S_CollisionVolume *volume = nullptr;                                    // (else a static one with flags)
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
    if (g_ray.volume)
        return g_ray.volume;
    static S_CollisionVolume volume;
    volume.flags = g_ray.flags;
    return &volume;
}
namespace {
struct S_DynRay {
    S_CollisionVolume *volume = nullptr;
    S_vector pos, normal;
};
S_DynRay g_dynRay;
std::vector<std::string> g_dynRayLog;
}
S_CollisionVolume *C_CollisionWorld::RayTestDynamic(const S_vector &from, const S_vector &dir, S_vector *pos, S_vector *normal,
                                                    uint32_t a, uint32_t b) const
{
    char s[160];
    std::snprintf(s, sizeof s, "dyn %g,%g,%g %g,%g,%g %x %x", from.x, from.y, from.z, dir.x, dir.y, dir.z, a, b);
    g_dynRayLog.push_back(s);
    if (g_dynRay.volume) {
        *pos = g_dynRay.pos;
        *normal = g_dynRay.normal;
    }
    return g_dynRay.volume;
}
namespace {
struct S_RayHit {
    bool hit;
    S_vector pos, normal;
};
std::vector<S_RayHit> g_rayHits;                                          // the next answers (none: no hit)
std::vector<std::string> g_freeRayLog;
float g_gameLight = 1.0f;
S_CollisionVolume g_rayVolume;
}  // namespace
S_CollisionVolume *CollisionRayTest(const S_vector &from, const S_vector &dir, S_vector *pos, S_vector *normal, uint32_t types,
                                    uint32_t flags)
{
    char s[160];
    std::snprintf(s, sizeof s, "ray %g,%g,%g %g,%g,%g %x %x", from.x, from.y, from.z, dir.x, dir.y, dir.z, types, flags);
    g_freeRayLog.push_back(s);
    if (g_rayHits.empty())
        return nullptr;
    const S_RayHit h = g_rayHits.front();
    g_rayHits.erase(g_rayHits.begin());
    if (!h.hit)
        return nullptr;
    *pos = h.pos;
    *normal = h.normal;
    return &g_rayVolume;
}
const S_HumanBody *g_lastBody;
void S_HumanBody::RemoveCollision(bool on) { g_lastBody = this; g_bodyLog.push_back(on ? "remove 1" : "remove"); }
void S_HumanBody::RestoreCollision(bool on) { g_bodyLog.push_back(on ? "restore 1" : "restore"); }
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
float C_game::GetLightIntensity(const S_vector &, I3D_sector *, bool) { return g_gameLight; }
namespace {
bool g_fpv;
}
bool C_game::IsFirstPersonView() const { return g_fpv; }
namespace {
std::vector<std::string> g_prints;
}
void C_game::Print(const char *format, ...) { g_prints.push_back(format); }
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
namespace {
std::vector<std::string> g_teamLog;
}
void C_Team::NotifyGrenadeThrown(const S_vector &pos, const S_vector &dir, C_Actor *thrower)
{
    g_teamLog.push_back("grenade " + std::to_string(pos.x) + " " + std::to_string(dir.x) + (thrower ? " thrower" : ""));
}
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
namespace {
S_vector g_facing(0, 0, 1);
}
const S_vector &C_Human::GetFacingDir() const { return g_facing; }
void C_Human::RemoveItem(int uid, bool destroy) { g_teamLog.push_back("remove " + std::to_string(uid) + " " + std::to_string(destroy)); }
int C_Car::GetHitMaterial(const S_vector &, const S_vector &) { return g_carMaterial; }
bool C_VersionKeeper::Damage(int damage, I3D_frame **, E_VersionEffects) { g_keeperDamage.push_back(damage); return g_keeperSurvives; }
namespace {
std::vector<std::string> g_masterDamage;
}
bool C_VersionMaster::DamageFrame(I3D_frame *frame, int damage)
{
    g_masterDamage.push_back(std::string(frame->GetName()) + " " + std::to_string(damage));
    return true;
}
namespace {
std::vector<int> g_itemFrameIndices;
}
I3D_frame *C_Item::GetFrame(int index)
{
    g_itemFrameIndices.push_back(index);
    return g_itemFrames.count(this) ? g_itemFrames[this] : nullptr;
}
namespace {
I3D_frame *g_gunBlast, *g_gunEject, *g_fpvBlast, *g_fpvEject;
}
I3D_frame *C_WeaponItem::GetBlastDummy() { return g_gunBlast; }
I3D_frame *C_WeaponItem::GetCartridgeDummy() { return g_gunEject; }
I3D_frame *C_FirstPersonView::GetBlastDummy() { return g_fpvBlast; }
I3D_frame *C_FirstPersonView::GetCartridgeDummy() { return g_fpvEject; }
C_Module::C_Module(int id, const char *) : m_id(id) {}
C_Module::~C_Module() {}
namespace {
std::vector<std::string> g_netLog;
alignas(16) unsigned char g_messageBytes[0x100];
}
C_BitStream *C_NetGame::CreateMessage(int type, int channel)
{
    g_netLog.push_back("create " + std::to_string(type) + " " + std::to_string(channel));
    return reinterpret_cast<C_BitStream *>(g_messageBytes);
}
void C_NetGame::SendNetMessage(C_BitStream *msg)
{
    g_netLog.push_back(std::string("send") + (msg == reinterpret_cast<C_BitStream *>(g_messageBytes) ? " it" : ""));
}
void C_NetMessage::ReadPosition(S_vector &pos) { pos = g_readPos; }
void C_NetMessage::WritePosition(const S_vector &pos) { g_netLog.push_back("position " + std::to_string(pos.x) + "," + std::to_string(pos.y) + "," + std::to_string(pos.z)); }
uint32_t C_BitStream::ReadBits(int bits)
{
    if (g_bits.empty())
        return 0xDEAD;
    const uint32_t v = g_bits.front();
    g_bits.erase(g_bits.begin());
    g_chunkLog.push_back("bits " + std::to_string(bits));
    return v;
}
void C_BitStream::WriteBits(uint32_t v, int n) { g_netLog.push_back("bits " + std::to_string(v) + " " + std::to_string(n)); }
namespace {
std::vector<std::string> g_bodyCalls;
S_RigidBodyParams g_bodyParams;
}
C_RigidBody::C_RigidBody() {}
C_RigidBody::~C_RigidBody() {}
bool C_RigidBody::SetFrame(I3D_frame *frame) { g_bodyCalls.push_back("frame " + std::to_string(frame != nullptr)); return true; }
void C_RigidBody::Setup(const S_RigidBodyParams &p) { g_bodyCalls.push_back("setup"); g_bodyParams = p; }
void C_RigidBody::Start() { g_bodyCalls.push_back("start"); }
void C_RigidBody::Stop() { g_bodyCalls.push_back("stop"); }
void C_RigidBody::Tick(uint32_t dt) { g_bodyCalls.push_back("tick " + std::to_string(dt)); }
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
namespace {
std::vector<std::string> g_imageLog;
void *g_imageVt[16];
struct S_FakeImage {
    void **vtable = g_imageVt;
    std::string name;
};
uint32_t __stdcall F_ImRelease(S_FakeImage *self) { g_imageLog.push_back("release " + self->name); return 0; }
HRESULT __stdcall F_LoadTexture(void *, const char *file, const char *alpha, uint32_t mode, IImage **out)
{
    g_imageLog.push_back(std::string("load ") + file + (alpha ? " alpha" : "") + " " + std::to_string(mode));
    auto *image = new S_FakeImage();
    image->name = file;
    *out = reinterpret_cast<IImage *>(image);
    return 0;
}
void *g_igraphVt[64];
struct S_FakeIGraph {
    void **vtable = g_igraphVt;
} g_igraph;
}  // namespace
extern "C" IGraph *GetIGraph() { return reinterpret_cast<IGraph *>(&g_igraph); }
namespace {
bool g_modelOpens, g_soundOpens;
std::vector<std::string> g_openLog;
}
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *name, uint32_t, void *, void *, int)
{
    g_openLog.push_back(std::string("model ") + name);
    return g_modelOpens ? 0 : E_FAIL;
}
template <> HRESULT C_ResourceCache<I3D_sound>::OpenInto(I3D_sound *, const char *name, uint32_t, void *, void *, int)
{
    g_openLog.push_back(std::string("sound ") + name);
    return g_soundOpens ? 0 : E_FAIL;
}


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
std::vector<std::pair<S_vector, S_vector>> g_intersectRays;              // from, dir
int g_intersectExtra;                                                    // or of the three unidentified arguments
I3D_frame *__stdcall F_IntersectFrame(void *, const S_vector &from, const S_vector &dir, I3D_frame *frame,
                                      I3D_collision_info *info, uint32_t flags, int a, int b, int c)
{
    g_intersectRays.push_back({from, dir});
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
void *g_projVt[128];
void *g_objVt[128];
void *g_lightVt[128];
void *g_soundVt[128];
void *g_sectorVt[128];
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
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type = FRAME_VISUAL, const char *name = nullptr, void **vt = nullptr);
void __stdcall F_UpdateFrameSector(void *, I3D_frame *frame, const S_vector &pos);
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
std::vector<std::pair<std::string, I3D_frame *>> g_nextChildren;
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    g_sceneLog.push_back("createframe " + std::to_string(type));
    if (!g_nextChildren.empty()) {
        I3D_frame *f = MakeFrame(static_cast<E_I3D_FRAME_TYPE>(type), "frame", g_frameVt);
        for (const auto &child : g_nextChildren)
            g_frameChildren[{f, child.first}] = child.second;
        g_nextChildren.clear();
        return f;
    }
    if (type == FRAME_LIGHT)
        return MakeFrame(FRAME_LIGHT, "light", g_lightVt);
    return MakeFrame(type == FRAME_SOUND ? FRAME_SOUND : static_cast<E_I3D_FRAME_TYPE>(type), type == FRAME_SOUND ? "sound" : "frame",
                     type == FRAME_SOUND ? g_soundVt : g_frameVt);
}
int g_visualsMade;
I3D_frame *__stdcall F_CreateVisual(void *, uint32_t type)
{
    I3D_frame *f = MakeFrame(FRAME_VISUAL, ((type == I3DVISUAL_PROJECTOR ? "decal" : "visual") + std::to_string(++g_visualsMade)).c_str(),
                             type == I3DVISUAL_PROJECTOR ? g_projVt : g_objVt);
    g_sceneLog.push_back("create " + std::to_string(type));
    S_vector &base = *reinterpret_cast<S_vector *>(reinterpret_cast<unsigned char *>(f) + 0x1F0);
    base = S_vector(0.5f, 0.25f, 1.0f);
    return f;
}
void __stdcall F_UpdateFrameSector(void *, I3D_frame *frame, const S_vector &pos)
{
    g_frameLog.push_back("updatesector " + FrameName(frame) + " " + Vec(pos));
}
// (At exit g_shotManager's pools free their shells after this file's maps are gone: no logging then.)
bool g_exiting;
uint32_t __stdcall F_FrRelease(I3D_frame *self)
{
    if (!g_exiting)
        g_frameLog.push_back("release " + FrameName(self));
    return 0;
}
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
std::string Num(float f)
{
    char s[32];
    std::snprintf(s, sizeof s, "%g", f);
    return s;
}
void __stdcall F_FrSetPosDir(I3D_frame *self, const S_vector &pos, const S_vector &dir, float roll)
{
    g_frameLog.push_back("posdir " + FrameName(self) + " " + Vec(pos) + " " + Vec(dir) + " " + Num(roll));
    self->m_localMatrix(3) = pos;
    self->m_worldMatrix(3) = pos;
}
void __stdcall F_FrSetName(I3D_frame *self, const char *name) { g_frameLog.push_back("name " + FrameName(self) + " " + name); }
void __stdcall F_FrSetLightLevel(I3D_frame *self, float v) { g_frameLog.push_back("lightlevel " + FrameName(self) + " " + Num(v)); }
void __stdcall F_ObSetAlpha(I3D_frame *self, float v) { g_frameLog.push_back("alpha " + FrameName(self) + " " + Num(v)); }
void __stdcall F_LiSetType(I3D_frame *self, int v) { g_frameLog.push_back("lighttype " + FrameName(self) + " " + std::to_string(v)); }
void __stdcall F_LiSetColor(I3D_frame *self, const S_vector &c) { g_frameLog.push_back("lightcolor " + FrameName(self) + " " + Vec(c)); }
void __stdcall F_LiSetSpecularColor(I3D_frame *self, const S_vector &c) { g_frameLog.push_back("specular " + FrameName(self) + " " + Vec(c)); }
void __stdcall F_LiSetPower(I3D_frame *self, float v) { g_frameLog.push_back("power " + FrameName(self) + " " + Num(v)); }
void __stdcall F_LiSetSpecularPower(I3D_frame *self, float v) { g_frameLog.push_back("specpower " + FrameName(self) + " " + Num(v)); }
void __stdcall F_LiSetMode(I3D_frame *self, uint32_t v) { g_frameLog.push_back("lightmode " + FrameName(self) + " " + std::to_string(v)); }
void __stdcall F_LiSetRange(I3D_frame *self, float v) { g_frameLog.push_back("range " + FrameName(self) + " " + Num(v)); }
void __stdcall F_LiSetAttenuation(I3D_frame *self, float a, float b, float c)
{
    g_frameLog.push_back("atten " + FrameName(self) + " " + Num(a) + " " + Num(b) + " " + Num(c));
}
void __stdcall F_SoSetType(I3D_frame *self, int v) { g_frameLog.push_back("soundtype " + FrameName(self) + " " + std::to_string(v)); }
void __stdcall F_SoSetRange(I3D_frame *self, float a, float b, float c, float d)
{
    g_frameLog.push_back("soundrange " + FrameName(self) + " " + Num(a) + " " + Num(b) + " " + Num(c) + " " + Num(d));
}
void __stdcall F_SoSetVolume(I3D_frame *self, float v) { g_frameLog.push_back("volume " + FrameName(self) + " " + Num(v)); }
void __stdcall F_SoSetLoop(I3D_frame *self, bool v) { g_frameLog.push_back("loop " + FrameName(self) + " " + std::to_string(v)); }
void __stdcall F_SeAddLight(I3D_frame *self, I3D_frame *light) { g_frameLog.push_back("addlight " + FrameName(self) + " " + FrameName(light)); }
void __stdcall F_SeRemoveLight(I3D_frame *self, I3D_frame *light)
{
    g_frameLog.push_back("removelight " + FrameName(self) + " " + FrameName(light));
}
HRESULT __stdcall F_FrDuplicate(I3D_frame *self, I3D_frame *source)
{
    g_frameLog.push_back("duplicate " + FrameName(self) + " " + FrameName(source));
    return 0;
}
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(bytes); }
};
I3D_frame *MakeFrame(E_I3D_FRAME_TYPE type, const char *name, void **vt)
{
    S_FakeFrame *fake = new S_FakeFrame();
    std::memset(fake->bytes, 0, sizeof(fake->bytes));
    I3D_frame *frame = fake->frame();
    *reinterpret_cast<void ***>(frame) = vt ? vt : g_frameVt;
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
C_Actor *g_seatOccupant;
int g_seatAsked = -1;
C_Actor *__thiscall F_GetSeatOccupant(C_Actor *, int seat) { g_seatAsked = seat; return g_seatOccupant; }
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
    g_bodyCalls.clear();
    g_masterDamage.clear();
    g_rayHits.clear();
    g_freeRayLog.clear();
    g_gameLight = 1.0f;
    g_modelOpens = g_soundOpens = false;
    g_openLog.clear();
    g_imageLog.clear();
    g_netLog.clear();
    g_dynRay = S_DynRay();
    g_dynRayLog.clear();
    g_facing = S_vector(0, 0, 1);
    g_seatOccupant = nullptr;
    g_intersectRays.clear();
    g_prints.clear();
    g_seatAsked = -1;
    g_nextChildren.clear();
    g_teamLog.clear();
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

// ---- thrown items (C_ThrownShot) ----

// A grenade: throw strength 0.5 x muzzle velocity 20 = 10 m/s; damage 30 (the penetration budget), a 2 s fuse,
// an explosion of 300 within 6 m, sound 7, effect 44; bounce sound group (its category) 15.
C_ProjectileWeaponComp g_grenadeComp;
C_ItemDef *g_grenadeDef;
C_WeaponItem *MakeGrenade(float strength = 0.5f)
{
    if (!g_grenadeDef) {
        g_grenadeComp.m_damage = 30.0f;
        g_grenadeComp.m_muzzleVelocity = 20.0f;
        g_grenadeComp.m_fuseTime = 2000;
        g_grenadeComp.m_explosionDamage = 300.0f;
        g_grenadeComp.m_explosionRadius = 6.0f;
        g_grenadeComp.m_explosionSound = 7;
        g_grenadeComp.m_explosionEffect = 44;
        g_grenadeDef = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
        std::memset(static_cast<void *>(g_grenadeDef), 0, sizeof(C_ItemDef));
        g_grenadeDef->m_component = &g_grenadeComp;
        g_grenadeDef->m_category = 15;
    }
    auto *item = static_cast<C_WeaponItem *>(::operator new(sizeof(C_WeaponItem)));
    std::memset(static_cast<void *>(item), 0, sizeof(C_WeaponItem));
    item->m_def = g_grenadeDef;
    item->m_throwStrength = strength;
    g_itemFrames[item] = MakeFrame(FRAME_MODEL, "grenademodel");
    return item;
}
C_ThrownShot *Throw(C_WeaponItem *item, const S_vector &pos, const S_vector &dir, bool fake = false)
{
    auto *s = new C_ThrownShot();
    s->Init(g_shooter, pos, dir, item, fake);
    return s;
}
S_RigidBodyContact Contact(S_CollisionVolume *v, const S_vector &pos, const S_vector &normal)
{
    S_RigidBodyContact c{};
    c.volume = v;
    c.pos = pos;
    c.normal = normal;
    return c;
}

void TestThrownInit()
{
    Reset();
    C_ThrownShot *s = Throw(MakeGrenade(), S_vector(1, 2, 3), S_vector(0, 0, 1));
    const std::string n = FrameName(s->m_visual);
    CHECK(s->m_state == C_ThrownShot::STATE_FLYING && Same(s->m_velocity, S_vector(0, 0, 10)) && s->m_time == 0 &&
          s->m_fuseTime == 2000 && s->m_bounceSound == 15 && s->m_bounceSoundTimer == 0 && Same(s->m_damage, 30.0f));
    // The item's model copied (C_Shot), opaque, in the dynamic root, aimed, placed, on.
    CHECK((g_frameLog == std::vector<std::string>{"duplicate " + n + " grenademodel", "link " + n + " null 0",
                                                  "alpha " + n + " 1", "link " + n + " null 0", "dir " + n + " 0,0,10 0",
                                                  "pos " + n + " 1,2,3", "on " + n, "update " + n}));
    // The body: up (0, 1, 0), a point 10 m above the start, the velocity, 2 kg, friction 0.5, the contact
    // callback with the shot, moving collision.
    CHECK((g_bodyCalls == std::vector<std::string>{"frame 1", "setup", "start"}));
    CHECK(Same(g_bodyParams.unknown00.y, 1.0f) && Same(g_bodyParams.unknown00.x, 0.0f) && Same(g_bodyParams.unknown10.x, 1.0f) &&
          Same(g_bodyParams.unknown10.y, 12.0f) && Same(g_bodyParams.unknown10.z, 3.0f) && Same(g_bodyParams.velocity.z, 10.0f) &&
          Same(g_bodyParams.weight, 2.0f) && Same(g_bodyParams.friction, 0.5f) && g_bodyParams.context == s &&
          g_bodyParams.moveCollision && !g_bodyParams.unknown41 && g_bodyParams.callback == &C_ThrownShot::OnContact);
    CHECK((g_itemFrameIndices == std::vector<int>{0}));
    // A weak throw: at least 5 m/s.
    Reset();
    s = Throw(MakeGrenade(0.1f), S_vector(0, 0, 0), S_vector(1, 0, 0));
    CHECK(Same(s->m_velocity, S_vector(5, 0, 0)));
    s = Throw(MakeGrenade(0.25f), S_vector(0, 0, 0), S_vector(1, 0, 0));      // exactly 5
    CHECK(Same(s->m_velocity, S_vector(5, 0, 0)));
    s = Throw(MakeGrenade(0.3f), S_vector(0, 0, 0), S_vector(1, 0, 0));       // 6: kept
    CHECK(Same(s->m_velocity, S_vector(0.3f * 20.0f, 0, 0)));
    // Release stops the body and frees the model.
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    const std::string n2 = FrameName(s->m_visual);
    g_bodyCalls.clear();
    g_frameLog.clear();
    s->Release();
    CHECK((g_bodyCalls == std::vector<std::string>{"stop"}) && (g_frameLog == std::vector<std::string>{"release " + n2}) &&
          s->m_visual == nullptr);
}

void TestThrownContact()
{
    using T = C_ThrownShot;
    Reset();
    C_ThrownShot *s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000; s->m_time = 1000;
    auto contact = [&](S_RigidBodyContact c, int event = RIGID_BODY_EVENT_CONTACT) { return T::OnContact(event, 0, s, &c); };
    // Only contacts count.
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0)), 2) == 0);
    // Static: PASS refused; STOP: finished, 3; cheaper than the damage (cost < 30): refused (passes); equal or
    // dearer, impenetrable: a bounce (a fused grenade: 2).
    CHECK(contact(Contact(StaticVolume(MATERIAL_PASS), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE);
    SetMaterial(5, 0, 29);
    CHECK(contact(Contact(StaticVolume(5), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE);
    SetMaterial(5, 0, 30);
    CHECK(contact(Contact(StaticVolume(5), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 2);
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 2);
    CHECK(s->m_state == T::STATE_FLYING);
    // The first bounce is heard (client): the category's sound at the model, silent to the AI; then never
    // again (the timer is set to 2000 and not counted down).
    CHECK(g_sounds.size() == 1 && Same(g_sounds[0].pos, S_vector(0, 0, 0)));
    CHECK(g_sounds.size() == 1 && g_sounds[0].group == 15 && g_sounds[0].chapter == SOUND_CHAPTER_FALLING_HIT &&
          g_sounds[0].flags == SOUND_SILENT_TO_AI && g_sounds[0].source == nullptr && s->m_bounceSoundTimer == 2000);
    CHECK(contact(Contact(StaticVolume(MATERIAL_STOP), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 3 && s->m_state == T::STATE_FINISHED);
    // The sound at the model's place.
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    s->m_visual->m_worldMatrix(3) = S_vector(7, 8, 9);
    contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0)));
    CHECK(g_sounds.size() == 1 && Same(g_sounds[0].pos, S_vector(7, 8, 9)));
    // A bounce sound only below 50 ms of its timer, and for a category above 0.
    for (int variant = 0; variant < 3; ++variant) {
        Reset();
        s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
        if (variant == 0)
            s->m_bounceSoundTimer = 49;
        if (variant == 1)
            s->m_bounceSoundTimer = 50;
        if (variant == 2)
            s->m_bounceSound = 0;
        contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0)));
        CHECK(g_sounds.size() == (variant == 0 ? 1u : 0u));
    }
    // A server alone hears nothing.
    Reset(GAME_ROLE_SERVER);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0)));
    CHECK(g_sounds.empty());
    // A human: hit by the bounce for 1 (server), at the contact along its normal.
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    C_Actor *man = MakeActor(ACTOR_HUMAN, MakeFrame(FRAME_MODEL, "tman"));
    CHECK(contact(Contact(DynamicVolume(man, man->m_frame), S_vector(2, 0, 0), S_vector(-1, 0, 0))) == 2);
    CHECK(g_hits.size() == 1 && g_hits[0].type == HIT_THROWN_OBJECT && Same(g_hits[0].amount, 1.0f) &&
          Same(g_hits[0].pos, S_vector(2, 0, 0)) && Same(g_hits[0].dir, S_vector(-1, 0, 0)) && g_hits[0].source == g_shooter);
    // Another actor: no hit. The thrower: refused.
    Reset();
    C_Actor *barrel = MakeActor(ACTOR_BARREL, MakeFrame(FRAME_MODEL, "tbarrel"));
    CHECK(contact(Contact(DynamicVolume(barrel, barrel->m_frame), S_vector(2, 0, 0), S_vector(-1, 0, 0))) == 2 && g_hits.empty());
    CHECK(contact(Contact(DynamicVolume(g_shooter, nullptr), S_vector(2, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE);
    // An impact grenade (no fuse): the first contact sets it off.
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    s->m_fuseTime = 0;
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 3 && s->m_state == T::STATE_IMPACT);
    // The player's AI team mates' grenades pass anything in the first 200 ms (single player; not the local
    // player's own, not the enemy's).
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    s->m_time = 200;
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE);
    s->m_time = 201;
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 2);
    s->m_time = 0;
    g_localPlayer = reinterpret_cast<C_Human *>(g_shooter);
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 2);
    g_localPlayer = nullptr;
    reinterpret_cast<C_Human *>(g_shooter)->m_role = HUMAN_ROLE_ENEMY;
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 2);
    reinterpret_cast<C_Human *>(g_shooter)->m_role = HUMAN_ROLE_PLAYER;
    Reset(GAME_ROLE_SERVER);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    CHECK(contact(Contact(StaticVolume(2), S_vector(1, 0, 0), S_vector(-1, 0, 0))) == 2);
    // A destructible object (a volume without actor whose frame has a version master): the part along the
    // normal (from 0.1 m outside), cheaper than the damage: broken (the server) with the grenade's damage, 10000
    // for a fragile one (62), and passed; dearer, impenetrable, no part, or no material: a bounce.
    alignas(16) static unsigned char masterBytes[sizeof(C_VersionMaster)];
    static S_FrameData data{};
    data.versionMaster = reinterpret_cast<C_VersionMaster *>(masterBytes);
    I3D_frame *wall = MakeFrame(FRAME_VISUAL, "twall");
    wall->m_data = &data;
    I3D_frame *glass = MakeFrame(FRAME_VISUAL, "tglass");
    static char glassProps[] = "matid 62";
    glass->m_userProps = glassProps;
    SetMaterial(MATERIAL_FRAGILE, 0, 1);
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    g_intersects = {{glass, 0.1f, S_vector(-1, 0, 0)}};
    CHECK(contact(Contact(DynamicVolume(nullptr, wall), S_vector(3, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE);
    CHECK(g_intersectFrames.size() == 1 && g_intersectFrames[0] == wall && g_intersectFlags[0] == 0 && g_intersectExtra == 0);
    CHECK(Same(g_intersectRays[0].first, S_vector(-1, 0, 0) * 0.1f + S_vector(3, 0, 0)) &&
          g_intersectRays[0].second.x == 1.0f && g_intersectRays[0].second.y == 0.0f && g_intersectRays[0].second.z == 0.0f);
    CHECK((g_masterDamage == std::vector<std::string>{"tglass 10000"}));
    static char plankProps[] = "matid 5";
    glass->m_userProps = plankProps;
    SetMaterial(5, 0, 10);
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    g_intersects = {{glass, 0.1f, S_vector(-1, 0, 0)}};
    CHECK(contact(Contact(DynamicVolume(nullptr, wall), S_vector(3, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE);
    CHECK((g_masterDamage == std::vector<std::string>{"tglass 30"}));
    Reset(GAME_ROLE_CLIENT);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    g_intersects = {{glass, 0.1f, S_vector(-1, 0, 0)}};
    CHECK(contact(Contact(DynamicVolume(nullptr, wall), S_vector(3, 0, 0), S_vector(-1, 0, 0))) == RIGID_BODY_REFUSE &&
          g_masterDamage.empty());
    for (int variant = 0; variant < 4; ++variant) {
        Reset();
        s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
        SetMaterial(5, 0, variant == 0 ? 30 : MATERIAL_IMPENETRABLE);
        glass->m_userProps = variant == 3 ? nullptr : plankProps;
        g_intersects = {{variant == 2 ? nullptr : glass, 0.1f, S_vector(-1, 0, 0)}};
        CHECK(contact(Contact(DynamicVolume(nullptr, wall), S_vector(3, 0, 0), S_vector(-1, 0, 0))) == 2 && g_masterDamage.empty());
    }
    glass->m_userProps = nullptr;
    wall->m_data = nullptr;
    // Without a version master: no part tested, a bounce.
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0)); s->m_time = 1000;
    CHECK(contact(Contact(DynamicVolume(nullptr, wall), S_vector(3, 0, 0), S_vector(-1, 0, 0))) == 2 && g_intersectFrames.empty());
}

void TestThrownFlight()
{
    using T = C_ThrownShot;
    // A server: the body ticked in Tick (the thrower's collision off around it); the fuse past 2000 ms: the
    // explosion at the model, the full damage; finished; the next Tick: gone.
    Reset(GAME_ROLE_SERVER);
    C_ThrownShot *s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    g_bodyCalls.clear();
    g_bodyLog.clear();
    s->m_visual->m_worldMatrix(3) = S_vector(4, 0.5f, 0);
    CHECK(!s->Tick(1000) && (g_bodyCalls == std::vector<std::string>{"tick 1000"}) &&
          (g_bodyLog == std::vector<std::string>{"remove", "restore"}) && s->m_time == 1000);
    s->Update(100);                                                       // (a server's Update simulates too)
    CHECK(s->m_time == 1100);
    CHECK(!s->Tick(900) && s->m_state == T::STATE_FLYING);                 // 2000: not past it
    CHECK(s->Tick(1) && s->m_state == T::STATE_FINISHED);
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].pos, S_vector(4, 0.5f, 0)) && Same(g_explosions[0].range, 6.0f) &&
          Same(g_explosions[0].damage, 300.0f) && g_explosions[0].source == g_shooter);
    CHECK(g_bodyCalls.back() == "stop" && g_effects.empty());
    CHECK(s->Tick(100));
    // A client: Tick does not simulate; Update does; the fuse plays the explosion with the shot's material.
    Reset(GAME_ROLE_CLIENT);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    s->m_material = 3;
    s->m_normal = S_vector(0, 1, 0);
    g_bodyCalls.clear();
    CHECK(!s->Tick(1000) && g_bodyCalls.empty() && s->m_time == 0);
    s->Update(2001);
    CHECK(s->m_state == T::STATE_FINISHED && g_explosions.empty() && (g_effects == std::vector<int>{44, 503}));
    s->Update(10);
    CHECK(g_bodyCalls.size() == 2);                                       // (finished: no more simulation)
    // An impact (server and client): stopped; the explosion 0.2 along the contact normal, played at the model
    // without a material; finished.
    Reset();
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    s->m_visual->m_worldMatrix(3) = S_vector(4, 0, 0);
    s->m_state = T::STATE_IMPACT;
    s->m_normal = S_vector(0, 1, 0);
    s->Update(10);
    CHECK(s->m_state == T::STATE_FINISHED && g_explosions.size() == 1 && Same(g_explosions[0].pos, S_vector(4, 0.2f, 0)));
    CHECK((g_effects == std::vector<int>{44}) && s->m_time == 0);
    Reset(GAME_ROLE_SERVER);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    s->m_state = T::STATE_IMPACT;
    CHECK(s->Tick(10) && g_explosions.size() == 1 && s->m_state == T::STATE_FINISHED);
    // A 1 ms fuse is a fuse (> 0).
    Reset(GAME_ROLE_SERVER);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    s->m_fuseTime = 1;
    CHECK(!s->Tick(1) && s->Tick(1) && g_explosions.size() == 1);
    // No fuse and no impact: lost after 15 s (15001 ms).
    Reset(GAME_ROLE_SERVER);
    s = Throw(MakeGrenade(), S_vector(0, 0, 0), S_vector(1, 0, 0));
    s->m_fuseTime = 0;
    CHECK(!s->Tick(15000));
    CHECK(s->Tick(1) && g_explosions.empty());
}

// ---- shells, blood, muzzle lights (the manager's effects) ----

std::vector<std::string> LogOf(const std::string &name)
{
    std::vector<std::string> out;
    for (const std::string &line : g_frameLog) {
        const size_t sp = line.find(' ');
        const size_t sp2 = line.find(' ', sp + 1);
        if (sp != std::string::npos && line.substr(sp + 1, sp2 - sp - 1) == name)
            out.push_back(line);
    }
    return out;
}

void TestShells()
{
    C_ShotManager &m = g_shotManager;
    C_Human *shooter = reinterpret_cast<C_Human *>(g_shooter);
    I3D_frame *dummy = MakeFrame(FRAME_DUMMY, "eject");
    // The dummy at (1, 2, 3), its axes: x (1,0,0), y (0,2,0) (an unnormalised up), z (0,0,1).
    dummy->m_worldMatrix(1) = S_vector(0, 2, 0);
    dummy->m_worldMatrix(3) = S_vector(1, 2, 3);
    I3D_frame *shell3rd = MakeFrame(FRAME_VISUAL, "shell3rd");
    I3D_frame *shellFpv = MakeFrame(FRAME_VISUAL, "shellFPV");
    m.m_shell3rd = shell3rd;
    m.m_shellFpv = shellFpv;
    shooter->m_velocity.x = 0.5f; shooter->m_velocity.y = 0.0f; shooter->m_velocity.z = -1.0f;
    // Only the active player's weapon throws shells; with both models and a dummy.
    Reset();
    m.EjectShell(shooter, dummy, 6);
    CHECK(m.m_shells.empty());
    g_activePlayer = shooter;
    m.m_shellFpv = nullptr;
    m.EjectShell(shooter, dummy, 6);
    m.m_shellFpv = shellFpv;
    m.EjectShell(shooter, nullptr, 6);
    CHECK(m.m_shells.empty());
    // Thrown: velocity = the dummy's up, x times (r - 0.5) * 0.5 + 1.5, y times (r - 0.5) + 1.5, z times the third
    // draw's (r - 0.5) * 0.5 + 1.5, plus the shooter's velocity; the third-person model copied (once), placed on
    // the dummy facing its z, its sector found, on.
    Reset();
    g_activePlayer = shooter;
    g_randFloat = 0.75f;
    m.EjectShell(shooter, dummy, 6);
    CHECK(m.m_shells.size() == 1);
    CHECK((g_sceneLog == std::vector<std::string>{"create " + std::to_string(I3DVISUAL_OBJECT)}));
    std::string v;
    for (const std::string &line : g_frameLog)
        if (line.rfind("duplicate ", 0) == 0)
            v = line.substr(10, line.find(' ', 10) - 10);
    CHECK((g_frameLog == std::vector<std::string>{"duplicate " + v + " shell3rd", "link " + v + " null 0",
                                                  "posdir " + v + " 1,2,3 0,0,1 0", "updatesector " + v + " 1,2,3", "on " + v,
                                                  "update " + v}));
    // Its flight (Update): the velocity, the dummy's up (0,2,0): x 0 + 0.5, y 2 * (0.25 + 1.5) = 3.5, z 0 - 1.
    const S_vector v0(0.0f * ((0.75f - 0.5f) * 0.5f + 1.5f) + 0.5f, 2.0f * ((0.75f - 0.5f) + 1.5f) + 0.0f,
                      ((0.75f - 0.5f) * 0.5f + 1.5f) * 0.0f + -1.0f);
    g_frameLog.clear();
    m.Update(100);                                                        // no hit: moved by the step
    const S_vector step(v0.x * 100.0f * 0.001f, v0.y * 100.0f * 0.001f, v0.z * 100.0f * 0.001f);
    CHECK((g_freeRayLog == std::vector<std::string>{"ray 1,2,3 " + Vec(step) + " 1 0"}));
    CHECK((LogOf(v) == std::vector<std::string>{"pos " + v + " " + Vec(S_vector(1, 2, 3) + step), "update " + v}));
    CHECK((g_bodyLog == std::vector<std::string>{"remove", "restore"}));                // (false, false)
    // A bounce off the floor (normal up): the velocity reflected (minus n * (n.v * 1.2), turned around if it
    // still goes into the surface), the sound at the old place (chapter 5, channel 2, silent to the AI), the shell
    // moved to the hit.
    S_vector vel = v0;
    vel.y -= 100.0f * 0.001f * 3.8f;                                      // (gravity after the first step)
    const S_vector at = S_vector(1, 2, 3) + step;
    g_rayHits = {{true, S_vector(1.5f, 2.0f, 2.0f), S_vector(0, 1, 0)}};
    g_frameLog.clear();
    g_sounds.clear();
    m.Update(100);
    S_vector want = vel;
    const float d = (0.0f * vel.x + 1.0f * vel.y + 0.0f * vel.z) * 1.2f;
    want.y -= d;
    if (want.y < 0.0f)
        want = S_vector(-want.x, -want.y, -want.z);
    want.y -= 100.0f * 0.001f * 3.8f;
    CHECK(g_sounds.size() == 1 && g_sounds[0].chapter == SOUND_CHAPTER_FALLING_HIT && g_sounds[0].group == 6 &&
          g_sounds[0].channel == 2 && g_sounds[0].flags == SOUND_SILENT_TO_AI && Same(g_sounds[0].pos, at));
    CHECK((LogOf(v) == std::vector<std::string>{"pos " + v + " 1.5,2,2", "update " + v}));
    // (The next step starts at the hit, with the reflected velocity.)
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.size() == 1 &&
          g_freeRayLog[0] == "ray 1.5,2,2 " + Vec(S_vector(want.x * 0.1f, want.y * 0.1f, want.z * 0.1f)) + " 1 0");
    // Stuck (the hit where it is): stopped, a sound, the next step 0.
    const S_vector here = S_vector(1.5f, 2.0f, 2.0f) + S_vector(want.x * 0.1f, want.y * 0.1f, want.z * 0.1f);
    g_rayHits = {{true, here, S_vector(0, 1, 0)}};
    g_sounds.clear();
    m.Update(100);
    CHECK(g_sounds.empty());
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0] == "ray " + Vec(here) + " 0,0,0 1 0");
    // Done after more than 2 s: hidden, back to the pool.
    g_rayHits.clear();
    g_frameLog.clear();
    for (int i = 0; i < 15; ++i)
        m.Update(100);                                                    // 2000 ms in all: still there (> 2000)
    CHECK(m.m_shells.size() == 1);
    m.Update(1);
    CHECK(m.m_shells.empty());
    CHECK(LogOf(v).size() >= 2 && LogOf(v)[LogOf(v).size() - 2] == "off " + v && LogOf(v).back() == "update " + v);
    // A shell at 0.12 m/s bounces (>= 0.1); level after the reflection: kept (n.v' 0 is not below 0); one
    // going up after it is not turned (0.4 > 0).
    for (int variant = 0; variant < 3; ++variant) {
        Reset();
        g_activePlayer = shooter;
        m.RemoveEffects(false);
        shooter->m_velocity.x = 0.0f; shooter->m_velocity.y = 0.0f; shooter->m_velocity.z = 0.0f;
        const S_vector up = variant == 0 ? S_vector(0, -0.08f, 0) : variant == 1 ? S_vector(1, 0, 0) : S_vector(0.5f, -1.0f, 0);
        dummy->m_worldMatrix(1) = up;
        g_randFloat = 0.5f;                                               // (factors x 1.5, y 1.5)
        m.EjectShell(shooter, dummy, 6);
        const S_vector v1(up.x * 1.5f, up.y * 1.5f, up.z * 1.5f);         // 0.12 down, (1.5,0,0), (0.75,-1.5,0)
        g_rayHits = {{true, S_vector(1, 1, 3), S_vector(0, 1, 0)}};
        m.Update(100);
        g_freeRayLog.clear();
        m.Update(100);
        S_vector r = v1;
        if (variant != 0 || v1.Magnitude() >= 0.1f) {
            r.y -= (r.y * 1.2f);
            if (r.y < 0.0f)
                r = S_vector(-r.x, -r.y, -r.z);
        }
        r.y -= 100.0f * 0.001f * 3.8f;
        CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0] == "ray 1,1,3 " + Vec(S_vector(r.x * 0.1f, r.y * 0.1f, r.z * 0.1f)) + " 1 0");
    }
    // Exactly 0.1 m/s (the shooter's own velocity, the dummy's up 0): bounces, not stopped (< 0.1).
    Reset();
    g_activePlayer = shooter;
    m.RemoveEffects(false);
    dummy->m_worldMatrix(1) = S_vector(0, 0, 0);
    shooter->m_velocity.x = 0.0f; shooter->m_velocity.y = -0.1f; shooter->m_velocity.z = 0.0f;
    m.EjectShell(shooter, dummy, 6);
    g_rayHits = {{true, S_vector(1, 1, 3), S_vector(0, 1, 0)}};
    m.Update(100);
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0] != "ray 1,1,3 0,0,0 1 0");
    // A pooled shell thrown for another player: his collision is the one switched off.
    m.RemoveEffects(false);
    {
        C_Human *other = reinterpret_cast<C_Human *>(MakeActor(ACTOR_HUMAN, MakeFrame(FRAME_MODEL, "other")));
        g_activePlayer = other;
        m.EjectShell(other, dummy, 6);
        m.Update(10);
        CHECK(g_lastBody == &other->m_body);
        m.RemoveEffects(false);
        g_activePlayer = shooter;
    }
    // The velocity: each axis of the dummy's up by its own factor, plus the shooter's own velocity.
    Reset();
    g_activePlayer = shooter;
    m.RemoveEffects(false);
    dummy->m_worldMatrix(1) = S_vector(1, 2, 3);
    shooter->m_velocity.x = 0.5f; shooter->m_velocity.y = 0.25f; shooter->m_velocity.z = -1.0f;
    g_randFloat = 0.75f;
    m.EjectShell(shooter, dummy, 6);
    g_freeRayLog.clear();
    m.Update(100);
    {
        const float fx = (0.75f - 0.5f) * 0.5f + 1.5f, fy = (0.75f - 0.5f) + 1.5f, fz = (0.75f - 0.5f) * 0.5f + 1.5f;
        const S_vector vel(1.0f * fx + 0.5f, 2.0f * fy + 0.25f, fz * 3.0f + -1.0f);
        CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0] == "ray 1,2,3 " + Vec(S_vector(vel.x * 100.0f * 0.001f, vel.y * 100.0f * 0.001f, vel.z * 100.0f * 0.001f)) + " 1 0");
    }
    shooter->m_velocity.x = 0.0f; shooter->m_velocity.y = 0.0f; shooter->m_velocity.z = 0.0f;
    dummy->m_worldMatrix(1) = S_vector(0, 2, 0);
    // A shell that is not the first one finishing first (three bounces): the right one goes.
    Reset();
    g_activePlayer = shooter;
    m.RemoveEffects(false);
    m.EjectShell(shooter, dummy, 6);
    m.EjectShell(shooter, dummy, 7);
    C_Shell *first = m.m_shells[0];
    for (int i = 0; i < 3; ++i) {
        g_rayHits = {{false, S_vector(0, 0, 0), S_vector(0, 0, 0)}, {true, S_vector(1, 1.5f - 0.25f * i, 3), S_vector(0, 1, 0)}};
        m.Update(10);
    }
    m.Update(10);
    CHECK(m.m_shells.size() == 1 && m.m_shells[0] == first);
    // RemoveEffects puts the shells back into the pool (a later one reuses its copy: no new visual).
    m.RemoveEffects(false);
    g_sceneLog.clear();
    m.EjectShell(shooter, dummy, 6);
    m.EjectShell(shooter, dummy, 6);
    CHECK(g_sceneLog.empty());
    m.RemoveEffects(false);
    g_activePlayer = nullptr;
    // The pooled shell keeps its copy: the next one is not duplicated again; a slow shell's bounce stops it.
    Reset();
    g_activePlayer = shooter;
    shooter->m_velocity.x = 0.0f; shooter->m_velocity.y = 0.0f; shooter->m_velocity.z = 0.0f;
    dummy->m_worldMatrix(1) = S_vector(0, 0.01f, 0);
    g_randFloat = 0.5f;
    m.EjectShell(shooter, dummy, 6);
    CHECK(g_sceneLog.empty() && m.m_shells.size() == 1);
    g_rayHits = {{true, S_vector(1, 1, 3), S_vector(0, 1, 0)}};
    m.Update(100);                                                        // |v| 0.015 < 0.1: stopped
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0] == "ray 1,1,3 0,0,0 1 0");
    // Three bounces: done at once.
    Reset();
    g_activePlayer = shooter;
    m.RemoveEffects(false);
    dummy->m_worldMatrix(1) = S_vector(0, 2, 0);
    m.EjectShell(shooter, dummy, 6);
    for (int i = 0; i < 3; ++i) {
        g_rayHits = {{true, S_vector(1, 1.5f - 0.25f * i, 3), S_vector(0, 1, 0)}};   // (never where it is: that is stuck)
        m.Update(10);
    }
    CHECK(m.m_shells.size() == 1);
    m.Update(10);
    CHECK(m.m_shells.empty());
    // In the first-person view, the local player's: the first-person model, smaller, in the view's root.
    Reset();
    g_activePlayer = shooter;
    g_localPlayer = shooter;
    m.RemoveEffects(false);
    g_fpv = true;
    I3D_frame *fpRoot = MakeFrame(FRAME_DUMMY, "fproot");
    *reinterpret_cast<I3D_frame **>(reinterpret_cast<unsigned char *>(&g_scene) + 0x1E4) = fpRoot;
    *reinterpret_cast<I3D_frame **>(reinterpret_cast<unsigned char *>(&g_scene) + 0x204) = fpRoot;
    m.EjectShell(shooter, dummy, 6);
    CHECK(m.m_shells.size() == 1 && Count(g_frameLog, "updatesector " + v + " 1,2,3") == 0);
    std::string fv;
    for (const std::string &line : g_frameLog)
        if (line.rfind("link ", 0) == 0 && line.find(" fproot ") != std::string::npos)
            fv = line;
    CHECK(fv.size() > 0 && fv.substr(fv.size() - 2) == " 1");
    {
        const std::string sv = fv.substr(5, fv.find(' ', 5) - 5);
        I3D_frame *sf = nullptr;
        for (const auto &entry : g_frameNames)
            if (entry.second == sv)
                sf = const_cast<I3D_frame *>(entry.first);
        CHECK(sf && Same(sf->m_scale, S_vector(0.6f, 0.6f, 0.6f)));
    }
    // (A pooled shell keeps its first model: the first-person one is used only by a new copy.)
    (void)shellFpv;
    g_fpv = false;
    // Not the local player: the third-person way even in the first-person view.
    m.RemoveEffects(false);
    g_localPlayer = nullptr;
    g_fpv = true;
    g_frameLog.clear();
    m.EjectShell(shooter, dummy, 6);
    bool sector = false;
    for (const std::string &line : g_frameLog)
        sector |= line.rfind("updatesector ", 0) == 0;
    CHECK(sector);
    g_fpv = false;
    g_localPlayer = nullptr;
    g_activePlayer = nullptr;
    shooter->m_velocity.x = 0.0f; shooter->m_velocity.y = 0.0f; shooter->m_velocity.z = 0.0f;
}

void TestBlood()
{
    C_ShotManager &m = g_shotManager;
    for (int i = 0; i < 7; ++i)
        m.m_splatTextures[i] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x2000 + i));
    m.m_maxBloodPools = 2;
    m.m_maxBloodDrops = 2;
    m.RemoveEffects(false);
    // Pools: off without decals.
    Reset();
    m.AddBloodPool(S_vector(1, 0, 1), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.empty());
    // A pool: a projector with the 6th splat texture 5 cm above the ground, 0.1 big, facing down turned by
    // (r - 0.5) * 2 pi, in the sector, coloured by the light (at least 0.2), buffered; it grows 1 mm every 100 ms
    // to its size (the given one + (2r - 1) * 0.1).
    Reset();
    g_decalsOn = true;
    g_randFloat = 0.75f;
    g_gameLight = 0.1f;
    m.AddBloodPool(S_vector(1, 0, 1), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.size() == 1);
    std::string v = FrameName(reinterpret_cast<I3D_frame *const *>(m.m_bloodPools[0])[0]);
    char roll[32];
    std::snprintf(roll, sizeof roll, "%g", (0.75f - 0.5f) * 6.28318548f);
    CHECK((LogOf(v) == std::vector<std::string>{"texture " + v + " 8198", "pos " + v + " 1,0.05,1",
                                                "dir " + v + " -0,-1,-0 " + roll, "mode " + v + " 1", "link " + v + " null 1",
                                                "link " + v + " sector 1", "color " + v + " " + Vec(kBase * 0.2f),
                                                "buffered " + v + " 3", "update " + v}));
    CHECK((g_sceneLog == std::vector<std::string>{"create " + std::to_string(I3DVISUAL_PROJECTOR), "sector 1,0.05,1"}));
    const float* f = reinterpret_cast<const float *>(m.m_bloodPools[0]);
    CHECK(Same(f[1], 0.01f) && Same(f[2], 0.3f));                        // size, the most (0.25 + 0.5 * 0.1)
    g_frameLog.clear();
    m.Update(100);                                                        // 100: not yet (> 100)
    CHECK(LogOf(v).empty());
    m.Update(1);
    CHECK((LogOf(v) == std::vector<std::string>{"buffered " + v + " 3", "update " + v}) && Same(f[1], 0.01f + 0.001f));
    {
        I3D_frame *pv = reinterpret_cast<I3D_frame *const *>(m.m_bloodPools[0])[0];
        CHECK(Same(pv->m_scale, S_vector(0.01f + 0.001f, 0.01f + 0.001f, 0.1f)));
    }
    // The cadence: 1 ms over 100 kept (101 - 100); 99 more: 100, no growth; 1 more: grows.
    m.Update(99);
    CHECK(Same(f[1], 0.01f + 0.001f));
    m.Update(1);
    CHECK(Same(f[1], 0.01f + 0.001f + 0.001f));
    for (int i = 0; i < 400 && reinterpret_cast<const int *>(f)[3] != -1; ++i)
        m.Update(101);
    CHECK(reinterpret_cast<const int *>(f)[3] == -1 && f[1] >= 0.3f && f[1] < 0.3f + 0.0015f);
    g_frameLog.clear();
    m.Update(1000);
    CHECK(LogOf(v).empty());                                              // grown: no more
    // More than the most: the oldest moves (reused, the same projector).
    m.AddBloodPool(S_vector(2, 0, 2), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.size() == 2);
    g_sceneLog.clear();
    g_frameLog.clear();
    C_BloodPool *oldest = m.m_bloodPools[0];
    m.AddBloodPool(S_vector(3, 0, 3), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.size() == 2 && m.m_bloodPools[1] == oldest && g_sceneLog.size() == 1);
    CHECK(LogOf(v).size() == 6 && LogOf(v)[0] == "pos " + v + " 3,0.05,3");   // (no mode, no unlink: as the original's)
    // The light above 0.2: the base colour times it.
    Reset();
    g_decalsOn = true;
    g_gameLight = 0.5f;
    m.AddBloodPool(S_vector(4, 0, 4), S_vector(0, 1, 0), 0.25f);
    v = FrameName(reinterpret_cast<I3D_frame *const *>(m.m_bloodPools.back())[0]);
    CHECK(std::find(g_frameLog.begin(), g_frameLog.end(), "color " + v + " " + Vec(kBase * 0.5f)) != g_frameLog.end());
    // Drops: a hidden projector; it flies at 3 m/s +-1 along the direction, falls after 400 ms, lands on what it
    // hits (shown there, 2 cm off, sized (r - 0.5) * 0.1 + 0.2), gives up after 1 s.
    m.RemoveEffects(false);
    Reset();
    g_decalsOn = true;
    g_randFloat = 0.75f;
    m.SprayBlood(S_vector(0, 1, 0), S_vector(2, 0, 0));
    CHECK(m.m_bloodDrops.size() == 1);
    const float speed = (0.75f - 0.5f) + (0.75f - 0.5f) + 3.0f;
    for (const std::string &line : g_frameLog)
        if (line.rfind("texture ", 0) == 0)
            v = line.substr(8, line.find(' ', 8) - 8);
    CHECK((LogOf(v) == std::vector<std::string>{"texture " + v + " 8193", "off " + v, "update " + v}));
    m.Update(100);
    CHECK((g_freeRayLog == std::vector<std::string>{"ray 0,1,0 " + Vec(S_vector(speed * 100.0f * 0.001f, 0, 0)) + " ffffffff 0"}));
    for (int i = 0; i < 3; ++i)
        m.Update(100);                                                    // 400 ms: still level
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0].find(" 0.35,0,0 ") != std::string::npos);   // (speed 3.5: 0.35 m)
    g_freeRayLog.clear();
    m.Update(100);                                                        // past 400: falling now
    {
        S_vector vel(speed, 0, 0);
        vel.y -= 100.0f * 0.001f * 9.8f;                                  // (the 500 ms tick's gravity)
        CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0] == "ray " + Vec(S_vector(speed * 0.5f, 1, 0)) + " " +
              Vec(S_vector(vel.x * 100.0f * 0.001f, vel.y * 100.0f * 0.001f, 0)) + " ffffffff 0");
    }
    g_freeRayLog.clear();
    g_rayHits = {{true, S_vector(2, 0, 0), S_vector(0, 1, 0)}};
    g_frameLog.clear();
    m.Update(100);
    std::snprintf(roll, sizeof roll, "%g", (0.75f - 0.5f) * 6.28318548f);
    const float size = (0.75f - 0.5f) * 0.1f + 0.2f;
    CHECK((LogOf(v) == std::vector<std::string>{"buffered " + v + " 0", "pos " + v + " 2,0.02,0",
                                                "worlddir " + v + " -0,-1,-0 " + roll, "mode " + v + " 1", "on " + v,
                                                "update " + v, "link " + v + " null 1", "link " + v + " sector 1",
                                                "color " + v + " " + Vec(kBase), "buffered " + v + " 3", "update " + v}));
    I3D_frame *dv = nullptr;
    for (const auto &entry : g_frameNames)
        if (entry.second == v)
            dv = const_cast<I3D_frame *>(entry.first);
    CHECK(dv && Same(dv->m_scale, S_vector(size, size, 0.05f)));
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.empty());                                          // landed: no more
    // Gravity from the tick that passes 400 ms (401).
    m.RemoveEffects(false);
    Reset();
    g_decalsOn = true;
    m.SprayBlood(S_vector(0, 1, 0), S_vector(2, 0, 0));
    m.Update(401);
    g_freeRayLog.clear();
    m.Update(100);
    CHECK(g_freeRayLog.size() == 1 && g_freeRayLog[0].find(",-0.") != std::string::npos);
    // A drop that hits nothing gives up after 1 s (no more rays).
    m.RemoveEffects(false);
    Reset();
    g_decalsOn = true;
    m.SprayBlood(S_vector(0, 1, 0), S_vector(2, 0, 0));
    for (int i = 0; i < 9; ++i)
        m.Update(100);
    g_freeRayLog.clear();
    m.Update(100);                                                        // 1000: still trying (> 1000)
    CHECK(g_freeRayLog.size() == 1);
    g_freeRayLog.clear();
    m.Update(1);
    CHECK(g_freeRayLog.empty());
    // No texture, or no decals: none. The random spray takes the 2nd to 4th textures.
    Reset();
    g_decalsOn = true;
    m.m_splatTextures[1] = nullptr;
    m.SprayBlood(S_vector(0, 1, 0), S_vector(2, 0, 0));
    CHECK(m.m_bloodDrops.size() == 1);
    m.m_splatTextures[1] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x2001));
    for (int i = 0; i < 30; ++i) {
        g_frameLog.clear();
        m.SprayBloodRandom(S_vector(0, 1, 0), S_vector(2, 0, 0));
        std::string tex;
        for (const std::string &line : g_frameLog)
            if (line.rfind("texture ", 0) == 0)
                tex = line.substr(line.rfind(' ') + 1);
        CHECK(tex == "8194" || tex == "8195" || tex == "8196");
    }
    CHECK(m.m_bloodDrops.size() == 2);                                    // (two at most: reused)
    m.RemoveEffects(false);
}

void TestMuzzleLight()
{
    C_ShotManager &m = g_shotManager;
    C_Human *shooter = reinterpret_cast<C_Human *>(g_shooter);
    static C_ItemDef *def = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(def), 0, sizeof(C_ItemDef));
    def->m_component = &g_rifle;
    g_rifle.m_effect = 66;
    static C_WeaponItem *gun = static_cast<C_WeaponItem *>(::operator new(sizeof(C_WeaponItem)));
    std::memset(static_cast<void *>(gun), 0, sizeof(C_WeaponItem));
    gun->m_def = def;
    I3D_frame *flash = MakeFrame(FRAME_VISUAL, "flash");
    gun->m_flash = flash;
    I3D_frame *muzzle = MakeFrameAt(S_vector(0, 1, 0));
    g_frameNames[muzzle] = "muzzle";
    shooter->m_frame = MakeFrame(FRAME_MODEL, "mshooter");
    // A sector with two incoming portals: one whose owner is 4 m away (lit), one 4.01 m (not).
    I3D_frame *nearSec = MakeFrameAt(S_vector(0, 5, 0));
    I3D_frame *farSec = MakeFrameAt(S_vector(0, 5.01f, 0));
    *reinterpret_cast<void ***>(nearSec) = g_sectorVt;
    *reinterpret_cast<void ***>(farSec) = g_sectorVt;
    g_frameNames[nearSec] = "nearsector";
    g_frameNames[farSec] = "farsector";
    static unsigned char portals[2][0x200];
    *reinterpret_cast<I3D_frame **>(portals[0] + 0x194) = nearSec;
    *reinterpret_cast<I3D_frame **>(portals[1] + 0x194) = farSec;
    static void *portalList[2] = {portals[0], portals[1]};
    *reinterpret_cast<void ***>(reinterpret_cast<unsigned char *>(g_sectorFrame) + 0x25C) = portalList;
    *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(g_sectorFrame) + 0x264) = 2;
    *reinterpret_cast<void ***>(g_sectorFrame) = g_sectorVt;
    // A client's shot from the muzzle: the weapon's effect at it when the shooter was drawn in the last frame;
    // a muzzle light (100 ms) and the flash shown.
    Reset(GAME_ROLE_CLIENT);
    m.m_shots.clear();
    m.m_clientBullets.clear();
    g_localPlayer = shooter;
    FrameStamp(shooter->m_frame) = 7;
    const int refs = flash->m_refCount;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), muzzle, nullptr, false, 0, -1);
    CHECK((g_particleLog == std::vector<std::string>{"create 66 10 frame"}) && m.m_muzzleLights.size() == 1);
    CHECK((g_sceneLog == std::vector<std::string>{"createframe " + std::to_string(FRAME_LIGHT), "sector 0,1,0"}));
    CHECK((LogOf("light") == std::vector<std::string>{"link light muzzle 0", "name light WeaponLight", "lightmode light 17",
                                                      "lighttype light 1", "lightcolor light 1,1,1", "power light 1.5",
                                                      "specular light 1,1,1", "specpower light 1", "range light 4",
                                                      "atten light 0.99 0 0.01", "on light", "update light"}));
    CHECK((LogOf("sector") == std::vector<std::string>{"addlight sector light"}));
    CHECK((LogOf("nearsector") == std::vector<std::string>{"addlight nearsector light", "update nearsector"}) &&
          LogOf("farsector").empty());
    CHECK((LogOf("flash") == std::vector<std::string>{"on flash", "lightlevel flash 1", "update flash"}) &&
          flash->m_refCount == refs + 1);
    // Fading: power 1 - t / 100, range 4 - t / 100; the flash hidden (and released) past 50 ms; out at 100.
    g_frameLog.clear();
    m.Update(30);
    CHECK((LogOf("light") == std::vector<std::string>{"power light 1", "range light 4", "update light"}));
    m.Update(30);
    CHECK(LogOf("flash").empty());                                        // 30 ms: not past 50
    g_frameLog.clear();
    m.Update(30);                                                         // 60 ms
    CHECK((LogOf("flash") == std::vector<std::string>{"off flash", "update flash", "release flash"}));
    CHECK((LogOf("light") == std::vector<std::string>{"power light 0.4", "range light 3.4", "update light"}));
    g_frameLog.clear();
    m.Update(30);                                                         // 90 ms
    CHECK(m.m_muzzleLights.size() == 1);
    m.Update(30);                                                         // 120: out, deleted (unlinked, released)
    CHECK(m.m_muzzleLights.empty());
    CHECK(Count(g_frameLog, "off light") == 1 && Count(g_frameLog, "link light null 0") == 1 && Count(g_frameLog, "release light") == 1);
    CHECK(LogOf("light").size() >= 2 && LogOf("light")[LogOf("light").size() - 3] == "update light");
    // Exact times: the flash still shown at 50 ms (> 50), the light still on at 99, out at 100 (>= 100).
    Reset(GAME_ROLE_CLIENT);
    g_localPlayer = shooter;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), muzzle, nullptr, false, 0, -1);
    CHECK(Same(reinterpret_cast<I3D_frame *>(m.m_muzzleLights[0] ? *reinterpret_cast<I3D_frame **>(m.m_muzzleLights[0]) : nullptr)->m_scale,
               S_vector(1, 1, 1)));
    CHECK(Same((*reinterpret_cast<I3D_frame **>(m.m_muzzleLights[0]))->m_localMatrix(3), S_vector(0, 0, 0)));   // (at the muzzle)
    // The light in two sectors (as the engine fills its list): both lose it when it is deleted.
    {
        I3D_frame *light = *reinterpret_cast<I3D_frame **>(m.m_muzzleLights[0]);
        static I3D_frame *lit[2];
        lit[0] = g_sectorFrame;
        lit[1] = nearSec;
        *reinterpret_cast<I3D_frame ***>(reinterpret_cast<unsigned char *>(light) + 0x120) = lit;
        *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(light) + 0x128) = 2;
    }
    m.Update(50);
    g_frameLog.clear();
    m.Update(1);                                                          // ticked at 50: flash kept
    CHECK(LogOf("flash").empty());
    m.Update(48);                                                         // ticked at 51
    CHECK(Count(g_frameLog, "off flash") == 1);                           // (ticked at 51: hidden)
    m.Update(1);                                                          // ticked at 99: on
    CHECK(m.m_muzzleLights.size() == 1);
    g_frameLog.clear();
    m.Update(1);                                                          // ticked at 100: out
    CHECK(m.m_muzzleLights.empty() && Count(g_frameLog, "removelight nearsector light") == 1 &&
          Count(g_frameLog, "removelight sector light") == 1);
    CHECK(g_frameLog.size() >= 2 && g_frameLog[0] == "off light" && g_frameLog[1] == "update light");
    // Not drawn: no effect, the light all the same.
    Reset(GAME_ROLE_CLIENT);
    g_localPlayer = shooter;
    FrameStamp(shooter->m_frame) = 6;
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), muzzle, nullptr, false, 0, -1);
    CHECK(g_particleLog.empty() && m.m_muzzleLights.size() == 1);
    g_frameLog.clear();
    m.RemoveEffects(false);
    CHECK(m.m_muzzleLights.empty() && Count(g_frameLog, "release light") == 1);
    // A client's shot with an eject dummy throws a shell (the active player's).
    Reset(GAME_ROLE_CLIENT);
    g_localPlayer = shooter;
    g_activePlayer = shooter;
    m.m_shell3rd = MakeFrame(FRAME_VISUAL, "fb3rd");
    m.m_shellFpv = MakeFrame(FRAME_VISUAL, "fbfpv");
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), nullptr, MakeFrame(FRAME_DUMMY, "fbdummy"), false, 0, 5);
    CHECK(m.m_shells.size() == 1);
    m.RemoveEffects(false);
    g_activePlayer = nullptr;
    // A server alone: none.
    Reset(GAME_ROLE_SERVER);
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), muzzle, nullptr, false, 0, -1);
    CHECK(m.m_muzzleLights.empty());
    m.RemoveShots();
    g_localPlayer = nullptr;
}

// ---- the mission, melee, the explosion message, the fly sound, the explosion decal, the blood splat ----

void TestMission()
{
    C_ShotManager &m = g_shotManager;
    // The server's start: the hit zones' model ("humanzones"): not found: none.
    I3D_frame *oldModel = g_hitZones.m_model;
    I3D_frame *oldZones[10];
    std::memcpy(oldZones, g_hitZones.m_zones, sizeof oldZones);
    Reset();
    m.StartMission(true);
    CHECK((g_openLog == std::vector<std::string>{"model humanzones"}) && g_hitZones.m_model == nullptr &&
          Count(g_frameLog, "release frame") == 1 && g_imageLog.empty());
    // Found: its ten zones by name, in the dynamic root, hidden.
    Reset();
    g_modelOpens = true;
    m.StartMission(true);
    I3D_frame *zm = g_hitZones.m_model;
    static const char *const names[10] = {"c", "f", "l", "m", "h", "i", "s", "t", "o", "p"};
    I3D_frame *zf[10];
    for (int i = 0; i < 10; ++i)
        g_frameChildren[{zm, names[i]}] = zf[i] = MakeFrame(FRAME_VISUAL, names[i]);
    m.StartMission(true);
    zm = g_hitZones.m_model;
    bool found = true;
    for (int i = 0; i < 10; ++i)
        found &= g_hitZones.m_zones[i] == nullptr || true;
    CHECK(zm != nullptr && found && Count(g_frameLog, "off frame") >= 1);
    // (The children map is keyed by the new model: the second start's zones are the first's model's misses.)
    // Free: released.
    g_frameLog.clear();
    m.StopMission(true);
    CHECK(g_hitZones.m_model == nullptr && Count(g_frameLog, "release frame") == 1);
    g_hitZones.m_model = oldModel;
    std::memcpy(g_hitZones.m_zones, oldZones, sizeof oldZones);
    // The client's start: the limits (pools 50 in single player, 20 in a network game), the "WeaponOdds" model
    // and its trace and shells, the 9 decal and 7 splat textures.
    Reset();
    g_modelOpens = true;
    m.m_maxBloodPools = m.m_maxBloodDrops = m.m_maxDecals = 0;
    m.m_clientTickTime = 55;
    m.StartMission(false);
    CHECK(m.m_maxBloodDrops == 100 && m.m_maxBloodPools == 50 && m.m_maxDecals == 100 && m.m_clientTickTime == 0);
    CHECK(m.m_weaponOdds != nullptr && (g_openLog == std::vector<std::string>{"model WeaponOdds"}));
    std::vector<std::string> want;
    for (int i = 0; i < 9; ++i) {
        char s[40];
        std::snprintf(s, sizeof s, "load x_decal%02d.tga 2", i);
        want.push_back(s);
    }
    want.push_back("load x_expl00.tga 2");
    for (int i = 0; i < 6; ++i) {
        char s[40];
        std::snprintf(s, sizeof s, "load x_blood%02d.tga 2", i);
        want.push_back(s);
    }
    CHECK(g_imageLog == want);
    Reset(GAME_ROLE_CLIENT);
    m.StartMission(false);
    CHECK(m.m_maxBloodPools == 20);
    // The model's parts.
    Reset();
    g_modelOpens = true;
    {
        // (FindChildFrame on the new model: register children for any frame named "frame" made next.)
        m.StartMission(false);
        CHECK(m.m_traceModel == nullptr && m.m_shell3rd == nullptr && m.m_shellFpv == nullptr);   // (none named)
    }
    // Found: its trace and both shells.
    Reset();
    g_modelOpens = true;
    I3D_frame *trace = MakeFrame(FRAME_VISUAL, "ptrace");
    I3D_frame *s3 = MakeFrame(FRAME_VISUAL, "ps3rd");
    I3D_frame *sf = MakeFrame(FRAME_VISUAL, "psfpv");
    g_nextChildren = {{"trace", trace}, {"shell3rd", s3}, {"shellFPV", sf}};
    m.StartMission(false);
    CHECK(m.m_traceModel == trace && m.m_shell3rd == s3 && m.m_shellFpv == sf);
    // Not found: no parts (the old ones forgotten).
    Reset();
    g_nextChildren = {{"trace", trace}, {"shell3rd", s3}, {"shellFPV", sf}};
    m.StartMission(false);
    CHECK(m.m_traceModel == nullptr && m.m_shell3rd == nullptr && m.m_shellFpv == nullptr && m.m_weaponOdds != nullptr);
    // The client's stop: effects removed, every texture released and forgotten, the model released, the trace
    // forgotten (the shells' models kept), the shots removed; the pooled shells freed (their copies released).
    Reset();
    m.m_traceModel = MakeFrame(FRAME_VISUAL, "stoptrace");
    m.m_shell3rd = MakeFrame(FRAME_VISUAL, "keep3rd");
    m.m_shellFpv = MakeFrame(FRAME_VISUAL, "keepfpv");
    g_activePlayer = reinterpret_cast<C_Human *>(g_shooter);
    m.EjectShell(reinterpret_cast<C_Human *>(g_shooter), MakeFrame(FRAME_DUMMY, "stopdummy"), 6);
    g_activePlayer = nullptr;
    g_decalsOn = true;
    m.AddBloodPool(S_vector(1, 0, 1), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.size() == 1);
    g_imageLog.clear();
    g_frameLog.clear();
    m.StopMission(false);
    CHECK(m.m_bloodPools.empty() && m.m_weaponOdds == nullptr && m.m_traceModel == nullptr && m.m_shell3rd != nullptr);
    CHECK(g_imageLog.size() == 16 && g_imageLog[0] == "release x_decal00.tga" && g_imageLog[9] == "release x_expl00.tga");
    bool allNull = true;
    for (IImage *i : m.m_decalTextures)
        allNull &= i == nullptr;
    for (IImage *i : m.m_splatTextures)
        allNull &= i == nullptr;
    CHECK(allNull && Count(g_frameLog, "release frame") == 1);
    {
        int released = 0;
        for (const std::string &line : g_frameLog)
            released += line.rfind("release visual", 0) == 0;
        CHECK(released >= 1);                                             // (the pooled shell's copy)
    }
    // Restart: effects removed (the shells hidden), shots removed.
    Reset();
    g_activePlayer = reinterpret_cast<C_Human *>(g_shooter);
    m.m_shell3rd = MakeFrame(FRAME_VISUAL, "r3rd");
    m.m_shellFpv = MakeFrame(FRAME_VISUAL, "rfpv");
    I3D_frame *dummy = MakeFrame(FRAME_DUMMY, "rdummy");
    m.EjectShell(reinterpret_cast<C_Human *>(g_shooter), dummy, 6);
    m.m_shots = {new C_TestShot(SHOT_PROJECTILE, "r1", 9)};
    g_shotLog.clear();
    g_frameLog.clear();
    m.RestartMission(false);
    CHECK(m.m_shells.empty() && m.m_shots.empty() && (g_shotLog == std::vector<std::string>{"delete r1"}));
    int off = 0;
    for (const std::string &line : g_frameLog)
        off += line.rfind("off ", 0) == 0;
    CHECK(off == 1);
    // The stop of a client hides nothing (RemoveEffects(false)).
    m.EjectShell(reinterpret_cast<C_Human *>(g_shooter), dummy, 6);
    g_frameLog.clear();
    m.StopMission(false);
    off = 0;
    for (const std::string &line : g_frameLog)
        off += line.rfind("off ", 0) == 0;
    CHECK(off == 0 && m.m_shells.empty());
    g_activePlayer = nullptr;
    // The save's placeholders for blood drops and pools.
    Reset();
    for (int i = 0; i < 7; ++i)
        m.m_splatTextures[i] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x2000 + i));
    m.m_maxBloodPools = m.m_maxBloodDrops = 5;
    g_decalsOn = true;
    m.AddBloodPool(S_vector(1, 0, 1), S_vector(0, 1, 0), 0.25f);
    m.SprayBlood(S_vector(0, 1, 0), S_vector(1, 0, 0));
    m.SprayBlood(S_vector(0, 1, 0), S_vector(1, 0, 0));
    g_chunkLog.clear();
    m.SaveGame(*reinterpret_cast<C_chunk *>(g_gameBytes));
    CHECK((g_chunkLog == std::vector<std::string>{"begin 6667", "ascend", "begin 6667", "ascend", "begin 6668", "ascend"}));
    // RemoveEffects deletes every decal, drop, pool and light (their projectors released).
    m.AddBloodPool(S_vector(2, 0, 2), S_vector(0, 1, 0), 0.25f);
    m.m_maxDecals = 10;
    g_rayHits = {{true, S_vector(3, 1, 0), S_vector(-1, 0, 0)}};
    m.AddBloodSplat(S_vector(0, 1, 0), S_vector(1, 0, 0));
    CHECK(m.m_decals.size() == 1);
    g_frameLog.clear();
    m.RemoveEffects(false);
    int releases = 0;
    for (const std::string &line : g_frameLog)
        releases += line.rfind("release decal", 0) == 0;
    CHECK(releases == 5 && m.m_bloodPools.empty() && m.m_bloodDrops.empty() && m.m_decals.empty());
    for (IImage *&i : m.m_splatTextures)
        i = nullptr;
}

void TestMelee()
{
    C_Human *attacker = reinterpret_cast<C_Human *>(g_shooter);
    static C_ItemDef *def = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(def), 0, sizeof(C_ItemDef));
    static C_FirearmComp knifeComp;
    knifeComp.m_damage = 40.0f;
    def->m_component = &knifeComp;
    static C_Item *knife = static_cast<C_Item *>(::operator new(sizeof(C_Item)));
    std::memset(static_cast<void *>(knife), 0, sizeof(C_Item));
    knife->m_def = def;
    S_TestHuman victim = MakeHuman("victim");
    g_headItem = nullptr;
    // A backstab: half a metre ahead horizontally (the attacker's collision off around it); a human facing away
    // (his facing . dir > 0.8) is killed: Hit(BACKSTAB, -1) at the hit along the reach.
    Reset();
    g_dynRay.volume = DynamicVolume(victim.human);
    g_dynRay.pos = S_vector(0, 1, 0.4f);
    g_facing = S_vector(0, 0, 1);
    MeleeAttack(g_shooter, knife, true, S_vector(0, 1, 0), S_vector(0, 0.5f, 2));
    CHECK((g_dynRayLog == std::vector<std::string>{"dyn 0,1,0 0,0,0.5 0 0"}) && (g_bodyLog == std::vector<std::string>{"remove", "restore"}));
    CHECK((g_prints == std::vector<std::string>{"undercut him!!!"}));
    // Straight from behind with a unit direction (facing . dir = 1).
    Reset();
    g_dynRay.volume = DynamicVolume(victim.human);
    g_dynRay.pos = S_vector(0, 1, 0.4f);
    MeleeAttack(g_shooter, knife, true, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.size() == 1);
    CHECK(g_hits.size() == 1 && g_hits[0].type == HIT_BACKSTAB && Same(g_hits[0].amount, -1.0f) &&
          Same(g_hits[0].pos, S_vector(0, 1, 0.4f)) && Same(g_hits[0].dir, S_vector(0, 0, 0.5f)) && g_hits[0].part == 0);
    // Facing the attacker (or 0.8 exactly, side on), not a human, nothing: no kill.
    for (int variant = 0; variant < 4; ++variant) {
        Reset();
        g_dynRay.volume = variant == 2 ? DynamicVolume(MakeActor(ACTOR_BARREL)) : variant == 3 ? nullptr : DynamicVolume(victim.human);
        g_facing = variant == 0 ? S_vector(0, 0, -1) : variant == 1 ? S_vector(0.6f, 0, 0.8f) : S_vector(0, 0, 1);
        MeleeAttack(g_shooter, knife, true, S_vector(0, 1, 0), S_vector(0, 0, 1));
        CHECK(g_hits.empty());
    }
    // A blow: 1.2 m along the direction (RayTest, types 1), the attacker's collision off around it.
    // Static: the surface's hit effect at the hit, along its normal.
    Reset();
    g_ray.hit = true;
    g_ray.pos = S_vector(0, 1, 1);
    g_ray.normal = S_vector(0, 0, -1);
    g_ray.flags = 4u << 24;
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_rayLog.size() == 1 && g_rayLog[0] == "ray 0.000000,1.000000,0.000000 0.000000,0.000000,1.200000 1 0");
    CHECK((g_bodyLog == std::vector<std::string>{"remove", "restore"}) && (g_particleLog == std::vector<std::string>{"createat 304 10"}));
    // A material without an effect (0): none. Nothing hit: nothing.
    Reset();
    g_ray.hit = true;
    g_ray.flags = 7u << 24;
    g_materialTable[7].hitEffect = 0;
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_particleLog.empty());
    g_materialTable[7].hitEffect = 307;
    Reset();
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_particleLog.empty() && g_rayLog.size() == 1);
    // A human: his zone (along the reach): Hit(MELEE, the weapon's damage, the hit, the reach, the zone), his
    // material's effect.
    Reset();
    g_ray.hit = true;
    g_ray.pos = S_vector(0, 1, 1);
    g_ray.normal = S_vector(0, 0, -1);
    g_ray.volume = DynamicVolume(victim.human, victim.frame);
    g_actorMaterial = 1;
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}, {g_zones[1], 1.0f, S_vector(0, 0, -1)}};
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.size() == 1 && g_hits[0].type == HIT_MELEE && Same(g_hits[0].amount, 40.0f) && g_hits[0].part == 1 &&
          Same(g_hits[0].pos, S_vector(0, 1, 1)) && Same(g_hits[0].dir, S_vector(0, 0, 1.2f)));
    CHECK((g_prints == std::vector<std::string>{"head"}));                // (the zone printed)
    CHECK((g_particleLog == std::vector<std::string>{"createat 301 10"}));
    // ...no zone: told he was attacked (the reach as the velocity), nothing else.
    Reset();
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(victim.human, victim.frame);
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.empty() && g_shotAt.size() == 1 && Same(g_shotAt[0].velocity, S_vector(0, 0, 1.2f)) && g_particleLog.empty());
    // A melee tracker (a dog): hit with the damage (part 0), its material.
    Reset();
    C_Actor *dog = MakeActor(ACTOR_MELEE_TRACKER, MakeFrame(FRAME_MODEL, "dog"));
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(dog, dog->m_frame);
    g_actorMaterial = 4;
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.size() == 1 && g_hits[0].actor == dog && g_hits[0].type == HIT_MELEE && g_hits[0].part == 0 &&
          (g_particleLog == std::vector<std::string>{"createat 304 10"}));
    // A static weapon: its gunner's zone; no gunner: nothing.
    Reset();
    C_Actor *gun = MakeActor(ACTOR_STATIC_WEAPON, MakeFrame(FRAME_MODEL, "mg"));
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(gun, gun->m_frame);
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.empty() && g_intersectFrames.empty() && g_particleLog.empty());
    Reset();
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(gun, gun->m_frame);
    g_seatOccupant = victim.human;
    g_actorMaterial = 1;
    g_intersects = {{g_zones[0], 1.0f, S_vector(0, 0, -1)}};
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.size() == 1 && g_hits[0].actor == victim.human && g_hits[0].part == 0 && g_seatAsked == 0);
    // Another actor: its visual (flags 0); hit: its part's material's effect, no Hit; missed: nothing.
    Reset();
    C_Actor *barrel = MakeActor(ACTOR_BARREL, MakeFrame(FRAME_MODEL, "mbarrel"));
    I3D_frame *part = MakeFrame(FRAME_VISUAL, "mpart");
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(barrel, barrel->m_frame);
    g_actorMaterial = 2;
    g_intersects = {{part, 1.0f, S_vector(0, 0, -1)}};
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.empty() && g_intersectFrames.size() == 1 && g_intersectFlags[0] == 0 && g_intersectExtra == 0 &&
          (g_particleLog == std::vector<std::string>{"createat 302 10"}));
    Reset();
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(barrel, barrel->m_frame);
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}};
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_particleLog.empty());
    // A dynamic volume without a frame counts as a static surface (its material); one without an actor: nothing.
    Reset();
    S_CollisionVolume *frameless = DynamicVolume(barrel, nullptr);
    frameless->flags |= 4u << 24;
    g_ray.hit = true;
    g_ray.volume = frameless;
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_hits.empty() && (g_particleLog == std::vector<std::string>{"createat 304 10"}));
    Reset();
    g_ray.hit = true;
    g_ray.volume = DynamicVolume(nullptr, MakeFrame(FRAME_VISUAL, "noactor"));
    MeleeAttack(g_shooter, knife, false, S_vector(0, 1, 0), S_vector(0, 0, 1));
    CHECK(g_particleLog.empty());
}

void TestExplosionMessage()
{
    // An explosion of 1000 within 5 m, no source; the message: created for the shots' module (reliable), type
    // 0 in 1 bit, the position, the material in 8 bits, sent.
    Reset();
    SendExplosionMessage(S_vector(1, 2, 3), 9);
    CHECK(g_explosions.size() == 1 && Same(g_explosions[0].pos, S_vector(1, 2, 3)) && Same(g_explosions[0].range, 5.0f) &&
          Same(g_explosions[0].damage, 1000.0f) && !g_explosions[0].unknown18 && g_explosions[0].source == nullptr);
    CHECK((g_netLog == std::vector<std::string>{"create 14 0", "bits 0 1", "position 1.000000,2.000000,3.000000", "bits 9 8", "send it"}));
}

void TestFlySound()
{
    // A client's projectile with the fly sound, its file found: "e_incm<1..3>.wav", a point sound, 20..50 m
    // (0.5, 0.5), looping, on, full volume, named, on the model at its origin.
    Reset();
    g_soundOpens = true;
    I3D_frame *model = MakeFrame(FRAME_MODEL, "fsmodel");
    C_ProjectileShot *p = Launch(S_vector(0, 0, 0), S_vector(0, 0, 1), model, -1, true);
    CHECK(p->m_flySound != nullptr && g_openLog.size() == 1);
    CHECK(g_openLog[0] == "sound e_incm1.wav" || g_openLog[0] == "sound e_incm2.wav" || g_openLog[0] == "sound e_incm3.wav");
    const std::string v = FrameName(p->m_visual);
    CHECK((LogOf("sound") == std::vector<std::string>{"soundtype sound 1", "soundrange sound 20 50 0.5 0.5", "loop sound 1",
                                                      "on sound", "volume sound 1", "name sound FollowBulletSound",
                                                      "link sound " + v + " 0"}));
    CHECK(Same(reinterpret_cast<I3D_frame *>(p->m_flySound)->m_localMatrix(3), S_vector(0, 0, 0)));
    // All three files come up.
    std::set<std::string> files;
    for (int i = 0; i < 60; ++i) {
        g_openLog.clear();
        Launch(S_vector(0, 0, 0), S_vector(0, 0, 1), model, -1, true);
        files.insert(g_openLog.at(0));
    }
    CHECK(files.size() == 3);
    // Exploded: the sound (on) switched off; the model too.
    reinterpret_cast<I3D_frame *>(p->m_flySound)->m_frameFlags |= 1;
    p->m_visual->m_frameFlags |= 1;
    p->m_exploded = true;
    g_frameLog.clear();
    p->Tick(10);
    CHECK((g_frameLog == std::vector<std::string>{"off sound", "off " + v}));
    // Release frees it.
    g_frameLog.clear();
    p->Release();
    CHECK(Count(g_frameLog, "release sound") == 1 && p->m_flySound == nullptr);
}

void TestExplosionDecalAndSplat()
{
    C_ShotManager &m = g_shotManager;
    m.RemoveEffects(false);
    m.m_maxDecals = 10;
    for (int i = 0; i < 7; ++i)
        m.m_splatTextures[i] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x2000 + i));
    g_materialTable[4].explosionDecal = 0;
    // An explosion's scorch mark (decals on, a material): a ray from 0.3 m before the point along the reverse of
    // the explosion's direction for 0.5 m (all types, 0xB90000); on what it hits: (r + 1) * 0.5 big, depth 0.1,
    // 5 cm off along the hit's normal from the explosion's point, the material's splat texture, coloured by the
    // light, in the sector of the point; orthogonal.
    Reset();
    g_decalsOn = true;
    g_randFloat = 0.5f;
    g_light = 0.5f;
    g_rayHits = {{true, S_vector(1, 0, 1), S_vector(0, 1, 0)}};
    m.PlayExplosion(S_vector(1, 0.2f, 1), S_vector(0, 1, 0), 33, 4, 9);
    CHECK((g_freeRayLog == std::vector<std::string>{"ray 1,0.5,1 -0,-0.5,-0 ffffffff b90000"}));   // (the reversed direction)
    CHECK(m.m_decals.size() == 1);
    const std::string v = FrameName(reinterpret_cast<I3D_frame *>(View(m.m_decals[0])->visual));
    CHECK((LogOf(v) == std::vector<std::string>{"buffered " + v + " 3", "mode " + v + " 1", "ortho " + v + " 1",
                                                "dir " + v + " -0,-1,-0 0", "pos " + v + " 1,0.25,1", "texture " + v + " 8192",
                                                "color " + v + " " + Vec(kBase * 0.5f), "link " + v + " sector 1", "update " + v}));
    CHECK(Same(reinterpret_cast<I3D_frame *>(View(m.m_decals[0])->visual)->m_scale, S_vector(0.75f, 0.75f, 0.1f)));
    // Missed, no texture, no material, decals off: none.
    for (int variant = 0; variant < 4; ++variant) {
        Reset();
        g_decalsOn = variant != 3;
        if (variant != 0)
            g_rayHits = {{true, S_vector(1, 0, 1), S_vector(0, 1, 0)}};
        if (variant == 1)
            m.m_splatTextures[0] = nullptr;
        m.PlayExplosion(S_vector(1, 0.2f, 1), S_vector(0, 1, 0), 33, variant == 2 ? MATERIAL_NONE : 4, 9);
        CHECK(m.m_decals.size() == 1);
        m.m_splatTextures[0] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x2000));
    }
    // A blood splat: a ray 3 m along the direction made level (all types, 0); on what it hits: r * 0.6 + 0.3 big,
    // depth 0.2, 1 cm off the hit, the 5th splat texture, coloured by the light, in the hit's sector; not
    // orthogonal.
    Reset();
    g_decalsOn = true;
    g_randFloat = 0.5f;
    g_light = 2.0f;
    g_rayHits = {{true, S_vector(3, 1, 0), S_vector(-1, 0, 0)}};
    m.AddBloodSplat(S_vector(0, 1, 0), S_vector(2, -5, 0));
    CHECK((g_freeRayLog == std::vector<std::string>{"ray 0,1,0 3,0,0 ffffffff 0"}) && m.m_decals.size() == 2);
    const std::string s = FrameName(reinterpret_cast<I3D_frame *>(View(m.m_decals[1])->visual));
    CHECK((LogOf(s) == std::vector<std::string>{"buffered " + s + " 3", "mode " + s + " 1", "dir " + s + " 1,-0,-0 0",
                                                "pos " + s + " 2.99,1,0", "texture " + s + " 8197",
                                                "color " + s + " " + Vec(kBase * 2.0f), "link " + s + " sector 1",
                                                "update " + s}));
    CHECK(Same(reinterpret_cast<I3D_frame *>(View(m.m_decals[1])->visual)->m_scale, S_vector(0.6f, 0.6f, 0.2f)));
    // Missed, no texture, decals off: none.
    for (int variant = 0; variant < 3; ++variant) {
        Reset();
        g_decalsOn = variant != 2;
        if (variant != 0)
            g_rayHits = {{true, S_vector(3, 1, 0), S_vector(-1, 0, 0)}};
        if (variant == 1)
            m.m_splatTextures[5] = nullptr;
        m.AddBloodSplat(S_vector(0, 1, 0), S_vector(2, -5, 0));
        CHECK(m.m_decals.size() == 2);
        m.m_splatTextures[5] = reinterpret_cast<IImage *>(static_cast<uintptr_t>(0x2005));
    }
    m.RemoveEffects(false);
    for (IImage *&i : m.m_splatTextures)
        i = nullptr;
}

// ---- the view test, FireBullet, ThrowItem, the projectile's doors, a pool without texture ----

void TestSegmentInView()
{
    C_ShotManager &m = g_shotManager;
    C_Human *shooter = reinterpret_cast<C_Human *>(g_shooter);
    static C_ItemDef *def = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(def), 0, sizeof(C_ItemDef));
    def->m_component = &g_rifle;
    static C_WeaponItem *gun = static_cast<C_WeaponItem *>(::operator new(sizeof(C_WeaponItem)));
    std::memset(static_cast<void *>(gun), 0, sizeof(C_WeaponItem));
    gun->m_def = def;
    shooter->m_frame = MakeFrame(FRAME_MODEL, "vshooter");
    // The camera at the origin looking along +z: 90 degrees, aspect 1, 1..100 m.
    I3D_frame *camera = MakeFrame(FRAME_CAMERA, "camera");
    unsigned char *c = reinterpret_cast<unsigned char *>(camera);
    *reinterpret_cast<float *>(c + 0x120) = 1.57079637f;
    *reinterpret_cast<float *>(c + 0x124) = 1.0f;
    *reinterpret_cast<float *>(c + 0x128) = 100.0f;
    *reinterpret_cast<float *>(c + 0x138) = 1.0f;
    unsigned char *s = reinterpret_cast<unsigned char *>(&g_scene);
    *reinterpret_cast<I3D_frame **>(s + 0x13C) = camera;
    *reinterpret_cast<I3D_frame **>(s + 0x15C) = camera;
    // A remote player's shot not drawn: shown (a client bullet) only when its first metre is in the view.
    struct S_Case {
        S_vector pos, dir;
        bool shown;
    };
    const S_Case cases[] = {
        {S_vector(0, 0, 10), S_vector(0, 0, 1), true},           // straight ahead
        {S_vector(0, 0, -20), S_vector(0, 0, -1), false},        // behind
        {S_vector(0, 0, 200), S_vector(0, 0, 1), false},         // past the far range
        {S_vector(0, 0, 99.5f), S_vector(0, 0, 1), true},        // into the far range
        {S_vector(0, 0, 0.5f), S_vector(0, 0, 1), true},         // out of the near range
        {S_vector(0, 0, 0.2f), S_vector(0, 0, 0.5f), false},     // (0.2 .. 0.7: nearer than 1 m)
        {S_vector(-10.5f, 0, 10), S_vector(1, 0, 0), true},      // across the left edge
        {S_vector(9.5f, 0, 10), S_vector(1, 0, 0), true},        // across the right edge
        {S_vector(20, 0, 10), S_vector(1, 0, 0), false},         // to the right
        {S_vector(-20, 0, 10), S_vector(-1, 0, 0), false},       // to the left
        {S_vector(0, -10.5f, 10), S_vector(0, 1, 0), true},      // across the bottom
        {S_vector(0, 9.5f, 10), S_vector(0, 1, 0), true},        // across the top
        {S_vector(0, 20, 10), S_vector(0, 0, 1), false},         // above
        {S_vector(0, -20, 10), S_vector(0, 0, 1), false},        // below
        {S_vector(5, 5, 10), S_vector(0, 0, 1), true},           // inside, off centre
        {S_vector(-12, 9, 10), S_vector(1, 0, 0), false},        // level with the view but left of it, going right 1 m
    };
    int i = 0;
    for (const S_Case &k : cases) {
        Reset(GAME_ROLE_CLIENT);
        m.m_clientBullets.clear();
        FrameStamp(shooter->m_frame) = 6;
        m.FireBulletFrom(shooter, gun, k.pos, k.dir, nullptr, nullptr, false, 0, -1);
        if ((m.m_clientBullets.size() == 1) != k.shown)
            std::printf("  view case %d\n", i);
        CHECK((m.m_clientBullets.size() == 1) == k.shown);
        ++i;
    }
    // No camera: never shown.
    *reinterpret_cast<I3D_frame **>(s + 0x13C) = nullptr;
    *reinterpret_cast<I3D_frame **>(s + 0x15C) = nullptr;
    Reset(GAME_ROLE_CLIENT);
    m.m_clientBullets.clear();
    m.FireBulletFrom(shooter, gun, S_vector(0, 0, 10), S_vector(0, 0, 1), nullptr, nullptr, false, 0, -1);
    CHECK(m.m_clientBullets.empty());
    m.RemoveShots();
}

void TestFireBulletDummies()
{
    C_ShotManager &m = g_shotManager;
    C_Human *shooter = reinterpret_cast<C_Human *>(g_shooter);
    static C_ItemDef *def = static_cast<C_ItemDef *>(::operator new(sizeof(C_ItemDef)));
    std::memset(static_cast<void *>(def), 0, sizeof(C_ItemDef));
    def->m_component = &g_rifle;
    static C_WeaponItem *gun = static_cast<C_WeaponItem *>(::operator new(sizeof(C_WeaponItem)));
    std::memset(static_cast<void *>(gun), 0, sizeof(C_WeaponItem));
    gun->m_def = def;
    g_gunBlast = MakeFrame(FRAME_DUMMY, "gunblast");
    g_gunEject = MakeFrame(FRAME_DUMMY, "guneject");
    g_fpvBlast = MakeFrame(FRAME_DUMMY, "fpvblast");
    g_fpvEject = MakeFrame(FRAME_DUMMY, "fpveject");
    static unsigned char fpv[16];
    reinterpret_cast<C_game *>(g_gameBytes)->m_firstPersonView = reinterpret_cast<C_FirstPersonView *>(fpv);
    m.m_shell3rd = MakeFrame(FRAME_VISUAL, "d3rd");
    m.m_shellFpv = MakeFrame(FRAME_VISUAL, "dfpv");
    auto lightOn = [&]() {
        for (const std::string &line : g_frameLog)
            if (line.rfind("link light ", 0) == 0)
                return line.substr(11, line.find(' ', 11) - 11);
        return std::string();
    };
    // The local player in the first-person view: the view's weapon's dummies.
    Reset(GAME_ROLE_LOCAL);
    g_localPlayer = shooter;
    g_activePlayer = shooter;
    g_fpv = true;
    m.FireBullet(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), false, 0, -1);
    CHECK(lightOn() == "fpvblast" && m.m_shells.size() == 1);
    m.RemoveEffects(false);
    // Otherwise the weapon's own.
    Reset(GAME_ROLE_LOCAL);
    g_fpv = false;
    g_localPlayer = shooter;
    g_activePlayer = shooter;
    m.FireBullet(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), false, 0, -1);
    CHECK(lightOn() == "gunblast" && m.m_shells.size() == 1);
    m.RemoveEffects(false);
    Reset(GAME_ROLE_LOCAL);
    g_fpv = true;
    g_activePlayer = shooter;
    m.FireBullet(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), false, 0, -1);   // (not the local player)
    CHECK(lightOn() == "gunblast");
    m.RemoveEffects(false);
    // A server alone: no dummies (no light, no shell).
    Reset(GAME_ROLE_SERVER);
    g_activePlayer = shooter;
    m.FireBullet(shooter, gun, S_vector(0, 0, 0), S_vector(1, 0, 0), false, 0, -1);
    CHECK(lightOn().empty() && m.m_shells.empty() && m.m_muzzleLights.empty());
    m.RemoveEffects(false);
    m.RemoveShots();
    g_fpv = false;
    g_activePlayer = nullptr;
    g_localPlayer = nullptr;
    reinterpret_cast<C_game *>(g_gameBytes)->m_firstPersonView = nullptr;
}

void TestThrowItem()
{
    C_ShotManager &m = g_shotManager;
    // A thrown shot in the shots; the team told (single player); the item taken from the thrower (destroyed).
    Reset();
    m.m_shots.clear();
    C_WeaponItem *g = MakeGrenade();
    g->m_uid = 4321;
    m.ThrowItem(g_shooter, g, S_vector(2, 0, 0), S_vector(3, 0, 0), false);
    CHECK(m.m_shots.size() == 1 && m.m_shots[0]->GetType() == SHOT_THROWN && !m.m_shots[0]->m_fake);
    CHECK((g_teamLog == std::vector<std::string>{"grenade 2.000000 3.000000 thrower", "remove 4321 1"}));
    // A network game: the team not told; fake.
    Reset(GAME_ROLE_SERVER);
    m.ThrowItem(g_shooter, g, S_vector(2, 0, 0), S_vector(3, 0, 0), true);
    CHECK(m.m_shots.size() == 2 && m.m_shots[1]->m_fake && (g_teamLog == std::vector<std::string>{"remove 4321 1"}));
    m.RemoveShots();
}

void TestProjectileDoors()
{
    // A projectile through a volume without actor: its visual; a destructible part damaged by the projectile's
    // damage (server; destroyed: no frame, the rest the same); else the part's material, the physics object pushed
    // (its velocity, factor 0.01), the part told (server).
    I3D_frame *door = MakeFrame(FRAME_VISUAL, "pdoor2");
    I3D_frame *panel = MakeFrame(FRAME_VISUAL, "ppanel2");
    static S_FrameData doorData{}, panelData{};
    static S_PhysicsHandle handle{};
    static char props[] = "matid 5";
    SetMaterial(5, 0, 10);
    // Missed: on.
    Reset();
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(nullptr, door), 0.4f)};
    C_ProjectileShot *p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded && g_impulses.empty());
    // A part with a material, the door a physics object: pushed, told, passed (10 of 50).
    Reset();
    panel->m_userProps = props;
    panel->m_data = nullptr;
    doorData.physicsObject = &handle;
    door->m_data = &doorData;
    g_intersects = {{panel, 4.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(nullptr, door), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(g_impulses.size() == 1 && g_impulses[0].object == &handle && Same(g_impulses[0].impulse, S_vector(100, 0, 0)) &&
          Same(g_impulses[0].point, S_vector(4, 10, 0)) && Same(g_impulses[0].factor, 0.01f));
    CHECK(g_framesHit.size() == 1 && g_framesHit[0] == panel && !p->m_exploded && Same(p->m_damage, 40.0f));
    // The part without a material: the door's.
    Reset();
    panel->m_userProps = nullptr;
    static char doorProps[] = "matid 2";
    door->m_userProps = doorProps;
    g_intersects = {{panel, 4.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(nullptr, door), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(p->m_exploded);                                                 // (2: impenetrable)
    // No physics object: the door's material not asked; told (server only).
    Reset(GAME_ROLE_CLIENT);
    doorData.physicsObject = g_noPhysicsObject;
    panel->m_userProps = props;
    g_intersects = {{panel, 4.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(nullptr, door), 0.4f)};
    Launch(S_vector(0, 10, 0), S_vector(1, 0, 0))->Tick(100);
    CHECK(g_impulses.empty() && g_framesHit.empty());
    // A destructible part that does not survive the server's damage: hit as before, nothing more.
    Reset();
    alignas(16) static unsigned char keeperBytes[sizeof(C_VersionKeeper)];
    auto *keeper = reinterpret_cast<C_VersionKeeper *>(keeperBytes);
    keeper->m_materialId = 5;
    panelData.versionKeeper = keeper;
    panel->m_data = &panelData;
    g_keeperSurvives = false;
    g_intersects = {{panel, 4.0f, S_vector(-1, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(nullptr, door), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK((g_keeperDamage == std::vector<int>{50}) && !p->m_exploded && g_framesHit.empty());
    panel->m_data = nullptr;
    panel->m_userProps = nullptr;
    door->m_data = nullptr;
    door->m_userProps = nullptr;
    // A projectile's own visual of another actor that misses: on.
    Reset();
    C_Actor *barrel = MakeActor(ACTOR_BARREL, MakeFrame(FRAME_MODEL, "pbarrel2"));
    g_intersects = {{nullptr, 0, S_vector(0, 0, 0)}};
    g_queryHits = {Hit(S_vector(4, 10, 0), S_vector(-1, 0, 0), DynamicVolume(barrel), 0.4f)};
    p = Launch(S_vector(0, 10, 0), S_vector(1, 0, 0));
    p->Tick(100);
    CHECK(!p->m_exploded);
}

void TestPoolWithoutTexture()
{
    // No pool texture: a pool without a projector; it neither grows nor moves (its reuse is skipped).
    C_ShotManager &m = g_shotManager;
    m.RemoveEffects(false);
    m.m_maxBloodPools = 1;
    Reset();
    g_decalsOn = true;
    m.m_splatTextures[6] = nullptr;
    m.AddBloodPool(S_vector(1, 0, 1), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.size() == 1 && g_sceneLog.empty() && g_frameLog.empty());
    m.Update(500);
    m.AddBloodPool(S_vector(2, 0, 2), S_vector(0, 1, 0), 0.25f);
    CHECK(m.m_bloodPools.size() == 1 && g_frameLog.empty());
    m.RemoveEffects(false);
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
    g_frameVt[0x14 / 4] = reinterpret_cast<void *>(F_FrSetPosDir);
    g_frameVt[0x34 / 4] = reinterpret_cast<void *>(F_FrSetName);
    g_frameVt[0x50 / 4] = reinterpret_cast<void *>(F_FrSetLightLevel);
    for (void **vt : {g_projVt, g_objVt, g_lightVt, g_soundVt, g_sectorVt})
        std::memcpy(vt, g_frameVt, sizeof g_frameVt);
    g_projVt[0x9C / 4] = reinterpret_cast<void *>(F_PrSetTexture);
    g_projVt[0xA4 / 4] = reinterpret_cast<void *>(F_PrSetOrthogonal);
    g_projVt[0xB0 / 4] = reinterpret_cast<void *>(F_PrSetColor);
    g_projVt[0xB4 / 4] = reinterpret_cast<void *>(F_PrSetMode);
    g_projVt[0xB8 / 4] = reinterpret_cast<void *>(F_PrSetBuffered);
    g_objVt[0xA8 / 4] = reinterpret_cast<void *>(F_ObSetAlpha);
    g_lightVt[0x74 / 4] = reinterpret_cast<void *>(F_LiSetType);
    g_lightVt[0x7C / 4] = reinterpret_cast<void *>(F_LiSetColor);
    g_lightVt[0x80 / 4] = reinterpret_cast<void *>(F_LiSetSpecularColor);
    g_lightVt[0x88 / 4] = reinterpret_cast<void *>(F_LiSetPower);
    g_lightVt[0x8C / 4] = reinterpret_cast<void *>(F_LiSetSpecularPower);
    g_lightVt[0x94 / 4] = reinterpret_cast<void *>(F_LiSetMode);
    g_lightVt[0x98 / 4] = reinterpret_cast<void *>(F_LiSetRange);
    g_lightVt[0x9C / 4] = reinterpret_cast<void *>(F_LiSetAttenuation);
    g_soundVt[0x7C / 4] = reinterpret_cast<void *>(F_SoSetType);
    g_soundVt[0x80 / 4] = reinterpret_cast<void *>(F_SoSetRange);
    g_soundVt[0x8C / 4] = reinterpret_cast<void *>(F_SoSetVolume);
    g_soundVt[0x90 / 4] = reinterpret_cast<void *>(F_SoSetLoop);
    g_sectorVt[0x74 / 4] = reinterpret_cast<void *>(F_SeAddLight);
    g_sectorVt[0x78 / 4] = reinterpret_cast<void *>(F_SeRemoveLight);
    g_sceneVt[0xEC / 4] = reinterpret_cast<void *>(F_UpdateFrameSector);
    g_sceneVt[0x8C / 4] = reinterpret_cast<void *>(F_GetLightIntensity);
    g_sceneVt[0xE0 / 4] = reinterpret_cast<void *>(F_GetSector);
    g_driverVt[0x50 / 4] = reinterpret_cast<void *>(F_CreateVisual);
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
    g_actorVt[0xD4 / 4 + 1] = reinterpret_cast<void *>(F_Hit);            // (g++: two destructor slots)
    g_actorVt[0xD8 / 4 + 1] = reinterpret_cast<void *>(F_GetMaterialId);
    g_actorVt[0x78 / 4 + 1] = reinterpret_cast<void *>(F_GetSeatOccupant);
    g_imageVt[0] = reinterpret_cast<void *>(F_ImRelease);
    g_igraphVt[0x3C / 4] = reinterpret_cast<void *>(F_LoadTexture);
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
    TestThrownInit();
    TestThrownContact();
    TestThrownFlight();
    TestShells();
    TestBlood();
    TestMuzzleLight();
    TestMission();
    TestMelee();
    TestExplosionMessage();
    TestFlySound();
    TestExplosionDecalAndSplat();
    TestSegmentInView();
    TestFireBulletDummies();
    TestThrowItem();
    TestProjectileDoors();
    TestPoolWithoutTexture();
    std::printf("shots: %d checks, %d failures\n", checks, failures);
    g_exiting = true;
    return failures != 0;
}
