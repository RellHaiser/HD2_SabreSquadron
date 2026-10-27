# The rigid body (src/game/rigid_body.h, part 5): the simulation of loose objects (thrown grenades
# and items, a car's wheels and parts flying off, dropped items): a box tested at 14 points
# (Setup, mode 2) or a rolling disc (SetupWheel, mode 1) under gravity, against the collision
# world's ray tests. 0x600 bytes, 16-byte aligned (embedded at +0x70 in C_ThrownShot / C_Debris);
# ctor 0x004c84a0, dtor 0x004c8770; every body is in g_rigidBodies (0x0089bcd4) for ForgetActor.

# C_RigidBody::Setup's parameters (0x004cd6e0; a box; game/world.h). Setup reads +0x20 on.
struct S_RigidBodyParams size=0x44
  0x00 S_vector4 unknown00                     # (thrown shots: 0, 1, 0)
  0x10 S_vector4 unknown10                     # (thrown shots: 10 m above the start)
  0x20 S_vector4 velocity                      # m/s; (nearly) zero: starts at rest
  0x30 float weight                            # kg
  0x34 float friction
  0x38 void* callback                          # T_RigidBodyCallback
  0x3c void* context
  0x40 bool moveCollision                      # the volume's type 0x801 instead of 0x80802
  0x41 bool unknown41                          # at rest the volume grows to the doubled box
end

# An actor a box point rests on (StepBox); Tick wakes the body when one moves, and forgets it a
# second after it was last touched.
struct S_RigidBodyActorContact size=0x20
  0x00 S_vector4 pos                           # where the actor was when last seen
  0x10 C_Actor* actor
  0x14 int time                                # ms left (1000 when touched again)
  0x18 int point                               # the box point (0..13)
  0x1c int unknown1C
end

