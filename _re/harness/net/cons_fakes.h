// Net part 5: the port's ShowWindow (net_console_commands.cpp's server / join) renamed to the test's fake
// (cons_diff.cpp); built into the test's files with -include cons_fakes.h.
#pragma once

#include <winsock2.h>
#include <windows.h>

BOOL WINAPI FakeShowWindow(HWND window, int show);
#define ShowWindow FakeShowWindow
