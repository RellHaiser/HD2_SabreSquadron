// Part 8f-ii's mission loading (game/mission_loader.cpp: C_game::LoadMission, the loader's
// Begin, Scene, actors.bin, sounds.bin, check2.bin), in the camera build, run by loader_test.inc's
// LoaderTest inside its fakes (the loading screen is the real one): scene2.bin's reader (part
// 8f-iii), the caches' Clear, C_game::FreeMission, the human animation tables, the checkpoint
// networks, the collision world's Load, the modules' LoadMissionData and the particles' static
// emitters faked (logged); the files in g_files. Prints "mission:" with 1 per check met.

#include "game/mission_loader.h"

C_ModelCache g_versionModelCache;
template <> void C_ResourceCache<I3D_model>::Clear()
{
    GfLog(this == &g_modelCache ? "clear models" : this == &g_versionModelCache ? "clear versions" : "clear ?");
}
template <> void C_ResourceCache<I3D_shadow>::Clear()
{
    GfLog(this == &g_shadowCache ? "clear shadows" : "clear ?");
}
void C_game::FreeMission() { GfLog("freemission"); }
bool g_msAnimTables = true;
bool EnsureHumanAnimTablesLoaded()
{
    GfLog("animtables");
    return g_msAnimTables;
}
alignas(16) uint8_t g_msCheckpointsMem[sizeof(C_Checkpoints)];
C_Checkpoints *MsCheckpoints() { return reinterpret_cast<C_Checkpoints *>(g_msCheckpointsMem); }
int __stdcall LoadCheckpointNetwork(C_chunk *chunk, C_CheckpointNetwork *network)
{
    uint8_t b = 0;
    chunk->Read(&b, 1);
    GfLog("cpnet %s %d", network == MsCheckpoints() ? "walk" : network == MsCheckpoints()->GetRoadNetwork() ? "roads" : "?",
          b);
    return 1;
}
void C_Checkpoints::Clear() { GfLog(this == MsCheckpoints() ? "cpclear" : "cpclear ?"); }
void C_CollisionWorld::Load(const char *fileName, bool buildGrid)
{
    GfLog("collision %s %d %d", fileName, buildGrid, this == &g_collisionWorld);
}
namespace ModuleManager {
void BroadcastLoadMissionData(bool server) { GfLog("loaddata %d", server); }
}  // namespace ModuleManager
void C_ParticlesModule::LoadStaticEmitters()
{
    GfLog("emitters %d %d", this == &g_particlesModule, m_scene == GetGame()->GetScene());
}
int g_msScene2Result;
int C_MissionLoader::LoadScene2()
{
    GfLog("scene2");
    return g_msScene2Result;
}
void C_MissionLoader::LoadObjects(C_chunk &chunk, int stage)
{
    uint8_t b = 0;
    chunk.Read(&b, 1);
    GfLog("objects %d %d", stage, b);
}

