// Part 8e-iv's camera placement and move, the view's choice and the special modes
// (game/camera_module.cpp: PlaceCamera, MoveCamera, SelectView, SetSpecialMode), in the camera
// build after update_test.inc: the first-person view's Show, the camera's water flag, the team's
// ReturnControl, the ray test (sight_test.inc's hook) and the scene's FindFrame faked here; the
// sphere test, the frames' slots and the quaternions as the earlier tests fake them. Prints
// "place:" with 1 per check met.


void C_FirstPersonView::Show(bool on)
{
    GfLog("fpvshow %d", on);
    m_on = on;
}
void C_FirstPersonView::ShowBinoculars(bool on)
{
    m_binoculars = on;
}
// The first-person view (shown) and the binoculars set, not logged.
void PlFpv(C_FirstPersonView *fpv, bool on, bool binoculars)
{
    fpv->Show(on);
    fpv->ShowBinoculars(binoculars);
    g_gfLog.pop_back();
}
bool g_plCameraUnderWater;
void C_HumanMovement::SetCameraUnderWater(bool on)
{
    GfLog("camwater %d", on);
    g_plCameraUnderWater = on;
}
void C_Team::ReturnControl()
{
    GfLog("returncontrol");
}

namespace {

// The ray test: what it was asked; a hit (g_plRayVolume) at g_plRayHit with g_plRayNormal.
struct S_PlRay {
    S_vector from, dir;
    uint32_t types, flags;
};
std::vector<S_PlRay> g_plRays;
bool g_plRayHitOn;
S_vector g_plRayHit, g_plRayNormal;
S_CollisionVolume g_plRayVolume;
S_CollisionVolume *PlRay(const S_vector &from, const S_vector &dir, S_vector *hitPos, S_vector *hitNormal, uint32_t types,
                         uint32_t flags)
{
    g_plRays.push_back({from, dir, types, flags});
    if (!g_plRayHitOn)
        return nullptr;
    *hitPos = g_plRayHit;
    *hitNormal = g_plRayNormal;
    return &g_plRayVolume;
}
// The sphere test: what it was asked; a hit (g_plSphereVolume) at g_plSphereHit when set.
struct S_PlSphere {
    S_vector from, dir;
    float radius;
    uint32_t types, flags;
};
std::vector<S_PlSphere> g_plSpheres;
bool g_plSphereHitOn;
S_CollisionVolume g_plSphereVolume;
S_CollisionVolume *PlSphere(const S_vector &from, const S_vector &dir, float radius, uint32_t types, uint32_t flags)
{
    g_plSpheres.push_back({from, dir, radius, types, flags});
    return g_plSphereHitOn ? &g_plSphereVolume : nullptr;
}
// The scene's frames by name (FindFrame, 0x7C).
std::map<std::string, I3D_frame *> g_plScene;
I3D_frame *__stdcall F_PlFind(void *, const char *name, uint32_t mask)
{
    GfLog("sfind %s %x", name, mask);
    const auto it = g_plScene.find(name);
    return it == g_plScene.end() ? nullptr : it->second;
}

bool PlVec(const S_vector &a, const S_vector &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool PlVec4(const S_vector4 &a, const S_vector &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// A module to place: the game camera "game" (the scene's active camera), the camera dummy
// "dummy" at (1, 2, 3) looking along +z, view 1 with its settings (distance 3, collide off).
C_CameraModule *PlNew()
{
    C_CameraModule *m = CmNew();
    I3D_frame *game = CmCamera("game", S_vector(0.0f, 0.0f, 0.0f));
    m->m_gameCamera = reinterpret_cast<I3D_camera *>(game);
    I3D_frame *dummy = GfFrame(FRAME_DUMMY, "dummy");
    dummy->m_localMatrix(3) = S_vector(1.0f, 2.0f, 3.0f);
    m->m_camera = dummy;
    m->m_view = 1;
    m->m_viewSettings = &m->m_settings[1];
    m->m_settings[1].flags = 0;
    m->m_wantedDir = {0.6f, 0.0f, 0.8f, 0.5f};
    m->m_dir = {0.6f, 0.0f, 0.8f, 0.25f};
    CmActive() = game;
    g_gfLog.clear();
    g_tcSlerps.clear();
    g_tcQuatDirs.clear();
    g_plSpheres.clear();
    g_plRays.clear();
    g_tcSlerpResult = S_quat(0.5f, 0.5f, -0.5f, 0.5f);
    return m;
}

// The look PlaceCamera turns to (the slerp's result direction, normalized as it does).
S_vector PlLook()
{
    return g_tcSlerpResult.GetDir().NormalizedGame();
}

// The collide path's module: PlNew's, view 1's settings distance 4, radius 0.2, speed 12, the
// distance's band 0.5 .. 1, the offsets 0.5 and -0.25 (the sweep from (1.5, 2, 2.75)).
C_CameraModule *PlSweepNew(float distance)
{
    C_CameraModule *m = PlNew();
    S_CameraSettings &s = m->m_settings[1];
    s.flags = CAMERA_SETTINGS_COLLIDE;
    s.distance = 4.0f;
    s.radius = 0.2f;
    s.speed = 12.0f;
    s.unknown04 = 0.5f;
    s.unknown08 = 1.0f;
    m->m_unknown050 = 0.5f;
    m->m_unknown058 = -0.25f;
    m->m_distance = distance;
    m->m_hit = nullptr;
    g_plSphereHitOn = g_plRayHitOn = false;
    g_plSphereVolume = S_CollisionVolume();
    g_plRayVolume = S_CollisionVolume();
    g_sgLog.clear();
    return m;
}
const float PL_RATE = 16.0f * 12.0f * 1.0f * 0.001f;   // (the distance's step in 16 ms)
// The camera m_distance behind (1.5, 2, 2.75) along m_dir.
bool PlBehind(const C_CameraModule *m)
{
    const S_vector4 &d = m->m_dir;
    const float k = m->m_distance;
    return m->m_pos.x == 1.5f - k * d.x && m->m_pos.y == (2.0f + m->m_waterOffset) - k * d.y &&
           m->m_pos.z == 2.75f - k * d.z;
}
// The sphere test asked once from (1.5, 2, 2.75) back along the look by distance + 1 m.
bool PlSwept(const S_vector &from, float distance, uint32_t types)
{
    const S_vector look = PlLook();
    const float k = distance + 1.0f;
    return g_plSpheres.size() == 1 && PlVec(g_plSpheres[0].from, from) &&
           PlVec(g_plSpheres[0].dir, S_vector(-look.x * k, -look.y * k, -look.z * k)) && g_plSpheres[0].radius == 0.2f &&
           g_plSpheres[0].types == types && g_plSpheres[0].flags == 0x200000;
}

void PlSweepTest(S_CameraResults &r, C_Human &player, C_Actor *car, C_Actor *junkers)
{
    const S_vector start(1.5f, 2.0f, 2.75f);
    // --- nothing in the way: the distance eases out (and in), the ray asked too (not in a car) ---
    {
        g_gfActive = &player;
        player.vehicle = nullptr;
        C_CameraModule *m = PlSweepNew(1.0f);
        m->m_hit = &g_plRayVolume;
        m->PlaceCamera(16);
        WM(r.plSweep, PlSwept(start, 4.0f, 0x98000) && g_plRays.size() == 1 && PlVec(g_plRays[0].from, start) &&
                          PlVec(g_plRays[0].dir, g_plSpheres[0].dir) && g_plRays[0].types == 0x98000 &&
                          g_plRays[0].flags == 0x200000 && m->m_distance == 1.0f + PL_RATE && !m->m_hit &&
                          PlVec4(m->m_dir, PlLook()) && PlBehind(m) && g_sgLog.size() == 2 &&
                          g_sgLog[0].compare(0, 7, "nocoll ") == 0 && g_sgLog[0].back() == '0' &&
                          g_sgLog[1].compare(0, 5, "coll ") == 0 && g_sgLog[1].back() == '0');
        m = PlSweepNew(3.9f);
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 4.0f);
        m = PlSweepNew(4.0f);
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 4.0f);
        m = PlSweepNew(4.1f);
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 4.1f - PL_RATE);
        m = PlSweepNew(0.6f);
        m->m_settings[1].distance = 0.05f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.6f - PL_RATE);
        m = PlSweepNew(0.1f);
        m->m_settings[1].distance = 0.05f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.0f && PlSwept(start, 0.05f, 0x98000));
        // No soldier: no collision taken off; in a car: its type kept out, no ray; the Junkers:
        // the ray.
        g_gfActive = nullptr;
        m = PlSweepNew(1.0f);
        m->PlaceCamera(16);
        WM(r.plSweep, g_sgLog.empty() && g_plRays.size() == 1 && PlSwept(start, 4.0f, 0x98000));
        g_gfActive = &player;
        player.vehicle = car;
        m = PlSweepNew(1.0f);
        m->PlaceCamera(16);
        WM(r.plSweep, g_plRays.empty() && PlSwept(start, 4.0f, 0x98002) && m->m_distance == 1.0f + PL_RATE);
        player.vehicle = junkers;
        m = PlSweepNew(1.0f);
        m->PlaceCamera(16);
        WM(r.plSweep, g_plRays.size() == 1 && PlSwept(start, 4.0f, 0x98000));
        player.vehicle = nullptr;
    }

    // --- the ray's hit: held off its surface by the radius + 0.05 ---
    {
        C_CameraModule *m = PlSweepNew(2.0f);
        g_plRayHitOn = true;
        g_plRayHit = S_vector(1.5f, 1.75f, 1.5f);
        g_plRayNormal = S_vector(0.0f, 0.0f, 1.0f);
        const S_vector4 oldDir = m->m_dir;
        m->PlaceCamera(16);
        // (1.5, 1.75, 1.75): 0.25 down, 1 back: the free distance sqrt(1.0625).
        const float free = std::sqrt((1.0f * 1.0f + 0.25f * 0.25f) + 0.0f);
        WM(r.plSweep, m->m_distance == 2.0f - PL_RATE && !m->m_hit && CmVec4(m->m_dir, oldDir.x, oldDir.y, oldDir.z, oldDir.w) &&
                          free < 2.0f - PL_RATE);
        g_plRayNormal = S_vector(0.6f, 0.0f, 0.8f);
        m = PlSweepNew(5.0f);
        g_plRayHitOn = true;
        m->m_hit = &g_plSphereVolume;
        m->PlaceCamera(16);
        const float off = 0.2f + 0.05f;
        const S_vector at(0.6f * off + 1.5f, 1.75f + 0.0f * off, 1.5f + 0.8f * off);
        const S_vector gap(1.5f - at.x, 2.0f - at.y, 2.75f - at.z);
        WM(r.plSweep, m->m_distance == std::sqrt((gap.z * gap.z + gap.y * gap.y) + gap.x * gap.x) && m->m_hit == &g_plSphereVolume);
    }

    // --- the sphere's hit (0.75 m back) ---
    {
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.0f);
        struct S_Case {
            float before;
            bool held;
            float distance, after;
            bool hit, dir;
        };
        const S_Case cases[] = {
            {0.5f, false, 4.0f, 0.5f, true, true},              // at the band's bottom: held there
            {0.3f, false, 4.0f, 0.3f + PL_RATE, true, true},    // nearer: out towards it
            {0.7f, false, 4.0f, 0.75f, true, true},             // ... up to it
            {0.55f, false, 0.6f, 0.6f, true, true},             // (the view's distance first)
            {2.0f, true, 4.0f, 0.75f, true, true},              // held: at it at once
            {2.0f, false, 4.0f, 2.0f - PL_RATE, false, false},  // farther: in towards it
            {0.76f, false, 4.0f, 0.75f - 0.01f, false, false},  // ... to 1 cm before it
            {0.93f, false, 4.0f, 0.75f - 0.01f, false, false},
            {0.95f, false, 4.0f, 0.95f - PL_RATE, false, false},
            {0.75f, false, 4.0f, 0.75f - 0.01f, false, false},
        };
        for (const S_Case &c : cases) {
            C_CameraModule *m = PlSweepNew(c.before);
            m->m_settings[1].distance = c.distance;
            g_plSphereHitOn = true;
            if (c.held)
                m->m_hit = &g_plRayVolume;
            const S_vector4 oldDir = m->m_dir;
            m->PlaceCamera(16);
            S_CollisionVolume *const hit = c.held ? &g_plRayVolume : c.hit ? &g_plSphereVolume : nullptr;
            WM(r.plSweep, m->m_distance == c.after && m->m_hit == hit && g_plRays.empty() &&
                              (c.dir ? PlVec4(m->m_dir, PlLook()) : CmVec4(m->m_dir, oldDir.x, oldDir.y, oldDir.z, oldDir.w)) &&
                              PlBehind(m));
        }
        // Nearer than the band: held by it (not when the step stays in the band).
        const auto freeAt = [](float z) {
            const float gap = 2.75f - z;
            return std::sqrt((gap * gap + 0.0f) + 0.0f);
        };
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.45f);
        C_CameraModule *m = PlSweepNew(0.45f);
        g_plSphereHitOn = true;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == freeAt(2.45f) - 0.01f && m->m_hit == &g_plSphereVolume);
        m = PlSweepNew(0.7f);
        g_plSphereHitOn = true;
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.15f);
        m->PlaceCamera(16);
        WM(r.plSweep, !m->m_hit && m->m_distance == freeAt(2.15f) - 0.01f);
        // Within the radius + 0.05: no room (0).
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.55f);
        m = PlSweepNew(0.1f);
        g_plSphereHitOn = true;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.0f - 0.01f && m->m_hit == &g_plSphereVolume);
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.5f);
        m = PlSweepNew(0.1f);
        g_plSphereHitOn = true;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == freeAt(2.5f) && freeAt(2.5f) == 0.25f);
        // The view's distance below the hit's.
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.0f);
        m = PlSweepNew(0.3f);
        m->m_settings[1].distance = 0.4f;
        g_plSphereHitOn = true;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.4f);
        // Under water: 1 m less below the surface, more above it; the band not kept.
        m = PlSweepNew(0.5f);
        g_plSphereHitOn = true;
        m->m_underwater = true;
        m->m_waterLevel = 9.0f;
        m->m_view = 2;                                  // (no push)
        m->m_viewSettings = &m->m_settings[2];
        m->m_settings[2] = m->m_settings[1];
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.5f - PL_RATE && !m->m_hit);   // (-0.25: no room)
        m->m_distance = 0.5f;
        m->m_waterLevel = -9.0f;
        m->m_settings[2].distance = 1.5f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.5f + PL_RATE && m->m_hit == &g_plSphereVolume);
        m->m_distance = 1.4f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 1.5f);
        g_sphereHookHit = S_vector(1.5f, 2.0f, 0.75f);  // (2 m back: the view's 1.5 - 1)
        m->m_distance = 0.45f;
        m->m_hit = nullptr;
        m->m_waterLevel = 9.0f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 1.5f - 1.0f && m->m_hit == &g_plSphereVolume);
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.0f);
        m->m_distance = 0.3f;
        m->m_waterLevel = 2.0f;                         // (at the surface: above it)
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == 0.3f + PL_RATE);
        // The water's push (0.3 m past the band) in the other views.
        m = PlSweepNew(1.0f);
        m->m_underwater = true;
        m->m_waterLevel = 2.1f;
        m->PlaceCamera(16);
        const float push = ((2.1f - 0.3f) - 0.2f) - 2.0f;
        WM(r.plSweep, m->m_waterOffset == push && PlVec(g_plSpheres[0].from, S_vector(1.5f, 2.0f + push, 2.75f)) && PlBehind(m));
        m->m_waterLevel = 1.9f;
        g_plSpheres.clear();
        m->PlaceCamera(16);
        const float up = ((0.2f + 1.9f) + 0.3f) - 2.0f;
        WM(r.plSweep, m->m_waterOffset == up && PlVec(g_plSpheres[0].from, S_vector(1.5f, 2.0f + up, 2.75f)));
        m->m_waterLevel = 1.5f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_waterOffset == 0.0f);
        // At the band's edges: no push (0.3 m past them otherwise).
        const float edgeBelow = (2.5f - 0.1f) - 0.2f, edgeAbove = 0.1f + (0.2f + 1.5f);
        m->m_waterLevel = 2.5f;
        m->m_camera->m_localMatrix(3).y = edgeBelow;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_waterOffset == 0.0f);
        m->m_waterLevel = 1.5f;
        m->m_camera->m_localMatrix(3).y = edgeAbove;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_waterOffset == 0.0f);
        m->m_camera->m_localMatrix(3).y = 2.0f;
        // (Not in view 2: kept.)
        m->m_waterLevel = 2.1f;
        m->m_view = 2;
        m->m_viewSettings = &m->m_settings[2];
        m->m_settings[2] = m->m_settings[1];
        m->m_waterOffset = 0.5f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_waterOffset == 0.5f);
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
        m->m_underwater = false;
        m->m_waterOffset = 5.0f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_waterOffset == 0.0f);
    }

    // --- the camera dummy's own model in the way (a moving volume whose frame's name begins the
    // dummy's): ignored ---
    {
        g_sphereHookHit = S_vector(1.5f, 2.0f, 2.0f);
        I3D_frame *own = GfFrame(FRAME_MODEL, "dum");
        I3D_frame *other = GfFrame(FRAME_MODEL, "dummy2");
        struct S_Case {
            uint32_t flags;
            I3D_frame *frame;
            bool ignored;
        };
        const S_Case cases[] = {{COLLISION_FLAG_DYNAMIC, own, true}, {COLLISION_FLAG_DYNAMIC, other, false},
                                {0, own, false}, {COLLISION_FLAG_DYNAMIC, nullptr, false},
                                {COLLISION_FLAG_DYNAMIC | 1, own, true}};
        for (const S_Case &c : cases) {
            C_CameraModule *m = PlSweepNew(0.5f);
            g_plSphereHitOn = true;
            g_plSphereVolume.flags = c.flags;
            g_plSphereVolume.frame = c.frame;
            m->PlaceCamera(16);
            WM(r.plSweep, c.ignored ? !m->m_hit && m->m_distance == 0.5f + PL_RATE && g_plRays.empty()
                                    : m->m_hit == &g_plSphereVolume && m->m_distance == 0.5f);
        }
    }

    // --- the turret's view (8): from its arm (the dummy's parent) back along the arm ---
    {
        C_CameraModule *m = PlSweepNew(1.0f);
        I3D_frame *arm = GfFrame(FRAME_DUMMY, "arm");
        GfPlace(arm, S_vector(4.0f, 5.0f, 6.0f));
        I3D_frame *dummy = GfFrame(FRAME_DUMMY, "dummy", arm);
        GfPlace(dummy, S_vector(0.0f, 1.2f, -1.6f));
        m->m_camera = dummy;
        m->m_view = 8;
        m->m_viewSettings = &m->m_settings[8];
        m->m_settings[8] = m->m_settings[1];
        m->PlaceCamera(16);
        const float length = S_vector(0.0f, 1.2f, -1.6f).Magnitude();
        const S_vector &end = dummy->GetMatrix()(3);
        S_vector sweep(end.x - 4.0f, end.y - 5.0f, end.z - 6.0f);
        sweep = TcNorm(sweep, (sweep.z * sweep.z + sweep.y * sweep.y) + sweep.x * sweep.x);
        sweep *= length + 1.0f;
        const S_vector4 &d = m->m_dir;
        const float k = m->m_distance;
        WM(r.plSweep, g_plSpheres.size() == 1 && PlVec(g_plSpheres[0].from, S_vector(4.0f, 5.0f, 6.0f)) &&
                          PlVec(g_plSpheres[0].dir, sweep) && m->m_distance == 1.0f + PL_RATE &&
                          m->m_pos.x == (4.0f + 0.5f) - k * d.x && m->m_pos.y == 5.0f - k * d.y &&
                          m->m_pos.z == (6.0f + -0.25f) - k * d.z);
        // (The distance capped by the arm's.)
        m->m_distance = 1.99f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_distance == length);
    }

    // --- +0x278: at the dummy; the first-person view off once as far as unknown08 ---
    {
        C_FirstPersonView *fpv = GetGame()->GetFirstPersonView();
        struct S_Case {
            float distance;
            bool shown, binoculars, off;
        };
        const S_Case cases[] = {{1.0f, true, false, true}, {1.0f, false, true, true}, {1.0f, false, false, false},
                                {0.9f, true, false, false}, {1.5f, true, true, true}};
        for (const S_Case &c : cases) {
            C_CameraModule *m = PlSweepNew(c.distance);
            m->m_settings[1].speed = 0.0f;              // (the distance kept)
            m->m_unknown278 = true;
            PlFpv(fpv, c.shown, c.binoculars);
            g_gfLog.clear();
            m->PlaceCamera(16);
            WM(r.plSweep, m->m_pos.x == 1.0f + 0.5f && m->m_pos.y == 2.0f + 0.0f && m->m_pos.z == 3.0f + -0.25f &&
                              m->m_unknown278 == !c.off &&
                              GfVecIs(g_gfLog, c.off ? std::vector<std::string>{"fpvshow 0"} : std::vector<std::string>{}));
        }
        PlFpv(fpv, false, false);
        // (Under water: the push added.)
        C_CameraModule *m = PlSweepNew(1.0f);
        m->m_settings[1].speed = 0.0f;
        m->m_unknown278 = true;
        m->m_underwater = true;
        m->m_waterLevel = 2.1f;
        m->PlaceCamera(16);
        WM(r.plSweep, m->m_waterOffset != 0.0f && m->m_pos.y == 2.0f + m->m_waterOffset);
    }
    g_gfActive = nullptr;
}

