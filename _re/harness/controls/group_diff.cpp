// Human part 53: the group's shared slots, the scrolled text, the combo box's header, the combo box, the titled combo box
// and the chat input (game/controls.cpp lines 2765-3360) against the original's. MinGW; build_group.bat; runs
// group_diff.exe ("group_diff: N checks, 0 failures"). The kit (controls_kit.inc, controls_tree.inc, controls_kinds.inc)
// fakes what the controls reach outside themselves on both sides and compares the trees, the message queue, the
// engine's fakes and the calls on them. The trees are planned: each group with its parts (as its Create makes them, now
// and then one missing, another added or out of order), its fields naming them.
// HD2_GROUP_ROUNDS: rounds per kind of call (default 20,000; HD2_GROUP_QUICK: 4,000); HD2_GROUP_CASE: one only;
// HD2_GROUP_FIRST_CHANCE: faults under the original's exception frames shown.
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

#include "controls_kit.inc"

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

// ---- this part's kinds ----
// A pointer field: the original's node (null: null).
uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }
// The first child of the node of type T (null: none).
template <class T> T *ChildOf(C_Control *c)
{
    for (C_Control *k : c->m_children)
        if (auto *t = dynamic_cast<T *>(k))
            return t;
    return nullptr;
}
std::string PtrsDiffer(const uint8_t *o, std::initializer_list<std::pair<uint32_t, const void *>> fields)
{
    for (const auto &f : fields)
        if (PtrName(OF<void *>(o, f.first)) != PtrName(f.second))
            return "part +" + std::to_string(f.first) + " " + PtrName(OF<void *>(o, f.first)) + " / " + PtrName(f.second);
    return "";
}

// The scrolled text: the group's focus, +0x70 its list, +0x74 its bar.
void RandomScrolled(C_Control *c)
{
    auto *s = static_cast<C_ScrolledText *>(c);
    RandomGroup(c);
    s->m_list = ChildOf<C_SelectList>(c);
    s->m_bar = ChildOf<C_ScrollBar>(c);
}
void ScrolledToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_ScrolledText *>(c);
    GroupToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = OrigOf(s->m_list);
    OF<uint8_t *>(o, 0x74) = OrigOf(s->m_bar);
}
std::string ScrolledDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_ScrolledText *>(c);
    const std::string why = PtrsDiffer(o, {{0x70, s->m_list}, {0x74, s->m_bar}});
    return why.empty() ? GroupDiffers(c, o) : why;
}

// The combo box: the group's focus, +0x70 the button, +0x74 the list, +0x78 the header, +0x7C open, +0x80 the picked
// line, +0x84 its text. (The header's edit callback on it now and then, as its Create adds it.)
const char *const PICKED[] = {"picked", "", "Easy", "x y"};
void RandomCombo(C_Control *c)
{
    auto *b = static_cast<C_ComboBox *>(c);
    RandomGroup(c);
    b->m_button = ChildOf<C_Button>(c);
    b->m_list = ChildOf<C_ScrolledText>(c);
    b->m_header = ChildOf<C_ComboEdit>(c);
    b->m_open = P(0.5);
    b->m_picked = P(0.3) ? -1 : I(-2, 6);
    b->m_pickedText = P(0.2) ? C_str() : C_str(Pick(PICKED));
    if (b->m_header && P(0.7)) {
        S_ControlCallback cb;
        cb.notification = CONTROL_NOTIFY_0B;
        cb.object = b;
        cb.function = &C_ComboBox::HeaderEdited;
        b->m_header->m_callbacks.insert(b->m_header->m_callbacks.begin() + I(0, int(b->m_header->m_callbacks.size())), cb);
    }
}
void ComboToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_ComboBox *>(c);
    GroupToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = OrigOf(b->m_button);
    OF<uint8_t *>(o, 0x74) = OrigOf(b->m_list);
    OF<uint8_t *>(o, 0x78) = OrigOf(b->m_header);
    OF<bool>(o, 0x7C) = b->m_open;
    OF<int>(o, 0x80) = b->m_picked;
    OF<uint32_t>(o, 0x84) = OrigText(b->m_pickedText);
}
std::string ComboDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_ComboBox *>(c);
    std::string why = PtrsDiffer(o, {{0x70, b->m_button}, {0x74, b->m_list}, {0x78, b->m_header}});
    if (why.empty() && (OF<bool>(o, 0x7C) != b->m_open || OF<int>(o, 0x80) != b->m_picked))
        why = "open/picked " + std::to_string(OF<bool>(o, 0x7C)) + " " + std::to_string(OF<int>(o, 0x80)) + " / " + std::to_string(b->m_open) +
              " " + std::to_string(b->m_picked);
    if (why.empty() && TextOf(OF<uint32_t>(o, 0x84)) != TextOf(b->m_pickedText))
        why = "picked text " + TextOf(OF<uint32_t>(o, 0x84)) + " / " + TextOf(b->m_pickedText);
    return why.empty() ? GroupDiffers(c, o) : why;
}

