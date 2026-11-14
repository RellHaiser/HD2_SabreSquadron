// The movement of a human (S_HumanBody::movement; original source 0x00748dc0..0x0078a890): the
// body's collision volumes, skeleton joints and their physics, the animations of the model, the
// footsteps, the light level and shadow of the model, and the network replay of the moves of
// humans simulated elsewhere.
//
// C_HumanMovement (0xDD0 bytes, 16-byte aligned) is a composite of cooperating parts that point
// at each other (SetOwner / Init wire them):
//   C_HumanAnimMixer    +0x260  animation stages of the model
//   C_HumanMorph        +0x610  morphs (Tables\morph.sav)
//   C_HumanSpecialMoves +0xAA0  ladders, obstacles, getting into water (role inferred)
//   C_HumanLocomotion   +0xAC0  walking, running, swimming (role inferred)
//   C_HumanAnimPlayer   +0xC00  plays the body animations
//   C_HumanIdle         +0xCB0  standing fidgets, breath vapour, the interactions (seats, doors, ...)
//   C_HumanVisual       +0xCD0  position history, sector, light level, shadow (role inferred)
//   C_HumanWeaponPose   +0xDC0  (allocated) the weapon in the hands and the arms' pose
// The parts are ported unit by unit; members not ported yet are declared with their addresses.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "game/human_weapon_pose.h"
#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_Human;
class C_HumanMovement;
class C_HumanLocomotion;
class C_HumanAnimPlayer;
class C_HumanSpecialMoves;
class C_HumanIdle;
class I3D_animation_set;
class I3D_frame;
class I3D_model;
class I3D_sector;
class I3D_sound;
struct S_CollisionVolume;
struct S_MoveContacts;

// The movement's clock: animation and physics times are given in game ms scaled by this.
constexpr float MOVE_TIME_SCALE = 1.4f;
// A time of `ms` game milliseconds in movement time (truncated, as the original's __ftol).
constexpr int MoveTime(float ms) { return static_cast<int>(ms / MOVE_TIME_SCALE); }

constexpr int MOVE_NO_TIME = -10000;
constexpr float MOVE_NO_VALUE = -10000.1f;

// The mission is a cold one: the humans breathe vapour (set by C_HumanMovement::Init).
extern bool g_coldMission;                              // 0x0098a6ad

// Stances (the 3 variants of the human animations).
enum E_HumanStance {
    HUMAN_STANCE_STAND = 0,
    HUMAN_STANCE_CROUCH = 1,
    HUMAN_STANCE_PRONE = 2,
};

// An animation of the human tables: id (E_HumanAnim, HUMAN_ANIM_NONE), stance, sub-variant.
struct S_HumanAnimRef {                                 // 0x10 bytes
    int anim = 0;                                       // +0x00
    int stance = 0;                                     // +0x04
    int sub = 0;                                        // +0x08
    bool valid = false;                                 // +0x0C set by Normalize / Set

    // (The original takes an S_HumanAnimRef by value and copies all but `valid`.)
    void Set(int anim_, int stance_, int sub_)          // 0x00783400
    {
        anim = anim_;
        stance = stance_;
        sub = sub_;
    }
    // Takes the stance from stance-specific ids and the id of the stance from the generic
    // stand / crouch / prone groups; marks the reference valid.
    void Normalize();                                   // 0x00783310
    // The same animation, stance and sub-variant (`valid` aside; `other` by value in the
    // original).
    bool IsSame(const S_HumanAnimRef &other) const      // 0x007748a0
    {
        return anim == other.anim && stance == other.stance && sub == other.sub;
    }
};

// What the body plays: the human animation, the carry animation of the hands item and the
// scripted activity (0x24 bytes; current at C_HumanMovement +0x1D8, requested at +0x1FC).
struct S_HumanAnimState {
    S_HumanAnimRef anim;                                // +0x00
    int carryAnim = 0;                                  // +0x10 C_CarryAnimTable id, 91: none
    int carryVariant = 0;                               // +0x14
    int unknown18 = 0;                                  // +0x18
    int activity = 0;                                   // +0x1C C_ActivityAnimTable id, 75: none
    int activityVariant = 0;                            // +0x20
};

// What the work tells the movement to do (the first 0xA0 bytes of C_HumanWork; the movement's
// copy at C_HumanMovement +0x770). Code 0x007801a0..0x00780a90 of the work unit. Each setter of
// the work marks its request in `changed` (WORK_REQUEST_*).
struct S_MoveRequest {                                  // 0xA0 bytes
    C_Actor *killer = nullptr;                          // +0x00 C_HumanWork::Die
    I3D_frame *workingFrame = nullptr;                  // +0x04 SetWorkingFrame
    C_Actor *interactTarget = nullptr;                  // +0x08 Interact
    int unknown0C = 0;                                  // +0x0C
    S_vector4 unknown10;                                // +0x10 SetLookAt (the step dust takes it as a direction)
    S_vector4 facingDir;                                // +0x20 SetFacingDir (x / z, normalized; remote humans move along it)
    uint8_t unknown30[0x10] = {};
    uint32_t changed = 0;                               // +0x40 WORK_REQUEST_* set since the last update
    int anim = 0x2D;                                    // +0x44 E_HumanAnim (Play)
    int activity = 0x4B;                                // +0x48 C_ActivityAnimTable id (0x4B: none; PlayWorkAnimation)
    int stance = HUMAN_STANCE_STAND;                    // +0x4C (SetStance)
    int gait = 1;                                       // +0x50 sub-variant of the moving animations (SetGait; 3 limping)
    int carryMode = 0;                                  // +0x54 C_ItemDef::GetCarryMode of the hands item
    int action = 0;                                     // +0x58 DoAction
    int unknown5C = 0;                                  // +0x5C Unknown77f830
    uint32_t hitFlags = 0;                              // +0x60 ApplyHit: the zone bits and the side it came from
    uint8_t unknown64[0x0C] = {};
    S_vector hitForce = {0.0f, 0.0f, 0.0f};             // +0x70
    uint8_t unknown7C[4] = {};
    S_vector hitPos = {0.0f, 0.0f, 0.0f};               // +0x80
    uint8_t unknown8C[4] = {};
    uint32_t interaction = 0;                          // +0x90
    int interactArg = 0;                                 // +0x94
    uint8_t unknown98[8] = {};

    void Reset();                                       // 0x007801a0
    // Chunk 0x308 with the raw record (the load clears the pointers).
    bool SaveGame(C_chunk &chunk) const;                // 0x00780990
    bool LoadGame(C_chunk &chunk);                      // 0x00780a90
};
static_assert(sizeof(S_MoveRequest) == 0xA0, "S_MoveRequest layout");

// A joint of the human skeleton (C_HumanMovement::m_joints).
struct S_HumanJoint {                                   // 0x0C bytes
    I3D_frame *frame = nullptr;                         // +0x00
    bool unknown04 = false;                             // +0x04
    int unknown08 = 0;                                  // +0x08
};

// Joints of the human skeleton (frames of the model, by name; HUMAN_JOINT_COUNT).
enum E_HumanJoint {
    HUMAN_JOINT_U = 0,                                  // left foot (footsteps)
    HUMAN_JOINT_Q = 1,                                  // right foot (footsteps)
    HUMAN_JOINT_T = 2,
    HUMAN_JOINT_P = 3,
    HUMAN_JOINT_F1 = 4,                                 // head height (under water test)
    HUMAN_JOINT_D1 = 5,
    HUMAN_JOINT_D = 6,                                  // back
    HUMAN_JOINT_C1 = 7,                                 // the mouth (breathing sounds)
    HUMAN_JOINT_G = 8,
    HUMAN_JOINT_K = 9,
    HUMAN_JOINT_H = 10,
    HUMAN_JOINT_L = 11,
    HUMAN_JOINT_I = 12,
    HUMAN_JOINT_M = 13,
    HUMAN_JOINT_J = 14,
    HUMAN_JOINT_N = 15,
    HUMAN_JOINT_A = 16,                                 // the pelvis (light level samples)
    HUMAN_JOINT_B = 17,
    HUMAN_JOINT_C = 18,
    HUMAN_JOINT_E = 19,
    HUMAN_JOINT_F = 20,
    HUMAN_JOINT_O = 21,
    HUMAN_JOINT_R = 22,
    HUMAN_JOINT_S = 23,
    HUMAN_JOINT_V = 24,
    HUMAN_JOINT_COUNT = 25,
};

// Dummies of the human model that give the collision spheres of the stances.
enum E_StanceDummy {
    STANCE_DUMMY_STAND = 0,                             // "stand1".."stand3"
    STANCE_DUMMY_KNEEL = 3,                             // "kneel1", "kneel2"
    STANCE_DUMMY_LIE = 5,                               // "lie1".."lie4"
    STANCE_DUMMY_COUNT = 9,
};

// C_HumanMovement::GetFlags() bits (meanings partly inferred). 0x1..0x8 are set from outside
// (S_HumanBody::SetMovementFlags; it names 2 / 4 carry modes); the detail bits 0x10..0x2000
// are set by the size of the body on the screen (C_HumanLocomotion::UpdateDetail).
enum : uint32_t {
    MOVE_FLAG_REMOTE = 0x1,                             // moved by the network (not simulated here; one sphere swept)
    MOVE_FLAG_GROUND_RAY = 0x2,                         // the AI's cheap ground mode: a ray down, no sweeps
    MOVE_FLAG_FOLLOW_PATH = 0x4,                        // along the AI's path (C_Human::AdjustToPathHeight)
    MOVE_FLAG_FROZEN = 0x8,                             // no collision, no physics (OnFrozen)
    MOVE_FLAG_LARGE = 0x10,                             // over 15 % of the screen and drawn (the morphs are applied)
    MOVE_FLAG_FULL_DETAIL = 0x20,                       // not small (or no display)
    MOVE_FLAG_SMALL = 0x40,                             // under 4.8 % of the screen
    MOVE_FLAG_TINY = 0x80,                              // under 1.35..3.5 % (by the gait)
    MOVE_FLAG_HIDDEN = 0x100,                           // not drawn or under 1.35 % (not the active player)
    MOVE_FLAG_NOT_DRAWN = 0x200,                        // not drawn in the last frame
    MOVE_FLAG_IN_RANGE = 0x1000,                        // within the camera's range
    MOVE_FLAG_OUT_OF_RANGE = 0x2000,                    // beyond it (or the view over 25 m high there)
    MOVE_FLAGS_INITIAL = 0x1020,
};

// Actions a remote human replays (S_RemoteAction::action; C_HumanMovement::ProcessRemoteActions).
enum E_RemoteAction {
    REMOTE_ACTION_STAND = 0,                            // 0..2: change to that stance
    REMOTE_ACTION_CROUCH = 1,
    REMOTE_ACTION_PRONE = 2,
    REMOTE_ACTION_USE = 3,                              // use C_HumanMovement::m_useObject
    REMOTE_ACTION_CLIMB_B = 0x13,                       // climbing animations 0x0B, 0x0C, 0x0D, 0x0A
    REMOTE_ACTION_CLIMB_C = 0x14,
    REMOTE_ACTION_CLIMB_D = 0x15,
    REMOTE_ACTION_CLIMB_A = 0x16,
};

