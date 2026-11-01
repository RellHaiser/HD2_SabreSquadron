#include "game/controls.h"

#include <cmath>
#include <cstring>

#include "game/cursor.h"
#include "game/mp_zone.h"
#include "game/pending.h"
#include "game/paged_text_box.h"
#include "game/text_box.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// --- the small controls (part 9c-ii) ---------------------------------------------------------------

namespace {

// The titles' and labels' frames turned to face this way; FACE_FRONT's.
const S_vector FACE_BACK(0.0f, 0.0f, -1.0f);
const S_vector FACE_FRONT(0.0f, 0.0f, 1.0f);
// The frames' children found by name (FindChildFrame's type mask).
constexpr uint32_t FIND_FRAMES = 0x10001;
constexpr uint32_t FIND_ANY = 0xFFFF;
// LoadTexture's mode.
constexpr uint32_t TEXTURE_MODE = 0x400;
// A picture's visual's alpha while inactive.
constexpr float INACTIVE_ALPHA = 0.4f;
// The tooltip: the cursor's still time it waits, its place from the cursor.
constexpr int TOOLTIP_WAIT = 500;
constexpr float TOOLTIP_DX = 0.15f, TOOLTIP_DY = 0.015f;
// The button: the cursor's still time before its press repeats.
constexpr int REPEAT_WAIT = 300;
// The group: the enter's id that asks Unknown20 instead.
constexpr int GROUP_ENTER_UNKNOWN = 0xF;
// The arrows' and the slider's frames (dummies, wildcards).
constexpr uint32_t FIND_DUMMIES = 0x10020;
// The slider: the interface's limits (x 0 .. 1, y -0.75 .. 0), its value at first.
constexpr float SLIDER_MAX_X = 1.0f, SLIDER_MIN_Y = -0.75f;
constexpr float SLIDER_START = 50.0f;
// The scroll bar: the wait before its held steps, between them; the wheel's step.
constexpr int SCROLL_HOLD_WAIT = 300, SCROLL_REPEAT = 50;
constexpr int WHEEL_STEP = 120;
// The children's ids' last 4 bits (their index + 1 in the arrows' and the bar's).
constexpr uint32_t CHILD_ID_MASK = 0xF;
constexpr uint32_t UP_ARROW = 1, DOWN_ARROW = 2, SLIDER = 3;
// The spin control's items' style.
constexpr uint8_t SPIN_STYLE = 0x14;
// The check boxes' frames' ids' last bits (their halves' too), the toggle buttons'.
constexpr uint32_t CHECK_NO = 1, CHECK_YES = 2;
// The highlighted check box's halves' ids' last bits (the yes half's 1: unlike type 9's frames).
constexpr uint32_t HALF_YES = 1, HALF_NO = 2;
constexpr uint32_t TOGGLE_DOWN = 1, TOGGLE_UP = 2;
// The side indicator: the time between its looks at the zone; the sides (TEAM_*).
constexpr int SIDE_PERIOD = 2000;
constexpr int SIDE_AXIS = 0, SIDE_ALLIED = 1, SIDE_NONE = 2;
// The mark's title's style.
constexpr uint32_t MARK_STYLE = 0xB;

// A frame's data: the control's id.
void SetFrameId(I3D_frame *frame, uint32_t id)
{
    frame->SetData(reinterpret_cast<S_FrameData *>(static_cast<uintptr_t>(id)));
}

// A message with only its code (the original's others are its stack's: the fix, 0).
S_ControlMessage Message(uint32_t id)
{
    S_ControlMessage message;
    message.id = id;
    return message;
}

// A route: on the notification in `id`, the message in it to `target`.
S_ControlMessage Route(uint32_t target, uint32_t id)
{
    S_ControlMessage message = Message(id);
    message.target = target;
    return message;
}

// A float in a message's value2 (its bits).
uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
float ValueFloat(uintptr_t value)
{
    const auto bits = static_cast<uint32_t>(value);
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

// The index of the child whose id's last 4 bits are `id`'s (none: -1).
int FindChildByLastBits(const std::vector<C_Control *> &children, uint32_t id)
{
    for (size_t i = 0; i < children.size(); ++i)
        if (((children[i]->GetId() ^ id) & CHILD_ID_MASK) == 0)
            return static_cast<int>(i);
    return -1;
}

// An arrow (a button) made on the frame's child `name` (none: null), routing its press to `owner`'s
// message `pressMessage`.
C_Button *MakeArrow(C_Control *owner, I3D_scene *scene, I3D_frame *frame, I3D_frame *root, const char *name,
                    uint32_t id, uint32_t pressMessage)
{
    I3D_frame *arrow = frame->FindChildFrame(name, FIND_DUMMIES);
    if (!arrow)
        return nullptr;
    auto *button = new C_Button(scene, id, owner);
    button->Create(arrow, root, nullptr);
    button->HandleMessageCopy(Route(owner->GetId(), CONTROL_NOTIFY_PRESS | pressMessage));
    return button;
}

} // namespace

// ----- the text control (vtable 0x00813e58) -----

C_TextControl::C_TextControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}

C_TextControl::~C_TextControl()
{
    delete m_box;
}

void C_TextControl::AttachBox()
{
    m_box->AttachTo(m_scene, m_frame);
}

void C_TextControl::Refresh()
{
    if (!m_box || !m_shown)
        return;
    m_box->Render();
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->Refresh();
}

bool C_TextControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return false;
    m_box = new C_TextBox();
    return m_box != nullptr;
}

bool C_TextControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    const auto style = static_cast<uint8_t>(message->value);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_TEXT:
        m_box->SetText(message->text, style, 0);
        return true;
    case CONTROL_MSG_SET_TEXT_ID:
        m_box->SetTextId(static_cast<int>(message->value2), static_cast<int>(message->value), 0);
        return true;
    case CONTROL_MSG_ADD_TEXT:
        m_box->AddText(message->text, style);
        return true;
    case CONTROL_MSG_ADD_TEXT_ID:
        m_box->AddTextId(static_cast<int>(message->value2), style);
        return true;
    case CONTROL_MSG_SET_RUN_TEXT:
        m_box->SetText(message->text, style, static_cast<int>(message->value2));
        return true;
    case CONTROL_MSG_REMOVE_RUN:
        m_box->RemoveRun(static_cast<int>(message->value));
        return true;
    case CONTROL_MSG_CLEAR_TEXT:
    case CONTROL_MSG_CLEAR:
        m_box->Clear();
        return true;
    case CONTROL_MSG_FACE_FRONT:
        m_frame->SetDir(FACE_FRONT, 0.0f);
        AttachBox();
        return true;
    case CONTROL_MSG_SET_COLOUR:
        m_box->SetColour(message->value);
        return true;
    case CONTROL_MSG_GET_RUNS:
        message->value = static_cast<uint32_t>(m_box->NumRuns());
        return true;
    case CONTROL_MSG_SET_MARGIN:
        m_box->SetUnknown90(static_cast<uint8_t>(message->value));
        AttachBox();
        return true;
    case CONTROL_MSG_REFRESH:
        AttachBox();
        return true;
    }
    return C_Control::HandleMessage(message);
}

void C_TextControl::SetInactive(bool inactive)
{
    m_inactive = inactive;
    if (m_box)
        m_box->SetDimmed(inactive);
}

// ----- the title (type 0x13) and the label (type 0x12) -----

C_TitleControl::C_TitleControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent)
{
    m_takesInput = false;
}

bool C_TitleControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc))
        return false;
    m_frame->SetDir(FACE_BACK, 0.0f);
    SetFrameId(m_frame, m_id);
    m_frame->Update();
    if (!m_box)
        return false;
    m_box->SetUnknown90(1);
    AttachBox();
    return true;
}

void C_LabelControl::Refresh()
{
    if (m_box && m_shown)
        m_box->Render();
}

bool C_LabelControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc))
        return false;
    m_frame->SetDir(FACE_BACK, 0.0f);
    SetFrameId(m_frame, m_id);
    m_frame->Update();
    if (!m_box)
        return false;
    AttachBox();
    return true;
}

bool C_LabelControl::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_ADD_TEXT:
            m_box->AddText(message->text, static_cast<uint8_t>(message->value));
            return true;
        case CONTROL_MSG_CLEAR:
            m_box->Clear();
            return true;
        }
    }
    return C_TextControl::HandleMessage(message);
}

