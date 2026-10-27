# The game profile, its objects and the campaign tree (src/game/profile.h, key_bindings.h,
# campaign.h; src/common/config_file.h).

# --- the campaign tree ---------------------------------------------------------------------------

enum E_CampaignNodeType
  CAMPAIGN_NODE_END = 0                    # the plain node a single mission ends with
  CAMPAIGN_NODE_ROOT = 1
  CAMPAIGN_NODE_OPERATION = 2
  CAMPAIGN_NODE_MISSION = 3
  CAMPAIGN_NODE_OBJECTIVE = 4
  CAMPAIGN_NODE_VIDEO = 5
  CAMPAIGN_NODE_SCREEN = 6
  CAMPAIGN_NODE_PLAY = 7
  CAMPAIGN_NODE_MP_OBJECTIVE = 12
  CAMPAIGN_NODE_MP_MISSION = 13
end

enum E_ObjectiveStatus
  OBJECTIVE_OPEN = 0
  OBJECTIVE_COMPLETED = 1
  OBJECTIVE_FAILED = 2
  OBJECTIVE_FAILED_CRITICAL = 3            # the mission is lost
  OBJECTIVE_INACTIVE = 4                   # not part of the mission (any more)
end

# A node of the tree (slots: dtor, LoadDefinition, Load, Save, LoadProgress, SaveProgress, Next,
# Prev, GetCurrent, Reset).
class C_CampaignNode size=0x10 vtable=0x815690:10
  0x04 int m_type                          # E_CampaignNodeType
  0x08 int m_id                            # text id; missions: their number
  0x0c C_str m_text                        # copied, never loaded
end

# A video between the steps (type 5).
class C_CampaignVideo : C_CampaignNode size=0x1c vtable=0x815668:10
  0x10 C_str m_fileName
  0x14 C_str m_title
  0x18 bool m_briefing                     # a single mission plays it too
end

# Equipment a menu screen offers (multiplayer: a map's allowed items).
struct S_CampaignItem size=0x10
  0x00 int itemId
  0x04 int count
  0x08 int ammo                            # -1: the default
  0x0c bool unknownC
end

# A menu screen between the steps (type 6).
class C_CampaignScreen : C_CampaignNode size=0x38 vtable=0x815b38:10
  0x10 int m_kind                          # 3: the squad's equipment
  0x14 bool m_backAcross                   # going back may cross into the previous mission
  0x15 bool m_unknown15
  0x18 std::vector<S_CampaignItem> m_items
  0x28 std::vector<int> m_unknown28
end

# The mission itself (type 7): C_GameApp starts the game.
class C_CampaignPlay : C_CampaignNode size=0x10 vtable=0x815b10:10
end

# An objective of a mission (type 4).
class S_MissionObjective : C_CampaignNode size=0x18 vtable=0x815a48:10
  0x10 int status                          # E_ObjectiveStatus
  0x14 bool bonus                          # 1500 experience instead of 1000
  0x15 bool rewarded                       # completed once
end

# An objective of a multiplayer map (type 12).
class S_MpObjective : S_MissionObjective size=0x30 vtable=0x815ae8:10
  0x18 int team                            # the attacking side (0 axis, 1 allied)
  0x1c int alliedText
  0x20 int axisText
  0x24 int points                          # the score for the team that wins it
  0x28 int type                            # 1: a defender is the VIP
  0x2c int vipUniform                      # the VIP's uniform item
end

# A node with children and a cursor among them (slots 2..5 are the children's Load / Save /
# LoadProgress / SaveProgress; added: Clear, GetCursorState, GetChild).
class C_CampaignContainer : C_CampaignNode size=0x24 vtable=0x815a70:13
  0x10 std::vector<C_CampaignNode*> m_children
  0x20 int m_cursor                        # -1: before the first child
end

# A campaign file's root (type 1).
class C_CampaignRoot : C_CampaignContainer size=0x24 vtable=0x815b60:13
end

# An operation (type 2).
class C_CampaignOperation : C_CampaignContainer size=0x28 vtable=0x815b94:13
  0x24 C_str m_name
end

