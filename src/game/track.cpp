#include "game/track.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/c_str.h"
#include "common/chunk.h"
#include "game/game.h"
#include "ls3d/i3d.h"

C_TrackModule g_trackModule;                                 // 0x00867a64 (static init 0x00401040)

namespace {

// tracks.dat
constexpr uint16_t CHUNK_TRACKS = 0x2426;
constexpr uint16_t CHUNK_TRACK = 0x2454;
constexpr uint16_t CHUNK_TRACK_NAME = 0x2486;
constexpr uint16_t CHUNK_TRACK_UNKNOWN24 = 0x2490;
constexpr uint16_t CHUNK_TRACK_TENSION = 0x249A;
constexpr uint16_t CHUNK_TRACK_KEY = 0x24B8;
constexpr uint16_t CHUNK_KEY_POSITION = 0x251C;
constexpr uint16_t CHUNK_KEY_SPEED = 0x2580;
constexpr uint16_t CHUNK_KEY_ROTATION = 0x25E4;

// saved games
constexpr uint16_t CHUNK_SAVE_PLAYER = 0x029A;
constexpr uint16_t CHUNK_SAVE_FRAME = 1000;
constexpr uint16_t CHUNK_SAVE_PARENT = 2000;
constexpr uint16_t CHUNK_SAVE_POSITION = 3000;
constexpr uint16_t CHUNK_SAVE_KEY_INDEX = 3500;
constexpr uint16_t CHUNK_SAVE_PATH_INDEX = 3750;
constexpr uint16_t CHUNK_SAVE_TRACK = 4000;

constexpr int MAX_NAME_LENGTH = 999;

// Looks a frame up among the scene's model frames first, then all scene frames.
I3D_frame *FindSceneFrame(const char *name)
{
    I3D_scene *scene = GetGame()->GetScene();
    I3D_frame *frame = I3DGetDriver()->FindFrame(scene, name);
    if (!frame)
        frame = GetGame()->GetScene()->FindFrame(name, 0xCFFFF);
    return frame;
}

// Reads the rest of the current chunk as a name (the original caps it at 999 bytes).
C_str ReadName(C_chunk &chunk)
{
    char buffer[MAX_NAME_LENGTH + 1];
    chunk.ReadString(buffer, sizeof(buffer));
    return C_str(buffer);
}

float Distance(const S_vector &a, const S_vector &b)
{
    return (a - b).Magnitude();
}

} // namespace

// ---------------------------------------------------------------------------
// Curve helpers

S_vector HermiteInterpolate(const S_vector &p0, const S_vector &p1, const S_vector &m0,
                            const S_vector &m1, float t)
{
    const float t2 = t * t;
    const float t3 = t * t2;
    const float h00 = (t3 + t3) - t2 * 3.0f + 1.0f;
    const float h01 = (3.0f - (t + t)) * t * t;
    const float h10 = (t3 - (t2 + t2)) + t;
    const float h11 = t3 - t2;
    return p0 * h00 + p1 * h01 + m0 * h10 + m1 * h11;
}

S_vector BezierInterpolate(const S_vector &p0, const S_vector &p1, const S_vector &p2,
                           const S_vector &p3, float t)
{
    const float u = 1.0f - t;
    const float b0 = u * u * u;
    const float b1 = u * u * t * 3.0f;
    const float b2 = u * t * t * 3.0f;
    const float b3 = t * t * t;
    return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
}

// ---------------------------------------------------------------------------
// C_Track

int C_Track::AddKey(const S_vector &pos, const S_quat &rot, float speed)
{
    S_TrackKey key;
    key.pos = pos;
    key.w = 0.0f;
    key.rot = rot;
    key.speed = speed;
    m_keys.push_back(key);
    return NumKeys() - 1;
}

// ---------------------------------------------------------------------------
// C_TrackPlayer

