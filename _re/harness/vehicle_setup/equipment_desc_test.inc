// Part 9e-ii-b's equipment panel description (C_EquipmentPanel::BuildDescription and
// S_ControlDesc::AddChild), in the factory build, run by ControlsTest after EquipmentPanelTest: the
// description tree dumped ("<depth> <type> <name> <id>" and its messages) and compared with the
// original's; the callbacks called. Prints "equipmentdesc:" with 1 per check met.
#include "game/equipment_menu.h"

namespace {

struct S_EquipmentDescResults {
    bool edAddChild = true, edTree = true, edIds = true, edNoMenu = true, edOptional = true, edCallbacks = true;
};

// A menu logging its item slots.
struct C_EdMenu : C_EquipmentMenuBase {
    bool answer = true;
    bool Log(const char *slot, void *argument, C_Control *control, uint32_t notification, uint32_t value)
    {
        GfLog("%s %d %x %x %x", slot, argument == this, control ? control->GetId() : 0, notification, value);
        return answer;
    }
    void OnMessage(S_ControlMessage *) override {}
    bool BuildScreens() override { return true; }
    bool OnItemNotify13(void *a, C_Control *c, uint32_t n, uint32_t v) override { return Log("n13", a, c, n, v); }
    bool OnItemNotify60(void *a, C_Control *c, uint32_t n, uint32_t v) override { return Log("n60", a, c, n, v); }
    bool OnItemNotify0A(void *a, C_Control *c, uint32_t n, uint32_t v) override { return Log("n0a", a, c, n, v); }
    bool OnItemNotify08(void *a, C_Control *c, uint32_t n, uint32_t v) override { return Log("n08", a, c, n, v); }
    bool OnItemNotify09(void *a, C_Control *c, uint32_t n, uint32_t v) override { return Log("n09", a, c, n, v); }
    bool OnItemHover(void *a, C_Control *c, uint32_t n, uint32_t v) override { return Log("hover", a, c, n, v); }
};

const void *g_edMenu, *g_edPanel;
std::string EdValue3(uintptr_t v)
{
    if (v == reinterpret_cast<uintptr_t>(&C_EquipmentMenuBase::CallItemNotify13))
        return "f13";
    if (v == reinterpret_cast<uintptr_t>(&C_EquipmentMenuBase::CallItemNotify0A))
        return "f0a";
    if (v == reinterpret_cast<uintptr_t>(&C_EquipmentMenuBase::CallItemNotify08))
        return "f08";
    if (v == reinterpret_cast<uintptr_t>(&C_EquipmentMenuBase::CallItemNotify09))
        return "f09";
    if (v == reinterpret_cast<uintptr_t>(&C_EquipmentMenuBase::CallItemHover))
        return "fhover";
    if (v == reinterpret_cast<uintptr_t>(&C_EquipmentPanel::OnScroll))
        return "fscroll";
    return CmText("%x", static_cast<uint32_t>(v));
}
std::string EdValue2(uintptr_t v)
{
    if (v != 0 && reinterpret_cast<const void *>(v) == g_edMenu)
        return "menu";
    if (v != 0 && reinterpret_cast<const void *>(v) == g_edPanel)
        return "panel";
    return CmText("%x", static_cast<uint32_t>(v));
}
void EdDump(const S_ControlDesc *d, int depth, std::vector<std::string> &out)
{
    out.push_back(CmText("%d %x %s %x", depth, d->type, d->name.c_str(), d->id));
    for (const S_ControlMessage &m : d->messages)
        out.push_back(CmText("  %x %x %x %s %s %s", m.target, m.id, m.value, EdValue2(m.value2).c_str(),
                             EdValue3(m.value3).c_str(), m.text.c_str()));
    for (const S_ControlDesc *c : d->children)
        EdDump(c, depth + 1, out);
}

// The original's description (written from 0x0066f200): the panel 5 "P_Equipment", the screen's
// control 3 of screen 2 (id 0x08300000); `menu`: the item callbacks; `overload` / `description`: the
// optional frames there.
std::vector<std::string> EdExpected(bool menu, bool overload, bool description)
{
    const std::string facegroup = "  0 100009a 0 0 0 X_SQUARE.BMP";
    const auto item = [&](std::vector<std::string> &v, bool all) {
        v.push_back(facegroup);
        if (!menu)
            return;
        if (all) {
            v.push_back("  0 1000007 4013000 menu f13 ");
            v.push_back("  0 1000007 400a000 menu f0a ");
            v.push_back("  0 1000007 4008000 menu f08 ");
            v.push_back("  0 1000007 4009000 menu f09 ");
        }
        v.push_back("  0 1000007 4003000 menu fhover ");
        v.push_back("  0 1000007 4004000 menu fhover ");
    };
    std::vector<std::string> v = {"0 5 P_Equipment 8300000"};
    const char *places[] = {"head", "uniform", "back", "shoulder", "hands"};
    for (int i = 0; i < 5; ++i) {
        v.push_back(CmText("1 4 %s %x", places[i], 0x08300000 | (i + 1) << 12));
        item(v, i != 1);
    }
    v.push_back("1 4 drop 8306000");
    const auto scroll = [&](uint32_t kind, const char *name, const char *bar, const char *start, const char *up,
                            const char *down, const char *caption, const char *text, const char *slider) {
        const uint32_t g = 0x08300000 | kind << 12;
        v.push_back(CmText("1 5 %s %x", name, g));
        v.push_back(CmText("2 16 %s %x", bar, g | 0x10));
        v.push_back("  0 1000093 2 0 0 ");
        v.push_back(CmText("  0 1000092 0 %s 0 ", start));
        v.push_back(CmText("2 2 %s %x", up, g | 0x20));
        v.push_back("  0 1000001 0 0 0 ");
        v.push_back("  0 1000007 4006000 panel fscroll ");
        v.push_back(CmText("2 2 %s %x", down, g | 0x30));
        v.push_back("  0 1000001 0 0 0 ");
        v.push_back("  0 1000007 4006000 panel fscroll ");
        v.push_back(CmText("2 13 %s %x", caption, g | 0x40));
        v.push_back(CmText("  0 1000015 b %s 0 ", text));
        v.push_back(CmText("2 7 %s %x", slider, g | 0x50));
        v.push_back("  0 1000004 0 0 0 ");
    };
    const auto pane = [&](uint32_t kind, const char *name, int first) {
        const uint32_t g = 0x08300000 | kind << 12;
        v.push_back(CmText("1 b %s %x", name, g));
        v.push_back("  0 1000028 4 0 0 ");
        for (int i = 0; i < 4; ++i) {
            v.push_back(CmText("2 4 item%02d %x", first + i, g | (i + 1) << 4));
            item(v, true);
        }
    };
    scroll(7, "scroll13", "hlcpct00", "3f000000", "barrow up23", "barrow down23", "text_hlstrscap", "a9c", "slider12");
    pane(8, "GB_Holsters", 0);
    scroll(9, "scroll14", "hlcpct01", "3f4ccccd", "barrow up24", "barrow down24", "text_bkpckcap", "a9d", "slider13");
    pane(10, "GR_BackPack", 4);
    v.push_back("1 13 text_cpcty 830b000");
    v.push_back("  0 1000015 b 7d2 0 ");
    v.push_back("1 16 hlcpct02 830c000");
    v.push_back("  0 1000093 2 0 0 ");
    v.push_back("  0 1000092 0 3f4ccccd 0 ");
    uint32_t next = 0xd;
    if (overload) {
        v.push_back(CmText("1 16 overload %x", 0x08300000 | next++ << 12));
        v.push_back("  0 1000093 2 0 0 ");
        v.push_back("  0 1000092 0 0 0 ");
    }
    if (description) {
        v.push_back(CmText("1 15 description %x", 0x08300000 | next++ << 12));
        v.push_back("  0 1000015 b 7d2 0 ");
        v.push_back("  0 1000004 0 0 0 ");
    }
    const char *labels[] = {"text_back", "text_head", "text_drop", "text_hands", "text_uniform", "text_shoulder",
                            "text_holsters", "text_backpack"};
    for (int i = 0; i < 8; ++i) {
        v.push_back(CmText("1 13 %s %x", labels[i], 0x08300000 | next++ << 12));
        v.push_back(CmText("  0 1000015 b %x 0 ", 0xa96 + i));
    }
    return v;
}

} // namespace

