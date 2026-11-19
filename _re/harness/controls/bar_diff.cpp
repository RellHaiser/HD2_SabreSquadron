// Human part 57: the bar and the upgrade bar (game/controls.cpp lines 4225-4410) against the original's. MinGW;
// build_bar.bat; runs bar_diff.exe ("bar_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc,
// controls_kinds.inc) fakes what the controls reach outside themselves on both sides and compares the trees, the message
// queue, the engine's fakes and the calls on them. The trees are planned: bars and upgrade bars (with their titles as
// Create makes them, now and then an extra child) under a control or alone. The frames' scales are set inline (the
// original's SetScale, LS3DF's S_quat::Make on both sides) and compared in the arena.
// HD2_BAR_ROUNDS: rounds per kind of call (default 20,000; HD2_BAR_QUICK: 4,000); HD2_BAR_CASE: one only;
// HD2_BAR_FIRST_CHANCE: faults under the original's exception frames shown.
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

// The formatter's own copy for the Format case (c_str.cpp's C_fstr has the other): a mutant of the header rebuilds this file
// only (MUT_WRAPPER), its functions not merged with c_str.cpp's.
#define msvc6 msvc6_test
#include "common/msvc_printf.h"
#undef msvc6
#include "original_exe.h"

#include "controls_kit.inc"

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }
// The node's n-th child of type T (null: none).
template <class T> T *ChildOf(C_Control *c, int n = 0)
{
    for (C_Control *k : c->m_children)
        if (auto *t = dynamic_cast<T *>(k))
            if (n-- == 0)
                return t;
    return nullptr;
}

// ---- the kinds ----
// A bar's float: the edges (0, -0, 1, the shown upgrade's 0.1 and below it, halves for the text's rounding), the
// usual range, now and then a NaN or an infinity. (One NaN only, the one arithmetic makes, 0xFFC00000: x87 and SSE pick
// differently between two NaNs, the sign of a NaN scale not the port's to keep.)
float RandomFloat()
{
    if (P(0.02))
        return Pick({-std::nanf(""), -std::nanf(""), INFINITY, -INFINITY});
    if (P(0.35))
        return Pick({0.0f, -0.0f, 1.0f, 0.5f, 100.0f, -1.0f, 0.1f, 0.099999994f, 0.25f, 2.75f, 0.05f, 0.15f, 1e-30f, 50.0f, 12.35f,
                     -0.05f, 99.95f, 1000000.0f});
    return P(0.5) ? F(-20, 120) : F(-2, 2);
}
const int AXES[] = {0, 1, 2, 0, 1, 2, 3, -1};

// The bar: +0x6C the value, +0x70 max, +0x74 min, +0x78 the axis.
void RandomBar(C_Control *c)
{
    auto *b = static_cast<C_BarControl *>(c);
    b->m_value = RandomFloat();
    b->m_max = P(0.3) ? Pick({1.0f, 100.0f, 10.0f}) : RandomFloat();
    b->m_min = P(0.4) ? 0.0f : RandomFloat();
    b->m_axis = P(0.9) ? Pick(AXES) : int(U32());
}
void BarToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_BarControl *>(c);
    OF<float>(o, 0x6C) = b->m_value;
    OF<float>(o, 0x70) = b->m_max;
    OF<float>(o, 0x74) = b->m_min;
    OF<int>(o, 0x78) = b->m_axis;
}
std::string BarDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_BarControl *>(c);
    if (!SameFloat(OF<float>(o, 0x6C), b->m_value) || !SameFloat(OF<float>(o, 0x70), b->m_max) || !SameFloat(OF<float>(o, 0x74), b->m_min) ||
        OF<int>(o, 0x78) != b->m_axis)
        return "bar " + FloatText(OF<float>(o, 0x6C)) + " " + FloatText(OF<float>(o, 0x70)) + " " + FloatText(OF<float>(o, 0x74)) + " " +
               std::to_string(OF<int>(o, 0x78)) + " / " + FloatText(b->m_value) + " " + FloatText(b->m_max) + " " + FloatText(b->m_min) +
               " " + std::to_string(b->m_axis);
    return "";
}