// The titled combo box: the group's focus, +0x70 the title, +0x74 the combo box.
void RandomTitled(C_Control *c)
{
    auto *t = static_cast<C_TitledComboBox *>(c);
    RandomGroup(c);
    t->m_title = ChildOf<C_TitleControl>(c);
    t->m_combo = ChildOf<C_ComboBox>(c);
}
void TitledToOrig(C_Control *c, uint8_t *o)
{
    auto *t = static_cast<C_TitledComboBox *>(c);
    GroupToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = OrigOf(t->m_title);
    OF<uint8_t *>(o, 0x74) = OrigOf(t->m_combo);
}
std::string TitledDiffers(C_Control *c, const uint8_t *o)
{
    auto *t = static_cast<C_TitledComboBox *>(c);
    const std::string why = PtrsDiffer(o, {{0x70, t->m_title}, {0x74, t->m_combo}});
    return why.empty() ? GroupDiffers(c, o) : why;
}

// The chat input: the group's focus, +0x70 the label, +0x74 the input, +0x78 the team toggle, +0x7C to the team. (The
// input's line callback on it now and then, as its Create adds it.)
void RandomChat(C_Control *c)
{
    auto *h = static_cast<C_ChatInput *>(c);
    RandomGroup(c);
    h->m_label = ChildOf<C_LabelControl>(c);
    h->m_input = ChildOf<C_EditBox>(c);
    h->m_teamToggle = ChildOf<C_AllToggleButton>(c);
    h->m_team = P(0.5);
    if (h->m_input && P(0.7)) {
        S_ControlCallback cb;
        cb.notification = CONTROL_NOTIFY_02;
        cb.object = h;
        cb.function = &C_ChatInput::LineEntered;
        h->m_input->m_callbacks.insert(h->m_input->m_callbacks.begin() + I(0, int(h->m_input->m_callbacks.size())), cb);
    }
}
void ChatToOrig(C_Control *c, uint8_t *o)
{
    auto *h = static_cast<C_ChatInput *>(c);
    GroupToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = OrigOf(h->m_label);
    OF<uint8_t *>(o, 0x74) = OrigOf(h->m_input);
    OF<uint8_t *>(o, 0x78) = OrigOf(h->m_teamToggle);
    OF<bool>(o, 0x7C) = h->m_team;
}
std::string ChatDiffers(C_Control *c, const uint8_t *o)
{
    auto *h = static_cast<C_ChatInput *>(c);
    std::string why = PtrsDiffer(o, {{0x70, h->m_label}, {0x74, h->m_input}, {0x78, h->m_teamToggle}});
    if (why.empty() && OF<bool>(o, 0x7C) != h->m_team)
        why = "team " + std::to_string(OF<bool>(o, 0x7C)) + " / " + std::to_string(h->m_team);
    return why.empty() ? GroupDiffers(c, o) : why;
}

