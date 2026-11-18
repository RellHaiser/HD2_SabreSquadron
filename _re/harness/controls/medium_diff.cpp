// Human part 50: the paged text control and the pages, the slider, the scroll bar and the spin control (game/controls.cpp
// lines 766-1518) against the original's. MinGW; build_medium.bat; runs medium_diff.exe ("medium_diff: N checks, 0
// failures"). The kit (controls_kit.inc, controls_tree.inc) fakes what the controls reach outside themselves on both sides
// and compares the trees, the message queue, the engine's fakes and the calls on them; the arrows and sliders a Create
// makes are adopted into the trees and compared too.
// HD2_MEDIUM_ROUNDS: rounds per kind of call (default 20,000; HD2_MEDIUM_QUICK: 4,000); HD2_MEDIUM_CASE: one only;
// HD2_MEDIUM_FIRST_CHANCE: faults under the original's exception frames shown.
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
std::string FloatText(float f)
{
    char text[24];
    std::snprintf(text, sizeof text, "%08x", Bits(f));
    return text;
}
bool SameFloat(float a, float b) { return Bits(a) == Bits(b); }

// The button (part 49's): +0x6C the cursor, +0x70 normal, +0x74 active, +0x78 inactive, +0x7C held, +0x80 the repeat time.
void RandomButton(C_Control *c)
{
    auto *b = static_cast<C_Button *>(c);
    b->m_cursor = TheCursor();
    b->m_normalFrame = RandomFrame();
    b->m_activeFrame = RandomFrame();
    b->m_inactiveFrame = RandomFrame();
    b->m_held = P(0.5);
    b->m_repeatTime = P(0.5) ? Pick({300, -1, 0}) : I(-5, 2000);
}
void ButtonToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_Button *>(c);
    OF<void *>(o, 0x6C) = b->m_cursor;
    OF<void *>(o, 0x70) = b->m_normalFrame;
    OF<void *>(o, 0x74) = b->m_activeFrame;
    OF<void *>(o, 0x78) = b->m_inactiveFrame;
    OF<bool>(o, 0x7C) = b->m_held;
    OF<int>(o, 0x80) = b->m_repeatTime;
}
std::string ButtonDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_Button *>(c);
    // (A button the call made: the original's inlined constructor leaves its repeat time unset, read only after OnPress
    // has set it; the port's 0.)
    const bool repeatSame = g_differsFresh || OF<int>(o, 0x80) == b->m_repeatTime;
    if (PtrName(OF<void *>(o, 0x6C)) != PtrName(b->m_cursor) || OF<void *>(o, 0x70) != b->m_normalFrame || OF<void *>(o, 0x74) != b->m_activeFrame ||
        OF<void *>(o, 0x78) != b->m_inactiveFrame || OF<bool>(o, 0x7C) != b->m_held || !repeatSame) {
        char text[200];
        std::snprintf(text, sizeof text, "button %s %s %s %s %d %d / %s %s %s %s %d %d", PtrName(OF<void *>(o, 0x6C)).c_str(),
                      PtrName(OF<void *>(o, 0x70)).c_str(), PtrName(OF<void *>(o, 0x74)).c_str(), PtrName(OF<void *>(o, 0x78)).c_str(),
                      OF<bool>(o, 0x7C), OF<int>(o, 0x80), PtrName(b->m_cursor).c_str(), PtrName(b->m_normalFrame).c_str(),
                      PtrName(b->m_activeFrame).c_str(), PtrName(b->m_inactiveFrame).c_str(), b->m_held, b->m_repeatTime);
        return text;
    }
    return "";
}

// The paged text controls: +0x6C the paged box.
C_TextBox *PagedBox(C_Control *c) { return static_cast<C_PagedTextControl *>(c)->m_box; }
void RandomPaged(C_Control *c) { RandomPagedBox(static_cast<C_PagedTextControl *>(c)->m_box); }
void PagedToOrig(C_Control *c, uint8_t *o) { BoxToOrig(static_cast<C_PagedTextControl *>(c)->m_box, o, O_BOX); }
std::string PagedDiffers(C_Control *c, const uint8_t *o) { return BoxDiffers(static_cast<C_PagedTextControl *>(c)->m_box, o, O_BOX); }

