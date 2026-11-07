// The soldier records humans and the game profile share: the campaign roster's soldier
// profiles (tables\HumanStore.def, profile.cpp's C_HumanStore), a human's stats and single
// player progress, and the soldier state a human is made from and saved back to.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"

class C_chunk;
class IImage;

// C_Human::GetStat() indices (m_stats). Names of 0..8 from the soldier screen and the single
// player experience messages; 3 and 5 are not shown there.
enum E_HumanStat {
    STAT_HEALTH = 0,                      // 800 = full
    STAT_STRENGTH = 1,                    // also what the human can carry
    STAT_ENDURANCE = 2,                   // initial stamina
    STAT_MORALE = 3,                      // enemy AI: raised / lowered (inferred)
    STAT_SHOOTING = 4,
    STAT_UNKNOWN5 = 5,
    STAT_STEALTH = 6,
    STAT_FIRST_AID = 7,
    STAT_LOCKPICK = 8,
    STAT_SIGHT_RANGE = 9,                 // m
    STAT_SPEECH_SET = 10,                 // Tables\speech.set entry
    STAT_HEARING_RANGE = 11,              // m
    STAT_COUNT = 12,
};

constexpr int HUMAN_FULL_HEALTH = 800;

// Soldier of the campaign roster (GetSoldierProfile(); 0x74 bytes). The roster's file and the
// saved games keep it in chunk 9000 (see SaveGame); the custom name comes from the player's
// usernames.sav.
struct S_SoldierProfile {
    S_SoldierProfile();                                 // (inline in C_HumanStore::Add 0x006a2050)
    ~S_SoldierProfile();                                // 0x0069fd30
    S_SoldierProfile(const S_SoldierProfile &) = delete;
    S_SoldierProfile &operator=(const S_SoldierProfile &) = delete;

    // The name chunks, the face, the numbers and the list (and with `withDeath`, the 8 bytes
    // at +0x6C).
    bool SaveGame(C_chunk &chunk, bool withDeath) const;   // 0x0069fe50
    // Also reads the old formats (chunks 0x24B8 / 0x2648 / 0x26AC and the full name 0x238C);
    // then the names' encoding and the face image.
    bool LoadGame(C_chunk &chunk);                      // 0x006a0370
    // fullName "first (nick) last" or "first last", shortName "F. last" (or "last").
    void UpdateNames();                                 // 0x006a00b0
    // faceTexture: "e_f001.bmp", or the face file with its "x_f_" turned into "e_f".
    void UpdateFaceTexture();                           // 0x006a01c0
    void SetCustomName(const C_str &name);              // 0x006a0d70
    // The menus' unicode font: names that are not UTF-8 are converted (only 'í' can be).
    void FixNameEncoding();                             // 0x006a10e0
    // Loads the face file (with the x_fc_a.bmp alpha) into faceImage.
    bool LoadFaceImage();                               // 0x006a1170
    bool HasUnknown28(int value) const;                 // 0x006a11c0

    C_str firstName;                                    // +0x00 chunk 0x2396
    C_str lastName;                                     // +0x04 chunk 0x23A0
    C_str nickname;                                     // +0x08 chunk 0x23AA
    C_str fullName;                                     // +0x0C "first (nick) last" / "first last"
    C_str shortName;                                    // +0x10 "F. last"
    C_str customName;                                   // +0x14 usernames.sav; overrides the others
    C_str faceFile;                                     // +0x18 chunk 0x23F0 ("x_f_001.bmp")
    C_str faceTexture;                                  // +0x1C
    IImage *faceImage = nullptr;                        // +0x20
    int unknown24 = 0;                                  // +0x24 chunk 0x26DE; sent with the soldier state
    std::vector<int> unknown28;                         // +0x28 chunks 0x26FC (the menus test it)
    int unknown38 = 0;                                  // +0x38 chunk 0x26F3
    int unknown3C = 0;                                  // +0x3C chunk 0x26F4
    int unknown40 = 0;                                  // +0x40 chunk 0x26F5
    int unknown44 = -1;                                 // +0x44 chunk 0x26AD with the stats
    float stats[9] = {};                                // +0x48 E_HumanStat 0..8
    int unknown6C = 1;                                  // +0x6C chunk 0x2580 (with `dead`)
    bool dead = false;                                  // +0x70 set when the soldier dies in single player
};
// Roster entry `index`, null if there is none (C_HumanStore::Get on the game profile's roster).
S_SoldierProfile *GetSoldierProfile(int index);         // 0x006a1f50

