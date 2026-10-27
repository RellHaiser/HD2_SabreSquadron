# The meleeTracker's body (src/game/melee_tracker_movement.h, src/game/melee_tracker_anim_mixer.cpp): the animation mixer
# (C_MeleeTrackerAnimMixer, C_MeleeTrackerMovement +0x10) and the movement/physics (C_MeleeTrackerMovement, C_MeleeTracker +0x90);
# then the actor's small structs and the controller hierarchy (src/game/melee_tracker_ai.h). The actor itself
# (C_MeleeTracker, vtable 0x008120e0) stays an auto class (90_auto.spec): its recovered layout lives in Ghidra.

# One of the model's animation stages the mixer blends on.
struct S_MeleeTrackerMixerStage size=0x20
  0x00 float weight
  0x04 float blendTime
  0x08 float blendElapsed                 # -10000: free
  0x0c uint8_t crossfade
  0x10 uint32_t flags                         # bit 0: fade out
  0x14 float rate
  0x18 uint8_t finished
  0x1c int tag
end

# One animation channel of the mixer (owns stages 2*track and 2*track+1).
struct S_MeleeTrackerMixerTrack size=0x24
  0x00 int current                        # stage of the pair playing (0/1; -1 none)
  0x04 int time
  0x08 int hold
  0x0c int events[3]
  0x18 int eventCount
  0x1c int length
  0x20 uint8_t reversed
end

# One track's requested animation (handed to PlayStages).
struct S_MeleeTrackerMixerAnim size=0x14
  0x00 I3D_animation_set* anim
  0x04 float speed
  0x08 float weight
  0x0c float blendTime
  0x10 uint8_t crossfade
end

# The animation mixer (C_MeleeTrackerMovement +0x10 = C_MeleeTracker +0xA0; vtable 0x008121d0).
struct C_MeleeTrackerAnimMixer size=0x150
  0x00 void* vtable
  0x10 I3D_model* m_model
  0x14 S_MeleeTrackerMixerTrack m_tracks[2]
  0x5c S_MeleeTrackerMixerStage m_stages[4]
  0xdc float m_blendScratch
  0xe0 float m_speedPrevious
  0xe4 float m_speedCurrent
  0xf0 S_vector4 m_move
  0x100 S_vector4 m_addMove
  0x110 int m_addMoveTime
  0x114 int m_addMoveDuration
  0x120 S_vector4 m_slide
  0x130 S_vector4 m_slideVelocity
  0x140 int m_slideStepTime
  0x144 int m_slideTime
  0x148 int m_slideDuration
  0x14c uint8_t m_stageFinished
end

# A meleeTracker animation command: an id run plus the mode / sub-modes (current + a 5-deep queue).
struct S_MeleeTrackerAnimSequence size=0x24
  0x00 int anims[4]
  0x10 int animA                          # the mode (MeleeTrackerAnimClass input)
  0x14 int animB                          # sub-mode A
  0x18 int animC                          # sub-mode B
  0x1c int readIndex                      # -1: none
  0x20 int endIndex
end

# One entry of the meleeTracker's animation table (Tables\dog_anim.sav; g_meleeTrackerAnims at 0x008ac5c0).
struct S_MeleeTrackerAnim size=0x0c
  0x00 I3D_animation_set* anim
  0x04 int blendTime
  0x08 float speed
end

