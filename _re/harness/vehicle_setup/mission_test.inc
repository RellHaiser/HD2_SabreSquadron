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
// Part 8f-iv's object reader: logged, the chunk's first byte read (0xFF: it fails).
bool C_MissionLoader::LoadObject(C_chunk &chunk)
{
    uint8_t b = 0;
    chunk.Read(&b, 1);
    GfLog("object %d", b);
    return b != 0xFF;
}
bool LoadTyres()
{
    GfLog("tyres");
    return true;
}
bool InitCarSurfaces()
{
    GfLog("surfaces");
    return true;
}

// The actors scene2.bin makes (part 8f-iii): logged; SetFrame fails for type 9, CreateActor gives
// none for 99.
class C_MsActor : public C_Actor {
public:
    explicit C_MsActor(E_ActorType type) : C_Actor(type) {}
    void CopyFrom(const C_Actor *) override {}
    void StartMission(bool) override {}
    void StopMission(bool) override {}
    void RestartMission(bool) override {}
    bool SetFrame(I3D_frame *frame) override
    {
        GfLog("setframe %d %s", GetType(), frame ? GfName(frame) : "-");
        return GetType() != 9;
    }
    bool ReadProperties(C_chunk &chunk) override
    {
        uint8_t b = 0;
        chunk.Read(&b, 1);
        GfLog("props %d %d", GetType(), b);
        return true;
    }
};
C_Actor *C_ActorsModule::CreateActor(E_ActorType type, I3D_frame *frame, bool active, int slot)
{
    GfLog("createactor %d %s %d %d", type, frame ? GfName(frame) : "-", active, slot);
    return type == 99 ? nullptr : new C_MsActor(type);
}
void C_ActorsModule::DestroyActor(C_Actor *actor, bool immediately)
{
    GfLog("destroyactor %d %d", actor->GetType(), immediately);
    // (Not deleted: a use after this logs as the actor's own.)
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

// The scene's EnumFrames (its frames: g_msNamed), slots 0xE4 / 0x128, AddFrame; the driver's
// FindFrame (g_msDriverFrames).
std::vector<I3D_frame *> g_msNamed;
void __stdcall F_MsEnum(void *, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *)
{
    GfLog("enum %x", typeMask);
    for (I3D_frame *f : g_msNamed)
        if (callback(f, context) != 0)                  // (non-zero: stop)
            break;
}
void __stdcall F_MsE4(void *, float a, float b) { GfLog("e4 %.3f %.3f", a, b); }
void __stdcall F_Ms128(void *, I3D_frame *frame) { GfLog("s128 %s", GfName(frame)); }
void __stdcall F_MsAddFrame(void *, I3D_frame *frame) { GfLog("addframe %s", GfName(frame)); }
std::map<std::string, I3D_frame *> g_msDriverFrames;
I3D_frame *__stdcall F_MsDriverFind(void *, I3D_scene *, const char *name)
{
    const auto it = g_msDriverFrames.find(name);
    GfLog("dfind %s", name);
    return it == g_msDriverFrames.end() ? nullptr : it->second;
}

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

// A chunk: `id`, its size, `body` (MsChunkFile's, for nesting).
std::vector<uint8_t> MsChunk(uint16_t id, const std::vector<uint8_t> &body) { return MsChunkFile(id, body); }
std::vector<uint8_t> operator+(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
// An object file: its object chunk with one object (part 8f-iv's reader logs `b`).
std::vector<uint8_t> MsObjectsFile(uint8_t b)
{
    return MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0x4000, MsChunk(0x4010, {b})));
}
// The log without the loading screen's redraws.
std::vector<std::string> MsLog()
{
    std::vector<std::string> log;
    for (const std::string &l : g_gfLog)
        if (l != "winmsg")
            log.push_back(l);
    return log;
}
// scene2.bin's start with the scene's camera "mscam".
std::vector<std::string> MsScene2Prologue()
{
    return {"tyres", "surfaces", "enum 4ffff", "ortho mscam 0", "range mscam 0.1 500.0", "fov mscam 1.3963",
            "active mscam", "update mscam"};
}

