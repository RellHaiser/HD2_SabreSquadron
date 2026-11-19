// Human part 59: the server list and its rows (game/controls.cpp lines 4558-4901) against the original's. MinGW;
// build_server.bat; runs server_diff.exe ("server_diff: N checks, 0 failures"). Planned trees of server lists (up to 11
// rows, the rows' callbacks to their list: the port's RowClicked / RowNotified and the original's 0x00666650 /
// 0x00666630, paired), a round's pool of server entries (the same blocks on both sides: not owned, their layout the
// original's), the list's entries (+0xA8), selected (+0xA0), sort key (+0xA4), window (+0xC0, +0xC4). The kit's cases;
// the list's and rows' messages straight to them (ADD_ITEM / ADD_ENTRY with entries, SORT with keys, SET_POSITION, ...);
// Create (the rows made on "Server1" .. "Server11", their icons); the constructors' fields.
// HD2_SERVER_ROUNDS: rounds per kind of call (default 20,000; HD2_SERVER_QUICK: 4,000); HD2_SERVER_CASE: one only;
// HD2_SERVER_FIRST_CHANCE: faults under the original's exception frames shown.
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

#define CONTROLS_KIT_HAS_GAME_TYPE_TEXT  // mp_text_ids.cpp's GetGameTypeTextId
#include "controls_kit.inc"

