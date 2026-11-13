// The game profile (see profile.h).
#include "game/profile.h"

#include <cstring>
#include <windows.h>

#include "common/chunk.h"
#include "common/os_files.h"
#include "game/campaign.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/pending.h"
#include "script/script_module.h"

C_GameProfile g_gameProfile;                            // 0x008aea0c (static init 0x006b4d40)
C_Campaign *&g_campaign = g_gameProfile.m_campaign;
S_HudOption *&g_hudOption = g_gameProfile.m_hudOption;
S_MissionSquad *&g_missionSquad = g_gameProfile.m_missionSquad;
S_MultiplayerSettings *&g_mpSettings = g_gameProfile.m_mpSettings;
S_PlayMode *&g_playMode = g_gameProfile.m_playMode;

namespace {

// The profile's part of a saved game.
enum : uint16_t {
    CHUNK_PROFILE = 1000,
    CHUNK_PROFILE_CAMPAIGN = 0x3E9,
    CHUNK_PROFILE_HUMAN_STORE = 0x3EA,
    CHUNK_PROFILE_MISSION_SQUAD = 0x3EB,
    CHUNK_PROFILE_ITEM_STOCK_18 = 0x3EC,
    CHUNK_PROFILE_ITEM_STOCK_1C = 0x3ED,
    CHUNK_PROFILE_SCRIPT_VALUES = 0x3EF,
    CHUNK_PROFILE_PLAYER = 0x3F0,
    CHUNK_PROFILE_PLAY_MODE = 0x3F1,
    CHUNK_PROFILE_HUD = 0x3F2,
    CHUNK_PLAY_MODE = 0x225C,
};

const char *const REGISTRY_KEY = "SOFTWARE\\Illusion Softworks\\Hidden & Dangerous 2";
const char *const PROFILES_DIRECTORY = "PlayersProfiles";
// The difficulty of a multiplayer game other than co-op.
constexpr int MULTIPLAYER_DIFFICULTY = 3;

} // namespace

int g_versionMajor = 1;                                 // 0x00845070
int g_versionMinor = 12;                                // 0x00845074

void __stdcall GetGameVersion(int *major, int *minor)   // 0x006b6d50
{
    *major = g_versionMajor;
    *minor = g_versionMinor;
}

// ---------------------------------------------------------------------------
// S_PlayMode

bool S_PlayMode::SaveGame(C_chunk &chunk)               // 0x006b49d0
{
    const int values[4] = {singleMission, mode, unknown0C, unknown10};
    chunk.WriteChunk(CHUNK_PLAY_MODE, values, sizeof(values));
    return true;
}

bool S_PlayMode::LoadGame(C_chunk &chunk)               // 0x006b4ad0
{
    if (!chunk.IsOpen())
        return false;
    uint16_t id = 0xFFFF;
    if (chunk.Descend(id) && id == CHUNK_PLAY_MODE) {
        int values[4];
        if (chunk.Read(values, sizeof(values))) {
            singleMission = values[0];
            mode = values[1];
            unknown0C = values[2];
            unknown10 = values[3];
        }
        chunk.Ascend();
        return true;
    }
    chunk.Ascend();
    return false;
}

// ---------------------------------------------------------------------------
// C_GameProfile

C_GameProfile::C_GameProfile()                          // 0x006b4d80
{
    m_campaign = new C_Campaign();
    m_config = new C_GameConfig();
    m_player = new C_PlayerProfile();
    m_humanStore = new C_HumanStore();
    m_itemStock18 = new C_ItemStock();
    m_itemStock1C = new C_ItemStock();
    m_missionSquad = new S_MissionSquad();
    m_mpSettings = new S_MultiplayerSettings();
    m_playMode = new S_PlayMode();
    m_hudOption = new S_HudOption();
    m_language = "";
    m_unicodeFont = false;
}

C_GameProfile::~C_GameProfile()                         // 0x006b62d0
{
    FreeObjects();
}

void C_GameProfile::FreeObjects()                       // 0x006b6330
{
    delete m_campaign;
    m_campaign = nullptr;
    delete m_config;
    m_config = nullptr;
    delete m_player;
    m_player = nullptr;
    delete m_humanStore;
    m_humanStore = nullptr;
    delete m_itemStock18;
    m_itemStock18 = nullptr;
    delete m_itemStock1C;
    m_itemStock1C = nullptr;
    delete m_missionSquad;
    m_missionSquad = nullptr;
    delete m_mpSettings;
    m_mpSettings = nullptr;
    delete m_playMode;
    m_playMode = nullptr;
    delete m_hudOption;
    m_hudOption = nullptr;
}

