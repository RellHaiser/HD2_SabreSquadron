#include "game/controls.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#include "common/msvc_sort.h"
#include "game/cursor.h"
#include "game/item_defs.h"
#include "game/key_bindings.h"
#include "game/mp_game_setup.h"
#include "game/mp_zone.h"
#include "game/pending.h"
#include "game/profile.h"
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
// The edit controls' keys (Backspace, Tab, Enter, Escape); the full-width digits (as ASCII).
constexpr int KEY_BACKSPACE = 8, KEY_TAB = 9, KEY_RETURN = 0xD, KEY_ESCAPE = 0x1B;
constexpr int FULLWIDTH_ZERO = 0xFF10, FULLWIDTH_NINE = 0xFF19, FULLWIDTH_TO_ASCII = 0xFEE0;
// The edit box's text style for its kept number.
constexpr uint32_t EDIT_NUMBER_STYLE = 0xD;

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
#ifdef HD2_DEDICATED   // the server's copy only returns true
    (void)message;
    return true;
#endif
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
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
        if (GetIGraph()->LoadTexture(message->text.c_str(), nullptr, TEXTURE_MODE, &image) < 0) {
            m_faceGroup->material->SetTexture(m_texture);
        } else {
            m_faceGroup->material->SetTexture(reinterpret_cast<I3D_texture *>(image));
            image->Release();
        }
#else
        m_faceGroup->material->SetTexture(m_texture);
#endif
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
#ifdef HD2_DEDICATED   // no mouse in LS3DRF.dll: the server's copy returns 0
    (void)id;
    return 0;
#endif
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

// ----- the edit control (vtable 0x00813ee0) -----

namespace {

// _stricmp's: the ASCII letters without their case.
bool EqualNoCase(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        const int x = std::tolower(static_cast<unsigned char>(*a)), y = std::tolower(static_cast<unsigned char>(*b));
        if (x != y)
            return false;
        if (x == 0)
            return true;
    }
}

// A message posted for the screen: its code, the control (value2).
void PostForScreen(uint32_t id, C_Control *control)
{
    S_ControlMessage message = Message(id);
    message.value2 = reinterpret_cast<uintptr_t>(control);
    PostControlMessage(message);
}

} // namespace

void C_EditControl::MakeBuffer()
{
    // (Emptied: the fix, the original's new buffer is not.)
    m_buffer.assign(static_cast<size_t>(m_maxLength) + 1, '\0');
}

void C_EditControl::ShowBuffer()
{
    // (Its text as is: the original's makes it with C_fstr, a '%' in it read stray arguments.)
    m_box->SetSelectedText(C_str(m_buffer.data()));
}

void C_EditControl::Refresh()
{
    if (m_box && m_shown)
        m_box->Render();
}

void C_EditControl::Tick(int time)
{
    if (m_editing && m_box)
        m_box->TickCaret(time);
}

bool C_EditControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    MakeBuffer();
    return C_TextControl::Create(frame, root, desc);
}

bool C_EditControl::Backspace()
{
    char *text = m_buffer.data();
    if (*text == '\0')
        return true;
    const int length = static_cast<int>(std::strlen(text));
    if (g_gameProfile.UsesUnicodeFont()) {
        // Back over the continuation bytes to the sequence's lead.
        int start = length - 1;
        while (start >= 0 && static_cast<uint8_t>(text[start]) >= 0x80 && static_cast<uint8_t>(text[start]) < 0xC0)
            --start;
        // (Back to the buffer's start: the fix, broken; the original's reads the byte before it.)
        if (start < 0)
            return false;
        const uint8_t lead = static_cast<uint8_t>(text[start]);
        bool valid;
        switch (length - start) {
        case 1:
            valid = lead < 0x80;
            break;
        case 2:
            valid = lead >= 0xC0 && lead < 0xE0;
            break;
        case 3:
            valid = lead >= 0xE0 && lead < 0xF0;
            break;
        case 4:
            valid = lead >= 0xF0 && lead <= 0xF7;
            break;
        default:
            valid = false;
        }
        if (!valid)
            return false;
        text[start] = '\0';
    } else {
        text[length - 1] = '\0';
    }
    ShowBuffer();
    return true;
}

bool C_EditControl::OnChar(int key)
{
    if (!m_editing || key == 0)
        return false;
    switch (key) {
    case KEY_BACKSPACE:
        Backspace();
        return true;
    case KEY_TAB:
        return false;
    case KEY_RETURN:
        Notify(CONTROL_NOTIFY_RETURN, 0);
        OnInput02(0);
        return true;
    case KEY_ESCAPE:
        Notify(CONTROL_NOTIFY_ESCAPE, 0);
        if (!m_editing)
            return false;
        m_box->SetSelectedText(m_savedText);
        OnInput02(0);
        return true;
    }
    auto code = static_cast<uint32_t>(key);
    if (code >= FULLWIDTH_ZERO && code <= FULLWIDTH_NINE)
        code -= FULLWIDTH_TO_ASCII;
    const size_t length = std::strlen(m_buffer.data());
    if (code > 0x7F && g_gameProfile.UsesUnicodeFont()) {
        char bytes[8];   // (the original's 4 for up to 6)
        const int count = EncodeUtf8(bytes, &code, 6);
        if (count > 0 && length + static_cast<size_t>(count) < static_cast<size_t>(m_maxLength)) {
            std::memcpy(m_buffer.data() + length, bytes, static_cast<size_t>(count));
            m_buffer[length + static_cast<size_t>(count)] = '\0';
            ShowBuffer();
        }
        return true;
    }
    const char c = static_cast<char>(code);
    if (m_forbidden && std::strchr(m_forbidden, c))
        return true;
    // (The character as unsigned: the original's isdigit takes it signed.)
    if (m_digitsOnly && !std::isdigit(static_cast<unsigned char>(c)) && !(length == 0 && c == '-'))
        return true;
    if (c == '\0')
        return false;
    if (length < static_cast<size_t>(m_maxLength)) {
        m_buffer[length] = c;
        m_buffer[length + 1] = '\0';
        ShowBuffer();
    }
    return true;
}

int C_EditControl::OnLeave(int)
{
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return CONTROL_NOT_HANDLED;
}

int C_EditControl::OnClick(int)
{
    if (m_editing)
        return 0;
    PostForScreen(CONTROL_MSG_START_EDITING, this);
    return CONTROL_HANDLED;
}

int C_EditControl::OnInput02(int)
{
    if (!m_editing)
        return 0;
    PostForScreen(CONTROL_MSG_STOP_EDITING, this);
    Notify(CONTROL_NOTIFY_02, 0);
    return CONTROL_HANDLED;
}

int C_EditControl::OnInput0B(int id)
{
    if (!m_editing)
        return 0;
    if (m_box->EditRun(-1))
        Notify(CONTROL_NOTIFY_0B, static_cast<uint32_t>(id));
    m_editing = false;
    return CONTROL_HANDLED;
}

int C_EditControl::BeginEdit(int)
{
    if (m_editing)
        return CONTROL_NOT_HANDLED;
    m_editing = true;
    if (m_run < 0 || m_run > m_box->NumRuns() - 1)
        m_box->SetText(C_str(), 0, 0);
    if (!m_box->EditRun(m_run))
        return 0;
    m_savedText = m_box->GetSelectedText();
    std::fill(m_buffer.begin(), m_buffer.end(), '\0');
    std::strncpy(m_buffer.data(), m_savedText.c_str(), static_cast<size_t>(m_maxLength));
    return CONTROL_HANDLED;
}

bool C_EditControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_TextControl::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_FORBIDDEN:
        m_forbidden = reinterpret_cast<const char *>(message->value2);
        return true;
    case CONTROL_MSG_SET_MAX_LENGTH:
        if (message->value != 0 && static_cast<int>(message->value) != m_maxLength) {
            m_maxLength = static_cast<int>(message->value);
            MakeBuffer();
        }
        return true;
    case CONTROL_MSG_SET_DIGITS_ONLY:
        m_digitsOnly = message->value != 0;
        return true;
    case CONTROL_MSG_IGNORED_74:
        return true;
    case CONTROL_MSG_EDIT:
        OnClick(0);
        return true;
    case CONTROL_MSG_GET_TEXT:
        // (The box's run 0 edited for it, then not: the editing's too, the original's.)
        m_box->EditRun(0);
        message->text = m_box->GetSelectedText();
        m_box->EditRun(-1);
        return true;
    case CONTROL_MSG_END_EDIT:
        if (IsEditing())
            OnInput02(0);
        return true;
    }
    return C_TextControl::HandleMessage(message);
}

// ----- the edit box (type 0x1b) -----

bool C_EditBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_EditControl::Create(frame, root, desc))
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
    if (m_box) {
        m_run = 0;
        m_box->AttachTo(m_scene, m_normalFrame);
    }
    return true;
}

int C_EditBox::OnEnter(int)
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

int C_EditBox::OnLeave(int id)
{
    const int result = C_EditControl::OnLeave(id);
    if (result != 0) {
        m_normalFrame->SetOn(true);
        m_activeFrame->SetOn(false);
        m_normalFrame->Update();
        m_activeFrame->Update();
        Notify(CONTROL_NOTIFY_LEAVE, 0);
    }
    return result;
}

int C_EditBox::OnClick(int id)
{
    return m_inactive ? 0 : C_EditControl::OnClick(id);
}

int C_EditBox::OnInput02(int id)
{
    if (C_EditControl::OnInput02(id) != CONTROL_HANDLED)
        return 0;
    m_box->ClearHover();
    m_box->SelectHovered();
    if (m_hasRange)
        KeepInRange();
    return CONTROL_HANDLED;
}

int C_EditBox::OnKeyClick(int id)
{
    if (m_inactive || static_cast<uint32_t>(id) != m_id)
        return 0;
    return IsEditing() ? OnInput02(id) : OnClick(id);
}

int C_EditBox::OnInput0B(int id)
{
    if (C_EditControl::OnInput0B(id) != CONTROL_HANDLED)
        return 0;
    if (m_hasRange)
        KeepInRange();
    return CONTROL_HANDLED;
}

void C_EditBox::KeepInRange()
{
    S_ControlMessage message = Message(CONTROL_MSG_GET_TEXT);
    C_EditControl::HandleMessage(&message);
    int number = 0;
    std::sscanf(message.text.c_str(), "%d", &number);
    if (number >= m_min && number <= m_max)
        return;
    message.id = CONTROL_MSG_SET_TEXT;
    message.value = EDIT_NUMBER_STYLE;
    message.text = C_fstr("%d", number < m_min ? m_min : m_max);
    C_EditControl::HandleMessage(&message);
}

bool C_EditBox::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_EditControl::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_RANGE:
        m_min = static_cast<int>(message->value);
        m_max = static_cast<int>(message->value2);
        m_hasRange = m_min != m_max;
        return true;
    case CONTROL_MSG_SET_NUMBER:
        // (The message turned into SET_TEXT: its style the value; digits only, value2's number.)
        message->id = CONTROL_MSG_SET_TEXT;
        if (m_digitsOnly)
            message->text = C_fstr("%d", static_cast<int>(message->value2));
        return C_EditControl::HandleMessage(message);
    case CONTROL_MSG_GET_NUMBER:
        if (m_digitsOnly) {
            message->id = CONTROL_MSG_GET_TEXT;
            C_EditControl::HandleMessage(message);
            int number = 0;
            std::sscanf(message->text.c_str(), "%d", &number);
            message->id = CONTROL_MSG_GET_NUMBER;
            message->value2 = static_cast<uint32_t>(number);
        }
        return true;
    case CONTROL_MSG_SET_MARGIN:
        if (m_box)
            m_box->SetUnknown90(static_cast<uint8_t>(message->value));
        [[fallthrough]];
    case CONTROL_MSG_REFRESH:
        if (m_box)
            m_box->AttachTo(m_scene, m_normalFrame);
        return true;
    }
    return C_EditControl::HandleMessage(message);
}

// ----- the selection list (type 0x1a) -----

C_SelectList::~C_SelectList()
{
    if (m_cursor)
        m_cursor->Release();
}

bool C_SelectList::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_EditControl::Create(frame, root, desc))
        return false;
    m_cursor = C_Cursor::Acquire(m_scene);
    m_frame->SetDir(FACE_BACK, 0.0f);
    SetFrameId(m_frame, m_id);
    m_frame->Update();
    m_selectFrame = frame->FindChildFrame("select*", FIND_FRAMES);
    if (m_selectFrame) {
        m_selectFrame->SetDir(FACE_FRONT, 0.0f);
        SetFrameId(m_selectFrame, m_id);
        m_selectFrame->SetOn(false);
        m_selectFrame->LinkTo(root, I3DLINK_KEEP_WORLD);
        m_selectFrame->Update();
    }
    if (!m_box)
        return false;
    m_box->AttachTo(m_scene, m_frame);
    return true;
}

int C_SelectList::OnLeave(int id)
{
    m_box->ClearHover();
    return C_EditControl::OnLeave(id);
}

void C_SelectList::PostScroll(bool always)
{
    S_ControlMessage count = Message(CONTROL_MSG_SET_COUNT);
    count.target = m_value;
    count.value = static_cast<uint32_t>(static_cast<int>(m_box->GetScrollRange()));
    PostControlMessage(count);
    if (!always && m_box->GetScrollRange() == 0)
        return;
    S_ControlMessage position = Message(CONTROL_MSG_SET_POSITION);
    position.target = m_value;
    position.value = static_cast<uint32_t>(m_box->GetScroll());
    PostControlMessage(position);
}

void C_SelectList::SelectHoveredLine()
{
    m_selected = m_box->SelectHovered();
    PostScroll(true);
    UpdateSelectBar();
}

int C_SelectList::OnClick(int)
{
    if (m_editing)
        return CONTROL_HANDLED;
    SelectHoveredLine();
    Notify(CONTROL_NOTIFY_CLICK, 0);
    return CONTROL_HANDLED;
}

int C_SelectList::OnPress(int)
{
    if (m_editing)
        return CONTROL_HANDLED;
    SelectHoveredLine();
    Notify(CONTROL_NOTIFY_PRESS, 0);
    return CONTROL_HANDLED;
}

int C_SelectList::OnInput05(int)
{
    if (m_takesInput && !m_editing) {
        Notify(CONTROL_NOTIFY_05, 0);
        float x, y;
        m_cursor->GetScreenPosition(&x, &y, nullptr);
        m_box->HoverAt(static_cast<int>(x), static_cast<int>(y));
    }
    return CONTROL_HANDLED;
}

void C_SelectList::UpdateSelectBar()
{
    if (!m_selectFrame)
        return;
    const S_vector4 centre = m_box->GetLineCentre(m_selected);
    if (centre.y == 0.0f) {
        m_selectFrame->SetOn(false);
        m_selectFrame->Update();
        return;
    }
    m_selectFrame->SetPos(S_vector(centre.x, centre.y, m_selectFrame->GetWorldPos().z));
    m_selectFrame->SetOn(true);
    m_selectFrame->Update();
}

