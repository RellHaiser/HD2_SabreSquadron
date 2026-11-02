#include "game/screen.h"

#include "game/cursor.h"
#include "ls3d/i3d.h"

// --- the descriptions ------------------------------------------------------------------------------

S_ScreenDesc::S_ScreenDesc(const char *name, const char *file, uint32_t id) : file(file), name(name), id(id) {}

S_ScreenDesc::~S_ScreenDesc()
{
    while (!controls.empty()) {
        delete controls.back();
        controls.pop_back();
    }
}

S_ControlDesc *S_ScreenDesc::AddControl(int type, const char *name)
{
    const uint32_t index = static_cast<uint32_t>(controls.size()) + 1;
    controls.push_back(new S_ControlDesc(type, name, (index & 0x3F) << 20 | id));
    return controls.back();
}

void AddScreenMessage(S_ScreenDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2,
                      uintptr_t value3)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    message.value3 = value3;
    desc->messages.push_back(message);
}

S_MenuDesc::~S_MenuDesc()
{
    while (!screens.empty()) {
        delete screens.back();
        screens.pop_back();
    }
}

S_ScreenDesc *S_MenuDesc::AddScreen(const char *name, const char *file)
{
    const uint32_t index = static_cast<uint32_t>(screens.size()) + 1;
    screens.push_back(new S_ScreenDesc(name, file, index << SCREEN_ID_SHIFT));
    return screens.back();
}

// --- the screen ------------------------------------------------------------------------------------

namespace {

// A screen has no id until its description's.
constexpr uint32_t NO_SCREEN_ID = 0xFFFFFFFF;
// The keys that click the focus (DirectInput codes): Enter, Space, the keypad's Enter.
constexpr uint8_t KEY_RETURN = 0x1C, KEY_SPACE = 0x39, KEY_NUMPAD_ENTER = 0x9C;
constexpr uint8_t KEY_ESCAPE = 0x01;

} // namespace

C_Screen::C_Screen(I3D_scene *scene, I3D_frame *frame) : C_ControlGroup(scene, NO_SCREEN_ID, nullptr), m_screenFrame(frame)
{
    m_cursor = C_Cursor::Acquire(scene);
}

C_Screen::~C_Screen()
{
    if (m_cursor) {
        m_cursor->Release();
        m_cursor = nullptr;
    }
}

void C_Screen::Hide()
{
    if (!m_screenFrame->IsOn())
        return;
    m_screenFrame->SetOn(false);
    OnLeave(0);
    Notify(CONTROL_NOTIFY_HIDDEN, 0);
}

void C_Screen::Tick(int time)
{
    Notify(CONTROL_NOTIFY_SCREEN_TICK, static_cast<uint32_t>(time));
    if (IsChild(m_focus))
        m_children[m_focus]->Tick(time);
}

bool C_Screen::Unknown20(int key)
{
    if (IsChild(m_focus)) {
        C_Control *focus = m_children[m_focus];
        if (!focus->IsInactive() && focus->Unknown20(key))
            return true;
    }
    const uint8_t code = static_cast<uint8_t>(key);
    if ((code == KEY_RETURN || code == KEY_SPACE || code == KEY_NUMPAD_ENTER) && IsChild(m_focus)) {
        C_Control *focus = m_children[m_focus];
        focus->OnClick(static_cast<int>(focus->GetId()));
        return true;
    }
    return false;
}

bool C_Screen::HandleMessage(S_ControlMessage *message)
{
    // (The base's, not the group's.)
    if (message->target == 0 || (message->id & CONTROL_NOTIFY_MASK) != 0)
        return C_Control::HandleMessage(message);
    if ((message->id & CONTROL_MSG_MASK) == CONTROL_MSG_EDIT && OnLeave(m_focus) != 0)
        OnEnter(static_cast<int>(message->target));
    if (FindChild(message->target) < 0)
        return false;
    m_children[FindChild(message->target)]->HandleMessage(message);
    return true;
}

void C_Screen::Open(int id)
{
    if (m_screenFrame->IsOn())
        return;
    OnEnter(id);
    m_screenFrame->SetOn(true);
    Notify(CONTROL_NOTIFY_SHOWN, 0);
}

bool C_Screen::Build(const S_ScreenDesc *desc)
{
    for (size_t i = 0; i < desc->controls.size(); ++i)
        CreateChild(m_screenFrame, m_screenFrame, desc->controls[i]);
    m_name = desc->name;
    m_id = desc->id;
    return true;
}

// --- the message box -------------------------------------------------------------------------------

namespace {

// The box's controls' ids (its description's id 0): the text, the buttons.
constexpr uint32_t BOX_TEXT = 0x100000, BOX_YES = 0x200000, BOX_NO = 0x300000, BOX_OK = 0x400000;
// The buttons' indices (their ids' first field).
constexpr uint32_t BUTTON_YES = 2, BUTTON_NO = 3, BUTTON_OK = 4;
// The text's styles.
constexpr uint32_t BOX_STYLE = 0x18, BOX_STYLE_2 = 0x19, BUTTON_STYLE = 0xD;
// The texts' ids.
constexpr uint32_t TEXT_BOX = 2004, TEXT_YES = 2001, TEXT_NO = 2000, TEXT_OK = 2013;

// (The box's messages carry no value.)
void Post(uint32_t target, uint32_t id)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    PostControlMessage(message);
}

