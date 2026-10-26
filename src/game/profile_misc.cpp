// The game profile's smaller objects: the item stocks (0x006a5a80..0x006a6220), game.cfg and the
// HUD option (0x006a6250..0x006a6b80), the debriefing and the movie list (0x006a6bb0..
// 0x006a7a60), the diary (0x006a7a90..0x006a83c0). See profile.h.
#include "game/profile.h"

#include <cstring>

#include "common/chunk.h"
#include "common/config_file.h"

namespace {

enum : uint16_t {
    CHUNK_ITEM_STOCK_ENTRY = 0x4D2,                     // the type; its count follows the chunk
    CHUNK_HUD_OPTION = 0x1642,
    CHUNK_MOVIES_FIRST = 0x1EC8,                        // the first ten seen flags, last first
    CHUNK_MOVIES_OTHERS = 0x1EC9,                       // the others, last first
    CHUNK_DIARY_OLD_TEXTS = 0,
    CHUNK_DIARY_OLD_CURRENT = 2,
    CHUNK_DIARY_TEXTS = 0x6F,
    CHUNK_DIARY_CURRENT = 0x70,
    CHUNK_DIARY_TEXT = 0x71,
};

// The movies the extras menu offers (Hidden & Dangerous 2's, then Sabre Squadron's).
struct S_MovieDef {
    int textId;
    const char *file;
};
const S_MovieDef MOVIES[] = {                           // (0x006a6de0)
    {0xA0, "Intro.bik"},       {0xA1, "IntroArctic.bik"},   {0xA2, "IntroAfrica.bik"},
    {0xA3, "IntroNorway.bik"}, {0xA4, "IntroBurma.bik"},    {0xA5, "IntroAlps.bik"},
    {0xA6, "IntroNormandy.bik"}, {0xA7, "IntroCzech.avi"}, {0xA8, "Outro1.bik"},
    {0xA9, "Outro2.bik"},      {0x3EE8, "D_InFrance1.bik"}, {0x3EE4, "D_InAfrica1.bik"},
    {0x3EE5, "D_InAfrica3.bik"}, {0x3EE9, "D_InSicily1.bik"}, {0x3EE6, "D_InBurgundy1.bik"},
    {0x3EE7, "D_InBurgundy2.bik"}, {0x3EEA, "D_Outro.bik"},
};
// The movies saved in chunk 0x1EC8.
constexpr int MOVIES_FIRST_CHUNK = 10;

// The values game.cfg's keys are read into.
constexpr int CONFIG_VALUE_SIZE = 100;

} // namespace

// ---------------------------------------------------------------------------
// C_ItemStock

void C_ItemStock::Reset()                               // 0x006a5fa0
{
    m_counts.clear();
}

void C_ItemStock::Set(int type, int count)              // 0x006a5cf0
{
    if (type < 0)
        return;
    while (static_cast<int>(m_counts.size()) <= type)
        m_counts.push_back(0);
    m_counts[type] = count;
}

void C_ItemStock::Add(int type, int count)              // 0x006a5d70
{
    if (type < 0)
        return;
    while (static_cast<int>(m_counts.size()) <= type)
        m_counts.push_back(0);
    m_counts[type] += count;
}

int C_ItemStock::Take(int type, int count)              // 0x006a5df0
{
    if (type < 0 || type >= static_cast<int>(m_counts.size()))
        return 0;
    if (m_counts[type] < count)
        count = m_counts[type];
    m_counts[type] -= count;
    return count;
}

int C_ItemStock::Get(int type) const                    // 0x006a5f00
{
    if (type < 0 || type >= static_cast<int>(m_counts.size()))
        return 0;
    return m_counts[type];
}

bool C_ItemStock::SaveGame(C_chunk &chunk)              // 0x006a5a80
{
    for (int type = 0; type < static_cast<int>(m_counts.size()); ++type) {
        if (m_counts[type] == 0)
            continue;
        chunk.Begin(CHUNK_ITEM_STOCK_ENTRY);
        chunk.Write(type);
        chunk.Ascend();
        // (The count is written after the chunk, into the parent.)
        chunk.Write(m_counts[type]);
    }
    return true;
}

