// Harness stub of game/controls.h: the browser's server entry only (the same layout).
#pragma once

#include <cstddef>
#include <cstdint>

class C_chunk;

struct S_ServerEntry {
    // Not answered yet: the map " ??? ", no players, no flags, game type 4, ping 9999, not listed (the net
    // module's; part 9h-xx).
    void Reset();                                       // 0x007252c0 (__fastcall)
    // Its chunk 2000 (the strings with their lengths, then the fields; part 9h-xxii). True.
    bool Save(C_chunk &chunk) const;                    // 0x007253f0
    // The chunks left in the one entered: 2000 read (the strings 0x32 at most), the others skipped. True.
    bool Load(C_chunk &chunk);                          // 0x00725660

    char name[0x33];                                    // +0x00
    char map[0x35];                                     // +0x33
    uint32_t address;                                   // +0x68 (shown top byte first; part 9h-xii)
    int port;                                           // +0x6C
    int gameType;                                       // +0x70 (E_MpGameType)
    uint32_t ping;                                      // +0x74
    uint32_t maxPlayers;                                // +0x78
    uint32_t players;                                   // +0x7C
    bool dedicated;                                     // +0x80
    bool internet;                                      // +0x81 (the game info's "Internet Server")
    bool password;                                      // +0x82
    uint8_t unknown83;
    int unknown84;                                      // +0x84 (saved)
    bool voice;                                         // +0x88
    uint8_t unknown89[3];
    int versionMinor;                                   // +0x8C
    int versionMajor;                                   // +0x90
    int unknown94;                                      // +0x94 (saved)
    bool listed;                                        // +0x98 the browser's list shows it (pinging)
    uint8_t unknown99[3];
};
static_assert(offsetof(S_ServerEntry, gameType) == 0x70 && offsetof(S_ServerEntry, voice) == 0x88 &&
                  offsetof(S_ServerEntry, address) == 0x68 && offsetof(S_ServerEntry, internet) == 0x81 &&
                  offsetof(S_ServerEntry, versionMajor) == 0x90 && offsetof(S_ServerEntry, listed) == 0x98 &&
                  sizeof(S_ServerEntry) == 0x9C, "S_ServerEntry layout");
