// C_Car: the car actor ("Car", vtable 0x00810778; 0x25C0 bytes, 16-byte aligned). Original source
// 0x004d0590..0x004d57a0 (the actor), 0x004d9050..0x004dab20 (controls, seats, cameras),
// 0x004db060..0x004dec60 (messages, state, saves) and 0x004e0010..0x004e2880 (the damage parts);
// ported in part 2 (game/car.cpp and its siblings).
//
// The car embeds its vehicle (C_Vehicle at +0x90, game/vehicle.h) and keeps its own members from
// +0x2360. As with the other actors the port is not byte-exact: the `+0xNNN` comments are the
// original offsets and everything is reached by name.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/perf_timer.h"
#include "game/vehicle.h"
#include "net/bitstream.h"                              // S_NetQuantization

class C_BitStream;
class C_chunk;
class C_Human;
class C_Item;
class C_ItemDef;
class C_NetObject;
class C_SeatWeapon;
class I3D_model;

// An explosive part of the damage model (an "EXPL_D*" visual; 0x14 bytes, _aligned_malloc'd): hit
// once, it starts its effect and the car's fuse.
struct S_CarExplosive {
    I3D_frame *frame;                                   // +0x00
    int fuse;                                           // +0x04 ms to the wreck once hit ("EXPL<s>" x 1000; 0: none)
    int effect;                                         // +0x08 its particle effect's handle (-1 none)
    int effectId;                                       // +0x0C the effect ("PARTID<n>"; -1 none)
    int started;                                        // +0x10 hit (the saved game keeps its first byte)
};

// A damage zone of the damage model (a "D_ZONE*" visual; 0x0C bytes): hits wear its points down.
struct S_CarZone {
    I3D_frame *frame;                                   // +0x00
    int hp;                                             // +0x04 hit points left ("HP<n>")
    int material;                                       // +0x08 its g_materials index ("matid<n>"; -1 none)
};

// The car's damage parts (C_Car +0x24E0; 0x68 bytes; game/car_parts.cpp): the damage model
// ("DM" + the model's file name, a hidden copy placed on the car for hit tests), its damage zones,
// explosive parts, the turrets' frames it turns with the car's, the seats' hit boxes, and the
// wreck's fire, smoke and sound.
struct S_CarParts {                                     // its vectors' implicit dtor: 0x004d0ec0 (DS 0x004bda00)
    S_CarParts();                                       // 0x004e08c0
    // The damage model of `owner`'s model loaded (none when there is no such file) and its parts
    // gathered, hidden.
    void Setup(class C_Car *owner);                     // 0x004e0930
    // The mission's start (StartMission, RestoreModel): the parts gathered again whole, no fuse,
    // not wrecked, the wreck's fire and smoke destroyed. (Quirk kept: their handles stay set; a
    // later Reset destroys them again, which then misses.)
    void Reset();                                       // 0x004e0ea0 (__fastcall) (DS 0x004d06c0)
    // The car's end: the model released, the explosive parts freed (their effects left), the
    // turrets' frames forgotten.
    void Release();                                     // 0x004e0ee0
    // The fuse counted down (not once wrecked): at its end the car is wrecked (WreckOnce).
    void Tick(int time);                                // 0x004e1990
    // A hit of the car (its Hit: bullets, projectiles, explosions and script kills; not once
    // wrecked, nor without the model). The model placed, the explosive parts hidden, the seats'
    // boxes shown where someone sits; the frame hit: an explosion's the zone nearest `pos` (a tie:
    // the earlier), else the model's frame the ray along `dir` meets (Quirk kept: from pos - 2 dir,
    // the start moved back twice). A seat's box: its occupant takes it (HIT_BULLET). Not a zone:
    // the seats' boxes on the ray (HitSeats). A zone loses what its material does not absorb; below
    // 0 HP it breaks: the rest to the seats' boxes, then a bullet's explosive part on the ray (not
    // yet started) starts its fuse (the car's the earliest) and effect (SendPartEffect), one without
    // a fuse or any other hit wrecks the car (WreckOnce); a bullet meeting no part does no more.
    void Hit(int type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker);   // 0x004e1a20
    // The material a shot along `dir` to `pos` meets on the model (the explosive parts hidden, the
    // seats' boxes shown where someone sits): the frame's "matid", a seat's box flesh (0x42); -1
    // without the model or a hit.
    int GetHitMaterial(const S_vector &pos, const S_vector &dir);   // 0x004e1530
    // A client's wreck (CAR_MSG_DESTROYED): the explosive parts' effects destroyed, the fire and
    // smoke started at the particle dummy (else the car's frame) with the car's frame their parent,
    // the wreck's sound there (SID; SOUND_CHAPTER_EXPLOSION, silent to the AI).
    void StartWreckEffects();                           // 0x004e2000 (__fastcall) (DS 0x004d1c90)
    // A damage part's effect (the server's CAR_MSG_PART_EFFECT, sent from Hit): the explosive
    // part's PARTID effect at its frame, the car's frame its parent. Fix: a part past the list is
    // ignored (the original indexes by the message's number). (Quirk kept: one running is not
    // destroyed first.)
    void StartPartEffect(int part);                     // 0x004e1f70 (DS 0x004d1bf0)
    // The saved game (the car's CHUNK_CAR_PARTS): each explosive part (from the last: its index,
    // started, fuse, effect), each zone (from the last: its index, HP), then wrecked and the fuse.
    // Read back: a part's index out of range is passed over (Fix: the count itself too; the
    // original reads one past the list). (Quirks kept: such a chunk, and an unknown one, is not
    // left, its contents read next; an index that cannot be read is the chunk before's; the effects'
    // handles come back as saved.)
    bool SaveGame(C_chunk &chunk);                      // 0x004e0f70
    bool LoadGame(C_chunk &chunk);                      // 0x004e1230

    // The int after `key` in the frame's user properties ("HP5000"), -1 without the frame or key.
    static int GetFrameInt(const I3D_frame *frame, const char *key);   // 0x004e19c0

