// The multiplayer game's set-up: the map list, the rotations, the rules (original 0x00616c50 ..
// 0x0061bdf0 in part, 0x006267f0, 0x006acbd0; part 9h-i).
#include "game/mp_game_setup.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/controls.h"
#include "game/game_texts.h"
#include "game/profile.h"
#include "game/xml_node.h"
#include "net/bitstream.h"

namespace {

// The map list's files: the current directory's first.
const char *const MAP_LIST_FILES[] = {"mpmaplist.txt", "GameData\\mpmaplist.txt"};
// GAMESTYLE's types (compared with their case) and the list each fills.
struct S_GameStyle {
    const char *name;
    int type;
};
const S_GameStyle GAME_STYLES[] = {
    {"deathmatch", 0}, {"hd2multiplayer", 2}, {"teamplay", 1}, {"cooperative", 3},
};
// A MAP short of its name or directory gets this for both.
const char *const UNKNOWN_MAP = " UNKNOWN ";
// ALLOWEDITEMS' versions: below the first, the old list (the items' ids in the text); the second's
// ITEM elements (the original compares the double with these floats).
constexpr float OLD_ITEMS_VERSION = 1e-8f;
constexpr float ITEMS_VERSION = 0.1f;
constexpr double VERSION_TOLERANCE = 1e-8;

// The defaults after the list (each game type's rules).
const S_MpGameRules DEFAULT_RULES[MP_GAME_TYPES] = {
    {32, 20, 0, 3, 10, 5, 1, 0, 0, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1},
    {32, 0, 30, 3, 10, 30, 1, 1, 1, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1},
    {32, 0, 5, 3, 10, 0, 0, 1, 1, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1},
    {6, 0, 0, 3, 10, 5, 1, 1, 0, 0, 5, 100, 1, 1, 1, 1, 0, 3, 30, 1, 0, 1},
};
constexpr int TEXT_SERVER_NAME = 0x7E9;             // "H&D 2 SERVER"
constexpr int DEFAULT_PORT = 11001;

// The set-up's chunks (in 0x44D).
enum : uint16_t {
    CHUNK_RULES_13 = 0x1AA,                         // three games' first 13 rules (older)
    CHUNK_SERVER_NAME = 0x1AB,
    CHUNK_PASSWORD = 0x1AC,
    CHUNK_OPTIONS_OLD = 0x1AD,                      // the options without +0x6C (older)
    CHUNK_RULES_14 = 0x1AE,                         // three games' first 14 rules (older)
    CHUNK_PORT = 0x1AF,
    CHUNK_RULES_17 = 0x1B0,                         // three games' first 17 rules (older)
    CHUNK_RULES_17x4 = 0x1B4,                       // four games' first 17 rules (older)
    CHUNK_ROTATION_INDEX = 0x1B5,
    CHUNK_ROTATION = 0x1B6,
    CHUNK_OPTIONS = 0x1B7,
    CHUNK_RULES = 0x1B8,
};
// A rotation's lists (the game types in order) and their maps' names.
constexpr uint16_t ROTATION_CHUNKS[MP_GAME_TYPES] = {0x1A4, 0x1A6, 0x1A8, 0x1B2};
constexpr uint16_t CHUNK_MAP_NAME = 0x4D2;
// The first two lists read at most this much of a name chunk (the original's buffers).
constexpr int MAX_NAME_READ = 999;

// MPSetings.sav (sic) and its chunks.
const char *const SETTINGS_FILE = "%s\\%s\\MPSetings.sav";
const char *const PROFILES_DIRECTORY = "PlayersProfiles";
enum : uint16_t {
    CHUNK_SETTINGS = 0x4E6,                         // the root
    CHUNK_SETUP = 0x44D,
    CHUNK_FILTER = 0x4B0,                           // twice: the root's child, then the filter's own
    CHUNK_FILTER_OLD = 0x4B1,                       // all but sameVersion (older)
    CHUNK_FILTER_UNKNOWN = 0x4B2,
    CHUNK_FILTER_ALL = 0x4B3,
    CHUNK_FILTER_VERSION = 0x514,
};
constexpr int FILTER_VERSION = 0x42;

// The rest of the current chunk as text (at most 999 characters, C_chunk 0x00415d00), the chunk left.
C_str ReadRemainingString(C_chunk &chunk)
{
    char text[1000];
    chunk.ReadString(text, sizeof text);
    chunk.Ascend();
    return C_str(text);
}

// An attribute's number (atoi), -1 without the attribute.
int IntAttribute(const C_XmlNode *node, const char *name)
{
    const char *value = node->GetAttribute(name);
    return value ? std::atoi(value) : -1;
}

} // namespace

