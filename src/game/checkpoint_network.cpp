// A checkpoint network: its data, tables, joins and spatial lookups (see checkpoints.h; the
// reachability queries and path searches are in checkpoint_search.cpp).
#include "game/checkpoints.h"

#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/game.h"
#include "ls3d/i3d.h"

namespace {

// The saved game chunk of the joins (in C_Checkpoints's chunk 0x81).
constexpr uint16_t CHUNK_CHECKPOINT_JOINS = 0x4C5;

// The checkpoint data's header (version 9): version, size, then the counts.
constexpr int HEADER_SIZE = 0x1C;

// Points are looked up in columns ("cells") of x: CELL_SIZE metres, sorted by z within each.
constexpr float CELL_SIZE = 10.0f;
// The largest point radius (255/16 m, rounded up); the radius-aware lookups keep this margin.
constexpr float MAX_RADIUS = 16.0f;

// The y range the lookups accept.
constexpr float MAX_HEIGHT = 100000.0f;

uint16_t ReadU16(const uint8_t *data)
{
    uint16_t v;
    std::memcpy(&v, data, 2);
    return v;
}

// Looks a frame up among the scene's model frames first, then all scene frames (as the track
// loader does).
I3D_frame *FindSceneFrame(const char *name)
{
    I3D_frame *frame = I3DGetDriver()->FindFrame(GetGame()->GetScene(), name);
    if (!frame)
        frame = GetGame()->GetScene()->FindFrame(name, FRAME_MASK_ALL);
    return frame;
}

// The data after `name`'s NUL.
const char *SkipName(const char *name)
{
    return name + std::strlen(name) + 1;
}

} // namespace

// --- tables ----------------------------------------------------------------------------------

const uint8_t *S_CheckpointName::Read(const uint8_t *data)   // 0x0051c7b0
{
    index = ReadU16(data);
    std::memcpy(name, data + 2, sizeof(name));
    return data + 0x12;
}

const uint8_t *S_CheckpointFrame::Read(const uint8_t *data)   // 0x0051c870
{
    index = ReadU16(data);
    frame = nullptr;
    const char *name = reinterpret_cast<const char *>(data + 2);
    if (*name != '\0')
        frame = FindSceneFrame(name);
    return reinterpret_cast<const uint8_t *>(SkipName(name));
}

const uint8_t *S_CheckpointActor::Read(const uint8_t *data)   // 0x0051c910
{
    index = ReadU16(data);
    actor = nullptr;
    const char *name = reinterpret_cast<const char *>(data + 2);
    if (*name != '\0')
        actor = GetFrameActor(FindSceneFrame(name));
    return reinterpret_cast<const uint8_t *>(SkipName(name));
}

// --- data ------------------------------------------------------------------------------------

void C_CheckpointNetwork::Clear()                       // 0x0051dd60
{
    delete[] m_points;
    m_points = nullptr;
    delete[] m_linkStorage;
    m_linkStorage = nullptr;
    m_pointCount = 0;
    delete[] m_names;
    m_names = nullptr;
    m_nameCount = 0;
    delete[] m_frames;
    m_frames = nullptr;
    m_frameCount = 0;
    delete[] m_actors;
    m_actors = nullptr;
    m_actorCount = 0;
    delete[] m_searchCosts;
    m_searchCosts = nullptr;
    delete[] m_searchPrev;
    m_searchPrev = nullptr;
}

