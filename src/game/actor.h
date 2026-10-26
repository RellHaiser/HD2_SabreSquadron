// Actors — game objects bound to a scene frame (humans, vehicles, doors, ...).
//
// Every actor class registers a creator object (vtable right after the class
// vtable: Create, GetName, Destroy) in g_actorCreators under its E_ActorType; the
// names below are the strings those creators return. C_Actor is ported
// (actor.cpp); of the actor classes only the members used by ported code are
// declared, with the addresses of their implementations (see src/tier1).
#pragma once

#include <cstdint>

#include "common/c_str.h"
#include "ls3d/i3d.h"

class C_BitStream;
class C_chunk;
class C_Item;
class C_ItemContainer;

enum E_ActorType {
    ACTOR_MINE = 2,               // "Mine"            C_Mine            vtable 0x00812f08
    ACTOR_HUMAN = 3,              // "Human"           C_Human           vtable 0x008111e0
    ACTOR_CAR = 4,                // "Car"             C_Car             vtable 0x00810778
    ACTOR_BARREL = 5,             // "Barrel"          C_Barrel          vtable 0x00813220
    ACTOR_DOOR = 6,               // "Door"            C_Door            vtable 0x00812ff8
    ACTOR_RADIO = 7,              // "Radio"           C_Radio           vtable 0x00813310
    ACTOR_LADDER = 8,             // "Ladder"          C_Ladder          vtable 0x00812a30
    ACTOR_SUPPLY_BOX = 9,         // "SupplyBox"       C_SupplyBox       vtable 0x00812d10
    ACTOR_SPAWN_POINT = 10,       // "SpawnPoint"      C_SpawnPoint      vtable 0x008138e0
    ACTOR_ITEM = 11,              // "Item"            C_ItemActor       vtable 0x00811aa8
    ACTOR_DEBRIS = 12,            // "Debris"          C_Debris          vtable 0x00812e18
    ACTOR_SWITCH = 13,            // "Switch"          C_Switch          vtable 0x008139d0
    ACTOR_TRAIN = 14,             // "Train"           C_Train           vtable 0x00811e78
    ACTOR_PHOTO_PLACE = 15,       // "Photo place"     C_PhotoPlace      vtable 0x00813400
    ACTOR_SUBMARINE = 20,         // "Submarine"       C_Submarine       vtable 0x00812650
    ACTOR_STATIC_WEAPON = 21,     // "StaticWeapon"    C_StaticWeapon    vtable 0x00813130
    ACTOR_EXPLOSIVE_PLACE = 22,   // "Explosive place" C_ExplosivePlace  vtable 0x008134f0
    ACTOR_DOCUMENT_PLACE = 23,    // "Document place"  C_DocumentPlace   vtable 0x008135e0
    ACTOR_ANIMATION = 25,         // "Animation"       C_AnimationActor  vtable 0x008119b8
    ACTOR_CLOCK = 27,             // "Clock"           C_Clock           vtable 0x00811b98
    ACTOR_PIKE = 28,              // "Pike"            C_Pike            vtable 0x00811c90
    ACTOR_FOWL_GENERATOR = 29,    // "FowlGenerator"   C_FowlGenerator   vtable 0x00811d80
    ACTOR_MELEE_TRACKER = 30,     // "Dog"             C_MeleeTracker    vtable 0x008120e0
    ACTOR_BIKE = 31,              // "Bike"            C_Bike            vtable 0x00812440
    ACTOR_FISH = 32,              // "Fish"            C_Fish            vtable 0x00812538
    ACTOR_NULL = 33,              // "NullActor"       C_NullActor       vtable 0x00812840
    ACTOR_JUNKERS88 = 34,         // "Junkers88"       C_Junkers88       vtable 0x00812c10
    ACTOR_WIRE_CUT = 35,          // "WireCut"         C_WireCut         vtable 0x00812750
    ACTOR_AIR_ATTACK = 36,        // "AirAttack"       C_AirAttack       vtable 0x00812930
    ACTOR_MP_ZONE = 37,           // "MPZone"          C_MPZone          vtable 0x00812b20
    ACTOR_TS_EXPLOSION = 38,      // "Explosion [TS]"  C_TSExplosion     vtable 0x008136d0
};