// The buttons of the kind shown, the others hidden (other kinds: none changed).
void ShowButtons(int kind)
{
    switch (kind) {
    case MESSAGE_BOX_TEXT:
        Post(BOX_YES, CONTROL_MSG_HIDE);
        Post(BOX_NO, CONTROL_MSG_HIDE);
        Post(BOX_OK, CONTROL_MSG_HIDE);
        break;
    case MESSAGE_BOX_YES_NO:
    case MESSAGE_BOX_YES_NO_2:
        Post(BOX_YES, CONTROL_MSG_SHOW);
        Post(BOX_NO, CONTROL_MSG_SHOW);
        Post(BOX_OK, CONTROL_MSG_HIDE);
        break;
    case MESSAGE_BOX_OK:
        Post(BOX_YES, CONTROL_MSG_HIDE);
        Post(BOX_NO, CONTROL_MSG_HIDE);
        Post(BOX_OK, CONTROL_MSG_SHOW);
        break;
    default:
        break;
    }
}

} // namespace

void C_MessageBox::ShowText(int kind, const C_str &text, uint32_t yesMessage, uint32_t noMessage)
{
    if (m_kind != MESSAGE_BOX_NONE)
        return;
    ShowButtons(kind);
    S_ControlMessage message;
    message.target = BOX_TEXT;
    message.id = CONTROL_MSG_SET_TEXT;
    message.value = kind == MESSAGE_BOX_YES_NO_2 ? BOX_STYLE_2 : BOX_STYLE;
    message.text = text;
    PostControlMessage(message);
    m_yesMessage = yesMessage;
    m_noMessage = noMessage;
    m_kind = kind;
    Post(0, MENU_MSG_MESSAGE_BOX_SHOWN);
}

void C_MessageBox::ShowTextId(int kind, uint32_t textId, uint32_t yesMessage, uint32_t noMessage)
{
    if (m_kind != MESSAGE_BOX_NONE)
        return;
    ShowButtons(kind);
    S_ControlMessage message;
    message.target = BOX_TEXT;
    message.id = CONTROL_MSG_SET_TEXT_ID;
    message.value = kind == MESSAGE_BOX_YES_NO_2 ? BOX_STYLE_2 : BOX_STYLE;
    message.value2 = textId;
    PostControlMessage(message);
    m_yesMessage = yesMessage;
    m_noMessage = noMessage;
    m_kind = kind;
    Post(0, MENU_MSG_MESSAGE_BOX_SHOWN);
}

void C_MessageBox::Close()
{
    m_kind = MESSAGE_BOX_NONE;
    Post(0, MENU_MSG_MESSAGE_BOX_CLOSED);
}

bool C_MessageBox::Unknown20(int key)
{
    if (m_kind == MESSAGE_BOX_TEXT || m_kind == MESSAGE_BOX_NONE)
        return true;
    if (static_cast<uint8_t>(key) != KEY_ESCAPE)
        return C_Screen::Unknown20(key);
    Post(0, MENU_MSG_MESSAGE_BOX_CLOSED);
    m_kind = MESSAGE_BOX_NONE;
    if (m_noMessage)
        Post(0, m_noMessage);
    ClearAnswers();
    return true;
}

bool C_MessageBox::Build(const S_ScreenDesc *desc)
{
    // (The original added to the description it was given.)
    auto *own = const_cast<S_ScreenDesc *>(desc);
    const uintptr_t self = reinterpret_cast<uintptr_t>(this);
    const uintptr_t onButton = reinterpret_cast<uintptr_t>(&C_MessageBox::OnButton);
    S_ControlDesc *control = own->AddControl(0x13, "text");
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, BOX_STYLE, TEXT_BOX);
    control = own->AddControl(0x14, "byes*");
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_YES);
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, onButton);
    control = own->AddControl(0x14, "bno*");
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_NO);
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, onButton);
    control = own->AddControl(0x14, "bok");
    AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_OK);
    AddDescMessage(control, 0, CONTROL_MSG_HIDE, 0);
    AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, onButton);
    return C_Screen::Build(desc);
}

bool C_MessageBox::OnButton(void *object, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *box = static_cast<C_MessageBox *>(object);
    if (!control)
        return false;
    if (box->m_kind == MESSAGE_BOX_NONE)
        return true;
    Post(0, MENU_MSG_MESSAGE_BOX_CLOSED);
    box->m_kind = MESSAGE_BOX_NONE;
    switch (control->GetId() >> 20 & 0x3F) {
    case BUTTON_YES:
    case BUTTON_OK:
        if (box->m_yesMessage)
            Post(0, box->m_yesMessage);
        break;
    case BUTTON_NO:
        if (box->m_noMessage)
            Post(0, box->m_noMessage);
        break;
    default:
        break;
    }
    box->ClearAnswers();
    return true;
}
