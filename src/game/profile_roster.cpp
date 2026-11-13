// The soldier roster: the campaign's soldier profiles (tables\HumanStore.def, the saved games'
// chunk 0x3EA) and the names the player gave them (usernames.sav). Original source file
// 0x0069f9a0..0x006a1f80 (see profile.h, soldier.h).
#include "game/profile.h"

#include <cstdio>
#include <cstring>

#include "common/chunk.h"
#include "game/pending.h"
#include "game/text_box.h"
#include "ls3d/igraph.h"

S_RankName g_rankNames[8] = {                           // 0x008ae960 (static init 0x0069f9b0)
    {0x27A6, 0x2792, C_str(), C_str()}, {0x27A7, 0x2793, C_str(), C_str()},
    {0x27A8, 0x2794, C_str(), C_str()}, {0x27A9, 0x2795, C_str(), C_str()},
    {0x27AA, 0x2796, C_str(), C_str()}, {0x27AB, 0x2797, C_str(), C_str()},
    {0x27AC, 0x2798, C_str(), C_str()}, {0x27AD, 0x2799, C_str(), C_str()},
};

int g_rosterTexts[24] = {                               // 0x008ae900 (static init 0x0069fb00)
    0x2774, 0x2775, 0x2776, 0x2777, 0x2778, 0x2779, 0x277A, 0x277B,
    0x277C, 0x277D, 0x277E, 0x277F, 0x2780, 0x2781, 0x2782, 0x2783,
    0x2784, 0x2785, 0x2786, 0x2787, 0x2788, 0x2789, 0x278A, 0x278B,
};