// ----- the picture (type 4) -----

bool C_PictureControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc))
        return false;
    I3D_mesh *mesh = static_cast<I3D_visual *>(m_frame)->GetVisualMesh();
    m_faceGroup = mesh->GetLevel(0)->FaceGroups();
    I3D_material *material = m_faceGroup->material;
    m_texture = material ? material->GetTexture() : nullptr;
    if (!m_box)
        return false;
    m_box->SetUnknown90(1);
    AttachBox();
    return true;
}

bool C_PictureControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_TextControl::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_RESTORE_TEXTURE:
        m_faceGroup->material->SetTexture(m_texture);
        return true;
    case CONTROL_MSG_SET_TEXTURE: {
        I3D_material *material = m_faceGroup->material;
        if (!material)
            return true;
        if (message->value2 != 0) {
            material->SetTexture(reinterpret_cast<I3D_texture *>(message->value2));
            Show();
        } else {
            Hide();
            material->SetTexture(nullptr);
        }
        return true;
    }
    case CONTROL_MSG_LOAD_TEXTURE: {
        IImage *image = nullptr;
        if (GetIGraph()->LoadTexture(message->text.c_str(), nullptr, TEXTURE_MODE, &image) < 0) {
            m_faceGroup->material->SetTexture(m_texture);
        } else {
            m_faceGroup->material->SetTexture(reinterpret_cast<I3D_texture *>(image));
            image->Release();
        }
        return true;
    }
    case CONTROL_MSG_FIND_FACE_GROUP: {
        if (message->text.IsEmpty())
            return true;
        I3D_mesh *mesh = static_cast<I3D_visual *>(m_frame)->GetVisualMesh();
        for (I3D_face_group *group = mesh->GetLevel(0)->FaceGroups(); group; group = group->next) {
            // (Each face group's material and texture taken to have one.)
            if (std::strcmp(group->material->GetTexture()->GetFileName(), message->text.c_str()) != 0)
                continue;
            m_faceGroup = group;
            m_texture = group->material ? group->material->GetTexture() : nullptr;
            return true;
        }
        return true;
    }
    case CONTROL_MSG_REFRESH:
        // The children's too, then the box placed.
        C_Control::HandleMessage(message);
        break;
    }
    return C_TextControl::HandleMessage(message);
}

void C_PictureControl::SetInactive(bool inactive)
{
    m_inactive = inactive;
    static_cast<I3D_object_visual *>(m_frame)->SetAlpha(inactive ? INACTIVE_ALPHA : 1.0f);
}

// ----- the group (type 5) -----

void C_ControlGroup::Tick(int time)
{
    if (C_Control *focus = Focus())
        focus->Tick(time);
}

bool C_ControlGroup::Unknown20(int value)
{
    C_Control *focus = Focus();
    return focus && focus->TakesInput() && focus->Unknown20(value);
}

bool C_ControlGroup::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    return true;
}

int C_ControlGroup::OnEnter(int id)
{
    if (id == GROUP_ENTER_UNKNOWN)
        return Unknown20(GROUP_ENTER_UNKNOWN) ? CONTROL_NOT_HANDLED : 0;
    const int index = FindChild(static_cast<uint32_t>(id));
    if (!IsChild(index) || !m_children[index]->TakesInput())
        return 0;
    m_focus = index;
    return m_children[index]->OnEnter(id);
}

int C_ControlGroup::OnLeave(int id)
{
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return 0;
        m_focus = -1;
    }
    return CONTROL_HANDLED;
}

int C_ControlGroup::OnClick(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnClick(id) : 0;
}

int C_ControlGroup::OnInput02(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnInput02(id) : 0;
}

int C_ControlGroup::OnInput05(int id)
{
    if (!IsChild(FindChild(static_cast<uint32_t>(id))))
        return OnLeave(id);
    if (m_focus != FindChild(static_cast<uint32_t>(id))) {
        if (OnLeave(id) == 0)
            return CONTROL_HANDLED;
        return OnEnter(id);
    }
    return m_children[m_focus]->OnInput05(id);
}

int C_ControlGroup::OnRelease(int id)
{
    if (IsChild(m_focus) && m_focus == FindChild(static_cast<uint32_t>(id)))
        return m_children[m_focus]->OnRelease(id);
    return 0;
}

int C_ControlGroup::OnPress(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnPress(id) : 0;
}

int C_ControlGroup::OnWheel(int id, int amount)
{
    C_Control *focus = Focus();
    return focus ? focus->OnWheel(id, amount) : CONTROL_NOT_HANDLED;
}

int C_ControlGroup::OnInput08(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnInput08(id) : 0;
}

int C_ControlGroup::OnInput09(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnInput09(id) : 0;
}

int C_ControlGroup::OnInput0A(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnInput0A(id) : 0;
}

int C_ControlGroup::OnKeyClick(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnKeyClick(id) : CONTROL_NOT_HANDLED;
}

int C_ControlGroup::OnInput13(int id)
{
    C_Control *focus = Focus();
    return focus ? focus->OnInput13(id) : CONTROL_NOT_HANDLED;
}

bool C_ControlGroup::HandleMessage(S_ControlMessage *message)
{
    const int index = FindChild(message->target);
    if (IsChild(index))
        return m_children[index]->HandleMessage(message);
    C_Control::HandleMessage(message);
    return true;
}

// ----- the frame switch (type 0xc) -----

C_FrameSwitch::C_FrameSwitch(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent)
{
    m_takesInput = false;
}

bool C_FrameSwitch::IsChild(int index) const
{
    return index >= 0 && static_cast<size_t>(index) < m_frames.size();
}

bool C_FrameSwitch::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_ADD_FRAME:
            if (I3D_frame *frame = m_frame->FindChildFrame(reinterpret_cast<const char *>(message->value2), FIND_FRAMES)) {
                frame->SetOn(false);
                m_frames.push_back(frame);
            }
            return true;
        case CONTROL_MSG_SELECT_FRAME:
            if (IsChild(m_selected))
                m_frames[m_selected]->SetOn(false);
            if (IsChild(static_cast<int>(message->value))) {
                m_frames[message->value]->SetOn(true);
                m_selected = static_cast<int>(message->value);
            }
            return true;
        case CONTROL_MSG_HIDE_FRAME:
            if (IsChild(m_selected))
                m_frames[m_selected]->SetOn(false);
            return true;
        case CONTROL_MSG_IGNORED_25:
            return true;
        }
    }
    C_Control::HandleMessage(message);
    return true;
}

// ----- the text button (type 0x14) -----

bool C_TextButton::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc))
        return false;
    m_normalFrame = frame->FindChildFrame("normal*", FIND_FRAMES);
    m_activeFrame = frame->FindChildFrame("actived*", FIND_FRAMES);
    if (!m_normalFrame || !m_activeFrame)
        return false;
    m_activeFrame->SetPosInline(m_normalFrame->GetPos());   // (not when its place is locked)
    m_activeFrame->SetOn(false);
    m_activeFrame->Update();
    SetFrameId(m_normalFrame, m_id);
    SetFrameId(m_activeFrame, m_id);
    if (m_box)
        m_box->AttachTo(m_scene, m_normalFrame);
    return true;
}

int C_TextButton::OnEnter(int)
{
    if (m_inactive)
        return 0;
    m_activeFrame->SetOn(true);
    m_normalFrame->SetOn(false);
    m_activeFrame->Update();
    m_normalFrame->Update();
    Notify(CONTROL_NOTIFY_ENTER, 0);
    return CONTROL_HANDLED;
}

int C_TextButton::OnLeave(int)
{
    m_normalFrame->SetOn(true);
    m_activeFrame->SetOn(false);
    m_normalFrame->Update();
    m_activeFrame->Update();
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return CONTROL_HANDLED;
}

int C_TextButton::OnClick(int)
{
    if (m_inactive)
        return 0;
    Notify(CONTROL_NOTIFY_CLICK, 0);
    return CONTROL_HANDLED;
}

bool C_TextButton::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_REFRESH:
            if (m_box)
                m_box->AttachTo(m_scene, m_normalFrame);
            return true;
        case CONTROL_MSG_WHITE_TEXT:
            if (m_box)
                m_box->SetColour(message->value != 0 ? 0xFFFFFFFF : 0);
            return true;
        }
    }
    return C_TextControl::HandleMessage(message);
}

// ----- the tooltip (type 0x15) -----

