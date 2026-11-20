// Human part 62: the movie control (game/controls.cpp lines 5422-5559) against the original's. MinGW; build_movie.bat;
// runs movie_diff.exe ("movie_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc,
// controls_kinds.inc) fakes what the controls reach outside themselves on both sides and compares the trees, the message
// queue, the engine's fakes and the calls on them. Planned trees of movie controls (alone or under a plain control). What
// the movie reaches outside the controls faked on both sides with the same loggers: its video stream (one fake
// ISND_stream: a round's answers for IsPlaying, Open, GetState, GetVideoSize), the graph's CreateStream (0x38), Set and
// GetRenderState (0x74, 0x78), the driver's Set and GetState (I3DGetDriver: the original's import 0x007e529c), the visual's
// UpdateWBoundProc (the original's import 0x007e5356). The kit's cases; the own messages straight to it; Refresh, FindArea,
// FitRect, CloseStream straight; the constructor's fields.
// HD2_MOVIE_ROUNDS: rounds per kind of call (default 20,000; HD2_MOVIE_QUICK: 4,000); HD2_MOVIE_CASE: one only;
// HD2_MOVIE_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/c_str.h"
#include "game/control.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/font.h"
#include "game/game_texts.h"
#include "game/item_defs.h"
#include "game/key_bindings.h"
#include "game/paged_text_box.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#define CONTROLS_KIT_HAS_MOVIE  // I3DGetDriver, UpdateWBoundProc: this part's fakes
#include "controls_kit.inc"

namespace {

// ---- the fakes outside the controls (a round's answers, both sides: one object each, stdcall, this on the stack) ----
struct S_FakeObject {
    void **vt;
};
void *g_streamVt[24], *g_driverVt[16];
S_FakeObject g_stream{g_streamVt}, g_created{g_streamVt}, g_driver{g_driverVt};
const char *StreamName(void *s) { return s == &g_stream ? "stream" : s == &g_created ? "created" : "stream?"; }
// The round's answers.
bool g_playing;
HRESULT g_openResult;
int g_streamState;
S_showRect g_videoSize;
bool g_createNone;
uint32_t g_driverState;
int g_renderState;

void __stdcall S_Other(void *s) { Log("%s other slot", StreamName(s)); }
uint32_t __stdcall S_Release(void *s)
{
    Log("%s Release", StreamName(s));
    return 0;
}
bool __stdcall S_IsPlaying(void *s)
{
    Log("%s IsPlaying", StreamName(s));
    return g_playing;
}
HRESULT __stdcall S_Open(void *s, const char *name, uint32_t flags)
{
    Log("%s Open '%s' %x", StreamName(s), name ? name : "(null)", flags);
    return g_openResult;
}
void __stdcall S_Close(void *s) { Log("%s Close", StreamName(s)); }
void __stdcall S_Play(void *s) { Log("%s Play", StreamName(s)); }
void __stdcall S_Stop(void *s) { Log("%s Stop", StreamName(s)); }
HRESULT __stdcall S_SetRect(void *s, const S_showRect *r)
{
    Log("%s SetRect %d %d %d %d", StreamName(s), r->x, r->y, r->width, r->height);
    return 0;
}
HRESULT __stdcall S_GetVideoSize(void *s, S_showRect *size)
{
    Log("%s GetVideoSize (%d %d)", StreamName(s), g_videoSize.width, g_videoSize.height);
    *size = g_videoSize;
    return 0;
}
int __stdcall S_GetState(void *s)
{
    Log("%s GetState", StreamName(s));
    return g_streamState;
}
void __stdcall S_SetVolume(void *s, float volume) { Log("%s SetVolume %a", StreamName(s), double(volume)); }
void __stdcall S_UpdateVideo(void *s, int unknown) { Log("%s UpdateVideo %d", StreamName(s), unknown); }
HRESULT __stdcall S_Draw(void *s, int keep)
{
    Log("%s Draw %d", StreamName(s), keep & 0xFF);
    return 0;
}

const char *DriverName(void *d) { return d == &g_driver ? "driver" : "driver?"; }
void __stdcall D_Other(void *d) { Log("%s other slot", DriverName(d)); }
void __stdcall D_SetState(void *d, int state, uint32_t value) { Log("%s SetState %d %x", DriverName(d), state, value); }
uint32_t __stdcall D_GetState(void *d, int state)
{
    Log("%s GetState %d", DriverName(d), state);
    return g_driverState;
}
void *__stdcall G_CreateStream(void *)
{
    Log("graph CreateStream");
    return g_createNone ? nullptr : &g_created;
}
void __stdcall G_SetRenderState(void *, int state, int value) { Log("graph SetRenderState %d %d", state, value); }
int __stdcall G_GetRenderState(void *, int state)
{
    Log("graph GetRenderState %d", state);
    return g_renderState;
}
// (Not logged: a getter; the port asks for the driver twice in Refresh, the original three times.)
void *__cdecl O_I3DGetDriver() { return &g_driver; }
void __stdcall O_UpdateWBoundProc(void *visual) { Log("%s UpdateWBoundProc", ArenaName(visual).c_str()); }

}  // namespace

