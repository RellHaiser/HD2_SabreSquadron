// An air attack (C_AirAttack, actor type 36, vtable 0x00812930, 0x1F0 bytes; original
// 0x005e2c00..0x005ea2f0): a fighter plane (the actor's frame, a model) that the scripts send at
// a target frame (AirAttack_SetTarget). Without one it circles. With one it flies a strafing
// run: away from the target to 400 m (state 0, sometimes rolling), back towards it (state 1)
// firing bursts while it sees it, over it to 80 m past (state 2) and away again. It keeps clear
// of the ground and of what is ahead. The shots hit after a delay where they strike sand (a
// particle, a hit on a human in the way, now and then an impact sound and, near the camera, a
// whizz); the muzzle flashes are its "bilboard_l0N" / "bilboard_r0N" frames; the propeller
// ("bluur") turns; four motor sounds and five gun sounds. Shot down (its hit points) it smokes
// and flies on. Low over sand it raises dust within 60 m of the camera. Its box
// (COLLISION_TYPE_PIKE) moves with it. The squads' gunners fire at it (FindAirAttackFor).
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_chunk;
class C_Human;
class I3D_frame;
class I3D_sound;

class alignas(16) C_AirAttack : public C_Actor {        // "AirAttack", vtable 0x00812930
public:
    C_AirAttack();                                      // 0x005e3c90 (joins g_airAttacks)
    ~C_AirAttack() override;                            // 0x005e3f10 (scalar deleting 0x005e3ef0)

    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005e4070 (the property defaults)
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005e3ee0
    void StartMission(bool server) override;            // 0x3C 0x005e4090
    void StopMission(bool server) override;             // 0x40 0x005e56d0
    void RestartMission(bool server) override;          // 0x44 0x005e3090 (stop, start: as a server)
    // The flight, the shots landing, the box, the flashes (not during a cutscene, not hidden,
    // not in the sight module's first 2 s).
    void Update(int frameTime) override;                // 0x14 0x005e5960
    // The dust under the plane near the camera.
    void Tick(int timeDelta) override;                  // 0x18 0x005e71e0
    // Properties: chunk 2 the hit points, 4 a shot's damage (reports false, quirk kept).
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x005e8e10
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005e9010
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005e30b0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005e3420
    void UnknownD0(int, int, int, int) override {}      // 0xD0 0x005e3b70
    // Damage (any kind) while it has hit points; below 0 it is shot down: smoke, the script told.
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x005e3b80

    // `stop` (or no target while not shot down): circling (speed 100, keeping 30 m clear, up to
    // 100 m); else the run at `target` begins over it (state 2). (Quirk kept: shot down, the
    // plane is put back in state 2 without a target.)
    void SetTarget(bool stop, I3D_frame *target);       // 0x005e7570
    // m/s along its direction (the squad's gunners lead it by it).
    float GetSpeed() const { return m_speed; }

    // The run's states (+0x130).
    enum : int {
        AIR_ATTACK_LEAVING = 0,                         // flying away to 400 m (horizontally)
        AIR_ATTACK_APPROACHING = 1,                     // turning in, firing while it sees the target
        AIR_ATTACK_PASSING = 2,                         // over the target to 80 m past
        AIR_ATTACK_SHOT_DOWN = 3,                       // (left at once by SetTarget: never kept)
    };

private:
    // A shot on its way down (aligned 0x30 bytes).
    struct alignas(16) S_Bullet {
        S_vector4 pos;                                  // +0x00 where it strikes
        S_vector4 shot;                                 // +0x10 the shot's vector (from the plane)
        int time = 0;                                   // +0x20 ms until it lands
    };
    // A muzzle flash (8 bytes): its frame, ms shown (-1: off).
    struct S_Flash {
        I3D_frame *frame = nullptr;
        int time = -1;
    };
    // Dust raised under the plane (aligned 0x30 bytes): a dummy frame with a particle effect,
    // moving on along the ground.
    struct alignas(16) S_Dust {
        S_Dust(const S_vector4 &pos, const S_vector4 &dir);   // 0x005e92b0
        ~S_Dust();                                      // 0x005e94d0
        // Moves it on over sand; false when its time is over or it is no longer over sand.
        bool Update(int frameTime);                     // 0x005e94f0

        I3D_frame *frame = nullptr;                     // +0x00
        S_vector4 dir;                                  // +0x10 level, normalized
        int particle = -1;                              // +0x20
        int time = 0;                                   // +0x24 ms left
    };

