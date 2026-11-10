// The Junkers 88 (C_Junkers88, actor type 34, vtable 0x00812c10, 0x330 bytes; original
// 0x005edcf0..0x005f9700): the bomber of the air-fight mission. The players man its five seats
// (SEAT00..04; seat weapons where a seat's flags have bit 8) while fighters (interceptors, the
// model "la_Bf109Canyon") fly scripted animation paths ("#airfight_0<n>_A<k>") around it. A
// fighter's path notes "fire_start" / "fire_end" make it attack: it fires tracers past the bomber,
// plays hit sounds on it, and each attacking fighter wears the bomber's health down. The bomber's
// damage shows through "#adamage<n>" animations and on its parts (the frames ending in 'd':
// wings, flaps, engines, propeller blades, elevators) breaking in stages; at 0 it is shot down
// ("#jushotdwn<n>"), falls, and the cutscene 31 runs. Hits on a fighter's model (the gunners')
// take its health; shot down it smokes, plays "#bfshotdwn<n>" and falls to the ground. A camera
// path ("#aircraftA<k>") moves the bomber's frame along. The crew's radio chatter comes from six
// groups of speech sounds. The Junkers is client-side: StartMission / StopMission work for
// `server` false only. (The scripts' "Junkers88 setup" global is the Junkers itself.)
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

class C_chunk;
class C_Human;
class C_SeatWeapon;
class I3D_model;
class I3D_sound;

class alignas(16) C_Junkers88 : public C_Actor {        // "Junkers88", vtable 0x00812c10
public:
    C_Junkers88();                                      // 0x005ef180 (becomes g_junkers88)
    ~C_Junkers88() override;                            // 0x005ef470 (scalar deleting 0x005ef430)

    // The chatter's timers, the camera path, the model's animations, the seat weapons, the
    // fighters (fading in between 9 and 11 s of game time), the bomber's velocity, its damage, the
    // debris, the propellers, the tracers, the fall once shot down (not during another cutscene
    // than 31, not hidden).
    void Update(int frameTime) override;                // 0x14 0x005f7710
    // The seat weapons; the chatter group 5 (a fighter around) while alive.
    void Tick(int timeDelta) override;                  // 0x18 0x005f76a0
    bool SetFrame(I3D_frame *frame) override { return C_Actor::SetFrame(frame); }   // 0x2C 0x005ef6b0
    // Chunks 100 the health, 200 a fighter's hit points, 300 the damage a fighter does a second,
    // 400 / 500 (kept, not used); others are entered, not left (quirk kept).
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005f29f0
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005ef6c0
    void StartMission(bool server) override;            // 0x3C 0x005ef6d0
    void StopMission(bool server) override;             // 0x40 0x005efc10
    // Stop and start for the roles the game has.
    void RestartMission(bool server) override;          // 0x44 0x005edde0
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005ee1e0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005ee830