// The blend (the distance 0 meanwhile) and the cut (the speed 1000 for one placement).
void PlBlendTest(S_CameraResults &r)
{
    struct S_Case {
        int before, after;
        float distance;                                 // the view's meanwhile
    };
    const S_Case cases[] = {{100, 84, 0.0f}, {16, 0, 0.0f}, {10, 0, 0.0f}, {0, 0, 4.0f}, {1, 0, 0.0f}, {17, 1, 0.0f},
                            {-15, 0, 4.0f},
                            {-100, -84, 4.0f}, {-16, 0, 4.0f}, {-10, 0, 4.0f}};
    for (const S_Case &c : cases) {
        C_CameraModule *m = PlNew();
        m->m_settings[1].distance = 4.0f;
        m->m_blendTime = c.before;
        m->PlaceCamera(16);
        const S_vector look = PlLook();
        // (Quirk kept: a negative blend time leaves the distance 0.)
        WM(r.plBlend, m->m_blendTime == c.after && m->m_settings[1].distance == (c.before < 0 ? 0.0f : 4.0f) &&
                          m->m_pos.x == 1.0f - look.x * c.distance && m->m_pos.z == 3.0f - look.z * c.distance);
    }
    // The cut: the speed 1000 meanwhile.
    C_CameraModule *m = PlSweepNew(1.0f);
    m->m_cut = true;
    m->PlaceCamera(16);
    WM(r.plBlend, m->m_distance == 4.0f && !m->m_cut && m->m_settings[1].speed == 12.0f);
    m = PlSweepNew(1.0f);
    m->m_cut = true;
    m->PlaceCamera(1);
    WM(r.plBlend, m->m_distance == 1.0f + 1.0f * 1000.0f * 1.0f * 0.001f && !m->m_cut && m->m_settings[1].speed == 12.0f);
    m = PlSweepNew(1.0f);
    m->PlaceCamera(1);
    WM(r.plBlend, m->m_distance == 1.0f + 1.0f * 12.0f * 1.0f * 0.001f && m->m_settings[1].speed == 12.0f);
}

