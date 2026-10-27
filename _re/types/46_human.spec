# Humans (src/game/human*.{h,cpp}): C_Human ("Human", actor type 3, 0x0059be30..0x005aa790), its
# body part (movement / work / equipment objects at +0x98, 0x005ab330..0x005ac4c0), the script
# command wrappers (0x005af150..0x005af780) and the base of the controllers that drive a human
# (C_HumanController, vtable 0x00810fb8, ported; the derived controller classes are not).
# C_HumanEvents (0x005ac950..0x005af110) and the speech sets (0x005af7a0..0x005b0e10) are ported
# too (human_events.cpp, human_speech.cpp).

# C_Human::GetStat() indices (m_stats). Names of 0..8 from the soldier stats screen (table
# 0x0083a9d0) and the single player experience messages; 3 and 5 are not shown there.
enum E_HumanStat
  STAT_HEALTH = 0                   # 800 = full
  STAT_STRENGTH = 1
  STAT_ENDURANCE = 2                # initial stamina
  STAT_MORALE = 3                   # enemy AI: raised / lowered, thresholds 40 / 75 / 90 (inferred)
  STAT_SHOOTING = 4
  STAT_UNKNOWN5 = 5
  STAT_STEALTH = 6
  STAT_FIRST_AID = 7
  STAT_LOCKPICK = 8
  STAT_SIGHT_RANGE = 9              # m
  STAT_SPEECH_SET = 10              # Tables\speech.set entry
  STAT_HEARING_RANGE = 11           # m
  STAT_COUNT = 12
end

# Soldier of the campaign roster (C_HumanStore, GetSoldierProfile; tables\HumanStore.def chunk 9000).
struct S_SoldierProfile size=0x74
  0x00 C_str firstName                     # chunk 0x2396
  0x04 C_str lastName                      # chunk 0x23A0
  0x08 C_str nickname                      # chunk 0x23AA
  0x0c C_str fullName                      # "first (nick) last" / "first last"
  0x10 C_str shortName                     # "F. last"
  0x14 C_str customName                    # usernames.sav; overrides the others when not empty
  0x18 C_str faceFile                      # chunk 0x23F0 ("x_f_001.bmp")
  0x1c C_str faceTexture                   # "e_f001.bmp"
  0x20 IImage* faceImage
  0x24 int unknown24                       # chunk 0x26DE; sent with the soldier state
  0x28 std::vector<int> unknown28          # chunks 0x26FC
  0x38 int unknown38                       # chunk 0x26F3
  0x3c int unknown3C                       # chunk 0x26F4
  0x40 int unknown40                       # chunk 0x26F5
  0x44 int unknown44                       # chunk 0x26AD with the stats
  0x48 float stats[9]                      # E_HumanStat 0..8
  0x6c int unknown6C                       # chunk 0x2580 with `dead`
  0x70 bool dead                           # set when the human dies in single player
end

# Single player progress of a soldier (C_Human +0xC0, saved for players).
struct S_SoldierProgress size=0x24
  0x00 float unknown00              # at most 30 (0x005a92f0)
  0x04 float unknown04              # 0.5 by default
  0x08 float endurance              # gains, at most 5
  0x0c float morale                 # 1.0 by default, at most 1 (0x005a9410)
  0x10 float shooting               # at most 2
  0x14 float stealth                # at most 2
  0x18 float firstAid               # at most 5
  0x1c float lockpick               # at most 5
  0x20 int experience
end

# Item of a soldier state (equipment sent over the network / kept between missions).
struct S_SoldierItem size=0x6
  0x00 int16_t uid                  # -1: the receiver creates a new uid
  0x02 int16_t typeId               # -1: none
  0x04 int16_t present
end

# What a human is and carries (C_Human::Setup, network state: FUN_00727850 / FUN_00727a30).
struct S_SoldierState size=0x8c
  0x00 C_str name
  0x04 int spawnZone                # multiplayer: the net id of the zone to spawn at
  0x08 bool spectating              # player controllers
  0x09 bool vip                     # objectives games (the player controller's +0x550)
  0x0c int health
  0x10 S_SoldierProgress progress
  0x34 int unknown34                # the squad's new soldiers: -1
  0x38 int unknown38                # the squad's new soldiers: -1
  0x3c int profileIndex             # S_SoldierProfile index, -1 none
  0x40 int team                     # C_Human::GetTeam()
  0x44 bool deployed                # single player: in the mission's squad
  0x48 int speechSet                # player speech set 1..4 (other values: random)
  0x4c S_SoldierItem head
  0x52 S_SoldierItem uniform
  0x58 S_SoldierItem backpack
  0x5e S_SoldierItem longGun
  0x64 S_SoldierItem hands
  0x6c std::vector<S_SoldierItem> belt
  0x7c std::vector<S_SoldierItem> backpackContents
end

# Sound a human heard (pooled, 0x008ab158), kept 3 s in C_Human +0x2A8.
struct S_RememberedSound size=0x20
  0x00 S_vector pos
  0x0c float posW
  0x10 int chapter                  # E_SoundChapter
  0x14 C_Actor* source
  0x18 uint32_t time                # timeGetTime()
  0x1c bool refreshed
end

# What the sound module tells an AI listener (C_Human::OnSoundHeard; sound_module.h).
struct S_HeardSound size=0x20
  0x00 S_vector pos
  0x0c float posW
  0x10 I3D_frame* listener
  0x14 float intensity              # 1 inside the near range, 0 at the far range
  0x18 int chapter                  # E_SoundChapter
  0x1c C_Actor* source
end

# What the AI of a human remembers (C_Human +0x2C4; saved by 0x005641b0, loaded by 0x005643e0).
# Floats marked "NaN" start as 0x7FC00000 (not set).
struct C_HumanAIMemory size=0x110
  0x00 float unknown00[3]           # a step aside from the others (the AI walks it while > 0.1 m)
  0x10 bool unknown10
  0x20 uint32_t unknown20[3]
  0x40 uint32_t unknown40[3]
  0x70 uint32_t unknown70           # float bits, NaN
  0x80 uint32_t unknown80           # float bits, NaN
  0xe0 int unknownE0
  0xe4 uint16_t unknownE4
  0xe8 C_Human* owner
  0xec int unknownEC
  0xf0 int unknownF0
  0x100 int unknown100              # -1 by default
  0x104 bool unknown104
  0x105 bool unknown105
  0x106 bool unknown106
  0x109 bool unknown109
  0x10a bool unknown10A
