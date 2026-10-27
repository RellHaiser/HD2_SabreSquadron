# Core game library types (layouts as in the original binary).

struct C_chunk size=0x414
  0x000 bool m_open
  0x004 int m_handle
  0x008 bool m_writing
  0x00c int m_fileSize
  0x010 int m_stack[256]
  0x410 int m_depth
end

# I3D_* engine interfaces are generated from src/ls3d/*.h by tools/ls3d_ghidra.py.

class C_Module size=0xc vtable=0x8103d8:16
  0x004 C_str m_name
  0x008 int m_id
end

# The application's states (menus, the video player, the game; src/game/game_state.h).
class C_GameState size=0xc vtable=0x8158ac:13
  0x004 int m_state                       # E_GameStateValue
  0x008 int m_pendingState                # 2: none
  vslot 0x00 ScalarDeletingDestructor | C_GameState * __thiscall ScalarDeletingDestructor(C_GameState *this, uchar flags)
  vslot 0x01 Start | bool __thiscall Start(C_GameState *this, void * arg)
  vslot 0x02 Stop | bool __thiscall Stop(C_GameState *this)
  vslot 0x03 HandleInput | bool __thiscall HandleInput(C_GameState *this, float * held, float * pressed)
  vslot 0x04 Tick | void __thiscall Tick(C_GameState *this, int timeDelta)
  vslot 0x05 Update | void __thiscall Update(C_GameState *this, int timeDelta)
  vslot 0x06 Render | void __thiscall Render(C_GameState *this)
  vslot 0x07 GetTypeId | int __thiscall GetTypeId(C_GameState *this)
  vslot 0x08 GetState | int __thiscall GetState(C_GameState *this)
  vslot 0x09 SetState | void __thiscall SetState(C_GameState *this, int state)
  vslot 0x0a GetPendingState | int __thiscall GetPendingState(C_GameState *this)
  vslot 0x0b SetPendingState | void __thiscall SetPendingState(C_GameState *this, int state)
  vslot 0x0c CommitPendingState | void __thiscall CommitPendingState(C_GameState *this)
end

# The video player of a campaign video node (src/game/pending.h; not ported; the fields as the
# auto recovery found them).
class C_VideoState : C_GameState size=0x7c vtable=0x815bfc:13
  0x00c int m_00c                         # -1
  0x010 int m_010                         # -1
  0x014 float m_014
  0x018 float m_018
  0x01c char m_01c
  0x01d bool m_01d
  0x01e bool m_01e
  0x020 void* m_020
  0x024 uint32_t m_024
  0x038 void* m_038
  0x03c uint8_t m_03c
  0x048 int m_048
  0x04c int m_04c
  0x06c uint32_t m_06c
  0x070 int m_070
  0x078 uint8_t m_078
  vslot 0x04 Tick | void __thiscall Tick(C_VideoState *this, int timeDelta)
end

# The logos, the intro and the movies over a state (C_GameApp).
class C_MovieState : C_VideoState size=0x84 vtable=0x815bc8:13
  0x07c int m_sequence                    # what follows (C_GameApp::OnMovieFinished)
  0x080 C_GameState* m_returnState        # sequence 0: the state the movie played over
  vslot 0x04 Tick | void __thiscall Tick(C_MovieState *this, int timeDelta)
end

# A respawn the server sends at its next tick (16-byte aligned).
struct S_PendingSpawn size=0x30
  0x00 S_vector4 pos
  0x10 S_vector4 dir
  0x20 S_SoldierState* state
  0x24 int* stats
  0x28 int slot
end

# The game's collaborators (not ported; opaque).
struct C_QuickMessages size=0x28
end

# The AI's checkpoint networks (src/game/checkpoints.h).
struct S_CheckpointLink size=0x4
  0x00 uint16_t target
  0x02 uint16_t cost                      # 1/256 m
end

