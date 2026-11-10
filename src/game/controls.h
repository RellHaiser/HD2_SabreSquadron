// The interface's small controls (game/controls.cpp, part 9c-ii; the base: game/control.h): the
// text controls (a text box on the control's frame), the button, the text button, the picture, the
// tooltip, the frame switch, the group (its input passed to the child under the cursor) and the
// paged text control; part 9c-iii-a: the slider, the scroll bar and the spin control; part 9c-iii-b:
// the check boxes, the toggle buttons, the side indicator and the mark; part 9c-iv: the edit
// controls, the scrolled text, the combo boxes, the chat input, the skill and the item information.
// Made by the factory (C_Control::CreateChild) by their descriptions' types.
#pragma once

#include <cstddef>

#include "game/control.h"

class C_Cursor;
class C_PagedTextBox;
class C_TextBox;
class ISND_stream;
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
    // The skill's: a point up or down (value: done; the pool's too), the base (the points 0), the
    // total (the points the rest).
    CONTROL_MSG_SKILL_UP = 0x1000070,
    CONTROL_MSG_SKILL_DOWN = 0x1000071,
    CONTROL_MSG_SET_BASE = 0x1000072,
    CONTROL_MSG_SET_TOTAL = 0x1000073,
    // The item information's: the item (value), its count and its second's; to the parent when
    // either count is (value: its id).
    CONTROL_MSG_SET_ITEM = 0x100007B,
    CONTROL_MSG_SET_ITEM_COUNT = 0x100007C,
    CONTROL_MSG_SET_SECOND_COUNT = 0x1000082,
    CONTROL_MSG_ITEM_COUNTED = 0x2000083,
    // The list group's: can it scroll up / down (value), its scroll range (value), the items shown
    // at once (value), an item's step across or down from the last (value2 a float; the other 0),
    // the first item's place (value2 an S_vector from _aligned_malloc, the list's to free), an
    // item selected by index (value); ADD_ITEM (value the root frame, value2 an S_ControlDesc *,
    // the list's to delete), REMOVE_ITEM (value the item's id; posted on with value2 its frame),
    // PREVIOUS_ITEM / NEXT_ITEM a step up / down, CLEAR, GET_POSITION, SET_POSITION, REFRESH.
    CONTROL_MSG_CAN_SCROLL_UP = 0x100000F,
    CONTROL_MSG_CAN_SCROLL_DOWN = 0x1000010,
    CONTROL_MSG_GET_SCROLL_RANGE = 0x100001F,
    CONTROL_MSG_SET_VISIBLE = 0x1000028,
    CONTROL_MSG_SET_ACROSS_STEP = 0x1000029,
    CONTROL_MSG_SET_DOWN_STEP = 0x100002A,
    CONTROL_MSG_SET_ORIGIN = 0x100006B,
    CONTROL_MSG_SELECT_INDEX = 0x20000AA,
    // The bar's: its minimum, maximum and value (value2 a float, got or set; the upgrade bar's by
    // value: 0 the value, 1 the upgrade, 2 (set) the suffix text, value2 a char *), the axis it
    // scales along (value: 0 x, 1 y, 2 z).
    CONTROL_MSG_GET_BAR_MIN = 0x100008D,
    CONTROL_MSG_GET_BAR_MAX = 0x100008E,
    CONTROL_MSG_GET_BAR_VALUE = 0x100008F,
    CONTROL_MSG_SET_BAR_MIN = 0x1000090,
    CONTROL_MSG_SET_BAR_MAX = 0x1000091,
    CONTROL_MSG_SET_BAR_VALUE = 0x1000092,
    CONTROL_MSG_SET_BAR_AXIS = 0x1000093,
    // The hiding list's: an item hidden (value its id), all hidden; ITEM_COUNTED shows one again.
    CONTROL_MSG_HIDE_ITEM = 0x2000084,
    CONTROL_MSG_HIDE_ALL = 0x2000085,
    // The pool list's: a hidden item filled and shown (value its index, value2 an entry whose name,
    // a C_str, is at + 1); the current one also hovered.
    CONTROL_MSG_ADD_CURRENT_ENTRY = 0x1000064,
    CONTROL_MSG_ADD_ENTRY = 0x1000067,
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
    ~C_ControlGroup() override = default;               // 0x00658f00 (0x00658f20); its members' cleanup 0x0065ed20 (DS 0x00695c80)

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
    I3D_frame *GetHighlightFrame() const override { return m_activeFrame; }   // 0x00655630 (DS 0x0068a2f0)
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
    int OnClick(int) override { return CONTROL_HANDLED; }   // 0x0064fd20 (DS 0x00683600)
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
    int OnClick(int id) override;                       // 0x00654110 (DS 0x00688790)
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
    int OnClick(int id) override;                       // 0x00655300 (DS 0x00689eb0)
    int OnPress(int) override { return CONTROL_HANDLED; }   // 0x00655310 (DS 0x00689ed0)
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
    int OnClick(int id) override;                       // 0x00654c40 (DS 0x00689610)
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
    int OnLeave(int) override { return CONTROL_HANDLED; }   // 0x006524a0 (DS 0x006863f0)
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
    bool IsEditing() override { return m_editing; }     // 0x0064e510 (DS 0x006819a0)
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
    ~C_ComboEdit() override = default;                  // 0x006592c0 (DS 0x0068e9d0) (0x006592e0 (DS 0x0068ea00))

    // The box placed on the frame (false: none).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065aec0
    // Notified (inactive: not handled).
    int OnEnter(int id) override;                       // 0x0065afd0
    // The box's hover cleared (not: 0): CONTROL_NOTIFY_LEAVE, then the edit control's (twice).
    int OnLeave(int id) override;                       // 0x0065aff0
    int OnClick(int) override { return CONTROL_NOT_HANDLED; }   // 0x0065b020 (DS 0x00690ee0)
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
    C_ComboBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}   // 0x0065f1b0 (DS 0x00696250)
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

