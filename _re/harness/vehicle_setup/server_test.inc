// Part 9c-v-c's controls (game/controls.cpp: the server list and its rows; common/msvc_sort.h), in
// the text build, run by ControlsTest after BarTest (its fonts, its screen; ScMessages, CbRuns,
// CsRuns, C_CsBox, CsVisual, CsNotified, g_texts' "text <id>", GetGameTypeTextId 7000 + type).
// Prints "server:" with 1 per check met.

#include "common/msvc_sort.h"

namespace {

struct S_ServerResults {
    bool svRow = true, svRowMsgs = true, svRowName = true, svList = true, svAdd = true, svWindow = true;
    bool svSelect = true, svRefresh = true, svSort = true, svSortKeys = true, svMsgs = true;
};

S_ServerEntry SvEntry(const char *name, const char *map, int type, uint32_t ping, uint32_t maxPlayers, uint32_t players)
{
    S_ServerEntry e = {};
    std::snprintf(e.name, sizeof e.name, "%s", name);
    std::snprintf(e.map, sizeof e.map, "%s", map);
    e.gameType = type;
    e.ping = ping;
    e.maxPlayers = maxPlayers;
    e.players = players;
    e.versionMajor = g_versionMajor;
    e.versionMinor = g_versionMinor;
    return e;
}
// A row's frame ("Server<n>": its normal and active visuals, its six icons) under `parent`.
I3D_frame *SvRowFrame(int n, I3D_frame *parent, bool icons = true)
{
    I3D_frame *f = GfFrame(FRAME_DUMMY, CmText("Server%d", n), parent);
    f->m_localMatrix(3) = S_vector(0.5f, 0.25f * static_cast<float>(n), 2.0f);
    CsVisual(CmText("normalsv%d", n).c_str(), f);
    CsVisual(CmText("activedsv%d", n).c_str(), f);
    if (icons)
        for (const char *icon : {"xpassword", "xdedicated", "xvoice", "xpassword01", "xdedicated01", "xvoice01"})
            GfVisual(icon, I3DVISUAL_OBJECT, f);
    return f;
}
I3D_frame *SvIcon(I3D_frame *row, const char *name)
{
    for (I3D_frame *c = row->m_firstChild; c; c = c->m_nextSibling)
        if (std::string(GfName(c)) == name)
            return c;
    return nullptr;
}
std::string SvNames(const std::vector<S_ServerEntry *> &entries)
{
    std::string s;
    for (const S_ServerEntry *e : entries)
        s += std::string(e->name) + " ";
    return s;
}
// The rows' entries ("-" none) and shown flags.
std::string SvRows(const C_ServerList &l)
{
    std::string s;
    for (C_Control *c : l.m_children) {
        const S_ServerEntry *e = static_cast<C_ServerRow *>(c)->GetEntry();
        s += std::string(e ? e->name : "-") + (c->m_shown ? "+ " : " ");
    }
    return s;
}

} // namespace