namespace {

struct S_MissionResults {
    bool msBegin = true, msScene = true, msObjects = true, msCheckpoints = true, msLoad = true, msFail = true;
    bool msModules = true;
};

// The scene file's progress callback, as the scene's Open got it.
using T_MsProgress = void(__stdcall *)(int message, float fraction, uint32_t, void *);
T_MsProgress g_msProgress;
HRESULT __stdcall F_MsOpen(void *self, const char *name, uint32_t flags, void *callback, void *context)
{
    if (self == GetGame()->GetScene())
        g_msProgress = reinterpret_cast<T_MsProgress>(callback);
    return F_LsOpen(self, name, flags, callback, context);
}
void __stdcall F_MsF0(void *self) { GfLog(self == GetGame()->GetScene() ? "f0" : "f0 ?"); }

// A chunk file: `id`, its size, then `body`.
std::vector<uint8_t> MsChunkFile(uint16_t id, const std::vector<uint8_t> &body)
{
    std::vector<uint8_t> f(6);
    std::memcpy(f.data(), &id, 2);
    const uint32_t size = static_cast<uint32_t>(6 + body.size());
    std::memcpy(f.data() + 2, &size, 4);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

void MissionLoaderTest()
{
    S_MissionResults r;
    C_game *game = GetGame();
    C_Checkpoints *const oldCheckpoints = game->m_checkpoints;
    game->m_checkpoints = MsCheckpoints();
    void *const oldOpen = g_sceneVtable[0xF4 / 4], *const oldF0 = g_sceneVtable[0xF0 / 4];
    g_sceneVtable[0xF4 / 4] = reinterpret_cast<void *>(&F_MsOpen);
    g_sceneVtable[0xF0 / 4] = reinterpret_cast<void *>(&F_MsF0);
    const uint32_t oldRole = g_gameRole;
    I3D_scene *const oldCarScene = g_carScene;
    const bool oldLoaded = game->m_missionLoaded;
    const auto files = [](const char *m, bool actors, bool sounds, int check) {
        for (const char *f : {"actors.bin", "sounds.bin", "check2.bin", "check.bin"})
            g_files.erase(CmText("missions\\%s\\%s", m, f));
        if (actors)
            g_files[CmText("missions\\%s\\actors.bin", m)] = MsChunkFile(MISSION_CHUNK_OBJECTS, {0x21, 0x22});
        if (sounds)
            g_files[CmText("missions\\%s\\sounds.bin", m)] = MsChunkFile(MISSION_CHUNK_OBJECTS, {0x31});
        if (check == 2)
            g_files[CmText("missions\\%s\\check2.bin", m)] = {0x41, 0x42};
        if (check == 1)
            g_files[CmText("missions\\%s\\check.bin", m)] = {0x51};
    };

    // --- a server's whole loading: the files, the screen's progress, the modules' data ---
    {
        g_gameRole = GAME_ROLE_SERVER;
        files("m7", true, true, 2);
        g_msAnimTables = true;
        g_msScene2Result = 0;
        game->m_missionLoaded = false;
        game->m_unknown66 = game->m_unknown68 = 5;
        game->m_unknownD8 = 6;
        g_carScene = nullptr;
        g_gfLog.clear();
        const int result = game->LoadMission("m7", nullptr);
        const std::vector<std::string> want = {
            "freemission", "clear models", "clear shadows", "clear versions", "animtables",
            "open missions\\m7\\Scene 7 1 0", "f0", "scene2", "objects 2 33", "objects 3 49", "cpnet walk 65",
            "cpnet roads 66", "winmsg", "collision missions\\m7\\tree.klz 1 1", "winmsg", "emitters 1 1", "winmsg",
            "loaddata 1"};
        WM(r.msLoad, result == 0 && GfVecIs(g_gfLog, want) && game->m_missionLoaded && game->m_unknown66 == 0 &&
                         game->m_unknown68 == 0 && game->m_unknownD8 == 0);
        WM(r.msBegin, game->m_missionName == C_str("m7") && g_carScene == game->GetScene() && g_loaderScreen.m_shown &&
                          !g_loaderScreen.m_scene && g_loaderScreen.m_twoParts && !g_loaderScreen.m_waitForKey);
        // The stuff stage's three steps (two-part table: 0.5 + 0.05 x).
        WM(r.msLoad, g_loaderScreen.m_progress == 0.05f * 1.0f + 0.5f);
        // The scene's progress: the first stage (not messages 0x10, 0x20).
        g_loaderScreen.m_progress = 0.0f;
        g_msProgress(0x10, 0.5f, 0, nullptr);
        g_msProgress(0x20, 0.5f, 0, nullptr);
        WM(r.msScene, g_loaderScreen.m_progress == 0.0f);
        g_msProgress(0x11, 0.5f, 0, nullptr);
        WM(r.msScene, g_loaderScreen.m_progress == 0.15f * 0.5f + 0.0f);
        g_msProgress(0, 1.0f, 0, nullptr);
        WM(r.msScene, g_loaderScreen.m_progress == 0.15f * 1.0f + 0.0f);
        g_loaderScreen.Finish();
        // The name as text.
        files("m%d", false, false, 0);
        g_gfLog.clear();
        game->LoadMission("m%d", nullptr);
        WM(r.msBegin, game->m_missionName == C_str("m%d") && g_gfLog.size() > 5 && g_gfLog[5] == "open missions\\m%d\\Scene 7 1 0");
        g_loaderScreen.Finish();
    }

    // --- the object files: their object chunk only ---
    {
        files("m8", false, false, 2);
        g_files["missions\\m8\\actors.bin"] = MsChunkFile(0x4C54, {0x21});
        g_files["missions\\m8\\sounds.bin"] = {0x53};   // (too short for a chunk)
        g_gfLog.clear();
        game->LoadMission("m8", nullptr);
        bool objects = false;
        for (const std::string &l : g_gfLog)
            objects = objects || l.compare(0, 7, "objects") == 0;
        WM(r.msObjects, !objects);
        g_loaderScreen.Finish();
        // (After the chunk: the file's next chunk not read.)
        std::vector<uint8_t> two = MsChunkFile(MISSION_CHUNK_OBJECTS, {0x61});
        const std::vector<uint8_t> second = MsChunkFile(MISSION_CHUNK_OBJECTS, {0x62});
        two.insert(two.end(), second.begin(), second.end());
        g_files["missions\\m8\\actors.bin"] = two;
        g_gfLog.clear();
        game->LoadMission("m8", nullptr);
        int count = 0;
        for (const std::string &l : g_gfLog)
            count += l == "objects 2 97";
        WM(r.msObjects, count == 1 && std::find(g_gfLog.begin(), g_gfLog.end(), "objects 2 98") == g_gfLog.end());
        g_loaderScreen.Finish();
    }

    // --- the checkpoints: check2.bin, else cleared (check.bin only opened) ---
    {
        for (int check = 0; check < 3; ++check) {
            files("m9", false, false, check);
            g_gfLog.clear();
            game->LoadMission("m9", nullptr);
            std::vector<std::string> cp;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 2, "cp") == 0)
                    cp.push_back(l);
            const std::vector<std::string> want = check == 2   ? std::vector<std::string>{"cpnet walk 65", "cpnet roads 66"}
                                                  : check == 1 ? std::vector<std::string>{"cpclear"}
                                                               : std::vector<std::string>{"cpclear", "cpclear"};
            WM(r.msCheckpoints, GfVecIs(cp, want));
            g_loaderScreen.Finish();
        }
    }

