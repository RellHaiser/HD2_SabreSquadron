// Sight part 1: the sight module (game/sight.cpp, the original's 0x0046d5d0..0x004702f0) against the original
// executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the module's calls go
// through sg_calls.s / sg_calls.inc (gen_cmd_calls.py sg). The original's module is its global (0x0089b288: its own
// functions read it), built once by its constructor 0x0046ee90 and cleared for each case; the port's is its global.
//
// Each case builds the same world on each side (cmd_diff's humans and meleeTrackers among its actors, their frames given
// the body parts' collision volumes "d", "z" / "f", "i", "m", "q" and "c", "f", "k", "o", some missing, some named in
// capitals; the trackers their body frames), then runs the same random sequence of operations on the module: AddObserver
// and AddTarget (humans', trackers', other actors' frames, a frame without an actor, none; masks 0..16),
// RemoveObserver, RemoveTarget, SetObserverState, SetTargetState, TestNow, CanSee, GetVisibleParts, a target's
// Refresh, its testedBy bits set, ticks (the game's time advancing), the module's mission calls. The view tests answer
// 100, 50 or 0; the rays nothing, a static volume, a dynamic one of a human's vehicle, of the target or of another actor.
// After each operation the answer, its calls and the module (every target's and observer's fields, records and parts,
// the cursors, the start delay, the sight time) are compared.
// MinGW; build_sg.bat; runs sg_diff.exe ("sg_diff: N checks, 0 failures"). Env: HD2_SG_CASES (default 1,500;
// HD2_SG_QUICK: 300), HD2_SG_SEED, HD2_SG_SHOW (1: the port's lines; 2: both), HD2_SG_ONLY, HD2_SG_TRACE.
#define HARNESS_NAME "sg_diff"
#define CMD_CALLS_INC "sg_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "game/collision.h"
#include "game/melee_tracker.h"
#include "game/sight.h"
#undef protected
#undef private

// ---- the port's side of what the module calls and the test provides ----
// The port's allocations zeroed, as the original's (its new redirected to a zeroing one).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
// The module base (its name and id; not logged: the port's global module is made before the logger is).
C_Module::C_Module(int id, const char *name) : m_name(name), m_id(id) {}
C_Module::~C_Module() {}

namespace H {

uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
std::string Fl(float f) { return FloatText(f); }
std::deque<std::string> g_texts[2];
// A world frame under `parent` (its name).
uint8_t *Child(uint8_t *parent, int type, const std::string &name)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_frameFlags |= 1;
    if (parent)
        LinkUnder(b, parent);
    return b;
}

constexpr uint32_t O_SIGHT = 0x0089b288, O_SIGHT_TIME = 0x00839390;
constexpr uint32_t O_SG_CTOR = 0x0046ee90, O_SG_CLEAR = 0x0046ffb0, O_SG_TICK = 0x0046f000, O_SG_LOAD_DATA = 0x0046eee0,
                   O_SG_FREE_DATA = 0x0046eef0, O_SG_START = 0x0046efd0, O_SG_STOP = 0x0046eff0, O_SG_RESTART = 0x00470090,
                   O_SG_ADD_OBSERVER = 0x0046f350, O_SG_ADD_TARGET = 0x0046f9d0, O_SG_REMOVE_OBSERVER = 0x0046fd50,
                   O_SG_REMOVE_TARGET = 0x0046fdf0, O_SG_OBSERVER_STATE = 0x0046f8c0, O_SG_TARGET_STATE = 0x0046f700,
                   O_SG_TEST_NOW = 0x0046f280, O_SG_CAN_SEE = 0x0046f320, O_SG_PARTS = 0x0046f2a0;
uint8_t *OrigModule() { return OriginalAt<uint8_t *>(O_SIGHT); }

