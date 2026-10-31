// Part 8e's camera module (CAMERA_TEST; game/camera_module.cpp): the world build (WORLD_TEST) with
// the real module linked in place of the world tests' fakes of it, after volumetrics_test.inc. The
// engine's frames, scene and driver slots it calls log what they get (the GEffects test's log); the
// team, the human effects' tactical view, the module's update and the collision's sphere test are
// faked. Prints per mission "== NAME" and the lines "camera:" (8e-i: the module's core) and
// "keeper:" (the vehicles' camera dummies) with 1 per check met.

#include <new>

#include "game/camera_module.h"
#include "game/submarine.h"

extern std::vector<std::string> g_dbgPrints;           // (harness.cpp: dbgPrintf's reports)

namespace {

// --- before main -------------------------------------------------------------------------------

// The module's static object is made before main, its human effects' camera with it: a driver slot
// set before the other static initialisers (the harness fills its vtables in main) gives a frame
// whose Release does nothing.
void *g_cmEarlyCameraVt[SLOTS];
struct S_CmEarlyCamera {
    void **vtable;
    uint8_t rest[FRAME_SIZE];
} g_cmEarlyCamera;
uint32_t __stdcall F_CmEarlyRelease(void *) { return 0; }
I3D_frame *__stdcall F_CmEarlyCreate(void *, int) { return reinterpret_cast<I3D_frame *>(&g_cmEarlyCamera); }
struct S_CmEarlyDriver {
    S_CmEarlyDriver()
    {
        g_cmEarlyCameraVt[0] = reinterpret_cast<void *>(&F_CmEarlyRelease);
        g_cmEarlyCamera.vtable = g_cmEarlyCameraVt;
        g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_CmEarlyCreate);
    }
};
__attribute__((init_priority(101))) S_CmEarlyDriver g_cmEarlyDriver;

// --- the engine --------------------------------------------------------------------------------

std::string CmText(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    return text;
}

// CreateFrame's next frames (none left: null).
std::deque<I3D_frame *> g_cmCreate;
I3D_frame *__stdcall F_CmCreateFrame(void *, int type)
{
    GfLog("create %d", type);
    if (g_cmCreate.empty())
        return nullptr;
    I3D_frame *f = g_cmCreate.front();
    g_cmCreate.pop_front();
    return f;
}
void __stdcall F_CmSetActive(void *scene, I3D_frame *camera)
{
    GfLog("active %s", GfName(camera));
    WmField<I3D_frame *>(scene, 0x15C) = camera;
}
void __stdcall F_CmSetPos(I3D_frame *self, const S_vector &pos)
{
    GfLog("setpos %s %.4f %.4f %.4f", GfName(self), pos.x, pos.y, pos.z);
    self->m_localMatrix(3) = pos;
}
void __stdcall F_CmSetDir(I3D_frame *self, const S_vector &dir, float roll)
{
    GfLog("setdir %s %.5f %.5f %.5f %.6f", GfName(self), dir.x, dir.y, dir.z, roll);
}
void __stdcall F_CmSetName(I3D_frame *self, const char *name) { GfLog("name %s %s", GfName(self), name); }
void __stdcall F_CmNearFov(I3D_frame *self, float fov)
{
    GfLog("nearfov %s %.4f", GfName(self), fov);
    WmField<float>(self, 0x12C) = fov;
}
void __stdcall F_CmNearRange(I3D_frame *self, float nearRange, float farRange)
{
    GfLog("nearrange %s %.3f %.1f", GfName(self), nearRange, farRange);
}

void CmInitVtables()
{
    GfInitVtables();
    for (void **vt : {g_gfFrameVt, g_gfCameraVt}) {
        vt[0x08 / 4] = reinterpret_cast<void *>(&F_CmSetPos);
        vt[0x0C / 4] = reinterpret_cast<void *>(&F_CmSetDir);
        vt[0x34 / 4] = reinterpret_cast<void *>(&F_CmSetName);
    }
    g_gfCameraVt[0x78 / 4] = reinterpret_cast<void *>(&F_CmNearFov);
    g_gfCameraVt[0x80 / 4] = reinterpret_cast<void *>(&F_CmNearRange);
    g_driverVtable[0x54 / 4] = reinterpret_cast<void *>(&F_CmCreateFrame);
    g_sceneVtable[0x90 / 4] = reinterpret_cast<void *>(&F_CmSetActive);
}

I3D_frame *CmCamera(const char *name, const S_vector &pos)
{
    I3D_frame *f = GfFrame(FRAME_CAMERA, name);
    f->m_localMatrix(3) = pos;
    return f;
}
I3D_frame *&CmActive()
{
    return WmField<I3D_frame *>(GetGame()->GetScene(), 0x15C);
}

// The sphere test: what it was asked, a hit at g_cmHitY when set.
struct S_CmSphere {
    S_vector from, dir;
    float radius;
    uint32_t types, flags;
};
std::vector<S_CmSphere> g_cmSpheres;
bool g_cmHit;
alignas(16) uint8_t g_cmVolume[0x60];
S_CollisionVolume *CmSphereHook(const S_vector &from, const S_vector &dir, float radius, uint32_t types, uint32_t flags)
{
    g_cmSpheres.push_back({from, dir, radius, types, flags});
    return g_cmHit ? reinterpret_cast<S_CollisionVolume *>(g_cmVolume) : nullptr;
}

// --- the module --------------------------------------------------------------------------------

alignas(16) uint8_t g_cmModule[sizeof(C_CameraModule)];
// A module made as the game makes its own (its human effects' camera "effects"), the log cleared.
C_CameraModule *CmNew()
{
    g_cmCreate.push_back(GfFrame(FRAME_CAMERA, "effects"));
    std::memset(g_cmModule, 0xCD, sizeof g_cmModule);
    C_CameraModule *m = new (g_cmModule) C_CameraModule(MODULE_CAMERA, "CameraModule");
    g_gfLog.clear();
    return m;
}
void CmDelete(C_CameraModule *m)
{
    m->~C_CameraModule();
    g_gfLog.clear();
}

bool CmDefault(const S_CameraSettings &s)
{
    return s.distance == 3.0f && s.unknown04 == 0.5f && s.unknown08 == 1.0f && s.unknown0C == 0.2f && s.flags == 0x8B &&
           s.maxPitch == 0.5f && s.minPitch == -0.5f && s.unknown1C == 12.0f;
}
bool CmVec4(const S_vector4 &v, float x, float y, float z, float w)
{
    return v.x == x && v.y == y && v.z == z && v.w == w;
}
std::vector<std::string> CmStartLog(const std::string &game)
{
    return {"create 3",
            "fov " + game + " 1.2500",
            "range " + game + " 0.5 500.0",
            "active " + game,
            "name " + game + " GameCamera",
            "nearfov " + game + " 1.1345",
            "nearrange " + game + " 0.010 10.0",
            "update " + game};
}