// Damage kinds passed to C_Actor::Hit().
enum E_HitType {
    HIT_FALL = 0,                 // a fall (C_HumanLocomotion::ApplyFallDamage)
    HIT_BULLET = 1,
    HIT_EXPLOSION = 2,
    HIT_MELEE = 3,
    HIT_SCRIPT_KILL = 4,          // HUMAN_Kill
    HIT_THROWN_OBJECT = 5,        // a grenade bouncing off a human (amount 1); a car running into a human or hurting its crew
    HIT_ENVIRONMENT = 6,          // deadly ground, falling out of the level (movement; inferred)
    HIT_PROJECTILE = 7,           // direct hit of a projectile on a vehicle
    HIT_MELEE_TRACKER_BITE = 8,             // a meleeTracker's bite (C_MeleeTrackerMovement::Bite)
    HIT_BACKSTAB = 11,            // knife from behind (amount -1: kills)
    HIT_SUICIDE = 12,             // (the multiplayer messages call it so)
    HIT_RUN_OVER = 13,            // by a car faster than 2 m/s
    HIT_9 = 9,                    // ground material 0x5D (movement; amount -1)
    HIT_DROWNING = 10,            // over 30 s under water
    HIT_FIRE = 15,                // ground material 0x46 (inferred)
    HIT_SCRIPT_KILL_EX = 14,      // HUMAN_KillEx
};

constexpr int ACTOR_TYPE_COUNT = 39;                    // size of g_actorCreators

// C_Actor::GetSeatFlags() bits identified so far (the occupant's pose from
// C_HumanIdle::UpdateInteraction; the animations named after their files).
enum : uint32_t {
    SEAT_FLAG_CONTROLS = 0x01,                          // at the controls ("ridic"; else "spol")
    SEAT_FLAG_SIT = 0x02,                               // sits (the weapon pose's passengers; inferred)
    SEAT_FLAG_STAND = 0x04,                             // stands in the seat
    SEAT_FLAG_CROUCH = 0x08,                            // crouches
    SEAT_FLAG_HIDDEN = 0x10,                            // the occupant is not drawn
    SEAT_FLAG_ARMED = 0x20,                             // keeps the hands item
    SEAT_FLAG_LIE = 0x40,                               // lies
    SEAT_FLAG_ASTRIDE = 0x80,                           // "naponorce" (submarine) / "namotorce" (motorbike)
    SEAT_FLAG_WEAPON = 0x100,                           // a seat weapon (GetSeatWeapon)
    // A crouching occupant dying there is turned round; the way out is behind the car
    // (C_Car::GetSeatEntry).
    SEAT_FLAG_200 = 0x200,
    SEAT_FLAG_TAKEN = 0x400,                            // someone is getting in (SetSeatBlocked)
};

