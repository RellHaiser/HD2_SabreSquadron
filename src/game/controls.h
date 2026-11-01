// The interface's small controls (game/controls.cpp, part 9c-ii; the base: game/control.h): the
// text controls (a text box on the control's frame), the button, the text button, the picture, the
// tooltip, the frame switch, the group (its input passed to the child under the cursor) and the
// paged text control; part 9c-iii-a: the slider, the scroll bar and the spin control; part 9c-iii-b:
// the check boxes, the toggle buttons, the side indicator and the mark; part 9c-iv: the edit
// controls, the scrolled text, the combo boxes and the chat input. Made by the factory
// (C_Control::CreateChild) by their descriptions' types.
#pragma once

#include "game/control.h"

class C_Cursor;
class C_PagedTextBox;
class C_TextBox;
struct S_vector;
class I3D_face_group;
class I3D_texture;

// The mark's click (value: its value).
constexpr uint32_t CONTROL_NOTIFY_0E = 0x400E000;
// The edit controls' Enter and Escape.
constexpr uint32_t CONTROL_NOTIFY_RETURN = 0x4016000;
constexpr uint32_t CONTROL_NOTIFY_ESCAPE = 0x4017000;
// The chat input's line sent (value: its text's address, valid during the notification).
constexpr uint32_t CONTROL_NOTIFY_SEND = 0x4015000;

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
    // The slider's (value2 a float, 0 .. 100).
    CONTROL_MSG_SET_SLIDER = 0x1000021,
    CONTROL_MSG_GET_SLIDER = 0x1000022,                 // (value2 set; the scroll bar: its slider's)
    // The scroll bar's.
    CONTROL_MSG_SET_COUNT = 0x1000020,                  // positions (value; < 2: none, inactive)
    CONTROL_MSG_SCROLL_UP = 0x1000023,                  // a page step (its arrows' press)
    CONTROL_MSG_SCROLL_DOWN = 0x1000024,
    CONTROL_MSG_SLIDER_MOVED = 0x1000025,               // (its slider's drag) the position from the slider
    CONTROL_MSG_SET_PAGE_STEP = 0x1000080,              // (0: kept)
    CONTROL_MSG_GET_POSITION = 0x100008F,
    CONTROL_MSG_SET_POSITION = 0x200002B,               // (sent to the control its value names)
    // The spin control's.
    CONTROL_MSG_ADD_ITEM = 0x1000067,                   // the text, or the text id (value)
    CONTROL_MSG_REMOVE_ITEM = 0x1000068,
    CONTROL_MSG_GET_SELECTED = 0x1000069,               // its index (value) and text (also 0x100006C)
    CONTROL_MSG_GET_ITEM = 0x100006A,                   // the text of the item `value` (none: value -1)
    CONTROL_MSG_GET_SELECTED_2 = 0x100006C,
    CONTROL_MSG_SELECT_ITEM = 0x100006D,
    CONTROL_MSG_CLEAR_ITEMS = 0x2000065,                // (inactive)
    CONTROL_MSG_PREVIOUS_ITEM = 0x200001B,              // (its arrows' press; around)
    CONTROL_MSG_NEXT_ITEM = 0x200001C,
    // The check boxes' and the toggle buttons' (value: checked; also the mark's).
    CONTROL_MSG_GET_CHECKED = 0x100006C,
    CONTROL_MSG_SET_CHECKED = 0x100006D,                // (changed: CONTROL_NOTIFY_CHANGED)
    CONTROL_MSG_SET_CHECKED_QUIET = 0x1000076,          // (the check boxes': no notification)
    // The side indicator's (value: 0 axis, 1 allied, else none).
    CONTROL_MSG_SET_SIDE = 0x1000097,
    CONTROL_MSG_GET_SIDE = 0x1000098,
    // The mark's: its title shows the value, two digits.
    CONTROL_MSG_SET_NUMBER = 0x1000078,
    // The edit controls': the characters refused (value2 a char *), the length (value; 0: kept),
    // digits only (value), the editing asked for (to the screen, START_EDITING) or stopped
    // (STOP_EDITING; value2 the control), the text got.
    CONTROL_MSG_SET_FORBIDDEN = 0x100009B,
    CONTROL_MSG_SET_MAX_LENGTH = 0x100006E,
    CONTROL_MSG_SET_DIGITS_ONLY = 0x100006F,
    CONTROL_MSG_IGNORED_74 = 0x1000074,                 // (nothing)
    CONTROL_MSG_EDIT = 0x2000016,                       // (the edit control's click)
    CONTROL_MSG_END_EDIT = 0x20000AB,                   // (while editing: OnInput02)
    CONTROL_MSG_START_EDITING = 0x20000BF,
    CONTROL_MSG_STOP_EDITING = 0x20000C0,
    CONTROL_MSG_GET_TEXT = 0x200002F,
    // The edit box's: a range (value .. value2; the same: none) its number is kept in on the
    // editing's end; the number got (value2; digits only).
    CONTROL_MSG_SET_RANGE = 0x10000A0,
    CONTROL_MSG_GET_NUMBER = 0x1000079,
    // The selection list's: a line put first (selected), the selected one removed, a line selected
    // by its text (no case; value: its index, else -1).
    CONTROL_MSG_INSERT_FIRST = 0x2000015,
    CONTROL_MSG_REMOVE_SELECTED = 0x2000017,
    CONTROL_MSG_SELECT_TEXT = 0x2000076,
    // The combo box's: a line picked by its index (value), the header's text (the text, the text id:
    // value2), the list shown or hidden, its selected line picked (CONTROL_NOTIFY_0E).
    CONTROL_MSG_PICK_INDEX = 0x1000076,
    CONTROL_MSG_SET_HEADER_TEXT = 0x100007A,
    CONTROL_MSG_SET_HEADER_TEXT_ID = 0x1000081,
    CONTROL_MSG_TOGGLE_LIST = 0x200002D,
    CONTROL_MSG_PICK_SELECTED = 0x200002E,
    // The chat input's: its team toggle changed (the toggle's route).
    CONTROL_MSG_TEAM_TOGGLED = 0x200000B,
    // A control to be ticked (posted to target 0 when made, value2 the control: the tooltip, the
    // scroll bar and its arrows).
    CONTROL_MSG_ADD_TICKER = 0x200007A,
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
    // The scrolled text's, the combo box's enter (0x00658550, 0x00659a30): id 0xF Unknown20 (true:
    // not handled), else the focus' leave (not taken: not handled); the child with the id's enter
    // (taken: the focus, handled; else 0).
    int EnterChild(int id);
    // The focus' Unknown20 (the scrolled text's, the chat input's; none: false).
    bool FocusUnknown20(int value);

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

