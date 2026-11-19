// Human part 58: the hiding list and the pool list (game/controls.cpp lines 4411-4542) against the original's. MinGW;
// build_hide.bat; runs hide_diff.exe ("hide_diff: N checks, 0 failures"). Part 56's list suite (list_diff.cpp) with the
// two kinds: planned lists of the three kinds, the hiding and pool lists with hidden items (owned in their vector at
// +0xA0, not children: the kit's S_Kind::heldAt / heldVec), the pool list's held item (+0xB4). Their messages: the item
// shown again (ITEM_COUNTED), hidden (HIDE_ITEM), all hidden (HIDE_ALL), the pool's entries (ADD_ENTRY,
// ADD_CURRENT_ENTRY: value2 a map, the port's C_MpMap and the original's block, its name at +0x01, paired as the list
// suite's descriptions are); the pool's inputs 08, 09, 0A.
// HD2_HIDE_ROUNDS: rounds per kind of call (default 20,000; HD2_HIDE_QUICK: 4,000); HD2_HIDE_CASE: one only;
// HD2_HIDE_FIRST_CHANCE: faults under the original's exception frames shown.
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
#include <malloc.h>
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
#include "game/mp_game_setup.h"
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

uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }
float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// ---- the kinds ----
// The text button (part 50's): the box, +0x70 normal, +0x74 active; the list item: +0x78 picked.
void RandomTextButton(C_Control *c)
{
    auto *b = static_cast<C_TextButton *>(c);
    RandomText(c);
    b->m_normalFrame = RandomFrame();
    b->m_activeFrame = RandomFrame();
}
void TextButtonToOrig(C_Control *c, uint8_t *o)
{
    auto *b = static_cast<C_TextButton *>(c);
    TextToOrig(c, o);
    OF<void *>(o, 0x70) = b->m_normalFrame;
    OF<void *>(o, 0x74) = b->m_activeFrame;
}
std::string TextButtonDiffers(C_Control *c, const uint8_t *o)
{
    auto *b = static_cast<C_TextButton *>(c);
    if (OF<void *>(o, 0x70) != b->m_normalFrame || OF<void *>(o, 0x74) != b->m_activeFrame)
        return "frames " + PtrName(OF<void *>(o, 0x70)) + " " + PtrName(OF<void *>(o, 0x74)) + " / " + PtrName(b->m_normalFrame) + " " +
               PtrName(b->m_activeFrame);
    return TextDiffers(c, o);
}
void RandomItem(C_Control *c)
{
    RandomTextButton(c);
    static_cast<C_ListItem *>(c)->m_picked = P(0.3);
}
void ItemToOrig(C_Control *c, uint8_t *o)
{
    TextButtonToOrig(c, o);
    OF<bool>(o, 0x78) = static_cast<C_ListItem *>(c)->m_picked;
}
std::string ItemDiffers(C_Control *c, const uint8_t *o)
{
    if (OF<bool>(o, 0x78) != static_cast<C_ListItem *>(c)->m_picked)
        return "item picked " + std::to_string(OF<bool>(o, 0x78)) + " / " + std::to_string(static_cast<C_ListItem *>(c)->m_picked);
    return TextButtonDiffers(c, o);
}