// Base of all actors (vtable 0x008137f0, 56 slots; 0x90 bytes, 16-byte aligned).
//
// An actor is bound to a scene frame (SetFrame) and lives in g_actorsModule or, if
// temporary (items, mines, debris created during play), in g_temporaryActors.
// While active it is in its list's active set and gets Update (every frame) and
// Tick (fixed 100 ms steps). Activation remembers the frame's parent and local
// placement, deactivation puts the frame back there.
class alignas(16) C_Actor {
public:
    virtual ~C_Actor();                                                         // 0x00 (0x00610150, 0x00610170)
    virtual C_str *GetName() { return &m_name; }                                // 0x04 0x004d0e40
    // Called by SetFrame once the frame is bound; vehicles get their item store.
    virtual void Init();                                                        // 0x08 0x00610f80
    virtual void Done();                                                        // 0x0C 0x00611020
    virtual int GetHitPoints() { return 100; }                                  // 0x10 0x005b61c0
    // Every frame while active (frame time in ms).
    virtual void Update(int /*frameTime*/) {}                                   // 0x14 0x00611050
    // Fixed game tick while active.
    virtual void Tick(int /*timeDelta*/) {}                                     // 0x18 0x00611060
    // Network messages addressed to the actor (C_ActorsModule::OnMessage/Unknown28).
    virtual void OnMessage(C_BitStream * /*msg*/) {}                            // 0x1C 0x005b8860
    virtual void Unknown20(C_BitStream * /*msg*/) {}                            // 0x20 0x005b61d0
    virtual bool Unknown24(C_chunk & /*chunk*/) { return true; }                // 0x24 0x004d0e50
    virtual bool Unknown28(C_chunk & /*chunk*/) { return true; }                // 0x28 0x004d0e60
    // Binds the actor to `frame` (holds a reference) and calls Init().
    virtual bool SetFrame(I3D_frame *frame);                                    // 0x2C 0x00610200
    // Mission file actor properties (role, health, ...).
    virtual bool WriteProperties(C_chunk & /*chunk*/) { return true; }          // 0x30 0x005b61e0
    virtual bool ReadProperties(C_chunk & /*chunk*/) { return true; }           // 0x34 0x005b8870
    // Takes over the state of `source` (pure, with a base implementation that
    // derived classes call).
    virtual void CopyFrom(const C_Actor *source) = 0;                           // 0x38 0x006101f0
    virtual void StartMission(bool server) = 0;                                 // 0x3C
    virtual void StopMission(bool server) = 0;                                  // 0x40
    virtual void RestartMission(bool server) = 0;                               // 0x44
    virtual bool SaveGame(C_chunk &chunk);                                      // 0x48 0x00610780
    virtual bool LoadGame(C_chunk &chunk);                                      // 0x4C 0x00610a80
    // Full state for a joining client (C_ActorsModule::WriteState/ReadState).
    virtual void WriteState(C_BitStream * /*msg*/) {}                           // 0x50 0x005b8880
    virtual void ReadState(C_BitStream * /*msg*/) {}                            // 0x54 0x005b8890
    // Contact with another actor (e.g. a car running into a human).
    virtual void OnCollision(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6)   // 0x58 0x0059c2d0
    {
        OnCollisionEx(other, pos, a3, a4, a5, a6, 0);
    }
    virtual bool OnCollisionEx(C_Actor * /*other*/, const S_vector * /*pos*/, int, int, int, int, int) { return false; }   // 0x5C 0x00611070
    virtual void Unknown60(int) {}                                              // 0x60 0x004d0e70
    virtual bool IsActive() const { return m_activeIndex >= 0; }                // 0x64 0x004d0e80
    virtual void Unknown68(int, int, int) {}                                    // 0x68 0x004d0e90

