# Track module (source file 0x00401000-0x00405c5f)

struct S_TrackKey size=0x30
  0x00 S_vector pos
  0x0c float w
  0x10 S_quat rot
  0x20 float speed
end

struct S_TrackKeyBuffer size=0x10
  0x00 S_TrackKey* data
  0x04 int capacity
  0x08 int count
  0x0c int growBy
end

struct C_Track size=0x28
  0x00 std::string m_name
  0x10 S_TrackKeyBuffer m_keys
  0x20 float m_tension
  0x24 float m_unknown24
end

struct S_TrackPathPoint size=0x20
  0x00 S_vector pos
  0x0c float w
  0x10 float speed
end

struct C_TrackPlayer size=0x8070
  0x0000 S_vector m_startPos
  0x0010 S_vector m_curPos
  0x0020 S_quat m_curRot
  0x0030 S_vector m_frameRestorePos
  0x0040 I3D_frame* m_frame
  0x0044 I3D_frame* m_parent
  0x0048 C_Track* m_track
  0x004c int m_keyIndex
  0x0050 int m_unknown50
  0x0054 float m_tolerance
  0x0058 bool m_active
  0x005c int m_time
  0x0060 S_TrackPathPoint m_path[1024]
  0x8060 int m_pathIndex
end

class C_TrackModule : C_Module size=0x2c vtable=0x80f4d0:16
  0x0c std::vector<C_Track*> m_tracks
  0x1c std::vector<C_TrackPlayer*> m_players
end