// ---- each side's targets and observers (pointers; the original's MSVC vectors) ----
std::vector<uint8_t *> Targets()
{
    std::vector<uint8_t *> out;
    if (g_side) {
        const uint8_t *g = OrigModule();
        for (uint32_t q = At<uint32_t>(g, 0x10); q && q < At<uint32_t>(g, 0x14); q += 4)
            out.push_back(*reinterpret_cast<uint8_t **>(q));
    } else
        for (C_SightTarget *t : g_sightModule.m_targets)
            out.push_back(reinterpret_cast<uint8_t *>(t));
    return out;
}
std::vector<uint8_t *> Observers()
{
    std::vector<uint8_t *> out;
    if (g_side) {
        const uint8_t *g = OrigModule();
        for (uint32_t q = At<uint32_t>(g, 0x20); q && q < At<uint32_t>(g, 0x24); q += 4)
            out.push_back(*reinterpret_cast<uint8_t **>(q));
    } else
        for (C_SightObserver *o : g_sightModule.m_observers)
            out.push_back(reinterpret_cast<uint8_t *>(o));
    return out;
}
uint32_t ObserverMask(const uint8_t *o) { return g_side ? At<uint32_t>(o, 8) : reinterpret_cast<const C_SightObserver *>(o)->mask; }
uint32_t ObserverFrame(const uint8_t *o) { return g_side ? At<uint32_t>(o, 4) : Ptr(reinterpret_cast<const C_SightObserver *>(o)->frame); }
size_t RecordCount(const uint8_t *o)
{
    if (g_side) {
        const uint32_t first = At<uint32_t>(o, 0x18), last = At<uint32_t>(o, 0x1C);
        return first ? (last - first) / 0x68 : 0;
    }
    return reinterpret_cast<const C_SightObserver *>(o)->records.size();
}
const uint8_t *Records(const uint8_t *o)
{
    return g_side ? reinterpret_cast<const uint8_t *>(At<uint32_t>(o, 0x18))
                  : reinterpret_cast<const uint8_t *>(reinterpret_cast<const C_SightObserver *>(o)->records.data());
}
// A target's parts (its frames, null where missing) and kind (0 human, 1 tracker; -1 null).
std::vector<uint32_t> TargetParts(const uint8_t *t)
{
    std::vector<uint32_t> out;
    if (g_side) {
        for (uint32_t q = At<uint32_t>(t, 0x1C); q && q < At<uint32_t>(t, 0x20); q += 4)
            out.push_back(*reinterpret_cast<uint32_t *>(q));
    } else
        for (I3D_frame *f : reinterpret_cast<const C_SightTarget *>(t)->parts)
            out.push_back(Ptr(f));
    return out;
}
uint32_t TargetMask(const uint8_t *t) { return g_side ? At<uint32_t>(t, 8) : reinterpret_cast<const C_SightTarget *>(t)->mask; }
int TargetKind(uint8_t *t)
{
    if (!t)
        return -1;
    if (g_side)
        return reinterpret_cast<int(TC *)(void *)>((*reinterpret_cast<void ***>(t))[1])(t);
    return reinterpret_cast<C_SightTarget *>(t)->GetKind();
}

