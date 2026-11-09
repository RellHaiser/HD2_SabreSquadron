// Shots and the shot manager — original source 0x00448270..0x004559c0 ("ShotMgr",
// g_shotManager).
//
// A shot is a bullet, a projectile (rocket, rifle grenade, flak shell) or a thrown item
// (grenade) in flight. The server moves shots in its fixed 100 ms ticks: every tick a
// shot sweeps the segment it covers through the collision world and handles each volume
// in it from near to far (damage, penetration by material, ricochets, explosions);
// between ticks the client interpolates the visuals. Clients that are not the server
// only see bullets of other players as "client bullets": a ray decides at once where the
// bullet ends, and the bullet then just flies there.
//
// The manager also owns the client-side effects of shooting: bullet holes and scorch
// marks (projected decals), blood drops and pools, ejected cartridge shells and the
// short light flashes at the muzzle.
//
// Surfaces are identified by material ids (tables\hdmaterials.def, g_materials): the
// material decides how much of a bullet's power a surface absorbs, what it costs to go
// through, the hit sounds, particle effects and decals.
#pragma once

#include <cstdint>
#include <vector>

#include "common/object_pool.h"
#include "game/module.h"
#include "game/sound_module.h"
#include "game/world.h"
#include "ls3d/math.h"

class C_Actor;
class C_BitStream;
class C_chunk;
class C_FirearmComp;
class C_Human;
class C_Item;
class C_ProjectileWeaponComp;
class C_WeaponComp;
class C_WeaponItem;
class I3D_frame;
class I3D_sound;
class IImage;

// ---- materials -------------------------------------------------------------------

// Record of tables\hdmaterials.def (0x100 bytes; only the fields the shots use are known).
struct S_MaterialDef {
    uint8_t unknown00[0x20];
    int footstepSound;                                      // +0x20 sound group of footsteps (chapter 0), -1 none
    uint8_t unknown24[4];
    // Power a bullet loses on the surface: with less left it stops (or ricochets); a hit
    // human takes the power above it as damage.
    uint32_t absorption;                                    // +0x28
    // Power a bullet or projectile spends to pass through; MATERIAL_IMPENETRABLE: none
    // do. Grenades pass through materials cheaper than their damage.
    uint32_t penetrationCost;                               // +0x2C
    int hitSound;                                           // +0x30 sound group (chapter 4), -1 none
    int hitSoundAlternate;                                  // +0x34 played instead when asked for
    uint8_t unknown38[4];
    int hitEffect;                                          // +0x3C particle effect, 0 none
    uint8_t unknown40[0x0C];
    int explosionEffect;                                    // +0x4C particle effect of explosions on it
    int decal;                                              // +0x50 bullet hole texture (0..8), -1 none
    // Scorch mark of explosions: 0 = x_expl00, 1..6 = x_blood00..05.
    int explosionDecal;                                     // +0x54
    uint8_t unknown58[0x80];
    // The vehicles' values (InitCarSurfaces into S_CarSurface).
    float carFriction;                                      // +0xD8
    float carBounce;                                        // +0xDC
    float carUnknownE0;                                     // +0xE0
    float carUnknownE4;                                     // +0xE4
    float carUnknownE8;                                     // +0xE8
    float carUnknownEC;                                     // +0xEC
    float carUnknownF0;                                     // +0xF0
    int carDustEffect;                                      // +0xF4
    int carSkidEffect;                                      // +0xF8
    int carContactEffect;                                   // +0xFC
};
static_assert(sizeof(S_MaterialDef) == 0x100, "S_MaterialDef layout");

constexpr uint32_t MATERIAL_IMPENETRABLE = 0xFFFFFFFF;

// Material ids with a special meaning to the shots.
enum : int {
    MATERIAL_NONE = -1,
    MATERIAL_HELMET = 12,                                   // what a bullet in a worn helmet hits
    MATERIAL_FRAGILE = 62,                                  // destroyed outright by a grenade's touch (glass, inferred)
    MATERIAL_STOP = 76,                                     // ends shots silently
    MATERIAL_PASS = 77,                                     // ignored by shots
};
// Material ids the car's wheels and body treat specially (the table's names are Czech).
enum : int {
    MATERIAL_WADING_WATER = 0x1F,                           // "Voda brodící"
    MATERIAL_SWIMMING_WATER = 0x20,                         // "Voda plavací"
    MATERIAL_MINEFIELD = 0x44,                              // "Minové pole - neodstranitelné I" (a minefield that cannot be cleared)
    MATERIAL_MINEFIELD_2 = 0x45,                            // (the same name)
    MATERIAL_COLD_WATER = 0x46,                             // "Studená voda"
    MATERIAL_62 = 0x62,                                     // (past the shipped table's last, 0x61; blows up as a minefield)
};

