// Checkpoint network part 1: the checkpoint networks (game/checkpoint_network.cpp, the original's 0x0051c7b0..0x005230d0),
// their points (game/checkpoint.cpp, 0x005245c0..0x00524b50, 0x0045acf0) and the pair of them with the data's loader
// (game/checkpoints.cpp, 0x00523ab0..0x005241a0) against the original executable's. cmd_diff.cpp's world, fakes and
// call logger are included (CMD_DIFF_NO_MAIN); the networks' calls go through cn_calls.s / cn_calls.inc
// (gen_cmd_calls.py cn): the scene's frames found by name.
//
// Each case builds the same world on each side (cmd_diff's), makes a C_Checkpoints on each side (the original's by its
// constructor 0x00523ab0 in a block of 0x78, the port's by its own) and loads both networks from the same data through
// each side's LoadCheckpointNetwork: a shipped mission's check2.bin (H:\HD2_Unpacked\MISSIONS), or a small network the
// test writes (points in and out of their cells' order, links, spare and joined links, names of 16 characters, frames
// and actors by name, headers of every version, wrong sizes). Then the same random sequence of operations runs on each
// side: Get, Find, FindByPrefix, the actors' and the tables' lookups, CollectReachableWithCosts, joins and separations by
// point and by name, ResetState, the saved game's calls (the networks' and the pair's), FindNearest / FindNearestPoint /
// FindNearestType15 and MarkWithin about the points (on them, near, far, out of range, huge), FindCoverWithin,
// GetPathLength, Clear and a reload, the points' own methods (links, costs, joined links, cover, value pairs, cells,
// direction bits and vectors). After each operation the answer and the calls are compared, and the networks' state:
// a digest of every point (all fields, the links in the storage), of the tables and the joins.
// MinGW; build_cn.bat; runs cn_diff.exe ("cn_diff: N checks, 0 failures"). Env: HD2_CN_CASES (default 1,500;
// HD2_CN_QUICK: 300), HD2_CN_SEED, HD2_CN_SHOW (1: the port's lines; 2: both), HD2_CN_ONLY (the case shown),
// HD2_CN_TRACE (a case's operations as they start), HD2_CN_FULL (every point's fields, not their digests),
// HD2_UNPACKED (the game data; default H:\HD2_Unpacked).
#ifndef CN_HARNESS
#define HARNESS_NAME "cn_diff"
#define CMD_CALLS_INC "cn_calls.inc"
#endif
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <dirent.h>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/checkpoints.h"
#undef protected
#undef private

// ---- the port's side of what the networks call and the test provides ----
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
// The port's allocations zeroed, as the original's (its operator new a zeroing one here).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }

namespace H {

int __cdecl O_Atexit(void *) { return 0; }
uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}
std::string Fl(float f) { return FloatText(f); }

// ---- the original's functions ----
constexpr uint32_t O_CK_CTOR = 0x00523ab0, O_CK_DTOR = 0x00523b20, O_CK_CLEAR = 0x00523e10, O_CK_RESET = 0x00523e30,
                   O_CK_SAVE = 0x00523f30, O_CK_LOAD = 0x005241a0, O_LOAD_NETWORK = 0x00523c00;
constexpr uint32_t O_CN_CLEAR = 0x0051dd60, O_CN_LOAD = 0x0051de80, O_CN_GET = 0x0051d5a0, O_CN_FIND = 0x0051e1a0,
                   O_CN_PREFIX = 0x0051c9c0, O_CN_ACTOR = 0x0051e520, O_CN_ACTOR_POINT = 0x0051e690,
                   O_CN_ACTOR_POINTS = 0x0051e6d0, O_CN_NAME_ENTRY = 0x0051e150, O_CN_NAME_OF = 0x0051e2c0,
                   O_CN_FRAME_OF = 0x0051e450, O_CN_ACTOR_OF = 0x0051e5e0, O_CN_COLLECT = 0x0051ff20,
                   O_CN_JOIN_POINTS = 0x0051f280, O_CN_JOIN = 0x0051f490, O_CN_SEPARATE_POINTS = 0x0051f4d0,
                   O_CN_SEPARATE = 0x0051f5a0, O_CN_RESET = 0x0051f230, O_CN_SAVE = 0x00522a30, O_CN_LOAD_STATE = 0x00522b60,
                   O_CN_NEAREST_POINT = 0x0051d5c0, O_CN_NEAREST = 0x0051d780, O_CN_NEAREST15 = 0x0051d970,
                   O_CN_MARK = 0x0051dbd0, O_CN_COVER = 0x0051f830, O_CN_PATH_LENGTH = 0x005230d0;
constexpr uint32_t O_CP_GET_LINK = 0x00524780, O_CP_FIND_LINK = 0x005247a0, O_CP_LINK_COST = 0x005247e0, O_CP_CLEAR = 0x00524600,
                   O_CP_CLEAR_LINKS = 0x005245e0, O_CP_RESET = 0x00524870, O_CP_ADD_JOINED = 0x005248c0,
                   O_CP_REMOVE_JOINED = 0x005249a0, O_CP_COVER_MASK = 0x00524b10, O_CP_COVER_STANCES = 0x0045acf0,
                   O_CP_VALUE_PAIR = 0x00524b50, O_CP_CELL = 0x00524750, O_CP_DIRECTION_BIT = 0x00524a30,
                   O_CP_DIRECTION_VECTOR = 0x005245c0, O_DIRECTION_VECTORS = 0x008aad10;
#define FC __attribute__((fastcall))

// ---- the data: the shipped missions' networks ----
std::vector<std::pair<std::string, std::string>> g_missions;   // (name, check2.bin)
void LoadMissions()
{
    const char *data = std::getenv("HD2_UNPACKED");
    const std::string dir = std::string(data ? data : "H:\\HD2_Unpacked") + "\\MISSIONS";
    DIR *d = opendir(dir.c_str());
    if (!d) {
        std::printf("cn_diff: no missions in %s\n", dir.c_str());
        std::exit(1);
    }
    std::vector<std::string> names;
    while (dirent *e = readdir(d))
        if (e->d_name[0] != '.')
            names.push_back(e->d_name);
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        FILE *f = std::fopen((dir + "\\" + n + "\\check2.bin").c_str(), "rb");
        if (!f)
            continue;
        std::string bytes;
        char buf[65536];
        for (size_t got; (got = std::fread(buf, 1, sizeof buf, f)) > 0;)
            bytes.append(buf, got);
        std::fclose(f);
        g_missions.push_back({n, bytes});
    }
    if (g_missions.empty()) {
        std::printf("cn_diff: no check2.bin under %s\n", dir.c_str());
        std::exit(1);
    }
}