const uint8_t *C_CheckpointNetwork::Load(const uint8_t *data)   // 0x0051de80
{
    Clear();
    uint32_t version;
    std::memcpy(&version, data, 4);
    if (version != CHECKPOINTS_VERSION_9)
        return nullptr;

    // Header: version, size, point count, link slots, name / frame / actor counts.
    int linkSlots;
    m_pointCount = ReadU16(data + 0x08);
    std::memcpy(&linkSlots, data + 0x0C, 4);
    m_nameCount = ReadU16(data + 0x10);
    m_frameCount = ReadU16(data + 0x14);
    m_actorCount = ReadU16(data + 0x18);
    if (m_pointCount != 0) {
        m_points = new S_Checkpoint[m_pointCount];
        m_searchCosts = new uint32_t[m_pointCount];
        m_searchPrev = new uint16_t[m_pointCount];
    }
    if (linkSlots != 0)
        m_linkStorage = new S_CheckpointLink[linkSlots];
    if (m_nameCount != 0)
        m_names = new S_CheckpointName[m_nameCount];
    if (m_frameCount != 0)
        m_frames = new S_CheckpointFrame[m_frameCount];
    if (m_actorCount != 0)
        m_actors = new S_CheckpointActor[m_actorCount];

    // The records follow in this order; each point's links are carved out of the storage.
    S_CheckpointLink *storage = m_linkStorage;
    const uint8_t *next = data + HEADER_SIZE;
    for (int i = 0; i < m_pointCount; ++i)
        next = m_points[i].Read(next, storage);
    for (int i = 0; i < m_nameCount; ++i)
        next = m_names[i].Read(next);
    for (int i = 0; i < m_frameCount; ++i)
        next = m_frames[i].Read(next);
    for (int i = 0; i < m_actorCount; ++i)
        next = m_actors[i].Read(next);
    return next;
}

// --- points and tables ------------------------------------------------------------------------

S_Checkpoint *C_CheckpointNetwork::Get(int index)       // 0x0051d5a0
{
    const uint16_t i = static_cast<uint16_t>(index);
    if (i >= m_pointCount)
        return nullptr;
    return &m_points[i];
}

int C_CheckpointNetwork::Find(const char *name) const   // 0x0051e1a0
{
    for (int i = 0; i < m_nameCount; ++i) {
        if (_strnicmp(m_names[i].name, name, sizeof(m_names[i].name)) == 0)
            return m_names[i].index;
    }
    return CHECKPOINT_NONE;
}

void C_CheckpointNetwork::FindByPrefix(const char *prefix, std::vector<uint16_t> &out) const   // 0x0051c9c0
{
    // Fix: the original tested strstr(name, prefix) == name, which reads on past a name of
    // all 16 characters (no NUL) into the next entry.
    const size_t length = std::strlen(prefix);
    for (int i = m_nameCount; i-- > 0;) {
        const S_CheckpointName &entry = m_names[i];
        if (length <= sizeof(entry.name) && std::strncmp(entry.name, prefix, length) == 0)
            out.push_back(entry.index);
    }
}

C_Actor *C_CheckpointNetwork::GetActor(int index) const   // 0x0051e520
{
    const int count = m_actorCount;
    if (count == 0)
        return nullptr;
    const S_CheckpointActor *table = m_actors;
    const uint16_t key = static_cast<uint16_t>(index);
    if (key > table[count - 1].index)
        return nullptr;
    if (table[count - 1].index == key)
        return table[count - 1].actor;
    if (key < table[0].index)
        return nullptr;
    if (table[0].index == key)
        return table[0].actor;
    int lo = 0;
    int hi = count - 1;
    while (hi - lo > 1) {
        const int mid = (lo + hi) >> 1;
        if (table[mid].index == key)
            return table[mid].actor;
        if (table[mid].index < key)
            lo = mid;
        else
            hi = mid;
    }
    return nullptr;
}

int C_CheckpointNetwork::FindActorPoint(const C_Actor *actor) const   // 0x0051e690
{
    for (int i = m_actorCount; i-- > 0;) {
        if (m_actors[i].actor == actor)
            return m_actors[i].index;
    }
    return CHECKPOINT_NONE;
}

void C_CheckpointNetwork::GetActorPoints(const C_Actor *actor, std::vector<uint16_t> &out) const   // 0x0051e6d0
{
    out.clear();
    for (int i = m_actorCount; i-- > 0;) {
        if (m_actors[i].actor == actor)
            out.push_back(m_actors[i].index);
    }
}