// tables\hdmaterials.def, loaded by LoadMaterialTables() (0x0071fd80, not ported).
extern S_MaterialDef *g_materials;                          // 0x0097b9b4
extern uint32_t g_materialCount;                            // 0x0097b9b8

// Material id from a frame's user properties ("matid<n>", any case), -1 if there is none.
// (A copy of GetFrameMaterialId() that lower-cases the properties first.)
int GetFrameMaterial(const I3D_frame *frame);              // 0x004511e0

// ---- shots -----------------------------------------------------------------------

// C_Shot::GetType(); the manager recycles bullets and deletes the others.
enum E_ShotType {
    SHOT_BULLET = 1,
    SHOT_THROWN = 3,
    SHOT_PROJECTILE = 4,
    SHOT_6 = 6,                                             // deleted like 3 and 4; never created
};

// Base of the shots (vtable 0x0080fa7c, 0x70 bytes, 16-byte aligned).
class alignas(16) C_Shot {
public:
    C_Shot(const C_Shot &) = delete;
    C_Shot &operator=(const C_Shot &) = delete;
    virtual ~C_Shot() { Release(); }                        // 0x00 0x0044dba0 (scalar deleting 0x0044dbd0)

    // Releases the visual.
    virtual void Release();                                 // 0x04 0x00448360
    // Fixed tick (server; clients for their own and single-player shots). True when the
    // shot is finished and can be removed.
    virtual bool Tick(int timeDelta) = 0;                   // 0x08
    // Every frame on clients: moves the visual between the tick positions.
    virtual void Update(int timeDelta) = 0;                 // 0x0C
    // Prepares a pooled shot for reuse (releases the visual).
    virtual void Recycle();                                 // 0x10 0x00448340
    // Shots are not kept in saved games: the manager writes an empty chunk per shot.
    virtual void SaveGame(C_chunk &) {}                     // 0x14 0x0044dbb0
    virtual void LoadGame(C_chunk &) {}                     // 0x18 0x0044dbc0

    E_ShotType GetType() const { return m_type; }

protected:
    explicit C_Shot(E_ShotType type) : m_type(type) {}

    // Common setup. `model`: shown as a copy (an object visual in the primary sector).
    // `fake`: see m_fake.
    void Init(C_Actor *shooter, const S_vector &pos, const S_vector &velocity, C_WeaponComp *weapon,
              I3D_frame *model, bool fake);                 // 0x004482a0

    S_vector m_velocity = {0, 0, 0};                        // +0x10 m/s
    S_vector m_pos = {0, 0, 0};                             // +0x20
    S_vector m_prevPos = {0, 0, 0};                         // +0x30 position before the last tick
    S_vector m_step = {0, 0, 0};                            // +0x40 movement of the last tick
    int m_interpolation = 0;                                // +0x50 % of m_step shown (Update)
    int m_traceDistance = 0;                                // +0x54 bullets: metres the trace has been shown
    float m_damage = 0.0f;                                  // +0x58 power left
    E_ShotType m_type;                                      // +0x5C
    C_Actor *m_shooter = nullptr;                           // +0x60
    C_WeaponComp *m_weapon = nullptr;                       // +0x64
    I3D_frame *m_visual = nullptr;                          // +0x68
    // Fired by a scripted fake attack (C_Item::Fire flags): the shot passes through actors
    // and its hit sounds are not heard by the AI.
    bool m_fake = false;                                    // +0x6C
    bool m_ricocheted = false;                              // +0x6D bullets ricochet once
    bool m_nearMissPlayed = false;                          // +0x6E
};

// A bullet (vtable 0x0080fa60, 0xC0 bytes). Pooled by the manager.
class C_BulletShot : public C_Shot {
public:
    C_BulletShot() : C_Shot(SHOT_BULLET) {}                 // 0x0044db40