int C_MpMap::AddObjective(const S_MpMapObjective &objective)
{
    objectives.push_back(objective);
    return static_cast<int>(objectives.size());
}

void S_MpRotation::Clear()
{
    for (S_MpMapGroup &group : types)
        group.maps.clear();
}

void S_MpRotation::Load(C_chunk &chunk, const C_MpGameSetup &setup)
{
    Clear();
    uint16_t id = 0;
    while (!chunk.AtEnd()) {
        chunk.Descend(id);
        int type = -1;
        for (int i = 0; i < MP_GAME_TYPES; ++i)
            if (id == ROTATION_CHUNKS[i])
                type = i;
        if (type < 0) {
            chunk.Ascend();
            continue;
        }
        while (!chunk.AtEnd()) {
            chunk.Descend(id);
            C_str name;
            if (type < 2) {
                // (The original reads the bytes left, at most 999, as a string: those of a name chunk.)
                char text[1000] = {};
                const int remaining = chunk.RemainingSize();
                chunk.Read(text, remaining < MAX_NAME_READ + 1 ? remaining : MAX_NAME_READ);
                name = text;
                chunk.Ascend();
            } else {
                name = ReadRemainingString(chunk);
            }
            if (C_MpMap *map = setup.FindMap(type, name))
                types[type].maps.push_back(map);
        }
        chunk.Ascend();
    }
}

void S_MpRotation::Save(C_chunk &chunk) const
{
    for (int type = 0; type < MP_GAME_TYPES; ++type) {
        chunk.Begin(ROTATION_CHUNKS[type]);
        for (const C_MpMap *map : types[type].maps)
            chunk.WriteChunk(CHUNK_MAP_NAME, map->GetName(), static_cast<int>(std::strlen(map->GetName())) + 1);
        chunk.Ascend();
    }
}

C_MpGameSetup::~C_MpGameSetup()
{
    Clear();
}

void C_MpGameSetup::Clear()
{
    for (S_MpRotation &each : rotations)
        each.Clear();
    for (S_MpMapGroup &group : maps) {
        for (std::size_t i = group.maps.size(); i-- > 0;)
            delete group.maps[i];
        group.maps.clear();
    }
}

C_MpMap *C_MpGameSetup::FindMap(int type, const C_str &name) const
{
    const S_MpMapGroup &group = maps[type >= 0 && type < MP_GAME_TYPES ? type : MP_GAME_TYPES - 1];
    for (std::size_t i = group.maps.size(); i-- > 0;)
        if (_stricmp(name.c_str(), group.maps[i]->GetName()) == 0)
            return group.maps[i];
    return nullptr;
}

void C_MpGameSetup::LoadItems(C_XmlNode *node, C_MpMap *map)
{
    for (C_XmlNode *item = node->Next(); item; item = node->Next()) {
        if (!item->IsNamed("ITEM"))
            continue;
        S_MpMapItem entry;
        entry.itemId = IntAttribute(item, "item_id");
        entry.count = IntAttribute(item, "item_num");
        entry.ammo = IntAttribute(item, "ammo_num");
        map->items.push_back(entry);
    }
}