// Groups of the movement sound bank (C_MovementSounds; the names of their files).
enum E_MovementSound {
    MOVE_SOUND_SWIM = 0,                                // p_swim01..02
    MOVE_SOUND_SWIM_UNDER_WATER = 1,                    // p_swuw01..02
    MOVE_SOUND_UVS = 2,                                 // uvs01..02
    MOVE_SOUND_UV1 = 3,                                 // uv100..103
    MOVE_SOUND_LIE_DOWN = 4,                            // o_lay
    MOVE_SOUND_KNEEL = 5,                               // o_kneel
    MOVE_SOUND_STAND_UP = 6,                            // o_stay
    MOVE_SOUND_MOVE_LOW = 7,                            // o_lwmv01..03
    MOVE_SOUND_MOVE = 8,                                // o_mimv01..06
    MOVE_SOUND_RUN = 9,                                 // o_frun01..06
    MOVE_SOUND_WEAPON_OUT = 10,                         // o_wout (drawing the hands item)
    MOVE_SOUND_WEAPON_IN = 11,                          // o_win (putting it away)
    MOVE_SOUND_GRENADE_TAKE = 12,                       // o_grtake
    MOVE_SOUND_OVER = 13,                               // o_over
    MOVE_SOUND_OVER2 = 14,                              // o_over2
    MOVE_SOUND_OVER3 = 15,                              // o_over3
    MOVE_SOUND_NONE16 = 16,                             // (no files)
    MOVE_SOUND_JUMP_OVER = 17,                          // o_juover
    MOVE_SOUND_DROWNING = 18,                           // drown01..04
    MOVE_SOUND_GASP = 19,                               // uv100 (surfacing after a long dive)
};

// Requests of OnRemoteAction (network messages; meanings inferred from the animations). Sent
// by C_Human::SendRemoteRequest: TryClimbOver for the climbing animations 0x0A..0x0E (A, B,
// C, D, END), EnterLadderFromTop CLIMB_START, the locomotion (0x00768690) CLIMB_ABORT.
enum E_RemoteRequest {
    REMOTE_REQUEST_CLIMB_END = 0,
    REMOTE_REQUEST_CLIMB_B = 1,
    REMOTE_REQUEST_CLIMB_C = 2,
    REMOTE_REQUEST_CLIMB_D = 3,
    REMOTE_REQUEST_CLIMB_START = 4,
    REMOTE_REQUEST_CLIMB_ABORT = 5,
    REMOTE_REQUEST_CLIMB_A = 6,
};

// Interactions of a human with the world (C_HumanIdle::Interact; the low 10 bits of the
// interaction, C_HumanMovement::m_interaction while one lasts). The pairs start and end one;
// the ends unblock the work and drop the start's steps from the remote actions. Codes from
// their targets' actor types, the animation files they play (Tables\anim.sav) and the
// controllers that issue them.
enum E_Interaction : uint32_t {
    INTERACT_DOOR = 0x01,                               // a C_Door; arg: 1 open, 0 close
    INTERACT_ENTER_CAR = 0x02,                          // arg: the seat
    INTERACT_LEAVE_CAR = 0x03,
    INTERACT_ENTER_SUBMARINE = 0x04,
    INTERACT_LEAVE_SUBMARINE = 0x05,
    INTERACT_ENTER_STATIC_WEAPON = 0x06,
    INTERACT_LEAVE_STATIC_WEAPON = 0x07,
    INTERACT_SIT = 0x08,                                // on a chair: arg is its frame
    INTERACT_STAND_UP = 0x09,                           // from the chair (animation 0x1D)
    INTERACT_STAND_UP_FAST = 0x0A,                      // (animation 0x26)
    INTERACT_ENTER_JUNKERS88 = 0x0B,
    INTERACT_LEAVE_JUNKERS88 = 0x0C,
    INTERACT_ENTER_SEAT_D = 0x0D,                       // (a seat whose kind is not identified)
    INTERACT_HEAL = 0x0E,                               // walks up to `target` ("medik01")
    INTERACT_HEAL_END = 0x0F,
    INTERACT_HEAL_HERE = 0x10,                          // where the human stands
    INTERACT_HEAL_HERE_END = 0x11,
    INTERACT_MANIPULATE = 0x12,                         // "manipul" (animation 4)
    INTERACT_MANIPULATE_END = 0x13,
    INTERACT_MANIPULATE_LONG = 0x14,                    // "manipulm" (5; under water "potapManipul")
    INTERACT_MANIPULATE_LONG_END = 0x15,
    INTERACT_MANIPULATE_LONG_2 = 0x16,                  // (as 0x14)
    INTERACT_MANIPULATE_LONG_2_END = 0x17,
    INTERACT_TIE_UP = 0x18,                             // `target` ("svazuje1")
    INTERACT_TIE_UP_END = 0x19,
    INTERACT_BE_TIED_UP = 0x1A,                         // by `target` ("svazovanej")
    INTERACT_BE_TIED_UP_END = 0x1B,                     // then tied up (animation 0x62 "svazanej")
    INTERACT_CUT = 0x1C,                                // with the wire cutters ("strihaLOOP")
    INTERACT_CUT_END = 0x1D,
    INTERACT_BACKSTAB = 0x1E,                           // cuts `target`'s throat ("podrezSTa")
    INTERACT_BACKSTAB_END = 0x1F,
    INTERACT_BE_BACKSTABBED = 0x20,                     // by `target` (HIT_BACKSTAB on the server)
    INTERACT_BE_BACKSTABBED_END = 0x21,
    INTERACT_24 = 0x24,                                 // animation 0x28 (no file in the tables)
    INTERACT_24_END = 0x25,
    INTERACT_CODE_MASK = 0x3FF,
    // The body takes part: the animations play and the model goes into / out of the seat
    // (without it only the state is set; inferred from what it guards).
    INTERACT_FLAG_BODY = 0x400,
};

// An action of a remote human waiting to be replayed (C_HumanMovement::m_remoteActions).
struct S_RemoteAction {                                 // 0x0C bytes
    int time = 0;                                       // counts down; negative: overdue
    int action = 0;                                     // E_RemoteAction
    int phase = 0;                                      // 0..3 (3: done, kept for a while)
};

// ------------------------------------------------------------------ the parts

// Events of the mixer's tracks (C_HumanAnimMixer::GetEvents; from the notes of the animations).
enum E_AnimEvent : int {
    ANIM_EVENT_WEAPON_DRAWN = 0,
    ANIM_EVENT_EQUIPMENT_1 = 1,
    ANIM_EVENT_WEAPON_READY = 2,
    ANIM_EVENT_UNKNOWN3 = 3,
    ANIM_EVENT_STEP = 4,                                // 4 / 6: the right foot, 5: the left foot
    ANIM_EVENT_STEP_LEFT = 5,
    ANIM_EVENT_STEP_6 = 6,
    ANIM_EVENT_DOOR = 7,
    ANIM_EVENT_PICK_UP = 8,
    ANIM_EVENT_ACTIVITY_END = 9,
    ANIM_EVENT_HIDE_HANDS_ITEM = 10,
    ANIM_EVENT_HIDE_HANDS_ITEM_B = 11,
    ANIM_EVENT_HIDE_HANDS_ITEM_C = 12,
    ANIM_EVENT_SPECIAL_DETACH = 13,
    ANIM_EVENT_SPECIAL_ATTACH = 14,
    ANIM_EVENT_PROJECTILE_HAND2 = 15,
    ANIM_EVENT_PROJECTILE_HAND1 = 16,
    ANIM_EVENT_PROJECTILE_WEAPON = 17,
    ANIM_EVENT_BREATH = 18,                             // a puff at the head
    ANIM_EVENT_LADDER_STEP = 19,
    ANIM_EVENT_LIGHT = 20,
    ANIM_EVENT_UNKNOWN21 = 21,
    ANIM_EVENT_UNKNOWN22 = 22,
    ANIM_EVENT_UNKNOWN23 = 23,
    ANIM_EVENT_SOUND = 24,
};

// An animation for a track of the mixer (C_HumanAnimMixer::Play / UpdateStages take one per
// track, MIXER_TRACKS; null `anim`: the track is left alone).
struct S_MixerAnim {                                    // 0x14 bytes
    I3D_animation_set *anim = nullptr;                  // +0x00
    float weight = 1.0f;                                // +0x04
    // Play mode of the set (E_I3D_ANIM_MODE) and how the new animation starts: 4 at the time
    // left of the one it replaces, 8 at the same share of its length (MIXER_SYNC_*).
    int mode = 2;                                       // +0x08 (2: I3DANIM_LOOP)
    float blendTime = 0.0f;                             // +0x0C ms the blend in takes
    bool crossfade = false;                             // +0x10 the old animation fades out as well
};
constexpr int MIXER_TRACKS = 4;
constexpr int MIXER_STAGES = 2 * MIXER_TRACKS;
constexpr int MIXER_TRACK_EVENTS = 5;
constexpr float MIXER_NO_BLEND = -10000.0f;
enum : int {
    MIXER_SYNC_TIME_LEFT = 0x4,
    MIXER_SYNC_SHARE = 0x8,
};
// A track of the mixer: the human animation (0), the carry animation (1), the activity (2) and
// the special animations of the animation player (3), each on a pair of stages of the model
// (2 * track + current), the other stage of the pair blending out.
struct S_MixerTrack {                                   // 0x2C bytes
    int current = -1;                                   // +0x00 stage of the pair playing (0 / 1; -1: none)
    int time = -1;                                      // +0x04 ms played (movement time)
    // > 0: the animation is held at this time (rate 0, HoldTrack); -1 after ReleaseTrack.
    int hold = 0;                                       // +0x08
    int events[MIXER_TRACK_EVENTS] = {};                // +0x0C E_AnimEvent since the last update
    int eventCount = 0;                                 // +0x20
    int length = 0;                                     // +0x24 ms (the animation's end time)
    bool reversed = false;                              // +0x28 the time was mirrored (MirrorTrackTime)
};
// A stage of the model as the mixer drives it.
struct S_MixerStage {                                   // 0x24 bytes
    float weight = 0.0f;                                // +0x00 the full weight
    float blendTime = 0.0f;                             // +0x04 ms
    float blendElapsed = MIXER_NO_BLEND;                // +0x08 ms (MIXER_NO_BLEND: the stage is free)
    bool crossfade = true;                              // +0x0C (S_MixerAnim::crossfade)
    uint32_t flags = 0;                                 // +0x10 MIXER_STAGE_*
    float rate = 1.0f;                                  // +0x14 of the time (0 while held)
    float speed = 1.0f;                                 // +0x18 of the time (from the body's speed)
    bool finished = false;                              // +0x1C the animation was taken off
    int tag = -1;                                       // +0x20 SetTrackTag / CheckTrackFinished
};
enum : uint32_t {
    MIXER_STAGE_FADE_OUT = 0x1,                         // blends to nothing, then the track stops
};
// A carry animation / an activity asked of the mixer (as in S_HumanAnimState).
struct S_MixerCarry {                                   // 0x0C bytes
    int anim = 0x5B;                                    // +0x00 C_CarryAnimTable id (0x5B: none)
    int variant = 0;                                    // +0x04
    int unknown08 = 0;                                  // +0x08
};
struct S_MixerActivity {                                // 0x08 bytes
    int activity = 0x4B;                                // +0x00 C_ActivityAnimTable id (0x4B: none)
    int variant = 0;                                    // +0x04
};
// The requests of a track queued while its blend is under way (2 entries, a ring).
template <class T>
struct S_MixerQueue {
    T entries[2];
    int read = 0;
    int write = 0;
    bool locked = false;                                // takes nothing until it has run empty
};

