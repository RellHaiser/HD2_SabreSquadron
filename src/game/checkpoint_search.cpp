// A checkpoint network's reachability queries, cover picking and path searches (see
// checkpoints.h).
#include "game/checkpoints.h"

#include <algorithm>
#include <cmath>

#include "game/actor.h"
#include "game/world.h"

namespace {

// The searches take their nodes from pools that hand them out of a free list growing `GROW`
// nodes at a time; nodes go back at the end of each search and the pool frees them at exit.
// (The original's pools prefilled their lists in static inits: 0x00520410 the flood's, vtable
// 0x008109bc; 0x00520f20 the path searches', vtable 0x008109c0.)
template <class T, int GROW>
class C_NodePool {
public:
    C_NodePool() = default;
    ~C_NodePool()                                       // (0x005204b0 / 0x00520fc0 at exit)
    {
        for (T *node : m_free)
            delete node;
    }
    C_NodePool(const C_NodePool &) = delete;
    C_NodePool &operator=(const C_NodePool &) = delete;

    T *Alloc()
    {
        if (m_free.empty()) {
            for (int i = 0; i < GROW; ++i)
                m_free.push_back(new T);
        }
        T *node = m_free.back();
        m_free.pop_back();
        return node;
    }
    void Free(T *node) { m_free.push_back(node); }

private:
    std::vector<T *> m_free;
};

// A point the flood reached (0xC bytes).
struct S_FloodNode {
    uint16_t point;
    float cost;                                         // metres from the start
    S_FloodNode *next;
};

// A point of a path search (0x14 bytes).
struct S_SearchNode {
    bool closed;                                        // expanded (or, escaping, inside a threat's minimum)
    uint16_t point;
    uint32_t f;                                         // g + h: the open list's order
    uint32_t g;                                         // cost from the start, 1/256 m
    int h;                                              // estimate to the goal, 1/256 m (-1: see FindEscapePath)
    S_SearchNode *next;                                 // the open or the closed list
};

C_NodePool<S_FloodNode, 100> s_floodNodes;             // 0x008aacb0
C_NodePool<S_SearchNode, 500> s_searchNodes;           // 0x0089c238

// The path searches' state: per point its node and the point it was reached from
// (CHECKPOINT_NONE: not reached; the start is its own), the open list sorted by f, the closed
// list. (The original also counted the inserts and moves in the open list, 0x008aacf0 /
// 0x008aacf4, for a debug comparison; nothing reads them.)
std::vector<S_SearchNode *> s_nodes;                    // 0x0089c250
std::vector<uint16_t> s_prev;                           // 0x008a5e90
S_SearchNode *s_open = nullptr;                         // 0x008aace8
S_SearchNode *s_closed = nullptr;                       // 0x008aacec

void PrepareSearch(int pointCount)
{
    // Fix: the original's arrays were statics of 10000 points; a bigger network overran them.
    if (static_cast<int>(s_prev.size()) < pointCount) {
        s_prev.resize(pointCount);
        s_nodes.resize(pointCount);
    }
    std::fill(s_prev.begin(), s_prev.begin() + pointCount, static_cast<uint16_t>(CHECKPOINT_NONE));
}

// Inserts `node` into the open list before the first node of an f as large or larger; never
// before the head, the node being expanded.
void InsertOpen(S_SearchNode *node)                     // 0x00521120 (__fastcall)
{
    S_SearchNode *place = s_open;
    for (S_SearchNode *it = s_open->next; it; it = it->next) {
        if (node->f <= it->f)
            break;
        place = it;
    }
    node->next = place->next;
    place->next = node;
}

// Moves `node`, whose f went down, to its new place in the open list.
void RepositionOpen(S_SearchNode *node)                 // 0x00521160 (__fastcall)
{
    S_SearchNode *place = s_open;
    for (S_SearchNode *it = s_open->next; it; it = it->next) {
        if (node->f <= it->f)
            break;
        place = it;
    }
    for (S_SearchNode *before = place, *it = place->next; it; before = it, it = it->next) {
        if (it == node) {
            before->next = node->next;
            break;
        }
    }
    node->next = place->next;
    place->next = node;
}

// Gives both lists' nodes back to the pool.
void ReleaseLists()
{
    while (S_SearchNode *node = s_open) {
        s_open = node->next;
        s_searchNodes.Free(node);
    }
    while (S_SearchNode *node = s_closed) {
        s_closed = node->next;
        s_searchNodes.Free(node);
    }
}

// Fills `path` (empty) with the points from the search's start to `to`.
void BuildPath(int to, std::vector<uint16_t> &path)
{
    int length = 1;
    for (uint16_t at = static_cast<uint16_t>(to), point = s_prev[at]; point != at; at = point, point = s_prev[at])
        ++length;
    path.resize(length);
    int i = length - 1;
    path[i] = static_cast<uint16_t>(to);
    for (uint16_t at = static_cast<uint16_t>(to), point = s_prev[at]; point != at; at = point, point = s_prev[at])
        path[--i] = point;
}

// The path searches' estimate: the Manhattan distance in 1/256 m.
int Estimate(const S_vector &goal, const S_vector &pos)
{
    const float dx = std::fabs(goal.x - pos.x);
    const float dy = std::fabs(goal.y - pos.y);
    const float dz = std::fabs(goal.z - pos.z);
    return static_cast<int>((dz + dy + dx) * 256.0f);
}

// Removes `list[i]`, moving the last element into its place.
void EraseUnordered(std::vector<uint16_t> &list, size_t i)   // 0x00520ed0 (__fastcall)
{
    if (i + 1 < list.size())
        list[i] = list.back();
    list.pop_back();
}

} // namespace

