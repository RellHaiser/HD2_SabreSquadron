// Human part 61b: the players' list and the check group (game/controls.cpp lines 5161-5171, 5298-5420) against the
// original's. MinGW; build_players.bat; runs players_diff.exe ("players_diff: N checks, 0 failures"). The kit
// (controls_kit.inc, controls_tree.inc, controls_kinds.inc) fakes what the controls reach outside themselves on both sides
// and compares the trees, the message queue, the engine's fakes and the calls on them. The trees are planned: each
// players' list with its scrolled text (its id; the text's list and now and then its bar: part 53's) and its title (id | 2:
// both used unchecked by the original), now and then the two swapped or a child added; each check group with its
// highlighted check box (its id; its halves: part 51's) or none. The network game's players faked on both sides (the
// port's g_netGame.m_players, the original's LockPlayers (0x0072f040) answering a vector of the same blocks; its
// UnlockPlayers (0x0072f100)): none to six, free (no name) or not, names and stats of all sizes.
// HD2_PLAYERS_ROUNDS: rounds per kind of call (default 20,000; HD2_PLAYERS_QUICK: 4,000); HD2_PLAYERS_CASE: one only;
// HD2_PLAYERS_FIRST_CHANCE: faults under the original's exception frames shown.
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

#define CONTROLS_KIT_HAS_PLAYERS  // LockPlayers, UnlockPlayers: this part's fakes
#include "controls_kit.inc"

static_assert(sizeof(S_NetPlayerState) == 0xC4, "the port's player state the original's size");

// The player state's constructor (net_game.cpp's: not linked; its members' initializers).
S_NetPlayerState::S_NetPlayerState() {}

namespace {

// ---- the network game's players, both sides ----
// The original's vector LockPlayers answers (MSVC 6's: +4 first, +8 last, +0xC the end of its room) and its blocks.
alignas(16) uint32_t g_origVector[4];
alignas(16) uint8_t g_origPlayers[8 * 0xC4];
const char *GameName(const void *self, bool port)
{
    return self == (port ? static_cast<const void *>(&g_netGame) : reinterpret_cast<const void *>(0x0097d070)) ? "netgame" : "netgame?";
}
int g_origLocks;   // (the original's SHOW_PLAYERS runs: printed at the end, a check the case is reached)
void *__attribute__((thiscall)) O_LockPlayers(void *self)
{
    ++g_origLocks;
    Log("%s LockPlayers", GameName(self, false));
    return g_origVector;
}
void __attribute__((thiscall)) O_UnlockPlayers(void *self) { Log("%s UnlockPlayers", GameName(self, false)); }

}  // namespace

// The port's.
S_NetPlayerState *C_NetGame::LockPlayers()
{
    Log("%s LockPlayers", GameName(this, true));
    return m_players.data();
}
void C_NetGame::UnlockPlayers() { Log("%s UnlockPlayers", GameName(this, true)); }

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