// Animation stages of the body model (C_HumanMovement +0x260; 0x3B0 bytes; 0x00772520..0x00777d10;
// human_anim_mixer.cpp): the tracks blend their animations in and out on the 8 stages of the
// model, at a rate from the body's speed; the root motion (velocity) the animation player
// reports and the moves it asks for give the move of each update; the note events of the
// animations come back as track events and sounds.
class C_HumanAnimMixer {
public:
    C_HumanAnimMixer() = default;                       // 0x00785d40
    bool SaveGame(C_chunk &chunk);                      // 0x00776610
    bool LoadGame(C_chunk &chunk);                      // 0x00776f90
    // Takes the owner's model; all tracks and stages free; hooks the note events.
    void Init(C_Human *owner);                          // 0x00772520
    // Unhooks the note events and takes the animations off the stages.
    void Done();                                        // 0x00772810
    // A new model: hooks it and takes over the stages of `previous` (unless the same).
    void Relink(I3D_model *model, I3D_model *previous); // 0x00772860
    // Advances the stages by `step` ms (unless `start`) and the blends of the tracks (with
    // `start`, only those with an animation in `anims`; MIXER_TRACKS or null).
    void UpdateStages(int time, int step, bool start, const S_MixerAnim *anims);   // 0x00772940
    // The move of the body for `time` ms (m_move): root motion and the requested moves.
    void Update(int time);                              // 0x00772e60
    // Puts the animations of the tracks 0..2 on their free stages for the body's animation,
    // carry animation and activity (`restart`: even the one playing); blends under way are
    // reversed (their mode set to loop).
    void Play(S_MixerAnim *anims, const S_HumanAnimRef *anim, const int *carry, const int *activity,
              bool restart);                            // 0x00773830
    // The root motion velocity of the new animation (m/s at `weight`); `result`: the velocity
    // of the heavier of it and the previous one.
    void SetVelocity(const S_vector4 &velocity, float weight, float maxSpeed, S_vector4 *result);   // 0x00773cb0
    // As SetVelocity, the previous velocity kept.
    void ReplaceVelocity(const S_vector4 &velocity, float weight, float maxSpeed, S_vector4 *result);   // 0x00773de0
    // Moves the body by `move` within `duration` ms.
    void AddMove(const S_vector4 &move, int duration);  // 0x00773ef0
    // Moves the body to `target` at `speed` m/s for `duration` ms.
    void MoveTo(const S_vector4 &target, float speed, int duration);   // 0x00773f30
    void AddMove2(const S_vector4 &move, int duration); // 0x00773f80
    // Moves the body by `offset` within `duration` ms, in 80 ms steps.
    void Slide(const S_vector4 &offset, int duration);  // 0x00773fc0
    // Queue the requests of the tracks 0..2 while their blends are under way; `anim` / `carry`
    // come back as what to play now (none: HUMAN_ANIM_NONE / CARRY_ANIM_NONE). Return 1.
    float QueueAnim(S_HumanAnimRef *anim);              // 0x007741f0
    float QueueCarry(int *carry);                       // 0x007748d0
    void PushCarry(const int *carry);                   // 0x00774e80
    float QueueActivity(int *activity);                 // 0x00774f70 (DS 0x007dfa60) (nothing)
    // Empties the queue of a track (0..2) / locks it until it has run empty.
    void ClearQueue(int track);                         // 0x00775770 (0x00774f80 is an unused copy)
    void LockQueue(int track);                          // 0x007758b0
    // ms the animation of a track has played (MOVE_NO_TIME: none).
    int GetTrackTime(int track);                        // 0x007750c0
    // Whether the animation of a track has played `ms` game ms (true: none).
    bool HasTrackPlayed(int track, int ms);             // 0x00775670
    // The animation of a track has played this share of its length (-1: none).
    float GetTrackProgress(int track);                  // 0x00775bd0
    // How far the blend of a track is (1: done; 0: no animation).
    float GetBlendProgress(int track);                  // 0x007758e0
    // Fades the track out within `time` ms.
    void FadeOutTrack(int track, int time);             // 0x00775120
    void SetTrackRate(int track, float rate);           // 0x007751b0
    // Holds the animation of a track at `time` / lets it go on.
    void HoldTrack(int track, int time);                // 0x007756c0
    void ReleaseTrack(int track);                       // 0x00775700
    // Holds it at a share of its length.
    void HoldTrackAt(int track, float share);           // 0x00775b70
    // In the first half of its animation: plays it from the mirrored time.
    void MirrorTrackTime(int track);                    // 0x00775b00
    // The animation events of a track (0..2) since the last update: their ids and number.
    const int *GetEvents(int track, int *count);        // 0x00775740
    // Tells when the animation of a track is taken off (CheckTrackFinished with `tag`).
    void SetTrackTag(int track, int tag);               // 0x00775a10
    bool CheckTrackFinished(int track, int tag);        // 0x00775a50
    int GetTrackTag(int track);                         // 0x00775ad0 (-1: none)
    // Plays `anim` on track 3 (blending in for `blendTime` ms; faded out `fadeOut` ms before
    // its end unless `loop`).
    void PlayTrack3(I3D_animation_set *anim, int blendTime, int fadeOut, bool loop);   // 0x00775c40
    void FadeOutTrack3(int time);                       // 0x00775dd0
    // Track 0 is held at `share` (at least 0.22) of `length` ms when it gets there.
    void SetHoldTime(int length, float share);          // 0x00776570
    // The parts of the model (I3D_model::GetTracks) of one of the frames `frames[count]` are
    // told the stage of `track`; the frames found are taken out (`frames` needs count + 1
    // entries: the original moves frames[count] into the place of a frame found).
    void NotifyFrames(int track, I3D_frame **frames, int count);   // 0x00775970
    // Adds an event to a track (C_HumanAnimPlayer does so for the carry track).
    void AddTrackEvent(int track, int event);
    // The callback of the model's note frame "a" (I3D_CALLBACK; I3DCALLBACK_ANIM_NOTE): the note
    // events of the owner's mixer and the note sounds (AttachModelUpdateHook hooks it on models
    // without an actor, which get the sounds only).
    static int __stdcall NoteHook(I3D_frame *frame, uint32_t message, uintptr_t param1, uintptr_t param2);   // 0x007751e0

    I3D_model *m_model = nullptr;                       // +0x000 the owner's model
    I3D_frame *m_noteFrame = nullptr;                   // +0x004 "a" (note events)
    C_Human *m_owner = nullptr;                         // +0x008
    S_MixerTrack m_tracks[MIXER_TRACKS];                // +0x00C
    S_MixerStage m_stages[MIXER_STAGES];                // +0x0BC
    // What the tracks blend from (none when the blend is done) and to.
    S_HumanAnimState m_previous;                        // +0x1DC
    S_HumanAnimState m_current;                         // +0x200
    int m_track3FadeOut = -1;                           // +0x224 ms before the end (-1: none)
    float m_blendProgress = 0.0f;                       // +0x228 of track 0 (1: done)
    uint8_t m_unknown22C[4] = {};
    // The root motion velocities of the previous and the new animation (m/s at weight 1)
    // and their weights; the move is limited to maxSpeed (0: not).
    S_vector4 m_velocityA = {};                         // +0x230
    float m_weightA = 0.0f;                             // +0x240
    uint8_t m_unknown244[0xC] = {};
    S_vector4 m_velocityB = {};                         // +0x250
    float m_weightB = 0.0f;                             // +0x260
    float m_maxSpeed = 0.0f;                            // +0x264
    uint8_t m_unknown268[8] = {};
    S_vector4 m_move = {};                              // +0x270 of the last update (m)
    // AddMove: what is left to move and the time.
    S_vector4 m_addMove = {};                           // +0x280
    int m_addMoveTime = 0;                              // +0x290
    int m_addMoveDuration = MOVE_NO_TIME;               // +0x294 MOVE_NO_TIME: none
    // MoveTo.
    float m_moveToSpeed = 0.0f;                         // +0x298
    uint8_t m_unknown29C[4] = {};
    S_vector4 m_moveToTarget = {};                      // +0x2A0
    int m_moveToTime = 0;                               // +0x2B0
    int m_moveToDuration = MOVE_NO_TIME;                // +0x2B4
    uint8_t m_unknown2B8[8] = {};
    // A move by m_extraMove within m_extraMoveDuration ms (zero: none). Nothing sets it but
    // LoadGame (and that without the times); the original leaves the times uninitialised.
    S_vector4 m_extraMove = {};                         // +0x2C0
    int m_extraMoveTime = 0;                            // +0x2D0
    int m_extraMoveDuration = 0;                        // +0x2D4
    uint8_t m_unknown2D8[8] = {};
    // AddMove2.
    S_vector4 m_addMove2 = {};                          // +0x2E0
    int m_addMove2Time = 0;                             // +0x2F0
    int m_addMove2Duration = MOVE_NO_TIME;              // +0x2F4
    uint8_t m_unknown2F8[8] = {};
    // Slide: what is left, the velocity, the time in the current 80 ms step.
    S_vector4 m_slide = {};                             // +0x300
    S_vector4 m_slideVelocity = {};                     // +0x310
    int m_slideStepTime = 0;                            // +0x320
    int m_slideTime = 0;                                // +0x324
    int m_slideDuration = MOVE_NO_TIME;                 // +0x328
    float m_speedScale = 1.0f;                          // +0x32C of the animation speed
    int m_stepSoundTime = 0;                            // +0x330 ms since the last footstep event
    int m_holdTime = MOVE_NO_TIME;                      // +0x334 SetHoldTime
    S_MixerQueue<S_HumanAnimRef> m_animQueue;           // +0x338
    S_MixerQueue<S_MixerCarry> m_carryQueue;      // +0x364
    S_MixerQueue<S_MixerActivity> m_activityQueue;    // +0x388
    uint8_t m_unknown3A4[0xC] = {};

private:
    // Plays the new animation of a track from the time `anims` asks (MIXER_SYNC_*).
    void SyncTrackTime(const S_MixerAnim *anims, int track);   // 0x007736c0
    // A note key of the model's animations: events of the tracks, holds, steps.
    void OnNote(uint32_t message, const char *note, int time);   // 0x00775250
};
// How far a blend of `duration` ms is after `elapsed` ms: the middle of the last step of
// `step` ms (also called at 0x005c6ee0 / 0x005c7b00).
float __stdcall MixerBlendProgress(float elapsed, float step, float duration);   // 0x00772900
static_assert(sizeof(S_MixerTrack) == 0x2C, "S_MixerTrack layout");
static_assert(sizeof(S_MixerStage) == 0x24, "S_MixerStage layout");

// The morphs of a human's model (C_HumanMovement +0x610; 0x88 bytes; 0x00780df0..0x007830e0;
// human_morph.cpp): the channels of the model's skinned morph visual ('SMMR') blend between the
// states of Tables\morph.sav (per animation group): blinking (channel 1), breathing by the
// exhaustion (channel 0, with breath sounds), expressions on request (channels 0 and 1) and the
// grips of the hands (channels 3 and 4); a random dead face when the body goes limp.
class C_HumanMorph {
public:
    C_HumanMorph();                                     // 0x00780df0 (loads the tables once)
    bool SaveGame(C_chunk &chunk);                      // 0x00782310
    bool LoadGame(C_chunk &chunk);                      // 0x00782730
    // Init + blinking on.
    void Reset();                                       // 0x00782cd0
    // Finds the morph visual and the head of the owner's model; neutral states.
    void Init(C_Human *owner);                          // 0x00780e20
    // Takes back the breath sound.
    void Done();                                        // 0x00780f10
    // Blinking on (a new random interval) or off.
    void SetBlinking(bool on);                          // 0x007816b0
    // A random dead face (states 15..18; the eyes turned a little), unless the camera is near.
    void ShowDeathFace();                               // 0x00781770
    // Exhaustion: `start` running (the exhaustion counts up) or stopping (it settles).
    void SetExhausted(bool start, bool cap);            // 0x007815b0
    void Update(int time);                              // 0x00781b10
    // Init again (after a model change).
    // Init() on the owner again (the frame argument the caller passes is overwritten).
    void Relink(I3D_frame *unused);                     // 0x00781f50
    // Expressions 2 / 3 / 4 at `speed` (0..3: how long they hold).
    void SetUnknown1(int speed);                        // 0x007816f0
    void SetUnknown2(int speed);                        // 0x00781710
    void SetUnknown3(int speed);                        // 0x00781730
    // The grips of the hands (0 none, 1 melee / thrown, 2 weapon; C_HumanEquipment).
    void SetUnknown64(int grip) { m_gripRequest = grip; }   // 0x00781750
    void SetUnknown78(int grip) { m_grip2Request = grip; }  // 0x00781760