void MsScene2Test();

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
    I3D_frame *const oldActive = CmActive();
    I3D_frame *oldSectors[3];
    for (int i = 0; i < 3; ++i) {
        oldSectors[i] = WmField<I3D_frame *>(game->GetScene(), 0x1F8 + 4 * i);
        WmField<I3D_frame *>(game->GetScene(), 0x1F8 + 4 * i) = GfFrame(FRAME_SECTOR, CmText("sector%d", i));
    }
    CmActive() = CmCamera("mscam", S_vector(0.0f, 0.0f, 0.0f));
    void *const oldSlots[] = {g_sceneVtable[0x44 / 4], g_sceneVtable[0xE4 / 4], g_sceneVtable[0x128 / 4],
                              g_sceneVtable[0x80 / 4], g_driverVtable[0x88 / 4]};
    g_sceneVtable[0x44 / 4] = reinterpret_cast<void *>(&F_MsEnum);
    g_sceneVtable[0xE4 / 4] = reinterpret_cast<void *>(&F_MsE4);
    g_sceneVtable[0x128 / 4] = reinterpret_cast<void *>(&F_Ms128);
    g_sceneVtable[0x80 / 4] = reinterpret_cast<void *>(&F_MsAddFrame);
    g_driverVtable[0x88 / 4] = reinterpret_cast<void *>(&F_MsDriverFind);
    const bool oldLoaded = game->m_missionLoaded;
    const auto files = [](const char *m, bool actors, bool sounds, int check) {
        for (const char *f : {"actors.bin", "sounds.bin", "check2.bin", "check.bin", "scene2.bin"})
            g_files.erase(CmText("missions\\%s\\%s", m, f));
        g_files[CmText("missions\\%s\\scene2.bin", m)] = MsChunkFile(0x1234, {});
        if (actors)
            g_files[CmText("missions\\%s\\actors.bin", m)] = MsObjectsFile(0x21);
        if (sounds)
            g_files[CmText("missions\\%s\\sounds.bin", m)] = MsObjectsFile(0x31);
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
        game->m_missionLoaded = false;
        game->m_unknown66 = game->m_unknown68 = 5;
        game->m_unknownD8 = 6;
        g_carScene = nullptr;
        g_gfLog.clear();
        const int result = game->LoadMission("m7", nullptr);
        std::vector<std::string> want = {"freemission", "clear models", "clear shadows", "clear versions", "animtables",
                                         "open missions\\m7\\Scene 7 1 0", "f0"};
        for (const std::string &l : MsScene2Prologue())
            want.push_back(l);
        for (const char *l : {"object 33", "object 49", "cpnet walk 65", "cpnet roads 66",
                              "collision missions\\m7\\tree.klz 1 1", "emitters 1 1", "loaddata 1"})
            want.push_back(l);
        WM(r.msLoad, result == 0 && GfVecIs(MsLog(), want) && game->m_missionLoaded && game->m_unknown66 == 0 &&
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
        g_files["missions\\m8\\actors.bin"] = MsChunkFile(0x4C54, MsChunk(0x4000, MsChunk(0x4010, {0x21})));
        g_files["missions\\m8\\sounds.bin"] = {0x53};   // (too short for a chunk)
        g_gfLog.clear();
        game->LoadMission("m8", nullptr);
        bool objects = false;
        for (const std::string &l : g_gfLog)
            objects = objects || l.compare(0, 6, "object") == 0;
        WM(r.msObjects, !objects);
        g_loaderScreen.Finish();
        // (After the chunk: the file's next chunk not read.)
        std::vector<uint8_t> two = MsObjectsFile(0x61);
        const std::vector<uint8_t> second = MsObjectsFile(0x62);
        two.insert(two.end(), second.begin(), second.end());
        g_files["missions\\m8\\actors.bin"] = two;
        g_gfLog.clear();
        game->LoadMission("m8", nullptr);
        int count = 0;
        for (const std::string &l : g_gfLog)
            count += l == "object 97";
        WM(r.msObjects, count == 1 && std::find(g_gfLog.begin(), g_gfLog.end(), "object 98") == g_gfLog.end());
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
        g_files["missions\\m10\\scene2.bin"] = {0x53};   // (its header does not read)
        g_gfLog.clear();
        result = game->LoadMission("m10", nullptr);
        const std::vector<std::string> log = MsLog();
        WM(r.msFail, result == 1 && log.size() > 3 && log.back() == "loaddata 1" && log[log.size() - 2] == "cpnet roads 66" &&
                         !game->m_missionLoaded && g_loaderScreen.m_progress == 0.05f * 0.3f + 0.5f &&
                         std::find(log.begin(), log.end(), "object 33") != log.end());
        g_loaderScreen.Finish();
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

    MsScene2Test();
    g_gameRole = oldRole;
    g_carScene = oldCarScene;
    CmActive() = oldActive;
    for (int i = 0; i < 3; ++i)
        WmField<I3D_frame *>(game->GetScene(), 0x1F8 + 4 * i) = oldSectors[i];
    g_sceneVtable[0x44 / 4] = oldSlots[0];
    g_sceneVtable[0xE4 / 4] = oldSlots[1];
    g_sceneVtable[0x128 / 4] = oldSlots[2];
    g_sceneVtable[0x80 / 4] = oldSlots[3];
    g_driverVtable[0x88 / 4] = oldSlots[4];
    game->m_missionLoaded = oldLoaded;
    game->m_checkpoints = oldCheckpoints;
    g_sceneVtable[0xF4 / 4] = oldOpen;
    g_sceneVtable[0xF0 / 4] = oldF0;
    std::printf("mission: msBegin %d msScene %d msObjects %d msCheckpoints %d msLoad %d msFail %d msModules %d\n",
                r.msBegin, r.msScene, r.msObjects, r.msCheckpoints, r.msLoad, r.msFail, r.msModules);
}

// --- part 8f-iii: scene2.bin's reader -----------------------------------------------------------

struct S_Scene2Results {
    bool s2File = true, s2Camera = true, s2Settings = true, s2List = true, s2TShift = true, s2Actors = true;
    bool s2States = true, s2Shadows = true, s2Groups = true, s2B820 = true, s2Music = true, s2Progress = true;
    bool s2Fail = true;
};

std::vector<uint8_t> MsBytes(const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    return std::vector<uint8_t>(p, p + size);
}
template <class T> std::vector<uint8_t> MsValue(const T &v) { return MsBytes(&v, sizeof v); }
std::vector<uint8_t> MsText(const char *text) { return MsBytes(text, std::strlen(text) + 1); }
// A shifting texture's entry: the two values, the name's length and the name.
std::vector<uint8_t> MsShift(int a, int b, const char *name)
{
    const int length = static_cast<int>(std::strlen(name));
    return MsChunk(0xAE01, MsValue(a) + MsValue(b) + MsValue(length) + MsBytes(name, length));
}
// An actor: its parts in order.
std::vector<uint8_t> MsActor(const std::vector<uint8_t> &parts) { return MsChunk(0xAE21, parts); }

void MsScene2Test()
{
    S_Scene2Results r;
    C_game *game = GetGame();
    g_gameRole = GAME_ROLE_SERVER;
    C_MissionLoader loader;
    loader.Begin("s2", nullptr);
    const char *const path = "missions\\s2\\scene2.bin";

    // --- the file: none (3), another chunk (0), a header that does not read (1) ---
    {
        g_msNamed.clear();
        g_files.erase(path);
        g_gfLog.clear();
        WM(r.s2File, loader.LoadScene2() == 3 && GfLogIs({"tyres", "surfaces"}));
        g_files[path] = MsChunkFile(0x1234, {});
        g_gfLog.clear();
        WM(r.s2File, loader.LoadScene2() == 0 && GfVecIs(MsLog(), MsScene2Prologue()));
        g_files[path] = {0x53, 0x4C};
        g_gfLog.clear();
        WM(r.s2File, loader.LoadScene2() == 1);
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, {});
        WM(r.s2File, loader.LoadScene2() == 0);
    }

    // --- the scene's camera made without one ---
    {
        I3D_frame *const old = CmActive();
        CmActive() = nullptr;
        g_cmCreate.clear();
        I3D_frame *made = CmCamera("newcam", S_vector(0.0f, 0.0f, 0.0f));
        g_cmCreate.push_back(made);
        g_files[path] = MsChunkFile(0x1234, {});
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> want = {
            "tyres", "surfaces", "enum 4ffff", "create 3", "active newcam", "addframe newcam",
            "worlddir newcam -1.00000 -0.10000 -1.00000 0.000000", "update newcam", "release newcam", "ortho newcam 0",
            "range newcam 0.1 500.0", "fov newcam 1.3963", "active newcam", "update newcam"};
        const S_vector &at = made->m_localMatrix(3);
        WM(r.s2Camera, GfVecIs(MsLog(), want) && at.x == 10.0f && at.y == 5.0f && at.z == 10.0f && CmActive() == made);
        CmActive() = old;
    }

    // --- the object chunk's entries ---
    {
        I3D_frame *lm = GfVisual("lm1", I3DVISUAL_LMAP);
        WmField<I3D_mesh *>(lm, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x1234);
        I3D_frame *obj = GfVisual("obj1", I3DVISUAL_OBJECT);
        WmField<I3D_mesh *>(obj, 0x1E0) = nullptr;
        I3D_frame *plain = GfFrame(FRAME_DUMMY, "plain");
        WmField<I3D_mesh *>(plain, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x5678);
        g_msNamed = {lm, obj, plain};
        g_plScene.clear();
        g_plScene["scenefound"] = GfFrame(FRAME_DUMMY, "scenefound");
        g_plScene["plain"] = plain;
        g_msDriverFrames.clear();
        g_msDriverFrames["driverfound"] = GfFrame(FRAME_DUMMY, "driverfound");
        g_lsStates[1] = 0;
        g_tshiftModule.m_entries.clear();
        game->m_unknown66 = game->m_unknown68 = 0;
        const uint32_t shadow[11] = {0x3FA00000, 0x40200000, 0x41F00000, 128, 256, 0x3DCCCCCD, 0x3E4CCCCD, 0x3E99999A, 7, 8, 9};
        const std::vector<uint8_t> body =
            MsChunk(1, {0x34, 0x12, 0x78, 0x56}) + MsChunk(0x3011, MsValue(300.0f)) + MsChunk(0x3010, {1, 2}) +
            MsChunk(0x3013, MsValue(7.0f)) + MsChunk(0x3012, {}) + MsChunk(0x3200, MsValue(1.0f) + MsValue(2.0f) + MsValue(3.0f)) +
            MsChunk(0x3211, MsValue(0.05f) + MsValue(7.0f)) + MsChunk(0x3211, MsValue(0.5f) + MsValue(2.0f)) +
            MsChunk(0x7777, {9, 9}) +
            MsChunk(0x4000, MsChunk(0x4010, {1}) + MsChunk(0x4011, {9}) + MsChunk(0x4010, {2})) +
            MsChunk(0xAE00, MsShift(5, 6, "lm1") + MsShift(7, 8, "obj1") + MsShift(1, 1, "nothere") + MsShift(2, 2, "plain") +
                                MsChunk(0xAE02, {})) +
            MsChunk(0xAE20, MsActor(MsChunk(0xAE23, MsText("plain")) + MsChunk(0xAE22, MsValue(7)) + MsChunk(0xAE24, {9})) +
                                MsActor(MsChunk(0xAE23, MsText("scenefound")) + MsChunk(0xAE22, MsValue(8))) +
                                MsActor(MsChunk(0xAE23, MsText("driverfound")) + MsChunk(0xAE22, MsValue(9))) +
                                MsActor(MsChunk(0xAE22, MsValue(99)) + MsChunk(0xAE24, {5})) + MsChunk(0xAE2F, {})) +
            MsChunk(0xAE40, {0x41, 0x42}) + MsChunk(0xB004, {1}) + MsChunk(0xB004, {0}) +
            MsChunk(0xB600, MsValue(0x1234)) + MsChunk(0xB650, MsValue(77)) + MsChunk(0xB700, MsBytes(shadow, sizeof shadow)) +
            MsChunk(0xB810, MsChunk(0x100, MsText("grp")) + MsChunk(0x110, MsText("f1"))) +
            MsChunk(0xB820, MsChunk(0xB820, MsText("plain")) + MsChunk(0xB820, MsText("nothere")) + MsChunk(0x1111, {}) +
                                MsChunk(0xB820, MsText("after"))) +
            MsChunk(0xB840, MsValue(3)) + MsChunk(0xB880, MsValue(4));
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, body);
        g_gfLog.clear();
        const int result = loader.LoadScene2();
        std::vector<std::string> want = MsScene2Prologue();
        for (const char *l : {"ortho mscam 1", "update mscam", "e4 0.100 7.000", "e4 0.500 2.000", "object 1", "object 2",
                              "createactor 7 plain 1 -1", "setframe 7 plain", "props 7 9", "sfind scenefound cffff",
                              "createactor 8 scenefound 1 -1", "setframe 8 scenefound", "dfind driverfound",
                              "createactor 9 driverfound 1 -1", "setframe 9 driverfound", "destroyactor 9 1",
                              "createactor 99 - 1 -1", "cpnet walk 65", "cpnet roads 66", "getstate 1", "setstate 1 1",
                              "getstate 1", "rstate 12 4660", "sfind plain ffff", "s128 plain"})
            want.push_back(l);
        const std::vector<std::string> log = MsLog();
        WM(r.s2List, result == 0 && GfVecIs(log, want));
        WM(r.s2Settings, game->m_unknown66 == 0x1234 && game->m_unknown68 == 0x5678 && game->m_fov == 300.0f &&
                             game->m_nearClip == 0.1f && game->m_farClip == 300.0f &&
                             WmField<float>(game->GetScene(), 0x210) == 1.0f && WmField<float>(game->GetScene(), 0x214) == 2.0f &&
                             WmField<float>(game->GetScene(), 0x218) == 3.0f);
        const std::vector<S_TShiftEntry> &shifts = g_tshiftModule.m_entries;
        WM(r.s2TShift, shifts.size() == 1 && shifts[0].mesh == reinterpret_cast<I3D_mesh *>(0x1234) && shifts[0].frame == lm &&
                           shifts[0].on && !shifts[0].reverse && shifts[0].unknown0C == 5 && shifts[0].unknown10 == 6 &&
                           shifts[0].unknown14 == 0 && shifts[0].unknown18 == 0);
        WM(r.s2States, game->m_unknownD8 == 77);
        const S_ShadowParams *params = GetGEffects()->GetShadowParams();
        WM(r.s2Shadows, params->maxHeight == 1.25f && params->depth == 2.5f && params->range == 30.0f && params->width == 128 &&
                            params->height == 256 && params->color.x == 0.1f && params->color.y == 0.2f &&
                            params->color.z == 0.3f && params->unknown10[0] == 7 && params->unknown10[1] == 8 &&
                            params->unknown10[2] == 9);
        const S_LightmapGroup *group = GetLightMapGroups()->FindGroup("grp");
        WM(r.s2Groups, group && group->frames.size() == 1 && group->frames[0] == C_str("f1"));
        WM(r.s2B820, std::find(log.begin(), log.end(), "s128 after") == log.end());
        WM(r.s2Music, game->m_music == 3 && game->m_alarmMusic == 4);
        // A frame looked up stays (null): a second actor of an unknown name asks the scene again.
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS,
                                    MsChunk(0xAE20, MsActor(MsChunk(0xAE23, MsText("nothere")) + MsChunk(0xAE22, MsValue(8)))));
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> again = MsLog();
        WM(r.s2Actors, std::find(again.begin(), again.end(), "dfind nothere") != again.end() &&
                           std::find(again.begin(), again.end(), "createactor 8 - 1 -1") != again.end());
        // The actor's frame from the names the scene enumerated (a sector too).
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS,
                                    MsChunk(0xAE20, MsActor(MsChunk(0xAE23, MsText("sector1")) + MsChunk(0xAE22, MsValue(8)))));
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> sector = MsLog();
        WM(r.s2Actors, std::find(sector.begin(), sector.end(), "createactor 8 sector1 1 -1") != sector.end() &&
                           std::find(sector.begin(), sector.end(), "dfind sector1") == sector.end());
        // Short entries: their values as they start (the first of their kind).
        game->m_fov = 9.0f;
        game->m_unknownD8 = 9;
        WmField<float>(game->GetScene(), 0x210) = 9.0f;
        g_lsStates[1] = 1;
        I3D_frame *empty = GfVisual("", I3DVISUAL_LMAP);
        WmField<I3D_mesh *>(empty, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x999);
        const std::string a255(255, 'a');
        I3D_frame *longName = GfVisual(a255, I3DVISUAL_LMAP);
        WmField<I3D_mesh *>(longName, 0x1E0) = reinterpret_cast<I3D_mesh *>(0x888);
        g_msNamed = {lm, empty, longName};
        g_tshiftModule.m_entries.clear();
        const int len256 = 256, len300 = 300;
        g_files[path] = MsChunkFile(
            MISSION_CHUNK_OBJECTS,
            MsChunk(0x3010, {1, 2}) + MsChunk(0x3200, {1}) + MsChunk(0xB004, {}) + MsChunk(0xB600, {1}) + MsChunk(0xB650, {1}) +
                MsChunk(0xAE00, MsChunk(0xAE01, {}) + MsChunk(0xAE01, MsValue(9) + MsValue(9) + MsValue(0) +
                                                                 std::vector<uint8_t>(255, 'b')) +
                                    MsChunk(0xAE01, MsValue(3) + MsValue(4) + MsValue(len256) +
                                                                 std::vector<uint8_t>(256, 'a')) +
                                    MsChunk(0xAE01, MsValue(5) + MsValue(6) + MsValue(len300) + std::vector<uint8_t>(300, 'a'))) +
                MsChunk(0xAE20, MsActor(MsChunk(0xAE22, {})) +
                                    MsActor(MsChunk(0xAE23, MsText("driverfound")) + MsChunk(0xAE22, MsValue(9)) + MsChunk(0xAE24, {3}))));
        g_gfLog.clear();
        loader.LoadScene2();
        const std::vector<std::string> shortLog = MsLog();
        const std::vector<S_TShiftEntry> &entries = g_tshiftModule.m_entries;
        WM(r.s2Settings, game->m_fov == 0.0f && WmField<float>(game->GetScene(), 0x210) == 0.0f && game->m_unknownD8 == 0);
        WM(r.s2States, std::find(shortLog.begin(), shortLog.end(), "setstate 1 0") != shortLog.end() &&
                           std::find(shortLog.begin(), shortLog.end(), "rstate 12 0") != shortLog.end());
        WM(r.s2TShift, entries.size() == 4 && entries[0].frame == empty && entries[0].unknown0C == 0 && entries[0].unknown10 == 0 &&
                           entries[1].frame == empty && entries[1].unknown0C == 9 && entries[2].frame == longName &&
                           entries[2].unknown0C == 3 && entries[3].frame == longName && entries[3].unknown0C == 5);
        WM(r.s2Actors, std::find(shortLog.begin(), shortLog.end(), "createactor 0 - 1 -1") != shortLog.end() &&
                           std::none_of(shortLog.begin(), shortLog.end(),
                                        [](const std::string &l) { return l.compare(0, 5, "props") == 0; }) &&
                           std::find(shortLog.begin(), shortLog.end(), "destroyactor 9 1") != shortLog.end());
        g_msNamed = {lm, obj, plain};
        // An entry list whose header does not read leaves the object chunk too: the end (loaded).
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0xB820, {1, 2}));
        WM(r.s2B820, loader.LoadScene2() == 0);
        // A matching driver state: not set.
        g_lsStates[1] = 1;
        g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0xB004, {1}));
        g_gfLog.clear();
        loader.LoadScene2();
        WM(r.s2States, std::find(g_gfLog.begin(), g_gfLog.end(), "setstate 1 1") == g_gfLog.end());
        g_lsStates[1] = 0;
    }

    // --- the progress: the file's position over its size (stage 1, the second table) ---
    {
        const std::vector<uint8_t> pad(100, 0), pad10(10, 0), pad40(40, 0);
        const std::vector<uint8_t> file =
            MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0x7777, pad) + MsChunk(0x4000, MsChunk(0x4011, pad10) + MsChunk(0x4011, pad40)));
        g_files[path] = file;
        g_loaderScreen.m_progress = 0.0f;
        loader.LoadScene2();
        // (The list's second entry, before its header.)
        const float last = 0.25f * (static_cast<float>(6 + 106 + 6 + 16) / static_cast<float>(file.size())) + 0.15f;
        WM(r.s2Progress, g_loaderScreen.m_progress == last && file.size() == 180);
        const std::vector<uint8_t> tail = MsChunkFile(MISSION_CHUNK_OBJECTS, MsChunk(0x7777, pad) + MsChunk(0x7778, pad40));
        g_files[path] = tail;
        g_loaderScreen.m_progress = 0.0f;
        loader.LoadScene2();
        WM(r.s2Progress, g_loaderScreen.m_progress == 0.25f * (118.0f / static_cast<float>(tail.size())) + 0.15f);
    }

    // --- failures: an object, an entry's header, an actor's part's header ---
    {
        const std::vector<std::vector<uint8_t>> bodies = {
            MsChunk(0x4000, MsChunk(0x4010, {0xFF})), MsChunk(0x3010, MsValue(1.0f)) + std::vector<uint8_t>{1, 2},
            MsChunk(0xAE20, MsActor({1, 2})), MsChunk(0xAE00, {1, 2}), MsChunk(0x4000, {1, 2})};
        for (const std::vector<uint8_t> &body : bodies) {
            g_files[path] = MsChunkFile(MISSION_CHUNK_OBJECTS, body);
            WM(r.s2Fail, loader.LoadScene2() == 1);
        }
    }

    g_loaderScreen.Finish();
    g_msNamed.clear();
    g_plScene.clear();
    std::printf("scene2: s2File %d s2Camera %d s2Settings %d s2List %d s2TShift %d s2Actors %d s2States %d s2Shadows %d "
                "s2Groups %d s2B820 %d s2Music %d s2Progress %d s2Fail %d\n",
                r.s2File, r.s2Camera, r.s2Settings, r.s2List, r.s2TShift, r.s2Actors, r.s2States, r.s2Shadows, r.s2Groups,
                r.s2B820, r.s2Music, r.s2Progress, r.s2Fail);
}

}  // namespace