// ---- this part's kinds ----
uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }
template <class T> T *ChildOf(C_Control *c, int n = 0)
{
    for (C_Control *k : c->m_children)
        if (auto *t = dynamic_cast<T *>(k))
            if (n-- == 0)
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

// The scrolled text (part 53's): the group's focus, +0x70 its list, +0x74 its bar.
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
// The scroll bar (part 50's) with its arrows and slider among its children (the original uses the arrows unchecked).
void RandomScrollWired(C_Control *c)
{
    auto *s = static_cast<C_ScrollBar *>(c);
    RandomScroll(c);
    for (C_Control *k : c->m_children) {
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

// The players' list: the group's focus, the combo box's parts (+0x70 .. +0x84: none made, unused), +0x88 the players'
// text, +0x8C the title.
void RandomPlayers(C_Control *c)
{
    auto *p = static_cast<C_PlayerList *>(c);
    RandomGroup(c);
    p->m_open = P(0.2);
    p->m_picked = P(0.8) ? -1 : I(-2, 6);
    p->m_players = ChildOf<C_ScrolledText>(c);
    p->m_title = ChildOf<C_TitleControl>(c);
}
void PlayersToOrig(C_Control *c, uint8_t *o)
{
    auto *p = static_cast<C_PlayerList *>(c);
    GroupToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = OrigOf(p->m_button);
    OF<uint8_t *>(o, 0x74) = OrigOf(p->m_list);
    OF<uint8_t *>(o, 0x78) = OrigOf(p->m_header);
    OF<bool>(o, 0x7C) = p->m_open;
    OF<int>(o, 0x80) = p->m_picked;
    OF<uint32_t>(o, 0x84) = OrigText(p->m_pickedText);
    OF<uint8_t *>(o, 0x88) = OrigOf(p->m_players);
    OF<uint8_t *>(o, 0x8C) = OrigOf(p->m_title);
}
std::string PlayersDiffers(C_Control *c, const uint8_t *o)
{
    auto *p = static_cast<C_PlayerList *>(c);
    std::string why = PtrsDiffer(o, {{0x70, p->m_button}, {0x74, p->m_list}, {0x78, p->m_header}, {0x88, p->m_players}, {0x8C, p->m_title}});
    if (why.empty() && (OF<bool>(o, 0x7C) != p->m_open || OF<int>(o, 0x80) != p->m_picked))
        why = "open/picked " + std::to_string(OF<bool>(o, 0x7C)) + " " + std::to_string(OF<int>(o, 0x80)) + " / " + std::to_string(p->m_open) +
              " " + std::to_string(p->m_picked);
    if (why.empty() && TextOf(OF<uint32_t>(o, 0x84)) != TextOf(p->m_pickedText))
        why = "picked text " + TextOf(OF<uint32_t>(o, 0x84)) + " / " + TextOf(p->m_pickedText);
    return why.empty() ? GroupDiffers(c, o) : why;
}

// The highlighted check box's halves (part 51's): +0x6C empty, +0x70 full, +0x74 active full, +0x78 active empty, +0x7C
// over, +0x7D empty.
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
// The highlighted check box (part 51's): the box, +0x70 yes, +0x74 no (child halves), +0x78 checked, +0x7C the id under
// the cursor.
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
    OF<uint8_t *>(o, 0x70) = OrigOf(h->m_yes);
    OF<uint8_t *>(o, 0x74) = OrigOf(h->m_no);
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

// The check group: the group's focus, +0x70 its box.
void RandomCheckGroup(C_Control *c)
{
    RandomGroup(c);
    static_cast<C_CheckGroup *>(c)->m_box = ChildOf<C_HighlightCheckBox>(c);
}
void CheckGroupToOrig(C_Control *c, uint8_t *o)
{
    GroupToOrig(c, o);
    OF<uint8_t *>(o, 0x70) = OrigOf(static_cast<C_CheckGroup *>(c)->m_box);
}
std::string CheckGroupDiffers(C_Control *c, const uint8_t *o)
{
    const std::string why = PtrsDiffer(o, {{0x70, static_cast<C_CheckGroup *>(c)->m_box}});
    return why.empty() ? GroupDiffers(c, o) : why;
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"title", 0x00813cc0, 0x70, Make<C_TitleControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"label", 0x00814188, 0x70, Make<C_LabelControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"button", 0x00813f68, 0x84, Make<C_Button>, RandomButton, ButtonToOrig, ButtonDiffers},
    {"slider", 0x00815288, 0xA0, Make<C_Slider>, RandomSlider, SliderToOrig, SliderDiffers},
    {"scroll", 0x00814210, 0x94, Make<C_ScrollBar>, RandomScrollWired, ScrollToOrig, ScrollDiffers},
    {"select", 0x00814298, 0x98, Make<C_SelectList>, RandomSelect, SelectToOrig, SelectDiffers, O_BOX, nullptr, true},
    {"scrolled", 0x008143a8, 0x78, Make<C_ScrolledText>, RandomScrolled, ScrolledToOrig, ScrolledDiffers},
    {"players", 0x00814870, 0x90, Make<C_PlayerList>, RandomPlayers, PlayersToOrig, PlayersDiffers},
    {"half", 0x00814078, 0x80, Make<C_CheckBoxHalf>, RandomHalf, HalfToOrig, HalfDiffers},
    {"highlight", 0x00815178, 0x80, Make<C_HighlightCheckBox>, RandomHighlight, HighlightToOrig, HighlightDiffers, O_BOX},
    {"checkgroup", 0x008150f0, 0x74, Make<C_CheckGroup>, RandomCheckGroup, CheckGroupToOrig, CheckGroupDiffers},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_TITLE = 1, K_LABEL = 2, K_BUTTON = 3, K_SLIDER = 4, K_SCROLL = 5, K_SELECT = 6, K_SCROLLED = 7, K_PLAYERS = 8,
          K_HALF = 9, K_HIGHLIGHT = 10, K_CHECKGROUP = 11;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
void PlanScroll(int at, int bits)
{
    // (Its arrows always, its slider now and then: part 50's.)
    const int bar = PlanAdd(at, K_SCROLL, bits);
    PlanAdd(bar, K_BUTTON, 1);
    PlanAdd(bar, K_BUTTON, 2);
    PlanAdd(bar, P(0.85) ? K_SLIDER : K_CONTROL, 3);
}
// The scrolled text: its list always (the original's handlers use it unchecked), its bar mostly (part 53's).
void PlanScrolled(int parent, int bits)
{
    const int at = PlanAdd(parent, K_SCROLLED, bits);
    const bool swap = P(0.1);
    if (!swap)
        PlanAdd(at, K_SELECT, 8);
    if (P(0.85))
        PlanScroll(at, Pick({4, 5, 6, 7}));
    if (swap)
        PlanAdd(at, K_SELECT, 8);
}
// The players' list: its scrolled text (its id) and its title (id | 2), both always (the original uses both unchecked),
// now and then swapped, and a third child always: FindChild answers 2 for the title's id (not its index, 1), and
// FindControl uses the index unchecked (the original's too: a crash with Create's two children only).
void PlanPlayers(int parent)
{
    const int at = PlanAdd(parent, K_PLAYERS, P(0.6) ? -1 : I(0, 15) & ~3);
    const bool swap = P(0.1);
    if (!swap)
        PlanScrolled(at, 0);
    PlanAdd(at, K_TITLE, 2);
    if (swap)
        PlanScrolled(at, 0);
    // (A check box's half now and then: its leave refused when not hovered, the only kind here refusing one.)
    PlanAdd(at, Pick({K_CONTROL, K_LABEL, K_TITLE, K_HALF, K_HALF}), P(0.5) ? -1 : I(0, 15));
}
// The check group: its highlighted check box (its id; its halves id | 1 and 2) mostly, now and then another child.
void PlanCheckGroup(int parent)
{
    const int at = PlanAdd(parent, K_CHECKGROUP, P(0.6) ? -1 : I(0, 15) & ~3);
    if (P(0.15))
        PlanAdd(at, Pick({K_CONTROL, K_LABEL}), P(0.5) ? -1 : I(0, 15));
    if (P(0.85)) {
        const int box = PlanAdd(at, K_HIGHLIGHT, 0);
        PlanAdd(box, K_HALF, 1);
        PlanAdd(box, K_HALF, 2);
    }
    if (P(0.15))
        PlanAdd(at, Pick({K_CONTROL, K_LABEL}), P(0.5) ? -1 : I(0, 15));
}
void PlanGroup(int parent)
{
    if (P(0.6))
        PlanPlayers(parent);
    else
        PlanCheckGroup(parent);
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.4)) {
        PlanGroup(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 2); k > 0; --k)
            PlanGroup(root);
        if (P(0.3))
            PlanAdd(root, Pick({K_CONTROL, K_LABEL}), -1);
    }
    return g_plan;
}