// The list group: the group's focus, +0x70 the cursor, +0x74 the picked, +0x78 the extra (owned), +0x7C the selector,
// +0x80 the selected, +0x84 sorted, +0x88 the origin, +0x8C the down step, +0x90 the across step, +0x94 visible, +0x98
// the first shown, +0x9C the last.
S_vector *NewOrigin()
{
    auto *v = static_cast<S_vector *>(_aligned_malloc(16, 16));
    *v = P(0.2) ? S_vector(0, 0, 0) : S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
    return v;
}
float RandomStep() { return P(0.4) ? 0.0f : P(0.3) ? Pick({0.1f, -0.1f, 0.25f, 1.0f}) : F(-0.5f, 0.5f); }
C_Control *g_lastExtra;   // (the plan's extra control: the next list's)
void RandomList(C_Control *c)
{
    auto *l = static_cast<C_ListGroup *>(c);
    RandomGroup(c);
    const int n = int(c->m_children.size());
    l->m_cursor = P(0.5) ? TheCursor() : nullptr;
    l->m_picked = n && P(0.5) ? c->m_children[size_t(I(0, n - 1))] : nullptr;
    l->m_selector = P(0.7) ? RandomFrame() : nullptr;
    l->m_selected = I(-2, n);
    l->m_sorted = P(0.7);
    l->m_origin = P(0.8) ? NewOrigin() : nullptr;
    l->m_downStep = RandomStep();
    l->m_acrossStep = P(0.5) ? 0.0f : RandomStep();
    // (At least one visible, the last shown the first + visible - 1, as the game's always are: Select scrolls on while
    // the last is below the selected, and a negative last never scrolls (unsigned): the original's and the port's loop
    // for ever.)
    l->m_visible = P(0.8) ? I(1, 6) : I(1, 10);
    l->m_first = P(0.7) ? I(0, std::max(0, n - 1)) : I(0, n + 2);
    l->m_last = l->m_first + l->m_visible - 1;
}
void ListToOrig(C_Control *c, uint8_t *o)
{
    auto *l = static_cast<C_ListGroup *>(c);
    GroupToOrig(c, o);
    OF<void *>(o, 0x70) = l->m_cursor;
    OF<uint8_t *>(o, 0x74) = OrigOf(l->m_picked);
    OF<uint8_t *>(o, 0x78) = OrigOf(l->m_extra);
    OF<void *>(o, 0x7C) = l->m_selector;
    OF<int>(o, 0x80) = l->m_selected;
    OF<bool>(o, 0x84) = l->m_sorted;
    OF<void *>(o, 0x88) = l->m_origin;
    OF<float>(o, 0x8C) = l->m_downStep;
    OF<float>(o, 0x90) = l->m_acrossStep;
    OF<int>(o, 0x94) = l->m_visible;
    OF<int>(o, 0x98) = l->m_first;
    OF<int>(o, 0x9C) = l->m_last;
}
std::string ListDiffers(C_Control *c, const uint8_t *o)
{
    auto *l = static_cast<C_ListGroup *>(c);
    char text[256];
    if (PtrName(OF<void *>(o, 0x70)) != PtrName(l->m_cursor) || PtrName(OF<void *>(o, 0x74)) != PtrName(l->m_picked) ||
        PtrName(OF<void *>(o, 0x78)) != PtrName(l->m_extra) || OF<void *>(o, 0x7C) != l->m_selector || OF<int>(o, 0x80) != l->m_selected ||
        OF<bool>(o, 0x84) != l->m_sorted || OF<void *>(o, 0x88) != l->m_origin || Bits(OF<float>(o, 0x8C)) != Bits(l->m_downStep) ||
        Bits(OF<float>(o, 0x90)) != Bits(l->m_acrossStep) || OF<int>(o, 0x94) != l->m_visible || OF<int>(o, 0x98) != l->m_first ||
        OF<int>(o, 0x9C) != l->m_last) {
        std::snprintf(text, sizeof text, "list %s %s %s %s sel %d sort %d %p %08x %08x vis %d %d %d / %s %s %s %s sel %d sort %d %p %08x %08x vis %d %d %d",
                      PtrName(OF<void *>(o, 0x70)).c_str(), PtrName(OF<void *>(o, 0x74)).c_str(), PtrName(OF<void *>(o, 0x78)).c_str(),
                      PtrName(OF<void *>(o, 0x7C)).c_str(), OF<int>(o, 0x80), OF<bool>(o, 0x84), OF<void *>(o, 0x88), Bits(OF<float>(o, 0x8C)),
                      Bits(OF<float>(o, 0x90)), OF<int>(o, 0x94), OF<int>(o, 0x98), OF<int>(o, 0x9C), PtrName(l->m_cursor).c_str(),
                      PtrName(l->m_picked).c_str(), PtrName(l->m_extra).c_str(), PtrName(l->m_selector).c_str(), l->m_selected, l->m_sorted,
                      static_cast<void *>(l->m_origin), Bits(l->m_downStep), Bits(l->m_acrossStep), l->m_visible, l->m_first, l->m_last);
        return text;
    }
    return GroupDiffers(c, o);
}
C_Control **ListExtra(C_Control *c) { return &static_cast<C_ListGroup *>(c)->m_extra; }