bool C_SelectList::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_EditControl::HandleMessage(message);
    const int runs = m_box->NumRuns();
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_CLEAR: {
        m_box->Clear();
        m_box->ClearHover();
        m_box->SelectHovered();   // (the selected not changed: the original's)
        S_ControlMessage count = Message(CONTROL_MSG_SET_COUNT);
        count.target = m_value;
        PostControlMessage(count);
        S_ControlMessage position = Message(CONTROL_MSG_SET_POSITION);
        position.target = m_value;
        PostControlMessage(position);
        if (m_selectFrame) {
            m_selectFrame->SetOn(false);
            m_selectFrame->Update();
        }
        return true;
    }
    case CONTROL_MSG_GET_ITEM: {
        const int index = static_cast<int>(message->value);
        if (index >= 0 && index <= runs - 1) {
            m_selected = index;
            message->text = m_box->GetRunText(index);
        } else {
            message->value = static_cast<uint32_t>(-1);
        }
        return true;
    }
    case CONTROL_MSG_SET_VALUE:
        m_value = message->value;
        return true;
    case CONTROL_MSG_ADD_TEXT:
        m_box->AddText(message->text, static_cast<uint8_t>(message->value));
        HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
        return true;
    case CONTROL_MSG_ADD_TEXT_ID:
        m_box->AddTextId(static_cast<int>(message->value2), static_cast<uint8_t>(message->value));
        HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
        return true;
    case CONTROL_MSG_INSERT_FIRST:
        m_box->InsertText(message->text, static_cast<uint8_t>(message->value), 0);
        PostScroll(true);
        m_selected = 0;
        UpdateSelectBar();
        return true;
    case CONTROL_MSG_EDIT:
        // The selected line edited.
        m_run = m_selected;
        m_box->SelectRun(m_selected);
        UpdateSelectBar();
        C_EditControl::OnClick(0);
        return true;
    case CONTROL_MSG_REMOVE_SELECTED:
        m_box->RemoveRun(m_selected);
        m_selected = m_box->GetSelection();
        UpdateSelectBar();
        PostScroll(true);
        return true;
    case CONTROL_MSG_PREVIOUS_ITEM:
    case CONTROL_MSG_NEXT_ITEM:
        return true;
    case CONTROL_MSG_SET_POSITION:
        m_box->ScrollTo(static_cast<int>(message->value));
        UpdateSelectBar();
        return true;
    case CONTROL_MSG_REFRESH:
        if (m_value != 0)
            PostScroll(false);
        return true;
    case CONTROL_MSG_GET_TEXT:
        message->value = static_cast<uint32_t>(m_selected);
        if (m_selected >= 0 && m_selected <= runs - 1)
            message->text = m_box->GetRunText(m_selected);
        return true;
    case CONTROL_MSG_GET_RUNS:
        message->value = static_cast<uint32_t>(runs);
        return true;
    case CONTROL_MSG_SELECT_TEXT:
        message->value = static_cast<uint32_t>(-1);
        for (int i = 0; i < runs; ++i) {
            if (!EqualNoCase(m_box->GetRunText(i).c_str(), message->text.c_str()))
                continue;
            m_box->SelectRun(i);
            m_selected = i;
            UpdateSelectBar();
            message->value = static_cast<uint32_t>(i);
            if (m_value != 0)
                PostScroll(false);
            break;
        }
        return true;
    }
    return C_EditControl::HandleMessage(message);
}

// ----- the group's shared slots (part 9c-iv-b) -----

int C_ControlGroup::EnterChild(int id)
{
    if (id == GROUP_ENTER_UNKNOWN) {
        if (Unknown20(GROUP_ENTER_UNKNOWN))
            return CONTROL_NOT_HANDLED;
    } else if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return CONTROL_NOT_HANDLED;
    }
    const int index = FindChild(static_cast<uint32_t>(id));
    if (IsChild(index) && m_children[index]->OnEnter(id) != 0) {
        m_focus = index;
        return CONTROL_HANDLED;
    }
    return 0;
}

bool C_ControlGroup::FocusUnknown20(int value)
{
    C_Control *focus = Focus();
    return focus && focus->Unknown20(value);
}

// ----- the scrolled text (type 0xf) -----

namespace {

// The scrolled text's parts' ids' last bits, its bar's page step; its lines' style.
constexpr uint32_t SCROLLED_LIST = 8, SCROLLED_BAR = 4;
constexpr uint32_t SCROLLED_PAGE_STEP = 5;
constexpr uint32_t SCROLLED_LINE_STYLE = 0x13;
// The combo box's parts' ids' last bits; the header's text style.
constexpr uint32_t COMBO_BUTTON = 1, COMBO_HEADER = 2;
constexpr uint32_t COMBO_HEADER_STYLE = 0xC;
// The chat input's parts' ids' last bits, its prompt, its length, the lines kept.
constexpr uint32_t CHAT_INPUT = 4, CHAT_LABEL = 8, CHAT_TOGGLE = 0xC;
constexpr int CHAT_PROMPT_TEXT = 2002;
constexpr uint32_t CHAT_PROMPT_STYLE = 0xD;
constexpr uint32_t CHAT_MAX_LENGTH = 100;
constexpr uint32_t CHAT_LINES = 8;

// A message with a value (and a text).
S_ControlMessage ValueMessage(uint32_t id, uint32_t value, const C_str &text = C_str())
{
    S_ControlMessage message = Message(id);
    message.value = value;
    message.text = text;
    return message;
}

} // namespace

int C_ScrolledText::FindChild(uint32_t id) const
{
    const uint32_t bits = id & CHILD_ID_MASK;
    if (bits == SCROLLED_LIST)
        return 0;
    if (bits > 3 && bits < 8)
        return 1;
    return -1;
}

bool C_ScrolledText::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    m_frame->SetData(nullptr);
    if (I3D_frame *text = frame->FindChildFrame("text*", FIND_FRAMES)) {
        m_list = new C_SelectList(m_scene, m_id | SCROLLED_LIST, this);
        m_list->Create(text, root, nullptr);
        m_children.push_back(m_list);
    }
    if (I3D_frame *scroll = frame->FindChildFrame("scroll*", FIND_FRAMES)) {
        m_bar = new C_ScrollBar(m_scene, m_id | SCROLLED_BAR, this);
        m_bar->Create(scroll, root, nullptr);
        m_bar->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_PAGE_STEP, SCROLLED_PAGE_STEP));
        m_children.push_back(m_bar);
    }
    if (m_bar && m_list) {
        m_bar->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_VALUE, m_id | SCROLLED_LIST));
        m_list->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_VALUE, m_id | SCROLLED_BAR));
    }
    return true;
}

int C_ScrolledText::OnPress(int id)
{
    if (!IsChild(m_focus) || m_focus != FindChild(static_cast<uint32_t>(id)))
        return 0;
    const int result = m_children[m_focus]->OnPress(id);
    if (m_focus == 0)
        Notify(CONTROL_NOTIFY_PRESS, 0);
    return result;
}

int C_ScrolledText::OnWheel(int, int amount)
{
    if (!m_bar || amount == 0)
        return CONTROL_NOT_HANDLED;
    if (amount > 0) {
        for (unsigned steps = (static_cast<unsigned>(amount) - 1) / WHEEL_STEP + 1; steps; --steps)
            m_bar->HandleMessageCopy(Message(CONTROL_MSG_SCROLL_UP));
    } else {
        for (unsigned steps = (static_cast<unsigned>(-amount) - 1) / WHEEL_STEP + 1; steps; --steps)
            m_bar->HandleMessageCopy(Message(CONTROL_MSG_SCROLL_DOWN));
    }
    return CONTROL_HANDLED;
}

bool C_ScrolledText::HandleMessage(S_ControlMessage *message)
{
    const int index = FindChild(message->target);
    if (IsChild(index))
        return m_children[index]->HandleMessage(message);
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_IGNORE_INPUT:
        case CONTROL_MSG_ADD_CALLBACK:
        case CONTROL_MSG_SET_VALUE:
        case CONTROL_MSG_ADD_TEXT:
        case CONTROL_MSG_ADD_TEXT_ID:
        case CONTROL_MSG_SET_RUN_TEXT:
        case CONTROL_MSG_GET_ITEM:
        case CONTROL_MSG_INSERT_FIRST:
        case CONTROL_MSG_EDIT:
        case CONTROL_MSG_REMOVE_SELECTED:
        case CONTROL_MSG_CLEAR:
        case CONTROL_MSG_PREVIOUS_ITEM:
        case CONTROL_MSG_NEXT_ITEM:
        case CONTROL_MSG_SET_POSITION:
        case CONTROL_MSG_REFRESH:
        case CONTROL_MSG_GET_TEXT:
        case CONTROL_MSG_SET_MARGIN:
        case CONTROL_MSG_SELECT_TEXT:
            // (The list checked: the fix, the original's without it crashes.)
            if (m_list)
                m_list->HandleMessage(message);
            return true;
        }
    }
    C_Control::HandleMessage(message);
    return true;
}

void C_ScrolledText::SetStrings(const std::vector<C_str> &strings)
{
    if (!m_list)
        return;
    for (const C_str &line : strings)
        m_list->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, SCROLLED_LINE_STYLE, line));
    m_list->HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
}

void C_ScrolledText::SetInactive(bool inactive)
{
    m_inactive = inactive;
    if (m_bar)
        m_bar->SetInactive(inactive);
    if (m_list)
        m_list->SetInactive(inactive);
}

// ----- the combo box's header (vtable 0x00814320) -----

bool C_ComboEdit::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_EditControl::Create(frame, root, desc))
        return false;
    m_frame->SetDir(FACE_BACK, 0.0f);
    SetFrameId(m_frame, m_id);
    m_frame->Update();
    if (!m_box)
        return false;
    m_run = 0;
    m_box->AttachTo(m_scene, m_frame);
    return true;
}

int C_ComboEdit::OnEnter(int)
{
    if (m_inactive)
        return 0;
    Notify(CONTROL_NOTIFY_ENTER, 0);
    return CONTROL_HANDLED;
}

int C_ComboEdit::OnLeave(int)
{
    if (!m_box->ClearHover())
        return 0;
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return C_EditControl::OnLeave(0);
}

int C_ComboEdit::OnInput02(int)
{
    if (C_EditControl::OnInput02(0) != CONTROL_HANDLED)
        return 0;
    m_box->ClearHover();
    m_box->SelectHovered();
    return CONTROL_HANDLED;
}

int C_ComboEdit::OnKeyClick(int id)
{
    if (m_inactive || static_cast<uint32_t>(id) != m_id)
        return 0;
    return IsEditing() ? OnInput02(id) : OnClick(id);
}

bool C_ComboEdit::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_EditControl::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_EDIT:
        C_EditControl::OnClick(0);
        return true;
    case CONTROL_MSG_SET_MARGIN:
        if (m_box)
            m_box->SetUnknown90(static_cast<uint8_t>(message->value));
        [[fallthrough]];
    case CONTROL_MSG_REFRESH:
        if (m_box)
            m_box->AttachTo(m_scene, m_frame);
        return true;
    }
    return C_EditControl::HandleMessage(message);
}

// ----- the combo box (type 0x11) -----

int C_ComboBox::FindChild(uint32_t id) const
{
    const uint32_t bits = id & CHILD_ID_MASK;
    if (bits > 3)
        return 0;
    if (bits == COMBO_BUTTON)
        return 1;
    return bits == COMBO_HEADER ? 2 : -1;
}

void C_ComboBox::SendTo(C_Control *part, const S_ControlMessage &message)
{
    if (part)
        part->HandleMessageCopy(message);
}

bool C_ComboBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    m_frame->SetData(nullptr);
    if (I3D_frame *list = frame->FindChildFrame("listbox*", FIND_FRAMES | FIND_DUMMIES)) {
        m_list = new C_ScrolledText(m_scene, m_id, this);
        m_list->Create(list, root, nullptr);
        m_list->HandleMessageCopy(Message(CONTROL_MSG_HIDE));
        m_list->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_PRESS | CONTROL_MSG_PICK_SELECTED));
        m_children.push_back(m_list);
    }
    if (I3D_frame *button = frame->FindChildFrame("cbdown*", FIND_DUMMIES)) {
        m_button = new C_Button(m_scene, m_id | COMBO_BUTTON, this);
        m_button->Create(button, root, nullptr);
        m_button->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_PRESS | CONTROL_MSG_TOGGLE_LIST));
        m_children.push_back(m_button);
    }
    if (I3D_frame *header = frame->FindChildFrame("header*", FIND_FRAMES)) {
        m_header = new C_ComboEdit(m_scene, m_id | COMBO_HEADER, this);
        m_header->Create(header, root, nullptr);
        S_ControlMessage takeInput = Message(CONTROL_MSG_TAKE_INPUT);
        takeInput.target = m_id;
        m_header->HandleMessageCopy(takeInput);
        m_header->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_PRESS | CONTROL_MSG_TOGGLE_LIST));
        S_ControlMessage callback = ValueMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0B);
        callback.value2 = reinterpret_cast<uintptr_t>(this);
        callback.value3 = reinterpret_cast<uintptr_t>(&C_ComboBox::HeaderEdited);
        m_header->HandleMessageCopy(callback);
        m_children.push_back(m_header);
    }
    return true;
}

bool C_ComboBox::HeaderEdited(void *object, void *, C_Control *, uint32_t, uint32_t)
{
    auto *combo = static_cast<C_ComboBox *>(object);
    if (combo->m_picked >= 0) {
        S_ControlMessage get = Message(CONTROL_MSG_GET_TEXT);
        combo->m_header->HandleMessage(&get);
        combo->m_pickedText = get.text;
        S_ControlMessage set = ValueMessage(CONTROL_MSG_SET_RUN_TEXT, SCROLLED_LINE_STYLE, combo->m_pickedText);
        set.value2 = static_cast<uint32_t>(combo->m_picked);
        SendTo(combo->m_list, set);
    }
    combo->Notify(CONTROL_NOTIFY_0B, 0);
    return true;
}

int C_ComboBox::OnLeave(int id)
{
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return 0;
        m_focus = -1;
    }
    return m_open ? 0 : CONTROL_HANDLED;
}

int C_ComboBox::OnInput05(int id)
{
    if (!IsChild(FindChild(static_cast<uint32_t>(id))))
        return OnLeave(id);
    if (m_focus != FindChild(static_cast<uint32_t>(id))) {
        OnLeave(id);
        return OnEnter(id);
    }
    return m_children[m_focus]->OnInput05(id);
}

