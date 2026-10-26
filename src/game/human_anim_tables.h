// The animation tables of the humans (Tables\anim.sav; original source 0x00783420..0x00784430 and
// the length queries 0x0075d000..0x0075d150). Every slot names an animation file; loading the
// tables resolves the names through g_animationCache.
//
// Tables\anim.sav is the record blocks of all tables one after the other, in the order of the
// globals below. Animation ids (E_HumanAnim) run 0..205 (HUMAN_ANIM_NONE = 206); the ids below
// 190 have 3 variants, the 16 from 190 on 3 x 6 variants.
#pragma once

#include <cstdint>

class I3D_animation_set;

enum : int {
    HUMAN_ANIM_FIRST_6X = 190,          // the ids from here on have 3 x 6 variants
    HUMAN_ANIM_NONE = 206,
};

// Slot of the movement and activity tables.
struct S_HumanAnimSlot {                                // 12 bytes
    uint32_t flags = 0;                                 // +0x00 (not identified)
    float time = 0.0f;                                  // +0x04 blend time (ms; the animation player divides it by 1.4)
    I3D_animation_set *anim = nullptr;                  // +0x08
};

// Slot of the carry tables (one table per carry mode of the hands item).
struct S_CarryAnimSlot {                                // 16 bytes
    uint32_t flags = 0;                                 // +0x00 bits 1..3 read by the animation player
    I3D_animation_set *anim = nullptr;                  // +0x04
    float weight = 0.0f;                                // +0x08 of the carry animation on its stage
    float time = 0.0f;                                  // +0x0C blend time (ms; the animation player divides it by 1.4)
};

// Movement animations of one animation group (C_Human::GetGroup() 1 / 2; 0x24DC bytes).
class C_HumanAnimTable {
public:
    C_HumanAnimTable(const C_HumanAnimTable &) = delete;
    C_HumanAnimTable &operator=(const C_HumanAnimTable &) = delete;
    static constexpr int BLOCK1_IDS = 166;              // ids with 3 variants that have slots
    static constexpr int BLOCK2_IDS = 16;               // ids 190..205: 3 x 6 variants
    static constexpr int SLOT_COUNT = BLOCK1_IDS * 3 + BLOCK2_IDS * 3 * 6;
    static constexpr int RECORD_SIZE = 0x1C;            // flags, time, name[0x14]
    static constexpr int RECORDS_SIZE = SLOT_COUNT * RECORD_SIZE;   // 0x55F8

    C_HumanAnimTable();                                 // 0x00783a00
    ~C_HumanAnimTable();                                // 0x00783af0
    // Size of the record block in `*ioSize`; with a non-zero `*ioSize` the records are also
    // copied to `out` and dropped (the editor's save path; the game only asks for the size).
    void SaveRecords(int *ioSize, void *out);           // 0x00783b70
    // Takes `size` bytes of records, resolves the animations and drops the records.
    void Load(int size, const void *data);              // 0x00783bc0

    // Animation of an id (< HUMAN_ANIM_FIRST_6X) and variant (0..2). (Ids 166..189 have no
    // slots of their own: they read the 3 x 6 block, as in the original.)
    I3D_animation_set *GetAnim(int id, int variant) const { return m_slots[id * 3 + variant].anim; }
    // Animation of an id >= HUMAN_ANIM_FIRST_6X, its variant (0..2) and sub-variant (0..5).
    I3D_animation_set *GetAnim6(int id, int variant, int sub) const
    {
        return m_slots[BLOCK1_IDS * 3 + ((id - HUMAN_ANIM_FIRST_6X) * 3 + variant) * 6 + sub].anim;
    }
    // Length (ms) of an animation (ids >= HUMAN_ANIM_FIRST_6X use `sub`); -1 without one.
    int GetAnimLength(int id, int variant, int sub) const;   // inline in 0x0075d000
    const S_HumanAnimSlot &GetSlot(int index) const { return m_slots[index]; }
    // (C_HumanEquipment: animations of the special object of place 9.)
    I3D_animation_set *GetUnknownD88() const { return m_slots[288].anim; }   // +0xD88
    I3D_animation_set *GetUnknownDAC() const { return m_slots[291].anim; }   // +0xDAC

private:
    S_HumanAnimSlot m_slots[SLOT_COUNT];                // +0x0000
    uint8_t *m_records;                                 // +0x24D8 until Load()
};

// Animations of one carry mode (0x1114 bytes).
class C_CarryAnimTable {
public:
    C_CarryAnimTable(const C_CarryAnimTable &) = delete;
    C_CarryAnimTable &operator=(const C_CarryAnimTable &) = delete;
    static constexpr int IDS = 91;
    static constexpr int SLOT_COUNT = IDS * 3;
    static constexpr int RECORD_SIZE = 0x20;            // flags, name[0x14], weight, time
    static constexpr int RECORDS_SIZE = SLOT_COUNT * RECORD_SIZE;   // 0x2220

