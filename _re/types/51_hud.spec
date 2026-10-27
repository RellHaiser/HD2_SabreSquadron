# The HUD (src/game/first_person_view.h, src/game/interface*.h): the first-person view (C_game +0x5C)
# and the game interface (C_game +0x58) with its panels.

# A stage of the first-person model's animation: the one playing (blending in) and the ones
# blending out (C_FirstPersonView::m_fading).
struct S_FpvStage size=0x10
  0x00 I3D_animation_set* anim
  0x04 int stage                          # the model's animation stage 0..2
  0x08 float speed                        # weight change per second (5)
  0x0c float weight
end

# The animations of one weapon (item type) and event: up to 4 variants, picked at random by
# their ratios (tables\FpvAnims.sav).
struct S_FpvAnim size=0x30
  0x00 I3D_animation_set* anims[4]        # loaded on first use
  0x10 C_str names[4]
  0x20 int ratios[4]                      # 1..100
end

# The first-person view: the active soldier's hands and weapon in front of the camera
# ("tables\FpvAnims.sav"; the original's name). The game's "HUD" option shows it.
struct C_FirstPersonView size=0x4c370
  0x000 S_vector4 m_unused00              # (Init clears them; nothing reads them)
  0x010 S_quat m_unused10
  0x020 S_vector4 m_unused20
  0x030 float m_fov                       # the camera's field of view it eases to
  0x034 float m_fov2                      # the camera's second one (65 deg, 40 through the sights)
  0x038 C_Human* m_human                  # the active soldier
  0x03c bool m_initialized                # Init (clients)
  0x040 I3D_model* m_model                # the weapon's model (a copy of m_weapon, the hands in it)
  0x044 I3D_model* m_hands                # "FPV_HANDS" (the uniform's fpv model)
  0x048 I3D_model* m_weapon               # the item's fpv model (its "fpv_weapon" frame)
  0x04c I3D_frame* m_magazine             # the weapon's fpv magazine
  0x054 int m_itemType                    # -1: none
  0x058 bool m_on                         # the first-person view is on
  0x059 bool m_binoculars                 # the binoculars' view
  0x05a bool m_aimView                    # through the sights / scope
  0x060 int m_pose                        # FPV_POSE_*
  0x064 float m_drop                      # how far the weapon is lowered (0 .. -0.8)
  0x068 float m_dropSpeed
  0x06c float m_lightLevel
  0x070 S_FpvStage m_current
  0x080 std::vector<S_FpvStage> m_fading
  0x090 int m_variant
  0x094 int m_event                       # the animation playing (FPV_EVENT_*)
  0x098 int m_nextEvent                   # what follows it
  0x09c float m_swayPhase
  0x0a0 S_FpvAnim m_anims[6500]           # [item type 0..499][event 0..12]
  0x4c360 float m_flashTime               # ms the muzzle flash is shown (-1: none)
  0x4c364 I3D_frame* m_flash              # the weapon model's "flash"
end

# --- the game interface (C_game +0x58; game/interface*.cpp) -------------------------------------

# A pre-transformed vertex of the message log's text quads (IGraph slot 0x88).
struct S_TLVertex size=0x20
  0x00 float x
  0x04 float y
  0x08 float z                            # 0.02
  0x0c float rhw                          # 1 / z
  0x10 uint32_t color                     # ARGB
  0x14 uint32_t specular
  0x18 float u
  0x1c float v
end

# A line of the message log.
struct S_LogLine size=0x18
  0x00 char* text                         # owned (new[])
  0x04 int r                              # 0..255
  0x08 int g
  0x0c int b
  0x10 int age                            # +1 every 2 s
  0x14 int font                           # the font manager's font (3)
end

