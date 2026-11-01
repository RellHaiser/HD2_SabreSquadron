// Part 9c-iii-b's controls (game/controls.cpp: the check box, the toggle buttons, the highlighted
// check box and its halves, the side indicator, the mark), in the text build, run by ControlsTest
// after ScrollTest (its fonts, its screen, its cursor; ScMessages). Prints "check:" with 1 per check
// met.

namespace {

struct S_CheckResults {
    bool ckBox = true, ckBoxInput = true, ckBoxMsgs = true, ckToggle = true, ckToggleInput = true, ckHigh = true;
    bool ckHighInput = true, ckHighMsgs = true, ckHalf = true, ckSide = true, ckMark = true, ckMarkMsgs = true;
};

// The log's last lines are `expected`.
bool CkLogEnds(std::initializer_list<std::string> expected)
{
    const std::vector<std::string> tail(expected);
    const bool same = g_gfLog.size() >= tail.size() &&
                      std::equal(tail.begin(), tail.end(), g_gfLog.end() - static_cast<std::ptrdiff_t>(tail.size()));
    if (!same && g_wmTrace) {
        for (const std::string &l : g_gfLog)
            std::printf("    gflog %s\n", l.c_str());
    }
    return same;
}
S_FrameData *CkId(uint32_t id)
{
    return reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(id));
}
// A title logging its refresh and its deletion.
struct C_CkTitle : C_TitleControl {
    C_CkTitle() : C_TitleControl(nullptr, 0, nullptr) {}
    ~C_CkTitle() override { GfLog("title deleted"); }
    void Refresh() override { GfLog("title refresh"); }
};

} // namespace

