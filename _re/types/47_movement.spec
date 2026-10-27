# Movement of a human (S_HumanBody::movement, 0xDD0 bytes, 16-byte aligned; 0x00748dc0..0x0078a890,
# src/game/human_movement.*). A composite: the body's own state (collision volumes, the 25 joints,
# the shadow) and the cooperating parts below, which reference each other through pointers.
# Draft layout from the field accesses of the decompilation (unknownNNN: not identified yet).

# A joint limit of a pose bone toward one neighbour (6 per bone; angles in radians).
struct S_PoseLimit size=0x40
  0x00 S_vector4 max                      # (pi; not used by the chains)
  0x10 S_vector4 min                      # (-pi)
  0x20 S_vector4 unknown20
  0x30 uint32_t axes                      # POSE_AXIS_*, 7
  0x34 float stiffness                    # how much of a chain step it takes, 1
  0x38 int unknown38
  0x3c int unknown3C
end

# A bone of the weapon pose (src/game/human_weapon_pose.h).
struct S_PoseBone size=0x440
  0x000 int index                         # E_PoseBone, -1: none
  0x004 I3D_frame* frame                  # the joint of the model
  0x008 int unknown008
  0x00c int unknown00C
  0x010 S_vector4 childPos[3]             # the children's local positions
  0x040 S_matrix poseMatrix
  0x080 S_matrix childMatrix[3]           # the bone's axes with z towards child i
  0x140 S_matrix chainLocal               # against the chain parent
  0x180 S_matrix chainWorld
  0x1c0 S_matrix matrix1C0
  0x200 S_matrix solvedWorld              # the joint's world matrix solved
  0x240 S_matrix chainOffset              # from the chain frame's axes
  0x280 I3D_frame* chainFrame             # (a bone number + 1 in saved games)
  0x284 float chainLength
  0x288 uint32_t pinMask                  # the effectors whose chains it is in
  0x28c S_PoseLimit* chainLimit           # the joint it turns at
  0x290 S_PoseBone* chainChildren[3]
  0x29c S_PoseBone* chainParent
  0x2a0 float childLength[3]
  0x2ac S_PoseBone* child[3]
  0x2b8 S_PoseBone* parent
  0x2bc int unknown2BC
  0x2c0 S_PoseLimit limits[6]             # towards child 0..2, the parent (3..5)
end

# An effector of the pose's chains.
struct S_PosePin size=0x30
  0x00 S_PoseBone* bone
  0x04 int unknown04
  0x08 int unknown08
  0x0c int unknown0C
  0x10 S_vector4 target                   # world
  0x20 float weight
  0x24 int unknown24
  0x28 int unknown28
  0x2c int unknown2C
end

# The effectors and the bone their chains start from.
struct S_PoseChain size=0xc8
  0x00 S_PosePin pins[4]
  0xc0 S_PoseBone* root
  0xc4 int count
end

# A rigid part of the dead body's ragdoll.
struct S_RagdollBody size=0x280
  0x000 int index                         # E_RagdollBody
  0x004 S_RagdollBody* parent
  0x008 S_RagdollBody* children[3]
  0x014 int unknown014[3]
  0x020 S_matrix local                    # against the parent
  0x060 S_matrix world                    # z along the part, row 3 the start
  0x0a0 S_quat rot                        # of `local`
  0x0b0 S_matrix rest                     # the rest pose against the parent
  0x0f0 S_matrix from                     # the pose the blend to it starts at
  0x130 S_quat restRot
  0x140 S_quat fromRot
  0x150 float fromLength
  0x154 float restLength
  0x158 int unknown158[2]
  0x160 S_vector4 savedStart              # the points at the last collision step
  0x170 S_vector4 savedEnd
  0x180 float roll                        # against the parent
  0x184 float length                      # the rest length
  0x188 float savedLength
  0x18c float thickness                   # the capsule's diameter
  0x190 int startBone                     # E_PoseBone
  0x194 int endBone
  0x198 float mass                        # its share of the points' corrections
  0x19c float unknown19C
  0x1a0 float unknown1A0
  0x1a4 int boneCount
  0x1a8 int* bones                        # the bones carried
  0x1ac S_matrix* boneOffsets             # their matrices in the part's axes
  0x1b0 S_vector4 limitMin                # the direction's lowest x / y / z (unset: free)
  0x1c0 S_vector4 limitMinSin             # sqrt(1 - limit²)
  0x1d0 S_vector4 limitMax
  0x1e0 S_vector4 limitMaxSin
  0x1f0 S_RagdollPoint* start
  0x1f4 S_RagdollPoint* end
  0x1f8 int unknown1F8[2]
  0x200 S_vector4 axis                    # from start to end, normalised
  0x210 S_vector4 savedAxis
  0x220 S_vector4 unknown220              # (0, -1, 0) on the shins
  0x230 S_matrix lateral                  # turns the hips' / shoulders' line onto x
  0x270 float currentLength               # the points' distance
  0x274 int unknown274[3]
