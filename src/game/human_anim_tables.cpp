// The animation tables of the humans (original source 0x00783420..0x00784430).
#include "game/human_anim_tables.h"

#include <cstring>

#include "game/pending.h"
#include "game/resource_cache.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"

C_HumanAnimTable *g_humanAnimTable = nullptr;
C_HumanAnimTable *g_humanAnimTable2 = nullptr;
C_CarryAnimTable *g_carryAnimTables[CARRY_ANIM_TABLES] = {};
C_ActivityAnimTable *g_activityAnimTable = nullptr;
C_SeatWeaponAnimTable *g_seatWeaponAnimTable = nullptr;

namespace {

// Animation names take 0x14 bytes of a record.
constexpr int NAME_SIZE = 0x14;

bool g_tablesLoaded = false;                            // 0x0098a4a4

template <class T> T ReadField(const uint8_t *record, int offset)
{
    T value;
    std::memcpy(&value, record + offset, sizeof(T));
    return value;
}

// Fix: a name filling all its bytes is not read past them (the original takes the record
// bytes as a C string).
void ReadName(const uint8_t *record, int offset, char (&name)[NAME_SIZE + 1])
{
    std::memcpy(name, record + offset, NAME_SIZE);
    name[NAME_SIZE] = '\0';
}

void Resolve(const char *name, I3D_animation_set **anim)
{
    g_animationCache.Get(name, anim);
}

// The shared part of the tables' SaveRecords().
void SaveRecordBlock(uint8_t *&records, int recordsSize, int *ioSize, void *out)
{
    const int requested = *ioSize;
    *ioSize = recordsSize;
    if (requested == 0)
        return;
    // Fix: records already dropped by Load() are not copied (the original reads null).
    if (records)
        std::memcpy(out, records, recordsSize);
    delete[] records;
    records = nullptr;
}

// Copies the records in (Load()).
void SetRecordBlock(uint8_t *records, int recordsSize, int size, const void *data)
{
    // Fix: at most the block's size is copied (the original copies `size` bytes).
    std::memcpy(records, data, size < recordsSize ? size : recordsSize);
}

}  // namespace

// ------------------------------------------------------------------ movement table

C_HumanAnimTable::C_HumanAnimTable() : m_records(new uint8_t[RECORDS_SIZE]()) {}

C_HumanAnimTable::~C_HumanAnimTable()
{
    delete[] m_records;
    m_records = nullptr;
    for (S_HumanAnimSlot &slot : m_slots) {
        if (slot.anim)
            slot.anim->Release();
    }
}

void C_HumanAnimTable::SaveRecords(int *ioSize, void *out)
{
    SaveRecordBlock(m_records, RECORDS_SIZE, ioSize, out);
}

void C_HumanAnimTable::Load(int size, const void *data)
{
    SetRecordBlock(m_records, RECORDS_SIZE, size, data);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint8_t *record = m_records + i * RECORD_SIZE;
        S_HumanAnimSlot &slot = m_slots[i];
        slot.flags = ReadField<uint32_t>(record, 0x00);
        slot.time = ReadField<float>(record, 0x04);
        char name[NAME_SIZE + 1];
        ReadName(record, 0x08, name);
        if (std::strlen(name) > 4)
            Resolve(name, &slot.anim);
    }
    delete[] m_records;
    m_records = nullptr;
}

int C_HumanAnimTable::GetAnimLength(int id, int variant, int sub) const
{
    if (id >= HUMAN_ANIM_NONE)
        return -1;
    I3D_animation_set *anim = id < HUMAN_ANIM_FIRST_6X ? GetAnim(id, variant) : GetAnim6(id, variant, sub);
    return anim ? anim->GetEndTime() : -1;
}

// ------------------------------------------------------------------ carry tables

C_CarryAnimTable::C_CarryAnimTable(int /*index*/) : m_records(new uint8_t[RECORDS_SIZE]()) {}