// The slider: +0x6C the cursor, +0x70 normal, +0x74 active, +0x78 base, +0x7C min x, +0x80 min y, +0x84 max x, +0x88 max y,
// +0x8C range x, +0x90 range y, +0x98 horizontal, +0x99 vertical, +0x9C the value.
float RandomPlace() { return P(0.1) ? Pick({0.0f, 1.0f, -0.75f, -1.0f, 0.5f}) : F(-1.2f, 1.2f); }
void RandomSlider(C_Control *c)
{
    auto *s = static_cast<C_Slider *>(c);
    s->m_cursor = TheCursor();
    s->m_normalFrame = RandomFrame();
    s->m_activeFrame = RandomFrame();
    s->m_baseFrame = RandomFrame();
    s->m_minX = RandomPlace();
    s->m_minY = RandomPlace();
    s->m_maxX = P(0.9) ? s->m_minX + F(0, 1) : RandomPlace();
    s->m_maxY = P(0.9) ? s->m_minY + F(0, 1) : RandomPlace();
    s->m_rangeX = P(0.9) ? s->m_maxX - s->m_minX : P(0.5) ? 0.0f : RandomPlace();
    s->m_rangeY = P(0.9) ? s->m_maxY - s->m_minY : P(0.5) ? 0.0f : RandomPlace();
    s->m_horizontal = P(0.5);
    s->m_vertical = P(0.9) ? !s->m_horizontal : P(0.5);
    s->m_value = P(0.3) ? Pick({0.0f, 50.0f, 100.0f, 33.333336f}) : F(-10, 110);
}
void SliderToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_Slider *>(c);
    OF<void *>(o, 0x6C) = s->m_cursor;
    OF<void *>(o, 0x70) = s->m_normalFrame;
    OF<void *>(o, 0x74) = s->m_activeFrame;
    OF<void *>(o, 0x78) = s->m_baseFrame;
    OF<float>(o, 0x7C) = s->m_minX;
    OF<float>(o, 0x80) = s->m_minY;
    OF<float>(o, 0x84) = s->m_maxX;
    OF<float>(o, 0x88) = s->m_maxY;
    OF<float>(o, 0x8C) = s->m_rangeX;
    OF<float>(o, 0x90) = s->m_rangeY;
    OF<bool>(o, 0x98) = s->m_horizontal;
    OF<bool>(o, 0x99) = s->m_vertical;
    OF<float>(o, 0x9C) = s->m_value;
}
std::string SliderDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_Slider *>(c);
    if (PtrName(OF<void *>(o, 0x6C)) != PtrName(s->m_cursor) || OF<void *>(o, 0x70) != s->m_normalFrame || OF<void *>(o, 0x74) != s->m_activeFrame ||
        OF<void *>(o, 0x78) != s->m_baseFrame)
        return "slider frames";
    const float of[] = {OF<float>(o, 0x7C), OF<float>(o, 0x80), OF<float>(o, 0x84), OF<float>(o, 0x88), OF<float>(o, 0x8C), OF<float>(o, 0x90),
                        OF<float>(o, 0x9C)};
    const float pf[] = {s->m_minX, s->m_minY, s->m_maxX, s->m_maxY, s->m_rangeX, s->m_rangeY, s->m_value};
    static const char *const names[] = {"min x", "min y", "max x", "max y", "range x", "range y", "value"};
    for (int k = 0; k < 7; ++k)
        if (!SameFloat(of[k], pf[k]))
            return std::string("slider ") + names[k] + " " + FloatText(of[k]) + " / " + FloatText(pf[k]);
    if (OF<bool>(o, 0x98) != s->m_horizontal || OF<bool>(o, 0x99) != s->m_vertical)
        return "slider axes";
    return "";
}

