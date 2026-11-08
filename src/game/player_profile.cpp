// The player's profile: the profile directories and the name map of the unicode font,
// profile.sav (the old profile.pr), Options.bin with the options and the key bindings, the
// options' effects, the campaign progress and the saves' bookkeeping. Original source file
// 0x006ae7c0..0x006b4960 (see profile.h).
#include "game/profile.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <windows.h>

#include "common/chunk.h"
#include "common/config_file.h"
#include "common/os_files.h"
#include "game/campaign.h"
#include "game/game_app.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"
#include "script/script_module.h"

std::map<std::string, std::string> g_profileDirectories;   // 0x008ae9e8 (static init 0x006ae670)

namespace {

// profile.sav.
enum : uint16_t {
    CHUNK_PROFILE = 0x1EDC,
    CHUNK_PROFILE_NAME = 0x1EDD,                        // the name (unicode font)
    CHUNK_PROFILE_OLD_PROGRESS = 0x1EE6,                // difficulty and the first campaign's progress
    CHUNK_PROFILE_OLD_SAVE = 0x1EE8,                    // the last save, then its text
    CHUNK_PROFILE_LAST_SAVE = 0x1EEA,
    CHUNK_PROFILE_LAST_SAVE_TEXT = 0x1EEB,
    CHUNK_PROFILE_MOVIES = 0x1EF0,
    CHUNK_PROFILE_SLOT_MISSIONS = 0x1EF5,
    CHUNK_PROFILE_SLOT_FILE = 0x1EFA,                   // .. 0x1EFE
    CHUNK_PROFILE_DIFFICULTY = 0x1EFF,
    CHUNK_PROFILE_PROGRESS = 0x1F05,                    // and 0x1F06 for the second campaign
    // Options.bin.
    CHUNK_OPTIONS_VERSION = 0x25A0,
    CHUNK_OPTIONS = 0x25A5,
    CHUNK_OPTIONS_KEY_BINDINGS = 0x25A6,
    CHUNK_OPTION_VALUES = 0x1201,
    // The saved games' player part.
    CHUNK_SAVED_PLAYER = 1000,
    CHUNK_SAVED_DIARY = 0x15B3,
};

const char *const PROFILES_DIRECTORY = "PlayersProfiles";   // 0x0083eff4
constexpr int OPTIONS_VERSION = 0x141;
// The unicode font's profile directories Profile_0 .. Profile_199.
constexpr int MAX_PROFILE_DIRECTORIES = 200;
constexpr int DEFAULT_DIFFICULTY = 2;
constexpr int MAX_DIFFICULTY = 4;
// profile.pr's difficulties above the maximum become this.
constexpr int OLD_PROFILE_DIFFICULTY = 3;
// The strings the loaders read (999 chars).
constexpr int STRING_BUFFER_SIZE = 1000;
// The script module's saved values (cleared when a profile is selected).
constexpr int SCRIPT_SAVE_VALUES = 256;

// The last save's text: "%s$%s$%s" formats (mode, mission, date).
enum : int {
    TEXT_LAST_QUICK_SAVE = 0x802,
    TEXT_LAST_SAVE = 0x803,
    TEXT_MODE_CAMPAIGN = 0x89A,
    TEXT_MODE_LONE_WOLF = 0x89B,
    TEXT_MODE_CARNAGE = 0x89C,
    TEXT_MODE_SINGLE_MISSION = 0x89D,
    TEXT_MODE_SINGLE_CARNAGE = 0x89E,
};

// The options' scales.
constexpr float VOLUME_SCALE = 0.01f;
constexpr double VOICE_SCALE = 0.01;
constexpr float MOUSE_SCALE_BASE = 1000.0f;
constexpr float MOUSE_SCALE_PER_POINT = -9.0f;
constexpr int MOUSE_SCALE_Z = 120;
constexpr double GAMMA_LINEAR = 50.0;
constexpr double GAMMA_BASE_STEP = 255.0;
// The 3D driver state the hardware option sets.
constexpr int DRIVER_STATE_2B = 0x2B;

C_str GetText(int id)
{
    return g_texts.Get(id);
}

// The directory a profile name maps to (unicode font), or the name.
C_str GetProfileDirectoryName(const C_str &name)
{
    if (!g_gameProfile.UsesUnicodeFont())
        return name;
    const auto it = g_profileDirectories.find(name.c_str());
    return it == g_profileDirectories.end() ? C_str() : C_str(it->second.c_str());
}

// Reads the rest of the current chunk as a string (at most 999 chars) and leaves the chunk.
C_str ReadRemainingString(C_chunk &chunk)               // (as C_chunk::ReadRemainingString)
{
    char buffer[STRING_BUFFER_SIZE];
    chunk.ReadString(buffer, sizeof(buffer));
    chunk.Ascend();
    return C_str(buffer);
}

// The name profile.sav keeps for a profile directory; false (and the directory name) without.
bool ReadProfileName(C_str *name, const char *directory)   // 0x006af9e0
{
    *name = directory;
    C_chunk chunk;
    if (!chunk.Open(C_fstr("%s\\%s\\profile.sav", PROFILES_DIRECTORY, directory).c_str(), false))
        return false;
    uint16_t id = 0;
    if (chunk.Descend(id) && id == CHUNK_PROFILE) {
        while (chunk.RemainingSize() > 0) {
            if (!chunk.Descend(id))
                break;
            if (id == CHUNK_PROFILE_NAME) {
                *name = ReadRemainingString(chunk);
                chunk.Close();
                return true;
            }
            chunk.Ascend();
        }
    }
    *name = directory;
    chunk.Close();
    return false;
}

// Gives a profile name another one, keeping its directory.
bool RenameProfileName(const char *oldName, const char *newName)   // 0x006aecf0
{
    const auto it = g_profileDirectories.find(oldName);
    if (it == g_profileDirectories.end())
        return false;
    const std::string directory = it->second;
    g_profileDirectories.erase(it);
    // (A taken new name keeps its own directory: the old mapping is lost.)
    g_profileDirectories.insert({newName, directory});
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// The profile directories

int CreateProfile(const char *name)                     // 0x006ae8e0
{
    if (!SetWorkingDirectory(PROFILES_DIRECTORY)) {
        MakeDirectory(PROFILES_DIRECTORY);
        SetWorkingDirectory(PROFILES_DIRECTORY);
    }
    if (!g_gameProfile.UsesUnicodeFont()) {
        if (MakeDirectory(name) == 0) {
            SetWorkingDirectory(nullptr);
            return 1;
        }
        const int error = errno;
        SetWorkingDirectory(nullptr);
        return error == EEXIST ? -1 : -2;
    }
    if (!name || !*name)
        return -2;
    C_str directory;
    for (int i = 0;; ++i) {
        directory = C_fstr("Profile_%d", i);
        if (MakeDirectory(directory.c_str()) == 0)
            break;
        if (errno != EEXIST) {
            SetWorkingDirectory(nullptr);
            return -2;
        }
        if (i + 1 >= MAX_PROFILE_DIRECTORIES) {
            SetWorkingDirectory(nullptr);
            return -1;
        }
    }
    SetWorkingDirectory(nullptr);
    if (g_profileDirectories.find(name) == g_profileDirectories.end())
        g_profileDirectories.insert({name, directory.c_str()});
    return 1;
}

bool DeleteProfile(const char *name)                    // 0x006aef40
{
    // No name: every profile's contents deleted.
    if (!*name)
        return DeleteDirectoryTree(PROFILES_DIRECTORY);
    SetWorkingDirectory(PROFILES_DIRECTORY);
    // (With the unicode font a profile lives in its own directory.)
    const char *directory = name;
    if (g_gameProfile.UsesUnicodeFont()) {
        directory = FindProfileDirectory(name);
        // (Not known: the working directory left in PlayersProfiles, as the original.)
        if (!directory)
            return false;
    }
    const bool deleted = DeleteDirectoryTree(directory);
    if (deleted) {
        if (!RemoveDir(directory)) {
            SetAttributes(directory, FILE_ATTRIBUTE_NORMAL);
            RemoveDir(directory);
        }
        ForgetProfileName(name);
    }
    SetWorkingDirectory(nullptr);
    return deleted;
}

bool ForgetProfileName(const char *name)                // 0x006af0f0
{
    const auto it = g_profileDirectories.find(name);
    if (it == g_profileDirectories.end())
        return false;
    g_profileDirectories.erase(it);
    return true;
}

const char *FindProfileDirectory(const char *name)      // 0x006aefe0
{
    const auto it = g_profileDirectories.find(name);
    return it == g_profileDirectories.end() ? nullptr : it->second.c_str();
}

void ScanProfiles(void (*callback)(const char *name, int context), int context)   // 0x006af200
{
    g_profileDirectories.clear();
    _finddata_t data;
    const intptr_t handle = FileFindFirst(C_fstr("%s\\*", PROFILES_DIRECTORY).c_str(), &data);
    if (handle != -1) {
        do {
            if ((data.attrib & _A_SUBDIR) && _stricmp(data.name, ".") != 0 && _stricmp(data.name, "..") != 0) {
                // Fix: the original passed the directory name to C_fstr as its format.
                C_str name = data.name;
                if (g_gameProfile.UsesUnicodeFont()) {
                    ReadProfileName(&name, data.name);
                    if (!FindProfileDirectory(name.c_str()))
                        g_profileDirectories.insert({name.c_str(), data.name});
                }
                if (callback)
                    callback(name.c_str(), context);
            }
        } while (FileFindNext(handle, &data) != -1);
        FileFindClose(handle);
    }
    SetWorkingDirectory(nullptr);
}

void LoadGameTexts(const char *language)                // 0x006b13b0
{
    _finddata_t data;
    const intptr_t handle =
        FileFindFirst(C_fstr("%s\\text\\%s\\*.txt", GetGameDirectory(), language).c_str(), &data);
    if (handle == -1)
        return;
    do {
        if (_stricmp(data.name, ".") != 0 && _stricmp(data.name, "..") != 0 &&
            _stricmp(data.name, "dialogs.txt") != 0)
            g_texts.Load(C_fstr("text\\%s\\%s", language, data.name).c_str());
    } while (FileFindNext(handle, &data) != -1);
    FileFindClose(handle);
}

// ---------------------------------------------------------------------------
// C_PlayerOptions

void C_PlayerOptions::SetDefaults()                     // 0x006b3740
{
    static const bool DEFAULT_SWITCHES[4][7] = {
        {false, false, false, false, false, false, false},
        {false, true, false, true, false, true, false},
        {true, true, true, true, true, true, true},
        {false, true, false, true, false, true, true},
    };
    std::memcpy(switches, DEFAULT_SWITCHES, sizeof(switches));
    level = 2;
    gamma = 50.0f;
    musicVolume = 50.0f;
    soundVolume = 50.0f;
    voiceValue = 50.0f;
    mouseSensitivity = 66.6f;
    invertMouse = false;
    voiceChat = false;
    crosshair = true;
    subtitles = true;
}

bool C_PlayerOptions::IsEnabled(int option) const      // 0x006b3700
{
    // (Levels other than 0..2 use the fourth set.)
    return switches[level >= 0 && level <= 2 ? level : 3][option];
}

void C_PlayerOptions::SetSwitch(int option, bool on)   // 0x006b36a0
{
    switches[level >= 0 && level <= 2 ? level : 3][option] = on;
}

void C_PlayerOptions::Save(C_chunk &chunk) const       // 0x006b3850
{
    chunk.Begin(CHUNK_OPTION_VALUES);
    chunk.Write(mouseSensitivity);
    chunk.Write(gamma);
    chunk.Write(soundVolume);
    chunk.Write(musicVolume);
    chunk.Write(voiceValue);
    chunk.Write(unknown14);
    chunk.Write(level);
    for (const bool *levelSwitches : switches)
        chunk.Write(levelSwitches, 7);
    chunk.Write(&voiceChat, 1);
    chunk.Write(&invertMouse, 1);
    chunk.Write(&subtitles, 1);
    chunk.Write(&crosshair, 1);
    chunk.Ascend();
}

void C_PlayerOptions::Load(C_chunk &chunk)             // 0x006b3a70
{
    uint16_t id = 0;
    // Fix: when no chunk could be entered the original still left one (the parent).
    if (!chunk.Descend(id))
        return;
    if (id == CHUNK_OPTION_VALUES) {
        chunk.Read(mouseSensitivity);
        chunk.Read(gamma);
        chunk.Read(soundVolume);
        chunk.Read(musicVolume);
        chunk.Read(voiceValue);
        chunk.Read(unknown14);
        chunk.Read(level);
        for (bool *levelSwitches : switches)
            chunk.Read(levelSwitches, 7);
        chunk.Read(&voiceChat, 1);
        chunk.Read(&invertMouse, 1);
        chunk.Read(&subtitles, 1);
        chunk.Read(&crosshair, 1);
    }
    chunk.Ascend();
}

// ---------------------------------------------------------------------------
// C_PlayerProfile

C_PlayerProfile::C_PlayerProfile()                      // (C_GameProfile's constructor)
    : m_options()
{
    m_options.SetDefaults();
    for (int (&campaign)[4] : m_progress)
        std::fill_n(campaign, 4, -1);
}

C_str C_PlayerProfile::GetName() const                  // 0x006ae7e0
{
    if (!g_gameProfile.UsesUnicodeFont())
        return m_name;
    const auto it = g_profileDirectories.find(m_name.c_str());
    // Fix: an unknown name made the original format a null string (and the directory was
    // passed to C_fstr as its format).
    if (it == g_profileDirectories.end())
        return C_str();
    return C_str(it->second.c_str());
}

int C_PlayerProfile::Rename(const char *oldName, const char *newName)   // 0x006aec40
{
    // (Only the old name is tested.)
    if (!*oldName)
        return -2;
    if (g_gameProfile.UsesUnicodeFont()) {
        if (!RenameProfileName(oldName, newName))
            return -1;
    } else {
        SetWorkingDirectory(PROFILES_DIRECTORY);
        if (!RenameFile(oldName, newName)) {
            const DWORD error = GetLastError();
            SetWorkingDirectory(nullptr);
            return error == ERROR_ALREADY_EXISTS ? -1 : -2;
        }
        SetWorkingDirectory(nullptr);
    }
    m_name = newName;
    Save();
    return 1;
}

bool C_PlayerProfile::SelectProfile(const char *name)  // 0x006af480
{
    // Fix: the original passed the name to C_fstr as its format.
    C_str directory = name;
    if (g_gameProfile.UsesUnicodeFont()) {
        const char *mapped = FindProfileDirectory(name);
        directory = mapped ? C_str(mapped) : C_str();
    }
    m_name = "";
    if (!directory.IsEmpty() && SetWorkingDirectory(PROFILES_DIRECTORY, directory.c_str()))
        m_name = name;
    SetWorkingDirectory(nullptr);
    std::fill_n(g_scriptModule.GetSaveGameValues(), SCRIPT_SAVE_VALUES, 0);
    LoadOptions();
    Load();
    return true;
}

void C_PlayerProfile::Reset()                           // 0x006af6d0
{
    m_difficulty = DEFAULT_DIFFICULTY;
    for (int (&campaign)[4] : m_progress)
        std::fill_n(campaign, 4, -1);
    m_lastSave = "";
    m_lastSaveText = "";
    for (S_SaveSlotInfo &slot : m_saveSlots)
        slot.mission = -1;
    m_movies.Reset();
}

bool C_PlayerProfile::LoadOldProfile()                  // 0x006af740
{
    char value[STRING_BUFFER_SIZE] = "";
    Reset();
    SetWorkingDirectory(PROFILES_DIRECTORY, m_name.c_str());
    C_ConfigFile file;
    if (file.Load(C_str("profile.pr"))) {
        file.GetInt("GAME_DIFFICULTY", &m_difficulty);
        // Fix: a key the file lacks took the value of the key before (the first time an
        // uninitialised one).
        const char *const progressKeys[4] = {"LASTGAME", "LASTCAMPAIGN", "LASTLONEWOLF", "LASTCARNAGE"};
        for (int i = 0; i < 4; ++i) {
            int progress = -1;
            file.GetInt(progressKeys[i], &progress);
            m_progress[0][i] = progress;
        }
        // (Keys match anywhere in a line, so a LASTSAVETEXT line before the LASTSAVE one is
        // read as the last save; without LASTSAVETEXT the text is the last save's name.)
        file.GetString("LASTSAVE", value, sizeof(value));
        m_lastSave = value;
        file.GetString("LASTSAVETEXT", value, sizeof(value));
        m_lastSaveText = value;
        if (m_difficulty > MAX_DIFFICULTY)
            m_difficulty = OLD_PROFILE_DIFFICULTY;
    }
    SetWorkingDirectory(nullptr);
    CheckSaves();
    return true;
}

bool C_PlayerProfile::Load()                            // 0x006afdb0
{
    C_chunk chunk;
    const C_str directory = GetProfileDirectoryName(m_name);
    if (!chunk.Open(C_fstr("%s\\%s\\profile.sav", PROFILES_DIRECTORY, directory.c_str()).c_str(), false))
        return LoadOldProfile();
    Reset();
    uint16_t id = 0;
    if (!chunk.Descend(id) || id != CHUNK_PROFILE) {
        chunk.Close();
        return false;
    }
    bool oldSaveText = false;
    while (chunk.RemainingSize() > 0) {
        uint16_t next;
        if (chunk.Descend(next))
            id = next;
        switch (id) {
        case CHUNK_PROFILE_NAME:
            if (g_gameProfile.UsesUnicodeFont())
                m_name = ReadRemainingString(chunk);
            else
                chunk.Ascend();
            break;
        case CHUNK_PROFILE_OLD_PROGRESS:
            chunk.Read(m_difficulty);
            for (int &progress : m_progress[0])
                chunk.Read(progress);
            chunk.Ascend();
            break;
        case CHUNK_PROFILE_OLD_SAVE:
            if (oldSaveText) {
                m_lastSaveText = ReadRemainingString(chunk);
            } else {
                m_lastSave = ReadRemainingString(chunk);
                oldSaveText = true;
            }
            break;
        case CHUNK_PROFILE_LAST_SAVE:
            m_lastSave = ReadRemainingString(chunk);
            break;
        case CHUNK_PROFILE_LAST_SAVE_TEXT:
            m_lastSaveText = ReadRemainingString(chunk);
            break;
        case CHUNK_PROFILE_MOVIES:
            m_movies.Load(chunk);
            chunk.Ascend();
            break;
        case CHUNK_PROFILE_SLOT_MISSIONS:
            for (S_SaveSlotInfo &slot : m_saveSlots)
                chunk.Read(slot.mission);
            chunk.Ascend();
            break;
        case CHUNK_PROFILE_SLOT_FILE + 0:
        case CHUNK_PROFILE_SLOT_FILE + 1:
        case CHUNK_PROFILE_SLOT_FILE + 2:
        case CHUNK_PROFILE_SLOT_FILE + 3:
        case CHUNK_PROFILE_SLOT_FILE + 4:
            m_saveSlots[id - CHUNK_PROFILE_SLOT_FILE].file = ReadRemainingString(chunk);
            break;
        case CHUNK_PROFILE_DIFFICULTY:
            chunk.Read(m_difficulty);
            chunk.Ascend();
            break;
        case CHUNK_PROFILE_PROGRESS:
        case CHUNK_PROFILE_PROGRESS + 1:
            for (int &progress : m_progress[id - CHUNK_PROFILE_PROGRESS])
                chunk.Read(progress);
            chunk.Ascend();
            break;
        default:
            chunk.Ascend();
            break;
        }
    }
    CheckSaves();
    chunk.Ascend();
    chunk.Close();
    return true;
}

bool C_PlayerProfile::Save()                            // 0x006b0cc0
{
    C_chunk chunk;
    const C_str directory = GetProfileDirectoryName(m_name);
    if (!chunk.Open(C_fstr("%s\\%s\\profile.sav", PROFILES_DIRECTORY, directory.c_str()).c_str(), true))
        return false;
    chunk.Begin(CHUNK_PROFILE);
    chunk.WriteStringChunk(CHUNK_PROFILE_NAME, m_name.c_str());
    chunk.WriteChunk(CHUNK_PROFILE_DIFFICULTY, m_difficulty);
    chunk.WriteChunk(CHUNK_PROFILE_PROGRESS, m_progress[0], sizeof(m_progress[0]));
    chunk.WriteChunk(CHUNK_PROFILE_PROGRESS + 1, m_progress[1], sizeof(m_progress[1]));
    chunk.WriteStringChunk(CHUNK_PROFILE_LAST_SAVE, m_lastSave.c_str());
    chunk.WriteStringChunk(CHUNK_PROFILE_LAST_SAVE_TEXT, m_lastSaveText.c_str());
    chunk.Begin(CHUNK_PROFILE_SLOT_MISSIONS);
    for (const S_SaveSlotInfo &slot : m_saveSlots)
        chunk.Write(slot.mission);
    chunk.Ascend();
    for (int i = 0; i < SAVE_SLOT_COUNT; ++i)
        chunk.WriteStringChunk(static_cast<uint16_t>(CHUNK_PROFILE_SLOT_FILE + i), m_saveSlots[i].file.c_str());
    chunk.Begin(CHUNK_PROFILE_MOVIES);
    m_movies.Save(chunk);
    chunk.Ascend();
    chunk.Ascend();
    chunk.Close();
    return true;
}

bool C_PlayerProfile::CheckSaves()                      // 0x006b0660
{
    C_GameApp *app = GetGameApp();
    if (!app)
        return false;
    for (S_SaveSlotInfo &slot : m_saveSlots) {
        if (slot.mission != -1 && !app->GetQuickSaveMission(slot.file, nullptr)) {
            slot.mission = -1;
            slot.file = "";
        }
    }
    if (!m_lastSave.IsEmpty() && std::strstr(m_lastSave.c_str(), "quick")) {
        int mission = -1;
        if (!app->GetQuickSaveMission(m_lastSave, &mission)) {
            if (mission >= 0 && UseMissionSave(mission))
                return true;
            m_lastSaveText = "";
            m_lastSave = "";
        }
    }
    return true;
}

bool C_PlayerProfile::UseMissionSave(int mission)       // 0x006b07d0
{
    const int firstCampaignMissions = g_campaign->CountMissions(0);
    const int campaign = mission >= firstCampaignMissions ? 1 : 0;
    if (campaign)
        mission -= firstCampaignMissions;
    const int index = mission - 1;
    if (index < 0)
        return false;
    const int missionId = g_campaign->GetMissionId(campaign, index);
    if (missionId < 0)
        return false;
    const char *last = m_lastSave.c_str();
    C_str modeText;
    if (std::strstr(last, "CAM_") == last) {
        m_lastSave = C_fstr("CAM_%02d%02d.sav", campaign, index);
        modeText = GetText(TEXT_MODE_CAMPAIGN);
    } else if (std::strstr(last, "LOW_") == last) {
        m_lastSave = C_fstr("LOW_%02d%02d.sav", campaign, index);
        modeText = GetText(TEXT_MODE_LONE_WOLF);
    } else if (std::strstr(last, "CAR_") == last) {
        m_lastSave = C_fstr("CAR_%02d%02d.sav", campaign, index);
        modeText = GetText(TEXT_MODE_CARNAGE);
    } else {
        return false;
    }
    // (The profile directory ends with a backslash already.)
    if (!FileExists(C_fstr("%s\\%s", g_gameProfile.GetProfileDirectory().c_str(), m_lastSave.c_str()).c_str()))
        return false;
    const C_str format = GetText(TEXT_LAST_SAVE);
    const C_str name = GetText(missionId);
    if (format.IsEmpty() || modeText.IsEmpty() || name.IsEmpty())
        return false;
    m_lastSaveText = C_fstr(format.c_str(), modeText.c_str(), name.c_str(), "");
    return true;
}

// --- options --------------------------------------------------------------------------

bool C_PlayerProfile::SaveOptions()                     // 0x006b2c70
{
    C_chunk chunk;
    const C_str directory = GetProfileDirectoryName(m_name);
    if (!chunk.Open(C_fstr("%s\\%s\\%s", PROFILES_DIRECTORY, directory.c_str(), "Options.bin").c_str(), true))
        return false;
    chunk.WriteChunk(CHUNK_OPTIONS_VERSION, OPTIONS_VERSION);
    chunk.Begin(CHUNK_OPTIONS_KEY_BINDINGS);
    m_keyBindings.Save(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_OPTIONS);
    m_options.Save(chunk);
    chunk.Ascend();
    chunk.Close();
    return true;
}

bool C_PlayerProfile::ReadOptions()                     // 0x006b30a0
{
    C_chunk chunk;
    const C_str directory = GetProfileDirectoryName(m_name);
    uint16_t id = 0;
    if (!chunk.Open(C_fstr("%s\\%s\\%s", PROFILES_DIRECTORY, directory.c_str(), "Options.bin").c_str(), false) ||
        !chunk.Descend(id) || id != CHUNK_OPTIONS_VERSION) {
        SetDefaultOptions();
        chunk.Close();
        return false;
    }
    int version = 0;
    chunk.Read(version);
    chunk.Ascend();
    if (version != OPTIONS_VERSION) {
        SetDefaultOptions();
        chunk.Close();
        return false;
    }
    bool entered = chunk.Descend(id);
    if (entered && id == CHUNK_OPTIONS_KEY_BINDINGS)
        m_keyBindings.Load(chunk);
    else
        SetDefaultOptions();
    // Fix: the original also left a chunk when it had entered none (here and below).
    if (entered)
        chunk.Ascend();
    entered = chunk.Descend(id);
    if (entered && id == CHUNK_OPTIONS)
        m_options.Load(chunk);
    else
        SetDefaultOptions();
    if (entered)
        chunk.Ascend();
    chunk.Close();
    return true;
}

bool C_PlayerProfile::LoadOptions()                     // 0x006b3060
{
    const bool ok = ReadOptions();
    ApplyHardwareOption();
    ApplyGamma();
    ApplyVolume();
    ApplyMusicVolume();
    ApplyVoiceValue();
    ApplyMouse();
    return ok;
}

void C_PlayerProfile::SetDefaultOptions()               // 0x006b34e0
{
    // Fix: +0x14 was copied from an uninitialised local.
    C_PlayerOptions defaults = {};
    defaults.SetDefaults();
    m_options = defaults;
    m_keyBindings.SetDefaults(0);
}

void C_PlayerProfile::SetKeyLayout(int layout)          // 0x006b3520
{
    m_keyBindings.SetDefaults(layout);
}

void C_PlayerProfile::ApplyGamma()                      // 0x006b3530
{
    // A linear ramp whose step grows with the gamma (255 at 50), at least 255 after the
    // first entry and at most 0xFFFF.
    const int step = static_cast<int>((m_options.gamma - GAMMA_LINEAR) * 2.0 + GAMMA_BASE_STEP);
    uint16_t ramp[3][256];
    uint32_t value = 0;
    for (int i = 0; i < 256; ++i) {
        ramp[0][i] = ramp[1][i] = ramp[2][i] = static_cast<uint16_t>(value);
        const uint32_t next = (value & 0xFFFF) + (static_cast<uint32_t>(step) & 0xFFFF);
        if (next < 0xFF)
            value = 0xFF;
        else if (next < 0xFFFF)
            value += static_cast<uint32_t>(step);
        else
            value = 0xFFFF;
    }
#ifndef HD2_DEDICATED   // no display in LS3DRF.dll
    GetIGraph()->SetGammaRamp(&ramp[0][0], 0);
#endif
}

void C_PlayerProfile::ApplyVolume()                     // 0x006b35c0
{
    GetSoundModule()->SetMasterVolume(m_options.soundVolume * VOLUME_SCALE);
}

void C_PlayerProfile::ApplyMusicVolume()                // 0x006b35f0
{
    GetSoundModule()->SetMusicVolume(m_options.musicVolume * VOLUME_SCALE);
}

void C_PlayerProfile::ApplyMouse()                      // 0x006b3610
{
    const int scale = static_cast<int>(m_options.mouseSensitivity * MOUSE_SCALE_PER_POINT + MOUSE_SCALE_BASE);
    g_controls.SetMouseScale(scale, scale, MOUSE_SCALE_Z);
    g_controls.SetInvertMouse(m_options.invertMouse);
}

void C_PlayerProfile::ApplyVoiceValue()                 // 0x006b3640
{
    g_netGame.SetVoiceValue(static_cast<int>(m_options.voiceValue * VOICE_SCALE));
}

void C_PlayerProfile::ApplyHardwareOption()             // 0x006b3660
{
    if (I3D_driver *driver = I3DGetDriver())
        driver->SetState(DRIVER_STATE_2B, m_options.IsEnabled(DETAIL_HARDWARE));
}

// --- progress ---------------------------------------------------------------------------

void C_PlayerProfile::SetDifficulty(int difficulty)    // 0x006b1730
{
    if (difficulty >= 0 && difficulty <= MAX_DIFFICULTY)
        m_difficulty = difficulty;
}

bool C_PlayerProfile::SetProgress(int campaign, int mission)   // 0x006b15f0
{
    if (campaign < 0 || campaign >= 2 || g_playMode->singleMission != 0)
        return false;
    const int mode = g_playMode->mode;
    if (mode != PLAY_MODE_CAMPAIGN && mode != PLAY_MODE_LONE_WOLF && mode != PLAY_MODE_CARNAGE)
        return false;
    m_progress[campaign][mode] = mission;
    if (m_progress[campaign][0] < mission)
        m_progress[campaign][0] = mission;
    return true;
}

bool C_PlayerProfile::HasProgress(int campaign) const  // 0x006b1660
{
    if (campaign < 0 || campaign >= 2 || g_playMode->singleMission != 0)
        return false;
    const int mode = g_playMode->mode;
    if (mode != PLAY_MODE_CAMPAIGN && mode != PLAY_MODE_LONE_WOLF && mode != PLAY_MODE_CARNAGE)
        return false;
    return m_progress[campaign][mode] >= 0;
}

int C_PlayerProfile::GetBestProgress(int campaign) const   // 0x006b16c0
{
    if (campaign < 0 || campaign >= 2)
        return -1;
    return m_progress[campaign][0];
}

int C_PlayerProfile::GetProgress(int campaign) const   // 0x006b16e0
{
    if (campaign < 0 || campaign >= 2 || g_playMode->singleMission != 0)
        return -1;
    const int mode = g_playMode->mode;
    if (mode != PLAY_MODE_CAMPAIGN && mode != PLAY_MODE_LONE_WOLF && mode != PLAY_MODE_CARNAGE)
        return -1;
    return m_progress[campaign][mode];
}

// --- diary, movies ------------------------------------------------------------------------

C_Diary *C_PlayerProfile::GetDiary()                    // 0x006ae7c0
{
    return g_campaign->GetCurrentIndex() == 1 ? &m_diaries[1] : &m_diaries[0];
}

void C_PlayerProfile::ClearCurrentDiary()               // 0x006b4950
{
    GetDiary()->ClearCurrent();
}

void C_PlayerProfile::SetMovieSeen(const char *file, bool seen)   // 0x006b15b0
{
    if (!m_name.IsEmpty() && m_movies.SetSeen(file, seen))
        Save();
}

// --- saved games --------------------------------------------------------------------------

const C_str *C_PlayerProfile::GetLastSave()             // 0x006b3ec0
{
    if (const char *slash = std::strrchr(m_lastSave.c_str(), '\\'))
        m_lastSave = C_str(slash + 1);
    return &m_lastSave;
}

void C_PlayerProfile::SetLastSave(const C_str &name)    // 0x006b3fb0
{
    const char *slash = std::strrchr(name.c_str(), '\\');
    m_lastSave = slash ? C_str(slash + 1) : name;
    UpdateLastSaveText(m_lastSave);
    Save();
}

bool C_PlayerProfile::GetLastSaveText(C_str *mode, C_str *mission, C_str *date) const   // 0x006b3de0
{
    if (m_lastSaveText.IsEmpty())
        return false;
    // Fix: the text was copied into a 1 KB buffer without a size check.
    char text[1024];
    std::strncpy(text, m_lastSaveText.c_str(), sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    char *first = std::strchr(text, '$');
    if (!first)
        return false;
    *first = '\0';
    *mode = text;
    char *second = std::strchr(first + 1, '$');
    if (!second)
        return false;
    *second = '\0';
    *mission = first + 1;
    *date = second + 1;
    return true;
}

void C_PlayerProfile::UpdateLastSaveText(const C_str &file)   // 0x006b4020
{
    C_MissionDef *mission = g_campaign->GetCurrentMission();
    if (!mission)
        return;
    m_lastSaveText = "";
    C_str format;
    int number = -1;
    if (!std::strstr(file.c_str(), "quick")) {
        format = GetText(TEXT_LAST_SAVE);
    } else {
        // A quick save also fills the play mode's slot with the mission's number.
        format = GetText(TEXT_LAST_QUICK_SAVE);
        if (g_campaign->GetCurrentIndex() == 1)
            number = g_campaign->CountMissions(0) + g_campaign->GetMissionNumber(1);
        else
            number = g_campaign->GetMissionNumber(g_campaign->GetCurrentIndex());
    }
    C_str modeText;
    int slot = -1;
    const S_PlayMode &playMode = *g_playMode;
    if (playMode.singleMission == 0) {
        switch (playMode.mode) {
        case PLAY_MODE_CAMPAIGN:
            modeText = GetText(TEXT_MODE_CAMPAIGN);
            slot = SAVE_SLOT_CAMPAIGN;
            break;
        case PLAY_MODE_LONE_WOLF:
            modeText = GetText(TEXT_MODE_LONE_WOLF);
            slot = SAVE_SLOT_LONE_WOLF;
            break;
        case PLAY_MODE_CARNAGE:
            modeText = GetText(TEXT_MODE_CARNAGE);
            slot = SAVE_SLOT_CARNAGE;
            break;
        default:
            return;
        }
    } else if (playMode.singleMission == 1) {
        if (playMode.mode == 0) {
            modeText = GetText(TEXT_MODE_SINGLE_MISSION);
            slot = SAVE_SLOT_SINGLE_MISSION;
        } else if (playMode.mode == PLAY_MODE_CARNAGE) {
            modeText = GetText(TEXT_MODE_SINGLE_CARNAGE);
            slot = SAVE_SLOT_SINGLE_CARNAGE;
        } else {
            return;
        }
    }
    if (slot >= 0 && number != -1) {
        m_saveSlots[slot].mission = number;
        m_saveSlots[slot].file = file;
    }
    const C_str name = GetText(mission->GetId());
    const char *date = nullptr;
    GetDateString(&date, false);
    if (format.IsEmpty() || modeText.IsEmpty() || name.IsEmpty() || !date)
        return;
    m_lastSaveText = C_fstr(format.c_str(), modeText.c_str(), name.c_str(), date);
}

bool C_PlayerProfile::SaveGame(C_chunk &chunk)          // 0x006b4570
{
    chunk.Begin(CHUNK_SAVED_PLAYER);
    chunk.Begin(CHUNK_SAVED_DIARY);
    GetDiary()->SaveGame(chunk);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_PlayerProfile::LoadGame(C_chunk &chunk)          // 0x006b4720
{
    uint16_t id = 0;
    if (!chunk.Descend(id) || id != CHUNK_SAVED_PLAYER)
        return false;
    bool ok = true;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        if (id == CHUNK_SAVED_DIARY)
            ok &= GetDiary()->LoadGame(chunk);
        if (!chunk.IsOpen())
            return ok;
        chunk.Ascend();
    }
    chunk.Ascend();
    return ok;
}
