// The interface's controls (game/control.cpp, part 9c): the widget toolkit under the menus' screens.
// A control wraps a frame of the menu's scene (found by name from its description), owns its child
// controls and talks by messages: a description's messages are handled when the control is made
// (routes among them: on a notification, send a message, to a control by id through the global
// queue or to the control itself), and callbacks are called with its notifications. Part 9c-i: the
// base (vtable 0x00813dd0), the messages, the descriptions and the queue.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "common/c_str.h"

class I3D_frame;
class I3D_scene;
class C_Control;

// A message to a control (0x18 bytes): its target (a control's id; in a route, -1: the control
// itself), its code (CONTROL_MSG_*; a route's carries a notification too, CONTROL_NOTIFY_MASK), its
// values (the callbacks': the notification, the object and the function, x86 pointers) and a text.
struct S_ControlMessage {
    uint32_t target = 0;                                // +0x00
    uint32_t id = 0;                                    // +0x04
    uint32_t value = 0;                                 // +0x08
    uintptr_t value2 = 0;                               // +0x0C
    uintptr_t value3 = 0;                               // +0x10
    C_str text;                                         // +0x14
};

// The messages' codes (S_ControlMessage::id & CONTROL_MSG_MASK) the base handles.
constexpr uint32_t CONTROL_MSG_MASK = 0x3000FFF;
enum : uint32_t {
    CONTROL_MSG_DEACTIVATE = 0x1000001,                 // inactive (SetInactive), unless already
    CONTROL_MSG_ACTIVATE = 0x1000002,
    CONTROL_MSG_SHOW = 0x1000003,
    CONTROL_MSG_HIDE = 0x1000004,
    CONTROL_MSG_IGNORE_INPUT = 0x1000005,
    CONTROL_MSG_TAKE_INPUT = 0x1000006,
    CONTROL_MSG_ADD_CALLBACK = 0x1000007,               // value: the notification, value2 / 3: object, function
    CONTROL_MSG_REMOVE_CALLBACK = 0x1000008,            // by the object, else the notification, else the function
    CONTROL_MSG_SET_INDEX = 0x1000009,
    CONTROL_MSG_GET_INDEX = 0x100000A,                  // (the message's value set)
    CONTROL_MSG_SET_VALUE = 0x100000B,
    CONTROL_MSG_GET_VALUE = 0x100000C,
    CONTROL_MSG_NOTIFY = 0x10000A1,                     // the notification in the value raised
    CONTROL_MSG_REFRESH = 0x200002C,                    // passed to the children (the text controls lay out again)
};
// The notifications (Notify; in a route's id with its message's code).
constexpr uint32_t CONTROL_NOTIFY_MASK = 0x40FF000;
enum : uint32_t {
    CONTROL_NOTIFY_CLICK = 0x4001000,
    CONTROL_NOTIFY_02 = 0x4002000,
    CONTROL_NOTIFY_ENTER = 0x4003000,                   // the cursor over the control
    CONTROL_NOTIFY_LEAVE = 0x4004000,
    CONTROL_NOTIFY_05 = 0x4005000,
    CONTROL_NOTIFY_PRESS = 0x4006000,                   // (a held button's: again every 300 ms)
    CONTROL_NOTIFY_RELEASE = 0x4007000,
    CONTROL_NOTIFY_08 = 0x4008000,
    CONTROL_NOTIFY_09 = 0x4009000,
    CONTROL_NOTIFY_0A = 0x400A000,
    CONTROL_NOTIFY_0B = 0x400B000,
    CONTROL_NOTIFY_HIDDEN = 0x400C000,
    CONTROL_NOTIFY_SHOWN = 0x400D000,
    CONTROL_NOTIFY_WHEEL = 0x4011000,                   // (the amount not passed on)
    CONTROL_NOTIFY_KEY_CLICK = 0x4012000,               // (then the click)
    CONTROL_NOTIFY_CHANGED = 0x4010000,                 // a value chosen (the scroll bar's, the spin control's)
    CONTROL_NOTIFY_13 = 0x4013000,
    CONTROL_NOTIFY_ANY = 0x4021000,                     // a callback for every notification (instead of the list)
};
// What the input slots return: handled, or not (the base's).
constexpr int CONTROL_HANDLED = 1;
constexpr int CONTROL_NOT_HANDLED = 7;

