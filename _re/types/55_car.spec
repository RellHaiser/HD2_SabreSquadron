# The car actor (src/game/car.h): C_Car, "Car", vtable 0x00810778 (C_Actor's 56 slots); 0x25C0 bytes,
# 16-byte aligned (creator 0x004d04f0); ctor 0x004d0b60, dtor 0x004d0f20. C_Actor, the vehicle at
# +0x90, the car's own members from +0x2360: one field per offset its functions access (typed by
# use; named as they are ported).

# An explosive part of a car's damage model (an "EXPL_D*" visual; _aligned_malloc'd by 0x004e09d0).
struct S_CarExplosive size=0x14
  0x00 I3D_frame* frame
  0x04 int fuse                                # ms to the wreck once hit ("EXPL<s>" x 1000)
  0x08 int effect                              # its particle effect's handle (-1 none)
  0x0c int effectId                            # "PARTID<n>"
  0x10 int started                             # hit
end

# A damage zone of a car's damage model (a "D_ZONE*" visual).
struct S_CarZone size=0x0c
  0x00 I3D_frame* frame
  0x04 int hp                                  # "HP<n>": hit points left
  0x08 int material                            # "matid<n>": g_materials index
end

# The car's damage parts (C_Car +0x24E0; ctor 0x004e08c0, Setup 0x004e0930, Reset 0x004e0ea0,
# released by 0x004e0ee0; 0x68 bytes): the damage model ("DM" + the model's file) and its parts.
struct S_CarParts size=0x68
  0x00 I3D_model* m_model                      # the damage model (hidden)
  0x04 C_Car* m_car
  0x08 I3D_frame* m_particleFrame              # the damage model's "particle" dummy
  0x0c int m_fuse                              # ms to the wreck (0 none)
  0x10 int m_fireEffect                        # "PFIRE", else 0x60
  0x14 int m_smokeEffect                       # "PSMOKE", else 0x5F
  0x18 int m_wreckSound                        # "SID"
  0x1c int m_fireHandle                        # the wreck's fire's particle handle
  0x20 int m_smokeHandle
  0x24 std::vector<S_CarExplosive*> m_explosives
  0x34 std::vector<I3D_frame*> m_turretFrames  # "HOR*" / "VERT*"
  0x44 std::vector<S_CarZone> m_zones
  0x54 std::vector<I3D_frame*> m_seatFrames    # per seat its "SEAT<nn>" hit box
  0x64 bool m_wrecked                          # the parts wrecked the car, or Wreck did
end

# The quantisation of up to three floats (net/bitstream.h; 0x80 bytes; Setup 0x0073dbb0): the
# player table's entries start with one, the car keeps two for its net object.
struct S_NetQuantization size=0x80
  0x00 float deadZone[4]                       # the step: smaller magnitudes are sent / read back as 0
  0x10 float min[4]                            # (y: the lowest y sent)
  0x20 float range[4]
  0x30 float offset[4]                         # the range's start (min again)
  0x40 float range40[4]                        # (range again)
  0x50 int bits[3]                             # floor(log2(range / step)) + 1
  0x5c int unknown5C
  0x60 float scale[3]                          # steps per unit: the bits' mask / range
  0x6c float unknown6C
  0x70 int unknown70                           # Setup's last argument (the car's: 100)
end

# The network library's period timer on the performance counter (game/perf_timer.h; 12 bytes).
struct S_PerfTimer size=0xc
  0x00 int start                               # ms (0x00748dc0)
  0x04 int period                              # ms
  0x08 uint32_t running
end

# A checkpoint the car marked (C_Car +0x2598's entries; operator new'd 4-byte records).
struct S_CarCheckpointMark size=0x4
  0x00 uint16_t checkpoint
  0x02 uint8_t flagged                         # CHECKPOINT_FLAG_CAR_0200 set (else +0x1A counted down)
end

# CAR_MSG_HIT (game/car.h): a client's car ran a human over or hurt its crew (the callbacks).
class C_CarHitMessage size=0x30 vtable=0x81088c:2
  0x04 int m_kind                              # 9
  0x08 int human                               # net id (16 bits); -1: the crew
  0x0c int amount                              # 16 bits
  0x10 S_vector4 impulse                       # (w not sent)
  0x20 S_vector4 point
  vslot 0x00 Read | void __thiscall Read(C_CarHitMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarHitMessage *this, C_BitStream *stream)
end

# CAR_MSG_GLASS (game/car.h): a shot hit a glass part (Hit).
class C_CarGlassMessage size=0x20 vtable=0x8108e0:2
  0x04 int m_kind                              # 0
  0x08 int part                                # 10 bits
  0x0c int health                              # 10 bits
  0x10 S_vector4 dir                           # WriteDirection3D
  vslot 0x00 Read | void __thiscall Read(C_CarGlassMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarGlassMessage *this, C_BitStream *stream)
end

# CAR_MSG_PART (game/car.h): a shot hit another damage part (Hit).
class C_CarPartMessage size=0x30 vtable=0x8108d8:2
  0x04 int m_kind                              # 1
  0x08 int part                                # 10 bits
  0x0c int health                              # 10 bits; at 0 or less the vectors follow
  0x10 S_vector4 velocity                      # quantised as a position
  0x20 S_vector4 push
  vslot 0x00 Read | void __thiscall Read(C_CarPartMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarPartMessage *this, C_BitStream *stream)