// The scroll bar: +0x6C the repeat time, +0x70 held, +0x74 up, +0x78 down, +0x7C the slider, +0x80 count, +0x84 position,
// +0x88 step, +0x8C focus, +0x90 the page step.
void RandomScroll(C_Control *c)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    s->m_repeatTime = P(0.3) ? Pick({0, 50, 300, -1}) : I(-50, 400);
    s->m_held = P(0.5);
    s->m_count = P(0.2) ? 0 : P(0.8) ? I(1, 12) : I(13, 300);
    s->m_position = s->m_count ? I(0, s->m_count) : 0;
    s->m_step = s->m_count > 1 ? 100.0f / float(s->m_count - 1) : 100.0f;
    // (Never 0, as SET_COUNT makes it: an infinite place sent to the original's own CRT floor takes its error path,
    // which needs the CRT the test never starts.)
    s->m_pageStep = P(0.6) ? 1 : I(0, 6);
    if (P(0.5) && !g_portNodes.empty())
        s->m_value = g_portNodes[size_t(I(0, int(g_portNodes.size()) - 1))]->m_id;
}
void ScrollToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    OF<int>(o, 0x6C) = s->m_repeatTime;
    OF<bool>(o, 0x70) = s->m_held;
    OF<uint8_t *>(o, 0x74) = s->m_upButton ? ToOrigNode(s->m_upButton) : nullptr;
    OF<uint8_t *>(o, 0x78) = s->m_downButton ? ToOrigNode(s->m_downButton) : nullptr;
    OF<uint8_t *>(o, 0x7C) = s->m_slider ? ToOrigNode(s->m_slider) : nullptr;
    OF<int>(o, 0x80) = s->m_count;
    OF<int>(o, 0x84) = s->m_position;
    OF<float>(o, 0x88) = s->m_step;
    OF<int>(o, 0x8C) = s->m_focus;
    OF<int>(o, 0x90) = s->m_pageStep;
}
std::string ScrollDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    char text[200];
    if (OF<int>(o, 0x6C) != s->m_repeatTime || OF<bool>(o, 0x70) != s->m_held || PtrName(OF<void *>(o, 0x74)) != PtrName(s->m_upButton) ||
        PtrName(OF<void *>(o, 0x78)) != PtrName(s->m_downButton) || PtrName(OF<void *>(o, 0x7C)) != PtrName(s->m_slider) ||
        OF<int>(o, 0x80) != s->m_count || OF<int>(o, 0x84) != s->m_position || !SameFloat(OF<float>(o, 0x88), s->m_step) ||
        OF<int>(o, 0x8C) != s->m_focus || OF<int>(o, 0x90) != s->m_pageStep) {
        std::snprintf(text, sizeof text, "scroll %d %d %s %s %s %d %d %s %d %d / %d %d %s %s %s %d %d %s %d %d", OF<int>(o, 0x6C), OF<bool>(o, 0x70),
                      PtrName(OF<void *>(o, 0x74)).c_str(), PtrName(OF<void *>(o, 0x78)).c_str(), PtrName(OF<void *>(o, 0x7C)).c_str(),
                      OF<int>(o, 0x80), OF<int>(o, 0x84), FloatText(OF<float>(o, 0x88)).c_str(), OF<int>(o, 0x8C), OF<int>(o, 0x90),
                      s->m_repeatTime, s->m_held, PtrName(s->m_upButton).c_str(), PtrName(s->m_downButton).c_str(), PtrName(s->m_slider).c_str(),
                      s->m_count, s->m_position, FloatText(s->m_step).c_str(), s->m_focus, s->m_pageStep);
        return text;
    }
    return "";
}

