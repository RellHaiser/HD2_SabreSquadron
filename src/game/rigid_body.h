// The rigid body (C_RigidBody, vtable 0x00810700, 0x600 bytes; original 0x004c7d90..0x004cff70): the
// physics of loose objects -- thrown grenades and items (C_ThrownShot), a car's parts and wheels flying
// off and other debris (C_Debris), dropped items. A body is a box tested at 14 points (Setup) or a
// rolling disc (SetupWheel) under gravity, moved by impulses, against the collision world's ray tests;
// it sleeps when it has been slow or still for long enough. The integration steps are part 5b.
#pragma once

#include <cstdint>
#include <vector>

#include "game/collision.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

class C_Actor;
class C_chunk;

// What a body touched, reported to its callback.
struct S_RigidBodyContact {
    S_CollisionVolume *volume;                          // +0x00 what it touched
    uint8_t unknown04[0x0C];
    S_vector pos;                                       // +0x10
    float posW;
    S_vector normal;                                    // +0x20
    float normalW;
};
enum : int {
    RIGID_BODY_EVENT_REST = 0,                          // the body came to rest (no contact)
    RIGID_BODY_EVENT_CONTACT = 1,
};
// Called for rigid body events: `event` in ECX, EDX unused (the original passes the body), the rest
// on the stack (callee pops). For a contact, 1 makes the body ignore it (thrown shots: the
// thrower's mates' at first); any other value lets it count (the item actors return 2).
using T_RigidBodyCallback = int(__fastcall *)(int event, int unused, void *context,
                                              const S_RigidBodyContact *contact);
constexpr int RIGID_BODY_REFUSE = 1;

// C_RigidBody::Setup's parameters (0x44 bytes; Setup reads +0x20 on).
struct S_RigidBodyParams {
    S_vector4 unknown00 = {};                           // thrown shots: (0, 1, 0)
    S_vector4 unknown10 = {};                           // thrown shots: 10 m above the start
    S_vector4 velocity = {};                            // +0x20 m/s; (nearly) zero: starts at rest
    float weight = 0.0f;                                // +0x30 kg (sets the inertia)
    float friction = 0.0f;                              // +0x34
    T_RigidBodyCallback callback = nullptr;             // +0x38
    void *context = nullptr;                            // +0x3C
    bool moveCollision = false;                         // +0x40 the volume's type RIGID_BODY_VOLUME_TYPE_MOVING
    bool unknown41 = false;                             // +0x41 at rest the volume grows to the doubled box

    void SetVelocity(const S_vector &v) { velocity = {v.x, v.y, v.z, 0.0f}; }
};
// FRM_CreatePhysicalObject passes the same parameters to its debris.
using S_DebrisParams = S_RigidBodyParams;

// C_RigidBody::SetupWheel's parameters (a rolling disc: a car's thrown-off wheel; 0x2C bytes).
struct S_WheelDebrisParams {
    S_vector4 velocity = {};                            // +0x00 m/s; (nearly) zero: starts at rest
    float radius = 0.0f;                                // +0x10 (with the weight: a disc's inertia)
    float width = 0.0f;                                 // +0x14
    float weight = 0.0f;                                // +0x18 kg
    float unknown1C = 0.0f;                             // +0x1C the friction (the car's: the wheel's unknown1B0)
    float unknown20 = 0.0f;                             // +0x20 (the car's: the wheel's unknown1C0)
    T_RigidBodyCallback callback = nullptr;             // +0x24 (the context is null)
    bool moveCollision = false;                         // +0x28 (SetupWheel: the volume grows at rest)

    void SetVelocity(const S_vector &v) { velocity = {v.x, v.y, v.z, 0.0f}; }
};

// An actor a box point rests on (StepBox); Tick wakes the body when one moves and forgets one a
// second after it was last touched. (0x20 bytes)
struct alignas(16) S_RigidBodyActorContact {
    S_vector4 pos;                                      // +0x00 where the actor was when last seen
    C_Actor *actor;                                     // +0x10
    int time;                                           // +0x14 ms left (1000 when touched again)
    int point;                                          // +0x18 the box point (0..13)
    int unknown1C;
};