// A callback (the original's a __thiscall member of `object`, the object also its first argument;
// 0 for the CONTROL_NOTIFY_ANY one): its result returned by Notify's callbacks.
using ControlCallback = bool (*)(void *object, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);
struct S_ControlCallback {                              // 0x0C bytes
    uint32_t notification = 0;
    void *object = nullptr;
    ControlCallback function = nullptr;
};

// A control's description (the menus build them in code): its frame's name (a mask), its id, its
// kind (CreateChild's), the messages it handles when made, its strings and its children's.
struct S_ControlDesc {                                  // 0x40 bytes, vtable 0x00813ab0
    S_ControlDesc(int type, const char *name, uint32_t id);   // 0x00615090
    // Its children deleted (the last first).
    virtual ~S_ControlDesc();                           // 0x006151b0 (0x006151d0)
    S_ControlDesc(const S_ControlDesc &) = delete;
    S_ControlDesc &operator=(const S_ControlDesc &) = delete;

    C_str name;                                         // +0x04
    uint32_t id;                                        // +0x08
    int type;                                           // +0x0C
    std::vector<S_ControlMessage> messages;             // +0x10
    std::vector<C_str> strings;                         // +0x20
    std::vector<S_ControlDesc *> children;              // +0x30
};
// A message added to a description's (after the others).
void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value);   // 0x00614e80
void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2);   // 0x00614f10
void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2,
                    uintptr_t value3);                  // 0x0061e100 (the callbacks')
void AddDescMessage(S_ControlDesc *desc, uint32_t target, uint32_t id, uint32_t value, const C_str &text);   // 0x00614fa0

// The messages posted to controls by id (the menu state delivers them, part 9d).
extern std::deque<S_ControlMessage> g_controlMessages;  // 0x008ae590
void PostControlMessage(const S_ControlMessage &message);   // 0x00615a90 (the deque's push_back)

// A control: its frame, its child controls (their ids its own with their index in the next of its
// id's fields: bits 20-25, then 12-19, then 4-11), its routes and callbacks; shown, inactive (shown
// as such), taking the cursor's input.
class C_Control {                                       // 0x6C bytes, vtable 0x00813dd0
public:
    C_Control(I3D_scene *scene, uint32_t id, C_Control *parent);   // 0x0064dfe0
    C_Control(const C_Control &) = delete;
    C_Control &operator=(const C_Control &) = delete;