    // The tables of Tables\morph.sav.
    static void LoadTables();                           // 0x00781f60

    C_Human *m_owner = nullptr;                         // +0x00
    I3D_frame *m_morphVisual = nullptr;                 // +0x04 'SMMR' child of the model
    int m_group = 1;                                    // +0x08 the owner's animation group
    I3D_frame *m_head = nullptr;                        // +0x0C frame "f" (breath sounds)
    bool m_dirty = false;                               // +0x10 the weights need applying
    int m_blinkState = 1;                               // +0x14
    int m_blinkPrevious = 1;                            // +0x18
    int m_blinkTime = 0;                                // +0x1C
    int m_blinkDuration = 0;                            // +0x20
    int m_blinkTimer = -1;                              // +0x24 ms to the next blink (-1 off)
    int m_breathState = 0;                              // +0x28
    int m_breathPrevious = 0;                           // +0x2C
    int m_breathTime = 0;                               // +0x30
    int m_breathDuration = 0;                           // +0x34
    int m_exhaustTime = -1;                             // +0x38 while running (-1: not)
    int m_exhaustion = 0;                               // +0x3C ms, settles down again
    bool m_heavyBreath = false;                         // +0x40
    int m_expressionTime = 0;                           // +0x44
    int m_expressionDuration = 0;                       // +0x48
    int m_expressionRequest = 100;                      // +0x4C (100: none)
    int m_expressionSpeed = 1;                          // +0x50
    int m_gripState = 0;                                // +0x54
    int m_gripPrevious = 0;                             // +0x58
    int m_gripTime = 0;                                 // +0x5C
    int m_gripDuration = 0;                             // +0x60
    int m_gripRequest = 100;                            // +0x64 (100: none)
    int m_grip2State = 0;                               // +0x68
    int m_grip2Previous = 0;                            // +0x6C
    int m_grip2Time = 0;                                // +0x70
    int m_grip2Duration = 0;                            // +0x74
    int m_grip2Request = 100;                           // +0x78 (100: none)
    I3D_sound *m_breathSound = nullptr;                 // +0x7C
    int m_breathSoundTime = -1;                         // +0x80 ms left of the sound
    float m_breathVolume = -1.0f;                       // +0x84

private:
    // Puts the blended weights on the channels (`force`: the target states at once).
    void Apply(bool force);                             // 0x00780f30
    void UpdateBreathSound(int time);                   // 0x00782000 (DS 0x007f00b0)
};

// Ladders, obstacles, getting into water (C_HumanMovement +0xAA0; 0x20 bytes; role inferred).
class C_HumanSpecialMoves {
public:
    void Init(C_HumanMovement *movement, C_HumanLocomotion *locomotion, C_HumanAnimPlayer *animPlayer,
              C_HumanIdle *idle, C_Human *owner);       // 0x0074a290
    // No wall in the way of the body's spheres turning by `angle` (rad; swimming, leaning or
    // lying; false also drops a turn in place asked for).
    bool CanTurn(float angle);                          // 0x0074a2c0
    // A lean (animations 0xA2..0xA5) asked for: how far it goes before a wall at the head
    // (C_HumanAnimPlayer::m_unknownA0; touching at once: none).
    void AdjustLean(S_HumanAnimState *next);            // 0x0074a6f0
    // An obstacle in front low enough to climb over: the climbing animation 0x0A..0x0E for its
    // height into `next` (C_HumanMovement::m_unknown958; remote steps on clients).
    bool TryClimbOver(S_HumanAnimState *next);          // 0x0074aaf0
    // Gets onto `ladder` from the end the body is at; 1 on it, 0 not possible, -1 busy.
    int UseLadder(C_Actor *ladder);                     // 0x0074b950
    // Room to get onto the ladder from its top / bottom (`ladderDir` the way the climber faces).
    bool EnterLadderFromTop(const S_vector4 &ladderDir);   // 0x0074c040
    bool CanEnterLadderFromBottom(const S_vector4 &ladderDir);   // 0x0074c640
    // Room to fall along `dir` (the death animations; `side` 0 ahead / behind, 1 / 2 the sides).
    bool HasRoomToFall(const S_vector4 &dir, int side); // 0x0074cb40
    // The death: the death animation for the hit into `next`, the body's state for it.
    bool Die(S_HumanAnimState *next);                   // 0x0074d970 (DS 0x007b0ce0)
    // Sitting down asked for: from sitting (0x22) "sednuti" (0x1C), else cross-legged (0x24).
    void StartSitting(S_HumanAnimState *next);          // 0x0074ec80
    // Moves the body out of a wall ahead (not called).
    void PushOutOfWalls();                              // 0x0074ecb0
    // The water at the body (the locomotion's depth and surface); true: deep enough to swim.
    bool UpdateWater();                                 // 0x0074eee0
    // Room for a change of stance (`change`: 0 / 1 lying down from standing / crouching, 2
    // standing up, 3 a step back from a wall ahead, 4 / 5 getting up from lying to standing /
    // crouching); lying down and getting up shift the body forward or back in steps when needed
    // (C_HumanLocomotion::m_unknown0D0). The replicas queue the stance instead (false).
    bool HasRoomForStance(int change);                  // 0x0074f3c0

    C_HumanMovement *m_movement = nullptr;              // +0x00
    C_HumanLocomotion *m_locomotion = nullptr;          // +0x04
    C_HumanAnimPlayer *m_animPlayer = nullptr;          // +0x08
    C_HumanIdle *m_idle = nullptr;                      // +0x0C
    C_Human *m_owner = nullptr;                         // +0x10
    uint8_t m_unknown14[0xC] = {};
};

// What a ground move stands on (C_HumanLocomotion::MoveOnGround; 0x18 bytes).
struct S_GroundContact {
    S_vector4 normal = {};                              // +0x00 of the contact found (zero: none)
    bool standing = false;                              // +0x10 that contact is a floor or ceiling, not a wall
    bool hasNormal = false;                             // +0x11
    S_CollisionVolume *hit = nullptr;                   // +0x14 what the sweep touched last
};

