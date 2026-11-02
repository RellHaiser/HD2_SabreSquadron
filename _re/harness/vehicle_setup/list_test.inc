// Part 9c-v-a's controls (game/controls.cpp: the item list and its items), in the text build, run by
// ControlsTest after InfoTest (its fonts, its screen, its cursor; ScMessages, C_ScTarget, CkId,
// g_ctCreateChild). Prints "list:" with 1 per check met.

namespace {

// The navigation keys (DirectInput codes).
constexpr int KEY_HOME = 0xC7, KEY_UP = 0xC8, KEY_PAGE_UP = 0xC9, KEY_LEFT = 0xCB, KEY_RIGHT = 0xCD, KEY_END = 0xCF,
              KEY_DOWN = 0xD0, KEY_PAGE_DOWN = 0xD1;

struct S_ListResults {
    bool lsBase = true, lsLayout = true, lsScroll = true, lsSelect = true, lsKeys = true, lsFocus = true;
    bool lsMsgs = true, lsAdd = true, lsItem = true;
};

// An item: its frame, its input logged ("<name> <slot> <id>"); its leave's result, its keys.
int g_lsLeave = 1;
struct C_LsItem : C_Control {
    std::string name;
    C_LsItem(const std::string &n, uint32_t id, C_Control *parent) : C_Control(nullptr, id, parent), name(n)
    {
        m_frame = GfFrame(FRAME_DUMMY, n);
    }
    ~C_LsItem() override { GfLog("%s deleted", name.c_str()); }
    int Log(const char *slot, int id, int result)
    {
        GfLog("%s %s %x", name.c_str(), slot, id);
        return result;
    }
    int OnEnter(int id) override { return Log("enter", id, CONTROL_HANDLED); }
    int OnLeave(int id) override { return Log("leave", id, g_lsLeave); }
    int OnClick(int id) override { return Log("click", id, CONTROL_HANDLED); }
    int OnPress(int id) override { return Log("press", id, CONTROL_HANDLED); }
    int OnInput02(int id) override { return Log("in02", id, CONTROL_HANDLED); }
    int refreshes = 0;
    bool HandleMessage(S_ControlMessage *m) override
    {
        if ((m->id & CONTROL_MSG_MASK) != CONTROL_MSG_REFRESH)
            GfLog("%s msg %x", name.c_str(), m->id);
        else
            ++refreshes;
        return C_Control::HandleMessage(m);
    }
};
// A description logging its deletion.
struct S_LsDesc : S_ControlDesc {
    S_LsDesc(const char *name, uint32_t id) : S_ControlDesc(0xb, name, id) {}
    ~S_LsDesc() override { GfLog("desc %s deleted", name.c_str()); }
};
// The factory's hook: an item for the description.
void LsCreate(C_Control *parent, I3D_frame *, I3D_frame *, const S_ControlDesc *desc)
{
    parent->m_children.push_back(new C_LsItem(desc->name.c_str(), desc->id, parent));
}
std::string LsShown(const C_ListGroup &l)
{
    std::string s;
    for (C_Control *c : l.m_children)
        s += c->m_shown ? '1' : '0';
    return s;
}
std::string LsOrder(const C_ListGroup &l)
{
    std::string s;
    for (C_Control *c : l.m_children)
        s += static_cast<C_LsItem *>(c)->name + " ";
    return s;
}
S_vector *LsOrigin(float x, float y, float z)
{
    auto *v = static_cast<S_vector *>(_aligned_malloc(sizeof(S_vector), 16));
    *v = S_vector(x, y, z);
    return v;
}

} // namespace

