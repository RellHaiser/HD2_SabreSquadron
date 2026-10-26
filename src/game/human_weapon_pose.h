// The pose of a human's skeleton beyond its animations (C_HumanMovement +0xDC0; original source
// 0x0079bd70..0x007ce4d0, see human_weapon_pose.cpp and human_pose_*.cpp): the 23 bones of the
// model's joints with their limits, the arms holding the weapon in the hands ("LW" / "RW" grips,
// the stock "BUTT") and in vehicles (the steering wheel "LWC" / "RWC", seat weapons "LWS" /
// "RWS", the gunner poses of static weapons), the head looking at things and glancing round,
// the prone body fitted to the ground, the arms' activity animations and the weapon's recoil,
// the reactions to hits, and the dead body: the dying animation sliding along the ground, or a
// ragdoll of eleven rigid parts between sixteen points.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_Human;
class C_HumanMovement;
class C_HumanStains;
class I3D_animation_set;
class I3D_frame;
struct S_CollisionVolume;

// The bones (POSE_BONE_COUNT; the joint names of the model in g_poseBoneNames).
enum E_PoseBone {
    POSE_BONE_LEFT_FOOT = 0,                            // "u"
    POSE_BONE_RIGHT_FOOT = 1,                           // "q"
    POSE_BONE_LEFT_KNEE = 2,                            // "t"
    POSE_BONE_RIGHT_KNEE = 3,                           // "p"
    POSE_BONE_NECK = 4,                                 // "e"
    POSE_BONE_HEAD_TOP = 5,                             // "f1"
    POSE_BONE_HEAD = 6,                                 // "f"
    POSE_BONE_UPPER_CHEST = 7,                          // "d1"
    POSE_BONE_CHEST = 8,                                // "d"
    POSE_BONE_SPINE = 9,                                // "b"
    POSE_BONE_LEFT_HIP = 10,                            // "s"
    POSE_BONE_RIGHT_HIP = 11,                           // "o"
    POSE_BONE_LOWER_BACK = 12,                          // "c1"
    POSE_BONE_BACK = 13,                                // "c"
    POSE_BONE_RIGHT_COLLAR = 14,                        // "g"
    POSE_BONE_LEFT_COLLAR = 15,                         // "k"
    POSE_BONE_RIGHT_SHOULDER = 16,                      // "h"
    POSE_BONE_LEFT_SHOULDER = 17,                       // "l"
    POSE_BONE_RIGHT_ELBOW = 18,                         // "i"
    POSE_BONE_LEFT_ELBOW = 19,                          // "m"
    POSE_BONE_RIGHT_HAND = 20,                          // "j"
    POSE_BONE_LEFT_HAND = 21,                           // "n"
    POSE_BONE_PELVIS = 22,                              // "a", the root
    POSE_BONE_COUNT = 23,
};

// The joint names of the human models, by E_PoseBone.
extern const char g_poseBoneNames[POSE_BONE_COUNT][12];   // 0x00816d18

// S_PoseLimit::axes: the axes (of the joint's chain frame) a joint turns about.
enum : uint32_t {
    POSE_AXIS_X = 0x1,
    POSE_AXIS_Y = 0x2,
    POSE_AXIS_Z = 0x4,
};

// The joint between a bone and one of its neighbours.
struct S_PoseLimit {                                    // 0x40 bytes
    S_vector4 max;                                      // +0x00 (pi; not used by the chains)
    S_vector4 min;                                      // +0x10 (-pi)
    S_vector4 unknown20;                                // +0x20
    uint32_t axes;                                      // +0x30 POSE_AXIS_*, 7
    float stiffness;                                    // +0x34 how much of a chain step it takes, 1
    int unknown38;
    int unknown3C;
};

struct S_PoseBone {                                     // 0x440 bytes
    int index;                                          // +0x000 E_PoseBone, -1: none
    I3D_frame *frame;                                   // +0x004 the joint of the model
    int unknown008;
    int unknown00C;
    S_vector4 childPos[3];                              // +0x010 the children's local positions
    // +0x040 a matrix for the bone: the ragdoll's world matrix of it; its local matrix while a
    // saved game of a dying body is loaded.
    S_matrix poseMatrix;
    S_matrix childMatrix[3];                            // +0x080 the bone's axes with z towards child i
    // Effector chains (C_HumanWeaponPose::m_chain): the bone turns about its chain frame's
    // joint at the chain length from it. (The death keeps the frame's local / world matrix in
    // chainLocal / chainWorld.)
    S_matrix chainLocal;                                // +0x140 against the chain parent
    S_matrix chainWorld;                                // +0x180
    S_matrix matrix1C0;                                 // +0x1C0
    S_matrix solvedWorld;                               // +0x200 the joint's world matrix solved
    S_matrix chainOffset;                               // +0x240 from the chain frame's axes
    I3D_frame *chainFrame;                              // +0x280 (a bone number + 1 in saved games)
    float chainLength;                                  // +0x284
    uint32_t pinMask;                                   // +0x288 the effectors whose chains it is in
    S_PoseLimit *chainLimit;                            // +0x28C the joint it turns at
    S_PoseBone *chainChildren[3];                       // +0x290
    S_PoseBone *chainParent;                            // +0x29C
    float childLength[3];                               // +0x2A0
    S_PoseBone *child[3];                               // +0x2AC
    S_PoseBone *parent;                                 // +0x2B8
    int unknown2BC;
    S_PoseLimit limits[6];                              // +0x2C0 towards child 0..2, the parent (3..5)
};