// Walking, falling, climbing, swimming, turning and the moves of a dead body (C_HumanMovement
// +0xAC0; 0x140 bytes; 0x00763950..0x007724d0; human_locomotion.cpp): each update moves the
// body by what the animation mixer moved it, swept through the collision world with the stance
// spheres (C_CollisionWorld::MoveSpheres*), falls and lands it, sets the root motion speed of
// the animation, turns the body towards the work's facing direction and keeps its collision
// volumes and the frame in step with the logical position.
class C_HumanLocomotion {
public:
    void Init(C_HumanMovement *movement, C_HumanSpecialMoves *specialMoves, C_HumanAnimPlayer *animPlayer,
              C_Human *owner);                          // 0x00763be0
    // Stops the swimming sound.
    void Done();                                        // 0x00763c20
    void Reset();                                       // 0x007724d0
    // Chunk 0xDE.
    bool SaveGame(C_chunk &chunk);                      // 0x00771570
    bool LoadGame(C_chunk &chunk);                      // 0x00771b00
    // Moves the body for `time` ms (the mixer is updated here).
    void Update(int time);                              // 0x00763c30 (DS 0x007caa10)
    // The detail of the body by its size on the screen (MOVE_FLAG_* 0x10..0x2000).
    void UpdateDetail();                                // 0x00770560 (DS 0x007da570)
    // The direction the work asks the body to face (x / z): the turn towards it starts (the
    // turn in place animations when the angle is large).
    void SetTurnTarget(const S_vector4 &dir, int time); // 0x0076df90
    // Turns the body towards the turn target for `time` ms; true once it faces it (or no
    // turn is due). `steady`: at the turn speed (replicas, no turn animations).
    bool UpdateTurn(int time, bool steady);             // 0x0076d440
    // Moves the collision volumes and the dummies to the logical position (the frame is put
    // there for that and back). `remote`: the frame was moved by the network.
    void UpdateCollisionVolumes(bool remote);           // 0x00764320
    // The change of stance the work asks for: the room for it (C_HumanSpecialMoves::
    // HasRoomForStance; refused: the stance stays) and the animation of the change.
    void ChangeStance();                                // 0x0076b3d0 (DS 0x007d3fe0)
    // The root motion speed of the animation playing (or about to play) and the facing it
    // moves along; `replace`: the previous velocity is kept (C_HumanAnimMixer).
    void UpdateVelocity(bool replace);                  // 0x0076bad0
    // The position on the ladder in use nearest to the body's (none: the body's).
    void GetLadderPos(S_vector4 *pos);                  // 0x00770e20
    // The ms of the turn in place animation towards `dir`.
    int GetTurnTime(const S_vector &dir);               // 0x0076d290
    // The body lands (`hard`: after falling over 9.81 m/s; the landing sound): the fall
    // damage and the fall state end.
    void Land(bool hard);                               // 0x00770ff0
    // Gets out of the water (the stance's walking animation, a step up; `putAway`: the
    // hands item is put away).
    void StopSwimming(bool putAway);                    // 0x0076b2d0
    // Fades the swimming sound out (`now`: stops it at once).
    void StopSwimSound(bool now);                       // 0x00770b50
    // The stance volumes at the stance spheres, their radii 1.25 times the spheres' with
    // `enlarged` (called by the vehicle code at 0x005d5620).
    void ScaleStanceVolumes(bool enlarged);             // 0x00770990

private:
    // Init / Reset: the state, and whether the body starts in the air (over 20 m above the
    // ground or water): it comes down on a parachute (C_HumanMovement::m_parachuting).
    void ResetState();                                  // 0x00763950
    // The moves of the body's states (Update picks one).
    bool MoveOnGround(int time);                        // 0x00764b50 (true: touched something)
    void MoveClimbing(int time);                        // 0x007681a0 (anims 8..0x0E)
    void MoveJumping(int time);                         // 0x007685d0 (anims 0x12, 0x17, 0x18)
    void MoveOnLadder(int time);                        // 0x00768690
    void MoveSwimming(int time);                        // 0x007692b0
    void MoveDying(int time);                           // 0x00769d80 (C_HumanMovement::m_unknown1AC != 1)
    void MoveDeadBody(int time);                        // 0x0076a770
    // The sweep of a ground move: the stance spheres moved by m_move (the cheap ground ray of
    // MOVE_FLAG_GROUND_RAY otherwise); `groundBelow` gets the ray down from the lowest sphere
    // (only tested every m_groundTestTime), `offset` the body's position from the first
    // sphere, `lift` raises the spheres.
    void SweepBody(S_MoveContacts &contacts, S_CollisionVolume **hit, bool *groundBelow, S_vector *offset,
                   float lift);                         // 0x007664b0
    // Slows the walk up and down slopes: `flatness` the horizontal share of the slope (1:
    // flat), `climb` the climbing speed (m/s), `facing` the move's dot with the slope;
    // `pos` is pulled back towards `from` by the slowdown (m_slopeSlowdown).
    void SlowOnSlope(float flatness, float climb, float facing, S_vector4 *pos, const S_vector4 *from, int time);   // 0x00767370
    // The slope of the ground of a move (SlowOnSlope; the slide factor of steep ground).
    void AdjustToGround(S_MoveContacts &contacts, const S_GroundContact &ground, const S_vector4 &from,
                        int time);                      // 0x00767620
    // Falling: the fall speed, the fall damage, the falling animations by the height of the
    // fall (`ground` touched; `standing` on it; `moved`; `slide`: may slide down).
    void UpdateFall(S_CollisionVolume *ground, bool standing, int time, bool moved, bool slide);   // 0x00767990
    // Pitches the body (swimming) towards `dir`.
    void PitchTowards(const S_vector4 &dir, int time);  // 0x00769950
    // A dead body over an edge: the way it slides off (`dir`, 0.25 m long); false: none.
    bool GetSlideOff(S_vector4 *dir);                   // 0x0076a2a0
    // Into the water (the swimming animation; the hands item put away).
    void StartSwimming();                               // 0x0076b0e0
    // The turn in place is allowed (not climbing with a ladder, not sitting).
    bool IsTurnAllowed() const;                         // 0x0076d3f0
    // A dying / dead body lies along the ground below it (pitch and the joints' heights).
    void AlignDeadBody(int time);                       // 0x0076e380
    // The pitch of the body: back to level, or along the ladder in use.
    void UpdatePitch(int time);                         // 0x00770020
    // The swimming sound: started (head under water or not) / kept up and faded.
    void StartSwimSound();                              // 0x00770a40
    void UpdateSwimSound(int time);                     // 0x00770bd0
    // The fall damage from m_fallStartY (`landed`: on the ground; otherwise only when
    // falling into deep water or the fall was long).
    void ApplyFallDamage(bool landed);                  // 0x00771090

public:
    S_vector4 m_unknown000 = {};                        // +0x000 (chunk 1; not used here)
    S_vector4 m_move = {};                              // +0x010 of this update (from the mixer)
    // The direction to turn to (x / z; x MOVE_NO_VALUE: none).
    S_vector4 m_turnTarget = {MOVE_NO_VALUE, 0.0f, 0.0f, 0.0f};   // +0x020
    S_vector4 m_unknown030 = {};                        // +0x030 (chunk 0x0F; not used here)
    C_HumanMovement *m_movement = nullptr;              // +0x040
    C_Human *m_owner = nullptr;                         // +0x044
    C_HumanSpecialMoves *m_specialMoves = nullptr;      // +0x048
    C_HumanAnimPlayer *m_animPlayer = nullptr;          // +0x04C
    // The owner's direction / position was changed this update (normalized / volumes moved).
    bool m_directionChanged = false;                    // +0x050
    bool m_positionChanged = false;                     // +0x051
    float m_slopeSlowdown = 0.0f;                       // +0x054 0..1 (SlowOnSlope)
    float m_unknown058 = 1.0f;                          // +0x058 (chunk 0x23; not used here)
    // A fall: the ground below (MOVE_NO_VALUE: none) and how high above it the body started
    // (0 low, 1 medium, 2 high: the falling animations; MOVE_NO_TIME: none).
    float m_fallGround = MOVE_NO_VALUE;                 // +0x05C
    int m_fallHeight = MOVE_NO_TIME;                    // +0x060
    // Pushed away from an edge: m_push (m/s) for m_pushTime ms (MOVE_NO_TIME: none).
    int m_pushTime = MOVE_NO_TIME;                      // +0x064
    int m_groundTestTime = 250;                         // +0x068 ms to the next ground ray of SweepBody
    uint8_t m_unknown06C[4] = {};
    S_vector4 m_push = {};                              // +0x070
    // At an edge: ms and the height there.
    int m_edgeTime = 0;                                 // +0x080
    float m_edgeHeight = MOVE_NO_VALUE;                 // +0x084
    bool m_turning = false;                             // +0x088 a turn is under way
    float m_turnSpeed = 0.015f;                         // +0x08C rad/ms of the steady turn
    int m_unknown090 = 0;                               // +0x090
    int m_waterTestTime = 10000;                        // +0x094 ms since the last water test
    bool m_inWater = false;                             // +0x098 the last water test found swimming depth
    int m_deadSlideTime = 0;                            // +0x09C ms to the next slide test of a dead body
    // The direction of the frame at the last volumes update.
    S_vector4 m_volumesDir = {};                        // +0x0A0
    S_vector4 m_velocity = {};                          // +0x0B0 m/s (from the animations)
    S_vector4 m_unknown0C0 = {};                        // +0x0C0 (chunk 0x20; not used here)
    // A shift of the body by the stance tests (C_HumanSpecialMoves::HasRoomForStance): its
    // time (MOVE_NO_TIME: none) and the stance of the volumes meanwhile.
    int m_shiftTime = MOVE_NO_TIME;                     // +0x0D0
    int m_shiftStance = 0;                              // +0x0D4
    int m_volumesTime = 0;                              // +0x0D8 ms to the next volumes update
    int m_unknown0DC = 0;                               // +0x0DC
    int m_unknown0E0 = 0;                               // +0x0E0
    bool m_unknown0E4 = false;                          // +0x0E4
    int m_unknown0E8 = 1;                               // +0x0E8 (3 while climbing)
    // > 0: the body is swept with its spheres even where only rays would do (counts down).
    int m_unknown0EC = 0;                               // +0x0EC
    int m_unknown0F0 = 0;                               // +0x0F0 (cleared each update)
    // Falling: the speed (m/s; 9.81 adds 3.27 per 85 ms step), ms into the step, the height
    // the fall started at (MOVE_NO_VALUE: none; 100000: no fall damage).
    float m_fallSpeed = 0.0f;                           // +0x0F4
    float m_fallStepTime = 0.0f;                        // +0x0F8
    float m_fallStartY = MOVE_NO_VALUE;                 // +0x0FC
    // A jump (anims 0x12, 0x17, 0x18): ms and whether backwards.
    int m_jumpTime = 0;                                 // +0x100
    bool m_jumpBack = false;                            // +0x104
    uint8_t m_unknown105[3] = {};
    // Water: from the surface to the ground (C_HumanSpecialMoves::UpdateWater; 10000: no
    // ground) and from the feet up to the surface.
    float m_waterDepth = MOVE_NO_VALUE;                 // +0x108
    float m_waterHeight = MOVE_NO_VALUE;                // +0x10C
    // Getting into the water (> 0, counts down) or out of it (< 0, counts up); the work takes
    // no animation or stance while it is < 0.
    int m_waterTransition = 0;                          // +0x110
    float m_waterSurface = MOVE_NO_VALUE;               // +0x114 water surface at the head, none: MOVE_NO_VALUE
    float m_waterLevel = 0.0f;                          // +0x118 C_CollisionWorld::GetWaterHeightRange
    // The swimming sound: the sound, its fade (ms; < 0 fading out; MOVE_NO_TIME: none), ms left
    // of it and its kind (C_MovementSounds; 1: head under water).
    I3D_sound *m_swimSound = nullptr;                   // +0x11C
    int m_swimSoundFade = MOVE_NO_TIME;                 // +0x120
    int m_swimSoundLeft = 0;                            // +0x124
    int m_swimSoundKind = 0x14;                         // +0x128
    bool m_swimBack = false;                            // +0x12C swimming backwards
    uint8_t m_unknown12D[3] = {};
    // ms the body turns at the steady turn speed (no turn in place; MOVE_NO_TIME: none).
    int m_steadyTurnTime = MOVE_NO_TIME;                // +0x130
    uint8_t m_unknown134[0xC] = {};
};

// Plays the body animations (C_HumanMovement +0xC00; 0xB0 bytes; 0x00777d10..0x0077e1e0;
// human_anim_player.cpp): each update it takes the movement's next animation state (what the
// work asked for), queues or starts the body animation, the carry animation and the activity on
// the mixer's tracks 0..2 from the animation tables, and handles what starting them means for
// the body (climbing, doors, lying down, drawing and putting away the hands item, the
// activities' props); the special animations (PlayAnimation) go on track 3.
class C_HumanAnimPlayer {
public:
    void Init(C_HumanMovement *movement, C_HumanSpecialMoves *specialMoves, C_HumanLocomotion *locomotion,
              C_Human *owner);                          // 0x00777ee0
    void Reset();                                       // 0x00777ea0
    bool SaveGame(C_chunk &chunk);                      // 0x0077d8c0
    bool LoadGame(C_chunk &chunk);                      // 0x0077dc90
    void Update();                                      // 0x00779f90
    // The head dummy "HD" of the owner's model (after a model change).
    void FindHeadDummy();                               // 0x0077d870
    // Plays the special animation `name` on the mixer's track 3 (blending in for `blendTime` ms;
    // faded out `fadeOut` ms before its end unless `loop`); an empty name or a human that
    // cannot move fades it out.
    void PlayAnimation(const char *name, int blendTime, int fadeOut, int loop);   // 0x0077d270

    C_HumanMovement *m_movement = nullptr;              // +0x00
    C_HumanSpecialMoves *m_specialMoves = nullptr;      // +0x04
    C_HumanLocomotion *m_locomotion = nullptr;          // +0x08
    C_Human *m_owner = nullptr;                         // +0x0C
    // Requests that wait until their track has played `delay` ms (MOVE_NO_TIME: none): what to
    // play then, the state of the wait (1, 0, -1) and the work's blocked requests meanwhile.
    S_HumanAnimRef m_pendingAnim;                       // +0x10
    int m_animDelay = MOVE_NO_TIME;                     // +0x20
    int m_animDelayState = 0;                           // +0x24
    uint32_t m_animBlocked = 0x20000;                   // +0x28
    int m_pendingCarry[3] = {};                         // +0x2C
    int m_carryDelay = MOVE_NO_TIME;                    // +0x38
    int m_carryDelayState = 0;                          // +0x3C
    uint32_t m_carryBlocked = 0x20000;                  // +0x40
    int m_pendingActivity[2] = {};                      // +0x44
    int m_activityDelay = MOVE_NO_TIME;                 // +0x4C
    int m_activityDelayState = 0;                       // +0x50
    uint32_t m_activityBlocked = 0x20000;               // +0x54
    int m_unknown58 = MOVE_NO_TIME;                     // +0x58 counts down (then activity 0x12)
    int m_unknown5C = 0;                                // +0x5C
    int m_unknown60 = 0;                                // +0x60
    uint8_t m_unknown64[0xC] = {};
    S_vector4 m_unknown70 = {};                         // +0x70
    I3D_frame *m_headDummy = nullptr;                   // +0x80 "HD"
    uint8_t m_unknown84[0xC] = {};
    S_vector4 m_unknown90 = {};                         // +0x90
    // A share of the delay of the body animations 0xA2..0xA5 to hold track 0 at
    // (MOVE_NO_VALUE: none; one use).
    float m_unknownA0 = MOVE_NO_VALUE;                  // +0xA0
    uint8_t m_unknownA4[0xC] = {};

private:
    // The tracks: the body animation, the carry animation, the activity.
    void UpdateAnimTrack();                             // 0x00777f30
    void UpdateCarryTrack();                            // 0x00778f80 (DS 0x007e4c30)
    void UpdateActivityTrack();                         // 0x00779850 (DS 0x007e56f0)
    // What starting an animation means for the body; sets the request that follows it.
    void OnAnimStarted(const S_HumanAnimRef &anim, const S_HumanAnimRef &old, bool singleStage);   // 0x0077a2c0
    void OnCarryStarted(const int *carry, const int *old);   // 0x0077bcd0 (DS 0x007e8490)
    void OnActivityStarted(const int *activity, const int *old);   // 0x0077c5a0 (DS 0x007e8e70)
    // Turn the requests into what the hands item and the carrying state allow.
    void AdjustAnimRequest(const S_HumanAnimRef &current, S_HumanAnimRef *next);   // 0x0077caf0
    void AdjustCarryRequest(const S_HumanAnimRef &current, const int *activity);   // 0x0077cd30
    void AdjustActivityRequest(const S_HumanAnimRef &current, const int *carry, int *next);   // 0x0077d190
    // Back from the aiming stances (0x36 / 0x3E / 0x41) to standing / crouching / prone.
    void ReturnToStand();                               // 0x0077d800
};