end

# Command for S_HumanBody::Execute (0x005abd90; C_Human::ExecuteBodyCommand 0x005a45f0).
class S_BodyCommand size=0x30 vtable=0x810b48:1
  0x04 int unknown04
  0x08 int kind                     # E_BodyCommand: 0 play, 1 work animation, 2 stance, 4 action, 5 pose, 6 interact
  0x0c int arg0
  0x10 int arg1
  0x14 void* arg2
  0x20 uint8_t data[16]
end

# Holster models: items worn on the body (holsters, pouches, grenades) are frames of a holster
# set model ("<nation>_vyb*.i3d") linked to the joints, shown while the human carries them.
struct S_HolsterSlot size=0x18
  0x00 int itemCount                # items of the slot carried
  0x04 int frameCount               # frames found in the holster set (at most 4)
  0x08 I3D_frame* frames[4]
end

# Human models (file names without path / extension) and their holster sets (0x0084f810).
struct S_HolsterModel size=0xc
  0x00 char* model
  0x04 int set                      # g_holsterSets index, -1 none
  0x08 int nation                   # 0 British, 1 German, 2 Russian, 3 Japanese, 4 Italian, 5 US, 6 civilian
end

# Frames of a holster set and the slot they show (0x0084fbd8, ends with slot 14).
struct S_HolsterDummy size=0x8
  0x00 int slot
  0x04 char name[4]
end

# The item models on a human's body (S_HumanBody::equipment, 0x230 bytes; 0x0078cd20..0x007965e0).
# Places: 0 head, 1 backpack, 2 long gun, 3 hands; 4..9 special objects carried in the hands
# (one model at +0x1C).
struct C_HumanEquipment size=0x230
  0x00 C_Human* m_owner
  0x04 C_Human* m_giveTo            # hand-over: the human whose place m_giveToPlace gets the model
  0x08 int m_giveToPlace
  0x0c C_Human* m_takeFrom          # hand-over: the human whose place m_takeFromPlace loses / gets it
  0x10 int m_takeFromPlace
  0x14 bool m_takeFromAttach
  0x18 I3D_frame* m_handsModel
  0x1c I3D_frame* m_specialModel
  0x20 I3D_frame* m_longGunModel
  0x24 I3D_frame* m_headModel
  0x28 I3D_frame* m_backpackModel
  0x2c bool m_unknown2c
  0x30 int m_handsUid
  0x34 int m_handsTypeId
  0x38 int m_backpackTypeId
  0x3c int m_longGunTypeId
  0x40 int m_longGunUid
  0x44 int m_headUid
  0x48 int m_backpackUid
  0x4c bool m_handsShown
  0x50 uint32_t m_flags             # 1: the hands item is put away for an action, 2: event 0 pending
  0x54 int m_specialTime            # -10000 none; -1..-9 where the special object came from, > 0 actor net id
  0x58 I3D_frame* m_divingGear[4]   # clones of the diving gear models of the backpack item
  0x68 I3D_frame* m_flashlight      # "m_SpecFrameLight": I3D_light of the special object
  0x6c int m_flashlightTime         # -10000 off
  0x70 int m_lensFlareTime          # -10000 off
  0x74 I3D_frame* m_jointD          # joint "d": long gun on the back
  0x78 I3D_frame* m_jointF          # joint "f": head
  0x7c I3D_frame* m_backpackJoint   # joint "d" too
  0x80 I3D_frame* m_jointU
  0x84 I3D_frame* m_jointQ
  0x90 S_quat m_specialRot
  0xa0 S_vector m_specialPos
  0xb0 int m_specialPlace           # 11 none
  0xb4 I3D_frame* m_handDummy1      # "HW1"
  0xb8 I3D_frame* m_handDummy2      # "HW2"
  0xbc float m_longGunTilt          # rad; the long gun on the back tilts / slides while lifted
  0xc0 float m_longGunSlide
  0xc4 float m_longGunMaxTilt
  0xc8 float m_longGunMaxSlide
  0xcc int m_longGunLift            # 1: lifted (set by the movement)
  0xd0 float m_unknownD0            # special object animation speed (place 9)
  0xd4 int m_handsTime              # -10000; 700 when the hands model is set
  0xd8 S_HolsterSlot m_holsters[14]
end

# Animation / work / equipment objects of a human (C_Human +0x98).
struct S_HumanBody size=0x18
  0x00 C_Human* owner
  0x04 int group                    # C_Human::m_group
  0x08 C_HumanMovement* movement    # 0xDD0 bytes (0x00785a20)
  0x0c C_HumanWork* work            # 0xE0 bytes (0x0077e1e0)
  0x10 C_HumanEquipment* equipment
  0x14 bool crippled                # three hits in the legs
end