// An effector: the tip of `bone` (its chain position + chainLength along its z) should reach
// `target`.
struct S_PosePin {                                      // 0x30 bytes
    S_PoseBone *bone;                                   // +0x00
    int unknown04;
    int unknown08;
    int unknown0C;
    S_vector4 target;                                   // +0x10 world
    float weight;                                       // +0x20
    int unknown24;
    int unknown28;
    int unknown2C;
};

// The effectors and the bone their chains start from.
struct S_PoseChain {                                    // 0xC8 bytes
    S_PosePin pins[4];                                  // +0x00
    S_PoseBone *root;                                   // +0xC0
    int count;                                          // +0xC4
};

// ---- the ragdoll ----------------------------------------------------------------------------

struct S_RagdollPoint;

// The rigid parts of the ragdoll (C_HumanWeaponPose::m_bodies; S_RagdollBody::index).
enum E_RagdollBody {
    RAGDOLL_PELVIS = 0,                                 // pelvis to upper chest
    RAGDOLL_CHEST = 1,                                  // chest to neck
    RAGDOLL_HEAD = 2,                                   // neck to head (three times as long)
    RAGDOLL_LEFT_UPPER_ARM = 3,
    RAGDOLL_LEFT_FOREARM = 4,
    RAGDOLL_RIGHT_UPPER_ARM = 5,
    RAGDOLL_RIGHT_FOREARM = 6,
    RAGDOLL_LEFT_THIGH = 7,
    RAGDOLL_LEFT_SHIN = 8,
    RAGDOLL_RIGHT_THIGH = 9,
    RAGDOLL_RIGHT_SHIN = 10,
    RAGDOLL_BODY_COUNT = 11,
};

// The points the parts meet at (C_HumanWeaponPose::m_points; S_RagdollPoint::index).
enum E_RagdollPoint {
    RAGDOLL_POINT_PELVIS = 0,                           // the root
    RAGDOLL_POINT_CHEST = 1,
    RAGDOLL_POINT_NECK = 2,
    RAGDOLL_POINT_HEAD = 3,
    RAGDOLL_POINT_LEFT_SHOULDER = 4,
    RAGDOLL_POINT_LEFT_ELBOW = 5,
    RAGDOLL_POINT_LEFT_HAND = 6,
    RAGDOLL_POINT_RIGHT_SHOULDER = 7,
    RAGDOLL_POINT_RIGHT_ELBOW = 8,
    RAGDOLL_POINT_RIGHT_HAND = 9,
    RAGDOLL_POINT_LEFT_HIP = 10,
    RAGDOLL_POINT_LEFT_KNEE = 11,
    RAGDOLL_POINT_LEFT_FOOT = 12,
    RAGDOLL_POINT_RIGHT_HIP = 13,
    RAGDOLL_POINT_RIGHT_KNEE = 14,
    RAGDOLL_POINT_RIGHT_FOOT = 15,
    RAGDOLL_POINT_COUNT = 16,
};

// S_RagdollPoint::flags
enum : uint8_t {
    RAGDOLL_POINT_CONTACT = 0x01,                       // lies on the world (normal, material)
    RAGDOLL_POINT_TOUCHING = 0x02,                      // touches another part (normal)
    // The joint limits its part hit at it (the part's direction in its parent's axes).
    RAGDOLL_POINT_MIN_X = 0x04,
    RAGDOLL_POINT_MAX_X = 0x08,
    RAGDOLL_POINT_MIN_Y = 0x10,
    RAGDOLL_POINT_MAX_Y = 0x20,
    RAGDOLL_POINT_MIN_Z = 0x40,
    RAGDOLL_POINT_MAX_Z = 0x80,
    RAGDOLL_POINT_LIMITS = 0xFC,
};

constexpr int RAGDOLL_PUSH_COUNT = 8;
constexpr int RAGDOLL_HOLD_COUNT = 2;

// The slots of C_HumanWeaponPose::m_restMatrices (the bones of g_ragdollRestBones).
enum {
    RAGDOLL_REST_SPINE = 0,
    RAGDOLL_REST_LOWER_BACK = 1,
    RAGDOLL_REST_BACK = 2,
    RAGDOLL_REST_UPPER_CHEST = 3,
    RAGDOLL_REST_CHEST = 4,
    RAGDOLL_REST_LEFT_COLLAR = 5,
    RAGDOLL_REST_RIGHT_COLLAR = 6,
    RAGDOLL_REST_NECK = 7,
    RAGDOLL_REST_HEAD_TOP = 8,
    RAGDOLL_REST_HEAD = 9,
    RAGDOLL_REST_LEFT_FOOT = 10,
    RAGDOLL_REST_RIGHT_FOOT = 11,
    RAGDOLL_REST_LEFT_HAND = 12,
    RAGDOLL_REST_RIGHT_HAND = 13,
};