end

# CAR_MSG_TYRE (game/car.h): a shot hit a tyre (Hit).
class C_CarPartEffectMessage size=0xc vtable=0x8108c8:2
  0x04 int m_kind                              # 5
  0x08 int part                                # 4 bits (a damage part's effect, part 2g)
  vslot 0x00 Read | void __thiscall Read(C_CarPartEffectMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarPartEffectMessage *this, C_BitStream *stream)
end

class C_CarFuelMessage size=0xc vtable=0x8108b8:2
  0x04 int m_kind                              # 10
  0x08 float fuel                              # 0..5000 l in 16 bits
  vslot 0x00 Read | void __thiscall Read(C_CarFuelMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarFuelMessage *this, C_BitStream *stream)
end

class C_CarTyreMessage size=0xc vtable=0x8108d0:2
  0x04 int m_kind                              # 3
  0x08 int wheel                               # 8 bits
  vslot 0x00 Read | void __thiscall Read(C_CarTyreMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarTyreMessage *this, C_BitStream *stream)
end

# CAR_MSG_IMPULSE (game/car.h): a push for the machine that simulates the car (OnCollision, TakePush).
class C_CarImpulseMessage size=0x30 vtable=0x810880:2
  0x04 int m_kind                              # 11
  0x10 S_vector4 point                         # quantised as a position
  0x20 int actor                               # net id, 10 bits
  vslot 0x00 Read | void __thiscall Read(C_CarImpulseMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarImpulseMessage *this, C_BitStream *stream)
end

# CAR_MSG_WHEEL (game/car.h): a wheel came off (CarOnWheelOff).
class C_CarWheelMessage size=0x20 vtable=0x8108b0:2
  0x04 int m_kind                              # 2
  0x08 int wheel                               # 8 bits
  0x0c int health                              # 10 bits; at 0 or less the velocity follows
  0x10 S_vector4 velocity                      # quantised as a position
  vslot 0x00 Read | void __thiscall Read(C_CarWheelMessage *this, C_BitStream *stream)
  vslot 0x01 Write | void __thiscall Write(C_CarWheelMessage *this, C_BitStream *stream)
end

# A rolling body's parameters (C_RigidBody 0x004cd550 via C_Debris::SetupWheel 0x005fdc00; the
# car's thrown-off wheels; game/world.h).
struct S_WheelDebrisParams size=0x2c
  0x00 S_vector4 velocity
  0x10 float radius
  0x14 float width
  0x18 float weight                            # kg (the car's: 10)
  0x1c float unknown1C                         # the wheel's +0x1B0
  0x20 float unknown20                         # the wheel's +0x1C0
  0x24 void* callback                          # T_RigidBodyCallback (the context: null)
  0x28 bool moveCollision
end

class C_Car : C_Actor size=0x25c0 vtable=0x810778:56
  0x090 C_Vehicle m_vehicle
  0x2360 void* m_netObject                     # C_NetObject (network games)
  0x2370 S_NetQuantization m_quantizations[2] # network games: handed to the net object (StartMission)
  0x2470 S_PerfTimer m_respawnTimer            # network games: the sunk / wrecked car's 38 s
  0x2480 S_vector4 m_startPosition             # where StartMission found the car (RestartMission)
  0x2490 S_vector4 m_startDirection
  0x24a0 uint8_t m_stateChanged                # the server sends the item store next tick (CAR_MSG_STORE)
  0x24a1 bool m_started                        # StartMission started the vehicle (once)
  0x24a4 I3D_model* m_wreckModel               # "C" + the model's file name (null when it did not load)
  0x24a8 uint32_t m_restrictions               # CAR_PULLS_LEFT 1, CAR_PULLS_RIGHT 2, CAR_NO_THROTTLE 4
  0x24ac C_Human* m_driver
  0x24b0 int m_lastAttacker                    # net id of who last hit the car (-1 none)
  0x24b4 void* m_seatWeapons[10]               # C_SeatWeapon per seat (StartMission; part 3)
  0x24dc int m_periodicLink                    # -1 when made (PeriodicLinks_Add)
  0x24e0 S_CarParts m_parts
  0x2548 bool m_wreckShown                     # the model shows the wreck's meshes (ShowWreck; RestoreModel)
  0x2549 uint8_t m_deadlyReported              # CAR_MSG_DEADLY_GROUND sent
  0x254a uint8_t m_wrecked                     # Wreck (0x004dbf00)
  0x254c I3D_frame* m_chargeVisual              # the placed charge's copy of its model
  0x2550 int m_chargeTime                      # ms to the charge's blast (-1 none)
  0x2554 int m_chargeOwner                     # net id of who placed it
  0x2560 S_vector4 m_chargePos
  0x2570 S_vector4 m_chargeNormal
  0x2580 bool m_vehiclesAllowed                # network games: the server allows vehicles (a client's car is solid)
  0x2584 int m_impulseSent                     # GetPerfTimeMs of the last CAR_MSG_IMPULSE
  0x2588 int* m_seatLeftTimes                  # network games: per seat, when its occupant left
  0x258c int* m_seatLeftIds                    # ... and its net id (-1 none)
  0x2594 int m_marksTime                       # ms of driving since the marks
  0x2598 std::vector<S_CarCheckpointMark*> m_marks
  0x25a8 std::vector<uint16_t> m_marksAhead
end