    bool IsSeatBlocked(int) override { return false; }  // 0x6C 0x005f2e60
    bool SetSeatBlocked(int seat, bool blocked) override;   // 0x70 0x005f2e70
    // Into the seat (nothing without an occupant): linked to its dummy, facing along it; the seat
    // weapon's gunner.
    void SetSeatOccupant(int seat, C_Actor *occupant) override;   // 0x74 0x005f2ec0
    C_Actor *GetSeatOccupant(int seat) override;        // 0x78 0x005f2fa0
    int FindSeat(const C_Actor *occupant) override;     // 0x7C 0x005f2fd0
    // The seat freed (its weapon's gunner too); the occupant stays where it is.
    C_Actor *LeaveSeat(int seat) override;              // 0x80 0x005f3000
    // The same, true (no way out to find).
    bool ExitSeat(int seat) override;                   // 0x84 0x005f30c0
    int GetSeatCount() override { return SEATS; }       // 0x88 0x005ef420
    // The seats' flags as the last StartMission found them (a table shared by every Junkers).
    uint32_t GetSeatFlags(int seat) override;           // 0x8C 0x005f2e30
    // First person: "cameraFPV*" under the seat's dummy; else the camera path's camera dummy.
    I3D_frame *GetSeatCamera(int seat) override;        // 0x90 0x005f3110
    // The number after "name" in the dummy's user properties (-1: none).
    int GetSeatText(int seat) override;                 // 0x94 0x005f3060
    bool HasSeatWeapon(int seat) override;              // 0x98 0x005f8370
    int GetSeatWeaponRounds(int seat) override;         // 0x9C 0x005f83b0
    S_vector GetSeatWeaponAim(int seat) override;       // 0xA4 0x005f84b0
    S_vector GetSeatWeaponOrigin(int seat) override;    // 0xA8 0x005f8440
    float UnknownAC(int seat) override;                 // 0xAC 0x005f8500
    C_ItemDef *GetSeatWeaponDef(int seat) override;     // 0xB4 0x005f8180
    // The seat weapon's controls (third person: only an AI's).
    void ApplySeatControls(int seat, float *axes, float *buttons) override;   // 0xC0 0x005f83e0
    void ShowSeatView(int seat, bool on) override;      // 0xC4 0x005f8490
    bool UnknownC8(int seat, int *a, int *b, int *c) override;   // 0xC8 0x005f8570
    // Every seat.
    bool IsSeatFirstPersonOnly(int seat) override;      // 0xCC 0x005f8540
    // The gunner's first-person field of view (property 400).
    float GetGunFov() const { return m_gunFov; }
    // A hit on a fighter's model (`flags`: the frame hit) takes its hit points (by the setup's
    // health factor); the bomber's own frames nothing. Only two hits by others than the player.
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x005f0d70

    // The script (Junkers88_SetState): `count` fighters on random paths 1..9, once.
    void SetState(int count);                           // 0x005f0a90
    // The script (SetupJunkers88): the factors of the fighters' damage and of their hit points.
    void SetInterceptors(float firepower, float health) // 0x005eddc0
    {
        m_firepower = firepower;
        m_interceptorHealth = health;
    }
    // 0 once shot down (the squads' gunners stop firing at its fighters).
    float GetHealth() const { return m_health; }        // +0x280

    // The fighter a squad gunner sees nearest in front of him (none in the game's first 10 s or
    // once the bomber is down): the Junkers, with the fighter's model in `frame`.
    C_Actor *FindTarget(C_Human *human, I3D_frame **frame);   // 0x005f0e60

    static constexpr int SEATS = 5;

private:
    friend class C_Junkers88Test;

