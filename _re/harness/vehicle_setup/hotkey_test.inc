// Part 9c-v-d-i's controls (game/controls.cpp: the hot key and the titled edit box), in the text
// build, run by ControlsTest after ServerTest (its fonts, its screen, its cursor; ScMessages,
// CbRuns, CsRuns, CsVisual, CsNotified, g_texts' "text <id>"). Prints "hotkey:" with 1 per check met.

#include "game/key_bindings.h"

// The controls' key poll, the key bindings and the cursor's +0x44 (their modules not in the text
// build): logged.
C_Controls::S_Press g_hkPress;
bool g_hkPressed = false;
const C_Controls::S_Press *C_Controls::PollPress(int unknown, bool reread)
{
    GfLog("poll %x %d", unknown, reread);
    return g_hkPressed ? &g_hkPress : nullptr;
}
bool g_hkBound = true;
bool C_KeyBindings::Bind(int index, int device, int key, int direction)
{
    GfLog("bind %d %d %d %d", index, device, key, direction);
    return g_hkBound;
}
C_str C_KeyBindings::GetBindingName(int index) const { return C_fstr("key%d", index); }
void C_Cursor::SetDelay(int time) { GfLog("cursor 44 %d", time); }

namespace {

struct S_HotKeyResults {
    bool hkMake = true, hkParts = true, hkTick = true, hkKeys = true, hkMsgs = true, teMake = true;
    bool teParts = true, teMsgs = true;
};

// The parts as loggers ("<name> <slot> <id>"): the edit box editing or not, the results set.
int g_hkResult = 1;
struct C_HkEdit : C_EditBox {
    const char *name;
    bool editing = false;
    explicit C_HkEdit(const char *n) : C_EditBox(nullptr, 0, nullptr), name(n) {}
    ~C_HkEdit() override { GfLog("%s deleted", name); }
    int Log(const char *slot, int id)
    {
        GfLog("%s %s %x", name, slot, id);
        return g_hkResult;
    }
    bool IsEditing() override { return editing; }
    void Refresh() override { GfLog("%s refresh", name); }
    void Tick(int time) override { GfLog("%s tick %d", name, time); }
    bool Unknown20(int key) override { return Log("u20", key) != 0; }
    int OnEnter(int id) override { return Log("enter", id); }
    int OnLeave(int id) override { return Log("leave", id); }
    int OnClick(int id) override { return Log("click", id); }
    int OnInput02(int id) override { return Log("in02", id); }
    int OnKeyClick(int id) override { return Log("keyclick", id); }
    void SetInactive(bool inactive) override { GfLog("%s inactive %d", name, inactive); }
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("%s msg %x %x %x %s", name, m->id, m->value, static_cast<uint32_t>(m->value2), m->text.c_str());
        return true;
    }
};
struct C_HkTitle : C_TitleControl {
    const char *name;
    explicit C_HkTitle(const char *n) : C_TitleControl(nullptr, 0, nullptr), name(n) {}
    ~C_HkTitle() override { GfLog("%s deleted", name); }
    void Refresh() override { GfLog("%s refresh", name); }
    int OnEnter(int id) override
    {
        GfLog("%s enter %x", name, id);
        return 9;
    }
    int OnLeave(int id) override
    {
        GfLog("%s leave %x", name, id);
        return 9;
    }
    void SetInactive(bool inactive) override { GfLog("%s inactive %d", name, inactive); }
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("%s msg %x %x %x %s", name, m->id, m->value, static_cast<uint32_t>(m->value2), m->text.c_str());
        return true;
    }
};
// A control's frame for an edit box ("normal*", "actived*") and its title.
I3D_frame *HkFrame(const char *name, const char *title)
{
    I3D_frame *f = GfFrame(FRAME_DUMMY, name);
    CsVisual(CmText("normal%s", name).c_str(), f);
    CsVisual(CmText("actived%s", name).c_str(), f);
    if (title)
        CsVisual(title, f);
    return f;
}
template <class T> void HkParts(T &c, C_HkEdit *&edit, C_HkTitle *&title)
{
    delete c.m_edit;
    delete c.m_title;
    c.m_edit = edit = new C_HkEdit("ed");
    c.m_title = title = new C_HkTitle("ti");
}

} // namespace

