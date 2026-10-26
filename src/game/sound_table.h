// C_SoundTable — the game's sound definitions (Tables\IngameSounds.def / .dat).
//
// Sounds are organised in chapters (fixed meaning, E_SoundChapter) of numbered
// groups; a group lists alternative samples, one of which is picked at random
// every time the group is played. The table also stores the length of every
// dubbing (speech) sample, used by scripts to wait for a line to finish.
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "common/c_str.h"

class C_chunk;
class I3D_sound;

enum E_SoundChapter {
    SOUND_CHAPTER_PUPPET_MOVEMENT = 0,
    SOUND_CHAPTER_OBJECT_MANIPULATION = 1,
    SOUND_CHAPTER_WEAPON_SHOOTING = 2,
    SOUND_CHAPTER_WEAPON_MANIPULATION = 3,
    SOUND_CHAPTER_BULLET_HIT = 4,
    SOUND_CHAPTER_FALLING_HIT = 5,
    SOUND_CHAPTER_EXPLOSION = 6,
    SOUND_CHAPTER_VERSION = 7,
    SOUND_CHAPTER_VEHICLE_NOT_CAR = 8,
    SOUND_CHAPTER_SPEECH = 9,
    SOUND_CHAPTER_INTERFACE = 10,       // "Interface & Menu"
    SOUND_CHAPTER_ACTION = 11,
    SOUND_CHAPTER_MUSIC = 12,
    SOUND_CHAPTER_WEAPON_SHOOTING_DIST = 13,
    SOUND_CHAPTER_COUNT = 14,
};

// One sound file of a group (chunk 0x514).
struct S_SoundSample {                                  // 0x30 bytes
    S_SoundSample() = default;
    S_SoundSample(const S_SoundSample &) = delete;
    S_SoundSample &operator=(const S_SoundSample &) = delete;
    ~S_SoundSample();                                   // 0x0043a630

    bool Load(C_chunk &chunk);                          // 0x0043aa50

    C_str fileName;                                     // +0x00 chunk 0x578 (below "sounds\")
    // Cumulative chance in percent: the first sample of the group whose value
    // exceeds a random 0..99 is chosen (C_SoundModule::PlayTableSound).
    int probability = 100;                              // +0x04 chunk 0x582
    float nearRange = 0.0f;                             // +0x08 chunk 0x58C full volume up to here (m)
    float farRange = 20.0f;                             // +0x0C chunk 0x596 inaudible beyond (m)
    float unknown10 = 0.5f;                             // +0x10 chunk 0x5A0
    float volume = 1.0f;                                // +0x14 chunk 0x5AA
    float unknown18 = 0.5f;                             // +0x18 chunk 0x5BE
    // Loaded copy new sounds are opened from, and finished sounds kept for reuse.
    I3D_sound *master = nullptr;                        // +0x1C
    std::vector<I3D_sound *> pool;                      // +0x20

    // Releases the master sound and the pooled sounds.
    void ReleaseSounds();
};

// Alternative samples for one sound (chunk 0x4B0).
struct S_SoundGroup {                                   // 0x18 bytes
    S_SoundGroup() = default;
    S_SoundGroup(const S_SoundGroup &) = delete;
    S_SoundGroup &operator=(const S_SoundGroup &) = delete;
    ~S_SoundGroup() { Clear(); }                        // 0x00431fd0

    bool Load(C_chunk &chunk);                          // 0x0043a6f0
    void Clear();                                       // 0x0043a5e0

    C_str name = "#name error";                         // +0x00 chunk 0x4BA
    std::vector<S_SoundSample *> samples;               // +0x04 chunk 0x514
    // Played even when out of range of the listener (as a non-positional sound).
    bool alwaysAudible = false;                         // +0x14 chunk 0x51E
};

struct S_SoundChapter {                                 // 0x14 bytes
    S_SoundChapter() = default;
    explicit S_SoundChapter(const char *chapterName) : name(chapterName) {}
    S_SoundChapter(const S_SoundChapter &) = delete;
    S_SoundChapter &operator=(const S_SoundChapter &) = delete;
    ~S_SoundChapter() { Clear(); }                      // 0x004399a0

    bool Load(C_chunk &chunk);                          // 0x00432040
    void Clear();                                       // 0x00431f80

    C_str name;                                         // +0x00 chunk 0x456
    std::vector<S_SoundGroup *> groups;                 // +0x04 chunk 0x4B0
};

class C_SoundTable {                                    // "C_sound_table", 0x24 bytes
public:
    C_SoundTable();                                     // 0x004394d0 (default chapter names)
    ~C_SoundTable();                                    // 0x00439770
    C_SoundTable(const C_SoundTable &) = delete;
    C_SoundTable &operator=(const C_SoundTable &) = delete;

    // Loads the table once (again with `reload`).
    bool Load(bool reload = false);                     // 0x00439080
    void Clear();                                       // 0x004398b0
    // Releases every loaded sound of every sample (mission end).
    void ReleaseSounds();

    int NumChapters() const { return static_cast<int>(m_chapters.size()); }
    S_SoundChapter *GetChapter(int index) const { return m_chapters[index]; }
    // Group `group` of chapter `chapter`, null if either index is out of range.
    S_SoundGroup *GetGroup(int chapter, int group) const;
    // Length of a dubbing sample in ms; unknown or implausible lengths give 2000.
    int GetDubbingSampleSize(int id);                   // 0x00439a10

private:
    bool m_loaded = false;                              // +0x00
    std::vector<S_SoundChapter *> m_chapters;           // +0x04
    std::map<int, int> m_dubbingLengths;                // +0x14 chunk 0x41A (id, ms)
};

extern C_SoundTable g_soundTable;                       // 0x00899948