uint32_t ObserverOwner(const C_SightObserver *x)
{
    if (auto *h = dynamic_cast<const C_HumanSightObserver *>(x))
        return Ptr(h->human);
    if (auto *t = dynamic_cast<const C_MeleeTrackerSightObserver *>(x))
        return Ptr(t->meleeTracker);
    return 0;
}
std::vector<std::string> ModuleState()
{
    std::vector<std::string> out;
    const auto targets = Targets();
    const auto observers = Observers();
    float time;
    if (g_side) {
        const uint8_t *g = OrigModule();
        time = *OriginalAt<float *>(O_SIGHT_TIME);
        out.push_back(F("module cursors %u %u delay %d time ", At<uint32_t>(g, 0x2C), At<uint32_t>(g, 0x30), At<int>(g, 0x34)) + Fl(time));
    } else {
        time = g_sightTime;
        out.push_back(F("module cursors %u %u delay %d time ", unsigned(g_sightModule.m_observerCursor),
                        unsigned(g_sightModule.m_targetCursor), g_sightModule.m_startDelay) + Fl(time));
    }
    for (size_t i = 0; i < targets.size(); ++i) {
        uint8_t *t = targets[i];
        if (!t) {
            out.push_back(F("  target %d null", int(i)));
            continue;
        }
        std::string s;
        if (g_side)
            s = F("  target %d kind %d frame %s mask %x index %d tested by %x observer %d alive %d hidden %d parts %d owner %s", int(i),
                  TargetKind(t), NameOf(At<uint32_t>(t, 4)).c_str(), At<uint32_t>(t, 8), At<int>(t, 0x0C), At<uint32_t>(t, 0x10),
                  At<int>(t, 0x14), int(t[0x28]), int(t[0x29]), At<int>(t, 0x2C), NameOf(At<uint32_t>(t, 0x30)).c_str());
        else {
            const C_SightTarget *x = reinterpret_cast<const C_SightTarget *>(t);
            const uint32_t owner = const_cast<C_SightTarget *>(x)->GetKind() == 0 ? Ptr(static_cast<const C_HumanSightTarget *>(x)->human)
                                                     : Ptr(static_cast<const C_MeleeTrackerSightTarget *>(x)->meleeTracker);
            s = F("  target %d kind %d frame %s mask %x index %d tested by %x observer %d alive %d hidden %d parts %d owner %s", int(i),
                  TargetKind(t), NameOf(Ptr(x->frame)).c_str(), x->mask, x->index, x->testedBy, x->observer, int(x->alive),
                  int(x->hidden), x->partCount, NameOf(owner).c_str());
        }
        s += " :";
        for (uint32_t p : TargetParts(t))
            s += " " + NameOf(p);
        out.push_back(s);
    }
    for (size_t i = 0; i < observers.size(); ++i) {
        uint8_t *o = observers[i];
        if (!o) {
            out.push_back(F("  observer %d null", int(i)));
            continue;
        }
        if (g_side)
            out.push_back(F("  observer %d frame %s mask %x index %d target %d disabled %d owner %s", int(i), NameOf(At<uint32_t>(o, 4)).c_str(),
                            At<uint32_t>(o, 8), At<int>(o, 0x0C), At<int>(o, 0x10), int(o[0x24]), NameOf(At<uint32_t>(o, 0x28)).c_str()));
        else {
            const C_SightObserver *x = reinterpret_cast<const C_SightObserver *>(o);
            out.push_back(F("  observer %d frame %s mask %x index %d target %d disabled %d owner %s", int(i), NameOf(Ptr(x->frame)).c_str(),
                            x->mask, x->index, x->target, int(x->disabled), NameOf(ObserverOwner(x)).c_str()));
        }
        const uint8_t *r = Records(o);
        for (size_t k = 0; k < RecordCount(o); ++k) {
            const uint8_t *x = r + 0x68 * k;
            std::string s = F("    record %d parts %x seen %d before %d visible %d part %d relation %d view", int(k), At<uint32_t>(x, 0),
                              At<int>(x, 4), At<int>(x, 8), int(x[0x0C]), At<int>(x, 0x10), At<int>(x, 0x14));
            for (int p = 0; p < 5; ++p)
                s += " " + Fl(At<float>(x, 0x18 + 4 * p));
            s += " times";
            for (int p = 0; p < 10; ++p)
                s += F(" %u", At<uint32_t>(x, 0x2C + 4 * p));
            out.push_back(s);
        }
    }
    return out;
}

bool g_walls;                                           // (every ray blocked: what was seen is lost)

