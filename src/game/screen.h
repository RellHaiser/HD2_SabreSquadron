// The menus' screens (game/screen.cpp, part 9d-i): a screen is a control group on a model of the
// menu's scene (one per screen, loaded by the menu state), its controls made from its description;
// the menus build the descriptions in code. The message box is the screen the menu states share
// for their questions ("confirm": a text and yes / no / ok buttons).
#pragma once

#include <cstdint>
#include <vector>

#include "game/controls.h"

class C_Cursor;

// A screen's description (the menus build them in code): its model's file and frame name, its id
// (its index + 1 in the top 6 bits), the messages the screen handles when made, its strings and
// its controls'.
struct S_ScreenDesc {                                   // 0x44 bytes, vtable 0x00813abc
    S_ScreenDesc(const char *name, const char *file, uint32_t id);   // 0x00626000
    // Its controls' descriptions deleted (the last first).
    virtual ~S_ScreenDesc();                            // 0x00626170 (0x00626190)
    S_ScreenDesc(const S_ScreenDesc &) = delete;
    S_ScreenDesc &operator=(const S_ScreenDesc &) = delete;

    // A control's description added (after the others; its id the screen's with its index + 1 in
    // the first field, bits 20-25).
    S_ControlDesc *AddControl(int type, const char *name);   // 0x00615360

    std::vector<S_ControlMessage> messages;             // +0x04
    std::vector<C_str> strings;                         // +0x14
    std::vector<S_ControlDesc *> controls;              // +0x24
    C_str file;                                         // +0x38 the model's
    C_str name;                                         // +0x3C its frame's
    uint32_t id;                                        // +0x40
};
// A message added to a screen description's (after the others; the original's arguments by value).
void AddScreenMessage(S_ScreenDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2,
                      uintptr_t value3);                // 0x00625f60 (__cdecl)
void AddScreenMessage(S_ScreenDesc *desc, uint32_t target, uint32_t id, uint32_t value, uintptr_t value2);   // 0x00625ed0 (DS 0x006551b0)

// The ids' top 6 bits: a screen's index + 1.
constexpr int SCREEN_ID_SHIFT = 26;

// A menu's screens' descriptions.
struct S_MenuDesc {                                     // 0x14 bytes, vtable 0x00813ab8
    S_MenuDesc() = default;                             // (inline)
    // The screens' descriptions deleted (the last first).
    virtual ~S_MenuDesc();                              // 0x00626400 (0x00626420)
    S_MenuDesc(const S_MenuDesc &) = delete;
    S_MenuDesc &operator=(const S_MenuDesc &) = delete;

    // A screen's description added (after the others; its id its index + 1 in the top bits).
    S_ScreenDesc *AddScreen(const char *name, const char *file);   // 0x00626300

    std::vector<S_ScreenDesc *> screens;                // +0x04
};

// A screen: its controls (a group; the child under the cursor its focus) on its model's frame,
// shown and hidden with the frame; the keys passed to the focus (Enter, Space: its click).
class C_Screen : public C_ControlGroup {                // 0x84 bytes, vtable 0x00815540
public:
    // No parent, no id (the description's, Build); the shared cursor acquired.
    C_Screen(I3D_scene *scene, I3D_frame *frame);       // 0x00668420
    // The cursor released.
    ~C_Screen() override;                               // 0x00668550 (0x00668530)

    // The frame shown: off, the focus left (OnLeave(0)), CONTROL_NOTIFY_HIDDEN.
    void Hide() override;                               // +0x14 0x00668740
    // CONTROL_NOTIFY_SCREEN_TICK, then the focus' Tick.
    void Tick(int time) override;                       // +0x1C 0x00668a50
    // A key (its DirectInput code's low byte): the active focus' (taken: true); else Enter, Space
    // or the keypad's Enter: the focus' click (its id); else false.
    bool Unknown20(int key) override;                   // +0x20 0x00668970 (DS 0x006a1eb0)
    // The base's for the screen's own (target 0) and the notifications; CONTROL_MSG_EDIT: the
    // focus left (OnLeave with its index), then the target's enter; then the target's child's
    // (none: false).
    bool HandleMessage(S_ControlMessage *message) override;   // +0x6C 0x006687f0
    // (+0x70 0x00668520: the base's HandleMessages.)