bool C_ItemStock::LoadGame(C_chunk &chunk)              // 0x006a5b60
{
    Reset();
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0;
        chunk.Descend(id);
        if (id != CHUNK_ITEM_STOCK_ENTRY)
            return false;
        int type = -1;
        chunk.Read(type);
        chunk.Ascend();
        int count = 0;
        chunk.Read(count);
        Add(type, count);
    }
    return true;
}

// ---------------------------------------------------------------------------
// C_GameConfig

C_GameConfig::C_GameConfig()                            // 0x006a6370
{
    C_ConfigFile file;
    file.Load(C_fstr("%s\\%s", GetGameDirectory(), "game.cfg"));
    char value[CONFIG_VALUE_SIZE];
    value[0] = '\0';
    file.GetString("LASTPROFIL", value, sizeof(value));
    m_lastProfile = value;
    value[0] = '\0';
    // (A prefix of the key the file has: keys match anywhere in a line.)
    file.GetString("PARENTLOC", value, sizeof(value));
    m_parentLock = value;
    value[0] = '\0';
    file.GetString("NETCDKEY", value, sizeof(value));
    m_cdKey = value;
}

bool C_GameConfig::Save()                               // 0x006a6250
{
    // (Unlike the constructor, the file in the working directory.)
    C_ConfigFile file;
    file.Load(C_str("game.cfg"));
    file.SetString("LASTPROFIL", m_lastProfile.c_str());
    file.SetString("PARENTLOCK", m_parentLock.c_str());
    file.SetString("NETCDKEY", m_cdKey.c_str());
    file.Save(C_str("game.cfg"));
    return true;
}

bool C_GameConfig::SetParentLock(C_str code)            // 0x006a65f0
{
    m_parentLock = code;
    return Save();
}

bool C_GameConfig::ClearParentLock(C_str code)          // 0x006a6670
{
    if (_stricmp(m_parentLock.c_str(), code.c_str()) != 0)
        return false;
    m_parentLock = "";
    return Save();
}

bool C_GameConfig::IsParentLockSet() const              // 0x006a6780
{
    return !m_parentLock.IsEmpty();
}

bool C_GameConfig::SetCdKey(C_str key)                  // 0x006a67b0
{
    m_cdKey = key;
    return Save();
}

void C_GameConfig::SetLastProfile(C_str name)           // 0x006a6840
{
    m_lastProfile = name;
    Save();
}

// ---------------------------------------------------------------------------
// S_HudOption

bool S_HudOption::SaveGame(C_chunk &chunk)              // 0x006a68c0
{
    chunk.Begin(CHUNK_HUD_OPTION);
    chunk.Write(&shown, 1);
    chunk.Ascend();
    return true;
}

bool S_HudOption::LoadGame(C_chunk &chunk)              // 0x006a69c0
{
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_HUD_OPTION) {
        chunk.Ascend();
        return false;
    }
    chunk.Read(&shown, 1);
    chunk.Ascend();
    return true;
}

// ---------------------------------------------------------------------------
// C_Debriefing

void C_Debriefing::AddText(int textId)                  // 0x006a6c10
{
    m_texts.push_back(textId);
}

void C_Debriefing::Clear()                              // 0x006a6bb0
{
    m_texts.clear();
}

// ---------------------------------------------------------------------------
// C_MovieList

void C_MovieList::Reset()                               // 0x006a6de0
{
    m_movies.clear();
    m_movies.reserve(20);
    for (const S_MovieDef &movie : MOVIES)
        m_movies.push_back({false, movie.textId, movie.file});
    SetSeen("Intro.bik", true);
}

bool C_MovieList::SetSeen(const char *file, bool seen)  // 0x006a70e0
{
    for (int i = GetCount() - 1; i >= 0; --i) {
        if (_stricmp(m_movies[i].file, file) == 0) {
            m_movies[i].seen = seen;
            return true;
        }
    }
    return false;
}

bool C_MovieList::Get(int index, int *textId, C_str *file) const   // 0x006a7600
{
    if (index < 0 || index >= GetCount())
        return false;
    *textId = m_movies[index].textId;
    *file = m_movies[index].file;
    return true;
}