struct S_Checkpoint size=0x24
  0x00 S_vector pos
  0x0c int cell                           # GetCell(pos.x); points sorted by cell, then z
  0x10 uint16_t unknown10
  0x12 uint8_t preference                 # links cheaper by preference/1024
  0x13 uint8_t radius                     # 1/16 m
  0x14 uint16_t flags                     # link count 0x3F, CHECKPOINT_*
  0x16 uint8_t type
  0x17 uint8_t cover2                     # cover directions of stance 2
  0x18 uint8_t cover4
  0x19 uint8_t cover1
  0x1a uint8_t unknown1A
  0x1b uint8_t values[4]                  # GetValuePair (n/254, 0xFF none)
  0x1f uint8_t unknown1F
  0x20 S_CheckpointLink* links
end

struct S_CheckpointName size=0x12
  0x00 char name[16]                      # not NUL-terminated at 16 characters
  0x10 uint16_t index
end

struct S_CheckpointFrame size=0x8
  0x00 I3D_frame* frame
  0x04 uint16_t index
end

struct S_CheckpointActor size=0x8
  0x00 C_Actor* actor
  0x04 uint16_t index
end

# The positions FindEscapePath flees from (the AI controller's aligned array).
struct S_Vector4Array size=0x10
  0x00 S_vector4* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end

struct C_CheckpointNetwork size=0x3c
  0x00 uint16_t m_pointCount
  0x04 S_Checkpoint* m_points             # new[] (count before the array)
  0x08 S_CheckpointLink* m_linkStorage
  0x0c uint16_t m_nameCount
  0x10 S_CheckpointName* m_names
  0x14 uint16_t m_frameCount
  0x18 S_CheckpointFrame* m_frames
  0x1c uint16_t m_actorCount
  0x20 S_CheckpointActor* m_actors        # sorted by point
  0x24 uint32_t* m_searchCosts            # the unused Dijkstra search's
  0x28 uint16_t* m_searchPrev
  0x2c std::vector<uint32_t> m_joins      # point pairs (a | b << 16)
end

# The mission's walking network and its road network (C_game +0x54).
class C_Checkpoints : C_CheckpointNetwork size=0x78
  0x3c C_CheckpointNetwork m_roads
end

# More collaborators of the game, opaque (sizes from the highest offsets used, not exact).
# The application's state machine (src/game/game_app.h; g_gameApp 0x008aee24).
struct C_GameApp size=0x44
  0x000 C_str m_name                      # the file of the save / load commands
  0x004 int m_pendingCommand              # E_AppCommand
  0x008 uint8_t m_unknown08
  0x00c int m_unknown0C
  0x010 uint8_t m_unknown10
  0x011 bool m_resume
  0x012 bool m_suspended
  0x014 C_GameState* m_state
  0x018 int m_serverTime                  # ms not yet ticked
  0x01c uint32_t m_randomId
  0x020 bool m_gamePaused                 # the game's pause before its menu opened
  0x024 int m_volumeDelay                 # ms until the profile's volume returns
  0x028 int m_updatePhase                 # 1 client, 2 server
  0x02c uint8_t m_commandLock[24]         # CRITICAL_SECTION
end

struct C_NetGame size=0x5080
end

struct C_LoaderScreen size=0x8
end

struct C_StainManager size=0x10
end

# An entry of the multiplayer map list (GAMEDATA\mpmaplist.txt, parser 0x006189f0; game/pending.h).
struct S_MpMapObjective size=0x18
  0x00 int axisText
  0x04 int alliedText
  0x08 int points                         # value
  0x0c int type                           # type="VIP": 1, else -1
  0x10 int team                           # side: AXIS 0, ALLIED (or none) 1
  0x14 int vipUniform
end

struct S_MpMapItem size=0xc
  0x00 int itemId
  0x04 int count
  0x08 int ammo
end

struct C_MpMap size=0x48
  0x01 C_str m_name                       # (packed)
  0x05 C_str m_directory
  0x0c int m_aircraftAttackAxis           # AIRCRAFTATACK
  0x10 int m_aircraftAttackAllied
  0x14 int m_balanceAxis                  # BALANCED
  0x18 int m_balanceAllied
  0x1c int m_unknown1C
  0x20 int m_unknown20
  0x24 std::vector<S_MpMapObjective> m_objectives
  0x34 std::vector<S_MpMapItem> m_items
  0x44 C_str m_presets                    # ALLOWEDPRESETS
end