namespace {
#include "controls_kinds.inc"
#include "controls_tree.inc"

uint8_t *OrigOf(const C_Control *c) { return c ? ToOrigNode(c) : nullptr; }

// ---- the round's server entries (the same blocks on both sides) ----
// (No '%' in the names and maps: the original's row formats them as C_fstr's format (a name "a%sb" read its
// arguments off the stack: garbage, or a fault in its printf), a flaw the port fixes ("%s").)
std::vector<S_ServerEntry> g_entries(300);
const char *const NAMES[] = {"Alpha", "alpha", "ALPHA", "Beta", "", "b", "a b", "\xC3\xA9t\xC3\xA9", "\xC3", "\xE2\x82\xAC 1",
                             "Zulu", "_x", "[x]", "\xFF\xFE", "alpha2", "Alph", "zulu"};
const char *const MAPS[] = {" ??? ", "Africa1", "africa1", "Normandy 2", "Czech", "", "1 2", "\xC3\xA9"};
void RandomName(char *out, size_t size, const char *const *names, size_t count)
{
    if (P(0.05)) {
        // (Full: the longest the entry holds.)
        for (size_t k = 0; k + 1 < size; ++k)
            out[k] = char(Pick({'a', 'B', 'z', '_', '\xC3', '\xA9'}));
        out[size - 1] = 0;
        return;
    }
    std::snprintf(out, size, "%s", names[I(0, int(count) - 1)]);
}
uint32_t RandomCount() { return P(0.7) ? uint32_t(Pick({0, 1, 8, 8, 16, 32})) : P(0.7) ? uint32_t(I(0, 40)) : U32(); }
void RandomEntry(S_ServerEntry &e)
{
    std::memset(&e, P(0.1) ? 0xCD : 0, sizeof e);
    RandomName(e.name, sizeof e.name, NAMES, sizeof NAMES / sizeof NAMES[0]);
    RandomName(e.map, sizeof e.map, MAPS, sizeof MAPS / sizeof MAPS[0]);
    e.address = U32();
    e.port = I(0, 70000);
    e.gameType = P(0.8) ? I(0, 3) : P(0.5) ? I(-3, 6) : int(U32());
    e.ping = P(0.6) ? uint32_t(Pick({0, 10, 10, 50, 120, 999, 9999})) : P(0.7) ? uint32_t(I(0, 2000)) : U32();
    e.maxPlayers = RandomCount();
    e.players = P(0.5) && e.maxPlayers < 64 ? uint32_t(I(0, int(e.maxPlayers))) : RandomCount();
    e.dedicated = P(0.5);
    e.internet = P(0.5);
    e.password = P(0.3);
    e.voice = P(0.4);
    e.versionMajor = P(0.8) ? g_versionMajor : I(0, 3);
    e.versionMinor = P(0.8) ? g_versionMinor : I(0, 6);
}
uintptr_t RandomEntryPtr() { return reinterpret_cast<uintptr_t>(&g_entries[size_t(I(0, int(g_entries.size()) - 1))]); }

// ---- the kinds ----
// The text button (part 50's): the box, +0x70 normal, +0x74 active.
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
// The server row: the text button's, its entry in the base's index (+0x5C), the icons +0x78 .. +0x8C (always there: the
// original's ADD_ENTRY sets them unchecked; a row whose Create failed crashes both).
I3D_frame *C_ServerRow::*const ICONS[] = {&C_ServerRow::m_password,       &C_ServerRow::m_dedicated,       &C_ServerRow::m_voice,
                                          &C_ServerRow::m_activePassword, &C_ServerRow::m_activeDedicated, &C_ServerRow::m_activeVoice};
void RandomRow(C_Control *c)
{
    auto *r = static_cast<C_ServerRow *>(c);
    RandomTextButton(c);
    for (auto icon : ICONS)
        r->*icon = RandomFrame();
    r->m_index = P(0.5) ? int(RandomEntryPtr()) : P(0.5) ? 0 : int(U32());
}
void RowToOrig(C_Control *c, uint8_t *o)
{
    auto *r = static_cast<C_ServerRow *>(c);
    TextButtonToOrig(c, o);
    for (int k = 0; k < 6; ++k)
        OF<void *>(o, 0x78 + 4 * k) = r->*ICONS[k];
}
std::string RowDiffers(C_Control *c, const uint8_t *o)
{
    auto *r = static_cast<C_ServerRow *>(c);
    for (int k = 0; k < 6; ++k)
        if (OF<void *>(o, 0x78 + 4 * k) != r->*ICONS[k])
            return "icon " + std::to_string(k) + " " + PtrName(OF<void *>(o, 0x78 + 4 * k)) + " / " + PtrName(r->*ICONS[k]);
    return TextButtonDiffers(c, o);
}

// The list group (part 56's): the group's focus, +0x70 the cursor, +0x74 the picked, +0x78 the extra (owned), +0x7C the
// selector, +0x80 the selected, +0x84 sorted, +0x88 the origin, +0x8C the down step, +0x90 the across step, +0x94
// visible, +0x98 the first shown, +0x9C the last.
S_vector *NewOrigin()
{
    auto *v = static_cast<S_vector *>(_aligned_malloc(16, 16));
    *v = P(0.2) ? S_vector(0, 0, 0) : S_vector(F(-1, 1), F(-1, 1), F(-1, 1));
    return v;
}
float RandomStep() { return P(0.4) ? 0.0f : P(0.3) ? Pick({0.1f, -0.1f, 0.25f, 1.0f, 0.033f}) : F(-0.5f, 0.5f); }
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
    // (At least one visible, the last shown the first + visible - 1: part 56's.)
    l->m_visible = P(0.7) ? std::max(1, n) : I(1, 11);
    l->m_first = P(0.7) ? 0 : I(0, n + 2);
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
// (An origin Create made: a block per side, compared by its vector.)
bool SameOrigin(const S_vector *o, const S_vector *p)
{
    if (o == p)
        return true;
    return o && p && std::memcmp(o, p, sizeof(S_vector)) == 0;
}
std::string ListDiffers(C_Control *c, const uint8_t *o)
{
    auto *l = static_cast<C_ListGroup *>(c);
    char text[256];
    if (PtrName(OF<void *>(o, 0x70)) != PtrName(l->m_cursor) || PtrName(OF<void *>(o, 0x74)) != PtrName(l->m_picked) ||
        PtrName(OF<void *>(o, 0x78)) != PtrName(l->m_extra) || OF<void *>(o, 0x7C) != l->m_selector || OF<int>(o, 0x80) != l->m_selected ||
        OF<bool>(o, 0x84) != l->m_sorted || !SameOrigin(OF<S_vector *>(o, 0x88), l->m_origin) ||
        Bits(OF<float>(o, 0x8C)) != Bits(l->m_downStep) || Bits(OF<float>(o, 0x90)) != Bits(l->m_acrossStep) ||
        OF<int>(o, 0x94) != l->m_visible || OF<int>(o, 0x98) != l->m_first || OF<int>(o, 0x9C) != l->m_last) {
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

// The server list: the list's, +0xA0 the selected entry, +0xA4 the sort key, +0xA8 the entries (an MSVC6 vector: +0xAC
// first, +0xB0 last, +0xB4 end), +0xB8, +0xBC (unused), +0xC0 the first entry shown, +0xC4 the last.
uint32_t RandomSortKey()
{
    if (P(0.1))
        return U32();
    return uint32_t(Pick({0x00, 0x40, 0x40, 0x80, 0xC0})) | uint32_t(P(0.9) ? I(0, 5) : I(0, 7)) | (P(0.1) ? uint32_t(I(1, 7)) << 3 : 0u);
}
void RandomServer(C_Control *c)
{
    auto *s = static_cast<C_ServerList *>(c);
    RandomList(c);
    s->m_entries.clear();
    for (int k = P(0.15) ? 0 : P(0.3) ? I(17, 250) : I(1, 16); k > 0; --k)
        s->m_entries.push_back(reinterpret_cast<S_ServerEntry *>(RandomEntryPtr()));
    const int n = int(s->m_entries.size());
    s->m_selectedEntry = n && P(0.5) ? s->m_entries[size_t(I(0, n - 1))] : P(0.5) ? nullptr : reinterpret_cast<S_ServerEntry *>(RandomEntryPtr());
    s->m_sortKey = P(0.3) ? 0x40 : RandomSortKey();
    s->m_unknownB8 = uint8_t(P(0.8) ? 0 : U32());
    s->m_unknownBC = P(0.8) ? 0 : U32();
    s->m_firstEntry = P(0.7) ? I(0, std::max(0, n - s->m_visible)) : P(0.7) ? I(0, n + 3) : P(0.5) ? I(-3, -1) : int(U32());
    s->m_lastEntry = P(0.8) ? s->m_firstEntry + s->m_visible - 1 : I(-2, n + 3);
}
void ServerToOrig(C_Control *c, uint8_t *o)
{
    auto *s = static_cast<C_ServerList *>(c);
    ListToOrig(c, o);
    OF<void *>(o, 0xA0) = s->m_selectedEntry;
    OF<uint32_t>(o, 0xA4) = s->m_sortKey;
    std::vector<uint32_t> entries;
    for (S_ServerEntry *e : s->m_entries)
        entries.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e)));
    OVecPut(o, 0xA8, entries.data(), entries.size() * 4);
    OF<uint8_t>(o, 0xB8) = s->m_unknownB8;
    OF<uint32_t>(o, 0xBC) = s->m_unknownBC;
    OF<int>(o, 0xC0) = s->m_firstEntry;
    OF<int>(o, 0xC4) = s->m_lastEntry;
}
std::string EntryName(const void *e)
{
    if (!e)
        return "null";
    const auto at = reinterpret_cast<const S_ServerEntry *>(e) - g_entries.data();
    if (at >= 0 && at < std::ptrdiff_t(g_entries.size()))
        return "e" + std::to_string(at);
    char text[24];
    std::snprintf(text, sizeof text, "%p", e);
    return text;
}
std::string ServerDiffers(C_Control *c, const uint8_t *o)
{
    auto *s = static_cast<C_ServerList *>(c);
    std::string port, orig;
    for (S_ServerEntry *e : s->m_entries)
        port += EntryName(e) + " ";
    for (size_t k = 0; k < OVecBytes(o, 0xA8) / 4; ++k)
        orig += EntryName(reinterpret_cast<void **>(OVecData(o, 0xA8))[k]) + " ";
    if (port != orig)
        return "entries " + orig + "/ " + port;
    char text[200];
    if (OF<void *>(o, 0xA0) != s->m_selectedEntry || OF<uint32_t>(o, 0xA4) != s->m_sortKey || OF<uint8_t>(o, 0xB8) != s->m_unknownB8 ||
        OF<uint32_t>(o, 0xBC) != s->m_unknownBC || OF<int>(o, 0xC0) != s->m_firstEntry || OF<int>(o, 0xC4) != s->m_lastEntry) {
        std::snprintf(text, sizeof text, "server sel %s key %x %x %x window %d %d / sel %s key %x %x %x window %d %d",
                      EntryName(OF<void *>(o, 0xA0)).c_str(), OF<uint32_t>(o, 0xA4), OF<uint8_t>(o, 0xB8), OF<uint32_t>(o, 0xBC), OF<int>(o, 0xC0),
                      OF<int>(o, 0xC4), EntryName(s->m_selectedEntry).c_str(), s->m_sortKey, s->m_unknownB8, s->m_unknownBC, s->m_firstEntry,
                      s->m_lastEntry);
        return text;
    }
    return ListDiffers(c, o);
}