int C_ComboBox::OnPress(int id)
{
    if (IsChild(m_focus) && m_focus == FindChild(static_cast<uint32_t>(id)))
        return m_children[m_focus]->OnPress(id);
    // A press outside the box closes the open list.
    if (m_id != (static_cast<uint32_t>(id) & ~CHILD_ID_MASK) && m_open) {
        S_ControlMessage toggle = Message(CONTROL_MSG_TOGGLE_LIST);
        HandleMessage(&toggle);
    }
    return 0;
}

void C_ComboBox::ShowPicked(int index, const C_str &text)
{
    m_picked = index;
    m_pickedText = text;
    SendTo(m_header, ValueMessage(CONTROL_MSG_SET_TEXT, COMBO_HEADER_STYLE, m_pickedText));
    SendTo(m_list, Message(CONTROL_MSG_HIDE));
    m_open = false;
}

bool C_ComboBox::HandleMessage(S_ControlMessage *message)
{
    const int index = FindChild(message->target);
    if (IsChild(index))
        return m_children[index]->HandleMessage(message);
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    // (The parts checked: the fix, the original's without them crashes.)
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_TEXT:
    case CONTROL_MSG_ADD_TEXT_ID:
        if (m_list)
            m_list->HandleMessage(message);
        return true;
    case CONTROL_MSG_PICK_INDEX: {
        S_ControlMessage get = ValueMessage(CONTROL_MSG_GET_ITEM, message->value);
        if (m_list)
            m_list->HandleMessage(&get);
        if (static_cast<int>(message->value) >= 0)
            ShowPicked(static_cast<int>(get.value), get.text);
        return true;
    }
    case CONTROL_MSG_SET_HEADER_TEXT:
        message->id = CONTROL_MSG_SET_TEXT;
        if (m_header)
            m_header->HandleMessage(message);
        return true;
    case CONTROL_MSG_SET_HEADER_TEXT_ID:
        message->id = CONTROL_MSG_SET_TEXT_ID;
        [[fallthrough]];
    case CONTROL_MSG_EDIT:
        if (m_header)
            m_header->HandleMessage(message);
        return true;
    case CONTROL_MSG_CLEAR:
        if (m_header)
            m_header->HandleMessage(message);
        if (m_list)
            m_list->HandleMessage(message);
        return true;
    case CONTROL_MSG_TOGGLE_LIST:
        SendTo(m_list, Message(m_open ? CONTROL_MSG_HIDE : CONTROL_MSG_SHOW));
        m_open = !m_open;
        return true;
    case CONTROL_MSG_PICK_SELECTED: {
        S_ControlMessage get = Message(CONTROL_MSG_GET_TEXT);
        if (m_list)
            m_list->HandleMessage(&get);
        if (static_cast<int>(get.value) >= 0) {
            ShowPicked(static_cast<int>(get.value), get.text);
            Notify(CONTROL_NOTIFY_0E, 0);
        }
        return true;
    }
    case CONTROL_MSG_GET_TEXT:
        message->value = static_cast<uint32_t>(m_picked);
        message->text = m_pickedText;
        return true;
    }
    C_Control::HandleMessage(message);
    return true;
}

void C_ComboBox::SetStrings(const std::vector<C_str> &strings)
{
    if (!m_list)
        return;
    if (!strings.empty())
        SendTo(m_header, ValueMessage(CONTROL_MSG_SET_TEXT, COMBO_HEADER_STYLE, strings[0]));
    m_list->SetStrings(strings);
}

void C_ComboBox::SetInactive(bool inactive)
{
    m_inactive = inactive;
    // (The parts checked: the fix, the original's without them crashes.)
    if (m_button)
        m_button->SetInactive(inactive);
    if (m_list)
        m_list->SetInactive(inactive);
    if (m_header)
        m_header->SetInactive(inactive);
}

// ----- the titled combo box (type 0x28) -----

int C_TitledComboBox::FindChild(uint32_t id) const
{
    if (!m_title || !m_combo)
        return -1;
    return (id & CHILD_ID_MASK) != 0 ? 1 : 0;
}

bool C_TitledComboBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_ControlGroup::Create(frame, root, desc))
        return false;
    m_frame->SetData(nullptr);
    if (I3D_frame *text = frame->FindChildFrame("text18", FRAME_MASK_VISUAL)) {
        m_title = new C_TitleControl(m_scene, m_id, this);
        m_title->Create(text, root, nullptr);
        m_title->HandleMessageCopy(Message(CONTROL_MSG_TAKE_INPUT));
        m_children.push_back(m_title);
    }
    I3D_frame *combo = frame->FindChildFrame("game difficulty", FRAME_MASK_VISUAL);
    if (!combo)
        return false;
    m_combo = new C_ComboBox(m_scene, m_id, this);
    m_combo->Create(combo, root, nullptr);
    m_children.push_back(m_combo);
    return true;
}

bool C_TitledComboBox::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_DEACTIVATE:
        if (!m_inactive)
            SetInactive(true);
        return true;
    case CONTROL_MSG_ACTIVATE:
        if (m_inactive)
            SetInactive(false);
        return true;
    case CONTROL_MSG_SHOW:
        Show();
        return true;
    case CONTROL_MSG_HIDE:
        Hide();
        return true;
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_SET_TEXT_ID:
        // (The parts checked: the fix, the original's without them crashes.)
        return m_title ? m_title->HandleMessage(message) : true;
    }
    return m_combo ? m_combo->HandleMessage(message) : true;
}

void C_TitledComboBox::SetInactive(bool inactive)
{
    m_inactive = inactive;
    if (m_title)
        m_title->SetInactive(inactive);
    if (m_combo)
        m_combo->SetInactive(inactive);
}

// ----- the chat input (type 0x21) -----

int C_ChatInput::FindChild(uint32_t id) const
{
    const uint32_t bits = id & CHILD_ID_MASK;
    if (bits == CHAT_INPUT)
        return 0;
    if (bits == CHAT_LABEL)
        return 1;
    return bits > CHAT_TOGGLE ? 2 : -1;
}

bool C_ChatInput::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_ControlGroup::Create(frame, root, desc);
    m_frame->SetData(nullptr);
    if (I3D_frame *input = frame->FindChildFrame("binput*", FIND_DUMMIES)) {
        m_input = new C_EditBox(m_scene, m_id | CHAT_INPUT, this);
        m_input->Create(input, root, nullptr);
        S_ControlMessage prompt = ValueMessage(CONTROL_MSG_SET_TEXT_ID, CHAT_PROMPT_STYLE);
        prompt.value2 = CHAT_PROMPT_TEXT;
        m_input->HandleMessageCopy(prompt);
        m_input->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_MAX_LENGTH, CHAT_MAX_LENGTH));
        S_ControlMessage callback = ValueMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_02);
        callback.value2 = reinterpret_cast<uintptr_t>(this);
        callback.value3 = reinterpret_cast<uintptr_t>(&C_ChatInput::LineEntered);
        m_input->HandleMessageCopy(callback);
        m_children.push_back(m_input);
    }
    if (I3D_frame *text = frame->FindChildFrame("text*", FIND_FRAMES)) {
        m_label = new C_LabelControl(m_scene, m_id | CHAT_LABEL, this);
        m_label->Create(text, root, nullptr);
        m_children.push_back(m_label);
    }
    if (I3D_frame *team = frame->FindChildFrame("bTeam", FIND_DUMMIES)) {
        m_teamToggle = new C_AllToggleButton(m_scene, m_id | CHAT_TOGGLE, this);
        m_teamToggle->Create(team, root, nullptr);
        m_teamToggle->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_CHANGED | CONTROL_MSG_TEAM_TOGGLED));
        m_children.push_back(m_teamToggle);
    }
    return true;
}

bool C_ChatInput::LineEntered(void *object, void *, C_Control *control, uint32_t, uint32_t)
{
    if (!control)
        return true;
    auto *chat = static_cast<C_ChatInput *>(object);
    S_ControlMessage get = Message(CONTROL_MSG_GET_TEXT);
    control->HandleMessage(&get);
    const char *text = get.text.c_str();
    while (static_cast<uint8_t>(*text) < 0x80 && std::isspace(static_cast<unsigned char>(*text)))
        ++text;
    if (*text != '\0') {
        // A command kept; else to the team ("/t") or to all ("/a").
        C_str line;
        if (std::strstr(text, "/t") == text || std::strstr(text, "/a") == text)
            line = C_str(text);
        else
            line = C_fstr(chat->m_team ? "/t%s" : "/a%s", text);
        chat->Notify(CONTROL_NOTIFY_SEND, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(line.c_str())));
    }
    S_ControlMessage prompt = ValueMessage(CONTROL_MSG_SET_TEXT_ID, CHAT_PROMPT_STYLE);
    prompt.value2 = CHAT_PROMPT_TEXT;
    control->HandleMessageCopy(prompt);
    S_ControlMessage edit = Message(CONTROL_MSG_EDIT);
    edit.target = control->GetId();
    PostControlMessage(edit);
    return true;
}

bool C_ChatInput::HandleMessage(S_ControlMessage *message)
{
    const int index = FindChild(message->target);
    if (IsChild(index))
        return m_children[index]->HandleMessage(message);
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_ADD_TEXT:
        case CONTROL_MSG_ADD_TEXT_ID:
        case CONTROL_MSG_INSERT_FIRST:
        case CONTROL_MSG_CLEAR: {
            // (The label checked: the fix, the original's without it crashes.)
            if (!m_label)
                return true;
            m_label->HandleMessage(message);
            // The oldest line removed past 8.
            S_ControlMessage runs = Message(CONTROL_MSG_GET_RUNS);
            m_label->HandleMessage(&runs);
            if (runs.value > CHAT_LINES)
                m_label->HandleMessageCopy(Message(CONTROL_MSG_REMOVE_RUN));
            return true;
        }
        case CONTROL_MSG_TEAM_TOGGLED: {
            S_ControlMessage get = Message(CONTROL_MSG_GET_CHECKED);
            get.target = m_id;
            if (m_teamToggle)
                m_teamToggle->HandleMessage(&get);
            m_team = get.value == 0;
            return true;
        }
        }
    }
    return C_ControlGroup::HandleMessage(message);
}

// ----- the skill (type 0x19) -----

namespace {

// The skill's parts' ids' last bits, its value's limit; its titles' styles.
constexpr uint32_t SKILL_TITLE = 1, SKILL_MINUS = 2, SKILL_PLUS = 3;
constexpr int SKILL_MAX = 100;
constexpr uint32_t SKILL_NAME_STYLE = 0xB, SKILL_VALUE_STYLE = 0x16;
// The keys (DirectInput codes): minus and the keypad's, equals and the keypad's plus.
constexpr int KEY_MINUS = 0x0C, KEY_PAD_MINUS = 0x4A, KEY_EQUALS = 0x0D, KEY_PAD_PLUS = 0x4E;
// The item information's parts' ids' last bits, its picture's face group, its texts' styles.
constexpr uint32_t INFO_PICTURE = 1, INFO_SECOND = 2;
constexpr uint32_t INFO_NAME_STYLE = 0x10, INFO_LINE_STYLE = 0x11, INFO_COUNT_STYLE = 0x12;
// The stats' texts: the weight, the ammo, the muzzle speed, the zoom.
constexpr int TEXT_WEIGHT = 0xAAA, TEXT_AMMO = 0xAAB, TEXT_MUZZLE_SPEED = 0xAAC, TEXT_ZOOM = 0xAAE;
// The zoom: tan(40 degrees) over tan(the scope's field of view / 2).
constexpr double ZOOM_HALF_FOV = 0.6981317202250164;
// The scope's aim modes with a reticle (below the binoculars').
constexpr int SCOPE_RETICLES = 4;

} // namespace

int C_SkillControl::FindChild(uint32_t id) const
{
    return FindChildByLastBits(m_children, id);
}

void C_SkillControl::Refresh()
{
    if (!m_shown)
        return;
    // (The titles checked: the fix, the original's without them crashes.)
    if (m_nameTitle)
        m_nameTitle->Refresh();
    if (m_valueTitle)
        m_valueTitle->Refresh();
}

bool C_SkillControl::Unknown20(int key)
{
#ifdef HD2_DEDICATED   // no keyboard in LS3DRF.dll: the server's copy returns true
    (void)key;
    return true;
#endif
    switch (static_cast<uint8_t>(key)) {
    case KEY_MINUS:
    case KEY_PAD_MINUS:
        PointDown();
        return true;
    case KEY_EQUALS:
    case KEY_PAD_PLUS:
        PointUp();
        return true;
    }
    return false;
}

bool C_SkillControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    if (I3D_frame *text = frame->FindChildFrame("skill_text*", FIND_FRAMES)) {
        m_nameTitle = new C_TitleControl(m_scene, m_id | SKILL_TITLE, this);
        m_nameTitle->Create(text, root, nullptr);
        m_children.push_back(m_nameTitle);
    }
    if (I3D_frame *value = frame->FindChildFrame("skill_value*", FIND_FRAMES)) {
        m_valueTitle = new C_TitleControl(m_scene, m_id | SKILL_TITLE, this);
        m_valueTitle->Create(value, root, nullptr);
        S_ControlMessage margin = Message(CONTROL_MSG_SET_MARGIN);
        margin.value = 1;
        m_valueTitle->HandleMessageCopy(margin);
        m_children.push_back(m_valueTitle);
    }
    if (I3D_frame *minus = frame->FindChildFrame("barrow up*", FIND_DUMMIES)) {
        m_minusButton = new C_Button(m_scene, m_id | SKILL_MINUS, this);
        m_minusButton->Create(minus, root, nullptr);
        m_minusButton->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_PRESS | CONTROL_MSG_SKILL_DOWN));
        m_children.push_back(m_minusButton);
    } else {
        m_takesInput = false;
    }
    if (I3D_frame *plus = frame->FindChildFrame("barrow down*", FIND_DUMMIES)) {
        m_plusButton = new C_Button(m_scene, m_id | SKILL_PLUS, this);
        m_plusButton->Create(plus, root, nullptr);
        m_plusButton->HandleMessageCopy(Route(m_id, CONTROL_NOTIFY_PRESS | CONTROL_MSG_SKILL_UP));
        m_children.push_back(m_plusButton);
    } else {
        m_takesInput = false;
    }
    // (The titles checked: the fix, the original's without them crashes.)
    if (m_plusButton && m_minusButton) {
        if (m_nameTitle)
            m_nameTitle->HandleMessageCopy(Message(CONTROL_MSG_TAKE_INPUT));
        if (m_valueTitle)
            m_valueTitle->HandleMessageCopy(Message(CONTROL_MSG_TAKE_INPUT));
    }
    return true;
}

int C_SkillControl::OnEnter(int id)
{
    if (m_nameTitle)
        m_nameTitle->OnEnter(id);
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return CONTROL_NOT_HANDLED;
    }
    m_focus = FindChild(static_cast<uint32_t>(id));
    if (C_Control *focus = Focus())
        return focus->OnEnter(id);
    return CONTROL_HANDLED;
}

