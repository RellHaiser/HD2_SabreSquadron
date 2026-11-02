#include "game/menu_state.h"

#include "game/app.h"
#include "game/cursor.h"
#include "game/items_module.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"

std::deque<C_Screen *> g_menuScreens;                   // 0x008ae5c4
C_MenuState *g_menuState;                               // 0x008ae5f8

std::deque<S_ControlMessage> g_deferredMessages;        // 0x008ae55c

namespace {

// A screen's index: its id's top 6 bits.
constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;
// The message box's index (made first, its description's id 0).
constexpr uint32_t MESSAGE_BOX_SCREEN = 0;
// The id the popup's screen is opened with (OnEnter's).
constexpr int POPUP_ENTER_ID = 0x10200000;

void Post(uint32_t id, uint32_t value)
{
    S_ControlMessage message;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

// The top screen hidden and taken off.
void PopScreen()
{
    g_menuScreens.back()->Hide();
    g_menuScreens.pop_back();
}

} // namespace

// --- the hint ----------------------------------------------------------------------------------------

void S_MenuHint::Reset()
{
    m_on = false;
    m_unknown00 = m_unknown04 = m_unknown08 = -1;
    m_frame = nullptr;
    m_hasText = false;
    m_box.Clear();
}

void S_MenuHint::SetText(const C_str &text, uint8_t style)
{
    m_hasText = true;
    m_box.SetText(text, style, 0);
}

void S_MenuHint::Render(I3D_scene *scene)
{
    if (!m_on || !m_hasText)
        return;
    m_box.AttachTo(scene, m_frame);
    m_box.Render();
}

// --- the menu state ----------------------------------------------------------------------------------

C_MenuState::C_MenuState()
{
    m_state = GAME_STATE_QUIT;
    g_menuState = this;
}

C_MenuState::~C_MenuState()
{
    g_menuState = nullptr;
    m_editing = nullptr;
}

void C_MenuState::HandleMessage(S_ControlMessage *message)
{
    const uint32_t id = message->id;
    if (id & MENU_MSG_DEFER) {
        g_deferredMessages.push_back(*message);
        return;
    }
    if (id & MENU_MSG_DEFERRED) {
        if (id & MENU_DEFERRED_POST) {
            while (!g_deferredMessages.empty()) {
                g_deferredMessages.front().id &= ~MENU_MSG_DEFER;
                PostControlMessage(g_deferredMessages.front());
                g_deferredMessages.pop_front();
            }
        }
        if (message->id & MENU_DEFERRED_DROP)
            g_deferredMessages.clear();
        return;
    }
    if (message->target != 0) {
        // (Fix: the original took the screen without checking the index.)
        const uint32_t index = message->target >> SCREEN_INDEX_SHIFT;
        if (index < m_screens.size())
            m_screens[index]->HandleMessage(message);
        return;
    }
    switch (id & CONTROL_MSG_MASK) {
    case MENU_MSG_OPEN_SCREEN:
        if (!g_menuScreens.empty())
            g_menuScreens.back()->Hide();
        // (Fix: the original read one past the screens for the index of their count.)
        if (message->value >= m_screens.size())
            return;
        g_menuScreens.push_back(m_screens[message->value]);
        g_menuScreens.back()->Open(0);
        return;
    case MENU_MSG_OPEN_POPUP:
        // (Fix: the original read the top of an empty stack and took the screen unchecked.)
        m_underPopup = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (message->value >= m_screens.size())
            return;
        g_menuScreens.push_back(m_screens[message->value]);
        g_menuScreens.back()->Open(POPUP_ENTER_ID);
        return;
    case MENU_MSG_BACK:
        if (g_menuScreens.size() == 1) {
            // The last screen: the quit question, the quit kept back for its answer.
            Post(MENU_MSG_OPEN_POPUP, QUIT_SCREEN);
            Post(MENU_MSG_DEFER | MENU_MSG_QUIT, 0);
        } else if ((message->value & SCREEN_INDEX_MASK << SCREEN_INDEX_SHIFT) == QUIT_SCREEN << SCREEN_INDEX_SHIFT) {
            // Back from the quit question: the kept quit dropped, the question closed.
            Post(MENU_MSG_DEFERRED | 0x4000000 | 0x1000 | MENU_DEFERRED_DROP, 1);
            Post(0x4000000 | 0x1000 | MENU_MSG_CLOSE_POPUP, 0);
        } else {
            if (g_menuScreens.empty())
                return;
            PopScreen();
            if (g_menuScreens.empty())
                return;
            g_menuScreens.back()->Open(0);
        }
        return;
    case MENU_MSG_QUIT:
        GetSoundModule()->StopAll(true, false);
        m_state = GAME_STATE_QUIT;
        return;
    case MENU_MSG_CLOSE_POPUP:
        if (g_menuScreens.empty())
            return;
        PopScreen();
        m_underPopup = nullptr;
        if (g_menuScreens.empty())
            return;
        g_menuScreens.back()->Open(0);
        return;
    case MENU_MSG_PREVIOUS:
        while (!g_menuScreens.empty())
            PopScreen();
        m_state = GAME_STATE_BACK;
        return;
    case MENU_MSG_NEXT:
        while (!g_menuScreens.empty())
            PopScreen();
        m_state = GAME_STATE_ENDED;
        return;
    case MENU_MSG_MAIN_MENU: {
        S_ControlMessage reset;
        reset.id = MENU_MSG_RESET_GAME;
        HandleMessage(&reset);
        m_state = GAME_STATE_MAIN_MENU;
        return;
    }
    case MENU_MSG_RESET_GAME:
        g_gameProfile.ResetGame();
        return;
    case MENU_MSG_RELEASE_FRAME:
        for (size_t i = 0; i < m_frames.size(); ++i) {
            if (reinterpret_cast<uintptr_t>(m_frames[i]) == message->value2) {
                ReleaseFrameTree(m_frames[i]);
                m_frames.erase(m_frames.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        return;
    case CONTROL_MSG_NOTIFY: {
        // The cursor's notifications to the top screen (its frame made).
        if (g_menuScreens.empty() || !g_menuScreens.back() || !g_menuScreens.back()->GetScreenFrame())
            return;
        C_Screen *top = g_menuScreens.back();
        if (message->value == CONTROL_NOTIFY_05)
            top->OnInput05(static_cast<int>(m_cursor->Pick(m_scene, top->GetScreenFrame())));
        else if (message->value == CONTROL_NOTIFY_LEAVE)
            top->OnLeave(0);
        return;
    }
    case MENU_MSG_ADD_TICKED: {
        auto *control = reinterpret_cast<C_Control *>(message->value2);
        if (!control)
            return;
        for (C_Control *c : m_tickedControls)
            if (c == control)
                return;
        m_tickedControls.push_back(control);
        return;
    }
    case MENU_MSG_REMOVE_TICKED: {
        auto *control = reinterpret_cast<C_Control *>(message->value2);
        if (!control)
            return;
        for (size_t i = 0; i < m_tickedControls.size(); ++i) {
            if (m_tickedControls[i] == control) {
                m_tickedControls.erase(m_tickedControls.begin() + static_cast<std::ptrdiff_t>(i));
                return;
            }
        }
        return;
    }
    case MENU_MSG_MESSAGE_BOX_SHOWN:
        // (Fix: the original read the top of an empty stack and took the box unchecked.)
        m_underPopup = g_menuScreens.empty() ? nullptr : g_menuScreens.back();
        if (m_screens.empty())
            return;
        g_menuScreens.push_back(m_screens[MESSAGE_BOX_SCREEN]);
        g_menuScreens.back()->Open(0);
        return;
    case MENU_MSG_MESSAGE_BOX_CLOSED:
        // Only the box itself taken off.
        if (g_menuScreens.empty() || (g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) != MESSAGE_BOX_SCREEN)
            return;
        PopScreen();
        m_underPopup = nullptr;
        return;
    case CONTROL_MSG_START_EDITING: {
        // The control editing asked to stop first (refused: not started).
        if (m_editing && m_editing->OnInput0B(0) == 0)
            return;
        m_editing = reinterpret_cast<C_Control *>(message->value2);
        m_editing->BeginEdit(0);
        return;
    }
    case CONTROL_MSG_STOP_EDITING:
        if (m_editing)
            m_editing->OnInput0B(0);
        if (reinterpret_cast<uintptr_t>(m_editing) != message->value2)
            return;
        m_editing = nullptr;
        m_keyboardOff = false;
        SetKeyboardPolling(true);
        m_cursor->SetClicksOff(m_keyboardOff);
        return;
    case MENU_MSG_KEYBOARD_OFF:
        m_keyboardOff = message->value != 0;
        SetKeyboardPolling(!m_keyboardOff);
        m_cursor->SetClicksOff(m_keyboardOff);
        return;
    default:
        OnMessage(message);
        return;
    }
}

void C_MenuState::HandleMessages()
{
    while (!g_controlMessages.empty()) {
        S_ControlMessage message = g_controlMessages.front();
        g_controlMessages.pop_front();
        HandleMessage(&message);
    }
}

C_Control *C_MenuState::FindControl(uint32_t id)
{
    if (id == 0)
        return nullptr;
    const uint32_t index = id >> SCREEN_INDEX_SHIFT;
    if (index >= m_screens.size())
        return nullptr;
    return m_screens[index]->FindControl(id, true);
}

void C_MenuState::ShowMessage(int kind, const C_str &text, uint32_t yesMessage, uint32_t noMessage)
{
    if (m_messageBox)
        m_messageBox->ShowText(kind, text, yesMessage, noMessage);
}

void C_MenuState::ShowMessageId(int kind, uint32_t textId, uint32_t yesMessage, uint32_t noMessage)
{
    if (m_messageBox)
        m_messageBox->ShowTextId(kind, textId, yesMessage, noMessage);
}

void C_MenuState::CloseMessage()
{
    if (m_messageBox)
        m_messageBox->Close();
}