// A tooltip (type 0x15, vtable 0x008148f8): announced to the screen when made (CONTROL_MSG_ADD_TICKER);
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

// A slider (type 7, vtable 0x00815288; the scroll bar's): its "normal*" frame (its "actived*" one
// while the cursor is over it) moved by the cursor along its "sliderbase*" frame (hidden), across
// or up and down, whichever way the base is longer; its value 0 .. 100.
class C_Slider : public C_Control {                     // 0xA0 bytes
public:
    C_Slider(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_Slider() override;                               // 0x0065e600 (0x0065e620): the cursor released

    // The cursor taken, the frames found (false: one not), the limits set, the value 50.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00664680
    int OnEnter(int id) override;                       // 0x006647c0
    int OnLeave(int id) override;                       // 0x00664810
    // The cursor's drag: moved with it (CONTROL_NOTIFY_08 when its value changed).
    int OnInput08(int id) override;                     // 0x00664860
    // SET_SLIDER / GET_SLIDER.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00664ac0
    void SetInactive(bool inactive) override;           // 0x00664630

protected:
    // The limits from the frames' bounds (in the base by the slider's half size), the direction.
    void SetLimits();                                   // 0x00664890
    // The frame at the cursor's place along the base (when the cursor moved that way): its value
    // changed (true).
    bool Drag();                                        // 0x00664b30
    void MoveTo(const S_vector &position);              // 0x00664b10
    // The value from the place along the base (changed: true).
    bool SetValueFrom(float place);                     // 0x00664ca0
    // The value (clamped) and the frame there.
    void SetValue(float value);                         // 0x00664d10

    C_Cursor *m_cursor = nullptr;                       // +0x6C
    I3D_frame *m_normalFrame = nullptr;                 // +0x70
    I3D_frame *m_activeFrame = nullptr;                 // +0x74
    I3D_frame *m_baseFrame = nullptr;                   // +0x78
    // The frame's place's limits (from the normal frame's place) and their ranges.
    float m_minX = 0.0f;                                // +0x7C
    float m_minY = 0.0f;                                // +0x80
    float m_maxX = 0.0f;                                // +0x84
    float m_maxY = 0.0f;                                // +0x88
    float m_rangeX = 0.0f;                              // +0x8C
    float m_rangeY = 0.0f;                              // +0x90
    bool m_horizontal = false;                          // +0x98
    bool m_vertical = false;                            // +0x99
    float m_value = 0.0f;                               // +0x9C
};

// A scroll bar (type 8, vtable 0x00814210): a position among `count`, its "barrow up*" / "barrow
// down*" arrows (buttons) and its "slider*" (a slider) its children; held on its frame, it steps the
// position toward the cursor every 50 ms. Its position sent to the control its value names.
class C_ScrollBar : public C_Control {                  // 0x94 bytes
public:
    C_ScrollBar(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_ScrollBar() override = default;                  // 0x00658070 (0x00658090)

    // The index of the child whose id's last 4 bits are `id`'s.
    int FindChild(uint32_t id) const override;          // 0x00664e70
    // While held: a step toward the cursor.
    void Tick(int time) override;                       // 0x00665ce0
    // The children made (each optional; posted to be ticked, the bar too); inactive.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00664eb0
    // The focus' leave (not handled: not), the child with the id's enter.
    int OnEnter(int id) override;                       // 0x00665750
    int OnLeave(int id) override;                       // 0x006657d0
    int OnInput05(int id) override;                     // 0x00665820
    // The focus', else held (a step toward the cursor).
    int OnPress(int id) override;                       // 0x006659f0
    // A page up or down per 120.
    int OnWheel(int id, int amount) override;           // 0x00665be0
    int OnInput08(int id) override;                     // 0x00665a40
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00665330
    // Not made active without positions or under an inactive parent.
    void SetInactive(bool inactive) override;           // 0x00665a80

protected:
    // The focus (the child under the cursor), or null.
    C_Control *Focus() const { return IsChild(m_focus) ? m_children[m_focus] : nullptr; }
    // The position sent to the control the value names (SET_POSITION).
    void SendPosition();                                // 0x006656c0
    // A step toward the cursor's place on the frame (a visual's world bound).
    void StepToCursor();                                // 0x00665890
    // The arrows inactive at the ends, or the slider at the position.
    void Update();                                      // 0x00665ae0

    int m_repeatTime = 0;                               // +0x6C ms to the next step while held
    bool m_held = false;                                // +0x70
    C_Button *m_upButton = nullptr;                     // +0x74
    C_Button *m_downButton = nullptr;                   // +0x78
    C_Slider *m_slider = nullptr;                       // +0x7C
    int m_count = 0;                                    // +0x80
    int m_position = 0;                                 // +0x84
    float m_step = 0.0f;                                // +0x88 the slider's value per position
    int m_focus = -1;                                   // +0x8C
    int m_pageStep = 1;                                 // +0x90
};

// A spin control (type 0x10, vtable 0x00815398): a text control showing one of its items, its
// "barrow up*" / "barrow down*" arrows (buttons, its children) choosing the previous or the next.
class C_SpinControl : public C_TextControl {            // 0x94 bytes
public:
    C_SpinControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_SpinControl() override = default;                // 0x006605c0 (0x006605e0)