// --- reachable points ------------------------------------------------------------------------

void C_CheckpointNetwork::FloodFill(const S_Checkpoint *from, float radius, std::vector<uint16_t> &out)   // 0x00520530
{
    if (!from || radius <= 0.0f)
        return;
    // A list of the points reached, in the order found; a point keeps the cost of the first
    // way found to it (not the cheapest).
    S_FloodNode *head = s_floodNodes.Alloc();
    head->point = static_cast<uint16_t>(GetIndex(from));
    head->cost = 0.0f;
    head->next = nullptr;
    S_FloodNode *tail = head;
    int added = 0;
    for (S_FloodNode *current = head; current; current = current->next) {
        const S_Checkpoint *point = Get(current->point);
        if (!point || !point->links)
            continue;
        const int count = point->GetLinkCount();
        for (int i = 0; i < count; ++i) {
            const S_CheckpointLink &link = point->links[i];
            const float cost = link.cost * (1.0f / 256.0f) + current->cost;
            if (!(cost <= radius))
                continue;
            bool known = false;
            for (const S_FloodNode *it = head; it; it = it->next) {
                if (static_cast<int16_t>(it->point) == static_cast<int16_t>(link.target)) {
                    known = true;
                    break;
                }
            }
            if (known)
                continue;
            ++added;
            S_FloodNode *node = s_floodNodes.Alloc();
            node->point = link.target;
            node->cost = cost;
            node->next = nullptr;
            tail->next = node;
            tail = node;
        }
    }
    out.reserve(added);
    for (S_FloodNode *node = head; node;) {
        out.push_back(node->point);
        S_FloodNode *next = node->next;
        s_floodNodes.Free(node);
        node = next;
    }
}

bool C_CheckpointNetwork::GetReachable(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)   // 0x00520860
{
    if (!from || m_pointCount == 0 || !(radius >= 0.0f) || !out)
        return false;
    FloodFill(from, radius, *out);
    return true;
}

bool C_CheckpointNetwork::GetReachableCopy(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)   // 0x005208a0
{
    if (!from || m_pointCount == 0 || !(radius >= 0.0f) || !out)
        return false;
    FloodFill(from, radius, *out);
    return true;
}

bool C_CheckpointNetwork::GetNeighbours(int from, float radius, std::vector<uint16_t> *out)   // 0x00520830
{
    const S_Checkpoint *point = Get(from);
    if (!point)
        return false;
    return GetReachable(point, radius, out);
}

bool C_CheckpointNetwork::GetReachableCover(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)   // 0x00520910
{
    if (!from || m_pointCount == 0 || !(radius >= 0.0f) || !out)
        return false;
    std::vector<uint16_t> reached;
    FloodFill(from, radius, reached);
    for (uint16_t index : reached) {
        const S_Checkpoint *point = Get(index);
        if (point && point->GetCoverMask(7))
            out->push_back(index);
    }
    return true;
}

