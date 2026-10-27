// Tests the ported checkpoint networks on the shipped missions:
//   harness DIR [DIR...]   every <DIR>\<mission>\check2.bin: loads both networks, checks the
//                          data's invariants, then the lookups, reachability, path searches,
//                          joins (saved to joins.bin in the working directory) and cover picks
//                          against brute-force references. One line per mission, totals last.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <vector>

#define private public
#include "common/chunk.h"
#include "game/checkpoints.h"
#undef private
#include "game/game.h"
#include "game/world.h"
#include "ls3d/rw_data.h"

// --- stubs ----------------------------------------------------------------------------------------
static std::map<std::string, I3D_frame *> s_frames;
static std::vector<C_Door *> s_doors;
I3D_frame *I3D_scene::FindFrame(const char *name, uint32_t)
{
    I3D_frame *&frame = s_frames[name];
    if (!frame) {
        frame = new I3D_frame;
        C_Door *door = new C_Door;
        door->m_type = ACTOR_DOOR;
        s_doors.push_back(door);
        frame->actor = door;
    }
    return frame;
}
I3D_frame *I3D_driver::FindFrame(I3D_scene *, const char *) { return nullptr; }
static I3D_driver s_driver;
I3D_driver *I3DGetDriver() { return &s_driver; }
static I3D_scene s_scene;
I3D_scene *C_game::GetScene() { return &s_scene; }
static C_game s_game;
C_game *GetGame() { return &s_game; }

static std::vector<FILE *> s_files;
static int AddFile(FILE *f)
{
    if (!f)
        return DTA_INVALID_HANDLE;
    s_files.push_back(f);
    return static_cast<int>(s_files.size()) - 1;
}
int __stdcall dtaOpen(const char *fileName, bool) { return AddFile(std::fopen(fileName, "rb")); }
int __stdcall dtaOpenWrite(const char *fileName, int) { return AddFile(std::fopen(fileName, "w+b")); }
int __stdcall dtaRead(int h, void *buffer, int size) { return static_cast<int>(std::fread(buffer, 1, size, s_files[h])); }
int __stdcall dtaWrite(int h, const void *buffer, int size) { return static_cast<int>(std::fwrite(buffer, 1, size, s_files[h])); }
int __stdcall dtaSeek(int h, int position, int origin)
{
    if (std::fseek(s_files[h], position, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END) != 0)
        return -1;
    return static_cast<int>(std::ftell(s_files[h]));
}
bool __stdcall dtaClose(int h)
{
    std::fclose(s_files[h]);
    s_files[h] = nullptr;
    return true;
}

// --- helpers --------------------------------------------------------------------------------------
struct S_Totals {
    std::map<std::string, long> counts;
    void Add(const std::string &key, long n = 1) { counts[key] += n; }
};
static S_Totals g_totals;
static int g_failures = 0;

static void Fail(const char *mission, const char *what, long a = 0, long b = 0)
{
    ++g_failures;
    if (g_failures <= 60)
        std::printf("  FAIL %s: %s (%ld, %ld)\n", mission, what, a, b);
}

