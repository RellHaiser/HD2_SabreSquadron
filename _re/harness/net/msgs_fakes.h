// Net part 9: the system calls net/net_messages.cpp makes (the app's command lock, the clock), renamed to the test's
// fakes (msgs_diff.cpp) when it is built for the test (build_msgs.bat: -include msgs_fakes.h). The original's calls go
// to the same fakes through its import table; a critical section other than the ones the test watches goes to the real
// call.
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

extern "C" void WINAPI FakeEnterCriticalSection(LPCRITICAL_SECTION section);
extern "C" void WINAPI FakeLeaveCriticalSection(LPCRITICAL_SECTION section);
extern "C" DWORD WINAPI FakeTimeGetTime();
#define EnterCriticalSection FakeEnterCriticalSection
#define LeaveCriticalSection FakeLeaveCriticalSection
#define timeGetTime FakeTimeGetTime
