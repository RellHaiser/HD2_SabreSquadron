// Part 9e-viii's options handler (game/options_handler.cpp), in the factory build, run by ControlsTest
// after GameMenuTest: a menu_test.inc menu (its screens' controls), the player's saving and key
// bindings logging fakes, world_modules_test.inc's detail settings, IGraph's info. Prints "options:"
// with 1 per check met.
#define private public
#include "game/options_handler.h"
#undef private

// The player's options saved and applied; the key bindings' unbound function, its text, group and
// place in it.
int g_opUnbound = -1, g_opText, g_opGroup, g_opPosition;
bool C_PlayerProfile::SaveOptions()
{
    GfLog("save options");
    return true;
}
void C_PlayerProfile::ApplyHardwareOption() { GfLog("hardware option"); }
void C_KeyBindings::ApplyChanged() { GfLog("keys applied"); }
int C_KeyBindings::FindUnbound() const { return g_opUnbound; }
void C_KeyBindings::GetTextAndGroup(int index, int *textId, int *group) const
{
    GfLog("text and group %d", index);
    *textId = g_opText;
    *group = g_opGroup;
}
void C_KeyBindings::GetGroupPosition(int index, int *position) const
{
    GfLog("group position %d", index);
    *position = g_opPosition;
}

namespace {

struct S_OptionsResults {
    bool opNew = true, opEscape = true, opSave = true, opOpen = true, opFill = true, opRefresh = true, opLeave = true;
    bool opUnbound = true;
};

S_IGraphInfo g_opInfo;
const S_IGraphInfo *__stdcall OpGetInfo(void *) { return &g_opInfo; }

// The key groups' list: its value the group chosen (<< 12).
uint32_t g_opChosen;
struct C_OpList : C_Control {
    explicit C_OpList(uint32_t id) : C_Control(nullptr, id, nullptr) {}
    bool HandleMessage(S_ControlMessage *message) override
    {
        GfLog("list asked %x", message->id);
        message->value = g_opChosen << 12;
        return true;
    }
};

} // namespace

