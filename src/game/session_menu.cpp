// The network session's menu (game/session_menu.h, part 9g-ii).
#include "game/session_menu.h"

#include <cstring>

#include "common/c_str.h"
#include "game/controls.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/text_style.h"
#include "ls3d/i3d.h"

bool g_sessionPasswordSet;

namespace {

// The screens (their ids' top 6 bits) and their controls: the console's text, the password's input
// and buttons.
constexpr uint32_t SCREEN_SHIFT = 26, SCREEN_MASK = 0x3F;
constexpr uint32_t CONSOLE_SCREEN = 1, PASSWORD_SCREEN = 2;
constexpr uint32_t CONSOLE_TEXT = 0x4100000, PASSWORD_INPUT = 0x8200000, PASSWORD_OK = 0x8300000,
                   PASSWORD_CANCEL = 0x8400000;
// The session's own message: the server made, its game started.
constexpr uint32_t SESSION_MSG_STARTED = 0x2000069;
constexpr int CONTROL_INPUT = 3, CONTROL_LABEL = 0x12, CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14;
// The console's texts: "Creating server ...", "Creating client ...", "Connecting to server ...",
// "ERROR -- !!!", "Connection lost.", "Unable to create server.", "Press <ESC> to return.", "Server
// created.", "Connected.", "Loading player list and mission status ....", "Too many players.", "Press
// <ESC> to return.", "Incorrect game password.", "Press <ESC> to return.", "CD Key not valid", "Not a
// valid version".
constexpr uint32_t TEXT_CREATING_SERVER = 0x91B, TEXT_CREATING_CLIENT = 0x91C, TEXT_CONNECTING = 0x91D,
                   TEXT_ERROR = 0x91E, TEXT_CONNECTION_LOST = 0x91F, TEXT_HOST_FAILED = 0x920,
                   TEXT_ESCAPE = 0x921, TEXT_SERVER_CREATED = 0x922, TEXT_CONNECTED = 0x923, TEXT_LOADING = 0x924,
                   TEXT_FULL = 0x925, TEXT_FULL_ESCAPE = 0x926, TEXT_BAD_PASSWORD = 0x927,
                   TEXT_REFUSED_ESCAPE = 0x928, TEXT_BAD_CD_KEY = 0x929, TEXT_BAD_VERSION = 0x92A;
// The password's texts: "ENTER PASSWORD", blank, "YES" / "NO" (the OK and Cancel buttons').
constexpr uint32_t TEXT_ENTER_PASSWORD = 0x7D3, TEXT_BLANK = 0x7D2, TEXT_YES = 0x7D1, TEXT_NO = 0x7D0;
constexpr uint32_t PASSWORD_TITLE_STYLE = 0xC, INPUT_STYLE = 0xE, BUTTON_STYLE = 0xD;
// The waits (ms).
constexpr int WAIT_STEP = 300, WAIT_ERROR = 1000, WAIT_PASSWORD = 500, WAIT_HOST_FAILED = 2500;
// The server's DirectPlay fields.
constexpr uint32_t APPLICATION_GUID[4] = {0xEBFE7BA0, 0x11D2628D, 0x60000FAE, 0x1114B097};
constexpr int16_t SESSION_UNKNOWN15C = 100, SESSION_UNKNOWN15E = 2000;

const S_vector BOX_POS(0.5f, -0.375f, -0.5f);

void Post(uint32_t target, uint32_t id, uint32_t value, uintptr_t value2 = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    PostControlMessage(message);
}

uint32_t ScreenIndex(uint32_t id) { return id >> SCREEN_SHIFT & SCREEN_MASK; }

// The input's text kept as the password (none: false).
bool KeepPassword(C_Control *input)
{
    S_ControlMessage get;
    get.id = CONTROL_MSG_GET_TEXT;
    input->HandleMessage(&get);
    if (get.text.IsEmpty())
        return false;
    g_mpSettings->password = get.text;
    g_sessionPasswordSet = true;
    return true;
}

}   // namespace

bool C_SessionMenu::OnStart(void *)
{
    S_TextStyleDesc desc = {};
    desc.font = 2;
    desc.color = 0xFF000000;
    desc.size = 0.035f;
    desc.width = 0.0f;
    desc.x = -1.0f;
    desc.y = -1.0f;
    desc.align = 1;
    desc.valign = 3;
    m_style = CreateTextStyle(desc);
    m_step = SESSION_STEP_START;
    return true;
}

void C_SessionMenu::OnStop()
{
    ReleaseTextStyle(m_style);
}

