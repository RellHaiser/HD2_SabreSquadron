// The body of a meleeTracker (C_MeleeTracker +0x90): a movement handler (C_MeleeTrackerMovement, vtable 0x008121c4) that
// follows the ground, slides its collision spheres through the world and drives a small
// animation mixer (C_MeleeTrackerAnimMixer, C_MeleeTrackerMovement +0x10 = C_MeleeTracker +0xA0, vtable 0x008121d0).
// Original source 0x005c0ce0..0x005c9050 (the body) and 0x005cfcf0..0x005d1f50 (save/load).
//
// The mixer blends two animation channels (body + an overlay) across the model's first four
// animation stages, advances them at a rate the body sets, and turns the root-motion velocity,
// add-moves and slides the body asks for into the move of the body. It is modelled on the
// human mixer (game/human_movement.h, C_HumanAnimMixer); the meleeTracker's is the slimmer cousin:
// 2 tracks of 2 stages, no carry/activity tracks and no first-person handling.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ls3d/i3d.h"
#include "ls3d/math.h"

class C_chunk;
class C_MeleeTracker;
struct S_CollisionVolume;
struct S_MoveContacts;

// ---------------------------------------------------------------------------
// C_MeleeTrackerAnimMixer

// Two tracks, each cross-fading over a pair of the model's stages (stages 0/1 and 2/3). The
// model's other four stages are left to tick at the plain movement time.
constexpr int MELEE_TRACKER_MIXER_TRACKS = 2;
constexpr int MELEE_TRACKER_MIXER_STAGES = 2 * MELEE_TRACKER_MIXER_TRACKS;
constexpr float MELEE_TRACKER_MIXER_NO_BLEND = -10000.0f;        // a free stage
constexpr int MELEE_TRACKER_MOVE_NO_TIME = -10000;               // 0xFFFFD8F0, "no add-move / slide"
constexpr uint32_t MELEE_TRACKER_MIXER_STAGE_FADE_OUT = 1;       // S_MeleeTrackerMixerStage::flags bit 0
constexpr int MELEE_TRACKER_MIXER_TRACK_EVENTS = 3;

// One of the model's animation stages the mixer blends on (0x20 bytes).
struct S_MeleeTrackerMixerStage {
    float weight = 0.0f;                                // +0x00 the full weight
    float blendTime = 0.0f;                             // +0x04 ms
    float blendElapsed = MELEE_TRACKER_MIXER_NO_BLEND;            // +0x08 ms (MELEE_TRACKER_MIXER_NO_BLEND: free)
    uint8_t crossfade = 1;                              // +0x0C fade the old stage out as this fades in
    uint8_t pad0D[3] = {};
    uint32_t flags = 0;                                 // +0x10 MELEE_TRACKER_MIXER_STAGE_FADE_OUT
    float rate = 1.0f;                                  // +0x14 of the time (also the speed factor)
    uint8_t finished = 0;                               // +0x18 the animation ran out
    uint8_t pad19[3] = {};
    int tag = -1;                                       // +0x1C
};

// One animation channel of the mixer (0x24 bytes); it owns the pair of stages 2*track and 2*track+1.
struct S_MeleeTrackerMixerTrack {
    int current = -1;                                   // +0x00 stage of the pair playing (0/1; -1: none)
    int time = -1;                                      // +0x04 ms played (movement time)
    int hold = 0;                                       // +0x08 > 0: held, rate 0
    int events[MELEE_TRACKER_MIXER_TRACK_EVENTS] = {};            // +0x0C note events since the last update
    int eventCount = 0;                                 // +0x18
    int length = 0;                                     // +0x1C the animation's end time (ms)
    uint8_t reversed = 0;                               // +0x20
    uint8_t pad21[3] = {};
};

// One track's requested animation, handed to PlayStages (0x14 bytes).
struct S_MeleeTrackerMixerAnim {
    I3D_animation_set *anim = nullptr;                  // +0x00 null: leave the track alone
    float speed = 0.0f;                                 // +0x04 root-motion speed (m/s at weight 1)
    float weight = 0.0f;                                // +0x08
    float blendTime = 0.0f;                             // +0x0C ms
    uint8_t crossfade = 0;                              // +0x10
    uint8_t pad11[3] = {};
};

