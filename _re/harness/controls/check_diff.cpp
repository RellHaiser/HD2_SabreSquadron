// Human part 51: the check box, the toggle buttons, the highlighted check box and its halves, the side indicator and the
// mark (game/controls.cpp lines 1529-2199) against the original's. MinGW; build_check.bat; runs check_diff.exe
// ("check_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc) fakes what the controls reach
// outside themselves on both sides and compares the trees, the message queue, the engine's fakes and the calls on them;
// the halves and titles a Create makes are adopted into the trees and compared too.
// HD2_CHECK_ROUNDS: rounds per kind of call (default 20,000; HD2_CHECK_QUICK: 4,000); HD2_CHECK_CASE: one only;
// HD2_CHECK_FIRST_CHANCE: faults under the original's exception frames shown.
#include <windows.h>

#include <algorithm>
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
#include "game/mp_zone.h"
#include "game/paged_text_box.h"
#include "game/pending.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "net/net_game.h"
#undef private
#undef protected

#include "original_exe.h"

#include "controls_kit.inc"

namespace {

// ---- the kinds ----
const uint32_t O_BOX = 0x6C;
template <class T> C_Control *Make(I3D_scene *scene, uint32_t id, C_Control *parent) { return new C_Logged<T>(scene, id, parent); }
I3D_frame *RandomFrame() { return FrameAt(I(0, 15)); }
void NoFields(C_Control *) {}
void NoToOrig(C_Control *, uint8_t *) {}
std::string NoDiffers(C_Control *, const uint8_t *) { return ""; }
// Frame fields (the port's members and the original's offsets) set, copied and compared alike.
std::string FramesDiffer(const uint8_t *o, std::initializer_list<std::pair<uint32_t, I3D_frame *>> fields)
{
    for (const auto &f : fields)
        if (OF<I3D_frame *>(o, f.first) != f.second)
            return "frame +" + std::to_string(f.first) + " " + PtrName(OF<I3D_frame *>(o, f.first)) + " / " + PtrName(f.second);
    return "";
}

// Text controls (the mark's title): the box.
void RandomText(C_Control *c) { RandomBox(static_cast<C_TextControl *>(c)->m_box); }
void TextToOrig(C_Control *c, uint8_t *o) { BoxToOrig(static_cast<C_TextControl *>(c)->m_box, o, O_BOX); }
std::string TextDiffers(C_Control *c, const uint8_t *o) { return BoxDiffers(static_cast<C_TextControl *>(c)->m_box, o, O_BOX); }

// The check box: the box, +0x70 yes full, +0x74 yes empty, +0x78 no full, +0x7C no empty, +0x80 checked.
void RandomCheck(C_Control *c)
{
    auto *b = static_cast<C_CheckBox *>(c);
    RandomText(c);
    b->m_yesFull = RandomFrame();
    b->m_yesEmpty = RandomFrame();
    b->m_noFull = RandomFrame();
    b->m_noEmpty = RandomFrame();
    b->m_checked = P(0.5);
}
void CheckToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_CheckBox *>(c);
    TextToOrig(c, o);
    OF<I3D_frame *>(o, 0x70) = b->m_yesFull;
    OF<I3D_frame *>(o, 0x74) = b->m_yesEmpty;
    OF<I3D_frame *>(o, 0x78) = b->m_noFull;
    OF<I3D_frame *>(o, 0x7C) = b->m_noEmpty;
    OF<bool>(o, 0x80) = b->m_checked;
}
std::string CheckDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_CheckBox *>(c);
    std::string why = FramesDiffer(o, {{0x70, b->m_yesFull}, {0x74, b->m_yesEmpty}, {0x78, b->m_noFull}, {0x7C, b->m_noEmpty}});
    if (why.empty() && OF<bool>(o, 0x80) != b->m_checked)
        why = "checked";
    return why.empty() ? TextDiffers(c, o) : why;
}

