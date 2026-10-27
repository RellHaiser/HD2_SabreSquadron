# Collision world (g_collisionWorld 0x008cb480; code 0x006ee000..0x00702ba0, sphere sweeps
# 0x00702bb0..0x007182c0, wrappers 0x00718700..). Two parts:
#  - the static level collision loaded from a "GifC" file: a 3D grid of cells listing triangles
#    and primitives (S_CollisionMesh, owned at +0x3E4);
#  - dynamic volumes of objects (S_CollisionVolume) filed in a 2D grid (S_CollisionGrid, +0x3EC).
# Flag bits shared by volumes and primitives (+0x00): low byte = shape, 0x20 disabled (always
# excluded), 0x40 dynamic volume, 0x80 static primitive, 0x10000 water, 0x40000 two-sided
# triangle, 0x200000 skipped by bullets; top byte = material.

# Dynamic volume of an object.
struct S_CollisionVolume size=0x60
  0x00 uint32_t m_flags             # shape 0x40 oriented box, 0x41 world box, 0x42/0x44 sphere, 0x43 vertical cylinder, 0x46 scaled box, 0x49 convex, 0x4A capsule
  0x04 I3D_frame* m_frame
  0x08 uint32_t m_type              # filter bits (8 = ignored)
  0x0c S_CollisionGridCell* m_cell
  0x10 I3D_bbox m_bbox              # local box (0x40), world box (0x41)
  0x30 S_vector m_center            # world center of the bounding sphere
  0x3c float m_centerW
  0x40 S_matrix* m_matrix
  0x44 S_matrix* m_invMatrix
  0x48 C_Actor* m_actor
  0x4c float m_radius               # bounding sphere; capsules: half height
  0x50 float m_capsuleRadius
end

# Dynamic volume of shape 0x49: convex hull, up to 32 planes (pointing inward) in a visual's space.
struct S_CollisionConvexVolume size=0x270
  0x00 S_CollisionVolume volume
  0x60 uint32_t numPlanes
  0x68 I3D_frame* visual
  0x70 S_plane planes[32]
end

# Cell of the dynamic grid (allocated when the first volume enters, kept for reuse).
struct S_CollisionGridCell size=0x18
  0x00 int x
  0x04 int z
  0x08 std::vector<S_CollisionVolume*> volumes
end

struct S_CollisionGrid size=0x1c
  0x00 float minX
  0x04 float minZ
  0x08 float cellSizeX
  0x0c float cellSizeZ
  0x10 int cellsX
  0x14 int cellsZ
  0x18 S_CollisionGridCell** cells  # [cellsZ][cellsX], null = empty
end

# ---- static level collision ------------------------------------------------------------
# Triangle (cell entries of kind 0). Low byte of flags: projection used by the ray test (1..7).
struct S_CollisionTriangle size=0x20
  0x00 uint32_t flags               # 0x40000 two-sided, 0x20 degenerate
  0x04 S_vector4* v0
  0x08 S_vector4* v1
  0x0c S_vector4* v2
  0x10 S_vector normal
  0x1c float d                      # plane: dot(normal, p) + d = 0
end

# Primitive 0x80: oriented box.
struct S_CollisionBox size=0xc0
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 S_vector worldMin
  0x14 S_vector worldMax
  0x20 S_vector4 localMin
  0x30 S_vector4 localMax
  0x40 S_matrix matrix
  0x80 S_matrix invMatrix
end

# Primitive 0x81: world-aligned box.
struct S_CollisionAabb size=0x20
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 S_vector min
  0x14 S_vector max
end

# Primitive 0x82: sphere.
struct S_CollisionSphere size=0x20
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 float radius
  0x10 S_vector4 center
end

# Primitive 0x83: oriented rectangle (local y = 0 between min and max).
struct S_CollisionRect size=0xa0
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 S_vector min
  0x14 S_vector max
  0x20 S_matrix matrix
  0x60 S_matrix invMatrix
end

# Primitive 0x84: vertical cylinder (pole) without ends.
struct S_CollisionPole size=0x20
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 float x
  0x0c float z
  0x10 float radius
end

# Primitive 0x85: convex hull, up to 32 planes (pointing inward) in the space of its frame (a visual).
struct S_CollisionConvexPrim size=0x220
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 uint32_t numPlanes
  0x10 S_vector center              # world space
  0x20 S_plane planes[32]
end

# Primitive 0x87: capsule along its local y axis.
struct S_CollisionCapsule size=0x90
  0x00 uint32_t flags
  0x04 I3D_frame* frame
  0x08 float halfHeight
  0x0c float radius
  0x10 S_matrix matrix
  0x50 S_matrix invMatrix
end

# Cell of the static grid: count, entries (triangles / primitives) and its height range.
struct S_CollisionMeshCell size=0x10
  0x00 int count
  0x04 void** entries
  0x08 float maxY
  0x0c float minY
end