// A rigid part of the ragdoll: a capsule from its start point to its end point that carries
// some of the model's bones. Its joint limits bound its direction in its parent's axes.
struct S_RagdollBody {                                  // 0x280 bytes, 16-aligned
    int index;                                          // +0x000 E_RagdollBody
    S_RagdollBody *parent;                              // +0x004
    S_RagdollBody *children[3];                         // +0x008
    int unknown014[3];
    S_matrix local;                                     // +0x020 against the parent
    S_matrix world;                                     // +0x060 z along the part, row 3 the start
    S_quat rot;                                         // +0x0A0 of `local`
    S_matrix rest;                                      // +0x0B0 the rest pose against the parent
    S_matrix from;                                      // +0x0F0 the pose the blend to it starts at
    S_quat restRot;                                     // +0x130
    S_quat fromRot;                                     // +0x140
    float fromLength;                                   // +0x150
    float restLength;                                   // +0x154
    int unknown158[2];
    S_vector4 savedStart;                               // +0x160 the points at the last collision step
    S_vector4 savedEnd;                                 // +0x170
    float roll;                                         // +0x180 against the parent
    float length;                                       // +0x184 the rest length
    float savedLength;                                  // +0x188
    float thickness;                                    // +0x18C the capsule's diameter
    int startBone;                                      // +0x190 E_PoseBone
    int endBone;                                        // +0x194
    float mass;                                         // +0x198 its share of the points' corrections
    float unknown19C;
    float unknown1A0;
    int boneCount;                                      // +0x1A4
    int *bones;                                         // +0x1A8 the bones carried
    S_matrix *boneOffsets;                              // +0x1AC their matrices in the part's axes
    S_vector4 limitMin;                                 // +0x1B0 the direction's lowest x / y / z (unset: free)
    S_vector4 limitMinSin;                              // +0x1C0 sqrt(1 - limit²)
    S_vector4 limitMax;                                 // +0x1D0
    S_vector4 limitMaxSin;                              // +0x1E0
    S_RagdollPoint *start;                              // +0x1F0
    S_RagdollPoint *end;                                // +0x1F4
    int unknown1F8[2];
    S_vector4 axis;                                     // +0x200 from start to end, normalised
    S_vector4 savedAxis;                                // +0x210
    S_vector4 unknown220;                               // +0x220 (0, -1, 0) on the shins
    S_matrix lateral;                                   // +0x230 turns the hips' / shoulders' line onto x
    float currentLength;                                // +0x270 the points' distance
    int unknown274[3];
};

// A point of the ragdoll moved by Verlet integration where up to four parts meet.
struct S_RagdollPoint {                                 // 0x160 bytes, 16-aligned
    int index;                                          // +0x000 E_RagdollPoint
    S_RagdollBody *bodies[4];                           // +0x004
    S_RagdollPoint *parent;                             // +0x014
    S_RagdollPoint *children[3];                        // +0x018
    float weight;                                       // +0x024 1: moving, 0: settled
    int unknown028[2];
    S_vector4 pos;                                      // +0x030
    S_vector4 oldPos;                                   // +0x040
    S_vector4 velocity;                                 // +0x050 (pos - oldPos) / dt, bounced
    S_vector4 gravityStep;                              // +0x060 (force + gravity) * dt
    S_vector4 step;                                     // +0x070 the move per second
    S_vector4 force;                                    // +0x080 for the next step
    S_vector4 localPos;                                 // +0x090 in its first part's axes (z in lengths)
    S_vector4 corrections[4];                           // +0x0A0 asked by bodies[i]
    S_vector4 smoothVelocity;                           // +0x0E0
    S_vector4 target;                                   // +0x0F0 (debug)
    S_vector4 lastPos;                                  // +0x100 at the last collision step
    S_vector4 parentLocalPos;                           // +0x110 in the axes of its first part's parent
    S_vector4 normal;                                   // +0x120 of the contact
    int unknown130[4];
    S_vector4 contactFramePos;                          // +0x140 where the contact frame was
    I3D_frame *contactFrame;                            // +0x150 a dynamic volume it lies on (referenced)
    int material;                                       // +0x154 of the contact
    uint8_t flags;                                      // +0x158 RAGDOLL_POINT_*
    uint8_t unknown159[7];
};

// A push of a ragdoll point (m_pushes) or a pull towards a target (m_holds).
struct S_RagdollPush {                                  // 0x30 bytes
    S_RagdollPoint *point;                              // +0x00 null: free
    int unknown04[3];
    S_vector4 force;                                    // +0x10 the force / the target
    float time;                                         // +0x20 s left
    float length;                                       // +0x24 s at the start
    int unknown28[2];
};

// ---- the pose's states ------------------------------------------------------------------------

// C_HumanWeaponPose::m_flags: what the pose does (one of the modes) and its timers.
enum : uint32_t {
    POSE_FLAG_HOLD = 0x1,                               // the weapon held up (SetMode 1)
    POSE_FLAG_GRIP = 0x2,                               // both hands on the weapon's grips
    POSE_FLAG_FREE = 0x4,                               // the hands free
    POSE_FLAG_STEER = 0x8,                              // the hands on a car's steering wheel
    POSE_FLAG_STATIC_WEAPON = 0x10,                     // a gunner at a static weapon
    POSE_FLAG_ON_VEHICLE = 0x10,                        // (dead) the body lies on m_vehicle
    POSE_FLAG_SEAT_WEAPON = 0x20,                       // the hands on a vehicle seat's weapon
    POSE_FLAG_PASSENGER = 0x40,                         // a seat: the neck sways with the vehicle
    POSE_FLAG_ACTIVITY = 0x80,                          // the arms play an activity animation
    POSE_FLAG_RECOIL = 0x100,                           // the weapon kicks
    POSE_FLAG_DEAD = 0x200,                             // the dead body (animation or ragdoll)
    POSE_FLAG_SUPPORT_ON = 0x4000000,                   // the support hand goes onto the grip
    POSE_FLAG_SUPPORT_OFF = 0x8000000,                  // ... comes off it
    POSE_FLAG_PAUSED = 0x10000000,                      // m_pauseTime runs out into raising the weapon
    POSE_FLAG_NO_RAISE = 0x20000000,                    // ... or not
    POSE_FLAG_RAISE = 0x40000000,                       // m_blendTime counts up to m_blendLength
    POSE_FLAG_LOWER = 0x80000000,                       // ... down to 0
};