// The hiding list: the list's, +0xA0 the hidden (an MSVC6 vector: +0xA4 first, +0xA8 last, +0xAC end; +0xB0 unused);
// the pool list: +0xB4 the held item.
std::vector<C_Control *> *Hidden(C_Control *c) { return &static_cast<C_HidingList *>(c)->m_hidden; }
void HidingToOrig(C_Control *c, uint8_t *o)
{
    ListToOrig(c, o);
    std::vector<uint32_t> hidden;
    for (C_Control *h : *Hidden(c))
        hidden.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OrigOf(h))));
    OVecPut(o, 0xA0, hidden.data(), hidden.size() * 4);
}
std::string HidingDiffers(C_Control *c, const uint8_t *o)
{
    const std::vector<C_Control *> &hidden = *Hidden(c);
    std::string port, orig;
    for (C_Control *h : hidden)
        port += PtrName(h) + " ";
    for (size_t k = 0; k < OVecBytes(o, 0xA0) / 4; ++k)
        orig += PtrName(reinterpret_cast<uint8_t **>(OVecData(o, 0xA0))[k]) + " ";
    if (port != orig || OVecBytes(o, 0xA0) != hidden.size() * 4)
        return "hidden " + orig + "/ " + port;
    return ListDiffers(c, o);
}
void RandomPool(C_Control *c)
{
    RandomList(c);
    auto *p = static_cast<C_PoolList *>(c);
    const int n = int(c->m_children.size());
    p->m_held = n && P(0.4) ? c->m_children[size_t(I(0, n - 1))] : nullptr;
}
void PoolToOrig(C_Control *c, uint8_t *o)
{
    HidingToOrig(c, o);
    OF<uint8_t *>(o, 0xB4) = OrigOf(static_cast<C_PoolList *>(c)->m_held);
}
std::string PoolDiffers(C_Control *c, const uint8_t *o)
{
    // (An item removed or cleared while held: the held left dangling on both sides, the original's.)
    C_Control *held = static_cast<C_PoolList *>(c)->m_held;
    const bool portGone = std::find(g_portDeleted.begin(), g_portDeleted.end(), held) != g_portDeleted.end();
    const bool origGone = std::find(g_origDeleted.begin(), g_origDeleted.end(), OF<void *>(o, 0xB4)) != g_origDeleted.end();
    if (portGone || origGone) {
        if (portGone != origGone)
            return "held deleted " + std::to_string(origGone) + " / " + std::to_string(portGone);
        return HidingDiffers(c, o);
    }
    if (PtrName(OF<uint8_t *>(o, 0xB4)) != PtrName(held))
        return "held " + PtrName(OF<uint8_t *>(o, 0xB4)) + " / " + PtrName(static_cast<C_PoolList *>(c)->m_held);
    return HidingDiffers(c, o);
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"label", 0x00814188, 0x70, Make<C_LabelControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"item", 0x00814ed0, 0x7C, Make<C_ListItem>, RandomItem, ItemToOrig, ItemDiffers, O_BOX, nullptr, true},
    {"list", 0x00815068, 0xA0, Make<C_ListGroup>, RandomList, ListToOrig, ListDiffers, 0, nullptr, false, 0x78, ListExtra},
    {"hiding", 0x00814a08, 0xB4, Make<C_HidingList>, RandomList, HidingToOrig, HidingDiffers, 0, nullptr, false, 0x78, ListExtra, 0xA0, Hidden},
    {"pool", 0x008144b8, 0xB8, Make<C_PoolList>, RandomPool, PoolToOrig, PoolDiffers, 0, nullptr, false, 0x78, ListExtra, 0xA0, Hidden},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_LABEL = 1, K_ITEM = 2, K_LIST = 3, K_HIDING = 4, K_POOL = 5;