# Base of the controllers (player, AI, network proxy) that drive a human; C_Human::m_controller.
# 0xC0 bytes, 16-byte aligned (src/game/human_controller.{h,cpp}, ported).
class C_HumanController size=0xc0 vtable=0x810fb8:67
  0x10 S_vector4 m_viewDir
  0x20 S_vector4 m_facingDir
  0x30 S_vector4 m_lookAt           # the walk asked of the body (C_HumanWork::SetLookAt)
  0x40 C_HumanEvents* m_events
  0x44 C_Human* m_human
  0x48 int m_type                   # E_HumanController
  0x4c uint32_t m_stance            # MOVE_STANCE_MASK at Attach
  0x50 uint32_t m_moveFlags         # MOVE_*: stance bits 0-2, movement 0x08-0xF8, AI mode 0x700, paces 0x1F000, 0x40000 swimming, 0x4000000 unconscious, 0x40000000 deaf
  0x54 uint32_t m_controlBits       # the control state replicated in network games (EncodeControlBits)
  0x58 float m_walkSpeed            # the controls' walk (0: 10)
  0x5c uint32_t m_stateFlags        # the pace asked for (MOVE_PACE_*)
  0x60 int m_interaction            # E_HumanCommand going on, -1 none
  0x64 int m_interactionTime        # ms until it completes
  0x68 C_Actor* m_interactionTarget
  0x6c int m_interactionArg         # self-healing: the medkit's uid
  0x70 bool m_spectating            # player and remote controllers
  0x74 float* m_axes                # the controls (64 floats)
  0x78 float* m_buttons             # (64 floats)
  0x7c void* m_netObject            # C_NetObject of the human (network games)
  0x80 std::vector<void*> m_toServer    # S_ControllerMessage* queued for the server
  0x90 std::vector<void*> m_toClients   # ... for the clients
  0xa0 C_Checkpoints* m_checkpoints
  0xa4 S_Checkpoint* m_currentCheckpoint  # nearest walking point at Attach
  0xa8 C_Item* m_handsItem          # UpdateHandsItem (0x00584830)
  0xac bool m_unknownAC
  0xad bool m_weaponRaised
  0xb0 int m_quietTime              # ms since the human last made a noise of his own
  0xb4 bool m_unknownB4
  0xb5 bool m_alarmRaised           # an enemy raised the alarm when it took control
  0xb8 int m_speechDelay            # ms until the next Say (0x005882f0)
  0xbc int m_unknownBC
  vslot 0x00 ControlVehicle | void __thiscall ControlVehicle(C_HumanController *this)
  vslot 0x01 AdjustAim | bool __thiscall AdjustAim(C_HumanController *this, S_vector *dir)
  vslot 0x02 RequestAction | void __thiscall RequestAction(C_HumanController *this, int action, S_vector4 *pos, S_vector4 *dir, float strength, int ammoUid, int rounds)
  vslot 0x03 EndInteraction | void __thiscall EndInteraction(C_HumanController *this, bool completed)
  vslot 0x04 Heal | void __thiscall Heal(C_HumanController *this, int humanId, int itemUid, int health, bool removeItem)
  vslot 0x05 UseMedkit | bool __thiscall UseMedkit(C_HumanController *this, int humanId, int itemUid)
  vslot 0x06 ScalarDeletingDestructor | C_HumanController * __thiscall ScalarDeletingDestructor(C_HumanController *this, uchar flags)
  vslot 0x07 Attach | void __thiscall Attach(C_HumanController *this, C_Human *human, C_HumanController *previous)
  vslot 0x08 Detach | void __thiscall Detach(C_HumanController *this)
  vslot 0x09 Update | void __thiscall Update(C_HumanController *this, int frameTime)
  vslot 0x0a Tick | void __thiscall Tick(C_HumanController *this, int timeDelta)
  vslot 0x0b SaveGame | void __thiscall SaveGame(C_HumanController *this, C_chunk *chunk)
  vslot 0x0c LoadGame | void __thiscall LoadGame(C_HumanController *this, C_chunk *chunk)
  vslot 0x0d TestSightOfMeleeTracker | void __thiscall TestSightOfMeleeTracker(C_HumanController *this, C_Actor *meleeTracker, int *status, float *visibility)
  vslot 0x0e TestSightOfHuman | void __thiscall TestSightOfHuman(C_HumanController *this, C_Human *target, int *status, float *visibility)
  vslot 0x0f OnActorNear | void __thiscall OnActorNear(C_HumanController *this, C_Actor *actor, float distance)
  vslot 0x10 OnActorGone | void __thiscall OnActorGone(C_HumanController *this, C_Actor *actor, float distance)
  vslot 0x11 RequestLeaveVehicle | void __thiscall RequestLeaveVehicle(C_HumanController *this)
  vslot 0x12 GetFacingDir | S_vector * __thiscall GetFacingDir(C_HumanController *this)
  vslot 0x13 GetViewDir | S_vector * __thiscall GetViewDir(C_HumanController *this)
  vslot 0x14 SetFacingDir | void __thiscall SetFacingDir(C_HumanController *this, S_vector4 *dir)
  vslot 0x15 SetViewDir | void __thiscall SetViewDir(C_HumanController *this, S_vector4 *dir)
  vslot 0x16 AdjustToPathHeight | bool __thiscall AdjustToPathHeight(C_HumanController *this, S_vector *pos)
  vslot 0x17 Unknown5C | bool __thiscall Unknown5C(C_HumanController *this, float *intensity, float *reference)
  vslot 0x18 SetSpectating | void __thiscall SetSpectating(C_HumanController *this, bool on)
  vslot 0x19 IsSpectating | bool __thiscall IsSpectating(C_HumanController *this)
  vslot 0x1a OnUniformChanged | void __thiscall OnUniformChanged(C_HumanController *this, int uid)
  vslot 0x1b RequestPickUpItem | void __thiscall RequestPickUpItem(C_HumanController *this, int actorId, int container)
  vslot 0x1c RequestThrowItem | void __thiscall RequestThrowItem(C_HumanController *this, int uid, S_vector *velocity)
  vslot 0x1d RequestMoveItem | bool __thiscall RequestMoveItem(C_HumanController *this, int uid, int otherId, int fromContainer, int toContainer, int slot)
  vslot 0x1e RequestTransferItem | bool __thiscall RequestTransferItem(C_HumanController *this, int uid, int arg, int otherId, int otherUid, int container)
  vslot 0x1f RequestTakeFromBox | void __thiscall RequestTakeFromBox(C_HumanController *this, int boxId, int index)
  vslot 0x20 OnBodyFlags | void __thiscall OnBodyFlags(C_HumanController *this, uint flags)
  vslot 0x21 OnDeath | void __thiscall OnDeath(C_HumanController *this)
  vslot 0x22 OnKilled | void __thiscall OnKilled(C_HumanController *this)
  vslot 0x23 OnKnockedOut | void __thiscall OnKnockedOut(C_HumanController *this)
  vslot 0x24 SetUnconscious | void __thiscall SetUnconscious(C_HumanController *this)
  vslot 0x25 OnKilledEnemy | void __thiscall OnKilledEnemy(C_HumanController *this, C_Human *victim)
  vslot 0x26 OnEnemySeen | void __thiscall OnEnemySeen(C_HumanController *this, C_Human *human)
  vslot 0x27 OnEnemyLost | void __thiscall OnEnemyLost(C_HumanController *this, C_Human *human)
  vslot 0x28 OnBodySeen | void __thiscall OnBodySeen(C_HumanController *this, C_Human *human)
  vslot 0x29 GetLastTarget | C_Actor * __thiscall GetLastTarget(C_HumanController *this)
  vslot 0x2a OnBodyHeard | void __thiscall OnBodyHeard(C_HumanController *this, C_Actor *source)
  vslot 0x2b OnFriendHeard | void __thiscall OnFriendHeard(C_HumanController *this, C_Actor *source)
  vslot 0x2c OnLoudSound | void __thiscall OnLoudSound(C_HumanController *this, C_Actor *source)
  vslot 0x2d OnSoundHeard | void __thiscall OnSoundHeard(C_HumanController *this, S_RememberedSound *sound)
  vslot 0x2e OnShotAt | void __thiscall OnShotAt(C_HumanController *this, C_Actor *shooter, S_vector *velocity)
  vslot 0x2f OnHit | void __thiscall OnHit(C_HumanController *this, int type, int zone, int health, C_Actor *attacker, S_vector *pos, S_vector *dir)
  vslot 0x30 OnMessage | void __thiscall OnMessage(C_HumanController *this, C_BitStream *msg)
  vslot 0x31 OnMessage2 | void __thiscall OnMessage2(C_HumanController *this, C_BitStream *msg)
  vslot 0x32 OnCollisionEx | bool __thiscall OnCollisionEx(C_HumanController *this, C_Actor *other, S_vector *pos, int a3, int a4, int a5, int a6, int a7)
  vslot 0x33 SetAIMode | void __thiscall SetAIMode(C_HumanController *this, int mode)
  vslot 0x34 SetMoveFlags | void __thiscall SetMoveFlags(C_HumanController *this, uint flags)
  vslot 0x35 GetWaypoints | void * __thiscall GetWaypoints(C_HumanController *this)
  vslot 0x36 IssueCommand | void __thiscall IssueCommand(C_HumanController *this, S_HumanCommand *command)
  vslot 0x37 ExecuteCommand | bool __thiscall ExecuteCommand(C_HumanController *this, S_HumanCommand *command)
  vslot 0x38 UseActor | bool __thiscall UseActor(C_HumanController *this, S_HumanCommand *command)
  vslot 0x39 LeaveVehicle | void __thiscall LeaveVehicle(C_HumanController *this)
  vslot 0x3a UnknownE8 | int __thiscall UnknownE8(C_HumanController *this)
  vslot 0x3b GetAimBonus | float __thiscall GetAimBonus(C_HumanController *this)
  vslot 0x3c Freeze | void __thiscall Freeze(C_HumanController *this, bool on)
  vslot 0x3d HandleEvent | S_HumanEvent * __thiscall HandleEvent(C_HumanController *this, S_HumanEvent *event)
  vslot 0x3e SendRemoteRequest | void __thiscall SendRemoteRequest(C_HumanController *this, int request)
  vslot 0x3f FlushMessages | void __thiscall FlushMessages(C_HumanController *this, bool toClients)
  vslot 0x40 IsSurrendered | bool __thiscall IsSurrendered(C_HumanController *this)
  vslot 0x41 Unknown104 | bool __thiscall Unknown104(C_HumanController *this)
  vslot 0x42 Unknown108 | bool __thiscall Unknown108(C_HumanController *this)