// The port's.
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
void __stdcall I3D_visual::UpdateWBoundProc() { Log("%s UpdateWBoundProc", ArenaName(this).c_str()); }

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

// ---- the kinds ----
// The movie: +0x6C its stream, +0x70 left, +0x74 bottom, +0x78 right, +0x7C top, +0x80 width, +0x84 height, +0x88.
float RandomEdge() { return P(0.15) ? Pick({0.0f, -0.0f, 1.0f, -1.0f, 640.0f, 1e30f, NAN}) : F(-1200, 1600); }
void RandomMovie(C_Control *c)
{
    auto *m = static_cast<C_MovieControl *>(c);
    m->m_stream = reinterpret_cast<ISND_stream *>(P(0.8) ? &g_stream : &g_created);
    m->m_left = RandomEdge();
    m->m_bottom = RandomEdge();
    m->m_right = RandomEdge();
    m->m_top = RandomEdge();
    m->m_width = P(0.8) ? F(1, 1600) : RandomEdge();
    m->m_height = P(0.8) ? F(1, 1200) : RandomEdge();
    m->m_unknown88 = P(0.5) ? 0 : U32();
}
void MovieToOrig(C_Control *c, uint8_t *o)
{
    auto *m = static_cast<C_MovieControl *>(c);
    OF<void *>(o, 0x6C) = m->m_stream;
    OF<float>(o, 0x70) = m->m_left;
    OF<float>(o, 0x74) = m->m_bottom;
    OF<float>(o, 0x78) = m->m_right;
    OF<float>(o, 0x7C) = m->m_top;
    OF<float>(o, 0x80) = m->m_width;
    OF<float>(o, 0x84) = m->m_height;
    OF<uint32_t>(o, 0x88) = m->m_unknown88;
}
std::string MovieDiffers(C_Control *c, const uint8_t *o)
{
    auto *m = static_cast<C_MovieControl *>(c);
    if (OF<void *>(o, 0x6C) != static_cast<void *>(m->m_stream))
        return std::string("stream ") + StreamName(OF<void *>(o, 0x6C)) + " / " + StreamName(m->m_stream);
    const float port[] = {m->m_left, m->m_bottom, m->m_right, m->m_top, m->m_width, m->m_height};
    for (int k = 0; k < 6; ++k)
        if (!SameFloat(OF<float>(o, 0x70 + 4 * uint32_t(k)), port[k]))
            return "area +" + std::to_string(0x70 + 4 * k) + " " + FloatText(OF<float>(o, 0x70 + 4 * uint32_t(k))) + " / " + FloatText(port[k]);
    if (OF<uint32_t>(o, 0x88) != m->m_unknown88)
        return "+0x88";
    return "";
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"movie", 0x00814540, 0x8C, Make<C_MovieControl>, RandomMovie, MovieToOrig, MovieDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_MOVIE = 1;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.4)) {
        PlanAdd(-1, K_MOVIE, -1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 2); k > 0; --k)
            PlanAdd(root, K_MOVIE, -1);
        if (P(0.3))
            PlanAdd(root, K_CONTROL, -1);
    }
    return g_plan;
}
std::vector<size_t> NodesOf(int kind)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[kind])
            out.push_back(i);
    return out;
}
// The frames' world bounds: now and then empty, flat, out of date (UpdateWBoundProc called), unordered.
void AfterTree()
{
    g_cursorMade = true;
    for (auto &frame : g_arena.frames) {
        auto *min = reinterpret_cast<S_vector *>(frame + 0x170), *max = reinterpret_cast<S_vector *>(frame + 0x180);
        if (P(0.1))
            *max = *min;
        else if (P(0.1))
            max->y = min->y;
        else if (P(0.05))
            std::swap(*min, *max);
        if (P(0.2))
            *reinterpret_cast<uint32_t *>(frame + 0x1D0) &= ~uint32_t(VISFLAGS_WBOUND_VALID);
    }
}