bool C_CheckpointNetwork::GetNeighbourCover(int from, float radius, std::vector<uint16_t> *out)   // 0x005208e0
{
    const S_Checkpoint *point = Get(from);
    if (!point)
        return false;
    return GetReachableCover(point, radius, out);
}

void C_CheckpointNetwork::CollectDepthFirst(const S_Checkpoint *point, float budget, std::vector<uint16_t> &list)   // 0x0051fcb0
{
    if (!point || !(budget >= 0.0f) || !point->links)
        return;
    const int count = point->GetLinkCount();
    for (int i = 0; i < count; ++i) {
        const S_CheckpointLink &link = point->links[i];
        const float cost = link.cost * (1.0f / 256.0f);
        if (!(cost <= budget))
            continue;
        if (std::find(list.begin(), list.end(), link.target) != list.end())
            continue;
        const S_Checkpoint *next = Get(link.target);
        if (next) {
            list.push_back(link.target);
            CollectDepthFirst(next, budget - cost, list);
        }
    }
}

bool C_CheckpointNetwork::GetReachableDepthFirst(const S_Checkpoint *from, float radius, std::vector<uint16_t> *out)   // 0x0051f5e0
{
    if (!from || m_pointCount == 0 || !(radius >= 0.0f) || !out)
        return false;
    std::vector<uint16_t> list;
    list.push_back(static_cast<uint16_t>(GetIndex(from)));
    CollectDepthFirst(from, radius, list);
    for (uint16_t index : list) {
        if (Get(index))
            out->push_back(index);
    }
    return true;
}

bool C_CheckpointNetwork::GetReachableType12(const S_vector &pos, float radius, std::vector<uint16_t> *out)   // 0x0051f700
{
    if (m_pointCount == 0 || !(radius >= 0.0f) || !out)
        return false;
    const S_Checkpoint *start = Get(FindNearest(pos, 5.0f, -1.0f));
    if (!start)
        return false;
    std::vector<uint16_t> reached;
    FloodFill(start, radius, reached);
    for (uint16_t index : reached) {
        const uint8_t type = m_points[index].type;
        if (type == 1 || type == 2)
            out->push_back(index);
    }
    return true;
}

int C_CheckpointNetwork::FindNearestFree(const S_Checkpoint *from)   // 0x0051e8a0
{
    if (m_pointCount == 0 || !from)
        return 0;
    float reach = 0.0f;
    uint16_t best = CHECKPOINT_NONE;
    float bestSq = 10000.0f;
    for (;;) {
        reach += 5.0f;
        std::vector<uint16_t> reached;
        FloodFill(from, reach, reached);
        for (uint16_t index : reached) {
            const S_Checkpoint &point = m_points[index];
            if (point.flags & CHECKPOINT_FLAG_UNDER_CAR)
                continue;
            const float dx = point.pos.x - from->pos.x;
            const float dy = point.pos.y - from->pos.y;
            const float dz = point.pos.z - from->pos.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < bestSq) {
                bestSq = distSq;
                best = index;
            }
        }
        if (best != CHECKPOINT_NONE)
            return best;
        if (reach >= 20.0f)
            return CHECKPOINT_NONE;
    }
}

int C_CheckpointNetwork::PickCoverPoint(const S_vector &self, const S_vector &enemy, std::vector<uint16_t> *candidates,
                                        uint32_t flags)   // 0x00520a30
{
    uint16_t best = CHECKPOINT_NONE;
    float nearest = 10000.0f;
    float farthest = 0.0f;
    const float selfDist = (self - enemy).Magnitude();
    if (!candidates || candidates->empty())
        return CHECKPOINT_NONE;
    std::vector<uint16_t> &list = *candidates;
    for (size_t i = list.size(); i-- > 0;) {
        const S_Checkpoint *point = Get(list[i]);
        if (!point || !point->GetCoverMask(7)) {
            EraseUnordered(list, i);
            continue;
        }
        // The stances the point covers toward the enemy.
        const uint8_t covered = point->GetCoverStances(S_Checkpoint::GetDirectionBit(enemy - point->pos), 7);
        const uint32_t stance = flags & COVER_PICK_STANCE;
        const uint32_t exposed = flags & COVER_PICK_EXPOSED;
        if ((stance == 0x100 && !(covered & 1)) || (stance == 0x200 && !(covered & 2)) ||
            (stance == 0x400 && !(covered & 4)) || (exposed == 0x2000 && (covered & 2)) ||
            (exposed == 0x4000 && (covered & 4))) {
            if (flags & COVER_PICK_DROP_REJECTED)
                EraseUnordered(list, i);
            continue;
        }
        const float dist = (((flags & COVER_PICK_FROM_SELF) ? self : enemy) - point->pos).Magnitude();
        if ((flags & COVER_PICK_NOT_FARTHER) && dist > selfDist) {
            EraseUnordered(list, i);
            continue;
        }
        if (flags & COVER_PICK_FARTHEST) {
            if (dist > farthest) {
                farthest = dist;
                best = list[i];
            }
        } else if (dist < nearest) {
            nearest = dist;
            best = list[i];
        }
    }
    return best;
}