C_Tooltip::C_Tooltip(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent)
{
    m_takesInput = false;
}

C_Tooltip::~C_Tooltip()
{
    if (m_cursor)
        m_cursor->Release();
}

bool C_Tooltip::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    // (True even when the text control is not made.)
    if (!C_TextControl::Create(frame, root, desc))
        return true;
    m_cursor = C_Cursor::Acquire(m_scene);
    S_ControlMessage message = Message(CONTROL_MSG_ADD_TICKER);
    message.value2 = reinterpret_cast<uintptr_t>(this);
    PostControlMessage(message);
    return true;
}

bool C_Tooltip::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SHOW:
        m_armed = true;
        return true;
    case CONTROL_MSG_HIDE:
        m_armed = false;
        break;
    }
    return C_TextControl::HandleMessage(message);
}

void C_Tooltip::Tick(int)
{
    if (!m_armed)
        return;
    if (m_cursor->GetStillTime() < TOOLTIP_WAIT || m_cursor->IsButtonDown(0)) {
        if (m_shown) {
            S_ControlMessage hide = Message(CONTROL_MSG_HIDE);
            C_TextControl::HandleMessage(&hide);
        }
        return;
    }
    S_vector position;
    m_cursor->GetPosition(&position);
    position.x += TOOLTIP_DX;
    position.z = m_frame->GetWorldPos().z;
    position.y += TOOLTIP_DY;
    m_frame->SetPos(position);
    m_frame->Update();
    S_ControlMessage refresh = Message(CONTROL_MSG_REFRESH);
    C_TextControl::HandleMessage(&refresh);
    S_ControlMessage show = Message(CONTROL_MSG_SHOW);
    C_TextControl::HandleMessage(&show);
}

// ----- the button (type 2) -----

C_Button::~C_Button()
{
    if (m_cursor)
        m_cursor->Release();
}

void C_Button::UpdateFrames()
{
    m_normalFrame->Update();
    m_activeFrame->Update();
    m_inactiveFrame->Update();
}

void C_Button::Tick(int)
{
    if (!m_held || m_repeatTime > m_cursor->GetStillTime())
        return;
    if (!m_cursor->IsButtonDown(0)) {
        m_held = false;
        m_repeatTime = REPEAT_WAIT;
        return;
    }
    m_repeatTime = -1;
    Notify(CONTROL_NOTIFY_PRESS, 0);
}

bool C_Button::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    m_cursor = C_Cursor::Acquire(m_scene);
    if (frame->NumChildren() != 3)
        return false;
    m_normalFrame = frame->FindChildFrame("normal*", FIND_FRAMES);
    m_activeFrame = frame->FindChildFrame("actived*", FIND_FRAMES);
    m_inactiveFrame = frame->FindChildFrame("inactived*", FIND_FRAMES);
    SetFrameId(m_normalFrame, m_id);
    m_normalFrame->SetOn(true);
    m_normalFrame->Update();
    for (I3D_frame *other : {m_activeFrame, m_inactiveFrame}) {
        other->SetPosInline(m_normalFrame->GetPos());   // (not when its place is locked)
        other->SetOn(false);
        SetFrameId(other, m_id);
        other->Update();
    }
    return true;
}

int C_Button::OnEnter(int)
{
    if (m_inactive)
        return 0;
    m_activeFrame->SetOn(true);
    m_normalFrame->SetOn(false);
    m_inactiveFrame->SetOn(false);
    m_inactiveFrame->Update();
    m_activeFrame->Update();
    m_normalFrame->Update();
    return CONTROL_HANDLED;
}

int C_Button::OnLeave(int)
{
    m_held = false;
    if (!m_inactive) {
        m_normalFrame->SetOn(true);
        m_activeFrame->SetOn(false);
        m_inactiveFrame->SetOn(false);
        UpdateFrames();
    }
    return CONTROL_HANDLED;
}

int C_Button::OnRelease(int)
{
    m_held = false;
    return CONTROL_HANDLED;
}

int C_Button::OnPress(int)
{
    if (!m_inactive) {
        m_held = true;
        m_repeatTime = REPEAT_WAIT;
        Notify(CONTROL_NOTIFY_PRESS, 0);
    }
    return CONTROL_HANDLED;
}

int C_Button::OnKeyClick(int)
{
    Notify(CONTROL_NOTIFY_KEY_CLICK, 0);
    return CONTROL_HANDLED;
}

void C_Button::SetInactive(bool inactive)
{
    m_inactive = inactive;
    m_normalFrame->SetOn(!inactive);
    m_activeFrame->SetOn(false);
    m_inactiveFrame->SetOn(inactive);
    UpdateFrames();
}

// ----- the paged text control (vtable 0x00815420) and the pages (type 0x25) -----

C_PagedTextControl::~C_PagedTextControl()
{
    delete m_box;
}

void C_PagedTextControl::Refresh()
{
    if (!m_box || !m_shown)
        return;
    m_box->Render();
    for (size_t i = 0; i < m_children.size(); ++i)
        m_children[i]->Refresh();
}

bool C_PagedTextControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return false;
    m_box = new C_PagedTextBox();
    return m_box != nullptr;
}

bool C_PagedTextControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    const auto style = static_cast<uint8_t>(message->value);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_TEXT:
        m_box->SetText(message->text, style, 0);
        return true;
    case CONTROL_MSG_SET_TEXT_ID:
        m_box->SetTextId(static_cast<int>(message->value2), static_cast<int>(message->value), 0);
        return true;
    case CONTROL_MSG_ADD_TEXT:
        m_box->AddText(message->text, style);
        return true;
    case CONTROL_MSG_ADD_TEXT_ID:
        m_box->AddTextId(static_cast<int>(message->value2), style);
        return true;
    case CONTROL_MSG_SET_RUN_TEXT:
        m_box->SetText(message->text, style, static_cast<int>(message->value2));
        return true;
    case CONTROL_MSG_REMOVE_RUN:
        m_box->RemoveRun(static_cast<int>(message->value));
        return true;
    case CONTROL_MSG_CLEAR_TEXT:
    case CONTROL_MSG_CLEAR:
        m_box->Clear();
        return true;
    case CONTROL_MSG_GET_RUNS:
        message->value = static_cast<uint32_t>(m_box->NumRuns());
        return true;
    case CONTROL_MSG_SET_MARGIN:
        m_box->SetUnknown90(static_cast<uint8_t>(message->value));
        m_box->Reattach(m_scene, m_frame);
        return true;
    case CONTROL_MSG_REFRESH:
        m_box->Reattach(m_scene, m_frame);
        return true;
    }
    return C_Control::HandleMessage(message);
}

void C_PagedTextControl::SetInactive(bool inactive)
{
    m_inactive = inactive;
    if (m_box)
        m_box->SetDimmed(inactive);
}

void C_PagesControl::Refresh()
{
    if (m_box && m_shown)
        m_box->Render();
}

bool C_PagesControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_PagedTextControl::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_AREA:
        if (I3D_frame *frame = m_frame->FindChildFrame(message->text.c_str(), FIND_ANY)) {
            frame->SetDir(FACE_BACK, 0.0f);
            SetFrameId(frame, m_id);
            frame->Update();
            if (m_box)
                m_box->AddArea(m_scene, frame);
        }
        return true;
    case CONTROL_MSG_ADD_TEXT:
        m_box->AddText(message->text, static_cast<uint8_t>(message->value));
        return true;
    case CONTROL_MSG_SET_PAGE:
        if (m_box)
            m_box->SetPage(static_cast<int>(message->value));
        return true;
    case CONTROL_MSG_GET_PAGES:
        message->value = m_box ? static_cast<uint32_t>(m_box->NumPages()) : 0;
        return true;
    case CONTROL_MSG_GET_PAGE_BREAK:
        if (m_box)
            message->value = static_cast<uint32_t>(m_box->LastPageBreak());
        return true;
    case CONTROL_MSG_CLEAR:
        m_box->Clear();
        return true;
    case CONTROL_MSG_REFRESH:
        if (m_box)
            m_box->Relayout();
        return true;
    }
    return C_PagedTextControl::HandleMessage(message);
}

// ----- the slider (type 7) -----

C_Slider::~C_Slider()
{
    if (m_cursor)
        m_cursor->Release();
}