// The idle behaviour of the body and its interactions with the world (C_HumanMovement +0xCB0;
// 0x20 bytes; 0x00750b40..0x00758020; human_idle.cpp): the standing fidgets of the player's
// soldiers, the breath vapour of cold missions, the swimming bob, and the interactions
// (E_Interaction: doors, vehicle seats, chairs, healing, tying up, cutting, backstabs) with the
// seat state machine that follows them (UpdateInteraction).
class C_HumanIdle {
public:
    void Reset();                                       // 0x00750b40
    void Done() {}                                      // 0x00750b80
    bool SaveGame(C_chunk &chunk);                      // 0x007513f0
    bool LoadGame(C_chunk &chunk);                      // 0x007515c0
    void Update(int time);                              // 0x00750c00 (DS 0x007b4bf0)
    // Advances the interaction under way (C_HumanMovement::m_interaction): into and out of the
    // seats and chairs, the doors' end.
    void UpdateInteraction(int time);                   // 0x00755960 (DS 0x007ba9a0)
    // Starts an interaction with `target` (the original returns true).
    void Interact(C_Actor *target, uint32_t interaction, int arg);   // 0x007518e0
    // Saves / loads the unit's statics (nothing; the original returns true).
    static bool SaveStatics(C_chunk &chunk);            // 0x00750ae0 (DS 0x007b4a00)
    static bool LoadStatics(C_chunk &chunk);            // 0x00750af0 (DS 0x007b4a20)

    C_HumanAnimPlayer *m_animPlayer = nullptr;          // +0x00
    C_HumanMovement *m_movement = nullptr;              // +0x04
    C_HumanLocomotion *m_locomotion = nullptr;          // +0x08
    C_Human *m_owner = nullptr;                         // +0x0C
    int m_standTime = 0;                                // +0x10 ms standing still (0: not standing)
    int m_fidgetTime = MOVE_NO_TIME;                    // +0x14 ms until the next fidget
    int m_vapourTime = 0;                               // +0x18 ms until the next breath vapour
    uint8_t m_unknown1C[4] = {};
};

// Light level of the body from the lights of its sector (C_HumanVisual +0xD4; 0x1C bytes;
// 0x0078a0e0..0x0078a520).
class C_HumanLighting {
public:
    void Init();                                        // 0x0078a100
    // The level the model shows, at most 1.
    float GetLevel() const { return m_level < 1.0f ? m_level : 1.0f; }   // 0x0078a0e0
    // Counts `time` and, every `mode`'s interval, marks the level for a new sample (a model
    // that was not drawn waits, except in mode 0); eases the shown level and passes it to the
    // model.
    void Update(I3D_frame *model, int time, int mode);  // 0x0078a130
    // Measures the level at `pos` in `sector` (once marked by Update; the model is hidden for
    // the measure) and passes it to the model. Unless `always`, the human's controller may
    // give the values instead.
    void Sample(I3D_sector *sector, I3D_frame *model, const S_vector &pos, bool always);   // 0x0078a210
    // Moves the shown level toward GetLevel() by 2 per second.
    void Approach(int time);                            // 0x0078a3c0
    // Takes the scale of the sector's ambient light (its first lit light of type 4).
    void SetSector(I3D_sector *sector);                 // 0x0078a420

    float m_intensity = 1.0f;                           // +0x00 light intensity at the body
    float m_level = 1.0f;                               // +0x04 relative to the reference
    float m_shownLevel = 1.0f;                          // +0x08 eased toward the level
    int m_time = 0;                                     // +0x0C ms since the last sample
    bool m_sampleDue = true;                            // +0x10
    float m_ambient = 1.0f;                             // +0x14 the sector's ambient scale
    float m_climate = 0.0f;                             // +0x18 raises dark levels (snow missions)
};

// Where the model is shown and lit (C_HumanMovement +0xCD0; 0xF0 bytes; 0x00758080..0x0075b0c0;
// human_visual.cpp): the model of a human moved by the network glides from the position it shows
// to the latest one received (and on along the last step until a new one comes); the sector the
// model is in, its light level and shadow; and the animation events of the stages, which drive
// the footsteps, the equipment models, doors and bodies picked up.
class C_HumanVisual {
public:
    void Init();                                        // 0x00758270
    void Done();                                        // 0x007583b0
    void Reset();                                       // 0x0075b0c0
    bool SaveGame(C_chunk &chunk);                      // 0x0075a840
    bool LoadGame(C_chunk &chunk);                      // 0x0075ab60
    // The model follows the network positions: always when another machine simulates the
    // human, else as `on` says.
    void SetNetworkDriven(bool on);                     // 0x007583c0
    // The weapon is drawn (`on`: carry animation 0x1F) or the drawing / holstering ends.
    void Unknown7585d0(bool on);                        // 0x007585d0
    // Glides the model toward the network position.
    void UpdatePosition(int time);                      // 0x00758c50
    // Animation events, sector, light level, carried body, delayed hands item, morphs.
    void Update(int time);                              // 0x00758c90 (DS 0x007be900)
    // Rebuilds / removes the model's shadow (C_HumanMovement::m_shadow).
    void RebuildShadow();                               // 0x00759050
    void RemoveShadow();                                // 0x007590c0
    // A position / direction of the human as the network (or the simulation) says.
    void SetNetworkPosition(const S_vector4 &pos);      // 0x00759e50
    void SetNetworkDirection(const S_vector4 &dir);     // 0x00759fe0
    // Puts the model there at once (with its volumes) / turns it (remote humans; on a ladder
    // facing the ladder).
    void SetPosition(const S_vector4 &pos);             // 0x0075a280
    void SetDirection(const S_vector4 &dir, float roll);   // 0x0075a520 (DS 0x007c0230)

    C_HumanMovement *m_movement = nullptr;              // +0x00
    C_HumanLocomotion *m_locomotion = nullptr;          // +0x04
    C_Human *m_owner = nullptr;                         // +0x08
    int m_unknown0C = 0;                                // +0x0C the owner's group bit 4
    bool m_initialized = false;                         // +0x10
    I3D_sector *m_sector = nullptr;                     // +0x14
    int m_sectorTime = 0;                               // +0x18 ms since the sector was looked up
    int m_showHandsTime = -1;                           // +0x1C ms until the hands item is shown again
    int m_glideTime = 0;                                // +0x20 of m_glideDuration
    int m_glideDuration = -1;                           // +0x24 (100: one network step)
    uint8_t m_unknown28[8] = {};
    S_vector4 m_previousTarget;                         // +0x30 the network position before
    S_vector4 m_unknown40;                              // +0x40 (only saved)
    S_vector4 m_target;                                 // +0x50 the latest network position
    bool m_newTarget = false;                           // +0x60
    uint8_t m_unknown61[0x0F] = {};
    S_vector4 m_unknown70;                              // +0x70 (only saved; cleared)
    S_vector4 m_glideFrom;                              // +0x80 the model's position at the start
    uint8_t m_unknown90[0x10] = {};
    S_vector4 m_glideTo;                                // +0xA0
    S_vector4 m_step;                                   // +0xB0 the last network step
    float m_unknownC0 = 0.0f;                           // +0xC0 the model's z at the reset (only saved)
    uint32_t m_flags = 0;                               // +0xC4 VISUAL_FLAG_*
    bool m_glideDone = false;                           // +0xC8 stopped at the target
    uint8_t m_unknownC9[3] = {};
    int m_moveSoundTime = 0;                            // +0xCC ms since the last movement sound
    bool m_networkDriven = false;                       // +0xD0
    uint8_t m_unknownD1[3] = {};
    C_HumanLighting m_lighting;                         // +0xD4

private:
    void ResetState();                                  // 0x00758080
    void InitModel();                                   // 0x00758220
    void DoneModel();                                   // 0x00758250
    void UpdateStandingFlag();                          // 0x00758410
    // A new glide toward the target (`restart`) or on along the last step.
    void SetTarget(const S_vector4 &pos, bool restart);   // 0x007586f0
    void Glide(int time);                               // 0x00758a20
    void ProcessAnimEvents();                           // 0x00759180 (DS 0x007beed0)
};

// C_HumanVisual::m_flags.
enum : uint32_t {
    VISUAL_FLAG_STANDING = 0x1,                         // the animation does not move the body: no gliding on
    VISUAL_FLAG_2 = 0x2,
};

// ------------------------------------------------------------------ the movement

class alignas(16) C_HumanMovement {
public:
    C_HumanMovement();                                  // 0x00785a20 (DS 0x007f43b0)
    ~C_HumanMovement();                                 // 0x00785db0
    C_HumanMovement(const C_HumanMovement &) = delete;
    C_HumanMovement &operator=(const C_HumanMovement &) = delete;

    void SetOwner(C_Human *owner);                      // 0x00785f80 (DS 0x007f4ab0)
    void Init();                                        // 0x00786040 (DS 0x007f4b80)
    void Done();                                        // 0x00786490 (DS 0x007f50f0)
    void Reset();                                       // 0x00784db0 (DS 0x007f3770)
    bool SaveGame(C_chunk &chunk);                      // 0x00784430 (DS 0x007f2bc0)
    bool LoadGame(C_chunk &chunk);                      // 0x00784770 (DS 0x007f2fb0)
    // Re-synchronises the movement with the actor's frame after a teleport.
    void ResetToFrame();                                // 0x0041d800
    // Animation of the model (also on clients) and its physics (server).
    void UpdateAnimation(int time);                     // 0x007876d0 (DS 0x007f63f0)
    void UpdatePhysics(int time);                       // 0x007881b0 (DS 0x007f6f30)
    void SetPlayerControlled(bool on);                  // 0x00787530
    // Re-files the hit volumes (`volumes`) and refreshes the sector and the light level (`light`).
    void Ragdoll(bool volumes, bool light);             // 0x007872b0 (DS 0x007f6290)
    void Unknown7865c0(int arg);                        // 0x007865c0 (passes `arg` on)
    // Uses `ladder` (remote humans that must stand up first queue the action).
    int UseObject(C_Actor *ladder);                     // 0x00786600
    void SetUnknown94(float value);                     // 0x00786ab0
    // Replays an action received from the network (E_RemoteRequest).
    void OnRemoteAction(int request);                   // 0x00786b50
    // Rebuilds the joints, collision volumes and parts after the model changed (dress / undress).
    void OnModelChanged();                              // 0x00787080
    // Lets the equipment rename the models for the shadow refresh.
    void UpdateModelNames();                            // 0x00786b30
    // The camera is under water (set by the camera code at 0x0046c340; the swimming sounds of
    // the humans are heard by it).
    static void SetCameraUnderWater(bool on);           // 0x00786b20 (DS 0x007f5910) __fastcall
    static bool IsCameraUnderWater();
    // Plays dubbing number `dubbing` at `frame`, lip-synced and with its subtitle
    // (GetSoundModule()->PlayDubbing; 0 ends the frame's speech).
    static void PlayDubbing(int dubbing, I3D_frame *frame, C_Human *owner, float nearRange, float farRange);   // 0x00786a70 __fastcall