// C_HumanWeaponPose::m_lookFlags (SetLook).
enum : uint32_t {
    LOOK_AT = 0x1,                                      // at m_lookAt
    LOOK_GLANCE = 0x2,                                  // glances round
    LOOK_QUICK = 0x8,
    LOOK_NERVOUS = 0x10,
    LOOK_GLANCE_LEFT = 0x20,                            // towards -x
    LOOK_GLANCE_RIGHT = 0x40,                           // towards +x
    LOOK_ONCE = 0x80,
    LOOK_LAST_LEFT = 0x20000000,
    LOOK_LAST_RIGHT = 0x40000000,
    LOOK_RETURNING = 0x80000000,
};

// C_HumanWeaponPose::m_stance (the work's stances; seated in vehicles).
enum {
    POSE_STANCE_STAND = 0,
    POSE_STANCE_CROUCH = 1,
    POSE_STANCE_PRONE = 2,
    POSE_STANCE_SEATED = 3,
};

// C_HumanWeaponPose::m_weaponKind (from the C_ItemDef behaviour, SetModel).
enum {
    WEAPON_KIND_NONE = 0,
    WEAPON_KIND_PISTOL = 4,
};

// How a body dies (StartDeath, C_HumanSpecialMoves::Die).
enum E_DeathKind {
    DEATH_FALL = 0,                                     // falls: the animation sliding, a ragdoll when drawn in detail
    DEATH_KIND_1 = 1,                                   // (the animation; not passed by the game)
    DEATH_SEATED = 2,                                   // in a seat: the animation without physics
    DEATH_HELD = 3,                                     // held (backstab, tied up): the animation until it hits something
    DEATH_RAGDOLL = 4,                                  // straight into the ragdoll (explosions, dropped bodies)
};

// m_slidePoints: a held body's head, neck, chest, back, spine and knees; a falling one's neck,
// pelvis and knees on the ground.
constexpr int DEATH_HELD_POINT_COUNT = 7;
enum {
    DEATH_SLIDE_NECK = 0,
    DEATH_SLIDE_PELVIS = 1,
    DEATH_SLIDE_KNEES = 2,
};

class C_HumanWeaponPose {
public:
    C_HumanWeaponPose();                                // 0x0079bd70
    ~C_HumanWeaponPose();                               // 0x0079bf20
    C_HumanWeaponPose(const C_HumanWeaponPose &) = delete;
    C_HumanWeaponPose &operator=(const C_HumanWeaponPose &) = delete;

    // The pose of `model` (the owner's; `reload`: after a saved game).
    void Attach(I3D_frame *model, C_Human *owner, bool reload);   // 0x0079c190
    void Reset();                                       // 0x0079d530
    bool SaveGame(C_chunk &chunk);                      // 0x0079d540
    bool LoadGame(C_chunk &chunk);                      // 0x0079f3f0
    void Update(int time);                              // 0x007a78d0
    // The pose again once per update of the owner (no time passing).
    void Refresh(int unused);                           // 0x007a8760
    // Binds the bones to the model's joints: `mode` 1 after a saved game (only finds them
    // again), otherwise builds the skeleton (2: the model's joints were swapped).
    void BindSkeleton(int mode);                        // 0x0079c780

    // The weapon.
    // Model of the weapon in the hands (null: none) and its C_ItemDef behaviour (-1 unknown).
    void SetModel(I3D_frame *model, int behaviour);     // 0x007b9510
    // 1: held up (in hw1), 0: down (in hw2), 2: the hand it is in.
    void SetMode(int mode);                             // 0x007b96c0
    void RaiseWeapon(int time);                         // 0x007b9160
    void LowerWeapon(int time);                         // 0x007b91c0
    // Lowers the weapon for `time` ms, then raises it again (`free`: the hands free meanwhile).
    void PauseWeapon(int time, bool free);              // 0x007b9270
    void PauseHands(int time);                          // 0x007b9220
    // The second hand on the weapon's grip (needs the "LW" dummy).
    void SetSupportHand(bool on);                       // 0x007b9b20
    bool IsSupportHandOn() const { return (m_hand & 0x80000000u) != 0; }
    // The view direction the weapon aims along.
    void SetViewDir(const S_vector &dir);               // 0x007b92e0
    // An activity of the arms (null: none).
    void PlayActivity(I3D_animation_set *anim);         // 0x007ae780
    void StartRecoil(I3D_animation_set *unused);        // 0x007ae7f0
    // The raise / lower blends take `time` ms. (Inline in the animation player.)
    void SetBlendLength(int time);
    I3D_frame *GetWeapon() const { return m_weapon; }
    // Low byte: the weapon's hand (1: hw1); bit 31: the support hand.
    uint32_t GetHand() const { return m_hand; }
    uint32_t GetFlags() const { return m_flags; }