// ---- the world's additions: the body parts, the trackers' body frames ----
struct S_SgCase;
std::vector<uint8_t *> g_sgFrames[2];                   // (the frames the operations pick from: the actors' and others)
std::vector<uint8_t *> g_humanFrames[2], g_trackerFrames[2];
void AddParts(uint32_t seed)
{
    std::vector<uint8_t *> &frames = g_sgFrames[g_side];
    frames.clear();
    g_humanFrames[g_side].clear();
    g_trackerFrames[g_side].clear();
    uint32_t s = seed;
    auto next = [&] {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    };
    for (const W_Actor &a : W().actors) {
        uint8_t *frame = g_side ? reinterpret_cast<uint8_t *>(At<uint32_t>(a.bytes, 0x20))
                                : reinterpret_cast<uint8_t *>(reinterpret_cast<C_Actor *>(a.bytes)->m_frame);
        if (!frame)
            continue;
        frames.push_back(frame);
        if (a.type == ACTOR_HUMAN)
            g_humanFrames[g_side].push_back(frame);
        else if (a.type == ACTOR_MELEE_TRACKER)
            g_trackerFrames[g_side].push_back(frame);
        if (a.type == ACTOR_HUMAN) {
            for (const char *n : {"d", "z", "f", "i", "m", "q"}) {
                const uint32_t r = next() % 10;
                if (r == 0 || (n[0] == 'f' && r < 5))
                    continue;
                Child(frame, r == 9 ? FRAME_DUMMY : FRAME_VOLUME, r == 8 ? std::string(1, char(n[0] - 32)) : std::string(n));
            }
        } else if (a.type == ACTOR_MELEE_TRACKER) {
            for (const char *n : {"c", "f", "k", "o"})
                if (next() % 8)
                    Child(frame, FRAME_VOLUME, n);
            // Its body volumes (SetIgnoredVolumes), now and then.
            if (next() % 2) {
                uint8_t *v = Block(0x80);
                Name(v, "tracker volume");
                if (g_side)
                    Mut<uint8_t *>(a.bytes, 0x90 + 0x1DC) = v;
                else
                    reinterpret_cast<C_MeleeTracker *>(a.bytes)->m_movement.m_volumeA[0] = reinterpret_cast<S_CollisionVolume *>(v);
            }
            // Its body frame (the sight's eye): its own frame or a part.
            uint8_t *body = next() % 2 ? frame : Child(frame, FRAME_DUMMY, "eye");
            if (g_side)
                Mut<uint8_t *>(a.bytes, 0x544) = body;
            else
                reinterpret_cast<C_MeleeTracker *>(a.bytes)->m_bodyFrame = AsFrame(body);
        }
    }
    // A frame without an actor.
    for (const S_FakeFrame &f : W().frames)
        if (!AsFrame(f.bytes)->m_data) {
            frames.push_back(f.bytes);
            break;
        }
}

void SetUpSightAnswers()
{
    // The view tests: in view (100), half, out.
    auto view = [](const std::vector<uint32_t> &a) {
        static const float values[] = {100.0f, 100.0f, 50.0f, 0.0f, 100.0f, 25.0f, 100.0f, 100.0f};
        if (int *status = reinterpret_cast<int *>(a[2]))
            *status = int(NextFake() % 3);
        if (float *v = reinterpret_cast<float *>(a[3]))
            *v = g_walls ? 100.0f : values[NextFake() % 8];   // (walled in: in view, the rays blocked)
        return 0u;
    };
    g_answers["C_Human::TestSightOfHuman"] = view;
    g_answers["C_Human::TestSightOfMeleeTracker"] = view;
    // (The tracker's: as the real one, now and then nothing written (its AI suspended), else status 99 or small.)
    g_answers["C_MeleeTracker::TestSight"] = [view](const std::vector<uint32_t> &a) {
        if (!g_walls && NextFake() % 5 == 0)
            return 0u;
        view(a);
        if (int *status = reinterpret_cast<int *>(a[2]))
            if (NextFake() % 2)
                *status = 99;
        return 0u;
    };
    // The rays: nothing (seen), a static volume, a dynamic one of another actor (the vehicles, the targets among them).
    g_answers["C_CollisionWorld::RayTest"] = [](const std::vector<uint32_t> &a) {
        const uint32_t r = g_walls ? 3 + NextFake() % 3 : NextFake() % 6;
        if (r < 3)
            return 0u;
        static uint8_t volume[2][0x80];
        std::memset(volume[g_side], 0, 0x80);
        S_CollisionVolume *v = reinterpret_cast<S_CollisionVolume *>(volume[g_side]);
        v->flags = r >= 4 ? COLLISION_FLAG_DYNAMIC : 0;
        const auto &actors = W().actors;
        v->actor = r >= 4 && !actors.empty() ? reinterpret_cast<C_Actor *>(actors[NextFake() % actors.size()].bytes) : nullptr;
        if (S_vector *hit = reinterpret_cast<S_vector *>(a[3]))
            *hit = S_vector(1.0f, 2.0f, 3.0f);
        Name(volume[g_side], "ray volume");
        return Ptr(volume[g_side]);
    };
    // The parts' search (EnumFrames: the callback for each of the mask's types; a non-zero answer stops it).
    g_answers["I3D_frame::EnumFrames"] = [](const std::vector<uint32_t> &a) {
        using T_Enum = uint32_t(__stdcall *)(I3D_frame *, void *);
        std::vector<I3D_frame *> found;
        std::function<void(I3D_frame *)> walk = [&](I3D_frame *f) {
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling) {
                if (c->m_type && (a[3] & (1u << (c->m_type - 1))))
                    found.push_back(c);
                walk(c);
            }
        };
        walk(reinterpret_cast<I3D_frame *>(a[0]));
        for (I3D_frame *f : found)
            if (reinterpret_cast<T_Enum>(a[1])(f, reinterpret_cast<void *>(a[2])))
                break;
        return 0u;
    };
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        const std::string want = a[1] ? reinterpret_cast<const char *>(a[1]) : "";
        const uint32_t types = a[2] & 0xFFFF;
        std::function<uint32_t(I3D_frame *)> find = [&](I3D_frame *p) -> uint32_t {
            for (I3D_frame *c = p->m_firstChild; c; c = c->m_nextSibling) {
                if (c->m_name && want == c->m_name && c->m_type >= 1 && (types & (1u << (c->m_type - 1))))
                    return Ptr(c);
                if (uint32_t d = find(c))
                    return d;
            }
            return 0u;
        };
        return find(reinterpret_cast<I3D_frame *>(a[0]));
    };
}