    // --- seats (vehicles and static weapons override these) ---------------------
    // SEAT_FLAG_TAKEN: someone is getting in (SetSeatBlocked is true when it sets it).
    virtual bool IsSeatBlocked(int /*seat*/) { return false; }                  // 0x6C 0x0059c300
    virtual bool SetSeatBlocked(int /*seat*/, bool /*blocked*/) { return false; }   // 0x70 0x0059c310
    // Puts `occupant` into `seat` (restoring saved games).
    virtual void SetSeatOccupant(int /*seat*/, C_Actor * /*occupant*/) {}       // 0x74 0x0059c320
    virtual C_Actor *GetSeatOccupant(int /*seat*/) { return nullptr; }          // 0x78 0x0059c330
    virtual int FindSeat(const C_Actor * /*occupant*/) { return -1; }           // 0x7C 0x0059c340
    // Takes the occupant out of `seat` where it is (its frame into the scene's primary sector);
    // returns it.
    virtual C_Actor *LeaveSeat(int /*seat*/) { return nullptr; }                // 0x80 0x0059c350
    // The occupant gets out to the seat's way out (or another seat's); false: no room.
    virtual bool ExitSeat(int /*seat*/) { return false; }                       // 0x84 0x0059c360
    virtual int GetSeatCount() { return 0; }                                    // 0x88 0x0059c370
    virtual uint32_t GetSeatFlags(int /*seat*/) { return 0; }                   // 0x8C 0x0059c380
    // Camera frame of a seat ("camera3RD_", "cameraFPV_" dummies).
    virtual I3D_frame *GetSeatCamera(int /*seat*/) { return nullptr; }          // 0x90 0x0059c390
    // The seat's text (the roll menu's; -1: the default "seat <n>").
    virtual int GetSeatText(int /*seat*/) { return -1; }                        // 0x94 0x0059c3a0
    virtual bool HasSeatWeapon(int /*seat*/) { return false; }                  // 0x98 0x0059c3b0
    // (Vehicles and static weapons ask the seat weapon; RET 4: one argument, the 2026-10-17
    // audit removed a second one the declaration had.)
    // The rounds left in the seat weapon (0: reload it).
    virtual int GetSeatWeaponRounds(int /*seat*/) { return 0; }                 // 0x9C 0x0059c3c0
    // Rounds left for the weapon of `seat` in the vehicle's item store.
    virtual int GetSeatAmmoCount(int seat);                                     // 0xA0 0x00611140
    // The direction the weapon of `seat` points, and where it is (the world's).
    virtual S_vector GetSeatWeaponAim(int /*seat*/) { return S_vector(0.0f, 0.0f, 0.0f); }   // 0xA4 0x0059c3d0
    virtual S_vector GetSeatWeaponOrigin(int /*seat*/) { return S_vector(0.0f, 0.0f, 0.0f); }  // 0xA8 0x0059c3f0
    // A float of the seat (vehicles: the seat weapon's, which is 0; pi without one).
    virtual float UnknownAC(int /*seat*/) { return 3.14159265f; }               // 0xAC 0x0059c410
    // The text (name) of the seat weapon's item, -1 without one.
    virtual int GetSeatWeaponText(int /*seat*/) { return -1; }                  // 0xB0 0x0059c420
    // The definition of the seat weapon's item, null without one.
    virtual class C_ItemDef *GetSeatWeaponDef(int /*seat*/) { return nullptr; } // 0xB4 0x0059c430
    virtual void GetSeatWeaponLimits(int /*seat*/, float * /*a*/, float * /*b*/, float * /*c*/, float * /*d*/) {}   // 0xB8 0x0059c440
    // The seat weapon can be turned to point at `pos`.
    virtual bool CanSeatWeaponAim(int /*seat*/, float * /*pos*/) { return false; }   // 0xBC 0x0059c450
    // The controls (C_HumanController's axes and buttons) of the human in `seat`, while the
    // seat has a weapon.
    virtual void ApplySeatControls(int /*seat*/, float * /*axes*/, float * /*buttons*/) {}   // 0xC0 0x0059c460
    // Shows / hides the seat's first-person parts (vehicles: the seat weapon's, else the
    // "VIEW<seat>" visuals): on for a local player seated in first person, off when it leaves.
    virtual void ShowSeatView(int /*seat*/, bool /*on*/) {}                     // 0xC4 0x0059c470
    virtual bool UnknownC8(int /*seat*/, int * /*a*/, int * /*b*/, int * /*c*/) { return false; }   // 0xC8 0x0059c480
    // Seat dummy marked "NO3RD" (no third-person view).
    virtual bool IsSeatFirstPersonOnly(int /*seat*/) { return false; }          // 0xCC 0x0059c490
    virtual void UnknownD0(int, int, int, int) {}                               // 0xD0 0x0059c4a0

    // Applies damage; `amount` < 0 kills.
    virtual void Hit(E_HitType /*type*/, float /*amount*/, const S_vector & /*pos*/, const S_vector & /*dir*/,
                     C_Actor * /*attacker*/, int /*flags*/) {}                  // 0xD4 0x005b88a0
    // Surface material of `part` (frame user property "matid"); 11 by default.
    virtual int GetMaterialId(I3D_frame * /*part*/) { return 11; }              // 0xD8 0x005b88b0
    // World-space corners of the box around the frame hierarchy's geometry.
    virtual void GetBoxCorners(S_vector4 corners[8]);                           // 0xDC 0x006115b0

