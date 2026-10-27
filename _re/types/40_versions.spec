# Destructible objects ("versions", source file 0x0043ad80-0x0043f1e0)
#
# A scene frame whose user properties contain "version=" is damageable. Its
# replacement stages come from a separate model (versions directory) with frames
# of the same name; each stage frame carries "version=<hit points>" / "sound=".

# One damageable frame.
struct C_VersionKeeper size=0x68
  0x00 I3D_frame* m_frame
  0x04 C_VersionMaster* m_master
  0x08 int m_hitPoints
  0x0c int m_particleEffect
  0x10 int m_materialId
  0x14 int m_version
  0x18 bool m_versionsLoaded
  0x1c std::vector<int> m_versionHitPoints
  0x2c std::vector<I3D_frame*> m_versionFrames
  0x3c std::vector<I3D_frame*> m_debrisFrames
  0x4c std::vector<int> m_versionSounds
  0x5c int m_numVersions
  0x60 int m_index
  0x64 bool m_transparent
end

# S_CollisionVolume: see 35_collision.spec.

# Collision box of a damageable model (16-byte aligned allocation).
struct S_VersionCollision size=0xe0
  0x00 S_matrix m_invMatrix
  0x40 S_matrix m_matrix
  0x80 S_CollisionVolume m_volume
end

# A damageable model (scene model containing keepers).
struct C_VersionMaster size=0x2c
  0x00 int m_refCount
  0x04 bool m_removed
  0x08 I3D_frame* m_frame
  0x0c I3D_model* m_versionsModel
  0x10 std::vector<C_VersionKeeper*> m_keepers
  0x20 S_VersionCollision* m_collision
  0x24 int m_index
  0x28 bool m_damaged
end

class C_VersionsModule : C_Module size=0x3c vtable=0x80f908:16
  0x0c std::vector<C_VersionMaster*> m_masters
  0x1c std::vector<S_VersionCollision*> m_keptCollisions
  0x2c std::vector<I3D_frame*> m_damagedModels
end

# C_CollisionWorld: see 35_collision.spec.