// A skill (type 0x19, vtable 0x00814ba0): its name ("skill_text*") and value ("skill_value*", base +
// points) titles, a minus ("barrow up*", id | 2) and a plus ("barrow down*", id | 3) button; a
// point taken or given asks the control its value names (a pool: SKILL_DOWN to take, SKILL_UP to
// give back; refused: the answer's value 0). The value 0 .. 100 (the points down to 0); the -
// and + keys too.
class C_SkillControl : public C_Control {               // 0x8C bytes
public:
    C_SkillControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_SkillControl() override = default;               // 0x0065f760 (0x0065f780)

    // The child whose id's last 4 bits are `id`'s (the titles share theirs: the name's).
    int FindChild(uint32_t id) const override;          // 0x00650180
    // The shown control's titles drawn (not the buttons).
    void Refresh() override;                            // 0x00650a30
    // The minus keys (0xc, 0x4a: a point down) and the plus keys (0xd, 0x4e: up): true.
    bool Unknown20(int key) override;                   // 0x006505e0
    // The parts (the buttons optional: without one, no input); true.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0064fda0
    // The name title's enter; the focus' leave (refused: not handled), the child with the id's.
    int OnEnter(int id) override;                       // 0x00650810
    int OnLeave(int id) override;                       // 0x00650890
    int OnInput05(int id) override;                     // 0x006508f0
    // Inactive: handled; else the focus'.
    int OnPress(int id) override;                       // 0x00650960
    int OnInput08(int id) override;                     // 0x006509a0
    // SET_TEXT (the name, not shown yet), SKILL_UP / SKILL_DOWN (value: done), SET_BASE (the
    // points 0), SET_TOTAL (the points the rest, negative: 0), GET_NUMBER; the rest to the titles.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00650670
    void SetStrings(const std::vector<C_str> &) override {}   // 0x00650170
    // The parts too.
    void SetInactive(bool inactive) override;           // 0x006509e0

protected:
    C_Control *Focus() const { return IsChild(m_focus) ? m_children[m_focus] : nullptr; }
    // A point down (none: false) or up (100: false), unless the pool refuses; shown.
    bool PointDown();                                   // 0x006501c0
    bool PointUp();                                     // 0x006502a0
    // The pool's answer to `id` (no pool: yes).
    bool AskPool(uint32_t id);
    // The name and the value shown, the buttons active as far as they go.
    void Update();                                      // 0x00650390

    C_TitleControl *m_nameTitle = nullptr;              // +0x6C
    C_TitleControl *m_valueTitle = nullptr;             // +0x70
    C_Button *m_plusButton = nullptr;                   // +0x74
    C_Button *m_minusButton = nullptr;                  // +0x78
    C_str m_name;                                       // +0x7C
    int m_points = 0;                                   // +0x80
    int m_base = 0;                                     // +0x84
    int m_focus = -1;                                   // +0x88
};

class C_ItemDef;