void ListTest(I3D_scene *scene)
{
    S_ListResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the item list: its selector at (0, 0, 0); four items laid out down from the origin ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "lsframe");
        I3D_frame *selector = GfVisual("selectorls", I3DVISUAL_OBJECT, frame);
        GfPlace(selector, S_vector(5.0f, 5.0f, 5.0f));
        C_Control root(scene, 0x10000000, nullptr);
        auto *l = new C_ListGroup(scene, 0x10100000, &root);
        auto *bar = new C_ScTarget("bar", 0x10200000);
        bar->m_parent = &root;
        root.m_children = {l, bar};
        WM(r.lsBase, l->m_selected == -1 && l->m_sorted && l->m_visible == 0 && l->m_first == 0 && l->m_last == 0 &&
                         !l->m_origin && l->m_focus == -1);
        WM(r.lsBase, l->Create(frame, frame, nullptr) && l->m_selector == selector && selector->GetPos() == S_vector(0.0f, 0.0f, 0.0f) &&
                         frame->GetData() == CkId(0x10100000));
        for (int i = 0; i < 4; ++i)
            l->m_children.push_back(new C_LsItem(CmText("it%d", i), 0x10102000 + 0x2000 * i, l));
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(LsOrigin(1.0f, 2.0f, 3.0f)));
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_DOWN_STEP, 0, ScFloat(0.5f));
        handled &= l->HandleMessage(&m);
        WM(r.lsLayout, l->m_downStep == 0.5f && l->m_acrossStep == 0.0f && l->m_origin && l->m_origin->y == 2.0f);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0x10200000);
        handled &= l->HandleMessage(&m);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_SET_VISIBLE, 2);
        handled &= l->HandleMessage(&m);
        WM(r.lsLayout, l->m_visible == 2 && l->m_last == 1 && LsShown(*l) == "1100" && l->ScrollRange() == 3 &&
                           l->m_children[1]->GetFrame()->GetPos() == S_vector(1.0f, 1.5f, 3.0f) &&
                           l->m_children[3]->GetFrame()->GetPos() == S_vector(1.0f, 0.5f, 3.0f) &&
                           ScMessages() == "10200000 1000020 3 0|10200000 200002b 0 0|");
        // Scrolled on and back (the frame by the step), as far as it goes; the range and the position.
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 1 && l->m_last == 2 && LsShown(*l) == "0110" && frame->GetPos() == S_vector(0.0f, 0.5f, 0.0f) &&
                           g_controlMessages.empty());
        m = CsMessage(CONTROL_MSG_CAN_SCROLL_UP, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, m.value == 1);
        m = CsMessage(CONTROL_MSG_CAN_SCROLL_DOWN, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, m.value == 1);
        m = CsMessage(CONTROL_MSG_GET_SCROLL_RANGE, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, m.value == 3);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 2);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 2 && l->m_last == 3 && LsShown(*l) == "0011" && frame->GetPos() == S_vector(0.0f, 1.0f, 0.0f));
        m = CsMessage(CONTROL_MSG_CAN_SCROLL_DOWN, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, m.value == 0);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 5);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 2 && l->m_last == 3);
        m = CsMessage(CONTROL_MSG_GET_POSITION, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, m.value == 2);
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 1 && frame->GetPos() == S_vector(0.0f, 0.5f, 0.0f));
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 0 && l->m_last == 1 && frame->GetPos() == S_vector(0.0f, 0.0f, 0.0f));
        m = CsMessage(CONTROL_MSG_CAN_SCROLL_UP, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, m.value == 0);
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 0 && frame->GetPos() == S_vector(0.0f, 0.0f, 0.0f));
        m = CsMessage(CONTROL_MSG_SET_POSITION, -1);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, l->m_first == 0);

        // Selected: scrolled to, the selector just in front of it (on), the position posted.
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 3);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, l->m_selected == 3 && l->m_first == 2 && l->m_last == 3 && (selector->m_frameFlags & 1) &&
                           selector->GetPos() == S_vector(1.0f, 0.5f, static_cast<float>(3.0f - 0.1)) &&
                           ScMessages() == "10200000 200002b 2 0|");
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, (selector->m_frameFlags & 1) == 0);
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 1);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, l->m_selected == 1 && l->m_first == 0 && (selector->m_frameFlags & 1));
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 9);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, l->m_selected == 1);
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 3);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 0);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, l->m_selected == 0 && l->m_first == 0 && l->m_last == 1);
        // Without a value: not posted.
        l->m_value = 0;
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 2);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, g_controlMessages.empty() && l->m_first == 1);
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, g_controlMessages.empty());
        l->m_value = 0x10200000;
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= l->HandleMessage(&m);

        // The keys: down moves the focus (scrolling at the last shown, the position posted); up back;
        // the ends; the pages.
        g_gfLog.clear();
        WM(r.lsKeys, l->Unknown20(KEY_DOWN) && l->m_focus == 0 && GfLogIs({"it0 enter f"}));
        g_gfLog.clear();
        WM(r.lsKeys, l->Unknown20(KEY_DOWN) && l->m_focus == 1 && GfLogIs({"it0 leave f", "it1 enter f"}) && l->m_first == 0);
        g_controlMessages.clear();
        WM(r.lsKeys, l->Unknown20(KEY_RIGHT) && l->m_focus == 2 && l->m_first == 1 && ScMessages() == "10200000 200002b 1 0|");
        WM(r.lsKeys, l->Unknown20(KEY_UP) && l->m_focus == 1 && l->m_first == 1);
        g_controlMessages.clear();
        WM(r.lsKeys, l->Unknown20(KEY_LEFT) && l->m_focus == 0 && l->m_first == 0 && ScMessages() == "10200000 200002b 0 0|");
        WM(r.lsKeys, !l->Unknown20(KEY_UP) && l->m_focus == 0);
        WM(r.lsKeys, !l->Unknown20(KEY_END) && l->m_focus == 3 && l->m_first == 2 && l->m_last == 3);
        WM(r.lsKeys, !l->Unknown20(KEY_DOWN) && l->m_focus == 3);
        WM(r.lsKeys, !l->Unknown20(KEY_HOME) && l->m_focus == 0 && l->m_first == 0);
        WM(r.lsKeys, !l->Unknown20(KEY_PAGE_DOWN) && l->m_focus == 1);
        WM(r.lsKeys, !l->Unknown20(KEY_PAGE_DOWN) && l->m_focus == 2 && l->m_first == 1);
        WM(r.lsKeys, !l->Unknown20(KEY_PAGE_UP) && l->m_focus == 1);
        // (A page step failing: true, the original's.)
        WM(r.lsKeys, !l->Unknown20(KEY_PAGE_UP) && l->m_focus == 0 && l->m_first == 0);
        WM(r.lsKeys, l->Unknown20(KEY_PAGE_UP) && l->m_focus == 0);
        WM(r.lsKeys, !l->Unknown20(0x1C) && l->m_focus == 0);
        // The focus' own keys first (taking input).
        struct C_LsKeys : C_LsItem {
            using C_LsItem::C_LsItem;
            bool Unknown20(int key) override
            {
                GfLog("%s key %x", name.c_str(), key);
                return key == 0x30;
            }
        };
        auto *keys = new C_LsKeys("ik", 0x10109000, l);
        l->m_children.push_back(keys);
        l->m_focus = 4;
        g_gfLog.clear();
        WM(r.lsKeys, l->Unknown20(0x30) && GfLogIs({"ik key 30"}) && l->m_focus == 4);
        keys->m_takesInput = false;
        g_gfLog.clear();
        WM(r.lsKeys, !l->Unknown20(0x30) && g_gfLog.empty());
        l->m_children.pop_back();
        delete keys;
        l->m_focus = 0;

        // The focus' moves: past items not taking input; a refused leave: not moved (true). (All shown: a hidden
        // item takes no input.)
        for (C_Control *c : l->m_children)
            c->m_shown = true;
        l->m_children[1]->m_takesInput = false;
        WM(r.lsFocus, l->FocusNext() && l->m_focus == 2 && l->FocusPrevious() && l->m_focus == 0);
        l->m_children[1]->m_takesInput = true;
        g_lsLeave = 0;
        WM(r.lsFocus, l->FocusNext() && l->m_focus == 0 && l->FocusPrevious() == false && l->m_focus == 0);
        l->m_focus = 2;
        WM(r.lsFocus, l->FocusPrevious() && l->m_focus == 2);
        g_lsLeave = 1;
        l->m_focus = -1;
        g_gfLog.clear();
        WM(r.lsFocus, l->FocusPrevious() && l->m_focus == 3 && GfLogIs({"it3 enter f"}));
        WM(r.lsFocus, !l->FocusNext() && l->m_focus == 3);
        // The click and the press: the group's (the focus'), handled: it selected.
        l->m_focus = 2;
        g_gfLog.clear();
        WM(r.lsFocus, l->OnClick(0x10106000) == CONTROL_HANDLED && l->m_selected == 2 && !g_gfLog.empty() &&
                          g_gfLog[0] == "it2 click 10106000");
        l->m_focus = 3;
        WM(r.lsFocus, l->OnPress(0x10108000) == CONTROL_HANDLED && l->m_selected == 3 && l->m_last == 3);
        l->m_focus = -1;
        WM(r.lsFocus, l->OnClick(1) == 0 && l->OnPress(1) == 0 && l->m_selected == 3);
        // The wheel: the bar's page per 120.
        g_gfLog.clear();
        WM(r.lsFocus, l->OnWheel(1, 240) == CONTROL_HANDLED && GfLogIs({"bar msg 0 1000023 0", "bar msg 0 1000023 0"}));
        g_gfLog.clear();
        WM(r.lsFocus, l->OnWheel(1, -1) == CONTROL_HANDLED && GfLogIs({"bar msg 0 1000024 0"}));
        g_gfLog.clear();
        WM(r.lsFocus, l->OnWheel(1, 0) == CONTROL_HANDLED && g_gfLog.empty());
        l->m_value = 0x10300000;
        WM(r.lsFocus, l->OnWheel(1, 120) == CONTROL_HANDLED && g_gfLog.empty());
        l->m_value = 0;
        WM(r.lsFocus, l->OnWheel(1, 120) == CONTROL_NOT_HANDLED);
        l->m_value = 0x10200000;

        // Picked: the previous one's input 02 and leave (1); show and hide drop it.
        C_Control *first = l->m_children[0], *second = l->m_children[1];
        WM(r.lsItem, l->PickItem(first) && l->m_picked == first);
        g_gfLog.clear();
        WM(r.lsItem, l->PickItem(second) && l->m_picked == second && GfLogIs({"it0 in02 1", "it0 leave 1"}));
        g_gfLog.clear();
        WM(r.lsItem, l->PickItem(second) && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_HIDE);
        handled &= l->HandleMessage(&m);
        WM(r.lsItem, !l->m_picked && !l->m_shown);
        l->PickItem(first);
        m = CsMessage(CONTROL_MSG_SHOW);
        handled &= l->HandleMessage(&m);
        WM(r.lsItem, !l->m_picked && l->m_shown);

        // The across step (the other step 0): items across, the frame moved back.
        m = CsMessage(CONTROL_MSG_SET_ACROSS_STEP, 0, ScFloat(0.25f));
        handled &= l->HandleMessage(&m);
        WM(r.lsLayout, l->m_acrossStep == 0.25f && l->m_downStep == 0.0f);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= l->HandleMessage(&m);
        const S_vector before = frame->GetWorldPos();
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= l->HandleMessage(&m);
        WM(r.lsLayout, l->m_children[2]->GetFrame()->GetPos() == S_vector(1.5f, 2.0f, 3.0f));
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsLayout, frame->GetPos() == S_vector(before.x - 0.25f, before.y, before.z));
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsLayout, frame->GetPos() == S_vector(before.x, before.y, before.z));
        m = CsMessage(CONTROL_MSG_SET_DOWN_STEP, 0, ScFloat(0.5f));
        handled &= l->HandleMessage(&m);

        // Removed by id: the screen told of its frame, deleted; scrolled back to fill the shown.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 2);
        handled &= l->HandleMessage(&m);
        l->PickItem(l->m_children[3]);
        I3D_frame *thirdFrame = l->m_children[3]->GetFrame();
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM, 0x10108000);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_children.size() == 3 && LsOrder(*l) == "it0 it1 it2 " && !l->m_picked && l->m_first == 1 &&
                         l->m_last == 2 && !g_gfLog.empty() && g_gfLog[0] == "it3 deleted" &&
                         ScMessages().find("0 1000068 0 " + EdPtr(thirdFrame) + "|") == 0);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM, 0x10109000);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_children.size() == 3 && g_controlMessages.empty());
        // To a child by its target; a route kept; the rest the base's.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_INDEX, 4, 0, nullptr, 0x10104000);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_children[1]->m_index == 4 && GfLogIs({"it1 msg 1000009"}));
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_NEXT_ITEM, 0, 0, nullptr, 0x10100000);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 5);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_routes.size() == 1 && l->m_index == 5);

        // Added from a description (the root frame its value): made by the factory, sorted by id,
        // refreshed; an id already there: not; the description deleted either way.
        g_ctCreateChild = &LsCreate;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_ADD_ITEM, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(selector)),
                      reinterpret_cast<uintptr_t>(new S_LsDesc("ia", 0x10103000)));
        handled &= l->HandleMessage(&m);
        WM(r.lsAdd, LsOrder(*l) == "it0 ia it1 it2 " && !g_gfLog.empty() && g_gfLog[0] == "child lsframe selectorls ia 11" &&
                        g_gfLog.back() == "desc ia deleted" && l->m_children[1]->GetFrame()->GetPos() == S_vector(1.0f, 1.5f, 3.0f));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, reinterpret_cast<uintptr_t>(new S_LsDesc("ib", 0x10104000)));
        handled &= l->HandleMessage(&m);
        WM(r.lsAdd, l->m_children.size() == 4 && GfLogIs({"desc ib deleted"}));
        l->m_sorted = false;
        m = CsMessage(CONTROL_MSG_ADD_ITEM, 0, reinterpret_cast<uintptr_t>(new S_LsDesc("ic", 0x10101000)));
        handled &= l->HandleMessage(&m);
        WM(r.lsAdd, LsOrder(*l) == "it0 ia it1 it2 ic ");
        l->m_sorted = true;
        l->Sort();
        WM(r.lsAdd, LsOrder(*l) == "ic it0 ia it1 it2 ");
        g_ctCreateChild = nullptr;

        // Cleared: each told and deleted, the frame back at 0, the first shown.
        l->PickItem(l->m_children[0]);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_children.empty() && !l->m_picked && l->m_first == 0 && l->m_last == 1 &&
                         frame->GetPos() == S_vector(0.0f, 0.0f, 0.0f) && g_controlMessages.size() >= 5 &&
                         ScMessages().find("0 1000068 0 ") == 0);
        // (Without items, scrolling on goes on: the original's.)
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_first == 1);
        root.m_children.clear();
        delete bar;
        g_gfLog.clear();
        delete l;
        // Without a selector: made.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "lsbare");
        C_ListGroup b(scene, 0x10400000, nullptr);
        WM(r.lsBase, b.Create(bare, bare, nullptr) && !b.m_selector);
        b.Select(0);
        WM(r.lsBase, b.m_selected == -1);
        frame->SetData(nullptr);
    }

    // --- its item: a text button; picked by its click (its parent's pick), the box's hover while
    // not picked ---
    {
        I3D_frame *listFrame = GfFrame(FRAME_DUMMY, "liparent");
        C_ListGroup list(scene, 0x11100000, nullptr);
        list.Create(listFrame, listFrame, nullptr);
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "liframe");
        CsVisual("normalli", frame);
        GfVisual("activedli", I3DVISUAL_OBJECT, frame);
        I3D_frame *frame2 = GfFrame(FRAME_DUMMY, "liframe2");
        CsVisual("normalli2", frame2);
        GfVisual("activedli2", I3DVISUAL_OBJECT, frame2);
        auto *a = new C_ListItem(scene, 0x11101000, &list);
        auto *b = new C_ListItem(scene, 0x11102000, &list);
        list.m_children = {a, b};
        WM(r.lsItem, a->Create(frame, frame, nullptr) && b->Create(frame2, frame2, nullptr) && !a->m_picked && a->m_takesInput);
        C_TextBox &box = *a->m_box;
        if (box.m_lines.empty())
            box.m_lines.push_back(new S_TextLine());
        box.m_hover = -1;
        a->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.lsItem, a->OnEnter(1) == CONTROL_HANDLED && box.m_hover == 0 && GfLogIs({"notify 4003000 0"}));
        g_gfLog.clear();
        WM(r.lsItem, a->OnLeave(1) == CONTROL_HANDLED && box.m_hover == -1 && GfLogIs({"notify 4004000 0"}));
        g_gfLog.clear();
        WM(r.lsItem, a->OnClick(1) == CONTROL_HANDLED && a->m_picked && list.m_picked == a &&
                         GfLogIs({"on activedli 1", "on normalli 0", "update activedli", "update normalli", "notify 4003000 0",
                                  "notify 4001000 0"}));
        // Picked: the hover not changed.
        box.m_hover = -1;
        g_gfLog.clear();
        WM(r.lsItem, a->OnEnter(1) == CONTROL_HANDLED && box.m_hover == -1 && GfLogIs({"notify 4003000 0"}));
        box.m_hover = 0;
        WM(r.lsItem, a->OnLeave(1) == CONTROL_HANDLED && box.m_hover == 0);
        // Another picked: this one unpicked (its input 02 with id 1: the text button's leave).
        g_gfLog.clear();
        b->OnClick(1);
        WM(r.lsItem, !a->m_picked && b->m_picked && list.m_picked == b && !g_gfLog.empty() &&
                         std::find(g_gfLog.begin(), g_gfLog.end(), "notify 4002000 0") != g_gfLog.end() &&
                         std::find(g_gfLog.begin(), g_gfLog.end(), "on normalli 1") != g_gfLog.end());
        g_gfLog.clear();
        WM(r.lsItem, a->OnInput02(2) == CONTROL_HANDLED && g_gfLog.empty());
        // The press: notified, then the click.
        g_gfLog.clear();
        WM(r.lsItem, a->OnPress(1) == CONTROL_HANDLED && a->m_picked && !b->m_picked && !g_gfLog.empty() &&
                         g_gfLog[0] == "notify 4006000 0" && g_gfLog.back() == "notify 4001000 0");
        a->m_anyCallback = S_ControlCallback();
        frame->SetData(nullptr);
        frame2->SetData(nullptr);
    }

    // --- more: a fresh list of three (two shown) ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "lmframe");
        I3D_frame *selector = GfVisual("selectorlm", I3DVISUAL_OBJECT, frame);
        C_Control root(scene, 0x14000000, nullptr);
        auto *l = new C_ListGroup(scene, 0x14100000, &root);
        auto *bar = new C_ScTarget("mbar", 0x14200000);
        bar->m_parent = &root;
        root.m_children = {l, bar};
        l->Create(frame, frame, nullptr);
        std::vector<C_LsItem *> items;
        for (int i = 0; i < 3; ++i) {
            items.push_back(new C_LsItem(CmText("im%d", i), 0x14102000 + 0x2000 * i, l));
            l->m_children.push_back(items.back());
        }
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(LsOrigin(1.0f, 2.0f, 3.0f)));
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_DOWN_STEP, 0, ScFloat(0.5f));
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0x14200000);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_VISIBLE, 2);
        handled &= l->HandleMessage(&m);
        // The first item laid out too; each refreshed by a scroll.
        WM(r.lsLayout, items[0]->GetFrame()->GetPos() == S_vector(1.0f, 2.0f, 3.0f) && items[0]->refreshes > 0);
        const int refreshes = items[2]->refreshes;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, items[2]->refreshes == refreshes + 1 &&
                           std::count(g_gfLog.begin(), g_gfLog.end(), "update lmframe") == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, std::count(g_gfLog.begin(), g_gfLog.end(), "update lmframe") == 1);
        m = CsMessage(CONTROL_MSG_SET_ACROSS_STEP, 0, ScFloat(0.25f));
        handled &= l->HandleMessage(&m);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, std::count(g_gfLog.begin(), g_gfLog.end(), "update lmframe") == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= l->HandleMessage(&m);
        WM(r.lsScroll, std::count(g_gfLog.begin(), g_gfLog.end(), "update lmframe") == 1);
        m = CsMessage(CONTROL_MSG_SET_DOWN_STEP, 0, ScFloat(0.5f));
        handled &= l->HandleMessage(&m);
        // The selector on with the first shown selected (after a refresh's scroll).
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 0);
        handled &= l->HandleMessage(&m);
        selector->m_frameFlags &= ~1u;
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, (selector->m_frameFlags & 1) != 0);
        // And with the last shown.
        m = CsMessage(CONTROL_MSG_SELECT_INDEX, 1);
        handled &= l->HandleMessage(&m);
        selector->m_frameFlags &= ~1u;
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= l->HandleMessage(&m);
        WM(r.lsSelect, l->m_first == 0 && l->m_last == 1 && (selector->m_frameFlags & 1) != 0);
        // The click selects (from none).
        l->m_selected = -1;
        l->m_focus = 1;
        WM(r.lsFocus, l->OnClick(1) == CONTROL_HANDLED && l->m_selected == 1);
        l->m_selected = -1;
        WM(r.lsFocus, l->OnPress(1) == CONTROL_HANDLED && l->m_selected == 1);
        l->m_focus = -1;
        // The wheel's small turns: one page each.
        g_gfLog.clear();
        WM(r.lsFocus, l->OnWheel(1, 1) == CONTROL_HANDLED && GfLogIs({"mbar msg 0 1000023 0"}));
        g_gfLog.clear();
        WM(r.lsFocus, l->OnWheel(1, 121) == CONTROL_HANDLED && GfLogIs({"mbar msg 0 1000023 0", "mbar msg 0 1000023 0"}));
        // Page down at the end: a step fails, true.
        for (C_Control *c : l->m_children)
            c->m_shown = true;
        m = CsMessage(CONTROL_MSG_SET_POSITION, 1);
        handled &= l->HandleMessage(&m);
        l->m_focus = 2;
        WM(r.lsKeys, l->Unknown20(0xD1) && l->m_focus == 2);
        // A range of 1 (two of two): the position posted too.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= l->HandleMessage(&m);
        auto *gone = l->m_children[2];
        l->m_children.pop_back();
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->ScrollRange() == 1 && ScMessages() == "14200000 1000020 1 0|14200000 200002b 0 0|");
        l->m_children.push_back(gone);
        // The first removed: scrolled back to 0 to fill the shown; refreshed (posted).
        m = CsMessage(CONTROL_MSG_SET_POSITION, 1);
        handled &= l->HandleMessage(&m);
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_REMOVE_ITEM, 0x14102000);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_children.size() == 2 && l->m_first == 0 && !g_gfLog.empty() && g_gfLog[0] == "im0 deleted" &&
                         ScMessages().find("14200000 1000020") != std::string::npos);
        // Cleared: each frame told, each deleted, refreshed (posted).
        I3D_frame *lastFrame = l->m_children.back()->GetFrame();
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= l->HandleMessage(&m);
        WM(r.lsMsgs, l->m_children.empty() && std::count(g_gfLog.begin(), g_gfLog.end(), "im2 deleted") == 1 &&
                         std::count(g_gfLog.begin(), g_gfLog.end(), "im1 deleted") == 1 &&
                         ScMessages().find("0 1000068 0 " + EdPtr(lastFrame) + "|") == 0 &&
                         ScMessages().find("14200000 1000020") != std::string::npos);
        // The extra control deleted with the list.
        l->m_extra = new C_LsItem("ex", 0, nullptr);
        root.m_children.clear();
        delete bar;
        g_gfLog.clear();
        delete l;
        WM(r.lsBase, std::count(g_gfLog.begin(), g_gfLog.end(), "ex deleted") == 1);
    }

    WM(r.lsMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("list: lsBase %d lsLayout %d lsScroll %d lsSelect %d lsKeys %d lsFocus %d lsMsgs %d lsAdd %d lsItem %d\n", r.lsBase,
                r.lsLayout, r.lsScroll, r.lsSelect, r.lsKeys, r.lsFocus, r.lsMsgs, r.lsAdd, r.lsItem);
}
