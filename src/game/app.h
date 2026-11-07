// Application core: startup, data archives, engine initialisation, the main loop
// and shutdown (original source files 0x006db2c0-0x006ddb00).
#pragma once

#include <cstdint>
#include <windows.h>

// Video/audio configuration written by setup.exe into the registry value
// HKLM\Software\Illusion Softworks\Hidden & Dangerous 2\LS3D_setup (0x23 bytes).
#pragma pack(push, 1)
struct S_LS3DSetup {
    uint8_t version;              // +0x00 must be 0x0C
    uint8_t adapter;              // +0x01 display adapter index
    uint32_t width;               // +0x02
    uint32_t height;              // +0x06
    uint32_t bitsPerPixel;        // +0x0A
    uint32_t refreshRate;         // +0x0E
    uint8_t antialiasing;         // +0x12 2 or 4 samples
    uint8_t unknown13;            // +0x13
    uint8_t unknown14;            // +0x14
    uint8_t fullscreen;           // +0x15
    uint8_t hardwareTnl;          // +0x16
    uint8_t vsync;                // +0x17 (fullscreen only)
    uint8_t renderState15;        // +0x18 -> I3D state 0x15
    uint8_t tripleBuffer;         // +0x19 (fullscreen only)
    uint8_t unknown1A;            // +0x1A
    uint8_t renderState19;        // +0x1B -> I3D state 0x19
    uint8_t renderState11;        // +0x1C -> I3D state 0x11
    uint8_t soundDisabled;        // +0x1D
    uint8_t soundFlag10;          // +0x1E
    uint8_t waitInactive;         // +0x1F
    uint8_t soundFlag20;          // +0x20
    uint8_t soundQuality;         // +0x21 0..3, 4 = default
    uint8_t unknown22;            // +0x22
};
#pragma pack(pop)
static_assert(sizeof(S_LS3DSetup) == 0x23, "registry blob size");

extern S_LS3DSetup g_setup;                       // 0x008aedf8
extern HINSTANCE g_instance;                      // 0x008aedf4

int WINAPI WinMain(HINSTANCE instance, HINSTANCE prevInstance, LPSTR cmdLine, int showCmd);  // 0x006dd920
int RunGame();                                    // 0x006dceb0  returns 2 to request a restart

bool InitGame(HINSTANCE instance);                // 0x006dc390
void ShutdownGame();                              // 0x006dc770

// Milliseconds since the previous call, clamped to [minMs, maxMs]; sleeps when faster.
uint32_t LimitFrameTime(uint32_t minMs, uint32_t maxMs);   // 0x006dcd20
void SetKeyboardPolling(bool enable);             // 0x006dcea0
// Microseconds from the performance counter (the meleeTracker's sighting timestamps use its low half).
uint64_t MicrosecondsNow();