    // A group of the crew's radio chatter: speech sounds "<id>.wav", one at a time, then a pause.
    struct S_Chatter {                                  // 0x20 bytes (ctor 0x005ef400 (DS 0x006108a0), dtor 0x005ef450)
        int minDelay = 0;                               // +0x00 ms
        int delay = 0;                                  // +0x04 ms until another may play
        int range = 0;                                  // +0x08 ms (the random part of the pause)
        int next = 0;                                   // +0x0C the next sound in turn
        std::vector<int> sounds;                        // +0x10
    };
    struct S_Seat {                                     // 0x10 bytes
        bool blocked = false;                           // +0x00
        C_Actor *occupant = nullptr;                    // +0x04
        I3D_frame *frame = nullptr;                     // +0x08 "SEAT<nn>"
        uint32_t flags = 0;                             // +0x0C the digits after "SEAT" in its user properties
    };
    // Something falling from a frame to the ground (a sphere swept along the frame's motion; the
    // constructor is 0x005f32f0).
    struct alignas(16) S_Fall {                         // 0x1C bytes
        S_vector4 pos = {};                             // +0x00 the last place
        I3D_frame *frame = nullptr;                     // +0x10 the model's "BODY"
        bool falling = false;                           // +0x14
        float radius = 0.0f;                            // +0x18
        void Init(I3D_frame *model, float sphere);      // 0x005f3300
        void Start();                                   // 0x005f34a0
        // Not falling: the frame's place taken; falling: false once it has hit something (the
        // frame put there). (RET 4: the caller passes the frame time, never read.)
        bool Update();                                  // 0x005f3360
        void Save(C_chunk &chunk) const;                // 0x005f31f0
        void Load(C_chunk &chunk);                      // 0x005f3230
    };
    // An animation path: a dummy model ("#aircraftdum", "#airfightdum") playing "<name><k>", k = 1,
    // 2, ... in turn; the owner frame (while following) takes the place of the model's first child.
    struct S_Path {                                     // 0x14 bytes
        S_Path(I3D_frame *owner, const char *modelFile, const char *animName);   // 0x005f5f20
        // (The original leaves the dummy model; released here.)
        ~S_Path();                                      // 0x005f60e0
        S_Path(const S_Path &) = delete;
        S_Path &operator=(const S_Path &) = delete;
        // The callback of the model's first child (the animation's notes, message 0x1000).
        void SetCallback(I3D_CALLBACK *callback);       // 0x005f60b0
        void Update(int frameTime);                     // 0x005f6100
        // The next animation, `carry` ms into it.
        void Next(int carry);                           // 0x005f62a0
        void Save(C_chunk &chunk) const;                // 0x005f5c40
        void Load(C_chunk &chunk);                      // 0x005f5d20

        I3D_frame *owner = nullptr;                     // +0x00
        I3D_model *model = nullptr;                     // +0x04 (null: did not load)
        int index = 0;                                  // +0x08 the animation's number
        C_str name;                                     // +0x0C
        bool follow = false;                            // +0x10
    };
    // A part of the bomber that breaks in stages: the intact frame ("<name>"), the damaged one
    // ("<name>d") and the stump ("<name>f"); the parts that hang on it break with it.
    struct S_Part {                                     // 0x40 bytes (dtor 0x005f6ab0)
        S_Part(I3D_frame *damaged, I3D_frame *model, bool engine, bool outer);   // 0x005f6840
        I3D_frame *frames[3] = {};                      // +0x00 intact, damaged, stump
        int stages = 0;                                 // +0x0C 1 or 2
        bool engine = false;                            // +0x10 "engine_l00d" / "engine_r00d"
        bool outer = false;                             // +0x11 the engines, "left_wing01d", "right_wing01d"
        bool hideIntact = false;                        // +0x12 (its engine fell: the intact frame goes too)
        int blades = -1;                                // +0x14 "listy_l00d" 0, "listy_r00d" 1, until hit
        std::vector<S_Part *> hanging;                  // +0x18 parts that break with it
        std::vector<S_Part *> thrown;                   // +0x28 parts thrown off at the last stage
        int index = 0;                                  // +0x38 in m_parts
        int stage = 0;                                  // +0x3C
        I3D_frame *Shown() const;                       // 0x005f6ae0 (null: none, or hidden)
        bool HasNext() const;                           // 0x005f6b10
        void Save(C_chunk &chunk) const;                // 0x005f6700
        void Load(C_chunk &chunk);                      // 0x005f6750
    };
    // A piece thrown off the bomber (a frame falling, spinning).
    struct alignas(16) S_Debris {                       // 0x30 bytes
        S_Debris(I3D_frame *piece, const S_vector4 &throwVelocity);   // 0x005f5960
        explicit S_Debris(C_chunk &chunk);              // 0x005f57c0 (DS 0x006184e0)
        S_vector4 velocity = {};                        // +0x00
        S_vector4 spin = {};                            // +0x10 rad a second about x, y, z
        float floor = 0.0f;                             // +0x20 (its height at the start)
        I3D_frame *frame = nullptr;                     // +0x24
        bool Update(int frameTime);                     // 0x005f5a30 (false: 500 m fallen)
        void Save(C_chunk &chunk) const;                // 0x005f5640
    };
    // A tracer (a copy of the "WeaponOdds" model's "trace") flying at 0.5 m a ms.
    struct S_Tracer {                                   // 0x08 bytes
        S_Tracer(I3D_frame *model, const S_vector &pos, const S_vector &dir);   // 0x005f91b0
        S_Tracer(I3D_frame *model, C_chunk &chunk);     // 0x005f8f70
        I3D_frame *frame = nullptr;                     // +0x00
        float distance = 0.0f;                          // +0x04 m left
        void Place(const S_vector &pos, const S_vector &dir);   // 0x005f8c50
        void Reuse(const S_vector &pos, const S_vector &dir);   // 0x005f8c20
        bool Update(int frameTime);                     // 0x005f88c0
        void Save(C_chunk &chunk) const;                // 0x005f8d70
    };
    // A fighter (the model "la_Bf109Canyon" as "interceptor_<n>" under the bomber's frame) on the
    // path "#airfight_0<n>_A<k>".
    struct alignas(16) S_Fighter {                      // 0x120 bytes
        S_Fighter(int index, const char *modelFile, float hitPoints);   // 0x005f5310
        S_Fighter(C_chunk &chunk, const char *modelFile);   // 0x005f40e0
        ~S_Fighter();                                   // 0x005f5540
        S_Fighter(const S_Fighter &) = delete;
        S_Fighter &operator=(const S_Fighter &) = delete;
        bool CreateModel(int index, const char *modelFile);   // 0x005f4910
        void LoadSounds();                              // 0x005f4740
        void InitVolume();                              // 0x005f4e50 (DS 0x00617910)
        void InitGuns();                                // 0x005f5100
        // False when gone (shot down and landed, or its fall's animation over).
        bool Update(int frameTime);                     // 0x005f3a70
        void Fire(int frameTime);                       // 0x005f36e0
        void HitBomber(int frameTime);                  // 0x005f38f0
        void Damage(float amount);                      // 0x005f34e0
        void ShootDown();                               // 0x005f3510
        void StartAttack();                             // 0x005f45a0
        void StopAttack();                              // 0x005f3e10
        // The bomber told if it attacked; the path's notes off.
        void Disengage();                               // 0x005f3e30
        void Save(C_chunk &chunk) const;                // 0x005f3e70