// Create's frames: each part's frame there now and then, made complete for its own Create (the arrows' and the slider's
// three children, the highlighted check box's seven: parts 50, 51's).
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
    case K_SCROLLED:
        PartFrame(f, "text*");
        CompleteFor(K_SCROLL, PartFrame(f, "scroll*"), depth + 1);
        break;
    case K_PLAYERS:
        CompleteFor(K_SCROLLED, f, depth + 1);
        PartFrame(f, "header*");
        break;
    case K_HIGHLIGHT:
        if (P(0.85))
            Complete(f, {"yes_empty*", "yes_full*", "yesactived_full*", "no_empty*", "no_full*", "noactived_full*", "noactived_empty*"}, 7);
        break;
    case K_CHECKGROUP:
        CompleteFor(K_HIGHLIGHT, PartFrame(f, "text*"), depth + 1);
        break;
    }
}
void CreateFrames(size_t node, int fi) { CompleteFor(int(g_nodeKinds[node] - KINDS), fi, 0); }
// (The halves wired to their box; the scroll bar asks for the cursor in its tick: the game's always is.)
void AfterTree()
{
    g_cursorMade = true;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_HIGHLIGHT]) {
            auto *h = static_cast<C_HighlightCheckBox *>(g_portNodes[i]);
            h->m_yes = ChildOf<C_CheckBoxHalf>(h, 0);
            h->m_no = ChildOf<C_CheckBoxHalf>(h, 1);
            h->m_overId = P(0.3) ? 0 : P(0.6) ? (h->m_id | uint32_t(I(1, 2))) : U32();
        }
}

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
// A target for a group: its id with any last 4 bits, everyone's, another node's.
uint32_t GroupTarget(size_t i)
{
    if (P(0.2))
        return 0xFFFFFFFF;
    if (P(0.6))
        return (g_portNodes[i]->m_id & ~0xFu) | uint32_t(I(0, 15));
    return RandomId();
}

