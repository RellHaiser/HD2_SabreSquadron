// The menus' mouse cursor (vtable 0x008155d0, 0x54 bytes; one shared by the menus' controls,
// counted). Declared for the controls (part 9c-ii); its own code is part 9c-vi.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class I3D_scene;

class C_Cursor {
public:
    // The shared cursor, made for `scene` the first time (null: it could not be made), its count raised.
    static C_Cursor *Acquire(I3D_scene *scene);         // 0x0066a9e0 (__fastcall)
    // Its count lowered (the last: deleted).
    void Release();                                     // 0x0066aa80 (__fastcall)
    // A button held (`buttons` 0: any).
    bool IsButtonDown(uint32_t buttons) const;          // 0x0066a3a0
    // Its place in the interface's coordinates (z -1).
    void GetPosition(S_vector *position) const;         // 0x0066a420
    // Its last move in the interface's coordinates (z -1).
    void GetMove(S_vector *move) const;                 // 0x0066a450
    // The milliseconds the cursor has stayed as it is (+0x40: the tooltips' wait, the buttons' repeat).
    int GetStillTime() const;
};
