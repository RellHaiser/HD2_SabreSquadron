// The multiplayer game's set-up (game/mp_game_setup.cpp, part 9h-i; the original's 0x00616c50 ..
// 0x0061bdf0 in part): the map list (GAMEDATA\mpmaplist.txt: the maps by game type, their objectives,
// allowed items and presets), the five rotations picked from it, the rules of each game type, the server's
// name, password and port; kept in the profile's MPSetings.sav (chunk 0x44D). The main menu's
// multiplayer settings own one; so does the dedicated server's start (0x006acc50).
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"

class C_chunk;
class C_MpGameSetup;
class C_XmlNode;

// A map's OBJECTIVE (0x18 bytes).
struct S_MpMapObjective {
    int axisText;                                   // +0x00 axis_text
    int alliedText;                                 // +0x04 allied_text
    int points;                                     // +0x08 value
    int type;                                       // +0x0C type="VIP": 1, else -1
    int team;                                       // +0x10 side: AXIS 0, ALLIED (or none) 1, else -1
    int vipUniform;                                 // +0x14 vip_uniform_id (VIP only; else -1)
};
// A map's allowed ITEM (0x0C bytes; the old list's: the id only, the others -1).
struct S_MpMapItem {
    int itemId;                                     // item_id
    int count;                                      // item_num
    int ammo;                                       // ammo_num
};

// A map of the map list (0x48 bytes; the original's strings sit unaligned at +0x01 / +0x05).
class C_MpMap {
public:
    // (Inline in the list's parser: the ints -1 but `frontline` 0.)
    C_MpMap(const C_str &name, const C_str &directory) : name(name), directory(directory) {}
    ~C_MpMap() = default;                           // 0x0061b470 (__fastcall)
    C_MpMap(const C_MpMap &) = delete;
    C_MpMap &operator=(const C_MpMap &) = delete;

    // An objective added (last); the count of them.
    int AddObjective(const S_MpMapObjective &objective);   // 0x006acbd0

    const char *GetName() const { return name.c_str(); }
    // The mission's directory.
    const char *GetDirectory() const { return directory.c_str(); }
    const std::vector<S_MpMapObjective> &GetObjectives() const { return objectives; }
    const std::vector<S_MpMapItem> &GetItems() const { return items; }
    // The allowed presets (ALLOWEDPRESETS' text; the equipment screen looks the presets' names up in it).
    const char *GetPresets() const { return presets.c_str(); }

    uint8_t unknown00 = 0;                          // +0x00
    C_str name;                                     // +0x01 name
    C_str directory;                                // +0x05 dir
    int aircraftAxis = -1;                          // +0x0C AIRCRAFTATACK axis
    int aircraftAllied = -1;                        // +0x10 AIRCRAFTATACK allied
    int balancedAxis = -1;                          // +0x14 BALANCED axis
    int balancedAllied = -1;                        // +0x18 BALANCED allied
    int frontline = 0;                              // +0x1C mode="FRONTLINE": 1
    int description = -1;                          // +0x20 desc (a text id)
    std::vector<S_MpMapObjective> objectives;       // +0x24
    std::vector<S_MpMapItem> items;                 // +0x34 ALLOWEDITEMS
    C_str presets;                                  // +0x44 ALLOWEDPRESETS
};

// The game types' indexes in the set-up's lists (E_MpGameType's order) and their GAMESTYLE names.
constexpr int MP_GAME_TYPES = 4;
// A list of maps (0x14 bytes: a vector and an int no code here touches).
struct S_MpMapGroup {
    std::vector<C_MpMap *> maps;                    // +0x00
    int unknown10 = 0;                              // +0x10 (not initialised by the original)
};
// A rotation: the maps picked for each game type (0x50 bytes; the maps are the list's).
struct S_MpRotation {
    // All emptied.
    void Clear();                                   // 0x00617b00 (__fastcall)
    // Chunks 0x1A4 / 0x1A6 / 0x1A8 / 0x1B2 (the game types in order; each map a chunk 0x4D2 of its name) up
    // to the end of the chunk read in: the names found in `setup`'s list of the type appended (the first
    // two: at most 999 characters of the chunk; the others: the rest of the chunk). Emptied first.
    void Load(C_chunk &chunk, const C_MpGameSetup &setup);   // 0x00617610
    void Save(C_chunk &chunk) const;                // 0x006172f0