end

# A point of the ragdoll (Verlet).
struct S_RagdollPoint size=0x160
  0x000 int index                         # E_RagdollPoint
  0x004 S_RagdollBody* bodies[4]
  0x014 S_RagdollPoint* parent
  0x018 S_RagdollPoint* children[3]
  0x024 float weight                      # 1: moving, 0: settled
  0x028 int unknown028[2]
  0x030 S_vector4 pos
  0x040 S_vector4 oldPos
  0x050 S_vector4 velocity                # (pos - oldPos) / dt, bounced
  0x060 S_vector4 gravityStep             # (force + gravity) * dt
  0x070 S_vector4 step                    # the move per second
  0x080 S_vector4 force                   # for the next step
  0x090 S_vector4 localPos                # in its first part's axes (z in lengths)
  0x0a0 S_vector4 corrections[4]          # asked by bodies[i]
  0x0e0 S_vector4 smoothVelocity
  0x0f0 S_vector4 target                  # (debug)
  0x100 S_vector4 lastPos                 # at the last collision step
  0x110 S_vector4 parentLocalPos          # in the axes of its first part's parent
  0x120 S_vector4 normal                  # of the contact
  0x130 int unknown130[4]
  0x140 S_vector4 contactFramePos         # where the contact frame was
  0x150 I3D_frame* contactFrame           # a dynamic volume it lies on (referenced)
  0x154 int material                      # of the contact
  0x158 uint8_t flags                     # RAGDOLL_POINT_*
  0x159 uint8_t unknown159[7]
end

# A push / hold of a ragdoll point.
struct S_RagdollPush size=0x30
  0x00 S_RagdollPoint* point              # null: free
  0x04 int unknown04[3]
  0x10 S_vector4 force                    # the force / the target
  0x20 float time                         # s left
  0x24 float length                       # s at the start
  0x28 int unknown28[2]
end