void C_MpGameSetup::LoadMapList()
{
    Clear();
    C_XmlNode root;
    if (!root.Load(MAP_LIST_FILES[0]) && !root.Load(MAP_LIST_FILES[1]))
        return;

    if (root.Next()->IsNamed("MAP_LIST")) {
        S_MpMapGroup *group = nullptr;
        C_MpMap *map = nullptr;
        // The old ALLOWEDITEMS' last id (the original's is not initialised before the first).
        int itemId = 0;
        for (C_XmlNode *node = root.Next(); node; node = root.Next()) {
            if (node->IsNamed("GAMESTYLE")) {
                // (A GAMESTYLE without a type: the original compares a null pointer.)
                const char *type = node->GetAttribute("type");
                const S_GameStyle *style = nullptr;
                for (const S_GameStyle &known : GAME_STYLES)
                    if (type && std::strcmp(type, known.name) == 0) {
                        style = &known;
                        break;
                    }
                if (!style)
                    break;
                group = &maps[style->type];
            } else if (node->IsNamed("MAP")) {
                if (!group)
                    break;
                const char *name = node->GetAttribute("name");
                const char *directory = node->GetAttribute("dir");
                // (The original formats the attributes, as C_fstr's format.)
                if (name && directory) {
                    map = new C_MpMap(C_str(name), C_str(directory));
                    const char *mode = node->GetAttribute("mode");
                    map->frontline = mode && _stricmp(mode, "FRONTLINE") == 0 ? 1 : 0;
                } else {
                    map = new C_MpMap(C_str(UNKNOWN_MAP), C_str(UNKNOWN_MAP));
                }
                map->description = IntAttribute(node, "desc");
                group->maps.push_back(map);
            } else if (node->IsNamed("OBJECTIVE")) {
                S_MpMapObjective objective;
                objective.alliedText = IntAttribute(node, "allied_text");
                objective.axisText = IntAttribute(node, "axis_text");
                objective.points = IntAttribute(node, "value");
                objective.vipUniform = -1;
                objective.team = -1;
                const char *type = node->GetAttribute("type");
                if (type && _stricmp(type, "VIP") == 0) {
                    objective.type = 1;
                    objective.vipUniform = IntAttribute(node, "vip_uniform_id");
                } else {
                    objective.type = -1;
                }
                const char *side = node->GetAttribute("side");
                if (!side || _stricmp(side, "ALLIED") == 0)
                    objective.team = 1;
                else if (_stricmp(side, "AXIS") == 0)
                    objective.team = 0;
                // Fix: before any MAP, nothing (the original adds it to a null map).
                if (map)
                    map->AddObjective(objective);
            } else if (node->IsNamed("AIRCRAFTATACK")) {
                const char *allied = node->GetAttribute("allied");
                const char *axis = node->GetAttribute("axis");
                // Fix: before any MAP, nothing (the original writes through a null map).
                if (allied && map)
                    map->aircraftAllied = std::atoi(allied);
                if (axis && map)
                    map->aircraftAxis = std::atoi(axis);
            } else if (node->IsNamed("BALANCED")) {
                const char *allied = node->GetAttribute("allied");
                const char *axis = node->GetAttribute("axis");
                if (!allied || !axis)
                    break;
                if (map) {                          // (fix, as above)
                    map->balancedAllied = std::atoi(allied);
                    map->balancedAxis = std::atoi(axis);
                }
            } else if (node->IsNamed("ALLOWEDITEMS")) {
                const char *text = node->GetAttribute("version");
                const double version = text ? std::atof(text) : 0.0;
                if (version >= OLD_ITEMS_VERSION) {
                    if (std::fabs(version - ITEMS_VERSION) < VERSION_TOLERANCE && map)   // (fix: map)
                        LoadItems(node, map);
                    continue;
                }
                // The old list: the ids in the text, separated by spaces. (As the original: a sign is not
                // skipped (a negative id repeats for ever); spaces at the end add the last id again.)
                const char *p = node->GetText().c_str();
                while (*p != '\0') {
                    while (std::isspace(static_cast<unsigned char>(*p)))
                        ++p;
                    if (std::sscanf(p, "%d", &itemId) == 0)
                        break;
                    if (map)                        // (fix, as above)
                        map->items.push_back({itemId, -1, -1});
                    while (std::isdigit(static_cast<unsigned char>(*p)))
                        ++p;
                }
            } else if (node->IsNamed("ALLOWEDPRESETS")) {
                if (map)                            // (fix, as above)
                    map->presets = C_str(node->GetText().c_str());
            } else if (!node->IsNamed("ZONEGRAPH")) {
                break;
            }
        }
    }

    for (int type = 0; type < MP_GAME_TYPES; ++type)
        rules[type] = DEFAULT_RULES[type];
    serverName = g_texts.Get(TEXT_SERVER_NAME);
    password = C_str("");
    port = DEFAULT_PORT;
    gameType = 0;
    voiceCodec = 0;
    internet = 0;
    dedicated = 0;
    loaded = true;
}