# The messages and the chat line (+0x2F90).
struct C_MessageLog size=0xd390
  0x0000 S_vector m_color                 # (255, 255, 255)
  0x0010 int m_mode                       # 0 the last lines, 1 the history (scrolled)
  0x0014 void* m_fonts                    # the font manager (a reference)
  0x0018 I3D_frame* m_area                # the interface model's frame of the log
  0x001c std::vector<S_LogLine> m_lines
  0x002c int m_maxLines                   # 50
  0x0030 int m_scroll
  0x0034 int m_repeatKey
  0x0038 int m_repeatTime
  0x003c int m_repeatDelay                # 150 ms, then 50
  0x0040 float m_screenWidth
  0x0044 float m_screenHeight
  0x0048 float m_rect[4]                  # left, top, right, bottom (pixels)
  0x0058 float m_historyRect[4]
  0x0068 char m_input[256]
  0x0168 int m_inputScroll                # the first character shown
  0x016c S_TLVertex m_vertices[1680]
  0xd36c int m_vertexCount
  0xd370 float m_lineHeight               # of the screen height (0.022)
  0xd374 int m_unknownD374
  0xd378 int m_unknownD378
  0xd37c int m_ageTime                    # ms toward the next 2 s step
  0xd380 int m_caretTime                  # the caret shows from 150 to 300 ms of its cycle
  0xd384 bool m_unknownD384
  0xd385 bool m_inputOpen
  0xd386 bool m_sayAll
  0xd387 bool m_sayTeam
end

# A squad member or a seen enemy on the compass' radar.
struct S_RadarBlip size=0x10
  0x00 I3D_frame* visual                  # a copy of "kul_us" / "kul_en"
  0x04 C_Human* human
  0x08 float radius                       # the template's distance from the centre
  0x0c float alpha                        # fades to 0 when the human is out
end

struct C_Radar size=0x18
  0x00 I3D_frame* m_ally                  # "kul_us"
  0x04 I3D_frame* m_enemy                 # "kul_en"
  0x08 std::vector<S_RadarBlip*> m_blips
end

# The compass (+0x10854): the needle, the sound marks around it and the radar.
struct C_Compass size=0xa4
  0x00 float m_heading                    # the active soldier's view (radians)
  0x04 float m_needle
  0x08 float m_wobble
  0x0c float m_needleSpeed
  0x10 I3D_frame* m_frame                 # "kompas"
  0x14 I3D_frame* m_card                  # "kmpscvr" (turns with the needle)
  0x18 I3D_frame* m_arrow                 # "strelka" (the wobble)
  0x1c C_Radar* m_radar
  0x20 I3D_frame* m_soundMarks[16]        # [direction 0..7][blue, red] "kmps_bl%02d" / "kmps_rd%02d"
  0x60 int m_soundTimes[16]               # ms left (500)
  0xa0 bool m_leftFoot                    # the active soldier's leading foot
end

# Rich text in an interface visual's rectangle (the text library, 0x0063de40..; was the auto
# class C_auto_00810458; here so the interface can embed it).
class C_TextBox size=0x9c vtable=0x00810458:4
  0x004 uint16_t m_004
  0x006 uint16_t m_006
  0x008 uint32_t m_008
  0x00c uint32_t m_00c
  0x010 uint32_t m_010
  0x014 uint32_t m_014
  0x018 uint32_t m_018
  0x01c uint32_t m_01c
  0x020 uint32_t m_020
  0x024 void* m_024
  0x028 void* m_028
  0x02c int m_02c
  0x030 uint8_t m_030
  0x034 uint32_t m_034
  0x038 uint32_t m_038
  0x03c uint32_t m_03c
  0x044 uint8_t m_044
  0x048 int m_048
  0x04c uint32_t m_04c
  0x050 uint32_t m_050
  0x058 uint8_t m_058
  0x05c uint32_t m_05c
  0x060 uint32_t m_060
  0x064 uint32_t m_064
  0x06c int16_t m_06c
  0x06e int16_t m_06e
  0x070 uint8_t m_070
  0x071 uint8_t m_071
  0x072 uint8_t m_072
  0x073 uint8_t m_073
  0x074 char m_074
  0x076 uint16_t m_076
  0x078 uint32_t m_078
  0x07c uint32_t m_07c
  0x080 uint8_t m_080
  0x081 uint8_t m_081
  0x084 uint32_t m_084
  0x088 uint32_t m_088
  0x08c uint32_t m_08c
  0x090 uint8_t m_090
  0x094 uint32_t m_094
  0x098 uint32_t m_098