    // The frame hidden: the enter (`id`), the frame on, CONTROL_NOTIFY_SHOWN.
    virtual void Open(int id);                          // +0x84 0x00668700
    // A popup of its own shown (the menu state draws the screen again under it; the base: none).
    virtual bool HasPopup() const { return false; }     // +0x88 0x00668510 (DS 0x006a1910)
    // The description's controls made (on the screen's frame), its name and id taken; true.
    virtual bool Build(const S_ScreenDesc *desc);       // +0x8C 0x00668770

    I3D_frame *GetScreenFrame() const { return m_screenFrame; }
    const C_str &GetName() const { return m_name; }

protected:
    C_Cursor *m_cursor;                                 // +0x70
    I3D_frame *m_screenFrame;                           // +0x74 the model's (the menu's)
    // (+0x78 not used by the screens.)
    C_str m_name;                                       // +0x7C the description's
    bool m_80 = false;                                  // +0x80 (zeroed; not read by the screens)
};

class C_TitleControl;

// The description popup (the cooperative game's player screen's, its model "DESCRIPTION"): shown,
// a text box that follows the cursor once it has stayed still for half a second (its button up);
// the menu state draws it over the top screen (HasPopup). Its controls are not described.
class C_DescriptionScreen : public C_Screen {           // 0x8C bytes, vtable 0x008156b8
public:
    C_DescriptionScreen(I3D_scene *scene, I3D_frame *frame) : C_Screen(scene, frame) {}   // 0x006838a0
    // The text deleted.
    ~C_DescriptionScreen() override;                    // 0x00676040 (0x00676020)

    // The text's.
    void Refresh() override;                            // +0x18 0x00669bf0 (DS 0x006a3200)
    // Shown and the cursor still (500 ms, button 0 up): the box on at the cursor (+0.15, +0.015;
    // its own depth kept), placed, the text refreshed; else the box off (when it was shown or on).
    // (Not the screen's: no tick notification.)
    void Tick(int time) override;                       // +0x1C 0x00669aa0
    // CONTROL_MSG_SHOW / HIDE: shown / hidden (the box with it); the others the text's.
    bool HandleMessage(S_ControlMessage *message) override;   // +0x6C 0x00669a50
    // The box shown.
    bool HasPopup() const override { return m_boxShown; }     // +0x88 0x00676010
    // The model's "text" (a title, the screen's id | 2) made, its "description" box off at (0.5,
    // -0.375, -0.5), "description01" off; true. (The description is not read: the screen gets no name
    // or id.)
    bool Build(const S_ScreenDesc *desc) override;      // +0x8C 0x006698c0

private:
    C_TitleControl *m_text = nullptr;                   // +0x84
    bool m_boxShown = false;                            // +0x88
    bool m_shown = false;                               // +0x89
};

// The squad screen's medal popup (its model "DESCRIPTION"; vtable 0x00815918, made inline by
// C_SquadMenu::BuildScreens): a medal's frame, name and description in a box that follows the cursor
// once it has stayed still for half a second (its button up), as C_DescriptionScreen's.
class C_MedalDescriptionScreen : public C_Screen {      // 0x98 bytes
public:
    C_MedalDescriptionScreen(I3D_scene *scene, I3D_frame *frame) : C_Screen(scene, frame) {}
    // The texts deleted.
    ~C_MedalDescriptionScreen() override;               // 0x00691920 (0x00691900)