// MoveCamera's direction while aiming (base: the direction with its shake and offset).
S_vector PlSway(S_vector base, float amplitude, int time)
{
    S_vector side(base.y * 0.0f - base.z, base.z * 0.0f - base.x * 0.0f, base.x - base.y * 0.0f);
    S_vector up(side.z * base.y - side.y * base.z, base.z * side.x - side.z * base.x, side.y * base.x - base.y * side.x);
    up = TcNorm(up, (up.z * up.z + up.y * up.y) + up.x * up.x);
    side = TcNorm(side, (side.z * side.z + side.y * side.y) + side.x * side.x);
    const double t = static_cast<double>(time);
    const float swayUp = static_cast<float>(std::sin(std::cos(static_cast<double>(static_cast<float>(t * 0.0018f)))) * amplitude);
    up *= swayUp;
    const float phase = static_cast<float>(t * 0.0008f) + 3.0f;
    const float wave = static_cast<float>(std::sin(static_cast<double>(phase)) + 1.0);
    const float swaySide = static_cast<float>(std::cos(static_cast<double>(wave)) * amplitude);
    side *= swaySide;
    base = S_vector(base.x + up.x, up.y + base.y, up.z + base.z);
    return S_vector(base.x + side.x, base.y + side.y, base.z + side.z).NormalizedGame();
}

// A module to move: PlNew's, the game camera turned a quarter about y at (10, 0, 0), the place
// (1, 2, 3) and the direction (0.6, 0, 0.8).
C_CameraModule *PlMoveNew()
{
    C_CameraModule *m = PlNew();
    I3D_frame *game = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
    S_matrix &l = game->m_localMatrix;
    l(0) = S_vector(0.0f, 0.0f, -1.0f);
    l(1) = S_vector(0.0f, 1.0f, 0.0f);
    l(2) = S_vector(1.0f, 0.0f, 0.0f);
    l(3) = S_vector(10.0f, 0.0f, 0.0f);
    m->m_pos = {1.0f, 2.0f, 3.0f, 1.0f};
    m->m_dir = {0.6f, 0.0f, 0.8f, 0.0f};
    m->m_camera = nullptr;
    g_upWorldDir = S_vector(9.0f, 9.0f, 9.0f);
    return m;
}

