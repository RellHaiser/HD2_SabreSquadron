# LS3D engine math types (exported by LS3DF.DLL; layouts fixed by the DLL ABI).
# C++ side is hand-written in src/ls3d/math.h.

struct S_vector2 size=0x8
  0x00 float x
  0x04 float y
end

struct S_vector size=0xc
  0x00 float x
  0x04 float y
  0x08 float z
end

struct S_vector4 size=0x10
  0x00 float x
  0x04 float y
  0x08 float z
  0x0c float w
end

# Quaternion stored scalar-first, as in the .4ds/.bin file formats.
# Vector part first (identity = 0,0,0,1); files store the scalar first.
struct S_quat size=0x10
  0x00 S_vector v
  0x0c float s
end

struct S_matrix size=0x40
  0x00 float m[4][4]
end

struct S_plane size=0x10
  0x00 S_vector normal
  0x0c float d
end

# Engine bounding box (src/ls3d/i3d.h): corners padded to 16 bytes.
struct I3D_bbox size=0x20
  0x00 S_vector min
  0x0c float minW
  0x10 S_vector max
  0x1c float maxW
end

# Bounding box plus the sphere around it (I3D_particle_visual::SetBoundVolume).
struct I3D_bound_volume size=0x30
  0x00 I3D_bbox bbox
  0x20 S_vector center
  0x2c float radius
end

# Vertex of particle visual quads (I3D_particle_visual::LockQuads).
struct I3D_particle_vertex size=0x18
  0x00 S_vector pos
  0x0c uint32_t diffuse
  0x10 float u
  0x14 float v
end

# Result of I3D_scene::IntersectFrame (src/ls3d/i3d.h; only the members the game reads are known).
struct I3D_collision_info size=0x30
  0x00 float distance
  0x10 S_vector normal
  0x1c float normalW
end

# A texture key of an I3D_particle_visual (src/ls3d/i3d.h; the car effects pass it to AddKey).
struct I3D_emitter_key size=0x34
  0x00 float rect[4]                           # the texture rectangle
  0x10 int unknown10
  0x14 int unknown14
  0x18 int unknown18
  0x1c int unknown1C
  0x20 int unknown20
  0x24 void* unknown24[3]                      # an empty vector: begin, end, capacity
  0x30 int unknown30
end