enum : int {
    RIGID_BODY_MOVING = 0,
    RIGID_BODY_AT_REST = 1,
    RIGID_BODY_FELL = 2,                                // falling faster than 85 m/s
};
enum : int {
    RIGID_BODY_DISC = 1,                                // SetupWheel
    RIGID_BODY_BOX = 2,                                 // Setup
};
// The volume's collision types (S_CollisionVolume::type; meanings not identified).
constexpr uint32_t RIGID_BODY_VOLUME_TYPE = 0x00080802;
constexpr uint32_t RIGID_BODY_VOLUME_TYPE_MOVING = 0x00000801;
constexpr int RIGID_BODY_POINTS = 14;

class alignas(16) C_RigidBody final {                   // vtable 0x00810700, 0x600 bytes
public:
    C_RigidBody();                                      // 0x004c84a0
    // (Not virtual: the vtable has no destructor; the owners destroy their bodies themselves.)
    ~C_RigidBody();                                     // 0x004c8770
    C_RigidBody(const C_RigidBody &) = delete;
    C_RigidBody &operator=(const C_RigidBody &) = delete;

    // The frame the body moves (its place and matrices taken); the body starts at rest.
    virtual bool SetFrame(I3D_frame *frame);            // +0x00 0x004c8820
    // Wakes the body and pushes it at `point` by 1.1 x `impulse`; a hit sound's record (m_hitMaterial
    // set) keeps the hardest. (The unused arguments are unread; kept for the slot's stack.)
    virtual bool ApplyImpulse(int unused, const S_vector4 &point, const S_vector &impulse, int unused4, int unused5,
                              int unused6);             // +0x04 0x004cdfe0
    // (Its eight stack arguments are unread; kept for the slot's stack.)
    virtual bool Wake(int unused1, int unused2, int unused3, int unused4, int unused5, int unused6, int unused7,
                      int unused8);                     // +0x08 0x004ce5f0
    virtual bool SaveGame(C_chunk &chunk);              // +0x0C 0x004cec60
    virtual bool LoadGame(C_chunk &chunk);              // +0x10 0x004cef00
    // A moving body that was nudged already starts its rest timers again.
    virtual void Nudge();                               // +0x14 0x004c9480
    // Into the collision world, the matrices, the volume; at rest unless it has a speed.
    virtual void Start();                               // +0x18 0x004c8980
    virtual void Stop();                                // +0x1C 0x004c8ea0
    // The simulation of `time` ms (at most 83, in steps of at most m_maxStep) unless it is resting.
    virtual void Step(uint32_t time);                   // +0x20 0x004c8ee0
    // A frame of a moving body: Step, the rest timers, the actors it lies on, the frame and volume.
    virtual void Tick(uint32_t time);                   // +0x24 0x004c9000

    // A box: the frame's hierarchy box gives its centre of mass, inertia, points and skin.
    void Setup(const S_RigidBodyParams &params);        // 0x004cd6e0
    // A rolling disc (a wheel).
    void SetupWheel(const S_WheelDebrisParams &params); // 0x004cd550
    void SetOwner(C_Actor *owner);                      // 0x004c8250 (also the volume's actor)
    // RIGID_BODY_*: coming to rest tells the callback; moving clears the slow time; at rest or
    // fallen, a body with m_restVolume gets the doubled box as its volume.
    void SetState(int state);                           // 0x004c8270
    int GetState() const { return m_state; }
    // Wakes the body and pushes it at `point` (its spin too; no hit record).
    void AddImpulse(const S_vector &impulse, const S_vector4 &point);   // 0x004ce7a0
    // Drops `actor` from the actors the body lies on.
    void ForgetActor(C_Actor *actor);                   // 0x004ce8f0

private:
    // The matrices, the centre of mass, the place and axes from the frame. (0x004ceaa0)
    void RefreshMatrices();
    // A box resting on its smaller side (taller than 2.5 times a side of its base) is pushed over
    // (false); true when it may rest. (0x004cf440)
    bool Topple();
    // --- the integration steps (rigid_body_step.cpp) ---
    // A step of the rolling disc: rolling resistance, friction and the ground's spring at the last
    // contact; the move; the ground under the disc and along its way.
    void StepWheel(float dt);                           // 0x004c94b0
    // A step of the box: gravity, the move, each of the 14 points against the world (ray tests or
    // the last planes), the impulses where they meet it, the deepest one pushed back out.
    void StepBox(float dt);                             // 0x004cb2b0
    // Turns the body by its spin over `dt` (about the centre of mass) and moves it by `move`.
    void Integrate(float dt, const S_vector &move);     // 0x004cc520
    // The impulse of `point` (on the body, moving into `normal`) bouncing off, and the friction
    // along the surface. `unusedDt` and `unused7` (the point's actor) are not read.
    void Collide(S_vector &velocity, S_vector &spin, const S_vector &normal, const S_vector4 &point, float unusedDt,
                 int16_t pointIndex, int unused7);      // 0x004cccc0
    // Whether a contact counts: the callback may refuse it; water does not; an actor's volume (a car
    // or an animation actor: `onActor` set, else cleared) only without m_ignoreActors.
    S_CollisionVolume *FilterContact(S_CollisionVolume *volume, const S_vector4 &pos, const S_vector4 &normal,
                                     bool *onActor);    // 0x004ce990