// ---- the round's state: the players, the boxes' answers ----
const char *const NAMES[] = {"Sabre", "x", "100%s", "Player with a name of fifty one letters, the most..", "\xC3\xA9t\xC3\xA9", "%d %n"};
void RandomPlayersState()
{
    const int n = P(0.1) ? 0 : I(1, 6);
    std::vector<S_NetPlayerState> &players = g_netGame.m_players;
    players.assign(size_t(n), S_NetPlayerState());
    for (S_NetPlayerState &p : players) {
        if (P(0.75)) {
            const char *name = Pick(NAMES);
            std::strncpy(p.name, name, sizeof p.name - 1);
        }
        for (int &s : p.stats.values)
            s = P(0.3) ? Pick({0, 1, -1, 100}) : int(P(0.8) ? I(-100, 100000) : int(U32()));
        p.connection = I(0, 50);
        p.team = I(0, 2);
    }
    // (Its blocks the port's bytes: the same layout, 0xC4 each.)
    std::memcpy(g_origPlayers, players.data(), players.size() * 0xC4);
    g_origVector[0] = 0;
    g_origVector[1] = uint32_t(uintptr_t(g_origPlayers));
    g_origVector[2] = g_origVector[3] = uint32_t(uintptr_t(g_origPlayers + players.size() * 0xC4));
}
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
    static const char *const selected[] = {"sel", "", "12", "-3", "abc", "line two"};
    g_box.selected = Pick(selected);
    RandomPlayersState();
}

const char *const TEXTS[] = {"run3", "nothing", "", "12", "Hard"};
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CALLBACK:
    case CONTROL_MSG_REMOVE_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    case CONTROL_MSG_GET_ITEM:
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
        m.value2 = uint32_t(Pick({2002, 1, 77, 0, 0xA33}));
        break;
    case CONTROL_MSG_SELECT_TEXT:
    case CONTROL_MSG_INSERT_FIRST:
    case CONTROL_MSG_ADD_TEXT:
    case CONTROL_MSG_SET_TEXT:
        m.text = Pick(TEXTS);
        break;
    case CONTROL_MSG_SET_CHECKED:
    case CONTROL_MSG_SET_CHECKED_QUIET:
        m.value = P(0.8) ? uint32_t(I(0, 1)) : U32();
        break;
    case CONTROL_MSG_SET_VALUE:
        if (P(0.5) && !g_portNodes.empty())
            m.value = g_portNodes[RandomNode()]->m_id;
        break;
    }
}