static std::vector<unsigned char> ReadFile(const std::string &name)
{
    std::vector<unsigned char> data;
    FILE *f = std::fopen(name.c_str(), "rb");
    if (!f)
        return data;
    std::fseek(f, 0, SEEK_END);
    data.resize(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    if (!data.empty() && std::fread(data.data(), 1, data.size(), f) != data.size())
        data.clear();
    std::fclose(f);
    return data;
}

static float Dist(const S_vector &a, const S_vector &b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Dijkstra over the links (1/256 m); UINT32_MAX where unreachable.
static std::vector<uint32_t> Dijkstra(const C_CheckpointNetwork &n, int from)
{
    std::vector<uint32_t> cost(n.m_pointCount, UINT32_MAX);
    using Item = std::pair<uint32_t, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    cost[from] = 0;
    queue.push({0, from});
    while (!queue.empty()) {
        const auto [c, i] = queue.top();
        queue.pop();
        if (c != cost[i])
            continue;
        const S_Checkpoint &p = n.m_points[i];
        for (int k = 0; k < p.GetLinkCount(); ++k) {
            const S_CheckpointLink &l = p.links[k];
            if (l.target >= n.m_pointCount)
                continue;
            const uint32_t nc = c + l.cost;
            if (nc < cost[l.target]) {
                cost[l.target] = nc;
                queue.push({nc, l.target});
            }
        }
    }
    return cost;
}

static bool ValidPath(const C_CheckpointNetwork &n, const std::vector<uint16_t> &path, int from, int to,
                      uint32_t *length)
{
    if (path.empty() || path.front() != from || (to >= 0 && path.back() != to))
        return false;
    uint32_t total = 0;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const S_Checkpoint &p = n.m_points[path[i]];
        const uint8_t k = p.FindLink(static_cast<int16_t>(path[i + 1]));
        if (k == 0xFF)
            return false;
        total += p.links[k].cost;
    }
    if (length)
        *length = total;
    return true;
}

// --- per network checks ---------------------------------------------------------------------------
struct S_NetInfo {
    int linkSlots = 0;
};

static void CheckData(const char *mission, const char *which, C_CheckpointNetwork &n, const S_NetInfo &info)
{
    // The link storage holds every point's links and spare slot.
    long used = 0;
    for (int i = 0; i < n.m_pointCount; ++i)
        used += n.m_points[i].GetLinkCount() + ((n.m_points[i].flags & CHECKPOINT_SPARE_LINK) ? 1 : 0);
    if (used != info.linkSlots)
        Fail(mission, "link slots", used, info.linkSlots);
    g_totals.Add(std::string(which) + " points", n.m_pointCount);
    g_totals.Add(std::string(which) + " links", used);
    if (n.m_pointCount > 10000)
        g_totals.Add("networks over 10000 points");
    if (n.m_pointCount > g_totals.counts["max points"])
        g_totals.counts["max points"] = n.m_pointCount;

    long badCell = 0, unsorted = 0, badTarget = 0, costOff = 0, costTotal = 0;
    for (int i = 0; i < n.m_pointCount; ++i) {
        const S_Checkpoint &p = n.m_points[i];
        if (p.cell != S_Checkpoint::GetCell(p.pos.x)) {
            ++badCell;
            if (badCell <= 3)
                std::printf("  %s %s: point %d x=%.9g cell %d, GetCell %d\n", mission, which, i, p.pos.x, p.cell,
                            S_Checkpoint::GetCell(p.pos.x));
        }
        if (i > 0) {
            const S_Checkpoint &q = n.m_points[i - 1];
            if (p.cell < q.cell || (p.cell == q.cell && p.pos.z < q.pos.z))
                ++unsorted;
        }
        for (int bit = 0x40; bit <= 0x8000; bit <<= 1) {
            if (p.flags & bit) {
                char key[32];
                std::snprintf(key, sizeof(key), "flag %04X", bit);
                g_totals.Add(key);
            }
        }
        for (int k = 0; k < p.GetLinkCount(); ++k) {
            const S_CheckpointLink &l = p.links[k];
            if (l.target >= n.m_pointCount) {
                ++badTarget;
                continue;
            }
            // Stored costs against the formula of GetLinkCost.
            const float computed = p.GetLinkCost(n.m_points[l.target]) * 256.0f;
            ++costTotal;
            if (std::fabs(computed - l.cost) > 1.5f)
                ++costOff;
        }
    }
    if (badCell)
        Fail(mission, "stored cell != GetCell", badCell, n.m_pointCount);
    if (unsorted)
        Fail(mission, "points not sorted by (cell, z)", unsorted, 0);
    if (badTarget)
        Fail(mission, "link targets out of range", badTarget, 0);
    g_totals.Add("link costs checked", costTotal);
    g_totals.Add("link costs off the formula", costOff);

    // Names: Find gives the first entry of a name; FindByPrefix includes it.
    for (int i = 0; i < n.m_nameCount; ++i) {
        char name[17] = {};
        std::memcpy(name, n.m_names[i].name, 16);
        int first = -1;
        for (int j = 0; j < n.m_nameCount && first < 0; ++j) {
            if (_strnicmp(n.m_names[j].name, name, 16) == 0)
                first = j;
        }
        if (n.Find(name) != n.m_names[first].index)
            Fail(mission, "Find(name)", n.Find(name), n.m_names[first].index);
        std::vector<uint16_t> found;
        n.FindByPrefix(name, found);
        if (std::find(found.begin(), found.end(), n.m_names[i].index) == found.end())
            Fail(mission, "FindByPrefix(name)", i, 0);
        if (n.m_names[i].index >= n.m_pointCount)
            Fail(mission, "name index out of range", n.m_names[i].index, n.m_pointCount);
    }
    g_totals.Add(std::string(which) + " names", n.m_nameCount);
    g_totals.Add(std::string(which) + " frames", n.m_frameCount);

    // Actors: sorted by point; GetActor's binary search finds each; FindActorPoint goes back.
    for (int i = 1; i < n.m_actorCount; ++i) {
        if (n.m_actors[i].index <= n.m_actors[i - 1].index)
            Fail(mission, "actor table not sorted / unique", n.m_actors[i - 1].index, n.m_actors[i].index);
    }
    for (int i = 0; i < n.m_actorCount; ++i) {
        const S_CheckpointActor &e = n.m_actors[i];
        if (!e.actor)
            Fail(mission, "actor not found", i, 0);
        if (n.GetActor(e.index) != e.actor)
            Fail(mission, "GetActor", i, e.index);
        int last = -1;
        for (int j = 0; j < n.m_actorCount; ++j) {
            if (n.m_actors[j].actor == e.actor)
                last = j;
        }
        if (n.FindActorPoint(e.actor) != n.m_actors[last].index)
            Fail(mission, "FindActorPoint", i, 0);
        if (n.m_points[e.index].type != CHECKPOINT_TYPE_DOOR)
            g_totals.Add("actor points not of type 1");
    }
    for (int i = 0; i < n.m_pointCount; i += 7) {
        bool inTable = false;
        for (int j = 0; j < n.m_actorCount; ++j)
            inTable |= n.m_actors[j].index == i;
        if (!inTable && n.GetActor(i))
            Fail(mission, "GetActor of a point without actor", i, 0);
    }
    g_totals.Add(std::string(which) + " actors", n.m_actorCount);
    for (int i = 0; i < n.m_pointCount; ++i)
        g_totals.Add(std::string(which) + " type " + std::to_string(n.m_points[i].type));
    // Which frames the actor points name, by point type (a few examples each).
    static std::map<int, int> shown;
    for (int i = 0; i < n.m_actorCount; ++i) {
        const int type = n.m_points[n.m_actors[i].index].type;
        if (shown[type]++ < 4) {
            for (const auto &[name, frame] : s_frames) {
                if (frame->actor == n.m_actors[i].actor)
                    std::printf("  actor point type %d: %s\n", type, name.c_str());
            }
        }
    }
}

static void CheckLookups(const char *mission, C_CheckpointNetwork &n, std::mt19937 &rng)
{
    if (n.m_pointCount == 0)
        return;
    std::uniform_int_distribution<int> anyPoint(0, n.m_pointCount - 1);
    std::uniform_real_distribution<float> offset(-20.0f, 20.0f), lift(-2.0f, 2.0f);
    for (int t = 0; t < 150; ++t) {
        const S_vector &base = n.m_points[anyPoint(rng)].pos;
        const S_vector pos(base.x + offset(rng), base.y + lift(rng), base.z + offset(rng));
        const float maxDist = 30.0f;
        // FindNearestPoint: plain distances, exact.
        {
            int brute = CHECKPOINT_NONE;
            float best = maxDist * maxDist;
            for (int i = 0; i < n.m_pointCount; ++i) {
                const float d = Dist(pos, n.m_points[i].pos);
                if (d * d >= -0.999f && d * d < best) {
                    best = d * d;
                    brute = i;
                }
            }
            const int got = n.FindNearestPoint(pos, maxDist, -1.0f);
            g_totals.Add("FindNearestPoint");
            if (got != brute) {
                const bool tie = got != CHECKPOINT_NONE && brute != CHECKPOINT_NONE &&
                                 std::fabs(Dist(pos, n.m_points[got].pos) - Dist(pos, n.m_points[brute].pos)) < 1e-4f;
                if (tie)
                    g_totals.Add("FindNearestPoint ties");
                else
                    Fail(mission, "FindNearestPoint", got, brute);
            }
        }
        // FindNearest: distances less the radius; the scan's stops can miss (reported).
        {
            int brute = CHECKPOINT_NONE;
            float best = maxDist * maxDist;
            for (int i = 0; i < n.m_pointCount; ++i) {
                float d = Dist(pos, n.m_points[i].pos) - n.m_points[i].radius * 0.0625f;
                d = d > 0.0f ? d * d : 0.0f;
                if (d < best) {
                    best = d;
                    brute = i;
                }
            }
            const int got = n.FindNearest(pos, maxDist, -1.0f);
            g_totals.Add("FindNearest");
            if (got != brute) {
                auto adj = [&](int i) {
                    float d = Dist(pos, n.m_points[i].pos) - n.m_points[i].radius * 0.0625f;
                    return d > 0.0f ? d * d : 0.0f;
                };
                if (got != CHECKPOINT_NONE && brute != CHECKPOINT_NONE && std::fabs(adj(got) - adj(brute)) < 1e-3f)
                    g_totals.Add("FindNearest ties");
                else if (got != CHECKPOINT_NONE && brute != CHECKPOINT_NONE && adj(got) > adj(brute))
                    g_totals.Add("FindNearest misses (scan stops)");
                else
                    Fail(mission, "FindNearest", got, brute);
            }
        }
        // FindNearestType15: points of type 15 in the xz plane.
        {
            int brute = CHECKPOINT_NONE;
            float best = maxDist * maxDist;
            auto adj = [&](int i) {
                const S_Checkpoint &p = n.m_points[i];
                float d = std::sqrt((pos.x - p.pos.x) * (pos.x - p.pos.x) + (pos.z - p.pos.z) * (pos.z - p.pos.z)) -
                          p.radius * 0.0625f;
                return d > 0.0f ? d * d : 0.0f;
            };
            for (int i = 0; i < n.m_pointCount; ++i) {
                if (n.m_points[i].type == CHECKPOINT_TYPE_15 && adj(i) < best) {
                    best = adj(i);
                    brute = i;
                }
            }
            const int got = n.FindNearestType15(pos, maxDist, -1.0f);
            g_totals.Add("FindNearestType15");
            if (got != brute) {
                if (got != CHECKPOINT_NONE && brute != CHECKPOINT_NONE && std::fabs(adj(got) - adj(brute)) < 1e-3f)
                    g_totals.Add("FindNearestType15 ties");
                else if (got != CHECKPOINT_NONE && brute != CHECKPOINT_NONE && adj(got) > adj(brute))
                    g_totals.Add("FindNearestType15 misses (scan stops)");
                else
                    Fail(mission, "FindNearestType15", got, brute);
            }
        }
        // MarkWithin: exact.
        {
            const float radius = 15.0f;
            std::vector<uint8_t> marks(n.m_pointCount, 7);
            n.MarkWithin(marks.data(), pos, radius);
            long wrong = 0;
            for (int i = 0; i < n.m_pointCount; ++i) {
                const float d = Dist(pos, n.m_points[i].pos);
                const bool in = d * d < radius * radius + 0.001f;
                if (marks[i] != (in ? 1 : 0))
                    ++wrong;
            }
            g_totals.Add("MarkWithin");
            if (wrong)
                Fail(mission, "MarkWithin", wrong, 0);
            // FindCoverWithin: the cover points among them.
            std::vector<uint16_t> cover;
            n.FindCoverWithin(pos, radius, &cover);
            long expected = 0;
            for (int i = 0; i < n.m_pointCount; ++i)
                expected += marks[i] && n.m_points[i].GetCoverMask(7) != 0;
            if (static_cast<long>(cover.size()) != expected)
                Fail(mission, "FindCoverWithin", static_cast<long>(cover.size()), expected);
            // PickCoverPoint: the pick is one of the candidates left.
            const S_vector enemy(pos.x + 25.0f, pos.y, pos.z);
            for (uint32_t flags : {0x40004104u, 0xC0004102u, 0x00000001u, 0x00000004u}) {
                std::vector<uint16_t> list = cover;
                const int pick = n.PickCoverPoint(pos, enemy, &list, flags);
                if (pick != CHECKPOINT_NONE && std::find(list.begin(), list.end(), pick) == list.end())
                    Fail(mission, "PickCoverPoint pick not a candidate", pick, flags);
                g_totals.Add("PickCoverPoint");
                g_totals.Add(pick != CHECKPOINT_NONE ? "PickCoverPoint picks" : "PickCoverPoint none");
            }
        }
    }
}

static void CheckPaths(const char *mission, C_CheckpointNetwork &n, std::mt19937 &rng)
{
    if (n.m_pointCount < 2)
        return;
    std::uniform_int_distribution<int> anyPoint(0, n.m_pointCount - 1);
    for (int t = 0; t < 60; ++t) {
        const int from = anyPoint(rng);
        const int to = anyPoint(rng);
        const std::vector<uint32_t> cost = Dijkstra(n, from);
        std::vector<uint16_t> path = {1, 2, 3};
        const bool found = n.FindPath(from, to, &path, 0);
        g_totals.Add("FindPath");
        if (found != (cost[to] != UINT32_MAX))
            Fail(mission, "FindPath success != reachable", from, to);
        if (found) {
            uint32_t length = 0;
            if (!ValidPath(n, path, from, to, &length))
                Fail(mission, "FindPath invalid path", from, to);
            if (std::fabs(n.GetPathLength(path) - length / 256.0f) > 0.01f)
                Fail(mission, "GetPathLength", from, to);
            g_totals.Add(length == cost[to] ? "FindPath optimal" : "FindPath longer than optimal");
            if (cost[to] > 0 && length > cost[to] * 1.5)
                g_totals.Add("FindPath over 1.5x optimal");
        } else if (!path.empty()) {
            Fail(mission, "FindPath left a path", from, to);
        }
        // Excluding the no-stop points can only lose paths.
        std::vector<uint16_t> path2;
        const bool found2 = n.FindPath(from, to, &path2, CHECKPOINT_FLAG_NO_STOP);
        if (found2 && !found)
            Fail(mission, "FindPath with exclusions found more", from, to);
        if (found2) {
            for (size_t i = 1; i < path2.size(); ++i) {
                if (n.m_points[path2[i]].flags & CHECKPOINT_FLAG_NO_STOP)
                    Fail(mission, "FindPath through an excluded point", from, to);
            }
        }

        // The meleeTracker's search: closed doors (state 0), then open ones (2).
        for (C_Door *door : s_doors)
            door->m_state = 0;
        std::vector<uint16_t> trackerPath;
        const bool trackerClosed = n.FindMeleeTrackerPath(from, to, &trackerPath);
        if (trackerClosed) {
            if (!ValidPath(n, trackerPath, from, to, nullptr))
                Fail(mission, "FindMeleeTrackerPath invalid path", from, to);
            for (size_t i = 0; i + 1 < trackerPath.size(); ++i) {
                if (!n.IsMeleeTrackerPassable(n.m_points[trackerPath[i]], n.m_points[trackerPath[i + 1]]))
                    Fail(mission, "FindMeleeTrackerPath impassable step", trackerPath[i], trackerPath[i + 1]);
            }
        }
        for (C_Door *door : s_doors)
            door->m_state = 2;
        const bool trackerOpen = n.FindMeleeTrackerPath(from, to, &trackerPath);
        if (trackerClosed && !trackerOpen)
            Fail(mission, "FindMeleeTrackerPath lost a path with open doors", from, to);
        g_totals.Add("FindMeleeTrackerPath");
        if (trackerOpen && !trackerClosed)
            g_totals.Add("FindMeleeTrackerPath only with open doors");
        if (found && !trackerOpen)
            g_totals.Add("FindMeleeTrackerPath none where FindPath found one");

        // Reachability within 30 m: no point beyond it, the start first.
        const S_Checkpoint *start = n.Get(from);
        const float radius = 30.0f;
        std::vector<uint16_t> reach;
        n.GetReachable(start, radius, &reach);
        if (reach.empty() || reach[0] != from)
            Fail(mission, "GetReachable start", from, 0);
        long within = 0;
        for (int i = 0; i < n.m_pointCount; ++i)
            within += cost[i] != UINT32_MAX && cost[i] <= radius * 256.0f;
        for (uint16_t i : reach) {
            if (cost[i] == UINT32_MAX || cost[i] > radius * 256.0f + 1.0f)
                Fail(mission, "GetReachable beyond the radius", from, i);
        }
        std::vector<uint16_t> sorted = reach;
        std::sort(sorted.begin(), sorted.end());
        if (std::unique(sorted.begin(), sorted.end()) != sorted.end())
            Fail(mission, "GetReachable duplicates", from, 0);
        g_totals.Add("GetReachable points", static_cast<long>(reach.size()));
        g_totals.Add("GetReachable Dijkstra points", within);
        std::vector<uint16_t> deep;
        n.GetReachableDepthFirst(start, radius, &deep);
        for (uint16_t i : deep) {
            if (cost[i] == UINT32_MAX || cost[i] > radius * 256.0f + 1.0f)
                Fail(mission, "GetReachableDepthFirst beyond the radius", from, i);
        }
        g_totals.Add("GetReachableDepthFirst points", static_cast<long>(deep.size()));
        const int free = n.FindNearestFree(start);
        if (free == CHECKPOINT_NONE || free >= n.m_pointCount)
            Fail(mission, "FindNearestFree", from, free);

        // Escaping three threats 4..8 m away: to a point 20 m from each, never within 2 m.
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f), dist(4.0f, 8.0f);
        S_vector4 threats[3];
        for (S_vector4 &th : threats) {
            const float a = angle(rng), d = dist(rng);
            th = {start->pos.x + d * std::cos(a), start->pos.y, start->pos.z + d * std::sin(a), 0.0f};
        }
        S_Vector4Array array;
        array.data = threats;
        array.capacity = array.count = 3;
        std::vector<uint16_t> escape;
        const bool escaped = n.FindEscapePath(from, array, 2.0f, 20.0f, escape, 0);
        g_totals.Add("FindEscapePath");
        if (escaped) {
            g_totals.Add("FindEscapePath found");
            if (!ValidPath(n, escape, from, -1, nullptr))
                Fail(mission, "FindEscapePath invalid path", from, 0);
            for (size_t i = 0; i < escape.size(); ++i) {
                for (const S_vector4 &th : threats) {
                    const float d = Dist(n.m_points[escape[i]].pos, S_vector(th.x, th.y, th.z));
                    if (d <= 2.0f)
                        Fail(mission, "FindEscapePath through a threat", from, escape[i]);
                    if (i + 1 == escape.size() && d < 20.0f / (1.0f + 1.0f / 256.0f) - 0.01f)
                        Fail(mission, "FindEscapePath end not safe", from, escape[i]);
                }
            }
        }
    }
}