    // --- failures: the animation tables (nothing more), scene2.bin (the rest of its stage first) ---
    {
        files("m10", true, true, 2);
        g_msAnimTables = false;
        game->m_missionLoaded = false;
        g_gfLog.clear();
        int result = game->LoadMission("m10", nullptr);
        WM(r.msFail, result == 1 && GfLogIs({"freemission", "clear models", "clear shadows", "clear versions", "animtables",
                                             "loaddata 1"}) &&
                         !game->m_missionLoaded);
        g_loaderScreen.Finish();
        g_msAnimTables = true;
        g_msScene2Result = 3;
        g_gfLog.clear();
        result = game->LoadMission("m10", nullptr);
        WM(r.msFail, result == 1 && g_gfLog.size() == 14 && g_gfLog[12] == "winmsg" && g_gfLog[13] == "loaddata 1" &&
                         g_gfLog[11] == "cpnet roads 66" && !game->m_missionLoaded &&
                         g_loaderScreen.m_progress == 0.05f * 0.3f + 0.5f);
        g_loaderScreen.Finish();
        g_msScene2Result = 0;
    }

    // --- the modules' data by the role: a client's, a single player's both (the loading screen
    // shown: its scene not opened here) ---
    {
        files("m11", false, false, 0);
        g_lsOpenResult = -5;
        const uint32_t roles[] = {GAME_ROLE_CLIENT, GAME_ROLE_LOCAL, 0};
        for (uint32_t role : roles) {
            g_gameRole = role;
            LsVisuals({});
            LsQueueFrames();
            g_gfLog.clear();
            game->LoadMission("m11", nullptr);
            std::vector<std::string> data;
            for (const std::string &l : g_gfLog)
                if (l.compare(0, 8, "loaddata") == 0)
                    data.push_back(l);
            const std::vector<std::string> want = role == GAME_ROLE_CLIENT  ? std::vector<std::string>{"loaddata 0"}
                                                  : role == GAME_ROLE_LOCAL ? std::vector<std::string>{"loaddata 1", "loaddata 0"}
                                                                            : std::vector<std::string>{};
            // (The screen: shown on a client, waiting in single player, in two parts.)
            WM(r.msModules, GfVecIs(data, want) && g_gfLog.size() > 1 && (g_gfLog[0] == "create 13") == (role != 0) &&
                                g_loaderScreen.m_waitForKey == (role == GAME_ROLE_LOCAL) && g_loaderScreen.m_twoParts);
            g_loaderScreen.Finish();
        }
        g_lsOpenResult = 0;
        // A mission's definition reaches the screen (its briefing).
        g_gameRole = GAME_ROLE_CLIENT;
        LsVisuals({"briefing"});
        LsQueueFrames();
        g_coop = false;
        C_MpMissionDef *mission = LsMission(0);
        g_gfLog.clear();
        game->LoadMission("m11", mission);
        WM(r.msBegin, std::find(g_gfLog.begin(), g_gfLog.end(), "tb briefing addid 11 84") != g_gfLog.end());
        g_loaderScreen.Finish();
    }

    g_gameRole = oldRole;
    g_carScene = oldCarScene;
    game->m_missionLoaded = oldLoaded;
    game->m_checkpoints = oldCheckpoints;
    g_sceneVtable[0xF4 / 4] = oldOpen;
    g_sceneVtable[0xF0 / 4] = oldF0;
    std::printf("mission: msBegin %d msScene %d msObjects %d msCheckpoints %d msLoad %d msFail %d msModules %d\n",
                r.msBegin, r.msScene, r.msObjects, r.msCheckpoints, r.msLoad, r.msFail, r.msModules);
}

}  // namespace
