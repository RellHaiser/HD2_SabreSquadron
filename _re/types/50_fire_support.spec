# The heavy fire support (src/game/heavy_fire_support.h): the air attacks and the artillery the
# player's radios call in (module 16, "HeavyFireSupportModule", the object 0x0089b2c0).

# The request of an air attack (C_PlayerController::DesignateTarget).
struct S_AirAttackRequest size=0x24
  0x00 float x
  0x04 float z
  0x08 float dirX                         # the way the planes fly
  0x0c float dirZ
  0x10 int kind                           # S_AirSupportKind 0..15
  0x14 int planes                         # 1..3
  0x18 float height
  0x1c C_Human* caller
  0x20 int team
end

struct S_ArtilleryRequest size=0x14
  0x00 float x
  0x04 float z
  0x08 C_Human* caller
  0x0c int kind                           # (not used)
  0x10 int team
end

# A kind of air attack (tables\airsupport.tbl).
struct S_AirSupportKind size=0x18
  0x00 C_str model
  0x04 float speed                        # m/s (30)
  0x08 int soundGroup                     # the engine (SOUND_CHAPTER_VEHICLE_NOT_CAR)
  0x0c int bombsPerPlane                  # (2)
  0x10 float damage                       # (1000)
  0x14 float range                        # m (10)
end

# A plane of an air attack.
struct S_AirStrikePlane size=0x20
  0x00 S_vector4 offset                   # behind the plane before it
  0x10 I3D_frame* model
  0x14 int sound                          # the engine's sound slot
end

# An air attack under way (15 s to the target, 15 s more).
struct C_AirStrike size=0x80
  0x00 S_vector4 m_pos                    # the lead plane
  0x10 S_vector4 m_target
  0x20 S_vector4 m_dir
  0x30 float m_speed
  0x34 std::vector<S_AirStrikePlane*> m_planes
  0x48 int64_t m_time                     # ms until over the target
  0x50 int m_blastTimer
  0x54 int m_blastsLeft
  0x58 int m_bombsPerPlane
  0x5c float m_range
  0x60 float m_damage
  0x64 int m_team
  0x68 C_Human* m_caller
  0x6c int m_speechTimer
  0x70 I3D_sound* m_speech
end

# An artillery strike under way (7 s to the hit, 7 s more).
struct C_ArtilleryStrike size=0x40
  0x00 S_vector4 m_target
  0x10 int64_t m_time
  0x18 int m_blastTimer
  0x1c int m_blastsLeft
  0x20 int m_blastCount                   # 20
  0x24 float m_range                      # 20 m
  0x28 float m_damage                     # 1000
  0x2c int m_team
  0x30 C_Human* m_caller
  0x34 int m_speechTimer
end

# The interface the player controller calls.
class I_HeavyFireSupport size=0x4 vtable=0x810058:2
end

# The module; its C_Module part (+4) has its own vtable (C_HeavyFireSupport_Module).
class C_HeavyFireSupport size=0x1b4 vtable=0x810050:2
  0x004 C_Module m_module
  0x010 S_AirSupportKind m_kinds[16]
  0x190 std::vector<C_AirStrike*> m_airStrikes
  0x1a0 std::vector<C_ArtilleryStrike*> m_artillery
  0x1b0 bool m_busy[4]                    # 0 / 1 the teams' air attacks, 2 / 3 their artillery
end

# The C_Module part of C_HeavyFireSupport (this = the object + 4).
class C_HeavyFireSupport_Module : C_Module size=0x1b0 vtable=0x810010:16
  0x00c S_AirSupportKind m_kinds[16]
  0x18c std::vector<C_AirStrike*> m_airStrikes
  0x19c std::vector<C_ArtilleryStrike*> m_artillery
  0x1ac bool m_busy[4]
end