int C_SkillControl::OnLeave(int id)
{
    if (m_nameTitle)
        m_nameTitle->OnLeave(id);
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(id) == 0)
            return 0;
        m_focus = -1;
    }
    return CONTROL_HANDLED;
}

int C_SkillControl::OnInput05(int id)
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

int C_SkillControl::OnPress(int id)
{
    if (m_inactive)
        return CONTROL_HANDLED;
    C_Control *focus = Focus();
    return focus ? focus->OnPress(id) : 0;
}

int C_SkillControl::OnInput08(int id)
{
    if (m_inactive)
        return CONTROL_HANDLED;
    C_Control *focus = Focus();
    return focus ? focus->OnInput08(id) : 0;
}

bool C_SkillControl::AskPool(uint32_t id)
{
    if (m_value == 0)
        return true;
    S_ControlMessage ask = Message(id);
    // (The pool checked: the fix, the original's without it crashes.)
    C_Control *pool = FindControl(m_value, true);
    if (pool)
        pool->HandleMessage(&ask);
    return ask.value != 0;
}

bool C_SkillControl::PointDown()
{
    if (m_points < 1 || !AskPool(CONTROL_MSG_SKILL_UP))
        return false;
    --m_points;
    Update();
    return true;
}

bool C_SkillControl::PointUp()
{
    if (m_base + m_points > SKILL_MAX - 1 || !AskPool(CONTROL_MSG_SKILL_DOWN))
        return false;
    ++m_points;
    Update();
    return true;
}

void C_SkillControl::Update()
{
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%d", m_points + m_base);
    if (m_nameTitle) {
        S_ControlMessage name = Message(CONTROL_MSG_SET_TEXT);
        name.value = SKILL_NAME_STYLE;
        name.text = m_name;
        m_nameTitle->HandleMessageCopy(name);
    }
    if (m_valueTitle) {
        S_ControlMessage value = Message(CONTROL_MSG_SET_TEXT);
        value.value = SKILL_VALUE_STYLE;
        value.text = C_str(digits);
        m_valueTitle->HandleMessageCopy(value);
    }
    if (m_minusButton)
        m_minusButton->HandleMessageCopy(Message(m_points < 1 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE));
    if (m_plusButton)
        m_plusButton->HandleMessageCopy(Message(m_points + m_base < SKILL_MAX ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE));
}

bool C_SkillControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_VALUE:
        C_Control::HandleMessage(message);
        return true;
    case CONTROL_MSG_SET_TEXT:
        m_name = message->text;
        return true;
    case CONTROL_MSG_SKILL_UP:
        message->value = PointUp();
        return true;
    case CONTROL_MSG_SKILL_DOWN:
        message->value = PointDown();
        return true;
    case CONTROL_MSG_SET_BASE:
        m_base = static_cast<int>(message->value);
        m_points = 0;
        Update();
        return true;
    case CONTROL_MSG_SET_TOTAL:
        m_points = static_cast<int>(message->value) - m_base;
        if (m_points < 0)
            m_points = 0;
        Update();
        return true;
    case CONTROL_MSG_GET_NUMBER:
        message->value = static_cast<uint32_t>(m_base + m_points);
        return true;
    }
    // (The titles checked: the fix, the original's without them crashes.)
    if (m_nameTitle)
        m_nameTitle->HandleMessage(message);
    if (m_valueTitle)
        m_valueTitle->HandleMessage(message);
    return true;
}

void C_SkillControl::SetInactive(bool inactive)
{
    m_inactive = inactive;
    if (m_nameTitle)
        m_nameTitle->SetInactive(inactive);
    if (m_valueTitle)
        m_valueTitle->SetInactive(m_inactive);
    if (m_plusButton)
        m_plusButton->SetInactive(m_inactive);
    if (m_minusButton)
        m_minusButton->SetInactive(m_inactive);
}

// ----- the item information (type 0x1c) -----

int C_ItemInfo::FindChild(uint32_t id) const
{
    switch (id & CHILD_ID_MASK) {
    case INFO_PICTURE:
        return 0;
    case INFO_SECOND:
        return 1;
    }
    return -1;
}

void C_ItemInfo::Refresh()
{
    if (!m_shown)
        return;
    // (The parts checked: the fix, the original's without them crashes.)
    for (C_Control *part : std::initializer_list<C_Control *>{m_picture, m_secondPicture, m_stats})
        if (part)
            part->Refresh();
}

bool C_ItemInfo::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_ControlGroup::Create(frame, root, desc))
        return false;
    S_ControlMessage square = Message(CONTROL_MSG_FIND_FACE_GROUP);
    square.text = "X_SQUARE.BMP";
    if (frame) {
        m_picture = new C_PictureControl(m_scene, m_id | INFO_PICTURE, this);
        m_picture->Create(frame, root, nullptr);
        m_picture->HandleMessageCopy(Message(CONTROL_MSG_HIDE));
        m_picture->HandleMessageCopy(square);
        m_children.push_back(m_picture);
    }
    if (I3D_frame *ammo = frame->FindChildFrame("ammo*", FIND_FRAMES)) {
        m_secondPicture = new C_PictureControl(m_scene, m_id | INFO_SECOND, this);
        m_secondPicture->Create(ammo, root, nullptr);
        m_secondPicture->HandleMessageCopy(Message(CONTROL_MSG_HIDE));
        m_secondPicture->HandleMessageCopy(square);
        m_children.push_back(m_secondPicture);
    }
    if (I3D_frame *text = frame->FindChildFrame("text_info*", FIND_FRAMES)) {
        m_stats = new C_TitleControl(m_scene, m_id, this);
        m_stats->Create(text, root, nullptr);
        m_children.push_back(m_stats);
    }
    return true;
}

void C_ItemInfo::ShowStats(C_ItemDef *def)
{
    if (!m_stats)
        return;   // (the fix: the original's without its title crashes)
    auto line = [this](const C_str &text) {
        S_ControlMessage add = Message(CONTROL_MSG_ADD_TEXT);
        add.value = INFO_LINE_STYLE;
        add.text = text;
        m_stats->HandleMessageCopy(add);
    };
    m_stats->HandleMessageCopy(Message(CONTROL_MSG_CLEAR));
    S_ControlMessage name = Message(CONTROL_MSG_ADD_TEXT_ID);
    name.value = INFO_NAME_STYLE;
    name.value2 = static_cast<uint32_t>(def->GetTextId());
    m_stats->HandleMessageCopy(name);
    line(C_fstr("%s : %.2f kg", g_texts.Get(TEXT_WEIGHT).c_str(), static_cast<double>(def->GetWeight())));
    const int behaviour = def->GetBehaviour();
    if (behaviour < ITEM_BEHAVIOUR_SIDEARM || (behaviour > ITEM_BEHAVIOUR_LMG && behaviour != ITEM_BEHAVIOUR_ASSAULT))
        return;
    if (C_ItemDef *ammo = GetItemDef(def->GetAmmoItemId()))
        line(C_fstr("%s : %d", g_texts.Get(TEXT_AMMO).c_str(), static_cast<int>(ammo->GetAmmoAmount())));
    const E_ItemComponentKind kind = def->GetComponentKind();
    if (kind == ICK_PROJECTILE_WEAPON || kind == ICK_FIREARM) {
        const float speed = static_cast<C_WeaponComp *>(def->GetComponent())->GetMuzzleVelocity();
        line(C_fstr("%s : %.2f m/s", g_texts.Get(TEXT_MUZZLE_SPEED).c_str(), static_cast<double>(speed)));
    }
    const C_ItemComponent *secondary = def->GetSecondaryComponent();
    if (def->GetBehaviour() == ITEM_BEHAVIOUR_RIFLE && secondary && secondary->GetKind() == ICK_SCOPE) {
        const auto *scope = static_cast<const C_ScopeComp *>(secondary);
        if (scope->GetAimMode() < SCOPE_RETICLES) {
            const auto zoom = static_cast<float>(std::tan(ZOOM_HALF_FOV) / std::tan(scope->GetAimedFovFactor() * 0.5));
            line(C_fstr("%s : %.2f x", g_texts.Get(TEXT_ZOOM).c_str(), static_cast<double>(zoom)));
        }
    }
}

void C_ItemInfo::UpdateCounts()
{
    // (The parts checked: the fix, the original's without them crashes.)
    if (m_secondPicture)
        m_secondPicture->HandleMessageCopy(Message(m_secondCount == 0 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE));
    if (m_picture)
        m_picture->HandleMessageCopy(Message(m_count == 0 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE));
    if (m_count + m_secondCount > 0 && m_parent) {
        S_ControlMessage counted = Message(CONTROL_MSG_ITEM_COUNTED);
        counted.value = m_id;
        m_parent->HandleMessageCopy(counted);
    }
    S_ControlMessage count = Message(CONTROL_MSG_SET_TEXT);
    count.value = INFO_COUNT_STYLE;
    count.text = C_fstr("%d ", m_count);
    if (m_picture)
        m_picture->HandleMessageCopy(count);
    count.text = C_fstr("%d ", m_secondCount);
    if (m_secondPicture)
        m_secondPicture->HandleMessageCopy(count);
}

void C_ItemInfo::ShowPicture(C_PictureControl *picture, int id, C_ItemDef *def)
{
    if (!picture)
        return;
    S_ControlMessage index = Message(CONTROL_MSG_SET_INDEX);
    index.value = static_cast<uint32_t>(id);
    picture->HandleMessageCopy(index);
    S_ControlMessage texture = Message(CONTROL_MSG_SET_TEXTURE);
    texture.value2 = reinterpret_cast<uintptr_t>(def->GetImage());
    picture->HandleMessageCopy(texture);
}

bool C_ItemInfo::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_ControlGroup::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_ITEM_COUNT:
        m_count = static_cast<int>(message->value);
        UpdateCounts();
        return true;
    case CONTROL_MSG_SET_SECOND_COUNT:
        m_secondCount = static_cast<int>(message->value);
        UpdateCounts();
        return true;
    case CONTROL_MSG_ADD_CALLBACK:
    case CONTROL_MSG_REMOVE_CALLBACK:
        if (m_picture)
            m_picture->HandleMessage(message);
        if (m_secondPicture)
            m_secondPicture->HandleMessage(message);
        return true;
    case CONTROL_MSG_SET_ITEM: {
        m_item = static_cast<int>(message->value);
        C_ItemDef *def = GetItemDef(m_item);
        if (!def)
            return true;
        ShowStats(def);
        ShowPicture(m_picture, m_item, def);
        // The second picture: a weapon's ammo, a uniform's headgear.
        if (def->GetRecordType() == ITEM_ANIMATED)
            m_second = def->GetAmmoItemId();
        else if (def->GetBehaviour() == ITEM_BEHAVIOUR_UNIFORM)
            m_second = def->GetHeadgearItemId();
        else
            return true;
        if (C_ItemDef *second = GetItemDef(m_second))
            ShowPicture(m_secondPicture, m_second, second);
        return true;
    }
    case CONTROL_MSG_REFRESH:
        for (C_Control *part : std::initializer_list<C_Control *>{m_picture, m_secondPicture, m_stats})
            if (part)
                part->HandleMessageCopy(*message);
        return true;
    }
    C_ControlGroup::HandleMessage(message);
    return true;
}

// ----- the list group (type 0xb) and its items (type 6) -----

namespace {

// The navigation keys (DirectInput codes).
constexpr int KEY_HOME = 0xC7, KEY_UP = 0xC8, KEY_PAGE_UP = 0xC9, KEY_LEFT = 0xCB, KEY_RIGHT = 0xCD, KEY_END = 0xCF,
              KEY_DOWN = 0xD0, KEY_PAGE_DOWN = 0xD1;
// The list's scroll directions (Scroll).
constexpr int SCROLL_NONE = 0, SCROLL_BACK = 1, SCROLL_ON = 2;
// The enter / leave id the list's focus moves use; the item's unpick id.
constexpr int FOCUS_MOVE_ID = 0xF;
constexpr int UNPICK_ID = 1;
// The selector just in front of its item (a double: the original's).
constexpr double SELECTOR_DEPTH = 0.1;

} // namespace

C_ListGroup::~C_ListGroup()
{
    if (m_origin)
        _aligned_free(m_origin);
    delete m_extra;
    if (m_cursor)
        m_cursor->Release();
}

bool C_ListGroup::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return true;
    m_selector = frame->FindChildFrame("selector*", FIND_FRAMES);
    if (m_selector)
        m_selector->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));   // (not when its place is locked)
    return true;
}

int C_ListGroup::ScrollRange() const
{
    return ItemCount() - m_visible + 1;
}

void C_ListGroup::PostToValue(uint32_t id, uint32_t value) const
{
    if (m_value == 0)
        return;
    S_ControlMessage message = Message(id);
    message.target = m_value;
    message.value = value;
    PostControlMessage(message);
}

bool C_ListGroup::PickItem(C_Control *item)
{
    if (m_picked != item) {
        if (m_picked) {
            m_picked->OnInput02(UNPICK_ID);
            m_picked->OnLeave(UNPICK_ID);
        }
        m_picked = item;
    }
    return true;
}

bool C_ListGroup::Scroll(int direction)
{
    if (direction == SCROLL_ON) {
        // (Unsigned: without items it scrolls on, the original's.)
        if (static_cast<uint32_t>(ItemCount() - 1) <= static_cast<uint32_t>(m_last))
            return false;
        ++m_first;
        ++m_last;
        if (m_downStep != 0.0f) {
            const S_vector place = m_frame->GetWorldPos();
            m_frame->SetPos(S_vector(place.x, m_downStep + place.y, place.z));
            m_frame->Update();
        }
        if (m_acrossStep != 0.0f) {
            const S_vector place = m_frame->GetWorldPos();
            m_frame->SetPos(S_vector(-m_acrossStep + place.x, place.y, place.z));
            m_frame->Update();
        }
    } else if (direction == SCROLL_BACK) {
        if (m_first < 1)
            return false;
        --m_first;
        --m_last;
        if (m_downStep != 0.0f) {
            const S_vector place = m_frame->GetWorldPos();
            m_frame->SetPos(S_vector(place.x, -m_downStep + place.y, place.z));
            m_frame->Update();
        }
        if (m_acrossStep != 0.0f) {
            const S_vector place = m_frame->GetWorldPos();
            m_frame->SetPos(S_vector(m_acrossStep + place.x, place.y, place.z));
            m_frame->Update();
        }
    }
    for (int i = 0; i < ItemCount(); ++i) {
        if (i < m_first || i > m_last)
            m_children[i]->Hide();
        else
            m_children[i]->Show();
        m_children[i]->HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
    }
    if (m_selector)
        m_selector->SetOn(m_selected >= m_first && m_selected <= m_last);
    return true;
}