end

# The mission information / scores (+0x6B3A0, "ingame_info").
struct C_InfoScreen size=0x144
  0x000 std::vector<int> m_hints          # text ids (the scripts' hints; saved)
  0x010 int m_kind                        # 0 single player, 1 + the multiplayer game type
  0x014 I3D_frame* m_frame                # "game_info"
  0x018 C_TextBox* m_text
  0x01c int m_styles[73]
  0x140 int m_refreshTime                 # redrawn every 500 ms while shown
end

# The inventory (C_Interface +0x30): the soldier's slots and lists on the left page, a searched
# human's on the right one (or the skills).
struct S_InvSlot size=0x148
  0x000 C_TextBox name                    # "text_*"
  0x09c I3D_frame* selection              # "sel*"
  0x0a0 I3D_frame* icon                   # frame data 200 / 210 + the slot
  0x0a4 I3D_material* material
  0x0a8 C_TextBox amount                  # a weapon's rounds
  0x144 C_Item* item
end
struct S_InvList size=0x3e4
  0x000 C_TextBox caption                 # "hlstrscaptab*" / "bkpckcaptab*"
  0x09c I3D_frame* selection
  0x0a0 I3D_frame* icons[4]               # frame data 220 / 230 / 240 / 250 + the line
  0x0b0 I3D_material* materials[4]
  0x0c0 C_TextBox amounts[4]
  0x330 C_Item* items[4]                  # each line's first item
  0x340 I3D_frame* capacityBar            # "hlcpct0x"
  0x344 C_TextBox capacity
  0x3e0 int scroll
end
# A scroll bar's arrow ("barrow up*" / "barrow down*").
struct S_InvScrollButton size=0x20
  0x000 void* vtable                      # 0x008104f0
  0x004 I3D_frame* frame
  0x008 int id                            # the bar's id | 1 / 2
  0x00c bool disabled
  0x010 C_InventoryScrollBar* owner
  0x014 I3D_frame* normal
  0x018 I3D_frame* active                 # "actived*"
  0x01c I3D_frame* inactive               # "inactived*"
end
struct S_InvScrollSlider size=0x2c
  0x000 void* vtable                      # 0x008104ec
  0x004 I3D_frame* frame                  # "slider*"
  0x008 int id                            # the bar's id | 3
  0x00c bool disabled
  0x010 C_InventoryScrollBar* owner
  0x014 I3D_frame* normal
  0x018 I3D_frame* active
  0x01c I3D_frame* base                   # "sliderbase*"
  0x020 float* rect                       # new float[7]: the centre's range, width, height
  0x024 bool horizontal
  0x025 bool vertical
  0x028 float position                    # 0..100
end
struct C_InventoryScrollBar size=0x94
  0x000 void* vtable                      # 0x008104e8
  0x004 I3D_frame* m_frame                # "scroll13".."scroll16"
  0x008 int m_id                          # 0xF10..0xF40
  0x00c bool m_disabled
  0x010 C_InventoryScreen* m_owner
  0x014 S_InvScrollButton m_up
  0x034 S_InvScrollButton m_down
  0x054 S_InvScrollSlider m_slider
  0x080 float m_step
  0x084 int m_count
  0x088 int m_index
  0x08c int m_part                        # the lit part, -1 none
  0x090 int m_unknown90