    void SetTurn(float turnRate, float climbRate)       // 0x005e7550
    {
        m_turnRate = turnRate;
        m_climbRate = climbRate;
    }
    // Bursts while the target is in sight: a shot each 25 ms (a flash; aimed at the target within
    // 200 m with a spread; struck sand keeps a bullet).
    void Fire(int frameTime);                           // 0x005e81c0
    // A shot landing at `pos`: a human on the line of `shot` back from it is hit.
    void BulletHit(const S_vector &pos, const S_vector &shot);   // 0x005e3a80
    void PlayImpactSound(const S_vector &pos);          // 0x005e75f0
    void PlayWhizzSound(const S_vector &pos);           // 0x005e7bf0
    void AddDust(const S_vector4 &pos, const S_vector4 &dir);    // 0x005e7340

    friend C_Actor *FindAirAttackFor(C_Human *human);
    friend void PauseAirAttackSounds(bool paused);

    uint8_t m_unknown84[0x0C] = {};                     // +0x84
    S_matrix m_invMatrix = {};                          // +0x90 the frame's world inverse (the box)
    S_CollisionVolume m_volume;                         // +0xD0
    int m_state = AIR_ATTACK_LEAVING;                   // +0x130
    bool m_started = false;                             // +0x134
    bool m_idle = false;                                // +0x135 circling (no run)
    bool m_targetInSight = false;                       // +0x136
    bool m_rolling = false;                             // +0x137 a victory roll while leaving
    bool m_gone = false;                                // +0x138 (state 3 past 500 m: never)
    float m_turnRate = 0.0f;                            // +0x13C rad a second about y
    float m_climbRate = 0.0f;                           // +0x140 direction y a second
    float m_roll = 0.0f;                                // +0x144 rad
    float m_speed = 0.0f;                               // +0x148 m/s
    float m_clearance = 0.0f;                           // +0x14C m kept over the ground and ahead
    float m_ceiling = 0.0f;                             // +0x150 m: keeping clear only under it
    float m_health = 0.0f;                              // +0x154
    float m_hitPoints = 100.0f;                         // +0x158 property (chunk 2)
    float m_damage = 70.0f;                             // +0x15C property (chunk 4): a shot's
    I3D_frame *m_target = nullptr;                      // +0x160
    I3D_frame *m_propeller = nullptr;                   // +0x164 "bluur"
    I3D_sound *m_motorSound = nullptr;                  // +0x168 the one playing
    I3D_sound *m_gunSound = nullptr;                    // +0x16C the one playing
    I3D_sound *m_gunSounds[5] = {};                     // +0x170 "kul_d1".."kul_d5"
    I3D_sound *m_motorSounds[4] = {};                   // +0x184 "motor1".."motor4"
    int m_fireTime = 0;                                 // +0x194 ms to the next shot (< 0: due)
    int m_dustTime = 0;                                 // +0x198 ms to the next dust
    int m_smoke = -1;                                   // +0x19C particle (shot down)
    std::vector<S_Bullet *> m_bullets;                  // +0x1A0
    std::vector<S_Flash *> m_flashes;                   // +0x1B0
    std::vector<I3D_sound *> m_impactSounds;            // +0x1C0 "sand0N"
    std::vector<I3D_sound *> m_whizzSounds;             // +0x1D0 "whizzN"
    std::vector<S_Dust *> m_dusts;                      // +0x1E0
};

// The air attacks' creator (g_actorCreators[ACTOR_AIR_ATTACK], vtable 0x00812920).
class C_AirAttackCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005e2c30
    const char *GetName() override { return "AirAttack"; }   // 0x005e2c90
    void Destroy(C_Actor *actor) override;              // 0x005e2ca0
};
extern C_AirAttackCreator g_airAttackCreator;           // 0x008ac9d4

// Every air attack, in creation order (0x008ac9c4).
extern std::vector<C_AirAttack *> g_airAttacks;

// The nearest air attack (state 1 or 2) within 150 m that `human`'s body frame sees past the
// borders, null if none.
C_Actor *FindAirAttackFor(C_Human *human);              // 0x005e8b70 (__fastcall)
// The planes' motor and gun sounds off while the game is paused (C_game::SetPaused).
void PauseAirAttackSounds(bool paused);                 // 0x005e8d50 (__fastcall)
// CollisionRayTest(from, dir, ...) through the borders (MATERIAL_STOP / MATERIAL_PASS volumes
// facing the ray): on from 5 cm past each, until less than 20 cm of the ray is left (then none).
// (Also the effects module's, 0x00487240.)
S_CollisionVolume *RayTestPastBorders(const S_vector4 &from, const S_vector4 &dir, S_vector4 *hitPos,
                                      S_vector4 *hitNormal, uint32_t excludeTypes,
                                      uint32_t excludeFlags);   // 0x005e2cc0 (__fastcall)