C_str C_GameProfile::GetLanguage()                      // 0x006b6ad0 (DS 0x006f9be0)
{
#ifdef HD2_DEDICATED   // the server's is always English (no registry read, no menu font)
    m_language = "english";
#else
    if (!m_language.IsEmpty())
        return m_language;
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, REGISTRY_KEY, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        char value[128] = {};
        // Fix: a value filling the buffer was read without a terminator.
        DWORD size = sizeof(value) - 1;
        if (RegQueryValueExA(key, "language", nullptr, nullptr, reinterpret_cast<BYTE *>(value), &size) == ERROR_SUCCESS)
            m_language = value;
        else
            m_language = "english";
        RegCloseKey(key);
    } else {
        m_language = "english";
    }
    // The menus' unicode font: Japanese, Russian, or where the language's texts configure a
    // font.
    m_unicodeFont = IsLanguage("Japan") || IsLanguage("Russian") ||
                    FileExists(C_fstr("%s\\FontConfig.hxml", GetTextDirectory().c_str()));
#endif
    return m_language;
}

bool C_GameProfile::IsLanguage(const char *language)    // 0x006b6a70
{
    return _stricmp(GetLanguage().c_str(), language) == 0;
}

C_str C_GameProfile::GetTextDirectory()                 // 0x0048fbc0
{
    return C_fstr("Text\\%s\\", GetLanguage().c_str());
}

bool C_GameProfile::Init()                              // 0x006b68f0
{
    const bool campaigns = m_campaign->LoadFiles();
    // Sabre Squadron's campaign (the second file) is the one played.
    m_campaign->SetCurrentIndex(1);
    const bool roster = m_humanStore->Load(C_str("tables\\HumanStore.def"));
    return campaigns && roster;
}

void C_GameProfile::ResetGame()                         // 0x006b6850
{
    m_campaign->Clear();
    m_campaign->LoadFiles();
    m_humanStore->Clear();
    m_itemStock18->Reset();
    m_itemStock1C->Reset();
    m_missionSquad->Clear();
    m_player->GetDiary()->Clear();
}

void C_GameProfile::ResetSession()                      // 0x006b7310
{
    m_player->ClearCurrentDiary();
    m_itemStock1C->Reset();
    m_mpSettings->sessionType = 0;
}

void C_GameProfile::SelectLastPlayer()                  // 0x006b6940
{
    if (m_unicodeFont)
        ScanProfiles(nullptr, 0);
    m_player->SelectProfile(m_config->GetLastProfile());
}

bool C_GameProfile::SelectPlayer(const char *name)      // 0x006b6990
{
    if (!m_player->SelectProfile(name))
        return false;
    // Fix: the original passed the name to C_fstr as its format.
    m_config->SetLastProfile(C_str(name));
    return true;
}

bool C_GameProfile::IsCooperativeGame() const           // 0x006b69d0
{
    return m_mpSettings->sessionType != 0 && m_mpSettings->gameType == MP_GAME_COOPERATIVE;
}

int C_GameProfile::GetDifficulty() const                // 0x006b6a20
{
    if (IsCooperativeGame())
        return m_mpSettings->coopDifficulty;
    if (!IsLocalGame())
        return MULTIPLAYER_DIFFICULTY;
    return m_player->GetDifficulty();
}

C_PlayerOptions *C_GameProfile::GetDetailSettings()     // 0x006b6a60
{
    return m_player->GetOptions();
}

bool C_GameProfile::IsCrosshairAllowed() const          // 0x006b69f0
{
    if (!IsLocalGame())
        return m_mpSettings->allowCrosshair != 0;
    return m_player->IsCrosshairOn();
}

void C_GameProfile::SetDifficulty(int difficulty)       // 0x006b6a50
{
    m_player->SetDifficulty(difficulty);
}

void C_GameProfile::SetControlMode(int mode)            // 0x006b7720
{
    m_player->SetControlMode(mode);
}

int C_GameProfile::GetObjectiveStatus(int campaign, int operation, int mission, int objective)   // 0x006b6820
{
    return m_campaign->GetObjectiveStatus(campaign, operation, mission, objective);
}

S_SquadSoldier *C_GameProfile::FindSoldierState(int profileIndex)   // 0x006b76b0
{
    const std::vector<S_SquadSoldier *> &states = m_missionSquad->states;
    for (size_t i = states.size(); i-- > 0;) {
        if (states[i]->profileIndex == profileIndex)
            return states[i];
    }
    return nullptr;
}

C_str C_GameProfile::GetProfileDirectory()              // 0x006b7420
{
    return C_fstr("%s\\%s\\", PROFILES_DIRECTORY, m_player->GetName().c_str());
}

C_str C_GameProfile::GetSaveFileName(int a, int b)      // 0x006b7a00
{
    if (m_playMode->singleMission != 0)
        return C_str();
    C_str name = GetProfileDirectory();
    switch (m_playMode->mode) {
    case PLAY_MODE_CAMPAIGN:
        name += C_fstr("CAM_%02d%02d.sav", a, b);
        return name;
    case PLAY_MODE_LONE_WOLF:
        name += C_fstr("LOW_%02d%02d.sav", a, b);
        return name;
    case PLAY_MODE_CARNAGE:
        name += C_fstr("CAR_%02d%02d.sav", a, b);
        return name;
    default:
        return C_str();
    }
}