void PlMoveTest(S_CameraResults &r, C_Human &player)
{
    C_game *game = GetGame();
    C_FirstPersonView *fpv = game->GetFirstPersonView();
    // --- nothing without the game camera or while paused ---
    {
        C_CameraModule *m = PlMoveNew();
        m->m_shakeTime = 100;
        game->m_paused = true;
        m->MoveCamera(16);
        WM(r.mvShake, g_gfLog.empty() && m->m_shakeTime == 100);
        game->m_paused = false;
        m->m_gameCamera = nullptr;
        m->MoveCamera(16);
        WM(r.mvShake, g_gfLog.empty() && m->m_shakeTime == 100);
    }

    // --- the shake ---
    {
        C_CameraModule *m = PlMoveNew();
        m->m_shakeTime = 1000;
        m->MoveCamera(16);
        const double left = 984.0;
        const float a = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(0.07f * left))) *
                                           static_cast<double>(static_cast<float>(left * 6e-05f)));
        const float b = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.08f))) *
                                           static_cast<double>(static_cast<float>(left * 4e-05f)));
        const float c = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(left * 0.05f))) *
                                           static_cast<double>(static_cast<float>(left * 7e-05f)));
        WM(r.mvShake, m->m_shakeTime == 984 && CmVec4(m->m_shakePos, a, b, c, 0.0f) &&
                          CmVec4(m->m_shakeDir, c * 0.15f, b * -0.25f, a * 0.15f, 0.0f));
        // (Placed and turned with it.)
        const S_vector &pos = reinterpret_cast<I3D_frame *>(m->m_gameCamera)->m_localMatrix(3);
        WM(r.mvShake, PlVec(pos, S_vector((1.0f + a) + 0.0f, 0.0f + (b + 2.0f), 0.0f + (c + 3.0f))) &&
                          PlVec(g_upWorldDir, S_vector((0.6f + c * 0.15f) + 0.0f, 0.0f + (b * -0.25f + 0.0f),
                                                       0.0f + (a * 0.15f + 0.8f))));
        m->m_shakeTime = 16;
        m->m_shakePos.w = m->m_shakeDir.w = 7.0f;
        m->MoveCamera(16);
        WM(r.mvShake, m->m_shakeTime == 0 && CmVec4(m->m_shakePos, 0.0f, 0.0f, 0.0f, 7.0f) &&
                          CmVec4(m->m_shakeDir, 0.0f, -0.0f, 0.0f, 7.0f));
        m->m_shakePos.x = 5.0f;
        m->MoveCamera(16);
        WM(r.mvShake, m->m_shakeTime == 0 && m->m_shakePos.x == 5.0f);
        m->m_shakeTime = 10;
        m->m_shakePos.w = m->m_shakeDir.w = 7.0f;
        m->m_shakeDir.x = m->m_shakeDir.z = 3.0f;
        m->MoveCamera(16);
        WM(r.mvShake, m->m_shakeTime == 0 && CmVec4(m->m_shakePos, 0.0f, 0.0f, 0.0f, 7.0f) &&
                          CmVec4(m->m_shakeDir, 0.0f, 0.0f, 0.0f, 7.0f));
        // The last millisecond: over.
        m->m_shakeTime = 1;
        m->m_shakePos.x = m->m_shakeDir.x = 3.0f;
        m->MoveCamera(16);
        WM(r.mvShake, m->m_shakeTime == 0 && m->m_shakePos.x == 0.0f && m->m_shakeDir.x == 0.0f);
    }

    // --- the offsets (in the game camera's space), the turret's view not placed here ---
    {
        C_CameraModule *m = PlMoveNew();
        m->m_offsetPos = {0.5f, 0.25f, 2.0f, 0.0f};
        m->m_offsetDir = {0.125f, 0.5f, -0.25f, 0.0f};
        m->MoveCamera(16);
        // (Turned a quarter: x along -z, z along x.)
        const S_vector &pos = reinterpret_cast<I3D_frame *>(m->m_gameCamera)->m_localMatrix(3);
        const S_vector off(((0.5f * 0.0f + 0.25f * 0.0f) + 2.0f * 1.0f + 10.0f) - 10.0f,
                           ((0.5f * 0.0f + 0.25f * 1.0f) + 2.0f * 0.0f + 0.0f) - 0.0f,
                           ((0.5f * -1.0f + 0.25f * 0.0f) + 2.0f * 0.0f + 0.0f) - 0.0f);
        WM(r.mvOffsets, PlVec(pos, S_vector(1.0f + off.x, off.y + 2.0f, off.z + 3.0f)) &&
                            GfLogIs({"worlddir game " + CmText("%.5f %.5f %.5f", 0.6f + -0.25f, 0.5f, 0.8f + -0.125f) +
                                         " 0.000000",
                                     "update game"}));
        m->m_view = 8;
        m->m_pos.x = 40.0f;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvOffsets, GfLogIs({"update game"}) && PlVec(pos, S_vector(1.0f + off.x, off.y + 2.0f, off.z + 3.0f)));
        // View 9: placed and turned.
        m->m_view = 9;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvOffsets, g_gfLog.size() == 2 && pos.x == 40.0f + off.x);
        // The debug camera: neither (the fade below still).
        m->m_view = 1;
        m->m_debugCamera = true;
        m->m_pos.x = 50.0f;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvOffsets, g_gfLog.empty() && pos.x == 40.0f + off.x);
    }

    // --- the target: the scene's active camera turned to it ---
    {
        C_CameraModule *m = PlMoveNew();
        I3D_frame *active = CmCamera("active", S_vector(1.0f, 1.0f, 1.0f));
        active->m_localMatrix(2) = S_vector(0.0f, 0.0f, 2.0f);
        CmActive() = active;
        m->m_target = GfFrame(FRAME_DUMMY, "target");
        GfPlace(m->m_target, S_vector(4.0f, 5.0f, 1.0f));
        m->m_targetSpeed = 0.0f;
        m->MoveCamera(16);
        WM(r.mvTarget, GfLogIs({"setdir active 0.60000 0.80000 0.00000 0.000000", "update active", "update game"}) &&
                           g_upWorldDir.x == 9.0f && g_tcSlerps.empty());
        m->m_targetSpeed = -2.0f;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvTarget, g_gfLog.size() == 3 && g_gfLog[0] == "setdir active 0.60000 0.80000 0.00000 0.000000");
        // At a speed: a slerp's step (the whole of it at last: then at once).
        S_vector way(3.0f, 4.0f, 0.0f);
        way.Normalize();
        const S_vector view = S_vector(0.0f, 0.0f, 2.0f).NormalizedGame();
        const float angle = view.AngleTo(way);
        m->m_targetSpeed = 2.0f;
        g_gfLog.clear();
        g_tcQuatDirs.clear();
        m->MoveCamera(16);
        WM(r.mvTarget, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 16.0f * 2.0f * 0.001f / angle && !g_tcSlerps[0].shortest &&
                           g_tcQuatDirs.size() == 2 && PlVec(g_tcQuatDirs[0].dir, view) && PlVec(g_tcQuatDirs[1].dir, way) &&
                           TcVec(g_cmLastDir, PlLook()) && m->m_targetSpeed == 2.0f && g_gfLog.size() == 3);
        g_tcSlerps.clear();
        m->MoveCamera(2000);
        WM(r.mvTarget, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 1.0f && m->m_targetSpeed == -1.0f);
        // A little past the whole turn (1.15): the same; a slow speed (0.25): a step.
        m->m_targetSpeed = 2.0f;
        g_tcSlerps.clear();
        g_tcQuatDirs.clear();
        g_cmLastRoll = 5.0f;
        m->MoveCamera(900);
        WM(r.mvTarget, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 1.0f && m->m_targetSpeed == -1.0f &&
                           g_tcQuatDirs.size() == 2 && g_tcQuatDirs[0].roll == 0.0f && g_tcQuatDirs[1].roll == 0.0f &&
                           g_cmLastRoll == 0.0f);
        m->m_targetSpeed = 0.25f;
        g_tcSlerps.clear();
        m->MoveCamera(16);
        WM(r.mvTarget, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 16.0f * 0.25f * 0.001f / angle);
        // No active camera: nothing.
        CmActive() = nullptr;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvTarget, GfLogIs({"update game"}));
        // (Not turned while it has a target, aiming or not.)
        m->m_aiming = true;
        g_upWorldDir = S_vector(9.0f, 9.0f, 9.0f);
        m->MoveCamera(16);
        WM(r.mvTarget, g_upWorldDir.x == 9.0f);
        CmActive() = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
    }

    // --- aiming: the sway ---
    {
        C_CameraModule *m = PlMoveNew();
        m->m_aiming = true;
        m->m_time = 1234;
        m->m_offsetDir = {0.125f, 0.5f, -0.25f, 0.0f};
        m->m_shakeDir = {0.01f, 0.02f, 0.03f, 0.0f};
        const S_vector base((0.6f + 0.01f) + 0.125f, (0.02f + 0.0f) + 0.5f, (0.03f + 0.8f) + -0.25f);
        g_gfActive = nullptr;
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, 0.006f, 1234)) && g_upWorldRoll == 0.0f);
        // The soldier's: 0.002 in view mode 2 (with his skill: 2 - the factor times it).
        g_gfActive = &player;
        player.playerViewMode = 2;
        player.statFactor = 0.75f;
        m->m_view = 8;                                  // (the direction still turned)
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * 0.002f, 1234)));
        m->m_view = 1;
        // Holding his breath: down 5e-6 a ms to 0.0015.
        player.playerViewMode = 0;
        player.holdingBreath = true;
        float sway = static_cast<float>(0.002f - static_cast<float>(16.0 * 5e-06));
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * sway, 1234)));
        m->MoveCamera(1000);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * 0.0015f, 1234)));
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * 0.0015f, 1234)));
        // Breathing: up 4e-6 a ms to 0.008.
        player.holdingBreath = false;
        sway = static_cast<float>(16.0 * 4e-06) + 0.0015f;
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * sway, 1234)));
        m->MoveCamera(5000);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * 0.008f, 1234)));
        // (Just past 0.008: back to it.)
        player.playerViewMode = 2;
        m->MoveCamera(16);
        player.playerViewMode = 0;
        m->MoveCamera(1600);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * 0.008f, 1234)));
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * 0.008f, 1234)));
        player.holdingBreath = true;
        sway = static_cast<float>(0.008f - static_cast<float>(16.0 * 5e-06));
        m->MoveCamera(16);
        WM(r.mvAim, PlVec(g_upWorldDir, PlSway(base, (2.0f - 0.75f) * sway, 1234)));
        player.holdingBreath = false;
        player.playerViewMode = 2;
        m->MoveCamera(16);
        player.playerViewMode = 0;
        player.statFactor = 1.0f;
        g_gfActive = nullptr;
    }

    // --- the turret's view (8) ---
    {
        C_CameraModule *m = PlMoveNew();
        I3D_frame *arm = GfFrame(FRAME_DUMMY, "arm");
        GfPlace(arm, S_vector(4.0f, 5.0f, 6.0f));
        I3D_frame *dummy = GfFrame(FRAME_DUMMY, "dummy", arm);
        GfPlace(dummy, S_vector(0.0f, 1.2f, -1.6f));
        dummy->m_localMatrix(2) = S_vector(0.0f, 0.0f, 3.0f);
        m->m_camera = dummy;
        m->m_view = 8;
        m->m_distance = 0.5f;
        I3D_frame *gameFrame = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
        m->MoveCamera(16);
        const float length = S_vector(0.0f, 1.2f, -1.6f).Magnitude();
        const S_vector at = dummy->GetMatrix()(3);
        S_vector way(4.0f - at.x, 5.0f - at.y, 6.0f - at.z);
        way = TcNorm(way, (way.x * way.x + way.z * way.z) + way.y * way.y);
        const float along = length - 0.5f;
        const S_vector want(way.x * along + at.x, way.y * along + at.y, way.z * along + at.z);
        WM(r.mvTurret, PlVec(gameFrame->m_localMatrix(3), want) && PlVec(g_upWorldDir, S_vector(0.0f, 0.0f, 1.0f)) &&
                           GfLogIs({"worlddir game 0.00000 0.00000 1.00000 0.000000", "update game"}));
        // In the first-person view (or the binoculars; or a seat with only it): at the dummy, as
        // turned.
        for (int k = 0; k < 3; ++k) {
            PlFpv(fpv, k == 0, k == 1);
            static C_UpCar seats(ACTOR_CAR);
            g_upSeatFirstOnly = k == 2;
            player.vehicle = k == 2 ? &seats : nullptr;
            player.vehicleSeat = 3;
            g_gfActive = k == 2 ? &player : nullptr;
            gameFrame->m_rot = S_quat(9.0f, 9.0f, 9.0f, 9.0f);
            g_gfLog.clear();
            m->MoveCamera(16);
            S_quat rot;
            rot.Make(dummy->GetMatrix());
            rot.Normalize();
            const std::string first = k == 2 ? "firstonly 3" : "";
            WM(r.mvTurret, g_gfLog.size() == (k == 2 ? 3u : 2u) && (k != 2 || g_gfLog[0] == first) &&
                               g_gfLog[k == 2 ? 1 : 0] == "setpos game 4.0000 6.2000 4.4000" && g_gfLog.back() == "update game" &&
                               gameFrame->m_rot.s == rot.s && gameFrame->m_rot.v.x == rot.v.x &&
                               gameFrame->m_rot.v.y == rot.v.y && gameFrame->m_rot.v.z == rot.v.z);
        }
        PlFpv(fpv, false, false);
        g_upSeatFirstOnly = false;
        // The seat's own view: shown when the dummy's model's actor's seat has only it.
        static C_UpCar owner(ACTOR_CAR);
        static S_FrameData data;
        data.actor = &owner;
        I3D_frame *model = GfFrame(FRAME_MODEL, "car");
        dummy->m_ownerModel = model;
        g_gfActive = &player;
        player.vehicle = &owner;
        player.vehicleSeat = 2;
        model->m_data = &data;
        for (int k = 0; k < 2; ++k) {
            g_upSeatFirstOnly = k == 1;
            g_gfLog.clear();
            m->MoveCamera(16);
            std::vector<std::string> want = {"firstonly 2"};
            if (k == 1)
                want = {"firstonly 2", "setpos game 4.0000 6.2000 4.4000", "firstonly 2", "seatview 2 1", "update game"};
            WM(r.mvTurret, k == 1 ? GfVecIs(g_gfLog, want) : g_gfLog.size() == 4 && g_gfLog[2] == "firstonly 2");
        }
        g_upSeatFirstOnly = false;
        // (Not without a soldier, a vehicle, the model's data or its actor.)
        for (int k = 0; k < 4; ++k) {
            g_upSeatFirstOnly = true;
            g_gfActive = k == 0 ? nullptr : &player;
            player.vehicle = k == 1 ? nullptr : &owner;
            model->m_data = k == 2 ? nullptr : &data;
            data.actor = k == 3 ? nullptr : &owner;
            g_gfLog.clear();
            m->MoveCamera(16);
            bool seatView = false;
            for (const std::string &l : g_gfLog)
                seatView = seatView || l.compare(0, 8, "seatview") == 0;
            WM(r.mvTurret, !seatView);
        }
        dummy->m_ownerModel = nullptr;
        model->m_data = &data;
        data.actor = &owner;
        g_upSeatFirstOnly = false;
        player.vehicle = nullptr;
        g_gfActive = nullptr;
        // Without the dummy: nothing.
        m->m_camera = nullptr;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvTurret, GfLogIs({"update game"}));
    }

    // --- the fade of the dummy's soldier ---
    {
        static C_Human soldier;
        static S_FrameData data;
        data.actor = &soldier;
        g_cmHumanNames[&soldier] = "soldier";
        I3D_frame *model = GfFrame(FRAME_MODEL, "soldier");
        GfPlace(model, S_vector(0.0f, 1.9f, 0.0f));
        model->m_data = &data;
        I3D_frame *body = GfFrame(FRAME_DUMMY, "body");
        GfPlace(body, S_vector(0.0f, 0.0f, 0.0f));
        soldier.bodyFrame = body;
        I3D_frame *dummy = GfFrame(FRAME_DUMMY, "camera3RD");
        dummy->m_ownerModel = model;
        struct S_Case {
            float x, y;                                 // the camera's (the segment from 0 to 2 up)
            float fadeBefore;
        };
        const S_Case cases[] = {{0.4f, 1.0f, 0.0f}, {0.4f, 1.0f, 0.95f}, {0.8f, 1.0f, 0.0f}, {1.2f, 1.0f, 0.5f},
                                {1.0f, 1.0f, 0.5f}, {1.1f, 1.0f, 0.05f}, {0.5f, 1.0f, 0.0f}, {0.0f, 2.45f, 0.0f},
                                {0.0f, 2.8f, 0.0f}};
        for (const S_Case &c : cases) {
            C_CameraModule *m = PlMoveNew();
            m->m_camera = dummy;
            m->m_pos = {c.x, c.y, 0.0f, 1.0f};           // (the game camera's place)
            m->m_fade = c.fadeBefore;
            m->m_fadeTarget = 0.3f;
            m->MoveCamera(16);
            const S_vector at(c.x, c.y, 0.0f);
            float t;
            const float d2 = SegmentDistance2(at, S_vector(0.0f, 0.0f, 0.0f), S_vector(0.0f, 1.9f + 0.1f, 0.0f), &t);
            const float target = d2 > 1.2f ? 0.0f : d2 < 0.2f ? 1.0f : 1.0f - (d2 - 0.2f);
            float fade = c.fadeBefore;
            if (target > fade)
                fade = std::min(fade + 0.1f, target);
            else if (target < fade)
                fade = std::max(fade - 0.1f, target);
            WM(r.mvFade, m->m_fadeTarget == target && m->m_fade == fade &&
                             GfVecIs(g_gfLog, {"worlddir game 0.60000 0.00000 0.80000 0.000000", "update game",
                                               CmText("unknown94 soldier %.4f", fade)}));
        }
        // Not the active camera: 0; the same: no change, nothing set.
        C_CameraModule *m = PlMoveNew();
        m->m_camera = dummy;
        CmActive() = nullptr;
        m->m_fade = m->m_fadeTarget = 0.5f;
        m->MoveCamera(16);
        WM(r.mvFade, m->m_fadeTarget == 0.0f && m->m_fade == 0.4f);
        CmActive() = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
        m->m_pos = {5.0f, 1.0f, 0.0f, 1.0f};
        m->m_fade = m->m_fadeTarget = 0.0f;
        g_gfLog.clear();
        m->MoveCamera(16);
        WM(r.mvFade, m->m_fade == 0.0f && g_gfLog.size() == 2);
        // In the first-person view: nothing.
        PlFpv(fpv, true, false);
        m->m_fadeTarget = 0.5f;
        m->MoveCamera(16);
        WM(r.mvFade, m->m_fade == 0.0f && m->m_fadeTarget == 0.5f);
        PlFpv(fpv, false, true);
        m->MoveCamera(16);
        WM(r.mvFade, m->m_fade == 0.0f);
        PlFpv(fpv, false, false);
        // Not a soldier's model (another actor, no actor, not a model): the fade towards the old
        // target, not set.
        static C_UpCar car(ACTOR_CAR);
        for (int k = 0; k < 4; ++k) {
            data.actor = k == 0 ? static_cast<C_Actor *>(&car) : k == 1 ? nullptr : &soldier;
            model->m_type = k == 2 ? FRAME_DUMMY : FRAME_MODEL;
            dummy->m_ownerModel = k == 3 ? nullptr : model;
            m->m_fade = 0.0f;
            m->m_fadeTarget = 0.5f;
            g_gfLog.clear();
            m->MoveCamera(16);
            WM(r.mvFade, m->m_fade == 0.1f && m->m_fadeTarget == 0.5f && g_gfLog.size() == 2);
        }
        model->m_type = FRAME_MODEL;
        data.actor = &soldier;
        // No dummy: nothing.
        m->m_camera = nullptr;
        m->m_fade = 0.0f;
        m->MoveCamera(16);
        WM(r.mvFade, m->m_fade == 0.0f);
    }
}