    S_MpMapGroup types[MP_GAME_TYPES];              // +0x00
};
// A game type's rules (0x58 bytes: S_MultiplayerSettings +0x228 .. +0x27C in the same order).
struct S_MpGameRules {
    int maxClients;                                 // +0x00
    int pointLimit;                                 // +0x04
    int roundLimit;                                 // +0x08 min
    int roundCount;                                 // +0x0C
    int warmup;                                     // +0x10 s
    int respawnTime;                                // +0x14 s
    int allowRespawn;                               // +0x18
    int friendlyFire;                               // +0x1C
    int autoTeamBalance;                            // +0x20
    int thirdPersonView;                            // +0x24
    int spawnProtection;                            // +0x28 s
    int inverseDamage;                              // +0x2C
    int fallingDamage;                              // +0x30
    int unknown34;                                  // +0x34
    int unknown38;                                  // +0x38
    int allowVehicles;                              // +0x3C
    int autoRestart;                                // +0x40
    int coopDifficulty;                             // +0x44
    int coopLives;                                  // +0x48 (-1: unlimited)
    int allowCrosshair;                             // +0x4C
    int spawnOnStart;                               // +0x50
    int teamLives;                                  // +0x54
};
static_assert(sizeof(S_MpGameRules) == 0x58, "S_MpGameRules");
// The rotations saved.
constexpr int MP_ROTATIONS = 5;

class C_MpGameSetup {                               // 0x364 bytes
public:
    C_MpGameSetup() = default;                      // (inline: 0x00626740 in the menu's, 0x006acc50)
    // The maps deleted.
    ~C_MpGameSetup();                               // 0x006267f0 (__fastcall)
    C_MpGameSetup(const C_MpGameSetup &) = delete;
    C_MpGameSetup &operator=(const C_MpGameSetup &) = delete;

    // The rotations emptied, the list's maps deleted.
    void Clear();                                   // 0x00617b80 (__fastcall)
    // Cleared, then the map list read (mpmaplist.txt, else GameData\mpmaplist.txt; neither: nothing else):
    // the maps of each GAMESTYLE (deathmatch, teamplay, hd2multiplayer, cooperative), their objectives,
    // aircraft, balance, allowed items and presets, up to an element not known (or a MAP before any
    // GAMESTYLE, a GAMESTYLE of no known type, a BALANCED short of a side). Then the defaults: each game
    // type's rules, the server's name (text 0x7E9), no password, port 11001; `loaded` set.
    void LoadMapList();                             // 0x006189f0 (__fastcall)
    // The list's map of game type `type` (0 .. 3; another: 3) named `name` (case ignored, the last
    // first); none: null.
    C_MpMap *FindMap(int type, const C_str &name) const;   // 0x00617cc0
    // The chunk's (0x44D) children up to its end: the rotations (0x1B6), the rules (0x1B8; the older
    // 0x1AA / 0x1AE / 0x1B0 / 0x1B4 their first bytes), the name (0x1AB), the password (0x1AC), the port
    // (0x1AF), the options (0x1B7; the older 0x1AD). The chunk is not left.
    void Load(C_chunk &chunk);                      // 0x00618130
    // The rotations (0x1B6: their index 0x1B5, the maps), the options (0x1B7), the rules (0x1B8), the
    // name (0x1AB), the password (0x1AC), the port (0x1AF).
    void Save(C_chunk &chunk) const;                // 0x00617df0

    bool loaded = false;                            // +0x000 the map list was read
    C_str serverName;                               // +0x001 (unaligned in the original)
    C_str password;                                 // +0x005
    int port = 0;                                   // +0x00C
    int unknown10 = 0;                              // +0x010 (saved in 0x1B7)
    uint8_t unknown14 = 0;                          // +0x014
    uint8_t unknown15 = 0;                          // +0x015
    int unknown18 = 0;                              // +0x018
    S_MpMapGroup maps[MP_GAME_TYPES];               // +0x01C the list's (owned)
    int unknown6C = 0;                              // +0x06C (saved in 0x1B7; the older 0x1AD: 0)
    S_MpRotation rotations[MP_ROTATIONS];           // +0x070
    S_MpGameRules rules[MP_GAME_TYPES] = {};        // +0x200
    int unknown360 = 0;                             // +0x360

private:
    // The ITEMs among the element's (ALLOWEDITEMS version 0.1) appended to the map's items.
    static void LoadItems(C_XmlNode *node, C_MpMap *map);   // 0x006186e0 (ECX unused)
};