C_str C_GameProfile::GetQuickSaveFileName()             // 0x006b7d20
{
    C_str name = GetProfileDirectory();
    const S_PlayMode &mode = *m_playMode;
    if (mode.singleMission == 0) {
        switch (mode.mode) {
        case PLAY_MODE_CAMPAIGN: name += "CAM_quick.sav"; return name;
        case PLAY_MODE_LONE_WOLF: name += "LOW_quick.sav"; return name;
        case PLAY_MODE_CARNAGE: name += "CAR_quick.sav"; return name;
        default: return C_str();
        }
    }
    if (mode.singleMission == 1) {
        if (mode.mode == 0) {
            name += "SMCAM_quick.sav";
            return name;
        }
        if (mode.mode == PLAY_MODE_CARNAGE) {
            name += "SMCAR_quick.sav";
            return name;
        }
        return C_str();
    }
    // (Other values: the directory alone.)
    return name;
}

C_str C_GameProfile::GetLastSavePath()                  // 0x006b8050
{
    C_str path = GetProfileDirectory();
    path += *m_player->GetLastSave();
    return path;
}

void C_GameProfile::SetLastSave(const C_str &name)      // 0x006b79f0
{
    m_player->SetLastSave(name);
}

bool C_GameProfile::SaveGame(C_chunk &chunk)            // 0x006b6d70
{
    bool ok = true;
    chunk.Begin(CHUNK_PROFILE_PLAYER);
    ok &= m_player->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_PLAY_MODE);
    ok &= m_playMode->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_CAMPAIGN);
    ok &= m_campaign->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_HUMAN_STORE);
    ok &= m_humanStore->SaveGame(chunk, true);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_MISSION_SQUAD);
    ok &= m_missionSquad->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_ITEM_STOCK_18);
    ok &= m_itemStock18->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_ITEM_STOCK_1C);
    ok &= m_itemStock1C->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_SCRIPT_VALUES);
    ok &= g_scriptModule.WriteSaveGameValues(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_PROFILE_HUD);
    ok &= m_hudOption->SaveGame(chunk);
    chunk.Ascend();
    return ok;
}

bool C_GameProfile::LoadGame(C_chunk &chunk)            // 0x006b7100
{
    bool ok = true;
    // While a game runs, the parts it set up stay as they are.
    const bool inGame = GetGame() != nullptr;
    while (chunk.RemainingSize() > 0) {
        uint16_t id = 0xFFFF;
        chunk.Descend(id);
        switch (id) {
        case CHUNK_PROFILE_CAMPAIGN:
            if (!inGame)
                ok &= m_campaign->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_HUMAN_STORE:
            if (!inGame)
                ok &= m_humanStore->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_MISSION_SQUAD:
            ok &= m_missionSquad->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_ITEM_STOCK_18:
            if (!inGame)
                ok &= m_itemStock18->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_ITEM_STOCK_1C:
            ok &= m_itemStock1C->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_SCRIPT_VALUES:
            ok &= g_scriptModule.ReadSaveGameValues(chunk);
            break;
        case CHUNK_PROFILE_PLAYER:
            ok &= m_player->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_PLAY_MODE:
            if (!inGame)
                ok &= m_playMode->LoadGame(chunk);
            break;
        case CHUNK_PROFILE_HUD:
            ok &= m_hudOption->LoadGame(chunk);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return ok;
}

bool C_GameProfile::SaveGameChunk(C_chunk &chunk)       // 0x006b74e0
{
    chunk.Begin(CHUNK_PROFILE);
    const bool ok = SaveGame(chunk);
    chunk.Ascend();
    return ok;
}

bool C_GameProfile::LoadGameChunk(C_chunk &chunk)       // 0x006b75d0
{
    // (The chunk's id is not checked.)
    uint16_t id = 0xFFFF;
    chunk.Descend(id);
    const bool ok = LoadGame(chunk);
    chunk.Ascend();
    return ok;
}

bool C_GameProfile::LoadSavedGame(C_chunk &chunk)       // 0x006b7730
{
    if (!chunk.IsOpen())
        return true;
    uint16_t id = 0xFFFF;
    if (!chunk.Descend(id) || id != CHUNK_PROFILE)
        chunk.Ascend();
    bool ok = true;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        switch (id) {
        case CHUNK_PROFILE_CAMPAIGN: ok &= m_campaign->LoadGame(chunk); break;
        case CHUNK_PROFILE_HUMAN_STORE: ok &= m_humanStore->LoadGame(chunk); break;
        case CHUNK_PROFILE_MISSION_SQUAD: ok &= m_missionSquad->LoadGame(chunk); break;
        case CHUNK_PROFILE_ITEM_STOCK_18: ok &= m_itemStock18->LoadGame(chunk); break;
        case CHUNK_PROFILE_ITEM_STOCK_1C: ok &= m_itemStock1C->LoadGame(chunk); break;
        case CHUNK_PROFILE_SCRIPT_VALUES: ok &= g_scriptModule.ReadSaveGameValues(chunk); break;
        case CHUNK_PROFILE_PLAYER: ok &= m_player->LoadGame(chunk); break;
        case CHUNK_PROFILE_PLAY_MODE: ok &= m_playMode->LoadGame(chunk); break;
        case CHUNK_PROFILE_HUD: ok &= m_hudOption->LoadGame(chunk); break;
        default: break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return ok;
}