// The scroll bar (part 50's) with its arrows and slider among its children (the original uses the arrows unchecked).
void RandomScrollWired(C_Control *c)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    RandomScroll(c);
    for (C_Control *k : c->m_children) {
        // (By their order: their ids the bar's | 1 and 2, the bar's own last bits 4 .. 7 under a scrolled text.)
        if (auto *b = dynamic_cast<C_Button *>(k)) {
            if (!s->m_upButton)
                s->m_upButton = b;
            else if (!s->m_downButton)
                s->m_downButton = b;
        } else if (auto *l = dynamic_cast<C_Slider *>(k)) {
            s->m_slider = l;
        }
    }
    s->m_focus = I(-1, int(c->m_children.size()));
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"label", 0x00814188, 0x70, Make<C_LabelControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"button", 0x00813f68, 0x84, Make<C_Button>, RandomButton, ButtonToOrig, ButtonDiffers},
    {"slider", 0x00815288, 0xA0, Make<C_Slider>, RandomSlider, SliderToOrig, SliderDiffers},
    {"scroll", 0x00814210, 0x94, Make<C_ScrollBar>, RandomScrollWired, ScrollToOrig, ScrollDiffers},
    {"alltoggle", 0x00814100, 0x84, Make<C_AllToggleButton>, RandomToggle, ToggleToOrig, ToggleDiffers},
    {"editbox", 0x00813d48, 0x9C, Make<C_EditBox>, RandomEditBox, EditBoxToOrig, EditBoxDiffers, O_BOX, nullptr, true},
    {"select", 0x00814298, 0x98, Make<C_SelectList>, RandomSelect, SelectToOrig, SelectDiffers, O_BOX, nullptr, true},
    {"scrolled", 0x008143a8, 0x78, Make<C_ScrolledText>, RandomScrolled, ScrolledToOrig, ScrolledDiffers},
    {"comboedit", 0x00814320, 0x88, Make<C_ComboEdit>, RandomEdit, EditToOrig, EditDiffers, O_BOX, nullptr, true},
    {"combo", 0x00814e48, 0x88, Make<C_ComboBox>, RandomCombo, ComboToOrig, ComboDiffers},
    {"titled", 0x00814dc0, 0x78, Make<C_TitledComboBox>, RandomTitled, TitledToOrig, TitledDiffers},
    {"chat", 0x008145c8, 0x80, Make<C_ChatInput>, RandomChat, ChatToOrig, ChatDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_LABEL = 2, K_BUTTON = 3, K_SLIDER = 4, K_SCROLL = 5, K_TOGGLE = 6, K_EDITBOX = 7, K_SELECT = 8,
          K_SCROLLED = 9, K_COMBOEDIT = 10, K_COMBO = 11, K_TITLED = 12, K_CHAT = 13;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
// A group's parts in its Create's order (now and then two swapped), an extra child now and then (a plain control or a
// label, its id RandomTree's).
void PlanParts(int at, std::vector<std::pair<int, int>> parts, const std::vector<bool> &required);
int PlanScrolled(int parent, int bits);
int PlanCombo(int parent, int bits);
void PlanPart(int at, int kind, int bits)
{
    if (kind == K_SCROLL) {
        // (Its arrows always, its slider now and then: part 50's.)
        const int bar = PlanAdd(at, K_SCROLL, bits);
        PlanAdd(bar, K_BUTTON, 1);
        PlanAdd(bar, K_BUTTON, 2);
        PlanAdd(bar, P(0.85) ? K_SLIDER : K_CONTROL, 3);
    } else if (kind == K_SCROLLED)
        PlanScrolled(at, bits);
    else if (kind == K_COMBO)
        PlanCombo(at, bits);
    else
        PlanAdd(at, kind, bits);
}
void PlanParts(int at, std::vector<std::pair<int, int>> parts, const std::vector<bool> &required)
{
    // (A part left out has a plain control in its place: FindChild's indices name children always, as FindControl uses
    // them unchecked, the original's too.)
    std::vector<std::pair<int, int>> kept;
    for (size_t k = 0; k < parts.size(); ++k)
        kept.push_back(required[k] || P(0.85) ? parts[k] : std::make_pair(K_CONTROL, parts[k].second));
    if (kept.size() > 1 && P(0.1)) {
        const size_t a = size_t(I(0, int(kept.size()) - 1)), b = size_t(I(0, int(kept.size()) - 1));
        std::swap(kept[a], kept[b]);
    }
    if (P(0.15))
        kept.push_back({P(0.5) ? K_CONTROL : K_LABEL, P(0.5) ? -1 : I(0, 15)});
    for (const auto &p : kept)
        PlanPart(at, p.first, p.second);
}
// (The parts the original uses unchecked always made: the scrolled text's list, the combo box's three, the titled combo
// box's two, the chat input's label and toggle.)
int PlanScrolled(int parent, int bits)
{
    const int at = PlanAdd(parent, K_SCROLLED, bits);
    PlanParts(at, {{K_SELECT, 8}, {K_SCROLL, Pick({4, 5, 6, 7})}}, {true, false});
    return at;
}
int PlanCombo(int parent, int bits)
{
    const int at = PlanAdd(parent, K_COMBO, bits);
    PlanParts(at, {{K_SCROLLED, P(0.8) ? 0 : I(4, 15)}, {K_BUTTON, 1}, {K_COMBOEDIT, 2}}, {true, true, true});
    return at;
}
int PlanTitled(int parent, int bits)
{
    const int at = PlanAdd(parent, K_TITLED, bits);
    PlanParts(at, {{K_TITLE, 0}, {K_COMBO, 0}}, {true, true});
    return at;
}
int PlanChat(int parent, int bits)
{
    const int at = PlanAdd(parent, K_CHAT, bits);
    PlanParts(at, {{K_EDITBOX, 4}, {K_LABEL, 8}, {K_TOGGLE, Pick({0xC, 0xD, 0xF})}}, {false, true, true});
    return at;
}
int PlanGroup(int parent)
{
    switch (I(0, 3)) {
    case 0:
        return PlanScrolled(parent, -1);
    case 1:
        return PlanCombo(parent, -1);
    case 2:
        return PlanTitled(parent, -1);
    default:
        return PlanChat(parent, -1);
    }
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.5)) {
        PlanGroup(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 2); k > 0; --k)
            PlanGroup(root);
    }
    return g_plan;
}