end

# --- AI controllers (src/game/ai_controller.*) --------------------------------------------

# A waypoint of an AI's route (0x30 bytes, from the pool at 0x008ab188).
struct S_AIWaypoint size=0x30
  0x00 S_vector4 pos
  0x10 S_vector4 pos2
  0x20 int checkpoint               # the walking point, -1 none
  0x24 uint16_t flags               # the point's flags & 0xFB40
  0x26 uint8_t type                 # the point's type
  0x28 float radius                 # the point's radius in m
end

# Where an AI looks (C_AIController +0x124; 0x60 bytes, 0x0055d860): the body's facing and
# the view direction, which stays within a cone around the facing.
struct S_AIFacing size=0x60
  0x00 S_vector4 facing
  0x10 S_vector4 view
  0x20 S_vector4 lookDir            # a direction to look around towards (zero: none)
  0x30 float facingX                # the facing's normalized x / z
  0x34 float facingZ
  0x38 float viewX                  # the view's normalized x / z
  0x3c float viewZ
  0x40 uint16_t locks               # 1: the facing stays, 2: the view stays
  0x44 float cone                   # cosine of the view's cone around the facing (0.7; lying 0.94)
  0x48 int lookPeriod               # ms between looks around (-1: none)
  0x4c int lookTimer
  0x50 I3D_frame* lookAt            # a frame to keep looking at
end

# A fire order an AI carries out (C_AIController +0x128; 0x50 bytes, 0x005607d0).
struct S_AIFireOrder size=0x50
  0x00 S_vector4 unknown00
  0x10 S_vector4 targetPos          # fire at a position (no target)
  0x20 S_vector4 throwPos           # where a grenade is to be thrown
  0x30 C_Actor* target
  0x34 bool reload                  # asked to reload
  0x35 bool throwGrenade            # asked to throw a grenade at throwPos
  0x38 uint32_t result              # what the last update did
  0x3c int burstMin                 # ms of a burst (-1: default)
  0x40 int burstMax
  0x44 int pauseMin                 # ms between bursts (-1: default)
  0x48 int pauseMax
end

# Where an AI moves (C_AIController +0x12C; 0x60 bytes, inline in the constructor).
struct S_AIMoveOrder size=0x60
  0x10 S_vector4 pos
  0x20 uint16_t checkpoint          # -1 none
  0x24 int unknown24                # ms counted down
  0x28 int unknown28                # steps without getting nearer the waypoint
  0x2c float unknown2C              # the nearest it got (FLT_MAX)
  0x30 int unknown30                # the movement stance put aside (-1)
  0x34 int unknown34                # ms to wait
  0x38 int unknown38                # SetFlee minDist (6)
  0x3c int unknown3C                # SetFlee safeDist (40)
  0x40 C_Actor* door
  0x44 C_Actor* ladder
  0x48 uint16_t ladderFrom
  0x4a uint16_t ladderTo
  0x4c int unknown4C                # the result (E_MoveResult)
  0x50 uint32_t flags
  0x54 uint32_t unknown54           # the pace (MOVE_PACE_*)
  0x58 bool unknown58