void EquipmentDescTest(I3D_scene *)
{
    S_EquipmentDescResults r;
    MrInitVtables();
    MsInitVtables();
    g_gfLog.clear();
    g_controlMessages.clear();

    // --- children's ids: under a control its index in bits 12-19, under a child in bits 4-11 ---
    {
        bool ok = true;
        S_ControlDesc top(5, "top", 0x08300000);
        S_ControlDesc *a = top.AddChild(4, "a");
        S_ControlDesc *b = top.AddChild(2, "b");
        S_ControlDesc *c = b->AddChild(7, "c");
        S_ControlDesc *d = b->AddChild(8, "d");
        ok &= a->id == 0x08301000 && b->id == 0x08302000 && c->id == 0x08302010 && d->id == 0x08302020 &&
              a->type == 4 && std::strcmp(a->name.c_str(), "a") == 0 && c->type == 7 && top.children.size() == 2 &&
              b->children.size() == 2 && b->children[1] == d && a->messages.empty();
        // (The index wraps at 256.)
        S_ControlDesc wide(5, "w", 0x1000000);
        for (int i = 0; i < 255; ++i)
            wide.AddChild(1, "x");
        ok &= wide.AddChild(1, "y")->id == 0x1000000 && wide.AddChild(1, "z")->id == 0x1001000;
        WM(r.edAddChild, ok);
    }

    // The screen: screen 2 with two controls already (the panel the third).
    S_ScreenDesc screen("scr", "scr.i3d", 2u << SCREEN_ID_SHIFT);
    const auto reset = [&]() {
        while (!screen.controls.empty()) {
            delete screen.controls.back();
            screen.controls.pop_back();
        }
        screen.AddControl(1, "first");
        screen.AddControl(1, "second");
    };
    S_MrObj *model = MsFrame("smodel"), *overload = MsFrame("over"), *description = MsFrame("descr"),
            *dummy = MsFrame("pdummy");

    // --- the whole description with a menu and both optional frames ---
    {
        bool ok = true;
        C_EdMenu menu;
        C_EquipmentPanel p;
        p.SetMenu(&menu);
        g_edMenu = &menu;
        g_edPanel = &p;
        g_msChildren[{model, "overload"}] = overload;
        g_msChildren[{model, "description"}] = description;
        g_msChildren[{model, "P_Equipment"}] = dummy;
        S_SquadSoldier s;
        p.m_squadSoldier = &s;
        reset();
        g_gfLog.clear();
        p.BuildDescription(&screen, As<I3D_frame>(model));
        std::vector<std::string> got;
        ok &= screen.controls.size() == 3;
        if (ok)
            EdDump(screen.controls[2], 0, got);
        ok &= MsLogIs(EdExpected(true, true, true), got);
        // The frames looked for; the description moved under the dummy, hidden; the highlights.
        ok &= MsLogIs({"smodel 48 overload 1", "smodel 48 description 1", "smodel 48 P_Equipment 20",
                       "descr 38 pdummy 1", "descr on 0", "smodel 48 selhead 1", "smodel 48 seluniform 1",
                       "smodel 48 selbck 1", "smodel 48 selshoulder 1", "smodel 48 selhands 1", "smodel 48 seldrop 1",
                       "smodel 48 selholsters 1", "smodel 48 selbackpack 1"},
                      g_gfLog);
        WM(r.edTree, ok);

        // --- the ids (the panel's with the kinds and children); the squad soldier cleared ---
        ok = true;
        const uint32_t id = 0x08300000;
        ok &= p.m_headSlot == (id | 0x1000) && p.m_uniformSlot == (id | 0x2000) && p.m_backpackSlot == (id | 0x3000) &&
              p.m_longGunSlot == (id | 0x4000) && p.m_handsSlot == (id | 0x5000) && p.m_dropSlot == (id | 0x6000) &&
              p.m_beltBar == (id | 0x7010) && p.m_beltUp == (id | 0x7020) && p.m_beltDown == (id | 0x7030) &&
              p.m_beltText == (id | 0x7040) && p.m_backpackBar == (id | 0x9010) && p.m_backpackUp == (id | 0x9020) &&
              p.m_backpackDown == (id | 0x9030) && p.m_backpackText == (id | 0x9040) && p.m_weightText == (id | 0xB000) &&
              p.m_weightBar == (id | 0xC000) && p.m_overweightBar == (id | 0xD000) && p.m_descriptionText == (id | 0xE000) &&
              !p.m_squadSoldier;
        for (uint32_t i = 0; i < 4; ++i)
            ok &= p.m_beltSlots[i] == (id | 0x8000 | (i + 1) << 4) && p.m_backpackSlots[i] == (id | 0xA000 | (i + 1) << 4);
        WM(r.edIds, ok);

        // --- the callbacks: the menu's slots with the callbacks' arguments; the scroll arrows' the
        // panel's OnScroll (no soldier: false) ---
        ok = true;
        C_EpControl control(0x8301000);
        const S_ControlDesc *head = screen.controls[2]->children[0];
        std::vector<std::string> want;
        for (const S_ControlMessage &m : head->messages) {
            if (m.id != CONTROL_MSG_ADD_CALLBACK)
                continue;
            const auto f = reinterpret_cast<ControlCallback>(m.value3);
            ok &= f(reinterpret_cast<void *>(m.value2), reinterpret_cast<void *>(m.value2), &control, m.value, 7);
        }
        menu.answer = false;
        const S_ControlMessage &first = head->messages[1];
        ok &= !reinterpret_cast<ControlCallback>(first.value3)(&menu, &menu, &control, first.value, 0);
        ok &= MsLogIs({"n13 1 8301000 4013000 7", "n0a 1 8301000 400a000 7", "n08 1 8301000 4008000 7",
                       "n09 1 8301000 4009000 7", "hover 1 8301000 4003000 7", "hover 1 8301000 4004000 7",
                       "n13 1 8301000 4013000 0"},
                      std::vector<std::string>(g_gfLog.end() - std::min<size_t>(7, g_gfLog.size()), g_gfLog.end()));
        const S_ControlMessage &arrow = screen.controls[2]->children[6]->children[1]->messages[1];
        p.m_squadSoldier = nullptr;
        ok &= !reinterpret_cast<ControlCallback>(arrow.value3)(reinterpret_cast<void *>(arrow.value2), nullptr, nullptr,
                                                               arrow.value, 0);
        // (Part 9e-xiii: the slot 0x60's, the in-game menu's soldier types' callback.)
        g_gfLog.clear();
        ok &= !C_EquipmentMenuBase::CallItemNotify60(&menu, &menu, &control, CONTROL_NOTIFY_0E, 5);
        menu.answer = true;
        ok &= C_EquipmentMenuBase::CallItemNotify60(&menu, nullptr, &control, CONTROL_NOTIFY_0E, 6) &&
              MsLogIs({"n60 1 8301000 400e000 5", "n60 0 8301000 400e000 6"}, g_gfLog);
        WM(r.edCallbacks, ok);
    }

    // --- no menu: no item callbacks ---
    {
        bool ok = true;
        C_EquipmentPanel p;
        g_edMenu = nullptr;
        g_edPanel = &p;
        reset();
        p.BuildDescription(&screen, As<I3D_frame>(model));
        std::vector<std::string> got;
        EdDump(screen.controls[2], 0, got);
        ok &= MsLogIs(EdExpected(false, true, true), got);
        WM(r.edNoMenu, ok);
    }

    // --- no overload frame, no description frame (or no dummy): neither, the ids after them moved up;
    // the overweight bar and the description text kept ---
    {
        bool ok = true;
        C_EdMenu menu;
        g_edMenu = &menu;
        for (int pass = 0; pass < 3; ++pass) {
            C_EquipmentPanel p;
            p.SetMenu(&menu);
            g_edPanel = &p;
            p.m_overweightBar = 0x55;
            p.m_descriptionText = 0x66;
            g_msChildren.erase({model, "overload"});
            g_msChildren.erase({model, "description"});
            g_msChildren.erase({model, "P_Equipment"});
            if (pass == 1) {
                g_msChildren[{model, "overload"}] = overload;
                g_msChildren[{model, "description"}] = description;
            }
            if (pass == 2)
                g_msChildren[{model, "P_Equipment"}] = dummy;
            reset();
            g_gfLog.clear();
            p.BuildDescription(&screen, As<I3D_frame>(model));
            std::vector<std::string> got;
            EdDump(screen.controls[2], 0, got);
            ok &= MsLogIs(EdExpected(true, pass == 1, false), got);
            ok &= p.m_overweightBar == (pass == 1 ? 0x0830D000u : 0x55u) && p.m_descriptionText == 0x66;
            if (pass == 1)
                ok &= g_gfLog[0] == "smodel 48 overload 1" && g_gfLog[1] == "smodel 48 description 1" &&
                      g_gfLog[2] == "smodel 48 P_Equipment 20" && g_gfLog[3] == "smodel 48 selhead 1";
        }
        WM(r.edOptional, ok);
    }

    while (!screen.controls.empty()) {
        delete screen.controls.back();
        screen.controls.pop_back();
    }
    g_msChildren.clear();
    g_edMenu = g_edPanel = nullptr;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("equipmentdesc: edAddChild %d edTree %d edIds %d edNoMenu %d edOptional %d edCallbacks %d\n",
                r.edAddChild, r.edTree, r.edIds, r.edNoMenu, r.edOptional, r.edCallbacks);
}
