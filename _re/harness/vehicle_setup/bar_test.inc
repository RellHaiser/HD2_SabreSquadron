// Part 9c-v-b's controls (game/controls.cpp: the bar, the upgrade bar, the hiding list and the pool
// list), in the text build, run by ControlsTest after ListTest (its fonts, its screen; ScMessages,
// ScFloat, CbRuns, C_CsBox, C_LsItem). Prints "bar:" with 1 per check met.

#include "game/mp_game_setup.h"

namespace {

struct S_BarResults {
    bool brBase = true, brScale = true, brMsgs = true, ubBase = true, ubScale = true, ubText = true;
    bool ubMsgs = true, hlBase = true, hlHide = true, hlShow = true, hlAll = true, plBase = true;
    bool plAdd = true, plHeld = true;
};

float BrFloat(uintptr_t value)
{
    const auto bits = static_cast<uint32_t>(value);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
// The last two log lines.
std::string BrTail()
{
    const size_t n = g_gfLog.size();
    return n < 2 ? std::string() : g_gfLog[n - 2] + "|" + g_gfLog[n - 1];
}
void BrDump(const char *tag)
{
    std::printf("DBG %s:", tag);
    for (const std::string &s : g_gfLog)
        std::printf(" [%s]", s.c_str());
    std::printf("\n");
}
void BrStr(const char *tag, const std::string &s, int a = 0, int b = 0)
{
    std::printf("DBG %s: [%s] %d %d\n", tag, s.c_str(), a, b);
}
// A message's result and its value2 (a float).
float BrGet(C_Control *c, uint32_t id, uint32_t value = 0)
{
    S_ControlMessage m = CsMessage(id, value, ScFloat(-7.0f));
    return c->HandleMessage(&m) ? BrFloat(m.value2) : -9.0f;
}
bool BrSet(C_Control *c, uint32_t id, float value2, uint32_t value = 0)
{
    S_ControlMessage m = CsMessage(id, value, ScFloat(value2));
    return c->HandleMessage(&m);
}
std::string BrOrder(const std::vector<C_Control *> &items)
{
    std::string s;
    for (C_Control *c : items)
        s += static_cast<C_LsItem *>(c)->name + " ";
    return s;
}
// An item logging its drag input ("<name> <slot> <id>"), returning 7.
struct C_BrItem : C_LsItem {
    C_BrItem(const std::string &n, uint32_t id, C_Control *parent) : C_LsItem(n, id, parent) {}
    int OnInput08(int id) override { return Log("in08", id, 7); }
    int OnInput09(int id) override { return Log("in09", id, 7); }
    int OnInput0A(int id) override { return Log("in0a", id, 7); }
};

} // namespace

void BarTest(I3D_scene *scene)
{
    S_BarResults r;
    bool handled = true;
    g_controlMessages.clear();
    g_gfLog.clear();

    // --- the bar: off at 0, its frame scaled by the value over the range ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "brframe");
        C_BarControl b(scene, 0x1C100000, nullptr);
        WM(r.brBase, b.m_value == 0.0f && b.m_max == 1.0f && b.m_min == 0.0f && b.m_axis == 0 && !b.m_takesInput &&
                         b.m_shown);
        g_gfLog.clear();
        WM(r.brBase, b.Create(frame, frame, nullptr) && b.m_frame == frame && BrTail() == "on brframe 0|update brframe");
        frame->SetScale(S_vector(9.0f, 9.0f, 9.0f));
        g_gfLog.clear();
        WM(r.brScale, BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 0.5f) && b.m_value == 0.5f &&
                          BrTail() == "on brframe 1|update brframe" && frame->GetScale() == S_vector(0.5f, 1.0f, 1.0f));
        // Over the range, not its part above the minimum.
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MAX, 4.0f);
        WM(r.brScale, b.m_max == 4.0f && frame->GetScale() == S_vector(0.125f, 1.0f, 1.0f));
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MIN, 2.0f);
        WM(r.brScale, b.m_min == 2.0f && frame->GetScale() == S_vector(0.25f, 1.0f, 1.0f));
        // A range not above 0: 1.
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MIN, 5.0f);
        WM(r.brScale, frame->GetScale() == S_vector(0.5f, 1.0f, 1.0f));
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MIN, 4.0f);
        WM(r.brScale, frame->GetScale() == S_vector(0.5f, 1.0f, 1.0f));
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MIN, 3.75f);
        WM(r.brScale, frame->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MIN, 0.0f);
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MAX, 1.0f);
        // At most 1; exactly 1; below 0 off (the scale kept).
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 3.0f);
        WM(r.brScale, frame->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 0.25f);
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 1.0f);
        WM(r.brScale, frame->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 0.5f);
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 1.25f);
        WM(r.brScale, frame->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        g_gfLog.clear();
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, -1.0f);
        WM(r.brScale, BrTail() == "on brframe 0|update brframe" && frame->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        // The axes (another: no scale, on).
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_VALUE, 0.75f);
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_BAR_AXIS, 1);
        handled &= b.HandleMessage(&m);
        WM(r.brScale, b.m_axis == 1 && frame->GetScale() == S_vector(1.0f, 0.75f, 1.0f));
        m = CsMessage(CONTROL_MSG_SET_BAR_AXIS, 2);
        handled &= b.HandleMessage(&m);
        WM(r.brScale, frame->GetScale() == S_vector(1.0f, 1.0f, 0.75f));
        m = CsMessage(CONTROL_MSG_SET_BAR_AXIS, 3);
        g_gfLog.clear();
        handled &= b.HandleMessage(&m);
        WM(r.brScale, frame->GetScale() == S_vector(1.0f, 1.0f, 0.75f) && BrTail() == "on brframe 1|update brframe");
        // Got.
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MIN, 0.5f);
        handled &= BrSet(&b, CONTROL_MSG_SET_BAR_MAX, 2.5f);
        WM(r.brMsgs, BrGet(&b, CONTROL_MSG_GET_BAR_MIN) == 0.5f && BrGet(&b, CONTROL_MSG_GET_BAR_MAX) == 2.5f &&
                         BrGet(&b, CONTROL_MSG_GET_BAR_VALUE) == 0.75f);
        // Notifications and others: the base's (true).
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_BAR_MIN, 0, ScFloat(9.0f), nullptr, 0x1C100000);
        WM(r.brMsgs, b.HandleMessage(&m) && b.m_routes.size() == 1 && b.m_min == 0.5f);
        m = CsMessage(CONTROL_MSG_SET_VALUE, 0x1C200000);
        WM(r.brMsgs, b.HandleMessage(&m) && b.m_value == 0.75f && b.C_Control::m_value == 0x1C200000);
        b.m_frame = nullptr;
    }

    // --- the upgrade bar: the value's and the upgrade's frames, the titles ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "ubframe");
        I3D_frame *name = CsVisual("name_ub", frame);
        I3D_frame *text = CsVisual("text_ub", frame);
        I3D_frame *value = GfVisual("hlcpctub", I3DVISUAL_OBJECT, frame);
        I3D_frame *upgrade = GfVisual("upgradeub", I3DVISUAL_OBJECT, frame);
        C_Control root(scene, 0x1C000000, nullptr);
        auto *u = new C_UpgradeBar(scene, 0x1C100000, &root);
        root.m_children = {u};
        WM(r.ubBase, !u->m_name && !u->m_text && !u->m_valueFrame && !u->m_upgradeFrame && u->m_current == 0.0f &&
                         u->m_upgrade == 0.0f && u->m_suffix.Size() == 0 && !u->m_takesInput);
        g_gfLog.clear();
        WM(r.ubBase, u->Create(frame, frame, nullptr) && u->m_children.size() == 2 && u->m_children[0] == u->m_name &&
                         u->m_children[1] == u->m_text && u->m_valueFrame == value && u->m_upgradeFrame == upgrade);
        WM(r.ubBase, u->m_name->GetId() == 0x1C100000 && u->m_text->GetId() == 0x1C100000 && u->m_name->m_parent == u &&
                         !u->m_name->m_takesInput && u->m_name->GetFrame() == name && u->m_text->GetFrame() == text);
        // Hidden at 0 and 0 (by the bar's own placing first), the frame updated.
        WM(r.ubBase, !u->m_shown && BrTail() == "find ubframe upgrade* 10001|update ubframe" &&
                         std::count(g_gfLog.begin(), g_gfLog.end(), "on ubframe 0") == 1);
        delete u->m_name->m_box;
        u->m_name->m_box = new C_CsBox("un");
        delete u->m_text->m_box;
        u->m_text->m_box = new C_CsBox("ut");
        // Not refreshed hidden.
        g_gfLog.clear();
        u->Refresh();
        WM(r.ubBase, g_gfLog.empty());
        // The name's text.
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "Armour");
        WM(r.ubMsgs, u->HandleMessage(&m) && CbRuns(u->m_name) == "Armour/3 ");
        m = CsMessage(CONTROL_MSG_SET_TEXT_ID, 4, 43);
        WM(r.ubMsgs, u->HandleMessage(&m) && CbRuns(u->m_name) == "text 43/4 ");
        // The value: shown, its frame scaled, the upgrade's 0; the text.
        value->SetScale(S_vector(9.0f, 9.0f, 9.0f));
        upgrade->SetScale(S_vector(9.0f, 9.0f, 9.0f));
        g_gfLog.clear();
        WM(r.ubScale, BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.5f, 0) && u->m_current == 0.5f && u->m_shown &&
                          value->GetScale() == S_vector(0.5f, 1.0f, 1.0f) && upgrade->GetScale() == S_vector(0.0f, 1.0f, 1.0f) &&
                          g_gfLog.back() == "update ubframe" &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "on ubframe 1") == 1);
        WM(r.ubText, CbRuns(u->m_text) == "0.5 /43 /43 ");
        // Refreshed shown: the titles'.
        g_gfLog.clear();
        u->Refresh();
        WM(r.ubBase, GfLogIs({"box un render", "box ut render"}));
        // The upgrade.
        WM(r.ubScale, BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.375f, 1) && u->m_upgrade == 0.375f &&
                          value->GetScale() == S_vector(0.5f, 1.0f, 1.0f) &&
                          upgrade->GetScale() == S_vector(0.875f, 1.0f, 1.0f));
        WM(r.ubText, CbRuns(u->m_text) == "0.5 /43 + 0.4/44 /43 ");
        // The suffix.
        S_ControlMessage s = CsMessage(CONTROL_MSG_SET_BAR_VALUE, 2, reinterpret_cast<uintptr_t>("pts"));
        WM(r.ubText, u->HandleMessage(&s) && std::string(u->m_suffix.c_str()) == "pts" &&
                         CbRuns(u->m_text) == "0.5 /43 + 0.4/44 pts/43 ");
        // Another value: only the update.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.25f, 3);
        WM(r.ubMsgs, u->m_current == 0.0f && u->m_upgrade == 0.375f && u->m_shown &&
                         value->GetScale() == S_vector(0.0f, 1.0f, 1.0f) && upgrade->GetScale() == S_vector(0.375f, 1.0f, 1.0f));
        WM(r.ubText, CbRuns(u->m_text) == "0.0 /43 + 0.4/44 pts/43 ");
        // Shown already: the frame updated once (not shown again).
        g_gfLog.clear();
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 0);
        WM(r.ubScale, std::count(g_gfLog.begin(), g_gfLog.end(), "update ubframe") == 1 &&
                          std::count(g_gfLog.begin(), g_gfLog.end(), "on ubframe 1") == 0);
        // An upgrade of just 0.1 shown; below, not.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.1f, 1);
        WM(r.ubText, CbRuns(u->m_text) == "0.0 /43 + 0.1/44 pts/43 ");
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.09f, 1);
        WM(r.ubText, CbRuns(u->m_text) == "0.0 /43 pts/43 ");
        // Over the maximum: 1, the value shown the maximum, the upgrade what is left.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 1.5f, 0);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 1.0f, 1.0f) && upgrade->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        WM(r.ubText, CbRuns(u->m_text) == "1.0 /43 pts/43 ");
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.75f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.5f, 1);
        WM(r.ubText, CbRuns(u->m_text) == "0.8 /43 + 0.2/44 pts/43 " && upgrade->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        // Exactly the maximum: divided.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.25f, 1);
        WM(r.ubScale, upgrade->GetScale() == S_vector(1.0f, 1.0f, 1.0f) && value->GetScale() == S_vector(0.75f, 1.0f, 1.0f));
        WM(r.ubText, CbRuns(u->m_text) == "0.8 /43 + 0.2/44 pts/43 ");
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MAX, 2.0f);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 2.0f, 0);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 1);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 1.5f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.5f, 1);
        WM(r.ubScale, value->GetScale() == S_vector(0.75f, 1.0f, 1.0f) && upgrade->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MAX, 1.0f);
        // Below the minimum: 0; the sum at or above it divided (over the range); below, 0.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MIN, 0.5f);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.25f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.25f, 1);
        WM(r.ubScale, value->GetScale() == S_vector(0.0f, 1.0f, 1.0f) && upgrade->GetScale() == S_vector(1.0f, 1.0f, 1.0f));
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.5f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.125f, 1);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 1.0f, 1.0f) && upgrade->GetScale() == S_vector(1.25f, 1.0f, 1.0f));
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.25f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.125f, 1);
        WM(r.ubScale, upgrade->GetScale() == S_vector(0.0f, 1.0f, 1.0f));
        // A range not above 0: 1.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MIN, 1.0f);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 1.0f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 1);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 1.0f, 1.0f) && u->m_min == 1.0f);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MIN, 0.0f);
        // A range of 0.25 kept.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MIN, 0.75f);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.875f, 0);
        WM(r.ubScale, value->GetScale() == S_vector(3.5f, 1.0f, 1.0f));
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_MIN, 0.0f);
        // The axes.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.5f, 0);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.25f, 1);
        S_ControlMessage a = CsMessage(CONTROL_MSG_SET_BAR_AXIS, 1);
        handled &= u->HandleMessage(&a);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 0.5f, 1.0f) && upgrade->GetScale() == S_vector(1.0f, 0.75f, 1.0f));
        a = CsMessage(CONTROL_MSG_SET_BAR_AXIS, 2);
        handled &= u->HandleMessage(&a);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 1.0f, 0.5f) && upgrade->GetScale() == S_vector(1.0f, 1.0f, 0.75f));
        a = CsMessage(CONTROL_MSG_SET_BAR_AXIS, 3);
        handled &= u->HandleMessage(&a);
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.125f, 0);
        WM(r.ubScale, value->GetScale() == S_vector(1.0f, 1.0f, 0.5f) && upgrade->GetScale() == S_vector(1.0f, 1.0f, 0.75f));
        // Got (another: kept).
        WM(r.ubMsgs, BrGet(u, CONTROL_MSG_GET_BAR_VALUE, 0) == 0.125f && BrGet(u, CONTROL_MSG_GET_BAR_VALUE, 1) == 0.25f &&
                         BrGet(u, CONTROL_MSG_GET_BAR_VALUE, 2) == -7.0f && BrGet(u, CONTROL_MSG_GET_BAR_MAX) == 1.0f);
        // Hidden again at 0 and 0; the upgrade alone shows.
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 0);
        g_gfLog.clear();
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 1);
        WM(r.ubBase, !u->m_shown && GfLogIs({"on ubframe 0", "update ubframe", "update ubframe"}));
        g_gfLog.clear();
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 1);
        WM(r.ubBase, GfLogIs({"update ubframe"}));
        handled &= BrSet(u, CONTROL_MSG_SET_BAR_VALUE, 0.5f, 1);
        WM(r.ubBase, u->m_shown);
        // A notification: the base's.
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_SET_TEXT, 0, 0, nullptr, 0x1C100000);
        WM(r.ubMsgs, u->HandleMessage(&m) && u->m_routes.size() == 1 && CbRuns(u->m_name) == "text 43/4 ");
        // Not made without the value's or the upgrade's frame; the titles optional.
        I3D_frame *half = GfFrame(FRAME_DUMMY, "ubhalf");
        GfVisual("hlcpcth", I3DVISUAL_OBJECT, half);
        C_UpgradeBar h(scene, 0x1C200000, nullptr);
        WM(r.ubBase, !h.Create(half, half, nullptr) && h.m_children.empty() && h.m_valueFrame && !h.m_upgradeFrame);
        I3D_frame *other = GfFrame(FRAME_DUMMY, "ubother");
        GfVisual("upgradeo", I3DVISUAL_OBJECT, other);
        C_UpgradeBar o(scene, 0x1C300000, nullptr);
        WM(r.ubBase, !o.Create(other, other, nullptr) && !o.m_valueFrame && o.m_upgradeFrame);
        handled &= BrSet(&o, CONTROL_MSG_SET_BAR_VALUE, 0.0f, 1);
        m = CsMessage(CONTROL_MSG_SET_TEXT, 3, 0, "x");
        o.Refresh();
        WM(r.ubMsgs, o.HandleMessage(&m));
        // Deleted with its titles.
        g_gfLog.clear();
        root.m_children.clear();
        delete u;
        WM(r.ubBase, std::count(g_gfLog.begin(), g_gfLog.end(), "box un deleted") == 1 &&
                         std::count(g_gfLog.begin(), g_gfLog.end(), "box ut deleted") == 1);
    }

    // --- the hiding list: items hidden (kept) and shown again ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "hlframe");
        C_Control root(scene, 0x1C000000, nullptr);
        auto *l = new C_HidingList(scene, 0x1C100000, &root);
        root.m_children = {l};
        WM(r.hlBase, l->m_sorted && l->m_hidden.empty() && l->m_selected == -1);
        l->Create(frame, frame, nullptr);
        std::vector<C_LsItem *> items;
        for (int i = 0; i < 5; ++i) {
            items.push_back(new C_LsItem(CmText("h%d", i), 0x1C102000 + 0x2000 * i, l));
            l->m_children.push_back(items.back());
        }
        S_ControlMessage m = CsMessage(CONTROL_MSG_SET_VISIBLE, 2);
        handled &= l->HandleMessage(&m);
        // Hidden: kept, refreshed.
        int refreshes = items[0]->refreshes;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C104000);
        WM(r.hlHide, l->HandleMessage(&m) && BrOrder(l->m_children) == "h0 h2 h3 h4 " && BrOrder(l->m_hidden) == "h1 " &&
                         !items[1]->m_shown && items[0]->refreshes == refreshes + 1 &&
                         std::find(g_gfLog.begin(), g_gfLog.end(), "h1 leave 0") == g_gfLog.end());
        // The focus: left, none.
        l->m_focus = 1;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C106000);
        WM(r.hlHide, l->HandleMessage(&m) && l->m_focus == -1 && !g_gfLog.empty() && g_gfLog[0] == "h2 leave 0" &&
                         BrOrder(l->m_hidden) == "h1 h2 ");
        // Another focus kept; none: nothing.
        l->m_focus = 2;
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C102000);
        handled &= l->HandleMessage(&m);
        WM(r.hlHide, l->m_focus == 2 && BrOrder(l->m_children) == "h3 h4 ");
        l->m_focus = -1;
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C1F0000);
        WM(r.hlHide, l->HandleMessage(&m) && BrOrder(l->m_children) == "h3 h4 " && BrOrder(l->m_hidden) == "h1 h2 h0 ");
        // Messages to a hidden item: its; to a shown one: its.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_SET_INDEX, 4, 0, nullptr, 0x1C106000);
        WM(r.hlBase, l->HandleMessage(&m) && items[2]->m_index == 4 && GfLogIs({"h2 msg 1000009"}));
        m = CsMessage(CONTROL_MSG_SET_INDEX, 6, 0, nullptr, 0x1C104000);
        WM(r.hlBase, l->HandleMessage(&m) && items[1]->m_index == 6 && l->m_index == -1);
        m = CsMessage(CONTROL_MSG_SET_INDEX, 5, 0, nullptr, 0x1C10A000);
        WM(r.hlBase, l->HandleMessage(&m) && items[4]->m_index == 5);
        m = CsMessage(CONTROL_MSG_HIDE, 0, 0, nullptr, 0x1C108000);
        WM(r.hlBase, !l->HandleMessage(&m) || !items[3]->m_shown);
        items[3]->m_shown = true;
        items[3]->m_frame->SetOn(true);
        // Shown again: sorted in, left.
        refreshes = items[3]->refreshes;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C104000);
        WM(r.hlShow, l->HandleMessage(&m) && BrOrder(l->m_children) == "h1 h3 h4 " && BrOrder(l->m_hidden) == "h2 h0 " &&
                         std::count(g_gfLog.begin(), g_gfLog.end(), "h1 leave 0") == 1 && items[3]->refreshes == refreshes + 1);
        // A shown one: nothing (not sorted); none: sorted.
        std::swap(l->m_children[0], l->m_children[2]);
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C108000);
        WM(r.hlShow, l->HandleMessage(&m) && BrOrder(l->m_children) == "h4 h3 h1 ");
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C1F0000);
        WM(r.hlShow, l->HandleMessage(&m) && BrOrder(l->m_children) == "h1 h3 h4 " && BrOrder(l->m_hidden) == "h2 h0 ");
        // The last hidden one.
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C102000);
        handled &= l->HandleMessage(&m);
        WM(r.hlShow, BrOrder(l->m_children) == "h0 h1 h3 h4 " && BrOrder(l->m_hidden) == "h2 ");
        // Not shown: not refreshed.
        l->m_shown = false;
        refreshes = items[0]->refreshes;
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C106000);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C106000);
        handled &= l->HandleMessage(&m);
        WM(r.hlShow, items[0]->refreshes == refreshes && BrOrder(l->m_hidden) == "h2 ");
        l->m_shown = true;
        // Scrolled back when the rest no longer fills the shown; past the end (unsigned): not.
        m = CsMessage(CONTROL_MSG_SET_POSITION, 2);
        handled &= l->HandleMessage(&m);
        WM(r.hlHide, l->m_first == 2 && l->m_last == 3);
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C104000);
        handled &= l->HandleMessage(&m);
        WM(r.hlHide, l->m_first == 1 && l->m_last == 2 && BrOrder(l->m_children) == "h0 h3 h4 ");
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C108000);
        handled &= l->HandleMessage(&m);
        WM(r.hlHide, l->m_first == 0 && l->m_last == 1);
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C104000);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_SET_POSITION, 1);
        handled &= l->HandleMessage(&m);
        l->m_first = 3;
        l->m_last = 4;
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C102000);
        handled &= l->HandleMessage(&m);
        WM(r.hlHide, l->m_first == 3 && BrOrder(l->m_children) == "h1 h4 ");
        // All hidden: unpicked, each hidden, in order; scrolled back (signed).
        l->m_picked = items[4];
        for (C_Control *c : l->m_children)
            c->m_shown = true;
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_HIDE_ALL);
        WM(r.hlAll, l->HandleMessage(&m) && l->m_children.empty() && BrOrder(l->m_hidden) == "h2 h3 h0 h1 h4 " &&
                        !items[1]->m_shown && !items[4]->m_shown && !l->m_picked && l->m_first == 0 &&
                        std::find(g_gfLog.begin(), g_gfLog.end(), "h4 in02 1") != g_gfLog.end());
        WM(r.hlAll, std::find(g_gfLog.begin(), g_gfLog.end(), "on h4 0") < std::find(g_gfLog.begin(), g_gfLog.end(), "on h1 0"));
        // Not shown: not refreshed (none to count: the list's own frame logged nothing).
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C104000);
        handled &= l->HandleMessage(&m);
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C10A000);
        handled &= l->HandleMessage(&m);
        // (The refresh seen by its post: the list's value names a bar.)
        l->m_value = 0x1C200000;
        l->m_shown = false;
        g_controlMessages.clear();
        m = CsMessage(CONTROL_MSG_HIDE_ALL);
        handled &= l->HandleMessage(&m);
        WM(r.hlAll, g_controlMessages.empty() && l->m_hidden.size() == 5);
        m = CsMessage(CONTROL_MSG_ITEM_COUNTED, 0x1C104000);
        handled &= l->HandleMessage(&m);
        l->m_shown = true;
        m = CsMessage(CONTROL_MSG_HIDE_ALL);
        handled &= l->HandleMessage(&m);
        WM(r.hlAll, ScMessages() == "1c200000 1000020 4294967295 0|1c200000 200002b 0 0|");
        l->m_value = 0;
        g_controlMessages.clear();
        // Notifications and others: the list group's.
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_HIDE_ALL, 0, 0, nullptr, 0x1C100000);
        WM(r.hlBase, l->HandleMessage(&m) && l->m_routes.size() == 1);
        m = CsMessage(CONTROL_MSG_SET_VISIBLE, 3);
        WM(r.hlBase, l->HandleMessage(&m) && l->m_visible == 3);
        // Deleted with the hidden, the last first.
        WM(r.hlBase, BrOrder(l->m_hidden) == "h2 h3 h0 h4 h1 ");
        g_gfLog.clear();
        root.m_children.clear();
        delete l;
        WM(r.hlBase, GfLogIs({"h1 deleted", "h4 deleted", "h0 deleted", "h3 deleted", "h2 deleted"}));
    }

    // --- the pool list: hidden items filled and shown (unsorted), the focus held for a drag ---
    {
        I3D_frame *frame = GfFrame(FRAME_DUMMY, "plframe");
        C_Control root(scene, 0x1C000000, nullptr);
        auto *l = new C_PoolList(scene, 0x1C100000, &root);
        root.m_children = {l};
        WM(r.plBase, !l->m_sorted && !l->m_held && l->m_hidden.empty() && l->m_selected == -1);
        l->Create(frame, frame, nullptr);
        S_ControlMessage shown = CsMessage(CONTROL_MSG_SET_VISIBLE, 3);
        handled &= l->HandleMessage(&shown);
        std::vector<C_BrItem *> items;
        for (int i = 0; i < 3; ++i) {
            items.push_back(new C_BrItem(CmText("p%d", i), 0x1C108000 - 0x2000 * i, l));
            items.back()->m_shown = false;
            l->m_hidden.push_back(items.back());
        }
        // Without an entry or a hidden item: false.
        S_ControlMessage m = CsMessage(CONTROL_MSG_ADD_ENTRY, 5);
        WM(r.plAdd, !l->HandleMessage(&m) && l->m_children.empty());
        C_str name("Bridge");
        // (value2: a C_MpMap of the picked maps' list; the port reads its name field.)
        const C_MpMap map(name, C_str());
        const void *entry = &map;
        // An entry: the first hidden named (style 0xd), indexed, shown.
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 5, reinterpret_cast<uintptr_t>(entry));
        WM(r.plAdd, l->HandleMessage(&m) && BrOrder(l->m_children) == "p0 " && BrOrder(l->m_hidden) == "p1 p2 " &&
                        items[0]->m_index == 5 && l->m_focus == -1);
        WM(r.plAdd, g_gfLog.size() >= 3 && g_gfLog[0] == "p0 msg 1000014" && g_gfLog[1] == "p0 msg 1000009" &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "p0 leave 0") == 1);
        // The text set: the item's own messages logged; a text control instead shows it.
        auto *t = new C_TextControl(scene, 0x1C10A000, l);
        t->Create(CsVisual("pltext"), frame, nullptr);
        delete t->m_box;
        t->m_box = new C_CsBox("pt");
        l->m_hidden.insert(l->m_hidden.begin(), t);
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 6, reinterpret_cast<uintptr_t>(entry));
        WM(r.plAdd, l->HandleMessage(&m) && CbRuns(t) == "Bridge/13 " && t->m_index == 6 &&
                        l->m_children.size() == 2 && l->m_children[1] == t);
        // The current entry: also hovered (unsorted: last).
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_ADD_CURRENT_ENTRY, 7, reinterpret_cast<uintptr_t>(entry));
        WM(r.plAdd, l->HandleMessage(&m) && BrOrder({l->m_children[0], l->m_children[2]}) == "p0 p1 " &&
                        items[1]->m_index == 7 && l->m_focus == 2 &&
                        std::count(g_gfLog.begin(), g_gfLog.end(), "p1 enter 1c106000") == 1);
        WM(r.plAdd, std::string(name.c_str()) == "Bridge");
        // Without an entry: false (hidden left).
        m = CsMessage(CONTROL_MSG_ADD_CURRENT_ENTRY, 8);
        WM(r.plAdd, !l->HandleMessage(&m) && l->m_hidden.size() == 1);
        // Notifications and others: the hiding list's.
        m = CsMessage(CONTROL_NOTIFY_CLICK | CONTROL_MSG_ADD_ENTRY, 0, 0, nullptr, 0x1C100000);
        WM(r.plBase, l->HandleMessage(&m) && l->m_routes.size() == 1);
        m = CsMessage(CONTROL_MSG_HIDE_ITEM, 0x1C108000);
        WM(r.plBase, l->HandleMessage(&m) && BrOrder(l->m_hidden) == "p2 p0 " && l->m_children.size() == 2);
        // To a hidden item: its.
        m = CsMessage(CONTROL_MSG_SET_INDEX, 9, 0, nullptr, 0x1C104000);
        WM(r.plBase, l->HandleMessage(&m) && items[2]->m_index == 9);
        // An entry to a hidden item: its (none filled).
        g_gfLog.clear();
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 4, reinterpret_cast<uintptr_t>(entry), nullptr, 0x1C104000);
        l->HandleMessage(&m);
        WM(r.plBase, GfLogIs({"p2 msg 1000067"}) && l->m_hidden.size() == 2 && items[2]->m_index == 9);
        // No hidden item: false.
        C_PoolList none(scene, 0x1C200000, nullptr);
        m = CsMessage(CONTROL_MSG_ADD_ENTRY, 1, reinterpret_cast<uintptr_t>(entry));
        WM(r.plAdd, !none.HandleMessage(&m) && none.m_children.empty());
        m = CsMessage(CONTROL_MSG_SET_INDEX, 3, 0, nullptr, 0x1C10A000);
        WM(r.plBase, l->HandleMessage(&m) && t->m_index == 3);
        // The drag: none held, 0; the focus held from 0A, its 08, its 09 (then none).
        l->m_focus = -1;
        g_gfLog.clear();
        WM(r.plHeld, l->OnInput0A(1) == 0 && l->OnInput08(1) == 0 && l->OnInput09(1) == 0 && !l->m_held && g_gfLog.empty());
        l->m_focus = 1;
        WM(r.plHeld, l->OnInput0A(0x21) == 7 && l->m_held == items[1] && GfLogIs({"p1 in0a 21"}));
        l->m_focus = 0;
        g_gfLog.clear();
        WM(r.plHeld, l->OnInput08(0x22) == 7 && l->m_held == items[1] && GfLogIs({"p1 in08 22"}));
        g_gfLog.clear();
        WM(r.plHeld, l->OnInput09(0x23) == 7 && !l->m_held && GfLogIs({"p1 in09 23"}));
        g_gfLog.clear();
        WM(r.plHeld, l->OnInput09(0x24) == 0 && l->OnInput08(0x24) == 0 && g_gfLog.empty());
        root.m_children.clear();
        delete l;
    }

    WM(r.plBase, handled);
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("bar: brBase %d brScale %d brMsgs %d ubBase %d ubScale %d ubText %d ubMsgs %d hlBase %d hlHide %d "
                "hlShow %d hlAll %d plBase %d plAdd %d plHeld %d\n",
                r.brBase, r.brScale, r.brMsgs, r.ubBase, r.ubScale, r.ubText, r.ubMsgs, r.hlBase, r.hlHide, r.hlShow,
                r.hlAll, r.plBase, r.plAdd, r.plHeld);
}