end

# A point of the AI driver's road path (0x30 bytes).
struct S_AIPathPoint size=0x30
  0x00 S_vector4 pos
  0x10 S_vector4 dir
  0x20 bool last
end

# Drives a car along the road network (C_AIController +0x130; 0xA0 bytes, 0x0055df90).
struct C_AIDriver size=0xa0
  0x00 std::vector<S_AIPathPoint> path    # 16-byte aligned storage
  0x10 uint16_t pathIndex
  0x14 float progress               # along the segment (0..1)
  0x18 float nearestDist2           # 1e8
  0x1c bool reversing
  0x1d bool moving
  0x20 int stuckTime                # ms
  0x24 int noProgressTime           # ms
  0x30 S_vector4 lastPos
  0x40 uint32_t flags
  0x50 S_vector4 target
  0x60 S_vector4 targetDir          # NaN: none
  0x70 C_CheckpointNetwork* roads
  0x74 C_Actor* vehicle
  0x78 std::vector<void*> unknown78     # _aligned_malloc'd entries
  0x88 bool unknown88
  0x8c float speed                  # -1
  0x90 uint32_t pace                # MOVE_PACE_*
  0x94 int label
end

# How alert an AI is (C_AIController +0x134; 8 bytes).
struct S_AIAlertState size=0x8
  0x00 int state                    # 1, 2, 4 (not identified)
  0x04 bool changed
end

# What an AI fires at (C_AIController +0xC0; 0x30 bytes, 0x00576ac0).
struct S_AIFire size=0x30
  0x00 bool unknown00
  0x04 int burstTime                # ms left of the burst
  0x08 int pauseTime                # ms until the next burst
  0x0c int unknown0C
  0x10 C_Actor* target
  0x14 C_Actor* unknown14
  0x18 uint32_t flags
  0x20 S_vector4 throwPos
end

# A task of an AI (C_AIController +0xF0; 12 types, from the pools at 0x008ab158).
class S_AITask size=0x14 vtable=0x8110e4:3
  0x04 int type
  0x08 int unknown08
  0x0c int unknown0C
  0x10 int unknown10
  vslot 0x00 ScalarDeletingDestructor | S_AITask * __thiscall ScalarDeletingDestructor(S_AITask *this, uchar flags)
  vslot 0x01 SaveGame | bool __thiscall SaveGame(S_AITask *this, C_chunk *chunk)
  vslot 0x02 LoadGame | bool __thiscall LoadGame(S_AITask *this, C_chunk *chunk)
end

# An object pool of the AI (0x18 bytes, a template class with its own vtable per type): the
# free objects in list nodes {object, next}, the spare nodes, the number added when empty.
struct S_AIPool size=0x18
  0x00 void* vtable
  0x04 void* free
  0x08 void* spareNodes
  0x0c int growBy                   # 16
  0x10 int freeCount
  0x14 int unknown14
end

# The AI's object pools (g_aiPools 0x008ab158).
struct S_AIPools size=0x180
  0x000 S_AIPool bodyCommands       # S_BodyCommand (squad orders of type 1)
  0x018 S_AIPool humanOrders        # squad orders of type 4 (0x130 bytes, vtable 0x008105a0)
  0x030 S_AIPool waypoints          # S_AIWaypoint
  0x048 S_AIPool sounds             # S_RememberedSound
  0x060 S_AIPool tasks[12]          # S_AITask types 1 .. 12
end

# Base of the AI controllers (vtable 0x00810e20, 70 slots; 0x138 bytes, 16-byte aligned).
class C_AIController : C_HumanController size=0x140 vtable=0x810e20:70
  0xc0 S_AIFire m_fire
  0xf0 S_AITask* m_task
  0xf4 std::vector<S_AIWaypoint*> m_waypoints
  0x104 S_AIWaypoint* m_waypoint
  0x108 int m_lookAroundTime        # ms
  0x10c int m_unknown10C            # counts up
  0x110 int m_unknown110
  0x114 int m_unknown114            # -1
  0x118 int m_unknown118            # -1
  0x11c bool m_ignoreCircumvention
  0x120 int m_unknown120            # ms until the next climb (network games)
  0x124 S_AIFacing* m_facing
  0x128 S_AIFireOrder* m_fireOrder
  0x12c S_AIMoveOrder* m_move
  0x130 C_AIDriver* m_driver
  0x134 S_AIAlertState* m_alert
  vslot 0x43 ReleaseTask | void __thiscall ReleaseTask(C_AIController *this)
  vslot 0x44 PlanGrenade | bool __thiscall PlanGrenade(C_AIController *this)
  vslot 0x45 Unknown114 | bool __thiscall Unknown114(C_AIController *this, S_vector *dir)
end

# An entry of a squad member's order queue (C_SquadController +0x24C; game/squad_order.h).
# Type 1 is an S_BodyCommand (0x30 bytes, the same leading fields); type 4 is this (0x130
# bytes, vtable 0x008105a0 with six no-op slots, from the AI pools: the command map's orders,
# with a marker frame and a pickable volume in the world).
struct S_HumanOrder size=0x130
  0x00 void* vtable                 # 0x008105a0
  0x04 int type                     # 1 body command, 4 this
  0x08 C_Human* human               # whose order it is
  0x0c int kind                     # E_HumanOrderKind (1 move, 2 watch, 3 act, 4 follow, 6, 7)
  0x10 int arg                      # kind 3: the command module's command
  0x20 S_vector4 data               # the direction
  0x30 S_vector4 pos
  0x40 C_Actor* actor               # the actor the order is about
  0x44 uint32_t flags               # pace | stance | AI mode
  0x48 bool done
  0x49 bool started                 # its task is set
  0x4a bool selected                       # the command map's chosen order (the info selector)
  0x50 S_vector4 pathFrom           # 100000: none
  0x60 S_vector4 pathTo             # 100000: none
  0x70 int unknown70                # kind 3: passed to the module
  0x74 S_Vector4Array path          # the map's path preview
  0x84 I3D_frame* marker
  0x90 S_CollisionVolume volume     # flags 0x40, type 0x8000, a 0.4 x 1.6 x 0.4 box
  0xf0 S_matrix invMatrix
