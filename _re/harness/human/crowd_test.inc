// Part 25 of the human harness (HUMAN_CROWD builds, build_crowd.bat; included by human_test.cpp after senses_test.inc):
// the humans keeping out of each other's way (game/human_crowd.cpp) against the original's, on part 24's harness.
//
// The AI memories (C_HumanAIMemory: the same layout on both sides) are each side's own: their owner (the human or one
// of part 24's other humans) and their route (the port's std::vector, the original's MSVC6 vector of the same
// waypoints) differ. The waypoints and the checkpoint network are shared: a network made by the test (points about
// the humans, circles of 0.5..4 m, links to near points), the port's game and the original's (+0x54) both pointing at
// it; the network's Get and FindNearestPoint (checkpoints.cpp, not linked) are fakes on both sides, the latter logged.
// After each call the memories are compared word by word (floats within the usual tolerance: the file's own note says
// sums are not always in the original's order), their pointers by what they name, and the routes.
//
// Part 25a: the geometry (AddPush, KeepInsideStrip, KeepOnNetwork, SortCorners) and a memory's own steps
// (AdvanceWaypoint, MoveOneStep, PushFromHuman, PushTargetFromHuman, PushOntoNetwork, HoldOnNetwork, TurnPushAside,
// UpdateStuckTimer), the memory's saved games.
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/human_crowd.h"

namespace {

// ---- the network
const int POINTS = 40;
alignas(16) uint8_t g_networkBytes[0x100];               // a C_Checkpoints (its constructor not linked)
S_Checkpoint g_points[POINTS];
S_CheckpointLink g_links[POINTS][8];
C_CheckpointNetwork *Network() { return reinterpret_cast<C_CheckpointNetwork *>(g_networkBytes); }
// The nearest point within `maxDist` (and beyond `minDist`) of `pos`, or none (the test's answer, both sides).
int NearestPoint(const S_vector &pos, float maxDist, float minDist)
{
    int best = -1;
    float bestSq = maxDist * maxDist;
    for (int i = 0; i < POINTS; ++i) {
        const S_vector d = g_points[i].pos - pos;
        const float sq = d.x * d.x + d.y * d.y + d.z * d.z;
        if (sq < bestSq && (minDist < 0.0f || sq > minDist * minDist)) {
            best = i;
            bestSq = sq;
        }
    }
    return best < 0 ? 0xFFFF : best;
}
S_Checkpoint *PointAt(int index) { return index >= 0 && index < POINTS ? &g_points[index] : nullptr; }
}  // namespace

// (checkpoint.cpp is not linked: the test's points own nothing.)
S_Checkpoint::~S_Checkpoint() {}
// The body's states the pass reads (part 25b's; the original's redirected to the same).
namespace {
struct S_CrowdScript {
    int climbState = 0;                                 // S_HumanBody::GetClimbState
    bool workState5 = false;                            // S_HumanBody::IsWorkState5
} g_crowd;
}  // namespace
int S_HumanBody::GetClimbState() const
{
    Log(nullptr, "BodyGetClimbState");
    return g_crowd.climbState;
}
bool S_HumanBody::IsWorkState5() const
{
    Log(nullptr, "BodyIsWorkState5");
    return g_crowd.workState5;
}
// The waypoint pool (ai_parts.cpp is not linked): logged (part 25b's).
namespace {
alignas(16) S_AIWaypoint g_newWaypoints[64];
int g_newWaypointNext = 0;
S_AIWaypoint *NewWaypoint(int checkpoint, const S_vector4 *pos, int flags, int type, float radius)
{
    Log(nullptr, "AllocWaypoint", {double(checkpoint), pos->x, pos->y, pos->z, pos->w, double(flags & 0xFFFF), double(type & 0xFF), radius});
    S_AIWaypoint *w = &g_newWaypoints[g_newWaypointNext++ % 64];
    w->pos = *pos;
    w->pos2 = *pos;
    w->checkpoint = checkpoint;
    w->flags = uint16_t(flags);
    w->type = uint8_t(type);
    w->radius = radius;
    return w;
}
}  // namespace
S_AIWaypoint *AllocWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags, uint8_t type, float radius)
{
    return NewWaypoint(checkpoint, &pos, flags, type, radius);
}
void ReleaseWaypoint(S_AIWaypoint *waypoint) { Log(nullptr, ("ReleaseWaypoint " + NameOf(waypoint)).c_str()); }

// The network's members the crowd calls (the port's; the original's redirected to the same).
S_Checkpoint *C_CheckpointNetwork::Get(int index) { return PointAt(index); }
int C_CheckpointNetwork::FindNearestPoint(S_vector pos, float maxDist, float minDist) const
{
    Log(this, "FindNearestPoint", {pos.x, pos.y, pos.z, maxDist, minDist});
    return NearestPoint(pos, maxDist, minDist);
}

