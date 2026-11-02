// Part 9d-ii-a's menu state (game/menu_state.cpp: the class, the screen stack and the messages;
// game/game_state.cpp: the base state), in the factory build, run by ControlsTest after ScreenTest:
// screens logging their slots, a menu logging OnMessage, the cursor faked (Pick logged), the sound
// module's StopAll, the profile's reset, the keyboard's polling and the frame trees' release faked.
// Prints "menu:" with 1 per check met.
#define private public
#define protected public
#include "game/menu_state.h"
#undef protected
#undef private
#include "game/app.h"
#include "game/items_module.h"

// (Part 9d-ii-b's: not yet ported.)
bool C_MenuState::Start(void *) { return false; }
bool C_MenuState::Stop() { return false; }
bool C_MenuState::HandleInput(float *, float *) { return false; }
void C_MenuState::Update(int) {}
void C_MenuState::Render() {}
void C_MenuState::DrawScreens() {}

void C_SoundModule::StopAll(bool releaseTableSounds, bool keepMusic) { GfLog("stopall %d %d", releaseTableSounds, keepMusic); }
void C_GameProfile::ResetGame() { GfLog("reset game"); }
void SetKeyboardPolling(bool enable) { GfLog("polling %d", enable); }
void ReleaseFrameTree(I3D_frame *frame) { GfLog("release tree %s", GfName(frame)); }
uint32_t g_msPicked;
uint32_t C_Cursor::Pick(I3D_scene *scene, I3D_frame *frame)
{
    GfLog("pick %d %s", scene != nullptr, GfName(frame));
    return g_msPicked;
}

namespace {

struct S_MenuResults {
    bool gsBase = true, msNew = true, msDefer = true, msTarget = true, msOpen = true, msPopup = true, msBack = true;
    bool msStates = true, msFrames = true, msTicked = true, msBox = true, msNotify = true, msEditing = true;
    bool msOther = true, msQueue = true, msFind = true, msBoxCalls = true, msHint = true;
};

// A screen logging its slots ("<name> <slot> <id>").
struct C_MsScreen : C_Screen {
    const char *name;
    C_MsScreen(I3D_scene *scene, const char *n, uint32_t id) : C_Screen(scene, GfFrame(FRAME_DUMMY, n)), name(n)
    {
        m_id = id;
    }
    void Hide() override { GfLog("%s hide", name); }
    void Open(int id) override { GfLog("%s open %x", name, id); }
    bool HandleMessage(S_ControlMessage *m) override
    {
        GfLog("%s message %x %x %x", name, m->target, m->id, m->value);
        return true;
    }
    int OnInput05(int id) override
    {
        GfLog("%s in05 %x", name, id);
        return 1;
    }
    int OnLeave(int id) override
    {
        GfLog("%s leave %x", name, id);
        return 1;
    }
};

// A control logging its editing slots, OnInput0B returning g_msStop.
int g_msStop = 1;
struct C_MsEdit : C_Control {
    const char *name;
    explicit C_MsEdit(const char *n) : C_Control(nullptr, 0, nullptr), name(n) {}
    int OnInput0B(int id) override
    {
        GfLog("%s stop %d", name, id);
        return g_msStop;
    }
    int BeginEdit(int id) override
    {
        GfLog("%s begin %d", name, id);
        return 1;
    }
};

// A menu logging its own messages.
struct C_MsMenu : C_MenuState {
    void OnMessage(S_ControlMessage *m) override { GfLog("menu message %x %x %x", m->target, m->id, m->value); }
    bool BuildScreens() override { return true; }
    ~C_MsMenu() override { m_screens.clear(); }
};

// The screen stack as names ("a b ").
std::string MsStack()
{
    std::string s;
    for (C_Screen *screen : g_menuScreens)
        s += std::string(static_cast<C_MsScreen *>(screen)->name) + " ";
    return s;
}
S_ControlMessage MsMessage(uint32_t id, uint32_t value = 0, uintptr_t value2 = 0, uint32_t target = 0)
{
    S_ControlMessage m;
    m.target = target;
    m.id = id;
    m.value = value;
    m.value2 = value2;
    return m;
}
void MsSend(C_MenuState &menu, uint32_t id, uint32_t value = 0, uintptr_t value2 = 0, uint32_t target = 0)
{
    S_ControlMessage m = MsMessage(id, value, value2, target);
    menu.HandleMessage(&m);
}

// A state with the base's slots.
struct C_MsState : C_GameState {};

} // namespace