// An item's information (type 0x1c, vtable 0x00814b18): its picture (on its frame, id | 1), its
// ammo's or headgear's (on "ammo*", id | 2), each with a count, and its stats (a title on
// "text_info*": the name, the weight; for guns the ammo, the muzzle speed, a rifle scope's zoom).
class C_ItemInfo : public C_ControlGroup {              // 0x8C bytes
public:
    C_ItemInfo(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_ItemInfo() override = default;                   // 0x00660c30 (0x00660c50)

    // By the id's last 4 bits: 1 the picture, 2 the second.
    int FindChild(uint32_t id) const override;          // 0x00653b30
    // The shown control's parts drawn.
    void Refresh() override;                            // 0x00653b00
    // The parts (each optional; the pictures hidden, their "X_SQUARE.BMP" face group); false only
    // when the group's is.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00652a60
    // SET_ITEM, SET_ITEM_COUNT, SET_SECOND_COUNT; the callbacks to the pictures; REFRESH to all.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00652dc0

protected:
    // The stats' lines of `def`.
    void ShowStats(C_ItemDef *def);                     // 0x00653180
    // The pictures active by their counts, shown; the parent told when either is.
    void UpdateCounts();                                // 0x00653890
    // A picture of the item `id`'s (its index the id).
    static void ShowPicture(C_PictureControl *picture, int id, C_ItemDef *def);

    int m_item = -1;                                    // +0x70
    int m_count = 0;                                    // +0x74
    int m_second = -1;                                  // +0x78 the ammo's or the headgear's item
    int m_secondCount = 0;                              // +0x7C
    C_PictureControl *m_picture = nullptr;              // +0x80
    C_PictureControl *m_secondPicture = nullptr;        // +0x84
    C_TitleControl *m_stats = nullptr;                  // +0x88
};

class C_ListItem;

// A list group (type 0xb, vtable 0x00815068; the base of types 0x23 and 0x18): a group of items
// laid out from an origin by a step (down or across), `m_visible` of them shown from `m_first` (the
// list's frame moved by the step as it scrolls); one selected (its "selector*" frame behind it),
// one picked (a C_ListItem). Its scroll posted to the control its value names (a scroll bar); the
// arrow keys, Home / End and the page keys move the focus.
class C_ListGroup : public C_ControlGroup {             // 0xA0 bytes
public:
    C_ListGroup(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_ListGroup() override;                             // 0x0065ea40 (0x0065e840): the origin freed, the extra
                                                        // control deleted, the cursor released

    // The focus' key (taking input); else the navigation keys (true: moved).
    bool Unknown20(int key) override;                   // 0x006557d0
    // The base's; the selector frame at (0, 0, 0); true.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00655b40
    // The group's; handled: the focus selected.
    int OnClick(int id) override;                       // 0x00655de0
    int OnPress(int id) override;                       // 0x00655db0
    // A page up or down of the scroll bar the value names per 120 (none: not handled).
    int OnWheel(int id, int amount) override;           // 0x00655fe0
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00656230
    // The items past the visible ones, + 1.
    virtual int ScrollRange() const;                    // +0x84 0x00656130

    // The item picked (the previous one's input 02 and leave: deselected).
    bool PickItem(C_Control *item);                     // 0x006569f0

protected:
    // A step (1 up, 2 down; 0 none), the items shown or hidden, refreshed; the selector shown with
    // the selected (false: no step).
    bool Scroll(int direction);                         // 0x00656a20
    // The item at the index selected (scrolled to; the selector behind it; the position posted).
    void Select(int index);                             // 0x00655e10
    // The focus moved to the previous / next item taking input (false: none; the focus' leave
    // refused: true, not moved).
    bool FocusPrevious();                               // 0x006556c0
    bool FocusNext();                                   // 0x00655750
    // The items by their ids (when sorted).
    void Sort();                                        // 0x00655bc0
    // The items at the origin by the step (the argument not read: the original's, 0).
    void Layout(int);                                   // 0x00655c70
    // A message posted to the control the value names (none: nothing).
    void PostToValue(uint32_t id, uint32_t value) const;
    int ItemCount() const { return static_cast<int>(m_children.size()); }

    C_Cursor *m_cursor = nullptr;                       // +0x70 (the subclasses')
    C_Control *m_picked = nullptr;                      // +0x74
    C_Control *m_extra = nullptr;                       // +0x78 (the subclasses'; deleted with the list)
    I3D_frame *m_selector = nullptr;                    // +0x7C
    int m_selected = -1;                                // +0x80
    bool m_sorted = true;                               // +0x84
    S_vector *m_origin = nullptr;                       // +0x88 (_aligned_malloc'd, owned)
    float m_downStep = 0.0f;                            // +0x8C
    float m_acrossStep = 0.0f;                          // +0x90
    int m_visible = 0;                                  // +0x94
    int m_first = 0;                                    // +0x98
    int m_last = 0;                                     // +0x9C
};

// An item of a list group (type 6, vtable 0x00814ed0): a text button whose click picks it (its
// parent a C_ListGroup), its box's hover while not picked.
class C_ListItem : public C_TextButton {                // 0x7C bytes
public:
    C_ListItem(I3D_scene *scene, uint32_t id, C_Control *parent) : C_TextButton(scene, id, parent) {}
    ~C_ListItem() override = default;                   // 0x006601b0 (0x006601d0)