// The Junkers' gunner's field of view (private: its member pointer taken by an explicit
// instantiation, which ignores access).
float C_Junkers88::*PlGunFovMember();
template <float C_Junkers88::*M> struct S_PlGunFov {
    friend float C_Junkers88::*PlGunFovMember() { return M; }
};
template struct S_PlGunFov<&C_Junkers88::m_gunFov>;

// The scene's GetSector (0xE0): g_plSector.
I3D_sector *g_plSector;
S_vector g_plSectorAt;                                  // (where it was asked)
I3D_sector *__stdcall F_PlSector(void *, const S_vector &at)
{
    g_plSectorAt = at;
    return g_plSector;
}
I3D_sector *PlSectorNamed(const char *props)
{
    I3D_frame *f = GfFrame(FRAME_SECTOR, "sector");
    f->m_userProps = props;
    return reinterpret_cast<I3D_sector *>(f);
}

// SelectView's new camera dummy: held, the place `distance` behind it along its view, the
// distance unknown08 (the blend's -1 ended), the cut; `view` in use.
bool PlSelected(const C_CameraModule *m, I3D_frame *camera, int view, float distance, int refs)
{
    const S_matrix &w = camera->GetMatrix();
    const S_vector dir = w(2).NormalizedGame();
    const S_vector &at = w(3);
    return m->m_camera == camera && m->m_view == view && m->m_viewSettings == &m->m_settings[view] &&
           CmVec4(m->m_wantedDir, dir.x, dir.y, dir.z, w.m[2][3]) && m->m_pos.x == at.x - dir.x * distance &&
           m->m_pos.y == at.y - dir.y * distance && m->m_pos.z == at.z - dir.z * distance &&
           CmVec4(m->m_wantedPos, m->m_pos.x, m->m_pos.y, m->m_pos.z, m->m_pos.w) && !m->m_hit && m->m_cut &&
           m->m_fade == 0.0f && m->m_fadeTarget == 0.0f && camera->m_refCount == refs;
}