C_TrackPlayer::C_TrackPlayer(C_Track *track, I3D_frame *frame, I3D_frame *parent)
    : m_frame(frame), m_parent(parent), m_track(track)
{
    if (frame)
        m_frameRestorePos = frame->GetWorldPos();

    const S_TrackKey &first = track->GetKey(0);
    m_startPos = first.pos;
    m_curPos = first.pos;
    m_curRot = first.rot;

    // The original calls SetPos before m_curPos is initialised (it reads uninitialised
    // memory); the next Update() overwrites it. Here the start position is used.
    if (frame) {
        frame->SetPos(m_curPos);
        frame->Update();
    }
    m_active = true;
}

bool C_TrackPlayer::GetOffsetFromStart(S_vector &offset, S_quat &rot) const
{
    if (!m_active)
        return false;
    rot = m_curRot;
    offset = m_curPos - m_startPos;
    return true;
}

float C_TrackPlayer::TimeToKey(const S_vector &pos, int keyIndex) const
{
    const S_TrackKey &prev = m_track->GetKey(keyIndex - 1);
    const S_TrackKey &next = m_track->GetKey(keyIndex);

    // Speed at `pos`, interpolated linearly by distance along the segment.
    float currentSpeed = 0.0f;
    const float segment = Distance(prev.pos, next.pos);
    if (segment != 0.0f)
        currentSpeed = (Distance(pos, prev.pos) / segment) * (next.speed - prev.speed) + prev.speed;

    const float averageSpeed = (currentSpeed + next.speed) * 0.5f;
    if (currentSpeed + next.speed == 0.0f)
        return 0.0f;
    return Distance(pos, next.pos) / averageSpeed;
}

float C_TrackPlayer::TimeToPathPoint(const S_vector &pos, int pointIndex) const
{
    const S_PathPoint &prev = m_path[pointIndex - 1];
    const S_PathPoint &next = m_path[pointIndex];

    const float currentSpeed = (Distance(pos, prev.pos) / Distance(prev.pos, next.pos)) *
                               (next.speed - prev.speed) + prev.speed;
    return Distance(pos, next.pos) / ((currentSpeed + next.speed) * 0.5f);
}

S_vector C_TrackPlayer::AccelerateTowardsKey(const S_vector &pos, int keyIndex, float time) const
{
    const S_TrackKey &from = m_track->GetKey(keyIndex);
    const S_TrackKey &to = m_track->GetKey(keyIndex + 1);

    const float traveled = Distance(pos, from.pos);
    const float segment = Distance(from.pos, to.pos);
    const float speedChange = to.speed - from.speed;
    // s = v*t + a*t^2/2 with the speed changing linearly over the segment.
    const float distance = speedChange * 0.5f * time * time * 0.5f +
                           ((speedChange / segment) * traveled + from.speed) * time;

    S_vector dir = to.pos - from.pos;
    dir.Normalize();
    return dir * distance + pos;
}

S_vector C_TrackPlayer::AccelerateTowardsPathPoint(const S_vector &pos, int pointIndex, float time) const
{
    const S_PathPoint &from = m_path[pointIndex];
    const S_PathPoint &to = m_path[pointIndex + 1];

    const float traveled = Distance(pos, from.pos);
    const float segment = Distance(from.pos, to.pos);
    const float speedChange = to.speed - from.speed;
    const float distance = (speedChange * 0.5f * time * 0.5f + (speedChange / segment) * traveled + from.speed) * time;

    S_vector dir = to.pos - from.pos;
    dir.Normalize();
    return dir * distance + pos;
}

S_vector C_TrackPlayer::AdvanceAlongKeys(const S_vector &from, int timeMs, bool *outDecelerating)
{
    S_vector pos = from;
    bool decelerating = false;

    if (m_keyIndex + 1 < m_track->NumKeys()) {
        float time = timeMs * 0.001f;
        for (;;) {
            const float timeToKey = TimeToKey(pos, m_keyIndex + 1);
            if (timeToKey > time)
                break;
            const int passed = m_keyIndex++;
            pos = m_track->GetKey(m_keyIndex).pos;
            time -= timeToKey;
            if (passed + 2 == m_track->NumKeys() || time <= timeToKey)
                break;
        }
        if (m_keyIndex + 1 < m_track->NumKeys()) {
            decelerating = m_track->GetKey(m_keyIndex + 1).speed < m_track->GetKey(m_keyIndex).speed;
            pos = AccelerateTowardsKey(pos, m_keyIndex, time);
        }
    }
    if (outDecelerating && decelerating)
        *outDecelerating = true;
    return pos;
}

