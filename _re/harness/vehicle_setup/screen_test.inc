// Part 9d-i's screens (game/screen.cpp: the descriptions, C_Screen, the message box), in the factory
// build (its real factory makes the screens' controls), run by ControlsTest after FactoryTest: the
// frames GfFrame's, the cursor faked, the children C_CsChild's where a test puts them in. Prints
// "screens:" with 1 per check met.
#define private public
#define protected public
#include "game/screen.h"
#undef protected
#undef private

namespace {

struct S_ScreenResults {
    bool scDescs = true, scMenuDesc = true, scNew = true, scBuild = true, scOpen = true, scHide = true, scTick = true;
    bool scKeys = true, scMessages = true, mbBuild = true, mbShow = true, mbShowId = true, mbClose = true, mbKeys = true;
    bool mbButtons = true, dsNew = true, dsBuild = true, dsTick = true, dsMessages = true;
};

// The description popup's text logging (part 9e-xiii): its refresh, messages (returning their id's
// low bit) and deletion.
struct C_DsTitle : C_TitleControl {
    C_DsTitle() : C_TitleControl(nullptr, 0, nullptr) {}
    ~C_DsTitle() override { GfLog("dstitle deleted"); }
    void Refresh() override { GfLog("dstitle refresh"); }
    bool HandleMessage(S_ControlMessage *message) override
    {
        GfLog("dstitle message %x %x", message->target, message->id);
        return (message->id & 1) != 0;
    }
};

// The queue as text ("target id value value2 text; ") and emptied.
std::string ScQueue()
{
    std::string s;
    for (const S_ControlMessage &m : g_controlMessages)
        s += CmText("%x %x %x %x %s; ", m.target, m.id, m.value, static_cast<uint32_t>(m.value2), m.text.c_str());
    g_controlMessages.clear();
    return s;
}

// Descriptions logging their deletion ("<name> deleted").
struct S_ScScreenDesc : S_ScreenDesc {
    S_ScScreenDesc(const char *name, uint32_t id) : S_ScreenDesc(name, "f", id) {}
    ~S_ScScreenDesc() override { GfLog("%s deleted", name.c_str()); }
};
struct S_ScControlDesc : S_ControlDesc {
    S_ScControlDesc(const char *name) : S_ControlDesc(2, name, 0) {}
    ~S_ScControlDesc() override { GfLog("%s deleted", name.c_str()); }
};

// A message's text.
std::string ScMessage(const S_ControlMessage &m)
{
    return CmText("%x %x %x %x %x %s", m.target, m.id, m.value, static_cast<uint32_t>(m.value2), static_cast<uint32_t>(m.value3),
                  m.text.c_str());
}

// A text button's frames under `parent`: the frame and its normal / active visuals.
I3D_frame *ScButtonFrame(const char *name, I3D_frame *parent)
{
    I3D_frame *f = GfFrame(FRAME_DUMMY, name, parent);
    CsVisual((std::string("normal") + name).c_str(), f);
    CsVisual((std::string("actived") + name).c_str(), f);
    return f;
}

// A screen's children C_CsChild's (ids its own with 1.. in the first field).
void ScChildren(C_Screen &s, int n)
{
    static const char *names[] = {"sc1", "sc2", "sc3"};
    for (int i = 0; i < n; ++i)
        s.m_children.push_back(new C_CsChild(names[i], s.m_id | static_cast<uint32_t>(i + 1) << 20));
}

} // namespace