    // The texts'.
    void Refresh() override;                            // +0x18 0x0066a2a0
    // Shown and the cursor still: the box on at the cursor (C_DescriptionScreen::Tick's), both texts
    // refreshed; else the box off.
    void Tick(int time) override;                       // +0x1C 0x0066a100
    // CONTROL_MSG_SHOW / HIDE: shown / hidden (the box with it); MEDAL_POPUP_MSG_SHOW: the medal `value`
    // (its frame value2, in the "new_medal" place; the one before unlinked, the texts cleared), its name
    // and description (g_rosterTexts' 0x2774 + 2 * value, + 1; style 3). True; the others ignored.
    bool HandleMessage(S_ControlMessage *message) override;   // +0x6C 0x00669e90
    // The box shown.
    bool HasPopup() const override { return m_boxShown; }     // +0x88 0x006918f0
    // The model's "description01" box off at (0.5, -0.375, -0.5), its "text01" and "text02" made (titles,
    // the screen's id | 2) and the "new_medal" place (a dummy) kept; "description" off. True. (The
    // description is not read.)
    bool Build(const S_ScreenDesc *desc) override;      // +0x8C 0x00669c00 (DS 0x006a3220)

private:
    C_TitleControl *m_name = nullptr;                   // +0x84 "text01"
    C_TitleControl *m_description = nullptr;            // +0x88 "text02"
    I3D_frame *m_place = nullptr;                       // +0x8C "new_medal"
    I3D_frame *m_medal = nullptr;                       // +0x90 the medal's frame shown
    bool m_boxShown = false;                            // +0x94
    bool m_shown = false;                               // +0x95
};
// The medal popup's message: the medal (value) and its frame (value2) shown.
constexpr uint32_t MEDAL_POPUP_MSG_SHOW = 0x1000033;

// The screens' notification on every tick (value: the time).
constexpr uint32_t CONTROL_NOTIFY_SCREEN_TICK = 0x4014000;

// The message box's kinds: none shown; a text alone; yes / no; ok; yes / no, the text's second style.
enum E_MessageBoxKind : int {
    MESSAGE_BOX_NONE = -1,
    MESSAGE_BOX_TEXT = 0,
    MESSAGE_BOX_YES_NO = 1,
    MESSAGE_BOX_OK = 2,
    MESSAGE_BOX_YES_NO_2 = 3,
};
// Posted to the menu (target 0) when the box is shown and closed.
enum : uint32_t {
    MENU_MSG_MESSAGE_BOX_SHOWN = 0x2000096,
    MENU_MSG_MESSAGE_BOX_CLOSED = 0x2000097,
};

// The message box ("confirm"; made by the menu state, 0x0066c4d0): a text and three buttons; a
// button or Escape closes it and posts the menu the message given for the answer (yes and ok: the
// first, no and Escape: the second; 0: none).
class C_MessageBox : public C_Screen {                  // 0x90 bytes, vtable 0x008155d8
public:
    C_MessageBox(I3D_scene *scene, I3D_frame *frame) : C_Screen(scene, frame) {}   // (inline)
    // (0x0066c6d0 (0x0066c6b0): the screen's.)

    // Shown (none shown before): the buttons of the kind shown, the others hidden (kinds above 3:
    // left), the text set (kind 3: the second style), the answers' messages kept, shown posted.
    void ShowText(int kind, const C_str &text, uint32_t yesMessage, uint32_t noMessage);   // 0x00668ba0
    // The same with a text id.
    void ShowTextId(int kind, uint32_t textId, uint32_t yesMessage, uint32_t noMessage);   // 0x00668fd0
    // Closed (no answer posted).
    void Close();                                       // 0x00669390 (__fastcall)
    int GetKind() const { return m_kind; }

    // None shown or a text alone: taken (true). Escape: closed, the second answer; else the screen's.
    bool Unknown20(int key) override;                   // +0x20 0x006694b0 (DS 0x006a2c40)
    // Its controls added to the description first: the text ("text", text 2004), the yes / no / ok
    // buttons ("byes*", "bno*", "bok"; texts 2001, 2000, 2013; ok hidden), their clicks to OnButton.
    bool Build(const S_ScreenDesc *desc) override;      // +0x8C 0x00668a90

private:
    // A button's click (by its index: 2 yes, 3 no, 4 ok): none shown, true; else closed, the
    // answer posted; no control: false.
    static bool OnButton(void *object, void *argument, C_Control *control, uint32_t notification,
                         uint32_t value);               // 0x006696a0 (__thiscall)
    void ClearAnswers() { m_yesMessage = m_noMessage = 0; }   // 0x006694a0 (__fastcall)

    int m_kind = MESSAGE_BOX_NONE;                      // +0x84
    uint32_t m_yesMessage = 0;                          // +0x88
    uint32_t m_noMessage = 0;                           // +0x8C
};