void C_ListGroup::Select(int index)
{
    if (!IsChild(index))
        return;
    m_selected = index;
    while (m_selected < m_first)
        Scroll(SCROLL_BACK);
    while (m_last < m_selected)
        Scroll(SCROLL_ON);
    if (m_selector) {
        const S_vector place = m_children[index]->GetFrame()->GetPos();
        m_selector->SetPosInline(S_vector(place.x, place.y, static_cast<float>(place.z - SELECTOR_DEPTH)));
        m_selector->SetOn(true);
    }
    PostToValue(CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(m_first));
}

bool C_ListGroup::FocusPrevious()
{
    int i = m_focus == -1 ? ItemCount() : m_focus;
    do {
        if (--i < 0)
            return false;
    } while (!IsChild(i) || !m_children[i]->TakesInput());
    if (C_Control *focus = Focus()) {
        if (focus->OnLeave(FOCUS_MOVE_ID) == 0)
            return true;
    }
    m_focus = i;
    m_children[i]->OnEnter(FOCUS_MOVE_ID);
    return true;
}

bool C_ListGroup::FocusNext()
{
    for (int i = m_focus + 1; i < ItemCount(); ++i) {
        if (!IsChild(i) || !m_children[i]->TakesInput())
            continue;
        if (C_Control *focus = Focus()) {
            if (focus->OnLeave(FOCUS_MOVE_ID) == 0)
                return true;
        }
        m_focus = i;
        m_children[i]->OnEnter(FOCUS_MOVE_ID);
        return true;
    }
    return false;
}

bool C_ListGroup::Unknown20(int key)
{
#ifdef HD2_DEDICATED   // no keyboard in LS3DRF.dll: the server's copy returns false
    (void)key;
    return false;
#endif
    if (C_Control *focus = Focus()) {
        if (focus->TakesInput() && focus->Unknown20(key))
            return true;
    }
    switch (static_cast<uint8_t>(key)) {
    case KEY_HOME:
        while (Unknown20(KEY_UP)) {
        }
        return false;
    case KEY_UP:
    case KEY_LEFT:
        if (m_focus == m_first) {
            if (!Scroll(SCROLL_BACK))
                return false;
            PostToValue(CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(m_first));
        }
        return FocusPrevious();
    case KEY_PAGE_UP:
        // (True when a step fails, false after all: the original's.)
        for (int i = 0; i < m_last - m_first; ++i)
            if (!Unknown20(KEY_UP))
                return true;
        return false;
    case KEY_RIGHT:
    case KEY_DOWN:
        if (m_focus == m_last) {
            if (!Scroll(SCROLL_ON))
                return false;
            PostToValue(CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(m_first));
        }
        return FocusNext();
    case KEY_END:
        while (Unknown20(KEY_DOWN)) {
        }
        return false;
    case KEY_PAGE_DOWN:
        for (int i = 0; i < m_last - m_first; ++i)
            if (!Unknown20(KEY_DOWN))
                return true;
        return false;
    }
    return false;
}

int C_ListGroup::OnClick(int id)
{
    const int result = C_ControlGroup::OnClick(id);
    if (result != 0)
        Select(m_focus);
    return result;
}

int C_ListGroup::OnPress(int id)
{
    const int result = C_ControlGroup::OnPress(id);
    if (result != 0)
        Select(m_focus);
    return result;
}

int C_ListGroup::OnWheel(int, int amount)
{
    if (m_value == 0)
        return CONTROL_NOT_HANDLED;
    C_Control *bar = FindControl(m_value, true);
    if (amount > 0) {
        for (unsigned steps = (static_cast<unsigned>(amount) - 1) / WHEEL_STEP + 1; steps; --steps)
            if (bar)
                bar->HandleMessageCopy(Message(CONTROL_MSG_SCROLL_UP));
    } else if (amount < 0) {
        for (unsigned steps = (static_cast<unsigned>(-amount) - 1) / WHEEL_STEP + 1; steps; --steps)
            if (bar)
                bar->HandleMessageCopy(Message(CONTROL_MSG_SCROLL_DOWN));
    }
    return CONTROL_HANDLED;
}

void C_ListGroup::Sort()
{
    if (!m_sorted)
        return;
    const int count = ItemCount();
    for (int i = 0; i < count - 1; ++i)
        for (int j = i + 1; j < count; ++j)
            if (m_children[j]->GetId() < m_children[i]->GetId())
                std::swap(m_children[i], m_children[j]);
}

void C_ListGroup::Layout(int)
{
    if (!m_origin)
        return;
    for (int i = 0; i < ItemCount(); ++i) {
        I3D_frame *frame = m_children[i]->GetFrame();
        if (m_downStep != 0.0f)
            frame->SetPosInline(S_vector(m_origin->x, -(static_cast<float>(i) * m_downStep) + m_origin->y, m_origin->z));
        if (m_acrossStep != 0.0f)
            frame->SetPosInline(S_vector(static_cast<float>(i) * m_acrossStep + m_origin->x, m_origin->y, m_origin->z));
    }
}

bool C_ListGroup::HandleMessage(S_ControlMessage *message)
{
    const int target = FindChild(message->target);
    if (IsChild(target))
        return m_children[target]->HandleMessage(message);
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    S_ControlMessage refresh = Message(CONTROL_MSG_REFRESH);
    refresh.target = m_id;
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_REMOVE_ITEM: {
        const int index = FindChild(message->value);
        if (index < 0)
            return true;
        C_Control *item = m_children[index];
        // The screen told of its frame.
        S_ControlMessage removed = Message(CONTROL_MSG_REMOVE_ITEM);
        removed.value2 = reinterpret_cast<uintptr_t>(item->GetFrame());
        PostControlMessage(removed);
        if (m_picked == item)
            m_picked = nullptr;
        delete item;
        m_children.erase(m_children.begin() + index);
        while (m_first != 0 && ItemCount() - m_first < m_visible)
            Scroll(SCROLL_BACK);
        HandleMessageCopy(refresh);
        return true;
    }
    case CONTROL_MSG_SHOW:
        Show();
        PickItem(nullptr);
        return true;
    case CONTROL_MSG_HIDE:
        Hide();
        PickItem(nullptr);
        return true;
    case CONTROL_MSG_CAN_SCROLL_UP:
        message->value = m_first > 0;
        return true;
    case CONTROL_MSG_CAN_SCROLL_DOWN:
        message->value = m_last < ItemCount() - 1;
        return true;
    case CONTROL_MSG_GET_SCROLL_RANGE:
        message->value = static_cast<uint32_t>(ScrollRange());
        return true;
    case CONTROL_MSG_SET_VISIBLE:
        m_visible = static_cast<int>(message->value);
        m_last = m_visible - 1;
        HandleMessageCopy(refresh);
        return true;
    case CONTROL_MSG_SET_ACROSS_STEP:
        m_downStep = 0.0f;
        m_acrossStep = ValueFloat(message->value2);
        return true;
    case CONTROL_MSG_SET_DOWN_STEP:
        m_acrossStep = 0.0f;
        m_downStep = ValueFloat(message->value2);
        return true;
    case CONTROL_MSG_ADD_ITEM: {
        auto *desc = reinterpret_cast<S_ControlDesc *>(message->value2);
        if (!IsChild(FindChild(desc->id))) {
            CreateChild(m_frame, reinterpret_cast<I3D_frame *>(static_cast<uintptr_t>(message->value)), desc);
            Sort();
            HandleMessageCopy(refresh);
        }
        delete desc;
        return true;
    }
    case CONTROL_MSG_NEXT_ITEM:
        Scroll(SCROLL_ON);
        return true;
    case CONTROL_MSG_PREVIOUS_ITEM:
        Scroll(SCROLL_BACK);
        return true;
    case CONTROL_MSG_CLEAR:
        while (!m_children.empty()) {
            S_ControlMessage removed = Message(CONTROL_MSG_REMOVE_ITEM);
            removed.value2 = reinterpret_cast<uintptr_t>(m_children.back()->GetFrame());
            PostControlMessage(removed);
            delete m_children.back();
            m_children.pop_back();
        }
        m_frame->SetPos(S_vector(0.0f, 0.0f, 0.0f));
        m_first = 0;
        m_last = m_visible - 1;
        HandleMessageCopy(refresh);
        m_picked = nullptr;
        return true;
    case CONTROL_MSG_SET_ORIGIN:
        m_origin = reinterpret_cast<S_vector *>(message->value2);
        return true;
    case CONTROL_MSG_GET_POSITION:
        message->value = static_cast<uint32_t>(m_first);
        return true;
    case CONTROL_MSG_SET_POSITION: {
        const int position = static_cast<int>(message->value);
        while (position != m_first) {
            if (!(position > m_first ? Scroll(SCROLL_ON) : Scroll(SCROLL_BACK)))
                break;
        }
        return true;
    }
    case CONTROL_MSG_REFRESH:
        Layout(0);
        Scroll(SCROLL_NONE);
        if (m_value != 0) {
            PostToValue(CONTROL_MSG_SET_COUNT, static_cast<uint32_t>(ScrollRange()));
            if (ScrollRange() != 0)
                PostToValue(CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(m_first));
        }
        return true;
    case CONTROL_MSG_SELECT_INDEX:
        Select(static_cast<int>(message->value));
        return true;
    }
    C_Control::HandleMessage(message);
    return true;
}

int C_ListItem::OnEnter(int)
{
    if (!m_picked)
        m_box->SetHover(0);
    Notify(CONTROL_NOTIFY_ENTER, 0);
    return CONTROL_HANDLED;
}

int C_ListItem::OnLeave(int)
{
    if (!m_picked)
        m_box->ClearHover();
    Notify(CONTROL_NOTIFY_LEAVE, 0);
    return CONTROL_HANDLED;
}

int C_ListItem::OnClick(int id)
{
    m_picked = true;
    // (Its parent taken to be a list group: the original's.)
    static_cast<C_ListGroup *>(m_parent)->PickItem(this);
    C_TextButton::OnEnter(id);
    Notify(CONTROL_NOTIFY_CLICK, 0);
    return CONTROL_HANDLED;
}

int C_ListItem::OnInput02(int id)
{
    if (id == UNPICK_ID) {
        m_picked = false;
        C_TextButton::OnLeave(UNPICK_ID);
        Notify(CONTROL_NOTIFY_02, 0);
    }
    return CONTROL_HANDLED;
}

int C_ListItem::OnPress(int id)
{
    Notify(CONTROL_NOTIFY_PRESS, 0);
    return OnClick(id);
}

// ----- the bar (type 0x16) and the upgrade bar (type 0x17) -----

namespace {

constexpr int BAR_AXIS_X = 0, BAR_AXIS_Y = 1, BAR_AXIS_Z = 2;
// The upgrade bar's text: the value and the suffix, the upgrade (shown from 0.1).
constexpr uint32_t UPGRADE_TEXT_STYLE = 0x2B, UPGRADE_ADDED_STYLE = 0x2C;
constexpr float UPGRADE_SHOWN = 0.1f;

} // namespace

C_BarControl::C_BarControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent)
{
    m_takesInput = false;
}

bool C_BarControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return false;
    UpdateBar();
    return true;
}

bool C_BarControl::HandleMessage(S_ControlMessage *message)
{
    if (!(message->id & CONTROL_NOTIFY_MASK)) {
        switch (message->id & CONTROL_MSG_MASK) {
        case CONTROL_MSG_GET_BAR_MIN:
            message->value2 = FloatValue(m_min);
            return true;
        case CONTROL_MSG_GET_BAR_MAX:
            message->value2 = FloatValue(m_max);
            return true;
        case CONTROL_MSG_GET_BAR_VALUE:
            message->value2 = FloatValue(m_value);
            return true;
        case CONTROL_MSG_SET_BAR_MIN:
            m_min = ValueFloat(message->value2);
            UpdateBar();
            return true;
        case CONTROL_MSG_SET_BAR_MAX:
            m_max = ValueFloat(message->value2);
            UpdateBar();
            return true;
        case CONTROL_MSG_SET_BAR_VALUE:
            m_value = ValueFloat(message->value2);
            UpdateBar();
            return true;
        case CONTROL_MSG_SET_BAR_AXIS:
            m_axis = static_cast<int>(message->value);
            UpdateBar();
            return true;
        }
    }
    C_Control::HandleMessage(message);
    return true;
}

void C_BarControl::ScaleAlong(I3D_frame *frame, int axis, float fraction)
{
    if (axis == BAR_AXIS_X)
        frame->SetScale(S_vector(fraction, 1.0f, 1.0f));
    else if (axis == BAR_AXIS_Y)
        frame->SetScale(S_vector(1.0f, fraction, 1.0f));
    else if (axis == BAR_AXIS_Z)
        frame->SetScale(S_vector(1.0f, 1.0f, fraction));
}

void C_BarControl::UpdateBar()
{
    float range = m_max - m_min;
    if (range <= 0.0f)
        range = 1.0f;
    // (The value over the range, not its part above the minimum: the original's.)
    float fraction = m_value / range;
    if (fraction < 0.0f || fraction == 0.0f) {
        m_frame->SetOn(false);
        m_frame->Update();
        return;
    }
    if (fraction > 1.0f)
        fraction = 1.0f;
    m_frame->SetOn(true);
    ScaleAlong(m_frame, m_axis, fraction);
    m_frame->Update();
}

void C_UpgradeBar::Refresh()
{
    if (!m_shown)
        return;
    // (The titles checked: the fix, the original's without them crashes.)
    if (m_name)
        m_name->Refresh();
    if (m_text)
        m_text->Refresh();
}

bool C_UpgradeBar::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_BarControl::Create(frame, root, desc))
        return false;
    if (I3D_frame *name = frame->FindChildFrame("name_*", FIND_FRAMES)) {
        m_name = new C_TitleControl(m_scene, m_id, this);
        m_name->Create(name, root, nullptr);
        m_children.push_back(m_name);
    }
    if (I3D_frame *text = frame->FindChildFrame("text_*", FIND_FRAMES)) {
        m_text = new C_TitleControl(m_scene, m_id, this);
        m_text->Create(text, root, nullptr);
        m_children.push_back(m_text);
    }
    m_valueFrame = frame->FindChildFrame("hlcpct*", FIND_FRAMES);
    m_upgradeFrame = frame->FindChildFrame("upgrade*", FIND_FRAMES);
    if (!m_valueFrame || !m_upgradeFrame)
        return false;
    UpdateBar();
    return true;
}

bool C_UpgradeBar::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_SET_TEXT_ID:
        // (The title checked: the fix, the original's without it crashes.)
        if (m_name)
            m_name->HandleMessage(message);
        return true;
    case CONTROL_MSG_GET_BAR_VALUE:
        if (message->value == 0)
            message->value2 = FloatValue(m_current);
        else if (message->value == 1)
            message->value2 = FloatValue(m_upgrade);
        return true;
    case CONTROL_MSG_SET_BAR_VALUE:
        if (message->value == 0)
            m_current = ValueFloat(message->value2);
        else if (message->value == 1)
            m_upgrade = ValueFloat(message->value2);
        else if (message->value == 2)
            m_suffix = reinterpret_cast<const char *>(message->value2);
        UpdateBar();
        return true;
    default:
        C_BarControl::HandleMessage(message);
        return true;
    }
}