// The spin control: the box, +0x70 up, +0x74 down, +0x78 its items (an MSVC6 vector of C_str), +0x8C selected, +0x90 focus.
const char *const ITEMS[] = {"one", "two", "", "four"};
void RandomSpin(C_Control *c)
{
    auto *s = static_cast<C_SpinControl *>(c);
    RandomBox(s->m_box);
    s->m_items.clear();
    // (At least one: stepping through none, the original shows the item past the end; the port's fix shows nothing.)
    for (int k = I(1, 4); k > 0; --k)
        s->m_items.push_back(C_str(Pick(ITEMS)));
    s->m_selected = I(-1, int(s->m_items.size()));
}
void SpinToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_SpinControl *>(c);
    BoxToOrig(s->m_box, o, O_BOX);
    OF<uint8_t *>(o, 0x70) = s->m_upButton ? ToOrigNode(s->m_upButton) : nullptr;
    OF<uint8_t *>(o, 0x74) = s->m_downButton ? ToOrigNode(s->m_downButton) : nullptr;
    std::vector<uint32_t> reps;
    for (const C_str &item : s->m_items)
        reps.push_back(OrigText(item));
    OVecPut(o, 0x78, reps.data(), reps.size() * 4);
    OF<int>(o, 0x8C) = s->m_selected;
    OF<int>(o, 0x90) = s->m_focus;
}
std::string SpinDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_SpinControl *>(c);
    if (PtrName(OF<void *>(o, 0x70)) != PtrName(s->m_upButton) || PtrName(OF<void *>(o, 0x74)) != PtrName(s->m_downButton))
        return "spin arrows";
    if (OVecBytes(o, 0x78) != s->m_items.size() * 4)
        return "spin items " + std::to_string(OVecBytes(o, 0x78) / 4) + " / " + std::to_string(s->m_items.size());
    for (size_t k = 0; k < s->m_items.size(); ++k)
        if (TextOf(reinterpret_cast<uint32_t *>(OVecData(o, 0x78))[k]) != TextOf(s->m_items[k]))
            return "spin item " + std::to_string(k) + ": " + TextOf(reinterpret_cast<uint32_t *>(OVecData(o, 0x78))[k]) + " / " + TextOf(s->m_items[k]);
    if (OF<int>(o, 0x8C) != s->m_selected || OF<int>(o, 0x90) != s->m_focus)
        return "spin selected " + std::to_string(OF<int>(o, 0x8C)) + " focus " + std::to_string(OF<int>(o, 0x90)) + " / " +
               std::to_string(s->m_selected) + " " + std::to_string(s->m_focus);
    return BoxDiffers(s->m_box, o, O_BOX);
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"button", 0x00813f68, 0x84, Make<C_Button>, RandomButton, ButtonToOrig, ButtonDiffers},
    {"paged", 0x00815420, 0x70, Make<C_PagedTextControl>, RandomPaged, PagedToOrig, PagedDiffers, O_BOX, PagedBox},
    {"pages", 0x00814d38, 0x70, Make<C_PagesControl>, RandomPaged, PagedToOrig, PagedDiffers, O_BOX, PagedBox},
    {"slider", 0x00815288, 0xA0, Make<C_Slider>, RandomSlider, SliderToOrig, SliderDiffers},
    {"scroll", 0x00814210, 0x94, Make<C_ScrollBar>, RandomScroll, ScrollToOrig, ScrollDiffers},
    {"spin", 0x00815398, 0x94, Make<C_SpinControl>, RandomSpin, SpinToOrig, SpinDiffers, O_BOX, nullptr, true},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_BUTTON = 1, K_SLIDER = 4, K_SCROLL = 5, K_SPIN = 6;

}  // namespace

