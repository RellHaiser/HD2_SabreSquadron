// Net part 10: the system call net/net_commands.cpp makes (the clock), renamed to the test's fake (cmds_diff.cpp) when
// it is built for the test (build_cmds.bat: -include cmds_fakes.h). The original's calls go to the same fake through
// its import table.
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

extern "C" DWORD WINAPI FakeTimeGetTime();
#define timeGetTime FakeTimeGetTime