bool IsListKind(size_t node)
{
    return g_nodeKinds[node] == &KINDS[K_LIST] || g_nodeKinds[node] == &KINDS[K_HIDING] || g_nodeKinds[node] == &KINDS[K_POOL];
}

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
std::vector<bool> g_planHidden;   // per plan node: a hidden item
int PlanAdd(int parent, int kind, int bits, bool child = true, bool hidden = false)
{
    g_plan.push_back({parent, kind, bits, child});
    g_planHidden.push_back(hidden);
    return int(g_plan.size()) - 1;
}
// A list (of the three kinds, mostly the part's): its items (only items: the navigation keys loop while a child refuses
// to be left; ids by their index, now and then any), its extra control now and then (owned: not a child); the hiding
// and pool lists' hidden items (items mostly, a label now and then; ids now and then a child's).
void PlanList(int parent)
{
    const int kind = P(0.15) ? K_LIST : P(0.5) ? K_HIDING : K_POOL;
    const int at = PlanAdd(parent, kind, -1);
    for (int k = P(0.15) ? 0 : I(1, 8); k > 0; --k)
        // (Now and then from few bits: items sharing an id, which the sort leaves in place.)
        PlanAdd(at, K_ITEM, P(0.25) ? Pick({1, 2, 2, 3}) : P(0.1) ? I(0, 15) : -1);
    if (kind != K_LIST)
        for (int k = P(0.15) ? 0 : I(1, 6); k > 0; --k)
            PlanAdd(at, P(0.9) ? K_ITEM : K_LABEL, P(0.25) ? Pick({1, 2, 3, 4, 5}) : P(0.1) ? I(0, 15) : -1, false, true);
    if (P(0.4))
        PlanAdd(at, P(0.5) ? K_CONTROL : K_LABEL, -1, false);
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    g_planHidden.clear();
    if (P(0.4)) {
        PlanList(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 2); k > 0; --k)
            PlanList(root);
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
// (The hidden items into their lists' vectors; the extra controls: the other nodes that are no one's child, under a
// list.)
void AfterTree()
{
    g_cursorMade = true;
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        C_Control *c = g_portNodes[i];
        if (i < g_planHidden.size() && g_planHidden[i]) {
            static_cast<C_HidingList *>(c->m_parent)->m_hidden.push_back(c);
            continue;
        }
        if (!c->m_parent || g_nodeKinds[i] == &KINDS[K_ITEM])
            continue;
        auto &kids = c->m_parent->m_children;
        if (std::find(kids.begin(), kids.end(), c) == kids.end())
            if (auto *l = dynamic_cast<C_ListGroup *>(c->m_parent))
                l->m_extra = c;
    }
    // (Used: the ticks' trees are not planned.)
    g_planHidden.clear();
    // (The pool lists' held item: a child (RandomPool's) or none, now and then a hidden one.)
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_POOL]) {
            auto *p = static_cast<C_PoolList *>(g_portNodes[i]);
            if (!p->m_hidden.empty() && P(0.1))
                p->m_held = p->m_hidden[size_t(I(0, int(p->m_hidden.size()) - 1))];
        }
    // (The lists' values: none, a node's id (the scroll bar the wheel pages: found), another.)
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (IsListKind(i))
            g_portNodes[i]->m_value = P(0.3) ? 0 : P(0.6) ? g_portNodes[RandomNode()]->m_id : RandomId();
    // (Items only under lists: their click takes the parent to be one.)
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_ITEM] && !dynamic_cast<C_ListGroup *>(g_portNodes[i]->m_parent))
            g_portNodes[i]->m_parent = nullptr;
}
// Create's frame: the selector now and then.
void CreateFrames(size_t node, int fi)
{
    if (fi >= 0 && IsListKind(node) && P(0.5))
        g_arena.child[fi][ChildName("selector*")] = static_cast<int16_t>(P(0.8) ? I(0, 15) : -1);
}