    I3D_model *model = nullptr;                         // +0x00 the damage model (hidden)
    class C_Car *car = nullptr;                         // +0x04
    I3D_frame *particleFrame = nullptr;                 // +0x08 the damage model's "particle" dummy (the wreck's effects)
    int fuse = -1;                                      // +0x0C ms to the wreck (0: none; -1 when made, Reset clears it)
    int fireEffect = -1;                                // +0x10 the wreck's fire ("PFIRE", else 0x60)
    int smokeEffect = 0;                                // +0x14 its smoke ("PSMOKE", else 0x5F; not set by the original's constructor)
    int wreckSound = -1;                                // +0x18 its sound ("SID"; below 1: none)
    int fireHandle = -1;                                // +0x1C the wreck's fire's particle handle
    int smokeHandle = -1;                               // +0x20 its smoke's
    std::vector<S_CarExplosive *> explosives;           // +0x24 ("EXPL_D*", from the model's last frame)
    std::vector<I3D_frame *> turretFrames;              // +0x34 ("HOR*" / "VERT*": turned as the car's namesakes)
    std::vector<S_CarZone> zones;                       // +0x44 ("D_ZONE*")
    std::vector<I3D_frame *> seatFrames;                // +0x54 per seat its "SEAT<nn>" hit box (null: none)
    bool wrecked = false;                               // +0x64 the parts wrecked the car, or Wreck did (no more hits, no fuse)

private:
    // The damage model's parts and the wreck's effects (Setup, Reset).
    void Gather();                                      // 0x004e09d0 (__fastcall)
    // Forgets the parts, destroying the explosive parts' effects.
    void Clear();                                       // 0x004e0dc0 (__fastcall) (DS 0x004d05b0)
    // The car wrecked, once (the fuse's end, Hit).
    void WreckOnce();                                   // 0x004e1fe0 (__fastcall)
    // The damage model put on the car (its world place and turn, in the scene's primary sector),
    // each turret frame turned as the car's namesake. Fix: a turret frame without one is left (the
    // original reads through null).
    void Place();                                       // 0x004e1dc0 (__fastcall)
    // The explosive parts hidden; the seats' boxes shown where someone sits (inline in Hit and
    // GetHitMaterial).
    void HideExplosives();
    void ShowOccupiedSeats();
    // The explosive part (from the last) the ray from `from` along `dir` meets, -1 none (the model
    // shown for it; Quirk kept: on a hit the model is left shown, as each part tested).
    int ExplosiveAt(const S_vector &from, const S_vector &dir);   // 0x004e1820
    // Each seat's occupant whose box the ray meets takes HIT_BULLET of `amount` (at `from`).
    void HitSeats(const S_vector &from, const S_vector &dir, float amount, C_Actor *attacker);   // 0x004e18c0
    // The car tells the clients to start a damage part's effect (CAR_MSG_PART_EFFECT).
    void SendPartEffect(int part);                      // 0x004e1f40
};

// A message of the car to the other machines (an actor message; the kinds with data have their
// classes): the kind in CAR_MSG_KIND_BITS. The server's reach the clients' OnMessage, a client's the
// server's Unknown20.
class C_CarMessage {
public:
    explicit C_CarMessage(int kind) : m_kind(kind) {}
    virtual ~C_CarMessage() = default;                  // (not in the original's vtables)
    virtual void Read(C_BitStream * /*stream*/) {}      // 0x00 (0x004e0760, 0x004e07b0)
    virtual void Write(C_BitStream *stream);            // 0x04 (0x004e0770, 0x004e07c0)
    int GetKind() const { return m_kind; }

protected:
    int m_kind;                                         // +0x04
};
// The kinds of the car's messages (4 bits: log2(12) + 1).
constexpr int CAR_MSG_KIND_BITS = 4;
enum : int {
    CAR_MSG_GLASS = 0,                                  // a shot hit a glass part (C_CarGlassMessage)
    CAR_MSG_PART = 1,                                   // a shot hit another damage part (C_CarPartMessage)
    CAR_MSG_WHEEL = 2,                                  // a wheel came off (C_CarWheelMessage)
    CAR_MSG_TYRE = 3,                                   // a shot hit a tyre (C_CarTyreMessage)
    CAR_MSG_DESTROYED = 4,                              // the car was wrecked (vtable 0x008108c0)
    CAR_MSG_PART_EFFECT = 5,                            // a damage part's effect (C_CarPartEffectMessage; part 2g)
    CAR_MSG_RESTART = 6,                                // the sunk / wrecked car's timer ran out (vtable 0x00810868)
    CAR_MSG_STORE = 7,                                  // the server's item store of the car (Tick; WriteStore)
    CAR_MSG_DEADLY_GROUND = 8,                          // the car reached HUMAN_ENV_DEADLY ground (vtable 0x00810874)
    CAR_MSG_HIT = 9,                                    // a client's car hurt someone (C_CarHitMessage)
    CAR_MSG_FUEL = 10,                                  // the fuel (C_CarFuelMessage; SendFuel)
    CAR_MSG_IMPULSE = 11,                               // a push for the machine that simulates the car (C_CarImpulseMessage)
};

// CAR_MSG_GLASS (vtable 0x008108e0; Hit): the part, its health (truncated) and the shot's direction.
class C_CarGlassMessage : public C_CarMessage {
public:
    C_CarGlassMessage() : C_CarMessage(CAR_MSG_GLASS) {}
    C_CarGlassMessage(int part, int health, const S_vector &dir)
        : C_CarMessage(CAR_MSG_GLASS), part(part), health(health), dir{dir.x, dir.y, dir.z, 0.0f}
    {
    }
    // The part and the health (10 bits each), the direction (C_NetMessage::WriteDirection3D).
    void Read(C_BitStream *stream) override;            // 0x004e0340
    void Write(C_BitStream *stream) override;           // 0x004e03a0

    int part = 0;                                       // +0x08
    int health = 0;                                     // +0x0C
    S_vector4 dir = {};                                 // +0x10 (w not sent)
};

// CAR_MSG_PART (vtable 0x008108d8; Hit): the part, its health and, at none left, the velocity it
// flies off at and the push.
class C_CarPartMessage : public C_CarMessage {
public:
    C_CarPartMessage() : C_CarMessage(CAR_MSG_PART) {}
    C_CarPartMessage(int part, int health, const S_vector &velocity, const S_vector &push)
        : C_CarMessage(CAR_MSG_PART), part(part), health(health), velocity{velocity.x, velocity.y, velocity.z, 0.0f},
          push{push.x, push.y, push.z, 0.0f}
    {
    }
    // The part and the health (10 bits each); at 0 or less the velocity and the push as positions.
    // (Quirk kept: a negative health would write them but read back unsigned, without them; Hit
    // clamps it to 0.)
    void Read(C_BitStream *stream) override;            // 0x004e0410
    void Write(C_BitStream *stream) override;           // 0x004e04b0

    int part = 0;                                       // +0x08
    int health = 0;                                     // +0x0C
    S_vector4 velocity = {};                            // +0x10 (w not sent)
    S_vector4 push = {};                                // +0x20 (w not sent)
};

