// The interface's small controls (game/controls.cpp, part 9c-ii; the base: game/control.h): the
// text controls (a text box on the control's frame), the button, the text button, the picture, the
// tooltip, the frame switch, the group (its input passed to the child under the cursor) and the
// paged text control. Made by the factory (C_Control::CreateChild) by their descriptions' types.
#pragma once

#include "game/control.h"

class C_Cursor;
class C_PagedTextBox;
class C_TextBox;
class I3D_face_group;
class I3D_texture;

// The text controls' messages (their box's).
enum : uint32_t {
    CONTROL_MSG_SET_TEXT = 0x1000014,                   // the text, the style (value) as run 0
    CONTROL_MSG_SET_TEXT_ID = 0x1000015,                // the text id (value2), the style
    CONTROL_MSG_ADD_TEXT = 0x1000016,
    CONTROL_MSG_ADD_TEXT_ID = 0x1000017,
    CONTROL_MSG_SET_RUN_TEXT = 0x1000018,               // the text, the style, the run (value2)
    CONTROL_MSG_REMOVE_RUN = 0x1000068,                 // the run (value)
    CONTROL_MSG_CLEAR_TEXT = 0x1000077,                 // (and CONTROL_MSG_CLEAR)
    CONTROL_MSG_FACE_FRONT = 0x1000099,                 // the frame turned to (0, 0, 1), the box placed again
    CONTROL_MSG_SET_COLOUR = 0x10000A3,                 // every word's colour (0: their styles')
    CONTROL_MSG_CLEAR = 0x2000018,
    CONTROL_MSG_GET_RUNS = 0x2000030,                   // (the value set)
    CONTROL_MSG_SET_MARGIN = 0x2000073,                 // the box's margin (value), placed again
    CONTROL_MSG_WHITE_TEXT = 0x2000076,                 // the text buttons': value != 0, white; else the styles'
    // The picture's.
    CONTROL_MSG_SET_TEXTURE = 0x1000038,                // value2 (0: none, the control hidden; else shown)
    CONTROL_MSG_RESTORE_TEXTURE = 0x1000039,
    CONTROL_MSG_LOAD_TEXTURE = 0x100003A,               // the text, a file (not loaded: the original)
    CONTROL_MSG_FIND_FACE_GROUP = 0x100009A,            // the face group whose texture is named the text
    // The frame switch's.
    CONTROL_MSG_ADD_FRAME = 0x1000032,                  // a child frame named value2 (a char *), off
    CONTROL_MSG_SELECT_FRAME = 0x2000019,               // the frame `value` on, the selected one off
    CONTROL_MSG_HIDE_FRAME = 0x200001A,                 // the selected frame off
    CONTROL_MSG_IGNORED_25 = 0x2000025,                 // (nothing)
    // The paged text control's.
    CONTROL_MSG_SET_PAGE = 0x1000067,
    CONTROL_MSG_GET_PAGES = 0x100009C,
    CONTROL_MSG_ADD_AREA = 0x100009D,                   // the frame named the text (under the root)
    CONTROL_MSG_GET_PAGE_BREAK = 0x100009E,
    // A tooltip made (posted to target 0, value2 the tooltip).
    CONTROL_MSG_TOOLTIP = 0x200007A,
};

// A control with a text box on its frame (vtable 0x00813e58): the text messages its box's; placed
// on the frame on a refresh (the children not refreshed), dimmed while inactive.
class C_TextControl : public C_Control {                // 0x70 bytes
public:
    C_TextControl(I3D_scene *scene, uint32_t id, C_Control *parent);   // 0x0064f220
    ~C_TextControl() override;                          // 0x0064e3b0 (0x0064e3d0): the box deleted

    // The shown control's box drawn, then its children.
    void Refresh() override;                            // 0x00661fa0
    // The box made (false: not).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00661e50
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00661c60
    void SetInactive(bool inactive) override;           // 0x00661fe0

protected:
    // The box placed on the control's frame.
    void AttachBox();

    C_TextBox *m_box = nullptr;                         // +0x6C
};

