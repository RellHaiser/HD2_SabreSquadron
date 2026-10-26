#include "game/sound_table.h"

#include <algorithm>

#include "common/chunk.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

// Constructed by the static initializer 0x00439040.
C_SoundTable g_soundTable;

namespace {

enum : uint16_t {
    CHUNK_SOUND_TABLE = 1000,
    CHUNK_DUBBING_LENGTHS = 0x41A,
    CHUNK_CHAPTER = 0x44C,
    CHUNK_CHAPTER_NAME = 0x456,
    CHUNK_GROUP = 0x4B0,
    CHUNK_GROUP_NAME = 0x4BA,
    CHUNK_SAMPLE = 0x514,
    CHUNK_GROUP_ALWAYS_AUDIBLE = 0x51E,
    CHUNK_SAMPLE_FILE = 0x578,
    CHUNK_SAMPLE_PROBABILITY = 0x582,
    CHUNK_SAMPLE_NEAR_RANGE = 0x58C,
    CHUNK_SAMPLE_FAR_RANGE = 0x596,
    CHUNK_SAMPLE_UNKNOWN10 = 0x5A0,
    CHUNK_SAMPLE_VOLUME = 0x5AA,
    CHUNK_SAMPLE_UNKNOWN18 = 0x5BE,
};

// Names of the chapters before the table is loaded (0x00838a8c).
const char *const DEFAULT_CHAPTER_NAMES[SOUND_CHAPTER_COUNT] = {
    "Puppet Movement", "Object Manipulation", "Weapon Shooting", "Weapon Manipulation",
    "Bullet Hit", "Falling Hit", "Explosion", "Version", "Vehicle Not Car", "Speech",
    "Interface & Menu", "Action", "Music", "Weapon Shooting Dist",
};

// Rest of the current chunk as a name (at most 999 characters).
C_str ReadNameChunk(C_chunk &chunk)
{
    char buffer[1000];
    chunk.ReadString(buffer, sizeof(buffer));
    return C_str(buffer);
}

template <class T>
void DeleteAll(std::vector<T *> &items)
{
    for (std::size_t i = items.size(); i-- > 0;)
        delete items[i];
    items.clear();
}

} // namespace

// ---------------------------------------------------------------------------
// samples, groups, chapters

S_SoundSample::~S_SoundSample()
{
    ReleaseSounds();
}

void S_SoundSample::ReleaseSounds()
{
    if (master)
        master->Release();
    master = nullptr;
    for (std::size_t i = pool.size(); i-- > 0;) {
        if (pool[i])
            pool[i]->Release();
    }
    pool.clear();
}

bool S_SoundSample::Load(C_chunk &chunk)
{
    uint16_t id = 0;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_SAMPLE_FILE:
            fileName = ReadNameChunk(chunk);
            break;
        case CHUNK_SAMPLE_PROBABILITY:
            chunk.Read(probability);
            break;
        case CHUNK_SAMPLE_NEAR_RANGE:
            chunk.Read(nearRange);
            break;
        case CHUNK_SAMPLE_FAR_RANGE:
            chunk.Read(farRange);
            break;
        case CHUNK_SAMPLE_UNKNOWN10:
            chunk.Read(unknown10);
            break;
        case CHUNK_SAMPLE_VOLUME:
            chunk.Read(volume);
            break;
        case CHUNK_SAMPLE_UNKNOWN18:
            chunk.Read(unknown18);
            break;
        default:
            return false;
        }
        chunk.Ascend();
    }
    return true;
}

void S_SoundGroup::Clear()
{
    DeleteAll(samples);
}