// CAR_MSG_TYRE (vtable 0x008108d0; Hit): the wheel (8 bits).
class C_CarTyreMessage : public C_CarMessage {
public:
    C_CarTyreMessage() : C_CarMessage(CAR_MSG_TYRE) {}
    explicit C_CarTyreMessage(int wheel) : C_CarMessage(CAR_MSG_TYRE), wheel(wheel) {}
    void Read(C_BitStream *stream) override;            // 0x004e0630
    void Write(C_BitStream *stream) override;           // 0x004e0650

    int wheel = 0;                                      // +0x08
};

// CAR_MSG_IMPULSE (vtable 0x00810880; OnCollision): where another actor pushed the car (a
// position) and who it was (net id, 10 bits); at most one a second.
class C_CarImpulseMessage : public C_CarMessage {
public:
    C_CarImpulseMessage() : C_CarMessage(CAR_MSG_IMPULSE) {}
    C_CarImpulseMessage(const S_vector4 &point, int actor) : C_CarMessage(CAR_MSG_IMPULSE), point(point), actor(actor) {}
    void Read(C_BitStream *stream) override;            // 0x004e0800
    void Write(C_BitStream *stream) override;           // 0x004e0830

    S_vector4 point = {};                               // +0x10 (w not sent)
    int actor = 0;                                      // +0x20
};

// CAR_MSG_WHEEL (vtable 0x008108b0; CarOnWheelOff): the wheel, its health (the wheel's,
// truncated; 0 when it came off) and the velocity it flew off at.
class C_CarWheelMessage : public C_CarMessage {
public:
    C_CarWheelMessage() : C_CarMessage(CAR_MSG_WHEEL) {}
    C_CarWheelMessage(int wheel, int health, const S_vector &velocity)
        : C_CarMessage(CAR_MSG_WHEEL), wheel(wheel), health(health), velocity{velocity.x, velocity.y, velocity.z, 0.0f}
    {
    }
    // The wheel (8 bits), the health (10 bits) and at 0 or less the velocity, quantised as a
    // position (C_NetMessage::WritePosition). (Quirk kept: a negative health would write the
    // velocity but read back unsigned, without it; the vehicle clamps it to 0 first.)
    void Read(C_BitStream *stream) override;            // 0x004e0540
    void Write(C_BitStream *stream) override;           // 0x004e05b0

    int wheel = 0;                                      // +0x08
    int health = 0;                                     // +0x0C
    S_vector4 velocity = {};                            // +0x10 (w not sent)
};

// CAR_MSG_PART_EFFECT (vtable 0x008108c8; the damage parts' 0x004e1f40): the part (4 bits).
class C_CarPartEffectMessage : public C_CarMessage {
public:
    C_CarPartEffectMessage() : C_CarMessage(CAR_MSG_PART_EFFECT) {}
    void Read(C_BitStream *stream) override;            // 0x004e06a0
    void Write(C_BitStream *stream) override;           // 0x004e06c0

    int part = 0;                                       // +0x08
};

// CAR_MSG_FUEL (vtable 0x008108b8; SendFuel): the fuel, 0..5000 l in 16 bits (log2(50000) + 1;
// read back clamped).
class C_CarFuelMessage : public C_CarMessage {
public:
    C_CarFuelMessage() : C_CarMessage(CAR_MSG_FUEL) {}
    explicit C_CarFuelMessage(float fuel) : C_CarMessage(CAR_MSG_FUEL), fuel(fuel) {}
    void Read(C_BitStream *stream) override;            // 0x004e0040
    void Write(C_BitStream *stream) override;           // 0x004e00f0

    float fuel = 0.0f;                                  // +0x08 l
};

// CAR_MSG_HIT (vtable 0x0081088c): a client's car ran a human over (CarOnBodyActorImpulse: its net
// id, 1000) or hurt its own crew (CarOnVehicleImpact: -1, the damage); the server hurts them (part
// 2f).
class C_CarHitMessage : public C_CarMessage {
public:
    C_CarHitMessage() : C_CarMessage(CAR_MSG_HIT) {}
    C_CarHitMessage(int human, int amount, const S_vector &impulse, const S_vector &point)
        : C_CarMessage(CAR_MSG_HIT), human(human), amount(amount), impulse{impulse.x, impulse.y, impulse.z, 0.0f},
          point{point.x, point.y, point.z, 0.0f}
    {
    }
    // The human and the amount (16 bits each, read back sign-extended: an amount over 32767 turns
    // negative), the impulse and the point (32-bit floats).
    void Read(C_BitStream *stream) override;            // 0x004e0180
    void Write(C_BitStream *stream) override;           // 0x004e0270

    int human = -1;                                     // +0x08 net id; -1: the crew
    int amount = 0;                                     // +0x0C
    S_vector4 impulse = {};                             // +0x10 (w not sent)
    S_vector4 point = {};                               // +0x20 (w not sent)
};

// C_Car::IsDrivable's checks.
enum : uint32_t {
    CAR_DRIVABLE_INTACT = 0x1,                          // engine (+0x610), gearbox (+0x63C) and fuel left, no wheel lost
    CAR_DRIVABLE_ON_WHEELS = 0x2,                       // below 1 m/s: two wheels on the ground, upright (0.6)
    CAR_DRIVABLE_BOTH_SIDES = 0x4,                      // a wheel on the ground on each side
};

// C_Car::m_restrictions (+0x24A8; StartMission clears it, nothing in this exe sets it): what the
// controls (ApplyControls, SetInput) leave the driver. The steering (axis 3 - axis 2) is positive
// to the right (inferred).
enum : uint32_t {
    // The car pulls to the left (right): a steering to that side is dropped, and then with the
    // throttle on it turns fully to that side at 30% throttle (a steering to the other side
    // stays, at full throttle). Both: no throttle, no steering.
    CAR_PULLS_LEFT = 0x1,
    CAR_PULLS_RIGHT = 0x2,
    CAR_NO_THROTTLE = 0x4,
};

// A checkpoint the car marked (C_Car::m_marks; heap-allocated 4-byte records in the original).
struct S_CarCheckpointMark {
    uint16_t checkpoint;
    // Set by MarkCheckpoints (CHECKPOINT_FLAG_CAR_0200); without it ClearMarks counts the point's
    // +0x1A down instead (none in this exe).
    bool flagged;
};

class C_Car : public C_Actor {                          // "Car", vtable 0x00810778, 0x25C0 bytes
public:
    C_Car();                                            // 0x004d0b60 (allocated by the creator 0x004d04f0)
    ~C_Car() override;                                  // 0x004d0f20 (0x004d0ea0 scalar-deleting)