// ---- one case ----
enum {
    OP_ADD_OBSERVER, OP_ADD_TARGET, OP_REMOVE_OBSERVER, OP_REMOVE_TARGET, OP_OBSERVER_STATE, OP_TARGET_STATE, OP_TEST_NOW,
    OP_CAN_SEE, OP_PARTS, OP_REFRESH, OP_TESTED_BY, OP_TICK, OP_MISSION, OP_WALLS, OP_KINDS
};
const char *const OP_NAMES[] = {"AddObserver", "AddTarget", "RemoveObserver", "RemoveTarget", "ObserverState", "TargetState",
                                "TestNow", "CanSee", "Parts", "Refresh", "TestedBy", "Tick", "Mission", "Walls"};
struct S_SgOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
};
struct S_SgCase {
    uint32_t partSeed = 0;
    float sightTime = 1.0f;
    std::vector<S_SgOp> ops;
};
int g_sgCases, g_opCounts[OP_KINDS];
S_SgCase RandomSgCase()
{
    S_SgCase c;
    c.partSeed = U32();
    static const float times[] = {1.0f, 0.0f, 999.0f, 250.5f};
    c.sightTime = times[I(0, 3)];
    const int count = P(0.2) ? I(150, 400) : I(10, 70);
    for (int i = 0; i < count; ++i) {
        S_SgOp op;
        const int r = I(0, 99);
        op.kind = r < 14 ? OP_ADD_OBSERVER : r < 28 ? OP_ADD_TARGET : r < 31 ? OP_REMOVE_OBSERVER : r < 34 ? OP_REMOVE_TARGET
                : r < 37 ? OP_OBSERVER_STATE : r < 42 ? OP_TARGET_STATE : r < 48 ? OP_TEST_NOW : r < 52 ? OP_CAN_SEE
                : r < 55 ? OP_PARTS : r < 57 ? OP_REFRESH : r < 60 ? OP_TESTED_BY : r < 94 ? OP_TICK : r < 97 ? OP_WALLS : OP_MISSION;
        op.a = I(0, 1000);
        op.b = I(-1, 8);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        c.ops.push_back(op);
    }
    // The tests' 4 s wait over first, mostly.
    if (P(0.8)) {
        S_SgOp op;
        op.kind = OP_TICK;
        op.a = -1;
        c.ops.insert(c.ops.begin() + std::min<ptrdiff_t>(ptrdiff_t(c.ops.size()), I(0, 12)), op);
    }
    return c;
}