    // Simulated bullet. `traceModel`: tracer (a billboard stretched along each step);
    // `flare`: a signal pistol flare (model "m_w_flare", flies at half speed).
    void Init(C_Actor *shooter, const S_vector &pos, const S_vector &dir, C_FirearmComp *weapon,
              I3D_frame *traceModel, bool fake, bool flare); // 0x0044b030
    // Client bullet: casts a 500 m ray to find where it ends (`flare` is passed, false,
    // and not used).
    void InitClient(C_Actor *shooter, const S_vector &pos, const S_vector &dir, C_FirearmComp *weapon,
                    I3D_frame *traceModel, bool fake, bool flare);   // 0x0044ca70
    // Moves a client bullet towards its end (every frame); true when it arrived.
    bool TickClient(int timeDelta);                         // 0x0044ccb0

    void Release() override;                                // 0x0044b380
    bool Tick(int timeDelta) override;                      // 0x0044b910
    void Update(int timeDelta) override;                    // 0x0044b400
    void Recycle() override;                                // 0x0044b350
    void SaveGame(C_chunk &) override {}                    // 0x0044db60
    void LoadGame(C_chunk &) override {}                    // 0x0044db70

private:
    // Power left after `distance` m: beyond the weapon's ideal range it drops linearly.
    float GetDamageAt(float distance, float damage) const;  // 0x0044b3b0
    // Plays the near-miss whiz once if the segment passes within 3 m of the local player.
    void CheckNearMiss(const S_vector &from, const S_vector &to);   // 0x004486b0

    float m_distance = 0.0f;                                // +0x70 metres flown
    int m_unknown74 = 0;                                    // +0x74 cleared by Init, not used
    bool m_finished = false;                                // +0x78
    int m_targetMaterial = MATERIAL_NONE;                   // +0x80 client bullets: material at the end
    S_vector m_target = {0, 0, 0};                          // +0x90 client bullets: where they end
    S_vector m_targetNormal = {0, 0, 0};                    // +0xA0
    bool m_flare = false;                                   // +0xB0
    int m_flareSparks = -1;                                 // +0xB4 particle effect handle
    bool m_burnedOut = false;                               // +0xB8 the flare went out
};

// A projectile (vtable 0x0080fad8, 0xA0 bytes): explodes where it hits.
class C_ProjectileShot : public C_Shot {
public:
    C_ProjectileShot() : C_Shot(SHOT_PROJECTILE) {}
    ~C_ProjectileShot() override { Release(); }             // 0x004518a0 (scalar deleting 0x00451880)

    // `model`: the projectile frame of the weapon (shown as a copy); `trailEffect`: smoke
    // trail particle effect (-1 none); `flySound`: whistle of an incoming shell;
    // `cameraFollow`: the camera may ride along when the local player fired it.
    void Init(C_Actor *shooter, const S_vector &pos, const S_vector &dir, C_ProjectileWeaponComp *weapon,
              I3D_frame *model, int trailEffect, bool flySound, bool cameraFollow, bool fake);   // 0x00448ce0

    void Release() override;                                // 0x00448fc0
    bool Tick(int timeDelta) override;                      // 0x00449290
    void Update(int timeDelta) override;                    // 0x00449030
    void Recycle() override {}                              // 0x00451870
    virtual bool Unknown1C(int) { return true; }            // 0x1C 0x00451860 (not called)

private:
    int m_trail = -1;                                       // +0x70 particle effect handle
    float m_distance = 0.0f;                                // +0x74 metres flown
    bool m_exploded = false;                                // +0x78
    int m_unknown7C = 0;                                    // +0x7C cleared by Init, not used
    int m_flightTime = 0;                                   // +0x80 ms
    int m_removeDelay = 0;                                  // +0x84 ms the shot is kept after exploding
    bool m_cameraFollows = false;                           // +0x88
    I3D_sound *m_flySound = nullptr;                        // +0x8C "e_incm<n>.wav"
    bool m_allowCameraFollow = false;                       // +0x90
};

// A thrown item (vtable 0x0080faa8, 0x6A0 bytes): a grenade bouncing as a rigid body
// until its fuse burns down, or until it hits something if it has none.
class C_ThrownShot : public C_Shot {
public:
    C_ThrownShot() : C_Shot(SHOT_THROWN) {}                 // inline in ThrowItem
    ~C_ThrownShot() override { Release(); }                 // 0x004505b0 (scalar deleting 0x00450590)