// ---- the networks on each side ----
uint8_t *g_pair[2];                                     // the C_Checkpoints
C_Checkpoints *PortPair() { return reinterpret_cast<C_Checkpoints *>(g_pair[0]); }
// The network `which` (0 walking, 1 roads) of the current side.
uint8_t *Net(int which)
{
    if (g_side)
        return g_pair[1] + (which ? 0x3C : 0);
    return which ? reinterpret_cast<uint8_t *>(PortPair()->GetRoadNetwork()) : g_pair[0];
}
C_CheckpointNetwork *PortNet(int which) { return reinterpret_cast<C_CheckpointNetwork *>(Net(which)); }
// A network's fields in either side's layout (the points and the tables have one layout).
struct S_NetView {
    int count = 0;
    uint8_t *points = nullptr;
    uint8_t *storage = nullptr;
    int nameCount = 0, frameCount = 0, actorCount = 0;
    uint8_t *names = nullptr, *frames = nullptr, *actors = nullptr;
    bool costs = false, prev = false;
    std::vector<uint32_t> joins;
};
S_NetView View(int which)
{
    S_NetView v;
    uint8_t *n = Net(which);
    if (g_side) {
        v.count = At<uint16_t>(n, 0);
        v.points = reinterpret_cast<uint8_t *>(At<uint32_t>(n, 4));
        v.storage = reinterpret_cast<uint8_t *>(At<uint32_t>(n, 8));
        v.nameCount = At<uint16_t>(n, 0xC);
        v.names = reinterpret_cast<uint8_t *>(At<uint32_t>(n, 0x10));
        v.frameCount = At<uint16_t>(n, 0x14);
        v.frames = reinterpret_cast<uint8_t *>(At<uint32_t>(n, 0x18));
        v.actorCount = At<uint16_t>(n, 0x1C);
        v.actors = reinterpret_cast<uint8_t *>(At<uint32_t>(n, 0x20));
        v.costs = At<uint32_t>(n, 0x24) != 0;
        v.prev = At<uint32_t>(n, 0x28) != 0;
        for (uint32_t p = At<uint32_t>(n, 0x30); p && p < At<uint32_t>(n, 0x34); p += 4)
            v.joins.push_back(*reinterpret_cast<const uint32_t *>(p));
        return v;
    }
    const C_CheckpointNetwork *p = PortNet(which);
    v.count = p->m_pointCount;
    v.points = reinterpret_cast<uint8_t *>(p->m_points);
    v.storage = reinterpret_cast<uint8_t *>(p->m_linkStorage);
    v.nameCount = p->m_nameCount;
    v.names = reinterpret_cast<uint8_t *>(p->m_names);
    v.frameCount = p->m_frameCount;
    v.frames = reinterpret_cast<uint8_t *>(p->m_frames);
    v.actorCount = p->m_actorCount;
    v.actors = reinterpret_cast<uint8_t *>(p->m_actors);
    v.costs = p->m_searchCosts != nullptr;
    v.prev = p->m_searchPrev != nullptr;
    v.joins = p->m_joins;
    return v;
}
uint8_t *PointAt(int which, int i)
{
    const S_NetView v = View(which);
    return i >= 0 && i < v.count ? v.points + 0x24 * i : nullptr;
}

// A point's fields (its links where they are in the storage, by index there).
std::string PointText(const S_NetView &v, int i)
{
    const uint8_t *p = v.points + 0x24 * i;
    const S_Checkpoint &c = *reinterpret_cast<const S_Checkpoint *>(p);
    std::string links;
    const uint8_t *l = reinterpret_cast<const uint8_t *>(c.links);
    if (!l)
        links = "none";
    else if (v.storage && l >= v.storage)
        links = F("@%d", int((l - v.storage) / 4));
    else
        links = "other";
    // (The spare slot too while it is free; a joined link in it is counted.)
    const int shown = (c.flags & CHECKPOINT_LINK_COUNT) + ((c.flags & CHECKPOINT_SPARE_LINK) && !(c.flags & CHECKPOINT_JOINED) ? 1 : 0);
    for (int k = 0; l && k < shown && k < 0x41; ++k)
        links += F(" %d:%d", int(c.links[k].target), int(c.links[k].cost));
    return F("p%d ", i) + VectorText(Ptr(&c.pos), 3) +
           F(" cell %d u10 %d pref %d r %d flags %x type %d cover %d %d %d u1a %d values %d %d %d %d u1f %d links ", c.cell,
             int(c.unknown10), int(c.preference), int(c.radius), int(c.flags), int(c.type), int(c.cover1), int(c.cover2),
             int(c.cover4), int(c.unknown1A), int(c.values[0]), int(c.values[1]), int(c.values[2]), int(c.values[3]),
             int(c.unknown1F)) +
           links;
}
uint32_t Fnv(const std::string &s, uint32_t h = 2166136261u)
{
    for (unsigned char ch : s)
        h = (h ^ ch) * 16777619u;
    return h;
}
bool g_full;
// The network's state: its counts, the points' digests (64 a line), the tables', the joins.
std::vector<std::string> NetState(int which)
{
    std::vector<std::string> out;
    const S_NetView v = View(which);
    out.push_back(F("net%d points %d names %d frames %d actors %d costs %d prev %d joins", which, v.count, v.nameCount, v.frameCount,
                    v.actorCount, int(v.costs), int(v.prev)) +
                  [&] {
                      std::string s;
                      for (uint32_t j : v.joins)
                          s += F(" %d-%d", int(j & 0xFFFF), int(j >> 16));
                      return s;
                  }());
    // (A point's bytes but the links' pointer, its links' place in the storage and the links shown by PointText.)
    auto fnv = [](const void *p, size_t n, uint32_t h) {
        for (size_t i = 0; i < n; ++i)
            h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
        return h;
    };
    for (int i = 0; i < v.count; i += 64) {
        uint32_t h = 2166136261u;
        for (int k = i; k < v.count && k < i + 64; ++k) {
            if (g_full)
                out.push_back("  " + PointText(v, k));
            const S_Checkpoint &c = *reinterpret_cast<const S_Checkpoint *>(v.points + 0x24 * k);
            h = fnv(&c, 0x20, h);
            const uint8_t *l = reinterpret_cast<const uint8_t *>(c.links);
            const int32_t at = !l ? -1 : v.storage && l >= v.storage ? int32_t((l - v.storage) / 4) : -2;
            h = fnv(&at, 4, h);
            const int shown = (c.flags & CHECKPOINT_LINK_COUNT) + ((c.flags & CHECKPOINT_SPARE_LINK) && !(c.flags & CHECKPOINT_JOINED) ? 1 : 0);
            if (l)
                h = fnv(l, 4 * size_t(std::min(shown, 0x41)), h);
        }
        out.push_back(F("  points %d.. %08x", i, h));
    }
    uint32_t h = 2166136261u;
    for (int i = 0; i < v.nameCount; ++i)
        h = Fnv(std::string(reinterpret_cast<const char *>(v.names + 0x12 * i), 16) + F(" %d", int(At<uint16_t>(v.names, 0x12 * i + 0x10))), h);
    for (int i = 0; i < v.frameCount; ++i)
        h = Fnv(NameOf(At<uint32_t>(v.frames, 8 * i)) + F(" %d", int(At<uint16_t>(v.frames, 8 * i + 4))), h);
    for (int i = 0; i < v.actorCount; ++i)
        h = Fnv(NameOf(At<uint32_t>(v.actors, 8 * i)) + F(" %d", int(At<uint16_t>(v.actors, 8 * i + 4))), h);
    out.push_back(F("  tables %08x", h));
    return out;
}