// ---- the messages' values ----
// The ADD_ITEM descriptions: the port's and the original's (paired in g_fnPairs: the logs name them alike).
S_ControlDesc *NewDesc(uint32_t id)
{
    auto *d = new S_ControlDesc(6, "item", id);
    auto *o = static_cast<uint8_t *>(std::calloc(1, 0x40));
    OF<uint32_t>(o, 0) = 0x00813ab0;
    OF<uint32_t>(o, 0x08) = id;
    OF<int>(o, 0x0C) = 6;
    g_fnPairs.push_back({reinterpret_cast<uintptr_t>(d), reinterpret_cast<uintptr_t>(o)});
    return d;
}
uint32_t ChildIdOrOther()
{
    const std::vector<size_t> items = NodesOf(K_ITEM);
    if (!items.empty() && P(0.7))
        return g_portNodes[items[size_t(I(0, int(items.size()) - 1))]]->m_id ^ (P(0.1) ? 1u << I(0, 31) : 0);
    return RandomId();
}
// A hidden item's id (any list's), else ChildIdOrOther's.
uint32_t HiddenIdOrOther()
{
    std::vector<uint32_t> ids;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (g_nodeKinds[i] == &KINDS[K_HIDING] || g_nodeKinds[i] == &KINDS[K_POOL])
            for (C_Control *h : *Hidden(g_portNodes[i]))
                ids.push_back(h->m_id);
    if (!ids.empty() && P(0.6))
        return ids[size_t(I(0, int(ids.size()) - 1))];
    return ChildIdOrOther();
}
// The pool's entries' maps: the port's C_MpMap and the original's block (its name's C_str at +0x01), paired in g_fnPairs.
const char *const MAP_NAMES[] = {"Africa1", "", "Normandy 2", "Czech"};
std::vector<C_MpMap *> g_maps;
std::vector<void *> g_origMaps;
uintptr_t NewMap()
{
    const C_str name = Pick(MAP_NAMES);
    auto *map = new C_MpMap(name, C_str("dir"));
    auto *o = static_cast<uint8_t *>(std::calloc(1, 0x48));
    OF<uint32_t>(o, 0x01) = OrigText(name);
    g_maps.push_back(map);
    g_origMaps.push_back(o);
    g_fnPairs.push_back({reinterpret_cast<uintptr_t>(map), reinterpret_cast<uintptr_t>(o)});
    return reinterpret_cast<uintptr_t>(map);
}
void EntryValues(S_ControlMessage &m)
{
    m.value = uint32_t(P(0.8) ? I(0, 20) : int(U32()));
    m.value2 = P(0.1) ? 0 : NewMap();
}
void MessageValues(S_ControlMessage &m)
{
    switch (m.id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CALLBACK:
        // (A function always: the original calls a callback without checking it.)
        m.value = RandomNotification();
        m.value2 = RandomObject();
        m.value3 = g_portCbs[I(0, 2)];
        break;
    case CONTROL_MSG_REMOVE_ITEM:
        m.value = ChildIdOrOther();
        break;
    case CONTROL_MSG_SET_VISIBLE:
        m.value = uint32_t(I(1, 8));   // (see RandomList: none visible makes the last -1)
        break;
    case CONTROL_MSG_SET_ACROSS_STEP:
    case CONTROL_MSG_SET_DOWN_STEP: {
        const float step = RandomStep();
        std::memcpy(&m.value2, &step, 4);
        break;
    }
    case CONTROL_MSG_ADD_ITEM:
        m.value = uint32_t(reinterpret_cast<uintptr_t>(P(0.8) ? RandomFrame() : nullptr));
        m.value2 = reinterpret_cast<uintptr_t>(NewDesc(ChildIdOrOther()));
        break;
    case CONTROL_MSG_SET_ORIGIN:
        m.value2 = P(0.15) ? 0 : reinterpret_cast<uintptr_t>(NewOrigin());
        break;
    case CONTROL_MSG_SET_POSITION:
    case CONTROL_MSG_SELECT_INDEX:
        // (Small and not negative: an empty list scrolls on without end, to a far position (unsigned: a negative one)
        // 2^32 steps, the original's too. TestOwn sends negative ones to lists with items.)
        m.value = uint32_t(I(0, 12));
        break;
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_ADD_TEXT:
        m.text = Pick({"one", "", "two"});
        break;
    case CONTROL_MSG_SET_VALUE:
        m.value = P(0.3) ? 0 : RandomId();
        break;
    case CONTROL_MSG_ITEM_COUNTED:
        m.value = HiddenIdOrOther();
        break;
    case CONTROL_MSG_HIDE_ITEM:
        m.value = ChildIdOrOther();
        break;
    case CONTROL_MSG_ADD_CURRENT_ENTRY:   // (ADD_ENTRY: ADD_ITEM's code, its map given by TestOwn to a pool)
        EntryValues(m);
        break;
    }
}

// SET_VISIBLE sets the last shown, not the first: sent to a list at its first (the game's at its setup; see RandomList).
void BeforeMessage(size_t i, const S_ControlMessage &m)
{
    if ((m.id & CONTROL_MSG_MASK) == CONTROL_MSG_SET_VISIBLE)
        if (auto *l = dynamic_cast<C_ListGroup *>(g_portNodes[i]))
            if (!l->IsChild(l->FindChild(m.target)))
                l->m_first = 0;
}