namespace {
// (A 16-bit index: KeepOnNetwork passes a link's target in AX, the rest of EAX left over.)
void *CT_ CR_Get(void *, int index) { return PointAt(index & 0xFFFF); }
int CT_ CR_FindNearestPoint(void *self, float x, float y, float z, float maxDist, float minDist)
{
    Log(self, "FindNearestPoint", {x, y, z, maxDist, minDist});
    return NearestPoint(S_vector(x, y, z), maxDist, minDist);
}

// ---- the memories: each side's two (the human's and another's), their routes; the shared waypoints
const int WAYPOINTS = 12;
alignas(16) S_AIWaypoint g_wp[WAYPOINTS];
alignas(16) uint8_t g_memPortBytes[2][sizeof(C_HumanAIMemory)], g_memOrig[2][0x110];
std::vector<S_AIWaypoint *> g_routePort[2];
alignas(16) uint8_t g_routeOrig[2][16];
alignas(16) uint8_t g_origCrowd[0x8000];                // the original's crowd object (its methods' this; zeroed)
C_HumanAIMemory &MemPort(int k) { return *reinterpret_cast<C_HumanAIMemory *>(g_memPortBytes[k]); }
// What the port's memories were before the call (the original's built from them).
uint8_t g_memBefore[2][0x110];
std::vector<S_AIWaypoint *> g_routeBefore[2];
uint8_t g_wpBefore[sizeof g_wp];

void *MemOwnerOrig(int k) { return k == 0 ? static_cast<void *>(o_human) : static_cast<void *>(g_otherOrig[0]); }
C_Human *MemOwnerPort(int k) { return k == 0 ? g_h.human : OtherPort(0); }

void SetUpCrowd()
{
    SetUpSenses();
    RedirectOriginal(0x0051d5a0, FAKE(CR_Get));
    RedirectOriginal(0x0051d5c0, FAKE(CR_FindNearestPoint));
    for (uint32_t at : {0x00565540u, 0x00567df0u, 0x00568190u, 0x0056de90u, 0x005653c0u, 0x00565f30u, 0x00566130u,
                        0x00566e50u, 0x00567ab0u, 0x00567ba0u, 0x00567c80u, 0x00569530u, 0x005641b0u, 0x005643e0u})
        RestoreOriginal(at);
    // The network: points about the humans (20, 1, 20), each linked to its nearest few.
    std::mt19937 rng(25);
    const auto u = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    for (int i = 0; i < POINTS; ++i) {
        g_points[i].pos = S_vector(u(10, 30), u(0, 2), u(10, 30));
        g_points[i].radius = uint8_t(std::uniform_int_distribution<int>(8, 64)(rng));
        // (Every fourth on whole metres with a radius of whole halves: exact ties at its circle's edge.)
        if (i % 4 == 0) {
            g_points[i].pos = S_vector(std::floor(g_points[i].pos.x), 1.0f, std::floor(g_points[i].pos.z));
            g_points[i].radius = uint8_t(8 * std::uniform_int_distribution<int>(1, 6)(rng));
        }
    }
    for (int i = 0; i < POINTS; ++i) {
        std::vector<std::pair<float, int>> nearest;
        for (int j = 0; j < POINTS; ++j)
            if (j != i)
                nearest.push_back({(g_points[j].pos - g_points[i].pos).Magnitude(), j});
        std::sort(nearest.begin(), nearest.end());
        const int links = std::uniform_int_distribution<int>(0, 5)(rng);
        for (int l = 0; l < links; ++l)
            g_links[i][l] = S_CheckpointLink{uint16_t(nearest[size_t(l)].second), uint16_t(nearest[size_t(l)].first * 100)};
        g_points[i].links = g_links[i];
        g_points[i].flags = uint16_t(links);
    }
    // (The count at +0x00 and the points at +0x04, both sides' layout.)
    const uint16_t count = POINTS;
    S_Checkpoint *points = g_points;
    std::memcpy(g_networkBytes, &count, 2);
    std::memcpy(g_networkBytes + 4, &points, 4);
    GetGame()->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_networkBytes);
    void *netWord = g_networkBytes;
    std::memcpy(g_origGame + 0x54, &netWord, 4);
    g_names[g_networkBytes] = "network";
    g_names[g_origCrowd] = "crowd";
    for (int k = 0; k < 2; ++k) {
        new (g_memPortBytes[k]) C_HumanAIMemory(MemOwnerPort(k));
        g_names[g_memPortBytes[k]] = g_names[g_memOrig[k]] = "memory" + std::to_string(k);
        g_names[&g_routePort[k]] = g_names[g_routeOrig[k]] = "route" + std::to_string(k);
        MemOwnerPort(k)->m_aiMemory = &MemPort(k);
    }
    for (int k = 0; k < WAYPOINTS; ++k)
        g_names[&g_wp[k]] = "waypoint" + std::to_string(k);
    for (int i = 0; i < POINTS; ++i)
        g_names[&g_points[i]] = "point" + std::to_string(i);
}