void C_MpGameSetup::Load(C_chunk &chunk)
{
    // (The original's id before the first chunk read is not initialised.)
    uint16_t id = 0;
    while (!chunk.AtEnd()) {
        chunk.Descend(id);
        switch (id) {
        case CHUNK_RULES_13:
            for (int type = 0; type < 3; ++type)
                chunk.Read(&rules[type], 0x34);
            chunk.Ascend();
            break;
        case CHUNK_SERVER_NAME:
            serverName = ReadRemainingString(chunk);
            break;
        case CHUNK_PASSWORD:
            password = ReadRemainingString(chunk);
            break;
        case CHUNK_OPTIONS_OLD:
            rotation = 0;
            chunk.Read(gameType);
            chunk.Read(voiceCodec);
            chunk.Read(internet);
            chunk.Read(dedicated);
            chunk.Ascend();
            break;
        case CHUNK_RULES_14:
            for (int type = 0; type < 3; ++type)
                chunk.Read(&rules[type], 0x38);
            chunk.Ascend();
            break;
        case CHUNK_PORT:
            chunk.Read(port);
            chunk.Ascend();
            break;
        case CHUNK_RULES_17:
            for (int type = 0; type < 3; ++type)
                chunk.Read(&rules[type], 0x44);
            chunk.Ascend();
            break;
        case CHUNK_RULES_17x4:
            for (S_MpGameRules &rule : rules)
                chunk.Read(&rule, 0x44);
            chunk.Ascend();
            break;
        case CHUNK_ROTATION:
            // (The id is the same variable: a child not read keeps the last.)
            chunk.Descend(id);
            if (id == CHUNK_ROTATION_INDEX) {
                int index = 0;
                chunk.Read(index);
                chunk.Ascend();
                // Fix: an index out of range skips the rotation (the original reads it out of bounds).
                if (index >= 0 && index < MP_ROTATIONS)
                    rotations[index].Load(chunk, *this);
            }
            chunk.Ascend();
            break;
        case CHUNK_OPTIONS:
            chunk.Read(rotation);
            chunk.Read(gameType);
            chunk.Read(voiceCodec);
            chunk.Read(internet);
            chunk.Read(dedicated);
            chunk.Ascend();
            break;
        case CHUNK_RULES:
            for (S_MpGameRules &rule : rules)
                chunk.Read(&rule, sizeof rule);
            chunk.Ascend();
            break;
        default:
            chunk.Ascend();
            break;
        }
    }
}

void C_MpGameSetup::Save(C_chunk &chunk) const
{
    for (int index = 0; index < MP_ROTATIONS; ++index) {
        chunk.Begin(CHUNK_ROTATION);
        chunk.WriteChunk(CHUNK_ROTATION_INDEX, index);
        rotations[index].Save(chunk);
        chunk.Ascend();
    }
    chunk.Begin(CHUNK_OPTIONS);
    chunk.Write(rotation);
    chunk.Write(gameType);
    chunk.Write(voiceCodec);
    chunk.Write(internet);
    chunk.Write(dedicated);
    chunk.Ascend();
    chunk.Begin(CHUNK_RULES);
    for (const S_MpGameRules &rule : rules)
        chunk.Write(rule);
    chunk.Ascend();
    chunk.WriteStringChunk(CHUNK_SERVER_NAME, serverName.c_str());
    chunk.WriteStringChunk(CHUNK_PASSWORD, password.c_str());
    chunk.WriteChunk(CHUNK_PORT, port);
}

// ---- the server browser's filter (part 9h-ii) ------------------------------------------------------------

void S_ServerFilter::Reset()
{
    maxPing = -1;
    gameType = -1;
    hideUnreachable = false;
    dedicatedOnly = false;
    hideEmpty = false;
    hideFull = false;
    noPassword = false;
    sameVersion = false;
    changed = false;
    unknown10 = 0;
    unknown11 = 0;
}

