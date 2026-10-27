# Shots and the shot manager ("ShotMgr", g_shotManager 0x0089aac0; code 0x00448270..0x004559c0;
# ported in src/game/shots.*).
# A shot is a flying bullet, projectile (rocket, rifle grenade) or thrown item (grenade);
# the manager also owns the impact decals, blood effects, ejected shells and muzzle lights.

struct S_SoundRequest size=0x40
  0x00 S_vector pos
  0x0c float posW
  0x10 uint32_t flags
  0x14 I3D_frame* attachTo
  0x18 C_Actor* source
  0x1c int chapter
  0x20 int group
  0x24 char* fileName
  0x28 float nearRange
  0x2c float farRange
  0x30 int channel
  0x34 float nearScale
  0x38 float farScale
  0x3c float volume
end

# Record of tables\hdmaterials.def (g_materials, 0x100 bytes each).
struct S_MaterialDef size=0x100
  0x28 uint32_t absorption          # power a bullet loses on it; less left: stops / ricochets
  0x2c uint32_t penetrationCost     # power spent to pass through, 0xFFFFFFFF impenetrable
  0x30 int hitSound                 # sound group (chapter 4), -1 none
  0x34 int hitSoundAlternate
  0x3c int hitEffect                # particle effect, 0 none
  0x4c int explosionEffect          # particle effect of explosions on it
  0x50 int decal                    # bullet hole texture x_decal<n> (0..8), -1 none
  0x54 int explosionDecal           # 0 x_expl00, 1..6 x_blood00..05
end

# One of the bullet hole / blood projections on the scene geometry.
struct S_ShotDecal size=0x8
  0x00 I3D_visual* visual           # 'PROJ' visual
  0x04 C_str parentName             # frame the decal is linked to ("No Parent": sector)
end

# Contact of a rigid body (C_RigidBody, vtable 0x00810700) passed to its callback.
struct S_RigidBodyContact size=0x30
  0x00 S_CollisionVolume* volume
  0x10 S_vector pos
  0x1c float posW
  0x20 S_vector normal
  0x2c float normalW
end

# ---- shots ---------------------------------------------------------------------------
class C_Shot size=0x70 vtable=0x80fa7c:7
  0x10 S_vector4 m_velocity
  0x20 S_vector4 m_pos
  0x30 S_vector4 m_prevPos
  0x40 S_vector4 m_step              # movement of the last tick
  0x50 int m_interpolation          # 0..100 % of m_step shown (Update between ticks)
  0x54 int m_traceDistance          # bullets: metres the trace has been shown
  0x58 float m_damage
  0x5c int m_type                   # 1 bullet (pooled), 3 thrown, 4 projectile
  0x60 C_Actor* m_shooter
  0x64 C_WeaponComp* m_weapon
  0x68 I3D_frame* m_visual
  0x6c bool m_fake                  # scripted fake shot: passes actors, silent to the AI
  0x6d bool m_ricocheted
  0x6e bool m_nearMissPlayed
  vslot 0x00 ScalarDeletingDestructor | C_Shot * __thiscall ScalarDeletingDestructor(C_Shot *this, uchar flags)
  vslot 0x01 Release | void __thiscall Release(C_Shot *this)
  vslot 0x02 Tick | bool __thiscall Tick(C_Shot *this, int timeDelta)
  vslot 0x03 Update | void __thiscall Update(C_Shot *this, int timeDelta)
  vslot 0x04 Recycle | void __thiscall Recycle(C_Shot *this)
  vslot 0x05 SaveGame | void __thiscall SaveGame(C_Shot *this, C_chunk *chunk)
  vslot 0x06 LoadGame | void __thiscall LoadGame(C_Shot *this, C_chunk *chunk)
end

class C_BulletShot : C_Shot size=0xc0 vtable=0x80fa60:7
  0x70 float m_distance             # metres flown
  0x74 int m_unknown74
  0x78 bool m_finished
  0x80 int m_targetMaterial         # client bullets
  0x90 S_vector4 m_target
  0xa0 S_vector4 m_targetNormal
  0xb0 bool m_flare                 # signal pistol flare
  0xb4 int m_flareSparks            # particle effect handle, -1 none
  0xb8 bool m_burnedOut
end