# The weapon pose (C_HumanMovement +0xDC0; src/game/human_weapon_pose.h).
struct C_HumanWeaponPose size=0x6950
  0x0000 C_Human* owner
  0x0004 C_HumanMovement* movement        # (the owner's)
  0x0008 I3D_frame* model
  0x000c int unknown000C
  0x0010 S_PoseBone bones[23]
  0x61d0 S_PoseBone* root                 # the pelvis
  0x61d4 int boneCount
  0x61d8 int unknown61D8
  0x61dc int unknown61DC
  0x61e0 S_PoseChain chain
  0x62a8 int unknown62A8
  0x62ac int unknown62AC
  0x62b0 C_HumanStains* stains            # (detailed single player only)
  0x62b4 int time                         # ms of the last update
  0x62b8 int updateCount                  # (random start)
  0x62bc int updateSeen                   # m_updateCount of the last refresh
  0x62c0 int aimTime                      # ms of the aim's turn
  0x62c4 int aimLength
  0x62c8 bool raised                      # the weapon up
  0x62c9 bool visible                     # drawn in enough detail to pose
  0x62ca uint8_t unknown62CA[2]
  0x62cc int weaponKind                   # WEAPON_KIND_* from the behaviour
  0x62d0 int stance                       # POSE_STANCE_*
  0x62d4 int stanceBeforeSeat
  0x62d8 uint32_t hand                    # low byte: the weapon hand (1: hw1); bit 31: support hand
  0x62dc int behaviour                    # C_ItemDef behaviour of the weapon
  0x62e0 I3D_frame* weapon                # the weapon in the hands (referenced)
  0x62e4 I3D_frame* leftGrip              # "LW"
  0x62e8 I3D_frame* rightGrip             # "RW"
  0x62ec I3D_frame* stock                 # "BUTT"
  0x62f0 I3D_frame* hw1                   # "hw1"
  0x62f4 I3D_frame* hw2                   # "hw2"
  0x62f8 I3D_frame* camera3rd             # "camera3RD" (referenced)
  0x62fc I3D_frame* cameraFpv             # "cameraFPV" (referenced)
  0x6300 I3D_frame* dummyZ                # "z"
  0x6304 uint32_t flags                   # POSE_FLAG_*
  0x6308 int blendTime
  0x630c int blendLength
  0x6310 int pauseTime                    # ms (POSE_FLAG_PAUSED)
  0x6314 int supportTime                  # 0..200 ms
  0x6318 float blend                      # m_blendTime / m_blendLength
  0x631c float support                    # m_supportTime / 200
  0x6320 I3D_frame* leftHand              # bone "n"
  0x6324 I3D_frame* rightHand             # bone "j"
  0x6328 int unknown6328
  0x632c int unknown632C
  0x6330 S_vector4 leftHandTarget
  0x6340 S_vector4 rightHandTarget
  0x6350 S_vector4 weaponDir              # where the weapon points
  0x6360 S_vector4 aimDir
  0x6370 S_vector4 aimFrom                # the aim's turn from ...
  0x6380 S_vector4 aimTarget              # ... to
  0x6390 S_vector4 cameraOffset
  0x63a0 S_vector4 leftHandOffset         # in the grip's axes
  0x63b0 S_vector4 rightHandOffset
  0x63c0 float aimAngle
  0x63c4 float aimSin
  0x63c8 int unknown63C8
  0x63cc int unknown63CC
  0x63d0 S_vector4 velocity               # of the model, smoothed
  0x63e0 S_vector4 lastVelocity
  0x63f0 S_vector4 sampledVelocity
  0x6400 S_vector4 lastPos
  0x6410 int velocityTime                 # ms to the next sample (100)
  0x6414 C_Actor* vehicle                 # the seat's (dead: the vehicle it lies on)
  0x6418 uint32_t seatFlags
  0x641c int seat
  0x6420 int seatCount
  0x6424 I3D_frame* seatLeftGrip          # "LWC" / "LWS"
  0x6428 I3D_frame* seatRightGrip         # "RWC" / "RWS"
  0x642c int unknown642C
  0x6430 S_matrix wheel[3]
  0x64f0 S_matrix unknown64F0[2]
  0x6570 uint32_t lookFlags               # LOOK_*
  0x6574 int unknown6574
  0x6578 int unknown6578
  0x657c int unknown657C
  0x6580 S_vector4 lookAt                 # world point
  0x6590 S_vector4 lookDir                # in the model's axes
  0x65a0 S_vector4 lookFrom
  0x65b0 S_vector4 lookTo
  0x65c0 int lookTime
  0x65c4 int glanceTime
  0x65c8 int lookLength
  0x65cc int holdTime
  0x65d0 float lookPower                  # the turn's ease exponent
  0x65d4 float lookWeight
  0x65d8 int unknown65D8
  0x65dc int unknown65DC
  0x65e0 S_vector4 proneDir               # the owner's direction last fitted
  0x65f0 bool proneFit                    # fit the prone body to the ground
  0x65f1 uint8_t unknown65F1[3]
  0x65f4 int proneSettle                  # ms
  0x65f8 uint8_t* proneHits               # [6] 0 none, 1 near, 2 far
  0x65fc S_vector4* proneGround           # [6] where the spheres stop
  0x6600 S_vector4* proneNormals          # [6]
  0x6604 S_vector4* proneFrom             # [6] the blends' start ...
  0x6608 S_vector4* proneTo               # [6] ... end ...
  0x660c S_vector4* proneNow              # [6] ... and value (x unset: reset)
  0x6610 S_vector4* proneSpheres          # [6]
  0x6614 int* proneBlendTime              # [6] ms left
  0x6618 int proneBlendLength
  0x661c S_quat** gunnerRot               # [6][23] the gunner poses' rotations
  0x6620 S_vector4** gunnerPos            # [6][23] ... and positions
  0x6624 bool hasGunnerPose
  0x6625 uint8_t unknown6625[3]
  0x6628 float elbowOut
  0x662c float elbowIn
  0x6630 I3D_animation_set* activityAnim
  0x6634 int activityTime
  0x6638 int activityPosKey[23]
  0x6694 int activityRotKey[23]
  0x66f0 int recoilTime
  0x66f4 int accelTime                    # ms to the next sample (200)
  0x66f8 int unknown66F8
  0x66fc int unknown66FC
  0x6700 S_vector4 vehicleVelocity        # last sample
  0x6710 S_vector4 accel                  # blended
  0x6720 S_vector4 lastAccel
  0x6730 S_vector4 sampledAccel
  0x6740 bool bloodPool                   # left its blood pool
  0x6741 uint8_t unknown6741[3]
  0x6744 I3D_animation_set* deathAnim     # the dying animation (null: ragdoll)
  0x6748 short deathKind                  # E_DeathKind
  0x674a uint8_t unknown674A[2]
  0x674c int deathTime                    # ms of the animation played
  0x6750 int deathPosKey[23]
  0x67ac int deathRotKey[23]
  0x6808 int unknown6808
  0x680c int unknown680C
  0x6810 S_vector4 bodyPos                # where the scene last heard of the body
  0x6820 S_vector4 slideVelocity          # the dying body's slide
  0x6830 S_vector4 cameraPos              # the first person camera's
  0x6840 S_vector4* slidePoints           # [7] (DEATH_SLIDE_*, a held body's)
  0x6844 C_Human* carrier                 # carrying the body when dropped
  0x6848 S_RagdollBody* bodies[11]
  0x6874 S_RagdollPoint* points[16]
  0x68b4 S_RagdollBody* rootBody
  0x68b8 S_RagdollPoint* rootPoint
  0x68bc int unknown68BC
  0x68c0 S_vector4 gravity                # (0, -9, 0); slower in water
  0x68d0 float ragdollWeight              # runs down to 0 as the body settles
  0x68d4 float settleCount                # the points counted, then their average weight
  0x68d8 float settleSum                  # the points' weights
  0x68dc S_matrix* restMatrices           # [14] RAGDOLL_REST_*
  0x68e0 S_RagdollPush* pushes            # [8]
  0x68e4 S_RagdollPush* holds             # [2]
  0x68e8 int unknown68E8                  # ms to the next 100 ms step
  0x68ec float settleDelay                # s before the body may settle (1)
  0x68f0 float groundTime                 # s on the ground before it settles faster (4)
  0x68f4 float sinceCollision             # s since the last collision substep
  0x68f8 float stepTime                   # s of a substep
  0x68fc int step
  0x6900 int steps                        # substeps per step (3..6 by the detail)
  0x6904 int unknown6904
  0x6908 int unknown6908
  0x690c int contactFrames                # points with a contact frame
  0x6910 uint8_t groundedPoints           # lying on flat ground
  0x6911 bool collideStep                 # this substep collides
  0x6912 bool inWater
  0x6913 uint8_t unknown6913
  0x6914 int impactSound                  # sound slot (-1: none)
  0x6918 int dropSound                    # (-2: done)
  0x691c float savedRolls[11]             # the parts' rolls as built
  0x6948 int unknown6948[2]
