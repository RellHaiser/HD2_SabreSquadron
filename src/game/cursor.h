// The menus' mouse cursor (vtable 0x008155d0, 0x54 bytes; one shared by the menus' controls,
// counted; part 9c-vi-a): the "CURZOR" frame of the menu's scene at the mouse's place, the
// buttons, the wheel, the id of the control under it (the frame data a ray hits) and its clicks.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class I3D_frame;
class I3D_scene;
class IImage;

#ifdef HD2_DEDICATED
// The server has no cursor: none of the client's C_Cursor functions is in it. Acquire gives null and
// the controls make their calls through that null pointer, which read and change nothing (inline
// constants), as the original server's controls show: Acquire null (C_Button / C_SelectList /
// C_Tooltip::Create), IsButtonDown false and GetStillTime 0 (C_Button / C_ScrollBar::Tick),
// GetScreenPosition writing nothing (C_SelectList::OnInput05). The other constants: MEDIUM.
class C_Cursor {
public:
    static C_Cursor *Acquire(I3D_scene *) { return nullptr; }
    void Release() {}
    bool Update(int) { return false; }
    void Draw() const {}
    uint32_t Pick(I3D_scene *, I3D_frame *) { return 0; }
    bool IsButtonDown(uint32_t) const { return false; }
    bool WasButtonDown(uint32_t) const { return false; }
    bool IsPressed(uint32_t) const { return false; }
    bool IsReleased(uint32_t) const { return false; }
    void GetPosition(S_vector *) const {}
    void GetMove(S_vector *) const {}
    void GetScreenPosition(float *, float *, float *) const {}
    bool HasMoved() const { return false; }
    float GetWheel() const { return 0.0f; }
    bool HasWheel() const { return false; }
    int GetStillTime() const { return 0; }
    void SetDelay(int) {}
    void SetClicksOff(bool) {}
    uint32_t GetHoveredId() const { return 0; }
    bool IsClicked() const { return false; }
    bool IsDoubleClicked() const { return false; }
};
#else
class C_Cursor {
public:
    virtual ~C_Cursor();                                // 0x0066aa50 (the scalar deleting one)

    // The shared cursor, made for `scene` the first time (null: it could not be made), its count raised.
    static C_Cursor *Acquire(I3D_scene *scene);         // 0x0066a9e0 (__fastcall)
    // Its count lowered (the last: its texture released, the shared one deleted).
    void Release();                                     // 0x0066aa80 (__fastcall)

    // The mouse read: the buttons, the move, the wheel; a delay counted down (false); nothing changed
    // (false); else the place moved (kept on the screen, the move cut by what it lost), the still
    // time reset, the clicks (a release over the control pressed on; a second within 400 ms: a
    // double click), the frame placed; true.
    bool Update(int time);                              // 0x0066a520
    // Drawn over the screen: its texture on a quad at the place, 30 pixels of a 1024 x 768
    // screen (the menu state's, after the scene).
    void Draw() const;                                  // 0x0066ab70 (__fastcall)
    // The id of the control under it: the frame data of what a ray from it (z -0.9, along +z)
    // hits under `frame` (none: 0), kept and returned.
    uint32_t Pick(I3D_scene *scene, I3D_frame *frame);  // 0x0066aac0

    // A button held now / in the last update (`buttons` 0: any).
    bool IsButtonDown(uint32_t buttons) const;          // 0x0066a3a0
    bool WasButtonDown(uint32_t buttons) const;         // 0x0066a380
    // Pressed / released in the last update.
    bool IsPressed(uint32_t buttons) const;             // 0x0066a3c0
    bool IsReleased(uint32_t buttons) const;            // 0x0066a3f0
    // Its place in the interface's coordinates (z -1).
    void GetPosition(S_vector *position) const;         // 0x0066a420
    // Its last move in the interface's coordinates (z -1).
    void GetMove(S_vector *move) const;                 // 0x0066a450
    // Its place on the screen in pixels (shared statics) and its buttons' bits (+0x30; each optional).
    void GetScreenPosition(float *x, float *y, float *buttons) const;   // 0x0066a480
    // Moved in the last update.
    bool HasMoved() const;                              // 0x0066a2f0 (__fastcall)
    // The wheel's last turn.
    float GetWheel() const { return m_wheel; }          // 0x0066a320 (__fastcall)
    bool HasWheel() const { return m_wheel != 0.0f; }   // 0x0066a330 (__fastcall)
    // The milliseconds the cursor has stayed as it is (the tooltips' wait, the buttons' repeat).
    int GetStillTime() const;
    // Updates skipped for `time` ms (the hot key control sets 200 after a binding).
    void SetDelay(int time);                            // 0x0066ab50
    // The clicks not tracked.
    void SetClicksOff(bool off) { m_clicksOff = off; }  // 0x0066ab60
    uint32_t GetHoveredId() const { return m_hoveredId; }
    bool IsClicked() const { return m_clicked; }
    bool IsDoubleClicked() const { return m_doubleClicked; }

protected:
    C_Cursor() { Reset(); }
    // Its fields cleared (not the texture, the buttons, the wheel).
    void Reset();                                       // 0x0066a7b0 (__fastcall)
    // The screen's size, the frame (none: false), the mouse placed (its last place, else the
    // screen's centre) and read, the frame placed and off, the texture (none: false).
    bool Init(I3D_scene *scene);                        // 0x0066a7e0
    // `buttons` against a state (0: any button).
    static bool Matches(uint32_t buttons, uint32_t state);   // 0x0066a350
    // The frame at the place.
    void PlaceFrame();

    int m_refs;                                         // +0x04
    I3D_frame *m_frame;                                 // +0x08 "CURZOR"
    uint32_t m_unknown0C = 0;
    float m_wheelTotal;                                 // +0x10
    float m_moveX;                                      // +0x14
    float m_moveY;                                      // +0x18
    float m_wheel = 0.0f;                               // +0x1C
    IImage *m_texture = nullptr;                        // +0x20 "x_kursor.tga"
    float m_screenWidth;                                // +0x24
    float m_screenHeight;                               // +0x28
    uint32_t m_lastButtons = 0;                         // +0x2C
    uint32_t m_buttons = 0;                             // +0x30
    uint32_t m_hoveredId;                               // +0x34 the picked id the last update kept
    uint32_t m_pickedId;                                // +0x38
    int m_pressTime;                                    // +0x3C since the press
    int m_stillTime;                                    // +0x40
    int m_delay;                                        // +0x44
    bool m_clicksOff;                                   // +0x48
    bool m_clicked = false;                             // +0x49
    bool m_doubleClicked = false;                       // +0x4A
    uint32_t m_pressedId;                               // +0x4C
    uint32_t m_clickedId;                               // +0x50 the last click's (for the double click)
};
#endif

// The cursor's place on the screen in pixels (-1 before the first: the screen's centre), kept
// across cursors.
extern float g_cursorX;                                 // 0x00840668
extern float g_cursorY;                                 // 0x0084066c