end

# The squad member's current action (C_SquadController +0x1E0; 0x48 bytes, set by 0x00534340):
# walk to `pos` (or the target), turn, then the command `type` (E_HumanCommandType values and
# the AI's own, E_SquadAction) on `target`.
struct S_SquadAction size=0x48
  0x00 S_vector4 pos
  0x10 S_vector4 dir
  0x20 bool arrived
  0x21 bool turned
  0x22 bool finished                # no action going on
  0x23 bool keepGoing
  0x24 bool noCommand               # only go there
  0x25 bool started
  0x26 bool completed
  0x27 bool skipTurn
  0x28 bool notifyModule
  0x29 bool useDir
  0x2c int targetId                 # the target's network id, -1 none
  0x30 int slot                     # the seat / the box's item
  0x34 int delay                    # ms
  0x38 uint32_t stance              # at the end (MOVE_STANCE_MASK, 0 none)
  0x3c float radius
  0x40 int type
  0x44 C_Actor* target
end

# Single player squad members (vtable 0x008109f8, 83 slots; 0x260 bytes, 0x00524c60;
# game/human_controller.h, squad_*.cpp).
class C_SquadController : C_AIController size=0x260 vtable=0x8109f8:83
  0x140 S_vector4 m_engagePos
  0x150 S_vector4 m_boundDir
  0x160 S_vector4 m_holdDir         # the facing kept when idle
  0x170 uint32_t m_pace                 # MOVE_PACE_*
  0x174 uint32_t m_flags
  0x178 uint32_t m_orderStance
  0x17c uint32_t m_coverStance
  0x180 uint32_t m_orderPace
  0x184 int m_idleCheckpoint
  0x188 bool m_quiet
  0x189 bool m_following
  0x18a bool m_silentMode
  0x18b bool m_lyingToFire
  0x18c bool m_steppedAside
  0x18d bool m_ordersHeld
  0x18e bool m_restartOrder
  0x190 int m_boundHops
  0x194 int m_engageTime            # ms
  0x198 int m_eventTime             # ms
  0x19c int m_alertTime             # ms
  0x1a0 int m_weaponCheckTime       # ms
  0x1a4 int m_formationTime         # ms
  0x1a8 int m_fireMoveTime          # ms (< 0 stopped to fire)
  0x1ac int m_airCheckTime          # ms
  0x1b0 int m_followDelay           # ms
  0x1b4 int m_statusIconTime        # ms
  0x1b8 int m_modeIconTime          # ms
  0x1bc int m_ordersChangedTime
  0x1c0 float m_coverRadius
  0x1c4 float m_friendlyFire
  0x1c8 int m_statusIcon
  0x1cc int m_tempStatusIcon        # -1
  0x1d0 int m_modeIcon
  0x1d4 int m_tempModeIcon          # -1
  0x1d8 int m_combatMode
  0x1dc int m_unknown1DC
  0x1e0 S_SquadAction m_action
  0x228 int m_unknown228
  0x22c int m_unknown22C
  0x230 bool m_gunnerLooksAhead
  0x234 int m_selfHealTime          # ms
  0x238 C_Human* m_pickedTarget
  0x23c C_Actor* m_airTarget
  0x240 I3D_frame* m_airTargetFrame
  0x244 bool m_mineNear
  0x248 int m_mineCheckTime         # ms
  0x24c std::vector<S_HumanOrder*> m_orders
  vslot 0x46 GetFlags | uint __thiscall GetFlags(C_SquadController *this)
  vslot 0x47 AddOrder | void __thiscall AddOrder(C_SquadController *this, S_HumanOrder *order)
  vslot 0x48 InsertOrder | void __thiscall InsertOrder(C_SquadController *this, S_HumanOrder *order, int index)
  vslot 0x49 SetOrder | void __thiscall SetOrder(C_SquadController *this, int index, S_HumanOrder *order)
  vslot 0x4a CancelOrders | void __thiscall CancelOrders(C_SquadController *this, bool turn)
  vslot 0x4b RemoveOrder | void __thiscall RemoveOrder(C_SquadController *this, int index, bool turn)
  vslot 0x4c HaveOrdersChanged | bool __thiscall HaveOrdersChanged(C_SquadController *this)
  vslot 0x4d GetOrderCount | int __thiscall GetOrderCount(C_SquadController *this)
  vslot 0x4e GetOrder | S_HumanOrder * __thiscall GetOrder(C_SquadController *this, int index)
  vslot 0x4f GetStatusIcon | int __thiscall GetStatusIcon(C_SquadController *this)
  vslot 0x50 GetModeIcon | int __thiscall GetModeIcon(C_SquadController *this)
  vslot 0x51 IsQuiet | bool __thiscall IsQuiet(C_SquadController *this)
  vslot 0x52 GetCombatOrder | int __thiscall GetCombatOrder(C_SquadController *this)
end

# The local player's input (vtable 0x00811848; not ported). The fields are the ones Ghidra's
# analysis found when its functions were filed under C_EnemyController (whose vtable it read as
# running on into this one); some below 0x140 may be the enemy's.
# The body action of CTRL_MSG_ACTION and the player's pending squad command: a command of
# category 3 (vtable 0x810528, the no-op methods of S_HumanCommand; 0x40 bytes with padding).
struct S_ActionCommand size=0x40
  0x00 void* vtable
  0x04 int category
  0x08 int unknown08
  0x0c int unknown0C
  0x10 S_vector4 dir
  0x20 int action                   # E_SquadCommand (0x21 / 0x22 the radios, 0x35 / 0x38 a command on an actor)
  0x24 int command                  # single player only: 0x35 / 0x38's E_HumanCommandType
  0x28 C_Actor* actor               # ... its actor
  0x2c int value                    # ... its value (a supply box's item)
  0x30 C_Human* member              # the member it is for (null: the squad)
end