end
struct C_InventoryScreen size=0x2e20
  0x0000 I3D_scene* m_scene
  0x0004 I3D_frame* m_frame               # "inventory"
  0x0008 I3D_frame* m_pages[2]            # "items00", "items01"
  0x0010 I3D_texture* m_emptyTexture
  0x0014 I3D_frame* m_description
  0x0018 int m_hoverTime
  0x001c I3D_frame* m_hovered
  0x0020 C_TextBox m_descriptionText
  0x00bc I3D_frame* m_dragIcon            # "DraggingFrame"
  0x00c0 I3D_material* m_dragMaterial
  0x00c4 C_Item* m_dragItem
  0x00d0 S_vector4 m_dragOffset
  0x00e0 int m_dragContainer
  0x00e4 C_Human* m_dragHuman
  0x00e8 I3D_frame* m_weightBars[2]       # "allcpct00/01"
  0x00f0 S_InvSlot m_slots[12]            # [slot][page]
  0x1050 S_InvList m_belts[2]
  0x1818 S_InvList m_backpacks[2]
  0x1fe0 I3D_frame* m_skills
  0x1fe4 C_TextBox m_skillsTitle
  0x2080 C_TextBox m_skillNames[7]
  0x24c4 I3D_frame* m_skillBars[7]
  0x24e0 C_TextBox m_skillValues[7]
  0x2924 C_InventoryScrollBar m_scrollBars[4]
  0x2b74 uint32_t m_mode                  # 1 own, 2 a searched human, 8 refresh
  0x2b78 C_Human* m_human
  0x2b7c C_Actor* m_other
  0x2b80 C_TextBox m_names[2]
  0x2cb8 C_TextBox m_weights[2]
  0x2df0 C_MapCursor* m_cursor
  0x2df4 bool m_pressed
  0x2df8 int m_doubleClickTime
  0x2e00 S_vector4 m_pressPos
  0x2e10 int m_activeBar                  # 1..4, 0 none
end
struct C_Speedometer size=0x10
  0x000 I3D_frame* m_frame                # "spee"
  0x004 I3D_frame* m_needles[3]           # "speed01..03": speed, rpm, fuel
end
struct C_Reticle size=0x20
  0x000 I3D_frame* m_mask                 # "sniper"
  0x004 I3D_frame* m_reticles[4]          # "arisaka", "k98", "springfield", "lee_enfield"
  0x014 I3D_frame* m_binoculars           # "scope"
end
# An entry of a signals page: its text and its action.
struct S_SignalEntry size=0x8
  0x000 int textId
  0x004 int action                        # a page 0x33..0x3B, a squad command, 100 + a command on the target, 0x46..0x48
end
struct C_SignalsPanel size=0x80
  0x000 C_Interface* m_ui
  0x004 I3D_frame* m_frame                # "signals"
  0x008 I3D_scene* m_scene
  0x00c int m_page                        # 0x32 the main page
  0x010 S_SignalEntry m_entries[10]
  0x060 int m_selected
  0x064 C_Actor* m_target
  0x068 C_Human* m_member                 # null: the whole squad
  0x06c bool m_noGroup
  0x070 C_TextBox* m_text
end
# An entry of the roll menu: its action and its text (below -10: a human's name).
struct S_RollEntry size=0x8
  0x000 int action                        # the human commands, 7 inventory, 8+ seats, 0x1D ask for healing, 0x32 greyed, 0x33 cancel
  0x004 int textId
end
struct C_RollMenu size=0x90
  0x000 float m_distance
  0x004 I3D_scene* m_scene
  0x008 C_Interface* m_ui
  0x00c I3D_frame* m_frame                # "rollmenu"
  0x010 I3D_frame* m_mapCursor            # "kurzor"
  0x014 C_TextBox* m_text
  0x018 int m_openTime
  0x020 S_vector4 m_home
  0x030 bool m_open
  0x031 bool m_commandMap
  0x034 S_RollEntry m_entries[10]
  0x084 int m_selected
  0x088 C_Actor* m_target