// ---- the round ----
void PickRound()
{
    g_playing = P(0.6);
    g_openResult = P(0.6) ? Pick({0, 1}) : Pick({-1, int(0x80004005), int(0x80070002)});
    g_streamState = P(0.6) ? Pick({1, -1}) : Pick({0, 2, 3, -2});
    const int sizes[][2] = {{640, 480}, {640, 360}, {512, 512}, {1, 1}, {720, 576}, {0, 0}, {640, 0}, {0, 480}, {-640, 480}};
    const auto &s = sizes[I(0, int(sizeof sizes / sizeof sizes[0]) - 1)];
    g_videoSize = {I(-5, 5), I(-5, 5), s[0], s[1]};
    if (P(0.2))
        g_videoSize = {0, 0, I(1, 2000), I(1, 2000)};
    g_createNone = P(0.2);
    g_driverState = P(0.5) ? Pick({0u, 1u}) : U32();
    g_renderState = P(0.5) ? Pick({0, 1}) : int(U32());
}

const char *const MOVIES[] = {"intro", "", "%s%d", "Mission01", "a long movie name\\x"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_PLAY_MOVIE:
        m.text = Pick(MOVIES);
        if (P(0.1))
            m.text = C_str();
        break;
    case CONTROL_MSG_ADD_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    }
}

// ---- the tests ----
const uint32_t OWN_CODES[] = {CONTROL_MSG_CLOSE_MOVIE, CONTROL_MSG_STOP_MOVIE,  CONTROL_MSG_PLAY_MOVIE, CONTROL_MSG_PLAY_MOVIE,
                              CONTROL_MSG_PLAY_MOVIE,  CONTROL_MSG_REFRESH,     CONTROL_MSG_SHOW,       CONTROL_MSG_HIDE,
                              CONTROL_MSG_GET_VALUE,   CONTROL_MSG_SET_VALUE,   CONTROL_MSG_ADD_CALLBACK, 0x1000000,
                              0x2000001};
size_t PickMovie()
{
    const std::vector<size_t> nodes = NodesOf(K_MOVIE);
    return nodes[size_t(I(0, int(nodes.size()) - 1))];
}
// The movie's own messages sent straight to it.
void TestOwn()
{
    const size_t i = PickMovie();
    S_ControlMessage m;
    m.target = P(0.5) ? g_portNodes[i]->m_id : P(0.5) ? 0xFFFFFFFF : RandomId();
    m.id = Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.2);
    Diff([&] { rp = copy ? g_portNodes[i]->HandleMessageCopy(m) : g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x: %d / %d", g_messageBefore.id, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(m, mo);
        CHECK(why.empty(), "the message after: %s", why.c_str());
    }
}

// Refresh, FindArea (also without a frame), FitRect, CloseStream (also without a stream) straight.
using BoolFn = bool(__attribute__((thiscall)) *)(void *);
void TestCalls()
{
    const size_t i = PickMovie();
    auto *m = static_cast<C_MovieControl *>(g_portNodes[i]);
    const int pick = I(0, 4);
    bool rp = false, ro = false;
    if (pick == 0) {
        Diff([&] { m->Refresh(); }, [&] { Slot<VoidFn>(i, 0x18)(g_origNodes[i]); });
    } else if (pick == 1) {
        if (P(0.1))
            m->m_frame = nullptr;
        Diff([&] { rp = m->FindArea(); }, [&] { ro = OriginalAt<BoolFn>(0x00667f90)(g_origNodes[i]); });
    } else if (pick == 2) {
        Diff([&] { m->FitRect(); }, [&] { OriginalAt<VoidFn>(0x00668050)(g_origNodes[i]); });
    } else {
        if (P(0.3))
            m->m_stream = nullptr;
        Diff([&] { m->CloseStream(); }, [&] { OriginalAt<VoidFn>(0x006683c0)(g_origNodes[i]); });
    }
    CHECK(rp == ro, "call %d: %d / %d", pick, ro, rp);
}