    // Flies at max(5, throw strength * muzzle velocity) m/s along `dir`.
    void Init(C_Actor *thrower, const S_vector &pos, const S_vector &dir, C_Item *item, bool fake);   // 0x0044cff0

    void Release() override;                                // 0x0044d210
    bool Tick(int timeDelta) override;                      // 0x0044d260
    void Update(int timeDelta) override;                    // 0x0044d240
    void Recycle() override {}                              // 0x00450560
    void SaveGame(C_chunk &) override {}                    // 0x00450570
    void LoadGame(C_chunk &) override {}                    // 0x00450580
    virtual bool Unknown1C(int) { return true; }            // 0x1C 0x00450550 (not called)

private:
    enum E_State {
        STATE_FLYING = 0,
        STATE_FINISHED = 1,
        STATE_IMPACT = 2,                                   // an impact grenade touched something
    };

    void Simulate(int timeDelta);                           // 0x0044d290
    void PlayBounceSound();                                 // 0x0044d4c0
    // Rigid body contact callback (`context`: the shot): RIGID_BODY_REFUSE to pass through what it
    // touched, 2 for a bounce, 3 for an impact (or a stopping material).
    static int __fastcall OnContact(int event, int unused, void *context,
                                     const S_RigidBodyContact *contact);   // 0x0044d570

    C_RigidBody m_body;                                     // +0x70
    int m_time = 0;                                         // +0x670 ms since the throw
    int m_fuseTime = 0;                                     // +0x674 ms, 0 = explodes on impact
    int m_state = STATE_FLYING;                             // +0x678 E_State
    // Direction and material passed to the explosion effects (set by ThrowItem, never changed).
    S_vector m_normal = {0, 1.0f, 0};                       // +0x680
    int m_material = MATERIAL_NONE;                         // +0x690
    int m_bounceSound = 0;                                  // +0x694 sound group (chapter 5)
    int m_bounceSoundTimer = 0;                             // +0x698
};

// ---- hit zones ------------------------------------------------------------------

// Body part hit on a human (C_HitZones::GetHitZone(), passed on to C_Actor::Hit()).
enum E_HitZone {
    HIT_ZONE_BODY = 0,
    HIT_ZONE_HEAD = 1,
    HIT_ZONE_LEFT_ARM_UPPER = 2,
    HIT_ZONE_LEFT_ARM_LOWER = 3,
    HIT_ZONE_RIGHT_ARM_UPPER = 4,
    HIT_ZONE_RIGHT_ARM_LOWER = 5,
    HIT_ZONE_LEFT_LEG_UPPER = 6,
    HIT_ZONE_LEFT_LEG_LOWER = 7,
    HIT_ZONE_RIGHT_LEG_UPPER = 8,
    HIT_ZONE_RIGHT_LEG_LOWER = 9,
    HIT_ZONE_HELMET = 11,                                   // the helmet stopped it
    HIT_ZONE_NONE = 0xFF,
};

// Boxes around the body parts of humans ("humanzones" model), placed on a human's
// joints whenever a shot is tested against it (server side).
class C_HitZones {                                          // 0x68 bytes
public:
    C_HitZones();                                           // 0x00455370
    void Load();                                            // 0x00455490
    void Free();                                            // 0x00455520
    // Places the boxes on `human` (the human's frame) without testing anything. Not called.
    void Place(I3D_frame *human);                           // 0x00455540
    // Shows or hides the model. Not called.
    void SetOn(bool on);                                    // 0x00455630 (DS 0x0046e130)
    // Zone the ray from `pos` along `dir` (its length is the reach) hits, HIT_ZONE_NONE if
    // none. A helmet (headgear type 0) in front of the head stops the ray: HIT_ZONE_HELMET.
    int GetHitZone(C_Human *human, const S_vector &pos, const S_vector &dir);   // 0x00455670
    // Debug output of a zone (C_game::Print, empty in the release build).
    void Print(int zone) const;                             // 0x004558a0

private:
    static constexpr int ZONE_COUNT = 10;

    I3D_frame *m_model = nullptr;                           // +0x00
    I3D_frame *m_zones[ZONE_COUNT] = {};                    // +0x04 frames named m_names
    int m_joints[ZONE_COUNT];                               // +0x2C joint each zone follows
    char m_names[ZONE_COUNT][2];                            // +0x54 one-letter frame names
};
extern C_HitZones g_hitZones;                               // 0x0089aa50