bool S_SoundGroup::Load(C_chunk &chunk)
{
    Clear();
    uint16_t id = 0;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_GROUP_NAME:
            name = ReadNameChunk(chunk);
            break;
        case CHUNK_SAMPLE: {
            auto *sample = new S_SoundSample;
            if (!sample->Load(chunk)) {
                delete sample;
                return false;
            }
            samples.push_back(sample);
            break;
        }
        case CHUNK_GROUP_ALWAYS_AUDIBLE: {
            uint8_t value = 0;
            chunk.Read(value);
            alwaysAudible = value != 0;
            break;
        }
        default:
            return false;
        }
        chunk.Ascend();
    }
    return true;
}

void S_SoundChapter::Clear()
{
    DeleteAll(groups);
}

bool S_SoundChapter::Load(C_chunk &chunk)
{
    Clear();
    uint16_t id = 0;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        if (id == CHUNK_CHAPTER_NAME) {
            name = ReadNameChunk(chunk);
        } else if (id == CHUNK_GROUP) {
            auto *group = new S_SoundGroup;
            if (!group->Load(chunk)) {
                delete group;
                return false;
            }
            groups.push_back(group);
        }
        chunk.Ascend();
    }
    return true;
}

// ---------------------------------------------------------------------------
// C_SoundTable

C_SoundTable::C_SoundTable()
{
    for (const char *name : DEFAULT_CHAPTER_NAMES)
        m_chapters.push_back(new S_SoundChapter(name));
}

C_SoundTable::~C_SoundTable()
{
    Clear();
}

void C_SoundTable::Clear()
{
    DeleteAll(m_chapters);
    m_dubbingLengths.clear();
    m_loaded = false;
}

void C_SoundTable::ReleaseSounds()
{
    for (S_SoundChapter *chapter : m_chapters) {
        for (S_SoundGroup *group : chapter->groups) {
            for (S_SoundSample *sample : group->samples)
                sample->ReleaseSounds();
        }
    }
}

bool C_SoundTable::Load(bool reload)
{
    if (m_loaded && !reload)
        return true;

    C_chunk chunk;
    uint16_t id = 0;
    if (!chunk.Open("Tables\\IngameSounds.def", false) && !chunk.Open("Tables\\IngameSounds.dat", false)) {
        dbgPrintf(" @SOUND_TABLE : tablefile not found");
    } else if (!chunk.Descend(id) || id != CHUNK_SOUND_TABLE) {
        dbgPrintf(" @SOUND_TABLE : bad tablefile format");
    } else {
        Clear();
        bool ok = true;
        while (ok && !chunk.AtEnd() && chunk.Descend(id)) {
            if (id == CHUNK_DUBBING_LENGTHS) {
                while (!chunk.AtEnd()) {
                    int dubbing = 0;
                    int length = 0;
                    chunk.Read(dubbing);
                    chunk.Read(length);
                    m_dubbingLengths[dubbing] = length;
                }
            } else if (id == CHUNK_CHAPTER) {
                auto *chapter = new S_SoundChapter;
                if (!chapter->Load(chunk)) {
                    dbgPrintf(" @SOUND_TABLE : chapter not loaded; loading stopped");
                    delete chapter;
                    ok = false;
                    break;
                }
                m_chapters.push_back(chapter);
            }
            chunk.Ascend();
        }
        if (ok)
            m_loaded = true;
    }

    // Every chapter must exist even without a table.
    while (m_chapters.size() < SOUND_CHAPTER_COUNT)
        m_chapters.push_back(new S_SoundChapter);
    chunk.Close();
    return m_loaded;
}

S_SoundGroup *C_SoundTable::GetGroup(int chapter, int group) const
{
    if (chapter < 0 || chapter >= NumChapters() || group < 0)
        return nullptr;
    const S_SoundChapter *c = m_chapters[chapter];
    return group < static_cast<int>(c->groups.size()) ? c->groups[group] : nullptr;
}

int C_SoundTable::GetDubbingSampleSize(int id)
{
    const int length = m_dubbingLengths[id];
    if (length < 100) {
        dbgPrintf(" ERROR : C_sound_table::GetDabingSampleSize() : dabing id '%d' with unknown lenght", id);
        return 2000;
    }
    return length;
}