        S_matrix invMatrix = {};                        // +0x00 the box's
        S_CollisionVolume volume;                       // +0x40
        I3D_model *model = nullptr;                     // +0xA0
        S_Path *path = nullptr;                         // +0xA4
        bool attacking = false;                         // +0xA8 (counted in g_attackingFighters)
        bool down = false;                              // +0xA9 shot down
        bool inWorld = false;                           // +0xAA the box added
        I3D_sound *guns[5] = {};                        // +0xAC "kul_d1".."kul_d5"
        I3D_sound *gun = nullptr;                       // +0xC0 the one playing
        I3D_sound *motor = nullptr;                     // +0xC4 "motor1".."motor4"
        int smoke[2] = {-1, -1};                        // +0xC8 the wings' (shot down)
        S_Fall fall;                                    // +0xD0
        int fireTime = 0;                               // +0xF0 ms to the next tracer
        int hitTime[2] = {};                            // +0xF4 ms to the next hit sounds on the bomber
        int pathIndex = 0;                              // +0xFC "#airfight_0<n>_A"
        float health = 0.0f;                            // +0x100
        std::vector<I3D_frame *> gunFrames;             // +0x104
    };
    // The parts by how they break: m_damage holds every part that breaks by itself (its count:
    // their stages), m_engines the engines, m_outer the outer wings and the engines; each with the
    // count still to break (going down with the health), its count at the start, the parts left
    // and those broken.
    struct S_PartGroup {                                // 0x28 bytes
        int left = 0;                                   // +0x00
        int initial = 0;                                // +0x04
        std::vector<S_Part *> parts;                    // +0x08
        std::vector<S_Part *> broken;                   // +0x18
    };