void PlViewTest(S_CameraResults &r, C_Human &player)
{
    C_game *game = GetGame();
    I3D_scene *scene = game->GetScene();
    C_FirstPersonView *fpv = game->GetFirstPersonView();
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_PlSector);
    I3D_sector *primary = PlSectorNamed("");
    WmField<I3D_sector *>(scene, 0x1F8) = primary;
    player.m_frame = GfFrame(FRAME_MODEL, "soldier");
    GfPlace(player.m_frame, S_vector(1.0f, 2.0f, 3.0f));
    I3D_frame *third = GfFrame(FRAME_DUMMY, "camera3RD_x", player.m_frame);
    GfPlace(third, S_vector(0.0f, 1.5f, -2.0f));
    third->m_localMatrix(2) = S_vector(0.36f, 0.48f, 0.8f);
    I3D_frame *firstView = GfFrame(FRAME_DUMMY, "cameraFPV_x", player.m_frame);
    GfPlace(firstView, S_vector(0.0f, 1.7f, 0.1f));
    player.movement = nullptr;
    player.vehicle = nullptr;
    g_gfActive = &player;

    // --- nothing with the debug camera or without the game camera; no soldier: the blend's -1 ended ---
    {
        C_CameraModule *m = PlNew();
        I3D_frame *old = m->m_camera;
        m->m_blendTime = -1;
        m->m_debugCamera = true;
        m->SelectView(0);
        WM(r.svFoot, m->m_camera == old && m->m_blendTime == -1 && g_gfLog.empty());
        m->m_debugCamera = false;
        m->m_gameCamera = nullptr;
        m->SelectView(0);
        WM(r.svFoot, m->m_camera == old && m->m_blendTime == -1 && g_gfLog.empty());
        m = PlNew();
        old = m->m_camera;
        g_gfActive = nullptr;
        m->m_blendTime = -1;
        m->m_distance = 7.0f;
        m->m_settings[1].unknown08 = 1.25f;
        m->SelectView(0);
        WM(r.svFoot, m->m_camera == old && m->m_blendTime == 0 && m->m_distance == 1.25f && g_gfLog.empty() && !m->m_cut);
        m->m_blendTime = 5;
        m->SelectView(0);
        WM(r.svFoot, m->m_blendTime == 5);
        g_gfActive = &player;
    }

    // --- on foot: behind him (by the sector: outdoors, indoors, close), in front under water ---
    {
        struct S_Case {
            const char *props;                          // (null: no sector; "=": the primary)
            bool underwater, body;
            int view;
        };
        const S_Case cases[] = {
            {"=", false, false, 1},           {"a nointerier b", false, false, 1}, {"NOINTERIER", false, false, 1},
            {"", false, false, 3},            {nullptr, false, false, 3},          {"interierclose", false, false, 4},
            {"x INTERIERCLOSE", false, false, 4}, {"nointerier interierclose", false, false, 1},
            {"=", true, false, 5},            {"nointerier", true, false, 5},      {"", true, false, 6},
            {"interierclose", true, false, 6}, {"=", false, true, 1},              {"", false, true, 3},
        };
        for (const S_Case &c : cases) {
            C_CameraModule *m = PlNew();
            m->m_unknown2AC = 2.5f;
            for (S_CameraSettings &s : m->m_settings) {
                s.distance = 3.0f;
                s.unknown08 = 0.75f;
            }
            m->m_underwater = c.underwater;
            m->m_view = 6;
            g_plSectorAt = S_vector(9.0f, 9.0f, 9.0f);
            I3D_sector *sector = !c.props ? nullptr : c.props[0] == '=' ? primary : PlSectorNamed(c.props);
            static C_Human other;
            player.movement = c.body ? reinterpret_cast<C_HumanMovement *>(&other) : nullptr;
            player.bodySector = c.body ? sector : nullptr;
            g_plSector = c.body ? nullptr : sector;
            third->m_refCount = 1;
            m->m_fade = m->m_fadeTarget = 0.5f;
            m->m_hit = &g_plRayVolume;
            m->m_cut = false;
            m->SelectView(0);
            const float distance = c.view == 1 ? 2.5f : 3.0f;
            WM(r.svFoot, PlSelected(m, third, c.view, distance, 2) && m->m_blendTime == 0 && m->m_distance == 0.75f &&
                             PlVec(g_plSectorAt, c.body ? S_vector(9.0f, 9.0f, 9.0f) : S_vector(1.0f, 2.0f, 3.0f)) &&
                             m->m_settings[1].distance == (c.view == 1 ? 2.5f : 3.0f) &&
                             GfVecIs(g_gfLog, {"find soldier camera3RD* " + CmText("%x", ENUMF_WILDMASK | FRAME_MASK_DUMMY),
                                               "release dummy"}));
            // Again: the same dummy (only the blend's -1 ended).
            m->m_distance = 9.0f;
            m->m_cut = false;
            g_gfLog.clear();
            m->SelectView(0);
            WM(r.svFoot, m->m_camera == third && !m->m_cut && m->m_distance == 0.75f && third->m_refCount == 2 &&
                             g_gfLog.size() == 1);
        }
        player.movement = nullptr;
        player.bodySector = nullptr;
        g_plSector = nullptr;
        // In the first-person view (or the binoculars): his "cameraFPV" (2).
        for (int k = 0; k < 2; ++k) {
            C_CameraModule *m = PlNew();
            m->m_settings[2].distance = 0.5f;
            m->m_blendTime = 7;
            PlFpv(fpv, k == 0, k == 1);
            firstView->m_refCount = 0;
            m->SelectView(0);
            WM(r.svFoot, PlSelected(m, firstView, 2, 0.5f, 1) && m->m_blendTime == 7 && m->m_distance == 0.5f &&
                             GfVecIs(g_gfLog, {"find soldier cameraFPV* " + CmText("%x", ENUMF_WILDMASK | FRAME_MASK_DUMMY),
                                               "release dummy"}));
        }
        PlFpv(fpv, false, false);
        // Without the dummies: his frame.
        I3D_frame *const soldierFrame = player.m_frame;
        player.m_frame = GfFrame(FRAME_MODEL, "bare");
        for (int k = 0; k < 2; ++k) {
            C_CameraModule *m = PlNew();
            PlFpv(fpv, k == 1, false);
            player.m_frame->m_refCount = 0;
            m->SelectView(0);
            WM(r.svFoot, m->m_camera == player.m_frame && m->m_view == (k == 1 ? 2 : 3) && player.m_frame->m_refCount == 1);
        }
        PlFpv(fpv, false, false);
        player.m_frame = soldierFrame;
    }

    // --- the change: the old dummy's soldier no longer faded, it let go ---
    {
        static C_Human soldier;
        static S_FrameData data;
        g_cmHumanNames[&soldier] = "soldier";
        static C_UpCar car(ACTOR_CAR);
        I3D_frame *model = GfFrame(FRAME_MODEL, "oldmodel");
        I3D_frame *old = GfFrame(FRAME_DUMMY, "old");
        old->m_ownerModel = model;
        model->m_data = &data;
        for (int k = 0; k < 5; ++k) {
            C_CameraModule *m = PlNew();
            data.actor = k == 1 ? static_cast<C_Actor *>(&car) : k == 2 ? nullptr : &soldier;
            model->m_type = k == 3 ? FRAME_DUMMY : FRAME_MODEL;
            old->m_ownerModel = k == 4 ? nullptr : model;
            old->m_refCount = 3;
            m->m_camera = old;
            m->SelectView(0);
            std::vector<std::string> want = {"find soldier camera3RD* " + CmText("%x", ENUMF_WILDMASK | FRAME_MASK_DUMMY)};
            if (k == 0)
                want.push_back("unknown94 soldier 0.0000");
            want.push_back("release old");
            WM(r.svChange, GfVecIs(g_gfLog, want) && old->m_refCount == 2 && m->m_camera == third);
        }
        model->m_type = FRAME_MODEL;
        // No old dummy: nothing let go.
        C_CameraModule *m = PlNew();
        m->m_camera = nullptr;
        third->m_refCount = 4;
        m->SelectView(0);
        WM(r.svChange, g_gfLog.size() == 1 && PlSelected(m, third, 3, m->m_settings[3].distance, 5));
    }

    // --- in a seat ---
    {
        static C_UpCar boat(ACTOR_SUBMARINE);
        alignas(16) static uint8_t junkersMem[sizeof(C_Junkers88)];
        static C_UpCar *junkers = new (junkersMem) C_UpCar(ACTOR_JUNKERS88);
        reinterpret_cast<C_Junkers88 *>(junkersMem)->*PlGunFovMember() = 0.75f;
        alignas(16) static uint8_t carMem[sizeof(C_Car)];
        static C_UpCar *carActor = new (carMem) C_UpCar(ACTOR_CAR);
        C_Car *car = reinterpret_cast<C_Car *>(carMem);
        static bool seatsMade = false;
        if (!seatsMade) {
            new (&car->m_vehicle.m_seats) std::vector<S_VehicleSeat>(3);
            seatsMade = true;
        }
        car->m_vehicle.m_unknown67C = 4.5f;
        I3D_frame *seatCamera = GfFrame(FRAME_DUMMY, "seatcam");
        GfPlace(seatCamera, S_vector(5.0f, 6.0f, 7.0f));
        seatCamera->m_localMatrix(2) = S_vector(0.0f, 0.0f, -1.0f);
        game->m_fov = 1.125f;
        player.vehicleSeat = 1;
        // No camera dummy: none (the old one kept held: Quirk kept).
        C_CameraModule *m = PlNew();
        player.vehicle = &boat;
        g_upSeatCamera = nullptr;
        m->m_view = 3;
        m->m_blendTime = -1;
        m->SelectView(0);
        WM(r.svSeat, !m->m_camera && m->m_view == 3 && m->m_blendTime == -1 && GfLogIs({"seatcam 1"}) && g_upSeatAsked == 1);
        g_upSeatCamera = seatCamera;
        struct S_Case {
            C_Actor *vehicle;
            uint32_t flags;
            bool fpv, firstOnly, hasSeatCamera;
            int view;
            float distance;                             // the view's after it
        };
        const S_Case cases[] = {
            {&boat, SEAT_FLAG_WEAPON, false, false, false, 8, 3.0f},
            {&boat, SEAT_FLAG_WEAPON | SEAT_FLAG_ARMED, false, true, false, 8, 3.0f},
            {&boat, SEAT_FLAG_WEAPON, true, false, false, 8, 3.0f},
            {carActor, SEAT_FLAG_WEAPON, false, false, false, 8, 3.0f},
            {junkers, SEAT_FLAG_WEAPON, false, false, false, 10, 3.0f},
            {junkers, SEAT_FLAG_WEAPON, true, false, false, 8, 3.0f},
            {&boat, 0, false, false, false, 7, 3.0f},
            {&boat, 0, true, false, false, 9, 3.0f},
            {carActor, 0, false, false, false, 7, 2.5f},
            {carActor, 0, false, false, true, 7, 4.5f},
            {carActor, 0x10, true, false, true, 9, 3.0f},
            {&boat, SEAT_FLAG_ARMED, false, false, false, 1, 2.5f},
            {&boat, SEAT_FLAG_ARMED | 1, true, false, false, 2, 3.0f},
        };
        for (const S_Case &c : cases) {
            m = PlNew();
            m->m_unknown2AC = 2.5f;
            for (S_CameraSettings &s : m->m_settings)
                s.distance = 3.0f;
            m->m_blendTime = 9;
            m->m_carPitch = m->m_carYaw = 0.5f;
            m->m_view = 5;
            player.vehicle = c.vehicle;
            g_upSeatFlags = c.flags;
            g_upSeatFirstOnly = c.firstOnly;
            PlFpv(fpv, c.fpv, false);
            car->m_vehicle.m_seats[1].camera = c.hasSeatCamera ? reinterpret_cast<C_CameraFrameKeeper *>(seatCamera) : nullptr;   // (only asked for)
            seatCamera->m_refCount = 0;
            m->SelectView(0);
            std::vector<std::string> want = {"seatcam 1"};
            if (!c.fpv) {
                want.push_back("firstonly 1");
                if (c.firstOnly)
                    want.push_back("seatview 1 1");
            }
            if (c.vehicle == junkers && (c.flags & SEAT_FLAG_WEAPON)) {
                want.push_back("returncontrol");
                want.push_back(CmText("fov game %.4f", c.fpv ? 0.75f : 1.125f));
            }
            want.push_back("release dummy");
            const bool passenger = !(c.flags & (SEAT_FLAG_WEAPON | SEAT_FLAG_ARMED));
            WM(r.svSeat, GfVecIs(g_gfLog, want) && PlSelected(m, seatCamera, c.view, c.distance, 1) &&
                             m->m_blendTime == (c.fpv ? 0 : 9) && m->m_settings[c.view].distance == c.distance &&
                             m->m_carPitch == (passenger ? 0.0f : 0.5f) && m->m_carYaw == (passenger ? 0.0f : 0.5f) &&
                             g_upSeatAsked == 1);
        }
        PlFpv(fpv, false, false);
        // The Junkers: no control given back by its own controller (5) or the local player.
        for (int k = 0; k < 2; ++k) {
            m = PlNew();
            player.vehicle = junkers;
            g_upSeatFlags = SEAT_FLAG_WEAPON;
            player.controllerType = k == 0 ? 5 : HUMAN_CONTROLLER_ENEMY;
            player.localPlayer = k == 1;
            m->SelectView(0);
            WM(r.svSeat, GfVecIs(g_gfLog, {"seatcam 1", "firstonly 1", "fov game 1.1250", "release dummy"}) && m->m_view == 10);
        }
        player.controllerType = HUMAN_CONTROLLER_ENEMY;
        player.localPlayer = false;
        player.vehicle = nullptr;
        player.vehicleSeat = 0;
        g_upSeatFlags = 0;
        g_upSeatCamera = nullptr;
        g_upSeatFirstOnly = false;
    }
    g_gfActive = nullptr;
    WmField<I3D_sector *>(scene, 0x1F8) = nullptr;
    g_sceneVtable[0xE0 / 4] = reinterpret_cast<void *>(&F_CmNoSector);
}

