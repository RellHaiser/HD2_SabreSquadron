# The vehicles (src/game/vehicle_params.h): the car's parameter block, kept at C_Vehicle +0xFDC
# (C_Car +0x106C). The vehicle itself (C_Vehicle, vtable 0x008108f8, 0x2298 bytes) stays an auto
# class (90_auto.spec) for now: its recovered layout lives in Ghidra.

# The parameter block (0x1240 bytes). Field comments: what ApplyParams 0x004ed270 checks and
# where the value goes (v = the vehicle, w = the axle's wheels).
struct S_VehicleParams size=0x1240
  0x000 float mass                              # kg > 0 (v+0x91C)
  0x004 float unknown004[19]
  0x050 float gearRatios[16]                    # [0] reverse, [1..15] the forward gears
  0x090 float shiftUpPercent[16]                # of the revolutions idle..limit
  0x0d0 float shiftDownPercent[16]
  0x110 float unknown110[13]
  0x144 float centreOfMass[3]                   # 0..1 per axis of the body's box (v+0x920)
  0x150 float gravity                           # m/s2 > 0 (v+0x788)
  0x154 float tankSize                          # litres (v+0x2220)
  0x158 float unknown158
  0x15c float axleUnknown15C[12]                # > 0, x1000 -> w+0x160
  0x18c float axleUnknown18C[12]                # >= 0.009 -> w+0x164
  0x1bc float axleUnknown1BC[12]                # > 0 -> w+0x170
  0x1ec float axleUnknown1EC[12]                # > 0 -> w+0x174
  0x21c float axleUnknown21C[12]                # degrees -10..10 -> w+0x178
  0x24c float axleUnknown24C[12]                # degrees -10..10 -> w+0x150
  0x27c float axleUnknown27C[12]                # degrees >= 0 -> w+0x154
  0x2ac float axleUnknown2AC[12]                # degrees >= 0 -> w+0x158
  0x2dc int axleTyreType[12]                    # tables\tyres.bin id -> w+0x17C
  0x30c float axleUnknown30C[12]                # > 0, x1000 -> w+0x18C
  0x33c float axleUnknown33C[12]                # degrees >= 1 -> w+0x194
  0x36c float axleUnknown36C[12]                # degrees >= 1 -> w+0x198
  0x39c float axleUnknown39C[12]                # 0..1 -> w+0x19C
  0x3cc float axleUnknown3CC[12]                # 0..1 -> w+0x190
  0x3fc float axleUnknown3FC[12]                # > 0 -> w+0x1B8
  0x42c float axleUnknown42C[12]                # > 0 -> w+0x1BC
  0x45c float axleUnknown45C[12]                # >= 0 -> w+0x1C0
  0x48c float axleUnknown48C[12]                # > 0, x1000 -> w+0x1C4
  0x4bc float axleUnknown4BC[12]                # > 0 -> w+0x1C8
  0x4ec float axleUnknown4EC[12]                # > 0 -> w+0x1CC
  0x51c float axleUnknown51C[12]                # >= 0 -> w+0x1D0
  0x54c float axleUnknown54C[12]                # percent 0..100 -> w+0x1F8
  0x57c float axleLeftShare[12]                 # percent of v+0x71C -> left wheel +0x1F4
  0x5ac float axleRightShare[12]                # the same, right wheel
  0x5dc float axleUnknown5DC[12]                # degrees 1..90 -> w+0x21C
  0x60c float axleUnknown60C[12]                # degrees 1..90 (<= +0x63C) -> w+0x220
  0x63c float axleUnknown63C[12]                # degrees 1..90 -> w+0x224
  0x66c float inertia[3]                        # > 0; v gets 1/x (+0x234, +0x238, +0x230)
  0x678 float unknown678                        # >= 0 -> v+0x71C
  0x67c float unknown67C                        # > 0 -> v+0x720
  0x680 float unknown680                        # >= 0 -> v+0x708
  0x684 float unknown684                        # > 0 -> v+0x70C
  0x688 float unknown688                        # >= 0 -> v+0x6F4
  0x68c float unknown68C                        # >= 1 -> v+0x6F8
  0x690 int unknown690                          # -> v+0x674 (a count; Setup's default 20)
  0x694 float unknown694                        # -> v+0x518
  0x698 uint8_t unknown698                      # -> v+0x2289
  0x69c float unknown69C                        # -> v+0x510
  0x6a0 float unknown6A0                        # -> v+0x514
  0x6a4 int unknown6A4                          # >= 0 -> v+0x754
  0x6a8 float unknown6A8                        # km/h >= 0 -> v+0x758 m/s
  0x6ac float unknown6AC                        # >= 0 -> v+0x75C
  0x6b0 float unknown6B0                        # >= +0x6AC -> v+0x760
  0x6b4 float unknown6B4                        # degrees -> v+0x504
  0x6b8 float unknown6B8[3]                     # -> v+0x140
  0x6c4 float unknown6C4                        # -> v+0x67C
  0x6c8 float unknown6C8[5]                     # -> every wheel's +0x24C +0x248 +0x254 +0x250 +0x258
  0x6dc float unknown6DC[20]
  0x72c float unknown72C[21]
  0x780 char glassTexture[16]                   # the glass parts' cracked texture, e.g. "2ft19r.bmp"
  0x790 char glassTextureAlpha[16]              # its alpha, e.g. "2ft19r+.bmp"
  0x7a0 float unknown7A0[61]
  0x894 char sounds1[3][16]                     # sound group 1 (editor defaults like "engine_on.wav")
  0x8c4 char layers1A[5][16]
  0x914 char layers1B[5][16]
  0x964 char layers1C[5][16]
  0x9b4 char sound1_9B4[16]
  0x9c4 char sound1_9C4[16]
  0x9d4 char sound1_9D4[16]
  0x9e4 char sounds1_9E4[9][16]
  0xa74 char crash1A[4][16]
  0xab4 char crash1B[4][16]
  0xaf4 char sound1_AF4[16]
  0xb04 char sound1_B04[16]
  0xb14 float layers1AValues[8][5]
  0xbb4 float layers1BValues[8][5]
  0xc54 float layers1CValues[8][5]
  0xcf4 float unknownCF4                        # 0..100.001
  0xcf8 float unknownCF8
  0xcfc float unknownCFC[5]
  0xd10 float crash1Values[4]                   # km/h (m/s x mass -> v+0xC38..)
  0xd20 char sounds2[4][16]                     # sound group 2: the car's own ("blitz_on.wav", ...)
  0xd60 char layers2A[5][16]                    # e.g. "blitz_1.wav"
  0xdb0 char layers2B[5][16]
  0xe00 char layers2C[5][16]
  0xe50 char sound2_E50[16]
  0xe60 char sound2_E60[16]                     # e.g. "afodrive.wav"
  0xe70 char sound2_E70[16]
  0xe80 char sounds2_E80[9][16]                 # handbrake, gears, skid, .., "pneu.wav"
  0xf10 char crash2A[4][16]                     # e.g. "carcrx2_a.wav"
  0xf50 char crash2B[4][16]
  0xf90 char sound2_F90[16]
  0xfa0 char sound2_FA0[16]
  0xfb0 float unknownFB0[4]
  0xfc0 float layers2AValues[8][5]
  0x1060 float layers2BValues[8][5]
  0x1100 float layers2CValues[8][5]
  0x11a0 float unknown11A0[6]
  0x11b8 float unknown11B8[9]
  0x11dc float crash2Values[4]                  # km/h (m/s x mass -> v+0xF38..)
  0x11ec uint8_t axleFlag2[12]                  # the wheels' flag 0x2
  0x11f8 uint8_t axleFlag1[12]                  # the wheels' flag 0x1
  0x1204 uint8_t axleFlag4[12]                  # the wheels' flag 0x4
  0x1210 uint8_t axleFlag800[12]                # the wheels' flag 0x800
  0x121c uint8_t unknown121C                    # -> v+0x228C
  0x121d uint8_t unknown121D                    # -> v+0x228F
  0x121e uint8_t unknown121E                    # -> v+0x2290
  0x121f uint8_t unknown121F                    # set: v+0x550 = 2
  0x1220 uint8_t unknown1220                    # -> v+0x228D
  0x1221 uint8_t layers1AFlags[5]
  0x1226 uint8_t layers1BFlags[5]
  0x122b uint8_t crash1Flags[4]
  0x122f uint8_t layers2AFlags[5]
  0x1234 uint8_t layers2BFlags[5]
  0x1239 uint8_t crash2Flags[4]