end
struct S_HumanJoint size=0xc
  0x00 I3D_frame* frame
  0x04 bool unknown04
  0x08 int unknown08
end

# A slot of the human / activity animation tables (src/game/human_anim_tables.h).
struct S_HumanAnimSlot size=0xc
  0x00 uint32_t flags
  0x04 float time                   # blend time (ms; divided by 1.4 when used)
  0x08 I3D_animation_set* anim
end

# A slot of the carry animation tables.
struct S_CarryAnimSlot size=0x10
  0x00 uint32_t flags
  0x04 I3D_animation_set* anim
  0x08 float weight
  0x0c float time                   # blend time (ms; divided by 1.4 when used)
end

# An animation of the human tables (C_HumanMovement::m_animState; Normalize 0x00783310).
struct S_HumanAnimRef size=0x10
  0x00 int anim                     # E_HumanAnim (206: none)
  0x04 int stance                   # 0 stand, 1 crouch, 2 prone
  0x08 int sub
  0x0c bool valid
end

# What the body plays: animation, carry animation of the hands item, activity.
struct S_HumanAnimState size=0x24
  0x00 S_HumanAnimRef anim
  0x10 int carryAnim                # C_CarryAnimTable id (91: none)
  0x14 int carryVariant
  0x18 int unknown18
  0x1c int activity                 # C_ActivityAnimTable id (75: none)
  0x20 int activityVariant
end

# A track of the mixer (C_HumanAnimMixer::m_tracks): an animation on a pair of stages.
struct S_MixerTrack size=0x2c
  0x00 int current                  # stage of the pair playing (0 / 1; -1: none)
  0x04 int time                     # ms played
  0x08 int hold                     # > 0: held at this time
  0x0c int events[5]                # E_AnimEvent since the last update
  0x20 int eventCount
  0x24 int length                   # ms
  0x28 bool reversed
end

# A stage of the model as the mixer drives it (C_HumanAnimMixer::m_stages).
struct S_MixerStage size=0x24
  0x00 float weight
  0x04 float blendTime              # ms
  0x08 float blendElapsed           # -10000: the stage is free
  0x0c bool crossfade
  0x10 uint32_t flags               # 1: fading out
  0x14 float rate
  0x18 float speed
  0x1c bool finished
  0x20 int tag
end

# An animation for a track of the mixer (C_HumanAnimMixer::Play / UpdateStages).
struct S_MixerAnim size=0x14
  0x00 I3D_animation_set* anim
  0x04 float weight
  0x08 int mode                     # play mode | 4 / 8: start time from the old animation
  0x0c float blendTime              # ms
  0x10 bool crossfade
end

struct S_MixerCarry size=0xc
  0x00 int anim                     # C_CarryAnimTable id (0x5B: none)
  0x04 int variant
  0x08 int unknown08
end

struct S_MixerActivity size=0x8
  0x00 int activity                 # C_ActivityAnimTable id (0x4B: none)
  0x04 int variant
end

# The queued requests of a track of the mixer (a ring of 2).
struct S_MixerAnimQueue size=0x2c
  0x00 S_HumanAnimRef entries[2]
  0x20 int read
  0x24 int write
  0x28 bool locked
end

struct S_MixerCarryQueue size=0x24
  0x00 S_MixerCarry entries[2]
  0x18 int read
  0x1c int write
  0x20 bool locked
end

struct S_MixerActivityQueue size=0x1c
  0x00 S_MixerActivity entries[2]
  0x10 int read
  0x14 int write
  0x18 bool locked
end