// Single player progress of a soldier (saved for players).
struct S_SoldierProgress {                              // 0x24 bytes
    float unknown00 = 0.0f;                             // at most 30 (C_Human::AddUnknownProgress)
    float unknown04 = 0.5f;
    float endurance = 0.0f;                             // gains, at most 5
    float morale = 1.0f;                                // at most 1
    float shooting = 0.0f;                              // at most 2
    float stealth = 0.0f;                               // at most 2
    float firstAid = 0.0f;                              // at most 5
    float lockpick = 0.0f;                              // at most 5
    int experience = 0;
};

// Item of a soldier state.
struct S_SoldierItem {                                  // 6 bytes
    // Chunk `id` with the item's 6 bytes (the squad soldiers' saved games).
    bool WriteChunk(int id, C_chunk &chunk) const;      // 0x006a4fd0
    // Reads the current chunk's 6 bytes and leaves it.
    bool ReadChunk(C_chunk &chunk);                     // 0x006a50d0

    int16_t uid = -1;                                   // -1: the receiver creates a new uid
    int16_t typeId = -1;                                // -1: none
    int16_t present = 0;                                // the count (stacks)
};

// What a soldier carries (S_SoldierState +0x8C; 0x20 bytes, saved as they are; the squad's kept within its
// limits, part 10d-v: sent with the state).
struct S_SoldierLoad {
    int capacity = 0;                                   // +0x00 (+0x8C) the weight the soldier can carry (strength)
    float weight = 0.0f;                                // +0x04 (+0x90) everything carried
    float backpackMaxWeight = 0.0f;                     // +0x08 (+0x94) the backpack's container
    int backpackMaxVolume = 0;                          // +0x0C (+0x98)
    float backpackWeight = 0.0f;                        // +0x10 (+0x9C) its contents
    int backpackVolume = 0;                             // +0x14 (+0xA0)
    float beltWeight = 0.0f;                            // +0x18 (+0xA4)
    int beltVolume = 0;                                 // +0x1C (+0xA8) at most 12

    // Fractions of the limits (absolute values): the belt's volume of 12, the backpack's volume,
    // the weight carried.
    float GetBeltFill() const;                          // 0x006a25e0
    float GetBackpackFill() const;                      // 0x006a25f0
    float GetWeightFill() const;                        // 0x006a2600
};
static_assert(sizeof(S_SoldierLoad) == 0x20, "saved as is");

// What a human is and carries: kept between missions for the campaign squad (S_SquadSoldier,
// profile.h), sent to joining network clients (C_NetMessage: 0x00727850 write, 0x00727a30
// read).
struct S_SoldierState {                                 // 0xAC bytes
    C_str name;
    int spawnZone = 0;                                  // +0x04 multiplayer: the net id of the zone to
                                                        //       spawn at (-1: the respawn screen picks it)
    bool spectating = false;                            // +0x08 player controllers
    bool vip = false;                                   // +0x09 objectives games: the VIP (the player
                                                        //       controller's +0x550)
    bool autoEquip = false;                             // +0x0A the campaign squad's: equipped by the
                                                        //       equipment screen (added on a kind 1 screen)
    int health = 0;                                     // +0x0C
    S_SoldierProgress progress;                         // +0x10
    int unknown34 = 0;                                  // +0x34 (the squad's new soldiers: -1)
    int unknown38 = 0;                                  // +0x38 (the squad's new soldiers: -1)
    int profileIndex = 0;                               // +0x3C S_SoldierProfile index, -1 none
    int team = 0;                                       // +0x40 C_Human::GetTeam()
    bool deployed = false;                              // +0x44 single player: in the mission's squad
    uint8_t unknown45[3] = {};
    int speechSet = 0;                                  // +0x48 player speech set 1..4 (else random)
    S_SoldierItem head;                                 // +0x4C
    S_SoldierItem uniform;                              // +0x52
    S_SoldierItem backpack;                             // +0x58
    S_SoldierItem longGun;                              // +0x5E
    S_SoldierItem hands;                                // +0x64
    std::vector<S_SoldierItem> belt;                    // +0x6C
    std::vector<S_SoldierItem> backpackContents;        // +0x7C
    S_SoldierLoad load;                                 // +0x8C
};