// The constructor: inline in the original's factory (0x0065d470, type 0x22: +0x70 .. +0x88 0, +0x66 0; +0x6C not set:
// the original's stream whatever its block held until Create, the port's null).
void TestConstruct()
{
    auto *scene = reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000));
    const uint32_t id = U32();
    C_MovieControl movie(scene, id, nullptr);
    CHECK(!movie.m_stream && movie.m_left == 0 && movie.m_bottom == 0 && movie.m_right == 0 && movie.m_top == 0 && movie.m_width == 0 &&
              movie.m_height == 0 && movie.m_unknown88 == 0 && !movie.m_takesInput && movie.m_id == id && movie.m_scene == scene,
          "the movie's fields at construction");
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = CHILD_NAME_COUNT;
    g_treePlan = TreePlan;
    for (void *&s : g_streamVt)
        s = reinterpret_cast<void *>(S_Other);
    g_streamVt[0x04 / 4] = reinterpret_cast<void *>(S_Release);
    g_streamVt[0x08 / 4] = reinterpret_cast<void *>(S_IsPlaying);
    g_streamVt[0x0C / 4] = reinterpret_cast<void *>(S_Open);
    g_streamVt[0x10 / 4] = reinterpret_cast<void *>(S_Close);
    g_streamVt[0x14 / 4] = reinterpret_cast<void *>(S_Play);
    g_streamVt[0x18 / 4] = reinterpret_cast<void *>(S_Stop);
    g_streamVt[0x20 / 4] = reinterpret_cast<void *>(S_SetRect);
    g_streamVt[0x28 / 4] = reinterpret_cast<void *>(S_GetVideoSize);
    g_streamVt[0x2C / 4] = reinterpret_cast<void *>(S_GetState);
    g_streamVt[0x3C / 4] = reinterpret_cast<void *>(S_SetVolume);
    g_streamVt[0x4C / 4] = reinterpret_cast<void *>(S_UpdateVideo);
    g_streamVt[0x50 / 4] = reinterpret_cast<void *>(S_Draw);
    for (void *&s : g_driverVt)
        s = reinterpret_cast<void *>(D_Other);
    g_driverVt[0x18 / 4] = reinterpret_cast<void *>(D_SetState);
    g_driverVt[0x1C / 4] = reinterpret_cast<void *>(D_GetState);
    g_graphVt[0x38 / 4] = reinterpret_cast<void *>(G_CreateStream);
    g_graphVt[0x74 / 4] = reinterpret_cast<void *>(G_SetRenderState);
    g_graphVt[0x78 / 4] = reinterpret_cast<void *>(G_GetRenderState);
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE,   CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_VALUE,
               CONTROL_MSG_GET_VALUE,  CONTROL_MSG_REFRESH,    CONTROL_MSG_CLOSE_MOVIE,  CONTROL_MSG_STOP_MOVIE, CONTROL_MSG_PLAY_MOVIE,
               CONTROL_MSG_ADD_CALLBACK, 0x1000000,            0x2000001,                0};
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY};
    g_tickKinds = {K_MOVIE, K_CONTROL};
    g_messageHook = MessageValues;
    g_afterTree = AfterTree;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
    RedirectOriginal(0x007e529c, reinterpret_cast<const void *>(O_I3DGetDriver));
    RedirectOriginal(0x007e5356, reinterpret_cast<const void *>(O_UpdateWBoundProc));
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_MOVIE_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("movie_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_MOVIE_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_MOVIE_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_MOVIE_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Calls", TestCalls},   {"Construct", TestConstruct},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round) {
            PickRound();
            RandomTree();
            c.run();
        }
    }
    std::printf("movie_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