bool C_MovieList::IsSeen(int index) const               // 0x006a7660
{
    if (index < 0 || index >= GetCount())
        return false;
    return m_movies[index].seen;
}

void C_MovieList::Save(C_chunk &chunk) const            // 0x006a7150
{
    chunk.Begin(CHUNK_MOVIES_FIRST);
    for (int i = MOVIES_FIRST_CHUNK - 1; i >= 0; --i)
        chunk.Write(&m_movies[i].seen, 1);
    chunk.Ascend();
    chunk.Begin(CHUNK_MOVIES_OTHERS);
    for (int i = GetCount() - 1; i >= MOVIES_FIRST_CHUNK; --i)
        chunk.Write(&m_movies[i].seen, 1);
    chunk.Ascend();
}

void C_MovieList::Load(C_chunk &chunk)                  // 0x006a7370
{
    uint16_t id = 0;
    while (chunk.RemainingSize() > 0) {
        uint16_t next;
        if (chunk.Descend(next))
            id = next;
        uint8_t seen;
        if (id == CHUNK_MOVIES_FIRST) {
            for (int i = MOVIES_FIRST_CHUNK - 1; i >= 0; --i) {
                if (chunk.Read(&seen, 1))
                    m_movies[i].seen = seen != 0;
            }
        } else if (id == CHUNK_MOVIES_OTHERS) {
            // From the last movie down while the chunk has flags.
            for (int i = GetCount() - 1; i >= 0 && chunk.RemainingSize() > 0; --i) {
                if (chunk.Read(&seen, 1))
                    m_movies[i].seen = seen != 0;
            }
        }
        chunk.Ascend();
    }
}

// ---------------------------------------------------------------------------
// C_Diary

void C_Diary::AddText(int textId)                       // 0x006a7ad0
{
    if (!locked)
        m_current.push_back(textId);
}

void C_Diary::Clear()                                   // 0x006a7a90
{
    m_texts.clear();
    m_current.clear();
}

void C_Diary::ClearCurrent()                            // 0x006a8390
{
    m_current.clear();
}

void C_Diary::CommitCurrent()                           // 0x006a7c80
{
    m_texts.insert(m_texts.end(), m_current.begin(), m_current.end());
    m_current.clear();
}

bool C_Diary::SaveGame(C_chunk &chunk)                  // 0x006a7ce0
{
    chunk.Begin(CHUNK_DIARY_TEXTS);
    for (int text : m_texts)
        chunk.WriteChunk(CHUNK_DIARY_TEXT, text);
    chunk.Ascend();
    chunk.Begin(CHUNK_DIARY_CURRENT);
    for (int text : m_current)
        chunk.WriteChunk(CHUNK_DIARY_TEXT, text);
    chunk.Ascend();
    return true;
}

bool C_Diary::LoadGame(C_chunk &chunk)                  // 0x006a7f30
{
    Clear();
    uint16_t id = 0;
    while (chunk.RemainingSize() > 0) {
        uint16_t next;
        if (chunk.Descend(next))
            id = next;
        switch (id) {
        case CHUNK_DIARY_OLD_TEXTS:
        case CHUNK_DIARY_OLD_CURRENT:
            // Old saves: a chunk per text.
            while (chunk.RemainingSize() > 0) {
                chunk.Descend(next);
                int text = 0;
                chunk.Read(text);
                chunk.Ascend();
                (id == CHUNK_DIARY_OLD_TEXTS ? m_texts : m_current).push_back(text);
            }
            break;
        case CHUNK_DIARY_TEXTS:
        case CHUNK_DIARY_CURRENT:
            while (chunk.RemainingSize() > 0) {
                uint16_t textId = 0;
                if (!chunk.Descend(textId))
                    break;
                if (textId == CHUNK_DIARY_TEXT) {
                    int text = 0;
                    chunk.Read(text);
                    chunk.Ascend();
                    (id == CHUNK_DIARY_TEXTS ? m_texts : m_current).push_back(text);
                } else {
                    chunk.Ascend();
                }
            }
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}