# A mission (type 3; added slots: CreateObjective, AddObjective).
class C_MissionDef : C_CampaignContainer size=0x50 vtable=0x815a0c:15
  0x24 C_str m_name                        # its directory (missions\<name>)
  0x28 C_str m_image
  0x2c bool m_unknown2C                    # set for the tutorial
  0x30 std::vector<S_MissionObjective*> m_objectives
  0x40 int m_texts[4]                      # the menus' text ids
end

# The mission of a multiplayer map (type 13; added slot: AddMpObjective).
class C_MpMissionDef : C_MissionDef size=0x80 vtable=0x815aa8:16
  0x50 int m_aircraftAttackAxis            # the map's AIRCRAFTATACK entry
  0x54 int m_aircraftAttackAllied
  0x58 int m_balanceAllied                 # the map's BALANCED entry: the team balance's sizes
  0x5c int m_balanceAxis
  0x60 int m_unknown60
  0x64 int m_unknown64
  0x68 C_str m_presets
  0x6c C_str m_mapName
  0x70 std::vector<S_CampaignItem> m_items
end

# The campaigns and the mission played (C_GameProfile +0x04; slots LoadGame, SaveGame).
class C_Campaign size=0xf4 vtable=0x815a00:2
  0x04 std::vector<C_CampaignRoot*> m_campaigns
  0x14 C_MpMissionDef m_mpMission
  0x94 C_MissionDef m_singleMission
  0xe4 int m_singleMissionNumber           # the mission a single mission copies
  0xe8 int m_current                       # the campaign played
  0xec int m_version                       # 2
  0xf0 bool m_unknownF0
end

# --- the profile's objects (profile_roster.cpp, profile_squad.cpp, profile_misc.cpp,
# player_profile.cpp, key_bindings.cpp) ------------------------------------------------------------

# Key=value text lines (game.cfg, profile.pr; src/common/config_file.h).
struct C_ConfigFile size=0x10
  0x00 std::vector<C_str> m_lines
end

# game.cfg (C_GameProfile +0x08).
struct C_GameConfig size=0xc
  0x00 C_str m_cdKey                       # NETCDKEY
  0x04 C_str m_parentLock                  # PARENTLOCK
  0x08 C_str m_lastProfile                 # LASTPROFIL
end

struct S_HudOption size=0x1
  0x00 bool shown
end

# The ranks' names in two forms (g_rankNames).
struct S_RankName size=0x10
  0x00 int textId                          # 0x27A6..
  0x04 int textId2                         # 0x2792..
  0x08 C_str name
  0x0c C_str name2
end

# The soldiers the campaign knows (tables\HumanStore.def; C_GameProfile +0x10).
struct C_HumanStore size=0x10
  0x00 std::vector<S_SoldierProfile*> m_profiles
end

# What a squad soldier carries (S_SquadSoldier +0x8C).
struct S_SoldierLoad size=0x20
  0x00 int capacity                        # the weight the soldier can carry (strength)
  0x04 float weight                        # everything carried
  0x08 float backpackMaxWeight             # the backpack's container
  0x0c int backpackMaxVolume
  0x10 float backpackWeight                # its contents
  0x14 int backpackVolume
  0x18 float beltWeight
  0x1c int beltVolume                      # at most 12
end

# A soldier of the mission squad (S_MissionSquad::AddSoldier).
struct S_SquadSoldier : S_SoldierState size=0xac
  0x8c S_SoldierLoad load
end

# The soldiers of the next mission (C_GameProfile +0x14).
class S_MissionSquad size=0x14 vtable=0x8159fc:1
  0x04 std::vector<S_SquadSoldier*> states
end

# Counts of items by item type (C_GameProfile +0x18 / +0x1C).
struct C_ItemStock size=0x10
  0x00 std::vector<int*> m_counts
end

# The texts the mission scripts add for the debriefing (C_PlayerProfile +0xDC).
struct C_Debriefing size=0x10
  0x00 std::vector<int*> m_texts
end

# A movie of the extras menu.
struct S_Movie size=0xc
  0x00 bool seen
  0x04 int textId
  0x08 char* file
end

# The movies and which of them the player has seen (C_PlayerProfile +0x100).
struct C_MovieList size=0x10
  0x00 std::vector<S_Movie> m_movies