// ---- the random state
S_vector4 Add4(const S_vector4 &a, const S_vector4 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
// Near the humans (about (20, 1, 20)), sometimes on a point, at its circle's edge or on another memory's place.
S_vector4 Place()
{
    if (P(0.25f)) {
        const S_Checkpoint &p = g_points[I(0, POINTS - 1)];
        const float r = float(p.radius) * 0.0625f * (P(0.3f) ? 1.0f : U(0, 1.5f));
        const float a = U(0, 6.2831853f);
        return {p.pos.x + r * std::cos(a), p.pos.y + U(-0.5f, 0.5f), p.pos.z + r * std::sin(a), 0.0f};
    }
    return {U(14, 26), U(0, 2.5f), U(14, 26), 0.0f};
}
void RandomMemory(C_HumanAIMemory &m, int k)
{
    std::memset(static_cast<void *>(&m), 0, sizeof m);
    m.owner = MemOwnerPort(k);
    for (float &f : m.step)
        f = U(-1, 1);
    m.stepping = P(0.5f);
    m.pos = Place();
    m.velocity = P(0.2f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-3, 3), P(0.7f) ? 0.0f : U(-0.5f, 0.5f), U(-3, 3), 0.0f};
    m.push = P(0.3f) ? S_vector4{0, 0, 0, 0} : S_vector4{U(-0.5f, 0.5f), P(0.7f) ? 0.0f : U(-0.1f, 0.1f), U(-0.5f, 0.5f), 0.0f};
    m.start = Place();
    m.target = P(0.3f) ? Add4(m.pos, S_vector4{U(-1, 1), 0, U(-1, 1), 0}) : Place();
    m.lastGoal = Place();
    m.routeStart = Place();
    // A lying body's box: upright axes turned about y, or tilted.
    const float a = U(0, 6.2831853f);
    S_vector ax(std::cos(a), 0, std::sin(a)), az(-std::sin(a), 0, std::cos(a)), ay(0, 1, 0);
    if (P(0.3f)) {
        ay = S_vector(U(-0.3f, 0.3f), 1, U(-0.3f, 0.3f));
        ay.Normalize();
    }
    m.axisX = {ax.x, ax.y, ax.z, 0};
    m.axisY = {ay.x, ay.y, ay.z, 0};
    m.axisZ = {az.x, az.y, az.z, 0};
    m.boxCenter = m.pos;
    m.halfSize[0] = U(0.2f, 1.0f);
    m.halfSize[1] = U(0.1f, 0.4f);
    m.halfSize[2] = U(0.2f, 1.0f);
    m.speed = P(0.2f) ? 0.0f : U(0.5f, 5);
    const int n = P(0.2f) ? 0 : I(1, 6);
    g_routePort[k].clear();
    for (int i = 0; i < n; ++i)
        g_routePort[k].push_back(&g_wp[I(0, WAYPOINTS - 1)]);
    m.route = P(0.1f) ? nullptr : &g_routePort[k];
    m.waypoint = n ? uint16_t(I(0, n - 1)) : 0;
    m.targetPoint = P(0.3f) ? nullptr : PointAt(I(0, POINTS - 1));
    m.nearPoint = P(0.3f) ? nullptr : PointAt(I(0, POINTS - 1));
    m.currentPoint = P(0.3f) ? nullptr : PointAt(I(0, POINTS - 1));
    m.stuckTimer = int16_t(P(0.2f) ? I(-2, 2) : I(-100, 3000));
    m.bestDistance = P(0.2f) ? 0.0f : U(0, 50);
    m.plannedFrom = I(-1, 9);
    m.routeChanged = P(0.5f);
    m.notPlayer = P(0.7f);
    m.walking = P(0.6f);
    m.solved = P(0.8f);
    m.givesWay = P(0.6f);
    m.unknown109 = P(0.5f);
    m.flags = uint8_t(P(0.6f) ? 0 : I(0, 0x3F));
}
void RandomCrowd()
{
    RandomSenses();
    for (int i = 0; i < WAYPOINTS; ++i) {
        S_AIWaypoint &w = g_wp[i];
        const int point = I(0, POINTS - 1);
        w.pos = P(0.5f) ? S_vector4{g_points[point].pos.x, g_points[point].pos.y, g_points[point].pos.z, 1.0f} : Place();
        w.pos2 = w.pos;
        w.checkpoint = P(0.1f) ? -1 : P(0.05f) ? POINTS + 3 : point;
        w.flags = uint16_t(I(0, 0xFFFF));
        w.type = uint8_t(I(0, 20));
        w.radius = U(0.5f, 4);
    }
    for (int k = 0; k < 2; ++k)
        RandomMemory(MemPort(k), k);
    // The two close now and then.
    if (P(0.5f)) {
        MemPort(1).pos = Add4(MemPort(0).pos, S_vector4{U(-1.2f, 1.2f), U(-0.3f, 0.3f), U(-1.2f, 1.2f), 0});
        MemPort(1).target = Add4(MemPort(0).target, S_vector4{U(-1.2f, 1.2f), U(-0.3f, 0.3f), U(-1.2f, 1.2f), 0});
        if (P(0.1f))
            MemPort(1).target = MemPort(0).target;
        if (P(0.1f))
            MemPort(1).pos = MemPort(0).pos;
    }
    g_ctl.bodyStance = P(0.3f) ? 2 : I(0, 3);
}