    // Not picked: the box's hover; notified (handled).
    int OnEnter(int id) override;                       // 0x00653f70
    int OnLeave(int id) override;                       // 0x00653fa0
    // Picked: the parent's pick, the text button's enter, notified.
    int OnClick(int id) override;                       // 0x00653fd0
    // Id 1 (the list's unpick): not picked, the text button's leave, CONTROL_NOTIFY_02.
    int OnInput02(int id) override;                     // 0x00654010
    // CONTROL_NOTIFY_PRESS, then the click.
    int OnPress(int id) override;                       // 0x00654040

protected:
    bool m_picked = false;                              // +0x78
};

// A bar (type 0x16, vtable 0x008147e8; the base of the upgrade bar): its frame scaled along an axis
// by its value over its range (max - min; not above 0: 1), off at 0 or below, at most 1; not taking
// input.
class C_BarControl : public C_Control {                 // 0x7C bytes
public:
    C_BarControl(I3D_scene *scene, uint32_t id, C_Control *parent);
    ~C_BarControl() override = default;                 // 0x0065fb10 (0x0065faf0)

    // The base's, then placed (false: not made).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006630a0
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006630e0
    // The frame scaled (or off) and updated.
    virtual void UpdateBar();                           // +0x84 0x00662e70

protected:
    // A scale of `fraction` along the axis (1 on the others); another axis: none.
    static void ScaleAlong(I3D_frame *frame, int axis, float fraction);

    float m_value = 0.0f;                               // +0x6C
    float m_max = 1.0f;                                 // +0x70
    float m_min = 0.0f;                                 // +0x74
    int m_axis = 0;                                     // +0x78
};

// An upgrade bar (type 0x17, vtable 0x00814760; the skills' and the upgrades'): a bar whose
// "hlcpct*" frame shows the value and "upgrade*" the value with its upgrade; a title on "name_*"
// (the text messages) and one on "text_*" (the value, the upgrade, the suffix).
class C_UpgradeBar : public C_BarControl {              // 0x98 bytes
public:
    C_UpgradeBar(I3D_scene *scene, uint32_t id, C_Control *parent) : C_BarControl(scene, id, parent) {}
    ~C_UpgradeBar() override = default;                 // 0x0065fc50 (0x0065fc30)

    // The shown bar's titles.
    void Refresh() override;                            // 0x00663a90
    // The bar's; the titles made; both frames found (false: not), placed.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00663880
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00663700
    // Hidden at value and upgrade 0; else shown, the frames scaled, the text set; the frame updated.
    void UpdateBar() override;                          // 0x006631a0

protected:
    C_TitleControl *m_name = nullptr;                   // +0x7C
    C_TitleControl *m_text = nullptr;                   // +0x80
    I3D_frame *m_valueFrame = nullptr;                  // +0x84 "hlcpct*"
    I3D_frame *m_upgradeFrame = nullptr;                // +0x88 "upgrade*"
    float m_current = 0.0f;                             // +0x8C
    float m_upgrade = 0.0f;                             // +0x90
    C_str m_suffix;                                     // +0x94
};

// A hiding list (type 0x23, vtable 0x00814a08; the base of the pool list): a list group whose items
// can be hidden (kept, the hidden ones still taking their messages) and shown again (ITEM_COUNTED:
// the item informations', sorted in).
class C_HidingList : public C_ListGroup {               // 0xB4 bytes
public:
    C_HidingList(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ListGroup(scene, id, parent) {}
    ~C_HidingList() override;                           // 0x0065eb60 (DS 0x00695a60) (0x0065eb40): the hidden deleted

    bool HandleMessage(S_ControlMessage *message) override;   // 0x00659d50

protected:
    // The hidden item's index by id (the last match; none: -1).
    int FindHidden(uint32_t id) const;                  // 0x0065a0c0
    int HiddenCount() const { return static_cast<int>(m_hidden.size()); }

    std::vector<C_Control *> m_hidden;                  // +0xA0 (owned)
};

// A pool list (type 0x26, vtable 0x008144b8): a hiding list, unsorted, whose hidden items are filled
// with entries and shown; the focus held from input 0A until input 09 (a drag).
class C_PoolList : public C_HidingList {                // 0xB8 bytes
public:
    C_PoolList(I3D_scene *scene, uint32_t id, C_Control *parent);
    ~C_PoolList() override = default;                   // 0x0065ee20 (DS 0x00695dd0) (0x0065ee00)