bool C_Slider::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    m_normalFrame = frame->FindChildFrame("normal*", FIND_FRAMES);
    m_activeFrame = frame->FindChildFrame("actived*", FIND_FRAMES);
    m_baseFrame = frame->FindChildFrame("sliderbase*", FIND_FRAMES);
    m_cursor = C_Cursor::Acquire(m_scene);
    if (!m_cursor || !m_normalFrame || !m_activeFrame || !m_baseFrame)
        return false;
    m_normalFrame->GetMatrix();
    m_activeFrame->SetPosInline(m_normalFrame->GetPos());   // (not when its place is locked)
    m_activeFrame->SetOn(false);
    SetFrameId(m_normalFrame, m_id);
    SetFrameId(m_activeFrame, m_id);
    m_normalFrame->Update();
    m_activeFrame->Update();
    m_baseFrame->SetOn(false);
    m_baseFrame->Update();
    SetLimits();
    return true;
}

void C_Slider::SetLimits()
{
    const I3D_bbox &own = m_normalFrame->GetBound().bbox;
    const float halfWidth = (own.max.x - own.min.x) * 0.5f;
    const float halfHeight = (own.min.y - own.max.y) * 0.5f;   // (negative)
    const I3D_bbox &base = m_baseFrame->GetBound().bbox;
    m_minX = halfWidth + base.min.x;
    m_maxX = base.max.x - halfWidth;
    m_minY = base.min.y - halfHeight;
    m_maxY = halfHeight + base.max.y;
    if (!(m_maxX <= SLIDER_MAX_X))
        m_maxX = SLIDER_MAX_X;
    else if (m_maxX < 0.0f)
        m_maxX = 0.0f;
    if (!(SLIDER_MIN_Y <= m_maxY))
        m_maxY = SLIDER_MIN_Y;
    else if (0.0f < m_maxY)
        m_maxY = 0.0f;
    if (!(m_minX <= SLIDER_MAX_X))
        m_minX = SLIDER_MAX_X;
    else if (m_minX < 0.0f)
        m_minX = 0.0f;
    if (!(SLIDER_MIN_Y <= m_minY))
        m_minY = SLIDER_MIN_Y;
    else if (0.0f < m_minY)
        m_minY = 0.0f;
    // From the normal frame's place (its z for the y: the original's).
    const S_vector place = m_normalFrame->GetPos();
    m_minX -= place.x;
    m_maxX -= place.x;
    m_minY -= place.z;
    m_maxY -= place.z;
    m_rangeY = m_maxY - m_minY;
    m_rangeX = m_maxX - m_minX;
    m_horizontal = !(m_rangeX < m_rangeY);
    m_vertical = !m_horizontal;
    SetValue(SLIDER_START);
}

int C_Slider::OnEnter(int)
{
    if (!m_inactive) {
        m_activeFrame->SetOn(true);
        m_normalFrame->SetOn(false);
        m_activeFrame->Update();
        m_normalFrame->Update();
        Notify(CONTROL_NOTIFY_ENTER, 0);
    }
    return CONTROL_HANDLED;
}

int C_Slider::OnLeave(int)
{
    m_normalFrame->SetOn(true);
    m_activeFrame->SetOn(false);
    m_normalFrame->Update();
    m_activeFrame->Update();
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return CONTROL_HANDLED;
}

int C_Slider::OnInput08(int)
{
    if (Drag())
        Notify(CONTROL_NOTIFY_08, 0);
    return CONTROL_HANDLED;
}

bool C_Slider::Drag()
{
    S_vector move;
    m_cursor->GetMove(&move);
    S_vector position = m_frame->GetWorldPos();
    S_vector cursor;
    if (m_vertical && move.y != 0.0f) {
        m_cursor->GetPosition(&cursor);
        position.y = cursor.y - m_normalFrame->GetPos().z;
        if (position.y < m_minY)
            position.y = m_minY;
        if (m_maxY < position.y)
            position.y = m_maxY;
        MoveTo(position);
        return SetValueFrom(position.y);
    }
    if (m_horizontal && move.x != 0.0f) {
        m_cursor->GetPosition(&cursor);
        position.x = cursor.x - m_normalFrame->GetPos().x;
        if (position.x < m_minX)
            position.x = m_minX;
        if (m_maxX < position.x)
            position.x = m_maxX;
        MoveTo(position);
        return SetValueFrom(position.x);
    }
    return false;
}

void C_Slider::MoveTo(const S_vector &position)
{
    m_frame->SetPos(position);
    m_frame->Update();
}

bool C_Slider::SetValueFrom(float place)
{
    float value = 0.0f;
    if (m_vertical)
        value = (m_maxY - place) / m_rangeY * 100.0f;
    if (m_horizontal)
        value = (place - m_minX) / m_rangeX * 100.0f;
    if (value == m_value)
        return false;
    m_value = value;
    return true;
}

void C_Slider::SetValue(float value)
{
    if (m_value == value)
        return;
    m_value = value;
    S_vector position = m_frame->GetWorldPos();
    if (m_value < 0.0f)
        m_value = 0.0f;
    if (100.0f < m_value)
        m_value = 100.0f;
    if (m_vertical) {
        position.y = m_maxY - m_rangeY * m_value * 0.01f;
        if (position.y < m_minY)
            position.y = m_minY;
        if (m_maxY < position.y)
            position.y = m_maxY;
        MoveTo(position);
    }
    if (m_horizontal) {
        position.x = m_rangeX * m_value * 0.01f + m_minX;
        if (position.x < m_minX)
            position.x = m_minX;
        if (m_maxX < position.x)
            position.x = m_maxX;
        MoveTo(position);
    }
}

bool C_Slider::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_SLIDER:
        SetValue(ValueFloat(message->value2));
        return true;
    case CONTROL_MSG_GET_SLIDER:
        message->value2 = FloatValue(m_value);
        return true;
    }
    return C_Control::HandleMessage(message);
}

void C_Slider::SetInactive(bool inactive)
{
    m_inactive = inactive;
    if (inactive) {
        m_normalFrame->SetOn(false);
        m_activeFrame->SetOn(false);
        m_normalFrame->Update();
        m_activeFrame->Update();
        return;
    }
    m_normalFrame->SetOn(true);
    m_normalFrame->Update();
}

// ----- the scroll bar (type 8) -----

int C_ScrollBar::FindChild(uint32_t id) const
{
    return FindChildByLastBits(m_children, id);
}

void C_ScrollBar::Tick(int time)
{
    if (!m_held)
        return;
    C_Cursor *cursor = C_Cursor::Acquire(nullptr);
    if (!cursor->IsButtonDown(0)) {
        m_held = false;
    } else {
        m_repeatTime -= time;
        if (m_repeatTime < 0) {
            StepToCursor();
            m_repeatTime = SCROLL_REPEAT;
        }
    }
    cursor->Release();
}

bool C_ScrollBar::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    S_ControlMessage ticker = Message(CONTROL_MSG_ADD_TICKER);
    m_upButton = MakeArrow(this, m_scene, frame, root, "barrow up*", m_id | UP_ARROW, CONTROL_MSG_SCROLL_UP);
    if (m_upButton) {
        m_children.push_back(m_upButton);
        ticker.value2 = reinterpret_cast<uintptr_t>(m_upButton);
        PostControlMessage(ticker);
    }
    m_downButton = MakeArrow(this, m_scene, frame, root, "barrow down*", m_id | DOWN_ARROW, CONTROL_MSG_SCROLL_DOWN);
    if (m_downButton) {
        m_children.push_back(m_downButton);
        ticker.value2 = reinterpret_cast<uintptr_t>(m_downButton);
        PostControlMessage(ticker);
    }
    if (I3D_frame *sliderFrame = frame->FindChildFrame("slider*", FIND_DUMMIES)) {
        m_slider = new C_Slider(m_scene, m_id | SLIDER, this);
        m_slider->Create(sliderFrame, root, nullptr);
        m_slider->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_08 | CONTROL_MSG_SLIDER_MOVED));
        m_slider->HandleMessageCopy(Message(CONTROL_MSG_SET_SLIDER));
        m_children.push_back(m_slider);
    }
    ticker.value2 = reinterpret_cast<uintptr_t>(this);
    PostControlMessage(ticker);
    SetInactive(true);
    return true;
}

int C_ScrollBar::OnEnter(int id)
{
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return CONTROL_NOT_HANDLED;
    }
    m_focus = FindChild(static_cast<uint32_t>(id));
    if (C_Control *focus = Focus())
        return focus->OnEnter(id);
    return 0;
}