# Animation stages of the body model (C_HumanMovement +0x260; src/game/human_anim_mixer.cpp).
struct C_HumanAnimMixer size=0x3b0
  0x000 I3D_model* model  # the owner's model
  0x004 I3D_frame* noteFrame  # "a" (note events)
  0x008 C_Human* owner
  0x00c S_MixerTrack tracks[4]
  0x0bc S_MixerStage stages[8]
  0x1dc S_HumanAnimState previous
  0x200 S_HumanAnimState current
  0x224 int track3FadeOut  # ms before the end (-1: none)
  0x228 float blendProgress  # of track 0 (1: done)
  0x230 S_vector4 velocityA
  0x240 float weightA
  0x250 S_vector4 velocityB
  0x260 float weightB
  0x264 float maxSpeed
  0x270 S_vector4 move  # of the last update (m)
  0x280 S_vector4 addMove
  0x290 int addMoveTime
  0x294 int addMoveDuration  # MOVE_NO_TIME: none
  0x298 float moveToSpeed
  0x2a0 S_vector4 moveToTarget
  0x2b0 int moveToTime
  0x2b4 int moveToDuration
  0x2c0 S_vector4 extraMove
  0x2d0 int extraMoveTime
  0x2d4 int extraMoveDuration
  0x2e0 S_vector4 addMove2
  0x2f0 int addMove2Time
  0x2f4 int addMove2Duration
  0x300 S_vector4 slide
  0x310 S_vector4 slideVelocity
  0x320 int slideStepTime
  0x324 int slideTime
  0x328 int slideDuration
  0x32c float speedScale  # of the animation speed
  0x330 int stepSoundTime  # ms since the last footstep event
  0x334 int holdTime  # SetHoldTime
  0x338 S_MixerAnimQueue animQueue
  0x364 S_MixerCarryQueue carryQueue
  0x388 S_MixerActivityQueue activityQueue
end

# Morphs of the body (Tables\morph.sav; C_HumanMovement +0x610).
struct C_HumanMorph size=0x88
  0x000 C_Human* owner
  0x004 I3D_frame* morphVisual  # 'SMMR' child of the model
  0x008 int group  # the owner's animation group
  0x00c I3D_frame* head  # frame "f" (breath sounds)
  0x010 bool dirty  # the weights need applying
  0x014 int blinkState
  0x018 int blinkPrevious
  0x01c int blinkTime
  0x020 int blinkDuration
  0x024 int blinkTimer  # ms to the next blink (-1 off)
  0x028 int breathState
  0x02c int breathPrevious
  0x030 int breathTime
  0x034 int breathDuration
  0x038 int exhaustTime  # while running (-1: not)
  0x03c int exhaustion  # ms, settles down again
  0x040 bool heavyBreath
  0x044 int expressionTime
  0x048 int expressionDuration
  0x04c int expressionRequest  # (100: none)
  0x050 int expressionSpeed
  0x054 int gripState
  0x058 int gripPrevious
  0x05c int gripTime
  0x060 int gripDuration
  0x064 int gripRequest  # (100: none)
  0x068 int grip2State
  0x06c int grip2Previous
  0x070 int grip2Time
  0x074 int grip2Duration
  0x078 int grip2Request  # (100: none)
  0x07c I3D_sound* breathSound
  0x080 int breathSoundTime  # ms left of the sound
  0x084 float breathVolume
end

# Ladders, obstacles, getting into water (C_HumanMovement +0xAA0; role inferred).
struct C_HumanSpecialMoves size=0x20
  0x00 C_HumanMovement* movement
  0x04 C_HumanLocomotion* locomotion
  0x08 C_HumanAnimPlayer* animPlayer
  0x0c C_HumanIdle* idle
  0x10 C_Human* owner
end

# What a ground move stands on (C_HumanLocomotion::MoveOnGround).
struct S_GroundContact size=0x18
  0x00 S_vector4 normal
  0x10 bool standing
  0x11 bool hasNormal
  0x14 S_CollisionVolume* hit
end