void C_UpgradeBar::UpdateBar()
{
    if (m_current == 0.0f && m_upgrade == 0.0f) {
        if (m_shown)
            Hide();
        m_frame->Update();
        return;
    }
    if (!m_shown)
        Show();
    float range = m_max - m_min;
    if (range <= 0.0f)
        range = 1.0f;
    // (Over the range, as the bar's; a NaN divided: the original's.)
    const float value = m_current > m_max ? 1.0f : m_current < m_min ? 0.0f : m_current / range;
    ScaleAlong(m_valueFrame, m_axis, value);
    const float sum = m_upgrade + m_current;
    const float upgraded = sum < m_min || m_upgrade == 0.0f ? 0.0f : sum > m_max ? 1.0f : sum / range;
    ScaleAlong(m_upgradeFrame, m_axis, upgraded);
    const float shown = m_current > m_max ? m_max : m_current;
    const float added = shown + m_upgrade > m_max ? m_max - shown : m_upgrade;
    if (m_text) {
        m_text->HandleMessageCopy(Message(CONTROL_MSG_CLEAR));
        m_text->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, UPGRADE_TEXT_STYLE,
                                               C_fstr("%.1f ", static_cast<double>(shown))));
        if (added >= UPGRADE_SHOWN)
            m_text->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, UPGRADE_ADDED_STYLE,
                                                   C_fstr("+ %.1f", static_cast<double>(added))));
        m_text->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, UPGRADE_TEXT_STYLE, m_suffix));
    }
    m_frame->Update();
}

// ----- the hiding list (type 0x23) and the pool list (type 0x26) -----

namespace {

constexpr uint32_t POOL_ENTRY_STYLE = 0xD;

} // namespace

C_HidingList::~C_HidingList()
{
    while (!m_hidden.empty()) {
        delete m_hidden.back();
        m_hidden.pop_back();
    }
}

int C_HidingList::FindHidden(uint32_t id) const
{
    for (int i = HiddenCount(); i-- > 0;)
        if (m_hidden[i]->GetId() == id)
            return i;
    return -1;
}

bool C_HidingList::HandleMessage(S_ControlMessage *message)
{
    const int target = FindChild(message->target);
    if (IsChild(target))
        return m_children[target]->HandleMessage(message);
    const int hidden = FindHidden(message->target);
    if (hidden >= 0)
        return m_hidden[hidden]->HandleMessage(message);
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_ListGroup::HandleMessage(message);
        return true;
    }
    S_ControlMessage refresh = Message(CONTROL_MSG_REFRESH);
    refresh.target = m_id;
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ITEM_COUNTED: {
        if (IsChild(FindChild(message->value)))
            return true;
        const int index = FindHidden(message->value);
        if (index >= 0) {
            m_children.push_back(m_hidden[index]);
            m_hidden.erase(m_hidden.begin() + index);
            m_children.back()->OnLeave(0);
        }
        Sort();
        if (m_shown)
            HandleMessageCopy(refresh);
        return true;
    }
    case CONTROL_MSG_HIDE_ITEM: {
        const int index = FindChild(message->value);
        if (index < 0)
            return true;
        C_Control *item = m_children[index];
        if (index == m_focus) {
            item->OnLeave(0);
            m_focus = -1;
        }
        item->Hide();
        m_hidden.push_back(item);
        m_children.erase(m_children.begin() + index);
        // (Unsigned: the original's.)
        while (m_first != 0 && static_cast<uint32_t>(ItemCount() - m_first) < static_cast<uint32_t>(m_visible))
            Scroll(SCROLL_BACK);
        if (m_shown)
            HandleMessageCopy(refresh);
        return true;
    }
    case CONTROL_MSG_HIDE_ALL:
        PickItem(nullptr);
        for (int i = ItemCount(); i-- > 0;)
            m_children[i]->Hide();
        m_hidden.insert(m_hidden.end(), m_children.begin(), m_children.end());
        m_children.clear();
        while (m_first != 0 && ItemCount() - m_first < m_visible)
            Scroll(SCROLL_BACK);
        if (m_shown)
            HandleMessageCopy(refresh);
        return true;
    default:
        C_ListGroup::HandleMessage(message);
        return true;
    }
}

C_PoolList::C_PoolList(I3D_scene *scene, uint32_t id, C_Control *parent) : C_HidingList(scene, id, parent)
{
    m_sorted = false;
}

int C_PoolList::OnInput08(int id)
{
    return m_held ? m_held->OnInput08(id) : 0;
}

int C_PoolList::OnInput09(int id)
{
    if (!m_held)
        return 0;
    const int result = m_held->OnInput09(id);
    m_held = nullptr;
    return result;
}

int C_PoolList::OnInput0A(int id)
{
    if (!IsChild(m_focus))
        return 0;
    m_held = m_children[m_focus];
    return m_held->OnInput0A(id);
}

bool C_PoolList::HandleMessage(S_ControlMessage *message)
{
    const int target = FindChild(message->target);
    if (IsChild(target))
        return m_children[target]->HandleMessage(message);
    const int hidden = FindHidden(message->target);
    if (hidden >= 0)
        return m_hidden[hidden]->HandleMessage(message);
    const uint32_t code = message->id & CONTROL_MSG_MASK;
    if ((message->id & CONTROL_NOTIFY_MASK) || (code != CONTROL_MSG_ADD_CURRENT_ENTRY && code != CONTROL_MSG_ADD_ENTRY)) {
        C_HidingList::HandleMessage(message);
        return true;
    }
    if (message->value2 == 0 || HiddenCount() <= 0)
        return false;
    // The first hidden item named, indexed and shown.
    const uint32_t item = m_hidden.front()->GetId();
    // (value2: the C_MpMap of the picked maps' list, its name at +0x01 in the original.)
    S_ControlMessage text =
        ValueMessage(CONTROL_MSG_SET_TEXT, POOL_ENTRY_STYLE, reinterpret_cast<const C_MpMap *>(message->value2)->name);
    text.target = item;
    HandleMessageCopy(text);
    S_ControlMessage index = ValueMessage(CONTROL_MSG_SET_INDEX, message->value);
    index.target = item;
    HandleMessageCopy(index);
    HandleMessageCopy(ValueMessage(CONTROL_MSG_ITEM_COUNTED, item));
    if (code == CONTROL_MSG_ADD_CURRENT_ENTRY)
        OnInput05(item);
    return true;
}

// ----- the server list (type 0x18) and its rows -----

namespace {

constexpr int SERVER_ROWS = 11;
constexpr uint32_t SERVER_ROW_STEP = 0x3D072B02;        // 0.033: the rows' step down
constexpr size_t SERVER_RESERVE = 200;
// The rows' runs (the run, the style).
constexpr uint32_t ROW_NAME_STYLE = 0x1E, ROW_MAP_STYLE = 0x1F, ROW_TYPE_STYLE = 0x20, ROW_PING_STYLE = 0x21,
                   ROW_PLAYERS_STYLE = 0x22;
// A name that is not text in the font: a text in its place.
constexpr int TEXT_BAD_NAME = 0x7E9;
constexpr uint32_t ROW_CURRENT_COLOUR = 0xFFFFFFFF;
// The sort key: the field (the low 3 bits), ascending when the direction bits are 0x40.
constexpr uint32_t SORT_FIELD = 7, SORT_DIRECTION = 0xC0, SORT_ASCENDING = 0x40;
enum : uint32_t { SORT_NAME = 1, SORT_MAP = 2, SORT_TYPE = 3, SORT_PING = 4, SORT_PLAYERS = 5 };

// The comparisons (0x006675a0 .. 0x006676f0, fastcall); ties by the ping.
bool ServerPingLess(const S_ServerEntry *a, const S_ServerEntry *b) { return a->ping < b->ping; }
bool ServerPingGreater(const S_ServerEntry *a, const S_ServerEntry *b) { return b->ping < a->ping; }
bool ServerNameLess(const S_ServerEntry *a, const S_ServerEntry *b) { return _stricmp(a->name, b->name) < 0; }
bool ServerNameGreater(const S_ServerEntry *a, const S_ServerEntry *b) { return _stricmp(a->name, b->name) > 0; }
// (Descending maps tied ascending by ping: the original's.)
bool ServerMapLess(const S_ServerEntry *a, const S_ServerEntry *b)
{
    const int order = _stricmp(a->map, b->map);
    return order < 0 || (order == 0 && ServerPingLess(a, b));
}
bool ServerMapGreater(const S_ServerEntry *a, const S_ServerEntry *b)
{
    const int order = _stricmp(a->map, b->map);
    return order > 0 || (order == 0 && ServerPingLess(a, b));
}
bool ServerTypeLess(const S_ServerEntry *a, const S_ServerEntry *b)
{
    return a->gameType < b->gameType || (a->gameType == b->gameType && ServerPingLess(a, b));
}
bool ServerTypeGreater(const S_ServerEntry *a, const S_ServerEntry *b)
{
    return a->gameType > b->gameType || (a->gameType == b->gameType && ServerPingGreater(a, b));
}
bool ServerPlayersLess(const S_ServerEntry *a, const S_ServerEntry *b)
{
    if (a->maxPlayers != b->maxPlayers)
        return a->maxPlayers < b->maxPlayers;
    if (a->players != b->players)
        return a->players < b->players;
    return ServerPingLess(a, b);
}
bool ServerPlayersGreater(const S_ServerEntry *a, const S_ServerEntry *b)
{
    if (a->maxPlayers != b->maxPlayers)
        return a->maxPlayers > b->maxPlayers;
    if (a->players != b->players)
        return a->players > b->players;
    return ServerPingGreater(a, b);
}

} // namespace

C_ServerRow::C_ServerRow(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextButton(scene, id, parent)
{
    m_index = 0;
}

bool C_ServerRow::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_TextButton::Create(frame, root, desc))
        return false;
    m_password = frame->FindChildFrame("xpassword", FIND_FRAMES);
    m_dedicated = frame->FindChildFrame("xdedicated", FIND_FRAMES);
    m_voice = frame->FindChildFrame("xvoice", FIND_FRAMES);
    m_activePassword = frame->FindChildFrame("xpassword01", FIND_FRAMES);
    m_activeDedicated = frame->FindChildFrame("xdedicated01", FIND_FRAMES);
    m_activeVoice = frame->FindChildFrame("xvoice01", FIND_FRAMES);
    if (!m_password || !m_dedicated || !m_voice || !m_activePassword || !m_activeDedicated || !m_activeVoice)
        return false;
    I3D_frame *const icons[] = {m_password, m_dedicated, m_voice, m_activePassword, m_activeDedicated, m_activeVoice};
    for (I3D_frame *icon : icons)
        icon->SetOn(false);
    for (I3D_frame *icon : icons)
        SetFrameId(icon, m_id);
    m_password->LinkTo(m_normalFrame, 1);
    m_dedicated->LinkTo(m_normalFrame, 1);
    m_voice->LinkTo(m_normalFrame, 1);
    m_activePassword->LinkTo(m_activeFrame, 1);
    m_activeDedicated->LinkTo(m_activeFrame, 1);
    m_activeVoice->LinkTo(m_activeFrame, 1);
    if (m_box) {
        m_box->SetUnknown90(0);
        m_box->AttachTo(m_scene, m_normalFrame);
    }
    return true;
}

bool C_ServerRow::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_TextButton::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_ADD_CURRENT_ENTRY:
        if (m_box)
            m_box->SetColour(ROW_CURRENT_COLOUR);
        return true;
    case CONTROL_MSG_ADD_ENTRY: {
        auto *entry = reinterpret_cast<S_ServerEntry *>(message->value2);
        m_index = static_cast<int>(reinterpret_cast<uintptr_t>(entry));
        // (The box checked: the fix, the original's without one crashes.)
        if (m_box)
            m_box->SetColour(0);
        if (!entry) {
            if (m_box)
                m_box->Clear();
            return true;
        }
        SetInactive(entry->versionMajor != g_versionMajor || entry->versionMinor != g_versionMinor);
        // The name as is, unless it is not text in the font.
        const auto *name = reinterpret_cast<const uint8_t *>(entry->name);
        const bool shown = g_gameProfile.UsesUnicodeFont() ? IsLegalUtf8Range(name, name + std::strlen(entry->name))
                                                           : !HasUtf8Sequences(entry->name);
        auto run = [this](uint32_t style, uintptr_t index, const C_str &text) {
            S_ControlMessage set = ValueMessage(CONTROL_MSG_SET_RUN_TEXT, style, text);
            set.value2 = index;
            HandleMessageCopy(set);
        };
        run(ROW_NAME_STYLE, 0, shown ? C_fstr("%s", entry->name) : g_texts.Get(TEXT_BAD_NAME));
        run(ROW_MAP_STYLE, 1, C_fstr("%s", entry->map));
        run(ROW_TYPE_STYLE, 2, g_texts.Get(GetGameTypeTextId(entry->gameType)));
        run(ROW_PING_STYLE, 3, C_fstr("%d", entry->ping));
        run(ROW_PLAYERS_STYLE, 4, C_fstr("%d / %d", entry->players, entry->maxPlayers));
        m_password->SetOn(entry->password);
        m_activePassword->SetOn(entry->password);
        m_dedicated->SetOn(entry->dedicated);
        m_activeDedicated->SetOn(entry->dedicated);
        m_voice->SetOn(entry->voice);
        m_activeVoice->SetOn(entry->voice);
        return true;
    }
    case CONTROL_MSG_REMOVE_ITEM:
        if (m_box)
            m_box->Clear();
        m_index = 0;
        return true;
    case CONTROL_MSG_GET_ITEM:
        message->value2 = static_cast<uintptr_t>(m_index);
        return true;
    default:
        C_TextButton::HandleMessage(message);
        return true;
    }
}