// The toggle buttons: +0x6C active down, +0x70 normal down, +0x74 active up, +0x78 normal up, +0x80 down, +0x81 over.
void RandomToggle(C_Control *c)
{
    auto *t = static_cast<C_ToggleButton *>(c);
    t->m_activeDown = RandomFrame();
    t->m_normalDown = RandomFrame();
    t->m_activeUp = RandomFrame();
    t->m_normalUp = RandomFrame();
    t->m_down = P(0.5);
    t->m_over = P(0.5);
}
void ToggleToOrig(C_Control *c, uint8_t *o)
{
    auto *t = static_cast<C_ToggleButton *>(c);
    OF<I3D_frame *>(o, 0x6C) = t->m_activeDown;
    OF<I3D_frame *>(o, 0x70) = t->m_normalDown;
    OF<I3D_frame *>(o, 0x74) = t->m_activeUp;
    OF<I3D_frame *>(o, 0x78) = t->m_normalUp;
    OF<bool>(o, 0x80) = t->m_down;
    OF<bool>(o, 0x81) = t->m_over;
}
std::string ToggleDiffers(C_Control *c, const uint8_t *o)
{
    auto *t = static_cast<C_ToggleButton *>(c);
    std::string why = FramesDiffer(o, {{0x6C, t->m_activeDown}, {0x70, t->m_normalDown}, {0x74, t->m_activeUp}, {0x78, t->m_normalUp}});
    if (why.empty() && (OF<bool>(o, 0x80) != t->m_down || OF<bool>(o, 0x81) != t->m_over))
        why = "down/over";
    return why;
}

// The highlighted check box's halves: +0x6C empty, +0x70 full, +0x74 active full, +0x78 active empty, +0x7C over, +0x7D empty.
void RandomHalf(C_Control *c)
{
    auto *h = static_cast<C_CheckBoxHalf *>(c);
    h->m_emptyFrame = RandomFrame();
    h->m_fullFrame = RandomFrame();
    h->m_activeFullFrame = RandomFrame();
    h->m_activeEmptyFrame = RandomFrame();
    h->m_over = P(0.5);
    h->m_empty = P(0.5);
}
void HalfToOrig(C_Control *c, uint8_t *o)
{
    auto *h = static_cast<C_CheckBoxHalf *>(c);
    OF<I3D_frame *>(o, 0x6C) = h->m_emptyFrame;
    OF<I3D_frame *>(o, 0x70) = h->m_fullFrame;
    OF<I3D_frame *>(o, 0x74) = h->m_activeFullFrame;
    OF<I3D_frame *>(o, 0x78) = h->m_activeEmptyFrame;
    OF<bool>(o, 0x7C) = h->m_over;
    OF<bool>(o, 0x7D) = h->m_empty;
}
std::string HalfDiffers(C_Control *c, const uint8_t *o)
{
    auto *h = static_cast<C_CheckBoxHalf *>(c);
    std::string why = FramesDiffer(o, {{0x6C, h->m_emptyFrame}, {0x70, h->m_fullFrame}, {0x74, h->m_activeFullFrame}, {0x78, h->m_activeEmptyFrame}});
    if (why.empty() && (OF<bool>(o, 0x7C) != h->m_over || OF<bool>(o, 0x7D) != h->m_empty))
        why = "over/empty " + std::to_string(OF<bool>(o, 0x7C)) + std::to_string(OF<bool>(o, 0x7D)) + " / " + std::to_string(h->m_over) +
              std::to_string(h->m_empty);
    return why;
}

// The highlighted check box: the box, +0x70 yes, +0x74 no (child halves), +0x78 checked, +0x7C the id under the cursor.
void RandomHighlight(C_Control *c)
{
    auto *h = static_cast<C_HighlightCheckBox *>(c);
    RandomText(c);
    h->m_checked = P(0.5);
}
void HighlightToOrig(C_Control *c, uint8_t *o)
{
    auto *h = static_cast<C_HighlightCheckBox *>(c);
    TextToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = h->m_yes ? ToOrigNode(h->m_yes) : nullptr;
    OF<uint8_t *>(o, 0x74) = h->m_no ? ToOrigNode(h->m_no) : nullptr;
    OF<bool>(o, 0x78) = h->m_checked;
    OF<uint32_t>(o, 0x7C) = h->m_overId;
}
std::string HighlightDiffers(C_Control *c, const uint8_t *o)
{
    auto *h = static_cast<C_HighlightCheckBox *>(c);
    if (PtrName(OF<void *>(o, 0x70)) != PtrName(h->m_yes) || PtrName(OF<void *>(o, 0x74)) != PtrName(h->m_no))
        return "halves";
    if (OF<bool>(o, 0x78) != h->m_checked || OF<uint32_t>(o, 0x7C) != h->m_overId)
        return "checked/over id";
    return TextDiffers(c, o);
}