# Walking, falling, climbing, swimming, turning (C_HumanMovement +0xAC0; src/game/human_locomotion.cpp).
struct C_HumanLocomotion size=0x140
  0x000 S_vector4 unknown000  # (chunk 1; not used here)
  0x010 S_vector4 move  # of this update (from the mixer)
  0x020 S_vector4 turnTarget
  0x030 S_vector4 unknown030  # (chunk 0x0F; not used here)
  0x040 C_HumanMovement* movement
  0x044 C_Human* owner
  0x048 C_HumanSpecialMoves* specialMoves
  0x04c C_HumanAnimPlayer* animPlayer
  0x050 bool directionChanged
  0x051 bool positionChanged
  0x054 float slopeSlowdown  # 0..1 (SlowOnSlope)
  0x058 float unknown058  # (chunk 0x23; not used here)
  0x05c float fallGround
  0x060 int fallHeight
  0x064 int pushTime
  0x068 int groundTestTime  # ms to the next ground ray of SweepBody
  0x070 S_vector4 push
  0x080 int edgeTime
  0x084 float edgeHeight
  0x088 bool turning  # a turn is under way
  0x08c float turnSpeed  # rad/ms of the steady turn
  0x090 int unknown090
  0x094 int waterTestTime  # ms since the last water test
  0x098 bool inWater  # the last water test found swimming depth
  0x09c int deadSlideTime  # ms to the next slide test of a dead body
  0x0a0 S_vector4 volumesDir
  0x0b0 S_vector4 velocity  # m/s (from the animations)
  0x0c0 S_vector4 unknown0C0  # (chunk 0x20; not used here)
  0x0d0 int shiftTime
  0x0d4 int shiftStance
  0x0d8 int volumesTime  # ms to the next volumes update
  0x0dc int unknown0DC
  0x0e0 int unknown0E0
  0x0e4 bool unknown0E4
  0x0e8 int unknown0E8  # (3 while climbing)
  0x0ec int unknown0EC
  0x0f0 int unknown0F0  # (cleared each update)
  0x0f4 float fallSpeed
  0x0f8 float fallStepTime
  0x0fc float fallStartY
  0x100 int jumpTime
  0x104 bool jumpBack
  0x108 float waterDepth
  0x10c float waterHeight
  0x110 int waterTransition
  0x114 float waterSurface  # water surface at the head, none: MOVE_NO_VALUE
  0x118 float waterLevel  # C_CollisionWorld::GetWaterHeightRange
  0x11c I3D_sound* swimSound
  0x120 int swimSoundFade
  0x124 int swimSoundLeft
  0x128 int swimSoundKind
  0x12c bool swimBack  # swimming backwards
  0x130 int steadyTurnTime
end

# Plays the body animations (C_HumanMovement +0xC00).
struct C_HumanAnimPlayer size=0xb0
  0x000 C_HumanMovement* movement
  0x004 C_HumanSpecialMoves* specialMoves
  0x008 C_HumanLocomotion* locomotion
  0x00c C_Human* owner
  0x010 S_HumanAnimRef pendingAnim
  0x020 int animDelay
  0x024 int animDelayState
  0x028 uint32_t animBlocked
  0x02c int pendingCarry[3]
  0x038 int carryDelay
  0x03c int carryDelayState
  0x040 uint32_t carryBlocked
  0x044 int pendingActivity[2]
  0x04c int activityDelay
  0x050 int activityDelayState
  0x054 uint32_t activityBlocked
  0x058 int unknown58  # counts down (then activity 0x12)
  0x05c int unknown5C
  0x060 int unknown60
  0x070 S_vector4 unknown70
  0x080 I3D_frame* headDummy  # "HD"
  0x090 S_vector4 unknown90
  0x0a0 float unknownA0
end

# Fidgets, breath vapour, swimming bob and the interactions (C_HumanMovement +0xCB0).
struct C_HumanIdle size=0x20
  0x00 C_HumanAnimPlayer* animPlayer
  0x04 C_HumanMovement* movement
  0x08 C_HumanLocomotion* locomotion
  0x0c C_Human* owner
  0x10 int standTime                 # ms standing still
  0x14 int fidgetTime                # ms until the next fidget (-10000: none)
  0x18 int vapourTime                # ms until the next breath vapour
end

# Light level of the body from the lights of its sector (C_HumanVisual +0xD4).
struct C_HumanLighting size=0x1c
  0x00 float intensity              # light at the body
  0x04 float level                  # relative to the sector's reference (GetLevel: min(level, 1))
  0x08 float shownLevel             # eased toward the level (2 per second)
  0x0c int time                     # ms since the last sample
  0x10 bool sampleDue
  0x14 float ambient                # the sector's ambient scale
  0x18 float climate                # per mission (0x0084f410): Arctic1 0.32, Arctic3 0.26, Africa4 1
end

# Position, sector, lighting and shadow of the body (C_HumanMovement +0xCD0; role inferred).
struct C_HumanVisual size=0xf0
  0x000 C_HumanMovement* movement
  0x004 C_HumanLocomotion* locomotion
  0x008 C_Human* owner
  0x00c int unknown0C  # the owner's group bit 4
  0x010 bool initialized
  0x014 I3D_sector* sector
  0x018 int sectorTime  # ms since the sector was looked up
  0x01c int showHandsTime  # ms until the hands item is shown again
  0x020 int glideTime  # of m_glideDuration
  0x024 int glideDuration  # (100: one network step)
  0x030 S_vector4 previousTarget  # the network position before
  0x040 S_vector4 unknown40  # (only saved)
  0x050 S_vector4 target  # the latest network position
  0x060 bool newTarget
  0x070 S_vector4 unknown70  # (only saved; cleared)
  0x080 S_vector4 glideFrom  # the model's position at the start
  0x0a0 S_vector4 glideTo
  0x0b0 S_vector4 step  # the last network step
  0x0c0 float unknownC0  # the model's z at the reset (only saved)
  0x0c4 uint32_t flags  # VISUAL_FLAG_*
  0x0c8 bool glideDone  # stopped at the target
  0x0cc int moveSoundTime  # ms since the last movement sound
  0x0d0 bool networkDriven
  0x0d4 C_HumanLighting lighting