// ---- the scene's frames by name (the frames and actors tables) ----
std::map<std::string, uint8_t *> g_byName[2];
uint8_t *g_sceneActorVtable;
uint32_t NameHash(const char *s) { return Fnv(s ? s : ""); }
void SetUpNetworkAnswers()
{
    // The driver's FindFrame (the models first): now and then the frame; the scene's: a frame made for the name, with an
    // actor for most (none for one name in eight, no actor for one in four).
    auto frameFor = [](const char *name) -> uint32_t {
        const uint32_t h = NameHash(name);
        if (!name || h % 8 == 0)
            return 0u;
        uint8_t *&f = g_byName[g_side][name];
        if (!f) {
            f = NewFrame(FRAME_MODEL, std::string("net ") + name);
            AsFrame(f)->m_name = W().frames.back().name.c_str();
            if (h % 4 != 1) {
                // (Doors for most: their type and motion, 0..3, at each side's place; the meleeTracker's search reads
                // them.)
                uint8_t *a = Block(std::max(sizeof(C_Door), size_t(0x300)) + 0x40);
                Name(a, std::string("actor ") + name);
                const int type = (h >> 4) % 4 ? ACTOR_DOOR : ACTOR_RADIO;
                const int motion = int((h >> 8) % 4);
                if (g_side) {
                    Mut<int>(a, 0x18) = type;
                    Mut<int>(a, 0x224) = motion;
                } else {
                    reinterpret_cast<C_Actor *>(a)->m_type = E_ActorType(type);
                    reinterpret_cast<C_Door *>(a)->m_motion = motion;
                }
                S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
                data->actor = reinterpret_cast<C_Actor *>(a);
                AsFrame(f)->m_data = data;
            }
        }
        return Ptr(f);
    };
    g_answers["I3D_driver::FindFrame"] = [frameFor](const std::vector<uint32_t> &a) {
        return NameHash(reinterpret_cast<const char *>(a[2])) % 5 == 0 ? frameFor(reinterpret_cast<const char *>(a[2])) : 0u;
    };
    g_answers["I3D_scene::FindFrame"] = [frameFor](const std::vector<uint32_t> &a) { return frameFor(reinterpret_cast<const char *>(a[1])); };
}

// ---- the networks the test writes (version 9 data) ----
struct S_Bytes {
    std::string s;
    template <class T> void Put(const T &v) { s.append(reinterpret_cast<const char *>(&v), sizeof v); }
};
const char *const NET_NAMES[] = {"door1", "Door1", "kp_01", "kp_02", "kp_10", "KP_11", "sniper", "cover_a", "abcdefghijklmnop",
                                 "abcdefghijklmnoq", "x", "kp", "longname_exactly"};
constexpr int NET_NAME_COUNT = int(sizeof NET_NAMES / sizeof *NET_NAMES);
// (cs_diff: no links past the network; the searches read through them, past the points, on both sides.)
bool g_linksInRange;
const char *const NET_FRAMES[] = {"dvere1", "dvere2", "gate", "door_a", "door_b", "mg_post", "radio1", ""};
std::string MadeNetwork(int &kind)
{
    // kind: 0 a network, 1 empty, 2 a header of another version, 3 a wrong size, 4 not checkpoint data.
    const int r = I(0, 99);
    kind = r < 80 ? 0 : r < 87 ? 1 : r < 94 ? 2 : r < 97 ? 3 : 4;
    S_Bytes body;
    int points = kind == 1 ? 0 : I(1, 40), linkSlots = 0, names = 0, frames = 0, actors = 0;
    if (kind != 4) {
        // Points by cell (10 m columns of x), then z; one case in six out of order.
        struct S_P {
            float x, y, z;
            int cell;
        };
        std::vector<S_P> ps;
        const bool spread = P(0.5);
        for (int i = 0; i < points; ++i) {
            S_P p;
            p.x = spread ? float(I(-400, 400)) * 0.25f : float(I(-60, 60)) * 0.25f;
            p.y = float(I(-8, 8)) * 0.5f;
            p.z = float(I(-200, 200)) * 0.25f;
            if (P(0.1))
                p.x = float(I(-4, 4)) * 10.0f;          // (on a cell's side)
            p.cell = S_Checkpoint::GetCell(p.x);
            ps.push_back(p);
        }
        if (P(0.83))
            std::sort(ps.begin(), ps.end(), [](const S_P &a, const S_P &b) { return a.cell != b.cell ? a.cell < b.cell : a.z < b.z; });
        if (P(0.3) && points > 2)
            ps[1].z = ps[0].z, ps[1].cell = ps[0].cell, ps[1].x = ps[0].x + 0.5f;   // (two at one z)
        for (int i = 0; i < points; ++i) {
            const S_P &p = ps[size_t(i)];
            body.Put(p.x);
            body.Put(p.y);
            body.Put(p.z);
            body.Put(P(0.05) ? p.cell + 1 : p.cell);
            body.Put(uint16_t(I(0, 3)));
            body.Put(uint8_t(P(0.7) ? 0 : I(0, 255)));                  // preference
            static const int radii[] = {0, 8, 16, 32, 64, 255};
            body.Put(uint8_t(radii[I(0, 5)]));
            const int links = P(0.1) ? 0 : I(1, 6);
            const bool spare = P(0.4), joined = spare && P(0.15);
            uint16_t flags = uint16_t(links | (spare ? 0x40 : 0) | (joined ? 0x80 : 0) | (P(0.2) ? 0x100 : 0) | (P(0.15) ? 0x200 : 0) |
                                      (P(0.1) ? 0x2000 : 0));
            body.Put(flags);
            static const int types[] = {0, 0, 0, 1, 2, 3, 7, 10, 11, 15, 15};
            body.Put(uint8_t(types[I(0, 10)]));
            for (int c = 0; c < 3; ++c)
                body.Put(uint8_t(P(0.5) ? 0 : I(0, 255)));
            body.Put(uint8_t(I(0, 255)));
            for (int c = 0; c < 4; ++c)
                body.Put(uint8_t(P(0.4) ? 0xFF : I(0, 254)));
            body.Put(uint8_t(I(0, 3)));
            for (int k = 0; k < links; ++k) {
                body.Put(uint16_t(P(0.05) && !g_linksInRange ? I(points, points + 5) : I(0, points - 1)));
                body.Put(uint16_t(I(0, 20000)));
            }
            linkSlots += links + (spare ? 1 : 0);
        }
        if (P(0.2))
            linkSlots += I(1, 3);                       // (spare storage at the end)
        names = P(0.15) ? 0 : I(1, 8);
        for (int i = 0; i < names; ++i) {
            body.Put(uint16_t(P(0.1) ? 9999 : I(0, std::max(points - 1, 0))));
            char name[16] = {};
            const char *n = NET_NAMES[I(0, NET_NAME_COUNT - 1)];
            std::memcpy(name, n, std::min<size_t>(16, std::strlen(n)));
            body.s.append(name, 16);
        }
        // The frames' and actors' tables sorted by point (mostly).
        auto table = [&](int count) {
            std::vector<int> idx(static_cast<size_t>(count));
            for (int &k : idx)
                k = I(0, std::max(points - 1, 0));
            if (P(0.9))
                std::sort(idx.begin(), idx.end());
            for (int k : idx) {
                body.Put(uint16_t(k));
                const char *n = NET_FRAMES[I(0, 7)];
                body.s.append(n, std::strlen(n) + 1);
            }
        };
        frames = P(0.3) ? 0 : I(1, 5);
        table(frames);
        actors = P(0.2) ? 0 : I(1, 6);
        table(actors);
    }
    S_Bytes head;
    static const uint32_t versions[] = {CHECKPOINTS_VERSION_1, CHECKPOINTS_VERSION_2, CHECKPOINTS_VERSION_3, 0x7ED00105, 0x7ED00108,
                                        0x7ED0010A, 0x12345678};
    const uint32_t version = kind == 2 ? versions[I(0, 6)] : CHECKPOINTS_VERSION_9;
    const int headerInts = version == CHECKPOINTS_VERSION_1 ? 5 : version == CHECKPOINTS_VERSION_2 ? 6 : 7;
    // (A wrong size: past the file (the read fails), or with bytes after the tables (the load does not end at it). Not
    // short of them: Load trusts the counts and reads on past the data, on both sides.)
    if (kind == 3 && P(0.5))
        body.s.append(size_t(I(1, 8)), '\x22');
    int size = headerInts * 4 + int(body.s.size());
    if (kind == 3 && body.s.back() != '\x22')
        size += I(1, 8);
    head.Put(version);
    head.Put(int32_t(size));
    const int counts[5] = {points, linkSlots, names, frames, actors};
    for (int i = 0; i < headerInts - 2; ++i)
        head.Put(int32_t(counts[i]));
    std::string data = head.s + body.s;
    if (kind == 4)
        data = data.substr(0, size_t(I(0, 3)));
    // (Size past the data: the read fails before the network is built; a little data after it now and then.)
    if (P(0.2))
        data.append(size_t(I(1, 12)), '\x11');
    return data;
}