// The animation mixer embedded in C_MeleeTrackerMovement at +0x10 (0x150 bytes). The original dispatches
// its Reset / SaveGame / LoadGame through a vtable (0x008121d0); the port keeps the vtable slot
// as a layout placeholder and calls the methods directly.
class C_MeleeTrackerAnimMixer {
public:
    // Sets the model and resets the tracks and stages.
    void Init(I3D_model *model);                        // 0x005c6e10
    // Detaches the model's note callback and resets.
    void Reset();                                       // vtable +0x00 0x005d0950
    bool SaveGame(C_chunk &chunk);                      // vtable +0x04 0x005cfd20
    bool LoadGame(C_chunk &chunk);                      // vtable +0x08 0x005d0180

    // Advances the playing stages by `timeDelta` ms and runs the blends. `start` (the original's
    // char flag) only resets the per-update note events without advancing; `trackActive`, when
    // given, gates each track on a non-zero entry.
    void UpdateStages(int unused, int timeDelta, bool start, const int *trackActive);   // 0x005c6ee0
    // Turns the body's `velocity` (m/s) plus any add-move and slide into the move of this update
    // (C_MeleeTrackerAnimMixer::GetMove, m).
    void UpdateMove(int timeDelta, const float *velocity);   // 0x005c71e0
    // Starts the animation of each track in `anims` (two entries) on the free stage of its pair,
    // cross-fading from the one playing.
    void PlayStages(const S_MeleeTrackerMixerAnim *anims);       // 0x005c7430
    // An add-move of `move` (x,y,z,w) over `duration` ms.
    void SetAddMove(const float *move, int duration);   // 0x005c75c0
    // A slide of `offset` over `duration` ms, applied in 80 ms steps.
    void SetSlide(const float *offset, int duration);   // 0x005c7600
    // The ms played on `track`, or -1 once its animation is no longer active.
    int GetActiveTrackTime(int track);                  // 0x005c7850
    // The length (ms) of the animation on `track`, 0 if none.
    int GetTrackLength(int track);                      // 0x005c7890
    // Sets the time rate of the stage `track` is playing.
    void SetTrackRate(int track, float rate);           // 0x005c7930
    // Returns the event array of `track` and, via `count`, how many events it holds, then clears
    // the count.
    int *TakeSoundEvents(int track, int *count);        // 0x005c7ae0
    // How far the blend of `track` is (0..1; 1 when done). Clears the scratch progress.
    float GetBlendProgress(int track);                  // 0x005c7b00

    // The move of the last UpdateMove (m).
    const S_vector &GetMove() const { return *reinterpret_cast<const S_vector *>(&m_move); }
    I3D_model *GetModel() const { return m_model; }
    // Reads and clears the "a stage finished this update" flag (the movement polls it at +0x15C).
    bool TakeStageFinished() { const bool v = m_stageFinished != 0; m_stageFinished = 0; return v; }

    // The note callback of the model's note frame "a" (I3DCALLBACK_ANIM_NOTE): routes the note
    // keys to the meleeTracker's mixer. (0x005c7960)
    static int __stdcall NoteHook(I3D_frame *frame, uint32_t message, uintptr_t param1, uintptr_t param2);
    // A note key: "a1"/"a2"/"m" add an overlay event, "s" a footstep sound event. NoteHook also
    // passes the callback's last parameter, which is not read.
    void OnNote(uint32_t message, const char *note, uintptr_t unused);   // 0x005c79b0

private:
    // Resets the tracks and stages and hooks the note callback. (0x005c6d10)
    void ResetTracks();