    // The body.
    // (Inline in C_HumanIdle::UpdateInteraction.)
    void SetSeatStance(int stance);
    // (Inline in C_HumanLocomotion::ChangeStance.)
    void SetStanceChange(int stance);
    // (Inline in C_HumanLocomotion::Update.)
    void StopProneFit() { m_proneFit = false; }
    // The seat the body is in (seat flags; null `vehicle`: out of it, the stance before it back).
    void SetSeat(uint32_t seatFlags, C_Actor *vehicle, int seat);   // 0x007ae190
    // The gunner's pose at a seat weapon from the keys of `anim` (null: none).
    void SetSeatWeaponPose(I3D_animation_set *anim);   // 0x007b9070
    // Fits the prone body to the ground.
    void FitProneBody();                                // 0x007a8980
    // Where the head looks (LOOK_* flags; `pos` the point for LOOK_AT).
    void SetLook(uint32_t flags, const S_vector4 *pos);   // 0x007acbe0

    // Hits and death.
    // A hit of the body (the zone bits of C_HumanWork::ApplyHit) at `pos` with `force`.
    void OnHit(uint32_t flags, const S_vector &pos, const S_vector &force);   // 0x007b88b0
    void SetStainsVisible(bool on);                     // 0x007b8a00
    // The death (clients): the killing hit's flags and force, the dying animation (null:
    // none), E_DeathKind, the human carrying the body when it is dropped (null: none).
    void StartDeath(uint32_t hitFlags, const float *force, I3D_animation_set *deathAnim, int kind, C_Human *carrier);   // 0x007b0d70
    // The body is picked up.
    void PickUpBody();                                  // 0x007b3510
    // The ragdoll's weight: below 1e-8 it has settled.
    float GetRagdollWeight() const { return m_ragdollWeight; }

private:
    // The skeleton and its chains (human_pose_ik.cpp).
    void AddBone(int index, int parent, I3D_frame *frame);   // 0x007a4700
    void SetLimitAxes(int index, int other, uint32_t axes);   // 0x007a51b0
    void SetLimitStiffness(int index, int other, float stiffness);   // 0x007a5250
    void AimBone(int index, const S_vector &dir, float weight);   // 0x007a4820
    void SetHandRotations(I3D_frame *left, I3D_frame *right);   // 0x007a52e0
    void SetHandMatrices(const S_matrix &left, const S_matrix &right);   // 0x007a5550
    void BendJoint(int index, const S_vector &dir, float weight);   // 0x007a5790
    void ClearChains();                                 // 0x007a5f70
    // Builds m_chain (BuildChain, 0x007a5e90).
    void BuildChains();
    void RefreshChain(S_PoseChain &chain);              // 0x007a69f0
    int SolveChain(S_PoseChain &chain, int iterations, bool moveRoot);   // 0x007a6c40
    void ApplyChain(S_PoseChain &chain);                // 0x007a6420

    // The core (human_weapon_pose.cpp).
    bool KeepsWeapon();
    void UpdateDead(int time);
    void KeepCamerasOutOfWalls();

    // The head (human_pose_body.cpp).
    void PickGlance();                                  // 0x007ace90
    void UpdateHeadLook(int time);                      // 0x007ad120
    void TurnHead(const S_vector &dir, float blend);    // 0x007ade70

    // Seats (human_pose_seats.cpp).
    void UpdateVehicleAccel();
    void UpdateSteering();                              // 0x007ae820
    void UpdateSeat();                                  // 0x007af040
    void UpdateStaticWeapon();                          // 0x007af780

    // Holding the weapon (human_pose_hold.cpp).
    void UpdateRightHandTarget();
    void UpdateLeftHandTarget();
    S_vector HandsDir() const;
    S_vector HandAxis(const S_vector &dir) const;
    void ClampAim(float minForward, float side);
    void HoldPistol(bool moving);
    void HoldRifle(const S_matrix *axes, const S_vector &lower, const S_vector &upper, bool settle);
    void HoldOnGrips(S_vector dir);
    S_matrix SpineAxes() const;
    void HoldStanding();                                // 0x007bd420
    void HoldCrouching();                               // 0x007bb9a0
    void HoldSeated();                                  // 0x007beb50
    void HoldProne();                                   // 0x007bac90

    // The arms and the weapon (human_pose_weapon.cpp).
    bool SolveTwoBone(const S_vector &root, float upper, const S_vector &target, float lower, S_vector4 &joint,
                      const S_vector *hint);            // 0x007a3b70
    void TurnBone(int index, const S_vector &from, const S_vector4 &to, const S_vector &weights);   // 0x007bfde0
    void TurnBoneAbout(int index, const S_vector &from, const S_vector4 &to, const S_matrix &axes,
                       const S_vector &weights);        // 0x007c02e0
    void AimWeapon(const S_vector4 &dir, bool hands, bool place, float weight);   // 0x007c0750
    void ReachArm(int shoulder, int elbow, const S_vector &target, const S_vector4 &handTarget, I3D_frame *hand,
                  float weight, const S_vector *hint);  // 0x007c0e20
    void InitHands(bool reload);                        // 0x007b8a20
    void TurnAim(float f);
    void UpdateGrip();                                  // 0x007b9bb0
    void UpdateHold();                                  // 0x007ba110
    void UpdateFree();                                  // 0x007ba520
    void UpdateRecoil();                                // 0x007b01d0
    void UpdateActivity();                              // 0x007b0080
    void ApplyAnimation(I3D_animation_set *anim, int *posKeys, int *rotKeys, int time, S_PoseBone *bone, float weight,
                        float (*boneWeight)(const S_PoseBone *));   // 0x007b0710