namespace {
#include "controls_tree.inc"

// The scroll bars' arrows and slider, the spin controls' arrows: child nodes (the original without them crashes: the
// port's fixes check them), their ids the owner's with 1, 2, 3.
C_Control *AddChildNode(C_Control *owner, int kind, uint32_t last)
{
    C_Control *c = KINDS[kind].make(owner->m_scene, owner->m_id | last, owner);
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
void WireChildren()
{
    g_cursorMade = true;   // (the scroll bar asks for the cursor in its tick: the game's always is)
    const size_t n = g_portNodes.size();
    for (size_t i = 0; i < n; ++i) {
        C_Control *c = g_portNodes[i];
        if (g_nodeKinds[i] == &KINDS[K_SCROLL]) {
            auto *s = static_cast<C_ScrollBar *>(c);
            s->m_upButton = static_cast<C_Button *>(AddChildNode(c, K_BUTTON, 1));
            s->m_downButton = static_cast<C_Button *>(AddChildNode(c, K_BUTTON, 2));
            // (Without a slider now and then: its arrows enabled by the position instead.)
            s->m_slider = P(0.85) ? static_cast<C_Slider *>(AddChildNode(c, K_SLIDER, 3)) : nullptr;
            s->m_focus = I(-1, int(c->m_children.size()));
        } else if (g_nodeKinds[i] == &KINDS[K_SPIN]) {
            auto *s = static_cast<C_SpinControl *>(c);
            s->m_upButton = static_cast<C_Button *>(AddChildNode(c, K_BUTTON, 1));
            s->m_downButton = static_cast<C_Button *>(AddChildNode(c, K_BUTTON, 2));
            s->m_focus = I(-1, int(c->m_children.size()));
        }
    }
}

// Create's frames: a scroll bar's or spin control's arrows (three children each), a scroll bar's slider (its three).
void CompleteFrame(int f, std::initializer_list<int> names)
{
    if (f < 0)
        return;
    g_arena.numChildren[f] = 3;
    for (int k : names)
        if (g_arena.child[f][k] < 0)
            g_arena.child[f][k] = static_cast<int16_t>(I(0, 15));
}
void CreateFrames(size_t node, int fi)
{
    const S_Kind *k = g_nodeKinds[node];
    if (k == &KINDS[K_BUTTON] && g_arena.numChildren[fi] == 3)
        CompleteFrame(fi, {0, 1, 2});
    if (k == &KINDS[K_SCROLL] || k == &KINDS[K_SPIN]) {
        for (int a : {3, 4}) {
            if (g_arena.child[fi][a] < 0)
                g_arena.child[fi][a] = static_cast<int16_t>(I(0, 15));
            CompleteFrame(g_arena.child[fi][a], {0, 1, 2});
        }
        if (k == &KINDS[K_SCROLL] && g_arena.child[fi][5] >= 0)
            CompleteFrame(g_arena.child[fi][5], {0, 1, 6});
    }
    if (k == &KINDS[K_SLIDER])
        CompleteFrame(fi, {0, 1, 6});
}

const char *const AREA_NAMES[] = {"page*", "normal*", "x"};
// The slider's messages only to scroll bars with one (the original's without one crashes on them: GET_SLIDER is the
// port's fix, SLIDER_MOVED crashes both); routes never carry them (noRouteCodes).
bool SliderMessageFits(size_t node, const S_ControlMessage &m)
{
    const uint32_t code = m.id & CONTROL_MSG_MASK;
    if (g_nodeKinds[node] != &KINDS[K_SCROLL] || (code != CONTROL_MSG_GET_SLIDER && code != CONTROL_MSG_SLIDER_MOVED))
        return true;
    return static_cast<C_ScrollBar *>(g_portNodes[node])->m_slider != nullptr;
}
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_SLIDER:
        m.value2 = Bits(P(0.3) ? Pick({0.0f, 50.0f, 100.0f, -1.0f, 101.0f}) : F(-20, 120));
        break;
    case CONTROL_MSG_SET_COUNT:
        // (Counts past 2^24 too: the step's rounding.)
        // (An odd divisor from 2^24 to 2^25: the count's float inexact.)
        m.value = uint32_t(P(0.7) ? I(-1, 14) : P(0.5) ? I(15, 300) : P(0.5) ? (1 << 24) + 2 * I(1, 1 << 22) : int(U32()));
        break;
    case CONTROL_MSG_SET_POSITION:
    case CONTROL_MSG_SELECT_ITEM:
    case CONTROL_MSG_GET_ITEM:
    case CONTROL_MSG_REMOVE_ITEM:   // (REMOVE_RUN's code too)
    case CONTROL_MSG_SET_PAGE_STEP:
        m.value = uint32_t(P(0.9) ? I(-1, 12) : int(U32()));
        break;
    case CONTROL_MSG_ADD_ITEM:   // (SET_PAGE's code too)
        m.value = P(0.5) ? 0 : uint32_t(I(1, 20));
        m.text = Pick(ITEMS);
        break;
    case CONTROL_MSG_ADD_AREA:
        m.text = Pick(AREA_NAMES);
        break;
    }
}