    // The held item's (none: 0).
    int OnInput08(int id) override;                     // 0x0065a510
    // The held item's, then none held (none: 0).
    int OnInput09(int id) override;                     // 0x0065a530
    // The focus held, its input 0A (none: 0).
    int OnInput0A(int id) override;                     // 0x0065a4d0
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065a100

protected:
    C_Control *m_held = nullptr;                        // +0xB4
};

// A server found by the browser (its module not ported yet: the fields the server list reads).
class C_chunk;

struct S_ServerEntry {
    // Not answered yet: the map " ??? ", no players, no flags, game type 4, ping 9999, not listed (the net
    // module's; part 9h-xx).
    void Reset();                                       // 0x007252c0 (__fastcall); the constructor 0x007252b0 (DS 0x0077ca40) is this
    // Its chunk 2000 (the strings with their lengths, then the fields; part 9h-xxii). True.
    bool Save(C_chunk &chunk) const;                    // 0x007253f0
    // The chunks left in the one entered: 2000 read (the strings 0x32 at most), the others skipped. True.
    bool Load(C_chunk &chunk);                          // 0x00725660

    char name[0x33];                                    // +0x00
    char map[0x35];                                     // +0x33
    uint32_t address;                                   // +0x68 (shown top byte first; part 9h-xii)
    int port;                                           // +0x6C
    int gameType;                                       // +0x70 (E_MpGameType)
    uint32_t ping;                                      // +0x74
    uint32_t maxPlayers;                                // +0x78
    uint32_t players;                                   // +0x7C
    bool dedicated;                                     // +0x80
    bool internet;                                      // +0x81 (the game info's "Internet Server")
    bool password;                                      // +0x82
    uint8_t unknown83;
    int unknown84;                                      // +0x84 (saved)
    bool voice;                                         // +0x88
    uint8_t unknown89[3];
    int versionMinor;                                   // +0x8C
    int versionMajor;                                   // +0x90
    int unknown94;                                      // +0x94 (saved)
    bool listed;                                        // +0x98 the browser's list shows it (pinging)
    uint8_t unknown99[3];
};
static_assert(offsetof(S_ServerEntry, gameType) == 0x70 && offsetof(S_ServerEntry, voice) == 0x88 &&
                  offsetof(S_ServerEntry, address) == 0x68 && offsetof(S_ServerEntry, internet) == 0x81 &&
                  offsetof(S_ServerEntry, versionMajor) == 0x90 && offsetof(S_ServerEntry, listed) == 0x98 &&
                  sizeof(S_ServerEntry) == 0x9C, "S_ServerEntry layout");

// The server list's messages: the entries' count (value), the sort (value: a key 1..5 in the low 3
// bits, 0x40 / 0x80 the direction toggled; 0 sorted again) and its key; a row's entry (value2; its
// own: the current one, in white).
enum : uint32_t {
    CONTROL_MSG_GET_ENTRY_COUNT = 0x10000A2,
    CONTROL_MSG_SORT = 0x2000079,
    CONTROL_MSG_GET_SORT = 0x20000AD,
};

// A server row (vtable 0x008154b8; the server list's, on its "Server<n>" frames): a text button whose
// runs show an entry (name, map, game type, ping, players; inactive when its version differs), with
// a password, a dedicated and a voice icon on each of its frames. The entry is kept as its index.
class C_ServerRow : public C_TextButton {               // 0x90 bytes
public:
    C_ServerRow(I3D_scene *scene, uint32_t id, C_Control *parent);   // 0x00666ad0
    ~C_ServerRow() override = default;                  // 0x00666ba0 (0x00666b80)

    // The text button's; the six icons found (false: not), off, given the row's id and linked to
    // the normal ("x*") or the active frame ("x*01"); the box on the normal frame, no margin.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00665d70
    // ADD_ENTRY (value2 the entry; none: cleared), ADD_CURRENT_ENTRY (white), REMOVE_ITEM (cleared),
    // GET_ITEM (value2 the entry); true.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00665f90