// A title (type 0x13, vtable 0x00813cc0): its frame turned to (0, 0, -1), the box placed at once with
// a margin of 1; not taking input.
class C_TitleControl : public C_TextControl {           // 0x70 bytes
public:
    C_TitleControl(I3D_scene *scene, uint32_t id, C_Control *parent);
    ~C_TitleControl() override = default;               // 0x0064e260 (0x0064e280)
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065c9a0
};

// A label (type 0x12, vtable 0x00814188): its frame turned and the box placed (its margin kept); its
// children not drawn.
class C_LabelControl : public C_TextControl {           // 0x70 bytes
public:
    C_LabelControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_LabelControl() override = default;               // 0x00657980 (0x006579a0)
    // The shown control's box drawn (no children).
    void Refresh() override;                            // 0x0065c6c0
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065c6e0
    // (C_TextControl's, ADD_TEXT and CLEAR handled first.)
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065c770
};

// A picture (type 4, vtable 0x00813ff0): a text control whose frame's visual's first face group's
// texture can be changed (by the messages: given, loaded, the original back), or another face group
// taken by its texture's name; faded while inactive.
class C_PictureControl : public C_TextControl {         // 0x78 bytes
public:
    C_PictureControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_PictureControl() override = default;             // 0x006513d0 (0x006513f0)
    // The face group and its texture taken; the box placed with a margin of 1.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00650a50
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00650b00
    // The visual's alpha 0.4 (inactive) or 1 (the box not dimmed).
    void SetInactive(bool inactive) override;           // 0x00650ad0

protected:
    I3D_texture *m_texture = nullptr;                   // +0x70 the face group's own
    I3D_face_group *m_faceGroup = nullptr;              // +0x74
};

// A group (type 5, vtable 0x00814430; the base of the scrolled text, the list and others): the
// cursor's input passed to the child it is over (m_focus, by the input's id), messages to the child
// their target names.
class C_ControlGroup : public C_Control {               // 0x70 bytes
public:
    C_ControlGroup(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_ControlGroup() override = default;               // 0x00658f00 (0x00658f20)

    // (0x00657150: the base's.)
    void Tick(int time) override;                       // 0x00656e10
    bool Unknown20(int value) override;                 // 0x00656dc0
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00657180
    // The child with the id becomes the focus (it takes input) and gets the enter (id 0xF: Unknown20(0xF)).
    int OnEnter(int id) override;                       // 0x00656e40
    // The focus' leave (handled: no focus).
    int OnLeave(int id) override;                       // 0x00656eb0
    int OnClick(int id) override;                       // 0x00656ef0
    int OnInput02(int id) override;                     // 0x00656f20
    // Over another child: the leave, then (left) the enter; over the focus: its; over none: the leave.
    int OnInput05(int id) override;                     // 0x00656f50
    // The focus' when it is the child with the id.
    int OnRelease(int id) override;                     // 0x00656ff0
    int OnPress(int id) override;                       // 0x00656fc0
    int OnWheel(int id, int amount) override;           // 0x006570c0
    int OnInput08(int id) override;                     // 0x00657030
    int OnInput09(int id) override;                     // 0x00657060
    int OnInput0A(int id) override;                     // 0x00657090
    int OnKeyClick(int id) override;                    // 0x006570f0
    int OnInput13(int id) override;                     // 0x00657120
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006571a0

protected:
    // The focus (the child under the cursor), or null.
    C_Control *Focus() const { return IsChild(m_focus) ? m_children[m_focus] : nullptr; }

    int m_focus = -1;                                   // +0x6C
};

// A frame switch (type 0xc, vtable 0x00815310): child frames of its frame added by name (off), one
// of them on at a time; its children's indices are the frames'. Not taking input.
class C_FrameSwitch : public C_Control {                // 0x80 bytes
public:
    C_FrameSwitch(I3D_scene *scene, uint32_t id, C_Control *parent);
    ~C_FrameSwitch() override = default;                // 0x0065e440 (0x0065e460)
    // An index of the frames.
    bool IsChild(int index) const override;             // 0x0065e410
    // (0x00650d20: the base's.)
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00650d30

protected:
    std::vector<I3D_frame *> m_frames;                  // +0x6C
    int m_selected = -1;                                // +0x7C
};

// A text button (type 0x14, vtable 0x00814cb0): a text control on its "normal*" frame, its
// "actived*" one shown while the cursor is over it.
class C_TextButton : public C_TextControl {             // 0x78 bytes
public:
    C_TextButton(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_TextButton() override = default;                 // 0x00660050 (0x00660070)
    I3D_frame *GetHighlightFrame() const override { return m_activeFrame; }   // 0x00655630
    // The frames found (false: not both), the box on the normal one.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00655400
    // The active frame (not while inactive: not handled).
    int OnEnter(int id) override;                       // 0x00655570
    int OnLeave(int id) override;                       // 0x006555c0
    // Its notification unless inactive (handled; inactive: not, 0).
    int OnClick(int id) override;                       // 0x00655610
    // REFRESH: the box on the normal frame (the children not refreshed); WHITE_TEXT.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006554f0

protected:
    I3D_frame *m_normalFrame = nullptr;                 // +0x70
    I3D_frame *m_activeFrame = nullptr;                 // +0x74
};

// A tooltip (type 0x15, vtable 0x008148f8): announced to the screen when made (CONTROL_MSG_TOOLTIP);
// SHOW only arms it: it shows by the cursor once the cursor has stayed 500 ms, and hides when it
// moves or a button is held. Not taking input.
class C_Tooltip : public C_TextControl {                // 0x78 bytes
public:
    C_Tooltip(I3D_scene *scene, uint32_t id, C_Control *parent);
    ~C_Tooltip() override;                              // 0x0065f8d0 (0x0065f8f0): the cursor released