const S_Kind KINDS[] = {
    {"control", 0x00813dd0, 0x6C, Make<C_Control>, NoFields, NoToOrig, NoDiffers},
    {"label", 0x00814188, 0x70, Make<C_LabelControl>, RandomText, TextToOrig, TextDiffers, O_BOX},
    {"row", 0x008154b8, 0x90, Make<C_ServerRow>, RandomRow, RowToOrig, RowDiffers, O_BOX, nullptr, true},
    {"server", 0x00814980, 0xC8, Make<C_ServerList>, RandomServer, ServerToOrig, ServerDiffers, 0, nullptr, false, 0x78, ListExtra},
};
const int KIND_COUNT = int(sizeof KINDS / sizeof KINDS[0]);
const int K_CONTROL = 0, K_LABEL = 1, K_ROW = 2, K_SERVER = 3;

// ---- the planned trees ----
std::vector<S_PlanNode> g_plan;
int PlanAdd(int parent, int kind, int bits, bool child = true)
{
    g_plan.push_back({parent, kind, bits, child});
    return int(g_plan.size()) - 1;
}
// A server list: its rows (mostly the 11 of the game's), its extra control now and then (owned: not a child).
void PlanServer(int parent)
{
    const int at = PlanAdd(parent, K_SERVER, -1);
    for (int k = P(0.6) ? 11 : I(0, 11); k > 0; --k)
        PlanAdd(at, K_ROW, -1);
    if (P(0.3))
        PlanAdd(at, P(0.5) ? K_CONTROL : K_LABEL, -1, false);
}
std::vector<S_PlanNode> TreePlan()
{
    g_plan.clear();
    if (P(0.5)) {
        PlanServer(-1);
    } else {
        const int root = PlanAdd(-1, K_CONTROL, -1);
        for (int k = I(1, 2); k > 0; --k)
            PlanServer(root);
        if (P(0.3))
            PlanAdd(root, K_ROW, -1);
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
// (The extra controls; the rows' ids the list's Create gives (row k: k << 12 | the list's) and their callbacks to the
// list, now and then; the lists' values.)
void AfterTree()
{
    g_cursorMade = true;
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        C_Control *c = g_portNodes[i];
        if (!c->m_parent)
            continue;
        auto &kids = c->m_parent->m_children;
        if (std::find(kids.begin(), kids.end(), c) == kids.end())
            if (auto *l = dynamic_cast<C_ListGroup *>(c->m_parent))
                l->m_extra = c;
    }
    for (size_t i = 0; i < g_portNodes.size(); ++i) {
        auto *s = dynamic_cast<C_ServerList *>(g_portNodes[i]);
        if (!s)
            continue;
        s->m_value = P(0.3) ? 0 : P(0.6) ? g_portNodes[RandomNode()]->m_id : RandomId();
        // (The input cases' lists as the game's: all rows shown, an entry for each. A row's click refreshes the list,
        // its last shown the entries' last (-1 without), and the list's Select then scrolls on for ever towards a
        // clicked row past it (part 56's endless Select, the original's too); the game hides the rows past the entries.)
        if (std::strcmp(g_case, "Slots") == 0 || std::strcmp(g_case, "Input") == 0) {
            const int rows = int(s->m_children.size());
            s->m_visible = std::max(s->m_visible, rows);
            s->m_last = s->m_first + s->m_visible - 1;
            while (int(s->m_entries.size()) < rows)
                s->m_entries.push_back(reinterpret_cast<S_ServerEntry *>(RandomEntryPtr()));
        }
        for (size_t k = 0; k < s->m_children.size(); ++k) {
            C_Control *row = s->m_children[k];
            if (P(0.7))
                row->m_id = uint32_t((k + 1) & 0xFF) << 12 | s->m_id;
            if (P(0.8)) {
                row->m_callbacks.push_back({CONTROL_NOTIFY_CLICK, s, &C_ServerList::RowClicked});
                row->m_callbacks.push_back({CONTROL_NOTIFY_13, s, &C_ServerList::RowNotified});
            }
        }
    }
}
// Create's frames: a server list's rows ("Server1" .. "Server11": all of them mostly), the rows' icons (all mostly) and
// their text buttons' frames.
void CreateFrames(size_t node, int fi)
{
    if (fi < 0)
        return;
    const int server = ChildName("Server1"), icon = ChildName("xpassword");
    auto rowFrame = [&](int f, bool all) {
        if (all || P(0.8))
            for (int k = 0; k < 6; ++k)
                g_arena.child[f][icon + k] = static_cast<int16_t>(I(0, 15));
        if (all) {
            g_arena.child[f][ChildName("normal*")] = static_cast<int16_t>(I(0, 15));
            g_arena.child[f][ChildName("actived*")] = static_cast<int16_t>(I(0, 15));
        }
    };
    if (g_nodeKinds[node] == &KINDS[K_SERVER]) {
        // (A row whose Create failed (a frame or an icon missing) keeps null icons, and the refresh at the list's end
        // shows an entry on it unchecked: both crash. Complete rows mostly; else the list without entries.)
        const bool all = P(0.7);
        if (all || P(0.5))
            for (int k = 0; k < 11; ++k) {
                const int f = I(0, 15);
                g_arena.child[fi][server + k] = static_cast<int16_t>(f);
                rowFrame(f, all);
            }
        if (!all)
            static_cast<C_ServerList *>(g_portNodes[node])->m_entries.clear();
        if (P(0.5))
            g_arena.child[fi][ChildName("selector*")] = static_cast<int16_t>(P(0.8) ? I(0, 15) : -1);
    } else if (g_nodeKinds[node] == &KINDS[K_ROW]) {
        rowFrame(fi, false);
    }
}

// ---- the messages' values ----
uint32_t RowIdOrOther()
{
    const std::vector<size_t> rows = NodesOf(K_ROW);
    if (!rows.empty() && P(0.7))
        return g_portNodes[rows[size_t(I(0, int(rows.size()) - 1))]]->m_id;
    return RandomId();
}
// The window's position: within the entries mostly, past them, negative, far.
uint32_t RandomPosition() { return P(0.7) ? uint32_t(I(0, 20)) : P(0.5) ? uint32_t(I(0, 260)) : uint32_t(Pick({-1, -11, INT_MIN, INT_MAX, INT_MAX - 5})); }
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
        m.value = RowIdOrOther();
        break;
    case CONTROL_MSG_SET_VISIBLE:
        m.value = uint32_t(I(1, 11));   // (part 56's: none visible makes the last -1)
        break;
    case CONTROL_MSG_SET_ACROSS_STEP:
    case CONTROL_MSG_SET_DOWN_STEP: {
        const float step = RandomStep();
        std::memcpy(&m.value2, &step, 4);
        break;
    }
    case CONTROL_MSG_ADD_ITEM:   // (ADD_ENTRY's code: an entry for the list and the row alike)
        m.value2 = P(0.1) ? 0 : RandomEntryPtr();
        break;
    case CONTROL_MSG_SET_ORIGIN:
        m.value2 = P(0.15) ? 0 : reinterpret_cast<uintptr_t>(NewOrigin());
        break;
    case CONTROL_MSG_SET_POSITION:
        m.value = RandomPosition();
        break;
    case CONTROL_MSG_SELECT_INDEX:
        // (Small and not negative: part 56's endless scroll.)
        m.value = uint32_t(I(0, 12));
        break;
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_ADD_TEXT:
        m.text = Pick({"one", "", "two"});
        break;
    case CONTROL_MSG_SET_RUN_TEXT:
        m.text = Pick({"one", "", "two"});
        m.value = uint32_t(Pick({0x1E, 0x1F, 0x20, 0x21, 0x22}));
        m.value2 = uint32_t(I(0, 5));
        break;
    case CONTROL_MSG_SET_VALUE:
        m.value = P(0.3) ? 0 : RandomId();
        break;
    case CONTROL_MSG_SORT:
        m.value = RandomSortKey();
        break;
    }
}