bool C_ServerList::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_ListGroup::Create(frame, root, desc);
    m_frame->SetData(nullptr);
    m_selectedEntry = nullptr;
    C_ServerRow *row = nullptr;
    for (int i = 1; i <= SERVER_ROWS; ++i) {
        const C_str name = C_fstr("Server%d", i);
        I3D_frame *rowFrame = frame->FindChildFrame(name.c_str(), FIND_DUMMIES);
        if (!rowFrame)
            return false;
        row = new C_ServerRow(m_scene, static_cast<uint32_t>(i & 0xFF) << 12 | m_id, this);
        row->Create(rowFrame, root, nullptr);
        S_ControlMessage clicked = ValueMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK);
        clicked.value2 = reinterpret_cast<uintptr_t>(this);
        clicked.value3 = reinterpret_cast<uintptr_t>(&C_ServerList::RowClicked);
        row->HandleMessageCopy(clicked);
        S_ControlMessage notified = ValueMessage(CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_13);
        notified.value2 = reinterpret_cast<uintptr_t>(this);
        notified.value3 = reinterpret_cast<uintptr_t>(&C_ServerList::RowNotified);
        row->HandleMessageCopy(notified);
        m_children.push_back(row);
    }
    m_visible = SERVER_ROWS;
    S_ControlMessage step = Message(CONTROL_MSG_SET_DOWN_STEP);
    step.value2 = SERVER_ROW_STEP;
    HandleMessageCopy(step);
    // From the last row's place.
    auto *origin = static_cast<S_vector *>(_aligned_malloc(16, 16));
    if (origin)
        *origin = row->GetFrame()->GetPos();
    S_ControlMessage place = Message(CONTROL_MSG_SET_ORIGIN);
    place.value2 = reinterpret_cast<uintptr_t>(origin);
    HandleMessageCopy(place);
    Layout(0);
    HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
    if (m_entries.capacity() < SERVER_RESERVE)
        m_entries.reserve(SERVER_RESERVE);
    return true;
}

int C_ServerList::ScrollRange() const
{
    return EntryCount() - m_visible + 1;
}

bool C_ServerList::RowClicked(void *object, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *list = static_cast<C_ServerList *>(object);
    list->m_selectedEntry = static_cast<C_ServerRow *>(control)->GetEntry();
    list->HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
    list->Notify(CONTROL_NOTIFY_0E, 0);
    return true;
}

bool C_ServerList::RowNotified(void *object, void *, C_Control *, uint32_t, uint32_t)
{
    static_cast<C_ServerList *>(object)->Notify(CONTROL_NOTIFY_13, 0);
    return true;
}

C_ServerList::T_Compare C_ServerList::Comparison() const
{
    const bool ascending = (m_sortKey & SORT_DIRECTION) == SORT_ASCENDING;
    switch (m_sortKey & SORT_FIELD) {
    case SORT_NAME:
        return ascending ? ServerNameLess : ServerNameGreater;
    case SORT_MAP:
        return ascending ? ServerMapLess : ServerMapGreater;
    case SORT_TYPE:
        return ascending ? ServerTypeLess : ServerTypeGreater;
    case SORT_PING:
        return ascending ? ServerPingLess : ServerPingGreater;
    case SORT_PLAYERS:
        return ascending ? ServerPlayersLess : ServerPlayersGreater;
    default:
        return nullptr;
    }
}

void C_ServerList::SortEntries(uint32_t key)
{
    if (key != 0) {
        const bool toggle = (key & SORT_DIRECTION) != 0;
        // (The direction toggled, the bits between dropped: the original's.)
        if (toggle)
            m_sortKey = (~m_sortKey & SORT_DIRECTION) | (m_sortKey & SORT_FIELD);
        if ((key & SORT_FIELD) == 0) {
            if (toggle)
                std::reverse(m_entries.begin(), m_entries.end());
            return;
        }
        m_sortKey = (m_sortKey & ~SORT_FIELD) | (key & SORT_FIELD);
    }
    if (T_Compare compare = Comparison())
        msvc6::Sort(m_entries.data(), m_entries.data() + m_entries.size(), compare);
}

bool C_ServerList::HandleMessage(S_ControlMessage *message)
{
    const int target = FindChild(message->target);
    if (IsChild(target))
        return m_children[target]->HandleMessage(message);
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    const auto count = static_cast<uint32_t>(m_entries.size());
    const auto visible = static_cast<uint32_t>(m_visible);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_PREVIOUS_ITEM:
    case CONTROL_MSG_NEXT_ITEM:
    case CONTROL_MSG_REMOVE_ITEM:
        return true;
    case CONTROL_MSG_GET_SELECTED:
        message->value2 = reinterpret_cast<uintptr_t>(m_selectedEntry);
        return true;
    case CONTROL_MSG_GET_ENTRY_COUNT:
        message->value = count;
        return true;
    case CONTROL_MSG_GET_SORT:
        message->value = m_sortKey;
        return true;
    case CONTROL_MSG_ADD_ITEM: {
        auto *entry = reinterpret_cast<S_ServerEntry *>(message->value2);
        if (!entry)
            return true;
        m_entries.push_back(entry);
        // The window kept on the entries (unsigned: the original's).
        const auto entries = static_cast<uint32_t>(m_entries.size());
        if (entries < visible) {
            m_firstEntry = 0;
            m_lastEntry = static_cast<int>(entries) - 1;
        } else if (static_cast<uint32_t>(m_firstEntry) > entries - visible) {
            m_lastEntry = static_cast<int>(entries) - 1;
            m_firstEntry = m_lastEntry - m_visible + 1;
        } else {
            m_lastEntry = m_firstEntry + m_visible - 1;
        }
        HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
        return true;
    }
    case CONTROL_MSG_CLEAR:
        m_selectedEntry = nullptr;
        m_entries.clear();
        HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
        return true;
    case CONTROL_MSG_SET_POSITION:
        if (count > visible) {
            m_firstEntry = static_cast<int>(message->value);
            m_lastEntry = m_visible + m_firstEntry - 1;
        }
        HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
        return true;
    case CONTROL_MSG_SORT:
        SortEntries(message->value);
        HandleMessageCopy(Message(CONTROL_MSG_REFRESH));
        return true;
    case CONTROL_MSG_REFRESH:
        // As many rows shown as there are entries.
        if (count == 0) {
            m_first = -1;
            m_last = -1;
        } else {
            m_first = 0;
            m_last = count < visible ? static_cast<int>(count) - 1 : m_visible - 1;
        }
        Scroll(SCROLL_NONE);
        for (int row = m_first, entry = m_firstEntry; row <= m_last && entry <= m_lastEntry; ++row, ++entry) {
            if (row < 0 || row > ItemCount() - 1 || entry < 0 || entry > EntryCount() - 1)
                continue;
            S_ControlMessage set = Message(CONTROL_MSG_ADD_ENTRY);
            set.value2 = reinterpret_cast<uintptr_t>(m_entries[entry]);
            m_children[row]->HandleMessageCopy(set);
            if (m_entries[entry] == m_selectedEntry)
                m_children[row]->HandleMessageCopy(Message(CONTROL_MSG_ADD_CURRENT_ENTRY));
        }
        if (m_value != 0) {
            PostToValue(CONTROL_MSG_SET_COUNT, static_cast<uint32_t>(ScrollRange()));
            if (ScrollRange() != 0)
                PostToValue(CONTROL_MSG_SET_POSITION, static_cast<uint32_t>(m_firstEntry));
        }
        Notify(CONTROL_NOTIFY_CHANGED, 0);
        return true;
    default:
        C_ListGroup::HandleMessage(message);
        return true;
    }
}

// ----- the hot key (type 0xd) and the titled edit box (type 0xe) -----

namespace {

constexpr uint32_t HOT_KEY_EDIT = 1, HOT_KEY_TITLE = 2;
constexpr uint32_t HOT_KEY_TITLE_STYLE = 0xC, HOT_KEY_KEY_STYLE = 0xD;
// The poll's argument (0x9A), Escape on the keyboard, the cursor's +0x44 after a binding.
constexpr int HOT_KEY_POLL = 0x9A;
constexpr uint16_t DEVICE_KEYBOARD = 1, SCAN_ESCAPE = 1;
constexpr int HOT_KEY_CURSOR_DELAY = 200;
// The titled edit box's text at first.
constexpr uint32_t TITLED_EDIT_STYLE = 0xD, TITLED_EDIT_TEXT = 0x7D2;

C_KeyBindings *KeyBindings()
{
    return g_gameProfile.GetPlayerProfile()->GetKeyBindings();
}

} // namespace

C_HotKeyControl::~C_HotKeyControl()
{
    delete m_edit;
    m_edit = nullptr;
    delete m_title;
    m_title = nullptr;
}

void C_HotKeyControl::Refresh()
{
    if (!m_shown)
        return;
    m_edit->Refresh();
    m_title->Refresh();
}

bool C_HotKeyControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return false;
    if (frame) {
        m_edit = new C_EditBox(m_scene, m_id | HOT_KEY_EDIT, this);
        m_edit->Create(frame, root, nullptr);
    }
    if (I3D_frame *text = frame->FindChildFrame("hktext*", FIND_FRAMES)) {
        m_title = new C_TitleControl(m_scene, m_id | HOT_KEY_TITLE, this);
        m_title->Create(text, root, nullptr);
        m_title->HandleMessageCopy(Message(CONTROL_MSG_TAKE_INPUT));
    }
    return true;
}

void C_HotKeyControl::Tick(int time)
{
    if (!m_edit->IsEditing())
        return;
    m_edit->Tick(time);
    // (A tick first: the click that started the edit not taken for the key.)
    if (!m_armed) {
        m_armed = true;
        return;
    }
    const C_Controls::S_Press *press = g_controls.PollPress(HOT_KEY_POLL, true);
    if (!press)
        return;
    if (press->device == DEVICE_KEYBOARD && press->key == SCAN_ESCAPE) {
        m_armed = false;
        m_edit->OnInput02(0);
        PostControlMessage(Message(CONTROL_MSG_HOT_KEY_DONE));
        return;
    }
    if (!KeyBindings()->Bind(m_binding, press->device, press->key, press->direction))
        return;
    m_armed = false;
    Notify(CONTROL_NOTIFY_CHANGED, 0);
    m_edit->OnInput02(0);
    PostControlMessage(Message(CONTROL_MSG_HOT_KEY_DONE));
    // (The cursor checked: the fix, the original's without one crashes.)
    if (C_Cursor *cursor = C_Cursor::Acquire(nullptr)) {
        cursor->SetDelay(HOT_KEY_CURSOR_DELAY);
        cursor->Release();
    }
}

bool C_HotKeyControl::Unknown20(int key)
{
    if (!m_edit->IsEditing())
        return false;
    // (Its low byte: the original's.)
    if (static_cast<uint8_t>(key) == SCAN_ESCAPE)
        m_edit->OnInput02(0);
    return true;
}

int C_HotKeyControl::OnEnter(int id)
{
    m_edit->OnEnter(id);
    m_title->OnEnter(id);
    return CONTROL_HANDLED;
}

int C_HotKeyControl::OnLeave(int id)
{
    if (m_edit->IsEditing())
        return 0;
    m_edit->OnLeave(id);
    m_title->OnLeave(id);
    return CONTROL_HANDLED;
}

int C_HotKeyControl::OnClick(int id)
{
    if (m_edit->IsEditing() || m_edit->OnClick(id) != CONTROL_HANDLED)
        return 0;
    // The menu's keys off while the key is waited for (value 1, the edit box's answer): no typed
    // character reaches the edit box (Tick's or OnInput02's, value 0, turns them on again).
    S_ControlMessage message = Message(CONTROL_MSG_HOT_KEY_DONE);
    message.value = 1;
    PostControlMessage(message);
    return CONTROL_HANDLED;
}

int C_HotKeyControl::OnInput02(int id)
{
    if (m_edit->OnInput02(id) != CONTROL_HANDLED)
        return 0;
    PostControlMessage(Message(CONTROL_MSG_HOT_KEY_DONE));
    return CONTROL_HANDLED;
}

void C_HotKeyControl::SetInactive(bool inactive)
{
    m_inactive = inactive;
    m_edit->SetInactive(inactive);
    m_title->SetInactive(inactive);
}

void C_HotKeyControl::ShowBinding()
{
    m_edit->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_TEXT, HOT_KEY_KEY_STYLE, KeyBindings()->GetBindingName(m_binding)));
}

bool C_HotKeyControl::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_GET_BINDING:
    case CONTROL_MSG_GET_BINDING2:
        message->value = static_cast<uint32_t>(m_binding);
        return true;
    case CONTROL_MSG_SET_BINDING: {
        m_binding = static_cast<int>(message->value);
        S_ControlMessage name = ValueMessage(CONTROL_MSG_SET_TEXT_ID, HOT_KEY_TITLE_STYLE);
        name.value2 = message->value2;
        m_title->HandleMessageCopy(name);
        ShowBinding();
        return true;
    }
    case CONTROL_MSG_REFRESH:
        m_title->HandleMessageCopy(*message);
        m_edit->HandleMessageCopy(*message);
        ShowBinding();
        return true;
    default:
        C_Control::HandleMessage(message);
        return true;
    }
}

void C_TitledEditBox::Tick(int time)
{
    if (m_edit->IsEditing())
        m_edit->Tick(time);
}

bool C_TitledEditBox::Unknown20(int key)
{
    return m_edit->Unknown20(key);
}

bool C_TitledEditBox::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_Control::Create(frame, root, desc))
        return false;
    if (frame) {
        m_edit = new C_EditBox(m_scene, m_id | HOT_KEY_EDIT, this);
        m_edit->Create(frame, root, nullptr);
        S_ControlMessage text = ValueMessage(CONTROL_MSG_SET_TEXT_ID, TITLED_EDIT_STYLE);
        text.value2 = TITLED_EDIT_TEXT;
        m_edit->HandleMessageCopy(text);
    }
    if (I3D_frame *title = frame->FindChildFrame("text*", FIND_FRAMES)) {
        m_title = new C_TitleControl(m_scene, m_id | HOT_KEY_TITLE, this);
        m_title->Create(title, root, nullptr);
        m_title->HandleMessageCopy(Message(CONTROL_MSG_TAKE_INPUT));
    }
    return true;
}

int C_TitledEditBox::OnLeave(int id)
{
    m_edit->OnLeave(id);
    m_title->OnLeave(id);
    return CONTROL_HANDLED;
}

int C_TitledEditBox::OnClick(int id)
{
    return m_edit->OnClick(id);
}

int C_TitledEditBox::OnInput02(int id)
{
    return m_edit->OnInput02(id);
}

int C_TitledEditBox::OnKeyClick(int id)
{
    return m_edit->OnKeyClick(id);
}

bool C_TitledEditBox::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_TEXT:
    case CONTROL_MSG_SET_TEXT_ID:
        m_title->HandleMessage(message);
        return true;
    case CONTROL_MSG_ADD_CALLBACK:
    case CONTROL_MSG_SET_MAX_LENGTH:
    case CONTROL_MSG_SET_DIGITS_ONLY:
    case CONTROL_MSG_SET_NUMBER:
    case CONTROL_MSG_GET_NUMBER:
    case CONTROL_MSG_SET_FORBIDDEN:
    case CONTROL_MSG_SET_RANGE:
    case CONTROL_MSG_GET_TEXT:
        m_edit->HandleMessage(message);
        return true;
    case CONTROL_MSG_REFRESH:
        m_title->HandleMessage(message);
        m_edit->HandleMessage(message);
        return true;
    default:
        C_Control::HandleMessage(message);
        return true;
    }
}