void ServerTest(I3D_scene *scene)
{
    S_ServerResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- a row: its icons off, given its id, linked to its frames; an entry's runs ---
    {
        I3D_frame *list = GfFrame(FRAME_DUMMY, "svrowlist");
        I3D_frame *frame = SvRowFrame(1, list);
        I3D_frame *icons[6];
        int n = 0;
        for (const char *icon : {"xpassword", "xdedicated", "xvoice", "xpassword01", "xdedicated01", "xvoice01"})
            icons[n++] = SvIcon(frame, icon);
        C_ServerRow row(scene, 0x1C101000, nullptr);
        WM(r.svRow, row.m_index == 0 && row.m_takesInput && !row.GetEntry() && !row.m_password);
        g_gfLog.clear();
        WM(r.svRow, icons[5] && row.Create(frame, list, nullptr) && row.m_password == icons[0] && row.m_dedicated == icons[1] &&
                        row.m_voice == icons[2] && row.m_activePassword == icons[3] && row.m_activeDedicated == icons[4] &&
                        row.m_activeVoice == icons[5]);
        WM(r.svRow, row.m_voice->GetData() == reinterpret_cast<S_FrameData *>(0x1C101000) &&
                        row.m_activeVoice->GetData() == reinterpret_cast<S_FrameData *>(0x1C101000) &&
                        row.m_password->GetData() == reinterpret_cast<S_FrameData *>(0x1C101000) &&
                        row.m_box->m_margin == 0 && CsPlaced(*row.m_box, 0));
        WM(r.svRow, std::count(g_gfLog.begin(), g_gfLog.end(), "on xvoice01 0") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "on xpassword 0") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "link xpassword normalsv1 1") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "link xdedicated normalsv1 1") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "link xvoice normalsv1 1") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "link xpassword01 activedsv1 1") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "link xdedicated01 activedsv1 1") == 1 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "link xvoice01 activedsv1 1") == 1);
        // An entry: its runs, its icons; the version's.
        S_ServerEntry e = SvEntry("Alpha", "Dunes", 2, 45, 8, 3);
        e.password = true;
        e.voice = true;
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&e));
        g_gfLog.clear();
        WM(r.svRowMsgs, row.HandleMessage(&m) && row.GetEntry() == &e && !row.m_inactive &&
                            CsRuns(*row.m_box) == "Alpha/30 Dunes/31 text 7002/32 45/33 3 / 8/34 ");
        WM(r.svRowMsgs, GfLogIs({"on xpassword 1", "on xpassword01 1", "on xdedicated 0", "on xdedicated01 0", "on xvoice 1",
                                 "on xvoice01 1"}));
        // The current one white; another entry: not.
        m = CsMessage(CONTROL_MSG_ADD_CURRENT_ENTRY);
        WM(r.svRowMsgs, row.HandleMessage(&m) && row.m_box->m_colour == 0xFFFFFFFF);
        S_ServerEntry old = SvEntry("Beta", "Port", 1, 7, 16, 16);
        old.versionMinor = g_versionMinor + 1;
        old.dedicated = true;
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&old));
        WM(r.svRowMsgs, row.HandleMessage(&m) && row.m_box->m_colour == 0 && row.m_inactive &&
                            CsRuns(*row.m_box) == "Beta/30 Port/31 text 7001/32 7/33 16 / 16/34 " && row.m_dedicated->IsOn() &&
                            row.m_activeDedicated->IsOn() && !row.m_password->IsOn() && !row.m_voice->IsOn());
        old.versionMinor = g_versionMinor;
        old.versionMajor = g_versionMajor + 1;
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&old));
        handled &= row.HandleMessage(&m);
        WM(r.svRowMsgs, row.m_inactive);
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&e));
        handled &= row.HandleMessage(&m);
        WM(r.svRowMsgs, !row.m_inactive);
        // Got.
        m = CsMessage(CONTROL_MSG_GET_ITEM);
        WM(r.svRowMsgs, row.HandleMessage(&m) && m.value2 == reinterpret_cast<uintptr_t>(&e));
        // None: cleared, white gone; removed: cleared.
        row.HandleMessage(&(m = CsMessage(CONTROL_MSG_ADD_CURRENT_ENTRY)));
        m = CsMessage(CONTROL_MSG_ADD_ENTRY);
        WM(r.svRowMsgs, row.HandleMessage(&m) && !row.GetEntry() && row.m_box->m_runs.empty() && row.m_box->m_colour == 0);
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&e));
        handled &= row.HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM);
        WM(r.svRowMsgs, row.HandleMessage(&m) && !row.GetEntry() && row.m_box->m_runs.empty());
        // Notifications and others: the text button's (true).
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_TEXT, 0, 0, nullptr, 0x1C100000);
        WM(r.svRowMsgs, row.HandleMessage(&m) && row.m_routes.size() == 1);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "hello");
        WM(r.svRowMsgs, row.HandleMessage(&m) && CsRuns(*row.m_box) == "hello/3 ");
        // A name not text in the font: the text 0x7E9 instead.
        const bool unicode = g_gameProfile.m_unicodeFont;
        S_ServerEntry utf = SvEntry("Caf\xC3\xA9", "M", 0, 1, 2, 1);
        S_ServerEntry bad = SvEntry("x\xC3", "M", 0, 1, 2, 1);
        g_gameProfile.m_unicodeFont = false;
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&utf));
        handled &= row.HandleMessage(&m);
        WM(r.svRowName, CsRuns(*row.m_box).find("text 2025/30 ") == 0);
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&e));
        handled &= row.HandleMessage(&m);
        WM(r.svRowName, CsRuns(*row.m_box).find("Alpha/30 ") == 0);
        g_gameProfile.m_unicodeFont = true;
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&utf));
        handled &= row.HandleMessage(&m);
        WM(r.svRowName, CsRuns(*row.m_box).find("Caf\xC3\xA9/30 ") == 0);
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 0, reinterpret_cast<uintptr_t>(&bad));
        handled &= row.HandleMessage(&m);
        WM(r.svRowName, CsRuns(*row.m_box).find("text 2025/30 ") == 0);
        g_gameProfile.m_unicodeFont = unicode;
        // An icon missing: not made.
        I3D_frame *short_ = SvRowFrame(2, list, false);
        GfVisual("xpassword", I3DVISUAL_OBJECT, short_);
        C_ServerRow none(scene, 0x1C102000, nullptr);
        WM(r.svRow, !none.Create(short_, list, nullptr));
        // Only the first missing; the text button's frames missing.
        I3D_frame *nopass = GfFrame(FRAME_DUMMY, "Server3", list);
        CsVisual("normalsv3", nopass);
        CsVisual("activedsv3", nopass);
        for (const char *icon : {"xdedicated", "xvoice", "xpassword01", "xdedicated01", "xvoice01"})
            GfVisual(icon, I3DVISUAL_OBJECT, nopass);
        C_ServerRow nopassRow(scene, 0x1C103000, nullptr);
        WM(r.svRow, !nopassRow.Create(nopass, list, nullptr) && !nopassRow.m_password && nopassRow.m_dedicated);
        I3D_frame *nobutton = GfFrame(FRAME_DUMMY, "Server4", list);
        for (const char *icon : {"xpassword", "xdedicated", "xvoice", "xpassword01", "xdedicated01", "xvoice01"})
            GfVisual(icon, I3DVISUAL_OBJECT, nobutton);
        C_ServerRow nobuttonRow(scene, 0x1C104000, nullptr);
        WM(r.svRow, !nobuttonRow.Create(nobutton, list, nullptr) && !nobuttonRow.m_password);
    }

    // --- the list: 11 rows, a window of entries ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "svframe");
        std::vector<I3D_frame *> rowFrames;
        for (int i = 1; i <= 11; ++i)
            rowFrames.push_back(SvRowFrame(i, frame));
        C_Control root(scene, 0x1C000000, nullptr);
        auto *l = new C_ServerList(scene, 0x1C100000, &root);
        auto *bar = new C_ScTarget("svbar", 0x1C200000);
        bar->m_parent = &root;
        root.m_children = {l, bar};
        WM(r.svList, !l->m_selectedEntry && l->m_sortKey == 0x40 && l->m_entries.empty() && l->m_firstEntry == 0 &&
                         l->m_lastEntry == 0 && l->m_unknownB8 == 0 && l->m_unknownBC == 0 && l->m_sorted);
        l->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        S_ServerEntry stale = SvEntry("stale", "x", 0, 1, 1, 1);
        l->m_selectedEntry = &stale;
        g_gfLog.clear();
        WM(r.svList, l->Create(frame, frame, nullptr) && l->m_children.size() == 11 && !frame->GetData() &&
                         l->m_visible == 11 && l->m_downStep == 0.033f && l->m_acrossStep == 0.0f &&
                         l->m_entries.capacity() == 200 && !l->m_selectedEntry);
        auto row = [&](int i) { return static_cast<C_ServerRow *>(l->m_children[i]); };
        WM(r.svList, row(0)->GetId() == 0x1C101000 && row(10)->GetId() == 0x1C10B000 && row(4)->GetFrame() == rowFrames[4] &&
                         row(0)->m_parent == l && row(0)->m_callbacks.size() == 2 &&
                         row(0)->m_callbacks[0].notification == CONTROL_NOTIFY_CLICK && row(0)->m_callbacks[0].object == l &&
                         row(0)->m_callbacks[1].notification == CONTROL_NOTIFY_13 && row(10)->m_callbacks[1].object == l);
        // Laid out down from the last row's place.
        WM(r.svList, l->m_origin && *l->m_origin == S_vector(0.5f, 2.75f, 2.0f) &&
                         rowFrames[0]->GetPos() == S_vector(0.5f, 2.75f, 2.0f) &&
                         rowFrames[3]->GetPos() == S_vector(0.5f, -(3.0f * 0.033f) + 2.75f, 2.0f));
        // Refreshed empty: every row hidden, CHANGED.
        WM(r.svList, l->m_first == -1 && l->m_last == -1 && !row(0)->m_shown && !row(10)->m_shown &&
                         std::count(g_gfLog.begin(), g_gfLog.end(), "notify 4010000 0") == 1);
        // Entries added: shown in the rows; the window.
        std::vector<S_ServerEntry> es;
        es.reserve(30);
        const char *names[] = {"kilo", "Alpha", "echo", "Lima", "delta", "bravo", "Golf", "juliet", "hotel", "India",
                               "Charlie", "foxtrot", "Mike", "november", "Oscar", "papa", "Quebec", "romeo", "Sierra", "tango"};
        for (int i = 0; i < 20; ++i)
            es.push_back(SvEntry(names[i], names[(i * 7) % 20], i % 4, 100 + (i * 37) % 50 * 3, 4 + i % 3 * 4, i % 5));
        auto add = [&](int i) {
            S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, reinterpret_cast<uintptr_t>(&es[i]));
            return l->HandleMessage(&m);
        };
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_ITEM);
        WM(r.svAdd, l->HandleMessage(&m) && l->m_entries.empty());
        for (int i = 0; i < 3; ++i)
            handled &= add(i);
        WM(r.svAdd, l->m_entries.size() == 3 && l->m_firstEntry == 0 && l->m_lastEntry == 2 && l->m_first == 0 && l->m_last == 2 &&
                        SvRows(*l) == "kilo+ Alpha+ echo+ - - - - - - - - ");
        m = CsMessage(CONTROL_MSG_GET_ENTRY_COUNT);
        WM(r.svAdd, l->HandleMessage(&m) && m.value == 3);
        for (int i = 3; i < 11; ++i)
            handled &= add(i);
        WM(r.svAdd, l->m_firstEntry == 0 && l->m_lastEntry == 10 && l->m_last == 10 && row(10)->GetEntry() == &es[10] &&
                        row(10)->m_shown);
        handled &= add(11);
        handled &= add(12);
        WM(r.svAdd, l->m_firstEntry == 0 && l->m_lastEntry == 10 && row(10)->GetEntry() == &es[10]);
        // The window moved (more entries than rows), the rows refilled.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 2);
        WM(r.svWindow, l->HandleMessage(&m) && l->m_firstEntry == 2 && l->m_lastEntry == 12 && row(0)->GetEntry() == &es[2] &&
                           row(10)->GetEntry() == &es[12]);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 5);
        handled &= l->HandleMessage(&m);
        WM(r.svWindow, l->m_firstEntry == 5 && l->m_lastEntry == 15 && row(7)->GetEntry() == &es[12] &&
                           row(8)->GetEntry() == &es[10] && row(8)->m_shown);
        // Added past the end: the window pulled back; within: kept.
        handled &= add(13);
        WM(r.svWindow, l->m_firstEntry == 3 && l->m_lastEntry == 13 && row(0)->GetEntry() == &es[3]);
        handled &= add(14);
        WM(r.svWindow, l->m_firstEntry == 3 && l->m_lastEntry == 13);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 4);
        handled &= l->HandleMessage(&m);
        handled &= add(15);
        WM(r.svWindow, l->m_firstEntry == 4 && l->m_lastEntry == 14);
        // The scroll range; the window kept when no more than the rows.
        WM(r.svWindow, l->ScrollRange() == 6);
        // A row's click: its entry selected, white; 0E.
        g_gfLog.clear();
        WM(r.svSelect, row(2)->OnClick(0x1C103000) == CONTROL_HANDLED && l->m_selectedEntry == &es[6] &&
                           row(2)->m_box->m_colour == 0xFFFFFFFF && row(3)->m_box->m_colour == 0 &&
                           std::count(g_gfLog.begin(), g_gfLog.end(), "notify 400e000 0") == 1);
        m = CsMessage(CONTROL_MSG_GET_SELECTED);
        WM(r.svSelect, l->HandleMessage(&m) && m.value2 == reinterpret_cast<uintptr_t>(&es[6]));
        // Notification 13 passed on.
        g_gfLog.clear();
        row(5)->Notify(CONTROL_NOTIFY_13, 0);
        WM(r.svSelect, GfLogIs({"notify 4013000 0"}));
        // Refreshed with a bar: its count and position.
        l->m_value = 0x1C200000;
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        WM(r.svRefresh, l->HandleMessage(&m) && ScMessages() == "1c200000 1000020 6 0|1c200000 200002b 4 0|");
        // The window past the entries: rows without entries kept.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 9);
        handled &= l->HandleMessage(&m);
        WM(r.svRefresh, l->m_firstEntry == 9 && l->m_lastEntry == 19 && row(6)->GetEntry() == &es[15] &&
                            row(7)->GetEntry() == &es[11]);
        // Sorted: by name, ascending (the case ignored) and back; the rows refilled.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SORT, 1);
        WM(r.svSort, l->HandleMessage(&m) && l->m_sortKey == 0x41 &&
                         SvNames(l->m_entries) == "Alpha bravo Charlie delta echo foxtrot Golf hotel India juliet kilo Lima "
                                                  "Mike november Oscar papa ");
        WM(r.svSort, row(0)->GetEntry() == &es[1] && row(1)->GetEntry() == &es[5]);
        m = CsMessage(CONTROL_MSG_SORT, 0x81);
        WM(r.svSort, l->HandleMessage(&m) && l->m_sortKey == 0x81 &&
                         SvNames(l->m_entries) == "papa Oscar november Mike Lima kilo juliet India hotel Golf foxtrot echo "
                                                  "delta Charlie bravo Alpha ");
        m = CsMessage(CONTROL_MSG_GET_SORT);
        WM(r.svSort, l->HandleMessage(&m) && m.value == 0x81);
        // The direction alone: reversed (twice: back).
        m = CsMessage(CONTROL_MSG_SORT, 0x40);
        WM(r.svSort, l->HandleMessage(&m) && l->m_sortKey == 0x41 && SvNames(l->m_entries).find("Alpha bravo ") == 0);
        m = CsMessage(CONTROL_MSG_SORT, 0x80);
        handled &= l->HandleMessage(&m);
        WM(r.svSort, l->m_sortKey == 0x81 && SvNames(l->m_entries).find("papa Oscar ") == 0);
        // Again (0): the key's order.
        std::swap(l->m_entries[0], l->m_entries[15]);
        m = CsMessage(CONTROL_MSG_SORT, 0);
        handled &= l->HandleMessage(&m);
        WM(r.svSort, l->m_sortKey == 0x81 && SvNames(l->m_entries).find("papa Oscar ") == 0);
        // The bits between the direction and the field dropped; another field kept the direction.
        l->m_sortKey = 0x79;
        m = CsMessage(CONTROL_MSG_SORT, 0x40);
        handled &= l->HandleMessage(&m);
        WM(r.svSort, l->m_sortKey == 0x81);
        m = CsMessage(CONTROL_MSG_SORT, 4);
        handled &= l->HandleMessage(&m);
        WM(r.svSort, l->m_sortKey == 0x84);
        // Each key against std::sort (every order total: the ties broken by the ping), 20 entries.
        for (int i = 16; i < 20; ++i)
            handled &= add(i);
        auto expect = [&](const std::vector<S_ServerEntry *> &input, bool (*less)(const S_ServerEntry *, const S_ServerEntry *)) {
            std::vector<S_ServerEntry *> v = input;
            std::stable_sort(v.begin(), v.end(), less);
            return v == l->m_entries;
        };
        struct S_Key {
            uint32_t key;
            bool (*less)(const S_ServerEntry *, const S_ServerEntry *);
        };
        const S_Key keys[] = {
            {0x41, [](const S_ServerEntry *a, const S_ServerEntry *b) { return _stricmp(a->name, b->name) < 0; }},
            {0x81, [](const S_ServerEntry *a, const S_ServerEntry *b) { return _stricmp(a->name, b->name) > 0; }},
            {0x42, [](const S_ServerEntry *a, const S_ServerEntry *b) {
                 const int o = _stricmp(a->map, b->map);
                 return o < 0 || (o == 0 && a->ping < b->ping);
             }},
            {0x82, [](const S_ServerEntry *a, const S_ServerEntry *b) {
                 const int o = _stricmp(a->map, b->map);
                 return o > 0 || (o == 0 && a->ping < b->ping);
             }},
            {0x43, [](const S_ServerEntry *a, const S_ServerEntry *b) {
                 return a->gameType < b->gameType || (a->gameType == b->gameType && a->ping < b->ping);
             }},
            {0x83, [](const S_ServerEntry *a, const S_ServerEntry *b) {
                 return a->gameType > b->gameType || (a->gameType == b->gameType && a->ping > b->ping);
             }},
            {0x44, [](const S_ServerEntry *a, const S_ServerEntry *b) { return a->ping < b->ping; }},
            {0x84, [](const S_ServerEntry *a, const S_ServerEntry *b) { return a->ping > b->ping; }},
            {0x45, [](const S_ServerEntry *a, const S_ServerEntry *b) {
                 return a->maxPlayers != b->maxPlayers ? a->maxPlayers < b->maxPlayers
                        : a->players != b->players     ? a->players < b->players
                                                       : a->ping < b->ping;
             }},
            {0x85, [](const S_ServerEntry *a, const S_ServerEntry *b) {
                 return a->maxPlayers != b->maxPlayers ? a->maxPlayers > b->maxPlayers
                        : a->players != b->players     ? a->players > b->players
                                                       : a->ping > b->ping;
             }},
        };
        for (const S_Key &k : keys) {
            std::reverse(l->m_entries.begin(), l->m_entries.end());
            std::swap(l->m_entries[3], l->m_entries[17]);
            const std::vector<S_ServerEntry *> input = l->m_entries;
            l->m_sortKey = k.key;
            m = CsMessage(CONTROL_MSG_SORT, 0);
            handled &= l->HandleMessage(&m);
            WM(r.svSortKeys, expect(input, k.less));
        }
        // Small lists too (the insertion sort alone), and a field with no order: none.
        std::vector<S_ServerEntry *> all = l->m_entries;
        l->m_entries.assign(all.begin(), all.begin() + 7);
        std::reverse(l->m_entries.begin(), l->m_entries.end());
        const std::vector<S_ServerEntry *> seven = l->m_entries;
        l->m_sortKey = 0x44;
        m = CsMessage(CONTROL_MSG_SORT, 0);
        handled &= l->HandleMessage(&m);
        WM(r.svSortKeys, expect(seven, keys[6].less));
        l->m_entries = all;
        std::reverse(l->m_entries.begin(), l->m_entries.end());
        std::vector<S_ServerEntry *> before = l->m_entries;
        l->m_sortKey = 0x46;
        m = CsMessage(CONTROL_MSG_SORT, 0);
        handled &= l->HandleMessage(&m);
        WM(r.svSortKeys, l->m_entries == before);
        l->m_sortKey = 0x40;
        handled &= l->HandleMessage(&m);
        WM(r.svSortKeys, l->m_entries == before);
        // Ties (the order of equals the algorithm's: MSVC6's, the orders as it gives them): 40 entries and 13.
        {
            const char *tieNames[] = {"ash", "Ash", "birch", "BIRCH", "cedar", "Ash", "elm", "Birch"};
            const char *tieMaps[] = {"Dunes", "port", "Port", "dunes"};
            std::vector<S_ServerEntry> ts;
            for (int i = 0; i < 40; ++i)
                ts.push_back(SvEntry(tieNames[(i * 5) % 8], tieMaps[(i * 3) % 4], i % 3, 10 + (i * 7) % 4 * 10, 8 + i % 2 * 8,
                                     static_cast<uint32_t>((i * 11) % 3)));
            auto order = [&](int n, uint32_t key) {
                l->m_entries.clear();
                for (int i = 0; i < n; ++i)
                    l->m_entries.push_back(&ts[(i * 17 + 3) % n]);
                l->m_sortKey = key;
                S_ControlMessage sort = CsMessage(CONTROL_MSG_SORT, 0);
                l->HandleMessage(&sort);
                std::string s;
                for (const S_ServerEntry *e : l->m_entries)
                    s += CmText("%d ", static_cast<int>(e - ts.data()));
                return s;
            };
            struct S_Gold {
                uint32_t key;
                const char *big, *small;
            };
            const S_Gold golds[] = {
                {0x41, "0 17 29 5 1 24 16 33 13 21 25 8 32 37 9 10 27 15 39 11 34 26 3 31 2 19 7 18 35 23 36 28 4 12 20 22 30 38 6 14 ",
                 "1 5 9 0 8 3 7 11 2 10 4 12 6 "},
                {0x81, "6 14 38 30 22 12 28 36 4 20 39 10 27 15 11 34 23 26 3 35 18 31 7 19 2 9 32 37 21 8 25 33 16 13 5 24 1 17 0 29 ",
                 "6 4 12 3 7 11 2 10 1 5 9 0 8 "},
                {0x42, "20 0 32 28 8 4 36 12 16 24 17 5 29 1 33 13 21 25 37 9 27 39 15 11 23 35 7 19 31 3 34 6 22 18 10 30 2 38 14 26 ",
                 "0 4 8 12 1 5 9 3 7 11 2 6 10 "},
                {0x82, "15 27 39 11 23 35 7 19 3 31 26 38 10 22 14 34 6 2 18 30 32 36 4 20 16 28 24 0 8 12 5 25 37 33 1 17 21 13 29 9 ",
                 "3 7 11 2 6 10 0 4 8 12 1 5 9 "},
                {0x43, "0 12 24 36 3 39 27 15 6 18 30 33 9 21 16 28 4 31 19 7 34 10 22 25 1 37 13 8 32 20 11 23 35 38 2 14 26 5 17 29 ",
                 "0 12 3 6 9 4 7 10 1 8 11 2 5 "},
                {0x83, "5 17 29 26 38 14 2 11 23 35 32 20 8 13 1 25 37 10 22 34 7 19 31 4 16 28 33 21 9 6 18 30 39 27 15 3 0 12 36 24 ",
                 "5 2 11 8 1 10 7 4 9 6 3 0 12 "},
                {0x44, "32 20 4 8 16 28 36 0 24 12 15 27 39 11 23 35 3 7 31 19 26 38 14 10 2 22 34 30 6 18 5 13 25 33 17 21 29 37 1 9 ",
                 "0 4 8 12 3 7 11 2 6 10 1 5 9 "},
                {0x84, "9 37 17 5 25 29 21 13 1 33 34 6 22 18 10 30 2 38 14 26 39 27 15 11 23 35 7 19 31 3 0 28 12 16 24 36 4 8 32 20 ",
                 "1 5 9 2 6 10 3 7 11 0 4 8 12 "},
                {0x45, "0 12 24 36 6 18 30 20 32 8 2 38 14 26 4 16 28 10 22 34 27 39 15 3 9 21 33 11 23 35 29 5 17 7 19 31 1 13 25 37 ",
                 "0 12 6 8 2 4 10 3 9 11 5 7 1 "},
                {0x85, "37 25 1 13 31 19 7 29 17 5 23 35 11 33 21 9 39 27 15 3 34 10 22 4 28 16 26 38 14 2 8 20 32 30 18 6 36 24 0 12 ",
                 "1 7 5 11 9 3 10 4 2 8 6 0 12 "},
            };
            for (const S_Gold &g : golds)
                WM(r.svSortKeys, order(40, g.key) == g.big && order(13, g.key) == g.small);
            // The partition where both scans meet on an equal of the pivot: split there.
            int values[] = {1, 2, 3, 3, 3, 4, 5};
            auto less = [](int x, int y) { return x < y; };
            WM(r.svSortKeys, msvc6::UnguardedPartition(values, values + 7, 3, less) - values == 3);
        }
        // Ignored: the previous / next item, a removal (true).
        l->m_firstEntry = 1;
        for (uint32_t id : {CONTROL_MSG_PREVIOUS_ITEM, CONTROL_MSG_NEXT_ITEM, CONTROL_MSG_REMOVE_ITEM}) {
            m = CsMessage(id, 0x1C103000);
            WM(r.svMsgs, l->HandleMessage(&m) && l->m_firstEntry == 1 && l->m_children.size() == 11);
        }
        // To a row: its; notifications: the base's; others: the list group's.
        m = CsMessage(CONTROL_MSG_GET_ITEM, 0, 0, nullptr, 0x1C102000);
        WM(r.svMsgs, l->HandleMessage(&m) && m.value2 == reinterpret_cast<uintptr_t>(row(1)->GetEntry()));
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SORT, 0, 0, nullptr, 0x1C000000);
        WM(r.svMsgs, l->HandleMessage(&m) && l->m_routes.size() == 1);
        m = CsMessage(CONTROL_MSG_SET_VISIBLE, 4);
        WM(r.svMsgs, l->HandleMessage(&m) && l->m_visible == 4);
        m = CsMessage(CONTROL_MSG_SET_VISIBLE, 11);
        handled &= l->HandleMessage(&m);
        // No keys, no tick.
        l->m_focus = 2;
        g_gfLog.clear();
        l->Tick(16);
        WM(r.svMsgs, !l->Unknown20(0xD0) && l->m_focus == 2 && g_gfLog.empty());
        l->m_focus = -1;
        // Cleared: none selected, every row hidden.
        m = CsMessage(CONTROL_MSG_CLEAR);
        WM(r.svMsgs, l->HandleMessage(&m) && l->m_entries.empty() && !l->m_selectedEntry && l->m_first == -1 &&
                         !row(0)->m_shown);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 3);
        handled &= l->HandleMessage(&m);
        WM(r.svMsgs, l->m_firstEntry == 1);
        // Added after it: the window from 0 again; exactly as many as the rows: not moved; the bar told
        // the position only with a range.
        handled &= add(0);
        WM(r.svWindow, l->m_firstEntry == 0 && l->m_lastEntry == 0);
        for (int i = 1; i < 11; ++i)
            handled &= add(i);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 3);
        handled &= l->HandleMessage(&m);
        WM(r.svWindow, l->m_firstEntry == 0 && l->m_lastEntry == 10);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= l->HandleMessage(&m);
        WM(r.svRefresh, ScMessages() == "1c200000 1000020 1 0|1c200000 200002b 0 0|");
        l->m_entries.pop_back();
        g_controlMessages.clear();
        handled &= l->HandleMessage(&m);
        WM(r.svRefresh, ScMessages() == "1c200000 1000020 0 0|");
        g_controlMessages.clear();
        // Deleted with its rows, not the entries.
        root.m_children.clear();
        delete bar;
        delete l;
        // A row's frame missing: not made.
        I3D_frame *part = GfFrame(FRAME_DUMMY, "svpart");
        SvRowFrame(1, part);
        auto *p = new C_ServerList(scene, 0x1C300000, nullptr);
        WM(r.svList, !p->Create(part, part, nullptr) && p->m_children.size() == 1 && p->m_visible == 0);
        delete p;
    }

    WM(r.svMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("server: svRow %d svRowMsgs %d svRowName %d svList %d svAdd %d svWindow %d svSelect %d svRefresh %d "
                "svSort %d svSortKeys %d svMsgs %d\n",
                r.svRow, r.svRowMsgs, r.svRowName, r.svList, r.svAdd, r.svWindow, r.svSelect, r.svRefresh, r.svSort,
                r.svSortKeys, r.svMsgs);
}
