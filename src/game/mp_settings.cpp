// The multiplayer session's settings (S_MultiplayerSettings, g_mpSettings; original 0x006acc10..0x006ad840): their
// defaults, the set-up they take the rules and maps from, the session's maps (part 9i-ii).
#include "game/pending.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string.h>

#include "game/controls.h"
#include "game/mp_game_setup.h"
#include "game/profile.h"
#include "net/bitstream.h"

namespace {

// The session's defaults: port 11001, the rules'.
constexpr int DEFAULT_PORT = 11001;
const char *const DEFAULT_ADMIN_PASSWORD = "\x01\x02\x03";
// The game types the set-up knows (E_MpGameType 0 .. 3).
constexpr int GAME_TYPES = 4;

// The rules' 22 values (S_MpGameRules' order) in the session's fields.
int *const *RuleFields(S_MultiplayerSettings &s, int *(&fields)[22])
{
    int *const all[22] = {&s.maxClients,      &s.pointLimit,  &s.roundLimit,  &s.roundCount,     &s.warmup,
                          &s.respawnTime,     &s.allowRespawn, &s.friendlyFire, &s.autoTeamBalance, &s.thirdPersonView,
                          &s.spawnProtection, &s.inverseDamage, &s.fallingDamage, &s.unknown25C,  &s.unknown260,
                          &s.allowVehicles,   &s.autoRestart, &s.coopDifficulty, &s.coopLives,     &s.allowCrosshair,
                          &s.spawnOnStart,    &s.teamLives};
    for (int i = 0; i < 22; ++i)
        fields[i] = all[i];
    return fields;
}

} // namespace

void S_MultiplayerSettings::SetRules(const S_MpGameRules &rules)
{
    int *fields[22];
    RuleFields(*this, fields);
    static_assert(sizeof rules == 22 * sizeof(int), "S_MpGameRules");
    int values[22];
    std::memcpy(values, &rules, sizeof values);
    for (int i = 0; i < 22; ++i)
        *fields[i] = values[i];
}

void S_MultiplayerSettings::GetRules(S_MpGameRules &rules)
{
    int *fields[22];
    RuleFields(*this, fields);
    int values[22];
    for (int i = 0; i < 22; ++i)
        values[i] = *fields[i];
    std::memcpy(&rules, values, sizeof values);
}

void S_MultiplayerSettings::ApplySetupRules()
{
    setup->SetGameType(gameType);
    SetRules(setup->GetCurrentRules());
}

void S_MultiplayerSettings::MakeSetup()
{
    setup = new C_MpGameSetup();
    setup->LoadMapList();
}

void S_MultiplayerSettings::DeleteSetup()
{
    delete setup;
    setup = nullptr;
}