class C_RigidBody size=0x600 vtable=0x810700:10
  0x004 uint8_t m_unknown04[12]
  0x010 int m_state                            # 0 moving, 1 at rest, 2 fell (vertical speed under -85 m/s)
  0x014 uint8_t m_unknown14[12]
  0x020 S_vector4 m_pos                        # the frame's world position (row 3 of m_matrix)
  0x030 S_vector4 m_dir                        # its z axis (row 2)
  0x040 I3D_frame* m_frame
  0x044 void* m_callback                       # T_RigidBodyCallback: (context, 0) at rest, (context, contact) per contact
  0x048 void* m_context
  0x04c bool m_restVolume                      # Setup's params +0x41: at rest the volume grows to the doubled box
  0x04d bool m_moveCollision                   # params +0x40 (SetupWheel: set): volume type 0x801, else 0x80802
  0x04e bool m_contactsChanged
  0x050 C_Actor* m_pointActors[14]             # the actor each box point stands on (StepBox)
  0x088 C_Actor* m_owner                       # also the volume's actor (SetOwner)
  0x08c uint8_t m_unknown8C[4]
  0x090 S_vector m_centre                      # the centre of mass in the frame's space (Setup: the hierarchy box's middle)
  0x09c float m_centreW
  0x0a0 S_vector4 m_centreWorld
  0x0b0 S_vector4 m_restPos                    # where the body was when it last moved 0.2 m (Tick)
  0x0c0 S_vector4 m_right                      # the frame's x axis (row 0)
  0x0d0 S_vector4 m_up                         # its y axis (row 1)
  0x0e0 S_vector4 m_normal                     # the ground's normal (0, 1, 0 at first; the wheel's ray)
  0x0f0 S_matrix m_matrix                      # the frame's world matrix
  0x130 S_matrix m_invMatrix
  0x170 S_matrix m_rotation                    # m_matrix without the translation
  0x1b0 S_matrix m_invRotation
  0x1f0 S_vector m_invInertia                  # the inverse moments of inertia about the frame's axes
  0x1fc float m_invInertiaW
  0x200 S_vector m_velocity                    # m/s
  0x20c float m_velocityW
  0x210 S_vector m_spin                        # angular velocity (rad/s; impulses subtract)
  0x21c float m_spinW
  0x220 S_vector4 m_gravity                    # (0, -mass * g, 0): the force of the weight
  0x230 S_vector4 m_points[14]                 # the box's corners, its faces' middles (frame space)
  0x310 S_vector4 m_lastPoints[14]             # their world positions at the last step
  0x3f0 S_vector4 m_contactPoint               # the wheel's ground contact
  0x400 S_vector4 m_hitPoint                   # where the hardest impulse of the frame hit
  0x410 S_CollisionVolume m_volume             # its sphere / box in the collision world
  0x470 S_plane m_pointPlanes[14]              # the plane each box point last hit
  0x550 bool m_pointHit[14]
  0x55e uint8_t m_unknown55E[2]
  0x560 int m_mode                             # 1 rolling disc (SetupWheel), 2 box (Setup)
  0x564 uint32_t m_stillTime                   # ms without moving 0.2 m
  0x568 float m_maxStep                        # s (0.028): longer steps are split
  0x56c float m_mass                           # kg
  0x570 float m_friction
  0x574 float m_bounce                         # 1 + restitution (box 1.01, disc 1.05)
  0x578 float m_contactRange                   # the ground counts this far (box 0.04, disc 0.03)
  0x57c float m_stiffness                      # the disc's spring (g x mass x 37.03704; box x 12.5)
  0x580 float m_g                              # 10 m/s2 (9 once in water)
  0x584 float m_unknown584                     # (Setup: 4 x mass; saved, never read)
  0x588 float m_skin                           # half the box's smallest side, at most 0.04
  0x58c float m_width                          # the disc's
  0x590 float m_radius                         # the disc's
  0x594 float m_unknown594                     # (the disc's params +0x20)
  0x598 float m_springForce                    # the disc's: (m_contactRange - m_groundDistance) x m_stiffness
  0x59c float m_groundDistance
  0x5a0 float m_slowTime                       # s spent slow (under 0.3 m/s and 0.1 rad/s)
  0x5a4 float m_restDelay                      # s (0.6): slow for longer, the body may rest
  0x5a8 float m_timeScale                      # (1; 83 / ms for frames over 83 ms)
  0x5ac int m_unknown5AC                       # (-1 when made; saved)
  0x5b0 int m_hitMaterial                      # -1: impulses make no hit sound
  0x5b4 float m_hardestHit                     # this frame's hardest impulse
  0x5b8 int m_unknown5B8                       # (Start: 0)
  0x5bc S_RigidBodyActorContact* m_actorContacts   # (_aligned_malloc)
  0x5c0 int m_actorContactsCapacity
  0x5c4 int m_actorContactCount
  0x5c8 int m_actorContactsGrowBy              # (4)
  0x5cc uint16_t m_pointSurface[14]            # each box point's last surface material (the disc's and ApplyImpulse's in [0])
  0x5e8 uint16_t m_hitPointIndex               # the box point of the hardest hit
  0x5ea bool m_pointOnActor[14]                # the point's surface belongs to a car or a human (FilterContact)
  0x5f8 bool m_unknown5F8                      # (1 when made)
  0x5f9 bool m_testTrailing                    # StepBox tests the trailing points too (every other step; slow: always)
  0x5fa bool m_nudged                          # (Start: set; Nudge restarts the rest timers when set)
  0x5fb bool m_hasCentre                       # m_centre is set (Setup)
  0x5fc bool m_stopOnFlat                      # the disc stops on a flat ground
  0x5fd bool m_ignoreActors                    # contacts with actors' volumes are ignored
  vslot 0x00 SetFrame | bool __thiscall SetFrame(C_RigidBody *this, I3D_frame *frame)
  vslot 0x01 ApplyImpulse | bool __thiscall ApplyImpulse(C_RigidBody *this, int unused, S_vector4 *point, S_vector *impulse, int unused4, int unused5, int unused6)
  vslot 0x02 Wake | bool __thiscall Wake(C_RigidBody *this, int unused1, int unused2, int unused3, int unused4, int unused5, int unused6, int unused7, int unused8)
  vslot 0x03 SaveGame | bool __thiscall SaveGame(C_RigidBody *this, void *stream)
  vslot 0x04 LoadGame | bool __thiscall LoadGame(C_RigidBody *this, void *stream)
  vslot 0x05 Nudge | void __thiscall Nudge(C_RigidBody *this)
  vslot 0x06 Start | void __thiscall Start(C_RigidBody *this)
  vslot 0x07 Stop | void __thiscall Stop(C_RigidBody *this)
  vslot 0x08 Step | void __thiscall Step(C_RigidBody *this, uint time)
  vslot 0x09 Tick | void __thiscall Tick(C_RigidBody *this, uint time)
end
