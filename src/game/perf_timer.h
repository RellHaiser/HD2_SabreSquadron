// The performance counter in milliseconds and a period timer on it (the network library's;
// 0x00748da0 / 0x00748dc0, 0x0073ee20..0x0073ee90; game/perf_timer.cpp).
#pragma once

#include <cstdint>

// Milliseconds of the performance counter since the base count InitPerfTime took.
int64_t GetPerfTimeMs();                                // 0x00748dc0
// The base count and the frequency (the network's start-up 0x0072e1d0 calls it).
void InitPerfTime();                                    // 0x00748da0

// A period on the performance counter (12 bytes; C_Car +0x2470, the network's 0x0072cde0): once
// running, Fire is true when the period has passed since the start and starts it again.
struct S_PerfTimer {
    // Started now, not running (Fix: the period 0; the original leaves it unset).
    S_PerfTimer();                                      // 0x0073ee20
    explicit S_PerfTimer(int periodMs);                 // 0x0073ee40 (started now, not running)
    // Starts it now.
    void Start();                                       // 0x0073ee90
    bool Fire();                                        // 0x0073ee60

    int start;                                          // +0x00 ms (GetPerfTimeMs)
    int period;                                         // +0x04 ms
    bool running;                                       // +0x08 (a dword in the original)
};