// Create's frames: each part's frame there now and then (the parts are optional), made complete for its own Create (the
// arrows' and the slider's three children, the toggle's four, the edit box's two: parts 49-52's).
void Complete(int f, std::initializer_list<const char *> names, int count)
{
    if (f < 0)
        return;
    if (count)
        g_arena.numChildren[f] = count;
    for (const char *name : names)
        if (g_arena.child[f][ChildName(name)] < 0)
            g_arena.child[f][ChildName(name)] = static_cast<int16_t>(I(0, 15));
}
int PartFrame(int f, const char *name, bool always = false)
{
    int16_t &child = g_arena.child[f][ChildName(name)];
    if (child < 0 && (always || P(0.8)))
        child = static_cast<int16_t>(I(0, 15));
    return child;
}
void CompleteFor(int kind, int f, int depth)
{
    if (f < 0 || depth > 4)
        return;
    switch (kind) {
    case K_BUTTON:
        Complete(f, {"normal*", "actived*", "inactived*"}, 3);
        break;
    case K_SLIDER:
        Complete(f, {"normal*", "actived*", "sliderbase*"}, 3);
        break;
    case K_SCROLL:
        CompleteFor(K_BUTTON, PartFrame(f, "barrow up*", true), depth + 1);
        CompleteFor(K_BUTTON, PartFrame(f, "barrow down*", true), depth + 1);
        if (g_arena.child[f][ChildName("slider*")] >= 0)
            CompleteFor(K_SLIDER, g_arena.child[f][ChildName("slider*")], depth + 1);
        break;
    case K_TOGGLE:
        Complete(f, {"allactived", "allnormal", "xallactived", "xallnormal"}, 4);
        break;
    case K_EDITBOX:
        Complete(f, {"normal*", "actived*"}, 0);
        break;
    case K_SCROLLED:
        PartFrame(f, "text*");
        CompleteFor(K_SCROLL, PartFrame(f, "scroll*"), depth + 1);
        break;
    case K_COMBO:
        CompleteFor(K_SCROLLED, PartFrame(f, "listbox*"), depth + 1);
        CompleteFor(K_BUTTON, PartFrame(f, "cbdown*"), depth + 1);
        PartFrame(f, "header*");
        break;
    case K_TITLED:
        PartFrame(f, "text18");
        CompleteFor(K_COMBO, PartFrame(f, "game difficulty"), depth + 1);
        break;
    case K_CHAT:
        CompleteFor(K_EDITBOX, PartFrame(f, "binput*"), depth + 1);
        PartFrame(f, "text*");
        CompleteFor(K_TOGGLE, PartFrame(f, "bTeam"), depth + 1);
        break;
    }
}
void CreateFrames(size_t node, int fi) { CompleteFor(int(g_nodeKinds[node] - KINDS), fi, 0); }
// (The scroll bar asks for the cursor in its tick: the game's always is. The chat input's label near its 8 lines now and
// then: the oldest removed past them.)
void AfterTree()
{
    g_cursorMade = true;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_CHAT]) {
            auto *h = static_cast<C_ChatInput *>(g_portNodes[i]);
            if (h->m_label && h->m_label->m_box && P(0.5))
                h->m_label->m_box->m_runs.resize(size_t(I(7, 10)));
        }
}