end
struct C_MemberPanel size=0xc0
  0x000 I3D_scene* m_scene
  0x004 int m_index
  0x008 I3D_frame* m_frame                # "Soldier%02d" (frame data 100 + the index)
  0x00c I3D_frame* m_select
  0x010 I3D_frame* m_activeMark           # "player0*"
  0x014 I3D_frame* m_photo
  0x018 I3D_frame* m_healthBar
  0x01c I3D_frame* m_healthCritical
  0x020 I3D_frame* m_healthIcon
  0x024 I3D_frame* m_staminaBar
  0x028 I3D_frame* m_disguiseBar
  0x02c I3D_frame* m_visibility
  0x034 I3D_frame* m_aiMode
  0x038 I3D_frame* m_ear
  0x03c I3D_frame* m_silencer
  0x040 I3D_frame* m_medic
  0x044 I3D_frame* m_vip
  0x048 I3D_material* m_commandMaterial
  0x04c I3D_material* m_mindMaterial
  0x054 IImage* m_deathImage
  0x05c I3D_material* m_photoMaterial
  0x060 I3D_texture** m_stateIcons
  0x064 I3D_texture** m_modeIcons
  0x068 I3D_texture* m_blankTexture       # "univerz"'s
  0x06c int m_statusIcon
  0x070 int m_modeIcon
  0x074 C_Human* m_human
  0x078 int m_unknown78
  0x07c float m_health                    # per cent shown
  0x080 int m_stamina
  0x084 int m_disguise
  0x088 bool m_unknown88
  0x089 bool m_selected
  0x08c int m_rank
  0x090 C_TextBox* m_name
  0x094 C_TextBox* m_visibilityText
  0x098 float m_unknown98
  0x0a0 S_vector4 m_photoColor
  0x0b0 bool m_alive
end
struct C_Crosshair size=0x40
  0x000 I3D_frame* m_frame                # "cross"
  0x004 I3D_frame* m_left
  0x008 I3D_frame* m_right
  0x00c I3D_frame* m_top
  0x010 I3D_frame* m_bottom
  0x014 float m_spread
  0x020 float m_color[4]
  0x030 C_Actor* m_target
  0x034 bool m_usable
  0x035 bool m_shown
  0x038 C_TextBox* m_name
end
struct C_WeaponPanel size=0x3c
  0x000 I3D_scene* m_scene
  0x004 I3D_frame* m_frame                # "tabl02"
  0x008 I3D_frame* m_handsIcon
  0x00c I3D_frame* m_shoulderIcon
  0x010 I3D_texture* m_emptyTexture
  0x014 I3D_material* m_handsMaterial
  0x018 I3D_material* m_shoulderMaterial
  0x01c C_ItemDef* m_handsDef
  0x020 C_ItemDef* m_shoulderDef
  0x024 C_TextBox* m_text
  0x028 int m_handsRounds
  0x02c int m_shoulderRounds
  0x030 int m_handsAmmo
  0x034 int m_shoulderAmmo
  0x038 int m_thrownItems
end
struct C_StancePanel size=0x38
  0x000 I3D_scene* m_scene
  0x004 I3D_frame* m_frame                # "tabl01"
  0x008 I3D_frame* m_kneel
  0x00c I3D_frame* m_prone
  0x010 I3D_frame* m_walk
  0x014 I3D_frame* m_run
  0x018 I3D_frame* m_sprint
  0x01c I3D_frame* m_axis
  0x020 I3D_frame* m_allied
  0x024 C_TextBox* m_text
  0x028 uint32_t m_stance
  0x02c uint32_t m_state
  0x030 int m_team
  0x034 int m_timer
end
# The map (and the respawn screen; C_Interface +0x108F8).
struct S_MapMarker size=0xc
  0x000 I3D_frame* visual
  0x004 C_TextBox* text                   # the chat bubble
  0x008 bool shown
end
struct S_MapZone size=0x18
  0x000 I3D_frame* frame
  0x004 I3D_frame* allied
  0x008 I3D_frame* axis
  0x00c int id                            # 1000 + the index
  0x010 int side                          # 0 axis, 1 allied, 2 none
  0x014 C_MPZone* zone