namespace {

// A soldier profile's chunks.
enum : uint16_t {
    CHUNK_SOLDIER_FULL_NAME_OLD = 0x238C,               // "first (nick) last"
    CHUNK_SOLDIER_FIRST_NAME = 0x2396,
    CHUNK_SOLDIER_LAST_NAME = 0x23A0,
    CHUNK_SOLDIER_NICKNAME = 0x23AA,
    CHUNK_SOLDIER_CUSTOM_NAME = 0x23B4,                 // usernames.sav
    CHUNK_SOLDIER_FACE = 0x23F0,
    CHUNK_SOLDIER_STATS_OLD1 = 0x24B8,                  // 0x48 bytes
    CHUNK_SOLDIER_DEATH = 0x2580,
    CHUNK_SOLDIER_STATS_OLD2 = 0x2648,                  // 0x4C bytes
    CHUNK_SOLDIER_STATS_INT = 0x26AC,                   // the stats as ints
    CHUNK_SOLDIER_STATS = 0x26AD,
    CHUNK_SOLDIER_UNKNOWN24 = 0x26DE,
    CHUNK_SOLDIER_UNKNOWN38 = 0x26F3,
    CHUNK_SOLDIER_UNKNOWN3C = 0x26F4,
    CHUNK_SOLDIER_UNKNOWN40 = 0x26F5,
    CHUNK_SOLDIER_UNKNOWN28 = 0x26FC,
    CHUNK_CUSTOM_NAMES = 0x29A,                         // usernames.sav
    CHUNK_ROSTER_SOLDIER = 9000,
};

// The roster's file and the stats scale.
const char *const ROSTER_FILE = "tables\\HumanStore.def";
constexpr float STAT_LEVEL_SCALE = 0.03f;
constexpr int STAT_MAX = 100;
// The strings the loader reads (the buffers' 999 chars).
constexpr int NAME_BUFFER_SIZE = 1000;

// The old stats chunks: where the record's ints go (the int at 0 is unknown44).
constexpr int OLD_STATS_INDEX[9] = {1, 2, 4, 5, 7, 9, 6, 13, 12};

C_str ReadName(C_chunk &chunk)
{
    // Fix: the original read at most 999 chars without adding a terminator (the files store
    // one).
    char buffer[NAME_BUFFER_SIZE];
    chunk.ReadString(buffer, sizeof(buffer));
    return C_str(buffer);
}

void WriteName(C_chunk &chunk, uint16_t id, const C_str &name)
{
    chunk.Begin(id);
    chunk.Write(name.c_str(), static_cast<int>(name.Size()) + 1);
    chunk.Ascend();
}

// The text's bytes are UTF-8 (an empty text counts).
bool IsUtf8(const C_str &text)                          // 0x006a0db0
{
    if (text.IsEmpty())
        return true;
    const uint8_t *begin = reinterpret_cast<const uint8_t *>(text.c_str());
    return IsLegalUtf8Range(begin, begin + text.Size());
}

// Latin-1 to UTF-8, but only for 'í' (0xED): other non-ASCII bytes leave the text as it is
// (false).
bool ConvertToUtf8(C_str &text)                         // 0x006a0df0
{
    C_str converted;
    for (size_t i = 0; i < text.Size(); ++i) {
        const uint8_t c = static_cast<uint8_t>(text.c_str()[i]);
        if (c <= 0x7F) {
            const char ch = static_cast<char>(c);
            converted += C_str(&ch, 1);                 // (C_str::AssignChar / AppendChar)
        } else if (c == 0xED) {
            converted += "\xC3\xAD";
        } else {
            return false;
        }
    }
    text = converted;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// S_SoldierProfile

S_SoldierProfile::S_SoldierProfile()                    // (C_HumanStore::Add 0x006a2050)
    : firstName("First"), lastName("Last"), nickname(""), faceFile("x_f_001.bmp"), faceTexture("e_f001.bmp")
{
    UpdateFaceTexture();
    UpdateNames();
}

S_SoldierProfile::~S_SoldierProfile()                   // 0x0069fd30 (DS 0x006e1170)
{
#ifndef HD2_DEDICATED   // the server makes no face images
    if (faceImage)
        faceImage->Release();
#endif
    faceImage = nullptr;
}

void S_SoldierProfile::UpdateNames()                    // 0x006a00b0
{
    fullName = "";
    shortName = "";
    // Fix: the names went into a 256-byte buffer with sprintf (the files' names can be 999
    // chars).
    char buffer[256];
    if (nickname.IsEmpty())
        std::snprintf(buffer, sizeof(buffer), "%s %s", firstName.c_str(), lastName.c_str());
    else
        std::snprintf(buffer, sizeof(buffer), "%s (%s) %s", firstName.c_str(), nickname.c_str(), lastName.c_str());
    fullName = buffer;
    if (firstName.IsEmpty())
        std::snprintf(buffer, sizeof(buffer), "%s", lastName.c_str());
    else
        std::snprintf(buffer, sizeof(buffer), "%c. %s", firstName.c_str()[0], lastName.c_str());
    shortName = buffer;
}

void S_SoldierProfile::UpdateFaceTexture()              // 0x006a01c0
{
    faceTexture = "e_f001.bmp";
    C_str lower = faceFile;
    lower.ToLower();
    const char *found = std::strstr(lower.c_str(), "x_f_");
    if (found) {
        C_str texture = "e_f";
        texture += found + std::strlen("x_f_");
        faceTexture = texture;
    }
}

void S_SoldierProfile::SetCustomName(const C_str &name) // 0x006a0d70
{
    customName = name;
    FixNameEncoding();
}

void S_SoldierProfile::FixNameEncoding()                // 0x006a10e0
{
    if (!g_gameProfile.UsesUnicodeFont())
        return;
    C_str *const names[] = {&firstName, &lastName, &nickname, &fullName, &shortName, &customName};
    for (C_str *name : names) {
        if (!IsUtf8(*name))
            ConvertToUtf8(*name);
    }
}

bool S_SoldierProfile::LoadFaceImage()                  // 0x006a1170 (DS 0x006e2ab0)
{
#ifndef HD2_DEDICATED   // the server loads no faces
    if (faceImage)
        faceImage->Release();
    if (GetIGraph()->LoadTexture(faceFile.c_str(), "x_fc_a.bmp", 4, &faceImage) < 0) {
        faceImage = nullptr;
        return false;
    }
#endif
    return true;
}

bool S_SoldierProfile::HasUnknown28(int value) const    // 0x006a11c0
{
    for (size_t i = unknown28.size(); i-- > 0;) {
        if (unknown28[i] == value)
            return true;
    }
    return false;
}

bool S_SoldierProfile::SaveGame(C_chunk &chunk, bool withDeath) const   // 0x0069fe50
{
    WriteName(chunk, CHUNK_SOLDIER_FIRST_NAME, firstName);
    WriteName(chunk, CHUNK_SOLDIER_LAST_NAME, lastName);
    WriteName(chunk, CHUNK_SOLDIER_NICKNAME, nickname);
    WriteName(chunk, CHUNK_SOLDIER_FACE, faceFile);
    chunk.WriteChunk(CHUNK_SOLDIER_UNKNOWN24, unknown24);
    chunk.WriteChunk(CHUNK_SOLDIER_UNKNOWN38, unknown38);
    chunk.WriteChunk(CHUNK_SOLDIER_UNKNOWN3C, unknown3C);
    chunk.WriteChunk(CHUNK_SOLDIER_UNKNOWN40, unknown40);
    chunk.Begin(CHUNK_SOLDIER_STATS);
    chunk.Write(unknown44);
    chunk.Write(stats, sizeof(stats));
    chunk.Ascend();
    for (int value : unknown28)
        chunk.WriteChunk(CHUNK_SOLDIER_UNKNOWN28, value);
    if (withDeath) {
        // (The original writes the struct's 8 bytes; the padding is zero here.)
        const uint8_t deathFlags[4] = {static_cast<uint8_t>(dead), 0, 0, 0};
        chunk.Begin(CHUNK_SOLDIER_DEATH);
        chunk.Write(unknown6C);
        chunk.Write(deathFlags, sizeof(deathFlags));
        chunk.Ascend();
    }
    return true;
}

bool S_SoldierProfile::LoadGame(C_chunk &chunk)         // 0x006a0370
{
    unknown28.clear();
    // Fix: an old stats chunk too short to be read left the record uninitialised (it was
    // still applied).
    int oldStats[19] = {};
    uint16_t id = 0;
    while (chunk.RemainingSize() > 0) {
        uint16_t next;
        if (chunk.Descend(next))
            id = next;
        switch (id) {
        case CHUNK_SOLDIER_STATS_OLD2:
            chunk.Read(oldStats, 0x4C);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_FULL_NAME_OLD: {
            const C_str text = ReadName(chunk);
            chunk.Ascend();
            // Fix: the text went into a 128-byte buffer without a size check.
            char name[128];
            std::strncpy(name, text.c_str(), sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            if (name[0] != '\0') {
                // "first (nick) last", "first last"; without a space and a '(' the first name
                // is not taken.
                char *space = std::strchr(name, ' ');
                char *open = std::strchr(name, '(');
                char *close = std::strchr(name, ')');
                if (space) {
                    *space = '\0';
                    firstName = name;
                } else if (open) {
                    *open = '\0';
                    firstName = name;
                }
                if (open && close) {
                    *close = '\0';
                    nickname = open + 1;
                }
                if (close) {
                    // After the ')' (and one space).
                    const char *last = close + 1;
                    if (*last == ' ')
                        ++last;
                    lastName = last;
                } else if (space) {
                    lastName = space + 1;
                }
            }
            UpdateNames();
            break;
        }
        case CHUNK_SOLDIER_FIRST_NAME:
            firstName = ReadName(chunk);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_LAST_NAME:
            lastName = ReadName(chunk);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_NICKNAME:
            nickname = ReadName(chunk);
            chunk.Ascend();
            UpdateNames();
            break;
        case CHUNK_SOLDIER_FACE:
            faceFile = ReadName(chunk);
            chunk.Ascend();
            UpdateFaceTexture();
            break;
        case CHUNK_SOLDIER_STATS_OLD1:
            chunk.Read(oldStats, 0x48);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_DEATH: {
            // (+0x6C and the flag at +0x70 as one 8-byte block.)
            uint8_t block[8];
            if (chunk.Read(block, sizeof(block))) {
                std::memcpy(&unknown6C, block, sizeof(unknown6C));
                dead = block[4] != 0;
            }
            chunk.Ascend();
            break;
        }
        case CHUNK_SOLDIER_STATS_INT: {
            int value = 0;
            chunk.Read(value);
            unknown44 = value;
            for (float &stat : stats) {
                chunk.Read(value);
                stat = static_cast<float>(value);
            }
            chunk.Ascend();
            break;
        }
        case CHUNK_SOLDIER_STATS: {
            // (+0x44 and the stats as one 0x28-byte block.)
            uint8_t block[0x28];
            if (chunk.Read(block, sizeof(block))) {
                std::memcpy(&unknown44, block, sizeof(unknown44));
                std::memcpy(stats, block + 4, sizeof(stats));
            }
            chunk.Ascend();
            break;
        }
        case CHUNK_SOLDIER_UNKNOWN24:
            chunk.Read(unknown24);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_UNKNOWN38:
            chunk.Read(unknown38);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_UNKNOWN3C:
            chunk.Read(unknown3C);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_UNKNOWN40:
            chunk.Read(unknown40);
            chunk.Ascend();
            break;
        case CHUNK_SOLDIER_UNKNOWN28: {
            int value = 0;
            chunk.Read(value);
            chunk.Ascend();
            unknown28.push_back(value);
            break;
        }
        default:
            // (Other chunks are entered and not left.)
            break;
        }
        if (id == CHUNK_SOLDIER_STATS_OLD1 || id == CHUNK_SOLDIER_STATS_OLD2) {
            unknown44 = oldStats[0];
            for (int i = 0; i < 9; ++i)
                stats[i] = static_cast<float>(oldStats[OLD_STATS_INDEX[i]]);
        }
    }
    FixNameEncoding();
    LoadFaceImage();
    return true;
}

// ---------------------------------------------------------------------------
// C_HumanStore

S_SoldierProfile *GetSoldierProfile(int index)          // 0x006a1f50
{
    return g_gameProfile.m_humanStore->Get(index);
}

C_HumanStore::~C_HumanStore()                           // 0x006b65f0
{
    Clear();
}

void C_HumanStore::Clear()                              // 0x0069fc00
{
    for (S_SoldierProfile *&profile : m_profiles) {
        delete profile;
        profile = nullptr;
    }
    m_profiles.clear();
    for (S_RankName &rank : g_rankNames) {
        rank.name = g_texts.Get(rank.textId);
        rank.name2 = g_texts.Get(rank.textId2);
    }
}

bool C_HumanStore::Reload()                             // 0x006a1b70
{
    Clear();
    return Load(C_str(ROSTER_FILE));
}

bool C_HumanStore::Load(C_str fileName)                 // 0x006a1bb0
{
    C_chunk chunk;
    if (!chunk.Open(fileName.c_str(), false))
        return false;
    uint16_t id;
    chunk.Descend(id);
    const bool ok = LoadGame(chunk);
    chunk.Close();
    return ok;
}

bool C_HumanStore::SaveGame(C_chunk &chunk, bool withDeath)   // 0x006a1a50
{
    for (S_SoldierProfile *profile : m_profiles) {
        chunk.Begin(CHUNK_ROSTER_SOLDIER);
        profile->SaveGame(chunk, withDeath);
        chunk.Ascend();
    }
    return true;
}

bool C_HumanStore::LoadGame(C_chunk &chunk)             // 0x006a1e10
{
    Clear();
    while (chunk.RemainingSize() > 0) {
        uint16_t id;
        chunk.Descend(id);
        m_profiles[Add()]->LoadGame(chunk);
        if (!chunk.IsOpen())
            break;
        chunk.Ascend();
    }
    LoadCustomNames();
    return true;
}

S_SoldierProfile *C_HumanStore::Get(int index) const    // 0x006a1f50
{
    if (index < 0 || index >= GetCount())
        return nullptr;
    return m_profiles[index];
}

void C_HumanStore::GetStats(int *stats, int index, int level) const   // 0x006a1f80
{
    const S_SoldierProfile *profile = Get(index);
    if (!profile)
        return;
    const float scale = static_cast<float>(level) * STAT_LEVEL_SCALE + 1.0f;
    stats[STAT_HEALTH] = static_cast<int>(profile->stats[STAT_HEALTH]);
    stats[STAT_STRENGTH] = static_cast<int>(profile->stats[STAT_STRENGTH]);
    stats[STAT_ENDURANCE] = static_cast<int>(scale * profile->stats[STAT_ENDURANCE]);
    // (STAT_MORALE keeps the caller's value.)
    for (int stat = STAT_SHOOTING; stat <= STAT_LOCKPICK; ++stat)
        stats[stat] = static_cast<int>(scale * profile->stats[stat]);
    if (stats[STAT_ENDURANCE] > STAT_MAX)
        stats[STAT_ENDURANCE] = STAT_MAX;
    for (int stat = STAT_SHOOTING; stat <= STAT_LOCKPICK; ++stat) {
        if (stats[stat] > STAT_MAX)
            stats[stat] = STAT_MAX;
    }
}

int C_HumanStore::Add()                                 // 0x006a2050
{
    S_SoldierProfile *profile = new S_SoldierProfile();
    profile->unknown6C = 1;
    m_profiles.push_back(profile);
    return GetCount() - 1;
}

bool C_HumanStore::LoadCustomNames()                    // 0x006a1200
{
    C_str path = g_gameProfile.GetProfileDirectory();
    path += "usernames.sav";
    C_chunk chunk;
    if (!chunk.Open(path.c_str(), false))
        return true;
    uint16_t id = 0;
    chunk.Descend(id);
    if (id != CHUNK_CUSTOM_NAMES) {
        chunk.Close();
        return false;
    }
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        S_SoldierProfile *profile = Get(id);
        if (!profile) {
            chunk.Close();
            return false;
        }
        uint16_t nameId = 0;
        chunk.Descend(nameId);
        if (nameId != CHUNK_SOLDIER_CUSTOM_NAME) {
            chunk.Close();
            return false;
        }
        const C_str name = ReadName(chunk);
        chunk.Ascend();
        profile->SetCustomName(name);
        chunk.Ascend();
    }
    chunk.Ascend();
    chunk.Close();
    return true;
}

bool C_HumanStore::SaveCustomNames()                    // 0x006a1680
{
    // The file is only written when a soldier has a custom name (so clearing the last one
    // leaves the old file).
    C_chunk chunk;
    for (int i = 0; i < GetCount(); ++i) {
        const C_str &name = m_profiles[i]->customName;
        if (name.IsEmpty())
            continue;
        if (!chunk.IsOpen()) {
            C_str path = g_gameProfile.GetProfileDirectory();
            path += "usernames.sav";
            if (!chunk.Open(path.c_str(), true))
                return false;
            chunk.Begin(CHUNK_CUSTOM_NAMES);
        }
        chunk.Begin(static_cast<uint16_t>(i));
        WriteName(chunk, CHUNK_SOLDIER_CUSTOM_NAME, name);
        chunk.Ascend();
    }
    if (chunk.IsOpen()) {
        chunk.Ascend();
        chunk.Close();
    }
    return true;
}
