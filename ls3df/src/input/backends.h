// The input device's backends, for ChooseDevice (input/input.cpp).
#pragma once

#include "input/input.h"

namespace ls3d::input {

// DirectInput (input/dinput): the Win32 platform's.
Device *DirectInputDevice();
// SDL3 (input/sdl3): the SDL3 platform's (its window's keyboard and mouse).
Device *Sdl3InputDevice();

}  // namespace ls3d::input
