// Part 9c-iv-a's controls (game/controls.cpp: the edit control, the edit box, the selection list), in
// the text build, run by ControlsTest after CheckTest (its fonts, its screen, its cursor; ScMessages,
// CkId). Prints "edit:" with 1 per check met.

namespace {

struct S_EditResults {
    bool edBase = true, edChar = true, edUtf8 = true, edEditing = true, edMsgs = true, edBox = true;
    bool edBoxRange = true, edList = true, edListInput = true, edListMsgs = true;
};

std::string EdBuffer(const C_EditControl &e)
{
    return e.m_buffer.empty() ? std::string("<none>") : std::string(e.m_buffer.data());
}
std::string EdRun(const C_TextBox &box, int index)
{
    return index >= 0 && index < box.NumRuns() ? std::string(box.m_runs[index].text.c_str()) : std::string("<none>");
}
std::string EdPtr(const void *p)
{
    return CmText("%u", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

} // namespace

void EditTest(I3D_scene *scene)
{
    S_EditResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();
    const bool unicode = g_gameProfile.m_unicodeFont;
    g_gameProfile.m_unicodeFont = false;

    // --- the edit control: its buffer (30 + 1), the box made, not placed ---
    {
        I3D_frame *frame = CsVisual("edframe");
        auto *e = new C_EditControl(scene, 0x80100000, nullptr);
        WM(r.edBase, e->m_maxLength == 30 && e->m_run == -1 && !e->m_editing && !e->m_digitsOnly && !e->m_forbidden &&
                         e->m_buffer.empty() && !e->IsEditing());
        WM(r.edBase, e->Create(frame, frame, nullptr) && e->m_box && e->m_buffer.size() == 31 && EdBuffer(*e).empty() &&
                         !e->m_box->m_placed && frame->GetData() == CkId(0x80100000));
        // The refresh: the box only (shown).
        delete e->m_box;
        e->m_box = new C_CsBox("ed");
        e->m_children.push_back(new C_CsChild("ek", 0));
        g_gfLog.clear();
        e->Refresh();
        WM(r.edBase, GfLogIs({"box ed render"}));
        e->m_shown = false;
        g_gfLog.clear();
        e->Refresh();
        WM(r.edBase, g_gfLog.empty());
        e->m_shown = true;
        C_TextBox &box = *e->m_box;
        // Not editing: no caret, no characters, the stop and the end nothing.
        box.m_caretTime = 7;
        e->Tick(5);
        WM(r.edBase, box.m_caretTime == 7 && !e->OnChar('a') && EdBuffer(*e).empty());
        e->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.edBase, e->OnLeave(1) == CONTROL_NOT_HANDLED && GfLogIs({"notify 4004000 0"}));
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.edBase, e->OnInput02(1) == 0 && e->OnInput0B(1) == 0 && g_controlMessages.empty() && g_gfLog.empty());
        // The click asks the screen for the editing.
        WM(r.edBase, e->OnClick(1) == CONTROL_HANDLED && ScMessages() == "0 20000bf 0 " + EdPtr(e) + "|");
        g_controlMessages.clear();

        // The editing: the run's text kept and in the buffer; the caret ticked.
        box.SetText(C_str("abc"), 3, 0);
        e->m_run = 0;
        WM(r.edEditing, e->BeginEdit(0) == CONTROL_HANDLED && e->m_editing && e->IsEditing() && box.m_editing &&
                            box.m_selection == 0 && std::string(e->m_savedText.c_str()) == "abc" && EdBuffer(*e) == "abc");
        WM(r.edEditing, e->BeginEdit(0) == CONTROL_NOT_HANDLED);
        box.m_caretTime = 0;
        e->Tick(5);
        WM(r.edEditing, box.m_caretTime == 5);
        WM(r.edEditing, e->OnClick(1) == 0 && g_controlMessages.empty());

        // The characters: appended and shown; Backspace; Tab and 0 not taken.
        WM(r.edChar, e->OnChar('d') && EdBuffer(*e) == "abcd" && EdRun(box, 0) == "abcd");
        WM(r.edChar, e->OnChar(8) && EdBuffer(*e) == "abc" && EdRun(box, 0) == "abc");
        WM(r.edChar, !e->OnChar(9) && !e->OnChar(0) && EdBuffer(*e) == "abc");
        // A '%' kept as is (the fix).
        WM(r.edChar, e->OnChar('%') && EdRun(box, 0) == "abc%" && e->OnChar(8));
        // Refused characters.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_FORBIDDEN, 0, reinterpret_cast<uintptr_t>("xy"));
        handled &= e->HandleMessage(&m);
        WM(r.edChar, e->OnChar('x') && e->OnChar('y') && EdBuffer(*e) == "abc" && e->OnChar('z') && EdBuffer(*e) == "abcz");
        // (A low byte of 0: taken with refused characters, strchr's terminator; else not.)
        WM(r.edChar, e->OnChar(0x100) && EdBuffer(*e) == "abcz");
        m = CsMessage(CONTROL_MSG_SET_FORBIDDEN);
        handled &= e->HandleMessage(&m);
        WM(r.edChar, !e->OnChar(0x100) && !e->m_forbidden);
        // The length: a new, empty buffer (the same or 0: kept); full: refused, still taken.
        m = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 4);
        handled &= e->HandleMessage(&m);
        WM(r.edChar, e->m_maxLength == 4 && e->m_buffer.size() == 5 && EdBuffer(*e).empty());
        for (char c : std::string("abcde"))
            e->OnChar(c);
        WM(r.edChar, EdBuffer(*e) == "abcd" && EdRun(box, 0) == "abcd" && e->OnChar('f') && EdBuffer(*e) == "abcd");
        m = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 4);
        handled &= e->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 0);
        handled &= e->HandleMessage(&m);
        WM(r.edChar, e->m_maxLength == 4 && EdBuffer(*e) == "abcd");
        // Digits only: a '-' first, digits; full-width digits as ASCII.
        m = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 10);
        handled &= e->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_DIGITS_ONLY, 5);
        handled &= e->HandleMessage(&m);
        WM(r.edChar, e->m_digitsOnly && e->OnChar('-') && e->OnChar('5') && e->OnChar('-') && e->OnChar('a') &&
                         EdBuffer(*e) == "-5");
        WM(r.edChar, e->OnChar(0xFF17) && e->OnChar(0xFF10) && e->OnChar(0xFF19) && EdBuffer(*e) == "-5709");
        WM(r.edChar, e->OnChar(0xFF0F) && e->OnChar(0xFF1A) && EdBuffer(*e) == "-5709");
        m = CsMessage(CONTROL_MSG_SET_DIGITS_ONLY, 0);
        handled &= e->HandleMessage(&m);
        // Without the Unicode font: the low byte.
        WM(r.edChar, !e->m_digitsOnly && e->OnChar(0x141) && e->OnChar(0xE9) && EdBuffer(*e) == "-5709A\xE9");
        WM(r.edChar, e->OnChar(8) && e->OnChar(8) && EdBuffer(*e) == "-5709");
        // An empty buffer's Backspace: nothing, taken.
        m = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 12);
        handled &= e->HandleMessage(&m);
        WM(r.edChar, e->OnChar(8) && EdBuffer(*e).empty());

        // The Unicode font: above 0x7f as UTF-8 (within the length); Backspace by sequence.
        g_gameProfile.m_unicodeFont = true;
        WM(r.edUtf8, e->OnChar(0xE9) && e->OnChar(0x20AC) && e->OnChar('a') && EdBuffer(*e) == "\xC3\xA9\xE2\x82\xAC" "a" &&
                         EdRun(box, 0) == "\xC3\xA9\xE2\x82\xAC" "a");
        WM(r.edUtf8, e->OnChar(8) && EdBuffer(*e) == "\xC3\xA9\xE2\x82\xAC" && e->OnChar(8) && EdBuffer(*e) == "\xC3\xA9" &&
                         e->OnChar(8) && EdBuffer(*e).empty());
        WM(r.edUtf8, e->OnChar(0x10348) && EdBuffer(*e) == "\xF0\x90\x8D\x88" && e->OnChar(8) && EdBuffer(*e).empty());
        // The length: a sequence that would reach it refused.
        for (int i = 0; i < 5; ++i)
            e->OnChar(0x20AC);
        WM(r.edUtf8, std::strlen(e->m_buffer.data()) == 9 && e->OnChar('b') && e->OnChar('c') && std::strlen(e->m_buffer.data()) == 11);
        WM(r.edUtf8, e->OnChar(0xE9) && std::strlen(e->m_buffer.data()) == 11);
        // Broken sequences: not removed (the fix too: back to the start).
        std::strcpy(e->m_buffer.data(), "a\x80");
        WM(r.edUtf8, e->Backspace() == false && EdBuffer(*e) == "a\x80");
        std::strcpy(e->m_buffer.data(), "\x80\x80");
        WM(r.edUtf8, e->Backspace() == false && EdBuffer(*e) == "\x80\x80");
        std::strcpy(e->m_buffer.data(), "\xE2\x82");
        WM(r.edUtf8, e->Backspace() == false);
        std::strcpy(e->m_buffer.data(), "\xF8\x80\x80\x80");
        WM(r.edUtf8, e->Backspace() == false);
        std::strcpy(e->m_buffer.data(), "x\xF7\x80\x80\x80");
        WM(r.edUtf8, e->Backspace() && EdBuffer(*e) == "x");
        std::strcpy(e->m_buffer.data(), "x\xC0\x80\x80");
        WM(r.edUtf8, e->Backspace() == false);
        std::strcpy(e->m_buffer.data(), "\xC0\x80");
        WM(r.edUtf8, e->Backspace() && EdBuffer(*e).empty());
        g_gameProfile.m_unicodeFont = false;
        std::strcpy(e->m_buffer.data(), "\xC3\xA9");
        WM(r.edUtf8, e->Backspace() && EdBuffer(*e) == "\xC3");

        // Enter: notified, the stop asked for (still editing); Escape: the kept text back too.
        std::strcpy(e->m_buffer.data(), "abc");
        e->OnChar('q');
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.edEditing, e->OnChar(0xD) && e->m_editing && GfLogIs({"notify 4016000 0", "notify 4002000 0"}) &&
                            ScMessages() == "0 20000c0 0 " + EdPtr(e) + "|");
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.edEditing, EdRun(box, 0) == "abcq" && e->OnChar(0x1B) && EdRun(box, 0) == "abc" &&
                            GfLogIs({"notify 4017000 0", "notify 4002000 0"}) && ScMessages() == "0 20000c0 0 " + EdPtr(e) + "|");
        // The stop: the box's editing ended, notified with the id.
        g_gfLog.clear();
        WM(r.edEditing, e->OnInput0B(0x80100000) == CONTROL_HANDLED && !e->m_editing && !box.m_editing &&
                            GfLogIs({"notify 400b000 2148532224"}));
        // No run: the text cleared (a run made), the editing of none.
        e->m_run = 5;
        WM(r.edEditing, e->BeginEdit(0) == CONTROL_HANDLED && box.NumRuns() == 1 && EdRun(box, 0).empty() && !box.m_editing &&
                            e->m_editing && EdBuffer(*e).empty() && CsRuns(box) == "/0 ");
        e->OnInput0B(1);
        e->m_run = 0;

        // The messages: the text got (the box's run 0); EDIT the click; END_EDIT while editing.
        box.SetText(C_str("hello"), 2, 0);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 0, 0, "old");
        handled &= e->HandleMessage(&m);
        WM(r.edMsgs, std::string(m.text.c_str()) == "hello" && !box.m_editing && box.m_selection == -1);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_EDIT);
        handled &= e->HandleMessage(&m);
        WM(r.edMsgs, ScMessages() == "0 20000bf 0 " + EdPtr(e) + "|");
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_END_EDIT);
        handled &= e->HandleMessage(&m);
        WM(r.edMsgs, g_controlMessages.empty());
        e->BeginEdit(0);
        handled &= e->HandleMessage(&m);
        WM(r.edMsgs, ScMessages() == "0 20000c0 0 " + EdPtr(e) + "|");
        // (GET_TEXT while editing: the box's editing ended too, the original's.)
        m = CsMessage(CONTROL_MSG_GET_TEXT);
        handled &= e->HandleMessage(&m);
        WM(r.edMsgs, std::string(m.text.c_str()) == "hello" && !box.m_editing && e->m_editing);
        e->OnInput0B(1);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_IGNORED_74, 3);
        handled &= e->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "t");
        handled &= e->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 4);
        handled &= e->HandleMessage(&m);
        WM(r.edMsgs, EdRun(box, 0) == "t" && e->m_index == 4 && g_controlMessages.empty());
        // The caret only while editing (the box's not touched otherwise).
        box.m_caretOn = true;
        e->Tick(5);
        WM(r.edEditing, box.m_caretOn);
        // An empty buffer's Backspace: true, nothing shown.
        e->BeginEdit(0);
        box.m_runs[0].text = C_str("keep");
        std::fill(e->m_buffer.begin(), e->m_buffer.end(), '\0');
        WM(r.edChar, e->Backspace() && EdRun(box, 0) == "keep");
        WM(r.edChar, e->OnChar(1) && EdBuffer(*e) == "\x01" && e->OnChar(8) && EdBuffer(*e).empty());
        // A sequence's bytes left behind by the Backspace: the next character ends the text.
        g_gameProfile.m_unicodeFont = true;
        WM(r.edUtf8, e->OnChar(0xE9) && EdRun(box, 0) == "\xC3\xA9" && e->OnChar(8) && EdBuffer(*e).empty());
        g_gameProfile.m_unicodeFont = false;
        WM(r.edUtf8, e->OnChar('a') && EdBuffer(*e) == "a");
        // 0x7F as ASCII with the Unicode font too (digits only refuses it).
        S_ControlMessage m2 = CsMessage(CONTROL_MSG_SET_DIGITS_ONLY, 1);
        handled &= e->HandleMessage(&m2);
        g_gameProfile.m_unicodeFont = true;
        WM(r.edUtf8, e->OnChar(0x7F) && EdBuffer(*e) == "a");
        // Leads just past their sequences' ranges.
        std::strcpy(e->m_buffer.data(), "x\xE0\x80");
        WM(r.edUtf8, !e->Backspace());
        std::strcpy(e->m_buffer.data(), "x\xF0\x80\x80");
        WM(r.edUtf8, !e->Backspace());
        g_gameProfile.m_unicodeFont = false;
        m2 = CsMessage(CONTROL_MSG_SET_DIGITS_ONLY, 0);
        handled &= e->HandleMessage(&m2);
        e->OnInput0B(1);
        // A text as long as the buffer: ended in it.
        m2 = CsMessage(CONTROL_MSG_SET_MAX_LENGTH, 3);
        handled &= e->HandleMessage(&m2);
        box.SetText(C_str("abc"), 2, 0);
        WM(r.edEditing, e->BeginEdit(0) == CONTROL_HANDLED && EdBuffer(*e) == "abc");
        e->OnInput0B(1);
        // The box already editing: begun, not its run's (0).
        box.m_editing = true;
        WM(r.edEditing, e->BeginEdit(0) == 0 && e->m_editing);
        box.m_editing = false;
        e->OnInput0B(1);
        e->m_anyCallback = S_ControlCallback();
        delete e;
        frame->SetData(nullptr);
    }

    // --- the edit box: its frames (the active one at the normal's place), the box on the normal
    // one, the run 0 ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "ebframe");
        I3D_frame *normal = CsVisual("normaleb", frame);
        I3D_frame *active = GfVisual("activedeb", I3DVISUAL_OBJECT, frame);
        GfPlace(normal, S_vector(0.1f, 0.2f, 0.3f));
        auto *b = new C_EditBox(scene, 0x81100000, nullptr);
        WM(r.edBox, !b->m_hasRange && b->m_min == 0 && b->m_max == 0 && b->m_takesInput);
        g_gfLog.clear();
        WM(r.edBox, b->Create(frame, frame, nullptr) && b->m_normalFrame == normal && b->m_activeFrame == active && b->m_run == 0 &&
                        CsPlaced(*b->m_box, 10));
        WM(r.edBox, GfLogIs({"find ebframe normal* 10001", "find ebframe actived* 10001", "on activedeb 0", "update activedeb"}) &&
                        active->GetPos() == S_vector(0.1f, 0.2f, 0.3f) && normal->GetData() == CkId(0x81100000) &&
                        active->GetData() == CkId(0x81100000));
        C_TextBox &box = *b->m_box;
        b->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_gfLog.clear();
        WM(r.edBox, b->OnEnter(1) == CONTROL_HANDLED &&
                        GfLogIs({"on activedeb 1", "on normaleb 0", "update activedeb", "update normaleb", "notify 4003000 0"}));
        g_gfLog.clear();
        WM(r.edBox, b->OnLeave(1) == CONTROL_NOT_HANDLED &&
                        GfLogIs({"notify 4004000 0", "on normaleb 1", "on activedeb 0", "update normaleb", "update activedeb",
                                 "notify 4004000 0"}));
        // Inactive: the enter and the click nothing; the key click only for its own id.
        b->m_inactive = true;
        g_gfLog.clear();
        g_controlMessages.clear();
        WM(r.edBox, b->OnEnter(1) == 0 && b->OnClick(1) == 0 && b->OnKeyClick(0x81100000) == 0 && g_gfLog.empty() &&
                        g_controlMessages.empty());
        b->m_inactive = false;
        WM(r.edBox, b->OnClick(1) == CONTROL_HANDLED && ScMessages() == "0 20000bf 0 " + EdPtr(b) + "|");
        g_controlMessages.clear();
        WM(r.edBox, b->OnKeyClick(0x81100001) == 0 && g_controlMessages.empty());
        WM(r.edBox, b->OnKeyClick(0x81100000) == CONTROL_HANDLED && ScMessages() == "0 20000bf 0 " + EdPtr(b) + "|");
        g_controlMessages.clear();
        b->BeginEdit(0);
        g_gfLog.clear();
        WM(r.edBox, b->OnKeyClick(0x81100000) == CONTROL_HANDLED && ScMessages() == "0 20000c0 0 " + EdPtr(b) + "|" &&
                        GfLogIs({"notify 4002000 0"}));
        b->OnInput0B(1);
        // Its stop: the box's hover cleared and selected.
        g_controlMessages.clear();
        b->BeginEdit(0);
        box.m_editing = false;
        box.m_hover = 0;
        box.m_selection = 3;
        WM(r.edBox, b->OnInput02(1) == CONTROL_HANDLED && box.m_hover == -1 && box.m_selection == -1);
        WM(r.edBox, b->OnInput0B(1) == CONTROL_HANDLED && b->OnInput02(1) == 0 && b->OnInput0B(1) == 0);

        // A range (digits): the number kept in it when the editing ends (both ways); the same ends:
        // none.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_RANGE, 1, 10);
        handled &= b->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_DIGITS_ONLY, 1);
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, b->m_hasRange && b->m_min == 1 && b->m_max == 10);
        box.SetText(C_str(""), 0, 0);
        b->BeginEdit(0);
        b->OnChar('2');
        b->OnChar('5');
        WM(r.edBoxRange, b->OnInput02(1) == CONTROL_HANDLED && CsRuns(box) == "10/13 ");
        WM(r.edBoxRange, b->OnInput0B(1) == CONTROL_HANDLED && CsRuns(box) == "10/13 " && !b->m_editing);
        box.SetText(C_str("-3"), 2, 0);
        b->BeginEdit(0);
        WM(r.edBoxRange, b->OnInput0B(1) == CONTROL_HANDLED && CsRuns(box) == "1/13 ");
        box.SetText(C_str("7"), 2, 0);
        b->BeginEdit(0);
        WM(r.edBoxRange, b->OnInput0B(1) == CONTROL_HANDLED && CsRuns(box) == "7/2 ");
        box.SetText(C_str("x"), 2, 0);
        b->BeginEdit(0);
        WM(r.edBoxRange, b->OnInput0B(1) == CONTROL_HANDLED && CsRuns(box) == "1/13 ");
        box.SetText(C_str("10"), 2, 0);
        b->BeginEdit(0);
        WM(r.edBoxRange, b->OnInput0B(1) == CONTROL_HANDLED && CsRuns(box) == "10/2 ");
        m = CsMessage(CONTROL_MSG_SET_RANGE, 5, 5);
        handled &= b->HandleMessage(&m);
        box.SetText(C_str("99"), 2, 0);
        b->BeginEdit(0);
        WM(r.edBoxRange, !b->m_hasRange && b->OnInput0B(1) == CONTROL_HANDLED && CsRuns(box) == "99/2 ");
        // The number set (digits: value2's) and got.
        m = CsMessage(CONTROL_MSG_SET_NUMBER, 3, 42, "zz");
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, CsRuns(box) == "42/3 " && m.id == CONTROL_MSG_SET_TEXT);
        m = CsMessage(CONTROL_MSG_GET_NUMBER, 0, 7);
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, m.value2 == 42 && m.id == CONTROL_MSG_GET_NUMBER && std::string(m.text.c_str()) == "42");
        box.SetText(C_str("-8z"), 2, 0);
        m = CsMessage(CONTROL_MSG_GET_NUMBER, 0, 7);
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, static_cast<int>(m.value2) == -8);
        box.SetText(C_str("x"), 2, 0);
        m = CsMessage(CONTROL_MSG_GET_NUMBER, 0, 7);
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, m.value2 == 0);
        m = CsMessage(CONTROL_MSG_SET_DIGITS_ONLY, 0);
        handled &= b->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_NUMBER, 4, 42, "ab");
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, CsRuns(box) == "ab/4 ");
        m = CsMessage(CONTROL_MSG_GET_NUMBER, 0, 7);
        handled &= b->HandleMessage(&m);
        WM(r.edBoxRange, m.value2 == 7 && m.id == CONTROL_MSG_GET_NUMBER && !m.text.c_str()[0]);
        // REFRESH and SET_MARGIN on the normal frame.
        box.m_placed = false;
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= b->HandleMessage(&m);
        WM(r.edBox, CsPlaced(box, 10));
        m = CsMessage(CONTROL_MSG_SET_MARGIN, 3);
        handled &= b->HandleMessage(&m);
        WM(r.edBox, CsPlaced(box, 3));
        m = CsMessage(CONTROL_MSG_SET_INDEX, 2);
        handled &= b->HandleMessage(&m);
        WM(r.edBox, b->m_index == 2);
        b->m_anyCallback = S_ControlCallback();
        delete b;
        // A frame missing: false.
        I3D_frame *bare = GfFrame(FRAME_DUMMY, "ebbare");
        CsVisual("normalebb", bare);
        C_EditBox n(scene, 0, nullptr);
        WM(r.edBox, !n.Create(bare, bare, nullptr) && n.m_normalFrame && !n.m_activeFrame);
        I3D_frame *bare2 = GfFrame(FRAME_DUMMY, "ebbare2");
        GfVisual("activedebc", I3DVISUAL_OBJECT, bare2);
        C_EditBox n2(scene, 0, nullptr);
        WM(r.edBox, !n2.Create(bare2, bare2, nullptr) && !n2.m_normalFrame && n2.m_activeFrame);
        for (I3D_frame *c = frame->m_firstChild; c; c = c->m_nextSibling)
            c->SetData(nullptr);
    }

    // --- the selection list: the cursor; its frame turned to the back, the select frame to the
    // front (off, linked to the root); the box placed ---
    {
        I3D_frame *root = GfFrame(FRAME_DUMMY, "slroot");
        I3D_frame *frame = CsVisual("slframe");
        I3D_frame *select = GfVisual("selectsl", I3DVISUAL_OBJECT, frame);
        auto *s = new C_SelectList(scene, 0x90100000, nullptr);
        WM(r.edList, s->m_selected == -1 && s->m_unknown90 && !s->m_cursor && !s->m_selectFrame);
        const int refs = g_csCursor.refs;
        g_gfLog.clear();
        WM(r.edList, s->Create(frame, root, nullptr) && s->m_cursor && g_csCursor.refs == refs + 1 && s->m_selectFrame == select &&
                         CsPlaced(*s->m_box, 10));
        WM(r.edList, GfLogIs({"cursor acquire 1", "setdir slframe 0.00000 0.00000 -1.00000 0.000000", "update slframe",
                              "find slframe select* 10001", "setdir selectsl 0.00000 0.00000 1.00000 0.000000", "on selectsl 0",
                              "link selectsl slroot 1", "update selectsl"}) &&
                         select->m_parent == root && select->GetData() == CkId(0x90100000) &&
                         frame->GetData() == CkId(0x90100000));
        C_TextBox &box = *s->m_box;
        // Lines added (each a refresh: the range posted to the control the value names).
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_VALUE, 0x90200000);
        handled &= s->HandleMessage(&m);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "one");
        handled &= s->HandleMessage(&m);
        const int range1 = box.GetScrollRange();
        WM(r.edListMsgs, s->m_value == 0x90200000 && CsRuns(box) == "one/3 " &&
                             ScMessages() == CmText("90200000 1000020 %d 0|", range1) +
                                                 (range1 ? CmText("90200000 200002b %d 0|", box.GetScroll()) : std::string()));
        m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, "two");
        handled &= s->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 4, 42);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, CsRuns(box) == "one/3 two/3 text 42/4 ");
        // The runs; a line by index (selected), out of range: -1; the selected's text.
        m = CsMessage(CONTROL_MSG_GET_RUNS);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 3);
        m = CsMessage(CONTROL_MSG_GET_ITEM, 1, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 1 && std::string(m.text.c_str()) == "two" && s->m_selected == 1);
        m = CsMessage(CONTROL_MSG_GET_ITEM, 3, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0xFFFFFFFF && std::string(m.text.c_str()) == "old" && s->m_selected == 1);
        m = CsMessage(CONTROL_MSG_GET_ITEM, 0xFFFFFFFF, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0xFFFFFFFF && s->m_selected == 1);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 9, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 1 && std::string(m.text.c_str()) == "two");
        // A line by its text (no case): selected, the bar moved, the scroll posted.
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 9, 0, "TEXT 42");
        handled &= s->HandleMessage(&m);
        const S_vector4 centre = box.GetLineCentre(2);
        WM(r.edListMsgs, m.value == 2 && s->m_selected == 2 && box.m_selection == 2 &&
                             (centre.y != 0.0f ? (select->m_frameFlags & 1) != 0 && select->GetPos().x == centre.x &&
                                                     select->GetPos().y == centre.y
                                               : (select->m_frameFlags & 1) == 0) &&
                             ScMessages().find("90200000 1000020") == 0);
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 9, 0, "four");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0xFFFFFFFF && s->m_selected == 2);
        // A line put first: selected; the scroll posted (both).
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_INSERT_FIRST, 5, 0, "zero");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, CsRuns(box) == "zero/5 one/3 two/3 text 42/4 " && s->m_selected == 0 &&
                             ScMessages() == CmText("90200000 1000020 %d 0|90200000 200002b %d 0|", box.GetScrollRange(),
                                                    box.GetScroll()));
        // The selected removed: the box's selection.
        box.m_selection = 0;
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REMOVE_SELECTED);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, CsRuns(box) == "one/3 two/3 text 42/4 " && s->m_selected == 0 && g_controlMessages.size() == 2);
        // The scroll set; the previous / next nothing.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 0);
        handled &= s->HandleMessage(&m);
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_PREVIOUS_ITEM);
        handled &= s->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_NEXT_ITEM);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, box.GetScroll() == 0 && g_controlMessages.empty() && g_gfLog.empty());
        // The selected line edited (its run; START_EDITING posted).
        m = CsMessage(CONTROL_MSG_GET_ITEM, 1);
        handled &= s->HandleMessage(&m);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_EDIT);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, s->m_run == 1 && box.m_selection == 1 && ScMessages() == "0 20000bf 0 " + EdPtr(s) + "|");
        WM(r.edListInput, s->BeginEdit(0) == CONTROL_HANDLED && EdBuffer(*s) == "two" && s->OnChar('x') && EdRun(box, 1) == "twox");
        // Editing: the click, the press and the move nothing.
        s->m_anyCallback = {CONTROL_NOTIFY_ANY, nullptr, CsNotified};
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.edListInput, s->OnClick(1) == CONTROL_HANDLED && s->OnPress(1) == CONTROL_HANDLED &&
                              s->OnInput05(1) == CONTROL_HANDLED && g_controlMessages.empty() && g_gfLog.empty());
        s->OnInput0B(1);
        // The click: the hovered line selected, the scroll posted (both), notified.
        box.m_hover = 0;
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.edListInput, s->OnClick(1) == CONTROL_HANDLED && s->m_selected == 0 && box.m_selection == 0 &&
                              g_controlMessages.size() == 2 && !g_gfLog.empty() && g_gfLog.back() == "notify 4001000 0");
        box.m_hover = 2;
        g_controlMessages.clear();
        g_gfLog.clear();
        WM(r.edListInput, s->OnPress(1) == CONTROL_HANDLED && s->m_selected == 2 && g_controlMessages.size() == 2 &&
                              !g_gfLog.empty() && g_gfLog.back() == "notify 4006000 0");
        // The move: the line under the cursor hovered (its screen place); not taking input: nothing.
        box.GetScrollRange();
        g_csCursor.screenX = 400.7f;
        g_csCursor.screenY = 300.9f;
        box.m_hover = 7;
        g_gfLog.clear();
        WM(r.edListInput, s->OnInput05(1) == CONTROL_HANDLED && box.m_hover == box.LineAt(400, 300) &&
                              GfLogIs({"notify 4005000 0"}));
        g_csCursor.screenX = -5.0f;
        g_csCursor.screenY = -5.0f;
        s->OnInput05(1);
        WM(r.edListInput, box.m_hover == box.LineAt(-5, -5));
        s->m_takesInput = false;
        box.m_hover = 7;
        g_gfLog.clear();
        WM(r.edListInput, s->OnInput05(1) == CONTROL_HANDLED && box.m_hover == 7 && g_gfLog.empty());
        s->m_takesInput = true;
        // The enter handled; the leave the hover cleared, not handled.
        g_gfLog.clear();
        WM(r.edListInput, s->OnEnter(1) == CONTROL_HANDLED && s->OnLeave(1) == CONTROL_NOT_HANDLED && box.m_hover == -1 &&
                              GfLogIs({"notify 4004000 0"}));
        s->m_anyCallback = S_ControlCallback();
        // The bar: none at the line's place (off).
        s->m_selected = 40;
        g_gfLog.clear();
        s->UpdateSelectBar();
        WM(r.edList, GfLogIs({"on selectsl 0", "update selectsl"}));
        // Cleared: the range and the scroll 0 posted, the bar off (the selected kept).
        g_controlMessages.clear();
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, box.NumRuns() == 0 && s->m_selected == 40 && box.m_selection == -1 &&
                             ScMessages() == "90200000 1000020 0 0|90200000 200002b 0 0|" &&
                             GfLogIs({"on selectsl 0", "update selectsl"}));
        // (A line that is not there: not scrolled to, the fix.)
        WM(r.edList, !box.ScrollToLine(5) && !box.ScrollToLine(-1));
        // Without a value: the refresh posts nothing.
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0);
        handled &= s->HandleMessage(&m);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= s->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "s");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, g_controlMessages.empty() && CsRuns(box) == "s/3 ");

        // Lines again, with a value: index 0 got, its text; the first by its text, "" none (not the
        // one past the last).
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0x90200000);
        handled &= s->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= s->HandleMessage(&m);
        for (const char *t : {"a1", "b2", "c3"}) {
            m = CsMessage(CONTROL_MSG_ADD_TEXT, 3, 0, t);
            handled &= s->HandleMessage(&m);
        }
        m = CsMessage(CONTROL_MSG_GET_ITEM, 0, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0 && std::string(m.text.c_str()) == "a1" && s->m_selected == 0);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 9, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0 && std::string(m.text.c_str()) == "a1");
        m = CsMessage(CONTROL_MSG_GET_ITEM, 2, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 2 && std::string(m.text.c_str()) == "c3" && s->m_selected == 2);
        m = CsMessage(CONTROL_MSG_GET_TEXT, 9, 0, "old");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 2 && std::string(m.text.c_str()) == "c3");
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 9, 0, "A1");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0 && s->m_selected == 0);
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 9, 0, "");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 0xFFFFFFFF);
        // A range: the count and the scroll posted with their values; the scroll set.
        box.GetScrollRange();
        box.m_dirty = false;
        box.m_scrollRange = 5;
        box.m_scroll = 2;
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_REFRESH);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, ScMessages() == "90200000 1000020 5 0|90200000 200002b 2 0|");
        m = CsMessage(CONTROL_MSG_SET_POSITION, 3);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, box.GetScroll() == 3);
        // The bar moved after each change (its update).
        auto barUpdated = [] { return std::find(g_gfLog.begin(), g_gfLog.end(), "update selectsl") != g_gfLog.end(); };
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_POSITION, 1);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, barUpdated());
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_INSERT_FIRST, 3, 0, "z0");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, barUpdated());
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_EDIT);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, barUpdated());
        g_gfLog.clear();
        box.m_selection = 1;
        s->m_selected = 2;
        m = CsMessage(CONTROL_MSG_REMOVE_SELECTED);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, barUpdated() && s->m_selected == 1 && CsRuns(box) == "z0/3 a1/3 c3/3 ");
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 9, 0, "c3");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, barUpdated() && m.value == 2);
        g_gfLog.clear();
        box.m_hover = 0;
        s->OnClick(1);
        WM(r.edListInput, barUpdated());
        // ADD_TEXT_ID refreshes (the range posted); without a value SELECT_TEXT posts nothing.
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_ADD_TEXT_ID, 3, 42);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, !g_controlMessages.empty());
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0);
        handled &= s->HandleMessage(&m);
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_SELECT_TEXT, 9, 0, "a1");
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, m.value == 1 && g_controlMessages.empty());
        // The bar on a shown line: at its centre, on.
        s->m_selected = 0;
        const S_vector4 first = box.GetLineCentre(0);
        g_gfLog.clear();
        s->UpdateSelectBar();
        WM(r.edList, first.y != 0.0f && select->GetPos().x == first.x && select->GetPos().y == first.y &&
                         (select->m_frameFlags & 1) != 0 && g_gfLog.size() >= 2 && g_gfLog.back() == "update selectsl" &&
                         g_gfLog[g_gfLog.size() - 2] == "on selectsl 1");
        // The move over a line: hovered (the cursor's screen place read).
        box.GetScrollRange();
        int lineY = -1;
        for (int y = 0; y < 600 && lineY < 0; ++y)
            if (box.LineAt(400, y) >= 0)
                lineY = y;
        g_csCursor.screenX = 400.0f;
        g_csCursor.screenY = static_cast<float>(lineY);
        box.m_hover = -1;
        s->OnInput05(1);
        WM(r.edListInput, lineY >= 0 && box.m_hover >= 0);
        // CLEAR clears the hover before the selection.
        box.m_hover = 1;
        m = CsMessage(CONTROL_MSG_CLEAR);
        handled &= s->HandleMessage(&m);
        WM(r.edListMsgs, box.m_selection == -1 && box.m_hover == -1);
        g_gfLog.clear();
        delete s;
        WM(r.edList, g_csCursor.refs == refs && g_gfLog.size() >= 1 && g_gfLog[0] == "cursor release");
        // Without a select frame: made; the bar nothing.
        I3D_frame *plain = CsVisual("slplain");
        C_SelectList p(scene, 0x90300000, nullptr);
        g_gfLog.clear();
        WM(r.edList, p.Create(plain, root, nullptr) && !p.m_selectFrame && g_gfLog.size() == 4);
        g_gfLog.clear();
        p.UpdateSelectBar();
        WM(r.edList, g_gfLog.empty());
        frame->SetData(nullptr);
        select->SetData(nullptr);
        plain->SetData(nullptr);
    }

    WM(r.edMsgs, handled);
    g_gameProfile.m_unicodeFont = unicode;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("edit: edBase %d edChar %d edUtf8 %d edEditing %d edMsgs %d edBox %d edBoxRange %d edList %d edListInput %d "
                "edListMsgs %d\n",
                r.edBase, r.edChar, r.edUtf8, r.edEditing, r.edMsgs, r.edBox, r.edBoxRange, r.edList, r.edListInput, r.edListMsgs);
}