void PickRound()
{
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
    g_fnPairs.clear();   // (descriptions no ADD_ITEM took: left)
    for (C_MpMap *m : g_maps)
        delete m;
    for (void *o : g_origMaps)
        std::free(o);
    g_maps.clear();
    g_origMaps.clear();
}

// ---- the tests ----
const uint32_t OWN_CODES[] = {
    CONTROL_MSG_REMOVE_ITEM,    CONTROL_MSG_SHOW,          CONTROL_MSG_HIDE,          CONTROL_MSG_CAN_SCROLL_UP, CONTROL_MSG_CAN_SCROLL_DOWN,
    CONTROL_MSG_GET_SCROLL_RANGE, CONTROL_MSG_SET_VISIBLE, CONTROL_MSG_SET_ACROSS_STEP, CONTROL_MSG_SET_DOWN_STEP, CONTROL_MSG_ADD_ITEM,
    CONTROL_MSG_NEXT_ITEM,      CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_CLEAR,         CONTROL_MSG_SET_ORIGIN,    CONTROL_MSG_GET_POSITION,
    CONTROL_MSG_SET_POSITION,   CONTROL_MSG_REFRESH,       CONTROL_MSG_SELECT_INDEX,  CONTROL_MSG_SET_VALUE,     CONTROL_MSG_GET_VALUE,
    CONTROL_MSG_SET_TEXT,       0x1000000,                 0x2000001,
    // (The part's, more often.)
    CONTROL_MSG_ITEM_COUNTED,   CONTROL_MSG_HIDE_ITEM,     CONTROL_MSG_HIDE_ALL,      CONTROL_MSG_ADD_ENTRY,     CONTROL_MSG_ADD_CURRENT_ENTRY,
    CONTROL_MSG_ITEM_COUNTED,   CONTROL_MSG_HIDE_ITEM,     CONTROL_MSG_HIDE_ALL,      CONTROL_MSG_ADD_ENTRY,     CONTROL_MSG_ADD_CURRENT_ENTRY,
    CONTROL_MSG_ITEM_COUNTED,   CONTROL_MSG_HIDE_ITEM,     CONTROL_MSG_ADD_ENTRY};
