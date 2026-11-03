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
// (Part 9e-xi: the player's options, applied: logged; the switches.)
S_DetailSettings *C_PlayerProfile::GetOptions() { return g_gameProfile.GetDetailSettings(); }
void C_PlayerProfile::ApplyGamma() { GfLog("apply gamma"); }
void C_PlayerProfile::ApplyMusicVolume() { GfLog("apply music"); }
void C_PlayerProfile::ApplyMouse() { GfLog("apply mouse"); }
void C_PlayerProfile::ApplyVoiceValue() { GfLog("apply voice"); }
void S_DetailSettings::SetSwitch(int option, bool on) { GfLog("switch %d %d", option, on); }
bool C_PlayerProfile::SaveOptions()
{
    GfLog("save options");
    return true;
}
void C_PlayerProfile::ApplyHardwareOption() { GfLog("hardware option"); }
void C_KeyBindings::ApplyChanged() { GfLog("keys applied"); }
int C_KeyBindings::FindUnbound() const { return g_opUnbound; }
// (Part 9e-xii: per binding, when given: the text 0x500 + its index, its group.)
std::vector<int> g_opBindingGroups;
void C_KeyBindings::GetTextAndGroup(int index, int *textId, int *group) const
{
    if (index >= 0 && index < static_cast<int>(g_opBindingGroups.size())) {
        *textId = 0x500 + index;
        *group = g_opBindingGroups[static_cast<size_t>(index)];
        return;
    }
    GfLog("text and group %d", index);
    *textId = g_opText;
    *group = g_opGroup;
}
void C_PlayerProfile::SetKeyLayout(int layout) { GfLog("layout %d", layout); }
void C_KeyBindings::GetGroupPosition(int index, int *position) const
{
    GfLog("group position %d", index);
    *position = g_opPosition;
}

