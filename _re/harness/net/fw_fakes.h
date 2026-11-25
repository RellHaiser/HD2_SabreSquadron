// Net part 12b: the system calls net/net_firewall.cpp and net/net_create.cpp make (the version, COM, the watcher's
// thread and windows, the strings and variants), renamed to the test's fakes (fw_diff.cpp) when they are built for
// the test (build_fw.bat: -include fw_fakes.h). The original's calls go to the same fakes through its import table.
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>

extern "C" BOOL WINAPI FakeGetVersionExA(LPOSVERSIONINFOA info);
extern "C" HRESULT WINAPI FakeCoInitialize(LPVOID reserved);
extern "C" HRESULT WINAPI FakeCoInitializeSecurity(PSECURITY_DESCRIPTOR descriptor, LONG count, SOLE_AUTHENTICATION_SERVICE *services,
                                                   void *reserved1, DWORD authentication, DWORD impersonation, void *list,
                                                   DWORD capabilities, void *reserved3);
extern "C" HRESULT WINAPI FakeCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, LPVOID *object);
extern "C" HANDLE WINAPI FakeCreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID parameter,
                                          DWORD flags, LPDWORD id);
extern "C" DWORD WINAPI FakeGetCurrentThreadId();
extern "C" BOOL WINAPI FakePostMessageA(HWND window, UINT message, WPARAM w, LPARAM l);
extern "C" BOOL WINAPI FakeGetWindowRect(HWND window, LPRECT rect);
extern "C" BOOL WINAPI FakeEnumThreadWindows(DWORD thread, WNDENUMPROC callback, LPARAM l);
extern "C" BOOL WINAPI FakeEnumChildWindows(HWND parent, WNDENUMPROC callback, LPARAM l);
extern "C" void WINAPI FakeSleep(DWORD ms);
extern "C" BSTR WINAPI FakeSysAllocString(const OLECHAR *text);
extern "C" void WINAPI FakeSysFreeString(BSTR text);
extern "C" void WINAPI FakeVariantInit(VARIANTARG *v);
extern "C" HRESULT WINAPI FakeVariantClear(VARIANTARG *v);
#define GetVersionExA FakeGetVersionExA
#define CoInitialize FakeCoInitialize
#define CoInitializeSecurity FakeCoInitializeSecurity
#define CoCreateInstance FakeCoCreateInstance
#define CreateThread FakeCreateThread
#define GetCurrentThreadId FakeGetCurrentThreadId
#define PostMessageA FakePostMessageA
#define GetWindowRect FakeGetWindowRect
#define EnumThreadWindows FakeEnumThreadWindows
#define EnumChildWindows FakeEnumChildWindows
#define Sleep FakeSleep
#define SysAllocString FakeSysAllocString
#define SysFreeString FakeSysFreeString
#define VariantInit FakeVariantInit
#define VariantClear FakeVariantClear