    void Tick(int time) override;                       // 0x00651690
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00651540
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00651640

protected:
    bool m_armed = false;                               // +0x70
    C_Cursor *m_cursor = nullptr;                       // +0x74
};

// A button (type 2, vtable 0x00813f68): its "normal*", "actived*" (the cursor over it) and
// "inactived*" frames; held, its press repeated every frame once the cursor has stayed 300 ms.
class C_Button : public C_Control {                     // 0x84 bytes
public:
    C_Button(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_Button() override;                               // 0x0064f710 (0x0064f730): the cursor released

    // The repeated press while held.
    void Tick(int time) override;                       // 0x006645d0
    // The cursor taken; the three frames (false: the frame has not 3 children).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00664300
    int OnEnter(int id) override;                       // 0x00664460
    int OnLeave(int id) override;                       // 0x006644e0
    int OnRelease(int id) override;                     // 0x006645c0
    int OnPress(int id) override;                       // 0x00664590
    // Its notification only (no click).
    int OnKeyClick(int id) override;                    // 0x006644c0
    void SetInactive(bool inactive) override;           // 0x00664540

protected:
    // The three frames updated (normal, active, inactive).
    void UpdateFrames();

    C_Cursor *m_cursor = nullptr;                       // +0x6C
    I3D_frame *m_normalFrame = nullptr;                 // +0x70
    I3D_frame *m_activeFrame = nullptr;                 // +0x74
    I3D_frame *m_inactiveFrame = nullptr;               // +0x78
    bool m_held = false;                                // +0x7C
    int m_repeatTime = 0;                               // +0x80 ms of the cursor's still time (-1: every frame)
};

// A control with a paged text box (vtable 0x00815420; the base of type 0x25): C_TextControl's text
// messages; a refresh reattaches (nothing: the plain box's Reattach).
class C_PagedTextControl : public C_Control {           // 0x70 bytes
public:
    C_PagedTextControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_PagedTextControl() override;                     // 0x00660030 (0x0065fef0): the box deleted

    void Refresh() override;                            // 0x00662450
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00662160
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00662000
    void SetInactive(bool inactive) override;           // 0x00662490

protected:
    C_PagedTextBox *m_box = nullptr;                    // +0x6C
};

// The paged text (type 0x25, vtable 0x00814d38): areas added by their frames' names, a page shown,
// laid out again on a refresh.
class C_PagesControl : public C_PagedTextControl {      // 0x70 bytes
public:
    C_PagesControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_PagedTextControl(scene, id, parent) {}
    ~C_PagesControl() override = default;               // 0x0065fd80 (0x0065fda0)
    // The shown control's box drawn (no children).
    void Refresh() override;                            // 0x0065c7d0
    // (0x0065c7f0: C_PagedTextControl's.)
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065c800
};