// ---- the manager ---------------------------------------------------------------

class C_Shell;
class C_MuzzleLight;
class C_BloodPool;
class C_BloodDrop;
struct S_ShotDecal;

class C_ShotManager : public C_Module {                     // "ShotMgr", vtable 0x0080fa18, 0x154 bytes
public:
    C_ShotManager(int id, const char *name);               // 0x0044d920
    ~C_ShotManager() override;                              // 0x0044dc30

    // Runs the shots (server, and clients through Update).
    void Tick(int timeDelta) override;                      // 0x00451ec0
    // Clients: 100 ms ticks of their own, client bullets, interpolation and the effects.
    void Update(int timeDelta) override;                    // 0x00451fb0
    void StartMission(bool server) override;                // 0x0044dde0
    void StopMission(bool server) override;                 // 0x0044e110
    bool OnMessage(C_BitStream *msg) override;              // 0x00452bd0
    void RestartMission(bool server) override;              // 0x00452280
    bool SaveGame(C_chunk &chunk) override;                 // 0x004524c0
    bool LoadGame(C_chunk &chunk) override;                 // 0x004527e0

    // --- firing ------------------------------------------------------------------------
    // Fires one bullet of `weapon` (C_WeaponItem) from `pos` along `dir`: shells come out
    // of the weapon's "cardum" and the flash shows at "blsdum" (for the local player's
    // first-person view, those of the HUD weapon). `shellSound`: sound group of the shell
    // bouncing on the ground. Bits 0-7 of `flags` set: a fake shot (C_Shot::m_fake).
    void FireBullet(C_Actor *shooter, C_Item *weapon, const S_vector &pos, const S_vector &dir, bool tracer,
                    uint32_t flags, int shellSound);        // 0x00451100
    // The same with explicit muzzle and shell dummies (vehicle guns call it directly).
    void FireBulletFrom(C_Human *shooter, C_WeaponItem *weapon, const S_vector &pos, const S_vector &dir,
                        I3D_frame *muzzle, I3D_frame *ejectDummy, bool tracer, uint32_t flags,
                        int shellSound);                    // 0x00450d30
    // Launches the projectile of a projectile weapon (`projectile`: its frame in the
    // weapon); see C_ProjectileShot::Init.
    void FireProjectile(C_Actor *shooter, C_Item *weapon, const S_vector &pos, const S_vector &dir,
                        I3D_frame *projectile, int trailEffect, bool flySound, bool cameraFollow,
                        uint32_t flags);                    // 0x00451670 (DS 0x0046c260)
    // Throws `item` (a grenade): the shot owns the flight, the item leaves the thrower's
    // inventory. In single player the thrower's team mates are warned.
    void ThrowItem(C_Actor *thrower, C_Item *item, const S_vector &pos, const S_vector &dir,
                   bool fake);                              // 0x00450400 (DS 0x0046ad10)

