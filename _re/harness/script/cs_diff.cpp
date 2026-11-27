// Checkpoint search part 1: the checkpoint networks' reachability queries, cover picking and path searches
// (game/checkpoint_search.cpp, the original's 0x0051e8a0..0x00522470) against the original executable's. cn_diff.cpp's
// world, networks, data and operations are included (CN_HARNESS); the calls go through cs_calls.s / cs_calls.inc
// (gen_cmd_calls.py cs).
//
// Each case loads the same networks on each side as cn_diff's do (a shipped mission's check2.bin or the test's), runs
// cn_diff's operations among this part's: GetNeighbours, GetReachable, GetReachableCopy, GetNeighbourCover,
// GetReachableCover, GetReachableDepthFirst and GetReachableType12 (from points, places and radii of every size),
// FindNearestFree (points under cars about), PickCoverPoint (candidates of every kind, every flag), FindPath (every
// exclusion), FindMeleeTrackerPath (doors open and closed, the types it will not pass) and FindEscapePath (threats near
// and far). After each operation the answer, the lists and the calls are compared, and the networks' state after those
// that change it.
// MinGW; build_cs.bat; runs cs_diff.exe ("cs_diff: N checks, 0 failures"). Env: HD2_CS_CASES (default 1,500;
// HD2_CS_QUICK: 300), HD2_CS_SEED, HD2_CS_SHOW (1: the port's lines; 2: both), HD2_CS_ONLY, HD2_CS_TRACE, HD2_CS_FULL,
// HD2_UNPACKED.
#define CN_HARNESS
#define HARNESS_NAME "cs_diff"
#define CMD_CALLS_INC "cs_calls.inc"
#include "cn_diff.cpp"

