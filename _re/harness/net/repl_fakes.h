// Net part 8: the performance counter net/net_object.cpp reads, renamed to the test's fake (repl_diff.cpp) when it is
// built for the test (build_repl.bat: -include repl_fakes.h). The original's calls go to the same fakes through its
// import table.
#pragma once

#include <winsock2.h>
#include <windows.h>

extern "C" BOOL WINAPI FakeQueryPerformanceCounter(LARGE_INTEGER *now);
extern "C" BOOL WINAPI FakeQueryPerformanceFrequency(LARGE_INTEGER *frequency);
#define QueryPerformanceCounter FakeQueryPerformanceCounter
#define QueryPerformanceFrequency FakeQueryPerformanceFrequency
