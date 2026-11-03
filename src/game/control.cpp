#include "game/control.h"

#include "ls3d/i3d.h"

// --- the descriptions and the queue (part 9c-i) ----------------------------------------------------

std::deque<S_ControlMessage> g_controlMessages;         // 0x008ae590

void PostControlMessage(const S_ControlMessage &message)
{
    g_controlMessages.push_back(message);
}

S_ControlDesc::S_ControlDesc(int type, const char *name, uint32_t id) : name(name), id(id), type(type) {}

S_ControlDesc::~S_ControlDesc()
{
    while (!children.empty()) {
        delete children.back();
        children.pop_back();
    }
}

S_ControlDesc *S_ControlDesc::AddChild(int type, const char *childName)
{
    const uint32_t index = static_cast<uint32_t>(children.size()) + 1 & 0xFF;
    const uint32_t childId = (id & 0xFF000) == 0 ? index << 12 | id : index << 4 | id;
    children.push_back(new S_ControlDesc(type, childName, childId));
    return children.back();
}

void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    desc->messages.push_back(message);
}

void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    desc->messages.push_back(message);
}

void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2,
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

void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value, const C_str &text)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.text = text;
    desc->messages.push_back(message);
}

// --- the control base (part 9c-i) ------------------------------------------------------------------

namespace {

// A route's target: the control itself.
constexpr uint32_t TARGET_SELF = 0xFFFFFFFF;
// A route's message: the notification's bits (CONTROL_NOTIFY_MASK) cleared.
constexpr uint32_t ROUTE_MESSAGE_MASK = ~CONTROL_NOTIFY_MASK;
// A raised notification's bit (CONTROL_MSG_NOTIFY).
constexpr uint32_t NOTIFICATION_BIT = 0x4000000;
// The ids' fields: a child's index (+ 1) in the first, the second, the third.
constexpr uint32_t ID_FIELD1 = 0x3F00000, ID_FIELD2 = 0xFF000;
// FindControl: no id; the ids' top bits (the screen's), the same for the whole search.
constexpr uint32_t NO_ID = 0xFFFFFFFF;
constexpr uint32_t ID_TOP_MASK = 0xFC000000;

} // namespace

C_Control::C_Control(I3D_scene *scene, uint32_t id, C_Control *parent) : m_parent(parent), m_id(id), m_scene(scene) {}

C_Control::~C_Control()
{
    while (!m_children.empty()) {
        delete m_children.back();
        m_children.pop_back();
    }
}

int C_Control::FindChild(uint32_t id) const
{
    // The field after this control's own: its children's index.
    int shift = 4;
    uint32_t mask = 0xFF;
    if ((m_id & ID_FIELD2) == 0) {
        if ((m_id & ID_FIELD1) == 0) {
            shift = 20;
            mask = 0x3F;
        } else {
            shift = 12;
        }
    }
    for (size_t i = 0; i < m_children.size(); ++i)
        if ((m_children[i]->m_id >> shift & mask) == (id >> shift & mask))
            return static_cast<int>(i);
    return -1;
}

bool C_Control::IsChild(int index) const
{
    return index >= 0 && static_cast<size_t>(index) < m_children.size();
}

bool C_Control::TakesInput() const
{
    return m_takesInput && m_shown && !m_inactive;
}

void C_Control::Show()
{
    m_shown = true;
    m_frame->SetOn(true);
    m_frame->Update();
    Notify(CONTROL_NOTIFY_SHOWN, 0);
}

void C_Control::Hide()
{
    m_shown = false;
    m_frame->SetOn(false);
    m_frame->Update();
    Notify(CONTROL_NOTIFY_HIDDEN, 0);
}

void C_Control::Refresh()
{
    if (!m_shown)
        return;
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->Refresh();
}

bool C_Control::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    m_frame = frame;
    // (The frame's data: the control's id.)
    frame->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(m_id)));
    if (desc) {
        m_type = desc->type;
        for (const S_ControlDesc *child : desc->children)
            CreateChild(frame, root, child);
    }
    return true;
}