# Header of the loaded level collision (tables follow it in the same block).
struct S_CollisionMesh size=0xa4
  0x00 float minX
  0x04 float minZ
  0x08 float maxX
  0x0c float maxZ
  0x10 float cellSizeX
  0x14 float cellSizeZ
  0x18 int cellsX
  0x1c int cellsZ
  0x24 float minY
  0x2c float cellSizeY
  0x30 int cellsY
  0x34 float* gridLinesY
  0x3c float* gridLinesX
  0x40 float* gridLinesZ
  0x44 S_CollisionMeshCell** cells  # [cellsY][cellsZ][cellsX]
  0x48 int numTriangles
  0x4c S_CollisionTriangle* triangles
  0x50 int numBoxes
  0x54 S_CollisionBox* boxes
  0x58 int numAabbs
  0x5c S_CollisionAabb* aabbs
  0x60 int numSpheres
  0x64 S_CollisionSphere* spheres
  0x68 int numRects
  0x6c S_CollisionRect* rects
  0x70 int numPoles
  0x74 S_CollisionPole* poles
  0x78 int numVertices
  0x7c S_vector4* vertices
  0x80 int numConvexes
  0x84 S_CollisionConvexPrim* convexes
  0x88 int numPrims86
  0x8c void* prims86                # 0xA0 bytes
  0x90 int numCapsules
  0x94 S_CollisionCapsule* capsules
end

# Ray / segment query (RayTest variants); `dir`'s length is the reach.
struct S_CollisionRay size=0x90
  0x00 S_vector4 from
  0x10 S_vector4 dir
  0x20 S_vector boxMin              # bounding box of the segment
  0x2c S_vector boxMax
  0x40 S_vector4 to
  0x50 uint32_t excludeFlags        # volume / primitive flags (0x20 always)
  0x54 uint32_t excludeTypes        # volume types (8 always)
  0x58 float hitT                   # fraction of dir to the nearest hit (1 = none)
  0x5c float dirSquare
  0x60 float dirLength
  0x70 S_vector4 hitNormal          # pointing into the surface
  0x80 void* hit                    # volume, primitive or triangle
  0x84 uint8_t visited             # walk progress flags (not read)
end

# One volume / primitive / triangle crossed by a segment query.
struct S_CollisionHit size=0x30
  0x00 S_vector pos                 # where the segment enters it
  0x10 S_vector normal              # unit; out of static surfaces, into volumes
  0x20 S_CollisionVolume* volume    # or a primitive / triangle
  0x24 float distance               # fraction of the segment (sort key)
end

# Segment query (QuerySegment): everything the segment crosses, nearest first.
struct S_CollisionQuery size=0x40
  0x00 S_vector4 from
  0x10 S_vector4 dir                # its length is the reach
  0x20 uint32_t excludeTypes        # volume types (-1: no volumes)
  0x24 uint32_t excludeFlags        # static flags (-1: no static geometry)
  0x30 std::vector<S_CollisionHit> hits
end

# Sphere sweep (SphereTest): `best` = squared distance to the nearest contact so far (initially
# the reach squared; 0 / -1 = touching from the start); hitNormal points from the center to the surface.
struct S_SphereSweep size=0xc0
  0x00 S_vector4 from
  0x10 S_vector4 dir
  0x20 float radius
  0x24 S_vector boxMin
  0x30 S_vector boxMax
  0x40 S_vector4 to
  0x50 S_vector4 normDir
  0x60 uint32_t excludeFlags
  0x64 uint32_t excludeTypes
  0x68 float length
  0x6c float best
  0x70 S_vector side                # horizontal, across the path
  0x80 float slabMin                # the path's extent along side (as -dot(side, p))
  0x84 float slabMax
  0x90 S_vector4 hitPos
  0xa0 S_vector4 hitNormal
  0xb0 void* hit
end

# Contact plane of SlideSphere.
struct S_SlideContact size=0x30
  0x00 S_vector4 pos
  0x10 S_vector4 normal
  0x20 float d
  0x24 S_CollisionVolume* volume    # null: static geometry
end

class C_CollisionWorld size=0x42c
  0x00 int m_unknown00
  0x04 int m_unknown04
  0x08 bool m_ownBuffer             # load into m_buffer instead of a new block
  0x0c void* m_buffer
  0x10 int m_unknown10
  0x20 S_SlideContact m_slideContacts[20]
  0x3e0 int m_numSlideContacts
  0x3e4 S_CollisionMesh* m_mesh
  0x3e8 int m_meshSize
  0x3ec S_CollisionGrid* m_grid
  0x3f0 std::vector<S_CollisionGridCell*> m_freeCells
  0x400 std::vector<S_CollisionGridCell*> m_emptyCells
  0x410 int m_ignoredUnknown
  0x414 S_CollisionVolume* m_ignoredSingle
  0x418 S_CollisionVolume** m_ignored
  0x41c int m_ignoredCount
  0x424 int m_unknown424
  0x428 float m_unknown428          # 1e6 after a load
end