    // The index of the child whose id's last 4 bits are `id`'s.
    int FindChild(uint32_t id) const override;          // 0x0064f4a0
    // The arrows made (each optional), the box placed; inactive (false: no box).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0064f4e0
    // The box's hover; the input to the arrow with the id.
    int OnEnter(int id) override;                       // 0x0064fc80
    int OnLeave(int id) override;                       // 0x0064fcd0
    int OnClick(int) override { return CONTROL_HANDLED; }   // 0x0064fd20
    int OnInput05(int id) override;                     // 0x0064fc20
    int OnPress(int id) override;                       // 0x0064fd30
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0064f870
    // The box dimmed, the arrows inactive.
    void SetInactive(bool inactive) override;           // 0x0064fd70

protected:
    C_Control *Focus() const { return IsChild(m_focus) ? m_children[m_focus] : nullptr; }
    // The selected item shown, then CONTROL_NOTIFY_CHANGED.
    void ShowSelected();

    C_Button *m_upButton = nullptr;                     // +0x70
    C_Button *m_downButton = nullptr;                   // +0x74
    std::vector<C_str> m_items;                         // +0x78
    int m_selected = -1;                                // +0x8C
    int m_focus = -1;                                   // +0x90
};

// A check box (type 9, vtable 0x00815200): a text control with "yes_full*" / "yes_empty*" /
// "no_full*" / "no_empty*" frames, checked (yes full, no empty) or not; a click toggles it.
class C_CheckBox : public C_TextControl {               // 0x84 bytes
public:
    C_CheckBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_CheckBox() override = default;                   // 0x00660310 (0x00660330)

    // The frames (false: not 4 children, one not found, no box), checked; the box placed.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00654120
    // The box's hover (inactive: not handled).
    int OnEnter(int id) override;                       // 0x00654060
    // (Its notification CONTROL_NOTIFY_ENTER too: the original's.)
    int OnLeave(int id) override;                       // 0x006540a0
    int OnClick(int id) override;                       // 0x00654110
    // The id's last bits: 1 (a no frame) unchecks, 2 (a yes frame) checks.
    int OnKeyClick(int id) override;                    // 0x006540d0
    // GET_CHECKED, SET_CHECKED, SET_CHECKED_QUIET.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00654420

protected:
    // Checked or not, then CONTROL_NOTIFY_CHANGED (the value: checked).
    void Toggle();                                      // 0x00654300
    void Check();                                       // 0x00654340
    void Uncheck();                                     // 0x006543b0