// --- joins and state ---------------------------------------------------------------------------

bool C_CheckpointNetwork::JoinPoints(int a, int b)      // 0x0051f280
{
    S_Checkpoint *pointA = Get(a);
    S_Checkpoint *pointB = Get(b);
    if (!pointA || !pointB)
        return false;
    // (When only the first link fits, it stays without a join recorded.)
    if (!pointA->AddJoinedLink(static_cast<int16_t>(b), pointB) ||
        !pointB->AddJoinedLink(static_cast<int16_t>(a), pointA))
        return false;
    m_joins.push_back(static_cast<uint16_t>(a) | static_cast<uint32_t>(static_cast<uint16_t>(b)) << 16);
    return true;
}

bool C_CheckpointNetwork::Join(const char *a, const char *b)   // 0x0051f490
{
    const int pointA = Find(a);
    const int pointB = Find(b);
    if (pointA == CHECKPOINT_NONE || pointB == CHECKPOINT_NONE)
        return false;
    return JoinPoints(pointA, pointB);
}

bool C_CheckpointNetwork::SeparatePoints(int a, int b)  // 0x0051f4d0
{
    S_Checkpoint *pointA = Get(a);
    S_Checkpoint *pointB = Get(b);
    if (!pointA || !pointB || !pointA->RemoveJoinedLink(static_cast<int16_t>(b)) ||
        !pointB->RemoveJoinedLink(static_cast<int16_t>(a)))
        return false;
    const uint16_t keyA = static_cast<uint16_t>(a);
    const uint16_t keyB = static_cast<uint16_t>(b);
    for (size_t i = m_joins.size(); i-- > 0;) {
        const uint16_t first = static_cast<uint16_t>(m_joins[i]);
        const uint16_t second = static_cast<uint16_t>(m_joins[i] >> 16);
        if ((first == keyA || second == keyA) && (first == keyB || second == keyB)) {
            m_joins[i] = m_joins.back();
            m_joins.pop_back();
            break;
        }
    }
    return true;
}

bool C_CheckpointNetwork::Separate(const char *a, const char *b)   // 0x0051f5a0
{
    const int pointA = Find(a);
    const int pointB = Find(b);
    if (pointA == CHECKPOINT_NONE || pointB == CHECKPOINT_NONE)
        return false;
    return SeparatePoints(pointA, pointB);
}

void C_CheckpointNetwork::ResetState()                  // 0x0051f230
{
    m_joins.clear();
    for (int i = 0; i < m_pointCount; ++i)
        m_points[i].ResetState();
}

bool C_CheckpointNetwork::SaveState(C_chunk &chunk)     // 0x00522a30
{
    if (m_joins.empty())
        return true;
    chunk.Begin(CHUNK_CHECKPOINT_JOINS);
    for (uint32_t join : m_joins)
        chunk.Write(join);
    chunk.Ascend();
    return true;
}

bool C_CheckpointNetwork::LoadState(C_chunk &chunk)     // 0x00522b60
{
    m_joins.clear();
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0;
        chunk.Descend(id);
        if (id == CHUNK_CHECKPOINT_JOINS) {
            // Fix: with under 4 bytes left the original skipped the read, joined the last
            // pair again and never got to the end of the chunk.
            while (chunk.RemainingSize() >= 4) {
                uint16_t pair[2] = {0xFFFF, 0xFFFF};
                chunk.Read(pair, 4);
                JoinPoints(pair[0], pair[1]);
            }
        }
        chunk.Ascend();
    }
    return true;
}

// --- spatial lookups ---------------------------------------------------------------------------