void PlModeTest(S_CameraResults &r)
{
    const std::string mask = CmText(" %x", FRAME_MASK_CAMERA);
    I3D_frame *spectators[3];
    for (int i = 0; i < 3; ++i)
        spectators[i] = CmCamera(CmText("spectator%d", i).c_str(), S_vector(0.0f, 0.0f, 0.0f));
    I3D_frame *spectator = CmCamera("spectator", S_vector(0.0f, 0.0f, 0.0f));
    g_plScene.clear();
    const uint32_t oldRole = g_gameRole;

    // --- the debug camera: from the active camera's place; the spectator's camera kept ---
    {
        C_CameraModule *m = PlNew();
        GfPlace(reinterpret_cast<I3D_frame *>(m->m_gameCamera), S_vector(3.0f, 4.0f, 5.0f));
        m->m_blendTime = -1;
        m->SetSpecialMode(CAMERA_MODE_DEBUG, true, 1);
        WM(r.smModes, m->m_debugCamera && CmVec4(m->m_wantedPos, 3.0f, 4.0f, 5.0f, 1.0f) && m->m_blendTime == -1 &&
                          g_gfLog.empty());
        m->SetSpecialMode(CAMERA_MODE_DEBUG, false, 1);
        WM(r.smModes, !m->m_debugCamera && m->m_blendTime == 0);
        m->m_blendTime = -1;
        m->m_wantedPos = {};
        m->SetSpecialMode(CAMERA_MODE_DEBUG, false, 0);
        WM(r.smModes, m->m_debugCamera && m->m_wantedPos.x == 3.0f && m->m_blendTime == -1);
        m->SetSpecialMode(CAMERA_MODE_DEBUG, true, 0);
        WM(r.smModes, !m->m_debugCamera && m->m_blendTime == 0);
        // No active camera: the place kept.
        CmActive() = nullptr;
        m->m_wantedPos = {};
        m->SetSpecialMode(CAMERA_MODE_DEBUG, true, 1);
        WM(r.smModes, m->m_debugCamera && m->m_wantedPos.x == 0.0f);
        m->m_debugCamera = false;
        // Spectating: the "spectator" camera with it, the game camera back without.
        m->m_spectating = true;
        g_plScene["spectator"] = spectator;
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_DEBUG, true, 1);
        WM(r.smModes, GfVecIs(g_gfLog, {"sfind spectator" + mask, "active spectator"}) && m->m_spectating);
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_DEBUG, true, 0);
        WM(r.smModes, GfVecIs(g_gfLog, {"active game"}) && !m->m_spectating);
        m->m_spectating = true;
        g_plScene.clear();
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_DEBUG, true, 1);
        WM(r.smModes, GfVecIs(g_gfLog, {"sfind spectator" + mask}) && m->m_spectating);
        m->m_spectating = false;
        CmActive() = reinterpret_cast<I3D_frame *>(m->m_gameCamera);
    }

    // --- an explosion: a client's shake within the distance (2.5 times it in the scripted view) ---
    {
        struct S_Case {
            uint32_t role;
            bool scripted;
            float x;
            uint32_t value;
            bool shakes;
        };
        const S_Case cases[] = {{GAME_ROLE_CLIENT, false, 4.0f, 5, true},  {GAME_ROLE_CLIENT, false, 5.0f, 5, false},
                                {GAME_ROLE_LOCAL, false, 4.9f, 5, true},   {GAME_ROLE_SERVER, false, 1.0f, 5, false},
                                {GAME_ROLE_CLIENT, true, 12.0f, 5, true},  {GAME_ROLE_CLIENT, true, 12.5f, 5, false},
                                {GAME_ROLE_CLIENT, false, 0.0f, 0, false}};
        for (const S_Case &c : cases) {
            C_CameraModule *m = PlNew();
            GfPlace(reinterpret_cast<I3D_frame *>(m->m_gameCamera), S_vector(1.0f, 2.0f, 3.0f));
            m->m_scriptedView = c.scripted ? GfFrame(FRAME_DUMMY, "scripted") : nullptr;
            g_gameRole = c.role;
            m->m_shakeTime = 7;
            const S_vector4 at = {1.0f + c.x * 0.6f, 2.0f, 3.0f + c.x * 0.8f, 0.0f};
            m->SetSpecialMode(CAMERA_MODE_EXPLOSION, &at, c.value);
            WM(r.smModes, m->m_shakeTime == (c.shakes ? 2000 : 7));
        }
        g_gameRole = oldRole;
    }

    // --- a spectator's camera (one at random), the next one ---
    {
        for (int i = 0; i < 3; ++i)
            g_plScene[CmText("spectator%d", i)] = spectators[i];
        C_CameraModule *m = PlNew();
        g_gameRole = GAME_ROLE_LOCAL;
        m->SetSpecialMode(CAMERA_MODE_SPECTATOR, true, 1);
        WM(r.smModes, !m->m_spectating && g_gfLog.empty());
        g_gameRole = GAME_ROLE_CLIENT;
        for (int seed = 1; seed < 12000; seed += 1000) {   // (MinGW: the first roll grows with the seed)
            std::srand(seed);
            const int roll = std::rand();
            std::srand(seed);
            const int pick = static_cast<int>(std::floor(static_cast<double>(static_cast<float>(
                static_cast<float>(roll) * 3.051851e-05f * static_cast<float>(3)))));
            g_gfLog.clear();
            m->m_spectating = false;
            m->SetSpecialMode(CAMERA_MODE_SPECTATOR, true, 1);
            WM(r.smModes, m->m_spectating && m->m_spectator == pick &&
                              GfVecIs(g_gfLog, {"sfind spectator0" + mask, "sfind spectator1" + mask, "sfind spectator2" + mask,
                                                "sfind spectator3" + mask, CmText("sfind spectator%d", pick) + mask,
                                                CmText("active spectator%d", pick)}));
        }
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_SPECTATOR, false, 1);
        WM(r.smModes, !m->m_spectating && GfLogIs({"active game"}));
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_SPECTATOR, true, 0);
        WM(r.smModes, !m->m_spectating && GfLogIs({"active game"}));
        // The next: on to the following, back to the first after the last.
        m->m_spectating = true;
        m->m_spectator = 1;
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_NEXT_SPECTATOR, true, 0);
        WM(r.smModes, m->m_spectator == 2 && GfLogIs({"sfind spectator2" + mask, "active spectator2"}));
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_NEXT_SPECTATOR, true, 0);
        WM(r.smModes, m->m_spectator == 0 &&
                          GfLogIs({"sfind spectator3" + mask, "sfind spectator0" + mask, "active spectator0"}));
        m->m_spectating = false;
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_NEXT_SPECTATOR, true, 0);
        WM(r.smModes, m->m_spectator == 0 && g_gfLog.empty());
        // None: off at the first; the random one none either.
        g_plScene.clear();
        m->m_spectating = true;
        m->m_spectator = 4;
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_NEXT_SPECTATOR, true, 0);
        WM(r.smModes, m->m_spectator == 0 && GfLogIs({"sfind spectator5" + mask, "sfind spectator0" + mask}));
        m->m_spectating = false;
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_SPECTATOR, false, 0);
        WM(r.smModes, m->m_spectating && m->m_spectator == 0 &&
                          GfLogIs({"sfind spectator0" + mask, "sfind spectator0" + mask}));
        g_gameRole = GAME_ROLE_SERVER;
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_SPECTATOR, true, 0);
        WM(r.smModes, !m->m_spectating && GfLogIs({"active game"}));
        g_gameRole = oldRole;
    }

    // --- the mouse look, the overloads' third arguments ---
    {
        C_CameraModule *m = PlNew();
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, true, 0);
        WM(r.smModes, !m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, false, 0);
        WM(r.smModes, m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, false, 1);
        WM(r.smModes, !m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, true, 2);
        WM(r.smModes, m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, true, 0.0f);
        WM(r.smModes, !m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, true, -0.0f);  // (its bits: not 0)
        WM(r.smModes, m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, true, static_cast<I3D_frame *>(nullptr));
        WM(r.smModes, !m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, true, m->m_camera);
        WM(r.smModes, m->m_mouseLook);
        const S_vector4 v = {};
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, &v, 0u);
        WM(r.smModes, !m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, &v, 3u);
        WM(r.smModes, m->m_mouseLook);
        m->SetSpecialMode(CAMERA_MODE_MOUSE_LOOK, static_cast<const S_vector4 *>(nullptr), 3u);
        WM(r.smModes, !m->m_mouseLook);
        m->SetSpecialMode(10, true, 1);
        m->SetSpecialMode(-1, true, 1);
        WM(r.smModes, !m->m_mouseLook && g_gfLog.empty());
    }

    // --- under water: the surface, the soldier's camera told, the view chosen ---
    {
        C_CameraModule *m = PlNew();
        m->m_blendTime = -1;
        m->SetSpecialMode(CAMERA_MODE_UNDERWATER, true, 2.5f);
        WM(r.smModes, m->m_underwater && m->m_waterLevel == 2.5f && GfLogIs({"camwater 1"}) && g_plCameraUnderWater &&
                          m->m_blendTime == 0);
        g_gfLog.clear();
        m->m_blendTime = -1;
        m->SetSpecialMode(CAMERA_MODE_UNDERWATER, false, 1.5f);
        WM(r.smModes, !m->m_underwater && m->m_waterLevel == 1.5f && GfLogIs({"camwater 0"}) && m->m_blendTime == 0);
        m->SetSpecialMode(CAMERA_MODE_UNDERWATER, true, m->m_camera);
        WM(r.smModes, m->m_underwater && m->m_waterLevel == 0.0f);
    }

    // --- the kicks (not while aiming) ---
    {
        struct S_Case {
            S_vector before, kick;
            float limit;                                // (0: kept)
        };
        const S_Case cases[] = {{{0.1f, 0.0f, 0.0f}, {0.1f, 0.0f, 0.0f}, 0.0f},
                                {{0.2f, 0.0f, 0.0f}, {0.2f, 0.1f, 0.0f}, 1.0f},
                                {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.00005f}, 0.0f},
                                {{0.5f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, 0.0f},   // (1)
                                {{0.0f, 0.0f, 0.0f}, {0.0f, 0.6f, 0.8f}, 0.0f},
                                {{0.0f, 0.0f, 0.0f}, {0.0f, 1.2f, 1.6f}, 1.0f},
                                {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.26f}, 1.0f},
                                {{0.5f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, 1.0f}};
        for (int pos = 0; pos < 2; ++pos) {
            const float most = pos ? 1.0f : 0.25f;
            for (const S_Case &c : cases) {
                C_CameraModule *m = PlNew();
                S_vector4 &v = pos ? m->m_offsetPos : m->m_offsetDir;
                v = {c.before.x, c.before.y, c.before.z, 0.5f};
                const S_vector4 kick = {c.kick.x, c.kick.y, c.kick.z, 9.0f};
                m->SetSpecialMode(pos ? CAMERA_MODE_KICK_POS : CAMERA_MODE_KICK_DIR, &kick, 0u);
                S_vector sum(c.kick.x + c.before.x, c.kick.y + c.before.y, c.kick.z + c.before.z);
                const float square = sum.Square();
                // (The direction's: also a unit one; the position's: kept.)
                const bool unit = std::fabs(square - 1.0f) < 1e-8f;
                if (!(square < 1e-8f) && (pos ? !unit && std::sqrt(square) > most : unit || std::sqrt(square) > most)) {
                    sum.Normalize();
                    if (!pos)
                        sum *= 0.25f;
                }
                const int delays = pos ? m->m_offsetPosDelay : m->m_offsetDirDelay;
                WM(r.smModes, CmVec4(v, sum.x, sum.y, sum.z, 0.5f) && delays == 200 &&
                                  (pos ? m->m_offsetDirDelay : m->m_offsetPosDelay) == 0);
                m->m_aiming = true;
                v = {c.before.x, c.before.y, c.before.z, 0.5f};
                (pos ? m->m_offsetPosDelay : m->m_offsetDirDelay) = 5;
                m->SetSpecialMode(pos ? CAMERA_MODE_KICK_POS : CAMERA_MODE_KICK_DIR, &kick, 0u);
                WM(r.smModes, CmVec4(v, c.before.x, c.before.y, c.before.z, 0.5f) &&
                                  (pos ? m->m_offsetPosDelay : m->m_offsetDirDelay) == 5);
            }
        }
    }

    // --- aiming ---
    {
        C_CameraModule *m = PlNew();
        m->m_aimTime = 300;
        m->SetSpecialMode(CAMERA_MODE_AIM, true, 0);
        WM(r.smModes, m->m_aiming && m->m_aimTime == 0);
        m->m_aimTime = 300;
        m->SetSpecialMode(CAMERA_MODE_AIM, false, 1);
        WM(r.smModes, !m->m_aiming && m->m_aimTime == 0);
    }

    // --- the projectile's view: the frame held ---
    {
        C_CameraModule *m = PlNew();
        I3D_frame *a = GfFrame(FRAME_DUMMY, "projectile");
        I3D_frame *b = GfFrame(FRAME_DUMMY, "other");
        a->m_refCount = b->m_refCount = 1;
        m->SetSpecialMode(CAMERA_MODE_PROJECTILE, true, a);
        WM(r.smModes, m->m_scriptedViewOn && m->m_scriptedView == a && a->m_refCount == 2 && g_gfLog.empty());
        m->SetSpecialMode(CAMERA_MODE_PROJECTILE, false, b);
        WM(r.smModes, !m->m_scriptedViewOn && m->m_scriptedView == b && b->m_refCount == 2 && a->m_refCount == 1 &&
                          GfLogIs({"release projectile"}));
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_PROJECTILE, true, static_cast<I3D_frame *>(nullptr));
        WM(r.smModes, !m->m_scriptedViewOn && !m->m_scriptedView && b->m_refCount == 1 && GfLogIs({"release other"}));
        g_gfLog.clear();
        m->SetSpecialMode(CAMERA_MODE_PROJECTILE, false, static_cast<I3D_frame *>(nullptr));
        WM(r.smModes, !m->m_scriptedViewOn && !m->m_scriptedView && g_gfLog.empty());
    }
    g_plScene.clear();
    g_gameRole = oldRole;
}