static void CheckJoins(const char *mission, C_CheckpointNetwork &n)
{
    // Two named points with spare slots that Join can link.
    for (int i = 0; i < n.m_nameCount; ++i) {
        for (int j = i + 1; j < n.m_nameCount; ++j) {
            const int a = n.m_names[i].index, b = n.m_names[j].index;
            if (a == b || !(n.m_points[a].flags & CHECKPOINT_SPARE_LINK) || !(n.m_points[b].flags & CHECKPOINT_SPARE_LINK))
                continue;
            if (n.m_points[a].FindLink(static_cast<int16_t>(b)) != 0xFF)
                continue;
            char nameA[17] = {}, nameB[17] = {};
            std::memcpy(nameA, n.m_names[i].name, 16);
            std::memcpy(nameB, n.m_names[j].name, 16);
            if (n.Find(nameA) != a || n.Find(nameB) != b)
                continue;
            if (!n.Join(nameA, nameB))
                continue;
            g_totals.Add("joins tested");
            if (n.m_points[a].FindLink(static_cast<int16_t>(b)) == 0xFF || n.m_points[b].FindLink(static_cast<int16_t>(a)) == 0xFF)
                Fail(mission, "Join links", a, b);
            if (n.Join(nameA, nameB))
                Fail(mission, "Join twice", a, b);
            // Save, reset, load: the join comes back.
            C_chunk out;
            out.Open("joins.bin", true);
            out.Begin(0x81);
            n.SaveState(out);
            out.Ascend();
            out.Close();
            n.ResetState();
            if (n.m_points[a].FindLink(static_cast<int16_t>(b)) != 0xFF || !n.m_joins.empty())
                Fail(mission, "ResetState kept the join", a, b);
            C_chunk in;
            in.Open("joins.bin", false);
            uint16_t id = 0;
            in.Descend(id);
            n.LoadState(in);
            in.Ascend();
            in.Close();
            if (n.m_joins.size() != 1 || n.m_points[a].FindLink(static_cast<int16_t>(b)) == 0xFF)
                Fail(mission, "LoadState lost the join", a, b);
            if (!n.Separate(nameB, nameA) || !n.m_joins.empty() ||
                n.m_points[a].FindLink(static_cast<int16_t>(b)) != 0xFF || n.m_points[b].FindLink(static_cast<int16_t>(a)) != 0xFF)
                Fail(mission, "Separate", a, b);
            return;
        }
    }
}