    void *m_vtable = nullptr;                           // (set by 0x005d4ce0 (DS 0x005ef320)) +0x00 (0x008121d0; dispatched directly in the port)
    uint8_t m_unknown04[0x0C] = {};                     // +0x04
    I3D_model *m_model = nullptr;                        // +0x10
    S_MeleeTrackerMixerTrack m_tracks[MELEE_TRACKER_MIXER_TRACKS];          // +0x14
    S_MeleeTrackerMixerStage m_stages[MELEE_TRACKER_MIXER_STAGES];          // +0x5C
    float m_blendScratch = 0.0f;                         // +0xDC GetBlendProgress leaves it here
    float m_speedPrevious = 0.0f;                        // +0xE0 root-motion speed of the old animation
    float m_speedCurrent = 0.0f;                         // +0xE4 and of the new one
    uint8_t m_unknownE8[8] = {};                         // +0xE8
    S_vector4 m_move = {};                               // +0xF0 move of the last update (w is scratch)
    S_vector4 m_addMove = {};                            // +0x100 what is left to add-move
    int m_addMoveTime = 0;                               // +0x110 ms elapsed
    int m_addMoveDuration = MELEE_TRACKER_MOVE_NO_TIME;            // +0x114
    uint8_t m_unknown118[8] = {};                        // +0x118
    S_vector4 m_slide = {};                              // +0x120 what is left to slide
    S_vector4 m_slideVelocity = {};                      // +0x130 per-80 ms step
    int m_slideStepTime = 0;                             // +0x140 ms into the current step
    int m_slideTime = 0;                                 // +0x144 ms elapsed
    int m_slideDuration = MELEE_TRACKER_MOVE_NO_TIME;              // +0x148
    uint8_t m_stageFinished = 0;                         // +0x14C a stage ran out this update
    uint8_t pad14D[3] = {};
};
static_assert(sizeof(S_MeleeTrackerMixerStage) == 0x20, "S_MeleeTrackerMixerStage layout");
static_assert(sizeof(S_MeleeTrackerMixerTrack) == 0x24, "S_MeleeTrackerMixerTrack layout");
static_assert(sizeof(S_MeleeTrackerMixerAnim) == 0x14, "S_MeleeTrackerMixerAnim layout");
static_assert(sizeof(C_MeleeTrackerAnimMixer) == 0x150, "C_MeleeTrackerAnimMixer layout");

// ---------------------------------------------------------------------------
// C_MeleeTrackerMovement (C_MeleeTracker +0x90; vtable 0x008121c4; ~0x410 bytes)
//
// A meleeTracker animation command: the group/animation ids and tags the mixer plays. One current (+0x284)
// and a 5-deep queue (+0x2A8). Read back by the seq accessors at 0x005c85a0/0x005c8600/0x005c8660.
struct S_MeleeTrackerAnimSequence {                              // 0x24 bytes
    int anims[4];                                       // +0x00 the animation-id run (filled from the back)
    int animA;                                          // +0x10 the mode (0x17 default; MeleeTrackerAnimClass input)
    int animB;                                          // +0x14 sub-mode A
    int animC;                                          // +0x18 sub-mode B
    int readIndex = -1;                                 // +0x1C index into anims[] being played (-1: none)
    int endIndex = -1;                                  // +0x20 one past the last filled anims[] slot
};

// One entry of the meleeTracker's animation table (loaded from Tables\dog_anim.sav).
struct S_MeleeTrackerAnim {                                      // 0x0C bytes
    class I3D_animation_set *anim;                      // +0x00
    int blendTime;                                      // +0x04 ms
    float speed;                                        // +0x08 root-motion speed (0: in place)
};
extern S_MeleeTrackerAnim g_meleeTrackerAnims[0x17];                      // 0x008AC5C0 (23 entries)

// The animation "class" of an animation group (walk / run / lie / ...; 6 = the hit reactions
// 0x13 / 0x14), driving Move's sphere and move constraints and the actor's hit reaction.
int MeleeTrackerAnimClass(int group);                   // 0x005d86f0