void C_TrackPlayer::SubdivideBezier(const S_vector &p0, const S_vector &p1, const S_vector &p2,
                                    const S_vector &p3, float tolerance, float &length, int &budget)
{
    --budget;

    // Flatness: twice the areas of the two triangles spanned by the control polygon.
    const float area1 = std::fabs((p1 - p0).Cross(p2 - p1).Magnitude());
    const float area2 = std::fabs((p2 - p0).Cross(p3 - p2).Magnitude());

    if (tolerance <= area1 + area2 && budget != 0) {
        // De Casteljau split at t = 0.5.
        const S_vector l1 = (p0 + p1) * 0.5f;
        const S_vector m = (p1 + p2) * 0.5f;
        const S_vector l2 = (l1 + m) * 0.5f;
        const S_vector r2 = (p2 + p3) * 0.5f;
        const S_vector r1 = (m + r2) * 0.5f;
        const S_vector mid = (l2 + r1) * 0.5f;
        SubdivideBezier(p0, l1, l2, mid, tolerance, length, budget);
        SubdivideBezier(mid, r1, r2, p3, tolerance, length, budget);
        return;
    }

    // Flat enough: emit the end point.
    length += Distance(m_path[m_pathIndex - 1].pos, p3);
    m_path[m_pathIndex].pos = p3;
    m_path[m_pathIndex].speed = 1.0f;
    ++m_pathIndex;
    ++budget;
}

void C_TrackPlayer::BuildPath(int keyIndex)
{
    if (keyIndex + 2 == m_track->NumKeys())
        return;

    const S_TrackKey &key0 = m_track->GetKey(keyIndex);
    const S_TrackKey &key1 = m_track->GetKey(keyIndex + 1);
    const float tension = m_track->m_tension;

    // Bezier control points with Catmull-Rom style tangents.
    const S_vector p0 = key0.pos;
    const S_vector p3 = key1.pos;
    S_vector c1 = p0;
    if (keyIndex != 0)
        c1 = p0 + (p3 - m_track->GetKey(keyIndex - 1).pos) * tension;
    S_vector c2 = p3;
    if (keyIndex < m_track->NumKeys() - 2)
        c2 = p3 + (p0 - m_track->GetKey(keyIndex + 2).pos) * tension;

    m_pathIndex = 0;
    m_path[0].pos = p0;
    m_path[0].speed = key0.speed;
    ++m_pathIndex;

    float length = 0.0f;
    int budget = 10;
    const float tolerance = m_tolerance <= 0.0001f ? 0.0001f : m_tolerance;
    SubdivideBezier(p0, c1, c2, p3, tolerance, length, budget);
    m_path[m_pathIndex].speed = -1.0f;

    // Speed changes linearly with the distance travelled along the curve.
    float distance = 0.0f;
    for (int i = 1; i < m_pathIndex; ++i) {
        distance += Distance(m_path[i].pos, m_path[i - 1].pos);
        m_path[i].speed = ((key1.speed - key0.speed) / length) * distance + key0.speed;
    }
}

float C_TrackPlayer::MoveAlongPath(S_vector &pos, float time)
{
    if (m_path[m_pathIndex + 1].speed < 0.0f)
        return 0.0f;

    for (;;) {
        const float timeToPoint = TimeToPathPoint(pos, m_pathIndex + 1);
        if (timeToPoint > time)
            break;
        ++m_pathIndex;
        pos = m_path[m_pathIndex].pos;
        time -= timeToPoint;
        if (m_path[m_pathIndex + 1].speed < 0.0f || time <= timeToPoint)
            break;
    }
    if (m_path[m_pathIndex + 1].speed < 0.0f)
        return time;

    pos = AccelerateTowardsPathPoint(pos, m_pathIndex, time);
    return 0.0f;
}