# The local player's controller, also the server's copy of a network player
# (src/game/player_controller.cpp, player_weapons.cpp, player_net.cpp).
class C_PlayerController : C_HumanController size=0x560 vtable=0x811848:67
  0x0c0 bool m_whispering           # the squad whispers (the command module)
  0x0c4 float m_controlAxes[64]     # the controls of the tick (copied / replicated)
  0x1c4 float m_controlButtons[64]
  0x2c4 float m_unknown2C4[64]      # (only cleared)
  0x3c4 float m_unknown3C4[64]
  0x4c4 int m_latchThrow            # the weapon keys pressed since the last tick
  0x4c8 int m_latchFire
  0x4cc int m_latchFire2
  0x4d0 int m_latchReload
  0x4d4 bool m_holdingBreath
  0x4d8 int m_breath                # ms (3500 full)
  0x4e0 S_ActionCommand m_pendingCommand
  0x520 int m_commandDelay          # ms until the spoken command is carried out
  0x524 bool m_unknown524           # (only saved and loaded)
  0x525 bool m_ordersChanged
  0x528 uint32_t m_squadFlags       # C_SquadController::FLAG_* of the member taken over
  0x52c int m_viewMode              # VIEW_*: 0 normal, 1 binoculars, 2 iron sights, 3 scope
  0x530 int m_scopeDelay            # ms (a reload in the scope)
  0x534 int m_raisedTime            # ms the weapon stays raised after use
  0x538 bool m_charging             # a throw being charged
  0x53c int m_chargeTime            # ms (1 s at most)
  0x540 int m_throwDelay            # ms after a throw
  0x544 int m_chargeAction          # the throw's body action (6, 7, 0xD)
  0x548 int m_squadCombatOrder      # E_SquadCombatOrder
  0x550 bool m_vip                  # objectives games
end

# A network client's copy of another player's human (src/game/remote_controller.cpp).
class C_RemoteController : C_HumanController size=0x2d0 vtable=0x8112d8:67
  0x0c0 float m_controlAxes[64]
  0x1c0 float m_controlButtons[64]
  0x2c0 bool m_vip
  0x2c1 bool m_surrendered          # CTRL_MSG_23
end

# The enemy AI (src/game/enemy_*.cpp). m_event (+0x148) is a search task's event; a guard
# task going to a static gun keeps the gun (C_Actor*) in the same field.
class C_EnemyController : C_AIController size=0x1e0 vtable=0x811730:70
  0x140 bool m_targetChanged        # the gun turned to a new target (fires the next tick)
  0x144 uint32_t m_enemyFlags       # 1 brave, 2 steady, 4 done, 8 no OnAlarmDone, 0x10 gunner, 0x20 cannon
  0x148 S_HumanEvent* m_event
  0x14c int m_unknown14C
  0x150 int m_unknown150
  0x154 bool m_unknown154
  0x158 std::vector<uint16_t> m_homePoints
  0x168 std::vector<uint16_t> m_homeCover
  0x180 S_vector4 m_homePos
  0x190 int m_homeCheckpoint
  0x194 float m_homeRadius
  0x198 int m_taskTime              # ms
  0x19c C_Human* m_target
  0x1a0 float m_aimHeight
  0x1a4 int m_alertRange            # m
  0x1a8 int m_alertTime             # ms
  0x1ac int m_grenadeTime           # ms
  0x1b0 int m_fireWait              # ms
  0x1b4 std::vector<C_Actor*> m_guns
  0x1c4 int m_gunCheckTime          # ms
  0x1c8 int m_moraleCheckTime       # ms
  0x1cc int m_coverTime             # ms
  0x1d0 int m_unarmedTime           # ms
  0x1d4 bool m_lowAmmo
  0x1d8 int m_surrenderTime         # mission time, -1 none
  0x1dc int m_enemySeenTime         # ms
end

# AttackMoveTo's attack (C_ScriptedController::S_Attack in src/game/human_controller.h).
struct S_ScriptAttack size=0x14
  0x00 bool active
  0x04 uint32_t state               # 0x10 decide, 0x40 firing, 0x80 stop
  0x08 int time                     # ms
  0x0c uint32_t stance              # MOVE_STANCE_MASK while firing (0: the script's)
  0x10 C_Actor* target
end

# The AI the mission script drives (src/game/scripted_controller.cpp).
class C_ScriptedController : C_AIController size=0x1f0 vtable=0x811500:70
  0x140 S_vector4 m_alarmPos
  0x150 char m_animation[56]        # SetAnimation's looped animation (empty: none)
  0x188 int m_animationFrame
  0x18c bool m_waitForPlayer
  0x18d bool m_watchPlayer
  0x190 float m_stopDist2
  0x194 float m_resumeDist2
  0x198 S_ScriptAttack m_attack
  0x1ac C_Script* m_script
  0x1b0 uint32_t m_scriptFlags      # MOVE_STANCE_MASK and MOVE_PACE_* of the moves
  0x1b4 uint32_t m_alarmType        # the last alarm (ALARM_*)
  0x1b8 uint32_t m_ignoredAlarms    # ALARM_* switched off
  0x1bc bool m_holdingObject
  0x1bd bool m_fireFromSeat
  0x1be uint8_t m_unknown1BE        # (two bytes, only saved)
  0x1bf uint8_t m_unknown1BF
  0x1c0 int m_fireTime              # ms
  0x1c4 C_Actor* m_fireTarget
  0x1c8 I3D_frame* m_fireFrame
  0x1cc I3D_frame* m_turnTarget
  0x1d0 bool m_turning
  0x1d1 bool m_seated
  0x1d2 bool m_suspended
  0x1d4 int m_carryMode
  0x1d8 int m_label
  0x1dc int m_fireLabel
  0x1e0 C_FormationShape* m_formation
end

# Civilians and allies (src/game/scripted_controller.cpp; no reactions of their own).
class C_NeutralController : C_AIController size=0x140 vtable=0x8113e8:70
end

# The sniper AI (src/game/sniper_controller.cpp; the C_Controller8 of earlier passes).
class C_SniperController : C_AIController size=0x180 vtable=0x811618:70
  0x140 S_vector4 m_homePos
  0x150 uint32_t m_startStance      # the stance at Attach (only saved)
  0x154 uint32_t m_sniperFlags      # 1 brave, 2 steady, 4 done, 8 no OnAlarmDone
  0x158 int m_taskTime              # ms
  0x15c C_Human* m_target
  0x160 std::vector<uint16_t> m_points
  0x170 std::vector<uint16_t> m_sniperPoints
end