end

# A diary (one per campaign; C_PlayerProfile +0x94 / +0xB8).
struct C_Diary size=0x24
  0x00 bool locked                         # while a saved game is written or read
  0x04 std::vector<int> m_texts
  0x14 std::vector<int> m_current          # the current mission's texts
end

# A key binding (saved as it is in Options.bin).
struct S_KeyBinding size=0x1c
  0x00 int action                          # C_Controls action
  0x04 int16_t device                      # 1 keyboard, 2 mouse
  0x06 uint16_t key                        # DIK code / mouse button bits, axes 0x100..0x300
  0x08 int direction                       # axes: 1 + / 2 -
  0x0c int mode                            # 1 all, 2, 3
  0x10 int group                           # the options screen's list
  0x14 int textId
  0x18 bool changed                        # not yet given to the controls
end

# The texts' key tags (g_keyTags).
struct S_KeyTag size=0xc
  0x00 char* tag
  0x04 int action
  0x08 int mode
end

# The player's key bindings (C_PlayerProfile +0xEC).
struct C_KeyBindings size=0x14
  0x00 std::vector<S_KeyBinding> m_bindings
  0x10 int m_mode                          # 2 or 3
end

# The player's options (C_PlayerProfile +0x38; Options.bin chunk 0x1201).
struct C_PlayerOptions size=0x3c
  0x00 float mouseSensitivity              # the controls get 1000 - 9 * it
  0x04 float gamma                         # 50 = linear
  0x08 float soundVolume                   # percent
  0x0c float musicVolume                   # percent
  0x10 float voiceValue                    # C_NetGame::SetVoiceValue
  0x14 int unknown14
  0x18 int level                           # the detail level 0..3
  0x1c bool switches[4][7]                 # per level; 2 = decals
  0x38 bool voiceChat
  0x39 bool invertMouse                    # the controls' +0xB4 (inferred)
  0x3a bool unknown3A
  0x3b bool crosshair
end

# A save slot of the player (the quick saves per play mode).
struct S_SaveSlotInfo size=0x8
  0x00 int mission                         # -1: none
  0x04 C_str file
end

# The player's profile (PlayersProfiles\<name>; C_GameProfile +0x20).
struct C_PlayerProfile size=0x110
  0x00 C_str m_name
  0x04 C_str m_lastSave                    # LASTSAVE
  0x08 C_str m_lastSaveText                # "mode$mission$date"
  0x0c S_SaveSlotInfo m_saveSlots[5]
  0x34 int m_difficulty
  0x38 C_PlayerOptions m_options
  0x74 int m_progress[2][4]                # per campaign: best, campaign, lone wolf, carnage
  0x94 C_Diary m_diaries[2]
  0xdc C_Debriefing m_debriefing
  0xec C_KeyBindings m_keyBindings
  0x100 C_MovieList m_movies
end

# --- the profile ----------------------------------------------------------------------------------

enum E_PlayMode
  PLAY_MODE_CAMPAIGN = 1
  PLAY_MODE_LONE_WOLF = 2
  PLAY_MODE_CARNAGE = 3
end

# The play mode (C_GameProfile +0x28).
class S_PlayMode size=0x14 vtable=0x8159f4:1
  0x04 int singleMission                   # a campaign mission played on its own
  0x08 int mode                            # E_PlayMode (single missions: 0 or 3)
  0x0c int unknown0C
  0x10 int unknown10
end

# The profile (g_gameProfile; slot GetLanguage).
class C_GameProfile size=0x34 vtable=0x815a08:1
  0x04 C_Campaign* m_campaign
  0x08 C_GameConfig* m_config
  0x0c S_HudOption* m_hudOption
  0x10 C_HumanStore* m_humanStore
  0x14 S_MissionSquad* m_missionSquad
  0x18 C_ItemStock* m_itemStock18        # kept while a game runs
  0x1c C_ItemStock* m_itemStock1C        # the squad soldiers return their items to it
  0x20 C_PlayerProfile* m_player
  0x24 S_MultiplayerSettings* m_mpSettings
  0x28 S_PlayMode* m_playMode
  0x2c C_str m_language
  0x30 bool m_unicodeFont
end