    // The chatter of `group` (0 a fighter attacks, 1 the bomber hit, 2 a fighter hit, 3 a fighter
    // shot down, 4 the bomber shot down, 5 a fighter around: the sound by its bearing).
    void Chatter(int group);                            // 0x005f17c0
    void ResetChatter();                                // 0x005f1390
    void LoadSounds();                                  // 0x005ef840
    void ReleaseSounds();                               // 0x005efb80
    void InitSeats();                                   // 0x005f2cc0
    void EmptySeats();                                  // 0x005f2de0
    void InitSeatWeapons();                             // 0x005f81b0
    void ReleaseSeatWeapons();                          // 0x005f8300
    C_SeatWeapon *SeatWeapon(int seat) const;
    // The parts: every frame of the model ending in 'd'.
    void InitParts();                                   // 0x005f07e0
    static uint32_t __stdcall EnumPart(I3D_frame *frame, void *junkers);   // 0x005f0830
    void AddPart(I3D_frame *frame);                     // 0x005efe80
    // The parts freed, the model opened again (its frames back as they were).
    void ReleaseParts();                                // 0x005f08a0
    void InitCamera();                                  // 0x005efdd0
    void ReleaseCamera();                               // 0x005efe50
    void InitPropellers();                              // 0x005f0a50
    // The damage look: "#adamage<n>" (-1: none); 4 sets both engines smoking.
    void ShowDamage(int level);                         // 0x005f70d0
    // Smoke at a propeller (`right`: the right one's frame); with `sounds` its damage sounds there.
    void EngineFire(bool right, bool sounds);           // 0x005f6ca0
    // A blades part reached a stage: the engine stops. (RET 8: the original's second argument
    // is 0 at its one call, so only that path is ported.)
    void EngineDown(S_Part *part);                      // 0x005f6e20
    void EngineFell(S_Part *part);                      // 0x005f7e10
    // The part on to its next stage (its hanging parts with it; debris thrown).
    void Advance(S_Part *part);                         // 0x005f6b40
    // Part `index` of `from` advanced `times`: into `done` without another stage, else into
    // `outer` / `engines` by its kind (neither: it stays).
    void BreakPart(int index, std::vector<S_Part *> &from, std::vector<S_Part *> &done,
                   std::vector<S_Part *> &outer, std::vector<S_Part *> &engines, int times);   // 0x005f8040
    void ThrowDebris(I3D_frame *piece);                 // 0x005f0580
    void UpdateDebris(int frameTime);                   // 0x005f75f0
    void UpdateDamage(int frameTime);                   // 0x005f71e0
    void ShootDown();                                   // 0x005f6ff0
    // (RET 4: the caller passes the frame time, never read.)
    void TurnPropellers();                              // 0x005f7bb0
    // A tracer from `pos` along `dir` (the pool's, else a new one).
    void Fire(const S_vector &pos, const S_vector &dir);   // 0x005f85a0
    void UpdateTracers(int frameTime);                  // 0x005f87f0
    S_Fighter *FindFighter(const I3D_frame *model) const;   // 0x005f0ca0
    S_Fighter *FindFighterByPath(const I3D_frame *pathModel) const;   // 0x005f0c50
    // A fighter's path notes "fire_start" / "fire_end".
    void StartAttack(const I3D_frame *pathModel);       // 0x005f0cf0
    void EndAttack(const I3D_frame *pathModel);         // 0x005f0d20
    static int __stdcall PathNote(I3D_frame *frame, uint32_t message, uintptr_t note, uintptr_t);   // 0x005f54c0
    static int __stdcall NoPathNote(I3D_frame *, uint32_t, uintptr_t, uintptr_t) { return 0; }   // 0x005f54b0
    // Chunks 14000..16000: the group's count and its two lists (as indices into m_parts). (The
    // original takes the group's fields one by one: RET 0x14 and 0x10.)
    void SaveParts(C_chunk &chunk, uint16_t id, const S_PartGroup &group) const;   // 0x005ede30
    void LoadParts(C_chunk &chunk, S_PartGroup &group);   // 0x005ee010