// The upgrade bar: the bar's, +0x7C the name's title, +0x80 the text's, +0x84 the value's frame, +0x88 the upgrade's,
// +0x8C the value, +0x90 the upgrade, +0x94 the suffix. (Its titles always in the trees: the original's Refresh and
// text messages use them unchecked; TestOwn takes one away for the messages that do not.)
const char *const SUFFIXES[] = {"%", " kg", "", "m/s"};
void RandomUpgrade(C_Control *c)
{
    auto *u = static_cast<C_UpgradeBar *>(c);
    RandomBar(c);
    u->m_name = ChildOf<C_TitleControl>(c, 0);
    u->m_text = ChildOf<C_TitleControl>(c, 1);
    u->m_valueFrame = RandomFrame();
    u->m_upgradeFrame = RandomFrame();
    const bool none = P(0.15);
    u->m_current = none ? Pick({0.0f, -0.0f}) : RandomFloat();
    u->m_upgrade = none ? Pick({0.0f, -0.0f}) : P(0.2) ? 0.0f : RandomFloat();
    u->m_suffix = P(0.2) ? C_str() : C_str(Pick(SUFFIXES));
}
void UpgradeToOrig(C_Control *c, uint8_t *o)
{
    auto *u = static_cast<C_UpgradeBar *>(c);
    BarToOrig(c, o);
    OF<uint8_t *>(o, 0x7C) = OrigOf(u->m_name);
    OF<uint8_t *>(o, 0x80) = OrigOf(u->m_text);
    OF<void *>(o, 0x84) = u->m_valueFrame;
    OF<void *>(o, 0x88) = u->m_upgradeFrame;
    OF<float>(o, 0x8C) = u->m_current;
    OF<float>(o, 0x90) = u->m_upgrade;
    OF<uint32_t>(o, 0x94) = OrigText(u->m_suffix);
}
std::string UpgradeDiffers(C_Control *c, const uint8_t *o)
{
    auto *u = static_cast<C_UpgradeBar *>(c);
    const std::string bar = BarDiffers(c, o);
    if (!bar.empty())
        return bar;
    if (PtrName(OF<void *>(o, 0x7C)) != PtrName(u->m_name) || PtrName(OF<void *>(o, 0x80)) != PtrName(u->m_text))
        return "titles " + PtrName(OF<void *>(o, 0x7C)) + " " + PtrName(OF<void *>(o, 0x80)) + " / " + PtrName(u->m_name) + " " +
               PtrName(u->m_text);
    const std::string frames = FramesDiffer(o, {{0x84, u->m_valueFrame}, {0x88, u->m_upgradeFrame}});
    if (!frames.empty())
        return frames;
    if (!SameFloat(OF<float>(o, 0x8C), u->m_current) || !SameFloat(OF<float>(o, 0x90), u->m_upgrade))
        return "upgrade " + FloatText(OF<float>(o, 0x8C)) + " " + FloatText(OF<float>(o, 0x90)) + " / " + FloatText(u->m_current) + " " +
               FloatText(u->m_upgrade);
    if (TextOf(OF<uint32_t>(o, 0x94)) != TextOf(u->m_suffix))
        return "suffix " + TextOf(OF<uint32_t>(o, 0x94)) + " / " + TextOf(u->m_suffix);
    return "";
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"bar", 0x008147e8, 0x7C, Make<C_BarControl>, RandomBar, BarToOrig, BarDiffers},
    {"upgrade", 0x00814760, 0x98, Make<C_UpgradeBar>, RandomUpgrade, UpgradeToOrig, UpgradeDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_BAR = 2, K_UPGRADE = 3;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
// A bar, or an upgrade bar with its name's and text's titles (as Create adds them), now and then an extra child.
void PlanBar(int parent)
{
    const bool upgrade = P(0.65);
    const int at = PlanAdd(parent, upgrade ? K_UPGRADE : K_BAR, P(0.7) ? -1 : I(0, 15));
    if (upgrade) {
        PlanAdd(at, K_TITLE, -1);
        PlanAdd(at, K_TITLE, -1);
    }
    if (P(0.15))
        PlanAdd(at, Pick({K_CONTROL, K_TITLE, K_BAR}), P(0.5) ? -1 : I(0, 15));
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.25)) {
        PlanBar(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 3); k > 0; --k)
            PlanBar(root);
        if (P(0.3))
            PlanAdd(root, Pick({K_CONTROL, K_TITLE}), -1);
    }
    return g_plan;
}