    uint8_t m_unknown04[12] = {};                       // +0x04 (after the vtable)
    int m_state = RIGID_BODY_MOVING;                    // +0x10 RIGID_BODY_*
    uint8_t m_unknown14[12] = {};
    S_vector4 m_pos = {};                               // +0x20 the frame's world position
    S_vector4 m_dir = {};                               // +0x30 its z axis
    I3D_frame *m_frame = nullptr;                       // +0x40
    T_RigidBodyCallback m_callback = nullptr;           // +0x44
    void *m_context = nullptr;                          // +0x48
    bool m_restVolume = false;                          // +0x4C at rest the volume is the doubled box
    bool m_moveCollision = false;                       // +0x4D the volume's type: RIGID_BODY_VOLUME_TYPE_MOVING
    bool m_contactsChanged = false;                     // +0x4E
    C_Actor *m_pointActors[RIGID_BODY_POINTS] = {};     // +0x50 the actor each box point stands on
    C_Actor *m_owner = nullptr;                         // +0x88
    uint8_t m_unknown8C[4] = {};
    S_vector m_centre = {0.0f, 0.0f, 0.0f};             // +0x90 the centre of mass in the frame's space
    float m_centreW = 0.0f;
    S_vector4 m_centreWorld = {};                       // +0xA0
    S_vector4 m_restPos = {};                           // +0xB0 where it was when it last moved 0.2 m
    S_vector4 m_right = {};                             // +0xC0 the frame's x axis
    S_vector4 m_up = {};                                // +0xD0 its y axis
    S_vector4 m_normal = {0.0f, 1.0f, 0.0f, 0.0f};      // +0xE0 the ground's normal
    S_matrix m_matrix = {};                             // +0xF0 the frame's world matrix
    S_matrix m_invMatrix = {};                          // +0x130
    S_matrix m_rotation = {};                           // +0x170 m_matrix without the translation
    S_matrix m_invRotation = {};                        // +0x1B0
    S_vector m_invInertia = {0.0f, 0.0f, 0.0f};         // +0x1F0 about the frame's axes
    float m_invInertiaW = 0.0f;
    S_vector m_velocity = {0.0f, 0.0f, 0.0f};           // +0x200 m/s
    float m_velocityW = 0.0f;
    S_vector m_spin = {0.0f, 0.0f, 0.0f};               // +0x210 rad/s (impulses subtract)
    float m_spinW = 0.0f;
    S_vector4 m_gravity = {};                           // +0x220 (0, -mass g, 0)
    S_vector4 m_points[RIGID_BODY_POINTS] = {};         // +0x230 the box's corners and faces' middles (frame space)
    S_vector4 m_lastPoints[RIGID_BODY_POINTS] = {};     // +0x310 their world positions at the last step
    S_vector4 m_contactPoint = {};                      // +0x3F0 the disc's ground contact
    S_vector4 m_hitPoint = {};                          // +0x400 where this frame's hardest impulse hit
    S_CollisionVolume m_volume;                         // +0x410
    S_plane m_pointPlanes[RIGID_BODY_POINTS] = {};      // +0x470 the plane each box point last hit
    bool m_pointHit[RIGID_BODY_POINTS] = {};            // +0x550
    uint8_t m_unknown55E[2] = {};
    int m_mode = 0;                                     // +0x560 RIGID_BODY_DISC / RIGID_BODY_BOX
    uint32_t m_stillTime = 0;                           // +0x564 ms without moving 0.2 m
    float m_maxStep = 0.028f;                           // +0x568 s: longer steps are split
    float m_mass = 0.0f;                                // +0x56C kg
    float m_friction = 0.0f;                            // +0x570
    float m_bounce = 0.0f;                              // +0x574 1 + restitution
    float m_contactRange = 0.0f;                        // +0x578
    float m_stiffness = 0.0f;                           // +0x57C the disc's spring
    float m_g = 10.0f;                                  // +0x580 m/s2 (9 in water)
    float m_unknown584 = 0.0f;                          // +0x584 (Setup: 4 x mass; saved, never read)
    float m_skin = 0.0f;                                // +0x588 half the box's smallest side, at most 0.04
    float m_width = 0.0f;                               // +0x58C the disc's
    float m_radius = 0.0f;                              // +0x590 the disc's
    float m_unknown594 = 0.0f;                          // +0x594 (the disc's params +0x20)
    float m_springForce = 0.0f;                         // +0x598 the disc's
    float m_groundDistance = 0.0f;                      // +0x59C
    float m_slowTime = 0.0f;                            // +0x5A0 s under 0.3 m/s and 0.1 rad/s
    float m_restDelay = 0.0f;                           // +0x5A4 s (Start: 0.6)
    float m_timeScale = 0.0f;                           // +0x5A8 (83 / ms for frames over 83 ms)
    int m_unknown5AC = -1;                              // +0x5AC (saved)
    int m_hitMaterial = -1;                             // +0x5B0 -1: ApplyImpulse records no hit
    float m_hardestHit = 0.0f;                          // +0x5B4 this frame's hardest impulse
    int m_unknown5B8 = 0;                               // +0x5B8
    S_RigidBodyActorContact *m_actorContacts = nullptr; // +0x5BC
    int m_actorContactsCapacity = 0;                    // +0x5C0
    int m_actorContactCount = 0;                        // +0x5C4
    int m_actorContactsGrowBy = 4;                      // +0x5C8
    uint16_t m_pointSurface[RIGID_BODY_POINTS] = {};    // +0x5CC each box point's last surface material
    uint16_t m_hitPointIndex = 0;                       // +0x5E8 the box point of the hardest hit
    bool m_pointOnActor[RIGID_BODY_POINTS] = {};        // +0x5EA its surface belongs to a car or a human
    bool m_unknown5F8 = true;                           // +0x5F8
    bool m_testTrailing = false;                        // +0x5F9 StepBox tests the trailing points too (every other step; slow: always)
    bool m_nudged = false;                              // +0x5FA (Start: set)
    bool m_hasCentre = false;                           // +0x5FB m_centre is set (Setup)
    bool m_stopOnFlat = false;                          // +0x5FC the disc stops on a flat ground
    bool m_ignoreActors = false;                        // +0x5FD contacts with actors' volumes are ignored
    uint8_t m_unknown5FE[2] = {};
};
static_assert(sizeof(C_RigidBody) == 0x600, "C_RigidBody layout");

// Every rigid body (0x0089bcd4): the constructor adds it, the destructor removes it.
extern std::vector<C_RigidBody *> g_rigidBodies;

// Forgets `actor` in every body (C_Actor's destructor). (0x004cff70)
void ForgetActor(C_Actor *actor);

// The box of `frame`'s visual ('OBJ_') and its visual descendants' (through visuals only), in
// `frame`'s space; empty (1e16 / -1e16) for a frame without a visual of its own and no visual
// child. (0x004c7db0; the original returns its static box, 0x0089bcf0.)
const I3D_bbox &GetHierarchyBox(I3D_frame *frame);