// The nodes of a kind (empty: none).
std::vector<size_t> NodesOf(std::initializer_list<int> kinds)
{
    std::vector<size_t> out;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        for (int k : kinds)
            if (g_nodeKinds[i] == &KINDS[k])
                out.push_back(i);
    return out;
}
size_t PickOf(const std::vector<size_t> &nodes) { return nodes[size_t(I(0, int(nodes.size()) - 1))]; }
// A target for a group: its id with any last 4 bits, everyone's, another node's, the 0xF.
uint32_t GroupTarget(size_t i)
{
    if (P(0.2))
        return 0xFFFFFFFF;
    if (P(0.6))
        return (g_portNodes[i]->m_id & ~0xFu) | uint32_t(I(0, 15));
    return RandomId();
}

// ---- the round's state and the messages' values ----
void PickRound()
{
    SetUnicode(P(0.4));
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.insert = P(0.8);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.selectRun = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
    g_box.centreX = F(-1, 1);
    g_box.centreY = P(0.3) ? 0.0f : F(-1, 1);
    static const char *const selected[] = {"sel", "", "12", "-3", "abc", "line two", "  /t hi", "/a x"};
    g_box.selected = Pick(selected);
}

const char *const TEXTS[] = {"run3", "RUN0", "Run3", "nothing", "", "12", "Hard"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    case CONTROL_MSG_SET_MAX_LENGTH:
        m.value = uint32_t(P(0.8) ? I(0, 40) : 30);
        break;
    case CONTROL_MSG_GET_ITEM:
    case CONTROL_MSG_PICK_INDEX:
    case CONTROL_MSG_SET_POSITION:
        m.value = uint32_t(P(0.8) ? I(-2, 8) : int(U32()));
        m.value2 = uint32_t(I(-100, 1000));
        break;
    case CONTROL_MSG_SET_RUN_TEXT:
        m.value2 = uint32_t(I(-1, 8));
        m.text = Pick(TEXTS);
        break;
    case CONTROL_MSG_ADD_TEXT_ID:
    case CONTROL_MSG_SET_TEXT_ID:
    case CONTROL_MSG_SET_HEADER_TEXT_ID:
        m.value2 = uint32_t(Pick({2002, 1, 77, 0}));
        break;
    case CONTROL_MSG_SELECT_TEXT:
    case CONTROL_MSG_INSERT_FIRST:
    case CONTROL_MSG_ADD_TEXT:
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_SET_HEADER_TEXT:
        m.text = Pick(TEXTS);
        break;
    case CONTROL_MSG_SET_VALUE:
        if (P(0.5) && !g_portNodes.empty())
            m.value = g_portNodes[RandomNode()]->m_id;
        break;
    }
}

// The chat input's line value (CONTROL_NOTIFY_SEND's: a pointer to its text) shown as its text.
std::string CbValueText(uint32_t notification, uint32_t value)
{
    char text[16];
    if (notification == CONTROL_NOTIFY_SEND && value)
        return "'" + std::string(reinterpret_cast<const char *>(uintptr_t(value))) + "'";
    std::snprintf(text, sizeof text, "%x", value);
    return text;
}