void OptionsTest(I3D_scene *scene)
{
    S_OptionsResults r;
    g_gfLog.clear();
    g_controlMessages.clear();
    C_PlayerProfile player;
    C_PlayerProfile *const oldPlayer = g_gameProfile.m_player;
    g_gameProfile.m_player = &player;
    void *const oldInfo = g_igraphVtable[0x04 / 4];
    g_igraphVtable[0x04 / 4] = reinterpret_cast<void *>(&OpGetInfo);
    C_MsMenu menu;

    // --- made: no cursor, the menu's, filled on the first open, screen 0; deleted: its cursor released ---
    {
        auto *h = new C_OptionsHandler(&menu);
        bool ok = !h->m_cursor && h->m_menu == &menu && h->m_fillOnOpen && h->m_screen == 0;
        h->m_cursor = C_Cursor::Acquire(scene);
        g_gfLog.clear();
        delete h;
        ok &= MsLogIs({"cursor release"}, g_gfLog);
        WM(r.opNew, ok);
    }

    C_OptionsHandler h(&menu);
    h.m_screen = 5;
    const auto handle = [&](uint32_t id) {
        S_ControlMessage message;
        message.id = id;
        g_gfLog.clear();
        g_controlMessages.clear();
        return h.HandleMessage(&message);
    };

    // --- escape on the options screen (the top's index): leaving it; else (no screen) not taken ---
    {
        bool ok = !handle(MENU_MSG_ESCAPE) && g_controlMessages.empty();
        C_MsScreen options(scene, "opscreen", 0x14000005), other(scene, "opother", 0x18000005);
        g_menuScreens = {&other, &options};
        ok &= handle(MENU_MSG_ESCAPE) && ScQueue() == "0 20000c4 0 0 ; ";
        // (The code's flag bits masked.)
        ok &= handle(MENU_MSG_ESCAPE | 0x0C000000) && ScQueue() == "0 20000c4 0 0 ; ";
        g_menuScreens = {&options, &other};
        ok &= !handle(MENU_MSG_ESCAPE) && g_controlMessages.empty();
        g_menuScreens.clear();
        // Others not taken.
        for (uint32_t id : {0x2000070u, 0x2000072u, 0x20000C5u, 0x20000C6u, 0x200006Eu, 0x2000009u, 0x200000Bu})
            ok &= !handle(id) && g_controlMessages.empty() && g_gfLog.empty();
        WM(r.opEscape, ok);
    }

    // --- saved: the options, the hardware option, the key bindings changed ---
    WM(r.opSave, handle(0x200006F) && MsLogIs({"save options", "hardware option", "keys applied"}, g_gfLog) &&
                     g_controlMessages.empty());

    // --- opened: the first time the controls filled from the options (the gamma's only with IGraph's
    // gamma; the voice's value only with the voice chat), the key groups refreshed (no list: none);
    // the screen opened ---
    auto *options = g_gameProfile.GetDetailSettings();
    const auto oldOptions = *options;
    {
        bool ok = true;
        options->mouseSensitivity = 33.9f;
        options->gamma = 50.7f;
        options->soundVolume = 80.2f;
        options->musicVolume = -3.7f;
        options->voiceValue = 7.99f;
        options->level = 2;
        options->invertMouse = true;
        options->unknown3A = false;
        options->voiceChat = true;
        options->crosshair = false;
        g_opInfo.flags = 2;
        ok &= handle(0x2000071) && !h.m_fillOnOpen &&
              ScQueue() == "14604000 1000002 0 0 ; 14604000 200002b 32 0 ; 14706000 200002b 50 0 ; "
                           "14707000 200002b fffffffd 0 ; 14806000 200002b 21 0 ; 14605000 100006d 2 0 ; "
                           "14804000 1000076 1 0 ; 14a02000 1000076 0 0 ; 14b04000 1000076 1 0 ; "
                           "14b06000 200002b 7 0 ; 14a03000 1000076 0 0 ; 14b06000 1000002 0 0 ; 0 2000001 5 0 ; ";
        // Again: opened only.
        ok &= handle(0x2000071) && ScQueue() == "0 2000001 5 0 ; ";
        WM(r.opOpen, ok);
    }
    {
        bool ok = true;
        h.m_fillOnOpen = true;
        options->invertMouse = false;
        options->unknown3A = true;
        options->voiceChat = false;
        options->crosshair = true;
        g_opInfo.flags = 0xFD;
        ok &= handle(0x2000071) && !h.m_fillOnOpen;
        const std::string q = ScQueue();
        ok &= q.find("14604000 1000001 0 0 ; ") == 0 && q.find("14804000 1000076 0 0 ; 14a02000 1000076 1 0 ; ") != std::string::npos &&
              q.find("14b04000 1000076 0 0 ; ") != std::string::npos &&
              q.find("14a03000 1000076 1 0 ; 14b06000 1000001 0 0 ; 0 2000001 5 0 ; ") != std::string::npos;
        WM(r.opFill, ok);
    }

    // --- the key groups refreshed: groups 4 .. 8, the list's choice last (asked by its value) ---
    {
        bool ok = true;
        C_MsScreen *screens[6];
        for (int i = 0; i < 6; ++i)
            screens[i] = new C_MsScreen(scene, CmText("ops%d", i).c_str(), static_cast<uint32_t>(i) << 26);
        menu.m_screens.assign(screens, screens + 6);
        auto *list = new C_OpList(0x14903000);
        screens[5]->m_children.push_back(list);
        g_opChosen = 6;
        g_gfLog.clear();
        g_controlMessages.clear();
        ok &= h.RefreshKeyGroups() && MsLogIs({"list asked 100000c"}, g_gfLog) &&
              ScQueue() == "14904000 200002c 0 0 ; 14905000 200002c 0 0 ; 14907000 200002c 0 0 ; 14908000 200002c 0 0 ; "
                           "14906000 200002c 0 0 ; ";
        // (Outside 4 .. 8: all five, then that one.)
        g_opChosen = 0x1F;
        ok &= h.RefreshKeyGroups() && ScQueue() == "14904000 200002c 0 0 ; 14905000 200002c 0 0 ; 14906000 200002c 0 0 ; "
                                                   "14907000 200002c 0 0 ; 14908000 200002c 0 0 ; 1491f000 200002c 0 0 ; ";
        // (The value's second byte: 0x186 is 0x86.)
        g_opChosen = 0x186;
        ok &= h.RefreshKeyGroups() && ScQueue() == "14904000 200002c 0 0 ; 14905000 200002c 0 0 ; 14906000 200002c 0 0 ; "
                                                   "14907000 200002c 0 0 ; 14908000 200002c 0 0 ; 14986000 200002c 0 0 ; ";
        // Opened with the list: the refresh after the controls.
        h.m_fillOnOpen = true;
        g_opChosen = 4;
        ok &= handle(0x2000071);
        const std::string q = ScQueue();
        ok &= q.find("14b06000 1000001 0 0 ; 14905000 200002c 0 0 ; ") != std::string::npos &&
              q.find("14904000 200002c 0 0 ; 0 2000001 5 0 ; ") != std::string::npos;
        // No list: false.
        screens[5]->m_children.clear();
        delete list;
        ok &= !h.RefreshKeyGroups() && g_controlMessages.empty();
        menu.m_screens.clear();
        for (C_MsScreen *s : screens)
            delete s;
        WM(r.opRefresh, ok);
    }

    // --- leaving: a function without a key: the OK box asking (its answers the unbound's page); else
    // the pages hidden, the save and back posted ---
    {
        bool ok = true;
        C_MessageBox box(scene, GfFrame(FRAME_DUMMY, "opbox"));
        menu.m_messageBox = &box;
        g_opUnbound = 12;
        ok &= handle(0x20000C4) && box.m_kind == MESSAGE_BOX_OK && box.m_yesMessage == 0x20000C7 &&
              box.m_noMessage == 0x20000C7 && ScQueue().find("100000 1000015 18 2530 ; ") != std::string::npos;
        menu.CloseMessage();
        menu.m_messageBox = nullptr;
        g_opUnbound = -1;
        ok &= handle(0x20000C4) && ScQueue() == "14600000 1000004 0 0 ; 14700000 1000004 0 0 ; 14800000 1000004 0 0 ; "
                                                 "14900000 1000004 0 0 ; 14a00000 1000004 0 0 ; 14b00000 1000004 0 0 ; "
                                                 "0 600106f 0 0 ; 0 6001003 0 0 ; ";
        WM(r.opLeave, ok);
    }

    // --- the unbound function shown: the keys' page alone, its group chosen, its place in the group's
    // list (none at 0) ---
    {
        bool ok = true;
        g_opUnbound = 12;
        g_opText = 0x99;
        g_opGroup = 2;
        g_opPosition = 5;
        ok &= handle(0x20000C7) && MsLogIs({"text and group 12", "group position 12"}, g_gfLog) &&
              ScQueue() == "14600000 1000004 0 0 ; 14700000 1000004 0 0 ; 14800000 1000004 0 0 ; "
                           "14900000 1000003 0 0 ; 14a00000 1000004 0 0 ; 14b00000 1000004 0 0 ; "
                           "14902000 100006d 2 0 ; 14906000 20000aa 4 0 ; ";
        g_opPosition = 0;
        ok &= handle(0x20000C7);
        const std::string q = ScQueue();
        ok &= q.find("14902000 100006d 2 0 ; ") != std::string::npos && q.find("20000aa") == std::string::npos;
        g_opPosition = 1;
        // (The group's list by the low byte of the group + 4.)
        g_opGroup = 0x1FC;
        handle(0x20000C7);
        ok &= ScQueue().find("14902000 100006d 1fc 0 ; 14900000 20000aa 0 0 ; ") != std::string::npos;
        g_opUnbound = -1;
        g_opGroup = g_opText = g_opPosition = 0;
        WM(r.opUnbound, ok);
    }

    *options = oldOptions;
    g_opInfo.flags = 0;
    g_igraphVtable[0x04 / 4] = oldInfo;
    g_gameProfile.m_player = oldPlayer;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("options: opNew %d opEscape %d opSave %d opOpen %d opFill %d opRefresh %d opLeave %d opUnbound %d\n",
                r.opNew, r.opEscape, r.opSave, r.opOpen, r.opFill, r.opRefresh, r.opLeave, r.opUnbound);
}
