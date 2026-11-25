// Net part 11a: the system calls net/net_game.cpp makes (the locks, the clocks, the key socket, the query thread),
// renamed to the test's fakes (ng_diff.cpp) when it is built for the test (build_ng.bat: -include ng_fakes.h). The
// original's calls go to the same fakes through its import table; a critical section other than the net game's two
// goes to the real call.
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <ctime>

extern "C" void WINAPI FakeEnterCriticalSection(LPCRITICAL_SECTION section);
extern "C" void WINAPI FakeLeaveCriticalSection(LPCRITICAL_SECTION section);
extern "C" void WINAPI FakeInitializeCriticalSection(LPCRITICAL_SECTION section);
extern "C" void WINAPI FakeDeleteCriticalSection(LPCRITICAL_SECTION section);
extern "C" DWORD WINAPI FakeTimeGetTime();
extern "C" int WSAAPI FakeClosesocket(SOCKET s);
extern "C" DWORD WINAPI FakeWaitForSingleObject(HANDLE handle, DWORD ms);
extern "C" BOOL WINAPI FakeCloseHandle(HANDLE handle);
extern "C" clock_t FakeClock();
#define EnterCriticalSection FakeEnterCriticalSection
#define LeaveCriticalSection FakeLeaveCriticalSection
#define InitializeCriticalSection FakeInitializeCriticalSection
#define DeleteCriticalSection FakeDeleteCriticalSection
#define timeGetTime FakeTimeGetTime
#define closesocket FakeClosesocket
#define WaitForSingleObject FakeWaitForSingleObject
#define CloseHandle FakeCloseHandle
#define clock FakeClock