// The physics/movement of the meleeTracker. The layout is byte-exact; the fields are named from the
// save/load (0x005d09b0 / 0x005d11a0), Init (0x005c7d30) and the update path. Fields whose role
// is not yet identified keep their offset names.
class C_MeleeTrackerMovement {
public:
    // --- lifecycle -----------------------------------------------------------------
    void Init();                                        // 0x005c7d30 (body frames, spheres, state)
    // Binds the body to the tracker and its model (the mixer drives the model), then Init.
    void Bind(I3D_model *model, C_MeleeTracker *owner); // 0x005c8370
    void Reset();                                       // vtable +0x00 0x005d1f50
    bool SaveGame(C_chunk &chunk);                      // vtable +0x04 0x005d09b0
    bool LoadGame(C_chunk &chunk);                      // vtable +0x08 0x005d11a0
    // Adds/removes the meleeTracker from simulation (collision volumes + the model's on-flag).
    void SetOptimized(bool optimized);                  // 0x005c86e0
    void SetOptimizedController(bool optimized);        // 0x005c86c0 (sets m_noCollision)

    // --- per-frame -----------------------------------------------------------------
    void Update(int timeDelta);                         // 0x005c8b30
    // The move/collision step (ground following, slopes, sphere stepping).
    void Move(int timeDelta);                           // 0x005c3420
    // Advances the model's animations and events for the frame.
    void UpdateAnimation(int timeDelta);                // 0x005c18c0 (DS 0x005d69f0)
    // Builds the animation-sequence queue for the current mode / sub-modes (driven by the actor).
    void BuildSequence();                               // 0x005c5030
    // Plays the next animation of the sequence through the mixer (driven by the actor each frame).
    // `unused` is a stack argument the caller passes but the body ignores.
    void StepAnimation(int unused);                     // 0x005c4cb0
    // Advances the attack animation sequence (driven by the AI when attacking).
    void CheckAttackSequence();                         // 0x005c5a90
    // Turns the meleeTracker ~2.8 rad for a bark-at reaction (driven by the AI, with a cooldown).
    void BarkTurn();                                    // 0x005c6c70

    // --- orders --------------------------------------------------------------------
    // The movement mode (0 stand, 1 walk, 2 lie, 3 run, 5 dead), taken if `priority` wins.
    void SetMode(int mode, int priority);               // 0x005c8490
    void SetSubModeA(int value, int priority);          // 0x005c8540
    void SetSubModeB(int value, int priority);          // 0x005c8570
    // The desired facing direction (world).
    void SetFacing(const float *dir);                   // 0x005c1e60
    // The target animation speed the mode eases to.
    void SetAnimSpeed(float speed) { m_speedTarget = speed; }   // 0x005c4b50
    // How far the tracker lunges at a target: 4.5 m, none while airborne. (The original compares
    // the float counter's bits with 5, which is the same for its values 0, 1, 2, ...)
    float GetLungeRange() const { return m_airSteps > 0.0f ? 0.0f : 4.5f; }   // 0x005c8b10
    // Removes the collision volumes and stops the looped sounds (on death / cleanup).
    void FreeVolumesAndSounds();                        // 0x005c83a0

    // --- networking ----------------------------------------------------------------
    // Sets the networked world position and derives the interpolation velocity.
    void SetNetPosition(const float *pos);              // 0x005c0f50