end
struct S_MapObjective size=0x1c
  0x000 I3D_frame* frame
  0x004 I3D_frame* allied
  0x008 I3D_frame* axis
  0x00c int status
  0x010 int side
  0x014 S_MpObjective* objective
  0x018 C_TextBox* text
end
struct S_MapSpObjective size=0x18
  0x000 I3D_frame* frame
  0x004 I3D_frame* allied
  0x008 I3D_frame* axis
  0x00c int status
  0x010 S_MissionObjective* objective
end
struct C_MapScreen size=0x210
  0x000 I3D_scene* m_scene
  0x004 I3D_frame* m_frame                # "center"
  0x008 I3D_frame* m_map                  # "map*"
  0x00c I3D_frame* m_selectMarker         # "selectzone"
  0x010 I3D_model* m_model                # "missions\%s\map"
  0x014 C_MapCursor* m_cursor
  0x018 int m_kind                        # 0 single player, 1 deathmatch, 2 occupation, 3 objectives, 4 cooperative
  0x01c int m_markerCount
  0x020 float m_scale
  0x024 bool m_commandMap
  0x028 float m_depth
  0x02c I3D_frame* m_activeMarker         # "allied_active"
  0x030 S_MapMarker m_markers[32]
  0x1b0 I3D_frame* m_vipMarker
  0x1b4 I3D_frame* m_cameraMarker         # "mapcamera"
  0x1b8 I3D_frame* m_itemTemplate         # "objective"
  0x1bc I3D_frame* m_enemyTemplate        # "enemy"
  0x1c0 std::vector<I3D_frame*> m_enemyMarkers
  0x1d0 std::vector<I3D_frame*> m_itemMarkers
  0x1e0 std::vector<S_MapZone*> m_zones
  0x1f0 std::vector<S_MapObjective*> m_objectives
  0x200 std::vector<S_MapSpObjective*> m_spObjectives
end
struct C_ProgressBar size=0x18
  0x000 I3D_frame* m_frame                # "prgbrbk"
  0x004 I3D_frame* m_bar
  0x010 int m_textId
  0x014 C_TextBox* m_text
end
# The command map (C_Interface +0x10B20): the buttons (their frames' data: ids 1..18), the hint
# texts, the roll menu's command map instance, the order markers and each member's order lines.
struct S_CommandButton size=0x10
  0x000 I3D_frame* frame                  # its data: the id
  0x004 int id
  0x00c I3D_frame* unlit                  # "~name", shown while not lit
end
struct S_CmdLineVertex size=0x18
  0x000 S_vector pos
  0x00c uint32_t color                    # ARGB, half opaque
  0x010 float u
  0x014 float v
end
struct S_CmdMemberLines size=0xf008
  0x0000 S_CmdLineVertex paths[2048]      # a line strip along the orders' paths
  0xc000 int pathSegments
  0xc004 S_CmdLineVertex routes[512]      # never filled (the unreferenced route code's)
  0xf004 int routeSegments