    S_vector4 m_velocity = {};                          // +0x090 m/s (the last update's)
    int m_hitsByOthers = 0;                             // +0x0A0 hits by others than the player (2 count)
    S_Chatter m_chatter[6];                             // +0x0A4
    S_Seat m_seats[SEATS];                              // +0x164
    std::vector<C_SeatWeapon *> m_seatWeapons;          // +0x1B4 (one a seat, null without)
    S_Path *m_camera = nullptr;                         // +0x1C4 "#aircraftA<k>"
    std::vector<S_Fighter *> m_fighters;                // +0x1C8
    std::vector<S_Debris *> m_debris;                   // +0x1D8
    S_PartGroup m_damage;                               // +0x1E8
    S_PartGroup m_engines;                              // +0x210
    S_PartGroup m_outer;                                // +0x238
    std::vector<S_Part *> m_dependent;                  // +0x260 parts that only break with others
    std::vector<S_Part *> m_parts;                      // +0x270 all of them
    float m_health = 0.0f;                              // +0x280
    float m_firepower = 1.0f;                           // +0x284 (SetInterceptors)
    float m_interceptorHealth = 1.0f;                   // +0x288 (SetInterceptors)
    float m_damageRate = 0.0f;                          // +0x28C health a second (the attacking fighters')
    I3D_frame *m_blur[2] = {};                          // +0x290 "bluur_l00", "bluur_r00"
    S_Part *m_bladeParts[2] = {};                       // +0x298 "listy_l00d", "listy_r00d"
    S_Part *m_engineParts[2] = {};                      // +0x2A0 "engine_l00d", "engine_r00d"
    // "ju88mtr1", "ju88mtr2", "DMGMTR01", "DMGMTR02", "expmtr01", "expmtr02".
    I3D_sound *m_sounds[6] = {};                        // +0x2A8
    I3D_frame *m_propeller[2] = {};                     // +0x2C0 "vrtule_l00", "vrtule_r00"
    I3D_frame *m_body = nullptr;                        // +0x2C8 "BODY"
    bool m_engineDown[2] = {};                          // +0x2CC
    S_Fall m_fall;                                      // +0x2D0
    float m_slowTime = 0.0f;                            // +0x2F0 ms of the camera's slow motion left
    float m_slowFraction = 0.0f;                        // +0x2F4
    I3D_frame *m_trace = nullptr;                       // +0x2F8 the tracers' model
    std::vector<S_Tracer *> m_tracers;                  // +0x2FC
    std::vector<S_Tracer *> m_tracerPool;               // +0x30C
    float m_maxHealth = 1000.0f;                        // +0x31C (property 100)
    float m_fighterHealth = 10.0f;                      // +0x320 (property 200)
    float m_fighterDamage = 30.0f;                      // +0x324 a second (property 300)
    float m_gunFov = 1.04719758f;                       // +0x328 (property 400) the gunner's first-person
                                                        //   field of view (the camera module's view 8)
    int m_unknown32C = 0x1BFE;                          // +0x32C (property 500, not used)
};

extern C_Junkers88 *g_junkers88;                        // 0x008ac9e4
// Fighters attacking (counted, not read).
extern int g_attackingFighters;                         // 0x008ac9e8

// The squads' gunners (C_SquadController::FindAirTarget): FindTarget of the Junkers (none
// without one).
C_Actor *FindJunkersTargetFor(C_Human *human, I3D_frame **frame);   // 0x005f0e40 (__fastcall)

// The Junkers' creator (g_actorCreators[ACTOR_JUNKERS88], vtable 0x00812c04).
class C_Junkers88Creator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005edd20
    const char *GetName() override { return "Junkers88"; }   // 0x005edd90
    void Destroy(C_Actor *actor) override;              // 0x005edda0
};
