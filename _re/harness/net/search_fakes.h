// Net part 12c: the system calls net/net_search.cpp makes (the key socket's, Sleep, rand), renamed to the test's fakes
// (search_diff.cpp) when it is built for the test (build_search.bat: -include search_fakes.h). The original's calls go
// to the same fakes through its import table (rand: its CRT's, redirected by address).
#pragma once

#include <winsock2.h>
#include <windows.h>
#include <cstdlib>

extern "C" int WSAAPI FakeWSAStartup(WORD version, LPWSADATA data);
extern "C" SOCKET WSAAPI FakeSocket(int family, int type, int protocol);
extern "C" int WSAAPI FakeBind(SOCKET s, const sockaddr *name, int size);
extern "C" int WSAAPI FakeSelect(int count, fd_set *readable, fd_set *writable, fd_set *errors, const timeval *wait);
extern "C" int WSAAPI FakeRecvfrom(SOCKET s, char *data, int size, int flags, sockaddr *from, int *fromSize);
extern "C" int WSAAPI FakeSendto(SOCKET s, const char *data, int size, int flags, const sockaddr *to, int toSize);
extern "C" void WINAPI FakeSleep(DWORD ms);
extern "C" int FakeRand();
namespace std {
using ::FakeRand;
}
#define WSAStartup FakeWSAStartup
#define socket FakeSocket
#define bind FakeBind
#define select FakeSelect
#define recvfrom FakeRecvfrom
#define sendto FakeSendto
#define Sleep FakeSleep
#define rand FakeRand