    // --- effects -----------------------------------------------------------------------
    // Area damage (both > 0) by `source`; in single player the AI hears it (sound group
    // `soundGroup`, nothing is played).
    void Explode(C_Actor *source, const S_vector &pos, float range, float damage, int soundGroup);   // 0x00451990 (DS 0x0046c670)
    // Sound, particle effect, scorch mark and material effect of an explosion (clients).
    void PlayExplosion(const S_vector &pos, const S_vector &dir, int effect, int material,
                       int soundGroup);                     // 0x00451a80 (DS 0x0046c790)
    // Hit sound of `material` (clients); `alternate` picks the material's second sound.
    void PlayHitSound(C_Actor *source, int material, const S_vector &pos, bool alternate,
                      bool silentToAI);                     // 0x00451b00
    // Bullet hole and hit effect of a bullet stopping on `frame` (null: level geometry).
    void OnBulletHit(C_Actor *shooter, I3D_frame *frame, int material, const S_vector &pos,
                     const S_vector &normal);               // 0x00451bc0
    // Particle effect of a hit, if it is in front of the camera.
    void PlayHitEffect(int material, const S_vector &pos, const S_vector &normal);   // 0x00451c00
    // Whiz of a bullet passing the local player.
    void PlayNearMissSound(const S_vector &pos);            // 0x00451e40
    // A pool of blood growing on the ground (dying humans).
    void AddBloodPool(const S_vector &pos, const S_vector &normal, float maxSize);   // 0x0044f2b0
    // A drop of blood flying from `pos` (3 m/s along `dir`, randomised) that leaves a
    // stain where it lands.
    void SprayBlood(const S_vector &pos, const S_vector &dir);   // 0x0044f4d0
    void SprayBloodRandom(const S_vector &pos, const S_vector &dir);   // 0x0044f4f0 (DS 0x0046a770)
    // A blood stain on the wall 3 m from `pos` along the horizontal part of `dir`. Not called.
    void AddBloodSplat(const S_vector &pos, const S_vector &dir);   // 0x0044f740

private:
    void LoadTextures();                                    // 0x0044e520
    // Shell flying out of `ejectDummy` (only for the local player).
    void EjectShell(C_Human *shooter, I3D_frame *ejectDummy, int soundGroup);   // 0x0044e5d0
    void AddBulletDecal(int material, const S_vector &pos, const S_vector &normal, I3D_frame *frame);   // 0x0044ebd0
    void AddBloodDrop(const S_vector &pos, const S_vector &dir, IImage *texture);   // 0x0044f520
    void AddExplosionDecal(int material, const S_vector &pos, const S_vector &dir);   // 0x0044fbc0
    void PlayExplosionEffects(int material, int effect, const S_vector &pos, const S_vector &dir);   // 0x00451db0
    // A decal to (re)use: the oldest one when m_maxDecals are shown, else a new one
    // (`setOrthogonal`: also calls SetOrthogonal(1) on a new one).
    S_ShotDecal *AllocDecal(bool setOrthogonal);
    // Recycles the bullets and deletes the other shots.
    void RemoveShots();
    // Deletes the client effects (shells go back to their pool).
    void RemoveEffects(bool hideShells);
    void RecycleBullet(C_BulletShot *bullet);

    C_ObjectPool<C_BulletShot> m_bulletPool;                // +0x0C
    IImage *m_decalTextures[9] = {};                        // +0x24 x_decal00..08.tga
    // Scorch / blood decal textures (S_MaterialDef::explosionDecal): x_expl00.tga, then
    // x_blood00..05.tga (0: drops, 1..3: random drops, 4: splats, 5: pools).
    IImage *m_splatTextures[7] = {};                        // +0x48
    C_ObjectPool<C_Shell> m_shellPool;                      // +0x64
    std::vector<S_ShotDecal *> m_decals;                    // +0x7C oldest first
    std::vector<C_BloodDrop *> m_bloodDrops;                // +0x8C
    std::vector<C_BloodPool *> m_bloodPools;                // +0x9C
    std::vector<C_MuzzleLight *> m_muzzleLights;            // +0xAC
    I3D_frame *m_weaponOdds = nullptr;                      // +0xBC "WeaponOdds" model
    I3D_frame *m_traceModel = nullptr;                      // +0xC0 its "trace"
    I3D_frame *m_shell3rd = nullptr;                        // +0xC8 "shell3rd"
    I3D_frame *m_shellFpv = nullptr;                        // +0xCC "shellFPV" (first-person view)
    // Shared by the hit, explosion and near-miss sounds (fields not set by one call
    // carry over from the previous one).
    S_SoundRequest m_sound;                                 // +0xD0
    std::vector<C_Shot *> m_shots;                          // +0x110
    std::vector<C_BulletShot *> m_clientBullets;            // +0x120
    std::vector<C_Shell *> m_shells;                        // +0x130
    int m_clientTickTime = 0;                               // +0x140 ms towards the next client tick
    int m_maxDecals = 0;                                    // +0x148
    int m_maxBloodDrops = 0;                                // +0x14C
    int m_maxBloodPools = 0;                                // +0x150
};

extern C_ShotManager g_shotManager;                         // 0x0089aac0

// Melee hit of `weapon` along `dir` (server side). `backstab`: a knife attack from behind
// (the target must face away) that kills.
void MeleeAttack(C_Actor *attacker, C_Item *weapon, bool backstab, const S_vector &pos,
                 const S_vector &dir);                      // 0x00451230
// Explosion with area damage at `pos`, also shown on the clients (with `material`'s
// scorch mark and a random explosion effect).
void SendExplosionMessage(const S_vector &pos, int material);   // 0x00451900