bool S_ServerFilter::Load(C_chunk &chunk)
{
    // (The original's id before the first chunk read is not initialised; a child not read keeps the last.)
    uint16_t id = 0;
    bool loaded = false;
    chunk.Descend(id);
    if (id == CHUNK_FILTER && chunk.IsOpen()) {
        chunk.Descend(id);
        if (id == CHUNK_FILTER_VERSION) {
            int version = 0;
            chunk.Read(version);
            chunk.Ascend();
            if (version == FILTER_VERSION) {
                while (!chunk.AtEnd()) {
                    chunk.Descend(id);
                    switch (id) {
                    case CHUNK_FILTER_OLD:
                        chunk.Read(maxPing);
                        chunk.Read(gameType);
                        chunk.Read(hideUnreachable);
                        chunk.Read(dedicatedOnly);
                        chunk.Read(hideEmpty);
                        chunk.Read(hideFull);
                        chunk.Read(noPassword);
                        chunk.Read(changed);
                        sameVersion = false;
                        break;
                    case CHUNK_FILTER_UNKNOWN:
                        chunk.Read(unknown10);
                        chunk.Read(unknown11);
                        break;
                    case CHUNK_FILTER_ALL:
                        chunk.Read(maxPing);
                        chunk.Read(gameType);
                        chunk.Read(hideUnreachable);
                        chunk.Read(dedicatedOnly);
                        chunk.Read(hideEmpty);
                        chunk.Read(hideFull);
                        chunk.Read(noPassword);
                        chunk.Read(sameVersion);
                        chunk.Read(changed);
                        break;
                    default:
                        break;
                    }
                    chunk.Ascend();
                }
                loaded = true;
            }
        }
    }
    chunk.Ascend();
    return loaded;
}

bool S_ServerFilter::Save(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_FILTER);
    chunk.WriteChunk(CHUNK_FILTER_VERSION, FILTER_VERSION);
    chunk.Begin(CHUNK_FILTER_ALL);
    chunk.Write(maxPing);
    chunk.Write(gameType);
    chunk.Write(hideUnreachable);
    chunk.Write(dedicatedOnly);
    chunk.Write(hideEmpty);
    chunk.Write(hideFull);
    chunk.Write(noPassword);
    chunk.Write(sameVersion);
    chunk.Write(changed);
    chunk.Ascend();
    chunk.Begin(CHUNK_FILTER_UNKNOWN);
    chunk.Write(unknown10);
    chunk.Write(unknown11);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

// ---- the main menu's multiplayer settings (part 9h-ii) ----------------------------------------------------

C_MpMenuSettings::C_MpMenuSettings()
{
    Reset();
}

C_MpMenuSettings::~C_MpMenuSettings()
{
    // (0x0061af70: the first list's servers deleted, the set-up cleared.)
    ClearServers();
    m_setup.Clear();
}

void C_MpMenuSettings::Reset()
{
    m_setup.LoadMapList();
    m_filter.Reset();
}

void C_MpMenuSettings::Load()
{
    LoadFile(g_gameProfile.GetPlayerProfile()->GetName());
}

void C_MpMenuSettings::Save()
{
    SaveFile(g_gameProfile.GetPlayerProfile()->GetName());
}

