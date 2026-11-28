// vh_diff.cpp's fakes of system calls, force-included into every file of the test (build_vh.bat: -include vh_fakes.h):
// the port's timeGetTime is the test's clock (C_Vehicle::Settle steps until it has run its time). The original's is
// its import slot's (vh_diff.cpp SetUpVh).
#pragma once
#include <windows.h>
#include <mmsystem.h>

DWORD VhTimeGetTime();
#define timeGetTime VhTimeGetTime