# What the AI of a human perceived (0x40 bytes, 16-aligned, from the pool at 0x008ac430).
struct S_HumanEvent size=0x40
  0x00 uint32_t type                # kind; top nibble: priority 1..4 (0x10000016: hit, sourceId = attacker)
  0x04 int timeLeft                 # ms (raised events: 10000)
  0x08 int age                      # ms
  0x0c int actorId                  # actor id (0x005aca30), -1 none
  0x10 int sourceId                 # the actor that caused it, -1 none
  0x20 S_vector4 pos
  0x30 int unknown30
end

# MSVC 6 std::deque iterator / std::deque<S_HumanEvent*> (blocks of 0x1000 bytes).
struct S_DequeIterator size=0x10
  0x00 void* blockFirst
  0x04 void* blockLast
  0x08 void* next
  0x0c void* mapEntry
end
struct S_HumanEventDeque size=0x30
  0x04 S_DequeIterator first
  0x14 S_DequeIterator last
  0x24 void* map
  0x28 int mapSize
  0x2c int size
end

# A sentence of a speech set: its samples (sound codes 9S VV NNNN) in a shuffled order.
struct S_SpeechSentence size=0x1c
  0x04 std::vector<int> samples
  0x10 void* cursor                 # int* into samples
  0x14 uint32_t nextTime            # mission time before which it is not said again
  0x18 bool enabled                 # the set has samples for it
end

# One set of Tables\speech.set, shared by the humans that use it (refcounted).
struct C_SpeechSet size=0xf24
  0x00 void* m_unknown00            # freed by the destructor, never set
  0x04 int m_id                     # index in Tables\speech.set
  0x08 S_SpeechSentence m_sentences[138]
  0xf20 int m_refCount
end

# The speech sets loaded so far (singleton 0x008ac448).
struct C_SpeechSets size=0x424
  0x04 std::vector<C_SpeechSet*> m_sets
  0x10 C_chunk m_file               # Tables\speech.set while Acquire loads a set
end

# What a human's AI is told about (C_Human +0xB4, 0xA0 bytes; 0x005ac950..0x005af110).
struct C_HumanEvents size=0xa0
  0x00 S_vector4 m_sniperPos        # HUMAN_SetSniper: the human stays within m_sniperRadius of it
  0x10 float m_sniperRadius
  0x14 bool m_sniper
  0x18 S_HumanEventDeque m_pending  # raised, handed to the controller by C_Human::ProcessEvents
  0x48 S_HumanEventDeque m_stimuli  # tracked by the AI until they time out (AddStimulus 0x005adee0)
  0x78 std::vector<int> m_nearbyActors   # actor ids in the proximity range (C_Human::OnProximity)
  0x88 std::vector<int> m_unknownActors  # actor ids (only saved / loaded)
end

class C_Human : C_Actor size=0x2d0 vtable=0x8111e0:56
  0x090 bool m_initialized          # StartMission set the human up (StopMission clears it)
  0x094 float m_unknown94           # passed on to the movement (0x00786ab0)
  0x098 S_HumanBody m_body
  0x0b0 C_HumanController* m_controller
  0x0b4 C_HumanEvents* m_events
  0x0b8 S_SoldierProfile* m_profile
  0x0bc C_str m_faceTexture
  0x0c0 S_SoldierProgress m_progress
  0x0e4 int m_stealthTime           # ms towards the next stealth gain
  0x0e8 int m_enduranceTime         # ms towards the next endurance gain
  0x0ec int m_stats[12]             # E_HumanStat
  0x11c int m_initialStats[12]
  0x14c float m_stamina
  0x150 bool m_fadedOut
  0x154 float m_bleeding            # s left
  0x158 int m_bloodDropTime         # ms
  0x15c int m_legHits
  0x160 C_Actor* m_vehicle
  0x164 int m_seat
  0x16c float m_disguise            # single player: 0..100 while wearing nothing of the own side
  0x170 int m_profileIndex          # -1 none
  0x174 C_Actor* m_carried
  0x178 bool m_inventoryChanged
  0x17c int m_actionTime            # ms left
  0x180 int m_actionTotal
  0x184 int m_actionText
  0x188 int m_periodicLink          # -1 none
  0x18c std::vector<void*> m_pendingOrders  # S_HumanOrder* (squad orders not yet handed to the controller)
  0x19c int m_role                  # E_HumanRole
  0x1a0 int m_group
  0x1a4 bool m_carnage
  0x1a5 bool m_unknown1a5           # network games: the uniform cannot be changed
  0x1a6 bool m_hostileToPlayers
  0x1a8 int m_nameTextId            # -1: default name of the role
  0x1ac int m_unknown1ac
  0x1b0 S_vector4 m_velocity        # m/s
  0x1c0 S_vector4 m_prevVelocity
  0x1d0 S_vector4 m_lastPos
  0x1e0 S_vector4 m_scale
  0x200 int m_unknown200
  0x204 int m_deadTime              # ms since death (multiplayer respawn)
  0x208 int m_aliveTime             # ms since spawn (multiplayer spawn protection)
  0x20c bool m_inPlay               # multiplayer: a player's human takes part
  0x210 int m_mpZone                # actor id of the multiplayer zone the human is in
  0x214 char m_chatText[64]
  0x254 int m_chatTime              # ms the chat text stays
  0x258 int m_sightId               # g_sightModule observer
  0x25c int m_sightTargetId         # g_sightModule target
  0x260 I3D_frame* m_bodyFrame
  0x264 std::vector<C_Human*> m_seenEnemies  # hostile humans in view
  0x274 std::vector<C_Human*> m_seenFriends  # friendly humans in view
  0x284 std::vector<C_Human*> m_seenBodies
  0x294 std::vector<C_Human*> m_seenTraitors  # disguised enemies in view
  0x2a4 int m_soundListener         # -1 none
  0x2a8 std::vector<S_RememberedSound*> m_heardSounds
  0x2b8 int m_unknown2b8
  0x2bc int m_unknown2bc
  0x2c0 C_SpeechSet* m_speechSet
  0x2c4 C_HumanAIMemory* m_aiMemory
  0x2c8 void* m_formation           # C_FormationShape this human leads (0xA0 bytes, 0x005949c0)
  0x2cc C_Human* m_leader
end