// Scrolling: the scroll bars' own messages sent to them (up and down past the ends, the count, the position, the slider).
void TestScroll()
{
    RandomTree({0, K_SCROLL, K_SPIN});
    std::vector<size_t> bars;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_SCROLL])
            bars.push_back(i);
    if (bars.empty())
        return;
    const size_t i = bars[size_t(I(0, int(bars.size()) - 1))];
    S_ControlMessage m;
    m.target = 0xFFFFFFFF;
    const uint32_t codes[] = {CONTROL_MSG_SCROLL_UP, CONTROL_MSG_SCROLL_DOWN, CONTROL_MSG_SET_COUNT, CONTROL_MSG_SET_POSITION,
                              CONTROL_MSG_SET_PAGE_STEP, CONTROL_MSG_GET_POSITION, CONTROL_MSG_SLIDER_MOVED, CONTROL_MSG_GET_SLIDER};
    m.id = Pick(codes);
    m.value = uint32_t(P(0.9) ? I(-1, 14) : int(U32()));
    if (!SliderMessageFits(i, m))
        m.id = CONTROL_MSG_SCROLL_DOWN;
    auto *bar = static_cast<C_ScrollBar *>(g_portNodes[i]);
    // (Its frame's world bound square now and then: StepToCursor's axis at the tie.)
    if (P(0.2)) {
        uint8_t *f = reinterpret_cast<uint8_t *>(bar->m_frame);
        const S_vector lo = *reinterpret_cast<S_vector *>(f + 0x170);
        const float side = F(0.1f, 1.0f);
        *reinterpret_cast<S_vector *>(f + 0x180) = S_vector(lo.x + side, lo.y + side, lo.z + 0.1f);
    }
    // A long list now and then (a step under 2: the clamp of a place before the start shows), the cursor before it.
    if (P(0.4)) {
        bar->m_count = I(51, 400);
        bar->m_step = 100.0f / static_cast<float>(bar->m_count - 1);
        // (At 0 or 1 mostly: a step toward a place near the start differs only there.)
        bar->m_position = P(0.7) ? I(0, 1) : I(0, bar->m_count - 1);
        const uint8_t *f = reinterpret_cast<const uint8_t *>(bar->m_frame);
        const S_vector lo = *reinterpret_cast<const S_vector *>(f + 0x170), hi = *reinterpret_cast<const S_vector *>(f + 0x180);
        // (Before the start, or just inside it: a place under 1 % of the way.)
        if (P(0.5))
            g_arena.cursorPos = S_vector(lo.x - F(0, 0.3f), hi.y + F(0, 0.3f), -1.0f);
        else
            g_arena.cursorPos = S_vector(lo.x + (hi.x - lo.x) * F(0, 0.01f), hi.y - (hi.y - lo.y) * F(0, 0.01f), -1.0f);
    }
    // Its tick at the repeat time's end, or a press (the step to the cursor).
    const int pick = I(0, 3);
    if (pick == 0) {
        bar->m_held = true;
        g_arena.cursorDown = 1;
        const int time = bar->m_repeatTime + Pick({-1, 0, 1});
        Diff([&] { bar->Tick(time); }, [&] { Slot<VoidIntFn>(i, 0x1C)(g_origNodes[i], time); });
        return;
    }
    if (pick == 1) {
        int rp = 0, ro = 0;
        Diff([&] { rp = bar->OnPress(0); }, [&] { ro = Slot<IntFn>(i, 0x48)(g_origNodes[i], 0); });
        CHECK(rp == ro, "scroll press %d / %d", ro, rp);
        return;
    }
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    Diff([&] { rp = g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "scroll message %x: %d / %d", g_messageBefore.id, ro, rp);
    const std::string why = MessageDiffers(m, mo);
    CHECK(why.empty(), "the scroll message after: %s", why.c_str());
}