// The settings file: chunk 0 with the version, chunk 0 with the records.
std::vector<uint8_t> CmChunk(uint16_t id, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> out(6);
    const uint32_t size = static_cast<uint32_t>(data.size() + 6);
    std::memcpy(out.data(), &id, 2);
    std::memcpy(out.data() + 2, &size, 4);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
std::vector<uint8_t> CmSettingsFile(uint32_t version, int versionBytes, const std::vector<S_CameraSettings> &records,
                                    int extraBytes = 0)
{
    std::vector<uint8_t> v(static_cast<size_t>(versionBytes), 0xEE);
    std::memcpy(v.data(), &version, static_cast<size_t>(std::min(versionBytes, 4)));
    std::vector<uint8_t> r(records.size() * sizeof(S_CameraSettings) + static_cast<size_t>(extraBytes), 0x11);
    if (!records.empty())
        std::memcpy(r.data(), records.data(), records.size() * sizeof(S_CameraSettings));
    std::vector<uint8_t> file = CmChunk(0, v);
    const std::vector<uint8_t> second = CmChunk(0, r);
    file.insert(file.end(), second.begin(), second.end());
    return file;
}
std::vector<S_CameraSettings> CmRecords(int count, float base)
{
    std::vector<S_CameraSettings> out(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        S_CameraSettings &s = out[static_cast<size_t>(i)];
        s.distance = base + static_cast<float>(i);
        s.unknown04 = base + 0.25f;
        s.unknown08 = -base;
        s.unknown0C = 0.125f * static_cast<float>(i);
        s.flags = 0x100u + static_cast<uint32_t>(i);
        s.maxPitch = 1.5f;
        s.minPitch = -1.25f;
        s.unknown1C = 7.0f + static_cast<float>(i);
    }
    return out;
}
bool CmSame(const S_CameraSettings &a, const S_CameraSettings &b)
{
    return std::memcmp(&a, &b, sizeof a) == 0;
}
// Every record from `records`, the rest the sentinel `rest`.
bool CmSettingsAre(const C_CameraModule *m, const std::vector<S_CameraSettings> &records, const S_CameraSettings &rest)
{
    for (int i = 0; i < CAMERA_VIEW_COUNT; ++i) {
        const S_CameraSettings &want = i < static_cast<int>(records.size()) ? records[static_cast<size_t>(i)] : rest;
        if (!CmSame(m->m_settings[i], want))
            return false;
    }
    return true;
}

struct S_CameraResults {
    bool cmMake = true, cmGetters = true, cmSaveLoad = true, cmRestart = true, cmStart = true, cmStartNone = true;
    bool cmSettings = true, cmSettingsFile = true, cmStop = true, cmPos = true, cmDir = true, cmBlend = true;
    bool cmTarget = true, cmFov = true, cmViewDir = true;
    bool kpMake = true, kpRestore = true, kpUpdate = true, kpCar = true, kpSubmarine = true, kpPlayer = true;
};

void CameraModuleTest(S_CameraResults &r)
{
    C_game *game = GetGame();
    const std::string tables = "tables\\gamecam.set";
    g_files.erase(tables);

    // --- made as the game's static object makes it ---
    {
        I3D_frame *effects = GfFrame(FRAME_CAMERA, "effects");
        g_cmCreate.push_back(effects);
        g_gfLog.clear();
        std::memset(g_cmModule, 0xCD, sizeof g_cmModule);
        C_CameraModule *m = new (g_cmModule) C_CameraModule(MODULE_CAMERA, "CameraModule");
        WM(r.cmMake, GfLogIs({"create 3"}));
        WM(r.cmMake, std::strcmp(m->GetName(), "CameraModule") == 0 && m->GetId() == MODULE_CAMERA);
        for (int i = 0; i < CAMERA_VIEW_COUNT; ++i)
            WM(r.cmMake, CmDefault(m->m_settings[i]));
        WM(r.cmMake, m->m_wantedDir.x == 0.0f && m->m_wantedDir.y == -1.0f && m->m_wantedDir.z == 1.0f);
        WM(r.cmMake, m->m_view == 1 && m->m_viewSettings == &m->m_settings[1]);
        WM(r.cmMake, !m->m_gameCamera && !m->m_sceneCamera && !m->m_camera && m->m_unknown254 == 0.0f &&
                         !m->m_unknown25C);
        WM(r.cmMake, !m->m_unknown26D && !m->m_unknown26E && m->m_unknown26F && !m->m_underwater);
        // The fields the original leaves: 0, as in its zeroed static object.
        WM(r.cmMake, m->m_unknown060 == 0 && m->m_unknown064 == 0 && m->m_unknown068 == 0 && m->m_unknown06C == 0.0f &&
                         m->m_unknown070 == 0 && m->m_unknown074 == 0 && m->m_unknown078 == 0 &&
                         m->m_unknown07C == 0.0f && m->m_unknown0D0 == 0 && m->m_unknown250 == 0 &&
                         m->m_unknown258 == 0.0f && m->m_unknown268 == 0 && m->m_unknown274 == 0 &&
                         m->m_unknown27C == 0.0f && m->m_blendTime == 0 && m->m_unknown284 == 0 &&
                         m->m_targetSpeed == 0.0f && m->m_unknown294 == 0 && m->m_unknown298 == 0 &&
                         m->m_unknown29C == 0 && m->m_unknown2A0 == 0 && m->m_unknown2A4 == 0.0f &&
                         m->m_unknown2A8 == 0.0f && m->m_unknown2AC == 0.0f && m->m_unknown2B8 == 0.0f &&
                         m->m_unknown2BC == 0.0f);
        C_CameraHumanEffects *e = m->m_humanEffects;
        WM(r.cmMake, e && (reinterpret_cast<uintptr_t>(e) & 15) == 0);
        if (e) {
            WM(r.cmMake, e->m_camera == effects && !e->m_gameCamera && !e->m_tactical && e->m_height == 5.0f &&
                             e->m_zoom == 1.0f && e->m_motionX == 0.0f && e->m_motionY == 0.0f &&
                             e->m_unknown44 == 0.0f && e->m_humans.empty());
        }
        g_gfLog.clear();
        m->~C_CameraModule();
        WM(r.cmMake, GfLogIs({"release effects"}));
        // Without a camera from the driver.
        std::memset(g_cmModule, 0xCD, sizeof g_cmModule);
        g_gfLog.clear();
        m = new (g_cmModule) C_CameraModule(MODULE_CAMERA, "CameraModule");
        WM(r.cmMake, m->m_humanEffects && !m->m_humanEffects->m_camera && GfLogIs({"create 3"}));
        g_gfLog.clear();
        m->~C_CameraModule();
        WM(r.cmMake, g_gfLog.empty());
        // The game's own.
        WM(r.cmMake, GetCameraModule() == &g_cameraModule && &g_cameraHumanEffects == &g_cameraModule.m_humanEffects &&
                         g_cameraHumanEffects == g_cameraModule.m_humanEffects && g_cameraHumanEffects &&
                         g_cameraHumanEffects->m_camera == reinterpret_cast<I3D_camera *>(&g_cmEarlyCamera));
    }

    C_CameraModule *m = CmNew();

    // --- the getters ---
    {
        int marker;
        I3D_frame *cam = GfFrame(FRAME_DUMMY, "dummy");
        m->m_unknown26D = true;
        WM(r.cmGetters, m->GetUnknown78() && !m->GetState() && !m->IsUnderwater() && !m->HasScriptedView() &&
                            !m->GetUnknown84());
        m->m_unknown26D = false;
        m->m_underwater = true;
        WM(r.cmGetters, !m->GetUnknown78() && m->GetState() && m->IsUnderwater() && !m->GetUnknown84());
        m->m_underwater = false;
        m->m_scriptedView = &marker;
        WM(r.cmGetters, m->HasScriptedView() && !m->GetState());
        m->m_scriptedView = nullptr;
        m->m_unknown26E = true;
        WM(r.cmGetters, m->GetUnknown84() && !m->GetUnknown78() && !m->HasScriptedView());
        m->m_unknown26E = false;
        WM(r.cmGetters, m->GetHumanEffects() == m->m_humanEffects && !m->GetCamera() && !m->GetListenerSector());
        m->m_camera = cam;
        m->m_listenerSector = reinterpret_cast<I3D_sector *>(&marker);
        WM(r.cmGetters, m->GetCamera() == cam && m->GetListenerSector() == reinterpret_cast<I3D_sector *>(&marker));
        m->m_camera = nullptr;
        m->m_listenerSector = nullptr;
        WM(r.cmGetters, m->IsGameCamera(nullptr) && !m->IsGameCamera(cam));
        m->m_gameCamera = reinterpret_cast<I3D_camera *>(cam);
        WM(r.cmGetters, m->IsGameCamera(cam) && !m->IsGameCamera(nullptr));
        m->m_gameCamera = nullptr;
        WM(r.cmGetters, g_gfLog.empty());
    }

    // --- the saved games ---
    {
        C_chunk chunk;
        g_gfLog.clear();
        WM(r.cmSaveLoad, m->SaveGame(chunk) && g_gfLog.empty());
        WM(r.cmSaveLoad, m->LoadGame(chunk) && GfLogIs({"cam40 0", "camupdate 100"}));
    }

    // The settings file and the game's view.
    std::vector<uint8_t> realFile;
    WmReadFile(g_wmData + "/Tables/gamecam.set", realFile);
    game->m_fov = 1.25f;
    game->m_nearClip = 0.5f;
    game->m_farClip = 500.0f;

    // --- a client's restart: stop, start, the sector call unless in the Junkers ---
    {
        alignas(16) static uint8_t humanMem[sizeof(C_Human)], carMem[sizeof(C_Car)], junkersMem[sizeof(C_Car)];
        C_Human *player = reinterpret_cast<C_Human *>(humanMem);
        C_Actor *car = reinterpret_cast<C_Actor *>(carMem);
        C_Actor *junkers = reinterpret_cast<C_Actor *>(junkersMem);
        car->m_type = ACTOR_CAR;
        junkers->m_type = ACTOR_JUNKERS88;
        g_files[tables] = CmSettingsFile(2, 4, CmRecords(11, 20.0f));
        struct S_Case {
            uint32_t role;
            C_Human *active;
            C_Actor *vehicle;
            bool restarts, sector;
        };
        const S_Case cases[] = {{GAME_ROLE_SERVER, nullptr, nullptr, false, false},
                                {GAME_ROLE_CLIENT, nullptr, nullptr, true, true},
                                {GAME_ROLE_LOCAL, player, nullptr, true, true},
                                {GAME_ROLE_CLIENT, player, car, true, true},
                                {GAME_ROLE_CLIENT, player, junkers, true, false},
                                {GAME_ROLE_LOCAL, player, junkers, true, false},
                                {GAME_ROLE_SERVER, player, car, false, false}};
        for (const S_Case &c : cases) {
            I3D_frame *scene = CmCamera("scene", S_vector(1.0f, 2.0f, 3.0f));
            CmActive() = scene;
            g_cmCreate.clear();
            g_cmCreate.push_back(CmCamera("game", S_vector(0.0f, 0.0f, 0.0f)));
            g_gameRole = c.role;
            g_gfActive = c.active;
            player->vehicle = c.vehicle;
            m->m_sceneCamera = nullptr;
            m->m_gameCamera = nullptr;
            m->m_camera = nullptr;
            m->m_unknown268 = 9;
            g_gfLog.clear();
            m->RestartMission(false);
            std::vector<std::string> want;
            if (c.restarts)
                want = CmStartLog("game");
            if (c.sector)
                want.push_back("cam40 0");
            WM(r.cmRestart, GfVecIs(g_gfLog, want));
            WM(r.cmRestart, (m->m_unknown268 == 5) == c.restarts && (m->m_sceneCamera == scene) == c.restarts);
        }
        // A client stops first: the old cameras go.
        I3D_frame *scene = CmCamera("scene", S_vector(1.0f, 2.0f, 3.0f));
        CmActive() = CmCamera("game", S_vector(0.0f, 0.0f, 0.0f));
        m->m_sceneCamera = reinterpret_cast<I3D_camera *>(scene);
        m->m_gameCamera = reinterpret_cast<I3D_camera *>(CmActive());
        g_cmCreate.clear();
        g_cmCreate.push_back(CmCamera("game2", S_vector(0.0f, 0.0f, 0.0f)));
        g_gameRole = GAME_ROLE_CLIENT;
        g_gfActive = nullptr;
        g_gfLog.clear();
        m->RestartMission(true);
        std::vector<std::string> want = {"active scene", "release game"};
        for (const std::string &l : CmStartLog("game2"))
            want.push_back(l);
        want.push_back("cam40 0");
        WM(r.cmRestart, GfVecIs(g_gfLog, want) && m->m_sceneCamera == scene);
        g_gameRole = GAME_ROLE_LOCAL;
        player->vehicle = nullptr;
    }

    // --- the start ---
    {
        int marker;
        auto junk = [&]() {
            m->m_unknown2B0 = true;
            m->m_scriptedView = &marker;
            m->m_unknown258 = 1.0f;
            m->m_unknown26C = true;
            m->m_unknown26D = true;
            m->m_unknown268 = 9;
            m->m_unknown26E = true;
            m->m_unknown26F = false;
            m->m_underwater = true;
            m->m_unknown278 = true;
            m->m_blendTime = 7;
            m->m_unknown284 = 7;
            m->m_unknown060 = m->m_unknown064 = m->m_unknown068 = 7;
            m->m_unknown070 = m->m_unknown074 = m->m_unknown078 = 7;
            for (int i = 0; i < 3; ++i)
                m->m_unknown080[i] = m->m_unknown090[i] = 7.0f;
            m->m_target = reinterpret_cast<I3D_frame *>(&marker);
            m->m_targetSpeed = 7.0f;
            m->m_listenerSector = reinterpret_cast<I3D_sector *>(&marker);
            m->m_unknown250 = 7;
            m->m_unknown290 = true;
            m->m_unknown298 = m->m_unknown29C = m->m_unknown2A0 = 7;
            m->m_unknown0D0 = 7;
            // Not touched.
            m->m_unknown294 = 7;
            m->m_unknown254 = 7.0f;
            m->m_unknown274 = 7;
            m->m_unknown27C = 7.0f;
            m->m_unknown2A4 = 7.0f;
            m->m_unknown06C = 7.0f;
            m->m_pos = m->m_wantedPos = m->m_dir = m->m_wantedDir = {7.0f, 7.0f, 7.0f, 7.0f};
            m->m_sceneCamera = nullptr;
            m->m_gameCamera = nullptr;
        };
        auto reset = [&]() {
            return !m->m_unknown2B0 && !m->m_scriptedView && m->m_unknown258 == 6.0f && !m->m_unknown26C &&
                   !m->m_unknown26D && m->m_unknown268 == 5 && !m->m_unknown26E && m->m_unknown26F && !m->m_underwater &&
                   !m->m_unknown278 && m->m_blendTime == 0 && m->m_unknown284 == 0 && m->m_unknown060 == 0 &&
                   m->m_unknown064 == 0 && m->m_unknown068 == 0 && m->m_unknown070 == 0 && m->m_unknown074 == 0 &&
                   m->m_unknown078 == 0 && m->m_unknown080[0] == 0.0f && m->m_unknown080[1] == 0.0f &&
                   m->m_unknown080[2] == 0.0f && m->m_unknown090[0] == 0.0f && m->m_unknown090[1] == 0.0f &&
                   m->m_unknown090[2] == 0.0f && !m->m_target && m->m_targetSpeed == -1.0f && !m->m_listenerSector &&
                   m->m_unknown250 == 0 && !m->m_unknown290 && m->m_unknown298 == 0 && m->m_unknown29C == 0 &&
                   m->m_unknown2A0 == 0 && m->m_unknown0D0 == 0 && m->m_unknown294 == 7 && m->m_unknown254 == 7.0f &&
                   m->m_unknown274 == 7 && m->m_unknown27C == 7.0f && m->m_unknown2A4 == 7.0f && m->m_unknown06C == 7.0f;
        };
        g_files[tables] = realFile;
        // The server's side: nothing.
        junk();
        m->m_unknown2AC = 42.0f;
        g_gfLog.clear();
        m->StartMission(true);
        WM(r.cmStart, g_gfLog.empty() && m->m_unknown268 == 9 && m->m_scriptedView == &marker &&
                          m->m_unknown2AC == 42.0f);
        // The game camera in the scene camera's place.
        I3D_frame *scene = CmCamera("scene", S_vector(1.0f, 2.0f, 3.0f));
        scene->m_rot = S_quat(0.6f, 0.0f, 0.0f, 0.8f);
        scene->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
        CmActive() = scene;
        I3D_frame *cam = CmCamera("game", S_vector(0.0f, 0.0f, 0.0f));
        cam->m_localMatrix.m[2][0] = 0.0f;
        cam->m_localMatrix.m[2][1] = 3.0f;
        cam->m_localMatrix.m[2][2] = 4.0f;
        cam->m_localMatrix.m[2][3] = 0.5f;
        cam->m_localMatrix.m[3][3] = 0.75f;
        cam->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY | FRMFLAGS_BOUND_VALID;
        g_cmCreate.clear();
        g_cmCreate.push_back(cam);
        junk();
        g_dbgPrints.clear();
        g_gfLog.clear();
        m->StartMission(false);
        WM(r.cmStart, GfVecIs(g_gfLog, CmStartLog("game")) && g_dbgPrints.empty());
        WM(r.cmStart, reset() && m->m_sceneCamera == scene && m->m_gameCamera == reinterpret_cast<I3D_camera *>(cam) &&
                          CmActive() == cam);
        S_quat rot = S_quat(0.6f, 0.0f, 0.0f, 0.8f);
        rot.Normalize();
        WM(r.cmStart, cam->m_localMatrix(3) == S_vector(1.0f, 2.0f, 3.0f) && cam->m_rot.s == rot.s &&
                          cam->m_rot.v == rot.v && cam->m_frameFlags == (FRMFLAGS_CHANGED | 0x8u));
        WM(r.cmStart, CmVec4(m->m_pos, 1.0f, 2.0f, 3.0f, 0.75f) && CmVec4(m->m_wantedPos, 1.0f, 2.0f, 3.0f, 0.75f) &&
                          CmVec4(m->m_dir, 0.0f, 0.6f, 0.8f, 0.5f) && CmVec4(m->m_wantedDir, 0.0f, 0.6f, 0.8f, 0.5f));
        WM(r.cmStart, WmField<float>(cam, 0x120) == 1.25f && WmField<float>(cam, 0x124) == 0.5f &&
                          WmField<float>(cam, 0x128) == 500.0f && WmField<float>(cam, 0x12C) == 1.1344640f);
        WM(r.cmStart, m->m_unknown2AC == 3.0f && realFile.size() == 368 &&
                          std::memcmp(m->m_settings, realFile.data() + 16, sizeof m->m_settings) == 0);
        // The scene camera's rotation from its matrix; a locked position stays.
        I3D_frame *scene2 = CmCamera("scene2", S_vector(4.0f, 5.0f, 6.0f));
        scene2->m_localMatrix.m[0][0] = 0.0f;
        scene2->m_localMatrix.m[0][2] = -1.0f;
        scene2->m_localMatrix.m[2][0] = 1.0f;
        scene2->m_localMatrix.m[2][2] = 0.0f;
        scene2->m_rot = S_quat(0.5f, 0.5f, 0.5f, 0.5f);
        scene2->m_frameFlags &= ~FRMFLAGS_ROT_DIRTY;
        S_quat made;
        made.Make(scene2->m_localMatrix);
        made.Normalize();
        CmActive() = scene2;
        I3D_frame *cam2 = CmCamera("game2", S_vector(9.0f, 8.0f, 7.0f));
        cam2->m_frameFlags |= FRMFLAGS_POS_LOCKED;
        g_cmCreate.clear();
        g_cmCreate.push_back(cam2);
        junk();
        g_gfLog.clear();
        m->StartMission(false);
        WM(r.cmStart, GfVecIs(g_gfLog, CmStartLog("game2")) && reset());
        WM(r.cmStart, (scene2->m_frameFlags & FRMFLAGS_ROT_DIRTY) && cam2->m_localMatrix(3) == S_vector(9.0f, 8.0f, 7.0f) &&
                          cam2->m_rot.s == made.s && cam2->m_rot.v == made.v &&
                          (cam2->m_frameFlags & ~FRMFLAGS_POS_LOCKED) == (FRMFLAGS_CHANGED | 0x8u));
        WM(r.cmStart, CmVec4(m->m_pos, 9.0f, 8.0f, 7.0f, 1.0f) && CmVec4(m->m_dir, 0.0f, 0.0f, 1.0f, 0.0f));
    }

    // --- no game camera ---
    {
        CmActive() = nullptr;
        I3D_frame *cam = CmCamera("game", S_vector(0.0f, 0.0f, 0.0f));
        g_cmCreate.clear();
        g_cmCreate.push_back(cam);
        m->m_unknown268 = 9;
        m->m_pos = {7.0f, 7.0f, 7.0f, 7.0f};
        g_dbgPrints.clear();
        g_gfLog.clear();
        m->StartMission(false);
        WM(r.cmStartNone, GfLogIs({"create 3"}) && !m->m_sceneCamera && m->m_gameCamera == reinterpret_cast<I3D_camera *>(cam) &&
                              m->m_unknown268 == 5 && CmVec4(m->m_pos, 7.0f, 7.0f, 7.0f, 7.0f));
        WM(r.cmStartNone, g_dbgPrints == std::vector<std::string>{"###### (MO) Game camera not created."});
        I3D_frame *scene = CmCamera("scene", S_vector(1.0f, 2.0f, 3.0f));
        CmActive() = scene;
        g_cmCreate.clear();
        m->m_unknown268 = 9;
        g_dbgPrints.clear();
        g_gfLog.clear();
        m->StartMission(false);
        WM(r.cmStartNone, GfLogIs({"create 3"}) && m->m_sceneCamera == scene && !m->m_gameCamera && m->m_unknown268 == 5 &&
                              CmActive() == scene && CmVec4(m->m_pos, 7.0f, 7.0f, 7.0f, 7.0f));
        WM(r.cmStartNone, g_dbgPrints == std::vector<std::string>{"###### (MO) Game camera not created."});
    }

    // --- the settings ---
    {
        S_CameraSettings sentinel;
        sentinel.distance = 99.0f;
        sentinel.flags = 0x55;
        auto fill = [&]() {
            for (S_CameraSettings &s : m->m_settings)
                s = sentinel;
            m->m_unknown2AC = 42.0f;
        };
        const std::vector<S_CameraSettings> recs = CmRecords(12, 10.0f);
        const std::vector<S_CameraSettings> eleven(recs.begin(), recs.begin() + 11);
        const std::vector<S_CameraSettings> three(recs.begin(), recs.begin() + 3);
        const std::vector<S_CameraSettings> four(recs.begin(), recs.begin() + 4);
        struct S_Case {
            std::vector<uint8_t> file;
            std::vector<S_CameraSettings> want;
            float unknown2AC;
        };
        const S_Case cases[] = {
            {CmSettingsFile(2, 4, eleven), eleven, 11.0f},
            {CmSettingsFile(2, 4, three), three, 11.0f},
            {CmSettingsFile(2, 4, three, 0x10), three, 11.0f},     // three and a half
            {CmSettingsFile(2, 4, three, 0x1F), three, 11.0f},
            {CmSettingsFile(2, 4, four), four, 11.0f},
            {CmSettingsFile(2, 4, recs), eleven, 11.0f},           // twelve: the first 11
            {CmSettingsFile(2, 4, {}, 0x1F), {}, 99.0f},           // not one whole record
            {CmSettingsFile(2, 4, {}), {}, 99.0f},
            {CmSettingsFile(2, 8, three), three, 11.0f},           // a longer version chunk
            {CmSettingsFile(1, 4, eleven), {}, 99.0f},
            {CmSettingsFile(3, 4, eleven), {}, 99.0f},
            {CmSettingsFile(0x102, 4, eleven), {}, 99.0f},
            {CmChunk(0, std::vector<uint8_t>{2, 0, 0, 0}), {}, 99.0f},   // no second chunk
        };
        for (const S_Case &c : cases) {
            g_files[tables] = c.file;
            fill();
            g_dbgPrints.clear();
            m->LoadSettings();
            WM(r.cmSettings, CmSettingsAre(m, c.want, sentinel) && m->m_unknown2AC == c.unknown2AC && g_dbgPrints.empty());
        }
        // One record: view 1 stays.
        g_files[tables] = CmSettingsFile(2, 4, {recs[5]});
        fill();
        m->LoadSettings();
        WM(r.cmSettings, CmSettingsAre(m, {recs[5]}, sentinel) && m->m_unknown2AC == 99.0f);
        // No file: the 3 m.
        g_files.erase(tables);
        fill();
        g_dbgPrints.clear();
        m->LoadSettings();
        WM(r.cmSettingsFile, CmSettingsAre(m, {}, sentinel) && m->m_unknown2AC == 3.0f &&
                                 g_dbgPrints == std::vector<std::string>{"###### (MO) Load camera file 'tables\\gamecam.set' failed."});
        // The game's file.
        g_files[tables] = realFile;
        fill();
        g_dbgPrints.clear();
        m->LoadSettings();
        WM(r.cmSettingsFile, std::memcmp(m->m_settings, realFile.data() + 16, sizeof m->m_settings) == 0 &&
                                 m->m_unknown2AC == 3.0f && g_dbgPrints.empty() && m->m_settings[10].distance == 13.0f &&
                                 m->m_settings[8].flags == 0x89);
        m->ResetUnknown2AC();
        WM(r.cmSettingsFile, m->m_unknown2AC == 3.0f);
    }

    // --- the stop ---
    {
        alignas(16) static uint8_t humanMem[sizeof(C_Human)], carMem[sizeof(C_Car)];
        C_Human *human = reinterpret_cast<C_Human *>(humanMem);
        human->m_type = ACTOR_HUMAN;
        C_Actor *car = reinterpret_cast<C_Actor *>(carMem);
        car->m_type = ACTOR_CAR;
        static S_FrameData humanData, carData;
        humanData.actor = human;
        carData.actor = car;
        I3D_frame *model = GfFrame(FRAME_MODEL, "soldier");
        model->m_data = &humanData;
        I3D_frame *carModel = GfFrame(FRAME_MODEL, "car");
        carModel->m_data = &carData;
        I3D_frame *visual = GfFrame(FRAME_VISUAL, "visual");
        visual->m_data = &humanData;
        I3D_frame *bare = GfFrame(FRAME_MODEL, "bare");
        struct S_Case {
            I3D_frame *owner;
            bool unknown94;
        };
        const S_Case cases[] = {{model, true}, {carModel, false}, {visual, false}, {bare, false}, {nullptr, false}};
        for (const S_Case &c : cases) {
            I3D_frame *scene = CmCamera("scene", S_vector(0.0f, 0.0f, 0.0f));
            I3D_frame *cam = CmCamera("game", S_vector(0.0f, 0.0f, 0.0f));
            I3D_frame *dummy = GfFrame(FRAME_DUMMY, "camera3RD");
            dummy->m_ownerModel = c.owner;
            m->m_sceneCamera = reinterpret_cast<I3D_camera *>(scene);
            m->m_gameCamera = reinterpret_cast<I3D_camera *>(cam);
            m->m_camera = dummy;
            CmActive() = cam;
            g_dmTactical = false;
            g_gfLog.clear();
            m->StopMission(false);
            std::vector<std::string> want = {"active scene", "release game"};
            if (c.unknown94)
                want.push_back("unknown94 0.0");
            want.push_back("release camera3RD");
            WM(r.cmStop, GfVecIs(g_gfLog, want) && !m->m_sceneCamera && !m->m_gameCamera && !m->m_camera &&
                             CmActive() == scene);
        }
        // The tactical view off first, on both sides; the server stops nothing else.
        I3D_frame *scene = CmCamera("scene", S_vector(0.0f, 0.0f, 0.0f));
        m->m_sceneCamera = reinterpret_cast<I3D_camera *>(scene);
        m->m_gameCamera = nullptr;
        m->m_camera = nullptr;
        g_dmTactical = true;
        g_gfLog.clear();
        m->StopMission(true);
        WM(r.cmStop, GfLogIs({"tactical 0"}) && m->m_sceneCamera == scene);
        g_gfLog.clear();
        m->StopMission(false);
        WM(r.cmStop, GfLogIs({"tactical 0", "active scene"}) && !m->m_sceneCamera);
        g_dmTactical = false;
        g_gfLog.clear();
        m->StopMission(false);
        m->StopMission(true);
        WM(r.cmStop, g_gfLog.empty());
    }

    // --- the positions and directions ---
    {
        S_vector4 v = {7.0f, 7.0f, 7.0f, 7.0f};
        CmActive() = nullptr;
        m->m_gameCamera = nullptr;
        WM(r.cmPos, !m->GetCameraPos(0, &v) && !m->GetCameraPos(1, &v) && !m->GetCameraPos(4, &v) &&
                        !m->GetCameraPos(-1, &v) && CmVec4(v, 7.0f, 7.0f, 7.0f, 7.0f));
        WM(r.cmDir, !m->GetCameraDir(0, &v) && !m->GetCameraDir(1, &v) && !m->GetCameraDir(4, &v) &&
                        !m->GetCameraDir(-1, &v) && CmVec4(v, 7.0f, 7.0f, 7.0f, 7.0f));
        I3D_frame *scene = CmCamera("scene", S_vector(1.0f, 2.0f, 3.0f));
        scene->m_localMatrix.m[3][3] = 0.25f;
        scene->m_localMatrix.m[2][0] = 0.0f;
        scene->m_localMatrix.m[2][1] = -6.0f;
        scene->m_localMatrix.m[2][2] = 8.0f;
        scene->m_localMatrix.m[2][3] = 0.125f;
        I3D_frame *cam = CmCamera("game", S_vector(-4.0f, 5.0f, -6.0f));
        cam->m_localMatrix.m[2][0] = 2.0f;
        cam->m_localMatrix.m[2][2] = 0.0f;
        CmActive() = scene;
        m->m_gameCamera = reinterpret_cast<I3D_camera *>(cam);
        m->m_pos = {1.5f, 2.5f, 3.5f, 4.5f};
        m->m_wantedPos = {-1.5f, -2.5f, -3.5f, -4.5f};
        m->m_dir = {0.5f, 0.25f, 0.125f, 2.0f};
        m->m_wantedDir = {-0.5f, -0.25f, -0.125f, -2.0f};
        WM(r.cmPos, m->GetCameraPos(0, &v) && CmVec4(v, 1.0f, 2.0f, 3.0f, 0.25f));
        WM(r.cmPos, m->GetCameraPos(1, &v) && CmVec4(v, -4.0f, 5.0f, -6.0f, 1.0f));
        WM(r.cmPos, m->GetCameraPos(2, &v) && CmVec4(v, 1.5f, 2.5f, 3.5f, 4.5f));
        WM(r.cmPos, m->GetCameraPos(3, &v) && CmVec4(v, -1.5f, -2.5f, -3.5f, -4.5f));
        WM(r.cmDir, m->GetCameraDir(0, &v) && CmVec4(v, 0.0f, -0.6f, 0.8f, 0.125f));
        WM(r.cmDir, m->GetCameraDir(1, &v) && CmVec4(v, 1.0f, 0.0f, 0.0f, 0.0f));
        WM(r.cmDir, m->GetCameraDir(2, &v) && CmVec4(v, 0.5f, 0.25f, 0.125f, 2.0f));
        WM(r.cmDir, m->GetCameraDir(3, &v) && CmVec4(v, -0.5f, -0.25f, -0.125f, -2.0f));
        // Short directions: a unit axis by the sign (x, then z, then y).
        const float rows[][3] = {{0.0f, 0.0f, 0.0f}, {-1e-5f, 1e-5f, 1e-5f}, {0.0f, -1e-5f, 2e-5f}, {0.0f, -1e-5f, 0.0f},
                                 {1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1e-5f}, {0.0f, 1e-5f, 0.0f}};
        const float want[][3] = {{0.0f, 1.0f, 0.0f}, {-1.0f, 1e-5f, 1e-5f}, {0.0f, -1e-5f, 1.0f}, {0.0f, -1.0f, 0.0f},
                                 {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}};
        for (int i = 0; i < 7; ++i) {
            for (int k = 0; k < 3; ++k)
                cam->m_localMatrix.m[2][k] = rows[i][k];
            WM(r.cmDir, m->GetCameraDir(1, &v) && CmVec4(v, want[i][0], want[i][1], want[i][2], 0.0f));
        }
        // Nearly unit: kept as it is.
        cam->m_localMatrix.m[2][0] = 0.6f;
        cam->m_localMatrix.m[2][1] = 0.0f;
        cam->m_localMatrix.m[2][2] = 0.8f;
        WM(r.cmDir, m->GetCameraDir(1, &v) && CmVec4(v, 0.6f, 0.0f, 0.8f, 0.0f));
        // The sum (y y + z z) + x x.
        cam->m_localMatrix.m[2][0] = 0.3f;
        cam->m_localMatrix.m[2][1] = 1.7f;
        cam->m_localMatrix.m[2][2] = -2.9f;
        {
            const S_vector d(0.3f, 1.7f, -2.9f);
            const float k = 1.0f / std::sqrt((d.y * d.y + d.z * d.z) + d.x * d.x);
            WM(r.cmDir, m->GetCameraDir(1, &v) && CmVec4(v, d.x * k, d.y * k, d.z * k, 0.0f));
            WM(r.cmPos, g_gfLog.empty());
        }
        m->m_gameCamera = nullptr;
    }

    // --- the blend time and the target ---
    {
        alignas(16) static uint8_t humanMem[sizeof(C_Human)], carMem[sizeof(C_Car)];
        C_Human *player = reinterpret_cast<C_Human *>(humanMem);
        C_Actor *car = reinterpret_cast<C_Actor *>(carMem);
        struct S_Case {
            uint32_t role;
            C_Human *active;
            C_Actor *vehicle;
            bool set;
        };
        const S_Case cases[] = {{GAME_ROLE_LOCAL, player, car, false},  {GAME_ROLE_CLIENT, player, car, false},
                                {GAME_ROLE_LOCAL, player, nullptr, true}, {GAME_ROLE_LOCAL, nullptr, nullptr, true},
                                {GAME_ROLE_SERVER, player, car, true},  {0u, player, car, true}};
        int time = 100;
        for (const S_Case &c : cases) {
            g_gameRole = c.role;
            g_gfActive = c.active;
            player->vehicle = c.vehicle;
            m->m_blendTime = 5;
            m->SetBlendTime(++time);
            WM(r.cmBlend, m->m_blendTime == (c.set ? time : 5));
        }
        g_gameRole = GAME_ROLE_LOCAL;
        g_gfActive = nullptr;
        I3D_frame *f = GfFrame(FRAME_DUMMY, "target");
        m->SetTarget(f, 90);
        WM(r.cmTarget, m->m_target == f && m->m_targetSpeed == 90.0f * 0.017453292f);
        m->SetTarget(nullptr, -1);
        WM(r.cmTarget, !m->m_target && m->m_targetSpeed == -0.017453292f);
        m->SetTarget(f, 7);
        WM(r.cmTarget, m->m_target == f && m->m_targetSpeed == 7.0f * 0.017453292f);
    }

    // --- the fields of view ---
    {
        m->m_gameCamera = nullptr;
        g_gfLog.clear();
        WM(r.cmFov, !m->SetFov(0.9f) && !m->SetNearSectorFov(0.7f) && m->GetFov() == 1.3962635f &&
                        m->GetNearSectorFov() == 1.3962635f && g_gfLog.empty());
        I3D_frame *cam = CmCamera("game", S_vector(0.0f, 0.0f, 0.0f));
        WmField<float>(cam, 0x120) = 0.25f;
        WmField<float>(cam, 0x12C) = 0.375f;
        m->m_gameCamera = reinterpret_cast<I3D_camera *>(cam);
        WM(r.cmFov, m->GetFov() == 0.25f && m->GetNearSectorFov() == 0.375f);
        WM(r.cmFov, m->SetFov(0.9f) && GfLogIs({"fov game 0.9000", "update game"}) && m->GetFov() == 0.9f &&
                        m->GetNearSectorFov() == 0.375f);
        g_gfLog.clear();
        WM(r.cmFov, m->SetNearSectorFov(0.7f) && GfLogIs({"nearfov game 0.7000", "update game"}) &&
                        m->GetNearSectorFov() == 0.7f && m->GetFov() == 0.9f);
        m->m_gameCamera = nullptr;
        I3D_frame *scene = CmCamera("scene", S_vector(0.0f, 0.0f, 0.0f));
        WmField<float>(scene, 0x124) = 2.0f;
        WmField<float>(scene, 0x128) = 321.5f;
        CmActive() = scene;
        WM(r.cmFov, m->GetViewDistance() == 321.5f);
    }

    // --- the view direction ---
    {
        const S_vector4 dir = {0.25f, -0.5f, 0.75f, 2.0f};
        auto set = [&](int view, uint32_t flags) {
            m->m_wantedDir = {7.0f, 7.0f, 7.0f, 7.0f};
            m->m_view = view;
            m->m_viewSettings = &m->m_settings[view == 10 ? 3 : 5];
            m->m_viewSettings->flags = flags;
            m->SetViewDir(dir);
            return CmVec4(m->m_wantedDir, 0.25f, -0.5f, 0.75f, 2.0f);
        };
        WM(r.cmViewDir, !set(1, 0x8B) && set(1, 0x83) && !set(1, 0x08) && set(1, 0xF7) && set(9, 0) && !set(10, 0) &&
                            set(11, 0) && !set(10, 0x08) && set(0, 0x100));
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
    }

    CmDelete(m);
    CmActive() = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
    g_files.erase(tables);
}

// --- the keepers -------------------------------------------------------------------------------

alignas(16) uint8_t g_kpMem[sizeof(C_CameraFrameKeeper)];
C_CameraFrameKeeper *KpNew(C_Actor *owner, I3D_frame *frame)
{
    std::memset(g_kpMem, 0xCD, sizeof g_kpMem);
    return new (g_kpMem) C_CameraFrameKeeper(owner, frame);
}
bool KpQuat(const S_quat &q, float s, float x, float y, float z)
{
    return q.s == s && q.v.x == x && q.v.y == y && q.v.z == z;
}
std::string KpSetDir(const char *name, float x, float y, float z, float roll)
{
    // The direction given y -0.3, normalized with (z z + x x) + 0.09.
    y = -0.3f;
    const float sq = (z * z + x * x) + 0.09f;
    if (std::fabs(sq - 1.0f) >= 1e-8f) {
        if (sq >= 1e-8f) {
            const float k = 1.0f / std::sqrt(sq);
            x *= k;
            y *= k;
            z *= k;
        } else if (x != 0.0f) {
            x = x >= 0.0f ? 1.0f : -1.0f;
        } else if (z != 0.0f) {
            z = z >= 0.0f ? 1.0f : -1.0f;
        } else {
            y = -1.0f;
        }
    }
    return CmText("setdir %s %.5f %.5f %.5f %.6f", name, x, y, z, roll);
}

void KeeperTest(S_CameraResults &r)
{
    C_game *game = GetGame();
    g_sphereTestHook = &CmSphereHook;
    g_sphereHookSetsHit = true;
    g_gfActive = nullptr;

    // --- made ---
    I3D_frame *seat = GfFrame(FRAME_DUMMY, "seat");
    seat->m_localMatrix(3) = S_vector(10.0f, 1.0f, 20.0f);
    I3D_frame *dummy = GfFrame(FRAME_DUMMY, "camera3RD", seat);
    dummy->m_localMatrix(3) = S_vector(0.0f, 2.5f, -4.0f);
    dummy->m_localMatrix.m[3][3] = 0.5f;
    dummy->m_rot = S_quat(0.6f, 0.8f, 0.0f, 0.0f);
    alignas(16) static uint8_t ownerMem[sizeof(C_Human)];
    C_Actor *owner = reinterpret_cast<C_Actor *>(ownerMem);
    owner->m_type = ACTOR_HUMAN;
    {
        C_CameraFrameKeeper *k = KpNew(owner, nullptr);
        WM(r.kpMake, !k->m_frame && k->m_owner == owner && !k->m_cut);
        g_gfLog.clear();
        g_cmSpheres.clear();
        WM(r.kpMake, !k->Restore() && !k->Update(5) && g_gfLog.empty() && g_cmSpheres.empty() && !k->m_cut);
        k->~C_CameraFrameKeeper();
        WM(r.kpMake, g_gfLog.empty());
        k = KpNew(nullptr, dummy);
        g_gfLog.clear();
        WM(r.kpMake, k->m_frame == dummy && !k->m_owner && !k->m_cut && !k->Update(5) && g_gfLog.empty() &&
                         g_cmSpheres.empty());
        WM(r.kpMake, KpQuat(k->m_rot, 0.6f, 0.8f, 0.0f, 0.0f) && CmVec4(k->m_localPos, 0.0f, 2.5f, -4.0f, 0.5f) &&
                         CmVec4(k->m_parentPos, 10.0f, 1.0f, 20.0f, 1.0f) && CmVec4(k->m_worldPos, 5.0f, 3.0f, 6.0f, 0.5f));
        // The rotation from the local matrix.
        I3D_frame *d2 = GfFrame(FRAME_DUMMY, "camera3RD2", seat);
        d2->m_localMatrix.m[0][0] = 0.0f;
        d2->m_localMatrix.m[0][1] = 1.0f;
        d2->m_localMatrix.m[1][0] = -1.0f;
        d2->m_localMatrix.m[1][1] = 0.0f;
        d2->m_rot = S_quat(0.5f, 0.5f, 0.5f, 0.5f);
        d2->m_frameFlags &= ~FRMFLAGS_ROT_DIRTY;
        S_quat made;
        made.Make(d2->m_localMatrix);
        k = KpNew(owner, d2);
        WM(r.kpMake, KpQuat(k->m_rot, made.s, made.v.x, made.v.y, made.v.z) && (d2->m_frameFlags & FRMFLAGS_ROT_DIRTY) &&
                         CmVec4(k->m_localPos, 0.0f, 0.0f, 0.0f, 1.0f) && CmVec4(k->m_worldPos, 10.0f, 1.0f, 20.0f, 1.0f));
    }

    // --- restored ---
    {
        C_CameraFrameKeeper *k = KpNew(owner, dummy);
        seat->m_localMatrix(3) = S_vector(12.0f, 1.0f, 20.0f);
        dummy->m_localMatrix(3) = S_vector(1.0f, 1.0f, 1.0f);
        dummy->m_localMatrix.m[3][3] = 0.25f;
        dummy->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
        dummy->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_BOUND_VALID | 0x40u;
        g_gfLog.clear();
        WM(r.kpRestore, k->Restore() && GfLogIs({"update camera3RD"}) && k->m_cut);
        S_quat rot = S_quat(0.6f, 0.8f, 0.0f, 0.0f);
        rot.Normalize();
        WM(r.kpRestore, dummy->m_localMatrix(3) == S_vector(0.0f, 2.5f, -4.0f) && dummy->m_localMatrix.m[3][3] == 0.25f &&
                            KpQuat(dummy->m_rot, rot.s, rot.v.x, rot.v.y, rot.v.z) &&
                            dummy->m_frameFlags == (FRMFLAGS_CHANGED | 0x48u));
        WM(r.kpRestore, CmVec4(k->m_parentPos, 12.0f, 1.0f, 20.0f, 1.0f) && CmVec4(k->m_worldPos, 3.0f, 2.75f, 1.0f, 0.25f) &&
                            CmVec4(k->m_localPos, 0.0f, 2.5f, -4.0f, 0.5f));
        // A locked position stays; the rotation is put back (normalized).
        dummy->m_localMatrix(3) = S_vector(1.0f, 1.0f, 1.0f);
        dummy->m_frameFlags |= FRMFLAGS_POS_LOCKED;
        k->m_rot = S_quat(0.0f, 0.0f, 2.0f, 0.0f);
        k->m_cut = false;
        g_gfLog.clear();
        WM(r.kpRestore, k->Restore() && dummy->m_localMatrix(3) == S_vector(1.0f, 1.0f, 1.0f) &&
                            KpQuat(dummy->m_rot, 0.0f, 0.0f, 1.0f, 0.0f) && k->m_cut && GfLogIs({"update camera3RD"}));
        // The destructor restores.
        dummy->m_frameFlags &= ~FRMFLAGS_POS_LOCKED;
        k->m_rot = S_quat(0.6f, 0.8f, 0.0f, 0.0f);
        g_gfLog.clear();
        k->~C_CameraFrameKeeper();
        WM(r.kpRestore, GfLogIs({"update camera3RD"}) && dummy->m_localMatrix(3) == S_vector(0.0f, 2.5f, -4.0f));
        dummy->m_frameFlags = FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY;
        dummy->m_localMatrix.m[3][3] = 1.0f;
        seat->m_localMatrix(3) = S_vector(10.0f, 1.0f, 20.0f);
    }

    // --- updated: over the parent, on the ground; looking down ---
    {
        C_CameraFrameKeeper *k = KpNew(owner, dummy);
        g_vehicleCameraSpeed = 77.0f;
        g_cmHit = false;
        g_cmSpheres.clear();
        g_gfLog.clear();
        WM(r.kpUpdate, k->Update(5));
        WM(r.kpUpdate, GfVecIs(g_gfLog, {"setpos camera3RD 10.0000 3.5000 20.0000",
                                         "setdir camera3RD 0.00000 -1.00000 0.00000 0.000000", "update camera3RD"}));
        WM(r.kpUpdate, g_cmSpheres.size() == 1 && g_cmSpheres[0].from == S_vector(10.0f, 1.0f, 20.0f) &&
                           g_cmSpheres[0].dir == S_vector(0.0f, 2.5f, 0.0f) && g_cmSpheres[0].radius == 0.3f &&
                           g_cmSpheres[0].types == 0xFFFFFFFFu && g_cmSpheres[0].flags == 0 && g_vehicleCameraSpeed == 77.0f);
        g_cmHit = true;
        g_sphereHookHit = S_vector(-5.0f, 0.75f, -5.0f);
        dummy->m_localMatrix(3) = S_vector(0.0f, 2.5f, -4.0f);
        g_gfLog.clear();
        WM(r.kpUpdate, k->Update(5) && GfVecIs(g_gfLog, {"setpos camera3RD 10.0000 0.7500 20.0000",
                                                         "setdir camera3RD 0.00000 -1.00000 0.00000 0.000000",
                                                         "update camera3RD"}));
        g_cmHit = false;
        // Another owner's speed is 3 (the active soldier's vehicle).
        alignas(16) static uint8_t playerMem[sizeof(C_Human)];
        C_Human *player = reinterpret_cast<C_Human *>(playerMem);
        player->vehicle = owner;
        g_gfActive = player;
        g_gfLog.clear();
        WM(r.kpPlayer, k->Update(5) && g_vehicleCameraSpeed == 3.0f);
        player->vehicle = nullptr;
        g_vehicleCameraSpeed = 77.0f;
        WM(r.kpPlayer, k->Update(5) && g_vehicleCameraSpeed == 77.0f);
        // After a restore: 1000 once.
        player->vehicle = owner;
        k->m_cut = true;
        WM(r.kpPlayer, k->Update(5) && g_vehicleCameraSpeed == 1000.0f && !k->m_cut);
        WM(r.kpPlayer, k->Update(5) && g_vehicleCameraSpeed == 3.0f);
        g_gfActive = nullptr;
        k->m_cut = true;
        g_vehicleCameraSpeed = 77.0f;
        WM(r.kpPlayer, k->Update(5) && g_vehicleCameraSpeed == 77.0f && !k->m_cut);
        player->vehicle = nullptr;
        dummy->m_localMatrix(3) = S_vector(0.0f, 2.5f, -4.0f);
    }

    // --- a car: along its heading and velocity ---
    {
        alignas(16) static uint8_t carMem[sizeof(C_Car)];
        C_Car *car = reinterpret_cast<C_Car *>(carMem);
        car->m_type = ACTOR_CAR;
        car->m_direction = {0.6f, 0.3f, 0.8f, 0.0f};
        car->m_vehicle.m_velocity = {2.0f, 0.5f, -1.0f, 0.0f};
        car->m_vehicle.m_speed = 5.0f;
        alignas(16) static uint8_t playerMem[sizeof(C_Human)];
        C_Human *player = reinterpret_cast<C_Human *>(playerMem);
        player->vehicle = car;
        g_gfActive = player;
        C_CameraFrameKeeper *k = KpNew(car, dummy);
        float x = 0.6f * 0.5f + 2.0f, z = 0.8f * 0.5f + -1.0f;
        {
            const float k2 = 1.0f / std::sqrt(z * z + x * x);
            x *= k2;
            z *= k2;
        }
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && GfVecIs(g_gfLog, {"setpos camera3RD 10.0000 3.5000 20.0000",
                                                      KpSetDir("camera3RD", x, 0.0f, z, 0.0f), "update camera3RD"}));
        WM(r.kpCar, g_vehicleCameraSpeed == (5.0f + 2.5f) * 0.3f);
        // A short heading: the unit axis.
        car->m_direction = {0.0f, 0.3f, 0.0f, 0.0f};
        car->m_vehicle.m_velocity = {0.0f, 0.5f, -1e-5f, 0.0f};
        car->m_vehicle.m_speed = -2.0f;
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3 && g_gfLog[1] == KpSetDir("camera3RD", 0.0f, 0.0f, -1.0f, 0.0f) &&
                        g_vehicleCameraSpeed == (-2.0f + 2.5f) * 0.3f);
        car->m_direction = {0.0f, 0.3f, 0.0f, 0.0f};
        car->m_vehicle.m_velocity = {0.0f, 0.5f, 0.0f, 0.0f};
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3 && g_gfLog[1] == KpSetDir("camera3RD", 0.0f, 0.0f, 0.0f, 0.0f));
        // Short along +x, along +z: the axis; squared exactly 1e-8: normalized.
        car->m_direction = {0.0f, 0.0f, 0.0f, 0.0f};
        car->m_vehicle.m_velocity = {1e-5f, 0.0f, 0.0f, 0.0f};
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3 && g_gfLog[1] == KpSetDir("camera3RD", 1.0f, 0.0f, 0.0f, 0.0f));
        car->m_vehicle.m_velocity = {0.0f, 0.0f, 1e-5f, 0.0f};
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3 && g_gfLog[1] == KpSetDir("camera3RD", 0.0f, 0.0f, 1.0f, 0.0f));
        {
            const float x = 0x1.f75106p-15f, z = 0x1.4f8b58p-14f;   // (about 6e-5, 8e-5)
            car->m_vehicle.m_velocity = {x, 0.0f, z, 0.0f};
            const float sq = z * z + x * x;
            const float k2 = 1.0f / std::sqrt(sq);
            g_gfLog.clear();
            WM(r.kpCar, sq == 1e-8f && k->Update(5) && g_gfLog.size() == 3 &&
                            g_gfLog[1] == KpSetDir("camera3RD", x * k2, 0.0f, z * k2, 0.0f));
        }
        // Already looking along it, y 0.3: not turned.
        car->m_direction = {0.0f, 0.0f, 1.0f, 0.0f};
        car->m_vehicle.m_velocity = {0.0f, 0.0f, 0.5f, 0.0f};
        dummy->m_localMatrix.m[2][0] = 0.0f;
        dummy->m_localMatrix.m[2][1] = 0.3f;
        dummy->m_localMatrix.m[2][2] = 0.9539392f;
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && GfVecIs(g_gfLog, {"setpos camera3RD 10.0000 3.5000 20.0000", "update camera3RD"}));
        // ... but turned with y 0.31 or away from it.
        dummy->m_localMatrix.m[2][1] = 0.31f;
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3);
        dummy->m_localMatrix.m[2][1] = 0.3f;
        dummy->m_localMatrix.m[2][0] = 0.05f;
        g_gfLog.clear();
        WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3 && g_gfLog[1] == KpSetDir("camera3RD", 0.0f, 0.0f, 1.0f, 0.0f));
        // A view off the axes (its squared length 1, kept): along it exactly, not turned; 0.1
        // radians away (the dot 0.995), turned.
        {
            float vz = 0.9486833f;
            for (int i = 0; i < 400 && std::fabs((0.3f * 0.3f + vz * vz) + 0.1f * 0.1f - 1.0f) >= 1e-8f; ++i)
                vz = std::nextafter(vz, (0.3f * 0.3f + vz * vz) + 0.1f * 0.1f > 1.0f ? 0.0f : 2.0f);
            dummy->m_localMatrix.m[2][0] = 0.1f;
            dummy->m_localMatrix.m[2][1] = 0.3f;
            dummy->m_localMatrix.m[2][2] = vz;
            car->m_direction = {0.0f, 0.0f, 0.0f, 0.0f};
            car->m_vehicle.m_velocity = {0.1f, 0.0f, vz, 0.0f};
            g_gfLog.clear();
            WM(r.kpCar, std::fabs((0.3f * 0.3f + vz * vz) + 0.1f * 0.1f - 1.0f) < 1e-8f && k->Update(5) &&
                            g_gfLog.size() == 2);
            const float c = std::cos(0.1f), s = std::sin(0.1f);
            car->m_vehicle.m_velocity = {0.1f * c + vz * s, 0.0f, vz * c - 0.1f * s, 0.0f};
            g_gfLog.clear();
            WM(r.kpCar, k->Update(5) && g_gfLog.size() == 3);
        }
        dummy->m_localMatrix.m[2][0] = 0.0f;
        dummy->m_localMatrix.m[2][1] = 0.0f;
        dummy->m_localMatrix.m[2][2] = 1.0f;
        dummy->m_localMatrix(3) = S_vector(0.0f, 2.5f, -4.0f);
        g_gfActive = nullptr;
        player->vehicle = nullptr;
    }

    // --- a submarine: lowered with its bobbing, rolled to and fro ---
    {
        alignas(16) static uint8_t subMem[sizeof(C_Submarine)];
        C_Submarine *sub = reinterpret_cast<C_Submarine *>(subMem);
        sub->m_type = ACTOR_SUBMARINE;
        sub->m_direction = {0.0f, 0.2f, 1.0f, 0.0f};
        sub->m_speed = 2.0f;
        sub->m_bob = 0.5f;
        alignas(16) static uint8_t playerMem[sizeof(C_Human)];
        C_Human *player = reinterpret_cast<C_Human *>(playerMem);
        player->vehicle = sub;
        g_gfActive = player;
        game->m_time = 1234;
        C_CameraFrameKeeper *k = KpNew(sub, dummy);
        const float t = static_cast<float>(static_cast<double>(1234u) * 0.001f);
        const float roll = static_cast<float>(std::sin(static_cast<double>(t)) * 0.01884955660992227);
        g_gfLog.clear();
        WM(r.kpSubmarine, k->Update(5) && GfVecIs(g_gfLog, {"setpos camera3RD 10.0000 3.5000 20.0000",
                                                            KpSetDir("camera3RD", 0.0f, 0.0f, 2.5f, roll),
                                                            "update camera3RD"}));
        WM(r.kpSubmarine, dummy->m_localMatrix(3) == S_vector(10.0f, 2.5f - 0.5f * 0.4f, 20.0f) &&
                              (dummy->m_frameFlags & FRMFLAGS_CHANGED) && g_vehicleCameraSpeed == std::fabs(2.5f * 0.025f));
        // Slow: at least 0.03; the time's roll.
        sub->m_speed = -0.8f;
        sub->m_bob = -1.0f;
        game->m_time = 4000;
        const float t2 = static_cast<float>(static_cast<double>(4000u) * 0.001f);
        const float roll2 = static_cast<float>(std::sin(static_cast<double>(t2)) * 0.01884955660992227);
        const float k2 = -0.8f + 0.5f;
        g_gfLog.clear();
        const bool kp = k->Update(5);
        WM(r.kpSubmarine, kp && g_gfLog.size() == 3 &&
                              g_gfLog[1] == KpSetDir("camera3RD", 0.0f * k2, 0.0f, k2, roll2) &&   // (x -0)
                              g_vehicleCameraSpeed == 0.03f && dummy->m_localMatrix(3) == S_vector(10.0f, 2.9f, 20.0f));
        sub->m_speed = 0.7f;                            // 1.2 * 0.025 = 0.03
        g_gfLog.clear();
        WM(r.kpSubmarine, k->Update(5) && g_vehicleCameraSpeed == std::fabs(1.2f * 0.025f));
        // Diving along the view (y 0.3): the dive left out of the test, not turned.
        dummy->m_localMatrix.m[2][1] = 0.3f;
        dummy->m_localMatrix.m[2][2] = 0.9539392f;
        sub->m_direction = {0.0f, -0.5f, 1.0f, 0.0f};
        sub->m_speed = 0.5f;
        g_gfLog.clear();
        WM(r.kpSubmarine, k->Update(5) && g_gfLog.size() == 2);
        dummy->m_localMatrix.m[2][1] = 0.0f;
        dummy->m_localMatrix.m[2][2] = 1.0f;
        sub->m_direction = {0.0f, 0.2f, 1.0f, 0.0f};
        // A locked position: only the raise (SetPos).
        sub->m_speed = 2.0f;
        dummy->m_frameFlags |= FRMFLAGS_POS_LOCKED;
        g_gfLog.clear();
        WM(r.kpSubmarine, k->Update(5) && dummy->m_localMatrix(3) == S_vector(10.0f, 3.5f, 20.0f));
        dummy->m_frameFlags &= ~FRMFLAGS_POS_LOCKED;
        WM(r.kpSubmarine, (dummy->m_frameFlags & FRMFLAGS_POS_LOCKED) == 0);
        game->m_time = 0;
        g_gfActive = nullptr;
        player->vehicle = nullptr;
    }

    g_sphereTestHook = nullptr;
    g_sphereHookSetsHit = false;
}

}  // namespace