int C_CheckpointNetwork::FindFirst(int cell, float z, int lo, int hi) const   // 0x0051cba0
{
    if (m_pointCount == 0)
        return CHECKPOINT_NONE;
    if (lo == CHECKPOINT_NONE)
        lo = 0;
    if (hi == CHECKPOINT_NONE)
        hi = m_pointCount - 1;
    if (lo > hi)
        return CHECKPOINT_NONE;
    const S_Checkpoint &last = m_points[hi];
    if (last.cell < cell)
        return CHECKPOINT_NONE;
    if (last.cell == cell && !(last.pos.z > z))
        return CHECKPOINT_NONE;
    // (Returns 0, not `lo`, when the range starts at or after the place; the callers only
    // pass a `lo` before it.)
    const S_Checkpoint &first = m_points[lo];
    if (first.cell > cell)
        return 0;
    if (first.cell == cell && !(first.pos.z < z))
        return 0;
    for (;;) {
        const int mid = (lo + hi + 1) >> 1;
        if (mid == hi)
            return hi;
        const S_Checkpoint &point = m_points[mid];
        if (point.cell > cell)
            hi = mid;
        else if (point.cell < cell)
            lo = mid;
        else if (point.pos.z > z)
            hi = mid;
        else if (point.pos.z == z)
            return mid;
        else
            lo = mid;
    }
}

void C_CheckpointNetwork::ScanNearestPoint(int cell, float x, float y, float z, uint16_t &best, float &bestSq,
                                           float minSq) const   // 0x0051ccd0
{
    int lo = CHECKPOINT_NONE;
    int hi = CHECKPOINT_NONE;
    if (best != CHECKPOINT_NONE) {
        if (m_points[best].cell < cell)
            lo = best;
        else
            hi = best;
    }
    int start = FindFirst(cell, z, lo, hi);
    if (start == CHECKPOINT_NONE)
        start = m_pointCount;
    // Forward through the points at z and after it, then back from before it, while the z
    // distance alone can still beat the best.
    for (int i = start; i < m_pointCount; ++i) {
        const S_Checkpoint &point = m_points[i];
        if (point.cell != cell)
            break;
        const float dz = z - point.pos.z;
        float distSq = dz * dz;
        if (!(distSq < bestSq))
            break;
        const float dx = x - point.pos.x;
        const float dy = y - point.pos.y;
        distSq = dx * dx + dy * dy + distSq;
        if (minSq <= distSq && distSq < bestSq) {
            bestSq = distSq;
            best = static_cast<uint16_t>(i);
        }
    }
    for (int i = start - 1; i >= 0 && i < m_pointCount; --i) {
        const S_Checkpoint &point = m_points[i];
        if (point.cell != cell)
            return;
        const float dz = z - point.pos.z;
        float distSq = dz * dz;
        if (!(distSq < bestSq))
            return;
        const float dx = x - point.pos.x;
        const float dy = y - point.pos.y;
        distSq = dx * dx + dy * dy + distSq;
        if (minSq <= distSq && distSq < bestSq) {
            bestSq = distSq;
            best = static_cast<uint16_t>(i);
        }
    }
}