    // The index of the child with `id`'s index in the field after this control's (none: -1).
    virtual int FindChild(uint32_t id) const;           // +0x00 0x0065cb50
    virtual bool IsChild(int index) const;              // +0x04 0x0064dce0
    // Shown, not inactive and taking input.
    virtual bool TakesInput() const;                    // +0x08 0x0064dd10
    // The frame shown while the cursor is over the control (the text buttons'; the base: none).
    virtual I3D_frame *GetHighlightFrame() const { return nullptr; }   // +0x0C 0x0064dd30
    // The frame on (or off) and the notification.
    virtual void Show();                                // +0x10 0x0065ca80
    virtual void Hide();                                // +0x14 0x0065cab0
    // The shown control's children refreshed (the base has nothing of its own).
    virtual void Refresh();                             // +0x18 0x00661c30
    virtual void Tick(int time) {}                      // +0x1C 0x0064dd40
    virtual bool Unknown20(int) { return false; }       // +0x20 0x0064dd50
    virtual bool Unknown24(int) { return false; }       // +0x24 0x0064dd60
    virtual bool Unknown28() { return false; }          // +0x28 0x0064dd70
    // The frame taken (its data the control's id) and the description's children made under it
    // (`root`: the screen's frames, passed on).
    virtual bool Create(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc);   // +0x2C 0x00661bc0
    // The cursor's input (the argument: the control's id): its notification, not handled.
    virtual int OnEnter(int id);                        // +0x30 0x0064dd80
    virtual int OnLeave(int id);                        // +0x34 0x0064dda0
    virtual int OnClick(int id);                        // +0x38 0x0064ddc0
    virtual int OnInput02(int id);                      // +0x3C 0x0064dde0
    virtual int OnInput05(int id);                      // +0x40 0x0064de00
    virtual int OnRelease(int id);                      // +0x44 0x0064de20
    virtual int OnPress(int id);                        // +0x48 0x0064de40
    // The mouse wheel's turn by `amount` (the base drops it).
    virtual int OnWheel(int id, int amount);            // +0x4C 0x0064de60
    virtual int OnInput08(int id);                      // +0x50 0x0064de80
    virtual int OnInput09(int id);                      // +0x54 0x0064dea0
    virtual int OnInput0A(int id);                      // +0x58 0x0064dec0
    // The key's click: its notification, then OnClick's.
    virtual int OnKeyClick(int id);                     // +0x5C 0x0064dee0
    virtual int OnInput13(int id);                      // +0x60 0x0064df00
    virtual int OnInput0B(int id);                      // +0x64 0x0064df20
    virtual int Unknown68(int) { return CONTROL_NOT_HANDLED; }   // +0x68 0x0064df40
    // A message: a route kept; else its code handled (always true).
    virtual bool HandleMessage(S_ControlMessage *message);   // +0x6C 0x0065d060
    virtual bool HandleMessages(std::vector<S_ControlMessage> &messages);   // +0x70 0x0065d220
    // A copy handled (HandleMessage's result).
    virtual bool HandleMessageCopy(const S_ControlMessage &message);   // +0x74 0x0064df50
    // The description's strings (the base: none).
    virtual void SetStrings(const std::vector<C_str> &) {}   // +0x78 0x0065d050
    virtual void SetInactive(bool inactive) { m_inactive = inactive; }   // +0x7C 0x00661c20
    // The children deleted (the last first).
    virtual ~C_Control();                               // +0x80 0x0064e080 (0x0064e0a0)

    // The notification: the callbacks (their result dropped), then each route on it: its message
    // (the notification bits cleared) posted to its target, or handled by the control (-1).
    void Notify(uint32_t notification, uint32_t value);   // 0x0065d270
    uint32_t GetId() const { return m_id; }
    bool IsInactive() const { return m_inactive; }
    // The control with `id` among this one's descendants (`up`: first up the parents to the top,
    // then down; the ids' top 6 bits must match all the way; none: null).
    C_Control *FindControl(uint32_t id, bool up);       // 0x0065cae0
    // A child control from its description (the factory; part 9c's last).
    bool CreateChild(I3D_frame *frame, I3D_frame *root, const S_ControlDesc *desc);   // 0x0065d470

protected:
    // The first callback on `notification` (the CONTROL_NOTIFY_ANY one instead when set; none: true).
    bool CallCallbacks(uint32_t notification, uint32_t value);   // 0x0065cc20
    // CONTROL_MSG_ADD_CALLBACK / _REMOVE_CALLBACK (false: no notification or function).
    bool AddCallback(const S_ControlMessage &message);  // 0x0065cde0
    bool RemoveCallback(const S_ControlMessage &message);   // 0x0065ccc0

    std::vector<C_Control *> m_children;                // +0x04
    std::vector<S_ControlMessage> m_routes;             // +0x18
    std::vector<S_ControlCallback> m_callbacks;         // +0x2C
    S_ControlCallback m_anyCallback;                    // +0x40
    C_Control *m_parent;                                // +0x4C
    I3D_frame *m_frame = nullptr;                       // +0x50
    int m_type = 0;                                     // +0x54 the description's
    uint32_t m_value = 0;                               // +0x58 (CONTROL_MSG_SET_VALUE)
    int m_index = -1;                                   // +0x5C (CONTROL_MSG_SET_INDEX)
    uint32_t m_id;                                      // +0x60
    bool m_inactive = false;                            // +0x64
    bool m_shown = true;                                // +0x65
    bool m_takesInput = true;                           // +0x66
    I3D_scene *m_scene;                                 // +0x68 the menu's (its cursor's)
};