// (Faked: the update, part 8e-iii; the human's effect value.)
void C_CameraModule::Update(int timeDelta) { GfLog("camupdate %d", timeDelta); }
void C_Human::SetUnknown94(float value) { GfLog("unknown94 %.1f", value); }

void CameraTest(const char *path)
{
    if (const char *data = std::getenv("WORLD_DATA"))
        g_wmData = data;
    g_wmTrace = std::getenv("CAR_TRACE") != nullptr;
    std::string mission = "-";
    if (FILE *f = std::fopen(path, "r")) {
        char line[256] = {};
        if (std::fgets(line, sizeof line, f)) {
            char name[200] = {};
            if (std::sscanf(line, "mission %199s", name) == 1)
                mission = name;
        }
        std::fclose(f);
    }
    static bool once;
    if (!once) {
        once = true;
        CmInitVtables();
    }
    GetGame()->m_scene = g_carScene;
    g_gameRole = GAME_ROLE_LOCAL;
    std::printf("== %s\n", mission.c_str());
    S_CameraResults r;
    CameraModuleTest(r);
    KeeperTest(r);
    std::printf("camera: cmMake %d cmGetters %d cmSaveLoad %d cmRestart %d cmStart %d cmStartNone %d cmSettings %d "
                "cmSettingsFile %d cmStop %d cmPos %d cmDir %d cmBlend %d cmTarget %d cmFov %d cmViewDir %d\n",
                r.cmMake, r.cmGetters, r.cmSaveLoad, r.cmRestart, r.cmStart, r.cmStartNone, r.cmSettings,
                r.cmSettingsFile, r.cmStop, r.cmPos, r.cmDir, r.cmBlend, r.cmTarget, r.cmFov, r.cmViewDir);
    std::printf("keeper: kpMake %d kpRestore %d kpUpdate %d kpCar %d kpSubmarine %d kpPlayer %d\n", r.kpMake,
                r.kpRestore, r.kpUpdate, r.kpCar, r.kpSubmarine, r.kpPlayer);
}
