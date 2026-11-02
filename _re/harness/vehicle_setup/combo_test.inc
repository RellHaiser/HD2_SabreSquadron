// Part 9c-iv-b's controls (game/controls.cpp: the scrolled text, the combo box and its header, the
// titled combo box, the chat input; the group's EnterChild / FocusUnknown20), in the text build, run
// by ControlsTest after EditTest (its fonts, its screen, its cursor; ScArrow, ScSliderFrames,
// ScMessages, CkId, EdPtr, EdRun). Prints "combo:" with 1 per check met.

namespace {

struct S_ComboResults {
    bool cbScrolled = true, cbScrolledInput = true, cbScrolledMsgs = true, cbHeader = true, cbCombo = true;
    bool cbComboInput = true, cbComboMsgs = true, cbTitled = true, cbChat = true, cbChatMsgs = true;
};

// The scrolled text's frames under `parent`: "text<tag>" (a visual) and "scroll<tag>" (its arrows
// and slider).
void CbScrolledFrames(I3D_frame *parent, const std::string &tag)
{
    CsVisual(("text" + tag).c_str(), parent);
    I3D_frame *scroll = CsVisual(("scroll" + tag).c_str(), parent);
    ScArrow("barrow up" + tag, tag + "u", scroll);
    ScArrow("barrow down" + tag, tag + "d", scroll);
    ScSliderFrames("slider" + tag, tag + "s", scroll, S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, -0.5f, 0.0f),
                   S_vector(0.5f, -0.45f, 0.0f));
}
// The chat input's sent line (the notification's text).
std::string g_cbSent;
bool CbSent(void *, void *, C_Control *, uint32_t notification, uint32_t value)
{
    GfLog("notify %x", notification);
    if (notification == CONTROL_NOTIFY_SEND)
        g_cbSent = reinterpret_cast<const char *>(static_cast<uintptr_t>(value));
    return true;
}
std::string CbRuns(const C_Control *control)
{
    return CsRuns(*static_cast<const C_TextControl *>(control)->m_box);
}

} // namespace