void MenuTest(I3D_scene *scene)
{
    S_MenuResults r;
    g_controlMessages.clear();
    g_menuScreens.clear();
    g_gfLog.clear();

    // --- the base state: nothing done, not handled; the pending state committed (none: kept) ---
    {
        C_MsState s;
        float held[4] = {}, pressed[4] = {};
        WM(r.gsBase, !s.Start(nullptr) && !s.Stop() && !s.HandleInput(held, pressed) && s.GetTypeId() == 0 &&
                         s.GetState() == 0 && s.GetPendingState() == GAME_STATE_NO_PENDING);
        s.Tick(1);
        s.Update(1);
        s.Render();
        s.CommitPendingState();
        WM(r.gsBase, s.GetState() == 0 && s.GetPendingState() == GAME_STATE_NO_PENDING);
        s.SetPendingState(GAME_STATE_MAIN_MENU);
        WM(r.gsBase, s.GetPendingState() == GAME_STATE_MAIN_MENU && s.GetState() == 0);
        s.CommitPendingState();
        WM(r.gsBase, s.GetState() == GAME_STATE_MAIN_MENU && s.GetPendingState() == GAME_STATE_NO_PENDING);
        s.SetState(7);
        s.SetPendingState(GAME_STATE_ENDED);
        s.CommitPendingState();
        WM(r.gsBase, s.GetState() == 7 && s.GetPendingState() == GAME_STATE_NO_PENDING);
        auto *heap = new C_MsState();
        WM(r.gsBase, reinterpret_cast<uintptr_t>(heap) % 16 == 0);
        delete heap;
    }

    // --- made: quitting (until started), its fields' defaults, the menu state; deleted: none ---
    {
        auto *m = new C_MsMenu();
        WM(r.msNew, m->GetState() == GAME_STATE_QUIT && m->GetPendingState() == GAME_STATE_NO_PENDING &&
                        m->m_hoveredId == 0xFFFFFFFF && !m->m_editing && !m->m_timing && m->m_time == 0 &&
                        m->m_unknown1C == 250 && m->m_inputLock == -1 && m->m_unknown28 == 0 && !m->m_dragging &&
                        !m->m_keyboardOff && !m->m_scene && !m->m_camera && !m->m_light && !m->m_clickSound &&
                        !m->m_startArg && !m->m_cursor && !m->m_underPopup && !m->m_popup && !m->m_messageBox &&
                        m->m_models.empty() && m->m_frames.empty() && m->m_tickedControls.empty() && m->m_screens.empty() &&
                        g_menuState == m && m->GetTypeId() == 0 && reinterpret_cast<uintptr_t>(m) % 16 == 0);
        WM(r.msNew, !m->m_hint.m_on && !m->m_hint.m_hasText && m->m_hint.m_unknown00 == -1 && m->m_hint.m_unknown04 == -1 &&
                        m->m_hint.m_unknown08 == -1 && !m->m_hint.m_frame && m->m_sound.channel == SOUND_CHANNEL_NONE &&
                        m->m_sound.volume == 1.0f);
        WM(r.msNew, m->OnStart(nullptr) && m->OnInput(nullptr, nullptr) && !m->OnRender() && !m->OnDrawScreens());
        m->OnStop();
        m->OnUpdate(1);
        m->OnTick(1);
        m->Tick(5);
        auto *other = new C_MsMenu();
        WM(r.msNew, g_menuState == other);
        delete m;
        WM(r.msNew, g_menuState == nullptr);
        delete other;
        WM(r.msNew, g_menuState == nullptr && g_gfLog.empty());
    }

    C_MsMenu menu;
    menu.m_scene = scene;
    menu.m_cursor = reinterpret_cast<C_Cursor *>(&g_csCursor);
    C_MsScreen box(scene, "box", 0), s1(scene, "s1", 0x04000000), s2(scene, "s2", 0x08000000), s3(scene, "s3", 0x0C000000),
        s4(scene, "s4", 0x10000000);
    menu.m_screens = {&box, &s1, &s2, &s3, &s4};

    // --- kept back, then posted (the bit cleared, the first first) or dropped ---
    {
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_DEFER | MENU_MSG_QUIT, 1);
        MsSend(menu, MENU_MSG_DEFER | 0x2000006, 2, 0, 0x08000000);
        WM(r.msDefer, g_gfLog.empty() && g_controlMessages.empty() && menu.m_state == GAME_STATE_QUIT &&
                          g_deferredMessages.size() == 2);
        MsSend(menu, MENU_MSG_DEFERRED | MENU_DEFERRED_POST, 0);
        WM(r.msDefer, g_deferredMessages.empty() && g_gfLog.empty() &&
                          ScQueue() == "0 2000004 1 0 ; 8000000 2000006 2 0 ; ");
        MsSend(menu, MENU_MSG_DEFER | 0x2000006, 3);
        MsSend(menu, MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 0);
        WM(r.msDefer, g_deferredMessages.empty() && ScQueue().empty() && g_gfLog.empty());
        MsSend(menu, MENU_MSG_DEFER | 0x2000006, 4);
        MsSend(menu, MENU_MSG_DEFERRED, 0);
        WM(r.msDefer, g_deferredMessages.size() == 1 && ScQueue().empty());
        MsSend(menu, MENU_MSG_DEFERRED | MENU_DEFERRED_POST | MENU_DEFERRED_DROP, 0);
        WM(r.msDefer, g_deferredMessages.empty() && ScQueue() == "0 2000006 4 0 ; " && g_gfLog.empty());
        // (Deferred messages to a screen kept too; a target with the deferred bit's code handled first.)
        MsSend(menu, MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 0, 0, 0x04000000);
        WM(r.msDefer, g_gfLog.empty());
    }

    // --- to a screen by the target's top 6 bits (out of range: nothing) ---
    {
        g_gfLog.clear();
        MsSend(menu, 0x1000009, 7, 0, 0x08300000);
        MsSend(menu, 0x2000001, 1, 0, 0x04000000);
        MsSend(menu, 0x1000009, 7, 0, 0x14000000);
        MsSend(menu, 0x1000009, 7, 0, 0xFC000000);
        WM(r.msTarget, GfLogIs({"s2 message 8300000 1000009 7", "s1 message 4000000 2000001 1"}) && g_menuScreens.empty());
    }

    // --- a screen opened: the top hidden, the screen pushed and opened (0); out of range: hidden only ---
    {
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_OPEN_SCREEN, 1);
        WM(r.msOpen, MsStack() == "s1 " && GfLogIs({"s1 open 0"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_OPEN_SCREEN | 0x4000000, 2);
        WM(r.msOpen, MsStack() == "s1 s2 " && GfLogIs({"s1 hide", "s2 open 0"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_OPEN_SCREEN, 5);
        MsSend(menu, MENU_MSG_OPEN_SCREEN, 6);
        WM(r.msOpen, MsStack() == "s1 s2 " && GfLogIs({"s2 hide", "s2 hide"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_OPEN_SCREEN, 4);
        WM(r.msOpen, MsStack() == "s1 s2 s4 " && GfLogIs({"s2 hide", "s4 open 0"}) && !menu.m_underPopup);
        g_menuScreens.clear();
    }

    // --- a popup: over the top (kept under), opened with its id; closed: hidden, popped, the top
    // opened again (0), none under ---
    {
        g_menuScreens = {&s1};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_OPEN_POPUP, 3);
        WM(r.msPopup, MsStack() == "s1 s3 " && menu.m_underPopup == &s1 && GfLogIs({"s3 open 10200000"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_OPEN_POPUP, 5);
        WM(r.msPopup, MsStack() == "s1 s3 " && menu.m_underPopup == &s3 && g_gfLog.empty());
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_CLOSE_POPUP, 0);
        WM(r.msPopup, MsStack() == "s1 " && !menu.m_underPopup && GfLogIs({"s3 hide", "s1 open 0"}));
        menu.m_underPopup = &s2;
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_CLOSE_POPUP, 0);
        WM(r.msPopup, MsStack().empty() && !menu.m_underPopup && GfLogIs({"s1 hide"}));
        menu.m_underPopup = &s2;
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_CLOSE_POPUP, 0);
        WM(r.msPopup, MsStack().empty() && menu.m_underPopup == &s2 && g_gfLog.empty());
        MsSend(menu, MENU_MSG_OPEN_POPUP, 2);
        WM(r.msPopup, MsStack() == "s2 " && !menu.m_underPopup);
        g_menuScreens.clear();
        menu.m_underPopup = nullptr;
    }

    // --- back: the last screen: the quit question and the quit kept back; from the quit question:
    // the kept quit dropped, the question closed; else the top closed, the next opened ---
    {
        g_menuScreens = {&s1};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_BACK, 0);
        WM(r.msBack, MsStack() == "s1 " && g_gfLog.empty() && ScQueue() == "0 2000002 4 0 ; 0 22000004 0 0 ; ");
        g_menuScreens = {&s1, &s4};
        MsSend(menu, MENU_MSG_BACK, 0x10300000);
        WM(r.msBack, MsStack() == "s1 s4 " && g_gfLog.empty() && ScQueue() == "0 14001200 1 0 ; 0 6001005 0 0 ; ");
        MsSend(menu, MENU_MSG_BACK, 0x10000000);
        WM(r.msBack, ScQueue() == "0 14001200 1 0 ; 0 6001005 0 0 ; ");
        MsSend(menu, MENU_MSG_BACK, 0x14000000);
        WM(r.msBack, MsStack() == "s1 " && GfLogIs({"s4 hide", "s1 open 0"}) && ScQueue().empty());
        g_menuScreens = {&s1, &s2, &s3};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_BACK, 0x0C000000);
        WM(r.msBack, MsStack() == "s1 s2 " && GfLogIs({"s3 hide", "s2 open 0"}) && ScQueue().empty());
        g_menuScreens.clear();
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_BACK, 0);
        WM(r.msBack, g_gfLog.empty() && ScQueue().empty());
        // (The quit question asked from no screen: none.)
        MsSend(menu, MENU_MSG_BACK, 0x10000000);
        WM(r.msBack, ScQueue() == "0 14001200 1 0 ; 0 6001005 0 0 ; ");
    }

    // --- the states: quit (the sounds stopped), the campaign's previous / next (every screen
    // closed, the last first), the main menu (the game reset) ---
    {
        menu.m_state = 1;
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_QUIT, 0);
        WM(r.msStates, menu.m_state == GAME_STATE_QUIT && GfLogIs({"stopall 1 0"}));
        g_menuScreens = {&s1, &s2};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_PREVIOUS, 0);
        WM(r.msStates, menu.m_state == GAME_STATE_BACK && MsStack().empty() && GfLogIs({"s2 hide", "s1 hide"}));
        g_menuScreens = {&s3, &s1, &s2};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_NEXT, 0);
        WM(r.msStates, menu.m_state == GAME_STATE_ENDED && MsStack().empty() && GfLogIs({"s2 hide", "s1 hide", "s3 hide"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_PREVIOUS, 0);
        WM(r.msStates, menu.m_state == GAME_STATE_BACK && g_gfLog.empty());
        MsSend(menu, MENU_MSG_NEXT, 0);
        WM(r.msStates, menu.m_state == GAME_STATE_ENDED);
        MsSend(menu, MENU_MSG_MAIN_MENU, 0);
        WM(r.msStates, menu.m_state == GAME_STATE_MAIN_MENU && GfLogIs({"reset game"}));
        menu.m_state = 1;
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_RESET_GAME, 0);
        WM(r.msStates, menu.m_state == 1 && GfLogIs({"reset game"}) && ScQueue().empty());
    }

    // --- one of the menu's frames released (its tree) and forgotten; none matching: nothing ---
    {
        I3D_frame *a = GfFrame(FRAME_DUMMY, "msfa"), *b = GfFrame(FRAME_DUMMY, "msfb"), *c = GfFrame(FRAME_DUMMY, "msfc");
        menu.m_frames = {a, b, c, b};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_RELEASE_FRAME, 0, reinterpret_cast<uintptr_t>(b));
        WM(r.msFrames, GfLogIs({"release tree msfb"}) && menu.m_frames == std::vector<I3D_frame *>({a, c, b}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_RELEASE_FRAME, reinterpret_cast<uintptr_t>(a), 0x12345);
        MsSend(menu, MENU_MSG_RELEASE_FRAME, 0, 0);
        WM(r.msFrames, g_gfLog.empty() && menu.m_frames.size() == 3);
        MsSend(menu, MENU_MSG_RELEASE_FRAME, 0, reinterpret_cast<uintptr_t>(a));
        MsSend(menu, MENU_MSG_RELEASE_FRAME, 0, reinterpret_cast<uintptr_t>(b));
        WM(r.msFrames, GfLogIs({"release tree msfa", "release tree msfb"}) && menu.m_frames == std::vector<I3D_frame *>({c}));
        menu.m_frames.clear();
    }

    // --- the ticked controls: added once (none: nothing), removed ---
    {
        g_gfLog.clear();
        C_CtLogged a("msta"), b("mstb"), c("mstc");
        const auto add = [&](C_Control *x) { MsSend(menu, MENU_MSG_ADD_TICKED, 0, reinterpret_cast<uintptr_t>(x)); };
        const auto remove = [&](C_Control *x) { MsSend(menu, MENU_MSG_REMOVE_TICKED, 0, reinterpret_cast<uintptr_t>(x)); };
        add(&a);
        add(&b);
        add(&a);
        add(nullptr);
        add(&c);
        WM(r.msTicked, menu.m_tickedControls == std::vector<C_Control *>({&a, &b, &c}));
        remove(&b);
        remove(nullptr);
        remove(&b);
        WM(r.msTicked, menu.m_tickedControls == std::vector<C_Control *>({&a, &c}));
        remove(&c);
        remove(&a);
        WM(r.msTicked, menu.m_tickedControls.empty() && g_gfLog.empty());
        // (Only the first of the same one removed.)
        menu.m_tickedControls = {&a, &b, &a};
        remove(&a);
        WM(r.msTicked, menu.m_tickedControls == std::vector<C_Control *>({&b, &a}));
        menu.m_tickedControls.clear();
    }

    // --- the message box: shown, pushed over the top (kept under), opened (0); closed: popped only
    // when it is the top ---
    {
        g_menuScreens = {&s1};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_MESSAGE_BOX_SHOWN, 0);
        WM(r.msBox, MsStack() == "s1 box " && menu.m_underPopup == &s1 && GfLogIs({"box open 0"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_MESSAGE_BOX_CLOSED, 0);
        WM(r.msBox, MsStack() == "s1 " && !menu.m_underPopup && GfLogIs({"box hide"}));
        menu.m_underPopup = &s2;
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_MESSAGE_BOX_CLOSED, 0);
        WM(r.msBox, MsStack() == "s1 " && menu.m_underPopup == &s2 && g_gfLog.empty());
        // (A screen of index 0 in the top bits only: the box; bits under them ignored.)
        C_MsScreen sub(scene, "sub", 0x03F00000);
        g_menuScreens = {&s1, &sub};
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_MESSAGE_BOX_CLOSED, 0);
        WM(r.msBox, MsStack() == "s1 " && !menu.m_underPopup && GfLogIs({"sub hide"}));
        g_menuScreens.clear();
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_MESSAGE_BOX_CLOSED, 0);
        MsSend(menu, MENU_MSG_MESSAGE_BOX_SHOWN, 0);
        WM(r.msBox, MsStack() == "box " && !menu.m_underPopup && GfLogIs({"box open 0"}));
        g_menuScreens.clear();
        menu.m_underPopup = nullptr;
    }

    // --- the cursor's notifications to the top: 05 with the id picked on its frame, the leave (0) ---
    {
        g_msPicked = 0x04300000;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
        WM(r.msNotify, g_gfLog.empty());
        g_menuScreens = {&s1, &s2};
        MsSend(menu, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
        MsSend(menu, CONTROL_MSG_NOTIFY | 0x1000000, CONTROL_NOTIFY_LEAVE);
        MsSend(menu, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_ENTER);
        MsSend(menu, CONTROL_MSG_NOTIFY, 0x0005000);
        WM(r.msNotify, GfLogIs({"pick 1 s2", "s2 in05 4300000", "s2 leave 0"}));
        // (A top without its frame, or none: nothing.)
        I3D_frame *frame = s2.m_screenFrame;
        s2.m_screenFrame = nullptr;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_LEAVE);
        s2.m_screenFrame = frame;
        g_menuScreens = {&s1, nullptr};
        MsSend(menu, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_LEAVE);
        WM(r.msNotify, g_gfLog.empty());
        g_menuScreens.clear();
    }

    // --- the editing: started (the one editing asked to stop first: refused, kept), stopped (its
    // own control: the keys and the clicks on again), the keyboard off / on ---
    {
        C_MsEdit a("eda"), b("edb");
        C_Cursor *cursor = menu.m_cursor;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_START_EDITING, 0, reinterpret_cast<uintptr_t>(&a));
        WM(r.msEditing, menu.m_editing == &a && GfLogIs({"eda begin 0"}));
        g_msStop = 0;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_START_EDITING, 0, reinterpret_cast<uintptr_t>(&b));
        WM(r.msEditing, menu.m_editing == &a && GfLogIs({"eda stop 0"}));
        g_msStop = 7;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_START_EDITING, 0, reinterpret_cast<uintptr_t>(&b));
        WM(r.msEditing, menu.m_editing == &b && GfLogIs({"eda stop 0", "edb begin 0"}));
        menu.m_keyboardOff = true;
        cursor->m_clicksOff = true;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_STOP_EDITING, 0, reinterpret_cast<uintptr_t>(&a));
        WM(r.msEditing, menu.m_editing == &b && menu.m_keyboardOff && cursor->m_clicksOff && GfLogIs({"edb stop 0"}));
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_STOP_EDITING, 0, reinterpret_cast<uintptr_t>(&b));
        WM(r.msEditing, !menu.m_editing && !menu.m_keyboardOff && !cursor->m_clicksOff && GfLogIs({"edb stop 0", "polling 1"}));
        menu.m_keyboardOff = true;
        cursor->m_clicksOff = true;
        g_gfLog.clear();
        MsSend(menu, CONTROL_MSG_STOP_EDITING, 0, 0);
        WM(r.msEditing, !menu.m_editing && !menu.m_keyboardOff && !cursor->m_clicksOff && GfLogIs({"polling 1"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_KEYBOARD_OFF, 3);
        WM(r.msEditing, menu.m_keyboardOff && cursor->m_clicksOff && GfLogIs({"polling 0"}));
        g_gfLog.clear();
        MsSend(menu, MENU_MSG_KEYBOARD_OFF, 0);
        WM(r.msEditing, !menu.m_keyboardOff && !cursor->m_clicksOff && GfLogIs({"polling 1"}));
        g_msStop = 1;
    }

    // --- the rest to the menu's OnMessage (the masked code's: the bits above it kept) ---
    {
        g_gfLog.clear();
        MsSend(menu, 0x2000006, 1);
        MsSend(menu, 0x200000A, 2);
        MsSend(menu, 0x1000003, 3);
        MsSend(menu, 0x4000000 | 0x2000005 | 0x8000000, 4);
        MsSend(menu, 0x20000C1, 5);
        WM(r.msOther, GfLogIs({"menu message 0 2000006 1", "menu message 0 200000a 2", "menu message 0 1000003 3",
                               "menu message 0 20000c1 5"}) && MsStack().empty());
    }

    // --- the queue handled, the first first, those posted meanwhile too ---
    {
        g_menuScreens = {&s1};
        g_controlMessages.clear();
        PostControlMessage(MsMessage(0x2000006, 1));
        PostControlMessage(MsMessage(MENU_MSG_BACK, 0));
        PostControlMessage(MsMessage(0x1000009, 2, 0, 0x08100000));
        g_gfLog.clear();
        menu.HandleMessages();
        WM(r.msQueue, g_controlMessages.empty() && g_deferredMessages.size() == 1 &&
                          GfLogIs({"menu message 0 2000006 1", "s2 message 8100000 1000009 2", "s4 open 10200000"}) &&
                          MsStack() == "s1 s4 " && menu.m_underPopup == &s1);
        MsSend(menu, MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 0);
        g_menuScreens.clear();
        menu.m_underPopup = nullptr;
    }

    // --- a control found in its screen by the id's top bits (id 0, out of range: none) ---
    {
        C_CtLogged *kid = new C_CtLogged("msk", 0x08200000, &s2);
        s2.m_children.push_back(kid);
        WM(r.msFind, menu.FindControl(0x08200000) == kid && menu.FindControl(0x08000000) == &s2 &&
                         !menu.FindControl(0x08300000) && !menu.FindControl(0) && !menu.FindControl(0x14200000) &&
                         !menu.FindControl(0xFC200000));
        C_CtLogged *top = new C_CtLogged("mstop", 0x00100000, &box);
        box.m_children.push_back(top);
        WM(r.msFind, menu.FindControl(0x00100000) == top && menu.FindControl(0x00200000) == nullptr);
    }

    // --- the message box's calls (none made: nothing) ---
    {
        bool ok = true;
        menu.ShowMessage(MESSAGE_BOX_OK, C_str("x"), 1, 2);
        menu.ShowMessageId(MESSAGE_BOX_OK, 5, 1, 2);
        menu.CloseMessage();
        ok &= ScQueue().empty();
        C_MessageBox real(scene, GfFrame(FRAME_DUMMY, "msbox"));
        menu.m_messageBox = &real;
        menu.ShowMessage(MESSAGE_BOX_OK, C_str("hello"), 0x11, 0x12);
        ok &= real.m_kind == MESSAGE_BOX_OK && real.m_yesMessage == 0x11 && real.m_noMessage == 0x12 &&
              ScQueue().find("100000 1000014 18 0 hello; ") != std::string::npos;
        menu.CloseMessage();
        ok &= real.m_kind == MESSAGE_BOX_NONE && ScQueue() == "0 2000097 0 0 ; ";
        menu.ShowMessageId(MESSAGE_BOX_YES_NO, 77, 0x21, 0x22);
        ok &= real.m_kind == MESSAGE_BOX_YES_NO && real.m_noMessage == 0x22 &&
              ScQueue().find("100000 1000015 18 4d ; ") != std::string::npos;
        menu.m_messageBox = nullptr;
        WM(r.msBoxCalls, ok);
    }

    // --- the hint: reset (off, no text, no frame), its text (shown when on), drawn on its frame (no
    // scene: no camera asked for) ---
    {
        S_MenuHint h;
        h.m_on = true;
        h.m_unknown00 = 2;
        h.m_unknown04 = 3;
        h.m_unknown08 = 5;
        h.SetText(C_str("old"), 3);
        h.m_frame = GfFrame(FRAME_DUMMY, "mshint");
        h.m_hasText = true;
        h.Reset();
        WM(r.msHint, !h.m_on && !h.m_hasText && h.m_unknown00 == -1 && h.m_unknown04 == -1 && h.m_unknown08 == -1 &&
                         !h.m_frame && CsRuns(h.m_box).empty());
        h.SetText(C_str("hint"), 9);
        WM(r.msHint, h.m_hasText && !h.m_on && CsRuns(h.m_box) == "hint/9 ");
        I3D_frame *visual = CsVisual("mshintv");
        h.m_frame = visual;
        g_gfLog.clear();
        h.Render(nullptr);
        WM(r.msHint, g_gfLog.empty() && !h.m_box.m_placed);
        h.m_on = true;
        h.m_hasText = false;
        h.Render(nullptr);
        WM(r.msHint, g_gfLog.empty() && !h.m_box.m_placed);
        // (Drawn with its text emptied: the box's own drawing is the text build's.)
        h.m_hasText = true;
        h.m_box.Clear();
        h.Render(nullptr);
        WM(r.msHint, h.m_box.m_placed && CsPlaced(h.m_box, 10));
        h.SetText(C_str("again"), 4);
        h.SetText(C_str("twice"), 5);
        WM(r.msHint, CsRuns(h.m_box) == "twice/5 ");
    }

    g_menuScreens.clear();
    g_deferredMessages.clear();
    g_controlMessages.clear();
    g_gfLog.clear();
    std::printf("menu: gsBase %d msNew %d msDefer %d msTarget %d msOpen %d msPopup %d msBack %d msStates %d msFrames %d "
                "msTicked %d msBox %d msNotify %d msEditing %d msOther %d msQueue %d msFind %d msBoxCalls %d msHint %d\n",
                r.gsBase, r.msNew, r.msDefer, r.msTarget, r.msOpen, r.msPopup, r.msBack, r.msStates, r.msFrames,
                r.msTicked, r.msBox, r.msNotify, r.msEditing, r.msOther, r.msQueue, r.msFind, r.msBoxCalls, r.msHint);
}