// ---- a call of both sides, the memories compared after it
// The port's memory in the original's form (its owner and route the original's).
void MemToOrig(int k)
{
    std::memcpy(g_memOrig[k], g_memBefore[k], 0x110);
    const auto *m = reinterpret_cast<const C_HumanAIMemory *>(g_memBefore[k]);
    void *route = m->route ? static_cast<void *>(g_routeOrig[k]) : nullptr;
    void *owner = MemOwnerOrig(k);
    std::memcpy(g_memOrig[k] + 0xE0, &route, 4);
    std::memcpy(g_memOrig[k] + 0xE8, &owner, 4);
    std::vector<uint32_t> words;
    for (S_AIWaypoint *w : g_routeBefore[k])
        words.push_back(Word(w));
    MsvcVectorPut(g_routeOrig[k], words, size_t(I(0, 2)));
}
// The first difference between the port's memory (after its call) and the original's, or "".
std::string MemDiffers(int k, const uint8_t *port, const std::vector<S_AIWaypoint *> &portRoute)
{
    const uint8_t *orig = g_memOrig[k];
    char text[160];
    for (size_t i = 0; i < 0x110; i += 4) {
        uint32_t x, y;
        std::memcpy(&x, port + i, 4);
        std::memcpy(&y, orig + i, 4);
        if (i == 0xE0) {
            const bool same = (x == Word(&g_routePort[k])) == (y == Word(g_routeOrig[k])) && (x == 0) == (y == 0);
            if (!same)
                return "route pointer";
            continue;
        }
        if (i == 0xE8) {
            if (NameOf(reinterpret_cast<void *>(uintptr_t(x))) != NameOf(reinterpret_cast<void *>(uintptr_t(y))))
                return "owner";
            continue;
        }
        // (The push's w: the original's AddPush copies a whole 16-byte push in, the port only x, y, z.)
        // (Likewise the position's, the velocity's and the target's w, +0x2C, +0x3C and +0x6C: the original's
        // HoldOnNetwork, MoveOneStep and MoveTarget store a stack temporary's undefined one.)
        if (i == 0x4C || i == 0x2C || i == 0x3C || i == 0x6C)
            continue;
        if (!WordsNear(x, y, 'm')) {
            float a, b;
            std::memcpy(&a, &x, 4);
            std::memcpy(&b, &y, 4);
            std::snprintf(text, sizeof text, "memory%d+0x%02x (port %08x %g original %08x %g)", k, unsigned(i), x, a, y, b);
            return text;
        }
    }
    if (ListText(Words(portRoute)) != ListText(MsvcVectorGet(g_routeOrig[k])))
        return "route" + std::to_string(k) + ": port " + ListText(Words(portRoute)) + "/ original " + ListText(MsvcVectorGet(g_routeOrig[k]));
    return "";
}
uint8_t g_memAfter[2][0x110];
std::vector<S_AIWaypoint *> g_routeAfter[2];
uint8_t g_wpAfter[sizeof g_wp];
template <class P, class O> void CrowdCall(const char *what, P port, O original)
{
    for (int k = 0; k < 2; ++k) {
        std::memcpy(g_memBefore[k], g_memPortBytes[k], 0x110);
        g_routeBefore[k] = g_routePort[k];
    }
    std::memcpy(g_wpBefore, g_wp, sizeof g_wp);
    DiffCall(what,
             [&] {
                 const int r = port();
                 for (int k = 0; k < 2; ++k) {
                     std::memcpy(g_memAfter[k], g_memPortBytes[k], 0x110);
                     g_routeAfter[k] = g_routePort[k];
                 }
                 std::memcpy(g_wpAfter, g_wp, sizeof g_wp);
                 return r;
             },
             [&](void *x) {
                 for (int k = 0; k < 2; ++k)
                     MemToOrig(k);
                 std::memcpy(g_wp, g_wpBefore, sizeof g_wp);
                 return original(x);
             });
    std::string why;
    for (int k = 0; k < 2 && why.empty(); ++k)
        why = MemDiffers(k, g_memAfter[k], g_routeAfter[k]);
    if (why.empty() && std::memcmp(g_wpAfter, g_wp, sizeof g_wp) != 0)
        why = "waypoints";
    CHECK(why.empty());
    static int shown;
    if (!why.empty() && shown++ < 15) {
        std::printf("  %s: %s\n", what, why.c_str());
        // (CROWD_DUMP: both memories before the call, as words.)
        if (std::getenv("CROWD_DUMP"))
            for (int k = 0; k < 2; ++k)
                std::printf("    before%d %s\n", k, Hex(g_memBefore[k], 0x110).c_str());
    }
    // (The port's state after its call, for the next one.)
    for (int k = 0; k < 2; ++k) {
        std::memcpy(g_memPortBytes[k], g_memAfter[k], 0x110);
        g_routePort[k] = g_routeAfter[k];
    }
}