// ---- the tests ----
const uint32_t OWN_CODES[] = {CONTROL_MSG_SHOW_PLAYERS, CONTROL_MSG_SHOW_PLAYERS,  CONTROL_MSG_SHOW_PLAYERS,   CONTROL_MSG_IGNORED_7E,
                              CONTROL_MSG_IGNORED_7F,   CONTROL_MSG_ADD_TEXT,      CONTROL_MSG_ADD_TEXT_ID,    CONTROL_MSG_CLEAR,
                              CONTROL_MSG_REFRESH,      CONTROL_MSG_SET_TEXT,      CONTROL_MSG_SET_TEXT_ID,    CONTROL_MSG_GET_TEXT,
                              CONTROL_MSG_SET_VALUE,    CONTROL_MSG_GET_VALUE,     CONTROL_MSG_ADD_CALLBACK,   CONTROL_MSG_GET_CHECKED,
                              CONTROL_MSG_SET_CHECKED,  CONTROL_MSG_SHOW,          CONTROL_MSG_HIDE,           CONTROL_MSG_DEACTIVATE,
                              CONTROL_MSG_ACTIVATE,     CONTROL_MSG_SET_POSITION,  CONTROL_MSG_TAKE_INPUT,     0x1000000,
                              0x2000001};
// The groups' own messages sent straight to them (their targets' last 4 bits any: to a part, or not).
void TestOwn()
{
    const std::vector<size_t> nodes = NodesOf({K_PLAYERS, K_CHECKGROUP});
    if (nodes.empty())
        return;
    const size_t i = PickOf(nodes);
    S_ControlMessage m;
    m.target = GroupTarget(i);
    // (The players' list's own last bits half the time: 0, 1, 3 stay with it; the others go to a child.)
    if (g_nodeKinds[i] == &KINDS[K_PLAYERS] && P(0.5))
        m.target = (g_portNodes[i]->m_id & ~0xFu) | Pick({0u, 1u, 3u});
    m.id = g_nodeKinds[i] == &KINDS[K_PLAYERS] && P(0.4) ? uint32_t(CONTROL_MSG_SHOW_PLAYERS) : Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
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

// The groups' input slots with ids naming their parts (any last 4 bits), FindChild, SetInactive, SetStrings.
using StringsFn = void(__attribute__((thiscall)) *)(void *, void *);
void TestInput()
{
    const std::vector<size_t> nodes = NodesOf({K_PLAYERS, K_CHECKGROUP});
    if (nodes.empty())
        return;
    const size_t i = PickOf(nodes);
    C_Control *p = g_portNodes[i];
    const int id = int(GroupTarget(i));
    if (P(0.5))
        static_cast<C_ControlGroup *>(p)->m_focus = P(0.8) ? I(-1, int(p->m_children.size())) : Pick({2, 3, -2, 100});
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter},   {0x34, &C_Control::OnLeave}, {0x38, &C_Control::OnClick}, {0x3C, &C_Control::OnInput02},
        {0x40, &C_Control::OnInput05}, {0x48, &C_Control::OnPress}, {0x5C, &C_Control::OnKeyClick},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 2);
    int rp = 0, ro = 0;
    if (pick < n) {
        // (The focus a child's index here: the original's slots use it after IsChild only.)
        Diff([&] { rp = (p->*slots[pick].port)(id); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], id); });
    } else if (pick == n) {
        Diff([&] { rp = p->FindChild(uint32_t(id)); }, [&] { ro = Slot<IntFn>(i, 0x00)(g_origNodes[i], id); });
    } else if (pick == n + 1) {
        const bool on = P(0.5);
        Diff([&] { p->SetInactive(on); }, [&] { Slot<VoidIntFn>(i, 0x7C)(g_origNodes[i], on); });
    } else {
        std::vector<C_str> lines;
        for (int k = I(0, 3); k > 0; --k)
            lines.push_back(C_str("line"));
        Diff([&] { p->SetStrings(lines); },
             [&] {
                 auto *v = static_cast<uint8_t *>(OrigAlloc(16));
                 std::vector<uint32_t> reps;
                 for (const C_str &s : lines)
                     reps.push_back(OrigText(s));
                 OVecPut(v, 0, reps.data(), reps.size() * 4);
                 Slot<StringsFn>(i, 0x78)(g_origNodes[i], v);
             });
    }
    CHECK(rp == ro, "input %d (%s) %x: %d / %d", pick, g_nodeKinds[i]->name, unsigned(id), ro, rp);
}

