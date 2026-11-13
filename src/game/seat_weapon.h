// C_SeatWeapon: the weapon mounted at a vehicle's seat (a car's C_Car::m_seatWeapons, made by its
// StartMission for seats with SEAT_FLAG_WEAPON; the static weapon's and the Junkers 88's too; 0x180
// bytes, 16-byte aligned). Original source 0x004c1230..0x004c7c70, ported in part 3 (3a: the setup,
// lifetime, messages and saves in seat_weapon.cpp; 3b: aiming, the controls and firing).
//
// The model holds per seat <nn>: "HORT<nn>*" (traverse), "VERT<nn>*" (elevation) and "AIM<nn>*"
// dummies, the barrels' "BARREL<nn>_<n>*" visuals (each with a "SHDUM*" muzzle and a "CARDUM*"
// cartridge dummy; the first's user properties give the item ("itemid"), the turning rates ("hs" / "vs", degrees a second)
// and limits ("up" / "down" / "left" / "right", degrees)), the first-person parts "VIEW<nn>" and a
// launcher's "projectile" visual. HORT's properties name its sounds ("soundm" while it turns,
// "sounds" when it stops; SOUND_CHAPTER_WEAPON_MANIPULATION groups).
#pragma once

#include <cstdint>
#include <vector>

#include "game/pending.h"                               // C_NetObjectMessage
#include "ls3d/math.h"

class C_Actor;
class C_BitStream;
class C_Car;
class C_chunk;
class C_Human;
class C_ItemContainer;
class C_ItemDef;
class C_WeaponItem;
class I3D_animation_set;
class I3D_frame;
class I3D_model;
class I3D_sound;

// A barrel (Init: "BARREL<seat>_<n>*" visuals from 0 until one is missing; those without a muzzle
// are left out).
struct S_SeatWeaponBarrel {                             // 0x10 bytes
    I3D_frame *frame;                                   // +0x00 (its animation notes throw the shells)
    I3D_frame *muzzle;                                  // +0x04 its "SHDUM*": the shots and the flash leave it
    I3D_frame *ejector;                                 // +0x08 its "CARDUM*": FireBulletFrom throws the cartridges there
    I3D_animation_set *animation;                       // +0x0C "<model>_f<seat>_<n>" (the shot's, on the model)
};

class alignas(16) C_SeatWeapon {
public:
    C_SeatWeapon();                                     // 0x004c19d0
    ~C_SeatWeapon();                                    // 0x004c1aa0
    C_SeatWeapon(const C_SeatWeapon &) = delete;
    C_SeatWeapon &operator=(const C_SeatWeapon &) = delete;