// The side indicator: +0x70 allied, +0x74 axis, +0x78 the side, +0x7C the timer; its index the zone's address.
void RandomSide(C_Control *c)
{
    auto *s = static_cast<C_SideIndicator *>(c);
    s->m_alliedFrame = RandomFrame();
    s->m_axisFrame = RandomFrame();
    s->m_side = P(0.8) ? I(0, 2) : I(-2, 5);
    s->m_timer = P(0.3) ? Pick({0, 1, 2, -1, 2000}) : I(-100, 2500);
    s->m_index = static_cast<int>(reinterpret_cast<uintptr_t>(g_arena.zone));
    // (The zone's team at the port's offset too: g++ lays C_MPZone out its own way.)
    static_assert(sizeof(C_MPZone) <= sizeof g_arena.zone, "the arena's zone too small");
    reinterpret_cast<C_MPZone *>(g_arena.zone)->m_team = *reinterpret_cast<int *>(g_arena.zone + 0x98);
}
void SideToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_SideIndicator *>(c);
    OF<I3D_frame *>(o, 0x70) = s->m_alliedFrame;
    OF<I3D_frame *>(o, 0x74) = s->m_axisFrame;
    OF<int>(o, 0x78) = s->m_side;
    OF<int>(o, 0x7C) = s->m_timer;
}
std::string SideDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_SideIndicator *>(c);
    std::string why = FramesDiffer(o, {{0x70, s->m_alliedFrame}, {0x74, s->m_axisFrame}});
    if (why.empty() && (OF<int>(o, 0x78) != s->m_side || OF<int>(o, 0x7C) != s->m_timer))
        why = "side " + std::to_string(OF<int>(o, 0x78)) + " timer " + std::to_string(OF<int>(o, 0x7C)) + " / " + std::to_string(s->m_side) +
              " " + std::to_string(s->m_timer);
    return why;
}

// The mark: +0x6C its title (owned, not a child), +0x70 no active, +0x74 no normal, +0x78 yes active, +0x7C yes normal,
// +0x80 empty active, +0x84 empty normal, +0x88 its value.
void RandomMark(C_Control *c)
{
    auto *m = static_cast<C_MarkBox *>(c);
    m->m_noActive = RandomFrame();
    m->m_noNormal = RandomFrame();
    m->m_yesActive = RandomFrame();
    m->m_yesNormal = RandomFrame();
    m->m_emptyActive = RandomFrame();
    m->m_emptyNormal = RandomFrame();
    m->m_value = static_cast<uint8_t>(P(0.8) ? I(0, 1) : I(2, 255));
}
void MarkToOrig(C_Control *c, uint8_t *o)
{
    auto *m = static_cast<C_MarkBox *>(c);
    OF<uint8_t *>(o, 0x6C) = m->m_title ? ToOrigNode(m->m_title) : nullptr;
    OF<I3D_frame *>(o, 0x70) = m->m_noActive;
    OF<I3D_frame *>(o, 0x74) = m->m_noNormal;
    OF<I3D_frame *>(o, 0x78) = m->m_yesActive;
    OF<I3D_frame *>(o, 0x7C) = m->m_yesNormal;
    OF<I3D_frame *>(o, 0x80) = m->m_emptyActive;
    OF<I3D_frame *>(o, 0x84) = m->m_emptyNormal;
    OF<uint8_t>(o, 0x88) = m->m_value;
}
std::string MarkDiffers(C_Control *c, const uint8_t *o)
{
    auto *m = static_cast<C_MarkBox *>(c);
    if (PtrName(OF<void *>(o, 0x6C)) != PtrName(m->m_title))
        return "title " + PtrName(OF<void *>(o, 0x6C)) + " / " + PtrName(m->m_title);
    std::string why = FramesDiffer(o, {{0x70, m->m_noActive}, {0x74, m->m_noNormal}, {0x78, m->m_yesActive}, {0x7C, m->m_yesNormal},
                                       {0x80, m->m_emptyActive}, {0x84, m->m_emptyNormal}});
    if (why.empty() && OF<uint8_t>(o, 0x88) != m->m_value)
        why = "value";
    return why;
}
C_Control **MarkTitle(C_Control *c) { return reinterpret_cast<C_Control **>(&static_cast<C_MarkBox *>(c)->m_title); }

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"check", 0x00815200, 0x84, Make<C_CheckBox>, RandomCheck, CheckToOrig, CheckDiffers, O_BOX},
    {"toggle", 0x00814c28, 0x84, Make<C_ToggleButton>, RandomToggle, ToggleToOrig, ToggleDiffers},
    {"alltoggle", 0x00814100, 0x84, Make<C_AllToggleButton>, RandomToggle, ToggleToOrig, ToggleDiffers},
    {"half", 0x00814078, 0x80, Make<C_CheckBoxHalf>, RandomHalf, HalfToOrig, HalfDiffers},
    {"highlight", 0x00815178, 0x80, Make<C_HighlightCheckBox>, RandomHighlight, HighlightToOrig, HighlightDiffers, O_BOX},
    {"side", 0x008146d8, 0x80, Make<C_SideIndicator>, RandomSide, SideToOrig, SideDiffers},
    {"mark", 0x00814650, 0x8C, Make<C_MarkBox>, RandomMark, MarkToOrig, MarkDiffers, 0, nullptr, false, 0x6C, MarkTitle},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_TITLE = 1, K_CHECK = 2, K_TOGGLE = 3, K_ALL = 4, K_HALF = 5, K_HIGHLIGHT = 6, K_SIDE = 7, K_MARK = 8;

}  // namespace