    S_ServerEntry *GetEntry() const { return reinterpret_cast<S_ServerEntry *>(static_cast<uintptr_t>(m_index)); }

protected:
    I3D_frame *m_password = nullptr;                    // +0x78 "xpassword"
    I3D_frame *m_dedicated = nullptr;                   // +0x7C "xdedicated"
    I3D_frame *m_voice = nullptr;                       // +0x80 "xvoice"
    I3D_frame *m_activePassword = nullptr;              // +0x84 "xpassword01"
    I3D_frame *m_activeDedicated = nullptr;             // +0x88 "xdedicated01"
    I3D_frame *m_activeVoice = nullptr;                 // +0x8C "xvoice01"
};

// The server list (type 0x18, vtable 0x00814980): a list group of 11 rows ("Server1" ..
// "Server11", 0.033 apart) showing a window of its entries (not owned); a row's click selects its
// entry (CONTROL_NOTIFY_0E), its notification 13 passed on; sorted by a key; no keys, no tick.
class C_ServerList : public C_ListGroup {               // 0xC8 bytes
public:
    C_ServerList(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ListGroup(scene, id, parent) {}
    ~C_ServerList() override = default;                 // 0x00661230 (DS 0x00698c20) (0x00661210)

    void Tick(int) override {}                          // 0x00667b20
    bool Unknown20(int) override { return false; }      // 0x00667b10
    // The list group's (its result dropped); the rows made (false: a frame missing), laid out from
    // the last one's place.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006666f0
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00666e80
    // The entries past the rows, + 1.
    int ScrollRange() const override;                   // 0x00666cf0

protected:
    // The entries sorted by a key (see CONTROL_MSG_SORT).
    void SortEntries(uint32_t key);                     // 0x006677c0
    // The key's comparison (none: null).
    using T_Compare = bool (*)(const S_ServerEntry *a, const S_ServerEntry *b);
    T_Compare Comparison() const;                       // 0x00667720
    int EntryCount() const { return static_cast<int>(m_entries.size()); }
    // The rows' callbacks: a click selects the row's entry; notification 13 passed on.
    static bool RowClicked(void *object, void *, C_Control *control, uint32_t, uint32_t);   // 0x00666650
    static bool RowNotified(void *object, void *, C_Control *, uint32_t, uint32_t);   // 0x00666630

    S_ServerEntry *m_selectedEntry = nullptr;           // +0xA0
    uint32_t m_sortKey = 0x40;                          // +0xA4
    std::vector<S_ServerEntry *> m_entries;             // +0xA8
    uint8_t m_unknownB8 = 0;                            // +0xB8
    uint32_t m_unknownBC = 0;                           // +0xBC
    int m_firstEntry = 0;                               // +0xC0 (the first row's)
    int m_lastEntry = 0;                                // +0xC4 (the original's not set when made)
};

// The hot key control's messages: its binding (value; GET has two codes), set with its name's text
// id (value2) for the title. HOT_KEY_DONE is the menu's MENU_MSG_KEYBOARD_OFF (target 0): value 1
// when the edit starts (the menu's keys and clicks off), 0 when it ends or a key is bound.
enum : uint32_t {
    CONTROL_MSG_GET_BINDING = 0x100003C,
    CONTROL_MSG_SET_BINDING = 0x100003D,
    CONTROL_MSG_GET_BINDING2 = 0x100003E,
    CONTROL_MSG_HOT_KEY_DONE = 0x20000C2,
};

// A hot key (type 0xd, vtable 0x00814fe0; the base of the titled edit box): an edit box on its frame
// (id | 1, not a child) showing the binding's key, a title on "hktext*" (id | 2, its name). Clicked,
// the edit starts; while editing, the next key pressed is bound (Escape: none).
class C_HotKeyControl : public C_Control {               // 0x7C bytes
public:
    C_HotKeyControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) {}
    ~C_HotKeyControl() override;                         // 0x006607a0 (0x00660780; 0x00660910 a copy): the
                                                         // edit box and the title deleted