    // The weapon of seat `seat` of `owner` (`frame`: the owner's model, `seatFrame`: the seat's
    // dummy); false: it could not be set up (the car's StartMission deletes it), or it already was.
    bool Init(C_Actor *owner, I3D_frame *frame, I3D_frame *seatFrame, int seat);   // 0x004c1ad0
    // The mission's end (the car's StopMission, before it deletes the weapon): the turret back at
    // rest, the item, the barrels' animations, the shells model and the sounds released.
    void Stop();                                        // 0x004c2380
    // The car's restart: Stop, Init again as before, the gunner kept.
    void Restart();                                     // 0x004c6620
    // Network games, a client's message (the car's ReadMessages): the server fires the shot from
    // `pos` along `dir` out of barrel `barrel` (true: it did, the message goes on to the clients;
    // false: no rounds or no gunner); a client shows another's shot (from its own next barrel); the
    // server's reload, a client's (with its sound); the rounds a server sent (the weapon item's
    // amount). (`unused`: the car passes 1000, `barrel` / `store` the message's and its item store;
    // the original reads none of them.)
    bool FireShot(const S_vector4 *pos, const S_vector4 *dir, int barrel, int unused);   // 0x004c3dc0
    void PlayShot(int barrel);                          // 0x004c4010
    void Reload(C_ItemContainer *store);                // 0x004c6270
    void ShowReload(C_ItemContainer *store);            // 0x004c62e0
    void SetRounds(int rounds);                         // 0x004c6560
    // The saved game's (the car's CHUNK_CAR_SEAT_WEAPON, after the seat's index): chunk 2000 the
    // turret's angles and the firing state, 0x7D1 the item's rounds.
    bool SaveGame(C_chunk &chunk);                      // 0x004c6660
    bool LoadGame(C_chunk &chunk);                      // 0x004c68b0
    // The gunner (null: none): hands the first-person parts to a local player in first person and
    // stops the turning sound.
    void SetGunner(C_Human *gunner);                    // 0x004c6dd0
    // The rounds left in the weapon's item (its amount, truncated).
    int GetRounds();                                    // 0x004c6550 (__fastcall)
    // The weapon's item's definition, null when Init failed.
    C_ItemDef *GetItemDef();                            // 0x004c57b0 (__fastcall)
    // The item definition's text (its name), -1 when Init failed.
    int GetItemText();                                  // 0x004c57d0 (__fastcall)
    float UnknownAC() { return 0.0f; }                  // 0x004c57f0
    // The world direction the barrel points (VERT's z) and where it is (VERT's position); zero when
    // Init failed.
    S_vector GetAim();                                  // 0x004c5870
    S_vector GetOrigin();                               // 0x004c5800
    // The turning limits (rad; 1e6: none; read in place by the car's 0x004da1f0).
    void GetLimits(float *up, float *down, float *left, float *right) const;
    // The weapon can be turned to point at `pos` (an S_vector; within the limits, strictly).
    bool CanAim(float *pos);                            // 0x004c6230
    // The gunner's controls (C_HumanController's axes and buttons): the turret's speeds, firing,
    // reloading.
    void ApplyControls(float *axes, float *buttons);    // 0x004c3b60
    // Shows / hides the first-person parts ("VIEW<nn>", once Init ran).
    void ShowView(bool on);                             // 0x004c6d90
    // The reload's progress for the HUD (the actor's vtable +0xC8): false when none, else the ms left,
    // the reload's ms and the text to show (0x1B9F).
    bool UnknownC8(int *left, int *total, int *text);   // 0x004c7670
    // Every frame and every tick (the car's Update / Tick): the reload's countdown and the turret
    // (a local player's by the controls, another gunner's to his view), the delay's countdown. A
    // client's Update also ticks, a server's Tick also updates.
    void Update(int time);                              // 0x004c56a0
    void Tick(int time);                                // 0x004c55e0
    // Network games: the mission time of the last ammo the server added for the weapon, and how few
    // rounds in the vehicle's store let it add more. (These accessors are inline in the original;
    // out of line here so that the car's harness can stand in for the weapon.)
    uint32_t GetRefillTime() const;
    void SetRefillTime(uint32_t time);
    int GetRefillBelow() const;
    void SetRefillBelow(int rounds);
    // Network games: what sends the weapon's messages (SeatWeaponSendMessage) and its context
    // (the car, the static weapon).
    using T_MessageSender = bool(__fastcall *)(int kind, const S_vector4 *pos, const S_vector4 *dir, int barrel,
                                               int seat, C_Actor *owner);
    void SetMessageSender(T_MessageSender sender, C_Actor *context);

private:
    // The value of `key` in the frame's user properties ("<key><number>", the key matched in any
    // case), -1 when it has none.
    static int GetFrameInt(I3D_frame *frame, const char *key);   // 0x004c6570 (__stdcall)
    // HORT's turning ("soundm") and stopping ("sounds") sounds, linked to it and off.
    void LoadSounds();                                  // 0x004c6e50 (__fastcall)
    void ReleaseSounds();                               // 0x004c7630 (__fastcall)
    // The barrel's animation note "S...": a copy of the shells model's "SHELL" thrown out of it.
    void EjectShell(I3D_frame *barrel);                 // 0x004c5080
    // The barrels' animation callback (their frame data's unknown1C: the weapon).
    static int __stdcall OnBarrelNote(I3D_frame *frame, uint32_t message, uintptr_t note,
                                      uintptr_t time);  // 0x004c1990
    // The item has no rounds left.
    bool IsEmpty();                                     // 0x004c3100 (__fastcall)
    // The turret turned by the controls' speeds / towards the gunner's view; the view direction
    // handed to the gunner, the turning sound.
    void TurnByControls(int time);                      // 0x004c3120
    void TurnToView(int time);                          // 0x004c2580
    // The turning sound on, the stopping one off, and the reverse (inline in the original).
    void StartMotorSound();
    void StopMotorSound();
    // The gunner's shot: at the view's target in third person (a local player), else along the
    // muzzle; spread, fired, sent, the delay, the flash; the gunner noticed.
    void Fire();                                        // 0x004c4780 (__fastcall)
    // A reload from the item store (`store`: the owner's container 0, not read), with its sound and
    // message.
    void StartReload(C_ItemContainer *store);           // 0x004c6400
    // The reload's sound at the first barrel (the component's group; inline in the original).
    void PlayReloadSound();
    // The barrel's shot from `pos` along `dir`: its animation on the model, the bullet (from the
    // muzzle, the cartridges at the ejector) or the projectile, a client's shot sound (inline in
    // the original).
    void Shoot(int barrel, const S_vector &pos, const S_vector &dir, bool cameraFollow);
    // The item's muzzle flash at the barrel's muzzle.
    void ShowFlash(int barrel);
    // A launcher's recoil on a car where it is simulated: `factor` x the shot's momentum (`dir` x
    // the vehicle's mass), at the vehicle's lever origin.
    void Recoil(const S_vector &dir, float factor);
    // Within the traverse / elevation limits.
    bool CanTurnTo(const S_vector &pos);                // 0x004c5a30
    bool CanTiltTo(const S_vector &pos);                // 0x004c5f60