// Each side's floats as words (the results compared within the tolerance, as the memories are).
uint32_t FloatsHash(const float *f, int n)
{
    uint32_t h = 0;
    for (int i = 0; i < n; ++i) {
        // (Rounded to 12 bits of mantissa: the last bits may differ.)
        uint32_t w;
        std::memcpy(&w, &f[i], 4);
        h = h * 31 + ((w + 0x400u) & ~0x7FFu);
    }
    return h;
}

template <class R, class... A> R OStd(uint32_t at, A... a)
{
    typedef R(__stdcall * T)(A...);
    return OriginalAt<T>(at)(a...);
}

// ---- part 25a
void TestCrowdGeometry()
{
    std::printf("crowd: geometry\n");
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomCrowd();
        switch (i % 4) {
        case 0: {
            // A total of pushes (none, along the new one, against it, across) and a new push.
            alignas(16) float total[4] = {U(-1, 1), U(-0.2f, 0.2f), U(-1, 1), U(-1, 1)};
            alignas(16) float push[4] = {U(-1, 1), U(-0.2f, 0.2f), U(-1, 1), 0.0f};
            if (P(0.15f))
                total[0] = total[1] = total[2] = 0.0f;
            else if (P(0.2f)) {
                const float k = P(0.5f) ? U(0.2f, 3) : U(-3, -0.2f);
                for (int c = 0; c < 3; ++c)
                    push[c] = total[c] * k;
            }
            alignas(16) float a[4], b[4];
            std::memcpy(a, total, 16);
            std::memcpy(b, total, 16);
            DiffCall("AddPush",
                     [&] {
                         crowd::AddPush(*reinterpret_cast<S_vector4 *>(a), S_vector(push[0], push[1], push[2]));
                         return int(FloatsHash(a, 3));
                     },
                     [&](void *) {
                         OStd<void>(0x00565540, static_cast<void *>(b), static_cast<void *>(push));
                         return int(FloatsHash(b, 3));
                     });
            break;
        }
        case 1: {
            // A position about the strip between two circles (inside either, beside, before, after).
            S_vector A(U(15, 25), U(0, 2), U(15, 25));
            S_vector B = P(0.1f) ? A : A + S_vector(U(-6, 6), U(-1, 1), U(-6, 6));
            float ra = U(0.3f, 3), rb = U(0.3f, 3);
            alignas(16) float pos[4] = {A.x + U(-8, 8), A.y + U(-1, 1), A.z + U(-8, 8), 0.0f};
            if (P(0.2f)) {
                pos[0] = B.x + U(-0.5f, 0.5f);
                pos[2] = B.z + U(-0.5f, 0.5f);
            }
            // Exact ties: whole metres, radii of whole halves; on a circle's edge, on the strip's edge half way (the
            // strip along x, both radii alike), before A or past B on the line.
            if (P(0.35f)) {
                A = S_vector(float(I(15, 25)), float(I(0, 2)), float(I(15, 25)));
                const int d = I(-4, 4);
                B = A + S_vector(float(2 * d), float(I(-1, 1)), 0.0f);
                ra = 0.5f * float(I(1, 4));
                rb = P(0.5f) ? ra : 0.5f * float(I(1, 4));
                const float y = A.y + float(I(-1, 1));
                switch (I(0, 3)) {
                case 0:
                    pos[0] = A.x + (P(0.5f) ? ra : -ra), pos[1] = y, pos[2] = A.z;
                    break;
                case 1:
                    pos[0] = B.x, pos[1] = y, pos[2] = B.z + (P(0.5f) ? rb : -rb);
                    break;
                case 2:
                    rb = ra;
                    pos[0] = A.x + float(d), pos[1] = y, pos[2] = A.z + (P(0.5f) ? ra : -ra);
                    break;
                default:
                    pos[0] = (d >= 0 ? A.x - ra : A.x + ra), pos[1] = y, pos[2] = A.z;
                    break;
                }
            }
            alignas(16) float a[4], b[4], va[4] = {A.x, A.y, A.z, 0}, vb[4] = {B.x, B.y, B.z, 0};
            std::memcpy(a, pos, 16);
            std::memcpy(b, pos, 16);
            DiffCall("KeepInsideStrip",
                     [&] {
                         S_vector p(a[0], a[1], a[2]);
                         const int r = crowd::KeepInsideStrip(p, A, ra, B, rb);
                         a[0] = p.x, a[1] = p.y, a[2] = p.z;
                         return r + 2 * int(FloatsHash(a, 3));
                     },
                     [&](void *) {
                         const int r = OStd<uint32_t>(0x00567df0, static_cast<void *>(b), static_cast<void *>(va), ra,
                                                      static_cast<void *>(vb), rb) & 0xFF;
                         return r + 2 * int(FloatsHash(b, 3));
                     });
            break;
        }
        case 2: {
            // A position about a point (in its circle, out of it, in a strip to a neighbour); no point.
            const int index = I(0, POINTS - 1);
            const S_Checkpoint *point = P(0.05f) ? nullptr : &g_points[index];
            alignas(16) S_vector4 pos = Place();
            if (point && P(0.4f))
                pos = {point->pos.x + U(-5, 5), point->pos.y + U(-1, 1), point->pos.z + U(-5, 5), 0.0f};
            // (On the edge of a whole-metre point's circle: an exact tie.)
            if (P(0.2f)) {
                point = &g_points[4 * I(0, POINTS / 4 - 1)];
                const float r = float(point->radius) * 0.0625f;
                pos = {point->pos.x + (P(0.5f) ? r : 0.0f), point->pos.y, point->pos.z + (P(0.5f) ? 0.0f : -r), 0.0f};
            }
            alignas(16) S_vector4 a = pos, b = pos;
            const S_Checkpoint *outA = nullptr, *outB = nullptr;
            DiffCall("KeepOnNetwork",
                     [&] {
                         const int r = crowd::KeepOnNetwork(Network(), a, point, &outA);
                         return r + 4 * int(FloatsHash(&a.x, 3)) + 8 * int(outA ? outA - g_points : -1);
                     },
                     [&](void *) {
                         typedef uint32_t(CT_ * T)(void *, void *, void *, const void *, const S_Checkpoint **);
                         const int r = OriginalAt<T>(0x00568190)(g_origCrowd, g_networkBytes, &b, point, &outB) & 0xFF;
                         return r + 4 * int(FloatsHash(&b.x, 3)) + 8 * int(outB ? outB - g_points : -1);
                     });
            break;
        }
        default: {
            // A position in the unit square or about it, a side and a direction.
            alignas(16) float pos[4] = {U(-1.5f, 1.5f), U(-0.5f, 0.5f), U(-1.5f, 1.5f), 0};
            alignas(16) float side[4] = {U(-1, 1), 0, U(-1, 1), 0}, dir[4] = {U(-1, 1), 0, U(-1, 1), 0};
            if (P(0.2f))
                pos[0] = 1.0f;
            // (No direction now and then: every corner equally far along it.)
            if (P(0.15f))
                dir[0] = dir[2] = 0.0f;
            alignas(16) float cornersA[16], cornersB[16];
            int negA[8], posA[8], ncA = -1, pcA = -1, negB[8], posB[8], ncB = -1, pcB = -1;
            const auto result = [](const float *c, const int *neg, const int *pos, int nc, int pc, int stride) {
                std::vector<float> f;
                for (int q = 0; q < 4; ++q)
                    for (int e = 0; e < 3; ++e)
                        f.push_back(c[q * stride + e]);
                uint32_t h = FloatsHash(f.data(), 12);
                for (int q = 0; q < nc; ++q)
                    h = h * 7 + uint32_t(neg[q]);
                for (int q = 0; q < pc; ++q)
                    h = h * 11 + uint32_t(pos[q]);
                return int(h * 13 + uint32_t(nc * 5 + pc));
            };
            DiffCall("SortCorners",
                     [&] {
                         S_vector corners[4];
                         crowd::SortCorners(S_vector(pos[0], pos[1], pos[2]), S_vector(side[0], side[1], side[2]),
                                            S_vector(dir[0], dir[1], dir[2]), corners, negA, posA, &ncA, &pcA);
                         for (int q = 0; q < 4; ++q)
                             std::memcpy(cornersA + q * 3, &corners[q], 12);
                         return result(cornersA, negA, posA, ncA, pcA, 3);
                     },
                     [&](void *) {
                         // (The original's corners are 16 bytes apart.)
                         OStd<void>(0x0056de90, static_cast<void *>(pos), static_cast<void *>(side), static_cast<void *>(dir),
                                    static_cast<void *>(cornersB), static_cast<void *>(negB), static_cast<void *>(posB),
                                    static_cast<void *>(&ncB), static_cast<void *>(&pcB));
                         return result(cornersB, negB, posB, ncB, pcB, 4);
                     });
            break;
        }
        }
    }
}