    // --- C_Actor's slots -----------------------------------------------------------------------
    int GetHitPoints() override { return 500; }         // 0x10 0x004d0e20
    // Every frame (game/car_update.cpp): the simulation (single player; network games: the
    // replication), the seat weapons, the actor's place, the seat cameras, the checkpoint marks
    // every 500 ms of driving, the model's animation. At most 83 ms.
    void Update(int frameTime) override;                // 0x14 0x004d3110
    // Every 100 ms: the sunk car's timer, a charge, the server's item store, the water, the seat
    // weapons and their ammo (network games), the damage parts, the crew bailing out of a car on
    // its side, the engine's noise for the AI.
    void Tick(int timeDelta) override;                  // 0x18 0x004d2790 (DS 0x004bf910)
    // The server's message (game/car_net.cpp): a glass or damage part's health (a part at none
    // comes off), a wheel's on a client (at none off, not in mode +0x550), a tyre burst, the wreck
    // (the parts' effects, ShowDestroyed), a part's effect, the restart, the item store (added on a
    // client), the fuel, a push. Fix: a part or wheel past the car's (or missing) is ignored.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x004db360 (DS 0x004c92e0)
    // A client's message to the server: a wheel's health, the deadly ground (Wreck(true), the
    // parts wrecked), a hit of its car (HIT_THROWN_OBJECT by the driver: on every living
    // occupant for -1, else on the human), the fuel (sent on to the clients), a push.
    void Unknown20(C_BitStream *msg) override;          // 0x20 0x004db760
    // Clears the seat weapons, binds the frame, sets the vehicle up from the model (false: not a
    // car model), hands the vehicle the car's callbacks and sets the damage parts up.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x004d10a0 (DS 0x004bdc60)
    // The parameter block as the mission file keeps it (version 3, the packed blob's size, the
    // blob; game/vehicle_params.h).
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x004d1280 (DS 0x004bdef0)
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x004d55c0
    // The source's frame; a car's actor state and parameter block.
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x004d1220
    // The mission's start (game/car_mission.cpp): no wreck, charge or attacker, the actor active,
    // the start place kept; network games: the net object (the state handlers, the seat weapons'
    // messages, the quantisations from the top speed, the place). Then, unless single player's
    // server side (`server`) or already started, the vehicle once: started at the actor's place,
    // the volumes, full fuel, the lights on automatic, the seats' "SEAT<digits>" flags, "name<n>"
    // texts, weapons and "camera3RD*" keepers, the checkpoints marked, the wreck's model ("C" +
    // the model's file name) loaded, 20 ms of settling.
    void StartMission(bool server) override;            // 0x3C 0x004d16a0 (DS 0x004be420)
    // The model back, the wreck's model released, the net object removed, the seat weapons
    // stopped and deleted, every seat left and its camera keeper put back; the vehicle stopped
    // (once started, or single player's client side); the actor inactive, the marks cleared.
    void StopMission(bool server) override;             // 0x40 0x004d25c0
    // The car back as the mission started: the model back, every seat left (its weapon
    // restarted), the vehicle reset to the start place and settled (5 ms) with full fuel, no
    // attacker, driver or charge; the volumes and the replicated place as at the start. (Quirk
    // kept: the vehicle's Restart then puts the volumes back in the world, a client's too.)
    void RestartMission(bool server) override;          // 0x44 0x004dd0d0 (DS 0x004cb800)
    // The saved game (game/car_save.cpp): C_Actor's, the vehicle's state block, each seat
    // weapon's (from the last seat), the wreck's two flags, the periodic link and the last
    // attacker, the damage parts', the placed charge. (Quirk kept: SaveGame returns false;
    // nothing reads it.) LoadGame makes the charge's visual again and shows the wreck (its glass
    // shattered out of sight).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x004dd350
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x004dd750 (DS 0x004cbff0)
    // A joining client's state (game/car_net.cpp): the frame's place and rotation (axis and angle;
    // raw floats), the item store, the engine's health (+0x610), the wreck shown, each wheel
    // (from the last) off, the fuel as its message's. Read back: the place and rotation to the
    // frame and the net property's tracks, the items added, a shown wreck shown with the engine
    // off at once, the wheels off thrown off (ThrowWheel).
    void WriteState(C_BitStream *msg) override;         // 0x50 0x004ddf90
    void ReadState(C_BitStream *msg) override;          // 0x54 0x004de270
    // Another actor ran into the car at `pos` (game/car_hits.cpp; a3 / a4 / a5: its push, the
    // normal, its mass as float bits). Where the car is simulated: a human's push x 500 as a
    // force (only the turn, at the centre of mass), another car's through ApplyCarHit (x 1.2;
    // half the mass and 1.4 times the push for a destroyed car when both are in mode +0x550).
    // Elsewhere the push goes to the simulating machine (a client: CAR_MSG_IMPULSE at most once a
    // second; else TakePush). (The original returns true.)
    void OnCollision(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6) override;   // 0x58 0x004d3ef0
    // The seats (game/car_seats.cpp): the vehicle's (S_VehicleSeat, +0xFA8), their weapons.
    bool IsSeatBlocked(int seat) override;              // 0x6C 0x004d93c0 (DS 0x004c6b30)
    bool SetSeatBlocked(int seat, bool blocked) override;   // 0x70 0x004d93e0 (DS 0x004c6b60)
    // Seats `occupant` (a human; a seat taken by another is left alone): out of the seats it
    // has, linked to the seat's frame facing the seat's z axis; the driver's seat makes it the
    // driver and starts the engine. The server tells the clients the seat weapon's rounds.
    void SetSeatOccupant(int seat, C_Actor *occupant) override;   // 0x74 0x004d9430 (DS 0x004c6bd0)
    C_Actor *GetSeatOccupant(int seat) override;        // 0x78 0x004d99d0 (DS 0x004c7290)
    int FindSeat(const C_Actor *occupant) override;     // 0x7C 0x004d9a00 (DS 0x004c72d0)
    C_Actor *LeaveSeat(int seat) override;              // 0x80 0x004d9a40 (DS 0x004c7320)
    bool ExitSeat(int seat) override;                   // 0x84 0x004d9be0 (DS 0x004c7530)
    int GetSeatCount() override;                        // 0x88 0x004d0e30
    uint32_t GetSeatFlags(int seat) override;           // 0x8C 0x004d9fc0 (DS 0x004c79d0)
    // The occupant's camera dummy ("camera3RD*", "cameraFPV*" in first person or for an "NO3RD"
    // seat); the driver's, a hidden occupant's and a gunner's are the seat dummy's (or its
    // parent's).
    I3D_frame *GetSeatCamera(int seat) override;        // 0x90 0x004d9e10
    int GetSeatText(int seat) override;                 // 0x94 0x004d9ff0 (DS 0x004c7a10)
    bool HasSeatWeapon(int seat) override;              // 0x98 0x004da020 (DS 0x004c7a50)
    int GetSeatWeaponRounds(int seat) override;         // 0x9C 0x004da040 (DS 0x004c7a80)
    S_vector GetSeatWeaponAim(int seat) override;       // 0xA4 0x004da150 (DS 0x004c7be0)
    S_vector GetSeatWeaponOrigin(int seat) override;    // 0xA8 0x004da110 (DS 0x004c7b90)
    float UnknownAC(int seat) override;                 // 0xAC 0x004da190 (DS 0x004c7c30)
    int GetSeatWeaponText(int seat) override;           // 0xB0 0x004da1b0 (DS 0x004c7c60)
    C_ItemDef *GetSeatWeaponDef(int seat) override;     // 0xB4 0x004da1d0 (DS 0x004c7c90)
    void GetSeatWeaponLimits(int seat, float *a, float *b, float *c, float *d) override;   // 0xB8 0x004da1f0 (DS 0x004c7cc0)
    bool CanSeatWeaponAim(int seat, float *pos) override;   // 0xBC 0x004da240 (DS 0x004c7d30)
    void ApplySeatControls(int seat, float *axes, float *buttons) override;   // 0xC0 0x004da060 (DS 0x004c7ab0)
    void ShowSeatView(int seat, bool on) override;      // 0xC4 0x004da090 (DS 0x004c7af0)
    bool UnknownC8(int seat, int *a, int *b, int *c) override;   // 0xC8 0x004da270 (DS 0x004c7d70)
    bool IsSeatFirstPersonOnly(int seat) override;      // 0xCC 0x004da2a0 (DS 0x004c7db0)
    void UnknownD0(int, int, int, int) override {}      // 0xD0 0x004dba00
    // A shot or blast (game/car_hits.cpp): not from an enemy without anyone hostile to them
    // aboard, nor an explosion on a wreck. The attacker remembered; the damage part the shot's ray
    // (10 x `dir` from `pos`) meets loses (amount less its absorbed share) x 0.2 health and the
    // clients are told (CAR_MSG_GLASS / CAR_MSG_PART with the part's velocity and the push), a tyre
    // hit beyond 70% of the radius too (CAR_MSG_TYRE); then the damage parts' own (part 2g).
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x004dba10
    // The frame's "matid" (11 without one).
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x004dbee0 (DS 0x004ca150)