C_CarryAnimTable::~C_CarryAnimTable()
{
    delete[] m_records;
    m_records = nullptr;
    for (S_CarryAnimSlot &slot : m_slots) {
        if (slot.anim)
            slot.anim->Release();
    }
}

void C_CarryAnimTable::SaveRecords(int *ioSize, void *out)
{
    SaveRecordBlock(m_records, RECORDS_SIZE, ioSize, out);
}

void C_CarryAnimTable::Load(int size, const void *data)
{
    SetRecordBlock(m_records, RECORDS_SIZE, size, data);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint8_t *record = m_records + i * RECORD_SIZE;
        S_CarryAnimSlot &slot = m_slots[i];
        slot.flags = ReadField<uint32_t>(record, 0x00);
        slot.time = ReadField<float>(record, 0x1C);
        slot.weight = ReadField<float>(record, 0x18);
        char name[NAME_SIZE + 1];
        ReadName(record, 0x04, name);
        if (std::strlen(name) > 4)
            Resolve(name, &slot.anim);
    }
    delete[] m_records;
    m_records = nullptr;
}

// ------------------------------------------------------------------ activity table

C_ActivityAnimTable::C_ActivityAnimTable() : m_records(new uint8_t[RECORDS_SIZE]()) {}

C_ActivityAnimTable::~C_ActivityAnimTable()
{
    delete[] m_records;
    m_records = nullptr;
    for (S_HumanAnimSlot &slot : m_slots) {
        if (slot.anim)
            slot.anim->Release();
    }
}

void C_ActivityAnimTable::SaveRecords(int *ioSize, void *out)
{
    SaveRecordBlock(m_records, RECORDS_SIZE, ioSize, out);
}

void C_ActivityAnimTable::Load(int size, const void *data)
{
    SetRecordBlock(m_records, RECORDS_SIZE, size, data);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const uint8_t *record = m_records + i * RECORD_SIZE;
        S_HumanAnimSlot &slot = m_slots[i];
        slot.flags = ReadField<uint32_t>(record, 0x00);
        slot.time = ReadField<float>(record, 0x04);
        char name[NAME_SIZE + 1];
        ReadName(record, 0x08, name);
        if (std::strlen(name) > 4)
            Resolve(name, &slot.anim);
    }
    delete[] m_records;
    m_records = nullptr;
}

// ------------------------------------------------------------------ idle table

C_SeatWeaponAnimTable::C_SeatWeaponAnimTable() : m_records(new uint8_t[RECORDS_SIZE]()) {}

C_SeatWeaponAnimTable::~C_SeatWeaponAnimTable()
{
    delete[] m_records;
    m_records = nullptr;
    for (I3D_animation_set *anim : m_anims) {
        if (anim)
            anim->Release();
    }
}

void C_SeatWeaponAnimTable::SaveRecords(int *ioSize, void *out)
{
    SaveRecordBlock(m_records, RECORDS_SIZE, ioSize, out);
}

void C_SeatWeaponAnimTable::Load(int size, const void *data)
{
    SetRecordBlock(m_records, RECORDS_SIZE, size, data);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        char name[NAME_SIZE + 1];
        ReadName(m_records + i * RECORD_SIZE, 0, name);
        // (Here any name that starts with a printable character counts.)
        if (name[0] > ' ')
            Resolve(name, &m_anims[i]);
    }
    delete[] m_records;
    m_records = nullptr;
}

// ------------------------------------------------------------------ globals

void InitHumanAnimTables()
{
    g_humanAnimTable = new C_HumanAnimTable;
    g_humanAnimTable2 = new C_HumanAnimTable;
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        g_carryAnimTables[i] = new C_CarryAnimTable(i + 2);
    g_activityAnimTable = new C_ActivityAnimTable;
    g_seatWeaponAnimTable = new C_SeatWeaponAnimTable;
}