namespace {

struct S_OptionsResults {
    bool opNew = true, opEscape = true, opSave = true, opOpen = true, opFill = true, opRefresh = true, opLeave = true;
    bool opUnbound = true, opBuild = true, opBuildFail = true, opShown = true;
    bool opVideo = true, opSound = true, opGame = true, opNetwork = true, opSlider = true, opDetail = true, opCheck = true;
    bool opVoice = true, opControls = true, opKeyGroup = true, opKeys = true;
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

// A control answering CONTROL_MSG_GET_SELECTED with its index (logged).
struct C_OpSelect : C_Control {
    int index;
    C_OpSelect(uint32_t id, int i) : C_Control(nullptr, id, nullptr), index(i) {}
    bool HandleMessage(S_ControlMessage *message) override
    {
        GfLog("selected %x", message->id);
        message->value = static_cast<uint32_t>(index);
        return true;
    }
};
// A control answering CONTROL_MSG_GET_SLIDER with its position (logged).
struct C_OpSlider : C_Control {
    float position;
    C_OpSlider(uint32_t id, float p) : C_Control(nullptr, id, nullptr), position(p) {}
    bool HandleMessage(S_ControlMessage *message) override
    {
        GfLog("asked %x", message->id);
        uint32_t bits;
        std::memcpy(&bits, &position, sizeof bits);
        message->value2 = bits;
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
        options->subtitles = false;
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
        options->subtitles = true;
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
        // The pages after the buttons, in order (the controls' after the video and sound pages).
        ok &= desc.controls.size() == 12;
        if (desc.controls.size() == 12) {
            const char *const pageNames[] = {"graphics setup", "sound setup", "keysscreen", "keysscreen01", "gameoptions",
                                             "network settings"};
            for (int i = 0; i < 6; ++i)
                ok &= desc.controls[5 + static_cast<size_t>(i)]->name == pageNames[i];
        }
        ok &= desc.messages.size() == 1 && desc.messages[0].target == 0 && desc.messages[0].id == CONTROL_MSG_ADD_CALLBACK &&
              desc.messages[0].value == CONTROL_NOTIFY_SHOWN && desc.messages[0].value2 == reinterpret_cast<uintptr_t>(&b) &&
              desc.messages[0].value3 == reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnShown);
        ok &= desc.controls.size() == 12 &&
              describe(std::vector<S_ControlDesc *>(desc.controls.begin(), desc.controls.begin() + 5)) ==
              "text_screen 13 8100000: 1000015 a 961; text_date00 13 8200000: 1000015 b 962; text_date01 13 8300000: 1000015 c 963; "
              "text_profile00 13 8400000: 1000015 b 964; GROptions 5 8500000: {bvideo setup 14 8501000: 1000015 d 965 @8600000 5001003 0 0; "
              "bsound setup 14 8502000: 1000015 d 966 @8700000 5001003 0 0; bcontrols 14 8503000: 1000015 d 967 @8800000 5001003 0 0; "
              "bgame setup 14 8504000: 1000015 d 968 @8a00000 5001003 0 0; bmultiplayer setup 14 8505000: 1000015 d 969 @8b00000 5001003 0 0; "
              "bback 14 8506000: 1000015 d 96a 60010c4 0 0; }; " &&
              describe({desc.controls.back()}) == "text_h&dversion 13 8c00000: 1000015 28 7d6; ";
        WM(r.opBuild, ok);
        // Without the second limits text: not placed.
        g_msChildren.erase({model, "text_min norm max02"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("opgroup2")};
        S_ScreenDesc desc2("OPTIONS", "options", 3u << 26);
        g_gfLog.clear();
        ok = b.BuildScreen(&desc2, As<I3D_frame>(model)) && b.m_screen == 3 && logHas("oplim2 8") == 0 && desc2.controls.size() == 12;
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

    // --- the pages (part 9e-xi), each on its own description: the group (hidden; when shown, the
    // other pages hidden), its texts and controls with their callbacks (by name: h the handler) ---
    {
        C_OptionsHandler p(&menu);
        p.m_screen = 2;
        const std::map<uintptr_t, std::string> callbacks = {
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnSlider), "slider"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnDetail), "detail"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnCheck), "check"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnVoiceChat), "voice"}};
        std::function<std::string(const std::vector<S_ControlDesc *> &)> pageText = [&](const std::vector<S_ControlDesc *> &cs) {
            std::string s;
            for (const S_ControlDesc *c : cs) {
                s += CmText("%s %x %x:", c->name.c_str(), c->type, c->id);
                for (const S_ControlMessage &m : c->messages) {
                    if (m.id == CONTROL_MSG_ADD_CALLBACK) {
                        const auto it = callbacks.find(m.value3);
                        s += (m.target ? CmText(" @%x", m.target) : std::string()) +
                             CmText(" cb %x %s %s", m.value, m.value2 == reinterpret_cast<uintptr_t>(&p) ? "h" : "?",
                                    it == callbacks.end() ? "?" : it->second.c_str());
                    } else {
                        s += (m.target ? CmText(" @%x", m.target) : std::string()) +
                             CmText(" %x %x %x", m.id, m.value, static_cast<uint32_t>(m.value2));
                    }
                }
                if (!c->children.empty())
                    s += " {" + pageText(c->children) + "}";
                s += "; ";
            }
            return s;
        };
        const auto hides = [](std::initializer_list<uint32_t> pages) {
            std::string s = " 1000004 0 0";
            for (uint32_t page : pages)
                s += CmText(" @%x 500d004 0 0", 0x08000000 | page);
            return s;
        };
        const std::string limits = " 1000017 1b 7ee 1000017 1c 7ef 1000017 1d 7f0";

        // The video page: the switches' frames named first (those found).
        S_MrObj *model = MsFrame("opvmodel"), *nd = MsFrame("opnd"), *mo = MsFrame("opmo");
        g_msChildren[{model, "nextdetail"}] = nd;
        g_msChildren[{model, "morphing"}] = mo;
        S_ScreenDesc video("OPTIONS", "options", 2u << 26);
        g_gfLog.clear();
        bool ok = p.BuildVideoPage(&video, As<I3D_frame>(model)) && MrHas("opnd 34 GD00") && MrHas("opmo 34 GD03") &&
                  logHas("opnd 34") == 1 && logHas("opvmodel 48") == 7;
        for (const char *name : {"nextdetail", "shadows", "mirrors", "morphing", "dynamic lightning", "light glow", "use high models"})
            ok &= MrHas(CmText("opvmodel 48 %s 1", name));
        ok &= pageText(video.controls) ==
              "graphics setup 5 8100000:" + hides({0x700000, 0x800000, 0x900000, 0xA00000, 0xB00000}) +
                  " {text_graphics setup 13 8101000: 1000015 b 96b; text_gama correction 13 8102000: 1000015 b 96c; "
                  "text_min norm max02 13 8103000:" + limits + "; scrollbar03 8 8104000: 1000020 64 0 cb 4010000 h slider; "
                  "geometry detail 10 8105000: 1000067 96e 0 1000067 96f 0 1000067 970 0 1000067 971 0 cb 4010000 h detail; "
                  "GD00 24 8106000: 1000015 d 972 cb 4010000 h detail; GD01 24 8107000: 1000015 d 973 cb 4010000 h detail; "
                  "GD02 24 8108000: 1000015 d 974 cb 4010000 h detail; GD03 24 8109000: 1000015 d 975 cb 4010000 h detail 1000001 0 0; "
                  "GD04 24 810a000: 1000015 d 976 cb 4010000 h detail; GD05 24 810b000: 1000015 d 977 cb 4010000 h detail; "
                  "GD06 24 810c000: 1000015 d 978 cb 4010000 h detail 1000004 0 0; "
                  "text_modifications 13 810d000: 1000017 b 97e; }; ";
        g_msChildren.clear();
        WM(r.opVideo, ok);

        S_ScreenDesc sound("OPTIONS", "options", 2u << 26);
        WM(r.opSound, p.BuildSoundPage(&sound, nullptr) &&
                          pageText(sound.controls) ==
                              "sound setup 5 8100000:" + hides({0x600000, 0x800000, 0x900000, 0xA00000, 0xB00000}) +
                                  " {text_sound setup 13 8101000: 1000015 b 979; text_sound volume 13 8102000: 1000015 b 97a; "
                                  "text_min norm max 13 8103000:" + limits + "; text_music volume 13 8104000: 1000015 b 97c; "
                                  "text_min norm max01 13 8105000: 2000073 1 0" + limits + "; "
                                  "scrollbar01 8 8106000: 1000020 64 0 cb 4010000 h slider; "
                                  "scrollbar02 8 8107000: 1000020 64 0 cb 4010000 h slider; }; ");

        S_ScreenDesc game("OPTIONS", "options", 2u << 26);
        WM(r.opGame, p.BuildGamePage(&game, nullptr) &&
                         pageText(game.controls) ==
                             "gameoptions 5 8100000:" + hides({0x600000, 0x700000, 0x800000, 0x900000, 0xB00000}) +
                                 " {text_game options 13 8101000: 1000015 b 99d; subtitles 24 8102000: 1000015 d 99e cb 4010000 h check; "
                                 "parental lock 24 8103000: 1000015 d 4ef4 cb 4010000 h check 1000001 0 0; }; ");

        S_ScreenDesc network("OPTIONS", "options", 2u << 26);
        WM(r.opNetwork, p.BuildMultiplayerPage(&network, nullptr) &&
                            pageText(network.controls) ==
                                "network settings 5 8100000:" + hides({0x600000, 0x700000, 0x800000, 0x900000, 0xA00000}) +
                                    " 1000004 0 0 1000004 0 0 {text_connection rate 13 8101000:; connection 10 8102000:; "
                                    "text_voice-chat settings 13 8103000: 1000015 b 981; "
                                    "enable voice-chat 24 8104000: 1000015 d 97f cb 4010000 h voice; "
                                    "text_voice-chat volume 13 8105000: 1000015 b 980; scrollbar04 8 8106000: 1000020 64 0 cb 4010000 h slider; "
                                    "text_min norm max020 13 8107000: 2000073 1 0" + limits + "; "
                                    "text_network setup 13 8108000: 1000015 b 9a9; }; ");
    }

    // --- the pages' callbacks: the sliders' positions (asked) into the options (applied), the detail
    // level and switches, the check boxes, the voice chat; not theirs: false ---
    {
        C_OptionsHandler h(&menu);
        h.m_screen = 2;
        auto *opts = g_gameProfile.GetDetailSettings();
        const auto saved = *opts;
        menu.m_clickSound = As<I3D_sound>(MrObj("opclick", g_mrSoundVt));
        const auto slide = [&](uint32_t id, float position) {
            C_OpSlider slider(0x08000000 | id, position);
            g_gfLog.clear();
            return C_OptionsHandler::OnSlider(&h, nullptr, &slider, CONTROL_NOTIFY_CHANGED, 0);
        };
        bool ok = slide(0x604000, 33.5f) && opts->gamma == 33.5f && MsLogIs({"asked 1000022", "apply gamma"}, g_gfLog);
        ok &= slide(0x706000, 61.25f) && opts->soundVolume == 61.25f &&
              MsLogIs({"asked 1000022", "volume", "opclick 78 0", "opclick on 1"}, g_gfLog);
        ok &= slide(0x707000, 0.5f) && opts->musicVolume == 0.0f && MsLogIs({"asked 1000022", "apply music"}, g_gfLog);
        ok &= slide(0x707000, 1.0f) && opts->musicVolume == 1.0f && slide(0x707000, std::nanf("")) && std::isnan(opts->musicVolume);
        ok &= slide(0x806000, 12.0f) && opts->mouseSensitivity == 12.0f && MsLogIs({"asked 1000022", "apply mouse"}, g_gfLog);
        ok &= slide(0xB06000, 7.0f) && opts->voiceValue == 7.0f && MsLogIs({"asked 1000022", "apply voice"}, g_gfLog);
        ok &= !slide(0x605000, 3.0f) && MsLogIs({"asked 1000022"}, g_gfLog) && !slide(0x04706000, 3.0f);
        ok &= !C_OptionsHandler::OnSlider(&h, nullptr, nullptr, 0, 0);
        WM(r.opSlider, ok);

        // The detail level: kept below 4; the switches' states (by the level), active at level 3.
        const auto detail = [&](uint32_t id, uint32_t value) {
            C_OpSlider control(0x08000000 | id, 0.0f);
            g_gfLog.clear();
            g_controlMessages.clear();
            return C_OptionsHandler::OnDetail(&h, nullptr, &control, CONTROL_NOTIFY_CHANGED, value);
        };
        for (int i = 0; i < 8; ++i)
            opts->switches[i] = i % 2 == 0;
        ok = detail(0x605000, 2) && opts->level == 2 && MsLogIs({"hardware option"}, g_gfLog);
        std::string want;
        for (uint32_t i = 0; i < 7; ++i)
            want += CmText("%x 1000076 %x 0 ; ", 0x08606000 + (i << 12), i % 2 == 0);
        for (uint32_t i = 0; i < 7; ++i)
            want += CmText("%x 1000001 0 0 ; ", 0x08606000 + (i << 12));
        ok &= ScQueue() == want;
        ok &= detail(0x605000, 4) && opts->level == 2 && detail(0x605000, 3) && opts->level == 3 &&
              ScQueue().find("8606000 1000002 0 0 ; ") != std::string::npos;
        ok &= detail(0x606000, 1) && MsLogIs({"switch 0 1"}, g_gfLog) && g_controlMessages.empty();
        ok &= detail(0x60C000, 0x100) && MsLogIs({"switch 6 0"}, g_gfLog);
        ok &= detail(0x60D000, 1) && g_gfLog.empty() && detail(0x604000, 1) && g_gfLog.empty();
        // (The index's whole byte: 0x15 neither the list nor a switch.)
        ok &= detail(0x615000, 1) && g_gfLog.empty() && g_controlMessages.empty() && opts->level == 3;
        ok &= !C_OptionsHandler::OnDetail(&h, nullptr, nullptr, 0, 0);
        WM(r.opDetail, ok);

        // The check boxes (the value's low byte).
        const auto check = [&](uint32_t id, uint32_t value) {
            C_OpSlider control(0x08000000 | id, 0.0f);
            g_gfLog.clear();
            return C_OptionsHandler::OnCheck(&h, nullptr, &control, CONTROL_NOTIFY_CHANGED, value);
        };
        opts->invertMouse = opts->subtitles = opts->crosshair = false;
        ok = check(0x804000, 1) && opts->invertMouse && MsLogIs({"apply mouse"}, g_gfLog);
        ok &= check(0xA02000, 0x101) && opts->subtitles && g_gfLog.empty() && check(0xA02000, 0x100) && !opts->subtitles;
        ok &= check(0xA03000, 1) && opts->crosshair && check(0xA03000, 0x100) && !opts->crosshair;
        ok &= !check(0xA04000, 1) && !check(0x04804000, 1) && !C_OptionsHandler::OnCheck(&h, nullptr, nullptr, 0, 1);
        WM(r.opCheck, ok);

        // The voice chat (its low byte), its volume active by the whole value.
        g_controlMessages.clear();
        ok = C_OptionsHandler::OnVoiceChat(&h, nullptr, nullptr, 0, 1) && opts->voiceChat && ScQueue() == "8b06000 1000002 0 0 ; ";
        ok &= C_OptionsHandler::OnVoiceChat(&h, nullptr, nullptr, 0, 0x100) && !opts->voiceChat &&
              ScQueue() == "8b06000 1000002 0 0 ; ";
        ok &= C_OptionsHandler::OnVoiceChat(&h, nullptr, nullptr, 0, 0) && !opts->voiceChat && ScQueue() == "8b06000 1000001 0 0 ; ";
        WM(r.opVoice, ok);
        menu.m_clickSound = nullptr;
        *opts = saved;
    }

    // --- the controls and keys pages (part 9e-xii): the controls page (the joystick's hidden), the
    // keys page with the groups' list, its scroll bar, a list per key group (its bindings' rows: copies
    // of "hkinput" under the group's dummy, 0.036 apart), the layouts' buttons ---
    {
        C_OptionsHandler k(&menu);
        k.m_screen = 2;
        const std::map<uintptr_t, std::string> callbacks = {
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnSlider), "slider"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnCheck), "check"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnKeyGroup), "keygroup"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnKeyChanged), "keychanged"},
            {reinterpret_cast<uintptr_t>(&C_OptionsHandler::OnKeyLayout), "layout"}};
        std::function<std::string(const std::vector<S_ControlDesc *> &)> text = [&](const std::vector<S_ControlDesc *> &cs) {
            std::string s;
            for (const S_ControlDesc *c : cs) {
                s += CmText("%s %x %x:", c->name.c_str(), c->type, c->id);
                for (const S_ControlMessage &m : c->messages) {
                    s += m.target ? CmText(" @%x", m.target) : std::string();
                    if (m.id == CONTROL_MSG_ADD_CALLBACK) {
                        const auto it = callbacks.find(m.value3);
                        s += CmText(" cb %x %s %s", m.value, m.value2 == reinterpret_cast<uintptr_t>(&k) ? "h" : "?",
                                    it == callbacks.end() ? "?" : it->second.c_str());
                    } else {
                        s += CmText(" %x %x %x", m.id, m.value, static_cast<uint32_t>(m.value2));
                    }
                }
                if (!c->children.empty())
                    s += " {" + text(c->children) + "}";
                s += "; ";
            }
            return s;
        };
        std::string hides = " 1000004 0 0";
        for (uint32_t page : {0x600000u, 0x700000u, 0x900000u, 0xA00000u, 0xB00000u})
            hides += CmText(" @%x 500d004 0 0", 0x08000000 | page);
        const std::string limits = " 1000017 1b 7ee 1000017 1c 7ef 1000017 1d 7f0";
        const std::string controlsPage =
            "keysscreen 5 8100000:" + hides +
            " {text_controls setup 13 8101000: 1000015 b 989; bcontrols01 14 8102000: 1000015 d 98a @8900000 5001003 0 0; "
            "text_mouse setup 13 8103000: 1000015 b 98b; invert mouse 24 8104000: 1000015 d 98c cb 4010000 h check; "
            "text_mouse sensitivity 13 8105000: 1000015 b 98d; scrollbar 8 8106000: 1000020 64 0 cb 4010000 h slider; "
            "text_minimal normal maximal 13 8107000:" + limits + "; text_joystick setup 13 8108000: 1000015 b 98f 1000004 0 0; "
            "bcalibrate 14 8109000: 1000015 d 990 1000004 0 0; force feedback 24 810a000: 1000015 d 991 1000004 0 0; "
            "lock fire 24 810b000: 1000015 d 992 1000004 0 0; }; ";
        const std::string keysHead =
            "keysscreen01 5 8200000: 1000004 0 0 @8800000 500d004 0 0 {text_last save01 13 8201000: 1000015 b 993; "
            "controls 10 8202000: 1000067 994 0 1000067 995 0 1000067 996 0 1000067 997 0 1000067 998 0 cb 4010000 h keygroup; "
            "scroll00 8 8203000: 100000b 8904000 0; ";
        const std::string listHead = ": 1000028 9 0 100000b 8903000 0 100002a 0 3d1374bc";

        // The bindings: four, of the groups 0, 1, 0 and 4.
        C_KeyBindings &bindings = *g_gameProfile.m_player->GetKeyBindings();
        bindings.m_bindings.resize(4);
        g_opBindingGroups = {0, 1, 0, 4};
        S_MrObj *model = MsFrame("opcmodel"), *input = MsFrame("ophk"), *hktext = MsFrame("ophkt"), *screen = MsFrame("opks");
        As<I3D_frame>(input)->m_localMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
        g_msChildren[{model, "hkinput"}] = input;
        g_msChildren[{model, "hktext"}] = hktext;
        g_msChildren[{model, "keysscreen01"}] = screen;
        S_MrObj *dummies[5], *clones[4];
        for (int i = 0; i < 5; ++i)
            dummies[i] = MsFrame(CmText("opkg%d", i).c_str());
        for (int i = 0; i < 4; ++i)
            clones[i] = MsFrame(CmText("opclone%d", i).c_str());
        g_mrMade[FRAME_DUMMY] = {dummies[0], dummies[1], dummies[2], dummies[3], dummies[4]};
        g_mrClones = {As<I3D_frame>(clones[0]), As<I3D_frame>(clones[1]), As<I3D_frame>(clones[2]), As<I3D_frame>(clones[3])};
        menu.m_models.clear();
        menu.m_frames.clear();
        S_ScreenDesc desc("OPTIONS", "options", 2u << 26);
        g_gfLog.clear();
        bool ok = k.BuildControlsPage(&desc, As<I3D_frame>(model)) && desc.controls.size() == 2;
        ok &= text({desc.controls[0]}) == controlsPage;
        ok &= text({desc.controls[1]}) ==
              keysHead + "GR_Movements b 8204000" + listHead +
                  " 200002c 0 0 {Control 0 d 8204010: 100003d 0 500 cb 4010000 h keychanged; "
                  "Control 2 d 8204020: 100003d 2 502 cb 4010000 h keychanged; }; "
                  "GR_Weapons b 8205000" + listHead + " 1000004 0 0 {Control 1 d 8205010: 100003d 1 501 cb 4010000 h keychanged; }; "
                  "GR_Other b 8206000" + listHead + " 1000004 0 0; GR_Selections b 8207000" + listHead + " 1000004 0 0; "
                  "GR_Tactical b 8208000" + listHead + " 1000004 0 0 {Control 3 d 8208010: 100003d 3 503 cb 4010000 h keychanged; }; "
                  "bdefault* 14 8209000: 1000015 d 999 cb 4001000 h layout; bback01 14 820a000: 1000015 d 99b cb 4001000 h layout; }; ";
        // The frames: the text under the input; per group its dummy under the keys screen (named, placed,
        // the menu's), the input under it (on) for the rows' copies (named, 0.036 apart, under the dummy).
        ok &= MrHas("ophkt 38 ophk 1") && MrHas("opkg0 38 opks 0") && MrHas("opkg0 34 GR_Movements") && MrHas("opkg0 8 (0 0 0)") &&
              MrHas("opkg4 34 GR_Tactical") && MrHas("ophk 38 opkg4 1") && MrHas("ophk on 1") && MrHas("ophk on 0") &&
              logHas("clone tree ophk 0") == 4 && MrHas("opclone0 34 Control 0") && MrHas("opclone1 34 Control 2") &&
              MrHas("opclone1 38 opkg0 0") && MrHas("opclone1 on 1") && MrHas("opclone3 38 opkg4 0") &&
              MrHas("opcmodel 48 hkinput 20") && logHas("opcmodel 48 hkinput 1") == 0 && MrHas("opcmodel 48 hktext 1") &&
              MrHas("opcmodel 48 keysscreen01 20") &&
              logHas("opkg0 24") == 1 && logHas("opkg4 24") == 1 && logHas("ophk 24") == 10 && logHas("opclone2 24") == 1;
        ok &= menu.m_models.size() == 5 && menu.m_frames.size() == 4 && menu.m_models[0] == As<I3D_frame>(dummies[0]) &&
              menu.m_frames[1] == As<I3D_frame>(clones[1]);
        const S_vector &p0 = As<I3D_frame>(clones[0])->m_localMatrix(3), &p1 = As<I3D_frame>(clones[1])->m_localMatrix(3),
                       &p2 = As<I3D_frame>(clones[2])->m_localMatrix(3);
        ok &= p0.x == 1.0f && p0.y == 2.0f && p0.z == 3.0f && p1.z == 1.0f * -0.036f + 3.0f && p2.z == 3.0f;
        WM(r.opControls, ok);

        // Without "hktext": false after the keys page's head (no groups, no buttons).
        g_msChildren.erase({model, "hktext"});
        S_ScreenDesc noText("OPTIONS", "options", 2u << 26);
        g_gfLog.clear();
        ok = !k.BuildControlsPage(&noText, As<I3D_frame>(model)) && noText.controls.size() == 2 &&
             noText.controls[1]->children.size() == 3 && logHas("clone tree") == 0;
        g_msChildren[{model, "hktext"}] = hktext;
        // A group without its keys screen: false, nothing described (its dummy made all the same).
        g_msChildren.erase({model, "keysscreen01"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("opkgx")};
        S_ControlDesc page(5, "page", 0x08200000);
        g_gfLog.clear();
        ok &= !k.BuildKeyGroup(&page, As<I3D_frame>(model), C_str("GR_X"), 1) && page.children.empty() && g_mrMade[FRAME_DUMMY].empty();
        g_msChildren[{model, "keysscreen01"}] = screen;
        // Without the input: false too.
        g_msChildren.erase({model, "hkinput"});
        g_mrMade[FRAME_DUMMY] = {MsFrame("opkgy")};
        ok &= !k.BuildKeyGroup(&page, As<I3D_frame>(model), C_str("GR_Y"), 1) && page.children.empty();
        g_msChildren[{model, "hkinput"}] = input;
        // All there: true (a group without bindings: its list alone).
        g_mrMade[FRAME_DUMMY] = {MsFrame("opkgz")};
        ok &= k.BuildKeyGroup(&page, As<I3D_frame>(model), C_str("GR_Z"), 3) && page.children.size() == 1 &&
              page.children[0]->children.empty();
        WM(r.opKeyGroup, ok);
        g_msChildren.clear();
        g_mrClones.clear();
        g_opBindingGroups.clear();
        bindings.m_bindings.clear();
        menu.m_models.clear();
        menu.m_frames.clear();
    }

    // --- the keys' callbacks: the groups' list (the chosen group's list shown, the scroll bar's,
    // refreshed; the others hidden; none chosen: nothing); a binding changed: refreshed; the layouts ---
    {
        C_OptionsHandler k(&menu);
        k.m_screen = 2;
        C_OpSelect chosen(0x08902000, 2), none(0x08902000, -1);
        g_controlMessages.clear();
        g_gfLog.clear();
        bool ok = C_OptionsHandler::OnKeyGroup(&k, nullptr, &chosen, CONTROL_NOTIFY_CHANGED, 0) && MsLogIs({"selected 1000069"}, g_gfLog) &&
                  ScQueue() == "8904000 1000004 0 0 ; 8905000 1000004 0 0 ; 8906000 1000003 0 0 ; 8903000 100000b 8906000 0 ; "
                               "8906000 200002c 0 0 ; 8907000 1000004 0 0 ; 8908000 1000004 0 0 ; ";
        ok &= C_OptionsHandler::OnKeyGroup(&k, nullptr, &none, 0, 0) && g_controlMessages.empty();
        // (No group list among the menu's screens: the refresh false.)
        ok &= !C_OptionsHandler::OnKeyChanged(&k, nullptr, nullptr, 0, 0);
        // With one: refreshed (five posts), true.
        C_MsScreen *screens[3];
        for (int i = 0; i < 3; ++i)
            screens[i] = new C_MsScreen(scene, CmText("opks%d", i).c_str(), static_cast<uint32_t>(i) << 26);
        menu.m_screens.assign(screens, screens + 3);
        auto *list = new C_OpList(0x08903000);
        screens[2]->m_children.push_back(list);
        g_opChosen = 5;
        g_controlMessages.clear();
        ok &= C_OptionsHandler::OnKeyChanged(&k, nullptr, nullptr, 0, 0) && g_controlMessages.size() == 5;
        g_controlMessages.clear();
        const auto layout = [&](uint32_t id) {
            C_OpSelect control(0x08000000 | id, 0);
            g_gfLog.clear();
            return C_OptionsHandler::OnKeyLayout(&k, nullptr, &control, CONTROL_NOTIFY_CLICK, 0);
        };
        ok &= layout(0x909000) && MsLogIs({"layout 0", "list asked 100000c"}, g_gfLog) && layout(0x90A000) &&
              MsLogIs({"layout 1", "list asked 100000c"}, g_gfLog) && layout(0x90B000) &&
              MsLogIs({"layout 0", "list asked 100000c"}, g_gfLog) && !C_OptionsHandler::OnKeyLayout(&k, nullptr, nullptr, 0, 0);
        // (The layout's refresh: the groups' five posts each time.)
        ok &= g_controlMessages.size() == 15;
        g_controlMessages.clear();
        screens[2]->m_children.clear();
        delete list;
        menu.m_screens.clear();
        for (C_MsScreen *sc : screens)
            delete sc;
        WM(r.opKeys, ok);
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
                "opBuild %d opBuildFail %d opShown %d opVideo %d opSound %d opGame %d opNetwork %d opSlider %d opDetail %d "
                "opCheck %d opVoice %d opControls %d opKeyGroup %d opKeys %d\n",
                r.opNew, r.opEscape, r.opSave, r.opOpen, r.opFill, r.opRefresh, r.opLeave, r.opUnbound, r.opBuild,
                r.opBuildFail, r.opShown, r.opVideo, r.opSound, r.opGame, r.opNetwork, r.opSlider, r.opDetail, r.opCheck,
                r.opVoice, r.opControls, r.opKeyGroup, r.opKeys);
}