// SET_VISIBLE sets the last shown, not the first: sent to a list at its first (part 56's).
void BeforeMessage(size_t i, const S_ControlMessage &m)
{
    if ((m.id & CONTROL_MSG_MASK) == CONTROL_MSG_SET_VISIBLE)
        if (auto *l = dynamic_cast<C_ListGroup *>(g_portNodes[i]))
            if (!l->IsChild(l->FindChild(m.target)))
                l->m_first = 0;
}

// The round's versions (the port's globals, the original's at 0x00845070 / 0x00845074), font, entries.
void SetVersions(int major, int minor)
{
    g_versionMajor = major;
    g_versionMinor = minor;
    *OriginalAt<int *>(0x00845070) = major;
    *OriginalAt<int *>(0x00845074) = minor;
}
void PickRound()
{
    g_box.editRun = P(0.8);
    g_box.hoverAt = P(0.7);
    g_box.hovered = int16_t(I(-1, 6));
    g_box.selection = int16_t(I(-1, 6));
    g_box.scrollRange = int16_t(P(0.4) ? 0 : I(1, 20));
    SetUnicode(P(0.5));
    SetVersions(P(0.7) ? 1 : I(0, 2), P(0.7) ? 4 : I(3, 5));
    for (S_ServerEntry &e : g_entries)
        RandomEntry(e);
    g_fnPairs.clear();
    g_fnPairs.push_back({reinterpret_cast<uintptr_t>(&C_ServerList::RowClicked), 0x00666650});
    g_fnPairs.push_back({reinterpret_cast<uintptr_t>(&C_ServerList::RowNotified), 0x00666630});
}