void C_CheckpointNetwork::ScanNearest(int cell, float x, float y, float z, uint16_t &best, float &bestSq,
                                      float minSq) const   // 0x0051d160
{
    int lo = CHECKPOINT_NONE;
    int hi = CHECKPOINT_NONE;
    if (best != CHECKPOINT_NONE) {
        if (m_points[best].cell < cell)
            lo = best;
        else
            hi = best;
    }
    int start = FindFirst(cell, z, lo, hi);
    if (start == CHECKPOINT_NONE)
        start = m_pointCount;
    // Distances less the point's radius (squared; 0 inside it). A point's radius makes it at
    // most MAX_RADIUS nearer: the scan stops where even that cannot beat the best (and, for
    // the whole distance, a further 10 m).
    auto consider = [&](int i, const S_Checkpoint &point, float dz) -> int {   // 1 stop, 2 found 0
        float dist = std::sqrt(dz * dz + (x - point.pos.x) * (x - point.pos.x) +
                               (y - point.pos.y) * (y - point.pos.y));
        const float beyond = dist - MAX_RADIUS - 10.0f;
        if (beyond > 0.0f && !(beyond * beyond < bestSq))
            return 1;
        dist -= point.radius * 0.0625f;
        dist = dist > 0.0f ? dist * dist : 0.0f;
        if (minSq <= dist && dist < bestSq) {
            bestSq = dist;
            best = static_cast<uint16_t>(i);
            if (bestSq <= 0.0f)
                return 2;
        }
        return 0;
    };
    for (int i = start; i < m_pointCount; ++i) {
        const S_Checkpoint &point = m_points[i];
        if (point.cell != cell)
            break;
        float gap = point.pos.z - z - MAX_RADIUS;
        if (gap >= 0.0f)
            gap *= gap;
        if (!(gap < bestSq))
            break;
        const int result = consider(i, point, z - point.pos.z);
        if (result == 1)
            break;
        if (result == 2)
            return;
    }
    for (int i = start - 1; i >= 0 && i < m_pointCount; --i) {
        const S_Checkpoint &point = m_points[i];
        if (point.cell != cell)
            return;
        const float dz = z - point.pos.z;
        float gap = dz - MAX_RADIUS;
        if (gap >= 0.0f)
            gap *= gap;
        if (!(gap < bestSq))
            return;
        if (consider(i, point, dz) != 0)
            return;
    }
}

void C_CheckpointNetwork::ScanNearestType15(int cell, float x, float y, float z, uint16_t &best, float &bestSq,
                                          float minSq) const   // 0x0051ce70
{
    (void)y;                                            // (in the xz plane)
    int lo = CHECKPOINT_NONE;
    int hi = CHECKPOINT_NONE;
    if (best != CHECKPOINT_NONE) {
        if (m_points[best].cell < cell)
            lo = best;
        else
            hi = best;
    }
    int start = FindFirst(cell, z, lo, hi);
    if (start == CHECKPOINT_NONE)
        start = m_pointCount;
    // As ScanNearest, for the points of type 15 only (the stops too only test those).
    auto consider = [&](int i, const S_Checkpoint &point, float dz) -> int {   // 1 stop, 2 found 0
        float dist = std::sqrt(dz * dz + (x - point.pos.x) * (x - point.pos.x));
        const float beyond = dist - MAX_RADIUS - 10.0f;
        if (beyond > 0.0f && !(beyond * beyond < bestSq))
            return 1;
        dist -= point.radius * 0.0625f;
        dist = dist > 0.0f ? dist * dist : 0.0f;
        if (minSq <= dist && dist < bestSq) {
            bestSq = dist;
            best = static_cast<uint16_t>(i);
            if (bestSq <= 0.0f)
                return 2;
        }
        return 0;
    };
    for (int i = start; i < m_pointCount; ++i) {
        const S_Checkpoint &point = m_points[i];
        if (point.cell != cell)
            break;
        if (point.type != CHECKPOINT_TYPE_15)
            continue;
        float gap = point.pos.z - z - MAX_RADIUS;
        if (gap >= 0.0f)
            gap *= gap;
        if (!(gap < bestSq))
            break;
        const int result = consider(i, point, z - point.pos.z);
        if (result == 1)
            break;
        if (result == 2)
            return;
    }
    for (int i = start - 1; i >= 0 && i < m_pointCount; --i) {
        const S_Checkpoint &point = m_points[i];
        if (point.cell != cell)
            return;
        if (point.type != CHECKPOINT_TYPE_15)
            continue;
        const float dz = z - point.pos.z;
        float gap = dz - MAX_RADIUS;
        if (gap >= 0.0f)
            gap *= gap;
        if (!(gap < bestSq))
            return;
        if (consider(i, point, dz) != 0)
            return;
    }
}