// ---- the tests ----
const uint32_t OWN_CODES[] = {CONTROL_MSG_IGNORE_INPUT,  CONTROL_MSG_ADD_CALLBACK,     CONTROL_MSG_SET_VALUE,  CONTROL_MSG_ADD_TEXT,
                              CONTROL_MSG_ADD_TEXT_ID,   CONTROL_MSG_SET_RUN_TEXT,     CONTROL_MSG_GET_ITEM,   CONTROL_MSG_INSERT_FIRST,
                              CONTROL_MSG_EDIT,          CONTROL_MSG_REMOVE_SELECTED,  CONTROL_MSG_CLEAR,      CONTROL_MSG_PREVIOUS_ITEM,
                              CONTROL_MSG_NEXT_ITEM,     CONTROL_MSG_SET_POSITION,     CONTROL_MSG_REFRESH,    CONTROL_MSG_GET_TEXT,
                              CONTROL_MSG_SET_MARGIN,    CONTROL_MSG_SELECT_TEXT,      CONTROL_MSG_PICK_INDEX, CONTROL_MSG_SET_HEADER_TEXT,
                              CONTROL_MSG_SET_HEADER_TEXT_ID, CONTROL_MSG_TOGGLE_LIST, CONTROL_MSG_PICK_SELECTED, CONTROL_MSG_DEACTIVATE,
                              CONTROL_MSG_ACTIVATE,      CONTROL_MSG_SHOW,             CONTROL_MSG_HIDE,       CONTROL_MSG_SET_TEXT,
                              CONTROL_MSG_SET_TEXT_ID,   CONTROL_MSG_TEAM_TOGGLED,     CONTROL_MSG_GET_RUNS,   CONTROL_MSG_TAKE_INPUT,
                              CONTROL_MSG_GET_VALUE,     CONTROL_MSG_END_EDIT,         0x1000000,              0x2000001};
// The groups' own messages sent straight to them (their targets' last 4 bits any: to a part, or not).
void TestOwn()
{
    const std::vector<size_t> nodes = NodesOf({K_SCROLLED, K_COMBOEDIT, K_COMBO, K_TITLED, K_CHAT});
    if (nodes.empty())
        return;
    const size_t i = PickOf(nodes);
    S_ControlMessage m;
    m.target = GroupTarget(i);
    m.id = Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    if ((m.id & CONTROL_NOTIFY_MASK) == 0 && (m.id & CONTROL_MSG_MASK) == CONTROL_MSG_NOTIFY)
        m.value = Pick(NOTIFY_RAISED);
    if (P(0.3) && !m.text.m_rep)
        m.text = P(0.5) ? "hello" : "";
    g_messageBefore = m;
    uint8_t mo[0x18];
    bool rp = false, ro = false;
    const bool copy = P(0.2);
    Diff([&] { rp = copy ? g_portNodes[i]->HandleMessageCopy(m) : g_portNodes[i]->HandleMessage(&m); },
         [&] {
             MessageToOrig(g_messageBefore, mo);
             ro = Slot<MsgFn>(i, copy ? 0x74 : 0x6C)(g_origNodes[i], mo);
         });
    CHECK(rp == ro, "message %x to %s: %d / %d", g_messageBefore.id, g_nodeKinds[i]->name, ro, rp);
    if (!copy) {
        const std::string why = MessageDiffers(m, mo);
        CHECK(why.empty(), "the message after (%s): %s", g_nodeKinds[i]->name, why.c_str());
    }
}

// The groups' input slots with ids naming their parts (any last 4 bits), the wheel's amounts at its steps' edges.
void TestInput()
{
    const std::vector<size_t> nodes = NodesOf({K_SCROLLED, K_COMBOEDIT, K_COMBO, K_TITLED, K_CHAT});
    if (nodes.empty())
        return;
    const size_t i = PickOf(nodes);
    C_Control *p = g_portNodes[i];
    const int id = int(GroupTarget(i));
    // (A titled combo box without a part now and then: FindChild and SetInactive check them, the original's too.)
    if (g_nodeKinds[i] == &KINDS[K_TITLED] && P(0.3)) {
        auto *t = static_cast<C_TitledComboBox *>(p);
        if (P(0.5))
            t->m_title = nullptr;
        else
            t->m_combo = nullptr;
        int rp = 0, ro = 0;
        if (P(0.5)) {
            Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
        } else {
            const bool on = P(0.5);
            Diff([&] { p->SetInactive(on); }, [&] { Slot<VoidIntFn>(i, 0x7C)(g_origNodes[i], on); });
        }
        CHECK(rp == ro, "titled without a part %x: %d / %d", unsigned(id), ro, rp);
        return;
    }
    // (A focus now and then: the group's.)
    if (auto *g = dynamic_cast<C_ControlGroup *>(p))
        if (P(0.5))
            g->m_focus = I(-1, int(g->m_children.size()));
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter},   {0x34, &C_Control::OnLeave},    {0x38, &C_Control::OnClick}, {0x3C, &C_Control::OnInput02},
        {0x40, &C_Control::OnInput05}, {0x48, &C_Control::OnPress},    {0x5C, &C_Control::OnKeyClick},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 1);
    int rp = 0, ro = 0;
    if (pick < n) {
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else if (pick == n) {
        const int amount = P(0.2) ? I(-3, 3) : (P(0.5) ? 1 : -1) * (Pick({1, 119, 120, 121, 239, 240, 241, 600}));
        Diff([&] { rp = p->OnWheel(id, amount); }, [&] { ro = Slot<Int2Fn>(i, 0x4C)(g_origNodes[i], id, amount); });
    } else {
        Diff([&] { rp = p->Unknown20(id); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], id); });
    }
    CHECK(rp == ro, "input slot %d (%s) %x: %d / %d", pick, g_nodeKinds[i]->name, unsigned(id), ro, rp);
}