size_t PickList()
{
    std::vector<size_t> lists;
    for (size_t i = 0; i < g_portNodes.size(); ++i)
        if (IsListKind(i))
            lists.push_back(i);
    return lists[size_t(I(0, int(lists.size()) - 1))];
}
// A target: everyone's, the list's, a child's (its messages go to it), another.
uint32_t ListTarget(size_t i)
{
    if (P(0.3))
        return 0xFFFFFFFF;
    if (P(0.3))
        return g_portNodes[i]->m_id;
    if (P(0.5) && !g_portNodes[i]->m_children.empty())
        return g_portNodes[i]->m_children[size_t(I(0, int(g_portNodes[i]->m_children.size()) - 1))]->m_id;
    return RandomId();
}
// The list's own messages sent straight to it (the original's descriptions: ADD_ITEM's value2 mapped by g_fnPairs).
void TestOwn()
{
    const size_t i = PickList();
    S_ControlMessage m;
    m.target = ListTarget(i);
    m.id = Pick(OWN_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    // (A pool's ADD_ITEM code is its ADD_ENTRY: a map for a description.)
    if ((m.id & CONTROL_MSG_MASK) == CONTROL_MSG_ADD_ENTRY && g_nodeKinds[i] == &KINDS[K_POOL])
        EntryValues(m);
    if ((m.id & CONTROL_MSG_MASK) == CONTROL_MSG_SET_POSITION && !g_portNodes[i]->m_children.empty() && P(0.3))
        m.value = uint32_t(Pick({-1, -2, INT_MIN}));
    BeforeMessage(i, m);
    g_messageBefore = m;
    if (const char *t = std::getenv("HD2_CONTROLS_TRACE"))
        if (std::atoi(t) == g_round) {
            auto *l = static_cast<C_ListGroup *>(g_portNodes[i]);
            std::printf("  c%u (%s): message %x target %x value %x value2 %x; %u items, %d hidden, focus %d first %d last %d visible %d selected %d\n",
                        unsigned(i), g_nodeKinds[i]->name, m.id, m.target, m.value, unsigned(m.value2), unsigned(l->m_children.size()),
                        g_nodeKinds[i]->heldVec ? int(Hidden(l)->size()) : -1, l->m_focus, l->m_first, l->m_last, l->m_visible, l->m_selected);
        }
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

// The list's and its items' input: the slots with ids of its items, the navigation keys (and others), the wheel at its
// steps' edges; ScrollRange (+0x84).
void TestInput()
{
    const bool item = P(0.3);
    const std::vector<size_t> items = NodesOf(K_ITEM);
    const size_t i = item && !items.empty() ? items[size_t(I(0, int(items.size()) - 1))] : PickList();
    C_Control *p = g_portNodes[i];
    const int id = int(ListTarget(IsListKind(i) ? i : PickList()));
    if (auto *g = dynamic_cast<C_ControlGroup *>(p))
        if (P(0.5))
            g->m_focus = I(-1, int(g->m_children.size()));
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter},   {0x34, &C_Control::OnLeave}, {0x38, &C_Control::OnClick},
        {0x3C, &C_Control::OnInput02}, {0x48, &C_Control::OnPress}, {0x40, &C_Control::OnInput05},
        // (The pool's: the held item.)
        {0x50, &C_Control::OnInput08}, {0x54, &C_Control::OnInput09}, {0x58, &C_Control::OnInput0A},
        {0x50, &C_Control::OnInput08}, {0x54, &C_Control::OnInput09}, {0x58, &C_Control::OnInput0A},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 2);
    if (const char *t = std::getenv("HD2_CONTROLS_TRACE"))
        if (std::atoi(t) == g_round) {
            std::printf("  c%u (%s) pick %d id %x focus %d", unsigned(i), g_nodeKinds[i]->name, pick, unsigned(id),
                        dynamic_cast<C_ControlGroup *>(p) ? static_cast<C_ControlGroup *>(p)->m_focus : -9);
            if (auto *l = dynamic_cast<C_ListGroup *>(p))
                std::printf(" items %u first %d last %d visible %d selected %d value %x", unsigned(l->m_children.size()), l->m_first, l->m_last,
                            l->m_visible, l->m_selected, l->m_value);
            std::printf("\n");
        }
    int rp = 0, ro = 0;
    if (pick < n) {
        const int arg = P(0.3) ? Pick({1, 0xF, 0, 2}) : id;
        Diff([&] { rp = (p->*slots[pick].port)(arg); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], arg); });
    } else if (pick == n) {
        const int key = Pick({0xC7, 0xC8, 0xC9, 0xCB, 0xCD, 0xCF, 0xD0, 0xD1, 0xC8, 0xD0, 0x1C, 0x01, 0xCA}) | (P(0.1) ? int(U32() & 0xFFFFFF00u) : 0);
        Diff([&] { rp = p->Unknown20(key); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], key); });
    } else if (pick == n + 1) {
        const int amount = P(0.2) ? I(-3, 3) : (P(0.5) ? 1 : -1) * (Pick({1, 119, 120, 121, 239, 240, 241, 600}));
        Diff([&] { rp = p->OnWheel(id, amount); }, [&] { ro = Slot<Int2Fn>(i, 0x4C)(g_origNodes[i], id, amount); });
    } else if (IsListKind(i)) {
        Diff([&] { rp = static_cast<C_ListGroup *>(p)->ScrollRange(); }, [&] { ro = Slot<IntFn>(i, 0x84)(g_origNodes[i], 0); });
    }
    CHECK(rp == ro, "input %d (%s) %x: %d / %d", pick, g_nodeKinds[i]->name, unsigned(id), ro, rp);
}