// ---- the tests ----
const uint32_t LIST_CODES[] = {
    CONTROL_MSG_REMOVE_ITEM,  CONTROL_MSG_SHOW,          CONTROL_MSG_HIDE,       CONTROL_MSG_CAN_SCROLL_UP, CONTROL_MSG_CAN_SCROLL_DOWN,
    CONTROL_MSG_GET_SCROLL_RANGE, CONTROL_MSG_SET_VISIBLE, CONTROL_MSG_SET_ACROSS_STEP, CONTROL_MSG_SET_DOWN_STEP, CONTROL_MSG_NEXT_ITEM,
    CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_SET_ORIGIN,   CONTROL_MSG_GET_POSITION, CONTROL_MSG_SELECT_INDEX, CONTROL_MSG_SET_VALUE,
    CONTROL_MSG_GET_VALUE,    CONTROL_MSG_SET_TEXT,      0x1000000,              0x2000001,
    // (The part's, more often.)
    CONTROL_MSG_GET_SELECTED, CONTROL_MSG_GET_ENTRY_COUNT, CONTROL_MSG_GET_SORT, CONTROL_MSG_ADD_ITEM, CONTROL_MSG_CLEAR,
    CONTROL_MSG_SET_POSITION, CONTROL_MSG_SORT,          CONTROL_MSG_REFRESH,
    CONTROL_MSG_ADD_ITEM,     CONTROL_MSG_ADD_ITEM,      CONTROL_MSG_ADD_ITEM,   CONTROL_MSG_SET_POSITION,  CONTROL_MSG_SORT,
    CONTROL_MSG_SORT,         CONTROL_MSG_REFRESH,       CONTROL_MSG_REFRESH,    CONTROL_MSG_CLEAR,         CONTROL_MSG_GET_SELECTED};
