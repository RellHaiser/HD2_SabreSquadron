// Net part 4a: the system calls net/net_socket.cpp makes, renamed to the test's fakes (sock_diff.cpp) when it is built
// for the test (build_sock.bat: -include sock_fakes.h). The original's calls go to the same fakes through its import
// table. Winsock's and the thread calls only; the pure ones (htons, inet_ntoa, __WSAFDIsSet) stay real. Built into
// every file of the test (-include): outside the calls compared (g_sockFaking false) the fakes call the real functions
// (the loader's process and waits in original_exe.cpp).
#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>

extern bool g_sockFaking;

int WSAAPI Fake_WSAStartup(WORD version, LPWSADATA data);
int WSAAPI Fake_WSACleanup();
SOCKET WSAAPI Fake_socket(int af, int type, int protocol);
int WSAAPI Fake_bind(SOCKET s, const sockaddr *name, int length);
int WSAAPI Fake_closesocket(SOCKET s);
int WSAAPI Fake_sendto(SOCKET s, const char *buffer, int length, int flags, const sockaddr *to, int toLength);
int WSAAPI Fake_recvfrom(SOCKET s, char *buffer, int length, int flags, sockaddr *from, int *fromLength);
int WSAAPI Fake_select(int count, fd_set *reads, fd_set *writes, fd_set *errors, const timeval *wait);
int WSAAPI Fake_gethostname(char *name, int length);
hostent *WSAAPI Fake_gethostbyname(const char *name);
HANDLE WINAPI Fake_CreateThread(LPSECURITY_ATTRIBUTES attributes, SIZE_T stack, LPTHREAD_START_ROUTINE start, LPVOID parameter,
                                DWORD flags, LPDWORD id);
HANDLE WINAPI Fake_CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manual, BOOL initial, LPCSTR name);
BOOL WINAPI Fake_SetEvent(HANDLE event);
BOOL WINAPI Fake_ResetEvent(HANDLE event);
DWORD WINAPI Fake_WaitForSingleObject(HANDLE handle, DWORD ms);
BOOL WINAPI Fake_CloseHandle(HANDLE handle);
BOOL WINAPI Fake_SetThreadPriority(HANDLE thread, int priority);

#define WSAStartup Fake_WSAStartup
#define WSACleanup Fake_WSACleanup
#define socket Fake_socket
#define bind Fake_bind
#define closesocket Fake_closesocket
#define sendto Fake_sendto
#define recvfrom Fake_recvfrom
#define select Fake_select
#define gethostname Fake_gethostname
#define gethostbyname Fake_gethostbyname
#define CreateThread Fake_CreateThread
#define CreateEventA Fake_CreateEventA
#define SetEvent Fake_SetEvent
#define ResetEvent Fake_ResetEvent
#define WaitForSingleObject Fake_WaitForSingleObject
#define CloseHandle Fake_CloseHandle
#define SetThreadPriority Fake_SetThreadPriority