// (Every live observer's records cover every target: AddObserver skips a null target's, the later ones out of step, and
// then every program reads past them.)
bool RecordsInStep()
{
    const auto targetList = Targets();
    const auto observers = Observers();
    const size_t targets = targetList.size();
    for (uint8_t *o : observers)
        if (o && (ObserverMask(o) != 0 || ObserverFrame(o)) && RecordCount(o) < targets)   // (one added with mask 0 too)
            return false;
    // (The tick's reverse tests: a target's observer, an observer's target, both there with their records.)
    for (uint8_t *t : targetList) {
        if (!t)
            continue;
        const int own = g_side ? At<int>(t, 0x14) : reinterpret_cast<const C_SightTarget *>(t)->observer;
        if (own != -1 && (own < 0 || size_t(own) >= observers.size() || !observers[size_t(own)] ||
                          RecordCount(observers[size_t(own)]) < targets))
            return false;
    }
    for (uint8_t *o : observers) {
        if (!o)
            continue;
        const int target = g_side ? At<int>(o, 0x10) : reinterpret_cast<const C_SightObserver *>(o)->target;
        // (Live too: the reverse test updates it, and the original takes a removed target's actor through null.)
        if (target != -1 && (target < 0 || size_t(target) >= targets || !targetList[size_t(target)] ||
                             !TargetMask(targetList[size_t(target)])))
            return false;
    }
    return true;
}
const int TICK_TIMES[] = {0, 16, 33, 100, 250, 1000, 3999, 4000, 1, 7};
using T_SgInt = void(TC *)(void *, int);
std::string RunSgOp(const S_SgOp &op)
{
    const bool o = g_side != 0;
    uint8_t *g = OrigModule();
    C_SightModule &m = g_sightModule;
    const auto targets = Targets();
    const auto observers = Observers();
    const auto &frames = g_sgFrames[g_side];
    switch (op.kind) {
    case OP_ADD_OBSERVER:
    case OP_ADD_TARGET: {
        // (Mostly from a few trackers' and humans' frames: one actor both observer and target, the tick's reverse tests.)
        const auto &humans = g_humanFrames[g_side], &trackers = g_trackerFrames[g_side];
        const int pick = op.c % 10;
        uint8_t *frame = nullptr;
        if (pick < 4 && !trackers.empty())
            frame = trackers[size_t(op.a) % std::min<size_t>(trackers.size(), 2)];
        else if (pick < 8 && !humans.empty())
            frame = humans[size_t(op.a) % std::min<size_t>(humans.size(), 3)];
        else if (pick < 9 && !frames.empty())
            frame = frames[size_t(op.a) % frames.size()];
        static const uint32_t masks[] = {1, 2, 0xF, 3, 0, 16, 8, 0xF};
        const uint32_t mask = masks[op.d % 8];
        const bool observer = op.kind == OP_ADD_OBSERVER;
        const int r = o ? OriginalAt<int(TC *)(void *, void *, uint32_t)>(observer ? O_SG_ADD_OBSERVER : O_SG_ADD_TARGET)(g, frame, mask)
                        : observer ? m.AddObserver(AsFrame(frame), mask) : m.AddTarget(AsFrame(frame), mask);
        return F("%s %s %x -> %d", OP_NAMES[op.kind], NameOf(Ptr(frame)).c_str(), mask, r);
    }
    case OP_REMOVE_OBSERVER: {
        const int id = op.b;
        const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_SG_REMOVE_OBSERVER)(g, id) : m.RemoveObserver(id);
        return F("RemoveObserver %d -> %d", id, int(r));
    }
    case OP_REMOVE_TARGET: {
        const int id = op.b;
        if (!RecordsInStep())
            return "RemoveTarget (records out of step)";
        const bool r = o ? OriginalAt<bool(TC *)(void *, int)>(O_SG_REMOVE_TARGET)(g, id) : m.RemoveTarget(id);
        return F("RemoveTarget %d -> %d", id, int(r));
    }
    case OP_OBSERVER_STATE: {
        const int id = op.b, state = op.c % 3;
        if (o)
            OriginalAt<void(TC *)(void *, int, int)>(O_SG_OBSERVER_STATE)(g, id, state);
        else
            m.SetObserverState(id, state);
        return F("SetObserverState %d %d", id, state);
    }
    case OP_TARGET_STATE: {
        const int id = op.b, state = op.c % 6;
        if (!RecordsInStep())
            return "SetTargetState (records out of step)";
        // (Not a null target's: the original reads its mask through null, the port's fix takes it as none.)
        if (id >= 0 && size_t(id) < targets.size() && !targets[size_t(id)])
            return "SetTargetState (a null target)";
        if (o)
            OriginalAt<void(TC *)(void *, int, int)>(O_SG_TARGET_STATE)(g, id, state);
        else
            m.SetTargetState(id, state);
        return F("SetTargetState %d %d", id, state);
    }
    case OP_TEST_NOW:
    case OP_CAN_SEE:
    case OP_PARTS: {
        // (An observer there, its record of the target there: both programs index them unchecked.)
        const int oi = op.b, ti = op.c % 8;
        if (oi < 0 || size_t(oi) >= observers.size() || !observers[size_t(oi)] || size_t(ti) >= RecordCount(observers[size_t(oi)]))
            return F("%s (none)", OP_NAMES[op.kind]);
        // (Not a null target: the original's inlined GetTarget reads its mask through null.)
        if (size_t(ti) < targets.size() && !targets[size_t(ti)])
            return F("%s (a null target)", OP_NAMES[op.kind]);
        if (op.kind == OP_TEST_NOW) {
            // (A target's parts all there: the test reads each unchecked.)
            if (size_t(ti) < targets.size() && targets[size_t(ti)])
                for (uint32_t p : TargetParts(targets[size_t(ti)]))
                    if (!p)
                        return "TestNow (a part missing)";
            const bool r = o ? OriginalAt<bool(TC *)(void *, int, int)>(O_SG_TEST_NOW)(g, oi, ti) : m.TestNow(oi, ti);
            return F("TestNow %d %d -> %d", oi, ti, int(r));
        }
        // (A valid target only: the original reads a null or removed target's count and parts, the port's fix none.)
        if (size_t(ti) >= targets.size() || !targets[size_t(ti)] || !TargetMask(targets[size_t(ti)]))
            return F("%s (no valid target)", OP_NAMES[op.kind]);
        if (op.kind == OP_CAN_SEE) {
            int before = 77;
            bool visible = op.a & 1;
            const bool outs = op.d % 3 != 0;
            const int r = o ? OriginalAt<int(TC *)(void *, int, int, int *, bool *)>(O_SG_CAN_SEE)(g, oi, ti, outs ? &before : nullptr,
                                                                                                outs ? &visible : nullptr)
                            : m.CanSee(oi, ti, outs ? &before : nullptr, outs ? &visible : nullptr);
            return F("CanSee %d %d -> %d %d %d", oi, ti, r, before, int(visible));
        }
        uint32_t parts = 0x77;
        const bool outs = op.d % 3 != 0;
        if (o)
            OriginalAt<void(TC *)(void *, int, int, uint32_t *)>(O_SG_PARTS)(g, oi, ti, outs ? &parts : nullptr);
        else
            m.GetVisibleParts(oi, ti, outs ? &parts : nullptr);
        return F("GetVisibleParts %d %d -> %x", oi, ti, parts);
    }
    case OP_REFRESH: {
        const size_t ti = size_t(op.c) % 8;
        if (ti >= targets.size() || !targets[ti] || TargetKind(targets[ti]) != 0 || !TargetMask(targets[ti]))
            return "Refresh (no human target)";
        if (o)
            reinterpret_cast<void(TC *)(void *)>((*reinterpret_cast<void ***>(targets[ti]))[2])(targets[ti]);
        else
            reinterpret_cast<C_SightTarget *>(targets[ti])->Refresh();
        return F("Refresh %d", int(ti));
    }
    case OP_TESTED_BY: {
        const size_t ti = size_t(op.c) % 8;
        if (ti >= targets.size() || !targets[ti])
            return "TestedBy (none)";
        const uint32_t bits = uint32_t(op.d % 16);
        if (o)
            Mut<uint32_t>(targets[ti], 0x10) = bits;
        else
            reinterpret_cast<C_SightTarget *>(targets[ti])->testedBy = bits;
        return F("testedBy %d %x", int(ti), bits);
    }
    case OP_WALLS:
        g_walls = op.c & 1;
        return F("walls %d", int(g_walls));
    case OP_TICK: {
        const int t = op.a < 0 ? 4000 : TICK_TIMES[op.a % 10];
        if (o)
            Mut<uint32_t>(g_origGame, 0x28) += uint32_t(t);
        else
            reinterpret_cast<C_game *>(g_portGameBuf)->m_missionTime += uint32_t(t);
        // (Not with records out of step, nor with a target's part missing that a test may reach: read unchecked.)
        if (!RecordsInStep())
            return "Tick (records out of step)";
        if (o)
            OriginalAt<T_SgInt>(O_SG_TICK)(g, t);
        else
            m.Tick(t);
        return F("Tick %d", t);
    }
    default: {
        const bool server = op.c & 1;
        const int which = op.d % 5;
        static const uint32_t at[] = {O_SG_START, O_SG_STOP, O_SG_RESTART, O_SG_LOAD_DATA, O_SG_FREE_DATA};
        if (o)
            OriginalAt<T_SgInt>(at[which])(g, server);
        else if (which == 0)
            m.StartMission(server);
        else if (which == 1)
            m.StopMission(server);
        else if (which == 2)
            m.RestartMission(server);
        else if (which == 3)
            m.LoadMissionData(server);
        else
            m.FreeMissionData(server);
        static const char *const names[] = {"StartMission", "StopMission", "RestartMission", "LoadMissionData", "FreeMissionData"};
        return F("%s %d", names[which], int(server));
    }
    }
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("sg_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_sgShow;
void RunSgCase(const S_SgCase &c)
{
    g_caseStart = GetTickCount();
    ++g_sgCases;
    g_holdFrees = true;
    g_current = F("case %d", g_sgCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        g_world[side].frames.reserve(65536);
        BuildWorld(side, worldSeed);
        AddParts(c.partSeed);
        SetRole(GAME_ROLE_LOCAL);
        // The module emptied, its wait 0 (StartMission sets it), the sight time.
        if (side) {
            OriginalAt<T_Void>(O_SG_CLEAR)(OrigModule());
            Mut<uint32_t>(OrigModule(), 0x2C) = 0;
            Mut<uint32_t>(OrigModule(), 0x30) = 0;
            Mut<int>(OrigModule(), 0x34) = 0;
            *OriginalAt<float *>(O_SIGHT_TIME) = c.sightTime;
            Mut<uint32_t>(g_origGame, 0x28) = 5000;
        } else {
            g_sightModule.Clear();
            g_sightModule.m_observerCursor = 0;
            g_sightModule.m_targetCursor = 0;
            g_sightModule.m_startDelay = 0;
            g_sightTime = c.sightTime;
            reinterpret_cast<C_game *>(g_portGameBuf)->m_missionTime = 5000;
        }
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        std::srand(randSeed);
        g_log[side].clear();
        g_walls = false;
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        static const int trace = std::getenv("HD2_SG_TRACE") ? std::atoi(std::getenv("HD2_SG_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_sgCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_NAMES[c.ops[i].kind], c.ops[i].a,
                            c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunSgOp(c.ops[i]));
            if (trace == g_sgCases) {
                std::printf("trace   -> %s\n", out.back().substr(0, 160).c_str());
                for (const std::string &x : ModuleState())
                    std::printf("trace      %s\n", x.substr(0, 200).c_str());
                std::fflush(stdout);
            }
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : ModuleState())
                out.push_back(s);
        }
        g_case = "";
        if (side)
            OriginalAt<T_Void>(O_SG_CLEAR)(OrigModule());
        else
            g_sightModule.Clear();
    }
    static const int only = std::getenv("HD2_SG_ONLY") ? std::atoi(std::getenv("HD2_SG_ONLY")) : 0;
    if (g_sgShow && (!only || only == g_sgCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_sgShow == 2)
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

int __cdecl O_Atexit(void *) { return 0; }
void SetUpSight()
{
    SetUpCmd();
    SetUpSightAnswers();
    // (The original's GetVisibleParts registers its static's destructor: its CRT's atexit, never set up here.)
    RedirectOriginal(0x007e562e, reinterpret_cast<const void *>(&O_Atexit));
    // The original's module (its static initialiser never ran).
    OriginalAt<void(TC *)(void *, int, const char *)>(O_SG_CTOR)(OrigModule(), MODULE_SIGHT, "Sight");
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("sg_diff: main");
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
        std::printf("sg_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_SG_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_SG_SHOW"))
        g_sgShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_SG_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_SG_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpSight();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunSgCase(RandomSgCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_NAMES[i], g_opCounts[i]);
    std::printf("sg_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_sgCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
