"""Part 8f-ii mutations: the mission's loading (game/mission_loader.cpp).

Run with the camera build: --build build_camera.bat --check check_camera.py (CAMERA_TEST; the
checks in mission_test.inc). The deletions are named by the line before the one they delete.

Equivalent (left alive, by design):
  M 51 num   the object chunk's id: Descend sets it whenever it is compared.
  M 55 del   the Ascend right before the read-only file's Close.
  M 90 del   FinishLoading: Begin always starts the screen in two parts (Finish closes it).
"""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
M = os.path.join(ROOT, r"src/game/mission_loader.cpp")

MUTATIONS = [
    (M, 'M 23 cmp', '    if (message != 0x10 && message != 0x20)\n', '    if (message == 0x10 && message != 0x20)\n'),
    (M, 'M 27 num', 'constexpr uint32_t SCENE_OPEN_FLAGS = 7;\n', 'constexpr uint32_t SCENE_OPEN_FLAGS = 8;\n'),
    (M, 'M 38 bool', '            ModuleManager::BroadcastLoadMissionData(true);\n', '            ModuleManager::BroadcastLoadMissionData(false);\n'),
    (M, 'M 40 bool', '            ModuleManager::BroadcastLoadMissionData(false);\n', '            ModuleManager::BroadcastLoadMissionData(true);\n'),
    (M, 'M 48 bool', '    if (!chunk.Open(path, false))\n', '    if (!chunk.Open(path, true))\n'),
    (M, 'M 51 num', '    uint16_t id = 0;\n', '    uint16_t id = 1;\n'),
    (M, 'M 53 cmp', '        if (id == MISSION_CHUNK_OBJECTS)\n', '        if (id != MISSION_CHUNK_OBJECTS)\n'),
    (M, 'M 55 del', '        chunk.Ascend();\n', '\n'),
    (M, 'M 64 del', '    m_unknown66 = 0;\n', '\n'),
    (M, 'M 64 num', '    m_unknown66 = 0;\n', '    m_unknown66 = 1;\n'),
    (M, 'M 65 del', '    m_unknown68 = 0;\n', '\n'),
    (M, 'M 65 num', '    m_unknown68 = 0;\n', '    m_unknown68 = 1;\n'),
    (M, 'M 66 del', '    m_unknownD8 = 0;\n', '\n'),
    (M, 'M 66 num', '    m_unknownD8 = 0;\n', '    m_unknownD8 = 1;\n'),
    (M, 'M 69 del', '    loader.Begin(name, mission);\n', '\n'),
    (M, 'M 71 num', '    if (!EnsureHumanAnimTablesLoaded())\n        return 1;\n', '    if (!EnsureHumanAnimTablesLoaded())\n        return 2;\n'),
    (M, 'M 72 del', '    loader.LoadScene();\n', '\n'),
    (M, 'M 73 del', '    m_scene->UnknownF0();\n', '\n'),
    (M, 'M 76 del', '    loader.LoadActors();\n', '\n'),
    (M, 'M 77 del', '    loader.LoadSounds();\n', '\n'),
    (M, 'M 78 del', '    loader.LoadCheckpoints();\n', '\n'),
    (M, 'M 79 del', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.3f);\n', '\n'),
    (M, 'M 79 num', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.3f);\n', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.45f);\n'),
    (M, 'M 81 num', '    if (failed)\n        return 1;\n', '    if (failed)\n        return 2;\n'),
    (M, 'M 84 del', '    g_collisionWorld.Load(path, true);\n', '\n'),
    (M, 'M 84 bool', '    g_collisionWorld.Load(path, true);\n', '    g_collisionWorld.Load(path, false);\n'),
    (M, 'M 85 del', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.8f);\n', '\n'),
    (M, 'M 85 num', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 0.8f);\n', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 1.2f);\n'),
    (M, 'M 86 del', '    g_particlesModule.SetScene(m_scene);\n', '\n'),
    (M, 'M 87 del', '    g_particlesModule.LoadStaticEmitters();\n', '\n'),
    (M, 'M 88 del', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 1.0f);\n', '\n'),
    (M, 'M 88 num', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 1.0f);\n', '    g_loaderScreen.SetProgress(LOADER_STAGE_STUFF, 1.5f);\n'),
    (M, 'M 89 del', '    m_missionLoaded = true;\n', '\n'),
    (M, 'M 89 bool', '    m_missionLoaded = true;\n', '    m_missionLoaded = false;\n'),
    (M, 'M 90 del', '    g_loaderScreen.FinishLoading();\n', '\n'),
    (M, 'M 91 num', '    return 0;\n', '    return 1;\n'),
    (M, 'M 96 cmp', '    g_loaderScreen.Start(name, (g_gameRole & GAME_ROLE_CLIENT) != 0, (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL,\n', '    g_loaderScreen.Start(name, (g_gameRole & GAME_ROLE_CLIENT) != 0, (g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL,\n'),
    (M, 'M 96 num', '    g_loaderScreen.Start(name, (g_gameRole & GAME_ROLE_CLIENT) != 0, (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL,\n', '    g_loaderScreen.Start(name, (g_gameRole & GAME_ROLE_CLIENT) != 1, (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL,\n'),
    (M, 'M 97 bool', '                         true, mission);\n', '                         false, mission);\n'),
    (M, 'M 98 del', '    GetGame()->FreeMission();\n', '\n'),
    (M, 'M 100 del', '    GetGame()->m_missionName = C_fstr("%s", name);\n', '\n'),
    (M, 'M 101 del', '    g_carScene = GetGame()->GetScene();\n', '\n'),
    (M, 'M 102 del', '    m_name = C_fstr("%s", name);\n', '\n'),
    (M, 'M 103 del', '    g_modelCache.Clear();\n', '\n'),
    (M, 'M 104 del', '    g_shadowCache.Clear();\n', '\n'),
    (M, 'M 105 del', '    g_versionModelCache.Clear();\n', '\n'),
    (M, 'M 116 del', '    LoadObjectFile(*this, C_fstr("missions\\\\%s\\\\actors.bin", m_name.c_str()), LOADER_STAGE_ACTORS);\n', '\n'),
    (M, 'M 121 del', '    LoadObjectFile(*this, C_fstr("missions\\\\%s\\\\sounds.bin", m_name.c_str()), LOADER_STAGE_SOUNDS);\n', '\n'),
    (M, 'M 128 bool', '    if (chunk.Open(C_fstr("missions\\\\%s\\\\check2.bin", m_name.c_str()), false)) {\n', '    if (chunk.Open(C_fstr("missions\\\\%s\\\\check2.bin", m_name.c_str()), true)) {\n'),
    (M, 'M 129 del', '        LoadCheckpointNetwork(&chunk, checkpoints);\n', '\n'),
    (M, 'M 130 del', '        LoadCheckpointNetwork(&chunk, GetGame()->GetCheckpoints()->GetRoadNetwork());\n', '\n'),
    (M, 'M 132 del', '        checkpoints->Clear();\n', '\n'),
    (M, 'M 134 bool', '        if (!chunk.Open(C_fstr("missions\\\\%s\\\\check.bin", m_name.c_str()), false))\n', '        if (!chunk.Open(C_fstr("missions\\\\%s\\\\check.bin", m_name.c_str()), true))\n'),
]