// SetStrings (the scrolled text's, the combo box's): the lines, none now and then.
using StringsFn = void(__attribute__((thiscall)) *)(void *, void *);
const char *const LINES[] = {"Easy", "Normal", "", "Hard", "line"};
void TestStrings()
{
    const std::vector<size_t> nodes = NodesOf({K_SCROLLED, K_COMBO, K_TITLED, K_CHAT});
    if (nodes.empty())
        return;
    const size_t i = PickOf(nodes);
    std::vector<C_str> lines;
    for (int k = P(0.2) ? 0 : I(1, 5); k > 0; --k)
        lines.push_back(P(0.05) ? C_str() : C_str(Pick(LINES)));
    // (Also a combo box's list's list taken away: SetStrings checks the list.)
    if (g_nodeKinds[i] == &KINDS[K_SCROLLED] && P(0.1))
        static_cast<C_ScrolledText *>(g_portNodes[i])->m_list = nullptr;
    if (g_nodeKinds[i] == &KINDS[K_COMBO] && P(0.1))
        static_cast<C_ComboBox *>(g_portNodes[i])->m_list = nullptr;
    Diff([&] { g_portNodes[i]->SetStrings(lines); },
         [&] {
             auto *v = static_cast<uint8_t *>(OrigAlloc(16));
             std::vector<uint32_t> reps;
             for (const C_str &s : lines)
                 reps.push_back(OrigText(s));
             OVecPut(v, 0, reps.data(), reps.size() * 4);
             Slot<StringsFn>(i, 0x78)(g_origNodes[i], v);
         });
}

// The callbacks called straight: the combo box's header edited, the chat input's line entered (its control: the input,
// another node, none; the input's text: spaces first, a command, empty, bytes past 0x7F).
using CallbackFn = bool(__attribute__((thiscall)) *)(void *, void *, void *, uint32_t, uint32_t);
const char *const CHAT_LINES[] = {"hello", "  hi there", "\t\n\v\f\r x", "/t team", "/a all", "  /t late", "/tx", "/", "   ", "",
                                  "\xC3\xA9t\xC3\xA9", " \xA0 x", "a/t", "/A no", "/T no", "12"};