    // --- the car's own -------------------------------------------------------------------------
    // The engine's health (the vehicle's +0x610) is 0.
    bool IsDestroyed() const;                           // 0x004ddcf0
    // Wrecked (once; m_wrecked): CAR_MSG_DESTROYED, network games (not co-op nor
    // MP_GAME_OBJECTIVES) start m_respawnTimer, the lights off, the engine dead without fuel, the
    // script's OnDeath, every occupant killed (HIT_THROWN_OBJECT -1) by the last attacker (`told`:
    // by the driver; a client told by the server) who gains 600 experience when one was hostile
    // to them; the charge's visual dropped; of the first six wheels each on its own coin flip
    // thrown off (not in mode +0x550); a dedicated server reloads the model at the restart; the
    // blast.
    void Wreck(bool told);                              // 0x004dbf00 (DS 0x004ca180)
    // Material of the part a shot from `pos` along `dir` hits, -1 if none (the damage parts' own
    // test, +0x24E0).
    int GetHitMaterial(const S_vector &pos, const S_vector &dir);   // 0x004dbed0
    C_Vehicle *GetVehicle() { return &m_vehicle; }      // +0x90
    // Is the car simulated on this machine: single player; the local human's own (Quirk kept:
    // also no driver where there is no local human); the server for a car without a driver or
    // with one who is not a player?
    bool IsSimulatedHere();                             // 0x004de800
    // The fuel to the other machines (CAR_MSG_FUEL; the server's to the clients, a client's to the
    // server).
    void SendFuel();                                    // 0x004db310
    float GetSpeed() const { return m_vehicle.m_speed; }    // m/s (+0x958)
    // Human in the driver's seat, null if none.
    C_Human *GetDriver() const { return m_driver; }     // 0x004d11f0 (+0x24AC)
    // Environment flags of the car (C_Human::GetEnvironmentFlags()): its grounded wheels' ground's
    // HUMAN_ENV_DEADLY / HUMAN_ENV_RESTRICTED.
    uint32_t GetEnvironmentFlags() const;               // 0x004dd090
    // The checks of CAR_DRIVABLE_* all pass.
    bool IsDrivable(uint32_t checks) const;             // 0x004d5430
    // `checkpoint` is among those the car marked.
    bool IsCheckpointMarked(int checkpoint) const;      // 0x004dab20
    // The body's box on the ground (x, z), 0.5 m wider each way, and the BODY frame: what the
    // meleeTracker's route goes round (C_MeleeTrackerAI::RouteAroundCars).
    void GetFootprint(I3D_frame **frame, float *min, float *max) const;   // 0x004dac00
    // A message to the other machines (CreateActorMessage / CreateActorMessageEx).
    void SendMessage(C_CarMessage &msg);                // 0x004db2b0
    void SendMessageEx(C_CarMessage &msg);              // 0x004db2e0
    // The vehicle's mode +0x550 (+0x5E0; the squad's shared enemy search skips the crews of cars
    // that have it set).
    int GetUnknown5E0() const { return m_vehicle.m_unknown550; }
    // A living occupant hostile to `human` (network games: a living one in play, not spectating,
    // of another team).
    bool HasEnemyAboard(const C_Human *human);          // 0x004dde20
    // The point of the car's model (hit zones, rendered geometry) 10 m along `dir` from `from` and
    // the normal towards `from`.
    bool RayTest(const S_vector4 &from, const S_vector &dir, S_vector4 *pos, S_vector4 *normal);   // 0x004dec60
    // A charge (`item`, behaviour 11) placed by `human` at `pos` (none placed yet): the car is
    // wrecked in 15 s, by `human`. Shown here: AttachCharge; a dedicated server removes the item.
    bool PlaceCharge(C_Item *item, C_Human *human, const S_vector4 &pos, const S_vector4 &normal);   // 0x004dead0 (DS 0x004cd8a0)
    // A copy of the item's model linked to the car at `pos` facing `normal`; the item removed.
    bool AttachCharge(C_Item *item, C_Human *human, const S_vector4 &pos, const S_vector4 &normal);  // 0x004deb50
    // The vehicle's top speed (+0x780; what C_Vehicle::SetUnknown4f2630 gets when the AI lets go:
    // the AI driver's top speed is it times the pace's factor).
    float GetUnknown780() const { return m_vehicle.m_topSpeed; }
    // The vehicle's velocity in m/s (+0x4A0: the AI driver looks 4 frames ahead along it).
    const S_vector &GetVelocity() const { return reinterpret_cast<const S_vector &>(m_vehicle.m_velocity); }
    // The throttle and steering of a living driver, under m_restrictions; applied in single
    // player or by the local driver's machine (true with a living driver). The AI driver's, and
    // 0, 0 when one gets out.
    bool SetInput(float throttle, float steering);      // 0x004d9280
    // The local driver's controls (C_HumanController's axes: 0 throttle, 1 brake, 2 / 3 left /
    // right, 18 handbrake, 61 the 0x004f23b0 curve's complement), under m_restrictions; starts the
    // engine (with 0.001 l of fuel); a dead driver lets everything go. (`buttons` is not used.)
    void ApplyControls(float *axes, float *buttons);    // 0x004d9050
    // A local player sits in one of the seats.
    bool HasLocalPlayerAboard();                        // 0x004dea90
    // The player's side may get in: not wrecked, on its wheels (CAR_DRIVABLE_ON_WHEELS), every
    // living occupant of the player's team (network games: of the local human's, those in play
    // and not spectating).
    bool CanEnter();                                    // 0x004ddd10
    // The side of the crew (from the last seat, the first living one in play, not spectating,
    // not neutral): 0 axis (an enemy, HUMAN_ROLE_NPC_TEAM_5), 1 allied (an allied NPC), a player's
    // team; 2 none.
    int GetCrewSide();                                  // 0x004ddee0
    // The way out of seat `seat` (and in): a point on the ground beside the car, on the seat's
    // side (behind it for SEAT_FLAG_200), facing the seat; false when a 0.25 m sphere cannot get
    // there or finds no ground within 2 m below.
    bool GetSeatEntry(int seat, S_vector4 *pos, S_vector4 *dir);   // 0x004da420
    // The car's driver (+0x24AC) and the vehicle's m_driven.
    void SetDriver(C_Human *driver);                    // 0x004d11c0
    void SetDriven(bool driven) { m_vehicle.m_driven = driven; }   // 0x004d1200 (DS 0x004bde30)
    // Puts `occupant` out at its seat's way out, else at another seat's (false: no room).
    bool PutOutside(C_Actor *occupant);                 // 0x004da2f0
    // The weapon of seat `seat`, null without one (and past the ten).
    C_SeatWeapon *GetSeatWeapon(int seat) const;
    // The number of entries: the vehicle's seats (+0x1040).
    int GetEntryCount() const { return static_cast<int>(m_vehicle.m_seats.size()); }
    // Seat `seat` has its own camera dummy keeper (the camera module's passenger view).
    bool HasSeatCamera(int seat) const { return m_vehicle.m_seats[seat].camera != nullptr; }   // 0x004def80
    void GetCameraOffset(S_vector *offset) const { m_vehicle.GetCameraOffset(offset); }