int C_ScrollBar::OnLeave(int id)
{
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return 0;
        m_focus = -1;
    }
    return CONTROL_HANDLED;
}

int C_ScrollBar::OnInput05(int id)
{
    if (!IsChild(FindChild(static_cast<uint32_t>(id))))
        return OnLeave(id);
    if (m_focus != FindChild(static_cast<uint32_t>(id))) {
        if (OnLeave(id) == 0)
            return CONTROL_HANDLED;
        return OnEnter(id);
    }
    return m_children[m_focus]->OnInput05(id);
}

int C_ScrollBar::OnPress(int id)
{
    if (!m_inactive) {
        if (C_Control *focus = Focus())
            return focus->OnPress(id);
        m_held = true;
        m_repeatTime = SCROLL_HOLD_WAIT;
        StepToCursor();
    }
    return CONTROL_HANDLED;
}

int C_ScrollBar::OnWheel(int, int amount)
{
    if (amount > 0) {
        for (unsigned steps = (static_cast<unsigned>(amount) - 1) / WHEEL_STEP + 1; steps; --steps)
            HandleMessageCopy(Message(CONTROL_MSG_SCROLL_UP));
    } else if (amount < 0) {
        for (unsigned steps = (static_cast<unsigned>(-amount) - 1) / WHEEL_STEP + 1; steps; --steps)
            HandleMessageCopy(Message(CONTROL_MSG_SCROLL_DOWN));
    }
    return CONTROL_HANDLED;
}

int C_ScrollBar::OnInput08(int id)
{
    if (m_inactive)
        return CONTROL_HANDLED;
    if (C_Control *focus = Focus())
        return focus->OnInput08(id);
    return 0;
}

bool C_ScrollBar::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_COUNT: {
        const int count = static_cast<int>(message->value);
        if (count < 2) {
            m_count = 0;
            m_position = 0;
            m_step = 100.0f;
            SetInactive(true);
            Update();
            return true;
        }
        if (m_count == 0) {
            m_count = count;
            SetInactive(false);
        }
        m_count = count;
        m_step = 100.0f / static_cast<float>(count - 1);
        if (m_count <= m_position)
            m_position = m_count - 1;
        Update();
        return true;
    }
    case CONTROL_MSG_GET_SLIDER:
        if (m_slider)   // (the fix: the original's without a slider crashes)
            m_slider->HandleMessage(message);
        return true;
    case CONTROL_MSG_SCROLL_UP:
        if (m_position > 0) {
            m_position -= m_pageStep;
            if (m_position < 0)
                m_position = 0;
            Update();
            SendPosition();
            Notify(CONTROL_NOTIFY_CHANGED, 0);
        }
        return true;
    case CONTROL_MSG_SCROLL_DOWN:
        if (m_position < m_count - 1) {
            m_position += m_pageStep;
            if (m_position > m_count)   // (the count, not the last: the original's)
                m_position = m_count;
            Update();
            SendPosition();
            Notify(CONTROL_NOTIFY_CHANGED, 0);
        }
        return true;
    case CONTROL_MSG_SLIDER_MOVED: {
        S_ControlMessage get = Message(CONTROL_MSG_GET_SLIDER);
        m_slider->HandleMessage(&get);
        auto position = static_cast<uint32_t>(static_cast<int>(std::floor(ValueFloat(get.value2) / m_step + 0.5f)));
        if (position > static_cast<uint32_t>(m_count - 1))
            position = static_cast<uint32_t>(m_count - 1);
        if (position != static_cast<uint32_t>(m_position)) {
            m_position = static_cast<int>(position);
            if (m_value != 0) {
                S_ControlMessage set = Message(CONTROL_MSG_SET_POSITION);
                set.target = m_value;
                set.value = position;
                PostControlMessage(set);
            }
            Notify(CONTROL_NOTIFY_CHANGED, 0);
        }
        return true;
    }
    case CONTROL_MSG_SET_PAGE_STEP:
        if (message->value != 0)
            m_pageStep = static_cast<int>(message->value);
        return true;
    case CONTROL_MSG_GET_POSITION:
        message->value = static_cast<uint32_t>(m_position);
        return true;
    case CONTROL_MSG_SET_POSITION:
        if (message->value <= static_cast<uint32_t>(m_count)) {   // (the count too: the original's)
            m_position = static_cast<int>(message->value);
            Update();
        }
        return true;
    }
    return C_Control::HandleMessage(message);
}

void C_ScrollBar::SetInactive(bool inactive)
{
    if (!inactive && ((m_parent && m_parent->IsInactive()) || m_count == 0))
        return;
    m_inactive = inactive;
    // (The arrows checked: the fix, the original's without them crashes.)
    if (m_upButton)
        m_upButton->SetInactive(inactive);
    if (m_downButton)
        m_downButton->SetInactive(m_inactive);
    if (m_slider)
        m_slider->SetInactive(m_inactive);
    Update();
}

void C_ScrollBar::SendPosition()
{
    if (C_Control *target = FindControl(m_value, true)) {
        S_ControlMessage set = Message(CONTROL_MSG_SET_POSITION);
        set.value = static_cast<uint32_t>(m_position);
        target->HandleMessageCopy(set);
    }
}

void C_ScrollBar::StepToCursor()
{
    C_Cursor *cursor = C_Cursor::Acquire(nullptr);
    S_vector position;
    cursor->GetPosition(&position);
    cursor->Release();
    if (m_frame->GetType() != FRAME_VISUAL)
        return;
    const I3D_bbox &bound = static_cast<I3D_visual *>(m_frame)->GetWorldBound();
    const float height = bound.max.y - bound.min.y;
    const float width = bound.max.x - bound.min.x;
    float value;
    if (height <= width)
        value = (position.x - bound.min.x) / width * 100.0f;
    else
        value = 100.0f - (position.y - bound.min.y) / height * 100.0f;
    if (value < 0.0f)
        value = 0.0f;
    auto target = static_cast<uint32_t>(static_cast<int>(std::floor(value / m_step + 0.5f)));
    if (target > static_cast<uint32_t>(m_count - 1))
        target = static_cast<uint32_t>(m_count - 1);
    const auto current = static_cast<uint32_t>(m_position);
    if (current == target)
        return;
    if (target < current)
        m_position = static_cast<int>(current - 1);
    if (static_cast<uint32_t>(m_position) < target)
        ++m_position;
    Update();
    SendPosition();
    Notify(CONTROL_NOTIFY_CHANGED, 0);
}

void C_ScrollBar::Update()
{
    if (m_inactive)
        return;
    if (m_slider) {
        S_ControlMessage set = Message(CONTROL_MSG_SET_SLIDER);
        set.value2 = FloatValue(static_cast<float>(m_position) * m_step);
        m_slider->HandleMessageCopy(set);
        return;
    }
    if (m_upButton)
        m_upButton->SetInactive(m_position == 0);
    if (m_downButton)
        m_downButton->SetInactive(!(m_position < m_count - 1));
}

// ----- the spin control (type 0x10) -----

int C_SpinControl::FindChild(uint32_t id) const
{
    return FindChildByLastBits(m_children, id);
}

bool C_SpinControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc))
        return false;
    m_upButton = MakeArrow(this, m_scene, frame, root, "barrow up*", m_id | UP_ARROW, CONTROL_MSG_PREVIOUS_ITEM);
    if (m_upButton)
        m_children.push_back(m_upButton);
    m_downButton = MakeArrow(this, m_scene, frame, root, "barrow down*", m_id | DOWN_ARROW, CONTROL_MSG_NEXT_ITEM);
    if (m_downButton)
        m_children.push_back(m_downButton);
    if (!m_box)
        return false;
    AttachBox();
    SetInactive(true);
    return true;
}

int C_SpinControl::OnEnter(int id)
{
    m_box->SetHover(0);
    m_focus = FindChild(static_cast<uint32_t>(id));
    if (C_Control *focus = Focus())
        return focus->OnEnter(id);
    return CONTROL_HANDLED;
}

int C_SpinControl::OnLeave(int id)
{
    m_box->ClearHover();
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return 0;
        m_focus = -1;
    }
    return CONTROL_HANDLED;
}