    I3D_frame *m_yesFull = nullptr;                     // +0x70
    I3D_frame *m_yesEmpty = nullptr;                    // +0x74
    I3D_frame *m_noFull = nullptr;                      // +0x78
    I3D_frame *m_noEmpty = nullptr;                     // +0x7C
    bool m_checked = true;                              // +0x80
};

// A toggle button (type 0xa, vtable 0x00814c28): down or up, its "normaldown*" / "normalup*"
// frame, the "actived" one while the cursor is over it; a click toggles it.
class C_ToggleButton : public C_Control {               // 0x84 bytes
public:
    C_ToggleButton(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_ToggleButton() override = default;               // 0x0065f610 (0x0065f630)

    // The frames (false: not 4 children, one not found), down.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00654f80
    int OnEnter(int id) override;                       // 0x00655320
    int OnLeave(int id) override;                       // 0x00655390
    int OnClick(int id) override;                       // 0x00655300
    int OnPress(int) override { return CONTROL_HANDLED; }   // 0x00655310
    // GET_CHECKED (down), SET_CHECKED.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006551a0

protected:
    // Create's, with the frames' names.
    bool CreateFrames(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc, const char *activeDown,
                      const char *normalDown, const char *activeUp, const char *normalUp);
    // Up or down, then CONTROL_NOTIFY_CHANGED (0).
    void Toggle();                                      // 0x00655200

    I3D_frame *m_activeDown = nullptr;                  // +0x6C
    I3D_frame *m_normalDown = nullptr;                  // +0x70
    I3D_frame *m_activeUp = nullptr;                    // +0x74
    I3D_frame *m_normalUp = nullptr;                    // +0x78
    int m_unknown7C = -1;                               // +0x7C (not read)
    bool m_down = true;                                 // +0x80
    bool m_over = false;                                // +0x81 the cursor over it
};

// The "all" toggle button (vtable 0x00814100; made by the control of vtable 0x008145c8): its
// frames "allactived" / "allnormal" (down) and "xallactived" / "xallnormal" (up).
class C_AllToggleButton : public C_ToggleButton {       // 0x84 bytes
public:
    C_AllToggleButton(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ToggleButton(scene, id, parent) {}
    ~C_AllToggleButton() override = default;            // 0x00657830
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00654d60
};

// A half of the highlighted check box (vtable 0x00814078): empty or full, its "actived" frame
// while the cursor is over it (the active empty frame shared by both halves).
class C_CheckBoxHalf : public C_Control {               // 0x80 bytes
public:
    C_CheckBoxHalf(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_CheckBoxHalf() override = default;               // 0x006548c0 (0x006548e0)

    // Not while inactive or already over it.
    int OnEnter(int id) override;                       // 0x00663f90
    int OnLeave(int id) override;                       // 0x00664070
    // While over it: the other state's active frame shown (the state kept).
    int OnKeyClick(int id) override;                    // 0x006640f0
    // The normal frames' alpha 0.4 (inactive) or 1.
    void SetInactive(bool inactive) override;           // 0x00663e20

    // Its frames, the empty one its frame (on); true.
    bool Setup(I3D_frame *empty, I3D_frame *full, I3D_frame *activeFull, I3D_frame *activeEmpty);   // 0x00663e80
    // Full or empty (the frames shown).
    void SetFull(bool full);                            // 0x00664200
    // The frames linked to `parent` (null: unlinked).
    void LinkFrames(I3D_frame *parent);                 // 0x006641c0

protected:
    // The active empty frame on at the empty one's place.
    void ShowActiveEmpty();

    I3D_frame *m_emptyFrame = nullptr;                  // +0x6C
    I3D_frame *m_fullFrame = nullptr;                   // +0x70
    I3D_frame *m_activeFullFrame = nullptr;             // +0x74
    I3D_frame *m_activeEmptyFrame = nullptr;            // +0x78
    bool m_over = false;                                // +0x7C
    bool m_empty = true;                                // +0x7D
};

// A highlighted check box (type 0x24, vtable 0x00815178): a text control with a yes and a no half
// (C_CheckBoxHalf, its children), checked (yes full, no empty) or not; a click toggles it.
class C_HighlightCheckBox : public C_TextControl {      // 0x80 bytes
public:
    C_HighlightCheckBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_HighlightCheckBox() override = default;          // 0x00660470 (0x00660490)