    // --- non-virtual ----------------------------------------------------------------
    E_ActorType GetType() const { return m_type; }
    I3D_frame *GetFrame() const { return m_frame; }
    bool IsTemporary() const { return m_temporary; }
    void SetTemporary(bool on) { m_temporary = on; }
    // Sector the actor is in (0x005ac4c0, on the member at +0x98).
    I3D_sector *GetSector() const;
    int GetNetId() const { return m_netId; }
    void SetNetId(int id) { m_netId = id; }                                     // 0x00610230
    int GetActiveIndex() const { return m_activeIndex; }
    void SetActiveIndex(int index) { m_activeIndex = index; }
    // Simulation position / direction (the frame follows them).
    S_vector GetPosition() const { return S_vector(m_position.x, m_position.y, m_position.z); }
    void SetPosition(const S_vector &pos, float w = 1.0f) { m_position = {pos.x, pos.y, pos.z, w}; }    // 0x0041d700
    void SetDirection(const S_vector &dir, float w = 0.0f) { m_direction = {dir.x, dir.y, dir.z, w}; }  // 0x0041d720
    S_vector GetDirection() const { return S_vector(m_direction.x, m_direction.y, m_direction.z); }
    const S_vector4 &GetPosition4() const { return m_position; }
    const S_vector4 &GetDirection4() const { return m_direction; }
    void SetPosition4(const S_vector4 &pos) { m_position = pos; }
    void SetDirection4(const S_vector4 &dir) { m_direction = dir; }
    // Item container `index` (ITEM_CONTAINER_* for humans; vehicles have only 0), null if
    // the actor carries no items.
    C_ItemContainer *GetContainer(int index) const { return m_containers ? m_containers[index] : nullptr; }
    void SetContainer(int index, C_ItemContainer *container) { m_containers[index] = container; }
    // Vehicles (and actors without items) have at most container 0.
    bool HasSingleContainer() const { return m_singleContainer; }
    // Weight of everything in the containers; what an actor can carry is GetHitPoints().
    float GetCarriedWeight() const { return m_carriedWeight; }
    void SetCarriedWeight(float weight) { m_carriedWeight = weight; }
    void AddCarriedWeight(float weight) { m_carriedWeight += weight; }

    // Puts the actor into its list's active set (server and/or client side) and
    // remembers where its frame is, so that Deactivate() can put it back.
    void Activate(bool server);                                                 // 0x00610240
    void Deactivate(bool server);                                               // 0x00610510
    // Deactivate + activate on every side the game runs (`server`, passed by the human's
    // RestartMission, is not used).
    void Reactivate(bool server);                                               // 0x00610710

protected:
    explicit C_Actor(E_ActorType type);                                         // 0x00610070

    int m_netId = -1;                                   // +0x10 index in the actors module / temporary id
    int m_activeIndex = -1;                             // +0x14 index in the active set, -1 = inactive
    E_ActorType m_type;                                 // +0x18
    bool m_temporary = false;                           // +0x1C kept by g_temporaryActors
    I3D_frame *m_frame = nullptr;                       // +0x20 (holds a reference)
    S_vector4 m_position = {};                          // +0x30
    S_vector4 m_direction = {};                         // +0x40
    S_vector4 m_initialPos = {};                        // +0x50 frame placement saved by Activate()
    S_quat m_initialRot = {1.0f, 0.0f, 0.0f, 0.0f};     // +0x60
    I3D_frame *m_initialParent = nullptr;               // +0x70 (holds a reference while set)
    // Item containers: humans have ITEM_CONTAINER_COUNT, vehicles one (m_singleContainer).
    C_ItemContainer **m_containers = nullptr;           // +0x74
    bool m_singleContainer = false;                     // +0x78
    float m_carriedWeight = 0.0f;                       // +0x7C weight of the items in the containers
    C_str m_name;                                       // +0x80 "Missing actor name!!" until set
};

// Creates and destroys actors of one type (g_actorCreators[type]).
class C_ActorCreator {
public:
    virtual C_Actor *Create() = 0;                      // 0x00
    virtual const char *GetName() = 0;                  // 0x04 e.g. "SpawnPoint"
    virtual void Destroy(C_Actor *actor) = 0;           // 0x08 (scalar deleting destructor)
};
extern C_ActorCreator **g_actorCreators;                // 0x00839460 -> [ACTOR_TYPE_COUNT]

// Material id from a frame's user properties ("matid<n>"), -1 if there is none.
int GetFrameMaterialId(const I3D_frame *frame);         // 0x00611090

// Actor bound to `frame`, or null (0x0041cf80).
inline C_Actor *GetFrameActor(const I3D_frame *frame)
{
    return frame && frame->GetData() ? frame->GetData()->actor : nullptr;
}
// Actor of `frame` if it has the given type.
template <class T> T *GetFrameActorAs(const I3D_frame *frame, E_ActorType type)
{
    C_Actor *actor = GetFrameActor(frame);
    return actor && actor->GetType() == type ? static_cast<T *>(actor) : nullptr;
}

// C_Human: see game/human.h.