int C_SpinControl::OnInput05(int id)
{
    const int focus = m_focus;
    if (focus == FindChild(static_cast<uint32_t>(id))) {
        if (IsChild(focus))
            return m_children[focus]->OnInput05(id);
    } else if (OnLeave(id) != 0) {
        return OnEnter(id);
    }
    return CONTROL_HANDLED;
}

int C_SpinControl::OnPress(int id)
{
    if (m_inactive)
        return CONTROL_HANDLED;
    if (C_Control *focus = Focus())
        return focus->OnPress(id);
    return 0;
}

void C_SpinControl::ShowSelected()
{
    // (Without items: the original's shows the item past the end; the fix, none.)
    if (static_cast<size_t>(m_selected) < m_items.size())
        m_box->SetText(m_items[m_selected], SPIN_STYLE, 0);
    Notify(CONTROL_NOTIFY_CHANGED, static_cast<uint32_t>(m_selected));
}

bool C_SpinControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_TextControl::HandleMessage(message);
    const int count = static_cast<int>(m_items.size());
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_PREVIOUS_ITEM:
        if (!m_inactive) {
            if (--m_selected < 0)
                m_selected = count - 1;
            ShowSelected();
        }
        return true;
    case CONTROL_MSG_NEXT_ITEM:
        if (!m_inactive) {
            if (static_cast<uint32_t>(++m_selected) >= static_cast<uint32_t>(count))
                m_selected = 0;
            ShowSelected();
        }
        return true;
    case CONTROL_MSG_CLEAR_ITEMS:
        m_items.clear();
        m_selected = -1;
        SetInactive(true);
        return true;
    case CONTROL_MSG_ADD_ITEM:
        if (message->value != 0)
            m_items.push_back(g_texts.Get(static_cast<int>(message->value)));
        else
            m_items.push_back(message->text);
        if (m_selected < 0) {
            m_selected = 0;
            m_box->SetText(m_items[0], SPIN_STYLE, 0);
        }
        if (m_inactive)
            SetInactive(false);
        return true;
    case CONTROL_MSG_REMOVE_ITEM: {
        const int index = static_cast<int>(message->value);
        if (index >= 0 && index <= count - 1)
            m_items.erase(m_items.begin() + index);
        if (m_selected > static_cast<int>(m_items.size()) - 1)
            m_selected = static_cast<int>(m_items.size()) - 1;
        // (The first selected too: the original's.)
        if (m_selected <= 0)
            SetInactive(true);
        return true;
    }
    case CONTROL_MSG_GET_SELECTED:
    case CONTROL_MSG_GET_SELECTED_2:
        message->value = static_cast<uint32_t>(m_selected);
        [[fallthrough]];
    case CONTROL_MSG_GET_ITEM: {
        const int index = static_cast<int>(message->value);
        if (index >= 0 && index <= count - 1)
            message->text = m_items[index];
        else
            message->value = static_cast<uint32_t>(-1);
        return true;
    }
    case CONTROL_MSG_SELECT_ITEM:
        if (message->value < static_cast<uint32_t>(count)) {
            m_selected = static_cast<int>(message->value);
            ShowSelected();
        }
        return true;
    }
    return C_TextControl::HandleMessage(message);
}

void C_SpinControl::SetInactive(bool inactive)
{
    m_inactive = inactive;
    m_box->SetDimmed(inactive);
    // (The arrows checked: the fix, the original's without them crashes.)
    if (m_upButton)
        m_upButton->SetInactive(m_inactive);
    if (m_downButton)
        m_downButton->SetInactive(m_inactive);
}

// ----- the check box (type 9) -----

bool C_CheckBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc) || frame->NumChildren() != 4)
        return false;
    m_yesFull = frame->FindChildFrame("yes_full*", FIND_FRAMES);
    m_yesEmpty = frame->FindChildFrame("yes_empty*", FIND_FRAMES);
    m_noFull = frame->FindChildFrame("no_full*", FIND_FRAMES);
    m_noEmpty = frame->FindChildFrame("no_empty*", FIND_FRAMES);
    if (!m_yesFull || !m_yesEmpty || !m_noFull || !m_noEmpty)
        return false;
    m_noEmpty->SetOn(true);
    SetFrameId(m_noEmpty, m_id | CHECK_NO);
    m_noEmpty->Update();
    m_noFull->SetPosInline(m_noEmpty->GetPos());   // (not when its place is locked)
    m_noFull->SetOn(false);
    SetFrameId(m_noFull, m_id | CHECK_NO);
    m_noFull->Update();
    m_yesEmpty->SetOn(false);
    SetFrameId(m_yesEmpty, m_id | CHECK_YES);
    m_yesEmpty->Update();
    m_yesFull->SetPosInline(m_yesEmpty->GetPos());
    m_yesFull->SetOn(true);
    SetFrameId(m_yesFull, m_id | CHECK_YES);
    m_yesFull->Update();
    if (!m_box)
        return false;
    AttachBox();
    return true;
}

int C_CheckBox::OnEnter(int)
{
    if (m_inactive)
        return 0;
    if (m_box)
        m_box->SetHover(0);
    Notify(CONTROL_NOTIFY_ENTER, 0);
    return CONTROL_HANDLED;
}

int C_CheckBox::OnLeave(int)
{
    if (m_box)
        m_box->ClearHover();
    Notify(CONTROL_NOTIFY_ENTER, 0);   // (not LEAVE: the original's)
    return CONTROL_HANDLED;
}

int C_CheckBox::OnClick(int)
{
    Toggle();
    return CONTROL_HANDLED;
}

int C_CheckBox::OnKeyClick(int id)
{
    if ((static_cast<uint32_t>(id) & CHILD_ID_MASK) == CHECK_NO && m_checked)
        Toggle();
    if ((static_cast<uint32_t>(id) & CHILD_ID_MASK) == CHECK_YES && !m_checked)
        Toggle();
    return CONTROL_HANDLED;
}

bool C_CheckBox::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_TextControl::HandleMessage(message);
    // (The value's low byte against the state: another than 0 or 1 toggles, then sets it back; the
    // original's.)
    const auto value = static_cast<uint8_t>(message->value);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_GET_CHECKED:
        message->value = m_checked;
        return true;
    case CONTROL_MSG_SET_CHECKED:
        if (value != static_cast<uint8_t>(m_checked))
            Toggle();
        [[fallthrough]];
    case CONTROL_MSG_SET_CHECKED_QUIET:
        if (value != static_cast<uint8_t>(m_checked)) {
            if (m_checked)
                Uncheck();
            else
                Check();
        }
        return true;
    }
    return C_TextControl::HandleMessage(message);
}

void C_CheckBox::Toggle()
{
    if (!m_checked)
        Check();
    else
        Uncheck();
    Notify(CONTROL_NOTIFY_CHANGED, m_checked);
}

void C_CheckBox::Check()
{
    if (m_checked)
        return;
    m_noEmpty->SetOn(true);
    m_noFull->SetOn(false);
    m_yesEmpty->SetOn(false);
    m_yesFull->SetOn(true);
    m_noEmpty->Update();
    m_noFull->Update();
    m_yesEmpty->Update();
    m_yesFull->Update();
    m_checked = true;
}

void C_CheckBox::Uncheck()
{
    if (!m_checked)
        return;
    m_noEmpty->SetOn(false);
    m_noFull->SetOn(true);
    m_yesEmpty->SetOn(true);
    m_yesFull->SetOn(false);
    m_noEmpty->Update();
    m_noFull->Update();
    m_yesEmpty->Update();
    m_yesFull->Update();
    m_checked = false;
}

// ----- the toggle buttons (type 0xa; the "all" one) -----

bool C_ToggleButton::CreateFrames(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc, const char *activeDown,
                                  const char *normalDown, const char *activeUp, const char *normalUp)
{
    C_Control::Create(frame, root, desc);
    if (frame->NumChildren() != 4)
        return false;
    m_activeDown = frame->FindChildFrame(activeDown, FIND_FRAMES);
    m_normalDown = frame->FindChildFrame(normalDown, FIND_FRAMES);
    m_activeUp = frame->FindChildFrame(activeUp, FIND_FRAMES);
    m_normalUp = frame->FindChildFrame(normalUp, FIND_FRAMES);
    if (!m_activeDown || !m_normalDown || !m_activeUp || !m_normalUp)
        return false;
    const S_vector place = m_normalDown->GetPos();
    m_normalDown->SetOn(true);
    SetFrameId(m_normalDown, m_id | TOGGLE_DOWN);
    m_normalDown->Update();
    m_activeDown->SetPosInline(place);   // (not when its place is locked)
    m_activeDown->SetOn(false);
    SetFrameId(m_activeDown, m_id | TOGGLE_DOWN);
    m_activeDown->Update();
    for (I3D_frame *up : {m_activeUp, m_normalUp}) {
        up->SetPosInline(place);
        up->SetOn(false);
        SetFrameId(up, m_id | TOGGLE_UP);
        up->Update();
    }
    return true;
}