end

# A growable 16-byte-aligned array (_aligned_malloc; grows by growBy elements), like S_Vector4Array.
struct S_AlignedArray size=0x10
  0x00 void* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end

# Records of the vehicle's lists (src/game/vehicle.h). The track strips' vertices that follow a
# wheel (+0x56C left, +0x57C right; STRIPL* / STRIPR* visuals).
struct S_VehicleTrack size=0x14
  0x00 I3D_frame* strip
  0x04 uint16_t* vertices                     # owned, [count]
  0x08 float* offsets                         # owned, [count]: heights under the wheel's centre
  0x0c int count
  0x10 uint16_t wheel                         # an index into C_Vehicle::m_wheels
end
# The lamps (+0x590; LIGHT<xx>* frames, kind 1..9 = IN L/R, BI L/R, LI L/R, RE, BR, BA) and the
# PROJECTOR* records (+0x5A0) of SetupLights 0x004e7450.
struct S_VehicleLamp size=0x20
  0x00 I3D_frame* frame
  0x04 I3D_frame* billboard                   # BILLBOARD* below (switched off)
  0x08 I3D_projector_visual* projector        # own (CreateLamp) or the PROJECTOR's (shared)
  0x0c I3D_light* light
  0x10 uint32_t flags                         # 0x80000000: shared with a +0x5A0 record
  0x14 float health                           # ResetCondition: C_Vehicle +0x560; Deform
  0x18 uint16_t kind
  0x1a uint16_t part                          # damage part index (+0x664), 0xFFFF none
  0x1c uint16_t projectorIndex                # +0x5A0 index when shared
  0x1e uint16_t unknown1E
end
struct S_VehicleProjector size=0x1c
  0x00 I3D_frame* frame                       # the PROJECTOR* dummy
  0x04 I3D_projector_visual* projector
  0x08 I3D_light* light
  0x0c IImage* textures[3]                    # "[both,left,right]"
  0x18 uint16_t state                         # ResetCondition: 3
  0x1a uint16_t unknown1A
end
# The damage parts (+0x664; AddVehiclePart 0x004fa520 per "[DEFORM <type>]" visual; type 1 body,
# 2 bumper, 3 glass, 4 mirror, 5 light, 6 wing, 7 plate (SPZ), 8 door, 9 roof, 10 spare wheel).
struct S_VehiclePart size=0x3c
  0x00 I3D_frame* frame                       # the main visual
  0x04 void* frames                           # owned std::vector<I3D_frame*>*: a "+DEFORM" group's others
  0x08 I3D_mesh* mesh                         # its own copy of the mesh (a group's: the visual's)
  0xc I3D_texture* glassTexture               # glass: the other texture (cracked, or intact while broken)
  0x10 char* name                             # owned: the short name
  0x14 float fullImpulse                      # m/s x mass: a full dent, all health lost
  0x18 float minImpulse                       # m/s x mass: no damage below it
  0x1c float fullHealth
  0x20 float crackHealth                      # glass and lights: cracked below it
  0x24 float health                           # RepairParts: fullHealth
  0x28 float absorbed                         # percent
  0x2c S_vector maxDent                       # m, along the part's axes
  0x38 uint8_t state                          # 1, 4 deformed, 8 glass broken
  0x3a uint8_t type                           # 1..10 (also the ApplyParams parameter group)
  0x3b uint8_t group                          # a "+DEFORM" group's number
end

# Setup 0x004e2e30's records of the model's frames (src/game/vehicle.h; arrays: S_AlignedArray
# layouts with a typed data pointer).
struct S_VehicleExhaust size=0x30             # EXHAUST* (+0x744)
  0x00 S_vector4 pos                          # through the body's local matrix
  0x10 S_vector4 dir
  0x20 int effect                             # car effect slot (-1 none)