bool S_MultiplayerSettings::RemoveMap(const char *name)
{
    size_t i = 0;
    while (i < maps.size() && _stricmp(name, maps[i]->GetName()) != 0)
        ++i;
    // (Not the current map; the maps after it keep their indices shifted, the current one too.)
    if (i == maps.size() || static_cast<uint32_t>(i) == static_cast<uint32_t>(currentMap))
        return false;
    delete maps[i];
    maps.erase(maps.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
}

bool S_MultiplayerSettings::AddMap(const char *name)
{
    if (!setup->loaded || gameType < 0 || gameType >= GAME_TYPES)
        return false;
    setup->SetGameType(gameType);
    const std::vector<C_MpMap *> &available = setup->GetCurrentMaps().maps;
    for (size_t i = available.size(); i-- > 0;) {
        if (_stricmp(available[i]->GetName(), name) == 0) {
            maps.push_back(new C_MpMap(*available[i]));
            return true;
        }
    }
    return false;
}

void S_MultiplayerSettings::Clear()
{
    while (!maps.empty()) {
        delete maps.back();
        maps.pop_back();
    }
    currentMap = -1;
    delete setup;
    setup = nullptr;
}

void S_MultiplayerSettings::Reset()
{
    sessionType = SESSION_NONE;
    password = "";
    adminPassword = DEFAULT_ADMIN_PASSWORD;
    unknown178 = 0;
    unknown174 = 0;
    dedicated = false;
    reinterpret_cast<S_ServerEntry *>(unknown17C)->Reset();
    std::memset(sessionName, 0, sizeof sessionName);
    S_MpGameRules rules = {};
    rules.maxClients = 32;
    rules.pointLimit = 20;
    rules.roundLimit = 0;
    rules.roundCount = 3;
    rules.warmup = 30;
    rules.respawnTime = 10;
    rules.allowRespawn = 1;
    rules.friendlyFire = 1;
    rules.autoTeamBalance = 1;
    rules.thirdPersonView = 0;
    rules.spawnProtection = 1;
    rules.inverseDamage = 100;
    rules.fallingDamage = 1;
    rules.unknown34 = 1;
    rules.unknown38 = 1;
    rules.allowVehicles = 1;
    rules.autoRestart = 0;
    rules.coopDifficulty = 3;
    rules.coopLives = -1;
    rules.allowCrosshair = 1;
    rules.spawnOnStart = 0;
    rules.teamLives = 1;
    gameType = 0;
    port = DEFAULT_PORT;
    SetRules(rules);
    currentMap = 0;
    for (size_t i = maps.size(); i-- > 0;)
        delete maps[i];
    maps.clear();
}

bool S_MultiplayerSettings::SelectMap(const char *name)
{
    if (!name) {
        // The next one, round.
        ++currentMap;
        // Fix: no maps: none current (the original divided by zero).
        if (maps.empty()) {
            currentMap = 0;
            return true;
        }
        currentMap = static_cast<int>(static_cast<uint32_t>(currentMap) % maps.size());
        return true;
    }
    for (size_t i = 0; i < maps.size(); ++i) {
        if (_stricmp(name, maps[i]->GetName()) == 0) {
            currentMap = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

C_MpMap *S_MultiplayerSettings::GetCurrentMap()
{
    return maps[static_cast<size_t>(currentMap)];
}

// ---- the session sent to the clients (part 9i-iii) --------------------------------------------------------

int g_netMaxFreq = 0;
int g_netMaxPing = 0;
int g_netMaxInactivity = 0;

namespace {

// Each value's bits: the listing and the game type, a rule, the net limits.
constexpr int BYTE_BITS = 8, RULE_BITS = 16;
// The inactivity limit is sent in seconds.
constexpr int MS_PER_SECOND = 1000;

int ReadShort(C_BitStream *stream)
{
    return static_cast<int16_t>(stream->ReadBits(RULE_BITS));
}

} // namespace

void S_MultiplayerSettings::WriteRules(C_BitStream *stream)
{
    stream->WriteBits(static_cast<uint32_t>(unknown174), BYTE_BITS);
    stream->WriteBits(static_cast<uint32_t>(gameType), BYTE_BITS);
    S_MpGameRules rules;
    GetRules(rules);
    int values[22];
    std::memcpy(values, &rules, sizeof values);
    for (int value : values)
        stream->WriteBits(static_cast<uint32_t>(value), RULE_BITS);
    stream->WriteBits(static_cast<uint32_t>(g_netMaxFreq), RULE_BITS);
    stream->WriteBits(static_cast<uint32_t>(g_netMaxPing), RULE_BITS);
    stream->WriteBits(static_cast<uint32_t>(g_netMaxInactivity / MS_PER_SECOND), RULE_BITS);
}

void S_MultiplayerSettings::ReadRules(C_BitStream *stream)
{
    unknown174 = static_cast<int>(stream->ReadBits(BYTE_BITS));
    gameType = static_cast<int>(stream->ReadBits(BYTE_BITS));
    int values[22];
    for (int &value : values)
        value = ReadShort(stream);
    S_MpGameRules rules;
    std::memcpy(&rules, values, sizeof values);
    SetRules(rules);
    g_netMaxFreq = ReadShort(stream);
    g_netMaxPing = ReadShort(stream);
    g_netMaxInactivity = ReadShort(stream) * MS_PER_SECOND;
}

void S_MultiplayerSettings::WriteCurrentMap(C_BitStream *stream)
{
    maps[static_cast<size_t>(currentMap)]->Write(stream);
}

void S_MultiplayerSettings::ReadCurrentMap(C_BitStream *stream)
{
    for (C_MpMap *map : maps)
        delete map;
    maps.clear();
    auto *map = new C_MpMap(C_str(), C_str());
    map->Read(stream);
    maps.push_back(map);
    currentMap = 0;
}

// ---- the session's description file (part 9i-iv) ---------------------------------------------------------

namespace {

// The default file; the server config's keywords (the net module's table of 0x204-byte entries: the rules' first
// 13 at 0x00846088, the game types at 0x008492ec, the domains at 0x00849d00, the voice codecs 1 .. 7 at 0x0084d36c).
const char *const DESCRIPTION_FILE = "mpdesc.txt";
const char *const RULE_KEYWORDS[13] = {"maxclients",   "pointlimit",      "roundlimit",   "roundcount", "warmup",
                                       "respawntime",  "allowrespawn",    "friendlyfire", "autoteambalance",
                                       "3rdpersonview", "spawnprotection", "inversedamage", "fallingdmg"};
const char *const GAME_TYPE_KEYWORDS[4] = {"deathmatch", "occupation", "objectives", "cooperative"};
const char *const DOMAIN_KEYWORDS[2] = {"local", "internet"};
const char *const VOICE_CODEC_KEYWORDS[7] = {"vr12", "sc03", "sc06", "truespeech", "gsm", "adpcm", "pcm"};

// A keyword of a table, or "" past it (the original read the neighbouring entries).
const char *Keyword(const char *const *table, int count, int index)
{
    return index >= 0 && index < count ? table[index] : "";
}

} // namespace

void S_MultiplayerSettings::WriteDescription(const char *fileName)
{
    FILE *file = std::fopen(fileName ? fileName : DESCRIPTION_FILE, "wt");
    // Fix: not opened: nothing (the original wrote to a null file).
    if (!file)
        return;
    std::fprintf(file, "sessionname \"%s\"\n", sessionName);
    std::fprintf(file, "style %s\n", Keyword(GAME_TYPE_KEYWORDS, 4, gameType));
    for (const C_MpMap *map : maps)
        std::fprintf(file, "mapname %s\n", map->GetName());
    std::fprintf(file, "domain %s\n", Keyword(DOMAIN_KEYWORDS, 2, unknown174));
    std::fprintf(file, "dedicated %d\n", dedicated ? 1 : 0);
    // (A dedicated LAN server's port not written.)
    if (!dedicated || unknown174 != 0)
        std::fprintf(file, "port %d\n", port);
    std::fprintf(file, "password \"%s\"\n", password.c_str());
    S_MpGameRules rules;
    GetRules(rules);
    int values[22];
    std::memcpy(values, &rules, sizeof values);
    for (int i = 0; i < 13; ++i)
        std::fprintf(file, "%s %d\n", RULE_KEYWORDS[i], values[i]);
    std::fprintf(file, "maxfreq %d\n", g_netMaxFreq);
    std::fprintf(file, "maxping %d\n", g_netMaxPing);
    std::fprintf(file, "maxinactivity %d\n", g_netMaxInactivity / MS_PER_SECOND);
    std::fprintf(file, "allowvehicles %d\n", allowVehicles);
    std::fprintf(file, "autorestart %d\n", autoRestart);
    std::fprintf(file, "coopdifficulty %d\n", coopDifficulty);
    std::fprintf(file, "cooplives %d\n", coopLives);
    std::fprintf(file, "allowcrosshair %d\n", allowCrosshair);
    std::fprintf(file, "spawnonstart %d\n", spawnOnStart);
    std::fprintf(file, "teamlives %d\n", teamLives);
    // The voice chat's codec (the game's session's, not this one's; 0: none written).
    // Fix: its name written (the original passed the keyword's table entry by value to %s).
    const int codec = g_mpSettings->unknown178;
    if (codec >= 1 && codec <= 7)
        std::fprintf(file, "voicechat %s\n", VOICE_CODEC_KEYWORDS[codec - 1]);
    std::fprintf(file, "server\n");
    std::fclose(file);
}