// Create's frames: the upgrade bar's titles' and its two frames now and then missing.
void CreateFrames(size_t node, int fi)
{
    if (fi < 0 || g_nodeKinds[node] != &KINDS[K_UPGRADE])
        return;
    for (const char *name : {"name_*", "text_*", "hlcpct*", "upgrade*"}) {
        int16_t &child = g_arena.child[fi][ChildName(name)];
        child = static_cast<int16_t>(P(0.85) ? I(0, 15) : -1);
    }
}

// The original's SetScale copies a 16-byte vector from its 12-byte one: the scale's fourth word (unused) its stack's.
void ScaleWordsBack()
{
    for (int f = 0; f < 16; ++f)
        std::memcpy(g_arena.frames[f] + 0xDC, g_arenaStart.frames[f] + 0xDC, 4);
}

// ---- the messages' values ----
const uint32_t BAR_CODES[] = {CONTROL_MSG_GET_BAR_MIN, CONTROL_MSG_GET_BAR_MAX, CONTROL_MSG_GET_BAR_VALUE, CONTROL_MSG_SET_BAR_MIN,
                              CONTROL_MSG_SET_BAR_MAX, CONTROL_MSG_SET_BAR_VALUE, CONTROL_MSG_SET_BAR_AXIS};
const char *const TEXTS[] = {"Shooting", "", "12", "Stamina"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_BAR_MIN:
    case CONTROL_MSG_SET_BAR_MAX:
        m.value2 = Bits(RandomFloat());
        break;
    case CONTROL_MSG_GET_BAR_VALUE:
        m.value = uint32_t(P(0.8) ? I(-1, 3) : int(U32()));
        break;
    case CONTROL_MSG_SET_BAR_VALUE:
        // (The upgrade bar's 2: the suffix, a text or none.)
        m.value = uint32_t(P(0.85) ? I(-1, 3) : int(U32()));
        m.value2 = m.value == 2 ? (P(0.2) ? 0 : reinterpret_cast<uintptr_t>(Pick(SUFFIXES))) : Bits(RandomFloat());
        break;
    case CONTROL_MSG_SET_BAR_AXIS:
        m.value = uint32_t(P(0.9) ? Pick(AXES) : int(U32()));
        break;
    case CONTROL_MSG_ADD_TEXT_ID:
    case CONTROL_MSG_SET_TEXT_ID:
        m.value2 = uint32_t(Pick({2002, 1, 77, 0}));
        break;
    case CONTROL_MSG_ADD_TEXT:
    case CONTROL_MSG_SET_TEXT:
        m.text = Pick(TEXTS);
        break;
    }
}

void PickRound()
{
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
}

// ---- the tests ----
std::vector<size_t> NodesOf(int kind)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[kind])
            out.push_back(i);
    return out;
}
size_t PickBar()
{
    std::vector<size_t> bars = NodesOf(K_BAR), upgrades = NodesOf(K_UPGRADE);
    bars.insert(bars.end(), upgrades.begin(), upgrades.end());
    return bars[size_t(I(0, int(bars.size()) - 1))];
}
const uint32_t OTHER_CODES[] = {CONTROL_MSG_SET_TEXT, CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_REFRESH, CONTROL_MSG_ADD_TEXT,
                                CONTROL_MSG_CLEAR,    CONTROL_MSG_GET_VALUE,   CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE,
                                CONTROL_MSG_SHOW,     CONTROL_MSG_HIDE,        0x1000000,           0x2000001};