end

# What the work asks the movement to do (the first 0xA0 bytes of C_HumanWork; the movement's
# copy at +0x770). Reset 0x007801a0, saved as chunk 0x308.
struct S_MoveRequest size=0xa0
  0x00 C_Actor* killer              # C_HumanWork::Die
  0x04 I3D_frame* workingFrame      # SetWorkingFrame
  0x08 C_Actor* interactTarget       # Interact
  0x0c int unknown0C
  0x10 S_vector4 unknown10          # SetLookAt
  0x20 S_vector4 facingDir          # SetFacingDir (remote humans move along it)
  0x40 uint32_t changed             # WORK_REQUEST_* since the last update
  0x44 int anim
  0x48 int activity
  0x4c int stance
  0x50 int gait                     # sub-variant of the moving animations (3: limping)
  0x54 int carryMode
  0x58 int action
  0x5c int unknown5C
  0x60 uint32_t hitFlags
  0x70 S_vector hitForce
  0x80 S_vector hitPos
  0x90 uint32_t interaction          # E_Interaction
  0x94 int interactArg
end

# What a human's body is asked to do (S_HumanBody::work; 0x0077e1e0..0x00780c70).
struct C_HumanWork size=0xe0
  0x00 S_MoveRequest request
  0xa0 int group
  0xa4 C_HumanMovement* movement        # for its parts
  0xa8 C_HumanMovement* movementBase    # for its members (the same object)
  0xac C_Human** owner                  # the movement's owner member
  0xb0 bool forced                      # the requests pass even when blocked
  0xb4 int playCount
  0xb8 int mode                         # 1 ladder / window, 2 / 3 lying, 4, 5 swimming
  0xbc int stanceTimer                  # -10000: none
  0xc0 uint32_t blockedTimer
  0xc4 int timerStance
  0xc8 uint32_t refused                 # blocked requests since the last update
  0xcc uint32_t bodyFlags               # TakeFlags
  0xd0 uint32_t blocked
  0xd4 uint32_t blocked2
  0xd8 uint32_t flagsD8
end

# An action of a remote human waiting to be replayed (C_HumanMovement::m_remoteActions).
struct S_RemoteAction size=0xc
  0x00 int time
  0x04 int action
  0x08 int phase
end

# A body of spheres moved together (C_CollisionWorld::MoveSpheres*; src/game/collision.h).
struct S_MoveContacts size=0x1f4
  0x00 uint32_t sphereCount
  0x04 float radii[11]
  0x30 S_vector4 spheres[10]
  0xd0 S_vector4 move
  0xe0 uint32_t excludeFlags
  0xe4 uint32_t excludeTypes
  0xf0 S_vector4 pos                 # where the first sphere ends
  0x100 S_vector4 normals[10]        # of the contacts
  0x1a0 int sphere[10]               # the sphere that touched
  0x1c8 S_CollisionVolume* volumes[10]
  0x1f0 int count
end

# A footprint (pooled; src/game/surface_marks.cpp).
struct S_SurfaceMark size=0x14
  0x00 uint32_t time
  0x04 int humanId
  0x08 I3D_frame* visual            # 'PROJ' projector
  0x0c int kind                     # index of g_footprintKinds
  0x10 bool left
end

# Cached sound frames of a file of the movement sound bank.
struct S_MovementSoundCache size=0xc
  0x00 I3D_sound* sounds[2]
  0x08 int count
end

# The movement sound bank (g_movementSounds 0x0098a690).
struct C_MovementSounds size=0x50
  0x00 S_MovementSoundCache* caches[20]
end