    // --- the members -----------------------------------------------------------------------------
    C_Vehicle m_vehicle;                                // +0x090
    C_NetObject *m_netObject = nullptr;                 // +0x2360 (network games)
    uint8_t m_unknown2364[0xC] = {};                    // +0x2364
    // Network games: the quantisations StartMission hands the net object, from the vehicle's top
    // speed v: [0] over (2v, 20, 2v) from (-v, -10, -v) in steps of (v / 100, 0.2, v / 100) (a
    // velocity's, inferred); [1] over (v / 10, v / 20, v / 10) from half of it below 0 in steps of
    // (0.01, 0.013, 0.01).
    S_NetQuantization m_quantizations[2] = {};          // +0x2370
    // Network games: started when the car sinks or is wrecked (period 38 s, StartMission); then
    // the car is restarted (CAR_MSG_RESTART).
    S_PerfTimer m_respawnTimer;                         // +0x2470
    // Where StartMission found the car (RestartMission puts it back; 0x004d3380 reads them).
    S_vector4 m_startPosition = {};                     // +0x2480
    S_vector4 m_startDirection = {};                    // +0x2490
    bool m_stateChanged = false;                        // +0x24A0 the server sends the item store next tick (CAR_MSG_STORE)
    // StartMission started the vehicle (once; StopMission stops it; SetFrame clears it).
    bool m_started = false;                             // +0x24A1
    // The wreck's model ("C" + the model's file name; StartMission, null when it did not load):
    // ShowWreck takes its meshes.
    I3D_model *m_wreckModel = nullptr;                  // +0x24A4
    uint32_t m_restrictions = 0;                        // +0x24A8 CAR_PULLS_* / CAR_NO_THROTTLE
    C_Human *m_driver = nullptr;                        // +0x24AC
    // (StartMission sets the charge's and the attacker's to -1; the original's constructor leaves them.)
    int m_lastAttacker = -1;                            // +0x24B0 net id of who last hit the car (-1: none; the charge's placer)
    C_SeatWeapon *m_seatWeapons[10] = {};               // +0x24B4 per seat (StartMission; part 3)
    int m_periodicLink = -1;                            // +0x24DC (PeriodicLinks_Add)
    S_CarParts m_parts;                                 // +0x24E0
    // The model shows the wreck's meshes (ShowWreck; a dedicated server's Wreck sets it without):
    // RestoreModel reloads the model's.
    bool m_wreckShown = false;                          // +0x2548
    bool m_deadlyReported = false;                      // +0x2549 CAR_MSG_DEADLY_GROUND sent (StartMission, RestartMission clear it)
    // Wrecked (Wreck; StartMission and the restart clear it): no more explosions' hits, full
    // simulation options wherever the camera is (UpdateDetail), nobody gets in.
    bool m_wrecked = false;                             // +0x254A
    I3D_frame *m_chargeVisual = nullptr;                // +0x254C the placed charge's copy of its model (AttachCharge)
    int m_chargeTime = -1;                              // +0x2550 ms to the charge's blast (-1: none)
    int m_chargeOwner = -1;                             // +0x2554 net id of who placed it
    S_vector4 m_chargePos = {};                         // +0x2560 (AttachCharge)
    S_vector4 m_chargeNormal = {};                      // +0x2570
    // Network games: the server allows vehicles (S_MultiplayerSettings::allowVehicles; StartMission,
    // RestartMission): without it a client's car is not solid.
    bool m_vehiclesAllowed = false;                     // +0x2580
    int m_impulseSent = 0;                              // +0x2584 GetPerfTimeMs of the last CAR_MSG_IMPULSE
    // Network games: per seat, when its occupant last left (GetPerfTimeMs) and who it was (net id;
    // -1 none; Update forgets them after 3 s). StartMission allocates them, StopMission frees them.
    int *m_seatLeftTimes = nullptr;                     // +0x2588
    int *m_seatLeftIds = nullptr;                       // +0x258C
    // The checkpoints marked around the car (redone every 500 ms of driving), and in single player
    // those ahead of it (CHECKPOINT_FLAG_UNDER_CAR).
    int m_marksTime = 0;                                // +0x2594 ms
    std::vector<S_CarCheckpointMark> m_marks;           // +0x2598
    std::vector<uint16_t> m_marksAhead;                 // +0x25A8

private:
    bool HasSeat(int seat) const { return seat >= 0 && seat < static_cast<int>(m_vehicle.m_seats.size()); }
    // The driver's seat left (LeaveSeat, ExitSeat): no driver, the engine off (at once if it was
    // not running), the vehicle's 0x004f2630 back to the top speed, the handbrake off.
    void StopDriving();
    // After an occupant left: single player keeps the vehicle awake once the car is empty
    // (VEHICLE_PHYSICS_KEEP_AWAKE, cleared while others remain).
    void UpdateKeepAwake();
    // Network games: who left `seat` and when (m_seatLeftIds / m_seatLeftTimes).
    void RememberLeaving(int seat, const C_Actor *occupant);
    // Network games: forgets who left the seats 3 s after.
    void ForgetLeaving();
    // How upright the model stands: the height of its y axis (inline in the original, summed
    // (z + x) * 0 + y).
    float GetUprightness() const;