    // The dead body (human_pose_dead.cpp).
    void StartDeathAnim();
    void StoreHeldPoints();
    void StartRagdoll(uint32_t hitFlags, const float *force, C_Human *carrier);
    void ApplyHitImpulse(uint32_t hitFlags, const S_vector4 &impulse, int time);   // 0x007b36d0
    void AddSettledBloodPool();                         // 0x007b3c20
    void UpdateDeathAnim();                             // 0x007b40e0
    bool UpdateHeldDeath();
    const S_CollisionVolume *SlideDeadBody();
    void SampleDeathAnim(S_PoseBone *bone, int time, float blend);   // 0x007b7870

    // The ragdoll (human_pose_ragdoll.cpp).
    static S_RagdollBody *NewRagdollBody(S_RagdollBody *parent, int boneCount);   // 0x007c54e0
    void FreeRagdollBody(S_RagdollBody *body);          // 0x007c5600
    void MeasureRagdollBody(S_RagdollBody *body, int startBone, int endBone, bool setLength, bool setThickness,
                            bool setRoll);              // 0x007c5740
    bool AddRagdollBone(S_RagdollBody *body, int bone);   // 0x007c5a80
    static S_RagdollPoint *NewRagdollPoint(S_RagdollPoint *parent, const S_vector4 &pos, S_RagdollBody *b0,
                                           S_RagdollBody *b1, S_RagdollBody *b2, S_RagdollBody *b3,
                                           S_RagdollBody *startOf, S_RagdollBody *endOf);   // 0x007c61b0
    void FreeRagdollPoint(S_RagdollPoint *point);       // 0x007c64b0
    void BuildRagdoll(bool reload);                     // 0x007c11f0
    static void SaveRagdollBodies(C_chunk &chunk, const S_RagdollBody *body);   // 0x007c3c90
    void LoadRagdollBodies(C_chunk &chunk, S_RagdollBody *body);   // 0x007c42a0
    static void SaveRagdollPoints(C_chunk &chunk, const S_RagdollPoint *point);   // 0x007c4a70
    void LoadRagdollPoints(C_chunk &chunk, S_RagdollPoint *point);   // 0x007c4f40
    void RagdollBoneMatrices(const S_RagdollBody *body);   // 0x007c5e20
    void SetRagdollBones(const S_RagdollBody *body, float weight);   // 0x007c5ef0
    void PoseFromRagdoll(const S_RagdollBody *body, float weight);   // 0x007c5d10
    static void StoreRagdollRest(S_RagdollBody *body);  // 0x007cdfd0
    static void ResetRagdollToRest(S_RagdollBody *body);   // 0x007cdef0
    static void BlendRagdollToRest(S_RagdollBody *body, float t);   // 0x007ce0f0
    void PlaceRagdollPoint(S_RagdollPoint *point, const S_vector4 &pos, bool stop);   // 0x007c6560