void C_MpMenuSettings::LoadFile(C_str player)
{
    C_chunk chunk;
    if (!chunk.Open(C_fstr(SETTINGS_FILE, PROFILES_DIRECTORY, player.c_str()).c_str(), false)) {
        Reset();
        return;
    }
    // (The root's id is not checked.)
    uint16_t id = 0;
    chunk.Descend(id);
    while (!chunk.AtEnd()) {
        chunk.Descend(id);
        if (id == CHUNK_SETUP)
            m_setup.Load(chunk);
        else if (id == CHUNK_FILTER)
            m_filter.Load(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    chunk.Close();
}

void C_MpMenuSettings::SaveFile(C_str player)
{
    C_chunk chunk;
    if (!chunk.Open(C_fstr(SETTINGS_FILE, PROFILES_DIRECTORY, player.c_str()).c_str(), true))
        return;
    chunk.Begin(CHUNK_SETTINGS);
    chunk.Begin(CHUNK_SETUP);
    m_setup.Save(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_FILTER);
    m_filter.Save(chunk);
    chunk.Ascend();
    chunk.Ascend();
    chunk.Close();
}

void C_MpMenuSettings::SetListKind(int kind)
{
    if (kind > 0 && kind < 3)
        m_listKind = kind;
}

std::vector<S_ServerEntry *> &C_MpMenuSettings::GetList(int kind)
{
    if (kind == 0)
        kind = m_listKind;
    return kind == 1 || kind != 2 ? m_servers : m_servers2;
}

void C_MpMenuSettings::ClearServers()
{
    for (std::size_t i = m_servers.size(); i-- > 0;)
        delete m_servers[i];
    m_servers.clear();
}

void C_MpMenuSettings::ResetServers()
{
    std::vector<S_ServerEntry *> &list = m_listKind == 1 ? m_servers : m_servers2;
    for (std::size_t i = list.size(); i-- > 0;)
        list[i]->Reset();
}

void S_ServerEntry::Reset()
{
    std::strcpy(map, " ??? ");
    maxPlayers = 0;
    players = 0;
    dedicated = false;
    internet = false;
    password = false;
    voice = false;
    listed = false;
    gameType = 4;
    ping = 9999;
}

void C_MpMenuSettings::CountServers()
{
    m_playerCount = 0;
    m_serverCount = static_cast<int>(m_servers.size());
    for (std::size_t i = m_servers.size(); i-- > 0;)
        m_playerCount += static_cast<int>(m_servers[i]->players);
}

bool C_MpMenuSettings::IsInFirstList(const S_ServerEntry *server) const
{
    for (std::size_t i = m_servers.size(); i-- > 0;)
        if (m_servers[i] == server)
            return true;
    return false;
}

bool C_MpMenuSettings::IsInSecondList(const S_ServerEntry *server) const
{
    for (std::size_t i = m_servers2.size(); i-- > 0;)
        if (m_servers2[i] == server)
            return true;
    return false;
}

S_MpMapGroup &C_MpGameSetup::GetCurrentGroup()
{
    S_MpRotation &current = rotations[rotation];
    switch (gameType) {
    case 0: return current.types[0];
    case 1: return current.types[1];
    case 2: return current.types[2];
    default: return current.types[3];
    }
}

void C_MpGameSetup::SelectRotation(int index)
{
    if (index < 0 || index >= MP_ROTATIONS)
        return;
    rotation = index;
    if (index != 0)
        return;
    // The first rotation: each game type's maps, in the list's order.
    S_MpRotation &first = rotations[0];
    first.Clear();
    for (int type = 0; type < MP_GAME_TYPES; ++type)
        first.types[type].maps = maps[type].maps;
}

S_MpGameRules &C_MpGameSetup::GetCurrentRules()
{
    switch (gameType) {
    case 0: return rules[0];
    case 1: return rules[1];
    case 2: return rules[2];
    default: return rules[3];
    }
}

S_MpMapGroup &C_MpGameSetup::GetCurrentMaps()
{
    switch (gameType) {
    case 0: return maps[0];
    case 1: return maps[1];
    case 2: return maps[2];
    default: return maps[3];
    }
}

void C_MpGameSetup::RemoveMap(int index, int)
{
    std::vector<C_MpMap *> &group = GetCurrentGroup().maps;
    if (index >= 0 && static_cast<size_t>(index) < group.size())
        group.erase(group.begin() + index);
}

void C_MpGameSetup::AddMap(int index, int)
{
    const std::vector<C_MpMap *> &list = GetCurrentMaps().maps;
    std::vector<C_MpMap *> &group = GetCurrentGroup().maps;
    if (index >= 0 && static_cast<size_t>(index) < list.size())
        group.push_back(list[static_cast<size_t>(index)]);
}

void C_MpGameSetup::MoveMap(int from, int to)
{
    std::vector<C_MpMap *> &group = GetCurrentGroup().maps;
    C_MpMap *map = group[static_cast<size_t>(from)];
    group.erase(group.begin() + from);
    group.insert(group.begin() + to, map);
}

// ---- the server list's file (part 9h-xxii) ----------------------------------------------------------------

namespace {

// PlayersProfiles\ServerList.bin: the root, its two lists (the Internet's servers, the favourites), a list's count,
// its servers; two chunks skipped.
const char *const SERVER_LIST_FILE = "%s\\ServerList.bin";
enum : uint16_t {
    CHUNK_SERVER_LIST = 0x28A, CHUNK_SERVER_COUNT = 0x28D, CHUNK_SERVER_28F = 0x28F, CHUNK_FOUND_SERVERS = 0x291,
    CHUNK_SERVER_294 = 0x294, CHUNK_FAVORITE_SERVERS = 0x295, CHUNK_SERVER = 0x296
};
// A server's own chunk (the net module's); its strings' longest read.
constexpr uint16_t CHUNK_SERVER_ENTRY = 2000;
constexpr int SERVER_STRING_SIZE = 0x32;

void DeleteServers(std::vector<S_ServerEntry *> &list)
{
    for (std::size_t i = list.size(); i-- > 0;)
        delete list[i];
    list.clear();
}

// A list's chunks (the caller leaves the list): its count (room made), its servers (each reset, then read); 0x294 (and
// in the Internet's list 0x28F) skipped; any other ends the list.
void LoadServers(C_chunk &chunk, std::vector<S_ServerEntry *> &list, bool found)
{
    while (chunk.HasMore()) {
        uint16_t id = 0;
        chunk.Descend(id);
        if (id == CHUNK_SERVER_COUNT) {
            int count = 0;
            chunk.Read(count);
            chunk.Ascend();
            if (count > 0)
                list.reserve(static_cast<std::size_t>(count));
        } else if (id == CHUNK_SERVER) {
            // (Fix: the original's name and address were not initialised before the read.)
            auto *server = new S_ServerEntry();
            server->Reset();
            server->Load(chunk);
            list.push_back(server);
            chunk.Ascend();
        } else if (id == CHUNK_SERVER_294 || (found && id == CHUNK_SERVER_28F)) {
            chunk.Ascend();
        } else {
            chunk.Ascend();
            return;
        }
    }
}

void SaveServers(C_chunk &chunk, uint16_t id, const std::vector<S_ServerEntry *> &list)
{
    chunk.Begin(id);
    chunk.WriteChunk(CHUNK_SERVER_COUNT, static_cast<int>(list.size()));
    for (const S_ServerEntry *server : list) {
        chunk.Begin(CHUNK_SERVER);
        server->Save(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
}

} // namespace

void C_MpMenuSettings::LoadServerList()
{
    DeleteServers(m_servers);
    DeleteServers(m_servers2);
    C_chunk chunk;
    uint16_t id = 0;
    if (chunk.Open(C_fstr(SERVER_LIST_FILE, PROFILES_DIRECTORY).c_str(), false))
        chunk.Descend(id);
    if (id != CHUNK_SERVER_LIST) {
        chunk.Close();
        return;
    }
    while (chunk.HasMore()) {
        chunk.Descend(id);
        if (id == CHUNK_FOUND_SERVERS)
            LoadServers(chunk, m_servers, true);
        else if (id == CHUNK_FAVORITE_SERVERS)
            LoadServers(chunk, m_servers2, false);
        chunk.Ascend();
    }
    chunk.Ascend();
    chunk.Close();
}

void C_MpMenuSettings::SaveServerList()
{
    C_chunk chunk;
    if (!chunk.Open(C_fstr(SERVER_LIST_FILE, PROFILES_DIRECTORY).c_str(), true)) {
        chunk.Close();
        return;
    }
    chunk.Begin(CHUNK_SERVER_LIST);
    SaveServers(chunk, CHUNK_FOUND_SERVERS, m_servers);
    SaveServers(chunk, CHUNK_FAVORITE_SERVERS, m_servers2);
    chunk.Ascend();
    chunk.Close();
}

bool S_ServerEntry::Save(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_SERVER_ENTRY);
    chunk.WriteSizedString(name);
    chunk.WriteSizedString(map);
    chunk.Write(address);
    chunk.Write(port);
    chunk.Write(gameType);
    chunk.Write(ping);
    chunk.Write(maxPlayers);
    chunk.Write(players);
    chunk.Write(dedicated);
    chunk.Write(internet);
    chunk.Write(password);
    chunk.Write(unknown84);
    chunk.Write(voice);
    chunk.Write(versionMinor);
    chunk.Write(versionMajor);
    chunk.Write(listed);
    chunk.Write(unknown94);
    chunk.Ascend();
    return true;
}

bool S_ServerEntry::Load(C_chunk &chunk)
{
    while (chunk.HasMore()) {
        uint16_t id = 0;
        chunk.Descend(id);
        if (id == CHUNK_SERVER_ENTRY) {
            chunk.ReadSizedString(name, SERVER_STRING_SIZE);
            chunk.ReadSizedString(map, SERVER_STRING_SIZE);
            chunk.Read(address);
            chunk.Read(port);
            chunk.Read(gameType);
            chunk.Read(ping);
            chunk.Read(maxPlayers);
            chunk.Read(players);
            chunk.Read(dedicated);
            chunk.Read(internet);
            chunk.Read(password);
            chunk.Read(unknown84);
            chunk.Read(voice);
            chunk.Read(versionMinor);
            chunk.Read(versionMajor);
            chunk.Read(listed);
            chunk.Read(unknown94);
        }
        chunk.Ascend();
    }
    return true;
}

// ---- a map sent to the clients (part 9i-iii) --------------------------------------------------------------

namespace {

// The strings' longest read; each value's bits.
constexpr int MAP_STRING_SIZE = 0x1000;
constexpr int BYTE_BITS = 8, SHORT_BITS = 16, TEAM_BITS = 2;

int ReadShort(C_BitStream *stream)
{
    return static_cast<int16_t>(stream->ReadBits(SHORT_BITS));
}

} // namespace

void S_MpMapObjective::Write(C_BitStream *stream) const
{
    stream->WriteBits(static_cast<uint32_t>(alliedText), SHORT_BITS);
    stream->WriteBits(static_cast<uint32_t>(axisText), SHORT_BITS);
    stream->WriteBits(static_cast<uint32_t>(points), SHORT_BITS);
    stream->WriteBits(static_cast<uint32_t>(type), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(team), TEAM_BITS);
    stream->WriteBits(static_cast<uint32_t>(vipUniform), SHORT_BITS);
}

void S_MpMapObjective::Read(C_BitStream *stream)
{
    alliedText = ReadShort(stream);
    axisText = ReadShort(stream);
    points = ReadShort(stream);
    type = static_cast<int>(stream->ReadBits(BYTE_BITS));
    team = static_cast<int>(stream->ReadBits(TEAM_BITS));
    vipUniform = ReadShort(stream);
}

void S_MpMapItem::Write(C_BitStream *stream) const
{
    stream->WriteBits(static_cast<uint32_t>(itemId), SHORT_BITS);
    stream->WriteBits(static_cast<uint32_t>(count), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(ammo), BYTE_BITS);
}

void S_MpMapItem::Read(C_BitStream *stream)
{
    itemId = ReadShort(stream);
    count = static_cast<int>(stream->ReadBits(BYTE_BITS));
    ammo = static_cast<int>(stream->ReadBits(BYTE_BITS));
}

void C_MpMap::Write(C_BitStream *stream) const
{
    stream->WriteString(name.c_str());
    stream->WriteString(directory.c_str());
    stream->WriteBits(static_cast<uint32_t>(aircraftAxis), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(aircraftAllied), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(balancedAxis), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(balancedAllied), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(frontline), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(description), SHORT_BITS);
    stream->WriteString(presets.c_str());
    stream->WriteBits(static_cast<uint32_t>(objectives.size()), BYTE_BITS);
    for (const S_MpMapObjective &objective : objectives)
        objective.Write(stream);
    stream->WriteBits(static_cast<uint32_t>(items.size()), BYTE_BITS);
    for (const S_MpMapItem &item : items)
        item.Write(stream);
}

void C_MpMap::Read(C_BitStream *stream)
{
    // (The bytes read unsigned: -1 comes back 255, as the original.)
    std::vector<char> text(MAP_STRING_SIZE);
    stream->ReadString(text.data(), MAP_STRING_SIZE);
    name = text.data();
    stream->ReadString(text.data(), MAP_STRING_SIZE);
    directory = text.data();
    aircraftAxis = static_cast<int>(stream->ReadBits(BYTE_BITS));
    aircraftAllied = static_cast<int>(stream->ReadBits(BYTE_BITS));
    balancedAxis = static_cast<int>(stream->ReadBits(BYTE_BITS));
    balancedAllied = static_cast<int>(stream->ReadBits(BYTE_BITS));
    frontline = static_cast<int>(stream->ReadBits(BYTE_BITS));
    description = ReadShort(stream);
    stream->ReadString(text.data(), MAP_STRING_SIZE);
    presets = text.data();
    const uint32_t objectiveCount = stream->ReadBits(BYTE_BITS);
    objectives.clear();
    for (uint32_t i = 0; i < objectiveCount; ++i) {
        S_MpMapObjective objective;
        objective.Read(stream);
        objectives.push_back(objective);
    }
    const uint32_t itemCount = stream->ReadBits(BYTE_BITS);
    items.clear();
    for (uint32_t i = 0; i < itemCount; ++i) {
        S_MpMapItem item;
        item.Read(stream);
        items.push_back(item);
    }
}