S_vector C_TrackPlayer::Advance(const S_vector &from, float time)
{
    S_vector pos = from;
    if (m_pathIndex == -1) {
        BuildPath(0);
        m_pathIndex = 0;
    }

    float remaining = MoveAlongPath(pos, time);
    while (remaining != 0.0f) {
        const int finished = m_keyIndex++;
        if (finished + 2 == m_track->NumKeys())
            pos = m_track->GetKey(m_track->NumKeys() - 1).pos;
        BuildPath(m_keyIndex);
        m_pathIndex = 0;
        remaining = MoveAlongPath(pos, remaining);
    }
    return pos;
}

void C_TrackPlayer::Update(int timeDelta)
{
    if (m_keyIndex + 2 >= m_track->NumKeys())
        return;

    const S_vector pos = Advance(m_curPos, timeDelta * 0.001f);
    if (m_frame) {
        if (m_parent) {
            // Track coordinates are relative to the first key, in the parent's space.
            m_frame->SetPos((pos - m_track->GetKey(0).pos) * m_parent->GetMatrix());
        } else {
            m_frame->SetPos(pos);
        }
        m_frame->Update();
    }
    m_curPos = pos;

    if (m_keyIndex < m_track->NumKeys() - 1) {
        const S_TrackKey &from = m_track->GetKey(m_keyIndex);
        const S_TrackKey &to = m_track->GetKey(m_keyIndex + 1);
        const float fraction = Distance(from.pos, pos) / Distance(from.pos, to.pos);
        const S_quat rot = from.rot.Slerp(to.rot, fraction, false);
        if (m_frame) {
            m_frame->SetRot(m_parent ? rot.RotateByMatrix(m_parent->GetMatrix()) : rot);
            m_frame->Update();
        }
        m_curRot = rot;
    } else {
        m_active = false;
    }

    if (m_frame) {
        S_vector probe = m_frame->GetWorldPos();
        probe.y += 0.2f;
        GetGame()->GetScene()->UpdateFrameSector(m_frame, probe);
    }
    m_time += timeDelta;
}

S_vector C_TrackPlayer::FindPointAtDistance(int keyIndex, const S_vector &center, float radius,
                                            int *outKeyIndex) const
{
    // Walk back while the key is still inside the sphere.
    while (Distance(m_track->GetKey(keyIndex).pos, center) < radius && keyIndex > 0)
        --keyIndex;
    if (m_track->NumKeys() <= keyIndex + 1)
        keyIndex = m_track->NumKeys() - 2;

    // Intersect the segment line p0 + t*d with the sphere |x - center| = radius.
    const S_vector p0 = m_track->GetKey(keyIndex).pos;
    const S_vector d = m_track->GetKey(keyIndex + 1).pos - p0;
    const double a = d.Dot(d);
    const double b = 2.0 * d.Dot(p0 - center);
    const double c = (p0 - center).Square() - static_cast<double>(radius) * radius;
    const double root = std::sqrt(b * b - c * a * 4.0);
    const float t1 = static_cast<float>((root - b) / (a + a));
    const double t2 = (-b - root) / (a + a);
    const double t = std::min<double>(t1, t2);

    if (outKeyIndex)
        *outKeyIndex = keyIndex;
    return S_vector(static_cast<float>(d.x * t + p0.x), static_cast<float>(d.y * t + p0.y),
                    static_cast<float>(d.z * t + p0.z));
}

// ---------------------------------------------------------------------------
// C_TrackModule

C_TrackModule::C_TrackModule()
    : C_Module(MODULE_TRACK, "Track module")
{
}

C_TrackModule::~C_TrackModule() = default;

bool C_TrackModule::AddTrack(const char *name)
{
    if (FindTrack(name))
        return false;
    m_tracks.push_back(new C_Track(name));
    return true;
}