// ---- one case ----
enum { OP_GET, OP_FIND, OP_PREFIX, OP_ACTOR, OP_ACTOR_POINT, OP_ACTOR_POINTS, OP_ENTRIES, OP_COLLECT, OP_JOIN, OP_SEPARATE,
       OP_RESET, OP_SAVE_LOAD, OP_NEAREST, OP_MARK, OP_COVER, OP_PATH_LENGTH, OP_RELOAD, OP_POINT, OP_STATIC, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"Get", "Find", "FindByPrefix", "GetActor", "FindActorPoint", "GetActorPoints", "Entries",
                                     "CollectReachable", "Join", "Separate", "ResetState", "SaveLoad", "Nearest", "MarkWithin",
                                     "FindCoverWithin", "GetPathLength", "Reload", "Point", "Static"};
struct S_CnOp {
    int kind = 0;
    int net = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_CnCase {
    int mission = -1;                                   // (-1: the test's networks)
    std::string walking, roads;                         // (the test's)
    int walkingKind = 0, roadsKind = 0;
    std::vector<S_CnOp> ops;
};
int g_cnCases, g_opCounts[64];
S_CnCase RandomCnCase()
{
    S_CnCase c;
    if (P(0.35))
        c.mission = I(0, int(g_missions.size()) - 1);
    else {
        c.walking = MadeNetwork(c.walkingKind);
        c.roads = MadeNetwork(c.roadsKind);
    }
    const int count = P(0.1) ? I(40, 100) : I(5, 40);
    for (int i = 0; i < count; ++i) {
        S_CnOp op;
        const int r = I(0, 99);
        op.kind = r < 5 ? OP_GET : r < 10 ? OP_FIND : r < 14 ? OP_PREFIX : r < 17 ? OP_ACTOR : r < 20 ? OP_ACTOR_POINT
                : r < 22 ? OP_ACTOR_POINTS : r < 27 ? OP_ENTRIES : r < 32 ? OP_COLLECT : r < 42 ? OP_JOIN : r < 47 ? OP_SEPARATE
                : r < 49 ? OP_RESET : r < 54 ? OP_SAVE_LOAD : r < 70 ? OP_NEAREST : r < 75 ? OP_MARK : r < 78 ? OP_COVER
                : r < 82 ? OP_PATH_LENGTH : r < 84 ? OP_RELOAD : r < 94 ? OP_POINT : OP_STATIC;
        op.net = P(0.8) ? 0 : 1;
        op.a = I(0, 100000);
        op.b = I(0, 100000);
        op.c = I(0, 100000);
        op.d = I(0, 100000);
        c.ops.push_back(op);
    }
    return c;
}

// An original-layout std::vector<uint16_t> (MSVC 6: first +4, last +8, end +0xC) or the port's, read and freed.
struct S_U16Vector {
    uint8_t orig[16] = {};
    std::vector<uint16_t> port;
    void *Arg() { return g_side ? static_cast<void *>(orig) : static_cast<void *>(&port); }
    std::string Text()
    {
        std::string s;
        if (g_side) {
            for (uint32_t p = At<uint32_t>(orig, 4); p && p < At<uint32_t>(orig, 8); p += 2)
                s += F(" %d", int(*reinterpret_cast<const uint16_t *>(p)));
            if (At<uint32_t>(orig, 4))
                O_Delete(reinterpret_cast<void *>(At<uint32_t>(orig, 4)));
            std::memset(orig, 0, sizeof orig);
        } else {
            for (uint16_t q : port)
                s += F(" %d", int(q));
            port.clear();
        }
        return s;
    }
};

// A list of points (an original-layout vector filled with them, its buffer the original's operator new's).
void FillU16(S_U16Vector &v, const std::vector<uint16_t> &points)
{
    if (g_side) {
        uint8_t *buf = static_cast<uint8_t *>(O_New(2 * points.size() + 2));
        if (!points.empty())
            std::memcpy(buf, points.data(), 2 * points.size());
        Mut<uint8_t *>(v.orig, 4) = buf;
        Mut<uint8_t *>(v.orig, 8) = buf + 2 * points.size();
        Mut<uint8_t *>(v.orig, 12) = buf + 2 * points.size();
    } else {
        v.port = points;
    }
}

// A place about the network's points (on one, near, far, out of range).
S_vector PlaceNear(int which, int a, int b)
{
    static const float offsets[] = {0.0f, 0.5f, -0.5f, 2.0f, -3.0f, 6.0f, 12.0f, -15.0f, 0.001f, 30.0f};
    S_vector pos(float(a % 400 - 200) * 0.25f, float(b % 20 - 10) * 0.5f, float(b % 400 - 200) * 0.25f);
    const S_NetView v = View(which);
    if (v.count && a % 5) {
        const S_Checkpoint &p = *reinterpret_cast<const S_Checkpoint *>(v.points + 0x24 * (b % v.count));
        pos = S_vector(p.pos.x + offsets[a % 10], p.pos.y + offsets[(a / 10) % 10] * 0.25f, p.pos.z + offsets[(a / 100) % 10]);
    }
    switch ((a / 1000) % 12) {
    case 0: pos.y = 200000.0f; break;                   // (out of the lookups' range)
    case 1: pos.y = -150000.0f; break;
    case 2: pos.x = float((a / 7) % 9 - 4) * 10.0f; break;   // (on a cell's side)
    case 3: pos.x = a & 1 ? 200000.0f : -150000.0f; break;  // (x past the heights' limit: not a limit of x)
    default: break;
    }
    return pos;
}

using T_CnVoid = void(TC *)(void *);
using T_CnChunk = bool(TC *)(void *, void *);
std::string RunCnOp(const S_CnCase &c, const S_CnOp &op, int index)
{
    const bool o = g_side != 0;
    const int which = op.net;
    uint8_t *n = Net(which);
    C_CheckpointNetwork *p = PortNet(which);
    const S_NetView v = View(which);
    auto ptName = [&](const void *q) -> std::string {
        if (!q)
            return "null";
        const S_NetView w = View(which);
        const uint8_t *b = static_cast<const uint8_t *>(q);
        if (w.points && b >= w.points && b < w.points + 0x24 * w.count && (b - w.points) % 0x24 == 0)
            return F("point %d", int((b - w.points) / 0x24));
        return NameOf(Ptr(q));
    };
    // A name: of the network's table (as it is, another case, cut, longer), or another.
    auto name = [&](int k, std::string &store) -> const char * {
        if (v.nameCount && k % 4) {
            store.assign(reinterpret_cast<const char *>(v.names + 0x12 * ((k / 4) % v.nameCount)), 16);
            store = store.c_str();
            switch ((k / 64) % 5) {
            case 1: for (char &ch : store) ch = char(std::toupper(static_cast<unsigned char>(ch))); break;
            case 2: store = store.substr(0, store.size() / 2); break;
            case 3: store += "z"; break;
            default: break;
            }
        } else {
            static const char *const others[] = {"", "nothing", "KP_01", "abcdefghijklmnopq", "door"};
            store = others[k % 5];
        }
        return store.c_str();
    };
    // A point index: in range mostly, past it, CHECKPOINT_NONE, negative.
    auto point = [&](int k) -> int {
        if (k % 10 == 0)
            return v.count + k % 3;
        if (k % 10 == 1)
            return k % 20 == 1 ? CHECKPOINT_NONE : -1 - k % 3;
        return v.count ? (k / 10) % v.count : 0;
    };
    switch (op.kind) {
    case OP_GET: {
        const int i = point(op.a);
        void *r = o ? OriginalAt<void *(TC *)(void *, int)>(O_CN_GET)(n, i) : p->Get(i);
        return F("Get %d %d -> ", which, i) + ptName(r);
    }
    case OP_FIND: {
        std::string s;
        const char *nm = name(op.a, s);
        const int r = o ? OriginalAt<int(TC *)(void *, const char *)>(O_CN_FIND)(n, nm) : p->Find(nm);
        return F("Find %d %s -> %d", which, nm, r & 0xFFFF);
    }
    case OP_PREFIX: {
        // (Prefixes of up to 16 characters: past a name of 16 the original reads on into the next entry.)
        std::string s;
        std::string prefix = name(op.a, s);
        prefix = prefix.substr(0, std::min<size_t>(prefix.size(), size_t(op.b % 17)));
        S_U16Vector out;
        if (op.c % 3 == 0)                               // (appended to)
            FillU16(out, {9, 9});
        if (o)
            OriginalAt<void(TC *)(void *, const char *, void *)>(O_CN_PREFIX)(n, prefix.c_str(), out.Arg());
        else
            p->FindByPrefix(prefix.c_str(), out.port);
        return F("FindByPrefix %d '%s' ->", which, prefix.c_str()) + out.Text();
    }
    case OP_ACTOR: {
        const int i = op.a % 3 == 0 ? point(op.b) : v.actorCount ? At<uint16_t>(v.actors, 8 * (op.b % v.actorCount) + 4) + op.c % 3 - 1 : 0;
        void *r = o ? OriginalAt<void *(TC *)(void *, int)>(O_CN_ACTOR)(n, i) : p->GetActor(i);
        return F("GetActor %d %d -> ", which, i) + NameOf(Ptr(r));
    }
    case OP_ACTOR_POINT:
    case OP_ACTOR_POINTS: {
        const void *actor = v.actorCount && op.a % 4 ? reinterpret_cast<const void *>(At<uint32_t>(v.actors, 8 * (op.b % v.actorCount)))
                          : op.a % 8 == 0 ? nullptr : static_cast<const void *>(W().decorators);
        if (op.kind == OP_ACTOR_POINT) {
            const int r = o ? OriginalAt<int(TC *)(void *, const void *)>(O_CN_ACTOR_POINT)(n, actor) : p->FindActorPoint(static_cast<const C_Actor *>(actor));
            return F("FindActorPoint %d %s -> %d", which, NameOf(Ptr(actor)).c_str(), r & 0xFFFF);
        }
        S_U16Vector out;
        if (op.c % 3 == 0)                               // (replaced)
            FillU16(out, {9, 9});
        if (o)
            OriginalAt<void(TC *)(void *, const void *, void *)>(O_CN_ACTOR_POINTS)(n, actor, out.Arg());
        else
            p->GetActorPoints(static_cast<const C_Actor *>(actor), out.port);
        return F("GetActorPoints %d %s ->", which, NameOf(Ptr(actor)).c_str()) + out.Text();
    }
    case OP_ENTRIES: {
        std::string s;
        const char *nm = name(op.a, s);
        const uint16_t i = uint16_t(op.b % 3 == 0 ? point(op.c)
                                    : op.b % 3 == 1 && v.frameCount ? At<uint16_t>(v.frames, 8 * (op.c % v.frameCount) + 4)
                                    : v.actorCount ? At<uint16_t>(v.actors, 8 * (op.c % v.actorCount) + 4) : 0);
        using T_Of = int(TC *)(void *, uint32_t);
        const int r0 = o ? OriginalAt<int(TC *)(void *, const char *)>(O_CN_NAME_ENTRY)(n, nm) : p->FindNameEntry(nm);
        const int r1 = o ? OriginalAt<T_Of>(O_CN_NAME_OF)(n, i) : p->FindNameEntryOfPoint(i);
        const int r2 = o ? OriginalAt<T_Of>(O_CN_FRAME_OF)(n, i) : p->FindFrameEntryOfPoint(i);
        const int r3 = o ? OriginalAt<T_Of>(O_CN_ACTOR_OF)(n, i) : p->FindActorEntryOfPoint(i);
        return F("Entries %d %s %d -> %d %d %d %d", which, nm, int(i), r0 & 0xFFFF, r1 & 0xFFFF, r2 & 0xFFFF, r3 & 0xFFFF);
    }
    case OP_COLLECT: {
        uint8_t *from = op.a % 10 == 0 ? nullptr : PointAt(which, point(op.b));
        static const float radii[] = {0.0f, 1.0f, 5.0f, 12.5f, 30.0f, 100.0f, -1.0f, 1e6f};
        const float radius = radii[op.c % 8];
        S_U16Vector out;
        if (o)
            OriginalAt<void(TC *)(void *, void *, float, void *)>(O_CN_COLLECT)(n, from, radius, op.d % 10 ? out.Arg() : nullptr);
        else
            p->CollectReachableWithCosts(reinterpret_cast<const S_Checkpoint *>(from), radius, op.d % 10 ? &out.port : nullptr);
        return F("CollectReachableWithCosts %d %s ", which, ptName(from).c_str()) + Fl(radius) + " ->" + out.Text();
    }
    case OP_JOIN:
    case OP_SEPARATE: {
        const bool join = op.kind == OP_JOIN;
        bool r;
        std::string what;
        if (op.a % 4 == 0) {
            std::string s1, s2;
            const char *a = name(op.b, s1), *b = name(op.c, s2);
            r = o ? OriginalAt<bool(TC *)(void *, const char *, const char *)>(join ? O_CN_JOIN : O_CN_SEPARATE)(n, a, b)
                  : join ? p->Join(a, b) : p->Separate(a, b);
            what = F("%s %s", a, b);
        } else {
            // (Neighbours mostly: a join must cost 0.001..250 m; for a separation, a joined pair now and then.)
            int a = point(op.b), b = point(op.c);
            if (op.a % 4 == 1 && v.count > 1)
                b = (a + 1) % v.count;
            if (!join && op.a % 4 == 2 && !v.joins.empty()) {
                const uint32_t j = v.joins[size_t(op.d) % v.joins.size()];
                a = int(j & 0xFFFF);
                b = int(j >> 16);
                if (op.d & 1)
                    std::swap(a, b);
            }
            r = o ? OriginalAt<bool(TC *)(void *, int, int)>(join ? O_CN_JOIN_POINTS : O_CN_SEPARATE_POINTS)(n, a, b)
                  : join ? p->JoinPoints(a, b) : p->SeparatePoints(a, b);
            what = F("%d %d", a, b);
        }
        return F("%s %d %s -> %d", join ? "Join" : "Separate", which, what.c_str(), int(r));
    }
    case OP_RESET:
        if (op.a % 2) {
            if (o)
                OriginalAt<T_CnVoid>(O_CN_RESET)(n);
            else
                p->ResetState();
            return F("ResetState %d", which);
        }
        if (o)
            OriginalAt<T_CnVoid>(O_CK_RESET)(g_pair[1]);
        else
            PortPair()->ResetState();
        return "C_Checkpoints::ResetState";
    case OP_SAVE_LOAD: {
        // The network's joins (or the pair's chunks) saved, then loaded back now and then (a sibling after them).
        const bool pair = op.a % 2;
        const std::string file = F("cn_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = pair ? (o ? OriginalAt<T_CnChunk>(O_CK_SAVE)(g_pair[1], &save) : PortPair()->SaveGame(save))
                             : (o ? OriginalAt<T_CnChunk>(O_CN_SAVE)(n, &save) : p->SaveState(save));
        if (op.b % 3 == 0) {
            const int value = 7;
            save.WriteChunk(0x77, &value, 4);
        }
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        bool rl = false;
        int depth = -1;
        if (op.c % 4) {
            // (Into this network or the other, after a reset now and then.)
            const int into = op.d % 3 == 0 ? 1 - which : which;
            if (op.d % 5 == 0) {
                if (o)
                    OriginalAt<T_CnVoid>(O_CN_RESET)(Net(into));
                else
                    PortNet(into)->ResetState();
            }
            C_chunk chunk;
            chunk.Open(file.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = pair ? (o ? OriginalAt<T_CnChunk>(O_CK_LOAD)(g_pair[1], &chunk) : PortPair()->LoadGame(chunk))
                      : (o ? OriginalAt<T_CnChunk>(O_CN_LOAD_STATE)(Net(into), &chunk) : PortNet(into)->LoadState(chunk));
            depth = chunk.m_depth;
            chunk.Close();
        }
        return F("%s %d -> %d load -> %d depth %d bytes ", pair ? "SaveGame" : "SaveState", which, int(rs), int(rl), depth) + Hex(bytes);
    }
    case OP_NEAREST: {
        const S_vector pos = PlaceNear(which, op.a, op.b);
        static const float maxes[] = {5.0f, 1.0f, 10.0f, 30.0f, 100.0f, 0.0f, 0.5f, 1000.0f};
        static const float mins[] = {-1.0f, -1.0f, 0.0f, 1.0f, 3.0f, 0.5f, -1.0f, 10.0f};
        const float maxDist = maxes[op.c % 8], minDist = mins[(op.c / 8) % 8];
        const int kind = op.d % 3;
        using T_Near = int(TC *)(void *, float, float, float, float, float);
        static const uint32_t at[] = {O_CN_NEAREST, O_CN_NEAREST_POINT, O_CN_NEAREST15};
        const int r = o ? OriginalAt<T_Near>(at[kind])(n, pos.x, pos.y, pos.z, maxDist, minDist)
                        : kind == 0 ? p->FindNearest(pos, maxDist, minDist)
                        : kind == 1 ? p->FindNearestPoint(pos, maxDist, minDist) : p->FindNearestType15(pos, maxDist, minDist);
        static const char *const names[] = {"FindNearest", "FindNearestPoint", "FindNearestType15"};
        return F("%s %d ", names[kind], which) + VectorText(Ptr(&pos), 3) + " " + Fl(maxDist) + " " + Fl(minDist) + F(" -> %d", r & 0xFFFF);
    }
    case OP_MARK: {
        const S_vector pos = PlaceNear(which, op.a, op.b);
        static const float radii[] = {0.0f, 1.0f, 5.0f, 10.0f, 25.0f, 0.5f, 60.0f};
        const float radius = radii[op.c % 7];
        std::vector<uint8_t> marks(size_t(v.count) + 1, 0x5A);
        if (o)
            OriginalAt<void(TC *)(void *, uint8_t *, float, float, float, float)>(O_CN_MARK)(n, marks.data(), pos.x, pos.y, pos.z, radius);
        else
            p->MarkWithin(marks.data(), pos, radius);
        std::string s;
        for (size_t i = 0; i < marks.size(); ++i)
            if (marks[i] != 0)
                s += F(" %d:%d", int(i), int(marks[i]));
        return F("MarkWithin %d ", which) + VectorText(Ptr(&pos), 3) + " " + Fl(radius) + " ->" + s;
    }
    case OP_COVER: {
        const S_vector pos = PlaceNear(which, op.a, op.b);
        static const float radii[] = {0.0f, 2.0f, 8.0f, 20.0f};
        const float radius = radii[op.c % 4];
        S_U16Vector out;
        const bool r = o ? OriginalAt<bool(TC *)(void *, const S_vector *, float, void *)>(O_CN_COVER)(n, &pos, radius, op.d % 8 ? out.Arg() : nullptr)
                         : p->FindCoverWithin(pos, radius, op.d % 8 ? &out.port : nullptr);
        return F("FindCoverWithin %d ", which) + VectorText(Ptr(&pos), 3) + " " + Fl(radius) + F(" -> %d", int(r)) + out.Text();
    }
    case OP_PATH_LENGTH: {
        // A way along the links (now and then a step that is not one, a point past the network) or a short one.
        std::vector<uint16_t> path;
        int at = point(op.a);
        path.push_back(uint16_t(at));
        const int steps = op.b % 6;
        uint32_t r = uint32_t(op.c);
        for (int k = 0; k < steps; ++k) {
            const uint8_t *q = PointAt(which, at);
            r = r * 1103515245u + 12345u;
            int next = q && (reinterpret_cast<const S_Checkpoint *>(q)->flags & 0x3F) && r % 7 ? reinterpret_cast<const S_Checkpoint *>(q)->links[(r >> 8) % (reinterpret_cast<const S_Checkpoint *>(q)->flags & 0x3F)].target
                                                                                       : int((r >> 8) % uint32_t(v.count + 2));
            path.push_back(uint16_t(next));
            at = next;
        }
        uint8_t vec[16] = {};
        Mut<uint16_t *>(vec, 4) = path.data();
        Mut<uint16_t *>(vec, 8) = path.data() + path.size();
        Mut<uint16_t *>(vec, 12) = path.data() + path.size();
        const float len = o ? OriginalAt<float(TC *)(void *, const void *)>(O_CN_PATH_LENGTH)(n, vec) : p->GetPathLength(path);
        std::string s;
        for (uint16_t q : path)
            s += F(" %d", int(q));
        return F("GetPathLength %d", which) + s + " -> " + Fl(len);
    }
    case OP_RELOAD: {
        // Cleared (the network or the pair), then loaded again now and then.
        if (op.a % 3 == 0) {
            if (o)
                OriginalAt<T_CnVoid>(O_CK_CLEAR)(g_pair[1]);
            else
                PortPair()->Clear();
        } else if (o)
            OriginalAt<T_CnVoid>(O_CN_CLEAR)(n);
        else
            p->Clear();
        int version = -1;
        if (op.b % 2) {
            const std::string file = c.mission >= 0 ? F("cn_mission%d.bin", c.mission) : F("cn_made%d.bin", which);
            C_chunk chunk;
            chunk.Open(file.c_str(), false);
            using T_Load = int(__stdcall *)(void *, void *);
            if (which) {                                // (the roads after the walking network)
                if (o)
                    OriginalAt<T_Load>(O_LOAD_NETWORK)(&chunk, Net(0));
                else
                    LoadCheckpointNetwork(&chunk, PortNet(0));
            }
            version = o ? OriginalAt<T_Load>(O_LOAD_NETWORK)(&chunk, n) : LoadCheckpointNetwork(&chunk, p);
            chunk.Close();
        }
        return F("Reload %d %s -> %x", which, op.a % 3 == 0 ? "pair" : "net", version);
    }
    case OP_POINT: {
        // A point's own methods.
        uint8_t *q = PointAt(which, v.count ? op.a % v.count : 0);
        if (!q)
            return "Point (none)";
        S_Checkpoint *pp = reinterpret_cast<S_Checkpoint *>(q);
        const std::string at = ptName(q);
        switch (op.b % 10) {
        case 0: {
            const int i = op.c % 10 - 2;
            void *r = o ? OriginalAt<void *(TC *)(void *, int)>(O_CP_GET_LINK)(q, i) : pp->GetLink(i);
            const S_NetView w = View(which);
            return F("GetLink %s %d -> ", at.c_str(), i) + (r ? F("@%d", int((static_cast<uint8_t *>(r) - w.storage) / 4)) : std::string("null"));
        }
        case 1: {
            const int16_t t = int16_t(op.c % 3 == 0 ? op.c % (v.count + 3) - 1
                                     : (pp->flags & 0x3F) ? pp->links[op.c % (pp->flags & 0x3F)].target : 0);
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_CP_FIND_LINK)(q, t) : pp->FindLink(t);
            return F("FindLink %s %d -> %d", at.c_str(), int(t), r & 0xFF);
        }
        case 2: {
            uint8_t *to = PointAt(which, v.count ? op.c % v.count : 0);
            const float r = o ? OriginalAt<float(TC *)(void *, const void *)>(O_CP_LINK_COST)(q, to) : pp->GetLinkCost(*reinterpret_cast<S_Checkpoint *>(to));
            return F("GetLinkCost %s %s -> ", at.c_str(), ptName(to).c_str()) + Fl(r);
        }
        case 3: {
            // Joined links by hand: a neighbour, itself, a far one.
            const int t = v.count ? (op.c % 3 == 0 ? op.a % v.count : op.c % 3 == 1 ? (op.a + 1) % v.count : op.c % v.count) : 0;
            uint8_t *to = PointAt(which, t);
            const bool add = op.d % 3 != 0;
            const bool r = add ? (o ? OriginalAt<bool(TC *)(void *, int, const void *)>(O_CP_ADD_JOINED)(q, int16_t(t), to)
                                    : pp->AddJoinedLink(int16_t(t), reinterpret_cast<S_Checkpoint *>(to)))
                               : (o ? OriginalAt<bool(TC *)(void *, int)>(O_CP_REMOVE_JOINED)(q, int16_t(t)) : pp->RemoveJoinedLink(int16_t(t)));
            return F("%s %s %d -> %d", add ? "AddJoinedLink" : "RemoveJoinedLink", at.c_str(), t, int(r));
        }
        case 4: {
            static const int stances[] = {1, 2, 4, 3, 7, 0, 5};
            const int s = stances[op.c % 7];
            const int r = o ? OriginalAt<int(TC *)(void *, int)>(O_CP_COVER_MASK)(q, s) : pp->GetCoverMask(s);
            const uint8_t dir = uint8_t(1u << (op.d % 8)), st = uint8_t(op.d % 8);
            const int r2 = o ? OriginalAt<int(TC *)(void *, int, int)>(O_CP_COVER_STANCES)(q, dir, st) : pp->GetCoverStances(dir, st);
            return F("GetCoverMask %s %d -> %d GetCoverStances %d %d -> %d", at.c_str(), s, r & 0xFF, int(dir), int(st), r2 & 0xFF);
        }
        case 5: {
            const int set = op.c % 3;
            float first = -7.0f, second = -7.0f;
            const bool r = o ? OriginalAt<bool(TC *)(void *, int, float *, float *)>(O_CP_VALUE_PAIR)(q, set, &first, &second)
                             : pp->GetValuePair(set, &first, &second);
            return F("GetValuePair %s %d -> %d ", at.c_str(), set, int(r)) + Fl(first) + " " + Fl(second);
        }
        case 6:
            if (o)
                OriginalAt<T_CnVoid>(O_CP_RESET)(q);
            else
                pp->ResetState();
            return "ResetState " + at;
        case 7:
            // (Only the links' storage is the network's: neither frees it.)
            if (o)
                OriginalAt<T_CnVoid>(op.c % 2 ? O_CP_CLEAR : O_CP_CLEAR_LINKS)(q);
            else if (op.c % 2)
                pp->Clear();
            else
                pp->ClearLinks();
            return (op.c % 2 ? "Clear " : "ClearLinks ") + at;
        case 8: {
            // A point of its own (not the network's) with links of its own (each side's new[]): destroyed, its links
            // cleared, or read over (from this point's bytes, into storage of the test's).
            uint8_t *own = Block(0x24 + 16);
            std::memcpy(own, q, 0x24);
            S_Checkpoint *op_ = reinterpret_cast<S_Checkpoint *>(own);
            const int links = op.c % 4;
            op_->flags = uint16_t((op_->flags & ~(CHECKPOINT_LINK_COUNT | CHECKPOINT_SHARED_LINKS | CHECKPOINT_SPARE_LINK | CHECKPOINT_JOINED)) |
                                  links | (op.d % 3 == 0 ? CHECKPOINT_SHARED_LINKS : 0));
            void *mine = nullptr;
            if (links || op.d % 5) {
                mine = o ? O_New(4 * size_t(links + 1)) : static_cast<void *>(new S_CheckpointLink[size_t(links + 1)]);
                for (int k = 0; k <= links; ++k)
                    static_cast<S_CheckpointLink *>(mine)[k] = {uint16_t(k + 1), uint16_t(100 * k)};
            }
            op_->links = static_cast<S_CheckpointLink *>(mine);
            const int what = (op.c / 4) % 3;
            std::string extra;
            if (what == 0) {
                // (Its fields not compared after: GCC drops the port's destructor's stores. ClearLinks shows them.)
                if (o)
                    OriginalAt<T_CnVoid>(0x00524840)(own);
                else
                    op_->~S_Checkpoint();
                return F("Own %s links %d destroyed", at.c_str(), links);
            } else if (what == 1) {
                if (o)
                    OriginalAt<T_CnVoid>(O_CP_CLEAR_LINKS)(own);
                else
                    op_->ClearLinks();
            } else {
                std::string data(reinterpret_cast<const char *>(q), 0x20);
                const int count = reinterpret_cast<S_Checkpoint *>(q)->flags & CHECKPOINT_LINK_COUNT;
                for (int k = 0; k < count; ++k)
                    data.append(reinterpret_cast<const char *>(&reinterpret_cast<S_Checkpoint *>(q)->links[k]), 4);
                uint8_t *storage = Block(4 * 0x42);
                Name(storage, "own storage");
                S_CheckpointLink *at = reinterpret_cast<S_CheckpointLink *>(storage);
                const uint8_t *end = o ? OriginalAt<const uint8_t *(TC *)(void *, const void *, S_CheckpointLink **)>(0x00524620)(
                                             own, data.data(), &at)
                                       : op_->Read(reinterpret_cast<const uint8_t *>(data.data()), at);
                extra = F(" read %d storage +%d", int(end - reinterpret_cast<const uint8_t *>(data.data())), int((reinterpret_cast<uint8_t *>(at) - storage) / 4));
            }
            const S_Checkpoint &c = *op_;
            return F("Own %s links %d flags %x type %d links %s", at.c_str(), links, int(c.flags), int(c.type),
                     !c.links ? "null" : c.links == mine ? "own" : "storage") +
                   extra;
        }
        default: {
            const S_vector dir = pp->pos;               // (the same bytes on both sides)
            S_vector d(dir.x - float(op.c % 200 - 100) * 0.1f, 0.0f, dir.z - float(op.d % 200 - 100) * 0.1f);
            const int r = o ? OriginalAt<int(FC *)(const S_vector *)>(O_CP_DIRECTION_BIT)(&d) : S_Checkpoint::GetDirectionBit(d);
            return "GetDirectionBit " + VectorText(Ptr(&d), 3) + F(" -> %d", r & 0xFF);
        }
        }
    }
    default: {
        // The static ones: the cell of x (huge ones too), the direction bits of directions and the bits' vectors.
        static const float xs[] = {0.0f, -0.0f, 9.99f, 10.0f, -10.0f, -0.001f, 123.4f, -55.5f, 2.1e8f, -2.2e8f, 3e10f, -3e10f, 1e20f, -1e30f,
                                   1e-30f, 99999.0f};
        const float x = op.a % 3 ? float(op.b % 4001 - 2000) * 0.05f : xs[op.b % 16];
        const int cell = o ? OriginalAt<int(__stdcall *)(float)>(O_CP_CELL)(x) : S_Checkpoint::GetCell(x);
        static const float dirs[][3] = {{0, 0, 1},     {1, 0, 1},       {1, 0, 0},        {1, 0, -1},     {0, 0, -1},   {-1, 0, -1},
                                        {-1, 0, 0},    {-1, 0, 1},      {0, 0, 0},        {1e-9f, 0, 0},  {0.3826834f, 0, 0.9238795f},
                                        {0.9238795f, 0, 0.3826834f},     {-0.3826834f, 0, -0.9238795f},   {0, 5, 0},    {2, 7, -3},
                                        {-0.0f, 0, 0}};
        const float *dd = dirs[op.c % 16];
        const S_vector dir(dd[0], dd[1], dd[2]);
        const int bit = o ? OriginalAt<int(FC *)(const S_vector *)>(O_CP_DIRECTION_BIT)(&dir) : S_Checkpoint::GetDirectionBit(dir);
        const uint8_t bits = uint8_t(op.d % 256);
        const S_vector4 *vec = o ? OriginalAt<const S_vector4 *(FC *)(uint32_t)>(O_CP_DIRECTION_VECTOR)(bits) : S_Checkpoint::GetDirectionVector(bits);
        return "GetCell " + Fl(x) + F(" -> %d GetDirectionBit ", cell) + VectorText(Ptr(&dir), 3) + F(" -> %d GetDirectionVector %d -> ", bit & 0xFF, int(bits)) +
               VectorText(Ptr(vec), 4);
    }
    }
}

// The environment's settings (HD2_CN_*; cs_diff's HD2_CS_*).
const char *g_envPrefix = "HD2_CN_";
const char *Env(const char *name) { return std::getenv((std::string(g_envPrefix) + name).c_str()); }
// (Checkpoint search part 1, cs_diff.cpp: its own operations past these, its names.)
std::function<std::string(const S_CnCase &, const S_CnOp &, int)> g_runOp = RunCnOp;
std::function<const char *(int)> g_opName = [](int kind) { return OP_KIND_NAMES[kind]; };

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("cn_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_cnShow;
void RunCnCase(const S_CnCase &c)
{
    g_caseStart = GetTickCount();
    ++g_cnCases;
    g_holdFrees = true;
    g_current = F("case %d", g_cnCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    if (c.mission >= 0)
        g_overrides[Lower(F("cn_mission%d.bin", c.mission))] = g_missions[size_t(c.mission)].second;
    else {
        // (One file of the two networks: the roads after the walking network, as check2.bin.)
        g_overrides["cn_made0.bin"] = c.walking + c.roads;
        g_overrides["cn_made1.bin"] = c.walking + c.roads;
    }
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_byName[side].clear();
        BuildWorld(side, worldSeed);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        uint8_t *b;
        if (side) {
            b = Block(0x78 + 16);
            OriginalAt<T_CnVoid>(O_CK_CTOR)(b);
        } else {
            b = Block(sizeof(C_Checkpoints) + 16);
            new (b) C_Checkpoints();
        }
        g_pair[side] = b;
        Name(b, "checkpoints");
        Name(Net(1), "roads");
        // Both networks loaded from the data, the walking network first.
        {
            const std::string file = c.mission >= 0 ? F("cn_mission%d.bin", c.mission) : "cn_made0.bin";
            C_chunk chunk;
            chunk.Open(file.c_str(), false);
            using T_Load = int(__stdcall *)(void *, void *);
            const int v0 = side ? OriginalAt<T_Load>(O_LOAD_NETWORK)(&chunk, Net(0)) : LoadCheckpointNetwork(&chunk, PortNet(0));
            const int v1 = side ? OriginalAt<T_Load>(O_LOAD_NETWORK)(&chunk, Net(1)) : LoadCheckpointNetwork(&chunk, PortNet(1));
            chunk.Close();
            out.push_back(F("loaded %s: %x %x", c.mission >= 0 ? g_missions[size_t(c.mission)].first.c_str() : "made", v0, v1));
        }
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (int w = 0; w < 2; ++w)
            for (const std::string &s : NetState(w))
                out.push_back(s);
        static const int trace = Env("TRACE") ? std::atoi(Env("TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_cnCases)
                std::printf("trace %s op %d kind %s net %d a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), g_opName(c.ops[i].kind),
                            c.ops[i].net, c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(g_runOp(c, c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            // (The state after the operations that change it.)
            const int k = c.ops[i].kind;
            if (k == OP_JOIN || k == OP_SEPARATE || k == OP_RESET || k == OP_SAVE_LOAD || k == OP_RELOAD || k == OP_POINT ||
                i + 1 == c.ops.size())
                for (int w = 0; w < 2; ++w)
                    for (const std::string &s : NetState(w))
                        out.push_back("  " + s);
        }
        // The end: destroyed.
        if (side)
            OriginalAt<T_CnVoid>(O_CK_DTOR)(b);
        else
            PortPair()->~C_Checkpoints();
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_pair[side] = nullptr;
    }
    static const int only = Env("ONLY") ? std::atoi(Env("ONLY")) : 0;
    if (g_cnShow && (!only || only == g_cnCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_cnShow == 2)
            for (const std::string &s : r.orig)
                std::printf("  orig: %s\n", s.c_str());
    }
    Compare(g_current, r);
    ReleaseHeldFrees();
    if (!g_overrun.empty()) {
        ++failures;
        std::printf("FAIL %s: the original wrote past a block\n", g_overrun.c_str());
        g_overrun.clear();
    }
    FreeWorld(0);
    FreeWorld(1);
}

void SetUpNetwork()
{
    SetUpCmd();
    SetUpNetworkAnswers();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite)},
        {0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek)},
        {0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite)},
        {0x007e562e, reinterpret_cast<const void *>(&O_Atexit)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    // The original's direction vectors: its static initializers, which its copy here never ran.
    OriginalAt<void (*)()>(0x005244b0)();
    OriginalAt<void (*)()>(0x005244c0)();
    g_full = Env("FULL") != nullptr;
    LoadMissions();
}

}  // namespace H

#ifndef CN_HARNESS
int main()
{
    using namespace H;
    std::puts("cn_diff: main");
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
        std::printf("cn_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_CN_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_CN_SHOW"))
        g_cnShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_CN_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_CN_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpNetwork();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunCnCase(RandomCnCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("cn_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_cnCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
#endif