// --- path searches ---------------------------------------------------------------------------

bool C_CheckpointNetwork::IsMeleeTrackerPassable(const S_Checkpoint &from, const S_Checkpoint &to) const   // 0x005223d0
{
    if (from.type == CHECKPOINT_TYPE_NORMAL)
        return true;
    if ((from.type == 3 && to.type == 3) || (from.type == 4 && to.type == 4) || (from.type == 6 && to.type == 5))
        return false;
    if (from.type == CHECKPOINT_TYPE_DOOR && to.type == CHECKPOINT_TYPE_DOOR) {
        int index = GetIndex(&from);
        if (index >= m_pointCount || index < 0)
            index = CHECKPOINT_NONE;
        const C_Actor *actor = GetActor(index);
        if (actor && actor->GetType() == ACTOR_DOOR) {
            const int state = static_cast<const C_Door *>(actor)->GetMotion();
            if (state == 0 || state == 3)               // closed, or 3
                return false;
        }
    }
    return true;
}

// The A* search of FindPath (0x00521b60) and FindMeleeTrackerPath (0x00522470). It ends as soon as a
// link reaches `to`, not when `to` comes off the open list: the path is the first found, not
// always the shortest.
bool C_CheckpointNetwork::RunPathSearch(int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags, bool meleeTracker)
{
    from = static_cast<uint16_t>(from);
    to = static_cast<uint16_t>(to);
    const int count = m_pointCount;
    if (count == 0 || from >= count || to >= count || !path)
        return false;
    path->clear();
    if (from == to) {
        path->push_back(static_cast<uint16_t>(from));
        return true;
    }
    PrepareSearch(count);
    const S_vector &goal = m_points[to].pos;
    S_SearchNode *start = s_searchNodes.Alloc();
    s_open = start;
    const int h = Estimate(goal, m_points[from].pos);
    start->closed = false;
    start->point = static_cast<uint16_t>(from);
    start->g = 0;
    start->h = h;
    start->f = h;
    start->next = nullptr;
    s_nodes[from] = start;
    s_prev[from] = static_cast<uint16_t>(from);

    while (s_open) {
        S_SearchNode *current = s_open;
        current->closed = true;
        const S_Checkpoint &point = m_points[current->point];
        for (int i = point.GetLinkCount(); i-- > 0;) {
            const S_CheckpointLink &link = point.links[i];
            const int target = link.target;
            // (The cost goes through a float, as in the original.)
            const float g = static_cast<float>(link.cost + current->g);
            const S_Checkpoint &next = m_points[target];
            if (meleeTracker ? !IsMeleeTrackerPassable(point, next) : (next.flags & excludeFlags & CHECKPOINT_SEARCH_FLAGS) != 0)
                continue;
            if (s_prev[target] == CHECKPOINT_NONE) {
                s_prev[target] = current->point;
                S_SearchNode *node = s_searchNodes.Alloc();
                s_nodes[target] = node;
                node->h = Estimate(goal, next.pos);
                node->g = static_cast<int>(g);
                node->closed = false;
                node->point = static_cast<uint16_t>(target);
                node->f = node->g + node->h;
                node->next = nullptr;
                InsertOpen(node);
            } else {
                S_SearchNode *node = s_nodes[target];
                if (static_cast<float>(node->g) > g && !node->closed) {
                    s_prev[target] = current->point;
                    node->g = static_cast<int>(g);
                    node->f = node->h + node->g;
                    RepositionOpen(node);
                }
            }
            if (target == to)
                break;
        }
        if (s_prev[to] != CHECKPOINT_NONE)
            break;
        s_open = current->next;
        current->next = s_closed;
        s_closed = current;
    }
    ReleaseLists();
    if (s_prev[to] == CHECKPOINT_NONE)
        return false;
    BuildPath(to, *path);
    return true;
}