    uint32_t GetFlags() const { return m_flags; }
    void OrFlags(uint32_t flags) { m_flags |= flags; }
    void ClearFlags(uint32_t flags);                    // 0x0075cbd0
    void OnFrozen(bool on);                             // 0x0075c730
    // Takes the volumes out of the collision world (`remove`) or only makes the queries
    // ignore them (`remove` false); RestoreCollision undoes it.
    void RemoveCollision(bool remove);                  // 0x0075cdc0
    void RestoreCollision(bool remove);                 // 0x0075cf20
    // The left foot is no farther than the right one from where the body will be in 2 s
    // (the compass' needle wobbles at each change).
    bool IsLeftFootForward() const;                     // 0x0075ea70
    float GetVisibilityFactor() const { return m_visual.m_lighting.m_intensity; }
    // Light level of the body (at most 1) that the attached item models take over.
    float GetLightLevel() const { return m_visual.m_lighting.GetLevel(); }   // 0x0078a0e0
    C_HumanWeaponPose *GetWeaponPose() const { return m_weaponPose; }
    // How the hands item is carried for the animations (C_ItemDef::GetCarryMode) and the
    // carrying state (0 none, 3 in the hands).
    void SetHandsCarry(int carryMode, int state)
    {
        m_request.carryMode = carryMode;
        m_handsCarryState = state;
    }
    // The carry animation waits until the carry track has played `delay` ms.
    void SetCarryDelay(int delay, int state)
    {
        m_animPlayer.m_carryDelay = delay;
        m_animPlayer.m_carryDelayState = state;
    }
    void SetUnknownDC8(int value) { m_unknownDC8 = value; }
    // > 0: in a vehicle seat (1..8; inferred: the item models are not shown).
    int GetSeatMode() const { return m_seatMode; }
    // 1: carrying a body (the equipment's place 9 model).
    int GetUnknownA30() const { return m_unknownA30; }
    int GetUnknown1E8() const { return m_animState.carryAnim; }
    void FadeOutMixerTrack(int track, int time) { m_mixer.FadeOutTrack(track, time); }
    void SetUnknown610_64(int value) { m_morph.SetUnknown64(value); }
    void SetUnknown610_78(int value) { m_morph.SetUnknown78(value); }
    void RefreshAttachedModels() { m_visual.RebuildShadow(); }
    void Unknown7585d0(bool on) { m_visual.Unknown7585d0(on); }
    I3D_frame *GetSectorFrame() const;
    const S_vector &GetVelocity() const { return reinterpret_cast<const S_vector &>(m_locomotion.m_velocity); }
    // Detached from the world (DetachFromWorld / restarts).
    bool IsDetached() const { return m_detached; }
    void SetDetachedFlag(bool on) { m_detached = on; }
    void ResetPushState()
    {
        m_unknown1AC = 0;
        m_unknown1B0 = 0;
        m_unknown18C = true;
        m_morph.ShowDeathFace();
    }
    void SetUnknown18C(bool on) { m_unknown18C = on; }
    bool IsRespawnBlocked() const { return m_unknown1AC != 0 || m_unknown9C0 != 0; }
    int GetState() const { return m_animState.anim.anim; }
    int GetUnknown990() const { return static_cast<int>(reinterpret_cast<intptr_t>(m_useObject)); }
    int GetUnknown698() const { return m_stanceVolumeCount; }
    I3D_frame *GetUnknown6E0() { return reinterpret_cast<I3D_frame *>(m_stanceSpheres); }
    const uint32_t *GetGround() const { return reinterpret_cast<const uint32_t *>(m_ground); }
    // Turns the shadow on or off (none: nothing).
    void SetModelVisible(bool on);
    void Interact(C_Actor *target, uint32_t interaction, int arg) { m_idle.Interact(target, interaction, arg); }
    void PlayAnimation(const char *name, int a, int b, int c) { m_animPlayer.PlayAnimation(name, a, b, c); }
    I3D_frame *GetShadowFrame() const { return m_shadow; }
    void RemoveShadow() { m_visual.RemoveShadow(); }
    I3D_sector *GetSector() const { return m_visual.m_sector; }
    C_Human *GetOwner() const { return m_owner; }
    // Length (ms) of an animation of the owner's group table; -1 without one.
    int GetAnimLength(int anim, int stance, int sub) const;   // 0x0075d000

    // ---- the base part (0x0075b260..0x00763650; human_movement_base.cpp) ----
    void SetOwnerBase(C_Human *owner, C_HumanWeaponPose *weaponPose);   // 0x0075b260
    // Initial values (also the dummies' positions, the shot dummies and the command record).
    void ResetBase();                                   // 0x0075b2a0
    // Finds the dummies and joints of the model, makes the collision volumes and puts the
    // stance (stand) and hit spheres into the collision world.
    void InitBase();                                    // 0x0075b6e0
    void DoneBase();                                    // 0x0075bb20
    // The hit spheres around the "shot1" / "shot2" dummies (a dead body's are smaller and
    // moved apart along the body).
    void UpdateHitSpheres();                            // 0x0075bcd0
    // Puts the hit spheres at `pos` plus their offsets.
    void MoveHitSpheres(const S_vector &pos);           // 0x0075c050
    // The stance spheres of `stance` (or of a stance change under way) into the collision
    // world; adds / removes the volumes whose count changed.
    void UpdateStanceVolumes(int stance);               // 0x0075c0e0
    // Collision spheres of a stance's dummies into `out`; returns how many there are.
    int GetStanceSpheres(int stance, S_vector4 *out) const;   // 0x0075c450
    // World positions, offsets and radii of the stance dummies (all / the lying ones).
    void UpdateDummies();                               // 0x0075c4d0
    void UpdateLieDummies();                            // 0x0075c5c0
    // Puts the dummies' positions at `pos` plus their offsets.
    void MoveDummies(const S_vector &pos);              // 0x0075c6b0
    // Puts the body back on the ground under it after a change of the movement flags.
    void SnapToGround();                                // 0x0075c950
    // The work modes of using things: 0 a ladder / window (work mode 1), 1 / 2 lying down
    // (work modes 2 / 3, animations 0x22 / 0x23), 3 (work mode 4), 4 swimming (work mode 5).
    void SetSeatMode(int mode, bool on);                // 0x0075d1b0
    // Takes the dummies, joints and volume frames of `model` (after a model change); the mixer
    // takes over the stages of `previous` (RelinkJoints passes the same model).
    void RelinkModel(I3D_frame *model, I3D_frame *previous);   // 0x0075d4e0
    void RelinkJoints();                                // 0x0075d620
    // A footstep of the left (true) or right foot: the ground under it, the step sound (and
    // the AI's hearing of it), dust particles on remote humans and the footprint.
    void Footstep(bool left);                           // 0x0075d670
    // Starts / ends carrying `body` (the equipment's place 9 model; null: only this side).
    void OnPickUpBody(C_Human *body);                   // 0x0075e3d0 (DS 0x007c40a0)
    void OnDropBody(C_Human *body);                     // 0x0075e6f0 (DS 0x007c4480)
    // m_unknown1AC (and m_unknown1B0 with `both`); 0 stops the physics of the body.
    void SetPushState(int state, bool both);            // 0x0075e6b0
    // Moves the model and the human to `pos` (the hit volumes back to full size).
    void SetPosition(const S_vector4 &pos);             // 0x0075e9c0 (DS 0x007c47e0)
    // A movement sound (E_MovementSound 4..19) at the mouth of the local player's active human.
    void PlaySound(int group);                          // 0x0075ebd0
    // Takes back the finished movement sounds (all of them with `stopAll` or when the human is
    // not the active player).
    void UpdateSounds(bool stopAll);                    // 0x0075ef00
    // Breath under water: gasps after 7.5 s, drowning after 30 s (an aqualung on the back
    // helps).
    void UpdateBreath(int time, bool underWater);       // 0x0075f040 (DS 0x007c4a80)
    // The sound of using m_useObject (a ladder's) at the pelvis.
    void StartUseSound();                               // 0x0075f240
    void StopUseSound();                                // 0x0075f310
    // Damage of a dangerous ground material during `time` ms (fire, barbed wire, mines...).
    void DamageByMaterial(int material, int time);      // 0x0075f340
    // DamageByMaterial for the materials touched (`contacts`: the slide contacts of the
    // collision world, each material once in a row; else `volume`'s).
    void DamageByContacts(const S_MoveContacts *contacts, const S_CollisionVolume *volume, int time);   // 0x0075f5e0
    // Replays the front action of m_remoteActions (remote humans).
    void ProcessRemoteActions(int time);                // 0x0075f690
    bool SaveBase(C_chunk &chunk);                      // 0x00760920
    bool LoadBase(C_chunk &chunk);                      // 0x00761af0
    // Back to the initial state (mission restart): sounds, morphs, footprints, volumes.
    void RestartBase();                                 // 0x00763650 (DS 0x007ca260)
    // The morph values 1..3 (C_HumanMorph::SetUnknown1..3).
    void SetMorph(int which, uint32_t value);           // 0x0075d160
    void AddBloodPool(const S_vector &pos, const S_vector &normal, int unused);   // 0x00787570 (callers pass 0)
    void UpdatePhysicsParts(int time);                  // 0x00788630 (DS 0x007f7470)
    // The body is lowered: lying, seated, carried (the work's mode 2 / 3 or such an animation).
    bool IsLowered() const;                             // 0x00784d60
    bool GetHeadWater(bool *underWater, float *surface) const;   // 0x00786510
    void AfterLoad();                                   // (the end of LoadGame)
    void UpdatePhysicsRest(int time);                   // (inline in UpdatePhysics / UpdatePhysicsParts)