    // The vehicle's update; the actor follows the model; the detail options.
    void UpdateVehicle(int time);                       // 0x004d38f0
    // The vehicle's simulation options by the active camera's distance (and the sounds on within
    // 100 m of a shown model).
    void UpdateDetail();                                // 0x004d3970 (DS 0x004c0ed0)
    // Under water: the engine dies and the crew drowns (network games start m_respawnTimer).
    void CheckWater();                                  // 0x004d2c50
    // The loudest playing engine sound's volume (0 with the engine off) and the engine sounds' far
    // range.
    void GetEngineNoise(float *volume, float *range) const;   // 0x004d2dc0
    void Restart() { RestartMission(true); }            // 0x004db2a0
    // The model's meshes back (game/car_mission.cpp): the damage parts released, no longer
    // wrecked; after a ShowWreck the model's file loaded again and each visual takes the mesh of
    // its namesake (the occupants unlinked meanwhile). Then the vehicle restarted with its parts.
    // (Quirk kept: when the model does not load the restart is skipped.)
    void RestoreModel();                                // 0x004dce40
    // The wreck shown (once): each visual takes the mesh of its namesake in m_wreckModel (the
    // occupants unlinked meanwhile). (Fix: nothing without the wreck's model; the original reads
    // through it.)
    void ShowWreck();                                   // 0x004dcd10
    // Each damage part's mesh is its visual's again (fastcall in the original).
    void RefreshPartMeshes();                           // 0x004dd040
    // The checkpoints around the car (within 15 m of the nearest within 5 m) marked
    // CHECKPOINT_FLAG_CAR_0200, then MarkAhead.
    void MarkCheckpoints();                             // 0x004db060
    void ClearMarks();                                  // 0x004dab60
    // Single player, at 0.1 m/s or more: the checkpoints (within 16 m) less than 2 m from the 10 m
    // ahead along the velocity marked CHECKPOINT_FLAG_UNDER_CAR.
    void MarkAhead();                                   // 0x004dac80
    void ClearAhead();                                  // 0x004dafe0
    // Network games (game/car_net.cpp). Where the car is driven (the local driver's machine; the
    // server for a car without a player driving): the place, the rotation (axis, angle) and the
    // velocity replicated, the vehicle stepped (where vehicles are not allowed it is kept 8 m under
    // its start place instead). Elsewhere (a client; the server for another's car): the
    // replicated place, rotation and velocity (with the speed) replayed, the model read again,
    // the wheels' contacts and the rear axle's velocities.
    void UpdateNetwork(int time);                       // 0x004d3380
    // The seat weapons' messages of the net object: shots and reloads (a client skips its
    // player's own seat; for a living gunner: the server acts and sends them on, a client shows
    // them), rounds, ammo for the store. Fix: a seat without a weapon is passed over.
    void ReadMessages();                                // 0x004d2ec0 (DS 0x004c01f0)
    // The item store's contents: each item's uid (16 bits) and type (8 bits), 0xFFFF last; read
    // back (`add`: the items made and added to the store, not emptied first; else skipped).
    void WriteStore(C_BitStream *msg);                  // 0x004de150
    void ReadStore(C_BitStream *msg, bool add);         // 0x004de1f0
    // A client's wreck (CAR_MSG_DESTROYED): the vehicle restarted at its speed (whole: the first
    // six wheels already off taken off again), ShutDown, a hop up where it is simulated, the
    // wreck's meshes, each damage part left (from the last) thrown 15..25 m/s out from the
    // model's place (at least 5 up): glass shatters, others come off on a coin flip; the
    // charge's visual released; the crew darkened (lighting 0.2 / 0.3, updated).
    void ShowDestroyed();                               // 0x004dc780
    // The damage part / wheel of a message's index, null past the car's (or a missing wheel).
    S_VehiclePart *PartAt(int index);
    S_VehicleWheel *WheelAt(int index);
    // CAR_MSG_WHEEL (OnMessage on a client, Unknown20): a wheel not yet broken takes the health; at
    // none it comes off (not in mode +0x550).
    void TakeWheelMessage(const C_CarWheelMessage &m);
    // The lights off (the original's inlined light setters, inline in Wreck and ShowDestroyed:
    // each leaving alone a light its automatic switch drives, those just cleared, and calling the
    // vehicle's callback 9), the engine off at once, no fuel, the engine's health 0.
    void ShutDown();
    // Throws wheel `index` off (CarOnWheelOff): pushed out from the centre of mass at 5..15 m/s
    // (upwards), spun up and out at 2..3 m/s (inline in Wreck and ReadState).
    void ThrowWheel(int index);
    // A placed charge's fuse: at its end the car is wrecked by its placer (the damage parts
    // wrecked).
    void UpdateCharge(int time);                        // 0x004dec10
    // The simulating machine's side of a push by the actor `id` at `point` (OnCollision, the
    // impulse message): only the local driver's machine, else the server (for a car with a driver
    // it tells that client, at most once a second): ApplyImpulse of the other's velocity x
    // min(its mass / this one's, 1) x this mass x 0.3. Fix: only another car pushes (the original
    // reads any actor as a car, past a human's 0x720 bytes).
    void TakePush(int id, const S_vector4 &point);      // 0x004d3d10
    // A shot at wheel `wheel` reached its tyre: the hit beyond 70% of the radius from the wheel's
    // centre (`axle`: from the axle, in the wheel frame's space); never in mode +0x550. (The
    // original returns 1 / 2.)
    bool IsTyreHit(int wheel, const S_vector &pos, bool axle) const;   // 0x004d3b70
    // After the wreck: every human of g_blastActors (never filled in this exe) within 5 m of the
    // model's place (0.5 above) with nothing between takes HIT_EXPLOSION, 1000 less 200 per metre,
    // at a random bone, by the last attacker.
    void BlastAround();                                 // 0x004dc580
};