    // The shown control's edit box and title.
    void Refresh() override;                             // 0x0064f000
    // While editing: the edit box's tick, then (after a first tick) the key pressed bound (the
    // binding's, CHANGED; Escape: none), the edit ended, HOT_KEY_DONE posted.
    void Tick(int time) override;                        // 0x0064e850
    // While editing: true (its low byte 1, Escape: the edit ended); else false.
    bool Unknown20(int key) override;                    // 0x0064ea90
    // The base's (false: not made); the edit box made on the frame (when there is one), the title on
    // "hktext*" taking input.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0064db20
    // Both parts' (handled).
    int OnEnter(int id) override;                        // 0x0064ed40
    // Not while editing (0); both parts' (handled).
    int OnLeave(int id) override;                        // 0x0064ed70
    // Not while editing (0); the edit box's (handled: HOT_KEY_DONE posted, value 1: keys off).
    int OnClick(int id) override;                        // 0x0064eec0
    // The edit box's (handled: HOT_KEY_DONE posted; else 0).
    int OnInput02(int id) override;                      // 0x0064edb0
    int OnPress(int) override { return CONTROL_HANDLED; }   // 0x0064eea0
    // The click.
    int OnKeyClick(int id) override { return OnClick(id); }   // 0x0064eeb0
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0064eac0
    // Both parts' too.
    void SetInactive(bool inactive) override;            // 0x0064efd0
    // The binding (CONTROL_MSG_SET_BINDING's, without the text).
    virtual void SetBinding(int binding) { m_binding = binding; }   // +0x84 0x00660770

protected:
    // The binding's key name in the edit box.
    void ShowBinding();

    bool m_armed = false;                                // +0x6C (the first editing tick passed)
    int m_binding = 0;                                   // +0x70 (C_KeyBindings' index)
    C_TitleControl *m_title = nullptr;                   // +0x74
    C_EditBox *m_edit = nullptr;                         // +0x78
};

// A titled edit box (type 0xe, vtable 0x00814f58): the hot key control's parts as a plain edit box
// (the input its; "text*" the title; its text 0x7D2 at first), the edit messages passed to it.
class C_TitledEditBox : public C_HotKeyControl {         // 0x7C bytes
public:
    C_TitledEditBox(I3D_scene *scene, uint32_t id, C_Control *parent) : C_HotKeyControl(scene, id, parent) {}
    ~C_TitledEditBox() override = default;               // (0x006608f0)

    // While editing: the edit box's.
    void Tick(int time) override;                        // 0x0064f2d0
    bool Unknown20(int key) override;                    // 0x0064f2f0
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0064f020
    // Both parts' (handled).
    int OnLeave(int id) override;                        // 0x0064f470
    // The edit box's.
    int OnClick(int id) override;                        // 0x0064f440
    int OnInput02(int id) override;                      // 0x0064f430
    int OnPress(int) override { return CONTROL_HANDLED; }   // 0x0064f450
    int OnKeyClick(int id) override;                     // 0x0064f460
    // The text messages the title's; the edit ones (callbacks, length, digits, number, range,
    // forbidden, text) the edit box's; REFRESH both.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0064f300
};

// An item's pictures (type 0x1d, vtable 0x00814a90; the hiding list's items): the item information
// without its stats: its picture (on its frame, id | 1) and its ammo's or headgear's (on "ammo*",
// id | 2), their counts as text; the parent told when either count is, or to hide it (a count
// below 0); the second shown only with its item and a count from 0.
class C_ItemPictures : public C_ControlGroup {          // 0x88 bytes
public:
    C_ItemPictures(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_ItemPictures() override = default;               // (0x00660a60)

    // By the id's last 4 bits: 1 the picture, 2 the second.
    int FindChild(uint32_t id) const override;          // 0x006521c0
    // The shown control's pictures drawn.
    void Refresh() override;                            // 0x00652190
    // The group's (false: not made); the pictures hidden, their "X_SQUARE.BMP" face group; true.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00651810
    // SET_ITEM, SET_ITEM_COUNT, SET_SECOND_COUNT; the callbacks to the pictures; REFRESH to both.
    bool HandleMessage(S_ControlMessage *message) override;   // 0x00651af0

protected:
    // The counts shown; the parent told (ITEM_COUNTED: either above 0; HIDE_ITEM: the count below 0).
    void UpdateCounts();                                // 0x00651ea0
    // A picture of the item `id`'s (its index the id, its texture the item's image).
    static void ShowPicture(C_PictureControl *picture, int id, C_ItemDef *def);

    int m_item = -1;                                    // +0x70
    int m_count = 0;                                    // +0x74
    int m_second = -1;                                  // +0x78 the ammo's or the headgear's item
    int m_secondCount = 0;                              // +0x7C
    C_PictureControl *m_picture = nullptr;              // +0x80
    C_PictureControl *m_secondPicture = nullptr;        // +0x84
};

// The players' list (type 0x20, vtable 0x00814870; the network game's): built from the combo box but
// none of its parts: a scrolled text on its frame (its id; a page step of 5) and a title on
// "header*" (id | 2). SHOW_PLAYERS fills them: the columns' names, each connected player's name and
// two of its stats.
class C_PlayerList : public C_ComboBox {                // 0x90 bytes
public:
    C_PlayerList(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ComboBox(scene, id, parent) {}
    ~C_PlayerList() override = default;                 // (0x0065f420: the combo box's)

    // By the id's last 4 bits: above 3 the list; 2: 2 (not the title's index: the original's).
    int FindChild(uint32_t id) const override;          // 0x0065ae10
    // The control's (its result dropped), its frame's id cleared; the parts (the title optional).
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x0065a620
    // The focus' leave (not taken: CONTROL_NOT_HANDLED); the child with the id's enter (taken: the
    // focus, handled; else 0).
    int OnEnter(int id) override;                       // 0x0065acb0
    // The focus' leave (not taken: 0; else no focus); handled.
    int OnLeave(int id) override;                       // 0x0065ad30
    // Off the children: the leave; another child: the leave and its enter; the focus: its.
    int OnInput05(int id) override;                     // 0x0065ad70
    // The focus' when it is the child with the id (else 0).
    int OnPress(int id) override;                       // 0x0065add0
    bool HandleMessage(S_ControlMessage *message) override;   // 0x0065a850
    void SetStrings(const std::vector<C_str> &) override {}   // 0x0065ae60
    // The parts' (not its own).
    void SetInactive(bool inactive) override;           // 0x0065ae30

protected:
    C_ScrolledText *m_players = nullptr;                // +0x88
    C_TitleControl *m_title = nullptr;                  // +0x8C
};

// A check group (type 0x27, vtable 0x008150f0): a group holding a highlighted check box on "text*"
// (its id; none: not made).
class C_CheckGroup : public C_ControlGroup {            // 0x74 bytes
public:
    C_CheckGroup(I3D_scene *scene, uint32_t id, C_Control *parent) : C_ControlGroup(scene, id, parent) {}
    ~C_CheckGroup() override = default;                 // (0x00661550)

