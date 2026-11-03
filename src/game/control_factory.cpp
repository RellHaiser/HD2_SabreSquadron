// The controls' factory (part 9c-vi-b): C_Control::CreateChild, a control of the description's type
// made on the child frame of its name. Apart from the base (game/control.cpp) so a harness may fake it.
#include "game/control.h"

#include "game/controls.h"
#include "ls3d/i3d.h"

namespace {

// The child frame's search: a visual or a dummy, the name a mask.
constexpr uint32_t FIND_CONTROL_FRAME = ENUMF_WILDMASK | FRAME_MASK_VISUAL | FRAME_MASK_DUMMY;

// The control of a description's type (made with the parent's scene, the description's id; an
// unknown type: none). The original's constructors inlined, the members' defaults its.
C_Control *MakeControl(int type, I3D_scene *scene, uint32_t id, C_Control *parent)
{
    switch (type) {
    case 2: return new C_Button(scene, id, parent);
    case 3: return new C_EditBox(scene, id, parent);   // (0x1b's)
    case 4: return new C_PictureControl(scene, id, parent);
    case 5: return new C_ControlGroup(scene, id, parent);
    case 6: return new C_ListItem(scene, id, parent);
    case 7: return new C_Slider(scene, id, parent);
    case 8: return new C_ScrollBar(scene, id, parent);
    case 9: return new C_CheckBox(scene, id, parent);
    case 0xa: return new C_ToggleButton(scene, id, parent);
    case 0xb: return new C_ListGroup(scene, id, parent);
    case 0xc: return new C_FrameSwitch(scene, id, parent);
    case 0xd: return new C_HotKeyControl(scene, id, parent);
    case 0xe: return new C_TitledEditBox(scene, id, parent);
    case 0xf: return new C_ScrolledText(scene, id, parent);
    case 0x10: return new C_SpinControl(scene, id, parent);
    case 0x11: return new C_ComboBox(scene, id, parent);
    case 0x12: return new C_LabelControl(scene, id, parent);
    case 0x13: return new C_TitleControl(scene, id, parent);
    case 0x14: return new C_TextButton(scene, id, parent);
    case 0x15: return new C_Tooltip(scene, id, parent);
    case 0x16: return new C_BarControl(scene, id, parent);
    case 0x17: return new C_UpgradeBar(scene, id, parent);
    case 0x18: return new C_ServerList(scene, id, parent);
    case 0x19: return new C_SkillControl(scene, id, parent);
    case 0x1a: return new C_SelectList(scene, id, parent);
    case 0x1b: return new C_EditBox(scene, id, parent);
    case 0x1c: return new C_ItemInfo(scene, id, parent);
    case 0x1d: return new C_ItemPictures(scene, id, parent);
    case 0x1e: return new C_SideIndicator(scene, id, parent);
    case 0x1f: return new C_MarkBox(scene, id, parent);
    case 0x20: return new C_PlayerList(scene, id, parent);
    case 0x21: return new C_ChatInput(scene, id, parent);
    case 0x22: return new C_MovieControl(scene, id, parent);
    case 0x23: return new C_HidingList(scene, id, parent);
    case 0x24: return new C_HighlightCheckBox(scene, id, parent);
    case 0x25: return new C_PagesControl(scene, id, parent);
    case 0x26: return new C_PoolList(scene, id, parent);
    case 0x27: return new C_CheckGroup(scene, id, parent);
    case 0x28: return new C_TitledComboBox(scene, id, parent);
    default: return nullptr;
    }
}

}  // namespace

bool C_Control::CreateChild(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc)
{
    // (Either failure's message was printed into a local buffer and dropped.)
    I3D_frame *childFrame = frame->FindChildFrame(desc->name.c_str(), FIND_CONTROL_FRAME);
    if (!childFrame)
        return false;   // "ERROR : cannot find frame : %s !!"
    C_Control *control = MakeControl(desc->type, m_scene, desc->id, this);
    if (!control)
        return false;   // "ERROR : cannot create item : %d !!"
    // Not made on its frame: deleted, still true.
    if (!control->Create(childFrame, root, desc)) {
        delete control;
        return true;
    }
    control->SetStrings(desc->strings);
    // (The original passed the description's own messages.)
    control->HandleMessages(const_cast<std::vector<S_ControlMessage> &>(desc->messages));
    m_children.push_back(control);
    return true;
}