    // ---- data (offsets of the original) ----
    I3D_frame *m_shotDummies[2] = {};                   // +0x000 "shot1", "shot2"
    I3D_frame *m_stanceDummies[STANCE_DUMMY_COUNT] = {};   // +0x008 E_StanceDummy
    uint8_t m_unknown02C[4] = {};
    S_vector4 m_dummyPos[STANCE_DUMMY_COUNT];           // +0x030 world positions
    float m_dummyRadius[STANCE_DUMMY_COUNT] = {};       // +0x0C0 half the width of their boxes
    uint8_t m_unknown0E4[0xC] = {};
    S_vector4 m_dummyOffset[STANCE_DUMMY_COUNT];        // +0x0F0 from the model's position
    int m_unknown180 = 0;                               // +0x180
    int m_bloodZone = 0;                                // +0x184 where the wound bleeds (5: none; 0: larger pools)
    I3D_frame *m_bloodFrame = nullptr;                  // +0x188 the frame of the wound
    bool m_unknown18C = true;                           // +0x18C
    int m_unknown190 = 0;                               // +0x190
    std::vector<I3D_sound *> m_sounds;                  // +0x194 playing movement sounds (local player)
    int m_breathTime = 0;                               // +0x1A4 ms under water
    int m_gaspTime = 0;                                 // +0x1A8 ms to the next gasp
    int m_unknown1AC = 0;                               // +0x1AC
    int m_unknown1B0 = 0;                               // +0x1B0
    bool m_carried = false;                             // +0x1B4 being carried
    C_HumanWeaponPose *m_weaponPose2 = nullptr;         // +0x1B8 = m_weaponPose
    int m_unknown1BC = 0;                               // +0x1BC
    int m_animProfileTime = 0;                          // +0x1C0 us spent in UpdateAnimation
    int m_physicsProfileTime = 0;                       // +0x1C4 us spent in UpdatePhysics
    float m_stanceVolumeScale = 0.8f;                   // +0x1C8 radius scale of the stance spheres
    float m_hitVolumeScale[2] = {1.0f, 1.0f};           // +0x1CC radius scale of the hit spheres
    bool m_inUpdate = false;                            // +0x1D4
    S_HumanAnimState m_animState;                       // +0x1D8
    S_HumanAnimState m_nextAnimState;                   // +0x1FC
    int m_unknown220 = 0;                               // +0x220
    bool m_unknown224 = true;                           // +0x224
    int m_stillTime = 0;                                // +0x228 ms the body has not moved (< 0: a while longer)
    uint32_t m_flags = MOVE_FLAGS_INITIAL;              // +0x22C MOVE_FLAG_*
    // Footprints of a special kind: > 0 bloody (steps left; 10 after stepping into blood),
    // < 0 wet (counts up to 0).
    int m_footstepSurface = 0;                          // +0x230
    // Phase (0..2 pi) of the bobbing while swimming in place (C_HumanIdle::Update; read by the
    // locomotion).
    float m_swimBobPhase = 0.0f;                        // +0x234
    uint8_t m_unknown238[8] = {};
    S_vector4 m_unknown240 = {MOVE_NO_VALUE, MOVE_NO_VALUE, MOVE_NO_VALUE, 0.0f};   // +0x240
    float m_unknown250 = 0.0f;                          // +0x250
    float m_unknown254 = 0.0f;                          // +0x254
    C_Human *m_owner = nullptr;                         // +0x258
    uint8_t m_unknown25C[4] = {};
    C_HumanAnimMixer m_mixer;                           // +0x260
    C_HumanMorph m_morph;                               // +0x610
    int m_stanceVolumeCount = 0;                        // +0x698 stance spheres in the collision world
    int m_hitVolumeCount = 0;                           // +0x69C hit spheres in the collision world
    S_vector4 m_hitSpheres[2];                          // +0x6A0 center, radius
    S_vector4 m_hitOffsets[2];                          // +0x6C0 from the model's position
    S_vector4 m_stanceSpheres[4];                       // +0x6E0 center, radius
    S_CollisionVolume *m_stanceVolumes[4] = {};         // +0x720
    S_CollisionVolume *m_hitVolumes[2] = {};            // +0x730
    uint8_t m_unknown738[8] = {};
    S_vector4 m_unknown740;                             // +0x740 offset of the third stand sphere at the start
    int m_groundMaterial = -1;                          // +0x750 -1: test under the foot
    S_CollisionVolume *m_ground = nullptr;              // +0x754 under the last footstep
    // Down steep ground: how much the cheap ground ray mode slides the move (0: none, else
    // 0.5 at least; C_HumanLocomotion::AdjustToGround).
    float m_slideFactor = 0.0f;                         // +0x758
    int m_damageMaterial = -1;                          // +0x75C
    bool m_unknown760 = false;                          // +0x760
    bool m_onDynamicGround = false;                     // +0x761 standing on a dynamic volume (a vehicle, ...)
    bool m_onGround = false;                            // +0x762 the last move found a floor (not only walls)
    int m_unknown764 = -1;                              // +0x764
    uint8_t m_unknown768[8] = {};
    // What the work asks for (its stance: the one to return to after using an object).
    S_MoveRequest m_request;                            // +0x770
    S_HumanJoint m_joints[HUMAN_JOINT_COUNT];           // +0x810
    uint8_t m_unknown93C[4] = {};
    S_vector4 m_unknown940;                             // +0x940
    int m_unknown950 = 0;                               // +0x950 counts down
    int m_unknown954 = 0;                               // +0x954
    float m_unknown958 = 0.0f;                          // +0x958 (climbing: heights, inferred)
    float m_unknown95C = 0.0f;                          // +0x95C
    float m_unknown960 = 0.0f;                          // +0x960 ms (scaled: the move clock) the climb starts rising
    float m_unknown964 = 0.0f;                          // +0x964 ms it stops (floats: not truncated)
    float m_unknown968 = 0.0f;                          // +0x968
    bool m_unknown96C = false;                          // +0x96C
    S_CollisionVolume *m_trailVolumes[4] = {};          // +0x970
    int m_trailTime = 0;                                // +0x980
    int m_trailCount = 0;                               // +0x984
    int m_useState = MOVE_NO_TIME;                      // +0x988
    bool m_unknown98C = false;                          // +0x98C
    C_Actor *m_useObject = nullptr;                     // +0x990 the ladder being used
    int m_useSound = -1;                                // +0x994 sound module handle
    int m_unknown998 = MOVE_NO_TIME;                    // +0x998
    int m_stanceChange = MOVE_NO_TIME;                  // +0x99C
    int m_unknown9A0 = MOVE_NO_TIME;                    // +0x9A0
    int m_nextStance = 3;                               // +0x9A4 (3: none)
    int m_nextStance2 = 3;                              // +0x9A8
    int m_unknown9AC = MOVE_NO_TIME;                    // +0x9AC
    int m_unknown9B0 = MOVE_NO_TIME;                    // +0x9B0
    int m_unknown9B4 = MOVE_NO_TIME;                    // +0x9B4
    int m_unknown9B8 = MOVE_NO_TIME;                    // +0x9B8
    int m_unknown9BC = MOVE_NO_TIME;                    // +0x9BC
    int m_unknown9C0 = MOVE_NO_TIME;                    // +0x9C0
    int m_unknown9C4 = MOVE_NO_TIME;                    // +0x9C4
    bool m_unknown9C8 = false;                          // +0x9C8
    bool m_unknown9C9 = false;                          // +0x9C9
    int m_unknown9CC = MOVE_NO_TIME;                    // +0x9CC
    S_vector4 m_unknown9D0;                             // +0x9D0
    int m_unknown9E0 = 3;                               // +0x9E0
    uint8_t m_unknown9E4[0xC] = {};
    S_vector4 m_unknown9F0 = {MOVE_NO_VALUE, MOVE_NO_VALUE, MOVE_NO_VALUE, 0.0f};   // +0x9F0
    S_vector4 m_unknownA00;                             // +0xA00
    int m_unknownA10 = 0;                               // +0xA10
    int m_unknownA14 = -1;                              // +0xA14
    int m_unknownA18 = 0;                               // +0xA18
    int m_unknownA1C = MOVE_NO_TIME;                    // +0xA1C
    int m_handsCarryState = 0;                          // +0xA20
    int m_unknownA24 = 0;                               // +0xA24
    int m_unknownA28 = 0;                               // +0xA28 counts down
    int m_unknownA2C = MOVE_NO_TIME;                    // +0xA2C
    int m_unknownA30 = MOVE_NO_TIME;                    // +0xA30 1: carrying a body
    // The interaction under way (E_Interaction and INTERACT_FLAG_BODY; 0: none; INTERACT_SIT /
    // INTERACT_STAND_UP: m_seatFrame) and its timer (ms; MOVE_NO_TIME: none).
    uint32_t m_interaction = 0;                         // +0xA34
    int m_interactionTime = MOVE_NO_TIME;               // +0xA38
    C_Actor *m_vehicle = nullptr;                       // +0xA3C
    union {                                             // +0xA40
        int m_seat = 0;                                 // +0xA40 the vehicle's seat
        I3D_frame *m_seatFrame;                         // a frame of the scene to sit at
    };
    // How the human sits in the seat: the vehicle's seat flags (SEAT_FLAG_*) until it is in,
    // then 1 at the controls, 2 / 4 passenger keeping the hands item (standing / crouching or
    // lying), 3 at a seat weapon without a pose, 5 passenger, 6..8 at a seat weapon of pose
    // kind 0..2 (C_HumanIdle::UpdateInteraction).
    int m_seatMode = 0;                                 // +0xA44
    int m_seatStance = 0;                               // +0xA48 the stance asked for when getting in
    std::deque<S_RemoteAction> m_remoteActions;         // +0xA4C
    uint32_t m_clock = 0;                               // +0xA7C ms, wraps at 1000 short of 2^32
    // Periodic timers (ms; their callers subtract the period when it is over).
    uint32_t m_timer200 = 0;                            // +0xA80
    uint32_t m_timer300 = 0;                            // +0xA84
    uint32_t m_timer400 = 0;                            // +0xA88
    uint32_t m_timer600 = 0;                            // +0xA8C
    // Coming down on a parachute (the fall is damped to 5 m/s, no fall damage; set when the
    // body starts over 20 m above the ground; the landing ends it).
    bool m_parachuting = false;                         // +0xA90
    int m_footstepTime = 0;                             // +0xA94 ms since the last noisy footstep (<= 1000)
    int m_footstepCount = -1;                           // +0xA98 quiet steps in a row
    I3D_frame *m_shadow = nullptr;                      // +0xA9C GEffects shadow of the model
    C_HumanSpecialMoves m_specialMoves;                 // +0xAA0
    C_HumanLocomotion m_locomotion;                     // +0xAC0
    C_HumanAnimPlayer m_animPlayer;                     // +0xC00
    C_HumanIdle m_idle;                                 // +0xCB0
    C_HumanVisual m_visual;                             // +0xCD0
    C_HumanWeaponPose *m_weaponPose = nullptr;          // +0xDC0 (allocated)
    bool m_detached = false;                            // +0xDC4
    int m_unknownDC8 = 0;                               // +0xDC8 counts down per frame
};

// The movement sound bank (0x0098a690): 20 groups of up to 6 sound files played in a shuffled
// order, up to two cached sound frames of each file (0x00784fd0..0x00785950).
class C_MovementSounds {
public:
    static constexpr int GROUPS = 20;
    static constexpr int MAX_VARIANTS = 6;
    static constexpr int CACHED = 2;

    C_MovementSounds();                                 // 0x00784fd0
    ~C_MovementSounds();                                // 0x00785330
    C_MovementSounds(const C_MovementSounds &) = delete;
    C_MovementSounds &operator=(const C_MovementSounds &) = delete;

    // Number of files of a group (builds the shuffled orders on the first call).
    static int GetCount(int group);                     // 0x007850a0
    // A sound frame of a file (a cached one or a new one; null when it cannot be made).
    I3D_sound *GetSound(int group, int variant);        // 0x007853c0 (DS 0x007f3f60)
    // Plays the next file of the group's order at `frame` (null: not linked).
    I3D_sound *Play(int group, I3D_frame *frame, const S_vector *pos);   // 0x007856f0
    // Takes a played sound back (stops it; caches or releases it).
    void Return(I3D_sound *sound);                      // 0x00785860

private:
    struct S_Cache {                                    // 0x0C bytes
        I3D_sound *sounds[CACHED] = {};
        int count = 0;
    };
    S_Cache *m_caches[GROUPS] = {};                     // per group: one per file
};

// The brightest footprints of the mission (0.45 on the snow missions, 1 elsewhere).
extern float g_footprintBrightness;                     // 0x0084f414

// The shared bank, made with the first human and freed with the last one.
C_MovementSounds *GetMovementSounds();                  // 0x0075ee90 (inline in others)
extern C_MovementSounds *g_movementSounds;              // 0x0098a690
extern int g_movementSoundUsers;                        // 0x0098a694

// Ground material of a collision volume or primitive (-1: none or unknown).
int GetCollisionMaterial(const S_CollisionVolume *volume);   // 0x0075cda0
// What the body touched in a move (human_locomotion.cpp): physics objects are pushed, a human
// is told of the collision (the result: 1 / 2, the human ahead on the left / right of the
// move), a ladder walked into is used, a closed door opens (with `openDoors`; server), trains
// and barrels are told. `touched` gets the last actor touched (stops at a volume without
// one: 0). (__fastcall.)
int ProcessMoveContacts(C_HumanMovement *movement, S_MoveContacts *contacts, bool openDoors, C_Actor **touched);   // 0x00788ca0
// Animations that the animation player plays on one stage of the model (on two otherwise:
// a human animation, a carry animation, an activity; meaning of the split inferred). The
// original takes the whole S_HumanAnimRef / S_MixerCarry / (activity, variant) by value
// (RET 0x10 / 0xC / 8) and reads only the id.
bool IsSingleStageAnim(int anim);                       // 0x0075cc40
bool IsSingleStageCarryAnim(int carryAnim);             // 0x0075cd30
bool IsSingleStageActivity(int activity);               // 0x0075cd60
// The speeds (m/s at weight 1) of the animations with root motion by sub-variant and stance
// (sub + stance * 6): 0 forward, 1 back, 2 / 3 left / right, 4..7 the diagonals (forward left,
// forward right, back left, back right), 8 the jump 0x18, 9 the jump 0x12, 10 the crouched
// jump 0x17, 11 / 12 swimming / with diving gear (C_HumanLocomotion::UpdateVelocity).
extern const float g_moveSpeeds[13][18];                // 0x00816878
// Signed angle (radians, -pi..pi) from the direction (x, y) of `a` to that of `b`.
float AngleBetween2D(const float *a, const float *b);   // 0x00788a10
// Rotations of a vector about the world axes.
void RotateAboutY(S_vector &v, float angle);            // 0x007888f0
void RotateAboutX(S_vector4 &v, float angle);           // 0x00788950
void RotateAboutZ(S_vector4 &v, float angle);           // 0x007889b0