void C_CheckpointNetwork::ScanWithin(uint8_t *marks, int cell, float x, float y, float z, float radiusSq) const   // 0x0051d460
{
    int start = FindFirst(cell, z, CHECKPOINT_NONE, CHECKPOINT_NONE);
    if (start == CHECKPOINT_NONE)
        start = m_pointCount;
    auto mark = [&](int i, const S_Checkpoint &point) -> bool {
        const float dz = z - point.pos.z;
        const float dzSq = dz * dz;
        if (point.cell != cell || !(dzSq < radiusSq))
            return false;
        const float dx = x - point.pos.x;
        const float dy = y - point.pos.y;
        if (dx * dx + dy * dy + dzSq < radiusSq)
            marks[i] = 1;
        return true;
    };
    for (int i = start; i < m_pointCount; ++i) {
        if (!mark(i, m_points[i]))
            break;
    }
    for (int i = start - 1; i >= 0 && i < m_pointCount; --i) {
        if (!mark(i, m_points[i]))
            return;
    }
}

int C_CheckpointNetwork::FindNearestPoint(S_vector pos, float maxDist, float minDist) const   // 0x0051d5c0
{
    if (m_pointCount == 0 || pos.y > MAX_HEIGHT || pos.y < -MAX_HEIGHT)
        return CHECKPOINT_NONE;
    if (minDist > 0.0f)
        minDist *= minDist;
    const float minSq = minDist + 0.001f;
    const int cell = S_Checkpoint::GetCell(pos.x);
    float left = pos.x - cell * CELL_SIZE;              // to the cell's sides
    float right = CELL_SIZE - left;
    uint16_t best = CHECKPOINT_NONE;
    float bestSq = maxDist * maxDist;
    int leftCell = cell;
    int rightCell = cell;
    ScanNearestPoint(cell, pos.x, pos.y, pos.z, best, bestSq, minSq);
    if (!(bestSq > 0.0f))
        return best;
    // Then the cells on either side while they can hold a nearer point.
    bool expanded;
    do {
        for (;;) {
            expanded = left * left < bestSq;
            if (expanded) {
                left += CELL_SIZE;
                ScanNearestPoint(--leftCell, pos.x, pos.y, pos.z, best, bestSq, minSq);
            }
            if (bestSq <= right * right)
                break;
            right += CELL_SIZE;
            ScanNearestPoint(++rightCell, pos.x, pos.y, pos.z, best, bestSq, minSq);
        }
    } while (expanded);
    return best;
}

int C_CheckpointNetwork::FindNearest(S_vector pos, float maxDist, float minDist) const   // 0x0051d780
{
    if (m_pointCount == 0 || pos.y > MAX_HEIGHT || pos.y < -MAX_HEIGHT)
        return CHECKPOINT_NONE;
    if (minDist > 0.0f)
        minDist *= minDist;
    const float minSq = minDist + 0.001f;
    const int cell = S_Checkpoint::GetCell(pos.x);
    float left = pos.x - cell * CELL_SIZE - MAX_RADIUS;   // to the cell's sides, less the radius margin
    uint16_t best = CHECKPOINT_NONE;
    float right = CELL_SIZE - left - 2.0f * MAX_RADIUS;
    float bestSq = maxDist * maxDist;
    int leftCell = cell;
    int rightCell = cell;
    ScanNearest(cell, pos.x, pos.y, pos.z, best, bestSq, minSq);
    if (!(bestSq > 0.0f))
        return best;
    bool expanded;
    do {
        for (;;) {
            expanded = false;
            if (left < 0.0f || left * left < bestSq) {
                left += CELL_SIZE;
                expanded = true;
                ScanNearest(--leftCell, pos.x, pos.y, pos.z, best, bestSq, minSq);
            }
            if (right >= 0.0f && bestSq <= right * right)
                break;
            right += CELL_SIZE;
            ScanNearest(++rightCell, pos.x, pos.y, pos.z, best, bestSq, minSq);
        }
    } while (expanded);
    return best;
}