void CheckTest(I3D_scene *scene)
{
    S_CheckResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the check box: its four frames (the fulls at the empties' places, the no frames' ids
    // with 1, the yes frames' 2), checked; the box placed ---
    {
        I3D_frame *frame = CsVisual("cbframe");
        I3D_frame *yesFull = GfVisual("yes_fullcb", I3DVISUAL_OBJECT, frame);
        I3D_frame *yesEmpty = GfVisual("yes_emptycb", I3DVISUAL_OBJECT, frame);
        I3D_frame *noFull = GfVisual("no_fullcb", I3DVISUAL_OBJECT, frame);
        I3D_frame *noEmpty = GfVisual("no_emptycb", I3DVISUAL_OBJECT, frame);
        GfPlace(noEmpty, S_vector(0.1f, 0.2f, 0.3f));
        GfPlace(yesEmpty, S_vector(0.4f, 0.5f, 0.6f));
        auto *c = new C_CheckBox(scene, 0x30100000, nullptr);
        WM(r.ckBox, c->m_checked && !c->m_yesFull);
        g_gfLog.clear();
        WM(r.ckBox, c->Create(frame, frame, nullptr) && c->m_box && CsPlaced(*c->m_box, 10) && c->m_yesFull == yesFull &&
                        c->m_yesEmpty == yesEmpty && c->m_noFull == noFull && c->m_noEmpty == noEmpty && c->m_checked);
        WM(r.ckBox, GfLogIs({"find cbframe yes_full* 10001", "find cbframe yes_empty* 10001", "find cbframe no_full* 10001",
                             "find cbframe no_empty* 10001", "on no_emptycb 1", "update no_emptycb", "on no_fullcb 0",
                             "update no_fullcb", "on yes_emptycb 0", "update yes_emptycb", "on yes_fullcb 1", "update yes_fullcb"}));
        WM(r.ckBox, noFull->GetPos() == S_vector(0.1f, 0.2f, 0.3f) && yesFull->GetPos() == S_vector(0.4f, 0.5f, 0.6f) &&
                        noEmpty->GetData() == CkId(0x30100001) && noFull->GetData() == CkId(0x30100001) &&
                        yesEmpty->GetData() == CkId(0x30100002) && yesFull->GetData() == CkId(0x30100002));

        // The enter: the box's hover, notified (inactive: not handled); the leave notifies the enter
        // too (the original's).
        C_TextBox &box = *c->m_box;
        if (box.m_lines.empty())
            box.m_lines.push_back(new S_TextLine());
        box.m_hover = -1;
        c->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnEnter(1) == CONTROL_HANDLED && box.m_hover == 0 && GfLogIs({"notify 4003000 0"}));
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnLeave(1) == CONTROL_HANDLED && box.m_hover == -1 && GfLogIs({"notify 4003000 0"}));
        // The click toggles, notified with the state.
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnClick(1) == CONTROL_HANDLED && !c->m_checked &&
                             GfLogIs({"on no_emptycb 0", "on no_fullcb 1", "on yes_emptycb 1", "on yes_fullcb 0", "update no_emptycb",
                                      "update no_fullcb", "update yes_emptycb", "update yes_fullcb", "notify 4010000 0"}));
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnClick(1) == CONTROL_HANDLED && c->m_checked &&
                             GfLogIs({"on no_emptycb 1", "on no_fullcb 0", "on yes_emptycb 0", "on yes_fullcb 1", "update no_emptycb",
                                      "update no_fullcb", "update yes_emptycb", "update yes_fullcb", "notify 4010000 1"}));
        // The key click by the frame's id: a no frame unchecks, a yes frame checks; else nothing.
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnKeyClick(0x30100002) == CONTROL_HANDLED && c->m_checked && g_gfLog.empty());
        WM(r.ckBoxInput, c->OnKeyClick(0x30100001) == CONTROL_HANDLED && !c->m_checked && g_gfLog.size() == 9 &&
                             g_gfLog.back() == "notify 4010000 0");
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnKeyClick(0x30100001) == CONTROL_HANDLED && c->OnKeyClick(0x30100003) == CONTROL_HANDLED &&
                             c->OnKeyClick(0x30100000) == CONTROL_HANDLED && !c->m_checked && g_gfLog.empty());
        WM(r.ckBoxInput, c->OnKeyClick(0x30100002) == CONTROL_HANDLED && c->m_checked && g_gfLog.back() == "notify 4010000 1");
        // Inactive: the enter not handled (nothing); the click still toggles.
        c->m_inactive = true;
        g_gfLog.clear();
        WM(r.ckBoxInput, c->OnEnter(1) == 0 && box.m_hover == -1 && g_gfLog.empty());
        WM(r.ckBoxInput, c->OnClick(1) == CONTROL_HANDLED && !c->m_checked);
        c->OnClick(1);
        c->m_inactive = false;

        // The messages: the state got; set (changed: notified); set quietly; the value's low byte
        // (2: toggled, then set back; the original's).
        S_ControlMessage m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, m.value == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 0);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, !c->m_checked && g_gfLog.size() == 9 && g_gfLog.back() == "notify 4010000 0");
        g_gfLog.clear();
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, !c->m_checked && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, m.value == 0);
        m = CsMessage(CONTROL_MSG_SET_CHECKED_QUIET, 1);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, c->m_checked && g_gfLog.size() == 8 && g_gfLog.back() == "update yes_fullcb");
        g_gfLog.clear();
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, c->m_checked && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 2);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, c->m_checked && g_gfLog.size() == 17 && g_gfLog[8] == "notify 4010000 0" &&
                            g_gfLog.back() == "update yes_fullcb");
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 0x100);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, !c->m_checked);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED_QUIET, 0x101);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, c->m_checked && g_gfLog.size() == 8);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED_QUIET, 0);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, !c->m_checked && g_gfLog.size() == 8 && g_gfLog.front() == "on no_emptycb 0");
        // Others: the text control's.
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "t");
        handled &= c->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6);
        handled &= c->HandleMessage(&m);
        WM(r.ckBoxMsgs, CsRuns(box) == "t/3 " && c->m_index == 6);
        c->m_anyCallback = S_ControlCallback();
        delete c;

        // Not 4 children, or a frame not found: false.
        I3D_frame *three = CsVisual("cbthree");
        GfVisual("yes_fullc3", I3DVISUAL_OBJECT, three);
        GfVisual("yes_emptyc3", I3DVISUAL_OBJECT, three);
        GfVisual("no_fullc3", I3DVISUAL_OBJECT, three);
        C_CheckBox t(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.ckBox, !t.Create(three, three, nullptr) && g_gfLog.empty() && !t.m_yesFull);
        GfVisual("no_fullc4", I3DVISUAL_OBJECT, three);
        C_CheckBox f(scene, 0, nullptr);
        WM(r.ckBox, !f.Create(three, three, nullptr) && f.m_yesFull && !f.m_noEmpty && g_gfLog.size() == 4);
        // (The first frame missing.)
        I3D_frame *first = CsVisual("cbfirst");
        for (const char *name : {"xyes_fullcf", "yes_emptycf", "no_fullcf", "no_emptycf"})
            GfVisual(name, I3DVISUAL_OBJECT, first);
        C_CheckBox g(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.ckBox, !g.Create(first, first, nullptr) && !g.m_yesFull && g.m_noEmpty && g_gfLog.size() == 4);
    }

    // --- the toggle button: its four frames at the normal down one's place (the down ones' ids
    // with 1, the up ones' 2), down ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "tbframe");
        I3D_frame *activeDown = GfVisual("activeddowntb", I3DVISUAL_OBJECT, frame);
        I3D_frame *normalDown = GfVisual("normaldowntb", I3DVISUAL_OBJECT, frame);
        I3D_frame *activeUp = GfVisual("activeduptb", I3DVISUAL_OBJECT, frame);
        I3D_frame *normalUp = GfVisual("normaluptb", I3DVISUAL_OBJECT, frame);
        GfPlace(normalDown, S_vector(0.1f, 0.2f, 0.3f));
        auto *t = new C_ToggleButton(scene, 0x40100000, nullptr);
        WM(r.ckToggle, t->m_down && !t->m_over && t->m_unknown7C == -1);
        g_gfLog.clear();
        WM(r.ckToggle, t->Create(frame, frame, nullptr) && t->m_activeDown == activeDown && t->m_normalDown == normalDown &&
                           t->m_activeUp == activeUp && t->m_normalUp == normalUp && t->m_down && t->m_frame == frame &&
                           frame->GetData() == CkId(0x40100000));
        WM(r.ckToggle, GfLogIs({"find tbframe activeddown* 10001", "find tbframe normaldown* 10001", "find tbframe activedup* 10001",
                                "find tbframe normalup* 10001", "on normaldowntb 1", "update normaldowntb", "on activeddowntb 0",
                                "update activeddowntb", "on activeduptb 0", "update activeduptb", "on normaluptb 0",
                                "update normaluptb"}));
        const S_vector place(0.1f, 0.2f, 0.3f);
        WM(r.ckToggle, activeDown->GetPos() == place && activeUp->GetPos() == place && normalUp->GetPos() == place &&
                           normalDown->GetData() == CkId(0x40100001) && activeDown->GetData() == CkId(0x40100001) &&
                           activeUp->GetData() == CkId(0x40100002) && normalUp->GetData() == CkId(0x40100002));

        // Over it: the active frame of its state; the click toggles the frames shown (the active ones
        // while over it), notified (0).
        t->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnEnter(1) == CONTROL_HANDLED && t->m_over &&
                                GfLogIs({"on activeddowntb 1", "on normaldowntb 0", "update activeddowntb", "update normaldowntb"}));
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnClick(1) == CONTROL_HANDLED && !t->m_down &&
                                GfLogIs({"on activeddowntb 0", "update activeddowntb", "on activeduptb 1", "update activeduptb",
                                         "notify 4010000 0"}));
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnLeave(1) == CONTROL_HANDLED && !t->m_over &&
                                GfLogIs({"on normaluptb 1", "on activeduptb 0", "update activeduptb", "update normaluptb"}));
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnClick(1) == CONTROL_HANDLED && t->m_down &&
                                GfLogIs({"on normaldowntb 1", "update normaldowntb", "on normaluptb 0", "update normaluptb",
                                         "notify 4010000 0"}));
        g_gfLog.clear();
        t->OnClick(1);
        WM(r.ckToggleInput, !t->m_down && GfLogIs({"on normaldowntb 0", "update normaldowntb", "on normaluptb 1", "update normaluptb",
                                                     "notify 4010000 0"}));
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnEnter(1) == CONTROL_HANDLED &&
                                GfLogIs({"on activeduptb 1", "on normaluptb 0", "update activeduptb", "update normaluptb"}));
        g_gfLog.clear();
        t->OnClick(1);
        WM(r.ckToggleInput, t->m_down && GfLogIs({"on activeddowntb 1", "update activeddowntb", "on activeduptb 0",
                                                    "update activeduptb", "notify 4010000 0"}));
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnLeave(1) == CONTROL_HANDLED &&
                                GfLogIs({"on normaldowntb 1", "on activeddowntb 0", "update activeddowntb", "update normaldowntb"}));
        // The press handled, nothing; inactive: the enter all the same.
        g_gfLog.clear();
        WM(r.ckToggleInput, t->OnPress(1) == CONTROL_HANDLED && g_gfLog.empty());
        t->m_inactive = true;
        WM(r.ckToggleInput, t->OnEnter(1) == CONTROL_HANDLED && t->m_over && g_gfLog.size() == 4);
        t->OnLeave(1);
        t->m_inactive = false;

        // The messages: the state (down); set (any value but 0 down; changed: notified).
        S_ControlMessage m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= t->HandleMessage(&m);
        WM(r.ckToggleInput, m.value == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 0);
        handled &= t->HandleMessage(&m);
        WM(r.ckToggleInput, !t->m_down && g_gfLog.size() == 5 && g_gfLog.back() == "notify 4010000 0");
        g_gfLog.clear();
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= t->HandleMessage(&m);
        WM(r.ckToggleInput, !t->m_down && m.value == 0 && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 0x100);
        handled &= t->HandleMessage(&m);
        WM(r.ckToggleInput, t->m_down && g_gfLog.size() == 5);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 7);
        handled &= t->HandleMessage(&m);
        // (Not the check boxes' quiet set: the base's, nothing.)
        m = CsMessage(CONTROL_MSG_SET_CHECKED_QUIET, 0);
        handled &= t->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 3);
        handled &= t->HandleMessage(&m);
        WM(r.ckToggleInput, t->m_down && g_gfLog.empty() && t->m_index == 3);
        t->m_anyCallback = S_ControlCallback();
        delete t;

        // Not 4 children, a frame not found: false.
        I3D_frame *five = GfFrame(FRAME_DUMMY, "tbfive");
        for (const char *name : {"activeddownt5", "normaldownt5", "activedupt5", "normalupt5", "othert5"})
            GfVisual(name, I3DVISUAL_OBJECT, five);
        C_ToggleButton a(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.ckToggle, !a.Create(five, five, nullptr) && g_gfLog.empty() && !a.m_activeDown);
        I3D_frame *wrong = GfFrame(FRAME_DUMMY, "tbwrong");
        for (const char *name : {"activeddowntw", "normaldowntw", "activeduptw", "xnormaluptw"})
            GfVisual(name, I3DVISUAL_OBJECT, wrong);
        C_ToggleButton b(scene, 0, nullptr);
        WM(r.ckToggle, !b.Create(wrong, wrong, nullptr) && b.m_activeUp && !b.m_normalUp && g_gfLog.size() == 4);
        I3D_frame *first = GfFrame(FRAME_DUMMY, "tbfirst");
        for (const char *name : {"xactiveddowntf", "normaldowntf", "activeduptf", "normaluptf"})
            GfVisual(name, I3DVISUAL_OBJECT, first);
        C_ToggleButton e(scene, 0, nullptr);
        WM(r.ckToggle, !e.Create(first, first, nullptr) && !e.m_activeDown && e.m_normalUp);

        // The "all" one: its own names (not masks).
        I3D_frame *all = GfFrame(FRAME_DUMMY, "atframe");
        for (const char *name : {"allactived", "allnormal", "xallactived", "xallnormal"})
            GfVisual(name, I3DVISUAL_OBJECT, all);
        C_AllToggleButton at(scene, 0x40200000, nullptr);
        g_gfLog.clear();
        WM(r.ckToggle, at.Create(all, all, nullptr) && at.m_down && GfName(at.m_activeDown) == std::string("allactived") &&
                           GfName(at.m_normalDown) == std::string("allnormal") && GfName(at.m_activeUp) == std::string("xallactived") &&
                           GfName(at.m_normalUp) == std::string("xallnormal") && at.m_normalUp->GetData() == CkId(0x40200002));
        WM(r.ckToggle, g_gfLog.size() == 12 && g_gfLog[0] == "find atframe allactived 10001" &&
                           g_gfLog[3] == "find atframe xallnormal 10001" && g_gfLog[4] == "on allnormal 1");
        for (I3D_frame *f : {frame, all})
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
                c->SetData(nullptr);
    }

    // --- the highlighted check box: the box placed; its halves (children, ids with 1 and 2) on
    // their frames (the active empty one shared), the yes half full ---
    {
        I3D_frame *frame = CsVisual("hcframe");
        I3D_frame *yesEmpty = GfVisual("yes_emptyhc", I3DVISUAL_OBJECT, frame);
        I3D_frame *yesFull = GfVisual("yes_fullhc", I3DVISUAL_OBJECT, frame);
        I3D_frame *yesActiveFull = GfVisual("yesactived_fullhc", I3DVISUAL_OBJECT, frame);
        I3D_frame *noEmpty = GfVisual("no_emptyhc", I3DVISUAL_OBJECT, frame);
        I3D_frame *noFull = GfVisual("no_fullhc", I3DVISUAL_OBJECT, frame);
        I3D_frame *noActiveFull = GfVisual("noactived_fullhc", I3DVISUAL_OBJECT, frame);
        I3D_frame *activeEmpty = GfVisual("noactived_emptyhc", I3DVISUAL_OBJECT, frame);
        GfPlace(yesEmpty, S_vector(0.1f, 0.2f, 0.3f));
        GfPlace(noEmpty, S_vector(0.4f, 0.5f, 0.6f));
        auto *h = new C_HighlightCheckBox(scene, 0x50100000, nullptr);
        WM(r.ckHigh, h->m_checked && h->m_overId == 0 && !h->m_yes);
        g_gfLog.clear();
        WM(r.ckHigh, h->Create(frame, frame, nullptr) && h->m_box && CsPlaced(*h->m_box, 10) && h->m_children.size() == 2 &&
                         h->m_children[0] == h->m_yes && h->m_children[1] == h->m_no && h->m_checked);
        WM(r.ckHigh, GfLogIs({"find hcframe yes_empty* 10001", "find hcframe yes_full* 10001", "find hcframe yesactived_full* 10001",
                              "find hcframe no_empty* 10001", "find hcframe no_full* 10001", "find hcframe noactived_full* 10001",
                              "find hcframe noactived_empty* 10001", "on yes_emptyhc 1", "update yes_emptyhc", "on yes_fullhc 0",
                              "update yes_fullhc", "on yesactived_fullhc 0", "update yesactived_fullhc", "on no_emptyhc 1",
                              "update no_emptyhc", "on no_fullhc 0", "update no_fullhc", "on noactived_fullhc 0",
                              "update noactived_fullhc", "on yes_emptyhc 0", "on yes_fullhc 1", "update yes_emptyhc",
                              "update yes_fullhc"}));
        C_CheckBoxHalf *yes = h->m_yes, *no = h->m_no;
        WM(r.ckHigh, yes->m_id == 0x50100001 && no->m_id == 0x50100002 && yes->m_parent == h && no->m_parent == h &&
                         yes->m_frame == yesEmpty && no->m_frame == noEmpty && yes->m_emptyFrame == yesEmpty &&
                         yes->m_fullFrame == yesFull && yes->m_activeFullFrame == yesActiveFull && yes->m_activeEmptyFrame == activeEmpty &&
                         no->m_activeFullFrame == noActiveFull && no->m_activeEmptyFrame == activeEmpty && !yes->m_empty && no->m_empty &&
                         !yes->m_over);
        WM(r.ckHigh, yesFull->GetPos() == S_vector(0.1f, 0.2f, 0.3f) && yesActiveFull->GetPos() == S_vector(0.1f, 0.2f, 0.3f) &&
                         noFull->GetPos() == S_vector(0.4f, 0.5f, 0.6f) && yesEmpty->GetData() == CkId(0x50100001) &&
                         yesActiveFull->GetData() == CkId(0x50100001) && noFull->GetData() == CkId(0x50100002) &&
                         activeEmpty->GetData() == nullptr && activeEmpty->GetPos() == S_vector(0.0f, 0.0f, 0.0f));

        // The enter: the box's hover, the half's (full: its active full frame), notified.
        C_TextBox &box = *h->m_box;
        if (box.m_lines.empty())
            box.m_lines.push_back(new S_TextLine());
        box.m_hover = -1;
        h->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnEnter(0x50100001) == CONTROL_HANDLED && box.m_hover == 0 && h->m_overId == 0x50100001 &&
                              yes->m_over && GfLogIs({"on yes_fullhc 0", "update yes_fullhc", "on yesactived_fullhc 1",
                                                      "update yesactived_fullhc", "notify 4003000 0"}));
        // To the other half: its leave, the other's enter (empty: the shared active empty frame at
        // its place, its id).
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnInput05(0x50100002) == CONTROL_HANDLED && h->m_overId == 0x50100002 && !yes->m_over && no->m_over &&
                              GfLogIs({"on yes_fullhc 1", "update yes_fullhc", "on yesactived_fullhc 0", "update yesactived_fullhc",
                                       "on no_emptyhc 0", "update no_emptyhc", "on noactived_emptyhc 1", "update noactived_emptyhc"}));
        WM(r.ckHighInput, activeEmpty->GetPos() == S_vector(0.4f, 0.5f, 0.6f) && activeEmpty->GetData() == CkId(0x50100002));
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnInput05(0x50100002) == CONTROL_HANDLED && g_gfLog.empty());
        // The click toggles (the halves' frames, the active ones under the cursor), notified.
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnClick(1) == CONTROL_HANDLED && !h->m_checked && yes->m_empty && !no->m_empty &&
                              GfLogIs({"on yes_emptyhc 1", "on yes_fullhc 0", "update yes_emptyhc", "update yes_fullhc",
                                       "on noactived_fullhc 1", "on noactived_emptyhc 0", "update noactived_fullhc",
                                       "update noactived_emptyhc", "notify 4010000 0"}));
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnKeyClick(1) == CONTROL_HANDLED && h->m_checked &&
                              GfLogIs({"on yes_emptyhc 0", "on yes_fullhc 1", "update yes_emptyhc", "update yes_fullhc",
                                       "on noactived_emptyhc 1", "on noactived_fullhc 0", "update noactived_fullhc",
                                       "update noactived_emptyhc", "notify 4010000 1"}));
        // The leave: the half's, notified.
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnLeave(5) == CONTROL_HANDLED && h->m_overId == 0 && box.m_hover == -1 && !no->m_over &&
                              GfLogIs({"on no_emptyhc 1", "update no_emptyhc", "on noactived_emptyhc 0", "update noactived_emptyhc",
                                       "notify 4004000 0"}));
        // Moving before an enter: nothing; over neither half: no half's enter.
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnInput05(0x50100001) == CONTROL_HANDLED && g_gfLog.empty() && h->m_overId == 0);
        WM(r.ckHighInput, h->OnEnter(0x50100003) == CONTROL_HANDLED && h->m_overId == 0x50100003 && GfLogIs({"notify 4003000 0"}));
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnInput05(0x50100001) == CONTROL_HANDLED && h->m_overId == 0x50100001 && yes->m_over &&
                              GfLogIs({"on yes_fullhc 0", "update yes_fullhc", "on yesactived_fullhc 1", "update yesactived_fullhc"}));
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnInput05(0x50100000) == CONTROL_HANDLED && h->m_overId == 0x50100000 && !yes->m_over &&
                              g_gfLog.size() == 4);
        WM(r.ckHighInput, h->OnLeave(5) == CONTROL_HANDLED && g_gfLog.back() == "notify 4004000 0");
        // Inactive: the enter not handled, nothing.
        h->m_inactive = true;
        g_gfLog.clear();
        WM(r.ckHighInput, h->OnEnter(0x50100001) == 0 && g_gfLog.empty() && h->m_overId == 0 && box.m_hover == -1);
        h->m_inactive = false;

        // A half alone: entered once; its key click shows the other state's active frame (the state
        // kept); the leave (not over: not handled); inactive: not entered.
        g_gfLog.clear();
        WM(r.ckHalf, yes->OnEnter(1) == CONTROL_HANDLED && yes->OnEnter(1) == 0 && g_gfLog.size() == 4);
        g_gfLog.clear();
        WM(r.ckHalf, yes->OnKeyClick(1) == CONTROL_HANDLED && !yes->m_empty &&
                         GfLogIs({"on yesactived_fullhc 0", "update yesactived_fullhc", "on noactived_emptyhc 1",
                                  "update noactived_emptyhc"}));
        WM(r.ckHalf, activeEmpty->GetPos() == S_vector(0.1f, 0.2f, 0.3f) && activeEmpty->GetData() == CkId(0x50100001));
        g_gfLog.clear();
        WM(r.ckHalf, yes->OnLeave(1) == CONTROL_HANDLED && !yes->m_over &&
                         GfLogIs({"on yes_fullhc 1", "update yes_fullhc", "on yesactived_fullhc 0", "update yesactived_fullhc"}));
        g_gfLog.clear();
        WM(r.ckHalf, yes->OnLeave(1) == 0 && yes->OnKeyClick(1) == 0 && g_gfLog.empty());
        // Empty and over: the key click the active full frame.
        WM(r.ckHalf, no->OnEnter(1) == CONTROL_HANDLED && no->m_over);
        g_gfLog.clear();
        WM(r.ckHalf, no->OnKeyClick(1) == CONTROL_HANDLED && no->m_empty &&
                         GfLogIs({"on noactived_emptyhc 0", "update noactived_emptyhc", "on noactived_fullhc 1",
                                  "update noactived_fullhc"}));
        no->OnLeave(1);
        // Full or empty while not over: the normal frames; the same: nothing.
        g_gfLog.clear();
        no->SetFull(true);
        WM(r.ckHalf, !no->m_empty && GfLogIs({"on no_emptyhc 0", "on no_fullhc 1", "update no_emptyhc", "update no_fullhc"}));
        g_gfLog.clear();
        no->SetFull(true);
        WM(r.ckHalf, g_gfLog.empty());
        no->SetFull(false);
        WM(r.ckHalf, no->m_empty && g_gfLog.size() == 4 && g_gfLog[0] == "on no_emptyhc 1");
        // Inactive: the normal frames faded (once), not entered.
        g_gfLog.clear();
        no->SetInactive(true);
        WM(r.ckHalf, no->m_inactive && GfLogIs({"alpha no_emptyhc 0.400", "alpha no_fullhc 0.400"}));
        g_gfLog.clear();
        no->SetInactive(true);
        WM(r.ckHalf, g_gfLog.empty() && no->OnEnter(1) == 0 && !no->m_over);
        no->SetInactive(false);
        WM(r.ckHalf, !no->m_inactive && GfLogIs({"alpha no_emptyhc 1.000", "alpha no_fullhc 1.000"}));
        // Linked: the four frames.
        g_gfLog.clear();
        yes->LinkFrames(nullptr);
        WM(r.ckHalf, GfLogIs({"link yes_emptyhc 0 1", "link yes_fullhc 0 1", "link yesactived_fullhc 0 1",
                              "link noactived_emptyhc 0 1"}) && !yesEmpty->m_parent);
        yes->LinkFrames(frame);
        WM(r.ckHalf, yesEmpty->m_parent == frame && frame->NumChildren() == 7);

        // Inactive: the box dimmed, the halves inactive (once).
        g_gfLog.clear();
        h->SetInactive(true);
        WM(r.ckHighInput, h->m_inactive && box.m_dimmed && yes->m_inactive && no->m_inactive &&
                              GfLogIs({"alpha yes_emptyhc 0.400", "alpha yes_fullhc 0.400", "alpha no_emptyhc 0.400",
                                       "alpha no_fullhc 0.400"}));
        g_gfLog.clear();
        h->SetInactive(true);
        WM(r.ckHighInput, g_gfLog.empty());
        h->SetInactive(false);
        WM(r.ckHighInput, !h->m_inactive && !box.m_dimmed && !yes->m_inactive && g_gfLog.size() == 4);

        // The messages: the state got, set (notified), set quietly; the low byte (C_CheckBox's).
        S_ControlMessage m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, m.value == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 0);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, !h->m_checked && yes->m_empty && !no->m_empty && g_gfLog.size() == 9 && g_gfLog.back() == "notify 4010000 0");
        g_gfLog.clear();
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_CHECKED_QUIET, 1);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, h->m_checked && !yes->m_empty && no->m_empty && g_gfLog.size() == 8 && g_gfLog.back() == "update no_fullhc");
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 2);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, h->m_checked && g_gfLog.size() == 17 && g_gfLog[8] == "notify 4010000 0");
        m = CsMessage(CONTROL_MSG_SET_CHECKED_QUIET, 0x100);
        handled &= h->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, !h->m_checked && m.value == 0);
        // FACE_FRONT: the halves' frames unlinked, the frame turned to the back, linked again (the box
        // not placed again).
        box.m_placed = false;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_FACE_FRONT);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, GfLogIs({"link yes_emptyhc 0 1", "link yes_fullhc 0 1", "link yesactived_fullhc 0 1",
                                  "link noactived_emptyhc 0 1", "link no_emptyhc 0 1", "link no_fullhc 0 1",
                                  "link noactived_fullhc 0 1", "link noactived_emptyhc 0 1",
                                  "setdir hcframe 0.00000 0.00000 -1.00000 0.000000", "link yes_emptyhc hcframe 1",
                                  "link yes_fullhc hcframe 1", "link yesactived_fullhc hcframe 1", "link noactived_emptyhc hcframe 1",
                                  "link no_emptyhc hcframe 1", "link no_fullhc hcframe 1", "link noactived_fullhc hcframe 1",
                                  "link noactived_emptyhc hcframe 1"}) &&
                             !box.m_placed && frame->NumChildren() == 7);
        // Others: the text control's.
        m = CsMessage(CONTROL_MSG_SET_TEXT, 4, 0, "h");
        handled &= h->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= h->HandleMessage(&m);
        WM(r.ckHighMsgs, CsRuns(box) == "h/4 " && box.m_placed);
        h->m_anyCallback = S_ControlCallback();
        delete h;

        // Not 7 children, a frame not found: false (no halves).
        I3D_frame *six = CsVisual("hcsix");
        for (const char *name : {"yes_emptyh6", "yes_fullh6", "yesactived_fullh6", "no_emptyh6", "no_fullh6", "noactived_fullh6"})
            GfVisual(name, I3DVISUAL_OBJECT, six);
        C_HighlightCheckBox s(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.ckHigh, !s.Create(six, six, nullptr) && g_gfLog.empty() && s.m_children.empty() && !s.m_box->m_placed);
        GfVisual("xnoactived_emptyh6", I3DVISUAL_OBJECT, six);
        C_HighlightCheckBox w(scene, 0, nullptr);
        WM(r.ckHigh, !w.Create(six, six, nullptr) && g_gfLog.size() == 7 && w.m_children.empty() && !w.m_yes && w.m_box->m_placed);
        I3D_frame *first = CsVisual("hcfirst");
        for (const char *name : {"xyes_emptyhf", "yes_fullhf", "yesactived_fullhf", "no_emptyhf", "no_fullhf", "noactived_fullhf",
                                 "noactived_emptyhf"})
            GfVisual(name, I3DVISUAL_OBJECT, first);
        C_HighlightCheckBox e(scene, 0, nullptr);
        WM(r.ckHigh, !e.Create(first, first, nullptr) && e.m_children.empty() && !e.m_yes);
        for (I3D_frame *c = frame->m_firstChild; c; c = c->m_nextSibling)
            c->SetData(nullptr);
    }

    // --- the side indicator: its frames off (with its id), no side, posted to be ticked; every 2 s
    // the zone's team (its index) shown ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "siframe");
        I3D_frame *allied = GfVisual("alliedsi", I3DVISUAL_OBJECT, frame);
        I3D_frame *axis = GfVisual("axissi", I3DVISUAL_OBJECT, frame);
        GfVisual("othersi", I3DVISUAL_OBJECT, frame);
        auto *s = new C_SideIndicator(scene, 0x60100000, nullptr);
        WM(r.ckSide, s->m_side == 0 && s->m_timer == 0 && s->m_unknown6C == 0);
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.ckSide, s->Create(frame, frame, nullptr) && s->m_alliedFrame == allied && s->m_axisFrame == axis && s->m_side == 2 &&
                         GfLogIs({"find siframe allied* 10001", "find siframe axis* 10001", "on alliedsi 0", "on axissi 0"}) &&
                         allied->GetData() == CkId(0x60100000) && axis->GetData() == CkId(0x60100000));
        WM(r.ckSide, ScMessages() == CmText("0 200007a 0 %u|", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s))));
        g_controlMessages.clear();

        alignas(16) static unsigned char zoneMemory[sizeof(C_MPZone)];
        auto *zone = reinterpret_cast<C_MPZone *>(zoneMemory);
        zone->m_team = 1;
        s->m_index = static_cast<int>(reinterpret_cast<uintptr_t>(zone));
        s->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        s->Tick(5);
        WM(r.ckSide, s->m_side == 1 && s->m_timer == 2000 &&
                         GfLogIs({"on alliedsi 1", "on axissi 0", "update alliedsi", "update axissi", "notify 4010000 0"}));
        zone->m_team = 0;
        g_gfLog.clear();
        s->Tick(1999);
        WM(r.ckSide, s->m_side == 1 && s->m_timer == 1 && g_gfLog.empty());
        s->Tick(1);
        WM(r.ckSide, s->m_side == 0 && s->m_timer == 2000 &&
                         GfLogIs({"on alliedsi 0", "on axissi 1", "update alliedsi", "update axissi", "notify 4010000 0"}));
        g_gfLog.clear();
        s->Tick(2500);
        WM(r.ckSide, s->m_side == 0 && s->m_timer == 2000 && g_gfLog.empty());
        // The messages: the side set (the same: nothing; another than 0 or 1: neither), got.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_SIDE, 2);
        handled &= s->HandleMessage(&m);
        WM(r.ckSide, s->m_side == 2 &&
                         GfLogIs({"on alliedsi 0", "on axissi 0", "update alliedsi", "update axissi", "notify 4010000 0"}));
        g_gfLog.clear();
        handled &= s->HandleMessage(&m);
        WM(r.ckSide, g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_SIDE, 7);
        handled &= s->HandleMessage(&m);
        WM(r.ckSide, s->m_side == 7 && g_gfLog.size() == 5 && g_gfLog[0] == "on alliedsi 0" && g_gfLog[1] == "on axissi 0");
        m = CsMessage(CONTROL_MSG_GET_SIDE, 9);
        handled &= s->HandleMessage(&m);
        WM(r.ckSide, m.value == 7);
        // The key click: its notification.
        g_gfLog.clear();
        WM(r.ckSide, s->OnKeyClick(1) == CONTROL_HANDLED && GfLogIs({"notify 4012000 0"}) && s->OnClick(1) == CONTROL_NOT_HANDLED);
        // Others: the base's.
        m = CsMessage(CONTROL_MSG_SET_VALUE, 4);
        handled &= s->HandleMessage(&m);
        WM(r.ckSide, s->m_value == 4);
        s->m_anyCallback = S_ControlCallback();
        delete s;
        // Fewer than 3 children: false.
        I3D_frame *two = GfFrame(FRAME_DUMMY, "sitwo");
        GfVisual("alliedst", I3DVISUAL_OBJECT, two);
        GfVisual("axisst", I3DVISUAL_OBJECT, two);
        C_SideIndicator t(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.ckSide, !t.Create(two, two, nullptr) && g_gfLog.empty() && g_controlMessages.empty());
        allied->SetData(nullptr);
        axis->SetData(nullptr);
    }

    // --- the mark: a title on its text frame ("00"), the frames at the empty normal one's place
    // (it on, its id with 1) ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "mbframe");
        I3D_frame *text = CsVisual("text_mb", frame);
        I3D_frame *noActive = GfVisual("noactivedmb", I3DVISUAL_OBJECT, frame);
        I3D_frame *noNormal = GfVisual("nonormalmb", I3DVISUAL_OBJECT, frame);
        I3D_frame *yesActive = GfVisual("yesactivedmb", I3DVISUAL_OBJECT, frame);
        I3D_frame *yesNormal = GfVisual("yesnormalmb", I3DVISUAL_OBJECT, frame);
        I3D_frame *emptyActive = GfVisual("emptyactivedmb", I3DVISUAL_OBJECT, frame);
        I3D_frame *emptyNormal = GfVisual("emptynormalmb", I3DVISUAL_OBJECT, frame);
        GfPlace(emptyNormal, S_vector(0.1f, 0.2f, 0.3f));
        auto *k = new C_MarkBox(scene, 0x70100000, nullptr);
        WM(r.ckMark, k->m_value == 0 && !k->m_title);
        g_gfLog.clear();
        WM(r.ckMark, k->Create(frame, frame, nullptr) && k->m_title && k->m_title->m_id == 0x70100001 && k->m_title->m_parent == k &&
                         !k->m_title->m_takesInput && k->m_title->m_frame == text && k->m_children.empty() &&
                         CsRuns(*k->m_title->m_box) == "00/11 ");
        WM(r.ckMark, !g_gfLog.empty() && g_gfLog[0] == "find mbframe text_* 10001" &&
                         CkLogEnds({"find mbframe noactived* 10001", "find mbframe nonormal* 10001", "find mbframe yesactived* 10001",
                                    "find mbframe yesnormal* 10001", "find mbframe emptyactived* 10001",
                                    "find mbframe emptynormal* 10001", "on emptynormalmb 1", "update emptynormalmb",
                                    "on emptyactivedmb 0", "update emptyactivedmb", "on yesnormalmb 0", "update yesnormalmb",
                                    "on yesactivedmb 0", "update yesactivedmb", "on nonormalmb 0", "update nonormalmb",
                                    "on noactivedmb 0", "update noactivedmb"}));
        const S_vector place(0.1f, 0.2f, 0.3f);
        WM(r.ckMark, k->m_noActive == noActive && k->m_noNormal == noNormal && k->m_yesActive == yesActive &&
                         k->m_yesNormal == yesNormal && k->m_emptyActive == emptyActive && k->m_emptyNormal == emptyNormal &&
                         noActive->GetPos() == place && yesNormal->GetPos() == place && emptyActive->GetPos() == place &&
                         emptyNormal->GetData() == CkId(0x70100001) && yesNormal->GetData() == CkId(0x70100000) &&
                         noActive->GetData() == CkId(0x70100000));

        // The number in the title, two digits (more: all).
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_NUMBER, 7);
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, CsRuns(*k->m_title->m_box) == "07/11 ");
        m = CsMessage(CONTROL_MSG_SET_NUMBER, 123);
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, CsRuns(*k->m_title->m_box) == "123/11 ");
        // Marked (any value but 0: 1): the yes frame, else the empty one (not updated).
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 5);
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, k->m_value == 1 && GfLogIs({"on yesnormalmb 1", "on emptynormalmb 0"}));
        m = CsMessage(CONTROL_MSG_GET_CHECKED, 9);
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, m.value == 1);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_CHECKED, 0x100);
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, k->m_value == 0 && GfLogIs({"on yesnormalmb 0", "on emptynormalmb 1"}));
        g_gfLog.clear();
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, k->m_value == 0 && g_gfLog.size() == 2);
        // Others: the base's.
        m = CsMessage(CONTROL_MSG_SET_INDEX, 2);
        handled &= k->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "zz");
        handled &= k->HandleMessage(&m);
        WM(r.ckMarkMsgs, k->m_index == 2 && CsRuns(*k->m_title->m_box) == "123/11 ");

        // The input: the click and the key click notify 0x400E000 with the value; the enter handled
        // unless inactive, the leave always.
        k->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.ckMark, k->OnClick(1) == CONTROL_HANDLED && GfLogIs({"notify 400e000 0"}));
        k->m_value = 1;
        g_gfLog.clear();
        WM(r.ckMark, k->OnKeyClick(1) == CONTROL_HANDLED && GfLogIs({"notify 400e000 1"}));
        WM(r.ckMark, k->OnEnter(1) == CONTROL_HANDLED && k->OnLeave(1) == CONTROL_HANDLED && k->OnPress(1) == CONTROL_NOT_HANDLED);
        k->m_inactive = true;
        WM(r.ckMark, k->OnEnter(1) == 0 && k->OnLeave(1) == CONTROL_HANDLED);
        k->m_inactive = false;
        k->m_anyCallback = S_ControlCallback();
        // The refresh: the title's only; deleted with it.
        delete k->m_title;
        k->m_title = new C_CkTitle();
        k->m_children.push_back(new C_CsChild("mk1", 0));
        g_gfLog.clear();
        k->Refresh();
        WM(r.ckMark, GfLogIs({"title refresh"}));
        g_gfLog.clear();
        delete k;
        WM(r.ckMark, !g_gfLog.empty() && g_gfLog.front() == "title deleted");

        // Without a text frame: no title (SET_NUMBER and the refresh nothing); a frame not found: false.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "mbbare");
        for (const char *name : {"noactivedmc", "nonormalmc", "yesactivedmc", "yesnormalmc", "emptyactivedmc", "emptynormalmc"})
            GfVisual(name, I3DVISUAL_OBJECT, bare);
        C_MarkBox b(scene, 0x70200000, nullptr);
        WM(r.ckMark, b.Create(bare, bare, nullptr) && !b.m_title);
        m = CsMessage(CONTROL_MSG_SET_NUMBER, 3);
        g_gfLog.clear();
        handled &= b.HandleMessage(&m);
        b.Refresh();
        WM(r.ckMark, g_gfLog.empty());
        I3D_frame *missing = GfFrame(FRAME_DUMMY, "mbmissing");
        for (const char *name : {"noactivedmd", "nonormalmd", "yesactivedmd", "yesnormalmd", "emptyactivedmd"})
            GfVisual(name, I3DVISUAL_OBJECT, missing);
        C_MarkBox n(scene, 0, nullptr);
        g_gfLog.clear();
        WM(r.ckMark, !n.Create(missing, missing, nullptr) && n.m_emptyActive && !n.m_emptyNormal && g_gfLog.size() == 7);
        I3D_frame *first = GfFrame(FRAME_DUMMY, "mbfirst");
        for (const char *name : {"xnoactivedme", "nonormalme", "yesactivedme", "yesnormalme", "emptyactivedme", "emptynormalme"})
            GfVisual(name, I3DVISUAL_OBJECT, first);
        C_MarkBox e(scene, 0, nullptr);
        WM(r.ckMark, !e.Create(first, first, nullptr) && !e.m_noActive && e.m_emptyNormal);
        for (I3D_frame *f : {frame, bare})
            for (I3D_frame *c = f->m_firstChild; c; c = c->m_nextSibling)
                c->SetData(nullptr);
    }

    WM(r.ckBoxMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("check: ckBox %d ckBoxInput %d ckBoxMsgs %d ckToggle %d ckToggleInput %d ckHigh %d ckHighInput %d ckHighMsgs %d "
                "ckHalf %d ckSide %d ckMark %d ckMarkMsgs %d\n",
                r.ckBox, r.ckBoxInput, r.ckBoxMsgs, r.ckToggle, r.ckToggleInput, r.ckHigh, r.ckHighInput, r.ckHighMsgs, r.ckHalf,
                r.ckSide, r.ckMark, r.ckMarkMsgs);
}