    // The box placed, the halves made (false: no box, not 7 children, a frame not found), checked.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00654500
    // The box's hover, the half with the id's (inactive: not handled).
    int OnEnter(int id) override;                       // 0x00654b60
    int OnLeave(int id) override;                       // 0x00654bd0
    int OnClick(int id) override;                       // 0x00654c40
    // To another half: the leave, the enter (nothing before an enter).
    int OnInput05(int id) override;                     // 0x00654c50
    int OnKeyClick(int id) override;                    // 0x00654c30
    // GET_CHECKED, SET_CHECKED, SET_CHECKED_QUIET; FACE_FRONT turns its frame to (0, 0, -1).
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00654a10
    void SetInactive(bool inactive) override;           // 0x006544c0

protected:
    // The half the id's last bits name (1 yes, 2 no; else null).
    C_CheckBoxHalf *Half(uint32_t id) const;
    // Checked or not, then CONTROL_NOTIFY_CHANGED (the value: checked).
    void Toggle();                                      // 0x00654cc0
    void Check();                                       // 0x00654d00
    void Uncheck();                                     // 0x00654d30

    C_CheckBoxHalf *m_yes = nullptr;                    // +0x70
    C_CheckBoxHalf *m_no = nullptr;                     // +0x74
    bool m_checked = true;                              // +0x78
    uint32_t m_overId = 0;                              // +0x7C the half's id under the cursor (0: none)
};

// A side indicator (type 0x1e, vtable 0x008146d8): its "allied*" or "axis*" frame shown by a side;
// every 2 s the side of the zone (a C_MPZone) set as its index (CONTROL_MSG_SET_INDEX).
class C_SideIndicator : public C_Control {              // 0x80 bytes
public:
    C_SideIndicator(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_SideIndicator() override = default;              // 0x00660e00 (0x00660e20)

    // Every 2 s the zone's team.
    void Tick(int time) override;                       // 0x00652430
    // The frames (false: fewer than 3 children; both off), no side; posted to be ticked.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006521f0
    // Its notification only.
    int OnKeyClick(int id) override;                    // 0x00652470
    // SET_SIDE, GET_SIDE.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00652380

protected:
    // The side (TEAM_*) shown, then CONTROL_NOTIFY_CHANGED (0); unless already.
    void SetSide(int side);                             // 0x006523d0

    int m_unknown6C = 0;                                // +0x6C (not read)
    I3D_frame *m_alliedFrame = nullptr;                 // +0x70
    I3D_frame *m_axisFrame = nullptr;                   // +0x74
    int m_side = 0;                                     // +0x78
    int m_timer = 0;                                    // +0x7C ms to the next look
};

// A mark (type 0x1f, vtable 0x00814650): its "yesnormal*" frame (marked) or its "emptynormal*" one
// (the other four kept off), and a title on its "text_*" frame showing a number ("00").
class C_MarkBox : public C_Control {                    // 0x8C bytes
public:
    C_MarkBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_MarkBox() override;                              // 0x00660f50 (0x00660f70): the title deleted

    // The title's only.
    void Refresh() override;                            // 0x006525e0
    // The title (optional), the frames (false: one not found), the empty one shown.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006525f0
    // Handled unless inactive.
    int OnEnter(int) override { return m_inactive ? 0 : CONTROL_HANDLED; }   // 0x00652490
    int OnLeave(int) override { return CONTROL_HANDLED; }   // 0x006524a0
    // CONTROL_NOTIFY_0E (its value).
    int OnClick(int id) override;                       // 0x006524e0
    int OnKeyClick(int id) override;                    // 0x006524b0
    // GET_CHECKED, SET_CHECKED (marked), SET_NUMBER.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006528a0

protected:
    // A frame at `place`'s place, off.
    void PlaceFrame(I3D_frame *frame, I3D_frame *place);   // 0x00652570
    // Marked (the yes frame shown) or not (the empty one).
    void Mark();                                        // 0x00652510
    void Unmark();                                      // 0x00652540

    C_TitleControl *m_title = nullptr;                  // +0x6C
    I3D_frame *m_noActive = nullptr;                    // +0x70
    I3D_frame *m_noNormal = nullptr;                    // +0x74
    I3D_frame *m_yesActive = nullptr;                   // +0x78
    I3D_frame *m_yesNormal = nullptr;                   // +0x7C
    I3D_frame *m_emptyActive = nullptr;                 // +0x80
    I3D_frame *m_emptyNormal = nullptr;                 // +0x84
    uint8_t m_value = 0;                                // +0x88
};

// An edit control (vtable 0x00813ee0; the base of the edit box and the selection list): a text
// control whose run `m_run` is typed into. A click asks the screen to start the editing
// (CONTROL_MSG_START_EDITING), which calls BeginEdit; the typed characters go to a buffer of up to
// `m_maxLength` bytes, shown as the run; Enter or Escape (the text back) ask the screen to stop it
// (STOP_EDITING), which calls OnInput0B.
class C_EditControl : public C_TextControl {            // 0x88 bytes
public:
    C_EditControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextControl(scene, id, parent) {}
    ~C_EditControl() override = default;                // 0x0064e6c0 (0x0064e6e0)