    // The check box (any id; none: -1).
    int FindChild(uint32_t) const override { return m_box ? 0 : -1; }   // 0x006613e0
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x006613f0

protected:
    C_HighlightCheckBox *m_box = nullptr;               // +0x70
};

// The players' list's messages: filled; two ignored.
enum : uint32_t {
    CONTROL_MSG_SHOW_PLAYERS = 0x100007D,
    CONTROL_MSG_IGNORED_7E = 0x100007E,
    CONTROL_MSG_IGNORED_7F = 0x100007F,
};

// The movie control's messages: stopped and closed, stopped, a movie played ("GameMovie\<text>2").
enum : uint32_t {
    CONTROL_MSG_CLOSE_MOVIE = 0x20000A0,
    CONTROL_MSG_STOP_MOVIE = 0x20000A1,
    CONTROL_MSG_PLAY_MOVIE = 0x20000A2,
};

// A movie (type 0x22, vtable 0x00814540): a video stream drawn on the screen over its frame's place
// (the frame off), fitted to it keeping the video's shape, muted, played again at its end; not taking
// input.
class C_MovieControl : public C_Control {               // 0x8C bytes
public:
    C_MovieControl(I3D_scene *scene, uint32_t id, C_Control *parent) : C_Control(scene, id, parent) { m_takesInput = false; }
    ~C_MovieControl() override;                         // 0x006610e0 (0x006610c0): the stream closed, released

    // Playing: the next picture drawn (driver state 3 off, the render state 0 kept); at its end
    // stopped, fitted again, played again.
    void Refresh() override;                            // 0x00668300
    // The control's (false: not made); the frame off, the stream made (none: false), the area found.
    bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc) override;   // 0x00668140
    bool HandleMessage(S_ControlMessage *message) override;   // 0x006681a0

protected:
    // The frame visual's world bound on the screen in pixels (its y negated and by 4:3: down; false:
    // no frame).
    bool FindArea();                                    // 0x00667f90
    // The stream's rectangle: the area's width, the video's shape (too tall: the area's height,
    // centred across).
    void FitRect();                                     // 0x00668050
    // The stream closed and released.
    void CloseStream();                                 // 0x006683c0

    ISND_stream *m_stream = nullptr;                    // +0x6C
    float m_left = 0.0f;                                // +0x70
    float m_bottom = 0.0f;                              // +0x74 (the bound's min y)
    float m_right = 0.0f;                               // +0x78
    float m_top = 0.0f;                                 // +0x7C (the bound's max y)
    float m_width = 0.0f;                               // +0x80
    float m_height = 0.0f;                              // +0x84
    uint32_t m_unknown88 = 0;                           // +0x88
};