class C_ProjectileShot : C_Shot size=0xa0 vtable=0x80fad8:8
  0x70 int m_trail                  # trail particle effect handle, -1 none
  0x74 float m_distance
  0x78 bool m_exploded
  0x7c int m_unknown7c
  0x80 int m_flightTime             # ms
  0x84 int m_removeDelay            # ms the shot is kept after exploding (camera following)
  0x88 bool m_cameraFollows
  0x8c I3D_sound* m_flySound        # "e_incm%d.wav"
  0x90 bool m_allowCameraFollow
end

class C_ThrownShot : C_Shot size=0x6a0 vtable=0x80faa8:8
  0x70 uint8_t m_body[0x600]        # C_RigidBody (vtable 0x00810700)
  0x670 int m_time                  # ms since the throw
  0x674 int m_fuseTime              # ms, 0 = explodes on impact
  0x678 int m_state                 # 0 flying, 1 finished, 2 impact
  0x680 S_vector4 m_normal          # explosion direction (0, 1, 0)
  0x690 int m_material              # -1
  0x694 int m_bounceSound           # sound group (chapter 5) = item category
  0x698 int m_bounceSoundTimer
end

# Free lists of shots / shells; blocks of m_growBy objects are allocated when empty.
struct S_PoolNode size=0x8
  0x00 void* m_object
  0x04 S_PoolNode* m_next
end

class C_BulletShotPool size=0x14 vtable=0x80fa5c:1
  0x04 S_PoolNode* m_free
  0x08 S_PoolNode* m_spareNodes
  0x0c int m_growBy
  0x10 int m_freeCount
end

class C_ShellPool size=0x14 vtable=0x80fa58:1
  0x04 S_PoolNode* m_free
  0x08 S_PoolNode* m_spareNodes
  0x0c int m_growBy
  0x10 int m_freeCount
end

# ---- effects --------------------------------------------------------------------------
struct C_Shell size=0x40
  0x00 S_vector4 m_pos
  0x10 S_vector4 m_velocity
  0x20 I3D_frame* m_visual
  0x24 C_Human* m_owner
  0x28 int m_time
  0x2c int m_bounces
  0x30 int m_soundGroup             # chapter 5
end

struct C_MuzzleLight size=0x14
  0x00 I3D_frame* m_light           # "WeaponLight" (I3D_light)
  0x04 I3D_sector* m_sector
  0x08 I3D_frame* m_flash           # the weapon's flash visual (50 ms)
  0x0c int m_time
  0x10 int m_duration
end

struct C_BloodPool size=0x10
  0x00 I3D_visual* m_visual         # 'PROJ'
  0x04 float m_size
  0x08 float m_maxSize
  0x0c int m_time                   # -1 = grown
end

struct C_BloodDrop size=0x30
  0x00 S_vector4 m_pos
  0x10 S_vector4 m_velocity
  0x20 I3D_visual* m_visual         # 'PROJ'
  0x24 bool m_landed
  0x28 int m_time
end

# Boxes around the body parts of humans ("humanzones" model), g_hitZones 0x0089aa50.
struct C_HitZones size=0x68
  0x00 I3D_frame* m_model
  0x04 I3D_frame* m_zones[10]
  0x2c int m_joints[10]             # joint index of each zone
  0x54 char m_names[10][2]          # "c" "f" "l" "m" "h" "i" "s" "t" "o" "p"
end

# ---- the manager --------------------------------------------------------------------
class C_ShotManager : C_Module size=0x154 vtable=0x80fa18:16
  0x0c C_BulletShotPool m_bulletPool
  0x24 IImage* m_decalTextures[9]   # x_decal%02d.tga
  0x48 IImage* m_splatTextures[7]   # x_expl00.tga, x_blood00..05.tga
  0x64 C_ShellPool m_shellPool
  0x7c std::vector<S_ShotDecal*> m_decals
  0x8c std::vector<C_BloodDrop*> m_bloodDrops
  0x9c std::vector<C_BloodPool*> m_bloodPools
  0xac std::vector<C_MuzzleLight*> m_muzzleLights
  0xbc I3D_frame* m_weaponOdds      # "WeaponOdds" model
  0xc0 I3D_frame* m_traceModel      # "trace"
  0xc8 I3D_frame* m_shell3rd        # "shell3rd"
  0xcc I3D_frame* m_shellFpv        # "shellFPV"
  0xd0 S_SoundRequest m_sound
  0x110 std::vector<C_Shot*> m_shots
  0x120 std::vector<C_BulletShot*> m_clientBullets
  0x130 std::vector<C_Shell*> m_shells
  0x140 int m_clientTickTime
  0x148 int m_maxDecals
  0x14c int m_maxBloodDrops
  0x150 int m_maxBloodPools
end