void DoneHumanAnimTables()
{
    delete g_humanAnimTable;
    g_humanAnimTable = nullptr;
    delete g_humanAnimTable2;
    g_humanAnimTable2 = nullptr;
    for (C_CarryAnimTable *&table : g_carryAnimTables) {
        delete table;
        table = nullptr;
    }
    delete g_activityAnimTable;
    g_activityAnimTable = nullptr;
    delete g_seatWeaponAnimTable;
    g_seatWeaponAnimTable = nullptr;
}

bool EnsureHumanAnimTablesLoaded()
{
    if (g_tablesLoaded)
        return true;
    g_tablesLoaded = true;
    return LoadHumanAnimTables();
}

bool LoadHumanAnimTables()
{
    const int file = dtaOpen("Tables\\anim.sav", 0);
    if (file == DTA_INVALID_HANDLE) {
        ShowMessage("KRITICKA chyba pri nahravani souboru anim.sav", "I/O ERROR", 0);
        dtaClose(file);
        return false;
    }
    // The file holds the record blocks of the tables in this order.
    int humanSize = 0;
    g_humanAnimTable->SaveRecords(&humanSize, nullptr);
    int humanSize2 = 0;
    g_humanAnimTable2->SaveRecords(&humanSize2, nullptr);
    int carrySizes[CARRY_ANIM_TABLES] = {};
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i)
        g_carryAnimTables[i]->SaveRecords(&carrySizes[i], nullptr);
    int activitySize = 0;
    g_activityAnimTable->SaveRecords(&activitySize, nullptr);
    int idleSize = 0;
    g_seatWeaponAnimTable->SaveRecords(&idleSize, nullptr);
    int total = humanSize + humanSize2 + activitySize + idleSize;
    for (int size : carrySizes)
        total += size;

    uint8_t *data = new uint8_t[total];
    if (dtaRead(file, data, total) == -1) {
        ShowMessage("KRITICKA chyba pri nahravani souboru anim.sav", "I/O ERROR", 0);
        dtaClose(file);
        delete[] data;
        return false;
    }
    dtaClose(file);
    const uint8_t *cursor = data;
    g_humanAnimTable->Load(humanSize, cursor);
    cursor += humanSize;
    g_humanAnimTable2->Load(humanSize2, cursor);
    cursor += humanSize2;
    for (int i = 0; i < CARRY_ANIM_TABLES; ++i) {
        g_carryAnimTables[i]->Load(carrySizes[i], cursor);
        cursor += carrySizes[i];
    }
    g_activityAnimTable->Load(activitySize, cursor);
    cursor += activitySize;
    g_seatWeaponAnimTable->Load(idleSize, cursor);
    delete[] data;
    return true;
}

// ------------------------------------------------------------------ queries

C_HumanAnimTable *GetHumanAnimTable(int group)
{
    if (group == 1)
        return g_humanAnimTable;
    return group == 2 ? g_humanAnimTable2 : nullptr;
}

C_CarryAnimTable *GetCarryAnimTable(int carryMode)
{
    switch (carryMode) {
    case 0:
    case 6:
        return g_carryAnimTables[0];
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        return g_carryAnimTables[carryMode];
    default:
        return nullptr;
    }
}

int GetCarryAnimLength(int id, int variant, int carryMode)
{
    if (id >= C_CarryAnimTable::IDS)
        return -1;
    const C_CarryAnimTable *table = GetCarryAnimTable(carryMode);
    if (!table)
        return -1;
    I3D_animation_set *anim = table->GetSlot(id, variant).anim;
    return anim ? anim->GetEndTime() : -1;
}

int GetActivityAnimLength(int id, int variant)
{
    if (id >= C_ActivityAnimTable::IDS)
        return -1;
    I3D_animation_set *anim = g_activityAnimTable->GetSlot(id, variant).anim;
    return anim ? anim->GetEndTime() : -1;
}
