// Part 9e-viii's options handler (game/options_handler.cpp), in the factory build, run by ControlsTest
// after GameMenuTest: a menu_test.inc menu (its screens' controls), the player's saving and key
// bindings logging fakes, world_modules_test.inc's detail settings, IGraph's info. Prints "options:"
// with 1 per check met.
#include <functional>

#define private public
#include "game/options_handler.h"
#undef private

// The player's options saved and applied; the key bindings' unbound function, its text, group and
// place in it.
int g_opUnbound = -1, g_opText, g_opGroup, g_opPosition;
// (Part 9e-x: the player's name; the pages' builders not ported yet: logged.)
C_str g_opName;
C_str C_PlayerProfile::GetName() const { return g_opName; }
std::string MrFrameName(const void *frame);
bool C_OptionsHandler::BuildVideoPage(S_ScreenDesc *desc, I3D_frame *model)
{
    GfLog("page video %d %s", static_cast<int>(desc->controls.size()), MrFrameName(model).c_str());
    return true;
}
bool C_OptionsHandler::BuildSoundPage(S_ScreenDesc *, I3D_frame *)
{
    GfLog("page sound");
    return true;
}
bool C_OptionsHandler::BuildControlsPage(S_ScreenDesc *, I3D_frame *)
{
    GfLog("page controls");
    return true;
}
bool C_OptionsHandler::BuildGamePage(S_ScreenDesc *, I3D_frame *)
{
    GfLog("page game");
    return true;
}
bool C_OptionsHandler::BuildMultiplayerPage(S_ScreenDesc *, I3D_frame *)
{
    GfLog("page multiplayer");
    return true;
}
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
    bool opUnbound = true, opBuild = true, opBuildFail = true, opShown = true;
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

    // --- the screen described (part 9e-x): the group's dummy frame under the model, the pages' buttons
    // found (dummies) and moved under it, the second limits text where the first is; the screen's
    // index kept, the shown callback, the texts, the group's buttons (their pages shown on a click;
    // back: leaving), the pages, the version ---
    // The log's lines starting with `prefix`.
    const auto logHas = [](const char *prefix) {
        return std::count_if(g_gfLog.begin(), g_gfLog.end(),
                             [prefix](const std::string &line) { return line.rfind(prefix, 0) == 0; });
    };
    void *const oldCreate = g_driverVtable[0x54 / 4];
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&MrCreateFrame);
    void *const oldSetPos = g_msFrameVt[0x08 / 4];
    g_msFrameVt[0x08 / 4] = reinterpret_cast<void *>(&MrFake<0x08, const S_vector *>);
    // Its controls as "<name> <type> <id>: [@target] <id> <value> <value2>, ...{children}; ".
    std::function<std::string(const std::vector<S_ControlDesc *> &)> describe = [&](const std::vector<S_ControlDesc *> &cs) {
        std::string s;
        for (const S_ControlDesc *c : cs) {
            s += CmText("%s %x %x:", c->name.c_str(), c->type, c->id);
            for (const S_ControlMessage &m : c->messages)
                s += (m.target ? CmText(" @%x", m.target) : std::string()) + CmText(" %x %x %x", m.id, m.value, static_cast<uint32_t>(m.value2));
            if (!c->children.empty())
                s += " {" + describe(c->children) + "}";
            s += "; ";
        }
        return s;
    };
    const char *const buttonNames[] = {"bvideo setup", "bsound setup", "bcontrols", "bgame setup", "bmultiplayer setup", "bback"};
    {
        bool ok = true;
        S_MrObj *model = MsFrame("opmodel"), *group = MsFrame("opgroup"), *lim = MsFrame("oplim"), *lim2 = MsFrame("oplim2");
        As<I3D_frame>(lim)->m_worldMatrix.m[3][0] = 2.0f;
        As<I3D_frame>(lim)->m_worldMatrix.m[3][2] = -1.5f;
        for (int i = 0; i < 6; ++i)
            g_msChildren[{model, buttonNames[i]}] = MsFrame(CmText("opb%d", i).c_str());
        g_msChildren[{model, "text_min norm max"}] = lim;
        g_msChildren[{model, "text_min norm max02"}] = lim2;
        g_mrMade[FRAME_DUMMY] = {group};
        C_OptionsHandler b(&menu);
        S_ScreenDesc desc("OPTIONS", "options", 2u << 26);
        menu.m_models.clear();
        g_gfLog.clear();
        ok &= b.BuildScreen(&desc, As<I3D_frame>(model)) && b.m_screen == 2;
        ok &= MrHas("opgroup 38 opmodel 0") && MrHas("opgroup 34 GROptions") && MrHas("opgroup 8 (0 0 0)") &&
              MrHas("opmodel 48 bvideo setup 20") && MrHas("opmodel 48 text_min norm max 1") && MrHas("opmodel 48 text_min norm max02 1") &&
              MrHas("oplim2 8 (2 0 -1.5)") && menu.m_models.size() == 1;
        for (int i = 0; i < 6; ++i)
            ok &= MrHas(CmText("opb%d 38 opgroup 1", i));
        // The pages after the buttons, in order (the video page's with the group's 5 controls).
        std::vector<std::string> pages;
        for (const std::string &line : g_gfLog)
            if (line.rfind("page ", 0) == 0)
                pages.push_back(line);
        ok &= pages == std::vector<std::string>({"page video 5 opmodel", "page sound", "page controls", "page game", "page multiplayer"});
        ok &= desc.messages.size() == 1 && desc.messages[0].target == 0 && desc.messages[0].id == CONTROL_MSG_ADD_CALLBACK &&
              desc.messages[0].value == CONTROL_NOTIFY_SHOWN && desc.messages[0].value2 == reinterpret_cast<uintptr_t>(&b) &&
              desc.messages[0].value3 == reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnShown);
        ok &= describe(desc.controls) ==
              "text_screen 13 8100000: 1000015 a 961; text_date00 13 8200000: 1000015 b 962; text_date01 13 8300000: 1000015 c 963; "
              "text_profile00 13 8400000: 1000015 b 964; GROptions 5 8500000: {bvideo setup 14 8501000: 1000015 d 965 @8600000 5001003 0 0; "
              "bsound setup 14 8502000: 1000015 d 966 @8700000 5001003 0 0; bcontrols 14 8503000: 1000015 d 967 @8800000 5001003 0 0; "
              "bgame setup 14 8504000: 1000015 d 968 @8a00000 5001003 0 0; bmultiplayer setup 14 8505000: 1000015 d 969 @8b00000 5001003 0 0; "
              "bback 14 8506000: 1000015 d 96a 60010c4 0 0; }; text_h&dversion 13 8600000: 1000015 28 7d6; ";
        WM(r.opBuild, ok);
        // Without the second limits text: not placed.
        g_msChildren.erase({model, "text_min norm max02"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("opgroup2")};
        S_ScreenDesc desc2("OPTIONS", "options", 3u << 26);
        g_gfLog.clear();
        ok = b.BuildScreen(&desc2, As<I3D_frame>(model)) && b.m_screen == 3 && logHas("oplim2 8") == 0 && desc2.controls.size() == 6;
        WM(r.opBuild, ok);

        // A button missing (any): false, nothing described, nothing moved (the group made all the same).
        for (int missing = 0; missing < 6; ++missing) {
            S_MrObj *const kept = g_msChildren[{model, buttonNames[missing]}];
            g_msChildren.erase({model, buttonNames[missing]});
            g_mrMade[FRAME_DUMMY] = {MsFrame(CmText("opgroupm%d", missing).c_str())};
            C_OptionsHandler f(&menu);
            S_ScreenDesc d("OPTIONS", "options", 2u << 26);
            g_gfLog.clear();
            bool okf = !f.BuildScreen(&d, As<I3D_frame>(model)) && f.m_screen == 0 && d.controls.empty() && d.messages.empty() &&
                       logHas("page") == 0 && MrHas(CmText("opgroupm%d 34 GROptions", missing));
            for (int i = 0; i < 6; ++i)
                okf &= logHas(CmText("opb%d 38", i).c_str()) == 0;
            g_msChildren[{model, buttonNames[missing]}] = kept;
            WM(r.opBuildFail, okf);
        }
        g_msChildren.clear();
        menu.m_models.clear();
    }

    // --- shown: the player's name on the third control (style 0xC); none: its text 0x7E4 ---
    {
        bool ok = true;
        C_OptionsHandler s(&menu);
        s.m_screen = 2;
        g_controlMessages.clear();
        g_opName = C_str("Kowalski");
        ok &= C_OptionsHandler::OnShown(&s, nullptr, nullptr, CONTROL_NOTIFY_SHOWN, 0) &&
              ScQueue() == "8300000 1000014 c 0 Kowalski; ";
        g_opName = C_str();
        ok &= C_OptionsHandler::OnShown(&s, nullptr, nullptr, CONTROL_NOTIFY_SHOWN, 0) && ScQueue() == "8300000 1000015 c 7e4 ; ";
        g_opName = C_str("");
        ok &= C_OptionsHandler::OnShown(&s, nullptr, nullptr, 0, 0) && ScQueue() == "8300000 1000015 c 7e4 ; ";
        WM(r.opShown, ok);
    }
    g_msFrameVt[0x08 / 4] = oldSetPos;
    g_driverVtable[0x54 / 4] = oldCreate;

    *options = oldOptions;
    g_opInfo.flags = 0;
    g_igraphVtable[0x04 / 4] = oldInfo;
    g_gameProfile.m_player = oldPlayer;
    g_gfLog.clear();
    g_controlMessages.clear();
    std::printf("options: opNew %d opEscape %d opSave %d opOpen %d opFill %d opRefresh %d opLeave %d opUnbound %d "
                "opBuild %d opBuildFail %d opShown %d\n",
                r.opNew, r.opEscape, r.opSave, r.opOpen, r.opFill, r.opRefresh, r.opLeave, r.opUnbound, r.opBuild,
                r.opBuildFail, r.opShown);
}
