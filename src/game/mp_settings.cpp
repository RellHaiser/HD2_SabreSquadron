// The multiplayer session's settings (S_MultiplayerSettings, g_mpSettings; original 0x006acc10..0x006ad840): their
// defaults, the set-up they take the rules and maps from, the session's maps (part 9i-ii).
#include "game/pending.h"

#include <cstddef>
#include <cstring>
#include <string.h>

#include "game/controls.h"
#include "game/mp_game_setup.h"

namespace {

// The session's defaults: port 11001, the rules'.
constexpr int DEFAULT_PORT = 11001;
const char *const DEFAULT_UNKNOWN21C = "\x01\x02\x03";
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
    unknown21C = DEFAULT_UNKNOWN21C;
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
