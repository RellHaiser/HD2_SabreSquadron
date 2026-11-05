// game/perf_timer.cpp for the network game harness (build_game.bat): the same, with a clock the tests can move on
// (g_perfOffset ms added), so the 3-second timers fire without waiting.
#include "game/perf_timer.h"

#include <windows.h>

int64_t g_perfOffset;

namespace {
LARGE_INTEGER g_perfBase;
LARGE_INTEGER g_perfFrequency = {{1, 0}};
}  // namespace

void InitPerfTime()
{
    QueryPerformanceFrequency(&g_perfFrequency);
    QueryPerformanceCounter(&g_perfBase);
}

int64_t GetPerfTimeMs()
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart - g_perfBase.QuadPart) * 1000 / g_perfFrequency.QuadPart + g_perfOffset;
}

S_PerfTimer::S_PerfTimer() : start(static_cast<int>(GetPerfTimeMs())), period(0), running(false) {}

S_PerfTimer::S_PerfTimer(int periodMs) : start(static_cast<int>(GetPerfTimeMs())), period(periodMs), running(false) {}

void S_PerfTimer::Start()
{
    start = static_cast<int>(GetPerfTimeMs());
    running = true;
}

bool S_PerfTimer::Fire()
{
    if (!running)
        return false;
    if (!(period + start < static_cast<int>(GetPerfTimeMs())))
        return false;
    start = static_cast<int>(GetPerfTimeMs());
    return true;
}