end
struct C_CommandPanel size=0x5a7c0
  0x00000 I3D_frame* m_frame              # "Border"
  0x00004 I3D_scene* m_scene              # the interface's
  0x00008 I3D_scene* m_gameScene
  0x0000c C_Interface* m_ui
  0x00010 int m_unknown10
  0x00014 C_Human* m_commanded
  0x00018 C_Human* m_active
  0x0001c C_Actor* m_target               # under the cursor
  0x00020 S_vector4 m_cursorPoint         # the ground under the cursor
  0x00040 C_InfoSelector* m_selector
  0x00044 C_MapCursor* m_cursor
  0x00048 uint32_t m_pace                 # -1: none
  0x0004c uint32_t m_mode
  0x00050 uint32_t m_stance
  0x00054 int m_time
  0x00058 int m_wheelTime
  0x0005c bool m_realtime
  0x00060 I3D_texture** m_stateIcons
  0x00064 I3D_texture** m_actionIcons
  0x00068 I3D_texture* m_stanceIcons[3]   # lie, kneel, stand
  0x00074 I3D_texture* m_paceIcons[4]     # slow, walk, run, sprint
  0x00084 I3D_texture* m_modeIcons[3]     # passive, defensive, aggressive
  0x00090 int m_zoom
  0x00094 I3D_frame* m_zoomFrames[5]      # "TMcamera1x".."TMcamera16x"
  0x000a8 C_TextBox m_hints[8]            # "text_regroup".."text_options"
  0x00590 C_RollMenu m_rollMenu
  0x00620 S_CommandButton m_buttons[18]
  0x00740 int m_flashTime
  0x00744 S_CommandButton* m_flashButton
  0x00748 I3D_model* m_markerModel        # "l_aicomm"
  0x0074c std::vector<I3D_frame*> m_spareMarkers
  0x0075c S_CmdMemberLines m_lines[6]
  0x5a790 S_vector4 m_dragPoint
  0x5a7a0 S_vector4 m_dragMarker
  0x5a7b0 bool m_draggingMarker
  0x5a7b1 bool m_refreshPaths
  0x5a7b4 C_Checkpoints* m_checkpoints
  0x5a7b8 bool m_leftDown
  0x5a7b9 bool m_rightDown
  0x5a7ba bool m_middleDown
  0x5a7bb bool m_draggingMap
  0x5a7bc bool m_unknown5A7BC
  0x5a7bd bool m_unknown5A7BD
end
struct C_InfoSelector size=0x40
  0x000 C_Human* m_human
  0x004 I3D_frame* m_icons[10]            # "info00..09"
  0x02c I3D_texture** m_stateIcons
  0x030 I3D_texture** m_actionIcons
  0x034 I3D_frame* m_marker               # "infosel"
end
struct C_MapCursor size=0x80
  0x000 I3D_frame* m_frame                # the shape shown
  0x004 I3D_scene* m_scene
  0x008 I3D_scene* m_gameScene
  0x00c int m_mode
  0x010 S_vector4 m_prevPos
  0x020 S_vector4 m_pos
  0x030 I3D_frame* m_shapes[16]
  0x070 float m_scale
  0x074 float m_targetScale
  0x078 float* m_clip
end
# An entry of the weapon selector (0x004ac190).
struct C_WeaponSelector_Entry size=0xb0
  0x000 C_Item* item                      # null: the empty hands
  0x004 int count
  0x008 int container
  0x00c I3D_frame* visual
  0x010 I3D_frame* shade
  0x014 C_TextBox text
end
struct C_WeaponSelector size=0x3c
  0x000 I3D_scene* m_scene
  0x004 I3D_frame* m_frame                # "select_weapon"
  0x008 I3D_frame* m_marker
  0x00c I3D_frame* m_entryTemplate
  0x010 I3D_frame* m_shadeTemplate
  0x018 std::vector<C_WeaponSelector_Entry*> m_entries
  0x028 C_Human* m_human
  0x02c int m_selected
  0x030 int m_nextRepeat
  0x034 int m_prevRepeat
  0x038 int m_idleTime
end
struct C_InjuryFlash size=0x30
  0x000 I3D_frame* m_frame                # "injury"
  0x004 ISND_stream* m_video
  0x008 ISND_stream* m_videos[7]
  0x024 int m_time
  0x028 float m_alpha
  0x02c bool m_rising
end
struct C_BigMessage size=0x8
  0x000 I3D_frame* m_frame                # "Htext"
  0x004 C_TextBox* m_text
end
struct C_PhotoFlash size=0xc
  0x000 I3D_frame* m_frame                # "camera"
  0x004 int m_timeLeft
  0x008 bool m_soundPending
end
struct C_ObjectiveIcons size=0xc4
  0x000 I3D_frame* m_frame                # "mpobjectives"
  0x004 I3D_frame* m_icons[48]            # [16][3]: allied, axis, neutral