namespace {
#include "controls_tree.inc"

C_Control *AddNode(C_Control *owner, int kind, uint32_t last, bool child)
{
    C_Control *c = KINDS[kind].make(owner->m_scene, owner->m_id | last, owner);
    if (child)
        owner->m_children.push_back(c);
    g_portNodes.push_back(c);
    g_nodeKinds.push_back(&KINDS[kind]);
    c->m_frame = RandomFrame();
    c->m_inactive = P(0.3);
    c->m_shown = P(0.8);
    c->m_takesInput = P(0.8);
    KINDS[kind].randomize(c);
    return c;
}
// The highlighted check boxes' halves (children; the original's handlers use them unchecked), the marks' titles (owned).
void WireChildren()
{
    const size_t n = g_portNodes.size();
    for (size_t i = 0; i < n; ++i) {
        C_Control *c = g_portNodes[i];
        if (g_nodeKinds[i] == &KINDS[K_HIGHLIGHT]) {
            auto *h = static_cast<C_HighlightCheckBox *>(c);
            h->m_yes = static_cast<C_CheckBoxHalf *>(AddNode(c, K_HALF, 1, true));
            h->m_no = static_cast<C_CheckBoxHalf *>(AddNode(c, K_HALF, 2, true));
            h->m_overId = P(0.3) ? 0 : P(0.6) ? (c->m_id | uint32_t(I(1, 2))) : U32();
        } else if (g_nodeKinds[i] == &KINDS[K_MARK] && P(0.8)) {
            static_cast<C_MarkBox *>(c)->m_title = static_cast<C_TitleControl *>(AddNode(c, K_TITLE, 1, false));
        }
    }
}

// Create's frames: the children a kind's Create needs (the side indicator's and the highlight's used unchecked).
void Complete(int f, std::initializer_list<const char *> names, int count)
{
    g_arena.numChildren[f] = count;
    for (const char *name : names) {
        const int k = ChildName(name);
        if (g_arena.child[f][k] < 0 || P(0.5))
            g_arena.child[f][k] = static_cast<int16_t>(I(0, 15));
    }
}
void CreateFrames(size_t node, int fi)
{
    const S_Kind *k = g_nodeKinds[node];
    const bool full = P(0.85);
    if (k == &KINDS[K_CHECK] && full)
        Complete(fi, {"yes_full*", "yes_empty*", "no_full*", "no_empty*"}, 4);
    if (k == &KINDS[K_TOGGLE] && full)
        Complete(fi, {"activeddown*", "normaldown*", "activedup*", "normalup*"}, 4);
    if (k == &KINDS[K_ALL] && full)
        Complete(fi, {"allactived", "allnormal", "xallactived", "xallnormal"}, 4);
    if (k == &KINDS[K_HIGHLIGHT] && full)
        Complete(fi, {"yes_empty*", "yes_full*", "yesactived_full*", "no_empty*", "no_full*", "noactived_full*", "noactived_empty*"}, 7);
    if (k == &KINDS[K_SIDE] && (full || g_arena.numChildren[fi] >= 3))
        Complete(fi, {"allied*", "axis*"}, P(0.8) ? 3 : 4);
    if (k == &KINDS[K_MARK] && full)
        Complete(fi, {"noactived*", "nonormal*", "yesactived*", "yesnormal*", "emptyactived*", "emptynormal*"}, 6);
}

void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_CHECKED:
    case CONTROL_MSG_SET_CHECKED_QUIET:
        m.value = P(0.8) ? uint32_t(I(0, 1)) : P(0.5) ? Pick({0x100u, 0x101u, 2u, 0xFFu}) : U32();
        break;
    case CONTROL_MSG_SET_SIDE:
        m.value = uint32_t(P(0.9) ? I(-1, 3) : int(U32()));
        break;
    case CONTROL_MSG_SET_NUMBER:
        m.value = uint32_t(P(0.8) ? I(-5, 120) : int(U32()));
        break;
    }
}