bool C_CheckpointNetwork::FindPath(int from, int to, std::vector<uint16_t> *path, uint32_t excludeFlags)   // 0x005223b0
{
    return RunPathSearch(from, to, path, excludeFlags, false);
}

bool C_CheckpointNetwork::FindMeleeTrackerPath(int from, int to, std::vector<uint16_t> *path)   // 0x00522470
{
    return RunPathSearch(from, to, path, 0, true);
}

bool C_CheckpointNetwork::FindEscapePath(int from, const S_Vector4Array &threats, float minDist, float safeDist,
                                         std::vector<uint16_t> &path, uint32_t excludeFlags)   // 0x00521460 (0x00521480)
{
    from = static_cast<uint16_t>(from);
    const int count = m_pointCount;
    if (count == 0 || from >= count || threats.count == 0)
        return false;
    // The estimate: (safeDist / distance - 1) of the nearest threat in 1/256, 0 once every
    // threat is safeDist away (the goal), -1 within minDist of one (the point is shut out).
    auto estimate = [&](const S_vector &pos) {
        float worst = 0.0f;
        for (int i = threats.count; i-- > 0;) {
            const S_vector4 &threat = threats.data[i];
            const float dist = (pos - S_vector(threat.x, threat.y, threat.z)).Magnitude();
            if (!(dist > minDist))
                return -1;
            const float v = safeDist / dist - 1.0f;
            if (v > worst)
                worst = v;
        }
        return static_cast<int>(worst * 256.0f);
    };
    uint16_t found = CHECKPOINT_NONE;
    path.clear();
    PrepareSearch(count);
    const int h = estimate(m_points[from].pos);
    if (h == -1)
        return false;
    S_SearchNode *start = s_searchNodes.Alloc();
    s_open = start;
    start->closed = false;
    start->point = static_cast<uint16_t>(from);
    start->g = 0;
    start->h = h;
    start->f = h;
    start->next = nullptr;
    s_nodes[from] = start;
    s_prev[from] = static_cast<uint16_t>(from);

    while (s_open) {
        S_SearchNode *current = s_open;
        const S_Checkpoint &point = m_points[current->point];
        for (int i = point.GetLinkCount(); i-- > 0;) {
            const S_CheckpointLink &link = point.links[i];
            const int target = link.target;
            const float g = static_cast<float>(link.cost + current->g);
            const S_Checkpoint &next = m_points[target];
            if (next.flags & excludeFlags & CHECKPOINT_SEARCH_FLAGS)
                continue;
            if (s_prev[target] == CHECKPOINT_NONE) {
                const int hTarget = estimate(next.pos);
                s_prev[target] = current->point;
                S_SearchNode *node = s_searchNodes.Alloc();
                s_nodes[target] = node;
                node->g = static_cast<int>(g);
                node->point = static_cast<uint16_t>(target);
                node->closed = false;
                node->h = hTarget;
                node->f = node->g + hTarget;
                node->next = nullptr;
                if (hTarget == -1) {
                    node->closed = true;
                    node->next = s_closed;
                    s_closed = node;
                    continue;
                }
                InsertOpen(node);
                if (hTarget == 0) {
                    found = static_cast<uint16_t>(target);
                    break;
                }
            } else {
                S_SearchNode *node = s_nodes[target];
                if (node->h == -1)
                    continue;
                if (static_cast<float>(node->g) > g && !node->closed) {
                    s_prev[target] = current->point;
                    node->g = static_cast<int>(g);
                    node->f = node->h + node->g;
                    RepositionOpen(node);
                }
            }
        }
        if (found != CHECKPOINT_NONE)
            break;
        s_open = current->next;
        current->closed = true;
        current->next = s_closed;
        s_closed = current;
    }
    ReleaseLists();
    if (found == CHECKPOINT_NONE || s_prev[found] == CHECKPOINT_NONE)
        return false;
    BuildPath(found, path);
    return true;
}
