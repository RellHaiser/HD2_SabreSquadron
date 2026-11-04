// Harness stub of game/controls.h: the browser's server entry only (the same layout).
#pragma once

#include <cstdint>

struct S_ServerEntry {
    char name[0x33];                                    // +0x00
    char map[0x3D];                                     // +0x33
    int gameType;                                       // +0x70
    uint32_t ping;                                      // +0x74
    uint32_t maxPlayers;                                // +0x78
    uint32_t players;                                   // +0x7C
    bool dedicated;                                     // +0x80
    uint8_t unknown81;
    bool password;                                      // +0x82
    uint8_t unknown83[5];
    bool voice;                                         // +0x88
    uint8_t unknown89[3];
    int versionMinor;                                   // +0x8C
    int versionMajor;                                   // +0x90
};