void ScreenTest(I3D_scene *scene)
{
    S_ScreenResults r;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the screen's description: made, its controls added (their ids the screen's with their
    // index + 1 in the first field, 6 bits), its messages; deleted with its controls (the last first) ---
    {
        auto *d = new S_ScreenDesc("NAME", "file", 0x08000000);
        WM(r.scDescs, d->name == C_str("NAME") && d->file == C_str("file") && d->id == 0x08000000 && d->controls.empty() &&
                          d->messages.empty() && d->strings.empty());
        S_ControlDesc *a = d->AddControl(0x14, "ca*");
        S_ControlDesc *b = d->AddControl(5, "cb");
        WM(r.scDescs, d->controls.size() == 2 && d->controls[0] == a && d->controls[1] == b && a->type == 0x14 &&
                          a->name == C_str("ca*") && a->id == 0x08100000 && b->type == 5 && b->id == 0x08200000 &&
                          a->messages.empty() && a->children.empty());
        for (int i = 2; i < 62; ++i)
            d->AddControl(2, "x");
        S_ControlDesc *last = d->AddControl(2, "x63"), *wrap = d->AddControl(2, "x64"), *next = d->AddControl(2, "x65");
        WM(r.scDescs, last->id == 0x0BF00000 && wrap->id == 0x08000000 && next->id == 0x08100000);
        AddScreenMessage(d, 0x0C100000, 0x1000014, 5, 6, 7);
        AddScreenMessage(d, 1, 2, 3, 4, 8);
        WM(r.scDescs, d->messages.size() == 2 && ScMessage(d->messages[0]) == "c100000 1000014 5 6 7 " &&
                          ScMessage(d->messages[1]) == "1 2 3 4 8 ");
        auto *e = new S_ScreenDesc(nullptr, nullptr, 5);
        e->controls.push_back(new S_ScControlDesc("e1"));
        e->controls.push_back(new S_ScControlDesc("e2"));
        e->controls.push_back(new S_ScControlDesc("e3"));
        g_gfLog.clear();
        delete e;
        WM(r.scDescs, GfLogIs({"e3 deleted", "e2 deleted", "e1 deleted"}));
        delete d;
    }

    // --- the menu's: its screens added (their ids their index + 1 in the top 6 bits); deleted with
    // them (the last first) ---
    {
        auto *m = new S_MenuDesc();
        S_ScreenDesc *a = m->AddScreen("A", "fa");
        S_ScreenDesc *b = m->AddScreen("B", "fb");
        WM(r.scMenuDesc, m->screens.size() == 2 && m->screens[0] == a && m->screens[1] == b && a->id == 0x04000000 &&
                             b->id == 0x08000000 && a->name == C_str("A") && a->file == C_str("fa") && b->name == C_str("B") &&
                             b->file == C_str("fb") && a->controls.empty());
        WM(r.scMenuDesc, a->AddControl(2, "x")->id == 0x04100000 && b->AddControl(2, "x")->id == 0x08100000);
        auto *n = new S_MenuDesc();
        n->screens.push_back(new S_ScScreenDesc("s1", 0));
        n->screens.push_back(new S_ScScreenDesc("s2", 0));
        n->screens.push_back(new S_ScScreenDesc("s3", 0));
        WM(r.scMenuDesc, n->AddScreen("s4", "f")->id == 0x10000000);
        g_gfLog.clear();
        delete n;
        WM(r.scMenuDesc, GfLogIs({"s3 deleted", "s2 deleted", "s1 deleted"}));
        delete m;
    }

    // --- a screen: no parent, no id, the scene, its frame, the cursor acquired; no focus, no name;
    // deleted: the cursor released, its children deleted ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "scframe");
        const int refs = g_csCursor.refs;
        g_gfLog.clear();
        auto *s = new C_Screen(scene, frame);
        WM(r.scNew, s->m_id == 0xFFFFFFFF && !s->m_parent && s->m_scene == scene && s->m_screenFrame == frame && !s->m_frame &&
                        s->m_cursor == reinterpret_cast<C_Cursor *>(&g_csCursor) && g_csCursor.refs == refs + 1 &&
                        s->m_focus == -1 && s->m_name.IsEmpty() && !s->m_80 && s->m_shown && s->m_takesInput &&
                        !s->m_inactive && s->m_index == -1 && s->m_value == 0 && !s->HasPopup());
        WM(r.scNew, GfLogIs({"cursor acquire 1"}) && s->GetScreenFrame() == frame);
        s->m_children.push_back(new C_CtLogged("sckid"));
        g_gfLog.clear();
        delete s;
        WM(r.scNew, g_csCursor.refs == refs && GfLogIs({"cursor release", "sckid deleted"}));
    }

    // --- built: the description's controls made on the screen's frame (the root too; one without its
    // frame skipped), its name and id taken; true ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "scbframe");
        I3D_frame *title = GfFrame(FRAME_DUMMY, "sctitle", frame);
        I3D_frame *group = GfFrame(FRAME_DUMMY, "scgroup", frame);
        S_ScreenDesc d("SCB", "scb", 0x08000000);
        d.AddControl(0x13, "sctit*");
        d.AddControl(5, "scnone");
        d.AddControl(5, "scgr*");
        AddDescMessage(d.controls[0], 0, CONTROL_MSG_SET_TEXT_ID, 0x18, 43);
        AddScreenMessage(&d, 0, CONTROL_MSG_SET_VALUE, 9, 0, 0);
        C_Screen s(scene, frame);
        g_gfLog.clear();
        WM(r.scBuild, s.Build(&d) && s.m_id == 0x08000000 && s.m_name == C_str("SCB") && s.m_value == 0 &&
                          s.m_children.size() == 2);
        C_Control *t = s.m_children.size() == 2 ? s.m_children[0] : nullptr;
        C_Control *g = s.m_children.size() == 2 ? s.m_children[1] : nullptr;
        WM(r.scBuild, t && typeid(*t) == typeid(C_TitleControl) && t->m_frame == title && t->m_id == 0x08100000 &&
                          t->m_parent == &s && CsRuns(*static_cast<C_TitleControl *>(t)->m_box) == "text 43/24 " && g &&
                          typeid(*g) == typeid(C_ControlGroup) && g->m_frame == group && g->m_id == 0x08300000);
        WM(r.scBuild, g_gfLog.size() >= 3 && g_gfLog[0] == "find scbframe sctit* 10021" &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "find scbframe scnone 10021") == 1 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "find scbframe scgr* 10021") == 1);
        title->SetData(nullptr);
        group->SetData(nullptr);
        // (The root the screen's frame: a scrolled text's list links its select frame to it.)
        I3D_frame *frame2 = GfFrame(FRAME_DUMMY, "scbframe2");
        I3D_frame *scroll = GfFrame(FRAME_DUMMY, "scscroll", frame2);
        CbScrolledFrames(scroll, "sc");
        I3D_frame *select = GfVisual("selectsc", I3DVISUAL_OBJECT, Find(scroll, "textsc", 0xFFFF));
        S_ScreenDesc d2("SCB2", "scb2", 0x0C000000);
        d2.AddControl(0xf, "scscroll");
        C_Screen s2(scene, frame2);
        s2.Build(&d2);
        auto *st = s2.m_children.size() == 1 ? dynamic_cast<C_ScrolledText *>(s2.m_children[0]) : nullptr;
        WM(r.scBuild, st && st->m_list && st->m_list->m_selectFrame == select && select->m_parent == frame2);
    }

    // --- opened (the frame off): the enter (the id), the frame on, shown; again (on): nothing ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "scoframe");
        frame->m_frameFlags &= ~1u;
        C_Screen s(scene, frame);
        s.m_id = 0x04000000;
        ScChildren(s, 2);
        s.m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_csReturn = 5;
        g_gfLog.clear();
        s.Open(0x04200000);
        WM(r.scOpen, s.m_focus == 1 && GfLogIs({"sc2 enter 4200000", "on scoframe 1", "notify 400d000 0"}));
        g_gfLog.clear();
        s.Open(0x04100000);
        WM(r.scOpen, s.m_focus == 1 && g_gfLog.empty());
        // (Not a child: no focus, still shown.)
        frame->m_frameFlags &= ~1u;
        s.m_focus = -1;
        g_gfLog.clear();
        s.Open(0x04400000);
        WM(r.scOpen, s.m_focus == -1 && GfLogIs({"on scoframe 1", "notify 400d000 0"}));
    }

    // --- hidden (the frame on): off, the focus left (with 0), hidden; again (off): nothing ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "schframe");
        frame->m_frameFlags |= 1u;
        C_Screen s(scene, frame);
        s.m_id = 0x04000000;
        ScChildren(s, 2);
        s.m_focus = 0;
        s.m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_csReturn = 5;
        g_gfLog.clear();
        s.Hide();
        WM(r.scHide, s.m_focus == -1 && GfLogIs({"on schframe 0", "sc1 leave 0", "notify 400c000 0"}) && s.m_shown);
        s.m_focus = 1;
        g_gfLog.clear();
        s.Hide();
        WM(r.scHide, s.m_focus == 1 && g_gfLog.empty());
        // (The focus refusing the leave: kept.)
        frame->m_frameFlags |= 1u;
        g_csReturn = 0;
        g_gfLog.clear();
        s.Hide();
        WM(r.scHide, s.m_focus == 1 && GfLogIs({"on schframe 0", "sc2 leave 0", "notify 400c000 0"}));
        g_csReturn = 5;
    }

    // --- a tick: the screen's notification (the time), then the focus' tick ---
    {
        C_Screen s(scene, GfFrame(FRAME_DUMMY, "sctframe"));
        ScChildren(s, 2);
        s.m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        s.Tick(40);
        WM(r.scTick, GfLogIs({"notify 4014000 40"}));
        s.m_focus = 1;
        g_gfLog.clear();
        s.Tick(-3);
        WM(r.scTick, GfLogIs({"notify 4014000 4294967293", "sc2 tick -3"}));
        s.m_focus = 2;
        g_gfLog.clear();
        s.Tick(7);
        WM(r.scTick, GfLogIs({"notify 4014000 7"}));
    }

    // --- the keys: the active focus' (taken: true); else Enter / Space / the keypad's Enter (the
    // low byte): the focus' click with its id; else false ---
    {
        C_Screen s(scene, GfFrame(FRAME_DUMMY, "sckframe"));
        s.m_id = 0x04000000;
        ScChildren(s, 2);
        bool ok = true;
        g_csReturn = 5;
        g_gfLog.clear();
        ok &= !s.Unknown20(0x1C);
        WM(r.scKeys, ok && g_gfLog.empty());
        s.m_focus = 1;
        g_gfLog.clear();
        ok &= s.Unknown20(0x30);
        WM(r.scKeys, ok && GfLogIs({"sc2 u20 48"}));
        g_csReturn = 0;
        g_gfLog.clear();
        ok &= !s.Unknown20(0x30);
        ok &= s.Unknown20(0x1C);
        ok &= s.Unknown20(0x39);
        ok &= s.Unknown20(0x9C);
        ok &= s.Unknown20(0x11C);
        WM(r.scKeys, ok && GfLogIs({"sc2 u20 48", "sc2 u20 28", "sc2 click 4200000", "sc2 u20 57", "sc2 click 4200000",
                                    "sc2 u20 156", "sc2 click 4200000", "sc2 u20 284", "sc2 click 4200000"}));
        // (An inactive focus: not asked; its click still.)
        s.m_children[1]->m_inactive = true;
        g_csReturn = 5;
        g_gfLog.clear();
        ok &= !s.Unknown20(0x30);
        ok &= s.Unknown20(0x39);
        WM(r.scKeys, ok && GfLogIs({"sc2 click 4200000"}));
        ok &= !s.Unknown20(0x1D) && !s.Unknown20(0x01) && !s.Unknown20(0x3A);
        s.m_focus = 3;
        g_gfLog.clear();
        ok &= !s.Unknown20(0x1C);
        WM(r.scKeys, ok && g_gfLog.empty());
    }

    // --- the messages: the screen's own (target 0) and the notifications the base's; the edit: the
    // focus left with its index, the target entered; the target's child's (none: false) ---
    {
        C_Screen s(scene, GfFrame(FRAME_DUMMY, "scmframe"));
        s.m_id = 0x04000000;
        ScChildren(s, 3);
        g_csReturn = 5;
        bool ok = true;
        S_ControlMessage m = CtMessage(0, CONTROL_MSG_SET_VALUE, 6);
        g_gfLog.clear();
        ok &= s.HandleMessage(&m);
        WM(r.scMessages, ok && s.m_value == 6 && g_gfLog.empty());
        m = CtMessage(0x04200000, CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_VALUE, 1);
        ok &= s.HandleMessage(&m);
        WM(r.scMessages, ok && s.m_routes.size() == 1 && s.m_routes[0].target == 0x04200000 && g_gfLog.empty());
        m = CtMessage(0x04200000, CONTROL_MSG_SET_INDEX, 2);
        ok &= s.HandleMessage(&m);
        WM(r.scMessages, ok && s.m_index == -1 && m.value == 0x77 && GfLogIs({"sc2 message 4200000 1000009"}));
        g_csReturn = 0;
        g_gfLog.clear();
        ok &= s.HandleMessage(&m);
        g_csReturn = 5;
        m = CtMessage(0x04500000, CONTROL_MSG_SET_INDEX, 2);
        ok &= !s.HandleMessage(&m);
        WM(r.scMessages, ok && GfLogIs({"sc2 message 4200000 1000009"}) && s.m_index == -1);
        m = CtMessage(0x04100000, CONTROL_MSG_SET_INDEX, 2);
        g_gfLog.clear();
        ok &= s.HandleMessage(&m);
        WM(r.scMessages, ok && GfLogIs({"sc1 message 4100000 1000009"}));
        // The edit: the focus (1) left with 1, the target (3) entered, its child's message.
        s.m_focus = 1;
        m = CtMessage(0x04300000, CONTROL_MSG_EDIT | 0x100000);
        g_gfLog.clear();
        ok &= s.HandleMessage(&m);
        WM(r.scMessages, ok && s.m_focus == 2 &&
                             GfLogIs({"sc2 leave 1", "sc3 enter 4300000", "sc3 message 4300000 2100016"}));
        // (The leave refused: no enter; no focus: no leave.)
        g_csReturn = 0;
        g_gfLog.clear();
        ok &= s.HandleMessage(&m);
        WM(r.scMessages, ok && s.m_focus == 2 && GfLogIs({"sc3 leave 2", "sc3 message 4300000 2100016"}));
        g_csReturn = 5;
        s.m_focus = -1;
        m = CtMessage(0x04500000, CONTROL_MSG_EDIT);
        g_gfLog.clear();
        ok &= !s.HandleMessage(&m);
        WM(r.scMessages, ok && s.m_focus == -1 && g_gfLog.empty());
    }

    // --- the message box: its controls added to the description (after its own) and made: the
    // text, yes / no / ok (ok hidden), their clicks the box's ---
    I3D_frame *boxFrame = GfFrame(FRAME_DUMMY, "CONFIRM");
    I3D_frame *boxText = GfFrame(FRAME_DUMMY, "text", boxFrame);
    I3D_frame *yes = ScButtonFrame("byes", boxFrame);
    I3D_frame *no = ScButtonFrame("bno", boxFrame);
    I3D_frame *ok = ScButtonFrame("bok", boxFrame);
    S_ScreenDesc boxDesc("CONFIRM", "confirm", 0);
    auto *box = new C_MessageBox(scene, boxFrame);
    {
        WM(r.mbBuild, box->m_kind == MESSAGE_BOX_NONE && box->m_yesMessage == 0 && box->m_noMessage == 0 &&
                          box->m_screenFrame == boxFrame && box->m_id == 0xFFFFFFFF && box->GetKind() == -1);
        g_controlMessages.clear();
        WM(r.mbBuild, box->Build(&boxDesc) && box->m_id == 0 && box->m_name == C_str("CONFIRM") && boxDesc.controls.size() == 4);
        const auto descIs = [&](size_t i, int type, const char *name, uint32_t id) {
            return i < boxDesc.controls.size() && boxDesc.controls[i]->type == type && boxDesc.controls[i]->name == C_str(name) &&
                   boxDesc.controls[i]->id == id;
        };
        const std::string self = CmText("%x", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(box)));
        const std::string fn = CmText("%x", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&C_MessageBox::OnButton)));
        const auto messagesAre = [&](size_t i, std::vector<std::string> expected) {
            if (i >= boxDesc.controls.size())
                return false;
            std::vector<std::string> got;
            for (const S_ControlMessage &m : boxDesc.controls[i]->messages)
                got.push_back(ScMessage(m));
            return GfVecIs(got, expected);
        };
        const std::string click = "0 1000007 4001000 " + self + " " + fn + " ";
        WM(r.mbBuild, descIs(0, 0x13, "text", 0x100000) && descIs(1, 0x14, "byes*", 0x200000) &&
                          descIs(2, 0x14, "bno*", 0x300000) && descIs(3, 0x14, "bok", 0x400000));
        WM(r.mbBuild, messagesAre(0, {"0 1000015 18 7d4 0 "}) && messagesAre(1, {"0 1000015 d 7d1 0 ", click}) &&
                          messagesAre(2, {"0 1000015 d 7d0 0 ", click}) &&
                          messagesAre(3, {"0 1000015 d 7dd 0 ", "0 1000004 0 0 0 ", click}));
        WM(r.mbBuild, box->m_children.size() == 4 && box->m_children[0]->m_frame == boxText &&
                          box->m_children.size() == 4 && box->m_children[1]->m_frame == yes && box->m_children[2]->m_frame == no &&
                          box->m_children[3]->m_frame == ok && box->m_children[1]->m_shown && box->m_children[2]->m_shown &&
                          !box->m_children[3]->m_shown && box->m_children[1]->m_callbacks.size() == 1 &&
                          box->m_children[3]->m_callbacks.size() == 1 && box->m_children[0]->m_callbacks.empty() &&
                          g_controlMessages.empty());
        WM(r.mbBuild, box->m_children.size() == 4 &&
                          CsRuns(*static_cast<C_TextControl *>(box->m_children[0])->m_box) == "text 2004/24 " &&
                          CsRuns(*static_cast<C_TextControl *>(box->m_children[3])->m_box) == "text 2013/13 ");
        // (A description with a control of its own: the box's after it.)
        S_ScreenDesc other("O", "o", 0x04000000);
        other.AddControl(2, "own");
        C_MessageBox b2(scene, GfFrame(FRAME_DUMMY, "mb2"));
        b2.Build(&other);
        WM(r.mbBuild, other.controls.size() == 5 && other.controls[1]->name == C_str("text") &&
                          other.controls[1]->id == 0x04200000 && other.controls[4]->id == 0x04500000 && b2.m_children.empty() &&
                          b2.m_id == 0x04000000);
    }

    // --- shown: the buttons by the kind, the text (the style; kind 3 the second), the answers kept,
    // shown posted; again while shown: nothing ---
    {
        bool okay = true;
        box->ShowText(MESSAGE_BOX_TEXT, C_str("wait"), 0x11, 0x12);
        okay &= ScQueue() == "200000 1000004 0 0 ; 300000 1000004 0 0 ; 400000 1000004 0 0 ; 100000 1000014 18 0 wait; "
                             "0 2000096 0 0 ; ";
        WM(r.mbShow, okay && box->m_kind == MESSAGE_BOX_TEXT && box->m_yesMessage == 0x11 && box->m_noMessage == 0x12);
        box->ShowText(MESSAGE_BOX_OK, C_str("x"), 1, 2);
        WM(r.mbShow, ScQueue().empty() && box->m_kind == MESSAGE_BOX_TEXT && box->m_yesMessage == 0x11);
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowText(MESSAGE_BOX_YES_NO, C_str("sure?"), 0x21, 0x22);
        okay &= ScQueue() == "200000 1000003 0 0 ; 300000 1000003 0 0 ; 400000 1000004 0 0 ; 100000 1000014 18 0 sure?; "
                             "0 2000096 0 0 ; ";
        WM(r.mbShow, okay && box->m_kind == MESSAGE_BOX_YES_NO && box->m_yesMessage == 0x21 && box->m_noMessage == 0x22);
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowText(MESSAGE_BOX_OK, C_str("done"), 0x31, 0);
        okay &= ScQueue() == "200000 1000004 0 0 ; 300000 1000004 0 0 ; 400000 1000003 0 0 ; 100000 1000014 18 0 done; "
                             "0 2000096 0 0 ; ";
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowText(MESSAGE_BOX_YES_NO_2, C_str("two"), 0, 0x42);
        okay &= ScQueue() == "200000 1000003 0 0 ; 300000 1000003 0 0 ; 400000 1000004 0 0 ; 100000 1000014 19 0 two; "
                             "0 2000096 0 0 ; ";
        WM(r.mbShow, okay && box->m_kind == MESSAGE_BOX_YES_NO_2 && box->m_yesMessage == 0 && box->m_noMessage == 0x42);
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowText(4, C_str("four"), 5, 6);
        okay &= ScQueue() == "100000 1000014 18 0 four; 0 2000096 0 0 ; ";
        WM(r.mbShow, okay && box->m_kind == 4 && box->m_yesMessage == 5 && box->m_noMessage == 6);
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowText(-2, C_str("neg"), 7, 8);
        okay &= ScQueue() == "100000 1000014 18 0 neg; 0 2000096 0 0 ; ";
        WM(r.mbShow, okay && box->m_kind == -2);
        box->m_kind = MESSAGE_BOX_NONE;
    }

    // --- the same with a text id ---
    {
        bool okay = true;
        box->ShowTextId(MESSAGE_BOX_TEXT, 900, 0x11, 0x12);
        okay &= ScQueue() == "200000 1000004 0 0 ; 300000 1000004 0 0 ; 400000 1000004 0 0 ; 100000 1000015 18 384 ; "
                             "0 2000096 0 0 ; ";
        WM(r.mbShowId, okay && box->m_kind == MESSAGE_BOX_TEXT && box->m_yesMessage == 0x11 && box->m_noMessage == 0x12);
        box->ShowTextId(MESSAGE_BOX_OK, 901, 1, 2);
        WM(r.mbShowId, ScQueue().empty() && box->m_kind == MESSAGE_BOX_TEXT && box->m_noMessage == 0x12);
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowTextId(MESSAGE_BOX_YES_NO, 902, 0x21, 0x22);
        okay &= ScQueue() == "200000 1000003 0 0 ; 300000 1000003 0 0 ; 400000 1000004 0 0 ; 100000 1000015 18 386 ; "
                             "0 2000096 0 0 ; ";
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowTextId(MESSAGE_BOX_OK, 903, 0x31, 0x32);
        okay &= ScQueue() == "200000 1000004 0 0 ; 300000 1000004 0 0 ; 400000 1000003 0 0 ; 100000 1000015 18 387 ; "
                             "0 2000096 0 0 ; ";
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowTextId(MESSAGE_BOX_YES_NO_2, 904, 0x41, 0x42);
        okay &= ScQueue() == "200000 1000003 0 0 ; 300000 1000003 0 0 ; 400000 1000004 0 0 ; 100000 1000015 19 388 ; "
                             "0 2000096 0 0 ; ";
        WM(r.mbShowId, okay && box->m_kind == MESSAGE_BOX_YES_NO_2 && box->m_yesMessage == 0x41 && box->m_noMessage == 0x42);
        box->m_kind = MESSAGE_BOX_NONE;
        box->ShowTextId(7, 905, 0, 0);
        okay &= ScQueue() == "100000 1000015 18 389 ; 0 2000096 0 0 ; ";
        WM(r.mbShowId, okay && box->m_kind == 7);
        box->m_kind = MESSAGE_BOX_NONE;
    }

    // --- closed: none shown, closed posted, the answers kept (a click then: nothing) ---
    {
        box->ShowTextId(MESSAGE_BOX_YES_NO, 1, 0x51, 0x52);
        ScQueue();
        box->Close();
        WM(r.mbClose, box->m_kind == MESSAGE_BOX_NONE && box->m_yesMessage == 0x51 && box->m_noMessage == 0x52 &&
                          ScQueue() == "0 2000097 0 0 ; ");
        box->Close();
        WM(r.mbClose, box->m_kind == MESSAGE_BOX_NONE && ScQueue() == "0 2000097 0 0 ; ");
        box->m_children[1]->Notify(CONTROL_NOTIFY_CLICK, 0);
        WM(r.mbClose, ScQueue().empty() && box->m_yesMessage == 0x51);
    }

    // --- the keys: none shown or a text alone: taken; Escape: closed, the second answer (if any),
    // the answers cleared; else the screen's (the focus' click) ---
    {
        bool okay = true;
        box->m_kind = MESSAGE_BOX_NONE;
        okay &= box->Unknown20(0x01) && box->Unknown20(0x1C);
        box->m_kind = MESSAGE_BOX_TEXT;
        okay &= box->Unknown20(0x01) && box->Unknown20(0x30);
        WM(r.mbKeys, okay && ScQueue().empty() && box->m_kind == MESSAGE_BOX_TEXT && box->m_yesMessage == 0x51);
        box->m_kind = MESSAGE_BOX_YES_NO;
        okay &= box->Unknown20(0x101);
        WM(r.mbKeys, okay && box->m_kind == MESSAGE_BOX_NONE && box->m_yesMessage == 0 && box->m_noMessage == 0 &&
                         ScQueue() == "0 2000097 0 0 ; 0 52 0 0 ; ");
        box->m_kind = MESSAGE_BOX_OK;
        box->m_yesMessage = 0x61;
        okay &= box->Unknown20(0x01);
        WM(r.mbKeys, okay && box->m_kind == MESSAGE_BOX_NONE && box->m_yesMessage == 0 && ScQueue() == "0 2000097 0 0 ; ");
        // Other keys: the screen's (no focus: false; the focus "no" and Enter: its click, the answer).
        box->m_kind = MESSAGE_BOX_YES_NO;
        box->m_yesMessage = 0x71;
        box->m_noMessage = 0x72;
        okay &= !box->Unknown20(0x1C) && !box->Unknown20(0x02);
        WM(r.mbKeys, okay && ScQueue().empty() && box->m_kind == MESSAGE_BOX_YES_NO);
        box->m_focus = 2;
        okay &= box->Unknown20(0x9C);
        WM(r.mbKeys, okay && box->m_kind == MESSAGE_BOX_NONE && box->m_noMessage == 0 &&
                         ScQueue().find("0 2000097 0 0 ; 0 72 0 0 ; ") != std::string::npos);
        box->m_focus = -1;
    }

    // --- the buttons (by their index): yes and ok the first answer, no the second, others none;
    // closed, the answers cleared; none shown: true, nothing; no control: false ---
    {
        bool okay = true;
        const auto click = [&](C_Control *c, int kind, uint32_t yesMessage, uint32_t noMessage) {
            box->m_kind = kind;
            box->m_yesMessage = yesMessage;
            box->m_noMessage = noMessage;
            g_controlMessages.clear();
            return C_MessageBox::OnButton(box, box, c, CONTROL_NOTIFY_CLICK, 0) && box->m_kind == MESSAGE_BOX_NONE &&
                   box->m_yesMessage == 0 && box->m_noMessage == 0;
        };
        okay &= click(box->m_children[1], MESSAGE_BOX_YES_NO, 0x81, 0x82) && ScQueue() == "0 2000097 0 0 ; 0 81 0 0 ; ";
        okay &= click(box->m_children[2], MESSAGE_BOX_YES_NO, 0x81, 0x82) && ScQueue() == "0 2000097 0 0 ; 0 82 0 0 ; ";
        okay &= click(box->m_children[3], MESSAGE_BOX_OK, 0x83, 0x84) && ScQueue() == "0 2000097 0 0 ; 0 83 0 0 ; ";
        WM(r.mbButtons, okay);
        okay &= click(box->m_children[1], MESSAGE_BOX_TEXT, 0, 0x82) && ScQueue() == "0 2000097 0 0 ; ";
        okay &= click(box->m_children[2], MESSAGE_BOX_YES_NO_2, 0x81, 0) && ScQueue() == "0 2000097 0 0 ; ";
        okay &= click(box->m_children[3], MESSAGE_BOX_OK, 0, 0x84) && ScQueue() == "0 2000097 0 0 ; ";
        okay &= click(box->m_children[0], MESSAGE_BOX_YES_NO, 0x81, 0x82) && ScQueue() == "0 2000097 0 0 ; ";
        WM(r.mbButtons, okay);
        C_CtLogged five("five", 0x05500000), one("one", 0x7C100000), signedTop("top", 0x80200000);
        okay &= click(&five, MESSAGE_BOX_YES_NO, 0x81, 0x82) && ScQueue() == "0 2000097 0 0 ; ";
        okay &= click(&one, MESSAGE_BOX_YES_NO, 0x81, 0x82) && ScQueue() == "0 2000097 0 0 ; ";
        okay &= click(&signedTop, MESSAGE_BOX_YES_NO, 0x81, 0x82) && ScQueue() == "0 2000097 0 0 ; 0 81 0 0 ; ";
        WM(r.mbButtons, okay);
        box->m_kind = MESSAGE_BOX_NONE;
        box->m_yesMessage = 0x91;
        okay &= C_MessageBox::OnButton(box, box, box->m_children[1], CONTROL_NOTIFY_CLICK, 0) && ScQueue().empty() &&
                box->m_yesMessage == 0x91;
        box->m_kind = MESSAGE_BOX_YES_NO;
        okay &= !C_MessageBox::OnButton(box, box, nullptr, CONTROL_NOTIFY_CLICK, 0) && ScQueue().empty() &&
                box->m_kind == MESSAGE_BOX_YES_NO && box->m_yesMessage == 0x91;
        // Through the button's click.
        box->m_noMessage = 0x92;
        box->m_children[2]->Notify(CONTROL_NOTIFY_CLICK, 0);
        okay &= box->m_kind == MESSAGE_BOX_NONE && ScQueue() == "0 2000097 0 0 ; 0 92 0 0 ; ";
        WM(r.mbButtons, okay);
    }

    const int refs = g_csCursor.refs;
    delete box;
    WM(r.mbButtons, g_csCursor.refs == refs - 1);
    for (I3D_frame *f : {boxText, yes, no, ok})
        f->SetData(nullptr);

    // --- the description popup (part 9e-xiii): made (none shown); built from its model's frames (the
    // text a title with the screen's id | 2; the box off at its place, its second off; the description
    // not read: no name or id; without the frames: nothing made); deleted: the text ---
    {
        I3D_frame *model = GfFrame(FRAME_DUMMY, "DESCRIPTION");
        I3D_frame *text = GfVisual("text", I3DVISUAL_OBJECT, model);
        I3D_frame *frame = GfVisual("description", I3DVISUAL_OBJECT, model);
        I3D_frame *frame1 = GfVisual("description01", I3DVISUAL_OBJECT, model);
        // (Dummies of the names: not found.)
        I3D_frame *other = GfFrame(FRAME_DUMMY, "DESCRIPTION2");
        GfFrame(FRAME_DUMMY, "text", other);
        GfFrame(FRAME_DUMMY, "description", other);
        I3D_frame *other1 = GfFrame(FRAME_DUMMY, "description01", other);
        frame->m_frameFlags |= 1;
        frame1->m_frameFlags |= 1;
        other1->m_frameFlags |= 1;
        const int refs0 = g_csCursor.refs;
        g_gfLog.clear();
        auto *d = new C_DescriptionScreen(scene, model);
        WM(r.dsNew, !d->m_text && !d->m_boxShown && !d->m_shown && !d->HasPopup() && d->m_screenFrame == model &&
                        d->m_scene == scene && d->m_id == 0xFFFFFFFF && g_csCursor.refs == refs0 + 1 &&
                        GfLogIs({"cursor acquire 1"}));
        d->m_id = 0x0C000000;
        g_gfLog.clear();
        WM(r.dsBuild, d->Build(nullptr) && d->m_text && typeid(*d->m_text) == typeid(C_TitleControl) &&
                          d->m_text->m_frame == text && d->m_text->m_id == 0x0C000002 && d->m_text->m_parent == d &&
                          d->m_text->m_scene == scene && d->m_frame == frame && !frame->IsOn() && !frame1->IsOn() &&
                          d->m_id == 0x0C000000 && d->m_name.IsEmpty() && d->m_children.empty());
        const S_vector &pos = frame->GetPos();
        WM(r.dsBuild, pos.x == 0.5f && pos.y == -0.375f && pos.z == -0.5f);
        WM(r.dsBuild, g_gfLog.size() > 3 && g_gfLog[0] == "find DESCRIPTION text 1" &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "find DESCRIPTION description 1") == 1 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "on description 0") == 1 &&
                          g_gfLog[g_gfLog.size() - 2] == "find DESCRIPTION description01 1" &&
                          g_gfLog.back() == "on description01 0");
        auto *e = new C_DescriptionScreen(scene, other);
        g_gfLog.clear();
        WM(r.dsBuild, e->Build(nullptr) && !e->m_text && !e->m_frame && other1->IsOn() &&
                          GfLogIs({"find DESCRIPTION2 text 1", "find DESCRIPTION2 description 1",
                                   "find DESCRIPTION2 description01 1"}));
        delete e;
        // The text replaced by a logging one; deleted with the screen.
        text->SetData(nullptr);
        delete d->m_text;
        d->m_text = new C_DsTitle();

        // --- ticked: not shown, nothing (the box left on); shown, the cursor still 500 ms (its button
        // up): the box on at the cursor + (0.15, 0.015) at its own depth, placed, the text refreshed;
        // the cursor moving or its button down: the box off (when shown or on) ---
        bool ok = true;
        frame->m_frameFlags |= 1;
        g_csCursor.still = 900;
        g_csCursor.buttons = 0;
        g_csCursor.position = S_vector(0.25f, -0.5f, -1.0f);
        g_gfLog.clear();
        d->Tick(10);
        ok &= g_gfLog.empty() && frame->IsOn() && !d->HasPopup();
        frame->m_frameFlags &= ~1u;
        S_ControlMessage show = CtMessage(0, CONTROL_MSG_SHOW | 0x100000);
        ok &= d->HandleMessage(&show) && d->m_shown && !d->m_boxShown;
        GfPlace(frame, S_vector(0.5f, -0.375f, -0.75f));
        g_csCursor.still = 500;
        g_gfLog.clear();
        d->Tick(10);
        const S_vector &at = frame->GetPos();
        ok &= d->HasPopup() && frame->IsOn() && at.x == 0.25f + 0.15f && at.y == -0.5f + 0.015f && at.z == -0.75f;
        ok &= GfLogIs({"on description 1", "setpos description 0.4000 -0.4850 -0.7500", "update description",
                       "dstitle message 0 200002c"});
        WM(r.dsTick, ok);
        g_csCursor.still = 499;
        g_gfLog.clear();
        d->Tick(10);
        ok &= !d->HasPopup() && !frame->IsOn() && GfLogIs({"on description 0"});
        g_gfLog.clear();
        d->Tick(10);
        ok &= g_gfLog.empty();
        // The box on, not shown by the popup: off.
        frame->m_frameFlags |= 1;
        d->Tick(10);
        ok &= !frame->IsOn() && GfLogIs({"on description 0"});
        // Shown by the popup, the box off: off again.
        d->m_boxShown = true;
        g_csCursor.still = 600;
        g_csCursor.buttons = 1;
        g_gfLog.clear();
        d->Tick(10);
        ok &= !d->HasPopup() && GfLogIs({"on description 0"});
        // (Any button down.)
        g_csCursor.buttons = 2;
        g_gfLog.clear();
        d->Tick(10);
        ok &= !d->HasPopup() && g_gfLog.empty();
        frame->m_frameFlags |= 1;
        g_csCursor.buttons = 0;
        WM(r.dsTick, ok);

        // --- the messages: show and hide (the box's state too), true; the others the text's (its
        // answer); refreshed: the text's ---
        ok = true;
        S_ControlMessage hide = CtMessage(0, CONTROL_MSG_HIDE | 0x4000);
        g_gfLog.clear();
        ok &= d->HandleMessage(&hide) && !d->m_shown && !d->HasPopup() && g_gfLog.empty() && frame->IsOn();
        d->m_boxShown = true;
        ok &= d->HandleMessage(&show) && d->m_shown && d->HasPopup() && g_gfLog.empty();
        S_ControlMessage other2 = CtMessage(0x0C100000, CONTROL_MSG_SET_TEXT_ID);
        ok &= d->HandleMessage(&other2) && GfLogIs({"dstitle message c100000 1000015"});
        other2.id = CONTROL_MSG_REFRESH;
        g_gfLog.clear();
        ok &= !d->HandleMessage(&other2) && GfLogIs({"dstitle message c100000 200002c"});
        g_gfLog.clear();
        d->Refresh();
        ok &= GfLogIs({"dstitle refresh"});
        WM(r.dsMessages, ok);
        g_gfLog.clear();
        delete d;
        WM(r.dsNew, g_csCursor.refs == refs0 && GfLogIs({"dstitle deleted", "cursor release"}));
        g_csCursor.still = 0;
        g_csCursor.position = S_vector(0.0f, 0.0f, 0.0f);
    }
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("screens: scDescs %d scMenuDesc %d scNew %d scBuild %d scOpen %d scHide %d scTick %d scKeys %d scMessages %d "
                "mbBuild %d mbShow %d mbShowId %d mbClose %d mbKeys %d mbButtons %d dsNew %d dsBuild %d dsTick %d "
                "dsMessages %d\n",
                r.scDescs, r.scMenuDesc, r.scNew, r.scBuild, r.scOpen, r.scHide, r.scTick, r.scKeys, r.scMessages, r.mbBuild,
                r.mbShow, r.mbShowId, r.mbClose, r.mbKeys, r.mbButtons, r.dsNew, r.dsBuild, r.dsTick, r.dsMessages);
}