    // The ragdoll's simulation (human_pose_physics.cpp).
    void StepRagdoll(int time);                         // 0x007c7c40
    void IntegrateRagdollPoints(S_RagdollPoint *point, float dt);   // 0x007c7e50
    void UpdateRagdollBodies(S_RagdollBody *body, bool save, bool rebuild);   // 0x007c8540
    void ConstrainRagdollBody(S_RagdollBody *body, float dt);   // 0x007c8f70
    void ApplyRagdollCorrections();                     // 0x007cbf40
    void CollideRagdoll(float dt);                      // 0x007cc1b0
    void CollideRagdollPoint(S_RagdollPoint *point, S_vector4 &out);   // 0x007cc3f0
    void CollideRagdollBody(S_RagdollBody *body, S_vector4 &startOut, S_vector4 &endOut);   // 0x007ccba0
    void SettleRagdoll(S_RagdollPoint *point);          // 0x007cd690
    void AddRagdollPush(S_RagdollPoint *point, const S_vector4 &force, int time);   // 0x007cda70
    void AddRagdollHold(S_RagdollPoint *point, const S_vector4 &target, int time);   // 0x007cdb20
    void ApplyRagdollPushes(float dt);                  // 0x007cdb90
    void UpdateRagdollContacts(int time);               // 0x007cdca0
    void ReleaseRagdollContacts();                      // 0x007cdea0
    void PlayRagdollSound(S_RagdollPoint *point, bool second);   // 0x007ce260
    void UpdateRagdollSound(bool second);               // 0x007ce370

public:
    C_Human *m_owner = nullptr;                         // +0x0000
    C_HumanMovement *m_movement = nullptr;              // +0x0004 (the owner's)
    I3D_frame *m_model = nullptr;                       // +0x0008
    int m_unknown000C = 0;
    S_PoseBone m_bones[POSE_BONE_COUNT];                // +0x0010
    S_PoseBone *m_root = nullptr;                       // +0x61D0 the pelvis
    int m_boneCount = 0;                                // +0x61D4
    int m_unknown61D8 = 0;
    int m_unknown61DC = 0;
    S_PoseChain m_chain;                                // +0x61E0
    int m_unknown62A8 = 0;
    int m_unknown62AC = 0;
    C_HumanStains *m_stains = nullptr;                  // +0x62B0 (detailed single player only)
    int m_time = 0;                                     // +0x62B4 ms of the last update
    int m_updateCount = 0;                              // +0x62B8 (random start)
    int m_updateSeen = 0;                               // +0x62BC m_updateCount of the last refresh
    int m_aimTime = 0;                                  // +0x62C0 ms of the aim's turn
    int m_aimLength = 0;                                // +0x62C4
    bool m_raised = false;                              // +0x62C8 the weapon up
    bool m_visible = false;                             // +0x62C9 drawn in enough detail to pose
    uint8_t m_unknown62CA[2] = {};
    int m_weaponKind = 0;                               // +0x62CC WEAPON_KIND_* from the behaviour
    int m_stance = 0;                                   // +0x62D0 POSE_STANCE_*
    int m_stanceBeforeSeat = 0;                         // +0x62D4
    uint32_t m_hand = 0;                                // +0x62D8 low byte: the weapon hand (1: hw1); bit 31: support hand
    int m_behaviour = 0;                                // +0x62DC C_ItemDef behaviour of the weapon
    I3D_frame *m_weapon = nullptr;                      // +0x62E0 the weapon in the hands (referenced)
    I3D_frame *m_leftGrip = nullptr;                    // +0x62E4 "LW"
    I3D_frame *m_rightGrip = nullptr;                   // +0x62E8 "RW"
    I3D_frame *m_stock = nullptr;                       // +0x62EC "BUTT"
    I3D_frame *m_hw1 = nullptr;                         // +0x62F0 "hw1"
    I3D_frame *m_hw2 = nullptr;                         // +0x62F4 "hw2"
    I3D_frame *m_camera3rd = nullptr;                   // +0x62F8 "camera3RD" (referenced)
    I3D_frame *m_cameraFpv = nullptr;                   // +0x62FC "cameraFPV" (referenced)
    I3D_frame *m_dummyZ = nullptr;                      // +0x6300 "z"
    uint32_t m_flags = 0;                               // +0x6304 POSE_FLAG_*
    int m_blendTime = 0;                                // +0x6308
    int m_blendLength = 0;                              // +0x630C
    int m_pauseTime = 0;                                // +0x6310 ms (POSE_FLAG_PAUSED)
    int m_supportTime = 0;                              // +0x6314 0..200 ms
    float m_blend = 0.0f;                               // +0x6318 m_blendTime / m_blendLength
    float m_support = 0.0f;                             // +0x631C m_supportTime / 200
    I3D_frame *m_leftHand = nullptr;                    // +0x6320 bone "n"
    I3D_frame *m_rightHand = nullptr;                   // +0x6324 bone "j"
    int m_unknown6328 = 0;
    int m_unknown632C = 0;
    S_vector4 m_leftHandTarget;                         // +0x6330
    S_vector4 m_rightHandTarget;                        // +0x6340
    S_vector4 m_weaponDir;                              // +0x6350 where the weapon points
    S_vector4 m_aimDir;                                 // +0x6360
    S_vector4 m_aimFrom;                                // +0x6370 the aim's turn from ...
    S_vector4 m_aimTarget;                              // +0x6380 ... to
    S_vector4 m_cameraOffset;                           // +0x6390
    S_vector4 m_leftHandOffset;                         // +0x63A0 in the grip's axes
    S_vector4 m_rightHandOffset;                        // +0x63B0
    float m_aimAngle = 0.0f;                            // +0x63C0
    float m_aimSin = 0.0f;                              // +0x63C4
    int m_unknown63C8 = 0;
    int m_unknown63CC = 0;
    S_vector4 m_velocity;                               // +0x63D0 of the model, smoothed
    S_vector4 m_lastVelocity;                           // +0x63E0
    S_vector4 m_sampledVelocity;                        // +0x63F0
    S_vector4 m_lastPos;                                // +0x6400
    int m_velocityTime = 0;                             // +0x6410 ms to the next sample (100)
    C_Actor *m_vehicle = nullptr;                       // +0x6414 the seat's (dead: the vehicle it lies on)
    uint32_t m_seatFlags = 0;                           // +0x6418
    int m_seat = 0;                                     // +0x641C
    int m_seatCount = 0;                                // +0x6420
    I3D_frame *m_seatLeftGrip = nullptr;                // +0x6424 "LWC" / "LWS"
    I3D_frame *m_seatRightGrip = nullptr;               // +0x6428 "RWC" / "RWS"
    int m_unknown642C = 0;
    // +0x6430 the steering wheel: [0] the inverse of the grips' parent's local matrix, [1] / [2]
    // the grips in the vehicle's axes.
    S_matrix m_wheel[3];
    S_matrix m_unknown64F0[2];
    uint32_t m_lookFlags = 0;                           // +0x6570 LOOK_*
    int m_unknown6574 = 0;
    int m_unknown6578 = 0;
    int m_unknown657C = 0;
    S_vector4 m_lookAt;                                 // +0x6580 world point
    S_vector4 m_lookDir;                                // +0x6590 in the model's axes
    S_vector4 m_lookFrom;                               // +0x65A0
    S_vector4 m_lookTo;                                 // +0x65B0
    int m_lookTime = 0;                                 // +0x65C0
    int m_glanceTime = 0;                               // +0x65C4
    int m_lookLength = 0;                               // +0x65C8
    int m_holdTime = 0;                                 // +0x65CC
    float m_lookPower = 2.5f;                           // +0x65D0 the turn's ease exponent
    float m_lookWeight = 0.0f;                          // +0x65D4
    int m_unknown65D8 = 0;
    int m_unknown65DC = 0;
    S_vector4 m_proneDir;                               // +0x65E0 the owner's direction last fitted
    bool m_proneFit = false;                            // +0x65F0 fit the prone body to the ground
    uint8_t m_unknown65F1[3] = {};
    int m_proneSettle = 0;                              // +0x65F4 ms
    uint8_t *m_proneHits = nullptr;                     // +0x65F8 [6] 0 none, 1 near, 2 far
    S_vector4 *m_proneGround = nullptr;                 // +0x65FC [6] where the spheres stop
    S_vector4 *m_proneNormals = nullptr;                // +0x6600 [6]
    S_vector4 *m_proneFrom = nullptr;                   // +0x6604 [6] the blends' start ...
    S_vector4 *m_proneTo = nullptr;                     // +0x6608 [6] ... end ...
    S_vector4 *m_proneNow = nullptr;                    // +0x660C [6] ... and value (x unset: reset)
    S_vector4 *m_proneSpheres = nullptr;                // +0x6610 [6]
    int *m_proneBlendTime = nullptr;                    // +0x6614 [6] ms left
    int m_proneBlendLength = 100;                       // +0x6618
    S_quat **m_gunnerRot = nullptr;                     // +0x661C [6][23] the gunner poses' rotations
    S_vector4 **m_gunnerPos = nullptr;                  // +0x6620 [6][23] ... and positions
    bool m_hasGunnerPose = false;                       // +0x6624
    uint8_t m_unknown6625[3] = {};
    float m_elbowOut = 0.5f;                            // +0x6628
    float m_elbowIn = 0.9f;                             // +0x662C
    I3D_animation_set *m_activityAnim = nullptr;        // +0x6630
    int m_activityTime = 0;                             // +0x6634
    int m_activityPosKey[POSE_BONE_COUNT] = {};         // +0x6638
    int m_activityRotKey[POSE_BONE_COUNT] = {};         // +0x6694
    int m_recoilTime = 0;                               // +0x66F0
    int m_accelTime = 0;                                // +0x66F4 ms to the next sample (200)
    int m_unknown66F8 = 0;
    int m_unknown66FC = 0;
    S_vector4 m_vehicleVelocity;                        // +0x6700 last sample
    S_vector4 m_accel;                                  // +0x6710 blended
    S_vector4 m_lastAccel;                              // +0x6720
    S_vector4 m_sampledAccel;                           // +0x6730
    // The dead body.
    bool m_bloodPool = false;                           // +0x6740 left its blood pool
    uint8_t m_unknown6741[3] = {};
    I3D_animation_set *m_deathAnim = nullptr;           // +0x6744 the dying animation (null: ragdoll)
    int16_t m_deathKind = 0;                            // +0x6748 E_DeathKind
    uint8_t m_unknown674A[2] = {};
    int m_deathTime = 0;                                // +0x674C ms of the animation played
    int m_deathPosKey[POSE_BONE_COUNT] = {};            // +0x6750
    int m_deathRotKey[POSE_BONE_COUNT] = {};            // +0x67AC
    int m_unknown6808 = 0;
    int m_unknown680C = 0;
    S_vector4 m_bodyPos;                                // +0x6810 where the scene last heard of the body
    S_vector4 m_slideVelocity;                          // +0x6820 the dying body's slide
    S_vector4 m_cameraPos;                              // +0x6830 the first person camera's
    S_vector4 *m_slidePoints = nullptr;                 // +0x6840 [7] (DEATH_SLIDE_*, a held body's)
    C_Human *m_carrier = nullptr;                       // +0x6844 carrying the body when dropped
    S_RagdollBody *m_bodies[RAGDOLL_BODY_COUNT] = {};   // +0x6848
    S_RagdollPoint *m_points[RAGDOLL_POINT_COUNT] = {};  // +0x6874
    S_RagdollBody *m_rootBody = nullptr;                // +0x68B4
    S_RagdollPoint *m_rootPoint = nullptr;              // +0x68B8
    int m_unknown68BC = 0;
    S_vector4 m_gravity;                                // +0x68C0 (0, -9, 0); slower in water
    float m_ragdollWeight = 0.0f;                       // +0x68D0 runs down to 0 as the body settles
    float m_settleCount = 0.0f;                         // +0x68D4 the points counted, then their average weight
    float m_settleSum = 0.0f;                           // +0x68D8 the points' weights
    S_matrix *m_restMatrices = nullptr;                 // +0x68DC [14] RAGDOLL_REST_*
    S_RagdollPush *m_pushes = nullptr;                  // +0x68E0 [8]
    S_RagdollPush *m_holds = nullptr;                   // +0x68E4 [2]
    int m_unknown68E8 = 0;                              // +0x68E8 ms to the next 100 ms step
    float m_settleDelay = 0.0f;                         // +0x68EC s before the body may settle (1)
    float m_groundTime = 0.0f;                          // +0x68F0 s on the ground before it settles faster (4)
    float m_sinceCollision = 0.0f;                      // +0x68F4 s since the last collision substep
    float m_stepTime = 0.0f;                            // +0x68F8 s of a substep
    int m_step = 0;                                     // +0x68FC
    int m_steps = 0;                                    // +0x6900 substeps per step (3..6 by the detail)
    int m_unknown6904 = 0;
    int m_unknown6908 = 0;
    int m_contactFrames = 0;                            // +0x690C points with a contact frame
    uint8_t m_groundedPoints = 0;                       // +0x6910 lying on flat ground
    bool m_collideStep = false;                         // +0x6911 this substep collides
    bool m_inWater = false;                             // +0x6912
    uint8_t m_unknown6913 = 0;
    int m_impactSound = -1;                             // +0x6914 sound slot (-1: none)
    int m_dropSound = -1;                               // +0x6918 (-2: done)
    float m_savedRolls[RAGDOLL_BODY_COUNT] = {};        // +0x691C the parts' rolls as built
    int m_unknown6948[2] = {};
};