struct C_HumanMovement size=0xdd0
  0x000 I3D_frame* shotDummies[2]  # "shot1", "shot2"
  0x008 I3D_frame* stanceDummies[9]  # E_StanceDummy
  0x030 S_vector4 dummyPos[9]  # world positions
  0x0c0 float dummyRadius[9]  # half the width of their boxes
  0x0f0 S_vector4 dummyOffset[9]  # from the model's position
  0x180 int unknown180
  0x184 int bloodZone  # where the wound bleeds (5: none; 0: larger pools)
  0x188 I3D_frame* bloodFrame  # the frame of the wound
  0x18c bool unknown18C
  0x190 int unknown190
  0x194 std::vector<int> sounds  # playing movement sounds (local player)
  0x1a4 int breathTime  # ms under water
  0x1a8 int gaspTime  # ms to the next gasp
  0x1ac int unknown1AC
  0x1b0 int unknown1B0
  0x1b4 bool carried  # being carried
  0x1b8 C_HumanWeaponPose* weaponPose2  # = m_weaponPose
  0x1bc int unknown1BC
  0x1c0 int animProfileTime  # us spent in UpdateAnimation
  0x1c4 int physicsProfileTime  # us spent in UpdatePhysics
  0x1c8 float stanceVolumeScale  # radius scale of the stance spheres
  0x1cc float hitVolumeScale[2]  # radius scale of the hit spheres
  0x1d4 bool inUpdate
  0x1d8 S_HumanAnimState animState
  0x1fc S_HumanAnimState nextAnimState
  0x220 int unknown220
  0x224 bool unknown224
  0x228 int stillTime
  0x22c uint32_t flags  # MOVE_FLAG_*
  0x230 int footstepSurface
  0x234 float swimBobPhase
  0x240 S_vector4 unknown240
  0x250 float unknown250
  0x254 float unknown254
  0x258 C_Human* owner
  0x260 C_HumanAnimMixer mixer
  0x610 C_HumanMorph morph
  0x698 int stanceVolumeCount  # stance spheres in the collision world
  0x69c int hitVolumeCount  # hit spheres in the collision world
  0x6a0 S_vector4 hitSpheres[2]  # center, radius
  0x6c0 S_vector4 hitOffsets[2]  # from the model's position
  0x6e0 S_vector4 stanceSpheres[4]  # center, radius
  0x720 S_CollisionVolume* stanceVolumes[4]
  0x730 S_CollisionVolume* hitVolumes[2]
  0x740 S_vector4 unknown740  # offset of the third stand sphere at the start
  0x750 int groundMaterial  # -1: test under the foot
  0x754 S_CollisionVolume* ground  # under the last footstep
  0x758 float slideFactor
  0x75c int damageMaterial
  0x760 bool unknown760
  0x761 bool onDynamicGround
  0x762 bool onGround
  0x764 int unknown764
  0x770 S_MoveRequest request
  0x810 S_HumanJoint joints[25]
  0x940 S_vector4 unknown940
  0x950 int unknown950  # counts down
  0x954 int unknown954
  0x958 float unknown958
  0x95c float unknown95C
  0x960 int unknown960
  0x964 int unknown964
  0x968 float unknown968
  0x96c bool unknown96C
  0x970 S_CollisionVolume* trailVolumes[4]
  0x980 int trailTime
  0x984 int trailCount
  0x988 int useState
  0x98c bool unknown98C
  0x990 C_Actor* useObject  # the ladder being used
  0x994 int useSound  # sound module handle
  0x998 int unknown998
  0x99c int stanceChange
  0x9a0 int unknown9A0
  0x9a4 int nextStance  # (3: none)
  0x9a8 int nextStance2
  0x9ac int unknown9AC
  0x9b0 int unknown9B0
  0x9b4 int unknown9B4
  0x9b8 int unknown9B8
  0x9bc int unknown9BC
  0x9c0 int unknown9C0
  0x9c4 int unknown9C4
  0x9c8 bool unknown9C8
  0x9c9 bool unknown9C9
  0x9cc int unknown9CC
  0x9d0 S_vector4 unknown9D0
  0x9e0 int unknown9E0
  0x9f0 S_vector4 unknown9F0
  0xa00 S_vector4 unknownA00
  0xa10 int unknownA10
  0xa14 int unknownA14
  0xa18 int unknownA18
  0xa1c int unknownA1C
  0xa20 int handsCarryState
  0xa24 int unknownA24
  0xa28 int unknownA28  # counts down
  0xa2c int unknownA2C
  0xa30 int unknownA30  # 1: carrying a body
  0xa34 uint32_t interaction  # E_Interaction under way (sit / stand up: seat is a frame)
  0xa38 int interactionTime
  0xa3c C_Actor* vehicle
  0xa40 int seat  # the vehicle's seat
  0xa44 int seatMode  # seat flags, then 1..8 (C_HumanIdle::UpdateInteraction)
  0xa48 int seatStance
  0xa4c S_HumanEventDeque remoteActions
  0xa7c uint32_t clock  # ms, wraps at 1000 short of 2^32
  0xa80 uint32_t timer200
  0xa84 uint32_t timer300
  0xa88 uint32_t timer400
  0xa8c uint32_t timer600
  0xa90 bool parachuting
  0xa94 int footstepTime  # ms since the last noisy footstep (<= 1000)
  0xa98 int footstepCount  # quiet steps in a row
  0xa9c I3D_frame* shadow  # GEffects shadow of the model
  0xaa0 C_HumanSpecialMoves specialMoves
  0xac0 C_HumanLocomotion locomotion
  0xc00 C_HumanAnimPlayer animPlayer
  0xcb0 C_HumanIdle idle
  0xcd0 C_HumanVisual visual
  0xdc0 C_HumanWeaponPose* weaponPose  # (allocated)
  0xdc4 bool detached
  0xdc8 int unknownDC8  # counts down per frame
end