end
struct S_VehicleExhaustArray size=0x10
  0x00 S_VehicleExhaust* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_VehicleCollisionDesc size=0x8
  0x00 uint32_t unknown00                     # 0x40
  0x04 uint8_t unknown04                      # 1
  0x05 uint8_t hasParent
end
struct S_VehicleSpecialCollision size=0x60    # SPECCOLL* (+0x2238; corners +0x2228)
  0x00 S_matrix invParentMatrix               # identity below the model
  0x40 S_vector4 localPos
  0x50 I3D_frame* parent                      # null below the model
end
struct S_VehicleSpecialCollisionArray size=0x10
  0x00 S_VehicleSpecialCollision* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_VehicleDoor size=0x80                # DOOR<n> (+0xFB8)
  0x00 S_quat closedRot
  0x10 S_vector4 pos
  0x20 I3D_frame* frame
  0x24 float angle                            # open by (UpdateDoors)
  0x28 float targetAngle
  0x2c float unknown2C                        # 1.25: open
  0x30 float speed                            # 1.45 per second
  0x40 S_vector4 windowPos
  0x50 I3D_frame* window                      # WINDOW* below the door
  0x54 float windowX
  0x58 float windowY
  0x5c float windowLowY                       # y - 0.5
  0x60 float unknown60                        # 0.3
  0x64 uint16_t windowPart                    # index into +0x664 (0xFFFF none)
  0x66 bool windowMoving                      # (UpdateWindows)
  0x67 bool windowDown
  0x70 uint8_t flags                          # 2: moving; 4: "[..I..]" property or backward; 8: backward
  0x72 uint16_t doorPart                      # index into +0x664 (0xFFFF none)
end
struct S_VehicleDoorArray size=0x10
  0x00 S_VehicleDoor* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_VehicleSeat size=0x40                # SEAT<n> (+0xFA8)
  0x00 S_vector4 pos
  0x10 S_vector4 dir
  0x20 float load                             # kg (SetSeatLoad)
  0x24 uint32_t flags                         # SEAT_FLAG_* (0x400 taken)
  0x28 S_VehicleDoor* door                    # the door of the same number
  0x2c I3D_frame* frame
  0x30 C_Actor* occupant
  0x34 void* camera                           # C_CameraFrameKeeper (the car's StartMission)
  0x38 int text                               # the "name<n>" property's text id (-1 none)
end
struct S_VehicleSeatArray size=0x10
  0x00 S_VehicleSeat* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_VehicleBallast size=0x30             # BALLAST<n> (+0xFC8)
  0x00 S_vector4 pos
  0x10 S_vector4 dir
  0x20 float mass                             # kg
end
struct S_VehicleBallastArray size=0x10
  0x00 S_VehicleBallast* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_VehicleStrut size=0x20               # AXIS* (+0x6C4 whole axles, +0x6D4 one side)
  0x00 S_vector offset                        # the "[x y z]" property, cm / 100
  0x0c float unknown0C
  0x10 I3D_frame* frame
  0x14 int axle
  0x18 uint8_t left
  0x19 uint8_t single
end
struct S_VehicleStrutArray size=0x10
  0x00 S_VehicleStrut* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_BodyCollisionPoint size=0x50         # dummies below a BODYCOL* box
  0x00 S_vector pos
  0x0c float unknown0C
  0x10 S_vector4 move                           # CollideBody: its move in the world over the step
  0x30 S_vector4 start                          # CollideBody: where it was in the world before the step
  0x40 I3D_frame* frame
  0x44 float radius
  0x48 S_BodyCollisionPoint* next
end
struct S_BodyCollisionBox size=0x80           # BODYCOL* (a tree from C_Vehicle +0x794)
  0x00 S_vector min
  0x0c float unknown0C
  0x10 S_vector max
  0x1c float unknown1C
  0x20 S_vector pos
  0x2c float unknown2C
  0x30 S_vector4 move                           # CollideBody: its move in the world over the step
  0x50 S_vector4 start                          # CollideBody: where it was before the step
  0x60 S_BodyCollisionPoint* points
  0x64 S_BodyCollisionBox* children
  0x68 I3D_frame* frame
  0x6c I3D_frame* parent                      # the moving parent (null: the model)
  0x70 float halfHeight
  0x74 S_BodyCollisionBox* next
  0x78 uint8_t flagB
end

# An original mesh of a deformed damage part (C_Vehicle +0x654; RepairParts copies its vertex
# positions back).
struct S_VehicleMeshOriginal size=0x10
  0x00 I3D_mesh* mesh                         # one reference
  0x04 int refs                               # the parts deformed from it
  0x08 char* partName                         # owned
  0x0c char* modelFile                        # owned: C_Vehicle::m_modelFileName
end
# A wheel's skid mark (S_VehicleWheel +0x204, a ring with the current index at +0x218; made by
# Start 0x004ea780, laid per tick 0x004fcbd0).
struct S_VehicleSkidMark size=0xc
  0x00 I3D_frame* frame0                      # the strip ("shlaif01.i3d"); Reset switches it off / unlinks it
  0x04 I3D_frame* frame1                      # the corner to the next ("shlaif02.i3d")
  0x08 uint8_t active                         # being laid
  0x09 uint8_t fresh                          # a mark's first segment (no corner before it)
  0xa uint8_t cornerShown
end
# A hard contact of the body this tick (+0x798; CollideBody; the first three have effect slots at
# +0x7A8).
struct S_VehicleBodyContact size=0x20
  0x00 S_vector4 point                        # the point's sphere's side towards it before the step
  0x10 int material                           # the surface's (g_carSurfaceOfMaterial)
end
# CollideBody's work lists (the boxes 0x0089c220, a hit box's points 0x0089c210): the farthest
# first, taken from the end.
struct S_BodyWorkEntry size=0x20
  0x00 void* item                             # S_BodyCollisionBox* / S_BodyCollisionPoint*
  0x04 float distance                         # from the point ahead, less the half height / radius