bool C_ToggleButton::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    return CreateFrames(frame, root, desc, "activeddown*", "normaldown*", "activedup*", "normalup*");
}

bool C_AllToggleButton::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    return CreateFrames(frame, root, desc, "allactived", "allnormal", "xallactived", "xallnormal");
}

int C_ToggleButton::OnEnter(int)
{
    I3D_frame *active = m_down ? m_activeDown : m_activeUp;
    I3D_frame *normal = m_down ? m_normalDown : m_normalUp;
    active->SetOn(true);
    normal->SetOn(false);
    active->Update();
    normal->Update();
    m_over = true;
    return CONTROL_HANDLED;
}

int C_ToggleButton::OnLeave(int)
{
    I3D_frame *active = m_down ? m_activeDown : m_activeUp;
    I3D_frame *normal = m_down ? m_normalDown : m_normalUp;
    normal->SetOn(true);
    active->SetOn(false);
    active->Update();
    normal->Update();
    m_over = false;
    return CONTROL_HANDLED;
}

int C_ToggleButton::OnClick(int)
{
    Toggle();
    return CONTROL_HANDLED;
}

bool C_ToggleButton::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_GET_CHECKED:
            message->value = m_down;
            return true;
        case CONTROL_MSG_SET_CHECKED:
            if ((message->value != 0) != m_down)
                Toggle();
            return true;
        }
    }
    return C_Control::HandleMessage(message);
}

void C_ToggleButton::Toggle()
{
    // The frames the cursor's over shows.
    I3D_frame *down = m_over ? m_activeDown : m_normalDown;
    I3D_frame *up = m_over ? m_activeUp : m_normalUp;
    m_down = !m_down;
    down->SetOn(m_down);
    down->Update();
    up->SetOn(!m_down);
    up->Update();
    Notify(CONTROL_NOTIFY_CHANGED, 0);
}

// ----- the highlighted check box's halves (vtable 0x00814078) -----

bool C_CheckBoxHalf::Setup(I3D_frame *empty, I3D_frame *full, I3D_frame *activeFull, I3D_frame *activeEmpty)
{
    m_fullFrame = full;
    m_frame = empty;
    m_emptyFrame = empty;
    m_activeFullFrame = activeFull;
    m_activeEmptyFrame = activeEmpty;
    empty->SetOn(true);
    SetFrameId(empty, m_id);
    empty->Update();
    for (I3D_frame *other : {full, activeFull}) {
        other->SetOn(false);
        SetFrameId(other, m_id);
        other->SetPosInline(empty->GetPos());   // (not when its place is locked)
        other->Update();
    }
    return true;
}

void C_CheckBoxHalf::ShowActiveEmpty()
{
    m_activeEmptyFrame->SetOn(true);
    SetFrameId(m_activeEmptyFrame, m_id);
    m_activeEmptyFrame->SetPosInline(m_emptyFrame->GetPos());
}

int C_CheckBoxHalf::OnEnter(int)
{
    if (m_inactive || m_over)
        return 0;
    if (m_empty) {
        m_emptyFrame->SetOn(false);
        m_emptyFrame->Update();
        ShowActiveEmpty();
        m_activeEmptyFrame->Update();
    } else {
        m_fullFrame->SetOn(false);
        m_fullFrame->Update();
        m_activeFullFrame->SetOn(true);
        m_activeFullFrame->Update();
    }
    m_over = true;
    return CONTROL_HANDLED;
}

int C_CheckBoxHalf::OnLeave(int)
{
    if (!m_over)
        return 0;
    I3D_frame *normal = m_empty ? m_emptyFrame : m_fullFrame;
    I3D_frame *active = m_empty ? m_activeEmptyFrame : m_activeFullFrame;
    normal->SetOn(true);
    normal->Update();
    active->SetOn(false);
    active->Update();
    m_over = false;
    return CONTROL_HANDLED;
}

int C_CheckBoxHalf::OnKeyClick(int)
{
    if (!m_over)
        return 0;
    if (m_empty) {
        m_activeEmptyFrame->SetOn(false);
        m_activeEmptyFrame->Update();
        m_activeFullFrame->SetOn(true);
        m_activeFullFrame->Update();
    } else {
        m_activeFullFrame->SetOn(false);
        m_activeFullFrame->Update();
        ShowActiveEmpty();
        m_activeEmptyFrame->Update();
    }
    return CONTROL_HANDLED;
}

void C_CheckBoxHalf::SetInactive(bool inactive)
{
    if (inactive == m_inactive)
        return;
    m_inactive = inactive;
    static_cast<I3D_object_visual *>(m_emptyFrame)->SetAlpha(inactive ? INACTIVE_ALPHA : 1.0f);
    static_cast<I3D_object_visual *>(m_fullFrame)->SetAlpha(m_inactive ? INACTIVE_ALPHA : 1.0f);
}

void C_CheckBoxHalf::SetFull(bool full)
{
    if (m_empty == !full)
        return;
    m_empty = !full;
    if (m_over) {
        if (m_empty) {
            ShowActiveEmpty();
            m_activeFullFrame->SetOn(false);
        } else {
            m_activeFullFrame->SetOn(true);
            m_activeEmptyFrame->SetOn(false);
        }
        m_activeFullFrame->Update();
        m_activeEmptyFrame->Update();
        return;
    }
    m_emptyFrame->SetOn(m_empty);
    m_fullFrame->SetOn(!m_empty);
    m_emptyFrame->Update();
    m_fullFrame->Update();
}

void C_CheckBoxHalf::LinkFrames(I3D_frame *parent)
{
    for (I3D_frame *frame : {m_emptyFrame, m_fullFrame, m_activeFullFrame, m_activeEmptyFrame})
        frame->LinkTo(parent, I3DLINK_KEEP_WORLD);
}

// ----- the highlighted check box (type 0x24) -----

bool C_HighlightCheckBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextControl::Create(frame, root, desc) || !m_box || frame->NumChildren() != 7)
        return false;
    AttachBox();
    I3D_frame *yesEmpty = frame->FindChildFrame("yes_empty*", FIND_FRAMES);
    I3D_frame *yesFull = frame->FindChildFrame("yes_full*", FIND_FRAMES);
    I3D_frame *yesActiveFull = frame->FindChildFrame("yesactived_full*", FIND_FRAMES);
    I3D_frame *noEmpty = frame->FindChildFrame("no_empty*", FIND_FRAMES);
    I3D_frame *noFull = frame->FindChildFrame("no_full*", FIND_FRAMES);
    I3D_frame *noActiveFull = frame->FindChildFrame("noactived_full*", FIND_FRAMES);
    I3D_frame *activeEmpty = frame->FindChildFrame("noactived_empty*", FIND_FRAMES);   // (both halves')
    if (!yesEmpty || !yesFull || !yesActiveFull || !noEmpty || !noFull || !noActiveFull || !activeEmpty)
        return false;
    m_yes = new C_CheckBoxHalf(m_scene, m_id | HALF_YES, this);
    m_yes->Setup(yesEmpty, yesFull, yesActiveFull, activeEmpty);
    m_children.push_back(m_yes);
    m_no = new C_CheckBoxHalf(m_scene, m_id | HALF_NO, this);
    m_no->Setup(noEmpty, noFull, noActiveFull, activeEmpty);
    m_children.push_back(m_no);
    m_yes->SetFull(true);
    m_no->SetFull(false);
    return true;
}

C_CheckBoxHalf *C_HighlightCheckBox::Half(uint32_t id) const
{
    switch (id & CHILD_ID_MASK) {
    case HALF_YES:
        return m_yes;
    case HALF_NO:
        return m_no;
    }
    return nullptr;
}

int C_HighlightCheckBox::OnEnter(int id)
{
    if (m_inactive)
        return 0;
    if (m_box)
        m_box->SetHover(0);
    if (C_CheckBoxHalf *half = Half(static_cast<uint32_t>(id)))
        half->OnEnter(id);
    m_overId = static_cast<uint32_t>(id);
    Notify(CONTROL_NOTIFY_ENTER, 0);
    return CONTROL_HANDLED;
}