// One message straight to a node of `kind`.
void SendTo(int kind, S_ControlMessage m)
{
    std::vector<size_t> nodes;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[kind])
            nodes.push_back(i);
    if (nodes.empty())
        return;
    const size_t i = nodes[size_t(I(0, int(nodes.size()) - 1))];
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    Diff([&] { rp = g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x to %s: %d / %d", g_messageBefore.id, KINDS[kind].name, ro, rp);
    const std::string why = MessageDiffers(m, mo);
    CHECK(why.empty(), "the message after (%s): %s", KINDS[kind].name, why.c_str());
}

// The kinds' own messages sent straight to them (the checks' values against the state's byte, the side, the number).
void TestOwn()
{
    RandomTree();
    const int kinds[] = {K_CHECK, K_TOGGLE, K_ALL, K_HIGHLIGHT, K_SIDE, K_MARK};
    S_ControlMessage m;
    m.target = 0xFFFFFFFF;
    const uint32_t codes[] = {CONTROL_MSG_GET_CHECKED, CONTROL_MSG_SET_CHECKED, CONTROL_MSG_SET_CHECKED_QUIET, CONTROL_MSG_SET_SIDE,
                              CONTROL_MSG_GET_SIDE,    CONTROL_MSG_SET_NUMBER,  CONTROL_MSG_FACE_FRONT};
    m.id = Pick(codes);
    MessageValues(m);
    SendTo(Pick(kinds), m);
}

// The halves' and the highlighted check box's slots straight (over or not, empty or full, inactive or not).
void TestHalves()
{
    RandomTree({0, K_HIGHLIGHT, K_HALF, K_CHECK});
    std::vector<size_t> nodes;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_HALF] || g_nodeKinds[i] == &KINDS[K_HIGHLIGHT] || g_nodeKinds[i] == &KINDS[K_CHECK])
            nodes.push_back(i);
    if (nodes.empty())
        return;
    const size_t i = nodes[size_t(I(0, int(nodes.size()) - 1))];
    C_Control *p = g_portNodes[i];
    // (Ids of the halves under the cursor: the highlight's own with 1 or 2, or another.)
    const int id = int(P(0.7) ? (p->m_id | uint32_t(I(0, 3))) : U32());
    int rp = 0, ro = 0;
    switch (I(0, 6)) {
    case 0:
        Diff([&] { rp = p->OnEnter(id); }, [&] { ro = Slot<IntFn>(i, 0x30)(g_origNodes[i], id); });
        break;
    case 1:
        Diff([&] { rp = p->OnLeave(id); }, [&] { ro = Slot<IntFn>(i, 0x34)(g_origNodes[i], id); });
        break;
    case 2:
        Diff([&] { rp = p->OnKeyClick(id); }, [&] { ro = Slot<IntFn>(i, 0x5C)(g_origNodes[i], id); });
        break;
    case 3:
        Diff([&] { rp = p->OnInput05(id); }, [&] { ro = Slot<IntFn>(i, 0x40)(g_origNodes[i], id); });
        break;
    case 4:
        Diff([&] { rp = p->OnClick(id); }, [&] { ro = Slot<IntFn>(i, 0x38)(g_origNodes[i], id); });
        break;
    default: {
        const bool on = P(0.5);
        Diff([&] { p->SetInactive(on); }, [&] { Slot<VoidIntFn>(i, 0x7C)(g_origNodes[i], on); });
        break;
    }
    }
    CHECK(rp == ro, "half slot (%s): %d / %d", g_nodeKinds[i]->name, ro, rp);
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX, CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_ADD_TEXT, CONTROL_MSG_CLEAR, CONTROL_MSG_GET_RUNS, CONTROL_MSG_SET_MARGIN, CONTROL_MSG_FACE_FRONT,
               CONTROL_MSG_GET_CHECKED, CONTROL_MSG_SET_CHECKED_QUIET, CONTROL_MSG_GET_SIDE, CONTROL_MSG_SET_NUMBER, 0x1000000,
               0x2000001, 0};
    // (Checking toggles notify CHANGED, the side's change too: loud.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SET_CHECKED, CONTROL_MSG_SET_SIDE};
    g_tickKinds = {0, K_SIDE};
    g_messageHook = MessageValues;
    g_afterTree = WireChildren;
    g_beforeCreate = CreateFrames;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_CHECK_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("check_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_CHECK_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_CHECK_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_CHECK_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Halves", TestHalves},
    };
    for (const S_Case &c : cases) {
        if (only && std::strcmp(only, c.name) != 0)
            continue;
        g_case = c.name;
        for (g_round = 0; g_round < rounds; ++g_round) {
            RandomTree();
            c.run();
        }
    }
    std::printf("check_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
