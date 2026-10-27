# Physics objects (source files 0x00405c60-0x00411590): doors, swinging and falling objects,
# cans, debris; C_PhysicsModule (g_physicsModule) keeps them and steps them.
#
# Scene frames whose user properties contain "phobj" become physics objects when a
# mission starts (type from the other words: "can", "cross", default door).

# Handle of a physics object (S_FrameData::physicsObject); index into the module's
# active (active=true) or resting list, -1 when the object is gone. g_noPhysicsObject
# points to a static {-1, false}.
struct S_PhysicsHandle size=0x8
  0x00 int index
  0x04 bool active
end

# Door part of S_PhysicsObjectDesc (user properties "free"/"axis", "angle", "break").
struct S_DoorParams size=0xc
  0x00 bool freeSwing
  0x01 bool tree
  0x02 bool breakable
  0x04 float angle
  0x08 C_str breakModel
end

# Parameters of C_PhysicsModule::CreateObject.
struct S_PhysicsObjectDesc size=0x2c
  0x00 I3D_frame* frame
  0x04 int type
  0x08 void* callback
  0x0c void* callbackContext
  0x10 int materialId
  0x14 bool unknown14
  0x15 bool collideWithBoxes
  0x16 bool startActive
  0x17 bool shootThrough
  0x18 int unknown18
  0x1c S_DoorParams door
  0x28 float radius
end

# Breakable fence registered with the temporary actors list (vehicles break it).
struct S_BreakableObject size=0xc
  0x00 I3D_frame* frame
  0x04 C_PhysDoor* door
  0x08 S_CollisionVolume* volume
end

# Base rigid body (vtable 0x80f598), 16-byte aligned allocation.
class C_PhysObject size=0x168 vtable=0x80f598:6
  0x010 S_vector m_velocity
  0x01c float m_velocityW
  0x020 S_vector m_angularVelocity
  0x02c float m_angularVelocityW
  0x030 S_vector m_pos
  0x03c float m_posW
  0x040 S_quat m_rot
  0x050 S_matrix m_invMatrix
  0x090 S_matrix m_matrix
  0x0d0 S_CollisionVolume m_volume
  0x130 float m_mass
  0x140 I3D_frame* m_frame
  0x144 bool m_unknown144
  0x145 bool m_collideWithBoxes
  0x148 int m_materialId
  0x14c void* m_callback
  0x150 S_PhysicsHandle* m_handle
  0x154 void* m_callbackContext
  0x158 bool m_resting
  0x15c int m_type
  0x160 bool m_volumeActive
  0x161 bool m_freezeVolume
  0x164 int m_state
end

# Type 0: point object that bounces off the collision world.
class C_PhysSimpleObject : C_PhysObject size=0x170 vtable=0x80f5f0:6
end

# Type 1: ball.
class C_PhysSphere : C_PhysObject size=0x180 vtable=0x80f624:7
  0x170 float m_radius
end

# Type 2: can ("can" in the user properties).
class C_PhysCan : C_PhysObject size=0x180 vtable=0x80f608:6
  0x170 float m_radius
end

# Type 3: tumbling box.
class C_PhysBox : C_PhysObject size=0x1d0 vtable=0x80f5d8:6
  0x170 S_vector m_axis1Start
  0x17c float m_axis1StartW
  0x180 S_vector m_axis1End
  0x18c float m_axis1EndW
  0x190 S_vector m_axis2Start
  0x19c float m_axis2StartW
  0x1a0 S_vector m_axis2End
  0x1ac float m_axis2EndW
  0x1b0 S_vector m_contact
  0x1bc float m_contactW
  0x1c0 S_vector m_rollAxis
  0x1cc float m_rollAxisW
end

# Type 5: door / gate ("phobj" frames by default).
class C_PhysDoor : C_PhysObject size=0x240 vtable=0x80f578:6
  0x170 S_vector m_center
  0x17c float m_centerW
  0x180 S_vector m_axis
  0x18c float m_axisW
  0x190 S_vector m_edge1
  0x19c float m_edge1W
  0x1a0 S_vector m_edge2
  0x1ac float m_edge2W
  0x1b0 S_vector m_hitDir
  0x1bc float m_hitDirW
  0x1c0 S_quat m_openRot1
  0x1d0 S_quat m_openRot2
  0x1e0 S_quat m_closedRot
  0x1f0 S_quat m_initialRot
  0x200 bool m_moving
  0x201 bool m_freeSwing
  0x202 bool m_rattling
  0x203 bool m_tree
  0x204 bool m_breakable
  0x205 C_str m_breakModel
  0x20c float m_phase
  0x210 float m_amplitude
  0x214 I3D_frame* m_originalFrame
  0x218 I3D_frame* m_brokenPart
  0x21c float m_damping
  0x220 float m_speed
  0x224 float m_openAngle
  0x228 float m_angle
  0x22c int m_swingState
  0x230 std::vector<S_VersionCollision*> m_breakVolumes
end

# Type 6: object swinging on its parent when hit ("cross").
class C_PhysSwingingObject : C_PhysObject size=0x2a0 vtable=0x80f55c:6
  0x170 S_quat m_swingRot1
  0x180 S_quat m_swingRot2
  0x190 S_quat m_restRot
  0x1a0 S_matrix m_matrix2
  0x1e0 S_matrix m_invMatrix2
  0x220 bool m_swinging
  0x224 float m_phase
  0x228 float m_amplitude
  0x230 S_CollisionVolume m_volume2
  0x290 I3D_frame* m_parent
  0x294 int m_swingState
end

class C_PhysFallingObject : C_PhysObject size=0x180 vtable=0x80f5b0:6
  0x170 bool m_sliding
  0x171 bool m_groundChecked
  0x174 float m_groundY
end

class C_PhysicsModule : C_Module size=0x5c vtable=0x80f640:20
  0x0c std::vector<C_PhysObject*> m_active
  0x1c std::vector<C_PhysObject*> m_resting
  0x2c std::vector<C_PhysObject*> m_doors
  0x3c std::vector<int> m_unknown3C
  0x4c std::vector<S_PhysicsHandle*> m_handles
end
