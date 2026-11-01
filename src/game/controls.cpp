#include "game/controls.h"

#include <cstring>

#include "game/cursor.h"
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
    S_ControlMessage message = Message(CONTROL_MSG_TOOLTIP);
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