void HotKeyTest(I3D_scene *scene)
{
    S_HotKeyResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();
    // A player for the bindings (only its address used).
    static C_PlayerProfile player;
    C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
    g_gameProfile.m_player = &player;

    // --- the hot key: an edit box on its frame, a title on "hktext*" (not children) ---
    {
        I3D_frame *frame = HkFrame("hk", "hktexthk");
        C_HotKeyControl h(scene, 0x1C100000, nullptr);
        WM(r.hkMake, !h.m_armed && h.m_binding == 0 && !h.m_title && !h.m_edit && h.m_takesInput);
        WM(r.hkMake, h.Create(frame, frame, nullptr) && h.m_edit && h.m_title && h.m_children.empty() &&
                         h.m_edit->GetId() == 0x1C100001 && h.m_title->GetId() == 0x1C100002 && h.m_edit->m_parent == &h &&
                         h.m_title->m_parent == &h && h.m_title->m_takesInput && h.m_edit->m_normalFrame &&
                         h.m_title->GetFrame() && std::string(GfName(h.m_title->GetFrame())) == "hktexthk" &&
                         CsRuns(*h.m_edit->m_box).empty());
        C_HkEdit *edit;
        C_HkTitle *title;
        HkParts(h, edit, title);
        h.m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        // Refreshed shown: both; hidden: none.
        g_gfLog.clear();
        h.Refresh();
        WM(r.hkParts, GfLogIs({"ed refresh", "ti refresh"}));
        h.m_shown = false;
        g_gfLog.clear();
        h.Refresh();
        WM(r.hkParts, g_gfLog.empty());
        h.m_shown = true;
        // Enter: both (handled); leave: both, not while editing.
        g_gfLog.clear();
        WM(r.hkParts, h.OnEnter(5) == CONTROL_HANDLED && GfLogIs({"ed enter 5", "ti enter 5"}));
        g_gfLog.clear();
        WM(r.hkParts, h.OnLeave(6) == CONTROL_HANDLED && GfLogIs({"ed leave 6", "ti leave 6"}));
        edit->editing = true;
        g_gfLog.clear();
        WM(r.hkParts, h.OnLeave(6) == 0 && g_gfLog.empty());
        // Click: not while editing; the edit box's, handled: DONE posted.
        g_controlMessages.clear();
        WM(r.hkParts, h.OnClick(7) == 0 && g_controlMessages.empty());
        edit->editing = false;
        g_gfLog.clear();
        WM(r.hkParts, h.OnClick(7) == CONTROL_HANDLED && GfLogIs({"ed click 7"}) && ScMessages() == "0 20000c2 0 0|");
        g_controlMessages.clear();
        g_hkResult = 5;
        WM(r.hkParts, h.OnClick(7) == 0 && g_controlMessages.empty());
        WM(r.hkParts, h.OnKeyClick(8) == 0 && g_controlMessages.empty());
        g_hkResult = 1;
        WM(r.hkParts, h.OnKeyClick(8) == CONTROL_HANDLED && ScMessages() == "0 20000c2 0 0|");
        // Input 02: the edit box's, handled: DONE posted (editing or not).
        g_controlMessages.clear();
        g_gfLog.clear();
        edit->editing = true;
        WM(r.hkParts, h.OnInput02(9) == CONTROL_HANDLED && GfLogIs({"ed in02 9"}) && ScMessages() == "0 20000c2 0 0|");
        g_controlMessages.clear();
        g_hkResult = 0;
        WM(r.hkParts, h.OnInput02(9) == 0 && g_controlMessages.empty());
        g_hkResult = 1;
        edit->editing = false;
        WM(r.hkParts, h.OnPress(1) == CONTROL_HANDLED);
        // Inactive: both.
        g_gfLog.clear();
        h.SetInactive(true);
        WM(r.hkParts, h.m_inactive && GfLogIs({"ed inactive 1", "ti inactive 1"}));
        h.SetInactive(false);
        // The keys: not editing, false; editing, true (Escape, its low byte: the edit ended).
        g_gfLog.clear();
        WM(r.hkKeys, !h.Unknown20(1) && g_gfLog.empty());
        edit->editing = true;
        WM(r.hkKeys, h.Unknown20(2) && g_gfLog.empty());
        WM(r.hkKeys, h.Unknown20(1) && GfLogIs({"ed in02 0"}));
        g_gfLog.clear();
        WM(r.hkKeys, h.Unknown20(0x201) && GfLogIs({"ed in02 0"}));
        // The tick: not editing, nothing; editing: the edit box's, then armed (no poll).
        edit->editing = false;
        g_gfLog.clear();
        h.Tick(16);
        WM(r.hkTick, g_gfLog.empty() && !h.m_armed);
        edit->editing = true;
        h.SetBinding(4);
        WM(r.hkTick, h.m_binding == 4);
        h.Tick(16);
        WM(r.hkTick, GfLogIs({"ed tick 16"}) && h.m_armed);
        // Nothing pressed.
        g_gfLog.clear();
        h.Tick(17);
        WM(r.hkTick, GfLogIs({"ed tick 17", "poll 9a 1"}) && h.m_armed);
        // Escape: none bound, the edit ended, DONE.
        g_hkPressed = true;
        g_hkPress = {0, 1, 1, 0};
        g_gfLog.clear();
        g_controlMessages.clear();
        h.Tick(18);
        WM(r.hkTick, GfLogIs({"ed tick 18", "poll 9a 1", "ed in02 0"}) && !h.m_armed && ScMessages() == "0 20000c2 0 0|");
        // Escape's key on another device, or another key: bound.
        h.m_armed = true;
        g_hkPress = {2, 2, 1, 0};
        g_gfLog.clear();
        g_controlMessages.clear();
        h.Tick(19);
        WM(r.hkTick, GfLogIs({"ed tick 19", "poll 9a 1", "bind 4 2 1 2", "notify 4010000 0", "ed in02 0", "cursor acquire 0",
                              "cursor 44 200", "cursor release"}) &&
                         !h.m_armed && ScMessages() == "0 20000c2 0 0|");
        h.m_armed = true;
        g_hkPress = {1, 1, 0x1E, 0};
        g_gfLog.clear();
        h.Tick(20);
        WM(r.hkTick, std::find(g_gfLog.begin(), g_gfLog.end(), "bind 4 1 30 1") != g_gfLog.end() && !h.m_armed);
        // Refused: nothing more, still armed.
        h.m_armed = true;
        g_hkBound = false;
        g_gfLog.clear();
        g_controlMessages.clear();
        h.Tick(21);
        WM(r.hkTick, GfLogIs({"ed tick 21", "poll 9a 1", "bind 4 1 30 1"}) && h.m_armed && g_controlMessages.empty());
        g_hkBound = true;
        g_hkPressed = false;
        edit->editing = false;
        // Messages: the binding got (both codes); set: the title's text id, the key's name.
        S_ControlMessage m = CsMessage(CONTROL_MSG_GET_BINDING);
        WM(r.hkMsgs, h.HandleMessage(&m) && m.value == 4);
        m = CsMessage(CONTROL_MSG_GET_BINDING2);
        WM(r.hkMsgs, h.HandleMessage(&m) && m.value == 4);
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_BINDING, 7, 0x55);
        WM(r.hkMsgs, h.HandleMessage(&m) && h.m_binding == 7 && GfLogIs({"ti msg 1000015 c 55 ", "ed msg 1000014 d 0 key7"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_REFRESH, 3);
        WM(r.hkMsgs, h.HandleMessage(&m) && GfLogIs({"ti msg 200002c 3 0 ", "ed msg 200002c 3 0 ", "ed msg 1000014 d 0 key7"}));
        // Notifications and others: the base's (true).
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_BINDING, 0, 0, nullptr, 0x1C100000);
        WM(r.hkMsgs, h.HandleMessage(&m) && h.m_routes.size() == 1 && h.m_binding == 7);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0x1C300000);
        WM(r.hkMsgs, h.HandleMessage(&m) && h.m_value == 0x1C300000);
        m = CsMessage(CONTROL_MSG_HIDE);
        WM(r.hkMsgs, h.HandleMessage(&m) && !h.m_shown);
        h.m_frame = nullptr;
        // Deleted with its parts.
        g_gfLog.clear();
        h.~C_HotKeyControl();
        new (&h) C_HotKeyControl(scene, 0, nullptr);
        WM(r.hkParts, GfLogIs({"ed deleted", "ti deleted"}));
        // No title frame: none.
        I3D_frame *bare = HkFrame("hb", nullptr);
        C_HotKeyControl b(scene, 0x1C200000, nullptr);
        WM(r.hkMake, b.Create(bare, bare, nullptr) && b.m_edit && !b.m_title);
    }

    // --- the titled edit box: the edit box's text 0x7D2, the title on "text*" ---
    {
        I3D_frame *frame = HkFrame("te", "textte");
        C_TitledEditBox t(scene, 0x1C100000, nullptr);
        WM(r.teMake, t.Create(frame, frame, nullptr) && t.m_edit && t.m_title && t.m_children.empty() &&
                         t.m_edit->GetId() == 0x1C100001 && t.m_title->GetId() == 0x1C100002 && t.m_title->m_takesInput &&
                         std::string(GfName(t.m_title->GetFrame())) == "textte" && CsRuns(*t.m_edit->m_box) == "text 2002/13 ");
        C_HkEdit *edit;
        C_HkTitle *title;
        HkParts(t, edit, title);
        // The tick: editing only.
        g_gfLog.clear();
        t.Tick(5);
        WM(r.teParts, g_gfLog.empty());
        edit->editing = true;
        t.Tick(6);
        WM(r.teParts, GfLogIs({"ed tick 6"}) && !t.m_armed);
        // The keys, the click, input 02 and the key click: the edit box's (editing or not).
        g_gfLog.clear();
        g_hkResult = 0;
        WM(r.teParts, !t.Unknown20(1) && t.OnClick(2) == 0 && t.OnInput02(3) == 0 && t.OnKeyClick(4) == 0);
        g_hkResult = 6;
        WM(r.teParts, t.Unknown20(1) && t.OnClick(2) == 6 && t.OnInput02(3) == 6 && t.OnKeyClick(4) == 6);
        WM(r.teParts, std::count(g_gfLog.begin(), g_gfLog.end(), "ed in02 3") == 2 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "ed keyclick 4") == 2 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "ed u20 1") == 2);
        g_hkResult = 1;
        WM(r.teParts, g_controlMessages.empty());
        // Leave: both, editing too.
        g_gfLog.clear();
        WM(r.teParts, t.OnLeave(7) == CONTROL_HANDLED && GfLogIs({"ed leave 7", "ti leave 7"}) && t.OnPress(1) == CONTROL_HANDLED);
        edit->editing = false;
        // Messages: the text ones the title's; the edit ones the edit box's; REFRESH both.
        g_gfLog.clear();
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_TEXT, 2, 0, "Port");
        WM(r.teMsgs, t.HandleMessage(&m) && GfLogIs({"ti msg 1000014 2 0 Port"}));
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 2, 9);
        WM(r.teMsgs, t.HandleMessage(&m) && GfLogIs({"ti msg 1000015 2 9 "}));
        const uint32_t editCodes[] = {CONTROL_MSG_ADD_CALLBACK, CONTROL_MSG_SET_MAX_LENGTH, CONTROL_MSG_SET_DIGITS_ONLY,
                                      CONTROL_MSG_SET_NUMBER,   CONTROL_MSG_GET_NUMBER,     CONTROL_MSG_SET_FORBIDDEN,
                                      CONTROL_MSG_SET_RANGE,    CONTROL_MSG_GET_TEXT};
        for (uint32_t code : editCodes) {
            g_gfLog.clear();
            m = CsMessage(code, 1);
            WM(r.teMsgs, t.HandleMessage(&m) && GfLogIs({CmText("ed msg %x 1 0 ", code)}));
        }
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        WM(r.teMsgs, t.HandleMessage(&m) && GfLogIs({"ti msg 200002c 0 0 ", "ed msg 200002c 0 0 "}));
        // Notifications and others: the base's (true); the hot key's own not taken.
        g_gfLog.clear();
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_TEXT, 0, 0, nullptr, 0x1C100000);
        WM(r.teMsgs, t.HandleMessage(&m) && t.m_routes.size() == 1 && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_SET_BINDING, 3, 4);
        WM(r.teMsgs, t.HandleMessage(&m) && t.m_binding == 0 && g_gfLog.empty());
        m = CsMessage(CONTROL_MSG_GET_BINDING);
        WM(r.teMsgs, t.HandleMessage(&m) && m.value == 0);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6);
        WM(r.teMsgs, t.HandleMessage(&m) && t.m_index == 6 && g_gfLog.empty());
        // The hot key's enter and inactive.
        g_gfLog.clear();
        WM(r.teParts, t.OnEnter(8) == CONTROL_HANDLED && GfLogIs({"ed enter 8", "ti enter 8"}));
        t.m_frame = nullptr;
    }

    g_gameProfile.m_player = oldPlayer;
    WM(r.teMsgs, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("hotkey: hkMake %d hkParts %d hkTick %d hkKeys %d hkMsgs %d teMake %d teParts %d teMsgs %d\n", r.hkMake,
                r.hkParts, r.hkTick, r.hkKeys, r.hkMsgs, r.teMake, r.teParts, r.teMsgs);
}