end
struct S_BodyWorkList size=0x10
  0x00 S_BodyWorkEntry* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
struct S_VehicleBodyContactArray size=0x10
  0x00 S_VehicleBodyContact* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end
# The special collision volumes (+0x2268; Start makes one per SPECCOLL* record).
struct S_VehicleVolumeArray size=0x10
  0x00 S_CollisionVolume* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end

# The tyre types of tables\tyres.bin (game/pending.h; the global vector 0x0089c174). A type's
# per-surface entries mirror a wheel's own values from S_VehicleWheel +0x18C (which the solver
# takes for tyre index 0).
struct S_TyreSurface size=0x34
  0x00 int surface                              # inferred: a ground surface kind
  0x04 float unknown04                          # the force cap (w+0x18C)
  0x08 float unknown08                          # the peak slip (w+0x194)
  0x0c float unknown0C                          # w+0x198
  0x10 float unknown10                          # w+0x19C
  0x14 float unknown14                          # w+0x190
  0x18 float unknown18                          # w+0x1B8
  0x1c float unknown1C                          # w+0x1BC
  0x20 float unknown20                          # w+0x1C0
  0x24 float unknown24                          # the cap on the rim (w+0x1C4)
  0x28 float unknown28                          # w+0x1C8
  0x2c float unknown2C                          # w+0x1CC
  0x30 float unknown30                          # w+0x1D0
end

# A std::vector<S_TyreSurface> as MSVC 6 lays it out.
struct S_TyreSurfaceList size=0x10
  0x04 S_TyreSurface* first
  0x08 S_TyreSurface* last
  0x0c S_TyreSurface* storageEnd
end

struct S_TyreType size=0x28
  0x00 int id
  0x24 S_TyreSurfaceList* surfaces
end

# A material's values for the vehicles (game/pending.h; InitCarSurfaces 0x004d57a0 from the material
# table; the vector 0x0089c160, a material's entry in 0x0089bd60).
struct S_CarSurface size=0x30
  0x00 int material
  0x04 float unknown04                          # the smaller of the material's +0x88 / +0x8C
  0x08 float unknown08                          # its +0x88
  0x0c float unknown0C                          # the smaller of +0x90 / +0x94
  0x10 float unknown10                          # its +0x90
  0x14 float unknown14                          # its +0x98, 0..0.2
  0x18 float friction                           # its +0x80, 0..1: the body's sliding friction
  0x1c float bounce                             # its +0x84, 1..2: 1 + the body's restitution
  0x20 int dustEffect                           # its +0x9C: a rolling wheel's car effect
  0x24 int skidEffect                           # its +0xA0: a skidding wheel's
  0x28 int contactEffect                        # its +0xA4: the body contacts' (its absolute value)
  0x2c uint8_t unknown2C                        # materials 0x30, 0x33..0x37, 0x40, 0x41, 0x5F
  0x2d uint8_t skidMarks                      # the same materials (roads, rock, concrete, paving): skid marks
end
struct S_CarSurfaceList size=0x10
  0x04 S_CarSurface* first
  0x08 S_CarSurface* last
  0x0c S_CarSurface* storageEnd
end

# The vehicles' particle effects (game/car_effects.h): a record of tables\effects_car.def
# (0x0097b9ac, count 0x0097b9b0); the emitter property each is set as.
struct S_CarEffectDef size=0xf0
  0x00 char texture[32]
  0x20 char alphaTexture[32]                    # used from 4 characters on
  0x40 float color1[4]                          # property 0x1D (dimmed by the light)
  0x50 float color2[4]                          # property 0x1E
  0x60 float rect[4]                            # the texture rectangle (its key selected)
  0x70 int unknown70                            # property 0
  0x74 int unknown74                            # property 1
  0x78 int unknown78                            # property 2
  0x7c int unknown7C                            # property 3
  0x80 float unknown80                          # property 4
  0x84 float unknown84                          # property 5
  0x88 float unknown88                          # property 6
  0x8c float unknown8C                          # property 7 (truncated)
  0x90 int unknown90                            # property 8
  0x94 int unknown94                            # property 9
  0x98 uint32_t flags                           # property 0x13 (blend) & 0x107
  0x9c uint32_t unknown9C
  0xa0 float unknownA0                          # property 0xA
  0xa4 float unknownA4                          # property 0xB
  0xa8 float unknownA8                          # property 0xC
  0xac float unknownAC                          # property 0x22
  0xb0 S_vector velocity                        # properties 0xD..0xF
  0xbc S_vector velocity2                       # properties 0x10..0x12
  0xc8 float unknownC8                          # property 0x18
  0xcc float unknownCC                          # property 0x19
  0xd0 uint32_t unknownD0
  0xd4 float unknownD4                          # property 0x1A
  0xd8 uint32_t unknownD8
end
# An emitter of the car effects' pool (InitCarEffects: 30 "Car_particle" emitters).
struct S_CarEmitter size=0x30
  0x00 S_vector4 velocity                       # its effect's (w not set)
  0x10 S_vector4 velocity2
  0x20 I3D_frame* visual                        # an I3DVISUAL_EMITTER
  0x24 int effect
  0x28 bool active                              # taken by a vehicle
  0x29 bool stopping                            # let go: free once its particles are gone
end
# The pool (0x0089bd50; grows by 2).
struct S_CarEmitterArray size=0x10
  0x00 S_CarEmitter* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end

# std::vector<S_TyreType> (g_tyreTypes, 0x0089c174).
struct S_TyreTypeList size=0x10
  0x04 S_TyreType* first
  0x08 S_TyreType* last
  0x0c S_TyreType* storageEnd
end