// The constructors: the original's are inline in its factory (0x0065d470, redirected by the kit), not callable alone.
// From its asm: type 0x23 the list group's constructor and the hidden vector's (empty); type 0x26 the same, then +0x84
// (sorted) 0 and +0xB4 (held) 0. Checked against a list group's own (the same constructor for type 0xB).
void TestConstruct()
{
    auto *scene = reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000));
    const uint32_t id = U32();
    C_ListGroup list(scene, id, nullptr);
    C_HidingList hiding(scene, id, nullptr);
    C_PoolList pool(scene, id, nullptr);
    CHECK(hiding.m_hidden.empty() && pool.m_hidden.empty() && pool.m_held == nullptr, "hidden / held at construction");
    CHECK(hiding.m_sorted == list.m_sorted && !pool.m_sorted, "sorted at construction: %d %d / list %d", hiding.m_sorted, pool.m_sorted,
          list.m_sorted);
    CHECK(hiding.m_id == id && pool.m_id == id && pool.m_scene == scene && pool.m_visible == list.m_visible && pool.m_first == list.m_first &&
              pool.m_last == list.m_last && pool.m_selected == list.m_selected,
          "the list group's construction");
}

void Noop() {}

void SetUp()
{
    SetUpKit();
    g_kinds = KINDS;
    g_kindCount = KIND_COUNT;
    g_arenaNames = CHILD_NAME_COUNT;
    g_treePlan = TreePlan;
    g_codes = {CONTROL_MSG_DEACTIVATE,      CONTROL_MSG_ACTIVATE,        CONTROL_MSG_IGNORE_INPUT,   CONTROL_MSG_TAKE_INPUT,
               CONTROL_MSG_SET_INDEX,       CONTROL_MSG_GET_INDEX,       CONTROL_MSG_SET_VALUE,      CONTROL_MSG_GET_VALUE,
               CONTROL_MSG_SET_TEXT,        CONTROL_MSG_ADD_TEXT,        CONTROL_MSG_CAN_SCROLL_UP,  CONTROL_MSG_CAN_SCROLL_DOWN,
               CONTROL_MSG_GET_SCROLL_RANGE, CONTROL_MSG_SET_ACROSS_STEP, CONTROL_MSG_SET_DOWN_STEP, CONTROL_MSG_GET_POSITION,
               CONTROL_MSG_SET_ORIGIN,      CONTROL_MSG_ADD_CALLBACK,    0x1000000,                  0x2000001,
               0};
    // (The scrolls show and hide the items, the picks unpick one: notifying.)
    g_loudCodes = {CONTROL_MSG_SHOW,      CONTROL_MSG_HIDE,         CONTROL_MSG_NOTIFY,       CONTROL_MSG_NEXT_ITEM,
                   CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_SET_POSITION, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_VISIBLE,
                   CONTROL_MSG_SELECT_INDEX, CONTROL_MSG_REMOVE_ITEM, CONTROL_MSG_CLEAR,
                   CONTROL_MSG_ITEM_COUNTED, CONTROL_MSG_HIDE_ITEM,   CONTROL_MSG_HIDE_ALL};
    // (ADD_ITEM's code a pool's ADD_ENTRY, a map in its value2 (ADD_CURRENT_ENTRY's too): sent only by TestOwn, to the
    // list it picks, its value2 the list's kind's.)
    NOTIFY_RAISING = {CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B};
    // (Not in routes: the descriptions and origins are owned by whom takes them; deleting a notifying node; the maps.)
    g_noRouteCodes = {CONTROL_MSG_SET_ORIGIN, CONTROL_MSG_ADD_ITEM, CONTROL_MSG_REMOVE_ITEM, CONTROL_MSG_CLEAR, CONTROL_MSG_ADD_ENTRY,
                      CONTROL_MSG_ADD_CURRENT_ENTRY};
    g_tickKinds = {K_LIST, K_HIDING, K_POOL, K_ITEM, K_CONTROL};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    g_beforeMessage = BeforeMessage;
    // (The removed item's frame posted: the original's message on its stack, value3 unset.)
    g_stackValue3Codes = {CONTROL_MSG_REMOVE_ITEM};
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
    // (The origin freed by the port's destructor only: the original's _aligned_free import a no-op.)
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(Noop));
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_HIDE_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("hide_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_HIDE_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_HIDE_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_HIDE_CASE");
    struct S_Case {
        const char *name;
        void (*run)();
    } const cases[] = {
        {"Slots", TestSlots}, {"Message", TestMessage}, {"Create", TestCreate}, {"Destructor", TestDestructor},
        {"Tick", TestTick},   {"Own", TestOwn},         {"Input", TestInput},         {"Construct", TestConstruct},
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
    std::printf("hide_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