// The bars' own messages sent straight to them; an upgrade bar's title now and then taken away (the others' routes too)
// for the bar's messages (the original's checks the text's title there).
void TestOwn()
{
    const size_t i = PickBar();
    S_ControlMessage m;
    m.target = P(0.2) ? 0xFFFFFFFF : P(0.7) ? g_portNodes[i]->m_id : RandomId();
    const bool untitled = g_nodeKinds[i] == &KINDS[K_UPGRADE] && P(0.15);
    m.id = untitled || P(0.75) ? Pick(BAR_CODES) : Pick(OTHER_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    if (untitled) {
        auto *u = static_cast<C_UpgradeBar *>(g_portNodes[i]);
        u->m_text = nullptr;
        for (C_Control *c : g_portNodes)
            c->m_routes.clear();
    }
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

// UpdateBar straight (its slot +0x84) and Refresh on the bars; an upgrade bar's text now and then with values half way
// between the shown tenths (the original's CRT rounds them as it does).
const float HALVES[] = {0.25f, 2.75f, 0.05f, 0.15f, 12.35f, 99.95f, -0.05f, 0.45f, 1.25f, 0.35f, 3.65f, 7.5f, 0.0f};
void TestUpdate()
{
    const size_t i = PickBar();
    if (g_nodeKinds[i] == &KINDS[K_UPGRADE] && P(0.4)) {
        auto *u = static_cast<C_UpgradeBar *>(g_portNodes[i]);
        u->m_min = 0.0f;
        u->m_max = Pick({100.0f, 1000.0f});
        u->m_current = Pick(HALVES);
        u->m_upgrade = P(0.3) ? 0.0f : Pick(HALVES);
    }
    if (P(0.5))
        Diff([&] { static_cast<C_BarControl *>(g_portNodes[i])->UpdateBar(); }, [&] { Slot<VoidFn>(i, 0x84)(g_origNodes[i]); });
    else
        Diff([&] { g_portNodes[i]->Refresh(); }, [&] { Slot<VoidFn>(i, 0x18)(g_origNodes[i]); });
}

// The texts' numbers: the port's C_fstr's formatter (msvc_printf.h) against the original's CRT (its static _vsnprintf, 0x007e5ad2:
// the kit puts MinGW's behind the original's C_fstr). The CRT's float conversions set first (its start-up's
// _cfltcvt_init, 0x007e599c: the copy's never ran; without them %f traps). The values: ties at the precisions (exact
// binary fractions), the bars' floats, big ones (the digits past 17), random bit patterns, infinities and NaNs; the
// formats: the game's, the flags, widths and precisions, %e and %g, a weight's with a text and a number.
const char *const FORMATS[] = {"%.1f ", "+ %.1f", "%3.1f", "%3.0f", "%2.0f", "%.2f", "%.3f", "%f", "%g", "%e", "%E", "%G", "%.0f",
                               "%10.4f", "%-8.1f|", "%+.1f", "% .2f", "%08.2f", "%-+9.3e|", "%.20f", "%5.1e", "%.17g", "%.0e",
                               "%#.0f", "%#.0e", "%#g", "%#.3g", "%.0g", "%.0G", "%.1g", "%.10g", "%12.6g|", "%0+10.1f", "%.30e", "%9.2f", "%.9f", "%19.9e"};
double RandomDouble()
{
    switch (I(0, 9)) {
    case 0:
        return Pick({0.25, 2.75, 0.75, 1.25, -0.25, 0.05, 1e-30, 100.0, 1000000.0, 0.0, -0.0, 0.125, 12.5, -2.5, 99.5, 9.95, 0.5,
                     1.5, 9.5, 99999.5, 0.0625, 1e30, 1e17, 1e16, 123456789012345678901.0, 5e-324, 1e-310, 1e300, 1e-5, 0.0001,
                     9.9999999, 0.99999999999999999, 4503599627370496.5,
                     // (17 nines and a 5 or more: the digits carried to a power of ten)
                     1e-14, 1e-70, 1e-73, 1e-78, 1e-79, 1e-174, 1e-175, 1e-176, 1e-243, 1e-305});
    case 1:
        return double(Pick(HALVES));
    case 2:
        return double(F(-200, 200));
    case 3:
        return I(-40000, 40000) / double(Pick({2, 4, 8, 16, 32, 64, 1024}));
    case 4:
        return double(RandomFloat());
    case 5: {
        uint64_t bits = (uint64_t(U32()) << 32) | U32();
        bits = (bits & ~(uint64_t(0x7FF) << 52)) | (uint64_t(I(1023 - 80, 1023 + 80)) << 52);
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }
    case 6: {
        uint64_t bits = (uint64_t(U32()) << 32) | U32();
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;   // (any: NaNs of every kind, infinities, denormals)
    }
    case 7:
        return Pick({HUGE_VAL, -HUGE_VAL, std::nan(""), -std::nan("")});
    case 8:
        return double(I(-1000, 1000)) + Pick({0.5, 0.05, 0.005, 0.25, 0.125, 0.0625});
    default:
        return std::ldexp(double(I(1, 1 << 20)), I(-60, 60)) * (P(0.5) ? -1 : 1);
    }
}
using T_Vsnprintf = int(__cdecl *)(char *, size_t, const char *, const void *);
// The port's text (C_fstr's: msvc6::VsnPrintf into its 2 KB).
std::string PortFormat(const char *format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    msvc6_test::VsnPrintf(text, sizeof text, format, args);
    va_end(args);
    return text;
}
int g_formatTies = 0, g_formatTieDiffers = 0;
void TestFormat()
{
    static const bool ready = (OriginalAt<void(__cdecl *)()>(0x007e599c)(), true);
    (void)ready;
    const double v = RandomDouble();
    char text[2048] = {};
    std::string port, orig;
    const char *format;
    if (P(0.15)) {
        // (The other conversions, passed on: the widths and precisions from the arguments, the integers, the 64-bit ones,
        // the pointer, the count, an unknown conversion.)
        std::vector<uint32_t> words;
        const auto put = [&](const void *at, size_t bytes) {
            for (size_t k = 0; k < bytes; k += 4) {
                uint32_t w = 0;
                std::memcpy(&w, static_cast<const uint8_t *>(at) + k, 4);
                words.push_back(w);
            }
        };
        const int w1 = P(0.2) ? -I(1, 12) : I(0, 12), p1 = P(0.2) ? -I(1, 3) : I(0, 8), w2 = P(0.2) ? -I(1, 9) : I(0, 9);
        const int a = int(U32()) >> I(0, 31), h = I(-70000, 70000), c = I(32, 126), z = P(0.5) ? 0 : I(-9, 9);
        const unsigned u = U32() >> I(0, 31);
        const long long x = (static_cast<long long>(U32()) << 32 | U32()) >> I(0, 63), y = -x;
        static const char *const NAMES[] = {"Weight", "", "x", "abcdef"};
        const char *s = Pick(NAMES);
        int count = -1, origCount = -1;
        switch (I(0, 4)) {
        case 0:
            format = "%*.*f|%-*d|%#x|%X|%o|%u|%hd|%.3s|%5c|%+d|% d|%05d|%-7s|%%|%i|%.0d|%.0x";
            put(&w1, 4), put(&p1, 4), put(&v, 8), put(&w2, 4), put(&a, 4), put(&u, 4), put(&u, 4), put(&u, 4), put(&u, 4), put(&h, 4);
            put(&s, 4), put(&c, 4), put(&a, 4), put(&a, 4), put(&a, 4), put(&s, 4), put(&a, 4), put(&z, 4), put(&z, 4);
            port = PortFormat(format, w1, p1, v, w2, a, u, u, u, u, h, s, c, a, a, a, s, a, z, z);
            break;
        case 1:
            // (MSVC6's 64-bit one only: %lld and %I32d it does not know.)
            format = "%I64d %I64d %I64x %I64u %I64X|%-20I64d|%Id|%I32d";
            put(&x, 8), put(&y, 8), put(&x, 8), put(&y, 8), put(&y, 8), put(&y, 8);
            port = PortFormat(format, x, y, x, y, y, y);
            break;
        case 2: {
            format = "%p|%s%n|%d";
            const void *ptr = reinterpret_cast<const void *>(uintptr_t(U32()));
            int *where = &origCount;
            put(&ptr, 4), put(&s, 4), put(&where, 4), put(&a, 4);
            port = PortFormat(format, ptr, s, &count, a);
            break;
        }
        case 3:
            format = "%*d|%.*f|%-*.*e|%*.*g";
            put(&w1, 4), put(&a, 4), put(&p1, 4), put(&v, 8), put(&w2, 4), put(&p1, 4), put(&v, 8), put(&w1, 4), put(&p1, 4), put(&v, 8);
            port = PortFormat(format, w1, a, p1, v, w2, p1, v, w1, p1, v);
            break;
        default:
            format = "%5.2y|%s|%-+ #012.3f|";
            put(&s, 4), put(&v, 8);
            port = PortFormat(format, s, v);
            break;
        }
        OriginalAt<T_Vsnprintf>(0x007e5ad2)(text, sizeof text, format, words.data());
        CHECK(count == origCount, "%%n: %d / %d", origCount, count);
        // (The port's own contract, a buffer too small: the whole length returned, the start kept and ended.)
        if (format[1] == '5') {
            char small[9];
            const size_t size = size_t(I(0, 8));
            std::memset(small, 'z', sizeof small);
            small[8] = '\0';
            const int made = msvc6_test::SnPrintf(small, size, format, s, v);
            const std::string all = PortFormat(format, s, v);
            // (Size 0: nothing written.)
            const std::string kept = size ? all.substr(0, size - 1) : std::string("zzzzzzzz");
            CHECK(made == int(all.size()) && std::string(small) == kept, "truncated to %u: %d '%s' / '%s'", unsigned(size), made, small,
                  all.c_str());
        }
    } else if (P(0.15)) {
        // (The weight's: a text, the weight, the limit.)
        static const char *const MIXED[] = {"%s: %3.1f / %d Kg", "%s %+08.2f %5d|%-6s|", "%d%%%.0f%c"};
        static const char *const NAMES[] = {"Weight", "", "x"};
        format = Pick(MIXED);
        const char *name = Pick(NAMES);
        const int n = I(-5, 120);
        uint32_t args[5];
        if (format[1] == 'd') {
            args[0] = uint32_t(n);
            std::memcpy(args + 1, &v, 8);
            args[3] = 'k';
            port = PortFormat(format, n, v, 'k');
        } else {
            args[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name));
            std::memcpy(args + 1, &v, 8);
            args[3] = uint32_t(n);
            args[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name));
            port = PortFormat(format, name, v, n, name);
        }
        OriginalAt<T_Vsnprintf>(0x007e5ad2)(text, sizeof text, format, args);
    } else {
        format = Pick(FORMATS);
        port = PortFormat(format, v);
        const double args[1] = {v};
        OriginalAt<T_Vsnprintf>(0x007e5ad2)(text, sizeof text, format, args);
    }
    orig = text;
    // (A tie past the 17th digit: the original's either way, counted, and the texts that differ there.)
    if (msvc6_test::detail::FltOut(v).tie) {
        ++g_formatTies;
        g_formatTieDiffers += port != orig;
        if (port != orig && std::getenv("HD2_BAR_TIES"))
            std::printf("  tie: %s: %s / %s\n", format, orig.c_str(), port.c_str());
        return;
    }
    uint64_t b;
    std::memcpy(&b, &v, 8);
    CHECK(port == orig, "C_fstr(%s, %016llx): %s / %s", format, static_cast<unsigned long long>(b), orig.c_str(), port.c_str());
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = 50;   // (the upgrade bar's names drawn)
    g_treePlan = TreePlan;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE,  CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX,  CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE,    CONTROL_MSG_REFRESH,    CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_ADD_TEXT, CONTROL_MSG_ADD_TEXT_ID,  CONTROL_MSG_CLEAR,      CONTROL_MSG_SET_MARGIN,
               CONTROL_MSG_GET_BAR_MIN, CONTROL_MSG_GET_BAR_MAX, CONTROL_MSG_GET_BAR_VALUE, 0x1000000,          0x2000001,
               0};
    // (The bar's sets update it: an upgrade bar shown or hidden.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SET_BAR_MIN, CONTROL_MSG_SET_BAR_MAX,
                   CONTROL_MSG_SET_BAR_VALUE, CONTROL_MSG_SET_BAR_AXIS};
    g_tickKinds = {K_BAR, K_UPGRADE, K_TITLE, K_CONTROL};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterOrig = ScaleWordsBack;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_BAR_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("bar_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_BAR_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_BAR_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_BAR_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Update", TestUpdate}, {"Format", TestFormat},
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
    if (g_formatTies)
        std::printf("  (format: %d values tied past the 17th digit, not compared; %d of their texts differ)\n", g_formatTies,
                    g_formatTieDiffers);
    std::printf("bar_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