    C_MeleeTrackerAnimMixer &GetMixer() { return m_mixer; }
    const C_MeleeTrackerAnimMixer &GetMixer() const { return m_mixer; }
    C_MeleeTracker *GetMeleeTracker() const { return m_meleeTracker; }

private:
    // --- helpers -------------------------------------------------------------------
    void ClearNetBit(const float *pos);                 // 0x005c0d10
    void SetWorldDir(const float *dir);                 // 0x005c0d60
    // `frameTime` (= m_388) is passed by the caller but the body reads it from the field; kept to
    // match the original signature.
    void ApplyNetInterp(int frameTime);                 // 0x005c1430
    void HandleAnimEvents();                             // 0x005c1600
    // `flag` is an unused second argument the caller passes as 1.
    bool TurnToFacing(int timeDelta, int flag);         // 0x005c1b80
    void UpdateSpheres();                               // 0x005c1dc0
    void TurnDir(float *dir);                            // 0x005c89c0
    void CheckGround();                                  // 0x005c88b0
    void Bite();                                         // 0x005c49a0
    // Plays the per-animation sounds / effects (barks, growls, the mouth-open frame).
    void PlayAnimSound(int animId);                     // 0x005c5bf0
    // The gravity landing / contact response after the sphere step.
    void ContactResponse(S_CollisionVolume *hit, char grounded, int timeDelta, float *move);  // 0x005c2260
    // Tilts the meleeTracker's direction to follow the ground slope under it.
    void AlignToSlope(int timeDelta);                   // 0x005c26d0
    // Blends the step's end position toward the ground and sets the body track rate when sliding
    // down a slope near a drop (0x005c43d0). The original's single param_2 pointer bundles the
    // surface normal, the hit volume (+0x14) and the "moving" flag (+0x11), which in Move alias an
    // uninitialised stack region; the port passes them as separate arguments (so the arg count
    // differs from the original's — not an ABI match).
    void SlopeSlide(S_MoveContacts *contacts, const float *normal, S_CollisionVolume *hit, bool moving,
                    const float *groundPos, const float *move, int timeDelta);
    // The 5-entry animation-sequence ring (m_queue): m_35c is the read id, m_360 the write index.
    S_MeleeTrackerAnimSequence *QueuePushSlot();                  // 0x005c4b90 (a new write slot, tags reset)
    void QueuePopFront();                                // 0x005c4c30 (drop the oldest; empty -> -1)
    void QueuePopBack();                                 // 0x005c4c70 (drop the newest)

public:
    // The current animation sequence's mode / sub-modes (read by the AI's behaviour apply).
    int GetSeqAnimA();                                   // 0x005c85a0 (made public for the AI)
    int GetSeqAnimB();                                   // 0x005c8600
    int GetSeqAnimC();                                   // 0x005c8660

private:

public:
    void *m_vtable = nullptr;                            // +0x000 (0x008121c4)
    uint8_t m_unknown04[0x0C] = {};                      // +0x004
    C_MeleeTrackerAnimMixer m_mixer;                              // +0x010 (ends +0x160; m_mixer.m_stageFinished is +0x15C)
    C_MeleeTracker *m_meleeTracker = nullptr;                              // +0x160 the owning meleeTracker
    I3D_frame *m_animFrame = nullptr;                    // +0x164 the model's "anim2" frame
    void *m_168 = nullptr;                               // +0x168
    uint8_t m_unknown16C[4] = {};                        // +0x16C
    S_vector4 m_moveDir = {};                            // +0x170 the move direction applied this frame
    S_vector4 m_facing = {};                             // +0x180 the desired facing direction (SetFacing)
    // +0x190..+0x1F0: collision spheres (3) and their world volumes (2 small + 3 large).
    I3D_frame *m_colFrame[3] = {};                       // +0x190 "col_dummy1..3"
    uint8_t m_unknown19C[4] = {};                        // +0x19C
    S_vector4 m_spherePos[3] = {};                       // +0x1A0 world centres (UpdateSpheres)
    float m_sphereRadius[3] = {};                        // +0x1D0
    S_CollisionVolume *m_volumeA[2] = {};                // +0x1DC radius * 1.65
    S_CollisionVolume *m_volumeB[3] = {};                // +0x1E4 radius * 2.50, y + 0.22
    int m_material = -1;                                 // +0x1F0 material id under the meleeTracker (-1: none)
    uint32_t m_1f4 = 0;                                  // +0x1F4
    float m_1f8 = 0.0f;                                  // +0x1F8
    float m_1fc = 0.0f;                                  // +0x1FC slope-slide blend factor
    float m_200 = 1.0f;                                  // +0x200
    uint8_t m_unknown204[0x0C] = {};                     // +0x204
    S_vector4 m_210 = {};                                // +0x210 (a direction; Init: 0,-1,0)
    S_vector4 m_220 = {};                                // +0x220 (a direction; Init: 0,-1,0)
    bool m_noCollision = false;                          // +0x230 (SetOptimizedController)
    uint8_t m_turned = 0;                                // +0x231 the meleeTracker was turned this frame
    uint8_t m_moved = 0;                                 // +0x232 the meleeTracker was moved this frame
    uint8_t pad233 = 0;
    uint32_t m_234 = 0;                                  // +0x234
    uint32_t m_238 = 0;                                  // +0x238
    uint32_t m_23c = 0;                                  // +0x23C
    float m_240 = 0.0f;                                  // +0x240
    float m_speed = 1.0f;                                // +0x244 the animation-speed weight (eased to m_speedTarget)
    float m_speedTarget = 1.0f;                          // +0x248
    float m_airSteps = 0.0f;                             // +0x24C Move steps without ground contact (0 grounded)
    float m_250 = 0.0f;                                  // +0x250
    float m_254 = 0.0f;                                  // +0x254
    float m_258 = 0.0f;                                  // +0x258
    int m_25c = MELEE_TRACKER_MOVE_NO_TIME;                        // +0x25C
    bool m_260 = false;                                  // +0x260
    uint8_t pad261[3] = {};
    uint32_t m_264 = 0;                                  // +0x264
    bool m_bitePending = false;                          // +0x268 a bite event fired this frame
    uint8_t pad269[3] = {};
    int m_mode = 0;                                      // +0x26C 0 stand, 1 walk, 2 lie, 3 run, 5 dead
    int m_subModeA = 0;                                  // +0x270
    int m_subModeB = 0;                                  // +0x274
    int m_modePriority = 0;                              // +0x278
    int m_subModeAPriority = 0;                          // +0x27C
    int m_subModeBPriority = 0;                          // +0x280
    S_MeleeTrackerAnimSequence m_currentSeq = {};                 // +0x284
    S_MeleeTrackerAnimSequence m_queue[5] = {};                   // +0x2A8 (0xB4 bytes)
    int m_35c = -1;                                      // +0x35C active queue entry id (-1: none)
    int m_360 = -1;                                      // +0x360 active queue index
    int m_364 = 0x17;                                    // +0x364 the current animation group/id
    int m_368 = 0x17;                                    // +0x368
    uint8_t m_36c = 0;                                   // +0x36C
    uint8_t pad36D[3] = {};
    int m_370 = 0;                                       // +0x370 a timer counted down in Update
    int m_374 = 0;                                       // +0x374 landing state (CheckGround: 0/1/2)
    int m_378 = MELEE_TRACKER_MOVE_NO_TIME;                        // +0x378 a timer
    int m_soundHandle[3] = {-1, -1, -1};                 // +0x37C looped sound handles (+0x37C/+0x380/+0x384)
    int m_388 = 0;                                       // +0x388 the frame time (net interpolation clock)
    int m_38c = -1;                                      // +0x38C the interpolation duration (-1: none)
    S_vector4 m_prevNetPos = {};                         // +0x390 previous net position
    S_vector4 m_prevFramePos = {};                       // +0x3A0 previous frame world position
    S_vector4 m_netPos = {};                             // +0x3B0 current net position (SetNetPosition)
    uint8_t m_3c0 = 0;                                   // +0x3C0 a net-position-set flag
    uint8_t pad3C1[0x0F] = {};
    S_vector4 m_netVelocity = {};                        // +0x3D0 interpolation velocity
    uint32_t m_3e0 = 0;                                  // +0x3E0 net flags (bit 0: interpolating)
    uint8_t m_3e4 = 0;                                   // +0x3E4 a pending-net flag
    uint8_t pad3E5[3] = {};
    I3D_frame *m_3e8 = nullptr;                          // +0x3E8 body frame
    I3D_frame *m_bodyFrame = nullptr;                    // +0x3EC body centre frame (CheckGround)
    I3D_frame *m_3f0 = nullptr;                          // +0x3F0
    I3D_frame *m_3f4 = nullptr;                          // +0x3F4
    I3D_frame *m_3f8 = nullptr;                          // +0x3F8
    I3D_frame *m_3fc = nullptr;                          // +0x3FC
    I3D_frame *m_400 = nullptr;                          // +0x400
    I3D_frame *m_groundFrame = nullptr;                  // +0x404 frame used for the ground sector test
    uint8_t pad408[8] = {};                              // to +0x410
};
static_assert(offsetof(C_MeleeTrackerMovement, m_mixer) == 0x10, "C_MeleeTrackerMovement.m_mixer");
static_assert(offsetof(C_MeleeTrackerMovement, m_meleeTracker) == 0x160, "C_MeleeTrackerMovement.m_meleeTracker");
static_assert(offsetof(C_MeleeTrackerMovement, m_168) == 0x168, "C_MeleeTrackerMovement.m_168");
static_assert(offsetof(C_MeleeTrackerMovement, m_moveDir) == 0x170, "C_MeleeTrackerMovement.m_moveDir");
static_assert(offsetof(C_MeleeTrackerMovement, m_facing) == 0x180, "C_MeleeTrackerMovement.m_facing");
static_assert(offsetof(C_MeleeTrackerMovement, m_colFrame) == 0x190, "C_MeleeTrackerMovement.m_colFrame");
static_assert(offsetof(C_MeleeTrackerMovement, m_spherePos) == 0x1A0, "C_MeleeTrackerMovement.m_spherePos");
static_assert(offsetof(C_MeleeTrackerMovement, m_sphereRadius) == 0x1D0, "C_MeleeTrackerMovement.m_sphereRadius");
static_assert(offsetof(C_MeleeTrackerMovement, m_volumeA) == 0x1DC, "C_MeleeTrackerMovement.m_volumeA");
static_assert(offsetof(C_MeleeTrackerMovement, m_volumeB) == 0x1E4, "C_MeleeTrackerMovement.m_volumeB");
static_assert(offsetof(C_MeleeTrackerMovement, m_material) == 0x1F0, "C_MeleeTrackerMovement.m_material");
static_assert(offsetof(C_MeleeTrackerMovement, m_210) == 0x210, "C_MeleeTrackerMovement.m_210");
static_assert(offsetof(C_MeleeTrackerMovement, m_noCollision) == 0x230, "C_MeleeTrackerMovement.m_noCollision");
static_assert(offsetof(C_MeleeTrackerMovement, m_speed) == 0x244, "C_MeleeTrackerMovement.m_speed");
static_assert(offsetof(C_MeleeTrackerMovement, m_bitePending) == 0x268, "C_MeleeTrackerMovement.m_bitePending");
static_assert(offsetof(C_MeleeTrackerMovement, m_mode) == 0x26C, "C_MeleeTrackerMovement.m_mode");
static_assert(offsetof(C_MeleeTrackerMovement, m_currentSeq) == 0x284, "C_MeleeTrackerMovement.m_currentSeq");
static_assert(offsetof(C_MeleeTrackerMovement, m_queue) == 0x2A8, "C_MeleeTrackerMovement.m_queue");
static_assert(offsetof(C_MeleeTrackerMovement, m_35c) == 0x35C, "C_MeleeTrackerMovement.m_35c");
static_assert(offsetof(C_MeleeTrackerMovement, m_soundHandle) == 0x37C, "C_MeleeTrackerMovement.m_soundHandle");
static_assert(offsetof(C_MeleeTrackerMovement, m_prevNetPos) == 0x390, "C_MeleeTrackerMovement.m_prevNetPos");
static_assert(offsetof(C_MeleeTrackerMovement, m_netVelocity) == 0x3D0, "C_MeleeTrackerMovement.m_netVelocity");
static_assert(offsetof(C_MeleeTrackerMovement, m_3e4) == 0x3E4, "C_MeleeTrackerMovement.m_3e4");
static_assert(offsetof(C_MeleeTrackerMovement, m_groundFrame) == 0x404, "C_MeleeTrackerMovement.m_groundFrame");
static_assert(sizeof(C_MeleeTrackerMovement) == 0x410, "C_MeleeTrackerMovement layout");