bool C_SessionMenu::BuildScreens()
{
    auto *menu = new S_MenuDesc();
    menu->AddScreen("MP Connect console", "confirm");
    menu->AddScreen("PASSWORD", "password");
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        if ((desc->id & 0xFC000000) == CONSOLE_SCREEN << SCREEN_SHIFT)
            DescribeConsole(desc, model);
        if ((desc->id & 0xFC000000) == PASSWORD_SCREEN << SCREEN_SHIFT)
            DescribePassword(desc, model);
        auto *screen = new C_Screen(m_scene, model);
        // (Quirk kept: a screen not built is leaked.)
        if (screen->Build(desc)) {
            screen->HandleMessages(desc->messages);
            m_screens.push_back(screen);
        }
    }
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = CONSOLE_SCREEN;
    HandleMessage(&open);
    delete menu;
    return true;
}

bool C_SessionMenu::DescribeConsole(S_ScreenDesc *desc, I3D_frame *model)
{
    model->SetPos(BOX_POS);
    const auto self = reinterpret_cast<uintptr_t>(this);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SCREEN_TICK, self,
                     reinterpret_cast<uintptr_t>(&C_SessionMenu::OnConsoleTick));
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_HIDDEN, self,
                     reinterpret_cast<uintptr_t>(&C_SessionMenu::OnConsoleHidden));
    desc->AddControl(CONTROL_LABEL, "text");
    AddDescMessage(desc->AddControl(CONTROL_BUTTON, "byes*"), 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(desc->AddControl(CONTROL_BUTTON, "bno*"), 0, CONTROL_MSG_HIDE, 0);
    S_ControlDesc *exit = desc->AddControl(CONTROL_BUTTON, "bok");
    AddDescMessage(exit, 0, CONTROL_MSG_SET_TEXT, BUTTON_STYLE, C_fstr("Exit Game"));
    AddDescMessage(exit, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_QUIT, 0);
    return true;
}

bool C_SessionMenu::DescribePassword(S_ScreenDesc *desc, I3D_frame *model)
{
    model->SetPos(BOX_POS);
    const auto self = reinterpret_cast<uintptr_t>(this);
    const auto notify = reinterpret_cast<uintptr_t>(&C_SessionMenu::OnPasswordNotify);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text"), 0, CONTROL_MSG_SET_TEXT_ID, PASSWORD_TITLE_STYLE,
                   TEXT_ENTER_PASSWORD);
    S_ControlDesc *input = desc->AddControl(CONTROL_INPUT, "binput");
    AddDescMessage(input, 0, CONTROL_MSG_SET_TEXT_ID, INPUT_STYLE, TEXT_BLANK);
    AddDescMessage(input, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_RETURN, self, notify);
    AddDescMessage(input, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ESCAPE, self, notify);
    S_ControlDesc *ok = desc->AddControl(CONTROL_BUTTON, "byes*");
    AddDescMessage(ok, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_YES);
    AddDescMessage(ok, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, notify);
    S_ControlDesc *cancel = desc->AddControl(CONTROL_BUTTON, "bno*");
    AddDescMessage(cancel, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_NO);
    AddDescMessage(cancel, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, notify);
    AddDescMessage(desc->AddControl(CONTROL_INPUT, "binput01"), 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(desc->AddControl(CONTROL_INPUT, "binput02"), 0, CONTROL_MSG_HIDE, 0);
    return true;
}

void C_SessionMenu::OnMessage(S_ControlMessage *message)
{
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE: {
        if (g_menuScreens.empty())
            return;
        const uint32_t top = ScreenIndex(g_menuScreens.back()->GetId());
        if (top == CONSOLE_SCREEN) {
            Post(0, MENU_MSG_CLOSE_POPUP, 0);
            Post(0, MENU_MSG_QUIT, 0);
        } else if (top == PASSWORD_SCREEN) {
            Post(0, MENU_MSG_DEFERRED | MENU_DEFERRED_DROP, 1);
            Post(0, MENU_MSG_CLOSE_POPUP, 0);
        }
        return;
    }
    case SESSION_MSG_STARTED:
        m_state = GAME_STATE_NEXT_MAP;
        return;
    default:
        return;
    }
}

void C_SessionMenu::AddLine(uint32_t textId) const
{
    Post(CONSOLE_TEXT, CONTROL_MSG_ADD_TEXT_ID, static_cast<uint32_t>(m_style), textId);
}