    S_vector4 m_vertRestAxis = {};                      // +0x000 VERT's turn at Init (S_quat::Inverse writes its s after the axis)
    S_vector4 m_hortRestAxis = {};                      // +0x010 HORT's
    uint8_t m_unknown20[0x40] = {};                     // +0x020 (not used by the weapon's code)
    uint8_t m_unknown60[0x60] = {};                     // +0x060 (zeroed by the constructor, not used otherwise)
    float m_vertRestAngle = 0.0f;                       // +0x0C0
    float m_hortRestAngle = 0.0f;                       // +0x0C4
    int m_seat = -1;                                    // +0x0C8
    float m_turnSpeed = 0.0f;                           // +0x0CC the controls' traverse (rad/s)
    int m_idleTime = 0;                                 // +0x0D0 ms without turning
    bool m_turnReleased = false;                        // +0x0D4 the traverse axis at 0
    float m_tiltSpeed = 0.0f;                           // +0x0D8 the controls' elevation
    bool m_tiltReleased = false;                        // +0x0DC
    int m_reloadLeft = 0;                               // +0x0E0 ms of the reload left
    int m_reloadTime = 0;                               // +0x0E4 the reload's ms
    int m_unknownE8 = 0;                                // +0x0E8 (Init clears it; not used otherwise)
    I3D_frame *m_model = nullptr;                       // +0x0EC the owner's
    I3D_frame *m_seatFrame = nullptr;                   // +0x0F0 the seat's dummy, its "HORT" parent if it has one
    I3D_frame *m_view = nullptr;                        // +0x0F4 "VIEW<nn>": the first-person parts
    I3D_frame *m_projectileFrame = nullptr;             // +0x0F8 "projectile" (a launcher's)
    C_Human *m_gunner = nullptr;                        // +0x0FC
    C_Actor *m_owner = nullptr;                         // +0x100 the car (or the static weapon, the Junkers 88)
    bool m_started = false;                             // +0x104 Init ran (until Stop)
    std::vector<S_SeatWeaponBarrel> m_barrels;          // +0x108
    float m_tilt = 0.0f;                                // +0x118 the elevation (rad)
    float m_turn = 0.0f;                                // +0x11C the traverse
    I3D_sound *m_motorSound = nullptr;                  // +0x120 HORT's "soundm" (on while turning)
    I3D_sound *m_stopSound = nullptr;                   // +0x124 HORT's "sounds"
    float m_upLimit = -1.0f;                            // +0x128 rad ("up" degrees; 1e6: none)
    float m_downLimit = -1.0f;                          // +0x12C ("down", negated)
    float m_leftLimit = -1.0f;                          // +0x130 ("left", negated)
    float m_rightLimit = -1.0f;                         // +0x134
    bool m_failed = false;                              // +0x138 Init could not set it up
    I3D_frame *m_hort = nullptr;                        // +0x13C "HORT<nn>*" (traverse)
    I3D_frame *m_vert = nullptr;                        // +0x140 "VERT<nn>*" (elevation)
    I3D_frame *m_aim = nullptr;                         // +0x144 "AIM<nn>*"
    I3D_model *m_shells = nullptr;                      // +0x148 the "GunShells" model (the cache's, hidden)
    uint32_t m_flags = 0;                               // +0x14C SEAT_WEAPON_* (seat_weapon.cpp)
    int m_barrel = 0;                                   // +0x150 the next to fire
    int m_delay = 0;                                    // +0x154 ms to the next shot (or the reload's end)
    C_WeaponItem *m_item = nullptr;                     // +0x158 "itemid" (a free item)
    int m_flashEffect = 0;                              // +0x15C the item's muzzle flash (particle effect)
    float m_turnRate = 0.3926991f;                      // +0x160 "hs" (rad/s; pi/4 without, pi/8 before Init)
    float m_tiltRate = 0.3926991f;                      // +0x164 "vs"
    bool m_launcher = false;                            // +0x168 the item fires projectiles (ICK_PROJECTILE_WEAPON)
    T_MessageSender m_sender = nullptr;                 // +0x16C network games
    C_Actor *m_senderContext = nullptr;                 // +0x170
    uint32_t m_refillTime = 0;                          // +0x174 network games: the mission time of the last ammo the server added
    int m_refillBelow = 0;                              // +0x178 how few rounds in the store let it add more
};