static void CheckTruncatedJoins()
{
    // A join chunk with 2 bytes too many: the Fix ends the load.
    C_chunk out;
    out.Open("joins_bad.bin", true);
    out.Begin(0x81);
    out.Begin(0x4C5);
    const uint16_t data[3] = {0, 1, 2};
    out.Write(data, 6);
    out.Ascend();
    out.Ascend();
    out.Close();
    C_CheckpointNetwork n;
    C_chunk in;
    in.Open("joins_bad.bin", false);
    uint16_t id = 0;
    in.Descend(id);
    n.LoadState(in);
    in.Ascend();
    in.Close();
    std::printf("truncated join chunk: loaded and ended\n");
}

int main(int argc, char **argv)
{
    CheckTruncatedJoins();
    std::mt19937 rng(12345);
    int missions = 0;
    for (int a = 1; a < argc; ++a) {
        std::vector<std::filesystem::path> files;
        for (const auto &dir : std::filesystem::directory_iterator(argv[a])) {
            if (!dir.is_directory())
                continue;
            for (const auto &f : std::filesystem::directory_iterator(dir.path())) {
                std::string name = f.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "check2.bin")
                    files.push_back(f.path());
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto &file : files) {
            const std::string mission = file.parent_path().filename().string();
            const std::vector<unsigned char> bytes = ReadFile(file.string());
            C_chunk chunk;
            if (!chunk.Open(file.string().c_str(), false)) {
                Fail(mission.c_str(), "open");
                continue;
            }
            C_Checkpoints checkpoints;
            const int v1 = LoadCheckpointNetwork(&chunk, &checkpoints);
            const int end1 = chunk.Tell();
            const int v2 = LoadCheckpointNetwork(&chunk, checkpoints.GetRoadNetwork());
            const int end2 = chunk.Tell();
            chunk.Close();
            ++missions;
            if (static_cast<uint32_t>(v1) != CHECKPOINTS_VERSION_9)
                Fail(mission.c_str(), "walking network version", v1, 0);
            if (static_cast<uint32_t>(v2) != CHECKPOINTS_VERSION_9)
                Fail(mission.c_str(), "road network version", v2, 0);
            if (end2 != static_cast<int>(bytes.size()))
                Fail(mission.c_str(), "file not read to its end", end2, static_cast<long>(bytes.size()));
            S_NetInfo walkInfo, roadInfo;
            std::memcpy(&walkInfo.linkSlots, bytes.data() + 0x0C, 4);
            if (end1 + 0x10 <= static_cast<int>(bytes.size()))
                std::memcpy(&roadInfo.linkSlots, bytes.data() + end1 + 0x0C, 4);
            C_CheckpointNetwork &roads = *checkpoints.GetRoadNetwork();
            std::printf("%-22s walk %5d pts %4d names %3d actors | roads %4d pts %3d names\n", mission.c_str(),
                        checkpoints.m_pointCount, checkpoints.m_nameCount, checkpoints.m_actorCount, roads.m_pointCount,
                        roads.m_nameCount);
            CheckData(mission.c_str(), "walk", checkpoints, walkInfo);
            CheckData(mission.c_str(), "road", roads, roadInfo);
            CheckLookups(mission.c_str(), checkpoints, rng);
            CheckLookups(mission.c_str(), roads, rng);
            CheckPaths(mission.c_str(), checkpoints, rng);
            CheckPaths(mission.c_str(), roads, rng);
            CheckJoins(mission.c_str(), checkpoints);
            CheckJoins(mission.c_str(), roads);
        }
    }
    std::printf("\n%d missions, %d failures\n", missions, g_failures);
    for (const auto &[key, value] : g_totals.counts)
        std::printf("  %-40s %ld\n", key.c_str(), value);
    return g_failures ? 1 : 0;
}