bool C_SessionMenu::OnConsoleTick(void *menu, void *, C_Control *control, uint32_t, uint32_t value)
{
    auto *self = static_cast<C_SessionMenu *>(menu);
    if (ScreenIndex(control->GetId()) != CONSOLE_SCREEN || !control->IsShown())
        return false;
    if (self->m_wait > 0) {
        self->m_wait -= static_cast<int>(value);
        return true;
    }
    S_MultiplayerSettings *settings = g_mpSettings;
    switch (self->m_step) {
    case SESSION_STEP_START:
        g_gameProfile.m_missionSquad->Clear();
        g_gameProfile.m_humanStore->Reload();
        Post(CONSOLE_TEXT, CONTROL_MSG_CLEAR, 0);
        if (settings->sessionType == SESSION_HOSTING) {
            self->AddLine(TEXT_CREATING_SERVER);
            self->m_wait = WAIT_STEP;
            self->m_step = SESSION_STEP_HOST;
        } else if (settings->sessionType == SESSION_JOINING) {
            self->AddLine(TEXT_CREATING_CLIENT);
            self->AddLine(TEXT_CONNECTING);
            self->m_wait = WAIT_STEP;
            self->m_step = SESSION_STEP_CONNECT;
        } else {
            self->AddLine(TEXT_ERROR);
            self->AddLine(TEXT_CONNECTION_LOST);
            self->m_wait = WAIT_ERROR;
            self->m_step = SESSION_STEP_FAILED;
        }
        return true;
    case SESSION_STEP_CONNECT:
        g_netGame.Connect(reinterpret_cast<S_ServerEntry *>(settings->unknown17C));
        self->m_step = SESSION_STEP_JOINING;
        return true;
    case SESSION_STEP_JOINING:
        switch (g_netGame.GetJoinStatus()) {
        case NET_JOIN_CONNECTED:
            self->AddLine(TEXT_CONNECTED);
            self->AddLine(TEXT_LOADING);
            self->m_step = SESSION_STEP_WAIT;
            return true;
        case NET_JOIN_FULL:
            self->AddLine(TEXT_FULL);
            self->AddLine(TEXT_FULL_ESCAPE);
            break;
        case NET_JOIN_BAD_PASSWORD:
            self->AddLine(TEXT_BAD_PASSWORD);
            self->m_wait = WAIT_PASSWORD;
            self->m_step = SESSION_STEP_PASSWORD;
            return true;
        case NET_JOIN_BAD_CD_KEY:
            self->AddLine(TEXT_BAD_CD_KEY);
            self->AddLine(TEXT_REFUSED_ESCAPE);
            break;
        case NET_JOIN_BAD_VERSION:
            self->AddLine(TEXT_BAD_VERSION);
            self->AddLine(TEXT_REFUSED_ESCAPE);
            break;
        default:
            return true;
        }
        self->m_step = SESSION_STEP_FAILED;
        return true;
    case SESSION_STEP_HOST: {
        S_NetSessionDesc desc;
        static_assert(sizeof desc == sizeof settings->sessionName, "the session's name");
        std::memcpy(&desc, settings->sessionName, sizeof desc);
        std::memcpy(desc.serviceProvider, APPLICATION_GUID, sizeof desc.serviceProvider);
        desc.unknown15C = SESSION_UNKNOWN15C;
        desc.unknown15E = SESSION_UNKNOWN15E;
        if (g_netGame.Host(desc, true) >= 0) {
            self->AddLine(TEXT_SERVER_CREATED);
            self->m_wait = WAIT_STEP;
            self->m_step = SESSION_STEP_HOSTED;
        } else {
            self->AddLine(TEXT_HOST_FAILED);
            self->AddLine(TEXT_ESCAPE);
            self->m_step = SESSION_STEP_FAILED;
            self->m_wait = WAIT_HOST_FAILED;
        }
        return true;
    }
    case SESSION_STEP_HOSTED:
        Post(0, SESSION_MSG_STARTED, 0);
        self->m_step = SESSION_STEP_WAIT;
        return true;
    case SESSION_STEP_PASSWORD:
        Post(PASSWORD_INPUT, CONTROL_MSG_SET_TEXT_ID, INPUT_STYLE, TEXT_BLANK);
        Post(0, MENU_MSG_OPEN_SCREEN, PASSWORD_SCREEN);
        Post(PASSWORD_INPUT, CONTROL_MSG_EDIT, 0);
        self->m_wait = 0;
        self->m_step = SESSION_STEP_WAIT;
        return true;
    default:
        return true;
    }
}

bool C_SessionMenu::OnConsoleHidden(void *, void *, C_Control *, uint32_t, uint32_t)
{
    return true;
}

bool C_SessionMenu::OnPasswordNotify(void *menu, void *, C_Control *control, uint32_t notification, uint32_t)
{
    auto *self = static_cast<C_SessionMenu *>(menu);
    if (!control)
        return false;
    switch (control->GetId()) {
    case PASSWORD_INPUT:
        if (notification == CONTROL_NOTIFY_RETURN) {
            if (KeepPassword(control)) {
                self->m_step = SESSION_STEP_START;
                Post(0, MENU_MSG_BACK, 0);
            }
        } else if (notification == CONTROL_NOTIFY_ESCAPE) {
            Post(0, MENU_MSG_BACK, 0);
        }
        return true;
    case PASSWORD_OK: {
        // (Fix: the original read the input without looking whether there was one.)
        C_Control *input = self->FindControl(PASSWORD_INPUT);
        if (input && KeepPassword(input)) {
            self->m_step = SESSION_STEP_START;
            Post(0, MENU_MSG_BACK, 0);
        }
        return true;
    }
    case PASSWORD_CANCEL:
        Post(0, MENU_MSG_BACK, 0);
        return true;
    default:
        return false;
    }
}