C_Track *C_TrackModule::FindTrack(const char *name, int *outIndex) const
{
    for (int i = static_cast<int>(m_tracks.size()) - 1; i >= 0; --i) {
        if (_stricmp(name, m_tracks[i]->GetName()) == 0) {
            if (outIndex)
                *outIndex = i;
            return m_tracks[i];
        }
    }
    if (outIndex)
        *outIndex = -1;
    return nullptr;
}

C_TrackPlayer *C_TrackModule::CreatePlayer(const char *trackName, I3D_frame *frame, I3D_frame *parent)
{
    // A frame follows at most one track.
    if (frame) {
        for (int i = static_cast<int>(m_players.size()) - 1; i >= 0; --i) {
            if (m_players[i]->m_frame == frame) {
                delete m_players[i];
                m_players[i] = m_players.back();
                m_players.pop_back();
                break;
            }
        }
    }

    C_Track *track = FindTrack(trackName);
    if (!track)
        return nullptr;

    C_TrackPlayer *player = new C_TrackPlayer(track, frame, parent);
    m_players.push_back(player);
    return player;
}

void C_TrackModule::RemovePlayer(C_TrackPlayer *player)
{
    for (int i = static_cast<int>(m_players.size()) - 1; i >= 0; --i) {
        if (m_players[i] == player) {
            delete m_players[i];
            m_players[i] = m_players.back();
            m_players.pop_back();
            return;
        }
    }
}

void C_TrackModule::Update(int timeDelta)
{
    if (!GetGame()->IsMissionRunning())
        return;
    for (int i = static_cast<int>(m_players.size()) - 1; i >= 0; --i)
        m_players[i]->Update(timeDelta);
}

void C_TrackModule::LoadMissionData(bool server)
{
    if (!server)
        LoadTracks();
}

void C_TrackModule::FreeMissionData(bool server)
{
    if (!server)
        FreeTracks();
}

void C_TrackModule::StopMission(bool server)
{
    if (server)
        return;
    // Put every frame back where it was before its track took over.
    for (int i = static_cast<int>(m_players.size()) - 1; i >= 0; --i) {
        C_TrackPlayer *player = m_players[i];
        if (player->m_frame) {
            player->m_frame->SetPos(player->m_frameRestorePos);
            player->m_frame->Update();
        }
        delete player;
    }
    m_players.clear();
}

void C_TrackModule::RestartMission(bool /*server*/)
{
    StopMission(false);
    StartMission(false);
}

void C_TrackModule::FreeTracks()
{
    for (int i = static_cast<int>(m_tracks.size()) - 1; i >= 0; --i)
        delete m_tracks[i];
    // Players reference tracks; they were already destroyed by StopMission.
    m_players.clear();
    m_tracks.clear();
}

bool C_TrackModule::LoadTracks()
{
    FreeTracks();

    const C_fstr fileName("missions\\%s\\%s", GetGame()->GetMissionName(), "tracks.dat");
    C_chunk chunk;
    if (!chunk.Open(fileName, false))
        return false;

    uint16_t id;
    if (!chunk.Descend(id) || id != CHUNK_TRACKS)
        return false;

    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id != CHUNK_TRACK) {
            chunk.Ascend();
            continue;
        }

        C_Track *track = nullptr;
        while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_TRACK_NAME: {
                const C_str name = ReadName(chunk);
                chunk.Ascend();
                AddTrack(name);
                track = FindTrack(name);
                break;
            }
            case CHUNK_TRACK_UNKNOWN24: {
                float value = 0.0f;
                chunk.Read(value);
                chunk.Ascend();
                if (track)
                    track->m_unknown24 = value;
                break;
            }
            case CHUNK_TRACK_TENSION: {
                float value = 0.0f;
                chunk.Read(value);
                chunk.Ascend();
                if (track)
                    track->m_tension = value;
                break;
            }
            case CHUNK_TRACK_KEY: {
                S_vector pos(0.0f, 0.0f, 0.0f);
                S_quat rot(1.0f, 0.0f, 0.0f, 0.0f);
                float speed = 1.0f;
                uint16_t keyChunk;
                while (chunk.RemainingSize() > 0 && chunk.Descend(keyChunk)) {
                    if (keyChunk == CHUNK_KEY_POSITION)
                        chunk.Read(&pos, sizeof(pos));
                    else if (keyChunk == CHUNK_KEY_SPEED)
                        chunk.Read(speed);
                    else if (keyChunk == CHUNK_KEY_ROTATION) {
                        float f[4];
                        chunk.Read(f, sizeof(f));
                        rot = S_quat::FromFile(f);
                    }
                    chunk.Ascend();
                }
                if (track)
                    track->AddKey(pos, rot, speed);
                chunk.Ascend();
                break;
            }
            default:
                chunk.Ascend();
                break;
            }
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    chunk.Close();
    return true;
}