// The constructors: inline in the original's factory (0x0065d470: type 0x22 the players' list, the combo box's
// constructor and +0x88 0, +0x8C 0; type 0xb the check group, +0x6C -1, +0x70 0).
void TestConstruct()
{
    auto *scene = reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000));
    const uint32_t id = U32();
    C_PlayerList players(scene, id, nullptr);
    CHECK(players.m_focus == -1 && !players.m_button && !players.m_list && !players.m_header && !players.m_open && players.m_picked == -1 &&
              !players.m_pickedText.m_rep && !players.m_players && !players.m_title && players.m_id == id && players.m_scene == scene,
          "the players' list's fields at construction");
    C_CheckGroup group(scene, id, nullptr);
    CHECK(group.m_focus == -1 && !group.m_box && group.m_id == id && group.m_takesInput, "the check group's fields at construction");
}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = CHILD_NAME_COUNT;
    g_treePlan = TreePlan;
    g_codes = {CONTROL_MSG_DEACTIVATE,  CONTROL_MSG_ACTIVATE,   CONTROL_MSG_IGNORE_INPUT, CONTROL_MSG_TAKE_INPUT,  CONTROL_MSG_SET_INDEX,
               CONTROL_MSG_GET_INDEX,   CONTROL_MSG_SET_VALUE,  CONTROL_MSG_GET_VALUE,    CONTROL_MSG_REFRESH,     CONTROL_MSG_SET_TEXT,
               CONTROL_MSG_SET_TEXT_ID, CONTROL_MSG_ADD_TEXT,   CONTROL_MSG_ADD_TEXT_ID,  CONTROL_MSG_CLEAR,       CONTROL_MSG_GET_TEXT,
               CONTROL_MSG_GET_ITEM,    CONTROL_MSG_SET_POSITION, CONTROL_MSG_GET_CHECKED, CONTROL_MSG_SET_CHECKED, CONTROL_MSG_SHOW_PLAYERS,
               CONTROL_MSG_IGNORED_7E,  CONTROL_MSG_ADD_CALLBACK, 0x1000000,              0x2000001,               0};
    g_loudCodes = {CONTROL_MSG_SHOW, CONTROL_MSG_HIDE, CONTROL_MSG_NOTIFY, CONTROL_MSG_SCROLL_UP, CONTROL_MSG_SCROLL_DOWN};
    NOTIFY_RAISING = {CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B, CONTROL_NOTIFY_CLICK};
    g_tickKinds = {K_PLAYERS, K_CHECKGROUP, K_SCROLLED, K_SELECT, K_HIGHLIGHT};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
    RedirectOriginal(0x0072f040, reinterpret_cast<const void *>(O_LockPlayers));
    RedirectOriginal(0x0072f100, reinterpret_cast<const void *>(O_UnlockPlayers));
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_PLAYERS_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("players_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_PLAYERS_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_PLAYERS_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_PLAYERS_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Input", TestInput},   {"Construct", TestConstruct},
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
    std::printf("(the players shown %d times)\n", g_origLocks);
    std::printf("players_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
