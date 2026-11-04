// The multiplayer game's set-up: the map list, the rotations, the rules (original 0x00616c50 ..
// 0x0061bdf0 in part, 0x006267f0, 0x006acbd0; part 9h-i).
#include "game/mp_game_setup.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/chunk.h"
#include "game/game_texts.h"
#include "game/xml_node.h"

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
    for (S_MpRotation &rotation : rotations)
        rotation.Clear();
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
    unknown18 = 0;
    unknown10 = 0;
    unknown14 = 0;
    unknown15 = 0;
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
            unknown6C = 0;
            chunk.Read(unknown18);
            chunk.Read(unknown10);
            chunk.Read(unknown14);
            chunk.Read(unknown15);
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
            chunk.Read(unknown6C);
            chunk.Read(unknown18);
            chunk.Read(unknown10);
            chunk.Read(unknown14);
            chunk.Read(unknown15);
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
    chunk.Write(unknown6C);
    chunk.Write(unknown18);
    chunk.Write(unknown10);
    chunk.Write(unknown14);
    chunk.Write(unknown15);
    chunk.Ascend();
    chunk.Begin(CHUNK_RULES);
    for (const S_MpGameRules &rule : rules)
        chunk.Write(rule);
    chunk.Ascend();
    chunk.WriteStringChunk(CHUNK_SERVER_NAME, serverName.c_str());
    chunk.WriteStringChunk(CHUNK_PASSWORD, password.c_str());
    chunk.WriteChunk(CHUNK_PORT, port);
}