const uint32_t ROW_CODES[] = {CONTROL_MSG_ADD_ENTRY,      CONTROL_MSG_ADD_ENTRY,  CONTROL_MSG_ADD_ENTRY, CONTROL_MSG_ADD_ENTRY,
                              CONTROL_MSG_ADD_CURRENT_ENTRY, CONTROL_MSG_REMOVE_ITEM, CONTROL_MSG_GET_ITEM, CONTROL_MSG_SET_RUN_TEXT,
                              CONTROL_MSG_SET_TEXT,       CONTROL_MSG_DEACTIVATE, CONTROL_MSG_ACTIVATE,  0x1000000};
size_t PickOf(int kind)
{
    const std::vector<size_t> nodes = NodesOf(kind);
    return nodes.empty() ? SIZE_MAX : nodes[size_t(I(0, int(nodes.size()) - 1))];
}
// A target: everyone's, the node's, a child's (its messages go to it), another.
uint32_t OwnTarget(size_t i)
{
    if (P(0.3))
        return 0xFFFFFFFF;
    if (P(0.3))
        return g_portNodes[i]->m_id;
    if (P(0.5) && !g_portNodes[i]->m_children.empty())
        return g_portNodes[i]->m_children[size_t(I(0, int(g_portNodes[i]->m_children.size()) - 1))]->m_id;
    return RandomId();
}
// The list's and rows' own messages sent straight to them.
void TestOwn()
{
    size_t i = P(0.35) ? PickOf(K_ROW) : SIZE_MAX;
    const bool row = i != SIZE_MAX;
    if (!row)
        i = PickOf(K_SERVER);
    S_ControlMessage m;
    m.target = OwnTarget(i);
    m.id = row ? Pick(ROW_CODES) : Pick(LIST_CODES);
    if (P(0.05))
        m.id |= RandomNotification() & CONTROL_NOTIFY_MASK;
    m.value = uint32_t(P(0.7) ? I(-1, 8) : int(U32()));
    m.value2 = uint32_t(I(-10, 10));
    m.value3 = U32();
    MessageValues(m);
    // (A child's target takes the list's message to the row: an entry for its ADD_ENTRY too.)
    BeforeMessage(i, m);
    g_messageBefore = m;
    if (const char *t = std::getenv("HD2_CONTROLS_TRACE"))
        if (std::atoi(t) == g_round) {
            std::printf("  c%u (%s): message %x target %x value %x value2 %x\n", unsigned(i), g_nodeKinds[i]->name, m.id, m.target, m.value,
                        unsigned(m.value2));
            if (auto *s = dynamic_cast<C_ServerList *>(g_portNodes[i]))
                std::printf("    %u rows, %u entries, window %d %d, visible %d, first %d last %d, key %x\n", unsigned(s->m_children.size()),
                            unsigned(s->m_entries.size()), s->m_firstEntry, s->m_lastEntry, s->m_visible, s->m_first, s->m_last, s->m_sortKey);
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

// The list's and rows' input: the slots with ids of its rows (a row's click: its callbacks, the list's RowClicked), the
// navigation keys, the wheel; ScrollRange (+0x84).
void TestInput()
{
    const size_t list = PickOf(K_SERVER);
    const size_t i = P(0.4) && PickOf(K_ROW) != SIZE_MAX ? PickOf(K_ROW) : list;
    C_Control *p = g_portNodes[i];
    const int id = int(OwnTarget(list));
    if (auto *g = dynamic_cast<C_ControlGroup *>(p))
        if (P(0.5))
            g->m_focus = I(-1, int(g->m_children.size()));
    struct S_Slot {
        uint32_t slot;
        int (C_Control::*port)(int);
    };
    static const S_Slot slots[] = {
        {0x30, &C_Control::OnEnter},   {0x34, &C_Control::OnLeave}, {0x38, &C_Control::OnClick},   {0x38, &C_Control::OnClick},
        {0x3C, &C_Control::OnInput02}, {0x48, &C_Control::OnPress}, {0x40, &C_Control::OnInput05}, {0x5C, &C_Control::OnKeyClick},
        {0x60, &C_Control::OnInput13}, {0x44, &C_Control::OnRelease},
    };
    const int n = int(sizeof slots / sizeof slots[0]);
    const int pick = I(0, n + 2);
    int rp = 0, ro = 0;
    if (pick < n) {
        const int arg = P(0.3) ? Pick({1, 0xF, 0, 2}) : id;
        Diff([&] { rp = (p->*slots[pick].port)(arg); }, [&] { ro = Slot<IntFn>(i, slots[pick].slot)(g_origNodes[i], arg); });
    } else if (pick == n) {
        const int key = Pick({0xC7, 0xC8, 0xC9, 0xCB, 0xCD, 0xCF, 0xD0, 0xD1, 0x1C, 0x01}) | (P(0.1) ? int(U32() & 0xFFFFFF00u) : 0);
        Diff([&] { rp = p->Unknown20(key); }, [&] { ro = Slot<BoolIntFn>(i, 0x20)(g_origNodes[i], key); });
    } else if (pick == n + 1) {
        const int amount = P(0.2) ? I(-3, 3) : (P(0.5) ? 1 : -1) * (Pick({1, 119, 120, 121, 239, 240, 241, 600}));
        Diff([&] { rp = p->OnWheel(id, amount); }, [&] { ro = Slot<Int2Fn>(i, 0x4C)(g_origNodes[i], id, amount); });
    } else if (g_nodeKinds[i] == &KINDS[K_SERVER]) {
        Diff([&] { rp = static_cast<C_ListGroup *>(p)->ScrollRange(); }, [&] { ro = Slot<IntFn>(i, 0x84)(g_origNodes[i], 0); });
    }
    CHECK(rp == ro, "input %d (%s) %x: %d / %d", pick, g_nodeKinds[i]->name, unsigned(id), ro, rp);
}

// The constructors: the list's inline in the original's factory (0x0065d470, redirected by the kit; type 0x18: the list
// group's constructor, the entries' vector's, +0xA0 0, +0xA4 0x40, +0xBC (a byte) 0, +0xC0 0, +0xC4 0); the row's
// inline in the list's Create (the text button's constructor 0x00666ad0, the vtable, +0x5C 0: checked by Create's case).
void TestConstruct()
{
    auto *scene = reinterpret_cast<I3D_scene *>(uintptr_t(0x5CE0E000));
    const uint32_t id = U32();
    C_ListGroup list(scene, id, nullptr);
    C_ServerList server(scene, id, nullptr);
    CHECK(server.m_selectedEntry == nullptr && server.m_sortKey == 0x40 && server.m_entries.empty() && server.m_unknownB8 == 0 &&
              server.m_unknownBC == 0 && server.m_firstEntry == 0 && server.m_lastEntry == 0,
          "the server list's fields at construction");
    CHECK(server.m_id == id && server.m_scene == scene && server.m_visible == list.m_visible && server.m_first == list.m_first &&
              server.m_last == list.m_last && server.m_selected == list.m_selected && server.m_sorted == list.m_sorted,
          "the list group's construction");
    C_ServerRow row(scene, id, nullptr);
    bool icons = true;
    for (auto icon : ICONS)
        icons = icons && row.*icon == nullptr;
    CHECK(row.m_index == 0 && icons && row.m_id == id, "the row's fields at construction");
}

void Noop() {}
// (The original's static CRT heap never made in the copy: its _aligned_malloc (Create's origin) MinGW's.)
void *__cdecl O_AlignedMalloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }

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
               CONTROL_MSG_SET_ORIGIN,      CONTROL_MSG_ADD_CALLBACK,    CONTROL_MSG_GET_SELECTED,   CONTROL_MSG_GET_ENTRY_COUNT,
               CONTROL_MSG_GET_SORT,        CONTROL_MSG_GET_ITEM,        CONTROL_MSG_ADD_CURRENT_ENTRY, CONTROL_MSG_REMOVE_ITEM,
               0x1000000,                   0x2000001,                   0};
    // (The scrolls show and hide the rows; the list's refresh notifies CHANGED and posts to its value.)
    g_loudCodes = {CONTROL_MSG_SHOW,          CONTROL_MSG_HIDE,         CONTROL_MSG_NOTIFY,  CONTROL_MSG_NEXT_ITEM,
                   CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_SET_POSITION, CONTROL_MSG_REFRESH, CONTROL_MSG_SET_VISIBLE,
                   CONTROL_MSG_SELECT_INDEX,  CONTROL_MSG_CLEAR,        CONTROL_MSG_SORT};
    // (ADD_ITEM / ADD_ENTRY carry an entry: sent only by TestOwn.)
    NOTIFY_RAISING = {CONTROL_NOTIFY_05, CONTROL_NOTIFY_0B};
    // (Not in routes: the origins are owned by whom takes them; the entries.)
    g_noRouteCodes = {CONTROL_MSG_SET_ORIGIN, CONTROL_MSG_ADD_ITEM};
    g_tickKinds = {K_SERVER, K_ROW, K_CONTROL};
    g_messageHook = MessageValues;
    g_beforeCreate = CreateFrames;
    g_afterTree = AfterTree;
    g_beforeMessage = BeforeMessage;
    for (const S_Kind &k : KINDS)
        AddLoggedVt(k.vtable);
    // (The origin freed by the port's destructor only: the original's _aligned_free import a no-op.)
    RedirectOriginal(0x007e559c, reinterpret_cast<const void *>(Noop));
    RedirectOriginal(0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc));
}

}  // namespace

int main()
{
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SERVER_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);
    if (!LoadOriginalExe()) {
        std::printf("server_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);
    SetUp();
    int rounds = std::getenv("HD2_SERVER_QUICK") ? 4000 : 20000;
    if (const char *r = std::getenv("HD2_SERVER_ROUNDS"))
        rounds = std::atoi(r);
    const char *only = std::getenv("HD2_SERVER_CASE");
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
    std::printf("server_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