# The movement / physics (C_MeleeTracker +0x90; vtable 0x008121c4).
struct C_MeleeTrackerMovement size=0x410
  0x00 void* vtable
  0x10 C_MeleeTrackerAnimMixer m_mixer
  0x160 C_MeleeTracker* m_meleeTracker
  0x164 I3D_frame* m_animFrame
  0x168 void* m_168
  0x170 S_vector4 m_moveDir
  0x180 S_vector4 m_facing
  0x190 I3D_frame* m_colFrame[3]
  0x1a0 S_vector4 m_spherePos[3]
  0x1d0 float m_sphereRadius[3]
  0x1dc S_CollisionVolume* m_volumeA[2]
  0x1e4 S_CollisionVolume* m_volumeB[3]
  0x1f0 int m_material
  0x1f4 uint32_t m_1f4
  0x1f8 float m_1f8
  0x1fc float m_1fc
  0x200 float m_200
  0x210 S_vector4 m_210
  0x220 S_vector4 m_220
  0x230 uint8_t m_noCollision
  0x231 uint8_t m_turned
  0x232 uint8_t m_moved
  0x234 uint32_t m_234
  0x238 uint32_t m_238
  0x23c uint32_t m_23c
  0x240 float m_240
  0x244 float m_speed
  0x248 float m_speedTarget
  0x24c float m_airSteps                       # Move steps without ground contact
  0x250 float m_250
  0x254 float m_254
  0x258 float m_258
  0x25c int m_25c
  0x260 uint8_t m_260
  0x264 uint32_t m_264
  0x268 uint8_t m_bitePending
  0x26c int m_mode
  0x270 int m_subModeA
  0x274 int m_subModeB
  0x278 int m_modePriority
  0x27c int m_subModeAPriority
  0x280 int m_subModeBPriority
  0x284 S_MeleeTrackerAnimSequence m_currentSeq
  0x2a8 S_MeleeTrackerAnimSequence m_queue[5]
  0x35c int m_35c
  0x360 int m_360
  0x364 int m_364
  0x368 int m_368
  0x36c uint8_t m_36c
  0x370 int m_370
  0x374 int m_374
  0x378 int m_378
  0x37c int m_soundHandle[3]
  0x388 int m_388
  0x38c int m_38c
  0x390 S_vector4 m_prevNetPos
  0x3a0 S_vector4 m_prevFramePos
  0x3b0 S_vector4 m_netPos
  0x3c0 uint8_t m_3c0
  0x3d0 S_vector4 m_netVelocity
  0x3e0 uint32_t m_3e0
  0x3e4 uint8_t m_3e4
  0x3e8 I3D_frame* m_3e8
  0x3ec I3D_frame* m_bodyFrame
  0x3f0 I3D_frame* m_3f0
  0x3f4 I3D_frame* m_3f4
  0x3f8 I3D_frame* m_3f8
  0x3fc I3D_frame* m_3fc
  0x400 I3D_frame* m_400
  0x404 I3D_frame* m_groundFrame
end

# A remembered sighting / heard sound (C_MeleeTracker's list; pooled, saved raw).
struct S_MeleeTrackerSighting size=0x20
  0x00 S_vector4 pos
  0x10 int type                                # the sense group (sound chapter)
  0x14 C_Actor* source
  0x18 uint32_t time
  0x1c int loudness                            # the AI alarms when > 10
end

# The tracker's tunables (C_MeleeTracker +0x4A0, and the mission-start copy at +0x4B0).
struct S_MeleeTrackerStats size=0x10
  0x00 int health
  0x04 int biteDamage
  0x08 int sightRange
  0x0c int hearRange
end

# The controllers (src/game/melee_tracker_ai.h, C_MeleeTracker +0x534): the root and its variants.
class C_MeleeTrackerController size=0x70 vtable=0x812048:27
  0x10 C_MeleeTracker* m_meleeTracker
  0x14 int m_mode                              # the variant: 1 player, 4 auto, 5 scripted, 7 remote
  0x18 C_Actor* m_owner
  0x1c int m_alarm
  0x20 uint32_t m_flags                        # MELEE_TRACKER_ORDER_*
  0x30 S_vector4 m_savedPos
  0x40 S_vector4 m_facing
  0x50 S_vector4 m_050
  0x60 float* m_actionA                        # the behaviour weights Apply commits
  0x64 float* m_actionB
  0x68 int m_068
end

# The full AI (abstract; sensing, the target, heel, the checkpoint path).
class C_MeleeTrackerAI : C_MeleeTrackerController size=0xc0 vtable=0x811fd0:29
  0x70 C_CheckpointNetwork* m_checkpoints
  0x74 std::vector<S_AIWaypoint*> m_path
  0x84 int m_084
  0x88 C_Actor* m_target
  0x8c C_Actor* m_orderedTarget
  0x90 S_vector4 m_090                         # where the target was at the last path (-10000: none)
  0xa0 S_vector4 m_0a0                         # the heel spot of the last path (-10000: none)
  0xb0 int m_0b0
  0xb4 int m_0b4
  0xb8 uint8_t m_changed
  0xbc int m_0bc
end

class C_MeleeTrackerScriptedAI : C_MeleeTrackerAI size=0xd0 vtable=0x812250:29
  0xc0 C_Script* m_script
  0xc4 int m_barkTimer
end

class C_MeleeTrackerAutoAI : C_MeleeTrackerAI size=0xc0 vtable=0x8122c8:29
end

class C_MeleeTrackerRemote : C_MeleeTrackerController size=0x270 vtable=0x8121e0:27
  0x70 float m_bufferA[64]
  0x170 float m_bufferB[64]
end

class C_MeleeTrackerPlayerControl : C_MeleeTrackerController size=0x4b0 vtable=0x812340:27
  0x70 float m_bufferA[64]
  0x170 float m_bufferB[64]
  0x270 float m_buffer270[64]
  0x370 float m_buffer370[64]
  0x470 S_vector4 m_netPos
  0x480 S_vector4 m_netDir
  0x490 S_vector4 m_netVelocity
end