int C_CheckpointNetwork::FindNearestType15(S_vector pos, float maxDist, float minDist) const   // 0x0051d970
{
    if (m_pointCount == 0 || pos.y > MAX_HEIGHT || pos.y < -MAX_HEIGHT)
        return CHECKPOINT_NONE;
    if (minDist > 0.0f)
        minDist *= minDist;
    const float minSq = minDist + 0.001f;
    const int cell = S_Checkpoint::GetCell(pos.x);
    float left = pos.x - cell * CELL_SIZE - MAX_RADIUS;
    uint16_t best = CHECKPOINT_NONE;
    float right = CELL_SIZE - left - 2.0f * MAX_RADIUS;
    float bestSq = maxDist * maxDist;
    int leftCell = cell;
    int rightCell = cell;
    ScanNearestType15(cell, pos.x, pos.y, pos.z, best, bestSq, minSq);
    if (!(bestSq > 0.0f))
        return best;
    bool expanded;
    do {
        for (;;) {
            expanded = false;
            if (left < 0.0f || left * left < bestSq) {
                left += CELL_SIZE;
                expanded = true;
                ScanNearestType15(--leftCell, pos.x, pos.y, pos.z, best, bestSq, minSq);
            }
            if (right >= 0.0f && bestSq <= right * right)
                break;
            right += CELL_SIZE;
            ScanNearestType15(++rightCell, pos.x, pos.y, pos.z, best, bestSq, minSq);
        }
    } while (expanded);
    return best;
}

void C_CheckpointNetwork::MarkWithin(uint8_t *marks, S_vector pos, float radius) const   // 0x0051dbd0
{
    const int count = m_pointCount;
    if (count == 0 || pos.y > MAX_HEIGHT || pos.y < -MAX_HEIGHT)
        return;
    if (radius == 0.0f) {
        std::memset(marks, 1, count);
        return;
    }
    std::memset(marks, 0, count);
    const float radiusSq = radius * radius + 0.001f;
    const int cell = S_Checkpoint::GetCell(pos.x);
    float left = pos.x - cell * CELL_SIZE;
    float right = CELL_SIZE - left;
    int leftCell = cell;
    int rightCell = cell;
    int scanCell = cell;
    for (;;) {
        ScanWithin(marks, scanCell, pos.x, pos.y, pos.z, radiusSq);
        for (;;) {
            const bool expanded = left * left < radiusSq;
            if (expanded) {
                left += CELL_SIZE;
                ScanWithin(marks, --leftCell, pos.x, pos.y, pos.z, radiusSq);
            }
            if (right * right < radiusSq)
                break;
            if (!expanded)
                return;
        }
        right += CELL_SIZE;
        scanCell = ++rightCell;
    }
}

bool C_CheckpointNetwork::FindCoverWithin(const S_vector &pos, float radius, std::vector<uint16_t> *out) const   // 0x0051f830
{
    if (m_pointCount == 0 || !out)
        return false;
    // Fix: the original never freed its new[] of the marks.
    std::vector<uint8_t> marks(m_pointCount);
    MarkWithin(marks.data(), pos, radius);
    for (int i = 0; i < m_pointCount; ++i) {
        if (marks[i] && Get(i)->GetCoverMask(7))
            out->push_back(static_cast<uint16_t>(i));
    }
    return true;
}

float C_CheckpointNetwork::GetPathLength(const std::vector<uint16_t> &path) const   // 0x005230d0
{
    if (path.size() < 2)
        return 0.0f;
    int from = path[0];
    if (m_pointCount == 0 || from >= m_pointCount)
        return 0.0f;
    uint32_t total = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const int to = path[i];
        if (to >= m_pointCount)
            return 0.0f;
        const uint8_t link = m_points[from].FindLink(static_cast<int16_t>(to));
        if (link == 0xFF)
            return 0.0f;
        total += m_points[from].links[link].cost;
        from = to;
    }
    return total * (1.0f / 256.0f);
}
