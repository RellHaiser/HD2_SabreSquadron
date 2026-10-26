// The performance counter in milliseconds (original 0x00748da0 / 0x00748dc0) and the period timer
// on it (0x0073ee20 / 0x0073ee40 / 0x0073ee60 / 0x0073ee90), from the network library.
#include "game/perf_timer.h"

#include <windows.h>

namespace {
LARGE_INTEGER g_perfBase;                               // 0x00983108
LARGE_INTEGER g_perfFrequency = {{1, 0}};               // 0x00983110
}  // namespace

void InitPerfTime()                                     // 0x00748da0
{
    QueryPerformanceFrequency(&g_perfFrequency);
    QueryPerformanceCounter(&g_perfBase);
}

int64_t GetPerfTimeMs()                                 // 0x00748dc0
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    // The quotient in the game's float precision (the x87 in D3D's 24-bit mode), times 1000,
    // truncated.
    const float seconds = static_cast<float>(static_cast<double>(counter.QuadPart - g_perfBase.QuadPart) /
                                             static_cast<double>(g_perfFrequency.QuadPart));
    return static_cast<int64_t>(seconds * 1000.0f);
}

S_PerfTimer::S_PerfTimer() : start(static_cast<int>(GetPerfTimeMs())), period(0), running(false) {}   // 0x0073ee20

S_PerfTimer::S_PerfTimer(int periodMs)                  // 0x0073ee40
    : start(static_cast<int>(GetPerfTimeMs())), period(periodMs), running(false)
{
}

void S_PerfTimer::Start()                               // 0x0073ee90
{
    start = static_cast<int>(GetPerfTimeMs());
    running = true;
}

bool S_PerfTimer::Fire()                                // 0x0073ee60
{
    if (!running)
        return false;
    if (!(period + start < static_cast<int>(GetPerfTimeMs())))
        return false;
    start = static_cast<int>(GetPerfTimeMs());
    return true;
}