end
struct C_ScoreTable size=0xc
  0x000 I3D_frame* m_frame
  0x004 C_TextBox* m_text
  0x008 int m_gameType                    # -1: single player, deathmatch
end
struct C_VotingPanel size=0x8
  0x000 I3D_frame* m_frame
  0x004 C_TextBox* m_text
end
struct C_ExplosiveTimer size=0x74
  0x000 I3D_frame* m_frame
  0x004 I3D_frame* m_timers[10]
  0x02c C_TextBox* m_texts[10]
  0x060 I3D_scene* m_scene
  0x064 std::vector<C_Actor*> m_explosives
end
struct C_CarnageStats size=0x8
  0x000 I3D_frame* m_frame
  0x004 C_TextBox* m_text
end
struct C_PauseText size=0xc
  0x000 C_TextBox* m_text
  0x004 bool m_shown
end
struct C_CountdownText size=0xc
  0x000 C_TextBox* m_text
  0x004 bool m_shown
  0x008 int m_timeLeft
end

# The game interface (C_game +0x58): the HUD's panels in the "gameinterface" model's own scene.
struct C_Interface size=0x6b900
  0x00000 I3D_scene* m_scene
  0x00004 I3D_frame* m_camera
  0x00008 I3D_model* m_model              # "gameinterface"
  0x0000c uint32_t m_flags                # INTERFACE_*
  0x00010 int m_pickTime                  # single player: the actor under the crosshair every 100 ms
  0x00014 int m_pickMisses
  0x00018 uint32_t m_savedFlags           # the panel size and the view before the command map
  0x00020 bool m_infoForced               # ShowScores
  0x00021 bool m_respawnForced            # ShowRespawnScreen
  0x00024 I3D_frame* m_downPanel          # "down_panel"
  0x00030 C_InventoryScreen m_inventory
  0x02e50 C_Speedometer m_speedometer
  0x02e60 C_Reticle m_reticle
  0x02e80 C_SignalsPanel m_signals
  0x02f00 C_RollMenu m_rollMenu
  0x02f90 C_MessageLog m_messages
  0x10320 C_MemberPanel m_members[6]
  0x107a0 C_Crosshair m_crosshair
  0x107e0 C_WeaponPanel m_weaponPanel
  0x1081c C_StancePanel m_stancePanel
  0x10854 C_Compass m_compass
  0x108f8 C_MapScreen m_map
  0x10b08 C_ProgressBar m_progress
  0x10b20 C_CommandPanel m_commandPanel
  0x6b2e0 C_InfoSelector m_infoSelector
  0x6b320 C_MapCursor m_cursor
  0x6b3a0 C_InfoScreen m_info
  0x6b4e4 C_WeaponSelector m_weaponSelector
  0x6b520 C_InjuryFlash m_injuryFlash
  0x6b550 C_BigMessage m_bigMessage
  0x6b558 C_PhotoFlash m_photoFlash
  0x6b564 C_ObjectiveIcons m_objectives
  0x6b628 C_ScoreTable m_scoreTable
  0x6b634 C_VotingPanel m_voting
  0x6b63c C_ExplosiveTimer m_explosiveTimer
  0x6b6b0 C_CarnageStats m_carnage
  0x6b6b8 C_PauseText m_pauseText
  0x6b6c4 C_CountdownText m_countdown
  0x6b6d0 int m_stateIcons[20]            # "playerstate1_%02d" materials' textures
  0x6b720 int m_stateIcons2[40]           # "playerstate2_%02d"
  0x6b7c0 int m_actionIcons[39]           # "action%02d"
  0x6b85c C_TextBox m_debugText           # "Camera pos: 0,0,0" (on "Border")
  0x6b8f8 C_Actor* m_pickedActor          # under the crosshair
  0x6b8fc C_Actor* m_usableActor          # near enough to use
end
