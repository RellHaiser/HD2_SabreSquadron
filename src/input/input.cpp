// The input device's choice of backend (input/input.h).
#include "input/input.h"

#include "input/backends.h"
#include "platform/windowing.h"

namespace ls3d::input {

void ChooseDevice()
{
    // SDL's keyboard and mouse belong to SDL's window: the SDL3 platform's device goes with it.
    const platform::Windowing *windowing = platform::CurrentWindowing();
    g_pInput = windowing && windowing != platform::Win32Windowing() ? Sdl3InputDevice() : DirectInputDevice();
}

}  // namespace ls3d::input