# A wheel (0x280 bytes, _aligned_malloc'd by the WH* frame callback 0x004f9be0; a list from
# C_Vehicle +0xF60 through +0x278, and the array C_Vehicle +0xF64). Fields: what ApplyParams writes.
struct S_VehicleWheel size=0x280
  0x0 S_quat unknown000                       # Start: about x by +0x154, then z by +0x158 (negated on the right)
  0x10 S_quat unknown010                      # Start: about z by +0x150 + +0x158 (negated on the left)
  0x20 S_vector4 holderPos                    # the holder's local position
  0x30 S_vector4 modelPos                     # the holder in the model's space
  0x40 S_vector4 worldPos                     # Start: the frame's world position
  0x50 S_vector4 unknown050                   # Start: worldPos lowered by +0x1E4 along the model's y
  0x60 S_vector4 unknown060                   # Start: 1 below unknown050
  0x70 S_vector unknown070                    # the solver: the load along unknown080 (inferred: the suspension's force)
  0x80 S_vector unknown080                    # inferred: the ground's normal at the contact
  0x90 S_vector unknown090                    # unknown080 of the pass before
  0xa0 S_vector unknown0A0                    # inferred: the contact's velocity
  0xb0 S_vector unknown0B0                    # inferred: the unit rolling direction along the ground
  0xc0 S_vector unknown0C0
  0xd0 S_vector unknown0D0
  0xe0 S_vector unknown0E0                    # the solver: the tyre's force (inferred)
  0xf0 S_vector4 skidLeft                     # LaySkidMark: the finished segment's end corners
  0x100 S_vector4 skidRight
  0x110 S_vector4 skidDir                     # the mark's direction (the contact's velocity at its start)
  0x120 S_vector4 skidStart                   # its current segment's start
  0x130 S_plane groundPlane                   # the ground's plane last found under the wheel
  0x140 S_CollisionVolume* groundPlaneVolume  # its volume (null: none kept)
  0x144 char* name                            # copy of the frame's name
  0x148 I3D_frame* frame
  0x14c I3D_frame* holder                     # DWHL* / DWHR*
  0x150 float unknown150
  0x154 float unknown154
  0x158 float unknown158
  0x15c float unknown15C                      # the frame's local x
  0x160 float unknown160
  0x164 float unknown164
  0x168 float unknown168                      # inferred: the load (N)
  0x16c float extension                       # how far the wheel hangs below its holder (0..unknown164)
  0x170 float unknown170
  0x174 float unknown174
  0x178 float unknown178
  0x17c int tyreType                          # tables\tyres.bin id (-1: none)
  0x180 int tyreIndex                         # its index (-1: none)
  0x184 int surfaceEntry                      # the tyre type's entry for surface (-1 none)
  0x188 int surface                           # inferred: the ground's surface kind
  0x18c float unknown18C
  0x190 float unknown190
  0x194 float unknown194
  0x198 float unknown198
  0x19c float unknown19C
  0x1a8 float unknown1A8                      # the solver: 1 upright .. 0 tilted 10 degrees
  0x1ac float unknown1AC                      # the solver: the grip's share past the peak slip
  0x1b0 float unknown1B0                      # the solver: the grip along the wheel (N)
  0x1b4 float unknown1B4                      # the solver: the grip across it (N)
  0x1b8 float unknown1B8
  0x1bc float unknown1BC
  0x1c0 float unknown1C0
  0x1c4 float unknown1C4
  0x1c8 float unknown1C8
  0x1cc float unknown1CC
  0x1d0 float unknown1D0
  0x1d4 float unknown1D4                      # the solver: the slip angle's sine
  0x1d8 float radius                          # half the frame's box height
  0x1dc float innerRadius                     # 0.8 x the radius
  0x1e0 float unknown1E0                      # + 0.1 per track strip
  0x1e4 float unknown1E4                      # sqrt(radius^2 + width^2)
  0x1e8 float unknown1E8                      # sqrt(innerRadius^2 + width^2)
  0x1ec float unknown1EC                      # acos(radius / unknown1E4)
  0x1f0 float unknown1F0
  0x1f4 float unknown1F4
  0x1f8 float unknown1F8
  0x1fc uint32_t flags
  0x200 S_CollisionVolume* ground             # the volume under the wheel this pass
  0x204 std::vector<S_VehicleSkidMark> skidMarks # a ring
  0x214 float width                           # the box's x extent or the holder's [cm]
  0x218 uint32_t skidMark                     # the ring's current one
  0x21c float unknown21C
  0x220 float unknown220
  0x224 float unknown224
  0x228 float maxBend                         # radians
  0x22c float bend                            # radians: the rolling direction turned about the car's y
  0x230 float health                          # ResetCondition: fullHealth
  0x234 float fullHealth
  0x238 float absorbed                        # percent
  0x23c float fullImpulse                     # m/s x mass
  0x240 float minImpulse                      # m/s x mass
  0x244 float bendHealth
  0x248 float unknown248
  0x24c float unknown24C
  0x250 float unknown250
  0x254 float unknown254
  0x258 float unknown258
  0x25c float unknown25C[3]                   # bumps: [0] smooth, [1] bump length, [2] the bump now (more reach)
  0x268 float unknown268                      # the next bump's height
  0x26c float unknown26C                      # the distance in the bump cycle (Start: 1000)
  0x270 int effect                            # car effect slot (-1 none)
  0x274 float unknown274                      # the solver: the tyre force over what the grip allows
  0x278 S_VehicleWheel* next
end