int C_Control::OnEnter(int)
{
    Notify(CONTROL_NOTIFY_ENTER, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnLeave(int)
{
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnClick(int)
{
    Notify(CONTROL_NOTIFY_CLICK, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnInput02(int)
{
    Notify(CONTROL_NOTIFY_02, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnInput05(int)
{
    Notify(CONTROL_NOTIFY_05, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnRelease(int)
{
    Notify(CONTROL_NOTIFY_RELEASE, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnPress(int)
{
    Notify(CONTROL_NOTIFY_PRESS, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnWheel(int, int)
{
    Notify(CONTROL_NOTIFY_WHEEL, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnInput08(int)
{
    Notify(CONTROL_NOTIFY_08, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnInput09(int)
{
    Notify(CONTROL_NOTIFY_09, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnInput0A(int)
{
    Notify(CONTROL_NOTIFY_0A, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnKeyClick(int id)
{
    Notify(CONTROL_NOTIFY_KEY_CLICK, 0);
    return OnClick(id);
}

int C_Control::OnInput13(int)
{
    Notify(CONTROL_NOTIFY_13, 0);
    return CONTROL_NOT_HANDLED;
}

int C_Control::OnInput0B(int)
{
    Notify(CONTROL_NOTIFY_0B, 0);
    return CONTROL_NOT_HANDLED;
}

bool C_Control::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        m_routes.push_back(*message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_DEACTIVATE:
        if (!m_inactive)
            SetInactive(true);
        break;
    case CONTROL_MSG_ACTIVATE:
        if (m_inactive)
            SetInactive(false);
        break;
    case CONTROL_MSG_SHOW:
        Show();
        break;
    case CONTROL_MSG_HIDE:
        Hide();
        break;
    case CONTROL_MSG_IGNORE_INPUT:
        m_takesInput = false;
        break;
    case CONTROL_MSG_TAKE_INPUT:
        m_takesInput = true;
        break;
    case CONTROL_MSG_ADD_CALLBACK:
        AddCallback(*message);
        break;
    case CONTROL_MSG_REMOVE_CALLBACK:
        RemoveCallback(*message);
        break;
    case CONTROL_MSG_SET_INDEX:
        m_index = static_cast<int>(message->value);
        break;
    case CONTROL_MSG_GET_INDEX:
        message->value = static_cast<uint32_t>(m_index);
        break;
    case CONTROL_MSG_SET_VALUE:
        m_value = message->value;
        break;
    case CONTROL_MSG_GET_VALUE:
        message->value = m_value;
        break;
    case CONTROL_MSG_NOTIFY:
        if (message->value & NOTIFICATION_BIT)
            Notify(message->value, 0);
        break;
    case CONTROL_MSG_REFRESH:
        for (size_t i = 0; i < m_children.size(); ++i) {
            // (Fix: its other values 0; the original's are its stack's.)
            S_ControlMessage refresh;
            refresh.id = CONTROL_MSG_REFRESH;
            m_children[i]->HandleMessageCopy(refresh);
        }
        break;
    }
    return true;
}

bool C_Control::HandleMessages(std::vector<S_ControlMessage> &messages)
{
    for (size_t i = 0; i < messages.size(); ++i)
        HandleMessage(&messages[i]);
    return true;
}

bool C_Control::HandleMessageCopy(const S_ControlMessage &message)
{
    S_ControlMessage copy = message;
    return HandleMessage(&copy);
}

void C_Control::Notify(uint32_t notification, uint32_t value)
{
    CallCallbacks(notification, value);
    for (size_t i = 0; i < m_routes.size(); ++i) {
        if ((m_routes[i].id & CONTROL_NOTIFY_MASK) != notification)
            continue;
        S_ControlMessage message = m_routes[i];
        message.id &= ROUTE_MESSAGE_MASK;
        if (message.target != TARGET_SELF)
            PostControlMessage(message);
        else
            HandleMessage(&message);
    }
}

C_Control *C_Control::FindControl(uint32_t id, bool up)
{
    C_Control *control = this;
    while (control->m_id != NO_ID && id != NO_ID && ((control->m_id ^ id) & ID_TOP_MASK) == 0) {
        if (control->m_id == id)
            return control;
        if (up && control->m_parent) {
            control = control->m_parent;
            continue;
        }
        const int index = control->FindChild(id);
        if (index == -1)
            return nullptr;
        up = false;
        control = control->m_children[index];
    }
    return nullptr;
}

bool C_Control::CallCallbacks(uint32_t notification, uint32_t value)
{
    if (m_anyCallback.notification == CONTROL_NOTIFY_ANY)
        return m_anyCallback.function(m_anyCallback.object, nullptr, this, notification, value);
    for (const S_ControlCallback &callback : m_callbacks)
        if (callback.notification == notification && callback.function)
            return callback.function(callback.object, callback.object, this, notification, value);
    return true;
}

bool C_Control::AddCallback(const S_ControlMessage &message)
{
    const auto function = reinterpret_cast<ControlCallback>(message.value3);
    if (message.value == 0 || !function)
        return false;
    const S_ControlCallback callback = {message.value, reinterpret_cast<void *>(message.value2), function};
    if (message.value == CONTROL_NOTIFY_ANY)
        m_anyCallback = callback;
    else
        m_callbacks.push_back(callback);
    return true;
}

bool C_Control::RemoveCallback(const S_ControlMessage &message)
{
    if (message.value == CONTROL_NOTIFY_ANY)
        m_anyCallback = S_ControlCallback();
    // From the last: by the message's object, else its notification, else its function (the last
    // of them given decides; none: nothing removed).
    bool match = false;
    for (size_t i = m_callbacks.size(); i-- > 0;) {
        const S_ControlCallback &callback = m_callbacks[i];
        if (message.value3 != 0)
            match = message.value3 == reinterpret_cast<uintptr_t>(callback.function);
        if (message.value != 0)
            match = message.value == callback.notification;
        if (message.value2 != 0)
            match = message.value2 == reinterpret_cast<uintptr_t>(callback.object);
        if (match) {
            m_callbacks.erase(m_callbacks.begin() + static_cast<std::ptrdiff_t>(i));
            match = false;
        }
    }
    return true;
}
