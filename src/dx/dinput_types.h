// DirectInput 8 types and constants: the key codes (DIK_*, which the game also stores in its key
// bindings) that IGraph's keyboard and the window-message keys use. This build has no DirectInput
// backend; on Windows the codes come from the SDK's dinput.h, whose COM interfaces nothing here may
// use (build.py checks it).
#pragma once

#include <windows.h>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