    // (`index`, 2 + the table's index, is not used.)
    explicit C_CarryAnimTable(int index);               // 0x00783820
    ~C_CarryAnimTable();                                // 0x007838a0
    // Size of the record block in `*ioSize`; with a non-zero `*ioSize` the records are also
    // copied to `out` and dropped (the editor's save path; the game only asks for the size).
    void SaveRecords(int *ioSize, void *out);           // 0x007838f0
    // Takes `size` bytes of records, resolves the animations and drops the records.
    void Load(int size, const void *data);              // 0x00783940

    const S_CarryAnimSlot &GetSlot(int id, int variant) const { return m_slots[id * 3 + variant]; }

private:
    S_CarryAnimSlot m_slots[SLOT_COUNT];                // +0x0000
    uint8_t *m_records;                                 // +0x1110 until Load()
};

// Animations of the scripted activities (C_ScriptedController::PlayActivity; 0xA90 bytes).
class C_ActivityAnimTable {
public:
    C_ActivityAnimTable(const C_ActivityAnimTable &) = delete;
    C_ActivityAnimTable &operator=(const C_ActivityAnimTable &) = delete;
    static constexpr int IDS = 75;
    static constexpr int SLOT_COUNT = IDS * 3;
    static constexpr int RECORD_SIZE = 0x1C;            // flags, time, name[0x14]
    static constexpr int RECORDS_SIZE = SLOT_COUNT * RECORD_SIZE;   // 0x189C

    C_ActivityAnimTable();                                 // 0x00783d00
    ~C_ActivityAnimTable();                             // 0x00783d70
    // Size of the record block in `*ioSize`; with a non-zero `*ioSize` the records are also
    // copied to `out` and dropped (the editor's save path; the game only asks for the size).
    void SaveRecords(int *ioSize, void *out);           // 0x00783dc0
    // Takes `size` bytes of records, resolves the animations and drops the records.
    void Load(int size, const void *data);              // 0x00783e10

    const S_HumanAnimSlot &GetSlot(int id, int variant) const { return m_slots[id * 3 + variant]; }

private:
    S_HumanAnimSlot m_slots[SLOT_COUNT];                // +0x0000
    uint8_t *m_records;                                 // +0xA8C until Load()
};

// Idle animations (4 x 3; 0x34 bytes).
class C_SeatWeaponAnimTable {
public:
    C_SeatWeaponAnimTable(const C_SeatWeaponAnimTable &) = delete;
    C_SeatWeaponAnimTable &operator=(const C_SeatWeaponAnimTable &) = delete;
    static constexpr int SLOT_COUNT = 4 * 3;
    static constexpr int RECORD_SIZE = 0x14;            // name
    static constexpr int RECORDS_SIZE = SLOT_COUNT * RECORD_SIZE;   // 0xF0

    C_SeatWeaponAnimTable();                                 // 0x00783ec0
    ~C_SeatWeaponAnimTable();                                 // 0x00783f20
    // Size of the record block in `*ioSize`; with a non-zero `*ioSize` the records are also
    // copied to `out` and dropped (the editor's save path; the game only asks for the size).
    void SaveRecords(int *ioSize, void *out);           // 0x00783f60
    // Takes `size` bytes of records, resolves the animations and drops the records.
    void Load(int size, const void *data);              // 0x00783fa0

    I3D_animation_set *GetAnim(int index) const { return m_anims[index]; }

private:
    I3D_animation_set *m_anims[SLOT_COUNT] = {};        // +0x00
    uint8_t *m_records;                                 // +0x30 until Load()
};

// Number of carry tables (carry modes 0..5; mode 6 uses table 0; the seventh is loaded but
// never read).
constexpr int CARRY_ANIM_TABLES = 7;

extern C_HumanAnimTable *g_humanAnimTable;              // 0x0098a474 group 1
extern C_HumanAnimTable *g_humanAnimTable2;             // 0x0098a478 group 2
extern C_CarryAnimTable *g_carryAnimTables[CARRY_ANIM_TABLES];   // 0x0098a47c
extern C_ActivityAnimTable *g_activityAnimTable;        // 0x0098a498
extern C_SeatWeaponAnimTable *g_seatWeaponAnimTable;                // 0x0098a49c

// Creates / deletes the tables (game start / end).
void InitHumanAnimTables();                             // 0x00783420
void DoneHumanAnimTables();                             // 0x00783680
// Loads Tables\anim.sav (once; later calls only report success).
bool EnsureHumanAnimTablesLoaded();                     // 0x007837b0
bool LoadHumanAnimTables();                             // 0x00784020

// Table of an animation group (1 / 2; others: none).
C_HumanAnimTable *GetHumanAnimTable(int group);
// Carry table of a carry mode (0..6; others: none).
C_CarryAnimTable *GetCarryAnimTable(int carryMode);

// Lengths (ms) of table animations; -1 without one (__stdcall).
int GetCarryAnimLength(int id, int variant, int carryMode);   // 0x0075d090
int GetActivityAnimLength(int id, int variant);         // 0x0075d120