bool C_TrackModule::SaveGame(C_chunk &chunk)
{
    const int count = static_cast<int>(m_players.size());
    chunk.Write(count);

    for (C_TrackPlayer *player : m_players) {
        chunk.Begin(CHUNK_SAVE_PLAYER);
        chunk.WriteStringChunk(CHUNK_SAVE_FRAME, player->m_frame->GetName());
        if (player->m_parent)
            chunk.WriteStringChunk(CHUNK_SAVE_PARENT, player->m_parent->GetName());
        chunk.WriteChunk(CHUNK_SAVE_POSITION, &player->m_frame->GetWorldPos(), sizeof(S_vector));
        chunk.WriteChunk(CHUNK_SAVE_KEY_INDEX, player->m_keyIndex);
        chunk.WriteChunk(CHUNK_SAVE_PATH_INDEX, player->m_pathIndex);
        chunk.WriteStringChunk(CHUNK_SAVE_TRACK, player->m_track->GetName());
        chunk.Ascend();
    }
    return true;
}

bool C_TrackModule::LoadGame(C_chunk &chunk)
{
    int count = 0;
    chunk.Read(count);

    C_str frameName = "";
    C_str parentName = "";
    I3D_frame *parent = nullptr;
    S_vector pos(0.0f, 0.0f, 0.0f);
    int keyIndex = 0;
    int pathIndex = 0;

    for (int i = 0; i < count; ++i) {
        uint16_t id;
        if (!chunk.Descend(id))
            break;

        while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
            switch (id) {
            case CHUNK_SAVE_KEY_INDEX:
                chunk.Read(keyIndex);
                chunk.Ascend();
                break;
            case CHUNK_SAVE_FRAME:
                frameName = ReadName(chunk);
                chunk.Ascend();
                break;
            case CHUNK_SAVE_PARENT:
                parentName = ReadName(chunk);
                chunk.Ascend();
                parent = FindSceneFrame(parentName);
                break;
            case CHUNK_SAVE_POSITION:
                chunk.Read(&pos, sizeof(pos));
                chunk.Ascend();
                break;
            case CHUNK_SAVE_PATH_INDEX:
                chunk.Read(pathIndex);
                chunk.Ascend();
                break;
            case CHUNK_SAVE_TRACK: {
                // The track name comes last: everything needed is known now.
                const C_str trackName = ReadName(chunk);
                chunk.Ascend();
                if (I3D_frame *frame = FindSceneFrame(frameName)) {
                    if (C_TrackPlayer *player = CreatePlayer(trackName, frame, parent)) {
                        player->m_keyIndex = keyIndex;
                        player->m_curPos = pos;
                        if (pathIndex != -1)
                            player->BuildPath(keyIndex);
                        player->m_pathIndex = pathIndex;
                    }
                    frame->SetPos(pos);
                    frame->Update();
                }
                frameName = "";
                parentName = "";
                parent = nullptr;
                pathIndex = 0;
                keyIndex = 0;
                break;
            }
            default:
                chunk.Ascend();
                break;
            }
        }
        chunk.Ascend();
    }
    return true;
}