// The pages control's box taken away for the messages it checks it for.
void PagesWithoutBox(size_t node, const S_ControlMessage &m)
{
    const uint32_t code = m.id & CONTROL_MSG_MASK;
    if (g_nodeKinds[node] != &KINDS[3] || !P(0.3) || (m.id & CONTROL_NOTIFY_MASK))
        return;
    if (code == CONTROL_MSG_SET_PAGE || code == CONTROL_MSG_GET_PAGES || code == CONTROL_MSG_GET_PAGE_BREAK || code == CONTROL_MSG_ADD_AREA ||
        code == CONTROL_MSG_REFRESH) {
        auto *p = static_cast<C_PagedTextControl *>(g_portNodes[node]);
        delete p->m_box;
        p->m_box = nullptr;
    }
}

// One message straight to a node of `kind` (its result and the message after compared).
void SendTo(int kind, S_ControlMessage m)
{
    std::vector<size_t> nodes;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[kind])
            nodes.push_back(i);
    if (nodes.empty())
        return;
    const size_t i = nodes[size_t(I(0, int(nodes.size()) - 1))];
    if (!SliderMessageFits(i, m))
        return;
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

// The slider's value set (its clamps at 0 and 100, NaN now and then) and read.
void TestSlide()
{
    RandomTree({0, K_SLIDER});
    S_ControlMessage m;
    m.target = 0xFFFFFFFF;
    m.id = P(0.8) ? CONTROL_MSG_SET_SLIDER : CONTROL_MSG_GET_SLIDER;
    m.value2 = Bits(P(0.6) ? Pick({-1.0f, -0.0f, 0.0f, 100.0f, 101.0f, 50.0f, -100.0f, 1e-30f}) : P(0.05) ? std::nanf("") : F(-50, 150));
    SendTo(K_SLIDER, m);
}

// The spin control's own messages (the wraps, the removal's clamp, the choice by index).
void TestSpin()
{
    RandomTree({0, K_SPIN});
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_SPIN]) {
            auto *s = static_cast<C_SpinControl *>(g_portNodes[i]);
            s->m_inactive = P(0.2);
            s->m_selected = P(0.5) ? Pick({0, -1, int(s->m_items.size()) - 1}) : I(-1, int(s->m_items.size()) - 1);
        }
    S_ControlMessage m;
    m.target = 0xFFFFFFFF;
    const uint32_t codes[] = {CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_NEXT_ITEM, CONTROL_MSG_SELECT_ITEM, CONTROL_MSG_REMOVE_ITEM,
                              CONTROL_MSG_ADD_ITEM,      CONTROL_MSG_CLEAR_ITEMS, CONTROL_MSG_GET_SELECTED, CONTROL_MSG_GET_ITEM,
                              CONTROL_MSG_GET_SELECTED_2};
    m.id = Pick(codes);
    m.value = uint32_t(P(0.9) ? I(-1, 5) : int(U32()));
    if (m.id == CONTROL_MSG_ADD_ITEM) {
        m.value = P(0.5) ? 0 : uint32_t(I(1, 20));
        m.text = Pick(ITEMS);
    }
    SendTo(K_SPIN, m);
}