// ----- the item pictures (type 0x1d), the players' list (type 0x20), the check group (type 0x27) -----

namespace {

constexpr uint32_t PICTURES_COUNT_STYLE = 0x12;
// The players' list: its list's page step (to its bar, id | 4), the title's id, its columns.
constexpr uint32_t PLAYERS_BAR = 4, PLAYERS_TITLE = 2, PLAYERS_PAGE_STEP = 5;
constexpr uint32_t PLAYERS_NAME_STYLE = 0x23, PLAYERS_STAT1_STYLE = 0x24, PLAYERS_STAT3_STYLE = 0x25;
constexpr uint32_t TEXT_PLAYERS_NAME = 0xA33, TEXT_PLAYERS_STAT1 = 0xA35, TEXT_PLAYERS_STAT3 = 0xA37;

} // namespace

int C_ItemPictures::FindChild(uint32_t id) const
{
    switch (id & CHILD_ID_MASK) {
    case INFO_PICTURE:
        return 0;
    case INFO_SECOND:
        return 1;
    }
    return -1;
}

void C_ItemPictures::Refresh()
{
    if (!m_shown)
        return;
    // (The second checked: the fix, the original's without it crashes.)
    m_picture->Refresh();
    if (m_secondPicture)
        m_secondPicture->Refresh();
}

bool C_ItemPictures::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_ControlGroup::Create(frame, root, desc))
        return false;
    S_ControlMessage square = Message(CONTROL_MSG_FIND_FACE_GROUP);
    square.text = "X_SQUARE.BMP";
    if (frame) {
        m_picture = new C_PictureControl(m_scene, m_id | INFO_PICTURE, this);
        m_picture->Create(frame, root, nullptr);
        m_picture->HandleMessageCopy(Message(CONTROL_MSG_HIDE));
        m_picture->HandleMessageCopy(square);
        m_children.push_back(m_picture);
    }
    if (I3D_frame *ammo = frame->FindChildFrame("ammo*", FIND_FRAMES)) {
        m_secondPicture = new C_PictureControl(m_scene, m_id | INFO_SECOND, this);
        m_secondPicture->Create(ammo, root, nullptr);
        m_secondPicture->HandleMessageCopy(Message(CONTROL_MSG_HIDE));
        m_secondPicture->HandleMessageCopy(square);
        m_children.push_back(m_secondPicture);
    }
    return true;
}

void C_ItemPictures::ShowPicture(C_PictureControl *picture, int id, C_ItemDef *def)
{
    picture->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(id)));
    S_ControlMessage texture = Message(CONTROL_MSG_SET_TEXTURE);
    texture.value2 = reinterpret_cast<uintptr_t>(def->GetImage());
    picture->HandleMessageCopy(texture);
}

void C_ItemPictures::UpdateCounts()
{
    if (m_count > 0 || m_secondCount > 0)
        m_parent->HandleMessageCopy(ValueMessage(CONTROL_MSG_ITEM_COUNTED, m_id));
    // (The second checked: the fix, the original's without it crashes.)
    if (m_secondPicture) {
        if (m_secondCount < 0 || m_second < 0) {
            m_secondPicture->HandleMessageCopy(Message(CONTROL_MSG_HIDE));
        } else {
            m_secondPicture->HandleMessageCopy(Message(CONTROL_MSG_SHOW));
            m_secondPicture->HandleMessageCopy(Message(m_secondCount > 0 ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE));
        }
    }
    if (m_count < 0) {
        m_parent->HandleMessageCopy(ValueMessage(CONTROL_MSG_HIDE_ITEM, m_id));
        return;
    }
    m_picture->HandleMessageCopy(Message(m_count > 0 ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE));
    m_picture->HandleMessageCopy(ValueMessage(CONTROL_MSG_SET_TEXT, PICTURES_COUNT_STYLE, C_fstr("%d ", m_count)));
    if (m_secondPicture)
        m_secondPicture->HandleMessageCopy(
            ValueMessage(CONTROL_MSG_SET_TEXT, PICTURES_COUNT_STYLE, C_fstr("%d ", m_secondCount)));
}

bool C_ItemPictures::HandleMessage(S_ControlMessage *message)
{
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_ControlGroup::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SET_ITEM_COUNT:
        m_count = static_cast<int>(message->value);
        UpdateCounts();
        return true;
    case CONTROL_MSG_SET_SECOND_COUNT:
        m_secondCount = static_cast<int>(message->value);
        UpdateCounts();
        return true;
    case CONTROL_MSG_ADD_CALLBACK:
    case CONTROL_MSG_REMOVE_CALLBACK:
        m_picture->HandleMessage(message);
        if (m_secondPicture)
            m_secondPicture->HandleMessage(message);
        return true;
    case CONTROL_MSG_SET_ITEM: {
        m_item = static_cast<int>(message->value);
        C_ItemDef *def = GetItemDef(m_item);
        if (!def)
            return true;
        ShowPicture(m_picture, m_item, def);
        // The second picture: a weapon's ammo, a uniform's headgear.
        if (def->GetRecordType() == ITEM_ANIMATED)
            m_second = def->GetAmmoItemId();
        else if (def->GetBehaviour() == ITEM_BEHAVIOUR_UNIFORM)
            m_second = def->GetHeadgearItemId();
        else
            return true;
        C_ItemDef *second = GetItemDef(m_second);
        if (second && m_secondPicture)
            ShowPicture(m_secondPicture, m_second, second);
        return true;
    }
    case CONTROL_MSG_REFRESH:
        m_picture->HandleMessageCopy(*message);
        if (m_secondPicture)
            m_secondPicture->HandleMessageCopy(*message);
        return true;
    }
    C_ControlGroup::HandleMessage(message);
    return true;
}

int C_PlayerList::FindChild(uint32_t id) const
{
    const uint32_t part = id & CHILD_ID_MASK;
    if (part > 3)
        return 0;
    return part == PLAYERS_TITLE ? 2 : -1;
}

bool C_PlayerList::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    C_Control::Create(frame, root, desc);
    m_frame->SetData(nullptr);
    m_players = new C_ScrolledText(m_scene, m_id, this);
    m_players->Create(frame, root, nullptr);
    S_ControlMessage step = ValueMessage(CONTROL_MSG_SET_PAGE_STEP, PLAYERS_PAGE_STEP);
    step.target = m_id | PLAYERS_BAR;
    m_players->HandleMessageCopy(step);
    m_children.push_back(m_players);
    if (I3D_frame *header = frame->FindChildFrame("header*", FIND_FRAMES)) {
        m_title = new C_TitleControl(m_scene, m_id | PLAYERS_TITLE, this);
        m_title->Create(header, root, nullptr);
        m_children.push_back(m_title);
    }
    return true;
}

int C_PlayerList::OnEnter(int id)
{
    if (IsChild(m_focus) && m_children[m_focus]->OnLeave(id) == 0)
        return CONTROL_NOT_HANDLED;
    if (IsChild(FindChild(static_cast<uint32_t>(id))) &&
        m_children[FindChild(static_cast<uint32_t>(id))]->OnEnter(id) != 0) {
        m_focus = FindChild(static_cast<uint32_t>(id));
        return CONTROL_HANDLED;
    }
    return 0;
}

int C_PlayerList::OnLeave(int id)
{
    if (IsChild(m_focus)) {
        if (m_children[m_focus]->OnLeave(id) == 0)
            return 0;
        m_focus = -1;
    }
    return CONTROL_HANDLED;
}

int C_PlayerList::OnInput05(int id)
{
    if (!IsChild(FindChild(static_cast<uint32_t>(id))))
        return OnLeave(id);
    if (m_focus == FindChild(static_cast<uint32_t>(id)))
        return m_children[m_focus]->OnInput05(id);
    OnLeave(id);
    return OnEnter(id);
}

int C_PlayerList::OnPress(int id)
{
    if (IsChild(m_focus) && m_focus == FindChild(static_cast<uint32_t>(id)))
        return m_children[m_focus]->OnPress(id);
    return 0;
}

void C_PlayerList::SetInactive(bool inactive)
{
    m_players->SetInactive(inactive);
    m_title->SetInactive(inactive);
}

bool C_PlayerList::HandleMessage(S_ControlMessage *message)
{
    const int target = FindChild(message->target);
    if (IsChild(target))
        return m_children[target]->HandleMessage(message);
    if (message->id & CONTROL_NOTIFY_MASK) {
        C_Control::HandleMessage(message);
        return true;
    }
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_SHOW_PLAYERS: {
        S_NetPlayerState *players = g_netGame.LockPlayers();
        auto column = [this](uint32_t style, uint32_t textId) {
            S_ControlMessage name = ValueMessage(CONTROL_MSG_ADD_TEXT_ID, style);
            name.value2 = textId;
            m_title->HandleMessageCopy(name);
        };
        column(PLAYERS_NAME_STYLE, TEXT_PLAYERS_NAME);
        column(PLAYERS_STAT1_STYLE, TEXT_PLAYERS_STAT1);
        column(PLAYERS_STAT3_STYLE, TEXT_PLAYERS_STAT3);
        for (int i = 0; i < g_netGame.GetPlayerCount(); ++i) {
            const S_NetPlayerState &player = players[i];
            if (!player.IsConnected())
                continue;
            m_players->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, PLAYERS_NAME_STYLE, C_fstr("%s", player.name)));
            m_players->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, PLAYERS_STAT1_STYLE, C_fstr("%d", player.stats[1])));
            m_players->HandleMessageCopy(ValueMessage(CONTROL_MSG_ADD_TEXT, PLAYERS_STAT3_STYLE, C_fstr("%d", player.stats[3])));
        }
        g_netGame.UnlockPlayers();
        return true;
    }
    case CONTROL_MSG_IGNORED_7E:
    case CONTROL_MSG_IGNORED_7F:
        return true;
    default:
        C_Control::HandleMessage(message);
        return true;
    }
}

bool C_CheckGroup::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    if (!C_ControlGroup::Create(frame, root, desc))
        return false;
    I3D_frame *text = frame->FindChildFrame("text*", FIND_FRAMES);
    if (!text)
        return false;
    m_box = new C_HighlightCheckBox(m_scene, m_id, this);
    m_box->Create(text, root, nullptr);
    m_children.push_back(m_box);
    return true;
}

// ----- the movie control (type 0x22) -----

namespace {

// The interface's y (4:3) to the screen's (down).
constexpr float MOVIE_Y_SCALE = -1.3333334f;
constexpr int DRIVER_STATE_3 = 3, RENDER_STATE_0 = 0;

} // namespace

C_MovieControl::~C_MovieControl()
{
    CloseStream();
}

void C_MovieControl::CloseStream()
{
    if (!m_stream)
        return;
    m_stream->Close();
    m_stream->Release();
    m_stream = nullptr;
}

bool C_MovieControl::Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
#ifdef HD2_DEDICATED   // no streams in LS3DRF.dll: the server's copy returns false
    (void)frame;
    (void)root;
    (void)desc;
    return false;
#else
    m_stream = nullptr;
    if (!C_Control::Create(frame, root, desc))
        return false;
    m_frame->SetOn(false);
    m_stream = GetIGraph()->CreateStream();
    if (!m_stream)
        return false;
    FindArea();
    return true;
#endif
}

bool C_MovieControl::FindArea()
{
    if (!m_frame)
        return false;
    const I3D_bbox bound = static_cast<I3D_visual *>(m_frame)->GetWorldBound();
    const auto width = static_cast<float>(GetIGraph()->ScrnX());
    const auto height = static_cast<float>(GetIGraph()->ScrnY());
    m_left = bound.min.x * width;
    m_bottom = bound.min.y * height * MOVIE_Y_SCALE;
    m_right = width * bound.max.x;
    m_top = height * bound.max.y * MOVIE_Y_SCALE;
    m_width = m_right - m_left;
    m_height = m_bottom - m_top;
    return true;
}

void C_MovieControl::FitRect()
{
    S_showRect size = {};
    m_stream->GetVideoSize(&size);
    const auto videoWidth = static_cast<float>(size.width);
    const auto videoHeight = static_cast<float>(size.height);
    S_showRect rect = {};
    // The area at least as tall as the video's shape: its width; else its height (a NaN: the
    // height). (Truncated, as __ftol.)
    if (m_height / m_width >= videoHeight / videoWidth) {
        rect.x = static_cast<int>(m_left);
        rect.width = static_cast<int>(m_width);
        rect.height = static_cast<int>(m_width / videoWidth * videoHeight);
        rect.y = static_cast<int>(m_top);
    } else {
        rect.y = static_cast<int>(m_top);
        rect.height = static_cast<int>(m_height);
        rect.width = static_cast<int>(m_height / videoHeight * videoWidth);
        rect.x = static_cast<int>((m_width - static_cast<float>(rect.width)) * 0.5f + m_left);
    }
    m_stream->SetRect(&rect);
}

void C_MovieControl::Refresh()
{
    if (!m_stream->IsPlaying())
        return;
    m_stream->UpdateVideo(0);
    I3D_driver *driver = I3DGetDriver();
    const bool state3 = driver->GetState(DRIVER_STATE_3) != 0;
    driver->SetState(DRIVER_STATE_3, 0);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    const int renderState = GetIGraph()->GetRenderState(RENDER_STATE_0);
#endif
    m_stream->Draw(false);
#ifndef HD2_DEDICATED   // no rendering in LS3DRF.dll
    GetIGraph()->SetRenderState(RENDER_STATE_0, renderState);
#endif
    I3DGetDriver()->SetState(DRIVER_STATE_3, state3);
    const int state = m_stream->GetState();
    if (state == ISNDSTREAM_STATE_FINISHED || state == -1) {
        m_stream->Stop();
        FitRect();
        m_stream->Play();
    }
}

bool C_MovieControl::HandleMessage(S_ControlMessage *message)
{
#ifdef HD2_DEDICATED   // no streams in LS3DRF.dll: the server's copy returns true
    (void)message;
    return true;
#endif
    if (message->id & CONTROL_NOTIFY_MASK)
        return C_Control::HandleMessage(message);
    switch (message->id & CONTROL_MSG_MASK) {
    case CONTROL_MSG_CLOSE_MOVIE:
        m_stream->Stop();
        m_stream->Close();
        return true;
    case CONTROL_MSG_STOP_MOVIE:
        m_stream->Stop();
        return true;
    case CONTROL_MSG_PLAY_MOVIE: {
        const C_str name = C_fstr("GameMovie\\%s2", message->text.c_str());
        // (An open failing while another plays: that one kept; else played, the original's.)
        if (m_stream->Open(name.c_str(), 0) >= 0 || !m_stream->IsPlaying()) {
            FitRect();
            m_stream->SetVolume(0.0f);
            m_stream->Play();
        }
        return true;
    }
    default:
        C_Control::HandleMessage(message);
        return true;
    }
}