# The vehicle (C_Car +0x90; ctor 0x004e2900; 0x22D0 bytes). The members src/game/vehicle.h ports, and one
# m_XXX field per other offset its members access (typed by use); refined as the vehicle is ported.
# The "vtable" is a one-slot sound loader interface.
class C_Vehicle size=0x22d0 vtable=0x8108f8:1
  vslot 0x00 OpenSound | int __thiscall OpenSound(C_Vehicle * this, I3D_sound * sound, char * fileName, uint flags)
  0x10 S_vector m_unknown010
  0x20 S_vector m_unknown020
  0x30 S_vector4 m_unknown030                 # Start: m_modelPos
  0x40 S_vector m_unknown040
  0x50 S_vector4 m_frontAxleCentre
  0x60 S_vector4 m_rearAxleCentre
  0x70 S_plane m_frontGround                  # the ground's plane under the front axle centre
  0x80 S_plane m_rearGround                   # under the rear one
  0xb0 S_vector4 m_previousModelPos           # Integrate: before the step
  0xc0 S_vector4 m_previousAxisZ
  0xd0 S_vector4 m_previousAxisY
  0xe0 S_vector4 m_previousAxisX
  0xf0 S_vector4 m_scaledSize                 # m_size x the model's world scale
  0x100 S_vector4 m_size                      # the model's hierarchy box extent
  0x110 S_vector4 m_scaledBoxOffset
  0x120 S_vector4 m_boxOffset                 # minus the box's min corner
  0x130 S_vector4 m_smokePos                  # SMOKE_MOTOR
  0x140 S_vector4 m_unknown140
  0x150 S_vector4 m_headPos                   # HEAD
  0x160 S_vector4 m_modelPos                  # the model's world position at setup
  0x170 S_vector4 m_velocityDir               # m_velocity normalized (ApplyImpulse)
  0x180 S_vector4 m_bodyMin
  0x190 S_vector4 m_bodyMax
  0x1a0 S_vector4 m_steeringAxis              # SWHEEL's local z axis
  0x1b0 S_vector4 m_unknown1B0                # where ApplyImpulse's lever arms start
  0x1c0 S_vector4 m_unknown1C0
  0x1d0 S_vector4 m_unknown1D0
  0x1e0 S_vector4 m_unknown1E0
  0x1f0 S_vector4 m_centreOfMass
  0x200 S_vector4 m_weight
  0x210 S_vector m_unknown210                 # ApplyImpulse subtracts the turn (angular velocity?)
  0x220 S_vector4 m_unknown220
  0x230 S_vector4 m_inverseInertia
  0x240 S_vector4 m_modelAxisZ                # the model's world matrix rows 2, 0, 1
  0x250 S_vector4 m_modelAxisX
  0x260 S_vector4 m_modelAxisY
  0x270 S_vector m_unknown270
  0x280 S_vector m_unknown280
  0x290 S_matrix m_modelMatrix                # at setup
  0x2d0 S_matrix m_invModelMatrix
  0x310 S_matrix m_unknown310                 # the torque back from the body's space
  0x350 S_matrix m_unknown350                 # the torque into the body's space
  0x390 S_matrix m_previousModelMatrix
  0x3d0 S_matrix m_previousInvModelMatrix
  0x410 S_vector4 m_velocity                  # ApplyImpulse adds impulse / mass
  0x420 S_vector4 m_previousVelocity          # Step: before the solver
  0x430 S_CollisionVolume m_bodyVolume        # the CENTRE box's
  0x490 S_CollisionVolume m_volume490         # the second one
  0x4f0 S_CollisionVolume* m_frontGroundVolume # their volumes
  0x4f4 S_CollisionVolume* m_rearGroundVolume
  0x4f8 uint32_t m_unknown4F8[2]
  0x500 uint32_t m_unknown500
  0x504 float m_unknown504
  0x508 float m_unknown508
  0x50c float m_unknown50C
  0x510 float m_unknown510
  0x514 float m_unknown514
  0x518 float m_unknown518
  0x51c float m_unknown51C
  0x520 float m_unknown520
  0x524 float m_unknown524
  0x528 float m_unknown528
  0x52c float m_unknown52C
  0x530 float m_unknown530
  0x534 uint32_t m_unknown534                 # 0xF02 when made
  0x538 int m_unknown538
  0x53c int m_unknown53C                      # 2
  0x540 int m_unknown540
  0x544 I3D_frame* m_unknown544               # Start: the model's parent
  0x548 float m_unknown548                    # 1
  0x54c int m_unknown54C
  0x550 int m_unknown550
  0x554 int m_unknown554                      # above 0: frozen
  0x558 float m_lampFullImpulse               # m/s x mass
  0x55c float m_lampMinImpulse                # m/s x mass
  0x560 float m_lampFullHealth
  0x564 float m_lampBreakHealth
  0x568 float m_lampAbsorbed                  # percent
  0x56c std::vector<S_VehicleTrack> m_tracksL
  0x57c std::vector<S_VehicleTrack> m_tracksR
  0x58c uint32_t m_unknown58C
  0x590 std::vector<S_VehicleLamp> m_lamps
  0x5a0 std::vector<S_VehicleProjector> m_projectors
  0x5b0 uint32_t m_unknown5B0
  0x5b4 uint32_t m_unknown5B4
  0x5b8 float m_unknown5B8                    # 0.4
  0x5bc float m_unknown5BC                    # 0.3
  0x5c0 float m_unknown5C0
  0x5c4 float m_unknown5C4                    # 0.2
  0x5c8 float m_unknown5C8                    # 0.3
  0x5cc float m_unknown5CC
  0x5d0 uint32_t m_unknown5D0[3]
  0x5dc float m_unknown5DC
  0x5e0 uint32_t m_unknown5E0
  0x5e4 float m_unknown5E4
  0x5e8 float m_unknown5E8
  0x5ec float m_unknown5EC
  0x5f0 float m_unknown5F0
  0x5f4 float m_unknown5F4                    # 0.5
  0x5f8 float m_unknown5F8                    # 0.5
  0x5fc uint32_t m_unknown5FC                 # only saved and loaded so far
  0x600 uint32_t m_unknown600                 # the brake clears 0x01000000
  0x604 int m_unknown604                      # ms trying to rest
  0x608 float m_unknown608
  0x60c float m_unknown60C
  0x610 float m_unknown610                    # > 0 needed to start (+0xF6C 0x4000)
  0x614 float m_unknown614
  0x618 float m_unknown618
  0x61c float m_unknown61C
  0x620 float m_unknown620
  0x624 float m_unknown624
  0x628 float m_unknown628
  0x62c float m_unknown62C
  0x630 int m_unknown630                      # the engine smoke's car effect
  0x634 int m_unknown634                      # the second one (below +0x628)
  0x638 int m_unknown638                      # car effect slot
  0x63c float m_unknown63C                    # > 0 needed to change gear (+0xF6C 0x4000)
  0x640 float m_unknown640
  0x644 float m_unknown644
  0x648 float m_unknown648
  0x64c float m_unknown64C
  0x650 int m_unknown650
  0x654 std::vector<S_VehicleMeshOriginal> m_meshOriginals
  0x664 std::vector<S_VehiclePart> m_parts
  0x674 int m_unknown674                      # a count (default 20)
  0x678 int m_unknown678                      # 50
  0x67c float m_unknown67C
  0x680 uint32_t m_actorImpulseCallback      # [0] CollideBody: the impulse for the actor met (car 0x004d4230)
  0x684 uint32_t m_actorTouchCallback        # [1] after a contact with an actor taken as the world (0x004d4450)
  0x688 uint32_t m_actorResponseCallback     # [2] a moving actor's side of a contact (0x004d4130; gets the vehicle)
  0x68c uint32_t m_actorFirmCallback         # [3] whether the actor counts as the world (0x004d4620)
  0x690 uint32_t m_impactCallback            # [4] a wheel's or the body's impulse (0x004d4740)
  0x694 uint32_t m_694
  0x698 uint32_t m_698
  0x69c uint32_t m_69c
  0x6a0 uint32_t m_callback6A0                # Setup's seat callback (C_Vehicle::m_callbacks[8]; the car leaves it empty)
  0x6a4 uint32_t m_6a4
  0x6a8 uint32_t m_bodyGroundCallback        # [10] the volume a body point met (0x004d4a70)
  0x6ac uint32_t m_wheelGroundCallback       # [11] the volume a wheel's ray found (0x004d4b50)
  0x6b0 uint32_t m_6b0
  0x6b4 uint32_t m_6b4
  0x6b8 uint32_t m_callback6B8                # Setup's collision-part callback ([14]; empty)
  0x6bc uint32_t m_setupEmitterCallback      # [15] StartEffect: an emitter set up for an effect (car 0x004d5b10)
  0x6c0 uint32_t m_reuseEmitterCallback      # [16] StartEffect: the slot's emitter switched in place (0x004d62c0)
  0x6c4 S_VehicleStrutArray m_axleStruts
  0x6d4 S_VehicleStrutArray m_sideStruts
  0x6e4 I3D_frame* m_model
  0x6e8 float m_unknown6E8                    # Start: the turning circle
  0x6ec float m_unknown6EC
  0x6f0 float m_topSpeed
  0x6f4 float m_unknown6F4                    # the body's friction with +0x228C (CollideBody)
  0x6f8 float m_unknown6F8                    # its bounce (1 + restitution) with +0x228C
  0x6fc float m_throttle
  0x700 float m_unknown700                    # the throttle's second channel (mode 1)
  0x704 float m_unknown704
  0x708 float m_unknown708
  0x70c float m_unknown70C
  0x710 float m_unknown710                    # the handbrake's eased time
  0x714 float m_unknown714                    # the throttle of the step before
  0x718 float m_unknown718
  0x71c float m_unknown71C
  0x720 float m_unknown720
  0x724 float m_unknown724                    # the brake's eased time
  0x728 float m_unknown728
  0x72c float m_unknown72C
  0x730 float m_unknown730
  0x734 float m_unknown734
  0x738 float m_unknown738
  0x73c float m_drivenWheels
  0x740 float m_handbrake
  0x744 S_VehicleExhaustArray m_exhausts
  0x754 int m_unknown754
  0x758 float m_unknown758
  0x75c float m_unknown75C
  0x760 float m_unknown760
  0x764 float m_unknown764
  0x768 float m_unknown768
  0x76c float m_unknown76C
  0x770 float m_unknown770
  0x774 float m_unknown774
  0x778 float m_unknown778
  0x77c float m_unknown77C
  0x780 float m_unknown780                    # SetGear zeroes it
  0x784 float m_unknown784
  0x788 float m_gravity
  0x78c float m_unknown78C
  0x790 float m_unknown790                    # SetUnknown4f2630
  0x794 S_BodyCollisionBox* m_bodyCollision
  0x798 S_VehicleBodyContactArray m_bodyContacts
  0x7a8 int m_bodyContactEffects[3]           # car effect slots
  0x7b4 char* m_modelName
  0x7b8 char* m_modelFileName
  0x7bc int m_axleCount
  0x7c0 int m_wheelCount
  0x7c4 float m_unknown7C4                    # the brakes' easing step
  0x7cc float m_shiftUpRevs[16]
  0x810 float m_shiftDownRevs[16]
  0x850 float m_unknown850
  0x854 float m_unknown854
  0x858 float m_unknown858
  0x85c float m_unknown85C
  0x860 float m_unknown860
  0x864 float m_engineRevs                    # revolutions per second
  0x868 float m_previousRevs
  0x86c float m_unknown86C
  0x870 float m_unknown870
  0x874 int m_gear                            # -1 reverse, 0 neutral
  0x878 int m_requestedGear                   # SetGear's
  0x87c float m_unknown87C
  0x880 int m_gearCount
  0x884 float m_gearRatios[17]
  0x8c8 float m_speed                         # m/s
  0x8cc float m_previousSpeed
  0x8d0 float m_unknown8D0
  0x8d4 float m_unknown8D4
  0x8d8 float m_maxWheelCircumference
  0x8dc float m_maxWheelRadius
  0x8e0 float m_unknown8E0
  0x8e4 float m_brake
  0x8e8 float m_unknown8E8
  0x8ec float m_unknown8EC
  0x8f0 float m_unknown8F0
  0x8f4 float m_unknown8F4
  0x8f8 float m_unknown8F8
  0x8fc float m_unknown8FC
  0x900 float m_unknown900
  0x904 float m_unknown904
  0x908 float m_unknown908                    # SetUnknown4f23b0
  0x90c float m_unknown90C
  0x910 uint32_t m_unknown910
  0x914 float m_idleRevs
  0x918 float m_currentMass
  0x91c float m_mass
  0x920 float m_centreOfMassFraction[3]
  0x92c float m_unknown92C
  0x930 float m_unknown930
  0x934 float m_unknown934                    # SetSteering's scale
  0x938 float m_unknown938
  0x93c float m_unknown93C
  0x940 float m_unknown940
  0x944 float m_unknown944
  0x948 float m_steering
  0x94c float m_unknown94C
  0x950 float m_unknown950
  0x954 float m_unknown954
  0x958 float m_unknown958
  0x95c float m_unknown95C
  0x960 float m_unknown960
  0x964 float m_unknown964
  0x968 float m_unknown968
  0x96c float m_unknown96C
  0x970 float m_unknown970
  0x974 float m_unknown974
  0x978 float m_unknown978
  0x97c int m_sounds1[40]
  0xa1c float m_layers1[3][8][5]
  0xbfc float m_unknownBFC
  0xc00 float m_unknownC00
  0xc04 float m_unknownC04
  0xc08 float m_unknownC08
  0xc0c float m_unknownC0C
  0xc10 float m_unknownC10
  0xc14 float m_unknownC14
  0xc18 float m_unknownC18
  0xc1c float m_unknownC1C
  0xc20 float m_unknownC20[6]
  0xc38 float m_crash1Thresholds[4]
  0xc48 int m_sounds2[41]
  0xcec float m_unknownCEC
  0xcf0 float m_unknownCF0
  0xcf4 float m_unknownCF4
  0xcf8 float m_unknownCF8
  0xcfc float m_layers2[3][8][5]
  0xedc float m_unknownEDC
  0xee0 float m_unknownEE0
  0xee4 float m_unknownEE4
  0xee8 float m_unknownEE8[4]
  0xef8 float m_unknownEF8
  0xefc float m_unknownEFC
  0xf00 float m_unknownF00
  0xf04 float m_unknownF04
  0xf08 float m_unknownF08
  0xf0c float m_unknownF0C
  0xf10 float m_unknownF10[4]
  0xf20 float m_unknownF20[6]
  0xf38 float m_crash2Thresholds[4]
  0xf48 float m_unknownF48
  0xf4c float m_unknownF4C
  0xf50 float m_unknownF50
  0xf54 float m_unknownF54                    # 0.028
  0xf58 I3D_frame* m_steeringWheel            # SWHEEL
  0xf5c float m_fuel                          # litres
  0xf60 S_VehicleWheel* m_wheelList
  0xf64 S_VehicleWheel** m_wheels
  0xf68 float m_unknownF68
  0xf6c uint32_t m_unknownF6C                 # 0x8000DFF9
  0xf70 uint32_t m_unknownF70                 # 0x8000DFF9
  0xf74 uint32_t m_unknownF74
  0xf78 void* m_callbackContext               # the car
  0xf7c int m_unknownF7C
  0xf80 uint32_t m_unknownF80[10]
  0xfa8 S_VehicleSeatArray m_seats
  0xfb8 S_VehicleDoorArray m_doors
  0xfc8 S_VehicleBallastArray m_ballasts
  0xfd8 I3D_frame* m_body                     # BODY
  0xfdc S_VehicleParams m_params
  0x221c float m_revLimit
  0x2220 float m_tankSize
  0x2224 float m_unknown2224
  0x2228 S_Vector4Array m_specialCollisionCorners
  0x2238 S_VehicleSpecialCollisionArray m_specialCollisions
  0x2248 std::vector<S_VehicleCollisionDesc> m_specialCollisionDescs
  0x2258 std::vector<S_CollisionVolume*> m_specialVolumeList
  0x2268 S_VehicleVolumeArray m_specialVolumes
  0x2278 S_VehicleCollisionDesc m_bodyCollisionDesc# the CENTRE box's
  0x2280 uint16_t m_substeps                  # Integrate's solver steps
  0x2282 uint16_t m_unknown2282
  0x2284 uint16_t m_sinceRimScrape            # ms since a wheel scraped on its rim (Update: at most 1000; Start, Restart: 30000)
  0x2286 uint8_t m_wheelOnWater               # a wheel's ground or a body point met COLLISION_FLAG_WATER
  0x2287 uint8_t m_unknown2287
  0x2288 uint8_t m_unknown2288
  0x2289 uint8_t m_unknown2289
  0x228a uint8_t m_tracked                    # track strips
  0x228b uint8_t m_unknown228B
  0x228c uint8_t m_unknown228C                # the body's own friction / bounce (+0x6F4) instead of the surface's
  0x228d uint8_t m_unknown228D
  0x228e uint8_t m_unknown228E
  0x228f uint8_t m_unknown228F
  0x2290 uint8_t m_unknown2290
  0x2291 uint8_t m_driven                     # a driver sits in the car (C_Car::SetDriver)
  0x2292 uint8_t m_unknown2292
  0x2293 uint8_t m_unknown2293
  0x2294 uint8_t m_unknown2294
  0x2295 uint8_t m_unknown2295
  0x2296 uint8_t m_soundsOn                   # EnableSounds
  0x2297 uint8_t m_unknown2297
  0x2298 uint8_t m_unknown2298
  0x2299 uint8_t m_unknown2299
  0x229a uint8_t m_unknown229A
  0x229b uint8_t m_unknown229B                # the brake at once
  0x229c uint8_t m_unknown229C                # SetUnknown4f23b0's curve
  0x229d uint8_t m_automatic                  # inferred: in reverse the pedals swap
  0x229e uint8_t m_unknown229E
  0x229f uint8_t m_unknown229F
  0x22a0 uint8_t m_unknown22A0
  0x22a2 uint8_t m_unknown22A2
  0x22a3 uint8_t m_layers1AFlags[5]
  0x22a8 uint8_t m_layers1BFlags[5]
  0x22ad uint8_t m_crash1Flags[4]
  0x22b1 uint8_t m_layers2AFlags[5]
  0x22b6 uint8_t m_layers2BFlags[5]
  0x22bb uint8_t m_crash2Flags[4]
  0x22bf uint8_t m_engineOn
  0x22c0 uint8_t m_unknown22C0                # ApplyImpulse sets it
  0x22c1 uint8_t m_unknown22C1
  0x22c2 uint8_t m_unknown22C2
  0x22c3 uint8_t m_specialVolumesOn
  0x22c4 uint8_t m_mainVolumesInWorld
  0x22c5 uint8_t m_volume490InWorld
  0x22c6 uint8_t m_unknown22C6
  0x22c7 uint8_t m_unknown22C7
  0x22c8 uint8_t m_unknown22C8
end