void ComboTest(I3D_scene *scene)
{
    S_ComboResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the scrolled text: its list (id | 8, child 0) and bar (id | 4, child 1, a page step of 5),
    // each the other's value; its frame's id cleared ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "stframe");
        CbScrolledFrames(frame, "st");
        auto *t = new C_ScrolledText(scene, 0xA0100000, nullptr);
        WM(r.cbScrolled, t->Create(frame, frame, nullptr) && t->m_children.size() == 2 &&
                             t->m_children[0] == t->m_list && t->m_children[1] == t->m_bar && !frame->GetData());
        WM(r.cbScrolled, t->m_list->m_id == 0xA0100008 && t->m_bar->m_id == 0xA0100004 && t->m_bar->m_pageStep == 5 &&
                             t->m_bar->m_value == 0xA0100008 && t->m_list->m_value == 0xA0100004 && t->m_list->m_parent == t &&
                             t->m_list->m_frame == Find(frame, "textst", 0xFFFF));
        WM(r.cbScrolled, t->FindChild(0xA0100008) == 0 && t->FindChild(4) == 1 && t->FindChild(7) == 1 && t->FindChild(5) == 1 &&
                             t->FindChild(0) == -1 && t->FindChild(3) == -1 && t->FindChild(9) == -1 && t->FindChild(0xC) == -1);
        C_TextBox &box = *t->m_list->m_box;
        // The list's messages to the list; to a child by its target; the rest the base's.
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "a");
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 4, 42);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_RUN_TEXT, 5, 0, "b");
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, CsRuns(box) == "b/5 text 42/4 ");
        m = CsMessage(CONTROL_MSG_GET_ITEM, 1);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, std::string(m.text.c_str()) == "text 42" && t->m_list->m_selected == 1);
        m = CsMessage(CONTROL_MSG_GET_TEXT);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, m.value == 1 && std::string(m.text.c_str()) == "text 42");
        m = CsMessage(CONTROL_MSG_INSERT_FIRST, 6, 0, "c");
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_RUNS);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, m.value == 0 && CsRuns(box) == "c/6 b/5 text 42/4 ");
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 0, 0, "B");
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, m.value == 1 && t->m_list->m_selected == 1);
        m = CsMessage(CONTROL_MSG_REMOVE_SELECTED);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, CsRuns(box) == "c/6 text 42/4 ");
        m = CsMessage(CONTROL_MSG_SET_MARGIN, 4);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_IGNORE_INPUT);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, !t->m_list->m_takesInput && t->m_takesInput && box.m_margin == 4);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0xA0100004);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, 0, nullptr);
        m.value2 = reinterpret_cast<uintptr_t>(t);
        m.value3 = reinterpret_cast<uintptr_t>(&CsNotified);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, t->m_list->m_callbacks.size() == 1 && t->m_callbacks.empty() && t->m_list->m_value == 0xA0100004);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, !g_controlMessages.empty() && ScMessages().find("a0100004 1000020") == 0);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_EDIT);
        g_controlMessages.clear();
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, ScMessages() == "0 20000bf 0 " + EdPtr(t->m_list) + "|");
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, box.NumRuns() == 0);
        m = CsMessage(CONTROL_MSG_SET_COUNT, 10, 0, nullptr, 0xA0100004);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 3);
        handled &= t->HandleMessage(&m);
        WM(r.cbScrolledMsgs, t->m_bar->m_count == 10 && t->m_index == 3 && t->m_list->m_index == -1);

        // The wheel: the bar's page up or down per 120 (a page step of 5).
        t->m_bar->m_position = 9;
        WM(r.cbScrolledInput, t->OnWheel(1, 1) == CONTROL_HANDLED && t->m_bar->m_position == 4);
        WM(r.cbScrolledInput, t->OnWheel(1, -121) == CONTROL_HANDLED && t->m_bar->m_position == 9);
        t->m_bar->m_position = 9;
        WM(r.cbScrolledInput, t->OnWheel(1, 240) == CONTROL_HANDLED && t->m_bar->m_position == 0);
        WM(r.cbScrolledInput, t->OnWheel(1, 0) == CONTROL_NOT_HANDLED && t->m_bar->m_position == 0);
        WM(r.cbScrolledInput, t->OnWheel(1, -121) == CONTROL_HANDLED && t->m_bar->m_position == 10);
        t->m_bar->m_position = 0;
        // The enter: the child with the id (the focus); 0xF: Unknown20 (the focus'), not taken: the
        // child's; the focus' leave refused: not handled.
        t->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        WM(r.cbScrolledInput, t->OnEnter(0xA0100008) == CONTROL_HANDLED && t->m_focus == 0);
        WM(r.cbScrolledInput, t->OnEnter(0xF) == 0 && t->m_focus == 0 && t->OnEnter(0xA0100003) == 0 && t->m_focus == 0);
        auto *k = new C_CsChild("sk", 0xA0100000);
        t->m_children.push_back(k);
        t->m_focus = 2;
        g_csReturn = 0;
        g_gfLog.clear();
        WM(r.cbScrolledInput, t->OnEnter(0xA0100008) == CONTROL_NOT_HANDLED && t->m_focus == 2 &&
                                  GfLogIs({"sk leave a0100008"}));
        g_gfLog.clear();
        WM(r.cbScrolledInput, t->OnEnter(0xF) == 0 && !t->Unknown20(3) && GfLogIs({"sk u20 15", "sk u20 3"}));
        g_csReturn = 5;
        g_gfLog.clear();
        WM(r.cbScrolledInput, t->OnEnter(0xF) == CONTROL_NOT_HANDLED && t->Unknown20(4) && GfLogIs({"sk u20 15", "sk u20 4"}));
        // (Not the group's: the focus' taking input not asked.)
        k->m_takesInput = false;
        WM(r.cbScrolledInput, t->Unknown20(4));
        t->m_focus = -1;
        WM(r.cbScrolledInput, !t->Unknown20(4));
        t->m_children.pop_back();
        delete k;
        // The press: the focus' with the id (the list's: notified too); else 0.
        t->m_focus = 0;
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.cbScrolledInput, t->OnPress(0xA0100008) == CONTROL_HANDLED && !g_gfLog.empty() && g_gfLog.back() == "notify 4006000 0");
        t->m_focus = 1;
        g_gfLog.clear();
        WM(r.cbScrolledInput, t->OnPress(0xA0100004) == CONTROL_HANDLED && t->m_bar->m_held &&
                                  std::find(g_gfLog.begin(), g_gfLog.end(), "notify 4006000 0") == g_gfLog.end());
        t->m_bar->m_held = false;
        WM(r.cbScrolledInput, t->OnPress(0xA0100008) == 0);
        t->m_focus = -1;
        WM(r.cbScrolledInput, t->OnPress(0xA0100008) == 0);
        t->m_anyCallback = S_ControlCallback();
        // The strings: lines (style 0x13), then a refresh.
        g_controlMessages.clear();
        t->SetStrings({C_str("x"), C_str("y")});
        WM(r.cbScrolledMsgs, CsRuns(box) == "x/19 y/19 " && !g_controlMessages.empty());
        g_controlMessages.clear();
        t->SetStrings({});
        WM(r.cbScrolledMsgs, !g_controlMessages.empty());
        // Inactive: the parts too.
        t->SetInactive(true);
        WM(r.cbScrolled, t->m_inactive && t->m_bar->m_inactive && t->m_list->m_inactive);
        t->SetInactive(false);
        WM(r.cbScrolled, !t->m_inactive && !t->m_list->m_inactive);
        delete t;
        // Without parts: made; the wheel, the strings and the list's messages nothing.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "stbare");
        C_ScrolledText b(scene, 0xA0200000, nullptr);
        WM(r.cbScrolled, b.Create(bare, bare, nullptr) && b.m_children.empty() && b.OnWheel(1, 120) == CONTROL_NOT_HANDLED);
        b.SetStrings({C_str("x")});
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "a");
        handled &= b.HandleMessage(&m);
        b.SetInactive(true);
        WM(r.cbScrolled, b.m_inactive);
        // Only the list: no values told.
        I3D_frame *only = GfFrame(FRAME_DUMMY, "stonly");
        CsVisual("textso", only);
        C_ScrolledText o(scene, 0xA0300000, nullptr);
        WM(r.cbScrolled, o.Create(only, only, nullptr) && o.m_list && !o.m_bar && o.m_list->m_value == 0);
        for (I3D_frame *c = frame->m_firstChild; c; c = c->m_nextSibling)
            c->SetData(nullptr);
    }

    // --- the combo box's header: its frame turned to the back, the run 0, the box placed ---
    {
        I3D_frame *frame = CsVisual("ceframe");
        auto *h = new C_ComboEdit(scene, 0xA1100002, nullptr);
        g_gfLog.clear();
        WM(r.cbHeader, h->Create(frame, frame, nullptr) && h->m_run == 0 && CsPlaced(*h->m_box, 10) &&
                           GfLogIs({"setdir ceframe 0.00000 0.00000 -1.00000 0.000000", "update ceframe"}) &&
                           frame->GetData() == CkId(0xA1100002));
        C_TextBox &box = *h->m_box;
        h->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.cbHeader, h->OnEnter(1) == CONTROL_HANDLED && GfLogIs({"notify 4003000 0"}));
        h->m_inactive = true;
        g_gfLog.clear();
        WM(r.cbHeader, h->OnEnter(1) == 0 && g_gfLog.empty());
        h->m_inactive = false;
        box.m_hover = 2;
        g_gfLog.clear();
        WM(r.cbHeader, h->OnLeave(1) == CONTROL_NOT_HANDLED && box.m_hover == -1 &&
                           GfLogIs({"notify 4004000 0", "notify 4004000 0"}));
        g_controlMessages.clear();
        WM(r.cbHeader, h->OnClick(1) == CONTROL_NOT_HANDLED && g_controlMessages.empty());
        // Its key click: its own id; not editing its click (not handled, nothing), editing the stop.
        WM(r.cbHeader, h->OnKeyClick(1) == 0 && g_controlMessages.empty());
        WM(r.cbHeader, h->OnKeyClick(0xA1100002) == CONTROL_NOT_HANDLED && g_controlMessages.empty());
        h->BeginEdit(0);
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.cbHeader, box.m_editing && h->OnLeave(1) == 0 && g_gfLog.empty());
        box.m_editing = false;
        box.m_hover = 1;
        box.m_selection = 4;
        WM(r.cbHeader, h->OnKeyClick(0xA1100002) == CONTROL_HANDLED && ScMessages() == "0 20000c0 0 " + EdPtr(h) + "|" &&
                           box.m_hover == -1 && box.m_selection == -1);
        h->OnInput0B(1);
        WM(r.cbHeader, h->OnInput02(1) == 0);
        h->m_inactive = true;
        WM(r.cbHeader, h->OnKeyClick(0xA1100002) == 0);
        h->m_inactive = false;
        // Its messages: EDIT asks for the editing; REFRESH and SET_MARGIN place the box.
        g_controlMessages.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_EDIT);
        handled &= h->HandleMessage(&m);
        WM(r.cbHeader, ScMessages() == "0 20000bf 0 " + EdPtr(h) + "|");
        box.m_placed = false;
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= h->HandleMessage(&m);
        WM(r.cbHeader, CsPlaced(box, 10));
        m = CsMessage(CONTROL_MSG_SET_MARGIN, 3);
        handled &= h->HandleMessage(&m);
        WM(r.cbHeader, CsPlaced(box, 3));
        m = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 7);
        handled &= h->HandleMessage(&m);
        WM(r.cbHeader, h->m_maxLength == 7);
        h->m_anyCallback = S_ControlCallback();
        delete h;
        frame->SetData(nullptr);
    }

    // --- the combo box: its list (its id, hidden: child 0), button (id | 1) and header (id | 2),
    // routed to it ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "cbframe");
        I3D_frame *listFrame = GfFrame(FRAME_DUMMY, "listboxcb", frame);
        CbScrolledFrames(listFrame, "cb");
        ScArrow("cbdowncb", "cn", frame);
        CsVisual("headercb", frame);
        auto *c = new C_ComboBox(scene, 0xA2100000, nullptr);
        WM(r.cbCombo, !c->m_open && c->m_picked == -1);
        WM(r.cbCombo, c->Create(frame, frame, nullptr) && c->m_children.size() == 3 && c->m_children[0] == c->m_list &&
                          c->m_children[1] == c->m_button && c->m_children[2] == c->m_header && !frame->GetData());
        WM(r.cbCombo, c->m_list->m_id == 0xA2100000 && c->m_button->m_id == 0xA2100001 && c->m_header->m_id == 0xA2100002 &&
                          !c->m_list->m_shown && c->m_header->m_takesInput && c->m_list->m_list && c->m_list->m_bar);
        WM(r.cbCombo, c->m_list->m_routes.size() == 1 && c->m_list->m_routes[0].target == 0xA2100000 &&
                          c->m_list->m_routes[0].id == 0x600602E && c->m_button->m_routes.size() == 1 &&
                          c->m_button->m_routes[0].id == 0x600602D && c->m_header->m_routes.size() == 1 &&
                          c->m_header->m_routes[0].id == 0x600602D && c->m_header->m_callbacks.size() == 1 &&
                          c->m_header->m_callbacks[0].notification == CONTROL_NOTIFY_0B && c->m_header->m_callbacks[0].object == c);
        WM(r.cbCombo, c->FindChild(4) == 0 && c->FindChild(0xF) == 0 && c->FindChild(1) == 1 && c->FindChild(2) == 2 &&
                          c->FindChild(0) == -1 && c->FindChild(3) == -1);
        C_TextBox &lines = *c->m_list->m_list->m_box;
        C_TextBox &header = *c->m_header->m_box;
        c->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        // The lines to the list; one picked by index into the header (the list closed).
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "red");
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "green");
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 3, 42);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, CsRuns(lines) == "red/3 green/3 text 42/3 ");
        c->m_open = true;
        c->m_list->m_shown = true;
        m = CsMessage(CONTROL_MSG_PICK_INDEX, 1);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == 1 && std::string(c->m_pickedText.c_str()) == "green" && CsRuns(header) == "green/12 " &&
                              !c->m_open && !c->m_list->m_shown && c->m_list->m_list->m_selected == 1);
        m = CsMessage(CONTROL_MSG_PICK_INDEX, 0);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == 0 && CsRuns(header) == "red/12 ");
        // Past the lines: none picked (the original's); negative: nothing.
        m = CsMessage(CONTROL_MSG_PICK_INDEX, 7);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == -1 && !c->m_pickedText.c_str()[0] && CsRuns(header) == "/12 ");
        m = CsMessage(CONTROL_MSG_PICK_INDEX, 2);
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_PICK_INDEX, 0xFFFFFFFF);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == 2 && CsRuns(header) == "text 42/12 ");
        // The list shown and hidden.
        m = CsMessage(CONTROL_MSG_TOGGLE_LIST);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_open && c->m_list->m_shown);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, !c->m_open && !c->m_list->m_shown);
        // The list's selected picked (notified); none: nothing.
        c->m_list->m_list->m_selected = 1;
        c->m_open = true;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_PICK_SELECTED);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == 1 && CsRuns(header) == "green/12 " && !c->m_open && !g_gfLog.empty() &&
                              g_gfLog.back() == "notify 400e000 0");
        c->m_list->m_list->m_selected = -1;
        g_gfLog.clear();
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == 1 && g_gfLog.empty());
        c->m_list->m_list->m_selected = 0;
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_picked == 0);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 9, 0, "old");
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, m.value == 0 && std::string(m.text.c_str()) == "red");
        // The header's text (the message turned into SET_TEXT / SET_TEXT_ID).
        m = CsMessage(CONTROL_MSG_SET_HEADER_TEXT, 5, 0, "hd");
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, CsRuns(header) == "hd/5 " && m.id == CONTROL_MSG_SET_TEXT);
        m = CsMessage(CONTROL_MSG_SET_HEADER_TEXT_ID, 4, 43);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, CsRuns(header) == "text 43/4 " && m.id == CONTROL_MSG_SET_TEXT_ID);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_EDIT);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, ScMessages() == "0 20000bf 0 " + EdPtr(c->m_header) + "|");
        // To a child by its target; the rest the base's.
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6, 0, nullptr, 0xA2100001);
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 5);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_button->m_index == 6 && c->m_index == 5);
        // The header's edit: its text the picked line's (notified); none picked: only notified.
        c->m_picked = 1;
        header.SetText(C_str("edited"), 2, 0);
        g_gfLog.clear();
        WM(r.cbComboMsgs, C_ComboBox::HeaderEdited(c, c, c->m_header, CONTROL_NOTIFY_0B, 0) &&
                              std::string(c->m_pickedText.c_str()) == "edited" && CsRuns(lines) == "red/3 edited/19 text 42/3 " &&
                              GfLogIs({"notify 400b000 0"}));
        c->m_picked = -1;
        header.SetText(C_str("other"), 2, 0);
        g_gfLog.clear();
        WM(r.cbComboMsgs, C_ComboBox::HeaderEdited(c, c, c->m_header, CONTROL_NOTIFY_0B, 0) &&
                              std::string(c->m_pickedText.c_str()) == "edited" && GfLogIs({"notify 400b000 0"}));
        // (Picked 0: put.)
        c->m_picked = 0;
        header.SetText(C_str("zero"), 2, 0);
        C_ComboBox::HeaderEdited(c, c, c->m_header, CONTROL_NOTIFY_0B, 0);
        WM(r.cbComboMsgs, EdRun(lines, 0) == "zero" && std::string(c->m_pickedText.c_str()) == "zero");
        header.SetText(C_str("other"), 2, 0);
        // (The header's notification calls it.)
        c->m_picked = 2;
        c->m_header->Notify(CONTROL_NOTIFY_0B, 0);
        WM(r.cbComboMsgs, EdRun(lines, 2) == "other");
        // The strings: the header the first's, the list the lines.
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, lines.NumRuns() == 0 && header.NumRuns() == 0);
        c->SetStrings({C_str("s1"), C_str("s2")});
        WM(r.cbComboMsgs, CsRuns(header) == "s1/12 " && CsRuns(lines) == "s1/19 s2/19 ");
        c->SetStrings({});
        WM(r.cbComboMsgs, CsRuns(header) == "s1/12 " && CsRuns(lines) == "s1/19 s2/19 ");

        // The leave: handled unless open; the focus' refused: 0.
        WM(r.cbComboInput, c->OnLeave(1) == CONTROL_HANDLED);
        c->m_open = true;
        WM(r.cbComboInput, c->OnLeave(1) == 0);
        c->m_open = false;
        // The move: off the children the leave; to the header its enter (the focus); on it its move.
        g_gfLog.clear();
        WM(r.cbComboInput, c->OnInput05(0xA2100003) == CONTROL_HANDLED && c->m_focus == -1);
        WM(r.cbComboInput, c->OnInput05(0xA2100002) == CONTROL_HANDLED && c->m_focus == 2);
        WM(r.cbComboInput, c->OnInput05(0xA2100002) == CONTROL_NOT_HANDLED && c->m_focus == 2);
        // (To another: the leave's result not asked.)
        c->m_open = true;
        WM(r.cbComboInput, c->OnInput05(0xA2100001) == CONTROL_HANDLED && c->m_focus == 1);
        c->m_open = false;
        // The press: the focus' with the id; else outside the box while open: closed.
        WM(r.cbComboInput, c->OnPress(0xA2100001) == CONTROL_HANDLED && c->m_button->m_held);
        c->m_button->m_held = false;
        c->m_open = true;
        c->m_list->m_shown = true;
        WM(r.cbComboInput, c->OnPress(0xA2100005) == 0 && c->m_open);
        WM(r.cbComboInput, c->OnPress(0xA3100000) == 0 && !c->m_open && !c->m_list->m_shown);
        WM(r.cbComboInput, c->OnPress(0xA3100000) == 0 && !c->m_open);
        // The enter (EnterChild): the child with the id.
        c->m_focus = -1;
        WM(r.cbComboInput, c->OnEnter(0xA2100002) == CONTROL_HANDLED && c->m_focus == 2);
        // The leave: the focus' (then none); refused: 0, the focus kept; the move to another leaves
        // the focus first.
        auto *k = new C_CsChild("ck", 0xA2100003);
        c->m_children.push_back(k);
        c->m_focus = 3;
        g_gfLog.clear();
        WM(r.cbComboInput, c->OnLeave(1) == CONTROL_HANDLED && c->m_focus == -1 && GfLogIs({"ck leave 1"}));
        c->m_focus = 3;
        g_csReturn = 0;
        WM(r.cbComboInput, c->OnLeave(1) == 0 && c->m_focus == 3);
        g_csReturn = 1;
        WM(r.cbComboInput, c->OnLeave(1) == CONTROL_HANDLED && c->m_focus == -1);
        g_csReturn = 5;
        c->m_focus = 3;
        g_gfLog.clear();
        WM(r.cbComboInput, c->OnInput05(0xA2100002) == CONTROL_HANDLED && c->m_focus == 2 && !g_gfLog.empty() &&
                               g_gfLog[0] == "ck leave a2100002");
        // (Refused: asked twice, the move's leave and the enter's.)
        c->m_focus = 3;
        g_csReturn = 0;
        g_gfLog.clear();
        WM(r.cbComboInput, c->OnInput05(0xA2100002) == CONTROL_NOT_HANDLED && c->m_focus == 3 &&
                               GfLogIs({"ck leave a2100002", "ck leave a2100002"}));
        g_csReturn = 5;
        c->m_children.pop_back();
        delete k;
        // A notification's route kept (the base's).
        const size_t routes = c->m_routes.size();
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_TOGGLE_LIST, 0, 0, nullptr, 0xA2100000);
        handled &= c->HandleMessage(&m);
        WM(r.cbComboMsgs, c->m_routes.size() == routes + 1);
        // Inactive: the parts too.
        c->SetInactive(true);
        WM(r.cbCombo, c->m_inactive && c->m_button->m_inactive && c->m_list->m_inactive && c->m_header->m_inactive);
        c->SetInactive(false);
        c->m_anyCallback = S_ControlCallback();
        delete c;
        // Without parts: made; the messages and the strings nothing.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "cbbare");
        C_ComboBox b(scene, 0xA2200000, nullptr);
        WM(r.cbCombo, b.Create(bare, bare, nullptr) && b.m_children.empty());
        for (uint32_t id : {CONTROL_MSG_ADD_TEXT, CONTROL_MSG_PICK_INDEX, CONTROL_MSG_SET_HEADER_TEXT, CONTROL_MSG_EDIT,
                            CONTROL_MSG_CLEAR, CONTROL_MSG_TOGGLE_LIST, CONTROL_MSG_PICK_SELECTED}) {
            m = CsMessage(id);
            handled &= b.HandleMessage(&m);
        }
        b.SetStrings({C_str("x")});
        b.SetInactive(true);
        WM(r.cbCombo, b.m_inactive && !b.m_open && b.m_picked == 0);
        for (I3D_frame *f : {frame, listFrame})
            for (I3D_frame *ch = f->m_firstChild; ch; ch = ch->m_nextSibling)
                ch->SetData(nullptr);
    }

    // --- the titled combo box: a title (taking input) and a combo box, both its id ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "tcframe");
        CsVisual("text18", frame);
        GfVisual("game difficulty", I3DVISUAL_OBJECT, frame);
        auto *t = new C_TitledComboBox(scene, 0xA4100000, nullptr);
        WM(r.cbTitled, t->Create(frame, frame, nullptr) && t->m_children.size() == 2 && t->m_children[0] == t->m_title &&
                           t->m_children[1] == t->m_combo && t->m_title->m_id == 0xA4100000 && t->m_combo->m_id == 0xA4100000 &&
                           t->m_title->m_takesInput && !frame->GetData() && t->m_combo->m_frame == Find(frame, "game difficulty", 0xFFFF));
        WM(r.cbTitled, t->FindChild(0xA4100000) == 0 && t->FindChild(0xA4100001) == 1 && t->FindChild(0xF) == 1);
        // The texts to the title; (de)activate, show, hide its own; the rest to the combo box.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "hi");
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, CbRuns(t->m_title) == "hi/2 ");
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 3, 44);
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, CbRuns(t->m_title) == "text 44/3 ");
        m = CsMessage(CONTROL_MSG_DEACTIVATE);
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, t->m_inactive && t->m_title->m_inactive && t->m_combo->m_inactive);
        t->m_title->m_inactive = false;
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, !t->m_title->m_inactive);
        m = CsMessage(CONTROL_MSG_ACTIVATE);
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, !t->m_inactive && !t->m_combo->m_inactive);
        t->m_combo->m_inactive = true;
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, t->m_combo->m_inactive);
        t->m_combo->m_inactive = false;
        m = CsMessage(CONTROL_MSG_HIDE);
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, !t->m_shown);
        m = CsMessage(CONTROL_MSG_SHOW);
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, t->m_shown);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 9, 0, "old");
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 3);
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, m.value == 3 && t->m_combo->m_index == 3 && t->m_index == -1);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 9, 0, "old");
        handled &= t->HandleMessage(&m);
        WM(r.cbTitled, m.value == 0xFFFFFFFF && !m.text.c_str()[0]);
        t->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        m = CsMessage(CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_CLICK);
        g_gfLog.clear();
        handled &= t->HandleMessage(&m);
        t->m_anyCallback = S_ControlCallback();
        delete t;
        // Without the combo box's frame: false; without the title: no children by id.
        I3D_frame *notitle = GfFrame(FRAME_DUMMY, "tcnotitle");
        GfVisual("game difficulty", I3DVISUAL_OBJECT, notitle);
        C_TitledComboBox n(scene, 0xA4200000, nullptr);
        WM(r.cbTitled, n.Create(notitle, notitle, nullptr) && !n.m_title && n.FindChild(1) == -1 && n.FindChild(0) == -1);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "x");
        handled &= n.HandleMessage(&m);
        I3D_frame *nocombo = GfFrame(FRAME_DUMMY, "tcnocombo");
        CsVisual("text18", nocombo);
        C_TitledComboBox o(scene, 0xA4300000, nullptr);
        WM(r.cbTitled, !o.Create(nocombo, nocombo, nullptr) && o.m_title && !o.m_combo && o.FindChild(0) == -1);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 3);
        handled &= o.HandleMessage(&m);
        o.SetInactive(true);
        WM(r.cbTitled, o.m_inactive && o.m_title->m_inactive);
    }

    // --- the chat input: its edit box (id | 4, the prompt, 100 characters), label (id | 8) and team
    // toggle (id | 0xc, routed to it) ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "chframe");
        I3D_frame *input = GfFrame(FRAME_DUMMY, "binputch", frame);
        CsVisual("normalch", input);
        GfVisual("activedch", I3DVISUAL_OBJECT, input);
        CsVisual("textch", frame);
        I3D_frame *team = GfFrame(FRAME_DUMMY, "bTeam", frame);
        for (const char *name : {"allactived", "allnormal", "xallactived", "xallnormal"})
            GfVisual(name, I3DVISUAL_OBJECT, team);
        auto *c = new C_ChatInput(scene, 0xA5100000, nullptr);
        WM(r.cbChat, !c->m_team && c->Create(frame, frame, nullptr) && c->m_children.size() == 3 &&
                         c->m_children[0] == c->m_input && c->m_children[1] == c->m_label && c->m_children[2] == c->m_teamToggle &&
                         !frame->GetData());
        WM(r.cbChat, c->m_input->m_id == 0xA5100004 && c->m_label->m_id == 0xA5100008 && c->m_teamToggle->m_id == 0xA510000C &&
                         CbRuns(c->m_input) == "text 2002/13 " && c->m_input->m_maxLength == 100 &&
                         c->m_input->m_callbacks.size() == 1 && c->m_input->m_callbacks[0].notification == CONTROL_NOTIFY_02 &&
                         c->m_input->m_callbacks[0].object == c && c->m_teamToggle->m_routes.size() == 1 &&
                         c->m_teamToggle->m_routes[0].target == 0xA5100000 && c->m_teamToggle->m_routes[0].id == 0x601000B);
        WM(r.cbChat, c->FindChild(4) == 0 && c->FindChild(8) == 1 && c->FindChild(0xD) == 2 && c->FindChild(0xF) == 2 &&
                         c->FindChild(0xC) == -1 && c->FindChild(0) == -1 && c->FindChild(5) == -1);
        // The lines to the label, the oldest removed past 8.
        S_ControlMessage m;
        for (int i = 1; i <= 9; ++i) {
            m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, CmText("l%d", i).c_str());
            handled &= c->HandleMessage(&m);
        }
        m = CsMessage(CONTROL_MSG_GET_RUNS);
        handled &= c->m_label->HandleMessage(&m);
        WM(r.cbChatMsgs, m.value == 8 && EdRun(*c->m_label->m_box, 0) == "l2" && EdRun(*c->m_label->m_box, 7) == "l9");
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 3, 45);
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_INSERT_FIRST, 3, 0, "f");
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_RUNS);
        handled &= c->m_label->HandleMessage(&m);
        WM(r.cbChatMsgs, m.value == 8 && EdRun(*c->m_label->m_box, 7) == "text 45");
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= c->HandleMessage(&m);
        WM(r.cbChatMsgs, c->m_label->m_box->NumRuns() == 0);
        // The team toggle: up, to the team.
        m = CsMessage(CONTROL_MSG_TEAM_TOGGLED);
        handled &= c->HandleMessage(&m);
        WM(r.cbChatMsgs, !c->m_team);
        c->m_teamToggle->m_down = false;
        handled &= c->HandleMessage(&m);
        WM(r.cbChatMsgs, c->m_team);
        g_controlMessages.clear();
        c->m_teamToggle->OnClick(1);
        WM(r.cbChatMsgs, c->m_teamToggle->m_down && ScMessages() == "a5100000 200000b 0 0|");
        handled &= c->HandleMessage(&m);
        WM(r.cbChatMsgs, !c->m_team);
        // A line entered: sent ("/a" or "/t" put first, a command's kept; the spaces before it
        // skipped), the prompt back, the editing asked for again.
        c->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CbSent};
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "  hello");
        handled &= c->m_input->HandleMessage(&m);
        g_cbSent.clear();
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.cbChat, C_ChatInput::LineEntered(c, c, c->m_input, CONTROL_NOTIFY_02, 0) && g_cbSent == "/ahello" &&
                         GfLogIs({"notify 4015000"}) && CbRuns(c->m_input) == "text 2002/13 " &&
                         ScMessages() == "a5100004 2000016 0 0|");
        c->m_team = true;
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "hi there");
        handled &= c->m_input->HandleMessage(&m);
        C_ChatInput::LineEntered(c, c, c->m_input, 0, 0);
        WM(r.cbChat, g_cbSent == "/thi there");
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "\t/a x");
        handled &= c->m_input->HandleMessage(&m);
        C_ChatInput::LineEntered(c, c, c->m_input, 0, 0);
        WM(r.cbChat, g_cbSent == "/a x");
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "/t y");
        handled &= c->m_input->HandleMessage(&m);
        C_ChatInput::LineEntered(c, c, c->m_input, 0, 0);
        WM(r.cbChat, g_cbSent == "/t y");
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "a/t");
        handled &= c->m_input->HandleMessage(&m);
        C_ChatInput::LineEntered(c, c, c->m_input, 0, 0);
        WM(r.cbChat, g_cbSent == "/ta/t");
        // Only spaces, or nothing: not sent; the prompt back all the same.
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "   ");
        handled &= c->m_input->HandleMessage(&m);
        g_cbSent = "none";
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.cbChat, C_ChatInput::LineEntered(c, c, c->m_input, 0, 0) && g_cbSent == "none" && g_gfLog.empty() &&
                         CbRuns(c->m_input) == "text 2002/13 " && g_controlMessages.size() == 1);
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= c->m_input->HandleMessage(&m);
        g_controlMessages.clear();
        WM(r.cbChat, C_ChatInput::LineEntered(c, c, c->m_input, 0, 0) && g_cbSent == "none" && g_controlMessages.size() == 1);
        // (A high byte stops the space skipping.)
        m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "\xA0 z");
        handled &= c->m_input->HandleMessage(&m);
        C_ChatInput::LineEntered(c, c, c->m_input, 0, 0);
        WM(r.cbChat, g_cbSent == "/t\xA0 z");
        // No control: nothing.
        g_controlMessages.clear();
        WM(r.cbChat, C_ChatInput::LineEntered(c, c, nullptr, 0, 0) && g_controlMessages.empty());
        // (Enter in the edit box calls it: its stop asked for; the run its text, the prompt kept.)
        c->m_input->BeginEdit(0);
        c->m_input->OnChar('q');
        g_cbSent.clear();
        c->m_input->OnChar(0xD);
        WM(r.cbChat, g_cbSent == "/ttext 2002q");
        c->m_input->OnInput0B(1);
        c->m_anyCallback = S_ControlCallback();
        // To a child by its target; the rest the group's.
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6, 0, nullptr, 0xA5100008);
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 5);
        handled &= c->HandleMessage(&m);
        WM(r.cbChatMsgs, c->m_label->m_index == 6 && c->m_index == 5);
        // The focus' Unknown20.
        c->m_focus = 1;
        WM(r.cbChatMsgs, !c->Unknown20(2));
        c->m_focus = -1;
        delete c;
        // Without parts: made; the lines and the toggle nothing.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "chbare");
        C_ChatInput b(scene, 0xA5200000, nullptr);
        WM(r.cbChat, b.Create(bare, bare, nullptr) && b.m_children.empty());
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "x");
        handled &= b.HandleMessage(&m);
        b.m_team = true;
        m = CsMessage(CONTROL_MSG_TEAM_TOGGLED);
        handled &= b.HandleMessage(&m);
        WM(r.cbChat, b.m_team);
        for (I3D_frame *f : {frame, input, team})
            for (I3D_frame *ch = f->m_firstChild; ch; ch = ch->m_nextSibling)
                ch->SetData(nullptr);
    }

    WM(r.cbScrolledMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("combo: cbScrolled %d cbScrolledInput %d cbScrolledMsgs %d cbHeader %d cbCombo %d cbComboInput %d "
                "cbComboMsgs %d cbTitled %d cbChat %d cbChatMsgs %d\n",
                r.cbScrolled, r.cbScrolledInput, r.cbScrolledMsgs, r.cbHeader, r.cbCombo, r.cbComboInput, r.cbComboMsgs,
                r.cbTitled, r.cbChat, r.cbChatMsgs);
}