// Network games: a message of a seat weapon of `car` queued on the car's net object (reliable;
// RET 0x10): kind 0 a shot (C_SeatWeaponShotMessage: `pos`, `dir`, `barrel`, `seat`), kind 1 a
// reload (C_SeatWeaponReloadMessage: `seat`); other kinds send nothing. Always true.
bool __fastcall SeatWeaponSendMessage(int kind, const S_vector4 *pos, const S_vector4 *dir, int barrel, int seat,
                                      C_Actor *car);    // 0x004c1230
// The factory of the messages of the car's net object (NetMessageFactory, the context the car):
// the seat weapons' kinds 0..3 (`write`: writes their number, 4, in 3 bits and makes none).
C_NetObjectMessage *__fastcall MakeSeatWeaponMessage(void *car, C_BitStream *stream, bool write);   // 0x004c14b0

// The kinds of the seat weapons' messages on the car's net object (MakeSeatWeaponMessage).
enum : int {
    SEAT_WEAPON_MSG_SHOT = 0,
    SEAT_WEAPON_MSG_RELOAD = 1,
    SEAT_WEAPON_MSG_ROUNDS = 2,
    SEAT_WEAPON_MSG_AMMO = 3,
};

// A shot of a seat weapon (type 0; vtable 0x008105e0, 0x60 bytes): a client's to the server, which
// fires it and sends it on. The type (3 bits), the seat (3), the barrel (2); a client's also the
// direction (the floats' bits) and the position (quantised with the sender's settings).
class C_SeatWeaponShotMessage : public C_NetObjectMessage {   // deleting dtor and dtor: 0x004c1420 (DS 0x004a8330), 0x004c1440 (DS 0x004a8360)
public:
    C_SeatWeaponShotMessage() : C_NetObjectMessage(SEAT_WEAPON_MSG_SHOT) {}
    void Write(C_BitStream *stream) override;           // 0x004c1760
    void Read(C_BitStream *stream) override;            // 0x004c1810

    S_vector4 dir = {};                                 // +0x30
    S_vector4 pos = {};                                 // +0x40
    int seat = 0;                                       // +0x50
    int barrel = 0;                                     // +0x54
};

// A reload of a seat weapon (type 1; vtable 0x00810608, 0x34 bytes), likewise: the type (3 bits), the
// seat (3).
class C_SeatWeaponReloadMessage : public C_NetObjectMessage {   // deleting dtor and dtor: 0x004c1480 (DS 0x004a83d0), 0x004c14a0 (DS 0x004a8400)
public:
    C_SeatWeaponReloadMessage() : C_NetObjectMessage(SEAT_WEAPON_MSG_RELOAD) {}
    void Write(C_BitStream *stream) override;           // 0x004c1920
    void Read(C_BitStream *stream) override;            // 0x004c1970

    int seat = 0;                                       // +0x30
};

// The weapon's rounds as the server sends them to the clients (type 2; vtable 0x00810680, 0x38
// bytes; queued on the car's net object by C_Car::SetSeatOccupant).
class C_SeatWeaponRoundsMessage : public C_NetObjectMessage {   // deleting dtor and dtor: 0x004c16a0 (DS 0x004a86c0), 0x004c16c0 (DS 0x004a86f0)
public:
    C_SeatWeaponRoundsMessage() : C_NetObjectMessage(SEAT_WEAPON_MSG_ROUNDS) {}
    // The type (3 bits), the seat (3), the rounds (10).
    void Write(C_BitStream *stream) override;           // 0x004c1890
    void Read(C_BitStream *stream) override;            // 0x004c18f0

    int rounds = 0;                                     // +0x30
    int seat = 0;                                       // +0x34
};

// An ammo item the server added to the vehicle's store for the clients (type 3; vtable 0x00810658,
// 0x38 bytes; queued on the car's net object by C_Car::Tick).
class C_SeatWeaponAmmoMessage : public C_NetObjectMessage {   // deleting dtor and dtor: 0x004c1660 (DS 0x004a8650), 0x004c1680 (DS 0x004a8680)
public:
    C_SeatWeaponAmmoMessage() : C_NetObjectMessage(SEAT_WEAPON_MSG_AMMO) {}
    // The type (3 bits), the uid (16, read back sign-extended), the item's type (8).
    void Write(C_BitStream *stream) override;           // 0x004c16d0
    void Read(C_BitStream *stream) override;            // 0x004c1730

    int typeId = 0;                                     // +0x30 the item's
    int uid = 0;                                        // +0x34
};