namespace H {

constexpr uint32_t O_CS_NEIGHBOURS = 0x00520830, O_CS_REACHABLE = 0x00520860, O_CS_REACHABLE_COPY = 0x005208a0,
                   O_CS_NEIGHBOUR_COVER = 0x005208e0, O_CS_REACHABLE_COVER = 0x00520910, O_CS_DEPTH_FIRST = 0x0051f5e0,
                   O_CS_TYPE12 = 0x0051f700, O_CS_NEAREST_FREE = 0x0051e8a0, O_CS_PICK_COVER = 0x00520a30,
                   O_CS_FIND_PATH = 0x005223b0, O_CS_TRACKER_PATH = 0x00522470, O_CS_ESCAPE = 0x00521460;
enum { CS_REACH = OP_KINDS, CS_TYPE12, CS_FREE, CS_COVER_PICK, CS_PATH, CS_TRACKER_PATH, CS_ESCAPE, CS_END };
const char *const CS_NAMES[] = {"Reach", "ReachableType12", "FindNearestFree", "PickCoverPoint", "FindPath", "FindMeleeTrackerPath",
                                "FindEscapePath"};

std::string RunCsOp(const S_CnCase &c, const S_CnOp &op, int index)
{
    if (op.kind < OP_KINDS)
        return RunCnOp(c, op, index);
    const bool o = g_side != 0;
    const int which = op.net;
    uint8_t *n = Net(which);
    C_CheckpointNetwork *p = PortNet(which);
    const S_NetView v = View(which);
    auto ptName = [&](const void *q) -> std::string {
        if (!q)
            return "null";
        const uint8_t *b = static_cast<const uint8_t *>(q);
        if (v.points && b >= v.points && b < v.points + 0x24 * v.count)
            return F("point %d", int((b - v.points) / 0x24));
        return NameOf(Ptr(q));
    };
    auto point = [&](int k) -> int {
        if (k % 16 == 0)
            return v.count + k % 3;
        if (k % 16 == 1)
            return k % 32 == 1 ? CHECKPOINT_NONE : -1 - k % 3;
        return v.count ? (k / 16) % v.count : 0;
    };
    static const float radii[] = {0.0f, 1.0f, 5.0f, 12.5f, 30.0f, 100.0f, -1.0f, 1e6f, 0.001f, 250.0f};
    switch (op.kind) {
    case CS_REACH: {
        // The flood's queries from a point (by index or pointer) within a radius.
        const int kind = op.a % 6;
        const int i = point(op.b);
        uint8_t *from = op.b % 23 == 0 ? nullptr : PointAt(which, i);
        const float radius = radii[op.c % 10];
        S_U16Vector out;
        if (op.d % 4 == 0)                                // (appended to)
            FillU16(out, {7, 8});
        void *arg = op.d % 13 ? out.Arg() : nullptr;
        bool r = false;
        static const char *const names[] = {"GetNeighbours", "GetReachable", "GetReachableCopy", "GetNeighbourCover", "GetReachableCover",
                                            "GetReachableDepthFirst"};
        using T_ByIndex = bool(TC *)(void *, int, float, void *);
        using T_ByPoint = bool(TC *)(void *, const void *, float, void *);
        std::vector<uint16_t> *out16 = arg ? &out.port : nullptr;
        const S_Checkpoint *fp = reinterpret_cast<const S_Checkpoint *>(from);
        switch (kind) {
        case 0: r = o ? OriginalAt<T_ByIndex>(O_CS_NEIGHBOURS)(n, i, radius, arg) : p->GetNeighbours(i, radius, out16); break;
        case 1: r = o ? OriginalAt<T_ByPoint>(O_CS_REACHABLE)(n, from, radius, arg) : p->GetReachable(fp, radius, out16); break;
        case 2: r = o ? OriginalAt<T_ByPoint>(O_CS_REACHABLE_COPY)(n, from, radius, arg) : p->GetReachableCopy(fp, radius, out16); break;
        case 3: r = o ? OriginalAt<T_ByIndex>(O_CS_NEIGHBOUR_COVER)(n, i, radius, arg) : p->GetNeighbourCover(i, radius, out16); break;
        case 4: r = o ? OriginalAt<T_ByPoint>(O_CS_REACHABLE_COVER)(n, from, radius, arg) : p->GetReachableCover(fp, radius, out16); break;
        default: r = o ? OriginalAt<T_ByPoint>(O_CS_DEPTH_FIRST)(n, from, radius, arg) : p->GetReachableDepthFirst(fp, radius, out16); break;
        }
        const std::string what = kind == 0 || kind == 3 ? F("%d", i) : ptName(from);
        return F("%s %d %s ", names[kind], which, what.c_str()) + Fl(radius) + F(" -> %d", int(r)) + out.Text();
    }
    case CS_TYPE12: {
        const S_vector pos = PlaceNear(which, op.a, op.b);
        const float radius = radii[op.c % 10];
        S_U16Vector out;
        void *arg = op.d % 13 ? out.Arg() : nullptr;
        const bool r = o ? OriginalAt<bool(TC *)(void *, const S_vector *, float, void *)>(O_CS_TYPE12)(n, &pos, radius, arg)
                         : p->GetReachableType12(pos, radius, arg ? &out.port : nullptr);
        return F("GetReachableType12 %d ", which) + VectorText(Ptr(&pos), 3) + " " + Fl(radius) + F(" -> %d", int(r)) + out.Text();
    }
    case CS_FREE: {
        // (Points about it under cars now and then.)
        uint8_t *from = op.a % 17 == 0 ? nullptr : PointAt(which, point(op.b));
        if (from && v.count && op.c % 2) {
            for (int k = 0; k < 6; ++k) {
                uint8_t *q = PointAt(which, (op.b / 16 + k * (op.c % 7 + 1)) % v.count);
                Mut<uint16_t>(q, 0x14) ^= CHECKPOINT_FLAG_UNDER_CAR;
            }
        }
        const int r = o ? OriginalAt<int(TC *)(void *, const void *)>(O_CS_NEAREST_FREE)(n, from) : p->FindNearestFree(reinterpret_cast<const S_Checkpoint *>(from));
        return F("FindNearestFree %d %s -> %d", which, ptName(from).c_str(), r & 0xFFFF);
    }
    case CS_COVER_PICK: {
        // Candidates: the points within a radius of the place (cover points among them mostly), others, repeated, past the
        // network; every flag.
        const S_vector self = PlaceNear(which, op.a, op.b), enemy = PlaceNear(which, op.b * 7 + 3, op.c);
        std::vector<uint16_t> candidates;
        const int count = op.d % 12;
        for (int k = 0; k < count; ++k)
            candidates.push_back(uint16_t(point((op.a >> (k % 8)) + k * 37)));
        S_U16Vector list;
        if (op.c % 9)
            FillU16(list, candidates);
        static const uint32_t flags[] = {0, COVER_PICK_FROM_SELF, COVER_PICK_FARTHEST, 0x100, 0x200, 0x400, 0x2000, 0x4000,
                                         COVER_PICK_DROP_REJECTED | 0x100, COVER_PICK_NOT_FARTHER, 0x40004104, 0xC0004102, 0x80000005,
                                         0x40002200, 0x40000401};
        const uint32_t f = flags[op.b % 15] | (op.a % 5 == 0 ? COVER_PICK_FROM_SELF : 0);
        void *arg = op.c % 9 ? list.Arg() : nullptr;
        const int r = o ? OriginalAt<int(TC *)(void *, const S_vector *, const S_vector *, void *, uint32_t)>(O_CS_PICK_COVER)(n, &self, &enemy, arg, f)
                        : p->PickCoverPoint(self, enemy, arg ? &list.port : nullptr, f);
        return F("PickCoverPoint %d ", which) + VectorText(Ptr(&self), 3) + " " + VectorText(Ptr(&enemy), 3) + F(" %x -> %d", f, r & 0xFFFF) +
               list.Text();
    }
    case CS_PATH:
    case CS_TRACKER_PATH: {
        // From a point to another (now and then itself, a neighbour, past the network), some flags excluded.
        const int from = point(op.a);
        int to = point(op.b);
        if (op.c % 6 == 0)
            to = from;
        else if (op.c % 6 == 1 && v.count) {
            const uint8_t *q = PointAt(which, from);
            if (q && (reinterpret_cast<const S_Checkpoint *>(q)->flags & 0x3F))
                to = reinterpret_cast<const S_Checkpoint *>(q)->links[0].target;
        }
        static const uint32_t excludes[] = {0, 0x200, 0x2000, 0x100, 0x40, 0xFFFFFFFF, 0x2200, 0x4000};
        const uint32_t exclude = excludes[op.d % 8];
        S_U16Vector path;
        if (op.d % 5 == 0)
            FillU16(path, {1, 2, 3});
        void *arg = op.d % 11 ? path.Arg() : nullptr;
        const bool tracker = op.kind == CS_TRACKER_PATH;
        const bool r = tracker ? (o ? OriginalAt<bool(TC *)(void *, int, int, void *)>(O_CS_TRACKER_PATH)(n, from, to, arg)
                                    : p->FindMeleeTrackerPath(from, to, arg ? &path.port : nullptr))
                               : (o ? OriginalAt<bool(TC *)(void *, int, int, void *, uint32_t)>(O_CS_FIND_PATH)(n, from, to, arg, exclude)
                                    : p->FindPath(from, to, arg ? &path.port : nullptr, exclude));
        return F("%s %d %d %d %x -> %d", tracker ? "FindMeleeTrackerPath" : "FindPath", which, from, to, tracker ? 0 : exclude, int(r)) + path.Text();
    }
    default: {
        // Threats about the start (near, far, one on it), the minimum and the safe distances.
        const int from = point(op.a);
        S_Vector4Array threats;
        const int count = op.b % 5;
        threats.data = static_cast<S_vector4 *>(_aligned_malloc(16 * size_t(count + 1), 16));
        threats.capacity = count + 1;
        threats.count = count;
        for (int k = 0; k < count; ++k) {
            const S_vector at = PlaceNear(which, op.c + 101 * k, op.b + 37 * k);
            threats.data[k] = {at.x, at.y, at.z, 1.0f};
        }
        static const float mins[] = {0.0f, 1.0f, 3.0f, 0.5f, 10.0f};
        static const float safes[] = {5.0f, 10.0f, 20.0f, 2.0f, 40.0f, 0.0f};
        const float minDist = mins[op.c % 5], safeDist = safes[(op.c / 5) % 6];
        static const uint32_t excludes[] = {0, 0x200, 0x2000, 0x100};
        const uint32_t exclude = excludes[op.d % 4];
        S_U16Vector path;
        if (op.d % 3 == 0)
            FillU16(path, {4, 5});
        const bool r = o ? OriginalAt<bool(TC *)(void *, int, const S_Vector4Array *, float, float, void *, uint32_t)>(O_CS_ESCAPE)(
                               n, from, &threats, minDist, safeDist, path.Arg(), exclude)
                         : p->FindEscapePath(from, threats, minDist, safeDist, path.port, exclude);
        std::string t;
        for (int k = 0; k < count; ++k)
            t += " " + VectorText(Ptr(&threats.data[k]), 3);
        _aligned_free(threats.data);
        return F("FindEscapePath %d %d", which, from) + t + " " + Fl(minDist) + " " + Fl(safeDist) + F(" %x -> %d", exclude, int(r)) + path.Text();
    }
    }
}

S_CnCase RandomCsCase()
{
    // cn_diff's case, its operations mostly turned into this part's.
    S_CnCase c = RandomCnCase();
    for (S_CnOp &op : c.ops) {
        if (P(0.3))
            continue;
        const int r = I(0, 99);
        op.kind = r < 25 ? CS_REACH : r < 32 ? CS_TYPE12 : r < 40 ? CS_FREE : r < 55 ? CS_COVER_PICK : r < 78 ? CS_PATH
                : r < 88 ? CS_TRACKER_PATH : CS_ESCAPE;
    }
    return c;
}

void SetUpSearch()
{
    g_envPrefix = "HD2_CS_";
    g_linksInRange = true;
    SetUpNetwork();
    g_runOp = RunCsOp;
    g_opName = [](int kind) { return kind < OP_KINDS ? OP_KIND_NAMES[kind] : CS_NAMES[kind - OP_KINDS]; };
    // The original's node pools: their static initializers (the flood's, the searches'), which its copy here never ran.
    OriginalAt<void (*)()>(0x00520410)();
    OriginalAt<void (*)()>(0x00520f20)();
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("cs_diff: main");
    std::fflush(stdout);
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("cs_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_CS_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_CS_SHOW"))
        g_cnShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_CS_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_CS_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpSearch();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunCnCase(RandomCsCase());
    std::string ops;
    for (int i = 0; i < CS_END; ++i)
        ops += F("%s%s %d", i ? ", " : "", g_opName(i), g_opCounts[i]);
    std::printf("cs_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_cnCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
