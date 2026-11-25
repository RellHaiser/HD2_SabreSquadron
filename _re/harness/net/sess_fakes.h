// Net part 11b: the system calls net/net_session.cpp and net/net_game.cpp make (the locks, the clocks, COM, the
// window, the dedicated server's process), renamed to the test's fakes (sess_diff.cpp) when they are built for the
// test (build_sess.bat: -include sess_fakes.h). The original's calls go to the same fakes through its import table; a
// critical section other than the net game's two goes to the real call.
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
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
extern "C" HRESULT WINAPI FakeCoInitialize(LPVOID reserved);
extern "C" HRESULT WINAPI FakeCoInitializeEx(LPVOID reserved, DWORD flags);
extern "C" BOOL WINAPI FakeShowWindow(HWND window, int show);
extern "C" void WINAPI FakeGetStartupInfoA(LPSTARTUPINFOA info);
extern "C" BOOL WINAPI FakeCreateProcessA(LPCSTR application, LPSTR command, LPSECURITY_ATTRIBUTES process,
                                          LPSECURITY_ATTRIBUTES thread, BOOL inherit, DWORD flags, LPVOID environment,
                                          LPCSTR directory, LPSTARTUPINFOA startup, LPPROCESS_INFORMATION info);
#define EnterCriticalSection FakeEnterCriticalSection
#define LeaveCriticalSection FakeLeaveCriticalSection
#define InitializeCriticalSection FakeInitializeCriticalSection
#define DeleteCriticalSection FakeDeleteCriticalSection
#define timeGetTime FakeTimeGetTime
#define closesocket FakeClosesocket
#define WaitForSingleObject FakeWaitForSingleObject
#define CloseHandle FakeCloseHandle
#define clock FakeClock
#define CoInitialize FakeCoInitialize
#define CoInitializeEx FakeCoInitializeEx
#define ShowWindow FakeShowWindow
#undef GetStartupInfo
#undef CreateProcess
#define GetStartupInfoA FakeGetStartupInfoA
#define CreateProcessA FakeCreateProcessA