    // The shown control's box drawn (no children).
    void Refresh() override;                            // 0x006624b0
    // While editing: the box's caret.
    void Tick(int time) override;                       // 0x00662500
    // (0x00662a80: Unknown20 false, the base's.)
    // While editing: the character (8 Backspace, 0xd Enter, 0x1b Escape; full-width digits as
    // ASCII; above 0x7f with the Unicode font as UTF-8), true when taken.
    bool OnChar(int key) override;                      // 0x006627b0
    bool IsEditing() override { return m_editing; }     // 0x0064e510
    // The buffer made, then the text control's.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006624d0
    // Its notification, not handled.
    int OnLeave(int id) override;                       // 0x00662a90
    // Not editing: START_EDITING posted (handled).
    int OnClick(int id) override;                       // 0x00662ab0
    // Editing: STOP_EDITING posted, CONTROL_NOTIFY_02 (handled).
    int OnInput02(int id) override;                     // 0x00662ba0
    // Editing: stopped (the run's text changed: CONTROL_NOTIFY_0B with the id).
    int OnInput0B(int id) override;                     // 0x00662ca0
    // Not editing: the run edited (none: the text cleared first), its text kept and in the buffer.
    int BeginEdit(int) override;                        // 0x00662ce0
    // SET_FORBIDDEN, SET_MAX_LENGTH, SET_DIGITS_ONLY, EDIT (OnClick), GET_TEXT, END_EDIT.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00662520

protected:
    // The buffer's last character removed (UTF-8: its sequence; a broken one: false), shown.
    bool Backspace();                                   // 0x006626a0
    // The buffer shown as the run's text.
    void ShowBuffer();
    // A buffer of `m_maxLength` + 1 bytes, empty.
    void MakeBuffer();

    C_str m_savedText;                                  // +0x70 the run's text before the editing
    std::vector<char> m_buffer;                         // +0x74 (the original's a char *)
    const char *m_forbidden = nullptr;                  // +0x78 characters refused (not owned)
    bool m_editing = false;                             // +0x7C
    bool m_digitsOnly = false;                          // +0x7D (a '-' first allowed)
    int m_run = -1;                                     // +0x80 the box's run edited
    int m_maxLength = 30;                               // +0x84
};

// An edit box (type 0x1b, vtable 0x00813d48): an edit control on its "normal*" frame, its
// "actived*" one while the cursor is over it; a number in a range kept when the editing ends.
class C_EditBox : public C_EditControl {                // 0x9C bytes
public:
    C_EditBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_EditControl(scene, id, parent) {}
    ~C_EditBox() override = default;                    // 0x0064e520 (0x0064e540)

    // The frames (false: one not found), the box on the normal one (the run 0).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065b0a0
    // The active frame (inactive: not handled).
    int OnEnter(int id) override;                       // 0x0065b350
    // The edit control's (not handled); the normal frame, CONTROL_NOTIFY_LEAVE again (the original's).
    int OnLeave(int id) override;                       // 0x0065b3b0
    // Not while inactive.
    int OnClick(int id) override;                       // 0x0065b410
    // The edit control's; the box's hover cleared and selected; the range kept.
    int OnInput02(int id) override;                     // 0x0065b470
    // Its own id, active: the editing stopped (editing) or asked for.
    int OnKeyClick(int id) override;                    // 0x0065b430
    // The edit control's; the range kept.
    int OnInput0B(int id) override;                     // 0x0065b5e0
    // SET_RANGE, SET_NUMBER (digits only: value2 as "%d"), GET_NUMBER, REFRESH and SET_MARGIN (on
    // the normal frame).
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065b1b0

protected:
    // The text's number (sscanf "%d", none: 0) set to the range's end it is past.
    void KeepInRange();