# Settings of the network game session (the object 0x008aea30 points to).
struct S_MultiplayerSettings size=0x298
  0x004 int sessionType                   # 0 none, 1 hosting, 2 joining
  0x008 char sessionName[360]
  0x170 bool dedicated
  0x174 int unknown174
  0x178 int unknown178
  0x218 C_str password
  0x220 int port
  0x224 int gameType                      # 0 deathmatch, 1 occupation, 2 objectives, 3 cooperative
  0x228 int maxClients
  0x22c int pointLimit
  0x230 int roundLimit                    # min
  0x234 int roundCount
  0x238 int warmup                        # s
  0x23c int respawnTime                   # s
  0x240 int allowRespawn
  0x244 int friendlyFire
  0x248 int autoTeamBalance
  0x24c int thirdPersonView
  0x250 int spawnProtection
  0x254 int inverseDamage
  0x258 int fallingDamage
  0x264 int allowVehicles
  0x268 int autoRestart
  0x26c int coopDifficulty
  0x270 int coopLives
  0x274 int allowCrosshair
  0x278 int spawnOnStart
  0x27c int teamLives
  0x280 std::vector<void*> maps           # the map list (C_MpMap*)
  0x290 int currentMap
  0x294 int unknown294                    # zeroed by the destructor
end

# Server-side state per player (C_NetGame::LockPlayers).
struct S_NetPlayerState size=0xc4
  0x00 int connection
  0x08 char name[52]                      # empty: a free slot
  0x3c C_Human* human
  0x40 int stats[9]                       # +0x0C the round's score, +0x20 the total
  0x64 int team
  0x68 int pendingTeam
  0xa2 bool left
  0xb0 int livesUsed                      # co-op respawns
end

# The game state of a running mission (src/game/game.h).
class C_game : C_GameState size=0xec vtable=0x8159ac:13
  0x00c bool m_hasQuickSave
  0x010 int m_savesUsed                   # quick saves at difficulty 3
  0x014 bool m_missionLoaded
  0x018 I3D_scene* m_scene
  0x01c int m_mode                        # 0 normal, 1 loading a saved game, 2 quitting to the menu
  0x020 int m_fadeTime
  0x024 int m_endDelay
  0x028 uint32_t m_missionTime
  0x02c uint32_t m_time
  0x030 int m_roundTime
  0x034 bool m_uniformDisguise
  0x035 bool m_noTeamDamage
  0x036 bool m_tacticalMode
  0x038 int m_inputDelay
  0x03c int m_campaignMissionNumber
  0x040 bool m_signalPistolFired
  0x044 std::vector<I3D_model*> m_animatedModels
  0x054 C_Checkpoints* m_checkpoints
  0x058 C_Interface* m_interface
  0x05c C_FirstPersonView* m_firstPersonView
  0x060 C_QuickMessages* m_quickMessages
  0x064 bool m_paused
  0x066 short m_unknown66
  0x068 short m_unknown68
  0x06a C_str m_missionName
  0x06e bool m_playerInputEnabled
  0x06f bool m_missionFailed
  0x070 bool m_vipChosen
  0x071 bool m_hasVipObjective
  0x074 int m_round
  0x078 int m_vipSlot
  0x07c int m_respawnWaveTime
  0x080 bool m_inRestrictedArea
  0x084 int m_teamLivesUsed
  0x088 int m_roundState                  # 0 playing, 1 over, 2 intermission, 3 warmup, 4 closing
  0x08c uint32_t m_roundStateTime
  0x090 int m_respawnRequest
  0x094 bool m_unknown94
  0x098 std::vector<S_PendingSpawn*> m_pendingSpawns
  0x0a8 C_MissionDef* m_mission
  0x0ac C_str m_aviName
  0x0b0 bool m_playAvi
  0x0b4 int m_spectatedSlot
  0x0b8 int m_cameraMode                  # 0 own, 1 following, 2 free
  0x0bc float m_fov
  0x0c0 float m_nearClip
  0x0c4 float m_farClip
  0x0c8 int m_music
  0x0cc int m_alarmMusic
  0x0d0 int m_musicTimer
  0x0d4 int m_alarmCount
  0x0d8 int m_unknownD8
  0x0dc std::vector<I3D_light*> m_staticLights
end
