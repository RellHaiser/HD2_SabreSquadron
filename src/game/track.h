// Track module — scripted motion of scene frames along key-framed spline tracks.
//
// Tracks are loaded per mission from missions\<mission>\tracks.dat. A track is a
// list of keys (position, rotation, speed). A track player attaches a frame to a
// track and moves it every game tick: the segment between two keys is turned into
// a cubic Bezier curve (Catmull-Rom style tangents scaled by the track tension),
// flattened into a polyline, and traversed with linearly changing speed.
// Rotation is slerped between the keys. Positions can be relative to a parent frame.
#pragma once

#include <string>
#include <vector>

#include "game/module.h"
#include "ls3d/math.h"

class C_chunk;
class I3D_frame;

struct S_TrackKey {                               // 0x30 bytes in the original
    S_vector pos;                                 // +0x00
    float w;                                      // +0x0C (carried along, unused)
    S_quat rot;                                   // +0x10
    float speed;                                  // +0x20  metres per second
};

class C_Track {                                   // 0x28 bytes in the original
public:
    explicit C_Track(const char *name) : m_name(name) {}

    const char *GetName() const { return m_name.c_str(); }
    int AddKey(const S_vector &pos, const S_quat &rot, float speed);   // 0x00402880
    int NumKeys() const { return static_cast<int>(m_keys.size()); }
    const S_TrackKey &GetKey(int i) const { return m_keys[i]; }

    float m_tension = 0.1f;                       // +0x20  tangent scale for the curve
    float m_unknown24 = 0.5f;                     // +0x24  loaded but not used by the player

private:
    std::string m_name;                           // +0x00
    std::vector<S_TrackKey> m_keys;               // +0x10
};

class C_TrackPlayer {                             // 0x8070 bytes in the original
public:
    C_TrackPlayer(C_Track *track, I3D_frame *frame, I3D_frame *parent);
    // Without a track or frame (the train's, inline in its constructor 0x005bcfb0).
    C_TrackPlayer() : m_frame(nullptr), m_parent(nullptr), m_track(nullptr) {}

    void Update(int timeDelta);                                          // 0x00404890
    // Movement since the start of the track and the current rotation; false if finished.
    bool GetOffsetFromStart(S_vector &offset, S_quat &rot) const;        // 0x004029f0
    // Key-based (non-spline) movement (the train's).
    S_vector AdvanceAlongKeys(const S_vector &from, int timeMs, bool *outDecelerating);  // 0x00402a70
    // Point on the polyline of keys at `radius` from `center`, searching back from `keyIndex`.
    S_vector FindPointAtDistance(int keyIndex, const S_vector &center, float radius,
                                 int *outKeyIndex) const;                // 0x00404c80
    void BuildPath(int keyIndex);                                        // 0x004042e0

    I3D_frame *GetFrame() const { return m_frame; }
    C_Track *GetTrack() const { return m_track; }
    void SetTrack(C_Track *track) { m_track = track; }
    int GetKeyIndex() const { return m_keyIndex; }
    void SetKeyIndex(int index) { m_keyIndex = index; }

private:
    friend class C_TrackModule;

    struct S_PathPoint {                          // 0x20 bytes
        S_vector pos;
        float speed;                              // negative marks the end of the path
    };
    static constexpr int MAX_PATH_POINTS = 1024;

    float TimeToKey(const S_vector &pos, int keyIndex) const;            // 0x00402bd0
    float TimeToPathPoint(const S_vector &pos, int pointIndex) const;    // 0x00402ed0
    S_vector AccelerateTowardsKey(const S_vector &pos, int keyIndex, float time) const;          // 0x004030d0
    S_vector AccelerateTowardsPathPoint(const S_vector &pos, int pointIndex, float time) const;  // 0x004034c0
    void SubdivideBezier(const S_vector &p0, const S_vector &p1, const S_vector &p2,
                         const S_vector &p3, float tolerance, float &length, int &budget);       // 0x00403bc0
    // Moves `pos` along the current path for `time` seconds; returns the unused time.
    float MoveAlongPath(S_vector &pos, float time);                      // 0x00404620
    S_vector Advance(const S_vector &from, float time);                  // 0x00404740

    S_vector m_startPos;                          // +0x00
    S_vector m_curPos;                            // +0x10
    S_quat m_curRot;                              // +0x20
    S_vector m_frameRestorePos;                   // +0x30  frame position before the track took over
    I3D_frame *m_frame;                           // +0x40
    I3D_frame *m_parent;                          // +0x44  coordinate space of the track (optional)
    C_Track *m_track;                             // +0x48
    int m_keyIndex = 0;                           // +0x4C
    int m_unknown50 = 0;                          // +0x50
    float m_tolerance = 0.5f;                     // +0x54  curve flattening tolerance
    bool m_active = false;                        // +0x58
    int m_time = 0;                               // +0x5C  milliseconds played
    S_PathPoint m_path[MAX_PATH_POINTS];          // +0x60
    int m_pathIndex = -1;                         // +0x8060 point count while building, then cursor
};

class C_TrackModule : public C_Module {
public:
    C_TrackModule();                                    // 0x00401060 (takes the module id 0 and name)
    ~C_TrackModule() override;                                           // 0x004010e0

    void Update(int timeDelta) override;                                 // 0x00401fd0
    void LoadMissionData(bool server) override;                          // 0x00402020 (DS 0x0041b4d0)
    void FreeMissionData(bool server) override;                          // 0x00402030 (DS 0x0041b4f0)
    void StartMission(bool server) override {}                           // 0x004010b0
    void StopMission(bool server) override;                              // 0x00401f40
    void RestartMission(bool server) override;                           // 0x00401140
    bool SaveGame(C_chunk &chunk) override;                              // 0x00401160
    bool LoadGame(C_chunk &chunk) override;                              // 0x00401440 (DS 0x0041a5c0)

    bool AddTrack(const char *name);                                     // 0x00401d40
    C_Track *FindTrack(const char *name, int *outIndex = nullptr) const; // 0x00401ed0
    // Starts moving `frame` along the named track (replacing any player on that frame).
    C_TrackPlayer *CreatePlayer(const char *trackName, I3D_frame *frame, I3D_frame *parent);  // 0x00401b20
    void RemovePlayer(C_TrackPlayer *player);                            // 0x00401a90

private:
    bool LoadTracks();                                                   // 0x00402120
    void FreeTracks();                                                   // 0x00402040

    std::vector<C_Track *> m_tracks;                                     // +0x0C
    std::vector<C_TrackPlayer *> m_players;                              // +0x1C
};

extern C_TrackModule g_trackModule;                                      // 0x00867a64

// Curve helpers (returned through function-local statics in the original).
S_vector HermiteInterpolate(const S_vector &p0, const S_vector &p1, const S_vector &m0,
                            const S_vector &m1, float t);                // 0x004038b0
S_vector BezierInterpolate(const S_vector &p0, const S_vector &p1, const S_vector &p2,
                           const S_vector &p3, float t);                 // 0x00403a50