    bool m_hasRange = false;                            // +0x88
    int m_min = 0;                                      // +0x8C
    int m_max = 0;                                      // +0x90
    I3D_frame *m_normalFrame = nullptr;                 // +0x94
    I3D_frame *m_activeFrame = nullptr;                 // +0x98
};

// A selection list (type 0x1a, vtable 0x00814298; the scrolled text's lines): an edit control whose
// runs are lines, one selected (its "select*" frame moved to it); the click selects the line under
// the cursor. Its range and scroll posted to the control its value names (a scroll bar: SET_COUNT,
// SET_POSITION).
class C_SelectList : public C_EditControl {             // 0x98 bytes
public:
    C_SelectList(I3D_scene *scene, uint32_t id, C_Control *parent) : C_EditControl(scene, id, parent) {}
    ~C_SelectList() override;                           // 0x006581c0 (0x006581e0): the cursor released

    // The cursor taken; the frame turned to (0, 0, -1), the select frame to (0, 0, 1) (off, linked
    // to the root); the box placed (false: none).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065c040
    int OnEnter(int) override { return CONTROL_HANDLED; }   // 0x0065c690
    // The box's hover cleared, the edit control's.
    int OnLeave(int id) override;                       // 0x0065c6a0
    // Not editing: the hovered line selected, the scroll posted, notified (CLICK / PRESS).
    int OnClick(int id) override;                       // 0x0065c240
    int OnPress(int id) override;                       // 0x0065c440
    // Taking input, not editing: CONTROL_NOTIFY_05, the line under the cursor hovered.
    int OnInput05(int id) override;                     // 0x0065c630
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065b750
    // The select frame at the selected line's centre (none there: off).
    virtual void UpdateSelectBar();                     // +0x84 0x0065c160

protected:
    // The hovered line selected; the range and the scroll posted; the bar moved.
    void SelectHoveredLine();
    // SET_COUNT (the box's scroll range) and SET_POSITION (its scroll; `always` false: only with a
    // range) posted to the control the value names.
    void PostScroll(bool always);

    I3D_frame *m_selectFrame = nullptr;                 // +0x88
    C_Cursor *m_cursor = nullptr;                       // +0x8C
    bool m_unknown90 = true;                            // +0x90 (not read)
    int m_selected = -1;                                // +0x94
};

// A scrolled text (type 0xf, vtable 0x008143a8; the combo box's list): a group of a selection list
// (on "text*", its id | 8: child 0) and a scroll bar (on "scroll*", its id | 4, a page step of 5:
// child 1), each the other's value (the list's scroll posted to the bar, the bar's to the list).
class C_ScrolledText : public C_ControlGroup {          // 0x78 bytes
public:
    C_ScrolledText(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_ScrolledText() override = default;               // 0x006590f0 (0x00659110)

    // By the id's last 4 bits: 8 the list, 4 .. 7 the bar (and its arrows, its slider).
    int FindChild(uint32_t id) const override;          // 0x006583a0
    bool Unknown20(int value) override { return FocusUnknown20(value); }   // 0x00658370
    // The parts (each optional; its frame's id cleared); true.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00657dd0
    int OnEnter(int id) override { return EnterChild(id); }   // 0x00658550
    // (0x006585e0: the group's leave.)
    // The focus' when it is the child with the id (the list's: CONTROL_NOTIFY_PRESS too).
    int OnPress(int id) override;                       // 0x00658620
    // The bar's page up or down per 120 (no bar or none: not handled).
    int OnWheel(int id, int amount) override;           // 0x00658680
    // To the child its target names; the text and list messages to the list.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006583d0
    // The lines (style 0x13), then a refresh.
    void SetStrings(const std::vector<C_str> &strings) override;   // 0x006587e0
    // The parts too (the base's not called).
    void SetInactive(bool inactive) override;           // 0x00657da0

protected:
    C_SelectList *m_list = nullptr;                     // +0x70
    C_ScrollBar *m_bar = nullptr;                       // +0x74
};

// The combo box's header (vtable 0x00814320): an edit control on its frame turned to (0, 0, -1), the
// run 0; its click not handled (the combo box's route toggles the list).
class C_ComboEdit : public C_EditControl {              // 0x88 bytes
public:
    C_ComboEdit(I3D_scene *scene, uint32_t id, C_Control *parent) : C_EditControl(scene, id, parent) {}
    ~C_ComboEdit() override = default;                  // 0x006592c0 (0x006592e0)

