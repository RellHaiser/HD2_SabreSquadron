#include "game/mission_loader.h"

#include <cstdio>

#include "common/chunk.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human_anim_tables.h"
#include "game/loader_screen.h"
#include "game/module.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "ls3d/i3d.h"

namespace {

// The scene's loading progress (an I3D load callback): the loading screen's first stage (not
// for messages 0x10 and 0x20).
void __stdcall SceneLoadProgress(int message, float fraction, uint32_t, void *)   // 0x006d4fc0
{
    if (message != 0x10 && message != 0x20)
        g_loaderScreen.SetProgress(LOADER_STAGE_SCENE, fraction);
}
// The scene file's open flags.
constexpr uint32_t SCENE_OPEN_FLAGS = 7;

// The modules load their mission data when C_game::LoadMission returns, however it ends: the
// server's side, then the client's.
struct S_ModuleMissionData {
    S_ModuleMissionData() = default;
    S_ModuleMissionData(const S_ModuleMissionData &) = delete;
    S_ModuleMissionData &operator=(const S_ModuleMissionData &) = delete;
    ~S_ModuleMissionData()                              // 0x006cb130
    {
        if (g_gameRole & GAME_ROLE_SERVER)
            ModuleManager::BroadcastLoadMissionData(true);
        if (g_gameRole & GAME_ROLE_CLIENT)
            ModuleManager::BroadcastLoadMissionData(false);
    }
};

// actors.bin / sounds.bin: the object chunk if the file starts with one.
void LoadObjectFile(C_MissionLoader &loader, const char *path, int stage)
{
    C_chunk chunk;
    if (!chunk.Open(path, false))
        return;
    // (The original compares an uninitialized id when the file is empty, and ascends then too.)
    uint16_t id = 0;
    if (chunk.Descend(id)) {
        if (id == MISSION_CHUNK_OBJECTS)
            loader.LoadObjects(chunk, stage);
        chunk.Ascend();
    }
    chunk.Close();
}

} // namespace

int C_game::LoadMission(const char *name, C_MissionDef *mission)   // 0x006caad0
{
    m_unknown66 = 0;
    m_unknown68 = 0;
    m_unknownD8 = 0;
    const S_ModuleMissionData moduleData;
    C_MissionLoader loader;
    loader.Begin(name, mission);
    if (!EnsureHumanAnimTablesLoaded())
        return 1;
    loader.LoadScene();
    m_scene->UnknownF0();
    const int failed = loader.LoadScene2();
    // (Dropped: the copy protection's stamps, here and after the loading.)
    loader.LoadActors();
    loader.LoadSounds();
    loader.LoadCheckpoints();
    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.3f);
    if (failed)
        return 1;
    char path[256];
    std::sprintf(path, "missions\\%s\\tree.klz", name);
    g_collisionWorld.Load(path, true);
    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.8f);
    g_particlesModule.SetScene(m_scene);
    g_particlesModule.LoadStaticEmitters();
    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 1.0f);
    m_missionLoaded = true;
    g_loaderScreen.FinishLoading();
    return 0;
}

void C_MissionLoader::Begin(const char *name, const C_MissionDef *mission)
{
    g_loaderScreen.Start(name, (g_gameRole & GAME_ROLE_CLIENT) != 0, (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL,
                         true, mission);
    GetGame()->FreeMission();
    // Fix: the name as text (the original used it as the format, here and for the loader's).
    GetGame()->m_missionName = C_fstr("%s", name);
    g_carScene = GetGame()->GetScene();
    m_name = C_fstr("%s", name);
    g_modelCache.Clear();
    g_shadowCache.Clear();
    g_versionModelCache.Clear();
}

HRESULT C_MissionLoader::LoadScene()
{
    const C_fstr path("missions\\%s\\Scene", m_name.c_str());
    return GetGame()->GetScene()->Open(path, SCENE_OPEN_FLAGS, reinterpret_cast<void *>(&SceneLoadProgress), nullptr);
}

void C_MissionLoader::LoadActors()
{
    LoadObjectFile(*this, C_fstr("missions\\%s\\actors.bin", m_name.c_str()), LOADER_STAGE_ACTORS);
}

void C_MissionLoader::LoadSounds()
{
    LoadObjectFile(*this, C_fstr("missions\\%s\\sounds.bin", m_name.c_str()), LOADER_STAGE_SOUNDS);
}

void C_MissionLoader::LoadCheckpoints()
{
    C_Checkpoints *checkpoints = GetGame()->GetCheckpoints();
    C_chunk chunk;
    if (chunk.Open(C_fstr("missions\\%s\\check2.bin", m_name.c_str()), false)) {
        LoadCheckpointNetwork(&chunk, checkpoints);
        LoadCheckpointNetwork(&chunk, GetGame()->GetCheckpoints()->GetRoadNetwork());
    } else {
        checkpoints->Clear();
        // (Quirk kept: the old format's check.bin is opened, not read.)
        if (!chunk.Open(C_fstr("missions\\%s\\check.bin", m_name.c_str()), false))
            GetGame()->GetCheckpoints()->Clear();
    }
    chunk.Close();
}