int C_HighlightCheckBox::OnLeave(int id)
{
    if (m_box)
        m_box->ClearHover();
    if (C_CheckBoxHalf *half = Half(m_overId))
        half->OnLeave(id);
    m_overId = 0;
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return CONTROL_HANDLED;
}

int C_HighlightCheckBox::OnClick(int)
{
    Toggle();
    return CONTROL_HANDLED;
}

int C_HighlightCheckBox::OnInput05(int id)
{
    if (m_overId != 0 && m_overId != static_cast<uint32_t>(id)) {
        if (C_CheckBoxHalf *half = Half(m_overId))
            half->OnLeave(id);
        if (C_CheckBoxHalf *half = Half(static_cast<uint32_t>(id)))
            half->OnEnter(id);
        m_overId = static_cast<uint32_t>(id);
    }
    return CONTROL_HANDLED;
}

int C_HighlightCheckBox::OnKeyClick(int)
{
    Toggle();
    return CONTROL_HANDLED;
}

bool C_HighlightCheckBox::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_TextControl::HandleMessage(message);
    // (The value's low byte against the state: C_CheckBox's.)
    const auto value = static_cast<uint8_t>(message->value);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_GET_CHECKED:
        message->value = m_checked;
        return true;
    case CONTROL_MSG_SET_CHECKED:
        if (value != static_cast<uint8_t>(m_checked))
            Toggle();
        [[fallthrough]];
    case CONTROL_MSG_SET_CHECKED_QUIET:
        if (value != static_cast<uint8_t>(m_checked)) {
            if (m_checked)
                Uncheck();
            else
                Check();
        }
        return true;
    case CONTROL_MSG_FACE_FRONT:
        // The halves' frames unlinked while the frame turns (to the back, not the front).
        m_yes->LinkFrames(nullptr);
        m_no->LinkFrames(nullptr);
        m_frame->SetDir(FACE_BACK, 0.0f);
        m_yes->LinkFrames(m_frame);
        m_no->LinkFrames(m_frame);
        return true;
    }
    return C_TextControl::HandleMessage(message);
}

void C_HighlightCheckBox::SetInactive(bool inactive)
{
    if (inactive == m_inactive)
        return;
    C_TextControl::SetInactive(inactive);
    if (m_yes)
        m_yes->SetInactive(inactive);
    if (m_no)
        m_no->SetInactive(inactive);
}

void C_HighlightCheckBox::Toggle()
{
    if (!m_checked)
        Check();
    else
        Uncheck();
    Notify(CONTROL_NOTIFY_CHANGED, m_checked);
}

void C_HighlightCheckBox::Check()
{
    if (m_checked)
        return;
    m_yes->SetFull(true);
    m_no->SetFull(false);
    m_checked = true;
}

void C_HighlightCheckBox::Uncheck()
{
    if (!m_checked)
        return;
    m_yes->SetFull(false);
    m_no->SetFull(true);
    m_checked = false;
}

// ----- the side indicator (type 0x1e) -----

void C_SideIndicator::Tick(int time)
{
    m_timer -= time;
    if (m_timer >= 1)
        return;
    m_timer = SIDE_PERIOD;
    // (The index is the zone's address: the menus'.)
    const int side = reinterpret_cast<const C_MPZone *>(static_cast<uintptr_t>(static_cast<uint32_t>(m_index)))->GetTeam();
    if (m_side != side)
        SetSide(side);
}

bool C_SideIndicator::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc) || frame->NumChildren() < 3)
        return false;
    m_alliedFrame = frame->FindChildFrame("allied*", FIND_FRAMES);
    m_axisFrame = frame->FindChildFrame("axis*", FIND_FRAMES);
    m_alliedFrame->SetOn(false);
    SetFrameId(m_alliedFrame, m_id);
    m_axisFrame->SetOn(false);
    SetFrameId(m_axisFrame, m_id);
    m_side = SIDE_NONE;
    S_ControlMessage ticker = Message(CONTROL_MSG_ADD_TICKER);
    ticker.value2 = reinterpret_cast<uintptr_t>(this);
    PostControlMessage(ticker);
    return true;
}

int C_SideIndicator::OnKeyClick(int)
{
    Notify(CONTROL_NOTIFY_KEY_CLICK, 0);
    return CONTROL_HANDLED;
}

bool C_SideIndicator::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_SET_SIDE:
            SetSide(static_cast<int>(message->value));
            return true;
        case CONTROL_MSG_GET_SIDE:
            message->value = static_cast<uint32_t>(m_side);
            return true;
        }
    }
    return C_Control::HandleMessage(message);
}

void C_SideIndicator::SetSide(int side)
{
    if (m_side == side)
        return;
    m_side = side;
    m_alliedFrame->SetOn(side == SIDE_ALLIED);
    m_axisFrame->SetOn(side == SIDE_AXIS);
    m_alliedFrame->Update();
    m_axisFrame->Update();
    Notify(CONTROL_NOTIFY_CHANGED, 0);
}

// ----- the mark (type 0x1f) -----

C_MarkBox::~C_MarkBox()
{
    delete m_title;
}

void C_MarkBox::Refresh()
{
    if (m_title)
        m_title->Refresh();
}

bool C_MarkBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return false;
    if (I3D_frame *text = frame->FindChildFrame("text_*", FIND_FRAMES)) {
        m_title = new C_TitleControl(m_scene, m_id | 1, this);
        m_title->Create(text, root, nullptr);
        S_ControlMessage set = Message(CONTROL_MSG_SET_TEXT);
        set.value = MARK_STYLE;
        set.text = "00";
        m_title->HandleMessageCopy(set);
    }
    m_noActive = frame->FindChildFrame("noactived*", FIND_FRAMES);
    m_noNormal = frame->FindChildFrame("nonormal*", FIND_FRAMES);
    m_yesActive = frame->FindChildFrame("yesactived*", FIND_FRAMES);
    m_yesNormal = frame->FindChildFrame("yesnormal*", FIND_FRAMES);
    m_emptyActive = frame->FindChildFrame("emptyactived*", FIND_FRAMES);
    m_emptyNormal = frame->FindChildFrame("emptynormal*", FIND_FRAMES);
    if (!m_noActive || !m_noNormal || !m_yesActive || !m_yesNormal || !m_emptyActive || !m_emptyNormal)
        return false;
    m_emptyNormal->SetOn(true);
    SetFrameId(m_emptyNormal, m_id | 1);
    m_emptyNormal->Update();
    for (I3D_frame *other : {m_emptyActive, m_yesNormal, m_yesActive, m_noNormal, m_noActive})
        PlaceFrame(other, m_emptyNormal);
    return true;
}

void C_MarkBox::PlaceFrame(I3D_frame *frame, I3D_frame *place)
{
    frame->SetPosInline(place->GetPos());   // (not when its place is locked)
    frame->SetOn(false);
    SetFrameId(frame, m_id);
    frame->Update();
}

int C_MarkBox::OnClick(int)
{
    Notify(CONTROL_NOTIFY_0E, m_value);
    return CONTROL_HANDLED;
}

int C_MarkBox::OnKeyClick(int)
{
    Notify(CONTROL_NOTIFY_0E, m_value);
    return CONTROL_HANDLED;
}

bool C_MarkBox::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_GET_CHECKED:
            message->value = m_value;
            return true;
        case CONTROL_MSG_SET_CHECKED:
            if (static_cast<uint8_t>(message->value) != 0)
                Mark();
            else
                Unmark();
            return true;
        case CONTROL_MSG_SET_NUMBER:
            if (m_title) {
                S_ControlMessage set = Message(CONTROL_MSG_SET_TEXT);
                set.value = MARK_STYLE;
                set.text = C_fstr("%02d", static_cast<int>(message->value));
                m_title->HandleMessageCopy(set);
            }
            return true;
        }
    }
    return C_Control::HandleMessage(message);
}

void C_MarkBox::Mark()
{
    m_value = 1;
    m_yesNormal->SetOn(true);
    m_emptyNormal->SetOn(false);
}

void C_MarkBox::Unmark()
{
    m_value = 0;
    m_yesNormal->SetOn(false);
    m_emptyNormal->SetOn(true);
}