// Drags: sliders' OnInput08 (the cursor's move and place at the thresholds).
void TestDrag()
{
    RandomTree({0, K_SLIDER, K_SCROLL});
    std::vector<size_t> sliders;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_SLIDER])
            sliders.push_back(i);
    if (sliders.empty())
        return;
    const size_t i = sliders[size_t(I(0, int(sliders.size()) - 1))];
    auto *s = static_cast<C_Slider *>(g_portNodes[i]);
    if (P(0.3))
        g_arena.cursorPos = S_vector(s->m_minX + Pick({0.0f, -0.01f, 0.01f}), s->m_maxY + Pick({0.0f, -0.01f, 0.01f}), -1.0f);
    int rp = 0, ro = 0;
    if (P(0.7)) {
        Diff([&] { rp = s->OnInput08(0); }, [&] { ro = Slot<IntFn>(i, 0x50)(g_origNodes[i], 0); });
    } else {
        // SetLimits (through Create: its frames' bounds; now and then the normal frame a point, the base's at the limits).
        I3D_frame *frame = RandomFrame();
        const int fi = FrameIndex(frame);
        CompleteFrame(fi, {0, 1, 6});
        if (P(0.5)) {
            I3D_frame *normal = FrameAt(g_arena.child[fi][0]), *base = FrameAt(g_arena.child[fi][6]);
            normal->m_frameFlags |= FRMFLAGS_BOUND_VALID;
            base->m_frameFlags |= FRMFLAGS_BOUND_VALID;
            const S_vector point(Pick({0.0f, 0.25f}), Pick({0.0f, -0.25f}), 0.0f);
            normal->m_bound.bbox.min = normal->m_bound.bbox.max = point;
            const float xs[] = {0.0f, 1.0f, 0.5f, -0.5f, 1.5f, 0.25f, 1.25f};
            const float ys[] = {0.0f, -0.75f, -0.5f, 0.5f, -1.0f, -0.25f, -0.5f};
            const float x0 = Pick(xs), x1 = Pick(xs), y0 = Pick(ys), y1 = Pick(ys);
            base->m_bound.bbox.min = S_vector(std::min(x0, x1), std::min(y0, y1), 0.0f);
            base->m_bound.bbox.max = S_vector(std::max(x0, x1), std::max(y0, y1), 0.0f);
            if (P(0.5))
                normal->m_localMatrix(3) = S_vector(0.0f, normal->m_localMatrix(3).y, 0.0f);
        }
        Diff([&] { rp = s->Create(frame, frame, nullptr); }, [&] { ro = Slot<CreateFn>(i, 0x2C)(g_origNodes[i], frame, frame, nullptr); });
    }
    CHECK(rp == ro, "drag %d / %d", ro, rp);
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_codes = {CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE, CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT, CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX, CONTROL_MSG_SET_VALUE, CONTROL_MSG_GET_VALUE, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_ADD_TEXT, CONTROL_MSG_ADD_TEXT_ID, CONTROL_MSG_SET_RUN_TEXT, CONTROL_MSG_REMOVE_RUN,
               CONTROL_MSG_CLEAR_TEXT, CONTROL_MSG_CLEAR, CONTROL_MSG_GET_RUNS, CONTROL_MSG_SET_MARGIN, CONTROL_MSG_SET_PAGE,
               CONTROL_MSG_GET_PAGES, CONTROL_MSG_ADD_AREA, CONTROL_MSG_GET_PAGE_BREAK, CONTROL_MSG_SET_SLIDER, CONTROL_MSG_GET_SLIDER,
               CONTROL_MSG_SET_COUNT, CONTROL_MSG_SET_PAGE_STEP, CONTROL_MSG_GET_POSITION, CONTROL_MSG_SET_POSITION,
               CONTROL_MSG_ADD_ITEM, CONTROL_MSG_REMOVE_ITEM, CONTROL_MSG_GET_SELECTED, CONTROL_MSG_GET_ITEM, CONTROL_MSG_GET_SELECTED_2,
               CONTROL_MSG_CLEAR_ITEMS, 0x1000000, 0x2000001, 0};
    // (Scrolling, the slider's move and the spin control's choice notify CHANGED: loud.)
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SCROLL_UP, CONTROL_MSG_SCROLL_DOWN,
                   CONTROL_MSG_SLIDER_MOVED, CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_NEXT_ITEM, CONTROL_MSG_SELECT_ITEM};
    g_tickKinds = {0, K_BUTTON, K_SCROLL};
    // (A route emptying a spin control's items before another steps through them: the original's past-the-end read.)
    g_noRouteCodes = {CONTROL_MSG_CLEAR_ITEMS, CONTROL_MSG_REMOVE_ITEM, CONTROL_MSG_GET_SLIDER, CONTROL_MSG_SLIDER_MOVED};
    g_messageFits = SliderMessageFits;
    g_beforeMessage = PagesWithoutBox;
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
    if (std::getenv("HD2_MEDIUM_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("medium_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_MEDIUM_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_MEDIUM_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_MEDIUM_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Drag", TestDrag}, {"Scroll", TestScroll}, {"Slide", TestSlide}, {"Spin", TestSpin},
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
    std::printf("medium_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