// The car's creator (g_actorCreators[ACTOR_CAR]; 0x0089bd44, vtable 0x00810758).
class C_CarCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x004d04f0
    const char *GetName() override { return "Car"; }    // 0x004d0560 (DS 0x004bcd70) (0x0083bdc8)
    void Destroy(C_Actor *actor) override;              // 0x004d0570 (DS 0x004bcd90)
};

// Every car (0x0089bd34; the constructor adds, the destructor removes).
extern std::vector<C_Car *> g_cars;
// The actors a wrecked car's blast hurts (BlastAround; 0x0089b4cc): nothing in this exe fills it.
extern std::vector<C_Actor *> g_blastActors;

// The car's net object's state handlers (NetStateHandler; StartMission sets them, the context the
// car; game/car_net.cpp). Every 300 ms (one timer for every car, Quirk kept) the sliding and the
// locked wheels (their indices, the wheel count last), the gear, the steering (6 bits) and the
// revs (8 bits of the rev limit; the writer clamps the vehicle's to it); the reader applies them
// while the net object's +0x3C is set (Quirk kept: it clears the wheels' flags all the same). Fix:
// a missing wheel is passed over, an index past the wheels ignored.
void __fastcall CarNetReadState(void *car, C_BitStream *stream);    // 0x004d0610
void __fastcall CarNetWriteState(void *car, C_BitStream *stream);   // 0x004d08c0

// The car's vehicle callbacks (SetFrame hands them to the vehicle; `context` / `vehicle` is the car;
// game/car_callbacks.cpp). Signatures as the vehicle's T_Vehicle*Callback types.
// A moving actor's side of a body contact: another car's vehicle answers (the body takes 1.2 times
// the impulse back, both push); a human gives way (no mass: no impulse, no push); others: none.
bool __fastcall CarOnBodyActorResponse(void *vehicle, S_CollisionVolume *hit, const S_vector4 *point,
                                       const S_vector *normal, S_vector4 *arm, S_vector4 *velocity, float *mass,
                                       S_vector4 *turn, float *scale, bool *push);   // 0x004d4130
// The impulse for the actor of the volume's frame: OnCollision for a car, OnCollisionEx (the car's
// velocity, its driver) for others; a human at 4 m/s or more also takes 1000 damage (network
// games: no contact with humans within 3 s of anyone leaving a seat).
void __fastcall CarOnBodyActorImpulse(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                      const S_vector4 *impulse, const S_vector *normal, float mass);   // 0x004d4230
// A human the car met as the world is pushed away from the car's velocity (below 2 m/s only at
// the front or the back).
void __fastcall CarOnBodyActorTouch(void *context, S_CollisionVolume *hit, const S_vector4 *point,
                                    const S_vector4 *velocity, const S_vector *normal);   // 0x004d4450
// The world: what is not an actor, most actors, a human below 4 m/s (not standing 0.3 m over the
// car's position), a meleeTracker below 4 m/s; not cars, mines, items, debris and a few markers.
// (`deform` is left alone.)
bool __fastcall CarIsActorFirm(void *context, S_CollisionVolume *hit, bool *deform);   // 0x004d4620
// A hard impact (the impulse per kg times 5 against the world, 3 against an actor; on the roof 15
// or 1000) over 20 hurts the crew, the driver the culprit; a client tells the server. Worked out
// where the car is driven: single player, the local driver's client, else the server.
void __fastcall CarOnVehicleImpact(void *context, const S_vector *impulse, const S_vector *largest,
                                   const S_vector4 *point, float radius, const S_vector *velocity, C_Actor *actor,
                                   const S_CollisionVolume *volume, int unknown);   // 0x004d4740 (DS 0x004c2020)
// A bumper, mirror, light, wing, plate or door is thrown off as debris (true; false for the
// others), hidden, and the last lamp on a part under it breaks.
bool __fastcall CarOnPartOff(void *context, int index, const S_vector *velocity, const S_vector *push);   // 0x004d4ca0 (DS 0x004c2700)
// The wheel is hidden (WHEEL_DETACHED; true) and thrown off as debris where it is shown; the
// server (or the driver's client) tells the others (CAR_MSG_WHEEL).
bool __fastcall CarOnWheelOff(void *context, int index, const S_vector *velocity, const S_vector *push);  // 0x004d4eb0 (DS 0x004c28d0)
// A shown glass part with its mesh shatters (GEffects) and is hidden.
void __fastcall CarOnGlassBroken(void *context, int part, const S_vector *velocity, const char *sound);   // 0x004d4a10
// Water (wading, swimming, cold) is not met; a physics object is hit and a destructible one broken
// (neither met); swinging objects are not met.
S_CollisionVolume *__fastcall CarOnBodyGround(void *context, S_CollisionVolume *hit, S_vector *hitPos,
                                              S_vector *normal);   // 0x004d4a70
// Water as the body's; on the server a mine is set off (not met), and in single player a minefield
// blows up under the wheel.
S_CollisionVolume *__fastcall CarOnWheelGround(void *context, S_CollisionVolume *ground, int wheel);   // 0x004d4b50
// Empty (RET 0x10 / RET; code without a function in Ghidra until part 2a).
void __fastcall CarOnPartHit(void *context, I3D_frame *frame, int type, const S_vector4 *point,
                             const S_vector *impulse, bool dented);   // 0x004d49f0
void __fastcall CarOnRepaired(void *context);           // 0x004d4a00

// (GetPerfTimeMs, InitPerfTime: game/perf_timer.h.)