// A chunk after the memory's (the loading must leave the file there: the next chunk is read after it).
void WriteAfterChunk(C_chunk &c)
{
    c.Begin(0x0123);
    const int word = 0x55AA;
    c.Write(&word, 4);
    c.Ascend();
}
int NextChunkId(C_chunk &c)
{
    uint16_t id = 0;
    return c.Descend(id) ? int(id) : -1;
}

void TestCrowdSteps()
{
    std::printf("crowd: a memory's steps\n");
    for (int i = 0; i < 22000 / QUICK; ++i) {
        RandomCrowd();
        C_HumanAIMemory &self = MemPort(0), &other = MemPort(1);
        const float time = P(0.2f) ? 0.0f : U(0.01f, 0.5f);
        switch (i % 9) {
        case 0:
            // (A route with points: the original reads one.)
            if (!self.route || self.route->empty())
                break;
            // (Now and then this move ends about 5 cm short of the waypoint: the reach it skips within.)
            if (P(0.3f)) {
                const S_vector4 &w = (*self.route)[self.waypoint]->pos;
                const float d = U(0.045f, 0.065f), a = U(0, 6.2831853f);
                self.pos = {w.x - time * self.velocity.x - d * std::cos(a), w.y - time * self.velocity.y,
                            w.z - time * self.velocity.z - d * std::sin(a), 0.0f};
            }
            CrowdCall("AdvanceWaypoint", [&] { crowd::AdvanceWaypoint(self, time); return 0; },
                      [&](void *) { OStd<void>(0x005653c0, static_cast<void *>(g_memOrig[0]), time); return 0; });
            break;
        case 1:
            CrowdCall("MoveOneStep", [&] { crowd::MoveOneStep(self, time == 0.0f ? 0.1f : time); return 0; },
                      [&](void *) { OStd<void>(0x00565f30, static_cast<void *>(g_memOrig[0]), time == 0.0f ? 0.1f : time); return 0; });
            break;
        case 2:
            // (Now and then overlapping and heading straight at the other: nothing across. Only overlapping: two apart
            // and head on, which side to give way to is the sign of rounding noise.)
            if (P(0.2f)) {
                other.pos = Add4(self.pos, S_vector4{U(-0.5f, 0.5f), U(-0.3f, 0.3f), U(-0.5f, 0.5f), 0});
                const float k = U(-3, 3);
                self.velocity = {(self.pos.x - other.pos.x) * k, 0.0f, (self.pos.z - other.pos.z) * k, 0.0f};
            }
            CrowdCall("PushFromHuman", [&] { crowd::PushFromHuman(self, other); return 0; },
                      [&](void *) { OCall<void>(0x00566130, g_origCrowd, static_cast<void *>(g_memOrig[0]), static_cast<void *>(g_memOrig[1]), 0); return 0; });
            break;
        case 3:
            // (Now and then the same target and almost the same place: apart along a tiny offset, one axis or none.)
            if (P(0.2f)) {
                other.target = self.target;
                const int axis = I(0, 3);
                other.pos = self.pos;
                if (axis < 3)
                    (&other.pos.x)[axis] += P(0.5f) ? 1e-5f : -1e-5f;
            }
            CrowdCall("PushTargetFromHuman", [&] { crowd::PushTargetFromHuman(self, other); return 0; },
                      [&](void *) { OCall<void>(0x00566e50, g_origCrowd, static_cast<void *>(g_memOrig[0]), static_cast<void *>(g_memOrig[1]), 0); return 0; });
            break;
        case 4:
            CrowdCall("PushOntoNetwork", [&] { crowd::PushOntoNetwork(self); return 0; },
                      [&](void *) { OCall<void>(0x00567ab0, g_origCrowd, static_cast<void *>(g_memOrig[0]), 0); return 0; });
            break;
        case 5:
            CrowdCall("HoldOnNetwork", [&] { crowd::HoldOnNetwork(self); return 0; },
                      [&](void *) { OCall<void>(0x00567ba0, g_origCrowd, static_cast<void *>(g_memOrig[0]), 0); return 0; });
            break;
        case 6: {
            // (A push straight back now and then, or nearly.)
            if (P(0.4f)) {
                const float k = -U(0.1f, 2);
                self.push = {self.velocity.x * k + U(-0.05f, 0.05f), self.velocity.y * k, self.velocity.z * k + U(-0.05f, 0.05f), 0};
            }
            CrowdCall("TurnPushAside", [&] { crowd::TurnPushAside(self, time); return 0; },
                      [&](void *) { OStd<void>(0x00567c80, static_cast<void *>(g_memOrig[0]), time); return 0; });
            break;
        }
        case 7: {
            if (!self.route || self.route->empty())
                break;
            const int16_t delta = int16_t(P(0.2f) ? I(0, 3) : I(0, 3000));
            if (P(0.2f)) {
                const S_vector4 &w = (*self.route)[self.waypoint]->pos;
                const S_vector d(self.pos.x - w.x, self.pos.y - w.y, self.pos.z - w.z);
                self.bestDistance = d.x * d.x + d.y * d.y + d.z * d.z;
            }
            CrowdCall("UpdateStuckTimer", [&] { crowd::UpdateStuckTimer(self, delta); return 0; },
                      [&](void *) { OStd<void>(0x00569530, static_cast<void *>(g_memOrig[0]), int(delta)); return 0; });
            break;
        }
        default: {
            // The memory's saved game: each side's file, then the port's (or a random one) loaded on both.
            const std::string portFile = TempFile("aimem_port.sav"), origFile = TempFile("aimem_orig.sav");
            std::remove(portFile.c_str());
            std::remove(origFile.c_str());
            CrowdCall("AIMemorySave",
                      [&] {
                          C_chunk c;
                          c.Open(portFile.c_str(), true);
                          c.Begin(0x7777);
                          const int r = self.SaveGame(c);
                          WriteAfterChunk(c);
                          c.Ascend();
                          return r;
                      },
                      [&](void *) {
                          C_chunk c;
                          c.Open(origFile.c_str(), true);
                          c.Begin(0x7777);
                          const int r = OCall<uint32_t>(0x005641b0, static_cast<void *>(g_memOrig[0]), &c) & 0xFF;
                          WriteAfterChunk(c);
                          c.Ascend();
                          return r;
                      });
            CHECK(FileBytes(portFile) == FileBytes(origFile));
            RandomMemory(self, 0);
            if (P(0.5f)) {
                std::remove(portFile.c_str());
                C_chunk c;
                c.Open(portFile.c_str(), true);
                c.Begin(0x7777);
                if (P(0.9f)) {
                    c.Begin(P(0.85f) ? 100 : 101);
                    const int n = I(0, 14);
                    for (int q = 0; q < n; ++q) {
                        c.Begin(uint16_t(0x3E8 + I(0, 12)));
                        const int bytes = P(0.2f) ? I(0, 17) : 16;
                        for (int b = 0; b < bytes; ++b) {
                            const uint8_t v = uint8_t(I(0, 255));
                            c.Write(&v, 1);
                        }
                        c.Ascend();
                    }
                    c.Ascend();
                }
                WriteAfterChunk(c);
                c.Ascend();
            }
            CrowdCall("AIMemoryLoad",
                      [&] {
                          C_chunk c;
                          c.Open(portFile.c_str(), false);
                          uint16_t outer = 0;
                          c.Descend(outer);
                          const int r = int(self.LoadGame(c));
                          return r + 2 * NextChunkId(c);
                      },
                      [&](void *) {
                          C_chunk c;
                          c.Open(portFile.c_str(), false);
                          uint16_t outer = 0;
                          c.Descend(outer);
                          const int r = int(OCall<uint32_t>(0x005643e0, static_cast<void *>(g_memOrig[0]), &c) & 0xFF);
                          return r + 2 * NextChunkId(c);
                      });
            break;
        }
        }
    }
}

void TestCrowd()
{
    SetUpCombat();
    SetUpMotion();
    SetUpDeaths();
    SetUpControl();
    SetUpCrowd();
    // CROWD_ONLY: one part's tests alone (25a, 25b ...), for its mutation list.
    const char *only = std::getenv("CROWD_ONLY");
    const auto run = [&](const char *part) { return !only || std::strcmp(only, part) == 0; };
    if (run("25a")) {
        TestCrowdGeometry();
        TestCrowdSteps();
    }
}

}  // namespace