void TestCallbacks()
{
    const std::vector<size_t> combos = NodesOf({K_COMBO}), chats = NodesOf({K_CHAT});
    const bool chat = !chats.empty() && (combos.empty() || P(0.5));
    if (!chat && combos.empty())
        return;
    const size_t i = chat ? PickOf(chats) : PickOf(combos);
    bool rp = false, ro = false;
    if (!chat) {
        Diff([&] { rp = C_ComboBox::HeaderEdited(g_portNodes[i], nullptr, nullptr, CONTROL_NOTIFY_0B, 0); },
             [&] { ro = OriginalAt<CallbackFn>(0x00658930)(g_origNodes[i], g_origNodes[i], nullptr, CONTROL_NOTIFY_0B, 0); });
        CHECK(rp == ro, "HeaderEdited: %d / %d", ro, rp);
        return;
    }
    auto *h = static_cast<C_ChatInput *>(g_portNodes[i]);
    // (An edit box's or a header's: the original reads the text GET_TEXT answers unchecked; a selection list answers none
    // for no selected line.)
    const std::vector<size_t> edits = NodesOf({K_EDITBOX, K_COMBOEDIT});
    C_Control *control = h->m_input && P(0.8) ? static_cast<C_Control *>(h->m_input)
                         : P(0.7) && !edits.empty() ? g_portNodes[PickOf(edits)]
                                                    : nullptr;
    // (The input's text a chat line: the box's selected text, GET_TEXT's answer; the stripping's and the commands' cases.)
    g_box.selected = Pick(CHAT_LINES);
    Diff([&] { rp = C_ChatInput::LineEntered(h, h, control, CONTROL_NOTIFY_02, 0); },
         [&] {
             ro = OriginalAt<CallbackFn>(0x006571f0)(g_origNodes[i], g_origNodes[i], control ? ToOrigNode(control) : nullptr, CONTROL_NOTIFY_02, 0);
         });
    CHECK(rp == ro, "LineEntered: %d / %d", ro, rp);
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = CHILD_NAME_COUNT;
    g_treePlan = TreePlan;
    g_codes = {CONTROL_MSG_DEACTIVATE,      CONTROL_MSG_ACTIVATE,   CONTROL_MSG_IGNORE_INPUT,  CONTROL_MSG_TAKE_INPUT,
               CONTROL_MSG_SET_INDEX,       CONTROL_MSG_GET_INDEX,  CONTROL_MSG_SET_VALUE,     CONTROL_MSG_GET_VALUE,
               CONTROL_MSG_REFRESH,         CONTROL_MSG_SET_TEXT,   CONTROL_MSG_SET_TEXT_ID,   CONTROL_MSG_ADD_TEXT,
               CONTROL_MSG_ADD_TEXT_ID,     CONTROL_MSG_SET_RUN_TEXT, CONTROL_MSG_CLEAR,       CONTROL_MSG_GET_RUNS,
               CONTROL_MSG_SET_MARGIN,      CONTROL_MSG_GET_TEXT,   CONTROL_MSG_GET_ITEM,      CONTROL_MSG_INSERT_FIRST,
               CONTROL_MSG_REMOVE_SELECTED, CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_NEXT_ITEM,  CONTROL_MSG_SET_POSITION,
               CONTROL_MSG_SELECT_TEXT,     CONTROL_MSG_SET_HEADER_TEXT, CONTROL_MSG_SET_HEADER_TEXT_ID, CONTROL_MSG_TEAM_TOGGLED,
               CONTROL_MSG_GET_CHECKED,     CONTROL_MSG_SET_MAX_LENGTH, CONTROL_MSG_EDIT,      CONTROL_MSG_ADD_CALLBACK,
               0x1000000,                   0x2000001,              0};
    // (The list's showing and hiding notify, so do picking it and the scroll bar's paging; ending an edit notifies 02.)
    g_loudCodes = {CONTROL_MSG_SHOW,       CONTROL_MSG_HIDE,      CONTROL_MSG_NOTIFY,         CONTROL_MSG_END_EDIT, CONTROL_MSG_TOGGLE_LIST,
                   CONTROL_MSG_PICK_INDEX, CONTROL_MSG_PICK_SELECTED, CONTROL_MSG_SCROLL_UP, CONTROL_MSG_SCROLL_DOWN};
    NOTIFY_RAISING = {CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B, CONTROL_NOTIFY_CLICK};
    g_tickKinds = {K_SCROLLED, K_COMBOEDIT, K_COMBO, K_CHAT, K_SELECT};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    g_cbValue = CbValueText;
    // The controls' own callbacks (the header's edit, the chat line).
    g_fnPairs = {{reinterpret_cast<uintptr_t>(&C_ComboBox::HeaderEdited), 0x00658930},
                 {reinterpret_cast<uintptr_t>(&C_ChatInput::LineEntered), 0x006571f0}};
    // The chat line's EDIT posted: the original's on its stack after the prompt's message (value2 2002, value3 unset); the
    // routes Create adds (the list's pick, the button's and the header's toggle, the team toggle's): their value2 and
    // value3 the stack's.
    g_stackValue2Codes = {CONTROL_MSG_EDIT, CONTROL_MSG_PICK_SELECTED, CONTROL_MSG_TOGGLE_LIST, CONTROL_MSG_TEAM_TOGGLED};
    g_stackValue3Codes = g_stackValue2Codes;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_GROUP_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("group_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_GROUP_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_GROUP_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_GROUP_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate},   {"Destructor", TestDestructor}, {"Tick", TestTick},
        {"Own", TestOwn},     {"Input", TestInput},     {"Strings", TestStrings}, {"Callbacks", TestCallbacks},
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
    std::printf("group_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