    // The box placed on the frame (false: none).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065aec0
    // Notified (inactive: not handled).
    int OnEnter(int id) override;                       // 0x0065afd0
    // The box's hover cleared (not: 0): CONTROL_NOTIFY_LEAVE, then the edit control's (twice).
    int OnLeave(int id) override;                       // 0x0065aff0
    int OnClick(int) override { return CONTROL_NOT_HANDLED; }   // 0x0065b020
    // The edit control's; the box's hover cleared and selected.
    int OnInput02(int id) override;                     // 0x0065b070
    // Its own id, active: the editing stopped (editing) or asked for (C_EditBox's).
    int OnKeyClick(int id) override;                    // 0x0065b030
    // EDIT (the edit control's click), REFRESH and SET_MARGIN (on the frame).
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065af50
};

// A combo box (type 0x11, vtable 0x00814e48): a header (C_ComboEdit on "header*", its id | 2), a
// "cbdown*" button (its id | 1) and a scrolled text list (on "listbox*", its own id; hidden) shown
// by the button or the header; a pressed line picked into the header (CONTROL_NOTIFY_0E); the
// header's edit puts its text in the picked line.
class C_ComboBox : public C_ControlGroup {              // 0x88 bytes
public:
    C_ComboBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_ComboBox() override = default;                   // 0x0065f260 (0x0065f280)

    // By the id's last 4 bits: 1 the button, 2 the header, above 3 the list.
    int FindChild(uint32_t id) const override;          // 0x00659450
    // The parts (each optional; its frame's id cleared); true.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00658a90
    int OnEnter(int id) override { return EnterChild(id); }   // 0x00659a30
    // The focus' leave (not taken: 0); then handled unless the list is open.
    int OnLeave(int id) override;                       // 0x00659ac0
    // Off the children: the leave; to another: the leave and the enter; else the focus'.
    int OnInput05(int id) override;                     // 0x00659b10
    // The focus' when it is the child with the id; else outside the box, open: closed (0).
    int OnPress(int id) override;                       // 0x00659b70
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00659480
    // The header the first line's, the list the lines.
    void SetStrings(const std::vector<C_str> &strings) override;   // 0x00659c40
    // The parts too (the base's not called).
    void SetInactive(bool inactive) override;           // 0x00659a00

protected:
    // The header's edit stopped: its text the picked line's (the original's callback).
    static bool HeaderEdited(void *object, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00658930
    // A message to a part (none: nothing; the fix, the original's crashes).
    static void SendTo(C_Control *part, const S_ControlMessage &message);
    // The picked line shown in the header, the list closed.
    void ShowPicked(int index, const C_str &text);

    C_Button *m_button = nullptr;                       // +0x70
    C_ScrolledText *m_list = nullptr;                   // +0x74
    C_ComboEdit *m_header = nullptr;                    // +0x78
    bool m_open = false;                                // +0x7C
    int m_picked = -1;                                  // +0x80
    C_str m_pickedText;                                 // +0x84
};

// A titled combo box (type 0x28, vtable 0x00814dc0; the game's difficulty): a title on "text18"
// (taking input) and a combo box on "game difficulty", both with its id.
class C_TitledComboBox : public C_ControlGroup {        // 0x78 bytes
public:
    C_TitledComboBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_TitledComboBox() override = default;             // 0x006619f0 (0x00661a10)

    // Both made: the id's last 4 bits 0 the title, else the combo box (else -1).
    int FindChild(uint32_t id) const override;          // 0x00661720
    // The parts (false: no combo box; its frame's id cleared).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00661780
    // (DE)ACTIVATE, SHOW, HIDE; the texts to the title; the rest to the combo box.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00661930
    // The parts too.
    void SetInactive(bool inactive) override;           // 0x00661750

protected:
    C_TitleControl *m_title = nullptr;                  // +0x70
    C_ComboBox *m_combo = nullptr;                      // +0x74
};

// The chat input (type 0x21, vtable 0x008145c8): an edit box on "binput*" (its id | 4, its prompt
// text 2002, 100 characters), a label on "text*" (its id | 8; the lines, 8 kept) and the "bTeam"
// toggle (its id | 0xc: up, to the team). Enter sends the line ("/t" or "/a" put first unless there).
class C_ChatInput : public C_ControlGroup {             // 0x80 bytes
public:
    C_ChatInput(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_ChatInput() override = default;                  // 0x0065efe0 (0x0065f000)

    // By the id's last 4 bits: 4 the edit box, 8 the label, above 0xc the toggle's frames.
    int FindChild(uint32_t id) const override;          // 0x00657bd0
    bool Unknown20(int value) override { return FocusUnknown20(value); }   // 0x00657ba0
    // The parts (each optional; its frame's id cleared); true.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006574d0
    // To the child its target names; the lines to the label (8 kept); TEAM_TOGGLED.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00657c00

protected:
    // The editing's stop asked for (Enter): the line sent (CONTROL_NOTIFY_SEND), the prompt back,
    // the editing started again (the original's callback).
    static bool LineEntered(void *object, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x006571f0

    C_LabelControl *m_label = nullptr;                  // +0x70
    C_EditBox *m_input = nullptr;                       // +0x74
    C_AllToggleButton *m_teamToggle = nullptr;          // +0x78
    bool m_team = false;                                // +0x7C to the team only (the toggle up)
};