void PlacementTest(S_CameraResults &r)
{
    C_game *game = GetGame();
    g_sceneVtable[0x7C / 4] = reinterpret_cast<void *>(&F_PlFind);
    g_sphereTestHook = &PlSphere;
    g_sphereHookSetsHit = true;
    g_worldRayHook = &PlRay;
    g_plSphereHitOn = g_plRayHitOn = false;
    static C_Human player;
    static C_UpCar car(ACTOR_CAR), junkers(ACTOR_JUNKERS88);
    player.vehicle = nullptr;
    g_gfActive = nullptr;
    game->m_paused = false;

    // --- the placement: nothing without the game camera or the dummy ---
    {
        C_CameraModule *m = PlNew();
        m->m_camera = nullptr;
        m->PlaceCamera(16);
        WM(r.plNull, g_tcSlerps.empty() && CmVec4(m->m_dir, 0.6f, 0.0f, 0.8f, 0.25f));
        m = PlNew();
        m->m_gameCamera = nullptr;
        m->PlaceCamera(16);
        WM(r.plNull, g_tcSlerps.empty());
    }

    // --- the turn and the plain placement ---
    {
        C_CameraModule *m = PlNew();
        m->m_wantedDir = {0.0f, 0.6f, -0.8f, 0.5f};
        m->m_dir = {0.8f, 0.0f, 0.6f, 0.25f};
        m->m_settings[1].distance = 4.0f;
        m->m_unknown050 = 0.5f;
        m->m_unknown058 = -0.25f;
        m->PlaceCamera(16);
        S_vector dir(0.8f, 0.0f, 0.6f), wanted(0.0f, 0.6f, -0.8f);
        dir.Normalize();
        wanted.Normalize();
        const float turn = dir.AngleTo(wanted);
        float ratio = 16.0f * 16.0f * 0.0031415930f / turn;
        if (ratio > 1.0f)
            ratio = 1.0f;
        const S_vector look = PlLook();
        WM(r.plTurn, g_tcQuatDirs.size() == 2 && PlVec(g_tcQuatDirs[0].dir, dir) && PlVec(g_tcQuatDirs[1].dir, wanted) &&
                         g_tcSlerps.size() == 1 && g_tcSlerps[0].t == ratio && !g_tcSlerps[0].shortest &&
                         m->m_turnSpeed == 6.0f && PlVec4(m->m_dir, look) && m->m_dir.w == 0.25f);
        WM(r.plPlain, CmVec4(m->m_pos, (1.0f + 0.5f) - look.x * 4.0f, (2.0f + 0.0f) - look.y * 4.0f,
                             (3.0f + -0.25f) - look.z * 4.0f, m->m_pos.w) &&
                          m->m_waterOffset == 0.0f && g_plSpheres.empty());
        // View 7: the turn's speed 6 after its first frame; the same direction: no turn.
        m->m_view = 7;
        m->m_turnSpeed = 6.0f;
        m->m_dir = {0.8f, 0.0f, 0.6f, 0.25f};
        g_tcSlerps.clear();
        m->PlaceCamera(16);
        const float ratio7 = std::min(1.0f, 16.0f * 6.0f * 0.0031415930f / turn);
        WM(r.plTurn, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == ratio7);
        m->m_view = 1;
        m->m_wantedDir = {0.8f, 0.0f, 0.6f, 0.5f};
        m->m_dir = {0.8f, 0.0f, 0.6f, 0.25f};
        g_tcSlerps.clear();
        m->PlaceCamera(16);
        WM(r.plTurn, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 0.0f);
        // A long time: the whole turn.
        m->m_wantedDir = {0.0f, 0.6f, -0.8f, 0.5f};
        g_tcSlerps.clear();
        m->PlaceCamera(100000);
        WM(r.plTurn, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 1.0f);
        // (A little past the whole turn: 1.)
        {
            S_vector d0(0.8f, 0.0f, 0.6f), w0(0.0f, 0.6f, -0.8f);
            d0.Normalize();
            w0.Normalize();
            const int t12 = static_cast<int>(1.2f * d0.AngleTo(w0) / (16.0f * 0.0031415930f));
            m->m_dir = {0.8f, 0.0f, 0.6f, 0.25f};
            g_tcSlerps.clear();
            g_tcQuatDirs.clear();
            m->PlaceCamera(t12);
            WM(r.plTurn, g_tcSlerps.size() == 1 && g_tcSlerps[0].t == 1.0f && g_tcQuatDirs.size() == 2 &&
                             g_tcQuatDirs[0].roll == 0.0f && g_tcQuatDirs[1].roll == 0.0f);
        }
        // A slerp's result not of unit length: its direction normalized.
        g_tcSlerpResult = S_quat(0.5f, 1.0f, -1.0f, 1.0f);
        m->m_dir = {0.8f, 0.0f, 0.6f, 0.25f};
        m->PlaceCamera(16);
        WM(r.plTurn, PlVec4(m->m_dir, g_tcSlerpResult.GetDir().NormalizedGame()) &&
                         std::fabs(m->m_dir.x * m->m_dir.x + m->m_dir.y * m->m_dir.y + m->m_dir.z * m->m_dir.z - 1.0f) < 1e-5f);
        g_tcSlerpResult = S_quat(0.5f, 0.5f, -0.5f, 0.5f);
        // Under water: pushed out of the band (0.1 m past it); not in view 2.
        m->m_underwater = true;
        m->m_waterLevel = 5.0f;
        m->m_settings[1].radius = 0.2f;
        const float below = (5.0f - 0.1f) - 0.2f, above = (0.2f + 5.0f) + 0.1f;
        struct S_Case {
            float y, push;
        };
        const S_Case cases[] = {{4.8f, below - 4.8f}, {4.5f, 0.0f}, {5.2f, above - 5.2f}, {5.5f, 0.0f}, {below, 0.0f}, {above, 0.0f},
                                {4.68f, 0.0f}, {5.32f, 0.0f}, {5.0f, above - 5.0f}};
        for (const S_Case &c : cases) {
            m->m_camera->m_localMatrix(3) = S_vector(1.0f, c.y, 3.0f);
            m->m_waterOffset = 9.0f;
            m->PlaceCamera(16);
            WM(r.plPlain, m->m_waterOffset == c.push && m->m_pos.y == (c.y + c.push) - PlLook().y * 4.0f);
        }
        m->m_view = 2;
        m->m_viewSettings = &m->m_settings[2];
        m->m_settings[2].flags = 0;
        m->m_waterOffset = 9.0f;
        m->PlaceCamera(16);
        WM(r.plPlain, m->m_waterOffset == 9.0f);
        m->m_underwater = false;
        m->m_view = 1;
        m->m_viewSettings = &m->m_settings[1];
        m->m_waterOffset = 9.0f;
        m->PlaceCamera(16);
        WM(r.plPlain, m->m_waterOffset == 0.0f);
        // Near +z: turned 0.5 radians aside and back.
        m->m_wantedDir = {0.01f, 0.2f, 1.0f, 0.5f};
        m->m_dir = {0.02f, 0.0f, 1.0f, 0.25f};
        g_tcQuatDirs.clear();
        m->PlaceCamera(16);
        {
            S_quat q;
            q.Make(S_vector(0.0f, 1.0f, 0.0f), 0.5f);
            S_vector w(0.01f, 0.2f, 1.0f), d(0.02f, 0.0f, 1.0f);
            w *= q.RotationMatrix();
            d *= q.RotationMatrix();
            d.Normalize();
            w.Normalize();
            S_quat back;
            back.Make(S_vector(0.0f, 1.0f, 0.0f), -0.5f);
            S_vector look = g_tcSlerpResult.GetDir();
            w *= back.RotationMatrix();
            look *= back.RotationMatrix();
            WM(r.plTurn, g_tcQuatDirs.size() == 2 && PlVec(g_tcQuatDirs[0].dir, d) && PlVec4(m->m_wantedDir, w) &&
                             PlVec4(m->m_dir, look.NormalizedGame()));
        }
    }

    PlSweepTest(r, player, &car, &junkers);
    PlBlendTest(r);
    PlMoveTest(r, player);
    PlViewTest(r, player);
    PlModeTest(r);

    g_sphereTestHook = nullptr;
    g_sphereHookSetsHit = false;
    g_worldRayHook = nullptr;
    g_gfActive = nullptr;
    CmActive() = nullptr;
}

}  // namespace
